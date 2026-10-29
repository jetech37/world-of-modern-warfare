//! First-person animation tracks as hand tracks for our melee and magic arms.
//!
//! Tracks are converted offline (the
//! archive [`bsa`] with [`lz4`], the Havok packfiles [`hkx`], the spline-compressed animations
//! [`spline`], composed into camera-relative grips by [`extract`]) into
//! `assets/hand_anims/<name>.json`, which are embedded here when the build finds them (`build.rs`:
//! the client's `assets/hand_anims/` or `private-assets/client/assets/hand_anims/`).
//! A track that isn't embedded loads as `None` and the hands keep their
//! hand-made keyframes. The tracks' skeleton is not retargeted:
//! each frame keeps only where each hand holds its weapon, relative to the first-person camera,
//! as a [`Grip`] in the eye's frame (inches; x right, y up, z back), which the arms reach for by
//! IK like the hand-made keyframes of [`crate::melee_pose`].
//!
//! The track format (`fps`, `duration`, `frames[]` of `t`, `right` and `left` grips, the wrists,
//! the camera kick, and the tracks' `events`) is described on [`JsonTrack`]; the frames' conventions
//! on [`extract`].

pub mod bsa;
pub mod extract;
pub mod hkx;
pub mod lz4;
pub mod spline;

use crate::hand_shape::HandShape;
use crate::melee_pose::Grip;
use bevy::math::Vec3;
use serde::{Deserialize, Serialize};

/// A grip as stored: the place (inches) and two unit directions.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct JsonGrip {
    pub at: [f32; 3],
    pub blade: [f32; 3],
    pub edge: [f32; 3],
}

/// The first-person camera node's own motion from its rest place (the animation's camera kick):
/// its offset (inches) and where it looks and its up, in the rest camera's eye frame.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct JsonCamera {
    pub at: [f32; 3],
    pub forward: [f32; 3],
    pub up: [f32; 3],
}

/// One sampled frame, everything relative to the (animated) first-person camera.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct JsonFrame {
    pub t: f32,
    /// The right fist (The tracks' `Weapon` node).
    pub right: JsonGrip,
    /// The left fist (The tracks' `Shield` node).
    pub left: JsonGrip,
    /// The wrists (`NPC R Hand [RHnd]`, `NPC L Hand [LHnd]`), for the arms' IK.
    pub right_wrist: [f32; 3],
    pub left_wrist: [f32; 3],
    pub camera: JsonCamera,
    /// Each hand's shape (crate::hand_shape): the middle finger's joints and the thumb's, in the
    /// hand's own frame. Tracks exported before the fingers were have none: a fist.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub right_hand: Option<JsonHand>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub left_hand: Option<JsonHand>,
}

/// A hand's shape as stored.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct JsonHand {
    pub mid: [[f32; 3]; 3],
    pub thumb: [[f32; 3]; 3],
    /// Where a spell held in the hand sits (`MagicNode`); a fist's place without.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub magic: Option<[f32; 3]>,
}

impl JsonHand {
    pub fn shape(&self) -> HandShape {
        HandShape {
            mid: self.mid.map(v),
            thumb: self.thumb.map(v),
            magic: self.magic.map_or(HandShape::FIST.magic, v),
        }
    }
}

/// An annotation of the tracks': `weaponSwing`, `HitFrame`, `preHitFrame`, `SoundPlay.*`, ...
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct JsonEvent {
    pub t: f32,
    pub name: String,
}

/// A whole exported animation: `assets/hand_anims/<name>.json`.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct JsonTrack {
    pub name: String,
    /// The path in `source archive` it was read from.
    pub source: String,
    pub fps: f32,
    /// Seconds.
    pub duration: f32,
    /// An additive (offset) animation, not a whole pose; its grips are the reference pose
    /// moved by it, only roughly meaningful.
    #[serde(default)]
    pub additive: bool,
    pub frames: Vec<JsonFrame>,
    #[serde(default)]
    pub events: Vec<JsonEvent>,
}

/// A loaded track.
#[derive(Clone, Debug)]
pub struct Track {
    pub fps: f32,
    pub duration: f32,
    /// Time (seconds), the right hand's grip, the left's.
    pub frames: Vec<(f32, Grip, Grip)>,
    /// The right and left wrists, frame by frame.
    pub wrists: Vec<(Vec3, Vec3)>,
    /// The tracks' annotations: time (seconds) and name.
    pub events: Vec<(f32, String)>,
}

fn v(a: [f32; 3]) -> Vec3 {
    Vec3::from_array(a)
}

fn grip(g: &JsonGrip, hand: &Option<JsonHand>) -> Grip {
    Grip {
        hand: hand.as_ref().map_or(HandShape::FIST, JsonHand::shape),
        at: v(g.at),
        blade: v(g.blade).normalize_or(Vec3::NEG_Z),
        edge: v(g.edge).normalize_or(Vec3::X),
    }
}

impl Track {
    pub fn parse(json: &str) -> Result<Track, String> {
        let t: JsonTrack = serde_json::from_str(json).map_err(|e| e.to_string())?;
        if t.frames.is_empty() {
            return Err(format!("{}: no frames", t.name));
        }
        Ok(Track {
            fps: t.fps,
            duration: t.duration,
            frames: t
                .frames
                .iter()
                .map(|f| {
                    (
                        f.t,
                        grip(&f.right, &f.right_hand),
                        grip(&f.left, &f.left_hand),
                    )
                })
                .collect(),
            wrists: t
                .frames
                .iter()
                .map(|f| (v(f.right_wrist), v(f.left_wrist)))
                .collect(),
            events: t.events.into_iter().map(|e| (e.t, e.name)).collect(),
        })
    }

    /// When an event (by name, any case) first happens, as a share (0..1) of the track.
    pub fn event(&self, name: &str) -> Option<f32> {
        let e = self
            .events
            .iter()
            .find(|e| e.1.eq_ignore_ascii_case(name))?;
        Some((e.0 / self.duration.max(1e-6)).clamp(0.0, 1.0))
    }
}

/// Between two grips: the place along the line, the directions turned (normalized lerp, which
/// at 30 frames a second is close to a slerp), the edge kept square to the blade.
fn blend(a: &Grip, b: &Grip, k: f32) -> Grip {
    let blade = a.blade.lerp(b.blade, k).normalize_or(a.blade);
    let edge = a.edge.lerp(b.edge, k);
    let edge = (edge - blade * edge.dot(blade))
        .try_normalize()
        .unwrap_or_else(|| blade.any_orthonormal_vector());
    Grip {
        at: a.at.lerp(b.at, k),
        blade,
        edge,
        hand: a.hand.mix(&b.hand, k),
    }
}

/// The frame index before `k` (0..1 of the track) and the share of the way to the next.
fn locate(track: &Track, k: f32) -> (usize, f32) {
    let t = k.clamp(0.0, 1.0) * track.duration;
    let f = &track.frames;
    let i = f.partition_point(|x| x.0 <= t).saturating_sub(1);
    if i + 1 >= f.len() {
        return (f.len() - 1, 0.0);
    }
    let span = f[i + 1].0 - f[i].0;
    let s = if span > 0.0 { (t - f[i].0) / span } else { 0.0 };
    (i, s.clamp(0.0, 1.0))
}

/// The right and left grips `k` (0..1) of the way through a track.
pub fn sample(track: &Track, k: f32) -> (Grip, Grip) {
    let (i, s) = locate(track, k);
    let a = &track.frames[i];
    let Some(b) = track.frames.get(i + 1) else {
        return (a.1, a.2);
    };
    (blend(&a.1, &b.1, s), blend(&a.2, &b.2, s))
}

/// The right and left wrists `k` (0..1) of the way through a track.
pub fn sample_wrists(track: &Track, k: f32) -> (Vec3, Vec3) {
    let (i, s) = locate(track, k);
    let a = track.wrists[i];
    let b = track.wrists.get(i + 1).copied().unwrap_or(a);
    (a.0.lerp(b.0, s), a.1.lerp(b.1, s))
}

// `embedded_source`: the tracks this build found (build.rs).
include!(concat!(env!("OUT_DIR"), "/hand_anim_tracks.rs"));

macro_rules! embedded {
    ($($name:literal),* $(,)?) => {
        /// Every track's name the hands may ask for (The tracks' file name, without `.hkx`).
        pub const NAMES: &[&str] = &[$($name),*];

        fn source(name: &str) -> Option<&'static str> {
            NAMES.contains(&name).then(|| embedded_source(name)).flatten()
        }
    };
}

// The tracks the converter exports.
embedded!(
    "1hm_idle",
    "1hm_attackright",
    "1hm_attackleft",
    "1hm_attackrightdiagonal",
    "1hm_attacklefttdiagonal",
    "1hm_attackstab",
    "1hm_attackpower",
    "1hm_attackpowerleft",
    "1hm_attackpowerright",
    "1hm_attackforwardsprint",
    "1hm_attackpowerforwardsprint",
    "1hm_blockidle",
    "1hm_blockhit",
    "1hm_blockbash",
    "1hm_blockbashpower",
    "1hm_equip",
    "1hm_unequip",
    "2hm_idle",
    "2hm_attackright",
    "2hm_attackleft",
    "2hm_attackpower",
    "2hm_attackforwardsprint",
    "2hm_attackpowerforwardsprint",
    "2hm_blockidle",
    "2hm_blockhit",
    "2hm_blockbash",
    "2hm_sprintforwardsword",
    "2hc_equip",
    "2hc_unequip",
    "2hw_idle",
    "2hw_attackright",
    "2hw_attackleft",
    "2hw_attackpower",
    "2hw_blockidle",
    "2hw_blockbash",
    "2hw_equip",
    "2hw_unequip",
    "2hw_sprintforwardsword",
    "dw1hm1hmidle",
    "dw1hm1hm_attackright",
    "dw1hm1hm_powerattack",
    "dw1hm1hmblockidle",
    "dw1hm1hmblockbash",
    "dw_attackpowerforward",
    "dw_sprintforwardsword",
    "mlh_1hm_attackforward",
    "mlh_1hm_attackforwardintro",
    "mlh_1hm_equip",
    "shd_blockidle",
    "shd_blockhit",
    "shd_blockbash",
    "shd_blockbashpower",
    "shd_blockbashsprint",
    "shd_equip_1hmout",
    "mrh_idle",
    "mlh_idle",
    "dualmagic_idle",
    "mrh_equip",
    "mlh_equip",
    "mrh_unequip",
    "mlh_unequip",
    "mrh_and_mlh_equip",
    "mrh_and_mlh_unequip",
    "mrh_precharge",
    "mlh_precharge",
    "mrh_chargeloop",
    "mlh_chargeloop",
    "mlh_preready",
    "mrh_readyloop",
    "mlh_readyloop",
    "mrh_release",
    "mlh_release",
    "mrh_preaimedcon",
    "mlh_preaimedcon",
    "mrh_aimedconcentration",
    "mlh_aimedconcentration",
    "mrh_wardloop",
    "mlh_wardloop",
    "magic_sprintforward",
    "h2h_idle",
    "h2h_attackright",
    "h2h_attackleft",
    "h2h_equip",
    "h2h_unequip",
    "mt_idle",
    "mt_sprintforward",
    "mt_sprintforwardsword",
);

/// An embedded track by name (e.g. `"1hm_attackright"`), parsed.
pub fn load(name: &str) -> Option<Track> {
    Track::parse(source(name)?).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn g(at: Vec3, blade: Vec3, edge: Vec3) -> JsonGrip {
        JsonGrip {
            at: at.to_array(),
            blade: blade.to_array(),
            edge: edge.to_array(),
        }
    }

    fn two_frames() -> Track {
        let frame = |t: f32, x: f32, blade: Vec3| JsonFrame {
            t,
            right: g(Vec3::new(x, 0.0, -10.0), blade, Vec3::X),
            left: g(Vec3::new(-x, 0.0, -10.0), Vec3::Y, Vec3::NEG_Z),
            right_wrist: [x, -2.0, -8.0],
            left_wrist: [-x, -2.0, -8.0],
            camera: JsonCamera {
                at: [0.0; 3],
                forward: [0.0, 0.0, -1.0],
                up: [0.0, 1.0, 0.0],
            },
            right_hand: None,
            left_hand: None,
        };
        let json = JsonTrack {
            name: "test".into(),
            source: "none".into(),
            fps: 30.0,
            duration: 1.0,
            additive: false,
            frames: vec![frame(0.0, 0.0, Vec3::Y), frame(1.0, 10.0, Vec3::NEG_Z)],
            events: vec![JsonEvent {
                t: 0.25,
                name: "HitFrame".into(),
            }],
        };
        Track::parse(&serde_json::to_string(&json).unwrap()).unwrap()
    }

    #[test]
    fn sampling_interpolates_and_clamps() {
        let t = two_frames();
        let (r, l) = sample(&t, 0.5);
        assert!((r.at.x - 5.0).abs() < 1e-4 && (l.at.x + 5.0).abs() < 1e-4);
        // Halfway from up to forward.
        assert!(r.blade.distance(Vec3::new(0.0, 1.0, -1.0).normalize()) < 1e-4);
        assert!(r.edge.dot(r.blade).abs() < 1e-4 && (r.edge.length() - 1.0).abs() < 1e-4);
        assert_eq!(sample(&t, -1.0).0.at, Vec3::new(0.0, 0.0, -10.0));
        assert_eq!(sample(&t, 2.0).0.at, Vec3::new(10.0, 0.0, -10.0));
        let (rw, _) = sample_wrists(&t, 0.5);
        assert!((rw.x - 5.0).abs() < 1e-4);
        assert!((t.event("HitFrame").unwrap() - 0.25).abs() < 1e-6);
        assert!(t.event("nothing").is_none());
    }

    #[test]
    #[cfg_attr(not(aw_hand_anim_tracks), ignore = "needs the hand tracks (private-assets.md)")]
    fn a_hand_ready_with_a_spell_is_open_and_one_round_a_hilt_is_a_fist() {
        let right_mid_tip = |name: &str| sample(&load(name).unwrap(), 0.0).0.hand.mid[2];
        let fist = right_mid_tip("1hm_idle");
        let open = right_mid_tip("mrh_readyloop");
        // The spell hand's middle finger reaches on past its knuckle (a loose claw); the fist's
        // curls back in behind it.
        assert!(open.x > 1.1, "{open}");
        assert!(fist.x < 0.8, "{fist}");
    }

    #[test]
    #[cfg_attr(not(aw_hand_anim_tracks), ignore = "needs the hand tracks (private-assets.md)")]
    fn a_spell_sits_by_its_hand_on_the_palm_side() {
        let tr = load("dualmagic_idle").unwrap();
        let (r, l) = sample(&tr, 0.0);
        for (side, h) in [("right", r.hand), ("left", l.hand)] {
            // Within a hand's length or so of the knuckles.
            assert!((h.magic - h.mid[0]).length() < 1.6, "{side} {}", h.magic);
            assert!(h.magic.length() > 0.2, "{side} {}", h.magic);
        }
        // The two hands mirror each other: the palm side is the frame's other z.
        assert!(
            r.hand.magic.z * l.hand.magic.z < 0.0,
            "{} {}",
            r.hand.magic,
            l.hand.magic
        );
    }

    #[test]
    #[cfg_attr(not(aw_hand_anim_tracks), ignore = "needs the hand tracks (private-assets.md)")]
    fn every_embedded_track_loads_and_is_sane() {
        for name in NAMES {
            let t = load(name).unwrap_or_else(|| panic!("{name} does not load"));
            assert!(t.duration > 0.0 && t.frames.len() >= 2, "{name}");
            for (_, r, l) in &t.frames {
                for g in [r, l] {
                    assert!((g.blade.length() - 1.0).abs() < 1e-3, "{name}");
                    assert!(g.blade.dot(g.edge).abs() < 0.01, "{name}");
                    // Within arm's reach of the eye (inches).
                    assert!(g.at.length() < 60.0, "{name}: {:?}", g.at);
                }
            }
        }
        assert!(load("no such track").is_none());
    }
}
