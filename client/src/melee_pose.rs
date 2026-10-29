//! Melee in the first person, the two-hand way: the main-hand weapon in the right fist (a
//! two-hander gripped by both hands), held ready, swung in arcs that alternate side to side, a
//! heavier overhead power swing, and a block across the body. The poses are the weapon's grip in
//! the eye's frame (inches, Bevy's camera axes: x right, y up, z back), keyframed here; the arms
//! reach for it by two-bone IK (`reach`), so they follow any weapon and swing.

use bevy::prelude::*;

use crate::hand_shape::HandShape;

/// Where the weapon is: the grip (the middle of the fist), the blade's direction from it, and
/// the way its edge faces (across the blade); and the hand's shape round it (a fist, a hand
/// opened for a spell).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Grip {
    pub at: Vec3,
    pub blade: Vec3,
    pub edge: Vec3,
    pub hand: HandShape,
}

impl Grip {
    /// This grip at depth `z` (eye frame).
    pub fn with_z(&self, z: f32) -> Grip {
        Grip {
            at: Vec3::new(self.at.x, self.at.y, z),
            ..*self
        }
    }
}

/// What the weapon is doing.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum Motion {
    #[default]
    Ready,
    /// A light swing, from the right across to the left, or back.
    Swing { from_right: bool },
    /// The power swing: overhead, straight down.
    Power,
    /// Held across the body.
    Block,
}

/// One keyframe: the grip's place and the blade's direction (made unit and square on use).
struct Key(Vec3, Vec3);

const READY_1H: Key = Key(Vec3::new(7.0, -6.5, -17.0), Vec3::new(0.05, 0.85, -0.55));
const READY_2H: Key = Key(Vec3::new(3.0, -8.0, -17.0), Vec3::new(0.0, 0.85, -0.55));
const BLOCK: Key = Key(Vec3::new(2.0, -1.5, -17.0), Vec3::new(-0.97, 0.2, -0.1));
/// A swing's wind-up and the end of its strike: from the right, from the left, and overhead.
const FROM_RIGHT: (Key, Key) = (
    Key(Vec3::new(14.0, 2.0, -13.0), Vec3::new(0.55, 0.6, 0.55)),
    Key(Vec3::new(-9.0, -9.0, -21.0), Vec3::new(-0.95, -0.15, -0.25)),
);
const FROM_LEFT: (Key, Key) = (
    Key(Vec3::new(-9.0, 1.0, -14.0), Vec3::new(-0.55, 0.6, 0.55)),
    Key(Vec3::new(12.0, -10.0, -21.0), Vec3::new(0.95, -0.2, -0.25)),
);
const OVERHEAD: (Key, Key) = (
    Key(Vec3::new(5.0, 8.0, -10.0), Vec3::new(0.1, 0.5, 0.86)),
    Key(Vec3::new(2.0, -13.0, -23.0), Vec3::new(0.0, -0.55, -0.83)),
);
/// A two-hander is held nearer the middle.
const TWO_HANDED_SHIFT: Vec3 = Vec3::new(-3.0, -1.0, 0.0);
/// A swing's share spent winding up, then striking; the rest recovers.
const WIND_UP: f32 = 0.3;
const STRIKE: f32 = 0.3;

fn ease_in_out(k: f32) -> f32 {
    let k = k.clamp(0.0, 1.0);
    k * k * (3.0 - 2.0 * k)
}

/// A unit `v` square to `blade` (the edge or a pole), or a fallback when they are parallel.
fn square_to(v: Vec3, blade: Vec3) -> Vec3 {
    let e = v - blade * v.dot(blade);
    e.try_normalize()
        .unwrap_or_else(|| blade.any_orthonormal_vector())
}

fn key(k: &Key, shift: Vec3) -> (Vec3, Vec3) {
    (k.0 + shift, k.1.normalize())
}

/// Between two (place, blade) poses, `k` of the way: the place along the line, the blade turned.
fn blend(a: (Vec3, Vec3), b: (Vec3, Vec3), k: f32) -> (Vec3, Vec3) {
    let turn = Quat::from_rotation_arc(a.1, b.1);
    let blade = Quat::IDENTITY.slerp(turn, k) * a.1;
    (a.0.lerp(b.0, k), blade.normalize())
}

/// The grip `k` (0..1) of the way through a motion, for a one- or two-handed weapon.
pub fn grip(motion: Motion, two_handed: bool, k: f32) -> Grip {
    let shift = if two_handed {
        TWO_HANDED_SHIFT
    } else {
        Vec3::ZERO
    };
    let ready = key(if two_handed { &READY_2H } else { &READY_1H }, Vec3::ZERO);
    let (at, blade, moving) = match motion {
        Motion::Ready => (ready.0, ready.1, None),
        Motion::Block => {
            let (at, blade) = key(&BLOCK, shift);
            (at, blade, None)
        }
        Motion::Swing { .. } | Motion::Power => {
            let (wind, end) = match motion {
                Motion::Swing { from_right: true } => &FROM_RIGHT,
                Motion::Swing { from_right: false } => &FROM_LEFT,
                _ => &OVERHEAD,
            };
            let (wind, end) = (key(wind, shift), key(end, shift));
            let k = k.clamp(0.0, 1.0);
            let (pose, moving) = if k < WIND_UP {
                (blend(ready, wind, ease_in_out(k / WIND_UP)), None)
            } else if k < WIND_UP + STRIKE {
                let s = (k - WIND_UP) / STRIKE;
                // The strike: slow out of the wind-up, fast through the middle.
                (blend(wind, end, s * s), Some(end.0 - wind.0))
            } else {
                let r = (k - WIND_UP - STRIKE) / (1.0 - WIND_UP - STRIKE);
                (blend(end, ready, ease_in_out(r)), None)
            };
            (pose.0, pose.1, moving)
        }
    };
    // The edge leads: along the strike while striking, else forward.
    let edge = square_to(moving.unwrap_or(Vec3::NEG_Z), blade);
    Grip {
        at,
        blade,
        edge,
        hand: HandShape::FIST,
    }
}

/// Part way between two grips (a block raised, a swing let go).
pub fn mix(a: &Grip, b: &Grip, k: f32) -> Grip {
    let (at, blade) = blend((a.at, a.blade), (b.at, b.blade), k.clamp(0.0, 1.0));
    let edge = square_to(a.edge.lerp(b.edge, k.clamp(0.0, 1.0)), blade);
    Grip {
        at,
        blade,
        edge,
        hand: a.hand.mix(&b.hand, k),
    }
}

/// The weapon carried low while sprinting: down by the hip, the point tipped forward.
pub fn lowered(g: &Grip) -> Grip {
    let tip = Quat::from_rotation_x(-0.6);
    let blade = (tip * g.blade).normalize();
    Grip {
        at: g.at + Vec3::new(2.0, -6.0, 3.0),
        blade,
        edge: square_to(tip * g.edge, blade),
        hand: g.hand,
    }
}

/// A grip seen in a mirror across the view's middle: the left hand's twin of a right-hand pose.
pub fn mirrored(g: &Grip) -> Grip {
    let m = |v: Vec3| Vec3::new(-v.x, v.y, v.z);
    Grip {
        at: m(g.at),
        blade: m(g.blade),
        edge: m(g.edge),
        hand: g.hand.mirrored(),
    }
}

/// A spell held ready in the right hand (mirrored for the left), the fist raised and the glow
/// above it; a cast `k` (0..1) of the way through thrusts it forward and back.
const SPELL_READY: Key = Key(Vec3::new(7.0, -6.0, -15.0), Vec3::new(0.1, 0.9, -0.42));
const SPELL_THRUST: Key = Key(Vec3::new(3.5, -3.0, -24.0), Vec3::new(0.0, 0.5, -0.87));
/// The share of a cast spent thrusting out; the rest comes back.
const THRUST: f32 = 0.35;
/// Seconds a cast's thrust takes on screen.
pub const CAST_SECS: f32 = 0.45;

pub fn spell_grip(cast: Option<f32>) -> Grip {
    let ready = key(&SPELL_READY, Vec3::ZERO);
    let thrust = key(&SPELL_THRUST, Vec3::ZERO);
    let (at, blade) = match cast.map(|k| k.clamp(0.0, 1.0)) {
        Some(k) if k < THRUST => blend(ready, thrust, ease_in_out(k / THRUST)),
        Some(k) => blend(thrust, ready, ease_in_out((k - THRUST) / (1.0 - THRUST))),
        None => ready,
    };
    Grip {
        at,
        blade,
        edge: square_to(Vec3::NEG_Z, blade),
        hand: HandShape::OPEN,
    }
}

/// Where a spell's glow sits: a little above the fist, along the raised hand.
pub fn glow_at(g: &Grip) -> Vec3 {
    g.at + g.blade * 4.5
}

/// A shield on the left arm: held low and to the side, raised across the face to block (`block`
/// 0..1).
const SHIELD_READY: Key = Key(Vec3::new(-11.0, -14.0, -12.0), Vec3::new(0.0, 1.0, -0.2));
const SHIELD_BLOCK: Key = Key(Vec3::new(-2.5, -4.0, -15.0), Vec3::new(0.15, 1.0, -0.05));

pub fn shield_grip(block: f32) -> Grip {
    let (at, blade) = blend(
        key(&SHIELD_READY, Vec3::ZERO),
        key(&SHIELD_BLOCK, Vec3::ZERO),
        ease_in_out(block),
    );
    Grip {
        at,
        blade,
        edge: square_to(Vec3::NEG_Z, blade),
        hand: HandShape::FIST,
    }
}

/// Seconds a weapon takes to come up into the hands (V).
pub const DRAW_SECS: f32 = 0.4;

/// The weapon `k` (0..1) of the way drawn up into view: from below the view, tipped back, to the
/// grip it is held at.
pub fn drawn(g: &Grip, k: f32) -> Grip {
    let k = ease_in_out(k);
    let stowed = Grip {
        at: g.at + Vec3::new(4.0, -16.0, 6.0),
        ..lowered(g)
    };
    mix(&stowed, g, k)
}

/// Where the off hand goes: on the hilt below the main hand for a two-hander (`hilt` inches down
/// the grip), else down at the side out of sight.
pub fn off_hand(g: &Grip, two_handed: bool, hilt: f32) -> Vec3 {
    if two_handed {
        g.at - g.blade * hilt
    } else {
        Vec3::new(-12.0, -24.0, -6.0)
    }
}

/// Two-bone IK: the elbow's new place for a shoulder `s` reaching its wrist to `target`, with
/// the upper arm `a` and forearm `b` long, the elbow bent toward `pole`. A target out of reach
/// is reached for along the same line.
pub fn elbow_for(s: Vec3, target: Vec3, a: f32, b: f32, pole: Vec3) -> (Vec3, Vec3) {
    let to = target - s;
    let n = to.try_normalize().unwrap_or(Vec3::NEG_Z);
    let d = to.length().clamp((a - b).abs() + 1e-3, a + b - 1e-3);
    let x = (a * a - b * b + d * d) / (2.0 * d);
    let h = (a * a - x * x).max(0.0).sqrt();
    let p = square_to(pole, n);
    (s + n * x + p * h, s + n * d)
}

/// A depth `z` (eye frame, negative ahead) kept at least `clear` ahead of the eye: unchanged
/// beyond `clear + soft`, eased between, so a path that runs into the face slides along short of
/// it without a corner.
pub fn ease_depth(z: f32, clear: f32, soft: f32) -> f32 {
    let edge = -(clear + soft);
    if z > edge {
        -clear - soft * (-(z - edge) / soft).exp()
    } else {
        z
    }
}

/// A wrist target `w` moved, if need be, so the line from the shoulder `s` to it passes at least
/// `clear` from the eye (the eye frame's origin): a fist swung across the face from the far
/// shoulder otherwise drags the arm under the nose, through the near plane. The target turns
/// about the shoulder, away from the eye, just far enough.
pub fn arm_off_the_eye(s: Vec3, w: Vec3, clear: f32) -> Vec3 {
    let sw = w - s;
    let len = sw.length();
    if len < 1e-3 {
        return w;
    }
    let mut w = w;
    // A few passes: the turn moves the nearest point along the line too.
    for _ in 0..6 {
        let sw = w - s;
        let len = sw.length();
        let d = sw / len;
        // The line's nearest point to the eye, within the segment.
        let t = (-s).dot(d).clamp(0.0, len);
        let near = s + d * t;
        let gap = near.length();
        if gap >= clear - 1e-4 || t <= 0.0 {
            break;
        }
        // Away from the eye, square to the line (straight down when the line runs through it).
        let away = (near - d * near.dot(d))
            .try_normalize()
            .unwrap_or_else(|| square_to(Vec3::NEG_Y, d));
        // Push the nearest point out to `clear`, the target with it in proportion along the line.
        w += away * (clear - gap) * (len / t);
    }
    w
}

/// The part of the near plane the camera sees through, in the eye's frame (inches, Bevy axes):
/// an arm passing through it is seen sliced open. `girth` is the arm's half-thickness.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct NearWindow {
    pub near: f32,
    pub half_w: f32,
    pub half_h: f32,
    pub girth: f32,
}

/// The clearance [`clear_pole`] and [`clear_reach`] settle for (inches): a little over nothing,
/// for the sampling of the segments and the frame-to-frame step.
pub const CLEAR_SLACK: f32 = 0.3;

impl NearWindow {
    /// How far a segment stays clear of the window, past the arm's girth (negative: through it).
    pub fn clearance(&self, a: Vec3, b: Vec3) -> f32 {
        let mut best = f32::MAX;
        for i in 0..=64 {
            let p = a.lerp(b, i as f32 / 64.0);
            let q = Vec3::new(
                p.x.clamp(-self.half_w, self.half_w),
                p.y.clamp(-self.half_h, self.half_h),
                -self.near,
            );
            best = best.min(p.distance(q));
        }
        best - self.girth
    }
}

/// The pole to bend an arm toward (shoulder `s`, wrist target `target`, eye frame) so neither
/// segment passes through the near plane's window: `pole` itself when it is clear, else the
/// nearest turn of the elbow about the shoulder-to-wrist line that is, else the clearest. A hand
/// brought up before the face otherwise swings its elbow back beside the head, and the forearm
/// crosses the near plane in view.
pub fn clear_pole(
    s: Vec3,
    target: Vec3,
    a: f32,
    b: f32,
    pole: Vec3,
    window: &NearWindow,
) -> (Vec3, f32) {
    let n = (target - s).try_normalize().unwrap_or(Vec3::NEG_Z);
    let u = square_to(pole, n);
    let v = n.cross(u);
    let clear = |dir: Vec3| {
        let (e, w) = elbow_for(s, target, a, b, dir);
        window.clearance(s, e).min(window.clearance(e, w))
    };
    let mut best = (f32::MIN, u);
    // Out from the pole both ways, a step at a time.
    for step in 0..=18 {
        for sign in [1.0, -1.0] {
            let t = sign * step as f32 * std::f32::consts::PI / 18.0;
            let dir = u * t.cos() + v * t.sin();
            let c = clear(dir);
            if c >= CLEAR_SLACK {
                return (dir, c);
            }
            if c > best.0 {
                best = (c, dir);
            }
            if step == 0 || step == 18 {
                break;
            }
        }
    }
    (best.1, best.0)
}

/// `turn` (a hand's turn onto the grip `g`) turned on, the least, so that `have` (the wrist from
/// the fist, as `turn` put it) points where `local` says the wrist is from the grip: in the
/// grip's frame, along the blade, the edge and the blade across the edge (crate::hand_anim::
/// wrist_at). A zero `local` leaves `turn` as it is.
pub fn wrist_toward(turn: Quat, g: &Grip, have: Vec3, local: Vec3) -> Quat {
    let want = g.blade * local.x + g.edge * local.y + g.blade.cross(g.edge) * local.z;
    match (have.try_normalize(), want.try_normalize()) {
        (Some(h), Some(w)) => (Quat::from_rotation_arc(h, w) * turn).normalize(),
        _ => turn,
    }
}

/// `turn` rolled about the hilt (`axis`, unit) as little as keeps `have` (the wrist from the
/// fist, as `turn` put it) within `most` degrees of `toward` (the shoulder from the fist), both
/// seen down the hilt: a hand may grip a hilt from any side, but not with the wrist on the far
/// side from its own shoulder, the hand folded back over the forearm. The limit fades out as
/// `toward` comes to lie along the hilt, where its side would flip from frame to frame.
pub fn wrist_within(turn: Quat, axis: Vec3, have: Vec3, toward: Vec3, most: f32) -> Quat {
    let across = |v: Vec3| v - axis * v.dot(axis);
    let (h, w) = (across(have), across(toward));
    if h.length() < 1e-3 || w.length() < 1e-3 || toward.length() < 1e-3 {
        return turn;
    }
    let (hn, wn) = (h.normalize(), w.normalize());
    let angle = hn.cross(wn).dot(axis).atan2(hn.dot(wn));
    let over = angle.abs() - most.to_radians();
    if over <= 0.0 {
        return turn;
    }
    let x = (w.length() / toward.length() / 0.4).clamp(0.0, 1.0);
    let fade = x * x * (3.0 - 2.0 * x);
    (Quat::from_axis_angle(axis, angle.signum() * over * fade) * turn).normalize()
}

/// The turn (eye frame, about the wrist) that rolls a hand on its own length (wrist `w` to
/// knuckle `k`) until the forearm (elbow `e` to the wrist) is wrung no more than `most` degrees:
/// the thumb's side (its root `th`) against the elbow's inside (toward the shoulder `s`), as the
/// drawn arm rolls its forearm with the hand. Identity when it is within, or the arm is straight.
pub fn forearm_unwring(s: Vec3, e: Vec3, w: Vec3, k: Vec3, th: Vec3, most: f32) -> Quat {
    let (Some(fore), Some(axis)) = ((w - e).try_normalize(), (k - w).try_normalize()) else {
        return Quat::IDENTITY;
    };
    let across = |v: Vec3| v - fore * v.dot(fore);
    let (inside, thumb) = (across(s - e), across(th - w));
    if inside.length() < 0.5 || thumb.length() < 1e-3 {
        return Quat::IDENTITY;
    }
    let (i, t) = (inside.normalize(), thumb.normalize());
    let angle = t.cross(i).dot(fore).atan2(t.dot(i));
    let over = angle.abs() - most.to_radians();
    if over <= 0.0 {
        return Quat::IDENTITY;
    }
    // About the hand's length: the fist stays where it is. The hand's length is near the
    // forearm's, so the forearm's measure of the turn serves.
    let sign = angle.signum() * axis.dot(fore).signum();
    Quat::from_axis_angle(axis, sign * over)
}

/// Where an arm from the shoulder `s` (upper arm `a`, forearm `b` long) reaches for `target`
/// without passing through the near plane's window, and the pole its elbow bends toward: the
/// target as asked when the arm can bend clear of the window ([`clear_pole`]), else turned about
/// the shoulder away from the eye ([`arm_off_the_eye`]) an inch at a time until it can, else the
/// clearest found. `pole` is the elbow's own way (down and out).
pub fn clear_reach(
    s: Vec3,
    target: Vec3,
    a: f32,
    b: f32,
    pole: Vec3,
    window: &NearWindow,
    line_clear: f32,
) -> (Vec3, Vec3) {
    let mut best = (f32::MIN, target, pole);
    for extra in 0..=16 {
        let t = arm_off_the_eye(s, target, line_clear + extra as f32);
        let (dir, c) = clear_pole(s, t, a, b, pole, window);
        if c >= CLEAR_SLACK {
            return (t, dir);
        }
        if c > best.0 {
            best = (c, t, dir);
        }
    }
    (best.1, best.2)
}

/// The bones under `root` (itself included) in a skeleton whose parents come first.
pub fn subtree(parents: &[Option<usize>], root: usize) -> Vec<bool> {
    let mut under = vec![false; parents.len()];
    if root < under.len() {
        under[root] = true;
    }
    for i in 0..parents.len() {
        if let Some(p) = parents[i]
            && under[p]
        {
            under[i] = true;
        }
    }
    under
}

/// Turn every bone marked in `under` by `turn` about the point `about`.
pub fn turn_bones(world: &mut [Mat4], under: &[bool], turn: Quat, about: Vec3) {
    let m = Mat4::from_translation(about) * Mat4::from_quat(turn) * Mat4::from_translation(-about);
    for (w, &u) in world.iter_mut().zip(under) {
        if u {
            *w = m * *w;
        }
    }
}

/// Reach an arm (shoulder, elbow, wrist bone indices, in the skeleton's frame) for `target`,
/// the elbow toward `pole`: turns the upper arm, then the forearm, carrying the hand along.
pub fn reach(
    world: &mut [Mat4],
    parents: &[Option<usize>],
    arm: [usize; 3],
    target: Vec3,
    pole: Vec3,
) {
    let at = |w: &[Mat4], i: usize| w[i].w_axis.truncate();
    let (s, e, w) = (at(world, arm[0]), at(world, arm[1]), at(world, arm[2]));
    let (a, b) = (s.distance(e), e.distance(w));
    if a < 1e-3 || b < 1e-3 {
        return;
    }
    let (elbow, wrist) = elbow_for(s, target, a, b, pole);
    let upper = Quat::from_rotation_arc((e - s).normalize(), (elbow - s).normalize());
    turn_bones(world, &subtree(parents, arm[0]), upper, s);
    let w_now = at(world, arm[2]);
    let fore = Quat::from_rotation_arc((w_now - elbow).normalize(), (wrist - elbow).normalize());
    turn_bones(world, &subtree(parents, arm[1]), fore, elbow);
}

/// The turn that lays a weapon model along a grip: its tip direction onto the blade, its
/// flat-side axis onto the edge.
pub fn weapon_turn(model_tip: Vec3, model_edge: Vec3, g: &Grip) -> Quat {
    let from = Mat3::from_cols(
        model_tip,
        square_to(model_edge, model_tip),
        model_tip.cross(square_to(model_edge, model_tip)),
    );
    let to = Mat3::from_cols(g.blade, g.edge, g.blade.cross(g.edge));
    Quat::from_mat3(&(to * from.transpose())).normalize()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_swing_starts_and_ends_at_the_ready() {
        for m in [
            Motion::Swing { from_right: true },
            Motion::Swing { from_right: false },
            Motion::Power,
        ] {
            for two in [false, true] {
                let ready = grip(Motion::Ready, two, 0.0);
                let (a, b) = (grip(m, two, 0.0), grip(m, two, 1.0));
                assert!(a.at.distance(ready.at) < 1e-3 && b.at.distance(ready.at) < 1e-3);
                assert!(a.blade.distance(ready.blade) < 1e-3);
            }
        }
    }

    #[test]
    fn swings_cross_the_view_in_turn() {
        let mid = WIND_UP + STRIKE;
        let r = grip(Motion::Swing { from_right: true }, false, mid - 1e-3);
        let l = grip(Motion::Swing { from_right: false }, false, mid - 1e-3);
        assert!(r.at.x < 0.0 && l.at.x > 0.0); // each ends on the other side
        let p = grip(Motion::Power, false, mid - 1e-3);
        assert!(p.blade.y < 0.0); // the power swing comes down
    }

    #[test]
    fn the_blade_stands_up_at_the_ready_and_the_edge_is_square() {
        let g = grip(Motion::Ready, false, 0.0);
        assert!(g.blade.y > 0.8); // point up, not upside down
        assert!(g.edge.dot(g.blade).abs() < 1e-4 && (g.edge.length() - 1.0).abs() < 1e-4);
    }

    #[test]
    fn a_block_comes_up_across_the_body_and_a_sprint_carries_it_low() {
        let (ready, block) = (
            grip(Motion::Ready, false, 0.0),
            grip(Motion::Block, false, 0.0),
        );
        assert_eq!(mix(&ready, &block, 0.0).at, ready.at);
        let up = mix(&ready, &block, 1.0);
        assert!(up.blade.distance(block.blade) < 1e-3 && up.blade.x < -0.9); // across, level
        let low = lowered(&ready);
        assert!(low.at.y < ready.at.y && low.blade.z < ready.blade.z); // down, point forward
        assert!(low.edge.dot(low.blade).abs() < 1e-4);
    }

    #[test]
    fn a_cast_thrusts_out_and_comes_back() {
        let ready = spell_grip(None);
        assert!(spell_grip(Some(0.0)).at.distance(ready.at) < 1e-3);
        let out = spell_grip(Some(THRUST));
        assert!(out.at.z < ready.at.z - 5.0); // forward, away from the eye
        assert!(spell_grip(Some(1.0)).at.distance(ready.at) < 1e-3);
        assert!(glow_at(&ready).y > ready.at.y); // the glow above the fist
    }

    #[test]
    fn the_left_hand_mirrors_and_the_shield_comes_up() {
        let r = grip(Motion::Ready, false, 0.0);
        let l = mirrored(&r);
        assert_eq!((l.at.x, l.at.y), (-r.at.x, r.at.y));
        assert!(l.edge.dot(l.blade).abs() < 1e-4);
        let (low, up) = (shield_grip(0.0), shield_grip(1.0));
        assert!(up.at.y > low.at.y && up.at.x > low.at.x); // up and across the face
    }

    #[test]
    fn a_weapon_draws_up_from_below_into_the_grip() {
        let g = grip(Motion::Ready, false, 0.0);
        assert!(drawn(&g, 0.0).at.y < g.at.y - 10.0); // out of sight below
        assert!(drawn(&g, 1.0).at.distance(g.at) < 1e-3);
        assert!(drawn(&g, 0.5).at.y < g.at.y && drawn(&g, 0.5).at.y > drawn(&g, 0.0).at.y);
    }

    #[test]
    fn the_off_hand_grips_a_two_hander_below_the_main() {
        let g = grip(Motion::Ready, true, 0.0);
        let off = off_hand(&g, true, 4.5);
        assert!((off.distance(g.at) - 4.5).abs() < 1e-3 && off.y < g.at.y);
        assert!(off_hand(&g, false, 4.5).y < -20.0); // a one-hander's off hand is down out of sight
    }

    #[test]
    fn the_arm_reaches_its_target_and_keeps_its_lengths() {
        let (s, a, b) = (Vec3::ZERO, 10.0, 9.0);
        let target = Vec3::new(5.0, -8.0, -10.0);
        let (e, w) = elbow_for(s, target, a, b, Vec3::new(1.0, -1.0, 0.0));
        assert!(w.distance(target) < 1e-3);
        assert!((e.distance(s) - a).abs() < 1e-3 && (w.distance(e) - b).abs() < 1e-3);
        // Out of reach: along the line, straight.
        let (_, far) = elbow_for(s, Vec3::new(0.0, 0.0, -100.0), a, b, Vec3::Y);
        assert!((far.length() - (a + b)).abs() < 1e-2);
    }

    #[test]
    fn a_depth_is_kept_off_the_face_smoothly() {
        let (clear, soft) = (10.0, 4.0);
        let at = |z: f32| ease_depth(z, clear, soft);
        // Far enough: untouched.
        assert_eq!(at(-20.0), -20.0);
        assert_eq!(at(-(clear + soft)), -(clear + soft));
        // Nearer: eased, never past the clear line, and still ordered.
        for z in [-13.0, -10.0, -5.0, 0.0, 6.0] {
            assert!(at(z) <= -clear && at(z) < at(z + 1.0) + 1e-6, "{z}");
        }
        // No jump at the edge.
        let e = -(clear + soft);
        assert!((at(e + 0.01) - (e + 0.01)).abs() < 1e-3);
    }

    #[test]
    fn an_arm_swung_across_the_face_passes_clear_of_the_eye() {
        let s = Vec3::new(7.5, -7.5, 4.0);
        let w = Vec3::new(-8.4, 3.2, -9.9);
        let gap = |w: Vec3| {
            let d = (w - s).normalize();
            let t = (-s).dot(d).clamp(0.0, (w - s).length());
            (s + d * t).length()
        };
        assert!(gap(w) < 4.0);
        let moved = arm_off_the_eye(s, w, 5.0);
        assert!(gap(moved) >= 5.0 - 1e-3, "{}", gap(moved));
        // Only as far as needed: a little, not across the screen.
        assert!(moved.distance(w) < 6.0);
        // A clear line is left alone.
        let low = Vec3::new(4.0, -10.0, -15.0);
        assert_eq!(arm_off_the_eye(s, low, 5.0), low);
    }

    #[test]
    fn the_elbow_turns_out_of_the_near_window() {
        let window = NearWindow {
            near: 3.6,
            half_w: 3.5,
            half_h: 1.5,
            girth: 3.0,
        };
        // The fist brought up before the face from a shoulder beside and behind the eye: bent
        // back and up, the forearm crosses the near plane in view.
        let s = Vec3::new(7.0, -6.5, 3.0);
        let target = Vec3::new(2.0, 1.0, -9.0);
        let (a, b) = (12.0, 11.5);
        let up = Vec3::new(0.0, 1.0, 0.2);
        let (e, w) = elbow_for(s, target, a, b, up);
        assert!(window.clearance(s, e).min(window.clearance(e, w)) < 0.0);
        let (pole, c) = clear_pole(s, target, a, b, up, &window);
        assert!(c >= 0.0);
        let (e, w) = elbow_for(s, target, a, b, pole);
        assert!(window.clearance(s, e) >= 0.0 && window.clearance(e, w) >= 0.0);
        assert!(w.distance(target) < 1e-2);
        // A pole already clear is kept.
        let down = Vec3::new(0.7, -1.0, 0.3);
        let far = Vec3::new(6.0, -6.0, -15.0);
        let (kept, _) = clear_pole(s, far, a, b, down, &window);
        assert!(kept.dot(square_to(down, (far - s).normalize())) > 0.999);
    }

    #[test]
    fn reaching_moves_the_hand_and_what_hangs_from_it() {
        // Shoulder 0, elbow 1, wrist 2, a finger 3 under the wrist, and an unrelated bone 4.
        let parents = [None, Some(0), Some(1), Some(2), None];
        let at = |p: Vec3| Mat4::from_translation(p);
        let mut world = vec![
            at(Vec3::ZERO),
            at(Vec3::new(0.0, -10.0, 0.0)),
            at(Vec3::new(0.0, -19.0, 0.0)),
            at(Vec3::new(0.0, -21.0, 0.0)),
            at(Vec3::new(5.0, 5.0, 5.0)),
        ];
        let target = Vec3::new(6.0, -6.0, -10.0);
        reach(
            &mut world,
            &parents,
            [0, 1, 2],
            target,
            Vec3::new(1.0, -1.0, 0.0),
        );
        let p = |i: usize| world[i].w_axis.truncate();
        assert!(p(2).distance(target) < 1e-2);
        assert!((p(3).distance(p(2)) - 2.0).abs() < 1e-3); // the finger came with the wrist
        assert_eq!(p(4), Vec3::new(5.0, 5.0, 5.0)); // the rest untouched
    }

    #[test]
    fn the_weapon_lies_along_the_grip() {
        let g = grip(Motion::Ready, false, 0.0);
        let turn = weapon_turn(Vec3::Z, Vec3::X, &g);
        assert!((turn * Vec3::Z).distance(g.blade) < 1e-4);
        assert!((turn * Vec3::X).distance(g.edge) < 1e-4);
    }
}
