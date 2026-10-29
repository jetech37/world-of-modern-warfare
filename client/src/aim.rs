//! What the crosshair is on: the nearest living unit whose pick bounds the camera's forward ray
//! crosses, unless the static world (a wall, a building, the ground) is hit first. The unit pick
//! uses benilla's own unit pick parts (`CreaturePickPart`, the bounds its hover falls back to) and
//! the occlusion uses its triangle-accurate world pick plus its world colliders (terrain
//! included, [`StaticWorld`]), so you cannot shoot through a wall or a hill.

use avian3d::prelude::SpatialQuery;
use benilla_app::{ObjectType, Objects};
use benilla_world::collision::{PickOccluder, WorldCollision};
use benilla_world::interact::{CreaturePickPart, WorldPick, ray_mesh_bounds};
use benilla_world::view::WorldCamera;
use bevy::camera::primitives::Aabb;
use bevy::platform::collections::{HashMap, HashSet};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::mode::FpsState;

/// The farthest the crosshair reaches, in yards: past every 1.12 spell range and as far as a
/// round carries (the server's `kGunfight.shotRange`, fusion/Gunfights.h; keep the two the same),
/// so a hit marker and a head claim reach as far as the server scores a hit.
pub const MAX_RANGE: f32 = 150.0;

/// The most `ChildOf` hops from a pick part to its unit: real joint chains are single digits.
const MAX_HOPS: usize = 64;

/// The unit under the crosshair.
#[derive(Clone, Debug)]
pub struct AimTarget {
    /// The unit's net entity, the one [`benilla_app::Interact`] takes.
    pub entity: Entity,
    pub guid: u64,
    /// Yards from the camera along the ray.
    pub distance: f32,
    /// A player, not a creature.
    pub player: bool,
    /// Something you can talk to (a vendor, a quest giver): never fired on, since targeting it
    /// through the interact path would open its window.
    pub service: bool,
}

/// This frame's aim.
#[derive(Resource, Debug, Default)]
pub struct Aim {
    pub target: Option<AimTarget>,
    /// Yards to the first static-world hit, `None` for the sky.
    pub world_distance: Option<f32>,
    /// Yards to the first solid static-world hit: what a walking body collides with (terrain, WMO
    /// walls and floors, the doodads with a collision hull), not what is only drawn (a bush, a
    /// canopy, grass). A spell's bolt flies through what a player walks through.
    pub solid_distance: Option<f32>,
    /// The unit under the crosshair before the solid world: a bolt's foreseen target, which may
    /// stand behind a bush the crosshair's own pick ([`Aim::target`]) stops at.
    pub bolt_target: Option<AimTarget>,
    /// The camera ray this frame, for the HUD and the weapon.
    pub ray: Option<Ray3d>,
    /// The static world under the crosshair (Bevy space), `None` for the sky: where a companion
    /// is sent, or a ground ability aimed (crate::companion).
    pub ground: Option<Vec3>,
}

pub struct AimPlugin;

impl Plugin for AimPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Aim>()
            .add_systems(Update, update_aim.in_set(Mw2Set::Aim));
    }
}

pub(crate) fn update_aim(
    state: Res<FpsState>,
    mut aim: ResMut<Aim>,
    camera: Query<&Transform, With<WorldCamera>>,
    parts: Query<
        (
            Entity,
            &Aabb,
            &GlobalTransform,
            Option<&InheritedVisibility>,
        ),
        With<CreaturePickPart>,
    >,
    parents: Query<&ChildOf>,
    objects: Objects,
    world: StaticWorld,
    third: Res<crate::thirdperson::ThirdPerson>,
    bodies: Query<&GlobalTransform, Without<CreaturePickPart>>,
    fusion: Res<crate::fusion::Fusion>,
    group: Option<Res<benilla_app::GroupMembers>>,
) {
    aim.target = None;
    aim.world_distance = None;
    aim.solid_distance = None;
    aim.bolt_target = None;
    aim.ray = None;
    aim.ground = None;
    if !state.active {
        return;
    }
    // The camera is a root, so its `Transform` is its pose, and it is this frame's: benilla
    // seats it in `WorldStage::Input`, which runs before this set.
    let Ok(cam) = camera.single() else {
        return;
    };
    let ray = Ray3d::new(cam.translation, cam.forward());
    aim.ray = Some(ray);

    let mut units = living_units(&objects, state.self_guid);
    // Rounds pass through our own and our group's: companions, pets, mounts, party and raid
    // members and whatever they control (the server's trace skips the same, ShotPassesThrough).
    let friends = own_side(state.self_guid, group.as_deref().map_or(&[][..], |g| &g.0));
    units.retain(|_, (guid, ..)| {
        let fields = objects.iter().find(|(g, ..)| *g == *guid).map(|(_, _, f)| f);
        !passes_through(*guid, fields.map(controllers).unwrap_or_default(), &friends)
            && !self_controls(&objects, state.self_guid, *guid)
    });

    let mut best: Option<AimTarget> = None;
    for (part, aabb, gt, vis) in &parts {
        if vis.is_some_and(|v| !v.get()) {
            continue;
        }
        let Some(d) = ray_mesh_bounds(ray.origin, *ray.direction, aabb, gt) else {
            continue;
        };
        if d > MAX_RANGE || best.as_ref().is_some_and(|b| b.distance <= d) {
            continue;
        }
        let Some((entity, &(guid, player, service))) = unit_of(part, &parents, &units) else {
            continue;
        };
        best = Some(AimTarget {
            entity,
            guid,
            distance: d,
            player,
            service,
        });
    }

    let solid = world.solid_distance(ray);
    let wall = world.distance_with(ray, solid);
    aim.world_distance = wall;
    aim.solid_distance = solid;
    aim.ground = wall.map(|d| ray.origin + *ray.direction * d);
    aim.bolt_target = best
        .clone()
        .filter(|t| solid.is_none_or(|w| t.distance <= w + 0.25));
    aim.target = best.filter(|t| wall.is_none_or(|w| t.distance <= w + 0.25));

    // Third person (crate::thirdperson): the camera picks what the crosshair is on, but rounds
    // leave from the character's eyes toward that point, as the server expects them to.
    if third.on
        && let Some(me) = objects.player()
        && let Ok(body) = bodies.get(me)
    {
        let reach = aim
            .target
            .as_ref()
            .map(|t| t.distance)
            .or(wall)
            .unwrap_or(MAX_RANGE);
        let point = ray.origin + *ray.direction * reach;
        let eye = body.translation()
            + Vec3::Y * (EYE_YARDS * crate::stance_view::eye_scale(fusion.stance));
        if let Ok(dir) = Dir3::new(point - eye) {
            aim.ray = Some(Ray3d {
                origin: eye,
                direction: dir,
            });
        }
    }
}

/// The static world a ray can meet. benilla's object pick ([`WorldPick`]) answers only the drawn
/// doodads and WMOs: terrain draws through its own lane and declares no pick geometry, so a ray
/// over open ground met nothing and the crosshair's ground point was always the sky. The terrain,
/// the WMO walk faces and the static doodad hulls are physics colliders marked [`PickOccluder`],
/// the set benilla's own mouse pick traces as the world (`target::hover`), so they are cast too.
/// Units, build pieces and liquid are not static world.
#[derive(bevy::ecs::system::SystemParam)]
pub struct StaticWorld<'w, 's> {
    drawn: WorldPick<'w, 's>,
    spatial: SpatialQuery<'w, 's>,
    occluders: Query<'w, 's, (), With<PickOccluder>>,
}

impl StaticWorld<'_, '_> {
    /// Yards along `ray` to the nearest static-world hit, `None` for the sky.
    pub fn distance(&self, ray: Ray3d) -> Option<f32> {
        self.distance_with(ray, self.solid_distance(ray))
    }

    /// [`Self::distance`] with the solid leg already cast.
    pub fn distance_with(&self, ray: Ray3d, solid: Option<f32>) -> Option<f32> {
        let drawn = self
            .drawn
            .cast(ray, &HashSet::new(), false)
            .into_iter()
            .map(|h| h.hit.distance);
        drawn.chain(solid).reduce(f32::min)
    }

    /// Yards along `ray` to the nearest solid static-world hit: the colliders a walking body
    /// collides with ([`WorldCollision::body_filter`]: terrain, WMO walk faces, doodad collision
    /// hulls), not the drawn-only pick (a bush has no hull, a WMO's DETAIL faces no walk collider),
    /// as the server's bolts fly (its vmaps hold the same hulls). `None` for none.
    pub fn solid_distance(&self, ray: Ray3d) -> Option<f32> {
        self.spatial
            .cast_ray_predicate(
                ray.origin,
                ray.direction,
                f32::MAX,
                true,
                // The mask that reaches every `PickOccluder` collider; the marker picks within it.
                &WorldCollision::body_filter(),
                &|e| self.occluders.contains(e),
            )
            .map(|h| h.distance)
    }
}

/// A character's eye height standing (yards), for third person's rounds.
const EYE_YARDS: f32 = 1.65;

/// The living units other than our own, by entity: (guid, a player, offers services).
pub fn living_units(
    objects: &Objects,
    self_guid: Option<u64>,
) -> HashMap<Entity, (u64, bool, bool)> {
    objects
        .iter()
        .filter(|&(guid, ..)| Some(guid) != self_guid)
        .filter_map(|(guid, entity, fields)| {
            let player = match fields.object_type() {
                Some(ObjectType::Player) => true,
                Some(ObjectType::Unit) => false,
                _ => return None,
            };
            if fields.unit_health() == Some(0) {
                return None;
            }
            Some((entity, (guid, player, fields.unit_npc_flags() != 0)))
        })
        .collect()
}

/// Our guid and our group members' guids: the side whose bodies a round passes through.
pub fn own_side(self_guid: Option<u64>, group: &[u64]) -> HashSet<u64> {
    self_guid.into_iter().chain(group.iter().copied()).collect()
}

/// The guids that control a unit: its summoner, charmer and creator (a pet, a companion, a
/// mount, a totem), 0 for none.
pub fn controllers(fields: &benilla_app::ObjectFields) -> [u64; 3] {
    [
        fields.unit_summoned_by().unwrap_or(0),
        fields.unit_charmed_by().unwrap_or(0),
        fields.unit_created_by().unwrap_or(0),
    ]
}

/// Whether a round passes through this unit: it is on our side (us or a group member), or one of
/// our side controls it.
pub fn passes_through(guid: u64, controllers: [u64; 3], side: &HashSet<u64>) -> bool {
    side.contains(&guid) || controllers.iter().any(|c| *c != 0 && side.contains(c))
}

/// Whether our own fields name the unit as our charm or summon (a mount we ride, a pet).
fn self_controls(objects: &Objects, self_guid: Option<u64>, guid: u64) -> bool {
    let Some(me) = self_guid else { return false };
    objects
        .iter()
        .find(|(g, ..)| *g == me)
        .is_some_and(|(_, _, f)| f.unit_charm() == Some(guid) || f.unit_summon() == Some(guid))
}

/// Climb a pick part's parents to the unit entity that owns it.
pub fn unit_of<'a>(
    part: Entity,
    parents: &Query<&ChildOf>,
    units: &'a HashMap<Entity, (u64, bool, bool)>,
) -> Option<(Entity, &'a (u64, bool, bool))> {
    let mut cur = part;
    for _ in 0..MAX_HOPS {
        if let Some(u) = units.get(&cur) {
            return Some((cur, u));
        }
        cur = parents.get(cur).ok()?.parent();
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rounds_pass_through_our_side_and_what_it_controls() {
        let side: HashSet<u64> = [1, 2].into_iter().collect(); // us, a party member
        assert!(passes_through(2, [0; 3], &side), "a party member");
        assert!(passes_through(10, [1, 0, 0], &side), "our companion (summoned by us)");
        assert!(passes_through(11, [0, 2, 0], &side), "a party member's charm");
        assert!(passes_through(12, [0, 0, 2], &side), "a party member's totem");
        assert!(!passes_through(13, [0; 3], &side), "an enemy blocks");
        assert!(!passes_through(14, [99, 0, 0], &side), "a stranger's pet blocks");
        let none = own_side(Some(1), &[]);
        assert!(!passes_through(2, [0; 3], &none), "out of a group a player blocks");
    }
    use avian3d::prelude::{Collider, PhysicsPlugins, RigidBody};
    use bevy::ecs::system::RunSystemOnce;

    /// A headless physics world with a flat 120 x 120 yd ground at y = 0, as the terrain streamer
    /// attaches it: a static trimesh on the default layer, marked [`PickOccluder`], with no
    /// render or pick mesh (terrain draws through its own lane, which the object pick never sees).
    fn world_on_flat_ground(occluder: bool) -> App {
        let mut app = App::new();
        app.add_plugins((
            MinimalPlugins,
            bevy::transform::TransformPlugin,
            bevy::asset::AssetPlugin::default(),
            bevy::scene::ScenePlugin,
            PhysicsPlugins::new(bevy::app::PostUpdate),
        ));
        app.init_asset::<Mesh>();
        let mut ground = app.world_mut().spawn((
            RigidBody::Static,
            Collider::trimesh(
                vec![
                    Vec3::new(-60.0, 0.0, -60.0),
                    Vec3::new(60.0, 0.0, -60.0),
                    Vec3::new(60.0, 0.0, 60.0),
                    Vec3::new(-60.0, 0.0, 60.0),
                ],
                vec![[0u32, 2, 1], [0, 3, 2]],
            ),
            Transform::default(),
        ));
        if occluder {
            ground.insert(PickOccluder);
        }
        app.finish();
        app.cleanup();
        app.update();
        app
    }

    /// The camera at eye height, pitched `pitch_deg` down, looking along -Z.
    fn look_down(pitch_deg: f32) -> Ray3d {
        let p = pitch_deg.to_radians();
        Ray3d::new(
            Vec3::new(0.0, 1.65, 0.0),
            Dir3::new(Vec3::new(0.0, -p.sin(), -p.cos())).unwrap(),
        )
    }

    fn cast(app: &mut App, ray: Ray3d) -> Option<f32> {
        app.world_mut()
            .run_system_once(move |w: StaticWorld| w.distance(ray))
            .expect("system runs")
    }

    #[test]
    fn the_crosshair_meets_the_terrain() {
        // Magicmike in Goldshire: 14 degrees down, the street about 7 yd ahead; and -3 degrees,
        // about 30 yd ahead. Both came back as the sky.
        let mut app = world_on_flat_ground(true);
        for pitch in [14.0f32, 3.0] {
            let ray = look_down(pitch);
            let d = cast(&mut app, ray).unwrap_or_else(|| panic!("no ground at {pitch} down"));
            let point = ray.origin + *ray.direction * d;
            assert!(point.y.abs() < 0.01, "{pitch} down lands at {point}");
            let ahead = 1.65 / pitch.to_radians().tan();
            assert!(
                (-point.z - ahead).abs() < 0.05,
                "{pitch} down lands at {point}"
            );
        }
    }

    fn solid(app: &mut App, ray: Ray3d) -> Option<f32> {
        app.world_mut()
            .run_system_once(move |w: StaticWorld| w.solid_distance(ray))
            .expect("system runs")
    }

    /// A 2 x 2 yd upright quad facing the camera at `z`, as a doodad's hull or a WMO face is
    /// attached: static, marked as world, on `layers` (None: the default layer, which a walking
    /// body collides with).
    fn wall_at(app: &mut App, z: f32, layers: Option<avian3d::prelude::CollisionLayers>) {
        let mut e = app.world_mut().spawn((
            RigidBody::Static,
            Collider::trimesh(
                vec![
                    Vec3::new(-1.0, 0.0, z),
                    Vec3::new(1.0, 0.0, z),
                    Vec3::new(1.0, 3.0, z),
                    Vec3::new(-1.0, 3.0, z),
                ],
                vec![[0u32, 1, 2], [0, 2, 3]],
            ),
            Transform::default(),
            PickOccluder,
        ));
        if let Some(l) = layers {
            e.insert(l);
        }
        app.update();
    }

    #[test]
    fn a_bolt_flies_through_what_a_body_walks_through() {
        // Level, at chest height, toward -Z.
        let ray = Ray3d::new(Vec3::new(0.0, 1.2, 0.0), Dir3::NEG_Z);
        let mut app = world_on_flat_ground(true);
        assert_eq!(solid(&mut app, ray), None, "nothing solid ahead");
        // A face the body walks through (a WMO DETAIL face: only on the camera's layer, bit 2,
        // as benilla bakes it) 6 yd ahead: not solid for a bolt.
        let walk_through = avian3d::prelude::CollisionLayers::new(
            avian3d::prelude::LayerMask(1 << 2),
            avian3d::prelude::LayerMask::ALL,
        );
        wall_at(&mut app, -6.0, Some(walk_through));
        assert_eq!(solid(&mut app, ray), None, "a walk-through face stops nothing");
        // A doodad with a collision hull (the default layer) 20 yd ahead: the bolt stops there.
        wall_at(&mut app, -20.0, None);
        let d = solid(&mut app, ray).expect("the hull is solid");
        assert!((d - 20.0).abs() < 0.05, "stopped at {d}");
    }

    #[test]
    fn the_sky_and_non_world_colliders_are_not_ground() {
        let mut app = world_on_flat_ground(true);
        let up = Ray3d::new(Vec3::new(0.0, 1.65, 0.0), Dir3::Y);
        assert_eq!(cast(&mut app, up), None);
        // A collider that is not static world (a unit's hull) is not ground.
        let mut app = world_on_flat_ground(false);
        assert_eq!(cast(&mut app, look_down(14.0)), None);
    }
}
