//! Building, after Valheim and Enshrouded: B opens the Construction window (crate::build::
//! homestead), where a piece (or its Build button) puts the gun away and enters build mode; a
//! small tooltip-style hint panel shows the piece in hand and the keys while building, B opens
//! the window again for another piece and Esc leaves; [ and ] (or the build menu) pick a piece, the wheel sets how far out it goes, R turns it, the left button
//! puts it down where the ghost shows (snapped to the pieces about it), the middle button takes
//! one of yours back down. Nothing goes up through a player: a ghost that would shut the builder
//! inside is red (and the server refuses one that would shut anyone in). The server holds every
//! piece (`fusion/Build.h` in azeroth-warfare-server): it checks the placement, keeps what holds
//! each piece up, takes the materials (Simple Wood, Rough Stone, Linen Cloth), stores the pieces
//! and streams them to the players near. Here they are drawn with WoW's own textures and light,
//! and are solid: you walk on floors, stairs and roofs, walls stop you and your bullets.
//!
//! In build mode every piece shows how well it is held up, as Valheim does: blue on the ground,
//! through green and yellow, to red at the edge of falling.
//!
//! F works a door (or opens a portal's list) in build mode or out; in build mode, F on a door or
//! portal of yours opens its lock dialog instead (crate::build::locks).

pub mod furniture;
pub mod homestead;
pub mod locks;
pub mod look;
pub mod menu;
pub mod models;
pub mod pieces;
pub mod plots;
pub mod render;
pub mod turret;
pub mod wire;

use avian3d::prelude::*;
use benilla_app::fusion::{
    CHANNEL_BUILD, CHANNEL_WORLD, ChannelMessage, FusionChannelReceived, SendFusionChannel,
};
use benilla_world::view::WorldCamera;
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::fusion::to_wow;
use crate::input::Mw2Input;
use crate::keybinds::{Action as KeyAction, Keymap};
use crate::mode::FpsState;
use crate::tracer::from_wow;
use pieces::{Choice, PieceType, Placed, blueprint_parts, choice, choices, def, snap};
use wire::{ServerMsg, WirePiece};

/// How far the hammer reaches to take a piece down or reinforce it (yards); the server allows a
/// little more.
pub const REACH: f32 = 9.0;
/// How far out a piece goes, as the wheel sets it (yards along the crosshair; the server's
/// `Build::kPlaceReach` is the most): from the nearest to the farthest, a notch a step.
pub const PLACE_MIN: f32 = 2.0;
pub const PLACE_MAX: f32 = 25.0;
pub const PLACE_DEFAULT: f32 = 6.0;
pub const PLACE_STEP: f32 = 1.0;
/// Why a ghost the builder stands in is red.
pub const TRAPS_WHY: &str = "You're standing in its way: step back, or wheel it farther out";

/// The placing distance after `notches` of the wheel (up farther), held to its range.
pub fn wheel_distance(current: f32, notches: f32) -> f32 {
    (current + notches * PLACE_STEP).clamp(PLACE_MIN, PLACE_MAX)
}

/// The distance the ghost shows on its way to `target`: most of the gap closed in a tenth of a
/// second, so a notch glides rather than jumps.
pub fn ease_distance(shown: f32, target: f32, dt: f32) -> f32 {
    if (target - shown).abs() < 0.01 {
        return target;
    }
    shown + (target - shown) * (1.0 - (-dt * 18.0).exp())
}
/// How near the aim a snap point must be to take the piece (yards).
pub const SNAP_RADIUS: f32 = 2.0;

/// A piece in the world.
#[derive(Debug, Clone)]
pub struct Piece {
    pub wire: WirePiece,
    pub entity: Option<Entity>,
    /// Its colours need making again (health, support, the light, build mode).
    pub dirty: bool,
    /// Its shape changed (a door opened): draw it again.
    pub respawn: bool,
}

impl Piece {
    pub fn placed(&self) -> Placed {
        Placed {
            kind: self.wire.kind,
            pos: self.wire.pos,
            yaw: self.wire.yaw,
        }
    }
}

/// Building's state.
#[derive(Resource, Default)]
pub struct Building {
    /// In build mode.
    pub on: bool,
    /// The piece on the bar.
    pub selected: usize,
    /// Quarter turns added to the facing.
    pub turns: i32,
    pub pieces: HashMap<u32, Piece>,
    /// The piece under the crosshair, and where the ghost stands.
    pub aimed: Option<u32>,
    pub ghost: Vec<(Placed, bool)>,
    /// A blueprint going up: its pieces still to send, and when the next goes (the server takes
    /// one every 150 ms).
    pub queue: std::collections::VecDeque<Placed>,
    pub next_send: f32,
    /// A placement sent and not yet answered (when it went): the next waits for the answer, as
    /// the server refuses placements closer than 150 ms apart.
    pub awaiting: Option<f32>,
    /// The placement in flight, and how often it was sent: one the server turned away for
    /// arriving too soon after the last (`Refused(Unknown)`) goes again.
    pub in_flight: Option<(Placed, u8)>,
    pub owned: u16,
    pub max: u16,
    pub free: bool,
    /// The last word from the server (a refusal), and until when it shows.
    pub toast: Option<(String, f32)>,
    /// A take-down or a reinforcement pressed once, waiting for its second press on the same
    /// piece: (what, piece id, until when).
    pub armed: Option<(Arm, u32, f32)>,
    /// The map the pieces are on: a new world clears them.
    pub world: Option<Entity>,
    /// Pieces just broken or fallen: they come apart in chunks.
    pub broken: Vec<Placed>,
    /// WoW sound files to play (placing, breaking, doors).
    pub sounds: Vec<&'static str>,
    /// Construction: each piece's recipe by type, as the server sent them (none from a server
    /// without the profession, when the pieces' own costs stand).
    pub recipes: HashMap<u8, wire::Recipe>,
    /// The kits made at Construction's trade window and not yet placed, by piece type (the
    /// professions channel, crate::professions), and whether the server keeps kits at all (one
    /// that never says builds from the bags as before).
    pub kits: HashMap<u8, u32>,
    pub kits_known: bool,
    /// The Construction tier (0 not learned), skill and the tier's cap.
    pub tier: u8,
    pub skill: u16,
    pub cap: u16,
    /// The plot underfoot (the homesteads), if any.
    pub plot: Option<wire::PlotInfo>,
    /// Counts the changes to the recipes, the skill and free building: the menu draws its tiles
    /// again when it moves.
    pub construction_version: u32,
    /// The reinforcement tiers the server offers (G on a piece of ours puts it up one).
    pub reinforcements: Vec<wire::ReinforceTier>,
    /// The builder's style (crate::build::look): every piece a style redraws goes up in it.
    pub style: u8,
    /// Why the ghost is red, when the plot underfoot says (over its edge, upkeep overdue) or
    /// the builder stands in its way.
    pub ghost_why: Option<&'static str>,
    /// How far out the piece goes (yards, the wheel's; 0 until build mode first opens, then kept
    /// for the session), and the distance the ghost shows on its way there.
    pub distance: f32,
    pub distance_shown: f32,
    /// What the lock and portal dialogs are to show (crate::build::locks drains it each frame).
    pub lock_events: Vec<locks::LockEvent>,
    /// The door or portal last used, for what a refusal says of its lock.
    pub last_lock_piece: Option<u32>,
    /// What the furniture dialogs are to show (crate::build::furniture drains it each frame).
    pub furniture_events: Vec<furniture::FurnitureEvent>,
    /// Furniture placement: R's 15-degree steps, and whether the grid and steps are off (free).
    pub fine_turns: i32,
    pub free_place: bool,
}

/// On a piece's box collider: the piece it belongs to.
#[derive(Component, Clone, Copy)]
pub struct PieceBox(pub u32);

/// Build mode reading its keys (probes that press them run before).
#[derive(SystemSet, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct BuildInput;

/// A piece's drawn root.
#[derive(Component)]
pub(crate) struct PieceRoot;

/// The ghost.
#[derive(Component)]
struct Ghost {
    kind: Option<PieceType>,
    valid: bool,
}

#[derive(Component)]
struct BuildToast;

/// A campfire's or a torch's flames.
#[derive(Component)]
pub(crate) struct Flame {
    pub phase: f32,
}

pub struct BuildPlugin;

impl Plugin for BuildPlugin {
    fn build(&self, app: &mut App) {
        // The Construction window and the plots marked in the world.
        app.add_plugins((
            homestead::HomesteadPlugin,
            plots::PlotMarkersPlugin,
            locks::LocksPlugin,
            furniture::FurniturePlugin,
        ));
        app.add_plugins(menu::BuildMenuPlugin)
            .init_resource::<Building>()
            .init_resource::<models::ModelCache>()
            .add_message::<turret::SentryShot>()
            .add_systems(Startup, (render::setup, spawn_ui))
            // Build mode takes its controls before Create-a-Class can read the wheel and the
            // number keys as a gun swap.
            .add_systems(
                Update,
                (toggle, take_controls)
                    .chain()
                    .in_set(Mw2Set::Mode)
                    .in_set(BuildInput)
                    .before(crate::cac::CacSet),
            )
            .add_systems(
                Update,
                (read_server, aim_and_act, use_doors)
                    .chain()
                    .after(Mw2Set::Mode)
                    .before(Mw2Set::Weapon),
            )
            .add_systems(
                Update,
                (
                    render::repeat_textures,
                    sync_pieces,
                    render::relight,
                    turret::work_turrets,
                    draw_ghost,
                    flicker,
                    break_apart,
                    fall_debris,
                    play_sounds,
                    spawn_hint,
                    draw_ui,
                )
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

/// Build mode is entered by Construction's Build Structure ability (Utility's bar), or from the
/// Construction window (a piece, or its Build button; B opens the Professions hub, never build
/// mode itself); leaving the layer leaves it, and so does Esc once the
/// build menu and any window are down (the first Esc closes them).
fn toggle(
    input: Res<Mw2Input>,
    mut state: ResMut<FpsState>,
    mut build: ResMut<Building>,
    mut systems: ResMut<crate::action_systems::ActionSystems>,
) {
    // Construction's Build Structure (Utility's bar): into build mode, its menu up.
    if std::mem::take(&mut systems.build_structure) && state.active && !build.on {
        build.on = true;
    }
    let want = build_mode_wanted(
        state.active,
        build.on,
        input.escape && !state.build_menu && !state.panel,
    );
    if build.on != want {
        build.on = want;
        for p in build.pieces.values_mut() {
            p.dirty = true;
        }
    }
    if state.building != want {
        state.building = want;
    }
}

/// Whether build mode is on after this frame's keys: Esc (with no window or menu up to close
/// first) leaves it, and it is never on with the layer off. No key enters it.
pub fn build_mode_wanted(active: bool, on: bool, escape: bool) -> bool {
    active && on && !escape
}

/// The controls building takes this frame, before the gun can see them.
#[derive(Resource, Default)]
struct BuildControls {
    place: bool,
    remove: bool,
    reinforce: bool,
    turn: bool,
    /// Sprint held: furniture goes down free (no grid, no 15-degree steps).
    free: bool,
    wheel: f32,
    /// [ and ]: the piece before or after.
    cycle: i32,
}

/// `MW2_PROBE_BUILD`'s steps this frame; true when it is time to place. Its value is
/// `<choice>@<secs>` or `<choice>:<style>@<secs>` (a style of crate::build::look, 0-4).
fn probe_build(time: &Time, build: &mut Building, state: &mut FpsState) -> bool {
    let Some((choice, style, at)) = std::env::var("MW2_PROBE_BUILD")
        .ok()
        .and_then(|v| parse_probe_build(&v))
    else {
        return false;
    };
    let (t, dt) = (time.elapsed_secs(), time.delta_secs());
    let crossed = |when: f32| t >= when && t - dt < when;
    if crossed(at) {
        build.on = true;
        state.building = true;
        state.build_menu = false;
        build.selected = choice;
        if let Some(style) = style {
            build.style = style.min(look::STYLE_COUNT - 1);
        }
        info!("probe: build choice {choice} style {}", build.style);
    }
    crossed(at + 1.5)
}

/// `MW2_PROBE_BUILD`'s value: the choice, the style if given, and when.
pub fn parse_probe_build(v: &str) -> Option<(usize, Option<u8>, f32)> {
    let (c, at) = v.split_once('@')?;
    let at = at.parse::<f32>().ok()?;
    match c.split_once(':') {
        Some((c, style)) => Some((c.parse().ok()?, Some(style.parse().ok()?), at)),
        None => Some((c.parse().ok()?, None, at)),
    }
}

fn take_controls(
    mut commands: Commands,
    time: Res<Time>,
    mut state: ResMut<FpsState>,
    mut build: ResMut<Building>,
    mut input: ResMut<Mw2Input>,
    mut controls: Option<ResMut<BuildControls>>,
) {
    // A test probe (crate::probe): MW2_PROBE_BUILD=<choice>@<secs> enters build mode with that
    // piece or blueprint (an index into the menu's order) and places it a second and a half on.
    let probe_place = probe_build(&time, &mut build, &mut state);
    let c = BuildControls {
        place: build.on && (input.fire.pressed || probe_place),
        remove: build.on && input.middle.pressed,
        reinforce: build.on && input.ads.pressed,
        turn: build.on && input.reload.pressed,
        free: build.on && input.sprint,
        wheel: if build.on { input.wheel } else { 0.0 },
        cycle: if build.on { input.build_cycle } else { 0 },
    };
    match controls.as_deref_mut() {
        Some(slot) => *slot = c,
        None => commands.insert_resource(c),
    }
    if build.on {
        // The gun is away: none of these reach it.
        input.fire = Default::default();
        input.ads = Default::default();
        input.reload = Default::default();
        input.lethal = Default::default();
        input.tactical = Default::default();
        input.killstreak = Default::default();
        input.wheel_swap = false;
        input.hand = None;
    }
}

/// WoW's own sounds for building: a heavy footfall on wood or stone for a piece put down, a
/// Deadmines door for a door and for a piece breaking apart.
const BREAK_SOUND: &str = "Sound\\Doodad\\DeadmineDoor01Destroy.wav";
const DOOR_SOUND: &str = "Sound\\Doodad\\DeadmineDoor01Open.wav";

fn place_sound(kind: PieceType) -> &'static str {
    match def(kind).material {
        pieces::Material::Stone | pieces::Material::Metal => {
            "Sound\\Character\\Footsteps\\mFootHugeStoneA.wav"
        }
        _ => "Sound\\Character\\Footsteps\\mFootHugeWoodA.wav",
    }
}

/// Play building's sounds through WoW's own player.
fn play_sounds(mut build: ResMut<Building>, script: Option<NonSend<benilla_ui::script::UiScript>>) {
    if build.sounds.is_empty() {
        return;
    }
    let Some(script) = script else {
        build.sounds.clear();
        return;
    };
    let mut played = Vec::new();
    for s in build.sounds.drain(..) {
        // One of each a frame: a blueprint's dozen pieces is one thud, not a dozen.
        if played.contains(&s) {
            continue;
        }
        played.push(s);
        crate::lua::run(&script, &format!("PlaySoundFile({})", crate::lua::quote(s)));
    }
}

/// A chunk of a broken piece, falling.
#[derive(Component)]
struct Debris {
    velocity: Vec3,
    spin: Vec3,
    born: f32,
}

/// How long chunks fall before they go.
const DEBRIS_SECS: f32 = 2.6;

fn break_apart(
    mut commands: Commands,
    time: Res<Time>,
    mut build: ResMut<Building>,
    assets: Option<Res<render::BuildAssets>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut rng: Local<u32>,
) {
    let Some(assets) = assets else {
        build.broken.clear();
        return;
    };
    let t = time.elapsed_secs();
    if *rng == 0 {
        *rng = 0x9E37_79B9;
    }
    let mut next = || {
        // xorshift: chunks need scatter, not quality.
        *rng ^= *rng << 13;
        *rng ^= *rng >> 17;
        *rng ^= *rng << 5;
        (*rng % 10_000) as f32 / 10_000.0
    };
    for p in std::mem::take(&mut build.broken) {
        let material = render::debris_material(&assets, p.kind);
        for w in p.boxes() {
            // Chunks by the box's size: a wall comes apart in a dozen, a beam in three.
            let volume = w.half.x * w.half.y * w.half.z * 8.0;
            let count = (volume * 6.0).clamp(3.0, 14.0) as usize;
            for _ in 0..count {
                let local = Vec3::new(next() - 0.5, next() - 0.5, next() - 0.5) * 2.0;
                let at_wow = w.center
                    + w.axis[0] * (local.x * w.half.x)
                    + w.axis[1] * (local.y * w.half.y)
                    + w.axis[2] * (local.z * w.half.z);
                let at = from_wow(at_wow.to_array());
                let size = Vec3::new(
                    0.15 + next() * 0.45,
                    0.1 + next() * 0.3,
                    0.15 + next() * 0.45,
                );
                let mesh = meshes.add(
                    Mesh::from(Cuboid::new(size.x, size.y, size.z)).with_inserted_attribute(
                        Mesh::ATTRIBUTE_COLOR,
                        vec![[0.55f32, 0.5, 0.45, 1.0]; 24],
                    ),
                );
                commands.spawn((
                    Name::new("mw2 debris"),
                    Debris {
                        velocity: Vec3::new(next() - 0.5, next() * 0.8 + 0.2, next() - 0.5) * 6.0,
                        spin: Vec3::new(next() - 0.5, next() - 0.5, next() - 0.5) * 8.0,
                        born: t,
                    },
                    Mesh3d(mesh),
                    MeshMaterial3d(material.clone()),
                    Transform::from_translation(at),
                ));
            }
        }
    }
}

fn fall_debris(
    mut commands: Commands,
    time: Res<Time>,
    mut chunks: Query<(Entity, &mut Debris, &mut Transform)>,
) {
    let (t, dt) = (time.elapsed_secs(), time.delta_secs());
    for (e, mut d, mut tf) in &mut chunks {
        let age = t - d.born;
        if age > DEBRIS_SECS {
            commands.entity(e).despawn();
            continue;
        }
        d.velocity.y -= 19.6 * dt;
        tf.translation += d.velocity * dt;
        let spin = d.spin * dt;
        tf.rotate(Quat::from_euler(EulerRot::XYZ, spin.x, spin.y, spin.z));
        // Shrinking away at the end.
        let s = ((DEBRIS_SECS - age) / 0.5).clamp(0.0, 1.0);
        tf.scale = Vec3::splat(s);
    }
}

/// What a sentry's burst needs to be seen and heard.
#[derive(bevy::ecs::system::SystemParam)]
struct SentryView<'w, 's> {
    objects: benilla_app::Objects<'w, 's>,
    bodies: Query<'w, 's, &'static GlobalTransform>,
    sentry_shots: MessageWriter<'w, turret::SentryShot>,
    sfx: MessageWriter<'w, crate::mw2::sound::Sfx>,
    mw2: Res<'w, crate::mw2::Mw2Assets>,
    banners: MessageWriter<'w, crate::streaks::Banner>,
}

fn read_server(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut build: ResMut<Building>,
    mut view: SentryView,
) {
    let t = time.elapsed_secs();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel == CHANNEL_WORLD {
            if m.body.first() == Some(&wire::OP_REINFORCEMENTS) {
                if let Some(tiers) = wire::decode_reinforcements(&m.body) {
                    build.reinforcements = tiers;
                }
                continue;
            }
            if let Some(b) = world_banner(&m.body) {
                if let crate::mw2::Mw2Assets::Ready(lib) = &*view.mw2
                    && let Some(alias) = lib
                        .weapons
                        .get("ac130_105mm_mp")
                        .and_then(|w| w.sounds.fire_npc.clone())
                {
                    view.sfx.write(crate::mw2::sound::Sfx::at(alias, 0.4));
                }
                view.banners.write(b);
            }
            continue;
        }
        if m.channel != CHANNEL_BUILD {
            continue;
        }
        let Some(msg) = wire::decode(&m.body) else {
            warn!("benilla-mw2: a build message that does not decode");
            continue;
        };
        match msg {
            ServerMsg::Pieces(list) => {
                for w in list {
                    let was_open = build.pieces.get(&w.id).map(|p| p.wire.open);
                    if was_open.is_none() && w.mine {
                        // The answer to our placement.
                        build.awaiting = None;
                    }
                    // A piece of ours just put down, or a door worked: their sounds (and a door's
                    // keypad, its code right, goes).
                    match was_open {
                        None if w.mine && t > 5.0 => build.sounds.push(place_sound(w.kind)),
                        Some(o) if o != w.open => {
                            build.sounds.push(DOOR_SOUND);
                            build.lock_events.push(locks::LockEvent::Swung(w.id));
                        }
                        _ => {}
                    }
                    info!(
                        "build: piece {} {:?} at {:?} yaw {:.2} support {:.2}",
                        w.id, w.kind, w.pos, w.yaw, w.support
                    );
                    let entity = build.pieces.get(&w.id).and_then(|p| p.entity);
                    build.pieces.insert(
                        w.id,
                        Piece {
                            wire: w,
                            entity,
                            dirty: true,
                            respawn: was_open.is_some_and(|o| o != w.open),
                        },
                    );
                }
            }
            ServerMsg::Removed(list) => {
                for (id, cause) in list {
                    if cause != 0
                        && let Some(p) = build.pieces.get(&id)
                    {
                        let placed = p.placed();
                        build.broken.push(placed);
                        build.sounds.push(BREAK_SOUND);
                    }
                    if let Some(p) = build.pieces.get_mut(&id) {
                        // Despawned by sync_pieces: a removed piece keeps its entity until then.
                        p.wire.health = -1.0;
                    }
                }
            }
            ServerMsg::Health(list) => {
                for (id, health, support) in list {
                    if let Some(p) = build.pieces.get_mut(&id) {
                        p.wire.health = health;
                        p.wire.support = support;
                        p.dirty = true;
                    }
                }
            }
            ServerMsg::Refused(r) => {
                build.awaiting = None;
                let why = refusal_message(&build, r);
                // A lock's answer shows in its dialog too (a keypad's wrong code).
                if is_lock_refusal(r) {
                    build.lock_events.push(locks::LockEvent::Refused(r));
                }
                info!("benilla-mw2: build refused: {why} ({r})");
                // Turned away for coming too soon: send it again (twice at most), quietly.
                if let Some((p, tries)) = build.in_flight
                    && retries_refusal(r, tries)
                {
                    build.queue.push_front(p);
                    build.next_send = t + PLACE_GAP_SECS;
                    continue;
                }
                build.toast = Some((why, t + 2.5));
            }
            // The turret aims, fires from its muzzle and sounds (crate::build::turret).
            ServerMsg::SentryFire { piece, target, hit } => {
                if !build.pieces.contains_key(&piece) {
                    continue;
                }
                let Some(at) = view
                    .objects
                    .entity(target)
                    .and_then(|e| view.bodies.get(e).ok())
                    .map(|g| g.translation() + Vec3::Y * 1.0)
                else {
                    continue;
                };
                view.sentry_shots
                    .write(turret::SentryShot { piece, to: at, hit });
            }
            ServerMsg::Status { owned, max, free } => {
                build.owned = owned;
                build.max = max;
                if build.free != free {
                    build.free = free;
                    build.construction_version += 1;
                }
            }
            ServerMsg::Recipes(list) => {
                build.recipes = list.into_iter().map(|r| (r.kind.0, r)).collect();
                build.construction_version += 1;
            }
            ServerMsg::Skill { tier, skill, cap } => {
                build.tier = tier;
                build.skill = skill;
                build.cap = cap;
                build.construction_version += 1;
            }
            ServerMsg::Plot(p) => build.plot = (p.id != 0).then_some(p),
            ServerMsg::AskCode { piece, portal } => {
                build.last_lock_piece = Some(piece);
                build
                    .lock_events
                    .push(locks::LockEvent::AskCode { piece, portal });
            }
            ServerMsg::PortalMenu(m) => build.lock_events.push(locks::LockEvent::Portal(m)),
            ServerMsg::FurniturePage(p) => build
                .furniture_events
                .push(furniture::FurnitureEvent::Page(p)),
            ServerMsg::Stash(s) => build
                .furniture_events
                .push(furniture::FurnitureEvent::Stash(s)),
        }
    }
}

/// A yard in from a plot's edge, as the server keeps a builder (Plots.h kBuildMargin).
pub const PLOT_MARGIN: f32 = 1.0;

/// Whether every corner of a placed piece's boxes lies inside a plot's bounds (x0, y0, x1, y1),
/// a yard in from its edge: the server's `Plots::Fits`.
pub fn fits_plot(p: &Placed, bounds: [f32; 4]) -> bool {
    p.boxes().all(|w| {
        (0..8).all(|i| {
            let s = |bit: usize| if i & bit != 0 { 1.0 } else { -1.0 };
            let c = w.center
                + w.axis[0] * (w.half.x * s(1))
                + w.axis[1] * (w.half.y * s(2))
                + w.axis[2] * (w.half.z * s(4));
            c.x >= bounds[0] + PLOT_MARGIN
                && c.x <= bounds[2] - PLOT_MARGIN
                && c.y >= bounds[1] + PLOT_MARGIN
                && c.y <= bounds[3] - PLOT_MARGIN
        })
    })
}

/// What the plot underfoot says of a ghost whose middle stands on it: None when it may go up
/// there as far as the plot goes; else why not. A game master builds anywhere.
pub fn plot_says(plot: Option<&wire::PlotInfo>, p: &Placed) -> Option<&'static str> {
    let plot = plot.filter(|q| q.id != 0)?;
    let c = p.center();
    let b = plot.bounds;
    if c.x < b[0] || c.x > b[2] || c.y < b[1] || c.y > b[3] || plot.access >= wire::ACCESS_GM {
        return None;
    }
    if plot.access < wire::ACCESS_BUILDER {
        return Some("Not your plot: its owner and those they permit build here");
    }
    if plot.standing != wire::PLOT_PAID {
        return Some("Upkeep overdue: nothing new goes up until a Plot Steward is paid");
    }
    (!fits_plot(p, b)).then_some("Over the plot's edge: keep it a yard inside")
}

/// Whether a refusal is a lock's or a portal's answer (shown in its dialog as well).
pub fn is_lock_refusal(reason: u8) -> bool {
    matches!(
        reason,
        wire::REFUSED_LOCKED
            | wire::REFUSED_WRONG_CODE
            | wire::REFUSED_TOO_MANY_TRIES
            | wire::REFUSED_COOLING_DOWN
    )
}

/// Who a lock lets in, as a refusal names it.
pub fn locked_text(mode: wire::LockMode) -> &'static str {
    match mode {
        wire::LockMode::Owner => "Locked: only its owner may open it",
        wire::LockMode::Party => "Locked: only its owner and their party may open it",
        wire::LockMode::Guild => "Locked: only its owner and their guild may open it",
        wire::LockMode::Code => "Locked: it needs its code",
        wire::LockMode::Unlocked => "Locked",
    }
}

/// What a refusal tells the player: a skill too low names the skill the piece in flight wants.
pub fn refusal_message(build: &Building, reason: u8) -> String {
    // A lock names whom it lets in.
    if reason == wire::REFUSED_LOCKED
        && let Some(p) = build.last_lock_piece.and_then(|id| build.pieces.get(&id))
    {
        return locked_text(p.wire.lock).into();
    }
    // On a plot the player builds on, "protected" is the plot's edge or its upkeep.
    if reason == wire::REFUSED_PROTECTED
        && let Some((p, _)) = &build.in_flight
        && let Some(why) = plot_says(build.plot.as_ref(), p)
    {
        return why.into();
    }
    if reason == wire::REFUSED_SKILL_TOO_LOW
        && let Some((p, _)) = &build.in_flight
        && let Some(r) = build.recipes.get(&p.kind.0)
    {
        return format!(
            "Requires Construction {} (yours is {})",
            r.required, build.skill
        );
    }
    // Short of materials: what the piece in flight takes.
    if reason == REFUSED_NO_MATERIALS
        && let Some((p, _)) = &build.in_flight
        && let Some(r) = build.recipes.get(&p.kind.0)
        && !r.materials.is_empty()
    {
        return format!(
            "Not enough materials: it takes {}",
            wire::materials_text(&r.materials)
        );
    }
    if reason == REFUSED_TOO_MANY && build.max > 0 {
        return format!(
            "You have built all you may ({} of {}): take something down first",
            build.owned, build.max
        );
    }
    wire::refusal_text(reason).into()
}

/// What a choice costs, as the bar and the menu show it: the server's recipe, else the piece's
/// own cost (a server without Construction).
pub fn cost_text(build: &Building, c: Choice) -> String {
    if build.free {
        return "free".into();
    }
    match c {
        Choice::Piece(t) => match build.recipes.get(&in_style(build, t).0) {
            Some(r) => wire::materials_text(&r.materials),
            None => format!("{} {}", def(t).cost.1, def(t).cost.0),
        },
        Choice::Blueprint(b) => match blueprint_materials(build, b) {
            Some(m) if !m.is_empty() => {
                format!("{} pieces: {}", b.parts.len(), wire::materials_text(&m))
            }
            _ => format!("{} pieces", b.parts.len()),
        },
    }
}

/// Everything a blueprint's pieces take together, by item in the order first met (None when a
/// piece has no recipe from the server).
pub fn blueprint_materials(build: &Building, b: &pieces::Blueprint) -> Option<Vec<(u32, u16)>> {
    let mut out: Vec<(u32, u16)> = Vec::new();
    for part in b.parts {
        let r = build.recipes.get(&in_style(build, part.0).0)?;
        for &(item, count) in &r.materials {
            match out.iter_mut().find(|(i, _)| *i == item) {
                Some((_, n)) => *n = n.saturating_add(count),
                None => out.push((item, count)),
            }
        }
    }
    Some(out)
}

/// A piece in the builder's style: the style's own type for a piece a style redraws (when the
/// server knows it, or there's no Construction to ask), else the piece itself.
pub fn in_style(build: &Building, t: PieceType) -> PieceType {
    match t.styled(build.style) {
        Some(s) if build.recipes.is_empty() || build.recipes.contains_key(&s.0) => s,
        _ => t,
    }
}

/// A choice's name as the bar and the menu show it: a piece a style redraws names its style.
pub fn choice_name(build: &Building, c: Choice) -> String {
    match c {
        Choice::Piece(t) if t.styled(1).is_some() => {
            let s = in_style(build, t);
            format!("{} · {}", def(t).name, look::style_of(s).name)
        }
        _ => c.name().to_string(),
    }
}

/// The Construction skill a choice asks (a blueprint: the most any of its pieces asks).
pub fn required_skill(build: &Building, c: Choice) -> u16 {
    let req = |t: PieceType| {
        build
            .recipes
            .get(&in_style(build, t).0)
            .map_or(0, |r| r.required)
    };
    match c {
        Choice::Piece(t) => req(t),
        Choice::Blueprint(b) => b.parts.iter().map(|p| req(p.0)).max().unwrap_or(0),
    }
}

/// Why the player can't build a choice yet (None: they can). Building free, or a server without
/// Construction, locks nothing.
pub fn locked_reason(build: &Building, c: Choice) -> Option<String> {
    if build.free || build.recipes.is_empty() {
        return None;
    }
    if build.tier == 0 {
        return Some("Learn Construction from a Construction Trainer".into());
    }
    let required = required_skill(build, c);
    if build.skill < required {
        return Some(format!("Requires Construction {required}"));
    }
    kits_short(build, c)
}

/// The kits a choice takes, by piece type (a blueprint one per part).
pub fn kits_needed(build: &Building, c: Choice) -> Vec<(u8, u32)> {
    let mut out: Vec<(u8, u32)> = Vec::new();
    let mut add = |t: PieceType| {
        let k = in_style(build, t).0;
        match out.iter_mut().find(|(x, _)| *x == k) {
            Some((_, n)) => *n += 1,
            None => out.push((k, 1)),
        }
    };
    match c {
        Choice::Piece(t) => add(t),
        Choice::Blueprint(b) => b.parts.iter().for_each(|p| add(p.0)),
    }
    out
}

/// The kits a choice wants and the builder hasn't made yet: None when there are enough (or the
/// server keeps no kits, or building is free).
pub fn kits_short(build: &Building, c: Choice) -> Option<String> {
    if build.free || !build.kits_known {
        return None;
    }
    let short: u32 = kits_needed(build, c)
        .iter()
        .map(|(k, n)| n.saturating_sub(build.kits.get(k).copied().unwrap_or(0)))
        .sum();
    (short > 0).then(|| match c {
        Choice::Piece(_) => "No kit: make one in Construction (Professions)".to_string(),
        Choice::Blueprint(_) => format!("{short} kit(s) short: make them in Construction (Professions)"),
    })
}

/// The kits held of a choice's piece (a blueprint: the fewest of its parts' complete sets).
pub fn kits_held(build: &Building, c: Choice) -> u32 {
    kits_needed(build, c)
        .iter()
        .map(|(k, n)| build.kits.get(k).copied().unwrap_or(0) / n.max(&1))
        .min()
        .unwrap_or(0)
}

/// The player's Construction, for the menu's title and the bar.
pub fn skill_text(build: &Building) -> String {
    if build.recipes.is_empty() {
        String::new()
    } else if build.tier == 0 {
        "Construction: not learned".into()
    } else {
        format!(
            "Construction {} {}/{}",
            wire::tier_name(build.tier),
            build.skill,
            build.cap
        )
    }
}

/// The plot underfoot, for the bar: whose it is to the player and how it stands.
pub fn plot_text(plot: Option<&wire::PlotInfo>, now_unix: u64) -> String {
    let Some(p) = plot else {
        return String::new();
    };
    let size = match p.size {
        1 => "Small homestead",
        3 => "Large homestead",
        _ => "Homestead",
    };
    let whose = match p.access {
        wire::ACCESS_OWNER => "yours",
        wire::ACCESS_BUILDER => "you may build here",
        wire::ACCESS_GUEST => "you are a guest",
        wire::ACCESS_GM => "game master",
        _ if p.standing == wire::PLOT_FOR_SALE => "for sale at the Plot Steward",
        _ => "another player's: protected",
    };
    let standing = match p.standing {
        wire::PLOT_FOR_SALE => String::new(),
        wire::PLOT_PAID => {
            let days = u64::from(p.paid_until).saturating_sub(now_unix) / 86_400;
            format!(
                "  ·  upkeep paid {days} more {}",
                if days == 1 { "day" } else { "days" }
            )
        }
        wire::PLOT_OVERDUE => "  ·  upkeep overdue".into(),
        _ => "  ·  lapsed".into(),
    };
    format!("{size} {}: {whose}{standing}", p.id)
}

/// A world-channel message as a banner: a raid's warning, its start and its end.
pub fn world_banner(body: &[u8]) -> Option<crate::streaks::Banner> {
    let banner = |title: &str, subtitle: String| crate::streaks::Banner {
        title: title.into(),
        subtitle,
    };
    match body {
        [1, secs, count] => Some(banner(
            "RAID INCOMING",
            format!("{count} raiders closing on your base in {secs} seconds"),
        )),
        [2, count, name @ ..] => Some(banner(
            "DEFEND YOUR BASE",
            format!("{} x{count}", String::from_utf8_lossy(name)),
        )),
        [3, 1] => Some(banner("RAID REPELLED", "Experience earned".into())),
        [3, _] => Some(banner("RAIDERS FALL BACK", String::new())),
        _ => None,
    }
}

/// Seconds between a blueprint's placements: the server takes one each 150 ms.
const PLACE_GAP_SECS: f32 = 0.25;
/// The server's `Refusal::Unknown`, also its answer to a placement that came too soon.
const REFUSED_TOO_SOON: u8 = 7;
/// `Refusal::TooMany` and `Refusal::NoMaterials`.
const REFUSED_TOO_MANY: u8 = 3;
const REFUSED_NO_MATERIALS: u8 = 5;

/// Whether a refused placement goes again: one turned away for coming too soon, twice at most.
pub fn retries_refusal(reason: u8, tries: u8) -> bool {
    reason == REFUSED_TOO_SOON && tries < 3
}

/// The player's facing in WoW (radians), from the camera.
fn wow_yaw(forward: Vec3) -> f32 {
    let f = to_wow(forward);
    f[1].atan2(f[0])
}

#[allow(clippy::too_many_arguments)]
fn aim_and_act(
    time: Res<Time>,
    state: Res<FpsState>,
    controls: Option<Res<BuildControls>>,
    mut build: ResMut<Building>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    objects: benilla_app::Objects,
    bodies: Query<&GlobalTransform>,
    spatial: SpatialQuery,
    boxes: Query<&PieceBox>,
    mut send: MessageWriter<SendFusionChannel>,
    keymap: Res<Keymap>,
) {
    let Some(controls) = controls else {
        return;
    };
    if !build.on || !state.active {
        build.ghost.clear();
        build.aimed = None;
        return;
    }
    let Some(cam) = cameras.iter().next() else {
        return;
    };
    let origin = cam.translation();
    let forward = cam.forward();
    // [ and ] pick the piece, the wheel how far out it goes (kept for the session, eased
    // there), R turns it.
    if controls.cycle != 0 {
        let n = choices() as i32;
        build.selected = ((build.selected as i32 + controls.cycle).rem_euclid(n)) as usize;
    }
    if build.distance <= 0.0 {
        build.distance = PLACE_DEFAULT;
        build.distance_shown = PLACE_DEFAULT;
    }
    if controls.wheel != 0.0 {
        build.distance = wheel_distance(build.distance, controls.wheel);
    }
    build.distance_shown = ease_distance(build.distance_shown, build.distance, time.delta_secs());
    let reach = build.distance_shown;
    let chosen = choice(build.selected);
    // Furniture turns in 15-degree steps (R), the rest a quarter at a time.
    let furnishing = matches!(chosen, Choice::Piece(k) if pieces::is_furniture(k));
    if controls.turn {
        if furnishing {
            build.fine_turns = (build.fine_turns + 1).rem_euclid(24);
        } else {
            build.turns = (build.turns + 1).rem_euclid(4);
        }
    }
    build.free_place = controls.free;
    // The builder's style for each piece it redraws (taken now: `build` changes below).
    let restyle: Vec<(PieceType, PieceType)> = look::STYLED
        .iter()
        .map(|t| (*t, in_style(&build, *t)))
        .collect();
    let styled = |t: PieceType| {
        restyle
            .iter()
            .find(|(base, _)| *base == t)
            .map_or(t, |(_, s)| *s)
    };

    // Where the piece goes: along the crosshair to the wheel's distance, on the first thing the
    // ray meets (the world, or a piece: both are colliders); with nothing that near, on the
    // ground or whatever surface is under the ray's end.
    let filter = benilla_world::collision::WorldCollision::body_filter();
    let ray_hit = spatial.cast_ray(origin, forward, reach, true, &filter);
    // The piece taken down or reinforced is the one under the crosshair within the hammer's reach.
    build.aimed = ray_hit
        .filter(|h| h.distance <= REACH)
        .and_then(|h| boxes.get(h.entity).ok())
        .map(|b| b.0);
    let end = origin + *forward * reach;
    let aim_hit = ray_hit
        .map(|h| (origin + *forward * h.distance, h.normal, h.entity))
        .or_else(|| {
            spatial
                .cast_ray(end, Dir3::NEG_Y, 60.0, true, &filter)
                .map(|h| (end + Vec3::NEG_Y * h.distance, h.normal, h.entity))
        });
    let yaw = wow_yaw(*forward) + build.turns as f32 * std::f32::consts::FRAC_PI_2;
    let near: Vec<Placed> = build
        .pieces
        .values()
        .filter(|p| p.wire.health >= 0.0)
        .map(Piece::placed)
        .collect();
    let aim_bevy = aim_hit.map_or(end, |(p, _, _)| p);
    let aim = Vec3::from_array(to_wow(aim_bevy));
    // On open ground: the surface faces up and is the terrain, or the top of a built floor
    // (the server lets ground pieces and WoW's objects stand on floors too).
    let ground = aim_hit.is_some_and(|(_, n, e)| {
        n.y > 0.55
            && boxes.get(e).map_or(true, |b| {
                build
                    .pieces
                    .get(&b.0)
                    .is_some_and(|p| pieces::is_floor(p.wire.kind))
            })
    });
    build.ghost = match chosen {
        // Furniture: on the grid, turned in 15-degree steps, flat on a wall when it hangs; it
        // stands on the ground, a floor or a table's top (the server checks the rest).
        Choice::Piece(kind) if pieces::is_furniture(kind) => {
            let normal = aim_hit.map_or(Vec3::Z, |(_, n, _)| Vec3::from_array(to_wow(n)));
            let p = pieces::place_furniture(
                kind,
                aim,
                normal,
                wow_yaw(*forward),
                build.fine_turns,
                build.free_place,
            );
            let wall = pieces::furniture(kind).is_some_and(|f| f.mount == pieces::Mount::Wall);
            let ok = (wall && normal.z.abs() < 0.5 && aim_hit.is_some()) || (!wall && ground);
            vec![(p, ok && !pieces::overlaps(&near, &p))]
        }
        Choice::Piece(kind) => vec![match snap(&near, styled(kind), aim, yaw, SNAP_RADIUS) {
            Some(p) if !def(kind).ground_only => (p, true),
            _ => {
                let p = Placed {
                    kind: styled(kind),
                    pos: aim,
                    yaw,
                };
                (p, ground && !pieces::overlaps(&near, &p))
            }
        }],
        Choice::Blueprint(b) => {
            // On a slope: lift the whole structure to the highest ground under its footing, so
            // every floor rests on the ground and nothing is buried.
            let footing = blueprint_parts(b, aim, yaw);
            let lift = footing
                .iter()
                .filter(|p| (p.pos.z - aim.z).abs() < 0.01)
                .filter_map(|p| {
                    let from = from_wow((p.center() + Vec3::Z * 4.0).to_array());
                    spatial
                        .cast_ray(from, Dir3::NEG_Y, 12.0, true, &filter)
                        .map(|h| (from.y - h.distance) - aim_bevy.y)
                })
                .fold(0.0f32, f32::max)
                .min(2.0);
            let mut parts = blueprint_parts(b, aim + Vec3::Z * lift, yaw);
            for p in parts.iter_mut() {
                p.kind = styled(p.kind);
            }
            // Pieces that must stand on the ground (sandbags) settle onto the ground under
            // each: the lift leaves them in the air downhill, and the server refuses those.
            for p in parts.iter_mut().filter(|p| def(p.kind).ground_only) {
                let from = from_wow((p.pos + Vec3::Z * 3.0).to_array());
                if let Some(h) = spatial.cast_ray(from, Dir3::NEG_Y, 8.0, true, &filter) {
                    p.pos.z = p.pos.z + 3.0 - h.distance;
                }
            }
            let clear = ground && parts.iter().all(|p| !pieces::overlaps(&near, p));
            parts.into_iter().map(|p| (p, clear)).collect()
        }
    };

    // The plot underfoot has its say: over its edge, or with its upkeep overdue, the ghost is
    // red and the hint says why (the server would refuse it as protected).
    let plot = build.plot;
    let mut why = None;
    // Nothing goes up through the builder: they'd be shut inside it (the server refuses it too,
    // for anyone standing there).
    let feet = objects
        .player()
        .and_then(|e| bodies.get(e).ok())
        .map(|g| Vec3::from_array(to_wow(g.translation())));
    if let Some(feet) = feet {
        for (p, ok) in build.ghost.iter_mut() {
            if pieces::traps_body(p, feet) {
                *ok = false;
                why = why.or(Some(TRAPS_WHY));
            }
        }
    }
    for (p, ok) in build.ghost.iter_mut() {
        if let Some(w) = plot_says(plot.as_ref(), p) {
            *ok = false;
            why = why.or(Some(w));
        }
    }
    build.ghost_why = why;

    let t = time.elapsed_secs();
    if controls.place && !build.queue.is_empty() {
        // Still sending a blueprint.
    } else if controls.place && build.ghost.iter().any(|(_, ok)| !*ok) {
        // Say why nothing went up.
        let why = build
            .ghost_why
            .unwrap_or("It can't stand there: nothing holds it up, or something is in the way");
        build.toast = Some((why.into(), t + 2.0));
    }
    if controls.place && build.queue.is_empty() && build.ghost.iter().all(|(_, ok)| *ok) {
        let parts: Vec<Placed> = build.ghost.iter().map(|(p, _)| *p).collect();
        build.queue.extend(parts);
    }
    // One placement in flight at a time (a second's wait at most for a lost answer).
    let answered = build.awaiting.is_none_or(|sent| t - sent > 1.0);
    if t >= build.next_send
        && answered
        && let Some(p) = build.queue.pop_front()
    {
        // A margin over the server's 150 ms: its tick and the network can bunch two sends.
        build.next_send = t + PLACE_GAP_SECS;
        build.awaiting = Some(t);
        let tries = match build.in_flight {
            Some((q, n)) if q == p => n + 1,
            _ => 1,
        };
        build.in_flight = Some((p, tries));
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_BUILD,
            body: wire::encode_place(p.kind, p.pos, p.yaw),
        }));
    }
    if controls.remove {
        match build.aimed.and_then(|id| build.pieces.get(&id)) {
            // A plot's owner takes down what their builders put up there too (the server checks).
            Some(p)
                if p.wire.mine
                    || build.free
                    || build.plot.is_some_and(|q| q.access >= wire::ACCESS_OWNER) =>
            {
                let id = p.wire.id;
                let name = def(p.wire.kind).name;
                if confirm_press(&mut build.armed, Arm::Remove, id, t) {
                    send.write(SendFusionChannel(ChannelMessage {
                        channel: CHANNEL_BUILD,
                        body: wire::encode_remove(id),
                    }));
                } else {
                    build.toast = Some((
                        format!(
                            "Press {} again to take down the {name} (about half its materials back)",
                            keymap.label(KeyAction::Deconstruct)
                        ),
                        t + CONFIRM_SECS,
                    ));
                }
            }
            Some(_) => {
                build.toast = Some((wire::refusal_text(6).into(), t + 2.0));
            }
            None => {}
        }
    }
    // G reinforces the piece aimed at, a tier at a time (the server checks the skill and takes
    // the materials).
    if controls.reinforce {
        match build.aimed.and_then(|id| build.pieces.get(&id)) {
            Some(p) if p.wire.mine || build.free => {
                let id = p.wire.id;
                let next = build
                    .reinforcements
                    .iter()
                    .find(|r| r.tier == p.wire.reinforcement + 1)
                    .map(|r| {
                        (
                            wire::reinforce_name(r.tier),
                            if build.free {
                                "free".to_string()
                            } else {
                                wire::materials_text(&r.materials)
                            },
                        )
                    });
                // At the top tier (or a piece that takes none) the server says why at once.
                let ask = next.is_some();
                if !ask || confirm_press(&mut build.armed, Arm::Reinforce, id, t) {
                    send.write(SendFusionChannel(ChannelMessage {
                        channel: CHANNEL_WORLD,
                        body: wire::encode_reinforce(id),
                    }));
                } else if let Some((tier, cost)) = next {
                    build.toast = Some((
                        format!(
                            "Press {} again to make it {tier}: {cost}",
                            keymap.label(KeyAction::Aim)
                        ),
                        t + CONFIRM_SECS,
                    ));
                }
            }
            Some(_) => {
                build.toast = Some((wire::refusal_text(6).into(), t + 2.0));
            }
            None => {}
        }
    }
}

/// What a press on a piece asks to be confirmed: taking it down, reinforcing it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arm {
    Remove,
    Reinforce,
}

/// How long a first press waits for its second.
pub const CONFIRM_SECS: f32 = 3.0;

/// A press on piece `id`: true when it goes to the server (the second press on the same piece
/// for the same thing, in time), else it arms and waits. Taking down and reinforcing can't be
/// undone (half a piece's materials, an expensive tier), so neither goes on one stray click.
pub fn confirm_press(armed: &mut Option<(Arm, u32, f32)>, what: Arm, id: u32, now: f32) -> bool {
    match *armed {
        Some((a, i, until)) if a == what && i == id && now <= until => {
            *armed = None;
            true
        }
        _ => {
            *armed = Some((what, id, now + CONFIRM_SECS));
            false
        }
    }
}

/// A piece's upkeep as the aimed-piece text says it: kept by a paid plot, or how long decay
/// (2% of its health a day, the server's `Maintenance.h`) leaves it standing unmended.
pub fn upkeep_text(w: &wire::WirePiece) -> String {
    if w.kept {
        return "kept by the plot's upkeep".into();
    }
    let days = (w.health.max(0.0) / DECAY_PER_DAY).floor() as u32;
    match days {
        0 => "decaying: falls within a day".into(),
        1 => "decaying: about 1 day left".into(),
        n => format!("decaying: about {n} days left"),
    }
}

/// What decay takes a day, of a piece's full health (the server's `Maintenance::kDecayPerDay`).
pub const DECAY_PER_DAY: f32 = 0.02;

/// The piece a Repair Hammer is aimed at, as the line under the crosshair shows it: its name,
/// health and upkeep.
pub fn repair_text(build: &Building, id: u32) -> String {
    let Some(p) = build.pieces.get(&id) else {
        return String::new();
    };
    let w = &p.wire;
    let name = match w.kind.style() {
        0 => def(w.kind).name.to_string(),
        _ => format!("{} ({})", def(w.kind).name, look::style_of(w.kind).name),
    };
    format!(
        "{name}  {}%  ·  {}",
        (w.health * 100.0).round() as u32,
        upkeep_text(w)
    )
}

/// The piece under the crosshair as the bar shows it: its name, health, reinforcement, upkeep and
/// what the next tier costs (for one of ours).
pub fn aimed_text(build: &Building, keys: &Keymap) -> String {
    let Some(p) = build.aimed.and_then(|id| build.pieces.get(&id)) else {
        return String::new();
    };
    let w = &p.wire;
    let name = match w.kind.style() {
        0 => def(w.kind).name.to_string(),
        _ => format!("{} ({})", def(w.kind).name, look::style_of(w.kind).name),
    };
    let mut s = format!(
        "{}  {}%  ·  {}  ·  {}",
        name,
        (w.health * 100.0).round() as u32,
        wire::reinforce_name(w.reinforcement),
        upkeep_text(w)
    );
    // A door's or portal's lock (yours: the Interact key sets it).
    if pieces::lockable(w.kind) {
        s.push_str(&format!("  ·  {}", w.lock.name()));
        if w.mine && build.on {
            s.push_str(&format!(" ({} to change)", keys.label(KeyAction::Interact)));
        }
    }
    if w.mine && !w.kind.is_portal() {
        match build
            .reinforcements
            .iter()
            .find(|r| r.tier == w.reinforcement + 1)
        {
            Some(next) => s.push_str(&format!(
                "  ·  {} {}: {} (Construction {})",
                keys.label(KeyAction::Aim),
                wire::reinforce_name(next.tier),
                if build.free {
                    "free".into()
                } else {
                    wire::materials_text(&next.materials)
                },
                next.skill
            )),
            None if !build.reinforcements.is_empty() => s.push_str("  ·  fully reinforced"),
            None => {}
        }
    }
    s
}

/// What F does on a piece: work a door, open a portal's list, or (in build mode, on one of
/// yours) open its lock dialog.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Interact {
    Toggle,
    Portal,
    LockSettings,
    /// A piece of furniture's use (sit, lie down, read, the stash...).
    Use,
}

/// How far F reaches: a door's handle, a portal's ring (the server allows a little more).
pub const DOOR_REACH: f32 = 4.0;
pub const PORTAL_REACH: f32 = 5.0;
/// F on furniture (the server's `Furniture::kUseReach`).
pub const FURNITURE_REACH: f32 = 4.0;

/// What F does on a piece of `kind` at `distance`: `mine` when the player may set its lock (its
/// builder, a plot's owner, a game master: the server checks).
pub fn interact_action(
    kind: PieceType,
    distance: f32,
    building: bool,
    mine: bool,
) -> Option<Interact> {
    let door = kind.base() == PieceType::WoodDoor;
    let portal = kind.is_portal();
    if let Some(f) = pieces::furniture(kind) {
        // Plain decor does nothing; the rest is used out of build mode (in it, F is free).
        return (f.use_ != pieces::FurnitureUse::Decor
            && f.use_ != pieces::FurnitureUse::Surface
            && distance <= FURNITURE_REACH
            && !building)
            .then_some(Interact::Use);
    }
    if !(door || portal) || distance > if portal { PORTAL_REACH } else { DOOR_REACH } {
        return None;
    }
    if building && mine {
        Some(Interact::LockSettings)
    } else if portal {
        Some(Interact::Portal)
    } else {
        Some(Interact::Toggle)
    }
}

/// F on a door within reach opens or shuts it, on a portal opens its list (in build mode or
/// out); in build mode, on one of yours, it opens the lock dialog.
fn use_doors(
    state: Res<FpsState>,
    input: Res<Mw2Input>,
    mut build: ResMut<Building>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    spatial: SpatialQuery,
    boxes: Query<&PieceBox>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    if !state.owns_input || !input.interact.pressed {
        return;
    }
    let Some(cam) = cameras.iter().next() else {
        return;
    };
    let Some(hit) = spatial.cast_ray(
        cam.translation(),
        cam.forward(),
        PORTAL_REACH,
        true,
        &benilla_world::collision::WorldCollision::body_filter(),
    ) else {
        return;
    };
    let Some(id) = boxes.get(hit.entity).ok().map(|b| b.0) else {
        return;
    };
    let Some(p) = build.pieces.get(&id) else {
        return;
    };
    let (kind, lock) = (p.wire.kind, p.wire.lock);
    let mine =
        p.wire.mine || build.free || build.plot.is_some_and(|q| q.access >= wire::ACCESS_OWNER);
    let Some(action) = interact_action(kind, hit.distance, build.on, mine) else {
        return;
    };
    build.last_lock_piece = Some(id);
    let body = match action {
        Interact::Toggle => wire::encode_toggle(id),
        Interact::Portal => wire::encode_portal_ask(id),
        Interact::Use => wire::encode_furniture_use(id),
        Interact::LockSettings => {
            build.lock_events.push(locks::LockEvent::Settings {
                piece: id,
                mode: lock,
                portal: kind.is_portal(),
            });
            return;
        }
    };
    send.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL_BUILD,
        body,
    }));
}

/// What drawing a piece as a model needs.
#[derive(bevy::ecs::system::SystemParam)]
pub struct ModelParams<'w> {
    cache: ResMut<'w, models::ModelCache>,
    world: Option<Res<'w, benilla_assets::WorldAssets>>,
    mw2: Res<'w, crate::mw2::Mw2Assets>,
    server: Res<'w, AssetServer>,
    materials: ResMut<'w, Assets<StandardMaterial>>,
    images: ResMut<'w, Assets<Image>>,
}

impl ModelParams<'_> {
    fn get(&mut self, m: pieces::PieceModel) -> models::ModelState {
        let world = self.world.as_deref();
        self.cache.get(
            m,
            world,
            &self.mw2,
            &self.server,
            &mut self.materials,
            &mut self.images,
        )
    }
}

/// Spawn, recolour and despawn the pieces' entities; a new world (another continent, a
/// reconnect) starts empty.
#[allow(clippy::too_many_arguments)]
fn sync_pieces(
    mut commands: Commands,
    mut build: ResMut<Building>,
    assets: Option<Res<render::BuildAssets>>,
    cameras: Query<Entity, With<WorldCamera>>,
    mut meshes: ResMut<Assets<Mesh>>,
    light: Res<render::BuildLight>,
    mut epoch: ResMut<benilla_world::collision::ColliderEpoch>,
    roots: Query<Entity, With<PieceRoot>>,
    mut models: ModelParams,
) {
    let Some(assets) = assets else {
        return;
    };
    // The camera stands for the world: a new one, a new map and a fresh stream from the server.
    let world = cameras.iter().next();
    if build.world != world {
        build.world = world;
        build.pieces.clear();
        for e in &roots {
            commands.entity(e).despawn();
        }
        epoch.bump();
        return;
    }
    let mut gone = Vec::new();
    let mut changed = false;
    for (id, p) in build.pieces.iter_mut() {
        if p.wire.health < 0.0 {
            if let Some(e) = p.entity.take() {
                commands.entity(e).despawn();
                changed = true;
            }
            gone.push(*id);
            continue;
        }
        if p.respawn {
            p.respawn = false;
            if let Some(e) = p.entity.take() {
                commands.entity(e).despawn();
            }
        }
        if p.entity.is_none() {
            // A piece drawn as a model waits for its model (the MW2 install loads behind).
            let model = match look::model_of(p.wire.kind) {
                Some(m) => match models.get(m) {
                    models::ModelState::Ready(t) => Some(t),
                    models::ModelState::Pending => continue,
                    models::ModelState::Failed => None,
                },
                None => None,
            };
            p.entity = Some(render::spawn_piece(
                &mut commands,
                &assets,
                &mut meshes,
                &light,
                p,
                model.as_deref().map(Vec::as_slice),
            ));
            p.dirty = false;
            changed = true;
        }
        // Colours follow in render::relight.
        p.dirty = false;
    }
    for id in gone {
        build.pieces.remove(&id);
    }
    if changed {
        epoch.bump();
    }
}

fn draw_ghost(
    mut commands: Commands,
    build: Res<Building>,
    assets: Option<Res<render::BuildAssets>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut models: ModelParams,
    mut ghosts: Query<(
        Entity,
        &mut Ghost,
        &mut Transform,
        &mut Visibility,
        &mut MeshMaterial3d<StandardMaterial>,
    )>,
) {
    let Some(assets) = assets else {
        return;
    };
    // A pool as big as the largest blueprint.
    let have = ghosts.iter().count();
    for _ in have..build.ghost.len() {
        commands.spawn((
            Name::new("mw2 build ghost"),
            Ghost {
                kind: None,
                valid: false,
            },
            Transform::default(),
            Visibility::Hidden,
            MeshMaterial3d(assets.ghost_ok.clone()),
        ));
    }
    let mut wanted = build.ghost.iter();
    for (e, mut ghost, mut tf, mut vis, mut mat) in &mut ghosts {
        let Some((p, valid)) = wanted.next() else {
            *vis = Visibility::Hidden;
            continue;
        };
        if ghost.kind != Some(p.kind) {
            // A piece drawn as a model shows its model (the sentry, the campfire, props).
            let model = look::model_of(p.kind).map(|m| models.get(m));
            let pending = matches!(model, Some(models::ModelState::Pending));
            let mesh = match model {
                Some(models::ModelState::Ready(t)) => {
                    models::merged_mesh(&t, render::model_scale(p.kind))
                }
                // Not read yet: try again next frame, boxes meanwhile.
                Some(models::ModelState::Pending) => render::ghost_mesh(p.kind),
                _ => render::ghost_mesh(p.kind),
            };
            if !pending {
                ghost.kind = Some(p.kind);
            }
            commands.entity(e).insert(Mesh3d(meshes.add(mesh)));
        }
        if ghost.valid != *valid {
            ghost.valid = *valid;
            mat.0 = if *valid {
                assets.ghost_ok.clone()
            } else {
                assets.ghost_bad.clone()
            };
        }
        *tf = render::root_transform(p.pos, p.yaw);
        *vis = Visibility::Visible;
    }
}

fn flicker(time: Res<Time>, mut flames: Query<(&Flame, &mut Transform)>) {
    let t = time.elapsed_secs();
    for (f, mut tf) in &mut flames {
        let s = 1.0 + 0.12 * (t * 9.0 + f.phase).sin() + 0.07 * (t * 23.0 + f.phase * 2.0).sin();
        tf.scale = Vec3::new(1.0, s, 1.0);
    }
}

fn spawn_ui(mut commands: Commands, assets: Res<AssetServer>) {
    let font: Handle<Font> = assets.load(crate::wow_ui::FONT);
    let text = |s: &str, size: f32, color: Color| {
        (
            Text::new(s),
            TextFont::from_font_size(size).with_font(font.clone()),
            TextColor(color),
            TextShadow {
                offset: Vec2::splat(1.5),
                color: Color::srgba(0.0, 0.0, 0.0, 0.85),
            },
            bevy::picking::Pickable::IGNORE,
        )
    };
    commands.spawn((
        Name::new("mw2 build toast"),
        BuildToast,
        Node {
            position_type: PositionType::Absolute,
            top: percent(58),
            width: percent(100),
            justify_content: JustifyContent::Center,
            ..default()
        },
        text("", 20.0, Color::srgb(1.0, 0.35, 0.3)),
        TextLayout::new_with_justify(Justify::Center),
        GlobalZIndex(i32::MAX - 2),
        Visibility::Hidden,
    ));
}

/// A line of the build-mode hint panel: its text, colour and size (UI units).
#[derive(Clone, Debug, PartialEq)]
pub struct HintLine {
    pub text: String,
    pub color: Color,
    pub size: f32,
}

/// The keys build mode takes, as the hint panel lists them under the piece: (key, what it does),
/// each the key bound now.
pub fn hint_keys(keys: &Keymap) -> [(String, &'static str); 9] {
    let k = |a: KeyAction| keys.label(a);
    [
        (k(KeyAction::Fire), "Place"),
        (
            format!("{} {}", k(KeyAction::BuildPrev), k(KeyAction::BuildNext)),
            "Piece",
        ),
        ("Mouse Wheel".into(), "Distance"),
        (k(KeyAction::Reload), "Turn"),
        (k(KeyAction::Build), "Pieces"),
        (format!("{} x2", k(KeyAction::Deconstruct)), "Take Down"),
        (format!("{} x2", k(KeyAction::Aim)), "Reinforce"),
        (k(KeyAction::Interact), "Set a Lock"),
        ("Esc".into(), "Done"),
    ]
}

/// The hint panel's lines, as a tooltip lists an item: the piece in hand (its neighbours on [ and
/// ] under it), how far out it goes, what it costs or why it's locked, the player's Construction and how much
/// they have built, the plot underfoot and the piece aimed at.
pub fn hint_lines(build: &Building, now_unix: u64, keys: &Keymap) -> Vec<HintLine> {
    use crate::wow_ui::{DIM, GOLD, GREEN, GREY, RED, WHITE};
    let line = |text: String, color: Color, size: f32| HintLine { text, color, size };
    let n = choices();
    let selected = build.selected;
    let name = |i: usize| choice(i % n).name();
    let c = choice(selected);
    let mut out = vec![
        line(choice_name(build, c), GOLD, 13.0),
        line(
            format!("‹ {}     {} ›", name(selected + n - 1), name(selected + 1)),
            GREY,
            10.0,
        ),
        line(cost_text(build, c), WHITE, 11.0),
    ];
    if build.distance > 0.0 {
        out.push(line(format!("{:.0} yd out", build.distance), DIM, 10.0));
    }
    if let Some(why) = locked_reason(build, c) {
        out.push(line(why, RED, 11.0));
    }
    if let Some(why) = build.ghost_why {
        out.push(line(why.into(), RED, 11.0));
    }
    let skill = skill_text(build);
    // No cap on pieces (max 0): their cost and upkeep are the limit.
    let built = if build.max == 0 {
        format!("Built {}", build.owned)
    } else {
        format!("Built {}/{}", build.owned, build.max)
    };
    out.push(line(
        if skill.is_empty() {
            built
        } else {
            format!("{skill}  ·  {built}")
        },
        DIM,
        10.0,
    ));
    let plot = plot_text(build.plot.as_ref(), now_unix);
    if !plot.is_empty() {
        let color = match build.plot.map(|p| (p.access, p.standing)) {
            Some((a, _)) if a >= wire::ACCESS_BUILDER => GREEN,
            Some((_, wire::PLOT_FOR_SALE)) => DIM,
            _ => RED,
        };
        out.push(line(plot, color, 10.0));
    }
    let aimed = aimed_text(build, keys);
    if !aimed.is_empty() {
        out.push(line(aimed, WHITE, 10.0));
    }
    out
}

/// The hint panel as plain text (its lines, then the keys), for the tests and the log.
pub fn bar_text(build: &Building, now_unix: u64, keys: &Keymap) -> String {
    let mut s: Vec<String> = hint_lines(build, now_unix, keys)
        .into_iter()
        .map(|l| l.text)
        .collect();
    s.push(
        hint_keys(keys)
            .iter()
            .map(|(k, a)| format!("{k} {a}"))
            .collect::<Vec<_>>()
            .join("   "),
    );
    s.join("\n")
}

/// The build-mode hint panel: a tooltip-framed panel low at the right, as the stock tooltip
/// sits, built at the first world frame.
#[derive(Component)]
struct HintPanel;
/// Its lines, drawn again when they change.
#[derive(Component)]
struct HintBody;
/// The key of the hint panel's `n`th key row, written again when a binding changes.
#[derive(Component)]
struct HintKey(usize);

#[derive(Resource)]
struct HintFont(Handle<Font>);

fn spawn_hint(
    mut commands: Commands,
    assets: Res<AssetServer>,
    state: Res<crate::mode::FpsState>,
    mut built: Local<bool>,
) {
    use crate::wow_ui::{self, Anchor, DIM, FONT, Frame, GOLD, WindowSpec};
    if *built || !state.in_world {
        return;
    }
    *built = true;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: "build hint",
        title: String::new(),
        frame: Frame::Tooltip,
        width: Some(250.0),
        movable: false,
        anchor: Anchor {
            at: Vec2::ONE,
            pivot: Vec2::ONE,
            offset: Vec2::new(-24.0, -150.0),
        },
        z: i32::MAX - 4,
    };
    wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        (HintPanel, Visibility::Hidden),
        None::<()>,
        |root| {
            root.spawn((
                HintBody,
                Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: wow_ui::u(1.0),
                    ..default()
                },
            ));
            root.spawn(Node {
                height: wow_ui::u(6.0),
                ..default()
            });
            for (i, (key, action)) in hint_keys(&Keymap::defaults()).into_iter().enumerate() {
                root.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    justify_content: JustifyContent::SpaceBetween,
                    width: wow_ui::u(234.0),
                    ..default()
                })
                .with_children(|r| {
                    r.spawn((wow_ui::text(f, &key, 10.0, GOLD), HintKey(i)));
                    r.spawn(wow_ui::text(f, action, 10.0, DIM));
                });
            }
        },
        |_| {},
    );
    commands.insert_resource(HintFont(font));
}

/// Unix seconds now (the plot's paid time is in them).
fn now_unix() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs())
}

#[allow(clippy::too_many_arguments)]
fn draw_ui(
    mut commands: Commands,
    time: Res<Time>,
    build: Res<Building>,
    font: Option<Res<HintFont>>,
    mut shown: Local<Vec<HintLine>>,
    mut panel: Query<&mut Visibility, (With<HintPanel>, Without<BuildToast>)>,
    body: Query<(Entity, Option<&Children>), With<HintBody>>,
    mut toast: Query<(&mut Text, &mut Visibility), (With<BuildToast>, Without<HintPanel>)>,
    mut key_rows: Query<(&HintKey, &mut Text), Without<BuildToast>>,
    keymap: Res<Keymap>,
) {
    if build.on {
        let rows = hint_keys(&keymap);
        for (HintKey(i), mut text) in &mut key_rows {
            if let Some((k, _)) = rows.get(*i)
                && text.0 != *k
            {
                text.0.clone_from(k);
            }
        }
    }
    let t = time.elapsed_secs();
    for mut vis in &mut panel {
        let want = if build.on {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *vis != want {
            *vis = want;
        }
    }
    if build.on
        && let Some(font) = font
        && let Ok((e, children)) = body.single()
    {
        let lines = hint_lines(&build, now_unix(), &keymap);
        if *shown != lines {
            for c in children.into_iter().flatten() {
                commands.entity(*c).despawn();
            }
            commands.entity(e).with_children(|b| {
                for l in &lines {
                    b.spawn(crate::wow_ui::text(&font.0, &l.text, l.size, l.color));
                }
            });
            *shown = lines;
        }
    }
    if let Ok((mut text, mut vis)) = toast.single_mut() {
        match &build.toast {
            Some((s, until)) if t < *until => {
                if text.0 != *s {
                    text.0 = s.clone();
                }
                *vis = Visibility::Visible;
            }
            _ => *vis = Visibility::Hidden,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_placement_turned_away_for_coming_too_soon_goes_again() {
        assert!(retries_refusal(REFUSED_TOO_SOON, 1));
        assert!(retries_refusal(REFUSED_TOO_SOON, 2));
        assert!(!retries_refusal(REFUSED_TOO_SOON, 3)); // twice more at most
        assert!(!retries_refusal(2, 1)); // something in the way: no
        assert!(PLACE_GAP_SECS > 0.15 + 0.05); // a margin over the server's gap
    }

    #[test]
    fn the_wheel_sets_how_far_out_within_its_range_and_glides_there() {
        assert_eq!(
            wheel_distance(PLACE_DEFAULT, 1.0),
            PLACE_DEFAULT + PLACE_STEP
        );
        assert_eq!(
            wheel_distance(PLACE_DEFAULT, -2.0),
            PLACE_DEFAULT - 2.0 * PLACE_STEP
        );
        assert_eq!(wheel_distance(PLACE_MAX - 0.5, 3.0), PLACE_MAX);
        assert_eq!(wheel_distance(PLACE_MIN + 0.5, -3.0), PLACE_MIN);
        assert!(PLACE_MIN <= 2.0 && PLACE_MAX >= 25.0);
        // Eased: partway in a frame, there within half a second, never past it.
        let mut shown = 6.0;
        shown = ease_distance(shown, 10.0, 1.0 / 60.0);
        assert!(shown > 6.0 && shown < 10.0);
        for _ in 0..30 {
            shown = ease_distance(shown, 10.0, 1.0 / 60.0);
        }
        assert!((shown - 10.0).abs() < 0.01);
        // The hint lists the keys.
        let keys = hint_keys(&Keymap::defaults());
        assert!(keys.contains(&("Mouse Wheel".into(), "Distance")));
        assert!(keys.contains(&("[ ]".into(), "Piece")));
        assert!(keys.contains(&("Middle Mouse x2".into(), "Take Down")));
        assert!(keys.contains(&("Esc".into(), "Done")));
        assert!(
            keys.contains(&("B".into(), "Pieces")),
            "B opens the window for a piece"
        );
    }

    fn bar(selected: usize, owned: u16, free: bool) -> Building {
        Building {
            selected,
            owned,
            max: 600,
            free,
            ..default()
        }
    }

    #[test]
    fn the_bar_shows_the_aimed_pieces_reinforcement_and_the_next_tier() {
        let mut b = bar(1, 3, false);
        let w = wire::WirePiece {
            id: 4,
            kind: PieceType::StoneWall,
            pos: Vec3::ZERO,
            yaw: 0.0,
            health: 0.5,
            support: 1.0,
            mine: true,
            open: false,
            reinforcement: 1,
            lock: wire::LockMode::Party,
            kept: false,
        };
        b.pieces.insert(
            4,
            Piece {
                wire: w,
                entity: None,
                dirty: false,
                respawn: false,
            },
        );
        b.aimed = Some(4);
        b.reinforcements = vec![wire::ReinforceTier {
            tier: 2,
            skill: 225,
            health_x100: 350,
            bullet_cut: 45,
            materials: vec![(91023, 3)],
        }];
        let s = aimed_text(&b, &Keymap::defaults());
        assert!(s.contains("50%") && s.contains("Braced"), "{s}");
        assert!(
            s.contains("Right Mouse Plated: 3 Reinforced Plate (Construction 225)"),
            "{s}"
        );
        assert!(hint_keys(&Keymap::defaults()).contains(&("Right Mouse x2".into(), "Reinforce")));
        assert!(bar_text(&b, 0, &Keymap::defaults()).contains("Right Mouse x2 Reinforce"));
        b.pieces.get_mut(&4).unwrap().wire.reinforcement = 2;
        assert!(aimed_text(&b, &Keymap::defaults()).contains("fully reinforced"));
        b.aimed = None;
        assert!(aimed_text(&b, &Keymap::defaults()).is_empty());
    }

    #[test]
    fn the_bar_names_the_piece_its_cost_and_neighbours() {
        let s = bar_text(&bar(1, 3, false), 0, &Keymap::defaults());
        assert!(
            s.contains("Wood wall") && s.contains("Wood floor") && s.contains("Wood half wall")
        );
        assert!(s.contains("2 Simple Wood") && s.contains("3/600"));
        assert!(bar_text(&bar(0, 0, true), 0, &Keymap::defaults()).contains("free"));
        assert!(bar_text(&bar(0, 0, true), 0, &Keymap::defaults()).contains("sandbag nest")); // wraps round
    }

    fn wall_recipe() -> wire::Recipe {
        wire::Recipe {
            kind: PieceType::WoodWall,
            required: 20,
            yellow: 45,
            green: 70,
            gray: 95,
            materials: vec![(91001, 5)],
        }
    }

    #[test]
    fn the_bar_shows_the_recipe_the_skill_and_what_is_locked() {
        let mut b = bar(1, 3, false);
        b.recipes.insert(PieceType::WoodWall.0, wall_recipe());
        // Not learned: the piece is locked, and the bar says where to learn.
        let s = bar_text(&b, 0, &Keymap::defaults());
        assert!(s.contains("5 Rough Lumber") && s.contains("Construction Trainer"));
        assert!(s.contains("Construction: not learned"));
        // Learned, too low for the wall, then high enough.
        b.tier = 1;
        b.skill = 12;
        b.cap = 75;
        assert!(bar_text(&b, 0, &Keymap::defaults()).contains("Requires Construction 20"));
        assert!(bar_text(&b, 0, &Keymap::defaults()).contains("Construction Apprentice 12/75"));
        b.skill = 20;
        assert!(locked_reason(&b, Choice::Piece(PieceType::WoodWall)).is_none());
        // Building free locks nothing.
        b.skill = 1;
        b.free = true;
        assert!(locked_reason(&b, Choice::Piece(PieceType::WoodWall)).is_none());
    }

    #[test]
    fn a_skill_refusal_names_the_skill_wanted() {
        let mut b = bar(1, 0, false);
        b.recipes.insert(PieceType::WoodWall.0, wall_recipe());
        b.skill = 7;
        b.in_flight = Some((
            Placed {
                kind: PieceType::WoodWall,
                pos: Vec3::ZERO,
                yaw: 0.0,
            },
            1,
        ));
        assert_eq!(
            refusal_message(&b, wire::REFUSED_SKILL_TOO_LOW),
            "Requires Construction 20 (yours is 7)"
        );
        assert!(refusal_message(&b, wire::REFUSED_PROTECTED).contains("Protected"));
    }

    #[test]
    fn the_plot_underfoot_reads_plainly() {
        let mut p = wire::PlotInfo {
            id: 105,
            size: 2,
            standing: wire::PLOT_PAID,
            access: wire::ACCESS_OWNER,
            bounds: [0.0; 4],
            paid_until: 10 * 86_400,
            permits: 0,
        };
        assert_eq!(
            plot_text(Some(&p), 3 * 86_400),
            "Homestead 105: yours  ·  upkeep paid 7 more days"
        );
        p.access = 0;
        assert!(plot_text(Some(&p), 0).contains("protected"));
        p.standing = wire::PLOT_FOR_SALE;
        assert!(plot_text(Some(&p), 0).contains("for sale"));
        assert!(plot_text(None, 0).is_empty());
    }

    #[test]
    fn raids_announce_themselves() {
        let w = world_banner(&[1, 20, 5]).unwrap();
        assert_eq!(w.title, "RAID INCOMING");
        assert!(w.subtitle.contains("5 raiders") && w.subtitle.contains("20 seconds"));
        let mut start = vec![2, 4];
        start.extend_from_slice(b"Defias Thug");
        assert_eq!(world_banner(&start).unwrap().subtitle, "Defias Thug x4");
        assert_eq!(world_banner(&[3, 1]).unwrap().title, "RAID REPELLED");
        assert!(world_banner(&[7]).is_none());
    }

    #[test]
    fn the_facing_reads_from_the_camera() {
        // Looking along WoW +x (bevy -z): yaw 0; along WoW +y (bevy -x): a quarter turn.
        assert!(wow_yaw(Vec3::NEG_Z).abs() < 1e-5);
        assert!((wow_yaw(Vec3::NEG_X) - std::f32::consts::FRAC_PI_2).abs() < 1e-5);
    }

    #[test]
    fn taking_down_and_reinforcing_ask_for_a_second_press() {
        let mut armed = None;
        // The first press arms; the second on the same piece, in time, goes.
        assert!(!confirm_press(&mut armed, Arm::Remove, 7, 10.0));
        assert!(confirm_press(&mut armed, Arm::Remove, 7, 11.0));
        assert!(armed.is_none());
        // Another piece, the other action or too late: it asks again.
        assert!(!confirm_press(&mut armed, Arm::Remove, 7, 20.0));
        assert!(!confirm_press(&mut armed, Arm::Remove, 8, 20.5));
        assert!(!confirm_press(&mut armed, Arm::Reinforce, 8, 21.0));
        assert!(!confirm_press(
            &mut armed,
            Arm::Reinforce,
            8,
            21.0 + CONFIRM_SECS + 0.1
        ));
        assert!(confirm_press(
            &mut armed,
            Arm::Reinforce,
            8,
            22.0 + CONFIRM_SECS
        ));
    }

    #[test]
    fn only_the_window_enters_build_mode_and_escape_leaves_it() {
        // No key enters it; it stays until Esc.
        assert!(!build_mode_wanted(true, false, false));
        assert!(build_mode_wanted(true, true, false));
        assert!(!build_mode_wanted(true, true, true));
        assert!(!build_mode_wanted(true, false, true));
        // Never with the layer off.
        assert!(!build_mode_wanted(false, true, false));
    }

    #[test]
    fn a_blueprint_shows_what_its_pieces_take_together() {
        let mut b = Building::default();
        let bp = &pieces::BLUEPRINTS[0];
        // No recipes from the server: the count of pieces only.
        assert_eq!(
            cost_text(&b, Choice::Blueprint(bp)),
            format!("{} pieces", bp.parts.len())
        );
        for part in bp.parts {
            let t = in_style(&b, part.0);
            b.recipes.insert(
                t.0,
                wire::Recipe {
                    kind: t,
                    required: 1,
                    yellow: 25,
                    green: 50,
                    gray: 75,
                    materials: vec![(91001, 2)],
                },
            );
        }
        let total = blueprint_materials(&b, bp).unwrap();
        assert_eq!(total.len(), 1);
        assert_eq!(usize::from(total[0].1), 2 * bp.parts.len());
        assert!(
            cost_text(&b, Choice::Blueprint(bp))
                .starts_with(&format!("{} pieces: ", bp.parts.len()))
        );
    }

    #[test]
    fn f_works_a_door_opens_a_portal_and_in_build_mode_sets_your_lock() {
        let portal = PieceType(pieces::PORTAL);
        let door = PieceType::WoodDoor;
        assert_eq!(
            interact_action(door, 3.0, false, true),
            Some(Interact::Toggle)
        );
        assert_eq!(
            interact_action(door, 3.0, true, false),
            Some(Interact::Toggle)
        );
        assert_eq!(
            interact_action(door, 3.0, true, true),
            Some(Interact::LockSettings)
        );
        assert_eq!(
            interact_action(portal, 4.5, false, false),
            Some(Interact::Portal)
        );
        assert_eq!(
            interact_action(portal, 4.5, true, true),
            Some(Interact::LockSettings)
        );
        // Out of reach, or nothing that takes F.
        assert_eq!(interact_action(door, 4.5, false, false), None);
        assert_eq!(interact_action(portal, 5.5, false, false), None);
        assert_eq!(interact_action(PieceType::WoodWall, 1.0, true, true), None);
        // A styled door is a door.
        let orcish = door.styled(3).unwrap();
        assert_eq!(
            interact_action(orcish, 1.0, false, false),
            Some(Interact::Toggle)
        );
        assert!(hint_keys(&Keymap::defaults()).contains(&("F".into(), "Set a Lock")));
    }

    #[test]
    fn a_lock_refusal_names_whom_it_lets_in() {
        let mut b = Building::default();
        let w = wire::WirePiece {
            id: 3,
            kind: PieceType::WoodDoor,
            pos: Vec3::ZERO,
            yaw: 0.0,
            health: 1.0,
            support: 1.0,
            mine: false,
            open: false,
            reinforcement: 0,
            lock: wire::LockMode::Guild,
            kept: false,
        };
        b.pieces.insert(
            3,
            Piece {
                wire: w,
                entity: None,
                dirty: false,
                respawn: false,
            },
        );
        b.last_lock_piece = Some(3);
        assert!(refusal_message(&b, wire::REFUSED_LOCKED).contains("guild"));
        assert!(is_lock_refusal(wire::REFUSED_WRONG_CODE) && !is_lock_refusal(5));
        // The bar names a door's lock.
        b.aimed = Some(3);
        assert!(aimed_text(&b, &Keymap::defaults()).contains("Owner and guild"));
        b.last_lock_piece = None;
        assert!(refusal_message(&b, wire::REFUSED_LOCKED).contains("Locked"));
    }

    #[test]
    fn refusals_say_what_is_missing() {
        let mut b = Building::default();
        b.owned = 600;
        b.max = 600;
        assert!(refusal_message(&b, REFUSED_TOO_MANY).contains("600 of 600"));
        let p = Placed {
            kind: PieceType::WoodWall,
            pos: Vec3::ZERO,
            yaw: 0.0,
        };
        b.in_flight = Some((p, 1));
        b.recipes.insert(
            PieceType::WoodWall.0,
            wire::Recipe {
                kind: PieceType::WoodWall,
                required: 1,
                yellow: 25,
                green: 50,
                gray: 75,
                materials: vec![(91001, 8)],
            },
        );
        assert!(
            refusal_message(&b, REFUSED_NO_MATERIALS)
                .starts_with("Not enough materials: it takes 8 ")
        );
        assert!(refusal_message(&b, wire::REFUSED_OUT_OF_SIGHT).contains("can't see"));
    }

    #[test]
    fn the_aimed_piece_says_its_decay_or_that_a_plot_keeps_it() {
        let mut w = wire::WirePiece {
            id: 9,
            kind: PieceType::WoodWall,
            pos: Vec3::ZERO,
            yaw: 0.0,
            health: 0.87,
            support: 1.0,
            mine: true,
            open: false,
            reinforcement: 0,
            lock: wire::LockMode::Party,
            kept: false,
        };
        // 2% a day: 87% is 43 days.
        assert_eq!(upkeep_text(&w), "decaying: about 43 days left");
        w.health = 0.03;
        assert_eq!(upkeep_text(&w), "decaying: about 1 day left");
        w.health = 0.01;
        assert_eq!(upkeep_text(&w), "decaying: falls within a day");
        w.kept = true;
        assert_eq!(upkeep_text(&w), "kept by the plot's upkeep");
        let mut b = Building::default();
        b.pieces.insert(
            9,
            Piece {
                wire: w,
                entity: None,
                dirty: false,
                respawn: false,
            },
        );
        let line = repair_text(&b, 9);
        assert!(line.starts_with("Wood wall  1%") && line.ends_with("kept by the plot's upkeep"));
        assert!(repair_text(&b, 10).is_empty());
        // The wire's bit 7.
        assert_eq!(wire::PIECE_KEPT, 0x80);
        // No cap on pieces: the hint says how many, without a max.
        assert!(bar_text(&b, 0, &Keymap::defaults()).contains("Built 0"));
    }
}
