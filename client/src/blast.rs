//! Rockets and explosions as the server tells them (the world channel): a launcher's round flies
//! from the gun to where it lands in its flight time — an RPG's or a Javelin's straight, trailing
//! smoke and flame; an underbarrel grenade in an arc — and every explosion (launchers, grenades,
//! C4, claymores, Martyrdom, killstreaks) is a fireball of WoW's own flame, rolling smoke, a dust
//! ring and sparks, with a report and, close by, a kick to the view.

use bevy::prelude::*;

use benilla_app::fusion::{CHANNEL_WORLD, FusionChannelReceived};
use benilla_world::view::WorldCamera;

use crate::Mw2Set;
use crate::tracer::from_wow;

pub const OP_EXPLOSION: u8 = 30;
pub const OP_PROJECTILE: u8 = 31;

/// A launcher's round in flight, as the server sent it (WoW coordinates).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Projectile {
    pub shooter: u64,
    pub weapon: u16,
    pub from: [f32; 3],
    pub to: [f32; 3],
    pub flight_ms: u16,
}

/// An explosion (WoW coordinates, radius in yards).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Explosion {
    pub at: [f32; 3],
    pub radius: f32,
    pub weapon: u16,
}

fn f32_at(b: &[u8], i: usize) -> f32 {
    f32::from_le_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]])
}

pub fn decode_explosion(b: &[u8]) -> Option<Explosion> {
    (b.len() == 19 && b[0] == OP_EXPLOSION).then(|| Explosion {
        at: [f32_at(b, 1), f32_at(b, 5), f32_at(b, 9)],
        radius: f32_at(b, 13),
        weapon: u16::from_le_bytes([b[17], b[18]]),
    })
}

pub fn decode_projectile(b: &[u8]) -> Option<Projectile> {
    (b.len() == 37 && b[0] == OP_PROJECTILE).then(|| Projectile {
        shooter: u64::from_le_bytes(b[1..9].try_into().unwrap_or_default()),
        weapon: u16::from_le_bytes([b[9], b[10]]),
        from: [f32_at(b, 11), f32_at(b, 15), f32_at(b, 19)],
        to: [f32_at(b, 23), f32_at(b, 27), f32_at(b, 31)],
        flight_ms: u16::from_le_bytes([b[35], b[36]]),
    })
}

/// A grenade launcher's round (the underbarrels, 1000 + the gun's id; the M79-style launchers)
/// arcs; rockets fly straight.
pub fn arcs(weapon: u16, name: Option<&str>) -> bool {
    (1000..2000).contains(&weapon)
        || weapon == NPC_FRAG
        || name.is_some_and(|n| n.starts_with("gl_") || n.contains("m79") || n.contains("frag"))
}

/// A killstreak's bomb (and a shot-down craft's wreck) on its way down (the server's
/// `Sky::kBombVisual`): it drops, nose down, faster as it falls.
pub const BOMB_VISUAL: u16 = 3099;

/// Where a dropped bomb is `k` (0..1) of the way down: on across the ground at an even pace
/// (it keeps the craft's way), falling from rest so its drop grows with the square of the time.
pub fn bomb_point(from: Vec3, to: Vec3, k: f32) -> Vec3 {
    let k = k.clamp(0.0, 1.0);
    let flat = from.lerp(to, k);
    Vec3::new(flat.x, from.y + (to.y - from.y) * k * k, flat.z)
}

/// An NPC's thrown frag (`frag_grenade_mp`), on the grenades' arc.
const NPC_FRAG: u16 = 4001;
/// The server's `Launch::kGrenadeGravity` (yards a second squared): a grenade falls as it flies.
pub const GRENADE_GRAVITY: f32 = 15.0;

/// Where a round is `k` (0..1) of the way through its flight: straight for a rocket, or for a
/// grenade the server's own fall over its `secs` of flight (Launch.h, op 31): the straight line
/// lifted by g·T²·k(1-k)/2.
pub fn flight_point(from: Vec3, to: Vec3, k: f32, arc_secs: Option<f32>) -> Vec3 {
    let k = k.clamp(0.0, 1.0);
    let lift = arc_secs.map_or(0.0, |t| 0.5 * GRENADE_GRAVITY * t * t * k * (1.0 - k));
    from.lerp(to, k) + Vec3::Y * lift
}

#[derive(Component)]
struct Rocket {
    from: Vec3,
    to: Vec3,
    born: f32,
    secs: f32,
    /// A grenade's flight time (it falls on the arc), or None for a rocket.
    arc: Option<f32>,
    /// A dropped bomb (`BOMB_VISUAL`): it falls, smoking, with no motor.
    bomb: bool,
    last_puff: f32,
}

/// A card that faces the eye, growing and fading over its life.
#[derive(Component)]
struct Puff {
    born: f32,
    secs: f32,
    from_size: f32,
    to_size: f32,
    rise: f32,
    drift: Vec3,
}

/// A spark thrown out by a blast.
#[derive(Component)]
struct Spark {
    velocity: Vec3,
    born: f32,
}

#[derive(Resource)]
struct BlastLooks {
    card: Handle<Mesh>,
    body: Handle<Mesh>,
    spark: Handle<Mesh>,
    fire: Handle<StandardMaterial>,
    smoke: Handle<StandardMaterial>,
    dust: Handle<StandardMaterial>,
    hull: Handle<StandardMaterial>,
    hot: Handle<StandardMaterial>,
}

/// The view's kick from a nearby blast (crate::weapon reads nothing of it; the camera system here
/// adds it after benilla seats the camera).
#[derive(Resource, Default)]
pub struct Shake {
    until: f32,
    strength: f32,
}

impl Shake {
    /// Shake the view this hard until then (a stun grenade's daze).
    pub fn kick(&mut self, until: f32, strength: f32) {
        if until > self.until {
            self.until = until;
        }
        self.strength = self.strength.max(strength);
    }
}

pub struct BlastPlugin;

impl Plugin for BlastPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Shake>()
            .add_systems(Startup, setup)
            .add_systems(
                Update,
                (read_world, fly_rockets, puffs, sparks)
                    .chain()
                    .in_set(Mw2Set::Hud),
            )
            .add_systems(
                Update,
                shake_view
                    .after(crate::stance_view::apply_stance_view)
                    .before(Mw2Set::Aim),
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
    let flame: Handle<Image> = assets.load("mpq://Particles/Flame02.blp");
    let cloud: Handle<Image> = assets.load("mpq://Spells/ToonCloud64_Soft.blp");
    let card = |c: Color, tex: &Handle<Image>, add: bool| StandardMaterial {
        base_color: c,
        base_color_texture: Some(tex.clone()),
        unlit: true,
        alpha_mode: if add {
            AlphaMode::Add
        } else {
            AlphaMode::Blend
        },
        cull_mode: None,
        ..default()
    };
    commands.insert_resource(BlastLooks {
        card: meshes.add(Rectangle::new(1.0, 1.0)),
        body: meshes.add(Cuboid::new(0.12, 0.12, 0.7)),
        spark: meshes.add(Cuboid::new(0.04, 0.04, 0.25)),
        fire: materials.add(card(Color::srgb(1.0, 0.72, 0.35), &flame, true)),
        smoke: materials.add(card(Color::srgba(0.32, 0.31, 0.3, 0.75), &cloud, false)),
        dust: materials.add(card(Color::srgba(0.55, 0.47, 0.36, 0.6), &cloud, false)),
        hull: materials.add(StandardMaterial {
            base_color: Color::srgb(0.2, 0.21, 0.19),
            unlit: true,
            ..default()
        }),
        hot: materials.add(StandardMaterial {
            base_color: Color::srgb(1.0, 0.85, 0.5),
            unlit: true,
            alpha_mode: AlphaMode::Add,
            ..default()
        }),
    });
}

#[allow(clippy::too_many_arguments)]
fn read_world(
    mut commands: Commands,
    time: Res<Time>,
    looks: Option<Res<BlastLooks>>,
    catalog: Res<crate::catalog::Catalog>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    mut shake: ResMut<Shake>,
    mut msgs: MessageReader<FusionChannelReceived>,
    script: Option<NonSend<benilla_ui::script::UiScript>>,
    mut rng: Local<u32>,
) {
    let Some(looks) = looks else {
        msgs.clear();
        return;
    };
    let t = time.elapsed_secs();
    let eye = cameras.iter().next().map(GlobalTransform::translation);
    if *rng == 0 {
        *rng = 0x2545_F491;
    }
    let mut next = || {
        *rng ^= *rng << 13;
        *rng ^= *rng >> 17;
        *rng ^= *rng << 5;
        (*rng % 10_000) as f32 / 10_000.0
    };
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        if let Some(p) = decode_projectile(&m.body) {
            let name = catalog.gun(p.weapon).map(|g| g.name.as_str());
            let (from, to) = (from_wow(p.from), from_wow(p.to));
            let bomb = p.weapon == BOMB_VISUAL;
            commands.spawn((
                Name::new("mw2 rocket"),
                Rocket {
                    from,
                    to,
                    born: t,
                    secs: (f32::from(p.flight_ms) / 1000.0).max(0.05),
                    arc: (!bomb && arcs(p.weapon, name))
                        .then_some((f32::from(p.flight_ms) / 1000.0).max(0.05)),
                    bomb,
                    last_puff: t,
                },
                Mesh3d(looks.body.clone()),
                MeshMaterial3d(looks.hull.clone()),
                // A bomb is a good deal bigger than a launcher's round, seen from the ground.
                Transform::from_translation(from)
                    .looking_at(to, Vec3::Y)
                    .with_scale(Vec3::splat(if bomb { 2.6 } else { 1.0 })),
            ));
        } else if let Some(x) = decode_explosion(&m.body) {
            let at = from_wow(x.at);
            let r = x.radius.clamp(1.5, 12.0);
            // The fireball: flame cards bursting out and up.
            for _ in 0..9 {
                let off = Vec3::new(next() - 0.5, next() * 0.6, next() - 0.5) * r * 0.5;
                commands.spawn((
                    Puff {
                        born: t,
                        secs: 0.35 + next() * 0.3,
                        from_size: r * 0.3,
                        to_size: r * (0.8 + next() * 0.5),
                        rise: 1.5,
                        drift: off * 1.5,
                    },
                    Mesh3d(looks.card.clone()),
                    MeshMaterial3d(looks.fire.clone()),
                    Transform::from_translation(at + off * 0.3).with_scale(Vec3::splat(0.1)),
                ));
            }
            // Smoke rolling up after it.
            for _ in 0..10 {
                let off = Vec3::new(next() - 0.5, next() * 0.5, next() - 0.5) * r;
                commands.spawn((
                    Puff {
                        born: t + 0.05,
                        secs: 2.5 + next() * 2.0,
                        from_size: r * 0.4,
                        to_size: r * (1.4 + next()),
                        rise: 2.0 + next() * 2.0,
                        drift: off * 0.4,
                    },
                    Mesh3d(looks.card.clone()),
                    MeshMaterial3d(looks.smoke.clone()),
                    Transform::from_translation(at + off * 0.4).with_scale(Vec3::splat(0.1)),
                ));
            }
            // A ring of dust thrown along the ground.
            for i in 0..12 {
                let a = i as f32 / 12.0 * std::f32::consts::TAU;
                let out = Vec3::new(a.cos(), 0.0, a.sin());
                commands.spawn((
                    Puff {
                        born: t,
                        secs: 1.4 + next() * 0.6,
                        from_size: r * 0.25,
                        to_size: r * 0.9,
                        rise: 0.4,
                        drift: out * r * 1.4,
                    },
                    Mesh3d(looks.card.clone()),
                    MeshMaterial3d(looks.dust.clone()),
                    Transform::from_translation(at + out * 0.3).with_scale(Vec3::splat(0.1)),
                ));
            }
            // Sparks.
            for _ in 0..14 {
                let v = Vec3::new(next() - 0.5, next() * 0.9 + 0.2, next() - 0.5)
                    .normalize_or_zero()
                    * (8.0 + next() * 10.0);
                commands.spawn((
                    Spark {
                        velocity: v,
                        born: t,
                    },
                    Mesh3d(looks.spark.clone()),
                    MeshMaterial3d(looks.hot.clone()),
                    Transform::from_translation(at).looking_to(v, Vec3::Y),
                ));
            }
            // The report, and a kick to a view close by.
            if let Some(eye) = eye {
                let d = eye.distance(at);
                if let Some(script) = &script
                    && d < 120.0
                {
                    crate::lua::run(
                        script,
                        "PlaySoundFile(\"Sound\\\\Spells\\\\DynamiteExplode.wav\")",
                    );
                }
                let strength = shake_strength(d, r);
                if strength > shake.strength * ((shake.until - t).max(0.0) / 0.6) {
                    shake.strength = strength;
                    shake.until = t + 0.6;
                }
            }
        }
    }
}

/// How hard a blast `radius` yards wide kicks a view `distance` yards off (degrees).
pub fn shake_strength(distance: f32, radius: f32) -> f32 {
    let reach = radius * 4.0;
    if distance >= reach {
        0.0
    } else {
        2.5 * (1.0 - distance / reach)
    }
}

fn fly_rockets(
    mut commands: Commands,
    time: Res<Time>,
    looks: Option<Res<BlastLooks>>,
    mut rockets: Query<(Entity, &mut Rocket, &mut Transform)>,
) {
    let Some(looks) = looks else {
        return;
    };
    let t = time.elapsed_secs();
    for (e, mut r, mut tf) in &mut rockets {
        let k = (t - r.born) / r.secs;
        if k >= 1.0 {
            commands.entity(e).despawn();
            continue;
        }
        let point = |k: f32| {
            if r.bomb {
                bomb_point(r.from, r.to, k)
            } else {
                flight_point(r.from, r.to, k, r.arc)
            }
        };
        let at = point(k);
        let ahead = point((k + 0.02).min(1.0));
        let scale = tf.scale;
        // Nose along its way (Bevy picks another "up" where a bomb points straight down).
        *tf = Transform::from_translation(at)
            .looking_at(ahead, Vec3::Y)
            .with_scale(scale);
        // A smoke trail and the motor's flame behind it (a bomb only smokes, thinly).
        if t - r.last_puff >= if r.bomb { 0.06 } else { 0.025 } {
            r.last_puff = t;
            let back = (at - ahead).normalize_or_zero();
            commands.spawn((
                Puff {
                    born: t,
                    secs: if r.arc.is_some() { 0.5 } else { 1.6 },
                    from_size: 0.25,
                    to_size: if r.arc.is_some() { 0.5 } else { 1.4 },
                    rise: 0.3,
                    drift: Vec3::ZERO,
                },
                Mesh3d(looks.card.clone()),
                MeshMaterial3d(looks.smoke.clone()),
                Transform::from_translation(at + back * 0.4).with_scale(Vec3::splat(0.1)),
            ));
            if r.arc.is_none() && !r.bomb {
                commands.spawn((
                    Puff {
                        born: t,
                        secs: 0.06,
                        from_size: 0.45,
                        to_size: 0.2,
                        rise: 0.0,
                        drift: Vec3::ZERO,
                    },
                    Mesh3d(looks.card.clone()),
                    MeshMaterial3d(looks.fire.clone()),
                    Transform::from_translation(at + back * 0.45).with_scale(Vec3::splat(0.1)),
                ));
            }
        }
    }
}

fn puffs(
    mut commands: Commands,
    time: Res<Time>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    mut q: Query<(Entity, &Puff, &mut Transform, &mut Visibility)>,
) {
    let (t, dt) = (time.elapsed_secs(), time.delta_secs());
    let eye = cameras.iter().next().map(GlobalTransform::translation);
    for (e, p, mut tf, mut vis) in &mut q {
        let age = t - p.born;
        if age < 0.0 {
            *vis = Visibility::Hidden;
            continue;
        }
        if age > p.secs {
            commands.entity(e).despawn();
            continue;
        }
        *vis = Visibility::Visible;
        let k = age / p.secs;
        // Eased growth, then a shrink into nothing at the very end.
        let grow = 1.0 - (1.0 - k).powi(2);
        let fade = ((1.0 - k) / 0.25).clamp(0.0, 1.0);
        let size = (p.from_size + (p.to_size - p.from_size) * grow) * fade.max(0.35);
        tf.translation += (p.drift * (1.0 - k) + Vec3::Y * p.rise) * dt;
        tf.scale = Vec3::splat(size.max(0.01));
        if let Some(eye) = eye {
            let at = tf.translation;
            tf.look_at(eye, Vec3::Y);
            tf.translation = at;
        }
    }
}

fn sparks(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut Spark, &mut Transform)>,
) {
    let (t, dt) = (time.elapsed_secs(), time.delta_secs());
    for (e, mut s, mut tf) in &mut q {
        if t - s.born > 0.9 {
            commands.entity(e).despawn();
            continue;
        }
        s.velocity.y -= 25.0 * dt;
        let v = s.velocity;
        tf.translation += v * dt;
        if v.length_squared() > 0.01 {
            let at = tf.translation;
            *tf = Transform::from_translation(at).looking_to(v, Vec3::Y);
        }
    }
}

/// A nearby blast shakes the view (after benilla seats the camera and the rocket's view, before
/// the gun aims).
pub(crate) fn shake_view(
    time: Res<Time>,
    shake: Res<Shake>,
    mut cameras: Query<&mut Transform, With<WorldCamera>>,
) {
    let t = time.elapsed_secs();
    let left = shake.until - t;
    if left <= 0.0 {
        return;
    }
    let s = shake.strength * (left / 0.6).clamp(0.0, 1.0);
    let w = t * 47.0;
    let (pitch, yaw) = (
        (w.sin() * s).to_radians(),
        ((w * 1.3).cos() * s * 0.6).to_radians(),
    );
    for mut tf in &mut cameras {
        tf.rotate_local(Quat::from_euler(EulerRot::YXZ, yaw, pitch, 0.0));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_server_messages_decode() {
        let mut b = vec![OP_EXPLOSION];
        for f in [1.0f32, 2.0, 3.0, 4.5] {
            b.extend_from_slice(&f.to_le_bytes());
        }
        b.extend_from_slice(&4001u16.to_le_bytes());
        assert_eq!(
            decode_explosion(&b),
            Some(Explosion {
                at: [1.0, 2.0, 3.0],
                radius: 4.5,
                weapon: 4001
            })
        );
        assert!(decode_explosion(&b[..18]).is_none());

        let mut p = vec![OP_PROJECTILE];
        p.extend_from_slice(&0x1122_3344_5566_7788u64.to_le_bytes());
        p.extend_from_slice(&23u16.to_le_bytes());
        for f in [1.0f32, 2.0, 3.0, 10.0, 20.0, 30.0] {
            p.extend_from_slice(&f.to_le_bytes());
        }
        p.extend_from_slice(&750u16.to_le_bytes());
        let d = decode_projectile(&p).unwrap();
        assert_eq!(d.shooter, 0x1122_3344_5566_7788);
        assert_eq!((d.weapon, d.flight_ms), (23, 750));
        assert_eq!(d.to, [10.0, 20.0, 30.0]);
        assert!(decode_projectile(&p[..36]).is_none());
    }

    #[test]
    fn grenades_arc_rockets_fly_straight() {
        let (a, b) = (Vec3::ZERO, Vec3::new(0.0, 0.0, -50.0));
        // A second's flight: the top of the fall, 15·1²/8 yd over the line, half way.
        assert!((flight_point(a, b, 0.5, Some(1.0)).y - 1.875).abs() < 1e-5);
        assert_eq!(flight_point(a, b, 0.5, None).y, 0.0);
        assert_eq!(flight_point(a, b, 1.0, Some(1.0)), b);
        assert!(arcs(1023, None) && !arcs(23, Some("rpg_mp")) && arcs(40, Some("gl_mp")));
        assert!(arcs(NPC_FRAG, None)); // an NPC's frag lobs
    }

    #[test]
    fn a_bomb_drops_faster_as_it_falls() {
        let (a, b) = (Vec3::new(0.0, 40.0, 0.0), Vec3::new(10.0, 0.0, 0.0));
        assert_eq!(bomb_point(a, b, 0.0), a);
        assert_eq!(bomb_point(a, b, 1.0), b);
        // Half the time: half the way across, a quarter of the way down.
        let mid = bomb_point(a, b, 0.5);
        assert!((mid.x - 5.0).abs() < 1e-5 && (mid.y - 30.0).abs() < 1e-5);
        // Each tenth of the time drops it more than the last.
        let drops: Vec<f32> = (0..10)
            .map(|i| {
                bomb_point(a, b, i as f32 / 10.0).y - bomb_point(a, b, (i + 1) as f32 / 10.0).y
            })
            .collect();
        assert!(drops.windows(2).all(|w| w[1] > w[0]));
        // No catalog gun takes the bomb's id: it never arcs as a grenade.
        assert!(!arcs(BOMB_VISUAL, None));
    }

    #[test]
    fn close_blasts_kick_harder() {
        assert!(shake_strength(2.0, 5.0) > shake_strength(15.0, 5.0));
        assert_eq!(shake_strength(25.0, 5.0), 0.0);
    }
}
