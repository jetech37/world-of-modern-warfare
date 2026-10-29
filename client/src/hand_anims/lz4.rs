//! LZ4 decompression, as BSA archives store their files: the frame format (magic
//! `0x184D2204`) around LZ4 blocks. Only what decoding needs; checksums are skipped, not checked.

/// An LZ4 frame's magic number.
const MAGIC: u32 = 0x184D_2204;

fn u32_at(b: &[u8], i: usize) -> Result<u32, String> {
    b.get(i..i + 4)
        .map(|s| u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
        .ok_or_else(|| "lz4: truncated".to_string())
}

/// Decode one LZ4 block onto the end of `out` (whose earlier bytes a match may refer back into,
/// as in a frame's linked blocks).
pub fn decode_block(src: &[u8], out: &mut Vec<u8>) -> Result<(), String> {
    let mut i = 0;
    let byte = |i: usize| {
        src.get(i)
            .copied()
            .ok_or("lz4: truncated block".to_string())
    };
    while i < src.len() {
        let token = byte(i)?;
        i += 1;
        let mut lit = (token >> 4) as usize;
        if lit == 15 {
            loop {
                let b = byte(i)?;
                i += 1;
                lit += b as usize;
                if b != 255 {
                    break;
                }
            }
        }
        let lits = src.get(i..i + lit).ok_or("lz4: literals past the end")?;
        out.extend_from_slice(lits);
        i += lit;
        if i >= src.len() {
            break; // the last sequence has literals only
        }
        let off = byte(i)? as usize | (byte(i + 1)? as usize) << 8;
        i += 2;
        let mut len = (token & 15) as usize;
        if len == 15 {
            loop {
                let b = byte(i)?;
                i += 1;
                len += b as usize;
                if b != 255 {
                    break;
                }
            }
        }
        len += 4;
        if off == 0 || off > out.len() {
            return Err(format!("lz4: bad match offset {off}"));
        }
        let start = out.len() - off;
        // Byte by byte: a match may overlap what it is writing.
        for k in 0..len {
            let b = out[start + k];
            out.push(b);
        }
    }
    Ok(())
}

/// Decode an LZ4 frame.
pub fn decode_frame(data: &[u8]) -> Result<Vec<u8>, String> {
    if u32_at(data, 0)? != MAGIC {
        return Err("lz4: not a frame".into());
    }
    let flg = *data.get(4).ok_or("lz4: truncated header")?;
    let mut i = 6; // magic, FLG, BD
    if flg & 0x08 != 0 {
        i += 8; // content size
    }
    if flg & 0x01 != 0 {
        i += 4; // dictionary id
    }
    i += 1; // header checksum
    let mut out = Vec::new();
    loop {
        let size = u32_at(data, i)?;
        i += 4;
        if size == 0 {
            break; // end mark
        }
        let raw = size & 0x8000_0000 != 0;
        let size = (size & 0x7FFF_FFFF) as usize;
        let block = data.get(i..i + size).ok_or("lz4: block past the end")?;
        i += size;
        if raw {
            out.extend_from_slice(block);
        } else {
            decode_block(block, &mut out)?;
        }
        if flg & 0x10 != 0 {
            i += 4; // block checksum
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn literals_then_an_overlapping_match() {
        // "abc" then a match 3 back, 9 long (4 + 5): "abcabcabcabc".
        let block = [0x35, b'a', b'b', b'c', 3, 0];
        let mut out = Vec::new();
        decode_block(&block, &mut out).unwrap();
        assert_eq!(out, b"abcabcabcabc");
    }

    #[test]
    fn long_literal_and_match_lengths_extend() {
        // 20 literals (15 + 5), then a match of 1 back, 4 + 15 + 3 = 22 long, then 1 literal.
        let mut block = vec![0xFF, 5];
        block.extend(std::iter::repeat_n(b'x', 20));
        block.extend([1, 0, 3, 0x10, b'y']);
        let mut out = Vec::new();
        decode_block(&block, &mut out).unwrap();
        assert_eq!(out.len(), 20 + 22 + 1);
        assert!(out[..42].iter().all(|&b| b == b'x') && out[42] == b'y');
    }

    #[test]
    fn a_frame_of_a_raw_and_a_compressed_block() {
        let mut f = MAGIC.to_le_bytes().to_vec();
        f.extend([0x40, 0x40, 0x00]); // FLG (v1, independent off), BD, header checksum
        f.extend((0x8000_0002u32).to_le_bytes()); // raw block, 2 bytes
        f.extend(b"hi");
        // A compressed (linked) block: no literals, a 4-long match 2 back, into the raw block.
        f.extend(3u32.to_le_bytes());
        f.extend([0x00, 2, 0]);
        f.extend(0u32.to_le_bytes()); // end mark
        assert_eq!(decode_frame(&f).unwrap(), b"hihihi");
    }

    #[test]
    fn rejects_bad_offsets_and_magic() {
        let mut out = Vec::new();
        assert!(decode_block(&[0x00, 5, 0], &mut out).is_err());
        assert!(decode_frame(&[0, 0, 0, 0, 0, 0, 0]).is_err());
    }
}
