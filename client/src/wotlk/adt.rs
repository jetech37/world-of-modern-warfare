//! A raw editor for 1.12 root ADTs: the top-level chunks kept as bytes, the placements, the
//! name tables and each `MCNK`'s heights, normals and object references editable, and the file
//! written back with `MHDR` and `MCIN` recomputed. An unedited file writes back byte for byte
//! (a test holds every shipped tile it touches to that).
//!
//! `MCNK` sub-chunks are found through the header's offsets (`+0x14` MCVT, `+0x18` MCNR, `+0x20`
//! MCRF, counts at `+0x10` doodad refs and `+0x38` WMO refs), as the reference does; an `MCRF`
//! that grows is appended at the chunk's end and the offset moved, so nothing after it shifts.

use anyhow::{Context, Result, bail};

fn rd32(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}
fn wr32(b: &mut [u8], o: usize, v: u32) {
    b[o..o + 4].copy_from_slice(&v.to_le_bytes());
}

/// A top-level chunk: its tag as written forward (`b"MCNK"`) and its payload.
#[derive(Clone)]
pub struct Chunk {
    pub tag: [u8; 4],
    pub data: Vec<u8>,
}

/// A parsed ADT.
#[derive(Clone)]
pub struct Adt {
    pub chunks: Vec<Chunk>,
}

/// One MDDF (36 bytes) or MODF (64 bytes) record, raw.
pub type Record = Vec<u8>;

impl Adt {
    pub fn parse(b: &[u8]) -> Result<Self> {
        let mut chunks = Vec::new();
        let mut o = 0;
        while o + 8 <= b.len() {
            let tag = [b[o + 3], b[o + 2], b[o + 1], b[o]];
            let n = rd32(b, o + 4) as usize;
            let data = b.get(o + 8..o + 8 + n).context("truncated ADT chunk")?.to_vec();
            chunks.push(Chunk { tag, data });
            o += 8 + n;
        }
        if chunks.first().map(|c| &c.tag) != Some(b"MVER") {
            bail!("not an ADT");
        }
        Ok(Self { chunks })
    }

    fn find(&self, tag: &[u8; 4]) -> Option<usize> {
        self.chunks.iter().position(|c| &c.tag == tag)
    }
    pub fn get(&self, tag: &[u8; 4]) -> &[u8] {
        self.find(tag).map_or(&[], |i| &self.chunks[i].data)
    }
    pub fn get_mut(&mut self, tag: &[u8; 4]) -> &mut Vec<u8> {
        let i = self.find(tag).expect("chunk");
        &mut self.chunks[i].data
    }

    /// The `MCNK` chunks' indices in [`Self::chunks`], in file order.
    pub fn mcnk(&self) -> Vec<usize> {
        (0..self.chunks.len()).filter(|&i| &self.chunks[i].tag == b"MCNK").collect()
    }

    /// The names of a name table (`MMDX`/`MWMO`) by their `MMID`/`MWID` slot.
    pub fn names(&self, table: &[u8; 4], ids: &[u8; 4]) -> Vec<String> {
        let blob = self.get(table);
        self.get(ids)
            .chunks_exact(4)
            .map(|c| {
                let o = rd32(c, 0) as usize;
                let s = &blob[o.min(blob.len())..];
                let e = s.iter().position(|&x| x == 0).unwrap_or(s.len());
                String::from_utf8_lossy(&s[..e]).into_owned()
            })
            .collect()
    }

    /// Add a name to `MWMO`/`MWID` (or `MMDX`/`MMID`); its slot.
    pub fn add_name(&mut self, table: &[u8; 4], ids: &[u8; 4], name: &str) -> u32 {
        let ofs = self.get(table).len() as u32;
        let t = self.get_mut(table);
        t.extend_from_slice(name.as_bytes());
        t.push(0);
        let i = self.get_mut(ids);
        i.extend(ofs.to_le_bytes());
        (i.len() / 4 - 1) as u32
    }

    pub fn records(&self, tag: &[u8; 4], size: usize) -> Vec<Record> {
        self.get(tag).chunks_exact(size).map(<[u8]>::to_vec).collect()
    }

    /// Replace the `MDDF` (`doodads`) or `MODF` (`wmos`) records, keeping `keep[i]` of the old
    /// ones in order and appending `added`; every `MCNK`'s references are renumbered to match,
    /// dropped ones removed. `add_refs(chunk index) -> bool` says which chunks reference each
    /// added record.
    pub fn set_records(
        &mut self,
        wmos: bool,
        keep: &[bool],
        added: &[Record],
        add_refs: &dyn Fn(usize, usize) -> bool,
    ) {
        let (tag, size) = if wmos { (b"MODF", 64) } else { (b"MDDF", 36) };
        let old = self.records(tag, size);
        let mut map = vec![None; old.len()];
        let mut out = Vec::new();
        for (i, r) in old.iter().enumerate() {
            if keep[i] {
                map[i] = Some((out.len() / size) as u32);
                out.extend_from_slice(r);
            }
        }
        let first_new = (out.len() / size) as u32;
        for r in added {
            out.extend_from_slice(r);
        }
        *self.get_mut(tag) = out;
        for (ci, idx) in self.mcnk().into_iter().enumerate() {
            let (mut doodads, mut objs) = self.refs(idx);
            let list = if wmos { &mut objs } else { &mut doodads };
            *list = list.iter().filter_map(|&r| map.get(r as usize).copied().flatten()).collect();
            for k in 0..added.len() {
                if add_refs(ci, k) {
                    list.push(first_new + k as u32);
                }
            }
            self.set_refs(idx, &doodads, &objs);
        }
    }

    /// An `MCNK`'s `(doodad refs, WMO refs)`.
    pub fn refs(&self, idx: usize) -> (Vec<u32>, Vec<u32>) {
        let d = &self.chunks[idx].data;
        let (nd, nw) = (rd32(d, 0x10) as usize, rd32(d, 0x38) as usize);
        let at = sub_at(d, rd32(d, 0x20) as usize, b"MCRF");
        let Some(at) = at else {
            return (Vec::new(), Vec::new());
        };
        let all: Vec<u32> = (0..nd + nw).map(|k| rd32(d, at + k * 4)).collect();
        (all[..nd].to_vec(), all[nd..].to_vec())
    }

    fn set_refs(&mut self, idx: usize, doodads: &[u32], objs: &[u32]) {
        let d = &mut self.chunks[idx].data;
        let refs: Vec<u8> = doodads.iter().chain(objs).flat_map(|v| v.to_le_bytes()).collect();
        let hdr_ofs = rd32(d, 0x20) as usize;
        let cap = sub_at(d, hdr_ofs, b"MCRF").map(|at| rd32(d, at - 4) as usize);
        match (sub_at(d, hdr_ofs, b"MCRF"), cap) {
            (Some(at), Some(cap)) if refs.len() <= cap => {
                d[at..at + refs.len()].copy_from_slice(&refs);
            }
            _ => {
                // A new MCRF at the end; the offset counts from the MCNK magic (8 before the data).
                let ofs = d.len() + 8;
                d.extend(*b"FRCM");
                d.extend((refs.len() as u32).to_le_bytes());
                d.extend_from_slice(&refs);
                wr32(d, 0x20, ofs as u32);
            }
        }
        wr32(d, 0x10, doodads.len() as u32);
        wr32(d, 0x38, objs.len() as u32);
    }

    /// An `MCNK`'s base position (world X, Y, Z) and its 145 heights (relative to Z).
    pub fn heights(&self, idx: usize) -> ([f32; 3], Vec<f32>) {
        let d = &self.chunks[idx].data;
        let pos = [0, 4, 8].map(|k| f32::from_bits(rd32(d, 0x68 + k)));
        let at = sub_at(d, rd32(d, 0x14) as usize, b"MCVT").expect("MCVT");
        (pos, (0..145).map(|k| f32::from_bits(rd32(d, at + k * 4))).collect())
    }

    /// An `MCNK`'s 145 MCNR normals as stored (disk bytes, `127` = 1.0), if it has them.
    pub fn normals(&self, idx: usize) -> Option<Vec<[i8; 3]>> {
        let d = &self.chunks[idx].data;
        let at = sub_at(d, rd32(d, 0x18) as usize, b"MCNR")?;
        d.get(at..at + 145 * 3)
            .map(|b| b.chunks_exact(3).map(|c| [c[0] as i8, c[1] as i8, c[2] as i8]).collect())
    }

    /// Write 145 MCNR normals as stored bytes.
    pub fn set_normals_raw(&mut self, idx: usize, normals: &[[i8; 3]]) {
        let d = &mut self.chunks[idx].data;
        if let Some(at) = sub_at(d, rd32(d, 0x18) as usize, b"MCNR") {
            for (k, n) in normals.iter().enumerate() {
                for c in 0..3 {
                    d[at + k * 3 + c] = n[c] as u8;
                }
            }
        }
    }

    /// An `MCNK`'s MCSH baked shadow map (64 rows of 8 bytes, bit `x & 7` of byte `y·8 + x/8`)
    /// as a mutable slice, when the chunk has one (header offset `+0x2C`, read as benilla does).
    pub fn shadow_mut(&mut self, idx: usize) -> Option<&mut [u8]> {
        let d = &mut self.chunks[idx].data;
        let at = sub_at(d, rd32(d, 0x2C) as usize, b"MCSH")?;
        let n = (rd32(d, at - 4) as usize).min(512);
        d.get_mut(at..at + n)
    }

    /// Write 145 heights (relative to the base Z) and normals for them.
    pub fn set_heights(&mut self, idx: usize, heights: &[f32], normals: &[[f32; 3]]) {
        let d = &mut self.chunks[idx].data;
        let at = sub_at(d, rd32(d, 0x14) as usize, b"MCVT").expect("MCVT");
        for (k, h) in heights.iter().enumerate() {
            wr32(d, at + k * 4, h.to_bits());
        }
        if let Some(at) = sub_at(d, rd32(d, 0x18) as usize, b"MCNR") {
            // Disk bytes are WoW [X, Y, Z], 127 = 1.0 (benilla_formats::terrain).
            for (k, n) in normals.iter().enumerate() {
                for c in 0..3 {
                    d[at + k * 3 + c] = (n[c] * 127.0).round().clamp(-127.0, 127.0) as i8 as u8;
                }
            }
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut layout = Vec::new(); // file offset of each chunk's magic
        let mut o = 0usize;
        for c in &self.chunks {
            layout.push(o);
            o += 8 + c.data.len();
        }
        let mut chunks = self.chunks.clone();
        if let Some(h) = self.find(b"MHDR") {
            let base = layout[h] + 8;
            for (slot, tag) in [
                (1usize, b"MCIN"),
                (2, b"MTEX"),
                (3, b"MMDX"),
                (4, b"MMID"),
                (5, b"MWMO"),
                (6, b"MWID"),
                (7, b"MDDF"),
                (8, b"MODF"),
            ] {
                if let Some(i) = self.find(tag) {
                    wr32(&mut chunks[h].data, slot * 4, (layout[i] - base) as u32);
                }
            }
        }
        if let Some(m) = self.find(b"MCIN") {
            for (k, i) in self.mcnk().into_iter().enumerate() {
                wr32(&mut chunks[m].data, k * 16, layout[i] as u32);
                wr32(&mut chunks[m].data, k * 16 + 4, (self.chunks[i].data.len() + 8) as u32);
            }
        }
        let mut out = Vec::with_capacity(o);
        for c in &chunks {
            out.extend([c.tag[3], c.tag[2], c.tag[1], c.tag[0]]);
            out.extend((c.data.len() as u32).to_le_bytes());
            out.extend_from_slice(&c.data);
        }
        out
    }
}

/// A sub-chunk's payload start, from an `MCNK` header offset counted from the magic (or from the
/// data), checking the reversed magic as benilla does.
fn sub_at(d: &[u8], ofs: usize, magic: &[u8; 4]) -> Option<usize> {
    if ofs == 0 {
        return None;
    }
    let rev = [magic[3], magic[2], magic[1], magic[0]];
    // `d` is the payload: the magic-relative offset is 8 more than the payload-relative one.
    for hdr in [ofs.checked_sub(8), Some(ofs)].into_iter().flatten() {
        if d.get(hdr..hdr + 4) == Some(rev.as_slice()) {
            return Some(hdr + 8);
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_shipped_tile_writes_back_byte_for_byte_and_edits_read_back() {
        let Some(data) = super::super::vanilla_install() else {
            eprintln!("no 1.12 install: skipped");
            return;
        };
        let chain = super::super::stock_chain(&data).unwrap();
        for (x, y) in super::super::dalaran::TILES {
            let name = format!("World\\Maps\\Azeroth\\Azeroth_{x}_{y}.adt");
            let raw = chain.read(&name).unwrap();
            let adt = Adt::parse(&raw).unwrap();
            assert!(adt.to_bytes() == raw, "{name} does not round-trip");
        }
        // Edits: drop every doodad, add one WMO referenced from chunk 0, raise chunk 5.
        let raw = chain.read("World\\Maps\\Azeroth\\Azeroth_31_31.adt").unwrap();
        let mut adt = Adt::parse(&raw).unwrap();
        let nd = adt.records(b"MDDF", 36).len();
        adt.set_records(false, &vec![false; nd], &[], &|_, _| false);
        let slot = adt.add_name(b"MWMO", b"MWID", "World\\wmo\\Test\\Test.wmo");
        let mut rec = vec![0u8; 64];
        rec[..4].copy_from_slice(&slot.to_le_bytes());
        let nw = adt.records(b"MODF", 64).len();
        adt.set_records(true, &vec![true; nw], &[rec], &|ci, _| ci == 0);
        let idx = adt.mcnk()[5];
        let (_, mut h) = adt.heights(idx);
        h.iter_mut().for_each(|v| *v += 10.0);
        adt.set_heights(idx, &h, &vec![[0.0, 0.0, 1.0]; 145]);
        let bytes = adt.to_bytes();
        let parsed = benilla_adt::parse_adt(&mut std::io::Cursor::new(bytes.as_slice())).unwrap();
        let benilla_adt::ParsedAdt::Root(root) = parsed;
        assert!(root.doodad_placements.is_empty());
        assert_eq!(root.wmo_placements.len(), nw + 1);
        assert!(root.wmos.iter().any(|w| w.contains("Test.wmo")));
        let back = Adt::parse(&bytes).unwrap();
        assert_eq!(back.refs(back.mcnk()[0]).1.last(), Some(&(nw as u32)));
        assert!(back.refs(back.mcnk()[3]).0.is_empty());
        let (_, h2) = back.heights(back.mcnk()[5]);
        assert_eq!(h2, h);
        let orig = Adt::parse(&raw).unwrap();
        assert_eq!(root.mcnk_chunks.len(), 256);
        assert_eq!(
            root.mcnk_chunks[5].heights.as_ref().unwrap().heights[0],
            orig.heights(orig.mcnk()[5]).1[0] + 10.0
        );
    }

    #[test]
    fn a_shipped_city_tile_survives_the_pass() {
        let Some(data) = super::super::vanilla_install() else {
            eprintln!("no 1.12 install: skipped");
            return;
        };
        let chain = super::super::stock_chain(&data).unwrap();
        let mut total = 0;
        for (map, x, y) in super::super::holes::city_tiles() {
            let Ok(raw) = chain.read(&super::super::holes::tile_path(map, x, y)) else { continue };
            let vols = super::super::holes::volumes_around(map, x, y, &|p| chain.read(p).ok());
            if let Some((bytes, n)) = super::super::holes::close_tile(map, &raw, &vols) {
                total += n;
                let back = Adt::parse(&bytes).unwrap();
                assert_eq!(back.mcnk().len(), 256);
            }
        }
        eprintln!("city void cells closed: {total}");
        // Dalaran's circle sits on dalaran.rs's tiles.
        assert!(super::super::dalaran::TILES.contains(&super::super::holes::tile_of(273.7, 369.2)));
    }
}
