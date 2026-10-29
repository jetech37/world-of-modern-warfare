//! How a skyride looks and sounds (crate::riding flies it): the mount's own flight animations
//! picked from the flight's state, a body that banks into turns and eases its pitch, a rider who
//! leans with it, a camera that feels the speed, wind and trails at speed, a burst on Skyward
//! Ascent and Surge Forward, a puff of dust on landing, and wind that rises with the speed.
//!
//! - **Animations** ([`pose_for`], [`pick`]): benilla plays the mount's locomotion, which in GM
//!   flight is the swim (41/42), so a gryphon paddled through the air. Here the pose comes from
//!   the flight: flapping on a climb, an Ascent, a Surge or a stall, gliding in level flight and
//!   shallow dives, a tucked dive when steep, a take-off as the skyride begins on the ground and a
//!   landing as it ends there. Each pose names its Wrath animation ids in order of preference,
//!   so a model without one falls to the next (every Wrath flyer has `Fly` 135 or its swim);
//!   benilla's [`LayerMountAnim`] seam plays the pick, cross-faded.
//! - **Body** ([`Tilt`]): a roll toward the turn from the facing's turn rate, and the pitch the
//!   mover flew eased, both spring-damped, written through [`LayerMountTilt`] about a pivot at
//!   the body's middle; the rider sits on the mount, so they lean with it, and a little more.
//! - **Camera** ([`camera_feel`]): a slight roll with the bank, a shake and (third person) a
//!   pull-back that grow with the speed; the view's widening is crate::riding's.
//! - **Effects**: wind streaks around the eye at speed, vapour from the wingtips at high speed,
//!   our own cards; the burst and the dust are stock WoW effect models ([`FusionEffect`]).
//! - **Sound**: a wind loop whose gain follows the speed, a wing beat on Ascent, stock 1.12 kits
//!   by name through benilla's [`LayerSound`].
//!
//! Per line tuning is [`LINES`], by the ridden display.

use benilla_app::Objects;
use benilla_app::fusion::{FusionEffect, LayerMountAnim, LayerMountTilt, LayerSound, MountAnimRequest};
use benilla_world::rig_anim::RigPose;
use benilla_world::view::WorldCamera;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::riding::{MAX_SPEED, Sky, STALL_SPEED};

/// `AnimationData.dbc` ids (3.3.5) the flying lines carry.
pub mod anim {
    pub const RUN: u16 = 5;
    pub const JUMP_START: u16 = 37;
    /// The riding drakes' wings-out loop.
    pub const JUMP: u16 = 38;
    pub const JUMP_END: u16 = 39;
    /// Wings spread, falling: the feathered lines' glide.
    pub const FALL: u16 = 40;
    pub const SWIM_IDLE: u16 = 41;
    /// What Wrath plays for a flying mount going forward: the wing beat.
    pub const SWIM: u16 = 42;
    pub const SWIM_LEFT: u16 = 43;
    pub const SWIM_RIGHT: u16 = 44;
    /// The taxi's flight loop.
    pub const FLY: u16 = 135;
    pub const JUMP_LAND_RUN: u16 = 187;
    pub const HOVER: u16 = 193;
    pub const FLY_STAND: u16 = 229;
    pub const FLY_WALK: u16 = 233;
    pub const FLY_RUN: u16 = 234;
    pub const FLY_SWIM_IDLE: u16 = 270;
    pub const FLY_SWIM: u16 = 271;
    /// The proto-drake's and the wyrm's take-off and landing.
    pub const TO_FLY: u16 = 458;
    pub const FLY_TO_GROUND: u16 = 463;
}

/// What the mount is doing in the air.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Pose {
    TakeOff,
    /// Wings beating: a climb, an Ascent or a Surge, a stall.
    Flap,
    /// Beating through a hard turn: the swim's banking variants.
    FlapLeft,
    FlapRight,
    /// Slow, nearly still: beating to hold the air.
    Hover,
    /// Wings out, still: level flight and shallow dives.
    Glide,
    /// Tucked: a steep dive.
    Dive,
    Land,
}

/// The flight's state, as [`pose_for`] reads it.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FlightState {
    pub speed: f32,
    /// Radians, nose up positive.
    pub pitch: f32,
    /// A Skyward Ascent still climbing.
    pub ascending: bool,
    /// Seconds since the last Surge Forward (large: none lately).
    pub since_surge: f32,
    /// The facing's turn rate, radians a second, left positive.
    pub turn: f32,
}

/// Below this pitch (radians) a dive tucks the wings.
pub const DIVE_PITCH: f32 = -0.45;
/// Above this pitch a climb beats the wings.
pub const CLIMB_PITCH: f32 = 0.18;
/// A Surge beats the wings this long (seconds).
pub const SURGE_FLAP: f32 = 0.9;
/// A beat through a turn this hard (radians a second) plays its banking clip.
pub const TURN_FLAP: f32 = 0.9;
/// Under this speed the mount hovers.
pub const HOVER_SPEED: f32 = 4.0;

/// The airborne pose for a flight state.
pub fn pose_for(s: FlightState) -> Pose {
    let flap = if s.turn > TURN_FLAP {
        Pose::FlapLeft
    } else if s.turn < -TURN_FLAP {
        Pose::FlapRight
    } else {
        Pose::Flap
    };
    if s.ascending || s.since_surge < SURGE_FLAP {
        Pose::Flap
    } else if s.pitch < DIVE_PITCH {
        Pose::Dive
    } else if s.speed < HOVER_SPEED {
        Pose::Hover
    } else if s.pitch > CLIMB_PITCH || s.speed < STALL_SPEED {
        flap
    } else {
        Pose::Glide
    }
}

/// How far (radians) past a pose's pitch edge the flight must go to leave it, so a climb or a
/// dive at the edge does not flicker between clips.
pub const POSE_HOLD: f32 = 0.08;

/// [`pose_for`] with a little hold: a flap at the climb's edge, a dive at the dive's, stays until
/// the pitch is clearly past it.
pub fn pose_held(prev: Option<Pose>, s: FlightState) -> Pose {
    let next = pose_for(s);
    match (prev, next) {
        (Some(p @ (Pose::Flap | Pose::FlapLeft | Pose::FlapRight)), Pose::Glide)
            if s.pitch > CLIMB_PITCH - POSE_HOLD =>
        {
            p
        }
        (Some(Pose::Dive), Pose::Glide) if s.pitch < DIVE_PITCH + POSE_HOLD => Pose::Dive,
        _ => next,
    }
}

/// Each pose's anim ids, best first, from the survey of the lines' models: Wrath's riding models
/// fly on the swim ids (42 the beat, 43/44 its banks, 41/193 the hover) with Fall (40, the
/// feathered lines) or Jump (38, the drakes) wings out; the proto-drake and wyrm carry the
/// `Fly*` set (229-271) and their own take-off (458) and landing (463).
pub fn candidates(pose: Pose, line: &LineTuning) -> Vec<u16> {
    use anim::*;
    let mut v: Vec<u16> = match pose {
        Pose::TakeOff => vec![TO_FLY, JUMP_START],
        Pose::Flap => vec![SWIM, FLY_RUN, FLY, RUN],
        Pose::FlapLeft => vec![SWIM_LEFT, SWIM, FLY_RUN, FLY, RUN],
        Pose::FlapRight => vec![SWIM_RIGHT, SWIM, FLY_RUN, FLY, RUN],
        Pose::Hover => vec![HOVER, SWIM_IDLE, FLY_STAND, FLY_SWIM_IDLE, SWIM, FLY],
        Pose::Glide => vec![FALL, JUMP, FLY_WALK, FLY_SWIM, SWIM, FLY],
        Pose::Dive => vec![FALL, JUMP, FLY_SWIM, FLY_WALK, SWIM, FLY],
        Pose::Land => vec![FLY_TO_GROUND, JUMP_END, JUMP_LAND_RUN],
    };
    if let Some(&(_, id)) = line.prefer.iter().find(|(p, _)| *p == pose) {
        v.insert(0, id);
    }
    v
}

/// The clip to play for a pose: the first candidate the model has, `(id, seconds, looping)`.
pub fn pick(pose: Pose, line: &LineTuning, clips: &[(u16, f32, bool)]) -> Option<(u16, f32, bool)> {
    candidates(pose, line)
        .into_iter()
        .find_map(|id| clips.iter().find(|c| c.0 == id).copied())
}

/// How fast a pose's clip plays: a Flap beats faster on an Ascent and with the speed, a Glide
/// a little with the speed, the rest as authored.
pub fn rate_for(pose: Pose, s: FlightState, line: &LineTuning) -> f32 {
    let k = (s.speed / MAX_SPEED).clamp(0.0, 1.0);
    match pose {
        Pose::Flap | Pose::FlapLeft | Pose::FlapRight if s.ascending => line.flap_rate * 1.45,
        Pose::Flap | Pose::FlapLeft | Pose::FlapRight => line.flap_rate * (0.95 + 0.35 * k),
        Pose::Glide => 0.85 + 0.3 * k,
        _ => 1.0,
    }
}

/// Per flying line, by the ridden display (`client/src/wotlk/import.rs` `LOOKS`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LineTuning {
    pub displays: &'static [u32],
    /// The deepest bank (radians).
    pub bank: f32,
    /// The flap clip's rate at cruise.
    pub flap_rate: f32,
    /// The tilt's pivot above the feet (yards): the body's middle.
    pub pivot: f32,
    /// Half the wingspan (yards), where the vapour trails start.
    pub wingtip: f32,
    /// A pose's own first choice, where the model's best clip is not the common one.
    pub prefer: &'static [(Pose, u16)],
    /// The wing beat's `SoundEntries` kit.
    pub flap_kit: &'static str,
}

pub const DEFAULT_LINE: LineTuning = LineTuning {
    displays: &[],
    bank: 0.55,
    flap_rate: 1.0,
    pivot: 1.4,
    wingtip: 3.0,
    prefer: &[],
    flap_kit: "WingFlapClose",
};

pub const LINES: &[LineTuning] = &[
    // Gryphon, wind rider, hippogryph: feathered, quick beats, banks hard.
    LineTuning { displays: &[40019, 40020], bank: 0.6, flap_rate: 1.0, pivot: 1.4, wingtip: 2.6, ..DEFAULT_LINE },
    LineTuning { displays: &[40021, 40022], bank: 0.6, flap_rate: 1.0, pivot: 1.4, wingtip: 2.8, flap_kit: "WyvernWingFlap", ..DEFAULT_LINE },
    LineTuning { displays: &[40023], bank: 0.62, flap_rate: 1.05, pivot: 1.4, wingtip: 2.6, ..DEFAULT_LINE },
    // Dragonhawk: light, fast beats.
    LineTuning { displays: &[40024, 40025, 40026], bank: 0.65, flap_rate: 1.15, pivot: 1.3, wingtip: 2.6, ..DEFAULT_LINE },
    // The drakes and the wyrm: big, slow, steady.
    LineTuning { displays: &[40027, 40028, 40029], bank: 0.5, flap_rate: 0.9, pivot: 1.6, wingtip: 3.6, flap_kit: "WingFlapHuge", ..DEFAULT_LINE },
    LineTuning { displays: &[40030, 40031, 40032], bank: 0.5, flap_rate: 0.9, pivot: 1.7, wingtip: 3.6, flap_kit: "WingFlapHuge", ..DEFAULT_LINE },
    LineTuning { displays: &[40033, 40034, 40035], bank: 0.42, flap_rate: 0.8, pivot: 1.9, wingtip: 4.5, flap_kit: "WingFlapHuge", ..DEFAULT_LINE },
    LineTuning { displays: &[40036], bank: 0.6, flap_rate: 1.0, pivot: 1.5, wingtip: 3.2, ..DEFAULT_LINE },
    LineTuning { displays: &[40037], bank: 0.48, flap_rate: 0.85, pivot: 1.7, wingtip: 3.8, flap_kit: "WingFlapHuge", ..DEFAULT_LINE },
];

pub fn line_for(display: u32) -> &'static LineTuning {
    LINES
        .iter()
        .find(|l| l.displays.contains(&display))
        .unwrap_or(&DEFAULT_LINE)
}

/// A critically damped spring toward `target`: `(value, velocity)` after `dt`, `omega` its
/// stiffness (1/s). Never overshoots, never lags for ever.
pub fn spring(value: f32, vel: f32, target: f32, omega: f32, dt: f32) -> (f32, f32) {
    let x = value - target;
    let e = (-omega * dt).exp();
    let new_x = (x + (vel + omega * x) * dt) * e;
    let new_v = (vel - omega * (vel + omega * x) * dt) * e;
    (target + new_x, new_v)
}

/// The bank a turn asks for: roll toward the turn (right positive) from the facing's turn rate
/// (radians a second, left positive), deeper the faster it flies, never past `max`.
pub fn bank_for(turn_rate: f32, speed: f32, max: f32) -> f32 {
    let k = (speed / 25.0).clamp(0.3, 1.0);
    (-turn_rate * 0.45 * k).clamp(-max, max)
}

/// The mount body's eased roll, pitch and yaw lead.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Tilt {
    pub roll: f32,
    pub roll_v: f32,
    pub pitch: f32,
    pub pitch_v: f32,
    /// The nose swung a little into the turn (radians, left positive).
    pub yaw: f32,
    pub yaw_v: f32,
}

impl Tilt {
    /// The tilt's turn in the host's frame: the host is already at the flown pitch, so this
    /// undoes it, then (about the level up) swings the nose into the turn, pitches to the eased
    /// pitch and rolls: host * local = facing * yaw lead * pitch * roll.
    pub fn local(&self, flown_pitch: f32) -> Quat {
        Quat::from_rotation_x(-flown_pitch)
            * Quat::from_rotation_y(self.yaw)
            * Quat::from_rotation_x(self.pitch)
            * Quat::from_rotation_z(-self.roll)
    }
}

/// How much of the flight's attitude shows at a speed (yards a second along the path): none
/// hovering or nearly still, all of it from a slow cruise up, smoothstepped between.
pub fn attitude_weight(path_speed: f32) -> f32 {
    let x = ((path_speed - HOVER_SPEED * 0.5) / (HOVER_SPEED * 2.0)).clamp(0.0, 1.0);
    x * x * (3.0 - 2.0 * x)
}

/// The nose the flight asks for (radians, up positive): mostly the path flown, so an Ascent or a
/// pull-up noses up and a dive noses down even before the aim gets there, the rest the aim,
/// levelled toward a hover and kept short of vertical.
pub fn pitch_target(aim: f32, forward: f32, up: f32) -> f32 {
    let path_speed = (forward * forward + up * up).sqrt();
    let path = if path_speed > 0.5 { up.atan2(forward.max(0.0)) } else { aim };
    ((aim * 0.35 + path * 0.65) * attitude_weight(path_speed)).clamp(-1.15, 1.0)
}

/// The nose's swing into a turn from the facing's turn rate (left positive), small: the bank
/// carries the turn, this only keeps the nose from looking dragged.
pub fn yaw_lead(turn_rate: f32) -> f32 {
    (turn_rate * 0.06).clamp(-0.12, 0.12)
}

/// The camera's speed feel: `(roll radians, shake degrees, pull-back yards)`.
pub fn camera_feel(speed: f32, bank: f32) -> (f32, f32, f32) {
    let k = ((speed - 20.0) / (MAX_SPEED - 20.0)).clamp(0.0, 1.0);
    (bank * 0.22, 0.35 * k * k, 2.5 * k)
}

/// The wind loop's gain at a speed: silent at a trot, full at the fastest dive.
pub fn wind_gain(speed: f32) -> f32 {
    ((speed - 8.0) / (MAX_SPEED - 8.0)).clamp(0.0, 1.0).powf(1.3)
}

/// The skyride's look, one phase machine.
#[derive(Resource, Debug, Default)]
pub struct MountFlight {
    phase: Option<Pose>,
    /// Seconds into a one-shot (take-off, landing).
    oneshot_left: f32,
    serial: u32,
    since_surge: f32,
    was_flying: bool,
    was_ascending: bool,
    last_speed: f32,
    last_yaw: Option<f32>,
    turn_rate: f32,
    pub tilt: Tilt,
    /// A rush of streaks still to throw: `Some(true)` an Ascent's, `Some(false)` a Surge's.
    rush: Option<bool>,
    /// The camera's eased pull-back.
    pull: f32,
    streak_clock: f32,
    trail_clock: f32,
}

/// The pose machine's step: what to play now, from the last phase and this frame's facts.
/// `flying`: the skyride is on; `grounded`: the mover stands on ground. Returns the next phase
/// (`None`: hand the mount back to its locomotion).
pub fn next_phase(
    phase: Option<Pose>,
    oneshot_left: f32,
    flying: bool,
    was_flying: bool,
    grounded: bool,
    state: FlightState,
) -> Option<Pose> {
    match (flying, was_flying) {
        // Up: from the ground a take-off first.
        (true, false) => Some(if grounded { Pose::TakeOff } else { pose_for(state) }),
        (true, true) => match phase {
            Some(Pose::TakeOff) if oneshot_left > 0.0 && !state.ascending => Some(Pose::TakeOff),
            // An Ascent off the ground keeps the take-off's own clip until it ends.
            Some(Pose::TakeOff) if oneshot_left > 0.0 => Some(Pose::TakeOff),
            _ => Some(pose_held(phase, state)),
        },
        // Down on the ground: the landing plays out.
        (false, true) => grounded.then_some(Pose::Land),
        (false, false) => match phase {
            Some(Pose::Land) if oneshot_left > 0.0 => Some(Pose::Land),
            _ => None,
        },
    }
}

pub struct MountFlightPlugin;

impl Plugin for MountFlightPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<MountFlight>()
            .add_systems(Update, (animate_mount, effects).chain().in_set(Mw2Set::Hud))
            .add_systems(
                Update,
                feel_camera
                    .after(crate::stance_view::apply_stance_view)
                    .after(crate::blast::shake_view)
                    .before(Mw2Set::Aim),
            )
            .add_systems(Update, (drift_streaks, lean_rider));
    }
}

/// The ridden display of our unit (0 on foot).
fn ridden(objects: &Objects, state: &crate::mode::FpsState) -> (Option<Entity>, u32) {
    let me = objects.player();
    let display = state
        .self_guid
        .and_then(|g| objects.object(g))
        .map_or(0, |f| f.unit_mount_display_id());
    (me, display)
}

#[allow(clippy::too_many_arguments)]
fn animate_mount(
    time: Res<Time>,
    sky: Res<Sky>,
    seam: Res<benilla_app::fusion::LayerFlight>,
    objects: Objects,
    fps: Res<crate::mode::FpsState>,
    bodies: Query<&Transform, Without<WorldCamera>>,
    mut mf: ResMut<MountFlight>,
    mut anim: ResMut<LayerMountAnim>,
    mut tilt: ResMut<LayerMountTilt>,
    mut sound: ResMut<LayerSound>,
    mut fx: MessageWriter<FusionEffect>,
) {
    let dt = time.delta_secs();
    // A capture probe drives the seams itself.
    if anim.any_rider {
        return;
    }
    let (me, display) = ridden(&objects, &fps);
    if display == 0 || me.is_none() {
        if anim.play.is_some() {
            anim.play = None;
        }
        if tilt.0.is_some() {
            tilt.0 = None;
        }
        sound.loop_kit = None;
        *mf = MountFlight::default();
        return;
    }
    let line = line_for(display);
    let flying = sky.flying;
    let speed = if flying { sky.body.speed } else { 0.0 };
    let ascending = sky.body.ascent_left > 0.0;
    // A Surge shows as a jump in speed the physics alone never makes in a frame.
    if flying && speed - mf.last_speed > crate::riding::SURGE_SPEED * 0.6 {
        mf.since_surge = 0.0;
        mf.rush = Some(false);
        burst(&mut fx, &bodies, me, false);
        sound.once.push((line.flap_kit.into(), 1.0));
    } else {
        mf.since_surge += dt;
    }
    if ascending && !mf.was_ascending {
        mf.rush = Some(true);
        burst(&mut fx, &bodies, me, true);
        sound.once.push((line.flap_kit.into(), 1.0));
    }
    // The pose reads the nose the body will take (the path flown, mostly), not the bare aim, so
    // a dive tucks as it dives and a pull-up beats as it climbs.
    let state = FlightState {
        speed,
        pitch: if flying && seam.levitating {
            pitch_target(seam.pitch, seam.forward, seam.up)
        } else {
            seam.pitch
        },
        ascending,
        since_surge: mf.since_surge,
        turn: mf.turn_rate,
    };
    let grounded = seam.grounded || !seam.levitating;
    let phase = next_phase(mf.phase, mf.oneshot_left, flying, mf.was_flying, grounded, state);
    if phase == Some(Pose::Land) && mf.phase != Some(Pose::Land) {
        dust(&mut fx, &bodies, me);
    }
    mf.oneshot_left -= dt;
    if phase != mf.phase {
        if let Some(p @ (Pose::TakeOff | Pose::Land)) = phase {
            mf.oneshot_left = pick(p, line, &anim.clips).map_or(0.0, |c| c.1.min(1.6));
        }
    }
    mf.phase = phase;
    mf.was_flying = flying;
    mf.was_ascending = ascending;
    mf.last_speed = speed;

    anim.play = phase.and_then(|p| {
        let (id, _, looping) = pick(p, line, &anim.clips)?;
        let oneshot = matches!(p, Pose::TakeOff | Pose::Land);
        Some(MountAnimRequest {
            id,
            looping: looping && !oneshot,
            rate: rate_for(p, state, line),
            blend: if oneshot { 0.2 } else { 0.35 },
            serial: mf.serial,
        })
    });

    // The body: banked into the turn, its pitch eased.
    let yaw = me
        .and_then(|e| bodies.get(e).ok())
        .map(|t| t.rotation.to_euler(EulerRot::YXZ).0);
    if let (Some(y), Some(last)) = (yaw, mf.last_yaw) {
        let d = (y - last + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU)
            - std::f32::consts::PI;
        let raw = if dt > 0.0 { d / dt } else { 0.0 };
        mf.turn_rate += (raw - mf.turn_rate) * (1.0 - (-10.0 * dt).exp());
    }
    mf.last_yaw = yaw;
    // Grounded (a landing, the take-off's first steps) it levels; nearly still it hovers level.
    let in_air = flying && seam.levitating && !seam.grounded;
    let path_speed = (seam.forward * seam.forward + seam.up * seam.up).sqrt();
    let w = attitude_weight(path_speed.max(speed));
    let (bank_target, nose_target, yaw_target) = if in_air {
        (
            bank_for(mf.turn_rate, speed, line.bank) * w,
            pitch_target(seam.pitch, seam.forward, seam.up),
            yaw_lead(mf.turn_rate) * w,
        )
    } else {
        (0.0, 0.0, 0.0)
    };
    let t = mf.tilt;
    let (roll, roll_v) = spring(t.roll, t.roll_v, bank_target, 5.0, dt);
    let (pitch, pitch_v) = spring(t.pitch, t.pitch_v, nose_target, 6.0, dt);
    let (yaw, yaw_v) = spring(t.yaw, t.yaw_v, yaw_target, 6.0, dt);
    mf.tilt = Tilt {
        roll,
        roll_v,
        pitch,
        pitch_v,
        yaw,
        yaw_v,
    };
    let flown = if seam.levitating { seam.pitch } else { 0.0 };
    let settled = !in_air
        && roll.abs() < 1e-3
        && yaw.abs() < 1e-3
        && (pitch - flown).abs() < 1e-3;
    tilt.0 = (!settled).then(|| (mf.tilt.local(flown), Vec3::Y * line.pivot));

    // The wind.
    if flying {
        sound.loop_kit = Some(WIND_KIT.into());
        sound.loop_gain = wind_gain(speed);
    } else {
        sound.loop_kit = None;
    }
}

/// The wind loop: the taxi flight's own wind bed.
const WIND_KIT: &str = "EnvelopingWindLoop";
/// Ascent's burst and Surge's: stock spell art.
const BURST_UP: &str = "Spells\\Cyclone_State.mdx";
const BURST_FORWARD: &str = "Spells\\DustNova_Cast_Base.mdx";
const DUST: &str = "Spells\\DustCloud_Land.mdx";

fn burst(
    fx: &mut MessageWriter<FusionEffect>,
    bodies: &Query<&Transform, Without<WorldCamera>>,
    me: Option<Entity>,
    up: bool,
) {
    let Some(t) = me.and_then(|e| bodies.get(e).ok()) else {
        return;
    };
    fx.write(FusionEffect::Play {
        key: 0,
        path: if up { BURST_UP } else { BURST_FORWARD }.into(),
        parent: None,
        transform: Transform::from_translation(t.translation).with_rotation(t.rotation),
        looping: false,
        size: 1.0,
        ground: false,
    });
}

fn dust(
    fx: &mut MessageWriter<FusionEffect>,
    bodies: &Query<&Transform, Without<WorldCamera>>,
    me: Option<Entity>,
) {
    let Some(t) = me.and_then(|e| bodies.get(e).ok()) else {
        return;
    };
    fx.write(FusionEffect::Play {
        key: 0,
        path: DUST.into(),
        parent: None,
        transform: Transform::from_translation(t.translation),
        looping: false,
        size: 1.0,
        ground: true,
    });
}

/// The camera: a roll with the bank, a shake and a pull-back with the speed.
fn feel_camera(
    time: Res<Time>,
    sky: Res<Sky>,
    third: Res<crate::thirdperson::ThirdPerson>,
    mut mf: ResMut<MountFlight>,
    mut cameras: Query<&mut Transform, With<WorldCamera>>,
) {
    let dt = time.delta_secs();
    let speed = if sky.flying { sky.body.speed } else { 0.0 };
    let (roll, shake, pull) = camera_feel(speed, mf.tilt.roll);
    let pull_target = if third.on { pull } else { 0.0 };
    mf.pull += (pull_target - mf.pull) * (1.0 - (-2.5 * dt).exp());
    if !sky.flying && mf.pull < 1e-3 && mf.tilt.roll.abs() < 1e-3 {
        return;
    }
    let t = time.elapsed_secs();
    let (sp, sy) = (
        ((t * 31.0).sin() + (t * 17.3).sin() * 0.5) * shake,
        ((t * 23.0).cos() + (t * 41.0).sin() * 0.4) * shake * 0.6,
    );
    for mut cam in &mut cameras {
        let back = cam.rotation * Vec3::Z;
        cam.translation += back * mf.pull;
        cam.rotate_local(Quat::from_euler(
            EulerRot::YXZ,
            sy.to_radians(),
            sp.to_radians(),
            -roll,
        ));
    }
}

/// The rider leans into the bank a little past the mount.
fn lean_rider(
    mf: Res<MountFlight>,
    objects: Objects,
    rigs: Query<&RigPose>,
    mut anchors: Query<&mut Transform, Without<WorldCamera>>,
) {
    let Some(me) = objects.player() else {
        return;
    };
    let Ok(pose) = rigs.get(me) else {
        return;
    };
    if pose.joints_root == me {
        return;
    }
    if let Ok(mut t) = anchors.get_mut(pose.joints_root) {
        let want = Quat::from_rotation_z(-mf.tilt.roll * 0.35);
        if t.rotation.angle_between(want) > 1e-4 {
            t.rotation = want;
        }
    }
}

// ── Wind streaks, rushes and vapour: our own cards ──

#[derive(Component)]
struct Streak {
    born: f32,
    life: f32,
    vel: Vec3,
    len: f32,
    /// The card's widest (yards), at the top of its fade.
    width: f32,
}

#[derive(Resource)]
struct StreakLook {
    mesh: Handle<Mesh>,
    /// Faint, middling and bright wind: the faster the flight, the brighter the streaks drawn.
    wind: [Handle<StandardMaterial>; 3],
    vapour: Handle<StandardMaterial>,
}

/// How fast the flight is for its effects, 0 at a cruise, 1 at the fastest dive.
pub fn speed_k(speed: f32) -> f32 {
    ((speed - 18.0) / (MAX_SPEED - 18.0)).clamp(0.0, 1.0)
}

/// How many streaks a second at a speed: none at a cruise, a stream at the fastest dive.
pub fn streak_rate(speed: f32) -> f32 {
    let k = speed_k(speed);
    k * (25.0 + 65.0 * k)
}

/// One wind streak's look at a speed: `(seconds, length, width, wind tier)`; longer, thicker,
/// shorter-lived and brighter the faster it flies.
pub fn streak_look(speed: f32) -> (f32, f32, f32, usize) {
    let k = speed_k(speed);
    let tier = if k > 0.7 {
        2
    } else if k > 0.35 {
        1
    } else {
        0
    };
    (0.42 - 0.14 * k, 0.6 + 2.6 * k, 0.025 + 0.04 * k, tier)
}

/// Wingtip vapour a second at a speed (each tip): from a fast glide up, thicker in a dive.
pub fn vapour_rate(speed: f32) -> f32 {
    let k = ((speed - 30.0) / (MAX_SPEED - 30.0)).clamp(0.0, 1.0);
    if k <= 0.0 { 0.0 } else { 18.0 + 40.0 * k }
}

/// A rush's streak count: a Surge throws the air past the eye, an Ascent down past the body.
pub const RUSH_STREAKS: usize = 36;

#[allow(clippy::too_many_arguments)]
fn effects(
    mut commands: Commands,
    time: Res<Time>,
    sky: Res<Sky>,
    mut mf: ResMut<MountFlight>,
    objects: Objects,
    fps: Res<crate::mode::FpsState>,
    look: Option<Res<StreakLook>>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    bodies: Query<&Transform, Without<WorldCamera>>,
    meshes: Option<ResMut<Assets<Mesh>>>,
    materials: Option<ResMut<Assets<StandardMaterial>>>,
) {
    let Some(look) = look else {
        if let (Some(mut meshes), Some(mut materials)) = (meshes, materials) {
            let mut card = |a: f32| {
                materials.add(StandardMaterial {
                    base_color: Color::srgba(0.86, 0.91, 1.0, a),
                    unlit: true,
                    alpha_mode: AlphaMode::Add,
                    cull_mode: None,
                    fog_enabled: false,
                    ..default()
                })
            };
            let wind = [card(0.10), card(0.17), card(0.26)];
            let vapour = card(0.2);
            commands.insert_resource(StreakLook {
                mesh: meshes.add(Rectangle::new(1.0, 1.0)),
                wind,
                vapour,
            });
        }
        return;
    };
    if !sky.flying {
        mf.rush = None;
        return;
    }
    let (Some(cam), Some(me)) = (cameras.iter().next(), objects.player()) else {
        return;
    };
    let Ok(body) = bodies.get(me) else {
        return;
    };
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let speed = sky.body.speed;
    let fwd = (body.rotation * -Vec3::Z).normalize_or_zero();
    let (_, display) = ridden(&objects, &fps);
    let line = line_for(display);
    let mut seed = (now * 1000.0) as u32 ^ 0x9e37_79b9;
    let mut rnd = || {
        seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        (seed >> 8) as f32 / 16_777_216.0
    };
    // Streaks: thin cards streaming past the eye, against the flight.
    mf.streak_clock += dt * streak_rate(speed);
    let (life, len, width, tier) = streak_look(speed);
    while mf.streak_clock >= 1.0 {
        mf.streak_clock -= 1.0;
        let a = rnd() * std::f32::consts::TAU;
        // Kept off the middle of the view, out at the edges where the speed is felt.
        let r = 2.0 + rnd() * 4.5;
        let side = cam.right() * a.cos() * r + cam.up() * a.sin() * r * 0.7;
        let at = cam.translation() + cam.forward() * (6.0 + rnd() * 12.0) + side;
        commands.spawn((
            Streak {
                born: now,
                life: life * (0.8 + 0.4 * rnd()),
                vel: -fwd * speed * 1.4,
                len: len * (0.7 + 0.6 * rnd()),
                width,
            },
            Mesh3d(look.mesh.clone()),
            MeshMaterial3d(look.wind[tier].clone()),
            Transform::from_translation(at),
        ));
    }
    // A rush on Surge (past the eye, fast) and on Ascent (down around the body, the air left
    // below): brief, bright, many at once.
    if let Some(up) = mf.rush.take() {
        let mid = body.translation + Vec3::Y * line.pivot;
        for _ in 0..RUSH_STREAKS {
            let a = rnd() * std::f32::consts::TAU;
            let (at, vel) = if up {
                let r = line.wingtip * (0.6 + rnd() * 0.9);
                let ring = Vec3::new(a.cos() * r, 0.0, a.sin() * r);
                (mid + ring + Vec3::Y * (rnd() * 2.0), -Vec3::Y * (30.0 + rnd() * 20.0))
            } else {
                let r = 1.2 + rnd() * 3.5;
                let side = cam.right() * a.cos() * r + cam.up() * a.sin() * r * 0.75;
                (
                    cam.translation() + cam.forward() * (3.0 + rnd() * 10.0) + side,
                    -fwd * (speed + 45.0 + rnd() * 25.0),
                )
            };
            commands.spawn((
                Streak {
                    born: now,
                    life: 0.25 + rnd() * 0.2,
                    vel,
                    len: 2.0 + rnd() * 2.5,
                    width: 0.06,
                },
                Mesh3d(look.mesh.clone()),
                MeshMaterial3d(look.wind[2].clone()),
                Transform::from_translation(at),
            ));
        }
    }
    // Vapour off the wingtips at speed: a trail left hanging in the air behind.
    let rate = vapour_rate(speed) + if mf.since_surge < SURGE_FLAP { 30.0 } else { 0.0 };
    if rate > 0.0 {
        mf.trail_clock += dt * rate;
        let right = body.rotation * Vec3::X;
        // The tips ride the bank: the low wing down, the high one up.
        let lift = Vec3::Y * (mf.tilt.roll.sin() * line.wingtip);
        let mid = body.translation + Vec3::Y * line.pivot;
        let k = speed_k(speed);
        while mf.trail_clock >= 1.0 {
            mf.trail_clock -= 1.0;
            for s in [-1.0_f32, 1.0] {
                let jitter = rnd() * 0.6;
                commands.spawn((
                    Streak {
                        born: now,
                        life: 0.5 + 0.5 * k,
                        vel: -fwd * 1.5,
                        len: 0.35 + 0.9 * k,
                        width: 0.07 + 0.08 * k,
                    },
                    Mesh3d(look.mesh.clone()),
                    MeshMaterial3d(look.vapour.clone()),
                    Transform::from_translation(
                        mid + right * line.wingtip * s - lift * s - fwd * (0.4 + jitter),
                    ),
                ));
            }
        }
    }
}

fn drift_streaks(
    mut commands: Commands,
    time: Res<Time>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    mut streaks: Query<(Entity, &Streak, &mut Transform)>,
) {
    let now = time.elapsed_secs();
    let dt = time.delta_secs();
    let eye = cameras.iter().next().map(|c| c.translation());
    for (e, s, mut t) in &mut streaks {
        let k = (now - s.born) / s.life;
        if k >= 1.0 {
            commands.entity(e).despawn();
            continue;
        }
        t.translation += s.vel * dt;
        // Long along its way, facing the eye.
        let dir = s.vel.normalize_or_zero();
        if let Some(eye) = eye {
            let to_eye = (eye - t.translation).normalize_or_zero();
            let side = dir.cross(to_eye).normalize_or_zero();
            if side != Vec3::ZERO && dir != Vec3::ZERO {
                let normal = side.cross(dir);
                t.rotation = Quat::from_mat3(&Mat3::from_cols(side, dir, normal));
            }
        }
        t.scale = streak_scale(k, s.len, s.width);
    }
}

/// A streak's card scale `k` of the way through its life: it fades in fast, stretches, thins
/// and fades out.
pub fn streak_scale(k: f32, len: f32, width: f32) -> Vec3 {
    let fade = (1.0 - k) * (k * 6.0).min(1.0);
    Vec3::new(width * (0.35 + 0.65 * fade), len * (0.4 + fade), 1.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn s(speed: f32, pitch: f32) -> FlightState {
        FlightState {
            speed,
            pitch,
            ascending: false,
            since_surge: 99.0,
            turn: 0.0,
        }
    }

    #[test]
    fn the_pose_follows_the_flight() {
        assert_eq!(pose_for(s(30.0, 0.0)), Pose::Glide, "level cruise glides");
        assert_eq!(pose_for(s(30.0, -0.2)), Pose::Glide, "a shallow dive glides");
        assert_eq!(pose_for(s(40.0, -0.9)), Pose::Dive, "a steep dive tucks");
        assert_eq!(pose_for(s(30.0, 0.5)), Pose::Flap, "a climb beats");
        assert_eq!(pose_for(s(7.0, 0.0)), Pose::Flap, "a stall beats");
        assert_eq!(pose_for(s(2.0, 0.0)), Pose::Hover, "near still, it hovers");
        let mut turn = s(7.0, 0.3);
        turn.turn = 1.5;
        assert_eq!(pose_for(turn), Pose::FlapLeft, "a hard left beat banks its clip");
        turn.turn = -1.5;
        assert_eq!(pose_for(turn), Pose::FlapRight);
        let mut a = s(30.0, -0.9);
        a.ascending = true;
        assert_eq!(pose_for(a), Pose::Flap, "an Ascent beats, whatever the pitch");
        let mut surge = s(30.0, -0.9);
        surge.since_surge = 0.2;
        assert_eq!(pose_for(surge), Pose::Flap, "a Surge beats");
        surge.since_surge = SURGE_FLAP + 0.1;
        assert_eq!(pose_for(surge), Pose::Dive, "then the dive again");
    }

    #[test]
    fn a_missing_clip_falls_to_the_next() {
        let line = &DEFAULT_LINE;
        // The riding gryphon's set, as surveyed (Wrath's riding models fly on the swim ids).
        let gryphon = [
            (0, 2.0, true),
            (anim::SWIM_IDLE, 2.0, true),
            (anim::HOVER, 2.0, true),
            (anim::SWIM, 1.5, true),
            (anim::FLY, 1.5, true),
            (anim::FALL, 3.0, true),
            (anim::SWIM_LEFT, 1.5, true),
            (anim::SWIM_RIGHT, 1.5, true),
            (anim::JUMP_START, 0.7, false),
        ];
        assert_eq!(pick(Pose::Flap, line, &gryphon).unwrap().0, anim::SWIM);
        assert_eq!(pick(Pose::FlapLeft, line, &gryphon).unwrap().0, anim::SWIM_LEFT);
        assert_eq!(pick(Pose::Glide, line, &gryphon).unwrap().0, anim::FALL);
        assert_eq!(pick(Pose::Hover, line, &gryphon).unwrap().0, anim::HOVER);
        assert_eq!(pick(Pose::TakeOff, line, &gryphon).unwrap().0, anim::JUMP_START);
        // No landing clip at all: nothing, and the machine moves straight on.
        assert_eq!(pick(Pose::Land, line, &gryphon), None);
        // The riding proto-drake has neither swim nor fall: its own fly set.
        let proto = [
            (0, 4.0, true),
            (anim::FLY_STAND, 2.0, true),
            (anim::FLY_WALK, 2.0, true),
            (anim::FLY_RUN, 2.0, true),
            (anim::TO_FLY, 0.3, false),
            (anim::FLY_TO_GROUND, 0.8, false),
        ];
        assert_eq!(pick(Pose::Flap, line, &proto).unwrap().0, anim::FLY_RUN);
        assert_eq!(pick(Pose::FlapRight, line, &proto).unwrap().0, anim::FLY_RUN);
        assert_eq!(pick(Pose::Glide, line, &proto).unwrap().0, anim::FLY_WALK);
        assert_eq!(pick(Pose::Hover, line, &proto).unwrap().0, anim::FLY_STAND);
        assert_eq!(pick(Pose::TakeOff, line, &proto).unwrap().0, anim::TO_FLY);
        assert_eq!(pick(Pose::Land, line, &proto).unwrap().0, anim::FLY_TO_GROUND);
        // The riding drakes: wings out on their jump loop.
        let drake = [(anim::SWIM, 4.0, true), (anim::JUMP, 1.7, true), (anim::HOVER, 2.0, true)];
        assert_eq!(pick(Pose::Glide, line, &drake).unwrap().0, anim::JUMP);
        assert_eq!(pick(Pose::Dive, line, &drake).unwrap().0, anim::JUMP);
    }

    #[test]
    fn a_line_can_prefer_its_own_clip() {
        const L: LineTuning = LineTuning {
            prefer: &[(Pose::Glide, 999)],
            ..DEFAULT_LINE
        };
        let clips = [(999, 1.0, true), (anim::FALL, 1.0, true)];
        assert_eq!(pick(Pose::Glide, &L, &clips).unwrap().0, 999);
        assert_eq!(pick(Pose::Glide, &DEFAULT_LINE, &clips).unwrap().0, anim::FALL);
    }

    #[test]
    fn take_off_and_landing_bracket_the_flight() {
        let st = s(20.0, 0.0);
        // Up from the ground: a take-off; from a fall: straight to the air pose.
        assert_eq!(next_phase(None, 0.0, true, false, true, st), Some(Pose::TakeOff));
        assert_eq!(next_phase(None, 0.0, true, false, false, st), Some(Pose::Glide));
        // The take-off plays out, then the air pose.
        assert_eq!(next_phase(Some(Pose::TakeOff), 0.5, true, true, false, st), Some(Pose::TakeOff));
        assert_eq!(next_phase(Some(Pose::TakeOff), 0.0, true, true, false, st), Some(Pose::Glide));
        // Down on the ground: a landing, which plays out and hands the mount back.
        assert_eq!(next_phase(Some(Pose::Glide), 0.0, false, true, true, st), Some(Pose::Land));
        assert_eq!(next_phase(Some(Pose::Land), 0.4, false, false, true, st), Some(Pose::Land));
        assert_eq!(next_phase(Some(Pose::Land), 0.0, false, false, true, st), None);
        // Ended in the air (knocked off, out of time): no landing pose.
        assert_eq!(next_phase(Some(Pose::Glide), 0.0, false, true, false, st), None);
    }

    #[test]
    fn the_flap_beats_faster_on_an_ascent() {
        let line = &DEFAULT_LINE;
        let mut a = s(20.0, 0.0);
        let cruise = rate_for(Pose::Flap, a, line);
        a.ascending = true;
        assert!(rate_for(Pose::Flap, a, line) > cruise);
    }

    #[test]
    fn the_spring_settles_without_overshoot() {
        let (mut x, mut v) = (0.0, 0.0);
        let mut peak: f32 = 0.0;
        for _ in 0..240 {
            (x, v) = spring(x, v, 1.0, 6.0, 1.0 / 60.0);
            peak = peak.max(x);
        }
        assert!((x - 1.0).abs() < 1e-3, "settled at {x}");
        assert!(peak <= 1.0 + 1e-4, "no overshoot: {peak}");
    }

    #[test]
    fn a_turn_banks_toward_it_within_the_line() {
        // Turning left (positive rate) rolls left (negative), and never past the line's bank.
        assert!(bank_for(1.0, 30.0, 0.6) < 0.0);
        assert!(bank_for(-1.0, 30.0, 0.6) > 0.0);
        assert_eq!(bank_for(-50.0, 30.0, 0.6), 0.6);
        assert!(bank_for(-1.0, 5.0, 0.6) < bank_for(-1.0, 30.0, 0.6), "slower, shallower");
    }

    #[test]
    fn the_tilt_puts_the_eased_pitch_in_place_of_the_flown() {
        let t = Tilt { pitch: 0.2, ..default() };
        let q = Quat::from_rotation_x(0.5) * t.local(0.5);
        assert!(q.angle_between(Quat::from_rotation_x(0.2)) < 1e-5);
        // The yaw lead turns about the level up, not the pitched one.
        let t = Tilt { pitch: 0.3, yaw: 0.1, roll: 0.2, ..default() };
        let host = Quat::from_rotation_y(1.0) * Quat::from_rotation_x(-0.4);
        let want = Quat::from_rotation_y(1.1) * Quat::from_rotation_x(0.3) * Quat::from_rotation_z(-0.2);
        assert!((host * t.local(-0.4)).angle_between(want) < 1e-5);
    }

    #[test]
    fn the_nose_follows_the_path_and_levels_in_a_hover() {
        // Level cruise: level. A climb noses up, a dive down, an Ascent up before the aim does.
        assert!(pitch_target(0.0, 30.0, 0.0).abs() < 1e-5);
        assert!(pitch_target(0.4, 25.0, 10.0) > 0.3);
        assert!(pitch_target(-0.8, 30.0, -30.0) < -0.6);
        assert!(pitch_target(0.0, 15.0, 28.0) > 0.5, "an Ascent noses up");
        // Hovering: level whatever the aim; never past the limits.
        assert!(pitch_target(-1.2, 1.0, 0.0).abs() < 1e-5);
        assert!(pitch_target(-1.4, 10.0, -60.0) >= -1.15);
        assert_eq!(attitude_weight(0.0), 0.0);
        assert_eq!(attitude_weight(30.0), 1.0);
        assert!(yaw_lead(1.0) > 0.0 && yaw_lead(-50.0) == -0.12);
    }

    #[test]
    fn the_wind_and_the_camera_grow_with_the_speed() {
        assert_eq!(wind_gain(5.0), 0.0);
        assert!((wind_gain(MAX_SPEED) - 1.0).abs() < 1e-5);
        let (_, shake_slow, pull_slow) = camera_feel(15.0, 0.0);
        let (_, shake_fast, pull_fast) = camera_feel(MAX_SPEED, 0.0);
        assert_eq!((shake_slow, pull_slow), (0.0, 0.0));
        assert!(shake_fast > 0.0 && pull_fast > 2.0);
        assert_eq!(streak_rate(10.0), 0.0);
    }

    #[test]
    fn the_effects_scale_with_the_speed() {
        assert!(streak_rate(30.0) < streak_rate(MAX_SPEED));
        let (slow_life, slow_len, slow_w, slow_tier) = streak_look(22.0);
        let (fast_life, fast_len, fast_w, fast_tier) = streak_look(MAX_SPEED);
        assert!(fast_len > slow_len && fast_w > slow_w && fast_life < slow_life);
        assert!(slow_tier == 0 && fast_tier == 2);
        assert_eq!(vapour_rate(25.0), 0.0, "no vapour at a cruise");
        assert!(vapour_rate(MAX_SPEED) > vapour_rate(35.0));
        // A streak fades in and out, never wider than its width nor longer than its length.
        let mid = streak_scale(0.3, 2.0, 0.05);
        assert!(mid.x <= 0.05 + 1e-6 && mid.y <= 2.0 * 1.4 + 1e-6);
        assert!(streak_scale(0.0, 2.0, 0.05).y < mid.y);
    }

    #[test]
    fn a_pose_at_its_edge_holds() {
        let at = |pitch: f32| s(30.0, pitch);
        // Just under the climb's edge: a beat that was going keeps going, a glide stays a glide.
        assert_eq!(pose_held(Some(Pose::Flap), at(CLIMB_PITCH - 0.03)), Pose::Flap);
        assert_eq!(pose_held(Some(Pose::Glide), at(CLIMB_PITCH - 0.03)), Pose::Glide);
        assert_eq!(pose_held(Some(Pose::Flap), at(0.0)), Pose::Glide);
        // Just over the dive's edge: still tucked; well out, a glide.
        assert_eq!(pose_held(Some(Pose::Dive), at(DIVE_PITCH + 0.03)), Pose::Dive);
        assert_eq!(pose_held(Some(Pose::Dive), at(-0.1)), Pose::Glide);
    }

    #[test]
    fn every_flying_line_is_tuned() {
        for d in 40019..=40037 {
            assert!(LINES.iter().any(|l| l.displays.contains(&d)), "{d}");
        }
    }
}
