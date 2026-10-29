//! The third-person body's layered pose (WOMW): a few bone rotations composed over whatever WoW
//! clip the body plays (its walk, its run, the rifle's ready stance, a swing), so a character
//! seen from outside does what its owner does in first person: the upper body and the gun follow
//! the aim up and down, a shot kicks, a reload drops the left hand to the magazine, a grenade is
//! wound up and thrown, a spell's arm points where it goes, a shield comes up, a crouch bends the
//! knees and a prone body lies down, a sprint carries the gun low, a tool swings.
//!
//! Why procedural and not MW2's own player animations: MW2's third-person animations
//! (`pb_*`/`pt_*`) live in the multiplayer zones on MW2's own body skeleton, whose bones have
//! rest rotations and proportions WoW's do not, and their aim is a blend tree of additive aim
//! poses. Retargeting them to nine WoW races' skeletons at runtime is a large piece of work whose
//! result cannot be judged without the game running; a handful of rotations on the bones WoW's
//! own clips already move costs microseconds, works on every race, and keeps WoW's walk and run.
//!
//! The bones are found from the rig alone ([`bind_bones`]): the chest is where the two arms meet,
//! the pelvis where an arm and a leg meet, the upper arm the second bone from the chest toward a
//! hand, the neck the first toward the helm, the thigh and shin the first two below where the
//! legs meet. The strafe counter-twist's bones (SpineLow and Head) are left to benilla.
//!
//! Model space is benilla's (Bevy axes): forward −Z, up +Y, right +X. A layer writes in
//! `PosePost`; a bone whose clip did not key it this frame still holds the layer's last output,
//! and is put back to its base first ([`LayerMemory`]), so nothing accumulates.

use std::collections::HashMap;
use std::f32::consts::PI;

use benilla_app::fusion::UnitAttach;
use benilla_world::rig_anim::{AnimParked, PhysicsDrivenPose, PosePost, RigPose};
use bevy::prelude::*;

/// M2 attachment points the bones are found from: the hands, the knees, the helm.
pub const ATTACH_HAND_RIGHT: u16 = 1;
pub const ATTACH_HAND_LEFT: u16 = 2;
pub const ATTACH_KNEE_RIGHT: u16 = 7;
pub const ATTACH_KNEE_LEFT: u16 = 8;
pub const ATTACH_HELM: u16 = 11;

/// A tool in the hands (Utility): the swing each one takes. The gathering tools' code tells a
/// swing through [`crate::body_state::ToolSwing`]; the kind travels on the wire as its byte.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum ToolKind {
    #[default]
    None,
    /// A mining pick: overhead, down onto the vein.
    Pick,
    /// A woodcutting axe: a level chop.
    Axe,
    /// A skinning knife: kneeling, cutting low.
    Knife,
    /// Herbs: bent to the ground, picking.
    Herb,
    /// The repair hammer: short overhead taps.
    Hammer,
    /// A fishing pole: back over the shoulder and cast out.
    FishingPole,
    /// An instrument (Minstrelsy): held and played, never swung.
    Instrument,
}

impl ToolKind {
    pub const ALL: [ToolKind; 8] = [
        ToolKind::None,
        ToolKind::Pick,
        ToolKind::Axe,
        ToolKind::Knife,
        ToolKind::Herb,
        ToolKind::Hammer,
        ToolKind::FishingPole,
        ToolKind::Instrument,
    ];

    pub fn wire(self) -> u8 {
        self as u8
    }

    pub fn from_wire(b: u8) -> ToolKind {
        ToolKind::ALL
            .get(usize::from(b))
            .copied()
            .unwrap_or_default()
    }

    /// The WoW clip a swing of it plays under the layer (`AnimationData.dbc`): a one-hander's
    /// attack for the striking tools, Loot (the kneel) for the low ones, the directed cast for
    /// the line.
    pub fn swing_anim(self) -> Option<u16> {
        match self {
            ToolKind::None | ToolKind::Instrument => None,
            ToolKind::Pick | ToolKind::Axe | ToolKind::Hammer => Some(17),
            ToolKind::Knife | ToolKind::Herb => Some(50),
            ToolKind::FishingPole => Some(53),
        }
    }

    /// How long one swing's layer lasts (seconds).
    pub fn swing_secs(self) -> f32 {
        match self {
            ToolKind::Hammer => 0.45,
            ToolKind::Knife | ToolKind::Herb => 1.0,
            ToolKind::FishingPole => 0.9,
            _ => 0.75,
        }
    }
}

/// What the body should be doing, as its owner does it in first person: the inputs of the
/// layer, the same for our own character and for another player's (crate::body_state).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PoseTarget {
    /// The aim's pitch, radians, up positive.
    pub pitch: f32,
    /// Modern with the gun up: the upper body carries it along the aim.
    pub gun_up: bool,
    /// The gun up is a pistol: held out in front, the off hand cupping the grip, rather than a
    /// long gun's stock to the shoulder and foregrip reach.
    pub pistol: bool,
    /// How far ahead of the grip the gun's foregrip is (yards, from its length), when known:
    /// the support hand reaches there instead of a fixed share of the shoulders.
    pub foregrip: Option<f32>,
    pub ads: bool,
    pub sprint: bool,
    pub crouch: bool,
    pub prone: bool,
    /// A spell held, charging or pouring in the right and the left hand.
    pub cast: [bool; 2],
    /// The shield raised (the left arm).
    pub block: bool,
    /// The last shot, a reload's start and length, a throw's start (all `Time::elapsed_secs`).
    pub fired_at: Option<f32>,
    pub reload: Option<(f32, f32)>,
    pub throw_at: Option<f32>,
    /// A grenade held, its pin pulled: the wind-up holds until it is thrown.
    pub cooking: bool,
    /// A spell cast from a hand: its hand (0 right, 1 left) and when.
    pub cast_at: Option<(usize, f32)>,
    /// A tool's swing and when.
    pub tool: Option<(ToolKind, f32)>,
}

/// The continuous inputs eased toward their targets, per body.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Smooth {
    pub pitch: f32,
    pub ads: f32,
    pub sprint: f32,
    pub crouch: f32,
    pub prone: f32,
    pub cast: [f32; 2],
    pub block: f32,
    pub gun: f32,
}

/// Ease `from` toward `to` at `rate` per second (exponential, frame-rate independent).
pub fn ease(from: f32, to: f32, rate: f32, dt: f32) -> f32 {
    from + (to - from) * (1.0 - (-rate * dt).exp())
}

fn on(b: bool) -> f32 {
    if b { 1.0 } else { 0.0 }
}

impl Smooth {
    pub fn step(&mut self, t: &PoseTarget, dt: f32) {
        self.pitch = ease(self.pitch, t.pitch, 14.0, dt);
        self.ads = ease(self.ads, on(t.ads), 10.0, dt);
        self.sprint = ease(self.sprint, on(t.sprint), 7.0, dt);
        // MW2's stance times: 0.2 s to crouch, 0.5 s into prone (crate::stance_view).
        self.crouch = ease(self.crouch, on(t.crouch), 12.0, dt);
        self.prone = ease(self.prone, on(t.prone), 6.0, dt);
        for h in 0..2 {
            self.cast[h] = ease(self.cast[h], on(t.cast[h]), 12.0, dt);
        }
        self.block = ease(self.block, on(t.block), 14.0, dt);
        self.gun = ease(self.gun, on(t.gun_up), 8.0, dt);
    }
}

/// One arm's layer: the shoulder turned so the hand points along `dir` (model space) by
/// `weight`, then the arm raised forward by `raise` radians (negative lowers it).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ArmLayer {
    pub dir: Vec3,
    pub weight: f32,
    pub raise: f32,
}

/// The layer's rotations for one frame (radians; model-space axes).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Offsets {
    /// The whole body pitched about the hips (prone lies it face down: about −π/2).
    pub pelvis_pitch: f32,
    /// How far down the pelvis goes toward lying (0 standing, 1 on the ground).
    pub prone: f32,
    /// The upper body about the chest: pitch (up positive) and yaw (right shoulder forward
    /// positive).
    pub chest_pitch: f32,
    pub chest_yaw: f32,
    pub neck_pitch: f32,
    /// Hips and knees bent: the thigh forward by this, the shin back by twice it.
    pub legs: f32,
    /// Right then left.
    pub arms: [ArmLayer; 2],
    /// A two-handed gun's support hand: how far the left hand is brought out to the foregrip
    /// (0 left to the clip, 1 on it), along the aim's direction.
    pub support: f32,
    pub support_dir: Vec3,
    /// How far ahead of the grip the support hand goes, in shoulder widths.
    pub support_span: f32,
    /// Or in yards, the gun's own foregrip (wins over `support_span`).
    pub support_reach: Option<f32>,
    /// Right then left: the forearm bent up by this (radians), the hand toward the shoulder.
    pub elbow: [f32; 2],
    /// Right then left: the hand turned back down by this (radians) after the lift and the bend,
    /// so the gun keeps the aim's line while the arms carry it higher.
    pub hand_back: [f32; 2],
}

/// How a gun class is held: the support hand's reach (shoulder widths ahead of the grip) and
/// each forearm's bend toward the shoulder.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GunHold {
    pub span: f32,
    pub elbow: [f32; 2],
}

/// A long gun: the stock up toward the shoulder (the grip forearm bent), the support hand out on
/// the foregrip. A pistol: both hands together on the grip, out in front.
pub fn gun_hold(pistol: bool) -> GunHold {
    if pistol {
        GunHold {
            span: PISTOL_SPAN,
            elbow: [0.25, 0.25],
        }
    } else {
        GunHold {
            span: FOREGRIP_SPAN,
            elbow: [STOCK_BEND, 0.0],
        }
    }
}

/// The off hand's reach on a pistol: onto the grip hand, cupping it.
pub const PISTOL_SPAN: f32 = 0.15;
/// The grip forearm's bend that brings a long gun's stock toward the shoulder (radians).
pub const STOCK_BEND: f32 = 0.3;

/// How far ahead of the grip hand the foregrip is, in shoulder widths.
pub const FOREGRIP_SPAN: f32 = 1.1;
/// How far the gun arm comes up toward the shoulder from the clip's low carry (radians).
pub const SHOULDER_LIFT: f32 = 0.2;

/// The aim's direction in model space: forward (−Z) pitched up by `pitch`.
pub fn aim_dir(pitch: f32) -> Vec3 {
    Vec3::new(0.0, pitch.sin(), -pitch.cos())
}

/// A shot's kick: up in 40 ms, back down over the next ~0.3 s.
pub fn recoil_curve(dt: f32) -> f32 {
    if !(0.0..0.6).contains(&dt) {
        return 0.0;
    }
    let rise = (dt / 0.04).min(1.0);
    rise * (-(dt - 0.04).max(0.0) / 0.1).exp()
}

/// A bump over `u` in 0..1: 0 at both ends, 1 in the middle, eased in and out.
pub fn bump(u: f32) -> f32 {
    if !(0.0..=1.0).contains(&u) {
        return 0.0;
    }
    (PI * u).sin()
}

/// A throw's phases: the wind-up's share and the release's (each 0..1) `dt` seconds in. Held
/// (cooking), the wind-up holds.
pub fn throw_phases(dt: f32, cooking: bool) -> (f32, f32) {
    const WIND: f32 = 0.25;
    const RELEASE: f32 = 0.2;
    const BACK: f32 = 0.4;
    if cooking {
        return ((dt / WIND).clamp(0.0, 1.0), 0.0);
    }
    if dt < 0.0 || dt > WIND + RELEASE + BACK {
        return (0.0, 0.0);
    }
    if dt < WIND {
        return (dt / WIND, 0.0);
    }
    if dt < WIND + RELEASE {
        let u = (dt - WIND) / RELEASE;
        return (1.0 - u, u);
    }
    (0.0, 1.0 - (dt - WIND - RELEASE) / BACK)
}

/// A direction from its parts, normalized.
fn dir(x: f32, y: f32, z: f32) -> Vec3 {
    Vec3::new(x, y, z).normalize()
}

/// The layer for one frame: the eased inputs `s` and the one-shots in `t`, at `now`.
pub fn offsets(t: &PoseTarget, s: &Smooth, now: f32) -> Offsets {
    let mut o = Offsets::default();
    let pitch = s.pitch.clamp(-1.3, 1.3);
    // The aim: the gun takes most of it in the chest (the arms and the gun ride the chest), a
    // bare body less, the neck the rest.
    let chest_share = 0.4 + 0.4 * s.gun;
    o.chest_pitch = chest_share * pitch;
    o.neck_pitch = (0.85 - chest_share).max(0.2) * pitch;
    // ADS: the cheek down onto the stock.
    o.neck_pitch -= 0.18 * s.ads * s.gun;
    // A sprint leans in and carries the gun low across the body.
    o.chest_pitch -= 0.15 * s.sprint;
    for arm in &mut o.arms {
        arm.raise -= 0.55 * s.sprint * s.gun;
    }
    // The gun up: brought from the clip's low carry toward the shoulder, the support hand out
    // on the foregrip (a reload or a sprint takes it off).
    let carry = s.gun * (1.0 - s.sprint);
    o.arms[0].raise += SHOULDER_LIFT * carry;
    o.arms[1].raise += SHOULDER_LIFT * carry;
    o.support = carry;
    o.support_dir = aim_dir(pitch);
    let hold = gun_hold(t.pistol);
    o.support_span = hold.span;
    o.support_reach = if t.pistol { None } else { t.foregrip };
    o.elbow = hold.elbow.map(|e| e * carry);
    o.hand_back = [0, 1].map(|i| SHOULDER_LIFT * carry + o.elbow[i]);
    // A crouch: hips and knees bent, the chest over the knees.
    o.legs = 0.85 * s.crouch * (1.0 - s.prone);
    o.chest_pitch -= 0.2 * s.crouch * (1.0 - s.prone);
    // Prone: the body face down, the chest raised to look ahead.
    o.pelvis_pitch = -1.45 * s.prone;
    o.prone = s.prone;
    o.chest_pitch += 1.05 * s.prone;
    // A shot's kick, less aimed down the sights.
    if let Some(at) = t.fired_at {
        let r = recoil_curve(now - at) * (1.0 - 0.5 * s.ads);
        o.chest_pitch += 0.07 * r;
        for arm in &mut o.arms {
            arm.raise += 0.06 * r;
        }
    }
    // A reload: the left hand down to the magazine, the eyes on the gun.
    if let Some((start, secs)) = t.reload {
        let b = bump((now - start) / secs.max(0.2));
        o.arms[1] = ArmLayer {
            dir: dir(0.25, -0.75, -0.6),
            weight: 0.85 * b,
            raise: o.arms[1].raise,
        };
        o.chest_pitch -= 0.1 * b;
        o.neck_pitch -= 0.12 * b;
        o.support *= 1.0 - b;
    }
    // A spell held in a hand: that arm points where it will go.
    let aim = aim_dir(pitch);
    for h in 0..2 {
        if s.cast[h] > 0.01 {
            o.arms[h] = ArmLayer {
                dir: aim,
                weight: 0.9 * s.cast[h],
                raise: 0.0,
            };
        }
    }
    // A cast from a hand: the arm thrust out along the aim, as the first-person hand thrusts.
    if let Some((hand, at)) = t.cast_at {
        let b = bump((now - at) / 0.45);
        if b > 0.0 && hand < 2 && o.arms[hand].weight < b {
            o.arms[hand] = ArmLayer {
                dir: aim,
                weight: b,
                raise: 0.0,
            };
        }
    }
    // The shield up across the front.
    if s.block > 0.01 {
        o.arms[1] = ArmLayer {
            dir: dir(0.45, 0.25, -0.85),
            weight: s.block,
            raise: 0.0,
        };
    }
    // A grenade: wound up over the shoulder, thrown forward, the chest turning with it.
    if let Some(at) = t.throw_at {
        let (wind, release) = throw_phases(now - at, t.cooking);
        let w = wind.max(release);
        if w > 0.0 {
            let d = dir(0.15, 0.7, 0.7) * wind + dir(0.0, 0.35, -0.94) * release;
            o.arms[0] = ArmLayer {
                dir: d.normalize_or(aim),
                weight: w,
                raise: 0.0,
            };
            o.chest_yaw += -0.35 * wind + 0.25 * release;
        }
    }
    if let Some((tool, at)) = t.tool {
        tool_layer(&mut o, tool, (now - at) / tool.swing_secs(), aim);
    }
    o
}

/// A tool's swing `u` (0..1) through it.
fn tool_layer(o: &mut Offsets, tool: ToolKind, u: f32, aim: Vec3) {
    if !(0.0..=1.0).contains(&u) {
        return;
    }
    match tool {
        ToolKind::None | ToolKind::Instrument => {}
        ToolKind::Pick | ToolKind::Hammer => {
            // Up over the head, then down onto the work.
            let (wind, strike) = if u < 0.4 {
                (u / 0.4, 0.0)
            } else if u < 0.6 {
                (1.0 - (u - 0.4) / 0.2, (u - 0.4) / 0.2)
            } else {
                (0.0, 1.0 - (u - 0.6) / 0.4)
            };
            let d = dir(0.0, 0.9, 0.3) * wind + dir(0.0, -0.45, -0.9) * strike;
            o.arms[0] = ArmLayer {
                dir: d.normalize_or(aim),
                weight: wind.max(strike),
                raise: 0.0,
            };
            o.arms[1] = o.arms[0];
            o.chest_pitch += 0.15 * wind - 0.25 * strike;
        }
        ToolKind::Axe => {
            // Back over the right shoulder, then level across.
            let b = bump(u);
            let d = if u < 0.45 {
                dir(0.8, 0.3, 0.3)
            } else {
                dir(-0.4, 0.05, -0.9)
            };
            o.arms[0] = ArmLayer {
                dir: d,
                weight: b,
                raise: 0.0,
            };
            o.arms[1] = o.arms[0];
            o.chest_yaw += if u < 0.45 { -0.4 * b } else { 0.35 * b };
        }
        ToolKind::Knife | ToolKind::Herb => {
            // Bent to the ground, the right hand working low.
            let b = bump(u);
            o.chest_pitch -= 0.55 * b;
            o.legs = o.legs.max(0.6 * b);
            o.arms[0] = ArmLayer {
                dir: dir(0.1, -0.8, -0.6),
                weight: b,
                raise: 0.0,
            };
        }
        ToolKind::FishingPole => {
            let (wind, release) = throw_phases(u * 0.85, false);
            let d = dir(0.1, 0.8, 0.6) * wind + dir(0.0, 0.3, -0.95) * release;
            o.arms[0] = ArmLayer {
                dir: d.normalize_or(aim),
                weight: wind.max(release),
                raise: 0.0,
            };
        }
    }
}

/// The bones the layer moves, found from the rig ([`bind_bones`]).
#[derive(Clone, Debug, PartialEq)]
pub struct RigBones {
    pub pelvis: u16,
    pub chest: u16,
    pub neck: Option<u16>,
    /// Right then left: the upper arm, and the hand's point (its bone and offset).
    pub upper_arm: [u16; 2],
    /// Right then left: the forearm between the upper arm and the hand, when the arm has one.
    pub forearm: [Option<u16>; 2],
    pub hand: [(u16, Vec3); 2],
    /// Right then left: thigh, shin and foot, when the model has knees.
    pub legs: Option<[[u16; 3]; 2]>,
}

impl RigBones {
    /// Every bone the layer writes.
    pub fn touched(&self) -> impl Iterator<Item = u16> + '_ {
        [self.pelvis, self.chest]
            .into_iter()
            .chain(self.neck)
            .chain(self.upper_arm)
            .chain(self.forearm.into_iter().flatten())
            // The gun hands are turned back level, so they are written too.
            .chain(
                self.hand
                    .iter()
                    .map(|h| h.0)
                    .filter(|h| !self.upper_arm.contains(h) && !self.forearm.contains(&Some(*h))),
            )
            .chain(self.legs.iter().flatten().flatten().copied())
    }
}

/// `b` and its ancestors, from `b` up, without allocating (capped against a bad hierarchy).
fn up(parents: &[i16], b: u16) -> impl Iterator<Item = u16> + '_ {
    let mut c = Some(b);
    let mut steps = 0;
    std::iter::from_fn(move || {
        let x = c.filter(|&x| usize::from(x) < parents.len() && steps <= parents.len())?;
        steps += 1;
        c = u16::try_from(parents[usize::from(x)]).ok();
        Some(x)
    })
}

/// `b` and its ancestors, from `b` up.
fn chain(parents: &[i16], b: u16) -> Vec<u16> {
    up(parents, b).collect()
}

/// The nearest common ancestor of two bones.
fn meet(parents: &[i16], a: u16, b: u16) -> Option<u16> {
    let up = chain(parents, a);
    chain(parents, b).into_iter().find(|x| up.contains(x))
}

/// The bones from just below `top` down to `b`, top first; empty when `top` is not above `b`.
fn below(parents: &[i16], top: u16, b: u16) -> Vec<u16> {
    let mut c = chain(parents, b);
    match c.iter().position(|&x| x == top) {
        Some(i) => {
            c.truncate(i);
            c.reverse();
            c
        }
        None => Vec::new(),
    }
}

/// Find the layer's bones from the parents and the attachment points (bone, offset): the hands
/// and the helm are needed; the knees, for the legs, may be missing (`None` then).
pub fn bind_bones(
    parents: &[i16],
    hands: [(u16, Vec3); 2],
    knees: Option<[u16; 2]>,
    helm: Option<u16>,
    model: impl Fn(u16) -> Vec3,
) -> Option<RigBones> {
    let chest = meet(parents, hands[0].0, hands[1].0)?;
    let arm = |h: u16| {
        let p = below(parents, chest, h);
        // Chest, clavicle, upper arm, forearm, hand: the upper arm is the second down; a
        // simpler arm (no clavicle) turns from its first.
        match p.len() {
            0 => None,
            1 | 2 => Some(p[0]),
            _ => Some(p[1]),
        }
    };
    let upper_arm = [arm(hands[0].0)?, arm(hands[1].0)?];
    // The bone after the upper arm toward the hand, unless that is the hand's own.
    let forearm = |h: u16, upper: u16| {
        let p = below(parents, chest, h);
        let i = p.iter().position(|&b| b == upper)?;
        p.get(i + 1).copied().filter(|&b| b != h)
    };
    let forearm = [
        forearm(hands[0].0, upper_arm[0]),
        forearm(hands[1].0, upper_arm[1]),
    ];
    let neck = helm.and_then(|h| below(parents, chest, h).first().copied());
    // No knee points (a gnome's model has none): the lowest bone on each side below the hips,
    // off the upper body, stands in for the knee; only the thigh above it is read from it.
    let knees = knees.or_else(|| {
        // Under the chest's grandparent (the pelvis), off the chest: the legs' bones, not the
        // root's loose ones.
        let hips = *chain(parents, chest).get(2)?;
        let lowest = |side: f32| {
            (0..parents.len() as u16)
                .filter(|&b| {
                    up(parents, b).any(|x| x == hips) && !up(parents, b).any(|x| x == chest)
                })
                .map(|b| (b, model(b)))
                .filter(|(_, at)| side * at.x > 0.03)
                .min_by(|a, b| a.1.y.total_cmp(&b.1.y))
                .map(|(b, _)| b)
        };
        Some([lowest(1.0)?, lowest(-1.0)?])
    });
    let knees_found = knees.and_then(|[r, l]| {
        let hips = meet(parents, r, l)?;
        let pelvis = meet(parents, hands[0].0, r)?;
        // The child of `b` whose subtree reaches lowest: down the leg, past a knee point's own
        // leaf bone (a tauren's hangs off the thigh beside the shin).
        let lowest_child = |b: u16| -> Option<u16> {
            let reach = |c: u16| {
                (0..parents.len() as u16)
                    .filter(|&d| up(parents, d).any(|x| x == c))
                    .map(|d| model(d).y)
                    .fold(f32::MAX, f32::min)
            };
            (0..parents.len())
                .filter(|&c| parents[c] == b as i16)
                .map(|c| c as u16)
                .min_by(|&a, &b| reach(a).total_cmp(&reach(b)))
        };
        let leg = |k: u16| -> Option<[u16; 3]> {
            let thigh = *below(parents, hips, k).first()?;
            let shin = lowest_child(thigh)?;
            let foot = lowest_child(shin)?;
            Some([thigh, shin, foot])
        };
        Some((pelvis, [leg(r)?, leg(l)?]))
    });
    let pelvis = match knees_found {
        Some((p, _)) => p,
        // The chest's grandparent: its parent is SpineLow, the strafe twist's.
        None => *chain(parents, chest).get(2).unwrap_or(&chest),
    };
    if pelvis == chest {
        return None;
    }
    Some(RigBones {
        pelvis,
        chest,
        neck,
        upper_arm,
        forearm,
        hand: hands,
        legs: knees_found.map(|(_, l)| l),
    })
}

/// A bone's model-space rotation from the locals.
pub fn model_rot(locals: &[Transform], parents: &[i16], b: u16) -> Quat {
    let mut q = Quat::IDENTITY;
    for x in up(parents, b) {
        q = locals[usize::from(x)].rotation * q;
    }
    q
}

/// A point `offset` under bone `b`, in model space, from the locals.
pub fn model_point(locals: &[Transform], parents: &[i16], b: u16, offset: Vec3) -> Vec3 {
    let mut p = offset;
    for x in up(parents, b) {
        p = locals[usize::from(x)].transform_point(p);
    }
    p
}

/// Turn bone `b` by `delta` in model space about its own pivot.
pub fn turn_in_model(locals: &mut [Transform], parents: &[i16], b: u16, delta: Quat) {
    let rp = match u16::try_from(parents[usize::from(b)]) {
        Ok(p) => model_rot(locals, parents, p),
        Err(_) => Quat::IDENTITY,
    };
    let t = &mut locals[usize::from(b)];
    t.rotation = (rp.inverse() * delta * rp * t.rotation).normalize();
}

/// How far the pelvis comes down for the lowest ankle's measured rise as the knees bend, so the
/// planted foot stays on the ground (never negative: a bend never lifts the body).
pub fn crouch_drop(ankle_rise: f32) -> f32 {
    ankle_rise.max(0.0)
}

/// Lay the offsets onto the locals (already at the clip's pose for this frame).
pub fn apply(locals: &mut [Transform], parents: &[i16], bones: &RigBones, o: &Offsets) {
    let n = locals.len();
    let ok = |b: u16| usize::from(b) < n;
    if !bones.touched().all(ok) || !ok(bones.hand[0].0) || !ok(bones.hand[1].0) {
        return;
    }
    // The legs bent, and the pelvis lowered so the feet stay on the ground.
    let mut drop = 0.0;
    if let Some(legs) = bones.legs
        && o.legs.abs() > 1e-4
    {
        // Measured, not assumed: the lowest ankle's height before the bend and after, so a
        // short-shinned or bent-at-rest leg (a gnome's, a dwarf's, a tauren's hocks) and an
        // uneven stance keep the planted foot planted.
        let (mut before, mut after) = (f32::MAX, f32::MAX);
        for [thigh, shin, foot] in legs {
            before = before.min(model_point(locals, parents, foot, Vec3::ZERO).y);
            turn_in_model(locals, parents, thigh, Quat::from_rotation_x(o.legs));
            turn_in_model(locals, parents, shin, Quat::from_rotation_x(-2.0 * o.legs));
            turn_in_model(locals, parents, foot, Quat::from_rotation_x(o.legs));
            after = after.min(model_point(locals, parents, foot, Vec3::ZERO).y);
        }
        drop += crouch_drop(after - before);
    }
    // Prone: the whole body pitched about the hips and brought down to the ground.
    if o.prone > 1e-4 || o.pelvis_pitch.abs() > 1e-4 {
        let hips = model_point(locals, parents, bones.pelvis, Vec3::ZERO);
        turn_in_model(
            locals,
            parents,
            bones.pelvis,
            Quat::from_rotation_x(o.pelvis_pitch),
        );
        drop += o.prone * (hips.y - 0.25).max(0.0);
    }
    if drop > 1e-5 {
        let rp = match u16::try_from(parents[usize::from(bones.pelvis)]) {
            Ok(p) => model_rot(locals, parents, p),
            Err(_) => Quat::IDENTITY,
        };
        locals[usize::from(bones.pelvis)].translation += rp.inverse() * Vec3::new(0.0, -drop, 0.0);
    }
    // The upper body: yaw, then pitch about the chest.
    if o.chest_pitch.abs() > 1e-4 || o.chest_yaw.abs() > 1e-4 {
        let q = Quat::from_rotation_y(-o.chest_yaw) * Quat::from_rotation_x(o.chest_pitch);
        turn_in_model(locals, parents, bones.chest, q);
    }
    if let Some(neck) = bones.neck
        && o.neck_pitch.abs() > 1e-4
    {
        turn_in_model(locals, parents, neck, Quat::from_rotation_x(o.neck_pitch));
    }
    // Each arm: turned so the hand points along its direction, then raised.
    for side in 0..2 {
        let arm = o.arms[side];
        let upper = bones.upper_arm[side];
        if arm.weight > 1e-3 && arm.dir.length_squared() > 1e-6 {
            let shoulder = model_point(locals, parents, upper, Vec3::ZERO);
            let (hb, ho) = bones.hand[side];
            let hand = model_point(locals, parents, hb, ho);
            if let (Some(from), Some(to)) =
                ((hand - shoulder).try_normalize(), arm.dir.try_normalize())
            {
                let full = Quat::from_rotation_arc(from, to);
                let q = Quat::IDENTITY.slerp(full, arm.weight.clamp(0.0, 1.0));
                turn_in_model(locals, parents, upper, q);
            }
        }
        if arm.raise.abs() > 1e-4 {
            turn_in_model(locals, parents, upper, Quat::from_rotation_x(arm.raise));
        }
    }
    // The forearms bent up toward the shoulder.
    for side in 0..2 {
        if let Some(fa) = bones.forearm[side]
            && o.elbow[side].abs() > 1e-4
            && usize::from(fa) < n
        {
            turn_in_model(locals, parents, fa, Quat::from_rotation_x(o.elbow[side]));
        }
    }
    // Each gun hand turned back level: the lift and the bend carried the gun higher, not off the
    // aim.
    for side in 0..2 {
        let hb = bones.hand[side].0;
        if o.hand_back[side].abs() > 1e-4
            && hb != bones.upper_arm[side]
            && Some(hb) != bones.forearm[side]
            && usize::from(hb) < n
        {
            turn_in_model(locals, parents, hb, Quat::from_rotation_x(-o.hand_back[side]));
        }
    }
    // The support hand onto the foregrip: the left arm turned so its hand points at a spot a
    // shoulder's width or so ahead of the grip hand, along the aim.
    if o.support > 1e-3 && o.support_dir.length_squared() > 1e-6 {
        let [ru, lu] = bones.upper_arm;
        let rs = model_point(locals, parents, ru, Vec3::ZERO);
        let ls = model_point(locals, parents, lu, Vec3::ZERO);
        let (rb, ro) = bones.hand[0];
        let (lb, lo) = bones.hand[1];
        let grip = model_point(locals, parents, rb, ro);
        let hand = model_point(locals, parents, lb, lo);
        let reach = o.support_reach.unwrap_or(o.support_span * rs.distance(ls));
        let goal = grip + o.support_dir.normalize_or_zero() * reach;
        if let (Some(from), Some(to)) = ((hand - ls).try_normalize(), (goal - ls).try_normalize()) {
            let q = Quat::IDENTITY.slerp(Quat::from_rotation_arc(from, to), o.support.min(1.0));
            turn_in_model(locals, parents, lu, q);
        }
    }
}

/// Where a two-handed gun's support hand goes: ahead of the grip hand along the gun.
pub fn foregrip(grip: Vec3, along: Vec3, shoulder_width: f32) -> Vec3 {
    grip + along.normalize_or_zero() * FOREGRIP_SPAN * shoulder_width
}

/// What a bone held after the clip (`base`) and after the layer (`out`) last frame: a bone still
/// holding `out` was not re-keyed, and goes back to `base` before the layer runs again.
#[derive(Clone, Copy, Debug, PartialEq)]
struct Held {
    bone: u16,
    base: Transform,
    out: Transform,
}

/// One body's layer state: its bones, its eased inputs and what it last wrote.
#[derive(Debug, Default)]
pub struct LayerMemory {
    pub bones: Option<RigBones>,
    /// The bones were looked for and none found (not a humanoid): leave the body alone.
    tried: bool,
    pub smooth: Smooth,
    held: Vec<Held>,
}

impl LayerMemory {
    /// Put back the clip's own pose where the layer's output still stands, and remember it.
    fn restore(&mut self, locals: &mut [Transform]) {
        for h in &mut self.held {
            let Some(t) = locals.get_mut(usize::from(h.bone)) else {
                continue;
            };
            if *t == h.out {
                *t = h.base;
            } else {
                h.base = *t;
            }
        }
    }

    fn remember(&mut self, locals: &[Transform]) {
        for h in &mut self.held {
            if let Some(t) = locals.get(usize::from(h.bone)) {
                h.out = *t;
            }
        }
    }
}

/// The targets this frame, by body (crate::body_state writes them in `Update`).
#[derive(Resource, Default, Debug)]
pub struct BodyTargets(pub HashMap<Entity, PoseTarget>);

/// Each layered body's memory.
#[derive(Resource, Default, Debug)]
pub struct BodyLayers(pub HashMap<Entity, LayerMemory>);

pub struct BodyPosePlugin;

impl Plugin for BodyPosePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<BodyTargets>()
            .init_resource::<BodyLayers>()
            .add_systems(PostUpdate, layer_bodies.in_set(PosePost));
    }
}

/// `MW2_TP_LAYER=0` stands the layer down (a bisect lever).
fn layer_on() -> bool {
    static ON: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *ON.get_or_init(|| std::env::var("MW2_TP_LAYER").map_or(true, |v| v != "0"))
}

/// Lay each targeted body's layer over its clip.
fn layer_bodies(
    time: Res<Time>,
    targets: Res<BodyTargets>,
    mut layers: ResMut<BodyLayers>,
    attach: UnitAttach,
    mut rigs: Query<&mut RigPose, (Without<AnimParked>, Without<PhysicsDrivenPose>)>,
) {
    let layers = &mut layers.0;
    // A body no longer targeted goes back to its clip (its held bones restored once), and is
    // forgotten.
    layers.retain(|e, mem| {
        if targets.0.contains_key(e) {
            return true;
        }
        if let Ok(mut rig) = rigs.get_mut(*e) {
            let rig = rig.as_mut();
            mem.restore(&mut rig.locals);
            rig.pose_dirty = true;
        }
        false
    });
    if !layer_on() {
        return;
    }
    let now = time.elapsed_secs();
    let dt = time.delta_secs().min(0.1);
    for (&unit, target) in &targets.0 {
        let Ok(mut rig) = rigs.get_mut(unit) else {
            continue;
        };
        let rig = rig.as_mut();
        let mem = layers.entry(unit).or_default();
        if mem.bones.is_none() && !mem.tried {
            let point = |id| attach.point(unit, id);
            mem.bones = match (point(ATTACH_HAND_RIGHT), point(ATTACH_HAND_LEFT)) {
                // Not a humanoid's skeleton, or one whose points are not read yet: looked at
                // again next frame until the hands are there, then once.
                (Some(r), Some(l)) => {
                    mem.tried = true;
                    let knees = point(ATTACH_KNEE_RIGHT)
                        .zip(point(ATTACH_KNEE_LEFT))
                        .map(|(r, l)| [r.0, l.0]);
                    let helm = point(ATTACH_HELM).map(|p| p.0);
                    let (locals, parents) = (&rig.locals, &rig.parents);
                    bind_bones(parents, [r, l], knees, helm, |b| {
                        model_point(locals, parents, b, Vec3::ZERO)
                    })
                }
                _ => None,
            };
            if let Some(b) = &mem.bones {
                mem.held = b
                    .touched()
                    .map(|bone| Held {
                        bone,
                        base: Transform::IDENTITY,
                        out: Transform::from_scale(Vec3::ZERO),
                    })
                    .collect();
            }
        }
        let Some(bones) = mem.bones.clone() else {
            continue;
        };
        mem.restore(&mut rig.locals);
        mem.smooth.step(target, dt);
        let o = offsets(target, &mem.smooth, now);
        apply(&mut rig.locals, &rig.parents, &bones, &o);
        mem.remember(&rig.locals);
        rig.pose_dirty = true;
    }
}

#[cfg(test)]
#[path = "body_pose_tests.rs"]
mod tests;
