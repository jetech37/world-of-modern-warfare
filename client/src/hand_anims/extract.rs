//! From a decoded source animation to our first-person tracks: every frame's bones composed into
//! model space, the hands' attachment nodes taken relative to the first-person camera node, and
//! the result turned into our eye frame and inches.
//!
//! The tracks' frames: a character faces +Y with +Z up and +X to its right, in game units of 1/64
//! yard (0.5625 in, the Creation Kit's "Unit"). The first-person camera sits on
//! `Camera1st [Cam1]` (a child of the root, 120.5 units up at rest; the animations move it a
//! little, a camera kick). Our eye frame is Bevy's camera's: x right, y up, z back, in inches. So
//! a source camera-relative vector (x, y, z) is ours (x, z, -y), times 0.5625 for a place.
//!
//! The grips: `right` is the `Weapon` node, where a right-hand weapon's model is attached (the
//! middle of the right fist); `left` is the `Shield` node, the same for the left hand (shields
//! and left-hand weapons, the left fist). `blade` is the node's +Y, along which the tracks' weapon
//! models point their blades; `edge` is the node's -X, the edge that leads in the tracks'
//! right-to-left swing (`1hm_attackright`; the other edge, +X, leads the backhand). The node's +Z
//! is the blade's flat. The two nodes are mirror images (the dual-wield idle holds them in
//! mirror), so the same axes serve both hands.

use super::hkx::{Qs, Skeleton};
use super::spline::Decoded;
use super::{JsonCamera, JsonEvent, JsonFrame, JsonGrip, JsonHand, JsonTrack};
use crate::hand_shape::HandShape;
use bevy::math::Vec3;

/// Inches per source unit.
pub const INCHES_PER_UNIT: f32 = 0.5625;
pub const CAMERA: &str = "Camera1st [Cam1]";
pub const RIGHT: &str = "Weapon";
pub const LEFT: &str = "Shield";
pub const RIGHT_WRIST: &str = "NPC R Hand [RHnd]";
pub const LEFT_WRIST: &str = "NPC L Hand [LHnd]";
/// Where a spell held in a hand sits: `{side}` R or L.
pub fn magic_node(side: char) -> String {
    format!("NPC {side} MagicNode [{side}Mag]")
}

/// A hand's middle finger (`Finger2x`) and thumb (`Finger0x`), root first: `{side}` R or L.
pub fn finger_bones(side: char) -> [String; 6] {
    ["20", "21", "22", "00", "01", "02"].map(|n| format!("NPC {side} Finger{n} [{side}F{n}]"))
}

/// A source direction (x right, y forward, z up) as ours (x right, y up, z back).
pub fn to_eye(v: Vec3) -> Vec3 {
    Vec3::new(v.x, v.z, -v.y)
}

/// A source place, in units, as ours, in inches.
pub fn place_to_eye(v: Vec3) -> Vec3 {
    to_eye(v) * INCHES_PER_UNIT
}

/// `of` in the frame of `frame` (both model space).
pub fn relative(frame: &Qs, of: &Qs) -> Qs {
    let inv = frame.r.inverse();
    Qs {
        t: inv * (of.t - frame.t),
        r: (inv * of.r).normalize(),
        s: of.s,
    }
}

fn round(v: Vec3, places: i32) -> [f32; 3] {
    let k = 10f32.powi(places);
    [v.x, v.y, v.z].map(|c| (c * k).round() / k)
}

/// A camera-relative node as a grip in our frame.
pub fn grip_of(node: &Qs) -> JsonGrip {
    JsonGrip {
        at: round(place_to_eye(node.t), 2),
        blade: round(to_eye(node.r * Vec3::Y).normalize(), 4),
        edge: round(to_eye(node.r * Vec3::NEG_X).normalize(), 4),
    }
}

/// The bones the export needs.
pub struct Bones {
    camera: usize,
    right: usize,
    left: usize,
    right_wrist: usize,
    left_wrist: usize,
    /// Each hand's middle finger and thumb joints (`finger_bones`), when the skeleton has them.
    right_fingers: Option<[usize; 6]>,
    left_fingers: Option<[usize; 6]>,
    right_magic: Option<usize>,
    left_magic: Option<usize>,
}

impl Bones {
    pub fn find(skeleton: &Skeleton) -> Result<Bones, String> {
        let b = |n: &str| skeleton.bone(n).ok_or(format!("skeleton has no {n}"));
        let fingers = |side: char| {
            let names = finger_bones(side);
            let found: Vec<usize> = names.iter().filter_map(|n| skeleton.bone(n)).collect();
            <[usize; 6]>::try_from(found).ok()
        };
        Ok(Bones {
            camera: b(CAMERA)?,
            right: b(RIGHT)?,
            left: b(LEFT)?,
            right_wrist: b(RIGHT_WRIST)?,
            left_wrist: b(LEFT_WRIST)?,
            right_fingers: fingers('R'),
            left_fingers: fingers('L'),
            right_magic: skeleton.bone(&magic_node('R')),
            left_magic: skeleton.bone(&magic_node('L')),
        })
    }
}

/// The skeleton's local pose at `time`: the reference pose, with the animated bones replaced.
pub fn local_pose(skeleton: &Skeleton, anim: &Decoded, time: f32) -> Vec<Qs> {
    let mut local = skeleton.reference.clone();
    for (track, qs) in anim.sample(time).into_iter().enumerate() {
        if let Some(&bone) = anim.anim.track_to_bone.get(track)
            && bone >= 0
            && (bone as usize) < local.len()
        {
            local[bone as usize] = qs;
        }
    }
    local
}

/// One frame in our terms.
pub fn frame(
    skeleton: &Skeleton,
    bones: &Bones,
    rest_camera: &Qs,
    local: &[Qs],
    t: f32,
) -> JsonFrame {
    let model = skeleton.model_space(local);
    let cam = model[bones.camera];
    let rel = |i: usize| relative(&cam, &model[i]);
    let kick = relative(rest_camera, &cam);
    let hand = |wrist: usize, fingers: Option<[usize; 6]>, magic: Option<usize>| {
        let j = fingers?.map(|i| rel(i).t);
        let at = magic.map(|m| rel(m).t);
        let s = HandShape::of(
            rel(wrist).t,
            [j[0], j[1], j[2]],
            [j[3], j[4], j[5]],
            at.unwrap_or(j[0]),
        );
        Some(JsonHand {
            mid: s.mid.map(|p| round(p, 3)),
            thumb: s.thumb.map(|p| round(p, 3)),
            magic: at.map(|_| round(s.magic, 3)),
        })
    };
    JsonFrame {
        t: (t * 10000.0).round() / 10000.0,
        right: grip_of(&rel(bones.right)),
        left: grip_of(&rel(bones.left)),
        right_wrist: round(place_to_eye(rel(bones.right_wrist).t), 2),
        left_wrist: round(place_to_eye(rel(bones.left_wrist).t), 2),
        camera: JsonCamera {
            at: round(place_to_eye(kick.t), 2),
            forward: round(to_eye(kick.r * Vec3::Y), 4),
            up: round(to_eye(kick.r * Vec3::Z), 4),
        },
        right_hand: hand(bones.right_wrist, bones.right_fingers, bones.right_magic),
        left_hand: hand(bones.left_wrist, bones.left_fingers, bones.left_magic),
    }
}

/// A whole animation, sampled at `fps`.
pub fn export(
    skeleton: &Skeleton,
    anim: &Decoded,
    name: &str,
    source: &str,
    fps: f32,
) -> Result<JsonTrack, String> {
    let bones = Bones::find(skeleton)?;
    let rest = skeleton.model_space(&skeleton.reference)[bones.camera];
    let duration = anim.anim.duration;
    let n = (duration * fps).round().max(0.0) as usize;
    let frames = (0..=n)
        .map(|k| {
            let t = (k as f32 / fps).min(duration);
            frame(skeleton, &bones, &rest, &local_pose(skeleton, anim, t), t)
        })
        .collect();
    let events = anim
        .anim
        .annotations
        .iter()
        .map(|(t, name)| JsonEvent {
            t: *t,
            name: name.clone(),
        })
        .collect();
    Ok(JsonTrack {
        name: name.to_string(),
        source: source.to_string(),
        fps,
        duration,
        additive: anim.anim.blend_hint != 0,
        frames,
        events,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use bevy::math::Quat;

    #[test]
    fn source_forward_is_our_minus_z_and_up_our_y() {
        assert_eq!(to_eye(Vec3::Y), Vec3::NEG_Z);
        assert_eq!(to_eye(Vec3::Z), Vec3::Y);
        assert_eq!(to_eye(Vec3::X), Vec3::X);
        // 64 units are a yard.
        assert!((place_to_eye(Vec3::new(0.0, 64.0, 0.0)).z + 36.0).abs() < 1e-4);
    }

    #[test]
    fn a_node_ahead_and_turned_is_a_grip_ahead() {
        let cam = Qs {
            t: Vec3::new(0.0, 0.0, 120.0),
            ..Qs::IDENTITY
        };
        // 32 units ahead, 16 below; the blade (+Y) turned to point up.
        let node = Qs {
            t: Vec3::new(8.0, 32.0, 104.0),
            r: Quat::from_rotation_x(std::f32::consts::FRAC_PI_2),
            s: Vec3::ONE,
        };
        let g = grip_of(&relative(&cam, &node));
        assert_eq!(g.at, [4.5, -9.0, -18.0]);
        assert_eq!(g.blade, [0.0, 1.0, 0.0]);
        assert_eq!(g.edge, [-1.0, 0.0, 0.0]);
    }
}
