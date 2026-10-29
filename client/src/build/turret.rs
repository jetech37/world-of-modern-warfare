//! The sentry gun as MW2 has it: its MW2 model in moving layers (crate::build::models::Layer)
//! that turn to face what it shoots, tip to it and spin the barrels, sweeping slowly while it
//! has nothing in sight; each round leaves the real muzzle, and the minigun spins up, fires and
//! spins down with MW2's own sounds.

use bevy::prelude::*;

use super::models::{Layer, Template};
use crate::tracer::from_wow;

/// The model drawn this much larger than MW2's (it read short beside WoW's bodies).
pub const SENTRY_SCALE: f32 = 1.3;
/// MW2's sentry skeleton (`sentry_minigun`, inches): where the gun body tips, where the barrels
/// spin, and the muzzle.
const PITCH_PIVOT: [f32; 3] = [-0.132, 0.0, 48.518];
const SPIN_PIVOT: [f32; 3] = [15.009, 0.0, 48.466];
const MUZZLE: [f32; 3] = [36.98, 0.0, 48.495];
/// Turning speed (radians a second), the barrels' top spin, and how long after its last round
/// the gun still counts as firing.
const TURN_RATE: f32 = 6.0;
const SPIN_RATE: f32 = 28.0;
const FIRING_HOLD: f32 = 0.35;
/// The idle sweep: this far each way, this fast.
const SWEEP: f32 = 0.6;
const SWEEP_SPEED: f32 = 0.45;
/// Seconds of MW2's firing loop sample, played again while the gun fires.
const FIRE_LOOP_SECS: f32 = 1.3;
const HEARING: f32 = 80.0;

/// A model inch, in Bevy yards, as the meshes are built.
fn inches(v: [f32; 3]) -> Vec3 {
    from_wow([v[0] / 36.0, v[1] / 36.0, v[2] / 36.0])
}

/// A sentry's moving parts and where it aims.
#[derive(Component)]
pub struct Turret {
    pub piece: u32,
    model: Entity,
    yaw_node: Entity,
    pitch_node: Entity,
    spin_node: Entity,
    yaw: f32,
    pitch: f32,
    spin: f32,
    spin_rate: f32,
    /// What it fires at (world), until when.
    aim: Option<(Vec3, f32)>,
    firing_until: f32,
    spun_up: bool,
    next_loop: f32,
}

/// A sentry's round, as the server reported it (crate::build's SentryFire).
#[derive(Message, Clone, Copy, Debug)]
pub struct SentryShot {
    pub piece: u32,
    /// Where it went: the target, or past it for a miss.
    pub to: Vec3,
    pub hit: bool,
}

/// Spawn a model's meshes under `root`: a turret's in its layers (with a [`Turret`] on the
/// root), anything else as one fixed set. Returns the mesh entities.
pub fn spawn_model(
    commands: &mut Commands,
    root: Entity,
    piece: u32,
    drawn: Vec<(Handle<Mesh>, Handle<StandardMaterial>, Layer)>,
    scale: f32,
    mut mesh_bundle: impl FnMut(&mut EntityCommands, usize),
) {
    let turret = drawn.iter().any(|(_, _, l)| *l != Layer::Base);
    let model = commands
        .spawn((
            Transform::from_scale(Vec3::splat(scale)),
            Visibility::Inherited,
            ChildOf(root),
        ))
        .id();
    let (pitch_at, spin_at) = (inches(PITCH_PIVOT), inches(SPIN_PIVOT));
    let yaw_node = commands
        .spawn((Transform::default(), Visibility::Inherited, ChildOf(model)))
        .id();
    let pitch_node = commands
        .spawn((
            Transform::from_translation(pitch_at),
            Visibility::Inherited,
            ChildOf(yaw_node),
        ))
        .id();
    let spin_node = commands
        .spawn((
            Transform::from_translation(spin_at - pitch_at),
            Visibility::Inherited,
            ChildOf(pitch_node),
        ))
        .id();
    for (i, (mesh, material, layer)) in drawn.into_iter().enumerate() {
        // Each mesh is in model space: under a pivot node it sits back by that pivot.
        let (parent, back) = match (turret, layer) {
            (false, _) | (_, Layer::Base) => (model, Vec3::ZERO),
            (_, Layer::Yaw) => (yaw_node, Vec3::ZERO),
            (_, Layer::Pitch) => (pitch_node, -pitch_at),
            (_, Layer::Spin) => (spin_node, -spin_at),
        };
        let mut e = commands.spawn((
            Mesh3d(mesh),
            MeshMaterial3d(material),
            Transform::from_translation(back),
            ChildOf(parent),
        ));
        mesh_bundle(&mut e, i);
    }
    if turret {
        commands.entity(root).insert(Turret {
            piece,
            model,
            yaw_node,
            pitch_node,
            spin_node,
            yaw: 0.0,
            pitch: 0.0,
            spin: 0.0,
            spin_rate: 0.0,
            aim: None,
            firing_until: -10.0,
            spun_up: false,
            next_loop: 0.0,
        });
    }
}

/// The yaw and pitch (radians) that point the gun's forward (-Z in the model's frame) along
/// `dir`, a direction in that frame.
pub fn aim_angles(dir: Vec3) -> (f32, f32) {
    let yaw = (-dir.x).atan2(-dir.z);
    let pitch = dir.y.atan2(Vec2::new(dir.x, dir.z).length());
    (yaw, pitch)
}

/// `from` turned toward `to` by at most `step`, the short way round.
pub fn turn_toward(from: f32, to: f32, step: f32) -> f32 {
    let d =
        (to - from + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
    from + d.clamp(-step, step)
}

/// Templates as spawnable meshes with their layers.
pub fn drawn_layers(
    templates: &[Template],
    meshes: &mut Assets<Mesh>,
) -> Vec<(Handle<Mesh>, Handle<StandardMaterial>, Layer)> {
    templates
        .iter()
        .map(|t| (meshes.add(t.mesh.clone()), t.material.clone(), t.layer))
        .collect()
}

/// Aim, spin and fire every sentry: a round turns it to its target at once, the barrels spin
/// while it fires, and with nothing to shoot it sweeps.
#[allow(clippy::too_many_arguments)]
pub fn work_turrets(
    time: Res<Time>,
    mut shots: MessageReader<SentryShot>,
    mut turrets: Query<&mut Turret>,
    mut nodes: Query<&mut Transform>,
    globals: Query<&GlobalTransform>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
    mut streaks: MessageWriter<crate::tracer::Streak>,
    mut sfx: MessageWriter<crate::mw2::sound::Sfx>,
) {
    let t = time.elapsed_secs();
    let dt = time.delta_secs();
    let eye = cameras.iter().next().map(GlobalTransform::translation);
    let mut fired: Vec<SentryShot> = shots.read().copied().collect();
    for mut tur in &mut turrets {
        let tur = &mut *tur;
        let Ok(model) = globals.get(tur.model) else {
            continue;
        };
        let to_model = model.affine().inverse();
        let pivot_world = model.transform_point(inches(PITCH_PIVOT));
        // This sentry's rounds: aim at once, fire from the muzzle.
        let mine: Vec<SentryShot> = {
            let (m, rest): (Vec<_>, Vec<_>) = fired.drain(..).partition(|s| s.piece == tur.piece);
            fired = rest;
            m
        };
        for s in &mine {
            tur.aim = Some((s.to, t + 2.0));
            let (yaw, pitch) = aim_angles(to_model.transform_vector3(s.to - pivot_world));
            tur.yaw = yaw;
            tur.pitch = pitch;
            tur.firing_until = t + FIRING_HOLD;
            if let Ok(p) = globals.get(tur.pitch_node) {
                let muzzle = p.transform_point(inches(MUZZLE) - inches(PITCH_PIVOT));
                let along = s.to - muzzle;
                let end = along.length() + if s.hit { 0.0 } else { 15.0 };
                if let Ok(dir) = Dir3::new(along) {
                    streaks.write(crate::tracer::Streak {
                        start: muzzle,
                        dir: *dir,
                        end,
                        enemy: false,
                    });
                }
            }
        }
        // With nothing to shoot, sweep from side to side.
        let (want_yaw, want_pitch) = match tur.aim {
            Some((at, until)) if t < until => {
                aim_angles(to_model.transform_vector3(at - pivot_world))
            }
            _ => {
                tur.aim = None;
                (SWEEP * (t * SWEEP_SPEED).sin(), 0.0)
            }
        };
        tur.yaw = turn_toward(tur.yaw, want_yaw, TURN_RATE * dt);
        tur.pitch = turn_toward(tur.pitch, want_pitch, TURN_RATE * dt);
        let firing = t < tur.firing_until;
        let goal = if firing { SPIN_RATE } else { 0.0 };
        tur.spin_rate += (goal - tur.spin_rate) * (1.0 - (-dt * 4.0).exp());
        tur.spin = (tur.spin + tur.spin_rate * dt) % std::f32::consts::TAU;
        if let Ok(mut tf) = nodes.get_mut(tur.yaw_node) {
            tf.rotation = Quat::from_rotation_y(tur.yaw);
        }
        if let Ok(mut tf) = nodes.get_mut(tur.pitch_node) {
            tf.rotation = Quat::from_rotation_x(tur.pitch);
        }
        if let Ok(mut tf) = nodes.get_mut(tur.spin_node) {
            // The barrels turn about the gun's forward axis.
            tf.rotation = Quat::from_rotation_z(tur.spin);
        }
        // MW2's minigun: spin up, the firing loop while it fires, spin down.
        let gain = eye.map_or(0.0, |e| {
            let k = (1.0 - e.distance(pivot_world) / HEARING).clamp(0.0, 1.0);
            k * k * 0.8
        });
        let mut play = |alias: &str| {
            if gain > 0.01 {
                sfx.write(crate::mw2::sound::Sfx::at(alias, gain));
            }
        };
        if firing && !tur.spun_up {
            tur.spun_up = true;
            tur.next_loop = t + 0.15;
            play("sentry_minigun_spinup1");
        }
        if firing && t >= tur.next_loop {
            tur.next_loop = t + FIRE_LOOP_SECS;
            play("sentry_minigun_fire");
        }
        if !firing && tur.spun_up {
            tur.spun_up = false;
            play("sentry_minigun_spindown1");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_gun_points_where_it_aims() {
        let (yaw, pitch) = aim_angles(Vec3::NEG_Z);
        assert!(yaw.abs() < 1e-6 && pitch.abs() < 1e-6); // straight ahead
        let (yaw, _) = aim_angles(Vec3::NEG_X); // to the left of -Z is -X
        let fwd = Quat::from_rotation_y(yaw) * Vec3::NEG_Z;
        assert!(fwd.distance(Vec3::NEG_X) < 1e-5);
        let (_, pitch) = aim_angles(Vec3::new(0.0, 1.0, -1.0));
        let up = Quat::from_rotation_x(pitch) * Vec3::NEG_Z;
        assert!(up.distance(Vec3::new(0.0, 1.0, -1.0).normalize()) < 1e-5);
    }

    #[test]
    fn turning_takes_the_short_way() {
        assert!((turn_toward(3.0, -3.0, 1.0) - (3.0 + (std::f32::consts::TAU - 6.0))).abs() < 1e-5);
        assert_eq!(turn_toward(0.0, 0.5, 0.1), 0.1);
        assert!((turn_toward(0.0, 0.05, 0.1) - 0.05).abs() < 1e-6); // arrives
    }

    #[test]
    fn the_turret_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems(work_turrets);
        schedule
            .initialize(&mut world)
            .expect("the system initialises");
    }
}
