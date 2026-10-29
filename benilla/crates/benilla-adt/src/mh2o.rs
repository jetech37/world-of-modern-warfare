//! `MH2O`, the tile-level liquid chunk of later clients, read into 1.12's per-chunk liquid blocks
//! ([`MclqChunk`]) so every liquid consumer (mesh, sound, swim queries) keeps one input. Not a
//! 1.12 chunk: benilla meets it in 1.12-format tiles made with later tools.
//!
//! Layout: 256 headers of 12 bytes (`ofs_instances`, `layer_count`, `ofs_attributes`), indexed
//! `index_y · 16 + index_x`, offsets from the chunk's data. Each instance is 24 bytes: `u16`
//! liquid type, `u16` vertex format, `f32` min and max height, `u8` x offset, y offset, width and
//! height (in cells, of the 8×8 grid), `u32` offset of the exists bitmap (0: every cell) and of
//! the vertex data (0: flat at the min height). Vertex formats: 0 heights + depths, 1 heights +
//! UVs, 2 depths only, 3 heights + UVs + depths, over `(width + 1) · (height + 1)` vertices.

use benilla_bytes::ByteExt;

use crate::{LiquidVertex, MclqChunk};

const HEADER: usize = 12;
const INSTANCE: usize = 24;
const GRID: usize = 9;
const CELLS: usize = 8;
/// The cell nibble for no liquid.
const DRY: u8 = 0x0f;

/// The 1.12 ADT cell nibble for a `LiquidType` id: its class, `(id − 1) mod 4` (water, ocean,
/// magma, slime, as the table repeats them for the slow, fast and WMO variants), as 1.12's own
/// tiles write it (4 river water, 1 ocean, 6 magma, 7 slime).
pub fn nibble_for_liquid_type(id: u16) -> u8 {
    match id.saturating_sub(1) % 4 {
        0 => 4,
        1 => 1,
        2 => 6,
        _ => 7,
    }
}

/// The liquid blocks of chunk `index` (`index_y · 16 + index_x`), one per instance; empty when
/// the chunk is dry or the data is short.
pub fn chunk_liquids(data: &[u8], index: usize) -> Vec<MclqChunk> {
    let h = index * HEADER;
    let (Some(ofs), Some(count)) = (data.u32_at(h), data.u32_at(h + 4)) else {
        return Vec::new();
    };
    if ofs == 0 || count == 0 {
        return Vec::new();
    }
    (0..count as usize)
        .filter_map(|i| instance(data, ofs as usize + i * INSTANCE))
        .collect()
}

fn instance(data: &[u8], at: usize) -> Option<MclqChunk> {
    let kind = data.u16_at(at)?;
    let format = data.u16_at(at + 2)?;
    let min_height = data.f32_at(at + 4)?;
    let max_height = data.f32_at(at + 8)?;
    let x0 = data.u8_at(at + 12)? as usize;
    let y0 = data.u8_at(at + 13)? as usize;
    let w = data.u8_at(at + 14)? as usize;
    let hgt = data.u8_at(at + 15)? as usize;
    let ofs_exists = data.u32_at(at + 16)? as usize;
    let ofs_vertex = data.u32_at(at + 20)? as usize;
    if w == 0 || hgt == 0 || x0 + w > CELLS || y0 + hgt > CELLS {
        return None;
    }

    let nibble = nibble_for_liquid_type(kind);
    let mut tile_flags = [DRY; CELLS * CELLS];
    for r in 0..hgt {
        for c in 0..w {
            let bit = r * w + c;
            let exists = ofs_exists == 0
                || data
                    .u8_at(ofs_exists + bit / 8)
                    .is_some_and(|b| (b >> (bit % 8)) & 1 != 0);
            if exists {
                tile_flags[(y0 + r) * CELLS + x0 + c] = nibble;
            }
        }
    }

    // The instance's vertex arrays, by format; any that the data cannot hold reads as absent.
    let n = (w + 1) * (hgt + 1);
    let (has_h, has_uv, has_d) = match format {
        0 => (true, false, true),
        1 => (true, true, false),
        2 => (false, false, true),
        3 => (true, true, true),
        _ => (false, false, false),
    };
    let mut cursor = ofs_vertex;
    let heights = (ofs_vertex != 0 && has_h).then(|| {
        let v: Option<Vec<f32>> = (0..n).map(|i| data.f32_at(cursor + i * 4)).collect();
        cursor += n * 4;
        v
    });
    let uvs = (ofs_vertex != 0 && has_uv).then(|| {
        let v: Option<Vec<[u8; 4]>> = (0..n)
            .map(|i| data.bytes_at(cursor + i * 4, 4)?.try_into().ok())
            .collect();
        cursor += n * 4;
        v
    });
    let depths = (ofs_vertex != 0 && has_d).then(|| data.bytes_at(cursor, n).map(<[u8]>::to_vec));
    let heights = heights.flatten();
    let uvs = uvs.flatten();
    let depths = depths.flatten();

    let mut vertices = vec![
        LiquidVertex {
            union_data: [0; 4],
            height: min_height,
        };
        GRID * GRID
    ];
    for r in 0..=hgt {
        for c in 0..=w {
            let i = r * (w + 1) + c;
            let v = &mut vertices[(y0 + r) * GRID + x0 + c];
            v.height = heights.as_ref().map_or(min_height, |h| h[i]);
            if let Some(uv) = &uvs {
                v.union_data = uv[i];
            }
            if let Some(d) = &depths {
                v.union_data[0] = d[i];
            } else if uvs.is_none() {
                // No depth: a mid swatch, so water neither vanishes nor turns black.
                v.union_data[0] = 0x80;
            }
        }
    }

    Some(MclqChunk {
        min_height,
        max_height,
        vertices,
        tile_flags,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// An MH2O payload with one instance on chunk `index`.
    fn payload(index: usize, inst: [u8; INSTANCE], tail: &[u8]) -> Vec<u8> {
        let mut d = vec![0u8; 256 * HEADER];
        let ofs = d.len() as u32;
        d[index * HEADER..index * HEADER + 4].copy_from_slice(&ofs.to_le_bytes());
        d[index * HEADER + 4..index * HEADER + 8].copy_from_slice(&1u32.to_le_bytes());
        d.extend_from_slice(&inst);
        d.extend_from_slice(tail);
        d
    }

    fn inst(kind: u16, format: u16, h: f32, rect: [u8; 4], exists: u32, vertex: u32) -> [u8; 24] {
        let mut b = [0u8; 24];
        b[0..2].copy_from_slice(&kind.to_le_bytes());
        b[2..4].copy_from_slice(&format.to_le_bytes());
        b[4..8].copy_from_slice(&h.to_le_bytes());
        b[8..12].copy_from_slice(&h.to_le_bytes());
        b[12..16].copy_from_slice(&rect);
        b[16..20].copy_from_slice(&exists.to_le_bytes());
        b[20..24].copy_from_slice(&vertex.to_le_bytes());
        b
    }

    #[test]
    fn a_flat_full_ocean_instance_wets_every_cell_at_its_height() {
        let d = payload(17, inst(2, 2, -5.0, [0, 0, 8, 8], 0, 0), &[]);
        let l = chunk_liquids(&d, 17);
        assert_eq!(l.len(), 1);
        assert!(l[0].tile_flags.iter().all(|&f| f == 1), "ocean nibble 1");
        assert!(l[0].vertices.iter().all(|v| v.height == -5.0));
        assert!(chunk_liquids(&d, 16).is_empty(), "other chunks are dry");
    }

    #[test]
    fn a_partial_instance_with_a_bitmap_and_heights_lands_on_its_cells() {
        // Water, format 0, the 2×1 cells at (x 3, y 5); bitmap 0b01: only the first exists.
        let base = (256 * HEADER + INSTANCE) as u32;
        let mut tail = vec![0b01u8];
        let heights_at = base + 1;
        for i in 0..6 {
            tail.extend_from_slice(&(10.0 + i as f32).to_le_bytes());
        }
        tail.extend_from_slice(&[9, 9, 9, 9, 9, 9]); // depths
        let d = payload(0, inst(1, 0, 10.0, [3, 5, 2, 1], base, heights_at), &tail);
        let l = &chunk_liquids(&d, 0)[0];
        assert_eq!(l.tile_flags[5 * 8 + 3], 4, "water cell");
        assert_eq!(l.tile_flags[5 * 8 + 4], DRY, "bitmap bit clear");
        assert_eq!(l.tile_flags.iter().filter(|&&f| f != DRY).count(), 1);
        assert_eq!(l.vertices[5 * GRID + 3].height, 10.0);
        assert_eq!(
            l.vertices[6 * GRID + 5].height,
            15.0,
            "last vertex of the 3×2 grid"
        );
        assert_eq!(l.vertices[6 * GRID + 5].depth_byte(), 9);
    }

    #[test]
    fn out_of_range_or_short_data_is_dry_never_a_panic() {
        assert!(chunk_liquids(&[0u8; 10], 3).is_empty());
        let d = payload(0, inst(1, 0, 0.0, [6, 0, 4, 1], 0, 0), &[]);
        assert!(
            chunk_liquids(&d, 0).is_empty(),
            "x 6 + width 4 is off the grid"
        );
        let d = payload(0, inst(1, 0, 3.0, [0, 0, 8, 8], 0, 99_999), &[]);
        let l = chunk_liquids(&d, 0);
        assert!(
            l[0].vertices.iter().all(|v| v.height == 3.0),
            "missing heights read flat"
        );
    }

    #[test]
    fn liquid_type_classes_repeat_every_four_ids() {
        assert_eq!(nibble_for_liquid_type(1), 4);
        assert_eq!(nibble_for_liquid_type(2), 1);
        assert_eq!(nibble_for_liquid_type(3), 6);
        assert_eq!(nibble_for_liquid_type(4), 7);
        assert_eq!(nibble_for_liquid_type(14), 1, "WMO ocean");
        assert_eq!(nibble_for_liquid_type(19), 6, "WMO magma");
    }
}
