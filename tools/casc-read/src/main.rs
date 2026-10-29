//! casc-read: a small, strictly read-only reader for a local CASC install (modern WoW clients).
//!
//! It reads `.build.info`, the build config, the local `.idx` indices and `data.NNN` archives,
//! the encoding table and the root manifest (by FileDataID, plus the Jenkins name hashes where
//! the root still carries them), and decodes BLTE (plain `N` and zlib `Z`; encrypted `E`
//! chunks are reported, never fatal). It never opens anything in the install for writing.
//!
//! Usage (`<install>` is the folder holding `.build.info` and `Data\`):
//!   casc-read <install> info
//!   casc-read <install> extract <fdid> <out-file>
//!   casc-read <install> hash <path>                 (look a file up by its name hash)
//!   casc-read <install> sniff <out.tsv> [from] [to] (magic of each FileDataID in a range)
//!   casc-read <install> find <listfile.csv> <substr> (needs a community listfile, `fdid;path`)
//!   casc-read <install> extract-list <listfile.csv> <substr> <out-dir> [max]
//!   casc-read <install> extract-ids <list.txt>       (lines of `<fdid> <out-path>`)

use anyhow::{anyhow, bail, Context, Result};
use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

type EKey9 = [u8; 9];
type Key16 = [u8; 16];

struct IdxEntry {
    archive: u16,
    offset: u32,
    size: u32,
}

pub struct RootEntry {
    pub ckey: Key16,
    pub locale: u32,
    pub content: u32,
    pub name_hash: Option<u64>,
}

pub struct Casc {
    data_dir: PathBuf,
    idx: HashMap<EKey9, IdxEntry>,
    /// ckey -> (first ekey, decoded size)
    encoding: HashMap<Key16, (Key16, u64)>,
    /// fdid -> entries (one per locale/content block)
    pub root: HashMap<u32, Vec<RootEntry>>,
    pub by_hash: HashMap<u64, u32>,
    pub root_format: String,
}

#[derive(Debug)]
pub struct Decoded {
    pub data: Vec<u8>,
    /// Number of encrypted chunks that were zero-filled.
    pub encrypted_chunks: usize,
}

fn hex(s: &str) -> Result<Vec<u8>> {
    if s.len() % 2 != 0 {
        bail!("odd hex length");
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).map_err(|e| anyhow!(e)))
        .collect()
}
fn tohex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}
fn be(b: &[u8]) -> u64 {
    b.iter().fold(0u64, |a, &x| (a << 8) | x as u64)
}
fn le32(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes(b[o..o + 4].try_into().unwrap())
}

/// Bob Jenkins' hashlittle2, as WoW uses it for root name hashes (path upper-cased, `/` -> `\`).
pub fn name_hash(path: &str) -> u64 {
    jenkins2(path.to_ascii_uppercase().replace('/', "\\").as_bytes())
}

/// hashlittle2 with pc = pb = 0; returns (c << 32) | b.
pub fn jenkins2(k: &[u8]) -> u64 {
    let mut len = k.len();
    let (mut a, mut b, mut c): (u32, u32, u32);
    a = 0xdeadbeefu32.wrapping_add(len as u32);
    b = a;
    c = a; // pc = 0, pb = 0
    let rot = |x: u32, n: u32| x.rotate_left(n);
    let mut i = 0;
    let get = |o: usize| -> u32 {
        let mut w = [0u8; 4];
        for j in 0..4 {
            if o + j < k.len() {
                w[j] = k[o + j];
            }
        }
        u32::from_le_bytes(w)
    };
    while len > 12 {
        a = a.wrapping_add(get(i));
        b = b.wrapping_add(get(i + 4));
        c = c.wrapping_add(get(i + 8));
        // mix
        a = a.wrapping_sub(c); a ^= rot(c, 4); c = c.wrapping_add(b);
        b = b.wrapping_sub(a); b ^= rot(a, 6); a = a.wrapping_add(c);
        c = c.wrapping_sub(b); c ^= rot(b, 8); b = b.wrapping_add(a);
        a = a.wrapping_sub(c); a ^= rot(c, 16); c = c.wrapping_add(b);
        b = b.wrapping_sub(a); b ^= rot(a, 19); a = a.wrapping_add(c);
        c = c.wrapping_sub(b); c ^= rot(b, 4); b = b.wrapping_add(a);
        len -= 12;
        i += 12;
    }
    if len == 0 {
        return ((c as u64) << 32) | b as u64;
    }
    // Tail: bytes past the end are read as zero, which matches the byte-wise switch.
    a = a.wrapping_add(get(i) & mask(len.min(4)));
    if len > 4 { b = b.wrapping_add(get(i + 4) & mask((len - 4).min(4))); }
    if len > 8 { c = c.wrapping_add(get(i + 8) & mask(len - 8)); }
    // final
    c ^= b; c = c.wrapping_sub(rot(b, 14));
    a ^= c; a = a.wrapping_sub(rot(c, 11));
    b ^= a; b = b.wrapping_sub(rot(a, 25));
    c ^= b; c = c.wrapping_sub(rot(b, 16));
    a ^= c; a = a.wrapping_sub(rot(c, 4));
    b ^= a; b = b.wrapping_sub(rot(a, 14));
    c ^= b; c = c.wrapping_sub(rot(b, 24));
    ((c as u64) << 32) | b as u64
}
fn mask(n: usize) -> u32 {
    if n >= 4 { u32::MAX } else { (1u32 << (8 * n)) - 1 }
}

/// Decode a BLTE blob. Encrypted chunks are zero-filled and counted.
pub fn blte_decode(b: &[u8]) -> Result<Decoded> {
    if b.len() < 8 || &b[0..4] != b"BLTE" {
        bail!("not BLTE");
    }
    let hsize = be(&b[4..8]) as usize;
    let mut out = Vec::new();
    let mut enc = 0;
    let mut chunks: Vec<(usize, usize)> = Vec::new(); // (comp, decomp)
    let mut pos;
    if hsize == 0 {
        chunks.push((b.len() - 8, 0));
        pos = 8;
    } else {
        let count = be(&b[9..12]) as usize;
        let mut o = 12;
        for _ in 0..count {
            chunks.push((be(&b[o..o + 4]) as usize, be(&b[o + 4..o + 8]) as usize));
            o += 24;
        }
        pos = hsize;
    }
    for (comp, decomp) in chunks {
        let c = b.get(pos..pos + comp).ok_or_else(|| anyhow!("BLTE chunk past end"))?;
        pos += comp;
        match c[0] {
            b'N' => out.extend_from_slice(&c[1..]),
            b'Z' => {
                let mut d = flate2::read::ZlibDecoder::new(&c[1..]);
                d.read_to_end(&mut out).context("zlib")?;
            }
            b'E' => {
                enc += 1;
                out.resize(out.len() + decomp, 0);
            }
            m => bail!("unsupported BLTE chunk mode {:?}", m as char),
        }
    }
    Ok(Decoded { data: out, encrypted_chunks: enc })
}

impl Casc {
    pub fn open(install: &Path) -> Result<Casc> {
        let bi = std::fs::read_to_string(install.join(".build.info")).context(".build.info")?;
        let mut lines = bi.lines();
        let head: Vec<String> = lines
            .next()
            .unwrap_or("")
            .split('|')
            .map(|h| h.split('!').next().unwrap_or("").to_string())
            .collect();
        let col = |n: &str| head.iter().position(|h| h == n);
        let (ia, ib) = (col("Active").unwrap_or(1), col("Build Key").context("Build Key column")?);
        let row: Vec<&str> = lines
            .map(|l| l.split('|').collect::<Vec<_>>())
            .find(|r| r.get(ia) == Some(&"1"))
            .context("no active build")?;
        let bkey = row[ib];
        let data_dir = install.join("Data");
        let cfg = std::fs::read_to_string(
            data_dir.join("config").join(&bkey[0..2]).join(&bkey[2..4]).join(bkey),
        )
        .context("build config")?;
        let val = |k: &str| {
            cfg.lines()
                .find_map(|l| l.strip_prefix(&format!("{k} = ")).map(|s| s.to_string()))
        };
        let root_ckey = hex(&val("root").context("root")?)?;
        let enc_ekey = hex(val("encoding").context("encoding")?.split(' ').nth(1).context("encoding ekey")?)?;

        let mut c = Casc {
            data_dir: data_dir.join("data"),
            idx: HashMap::new(),
            encoding: HashMap::new(),
            root: HashMap::new(),
            by_hash: HashMap::new(),
            root_format: String::new(),
        };
        c.load_idx()?;
        let enc = c.read_ekey(&enc_ekey[..9])?;
        c.load_encoding(&enc.data)?;
        let rk: Key16 = root_ckey.try_into().unwrap();
        let root = c.read_ckey(&rk)?;
        c.load_root(&root.data)?;
        Ok(c)
    }

    fn load_idx(&mut self) -> Result<()> {
        let mut newest: HashMap<String, (u32, PathBuf)> = HashMap::new();
        for e in std::fs::read_dir(&self.data_dir)? {
            let p = e?.path();
            let n = p.file_name().unwrap().to_string_lossy().to_string();
            if let Some(stem) = n.strip_suffix(".idx") {
                if stem.len() != 10 { continue; }
                let bucket = stem[0..2].to_string();
                let ver = u32::from_str_radix(&stem[2..], 16).unwrap_or(0);
                if newest.get(&bucket).map_or(true, |(v, _)| ver > *v) {
                    newest.insert(bucket, (ver, p));
                }
            }
        }
        for (_, (_, p)) in newest {
            let b = std::fs::read(&p)?;
            let hhs = le32(&b, 0) as usize;
            let (spsz, spoff, eklen, offbits) = (b[12] as usize, b[13] as usize, b[14] as usize, b[15] as u32);
            if (spsz, spoff, eklen) != (4, 5, 9) {
                bail!("{}: unexpected idx layout", p.display());
            }
            let start = (8 + hhs + 15) & !15;
            let esize = le32(&b, start) as usize;
            let mut o = start + 8;
            let end = (o + esize).min(b.len());
            while o + 18 <= end {
                let k: EKey9 = b[o..o + 9].try_into().unwrap();
                let packed = be(&b[o + 9..o + 14]);
                let size = le32(&b, o + 14);
                o += 18;
                if size == 0 { continue; }
                self.idx.entry(k).or_insert(IdxEntry {
                    archive: (packed >> offbits) as u16,
                    offset: (packed & ((1u64 << offbits) - 1)) as u32,
                    size,
                });
            }
        }
        Ok(())
    }

    fn raw(&self, ekey: &[u8]) -> Result<Vec<u8>> {
        let k: EKey9 = ekey[..9].try_into().unwrap();
        let e = self.idx.get(&k).ok_or_else(|| anyhow!("ekey {} not in local storage", tohex(ekey)))?;
        let mut f = File::open(self.data_dir.join(format!("data.{:03}", e.archive)))?;
        f.seek(SeekFrom::Start(e.offset as u64 + 30))?;
        let mut buf = vec![0u8; e.size as usize - 30];
        f.read_exact(&mut buf)?;
        Ok(buf)
    }

    pub fn read_ekey(&self, ekey: &[u8]) -> Result<Decoded> {
        blte_decode(&self.raw(ekey)?)
    }

    pub fn read_ckey(&self, ckey: &Key16) -> Result<Decoded> {
        let (ek, _) = self.encoding.get(ckey).ok_or_else(|| anyhow!("ckey {} not in encoding", tohex(ckey)))?;
        self.read_ekey(ek)
    }

    /// First bytes of a file (decodes only the first BLTE chunk).
    pub fn peek(&self, ckey: &Key16) -> Result<Vec<u8>> {
        let (ek, _) = self.encoding.get(ckey).ok_or_else(|| anyhow!("no encoding"))?;
        let k: EKey9 = ek[..9].try_into().unwrap();
        let e = self.idx.get(&k).ok_or_else(|| anyhow!("not local"))?;
        let mut f = File::open(self.data_dir.join(format!("data.{:03}", e.archive)))?;
        f.seek(SeekFrom::Start(e.offset as u64 + 30))?;
        let mut h = vec![0u8; 12];
        f.read_exact(&mut h)?;
        let hsize = be(&h[4..8]) as usize;
        let (first_off, comp) = if hsize == 0 {
            (8usize, (e.size as usize - 38).min(4096))
        } else {
            let mut t = vec![0u8; 4];
            f.read_exact(&mut t)?;
            (hsize, be(&t) as usize)
        };
        f.seek(SeekFrom::Start(e.offset as u64 + 30 + first_off as u64))?;
        let mut c = vec![0u8; comp.min(1 << 20)];
        f.read_exact(&mut c)?;
        Ok(match c[0] {
            b'N' => c[1..].iter().take(64).copied().collect(),
            b'Z' => {
                let mut d = flate2::read::ZlibDecoder::new(&c[1..]);
                let mut o = vec![0u8; 64];
                let n = d.read(&mut o).unwrap_or(0);
                o.truncate(n);
                o
            }
            b'E' => b"<encrypted>".to_vec(),
            _ => Vec::new(),
        })
    }

    fn load_encoding(&mut self, b: &[u8]) -> Result<()> {
        if &b[0..2] != b"EN" { bail!("bad encoding magic"); }
        let (ck, ek) = (b[3] as usize, b[4] as usize);
        let ce_page = be(&b[5..7]) as usize * 1024;
        let ce_count = be(&b[9..13]) as usize;
        let espec = be(&b[18..22]) as usize;
        let table = 22 + espec;
        let pages = table + ce_count * 32;
        for p in 0..ce_count {
            let page = &b[pages + p * ce_page..(pages + (p + 1) * ce_page).min(b.len())];
            let mut o = 0;
            while o + 6 + ck <= page.len() {
                let n = page[o] as usize;
                if n == 0 { break; }
                let size = be(&page[o + 1..o + 6]);
                let ckey: Key16 = page[o + 6..o + 6 + 16].try_into().unwrap();
                let ekey: Key16 = page[o + 6 + ck..o + 6 + ck + 16].try_into().unwrap();
                self.encoding.insert(ckey, (ekey, size));
                o += 6 + ck + n * ek;
            }
        }
        Ok(())
    }

    fn load_root(&mut self, b: &[u8]) -> Result<()> {
        let mut o;
        let mut version = 0;
        if &b[0..4] == b"TSFM" {
            let hs = le32(b, 4);
            if hs == 0x18 {
                version = le32(b, 8);
                o = hs as usize;
            } else {
                version = 1;
                o = 12;
            }
            self.root_format = format!("MFST v{version}");
        } else {
            o = 0;
            self.root_format = "legacy".into();
        }
        while o + 12 <= b.len() {
            let n = le32(b, o) as usize;
            let (locale, content);
            if version >= 2 {
                locale = le32(b, o + 4);
                content = le32(b, o + 8) | le32(b, o + 12) | ((b[o + 16] as u32) << 17);
                o += 17;
            } else {
                content = le32(b, o + 4);
                locale = le32(b, o + 8);
                o += 12;
            }
            let mut ids = Vec::with_capacity(n);
            let mut cur: i64 = -1;
            for i in 0..n {
                cur += 1 + i32::from_le_bytes(b[o + i * 4..o + i * 4 + 4].try_into().unwrap()) as i64;
                ids.push(cur as u32);
            }
            o += n * 4;
            let legacy = version == 0;
            let has_names = legacy || content & 0x1000_0000 == 0;
            let (ck_base, nh_base) = if legacy { (o, 0) } else { (o, o + n * 16) };
            for (i, id) in ids.into_iter().enumerate() {
                let (ckey, nh): (Key16, Option<u64>) = if legacy {
                    let r = ck_base + i * 24;
                    (b[r..r + 16].try_into().unwrap(), Some(u64::from_le_bytes(b[r + 16..r + 24].try_into().unwrap())))
                } else {
                    let r = ck_base + i * 16;
                    let nh = has_names.then(|| {
                        let q = nh_base + i * 8;
                        u64::from_le_bytes(b[q..q + 8].try_into().unwrap())
                    });
                    (b[r..r + 16].try_into().unwrap(), nh)
                };
                if let Some(h) = nh { self.by_hash.entry(h).or_insert(id); }
                self.root.entry(id).or_default().push(RootEntry { ckey, locale, content, name_hash: nh });
            }
            o += if legacy { n * 24 } else { n * 16 + if has_names { n * 8 } else { 0 } };
        }
        Ok(())
    }

    /// Best content key for a FileDataID: prefer enUS (0x2) or all-locale entries.
    /// Best content key for a FileDataID: an entry stored locally first (enUS or all-locale
    /// before others), then any entry.
    pub fn ckey_for(&self, fdid: u32) -> Option<Key16> {
        let v = self.root.get(&fdid)?;
        let en = |e: &&RootEntry| e.locale & 0x2 != 0;
        v.iter()
            .filter(|e| self.is_local(&e.ckey))
            .find(en)
            .or_else(|| v.iter().find(|e| self.is_local(&e.ckey)))
            .or_else(|| v.iter().find(en))
            .or(v.first())
            .map(|e| e.ckey)
    }

    pub fn is_local(&self, ckey: &Key16) -> bool {
        self.encoding.get(ckey).map_or(false, |(ek, _)| {
            let k: EKey9 = ek[..9].try_into().unwrap();
            self.idx.contains_key(&k)
        })
    }

    pub fn read_fdid(&self, fdid: u32) -> Result<Decoded> {
        let ck = self.ckey_for(fdid).ok_or_else(|| anyhow!("FileDataID {fdid} not in root"))?;
        self.read_ckey(&ck)
    }
}

fn load_listfile(p: &Path) -> Result<Vec<(u32, String)>> {
    let s = std::fs::read_to_string(p)?;
    Ok(s.lines()
        .filter_map(|l| {
            let (a, b) = l.split_once(';')?;
            Some((a.trim().parse().ok()?, b.trim().to_string()))
        })
        .collect())
}

fn write_out(path: &Path, d: &Decoded) -> Result<()> {
    if let Some(p) = path.parent() { std::fs::create_dir_all(p)?; }
    File::create(path)?.write_all(&d.data)?;
    Ok(())
}

fn main() -> Result<()> {
    let a: Vec<String> = std::env::args().collect();
    if a.len() < 3 {
        bail!("usage: casc-read <install> info|extract|hash|sniff|find|extract-list ...");
    }
    let c = Casc::open(Path::new(&a[1]))?;
    match a[2].as_str() {
        "info" => {
            let named = c.root.values().flatten().filter(|e| e.name_hash.is_some()).count();
            let local = c.root.values().filter(|v| v.iter().any(|e| c.is_local(&e.ckey))).count();
            println!("idx entries   {}", c.idx.len());
            println!("encoding      {}", c.encoding.len());
            println!("root format   {}", c.root_format);
            println!("FileDataIDs   {} ({} stored locally)", c.root.len(), local);
            println!("named entries {named}");
            println!("max fdid      {:?}", c.root.keys().max());
        }
        "extract" => {
            let id: u32 = a[3].parse()?;
            let d = c.read_fdid(id)?;
            write_out(Path::new(&a[4]), &d)?;
            println!("{id}: {} bytes, {} encrypted chunks", d.data.len(), d.encrypted_chunks);
        }
        "extract-ids" => {
            // A text file of `<fdid> <out-path>` lines; one CASC open for the whole batch.
            for l in std::fs::read_to_string(&a[3])?.lines() {
                let Some((id, p)) = l.trim().split_once(' ') else { continue };
                let Ok(id) = id.parse::<u32>() else { continue };
                match c.read_fdid(id) {
                    Ok(d) => {
                        write_out(Path::new(p.trim()), &d)?;
                        if d.encrypted_chunks > 0 { println!("ENCRYPTED {id} ({} chunks)", d.encrypted_chunks); }
                    }
                    Err(e) => println!("SKIP {id}: {e}"),
                }
            }
        }
        "hash" => {
            let h = name_hash(&a[3]);
            println!("{:016x} -> {:?}", h, c.by_hash.get(&h));
        }
        "sniff" => {
            let from: u32 = a.get(4).map_or(Ok(0), |s| s.parse())?;
            let to: u32 = a.get(5).map_or(Ok(u32::MAX), |s| s.parse())?;
            let mut ids: Vec<u32> = c.root.keys().copied().filter(|&i| i >= from && i <= to).collect();
            ids.sort();
            let mut out = std::io::BufWriter::new(File::create(&a[3])?);
            for id in ids {
                let ck = c.ckey_for(id).unwrap();
                let size = c.encoding.get(&ck).map_or(0, |e| e.1);
                let magic = match c.peek(&ck) {
                    Ok(p) => tohex(&p[..p.len().min(16)]),
                    Err(e) => format!("ERR:{e}"),
                };
                writeln!(out, "{id}\t{size}\t{magic}")?;
            }
        }
        "find" => {
            for (id, p) in load_listfile(Path::new(&a[3]))? {
                if p.to_ascii_lowercase().contains(&a[4].to_ascii_lowercase()) {
                    println!("{id}\t{}\t{p}", if c.root.contains_key(&id) { "present" } else { "absent" });
                }
            }
        }
        "extract-list" => {
            let max: usize = a.get(6).map_or(Ok(usize::MAX), |s| s.parse())?;
            let mut n = 0;
            for (id, p) in load_listfile(Path::new(&a[3]))? {
                if n >= max { break; }
                if !p.to_ascii_lowercase().contains(&a[4].to_ascii_lowercase()) || !c.root.contains_key(&id) { continue; }
                match c.read_fdid(id) {
                    Ok(d) => {
                        write_out(&Path::new(&a[5]).join(&p), &d)?;
                        if d.encrypted_chunks > 0 { println!("ENCRYPTED {id} {p} ({} chunks)", d.encrypted_chunks); }
                        n += 1;
                    }
                    Err(e) => println!("SKIP {id} {p}: {e}"),
                }
            }
            println!("extracted {n}");
        }
        x => bail!("unknown command {x}"),
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn blte_plain_and_zlib() {
        let mut b = b"BLTE\0\0\0\0N".to_vec();
        b.extend_from_slice(b"hello");
        assert_eq!(blte_decode(&b).unwrap().data, b"hello");
        let mut z = flate2::write::ZlibEncoder::new(Vec::new(), flate2::Compression::default());
        z.write_all(b"world").unwrap();
        let mut b = b"BLTE\0\0\0\0Z".to_vec();
        b.extend_from_slice(&z.finish().unwrap());
        assert_eq!(blte_decode(&b).unwrap().data, b"world");
    }
    #[test]
    fn jenkins_known() {
        // hashlittle2("", 0, 0) = deadbeef/deadbeef
        assert_eq!(name_hash(""), 0xdeadbeef_deadbeef);
        // lookup3.c driver: hashlittle2("Four score and seven years ago", 0, 0) -> 17770551 ce7226e6.
        assert_eq!(jenkins2(b"Four score and seven years ago"), 0x17770551_ce7226e6);
    }
}
