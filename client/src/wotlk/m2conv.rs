//! An offline down-port of WotLK M2 models (MD20 version 264, build 12340) to the 1.12 layout
//! (version 256) that benilla's M2 reader, particle, ribbon, light and bone readers take as is.
//!
//! What differs between the two, and what this does about it:
//!
//! - **Header.** 264 drops `PlayableAnimationLookup`, the embedded `views` array (a skin-profile
//!   count instead) and `texture_flipbooks`. The lookup is rebuilt from the model's own
//!   `AnimationLookup` and the 1.12 `AnimationData.dbc` fallback chain; the views are the
//!   `*NN.skin` files, embedded; the flipbooks stay empty.
//! - **Tracks.** A 264 `M2Track` (`0x14` bytes) holds one key list per sequence, timestamps local
//!   to that sequence. A 256 track (`0x1c`) holds one list on a single timeline, sequence `i`
//!   being the time band `[start_i, end_i]` with a key-index window per sequence. The sequences
//!   are laid end to end on that timeline and every per-sequence list is moved into its band.
//!   Global-sequence tracks keep their own clock. A sequence whose keys live in an external
//!   `*IIII-VV.anim` file (flag `0x20` clear) reads them from that file.
//! - **Bone rotations** are compressed quaternions (4 x i16) in 264 and floats in 256.
//! - **Records** that embed tracks grow: sequences (64 to 68 bytes: a start and an end for the
//!   duration), bones (88 to 108, no name CRC), colours, texture weights and transforms,
//!   attachments, events, lights, cameras, ribbons and particles.
//! - **Particles.** 264 animates colour, alpha, scale and texture cells with `M2FakeTrack`s; 256
//!   has three fixed points (start, middle, end). The fake tracks are sampled at their keys.
//! - **Skins.** A 264 skin section is 48 bytes; 256 reads the first 32.
//! - **Blend mode 7** (WotLK's blend-add) has no 1.12 counterpart and becomes additive (4).

use std::collections::HashMap;

use anyhow::{Context, Result, bail};

/// The 1.12 header's size: the particle array ends at `0x144`.
const HDR_256: usize = 0x144;
/// The gap left between consecutive sequence bands on the 256 timeline, in ms.
const SEQ_GAP: u32 = 33;

fn rd16(b: &[u8], o: usize) -> Result<u16> {
    b.get(o..o + 2)
        .map(|s| u16::from_le_bytes([s[0], s[1]]))
        .with_context(|| format!("u16 at {o:#x} past {:#x}", b.len()))
}
fn rd32(b: &[u8], o: usize) -> Result<u32> {
    b.get(o..o + 4)
        .map(|s| u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
        .with_context(|| format!("u32 at {o:#x} past {:#x}", b.len()))
}
fn slice(b: &[u8], o: usize, n: usize) -> Result<&[u8]> {
    b.get(o..o.checked_add(n).context("overflow")?)
        .with_context(|| format!("{n} bytes at {o:#x} past {:#x}", b.len()))
}
/// An `M2Array` (`count`, `offset`) at `o`.
fn arr(b: &[u8], o: usize) -> Result<(usize, usize)> {
    Ok((rd32(b, o)? as usize, rd32(b, o + 4)? as usize))
}

/// Where one sequence's keys live.
#[derive(Clone, Copy)]
enum SeqData<'a> {
    /// In the `.m2` itself (flag `0x20`).
    M2,
    /// In its external `.anim` file.
    Anim(&'a [u8]),
    /// External, but the file is absent: no keys.
    Missing,
}

/// What a conversion needs besides the `.m2` bytes.
pub struct Input<'a> {
    pub m2: &'a [u8],
    /// The skin profiles `*00.skin`, `*01.skin`, ... in order; at least the first.
    pub skins: Vec<&'a [u8]>,
    /// External animation files by `(sequence id, variation)`.
    pub anims: HashMap<(u16, u16), Vec<u8>>,
    /// Rows of the rebuilt `PlayableAnimationLookup` (every 1.12 model has 203).
    pub playable_count: usize,
    /// The 1.12 `AnimationData.dbc` fallback for an id, `None` for Stand.
    pub fallback: &'a dyn Fn(u16) -> Option<u16>,
}

/// The version-264 sequences that keep their keys in an external `.anim` file: `(id, variation)`.
pub fn external_anims(m2: &[u8]) -> Result<Vec<(u16, u16)>> {
    check_264(m2)?;
    let (n, o) = arr(m2, 0x1c)?;
    let mut out = Vec::new();
    for i in 0..n {
        let r = o + i * 0x40;
        let flags = rd32(m2, r + 0x0c)?;
        if flags & 0x20 == 0 && flags & 0x40 == 0 {
            out.push((rd16(m2, r)?, rd16(m2, r + 2)?));
        }
    }
    Ok(out)
}

/// The `.anim` file name for sequence `(id, var)` of the model at `m2_path`.
pub fn anim_file_name(m2_path: &str, id: u16, var: u16) -> String {
    let stem = m2_path.strip_suffix(".m2").unwrap_or(m2_path);
    format!("{stem}{id:04}-{var:02}.anim")
}

/// The `.skin` file name for profile `n` of the model at `m2_path`.
pub fn skin_file_name(m2_path: &str, n: usize) -> String {
    let stem = m2_path.strip_suffix(".m2").unwrap_or(m2_path);
    format!("{stem}{n:02}.skin")
}

/// The number of skin profiles a version-264 model names.
pub fn skin_count(m2: &[u8]) -> Result<usize> {
    check_264(m2)?;
    Ok(rd32(m2, 0x44)? as usize)
}

/// The hard-coded texture files (type 0) a version-264 model names, and its particles' model files.
pub fn referenced_files(m2: &[u8]) -> Result<Vec<String>> {
    check_264(m2)?;
    let mut out = Vec::new();
    let s = |o: usize, n: usize| -> Result<String> {
        let raw = slice(m2, o, n)?;
        let end = raw.iter().position(|&c| c == 0).unwrap_or(raw.len());
        Ok(String::from_utf8_lossy(&raw[..end]).into_owned())
    };
    let (n, o) = arr(m2, 0x50)?;
    for i in 0..n {
        let t = o + i * 16;
        if rd32(m2, t)? == 0 {
            let (len, ofs) = arr(m2, t + 8)?;
            let name = s(ofs, len)?;
            if !name.is_empty() {
                out.push(name);
            }
        }
    }
    let (n, o) = arr(m2, 0x128)?;
    for i in 0..n {
        for f in [0x18, 0x20] {
            let (len, ofs) = arr(m2, o + i * 0x1dc + f)?;
            if len > 1 {
                let name = s(ofs, len)?;
                if !name.is_empty() {
                    out.push(name.replace(".mdx", ".m2").replace(".MDX", ".m2"));
                }
            }
        }
    }
    Ok(out)
}

fn check_264(m2: &[u8]) -> Result<()> {
    if m2.len() < 0x130 || &m2[..4] != b"MD20" {
        bail!("not an MD20 model");
    }
    let v = rd32(m2, 4)?;
    if v != 264 {
        bail!("MD20 version {v}, not 264");
    }
    Ok(())
}

struct Conv<'a> {
    src: &'a [u8],
    seq_start: Vec<u32>,
    seq_data: Vec<SeqData<'a>>,
    out: Vec<u8>,
}

/// A 256 `M2Array` header, 8 bytes.
fn arr_bytes(n: usize, ofs: u32) -> [u8; 8] {
    let mut b = [0u8; 8];
    b[..4].copy_from_slice(&(n as u32).to_le_bytes());
    b[4..].copy_from_slice(&(if n == 0 { 0 } else { ofs }).to_le_bytes());
    b
}

/// A key value converter: reads one 264 value, writes one 256 value.
type ValConv<'f> = &'f dyn Fn(&[u8], &mut Vec<u8>);

fn copy_val(v: &[u8], out: &mut Vec<u8>) {
    out.extend_from_slice(v);
}

/// A compressed quaternion component (264) to a float (256), as the WotLK client expands it.
fn unpack_quat(v: &[u8], out: &mut Vec<u8>) {
    for c in v.chunks_exact(2) {
        let s = i16::from_le_bytes([c[0], c[1]]);
        let f = if s < 0 {
            (i32::from(s) + 32768) as f32 / 32767.0
        } else {
            (i32::from(s) - 32767) as f32 / 32767.0
        };
        out.extend_from_slice(&f.to_le_bytes());
    }
}

impl<'a> Conv<'a> {
    /// Append `bytes` 16-aligned; returns their offset (0 for nothing).
    fn append(&mut self, bytes: &[u8]) -> u32 {
        if bytes.is_empty() {
            return 0;
        }
        while !self.out.len().is_multiple_of(16) {
            self.out.push(0);
        }
        let at = self.out.len() as u32;
        self.out.extend_from_slice(bytes);
        at
    }

    /// Copy the source array at header/record offset `o` (elements of `size` bytes) to the output.
    fn copy_array(&mut self, o: usize, size: usize) -> Result<[u8; 8]> {
        let (n, ofs) = arr(self.src, o)?;
        let data = slice(self.src, ofs, n * size)?.to_vec();
        let at = self.append(&data);
        Ok(arr_bytes(n, at))
    }

    /// The bytes a sequence's inner key arrays point into.
    fn seq_buf(&self, seq: usize) -> Option<&'a [u8]> {
        match self.seq_data.get(seq) {
            Some(SeqData::M2) => Some(self.src),
            Some(SeqData::Anim(b)) => Some(b),
            Some(SeqData::Missing) | None => None,
        }
    }

    /// Convert the 264 track at `o` (values `vin` bytes each) to a 256 track (`0x1c` bytes),
    /// its keys moved onto the single timeline. A sequence with no keys in a track that has keys
    /// elsewhere gets one `default` key at its band's start, when one is given (a bone at rest).
    fn track(
        &mut self,
        o: usize,
        vin: usize,
        conv: ValConv,
        default: Option<&[u8]>,
    ) -> Result<[u8; 28]> {
        let src = self.src;
        let interp = rd16(src, o)?;
        let gseq = rd16(src, o + 2)?;
        let (tn, to) = arr(src, o + 4)?;
        let (vn, vo) = arr(src, o + 12)?;
        let mut times: Vec<u32> = Vec::new();
        let mut vals: Vec<u8> = Vec::new();
        let mut ranges: Vec<(u32, u32)> = Vec::new();
        let inner = |buf: &[u8], j: usize| -> Result<Option<(usize, usize, usize)>> {
            let (tc, toff) = arr(src, to + j * 8)?;
            let (vc, voff) = arr(src, vo + j * 8)?;
            let n = tc.min(vc);
            if n == 0 {
                return Ok(None);
            }
            // Both arrays must lie in the buffer the sequence's keys live in.
            slice(buf, toff, n * 4)?;
            slice(buf, voff, n * vin)?;
            Ok(Some((n, toff, voff)))
        };
        if gseq != 0xffff {
            // A global-sequence track: its own clock, keys from every list as they are.
            for j in 0..tn.min(vn) {
                if let Some((n, toff, voff)) = inner(src, j)? {
                    for k in 0..n {
                        times.push(rd32(src, toff + k * 4)?);
                        conv(slice(src, voff + k * vin, vin)?, &mut vals);
                    }
                }
            }
        } else {
            let lists = tn.min(vn);
            let mut any = false;
            for j in 0..lists.min(self.seq_start.len()) {
                if let Some(buf) = self.seq_buf(j)
                    && inner(buf, j)?.is_some()
                {
                    any = true;
                }
            }
            if any {
                for j in 0..self.seq_start.len() {
                    let start = self.seq_start[j];
                    let lo = times.len() as u32;
                    let keys = match self.seq_buf(j) {
                        Some(buf) if j < lists => inner(buf, j)?.map(|k| (buf, k)),
                        _ => None,
                    };
                    match keys {
                        Some((buf, (n, toff, voff))) => {
                            for k in 0..n {
                                times.push(start.saturating_add(rd32(buf, toff + k * 4)?));
                                conv(slice(buf, voff + k * vin, vin)?, &mut vals);
                            }
                        }
                        None => {
                            if let Some(d) = default {
                                times.push(start);
                                vals.extend_from_slice(d);
                            }
                        }
                    }
                    let hi = (times.len() as u32).saturating_sub(1).max(lo);
                    ranges.push((lo, hi));
                }
            }
        }
        let rbytes: Vec<u8> = ranges
            .iter()
            .flat_map(|(a, b)| a.to_le_bytes().into_iter().chain(b.to_le_bytes()))
            .collect();
        let tbytes: Vec<u8> = times.iter().flat_map(|t| t.to_le_bytes()).collect();
        let r_at = self.append(&rbytes);
        let t_at = self.append(&tbytes);
        let v_at = self.append(&vals);
        let mut t = [0u8; 28];
        t[0..2].copy_from_slice(&interp.to_le_bytes());
        t[2..4].copy_from_slice(&gseq.to_le_bytes());
        t[4..12].copy_from_slice(&arr_bytes(ranges.len(), r_at));
        t[12..20].copy_from_slice(&arr_bytes(times.len(), t_at));
        t[20..28].copy_from_slice(&arr_bytes(times.len(), v_at));
        Ok(t)
    }

    /// A value-less 264 `M2TrackBase` (`0x0c`: interp, gseq, timestamps) to the 256 form (`0x14`:
    /// interp, gseq, ranges, timestamps), as the animation events carry.
    fn track_base(&mut self, o: usize) -> Result<[u8; 20]> {
        let src = self.src;
        let interp = rd16(src, o)?;
        let gseq = rd16(src, o + 2)?;
        let (tn, to) = arr(src, o + 4)?;
        let mut times = Vec::new();
        let mut ranges = Vec::new();
        if tn > 0 {
            for j in 0..self.seq_start.len() {
                let lo = times.len() as u32;
                if j < tn
                    && let Some(buf) = if gseq != 0xffff { Some(src) } else { self.seq_buf(j) }
                {
                    let (n, toff) = arr(src, to + j * 8)?;
                    let base = if gseq != 0xffff { 0 } else { self.seq_start[j] };
                    for k in 0..n {
                        times.push(base.saturating_add(rd32(buf, toff + k * 4)?));
                    }
                }
                let hi = (times.len() as u32).saturating_sub(1).max(lo);
                ranges.push((lo, hi));
            }
        }
        let rbytes: Vec<u8> = ranges
            .iter()
            .flat_map(|(a, b): &(u32, u32)| a.to_le_bytes().into_iter().chain(b.to_le_bytes()))
            .collect();
        let tbytes: Vec<u8> = times.iter().flat_map(|t: &u32| t.to_le_bytes()).collect();
        let r_at = self.append(&rbytes);
        let t_at = self.append(&tbytes);
        let mut t = [0u8; 20];
        t[0..2].copy_from_slice(&interp.to_le_bytes());
        t[2..4].copy_from_slice(&gseq.to_le_bytes());
        t[4..12].copy_from_slice(&arr_bytes(ranges.len(), r_at));
        t[12..20].copy_from_slice(&arr_bytes(times.len(), t_at));
        Ok(t)
    }

    /// Convert `n` records of `stride_in` bytes at `ofs` with `rec`, append them, and return the
    /// 256 array header.
    fn records(
        &mut self,
        hdr: usize,
        stride_in: usize,
        rec: &dyn Fn(&mut Self, usize) -> Result<Vec<u8>>,
    ) -> Result<[u8; 8]> {
        let (n, ofs) = arr(self.src, hdr)?;
        slice(self.src, ofs, n * stride_in)?;
        let mut all = Vec::new();
        for i in 0..n {
            all.extend(rec(self, ofs + i * stride_in)?);
        }
        let at = self.append(&all);
        Ok(arr_bytes(n, at))
    }
}

const ZERO3: [u8; 12] = [0; 12];
const QUAT_IDENTITY: [u8; 16] = {
    let mut q = [0u8; 16];
    let one = 1.0f32.to_le_bytes();
    q[12] = one[0];
    q[13] = one[1];
    q[14] = one[2];
    q[15] = one[3];
    q
};
const ONES3: [u8; 12] = {
    let one = 1.0f32.to_le_bytes();
    [
        one[0], one[1], one[2], one[3], one[0], one[1], one[2], one[3], one[0], one[1], one[2],
        one[3],
    ]
};

/// Convert a version-264 model and its skins to one version-256 `.m2`.
pub fn convert(input: &Input) -> Result<Vec<u8>> {
    let src = input.m2;
    check_264(src)?;
    if input.skins.is_empty() {
        bail!("no skin profile");
    }
    for s in &input.skins {
        if s.len() < 48 || &s[..4] != b"SKIN" {
            bail!("not a SKIN profile");
        }
    }
    let flags = rd32(src, 0x10)?;

    // The sequences, end to end on one timeline.
    let (nseq, oseq) = arr(src, 0x1c)?;
    slice(src, oseq, nseq * 0x40)?;
    let mut seq_start = Vec::with_capacity(nseq);
    let mut seq_data = Vec::with_capacity(nseq);
    let mut t = 0u32;
    for i in 0..nseq {
        let r = oseq + i * 0x40;
        let dur = rd32(src, r + 4)?;
        seq_start.push(t);
        t = t.saturating_add(dur).saturating_add(SEQ_GAP);
        let sflags = rd32(src, r + 0x0c)?;
        let key = (rd16(src, r)?, rd16(src, r + 2)?);
        seq_data.push(if sflags & 0x20 != 0 || sflags & 0x40 != 0 {
            SeqData::M2
        } else {
            match input.anims.get(&key) {
                Some(b) => SeqData::Anim(b.as_slice()),
                None => SeqData::Missing,
            }
        });
    }
    let mut c = Conv {
        src,
        seq_start,
        seq_data,
        out: vec![0u8; HDR_256],
    };
    let mut hdr = vec![0u8; HDR_256];
    let put = |hdr: &mut Vec<u8>, at: usize, b: &[u8]| hdr[at..at + b.len()].copy_from_slice(b);

    put(&mut hdr, 0, b"MD20");
    put(&mut hdr, 4, &256u32.to_le_bytes());
    let name = c.copy_array(0x08, 1)?;
    put(&mut hdr, 0x08, &name);
    // Bit 3 (texture combiner combos) names an array 1.12 does not have.
    put(&mut hdr, 0x10, &(flags & !0x8).to_le_bytes());
    let gseq = c.copy_array(0x14, 4)?;
    put(&mut hdr, 0x14, &gseq);

    // Sequences: 64 to 68 bytes, the duration to a band.
    let seqs = c.records(0x1c, 0x40, &|c, r| {
        let s = c.src;
        let i = (r - oseq) / 0x40;
        let start = c.seq_start[i];
        let mut o = Vec::with_capacity(0x44);
        o.extend_from_slice(slice(s, r, 4)?);
        o.extend_from_slice(&start.to_le_bytes());
        o.extend_from_slice(&start.saturating_add(rd32(s, r + 4)?).to_le_bytes());
        o.extend_from_slice(slice(s, r + 8, 0x38)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x1c, &seqs);
    let lookup = c.copy_array(0x24, 2)?;
    put(&mut hdr, 0x24, &lookup);

    // PlayableAnimationLookup: each 1.12 id plays itself when the model has it, else the first
    // id down its fallback chain the model has, else Stand.
    let (nl, ol) = arr(src, 0x24)?;
    let owns = |id: u16| -> bool {
        (id as usize) < nl && rd16(src, ol + id as usize * 2).is_ok_and(|v| v != 0xffff)
    };
    let mut pal = Vec::with_capacity(input.playable_count * 4);
    for id in 0..input.playable_count as u16 {
        let mut cur = id;
        let mut resolved = 0u16;
        for _ in 0..32 {
            if owns(cur) {
                resolved = cur;
                break;
            }
            match (input.fallback)(cur) {
                Some(f) if f != cur => cur = f,
                _ => break,
            }
        }
        pal.extend_from_slice(&u32::from(resolved).to_le_bytes());
    }
    let pal_at = c.append(&pal);
    put(&mut hdr, 0x2c, &arr_bytes(input.playable_count, pal_at));

    // Bones: 88 to 108 bytes; rotations to float quaternions.
    let bones = c.records(0x2c, 0x58, &|c, r| {
        let mut o = Vec::with_capacity(0x6c);
        o.extend_from_slice(slice(c.src, r, 12)?);
        let tr = c.track(r + 0x10, 12, &copy_val, Some(&ZERO3))?;
        let rot = c.track(r + 0x24, 8, &unpack_quat, Some(&QUAT_IDENTITY))?;
        let sc = c.track(r + 0x38, 12, &copy_val, Some(&ONES3))?;
        o.extend_from_slice(&tr);
        o.extend_from_slice(&rot);
        o.extend_from_slice(&sc);
        o.extend_from_slice(slice(c.src, r + 0x4c, 12)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x34, &bones);
    let keybone = c.copy_array(0x34, 2)?;
    put(&mut hdr, 0x3c, &keybone);
    let verts = c.copy_array(0x3c, 48)?;
    put(&mut hdr, 0x44, &verts);

    // Views: the skin files embedded, offsets now into the .m2.
    let mut views = Vec::with_capacity(input.skins.len() * 44);
    for skin in &input.skins {
        let mut part = |o: usize, size: usize, keep: usize| -> Result<[u8; 8]> {
            let (n, ofs) = arr(skin, o)?;
            let data = slice(skin, ofs, n * size)?;
            let mut v = Vec::with_capacity(n * keep);
            for e in data.chunks_exact(size) {
                v.extend_from_slice(&e[..keep]);
            }
            let at = c.append(&v);
            Ok(arr_bytes(n, at))
        };
        views.extend(part(4, 2, 2)?); // vertex lookup
        views.extend(part(12, 2, 2)?); // triangles
        views.extend(part(20, 4, 4)?); // bone-influence properties
        views.extend(part(28, 48, 32)?); // submeshes: 48 to 32 bytes
        views.extend(part(36, 24, 24)?); // batches
        views.extend(rd32(skin, 44)?.to_le_bytes());
    }
    let views_at = c.append(&views);
    put(&mut hdr, 0x4c, &arr_bytes(input.skins.len(), views_at));

    // Colours: RGB and alpha tracks.
    let colors = c.records(0x48, 0x28, &|c, r| {
        let mut o = Vec::with_capacity(0x38);
        o.extend(c.track(r, 12, &copy_val, None)?);
        o.extend(c.track(r + 0x14, 2, &copy_val, None)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x54, &colors);
    // Textures: type, flags, file name.
    let textures = c.records(0x50, 16, &|c, r| {
        let mut o = Vec::with_capacity(16);
        o.extend_from_slice(slice(c.src, r, 8)?);
        o.extend(c.copy_array(r + 8, 1)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x5c, &textures);
    let weights = c.records(0x58, 0x14, &|c, r| Ok(c.track(r, 2, &copy_val, None)?.to_vec()))?;
    put(&mut hdr, 0x64, &weights);
    // 0x6c: texture flipbooks, none.
    let transforms = c.records(0x60, 0x3c, &|c, r| {
        let mut o = Vec::with_capacity(0x54);
        o.extend(c.track(r, 12, &copy_val, None)?);
        o.extend(c.track(r + 0x14, 16, &copy_val, None)?);
        o.extend(c.track(r + 0x28, 12, &copy_val, None)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x74, &transforms);
    let replace = c.copy_array(0x68, 2)?;
    put(&mut hdr, 0x7c, &replace);
    // Materials: blend mode 7 (blend-add) has no 1.12 form; additive is the nearest.
    let (nm, om) = arr(src, 0x70)?;
    let mut mats = slice(src, om, nm * 4)?.to_vec();
    for m in mats.chunks_exact_mut(4) {
        if u16::from_le_bytes([m[2], m[3]]) > 6 {
            m[2..4].copy_from_slice(&4u16.to_le_bytes());
        }
    }
    let mats_at = c.append(&mats);
    put(&mut hdr, 0x84, &arr_bytes(nm, mats_at));
    for (from, to) in [(0x78, 0x8c), (0x80, 0x94), (0x88, 0x9c), (0x90, 0xa4), (0x98, 0xac)] {
        let a = c.copy_array(from, 2)?;
        put(&mut hdr, to, &a);
    }
    // Bounds: box, radius, collision box, radius.
    put(&mut hdr, 0xb4, slice(src, 0xa0, 56)?);
    let btri = c.copy_array(0xd8, 2)?;
    put(&mut hdr, 0xec, &btri);
    let bvert = c.copy_array(0xe0, 12)?;
    put(&mut hdr, 0xf4, &bvert);
    let bnorm = c.copy_array(0xe8, 12)?;
    put(&mut hdr, 0xfc, &bnorm);
    // Attachments: 40 to 48 bytes.
    let attach = c.records(0xf0, 0x28, &|c, r| {
        let mut o = Vec::with_capacity(48);
        o.extend_from_slice(slice(c.src, r, 0x14)?);
        o.extend(c.track(r + 0x14, 1, &copy_val, None)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x104, &attach);
    let attlookup = c.copy_array(0xf8, 2)?;
    put(&mut hdr, 0x10c, &attlookup);
    // Events: 36 to 44 bytes.
    let events = c.records(0x100, 0x24, &|c, r| {
        let mut o = Vec::with_capacity(44);
        o.extend_from_slice(slice(c.src, r, 0x18)?);
        o.extend(c.track_base(r + 0x18)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x114, &events);
    // Lights: 156 to 212 bytes.
    let lights = c.records(0x108, 0x9c, &|c, r| {
        let mut o = Vec::with_capacity(0xd4);
        o.extend_from_slice(slice(c.src, r, 0x10)?);
        for (k, size) in [12, 4, 12, 4, 4, 4, 1].into_iter().enumerate() {
            o.extend(c.track(r + 0x10 + k * 0x14, size, &copy_val, None)?);
        }
        Ok(o)
    })?;
    put(&mut hdr, 0x11c, &lights);
    // Cameras: 100 to 124 bytes.
    let cams = c.records(0x110, 0x64, &|c, r| {
        let mut o = Vec::with_capacity(0x7c);
        o.extend_from_slice(slice(c.src, r, 0x10)?);
        o.extend(c.track(r + 0x10, 36, &copy_val, None)?);
        o.extend_from_slice(slice(c.src, r + 0x24, 12)?);
        o.extend(c.track(r + 0x30, 36, &copy_val, None)?);
        o.extend_from_slice(slice(c.src, r + 0x44, 12)?);
        o.extend(c.track(r + 0x50, 12, &copy_val, None)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x124, &cams);
    let camlookup = c.copy_array(0x118, 2)?;
    put(&mut hdr, 0x12c, &camlookup);
    // Ribbons: 0xb0 to 0xdc bytes.
    let ribbons = c.records(0x120, 0xb0, &|c, r| {
        let mut o = Vec::with_capacity(0xdc);
        o.extend_from_slice(slice(c.src, r, 0x14)?);
        o.extend(c.copy_array(r + 0x14, 2)?);
        o.extend(c.copy_array(r + 0x1c, 2)?);
        o.extend(c.track(r + 0x24, 12, &copy_val, None)?);
        o.extend(c.track(r + 0x38, 2, &copy_val, None)?);
        o.extend(c.track(r + 0x4c, 4, &copy_val, None)?);
        o.extend(c.track(r + 0x60, 4, &copy_val, None)?);
        o.extend_from_slice(slice(c.src, r + 0x74, 0x10)?);
        o.extend(c.track(r + 0x84, 2, &copy_val, None)?);
        o.extend(c.track(r + 0x98, 1, &copy_val, None)?);
        Ok(o)
    })?;
    put(&mut hdr, 0x134, &ribbons);
    let particles = c.records(0x128, 0x1dc, &particle)?;
    put(&mut hdr, 0x13c, &particles);

    let mut out = c.out;
    out[..HDR_256].copy_from_slice(&hdr);
    Ok(out)
}

/// One `M2FakeTrack` (264 particle colour/alpha/scale/cell ramps): `(timestamps, values)`, the
/// timestamps fixed-point fractions of the particle's life.
fn fake_track(b: &[u8], o: usize, vsize: usize) -> Result<(Vec<f32>, Vec<&[u8]>)> {
    let (tn, to) = arr(b, o)?;
    let (vn, vo) = arr(b, o + 8)?;
    let n = tn.min(vn);
    let mut ts = Vec::with_capacity(n);
    let mut vs = Vec::with_capacity(n);
    for k in 0..n {
        ts.push(f32::from(rd16(b, to + k * 2)?) / 32767.0);
        vs.push(slice(b, vo + k * vsize, vsize)?);
    }
    Ok((ts, vs))
}

/// Key `k` of a ramp, the last for a short one, `None` for an empty one.
fn key<'k>(vs: &[&'k [u8]], k: usize) -> Option<&'k [u8]> {
    vs.get(k.min(vs.len().saturating_sub(1))).copied()
}

fn f32_of(v: &[u8], o: usize) -> f32 {
    f32::from_le_bytes([v[o], v[o + 1], v[o + 2], v[o + 3]])
}

/// One particle emitter, `0x1dc` to `0x1f8` bytes (see the module comment).
fn particle(c: &mut Conv, r: usize) -> Result<Vec<u8>> {
    let s = c.src;
    let mut o = vec![0u8; 0x1f8];
    o[..0x18].copy_from_slice(slice(s, r, 0x18)?);
    let geo = c.copy_array(r + 0x18, 1)?;
    o[0x18..0x20].copy_from_slice(&geo);
    let rec = c.copy_array(r + 0x20, 1)?;
    o[0x20..0x28].copy_from_slice(&rec);
    // 264 packs blend and emitter type in a byte each; 1.12 gives each a u16.
    o[0x28] = s[r + 0x28];
    o[0x2a] = s[r + 0x29];
    o[0x2c..0x34].copy_from_slice(slice(s, r + 0x2c, 8)?);
    // The ten animated floats; 264 adds a variance after lifespan and after emission rate.
    for (from, to) in [
        (0x34, 0x34),
        (0x48, 0x50),
        (0x5c, 0x6c),
        (0x70, 0x88),
        (0x84, 0xa4),
        (0x98, 0xc0),
        (0xb0, 0xdc),
        (0xc8, 0xf8),
        (0xdc, 0x114),
        (0xf0, 0x130),
    ] {
        let t = c.track(r + from, 4, &copy_val, None)?;
        o[to..to + 28].copy_from_slice(&t);
    }
    // The ramps: start, middle and end of a particle's life.
    let (cts, cvs) = fake_track(s, r + 0x104, 12)?;
    let (_, avs) = fake_track(s, r + 0x114, 2)?;
    let (_, svs) = fake_track(s, r + 0x124, 8)?;
    let (_, hvs) = fake_track(s, r + 0x13c, 2)?;
    let (_, tvs) = fake_track(s, r + 0x14c, 2)?;
    let mid = if cts.len() >= 3 { cts[1] } else { 0.5 };
    o[0x14c..0x150].copy_from_slice(&mid.to_le_bytes());
    for k in 0..3 {
        let rgb = key(&cvs, k).map_or([255.0; 3], |v| [f32_of(v, 0), f32_of(v, 4), f32_of(v, 8)]);
        let a = key(&avs, k).map_or(255.0, |v| {
            f32::from(i16::from_le_bytes([v[0], v[1]])).max(0.0) / 32767.0 * 255.0
        });
        let byte = |x: f32| x.round().clamp(0.0, 255.0) as u32;
        let col = (byte(a) << 24) | (byte(rgb[0]) << 16) | (byte(rgb[1]) << 8) | byte(rgb[2]);
        o[0x150 + k * 4..0x154 + k * 4].copy_from_slice(&col.to_le_bytes());
        let sc = key(&svs, k).map_or(1.0, |v| f32_of(v, 0));
        o[0x15c + k * 4..0x160 + k * 4].copy_from_slice(&sc.to_le_bytes());
    }
    let cell = |vs: &[&[u8]], k: usize| key(vs, k).map_or(0u16, |v| u16::from_le_bytes([v[0], v[1]]));
    let put16 = |o: &mut Vec<u8>, at: usize, v: u16| o[at..at + 2].copy_from_slice(&v.to_le_bytes());
    // Head cells: life (start, middle, repeat 1), decay (middle, end, repeat 1).
    put16(&mut o, 0x168, cell(&hvs, 0));
    put16(&mut o, 0x16a, cell(&hvs, 1));
    put16(&mut o, 0x16c, 1);
    put16(&mut o, 0x16e, cell(&hvs, 1));
    put16(&mut o, 0x170, cell(&hvs, 2));
    put16(&mut o, 0x172, 1);
    // Tail cells: life (start, middle), decay (middle, end).
    put16(&mut o, 0x174, cell(&tvs, 0));
    put16(&mut o, 0x176, cell(&tvs, 1));
    put16(&mut o, 0x178, cell(&tvs, 1));
    put16(&mut o, 0x17a, cell(&tvs, 2));
    // tail length, twinkle (speed, percent, scale min/max), burst, drag.
    o[0x17c..0x198].copy_from_slice(slice(s, r + 0x15c, 0x1c)?);
    // 264's base spin and its variance have no 1.12 field; spin is the one 1.12 reads.
    o[0x198..0x19c].copy_from_slice(slice(s, r + 0x180, 4)?);
    // tumble box, wind vector and time, the two follow pairs.
    o[0x19c..0x1d4].copy_from_slice(slice(s, r + 0x188, 0x38)?);
    let spline = c.copy_array(r + 0x1c0, 12)?;
    o[0x1d4..0x1dc].copy_from_slice(&spline);
    let enabled = c.track(r + 0x1c8, 1, &copy_val, None)?;
    o[0x1dc..0x1f8].copy_from_slice(&enabled);
    Ok(o)
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// A tiny builder for version-264 test models.
    pub(crate) struct Build {
        pub b: Vec<u8>,
    }
    impl Build {
        pub fn new() -> Self {
            let mut b = vec![0u8; 0x130];
            b[..4].copy_from_slice(b"MD20");
            b[4..8].copy_from_slice(&264u32.to_le_bytes());
            Self { b }
        }
        pub fn data(&mut self, bytes: &[u8]) -> u32 {
            while !self.b.len().is_multiple_of(16) {
                self.b.push(0);
            }
            let at = self.b.len() as u32;
            self.b.extend_from_slice(bytes);
            at
        }
        pub fn set(&mut self, at: usize, bytes: &[u8]) {
            self.b[at..at + bytes.len()].copy_from_slice(bytes);
        }
        pub fn array(&mut self, at: usize, n: usize, bytes: &[u8]) {
            let ofs = self.data(bytes);
            self.set(at, &(n as u32).to_le_bytes());
            self.set(at + 4, &ofs.to_le_bytes());
        }
        /// A per-sequence track: `keys[seq]` is that sequence's `(ms, value bytes)`.
        pub fn track(&mut self, keys: &[Vec<(u32, Vec<u8>)>]) -> [u8; 20] {
            let mut touter = Vec::new();
            let mut vouter = Vec::new();
            for seq in keys {
                let ts: Vec<u8> = seq.iter().flat_map(|(t, _)| t.to_le_bytes()).collect();
                let vs: Vec<u8> = seq.iter().flat_map(|(_, v)| v.clone()).collect();
                let to = if ts.is_empty() { 0 } else { self.data(&ts) };
                let vo = if vs.is_empty() { 0 } else { self.data(&vs) };
                touter.extend((seq.len() as u32).to_le_bytes());
                touter.extend(to.to_le_bytes());
                vouter.extend((seq.len() as u32).to_le_bytes());
                vouter.extend(vo.to_le_bytes());
            }
            let to = self.data(&touter);
            let vo = self.data(&vouter);
            let mut t = [0u8; 20];
            t[0..2].copy_from_slice(&1u16.to_le_bytes());
            t[2..4].copy_from_slice(&0xffffu16.to_le_bytes());
            t[4..8].copy_from_slice(&(keys.len() as u32).to_le_bytes());
            t[8..12].copy_from_slice(&to.to_le_bytes());
            t[12..16].copy_from_slice(&(keys.len() as u32).to_le_bytes());
            t[16..20].copy_from_slice(&vo.to_le_bytes());
            t
        }
    }

    fn f3(x: f32, y: f32, z: f32) -> Vec<u8> {
        [x, y, z].iter().flat_map(|v| v.to_le_bytes()).collect()
    }

    /// A two-sequence, two-bone, one-triangle model with a skin.
    pub(crate) fn sample() -> (Vec<u8>, Vec<u8>) {
        let mut m = Build::new();
        // Sequences: Stand (0) 1000 ms, Walk (4) 500 ms, both in the .m2.
        let mut seqs = Vec::new();
        for (id, dur) in [(0u16, 1000u32), (4, 500)] {
            let mut s = vec![0u8; 0x40];
            s[0..2].copy_from_slice(&id.to_le_bytes());
            s[4..8].copy_from_slice(&dur.to_le_bytes());
            s[0x0c..0x10].copy_from_slice(&0x20u32.to_le_bytes());
            s[0x3c..0x3e].copy_from_slice(&0xffffu16.to_le_bytes());
            seqs.extend(s);
        }
        m.array(0x1c, 2, &seqs);
        // AnimationLookup: Stand at 0, Walk at 4.
        let mut lk = vec![0xffu8; 10];
        lk[0..2].copy_from_slice(&0u16.to_le_bytes());
        lk[8..10].copy_from_slice(&1u16.to_le_bytes());
        m.array(0x24, 5, &lk);
        // Bones: the root rotates in Stand only; the child translates in both.
        let q = |x: i16, w: i16| -> Vec<u8> {
            [x, 32767, 32767, w].iter().flat_map(|v| v.to_le_bytes()).collect()
        };
        let rot = m.track(&[vec![(0, q(32767, -1)), (500, q(0, -1))], vec![]]);
        let tr = m.track(&[vec![(0, f3(0.0, 0.0, 1.0))], vec![(100, f3(0.0, 0.0, 2.0))]]);
        let empty = m.track(&[vec![], vec![]]);
        let mut bones = Vec::new();
        for (parent, t, r) in [(-1i16, empty, rot), (0, tr, empty)] {
            let mut b = vec![0u8; 0x58];
            b[0..4].copy_from_slice(&(-1i32).to_le_bytes());
            b[8..10].copy_from_slice(&parent.to_le_bytes());
            b[0x10..0x24].copy_from_slice(&t);
            b[0x24..0x38].copy_from_slice(&r);
            b[0x38..0x4c].copy_from_slice(&empty);
            bones.extend(b);
        }
        m.array(0x2c, 2, &bones);
        // Three vertices.
        let mut verts = Vec::new();
        for (i, p) in [[0.0f32, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]].iter().enumerate() {
            let mut v = vec![0u8; 48];
            v[0..12].copy_from_slice(&f3(p[0], p[1], p[2]));
            v[12] = 255;
            v[16] = (i % 2) as u8;
            verts.extend(v);
        }
        m.array(0x3c, 3, &verts);
        m.set(0x44, &1u32.to_le_bytes());
        // One texture, monster skin 1.
        let mut tex = vec![0u8; 16];
        tex[0..4].copy_from_slice(&11u32.to_le_bytes());
        m.array(0x50, 1, &tex);
        // One material with blend mode 7.
        m.array(0x70, 1, &[0, 0, 7, 0]);
        m.array(0x80, 1, &[0, 0]);
        // Bounds.
        m.set(0xa0, &f3(-1.0, -1.0, 0.0));
        m.set(0xac, &f3(1.0, 1.0, 2.0));

        // The skin: 3 vertices, 1 triangle, 1 section, 1 batch.
        let mut s = vec![0u8; 48];
        s[..4].copy_from_slice(b"SKIN");
        let put = |s: &mut Vec<u8>, at: usize, n: usize, bytes: &[u8]| {
            let ofs = s.len() as u32;
            s.extend_from_slice(bytes);
            s[at..at + 4].copy_from_slice(&(n as u32).to_le_bytes());
            s[at + 4..at + 8].copy_from_slice(&ofs.to_le_bytes());
        };
        put(&mut s, 4, 3, &[0, 0, 1, 0, 2, 0]);
        put(&mut s, 12, 3, &[0, 0, 1, 0, 2, 0]);
        put(&mut s, 20, 3, &[0; 12]);
        let mut sec = vec![0u8; 48];
        sec[6..8].copy_from_slice(&3u16.to_le_bytes()); // vertex count
        sec[10..12].copy_from_slice(&3u16.to_le_bytes()); // index count
        put(&mut s, 28, 1, &sec);
        put(&mut s, 36, 1, &[0u8; 24]);
        s[44..48].copy_from_slice(&2u32.to_le_bytes());
        (m.b, s)
    }

    fn no_fallback(_: u16) -> Option<u16> {
        None
    }

    #[test]
    fn a_264_model_converts_to_a_256_model_benilla_reads() {
        let (m2, skin) = sample();
        let out = convert(&Input {
            m2: &m2,
            skins: vec![&skin],
            anims: HashMap::new(),
            playable_count: 203,
            fallback: &|id| if id == 5 { Some(4) } else { no_fallback(id) },
        })
        .unwrap();
        let parsed = benilla_m2::parse_m2(&mut std::io::Cursor::new(out.as_slice())).unwrap();
        let model = parsed.model();
        assert_eq!(model.vertices.len(), 3);
        assert_eq!(model.bones.len(), 2);
        assert_eq!(model.bones[1].parent, 0);
        assert_eq!(model.materials[0].blend_mode.bits(), 4, "blend-add becomes additive");
        assert_eq!(model.playable_animation_lookup.len(), 203);
        assert_eq!(model.playable_animation_lookup[4].resolved_id, 4, "Walk plays itself");
        assert_eq!(model.playable_animation_lookup[5].resolved_id, 4, "Run falls back to Walk");
        assert_eq!(model.playable_animation_lookup[7].resolved_id, 0, "a missing id is Stand");
        assert_eq!(model.header.bounding_box_max, [1.0, 1.0, 2.0]);
        let skin = model.parse_embedded_skin(&out, 0).unwrap();
        assert_eq!(skin.triangles().len(), 3);
        assert_eq!(skin.submeshes()[0].triangle_count, 3);

        // The timeline: Stand [0, 1000], Walk [1033, 1533].
        let seq = |i: usize, f: usize| rd32(&out, rd32(&out, 0x20).unwrap() as usize + i * 0x44 + f);
        assert_eq!((seq(0, 4).unwrap(), seq(0, 8).unwrap()), (0, 1000));
        assert_eq!((seq(1, 4).unwrap(), seq(1, 8).unwrap()), (1000 + SEQ_GAP, 1500 + SEQ_GAP));

        // The child bone's translation: one key per sequence, moved into each band.
        let bones_ofs = rd32(&out, 0x38).unwrap() as usize;
        let tr = bones_ofs + 0x6c + 0x0c;
        let (n, to) = arr(&out, tr + 0x0c).unwrap();
        assert_eq!(n, 2);
        assert_eq!(rd32(&out, to).unwrap(), 0);
        assert_eq!(rd32(&out, to + 4).unwrap(), 1000 + SEQ_GAP + 100);
        let (nr, ro) = arr(&out, tr + 0x04).unwrap();
        assert_eq!(nr, 2);
        assert_eq!((rd32(&out, ro + 8).unwrap(), rd32(&out, ro + 12).unwrap()), (1, 1));

        // The root's rotation: two float keys in Stand, a rest key for Walk.
        let rot = bones_ofs + 0x28;
        let (n, _) = arr(&out, rot + 0x0c).unwrap();
        assert_eq!(n, 3);
        let (_, vo) = arr(&out, rot + 0x14).unwrap();
        let first: Vec<f32> = (0..4).map(|k| f32_of(&out, vo + k * 4)).collect();
        assert!((first[0] - 0.0).abs() < 1e-4 && (first[3] - 1.0).abs() < 1e-3, "{first:?}");
        let rest: Vec<f32> = (0..4).map(|k| f32_of(&out, vo + 32 + k * 4)).collect();
        assert_eq!(rest, vec![0.0, 0.0, 0.0, 1.0]);

        // Through benilla's own animation reader too: two clips, each cut to its band.
        let anims = benilla_formats::parse_m2_animations(&out);
        assert_eq!(anims.len(), 2);
        assert_eq!((anims[1].anim_id, anims[1].start_ms), (4, 1000 + SEQ_GAP));
        assert_eq!(anims[0].bones.len(), 2);
        assert!(anims[0].bones[0].rotation.len() >= 2, "Stand's root rotation keys");
        assert_eq!(anims[1].bones[1].translation.len(), 1, "Walk's child translation key");
    }

    #[test]
    fn external_sequences_read_their_anim_file() {
        let (mut m2, skin) = sample();
        // Walk moves to an external file: flag 0x20 clear.
        let so = rd32(&m2, 0x20).unwrap() as usize;
        m2[so + 0x40 + 0x0c..so + 0x40 + 0x10].copy_from_slice(&0u32.to_le_bytes());
        assert_eq!(external_anims(&m2).unwrap(), vec![(4, 0)]);
        assert_eq!(anim_file_name("Creature\\A\\A.m2", 4, 0), "Creature\\A\\A0004-00.anim");
        // Without the file the child's Walk key is gone; with it, it is read from the file.
        let child_keys = |anims: HashMap<(u16, u16), Vec<u8>>| {
            let out = convert(&Input {
                m2: &m2,
                skins: vec![&skin],
                anims,
                playable_count: 10,
                fallback: &no_fallback,
            })
            .unwrap();
            let tr = rd32(&out, 0x38).unwrap() as usize + 0x6c + 0x0c;
            let (n, to) = arr(&out, tr + 0x0c).unwrap();
            let (_, vo) = arr(&out, tr + 0x14).unwrap();
            (0..n)
                .map(|k| (rd32(&out, to + k * 4).unwrap(), f32_of(&out, vo + k * 12 + 8)))
                .collect::<Vec<_>>()
        };
        // The .m2's own offsets for Walk's inner arrays now name the .anim file: build one whose
        // bytes at those offsets hold a different key.
        let bones = rd32(&m2, 0x30).unwrap() as usize;
        let tr = bones + 0x58 + 0x10;
        let touter = rd32(&m2, tr + 8).unwrap() as usize;
        let vouter = rd32(&m2, tr + 16).unwrap() as usize;
        let t_off = rd32(&m2, touter + 12).unwrap() as usize;
        let v_off = rd32(&m2, vouter + 12).unwrap() as usize;
        let mut anim = vec![0u8; v_off.max(t_off) + 16];
        anim[t_off..t_off + 4].copy_from_slice(&50u32.to_le_bytes());
        anim[v_off..v_off + 12].copy_from_slice(&f3(0.0, 0.0, 9.0));
        // Without it, Walk holds the rest pose.
        assert_eq!(child_keys(HashMap::new()), vec![(0, 1.0), (1000 + SEQ_GAP, 0.0)]);
        let with = child_keys(HashMap::from([((4, 0), anim)]));
        assert_eq!(with, vec![(0, 1.0), (1000 + SEQ_GAP + 50, 9.0)]);
    }

    #[test]
    fn other_versions_are_refused() {
        let (mut m2, skin) = sample();
        m2[4..8].copy_from_slice(&256u32.to_le_bytes());
        let r = convert(&Input {
            m2: &m2,
            skins: vec![&skin],
            anims: HashMap::new(),
            playable_count: 1,
            fallback: &no_fallback,
        });
        assert!(r.is_err());
        assert!(convert(&Input {
            m2: &sample().0,
            skins: vec![b"nope".as_slice()],
            anims: HashMap::new(),
            playable_count: 1,
            fallback: &no_fallback,
        })
        .is_err());
    }
}
