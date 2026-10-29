//! Flight on a mount (the server's `fusion/Riding.h`): world op 70 tells us a glide, a launch
//! point's burst of flight, the glide down after it, or a skyride has begun or ended. A glide is
//! the server's feather fall and a burst its GM flight (levitating swim), which benilla's mover
//! already plays.
//!
//! A skyride (the flying companion lines, `fusion/Skyriding.h`) is GM flight too, but flown here by
//! momentum: [`advance`] is the server's `Skyriding::Advance` to the float, and its velocity drives
//! the mover through benilla's [`LayerFlight`] seam. The mount always flies forward along the
//! rider's pitch: a dive trades height for speed, a pull-up speed for height, drag and the sink
//! bleed it away, and landing ends it. Space in the air is Launch (so is Ctrl+Space, on the
//! ground too: crate::companion), Shift Surge (world op 72); the server spends the vigor charges and tells us (op 71), which the gauge shows.

use benilla_app::fusion::{
    CHANNEL_WORLD, ChannelMessage, FusionChannelReceived, LayerFlight, SendFusionChannel,
};
use benilla_world::view::WorldCamera;
use bevy::picking::Pickable;
use bevy::prelude::*;

use crate::Mw2Set;

/// World op 70, `[70, phase u8, seconds left f32, ceiling f32]`.
pub const OP_FLIGHT: u8 = 70;
/// World op 71, `[71, flags u8, charges u8, max u8, progress f32, speed f32]`.
pub const OP_SKY: u8 = 71;
/// World op 72, `[72, move u8]`: Launch (1) or Surge (2).
pub const OP_SKY_MOVE: u8 = 72;
pub const SKY_FLYING: u8 = 0x01;
pub const SKY_ASCENT: u8 = 0x02;
pub const SKY_SURGE: u8 = 0x04;
pub const SKY_RIDER: u8 = 0x08;
/// World op 73, `[73, flags u8, ceiling f32]`: warded airspace (`fusion/Airspace.h`).
pub const OP_WARD: u8 = 73;
pub const WARD_IN: u8 = 0x01;
pub const WARD_UNFINISHED: u8 = 0x02;
pub const WARD_PAD: u8 = 0x04;
// The push under a ward's ceiling: `Airspace.h`'s constants, the same numbers.
pub const WARD_EASE_GAIN: f32 = 2.0;
pub const WARD_PUSH_MIN: f32 = 4.0;
pub const WARD_PUSH_GAIN: f32 = 0.5;
pub const WARD_PUSH_MAX: f32 = 20.0;

// ── The physics: `Skyriding.h`'s constants, the same numbers ──
pub const GRAVITY: f32 = 16.0;
pub const DRAG: f32 = 0.06;
pub const SINK: f32 = 1.5;
pub const STALL_SPEED: f32 = 10.0;
pub const STALL_SINK: f32 = 0.8;
pub const MAX_SPEED: f32 = 50.0;
pub const MIN_PITCH: f32 = -1.3963;
pub const MAX_PITCH: f32 = 1.2217;
pub const ASCENT_SECONDS: f32 = 1.2;
pub const ASCENT_RATE: f32 = 28.0;
pub const ASCENT_EXIT_SPEED: f32 = 20.0;
pub const SURGE_SPEED: f32 = 16.0;
pub const MAX_CHARGES: u8 = 3;
/// A running mount's speed: the field of view starts widening above it.
const CRUISE_SPEED: f32 = 14.0;
/// The widest the view gets at the fastest dive.
const SPEED_FOV: f32 = 1.25;

/// The mount in flight: its speed along the path and the Ascent still to run.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Body {
    pub speed: f32,
    pub ascent_left: f32,
}

/// One step's velocity: along the level facing, and up (yards per second).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Motion {
    pub forward: f32,
    pub up: f32,
}

pub fn clamp_pitch(pitch: f32) -> f32 {
    pitch.clamp(MIN_PITCH, MAX_PITCH)
}

/// One step of `Skyriding::Advance`: the speed after it and the velocity to move at.
pub fn advance(b: &mut Body, pitch: f32, dt: f32) -> Motion {
    if dt <= 0.0 {
        return Motion::default();
    }
    let pitch = clamp_pitch(pitch);
    let (sp, cp) = pitch.sin_cos();
    if b.ascent_left > 0.0 {
        let t = dt.min(b.ascent_left);
        b.ascent_left -= t;
        b.speed = (b.speed - DRAG * b.speed * dt).max(0.0);
        if b.ascent_left <= 1e-4 {
            b.ascent_left = 0.0;
            b.speed = b.speed.max(ASCENT_EXIT_SPEED);
        }
        return Motion {
            forward: b.speed * cp,
            up: ASCENT_RATE * t / dt,
        };
    }
    b.speed = (b.speed + (-GRAVITY * sp - DRAG * b.speed) * dt).clamp(0.0, MAX_SPEED);
    let sink = SINK + (STALL_SPEED - b.speed).max(0.0) * STALL_SINK;
    Motion {
        forward: b.speed * cp,
        up: b.speed * sp - sink,
    }
}

pub fn ascend(b: &mut Body) {
    b.ascent_left = ASCENT_SECONDS;
}

pub fn surge(b: &mut Body) {
    b.speed = (b.speed + SURGE_SPEED).min(MAX_SPEED);
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Phase {
    #[default]
    None,
    Glide,
    Burst,
    Descent,
    Skyride,
}

/// What the server last said of our flight.
#[derive(Resource, Debug, Default, Clone, PartialEq)]
pub struct Flight {
    pub phase: Phase,
    /// Seconds left in the phase when it was told, and when (`Time::elapsed_secs`).
    pub seconds: f32,
    pub told_at: f32,
    /// A burst's ceiling (WoW z).
    pub ceiling: f32,
}

/// The skyride: what the server last said (op 71) and the body we fly.
#[derive(Resource, Debug, Default, Clone, PartialEq)]
pub struct Sky {
    /// Riding a flying line: the gauge shows.
    pub rider: bool,
    pub flying: bool,
    pub charges: u8,
    pub max: u8,
    /// Toward the next charge, 0..1, when told.
    pub progress: f32,
    pub body: Body,
}

/// Warded airspace over a city or an unfinished place, as the server last told it (op 73): a
/// skyride may not climb over the ceiling, and over it the mount is pushed down.
#[derive(Resource, Debug, Default, Clone, Copy, PartialEq)]
pub struct Ward {
    pub active: bool,
    pub flags: u8,
    /// WoW z.
    pub ceiling: f32,
}

pub fn decode_ward(body: &[u8]) -> Option<Ward> {
    let [OP_WARD, flags, a, b, c, d] = body else {
        return None;
    };
    Some(Ward {
        active: flags & WARD_IN != 0,
        flags: *flags,
        ceiling: f32::from_le_bytes([*a, *b, *c, *d]),
    })
}

/// `Airspace::AllowedUp`: the fastest climb at height `z` under (or over) the ceiling.
pub fn ward_allowed_up(z: f32, ceiling: f32) -> f32 {
    let left = ceiling - z;
    if left >= 0.0 {
        left * WARD_EASE_GAIN
    } else {
        -(WARD_PUSH_MAX.min(WARD_PUSH_MIN - left * WARD_PUSH_GAIN))
    }
}

/// Op 70's body: the phase, its seconds left and the ceiling.
pub fn decode_flight(body: &[u8]) -> Option<(Phase, f32, f32)> {
    let [OP_FLIGHT, phase, rest @ ..] = body else {
        return None;
    };
    if rest.len() < 8 {
        return None;
    }
    let f = |b: &[u8]| f32::from_le_bytes([b[0], b[1], b[2], b[3]]);
    let phase = match phase {
        1 => Phase::Glide,
        2 => Phase::Burst,
        3 => Phase::Descent,
        4 => Phase::Skyride,
        _ => Phase::None,
    };
    Some((phase, f(&rest[0..4]), f(&rest[4..8])))
}

/// Op 71 decoded.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SkyNews {
    pub flags: u8,
    pub charges: u8,
    pub max: u8,
    pub progress: f32,
    pub speed: f32,
}

pub fn decode_sky(body: &[u8]) -> Option<SkyNews> {
    let [OP_SKY, flags, charges, max, rest @ ..] = body else {
        return None;
    };
    if rest.len() < 8 {
        return None;
    }
    let f = |b: &[u8]| f32::from_le_bytes([b[0], b[1], b[2], b[3]]);
    Some(SkyNews {
        flags: *flags,
        charges: *charges,
        max: *max,
        progress: f(&rest[0..4]),
        speed: f(&rest[4..8]),
    })
}

/// Apply op 71: a skyride begun takes the mount's speed; an Ascent or a Surge acts on the body.
pub fn apply_sky(sky: &mut Sky, n: SkyNews) {
    let flying = n.flags & SKY_FLYING != 0;
    if flying && !sky.flying {
        sky.body = Body {
            speed: n.speed.clamp(0.0, MAX_SPEED),
            ascent_left: 0.0,
        };
    }
    if !flying {
        sky.body = Body::default();
    }
    if n.flags & SKY_ASCENT != 0 {
        ascend(&mut sky.body);
    }
    if n.flags & SKY_SURGE != 0 {
        surge(&mut sky.body);
    }
    sky.flying = flying;
    sky.rider = n.flags & SKY_RIDER != 0;
    sky.charges = n.charges;
    sky.max = n.max;
    sky.progress = n.progress;
}

pub fn encode_sky_move(ascent: bool) -> Vec<u8> {
    vec![OP_SKY_MOVE, if ascent { 1 } else { 2 }]
}

/// The banner a change of phase shows, if any.
pub fn flight_banner(phase: Phase, seconds: f32) -> Option<crate::streaks::Banner> {
    let banner = |title: &str, subtitle: String| crate::streaks::Banner {
        title: title.into(),
        subtitle,
    };
    match phase {
        Phase::Burst => Some(banner(
            "UPDRAFT",
            format!("Your mount flies for {:.0} seconds", seconds),
        )),
        Phase::Descent => Some(banner("GLIDING DOWN", String::new())),
        _ => None,
    }
}

/// The view's widening at a speed: none at a running mount's, [`SPEED_FOV`] at the fastest dive.
pub fn speed_fov(speed: f32) -> f32 {
    let k = ((speed - CRUISE_SPEED) / (MAX_SPEED - CRUISE_SPEED)).clamp(0.0, 1.0);
    1.0 + (SPEED_FOV - 1.0) * k
}

pub struct RidingPlugin;

impl Plugin for RidingPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Flight>()
            .init_resource::<Sky>()
            .init_resource::<Ward>()
            .add_systems(Startup, setup_gauge)
            .add_systems(
                Update,
                (read_flight, sky_keys, fly_sky)
                    .chain()
                    .before(Mw2Set::Aim),
            )
            .add_systems(Update, no_throw_skyriding.in_set(Mw2Set::Mode))
            .add_systems(
                Update,
                widen_view
                    .after(crate::stance_view::apply_stance_view)
                    .before(crate::blast::shake_view),
            )
            .add_systems(Update, draw_gauge.in_set(Mw2Set::Hud));
    }
}

fn read_flight(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut flight: ResMut<Flight>,
    mut sky: ResMut<Sky>,
    mut ward: ResMut<Ward>,
    mut banners: MessageWriter<crate::streaks::Banner>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        if let Some(w) = decode_ward(&m.body) {
            *ward = w;
            continue;
        }
        if let Some(n) = decode_sky(&m.body) {
            debug!("benilla-mw2: sky {n:?}");
            apply_sky(&mut sky, n);
            continue;
        }
        let Some((phase, seconds, ceiling)) = decode_flight(&m.body) else {
            continue;
        };
        debug!("benilla-mw2: flight {phase:?}, {seconds:.1} s, ceiling {ceiling:.0}");
        if let Some(b) = flight_banner(phase, seconds) {
            banners.write(b);
        }
        *flight = Flight {
            phase,
            seconds,
            told_at: time.elapsed_secs(),
            ceiling,
        };
    }
}

/// Space in a skyride: Launch; Shift: Surge. The server spends the charge and
/// answers with op 71, which starts the move here.
fn sky_keys(
    input: Res<crate::input::Mw2Input>,
    sky: Res<Sky>,
    mut was_sprint: Local<bool>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    let shift = input.sprint && !*was_sprint;
    *was_sprint = input.sprint;
    if !sky.flying || sky.charges == 0 {
        return;
    }
    let ask = if input.jump.pressed {
        Some(true)
    } else if shift {
        Some(false)
    } else {
        None
    };
    if let Some(ascent) = ask {
        out.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_WORLD,
            body: encode_sky_move(ascent),
        }));
    }
}

/// The skyride's step: the physics at the pitch the mover flew last frame, handed to the mover.
fn fly_sky(
    time: Res<Time>,
    mut sky: ResMut<Sky>,
    mut seam: ResMut<LayerFlight>,
    ward: Res<Ward>,
    objects: benilla_app::Objects,
    transforms: Query<&GlobalTransform>,
) {
    if !sky.flying {
        if seam.active {
            *seam = LayerFlight::default();
        }
        return;
    }
    seam.active = true;
    seam.pitch_limits = (MIN_PITCH, MAX_PITCH);
    // Until the server's flight grant lands, the body waits.
    if !seam.levitating {
        return;
    }
    let pitch = seam.pitch;
    let m = advance(&mut sky.body, pitch, time.delta_secs());
    // On the ground (the server ends the flight on its next look), no skating along it.
    let skating = seam.grounded && sky.body.ascent_left <= 0.0 && m.up <= 0.0;
    seam.forward = if skating { 0.0 } else { m.forward };
    seam.up = m.up;
    // Warded airspace: no climb over the ceiling, and a push down from over it.
    if ward.active {
        if let Some(z) = objects
            .player()
            .and_then(|e| transforms.get(e).ok())
            .map(|g| benilla_assets::coords::bevy_to_wow(g.translation())[2])
        {
            seam.up = seam.up.min(ward_allowed_up(z, ward.ceiling));
        }
    }
}

/// The speed's sense: the view widens with the dive (benilla writes the field of view afresh
/// each frame, so this scales this frame's).
fn widen_view(sky: Res<Sky>, mut cameras: Query<&mut Projection, With<WorldCamera>>) {
    if !sky.flying {
        return;
    }
    let widen = speed_fov(sky.body.speed);
    if widen <= 1.0 {
        return;
    }
    for mut projection in &mut cameras {
        if let Projection::Perspective(p) = &mut *projection {
            p.fov = (p.fov * widen).min(2.0);
        }
    }
}

// ── The vigor gauge: three pips over the action bar, in the stock frames' look ──

#[derive(Component)]
struct GaugeRoot;
/// A charge pip's fill, by index; the one refilling shows its share.
#[derive(Component)]
struct GaugePip(u8);
#[derive(Component)]
struct GaugeSpeed;

const PIP_W: f32 = 34.0;
const PIP_H: f32 = 12.0;
/// The stock casting bar's gold, and the vigor's blue.
const PIP_BORDER: Color = Color::srgb(0.72, 0.6, 0.3);
const PIP_FULL: Color = Color::srgb(0.25, 0.6, 1.0);
const PIP_FILLING: Color = Color::srgba(0.25, 0.6, 1.0, 0.45);

fn setup_gauge(mut commands: Commands, assets: Res<AssetServer>) {
    let font: Handle<Font> = assets.load(crate::wow_ui::FONT);
    commands
        .spawn((
            GaugeRoot,
            Node {
                position_type: PositionType::Absolute,
                bottom: Val::Px(150.0),
                left: Val::Percent(50.0),
                margin: UiRect::left(Val::Px(-(PIP_W * 1.5 + 8.0))),
                flex_direction: FlexDirection::Column,
                align_items: AlignItems::Center,
                ..default()
            },
            Visibility::Hidden,
            Pickable::IGNORE,
        ))
        .with_children(|g| {
            g.spawn((
                GaugeSpeed,
                crate::wow_ui::text(&font, "", 10.0, Color::srgb(1.0, 0.82, 0.25)),
            ));
            g.spawn((
                Node {
                    flex_direction: FlexDirection::Row,
                    column_gap: Val::Px(4.0),
                    margin: UiRect::top(Val::Px(2.0)),
                    ..default()
                },
                Pickable::IGNORE,
            ))
            .with_children(|row| {
                for i in 0..MAX_CHARGES {
                    row.spawn((
                        Node {
                            width: Val::Px(PIP_W),
                            height: Val::Px(PIP_H),
                            border: UiRect::all(Val::Px(1.0)),
                            ..default()
                        },
                        BorderColor::all(PIP_BORDER),
                        BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.6)),
                        Pickable::IGNORE,
                    ))
                    .with_child((
                        GaugePip(i),
                        Node {
                            width: Val::Percent(0.0),
                            height: Val::Percent(100.0),
                            ..default()
                        },
                        BackgroundColor(PIP_FULL),
                        Pickable::IGNORE,
                    ));
                }
            });
        });
}

/// How full pip `i` is: whole for a charge in hand, the refill's share for the next one.
pub fn pip_fill(i: u8, charges: u8, progress: f32) -> f32 {
    match i.cmp(&charges) {
        std::cmp::Ordering::Less => 1.0,
        std::cmp::Ordering::Equal => progress.clamp(0.0, 1.0),
        std::cmp::Ordering::Greater => 0.0,
    }
}

fn draw_gauge(
    sky: Res<Sky>,
    mut root: Query<&mut Visibility, With<GaugeRoot>>,
    mut pips: Query<(&GaugePip, &mut Node, &mut BackgroundColor)>,
    mut speed: Query<&mut Text, With<GaugeSpeed>>,
) {
    let want = if sky.rider {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut root {
        if *v != want {
            *v = want;
        }
    }
    if !sky.rider {
        return;
    }
    for (pip, mut node, mut color) in &mut pips {
        let fill = pip_fill(pip.0, sky.charges, sky.progress);
        let w = Val::Percent(fill * 100.0);
        if node.width != w {
            node.width = w;
        }
        let c = if fill >= 1.0 { PIP_FULL } else { PIP_FILLING };
        if color.0 != c {
            color.0 = c;
        }
    }
    let line = if sky.flying {
        format!("{:.0} yd/s", sky.body.speed)
    } else {
        String::new()
    };
    for mut t in &mut speed {
        if t.0 != line {
            t.0 = line.clone();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn body(phase: u8, seconds: f32, ceiling: f32) -> Vec<u8> {
        let mut b = vec![OP_FLIGHT, phase];
        b.extend_from_slice(&seconds.to_le_bytes());
        b.extend_from_slice(&ceiling.to_le_bytes());
        b
    }

    fn sky_body(flags: u8, charges: u8, progress: f32, speed: f32) -> Vec<u8> {
        let mut b = vec![OP_SKY, flags, charges, MAX_CHARGES];
        b.extend_from_slice(&progress.to_le_bytes());
        b.extend_from_slice(&speed.to_le_bytes());
        b
    }

    /// `AirspaceTests.cpp`'s `airspace_the_push_eases_under_and_pushes_over`: the same numbers.
    #[test]
    fn the_ward_matches_the_server() {
        assert!((ward_allowed_up(97.0, 100.0) - 6.0).abs() < 1e-5);
        assert!(ward_allowed_up(100.0, 100.0).abs() < 1e-5);
        assert!((ward_allowed_up(120.0, 100.0) + 14.0).abs() < 1e-5);
        assert!((ward_allowed_up(500.0, 100.0) + WARD_PUSH_MAX).abs() < 1e-5);
        // Pushed from 100 over, the mount is under the ceiling inside the server's hold
        // (3 yd/s plus 10 of slack).
        let (mut z, mut t) = (200.0_f32, 0.0_f32);
        while z > 100.5 {
            z += ward_allowed_up(z, 100.0) / 60.0;
            t += 1.0 / 60.0;
            assert!(z - 100.0 <= (100.0 - 3.0 * t).max(0.0) + 10.0);
        }
        let mut b = vec![OP_WARD, WARD_IN | WARD_UNFINISHED];
        b.extend_from_slice(&1700.0_f32.to_le_bytes());
        let w = decode_ward(&b).unwrap();
        assert!(w.active && w.ceiling == 1700.0 && w.flags & WARD_UNFINISHED != 0);
        b[1] = 0;
        assert!(!decode_ward(&b).unwrap().active);
        assert!(decode_ward(&b[..4]).is_none());
        assert!(decode_flight(&b).is_none() && decode_sky(&b).is_none());
    }

    #[test]
    fn the_flight_op_decodes() {
        assert_eq!(
            decode_flight(&body(2, 15.0, 260.0)),
            Some((Phase::Burst, 15.0, 260.0))
        );
        assert_eq!(decode_flight(&body(0, 0.0, 0.0)).unwrap().0, Phase::None);
        assert_eq!(
            decode_flight(&body(4, 300.0, 0.0)).unwrap().0,
            Phase::Skyride
        );
        assert!(decode_flight(&[OP_FLIGHT, 2, 0]).is_none());
        assert!(decode_flight(&[22, 0, 0, 0, 0, 0, 0, 0, 0, 0]).is_none());
        assert!(decode_flight(&sky_body(1, 3, 0.0, 14.0)).is_none());
    }

    #[test]
    fn a_burst_and_its_end_are_announced() {
        assert_eq!(flight_banner(Phase::Burst, 15.0).unwrap().title, "UPDRAFT");
        assert!(
            flight_banner(Phase::Burst, 15.0)
                .unwrap()
                .subtitle
                .contains("15 seconds")
        );
        assert_eq!(
            flight_banner(Phase::Descent, 60.0).unwrap().title,
            "GLIDING DOWN"
        );
        assert!(flight_banner(Phase::Glide, 8.0).is_none());
        assert!(flight_banner(Phase::Skyride, 300.0).is_none());
        // The raid banners never take op 70.
        assert!(crate::build::world_banner(&body(2, 15.0, 260.0)).is_none());
    }

    /// `Skyriding.cpp`'s `skyriding_the_shared_vector`: the same steps, the same numbers.
    #[test]
    fn the_physics_matches_the_server() {
        let mut b = Body {
            speed: 14.0,
            ascent_left: 0.0,
        };
        let (mut x, mut z) = (0.0_f32, 200.0_f32);
        let mut fly = |b: &mut Body, pitch: f32, seconds: f32| {
            let dt = 1.0 / 30.0;
            for _ in 0..(seconds / dt).round() as i32 {
                let m = advance(b, pitch, dt);
                x += m.forward * dt;
                z += m.up * dt;
            }
        };
        fly(&mut b, -0.5, 2.0);
        fly(&mut b, 0.4, 2.0);
        ascend(&mut b);
        fly(&mut b, 0.0, 2.0);
        surge(&mut b);
        fly(&mut b, -0.2, 1.0);
        assert!((b.speed - 36.1062).abs() < 2e-3, "speed {}", b.speed);
        assert!((x - 136.4708).abs() < 2e-2, "x {x}");
        assert!((z - 212.9639).abs() < 2e-2, "z {z}");
    }

    #[test]
    fn no_free_flight_level_flight_sinks_and_a_dive_never_makes_energy() {
        let energy = |z: f32, s: f32| GRAVITY * z + 0.5 * s * s;
        let mut b = Body {
            speed: 30.0,
            ascent_left: 0.0,
        };
        let mut z = 100.0;
        for i in 0..1200 {
            let pitch = if i < 600 { -0.6 } else { 0.5 };
            let before = energy(z, b.speed);
            let m = advance(&mut b, pitch, 1.0 / 60.0);
            z += m.up / 60.0;
            assert!(energy(z, b.speed) <= before + 1e-3);
        }
        let mut level = Body {
            speed: 30.0,
            ascent_left: 0.0,
        };
        let mut lz = 100.0;
        for _ in 0..1200 {
            lz += advance(&mut level, 0.0, 1.0 / 60.0).up / 60.0;
        }
        assert!(lz < 70.0 && level.speed < 10.0);
    }

    #[test]
    fn the_sky_op_starts_moves_and_feeds_the_gauge() {
        let mut sky = Sky::default();
        let n = decode_sky(&sky_body(SKY_FLYING | SKY_RIDER | SKY_ASCENT, 2, 0.25, 14.0)).unwrap();
        apply_sky(&mut sky, n);
        assert!(sky.flying && sky.rider && sky.charges == 2);
        assert_eq!(sky.body.speed, 14.0);
        assert_eq!(sky.body.ascent_left, ASCENT_SECONDS);
        apply_sky(
            &mut sky,
            decode_sky(&sky_body(SKY_FLYING | SKY_RIDER | SKY_SURGE, 1, 0.0, 14.0)).unwrap(),
        );
        assert_eq!(sky.body.speed, 30.0);
        apply_sky(
            &mut sky,
            decode_sky(&sky_body(SKY_RIDER, 1, 0.5, 14.0)).unwrap(),
        );
        assert!(!sky.flying && sky.body == Body::default());
        assert!(decode_sky(&[OP_SKY, 1, 2]).is_none());
        assert_eq!(encode_sky_move(true), vec![OP_SKY_MOVE, 1]);
        assert_eq!(encode_sky_move(false), vec![OP_SKY_MOVE, 2]);
        // The gauge: whole pips for charges in hand, the next one by its share.
        assert_eq!(pip_fill(0, 1, 0.5), 1.0);
        assert_eq!(pip_fill(1, 1, 0.5), 0.5);
        assert_eq!(pip_fill(2, 1, 0.5), 0.0);
        // The view widens only above a running mount's speed.
        assert_eq!(speed_fov(14.0), 1.0);
        assert!((speed_fov(MAX_SPEED) - SPEED_FOV).abs() < 1e-6);
    }
}

/// Skyriding in the air throws nothing (the server's `Riding::ThrowsFromSaddle`): the lethal and
/// tactical keys do nothing until the flyer lands. A ground mount throws as on foot.
fn no_throw_skyriding(sky: Res<Sky>, mut input: ResMut<crate::input::Mw2Input>) {
    if sky.rider && sky.flying {
        input.lethal = Default::default();
        input.tactical = Default::default();
    }
}
