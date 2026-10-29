//! Player ships (the server's `fusion/Ships.h`, `game/Fusion/FusionShips.cpp`;
//! `design/PLAYER_SHIPS.md`), on fusion channel 14.
//!
//! A ship is a type-11 transport the server drives. Its pose comes on this channel, five times a
//! second while it moves; this module hands each pose to benilla's streamed transport drive
//! ([`benilla_app::fusion::StreamedDecks`]), which moves the deck, so walking on it and riding it
//! work as on the stock boats. Standing on a ship you own or crew, the **Ship** frame shows its
//! hull, speed and telegraph, with Take the Helm, the anchor, the Ship's Bell, Man a Gun and the
//! Cargo hold. At the helm the server roots you and steers by your own movement bindings: move
//! forward / back step the telegraph a notch, turn or strafe steer, jump and sit climb and dive (an
//! airship). At a deck gun the server hands you its gun; fire it as any gun. Escape leaves the helm
//! or the gun. `/ship` shows or hides the frame anywhere; `/shipbell` rings the bell (back aboard
//! from anywhere, every 30 minutes).
//!
//! The wire is the C++ encoders' byte for byte, little-endian, first byte the op.

use std::time::Instant;

use benilla_app::fusion::{
    ChannelMessage, FusionChannelReceived, LayerBoundKeys, LayerRiding, LayoutNames,
    SendFusionChannel, StreamedDeck, StreamedDecks,
};
use benilla_ui::script::UiScript;
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::input::{Mw2Input, takes_world_keys};
use crate::mode::FpsState;
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, RED, WHITE, WindowSpec, WowButton, text, u,
};

pub use wire::*;

/// The ships' wire (`Fusion::Ships` in `Ships.h` / `Ships.cpp`).
pub mod wire {
    pub const CHANNEL_SHIPS: u8 = 14;
    // Client to server.
    pub const OP_TAKE_HELM: u8 = 1;
    pub const OP_LEAVE_HELM: u8 = 2;
    pub const OP_KEYS: u8 = 3;
    pub const OP_ANCHOR: u8 = 4;
    pub const OP_BELL: u8 = 5;
    pub const OP_MAN_GUN: u8 = 6;
    pub const OP_LEAVE_GUN: u8 = 7;
    pub const OP_CARGO: u8 = 8;
    pub const OP_STOW: u8 = 9;
    pub const OP_TAKE: u8 = 10;
    /// Man the free gun nearest (`Ships::kNoMount`).
    pub const NO_MOUNT: u8 = 0xFF;
    // Server to client.
    pub const OP_POSE: u8 = 20;
    pub const OP_GONE: u8 = 21;
    pub const OP_HELM: u8 = 22;
    pub const OP_GUN: u8 = 23;
    pub const OP_CARGO_LIST: u8 = 24;

    pub const KEY_AHEAD: u8 = 0x01;
    pub const KEY_ASTERN: u8 = 0x02;
    pub const KEY_PORT: u8 = 0x04;
    pub const KEY_STARBOARD: u8 = 0x08;
    pub const KEY_CLIMB: u8 = 0x10;
    pub const KEY_DIVE: u8 = 0x20;

    pub const POSE_ANCHORED: u8 = 0x01;
    pub const POSE_PILOTED: u8 = 0x02;
    pub const POSE_MINE: u8 = 0x04;
    pub const POSE_NEGLECTED: u8 = 0x08;
    pub const POSE_LISTING: u8 = 0x10;
    /// The receiver may man the guns, take the helm (`ShipCrew.h`).
    pub const POSE_CREW: u8 = 0x20;
    pub const POSE_HELMSMAN: u8 = 0x40;

    pub const CLASS_SLOOP: u8 = 1;
    pub const CLASS_AIRSHIP: u8 = 2;

    /// Through the name's length byte.
    const POSE_FIXED: usize = 48;

    #[derive(Clone, Debug, Default, PartialEq)]
    pub struct Pose {
        pub guid: u64,
        pub class: u8,
        pub flags: u8,
        pub notch: i8,
        pub hull_pct: u8,
        /// Raw WoW coordinates and radians.
        pub pos: [f32; 3],
        pub heading: f32,
        pub vel: [f32; 3],
        pub yaw_rate: f32,
        pub bell_secs: u16,
        pub name: String,
    }

    fn f32_at(b: &[u8], at: usize) -> f32 {
        f32::from_le_bytes(b[at..at + 4].try_into().unwrap())
    }
    fn u64_at(b: &[u8], at: usize) -> u64 {
        u64::from_le_bytes(b[at..at + 8].try_into().unwrap())
    }

    pub fn decode_pose(b: &[u8]) -> Option<Pose> {
        if b.len() < POSE_FIXED || b[0] != OP_POSE {
            return None;
        }
        let n = b[POSE_FIXED - 1] as usize;
        if b.len() != POSE_FIXED + n {
            return None;
        }
        let f = |i: usize| f32_at(b, 13 + 4 * i);
        Some(Pose {
            guid: u64_at(b, 1),
            class: b[9],
            flags: b[10],
            notch: b[11] as i8,
            hull_pct: b[12],
            pos: [f(0), f(1), f(2)],
            heading: f(3),
            vel: [f(4), f(5), f(6)],
            yaw_rate: f(7),
            bell_secs: u16::from_le_bytes([b[45], b[46]]),
            name: String::from_utf8_lossy(&b[POSE_FIXED..]).into_owned(),
        })
    }

    /// Op 21 (the ship left the world) or 22 (you are at this helm; 0: none): the guid.
    pub fn decode_guid(b: &[u8], op: u8) -> Option<u64> {
        (b.len() == 9 && b[0] == op).then(|| u64_at(b, 1))
    }

    pub fn encode_keys(keys: u8) -> Vec<u8> {
        vec![OP_KEYS, keys]
    }

    /// Op 23: the deck gun we man (`None`: none).
    pub fn decode_gun(b: &[u8]) -> Option<(u64, Option<u8>)> {
        (b.len() == 10 && b[0] == OP_GUN)
            .then(|| (u64_at(b, 1), (b[9] != NO_MOUNT).then_some(b[9])))
    }

    pub fn encode_stow(entry: u32, count: u32) -> Vec<u8> {
        let mut o = vec![OP_STOW];
        o.extend_from_slice(&entry.to_le_bytes());
        o.extend_from_slice(&count.to_le_bytes());
        o
    }

    /// One line of the hold or of the bags.
    #[derive(Clone, Debug, Default, PartialEq, Eq)]
    pub struct CargoLine {
        pub entry: u32,
        pub count: u32,
        pub name: String,
    }

    /// Op 24: the hold's slots, its stacks, and the bags' plain goods.
    #[derive(Clone, Debug, Default, PartialEq, Eq)]
    pub struct Cargo {
        pub slots: u8,
        pub hold: Vec<CargoLine>,
        pub bags: Vec<CargoLine>,
    }

    fn lines(b: &[u8], at: &mut usize) -> Option<Vec<CargoLine>> {
        let n = *b.get(*at)? as usize;
        *at += 1;
        let mut v = Vec::with_capacity(n);
        for _ in 0..n {
            let head = b.get(*at..*at + 9)?;
            let len = head[8] as usize;
            let name = b.get(*at + 9..*at + 9 + len)?;
            v.push(CargoLine {
                entry: u32::from_le_bytes(head[0..4].try_into().unwrap()),
                count: u32::from_le_bytes(head[4..8].try_into().unwrap()),
                name: String::from_utf8_lossy(name).into_owned(),
            });
            *at += 9 + len;
        }
        Some(v)
    }

    pub fn decode_cargo(b: &[u8]) -> Option<Cargo> {
        if b.len() < 4 || b[0] != OP_CARGO_LIST {
            return None;
        }
        let mut at = 2;
        let hold = lines(b, &mut at)?;
        let bags = lines(b, &mut at)?;
        (at == b.len()).then_some(Cargo {
            slots: b[1],
            hold,
            bags,
        })
    }

    pub fn notch_name(notch: i8) -> &'static str {
        match notch {
            -1 => "Full Astern",
            1 => "Slow Ahead",
            2 => "Half Ahead",
            3 => "Full Ahead",
            _ => "All Stop",
        }
    }
}

/// A ship as last heard.
#[derive(Clone, Debug)]
pub struct Ship {
    pub pose: Pose,
    pub at: Instant,
}

/// The ships this client has heard of, the helm we hold, and the frame's state.
#[derive(Resource, Debug, Default)]
pub struct Ships {
    pub ships: HashMap<u64, Ship>,
    /// The ship whose helm the server says we hold.
    pub helm: Option<u64>,
    /// `/ship`: the frame shown or hidden by hand (`None`: by where we stand).
    pub shown: Option<bool>,
    /// The deck gun we man: the ship and the mounting.
    pub gun: Option<(u64, u8)>,
    /// The hold, as last sent, and whether its window is open.
    pub cargo: Cargo,
    pub cargo_open: bool,
    cargo_version: u32,
    /// The keys last sent, and when.
    sent_keys: u8,
    sent_at: Option<Instant>,
    seq: u32,
    built: bool,
}

impl Ships {
    /// The ship the frame is about: the one at whose helm (or gun) we are, else the one we stand
    /// on that we own or crew, else (shown by hand) any of ours.
    pub fn subject(&self, riding: Option<u64>) -> Option<&Ship> {
        if let Some(s) = self
            .helm
            .or(self.gun.map(|(g, _)| g))
            .and_then(|g| self.ships.get(&g))
        {
            return Some(s);
        }
        if let Some(s) = riding
            .and_then(|g| self.ships.get(&g))
            .filter(|s| s.pose.flags & (POSE_MINE | POSE_CREW | POSE_HELMSMAN) != 0)
        {
            return Some(s);
        }
        if self.shown == Some(true) {
            return self.ships.values().find(|s| s.pose.flags & POSE_MINE != 0);
        }
        None
    }
}

/// What the Ship frame shows.
#[derive(Clone, Debug, PartialEq)]
pub struct FrameView {
    pub title: String,
    pub lines: Vec<(String, Color)>,
    pub helm: &'static str,
    pub helm_on: bool,
    pub anchor: &'static str,
    pub anchor_on: bool,
    pub bell: String,
    pub bell_on: bool,
    pub gun: &'static str,
    pub gun_on: bool,
    pub cargo_on: bool,
}

/// The frame for a ship: `aboard` is whether we stand on it, `at_helm` whether we steer it,
/// `at_gun` whether we man one of its guns.
pub fn frame_view(
    p: &Pose,
    aboard: bool,
    at_helm: bool,
    at_gun: bool,
    bell_left: u32,
) -> FrameView {
    let mine = p.flags & POSE_MINE != 0;
    let helmsman = mine || p.flags & POSE_HELMSMAN != 0;
    let crew = mine || p.flags & POSE_CREW != 0;
    let kind = if p.class == CLASS_AIRSHIP {
        "Airship"
    } else {
        "Sloop"
    };
    let speed = (p.vel[0] * p.vel[0] + p.vel[1] * p.vel[1]).sqrt();
    let hull_color = if p.hull_pct < 25 {
        RED
    } else if p.hull_pct < 60 {
        GOLD
    } else {
        GREEN
    };
    let mut lines = vec![
        (format!("{kind} — Hull {}%", p.hull_pct), hull_color),
        if p.flags & POSE_ANCHORED != 0 {
            ("At anchor".to_string(), DIM)
        } else {
            (
                format!("{} — {:.0} yd/s", notch_name(p.notch), speed),
                WHITE,
            )
        },
    ];
    if p.class == CLASS_AIRSHIP {
        lines.push((format!("Altitude {:.0}", p.pos[2]), WHITE));
    }
    if p.flags & POSE_NEGLECTED != 0 {
        lines.push(("Upkeep overdue: half speed".to_string(), RED));
    }
    if p.flags & POSE_LISTING != 0 {
        lines.push(("Listing badly: repair at a Shipwright".to_string(), RED));
    }
    if at_helm {
        let climb = if p.class == CLASS_AIRSHIP {
            ", jump/sit climb"
        } else {
            ""
        };
        lines.push((format!("Forward/back telegraph, turn to steer{climb}"), DIM));
    } else if at_gun {
        lines.push(("At a deck gun: fire as with any gun".to_string(), DIM));
    } else if !aboard {
        lines.push(("Not aboard".to_string(), DIM));
    }
    let piloted_by_other = p.flags & POSE_PILOTED != 0 && !at_helm;
    FrameView {
        title: p.name.clone(),
        lines,
        helm: if at_helm {
            "Leave the Helm"
        } else {
            "Take the Helm"
        },
        helm_on: aboard && helmsman && !piloted_by_other && !at_gun,
        anchor: if p.flags & POSE_ANCHORED != 0 {
            "Weigh Anchor"
        } else {
            "Drop Anchor"
        },
        anchor_on: aboard && helmsman,
        bell: if bell_left > 0 {
            format!("Bell ({}m)", bell_left.div_ceil(60))
        } else {
            "Ring the Bell".to_string()
        },
        bell_on: mine && bell_left == 0 && !aboard,
        gun: if at_gun { "Leave the Gun" } else { "Man a Gun" },
        gun_on: aboard && crew && !at_helm,
        cargo_on: aboard && mine,
    }
}

/// Which stock movement command steers which helm key: the player's own bindings drive the ship.
pub const HELM_COMMANDS: [(&str, u8); 8] = [
    ("MOVEFORWARD", KEY_AHEAD),
    ("MOVEBACKWARD", KEY_ASTERN),
    ("TURNLEFT", KEY_PORT),
    ("STRAFELEFT", KEY_PORT),
    ("TURNRIGHT", KEY_STARBOARD),
    ("STRAFERIGHT", KEY_STARBOARD),
    ("JUMP", KEY_CLIMB),
    ("SITORSTAND", KEY_DIVE),
];

/// The stock defaults, for before the key table is in (`WTF\DefaultBindings.wtf`).
pub const DEFAULT_HELM_KEYS: [(&str, &str); 8] = [
    ("W", "MOVEFORWARD"),
    ("S", "MOVEBACKWARD"),
    ("A", "TURNLEFT"),
    ("Q", "STRAFELEFT"),
    ("D", "TURNRIGHT"),
    ("E", "STRAFERIGHT"),
    ("SPACE", "JUMP"),
    ("X", "SITORSTAND"),
];

/// The helm's key bits from the keys held (as binding tokens: `W`, `SPACE`, `UP`) and the key
/// table's `(chord, command)` pairs. A chord's modifiers are ignored, so a held Shift doesn't
/// drop the throttle.
pub fn helm_keys_bound<'a>(
    held: &[String],
    bound: impl IntoIterator<Item = (&'a str, &'a str)>,
) -> u8 {
    let mut keys = 0;
    for (chord, command) in bound {
        let token = crate::keybinds::base_token(chord);
        if !held.iter().any(|h| h.eq_ignore_ascii_case(token)) {
            continue;
        }
        for (c, bit) in HELM_COMMANDS {
            if command.eq_ignore_ascii_case(c) {
                keys |= bit;
            }
        }
    }
    keys
}

/// The keys go up when they change, and once a second while held (a lost update heals).
pub fn should_send(now_keys: u8, sent_keys: u8, since_secs: Option<f32>) -> bool {
    now_keys != sent_keys || (now_keys != 0 && since_secs.is_none_or(|s| s >= 1.0))
}

pub struct ShipsPlugin;

impl Plugin for ShipsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Ships>().add_systems(
            Update,
            (
                read_channel,
                slash,
                helm_input,
                helm_camera,
                spawn_frame,
                draw,
                click,
                spawn_cargo,
                draw_cargo,
                click_cargo,
            )
                .chain()
                .in_set(Mw2Set::Hud),
        );
    }
}

fn read_channel(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut ships: ResMut<Ships>,
    mut decks: ResMut<StreamedDecks>,
    state: Res<FpsState>,
) {
    if !state.in_world && !ships.ships.is_empty() {
        ships.ships.clear();
        ships.helm = None;
        ships.gun = None;
        ships.cargo_open = false;
        decks.0.clear();
    }
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_SHIPS || m.body.is_empty() {
            continue;
        }
        let b = &m.body;
        match b[0] {
            OP_POSE => {
                let Some(p) = decode_pose(b) else { continue };
                ships.seq = ships.seq.wrapping_add(1);
                decks.0.insert(
                    p.guid,
                    StreamedDeck {
                        pos: p.pos,
                        heading: p.heading,
                        vel: p.vel,
                        yaw_rate: p.yaw_rate,
                        seq: ships.seq,
                    },
                );
                ships.ships.insert(
                    p.guid,
                    Ship {
                        pose: p,
                        at: Instant::now(),
                    },
                );
            }
            OP_GONE => {
                if let Some(g) = decode_guid(b, OP_GONE) {
                    ships.ships.remove(&g);
                    decks.0.remove(&g);
                    if ships.helm == Some(g) {
                        ships.helm = None;
                    }
                }
            }
            OP_GUN => {
                if let Some((g, mount)) = decode_gun(b) {
                    ships.gun = mount.map(|m| (g, m));
                }
            }
            OP_CARGO_LIST => {
                if let Some(c) = decode_cargo(b) {
                    ships.cargo = c;
                    ships.cargo_open = true;
                    ships.cargo_version = ships.cargo_version.wrapping_add(1);
                }
            }
            OP_HELM => {
                if let Some(g) = decode_guid(b, OP_HELM) {
                    ships.helm = (g != 0).then_some(g);
                    ships.sent_keys = 0;
                    ships.sent_at = None;
                }
            }
            _ => {}
        }
    }
}

const SLASH_LUA: &str = r#"
if not BenillaMw2Ships then
  BenillaMw2Ships = { toggle = nil, bell = nil }
  if SlashCmdList then
    SLASH_BENILLAMW2SHIP1 = "/ship"
    SlashCmdList["BENILLAMW2SHIP"] = function() BenillaMw2Ships.toggle = 1 end
    SLASH_BENILLAMW2SHIPBELL1 = "/shipbell"
    SlashCmdList["BENILLAMW2SHIPBELL"] = function() BenillaMw2Ships.bell = 1 end
  end
end
local S = BenillaMw2Ships
local t, b = S.toggle ~= nil, S.bell ~= nil
S.toggle = nil S.bell = nil
return t, b
"#;

/// `/ship` and `/shipbell`, installed into the world's VM and polled.
fn slash(
    state: Res<FpsState>,
    script: Option<NonSend<UiScript>>,
    mut ships: ResMut<Ships>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    let Some(script) = script.filter(|_| state.in_world) else {
        return;
    };
    let Some((toggle, bell)) = crate::lua::eval::<(bool, bool)>(&script, SLASH_LUA) else {
        return;
    };
    if toggle {
        ships.shown = match ships.shown {
            Some(true) => Some(false),
            _ => Some(true),
        };
    }
    if bell {
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_SHIPS,
            body: vec![OP_BELL],
        }));
    }
}

/// At the helm: the held keys up to the server, by the player's movement bindings; Escape
/// leaves the helm, or the deck gun.
fn helm_input(
    mut state: ResMut<FpsState>,
    keys: Res<ButtonInput<KeyCode>>,
    input: Res<Mw2Input>,
    bound: Option<Res<LayerBoundKeys>>,
    layout: Option<Res<LayoutNames>>,
    mut ships: ResMut<Ships>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    // crate::input hands the helm its keys while we hold it.
    let at_helm = ships.helm.is_some() && state.in_world;
    if state.helm != at_helm {
        state.helm = at_helm;
    }
    let world = takes_world_keys(state.in_world, state.chat_focus, state.typing);
    if ships.gun.is_some() && ships.helm.is_none() {
        if world && input.escape {
            send.write(SendFusionChannel(ChannelMessage {
                channel: CHANNEL_SHIPS,
                body: vec![OP_LEAVE_GUN],
            }));
        }
        return;
    }
    if ships.helm.is_none() {
        return;
    }
    if world && input.leave_helm {
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_SHIPS,
            body: vec![OP_LEAVE_HELM],
        }));
        return;
    }
    let now = if world {
        let us = LayoutNames::default();
        let layout = layout.as_deref().unwrap_or(&us);
        let held: Vec<String> = keys
            .get_pressed()
            .filter_map(|k| benilla_app::fusion::binding_key_name(*k, layout))
            .collect();
        match bound.as_deref().filter(|b| !b.keys.is_empty()) {
            Some(b) => helm_keys_bound(&held, b.keys.iter().map(|(k, c)| (k.as_str(), c.as_str()))),
            None => helm_keys_bound(&held, DEFAULT_HELM_KEYS),
        }
    } else {
        0
    };
    let since = ships.sent_at.map(|t| t.elapsed().as_secs_f32());
    if should_send(now, ships.sent_keys, since) {
        ships.sent_keys = now;
        ships.sent_at = Some(Instant::now());
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_SHIPS,
            body: encode_keys(now),
        }));
    }
}

/// How far the camera pulls back at the helm (yards), to see the whole hull.
pub const HELM_CAMERA: f32 = 30.0;

/// At the helm the view goes third person and pulls back over the ship; leaving puts the view
/// back as it was. The pull waits two frames, as the V toggle's does, for first person's zoom-in.
fn helm_camera(
    ships: Res<Ships>,
    state: Res<FpsState>,
    script: Option<NonSend<UiScript>>,
    mut third: ResMut<crate::thirdperson::ThirdPerson>,
    mut was: Local<Option<bool>>,
    mut settle: Local<u8>,
) {
    match (ships.helm.is_some(), *was) {
        (true, None) => {
            *was = Some(third.on);
            third.on = true;
            *settle = 2;
        }
        (false, Some(before)) => {
            third.on = before;
            *was = None;
            *settle = 0;
        }
        _ => {}
    }
    if *settle > 0 && state.active {
        *settle -= 1;
        if *settle == 0
            && let Some(script) = &script
        {
            crate::lua::run(script, &format!("CameraZoomOut({HELM_CAMERA})"));
        }
    }
}

#[derive(Component)]
struct ShipRoot;
#[derive(Component)]
struct ShipLines;
#[derive(Component, Clone, Copy, PartialEq, Eq)]
enum ShipButton {
    Helm,
    Anchor,
    Bell,
    Gun,
    Cargo,
}

#[derive(Resource)]
struct ShipFont(Handle<Font>);

const WIDTH: f32 = 300.0;

fn spawn_frame(
    mut commands: Commands,
    assets: Res<AssetServer>,
    state: Res<FpsState>,
    mut ships: ResMut<Ships>,
) {
    if ships.built || !state.in_world {
        return;
    }
    ships.built = true;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: "ship",
        title: "Ship".into(),
        frame: Frame::Dialog,
        width: Some(WIDTH),
        movable: true,
        // Above the action bars, right of centre, out of the crosshair's way.
        anchor: Anchor {
            at: Vec2::new(0.5, 1.0),
            pivot: Vec2::new(0.0, 1.0),
            offset: Vec2::new(120.0, -150.0),
        },
        z: i32::MAX - 4,
    };
    wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        (ShipRoot, Visibility::Hidden),
        None::<()>,
        |root| {
            root.spawn((
                ShipLines,
                Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: u(2.0),
                    margin: UiRect::bottom(u(6.0)),
                    ..default()
                },
            ));
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                justify_content: JustifyContent::SpaceBetween,
                ..default()
            })
            .with_children(|row| {
                row.spawn(wow_ui::button(f, "Take the Helm", 94.0, ShipButton::Helm));
                row.spawn(wow_ui::button(f, "Drop Anchor", 94.0, ShipButton::Anchor));
                row.spawn(wow_ui::button(f, "Ring the Bell", 94.0, ShipButton::Bell));
            });
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                justify_content: JustifyContent::SpaceBetween,
                margin: UiRect::top(u(2.0)),
                ..default()
            })
            .with_children(|row| {
                row.spawn(wow_ui::button(f, "Man a Gun", 142.0, ShipButton::Gun));
                row.spawn(wow_ui::button(f, "Cargo Hold", 142.0, ShipButton::Cargo));
            });
        },
        |_| {},
    );
    commands.insert_resource(ShipFont(font));
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn draw(
    mut commands: Commands,
    ships: Res<Ships>,
    riding: Res<LayerRiding>,
    font: Option<Res<ShipFont>>,
    mut root: Query<&mut Visibility, With<ShipRoot>>,
    lines: Query<(Entity, Option<&Children>), With<ShipLines>>,
    mut buttons: Query<(&ShipButton, &mut WowButton, &Children)>,
    mut captions: Query<&mut Text, With<wow_ui::ButtonText>>,
    mut drawn: Local<Option<FrameView>>,
) {
    let Some(font) = font else {
        return;
    };
    let subject = ships
        .subject(riding.0)
        .filter(|_| ships.shown != Some(false));
    let want = if subject.is_some() {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut root {
        v.set_if_neq(want);
    }
    let Some(ship) = subject else {
        *drawn = None;
        return;
    };
    let aboard = riding.0 == Some(ship.pose.guid);
    let at_helm = ships.helm == Some(ship.pose.guid);
    let bell_left =
        u32::from(ship.pose.bell_secs).saturating_sub(ship.at.elapsed().as_secs() as u32);
    let at_gun = ships.gun.is_some_and(|(g, _)| g == ship.pose.guid);
    let view = frame_view(&ship.pose, aboard, at_helm, at_gun, bell_left);
    if drawn.as_ref() == Some(&view) {
        return;
    }
    if let Ok((e, children)) = lines.single() {
        for c in children.into_iter().flatten() {
            commands.entity(*c).despawn();
        }
        commands.entity(e).with_children(|l| {
            l.spawn(text(&font.0, &view.title, 14.0, GOLD));
            for (s, c) in &view.lines {
                l.spawn(text(&font.0, s, 11.0, *c));
            }
        });
    }
    for (which, mut b, children) in &mut buttons {
        let (label, on) = match which {
            ShipButton::Helm => (view.helm.to_string(), view.helm_on),
            ShipButton::Anchor => (view.anchor.to_string(), view.anchor_on),
            ShipButton::Bell => (view.bell.clone(), view.bell_on),
            ShipButton::Gun => (view.gun.to_string(), view.gun_on),
            ShipButton::Cargo => ("Cargo Hold".to_string(), view.cargo_on),
        };
        b.disabled = !on;
        for c in children {
            if let Ok(mut t) = captions.get_mut(*c) {
                if t.0 != label {
                    t.0 = label.clone();
                }
            }
        }
    }
    *drawn = Some(view);
}

fn click(
    ships: Res<Ships>,
    riding: Res<LayerRiding>,
    buttons: Query<(&Interaction, &ShipButton, &WowButton), Changed<Interaction>>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    for (i, which, b) in &buttons {
        if *i != Interaction::Pressed || b.disabled {
            continue;
        }
        let Some(ship) = ships.subject(riding.0) else {
            continue;
        };
        let op = match which {
            ShipButton::Helm if ships.helm == Some(ship.pose.guid) => OP_LEAVE_HELM,
            ShipButton::Helm => OP_TAKE_HELM,
            ShipButton::Anchor => OP_ANCHOR,
            ShipButton::Bell => OP_BELL,
            ShipButton::Gun if ships.gun.is_some() => OP_LEAVE_GUN,
            ShipButton::Gun => OP_MAN_GUN,
            ShipButton::Cargo => OP_CARGO,
        };
        let body = if op == OP_MAN_GUN {
            vec![op, NO_MOUNT]
        } else {
            vec![op]
        };
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_SHIPS,
            body,
        }));
    }
}

// ── The cargo hold ──────────────────────────────────────────────────────────────────────────────

#[derive(Component)]
struct CargoRoot;
#[derive(Component)]
struct CargoList;
#[derive(Component)]
struct CargoClose;
#[derive(Component, Clone, Copy)]
enum CargoButton {
    Take(u8),
    Stow(u32, u32),
}

/// The hold window's lines: a header, then (text, button) rows.
pub fn cargo_rows(c: &Cargo) -> Vec<(String, Option<(&'static str, bool)>)> {
    let mut rows = vec![(
        format!("In the hold ({} of {} slots)", c.hold.len(), c.slots),
        None,
    )];
    if c.hold.is_empty() {
        rows.push(("Nothing yet.".to_string(), None));
    }
    for l in &c.hold {
        rows.push((
            format!("{} x{}", label_of(l), l.count),
            Some(("Take", true)),
        ));
    }
    rows.push(("In your bags (plain goods)".to_string(), None));
    if c.bags.is_empty() {
        rows.push(("Nothing that can go in the hold.".to_string(), None));
    }
    let full = c.hold.len() >= c.slots as usize;
    for l in &c.bags {
        let fits = !full || c.hold.iter().any(|h| h.entry == l.entry);
        rows.push((
            format!("{} x{}", label_of(l), l.count),
            Some(("Stow", fits)),
        ));
    }
    rows
}

fn label_of(l: &CargoLine) -> String {
    if l.name.is_empty() {
        format!("Item {}", l.entry)
    } else {
        l.name.clone()
    }
}

fn spawn_cargo(
    mut commands: Commands,
    font: Option<Res<ShipFont>>,
    root: Query<(), With<CargoRoot>>,
) {
    let Some(font) = font else {
        return;
    };
    if !root.is_empty() {
        return;
    }
    let spec = WindowSpec {
        key: "ship cargo",
        title: "Cargo Hold".into(),
        frame: Frame::Dialog,
        width: Some(300.0),
        movable: true,
        anchor: Anchor {
            at: Vec2::new(0.5, 0.5),
            pivot: Vec2::new(0.5, 0.5),
            offset: Vec2::ZERO,
        },
        z: i32::MAX - 3,
    };
    wow_ui::spawn_window(
        &mut commands,
        &font.0,
        spec,
        (CargoRoot, Visibility::Hidden),
        Some(CargoClose),
        |root| {
            wow_ui::scroll_frame(root, 280.0, None, CargoList, |_| {});
        },
        |_| {},
    );
}

#[allow(clippy::type_complexity)]
fn draw_cargo(
    mut commands: Commands,
    mut ships: ResMut<Ships>,
    riding: Res<LayerRiding>,
    font: Option<Res<ShipFont>>,
    mut root: Query<&mut Visibility, With<CargoRoot>>,
    list: Query<(Entity, Option<&Children>), With<CargoList>>,
    mut drawn: Local<Option<u32>>,
) {
    let Some(font) = font else {
        return;
    };
    // Off the deck of your own ship, the hold shuts.
    let aboard_mine = riding
        .0
        .and_then(|g| ships.ships.get(&g))
        .is_some_and(|s| s.pose.flags & POSE_MINE != 0);
    if ships.cargo_open && !aboard_mine {
        ships.cargo_open = false;
    }
    let want = if ships.cargo_open {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut root {
        v.set_if_neq(want);
    }
    if !ships.cargo_open || *drawn == Some(ships.cargo_version) {
        return;
    }
    let Ok((e, children)) = list.single() else {
        return;
    };
    *drawn = Some(ships.cargo_version);
    for c in children.into_iter().flatten() {
        commands.entity(*c).despawn();
    }
    let rows = cargo_rows(&ships.cargo);
    let (mut hold_i, mut bag_i) = (0u8, 0usize);
    commands.entity(e).with_children(|l| {
        for (line, button) in rows {
            let Some((label, on)) = button else {
                l.spawn(text(&font.0, &line, 12.0, GOLD));
                continue;
            };
            let action = if label == "Take" {
                hold_i += 1;
                CargoButton::Take(hold_i - 1)
            } else {
                let b = &ships.cargo.bags[bag_i];
                bag_i += 1;
                CargoButton::Stow(b.entry, b.count)
            };
            l.spawn(Node {
                flex_direction: FlexDirection::Row,
                justify_content: JustifyContent::SpaceBetween,
                align_items: AlignItems::Center,
                width: u(270.0),
                ..default()
            })
            .with_children(|r| {
                r.spawn(text(&font.0, &line, 11.0, WHITE));
                let state = WowButton {
                    disabled: !on,
                    ..WowButton::new(wow_ui::Kind::Panel)
                };
                r.spawn(wow_ui::button_fit(&font.0, label, state, action));
            });
        }
    });
}

fn click_cargo(
    mut ships: ResMut<Ships>,
    buttons: Query<(&Interaction, &CargoButton, &WowButton), Changed<Interaction>>,
    close: Query<&Interaction, (Changed<Interaction>, With<CargoClose>)>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    if close.iter().any(|i| *i == Interaction::Pressed) {
        ships.cargo_open = false;
    }
    for (i, which, b) in &buttons {
        if *i != Interaction::Pressed || b.disabled {
            continue;
        }
        let body = match *which {
            CargoButton::Take(slot) => vec![OP_TAKE, slot],
            CargoButton::Stow(entry, count) => encode_stow(entry, count),
        };
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_SHIPS,
            body,
        }));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The C++ test's pose (ShipTests.cpp `ships_the_wire_round_trips_and_refuses_the_malformed`),
    /// encoded as `EncodePose` lays it out.
    fn cpp_pose() -> Vec<u8> {
        let mut b = vec![OP_POSE];
        b.extend_from_slice(&0x00F1_1000_2E9F_0001u64.to_le_bytes());
        b.extend_from_slice(&[2, POSE_PILOTED | POSE_MINE, (-1i8) as u8, 73]);
        for f in [-1000.5f32, 2000.25, 120.0, 3.0, 1.0, -2.0, 0.5, -0.3] {
            b.extend_from_slice(&f.to_le_bytes());
        }
        b.extend_from_slice(&1200u16.to_le_bytes());
        let name = b"Thrall's Airship";
        b.push(name.len() as u8);
        b.extend_from_slice(name);
        b
    }

    #[test]
    fn the_wire_matches_the_server() {
        let b = cpp_pose();
        assert_eq!(b.len(), 48 + 16);
        let p = decode_pose(&b).expect("decodes");
        assert_eq!(p.guid, 0x00F1_1000_2E9F_0001);
        assert_eq!((p.class, p.notch, p.hull_pct), (CLASS_AIRSHIP, -1, 73));
        assert_eq!(p.pos, [-1000.5, 2000.25, 120.0]);
        assert_eq!(
            (p.heading, p.vel, p.yaw_rate),
            (3.0, [1.0, -2.0, 0.5], -0.3)
        );
        assert_eq!((p.bell_secs, p.name.as_str()), (1200, "Thrall's Airship"));
        assert!(decode_pose(&b[..b.len() - 1]).is_none());
        let helm = [OP_HELM, 8, 7, 6, 5, 4, 3, 2, 1];
        assert_eq!(decode_guid(&helm, OP_HELM), Some(0x0102_0304_0506_0708));
        assert_eq!(decode_guid(&helm, OP_GONE), None);
        assert_eq!(encode_keys(KEY_PORT), vec![OP_KEYS, KEY_PORT]);
    }

    #[test]
    fn the_keys_map_to_the_servers_bits_and_go_up_on_change_or_each_second() {
        let held = |k: &[&str]| k.iter().map(|s| s.to_string()).collect::<Vec<_>>();
        assert_eq!(helm_keys_bound(&held(&["W", "D"]), DEFAULT_HELM_KEYS), 0x09);
        assert_eq!(helm_keys_bound(&held(&[]), DEFAULT_HELM_KEYS), 0);
        assert_eq!(
            helm_keys_bound(
                &held(&["W", "S", "Q", "E", "SPACE", "X"]),
                DEFAULT_HELM_KEYS
            ),
            0x3F
        );
        // The player's own table: the arrows, a modifier chord, and an unrelated key.
        let mine = [
            ("UP", "MOVEFORWARD"),
            ("SHIFT-LEFT", "TURNLEFT"),
            ("W", "TOGGLEAUTORUN"),
        ];
        assert_eq!(helm_keys_bound(&held(&["UP"]), mine), KEY_AHEAD);
        assert_eq!(helm_keys_bound(&held(&["LEFT"]), mine), KEY_PORT);
        assert_eq!(
            helm_keys_bound(&held(&["W"]), mine),
            0,
            "W isn't forward here"
        );
        assert!(should_send(1, 0, Some(0.1)));
        assert!(!should_send(1, 1, Some(0.5)));
        assert!(should_send(1, 1, Some(1.0)), "held: a heartbeat");
        assert!(!should_send(0, 0, None), "nothing held, nothing to say");
    }

    #[test]
    fn the_frame_reads_like_the_ship() {
        let mut p = decode_pose(&cpp_pose()).unwrap();
        let v = frame_view(&p, true, true, false, 0);
        assert_eq!(v.title, "Thrall's Airship");
        assert_eq!(v.helm, "Leave the Helm");
        assert!(v.lines.iter().any(|(s, _)| s.contains("jump/sit climb")));
        assert!(!v.gun_on, "at the helm: no gun");
        assert!(v.lines.iter().any(|(s, _)| s.starts_with("Altitude 120")));
        assert!(!v.bell_on, "aboard: no bell to ring");
        p.flags = POSE_ANCHORED | POSE_MINE | POSE_LISTING;
        p.class = CLASS_SLOOP;
        let v = frame_view(&p, true, false, false, 0);
        assert_eq!((v.helm, v.anchor), ("Take the Helm", "Weigh Anchor"));
        assert!(v.helm_on && v.gun_on && v.cargo_on);
        assert!(v.lines.iter().any(|(s, _)| s == "At anchor"));
        assert!(v.lines.iter().any(|(s, _)| s.contains("Listing")));
        assert!(!v.lines.iter().any(|(s, _)| s.starts_with("Altitude")));
        // Someone else at the helm; off the deck, the bell on its cooldown.
        p.flags = POSE_PILOTED | POSE_MINE;
        let v = frame_view(&p, false, false, false, 61);
        assert!(!v.helm_on && !v.anchor_on && !v.bell_on && !v.cargo_on);
        assert_eq!(v.bell, "Bell (2m)");
        let v = frame_view(&p, false, false, false, 0);
        assert!(v.bell_on);
        // Crew, not owner: the guns, no helm, no hold, no bell; a gunner sees Leave the Gun.
        p.flags = POSE_CREW;
        let v = frame_view(&p, true, false, false, 0);
        assert!(v.gun_on && !v.helm_on && !v.anchor_on && !v.cargo_on);
        let v = frame_view(&p, true, false, true, 0);
        assert_eq!(v.gun, "Leave the Gun");
        assert!(!v.helm_on);
        // A helmsman by the owner's rights.
        p.flags = POSE_HELMSMAN;
        let v = frame_view(&p, true, false, false, 0);
        assert!(v.helm_on && v.anchor_on && !v.gun_on);
    }

    #[test]
    fn the_crew_wire_matches_the_server() {
        // ShipCrewTests.cpp: EncodeGun(1, kNoMount) and EncodeCargo's layout.
        assert_eq!(
            decode_gun(&[OP_GUN, 1, 0, 0, 0, 0, 0, 0, 0, NO_MOUNT]),
            Some((1, None))
        );
        assert_eq!(
            decode_gun(&[OP_GUN, 1, 0, 0, 0, 0, 0, 0, 0, 2]),
            Some((1, Some(2)))
        );
        let mut b = vec![OP_CARGO_LIST, 16, 1];
        b.extend_from_slice(&2589u32.to_le_bytes());
        b.extend_from_slice(&20u32.to_le_bytes());
        b.push(11);
        b.extend_from_slice(b"Linen Cloth");
        b.push(1);
        b.extend_from_slice(&2592u32.to_le_bytes());
        b.extend_from_slice(&7u32.to_le_bytes());
        b.push(0);
        let c = decode_cargo(&b).expect("decodes");
        assert_eq!(c.slots, 16);
        assert_eq!(c.hold[0].name, "Linen Cloth");
        assert_eq!((c.bags[0].entry, c.bags[0].count), (2592, 7));
        b.push(0);
        assert!(decode_cargo(&b).is_none());
        assert_eq!(
            encode_stow(2589, 5),
            vec![OP_STOW, 0x1D, 0x0A, 0, 0, 5, 0, 0, 0]
        );
    }

    #[test]
    fn the_hold_lists_take_and_stow_and_greys_what_will_not_fit() {
        let line = |e, n, s: &str| CargoLine {
            entry: e,
            count: n,
            name: s.into(),
        };
        let c = Cargo {
            slots: 1,
            hold: vec![line(2589, 20, "Linen Cloth")],
            bags: vec![line(2589, 3, "Linen Cloth"), line(2592, 1, "")],
        };
        let rows = cargo_rows(&c);
        assert_eq!(rows[0].0, "In the hold (1 of 1 slots)");
        assert_eq!(
            rows[1],
            ("Linen Cloth x20".to_string(), Some(("Take", true)))
        );
        assert_eq!(rows[3].1, Some(("Stow", true)), "tops up its own stack");
        assert_eq!(rows[4], ("Item 2592 x1".to_string(), Some(("Stow", false))));
    }

    #[test]
    fn the_frame_follows_the_helm_then_our_deck_then_the_hand() {
        let mut s = Ships::default();
        let mut mine = decode_pose(&cpp_pose()).unwrap();
        mine.guid = 1;
        let mut theirs = mine.clone();
        theirs.guid = 2;
        theirs.flags = 0;
        for p in [mine, theirs] {
            s.ships.insert(
                p.guid,
                Ship {
                    pose: p,
                    at: Instant::now(),
                },
            );
        }
        assert!(s.subject(None).is_none());
        assert!(
            s.subject(Some(2)).is_none(),
            "someone else's deck: no frame"
        );
        assert_eq!(s.subject(Some(1)).map(|x| x.pose.guid), Some(1));
        s.shown = Some(true);
        assert_eq!(s.subject(None).map(|x| x.pose.guid), Some(1));
        s.helm = Some(2);
        assert_eq!(s.subject(None).map(|x| x.pose.guid), Some(2));
    }
}
