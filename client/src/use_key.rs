//! F, the use key, as one arbiter. F already builds (doors, crate::build), picks guns up
//! (held, crate::pickups) and works a first-aid kit (crate::downed); whatever F is not claimed by
//! goes to the crosshair: it targets the creature, player or object under the reticle and, within
//! the stock interact ranges, uses it as a right click would (talk, a vendor, loot, a chest)
//! through benilla's own dispatcher ([`benilla_app::fusion::LayerUse`]). A hostile is only
//! targeted: F never starts an attack.
//!
//! Priority: build mode, then a door in reach, then a gun on the ground under the crosshair, then
//! a revive (yourself down, or a downed ally under the crosshair), then target and use.

use benilla_app::fusion::LayerUse;
use benilla_app::{ObjectType, Objects};
use benilla_world::interact::{CreaturePickPart, GoPickPart, WorldPick, ray_mesh_bounds};
use benilla_world::view::WorldCamera;
use bevy::camera::primitives::Aabb;
use bevy::platform::collections::{HashMap, HashSet};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::mode::FpsState;

/// Who F belongs to this press.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UseOwner {
    /// Build mode has F (its doors, crate::build).
    Build,
    /// A door in reach under the crosshair opens or shuts (crate::build).
    Door,
    /// A gun on the ground under the crosshair: held to pick it up (crate::pickups).
    Pickup,
    /// A first-aid kit: on yourself while down, or on the downed ally under the crosshair
    /// (crate::downed).
    Revive,
    /// The crosshair's target: select it, and use it in reach.
    Target,
}

/// F's owner, by the priority above.
pub fn use_owner(building: bool, door: bool, pickup: bool, revive: bool) -> UseOwner {
    if building {
        UseOwner::Build
    } else if door {
        UseOwner::Door
    } else if pickup {
        UseOwner::Pickup
    } else if revive {
        UseOwner::Revive
    } else {
        UseOwner::Target
    }
}

/// What the reticle is over, as F acts on it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UseKind {
    /// A living creature or NPC: its services (gossip, a vendor, a trainer, a flight master).
    Unit,
    /// A dead creature: its loot (or skinning).
    DeadUnit,
    /// Another player.
    Player,
    /// A player's corpse.
    Corpse,
    /// A GameObject: a chest, a door, a mailbox, a herb.
    GameObject,
}

/// The NPC-service reach, 5.5556 yd (`[0x804328]`), which the stock client also gives most
/// GameObjects.
pub const SERVICE_REACH: f32 = 5.5556;
/// Loot's melee reach: `max(reach + reach + 4/3, 5)` (the target's combat reach and ours).
pub const LOOT_FLOOR: f32 = 5.0;
pub const LOOT_OFFSET: f32 = 1.333_33;

/// How far F may use `kind` (yards, center to center), the stock interact ranges: the service
/// reach for NPCs, players and objects, loot's melee reach for the dead.
pub fn use_reach(kind: UseKind, target_reach: f32, my_reach: f32) -> f32 {
    match kind {
        UseKind::DeadUnit => (target_reach + my_reach + LOOT_OFFSET).max(LOOT_FLOOR),
        UseKind::Corpse => LOOT_FLOOR,
        UseKind::Unit | UseKind::Player | UseKind::GameObject => SERVICE_REACH,
    }
}

/// Whether F uses as well as targets: in reach. The dispatcher re-checks with the stock gray.
pub fn in_use_reach(kind: UseKind, distance: f32, target_reach: f32, my_reach: f32) -> bool {
    distance <= use_reach(kind, target_reach, my_reach)
}

/// One thing under the reticle: what F would act on.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ReticleHit {
    pub entity: Entity,
    pub kind: UseKind,
    /// Yards along the crosshair's ray.
    pub distance: f32,
}

/// The nearest of the unit and object hits, unless a wall stands in front of it (`wall`, the
/// first static-world hit; a hit within `WALL_SLACK` of it still counts, a body against a wall).
pub fn nearest_unblocked(
    hits: impl IntoIterator<Item = ReticleHit>,
    wall: Option<f32>,
) -> Option<ReticleHit> {
    hits.into_iter()
        .filter(|h| h.distance <= MAX_PICK && wall.is_none_or(|w| h.distance <= w + WALL_SLACK))
        .min_by(|a, b| a.distance.total_cmp(&b.distance))
}

/// How far F picks along the crosshair (yards): far enough to target what you can see.
pub const MAX_PICK: f32 = 100.0;
/// A body against a wall is still picked.
const WALL_SLACK: f32 = 0.25;

/// The most `ChildOf` hops from a pick part to its object.
const MAX_HOPS: usize = 64;

pub struct UseKeyPlugin;

impl Plugin for UseKeyPlugin {
    fn build(&self, app: &mut App) {
        // After the HUD set, where the pickups aim (crate::pickups) and the downed state settle.
        app.add_systems(Update, use_key.after(Mw2Set::Hud));
    }
}

/// The objects F may act on, by entity: their guid's kind, their combat reach and their guid.
fn usable_objects(
    objects: &Objects,
    self_guid: Option<u64>,
) -> HashMap<Entity, (UseKind, f32, u64)> {
    objects
        .iter()
        .filter(|&(guid, ..)| Some(guid) != self_guid)
        .filter_map(|(guid, entity, f)| {
            let kind = match f.object_type()? {
                ObjectType::Unit if f.unit_health() == Some(0) || f.unit_is_dead() => {
                    UseKind::DeadUnit
                }
                ObjectType::Unit => UseKind::Unit,
                ObjectType::Player => UseKind::Player,
                ObjectType::Corpse => UseKind::Corpse,
                ObjectType::GameObject => UseKind::GameObject,
                _ => return None,
            };
            let reach = match kind {
                UseKind::Unit | UseKind::DeadUnit | UseKind::Player => f.unit_combat_reach(),
                _ => 0.0,
            };
            Some((entity, (kind, reach, guid)))
        })
        .collect()
}

/// Climb a pick part's parents to the object that owns it.
fn owner_of(
    part: Entity,
    parents: &Query<&ChildOf>,
    usable: &HashMap<Entity, (UseKind, f32, u64)>,
) -> Option<Entity> {
    let mut cur = part;
    for _ in 0..MAX_HOPS {
        if usable.contains_key(&cur) {
            return Some(cur);
        }
        cur = parents.get(cur).ok()?.parent();
    }
    None
}

/// What is under the crosshair: the units, players, the dead and GameObjects by their pick
/// bounds and triangles, the static world in front of them (F's own pick; crate::tools aims the
/// trades' tools with it too).
#[derive(bevy::ecs::system::SystemParam)]
pub struct Reticle<'w, 's> {
    pub objects: Objects<'w, 's>,
    unit_parts: Query<
        'w,
        's,
        (
            Entity,
            &'static Aabb,
            &'static GlobalTransform,
            Option<&'static InheritedVisibility>,
        ),
        With<CreaturePickPart>,
    >,
    go_parts: Query<'w, 's, Entity, With<GoPickPart>>,
    parents: Query<'w, 's, &'static ChildOf>,
    places: Query<'w, 's, &'static GlobalTransform>,
    world: WorldPick<'w, 's>,
}

/// The thing the reticle picked: its hit, guid, and whether it is within the stock use reach.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Picked {
    pub hit: ReticleHit,
    pub guid: u64,
    /// Yards from our position to its (center to center), when both are known.
    pub apart: Option<f32>,
    pub interact: bool,
}

impl Reticle<'_, '_> {
    /// The nearest object along `ray` that no wall hides (never ourselves).
    pub fn pick(&self, ray: Ray3d, self_guid: Option<u64>) -> Option<Picked> {
        let usable = usable_objects(&self.objects, self_guid);
        let mut hits: Vec<ReticleHit> = Vec::new();
        // Units, players and the dead, by their pick bounds (the crosshair's own test, crate::aim).
        for (part, aabb, gt, vis) in &self.unit_parts {
            if vis.is_some_and(|v| !v.get()) {
                continue;
            }
            let Some(d) = ray_mesh_bounds(ray.origin, *ray.direction, aabb, gt) else {
                continue;
            };
            if let Some(e) = owner_of(part, &self.parents, &usable)
                && let Some(&(kind, ..)) = usable.get(&e)
            {
                hits.push(ReticleHit {
                    entity: e,
                    kind,
                    distance: d,
                });
            }
        }
        // GameObjects by their triangles, and the static world in front of everything.
        let go_set: HashSet<Entity> = self.go_parts.iter().collect();
        let mut wall: Option<f32> = None;
        for h in self.world.cast(ray, &go_set, false) {
            let object = h
                .entity
                .filter(|p| go_set.contains(p))
                .and_then(|p| owner_of(p, &self.parents, &usable));
            match object.and_then(|e| Some((e, usable.get(&e)?.0))) {
                Some((entity, kind)) => hits.push(ReticleHit {
                    entity,
                    kind,
                    distance: h.hit.distance,
                }),
                None => wall = Some(wall.map_or(h.hit.distance, |w: f32| w.min(h.hit.distance))),
            }
        }
        let hit = nearest_unblocked(hits, wall)?;
        // In reach, measured as the stock client does: from our position to the object's.
        let me = self.objects.player();
        let (my_reach, my_at) = (
            self_guid
                .and_then(|g| self.objects.object(g))
                .map_or(0.0, |f| f.unit_combat_reach()),
            me.and_then(|e| self.places.get(e).ok())
                .map(|g| g.translation()),
        );
        let target_at = self.places.get(hit.entity).ok().map(|g| g.translation());
        let &(_, target_reach, guid) = usable.get(&hit.entity)?;
        let apart = match (my_at, target_at) {
            (Some(a), Some(b)) => Some(a.distance(b)),
            _ => None,
        };
        let interact = apart.is_some_and(|d| in_use_reach(hit.kind, d, target_reach, my_reach));
        Some(Picked {
            hit,
            guid,
            apart,
            interact,
        })
    }
}

#[allow(clippy::too_many_arguments)]
fn use_key(
    state: Res<FpsState>,
    input: Res<crate::input::Mw2Input>,
    reticle: Reticle,
    pickups: Res<crate::pickups::Pickups>,
    downed: Res<crate::downed::Downed>,
    aim: Res<crate::aim::Aim>,
    build: Res<crate::build::Building>,
    spatial: avian3d::prelude::SpatialQuery,
    boxes: Query<&crate::build::PieceBox>,
    camera: Query<&Transform, With<WorldCamera>>,
    mut out: MessageWriter<LayerUse>,
) {
    if !state.owns_input || !input.interact.pressed {
        return;
    }
    let Ok(cam) = camera.single() else {
        return;
    };
    let ray = Ray3d::new(cam.translation, cam.forward());
    // A door in reach under the crosshair (crate::build's own cast).
    let door = spatial
        .cast_ray(
            ray.origin,
            ray.direction,
            DOOR_REACH,
            true,
            &benilla_world::collision::WorldCollision::body_filter(),
        )
        .and_then(|h| boxes.get(h.entity).ok())
        .is_some_and(|b| {
            build
                .pieces
                .get(&b.0)
                .is_some_and(|p| p.wire.kind.base() == crate::build::pieces::PieceType::WoodDoor)
        });
    // A downed ally under the crosshair (crate::downed's own test), or yourself down.
    let objects = &reticle.objects;
    let ally = aim.target.as_ref().filter(|a| a.player).map(|a| {
        let reads_down = downed.near.contains(&a.guid)
            || objects
                .object(a.guid)
                .is_some_and(|f| f.unit_dynflag_dead() && f.unit_health().unwrap_or(0) > 0);
        (a.guid, a.distance, reads_down)
    });
    let revive = crate::downed::kit_request(downed.active, ally).is_some();
    let owner = use_owner(state.building, door, pickups.aimed.is_some(), revive);
    if owner != UseOwner::Target {
        return;
    }
    let Some(picked) = reticle.pick(ray, state.self_guid) else {
        return;
    };
    let hit = picked.hit;
    let interact = picked.interact;
    debug!(
        "benilla-mw2: F on {:?} ({:?}, {:.1} yd along the crosshair), use {interact}",
        hit.entity, hit.kind, hit.distance
    );
    out.write(LayerUse {
        entity: hit.entity,
        interact,
    });
}

/// A door's reach, as crate::build casts it.
const DOOR_REACH: f32 = 4.0;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn build_mode_then_door_then_pickup_then_revive_then_target() {
        use UseOwner::*;
        assert_eq!(use_owner(true, true, true, true), Build);
        assert_eq!(use_owner(true, false, false, false), Build);
        assert_eq!(use_owner(false, true, true, true), Door);
        assert_eq!(use_owner(false, false, true, true), Pickup);
        assert_eq!(use_owner(false, false, false, true), Revive);
        assert_eq!(use_owner(false, false, false, false), Target);
    }

    #[test]
    fn f_uses_within_the_stock_ranges_and_only_targets_beyond() {
        // An NPC's services: 5.5556 yd.
        assert!(in_use_reach(UseKind::Unit, 5.5, 1.5, 1.5));
        assert!(!in_use_reach(UseKind::Unit, 5.6, 1.5, 1.5));
        // A GameObject the same.
        assert!(in_use_reach(UseKind::GameObject, 5.0, 0.0, 0.0));
        assert!(!in_use_reach(UseKind::GameObject, 6.0, 0.0, 0.0));
        // Loot: melee reach, floored at 5 yd; a big corpse reaches farther.
        assert_eq!(use_reach(UseKind::DeadUnit, 1.0, 1.5), 5.0);
        assert!((use_reach(UseKind::DeadUnit, 4.0, 1.5) - 6.83333).abs() < 1e-3);
        assert!(in_use_reach(UseKind::DeadUnit, 6.5, 4.0, 1.5));
        assert!(!in_use_reach(UseKind::DeadUnit, 5.2, 1.0, 1.5));
        // A player's corpse, 5 yd.
        assert!(!in_use_reach(UseKind::Corpse, 5.1, 0.0, 0.0));
    }

    #[test]
    fn the_nearest_hit_wins_unless_a_wall_is_in_front() {
        let e = |i: u32| Entity::from_raw_u32(i).unwrap();
        let unit = ReticleHit {
            entity: e(1),
            kind: UseKind::Unit,
            distance: 8.0,
        };
        let chest = ReticleHit {
            entity: e(2),
            kind: UseKind::GameObject,
            distance: 3.0,
        };
        assert_eq!(nearest_unblocked([unit, chest], None), Some(chest));
        assert_eq!(nearest_unblocked([unit], Some(20.0)), Some(unit));
        // A wall at 5 yd hides the unit at 8, not the chest at 3.
        assert_eq!(nearest_unblocked([unit, chest], Some(5.0)), Some(chest));
        assert_eq!(nearest_unblocked([unit], Some(5.0)), None);
        // A body against the wall is still picked.
        assert_eq!(nearest_unblocked([unit], Some(7.9)), Some(unit));
        // Too far to pick at all.
        let far = ReticleHit {
            distance: MAX_PICK + 1.0,
            ..unit
        };
        assert_eq!(nearest_unblocked([far], None), None);
    }
}
