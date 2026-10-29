//! Havok packfiles (`.hkx`), as the source archives store them: binary packfile version 8
//! (`hk_2010.2.0-r1`), 64-bit little-endian layout (pointers 8 bytes; an `hkArray` is a pointer,
//! an `i32` size and an `i32` capacity-and-flags, 16 bytes).
//!
//! A packfile is a 64-byte header then 48-byte section headers (`__classnames__`, `__types__`,
//! `__data__`). Each section's fixup tables say where its pointers point, since the stored
//! pointers are zero: local fixups (`src`, `dst` pairs within the section), global fixups (`src`,
//! section, `dst`) and virtual fixups (`src`, the class-name section, the name's offset), the
//! last naming the class of each object that starts at `src`. Objects are read here at their
//! 2010 64-bit field offsets; `hkReferencedObject` (vtable, size-and-flags, reference count)
//! takes the first 16 bytes of each.

use bevy::math::{Quat, Vec3};
use std::collections::HashMap;

fn u32le(b: &[u8], i: usize) -> u32 {
    u32::from_le_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]])
}
fn i32le(b: &[u8], i: usize) -> i32 {
    u32le(b, i) as i32
}
fn f32le(b: &[u8], i: usize) -> f32 {
    f32::from_bits(u32le(b, i))
}

/// A packfile with its pointers resolved.
pub struct Packfile {
    pub data: Vec<u8>,
    /// Where each pointer field (by absolute offset) points (absolute offset).
    ptr: HashMap<usize, usize>,
    /// Each object's offset and class name.
    pub objects: Vec<(usize, String)>,
}

impl Packfile {
    pub fn parse(data: Vec<u8>) -> Result<Packfile, String> {
        if data.len() < 64 || u32le(&data, 0) != 0x57E0_E057 || u32le(&data, 4) != 0x10C0_C010 {
            return Err("not a Havok packfile".into());
        }
        let version = i32le(&data, 12);
        let layout = &data[16..20];
        if version != 8 || layout[0] != 8 || layout[1] != 1 {
            return Err(format!(
                "packfile version {version}, layout {layout:?}: only the 64-bit variant (8, 64-bit LE)"
            ));
        }
        let sections = i32le(&data, 20) as usize;
        struct Sec {
            start: usize,
            local: usize,
            global: usize,
            virt: usize,
            exports: usize,
        }
        let secs: Vec<Sec> = (0..sections)
            .map(|k| {
                let h = 64 + k * 48 + 20;
                let start = i32le(&data, h) as usize;
                let at = |n: usize| start + i32le(&data, h + 4 * n) as usize;
                Sec {
                    start,
                    local: at(1),
                    global: at(2),
                    virt: at(3),
                    exports: at(4),
                }
            })
            .collect();
        let cstr = |o: usize| -> String {
            let end = data[o..].iter().position(|&b| b == 0).unwrap_or(0);
            String::from_utf8_lossy(&data[o..o + end]).to_string()
        };
        let mut ptr = HashMap::new();
        let mut objects = Vec::new();
        for s in &secs {
            let mut i = s.local;
            while i + 8 <= s.global {
                let (src, dst) = (i32le(&data, i), i32le(&data, i + 4));
                if src >= 0 {
                    ptr.insert(s.start + src as usize, s.start + dst as usize);
                }
                i += 8;
            }
            let mut i = s.global;
            while i + 12 <= s.virt {
                let (src, sec, dst) = (i32le(&data, i), i32le(&data, i + 4), i32le(&data, i + 8));
                if src >= 0 && (sec as usize) < secs.len() {
                    ptr.insert(
                        s.start + src as usize,
                        secs[sec as usize].start + dst as usize,
                    );
                }
                i += 12;
            }
            let mut i = s.virt;
            while i + 12 <= s.exports {
                let (src, sec, dst) = (i32le(&data, i), i32le(&data, i + 4), i32le(&data, i + 8));
                if src >= 0 && (sec as usize) < secs.len() {
                    let name = cstr(secs[sec as usize].start + dst as usize);
                    objects.push((s.start + src as usize, name));
                }
                i += 12;
            }
        }
        Ok(Packfile { data, ptr, objects })
    }

    /// The first object of a class.
    pub fn find(&self, class: &str) -> Option<usize> {
        self.objects.iter().find(|(_, c)| c == class).map(|o| o.0)
    }

    fn pointer(&self, field: usize) -> Option<usize> {
        self.ptr.get(&field).copied()
    }

    /// An `hkArray` field: where its items start and how many.
    fn array(&self, field: usize) -> (usize, usize) {
        let n = i32le(&self.data, field + 8).max(0) as usize;
        match self.pointer(field) {
            Some(p) if n > 0 => (p, n),
            _ => (0, 0),
        }
    }

    /// An `hkStringPtr` field.
    fn string(&self, field: usize) -> Option<String> {
        let p = self.pointer(field)?;
        let end = self.data[p..].iter().position(|&b| b == 0)?;
        Some(String::from_utf8_lossy(&self.data[p..p + end]).to_string())
    }

    fn i16s(&self, field: usize) -> Vec<i16> {
        let (p, n) = self.array(field);
        (0..n)
            .map(|k| i16::from_le_bytes([self.data[p + 2 * k], self.data[p + 2 * k + 1]]))
            .collect()
    }
}

/// A bone's local transform (`hkQsTransform`): translation, rotation, scale.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Qs {
    pub t: Vec3,
    pub r: Quat,
    pub s: Vec3,
}

impl Qs {
    pub const IDENTITY: Qs = Qs {
        t: Vec3::ZERO,
        r: Quat::IDENTITY,
        s: Vec3::ONE,
    };

    /// `self` then `child`: the child's transform in this one's parent frame.
    pub fn then(&self, child: &Qs) -> Qs {
        Qs {
            t: self.t + self.r * (self.s * child.t),
            r: (self.r * child.r).normalize(),
            s: self.s * child.s,
        }
    }
}

/// `hkaSkeleton`: the bones, their parents (parents first) and the reference pose.
pub struct Skeleton {
    pub name: String,
    pub bones: Vec<String>,
    pub parents: Vec<i16>,
    pub reference: Vec<Qs>,
}

impl Skeleton {
    pub fn read(pf: &Packfile) -> Result<Skeleton, String> {
        let o = pf.find("hkaSkeleton").ok_or("no hkaSkeleton")?;
        let d = &pf.data;
        let parents = pf.i16s(o + 24);
        let (b, nb) = pf.array(o + 40); // hkaBone: name pointer, lockTranslation; 16 bytes
        let bones = (0..nb)
            .map(|k| pf.string(b + 16 * k).unwrap_or_default())
            .collect::<Vec<_>>();
        let (r, nr) = pf.array(o + 56); // hkQsTransform: 3 x 16 bytes
        let reference = (0..nr)
            .map(|k| {
                let f = |n: usize| f32le(d, r + 48 * k + 4 * n);
                Qs {
                    t: Vec3::new(f(0), f(1), f(2)),
                    r: Quat::from_xyzw(f(4), f(5), f(6), f(7)),
                    s: Vec3::new(f(8), f(9), f(10)),
                }
            })
            .collect::<Vec<_>>();
        if parents.len() != bones.len() || reference.len() != bones.len() {
            return Err("skeleton: bones, parents and pose disagree".into());
        }
        Ok(Skeleton {
            name: pf.string(o + 16).unwrap_or_default(),
            bones,
            parents,
            reference,
        })
    }

    pub fn bone(&self, name: &str) -> Option<usize> {
        self.bones.iter().position(|b| b == name)
    }

    /// Model-space transforms from local ones.
    pub fn model_space(&self, local: &[Qs]) -> Vec<Qs> {
        let mut out: Vec<Qs> = Vec::with_capacity(local.len());
        for (i, l) in local.iter().enumerate() {
            let w = match self.parents[i] {
                p if p >= 0 && (p as usize) < i => out[p as usize].then(l),
                _ => *l,
            };
            out.push(w);
        }
        out
    }
}

/// `hkaSplineCompressedAnimation` with its `hkaAnimationBinding`.
pub struct SplineAnimation {
    pub duration: f32,
    pub transform_tracks: usize,
    pub float_tracks: usize,
    pub frames: usize,
    pub blocks: usize,
    pub max_frames_per_block: usize,
    pub frame_duration: f32,
    pub block_offsets: Vec<u32>,
    pub data: Vec<u8>,
    /// Each transform track's bone; empty in the file means track `i` is bone `i`.
    pub track_to_bone: Vec<i16>,
    /// `hkaAnimationBinding::blendHint`: 0 normal, 1 additive.
    pub blend_hint: i8,
    /// The annotations (time, text), e.g. `weaponSwing`, `HitFrame`, `SoundPlay.*`.
    pub annotations: Vec<(f32, String)>,
}

impl SplineAnimation {
    pub fn read(pf: &Packfile) -> Result<SplineAnimation, String> {
        let o = pf
            .find("hkaSplineCompressedAnimation")
            .ok_or("no hkaSplineCompressedAnimation")?;
        let d = &pf.data;
        // hkaAnimation: type @16, duration @20, tracks @24, float tracks @28,
        // extractedMotion @32, annotationTracks @40.
        let transform_tracks = i32le(d, o + 24).max(0) as usize;
        let mut annotations = Vec::new();
        let (at, nt) = pf.array(o + 40); // hkaAnnotationTrack: name, annotations; 24 bytes
        for k in 0..nt {
            let (a, na) = pf.array(at + 24 * k + 8); // hkaAnnotation: time, text; 16 bytes
            for j in 0..na {
                if let Some(text) = pf.string(a + 16 * j + 8) {
                    annotations.push((f32le(d, a + 16 * j), text));
                }
            }
        }
        annotations.sort_by(|a, b| a.0.total_cmp(&b.0));
        let (bo, nbo) = pf.array(o + 88);
        let block_offsets = (0..nbo).map(|k| u32le(d, bo + 4 * k)).collect();
        let (da, dn) = pf.array(o + 152);
        let (track_to_bone, blend_hint) = match pf.find("hkaAnimationBinding") {
            Some(b) => (pf.i16s(b + 32), d[b + 64] as i8),
            None => (Vec::new(), 0),
        };
        let track_to_bone = if track_to_bone.is_empty() {
            (0..transform_tracks as i16).collect()
        } else {
            track_to_bone
        };
        Ok(SplineAnimation {
            duration: f32le(d, o + 20),
            transform_tracks,
            float_tracks: i32le(d, o + 28).max(0) as usize,
            frames: i32le(d, o + 56).max(0) as usize,
            blocks: i32le(d, o + 60).max(0) as usize,
            max_frames_per_block: i32le(d, o + 64).max(2) as usize,
            frame_duration: f32le(d, o + 80),
            block_offsets,
            data: d[da..da + dn].to_vec(),
            track_to_bone,
            blend_hint,
            annotations,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn transforms_compose_parent_first() {
        let parent = Qs {
            t: Vec3::new(0.0, 0.0, 10.0),
            r: Quat::from_rotation_z(std::f32::consts::FRAC_PI_2),
            s: Vec3::ONE,
        };
        let child = Qs {
            t: Vec3::new(1.0, 0.0, 0.0),
            ..Qs::IDENTITY
        };
        let w = parent.then(&child);
        assert!(w.t.distance(Vec3::new(0.0, 1.0, 10.0)) < 1e-5);
    }

    #[test]
    fn rejects_what_is_not_a_packfile() {
        assert!(Packfile::parse(vec![0; 80]).is_err());
    }
}
