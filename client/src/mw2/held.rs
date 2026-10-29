//! The character's own gear in the hands with the gun put away (crate::hands), in place of MW2's
//! knife: the main-hand weapon in the right fist, and in the left a shield or a second weapon,
//! each read from the player's WoW install (`ItemDisplayInfo.dbc` →
//! `Item\ObjectComponents\{Weapon,Shield}\<model>.m2` with its own skin), built into a mesh and
//! lit by the scene's light like the arms. A spell readied in a hand shows its WoW precast model
//! there (crate::spell_fx).

use bevy::asset::RenderAssetUsages;
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;

use benilla_assets::{LockRecover, WorldAssets, skin_url};
use benilla_formats::{CharSkinSlot, ItemDisplayCatalog, load_item_display_catalog};

use super::viewmodel::{ViewLight, Viewmodel};
use crate::hands::{Hand, Hands};
use crate::melee::{MeleeWeapon, OffHand};

const WEAPON_DIR: &str = "Item\\ObjectComponents\\Weapon";
const SHIELD_DIR: &str = "Item\\ObjectComponents\\Shield";

/// How long the drawn weapon is in the hand (MW2 inches): WoW's weapons are cartoonishly big, so
/// each is scaled to a believable length for its class.
pub fn held_length(subclass: u8, two_handed: bool) -> f32 {
    match (subclass, two_handed) {
        (15, _) => 13.0,                              // dagger
        (13, _) => 9.0,                               // fist weapon
        (6, _) | (10, _) => 52.0,                     // polearm, staff
        (20, _) => 48.0,                              // fishing pole (a Utility tool)
        (_, true) | (1, _) | (5, _) | (8, _) => 42.0, // two-handers
        _ => 30.0,                                    // one-handed sword, axe, mace
    }
}

/// A shield's size across (inches).
const SHIELD_SIZE: f32 = 18.0;

/// One built part of a held model: its mesh (WoW model space, yards) and material.
struct Part {
    mesh: Handle<Mesh>,
    material: Handle<StandardMaterial>,
    normals: Vec<[f32; 3]>,
}

/// One hand's model.
#[derive(Default)]
struct Model {
    /// The display and folder the parts were built for.
    built: Option<(u32, &'static str)>,
    parts: Vec<Part>,
    /// The model's size (yards), for its scale in the hand.
    length: f32,
    /// The model's own axes (crate::melee_pose::weapon_turn lays them along the grip): from
    /// the grip (its origin) to the tip, and across, the way its head or edge reaches.
    tip: Vec3,
    edge: Vec3,
    root: Option<Entity>,
}

#[derive(Resource, Default)]
pub struct HeldWeapon {
    catalog: Option<ItemDisplayCatalog>,
    /// 0 the right hand (the main hand), 1 the left (the off hand).
    models: [Model; 2],
}

/// The root of a drawn model (child of the viewmodel root, MW2 inches).
#[derive(Component)]
pub struct HeldRoot;

#[derive(Component)]
pub struct HeldPart;

/// What each hand's model is: the main hand's weapon; the off hand's shield or weapon.
fn wanted(melee: &MeleeWeapon) -> [Option<(u32, &'static str)>; 2] {
    let main = (melee.display != 0).then_some((melee.display, WEAPON_DIR));
    let off = match melee.off {
        OffHand::Weapon if melee.off_display != 0 => Some((melee.off_display, WEAPON_DIR)),
        OffHand::Shield if melee.off_display != 0 => Some((melee.off_display, SHIELD_DIR)),
        _ => None,
    };
    [main, off]
}

/// Build each hand's model when its item changes.
#[allow(clippy::too_many_arguments)]
pub fn build_held(
    melee: Res<MeleeWeapon>,
    mut held: ResMut<HeldWeapon>,
    world: Option<Res<WorldAssets>>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut commands: Commands,
) {
    let want = wanted(&melee);
    if held.models.iter().zip(&want).all(|(m, w)| m.built == *w) {
        return;
    }
    let Some(world) = world else {
        return;
    };
    if held.catalog.is_none() {
        let mut chain = world.chain.lock_recover();
        match load_item_display_catalog(&mut chain) {
            Ok(c) => held.catalog = Some(c),
            Err(e) => {
                warn!("benilla-mw2: no ItemDisplayInfo: {e:#}");
                return;
            }
        }
    }
    for (slot, w) in want.into_iter().enumerate() {
        if held.models[slot].built == w {
            continue;
        }
        if let Some(root) = held.models[slot].root.take() {
            commands.entity(root).despawn();
        }
        let mut model = Model {
            built: w,
            ..default()
        };
        if let Some((display, dir)) = w {
            let catalog = held.catalog.as_ref().expect("loaded above");
            build_model(
                catalog,
                &world,
                display,
                dir,
                &assets,
                &mut meshes,
                &mut materials,
                &mut model,
            );
        }
        held.models[slot] = model;
    }
}

#[allow(clippy::too_many_arguments)]
fn build_model(
    catalog: &ItemDisplayCatalog,
    world: &WorldAssets,
    display_id: u32,
    dir: &'static str,
    assets: &AssetServer,
    meshes: &mut Assets<Mesh>,
    materials: &mut Assets<StandardMaterial>,
    out: &mut Model,
) {
    let Some(row) = catalog.get(display_id).cloned() else {
        warn!("benilla-mw2: no display row {display_id}");
        return;
    };
    let Some(model) = row.model[0].clone() else {
        return;
    };
    let path = format!("{dir}\\{model}");
    let bytes = match world.chain.lock_recover().read(&path) {
        Ok(b) => b,
        Err(e) => {
            warn!("benilla-mw2: held model {path}: {e:#}");
            return;
        }
    };
    let submeshes = match benilla_formats::parse_m2_render_submeshes(&bytes, dir, &[]) {
        Ok(s) => s,
        Err(e) => {
            warn!("benilla-mw2: held model {path}: {e:#}");
            return;
        }
    };
    let skin = row.model_texture[0].clone();
    let (mut lo, mut hi) = (Vec3::splat(f32::MAX), Vec3::splat(f32::MIN));
    let mut points: Vec<Vec3> = Vec::new();
    for sub in submeshes {
        // Glow cards and additive sheen are effects, not the weapon.
        if sub.billboard.is_some()
            || !matches!(
                sub.blend,
                benilla_formats::ModelBlend::Opaque | benilla_formats::ModelBlend::AlphaTest
            )
        {
            continue;
        }
        let texture = if sub.char_slot == Some(CharSkinSlot::Object) {
            skin.as_deref().map(|s| skin_url(dir, s))
        } else {
            sub.texture.as_deref().map(|t| {
                let t = t.to_ascii_lowercase().replace('\\', "/");
                let t = t.strip_suffix(".blp").unwrap_or(&t).to_string();
                format!("mpq://{t}.blp")
            })
        };
        for p in &sub.positions {
            let v = Vec3::from_array(*p);
            lo = lo.min(v);
            hi = hi.max(v);
            points.push(v);
        }
        let n = sub.positions.len();
        let normals = if sub.normals.len() == n {
            sub.normals.clone()
        } else {
            vec![[0.0, 0.0, 1.0]; n]
        };
        let mut mesh = Mesh::new(
            PrimitiveTopology::TriangleList,
            RenderAssetUsages::default(),
        );
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, sub.positions.clone());
        mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, normals.clone());
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, sub.uvs.clone());
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, vec![[1.0f32; 4]; n]);
        mesh.insert_indices(Indices::U32(sub.indices.clone()));
        let material = materials.add(StandardMaterial {
            base_color_texture: texture.map(|t| assets.load(t)),
            unlit: true,
            alpha_mode: if sub.blend == benilla_formats::ModelBlend::AlphaTest {
                AlphaMode::Mask(0.5)
            } else {
                AlphaMode::Opaque
            },
            cull_mode: if sub.two_sided {
                None
            } else {
                Some(bevy::render::render_resource::Face::Back)
            },
            ..default()
        });
        out.parts.push(Part {
            mesh: meshes.add(mesh),
            material,
            normals,
        });
    }
    out.length = (hi - lo).max_element().max(0.05);
    (out.tip, out.edge) = model_axes(&points);
    info!(
        "benilla-mw2: in hand: {model} ({} parts, {:.2} yd)",
        out.parts.len(),
        out.length
    );
}

/// A spell school's colour (`Spell.dbc` School: 0 physical, 1 holy, 2 fire, 3 nature, 4 frost,
/// 5 shadow, 6 arcane), for the charge bar (crate::spellcast).
pub fn school_color(school: u32) -> Color {
    match school {
        1 => Color::srgb(1.0, 0.9, 0.45),
        2 => Color::srgb(1.0, 0.45, 0.1),
        3 => Color::srgb(0.35, 1.0, 0.35),
        4 => Color::srgb(0.45, 0.8, 1.0),
        5 => Color::srgb(0.55, 0.2, 0.75),
        6 => Color::srgb(0.9, 0.5, 1.0),
        _ => Color::srgb(0.95, 0.95, 0.95),
    }
}

/// Hold each hand's model where its grip is (crate::melee_pose) while the hands are up, the
/// main hand's in the knife hand for a shield bash; light it like the arms.
#[allow(clippy::too_many_arguments)]
pub fn place_held(
    mut commands: Commands,
    melee: Res<MeleeWeapon>,
    hands: Res<Hands>,
    mut held: ResMut<HeldWeapon>,
    light: Res<ViewLight>,
    mut meshes: ResMut<Assets<Mesh>>,
    up: Res<crate::melee::MeleeHeld>,
    viewmodels: Query<(Entity, &Viewmodel)>,
    mut roots: Query<(&mut Transform, &mut Visibility), With<HeldRoot>>,
) {
    // The right hand's gun (akimbo: the right one).
    let Some((vm_entity, vm)) = viewmodels.iter().find(|(_, vm)| vm.hand().0 == 0) else {
        return;
    };
    let (Some(wrist), knife_swing) = vm.knife_hand() else {
        return;
    };
    let held = &mut *held;
    for slot in 0..2 {
        let model = &mut held.models[slot];
        if model.parts.is_empty() {
            continue;
        }
        // Where this hand's model goes: the right hand's grip while it holds the weapon, the
        // left's while it holds a shield or a second weapon (not a two-hander's other hand).
        let grip = if !up.0 {
            None
        } else if slot == 0 {
            vm.melee_grip().filter(|_| hands.right == Hand::Weapon)
        } else {
            vm.left_grip
                .filter(|_| matches!(hands.left, Hand::Weapon | Hand::Shield) && !hands.two_handed)
                .map(|g| {
                    if hands.left == Hand::Shield {
                        // A shield's face looks ahead: its width across the view.
                        crate::melee_pose::Grip {
                            edge: (Vec3::X - g.blade * g.blade.x).normalize_or(Vec3::X),
                            ..g
                        }
                    } else {
                        g
                    }
                })
        };
        // A shield bash (not the hands) shows the main weapon in MW2's knife hand.
        let bash = slot == 0 && !up.0 && knife_swing;
        let shown = grip.is_some() || bash;
        let root = match model.root {
            Some(r) if roots.get(r).is_ok() => r,
            _ => {
                let r = commands
                    .spawn((
                        Name::new(if slot == 0 {
                            "mw2 held main hand"
                        } else {
                            "mw2 held off hand"
                        }),
                        HeldRoot,
                        Transform::default(),
                        Visibility::Hidden,
                        ChildOf(vm_entity),
                    ))
                    .id();
                for p in &model.parts {
                    commands.spawn((
                        HeldPart,
                        Mesh3d(p.mesh.clone()),
                        MeshMaterial3d(p.material.clone()),
                        Transform::default(),
                        ChildOf(r),
                    ));
                }
                model.root = Some(r);
                continue;
            }
        };
        let Ok((mut tf, mut vis)) = roots.get_mut(root) else {
            continue;
        };
        let want = if shown {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *vis != want {
            *vis = want;
        }
        if !shown {
            continue;
        }
        // Scaled from the model's yards to a believable size in inches.
        let size = if slot == 1 && melee.off == OffHand::Shield {
            SHIELD_SIZE
        } else if slot == 1 {
            held_length(melee.off_subclass, false)
        } else {
            held_length(melee.subclass, melee.two_handed)
        };
        let scale = size / model.length.max(0.05);
        if let Some(g) = grip {
            // The model's grip in the fist, its tip along the blade, its edge leading.
            let turn = crate::melee_pose::weapon_turn(model.tip, model.edge, &g);
            *tf = Transform {
                translation: g.at,
                rotation: turn,
                scale: Vec3::splat(scale),
            };
        } else {
            let Some(hand) = vm.bone_in_root(wrist) else {
                continue;
            };
            // In the knife hand: the grip at the wrist's fingers side, the blade along the hand's
            // forward axis (MW2's bone x).
            let grip = Mat4::from_translation(Vec3::new(3.2, 0.0, -0.6))
                * Mat4::from_quat(grip_turn())
                * Mat4::from_scale(Vec3::splat(scale));
            *tf = Transform::from_matrix(hand * grip);
        }
        // Lit as the arms are: the scene's light in the eye's frame.
        let turn = tf.rotation;
        for p in &model.parts {
            if let Some(mesh) = meshes.get_mut(&p.mesh) {
                let colors: Vec<[f32; 4]> = p
                    .normals
                    .iter()
                    .map(|n| light.lit((turn * Vec3::from_array(*n)).normalize_or_zero()))
                    .collect();
                mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, colors);
            }
        }
    }
}

/// A weapon model's axes, from its points (the grip at the origin): the tip is the farthest
/// point, the edge the way the model reaches farthest across it (a sword's guard, an axe's
/// head). WoW draws its weapons point up its z when none reach farther.
pub fn model_axes(points: &[Vec3]) -> (Vec3, Vec3) {
    let tip = points
        .iter()
        .copied()
        .max_by(|a, b| a.length_squared().total_cmp(&b.length_squared()))
        .and_then(|p| p.try_normalize())
        .unwrap_or(Vec3::Z);
    let across = |p: Vec3| p - tip * p.dot(tip);
    let edge = points
        .iter()
        .map(|&p| across(p))
        .max_by(|a, b| a.length_squared().total_cmp(&b.length_squared()))
        .and_then(|p| p.try_normalize())
        .unwrap_or_else(|| tip.any_orthonormal_vector());
    (tip, edge)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_models_tip_and_edge_come_from_its_shape() {
        // A sword standing up its x: the blade to 1.0, the guard across its y, an axe-like bump
        // reaching along -y farther.
        let points = [
            Vec3::new(-0.2, 0.0, 0.0),
            Vec3::new(1.0, 0.0, 0.0),
            Vec3::new(0.1, 0.15, 0.0),
            Vec3::new(0.1, -0.3, 0.0),
            Vec3::new(0.5, 0.0, 0.05),
        ];
        let (tip, edge) = model_axes(&points);
        assert!(tip.distance(Vec3::X) < 1e-5);
        assert!(edge.distance(Vec3::NEG_Y) < 1e-5);
        assert_eq!(model_axes(&[]).0, Vec3::Z);
    }

    #[test]
    fn weapons_hold_at_their_class_length() {
        assert!(held_length(15, false) < held_length(7, false)); // a dagger is short
        assert!(held_length(7, false) < held_length(8, true)); // a two-hander is long
        assert_eq!(held_length(4, false), held_length(0, false)); // one-handers alike
    }

    #[test]
    fn each_hand_builds_what_it_holds() {
        let mut m = MeleeWeapon {
            display: 10,
            off: OffHand::Shield,
            off_display: 20,
            ..Default::default()
        };
        assert_eq!(wanted(&m), [Some((10, WEAPON_DIR)), Some((20, SHIELD_DIR))]);
        m.off = OffHand::Held; // a book: nothing drawn, the hand is free
        assert_eq!(wanted(&m)[1], None);
        m.off = OffHand::Weapon;
        assert_eq!(wanted(&m)[1], Some((20, WEAPON_DIR)));
    }

    #[test]
    fn schools_glow_their_own_colours() {
        assert_ne!(school_color(2), school_color(4)); // fire and frost
        assert_eq!(school_color(0), school_color(99)); // physical or unknown: white
    }
}

/// The weapon's turn in the fist: the blade along the hand's forward axis.
/// `MW2_HELD_TURN=yaw,pitch,roll` (degrees, about the wrist's y, x, z) tries others.
fn grip_turn() -> Quat {
    let parts: Vec<f32> = std::env::var("MW2_HELD_TURN")
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
        _ => Quat::from_rotation_y(-std::f32::consts::FRAC_PI_2),
    }
}
