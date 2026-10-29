//! The downed state (the server's `fusion/Downed.h`, `FusionDowned.cpp`): a hit that would kill
//! leaves the player down at 1 health. Down, the eye drops to the ground and the body crawls
//! (forced prone), the gun is lowered and nothing fires, casts or throws, unless the class carries
//! Last Stand: then its pistol comes up and fires, as in MW2. A bleed-out timer runs; when it ends
//! the player really dies (ghost, corpse run). Only a resurrection spell or a revival kit (First
//! Aid's Field Revival Kit, Engineering's Gnomish Revival Coil) brings a downed player back up:
//! **F** uses a kit on yourself while down, or on a downed ally under the crosshair.
//!
//! The server owns all of it and tells the client on fusion channel 5; the client never predicts
//! it. Others see a downed player lying down (the server marks the body as feign death does).
//!
//! The wire is the C++ encoders' byte for byte, little-endian, first byte the op (see [`wire`]);
//! the tests use the C++ tests' bytes.

use benilla_app::Objects;
use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use benilla_ui::script::UiScript;
use bevy::picking::Pickable;
use bevy::platform::collections::HashSet;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::aim::Aim;
use crate::input::{Control, Mw2Input};
use crate::mode::FpsState;
use crate::{cac, lua};

pub use wire::*;

/// The downed messages' wire (`Fusion::Downed` in `Downed.h` / `Downed.cpp`).
pub mod wire {
    /// The fusion channel of the downed state (`kChannelDowned`).
    pub const CHANNEL_DOWNED: u8 = 5;
    // Server to client.
    pub const OP_DOWNED: u8 = 1;
    pub const OP_UP: u8 = 2;
    pub const OP_DIED: u8 = 3;
    pub const OP_KIT: u8 = 4;
    pub const OP_NEAR: u8 = 5;
    // Client to server.
    pub const OP_USE: u8 = 10;

    /// Last Stand: the pistol is up.
    pub const DOWNED_ARMED: u8 = 0x01;
    /// Final Stand: back up by itself.
    pub const DOWNED_FINAL_STAND: u8 = 0x02;
    /// A revival kit is in the bags.
    pub const DOWNED_HAS_KIT: u8 = 0x04;

    /// The package's item entries (`Downed::kFirstItem`..`kLastItem`): the kits and manuals.
    pub const FIRST_ITEM: u32 = 93_020;
    pub const LAST_ITEM: u32 = 93_049;
    pub const FIELD_REVIVAL_KIT: u32 = 93_020;
    pub const REVIVAL_COIL: u32 = 93_021;

    const DOWNED_BYTES: usize = 12;
    const UP_BYTES: usize = 11;
    const DIED_BYTES: usize = 2;
    const KIT_BYTES: usize = 15;
    const NEAR_BYTES: usize = 10;

    /// Op 1: you are down.
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct DownedMsg {
        pub flags: u8,
        pub remaining_ms: u32,
        pub total_ms: u32,
        /// The gun to raise when armed (Last Stand), else 0.
        pub pistol: u16,
    }

    /// Op 2: why you are back up (`UpReason`).
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub enum UpReason {
        #[default]
        Spell,
        Kit,
        FinalStand,
        GameMaster,
    }

    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct UpMsg {
        pub reason: UpReason,
        pub health_pct: u8,
        pub by: u64,
    }

    /// Op 3: why a downed player died (`DeathCause`).
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum DeathCause {
        BledOut,
        Finished,
    }

    /// Op 4: a kit at work, by you or on you (`KitState`).
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub enum KitState {
        #[default]
        Stopped,
        Using,
        Done,
        Refused,
    }

    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct KitMsg {
        pub state: KitState,
        pub self_use: bool,
        /// To the user: the target (0 on yourself); to the target: the one working.
        pub other: u64,
        pub duration_ms: u32,
    }

    fn u16_at(b: &[u8], at: usize) -> u16 {
        u16::from_le_bytes([b[at], b[at + 1]])
    }
    fn u32_at(b: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
    }
    fn u64_at(b: &[u8], at: usize) -> u64 {
        u64::from_le_bytes(b[at..at + 8].try_into().unwrap())
    }

    pub fn encode_downed(m: &DownedMsg) -> Vec<u8> {
        let mut o = Vec::with_capacity(DOWNED_BYTES);
        o.push(OP_DOWNED);
        o.push(m.flags);
        o.extend_from_slice(&m.remaining_ms.to_le_bytes());
        o.extend_from_slice(&m.total_ms.to_le_bytes());
        o.extend_from_slice(&m.pistol.to_le_bytes());
        o
    }

    pub fn decode_downed(b: &[u8]) -> Option<DownedMsg> {
        (b.len() == DOWNED_BYTES && b[0] == OP_DOWNED).then(|| DownedMsg {
            flags: b[1],
            remaining_ms: u32_at(b, 2),
            total_ms: u32_at(b, 6),
            pistol: u16_at(b, 10),
        })
    }

    pub fn encode_up(m: &UpMsg) -> Vec<u8> {
        let mut o = Vec::with_capacity(UP_BYTES);
        o.push(OP_UP);
        o.push(match m.reason {
            UpReason::Spell => 1,
            UpReason::Kit => 2,
            UpReason::FinalStand => 3,
            UpReason::GameMaster => 4,
        });
        o.push(m.health_pct);
        o.extend_from_slice(&m.by.to_le_bytes());
        o
    }

    pub fn decode_up(b: &[u8]) -> Option<UpMsg> {
        if b.len() != UP_BYTES || b[0] != OP_UP {
            return None;
        }
        let reason = match b[1] {
            2 => UpReason::Kit,
            3 => UpReason::FinalStand,
            4 => UpReason::GameMaster,
            _ => UpReason::Spell,
        };
        Some(UpMsg {
            reason,
            health_pct: b[2],
            by: u64_at(b, 3),
        })
    }

    pub fn encode_died(c: DeathCause) -> Vec<u8> {
        vec![
            OP_DIED,
            match c {
                DeathCause::BledOut => 1,
                DeathCause::Finished => 2,
            },
        ]
    }

    pub fn decode_died(b: &[u8]) -> Option<DeathCause> {
        (b.len() == DIED_BYTES && b[0] == OP_DIED).then(|| {
            if b[1] == 1 {
                DeathCause::BledOut
            } else {
                DeathCause::Finished
            }
        })
    }

    pub fn encode_kit(m: &KitMsg) -> Vec<u8> {
        let mut o = Vec::with_capacity(KIT_BYTES);
        o.push(OP_KIT);
        o.push(match m.state {
            KitState::Stopped => 0,
            KitState::Using => 1,
            KitState::Done => 2,
            KitState::Refused => 3,
        });
        o.push(u8::from(m.self_use));
        o.extend_from_slice(&m.other.to_le_bytes());
        o.extend_from_slice(&m.duration_ms.to_le_bytes());
        o
    }

    pub fn decode_kit(b: &[u8]) -> Option<KitMsg> {
        if b.len() != KIT_BYTES || b[0] != OP_KIT {
            return None;
        }
        let state = match b[1] {
            1 => KitState::Using,
            2 => KitState::Done,
            3 => KitState::Refused,
            _ => KitState::Stopped,
        };
        Some(KitMsg {
            state,
            self_use: b[2] != 0,
            other: u64_at(b, 3),
            duration_ms: u32_at(b, 11),
        })
    }

    pub fn encode_near(player: u64, downed: bool) -> Vec<u8> {
        let mut o = Vec::with_capacity(NEAR_BYTES);
        o.push(OP_NEAR);
        o.extend_from_slice(&player.to_le_bytes());
        o.push(u8::from(downed));
        o
    }

    pub fn decode_near(b: &[u8]) -> Option<(u64, bool)> {
        (b.len() == NEAR_BYTES && b[0] == OP_NEAR).then(|| (u64_at(b, 1), b[9] != 0))
    }

    /// Op 10: use a kit (or a manual): item 0 for any kit in the bags, target 0 for the server's
    /// pick (a downed player selected and in reach, else yourself).
    pub fn encode_use(item: u64, target: u64) -> Vec<u8> {
        let mut o = Vec::with_capacity(17);
        o.push(OP_USE);
        o.extend_from_slice(&item.to_le_bytes());
        o.extend_from_slice(&target.to_le_bytes());
        o
    }
}

/// Yards within which F works a kit on a downed ally (the server's `kKitReach`, and a little
/// for the camera's place).
pub const KIT_REACH: f32 = 5.5;

/// A kit at work, from the server's op 4.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KitWork {
    /// Reviving yourself.
    pub self_use: bool,
    /// Someone else works on you.
    pub on_me: bool,
    pub other: u64,
    pub started: f32,
    pub secs: f32,
}

/// The downed state as the server last told it.
#[derive(Resource, Debug, Default)]
pub struct Downed {
    pub active: bool,
    pub armed: bool,
    pub final_stand: bool,
    /// The server's flag at going down, kept fresh by the bags' poll.
    pub has_kit: bool,
    /// Last Stand's pistol (a gun id), 0 unarmed.
    pub pistol: u16,
    /// Local clock: when it began and when the bleed-out comes.
    pub since: f32,
    pub ends_at: f32,
    pub total: f32,
    pub kit: Option<KitWork>,
    /// Players near that are down (op 5).
    pub near: HashSet<u64>,
    /// A line after the state ends ("You bled out"), and until when.
    pub banner: Option<(String, f32)>,
    /// The gun that was up before Last Stand's pistol, put back on getting up.
    prior: Option<String>,
    raised: bool,
    poll_at: f32,
}

impl Downed {
    /// Down without Last Stand: the gun is lowered and nothing fires.
    pub fn lowered(&self) -> bool {
        self.active && !self.armed
    }

    /// Seconds left to the bleed-out.
    pub fn remaining(&self, now: f32) -> f32 {
        (self.ends_at - now).max(0.0)
    }

    /// Apply a server message (`now` the local clock). Returns a chat line to show, if any.
    pub fn apply(&mut self, body: &[u8], now: f32) -> Option<String> {
        match body.first() {
            Some(&OP_DOWNED) => {
                let m = decode_downed(body)?;
                self.active = true;
                self.armed = m.flags & DOWNED_ARMED != 0;
                self.final_stand = m.flags & DOWNED_FINAL_STAND != 0;
                self.has_kit = m.flags & DOWNED_HAS_KIT != 0;
                self.pistol = if self.armed { m.pistol } else { 0 };
                self.since = now;
                self.total = m.total_ms as f32 / 1000.0;
                self.ends_at = now + m.remaining_ms as f32 / 1000.0;
                self.banner = None;
                None
            }
            Some(&OP_UP) => {
                let m = decode_up(body)?;
                self.active = false;
                self.kit = None;
                self.banner = Some(("Back on your feet".into(), now + 2.5));
                Some(format!(
                    "benilla-mw2: back up ({:?}) at {}% health",
                    m.reason, m.health_pct
                ))
            }
            Some(&OP_DIED) => {
                let cause = decode_died(body)?;
                self.active = false;
                self.kit = None;
                let line = match cause {
                    DeathCause::BledOut => "You bled out",
                    DeathCause::Finished => "You died",
                };
                self.banner = Some((line.into(), now + 4.0));
                None
            }
            Some(&OP_KIT) => {
                let m = decode_kit(body)?;
                self.kit = (m.state == KitState::Using).then(|| KitWork {
                    self_use: m.self_use,
                    on_me: !m.self_use && self.active,
                    other: m.other,
                    started: now,
                    secs: m.duration_ms as f32 / 1000.0,
                });
                None
            }
            Some(&OP_NEAR) => {
                let (guid, down) = decode_near(body)?;
                if down {
                    self.near.insert(guid);
                } else {
                    self.near.remove(&guid);
                }
                None
            }
            _ => None,
        }
    }
}

/// What F does: work a kit on yourself (down), on the downed ally under the crosshair within
/// reach (`target`: guid and distance, and whether it reads down), or nothing.
pub fn kit_request(self_down: bool, target: Option<(u64, f32, bool)>) -> Option<u64> {
    if self_down {
        return Some(0);
    }
    match target {
        Some((guid, distance, down)) if down && distance <= KIT_REACH => Some(guid),
        _ => None,
    }
}

/// The vignette's strength: a slow pulse, quicker and stronger as the bleed-out nears.
pub fn pulse(t: f32, left_share: f32) -> f32 {
    let urgency = 1.0 - left_share.clamp(0.0, 1.0);
    let rate = 0.8 + 1.6 * urgency;
    let wave = 0.5 + 0.5 * (t * rate * std::f32::consts::TAU).sin();
    (0.35 + 0.2 * urgency + 0.25 * wave).min(0.85)
}

#[derive(Component)]
struct Root;
/// One ring of the vignette: rings of growing width stack, so the red thickens toward the edges
/// rather than standing as a hard frame.
#[derive(Component)]
struct Edge(usize);

/// How many rings, and the widest one's width (px).
/// The red edge's texture: a soft gradient, reddest at the screen's edge, clear by a fifth of the
/// way in (a stack of bordered rings drew visible bands).
const EDGE_TEX: u32 = 128;
const EDGE_REACH: f32 = 0.2;

/// The edge gradient's alpha at `(u, v)` in 0..1: 1 at the border, 0 at `EDGE_REACH` in.
pub fn edge_alpha(u: f32, v: f32) -> f32 {
    let d = u.min(1.0 - u).min(v).min(1.0 - v).max(0.0);
    let t = (1.0 - d / EDGE_REACH).clamp(0.0, 1.0);
    t * t
}

pub(crate) fn edge_image() -> Image {
    let n = EDGE_TEX as usize;
    let mut rgba = vec![0u8; n * n * 4];
    for y in 0..n {
        for x in 0..n {
            let a = edge_alpha((x as f32 + 0.5) / n as f32, (y as f32 + 0.5) / n as f32);
            let i = (y * n + x) * 4;
            rgba[i..i + 4].copy_from_slice(&[150, 0, 0, (a * 255.0).round() as u8]);
        }
    }
    benilla_assets::sprite_image(EDGE_TEX, EDGE_TEX, rgba)
}
#[derive(Component, Clone, Copy, PartialEq, Eq)]
enum Line {
    Title,
    Status,
    Hint,
    Kit,
}
#[derive(Component)]
struct BleedFill;
#[derive(Component)]
struct KitFill;

use crate::wow_ui::FONT;
const RED: Color = Color::srgb(0.9, 0.12, 0.1);
const WHITE: Color = Color::srgb(0.95, 0.95, 0.95);
const GOLD: Color = Color::srgb(1.0, 0.82, 0.0);

pub struct DownedPlugin;

impl Plugin for DownedPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Downed>()
            .add_systems(Startup, spawn)
            .add_systems(
                Update,
                (read_channel, hold_controls, raise_pistol, use_kit)
                    .chain()
                    .in_set(Mw2Set::Mode)
                    .after(cac::CacSet),
            )
            .add_systems(Update, draw.in_set(Mw2Set::Hud));
    }
}

fn read_channel(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut downed: ResMut<Downed>,
    script: Option<NonSend<UiScript>>,
) {
    let now = time.elapsed_secs();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_DOWNED {
            continue;
        }
        if let Some(line) = downed.apply(&m.body, now) {
            info!("{line}");
        }
    }
    // The bags, twice a second: a kit to use (the hint), made or traded while down.
    if now >= downed.poll_at
        && let Some(script) = &script
    {
        downed.poll_at = now + 0.5;
        // The fork's Lua has the bag calls but no GetItemCount: scan the bags' links.
        let count: Option<u32> = lua::eval(
            script,
            &format!(
                "local n = 0 for b = 0, 4 do for s = 1, GetContainerNumSlots(b) do                  local l = GetContainerItemLink(b, s)                  if l and (string.find(l, 'item:{FIELD_REVIVAL_KIT}:')                  or string.find(l, 'item:{REVIVAL_COIL}:')) then n = n + 1 end end end return n"
            ),
        );
        if let Some(n) = count {
            downed.has_kit = n > 0;
        }
    }
}

/// Down: no grenades, knife, killstreak or gun swap; lowered, no trigger or sights either. The
/// server refuses them all; this keeps the client from playing them.
fn hold_controls(downed: Res<Downed>, mut input: ResMut<Mw2Input>) {
    if !downed.active {
        return;
    }
    let input = &mut *input;
    for c in [
        &mut input.lethal,
        &mut input.tactical,
        &mut input.killstreak,
        &mut input.crouch,
        &mut input.prone,
    ] {
        *c = Control::default();
    }
    input.hand = None;
    input.wheel_swap = false;
    input.sprint = false;
    if downed.lowered() {
        input.fire = Control::default();
        input.ads = Control::default();
        input.reload = Control::default();
    }
}

/// Last Stand: the pistol up while down, the gun before it back on getting up.
fn raise_pistol(
    mut downed: ResMut<Downed>,
    catalog: Res<crate::catalog::Catalog>,
    mut fusion: ResMut<crate::fusion::Fusion>,
) {
    let want = downed.active && downed.armed && downed.pistol != 0;
    if want {
        let Some(name) = catalog.gun(downed.pistol).map(|g| g.name.clone()) else {
            return;
        };
        if !downed.raised {
            downed.raised = true;
            downed.prior = fusion.selected.clone();
        }
        if fusion.selected.as_deref() != Some(name.as_str()) {
            fusion.selected = Some(name);
        }
    } else if downed.raised {
        downed.raised = false;
        if let Some(prior) = downed.prior.take() {
            fusion.selected = Some(prior);
        }
    }
}

/// F: a kit on yourself while down, or on the downed ally under the crosshair.
fn use_kit(
    state: Res<FpsState>,
    input: Res<Mw2Input>,
    aim: Res<Aim>,
    downed: Res<Downed>,
    objects: Objects,
    mut out: MessageWriter<SendFusionChannel>,
) {
    if !state.owns_input || !input.interact.pressed || downed.kit.is_some() {
        return;
    }
    let target = aim.target.as_ref().filter(|a| a.player).map(|a| {
        let reads_down = downed.near.contains(&a.guid)
            || objects
                .object(a.guid)
                .is_some_and(|f| f.unit_dynflag_dead() && f.unit_health().unwrap_or(0) > 0);
        (a.guid, a.distance, reads_down)
    });
    if let Some(guid) = kit_request(downed.active, target) {
        out.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_DOWNED,
            body: encode_use(0, guid),
        }));
    }
}

fn spawn(mut commands: Commands, assets: Res<AssetServer>, mut images: ResMut<Assets<Image>>) {
    let edge_tex = images.add(edge_image());
    let font: Handle<Font> = assets.load(FONT);
    let label = |line: Line, size: f32, color: Color| {
        (
            line,
            Text::new(""),
            TextFont::from_font_size(size).with_font(font.clone()),
            TextColor(color),
            TextShadow {
                offset: Vec2::splat(1.5),
                color: Color::srgba(0.0, 0.0, 0.0, 0.85),
            },
            Pickable::IGNORE,
        )
    };
    let bar = |width: f32, fill: Color| {
        (
            Node {
                width: px(width),
                height: px(8),
                margin: UiRect::vertical(px(6)),
                ..default()
            },
            BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.6)),
            Pickable::IGNORE,
            children![(
                Node {
                    width: percent(100),
                    height: percent(100),
                    ..default()
                },
                BackgroundColor(fill),
                Pickable::IGNORE,
            )],
        )
    };
    commands
        .spawn((
            Name::new("mw2 downed"),
            Root,
            Node {
                position_type: PositionType::Absolute,
                width: percent(100),
                height: percent(100),
                flex_direction: FlexDirection::Column,
                align_items: AlignItems::Center,
                justify_content: JustifyContent::FlexEnd,
                padding: UiRect::bottom(percent(22)),
                ..default()
            },
            GlobalZIndex(i32::MAX - 2),
            Visibility::Hidden,
            Pickable::IGNORE,
        ))
        .with_children(|root| {
            // The red edges, pulsing.
            root.spawn((
                Edge(0),
                Node {
                    position_type: PositionType::Absolute,
                    left: px(0),
                    top: px(0),
                    width: percent(100),
                    height: percent(100),
                    ..default()
                },
                BorderColor::all(Color::NONE),
                BackgroundColor(Color::NONE),
                Pickable::IGNORE,
            ));
            root.spawn((
                Edge(1),
                Node {
                    position_type: PositionType::Absolute,
                    left: px(0),
                    top: px(0),
                    width: percent(100),
                    height: percent(100),
                    ..default()
                },
                ImageNode {
                    image: edge_tex.clone(),
                    color: Color::WHITE.with_alpha(0.0),
                    image_mode: NodeImageMode::Stretch,
                    ..default()
                },
                BorderColor::all(Color::NONE),
                BackgroundColor(Color::NONE),
                Pickable::IGNORE,
            ));
            root.spawn(label(Line::Title, 34.0, RED));
            root.spawn(label(Line::Status, 20.0, WHITE));
            root.spawn((BleedFill, bar(320.0, RED)));
            root.spawn(label(Line::Hint, 18.0, GOLD));
            root.spawn(label(Line::Kit, 18.0, WHITE));
            root.spawn((KitFill, bar(240.0, Color::srgb(0.35, 0.85, 0.4))));
        });
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn draw(
    time: Res<Time>,
    state: Res<FpsState>,
    downed: Res<Downed>,
    aim: Res<Aim>,
    objects: Objects,
    keymap: Option<Res<crate::keybinds::Keymap>>,
    mut root: Query<&mut Visibility, With<Root>>,
    mut edge: Query<(&Edge, &mut BackgroundColor, Option<&mut ImageNode>)>,
    mut lines: Query<(&Line, &mut Text)>,
    bleed: Query<(&Children, &mut Visibility), (With<BleedFill>, Without<Root>, Without<KitFill>)>,
    kit_bar: Query<(&Children, &mut Visibility), (With<KitFill>, Without<Root>, Without<BleedFill>)>,
    mut fills: Query<&mut Node>,
) {
    let now = time.elapsed_secs();
    let banner = downed.banner.as_ref().filter(|(_, until)| now < *until);
    // A downed ally under the crosshair, in reach: the hint to use a kit on them.
    let ally_down = !downed.active
        && aim.target.as_ref().is_some_and(|a| {
            a.player
                && a.distance <= KIT_REACH
                && (downed.near.contains(&a.guid)
                    || objects
                        .object(a.guid)
                        .is_some_and(|f| f.unit_dynflag_dead() && f.unit_health().unwrap_or(0) > 0))
        });
    let show = state.active && (downed.active || downed.kit.is_some() || banner.is_some() || ally_down);
    for mut v in &mut root {
        let want = if show {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *v != want {
            *v = want;
        }
    }
    if !show {
        return;
    }
    let left = downed.remaining(now);
    let share = if downed.total > 0.0 {
        left / downed.total
    } else {
        0.0
    };
    let a = if downed.active { pulse(now, share) } else { 0.0 };
    // The gradient reddens the edges; the first layer only tints the whole view faintly.
    for (ring, mut bg, img) in &mut edge {
        if ring.0 == 0 {
            bg.0 = Color::srgba(0.5, 0.0, 0.0, a * 0.12);
        } else if let Some(mut img) = img {
            img.color = Color::WHITE.with_alpha(a.min(1.0));
        }
    }
    let interact = keymap.as_deref().map_or_else(
        || "F".to_string(),
        |k| k.label(crate::keybinds::Action::Interact),
    );
    let kit_left = downed
        .kit
        .map(|k| ((k.started + k.secs - now).max(0.0), k));
    for (line, mut text) in &mut lines {
        let s = match line {
            Line::Title if downed.active => "DOWNED".to_string(),
            Line::Title => banner.map(|(b, _)| b.clone()).unwrap_or_default(),
            Line::Status if downed.active && downed.final_stand => format!(
                "Final Stand: hold on, back up in {:.0}",
                (FINAL_STAND_SECS - (now - downed.since)).max(0.0).ceil()
            ),
            Line::Status if downed.active => {
                format!("Downed \u{2014} waiting for a revive  {:.0}", left.ceil())
            }
            Line::Status => String::new(),
            Line::Hint if downed.active && downed.has_kit && downed.kit.is_none() => {
                format!("{interact}: use your revival kit (hold still)")
            }
            Line::Hint if downed.active && downed.armed => "Last Stand: your pistol is up".to_string(),
            Line::Hint if ally_down && downed.kit.is_none() => {
                format!("{interact}: revive them with a revival kit")
            }
            Line::Hint => String::new(),
            Line::Kit => match kit_left {
                Some((secs, k)) if k.self_use => format!("Reviving yourself\u{2026} {secs:.1}"),
                Some((secs, k)) if k.on_me => format!("An ally is reviving you\u{2026} {secs:.1}"),
                Some((secs, _)) => format!("Reviving\u{2026} {secs:.1}  (hold still)"),
                None => String::new(),
            },
        };
        if text.0 != s {
            text.0 = s;
        }
    }
    fill_bar(bleed, downed.active.then_some(share), &mut fills);
    fill_bar(
        kit_bar,
        kit_left.map(|(secs, k)| 1.0 - secs / k.secs.max(0.01)),
        &mut fills,
    );
}

/// Final Stand's wait (the server's `kFinalStandSeconds`).
const FINAL_STAND_SECS: f32 = 10.0;

fn fill_bar<F: bevy::ecs::query::QueryFilter>(
    mut bars: Query<(&Children, &mut Visibility), F>,
    frac: Option<f32>,
    fills: &mut Query<&mut Node>,
) {
    for (children, mut vis) in &mut bars {
        let want = if frac.is_some() {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *vis != want {
            *vis = want;
        }
        if let Some(f) = frac
            && let Some(&child) = children.first()
            && let Ok(mut node) = fills.get_mut(child)
        {
            node.width = percent(f.clamp(0.0, 1.0) * 100.0);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The C++ tests' bytes (`DownedTests.cpp`, downed_wire_round_trips).
    #[test]
    fn the_wire_matches_the_server() {
        let d = DownedMsg {
            flags: DOWNED_ARMED | DOWNED_HAS_KIT,
            remaining_ms: 27500,
            total_ms: 28000,
            pistol: 30,
        };
        let b = encode_downed(&d);
        assert_eq!(b, vec![OP_DOWNED, 0x05, 0x6C, 0x6B, 0, 0, 0x60, 0x6D, 0, 0, 30, 0]);
        assert_eq!(decode_downed(&b), Some(d));
        assert_eq!(decode_downed(&[OP_DOWNED, 1, 2]), None);

        let u = UpMsg {
            reason: UpReason::Kit,
            health_pct: 25,
            by: 0x0102,
        };
        let b = encode_up(&u);
        assert_eq!(b, vec![OP_UP, 2, 25, 0x02, 0x01, 0, 0, 0, 0, 0, 0]);
        assert_eq!(decode_up(&b), Some(u));

        let b = encode_died(DeathCause::Finished);
        assert_eq!(b, vec![OP_DIED, 2]);
        assert_eq!(decode_died(&b), Some(DeathCause::Finished));

        let k = KitMsg {
            state: KitState::Using,
            self_use: false,
            other: 9,
            duration_ms: 3000,
        };
        let b = encode_kit(&k);
        assert_eq!(
            b,
            vec![OP_KIT, 1, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0xB8, 0x0B, 0, 0]
        );
        assert_eq!(decode_kit(&b), Some(k));

        let b = encode_near(0xAB, true);
        assert_eq!(b, vec![OP_NEAR, 0xAB, 0, 0, 0, 0, 0, 0, 0, 1]);
        assert_eq!(decode_near(&b), Some((0xAB, true)));

        assert_eq!(
            encode_use(0x11, 0x22),
            vec![OP_USE, 0x11, 0, 0, 0, 0, 0, 0, 0, 0x22, 0, 0, 0, 0, 0, 0, 0]
        );
        assert_eq!(CHANNEL_DOWNED, 5);
    }

    #[test]
    fn going_down_and_getting_up() {
        let mut d = Downed::default();
        let down = encode_downed(&DownedMsg {
            flags: DOWNED_ARMED,
            remaining_ms: 20_000,
            total_ms: 28_000,
            pistol: 30,
        });
        d.apply(&down, 100.0);
        assert!(d.active && d.armed && !d.lowered());
        assert_eq!(d.pistol, 30);
        assert!((d.remaining(110.0) - 10.0).abs() < 1e-4);
        assert_eq!(d.remaining(200.0), 0.0);
        // A kit on you, then up.
        d.apply(
            &encode_kit(&KitMsg {
                state: KitState::Using,
                self_use: false,
                other: 7,
                duration_ms: 3000,
            }),
            101.0,
        );
        assert!(d.kit.is_some_and(|k| k.on_me && k.other == 7));
        d.apply(
            &encode_up(&UpMsg {
                reason: UpReason::Kit,
                health_pct: 25,
                by: 7,
            }),
            104.0,
        );
        assert!(!d.active && d.kit.is_none() && d.banner.is_some());
        // Unarmed: lowered; then bled out.
        d.apply(
            &encode_downed(&DownedMsg {
                flags: 0,
                remaining_ms: 20_000,
                total_ms: 20_000,
                pistol: 30,
            }),
            200.0,
        );
        assert!(d.lowered() && d.pistol == 0);
        d.apply(&encode_died(DeathCause::BledOut), 220.0);
        assert!(!d.active);
        assert_eq!(d.banner.as_ref().map(|b| b.0.as_str()), Some("You bled out"));
        // Finished while down: a plain death line.
        d.apply(&encode_died(DeathCause::Finished), 230.0);
        assert_eq!(d.banner.as_ref().map(|b| b.0.as_str()), Some("You died"));
        // Players near.
        d.apply(&encode_near(5, true), 0.0);
        assert!(d.near.contains(&5));
        d.apply(&encode_near(5, false), 0.0);
        assert!(d.near.is_empty());
    }

    #[test]
    fn f_works_a_kit_on_yourself_or_a_downed_ally_in_reach() {
        assert_eq!(kit_request(true, None), Some(0));
        assert_eq!(kit_request(true, Some((9, 3.0, true))), Some(0)); // down: yourself only
        assert_eq!(kit_request(false, Some((9, 3.0, true))), Some(9));
        assert_eq!(kit_request(false, Some((9, 3.0, false))), None); // standing
        assert_eq!(kit_request(false, Some((9, KIT_REACH + 1.0, true))), None); // too far
        assert_eq!(kit_request(false, None), None);
    }

    #[test]
    fn the_vignette_pulses_harder_as_time_runs_out() {
        let calm = (0..100).map(|i| pulse(i as f32 * 0.05, 1.0)).fold(0.0, f32::max);
        let late = (0..100).map(|i| pulse(i as f32 * 0.05, 0.0)).fold(0.0, f32::max);
        assert!(late > calm);
        assert!(late <= 0.85);
        assert!((0..100).all(|i| pulse(i as f32 * 0.05, 0.5) >= 0.35));
    }
}

#[cfg(test)]
mod edge_tests {
    use super::edge_alpha;

    #[test]
    fn the_red_edge_fades_smoothly_to_clear() {
        assert!((edge_alpha(0.0, 0.5) - 1.0).abs() < 1e-6);
        assert_eq!(edge_alpha(0.5, 0.5), 0.0);
        assert_eq!(edge_alpha(0.25, 0.5), 0.0);
        // Monotone toward the edge, no bands.
        let mut last = 0.0;
        for i in (0..=20).rev() {
            let a = edge_alpha(i as f32 * 0.01, 0.5);
            assert!(a >= last);
            last = a;
        }
    }
}
