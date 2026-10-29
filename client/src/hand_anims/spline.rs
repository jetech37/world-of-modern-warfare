//! Havok's spline-compressed animation (`hkaSplineCompressedAnimation`) decoded.
//!
//! The animation is cut into blocks of up to `max_frames_per_block` frames (consecutive blocks
//! share their boundary frame). A block is, for each transform track, a 4-byte mask
//! (quantization types; position, rotation and scale sub-track kinds), then one byte per float
//! track, padded to 4; then per track its position, rotation and scale:
//!
//! - position / scale: if any component is splined, the control-point count less one (u16), the
//!   degree (u8) and `count + degree + 2` knots (u8 frames within the block), pad 4; per
//!   component a (min, max) pair if splined or one f32 if static; then the control points,
//!   component by component, as 8- or 16-bit fractions of each range, pad 4. Otherwise one f32
//!   per static component. Missing components are 0 (position) or 1 (scale).
//! - rotation: splined (mask bits 4-7): count, degree and knots as above, pad to the
//!   quantization's alignment, then the quantized quaternions; static (bits 0-3): pad, one
//!   quaternion; else identity. Then pad 4.
//!
//! Quaternions are quantized as `POLAR32`, `THREECOMP40`, `THREECOMP48` or uncompressed: the
//! three-component kinds store three components scaled into ±1/√2 and the index of the largest,
//! which is rebuilt from the unit length. The curves are B-splines evaluated by de Boor's basis
//! (The NURBS Book, A2.1/A2.2). Ported from Lukas Cone's HavokLib (GPL-3.0; algorithm only).

use super::hkx::{Qs, SplineAnimation};
use bevy::math::{Quat, Vec3, Vec4};

/// A rotation quantization (mask bits 2-5 of the quantization byte).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RotQuant {
    Polar32,
    ThreeComp40,
    ThreeComp48,
    ThreeComp24,
    Straight16,
    Uncompressed,
}

impl RotQuant {
    fn from_bits(b: u8) -> Result<RotQuant, String> {
        Ok(match b {
            0 => RotQuant::Polar32,
            1 => RotQuant::ThreeComp40,
            2 => RotQuant::ThreeComp48,
            3 => RotQuant::ThreeComp24,
            4 => RotQuant::Straight16,
            5 => RotQuant::Uncompressed,
            _ => return Err(format!("rotation quantization {b}")),
        })
    }
    fn align(self) -> usize {
        match self {
            RotQuant::Polar32 | RotQuant::Uncompressed => 4,
            RotQuant::ThreeComp48 | RotQuant::Straight16 => 2,
            RotQuant::ThreeComp40 | RotQuant::ThreeComp24 => 1,
        }
    }
}

/// Rebuild a unit quaternion (x, y, z, w order) from three of its components, the missing one at
/// `index` and negative if `negative`.
fn with_largest(v: [f32; 3], index: usize, negative: bool) -> Quat {
    let mut w = (1.0 - v[0] * v[0] - v[1] * v[1] - v[2] * v[2])
        .max(0.0)
        .sqrt();
    if negative {
        w = -w;
    }
    let mut q = [0.0; 4];
    let mut src = v.iter();
    for (k, slot) in q.iter_mut().enumerate() {
        *slot = if k == index {
            w
        } else {
            *src.next().unwrap_or(&0.0)
        };
    }
    Quat::from_xyzw(q[0], q[1], q[2], q[3])
}

/// `THREECOMP40`: three 12-bit components, the largest's index (2 bits) and its sign (1 bit).
pub fn quat40(b: &[u8]) -> Quat {
    let c = (0..5).fold(0u64, |acc, k| acc | (b[k] as u64) << (8 * k));
    const SCALE: f32 = 0.000345436; // (1/√2) / 2047
    let comp = |k: u32| (((c >> (12 * k)) & 0xFFF) as i32 - 2047) as f32 * SCALE;
    with_largest(
        [comp(0), comp(1), comp(2)],
        ((c >> 36) & 3) as usize,
        (c >> 38) & 1 != 0,
    )
}

/// `THREECOMP48`: three 15-bit components; the largest's index in the top bits of the first
/// two, its sign in the third's.
pub fn quat48(b: &[u8]) -> Quat {
    let s = |k: usize| u16::from_le_bytes([b[2 * k], b[2 * k + 1]]);
    let (x, y, z) = (s(0), s(1), s(2));
    const SCALE: f32 = 0.000043161; // (1/√2) / 16383
    let comp = |v: u16| ((v & 0x7FFF) as i32 - 0x3FFF) as f32 * SCALE;
    let index = (((y >> 14) & 2) | ((x >> 15) & 1)) as usize;
    with_largest([comp(x), comp(y), comp(z)], index, z >> 15 != 0)
}

/// `POLAR32`: w from a 10-bit radius, the xyz direction from an 18-bit polar pair, and a sign bit
/// per component.
pub fn quat32(b: &[u8]) -> Quat {
    use std::f32::consts::{FRAC_PI_2, FRAC_PI_4};
    let c = u32::from_le_bytes([b[0], b[1], b[2], b[3]]);
    let r = ((c >> 18) & 0x3FF) as f32 / 1023.0;
    let r = 1.0 - r * r;
    let phi_theta = (c & 0x3FFFF) as f32;
    let mut phi = phi_theta.sqrt().floor();
    let mut theta = 0.0;
    if phi > 0.0 {
        theta = FRAC_PI_4 * (phi_theta - phi * phi) / phi;
        phi *= FRAC_PI_2 / 511.0;
    }
    let m = (1.0 - r * r).max(0.0).sqrt();
    let mut v = [
        phi.sin() * theta.cos() * m,
        phi.sin() * theta.sin() * m,
        phi.cos() * m,
        r,
    ];
    for (k, c_) in v.iter_mut().enumerate() {
        if c & (0x1000_0000 << k) != 0 {
            *c_ = -*c_;
        }
    }
    Quat::from_xyzw(v[0], v[1], v[2], v[3])
}

fn read_quat(q: RotQuant, b: &[u8], i: &mut usize) -> Result<Quat, String> {
    let (size, f): (usize, fn(&[u8]) -> Quat) = match q {
        RotQuant::Polar32 => (4, quat32),
        RotQuant::ThreeComp40 => (5, quat40),
        RotQuant::ThreeComp48 => (6, quat48),
        RotQuant::Uncompressed => (16, |b| {
            let f =
                |k: usize| f32::from_le_bytes([b[4 * k], b[4 * k + 1], b[4 * k + 2], b[4 * k + 3]]);
            Quat::from_xyzw(f(0), f(1), f(2), f(3))
        }),
        other => return Err(format!("unsupported rotation quantization {other:?}")),
    };
    let bytes = b
        .get(*i..*i + size)
        .ok_or("spline: quaternion past the end")?;
    *i += size;
    Ok(f(bytes))
}

/// The knot span holding `t` (The NURBS Book, A2.1), for `n` control points.
fn find_span(degree: usize, t: f32, n: usize, knots: &[u8]) -> usize {
    if t >= knots[n] as f32 {
        return n - 1;
    }
    let (mut lo, mut hi) = (degree, n);
    let mut mid = (lo + hi) / 2;
    while t < knots[mid] as f32 || t >= knots[mid + 1] as f32 {
        if t < knots[mid] as f32 {
            hi = mid;
        } else {
            lo = mid;
        }
        let next = (lo + hi) / 2;
        if next == mid {
            break;
        }
        mid = next;
    }
    mid
}

/// The nonzero B-spline basis values at `t` in `span`, `n[i]` weighting control point
/// `span - i` (HavokLib's ordering).
fn basis(span: usize, degree: usize, t: f32, knots: &[u8]) -> [f32; 5] {
    let mut n = [0.0f32; 5];
    n[0] = 1.0;
    for i in 1..=degree {
        for j in (0..i).rev() {
            let lo = knots[span - j] as f32;
            let hi = knots[span + i - j] as f32;
            let a = if hi > lo { (t - lo) / (hi - lo) } else { 0.0 };
            let tmp = n[j] * a;
            n[j + 1] += n[j] - tmp;
            n[j] = tmp;
        }
    }
    n
}

/// A B-spline (or a constant, with one control point) over a block's local frames.
#[derive(Clone, Debug)]
pub struct Curve<T> {
    pub degree: usize,
    pub knots: Vec<u8>,
    pub points: Vec<T>,
}

impl<T> Curve<T>
where
    T: Copy + std::ops::Mul<f32, Output = T> + std::ops::Add<Output = T>,
{
    pub fn at(&self, t: f32) -> T {
        let n = self.points.len();
        if n == 1 || self.knots.is_empty() {
            return self.points[0];
        }
        let degree = self.degree.min(4);
        let span = find_span(degree, t, n, &self.knots);
        let w = basis(span, degree, t, &self.knots);
        let mut out = self.points[span] * w[0];
        for (i, wi) in w.iter().enumerate().take(degree + 1).skip(1) {
            out = out + self.points[span - i] * *wi;
        }
        out
    }
}

/// One track's decoded curves within a block.
#[derive(Clone, Debug)]
pub struct TrackCurves {
    pub translation: [Curve<f32>; 3],
    pub rotation: Curve<Vec4>,
    pub scale: [Curve<f32>; 3],
}

fn constant<T>(v: T) -> Curve<T> {
    Curve {
        degree: 0,
        knots: Vec::new(),
        points: vec![v],
    }
}

impl TrackCurves {
    pub fn at(&self, t: f32) -> Qs {
        let v = |c: &[Curve<f32>; 3]| Vec3::new(c[0].at(t), c[1].at(t), c[2].at(t));
        let q = self.rotation.at(t);
        let q = Quat::from_vec4(q);
        Qs {
            t: v(&self.translation),
            r: if q.length_squared() > 1e-12 {
                q.normalize()
            } else {
                Quat::IDENTITY
            },
            s: v(&self.scale),
        }
    }
}

struct Reader<'a> {
    b: &'a [u8],
    i: usize,
    base: usize,
}

impl Reader<'_> {
    fn u8(&mut self) -> Result<u8, String> {
        let v = *self.b.get(self.i).ok_or("spline: past the end")?;
        self.i += 1;
        Ok(v)
    }
    fn u16(&mut self) -> Result<u16, String> {
        Ok(self.u8()? as u16 | (self.u8()? as u16) << 8)
    }
    fn f32(&mut self) -> Result<f32, String> {
        let s = self
            .b
            .get(self.i..self.i + 4)
            .ok_or("spline: past the end")?;
        self.i += 4;
        Ok(f32::from_le_bytes([s[0], s[1], s[2], s[3]]))
    }
    fn bytes(&mut self, n: usize) -> Result<Vec<u8>, String> {
        let s = self
            .b
            .get(self.i..self.i + n)
            .ok_or("spline: past the end")?;
        self.i += n;
        Ok(s.to_vec())
    }
    fn align(&mut self, a: usize) {
        let r = (self.i - self.base) % a;
        if r != 0 {
            self.i += a - r;
        }
    }
}

/// Position or scale: bits 0-2 static x/y/z, bits 4-6 splined x/y/z.
fn read_vector(
    r: &mut Reader,
    kinds: u8,
    sixteen_bit: bool,
    default: f32,
) -> Result<[Curve<f32>; 3], String> {
    let is_static = |k: usize| kinds & (1 << k) != 0;
    let is_spline = |k: usize| !is_static(k) && kinds & (0x10 << k) != 0;
    if (0..3).any(is_spline) {
        let count = r.u16()? as usize;
        let degree = r.u8()? as usize;
        let knots = r.bytes(count + degree + 2)?;
        r.align(4);
        let mut ranges = [(0.0f32, 0.0f32); 3];
        let mut out: [Curve<f32>; 3] = [constant(default), constant(default), constant(default)];
        for k in 0..3 {
            if is_spline(k) {
                ranges[k] = (r.f32()?, r.f32()?);
                out[k] = Curve {
                    degree,
                    knots: knots.clone(),
                    points: Vec::with_capacity(count + 1),
                };
            } else if is_static(k) {
                out[k] = constant(r.f32()?);
            }
        }
        for _ in 0..=count {
            for k in 0..3 {
                if is_spline(k) {
                    let f = if sixteen_bit {
                        r.u16()? as f32 / 65535.0
                    } else {
                        r.u8()? as f32 / 255.0
                    };
                    let (lo, hi) = ranges[k];
                    out[k].points.push(lo + (hi - lo) * f);
                }
            }
        }
        r.align(4);
        Ok(out)
    } else {
        let mut v = [default; 3];
        for (k, slot) in v.iter_mut().enumerate() {
            if is_static(k) {
                *slot = r.f32()?;
            }
        }
        Ok(v.map(constant))
    }
}

/// Decode one block's transform tracks (`base` is the block's offset in `data`).
pub fn read_block(
    data: &[u8],
    base: usize,
    tracks: usize,
    float_tracks: usize,
) -> Result<Vec<TrackCurves>, String> {
    let masks = data
        .get(base..base + 4 * tracks)
        .ok_or("spline: masks past the end")?
        .to_vec();
    let mut r = Reader {
        b: data,
        i: base + 4 * tracks + float_tracks,
        base,
    };
    r.align(4);
    let mut out = Vec::with_capacity(tracks);
    for &[quant, pos, rot, scale] in masks.as_chunks::<4>().0 {
        let translation = read_vector(&mut r, pos, quant & 3 != 0, 0.0)?;
        let rq = RotQuant::from_bits((quant >> 2) & 0xF)?;
        let rotation = if rot & 0xF0 != 0 {
            let count = r.u16()? as usize;
            let degree = r.u8()? as usize;
            let knots = r.bytes(count + degree + 2)?;
            r.align(rq.align());
            let mut points = Vec::with_capacity(count + 1);
            for _ in 0..=count {
                points.push(Vec4::from(read_quat(rq, data, &mut r.i)?));
            }
            Curve {
                degree,
                knots,
                points,
            }
        } else if rot & 0x0F != 0 {
            r.align(rq.align());
            constant(Vec4::from(read_quat(rq, data, &mut r.i)?))
        } else {
            constant(Vec4::W)
        };
        r.align(4);
        let scale = read_vector(&mut r, scale, (quant >> 6) & 3 != 0, 1.0)?;
        out.push(TrackCurves {
            translation,
            rotation,
            scale,
        });
    }
    Ok(out)
}

/// A whole animation decoded, ready to sample.
pub struct Decoded {
    pub anim: SplineAnimation,
    pub blocks: Vec<Vec<TrackCurves>>,
}

impl Decoded {
    pub fn new(anim: SplineAnimation) -> Result<Decoded, String> {
        let blocks = anim
            .block_offsets
            .iter()
            .map(|&o| {
                read_block(
                    &anim.data,
                    o as usize,
                    anim.transform_tracks,
                    anim.float_tracks,
                )
            })
            .collect::<Result<Vec<_>, _>>()?;
        if blocks.is_empty() {
            return Err("spline: no blocks".into());
        }
        Ok(Decoded { anim, blocks })
    }

    /// Every track's local transform at `time` seconds.
    pub fn sample(&self, time: f32) -> Vec<Qs> {
        let a = &self.anim;
        let last = a.frames.saturating_sub(1) as f32;
        let frame = (time / a.frame_duration).clamp(0.0, last);
        let per = (a.max_frames_per_block - 1) as f32;
        let block = ((frame / per).floor() as usize).min(self.blocks.len() - 1);
        let local = frame - block as f32 * per;
        self.blocks[block].iter().map(|t| t.at(local)).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pack40(c: [u32; 3], index: u64, negative: bool) -> [u8; 5] {
        let v = c[0] as u64
            | (c[1] as u64) << 12
            | (c[2] as u64) << 24
            | index << 36
            | (negative as u64) << 38;
        let b = v.to_le_bytes();
        [b[0], b[1], b[2], b[3], b[4]]
    }

    #[test]
    fn the_identity_in_forty_bits() {
        // Zero x, y, z (2047 is zero) and the largest, w (index 3), rebuilt as 1.
        let q = quat40(&pack40([2047, 2047, 2047], 3, false));
        assert!(q.abs_diff_eq(Quat::IDENTITY, 1e-6));
        let q = quat40(&pack40([2047, 2047, 2047], 0, true));
        assert!(q.abs_diff_eq(Quat::from_xyzw(-1.0, 0.0, 0.0, 0.0), 1e-6));
    }

    #[test]
    fn a_half_turn_component_in_forty_bits() {
        // x = 0.5: 2047 + 0.5 / 0.000345436.
        let x = (2047.0 + 0.5 / 0.000345436f32).round() as u32;
        let q = quat40(&pack40([x, 2047, 2047], 3, false));
        assert!((q.x - 0.5).abs() < 1e-3 && (q.w - 0.75f32.sqrt()).abs() < 1e-3);
        assert!((q.length() - 1.0).abs() < 1e-5);
    }

    #[test]
    fn forty_eight_bits_put_the_largest_in_place() {
        // y = -0.25 (stored first, since the largest is x, index 1 from the bits), z = 0, w = 0;
        // the index's low bit in the first word's top bit, its high bit in the second's.
        let c = |v: f32| ((v / 0.000043161) as i32 + 0x3FFF) as u16;
        let words = [c(-0.25) | 0x8000, c(0.0), c(0.0)];
        let mut b = [0u8; 6];
        for (k, w) in words.iter().enumerate() {
            b[2 * k..2 * k + 2].copy_from_slice(&w.to_le_bytes());
        }
        let q = quat48(&b);
        // Index 1: (stored 0, largest, stored 1, stored 2).
        assert!((q.x + 0.25).abs() < 1e-3 && q.y > 0.96 && q.z.abs() < 1e-3);
    }

    #[test]
    fn polar_thirty_two_is_unit_and_signed() {
        let q = quat32(&0u32.to_le_bytes());
        assert!(q.abs_diff_eq(Quat::IDENTITY, 1e-6));
        // All sign bits set, radius 0 (w = 1): w negated.
        let q = quat32(&0xF000_0000u32.to_le_bytes());
        assert!((q.w + 1.0).abs() < 1e-6);
        for c in [0x0123_4567u32, 0x0ABC_DEF0, 0x0FFF_FFFF] {
            assert!((quat32(&c.to_le_bytes()).length() - 1.0).abs() < 1e-4);
        }
    }

    #[test]
    fn a_degree_one_spline_interpolates_linearly() {
        // Clamped knots for 3 points, degree 1: 0 0 5 10 10.
        let c = Curve {
            degree: 1,
            knots: vec![0, 0, 5, 10, 10],
            points: vec![0.0f32, 10.0, 0.0],
        };
        assert!((c.at(0.0) - 0.0).abs() < 1e-5);
        assert!((c.at(2.5) - 5.0).abs() < 1e-5);
        assert!((c.at(5.0) - 10.0).abs() < 1e-5);
        assert!((c.at(10.0) - 0.0).abs() < 1e-5);
    }

    #[test]
    fn a_clamped_cubic_hits_its_ends_and_stays_in_the_hull() {
        let c = Curve {
            degree: 3,
            knots: vec![0, 0, 0, 0, 4, 8, 8, 8, 8],
            points: vec![1.0f32, 3.0, -2.0, 5.0, 2.0],
        };
        assert!((c.at(0.0) - 1.0).abs() < 1e-5 && (c.at(8.0) - 2.0).abs() < 1e-5);
        for k in 0..=80 {
            let v = c.at(k as f32 * 0.1);
            assert!((-2.0..=5.0).contains(&v));
        }
    }

    #[test]
    fn a_block_of_one_static_and_one_splined_track() {
        let mut b = Vec::new();
        // Track 0: static x position (bit 0), identity rotation, identity scale.
        b.extend([0x00, 0x01, 0x00, 0x00]);
        // Track 1: 8-bit splined y position (bit 5), static 40-bit rotation, no scale.
        b.extend([0x04, 0x20, 0x01, 0x00]);
        b.extend(7.5f32.to_le_bytes()); // track 0's x
        // Track 1 position: 1 + 1 points, degree 1, knots 0 0 2 2; pad to 4.
        b.extend([1, 0, 1, 0, 0, 2, 2]);
        b.push(0);
        b.extend((-1.0f32).to_le_bytes());
        b.extend(1.0f32.to_le_bytes());
        b.extend([0, 255]); // y from -1 to 1
        b.extend([0, 0]); // pad 4
        b.extend(pack40([2047, 2047, 2047], 3, false)); // rotation: identity
        let tracks = read_block(&b, 0, 2, 0).unwrap();
        let t0 = tracks[0].at(1.0);
        assert_eq!(t0.t, Vec3::new(7.5, 0.0, 0.0));
        assert_eq!(t0.s, Vec3::ONE);
        let t1 = tracks[1].at(1.0);
        assert!(t1.t.y.abs() < 1e-5 && tracks[1].at(2.0).t.y > 0.99);
        assert!(t1.r.abs_diff_eq(Quat::IDENTITY, 1e-6));
    }
}
