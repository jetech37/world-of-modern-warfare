//! A minimal MPQ writer: format V1, every file stored whole (no compression, encryption or sector
//! table), the hash and block tables encrypted with the standard keys, and a `(listfile)`. The
//! plainest shape `benilla-mpq` reads, and StormLib (the server's extractors) reads it too. The
//! same writer as the WotLK layer's (`client/src/wotlk/mpq_write.rs`).

use std::io::Write;

/// The MPQ crypt table (`0x500` words), as every MPQ reader builds it.
fn crypt_table() -> &'static [u32; 0x500] {
    static TABLE: std::sync::OnceLock<[u32; 0x500]> = std::sync::OnceLock::new();
    TABLE.get_or_init(|| {
        let mut t = [0u32; 0x500];
        let mut seed: u32 = 0x0010_0001;
        for i in 0..0x100 {
            let mut idx = i;
            for _ in 0..5 {
                seed = (seed * 125 + 3) % 0x2A_AAAB;
                let a = (seed & 0xFFFF) << 16;
                seed = (seed * 125 + 3) % 0x2A_AAAB;
                let b = seed & 0xFFFF;
                t[idx] = a | b;
                idx += 0x100;
            }
        }
        t
    })
}

/// The MPQ string hash of `name` (any case, `/` as `\`) for hash type `kind` (0 index, 1 and 2 the
/// name checks, 3 a file key).
pub fn hash_string(name: &str, kind: u32) -> u32 {
    let t = crypt_table();
    let (mut s1, mut s2) = (0x7FED_7FEDu32, 0xEEEE_EEEEu32);
    for c in name.bytes() {
        let c = if c == b'/' {
            b'\\'
        } else {
            c.to_ascii_uppercase()
        };
        let v = t[((kind << 8) + u32::from(c)) as usize];
        s1 = v ^ s1.wrapping_add(s2);
        s2 = u32::from(c)
            .wrapping_add(s1)
            .wrapping_add(s2)
            .wrapping_add(s2 << 5)
            .wrapping_add(3);
    }
    s1
}

fn encrypt(words: &mut [u32], mut key: u32) {
    let t = crypt_table();
    let mut seed2 = 0xEEEE_EEEEu32;
    for w in words {
        seed2 = seed2.wrapping_add(t[0x400 + (key & 0xFF) as usize]);
        let plain = *w;
        *w = plain ^ key.wrapping_add(seed2);
        key = ((!key) << 0x15).wrapping_add(0x1111_1111) | (key >> 0x0B);
        seed2 = plain
            .wrapping_add(seed2)
            .wrapping_add(seed2 << 5)
            .wrapping_add(3);
    }
}

const FLAG_EXISTS: u32 = 0x8000_0000;

/// Stream an archive holding `files` (`(internal path, bytes)`) plus a `(listfile)` to `out`. A
/// later duplicate path (any case, either separator) replaces an earlier one. Returns the size.
pub fn write_mpq_to<W: Write>(files: &[(String, Vec<u8>)], out: &mut W) -> std::io::Result<u64> {
    let sized: Vec<(String, u64)> = files
        .iter()
        .map(|(n, b)| (n.clone(), b.len() as u64))
        .collect();
    let mut last = std::collections::HashMap::new();
    for (i, (n, _)) in files.iter().enumerate() {
        last.insert(n.replace('/', "\\").to_ascii_lowercase(), i);
    }
    write_mpq_with(&sized, out, |name| {
        let i = last[&name.to_ascii_lowercase()];
        Ok(files[i].1.clone())
    })
}

/// [`write_mpq_to`] over files fetched one at a time as they are written, so an archive larger
/// than memory streams: `files` gives each path and its exact size, `fetch` its bytes.
pub fn write_mpq_with<W: Write>(
    files: &[(String, u64)],
    out: &mut W,
    mut fetch: impl FnMut(&str) -> std::io::Result<Vec<u8>>,
) -> std::io::Result<u64> {
    let mut entries: Vec<(String, u64)> = Vec::new();
    let mut index = std::collections::HashMap::new();
    for (name, size) in files {
        let name = name.replace('/', "\\");
        let key = name.to_ascii_lowercase();
        if let Some(&i) = index.get(&key) {
            entries[i] = (name, *size);
        } else {
            index.insert(key, entries.len());
            entries.push((name, *size));
        }
    }
    let listfile = entries
        .iter()
        .map(|(n, _)| n.as_str())
        .collect::<Vec<_>>()
        .join("\r\n")
        .into_bytes();
    entries.push(("(listfile)".to_string(), listfile.len() as u64));

    let mut pos: u64 = 32;
    let mut blocks: Vec<[u32; 4]> = Vec::with_capacity(entries.len());
    for (_, size) in &entries {
        let p = u32::try_from(pos).map_err(|_| std::io::Error::other("archive over 4 GiB"))?;
        let len = u32::try_from(*size).map_err(|_| std::io::Error::other("file over 4 GiB"))?;
        blocks.push([p, len, len, FLAG_EXISTS]);
        pos += size;
    }
    let hash_count = (entries.len() * 2).next_power_of_two().max(16);
    let mut hash = vec![[0xFFFF_FFFFu32; 4]; hash_count];
    for (i, (name, _)) in entries.iter().enumerate() {
        let mut slot = hash_string(name, 0) as usize % hash_count;
        while hash[slot][3] != 0xFFFF_FFFF {
            slot = (slot + 1) % hash_count;
        }
        hash[slot] = [hash_string(name, 1), hash_string(name, 2), 0, i as u32];
    }
    let mut hash_words: Vec<u32> = hash.into_iter().flatten().collect();
    encrypt(&mut hash_words, hash_string("(hash table)", 3));
    let mut block_words: Vec<u32> = blocks.into_iter().flatten().collect();
    encrypt(&mut block_words, hash_string("(block table)", 3));

    let hash_pos = pos;
    let block_pos = hash_pos + hash_words.len() as u64 * 4;
    let size = block_pos + block_words.len() as u64 * 4;
    let size32 = u32::try_from(size).map_err(|_| std::io::Error::other("archive over 4 GiB"))?;
    let mut hdr = Vec::with_capacity(32);
    hdr.extend_from_slice(b"MPQ\x1a");
    hdr.extend_from_slice(&32u32.to_le_bytes());
    hdr.extend_from_slice(&size32.to_le_bytes());
    hdr.extend_from_slice(&0u16.to_le_bytes()); // format version 0 (V1)
    hdr.extend_from_slice(&3u16.to_le_bytes()); // 4 KiB sectors (unused: nothing is sectored)
    hdr.extend_from_slice(&(hash_pos as u32).to_le_bytes());
    hdr.extend_from_slice(&(block_pos as u32).to_le_bytes());
    hdr.extend_from_slice(&(hash_count as u32).to_le_bytes());
    hdr.extend_from_slice(&(entries.len() as u32).to_le_bytes());
    out.write_all(&hdr)?;
    let n = entries.len();
    for (i, (name, size)) in entries.iter().enumerate() {
        let bytes = if i + 1 == n {
            listfile.clone()
        } else {
            fetch(name)?
        };
        if bytes.len() as u64 != *size {
            return Err(std::io::Error::other(format!(
                "{name}: {} bytes, {size} announced",
                bytes.len()
            )));
        }
        out.write_all(&bytes)?;
    }
    for w in hash_words.iter().chain(block_words.iter()) {
        out.write_all(&w.to_le_bytes())?;
    }
    Ok(size)
}

/// [`write_mpq_to`] into memory.
pub fn write_mpq(files: &[(String, Vec<u8>)]) -> Vec<u8> {
    let mut v = Vec::new();
    write_mpq_to(files, &mut v).expect("writing to memory");
    v
}

#[cfg(test)]
mod tests {
    use super::*;

    pub(crate) fn temp_dir(tag: &str) -> std::path::PathBuf {
        let d = std::env::temp_dir().join(format!("womw-community-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn the_hash_matches_the_published_values() {
        assert_eq!(hash_string("(hash table)", 3), 0xC3AF_3770);
        assert_eq!(hash_string("(block table)", 3), 0xEC83_B3A3);
    }

    #[test]
    fn a_written_archive_reads_back_through_benilla() {
        let dir = temp_dir("roundtrip");
        let files = vec![
            (
                "World\\Maps\\Kalimdor\\Kalimdor_1_2.adt".to_string(),
                b"REVM".to_vec(),
            ),
            ("DBFilesClient/AreaTable.dbc".to_string(), vec![7u8; 9000]),
            ("dbfilesclient\\areatable.DBC".to_string(), vec![8u8; 10]),
        ];
        let path = dir.join(crate::ARCHIVE_NAME);
        std::fs::write(&path, write_mpq(&files)).unwrap();
        let a = benilla_mpq::Archive::open(&path).expect("opens");
        assert_eq!(
            a.read_file("world/maps/kalimdor/KALIMDOR_1_2.adt").unwrap(),
            b"REVM"
        );
        assert_eq!(
            a.read_file("DBFilesClient\\AreaTable.dbc").unwrap(),
            vec![8u8; 10],
            "a later duplicate replaces the earlier"
        );
        let list = String::from_utf8(a.read_file("(listfile)").unwrap()).unwrap();
        assert_eq!(list.lines().count(), 2, "{list}");
        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Archive priority as benilla mounts a `Data` folder: our `patch-A` wins over our WotLK
    /// `patch-9` and over the stock archives, each lower archive still serves its own paths, and a
    /// two-character name (which the reference's `patch-?` glob skips) never mounts.
    #[test]
    fn patch_a_mounts_above_patch_9_and_the_stock_archives() {
        let dir = temp_dir("priority");
        let put = |name: &str, files: &[(&str, &[u8])]| {
            let f: Vec<(String, Vec<u8>)> = files
                .iter()
                .map(|(n, b)| (n.to_string(), b.to_vec()))
                .collect();
            std::fs::write(dir.join(name), write_mpq(&f)).unwrap();
        };
        put("terrain.MPQ", &[("t.adt", b"stock"), ("only-stock", b"s")]);
        put("patch.MPQ", &[("p", b"p")]);
        put(
            "patch-9.MPQ",
            &[("t.adt", b"wotlk"), ("alterac.adt", b"ours")],
        );
        put(
            crate::ARCHIVE_NAME,
            &[("t.adt", b"community"), ("new.adt", b"n")],
        );
        put("patch-10.MPQ", &[("t.adt", b"never")]);
        let chain = benilla_formats::Chain::open(&dir).expect("chain");
        assert_eq!(chain.read("t.adt").unwrap(), b"community");
        assert_eq!(chain.read("alterac.adt").unwrap(), b"ours");
        assert_eq!(chain.read("only-stock").unwrap(), b"s");
        assert_eq!(chain.read("new.adt").unwrap(), b"n");

        // The merge's own view of our side skips patch-A, so a rebuild never reads its output.
        let names: Vec<String> = std::fs::read_dir(&dir)
            .unwrap()
            .map(|e| e.unwrap().file_name().into_string().unwrap())
            .collect();
        let order = crate::source::mount_order(&names, crate::ARCHIVE_NAME);
        assert_eq!(order, ["terrain.MPQ", "patch.MPQ", "patch-9.MPQ"]);
        let _ = std::fs::remove_dir_all(&dir);
    }
}
