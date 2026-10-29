//! A minimal MPQ writer for the patch layer (`patch-9.MPQ`): format V1, every file stored whole
//! (no compression, no encryption, no sector table), the hash and block tables encrypted with the
//! standard keys, and a `(listfile)`. That is the plainest shape `benilla-mpq` reads, and StormLib
//! (the server's extractors) reads it too.

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
        let c = if c == b'/' { b'\\' } else { c.to_ascii_uppercase() };
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

/// Encrypt little-endian words in place with `key`.
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

/// Build an archive holding `files` (`(internal path, bytes)`, `\` separators) plus a
/// `(listfile)` naming them. A later duplicate path replaces an earlier one.
pub fn write_mpq(files: &[(String, Vec<u8>)]) -> Vec<u8> {
    let mut entries: Vec<(String, &[u8])> = Vec::new();
    for (name, bytes) in files {
        let name = name.replace('/', "\\");
        if let Some(e) = entries.iter_mut().find(|e| e.0.eq_ignore_ascii_case(&name)) {
            e.1 = bytes;
        } else {
            entries.push((name, bytes));
        }
    }
    let listfile = entries
        .iter()
        .map(|(n, _)| n.as_str())
        .collect::<Vec<_>>()
        .join("\r\n")
        .into_bytes();
    entries.push(("(listfile)".to_string(), &listfile));

    let mut out = vec![0u8; 32];
    let mut blocks: Vec<[u32; 4]> = Vec::with_capacity(entries.len());
    for (_, bytes) in &entries {
        let pos = u32::try_from(out.len()).expect("a patch archive under 4 GiB");
        let len = u32::try_from(bytes.len()).expect("a file under 4 GiB");
        out.extend_from_slice(bytes);
        blocks.push([pos, len, len, FLAG_EXISTS]);
    }
    let hash_count = (entries.len() * 2).next_power_of_two().max(16);
    let mut hash = vec![[0xFFFF_FFFFu32; 4]; hash_count];
    for (i, (name, _)) in entries.iter().enumerate() {
        let mut slot = hash_string(name, 0) as usize % hash_count;
        while hash[slot][3] != 0xFFFF_FFFF {
            slot = (slot + 1) % hash_count;
        }
        // name_a, name_b, locale (0) | platform (0), block index.
        hash[slot] = [hash_string(name, 1), hash_string(name, 2), 0, i as u32];
    }
    let mut hash_words: Vec<u32> = hash.into_iter().flatten().collect();
    encrypt(&mut hash_words, hash_string("(hash table)", 3));
    let mut block_words: Vec<u32> = blocks.into_iter().flatten().collect();
    encrypt(&mut block_words, hash_string("(block table)", 3));

    let hash_pos = out.len() as u32;
    out.extend(hash_words.iter().flat_map(|w| w.to_le_bytes()));
    let block_pos = out.len() as u32;
    out.extend(block_words.iter().flat_map(|w| w.to_le_bytes()));
    let size = out.len() as u32;
    let mut hdr = Vec::with_capacity(32);
    hdr.extend_from_slice(b"MPQ\x1a");
    hdr.extend_from_slice(&32u32.to_le_bytes());
    hdr.extend_from_slice(&size.to_le_bytes());
    hdr.extend_from_slice(&0u16.to_le_bytes()); // format version 0 (V1)
    hdr.extend_from_slice(&3u16.to_le_bytes()); // 4 KiB sectors (unused: nothing is sectored)
    hdr.extend_from_slice(&hash_pos.to_le_bytes());
    hdr.extend_from_slice(&block_pos.to_le_bytes());
    hdr.extend_from_slice(&(hash_count as u32).to_le_bytes());
    hdr.extend_from_slice(&(entries.len() as u32).to_le_bytes());
    out[..32].copy_from_slice(&hdr);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_dir(tag: &str) -> std::path::PathBuf {
        let d = std::env::temp_dir().join(format!("womw-mpq-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn the_hash_matches_the_published_values() {
        // The table keys every MPQ reader decrypts with.
        assert_eq!(hash_string("(hash table)", 3), 0xC3AF_3770);
        assert_eq!(hash_string("(block table)", 3), 0xEC83_B3A3);
    }

    #[test]
    fn a_written_archive_reads_back_through_benilla() {
        let dir = temp_dir("roundtrip");
        let files = vec![
            ("Creature\\Test\\Test.m2".to_string(), b"MD20 hello".to_vec()),
            ("DBFilesClient/Map.dbc".to_string(), vec![7u8; 9000]),
            ("empty.txt".to_string(), Vec::new()),
        ];
        let path = dir.join("patch-9.MPQ");
        std::fs::write(&path, write_mpq(&files)).unwrap();
        let a = benilla_mpq::Archive::open(&path).expect("opens");
        assert_eq!(a.read_file("creature/test/TEST.m2").unwrap(), b"MD20 hello");
        assert_eq!(a.read_file("DBFilesClient\\Map.dbc").unwrap(), vec![7u8; 9000]);
        assert_eq!(a.read_file("empty.txt").unwrap(), Vec::<u8>::new());
        let list = String::from_utf8(a.read_file("(listfile)").unwrap()).unwrap();
        assert!(list.contains("DBFilesClient\\Map.dbc"), "{list}");
        assert!(!a.contains("missing.blp"));
        let _ = std::fs::remove_dir_all(&dir);
    }

    /// The patch layer mounts above the stock archives: in a `Data` folder with a base archive,
    /// `patch-9.MPQ` wins for a path both hold, and the base still serves the rest.
    #[test]
    fn the_patch_layer_wins_over_the_base_archives() {
        let dir = temp_dir("chain");
        let base = vec![
            ("shared.txt".to_string(), b"base".to_vec()),
            ("only-base.txt".to_string(), b"base only".to_vec()),
        ];
        std::fs::write(dir.join("model.MPQ"), write_mpq(&base)).unwrap();
        std::fs::write(dir.join("patch.MPQ"), write_mpq(&[("p.txt".into(), b"p".to_vec())]))
            .unwrap();
        let patch = vec![
            ("shared.txt".to_string(), b"womw".to_vec()),
            ("only-patch.txt".to_string(), b"new".to_vec()),
        ];
        std::fs::write(dir.join(super::super::PATCH_ARCHIVE), write_mpq(&patch)).unwrap();
        let chain = benilla_formats::Chain::open(&dir).expect("chain");
        assert_eq!(chain.read("shared.txt").unwrap(), b"womw");
        assert_eq!(chain.read("only-base.txt").unwrap(), b"base only");
        assert_eq!(chain.read("only-patch.txt").unwrap(), b"new");
        assert!(
            chain
                .find_file_archive("shared.txt")
                .unwrap()
                .ends_with(super::super::PATCH_ARCHIVE)
        );
        let _ = std::fs::remove_dir_all(&dir);
    }
}
