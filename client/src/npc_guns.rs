//! NPC gunners carry MW2's guns: the server names each gunner's gun (the world channel, op 33,
//! every 2 s) and every round one fires (op 32). Each gunner holds its gun's MW2 world model in
//! its right hand in place of the WoW gun, and each round plays the rifle shot on the body, a
//! muzzle flash and the round's flight from the muzzle, and the gun's own NPC fire sound,
//! quieter with distance.

use std::collections::HashMap;
use std::sync::Arc;

use bevy::prelude::*;

use benilla_app::Objects;
use benilla_app::fusion::{
    ATTACH_HAND_RIGHT, CHANNEL_WORLD, FusionChannelReceived, HELD_SLOT_MAIN, HELD_SLOT_RANGED,
    PlayUnitAnim, UnitAttach,
};
use benilla_world::rig_anim::RigPose;
use benilla_world::view::WorldCamera;

use crate::Mw2Set;
use crate::build::models::Template;
use crate::catalog::Catalog;
use crate::mw2::Mw2Assets;
use crate::mw2::sound::Sfx;

pub const OP_NPC_FIRE: u8 = 32;
pub const OP_NPC_GUNS: u8 = 33;
/// WoW's `AnimationData.dbc` rifle shot.
pub const ANIM_ATTACK_RIFLE: u16 = 49;
/// Beyond this a round's report is not heard (yards).
const HEARING: f32 = 120.0;

/// One round an NPC fired: where it ends (raw WoW yards, z up) and whether it hit.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct NpcFire {
    pub guid: u64,
    pub gun: u16,
    pub hit: bool,
    pub to: [f32; 3],
}

fn u64_at(b: &[u8], i: usize) -> u64 {
    u64::from_le_bytes(b[i..i + 8].try_into().unwrap_or_default())
}

fn f32_at(b: &[u8], i: usize) -> f32 {
    f32::from_le_bytes(b[i..i + 4].try_into().unwrap_or_default())
}

/// Op 32, 24 bytes: `[32, guid u64, gun u16, hit u8, to x, y, z f32]`.
pub fn decode_fire(b: &[u8]) -> Option<NpcFire> {
    (b.len() == 24 && b[0] == OP_NPC_FIRE).then(|| NpcFire {
        guid: u64_at(b, 1),
        gun: u16::from_le_bytes([b[9], b[10]]),
        hit: b[11] != 0,
        to: [f32_at(b, 12), f32_at(b, 16), f32_at(b, 20)],
    })
}

/// Op 33: `[33, count u8, then count × (guid u64, gun u16)]`.
pub fn decode_guns(b: &[u8]) -> Option<Vec<(u64, u16)>> {
    let (&op, rest) = b.split_first()?;
    let (&n, rest) = rest.split_first()?;
    if op != OP_NPC_GUNS || rest.len() != usize::from(n) * 10 {
        return None;
    }
    Some(
        rest.chunks_exact(10)
            .map(|c| (u64_at(c, 0), u16::from_le_bytes([c[8], c[9]])))
            .collect(),
    )
}

/// How loud a round fired `distance` yards away sounds (1 alongside, nothing past hearing).
pub fn report_gain(distance: f32) -> f32 {
    let k = (1.0 - distance / HEARING).clamp(0.0, 1.0);
    k * k
}

/// A gun's model, ready to spawn, and where its muzzle is (yards, the model's frame).
struct GunModel {
    templates: Arc<Vec<Template>>,
    meshes: Vec<Handle<Mesh>>,
    /// The scale that brings the model to its class's real size in the hand (`held_length`):
    /// MW2's world models come out far smaller than a WoW hand holds.
    scale: f32,
    muzzle: Vec3,
    /// Where the hand holds it (yards, the model's frame).
    grip: Vec3,
    fire_sound: Option<String>,
    /// Each template's material made unlit, for the held copy lit by WoW's light
    /// ([`light_held_guns`]).
    unlit: Vec<Handle<StandardMaterial>>,
}

/// A gun in a gunner's hand.
struct Carried {
    gun: u16,
    root: Entity,
    /// The barrel's direction in the model's frame (from the grip toward the muzzle, level).
    barrel: Vec3,
    /// The grip (the model's frame, its own units) and the hand point it sits on (the anchor's).
    grip: Vec3,
    hand: Vec3,
}

#[derive(Resource, Default)]
pub struct NpcGuns {
    /// The server's last word: each gunner's gun.
    carried: HashMap<u64, u16>,
    /// Built per catalog gun; `None` when the install has no model for it.
    models: HashMap<u16, Option<GunModel>>,
    held: HashMap<u64, Carried>,
    fires: Vec<NpcFire>,
    /// The server sends the rounds: the hit-only tracer for NPC rounds stands down
    /// (crate::tracer).
    pub fire_seen: bool,
    /// Our own character's gun in third person (crate::thirdperson): its guid and gun.
    self_gun: Option<(u64, u16)>,
    /// Other players' guns up (crate::body_state), by guid.
    players: HashMap<u64, u16>,
    /// `MW2_PROBE_HOLD_GUN=<name>` (`m4`, `usp_mp`), resolved once the catalog has it.
    probe_name: Option<String>,
}

impl NpcGuns {
    /// A catalog item's MW2 world model (a grenade, a claymore, C4) as mesh and material pairs
    /// in yards, built once; `None` while the install loads or when it has none.
    pub fn world_model(
        &mut self,
        gun: u16,
        assets: &Mw2Assets,
        catalog: &Catalog,
        meshes: &mut Assets<Mesh>,
        materials: &mut Assets<StandardMaterial>,
        images: &mut Assets<Image>,
    ) -> Option<(f32, Vec<(Handle<Mesh>, Handle<StandardMaterial>)>)> {
        let m = model_for(
            &mut self.models,
            gun,
            assets,
            catalog,
            meshes,
            materials,
            images,
        )?;
        Some((
            m.scale,
            m.meshes
                .iter()
                .zip(m.templates.iter())
                .map(|(mesh, t)| (mesh.clone(), t.material.clone()))
                .collect(),
        ))
    }

    pub fn self_gun(&self) -> Option<(u64, u16)> {
        self.self_gun
    }
    pub fn set_self_gun(&mut self, gun: Option<(u64, u16)>) {
        self.self_gun = gun;
    }
    /// The guns other players hold up now, by guid (crate::body_state).
    pub fn set_player_guns(&mut self, guns: HashMap<u64, u16>) {
        if self.players != guns {
            self.players = guns;
        }
    }
}

pub struct NpcGunsPlugin;

impl Plugin for NpcGunsPlugin {
    fn build(&self, app: &mut App) {
        let mut guns = NpcGuns::default();
        // `MW2_PROBE_HOLD_GUN=<catalog gun>`: the capture subject (crate::body_state's
        // `FXVIEW_UNIT_GUID`) holds it, a headless look at the grip.
        // The gun's catalog id, or its name (`m4`, `usp_mp`).
        if let Ok(v) = std::env::var("MW2_PROBE_HOLD_GUN") {
            match v.trim().parse() {
                Ok(gun) => {
                    guns.carried.insert(crate::body_state::FXVIEW_UNIT_GUID, gun);
                }
                Err(_) => guns.probe_name = Some(probe_gun_name(&v)),
            }
        }
        app.insert_resource(guns).add_systems(
            Update,
            (read_world, dress, true_size, aim_held_guns, light_held_guns, fire)
                .chain()
                .in_set(Mw2Set::Hud),
        );
    }
}

fn read_world(mut msgs: MessageReader<FusionChannelReceived>, mut guns: ResMut<NpcGuns>) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        if let Some(list) = decode_guns(&m.body) {
            guns.carried = list.into_iter().collect();
        } else if let Some(f) = decode_fire(&m.body) {
            guns.fire_seen = true;
            guns.carried.entry(f.guid).or_insert(f.gun);
            guns.fires.push(f);
        }
    }
}

/// Build a gun's model once (the install reads in the background: try again later).
fn model_for<'a>(
    models: &'a mut HashMap<u16, Option<GunModel>>,
    gun: u16,
    assets: &Mw2Assets,
    catalog: &Catalog,
    meshes: &mut Assets<Mesh>,
    materials: &mut Assets<StandardMaterial>,
    images: &mut Assets<Image>,
) -> Option<&'a GunModel> {
    if !models.contains_key(&gun) {
        let Mw2Assets::Ready(lib) = assets else {
            return None;
        };
        let built = catalog.gun(gun).and_then(|g| {
            let world = lib.world_gun(&g.name)?;
            let templates = Arc::new(crate::build::models::build_mw2(
                &world.part,
                materials,
                images,
            ));
            let length = model_length(&templates);
            let unlit = templates
                .iter()
                .map(|t| {
                    let mut m = materials.get(&t.material).cloned().unwrap_or_default();
                    m.unlit = true;
                    materials.add(m)
                })
                .collect();
            let scale = held_length(&g.class) / length.max(0.01);
            Some(GunModel {
                scale,
                meshes: templates
                    .iter()
                    .map(|t| meshes.add(t.mesh.clone()))
                    .collect(),
                templates,
                // MW2 inches, its X forward, Y left, Z up, as the meshes are built (from_wow).
                muzzle: crate::tracer::from_wow((world.muzzle / 36.0).to_array()),
                grip: crate::tracer::from_wow((world.grip / 36.0).to_array()),
                fire_sound: lib
                    .weapons
                    .get(&g.name)
                    .and_then(|r| r.sounds.fire_npc.clone()),
                unlit,
            })
        });
        if built.is_none() {
            warn!("benilla-mw2: no MW2 world model for NPC gun {gun}");
        }
        models.insert(gun, built);
    }
    models.get(&gun).and_then(Option::as_ref)
}

/// Each gunner in sight holds its MW2 gun in the right hand, and its WoW weapons are hidden.
#[allow(clippy::too_many_arguments)]
fn dress(
    mut commands: Commands,
    mut guns: ResMut<NpcGuns>,
    objects: Objects,
    attach: UnitAttach,
    mut rigs: Query<&mut RigPose>,
    mut vis: Query<&mut Visibility>,
    roots: Query<(), With<NpcGunRoot>>,
    assets: Res<Mw2Assets>,
    catalog: Res<Catalog>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut images: ResMut<Assets<Image>>,
) {
    let guns = &mut *guns;
    if let Some(name) = &guns.probe_name
        && let Some(g) = catalog.gun_named(name)
    {
        guns.carried.insert(crate::body_state::FXVIEW_UNIT_GUID, g.id);
        guns.probe_name = None;
    }
    // The gunners the server names, the players with a gun up, and our own character in third
    // person.
    let mut wanted = guns.carried.clone();
    wanted.extend(guns.players.iter().map(|(g, n)| (*g, *n)));
    if let Some((guid, gun)) = guns.self_gun {
        wanted.insert(guid, gun);
    }
    // Gone, changed guns, or a rebuilt body (its anchors went with it): take the gun back, and
    // a body that holds no gun now shows its own weapons again (our character, the gun put away).
    let mut bared: Vec<u64> = Vec::new();
    guns.held.retain(|guid, c| {
        let keep = wanted.get(guid) == Some(&c.gun) && roots.get(c.root).is_ok();
        if !keep {
            if let Ok(mut e) = commands.get_entity(c.root) {
                e.despawn();
            }
            if !wanted.contains_key(guid) {
                bared.push(*guid);
            }
        }
        keep
    });
    for guid in bared {
        if let Some(unit) = objects.entity(guid) {
            for slot in [HELD_SLOT_RANGED, HELD_SLOT_MAIN] {
                if let Some(item) = attach.held_root(unit, slot)
                    && let Ok(mut v) = vis.get_mut(item)
                {
                    *v = Visibility::Inherited;
                }
            }
        }
    }
    let carried: Vec<(u64, u16)> = wanted.iter().map(|(g, n)| (*g, *n)).collect();
    for (guid, gun) in carried {
        let Some(unit) = objects.entity(guid) else {
            continue;
        };
        // The WoW gun (and the melee weapon a gunner would otherwise hold) out of sight.
        for slot in [HELD_SLOT_RANGED, HELD_SLOT_MAIN] {
            if let Some(item) = attach.held_root(unit, slot)
                && let Ok(mut v) = vis.get_mut(item)
                && *v != Visibility::Hidden
            {
                *v = Visibility::Hidden;
            }
        }
        if guns.held.contains_key(&guid) {
            continue;
        }
        let Some((bone, offset)) = attach.point(unit, ATTACH_HAND_RIGHT) else {
            continue;
        };
        let Some(model) = model_for(
            &mut guns.models,
            gun,
            &assets,
            &catalog,
            &mut meshes,
            &mut materials,
            &mut images,
        ) else {
            continue;
        };
        let Ok(mut rig) = rigs.get_mut(unit) else {
            continue;
        };
        let Some(anchor) = rig.anchor_for(&mut commands, unit, bone) else {
            continue;
        };
        let root = commands
            .spawn((
                Name::new("mw2 npc gun"),
                NpcGunRoot,
                TrueSize(model.scale * race_gun_scale(objects.object(guid).and_then(|f| f.unit_race()))),
                Transform::from_translation(offset)
                    .with_rotation(hand_turn())
                    .with_scale(Vec3::splat(model.scale)),
                Visibility::default(),
                ChildOf(anchor),
            ))
            .id();
        // Each holder its own copy of the meshes, coloured by WoW's light where it stands, as
        // the body holding it is (Bevy's own lights light nothing in this world).
        for (t, mat) in model.templates.iter().zip(model.unlit.iter()) {
            let normals: Vec<Vec3> = match t.mesh.attribute(Mesh::ATTRIBUTE_NORMAL) {
                Some(bevy::mesh::VertexAttributeValues::Float32x3(n)) => {
                    n.iter().map(|v| Vec3::from_array(*v)).collect()
                }
                _ => Vec::new(),
            };
            commands.spawn((
                Mesh3d(meshes.add(t.mesh.clone())),
                MeshMaterial3d(mat.clone()),
                Transform::default(),
                HeldGunPart { unit, normals },
                ChildOf(root),
            ));
        }
        let along = model.muzzle - model.grip;
        let barrel = Vec3::new(along.x, 0.0, along.z)
            .try_normalize()
            .unwrap_or(Vec3::NEG_Z);
        guns.held.insert(
            guid,
            Carried {
                gun,
                root,
                barrel,
                grip: model.grip,
                hand: offset,
            },
        );
    }
}

/// A probe's gun name as the catalog spells it: `m4` is `m4_mp`.
pub fn probe_gun_name(v: &str) -> String {
    let v = v.trim().to_ascii_lowercase();
    if v.ends_with("_mp") { v } else { format!("{v}_mp") }
}

/// A held gun's size for its holder's race (`ChrRaces` id): a gnome's or a dwarf's hands hold a
/// gun a little smaller than its real size, which read as a cannon on them.
pub fn race_gun_scale(race: Option<u8>) -> f32 {
    match race {
        Some(7) => 0.8,  // gnome
        Some(3) => 0.85, // dwarf
        _ => 1.0,
    }
}

/// How long a gun of a class is in the hand (yards): what an MW2 gun of it measures.
pub fn held_length(class: &str) -> f32 {
    match class {
        "pistol" => 0.33,
        "machine_pistol" => 0.45,
        "smg" => 0.7,
        "shotgun" => 1.05,
        "sniper" => 1.3,
        "lmg" => 1.15,
        "launcher" => 1.2,
        "riot_shield" => 1.2,
        "lethal" | "tactical" => 0.15,
        _ => 0.95, // an assault rifle
    }
}

/// A built model's longest extent (its own units).
fn model_length(templates: &[Template]) -> f32 {
    let (mut lo, mut hi) = (Vec3::splat(f32::MAX), Vec3::splat(f32::MIN));
    for t in templates {
        if let Some(bevy::mesh::VertexAttributeValues::Float32x3(ps)) =
            t.mesh.attribute(Mesh::ATTRIBUTE_POSITION)
        {
            for p in ps {
                let v = Vec3::from_array(*p);
                lo = lo.min(v);
                hi = hi.max(v);
            }
        }
    }
    if lo.x > hi.x { 0.0 } else { (hi - lo).max_element() }
}

/// The gun's turn in the hand: WoW's hand point holds an item model barrel along the same axis
/// as MW2's world guns but backwards, so a half turn; `MW2_NPC_GUN_TURN=yaw,pitch,roll` (degrees) tries others.
fn hand_turn() -> Quat {
    let parts: Vec<f32> = std::env::var("MW2_NPC_GUN_TURN")
        .unwrap_or_default()
        .split(',')
        .filter_map(|s| s.trim().parse().ok())
        .collect();
    match parts[..] {
        [y, p, r] => Quat::from_euler(
            EulerRot::YXZ,
            y.to_radians(),
            p.to_radians(),
            r.to_radians(),
        ),
        // Captured (docs/CAPTURE.md): unturned, the barrel pointed back past the wrist.
        _ => Quat::from_rotation_y(std::f32::consts::PI),
    }
}

#[derive(Component)]
struct NpcGunRoot;

/// A held gun's mesh copy: the unit holding it (whose light it takes) and its model normals.
#[derive(Component)]
struct HeldGunPart {
    unit: Entity,
    normals: Vec<Vec3>,
}

/// The light a held gun takes at a normal (world space): the holder's own indoor term when it
/// stands in a lit room (benilla's `ParticleLight`, ambient plus diffuse plus lamps along up),
/// else the day/night scene's ambient plus its diffuse by the sun's angle, the law a WoW body's
/// vertices take.
pub fn held_gun_light(
    normal: Vec3,
    ambient: Vec3,
    diffuse: Vec3,
    to_sun: Vec3,
    indoor: Option<Vec3>,
) -> Vec3 {
    match indoor {
        Some(c) => c * (0.7 + 0.3 * normal.y.max(0.0)),
        None => ambient + diffuse * normal.dot(to_sun).max(0.0),
    }
}

/// Colour each held gun's vertices by WoW's light at its holder, every frame (the hand turns).
fn light_held_guns(
    lighting: Option<Res<benilla_world::lighting::WowLighting>>,
    indoor: Query<&benilla_world::interior::ParticleLight>,
    parts: Query<(&HeldGunPart, &Mesh3d, &GlobalTransform)>,
    mut meshes: ResMut<Assets<Mesh>>,
) {
    let Some(l) = lighting else {
        return;
    };
    let (ambient, diffuse) = (Vec3::from_array(l.ambient), Vec3::from_array(l.diffuse));
    let to_sun = (-l.sun_dir).normalize_or_zero();
    for (part, mesh, gt) in &parts {
        let room = indoor.get(part.unit).ok().map(|p| Vec3::from_array(p.0));
        let rot = gt.compute_transform().rotation;
        let Some(m) = meshes.get_mut(&mesh.0) else {
            continue;
        };
        let colors: Vec<[f32; 4]> = part
            .normals
            .iter()
            .map(|n| {
                let c = held_gun_light((rot * *n).normalize_or_zero(), ambient, diffuse, to_sun, room);
                [c.x.min(1.5), c.y.min(1.5), c.z.min(1.5), 1.0]
            })
            .collect();
        if !colors.is_empty() {
            m.insert_attribute(Mesh::ATTRIBUTE_COLOR, colors);
        }
    }
}

/// The scale that makes a gun its real size in yards; the hand bone it hangs from carries the
/// body model's own scale, which `true_size` takes back out.
#[derive(Component)]
struct TrueSize(f32);

/// Keep each held gun its real size in the world, whatever the hand bone's scale.
fn true_size(
    mut roots: Query<(&TrueSize, &ChildOf, &mut Transform), With<NpcGunRoot>>,
    anchors: Query<&GlobalTransform>,
) {
    for (size, parent, mut tf) in &mut roots {
        let Ok(anchor) = anchors.get(parent.parent()) else {
            continue;
        };
        let bone = anchor.compute_transform().scale.max_element();
        if bone > 1e-4 {
            let want = Vec3::splat(size.0 / bone);
            if tf.scale.distance(want) > 1e-4 {
                tf.scale = want;
            }
        }
    }
}

/// The gun's turn that lays its barrel along the holder's facing, whatever the hand bone's own
/// turn: every race's hand is rigged differently, and a pistol held in it pointed up, back or at
/// the camera. `facing` is the holder's world rotation; `anchor` the hand anchor's.
pub fn aimed_turn(barrel: Vec3, facing: Quat, anchor: Quat) -> Quat {
    let along = Quat::from_rotation_arc(barrel.normalize_or(Vec3::NEG_Z), Vec3::NEG_Z);
    (anchor.inverse() * facing * along).normalize()
}

/// Where a held gun's root goes so that its grip (model frame) lands on the hand point: both in
/// the anchor's frame, the root turned by `turn` and scaled by `scale`.
pub fn grip_seat(hand: Vec3, turn: Quat, scale: Vec3, grip: Vec3) -> Vec3 {
    hand - turn * (scale * grip)
}

/// Keep each held gun's barrel along its holder's facing (crate::npc_guns `aimed_turn`); its
/// place stays the hand's. `MW2_NPC_GUN_TURN` set keeps the hand's own turn instead (a probe).
fn aim_held_guns(
    guns: Res<NpcGuns>,
    objects: Objects,
    globals: Query<&GlobalTransform>,
    mut roots: Query<(&ChildOf, &mut Transform), With<NpcGunRoot>>,
) {
    if std::env::var_os("MW2_NPC_GUN_TURN").is_some() {
        return;
    }
    for (guid, c) in &guns.held {
        let Some(unit) = objects.entity(*guid) else {
            continue;
        };
        let Ok((parent, mut tf)) = roots.get_mut(c.root) else {
            continue;
        };
        let (Ok(body), Ok(anchor)) = (globals.get(unit), globals.get(parent.parent())) else {
            continue;
        };
        let (_, facing, _) = body.to_scale_rotation_translation();
        let (_, hand, _) = anchor.to_scale_rotation_translation();
        let want = aimed_turn(c.barrel, facing, hand);
        // The grip, not the model's origin, on the hand point.
        let at = grip_seat(c.hand, want, tf.scale, c.grip);
        if tf.rotation.angle_between(want) > 1e-3 || tf.translation.distance(at) > 1e-4 {
            tf.rotation = want;
            tf.translation = at;
        }
    }
}

/// Each round: the rifle shot on the body, the flash and flight from the muzzle, the report.
#[allow(clippy::too_many_arguments)]
fn fire(
    mut guns: ResMut<NpcGuns>,
    objects: Objects,
    roots: Query<&GlobalTransform, With<NpcGunRoot>>,
    bodies: Query<&GlobalTransform, Without<NpcGunRoot>>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    mut anims: MessageWriter<PlayUnitAnim>,
    mut streaks: MessageWriter<crate::tracer::Streak>,
    mut sfx: MessageWriter<Sfx>,
) {
    let fires = std::mem::take(&mut guns.fires);
    let eye = cameras.iter().next().map(GlobalTransform::translation);
    for f in fires {
        let Some(unit) = objects.entity(f.guid) else {
            continue;
        };
        anims.write(PlayUnitAnim {
            entity: unit,
            anim_id: ANIM_ATTACK_RIFLE,
        });
        // From the muzzle of the gun in hand, else about chest high on the body.
        let muzzle = guns
            .held
            .get(&f.guid)
            .and_then(|c| {
                let m = guns.models.get(&c.gun)?.as_ref()?.muzzle;
                Some(roots.get(c.root).ok()?.transform_point(m))
            })
            .or_else(|| {
                bodies
                    .get(unit)
                    .ok()
                    .map(|g| g.translation() + Vec3::Y * 1.3)
            });
        let Some(start) = muzzle else {
            continue;
        };
        let to = crate::tracer::from_wow(f.to);
        if let Ok(dir) = Dir3::new(to - start) {
            // A round into us stops short of the eye rather than running into the camera.
            let end = eye.map_or((to - start).length(), |eye| {
                crate::tracer::streak_end(start, to, eye)
            });
            if end > 0.5 {
                streaks.write(crate::tracer::Streak {
                    start,
                    dir: *dir,
                    end,
                    enemy: true,
                });
            }
        }
        if let (Some(eye), Some(alias)) = (
            eye,
            guns.models
                .get(&f.gun)
                .and_then(Option::as_ref)
                .and_then(|m| m.fire_sound.clone()),
        ) {
            let gain = report_gain(eye.distance(start));
            if gain > 0.01 {
                sfx.write(Sfx::at(alias, gain));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_held_guns_grip_lands_on_the_hand() {
        let (hand, turn, scale, grip) = (
            Vec3::new(0.1, 0.2, 0.3),
            Quat::from_rotation_y(1.1),
            Vec3::splat(2.0),
            Vec3::new(0.05, -0.1, 0.0),
        );
        let root = super::grip_seat(hand, turn, scale, grip);
        assert!((root + turn * (scale * grip) - hand).length() < 1e-6);
    }


    #[test]
    fn small_races_hold_a_smaller_gun() {
        assert!(super::race_gun_scale(Some(7)) < 0.9);
        assert!(super::race_gun_scale(Some(3)) < 0.9);
        assert_eq!(super::race_gun_scale(Some(6)), 1.0);
        assert_eq!(super::race_gun_scale(None), 1.0);
    }

    #[test]
    fn a_held_gun_points_along_its_holders_facing_whatever_the_hand() {
        let barrel = Vec3::X;
        let facing = Quat::from_rotation_y(0.7);
        for hand in [Quat::IDENTITY, Quat::from_rotation_z(1.2), Quat::from_euler(EulerRot::YXZ, 2.0, 0.4, -1.0)] {
            let local = super::aimed_turn(barrel, facing, hand);
            let world = hand * local * barrel;
            assert!((world - facing * Vec3::NEG_Z).length() < 1e-4, "{world:?}");
        }
    }

    #[test]
    fn a_probe_gun_is_named_as_the_catalog_spells_it() {
        assert_eq!(super::probe_gun_name("M4"), "m4_mp");
        assert_eq!(super::probe_gun_name(" usp_mp "), "usp_mp");
    }

    use super::*;

    #[test]
    fn a_round_decodes_as_the_server_writes_it() {
        let mut b = vec![OP_NPC_FIRE];
        b.extend_from_slice(&0xF130_0000_0000_1234u64.to_le_bytes());
        b.extend_from_slice(&7u16.to_le_bytes());
        b.push(1);
        for v in [10.5f32, -20.0, 3.25] {
            b.extend_from_slice(&v.to_le_bytes());
        }
        assert_eq!(
            decode_fire(&b),
            Some(NpcFire {
                guid: 0xF130_0000_0000_1234,
                gun: 7,
                hit: true,
                to: [10.5, -20.0, 3.25],
            })
        );
        assert_eq!(decode_fire(&b[..23]), None);
    }

    #[test]
    fn the_gunner_list_decodes() {
        let mut b = vec![OP_NPC_GUNS, 2];
        for (g, n) in [(5u64, 2u16), (9, 30)] {
            b.extend_from_slice(&g.to_le_bytes());
            b.extend_from_slice(&n.to_le_bytes());
        }
        assert_eq!(decode_guns(&b), Some(vec![(5, 2), (9, 30)]));
        assert_eq!(decode_guns(&[OP_NPC_GUNS, 0]), Some(vec![])); // nobody near: clear
        assert_eq!(decode_guns(&b[..20]), None);
    }

    #[test]
    fn the_npc_gun_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((dress, fire));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }

    #[test]
    fn a_report_fades_with_distance() {
        assert_eq!(report_gain(0.0), 1.0);
        assert!(report_gain(30.0) > report_gain(60.0));
        assert_eq!(report_gain(HEARING + 1.0), 0.0);
    }
}
