//! The branding scrub: the community client's own name, web addresses and mascot never ship.
//! Every build passes its outputs through here (table string blocks, file names, text files, the
//! binary files' embedded paths, the server's SQL and tsv files), and the gate ([`scan_paths`])
//! fails the build if a banned string is left.
//!
//! The words themselves are private (the client is never named in this tree): they live in a
//! rules file beside the other private data, `private-assets/brand-rules.tsv` by default
//! ([`Rules::find`]); the build refuses to run without one. Its lines are
//! `kind<TAB>from[<TAB>to]`, `#` for comments:
//!
//! - `banned  w`: the gate refuses `w` anywhere (case-insensitive; UTF-8 is fine).
//! - `prose   from  to`: a phrase rewritten in text (the name in each locale -> "Azeroth").
//! - `addr    key`: a web address holding `key` is dropped from text; `key` alone, not part of an
//!   address, takes the first `prose` rule's replacement.
//! - `word    from  to`: a whole word rewritten in text (an abbreviation).
//! - `screen  path  stock`: a branded picture replaced by a stock one under its own name.
//! - `same    from  to`: a same-length rewrite, case kept per letter, for paths, identifiers and
//!   binary files (the mascot in icon and model names).
//!
//! Two kinds of rewrite follow from that:
//! - **text** ([`Rules::scrub_text`]): table strings, SQL, tsv and text files. Lengths may change,
//!   so a table's string block is rebuilt ([`Rules::scrub_dbc`]) with every string offset
//!   remapped.
//! - **paths and binaries** ([`Rules::scrub_same_length`]): file names and the paths that models,
//!   tiles and buildings embed. Those files hold offset tables over their strings, so only
//!   same-length rewrites are made there. The file is kept and stored under the rewritten name (an
//!   alias), and every reference (table strings, model texture names, tile and building name
//!   lists) is rewritten by the same rule, so the alias and its references always agree. A path
//!   token inside text (one holding `\` or `_`) takes the same rule, never the prose one.

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};

use crate::dbc::Dbc;

/// The default place of the rules, under the repository root.
pub const RULES_FILE: &str = "private-assets/brand-rules.tsv";

/// The scrub's rules (see the module docs). Every `from` is held ASCII-lowercased.
#[derive(Clone, Debug, Default)]
pub struct Rules {
    pub banned: Vec<String>,
    pub prose: Vec<(String, String)>,
    pub addr: Vec<String>,
    pub word: Vec<(String, String)>,
    pub same: Vec<(String, String)>,
    /// `screen  path  stock`: a branded picture (a loading screen showing their mascot) replaced
    /// by a stock one under its own name, so the rows naming it keep working.
    pub screen: Vec<(String, String)>,
}

fn lower(b: &[u8]) -> Vec<u8> {
    b.to_ascii_lowercase()
}

fn find(hay: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() || needle.len() > hay.len() {
        return None;
    }
    hay.windows(needle.len()).position(|w| w == needle)
}

fn find_from(hay: &[u8], needle: &[u8], from: usize) -> Option<usize> {
    find(&hay[from.min(hay.len())..], needle).map(|i| i + from)
}

/// Same case pattern as `orig` (per letter), for a same-length replacement.
fn keep_case(orig: &[u8], repl: &[u8]) -> Vec<u8> {
    orig.iter()
        .zip(repl)
        .map(|(&o, &r)| if o.is_ascii_uppercase() { r.to_ascii_uppercase() } else { r })
        .collect()
}

fn replace_same_length(b: &mut [u8], needle: &[u8], repl: &[u8]) -> usize {
    let mut n = 0;
    let mut i = 0;
    while i + needle.len() <= b.len() {
        if b[i..i + needle.len()].eq_ignore_ascii_case(needle) {
            let r = keep_case(&b[i..i + needle.len()], repl);
            b[i..i + needle.len()].copy_from_slice(&r);
            n += 1;
            i += needle.len();
        } else {
            i += 1;
        }
    }
    n
}

const URL_CHARS: &[u8] = b"./:-_?=&%#~+";

/// Whether a token (no whitespace) is a path or an identifier (a file, icon or model name): its
/// rewrites keep lengths and agree with the aliased file names.
fn is_pathish(t: &[u8]) -> bool {
    t.iter().any(|&c| c == b'\\' || c == b'_') && !t.contains(&b'/')
}

/// Whether a file is text (scanned and rewritten whole) rather than binary.
pub fn is_text_name(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    [".txt", ".lua", ".xml", ".toc", ".html", ".htm", ".sql", ".tsv", ".wtf", ".ini", ".md", ".csv"]
        .iter()
        .any(|e| n.ends_with(e))
}

/// The community client's own screens and documents (credits, terms, connection help, readme):
/// dropped from anything we pack, referenced or not.
pub fn is_branding_doc(name: &str) -> bool {
    let n = name.to_ascii_lowercase().replace('/', "\\");
    let file = n.rsplit('\\').next().unwrap_or(&n);
    ["credits.html", "connection-help.html", "tos.html", "eula.html", "readme.txt", "credits.txt"]
        .contains(&file)
        || n.starts_with("interface\\glues\\credits\\")
}

/// The printable ASCII runs of at least `min` bytes: `(start, end)`.
fn printable_runs(b: &[u8], min: usize) -> Vec<(usize, usize)> {
    let mut out = Vec::new();
    let mut st = None;
    for (i, &c) in b.iter().enumerate() {
        let p = (0x20..0x7f).contains(&c);
        match (p, st) {
            (true, None) => st = Some(i),
            (false, Some(s)) => {
                if i - s >= min {
                    out.push((s, i));
                }
                st = None;
            }
            _ => {}
        }
    }
    if let Some(s) = st {
        if b.len() - s >= min {
            out.push((s, b.len()));
        }
    }
    out
}

/// The embedded strings of a binary file: printable runs of 8 bytes or more that end at a NUL (or
/// the file's end) and hold a `\`, `/` or `.` (a path or a file name). Compressed pixels and
/// alpha maps are printable often enough to spell a short word by chance; they are not strings.
fn string_runs(b: &[u8]) -> Vec<(usize, usize)> {
    printable_runs(b, 8)
        .into_iter()
        .filter(|&(s, e)| {
            (e == b.len() || b[e] == 0) && b[s..e].iter().any(|&c| c == b'\\' || c == b'/' || c == b'.')
        })
        .collect()
}

/// Every string start of a string block (0, and each offset after a NUL inside it).
fn string_starts(s: &[u8]) -> HashSet<u32> {
    let mut out = HashSet::new();
    if s.is_empty() {
        return out;
    }
    out.insert(0);
    for (i, &c) in s.iter().enumerate() {
        if c == 0 && i + 1 < s.len() {
            out.insert(i as u32 + 1);
        }
    }
    out
}

/// What [`Rules::scrub_dbc`] changed.
#[derive(Debug, Default, PartialEq)]
pub struct DbcScrub {
    pub strings_changed: usize,
    pub columns: Vec<usize>,
}

impl Rules {
    pub fn parse(text: &str) -> Result<Self> {
        let mut r = Rules::default();
        for (n, line) in text.lines().enumerate() {
            let line = line.trim_end_matches('\r');
            if line.trim().is_empty() || line.starts_with('#') {
                continue;
            }
            let f: Vec<&str> = line.split('\t').collect();
            let from = f.get(1).map(|s| s.to_ascii_lowercase()).unwrap_or_default();
            if from.is_empty() {
                bail!("brand rules line {}: no pattern", n + 1);
            }
            let to = || -> Result<String> {
                f.get(2)
                    .map(|s| s.to_string())
                    .with_context(|| format!("brand rules line {}: no replacement", n + 1))
            };
            match f[0] {
                "banned" => r.banned.push(from),
                "addr" => r.addr.push(from),
                "prose" => r.prose.push((from, to()?)),
                "word" => r.word.push((from, to()?)),
                "screen" => r.screen.push((f[1].to_string(), to()?)),
                "same" => {
                    let t = to()?;
                    if t.len() != from.len() || !t.is_ascii() || !from.is_ascii() {
                        bail!("brand rules line {}: a same-length rule must keep its length", n + 1);
                    }
                    r.same.push((from, t.to_ascii_lowercase()))
                }
                k => bail!("brand rules line {}: unknown kind {k:?}", n + 1),
            }
        }
        // Whatever a rule rewrites is banned too, so the gate sees what the scrub missed.
        let extra: Vec<String> = r
            .prose
            .iter()
            .chain(&r.word)
            .chain(&r.same)
            .map(|(f, _)| f.clone())
            .chain(r.addr.iter().cloned())
            .collect();
        for e in extra {
            if !r.banned.contains(&e) {
                r.banned.push(e);
            }
        }
        if r.banned.is_empty() {
            bail!("brand rules: no banned strings");
        }
        Ok(r)
    }

    pub fn load(path: &Path) -> Result<Self> {
        let t = std::fs::read_to_string(path)
            .with_context(|| format!("reading the branding rules {}", path.display()))?;
        Self::parse(&t).with_context(|| path.display().to_string())
    }

    /// `--brand-rules <file>` from `args`, else [`RULES_FILE`] under the working directory or one
    /// of its parents. None found is an error: the scrub never runs without its rules.
    pub fn find(args: &[String]) -> Result<Self> {
        if let Some(p) = args.iter().position(|a| a == "--brand-rules").and_then(|i| args.get(i + 1)) {
            return Self::load(Path::new(p));
        }
        let mut dir: Option<PathBuf> = std::env::current_dir().ok();
        while let Some(d) = dir {
            let p = d.join(RULES_FILE);
            if p.is_file() {
                return Self::load(&p);
            }
            dir = d.parent().map(Path::to_path_buf);
        }
        bail!("no branding rules: pass --brand-rules <file> or put them at {RULES_FILE} (private)")
    }

    /// A file dropped from anything we pack: their documents and screens ([`is_branding_doc`]),
    /// and any text file whose name is branded.
    pub fn is_branding_file(&self, name: &str) -> bool {
        is_branding_doc(name) || (is_text_name(name) && self.find_banned(name.as_bytes()).is_some())
    }

    /// The first banned string in `b` (case-insensitive): `(offset, which)`.
    pub fn find_banned(&self, b: &[u8]) -> Option<(usize, &str)> {
        let l = lower(b);
        self.banned
            .iter()
            .filter_map(|w| find(&l, w.as_bytes()).map(|i| (i, w.as_str())))
            .min_by_key(|(i, _)| *i)
    }

    /// The same-length rewrite for paths, identifiers and binary files.
    pub fn scrub_same_length(&self, b: &mut [u8]) -> usize {
        self.same
            .iter()
            .map(|(f, t)| replace_same_length(b, f.as_bytes(), t.as_bytes()))
            .sum()
    }

    /// A file name with the same-length rules applied (its alias).
    pub fn scrub_path(&self, p: &str) -> String {
        let mut b = p.as_bytes().to_vec();
        self.scrub_same_length(&mut b);
        String::from_utf8(b).unwrap_or_else(|_| p.to_string())
    }

    fn name_neutral(&self) -> &str {
        self.prose.first().map_or("", |(_, t)| t.as_str())
    }

    /// One whitespace-free token. `None`: dropped whole; a leading NUL: an address was dropped
    /// from its start (the rest joins the previous word).
    fn scrub_token(&self, t: &[u8]) -> Option<Vec<u8>> {
        if is_pathish(t) {
            let mut b = t.to_vec();
            self.scrub_same_length(&mut b);
            return Some(b);
        }
        let l = lower(t);
        if self.addr.iter().any(|k| find(&l, k.as_bytes()).is_some()) {
            let st = t.iter().position(|c| c.is_ascii_alphanumeric()).unwrap_or(t.len());
            let mut en = st;
            while en < t.len() && (t[en].is_ascii_alphanumeric() || URL_CHARS.contains(&t[en])) {
                en += 1;
            }
            while en > st && b".:?!".contains(&t[en - 1]) {
                en -= 1;
            }
            if t[st..en].iter().any(|&c| c == b'.' || c == b'/') {
                if st == 0 {
                    return if en >= t.len() { None } else { Some([&[0u8][..], &t[en..]].concat()) };
                }
                return Some([&t[..st], &t[en..]].concat());
            }
            // Not an address: the name written as one word.
            let mut b = t.to_vec();
            for k in &self.addr {
                while let Some(i) = find(&lower(&b), k.as_bytes()) {
                    b.splice(i..i + k.len(), self.name_neutral().bytes());
                }
            }
            return self.scrub_token(&b).or(Some(b));
        }
        let mut b = t.to_vec();
        for (w, r) in &self.word {
            let mut from = 0;
            while let Some(i) = find_from(&lower(&b), w.as_bytes(), from) {
                let before = i == 0 || !b[i - 1].is_ascii_alphanumeric();
                let after = i + w.len() >= b.len() || !b[i + w.len()].is_ascii_alphanumeric();
                if before && after {
                    b.splice(i..i + w.len(), r.bytes());
                    from = i + r.len();
                } else {
                    // Inside a longer word: left, and the gate reports it if it is branding.
                    from = i + w.len();
                }
            }
        }
        // The same-length rules for whatever is left (the mascot in prose), except those a
        // `word` rule owns: in prose those only go as whole words.
        for (f, r) in &self.same {
            if !self.word.iter().any(|(w, _)| w == f) {
                replace_same_length(&mut b, f.as_bytes(), r.as_bytes());
            }
        }
        Some(b)
    }

    /// Rewrite branded text (see the module docs). Never introduces quotes, so it is safe on SQL.
    pub fn scrub_text_bytes(&self, s: &[u8]) -> Vec<u8> {
        let mut b = s.to_vec();
        // 1. Phrases (they may span a space). One that runs on into an address ("x.org", "x/")
        //    is left for the address pass.
        for (p, r) in &self.prose {
            let mut from = 0;
            while let Some(i) = find_from(&lower(&b), p.as_bytes(), from) {
                let tail = &b[i + p.len()..];
                let addr = self.addr.iter().any(|k| p.contains(k.as_str()))
                    && matches!(tail.first(), Some(b'.' | b'/'))
                    && tail.get(1).is_some_and(|c| c.is_ascii_alphanumeric());
                if addr || is_pathish(&token_at(&b, i)) {
                    from = i + p.len();
                    continue;
                }
                b.splice(i..i + p.len(), r.bytes());
                from = i + r.len();
            }
        }
        // 2. Token by token: addresses, words, paths.
        let mut out: Vec<u8> = Vec::with_capacity(b.len());
        let mut i = 0;
        while i < b.len() {
            if b[i].is_ascii_whitespace() {
                out.push(b[i]);
                i += 1;
                continue;
            }
            let st = i;
            while i < b.len() && !b[i].is_ascii_whitespace() {
                i += 1;
            }
            match self.scrub_token(&b[st..i]) {
                Some(t) if t.first() == Some(&0) && b[st] != 0 => {
                    if out.last() == Some(&b' ') {
                        out.pop();
                    }
                    out.extend_from_slice(&t[1..]);
                }
                Some(t) => out.extend_from_slice(&t),
                None => {
                    // A dropped address takes one adjoining space with it.
                    if out.last() == Some(&b' ') {
                        out.pop();
                    } else if b.get(i) == Some(&b' ') {
                        i += 1;
                    }
                }
            }
        }
        out
    }

    pub fn scrub_text(&self, s: &str) -> String {
        String::from_utf8(self.scrub_text_bytes(s.as_bytes())).unwrap_or_else(|_| s.to_string())
    }

    /// Where a file still carries branding: its name, then a text file or table whole, a binary
    /// file in its embedded strings ([`string_runs`]).
    pub fn file_hit(&self, name: &str, b: &[u8]) -> Option<String> {
        if let Some((_, w)) = self.find_banned(name.as_bytes()) {
            return Some(format!("its name holds {w:?}"));
        }
        let snip = |s: usize, e: usize| {
            let a = s.saturating_sub(40);
            let z = (e + 40).min(b.len());
            String::from_utf8_lossy(&b[a..z]).replace(['\n', '\r', '\t', '\0'], " ")
        };
        if is_text_name(name) || name.to_ascii_lowercase().ends_with(".dbc") {
            return self
                .find_banned(b)
                .map(|(i, w)| format!("{w:?} in {:?}", snip(i, i + w.len())));
        }
        for (s, e) in string_runs(b) {
            if let Some((i, w)) = self.find_banned(&b[s..e]) {
                return Some(format!("{w:?} in {:?}", snip(s + i, s + i + w.len())));
            }
        }
        None
    }

    /// A file's bytes scrubbed: text whole, a table through [`Rules::scrub_dbc`] (left as it was
    /// if its strings cannot be rewritten safely; the gate then reports it), a binary file's
    /// embedded strings in place (same length, so its offsets hold).
    pub fn scrub_file(&self, name: &str, b: Vec<u8>) -> Vec<u8> {
        if is_text_name(name) {
            return self.scrub_text_bytes(&b);
        }
        if name.to_ascii_lowercase().ends_with(".dbc") {
            if let Ok(mut t) = Dbc::parse(&b) {
                if self.scrub_dbc(&mut t).is_ok() {
                    return t.to_bytes();
                }
            }
            return b;
        }
        let mut b = b;
        for (s, e) in string_runs(&b) {
            self.scrub_same_length(&mut b[s..e]);
        }
        b
    }

    /// Rewrite every string of a table with [`Rules::scrub_text`]: the string block is rebuilt in
    /// its original order with every string kept (referenced or not), and each string column's
    /// offsets remapped; offsets before the first rewritten string do not move. A string column:
    /// every row's value is 0 or a string start, and some value is past 1 (a 0/1 flag column stays
    /// a number). Refused (the table left as it was) when a rewritten string is referenced from a
    /// column that does not read as strings.
    pub fn scrub_dbc(&self, t: &mut Dbc) -> Result<DbcScrub> {
        if self.find_banned(&t.strings).is_none() {
            return Ok(DbcScrub::default());
        }
        let starts = string_starts(&t.strings);
        let cols: Vec<usize> = (1..t.field_count)
            .filter(|&f| {
                t.records.iter().all(|r| r[f] == 0 || starts.contains(&r[f]))
                    && t.records.iter().any(|r| r[f] > 1)
            })
            .collect();
        let mut map: HashMap<u32, u32> = HashMap::new();
        let mut rewritten: HashSet<u32> = HashSet::new();
        let mut out: Vec<u8> = Vec::with_capacity(t.strings.len());
        let mut i = 0;
        while i < t.strings.len() {
            let end = t.strings[i..].iter().position(|&c| c == 0).map_or(t.strings.len(), |p| i + p);
            let s = &t.strings[i..end];
            let new = self.scrub_text_bytes(s);
            if new != s {
                rewritten.insert(i as u32);
            }
            map.insert(i as u32, out.len() as u32);
            out.extend_from_slice(&new);
            out.push(0);
            i = end + 1;
        }
        for f in (1..t.field_count).filter(|f| !cols.contains(f)) {
            if let Some(r) = t.records.iter().find(|r| rewritten.contains(&r[f])) {
                bail!(
                    "column {f} of row {} names a rewritten string but does not read as strings",
                    r[0]
                );
            }
        }
        for r in &mut t.records {
            for &c in &cols {
                r[c] = map.get(&r[c]).copied().unwrap_or(r[c]);
            }
        }
        t.strings = out;
        Ok(DbcScrub {
            strings_changed: rewritten.len(),
            columns: cols,
        })
    }

    /// Every branded file or string in an archive (names in its `(listfile)`, then contents).
    pub fn scan_archive(&self, path: &Path) -> Result<Vec<String>> {
        let a = benilla_mpq::Archive::open(path)?;
        let list = a.read_file("(listfile)")?;
        let mut hits = Vec::new();
        for n in String::from_utf8_lossy(&list).split([';', '\r', '\n']) {
            let n = n.trim();
            if n.is_empty() || n == "(listfile)" || !a.contains(n) || a.is_delete_marker(n) {
                continue;
            }
            let b = a.read_file(n)?;
            if let Some(h) = self.file_hit(n, &b) {
                hits.push(format!("{}: {n}: {h}", path.display()));
            }
        }
        Ok(hits)
    }

    /// Every branded file or string under `paths`: archives (`.mpq`) through
    /// [`Rules::scan_archive`], folders recursively, other files by [`Rules::file_hit`] (their
    /// names relative to the folder given).
    pub fn scan_paths(&self, paths: &[PathBuf]) -> Result<Vec<String>> {
        let mut hits = Vec::new();
        for root in paths {
            let mut stack = vec![root.clone()];
            while let Some(p) = stack.pop() {
                if p.is_dir() {
                    for e in std::fs::read_dir(&p)? {
                        stack.push(e?.path());
                    }
                    continue;
                }
                let rel = p.strip_prefix(root).unwrap_or(&p);
                let rel = if rel.as_os_str().is_empty() {
                    p.file_name().map(PathBuf::from).unwrap_or_default()
                } else {
                    rel.to_path_buf()
                };
                let name = rel.to_string_lossy().to_string();
                if name.to_ascii_lowercase().ends_with(".mpq") {
                    hits.extend(self.scan_archive(&p)?);
                } else if let Some(h) = self.file_hit(&name, &std::fs::read(&p)?) {
                    hits.push(format!("{}: {h}", p.display()));
                }
            }
        }
        Ok(hits)
    }

    /// The build's gate: the archive, the server's tables and the tsv/text outputs in `out`.
    pub fn scan_outputs(&self, archive: &Path, out: &Path) -> Result<Vec<String>> {
        let mut paths = vec![archive.to_path_buf(), out.join("server-dbc")];
        for n in ["areas.tsv", "area_template.tsv", "tiles.txt", "scripts.tsv", "displays.txt"] {
            if out.join(n).is_file() {
                paths.push(out.join(n));
            }
        }
        self.scan_paths(&paths)
    }
}

/// The whitespace-free token around byte `i`.
fn token_at(b: &[u8], i: usize) -> Vec<u8> {
    let st = b[..i].iter().rposition(|c| c.is_ascii_whitespace()).map_or(0, |p| p + 1);
    let en = b[i..].iter().position(|c| c.is_ascii_whitespace()).map_or(b.len(), |p| i + p);
    b[st..en].to_vec()
}

#[cfg(test)]
pub mod tests {
    use super::*;

    /// A made-up brand standing in for the real one (whose words are private): "Murky WoW",
    /// "murky-wow.example", the abbreviation "MkWoW", the mascot "wombatix" (8 letters).
    pub const RULES: &str = "# test rules\n\
        banned\tmurky wow\n\
        banned\tmkwow\n\
        prose\tmurky wow\tAzeroth\n\
        prose\tmurky-wow\tAzeroth\n\
        prose\tSumpf-WoW\tAzeroth\n\
        prose\t泥鱼魔兽\t艾泽拉斯\n\
        addr\tmurky-wow\n\
        addr\tmurkywow\n\
        word\tmkwow\tAzeroth\n\
        same\twombatix\trivermaw\n\
        same\tmkwow\twomwx\n";

    pub fn rules() -> Rules {
        Rules::parse(RULES).unwrap()
    }

    #[test]
    fn the_name_becomes_azeroth_and_addresses_go() {
        let r = rules();
        assert_eq!(r.scrub_text("Big Murky WoW Bear"), "Big Azeroth Bear");
        assert_eq!(
            r.scrub_text("a proud member of the murky wow community"),
            "a proud member of the Azeroth community"
        );
        assert_eq!(r.scrub_text("Welcome to MkWoW!"), "Welcome to Azeroth!");
        assert_eq!(r.scrub_text("MurkyWoW"), "Azeroth");
        assert_eq!(r.scrub_text("Murky-WoW rules"), "Azeroth rules");
        assert_eq!(r.scrub_text("der Murky-WoW-Gemeinschaft"), "der Azeroth-Gemeinschaft");
        assert_eq!(r.scrub_text("Großer Sumpf-WoW-Bär"), "Großer Azeroth-Bär");
        assert_eq!(r.scrub_text("他是泥鱼魔兽社区"), "他是艾泽拉斯社区");
        assert_eq!(
            r.scrub_text("See https://forum.murky-wow.example/viewtopic.php?t=1 for more."),
            "See for more."
        );
        assert_eq!(r.scrub_text("Visit murky-wow.example."), "Visit.");
        assert_eq!(r.scrub_text("It's 'murky-wow.example', ok"), "It's '', ok");
        let row = "(1,'He is one of us, see murky-wow.example.',1)";
        assert_eq!(r.scrub_text(row), "(1,'He is one of us, see.',1)");
    }

    #[test]
    fn other_words_are_left() {
        let r = rules();
        for s in ["Sea Turtle Shell", "Murky Waters", "MkWoWx", "Aspect of the Wombat"] {
            assert_eq!(r.scrub_text(s), s);
        }
    }

    #[test]
    fn names_and_paths_keep_their_length_and_agree() {
        let r = rules();
        assert_eq!(r.scrub_text("Wombatix Pup"), "Rivermaw Pup");
        assert_eq!(
            r.scrub_path(r"Interface\Icons\INV_wombatix_albino.blp"),
            r"Interface\Icons\INV_rivermaw_albino.blp"
        );
        assert_eq!(r.scrub_text("INV_Rifle_MkWoW_02_Gray"), "INV_Rifle_WoMwX_02_Gray");
        for p in [
            r"Interface\Icons\INV_Wombatix_Gold.blp",
            r"Item\MkWoW\Rifle_MkWoW.m2",
            r"World\custom\mkwow_cellar_01.wmo",
        ] {
            assert_eq!(r.scrub_text(p), r.scrub_path(p), "text and file rules agree on {p}");
        }
        let m2 = b"\x00\x01Creature\\Wombatix\\Wombatix.blp\x00\x7f".to_vec();
        let out = r.scrub_file("Creature\\Wombatix\\Wombatix.m2", m2.clone());
        assert_eq!(out.len(), m2.len());
        assert!(r.file_hit("x.m2", &out).is_none());
        assert_eq!(&out[2..32], b"Creature\\Rivermaw\\Rivermaw.blp");
    }

    #[test]
    fn the_gate_reads_binaries_by_their_strings() {
        let r = rules();
        assert!(r.file_hit("a.blp", b"\x00\x93mkWoW\x02\x00").is_none(), "random bytes");
        assert!(r.file_hit("a.adt", b"\x01xmkwowTUxxxx\x02").is_none(), "no path");
        assert!(r.file_hit("a.m2", b"\x00World\\MurkyWoW\\Logo.blp\x00").is_some());
        assert!(r.file_hit("Credits.html", b"mkwow").is_some());
        assert!(r.file_hit(r"Interface\Icons\INV_Wombatix.blp", b"").is_some());
        assert!(r.file_hit("a.dbc", b"WDBC....Murky WoW").is_some());
        assert!(r.find_banned("Sumpf-WoW".as_bytes()).is_some(), "rule patterns are banned");
    }

    #[test]
    fn rules_are_checked() {
        assert!(Rules::parse("same\tab\tabc\nbanned\tx\n").is_err(), "length kept");
        assert!(Rules::parse("prose\tx\ty\n").unwrap().banned == vec!["x".to_string()]);
        assert!(Rules::parse("# nothing\n").is_err());
        assert!(Rules::parse("oops\tx\n").is_err());
    }

    fn table(rows: &[(u32, &str, u32, &str)]) -> Dbc {
        let mut t = Dbc {
            field_count: 4,
            records: Vec::new(),
            strings: vec![0],
        };
        for &(id, s, n, s2) in rows {
            let o = t.add_string(s.as_bytes());
            let o2 = t.add_string(s2.as_bytes());
            t.records.push(vec![id, o, n, o2]);
        }
        t
    }

    #[test]
    fn a_table_string_block_is_rebuilt_with_valid_offsets() {
        let r = rules();
        let mut t = table(&[
            (1, "Stormwind", 1, "Lion"),
            (2, "Murky WoW Tabard", 0, r"Interface\Icons\INV_Wombatix"),
            (3, "Ironforge", 1, "Welcome to mkwow, see murky-wow.example/x"),
            (4, "Ironforge", 0, ""),
        ]);
        let before: Vec<(String, String)> = t
            .records
            .iter()
            .map(|row| (t.string(row[1]).to_string(), t.string(row[3]).to_string()))
            .collect();
        let s = r.scrub_dbc(&mut t).unwrap();
        assert_eq!(s.strings_changed, 3);
        assert_eq!(s.columns, vec![1, 3]);
        let back = Dbc::parse(&t.to_bytes()).unwrap();
        for (row, (a, b)) in back.records.iter().zip(before) {
            assert_eq!(back.string(row[1]), r.scrub_text(&a));
            assert_eq!(back.string(row[3]), r.scrub_text(&b));
        }
        assert_eq!(back.string(back.row(2).unwrap()[1]), "Azeroth Tabard");
        assert_eq!(back.string(back.row(2).unwrap()[3]), r"Interface\Icons\INV_Rivermaw");
        assert_eq!(back.string(back.row(3).unwrap()[3]), "Welcome to Azeroth, see");
        assert_eq!(back.row(1).unwrap()[2], 1, "the flag column is a number");
        assert_eq!(back.row(1).unwrap()[1], 1, "offsets before the first rewrite stay");
        assert!(r.find_banned(&back.strings).is_none());
    }

    #[test]
    fn a_number_column_naming_a_rewritten_string_is_refused() {
        let r = rules();
        let mut t = table(&[(1, "Murky WoW", 5, "x"), (2, "y", 3, "z")]);
        // Column 2 holds 1 (the branded string's offset) in one row and a non-start elsewhere.
        t.records[0][2] = 1;
        t.records[1][2] = 3;
        assert!(r.scrub_dbc(&mut t).is_err());
    }

    #[test]
    fn a_clean_table_is_untouched() {
        let r = rules();
        let mut t = table(&[(1, "Stormwind", 1, "Lion")]);
        let b = t.to_bytes();
        r.scrub_dbc(&mut t).unwrap();
        assert_eq!(t.to_bytes(), b);
    }
}
