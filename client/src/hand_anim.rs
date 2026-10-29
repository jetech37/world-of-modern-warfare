//! The hands' animation from the tracks' own first-person clips (crate::hand_anims):
//! the idles loop, a swing plays its attack, the shield its block, a spell its
//! ready loop and release, the weapon its draw, all as grips in the eye's frame that the arms reach
//! for (crate::melee_pose). The source blends its clips in a behaviour graph and its attacks start
//! wound up, so each one-shot blends in from the ready pose and back out.

use std::collections::HashMap;
use std::sync::Arc;

use bevy::prelude::*;

use crate::hands::Hand;
use crate::melee_pose::{Grip, mix};
use crate::hand_anims::{Track, sample};

/// The clips, loaded on first use (`None`: not embedded).
#[derive(Resource, Default)]
pub struct HandAnimClips(HashMap<&'static str, Option<Arc<Track>>>);

impl HandAnimClips {
    pub fn get(&mut self, name: &'static str) -> Option<Arc<Track>> {
        self.0
            .entry(name)
            .or_insert_with(|| crate::hand_anims::load(name).map(Arc::new))
            .clone()
    }
}

/// The tracks' first-person camera sees wider than ours, so its poses sit low and to the sides on
/// our screen: each grip is moved this far (inches; x toward the middle, y up) to fit our view.
pub const VIEW_FIT: Vec3 = Vec3::new(2.0, 6.5, -1.0);

fn fit(g: Grip, left: bool) -> Grip {
    let toward_middle = if left { VIEW_FIT.x } else { -VIEW_FIT.x };
    Grip {
        at: g.at + Vec3::new(toward_middle, VIEW_FIT.y, VIEW_FIT.z),
        ..g
    }
}

/// A clip `k` (0..1) of the way through, fitted to our view.
pub fn at(track: &Track, k: f32) -> (Grip, Grip) {
    let (r, l) = sample(track, k);
    (fit(r, false), fit(l, true))
}

/// A looping clip (an idle, a ready loop) at time `t` (seconds).
pub fn looped(track: &Track, t: f32) -> (Grip, Grip) {
    at(track, (t / track.duration.max(0.01)).fract())
}

/// The share of a one-shot `k` (0..1) of the way through that shows the clip rather than the
/// ready pose: in over its first [`BLEND_IN`], out over its last [`BLEND_OUT`].
pub fn one_shot_weight(k: f32) -> f32 {
    let s = |x: f32| {
        let x = x.clamp(0.0, 1.0);
        x * x * (3.0 - 2.0 * x)
    };
    s(k / BLEND_IN) * (1.0 - s((k - (1.0 - BLEND_OUT)) / BLEND_OUT))
}

pub const BLEND_IN: f32 = 0.12;
pub const BLEND_OUT: f32 = 0.22;

/// A one-shot clip `k` of the way through, blended with the ready pose.
pub fn one_shot(track: &Track, k: f32, ready: (Grip, Grip)) -> (Grip, Grip) {
    let (r, l) = at(track, k.clamp(0.0, 1.0));
    let w = one_shot_weight(k);
    (mix(&ready.0, &r, w), mix(&ready.1, &l, w))
}

/// The clip names by what the hands hold.
pub mod names {
    /// The ready idle: a one-hander, a two-handed sword, an axe or hammer, two weapons, fists.
    pub fn idle(two_handed: bool, axe: bool, dual: bool, fists: bool) -> &'static str {
        match (two_handed, axe, dual, fists) {
            (_, _, _, true) => "h2h_idle",
            (true, true, _, _) => "2hw_idle",
            (true, false, _, _) => "2hm_idle",
            (false, _, true, _) => "dw1hm1hmidle",
            _ => "1hm_idle",
        }
    }

    /// A swing: from the right (`attackright`) or the left, by the weapon.
    pub fn swing(from_right: bool, two_handed: bool, axe: bool, fists: bool) -> &'static str {
        match (fists, two_handed, axe, from_right) {
            (true, _, _, true) => "h2h_attackright",
            (true, _, _, false) => "h2h_attackleft",
            (_, true, true, true) => "2hw_attackright",
            (_, true, true, false) => "2hw_attackleft",
            (_, true, false, true) => "2hm_attackright",
            (_, true, false, false) => "2hm_attackleft",
            (_, false, _, true) => "1hm_attackright",
            (_, false, _, false) => "1hm_attackleft",
        }
    }

    /// The weapon drawn into the hands.
    pub fn draw(two_handed: bool, axe: bool, fists: bool) -> &'static str {
        match (fists, two_handed, axe) {
            (true, _, _) => "h2h_equip",
            (_, true, true) => "2hw_equip",
            (_, true, false) => "2hc_equip",
            _ => "1hm_equip",
        }
    }

    /// A spell held ready, and its release, in the right hand or the left.
    pub fn spell_ready(left: bool) -> &'static str {
        if left { "mlh_readyloop" } else { "mrh_readyloop" }
    }
    pub fn spell_release(left: bool) -> &'static str {
        if left { "mlh_release" } else { "mrh_release" }
    }

    pub const SHIELD_BLOCK: &str = "shd_blockidle";

    /// A Utility tool's stroke (crate::tools), by its ability id: the pick, the axe and the
    /// hammers come down overhead, the knife cuts down across, a herb is reached for and pulled
    /// with an open hand, the pole is cast overhead with both hands. None: no tool (a swing).
    pub fn tool_swing(tool: u8, two_handed: bool) -> Option<&'static str> {
        use crate::action_systems::Ability as A;
        let a = A::from_id(u32::from(tool)).filter(|a| a.is_tool())?;
        Some(match a {
            A::SkinningKnife => "1hm_attackrightdiagonal",
            A::Herbalism => "mrh_release",
            A::FishingPole if two_handed => "2hw_attackpower",
            _ => "1hm_attackpower",
        })
    }
}

/// A two-handed axe or hammer swings as the tracks' `2hw` (warhammer) set; swords and the rest as
/// `2hm` (`ItemSubclassWeapon` 1 two-handed axe, 5 two-handed mace).
pub fn is_axe_or_hammer(subclass: u8) -> bool {
    matches!(subclass, 1 | 5)
}

/// What the hands hold and are doing this frame: everything [`compose`] needs.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct HandsNow {
    pub two: bool,
    pub fists: bool,
    pub axe: bool,
    /// A second weapon in the left hand (not a two-hander's other grip).
    pub dual: bool,
    pub right_spell: bool,
    pub left: Hand,
    /// Seconds (the idles loop on it).
    pub t: f32,
    /// How far the weapon is drawn (0 just raised, 1 at the ready).
    pub drawn: f32,
    /// How far into the sprint's carry (0 at the ready, 1 at the hip).
    pub sprint: f32,
    /// How far the shield is up (0..1).
    pub block: f32,
    /// The right hand's swing: from the right, how far through (0..1), and its length (seconds,
    /// [`swing_secs`]).
    pub swing: Option<(bool, f32, f32)>,
    /// The left hand's swing (a dual wielder's off hand): from the right, how far, how long.
    pub left_swing: Option<(bool, f32, f32)>,
    /// Each hand's cast (0 right, 1 left), how far through (0..1).
    pub cast: [Option<f32>; 2],
    /// The hand-made keys (crate::melee_pose) instead of the tracks' clips.
    pub handmade: bool,
    /// Utility's tool in the hands (its ability id, crate::action_systems), 0 for none: its
    /// swings are the tool's strokes ([`names::tool_swing`]).
    pub tool: u8,
    /// How far Utility's hands are at rest (crate::hands `relaxed`): 0 the fists raised, 1
    /// lowered and open with no guard up; eased between ([`relax_step`]) so they never snap.
    pub relaxed: f32,
}

impl HandsNow {
    /// Fists with the left hand free: the left fist is up and punches in turn (The tracks'
    /// `h2h_attackleft` is the left fist's punch).
    pub fn left_fist(&self) -> bool {
        self.fists && self.left == Hand::Empty
    }
    /// Both hands on a two-hander's hilt: never while either hand holds a spell. The tracks put the
    /// two-hander away when a spell is readied; the hands then cast apart, each its own.
    pub fn gripped_two(&self) -> bool {
        self.two && !self.right_spell && !matches!(self.left, Hand::Spell { .. })
    }
}

/// Which hand a clip's strike is measured on.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Side {
    Right,
    Left,
}

/// Where each one-shot clip strikes (its own seconds): from the end of its wind-up (the weapon
/// drawn back and still) to the end of its sweep across the view, and the hand that strikes.
/// Read off the clips' own paths: the grip barely moves before, crosses 30 to 50 inches within,
/// and slows after (`clip_strikes_are_the_fast_part`). The tracks key its strikes at 30 frames a
/// second, a sword's crossing 20 inches between two keys; played by the clip's clock that is a
/// hand teleporting across the screen, so the strike is played by the distance it covers
/// instead ([`play_strike`]).
pub fn strike(name: &str) -> Option<(f32, f32, Side)> {
    use Side::*;
    Some(match name {
        "1hm_attackright" => (0.300, 0.500, Right),
        "1hm_attackleft" => (0.267, 0.533, Right),
        "2hm_attackright" => (0.500, 0.867, Right),
        "2hm_attackleft" => (0.633, 0.900, Right),
        "2hw_attackright" => (0.500, 0.900, Right),
        "2hw_attackleft" => (0.633, 0.900, Right),
        "h2h_attackright" => (0.433, 0.567, Right),
        "h2h_attackleft" => (0.533, 0.667, Left),
        // A spell's release: the hand thrust out (10 inches between two keys).
        "mrh_release" => (0.067, 0.167, Right),
        "mlh_release" => (0.067, 0.133, Left),
        _ => return None,
    })
}

/// How a one-shot is paced: its wind-up and strike (seconds, the rest recovers) and the bounds of
/// its whole length.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Pace {
    pub wind: f32,
    pub strike: f32,
    pub min: f32,
    pub max: f32,
}

/// A one-hander (and a dual wielder's off hand): the blade across the middle of the screen
/// 0.2 s after the press, the whole swing about half a second.
pub const LIGHT: Pace = Pace {
    wind: 0.12,
    strike: 0.16,
    min: 0.4,
    max: 0.58,
};
/// Fists: quicker still.
pub const FISTS: Pace = Pace {
    wind: 0.09,
    strike: 0.12,
    min: 0.36,
    max: 0.48,
};
/// A two-hander: a real wind-up, the weight felt, under a second.
pub const HEAVY: Pace = Pace {
    wind: 0.26,
    strike: 0.2,
    min: 0.75,
    max: 1.0,
};
/// A spell's release ([`crate::melee_pose::CAST_SECS`] long).
pub const CAST: Pace = Pace {
    wind: 0.06,
    strike: 0.09,
    min: 0.3,
    max: 0.6,
};

/// The share of the weapon's recovery between swings (the server's `swingMs`) a swing takes on
/// screen: each swing is over, back at the ready, a moment before the next may start. The tracks'
/// own clips (1.5 s for a sword, 2 s for a two-hander) played at 0.8 ran past the next swing,
/// which then started over from the ready pose: the hand jumped back across the screen.
pub const GATE_SHARE: f32 = 0.92;

impl Pace {
    /// The wind-up and strike for a one-shot `total` seconds long, shortened in proportion if
    /// it is too short for them (the recovery keeps at least 30%).
    pub fn beats(&self, total: f32) -> (f32, f32) {
        let k = (total * 0.7 / (self.wind + self.strike)).min(1.0);
        (self.wind * k, self.strike * k)
    }
}

/// The pace a hand swings at.
pub fn pace(now: &HandsNow, left: bool) -> Pace {
    match (left, now.gripped_two(), now.fists) {
        (false, true, _) => HEAVY,
        (false, _, true) => FISTS,
        _ => LIGHT,
    }
}

/// Seconds a swing takes on screen: the right hand's (`left` false) or a dual wielder's off
/// hand's, the weapon's recovery between swings being `gate` seconds.
pub fn swing_secs(now: &HandsNow, left: bool, gate: f32) -> f32 {
    let p = pace(now, left);
    (gate * GATE_SHARE).clamp(p.min, p.max).min(gate)
}

fn smooth(x: f32) -> f32 {
    let x = x.clamp(0.0, 1.0);
    x * x * (3.0 - 2.0 * x)
}

/// An ease with a steady middle: speeding up over the first quarter, steady, slowing over the
/// last (a trapezoid of speed, its top a third above the average; a smoothstep's is half).
pub fn steady(u: f32) -> f32 {
    const A: f32 = 0.25;
    let u = u.clamp(0.0, 1.0);
    let v = 1.0 / (1.0 - A);
    if u < A {
        v * u * u / (2.0 * A)
    } else if u <= 1.0 - A {
        v * (u - A / 2.0)
    } else {
        1.0 - v * (1.0 - u) * (1.0 - u) / (2.0 * A)
    }
}

/// A clip at `secs` (its own clock), fitted to our view.
fn at_secs(track: &Track, secs: f32) -> (Grip, Grip) {
    at(track, secs / track.duration.max(1e-6))
}

/// Inches a blade's turn counts for in a strike's distance (the turn of a point this far up it).
const TURN_LEVER: f32 = 10.0;

/// The clip time `u` (0..1) of the way along the path `side`'s hand covers from `c0` to `c1`
/// (seconds), its place and its blade's turn: the hand moves at an even pace along the tracks' path
/// rather than in its lurches from key to key.
pub fn along_path(track: &Track, side: Side, c0: f32, c1: f32, u: f32) -> f32 {
    let mut ts = vec![c0];
    ts.extend(
        track
            .frames
            .iter()
            .map(|f| f.0)
            .filter(|&t| t > c0 && t < c1),
    );
    ts.push(c1);
    let pick = |t: f32| {
        let (r, l) = at_secs(track, t);
        if side == Side::Right { r } else { l }
    };
    let mut cum = vec![0.0f32];
    let mut prev = pick(c0);
    for &t in &ts[1..] {
        let g = pick(t);
        let d = g.at.distance(prev.at) + TURN_LEVER * g.blade.distance(prev.blade);
        cum.push(cum.last().copied().unwrap_or(0.0) + d);
        prev = g;
    }
    let total = cum.last().copied().unwrap_or(0.0);
    if total < 1e-4 {
        return c0 + (c1 - c0) * u;
    }
    let want = u.clamp(0.0, 1.0) * total;
    let i = cum.partition_point(|&c| c < want).clamp(1, cum.len() - 1);
    let span = (cum[i] - cum[i - 1]).max(1e-6);
    let s = ((want - cum[i - 1]) / span).clamp(0.0, 1.0);
    ts[i - 1] + (ts[i] - ts[i - 1]) * s
}

/// The clip seconds a recovery advances per second: the follow-through drifts on, slower than
/// The tracks' (whose follow-through swings the weapon down behind the shoulder), while it fades
/// back into the ready pose.
const FOLLOW: f32 = 0.3;

/// A one-shot clip `k` (0..1) of the way through its `total` seconds, played in three beats
/// ([`Pace::beats`]): wound up from the ready pose into the clip's wound pose, the strike along
/// its path at an even pace ([`along_path`]), and the recovery fading back into the ready pose.
/// A clip without a known strike plays as a plain one-shot.
pub fn play_strike(
    track: &Track,
    name: &str,
    k: f32,
    total: f32,
    pace: Pace,
    ready: (Grip, Grip),
) -> (Grip, Grip) {
    let Some((c0, c1, side)) = strike(name) else {
        return one_shot(track, k, ready);
    };
    let (wind, strike) = pace.beats(total);
    let s = k.clamp(0.0, 1.0) * total;
    let rec = (total - wind - strike).max(1e-3);
    let (c, w) = if s < wind {
        // Straight from the ready into the clip's wound pose: a clip's own wind-up (half a
        // second for a two-hander's) squeezed into this one swept the hands about.
        (c0, smooth(s / wind))
    } else if s < wind + strike {
        let u = steady((s - wind) / strike);
        (along_path(track, side, c0, c1, u), 1.0)
    } else {
        let u = ((s - wind - strike) / rec).min(1.0);
        ((c1 + u * rec * FOLLOW).min(track.duration), 1.0 - smooth(u))
    };
    let (r, l) = at_secs(track, c);
    (mix(&ready.0, &r, w), mix(&ready.1, &l, w))
}

/// A pair of hands seen in the mirror: the left hand's twin of the right's and back.
fn mirror_pair(p: (Grip, Grip)) -> (Grip, Grip) {
    use crate::melee_pose::mirrored;
    (mirrored(&p.1), mirrored(&p.0))
}

/// Seconds Utility's hands take to go between at rest and the fists raised.
pub const RELAX_SECS: f32 = 0.35;

/// One frame's step of the hands' rest ([`HandsNow::relaxed`]) toward at rest (`relaxed`) or
/// raised, over [`RELAX_SECS`].
pub fn relax_step(now: f32, relaxed: bool, dt: f32) -> f32 {
    let goal = if relaxed { 1.0 } else { 0.0 };
    now + (goal - now).clamp(-dt / RELAX_SECS, dt / RELAX_SECS)
}

/// The tracks' draw clips play from the moment the weapon leaves its sheath (`weaponDraw`): before
/// it the hand reaches empty for the hip, and with the weapon already in it the sword appeared in
/// the middle of the face. This many of the clip's seconds a second.
pub const DRAW_RATE: f32 = 1.6;
/// The share of the draw spent coming up from out of sight below into the clip.
const DRAW_IN: f32 = 0.4;
/// The share of the draw, at its end, spent settling into the idle.
const DRAW_OUT: f32 = 0.4;

/// Where a draw clip starts (seconds): when the weapon leaves its sheath.
fn draw_from(track: &Track) -> f32 {
    track.event("weaponDraw").unwrap_or(0.0) * track.duration
}

/// Seconds the draw takes for what the hands hold.
pub fn draw_secs(now: &HandsNow, clips: &mut HandAnimClips) -> f32 {
    let draw = (!now.handmade)
        .then(|| clips.get(names::draw(now.gripped_two(), now.axe, now.fists)))
        .flatten();
    draw.as_ref().map_or(crate::melee_pose::DRAW_SECS, |tr| {
        ((tr.duration - draw_from(tr)) / DRAW_RATE).clamp(0.3, 0.55)
    })
}

/// The hands' grips this frame (the eye's frame, before the view's sway): the right hand's, and
/// the left's, `None` when it is down out of sight.
pub fn compose(now: &HandsNow, clips: &mut HandAnimClips) -> (Grip, Option<Grip>) {
    use crate::melee_pose::{self as pose, Motion};
    let (two, axe, fists, dual, t) = (now.gripped_two(), now.axe, now.fists, now.dual, now.t);
    let mut clip = |name: &'static str| if now.handmade { None } else { clips.get(name) };
    // Lowered by the sprint's carry, or all the way at rest out of a fight (Utility's relaxed
    // hands), the fingers open rather than fisted.
    let lower = |g: Grip| {
        let raised = mix(&g, &pose::lowered(&g), now.sprint);
        if now.relaxed <= 0.0 {
            return raised;
        }
        let rest = Grip {
            hand: crate::hand_shape::HandShape::OPEN,
            ..pose::lowered(&pose::lowered(&g))
        };
        mix(&raised, &rest, smooth(now.relaxed.min(1.0)))
    };
    // The ready pose: the tracks' idle for what the hands hold, looping.
    let made = pose::grip(Motion::Ready, two, 0.0);
    let (ready_r, ready_l) = clip(names::idle(two, axe, dual, fists))
        .map(|tr| looped(&tr, t))
        .unwrap_or((made, pose::mirrored(&made)));
    let (mut r, mut l) = (lower(ready_r), lower(ready_l));
    // Drawn on V: up from out of sight below into the tracks' draw as the weapon leaves the sheath,
    // then into the idle; else the hand-made rise.
    if now.drawn < 1.0 {
        match clip(names::draw(two, axe, fists)) {
            Some(tr) => {
                let from = draw_from(&tr);
                let (dr, dl) = at_secs(&tr, from + (tr.duration - from) * now.drawn);
                let up = smooth(now.drawn / DRAW_IN);
                let stowed_l = pose::mirrored(&pose::drawn(&pose::mirrored(&l), 0.0));
                let (dr, dl) = (mix(&pose::drawn(&r, 0.0), &dr, up), mix(&stowed_l, &dl, up));
                let w = smooth((now.drawn - (1.0 - DRAW_OUT)) / DRAW_OUT);
                r = mix(&dr, &r, w);
                l = mix(&dl, &l, w);
            }
            None => r = pose::drawn(&r, now.drawn),
        }
    }
    // A swing: the tracks' attack for the side and weapon, paced in three beats out of and back into
    // the ready pose (crate::hand_anim::play_strike). Fists with the left hand busy punch with the
    // right every time.
    if let Some((from_right, k, secs)) = now.swing {
        let from_right = from_right || (fists && !now.left_fist());
        let name = names::tool_swing(now.tool, two)
            .unwrap_or_else(|| names::swing(from_right, two, axe, fists));
        match clip(name) {
            Some(tr) => {
                let (sr, sl) = play_strike(&tr, name, k, secs, pace(now, false), (r, l));
                r = sr;
                if two || now.left_fist() {
                    l = sl;
                }
            }
            None => r = pose::grip(Motion::Swing { from_right }, two, k),
        }
    }
    // A spell in a hand: the tracks' ready loop, its release on a cast; else the hand-made.
    let mut spell = |left: bool| -> Grip {
        let side = usize::from(left);
        let pick = |pair: (Grip, Grip)| if left { pair.1 } else { pair.0 };
        let ready = clip(names::spell_ready(left)).map(|tr| looped(&tr, t));
        let cast = now.cast[side].filter(|k| *k < 1.0);
        match (ready, cast) {
            (Some(rd), Some(k)) => {
                let name = names::spell_release(left);
                match clip(name) {
                    Some(tr) => pick(play_strike(&tr, name, k, pose::CAST_SECS, CAST, rd)),
                    None => pick(rd),
                }
            }
            (Some(rd), None) => pick(rd),
            (None, k) => {
                let s = pose::spell_grip(k);
                if left { pose::mirrored(&s) } else { s }
            }
        }
    };
    let right = if now.right_spell { spell(false) } else { r };
    let left = match now.left {
        // A two-hander: the left hand on the hilt below the right. (The tracks' left grip is its
        // `Shield` node, on the back of the forearm for a shield: under a two-hander it sat 6 to
        // 20 inches off the hilt, out past the arm's reach.)
        _ if two => Some(Grip {
            at: pose::off_hand(&right, true, HILT_SPAN),
            hand: if now.handmade {
                right.hand.mirrored()
            } else {
                l.hand
            },
            ..right
        }),
        // A second weapon: its own swing is the main hand's seen in a mirror (The tracks'
        // `mlh_1hm_attackforward`, played before, swings the right hand's weapon: the left one
        // barely stirred).
        Hand::Weapon => Some(match now.left_swing {
            Some((from_right, k, secs)) => {
                // The left hand's swing from the right is the right hand's from the left, mirrored.
                let name = names::swing(!from_right, false, false, false);
                match clip(name) {
                    Some(tr) => {
                        let (ml, _) = play_strike(&tr, name, k, secs, LIGHT, mirror_pair((r, l)));
                        pose::mirrored(&ml)
                    }
                    None => pose::mirrored(&pose::grip(Motion::Swing { from_right }, false, k)),
                }
            }
            None if !now.handmade => l,
            None => pose::mirrored(&made),
        }),
        Hand::Shield => {
            let block = clip(names::SHIELD_BLOCK)
                .map(|tr| looped(&tr, t).1)
                .unwrap_or_else(|| pose::shield_grip(1.0));
            // At rest low on the arm and out of the way, turned as it is to block (the hand-made
            // low pose's own turn put the wrist ahead of the strap, the hand bent 148 degrees);
            // the block raises it as the tracks'.
            let low = Grip {
                at: pose::shield_grip(0.0).at,
                ..block
            };
            Some(mix(&low, &block, now.block))
        }
        Hand::Spell { .. } => Some(spell(true)),
        // Fists: the left fist up in the tracks' guard.
        Hand::Empty if now.left_fist() && !now.handmade => Some(l),
        Hand::Empty => None,
    };
    (right, left)
}

/// Where a wrist sits from its grip, in the grip's frame (inches along the blade, the edge, and
/// the blade across the edge), as the tracks' hands hold each thing: constant through every clip
/// (the grip is a node on the hand), and different by what the hand holds. A sword's hilt lies
/// across the palm, the wrist behind it and 2.5 inches down it.
pub mod wrist {
    use bevy::math::Vec3;
    pub const WEAPON: Vec3 = Vec3::new(-2.50, -3.40, -0.41);
    pub const TWO_HANDED: Vec3 = Vec3::new(0.21, -3.27, -1.22);
    pub const FIST: Vec3 = Vec3::new(-1.14, -3.26, -1.22);
    pub const SPELL: Vec3 = Vec3::new(-0.43, -3.85, -1.33);
    /// The left hand's grip in the tracks' clips is its `Shield` node (a spell, a fist, at rest).
    pub const LEFT: Vec3 = Vec3::new(0.0, 4.03, -1.37);
    pub const SHIELD: Vec3 = Vec3::new(-1.25, 3.83, -0.66);
    /// In the left hand: the right hand's seen in a mirror.
    pub fn mirrored(v: Vec3) -> Vec3 {
        Vec3::new(v.x, v.y, -v.z)
    }
}

/// How the hands hold what they hold, for the arms' reach (crate::mw2::viewmodel::hold_melee).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Hold {
    /// Both hands on one hilt: the left's grip goes below the right's, wherever it ended.
    pub two_handed: bool,
    /// Where each wrist sits from its grip (crate::hand_anim::wrist).
    pub wrists: [Vec3; 2],
    /// Each hand holds no hilt (a spell, a bare fist, a shield's strap): it may turn on its own
    /// length to spare the forearm.
    pub free: [bool; 2],
}

/// How the hands hold what they hold this frame.
pub fn hold(now: &HandsNow) -> Hold {
    Hold {
        two_handed: now.gripped_two(),
        wrists: wrist_at(now),
        free: [
            now.right_spell || now.fists,
            !now.gripped_two() && now.left != Hand::Weapon,
        ],
    }
}

/// Where each wrist sits from its grip ([`wrist`]), by what the hands hold; zero for the
/// hand-made keys, which keep the gun's grip.
pub fn wrist_at(now: &HandsNow) -> [Vec3; 2] {
    if now.handmade {
        return [Vec3::ZERO; 2];
    }
    let right = match (now.right_spell, now.fists, now.gripped_two()) {
        (true, _, _) => wrist::SPELL,
        (_, true, _) => wrist::FIST,
        (_, _, true) => wrist::TWO_HANDED,
        _ => wrist::WEAPON,
    };
    let left = match now.left {
        _ if now.gripped_two() => wrist::mirrored(wrist::TWO_HANDED),
        Hand::Weapon => wrist::mirrored(wrist::WEAPON),
        Hand::Shield => wrist::SHIELD,
        _ => wrist::LEFT,
    };
    [right, left]
}

/// A two-hander's hands, this far apart along the hilt (inches).
pub const HILT_SPAN: f32 = 4.5;

/// Seconds a jump in the hands' pose is eased over.
pub const SETTLE_SECS: f32 = 0.15;
/// A hand moving faster than this (inches a second) or turning faster (radians a second) has
/// jumped: the fastest strike's hand moves at about 410 (`no_hand_or_weapon_pops_between_frames`).
const SETTLE_SPEED: f32 = 600.0;
const SETTLE_TURN: f32 = 40.0;

/// Eases over any jump in the hands' pose: a weapon swapped in hand, a spell readied, a swing
/// cut short. The pose asked for is followed exactly except for [`SETTLE_SECS`] after a jump,
/// eased from where the hands were.
#[derive(Clone, Copy, Debug, Default)]
pub struct Settle {
    /// The last frame: when, the pose asked for, and the pose shown.
    last: Option<(f32, (Grip, Option<Grip>), (Grip, Option<Grip>))>,
    /// The jump being eased: when, and from where.
    from: Option<(f32, (Grip, Option<Grip>))>,
}

impl Settle {
    pub fn apply(&mut self, t: f32, pose: (Grip, Option<Grip>)) -> (Grip, Option<Grip>) {
        if let Some((lt, asked, shown)) = self.last {
            let dt = (t - lt).max(1e-3);
            let jumped = |a: &Grip, b: &Grip| {
                a.at.distance(b.at) > (SETTLE_SPEED * dt).max(2.0)
                    || a.blade.angle_between(b.blade) > (SETTLE_TURN * dt).max(0.3)
            };
            let popped = jumped(&asked.0, &pose.0)
                || match (asked.1, pose.1) {
                    (Some(a), Some(b)) => jumped(&a, &b),
                    (None, Some(_)) => true,
                    _ => false,
                };
            if popped {
                self.from = Some((t, shown));
            }
        }
        let shown = match self.from {
            Some((t0, from)) if t - t0 < SETTLE_SECS => {
                let w = smooth((t - t0) / SETTLE_SECS);
                let left = match (from.1, pose.1) {
                    (Some(a), Some(b)) => Some(mix(&a, &b, w)),
                    // A hand coming up rises from where an empty hand hangs, out of sight.
                    (None, Some(b)) => {
                        let hanging = Grip {
                            at: crate::melee_pose::off_hand(&pose.0, false, HILT_SPAN),
                            ..b
                        };
                        Some(mix(&hanging, &b, w))
                    }
                    (_, b) => b,
                };
                (mix(&from.0, &pose.0, w), left)
            }
            _ => pose,
        };
        self.last = Some((t, pose, shown));
        shown
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn utility_hands_ease_between_rest_and_raised_fists() {
        let dt = 1.0 / 60.0;
        let mut k = 0.0;
        let mut frames = 0;
        while k < 1.0 {
            let next = super::relax_step(k, true, dt);
            assert!(next - k <= dt / super::RELAX_SECS + 1e-6, "no snap");
            k = next;
            frames += 1;
        }
        assert_eq!(k, 1.0);
        assert!(frames >= (super::RELAX_SECS / dt) as usize - 1);
        let back = super::relax_step(1.0, false, dt);
        assert!(back < 1.0 && back > 0.9, "the fists rise gradually: {back}");
    }

    use super::*;

    #[test]
    fn a_one_shot_blends_in_and_out() {
        assert_eq!(one_shot_weight(0.0), 0.0);
        assert!((one_shot_weight(0.5) - 1.0).abs() < 1e-6);
        assert!(one_shot_weight(1.0) < 1e-6);
        assert!(one_shot_weight(BLEND_IN * 0.5) > 0.0 && one_shot_weight(BLEND_IN * 0.5) < 1.0);
    }

    #[test]
    #[cfg_attr(not(aw_hand_anim_tracks), ignore = "needs the hand tracks (private-assets.md)")]
    fn every_clip_the_hands_ask_for_is_embedded() {
        let mut clips = HandAnimClips::default();
        let mut wanted = vec![
            names::SHIELD_BLOCK,
            names::spell_ready(true),
            names::spell_ready(false),
            names::spell_release(true),
            names::spell_release(false),
        ];
        for two in [false, true] {
            for axe in [false, true] {
                for fists in [false, true] {
                    wanted.push(names::idle(two, axe, false, fists));
                    wanted.push(names::draw(two, axe, fists));
                    for right in [false, true] {
                        wanted.push(names::swing(right, two, axe, fists));
                    }
                }
            }
        }
        wanted.push(names::idle(false, false, true, false));
        for n in wanted {
            assert!(clips.get(n).is_some(), "{n} is not embedded");
        }
    }

    #[test]
    #[cfg_attr(not(aw_hand_anim_tracks), ignore = "needs the hand tracks (private-assets.md)")]
    fn an_idle_track_holds_the_blade_up_in_view() {
        let mut clips = HandAnimClips::default();
        let (r, _) = looped(&clips.get("1hm_idle").unwrap(), 0.3);
        assert!(r.at.z < 0.0 && r.blade.y > 0.3); // ahead of the eye, point up
    }

    /// The grip's mean speed (inches a clip second) between `a` and `b`, of `side`'s hand.
    fn mean_speed(tr: &Track, side: Side, a: f32, b: f32) -> f32 {
        let n = 40;
        let pick = |t: f32| {
            let (r, l) = at_secs(tr, t);
            if side == Side::Right { r.at } else { l.at }
        };
        let mut d = 0.0;
        for i in 0..n {
            let t0 = a + (b - a) * i as f32 / n as f32;
            let t1 = a + (b - a) * (i + 1) as f32 / n as f32;
            d += pick(t0).distance(pick(t1));
        }
        d / (b - a).max(1e-4)
    }

    #[test]
    #[cfg_attr(not(aw_hand_anim_tracks), ignore = "needs the hand tracks (private-assets.md)")]
    fn clip_strikes_are_the_fast_part() {
        let mut clips = HandAnimClips::default();
        let names = [
            "1hm_attackright",
            "1hm_attackleft",
            "2hm_attackright",
            "2hm_attackleft",
            "2hw_attackright",
            "2hw_attackleft",
            "h2h_attackright",
            "h2h_attackleft",
            "mrh_release",
            "mlh_release",
        ];
        for name in names {
            let tr = clips.get(name).unwrap();
            let (c0, c1, side) = strike(name).unwrap();
            assert!(0.0 < c0 && c0 < c1 && c1 < tr.duration, "{name}");
            // The strike is at least twice as fast as the wind-up before it.
            let inside = mean_speed(&tr, side, c0, c1);
            let before = mean_speed(&tr, side, 0.0, c0);
            assert!(
                inside > before * 2.0,
                "{name}: {inside:.0} against {before:.0}"
            );
        }
        // Every swing the hands ask for has its strike marked.
        for two in [false, true] {
            for axe in [false, true] {
                for fists in [false, true] {
                    for right in [false, true] {
                        let n = names::swing(right, two, axe, fists);
                        assert!(strike(n).is_some(), "{n}");
                    }
                }
            }
        }
    }

    #[test]
    #[cfg_attr(not(aw_hand_anim_tracks), ignore = "needs the hand tracks (private-assets.md)")]
    fn a_strike_moves_at_an_even_pace_along_the_tracks_path() {
        let mut clips = HandAnimClips::default();
        let tr = clips.get("1hm_attackright").unwrap();
        let (c0, c1, side) = strike("1hm_attackright").unwrap();
        // The tracks' own keys: one step of 20 inches between two keys 1/30 s apart.
        let key = at_secs(&tr, 0.3)
            .0
            .at
            .distance(at_secs(&tr, 1.0 / 30.0 + 0.3).0.at);
        assert!(key > 15.0, "{key}");
        // Along the path, evenly: 20 equal parts, none more than twice another.
        let parts: Vec<f32> = (0..20)
            .map(|i| {
                let t = |u: f32| along_path(&tr, side, c0, c1, u);
                let (a, b) = (t(i as f32 / 20.0), t((i + 1) as f32 / 20.0));
                let (ga, gb) = (at_secs(&tr, a).0, at_secs(&tr, b).0);
                ga.at.distance(gb.at) + TURN_LEVER * ga.blade.distance(gb.blade)
            })
            .collect();
        let (lo, hi) = parts
            .iter()
            .fold((f32::MAX, 0.0f32), |(lo, hi), &p| (lo.min(p), hi.max(p)));
        assert!(hi < lo * 2.0 + 0.5, "{parts:?}");
        assert_eq!(along_path(&tr, side, c0, c1, 0.0), c0);
        assert!((along_path(&tr, side, c0, c1, 1.0) - c1).abs() < 1e-4);
        // The ease: still at both ends, steady through the middle.
        assert_eq!((steady(0.0), steady(1.0)), (0.0, 1.0));
        assert!((steady(0.5) - 0.5).abs() < 1e-6);
        assert!(steady(0.1) < 0.1 && steady(0.9) > 0.9);
    }

    #[test]
    fn swings_take_the_weapons_pace_and_end_before_the_next() {
        // The server's swing recoveries (server/src/fusion/src/Melee.cpp): dagger, fist weapon,
        // sword, axe, mace; staff, two-handed sword, polearm, axe, mace; no weapon.
        let one = |two: bool, fists: bool| HandsNow {
            two,
            fists,
            ..Default::default()
        };
        for (gate, two, fists, lo, hi) in [
            (0.45, false, false, 0.4, 0.45),
            (0.4, false, false, 0.4, 0.4),
            (0.6, false, false, 0.5, 0.58),
            (0.7, false, false, 0.55, 0.58),
            (0.8, false, false, 0.55, 0.58),
            (0.9, true, false, 0.75, 0.9),
            (1.0, true, false, 0.85, 0.95),
            (1.25, true, false, 0.95, 1.0),
            (0.5, false, true, 0.4, 0.48),
        ] {
            let s = swing_secs(&one(two, fists), false, gate);
            assert!(
                s >= lo - 1e-4 && s <= hi + 1e-4 && s <= gate,
                "{gate} {two} {fists}: {s}"
            );
        }
        // The off hand swings light.
        assert!(swing_secs(&one(true, false), true, 0.6) <= 0.58);
        // Beats fit any length, the recovery keeping its share.
        let (w, s) = LIGHT.beats(0.3);
        assert!(w + s <= 0.3 * 0.7 + 1e-4);
        assert_eq!(LIGHT.beats(0.55), (LIGHT.wind, LIGHT.strike));
    }

    #[test]
    fn a_swing_starts_and_ends_at_the_ready_and_strikes_between() {
        let mut clips = HandAnimClips::default();
        let now = HandsNow {
            drawn: 1.0,
            ..Default::default()
        };
        let ready = compose(&now, &mut clips);
        for from_right in [true, false] {
            let mut at = |k: f32| {
                compose(
                    &HandsNow {
                        swing: Some((from_right, k, 0.55)),
                        ..now
                    },
                    &mut clips,
                )
                .0
            };
            assert!(at(0.0).at.distance(ready.0.at) < 1e-3);
            assert!(at(1.0).at.distance(ready.0.at) < 1e-3);
            // Mid-strike the fist is across on the other side from where it was wound up.
            let (wind, strike) = LIGHT.beats(0.55);
            let (a, b) = (at(wind / 0.55), at((wind + strike) / 0.55));
            assert!((a.at.x - b.at.x).abs() > 15.0, "{a:?} {b:?}");
            assert_eq!(a.at.x > b.at.x, from_right);
        }
    }

    #[test]
    fn fists_punch_in_turn_and_the_off_hand_swings_its_own_weapon() {
        let mut clips = HandAnimClips::default();
        let fists = HandsNow {
            fists: true,
            left: Hand::Empty,
            drawn: 1.0,
            ..Default::default()
        };
        // The left fist is up, and on the left punch it crosses to the right.
        let (_, rest) = compose(&fists, &mut clips);
        let rest = rest.expect("the left fist up");
        let (wind, strike) = FISTS.beats(0.46);
        let (_, punch) = compose(
            &HandsNow {
                swing: Some((false, (wind + strike) / 0.46, 0.46)),
                ..fists
            },
            &mut clips,
        );
        assert!(punch.unwrap().at.x > rest.at.x + 12.0, "{rest:?} {punch:?}");
        // With a spell in the left hand the right fist does every punch.
        let busy = HandsNow {
            left: Hand::Spell {
                action: 0,
                spell_id: 0,
            },
            ..fists
        };
        let (r_left, _) = compose(
            &HandsNow {
                swing: Some((false, wind / 0.46, 0.46)),
                ..busy
            },
            &mut clips,
        );
        let (r_right, _) = compose(
            &HandsNow {
                swing: Some((true, wind / 0.46, 0.46)),
                ..busy
            },
            &mut clips,
        );
        assert!(r_left.at.distance(r_right.at) < 1e-3);
        // A dual wielder's left weapon sweeps across the view (The tracks' `mlh_1hm_attackforward`
        // stirred it 4 inches).
        let dual = HandsNow {
            dual: true,
            left: Hand::Weapon,
            drawn: 1.0,
            ..Default::default()
        };
        let (wind, strike) = LIGHT.beats(0.55);
        let mut left_at = |k: f32| {
            compose(
                &HandsNow {
                    left_swing: Some((false, k, 0.55)),
                    ..dual
                },
                &mut clips,
            )
            .1
            .unwrap()
        };
        let (a, b) = (left_at(wind / 0.55), left_at((wind + strike) / 0.55));
        assert!(b.at.x - a.at.x > 15.0, "{a:?} {b:?}");
    }

    #[test]
    fn a_draw_rises_from_out_of_sight_and_never_before_the_face() {
        let mut clips = HandAnimClips::default();
        for (two, axe, fists) in [
            (false, false, false),
            (true, false, false),
            (true, true, false),
        ] {
            let now = HandsNow {
                two,
                axe,
                fists,
                ..Default::default()
            };
            let start = compose(&now, &mut clips).0;
            assert!(start.at.y / -start.at.z < -0.6, "{two} {axe}: {start:?}"); // below the view
            for i in 0..=20 {
                let g = compose(
                    &HandsNow {
                        drawn: i as f32 / 20.0,
                        ..now
                    },
                    &mut clips,
                )
                .0;
                // Never within a foot of the eye in the middle of the view (1hm_equip's first
                // key had the sword 9 inches before the face).
                let middle = (g.at.x / -g.at.z).abs() < 0.3 && (g.at.y / -g.at.z).abs() < 0.3;
                assert!(!(middle && g.at.length() < 12.0), "{i}: {g:?}");
            }
            let secs = draw_secs(&now, &mut clips);
            assert!((0.3..=0.55).contains(&secs), "{secs}");
        }
    }

    #[test]
    fn a_jump_in_the_pose_is_eased_and_a_path_is_followed() {
        let g = |x: f32| Grip {
            at: Vec3::new(x, 0.0, -15.0),
            blade: Vec3::Y,
            edge: Vec3::NEG_Z,
            hand: crate::hand_shape::HandShape::FIST,
        };
        let mut s = Settle::default();
        // A steady path is followed exactly.
        for i in 0..30 {
            let t = i as f32 / 60.0;
            let x = t * 120.0;
            assert_eq!(s.apply(t, (g(x), None)).0.at.x, x);
        }
        // A jump of 22 inches is eased over SETTLE_SECS, starting where the hand was.
        let t0 = 0.5;
        let from = s.apply(t0, (g(80.0), None)).0.at.x;
        assert!((from - 58.0).abs() < 1e-3, "{from}");
        let mut last = from;
        let mut t = t0;
        while t < t0 + SETTLE_SECS + 0.05 {
            t += 1.0 / 60.0;
            let x = s.apply(t, (g(80.0), None)).0.at.x;
            assert!(x >= last - 1e-4 && x - last < 8.0, "{t} {x}");
            last = x;
        }
        assert_eq!(last, 80.0);
        // A hand coming up rises from below rather than appearing.
        let up = s.apply(t + 1.0 / 60.0, (g(80.0), Some(g(-8.0)))).1.unwrap();
        assert!(up.at.y < -10.0, "{up:?}");
    }
}

#[cfg(test)]
mod dump {
    #[test]
    #[ignore]
    fn print_swing_path() {
        let mut clips = super::HandAnimClips::default();
        for name in [
            "1hm_idle",
            "1hm_attackright",
            "1hm_attackleft",
            "shd_blockidle",
        ] {
            let tr = clips.get(name).unwrap();
            for i in 0..=10 {
                let k = i as f32 / 10.0;
                let (r, l) = super::at(&tr, k);
                println!(
                    "{name} {k:.1} right at {:?} blade {:?} | left at {:?}",
                    r.at, r.blade, l.at
                );
            }
        }
    }
}
