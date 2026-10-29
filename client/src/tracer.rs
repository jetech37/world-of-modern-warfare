//! Rounds in flight: every round (every pellet of a shotgun) leaves the gun's muzzle as a small
//! grey slug and flies along its own scattered path (as the server draws it) to the first wall or
//! the shot's range. Rounds fired at us, and sentries' rounds, fly the same way from their guns.
//! Other players' rounds come from the server (world op 34, every shot's first pellet, hit or
//! miss); theirs and the NPCs' are drawn brighter and thicker than our own, so fire coming our
//! way stands out.

use bevy::platform::collections::HashSet;
use bevy::prelude::*;

use benilla_app::fusion::{CHANNEL_WORLD, FusionChannelReceived};
use benilla_world::interact::WorldPick;

use crate::Mw2Set;
use crate::config::Loadout;
use crate::fusion::FiredRound;

/// How fast a round flies (yards a second: slower than a real one, to be seen), how long its
/// streak is, how thick.
pub const TRACER_SPEED: f32 = 380.0;
pub const TRACER_LENGTH: f32 = 0.8;
pub const TRACER_WIDTH: f32 = 0.022;
/// An enemy's round (another player's, an NPC's) is this thick.
pub const ENEMY_WIDTH: f32 = 0.045;

/// World op 34: another player's round (the server's `Fusion::kOpPlayerRound`).
pub const OP_PLAYER_ROUND: u8 = 34;

/// Another player's round as the server tells it (raw WoW yards, z up): from the shooter's eye
/// to the body it hit, the wall, or its range.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlayerRound {
    pub shooter: u64,
    pub gun: u16,
    pub hit: bool,
    pub from: [f32; 3],
    pub to: [f32; 3],
}

fn f32_at(b: &[u8], i: usize) -> f32 {
    f32::from_le_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]])
}

/// Op 34, 36 bytes: `[34, shooter u64, gun u16, hit u8, from x, y, z f32, to x, y, z f32]`.
pub fn decode_player_round(b: &[u8]) -> Option<PlayerRound> {
    (b.len() == 36 && b[0] == OP_PLAYER_ROUND).then(|| PlayerRound {
        shooter: u64::from_le_bytes(b[1..9].try_into().unwrap_or_default()),
        gun: u16::from_le_bytes([b[9], b[10]]),
        hit: b[11] != 0,
        from: [f32_at(b, 12), f32_at(b, 16), f32_at(b, 20)],
        to: [f32_at(b, 24), f32_at(b, 28), f32_at(b, 32)],
    })
}

/// Where a gun held at the eye has its muzzle, firing along `dir`: a little ahead, right of and
/// below the eye.
pub fn muzzle_estimate(eye: Vec3, dir: Vec3) -> Vec3 {
    let right = dir.cross(Vec3::Y).normalize_or_zero();
    eye + dir * 0.6 + right * 0.18 - Vec3::Y * 0.15
}

/// How far a streak from `start` toward `to` flies: all the way, unless it ends at (or within
/// `HIT_SHORT` of) our eye, when it stops `HIT_SHORT` short of it, so a round that hits us never
/// runs into the camera. A near miss that only passes by flies on whole.
pub fn streak_end(start: Vec3, to: Vec3, eye: Vec3) -> f32 {
    let seg = to - start;
    let len = seg.length();
    if len < 1e-4 {
        return 0.0;
    }
    if to.distance(eye) > HIT_SHORT {
        return len;
    }
    // The first point along the way HIT_SHORT from the eye.
    let dir = seg / len;
    let m = start - eye;
    let b = m.dot(dir);
    let c = m.length_squared() - HIT_SHORT * HIT_SHORT;
    if c <= 0.0 {
        return 0.0; // it starts that close: nothing to see
    }
    let disc = (b * b - c).max(0.0);
    (-b - disc.sqrt()).clamp(0.0, len)
}

/// Whether the server sends other players' rounds (op 34): their hit-only tracers stand down.
#[derive(Resource, Debug, Default)]
pub struct PlayerRounds {
    pub seen: bool,
}
/// How far a streak flies with nothing in the way.
pub const TRACER_RANGE: f32 = 150.0;

/// Whether a catalog weapon class fires rounds that fly (launchers, equipment and melee draw
/// their own).
pub fn fires_rounds(class: &str) -> bool {
    !matches!(
        class,
        "launcher" | "grenade" | "lethal" | "tactical" | "killstreak" | "melee" | "riot_shield"
    )
}

/// A creature's guid (high word 0xF130 / 0xF530 / 0xF140...), not a player's.
pub fn is_creature(guid: u64) -> bool {
    guid >> 48 != 0
}

/// Bevy space from WoW's (`bevy = (-y, z, -x)`, the inverse of `crate::fusion::to_wow`).
pub fn from_wow(w: [f32; 3]) -> Vec3 {
    Vec3::new(-w[1], w[2], -w[0])
}

/// A streak `travelled` yards out along `dir` from `start`, ending at `end` yards: its centre and
/// length (shorter at the start and as it reaches the end), `None` once past the end.
pub fn beam_at(start: Vec3, dir: Vec3, travelled: f32, end: f32) -> Option<(Vec3, f32)> {
    if travelled - TRACER_LENGTH >= end {
        return None;
    }
    let head = travelled.min(end);
    let tail = (travelled - TRACER_LENGTH).max(0.0);
    let length = (head - tail).max(0.0);
    Some((start + dir * ((head + tail) * 0.5), length))
}

/// Draw a streak (Bevy space): from `start` along `dir`, `end` yards long. For the layer's
/// other shooters (sentries, NPC gunners); `enemy` draws it in the enemy rounds' brighter look.
#[derive(Message, Clone, Copy, Debug)]
pub struct Streak {
    pub start: Vec3,
    pub dir: Vec3,
    pub end: f32,
    pub enemy: bool,
}

/// A streak in flight.
#[derive(Component)]
struct Tracer {
    start: Vec3,
    dir: Vec3,
    end: f32,
    born: f32,
}

/// The beam's mesh and material, made once.
#[derive(Resource)]
struct TracerAssets {
    mesh: Handle<Mesh>,
    material: Handle<StandardMaterial>,
    flash_mesh: Handle<Mesh>,
    flash: Handle<StandardMaterial>,
    /// An enemy's round: thicker and glowing.
    enemy_mesh: Handle<Mesh>,
    enemy: Handle<StandardMaterial>,
}

/// A muzzle flash: a flame card facing the eye for a few hundredths of a second.
#[derive(Component)]
struct Flash {
    born: f32,
}

/// How long a muzzle flash shows.
pub const FLASH_SECS: f32 = 0.06;

pub struct TracerPlugin;

impl Plugin for TracerPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<Streak>()
            .init_resource::<PlayerRounds>()
            .add_systems(Startup, setup)
            .add_systems(
                Update,
                (
                    tint,
                    spawn_tracers,
                    player_rounds,
                    incoming,
                    streaks,
                    fly,
                    fade_flashes,
                )
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

fn setup(
    mut commands: Commands,
    meshes: Option<ResMut<Assets<Mesh>>>,
    materials: Option<ResMut<Assets<StandardMaterial>>>,
    server: Res<AssetServer>,
) {
    let (Some(mut meshes), Some(mut materials)) = (meshes, materials) else {
        return;
    };
    commands.insert_resource(TracerAssets {
        // A unit beam along Z, scaled to the streak's length.
        mesh: meshes.add(Cuboid::new(TRACER_WIDTH, TRACER_WIDTH, 1.0)),
        material: materials.add(StandardMaterial {
            // A round's grey glint, half seen.
            base_color: GLINT,
            unlit: true,
            alpha_mode: AlphaMode::Blend,
            ..default()
        }),
        flash_mesh: meshes.add(Rectangle::new(0.55, 0.55)),
        flash: materials.add(StandardMaterial {
            base_color: Color::srgb(1.0, 0.8, 0.45),
            base_color_texture: Some(server.load("mpq://Particles/Flame02.blp")),
            unlit: true,
            alpha_mode: AlphaMode::Add,
            cull_mode: None,
            ..default()
        }),
        enemy_mesh: meshes.add(Cuboid::new(ENEMY_WIDTH, ENEMY_WIDTH, 1.0)),
        enemy: materials.add(StandardMaterial {
            base_color: ENEMY_GLOW,
            unlit: true,
            alpha_mode: AlphaMode::Add,
            ..default()
        }),
    });
}

/// The round's grey glint (its colour when no prestige tracer is worn).
const GLINT: Color = Color::srgba(0.78, 0.76, 0.7, 0.45);
/// An enemy round's hot glow.
const ENEMY_GLOW: Color = Color::srgba(1.0, 0.8, 0.45, 0.95);

/// A streak in flight from `start` along `dir`, `end` yards long, in the enemy look or our own.
fn spawn_streak(
    commands: &mut Commands,
    assets: &TracerAssets,
    name: &'static str,
    (start, dir, end): (Vec3, Vec3, f32),
    enemy: bool,
    t: f32,
) {
    let (mesh, material) = if enemy {
        (&assets.enemy_mesh, &assets.enemy)
    } else {
        (&assets.mesh, &assets.material)
    };
    commands.spawn((
        Name::new(name),
        Tracer {
            start,
            dir,
            end,
            born: t,
        },
        Mesh3d(mesh.clone()),
        MeshMaterial3d(material.clone()),
        Transform::from_translation(start).looking_to(dir, Vec3::Y),
    ));
}

/// The tracer colour worn (crate::prestige): the beam's material takes it, brighter.
fn tint(
    look: Option<Res<crate::prestige::TracerLook>>,
    assets: Option<Res<TracerAssets>>,
    materials: Option<ResMut<Assets<StandardMaterial>>>,
) {
    let (Some(look), Some(assets), Some(mut materials)) = (look, assets, materials) else {
        return;
    };
    if !look.is_changed() && !assets.is_changed() {
        return;
    }
    if let Some(m) = materials.get_mut(&assets.material) {
        m.base_color = match look.0 {
            Some([r, g, b]) => Color::srgba_u8(r, g, b, 200),
            None => GLINT,
        };
    }
}

/// A flash at `at`, turned to the eye.
fn spawn_flash(commands: &mut Commands, assets: &TracerAssets, at: Vec3, eye: Vec3, t: f32) {
    commands.spawn((
        Name::new("mw2 muzzle flash"),
        Flash { born: t },
        Mesh3d(assets.flash_mesh.clone()),
        MeshMaterial3d(assets.flash.clone()),
        Transform::from_translation(at).looking_at(eye, Vec3::Y),
    ));
}

fn fade_flashes(mut commands: Commands, time: Res<Time>, flashes: Query<(Entity, &Flash)>) {
    let t = time.elapsed_secs();
    for (e, f) in &flashes {
        if t - f.born > FLASH_SECS {
            commands.entity(e).despawn();
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn spawn_tracers(
    mut commands: Commands,
    time: Res<Time>,
    assets: Option<Res<TracerAssets>>,
    loadout: Res<Loadout>,
    weapon: Res<crate::weapon::Weapon>,
    catalog: Res<crate::catalog::Catalog>,
    world: WorldPick,
    viewmodels: Query<&crate::mw2::viewmodel::Viewmodel>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
    mut rounds: MessageReader<FiredRound>,
    enchanted: Res<crate::rounds::RoundTracer>,
) {
    let Some(assets) = assets else {
        rounds.clear();
        return;
    };
    // Enchanted rounds in the gun (crate::rounds) colour its tracers.
    let material = enchanted
        .0
        .clone()
        .unwrap_or_else(|| assets.material.clone());
    let gun = catalog.gun_named(&loadout.mw2_weapon);
    if gun.is_some_and(|g| !fires_rounds(&g.class)) {
        rounds.clear();
        return;
    }
    let pellets = gun.map_or(1, |g| g.pellets.max(1));
    let camera = cameras.iter().next().copied();
    // The muzzle of the gun that fired (akimbo: the hand the shot came from).
    let muzzle = camera.and_then(|cam| {
        viewmodels
            .iter()
            .find(|vm| {
                let (hand, akimbo) = vm.hand();
                !akimbo || hand == weapon.shot_hand
            })
            .and_then(|vm| vm.muzzle_eye())
            .map(|eye| cam.transform_point(eye))
    });
    for r in rounds.read() {
        let aim = crate::fusion::to_wow(*r.ray.direction);
        for i in 0..pellets {
            // Each pellet's own direction, scattered as the server draws it.
            let dir = from_wow(benilla_app::fusion::pellet_direction(
                aim,
                r.seed,
                i,
                r.spread_deg,
            ))
            .normalize_or_zero();
            if dir == Vec3::ZERO {
                continue;
            }
            // Where it lands: the first thing on the round's path from the eye, else its range.
            let reach = world
                .cast(
                    Ray3d::new(r.ray.origin, Dir3::new(dir).unwrap_or(Dir3::NEG_Z)),
                    &HashSet::new(),
                    false,
                )
                .into_iter()
                .map(|h| h.hit.distance)
                .fold(TRACER_RANGE, f32::min);
            let lands = r.ray.origin + dir * reach;
            // From the muzzle to there (a little below and right of the eye without a gun).
            let start = muzzle.unwrap_or_else(|| muzzle_estimate(r.ray.origin, dir));
            let to = lands - start;
            let (len, fly) = (to.length(), to.normalize_or_zero());
            if len < 0.5 || fly == Vec3::ZERO {
                continue;
            }
            commands.spawn((
                Name::new("mw2 round"),
                Tracer {
                    start,
                    dir: fly,
                    end: len,
                    born: time.elapsed_secs(),
                },
                Mesh3d(assets.mesh.clone()),
                MeshMaterial3d(material.clone()),
                Transform::from_translation(start).looking_to(fly, Vec3::Y),
            ));
        }
    }
}

/// Where a round fired at us flies: from the shooter's chest at our eye, ending short of it on a
/// hit; on a miss it passes `MISS_BY` yards to one side (the side picked by `side`) and flies on.
/// Returns the start, direction and length.
pub fn incoming_path(shooter: Vec3, eye: Vec3, hit: bool, side: f32) -> Option<(Vec3, Vec3, f32)> {
    let start = shooter + Vec3::Y * 1.3;
    let to_eye = eye - start;
    let dist = to_eye.length();
    if dist < 1.0 {
        return None;
    }
    if hit {
        let dir = to_eye / dist;
        return Some((start, dir, (dist - HIT_SHORT).max(0.0)));
    }
    let across = to_eye.cross(Vec3::Y).normalize_or_zero();
    let past = eye + across * MISS_BY * side.signum() - Vec3::Y * 0.3;
    let dir = (past - start).normalize_or_zero();
    Some((start, dir, TRACER_RANGE.min(dist * 2.0)))
}

/// A round that hits us stops this far short of the eye, so its streak never fills the view.
pub const HIT_SHORT: f32 = 2.5;
/// How far a missed round passes us by (yards).
pub const MISS_BY: f32 = 1.5;
/// Gunfire fades out over this distance (yards).
pub const HEARING: f32 = 80.0;

/// Rounds fired at us: a streak from the shooter and the gun's report, quieter the farther off.
#[allow(clippy::too_many_arguments)]
fn incoming(
    mut commands: Commands,
    time: Res<Time>,
    assets: Option<Res<TracerAssets>>,
    catalog: Res<crate::catalog::Catalog>,
    mw2: Res<crate::mw2::Mw2Assets>,
    objects: benilla_app::Objects,
    bodies: Query<&GlobalTransform>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
    mut fire: MessageReader<crate::fusion::IncomingFire>,
    mut sfx: MessageWriter<crate::mw2::sound::Sfx>,
    mut count: Local<u32>,
    npc_guns: Res<crate::npc_guns::NpcGuns>,
    player_rounds: Res<PlayerRounds>,
) {
    let Some(eye) = cameras.iter().next().map(GlobalTransform::translation) else {
        fire.clear();
        return;
    };
    for f in fire.read() {
        // A server that sends every NPC round (crate::npc_guns), or every player's (op 34,
        // `player_rounds`), draws and sounds them there.
        if (npc_guns.fire_seen && is_creature(f.guid))
            || (player_rounds.seen && !is_creature(f.guid))
        {
            continue;
        }
        let Some(shooter) = objects
            .entity(f.guid)
            .and_then(|e| bodies.get(e).ok())
            .map(GlobalTransform::translation)
        else {
            continue;
        };
        let gun = catalog.gun(f.gun);
        if let crate::mw2::Mw2Assets::Ready(lib) = &*mw2
            && let Some(alias) = gun
                .and_then(|g| lib.weapons.get(&g.name))
                .and_then(|w| w.sounds.fire_npc.clone())
        {
            let gain = (1.0 - shooter.distance(eye) / HEARING).clamp(0.15, 1.0) * 0.8;
            sfx.write(crate::mw2::sound::Sfx::at(alias, gain));
        }
        *count += 1;
        let Some(assets) = &assets else {
            continue;
        };
        let side = if count.is_multiple_of(2) { 1.0 } else { -1.0 };
        if let Some((start, dir, end)) = incoming_path(shooter, eye, f.hit, side) {
            spawn_flash(
                &mut commands,
                assets,
                start + dir * 0.5,
                eye,
                time.elapsed_secs(),
            );
            spawn_streak(
                &mut commands,
                assets,
                "mw2 incoming tracer",
                (start, dir, end),
                true,
                time.elapsed_secs(),
            );
        }
    }
}

/// Other players' rounds (world op 34): the muzzle flash, the round's flight from the gun at
/// their eye to where it ended (short of our eye if it hit us), and the gun's report, quieter
/// the farther off.
#[allow(clippy::too_many_arguments)]
fn player_rounds(
    mut commands: Commands,
    time: Res<Time>,
    assets: Option<Res<TracerAssets>>,
    catalog: Res<crate::catalog::Catalog>,
    mw2: Res<crate::mw2::Mw2Assets>,
    state: Res<crate::mode::FpsState>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut seen: ResMut<PlayerRounds>,
    mut sfx: MessageWriter<crate::mw2::sound::Sfx>,
) {
    let eye = cameras.iter().next().map(GlobalTransform::translation);
    let t = time.elapsed_secs();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        let Some(r) = decode_player_round(&m.body) else {
            continue;
        };
        seen.seen = true;
        // Our own are drawn from our gun (the server leaves us out anyway).
        if state.self_guid == Some(r.shooter) {
            continue;
        }
        let (from, to) = (from_wow(r.from), from_wow(r.to));
        let Ok(dir) = Dir3::new(to - from) else {
            continue;
        };
        let start = muzzle_estimate(from, *dir);
        let Some(eye) = eye else {
            continue;
        };
        if let crate::mw2::Mw2Assets::Ready(lib) = &*mw2
            && let Some(alias) = catalog
                .gun(r.gun)
                .and_then(|g| lib.weapons.get(&g.name))
                .and_then(|w| w.sounds.fire_npc.clone())
        {
            let gain = crate::npc_guns::report_gain(eye.distance(start)) * 0.8;
            if gain > 0.01 {
                sfx.write(crate::mw2::sound::Sfx::at(alias, gain));
            }
        }
        let Some(assets) = &assets else {
            continue;
        };
        let end = streak_end(start, to, eye);
        let Ok(fly) = Dir3::new(to - start) else {
            continue;
        };
        spawn_flash(&mut commands, assets, start, eye, t);
        if end > 0.5 {
            spawn_streak(
                &mut commands,
                assets,
                "mw2 player round",
                (start, *fly, end),
                true,
                t,
            );
        }
    }
}

fn streaks(
    mut commands: Commands,
    time: Res<Time>,
    assets: Option<Res<TracerAssets>>,
    mut streaks: MessageReader<Streak>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
) {
    let eye = cameras.iter().next().map(GlobalTransform::translation);
    let Some(assets) = assets else {
        streaks.clear();
        return;
    };
    for s in streaks.read() {
        if let Some(eye) = eye {
            spawn_flash(&mut commands, &assets, s.start, eye, time.elapsed_secs());
        }
        spawn_streak(
            &mut commands,
            &assets,
            "mw2 streak",
            (s.start, s.dir, s.end),
            s.enemy,
            time.elapsed_secs(),
        );
    }
}

fn fly(
    mut commands: Commands,
    time: Res<Time>,
    mut tracers: Query<(Entity, &Tracer, &mut Transform)>,
) {
    let t = time.elapsed_secs();
    for (e, tr, mut tf) in &mut tracers {
        match beam_at(tr.start, tr.dir, (t - tr.born) * TRACER_SPEED, tr.end) {
            Some((centre, length)) => {
                *tf = Transform::from_translation(centre)
                    .looking_to(tr.dir, Vec3::Y)
                    .with_scale(Vec3::new(1.0, 1.0, length.max(0.01)));
            }
            None => commands.entity(e).despawn(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn guns_fire_rounds_launchers_do_not() {
        assert!(fires_rounds("assault_rifle"));
        assert!(fires_rounds("shotgun"));
        assert!(fires_rounds("sniper"));
        assert!(!fires_rounds("launcher"));
        assert!(!fires_rounds("lethal"));
    }

    #[test]
    fn rounds_fired_at_us_hit_short_of_the_eye_or_pass_by() {
        let shooter = Vec3::new(0.0, 0.0, -20.0);
        let eye = Vec3::new(0.0, 1.6, 0.0);
        let (start, dir, end) = incoming_path(shooter, eye, true, 1.0).unwrap();
        let tip = start + dir * end;
        assert!((tip.distance(eye) - HIT_SHORT).abs() < 1e-3);
        let (start, dir, _) = incoming_path(shooter, eye, false, 1.0).unwrap();
        // The miss's closest pass is about MISS_BY off the eye.
        let along = (eye - start).dot(dir);
        let closest = (start + dir * along).distance(eye);
        assert!(closest > 1.0 && closest < 2.0, "{closest}");
        assert!(incoming_path(eye - Vec3::Y * 1.3, eye, true, 1.0).is_none());
    }

    #[test]
    fn wow_axes_round_trip() {
        let v = Vec3::new(1.5, -2.0, 3.25);
        assert_eq!(from_wow(crate::fusion::to_wow(v)), v);
    }

    #[test]
    fn a_streak_grows_flies_and_stops_at_the_wall() {
        let dir = Vec3::NEG_Z;
        let leaving = TRACER_LENGTH / 2.0;
        let (c, len) = beam_at(Vec3::ZERO, dir, leaving, 50.0).unwrap();
        assert!((len - leaving).abs() < 1e-5 && (c.z + leaving / 2.0).abs() < 1e-5); // still leaving the muzzle
        let (c, len) = beam_at(Vec3::ZERO, dir, 20.0, 50.0).unwrap();
        assert!((len - TRACER_LENGTH).abs() < 1e-5);
        assert!((c.z + (20.0 - TRACER_LENGTH / 2.0)).abs() < 1e-4);
        let (_, len) = beam_at(Vec3::ZERO, dir, 50.0 + TRACER_LENGTH / 2.0, 50.0).unwrap(); // its head at the wall
        assert!(len < TRACER_LENGTH);
        assert!(beam_at(Vec3::ZERO, dir, 50.0 + TRACER_LENGTH, 50.0).is_none());
    }

    #[test]
    fn the_tracer_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((spawn_tracers, player_rounds, incoming, streaks, fly));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }

    #[test]
    fn a_players_round_decodes_as_the_server_writes_it() {
        let mut b = vec![OP_PLAYER_ROUND];
        b.extend_from_slice(&0x1122_3344_5566_7788u64.to_le_bytes());
        b.extend_from_slice(&0x0203u16.to_le_bytes());
        b.push(1);
        for v in [1.0f32, 2.0, 3.0, 4.0, -5.0, 6.5] {
            b.extend_from_slice(&v.to_le_bytes());
        }
        assert_eq!(
            decode_player_round(&b),
            Some(PlayerRound {
                shooter: 0x1122_3344_5566_7788,
                gun: 0x0203,
                hit: true,
                from: [1.0, 2.0, 3.0],
                to: [4.0, -5.0, 6.5],
            })
        );
        assert_eq!(decode_player_round(&b[..35]), None);
        b[0] = 32;
        assert_eq!(decode_player_round(&b), None);
    }

    #[test]
    fn the_muzzle_is_ahead_right_and_below_the_eye() {
        let eye = Vec3::new(0.0, 1.6, 0.0);
        let m = muzzle_estimate(eye, Vec3::NEG_Z);
        assert!((m - eye).dot(Vec3::NEG_Z) > 0.5);
        assert!(m.y < eye.y);
        // Facing -Z, right is +X.
        assert!(m.x > 0.1);
    }

    #[test]
    fn a_streak_stops_short_of_our_eye_only_when_it_ends_there() {
        let eye = Vec3::new(0.0, 1.6, 0.0);
        let start = Vec3::new(0.0, 1.6, -30.0);
        // A round into our face stops HIT_SHORT short of it.
        let end = streak_end(start, eye, eye);
        assert!((end - (30.0 - HIT_SHORT)).abs() < 1e-3, "{end}");
        // Into our chest, just under the eye: still short of the eye.
        let chest = eye - Vec3::Y * 0.4;
        let end = streak_end(start, chest, eye);
        let dir = (chest - start).normalize();
        assert!(((start + dir * end).distance(eye) - HIT_SHORT).abs() < 1e-3);
        // Past us by two yards and on to a wall: all the way.
        let wall = Vec3::new(2.0, 1.6, 20.0);
        assert!((streak_end(start, wall, eye) - start.distance(wall)).abs() < 1e-4);
        // Nothing to draw from inside the eye's reach, or no length.
        assert_eq!(streak_end(eye + Vec3::X, eye, eye), 0.0);
        assert_eq!(streak_end(start, start, eye), 0.0);
    }
}
