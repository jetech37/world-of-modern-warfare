//! Crash reports: after the game exits, the launcher looks for what a crash left behind and,
//! when the player says yes (asked once, the answer kept in `crash-reports.txt`), sends it to the
//! server (`POST /api/crash`, with the game account, as the downloads go).
//!
//! The game writes `crash-<unix>.txt` (and on a native crash `crash-<unix>.dmp`, a minidump) into
//! `<install>\crashes` (`AW_CRASH_DIR`, set by [`crate::play`]). An exit code that says it crashed
//! with no report gets a short one written here. What is sent: that text, with the Windows user's
//! folder replaced by `%USERPROFILE%`, the minidump gzipped (at most [`MAX_DUMP_GZ`]), the game
//! version, the client exe's hash, the launcher version, the time and the exit code. Nothing else
//! from the PC, and never the password (it only travels as the login, as for every download).
//! Sent or not, a report moves to `crashes\old` (the newest [`KEEP_OLD`] are kept).

use std::fs;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::time::Duration;

use base64::Engine;

/// The crash folder, under the install.
pub const DIR: &str = "crashes";
/// The player's answer: `yes` or `no`.
pub const CONSENT_FILE: &str = "crash-reports.txt";
/// The largest gzipped minidump sent; a bigger one stays on the PC.
pub const MAX_DUMP_GZ: usize = 8 * 1024 * 1024;
/// The largest report text sent: its start and end are kept.
pub const MAX_REPORT: usize = 256 * 1024;
/// Reports kept in `crashes\old` after they are dealt with.
pub const KEEP_OLD: usize = 10;
/// At most this many reports go up at once (the newest); the rest are put away unsent.
pub const MAX_SEND: usize = 3;
/// Windows' code for a console closed or Ctrl+C: the player quit, nothing crashed.
const CONTROL_C_EXIT: u32 = 0xC000_013A;

/// One crash's files: `crash-<unix>.txt` and maybe `crash-<unix>.dmp`.
#[derive(Debug, Clone, PartialEq)]
pub struct Bundle {
    pub unix: u64,
    pub report: Option<PathBuf>,
    pub dump: Option<PathBuf>,
}

impl Bundle {
    fn files(&self) -> impl Iterator<Item = &PathBuf> {
        self.report.iter().chain(self.dump.iter())
    }
}

/// The crash files waiting in `dir` (not yet sent or put away), oldest first.
pub fn pending(dir: &Path) -> Vec<Bundle> {
    let mut found: Vec<Bundle> = Vec::new();
    let Ok(entries) = fs::read_dir(dir) else {
        return found;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let Some((unix, ext)) = parse_name(&name) else {
            continue;
        };
        if !entry.file_type().map(|t| t.is_file()).unwrap_or(false) {
            continue;
        }
        let i = match found.iter().position(|b| b.unix == unix) {
            Some(i) => i,
            None => {
                found.push(Bundle {
                    unix,
                    report: None,
                    dump: None,
                });
                found.len() - 1
            }
        };
        match ext {
            "txt" => found[i].report = Some(entry.path()),
            _ => found[i].dump = Some(entry.path()),
        }
    }
    found.sort_by_key(|b| b.unix);
    found
}

/// `crash-<unix>.txt` or `.dmp`: the time and the extension.
fn parse_name(name: &str) -> Option<(u64, &str)> {
    let rest = name.strip_prefix("crash-")?;
    let (unix, ext) = rest.split_once('.')?;
    if !matches!(ext, "txt" | "dmp") || unix.is_empty() || !unix.bytes().all(|b| b.is_ascii_digit())
    {
        return None;
    }
    Some((unix.parse().ok()?, ext))
}

/// Whether an exit code means the game crashed (0 is a normal exit, and closing its console or
/// Ctrl+C is the player's doing).
pub fn is_crash(code: Option<i32>) -> bool {
    match code {
        Some(0) => false,
        Some(c) => c as u32 != CONTROL_C_EXIT,
        None => true,
    }
}

/// An exit code as people say it: `0xC0000005 (access violation)`, `101 (panic)`.
pub fn describe_exit(code: Option<i32>) -> String {
    let Some(code) = code else {
        return "no exit code".into();
    };
    let c = code as u32;
    let meaning = match c {
        0xC000_0005 => Some("access violation"),
        0xC000_00FD => Some("stack overflow"),
        0xC000_0409 => Some("fail fast (abort)"),
        0xC000_0374 => Some("heap corruption"),
        0xC000_0017 => Some("out of memory"),
        0xC000_001D => Some("illegal instruction"),
        0xE06D_7363 => Some("C++ exception"),
        101 => Some("panic"),
        _ => None,
    };
    let shown = if c > 0xFFFF {
        format!("0x{c:08X}")
    } else {
        c.to_string()
    };
    match meaning {
        Some(m) => format!("{shown} ({m})"),
        None => shown,
    }
}

/// The short report for a crash the game wrote nothing about (it died before it could, or an
/// abort skipped its handlers): the exit code says what there is to say.
pub fn exit_report(code: Option<i32>, game_version: &str, unix: u64) -> String {
    format!(
        "World of Modern Warfare crash report\n\
         kind:      exit\n\
         time:      {unix} unix\n\
         game:      {game_version}\n\
         exit:      {}\n\
         \n\
         The game exited abnormally and wrote no crash report of its own.\n",
        describe_exit(code)
    )
}

/// What the report's `kind:` line says (`panic`, `native`), else `exit`.
pub fn kind_of(report: &str) -> &'static str {
    for line in report.lines().take(20) {
        if let Some(kind) = line.strip_prefix("kind:") {
            return match kind.trim() {
                "panic" => "panic",
                "native" => "native",
                _ => "exit",
            };
        }
    }
    "exit"
}

/// The text with each of `paths` (the Windows user's folder) replaced by its stand-in, in any
/// case and with either slash.
pub fn redact(text: &str, paths: &[(String, &str)]) -> String {
    let mut out = text.to_string();
    for (path, stand_in) in paths {
        if path.len() < 4 {
            continue;
        }
        for form in [path.replace('/', "\\"), path.replace('\\', "/")] {
            out = replace_ignoring_case(&out, &form, stand_in);
        }
    }
    out
}

fn replace_ignoring_case(text: &str, needle: &str, with: &str) -> String {
    let lower = text.to_ascii_lowercase();
    let needle_lower = needle.to_ascii_lowercase();
    let mut out = String::with_capacity(text.len());
    let mut at = 0;
    while let Some(i) = lower[at..].find(&needle_lower) {
        out.push_str(&text[at..at + i]);
        out.push_str(with);
        at += i + needle.len();
    }
    out.push_str(&text[at..]);
    out
}

/// The Windows user's folders a report may name, with what stands in for them.
fn private_paths() -> Vec<(String, &'static str)> {
    let mut paths = Vec::new();
    for (var, stand_in) in [
        ("LOCALAPPDATA", "%LOCALAPPDATA%"),
        ("APPDATA", "%APPDATA%"),
        ("USERPROFILE", "%USERPROFILE%"),
    ] {
        if let Some(p) = std::env::var_os(var) {
            paths.push((p.to_string_lossy().into_owned(), stand_in));
        }
    }
    paths
}

/// A report cut to [`MAX_REPORT`] bytes: its start (what crashed) and its end (the last log
/// lines).
pub fn trim_report(text: &str) -> String {
    if text.len() <= MAX_REPORT {
        return text.to_string();
    }
    let head = floor_char(text, MAX_REPORT * 3 / 4);
    let tail = ceil_char(text, text.len() - MAX_REPORT / 5);
    format!(
        "{}\n... [{} bytes cut] ...\n{}",
        &text[..head],
        tail - head,
        &text[tail..]
    )
}

fn floor_char(s: &str, mut i: usize) -> usize {
    while !s.is_char_boundary(i) {
        i -= 1;
    }
    i
}

fn ceil_char(s: &str, mut i: usize) -> usize {
    while !s.is_char_boundary(i) {
        i += 1;
    }
    i
}

/// The minidump gzipped, or None when that is over `cap` bytes (or it can't be read).
pub fn gzip_file(path: &Path, cap: usize) -> Option<Vec<u8>> {
    let mut input = fs::File::open(path).ok()?;
    let mut encoder = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::best());
    let mut buf = vec![0u8; 1 << 16];
    loop {
        let n = input.read(&mut buf).ok()?;
        if n == 0 {
            break;
        }
        encoder.write_all(&buf[..n]).ok()?;
        if encoder.get_ref().len() > cap {
            return None;
        }
    }
    let out = encoder.finish().ok()?;
    (out.len() <= cap).then_some(out)
}

/// What goes with a report.
#[derive(Debug, Clone, PartialEq)]
pub struct Meta {
    pub game_version: String,
    pub client_sha256: String,
    pub crashed_at: u64,
    pub exit_code: Option<i32>,
}

/// The JSON `POST /api/crash` takes: no account in it (the login says who), no file but these.
pub fn body(meta: &Meta, report: &str, dump_gz: Option<&[u8]>) -> serde_json::Value {
    let mut body = serde_json::json!({
        "kind": kind_of(report),
        "report": report,
        "game_version": meta.game_version,
        "client_sha256": meta.client_sha256,
        "launcher_version": env!("CARGO_PKG_VERSION"),
        "crashed_at": meta.crashed_at,
        "exit_code": meta.exit_code.map(|c| c as u32),
    });
    if let Some(dump) = dump_gz {
        body["dump_gz"] = base64::engine::general_purpose::STANDARD
            .encode(dump)
            .into();
    }
    body
}

/// `POST /api/crash` on `host` with the account's `Authorization`: the report's id, or the status
/// (0: no answer) and what the server said.
pub fn send(
    host: &str,
    authorization: &str,
    body: &serde_json::Value,
) -> Result<String, (u16, String)> {
    let reply = ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(20))
        .timeout(Duration::from_secs(180))
        .build()
        .post(&format!("{host}/api/crash"))
        .set("Authorization", authorization)
        .send_json(body);
    let message = |r: ureq::Response| {
        r.into_json::<serde_json::Value>()
            .ok()
            .map(|v| {
                (
                    v["message"].as_str().unwrap_or("").to_string(),
                    v["id"].as_str().unwrap_or("").to_string(),
                )
            })
            .unwrap_or_default()
    };
    match reply {
        Ok(r) => Ok(message(r).1),
        Err(ureq::Error::Status(status, r)) => {
            let (text, _) = message(r);
            Err((
                status,
                if text.is_empty() {
                    format!("the server said no (error {status})")
                } else {
                    text
                },
            ))
        }
        Err(e) => Err((0, format!("can't reach the server: {e}"))),
    }
}

/// The player's standing answer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Consent {
    Yes,
    No,
    Ask,
}

pub fn consent(install: &Path) -> Consent {
    parse_consent(&fs::read_to_string(install.join(CONSENT_FILE)).unwrap_or_default())
}

fn parse_consent(text: &str) -> Consent {
    match text.trim().to_ascii_lowercase().as_str() {
        "yes" => Consent::Yes,
        "no" => Consent::No,
        _ => Consent::Ask,
    }
}

/// Ask once, and keep the answer. Enter is yes; no console to ask in is no (this time).
fn ask_consent(install: &Path) -> bool {
    println!(
        "\nA crash report helps us fix this. It holds the error and where in the game it \
         happened,\nthe game's last log lines, the game version, your Windows version and \
         graphics card,\nand your account name. After a hard crash it also holds a minidump: \
         the state of the\ngame's threads at that moment (raw memory, which can hold \
         fragments of what the game\nwas handling then). Nothing else from your PC is sent."
    );
    print!("Send crash reports when the game crashes? [Y/n] ");
    io::stdout().flush().ok();
    let mut line = String::new();
    if io::stdin().read_line(&mut line).unwrap_or(0) == 0 {
        return false;
    }
    let yes = !line.trim().to_ascii_lowercase().starts_with('n');
    let _ = fs::write(install.join(CONSENT_FILE), if yes { "yes" } else { "no" });
    println!(
        "{} (Asked once: delete {CONSENT_FILE} in {} to be asked again.)",
        if yes {
            "Crash reports will be sent."
        } else {
            "No crash reports will be sent."
        },
        install.display()
    );
    yes
}

/// After the game exited (`exit` is its code and when it started, unix seconds), or at the
/// launcher's start (`exit` None: reports a run left behind when the launcher was closed): offer
/// what is waiting. True when something was offered (the caller then pauses so the player can
/// read it).
pub fn offer(
    install: &Path,
    host: &str,
    authorization: &str,
    game_version: &str,
    exit: Option<(Option<i32>, u64)>,
) -> bool {
    let dir = install.join(DIR);
    let mut bundles = pending(&dir);
    let code_for = |b: &Bundle| {
        exit.filter(|(_, started)| b.unix + 2 >= *started)
            .and_then(|(c, _)| c)
    };
    if let Some((code, started)) = exit {
        let fresh = bundles.iter().any(|b| b.unix + 2 >= started);
        if is_crash(code) && !fresh {
            let unix = now();
            let path = dir.join(format!("crash-{unix}.txt"));
            if fs::create_dir_all(&dir).is_ok()
                && fs::write(&path, exit_report(code, game_version, unix)).is_ok()
            {
                bundles = pending(&dir);
            }
        }
        if bundles.is_empty() {
            return false;
        }
        if is_crash(code) {
            println!("\nThe game crashed (exit code {}).", describe_exit(code));
        } else {
            println!("\nThe game left a crash report.");
        }
    } else if bundles.is_empty() {
        return false;
    } else {
        println!("\nThe game crashed last time.");
    }
    let yes = match consent(install) {
        Consent::Yes => true,
        Consent::No => false,
        Consent::Ask => ask_consent(install),
    };
    let sha = client_sha256(install);
    let count = bundles.len();
    for (i, bundle) in bundles.iter().enumerate() {
        if yes && i + MAX_SEND >= count {
            let meta = Meta {
                game_version: game_version.to_string(),
                client_sha256: sha.clone(),
                crashed_at: bundle.unix,
                exit_code: code_for(bundle),
            };
            match send_bundle(host, authorization, bundle, &meta) {
                Ok(id) if id.is_empty() => println!("Crash report sent. Thanks!"),
                Ok(id) => println!("Crash report sent (#{id}). Thanks!"),
                // Not reachable now: keep it for the next start.
                Err((0, message)) => {
                    println!("Couldn't send the crash report ({message}); will try next time.");
                    continue;
                }
                Err((_, message)) => println!("The crash report wasn't taken: {message}"),
            }
        }
        put_away(&dir, bundle);
    }
    prune(&dir.join("old"), KEEP_OLD);
    true
}

fn send_bundle(
    host: &str,
    authorization: &str,
    bundle: &Bundle,
    meta: &Meta,
) -> Result<String, (u16, String)> {
    let mut report = bundle
        .report
        .as_ref()
        .and_then(|p| fs::read(p).ok())
        .map(|b| String::from_utf8_lossy(&b).into_owned())
        .unwrap_or_else(|| exit_report(meta.exit_code, &meta.game_version, bundle.unix));
    let dump = bundle.dump.as_ref().and_then(|p| {
        let gz = gzip_file(p, MAX_DUMP_GZ);
        if gz.is_none() {
            let size = fs::metadata(p).map(|m| m.len()).unwrap_or(0);
            report.push_str(&format!(
                "\nminidump: not sent (over {} MB gzipped; {} MB raw, kept on the player's PC)\n",
                MAX_DUMP_GZ >> 20,
                size >> 20
            ));
        }
        gz
    });
    let report = trim_report(&redact(&report, &private_paths()));
    send(host, authorization, &body(meta, &report, dump.as_deref()))
}

/// Into `crashes\old`, sent or not, so it isn't offered again.
fn put_away(dir: &Path, bundle: &Bundle) {
    let old = dir.join("old");
    let _ = fs::create_dir_all(&old);
    for f in bundle.files() {
        if let Some(name) = f.file_name()
            && fs::rename(f, old.join(name)).is_err()
        {
            let _ = fs::remove_file(f);
        }
    }
}

/// Keep the newest `keep` crashes in `old`.
fn prune(old: &Path, keep: usize) {
    let bundles = pending(old);
    let extra = bundles.len().saturating_sub(keep);
    for b in &bundles[..extra] {
        for f in b.files() {
            let _ = fs::remove_file(f);
        }
    }
}

/// The client exe's hash as this install last checked it (state.json), which names the build:
/// its PDB is archived under it.
fn client_sha256(install: &Path) -> String {
    fs::read(install.join("state.json"))
        .ok()
        .and_then(|b| serde_json::from_slice::<serde_json::Value>(&b).ok())
        .and_then(|v| {
            v["files"]["client/benilla-mw2.exe"]["sha256"]
                .as_str()
                .map(str::to_string)
        })
        .unwrap_or_default()
}

fn now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("aw-crash-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn finds_the_crash_files_by_time() {
        let dir = temp("pending");
        for name in [
            "crash-200.txt",
            "crash-200.dmp",
            "crash-100.txt",
            "crash-x.txt",
            "crash-300.log",
            "other.txt",
        ] {
            fs::write(dir.join(name), "x").unwrap();
        }
        fs::create_dir_all(dir.join("old")).unwrap();
        fs::write(dir.join("old").join("crash-50.txt"), "x").unwrap();
        let found = pending(&dir);
        assert_eq!(found.len(), 2, "{found:?}");
        assert_eq!(found[0].unix, 100);
        assert_eq!(found[0].dump, None);
        assert_eq!(found[1].unix, 200);
        assert_eq!(found[1].report, Some(dir.join("crash-200.txt")));
        assert_eq!(found[1].dump, Some(dir.join("crash-200.dmp")));

        put_away(&dir, &found[1]);
        assert_eq!(pending(&dir).len(), 1);
        assert_eq!(pending(&dir.join("old")).len(), 2);
        prune(&dir.join("old"), 1);
        let left = pending(&dir.join("old"));
        assert_eq!(left.len(), 1);
        assert_eq!(left[0].unix, 200); // the newest stays
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn what_counts_as_a_crash() {
        assert!(!is_crash(Some(0)));
        assert!(!is_crash(Some(0xC000_013Au32 as i32))); // the console closed
        assert!(is_crash(Some(101)));
        assert!(is_crash(Some(1)));
        assert!(is_crash(Some(0xC000_0005u32 as i32)));
        assert_eq!(
            describe_exit(Some(0xC000_0005u32 as i32)),
            "0xC0000005 (access violation)"
        );
        assert_eq!(describe_exit(Some(101)), "101 (panic)");
        assert_eq!(describe_exit(Some(3)), "3");
    }

    #[test]
    fn the_kind_comes_from_the_report() {
        assert_eq!(
            kind_of("World of Modern Warfare crash report\nkind:      panic\n"),
            "panic"
        );
        assert_eq!(kind_of("x\nkind: native\n"), "native");
        assert_eq!(kind_of(&exit_report(Some(1), "v1", 5)), "exit");
        assert_eq!(kind_of("whatever"), "exit");
        let text = exit_report(Some(0xC000_0409u32 as i32), "2026-10-18 12:00", 42);
        assert!(
            text.contains("exit:      0xC0000409 (fail fast (abort))"),
            "{text}"
        );
        assert!(text.contains("game:      2026-10-18 12:00"));
    }

    #[test]
    fn the_users_folder_is_left_out() {
        let paths = vec![
            (
                r"C:\Users\Alice\AppData\Local".to_string(),
                "%LOCALAPPDATA%",
            ),
            (r"C:\Users\Alice".to_string(), "%USERPROFILE%"),
        ];
        let text = "exe c:\\users\\alice\\AppData\\Local\\WorldOfModernWarfare\\client\nWOW_DATA=C:/Users/Alice/Games\nC:\\Users\\Bob";
        assert_eq!(
            redact(text, &paths),
            "exe %LOCALAPPDATA%\\WorldOfModernWarfare\\client\nWOW_DATA=%USERPROFILE%/Games\nC:\\Users\\Bob"
        );
    }

    #[test]
    fn a_long_report_keeps_its_start_and_end() {
        let short = "kind: panic\n";
        assert_eq!(trim_report(short), short);
        let long = format!("START{}END", "é".repeat(MAX_REPORT));
        let cut = trim_report(&long);
        assert!(cut.len() <= MAX_REPORT + 100, "{}", cut.len());
        assert!(cut.starts_with("START"));
        assert!(cut.ends_with("END"));
        assert!(cut.contains("bytes cut"));
    }

    #[test]
    fn dumps_are_gzipped_and_capped() {
        let dir = temp("gzip");
        let path = dir.join("crash-1.dmp");
        fs::write(&path, b"MDMP".repeat(10_000)).unwrap();
        let gz = gzip_file(&path, MAX_DUMP_GZ).expect("small enough");
        assert_eq!(&gz[..2], &[0x1f, 0x8b]);
        let mut back = Vec::new();
        flate2::read::GzDecoder::new(&gz[..])
            .read_to_end(&mut back)
            .unwrap();
        assert_eq!(back, b"MDMP".repeat(10_000));
        // Random bytes don't compress: over the cap, nothing is sent.
        let noise: Vec<u8> = (0..20_000u32)
            .map(|i| (i.wrapping_mul(2654435761) >> 13) as u8)
            .collect();
        fs::write(&path, noise).unwrap();
        assert_eq!(gzip_file(&path, 1000), None);
        assert_eq!(gzip_file(&dir.join("missing.dmp"), 1000), None);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn the_request_holds_the_report_and_nothing_private() {
        let meta = Meta {
            game_version: "2026-10-18 12:00".into(),
            client_sha256: "ab".repeat(32),
            crashed_at: 1_790_000_000,
            exit_code: Some(0xC000_0005u32 as i32),
        };
        let report = "World of Modern Warfare crash report\nkind:      native\n";
        let b = body(&meta, report, Some(&[0x1f, 0x8b, 1, 2]));
        assert_eq!(b["kind"], "native");
        assert_eq!(b["report"], report);
        assert_eq!(b["game_version"], "2026-10-18 12:00");
        assert_eq!(b["client_sha256"], "ab".repeat(32));
        assert_eq!(b["crashed_at"], 1_790_000_000u64);
        assert_eq!(b["exit_code"], 0xC000_0005u32);
        assert_eq!(b["dump_gz"], "H4sBAg==");
        assert_eq!(b["launcher_version"], env!("CARGO_PKG_VERSION"));
        let keys: Vec<_> = b.as_object().unwrap().keys().cloned().collect();
        assert_eq!(keys.len(), 8, "{keys:?}");
        assert!(
            !keys
                .iter()
                .any(|k| k.contains("pass") || k.contains("account"))
        );
        assert!(body(&meta, report, None).get("dump_gz").is_none());
    }

    #[test]
    fn sends_with_the_account() {
        let (host, server) = crate::tests::serve_once(
            "201 Created",
            r#"{"message": "Thanks", "id": "20261018-120000-abcdef"}"#,
        );
        let auth = crate::basic("Tester", "secret12");
        let meta = Meta {
            game_version: "v".into(),
            client_sha256: String::new(),
            crashed_at: 5,
            exit_code: Some(101),
        };
        let b = body(&meta, "kind: panic\n", None);
        assert_eq!(
            send(&host, &auth, &b),
            Ok("20261018-120000-abcdef".to_string())
        );
        let request = server.join().unwrap();
        assert!(request.starts_with("POST /api/crash "), "{request}");
        assert!(
            request.contains(&format!("Authorization: {auth}")),
            "{request}"
        );
        let sent: serde_json::Value =
            serde_json::from_str(request.split("\r\n\r\n").nth(1).unwrap()).unwrap();
        assert_eq!(sent, b);
    }

    #[test]
    fn says_what_the_server_said() {
        let (host, server) =
            crate::tests::serve_once("429 Too Many Requests", r#"{"message": "Too many"}"#);
        let b = serde_json::json!({});
        assert_eq!(
            send(&host, "Basic x", &b),
            Err((429, "Too many".to_string()))
        );
        server.join().unwrap();
    }

    #[test]
    fn the_answer_is_kept() {
        assert_eq!(parse_consent("yes\n"), Consent::Yes);
        assert_eq!(parse_consent(" NO "), Consent::No);
        assert_eq!(parse_consent(""), Consent::Ask);
        let dir = temp("consent");
        assert_eq!(consent(&dir), Consent::Ask);
        fs::write(dir.join(CONSENT_FILE), "no").unwrap();
        assert_eq!(consent(&dir), Consent::No);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_crash_without_a_report_gets_one_and_a_clean_exit_none() {
        let install = temp("offer");
        fs::write(install.join(CONSENT_FILE), "no").unwrap();
        let started = now();
        // A clean exit: nothing to offer, nothing written.
        assert!(!offer(
            &install,
            "http://127.0.0.1:9",
            "",
            "v",
            Some((Some(0), started))
        ));
        assert!(pending(&install.join(DIR)).is_empty());
        // A crash with no report: one is written, and (the player said no) put away unsent.
        assert!(offer(
            &install,
            "http://127.0.0.1:9",
            "",
            "v",
            Some((Some(1), started))
        ));
        assert!(pending(&install.join(DIR)).is_empty());
        let old = pending(&install.join(DIR).join("old"));
        assert_eq!(old.len(), 1);
        let text = fs::read_to_string(old[0].report.as_ref().unwrap()).unwrap();
        assert!(text.contains("kind:      exit"), "{text}");
        let _ = fs::remove_dir_all(&install);
    }
}
