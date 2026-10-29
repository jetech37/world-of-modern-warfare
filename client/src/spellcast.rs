//! Spells from the hands, the two-hand way (the server's `fusion/Spellcasting.h` and
//! `game/Fusion/FusionSpellcasting.cpp`): a spell readied into a hand (crate::hands) charges while
//! that hand's mouse button is held, keeps its charge for as long as the button stays down, and
//! casts at the crosshair when the button comes up. Released early it fizzles: nothing is cast and
//! nothing spent. Both hands holding the same spell and let go together cast it once, stronger
//! (+50%, a 50% wider area). No target is selected first: the release carries the crosshair's ray,
//! unit and point, and the server casts it at once, instant and on the move.
//!
//! Hostile spells with reach are free-aimed, two-hand style: a bolt leaves the hand along the
//! crosshair's ray at the spell's own speed and strikes whatever it meets first (a unit, a wall,
//! the ground, a built piece; never a bush or anything else a player walks through), with no range:
//! it flies until it strikes, or fizzles at its fizzle distance (a puff, crate::spell_fx). It
//! flies straight unless it homes (Arcane Missiles, or a talent's or perk's grant): then it curves
//! toward its target at a limited turn rate, and the server tells its course as it turns. Where it
//! strikes it splashes the enemies around. The server flies it and decides what it hit. The caster
//! draws its own bolt at the release (the spell's missile model through benilla,
//! [`benilla_app::fusion::FusionSpellMissile`]); everyone else draws the server's word of it, and
//! everyone the impact.
//!
//! A spray (Scorch's fire stream, Cone of Cold's frost spray) pours while its button is held: op 43
//! at the press, again with the aim every 100 ms, and at the release; the server ticks it and tells
//! those near (op 44), and crate::spell_fx draws it.
//!
//! The server sends, on fusion channel 7, every spell a hand can hold with its charge time and
//! how it lands (op 9), a release it refused (op 10: the hand fizzles), a lasting zone a release
//! left (op 13: WoW's ground reticle and the spell's ground model), each bolt's flight and end (op
//! 42) and the sprays (op 44). A release goes out as op 41 (op 12 with its ray). The charge shows
//! on the stock casting bar (crate::cast_bar); this draws WoW's reticle where an area spell will
//! land, the zones and the bolts; crate::spell_fx the hands' and sprays' WoW models.

use std::collections::HashMap;

use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::aim::Aim;
use crate::hands::{Hand, Hands};
use crate::melee::MeleeHeld;
use crate::mode::FpsState;

pub use wire::*;

/// The hand-spell messages' wire (`Fusion::Spellcasting`), byte for byte, little-endian.
pub mod wire {
    /// The class framework's channel (`Fusion::Classes::kChannelClasses`).
    pub const CHANNEL: u8 = 7;
    pub const OP_HAND_SPELLS: u8 = 9;
    pub const OP_CAST_RESULT: u8 = 10;
    pub const OP_HAND_CAST: u8 = 12;
    pub const OP_ZONE: u8 = 13;
    /// A release with the ray it was aimed along (`kOpHandAim`).
    pub const OP_HAND_AIM: u8 = 41;
    /// A free-aimed bolt launched or ended (`kOpBolt`).
    pub const OP_BOLT: u8 = 42;
    /// A spray held, aimed again or let go (`kOpSprayAim`, client).
    pub const OP_SPRAY_AIM: u8 = 43;
    /// Someone's spray pouring or stopped (`kOpSpray`, server).
    pub const OP_SPRAY: u8 = 44;

    /// `BoltImpactFlags`: a built piece took it; it ended in the air (nothing to show); a
    /// missile channel's missile (the caster draws these from the server too); it fizzled out at
    /// its fizzle distance (the fizzle's puff); a homing bolt's course (sent to its caster too, and
    /// again as it turns).
    pub const BOLT_HIT_PIECE: u8 = 0x01;
    pub const BOLT_SPENT: u8 = 0x02;
    pub const BOLT_VOLLEY: u8 = 0x04;
    pub const BOLT_FIZZLED: u8 = 0x08;
    pub const BOLT_HOMING: u8 = 0x10;

    /// The hands as the mouse fires them (`kHandLeftButton`, `kHandRightButton`, `kHandBoth`).
    pub const HAND_LEFT_BUTTON: u8 = 0;
    pub const HAND_RIGHT_BUTTON: u8 = 1;
    pub const HAND_BOTH: u8 = 2;

    /// `HandCastFlags`: the crosshair is on the world, the point is where.
    pub const CAST_POINT: u8 = 0x01;
    /// The self key was held (a heal on oneself).
    pub const CAST_SELF: u8 = 0x02;
    /// Held to a full charge ([`super::FULL_CHARGE_FACTOR`] times the charge): the server's
    /// `CAST_FULL`, a stronger release.
    pub const CAST_FULL: u8 = 0x04;

    /// How a release lands (`Fusion::Spellcasting::Shape`).
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
    pub enum Shape {
        #[default]
        Bolt,
        Heal,
        Area,
        Splash,
        /// Free-aimed: flies from the hand along the crosshair's ray and strikes whatever it meets
        /// first, a unit or the world; the server flies it.
        Missile,
        /// The spell's own ground area (Blizzard, Flamestrike) at the crosshair's point.
        Ground,
        /// Held: a short cone in front (Scorch's fire stream, Cone of Cold's frost spray) while
        /// the button is down; `radius` is its reach.
        Spray,
    }

    impl Shape {
        fn from_u8(v: u8) -> Self {
            match v {
                1 => Shape::Heal,
                2 => Shape::Area,
                3 => Shape::Splash,
                4 => Shape::Missile,
                5 => Shape::Ground,
                6 => Shape::Spray,
                _ => Shape::Bolt,
            }
        }

        /// It lands on the ground at the crosshair (a marker shows where).
        pub fn on_ground(self) -> bool {
            matches!(self, Shape::Area | Shape::Splash | Shape::Ground)
        }
    }

    /// One spell a hand can hold (op 9).
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
    pub struct HandSpell {
        pub spell: u32,
        pub charge_ms: u16,
        pub shape: Shape,
        /// Yards, 0 for none.
        pub radius: u8,
    }

    impl HandSpell {
        pub fn charge_secs(&self) -> f32 {
            f32::from(self.charge_ms) / 1000.0
        }
    }

    /// A release (op 12): the crosshair's unit (0 none) and point.
    #[derive(Debug, Clone, Copy, PartialEq, Default)]
    pub struct HandCast {
        pub spell: u32,
        pub hand: u8,
        pub flags: u8,
        pub target: u64,
        pub point: [f32; 3],
    }

    /// A release with its aim (op 41): op 12's body, then the ray's origin and unit direction
    /// (WoW space): the camera's, in third person the eye's toward the crosshair's point.
    #[derive(Debug, Clone, Copy, PartialEq, Default)]
    pub struct HandAim {
        pub cast: HandCast,
        pub origin: [f32; 3],
        pub dir: [f32; 3],
    }

    /// A bolt's flight or its end (op 42), WoW space.
    #[derive(Debug, Clone, Copy, PartialEq)]
    pub enum BoltKind {
        /// It left `from` and, as things stood, ends at `to`.
        Launch { from: [f32; 3], to: [f32; 3], speed: f32 },
        /// It ended at `at`, on `unit` (0: the world).
        Impact { at: [f32; 3], unit: u64 },
    }

    #[derive(Debug, Clone, Copy, PartialEq)]
    pub struct BoltEvent {
        pub id: u32,
        /// The spell it carries (a missile channel's missile, else the spell released).
        pub spell: u32,
        pub caster: u64,
        pub kind: BoltKind,
        pub flags: u8,
    }

    /// A lasting zone (op 13).
    #[derive(Debug, Clone, Copy, PartialEq, Default)]
    pub struct Zone {
        pub spell: u32,
        pub at: [f32; 3],
        pub radius: f32,
        pub ms: u16,
    }

    struct Reader<'a> {
        b: &'a [u8],
        at: usize,
    }

    impl Reader<'_> {
        fn take<const N: usize>(&mut self) -> Option<[u8; N]> {
            let s = self.b.get(self.at..self.at + N)?;
            self.at += N;
            s.try_into().ok()
        }
        fn u8(&mut self) -> Option<u8> {
            self.take::<1>().map(|b| b[0])
        }
        fn u16(&mut self) -> Option<u16> {
            self.take().map(u16::from_le_bytes)
        }
        fn u32(&mut self) -> Option<u32> {
            self.take().map(u32::from_le_bytes)
        }
        fn u64(&mut self) -> Option<u64> {
            self.take().map(u64::from_le_bytes)
        }
        fn f32(&mut self) -> Option<f32> {
            self.take().map(f32::from_le_bytes)
        }
        fn done(&self) -> bool {
            self.at == self.b.len()
        }
    }

    pub fn encode_hand_cast(c: &HandCast) -> Vec<u8> {
        let mut o = Vec::with_capacity(27);
        o.push(OP_HAND_CAST);
        o.extend_from_slice(&c.spell.to_le_bytes());
        o.push(c.hand);
        o.push(c.flags);
        o.extend_from_slice(&c.target.to_le_bytes());
        for v in c.point {
            o.extend_from_slice(&v.to_le_bytes());
        }
        o
    }

    pub fn encode_hand_aim(a: &HandAim) -> Vec<u8> {
        let mut o = encode_hand_cast(&a.cast);
        o[0] = OP_HAND_AIM;
        for v in a.origin.into_iter().chain(a.dir) {
            o.extend_from_slice(&v.to_le_bytes());
        }
        o
    }

    pub fn decode_bolt_event(b: &[u8]) -> Option<BoltEvent> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_BOLT {
            return None;
        }
        let kind = r.u8()?;
        let id = r.u32()?;
        let spell = r.u32()?;
        let caster = r.u64()?;
        let v3 = |r: &mut Reader| -> Option<[f32; 3]> { Some([r.f32()?, r.f32()?, r.f32()?]) };
        let kind = match kind {
            1 => BoltKind::Launch {
                from: v3(&mut r)?,
                to: v3(&mut r)?,
                speed: r.f32()?,
            },
            2 => BoltKind::Impact {
                at: v3(&mut r)?,
                unit: r.u64()?,
            },
            _ => return None,
        };
        let flags = r.u8()?;
        r.done().then_some(BoltEvent {
            id,
            spell,
            caster,
            kind,
            flags,
        })
    }

    pub fn decode_hand_spells(b: &[u8]) -> Option<Vec<HandSpell>> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_HAND_SPELLS {
            return None;
        }
        let n = r.u16()?;
        let mut out = Vec::with_capacity(usize::from(n));
        for _ in 0..n {
            out.push(HandSpell {
                spell: r.u32()?,
                charge_ms: r.u16()?,
                shape: Shape::from_u8(r.u8()?),
                radius: r.u8()?,
            });
        }
        r.done().then_some(out)
    }

    /// op 10: the hand and the spell (the reason is the server's `CastError`).
    pub fn decode_cast_result(b: &[u8]) -> Option<(u8, u8, u32)> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_CAST_RESULT {
            return None;
        }
        let v = (r.u8()?, r.u8()?, r.u32()?);
        r.done().then_some(v)
    }

    pub fn decode_zone(b: &[u8]) -> Option<Zone> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_ZONE {
            return None;
        }
        let z = Zone {
            spell: r.u32()?,
            at: [r.f32()?, r.f32()?, r.f32()?],
            radius: r.f32()?,
            ms: r.u16()?,
        };
        r.done().then_some(z)
    }

    /// A spray held or let go (op 43): `[43, spell u32, hand u8, on u8, origin xyz, dir xyz]`.
    #[derive(Debug, Clone, Copy, PartialEq, Default)]
    pub struct SprayAim {
        pub spell: u32,
        /// [`HAND_LEFT_BUTTON`] or [`HAND_RIGHT_BUTTON`]: each hand pours its own.
        pub hand: u8,
        pub on: bool,
        pub origin: [f32; 3],
        pub dir: [f32; 3],
    }

    pub fn encode_spray_aim(a: &SprayAim) -> Vec<u8> {
        let mut o = Vec::with_capacity(31);
        o.push(OP_SPRAY_AIM);
        o.extend_from_slice(&a.spell.to_le_bytes());
        o.push(a.hand);
        o.push(u8::from(a.on));
        for v in a.origin.into_iter().chain(a.dir) {
            o.extend_from_slice(&v.to_le_bytes());
        }
        o
    }

    /// Someone's spray pouring or stopped (op 44), WoW space.
    #[derive(Debug, Clone, Copy, PartialEq, Default)]
    pub struct SprayEvent {
        pub caster: u64,
        pub spell: u32,
        pub hand: u8,
        pub on: bool,
        pub from: [f32; 3],
        pub dir: [f32; 3],
        pub range: f32,
        pub half_angle_deg: f32,
    }

    pub fn decode_spray_event(b: &[u8]) -> Option<SprayEvent> {
        let mut r = Reader { b, at: 0 };
        if r.u8()? != OP_SPRAY {
            return None;
        }
        let e = SprayEvent {
            caster: r.u64()?,
            spell: r.u32()?,
            hand: r.u8()?,
            on: r.u8()? != 0,
            from: [r.f32()?, r.f32()?, r.f32()?],
            dir: [r.f32()?, r.f32()?, r.f32()?],
            range: r.f32()?,
            half_angle_deg: r.f32()?,
        };
        r.done().then_some(e)
    }
}

/// Seconds the two hands' releases may lie apart and still make one dual cast.
pub const DUAL_WINDOW_SECS: f32 = 0.15;

/// A charge held this many times its length is a full charge (the server's `kFullChargeFactor`):
/// the release hits 2.25x as hard (`kFullChargePower`). Let go once charged: a tap, as designed.
pub const FULL_CHARGE_FACTOR: f32 = 2.75;

/// Whether a hand held since `started` is fully charged at `now` (a spell of `secs` charge; a
/// spell with no charge never is).
pub fn full_charge(started: f32, now: f32, secs: f32) -> bool {
    secs > 0.0 && now - started >= secs * FULL_CHARGE_FACTOR
}

/// The spells a hand can hold, as the server sent them (op 9), by spell id (every rank).
#[derive(Resource, Debug, Default)]
pub struct HandSpells(pub HashMap<u32, HandSpell>);

/// What leaving the buttons does this frame: one hand casts, or both together.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Release {
    Single(usize),
    Dual,
}

/// The releases due now. A hand let go fully charged waits (`pending`, when it was let go) up to
/// [`DUAL_WINDOW_SECS`] for the other hand, while that one holds the same spell fully charged; both
/// let go in that time cast once, together.
pub fn resolve_releases(
    pending: &mut [Option<f32>; 2],
    charged_held: [bool; 2],
    same_spell: bool,
    now: f32,
) -> Vec<Release> {
    if same_spell && pending[0].is_some() && pending[1].is_some() {
        *pending = [None; 2];
        return vec![Release::Dual];
    }
    let mut out = Vec::new();
    for side in 0..2 {
        if let Some(at) = pending[side] {
            let wait = same_spell && charged_held[1 - side] && now - at < DUAL_WINDOW_SECS;
            if !wait {
                pending[side] = None;
                out.push(Release::Single(side));
            }
        }
    }
    out
}

/// How far a hand's charge is (0..=1).
pub fn charge_fraction(started: f32, now: f32, secs: f32) -> f32 {
    if secs <= 0.0 {
        1.0
    } else {
        ((now - started) / secs).clamp(0.0, 1.0)
    }
}

/// Seconds the release flash and the fizzle last on the hand.
pub const FLASH_SECS: f32 = 0.18;
pub const FIZZLE_SECS: f32 = 0.3;

/// A hand's glow size (inches; crate::spell_fx scales the hand's precast model by it over its
/// rest): at rest, growing with the charge and pulsing once full, a flash on the release,
/// shrinking away on a fizzle.
pub fn glow_scale(hands: &Hands, side: usize, t: f32) -> f32 {
    const REST: f32 = 1.3;
    if let Some(at) = hands.released_at[side].filter(|&at| t - at < FLASH_SECS) {
        let k = (t - at) / FLASH_SECS;
        return REST + 2.4 * (1.0 - k);
    }
    if let Some(at) = hands.fizzled_at[side].filter(|&at| t - at < FIZZLE_SECS) {
        let k = (t - at) / FIZZLE_SECS;
        return REST * (1.0 - 0.75 * k);
    }
    // A spray pouring: big and flickering.
    if hands.spraying[side].is_some() {
        return REST + 1.3 + 0.25 * (t * 18.0).sin();
    }
    if let Some(started) = hands.charging[side] {
        let f = charge_fraction(started, t, hands.charge_secs[side]);
        // Past the charge it keeps growing toward the full charge, then pulses harder.
        let over = if f >= 1.0 && hands.charge_secs[side] > 0.0 {
            ((t - started) / hands.charge_secs[side] - 1.0).clamp(0.0, FULL_CHARGE_FACTOR - 1.0)
                / (FULL_CHARGE_FACTOR - 1.0)
        } else {
            0.0
        };
        let pulse = if full_charge(started, t, hands.charge_secs[side]) {
            0.3 * (t * 16.0).sin()
        } else if f >= 1.0 {
            0.15 * (t * 12.0).sin()
        } else {
            0.0
        };
        return REST + 1.1 * f + 0.6 * over + pulse;
    }
    REST
}

/// The release with its aim (op 41): [`hand_cast`] plus the crosshair's ray, WoW space.
pub fn hand_aim(aim: &Aim, spell: u32, hand: u8, self_key: bool) -> HandAim {
    let (origin, dir) = aim.ray.map_or(([0.0; 3], [0.0; 3]), |r| {
        (crate::fusion::to_wow(r.origin), crate::fusion::to_wow(*r.direction))
    });
    HandAim {
        cast: hand_cast(aim, spell, hand, self_key),
        origin,
        dir,
    }
}

pub fn send_aim(out: &mut MessageWriter<SendFusionChannel>, a: &HandAim) {
    out.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL,
        body: encode_hand_aim(a),
    }));
}

/// A hand let a spell go (crate::hands): the bolt it fires is drawn from here.
#[derive(Message, Clone, Copy, Debug)]
pub struct HandReleased {
    pub spell: u32,
    /// The hands as the mouse fires them ([`HAND_LEFT_BUTTON`], [`HAND_RIGHT_BUTTON`], [`HAND_BOTH`]).
    pub hand: u8,
}

/// The farthest a bolt is drawn before the server's word ends it: bolts have no range, only the
/// server's safety cap (`kBoltSafetyYards`); a fizzle or a strike ends it sooner.
pub const BOLT_DRAW_REACH: f32 = 200.0;

/// Where a released bolt leaves on screen: before the camera, low, on the side of the hand that
/// threw it (the left button's hand is the right fist), both hands' midway.
pub fn bolt_launch_point(ray: Ray3d, hand: u8) -> Vec3 {
    let fwd = *ray.direction;
    let right = fwd.cross(Vec3::Y).normalize_or(Vec3::X);
    let up = right.cross(fwd).normalize_or(Vec3::Y);
    let side = match hand {
        HAND_LEFT_BUTTON => 0.28,
        HAND_RIGHT_BUTTON => -0.28,
        _ => 0.0,
    };
    ray.origin + fwd * 0.7 + right * side - up * 0.22
}

/// Where the caster foresees its bolt ending: the crosshair's unit, else the solid world under the
/// crosshair (what a body collides with: not a bush, not a canopy, [`Aim::bolt_target`] and
/// [`Aim::solid_distance`]), within [`BOLT_DRAW_REACH`]; the server's impact settles it.
pub fn bolt_predicted_end(aim: &Aim) -> Option<Vec3> {
    let ray = aim.ray?;
    let d = aim
        .bolt_target
        .as_ref()
        .filter(|t| !t.service)
        .map(|t| t.distance)
        .or(aim.solid_distance)
        .unwrap_or(BOLT_DRAW_REACH)
        .min(BOLT_DRAW_REACH);
    Some(ray.origin + *ray.direction * d)
}

/// The caster's own bolts drawn at the release, oldest first: (spell, key, the server's id once a
/// homing course named it), each ended by the server's impact of the same spell; and the next key,
/// apart from the server's ids.
#[derive(Resource, Default)]
pub struct OwnBolts {
    pub flying: std::collections::VecDeque<(u32, u64, Option<u32>)>,
    next: u64,
}

/// Local keys sit above every server bolt id (a u32).
const LOCAL_BOLT_KEYS: u64 = 1 << 40;

impl OwnBolts {
    pub fn launch(&mut self, spell: u32) -> u64 {
        self.next += 1;
        let key = LOCAL_BOLT_KEYS | self.next;
        self.flying.push_back((spell, key, None));
        // A lost impact never leaves the queue growing.
        while self.flying.len() > 32 {
            self.flying.pop_front();
        }
        key
    }

    /// The server ended one of ours: the one its id steered, else the oldest drawn of that spell.
    pub fn land(&mut self, spell: u32, id: u32) -> Option<u64> {
        let i = self
            .flying
            .iter()
            .position(|&(_, _, linked)| linked == Some(id))
            .or_else(|| {
                self.flying
                    .iter()
                    .position(|&(s, _, linked)| s == spell && linked.is_none())
            })?;
        self.flying.remove(i).map(|(_, k, _)| k)
    }

    /// A homing course of one of ours: the bolt drawn for it (the oldest of that spell not yet
    /// named), from now on named by the server's id.
    pub fn steer(&mut self, spell: u32, id: u32) -> Option<u64> {
        if let Some(&(_, k, _)) = self.flying.iter().find(|f| f.2 == Some(id)) {
            return Some(k);
        }
        let f = self
            .flying
            .iter_mut()
            .find(|f| f.0 == spell && f.2.is_none())?;
        f.2 = Some(id);
        Some(f.1)
    }
}

/// The key a bolt message flies or ends: `None` for our own launch, drawn at the release. A homing
/// course of ours re-aims the bolt we drew; anyone's, the one of its id.
pub fn bolt_key(e: &BoltEvent, me: Option<u64>, own: &mut OwnBolts) -> Option<u64> {
    let mine = me == Some(e.caster) && e.flags & BOLT_VOLLEY == 0;
    let id = u64::from(e.id);
    match e.kind {
        BoltKind::Launch { .. } if mine && e.flags & BOLT_HOMING != 0 => {
            Some(own.steer(e.spell, e.id).unwrap_or(id))
        }
        BoltKind::Launch { .. } if mine => None,
        BoltKind::Launch { .. } => Some(id),
        // Ours ends the one drawn for it (an instant had none: its key is the id).
        BoltKind::Impact { .. } if mine => Some(own.land(e.spell, e.id).unwrap_or(id)),
        BoltKind::Impact { .. } => Some(id),
    }
}

/// The spray message for the crosshair as it is: a hand's ([`HAND_LEFT_BUTTON`] or
/// [`HAND_RIGHT_BUTTON`]) spray held (`on`) or let go, along the crosshair's ray, WoW space.
pub fn spray_aim(aim: &Aim, spell: u32, hand: u8, on: bool) -> SprayAim {
    let (origin, dir) = aim.ray.map_or(([0.0; 3], [0.0; 3]), |r| {
        (crate::fusion::to_wow(r.origin), crate::fusion::to_wow(*r.direction))
    });
    SprayAim {
        spell,
        hand,
        on,
        origin,
        dir,
    }
}

pub fn send_spray(out: &mut MessageWriter<SendFusionChannel>, a: &SprayAim) {
    out.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL,
        body: encode_spray_aim(a),
    }));
}

/// How often a held spray's aim goes to the server (`kSprayAimEveryMs`).
pub const SPRAY_AIM_EVERY_SECS: f32 = 0.1;

/// The release message for the crosshair as it is.
pub fn hand_cast(aim: &Aim, spell: u32, hand: u8, self_key: bool) -> HandCast {
    let unit = aim.target.as_ref().filter(|a| !a.service);
    let mut flags = if self_key { CAST_SELF } else { 0 };
    // On a unit, the point is where the crosshair meets it; else the world under the crosshair;
    // the sky has none (a bolt then needs the unit, an area spell can't land).
    let point = match (unit, aim.ray, aim.ground) {
        (Some(u), Some(ray), _) => {
            flags |= CAST_POINT;
            crate::fusion::to_wow(ray.origin + *ray.direction * u.distance)
        }
        (_, _, Some(g)) => {
            flags |= CAST_POINT;
            crate::fusion::to_wow(g)
        }
        _ => [0.0; 3],
    };
    HandCast {
        spell,
        hand,
        flags,
        target: unit.map_or(0, |u| u.guid),
        point,
    }
}

pub struct SpellcastPlugin;

impl Plugin for SpellcastPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<HandSpells>()
            .init_resource::<Zones>()
            .init_resource::<OwnBolts>()
            .add_message::<HandReleased>()
            .add_systems(
                Update,
                (draw_own_bolts, read_channel, draw_markers)
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

/// The caster's bolts, drawn the moment the hand lets go: the spell's own missile from the hand to
/// where the crosshair is, at the spell's speed. An instant spell (speed 0) and a missile channel
/// (its missiles come from the server) draw nothing here.
fn draw_own_bolts(
    mut released: MessageReader<HandReleased>,
    aim: Res<Aim>,
    spells: Res<HandSpells>,
    table: Res<crate::hands::SpellTable>,
    mut own: ResMut<OwnBolts>,
    mut out: MessageWriter<benilla_app::fusion::FusionSpellMissile>,
) {
    for r in released.read() {
        if spells.0.get(&r.spell).map(|h| h.shape) != Some(Shape::Missile) {
            continue;
        }
        let speed = table
            .0
            .as_ref()
            .and_then(|c| c.get(r.spell))
            .map_or(0.0, |s| s.speed);
        let (Some(ray), Some(to)) = (aim.ray, bolt_predicted_end(&aim)) else {
            continue;
        };
        if speed <= 0.0 {
            continue;
        }
        out.write(benilla_app::fusion::FusionSpellMissile {
            key: own.launch(r.spell),
            spell_id: r.spell,
            from: bolt_launch_point(ray, r.hand),
            to,
            speed,
        });
    }
}

/// A zone standing: its spell, where (Bevy space), how wide, until when (the app clock), and its
/// serial (crate::spell_fx keys its ground model by it).
#[derive(Clone, Copy, Debug, PartialEq)]
struct ZoneShown {
    spell: u32,
    at: Vec3,
    radius: f32,
    until: f32,
    serial: u32,
}

/// The zones standing, and the next zone's serial.
#[derive(Resource, Default)]
struct Zones(Vec<ZoneShown>, u32);

#[allow(clippy::too_many_arguments)]
fn read_channel(
    time: Res<Time>,
    state: Res<FpsState>,
    objects: benilla_app::Objects,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut spells: ResMut<HandSpells>,
    mut hands: ResMut<Hands>,
    mut zones: ResMut<Zones>,
    mut own: ResMut<OwnBolts>,
    mut missiles: MessageWriter<benilla_app::fusion::FusionSpellMissile>,
    mut impacts: MessageWriter<benilla_app::fusion::FusionSpellImpact>,
    mut sprays: ResMut<crate::spell_fx::Sprays>,
    mut fizzles: MessageWriter<crate::spell_fx::Fizzle>,
    mut seen: MessageWriter<crate::spell_fx::CastSeen>,
) {
    let t = time.elapsed_secs();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL {
            continue;
        }
        match m.body.first() {
            Some(&OP_HAND_SPELLS) => {
                if let Some(list) = decode_hand_spells(&m.body) {
                    info!("benilla-mw2: {} hand spells from the server", list.len());
                    spells.0.extend(list.into_iter().map(|h| (h.spell, h)));
                }
            }
            Some(&OP_CAST_RESULT) => {
                if let Some((hand, error, spell)) = decode_cast_result(&m.body) {
                    info!("benilla-mw2: the server refused hand {hand}'s spell {spell} ({error})");
                    for side in 0..2 {
                        if hand == HAND_BOTH || usize::from(hand) == side {
                            hands.fizzled_at[side] = Some(t);
                            // A spray the server stopped (out of mana, silenced): it stops here too.
                            hands.spraying[side] = None;
                        }
                    }
                }
            }
            Some(&OP_BOLT) => {
                let Some(e) = decode_bolt_event(&m.body) else {
                    continue;
                };
                let Some(key) = bolt_key(&e, state.self_guid, &mut own) else {
                    continue;
                };
                match e.kind {
                    BoltKind::Launch { from, to, speed } => {
                        // Someone else's bolt leaving: their hand flashes (crate::spell_fx).
                        if Some(e.caster) != state.self_guid
                            && e.flags & BOLT_VOLLEY == 0
                            && let Some(caster) = objects.entity(e.caster)
                        {
                            seen.write(crate::spell_fx::CastSeen {
                                caster,
                                spell: e.spell,
                            });
                        }
                        missiles.write(benilla_app::fusion::FusionSpellMissile {
                            key,
                            spell_id: e.spell,
                            from: crate::tracer::from_wow(from),
                            to: crate::tracer::from_wow(to),
                            speed,
                        });
                    }
                    BoltKind::Impact { at, unit } => {
                        if e.flags & BOLT_FIZZLED != 0 {
                            fizzles.write(crate::spell_fx::Fizzle {
                                at: crate::tracer::from_wow(at),
                                spell: e.spell,
                            });
                        }
                        impacts.write(benilla_app::fusion::FusionSpellImpact {
                            key,
                            spell_id: e.spell,
                            at: crate::tracer::from_wow(at),
                            unit: (unit != 0).then(|| objects.entity(unit)).flatten(),
                            spent: e.flags & BOLT_SPENT != 0,
                        });
                    }
                }
            }
            Some(&OP_SPRAY) => {
                if let Some(e) = decode_spray_event(&m.body) {
                    sprays.heard(&e, t);
                }
            }
            Some(&OP_ZONE) => {
                if let Some(z) = decode_zone(&m.body) {
                    let serial = zones.1;
                    zones.1 = zones.1.wrapping_add(1);
                    zones.0.push(ZoneShown {
                        spell: z.spell,
                        at: crate::tracer::from_wow(z.at),
                        radius: z.radius,
                        until: t + f32::from(z.ms) / 1000.0,
                        serial,
                    });
                }
            }
            _ => {}
        }
    }
    zones.0.retain(|z| z.until > t);
}

/// Where a charging area spell will land and the zones standing: WoW's own ground reticle
/// (benilla's `LayerGroundReticles`, the stock AoE targeting's decal), and each zone its spell's
/// ground model (crate::spell_fx::Look::zone), looping while it stands.
#[allow(clippy::too_many_arguments)]
fn draw_markers(
    state: Res<FpsState>,
    held: Res<MeleeHeld>,
    hands: Res<Hands>,
    spells: Res<HandSpells>,
    aim: Res<Aim>,
    zones: Res<Zones>,
    table: Res<crate::hands::SpellTable>,
    mut looks: ResMut<crate::spell_fx::SpellLooks>,
    mut reticles: ResMut<benilla_app::fusion::LayerGroundReticles>,
    mut fx: MessageWriter<benilla_app::fusion::FusionEffect>,
    mut shown: Local<Vec<(u32, usize)>>,
) {
    reticles.0.clear();
    // The aim marker: a hand charging a spell that lands on the ground.
    let charging_area = (0..2).find_map(|side| {
        let hand = if side == 0 { hands.right } else { hands.left };
        let Hand::Spell { spell_id, .. } = hand else {
            return None;
        };
        let info = spells.0.get(&spell_id)?;
        (hands.charging[side].is_some() && info.shape.on_ground()).then_some((spell_id, info))
    });
    let marker = charging_area
        .filter(|_| state.active && held.0)
        .and_then(|(spell, info)| {
            let point = match (&aim.target, aim.ray) {
                (Some(u), Some(ray)) if !u.service => {
                    Some(ray.origin + *ray.direction * u.distance)
                }
                _ => aim.ground,
            }?;
            Some((point, marker_radius(&hands, spell, info)))
        });
    if let Some((center, radius)) = marker {
        reticles.0.push(benilla_app::fusion::LayerReticle {
            center,
            radius,
            acceptable: true,
            alpha: 1.0,
        });
    }
    for z in &zones.0 {
        reticles.0.push(benilla_app::fusion::LayerReticle {
            center: z.at,
            radius: z.radius,
            acceptable: true,
            alpha: 0.45,
        });
    }
    // Each new zone's ground models, looping until it goes.
    let mut now: Vec<(u32, usize)> = Vec::new();
    for z in &zones.0 {
        let models = looks
            .get(&table, z.spell)
            .map(|l| l.zone.clone())
            .unwrap_or_default();
        for (i, path) in models.into_iter().enumerate().take(4) {
            now.push((z.serial, i));
            if shown.contains(&(z.serial, i)) {
                continue;
            }
            fx.write(benilla_app::fusion::FusionEffect::Play {
                key: crate::spell_fx::key::zone(z.serial, i),
                path,
                parent: None,
                transform: Transform::from_translation(z.at),
                looping: true,
                size: 1.0,
                ground: true,
            });
        }
    }
    for gone in shown.iter().filter(|k| !now.contains(k)) {
        fx.write(benilla_app::fusion::FusionEffect::Stop {
            key: crate::spell_fx::key::zone(gone.0, gone.1),
        });
    }
    *shown = now;
}

/// The aim marker's radius: the spell's, half again wider when both hands charge it (a dual cast).
fn marker_radius(hands: &Hands, spell: u32, info: &HandSpell) -> f32 {
    let both = [hands.right, hands.left]
        .iter()
        .all(|h| matches!(h, Hand::Spell { spell_id, .. } if *spell_id == spell))
        && hands.charging.iter().all(Option::is_some);
    f32::from(info.radius.max(1)) * if both { 1.5 } else { 1.0 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_release_encodes_as_the_server_reads_it() {
        let c = HandCast {
            spell: 25306,
            hand: HAND_BOTH,
            flags: CAST_POINT | CAST_SELF,
            target: 0xF130_0000_0000_1234,
            point: [-8913.5, 554.25, 93.75],
        };
        let b = encode_hand_cast(&c);
        // [12, spell u32, hand u8, flags u8, target u64, x f32, y f32, z f32]: 27 bytes.
        assert_eq!(b.len(), 27);
        assert_eq!(b[0], OP_HAND_CAST);
        assert_eq!(&b[1..5], &25306u32.to_le_bytes());
        assert_eq!((b[5], b[6]), (HAND_BOTH, CAST_POINT | CAST_SELF));
        assert_eq!(&b[7..15], &0xF130_0000_0000_1234u64.to_le_bytes());
        assert_eq!(&b[23..27], &93.75f32.to_le_bytes());
    }

    #[test]
    fn hand_spells_zones_and_refusals_decode_as_the_server_writes_them() {
        let mut b = vec![OP_HAND_SPELLS, 2, 0];
        b.extend_from_slice(&9835u32.to_le_bytes());
        b.extend_from_slice(&900u16.to_le_bytes());
        b.extend_from_slice(&[3, 8]);
        b.extend_from_slice(&25306u32.to_le_bytes());
        b.extend_from_slice(&600u16.to_le_bytes());
        b.extend_from_slice(&[0, 0]);
        let s = decode_hand_spells(&b).expect("hand spells");
        assert_eq!(
            s,
            vec![
                HandSpell {
                    spell: 9835,
                    charge_ms: 900,
                    shape: Shape::Splash,
                    radius: 8
                },
                HandSpell {
                    spell: 25306,
                    charge_ms: 600,
                    shape: Shape::Bolt,
                    radius: 0
                },
            ]
        );
        assert!(s[0].shape.on_ground() && !s[1].shape.on_ground());
        assert!((s[1].charge_secs() - 0.6).abs() < 1e-6);
        b.pop();
        assert!(decode_hand_spells(&b).is_none());

        let mut r = vec![OP_CAST_RESULT, 1, 5];
        r.extend_from_slice(&9835u32.to_le_bytes());
        assert_eq!(decode_cast_result(&r), Some((1, 5, 9835)));

        let mut z = vec![OP_ZONE];
        z.extend_from_slice(&9835u32.to_le_bytes());
        for v in [1.0f32, 2.0, 3.0, 12.0] {
            z.extend_from_slice(&v.to_le_bytes());
        }
        z.extend_from_slice(&6000u16.to_le_bytes());
        let zone = decode_zone(&z).expect("zone");
        assert_eq!(
            (zone.spell, zone.at, zone.radius, zone.ms),
            (9835, [1.0, 2.0, 3.0], 12.0, 6000)
        );
        assert!(decode_zone(&r).is_none());
    }

    #[test]
    fn a_full_charge_is_a_long_hold() {
        assert!(!full_charge(1.0, 2.2, 0.6));
        assert!(!full_charge(1.0, 2.64, 0.6));
        assert!(full_charge(1.0, 2.66, 0.6));
        // A spell with no charge (a spray) never is.
        assert!(!full_charge(1.0, 99.0, 0.0));
        // The glow keeps growing past the charge and is biggest held full.
        let mut h = Hands::default();
        h.charge_secs[0] = 0.6;
        h.charging[0] = Some(1.0);
        let charged = glow_scale(&h, 0, 1.6);
        let fuller = glow_scale(&h, 0, 1.9);
        assert!(fuller > charged - 0.2);
        assert!(glow_scale(&h, 0, 2.2) > glow_scale(&h, 0, 1.0));
    }

    #[test]
    fn a_charge_fills_then_holds() {
        assert_eq!(charge_fraction(1.0, 1.0, 0.6), 0.0);
        assert!((charge_fraction(1.0, 1.3, 0.6) - 0.5).abs() < 1e-5);
        assert_eq!(charge_fraction(1.0, 1.6, 0.6), 1.0);
        assert_eq!(
            charge_fraction(1.0, 30.0, 0.6),
            1.0,
            "held as long as you like"
        );
    }

    #[test]
    fn hands_let_go_together_cast_once_as_a_pair() {
        // Both let go in the same frame, the same spell: one dual cast.
        let mut p = [Some(5.0), Some(5.0)];
        assert_eq!(
            resolve_releases(&mut p, [false; 2], true, 5.0),
            vec![Release::Dual]
        );
        assert_eq!(p, [None, None]);
        // One let go, the other still holding it charged: it waits...
        let mut p = [Some(5.0), None];
        assert!(resolve_releases(&mut p, [false, true], true, 5.05).is_empty());
        // ...the other let go in time: a pair.
        p[1] = Some(5.1);
        assert_eq!(
            resolve_releases(&mut p, [false; 2], true, 5.1),
            vec![Release::Dual]
        );
        // Too late: the first goes alone.
        let mut p = [Some(5.0), None];
        assert_eq!(
            resolve_releases(&mut p, [false, true], true, 5.0 + DUAL_WINDOW_SECS),
            vec![Release::Single(0)]
        );
        // Different spells never pair, nor wait.
        let mut p = [Some(5.0), Some(5.0)];
        assert_eq!(
            resolve_releases(&mut p, [false; 2], false, 5.0),
            vec![Release::Single(0), Release::Single(1)]
        );
        let mut p = [None, Some(5.0)];
        assert_eq!(
            resolve_releases(&mut p, [true, false], false, 5.0),
            vec![Release::Single(1)]
        );
    }

    #[test]
    fn the_glow_grows_with_the_charge_and_flashes_on_release() {
        let mut h = Hands::default();
        let rest = glow_scale(&h, 0, 10.0);
        h.charging[0] = Some(10.0);
        h.charge_secs[0] = 0.6;
        let half = glow_scale(&h, 0, 10.3);
        let full = glow_scale(&h, 0, 10.6);
        assert!(rest < half && half < full - 0.2);
        h.charging[0] = None;
        h.released_at[0] = Some(11.0);
        assert!(glow_scale(&h, 0, 11.0) > full);
        assert_eq!(glow_scale(&h, 0, 11.0 + FLASH_SECS), rest);
        h.fizzled_at[1] = Some(12.0);
        assert!(glow_scale(&h, 1, 12.2) < rest);
    }
    #[test]
    fn an_aimed_release_is_op_12_and_its_ray() {
        let a = HandAim {
            cast: HandCast {
                spell: 133,
                hand: HAND_RIGHT_BUTTON,
                flags: CAST_POINT,
                target: 0,
                point: [10.0, 20.0, 30.0],
            },
            origin: [1.0, 2.0, 3.5],
            dir: [0.0, 0.6, 0.8],
        };
        let b = encode_hand_aim(&a);
        // [41, op 12's 26 bytes, origin xyz, dir xyz]: 51 bytes, as the server reads it.
        assert_eq!(b.len(), 51);
        assert_eq!(b[0], OP_HAND_AIM);
        assert_eq!(&b[1..27], &encode_hand_cast(&a.cast)[1..]);
        assert_eq!(&b[27..31], &1.0f32.to_le_bytes());
        assert_eq!(&b[35..39], &3.5f32.to_le_bytes());
        assert_eq!(&b[47..51], &0.8f32.to_le_bytes());
    }

    fn launch_body(id: u32, caster: u64, flags: u8) -> Vec<u8> {
        let mut b = vec![OP_BOLT, 1];
        b.extend_from_slice(&id.to_le_bytes());
        b.extend_from_slice(&133u32.to_le_bytes());
        b.extend_from_slice(&caster.to_le_bytes());
        for v in [1.0f32, 2.0, 3.0, 31.0, 2.0, 3.0, 24.0] {
            b.extend_from_slice(&v.to_le_bytes());
        }
        b.push(flags);
        b
    }

    fn impact_body(id: u32, caster: u64, unit: u64, flags: u8) -> Vec<u8> {
        let mut b = vec![OP_BOLT, 2];
        b.extend_from_slice(&id.to_le_bytes());
        b.extend_from_slice(&133u32.to_le_bytes());
        b.extend_from_slice(&caster.to_le_bytes());
        for v in [20.0f32, 2.0, 3.0] {
            b.extend_from_slice(&v.to_le_bytes());
        }
        b.extend_from_slice(&unit.to_le_bytes());
        b.push(flags);
        b
    }

    #[test]
    fn bolt_launches_and_impacts_decode_as_the_server_writes_them() {
        let l = launch_body(77, 0x42, BOLT_VOLLEY);
        assert_eq!(l.len(), 47);
        let e = decode_bolt_event(&l).expect("launch");
        assert_eq!((e.id, e.spell, e.caster, e.flags), (77, 133, 0x42, BOLT_VOLLEY));
        assert_eq!(
            e.kind,
            BoltKind::Launch {
                from: [1.0, 2.0, 3.0],
                to: [31.0, 2.0, 3.0],
                speed: 24.0
            }
        );
        let i = impact_body(77, 0x42, 0xF130_0000_0000_1234, BOLT_HIT_PIECE);
        assert_eq!(i.len(), 39);
        let e = decode_bolt_event(&i).expect("impact");
        assert_eq!(
            e.kind,
            BoltKind::Impact {
                at: [20.0, 2.0, 3.0],
                unit: 0xF130_0000_0000_1234
            }
        );
        assert_eq!(e.flags, BOLT_HIT_PIECE);
        // Short, a kind that is neither, another op: nothing.
        assert!(decode_bolt_event(&i[..38]).is_none());
        let mut bad = i.clone();
        bad[1] = 3;
        assert!(decode_bolt_event(&bad).is_none());
        let mut zone = i;
        zone[0] = OP_ZONE;
        assert!(decode_bolt_event(&zone).is_none());
    }

    #[test]
    fn our_bolts_are_drawn_at_the_release_and_ended_by_the_server() {
        let me = Some(0x42);
        let mut own = OwnBolts::default();
        let first = own.launch(133);
        let second = own.launch(133);
        let other = own.launch(116);
        assert!(first > u64::from(u32::MAX), "local keys sit above the server's ids");
        // The server's word of our own launch: drawn already.
        let ours = decode_bolt_event(&launch_body(5, 0x42, 0)).unwrap();
        assert_eq!(bolt_key(&ours, me, &mut own), None);
        // Its impact ends the oldest of that spell, then the next.
        let hit = decode_bolt_event(&impact_body(5, 0x42, 0, 0)).unwrap();
        assert_eq!(bolt_key(&hit, me, &mut own), Some(first));
        assert_eq!(bolt_key(&hit, me, &mut own), Some(second));
        // None of that spell left (an instant): the server's id.
        assert_eq!(bolt_key(&hit, me, &mut own), Some(5));
        assert_eq!(own.flying.iter().map(|f| f.1).collect::<Vec<_>>(), vec![other]);
        // A missile channel's missiles we draw from the server, by id.
        let volley = decode_bolt_event(&launch_body(9, 0x42, BOLT_VOLLEY)).unwrap();
        assert_eq!(bolt_key(&volley, me, &mut own), Some(9));
        // Someone else's: by id, both ends.
        let theirs = decode_bolt_event(&launch_body(11, 0x77, 0)).unwrap();
        assert_eq!(bolt_key(&theirs, me, &mut own), Some(11));
        let theirs_hit = decode_bolt_event(&impact_body(11, 0x77, 0, 0)).unwrap();
        assert_eq!(bolt_key(&theirs_hit, me, &mut own), Some(11));
        // The queue never grows past a few dozen lost impacts.
        for _ in 0..100 {
            own.launch(1);
        }
        assert!(own.flying.len() <= 32);
    }

    #[test]
    fn a_bolt_leaves_the_hand_that_threw_it_toward_the_crosshair() {
        let ray = Ray3d::new(Vec3::new(0.0, 1.6, 0.0), Dir3::NEG_Z);
        let right_fist = bolt_launch_point(ray, HAND_LEFT_BUTTON);
        let left_fist = bolt_launch_point(ray, HAND_RIGHT_BUTTON);
        let both = bolt_launch_point(ray, HAND_BOTH);
        // Forward of the eye, below it, on its own side (screen right is +X looking down -Z).
        assert!(right_fist.z < 0.0 && right_fist.y < 1.6 && right_fist.x > 0.1);
        assert!(left_fist.x < -0.1 && both.x.abs() < 1e-5);
        // Where it is foreseen to end: the unit, else the solid world, else its reach.
        let mut aim = Aim {
            ray: Some(ray),
            world_distance: Some(30.0),
            solid_distance: Some(30.0),
            ..Default::default()
        };
        assert!((bolt_predicted_end(&aim).unwrap().z + 30.0).abs() < 1e-4);
        aim.bolt_target = Some(crate::aim::AimTarget {
            entity: Entity::PLACEHOLDER,
            guid: 1,
            distance: 12.0,
            player: false,
            service: false,
        });
        assert!((bolt_predicted_end(&aim).unwrap().z + 12.0).abs() < 1e-4);
        aim.bolt_target = None;
        aim.world_distance = None;
        aim.solid_distance = None;
        assert!((bolt_predicted_end(&aim).unwrap().z + BOLT_DRAW_REACH).abs() < 1e-4);
        aim.ray = None;
        assert!(bolt_predicted_end(&aim).is_none());
    }

    #[test]
    fn a_bolt_is_foreseen_through_a_bush_to_what_stands_behind_it() {
        // The bush bug: the crosshair's pick stops at a bush 8 yd off (drawn, no collision hull),
        // so the bolt was drawn into the bush. The solid world is 40 yd off, an enemy 25.
        let ray = Ray3d::new(Vec3::new(0.0, 1.6, 0.0), Dir3::NEG_Z);
        let enemy = crate::aim::AimTarget {
            entity: Entity::PLACEHOLDER,
            guid: 9,
            distance: 25.0,
            player: false,
            service: false,
        };
        let mut aim = Aim {
            ray: Some(ray),
            world_distance: Some(8.0),
            solid_distance: Some(40.0),
            target: None,
            bolt_target: Some(enemy),
            ..Default::default()
        };
        assert!((bolt_predicted_end(&aim).unwrap().z + 25.0).abs() < 1e-4);
        aim.bolt_target = None;
        assert!((bolt_predicted_end(&aim).unwrap().z + 40.0).abs() < 1e-4);
        // No range: far past the old 43-yard draw.
        aim.solid_distance = Some(150.0);
        assert!((bolt_predicted_end(&aim).unwrap().z + 150.0).abs() < 1e-4);
    }

    #[test]
    fn a_homing_course_reaims_the_bolt_we_drew_and_a_fizzle_says_so() {
        let me = Some(0x42);
        let mut own = OwnBolts::default();
        let first = own.launch(133);
        let second = own.launch(133);
        // The server's homing course of ours (its id 5): it names our oldest unnamed one, and
        // keeps naming it as it turns.
        let course = decode_bolt_event(&launch_body(5, 0x42, BOLT_HOMING)).unwrap();
        assert_eq!(bolt_key(&course, me, &mut own), Some(first));
        assert_eq!(bolt_key(&course, me, &mut own), Some(first));
        // Another homing bolt of ours (id 6): the next one.
        let other = decode_bolt_event(&launch_body(6, 0x42, BOLT_HOMING)).unwrap();
        assert_eq!(bolt_key(&other, me, &mut own), Some(second));
        // The impacts end the one each id named, whatever order they come in.
        let hit6 = decode_bolt_event(&impact_body(6, 0x42, 0, 0)).unwrap();
        assert_eq!(bolt_key(&hit6, me, &mut own), Some(second));
        let fizzle5 = decode_bolt_event(&impact_body(5, 0x42, 0, BOLT_SPENT | BOLT_FIZZLED)).unwrap();
        assert_eq!(fizzle5.flags & BOLT_FIZZLED, BOLT_FIZZLED);
        assert_eq!(bolt_key(&fizzle5, me, &mut own), Some(first));
        assert!(own.flying.is_empty());
        // A homing course with nothing drawn for it (an instant had none): by its id.
        assert_eq!(bolt_key(&course, me, &mut own), Some(5));
        // Someone else's homing bolt: by id, every course.
        let theirs = decode_bolt_event(&launch_body(11, 0x77, BOLT_HOMING)).unwrap();
        assert_eq!(bolt_key(&theirs, me, &mut own), Some(11));
    }

    #[test]
    fn sprays_go_out_and_come_in_as_the_server_has_them() {
        let a = SprayAim {
            spell: 10161,
            hand: HAND_RIGHT_BUTTON,
            on: true,
            origin: [1.0, 2.0, 3.0],
            dir: [0.0, 1.0, 0.0],
        };
        let b = encode_spray_aim(&a);
        // [43, spell u32, hand u8, on u8, origin xyz, dir xyz]: 31 bytes.
        assert_eq!(b.len(), 31);
        assert_eq!(b[0], OP_SPRAY_AIM);
        assert_eq!(&b[1..5], &10161u32.to_le_bytes());
        assert_eq!((b[5], b[6]), (HAND_RIGHT_BUTTON, 1));
        assert_eq!(&b[27..31], &0.0f32.to_le_bytes());
        assert_eq!(encode_spray_aim(&SprayAim { on: false, ..a })[6], 0);

        let mut e = vec![OP_SPRAY];
        e.extend_from_slice(&0x77u64.to_le_bytes());
        e.extend_from_slice(&2948u32.to_le_bytes());
        e.extend_from_slice(&[1, 1]);
        for v in [5.0f32, 6.0, 7.0, 1.0, 0.0, 0.0, 10.0, 20.0] {
            e.extend_from_slice(&v.to_le_bytes());
        }
        assert_eq!(e.len(), 47);
        let s = decode_spray_event(&e).expect("spray");
        assert_eq!((s.caster, s.spell, s.hand, s.on), (0x77, 2948, 1, true));
        assert_eq!((s.from, s.range, s.half_angle_deg), ([5.0, 6.0, 7.0], 10.0, 20.0));
        assert!(decode_spray_event(&e[..46]).is_none());
        assert!(decode_spray_event(&b).is_none());

        // The spray shape: no ring on the ground, its reach in the radius.
        let mut h = vec![OP_HAND_SPELLS, 1, 0];
        h.extend_from_slice(&2948u32.to_le_bytes());
        h.extend_from_slice(&0u16.to_le_bytes());
        h.extend_from_slice(&[6, 10]);
        let s = decode_hand_spells(&h).expect("hand spells");
        assert_eq!((s[0].shape, s[0].radius, s[0].charge_ms), (Shape::Spray, 10, 0));
        assert!(!s[0].shape.on_ground());
    }

    #[test]
    fn a_pouring_hand_glows_big() {
        let mut h = Hands::default();
        let rest = glow_scale(&h, 0, 1.0);
        h.spraying[0] = Some(1.0);
        assert!(glow_scale(&h, 0, 1.2) > rest + 1.0);
        assert_eq!(glow_scale(&h, 1, 1.2), rest);
    }

    #[test]
    fn the_free_aimed_and_ground_shapes_decode() {
        let mut b = vec![OP_HAND_SPELLS, 2, 0];
        b.extend_from_slice(&133u32.to_le_bytes());
        b.extend_from_slice(&600u16.to_le_bytes());
        b.extend_from_slice(&[4, 0]);
        b.extend_from_slice(&10u32.to_le_bytes());
        b.extend_from_slice(&900u16.to_le_bytes());
        b.extend_from_slice(&[5, 8]);
        let s = decode_hand_spells(&b).expect("hand spells");
        assert_eq!(s[0].shape, Shape::Missile);
        assert!(!s[0].shape.on_ground(), "a bolt shows no ring");
        assert_eq!((s[1].shape, s[1].radius), (Shape::Ground, 8));
        assert!(s[1].shape.on_ground(), "Blizzard's ring where it lands");
    }
}
