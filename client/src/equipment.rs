//! MW2's equipment and killstreak call-ins with an Azeroth Warfare server: G throws (or plants)
//! the class's lethal, Q its tactical, and 4 calls in the latest earned killstreak where the
//! crosshair points. Each goes up as a shot of its own catalog id (crate::fusion::FiredRound); the
//! server throws, plants and sets them off (FusionMgr::UseEquipment, CallIn). Here they are drawn:
//! the grenade's arc to where it lands (the server's own arc, `ThrowArc`), a flash where it goes
//! off, a claymore or C4 lying where it was put, a missile falling on the predator's mark.
//!
//! The lethal and tactical are readied in a hand from their slots first (crate::action_systems
//! `grenade`); that hand's mouse button is then the control read here as the lethal or tactical.
//!
//! A frag is held to cook (the button down pulls the pin; after the pull every moment held comes off its
//! fuse, sent up in the throw's seed) and thrown on release; held its whole fuse it leaves the
//! hand on its own and goes off at once. Frags bounce and roll on the server, which tells everyone
//! near (us too) the path (world op 35, crate::grenade): every frag, ours, other players' and NPC
//! gunners', is drawn from that.

use bevy::platform::collections::HashSet;
use bevy::prelude::*;

use benilla_app::Objects;
use benilla_world::interact::WorldPick;

use crate::Mw2Set;
use crate::aim::Aim;
use crate::cac::Classes;
use crate::catalog::Catalog;
use crate::fusion::{FiredRound, Fusion};
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::mw2::sound::Sfx;
use crate::streaks::Streak;

/// The server's throw (`Fusion::kThrowSpeed`, `kWowGravity`, `kMaxFlightSeconds`).
pub const THROW_SPEED: f32 = 22.0;
pub const GRAVITY: f32 = 19.2911;
pub const MAX_FLIGHT: f32 = 4.0;

/// How long after a throw a grenade goes off, as the server times it (`FuseSeconds`): from the
/// throw for a frag, from the landing for the rest; `None` for C4 and the claymore.
pub fn fuse(name: &str) -> Option<(f32, bool)> {
    match name {
        "frag_grenade_mp" => Some((3.5, false)),
        "semtex_mp" => Some((2.5, true)),
        "flash_grenade_mp" | "concussion_grenade_mp" => Some((1.5, true)),
        "smoke_grenade_mp" => Some((1.0, true)),
        _ => None,
    }
}

/// Where a throw from `origin` along `dir` comes down: its points every 0.02 s until `hit` says a
/// segment met the world (the point and the seconds flown), or the flight runs out.
pub fn arc(
    origin: Vec3,
    dir: Vec3,
    mut hit: impl FnMut(Vec3, Vec3) -> Option<Vec3>,
) -> (Vec<Vec3>, f32, bool) {
    let step = 0.02;
    let mut p = origin;
    let mut v = dir.normalize_or_zero() * THROW_SPEED;
    let mut points = vec![p];
    let mut t = 0.0;
    while t < MAX_FLIGHT {
        let next = p + v * step - Vec3::Y * (0.5 * GRAVITY * step * step);
        if let Some(at) = hit(p, next) {
            points.push(at);
            return (points, t + step, true);
        }
        p = next;
        v.y -= GRAVITY * step;
        t += step;
        points.push(p);
    }
    (points, MAX_FLIGHT, false)
}

/// The killstreak catalog names (the server's `KILLSTREAKS`) by the HUD's reward names.
pub fn streak_item(reward: &str) -> Option<&'static str> {
    Some(match reward {
        "UAV" => "uav_mp",
        "SENTRY GUN" => "sentry_gun_mp",
        "PREDATOR MISSILE" => "remotemissile_projectile_mp",
        "HARRIER STRIKE" => "harrier_ffar_mp",
        "ATTACK HELICOPTER" => "cobra_player_minigun_mp",
        "STEALTH BOMBER" => "stealth_bomb_mp",
        "CHOPPER GUNNER" => "cobra_20mm_mp",
        "TACTICAL NUKE" => "nuke_mp",
        _ => return None,
    })
}

/// What's left of a life's equipment, and C4 out waiting for its trigger.
#[derive(Resource, Debug, Default)]
pub struct EquipState {
    pub lethal_left: u32,
    pub tactical_left: u32,
    pub c4_out: bool,
    /// The class's equipment the counts are for, and whether we were alive (a respawn refills).
    pub(crate) for_class: Option<(u16, u16)>,
    was_alive: bool,
    /// The class has a lethal: G throws it rather than casting the WoW class's lethal spell.
    pub has_lethal: bool,
    /// A frag in hand, its pin pulled at this time (crate::grenade cooking).
    pub cooking: Option<f32>,
    /// Grenades thrown this session, and when the last left the hand: the first-person throw
    /// (crate::mw2::viewmodel `grenade_clip`) plays from it.
    pub throws: u32,
    pub thrown_at: Option<f32>,
    /// The last throw's motion is still playing: the grenade's hand stays up until it ends
    /// (crate::action_systems `grenade_controls`), then the gun comes back.
    pub throwing: bool,
}

/// Seconds the throw holds the hand up before the gun may come back.
pub const THROW_HOLD_SECS: f32 = 0.75;

impl EquipState {
    fn threw(&mut self, t: f32) {
        self.throws = self.throws.wrapping_add(1);
        self.thrown_at = Some(t);
        self.throwing = true;
    }
}

/// The frag's catalog name.
pub const FRAG: &str = "frag_grenade_mp";

/// A grenade in the air or on the ground, until it goes off.
#[derive(Component)]
struct Thrown {
    points: Vec<Vec3>,
    born: f32,
    goes_off: Option<f32>,
    smoke: bool,
    /// A flash or stun grenade: it bursts white rather than exploding.
    tactical: bool,
}

/// A grenade or charge wears its MW2 model once the install has it (the ball until then).
#[derive(Component)]
struct Dressed;

/// A drifting card of a smoke grenade's cloud, facing the camera.
#[derive(Component)]
struct SmokeCard {
    born: f32,
    secs: f32,
    drift: Vec3,
    size: f32,
}

/// Seconds a smoke grenade's cloud hangs (MW2's smoke lasts about this long).
const SMOKE_SECS: f32 = 18.0;
/// The cloud's reach (yards): the server's `kSmokeRadius`.
const SMOKE_RADIUS: f32 = 6.0;

/// A claymore or C4 lying where it was put, until it goes off.
#[derive(Component)]
struct Planted {
    weapon_id: u16,
    c4: bool,
}

/// A flash where something went off, growing and fading.
#[derive(Component)]
struct Burst {
    born: f32,
    size: f32,
    secs: f32,
}

/// A missile falling on its mark.
#[derive(Component)]
struct Falling {
    from: Vec3,
    to: Vec3,
    born: f32,
    secs: f32,
    size: f32,
}

/// The meshes and materials, made once.
#[derive(Resource)]
struct Looks {
    card: Handle<Mesh>,
    smoke_card: Handle<StandardMaterial>,
    ball: Handle<Mesh>,
    grenade: Handle<StandardMaterial>,
    flash: Handle<StandardMaterial>,
    smoke: Handle<StandardMaterial>,
    charge: Handle<StandardMaterial>,
}

/// A hit the server reported, with the weapon that made it (claymores and C4 go off server-side).
#[derive(Message, Debug, Clone, Copy)]
pub struct WeaponHit(pub u16);

pub struct EquipmentPlugin;

impl Plugin for EquipmentPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<EquipState>()
            .add_message::<WeaponHit>()
            .add_systems(Startup, setup)
            .add_systems(
                Update,
                (
                    refill,
                    use_equipment,
                    call_in,
                    frag_paths,
                    fly,
                    planted_go_off,
                    bursts,
                )
                    .chain()
                    .in_set(Mw2Set::Weapon),
            )
            .add_systems(
                Update,
                (dress, smoke_cards, tactical_hit, class_smoke).in_set(Mw2Set::Hud),
            );
    }
}

fn setup(
    mut commands: Commands,
    assets: Res<AssetServer>,
    meshes: Option<ResMut<Assets<Mesh>>>,
    materials: Option<ResMut<Assets<StandardMaterial>>>,
) {
    let (Some(mut meshes), Some(mut materials)) = (meshes, materials) else {
        return;
    };
    let lit = |c: Color| StandardMaterial {
        base_color: c,
        unlit: true,
        ..default()
    };
    let cloud: Handle<Image> = assets.load("mpq://Spells/ToonCloud64_Soft.blp");
    commands.insert_resource(Looks {
        card: meshes.add(Rectangle::new(1.0, 1.0)),
        smoke_card: materials.add(StandardMaterial {
            base_color: Color::srgba(0.82, 0.82, 0.84, 0.7),
            base_color_texture: Some(cloud),
            unlit: true,
            alpha_mode: AlphaMode::Blend,
            cull_mode: None,
            ..default()
        }),
        ball: meshes.add(Sphere::new(1.0)),
        grenade: materials.add(lit(Color::srgb(0.2, 0.25, 0.15))),
        flash: materials.add(StandardMaterial {
            base_color: Color::srgba(1.0, 0.75, 0.35, 0.8),
            emissive: LinearRgba::rgb(6.0, 3.5, 1.2),
            unlit: true,
            alpha_mode: AlphaMode::Add,
            ..default()
        }),
        smoke: materials.add(StandardMaterial {
            base_color: Color::srgba(0.8, 0.8, 0.82, 0.55),
            unlit: true,
            alpha_mode: AlphaMode::Blend,
            ..default()
        }),
        charge: materials.add(lit(Color::srgb(0.35, 0.33, 0.2))),
    });
}

/// A fresh life (or a new class) brings the class's equipment back.
fn refill(
    classes: Res<Classes>,
    catalog: Res<Catalog>,
    state: Res<FpsState>,
    objects: Objects,
    mut equip: ResMut<EquipState>,
) {
    let class = classes.equipped_class();
    let ids = class.map(|c| (c.lethal, c.tactical));
    let alive = state.self_guid.is_some_and(|g| {
        objects
            .iter()
            .find(|(guid, ..)| *guid == g)
            .is_some_and(|(_, _, f)| f.unit_health().unwrap_or(0) > 0)
    });
    if ids != equip.for_class || (alive && !equip.was_alive) {
        let count = |id: u16| catalog.gun(id).map_or(0, |g| g.mag_size.max(1));
        let (lethal, tactical) = ids.unwrap_or((0, 0));
        equip.lethal_left = count(lethal);
        equip.tactical_left = count(tactical);
        equip.c4_out = false;
        equip.for_class = ids;
    }
    equip.has_lethal = ids.is_some_and(|(l, _)| l != 0);
    equip.was_alive = alive;
}

#[allow(clippy::too_many_arguments)]
fn use_equipment(
    mut commands: Commands,
    time: Res<Time>,
    state: Res<FpsState>,
    fusion: Res<Fusion>,
    input: Res<Mw2Input>,
    aim: Res<Aim>,
    classes: Res<Classes>,
    catalog: Res<Catalog>,
    looks: Option<Res<Looks>>,
    world: WorldPick,
    mut equip: ResMut<EquipState>,
    mut rounds: MessageWriter<FiredRound>,
) {
    let now = time.elapsed_secs();
    equip.throwing = equip
        .thrown_at
        .is_some_and(|at| now - at < THROW_HOLD_SECS);
    if !fusion.enabled || !state.active {
        // Put away mid-cook: the pin goes back in.
        equip.cooking = None;
        return;
    }
    let (Some(class), Some(ray)) = (classes.equipped_class(), aim.ray) else {
        equip.cooking = None;
        return;
    };
    let t = time.elapsed_secs();
    for (control, id, lethal) in [
        (input.lethal, class.lethal, true),
        (input.tactical, class.tactical, false),
    ] {
        // A frag: held to cook, thrown on release (or once cooked through).
        if let Some(gun) = catalog.gun(id).filter(|g| g.name == FRAG) {
            let count = if lethal {
                equip.lethal_left
            } else {
                equip.tactical_left
            };
            if control.pressed && equip.cooking.is_none() && count > 0 {
                equip.cooking = Some(t);
            }
            let Some(start) = equip.cooking else {
                continue;
            };
            let held = t - start;
            if control.held && !crate::grenade::cooked_through(held) {
                continue;
            }
            equip.cooking = None;
            let left = if lethal {
                &mut equip.lethal_left
            } else {
                &mut equip.tactical_left
            };
            *left = left.saturating_sub(1);
            equip.threw(t);
            rounds.write(FiredRound {
                ray,
                target: None,
                ads: false,
                // The cook (ms) rides in the seed: the server takes it off the fuse.
                seed: crate::grenade::cook_ms(held),
                spread_deg: 0.0,
                item: Some(gun.name.clone()),
                detonate: false,
                left: false,
            });
            // Drawn when the server sends its path (`frag_paths`).
            continue;
        }
        let Some(gun) = catalog.gun(id).filter(|_| control.pressed) else {
            continue;
        };
        let name = gun.name.clone();
        // C4 out: the same key sets it off.
        if lethal && name == "c4_mp" && equip.c4_out {
            rounds.write(FiredRound {
                ray,
                target: None,
                ads: false,
                seed: 0,
                spread_deg: 0.0,
                item: Some(name),
                detonate: true,
                left: false,
            });
            equip.c4_out = false;
            continue;
        }
        let left = if lethal {
            &mut equip.lethal_left
        } else {
            &mut equip.tactical_left
        };
        if *left == 0 {
            continue;
        }
        *left -= 1;
        equip.threw(t);
        rounds.write(FiredRound {
            ray,
            target: None,
            ads: false,
            seed: 0,
            spread_deg: 0.0,
            item: Some(name.clone()),
            detonate: false,
            left: false,
        });
        let Some(looks) = &looks else {
            continue;
        };
        let dir = *ray.direction;
        if name == "claymore_mp" {
            // Planted a yard ahead of the feet.
            let flat = Vec3::new(dir.x, 0.0, dir.z).normalize_or_zero();
            let feet = ray.origin - Vec3::Y * 1.6 + flat;
            commands.spawn((
                Name::new("mw2 claymore"),
                Planted {
                    weapon_id: gun.id,
                    c4: false,
                },
                Mesh3d(looks.ball.clone()),
                MeshMaterial3d(looks.charge.clone()),
                Transform::from_translation(feet).with_scale(Vec3::new(0.25, 0.15, 0.1)),
            ));
            continue;
        }
        if name == "throwingknife_mp" {
            continue; // too quick to see
        }
        let (points, flight, _landed) = arc(ray.origin + dir * 0.5, dir, |a, b| {
            let seg = b - a;
            let len = seg.length();
            let d = Dir3::new(seg).ok()?;
            world
                .cast(Ray3d::new(a, d), &HashSet::new(), false)
                .into_iter()
                .map(|h| h.hit.distance)
                .filter(|&x| x <= len)
                .fold(None, |m: Option<f32>, x| Some(m.map_or(x, |m| m.min(x))))
                .map(|x| a + d * x)
        });
        let goes_off = fuse(&name).map(|(secs, from_landing)| {
            t + if from_landing {
                flight + secs
            } else {
                secs.max(flight)
            }
        });
        if name == "c4_mp" {
            equip.c4_out = true;
        }
        let last = *points.last().unwrap_or(&ray.origin);
        commands.spawn((
            Name::new("mw2 thrown"),
            Thrown {
                points,
                born: t,
                goes_off,
                smoke: name == "smoke_grenade_mp",
                tactical: matches!(name.as_str(), "flash_grenade_mp" | "concussion_grenade_mp"),
            },
            Planted {
                weapon_id: gun.id,
                c4: name == "c4_mp",
            },
            Mesh3d(looks.ball.clone()),
            MeshMaterial3d(looks.grenade.clone()),
            Transform::from_translation(last).with_scale(Vec3::splat(0.12)),
        ));
    }
}

/// 4 calls in the latest earned killstreak where the crosshair points.
#[allow(clippy::too_many_arguments)]
fn call_in(
    mut commands: Commands,
    time: Res<Time>,
    state: Res<FpsState>,
    input: Res<Mw2Input>,
    aim: Res<Aim>,
    looks: Option<Res<Looks>>,
    mut streak: ResMut<Streak>,
    mut rounds: MessageWriter<FiredRound>,
    mut sfx: MessageWriter<Sfx>,
    fusion: Res<Fusion>,
    mut control: ResMut<crate::lore_streaks::StreakControl>,
) {
    // One at a time: not while a turret is being set down or the rocket flown.
    if !state.active || !input.killstreak.pressed || state.placing || state.piloting {
        return;
    }
    let Some(reward) = streak.ready.pop() else {
        return;
    };
    // The turret is set down where the player then points (crate::lore_streaks); the rocket is
    // flown once the server sends it up (world op 37).
    if fusion.enabled && reward == "SENTRY GUN" {
        control.place(reward);
        sfx.write(Sfx::new("mp_killstreak_radar"));
        return;
    }
    let t = time.elapsed_secs();
    let Some(ray) = aim.ray else {
        return;
    };
    if let Some(item) = streak_item(reward) {
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
    streak.call_in(reward, t);
    sfx.write(Sfx::new("mp_killstreak_radar"));
    // Without a server, the strikes the client can show: the predator's fall, the harrier's
    // bombs (a server flies its own craft and sends the blasts).
    let Some(looks) = looks.filter(|_| !fusion.enabled) else {
        return;
    };
    let mark = ray.origin + *ray.direction * aim.world_distance.unwrap_or(150.0).min(200.0);
    let fall = |commands: &mut Commands, to: Vec3, delay: f32, size: f32| {
        commands.spawn((
            Name::new("mw2 strike"),
            Falling {
                from: to + Vec3::Y * 120.0,
                to,
                born: t + delay,
                secs: 2.0,
                size,
            },
            Mesh3d(looks.ball.clone()),
            MeshMaterial3d(looks.flash.clone()),
            Transform::from_translation(to + Vec3::Y * 120.0).with_scale(Vec3::splat(0.4)),
        ));
    };
    match reward {
        "PREDATOR MISSILE" => fall(&mut commands, mark, 0.0, 10.0),
        "HARRIER STRIKE" => {
            let along = Vec3::new(ray.direction.x, 0.0, ray.direction.z).normalize_or_zero();
            for (i, k) in [-1.0f32, 0.0, 1.0].into_iter().enumerate() {
                fall(&mut commands, mark + along * (6.0 * k), 0.3 * i as f32, 8.0);
            }
        }
        _ => {}
    }
}

/// A frag's path from the server (world op 35, crate::grenade): the frag flies it, bouncing and
/// rolling, and goes off at its fuse. Every frag near us comes this way, our own too.
fn frag_paths(
    mut commands: Commands,
    time: Res<Time>,
    looks: Option<Res<Looks>>,
    mut msgs: MessageReader<benilla_app::fusion::FusionChannelReceived>,
) {
    let Some(looks) = looks else {
        msgs.clear();
        return;
    };
    let t = time.elapsed_secs();
    for benilla_app::fusion::FusionChannelReceived(m) in msgs.read() {
        if m.channel != benilla_app::fusion::CHANNEL_WORLD {
            continue;
        }
        let Some(path) = crate::grenade::decode_path(&m.body) else {
            continue;
        };
        // Sampled at the 0.02 s steps `fly` walks.
        let points = path.points(20.0);
        let first = points.first().copied().unwrap_or_default();
        commands.spawn((
            Name::new("mw2 frag"),
            Thrown {
                points,
                born: t,
                goes_off: Some(t + f32::from(path.fuse_ms) / 1000.0),
                smoke: false,
                tactical: false,
            },
            Planted {
                weapon_id: path.weapon,
                c4: false,
            },
            Mesh3d(looks.ball.clone()),
            MeshMaterial3d(looks.grenade.clone()),
            Transform::from_translation(first).with_scale(Vec3::splat(0.12)),
        ));
    }
}

#[allow(clippy::type_complexity)]
fn fly(
    mut commands: Commands,
    time: Res<Time>,
    looks: Option<Res<Looks>>,
    mut thrown: Query<(Entity, &Thrown, &mut Transform), Without<Falling>>,
    mut falling: Query<(Entity, &Falling, &mut Transform), Without<Thrown>>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
    mut sfx: MessageWriter<Sfx>,
) {
    let t = time.elapsed_secs();
    let Some(looks) = looks else {
        return;
    };
    let eye = cameras.iter().next().map(GlobalTransform::translation);
    // A report heard by how far off it is (a frag across the field is quieter than ours).
    let gain = |at: Vec3| eye.map_or(1.0, |e| crate::npc_guns::report_gain(e.distance(at)));
    for (e, th, mut tf) in &mut thrown {
        // Along its arc, then where it came to rest.
        let k = ((t - th.born) / 0.02) as usize;
        if let Some(p) = th.points.get(k.min(th.points.len().saturating_sub(1))) {
            tf.translation = *p;
        }
        if let Some(at) = th.goes_off
            && t >= at
        {
            commands.entity(e).despawn();
            if th.smoke {
                // A cloud that spreads, drifts up and thins over a long while.
                smoke_cloud(&mut commands, &looks, tf.translation, t);
            } else if th.tactical {
                // A flash or stun grenade: a white burst, no fire.
                burst(&mut commands, &looks, tf.translation, 5.0, false, t);
                sfx.write(Sfx::at("flashbang_explode_default", gain(tf.translation)));
            } else {
                // The server's explosion draws the fire and smoke (crate::blast).
                let g = gain(tf.translation);
                if g > 0.01 {
                    sfx.write(Sfx::at("grenade_explode_default", g));
                }
            }
        }
    }
    for (e, f, mut tf) in &mut falling {
        let k = ((t - f.born) / f.secs).clamp(0.0, 1.0);
        tf.translation = f.from.lerp(f.to, k * k);
        if k >= 1.0 {
            commands.entity(e).despawn();
            burst(&mut commands, &looks, f.to, f.size, false, t);
            sfx.write(Sfx::new("grenade_explode_default"));
        }
    }
}

/// A class's smoke (a Scout's Smoke Pellet, a Trickster's Smoke Bomb; crate::classes): the
/// smoke grenade's cloud where the server says.
fn class_smoke(
    mut commands: Commands,
    time: Res<Time>,
    looks: Option<Res<Looks>>,
    mut page: ResMut<crate::classes::ClassPage>,
) {
    let Some(looks) = looks else {
        return;
    };
    let t = time.elapsed_secs();
    for s in page.smokes.drain(..) {
        smoke_cloud(&mut commands, &looks, crate::tracer::from_wow(s.at), t);
    }
}

/// A smoke grenade's cloud: cards scattered through its reach, spreading and drifting up.
fn smoke_cloud(commands: &mut Commands, looks: &Looks, at: Vec3, t: f32) {
    const CARDS: usize = 18;
    for i in 0..CARDS {
        // A spiral through the reach, low near the edge, high in the middle.
        let k = i as f32 / CARDS as f32;
        let angle = k * std::f32::consts::TAU * 2.6;
        let r = SMOKE_RADIUS * (0.25 + 0.7 * k);
        let offset = Vec3::new(angle.cos() * r, 0.6 + 2.2 * (1.0 - k), angle.sin() * r);
        commands.spawn((
            Name::new("mw2 smoke"),
            SmokeCard {
                born: t + k * 0.6,
                secs: SMOKE_SECS,
                drift: Vec3::new(angle.cos() * 0.15, 0.12, angle.sin() * 0.15),
                size: 3.5 + 2.5 * (1.0 - k),
            },
            Mesh3d(looks.card.clone()),
            MeshMaterial3d(looks.smoke_card.clone()),
            Transform::from_translation(at + offset).with_scale(Vec3::splat(0.01)),
        ));
    }
}

/// The smoke cards face the camera, grow in, drift and thin out.
fn smoke_cards(
    mut commands: Commands,
    time: Res<Time>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
    mut cards: Query<(Entity, &SmokeCard, &mut Transform)>,
) {
    let t = time.elapsed_secs();
    let dt = time.delta_secs();
    let Ok(cam) = cameras.single() else {
        return;
    };
    for (e, c, mut tf) in &mut cards {
        let age = t - c.born;
        if age >= c.secs {
            commands.entity(e).despawn();
            continue;
        }
        let grow = (age / 1.5).clamp(0.0, 1.0);
        let fade = 1.0 - ((age - c.secs * 0.7) / (c.secs * 0.3)).clamp(0.0, 1.0);
        tf.translation += c.drift * dt;
        tf.scale = Vec3::splat((c.size * grow * (0.6 + 0.4 * fade)).max(0.01));
        tf.look_at(cam.translation(), Vec3::Y);
    }
}

/// A grenade or charge in the world wears its MW2 model (a frag, a claymore, C4) once built.
#[allow(clippy::too_many_arguments)]
fn dress(
    mut commands: Commands,
    planted: Query<(Entity, &Planted), Without<Dressed>>,
    mut guns: ResMut<crate::npc_guns::NpcGuns>,
    assets: Res<crate::mw2::Mw2Assets>,
    catalog: Res<Catalog>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut images: ResMut<Assets<Image>>,
    mut transforms: Query<&mut Transform>,
) {
    for (e, p) in &planted {
        let Some((scale, parts)) = guns.world_model(
            p.weapon_id,
            &assets,
            &catalog,
            &mut meshes,
            &mut materials,
            &mut images,
        ) else {
            continue;
        };
        if parts.is_empty() {
            continue;
        }
        // The model in place of the ball, at its own size.
        commands.entity(e).insert(Dressed).remove::<Mesh3d>();
        if let Ok(mut tf) = transforms.get_mut(e) {
            tf.scale = Vec3::splat(scale);
        }
        for (mesh, material) in parts {
            commands.spawn((
                Mesh3d(mesh),
                MeshMaterial3d(material),
                Transform::default(),
                ChildOf(e),
            ));
        }
    }
}

/// The server says a tactical caught us (world op 41): a flash whites out the view, a stun
/// grenade shakes and dazes it (the server's Dazed slows the legs).
fn tactical_hit(
    time: Res<Time>,
    mut msgs: MessageReader<benilla_app::fusion::FusionChannelReceived>,
    mut streak: ResMut<Streak>,
    mut shake: ResMut<crate::blast::Shake>,
    mut sfx: MessageWriter<Sfx>,
) {
    let t = time.elapsed_secs();
    for benilla_app::fusion::FusionChannelReceived(m) in msgs.read() {
        if m.channel != benilla_app::fusion::CHANNEL_WORLD
            || m.body.first() != Some(&OP_TACTICAL_HIT)
        {
            continue;
        }
        match m.body.get(1) {
            Some(1) => {
                streak.flash = 1.0;
                sfx.write(Sfx::new("flashbang_ringing"));
            }
            Some(2) => shake.kick(t + 3.0, 0.6),
            _ => {}
        }
    }
}

/// World op 41: a tactical caught us, [41, 1 flash | 2 stun | 3 smoke] (the server's
/// `Fusion::kOpTacticalHit`).
pub const OP_TACTICAL_HIT: u8 = 41;

fn burst(commands: &mut Commands, looks: &Looks, at: Vec3, size: f32, smoke: bool, t: f32) {
    commands.spawn((
        Name::new("mw2 burst"),
        Burst {
            born: t,
            size,
            secs: if smoke { 6.0 } else { 0.4 },
        },
        Mesh3d(looks.ball.clone()),
        MeshMaterial3d(if smoke {
            looks.smoke.clone()
        } else {
            looks.flash.clone()
        }),
        Transform::from_translation(at).with_scale(Vec3::splat(0.1)),
    ));
}

/// Claymores and C4 go off when the server says their weapon hit (C4 also on its trigger).
#[allow(clippy::too_many_arguments)]
fn planted_go_off(
    mut commands: Commands,
    time: Res<Time>,
    input: Res<Mw2Input>,
    equip: Res<EquipState>,
    looks: Option<Res<Looks>>,
    mut hits: MessageReader<WeaponHit>,
    planted: Query<(Entity, &Planted, &Transform), Without<Thrown>>,
    thrown_c4: Query<(Entity, &Planted, &Transform), With<Thrown>>,
    mut sfx: MessageWriter<Sfx>,
) {
    let t = time.elapsed_secs();
    let Some(looks) = looks else {
        hits.clear();
        return;
    };
    let hit: Vec<u16> = hits.read().map(|h| h.0).collect();
    // C4's trigger was pulled this frame (the round went up and `c4_out` dropped).
    let detonated = input.lethal.pressed && !equip.c4_out;
    // (Of what is thrown only C4 waits on a hit or the trigger: a frag goes off on its fuse.)
    for (e, p, tf) in planted
        .iter()
        .chain(thrown_c4.iter().filter(|(_, p, _)| p.c4))
    {
        let goes = hit.contains(&p.weapon_id) || (p.c4 && detonated);
        if goes && (p.c4 || !hit.is_empty()) {
            commands.entity(e).despawn();
            burst(&mut commands, &looks, tf.translation, 7.0, false, t);
            sfx.write(Sfx::new("grenade_explode_default"));
        }
    }
}

fn bursts(mut commands: Commands, time: Res<Time>, mut q: Query<(Entity, &Burst, &mut Transform)>) {
    let t = time.elapsed_secs();
    for (e, b, mut tf) in &mut q {
        let k = (t - b.born) / b.secs;
        if k >= 1.0 {
            commands.entity(e).despawn();
        } else {
            tf.scale = Vec3::splat(b.size * (0.3 + 0.7 * k.sqrt()));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_grenade_arcs_to_the_ground() {
        // Flat ground at y = 0, thrown level from 1.5 yd: as the server's ThrowArc.
        let ground =
            |a: Vec3, b: Vec3| (a.y >= 0.0 && b.y < 0.0).then(|| a.lerp(b, a.y / (a.y - b.y)));
        let (points, secs, landed) = arc(Vec3::new(0.0, 1.5, 0.0), Vec3::X, ground);
        assert!(landed);
        let end = *points.last().unwrap();
        assert!(end.y.abs() < 1e-3 && end.x > 5.0);
        assert!(secs > 0.2 && secs < MAX_FLIGHT);
    }

    #[test]
    fn fuses_match_the_server() {
        assert_eq!(fuse("frag_grenade_mp"), Some((3.5, false)));
        assert_eq!(fuse("semtex_mp"), Some((2.5, true)));
        assert_eq!(fuse("c4_mp"), None);
        assert_eq!(fuse("claymore_mp"), None);
    }

    #[test]
    fn every_reward_has_its_item() {
        for (_, name, _) in crate::streaks::REWARDS {
            assert!(streak_item(name).is_some(), "{name}");
        }
    }

    #[test]
    fn the_equipment_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((
            refill,
            use_equipment,
            call_in,
            frag_paths,
            fly,
            planted_go_off,
            bursts,
        ));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }
}
