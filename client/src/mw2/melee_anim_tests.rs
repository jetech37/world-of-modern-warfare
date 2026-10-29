//! The melee hands' animation, measured frame by frame as the view model plays it: every
//! loadout (a dagger, a sword, a mace, sword and shield, two swords, a sword and a spell, fists,
//! two-handed swords, axes, hammers and a staff, a spell in each hand) drawn, swung as fast as the
//! weapon allows, swung while sprinting, blocking and casting, at 60 frames a second, through
//! [`hand_anim::compose`] and the arms' reach ([`hold_melee`]) on MW2's rifle arms.
//!
//! Each check names what a player would see go wrong: a swing that never crosses the middle of
//! the screen, one that wavers back and forth, a hand or weapon that jumps between frames, a blade
//! sliced by the near plane, an arm bent the wrong way or a wrist wrung past what a wrist does, the
//! weapon held out of sight, and swings too slow to feel like a shooter's.

use super::*;
use crate::hand_anim::{HandsNow, HandAnimClips};
use crate::hands::Hand;
use crate::mw2::held::held_length;
use crate::mw2::viewmodel::{BLOCK_SECS, SPRINT_BLEND_SECS};
use bevy::math::Vec2;

/// The frame step the view model is measured at.
const DT: f32 = 1.0 / 60.0;

/// What the hands hold.
#[derive(Clone, Copy, Debug)]
pub(super) struct Loadout {
    pub name: &'static str,
    subclass: u8,
    two: bool,
    left: Hand,
    off_subclass: u8,
    right_spell: bool,
    /// The weapon's recovery between swings (seconds): the server's `swingMs`
    /// (`server/src/fusion/src/Melee.cpp`).
    gate: f32,
}

const SPELL: Hand = Hand::Spell {
    action: 0,
    spell_id: 0,
};

pub(super) const LOADOUTS: &[Loadout] = &[
    Loadout {
        name: "dagger",
        subclass: 15,
        two: false,
        left: Hand::Empty,
        off_subclass: 255,
        right_spell: false,
        gate: 0.45,
    },
    Loadout {
        name: "sword",
        subclass: 7,
        two: false,
        left: Hand::Empty,
        off_subclass: 255,
        right_spell: false,
        gate: 0.6,
    },
    Loadout {
        name: "mace",
        subclass: 4,
        two: false,
        left: Hand::Empty,
        off_subclass: 255,
        right_spell: false,
        gate: 0.8,
    },
    Loadout {
        name: "sword+shield",
        subclass: 7,
        two: false,
        left: Hand::Shield,
        off_subclass: 255,
        right_spell: false,
        gate: 0.6,
    },
    Loadout {
        name: "dual swords",
        subclass: 7,
        two: false,
        left: Hand::Weapon,
        off_subclass: 7,
        right_spell: false,
        gate: 0.6,
    },
    Loadout {
        name: "sword+spell",
        subclass: 7,
        two: false,
        left: SPELL,
        off_subclass: 255,
        right_spell: false,
        gate: 0.6,
    },
    Loadout {
        name: "fists",
        subclass: 255,
        two: false,
        left: Hand::Empty,
        off_subclass: 255,
        right_spell: false,
        gate: 0.5,
    },
    Loadout {
        name: "fist weapon",
        subclass: 13,
        two: false,
        left: Hand::Empty,
        off_subclass: 255,
        right_spell: false,
        gate: 0.4,
    },
    Loadout {
        name: "fists+spell",
        subclass: 255,
        two: false,
        left: SPELL,
        off_subclass: 255,
        right_spell: false,
        gate: 0.5,
    },
    Loadout {
        name: "2h sword",
        subclass: 8,
        two: true,
        left: Hand::Weapon,
        off_subclass: 255,
        right_spell: false,
        gate: 1.0,
    },
    Loadout {
        name: "2h axe",
        subclass: 1,
        two: true,
        left: Hand::Weapon,
        off_subclass: 255,
        right_spell: false,
        gate: 1.1,
    },
    Loadout {
        name: "2h mace",
        subclass: 5,
        two: true,
        left: Hand::Weapon,
        off_subclass: 255,
        right_spell: false,
        gate: 1.25,
    },
    Loadout {
        name: "staff",
        subclass: 10,
        two: true,
        left: Hand::Weapon,
        off_subclass: 255,
        right_spell: false,
        gate: 0.9,
    },
    Loadout {
        name: "spell+fist",
        subclass: 255,
        two: false,
        left: Hand::Empty,
        off_subclass: 255,
        right_spell: true,
        gate: 0.5,
    },
    Loadout {
        name: "two spells",
        subclass: 255,
        two: false,
        left: SPELL,
        off_subclass: 255,
        right_spell: true,
        gate: 0.5,
    },
];

impl Loadout {
    fn fists(&self) -> bool {
        self.subclass == 255
    }
    fn dual(&self) -> bool {
        self.left == Hand::Weapon && !self.two
    }
    /// The swinging weapon's length (inches): what the right hand holds, or the off hand's.
    fn length(&self, left: bool) -> f32 {
        match (left, self.fists()) {
            (_, true) => 0.0,
            (true, false) => held_length(self.off_subclass, false),
            (false, false) => held_length(self.subclass, self.two),
        }
    }
    fn now(&self, t: f32) -> HandsNow {
        HandsNow {
            two: self.two,
            fists: self.fists(),
            axe: crate::hand_anim::is_axe_or_hammer(self.subclass),
            dual: self.dual(),
            right_spell: self.right_spell,
            left: self.left,
            t,
            drawn: 1.0,
            ..Default::default()
        }
    }
    /// Swings the right button takes (the left hand's second weapon); the left otherwise.
    fn swings_left(&self) -> bool {
        self.dual()
    }
}

/// What happens when in a scene.
#[derive(Clone, Copy, Debug, PartialEq)]
pub(super) enum Cue {
    /// The trigger held: a swing each time the weapon's recovery allows, the hands in turn for a
    /// dual wielder.
    Spam(f32, f32),
    /// One swing of the right hand.
    Swing(f32),
    Sprint(f32, f32),
    Block(f32, f32),
    Cast(f32, usize),
    /// Raised with V at this time (drawn from nothing).
    Draw(f32),
    /// What the hands hold changes to `LOADOUTS[i]` (a weapon swapped, a spell readied).
    Swap(f32, usize),
}

/// One frame as drawn: where the arms put the hands, and the arms' joints (eye frame).
#[derive(Clone, Debug)]
pub(super) struct Frame {
    pub t: f32,
    pub right: Grip,
    pub left: Option<Grip>,
    /// Per side: shoulder, elbow, wrist, the middle knuckle, the thumb's root.
    pub joints: [[Vec3; 5]; 2],
    /// The swing playing: which hand (left), from the right, how far through, its seconds.
    pub swing: Option<(bool, bool, f32, f32)>,
    /// Nothing under way: drawn, no swing, cast, sprint or block moving the hands.
    pub at_rest: bool,
    /// The shield fully up.
    pub blocking: bool,
    /// What the hands held this frame.
    pub lo: Loadout,
    /// The posed skeleton (crate::mw2::wow_arms::cut_tests::arm_faults).
    pub world: Vec<Mat4>,
}

/// The arms at the rifle's idle, the base the view model reaches from.
fn rig() -> (Vec<Mat4>, Vec<Option<usize>>) {
    let (_, r, l) = &IDLES[0];
    idle_world(r, l)
}

/// Play a scene for `secs` as the view model would, frame by frame.
pub(super) fn play(lo: &Loadout, cues: &[Cue], secs: f32, clips: &mut HandAnimClips) -> Vec<Frame> {
    let (idle, parents) = rig();
    let mut lo = *lo;
    let mut out = Vec::new();
    let mut drawn = if cues.iter().any(|c| matches!(c, Cue::Draw(_))) {
        0.0
    } else {
        1.0
    };
    let (mut sprint, mut block) = (0.0f32, 0.0f32);
    let mut ready_at = 0.0f32;
    let (mut from_right, mut left_from_right, mut next_left) = (true, false, false);
    let mut swing: Option<(bool, f32, f32)> = None;
    let mut left_swing: Option<(bool, f32, f32)> = None;
    let mut cast: [Option<f32>; 2] = [None; 2];
    let mut settle = crate::hand_anim::Settle::default();
    let frames = (secs / DT).round() as usize;
    for i in 0..=frames {
        let t = i as f32 * DT;
        let mut now = lo.now(t);
        let drawing = cues.iter().any(|c| matches!(c, Cue::Draw(at) if t >= *at));
        // The keys this frame.
        let mut press = None;
        for c in cues {
            match *c {
                Cue::Spam(a, b) if t >= a && t < b => {
                    press = Some(lo.swings_left() && next_left);
                }
                Cue::Swing(a) if (t - a).abs() < DT * 0.5 => press = Some(false),
                Cue::Cast(a, side) if (t - a).abs() < DT * 0.5 => cast[side] = Some(t),
                Cue::Swap(a, i) if (t - a).abs() < DT * 0.5 => {
                    lo = LOADOUTS[i];
                    now = lo.now(t);
                }
                _ => {}
            }
        }
        let held = |c: &Cue| match *c {
            Cue::Sprint(a, b) => (t >= a && t < b).then_some(0),
            Cue::Block(a, b) => (t >= a && t < b).then_some(1),
            _ => None,
        };
        let sprint_goal = if cues.iter().any(|c| held(c) == Some(0)) {
            1.0
        } else {
            0.0
        };
        let block_goal = if lo.left == Hand::Shield && cues.iter().any(|c| held(c) == Some(1)) {
            1.0
        } else {
            0.0
        };
        // The swing gate (crate::weapon::KnifeGate) and the view model's start of a swing.
        if let Some(left) = press
            && t >= ready_at
            && drawn >= 1.0
        {
            ready_at = t + lo.gate;
            let turn = if left {
                &mut left_from_right
            } else {
                &mut from_right
            };
            let fr = *turn;
            *turn = !fr;
            let secs = crate::hand_anim::swing_secs(&now, left, lo.gate);
            if left {
                left_swing = Some((fr, t, secs));
            } else {
                swing = Some((fr, t, secs));
            }
            if lo.swings_left() {
                next_left = !next_left;
            }
        }
        // The view model's blends.
        block += (block_goal - block).clamp(-DT / BLOCK_SECS, DT / BLOCK_SECS);
        sprint += (sprint_goal - sprint).clamp(-DT / SPRINT_BLEND_SECS, DT / SPRINT_BLEND_SECS);
        now.sprint = sprint;
        now.block = block;
        now.drawn = drawn;
        if drawing {
            let d = crate::hand_anim::draw_secs(&now, clips);
            drawn = (drawn + DT / d).min(1.0);
        } else if drawn < 1.0 {
            // Not raised yet.
            continue;
        }
        now.drawn = drawn;
        now.swing = swing
            .filter(|&(_, at, s)| t - at < s)
            .map(|(fr, at, s)| (fr, (t - at) / s, s));
        now.left_swing = left_swing
            .filter(|&(_, at, s)| t - at < s)
            .map(|(fr, at, s)| (fr, (t - at) / s, s));
        now.cast = cast.map(|c| c.map(|at| (t - at) / melee_pose::CAST_SECS));
        let asked = settle.apply(t, crate::hand_anim::compose(&now, clips));
        let mut world = idle.clone();
        let (mut r, mut l) = asked;
        crate::mw2::viewmodel::hold_melee(
            &mut world,
            &parents,
            ARMS,
            Mat4::IDENTITY,
            &mut r,
            l.as_mut(),
            &crate::hand_anim::hold(&now),
        );
        let joint = |side: usize| {
            let j = ARMS[side].expect("both arms");
            [0, 1, 2, 3, 6].map(|k| world[j[k]].w_axis.truncate())
        };
        let playing = now
            .swing
            .map(|(fr, k, s)| (false, fr, k, s))
            .or(now.left_swing.map(|(fr, k, s)| (true, fr, k, s)));
        let casting = now.cast.iter().flatten().any(|k| *k < 1.0);
        // Within a swap's ease (crate::hand_anim::Settle) the hands are between two holds.
        let settling = cues.iter().any(|c| {
            matches!(*c, Cue::Swap(a, _) if t >= a && t < a + crate::hand_anim::SETTLE_SECS + DT)
        });
        out.push(Frame {
            t,
            right: r,
            left: l,
            joints: [joint(0), joint(1)],
            swing: playing,
            at_rest: drawn >= 1.0
                && playing.is_none()
                && !casting
                && !settling
                && sprint == 0.0
                && block == 0.0,
            blocking: block >= 1.0 && playing.is_none(),
            lo,
            world,
        });
    }
    out
}

/// The scenes every loadout plays.
pub(super) fn scenes(lo: &Loadout) -> Vec<(&'static str, Vec<Cue>, f32)> {
    let mut s = vec![("draw", vec![Cue::Draw(0.0)], 1.6), ("rest", vec![], 4.0)];
    // A spell in the right hand casts; it does not swing.
    if !lo.right_spell {
        s.extend([
            ("swings", vec![Cue::Spam(0.2, 3.2)], 4.2),
            ("one swing", vec![Cue::Swing(0.2)], 2.2),
            (
                "sprint swing",
                vec![Cue::Sprint(0.2, 1.6), Cue::Swing(0.7)],
                2.6,
            ),
        ]);
    } else {
        s.push(("sprint", vec![Cue::Sprint(0.2, 1.6)], 2.4));
    }
    if lo.left == Hand::Shield {
        s.push(("block", vec![Cue::Block(0.2, 1.2), Cue::Swing(0.6)], 2.0));
    }
    if lo.right_spell || matches!(lo.left, Hand::Spell { .. }) {
        s.push(("casts", vec![Cue::Cast(0.2, 0), Cue::Cast(1.0, 1)], 2.0));
    }
    s
}

/// Every loadout's every scene.
pub(super) fn all_scenes() -> Vec<(String, Loadout, Vec<Frame>)> {
    let mut clips = HandAnimClips::default();
    let mut out = Vec::new();
    for lo in LOADOUTS {
        for (name, cues, secs) in scenes(lo) {
            let frames = play(lo, &cues, secs, &mut clips);
            out.push((format!("{} {name}", lo.name), *lo, frames));
        }
    }
    out
}

/// A point `along` inches up the weapon from the grip.
fn up_blade(g: &Grip, along: f32) -> Vec3 {
    g.at + g.blade * along
}

/// Where a point is on the screen: its tangents right and up (the view's middle 0).
fn screen(p: Vec3) -> Option<Vec2> {
    (p.z < -0.5).then(|| Vec2::new(p.x / -p.z, p.y / -p.z))
}

/// The weapon's business end: two thirds up a weapon (the blade, the head), the fist itself.
fn business(lo: &Loadout, left: bool, g: &Grip) -> Vec3 {
    up_blade(g, lo.length(left) * 0.67)
}

/// A frame's swinging grip: the left hand's for its swing or a fist's left punch.
fn swinging<'a>(lo: &Loadout, f: &'a Frame) -> Option<(bool, &'a Grip)> {
    let (left, from_right, _, _) = f.swing?;
    // The tracks' fists punch with the left on every other swing (`h2h_attackleft`).
    let left_fist = lo.fists() && lo.left == Hand::Empty && !left && !from_right;
    if left || left_fist {
        Some((true, f.left.as_ref()?))
    } else {
        Some((false, &f.right))
    }
}

/// The swings in a scene: per swing, its frames' indices.
fn each_swing(frames: &[Frame]) -> Vec<Vec<usize>> {
    let mut out: Vec<Vec<usize>> = Vec::new();
    let mut last: Option<(bool, f32)> = None;
    for (i, f) in frames.iter().enumerate() {
        match f.swing {
            Some((left, _, k, _)) => {
                let fresh = last.is_none_or(|(l, lk)| l != left || k < lk);
                if fresh {
                    out.push(Vec::new());
                }
                out.last_mut().unwrap().push(i);
                last = Some((left, k));
            }
            None => last = None,
        }
    }
    out
}

/// The view's horizontal half-field (tangent) on a 16:9 screen, unwidened: where a player sees.
fn tan_h_16_9() -> f32 {
    22.5f32.to_radians().tan() * 16.0 / 9.0
}
fn tan_v() -> f32 {
    22.5f32.to_radians().tan()
}

// ---- the checks ------------------------------------------------------------------------------

/// A swing's on-screen measures: whether its business end crossed the screen's middle and how
/// high, the frames it wavered against its way, and when it crossed (seconds after the press).
#[derive(Debug, Default)]
pub(super) struct SwingRead {
    pub crossed: bool,
    pub cross_y: f32,
    pub cross_after: f32,
    pub reversals: usize,
    pub secs: f32,
}

pub(super) fn read_swing(lo: &Loadout, frames: &[Frame], idx: &[usize]) -> SwingRead {
    let first = &frames[idx[0]];
    let (_, from_right, _, secs) = first.swing.expect("a swing");
    // Fists with the left hand busy punch with the right every time, from the right.
    let from_right = from_right || (lo.fists() && lo.left != Hand::Empty);
    // From the right, the sweep runs right to left on the screen, whichever hand swings.
    let way = if from_right { -1.0 } else { 1.0 };
    let xs: Vec<(f32, Vec2)> = idx
        .iter()
        .filter_map(|&i| {
            let f = &frames[i];
            let (l, g) = swinging(lo, f)?;
            Some((f.t, screen(business(lo, l, g))?))
        })
        .collect();
    let mut r = SwingRead {
        secs,
        ..Default::default()
    };
    // The sweep: from the start side's farthest to the far side's.
    let start = xs
        .iter()
        .enumerate()
        .filter(|(_, (_, p))| p.x * way < 0.0)
        .min_by(|a, b| (a.1.1.x * way).total_cmp(&(b.1.1.x * way)))
        .map(|(i, _)| i);
    let Some(start) = start else {
        return r;
    };
    let end = xs
        .iter()
        .enumerate()
        .skip(start)
        .max_by(|a, b| (a.1.1.x * way).total_cmp(&(b.1.1.x * way)))
        .map(|(i, _)| i)
        .unwrap_or(start);
    for w in xs[start..=end].windows(2) {
        let (a, b) = (w[0].1, w[1].1);
        if (b.x - a.x) * way < -0.01 {
            r.reversals += 1;
        }
        if a.x * way <= 0.0 && b.x * way > 0.0 && !r.crossed {
            r.crossed = true;
            let s = -a.x / (b.x - a.x);
            r.cross_y = a.y + (b.y - a.y) * s;
            r.cross_after = w[0].0 - first.t;
        }
    }
    r
}

/// A frame-to-frame jump: the grip's step (inches), the change of step (inches a frame, a
/// frame), the blade's turn (degrees).
pub(super) fn steps(a: &Grip, b: &Grip) -> (f32, f32) {
    (
        a.at.distance(b.at),
        a.blade.angle_between(b.blade).to_degrees(),
    )
}

/// The largest per-frame jumps in a scene: per hand, (step, jerk, turn) and where.
pub(super) fn worst_jumps(frames: &[Frame]) -> [(f32, f32, f32, f32); 2] {
    let mut worst = [(0.0f32, 0.0f32, 0.0f32, 0.0f32); 2];
    for side in 0..2 {
        let g = |f: &Frame| if side == 0 { Some(f.right) } else { f.left };
        let mut last_step: Option<f32> = None;
        for w in frames.windows(2) {
            let (Some(a), Some(b)) = (g(&w[0]), g(&w[1])) else {
                last_step = None;
                continue;
            };
            let (step, turn) = steps(&a, &b);
            let jerk = last_step.map_or(0.0, |l| (step - l).abs());
            let e = &mut worst[side];
            if step > e.0 {
                *e = (step, e.1, e.2, w[1].t);
            }
            e.1 = e.1.max(jerk);
            e.2 = e.2.max(turn);
            last_step = Some(step);
        }
    }
    worst
}

/// The elbow's bend (degrees, 180 straight) and whether it bends the wrong way (up and in past
/// the shoulder-to-wrist line, the way no elbow folds); the wrist's bend (the hand off the
/// forearm's line, degrees) and the forearm's roll: the thumb's side against the elbow's bend
/// (0 a handshake's thumb up toward the upper arm, 90 the palm turned fully up or down; past
/// that the forearm is wrung further than it turns). The drawn arm rolls the whole forearm with
/// the hand (crate::mw2::wow_arms::arm_bones), so the roll is where a wrung wrist shows.
pub(super) fn anatomy(f: &Frame, side: usize) -> (f32, bool, f32, f32) {
    let [s, e, w, knuckle, thumb] = f.joints[side];
    let bend = (s - e).angle_between(w - e).to_degrees();
    let n = (w - s).normalize_or(Vec3::NEG_Z);
    let off = (e - s) - n * (e - s).dot(n);
    let outward = if side == 0 { Vec3::X } else { Vec3::NEG_X };
    let wrong = off.length() > 1.0 && {
        let o = off.normalize();
        o.y > 0.5 && o.dot(outward) < 0.0
    };
    let fore = (w - e).normalize_or(Vec3::NEG_Z);
    let wrist = fore.angle_between(knuckle - w).to_degrees();
    let across = |v: Vec3| v - fore * v.dot(fore);
    let (inside, thumb_side) = (across(s - e), across(thumb - w));
    let roll = if bend < 165.0 && inside.length() > 1e-3 && thumb_side.length() > 1e-3 {
        inside.angle_between(thumb_side).to_degrees()
    } else {
        0.0
    };
    (bend, wrong, wrist, roll)
}

/// How far the fist (the middle of the hand, where it holds the weapon) is from the grip the
/// weapon is drawn at (inches): an arm too short for its reach leaves the weapon floating.
pub(super) fn shortfall(f: &Frame, side: usize) -> f32 {
    let g = if side == 0 { Some(f.right) } else { f.left };
    let [_, _, w, knuckle, _] = f.joints[side];
    g.map_or(0.0, |g| g.at.distance(w.lerp(knuckle, 0.9)))
}

/// Whether the blade (grip to tip, an inch thick) passes through the near plane in view.
pub(super) fn blade_sliced(lo: &Loadout, left: bool, g: &Grip) -> bool {
    let len = lo.length(left);
    len > 0.0 && through_near_window(g.at, up_blade(g, len), 1.0)
}

// ---- reports and tests ---------------------------------------------------------------------

/// `cargo test --release --lib melee_report -- --ignored --nocapture`: every scene's measures.
#[test]
#[ignore]
fn melee_report() {
    for (name, lo, frames) in all_scenes() {
        let j = worst_jumps(&frames);
        println!(
            "{name}: right step {:.2} jerk {:.2} turn {:.1} at {:.2} | left step {:.2} jerk {:.2} turn {:.1} at {:.2}",
            j[0].0, j[0].1, j[0].2, j[0].3, j[1].0, j[1].1, j[1].2, j[1].3
        );
        for idx in each_swing(&frames) {
            let r = read_swing(&lo, &frames, &idx);
            let f = &frames[idx[0]];
            println!(
                "    swing {:?} at {:.2}: {:.2}s crossed {} y {:.2} after {:.2}s reversals {}",
                f.swing.map(|s| (s.0, s.1)),
                f.t,
                r.secs,
                r.crossed,
                r.cross_y,
                r.cross_after,
                r.reversals
            );
        }
        let (mut min_bend, mut wrong, mut wrist, mut twist, mut sliced) =
            (180.0f32, 0, 0.0f32, 0.0f32, 0);
        let (mut max_bend, mut short) = (0.0f32, [0.0f32; 2]);
        let mut worst_at = [(0.0f32, 0.0f32, None, 0.0f32, 0.0f32, None); 2];
        for f in &frames {
            for side in 0..2 {
                if side == 1 && f.left.is_none() {
                    continue;
                }
                let (b, w, s, tw) = anatomy(f, side);
                let e = &mut worst_at[side];
                if s > e.0 {
                    (e.0, e.1, e.2) = (s, f.t, f.swing.map(|x| x.2));
                }
                if tw > e.3 {
                    (e.3, e.4, e.5) = (tw, f.t, f.swing.map(|x| x.2));
                }
                min_bend = min_bend.min(b);
                max_bend = max_bend.max(b);
                short[side] = short[side].max(shortfall(f, side));
                wrong += usize::from(w);
                wrist = wrist.max(s);
                twist = twist.max(tw);
            }
            if blade_sliced(&lo, false, &f.right) {
                sliced += 1;
            }
        }
        println!(
            "    short {:.1} {:.1} elbow max {max_bend:.0} min {min_bend:.0} wrong-way frames {wrong} wrist bend max {wrist:.0} roll max {twist:.0} sliced frames {sliced}",
            short[0], short[1]
        );
        for (side, e) in worst_at.iter().enumerate() {
            println!(
                "      side {side}: wrist {:.0} at {:.2} k {:?}, roll {:.0} at {:.2} k {:?}",
                e.0, e.1, e.2, e.3, e.4, e.5
            );
        }
    }
}

/// Every scene once (they take a moment to play), and a few swaps between loadouts.
fn scenes_once() -> &'static [(String, Loadout, Vec<Frame>)] {
    static ALL: std::sync::OnceLock<Vec<(String, Loadout, Vec<Frame>)>> =
        std::sync::OnceLock::new();
    ALL.get_or_init(|| {
        let mut all = all_scenes();
        let mut clips = HandAnimClips::default();
        let at = |name: &str| {
            LOADOUTS
                .iter()
                .position(|l| l.name == name)
                .expect("a loadout")
        };
        // A weapon swapped in hand, a spell readied in the left, fists, a two-hander, back.
        let cues = [
            Cue::Swap(0.5, at("sword+spell")),
            Cue::Swap(1.0, at("fists")),
            Cue::Swap(1.5, at("2h sword")),
            Cue::Swap(2.0, at("dual swords")),
            Cue::Swap(2.5, at("two spells")),
            Cue::Swap(3.0, at("sword+shield")),
        ];
        let frames = play(&LOADOUTS[at("sword")], &cues, 3.6, &mut clips);
        all.push(("swaps".into(), LOADOUTS[at("sword")], frames));
        all
    })
}

/// Fails listed, at most a dozen shown.
fn none(what: &str, fails: &[String]) {
    assert!(
        fails.is_empty(),
        "{what}: {} frames, e.g. {:#?}",
        fails.len(),
        fails.iter().take(12).collect::<Vec<_>>()
    );
}

/// The largest a hand may move between two frames at 60 a second (inches): 480 inches a second,
/// a hard swing's hand. The tracks' strikes played by their own clock jumped 20 to 27 inches in a
/// frame; a swing restarted mid-swing jumped the hand back across the screen.
const MOST_STEP: f32 = 8.0;
/// The most a hand's step may change from one frame to the next (inches): a sudden start or stop.
const MOST_JERK: f32 = 5.0;
/// The most a weapon may turn in a frame (degrees).
const MOST_TURN: f32 = 35.0;

#[test]
fn no_hand_or_weapon_pops_between_frames() {
    let mut fails = Vec::new();
    for (name, _, frames) in scenes_once() {
        for side in 0..2 {
            let g = |f: &Frame| if side == 0 { Some(f.right) } else { f.left };
            let mut last: Option<f32> = None;
            for w in frames.windows(2) {
                let (Some(a), Some(b)) = (g(&w[0]), g(&w[1])) else {
                    last = None;
                    continue;
                };
                let (step, turn) = steps(&a, &b);
                let jerk = last.map_or(0.0, |l| (step - l).abs());
                if step > MOST_STEP || jerk > MOST_JERK || turn > MOST_TURN {
                    fails.push(format!(
                        "{name} side {side} at {:.2}: step {step:.1} jerk {jerk:.1} turn {turn:.0}",
                        w[1].t
                    ));
                }
                last = Some(step);
            }
        }
    }
    none("hands jump", &fails);
}

#[test]
fn every_swing_sweeps_across_the_middle_one_way() {
    let mut fails = Vec::new();
    let mut swings = 0;
    for (name, lo, frames) in scenes_once() {
        for idx in each_swing(frames) {
            swings += 1;
            let r = read_swing(lo, frames, &idx);
            let at = frames[idx[0]].t;
            // Crossing the middle, within 17 degrees of it up or down, without wavering back.
            if !r.crossed || r.cross_y.abs() > 0.3 || r.reversals > 0 {
                fails.push(format!("{name} swing at {at:.2}: {r:?}"));
            }
        }
    }
    assert!(swings > 80, "{swings}");
    none("swings", &fails);
}

#[test]
fn swings_are_quick_and_over_before_the_next() {
    let mut fails = Vec::new();
    for (name, lo, frames) in scenes_once() {
        for idx in each_swing(frames) {
            let r = read_swing(lo, frames, &idx);
            let at = frames[idx[0]].t;
            // A light swing in 0.4 to 0.6 s (fists a touch quicker), its strike across the
            // middle within a quarter second of the press; a two-hander under a second, across
            // the middle by 0.45 s. Before: 1.17 s for every one-hander (0.9 s for fists, 1.6 s
            // for two-handers), longer than the weapon's own recovery.
            let (lo_s, hi_s, cross) = match (lo.two, lo.fists()) {
                (true, _) => (0.75, 1.0, 0.45),
                (_, true) => (0.36, 0.5, 0.25),
                _ => (0.4, 0.6, 0.25),
            };
            if r.secs < lo_s - 1e-3 || r.secs > hi_s + 1e-3 || r.secs > lo.gate {
                fails.push(format!(
                    "{name} at {at:.2}: {:.2} s (recovery {:.2})",
                    r.secs, lo.gate
                ));
            }
            if r.cross_after > cross {
                fails.push(format!(
                    "{name} at {at:.2}: across after {:.2} s",
                    r.cross_after
                ));
            }
        }
    }
    none("swing timing", &fails);
}

#[test]
fn the_weapon_never_meets_the_camera() {
    let mut fails = Vec::new();
    for (name, _, frames) in scenes_once() {
        for f in frames {
            let lo = &f.lo;
            if lo.right_spell {
                continue;
            }
            if blade_sliced(lo, false, &f.right) {
                fails.push(format!("{name} at {:.2}: right", f.t));
            }
            if lo.dual()
                && let Some(l) = &f.left
                && blade_sliced(lo, true, l)
            {
                fails.push(format!("{name} at {:.2}: left", f.t));
            }
        }
    }
    none("blade through the near plane", &fails);
}

#[test]
fn the_weapon_stays_in_the_fist() {
    let mut fails = Vec::new();
    for (name, _, frames) in scenes_once() {
        for f in frames {
            for side in 0..2 {
                let gap = shortfall(f, side);
                // Before: up to 16 inches, the arm locked straight short of the tracks' reach.
                if gap > 0.5 {
                    fails.push(format!("{name} side {side} at {:.2}: {gap:.1} in", f.t));
                }
            }
        }
    }
    none("weapon off the hand", &fails);
}

#[test]
fn the_arms_cut_edge_stays_off_screen_and_out_of_the_near_plane_in_play() {
    let mut fails = Vec::new();
    for (name, _, frames) in scenes_once() {
        for f in frames.iter().step_by(2) {
            let shapes = [Some(f.right.hand), f.left.map(|l| l.hand)];
            for e in arm_faults(&f.world, shapes) {
                fails.push(format!("{name} at {:.2}: {e}", f.t));
            }
        }
    }
    none("arm faults", &fails);
}

#[test]
fn the_arms_bend_as_arms_do() {
    let mut fails = Vec::new();
    for (name, _, frames) in scenes_once() {
        for f in frames {
            for side in 0..2 {
                if side == 1 && f.left.is_none() {
                    continue;
                }
                let (bend, wrong, wrist, roll) = anatomy(f, side);
                let at = format!("{name} side {side} at {:.2}", f.t);
                // Never locked straight (before: 179 degrees, reaching past the arm's length)
                // nor folded shut, never bent the wrong way.
                if !(40.0..=165.0).contains(&bend) || wrong {
                    fails.push(format!("{at}: elbow {bend:.0} wrong {wrong}"));
                }
                // The hand never folded back over the forearm.
                if wrist > 150.0 {
                    fails.push(format!("{at}: wrist {wrist:.0}"));
                }
                // At rest, a hand as a hand holds things: the wrist within 70 degrees of the
                // forearm's line, the forearm turned no more than it turns (before, a sword at
                // the ready: wrist 92, forearm wrung 146).
                if f.at_rest && (wrist > 70.0 || roll > 115.0) {
                    fails.push(format!("{at} at rest: wrist {wrist:.0} roll {roll:.0}"));
                }
            }
        }
    }
    none("arms", &fails);
}

#[test]
fn at_the_ready_and_blocking_the_weapon_is_in_view() {
    // On a 16:9 screen, unwidened: where the player looks.
    let in_view = |p: Vec3| {
        screen(p).is_some_and(|s| s.x.abs() < tan_h_16_9() && s.y.abs() < tan_v()) && -p.z > NEAR
    };
    let mut fails = Vec::new();
    for (name, _, frames) in scenes_once() {
        for f in frames.iter().filter(|f| f.at_rest || f.blocking) {
            let lo = &f.lo;
            let at = format!("{name} at {:.2}", f.t);
            // The right hand's weapon: its fist and the hilt's end, 8 inches up (a spell's hand,
            // a fist).
            let len = lo.length(false).min(8.0);
            if !in_view(f.right.at) || !in_view(up_blade(&f.right, len)) {
                fails.push(format!("{at}: right at {:.1}", f.right.at));
            }
            // A second weapon, a spell, the left fist; a shield only while it blocks (at rest it
            // is carried low, out of the way, by design).
            if let Some(l) = &f.left {
                let shown = match lo.left {
                    Hand::Shield => f.blocking,
                    _ => !lo.two,
                };
                if shown && !in_view(l.at) {
                    fails.push(format!("{at}: left at {:.1}", l.at));
                }
            }
        }
    }
    none("out of view", &fails);
}

/// A spell is held and cast by its own hand, the two-hand way: the hands stay apart, the right on the
/// right of the view and the left on the left, never brought together on one hilt or one point.
#[test]
fn a_spell_hand_stays_its_own_hand() {
    let mut fails = Vec::new();
    for (name, lo, frames) in scenes_once() {
        if !(lo.right_spell || matches!(lo.left, Hand::Spell { .. })) || name.contains("draw") {
            continue;
        }
        for f in frames {
            let spell_now = f.lo.right_spell || matches!(f.lo.left, Hand::Spell { .. });
            // A swing crosses the middle on its way; the hands at the ready and casting stay apart.
            if !spell_now || f.swing.is_some() {
                continue;
            }
            let Some(l) = f.left else {
                // The other hand down out of sight is its own hand too.
                continue;
            };
            let apart = f.right.at.x - l.at.x;
            if apart < SPELL_HANDS_APART {
                fails.push(format!("{name} t {:.2}: right x {:.1} left x {:.1}", f.t, f.right.at.x, l.at.x));
            }
        }
    }
    none("a spell hand drawn together with the other", &fails);
}

/// How far apart (inches, across the view) the two hands stay when one holds a spell.
const SPELL_HANDS_APART: f32 = 6.0;
