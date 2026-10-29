//! BSA archives: the TES4-family format, version 105, read-only.
//!
//! Layout: a 36-byte header (`BSA\0`, version, folder records' offset, archive flags, folder and
//! file counts, the names' total lengths, file flags); 24-byte folder records (hash, file count,
//! padding, offset); per folder its name (a length byte then the zero-terminated name, when the
//! archive keeps folder names) and 16-byte file records (hash, size, offset); then all the file
//! names, zero-terminated, in record order. A file's size carries bit 30 to flip the archive's
//! default compression; compressed data is the original size (u32) then an LZ4 frame. With
//! archive flag 0x100 every file's data starts with its full path as a length-prefixed string.

use super::lz4;
use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

const COMPRESSED_BY_DEFAULT: u32 = 0x4;
const EMBED_NAMES: u32 = 0x100;
const FLIP_COMPRESSION: u32 = 0x4000_0000;

/// An open archive: its files by lowercase backslash path.
pub struct Bsa {
    file: File,
    flags: u32,
    files: HashMap<String, (u32, u32)>,
}

fn u32le(b: &[u8], i: usize) -> u32 {
    u32::from_le_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]])
}

impl Bsa {
    pub fn open(path: &Path) -> Result<Bsa, String> {
        let mut file = File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut all = vec![0; 36];
        file.read_exact(&mut all).map_err(|e| e.to_string())?;
        if &all[0..4] != b"BSA\0" {
            return Err("not a BSA".into());
        }
        // The directory: the header, the folder records, the folders' names (each with its
        // length byte) and file records, and the file names.
        let dir = 36
            + u32le(&all, 16) as usize * 25
            + u32le(&all, 24) as usize
            + u32le(&all, 20) as usize * 16
            + u32le(&all, 28) as usize;
        all.resize(dir, 0);
        file.read_exact(&mut all[36..])
            .map_err(|e| format!("BSA directory: {e}"))?;
        let version = u32le(&all, 4);
        if version != 105 {
            return Err(format!("BSA version {version}, expected 105 (the 64-bit variant)"));
        }
        let flags = u32le(&all, 12);
        let (folders, files_total) = (u32le(&all, 16) as usize, u32le(&all, 20) as usize);
        let names_len = u32le(&all, 28) as usize;
        let mut at = u32le(&all, 8) as usize;
        let counts: Vec<usize> = (0..folders)
            .map(|k| u32le(&all, at + k * 24 + 8) as usize)
            .collect();
        at += folders * 24;
        let mut records = Vec::with_capacity(files_total);
        for count in counts {
            let len = all[at] as usize;
            let name = String::from_utf8_lossy(&all[at + 1..at + len]).to_string(); // drops NUL
            at += 1 + len;
            for _ in 0..count {
                records.push((name.clone(), u32le(&all, at + 8), u32le(&all, at + 12)));
                at += 16;
            }
        }
        let names = all
            .get(at..at + names_len)
            .ok_or("BSA: names past the end")?;
        let mut files = HashMap::new();
        for ((folder, size, offset), name) in records.into_iter().zip(names.split(|&b| b == 0)) {
            let path = format!("{folder}\\{}", String::from_utf8_lossy(name));
            files.insert(path.to_lowercase(), (size, offset));
        }
        Ok(Bsa { file, flags, files })
    }

    /// Every path in the archive (lowercase, backslashes).
    pub fn paths(&self) -> impl Iterator<Item = &str> {
        self.files.keys().map(String::as_str)
    }

    /// A file's contents, decompressed.
    pub fn read(&mut self, path: &str) -> Result<Vec<u8>, String> {
        let key = path.replace('/', "\\").to_lowercase();
        let &(size, offset) = self
            .files
            .get(&key)
            .ok_or_else(|| format!("BSA: no {key}"))?;
        let compressed = (self.flags & COMPRESSED_BY_DEFAULT != 0) ^ (size & FLIP_COMPRESSION != 0);
        let size = (size & 0x3FFF_FFFF) as usize;
        let mut data = vec![0; size];
        self.file
            .seek(SeekFrom::Start(offset as u64))
            .and_then(|_| self.file.read_exact(&mut data))
            .map_err(|e| e.to_string())?;
        let mut data = &data[..];
        if self.flags & EMBED_NAMES != 0 {
            let len = data[0] as usize;
            data = &data[1 + len..];
        }
        if !compressed {
            return Ok(data.to_vec());
        }
        let original = u32le(data, 0) as usize;
        let out = lz4::decode_frame(&data[4..])?;
        if out.len() != original {
            return Err(format!("{key}: {} bytes, expected {original}", out.len()));
        }
        Ok(out)
    }
}
