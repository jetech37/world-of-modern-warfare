//! Death ragdolls: a unit seen to die goes limp and falls under physics onto the ground around it
//! instead of playing WoW's death animation, and a blast nearby throws the body.
//!
//! A death is the alive-to-dead edge; corpses already dead when first seen do not replay the fall;
//! a blast nearby holds the body for its throw.
//!
//! The physics is [`solver`]; this file finds the deaths, feeds the solver the rig's pose and the
//! world's floors, and writes the result back through benilla's [`PhysicsPose`] hook, where the
//! [`PhysicsDrivenPose`] marker keeps the animation evaluator off the bones.
//!
//! On by default. `MW2_RAGDOLL=0` (or `1`) at launch decides it outright; otherwise the CVar
//! `mw2Ragdoll` (`/console mw2Ragdoll 0`, saved with the other CVars) is read every two seconds.

pub mod solver;

use std::collections::HashMap;

use benilla_app::fusion::{CHANNEL_WORLD, FusionChannelReceived};
use benilla_app::{ObjectFields, ObjectType, Objects};
use benilla_world::collision::WorldCollision;
use benilla_world::rig_anim::{PhysicsDrivenPose, PhysicsPose, RigPose};
use benilla_world::view::WorldCamera;
use bevy::math::Affine3A;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::blast::decode_explosion;
use crate::lua;
use crate::tracer::from_wow;
use solver::{Body, Start};

/// Living units farther than this from the camera are not followed (a death out there stays
/// WoW's).
const FOLLOW_RANGE: f32 = 80.0;
/// The most bodies simulating at once; the oldest sleeps first.
const MAX_AWAKE: usize = 12;
/// The most ragdolled corpses kept; past it the oldest sleeping one goes back to WoW's pose.
const MAX_CORPSES: usize = 48;
/// The death animation hands over to the ragdoll over this long.
const BLEND_SECS: f32 = 0.2;
/// A blast's throw waits this long for the death it may cause.
const HOLD_SECS: f32 = 2.0;
/// A body this fresh still takes a blast that arrives after it fell.
const LATE_BLAST_SECS: f32 = 0.6;
/// The CVar poll.
const POLL_SECS: f32 = 2.0;
/// The longest frame one advance catches up.
const MAX_FRAME: f32 = 0.067;

pub struct RagdollPlugin;

impl Plugin for RagdollPlugin {
    fn build(&self, app: &mut App) {
        let forced = std::env::var("MW2_RAGDOLL")
            .ok()
            .and_then(|v| parse_switch(&v));
        app.insert_resource(Switch {
            on: forced.unwrap_or(true),
            forced,
            next_poll: 0.0,
            lua: true,
        })
        .init_resource::<Ragdolls>()
        .add_systems(Update, (poll_switch, read_blasts.in_set(Mw2Set::Hud)))
        .add_systems(PostUpdate, drive.in_set(PhysicsPose));
    }
}

/// `"0"`/`"1"` (and the usual spellings) to off/on.
fn parse_switch(v: &str) -> Option<bool> {
    match v.trim().to_ascii_lowercase().as_str() {
        "0" | "off" | "false" | "no" => Some(false),
        "1" | "on" | "true" | "yes" => Some(true),
        _ => None,
    }
}

#[derive(Resource)]
struct Switch {
    on: bool,
    /// The environment's word, which the CVar does not override.
    forced: Option<bool>,
    next_poll: f32,
    /// Lua answered (a VM with no `GetCVar` stops the poll).
    lua: bool,
}

/// A living unit followed for the moment it dies.
struct Alive {
    at: Vec3,
    time: f32,
    velocity: Vec3,
}

struct Corpse {
    guid: u64,
    body: Body,
    born: f32,
    /// The marker is on: the evaluator is off the bones.
    owned: bool,
    /// The final pose once asleep, reasserted only if something moved a bone.
    settled: Option<Vec<Transform>>,
}

#[derive(Resource, Default)]
struct Ragdolls {
    alive: HashMap<Entity, Alive>,
    corpses: HashMap<Entity, Corpse>,
    /// A blast's throw by guid, until it expires: `(expires, velocity)`.
    held: HashMap<u64, (f32, Vec3)>,
    scratch: Vec<Transform>,
}

fn poll_switch(
    time: Res<Time>,
    mut switch: ResMut<Switch>,
    script: Option<NonSend<benilla_ui::script::UiScript>>,
) {
    if switch.forced.is_some() || !switch.lua {
        return;
    }
    let t = time.elapsed_secs();
    if t < switch.next_poll {
        return;
    }
    let Some(script) = script else {
        return;
    };
    switch.next_poll = t + POLL_SECS;
    match lua::eval::<Option<String>>(
        &script,
        "RegisterCVar(\"mw2Ragdoll\", \"1\") return GetCVar(\"mw2Ragdoll\")",
    ) {
        Some(v) => {
            let on = v.as_deref().and_then(parse_switch).unwrap_or(true);
            if switch.on != on {
                info!("benilla-mw2: ragdolls {}", if on { "on" } else { "off" });
                switch.on = on;
            }
        }
        None => switch.lua = false,
    }
}

fn is_unit(fields: &ObjectFields) -> bool {
    matches!(
        fields.object_type(),
        Some(ObjectType::Unit | ObjectType::Player)
    )
}

/// The server's explosions: each unit in reach with a clear line from the blast gets a throw,
/// applied now to a body that just fell, or held for the death it may cause.
fn read_blasts(
    time: Res<Time>,
    switch: Res<Switch>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut state: ResMut<Ragdolls>,
    objects: Objects,
    transforms: Query<&GlobalTransform>,
    collision: WorldCollision,
) {
    if !switch.on {
        msgs.clear();
        return;
    }
    let now = time.elapsed_secs();
    let me = objects.player();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        let Some(blast) = decode_explosion(&m.body) else {
            continue;
        };
        let at = from_wow(blast.at);
        if !at.is_finite() || !blast.radius.is_finite() {
            continue;
        }
        let reach = blast.radius.max(0.0) + 2.0;
        for (guid, entity, fields) in objects.iter() {
            if Some(entity) == me || !is_unit(fields) {
                continue;
            }
            let Ok(g) = transforms.get(entity) else {
                continue;
            };
            let chest = g.translation() + Vec3::Y;
            let offset = chest - at;
            if offset.length_squared() > reach * reach {
                continue;
            }
            let Some(velocity) = solver::blast_velocity(offset, blast.radius) else {
                continue;
            };
            // A wall between keeps the body where it is.
            let from = at + Vec3::Y * 0.3;
            let line = chest - from;
            let distance = line.length();
            if let Ok(dir) = Dir3::new(line)
                && collision
                    .ray_body(from, dir, distance)
                    .is_some_and(|h| h.distance < distance - 0.3)
            {
                continue;
            }
            match state.corpses.get_mut(&entity) {
                Some(c) if now - c.born <= LATE_BLAST_SECS && !c.body.is_asleep() => {
                    c.body.kick(velocity);
                }
                Some(_) => {}
                None => {
                    state.held.insert(guid, (now + HOLD_SECS, velocity));
                }
            }
        }
    }
}

/// The world as the solver sees it: benilla's walking colliders.
struct Floors<'a, 'w, 's>(&'a WorldCollision<'w, 's>);

impl solver::World for Floors<'_, '_, '_> {
    fn ray(&self, origin: Vec3, dir: Vec3, max: f32) -> Option<solver::Hit> {
        let dir = Dir3::new(dir).ok()?;
        self.0.ray_body(origin, dir, max).map(|h| solver::Hit {
            distance: h.distance,
            normal: h.normal,
        })
    }
}

fn blend(from: &Transform, to: &Transform, w: f32) -> Transform {
    Transform {
        translation: from.translation.lerp(to.translation, w),
        rotation: from.rotation.slerp(to.rotation, w),
        scale: from.scale.lerp(to.scale, w),
    }
}

/// Follow the living, start a body at each death, step the bodies and write their poses.
#[allow(clippy::too_many_arguments)]
fn drive(
    time: Res<Time>,
    switch: Res<Switch>,
    mut state: ResMut<Ragdolls>,
    objects: Objects,
    mut rigs: Query<&mut RigPose>,
    roots: Query<&GlobalTransform>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    collision: WorldCollision,
    mut commands: Commands,
) {
    let state = &mut *state;
    if !switch.on {
        release_all(state, &mut commands);
        return;
    }
    let now = time.elapsed_secs();
    let dt = time.delta_secs().min(MAX_FRAME);
    state.held.retain(|_, (expires, _)| *expires > now);
    state.alive.retain(|e, _| rigs.contains(*e));
    state.corpses.retain(|&e, _| {
        let keep = rigs.contains(e);
        if !keep {
            release(&mut commands, e);
        }
        keep
    });
    let Some(eye) = cameras.iter().next().map(GlobalTransform::translation) else {
        return;
    };
    let me = objects.player();
    let floors = Floors(&collision);
    for (guid, entity, fields) in objects.iter() {
        if Some(entity) == me || !is_unit(fields) {
            continue;
        }
        let Some(health) = fields.unit_health() else {
            continue;
        };
        let Ok(mut rig) = rigs.get_mut(entity) else {
            continue;
        };
        let Ok(root) = roots.get(rig.joints_root) else {
            continue;
        };
        let root: Affine3A = root.affine();
        let at = Vec3::from(root.translation);
        if health > 0 {
            // Alive (again): WoW's animation has the bones.
            if state.corpses.remove(&entity).is_some() {
                release(&mut commands, entity);
            }
            if at.distance_squared(eye) > FOLLOW_RANGE * FOLLOW_RANGE {
                state.alive.remove(&entity);
                continue;
            }
            let entry = state.alive.entry(entity).or_insert(Alive {
                at,
                time: now,
                velocity: Vec3::ZERO,
            });
            let since = now - entry.time;
            if since > 0.0 {
                let v = (at - entry.at) / since.max(0.001);
                // A teleport is not a run.
                entry.velocity = if v.is_finite() && v.length() < 40.0 {
                    v
                } else {
                    Vec3::ZERO
                };
                entry.at = at;
                entry.time = now;
            }
            continue;
        }
        if !state.corpses.contains_key(&entity) {
            // Only a death seen happen: a corpse already lying there at login stays as it is.
            let Some(alive) = state.alive.remove(&entity) else {
                continue;
            };
            let Some(mut body) = Body::new(Start {
                locals: &rig.locals,
                parents: &rig.parents,
                root,
                velocity: alive.velocity,
                viewer: eye,
                salt: guid,
            }) else {
                continue;
            };
            if let Some((_, v)) = state.held.remove(&guid) {
                body.kick(v);
            }
            {
                let mut awake: Vec<&mut Body> =
                    state.corpses.values_mut().map(|c| &mut c.body).collect();
                solver::make_room(&mut awake, MAX_AWAKE);
            }
            state.corpses.insert(
                entity,
                Corpse {
                    guid,
                    body,
                    born: now,
                    owned: false,
                    settled: None,
                },
            );
            if state.corpses.len() > MAX_CORPSES
                && let Some(oldest) = state
                    .corpses
                    .iter()
                    .filter(|(_, c)| c.settled.is_some())
                    .min_by(|a, b| a.1.born.total_cmp(&b.1.born))
                    .map(|(e, _)| *e)
            {
                state.corpses.remove(&oldest);
                release(&mut commands, oldest);
            }
        }
        let Some(corpse) = state.corpses.get_mut(&entity) else {
            continue;
        };
        if let Some(settled) = &corpse.settled {
            if rig.locals != *settled {
                rig.locals.clone_from(settled);
                rig.pose_dirty = true;
            }
            continue;
        }
        corpse.body.advance(dt, &floors);
        let scratch = &mut state.scratch;
        corpse.body.pose(root, scratch);
        if scratch.len() != rig.locals.len() {
            // The model was rebuilt under the body: give it back to WoW.
            state.corpses.remove(&entity);
            release(&mut commands, entity);
            continue;
        }
        let age = now - corpse.born;
        if !corpse.owned && age < BLEND_SECS {
            // The death animation's first frames, leaning into the ragdoll.
            let w = (age / BLEND_SECS).clamp(0.0, 1.0);
            for (local, target) in rig.locals.iter_mut().zip(scratch.iter()) {
                *local = blend(local, target, w);
            }
        } else {
            if !corpse.owned {
                corpse.owned = true;
                commands.entity(entity).insert(PhysicsDrivenPose);
                debug!(
                    "benilla-mw2: ragdoll {:#x}, {} bones",
                    corpse.guid,
                    rig.locals.len()
                );
            }
            rig.locals.clone_from(scratch);
            if corpse.body.is_asleep() {
                corpse.settled = Some(scratch.clone());
            }
        }
        rig.pose_dirty = true;
    }
}

/// Hand a rig back to WoW's animation.
fn release(commands: &mut Commands, entity: Entity) {
    if let Ok(mut e) = commands.get_entity(entity) {
        e.try_remove::<PhysicsDrivenPose>();
    }
}

fn release_all(state: &mut Ragdolls, commands: &mut Commands) {
    for (entity, _) in state.corpses.drain() {
        release(commands, entity);
    }
    state.alive.clear();
    state.held.clear();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_switch_reads_the_usual_words() {
        assert_eq!(parse_switch("0"), Some(false));
        assert_eq!(parse_switch(" off "), Some(false));
        assert_eq!(parse_switch("1"), Some(true));
        assert_eq!(parse_switch("TRUE"), Some(true));
        assert_eq!(parse_switch("maybe"), None);
    }
}
