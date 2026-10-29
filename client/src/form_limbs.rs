//! A form's own front limbs in first person (crate::forms): paws, hooves, talons, a spider's front
//! legs, a worgen's clawed hands. In the spirit of the humanoid's arms (crate::mw2::wow_arms),
//! they are the player's own model's triangles: the creature model the form wears, cut to the
//! bones of its two forelimbs (`forms::find_forelimbs` on the model's skeleton: its hand points,
//! else its shoulder key bones, else its two foremost feet, up to below the chest) and skinned on the CPU from the very palette rows the GPU skins the
//! body with, so a swipe, a pounce or a cast moves them as the model's own animation does.
//!
//! benilla's first-person fade hides the whole model at the eye. The copy is drawn in front of the
//! eye instead: the limbs' shoulders are brought to a point below and ahead of it (scaled to one
//! reach, so a bear's paws and a cat's fill the view alike) and turned with the look up and down.
//! They show only in action, while the model plays an attack, a cast or a special
//! ([`is_action_anim`]) or the swipe was just pressed; otherwise the view is clear. The worgen's
//! hands stay up at rest too.

use std::hash::{Hash, Hasher};

use benilla_app::Objects;
use benilla_app::fusion::UnitAttach;
use benilla_assets::ModelAnimations;
use benilla_assets::materials::WowModelMaterial;
use benilla_world::interact::PickMesh;
use benilla_world::interior::InteriorLit;
use benilla_world::model_fade::FadeMaterials;
use benilla_world::rig_anim::RigPose;
use benilla_world::rig_palette::{RigPalettes, RigPalettesWritten};
use benilla_world::view::WorldCamera;
use bevy::asset::RenderAssetUsages;
use bevy::camera::visibility::NoFrustumCulling;
use bevy::mesh::{Indices, MeshTag, PrimitiveTopology};
use bevy::prelude::*;

use crate::forms::{
    LimbSkeleton, LimbSource, PlayerForm, find_forelimbs, forelimb_bones, keeps_limb_triangle,
};
use crate::mode::FpsState;

/// M2 attachment points: the right and the left hand, and the head.
pub const ATTACH_HAND_RIGHT: u16 = 1;
pub const ATTACH_HAND_LEFT: u16 = 2;
pub const ATTACH_HEAD: u16 = 11;

/// Where the limbs' shoulders sit, in the level frame of the eye (yards): ahead, and below; just
/// inside the bottom edge of the view (18 degrees down of `CAM_FOVY`'s 22.5), so a paw raised in
/// a swipe or a cast comes up into it and one hanging at rest drops out.
pub const AHEAD: f32 = 0.45;
pub const BELOW: f32 = 0.15;
/// The reach a limb is scaled to (yards), and the bounds of that scale.
pub const REACH: f32 = 0.5;
const SCALE_MIN: f32 = 0.15;
const SCALE_MAX: f32 = 1.6;
/// How long the limbs stay after the action that raised them (seconds), and after a swipe press.
pub const LINGER: f32 = 0.8;
pub const SWIPE_SHOW: f32 = 1.2;
/// An action animation counts once it has this much weight.
const ACTION_WEIGHT: f32 = 0.05;

/// `AnimationData.dbc` ids that put the front limbs in view: the attacks, the casts, the
/// specials, a roar, a shield bash.
pub fn is_action_anim(id: u16) -> bool {
    matches!(
        id,
        2           // Spell
        | 16..=19   // AttackUnarmed, Attack1H, Attack2H, Attack2HL
        | 31..=33   // SpellPrecast, SpellCast, SpellCastArea
        | 46 | 47   // AttackBow, FireBow
        | 49        // AttackRifle
        | 53..=55   // SpellCastDirected, SpellCastOmni, BattleRoar
        | 57..=59   // Special1H, Special2H, ShieldBash
    )
}

/// What decides whether the limbs are drawn.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct LimbView {
    pub active: bool,
    pub third_person: bool,
    pub killcam: bool,
    pub dead: bool,
    /// The form has limbs to show and the model carries them.
    pub has_limbs: bool,
    /// Shown at rest too (the worgen).
    pub always: bool,
    /// An action is on (an attack, a cast, a swipe just pressed).
    pub in_action: bool,
}

/// The limbs show in first person, alive, in a form with limbs, in action (or always).
pub fn limbs_shown(v: LimbView) -> bool {
    v.active && !v.third_person && !v.killcam && !v.dead && v.has_limbs && (v.always || v.in_action)
}

/// The limb scale that brings a limb of `reach` yards to [`REACH`].
pub fn limb_scale(reach: f32) -> f32 {
    if reach <= 1e-4 {
        1.0
    } else {
        (REACH / reach).clamp(SCALE_MIN, SCALE_MAX)
    }
}

/// One posed point of the limbs into the view: `p` (world) about the shoulders' `anchor` (world),
/// scaled by `k`, set below and ahead of the eye in its level frame (`level_fwd`), then turned
/// with the look (`look`, the pitch from level).
pub fn place(p: Vec3, anchor: Vec3, k: f32, eye: Vec3, level_fwd: Vec3, look: Quat) -> Vec3 {
    let level = eye + level_fwd * AHEAD - Vec3::Y * BELOW + (p - anchor) * k;
    eye + look * (level - eye)
}

/// One part's limb copy.
struct LimbCopy {
    source: Entity,
    copy: Entity,
    mesh: Handle<Mesh>,
    positions: Vec<Vec3>,
    normals: Vec<Vec3>,
    joints: Vec<[u16; 4]>,
    weights: Vec<[f32; 4]>,
    tris: Vec<[u32; 3]>,
    /// The vertex sits on a limb's root (the shoulder): the anchor's.
    at_root: Vec<bool>,
}

/// The form's limb copies, rebuilt when the model or the form changes.
#[derive(Resource, Default)]
pub struct FormLimbs {
    parts: Vec<LimbCopy>,
    signature: u64,
    /// The limbs' scale ([`limb_scale`]).
    pub scale: f32,
    /// Shown until (elapsed seconds).
    pub action_until: f32,
    /// Drawn this frame.
    pub shown: bool,
}

#[derive(Component)]
pub struct FormLimbPart;

/// Each form display's front limbs as its M2 file has them ([`find_forelimbs`]), read once.
#[derive(Resource, Default)]
pub struct LimbModels {
    /// `CreatureDisplayInfo` to model, loaded on the first form (`None` inside: no install).
    catalog: Option<Option<benilla_formats::CreatureCatalog>>,
    by_display: std::collections::HashMap<u32, Option<[Vec<bool>; 2]>>,
}

impl LimbModels {
    /// The display's limb bones, right then left, by its model's skeleton.
    fn limbs(&mut self, world: &benilla_assets::WorldAssets, display_id: u32) -> Option<[Vec<bool>; 2]> {
        use benilla_assets::LockRecover;
        if let Some(l) = self.by_display.get(&display_id) {
            return l.clone();
        }
        let mut chain = world.chain.lock_recover();
        let catalog = self
            .catalog
            .get_or_insert_with(|| benilla_formats::load_creature_catalog(&mut chain).ok());
        let found = catalog.as_ref().and_then(|c| c.model(display_id)).and_then(|m| {
            let path = m.model_path.replace(".mdx", ".m2").replace(".MDX", ".m2");
            let bytes = chain.read_file(&path).ok()?;
            let skel = benilla_formats::parse_m2_skeleton(&bytes).ok()?;
            let attach = benilla_formats::parse_m2_attachments(&bytes).unwrap_or_default();
            let point = |id: u16| attach.iter().find(|a| a.id == id).map(|a| usize::from(a.bone));
            let parents: Vec<i16> = skel.bones.iter().map(|b| b.parent).collect();
            let pivots: Vec<[f32; 3]> = skel.bones.iter().map(|b| b.pivot).collect();
            let key_bones: Vec<i16> = skel.bones.iter().map(|b| b.key_bone).collect();
            let (limbs, source) = find_forelimbs(&LimbSkeleton {
                parents: &parents,
                pivots: &pivots,
                key_bones: &key_bones,
                hands: [point(ATTACH_HAND_RIGHT), point(ATTACH_HAND_LEFT)],
                head: point(ATTACH_HEAD),
            });
            info!("benilla-mw2: display {} ({path}): front limbs from {source:?}", display_id);
            (source != LimbSource::None).then_some(limbs)
        });
        self.by_display.insert(display_id, found.clone());
        found
    }
}

pub struct FormLimbsPlugin;

impl Plugin for FormLimbsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<FormLimbs>()
            .init_resource::<LimbModels>()
            .add_systems(Update, (gather, watch_actions).chain().after(crate::Mw2Set::Hud))
            .add_systems(PostUpdate, skin.after(RigPalettesWritten));
    }
}

/// Build the limb copies from the form's model, again when it changes; none outside a form.
#[allow(clippy::type_complexity)]
fn gather(
    mut commands: Commands,
    objects: Objects,
    form: Res<PlayerForm>,
    roots: Query<(&RigPose, &Children)>,
    parts: Query<(Entity, &PickMesh, &FadeMaterials, &MeshTag)>,
    attach: UnitAttach,
    mut meshes: ResMut<Assets<Mesh>>,
    mut limbs: ResMut<FormLimbs>,
    (state, world, mut models): (
        Res<FpsState>,
        Option<Res<benilla_assets::WorldAssets>>,
        ResMut<LimbModels>,
    ),
) {
    let me = objects.player();
    let wanted = form.rules.and_then(|r| r.limbs).is_some();
    let source = me.filter(|_| wanted).and_then(|me| roots.get(me).ok().map(|r| (me, r)));
    let Some((me, (pose, children))) = source else {
        if !limbs.parts.is_empty() || limbs.signature != 0 {
            for p in limbs.parts.drain(..) {
                commands.entity(p.copy).despawn();
            }
            limbs.signature = 0;
        }
        return;
    };
    // A form's model is a creature's: every part of it, no character skin slots.
    let sources: Vec<(Entity, &PickMesh, &FadeMaterials, &MeshTag)> = children
        .iter()
        .filter_map(|c| parts.get(c).ok())
        .filter(|(_, p, ..)| p.0.char_slot.is_none())
        .collect();
    let mut h = std::collections::hash_map::DefaultHasher::new();
    me.hash(&mut h);
    form.form.hash(&mut h);
    for (e, p, fm, _) in &sources {
        e.hash(&mut h);
        p.0.positions.len().hash(&mut h);
        fm.cutout.id().hash(&mut h);
    }
    let signature = h.finish();
    if signature == limbs.signature {
        return;
    }
    for p in limbs.parts.drain(..) {
        commands.entity(p.copy).despawn();
    }
    limbs.signature = signature;
    if sources.is_empty() {
        return;
    }
    let point = |id: u16| attach.point(me, id).map(|(b, _)| usize::from(b));
    let hands = [point(ATTACH_HAND_RIGHT), point(ATTACH_HAND_LEFT)];
    // The model file's skeleton (key bones, bind pivots) finds the limbs of a model with no hand
    // points; the live rig's parents and points are the fallback.
    let display = state
        .self_guid
        .and_then(|g| objects.object(g))
        .and_then(|f| f.unit_displayid())
        .and_then(|d| u32::try_from(d).ok());
    let from_file = display.zip(world.as_deref()).and_then(|(d, w)| {
        models
            .limbs(w, d)
            .filter(|l| l[0].len() == pose.parents.len())
    });
    let [right, left] =
        from_file.unwrap_or_else(|| forelimb_bones(&pose.parents, hands, point(ATTACH_HEAD)));
    let in_limb = |b: usize| right.get(b).copied().unwrap_or(false) || left.get(b).copied().unwrap_or(false);
    let is_root = |b: usize| {
        in_limb(b)
            && pose
                .parents
                .get(b)
                .and_then(|&p| usize::try_from(p).ok())
                .is_none_or(|p| !in_limb(p))
    };
    if !(0..pose.parents.len()).any(in_limb) {
        warn_once!("benilla-mw2: no front limbs found on this form's model; none are drawn");
        return;
    }
    let mut kept_tris = 0;
    let mut anchor_bind = Vec::new();
    let mut all_bind = Vec::new();
    for (source, pick, fm, tag) in sources {
        let sub = &pick.0;
        let n = sub.positions.len();
        let corner = |i: u32| {
            let i = i as usize;
            (
                sub.joints.get(i).copied().unwrap_or_default(),
                sub.weights.get(i).copied().unwrap_or_default(),
            )
        };
        let tris: Vec<[u32; 3]> = sub
            .indices
            .as_chunks::<3>()
            .0
            .iter()
            .map(|t| [t[0], t[1], t[2]])
            .filter(|t| keeps_limb_triangle(t.map(corner), &in_limb))
            .collect();
        if tris.is_empty() {
            continue;
        }
        kept_tris += tris.len();
        let positions: Vec<Vec3> = sub
            .positions
            .iter()
            .map(|p| benilla_assets::coords::wow_to_bevy(*p))
            .collect();
        let normals: Vec<Vec3> = (0..n)
            .map(|i| {
                sub.normals
                    .get(i)
                    .map_or(Vec3::Y, |v| benilla_assets::coords::wow_to_bevy(*v))
            })
            .collect();
        let joints: Vec<[u16; 4]> = (0..n).map(|i| corner(i as u32).0).collect();
        let weights: Vec<[f32; 4]> = (0..n).map(|i| corner(i as u32).1).collect();
        let at_root: Vec<bool> = joints
            .iter()
            .zip(&weights)
            .map(|(j, w)| {
                j.iter()
                    .zip(w)
                    .filter(|(j, _)| is_root(usize::from(**j)))
                    .map(|(_, w)| *w)
                    .sum::<f32>()
                    >= 0.5
            })
            .collect();
        for t in &tris {
            for &v in t {
                let p = positions[v as usize];
                all_bind.push(p);
                if at_root[v as usize] {
                    anchor_bind.push(p);
                }
            }
        }
        let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        mesh.insert_attribute(
            Mesh::ATTRIBUTE_POSITION,
            positions.iter().map(|p| p.to_array()).collect::<Vec<_>>(),
        );
        mesh.insert_attribute(
            Mesh::ATTRIBUTE_NORMAL,
            normals.iter().map(|p| p.to_array()).collect::<Vec<_>>(),
        );
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, sub.uvs.clone());
        if sub.vertex_colors.len() == n {
            mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, sub.vertex_colors.clone());
        }
        mesh.insert_indices(Indices::U32(tris.iter().flatten().copied().collect()));
        let handle = meshes.add(mesh);
        let copy = commands
            .spawn((
                Name::new("mw2 form limbs"),
                FormLimbPart,
                Mesh3d(handle.clone()),
                MeshMaterial3d::<WowModelMaterial>(fm.cutout.clone()),
                MeshTag(benilla_world::mesh_tag::with_alpha(tag.0, 1.0)),
                Transform::IDENTITY,
                Visibility::Hidden,
                NoFrustumCulling,
            ))
            .id();
        limbs.parts.push(LimbCopy {
            source,
            copy,
            mesh: handle,
            positions,
            normals,
            joints,
            weights,
            tris,
            at_root,
        });
    }
    // The reach at rest: the farthest limb vertex from the shoulders.
    let mean = |v: &[Vec3]| v.iter().copied().sum::<Vec3>() / v.len().max(1) as f32;
    let anchor = if anchor_bind.is_empty() { mean(&all_bind) } else { mean(&anchor_bind) };
    let reach = all_bind.iter().map(|p| p.distance(anchor)).fold(0.0f32, f32::max);
    limbs.scale = limb_scale(reach);
    info!(
        "benilla-mw2: form {} limbs: {} parts, {kept_tris} triangles, reach {reach:.2} yd, scale {:.2}",
        form.form,
        limbs.parts.len(),
        limbs.scale
    );
}

/// Keep the limbs up while the model plays an action, and from a swipe's press.
fn watch_actions(
    time: Res<Time>,
    objects: Objects,
    form: Res<PlayerForm>,
    players: Query<(&ModelAnimations, &AnimationPlayer)>,
    mut limbs: ResMut<FormLimbs>,
) {
    let now = time.elapsed_secs();
    if form.swipe {
        limbs.action_until = limbs.action_until.max(now + SWIPE_SHOW);
    }
    let Some((anims, player)) = objects.player().and_then(|e| players.get(e).ok()) else {
        return;
    };
    let acting = player.playing_animations().any(|(node, active)| {
        active.weight() > ACTION_WEIGHT
            && anims.clips.iter().any(|c| {
                is_action_anim(c.anim_id)
                    && (c.node == *node
                        || c.upper_node == Some(*node)
                        || c.arm_nodes.is_some_and(|(r, l)| r == *node || l == *node))
            })
    });
    if acting {
        limbs.action_until = limbs.action_until.max(now + LINGER);
    }
}

/// The inputs of [`limbs_shown`].
#[derive(bevy::ecs::system::SystemParam)]
struct ViewInputs<'w, 's> {
    time: Res<'w, Time>,
    state: Res<'w, FpsState>,
    third: Res<'w, crate::thirdperson::ThirdPerson>,
    form: Res<'w, PlayerForm>,
    objects: Objects<'w, 's>,
}

impl ViewInputs<'_, '_> {
    fn view(&self, limbs: &FormLimbs) -> LimbView {
        let me = self.state.self_guid.and_then(|g| self.objects.object(g));
        LimbView {
            active: self.state.active,
            third_person: self.third.on,
            killcam: self.state.view_away(),
            dead: me.is_some_and(|f| f.is_dead_or_ghost() || f.unit_health() == Some(0)),
            has_limbs: !limbs.parts.is_empty() && self.form.rules.and_then(|r| r.limbs).is_some(),
            always: self.form.rules.is_some_and(|r| r.limbs_always),
            in_action: self.time.elapsed_secs() < limbs.action_until,
        }
    }
}

/// Skin each copy from this frame's palette rows and set it before the eye.
#[allow(clippy::type_complexity)]
fn skin(
    inputs: ViewInputs,
    palettes: Res<RigPalettes>,
    camera: Query<&Transform, (With<WorldCamera>, Without<FormLimbPart>)>,
    sources: Query<(&MeshTag, &FadeMaterials, Option<&InteriorLit>), Without<FormLimbPart>>,
    mut copies: Query<
        (
            &mut Transform,
            &mut GlobalTransform,
            &mut Visibility,
            &mut MeshTag,
            &mut MeshMaterial3d<WowModelMaterial>,
        ),
        With<FormLimbPart>,
    >,
    mut meshes: ResMut<Assets<Mesh>>,
    mut limbs: ResMut<FormLimbs>,
) {
    let limbs = &mut *limbs;
    let cam = camera
        .single()
        .ok()
        .filter(|_| limbs_shown(inputs.view(limbs)));
    // One body, one rig slot.
    let rig = cam.and_then(|_| {
        limbs.parts.iter().find_map(|p| {
            let (tag, ..) = sources.get(p.source).ok()?;
            let slot = benilla_world::mesh_tag::rig_of(tag.0);
            (slot != 0).then_some(())?;
            Some((palettes.rig_rows(slot, usize::MAX)?, palettes.slot_origin(slot)?))
        })
    });
    let (Some(cam), Some((rows, origin))) = (cam, rig) else {
        limbs.shown = false;
        for p in &limbs.parts {
            if let Ok((_, _, mut vis, ..)) = copies.get_mut(p.copy)
                && *vis != Visibility::Hidden
            {
                *vis = Visibility::Hidden;
            }
        }
        return;
    };
    let skin_at = |p: Vec3, j: [u16; 4], w: [f32; 4]| {
        let mut sum = Vec3::ZERO;
        let mut total = 0.0;
        for (j, w) in j.iter().zip(w) {
            if w > 0.0
                && let Some(m) = rows.get(usize::from(*j))
            {
                sum += m.transform_point3(p) * w;
                total += w;
            }
        }
        (total > 0.0).then(|| sum / total)
    };
    // Posed, rig-relative.
    let posed: Vec<Vec<Vec3>> = limbs
        .parts
        .iter()
        .map(|p| {
            (0..p.positions.len())
                .map(|i| skin_at(p.positions[i], p.joints[i], p.weights[i]).unwrap_or(Vec3::ZERO))
                .collect()
        })
        .collect();
    let (mut at_root, mut all) = (Vec3::ZERO, Vec3::ZERO);
    let (mut n_root, mut n_all) = (0usize, 0usize);
    for (p, pts) in limbs.parts.iter().zip(&posed) {
        for t in &p.tris {
            for &v in t {
                let q = pts[v as usize];
                all += q;
                n_all += 1;
                if p.at_root[v as usize] {
                    at_root += q;
                    n_root += 1;
                }
            }
        }
    }
    let anchor = if n_root > 0 { at_root / n_root as f32 } else { all / n_all.max(1) as f32 };
    let fwd = *cam.forward();
    let level_fwd = Vec3::new(fwd.x, 0.0, fwd.z).try_normalize().unwrap_or(Vec3::NEG_Z);
    let look = Quat::from_rotation_arc(level_fwd, fwd);
    // Rig-relative throughout: the eye too.
    let eye = cam.translation - origin;
    let k = limbs.scale;
    limbs.shown = true;
    for (p, pts) in limbs.parts.iter().zip(posed) {
        let Ok((mut tf, mut gt, mut vis, mut tag, mut mat)) = copies.get_mut(p.copy) else {
            continue;
        };
        let Ok((src_tag, fm, lit)) = sources.get(p.source) else {
            if *vis != Visibility::Hidden {
                *vis = Visibility::Hidden;
            }
            continue;
        };
        let placed: Vec<Vec3> = pts.iter().map(|&q| place(q, anchor, k, eye, level_fwd, look)).collect();
        let normals: Vec<[f32; 3]> = (0..p.normals.len())
            .map(|i| {
                let main = p.joints[i]
                    .iter()
                    .zip(p.weights[i])
                    .filter(|(_, w)| *w > 0.0)
                    .max_by(|a, b| a.1.total_cmp(&b.1))
                    .and_then(|(j, _)| rows.get(usize::from(*j)));
                main.map_or(Vec3::Y, |m| (look * m.transform_vector3(p.normals[i])).normalize_or_zero())
                    .to_array()
            })
            .collect();
        let indices: Vec<u32> = p
            .tris
            .iter()
            .filter(|t| !crate::fp_body::too_near(t.map(|v| placed[v as usize]), eye))
            .flatten()
            .copied()
            .collect();
        if let Some(mesh) = meshes.get_mut(&p.mesh) {
            mesh.insert_attribute(
                Mesh::ATTRIBUTE_POSITION,
                placed.iter().map(|v| v.to_array()).collect::<Vec<_>>(),
            );
            mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, normals);
            mesh.insert_indices(Indices::U32(indices));
        }
        let at = Transform::from_translation(origin);
        if *tf != at {
            *tf = at;
        }
        *gt = GlobalTransform::from(at);
        let bits = benilla_world::mesh_tag::with_alpha(src_tag.0, 1.0);
        if tag.0 != bits {
            tag.0 = bits;
        }
        let want = fm.material_for(lit, false);
        if mat.0 != *want {
            mat.0 = want.clone();
        }
        if *vis != Visibility::Inherited {
            *vis = Visibility::Inherited;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_limbs_show_only_in_action_unless_always() {
        let v = LimbView {
            active: true,
            has_limbs: true,
            ..LimbView::default()
        };
        // At rest the view is clear.
        assert!(!limbs_shown(v));
        assert!(limbs_shown(LimbView { in_action: true, ..v }));
        // The worgen's hands stay up.
        assert!(limbs_shown(LimbView { always: true, ..v }));
        for off in [
            LimbView { active: false, in_action: true, ..v },
            LimbView { third_person: true, in_action: true, ..v },
            LimbView { killcam: true, in_action: true, ..v },
            LimbView { dead: true, in_action: true, ..v },
            LimbView { has_limbs: false, always: true, in_action: true, ..v },
        ] {
            assert!(!limbs_shown(off), "{off:?}");
        }
    }

    #[test]
    fn the_actions_are_attacks_casts_and_specials() {
        for id in [16u16, 17, 18, 19, 32, 53, 54, 55, 57, 58, 2] {
            assert!(is_action_anim(id), "{id}");
        }
        // Standing, walking, running, swimming, dying, readying: not an action.
        for id in [0u16, 1, 4, 5, 25, 26, 41, 42, 6, 37, 38] {
            assert!(!is_action_anim(id), "{id}");
        }
    }

    #[test]
    fn a_limb_is_scaled_to_one_reach() {
        assert!((limb_scale(1.0) - REACH).abs() < 1e-6);
        assert!((limb_scale(0.25) - 2.0f32.min(SCALE_MAX)).abs() < 1e-6);
        assert_eq!(limb_scale(100.0), SCALE_MIN);
        assert_eq!(limb_scale(0.0), 1.0);
    }

    #[test]
    fn the_shoulders_sit_below_and_ahead_of_the_eye_and_follow_the_look() {
        let eye = Vec3::new(1.0, 2.0, 3.0);
        let fwd = Vec3::NEG_Z;
        let anchor = Vec3::new(5.0, -1.0, 7.0);
        // Level: the anchor lands ahead and below; a paw ahead of the shoulder stays ahead.
        let level = Quat::IDENTITY;
        let a = place(anchor, anchor, 0.5, eye, fwd, level);
        assert!((a - (eye + fwd * AHEAD - Vec3::Y * BELOW)).length() < 1e-5);
        let paw = place(anchor + fwd * 0.4, anchor, 0.5, eye, fwd, level);
        assert!((paw - a - fwd * 0.2).length() < 1e-5);
        // Looking down 45 degrees: the whole set turns about the eye, so it stays in view.
        let down = Quat::from_rotation_arc(fwd, (fwd - Vec3::Y).normalize());
        let a_down = place(anchor, anchor, 0.5, eye, fwd, down);
        assert!((a_down.distance(eye) - a.distance(eye)).abs() < 1e-5);
        // In view: within the vertical half field of the camera (the shoulders must not sit
        // under the bottom edge, where a swipe never reaches).
        let half = benilla_world::view::CAM_FOVY * 0.5;
        let in_view = |p: Vec3, look: Vec3| (p - eye).normalize().angle_between(look) < half;
        assert!(in_view(a, fwd));
        assert!(in_view(a_down, (fwd - Vec3::Y).normalize()));
        // A paw a reach below the shoulders (hanging at rest) is out of view.
        let hanging = place(anchor - Vec3::Y * 1.0, anchor, 0.5, eye, fwd, level);
        assert!(!in_view(hanging, fwd));
    }
}
