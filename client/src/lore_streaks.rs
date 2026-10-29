//! The lore killstreaks' client side (the server's `fusion/SkyStreaks.h` and
//! `game/Fusion/FusionSkyStreaks.cpp`). The craft themselves are creatures the server flies (a
//! gryphon or wind rider gunship, a gnomish gyrocopter, a bomber, a goblin rocket car, a dwarven
//! cannon), so every client draws them as it draws any creature; what is left here is what only
//! the caller does:
//!
//! - **The Goblin Rocket** (predator missile): the server sends it up over the mark (world op 37)
//!   and the view jumps to it. The mouse steers it (it only ever dives; `/rocketsens <n>` or the
//!   `womwRocketSensitivity` CVar scales the mouse), a click lights its second stage (the view
//!   widens and shakes, the hint says so), Escape bails out (no blast). It flies here, against
//!   the same world the server tests it against (the terrain, the buildings' and doodads' hulls,
//!   the built pieces) and the bodies near its path, and reports where it is ten times a second
//!   and where it struck (op 38); the server tests each leg itself and sets the blast off through
//!   the explosions' path (op 30), so the bodies it kills are thrown. The view eases after the
//!   nose rather than snapping to it. Enemies in view get red corner brackets while it flies. When
//!   it strikes or is lost the screen says SIGNAL LOST for a moment. It is lost after 22 s.
//! - **The craft's news** (world op 39): the owner's craft up with their time and health (a HUD
//!   column under the radar's), what they killed, their end; a craft the reader shot down; and,
//!   to everyone near, the marks on the ground where a bombing run's bombs will land (amber the
//!   reader's own, red an enemy's, blue a friend's) until each lands.
//! - **The Dwarven Sentry Turret** (sentry gun): 4 picks it up; a marker shows where it would
//!   stand (green within 6 yards on ground, red otherwise), a left click sets it down (the server
//!   checks the ground, the slope and the sight to it), a right click or Escape puts it back.

use bevy::prelude::*;

use benilla_app::fusion::{
    CHANNEL_WORLD, ChannelMessage, FusionChannelReceived, SendFusionChannel,
};
use benilla_app::{ObjectType, Objects};
use benilla_world::view::WorldCamera;

use crate::Mw2Set;
use crate::aim::{Aim, StaticWorld};
use crate::blast::Shake;
use crate::fusion::{FiredRound, to_wow};
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::mw2::sound::Sfx;
use crate::streaks::{Banner, FeedLine, Sound, Streak};
use crate::tracer::from_wow;

/// World op 37, server to client: the rocket goes up (phase 1) or is gone (phase 0).
/// `[37, phase u8, ticket u32, vehicle u64, x f32, y f32, z f32, yaw f32, pitch f32, life ms u16]`.
pub const OP_ROCKET: u8 = 37;
/// World op 38, client to server: `[38, ticket u32, kind u8, x f32, y f32, z f32]`.
pub const OP_ROCKET_REPORT: u8 = 38;
pub const REPORT_FLY: u8 = 0;
pub const REPORT_IMPACT: u8 = 1;
pub const REPORT_BAIL: u8 = 2;

/// The rocket's flight, the server's `Sky::kRocket*` (yards, seconds, radians).
pub const ROCKET_SPEED: f32 = 18.0;
pub const ROCKET_BOOST: f32 = 55.0;
pub const ROCKET_PITCH_MIN: f32 = -88.0 * std::f32::consts::PI / 180.0;
pub const ROCKET_PITCH_MAX: f32 = -12.0 * std::f32::consts::PI / 180.0;
pub const ROCKET_TURN: f32 = 0.0022;
pub const ROCKET_MAX_TURN_RATE: f32 = 2.2;
pub const ROCKET_REPORT_SECS: f32 = 0.1;
/// A body this near the rocket's path is struck.
pub const ROCKET_HIT_REACH: f32 = 1.6;
/// The most corner brackets drawn at once.
const BRACKETS: usize = 32;

/// The turret's placement: the server's `Sky::kPlaceReach` and how far the pointing ray looks.
pub const PLACE_REACH: f32 = 6.0;
pub const PLACE_RAY: f32 = 9.0;
/// The server's `kPlaceRise`: no higher or lower than this from the feet.
pub const PLACE_RISE: f32 = 2.5;

/// The unit flag that keeps a unit from being picked (`UNIT_FLAG_NOT_SELECTABLE`): the rocket.
const NOT_SELECTABLE: u32 = 0x0200_0000;

/// The rocket's view: how fast it eases after the nose (per second), how much wider the second
/// stage's view is, how long the hint shouts the second stage, and how long the screen says the
/// signal is lost after it strikes.
pub const ROCKET_VIEW_RATE: f32 = 12.0;
pub const BOOST_FOV: f32 = 1.2;
pub const BOOST_FLASH_SECS: f32 = 0.9;
pub const SIGNAL_LOST_SECS: f32 = 1.2;
/// The mouse's scale for the rocket (a CVar, saved with the others), and its bounds.
const SENSITIVITY_CVAR: &str = "womwRocketSensitivity";
pub const SENSITIVITY_MIN: f32 = 0.2;
pub const SENSITIVITY_MAX: f32 = 3.0;

/// World op 39, server to client: the craft's news (the server's `Sky::News`).
/// `[39, 1, streak u8, guid u64, seconds u16, health % u8]` up, `[39, 2, guid u64, why u8]` gone,
/// `[39, 3, streak u8, str8]` a kill, `[39, 4, streak u8, yours u8, str8]` shot down,
/// `[39, 5, flags u8, first ms u16, last ms u16, n u8, (x, y, z f32) * n]` a strike line.
pub const OP_CRAFT: u8 = 39;
pub const GONE_DONE: u8 = 0;
pub const GONE_SHOT_DOWN: u8 = 1;
pub const GONE_DISMISSED: u8 = 2;
/// A strike line's flags: the reader called it, or an enemy did.
pub const LINE_MINE: u8 = 1;
pub const LINE_HOSTILE: u8 = 2;
const MAX_LINE_POINTS: usize = 16;
/// How long a craft's kill shows in the HUD column, and how many show.
pub const CREDIT_SECS: f32 = 4.0;
const CREDITS: usize = 3;
/// A strike mark's radius on the ground (yards; about a bomb's inner blast).
const MARK_RADIUS: f32 = 3.5;

/// The rocket in WoW coordinates (x, y, z up; yaw counter-clockwise from +x, pitch up positive).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RocketPose {
    pub pos: Vec3,
    pub yaw: f32,
    pub pitch: f32,
    pub boost: bool,
}

/// Where the rocket points (WoW coordinates).
pub fn rocket_forward(p: &RocketPose) -> Vec3 {
    let c = p.pitch.cos();
    Vec3::new(c * p.yaw.cos(), c * p.yaw.sin(), p.pitch.sin())
}

fn wrap(a: f32) -> f32 {
    use std::f32::consts::PI;
    (a + PI).rem_euclid(2.0 * PI) - PI
}

/// Steer by the mouse (counts this frame, right and down positive) and fly `dt` seconds: the
/// server's `Sky::SteerRocket`.
pub fn steer_rocket(mut p: RocketPose, mouse_x: f32, mouse_y: f32, dt: f32) -> RocketPose {
    let dt = dt.max(0.0);
    let most = ROCKET_MAX_TURN_RATE * dt;
    p.yaw = wrap(p.yaw - (mouse_x * ROCKET_TURN).clamp(-most, most));
    p.pitch = (p.pitch - (mouse_y * ROCKET_TURN).clamp(-most, most))
        .clamp(ROCKET_PITCH_MIN, ROCKET_PITCH_MAX);
    let speed = if p.boost { ROCKET_BOOST } else { ROCKET_SPEED };
    p.pos += rocket_forward(&p) * (speed * dt);
    p
}

/// A direction in WoW coordinates in Bevy's (as `from_wow`, without the translation).
pub fn dir_from_wow(d: Vec3) -> Vec3 {
    Vec3::new(-d.y, d.z, -d.x)
}

/// Op 37, decoded.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RocketMsg {
    pub start: bool,
    pub ticket: u32,
    pub vehicle: u64,
    pub pose: RocketPose,
    pub life_ms: u16,
}

fn f32_at(b: &[u8], i: usize) -> f32 {
    f32::from_le_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]])
}

pub fn decode_rocket(b: &[u8]) -> Option<RocketMsg> {
    if b.len() != 36 || b[0] != OP_ROCKET || b[1] > 1 {
        return None;
    }
    let pos = Vec3::new(f32_at(b, 14), f32_at(b, 18), f32_at(b, 22));
    let (yaw, pitch) = (f32_at(b, 26), f32_at(b, 30));
    if !pos.is_finite() || !yaw.is_finite() || !pitch.is_finite() {
        return None;
    }
    Some(RocketMsg {
        start: b[1] == 1,
        ticket: u32::from_le_bytes(b[2..6].try_into().ok()?),
        vehicle: u64::from_le_bytes(b[6..14].try_into().ok()?),
        pose: RocketPose {
            pos,
            yaw,
            pitch,
            boost: false,
        },
        life_ms: u16::from_le_bytes([b[34], b[35]]),
    })
}

pub fn encode_report(ticket: u32, kind: u8, at: Vec3) -> Vec<u8> {
    let mut o = vec![OP_ROCKET_REPORT];
    o.extend_from_slice(&ticket.to_le_bytes());
    o.push(kind);
    for v in [at.x, at.y, at.z] {
        o.extend_from_slice(&v.to_le_bytes());
    }
    o
}

/// Op 39, decoded.
#[derive(Clone, Debug, PartialEq)]
pub enum News {
    Up {
        streak: u8,
        guid: u64,
        seconds: u16,
        health: u8,
    },
    Gone {
        guid: u64,
        why: u8,
    },
    Kill {
        streak: u8,
        victim: String,
    },
    Downed {
        streak: u8,
        yours: bool,
        other: String,
    },
    StrikeLine {
        flags: u8,
        first_ms: u16,
        last_ms: u16,
        /// WoW coordinates, on the ground.
        points: Vec<Vec3>,
    },
}

/// A name after its length byte, the message's last field: exactly that long.
fn str8(b: &[u8], at: usize) -> Option<String> {
    let n = usize::from(*b.get(at)?);
    (b.len() == at + 1 + n).then(|| String::from_utf8_lossy(&b[at + 1..]).into_owned())
}

/// The server's `Sky::DecodeNews`, with the same checks.
pub fn decode_news(b: &[u8]) -> Option<News> {
    if b.len() < 2 || b[0] != OP_CRAFT {
        return None;
    }
    match b[1] {
        1 if b.len() == 14 => Some(News::Up {
            streak: b[2],
            guid: u64::from_le_bytes(b[3..11].try_into().ok()?),
            seconds: u16::from_le_bytes([b[11], b[12]]),
            health: b[13].min(100),
        }),
        2 if b.len() == 11 && b[10] <= GONE_DISMISSED => Some(News::Gone {
            guid: u64::from_le_bytes(b[2..10].try_into().ok()?),
            why: b[10],
        }),
        3 if b.len() >= 3 => Some(News::Kill {
            streak: b[2],
            victim: str8(b, 3)?,
        }),
        4 if b.len() >= 4 => Some(News::Downed {
            streak: b[2],
            yours: b[3] != 0,
            other: str8(b, 4)?,
        }),
        5 if b.len() >= 8 => {
            let n = usize::from(b[7]);
            if n > MAX_LINE_POINTS || b.len() != 8 + n * 12 {
                return None;
            }
            let points: Vec<Vec3> = (0..n)
                .map(|i| {
                    let at = 8 + i * 12;
                    Vec3::new(f32_at(b, at), f32_at(b, at + 4), f32_at(b, at + 8))
                })
                .collect();
            points
                .iter()
                .all(|p| p.is_finite())
                .then_some(News::StrikeLine {
                    flags: b[2],
                    first_ms: u16::from_le_bytes([b[3], b[4]]),
                    last_ms: u16::from_le_bytes([b[5], b[6]]),
                    points,
                })
        }
        _ => None,
    }
}

/// Whether a guid is a killstreak craft's (a creature of entries 93500-93599, the server's
/// `sql/custom/azeroth_warfare/23_lore_streaks.sql`).
pub fn is_craft(guid: u64) -> bool {
    (guid >> 48) == 0xF130 && (93_500..=93_599).contains(&((guid >> 24) & 0xFF_FFFF))
}

/// A streak's name in the world by its server id ("Sky Rider Gunship" for the attack
/// helicopter's 8).
pub fn streak_name(id: u8) -> String {
    crate::streaks::REWARDS
        .iter()
        .find(|(_, label, _)| crate::streaks::reward_id(label) == Some(u16::from(id)))
        .map_or_else(
            || "Killstreak".to_owned(),
            |(_, label, _)| {
                crate::arsenal::arsenal()
                    .streak_by_label(label)
                    .map_or_else(|| (*label).to_owned(), |l| l.name.clone())
            },
        )
}

/// One of the reader's craft up, as the server last told it.
#[derive(Clone, Debug, PartialEq)]
pub struct CraftUp {
    pub guid: u64,
    pub streak: u8,
    /// Seconds of work left when told, and when that was (elapsed seconds).
    pub seconds: u16,
    pub told: f32,
    pub health: u8,
}

/// What a piece of news makes the HUD say besides its column.
#[derive(Clone, Debug, PartialEq)]
pub enum Say {
    Feed(String),
    Banner(String, String),
    Sound(&'static str),
}

/// The reader's craft and what they did lately: the HUD column under the radar's label.
#[derive(Resource, Debug, Default)]
pub struct SkyHud {
    pub crafts: Vec<CraftUp>,
    /// Kills credited to a craft: when, and the line.
    pub credits: Vec<(f32, String)>,
}

impl SkyHud {
    /// Take one piece of news at `t`; what else it says (a strike line's marks are drawn apart).
    pub fn apply(&mut self, news: &News, t: f32) -> Vec<Say> {
        match news {
            News::Up {
                streak,
                guid,
                seconds,
                health,
            } => {
                let up = CraftUp {
                    guid: *guid,
                    streak: *streak,
                    seconds: *seconds,
                    told: t,
                    health: *health,
                };
                match self.crafts.iter_mut().find(|c| c.guid == *guid) {
                    Some(c) => *c = up,
                    None => self.crafts.push(up),
                }
                Vec::new()
            }
            News::Gone { guid, .. } => {
                self.crafts.retain(|c| c.guid != *guid);
                Vec::new()
            }
            News::Kill { streak, victim } => {
                self.credits
                    .push((t, format!("{} killed {victim}", streak_name(*streak))));
                let over = self.credits.len().saturating_sub(CREDITS);
                self.credits.drain(..over);
                Vec::new()
            }
            News::Downed {
                streak,
                yours: true,
                other,
            } => vec![
                Say::Feed(format!(
                    "Your {} was shot down by {other}",
                    streak_name(*streak)
                )),
                Say::Sound("igQuestFailed"),
            ],
            News::Downed {
                streak,
                yours: false,
                other,
            } => {
                let name = streak_name(*streak);
                vec![
                    Say::Banner(
                        format!("{} SHOT DOWN", name.to_uppercase()),
                        format!("You shot down {other}'s {name}"),
                    ),
                    Say::Feed(format!("You shot down {other}'s {name}")),
                    Say::Sound("LEVELUPSOUND"),
                ]
            }
            News::StrikeLine { flags, .. } if flags & LINE_HOSTILE != 0 => vec![
                Say::Banner(
                    "AIR STRIKE INBOUND".into(),
                    "Get clear of the red marks".into(),
                ),
                Say::Sound("RaidWarning"),
            ],
            News::StrikeLine { .. } => Vec::new(),
        }
    }

    /// The column's lines at `t`: each craft's name, time left and health, then recent kills.
    pub fn lines(&self, t: f32) -> Vec<String> {
        let mut out: Vec<String> = self
            .crafts
            .iter()
            .map(|c| {
                let left = (f32::from(c.seconds) - (t - c.told)).max(0.0).ceil() as u32;
                let filled = usize::from(c.health.div_ceil(10).min(10));
                format!(
                    "{}  {}:{:02}  [{}{}] {}%",
                    streak_name(c.streak).to_uppercase(),
                    left / 60,
                    left % 60,
                    "|".repeat(filled),
                    ".".repeat(10 - filled),
                    c.health
                )
            })
            .collect();
        out.extend(
            self.credits
                .iter()
                .filter(|(at, _)| t - at < CREDIT_SECS)
                .map(|(_, l)| l.clone()),
        );
        out
    }
}

/// When mark `i` of `n` lands, in seconds after the line was told: spread from the first bomb's
/// landing to the last's.
pub fn mark_lands(first_ms: u16, last_ms: u16, n: usize, i: usize) -> f32 {
    let (a, b) = (f32::from(first_ms), f32::from(last_ms.max(first_ms)));
    let k = if n > 1 {
        i as f32 / (n - 1) as f32
    } else {
        0.0
    };
    (a + (b - a) * k) / 1000.0
}

/// The CVar's word to the mouse's scale for the rocket: 1 when unset or junk, else in bounds.
pub fn parse_sensitivity(v: Option<&str>) -> f32 {
    v.and_then(|s| s.trim().parse::<f32>().ok())
        .filter(|s| s.is_finite() && *s > 0.0)
        .map_or(1.0, |s| s.clamp(SENSITIVITY_MIN, SENSITIVITY_MAX))
}

/// The rocket's view a frame on: eased after the nose at `rate` per second.
pub fn ease_view(current: Quat, want: Quat, dt: f32, rate: f32) -> Quat {
    current.slerp(want, 1.0 - (-dt.max(0.0) * rate).exp())
}

/// How near the segment `a`..`b` passes `p`.
pub fn segment_distance(a: Vec3, b: Vec3, p: Vec3) -> f32 {
    let ab = b - a;
    let len2 = ab.length_squared();
    let t = if len2 < 1e-8 {
        0.0
    } else {
        ((p - a).dot(ab) / len2).clamp(0.0, 1.0)
    };
    (a + ab * t).distance(p)
}

/// Whether a turret may stand at `spot` for a player at `feet` (Bevy coordinates, y up): within
/// reach on the flat and not far above or below. The server checks the ground and the slope.
pub fn turret_spot_ok(feet: Vec3, spot: Vec3) -> bool {
    let flat = Vec2::new(spot.x - feet.x, spot.z - feet.z).length();
    flat <= PLACE_REACH && (spot.y - feet.y).abs() <= PLACE_RISE
}

/// A player's side by race: the Alliance's races (human, dwarf, night elf, gnome) or the Horde's.
pub fn alliance_race(race: u8) -> Option<bool> {
    match race {
        1 | 3 | 4 | 7 => Some(true),
        2 | 5 | 6 | 8 => Some(false),
        _ => None,
    }
}

/// A body's corner-bracket box on screen from its projected corners: its centre and size, a
/// little larger than the body and kept between 16 and 180 pixels.
pub fn bracket_box(low: Vec2, high: Vec2) -> (Vec2, Vec2) {
    let centre = (low + high) * 0.5;
    let extent = (high - low + Vec2::splat(6.0)).clamp(Vec2::splat(16.0), Vec2::splat(180.0));
    (centre, extent)
}

/// The rocket being flown.
#[derive(Debug, Clone, Copy)]
pub struct Flight {
    pub ticket: u32,
    pub vehicle: u64,
    pub pose: RocketPose,
    pub born: f32,
    pub life: f32,
    pub last_report: f32,
}

/// What a killstreak in hand is doing.
#[derive(Resource, Debug, Default)]
pub struct StreakControl {
    /// A turret being carried to its spot (the reward, to give back on a cancel).
    pub placing: Option<&'static str>,
    pub rocket: Option<Flight>,
}

impl StreakControl {
    /// Pick the turret up: the next click sets it down.
    pub fn place(&mut self, reward: &'static str) {
        self.placing = Some(reward);
    }
}

/// The rocket's view and its feedback: the eased camera, when the second stage lit, when the
/// signal was lost, and the mouse's scale (the CVar, polled).
#[derive(Resource, Debug)]
pub struct RocketView {
    pub rot: Option<Quat>,
    pub boost_at: f32,
    pub lost_at: f32,
    pub sensitivity: f32,
    next_poll: f32,
    lua: bool,
}

impl Default for RocketView {
    fn default() -> Self {
        Self {
            rot: None,
            boost_at: -100.0,
            lost_at: -100.0,
            sensitivity: 1.0,
            next_poll: 0.0,
            lua: true,
        }
    }
}

#[derive(Component)]
struct PlaceMarker;

#[derive(Component)]
struct Bracket(usize);

#[derive(Component)]
struct StreakHint;

#[derive(Component)]
struct SkyColumn;

#[derive(Component)]
struct SignalLost;

/// A bombing run's mark on the ground: until when it shows (elapsed seconds), from when.
#[derive(Component)]
struct StrikeMark {
    born: f32,
    lands: f32,
}

#[derive(Resource)]
struct MarkerLooks {
    good: Handle<StandardMaterial>,
    bad: Handle<StandardMaterial>,
    disc: Handle<Mesh>,
    mine: Handle<StandardMaterial>,
    hostile: Handle<StandardMaterial>,
    friendly: Handle<StandardMaterial>,
}

pub struct LoreStreaksPlugin;

impl Plugin for LoreStreaksPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<StreakControl>()
            .init_resource::<RocketView>()
            .init_resource::<SkyHud>()
            .add_systems(Startup, setup)
            .add_systems(
                Update,
                (read_rocket, fly_rocket, rocket_camera)
                    .chain()
                    .after(crate::stance_view::apply_stance_view)
                    .before(crate::blast::shake_view)
                    .before(Mw2Set::Aim),
            )
            .add_systems(Update, poll_sensitivity)
            .add_systems(Update, place_turret.in_set(Mw2Set::Weapon))
            .add_systems(
                Update,
                (
                    hide_vehicle,
                    draw_hint,
                    read_news,
                    draw_sky_column,
                    strike_marks,
                    draw_signal_lost,
                )
                    .in_set(Mw2Set::Hud),
            )
            .add_systems(
                PostUpdate,
                brackets.after(bevy::transform::TransformSystems::Propagate),
            );
    }
}

fn setup(
    mut commands: Commands,
    assets: Res<AssetServer>,
    meshes: Option<ResMut<Assets<Mesh>>>,
    materials: Option<ResMut<Assets<StandardMaterial>>>,
) {
    let font: Handle<Font> = assets.load(crate::wow_ui::FONT);
    // The owner's craft: top left under the UAV's label (crate::hud's RadarLabel).
    commands.spawn((
        SkyColumn,
        crate::hud::TopLeftSlot(16.0),
        Node {
            position_type: PositionType::Absolute,
            left: px(20),
            top: px(130),
            ..default()
        },
        Text::new(""),
        crate::hud::FontUnits(11.0),
        TextFont::from_font_size(14.0).with_font(font.clone()),
        TextColor(Color::srgb(1.0, 0.82, 0.3)),
        GlobalZIndex(1050),
        bevy::picking::Pickable::IGNORE,
    ));
    // The rocket's end: the feed is gone.
    commands
        .spawn((
            SignalLost,
            Node {
                position_type: PositionType::Absolute,
                width: percent(100),
                height: percent(100),
                justify_content: JustifyContent::Center,
                align_items: AlignItems::Center,
                ..default()
            },
            BackgroundColor(Color::srgba(0.05, 0.06, 0.05, 0.9)),
            Visibility::Hidden,
            GlobalZIndex(1060),
            bevy::picking::Pickable::IGNORE,
        ))
        .with_child((
            Text::new("SIGNAL LOST"),
            crate::hud::FontUnits(26.0),
            TextFont::from_font_size(34.0).with_font(font.clone()),
            TextColor(Color::srgb(0.85, 0.9, 0.85)),
            bevy::picking::Pickable::IGNORE,
        ));
    commands.spawn((
        StreakHint,
        Node {
            position_type: PositionType::Absolute,
            bottom: percent(18),
            width: percent(100),
            justify_content: JustifyContent::Center,
            ..default()
        },
        Text::new(""),
        crate::hud::FontUnits(14.0),
        TextFont::from_font_size(18.0).with_font(font),
        TextColor(Color::srgb(1.0, 0.82, 0.3)),
        TextLayout::new_with_justify(Justify::Center),
        GlobalZIndex(1050),
        bevy::picking::Pickable::IGNORE,
    ));
    // The rocket's corner brackets: a pool, placed and shown each frame.
    let red = Color::srgb(1.0, 0.12, 0.06);
    for i in 0..BRACKETS {
        commands
            .spawn((
                Bracket(i),
                Node {
                    position_type: PositionType::Absolute,
                    ..default()
                },
                Visibility::Hidden,
                GlobalZIndex(1049),
                bevy::picking::Pickable::IGNORE,
            ))
            .with_children(|parent| {
                for (left, top) in [(true, true), (false, true), (true, false), (false, false)] {
                    let edge = |on: bool| if on { px(1.5) } else { px(0) };
                    parent.spawn((
                        Node {
                            position_type: PositionType::Absolute,
                            left: if left { px(0) } else { auto() },
                            right: if left { auto() } else { px(0) },
                            top: if top { px(0) } else { auto() },
                            bottom: if top { auto() } else { px(0) },
                            width: px(8),
                            height: px(8),
                            border: UiRect {
                                left: edge(left),
                                right: edge(!left),
                                top: edge(top),
                                bottom: edge(!top),
                            },
                            ..default()
                        },
                        BorderColor::all(red),
                        bevy::picking::Pickable::IGNORE,
                    ));
                }
            });
    }
    let (Some(mut meshes), Some(mut materials)) = (meshes, materials) else {
        return;
    };
    let tint = |c: Color| StandardMaterial {
        base_color: c,
        unlit: true,
        alpha_mode: AlphaMode::Blend,
        ..default()
    };
    let looks = MarkerLooks {
        good: materials.add(tint(Color::srgba(0.3, 1.0, 0.35, 0.45))),
        bad: materials.add(tint(Color::srgba(1.0, 0.25, 0.2, 0.45))),
        disc: meshes.add(Cylinder::new(MARK_RADIUS, 0.08)),
        mine: materials.add(tint(Color::srgba(1.0, 0.72, 0.15, 0.42))),
        hostile: materials.add(tint(Color::srgba(1.0, 0.12, 0.08, 0.5))),
        friendly: materials.add(tint(Color::srgba(0.3, 0.6, 1.0, 0.4))),
    };
    commands.spawn((
        Name::new("mw2 turret marker"),
        PlaceMarker,
        Mesh3d(meshes.add(Cylinder::new(0.6, 1.1))),
        MeshMaterial3d(looks.good.clone()),
        Transform::default(),
        Visibility::Hidden,
    ));
    commands.insert_resource(looks);
}

/// World op 37: the rocket goes up (the view goes with it) or the server says it is gone.
fn read_rocket(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut control: ResMut<StreakControl>,
    mut view: ResMut<RocketView>,
    mut state: ResMut<FpsState>,
    mut sfx: MessageWriter<Sfx>,
) {
    let t = time.elapsed_secs();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        let Some(r) = decode_rocket(&m.body) else {
            continue;
        };
        if r.start {
            control.rocket = Some(Flight {
                ticket: r.ticket,
                vehicle: r.vehicle,
                pose: r.pose,
                born: t,
                life: f32::from(r.life_ms) / 1000.0,
                last_report: t,
            });
            view.rot = None;
            view.boost_at = -100.0;
            sfx.write(Sfx::new("mp_killstreak_radar"));
        } else if control.rocket.is_some_and(|f| f.ticket == r.ticket) {
            // The server ended it (it met a wall this client had not loaded, or ran out).
            control.rocket = None;
            view.lost_at = t;
        }
    }
    let flying = control.rocket.is_some();
    if state.piloting != flying {
        state.piloting = flying;
    }
}

/// Fly the rocket: steer, boost, strike or bail, and tell the server.
#[allow(clippy::too_many_arguments)]
fn fly_rocket(
    time: Res<Time>,
    input: Res<Mw2Input>,
    mut control: ResMut<StreakControl>,
    mut view: ResMut<RocketView>,
    mut shake: ResMut<Shake>,
    mut state: ResMut<FpsState>,
    world: StaticWorld,
    building: Option<Res<crate::build::Building>>,
    objects: Objects,
    bodies: Query<&GlobalTransform>,
    mut send: MessageWriter<SendFusionChannel>,
    mut sfx: MessageWriter<Sfx>,
) {
    let Some(mut f) = control.rocket else {
        return;
    };
    let t = time.elapsed_secs();
    let dt = time.delta_secs();
    let ticket = f.ticket;
    let mut report = |kind: u8, at: Vec3| {
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_WORLD,
            body: encode_report(ticket, kind, at),
        }));
    };
    let me = state.self_guid.and_then(|g| objects.object(g));
    let dead = me.is_none_or(|m| m.unit_health().unwrap_or(0) == 0);
    // Escape, death, leaving the world or the fuel run out: the rocket is lost, no blast.
    if input.streak_cancel || dead || !state.in_world || t - f.born > f.life {
        report(REPORT_BAIL, f.pose.pos);
        control.rocket = None;
        state.piloting = false;
        view.lost_at = t;
        return;
    }
    if input.streak_fire.pressed && !f.pose.boost {
        f.pose.boost = true;
        view.boost_at = t;
        // The second stage kicks: a jolt to the view as it lights.
        shake.kick(t + 0.45, 1.6);
        sfx.write(Sfx::new("weap_remotemissile_boost"));
    }
    let from = f.pose.pos;
    let look = input.pilot_look * view.sensitivity;
    let next = steer_rocket(f.pose, look.x, look.y, dt);
    // What it meets on the way, as the server tests it (its WorldOnSegment): the static world
    // (terrain, buildings, doodads), the built pieces, then a body.
    let (a, b) = (from_wow(from.to_array()), from_wow(next.pos.to_array()));
    let step = a.distance(b);
    let mut impact = None;
    if let Ok(dir) = Dir3::new(b - a)
        && let Some(d) = world.distance(Ray3d::new(a, dir))
        && d <= step + 0.3
    {
        impact = Some(a + *dir * d);
    }
    if let Some(building) = &building
        && step > 1e-4
    {
        let dir_wow = (next.pos - from) / step;
        for piece in building.pieces.values() {
            let placed = piece.placed();
            if placed.pos.distance(from) > step + 30.0 {
                continue;
            }
            if let Some(d) = placed.ray(from, dir_wow, step + 0.3)
                && impact.is_none_or(|i: Vec3| a.distance(i) > d)
            {
                impact = Some(from_wow((from + dir_wow * d).to_array()));
            }
        }
    }
    for (guid, entity, fields) in objects.iter() {
        if Some(guid) == state.self_guid || guid == f.vehicle {
            continue;
        }
        // Not the rocket itself, nor the caller's own craft around it.
        if !matches!(
            fields.object_type(),
            Some(ObjectType::Unit | ObjectType::Player)
        ) || fields.unit_health().unwrap_or(0) == 0
            || fields.unit_flags() & NOT_SELECTABLE != 0
            || fields
                .unit_created_by()
                .is_some_and(|c| Some(c) == state.self_guid)
        {
            continue;
        }
        let Ok(gt) = bodies.get(entity) else {
            continue;
        };
        let body = gt.translation() + Vec3::Y;
        if segment_distance(a, b, body) <= ROCKET_HIT_REACH
            && impact.is_none_or(|i: Vec3| a.distance(body) < a.distance(i))
        {
            impact = Some(body);
        }
    }
    if let Some(at) = impact {
        report(REPORT_IMPACT, Vec3::from(to_wow(at)));
        control.rocket = None;
        state.piloting = false;
        view.lost_at = t;
        return;
    }
    f.pose = next;
    if t - f.last_report >= ROCKET_REPORT_SECS {
        f.last_report = t;
        report(REPORT_FLY, f.pose.pos);
    }
    control.rocket = Some(f);
}

/// The view from the rocket's nose: eased after it, so the mouse's steps don't jerk the picture,
/// and wider once the second stage is lit (benilla writes the field of view afresh each frame).
fn rocket_camera(
    time: Res<Time>,
    control: Res<StreakControl>,
    mut view: ResMut<RocketView>,
    mut cameras: Query<(&mut Transform, Option<&mut Projection>), With<WorldCamera>>,
) {
    let Some(f) = control.rocket else {
        view.rot = None;
        return;
    };
    let at = from_wow(f.pose.pos.to_array());
    let look = dir_from_wow(rocket_forward(&f.pose));
    let want = Transform::from_translation(at)
        .looking_to(look, Vec3::Y)
        .rotation;
    let rot = view.rot.map_or(want, |r| {
        ease_view(r, want, time.delta_secs(), ROCKET_VIEW_RATE)
    });
    view.rot = Some(rot);
    let widen = if f.pose.boost {
        let k = ((time.elapsed_secs() - view.boost_at) / 0.35).clamp(0.0, 1.0);
        1.0 + (BOOST_FOV - 1.0) * k
    } else {
        1.0
    };
    for (mut tf, projection) in &mut cameras {
        *tf = Transform::from_translation(at).with_rotation(rot);
        if let Some(mut projection) = projection
            && let Projection::Perspective(p) = &mut *projection
            && widen > 1.0
        {
            p.fov = (p.fov * widen).min(2.0);
        }
    }
}

/// The rocket's mouse scale from its CVar, once a second (and `/rocketsens <n>` to set it).
fn poll_sensitivity(
    time: Res<Time>,
    mut view: ResMut<RocketView>,
    script: Option<NonSend<benilla_ui::script::UiScript>>,
) {
    let t = time.elapsed_secs();
    if !view.lua || t < view.next_poll {
        return;
    }
    let Some(script) = script else {
        return;
    };
    view.next_poll = t + 1.0;
    let chunk = format!(
        r#"RegisterCVar("{SENSITIVITY_CVAR}", "1")
if SlashCmdList and not SlashCmdList["WOMWROCKETSENS"] then
  SLASH_WOMWROCKETSENS1 = "/rocketsens"
  SlashCmdList["WOMWROCKETSENS"] = function(msg)
    local v = tonumber(msg)
    if v then SetCVar("{SENSITIVITY_CVAR}", tostring(v)) end
    if DEFAULT_CHAT_FRAME then
      DEFAULT_CHAT_FRAME:AddMessage("Goblin Rocket mouse sensitivity: " .. (GetCVar("{SENSITIVITY_CVAR}") or "1") .. " (0.2 to 3)")
    end
  end
end
return GetCVar("{SENSITIVITY_CVAR}")"#
    );
    match crate::lua::eval::<Option<String>>(&script, &chunk) {
        Some(v) => view.sensitivity = parse_sensitivity(v.as_deref()),
        None => view.lua = false,
    }
}

/// The pilot does not see the rocket car around the camera.
fn hide_vehicle(control: Res<StreakControl>, objects: Objects, mut vis: Query<&mut Visibility>) {
    let Some(f) = control.rocket else {
        return;
    };
    if let Some(e) = objects.entity(f.vehicle)
        && let Ok(mut v) = vis.get_mut(e)
        && *v != Visibility::Hidden
    {
        *v = Visibility::Hidden;
    }
}

/// Carry the turret: the marker where it would stand, a click to set it down, a right click or
/// Escape to put it back.
#[allow(clippy::too_many_arguments)]
fn place_turret(
    aim: Res<Aim>,
    input: Res<Mw2Input>,
    mut control: ResMut<StreakControl>,
    mut state: ResMut<FpsState>,
    mut streak: ResMut<Streak>,
    objects: Objects,
    bodies: Query<&GlobalTransform, Without<PlaceMarker>>,
    looks: Option<Res<MarkerLooks>>,
    mut marker: Query<
        (
            &mut Transform,
            &mut Visibility,
            &mut MeshMaterial3d<StandardMaterial>,
        ),
        With<PlaceMarker>,
    >,
    mut rounds: MessageWriter<FiredRound>,
    mut banners: MessageWriter<Banner>,
) {
    let placing = control.placing.is_some();
    if state.placing != placing {
        state.placing = placing;
    }
    let Ok((mut tf, mut vis, mut mat)) = marker.single_mut() else {
        return;
    };
    let Some(reward) = control.placing else {
        if *vis != Visibility::Hidden {
            *vis = Visibility::Hidden;
        }
        return;
    };
    let me = state.self_guid.and_then(|g| objects.object(g));
    let dead = me.is_none_or(|m| m.unit_health().unwrap_or(0) == 0);
    if input.streak_cancel || input.streak_alt.pressed || dead || !state.active {
        // Put back: the reward waits to be called in again.
        streak.ready.push(reward);
        control.placing = None;
        state.placing = false;
        *vis = Visibility::Hidden;
        return;
    }
    let feet = objects
        .player()
        .and_then(|e| bodies.get(e).ok())
        .map(GlobalTransform::translation);
    let spot = aim.ray.and_then(|ray| {
        let d = aim.world_distance.filter(|d| *d <= PLACE_RAY)?;
        Some((ray, ray.origin + *ray.direction * d))
    });
    let ok = matches!((feet, spot), (Some(f), Some((_, s))) if turret_spot_ok(f, s));
    match spot {
        Some((_, s)) => {
            tf.translation = s + Vec3::Y * 0.55;
            if *vis != Visibility::Visible {
                *vis = Visibility::Visible;
            }
            if let Some(looks) = &looks {
                let want = if ok { &looks.good } else { &looks.bad };
                if mat.0 != *want {
                    mat.0 = want.clone();
                }
            }
        }
        None => *vis = Visibility::Hidden,
    }
    if !input.streak_fire.pressed {
        return;
    }
    let Some((ray, _)) = spot.filter(|_| ok) else {
        banners.write(Banner {
            title: String::new(),
            subtitle: "Set the turret down on ground within 6 yards".into(),
        });
        return;
    };
    // The call-in's ray points at the spot; the server sets it down there (or hands it back).
    if let Some(item) = crate::equipment::streak_item(reward) {
        rounds.write(FiredRound {
            ray,
            target: None,
            ads: false,
            seed: 0,
            spread_deg: 0.0,
            item: Some(item.to_owned()),
            detonate: false,
            left: false,
        });
    }
    control.placing = None;
    state.placing = false;
    *vis = Visibility::Hidden;
}

/// The controls while a streak is in hand.
fn draw_hint(
    time: Res<Time>,
    control: Res<StreakControl>,
    view: Res<RocketView>,
    mut text: Query<&mut Text, With<StreakHint>>,
) {
    let t = time.elapsed_secs();
    let s = if let Some(f) = control.rocket {
        let left = (f.life - (t - f.born)).max(0.0);
        let lit = if !f.pose.boost {
            "Click: second stage"
        } else if t - view.boost_at < BOOST_FLASH_SECS {
            ">>> SECOND STAGE LIT <<<"
        } else {
            "Second stage lit"
        };
        format!("GOBLIN ROCKET  {left:.0}\nMouse steers  -  {lit}  -  Esc: bail out")
    } else if control.placing.is_some() {
        "DWARVEN SENTRY TURRET\nLeft click: set it down (within 6 yards)  -  Right click or Esc: put it back".into()
    } else {
        String::new()
    };
    for mut t in &mut text {
        if t.0 != s {
            t.0.clone_from(&s);
        }
    }
}

/// World op 39: the owner's craft for the HUD column, what they did, and every bombing run's
/// marks on the ground.
#[allow(clippy::too_many_arguments)]
fn read_news(
    mut commands: Commands,
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut hud: ResMut<SkyHud>,
    looks: Option<Res<MarkerLooks>>,
    mut feed: MessageWriter<FeedLine>,
    mut banners: MessageWriter<Banner>,
    mut sounds: MessageWriter<Sound>,
) {
    let t = time.elapsed_secs();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        let Some(news) = decode_news(&m.body) else {
            continue;
        };
        for say in hud.apply(&news, t) {
            match say {
                Say::Feed(s) => {
                    feed.write(FeedLine(s));
                }
                Say::Banner(title, subtitle) => {
                    banners.write(Banner { title, subtitle });
                }
                Say::Sound(kit) => {
                    sounds.write(Sound(kit));
                }
            }
        }
        if let News::StrikeLine {
            flags,
            first_ms,
            last_ms,
            points,
        } = &news
            && let Some(looks) = &looks
        {
            let material = if flags & LINE_MINE != 0 {
                &looks.mine
            } else if flags & LINE_HOSTILE != 0 {
                &looks.hostile
            } else {
                &looks.friendly
            };
            for (i, p) in points.iter().enumerate() {
                commands.spawn((
                    Name::new("mw2 strike mark"),
                    StrikeMark {
                        born: t,
                        lands: t + mark_lands(*first_ms, *last_ms, points.len(), i),
                    },
                    Mesh3d(looks.disc.clone()),
                    MeshMaterial3d(material.clone()),
                    Transform::from_translation(from_wow(p.to_array()) + Vec3::Y * 0.1),
                ));
            }
        }
    }
}

/// The owner's craft, their time and health, and their kills.
fn draw_sky_column(
    time: Res<Time>,
    state: Res<FpsState>,
    mut hud: ResMut<SkyHud>,
    mut text: Query<&mut Text, With<SkyColumn>>,
) {
    let t = time.elapsed_secs();
    // A craft the server stopped telling of (a lost packet, a new world) goes after its time.
    hud.crafts
        .retain(|c| t - c.told < f32::from(c.seconds) + 10.0);
    hud.credits.retain(|(at, _)| t - at < CREDIT_SECS);
    let s = if state.active {
        hud.lines(t).join("\n")
    } else {
        String::new()
    };
    for mut text in &mut text {
        if text.0 != s {
            text.0.clone_from(&s);
        }
    }
}

/// The marks pulse, quicker as their bomb nears, and go when it lands.
fn strike_marks(
    mut commands: Commands,
    time: Res<Time>,
    mut marks: Query<(Entity, &StrikeMark, &mut Transform)>,
) {
    let t = time.elapsed_secs();
    for (e, mark, mut tf) in &mut marks {
        if t >= mark.lands + 0.2 {
            commands.entity(e).despawn();
            continue;
        }
        let left = (mark.lands - t).max(0.0);
        let rate = if left < 1.0 { 14.0 } else { 6.0 };
        let grow = ((t - mark.born) / 0.25).clamp(0.2, 1.0);
        let pulse = 0.85 + 0.15 * ((t - mark.born) * rate).sin();
        tf.scale = Vec3::new(grow * pulse, 1.0, grow * pulse);
    }
}

/// SIGNAL LOST over the view for a moment after the rocket's end, flickering out.
fn draw_signal_lost(
    time: Res<Time>,
    view: Res<RocketView>,
    mut overlay: Query<(&mut Visibility, &mut BackgroundColor), With<SignalLost>>,
) {
    let age = time.elapsed_secs() - view.lost_at;
    for (mut vis, mut bg) in &mut overlay {
        if !(0.0..SIGNAL_LOST_SECS).contains(&age) {
            if *vis != Visibility::Hidden {
                *vis = Visibility::Hidden;
            }
            continue;
        }
        if *vis != Visibility::Inherited {
            *vis = Visibility::Inherited;
        }
        let flicker = 0.82 + 0.18 * (age * 83.0).sin().abs();
        let fade = ((SIGNAL_LOST_SECS - age) / 0.3).clamp(0.0, 1.0);
        bg.0 = Color::srgba(0.05, 0.06, 0.05, 0.92 * flicker * fade);
    }
}

/// Red corner brackets on the enemies the rocket's camera sees (the view only: no lock, no
/// damage, nothing the client does not already draw).
#[allow(clippy::type_complexity)]
fn brackets(
    control: Res<StreakControl>,
    state: Res<FpsState>,
    objects: Objects,
    cameras: Query<(&Camera, &GlobalTransform), With<WorldCamera>>,
    bodies: Query<&GlobalTransform, Without<WorldCamera>>,
    mut pool: Query<(&Bracket, &mut Node, &mut Visibility)>,
) {
    let mut boxes: Vec<(Vec2, Vec2)> = Vec::new();
    if let Some(f) = control.rocket
        && let Ok((camera, eye)) = cameras.single()
        && let Some(size) = camera.logical_viewport_size()
    {
        let me = state.self_guid.and_then(|g| objects.object(g));
        let my_side = me.and_then(|m| m.unit_race()).and_then(alliance_race);
        let right = eye.right().as_vec3();
        for (guid, entity, fields) in objects.iter() {
            if boxes.len() >= BRACKETS {
                break;
            }
            if Some(guid) == state.self_guid
                || guid == f.vehicle
                || fields.unit_health().unwrap_or(0) == 0
                || fields.unit_flags() & NOT_SELECTABLE != 0
                || fields
                    .unit_created_by()
                    .is_some_and(|c| Some(c) == state.self_guid)
            {
                continue;
            }
            let side_of = |guid: u64| {
                objects
                    .object(guid)
                    .and_then(|o| o.unit_race())
                    .and_then(alliance_race)
            };
            let enemy = match fields.object_type() {
                // A craft is its caller's side.
                Some(ObjectType::Unit) if is_craft(guid) => {
                    let side = fields.unit_created_by().and_then(side_of);
                    side.is_some() && my_side.is_some() && side != my_side
                }
                Some(ObjectType::Unit) => fields.unit_npc_flags() == 0,
                Some(ObjectType::Player) => {
                    let side = fields.unit_race().and_then(alliance_race);
                    side.is_some() && my_side.is_some() && side != my_side
                }
                _ => false,
            };
            if !enemy {
                continue;
            }
            let Ok(gt) = bodies.get(entity) else {
                continue;
            };
            let base = gt.translation();
            let (mut low, mut high) = (Vec2::splat(f32::INFINITY), Vec2::splat(f32::NEG_INFINITY));
            let mut seen = true;
            for off in [
                -right * 0.6,
                right * 0.6,
                Vec3::Y * 2.0 - right * 0.6,
                Vec3::Y * 2.0 + right * 0.6,
            ] {
                match camera.world_to_viewport(eye, base + off) {
                    Ok(p) => {
                        low = low.min(p);
                        high = high.max(p);
                    }
                    Err(_) => {
                        seen = false;
                        break;
                    }
                }
            }
            if !seen || high.x < 0.0 || high.y < 0.0 || low.x > size.x || low.y > size.y {
                continue;
            }
            boxes.push(bracket_box(low, high));
        }
    }
    for (b, mut node, mut vis) in &mut pool {
        match boxes.get(b.0) {
            Some((centre, extent)) => {
                node.left = px(centre.x - extent.x * 0.5);
                node.top = px(centre.y - extent.y * 0.5);
                node.width = px(extent.x);
                node.height = px(extent.y);
                if *vis != Visibility::Inherited {
                    *vis = Visibility::Inherited;
                }
            }
            None => {
                if *vis != Visibility::Hidden {
                    *vis = Visibility::Hidden;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_rocket_steers_down_only_and_boosts() {
        let p = RocketPose {
            pos: Vec3::new(0.0, 0.0, 90.0),
            yaw: 0.0,
            pitch: -1.0,
            boost: false,
        };
        // Straight on at its speed.
        let q = steer_rocket(p, 0.0, 0.0, 1.0);
        assert!((q.pos.distance(p.pos) - ROCKET_SPEED).abs() < 1e-3);
        assert!(q.pos.z < p.pos.z);
        // Right turns right: facing +x, toward -y.
        assert!(steer_rocket(p, 40.0, 0.0, 0.1).yaw < 0.0);
        // Pulling up stops short of level; pushing down short of straight down.
        let mut up = p;
        for _ in 0..200 {
            up = steer_rocket(up, 0.0, -300.0, 0.05);
        }
        assert!((up.pitch - ROCKET_PITCH_MAX).abs() < 1e-5);
        assert!(rocket_forward(&up).z < 0.0);
        let mut down = p;
        for _ in 0..200 {
            down = steer_rocket(down, 0.0, 300.0, 0.05);
        }
        assert!((down.pitch - ROCKET_PITCH_MIN).abs() < 1e-5);
        // A flick turns no faster than the cap.
        let flick = steer_rocket(p, 1.0e6, 0.0, 0.1);
        assert!((flick.yaw.abs() - ROCKET_MAX_TURN_RATE * 0.1).abs() < 1e-4);
        // The second stage.
        let lit = RocketPose { boost: true, ..p };
        let r = steer_rocket(lit, 0.0, 0.0, 1.0);
        assert!((r.pos.distance(p.pos) - ROCKET_BOOST).abs() < 1e-3);
    }

    #[test]
    fn the_rockets_view_points_where_it_flies() {
        // WoW +x is Bevy -z; straight down is Bevy -y.
        let d = dir_from_wow(Vec3::new(1.0, 0.0, 0.0));
        assert!(d.distance(Vec3::new(0.0, 0.0, -1.0)) < 1e-6);
        assert!(dir_from_wow(Vec3::new(0.0, 0.0, -1.0)).distance(Vec3::NEG_Y) < 1e-6);
        // The same map as positions.
        let w = Vec3::new(3.0, -4.0, 5.0);
        assert!(dir_from_wow(w).distance(from_wow(w.to_array())) < 1e-6);
        assert_eq!(to_wow(from_wow(w.to_array())), w.to_array());
    }

    #[test]
    fn the_rocket_wire_matches_the_servers() {
        // As the server's EncodeRocket: 36 bytes.
        let mut b = vec![OP_ROCKET, 1];
        b.extend_from_slice(&77u32.to_le_bytes());
        b.extend_from_slice(&0x1122_3344_5566_7788u64.to_le_bytes());
        for v in [1.5f32, -2.0, 300.0, 1.0, -0.7] {
            b.extend_from_slice(&v.to_le_bytes());
        }
        b.extend_from_slice(&22000u16.to_le_bytes());
        let m = decode_rocket(&b).expect("decodes");
        assert!(m.start && m.ticket == 77 && m.vehicle == 0x1122_3344_5566_7788);
        assert_eq!(m.pose.pos, Vec3::new(1.5, -2.0, 300.0));
        assert_eq!((m.pose.yaw, m.pose.pitch, m.life_ms), (1.0, -0.7, 22000));
        assert!(decode_rocket(&b[..35]).is_none());
        let mut bad = b.clone();
        bad[1] = 2;
        assert!(decode_rocket(&bad).is_none());
        bad = b.clone();
        bad[14..18].copy_from_slice(&f32::NAN.to_le_bytes());
        assert!(decode_rocket(&bad).is_none());
        b[1] = 0;
        assert!(!decode_rocket(&b).unwrap().start);
        // The report: 18 bytes, the server's DecodeRocketReport.
        let r = encode_report(77, REPORT_IMPACT, Vec3::new(10.0, -20.0, 5.5));
        assert_eq!(r.len(), 18);
        assert_eq!(r[0], OP_ROCKET_REPORT);
        assert_eq!(u32::from_le_bytes(r[1..5].try_into().unwrap()), 77);
        assert_eq!(r[5], REPORT_IMPACT);
        assert_eq!(f32_at(&r, 6), 10.0);
        assert_eq!(f32_at(&r, 14), 5.5);
    }

    #[test]
    fn a_body_near_the_path_is_struck() {
        let (a, b) = (Vec3::ZERO, Vec3::new(0.0, 0.0, -10.0));
        assert!(segment_distance(a, b, Vec3::new(1.0, 0.0, -5.0)) <= ROCKET_HIT_REACH);
        assert!(segment_distance(a, b, Vec3::new(3.0, 0.0, -5.0)) > ROCKET_HIT_REACH);
        // Past the end is measured from the end.
        assert!((segment_distance(a, b, Vec3::new(0.0, 0.0, -13.0)) - 3.0).abs() < 1e-5);
        assert!((segment_distance(a, a, Vec3::X) - 1.0).abs() < 1e-6);
    }

    #[test]
    fn the_turret_goes_down_within_reach_on_level_ground() {
        let feet = Vec3::new(10.0, 5.0, 10.0);
        assert!(turret_spot_ok(feet, feet + Vec3::new(4.0, 0.3, 0.0)));
        assert!(turret_spot_ok(feet, feet + Vec3::new(0.0, -1.0, -5.9)));
        assert!(!turret_spot_ok(feet, feet + Vec3::new(5.0, 0.0, 5.0)));
        assert!(!turret_spot_ok(feet, feet + Vec3::new(2.0, 4.0, 0.0)));
    }

    #[test]
    fn brackets_frame_the_body_and_stay_readable() {
        let (c, e) = bracket_box(Vec2::new(100.0, 100.0), Vec2::new(120.0, 160.0));
        assert_eq!(c, Vec2::new(110.0, 130.0));
        assert_eq!(e, Vec2::new(26.0, 66.0));
        // A far body still gets a box to see; a near one is capped.
        assert_eq!(
            bracket_box(Vec2::ZERO, Vec2::splat(1.0)).1,
            Vec2::splat(16.0)
        );
        assert_eq!(
            bracket_box(Vec2::ZERO, Vec2::splat(900.0)).1,
            Vec2::splat(180.0)
        );
    }

    /// The server's `EncodeCraftUp` and friends, byte for byte.
    fn up_bytes(streak: u8, guid: u64, seconds: u16, health: u8) -> Vec<u8> {
        let mut b = vec![OP_CRAFT, 1, streak];
        b.extend_from_slice(&guid.to_le_bytes());
        b.extend_from_slice(&seconds.to_le_bytes());
        b.push(health);
        b
    }

    fn named(kind: u8, head: &[u8], name: &str) -> Vec<u8> {
        let mut b = vec![OP_CRAFT, kind];
        b.extend_from_slice(head);
        b.push(name.len() as u8);
        b.extend_from_slice(name.as_bytes());
        b
    }

    fn line_bytes(flags: u8, first: u16, last: u16, points: &[[f32; 3]]) -> Vec<u8> {
        let mut b = vec![OP_CRAFT, 5, flags];
        b.extend_from_slice(&first.to_le_bytes());
        b.extend_from_slice(&last.to_le_bytes());
        b.push(points.len() as u8);
        for p in points {
            for v in p {
                b.extend_from_slice(&v.to_le_bytes());
            }
        }
        b
    }

    #[test]
    fn the_craft_news_matches_the_servers_wire() {
        let up = up_bytes(8, 0xF130_0016_D3D0_0042, 59, 87);
        assert_eq!(up.len(), 14);
        assert_eq!(
            decode_news(&up),
            Some(News::Up {
                streak: 8,
                guid: 0xF130_0016_D3D0_0042,
                seconds: 59,
                health: 87
            })
        );
        assert!(decode_news(&up[..13]).is_none());
        let mut gone = vec![OP_CRAFT, 2];
        gone.extend_from_slice(&77u64.to_le_bytes());
        gone.push(GONE_SHOT_DOWN);
        assert_eq!(
            decode_news(&gone),
            Some(News::Gone {
                guid: 77,
                why: GONE_SHOT_DOWN
            })
        );
        gone[10] = 9;
        assert!(decode_news(&gone).is_none());
        assert_eq!(
            decode_news(&named(3, &[4], "Hogger")),
            Some(News::Kill {
                streak: 4,
                victim: "Hogger".into()
            })
        );
        assert_eq!(
            decode_news(&named(4, &[12, 1], "Thrall")),
            Some(News::Downed {
                streak: 12,
                yours: true,
                other: "Thrall".into()
            })
        );
        // A name's length must be the rest of the message.
        let mut long = named(3, &[4], "Hogger");
        long.push(b'!');
        assert!(decode_news(&long).is_none());
        let pts = [[1.0, 2.0, 3.0], [9.0, 2.0, 3.5]];
        let line = line_bytes(LINE_HOSTILE, 3500, 4300, &pts);
        assert_eq!(line.len(), 8 + 2 * 12);
        assert_eq!(
            decode_news(&line),
            Some(News::StrikeLine {
                flags: LINE_HOSTILE,
                first_ms: 3500,
                last_ms: 4300,
                points: vec![Vec3::new(1.0, 2.0, 3.0), Vec3::new(9.0, 2.0, 3.5)]
            })
        );
        let mut bad = line.clone();
        bad[7] = 3;
        assert!(decode_news(&bad).is_none());
        bad = line.clone();
        bad[8..12].copy_from_slice(&f32::NAN.to_le_bytes());
        assert!(decode_news(&bad).is_none());
        assert!(decode_news(&line_bytes(0, 0, 0, &[[0.0; 3]; 17])).is_none());
        assert!(decode_news(&[OP_CRAFT]).is_none());
        assert!(decode_news(&[OP_CRAFT, 9]).is_none());
        assert!(decode_news(&[OP_ROCKET, 2]).is_none());
        // Its op follows the rocket's, free on the world channel.
        assert_eq!(OP_CRAFT, OP_ROCKET_REPORT + 1);
        assert_ne!(OP_CRAFT, crate::blast::OP_EXPLOSION);
    }

    #[test]
    fn the_hud_counts_down_each_craft_and_credits_its_kills() {
        let mut hud = SkyHud::default();
        let gunship = News::Up {
            streak: 8,
            guid: 5,
            seconds: 61,
            health: 100,
        };
        assert!(hud.apply(&gunship, 10.0).is_empty());
        let lines = hud.lines(10.0);
        assert_eq!(lines.len(), 1);
        assert!(lines[0].starts_with("SKY RIDER GUNSHIP  1:01"), "{lines:?}");
        assert!(lines[0].ends_with("[||||||||||] 100%"), "{lines:?}");
        // Counting down between the server's word, which then corrects it.
        assert!(hud.lines(30.5)[0].contains("0:41"), "{:?}", hud.lines(30.5));
        hud.apply(
            &News::Up {
                streak: 8,
                guid: 5,
                seconds: 30,
                health: 42,
            },
            31.0,
        );
        assert_eq!(hud.crafts.len(), 1);
        assert!(hud.lines(31.0)[0].contains("0:30  [|||||.....] 42%"));
        // A kill shows under it for a while.
        hud.apply(
            &News::Kill {
                streak: 8,
                victim: "Hogger".into(),
            },
            32.0,
        );
        assert_eq!(hud.lines(32.0)[1], "Sky Rider Gunship killed Hogger");
        assert_eq!(hud.lines(32.0 + CREDIT_SECS + 0.1).len(), 1);
        // Gone, by its guid.
        hud.apply(&News::Gone { guid: 5, why: 0 }, 40.0);
        assert!(hud.crafts.is_empty());
    }

    #[test]
    fn a_craft_shot_down_is_news_to_both_sides() {
        let mut hud = SkyHud::default();
        let mine = hud.apply(
            &News::Downed {
                streak: 12,
                yours: true,
                other: "Thrall".into(),
            },
            1.0,
        );
        assert!(mine.contains(&Say::Feed(
            "Your Gyrocopter Gunship was shot down by Thrall".into()
        )));
        let theirs = hud.apply(
            &News::Downed {
                streak: 4,
                yours: false,
                other: "Jaina".into(),
            },
            1.0,
        );
        assert!(theirs.contains(&Say::Banner(
            "DWARVEN SENTRY TURRET SHOT DOWN".into(),
            "You shot down Jaina's Dwarven Sentry Turret".into()
        )));
        // An enemy's strike line warns; one's own doesn't.
        let line = |flags| News::StrikeLine {
            flags,
            first_ms: 3000,
            last_ms: 4000,
            points: vec![Vec3::ZERO],
        };
        assert!(
            hud.apply(&line(LINE_HOSTILE), 2.0)
                .contains(&Say::Sound("RaidWarning"))
        );
        assert!(hud.apply(&line(LINE_MINE), 2.0).is_empty());
    }

    #[test]
    fn strike_marks_go_as_their_bombs_land() {
        assert_eq!(mark_lands(3500, 4300, 9, 0), 3.5);
        assert!((mark_lands(3500, 4300, 9, 8) - 4.3).abs() < 1e-6);
        assert!((mark_lands(3500, 4300, 9, 4) - 3.9).abs() < 1e-6);
        assert_eq!(mark_lands(3500, 4300, 1, 0), 3.5);
        // A last before the first is read as the first.
        assert_eq!(mark_lands(3500, 100, 3, 2), 3.5);
    }

    #[test]
    fn the_rocket_sensitivity_reads_its_cvar() {
        assert_eq!(parse_sensitivity(None), 1.0);
        assert_eq!(parse_sensitivity(Some("")), 1.0);
        assert_eq!(parse_sensitivity(Some("junk")), 1.0);
        assert_eq!(parse_sensitivity(Some(" 1.5 ")), 1.5);
        assert_eq!(parse_sensitivity(Some("0.01")), SENSITIVITY_MIN);
        assert_eq!(parse_sensitivity(Some("40")), SENSITIVITY_MAX);
        assert_eq!(parse_sensitivity(Some("-2")), 1.0);
        assert_eq!(parse_sensitivity(Some("NaN")), 1.0);
    }

    #[test]
    fn the_rockets_view_eases_after_its_nose() {
        let a = Quat::IDENTITY;
        let b = Quat::from_rotation_y(1.0);
        let half_frame = ease_view(a, b, 1.0 / 60.0, ROCKET_VIEW_RATE);
        let angle = half_frame.angle_between(a);
        // A frame closes part of the gap, not all of it.
        assert!(angle > 0.05 && angle < 0.5, "{angle}");
        // A quarter second closes nearly all of it.
        let mut q = a;
        for _ in 0..15 {
            q = ease_view(q, b, 1.0 / 60.0, ROCKET_VIEW_RATE);
        }
        assert!(q.angle_between(b) < 0.06);
        assert_eq!(ease_view(a, b, 0.0, ROCKET_VIEW_RATE), a);
    }

    #[test]
    fn craft_guids_are_known_by_their_entry() {
        let craft = (0xF130u64 << 48) | (93_501u64 << 24) | 42;
        assert!(is_craft(craft));
        assert!(!is_craft((0xF130u64 << 48) | (299u64 << 24) | 42));
        assert!(!is_craft(42)); // a player
        assert_eq!(streak_name(5), "Goblin Rocket");
        assert_eq!(streak_name(200), "Killstreak");
    }

    #[test]
    fn sides_by_race() {
        assert_eq!(alliance_race(1), Some(true));
        assert_eq!(alliance_race(7), Some(true));
        assert_eq!(alliance_race(2), Some(false));
        assert_eq!(alliance_race(8), Some(false));
        assert_eq!(alliance_race(0), None);
    }
}
