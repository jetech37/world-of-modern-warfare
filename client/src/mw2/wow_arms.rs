//! The player's own WoW arms in place of MW2's view hands: the character's forearm and hand
//! triangles (its race, its gloves and sleeves, the composited skin texture), posed each frame so
//! the hands sit where MW2's view hands hold the gun. MW2's arms still run underneath (their
//! skeleton carries the gun and every clip); only their surfaces give way.
//!
//! The pose: the WoW hand goes to MW2's wrist, turned by a frame both skeletons agree on (wrist to
//! middle finger, and the thumb's side); the forearm runs from the hand toward MW2's elbow at the
//! character's own forearm length, the upper arm on to MW2's shoulder; the fingers follow the
//! MW2 grip (the thumb its thumb, the others its middle finger) at the character's own finger
//! lengths. No bone axis is assumed of either skeleton.

use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::sync::Arc;

use bevy::asset::RenderAssetUsages;
use bevy::camera::visibility::NoFrustumCulling;
use bevy::math::{Mat3, Mat4, Quat, Vec3};
use bevy::mesh::skinning::SkinnedMeshInverseBindposes;
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;

use benilla_app::Objects;
use benilla_assets::ModelAnimations;
use benilla_assets::materials::WowModelMaterial;
use benilla_formats::CharSkinSlot;
use benilla_world::interact::PickMesh;
use benilla_world::rig_anim::RigPose;
use benilla_world::rig_palette::RigSkin;

use super::viewmodel::{Viewmodel, ViewmodelPart};

/// A frame from a forward axis and a side reference: x forward, y the side (made square to x),
/// z = x × y.
pub fn frame(forward: Vec3, side: Vec3) -> Mat3 {
    let x = forward.normalize_or_zero();
    let y = (side - x * side.dot(x)).normalize_or_zero();
    Mat3::from_cols(x, y, x.cross(y))
}

/// A hand's frame from its wrist, middle-finger root and thumb root.
pub fn hand_frame(wrist: Vec3, mid: Vec3, thumb: Vec3) -> Mat3 {
    frame(mid - wrist, thumb - wrist)
}

/// The rotation turning frame `from` onto frame `to`.
pub fn turn(from: Mat3, to: Mat3) -> Mat3 {
    to * from.transpose()
}

/// Which bones an arm is made of (global M2 bone indices).
#[derive(Debug, Clone, PartialEq)]
pub struct ArmChain {
    pub upper: usize,
    pub forearm: usize,
    pub hand: usize,
    /// Every bone below the hand (the fingers).
    pub fingers: Vec<usize>,
    pub thumb: usize,
    /// Where the hand points in the bind pose: from the wrist to the knuckles (the finger roots
    /// but the thumb, their mean).
    pub forward: Vec3,
    /// Each finger root's chain out to its tip (a vanilla hand has a one-bone thumb and a
    /// two-bone mitten for the four fingers).
    pub finger_chains: Vec<Vec<usize>>,
    /// Where a chain's last bone points in the bind pose (it has no child to say so): toward
    /// the middle of its own skin ([`tip_dirs`]).
    pub dirs: HashMap<usize, Vec3>,
    /// The character's own closed hand (`HandsClosed`, the grip WoW holds a weapon with): each
    /// finger bone's turn about its pivot. Made for this hand's thick fingers, it closes them
    /// round a hilt without folding them into themselves.
    pub closed: HashMap<usize, Quat>,
}

/// The arm chains of a WoW skeleton from its parents, pivots and the pose source's finger masks
/// (`finger_bit` 3 for the right hand, 4 the left: a bone whose mask has the bit clear is a
/// finger bone). `None` when the skeleton has no such hand.
pub fn arm_chain(
    parents: &[i16],
    pivots: &[Vec3],
    masks: &[u64],
    finger_bit: u32,
) -> Option<ArmChain> {
    let finger = |b: usize| masks.get(b).is_some_and(|m| m & (1 << finger_bit) == 0);
    let parent = |b: usize| usize::try_from(*parents.get(b)?).ok();
    // The finger roots: finger bones whose parent is not one.
    let roots: Vec<usize> = (0..parents.len())
        .filter(|&b| finger(b) && !parent(b).is_some_and(finger))
        .collect();
    if roots.len() < 2 {
        return None;
    }
    // The hand: the parent most roots share.
    let mut counts: HashMap<usize, usize> = HashMap::new();
    for &r in &roots {
        if let Some(p) = parent(r) {
            *counts.entry(p).or_default() += 1;
        }
    }
    let (&hand, _) = counts.iter().max_by_key(|(b, n)| (**n, usize::MAX - **b))?;
    let roots: Vec<usize> = roots
        .into_iter()
        .filter(|&r| parent(r) == Some(hand))
        .collect();
    let forearm = parent(hand)?;
    let upper = parent(forearm)?;
    let below = |b: usize| {
        let mut up = parent(b);
        while let Some(p) = up {
            if p == hand {
                return true;
            }
            up = parent(p);
        }
        false
    };
    let fingers: Vec<usize> = (0..parents.len()).filter(|&b| below(b)).collect();
    // The thumb: the root nearest the wrist along the hand (its root is down by the wrist, the
    // others' at the knuckles), the hand's way first taken toward its farthest finger joint.
    let off = |b: usize| pivots[b] - pivots[hand];
    let reach = fingers
        .iter()
        .copied()
        .filter(|&b| finger(b))
        .map(off)
        .max_by(|a, b| a.length().total_cmp(&b.length()))?
        .normalize_or_zero();
    let thumb = *roots
        .iter()
        .min_by(|&&a, &&b| off(a).dot(reach).total_cmp(&off(b).dot(reach)))?;
    // Where the hand points: to the knuckles (every other root's mean).
    let forward = roots
        .iter()
        .filter(|&&r| r != thumb)
        .map(|&r| off(r))
        .sum::<Vec3>()
        .normalize_or_zero();
    // Each root's chain: on through its first child to the tip.
    let finger_chains = roots
        .iter()
        .map(|&r| {
            let mut chain = vec![r];
            while let Some(next) = (0..parents.len()).find(|&b| parent(b) == chain.last().copied())
            {
                chain.push(next);
            }
            chain
        })
        .collect();
    Some(ArmChain {
        upper,
        forearm,
        hand,
        fingers,
        thumb,
        forward,
        finger_chains,
        dirs: HashMap::new(),
        closed: HashMap::new(),
    })
}

/// The character's closed hand (`side` 0 right, 1 left): each of `fingers`' turns in its
/// `HandsClosed` grip clip; empty when the model has none.
pub fn closed_hand(
    anims: &ModelAnimations,
    side: usize,
    fingers: &[usize],
) -> HashMap<usize, Quat> {
    let Some(clip) = anims.hand_close[side]
        .and_then(|n| anims.pose.node(n))
        .and_then(|n| anims.pose.clips.get(n.clip as usize))
    else {
        return HashMap::new();
    };
    clip.bones
        .iter()
        .filter(|b| fingers.contains(&usize::from(b.bone)))
        .filter_map(|b| Some((usize::from(b.bone), b.rotation.sample(0.0)?)))
        .collect()
}

/// Where each finger chain's last bone points: from its pivot to the middle of the skin that
/// leans mostly on it (`None` for a bone with no such skin).
pub fn tip_dirs(chain: &ArmChain, pivots: &[Vec3], verts: &[ArmVertex]) -> HashMap<usize, Vec3> {
    let mut out = HashMap::new();
    for &b in chain.finger_chains.iter().filter_map(|c| c.last()) {
        let (sum, n) = verts
            .iter()
            .filter(|v| {
                v.joints
                    .iter()
                    .zip(v.weights)
                    .any(|(j, w)| w > 0.5 && usize::from(*j) == b)
            })
            .fold((Vec3::ZERO, 0.0), |(s, n), v| (s + v.pos, n + 1.0));
        if n > 0.0
            && let Some(d) = (sum / n - pivots[b]).try_normalize()
        {
            out.insert(b, d);
        }
    }
    out
}

/// Where MW2's view hands put one arm this frame, in the space the arms are drawn in.
#[derive(Debug, Clone, Copy)]
pub struct ArmTarget {
    pub shoulder: Vec3,
    pub elbow: Vec3,
    pub wrist: Vec3,
    /// MW2's middle finger and thumb joints, root first.
    pub mid: [Vec3; 3],
    pub thumb: [Vec3; 3],
}

/// Each arm bone's skinning matrix (bind model space to the drawn space), for a WoW arm `chain`
/// with bind `pivots`, posed onto `target`, scaled by `k` (WoW model units to drawn units). The
/// hand sits on MW2's wrist; the forearm runs from MW2's elbow to it and the upper arm from MW2's
/// shoulder to the elbow, each stretched along its own length to fit ([`stretch_along`]): at the
/// hand's scale a character's arm is about half MW2's, and where it stopped short the kept
/// geometry's open end (the shoulder, where the arm is cut from the body) hung in front of the
/// eye whenever the hand reached forward. On MW2's shoulder it is where MW2's own view arms end,
/// out of every view. The thumb follows MW2's thumb and the other fingers its middle finger, joint
/// by joint at the character's own finger lengths; `closed` (0..1) of the way to the character's
/// own closed hand ([`ArmChain::closed`]).
pub fn arm_bones(
    chain: &ArmChain,
    pivots: &[Vec3],
    target: &ArmTarget,
    k: f32,
    closed: f32,
) -> HashMap<usize, Mat4> {
    let c = |b: usize| pivots[b];
    let skin = |at: Vec3, rot: Mat3, pivot: Vec3| {
        Mat4::from_translation(at)
            * Mat4::from_mat3(rot)
            * Mat4::from_scale(Vec3::splat(k))
            * Mat4::from_translation(-pivot)
    };
    let mut out = HashMap::new();
    // The hand: on the wrist, turned by the frame both hands agree on.
    let bind_hand = frame(chain.forward, c(chain.thumb) - c(chain.hand));
    let goal_hand = hand_frame(target.wrist, target.mid[0], target.thumb[0]);
    let r_hand = turn(bind_hand, goal_hand);
    out.insert(chain.hand, skin(target.wrist, r_hand, c(chain.hand)));
    let thumb_side = |f: Mat3| f.y_axis;
    // The forearm: from MW2's elbow to the wrist.
    let segment = |from: usize, to: usize, start: Vec3, end: Vec3| {
        let bind = c(to) - c(from);
        let goal = end - start;
        let r = turn(
            frame(bind, thumb_side(bind_hand)),
            frame(goal, thumb_side(goal_hand)),
        );
        let s = stretch_factor(bind.length() * k, goal.length());
        Mat4::from_translation(start)
            * Mat4::from_mat3(r)
            * stretch_along(bind, s, k)
            * Mat4::from_translation(-c(from))
    };
    out.insert(
        chain.forearm,
        segment(chain.forearm, chain.hand, target.elbow, target.wrist),
    );
    // The upper arm: from a little past MW2's shoulder (into the body) to the elbow.
    let shoulder = target.shoulder
        + (target.shoulder - target.elbow).normalize_or_zero() * SHOULDER_PAST;
    out.insert(
        chain.upper,
        segment(chain.upper, chain.forearm, shoulder, target.elbow),
    );
    // The fingers: each chain bent along MW2's finger, joint by joint, and turned on toward the
    // closed hand.
    let hand = out[&chain.hand];
    let hand_turn = Quat::from_mat3(&r_hand).normalize();
    for fc in &chain.finger_chains {
        let thumb = fc.first() == Some(&chain.thumb);
        let mw = if thumb { &target.thumb } else { &target.mid };
        let mut joint = hand.transform_point3(c(fc[0]));
        let mut bind_dir = chain.forward;
        let mut closed_turn = hand_turn;
        let mut follow = hand_turn;
        for (i, &b) in fc.iter().enumerate() {
            let next = fc.get(i + 1).copied();
            match next {
                Some(n) => bind_dir = c(n) - c(b),
                None => bind_dir = chain.dirs.get(&b).copied().unwrap_or(bind_dir),
            }
            // A one-bone thumb is the whole thumb: MW2's from its root to its last joint.
            let goal_dir = if thumb && fc.len() == 1 {
                mw[2] - mw[0]
            } else {
                let seg = i.min(1);
                mw[seg + 1] - mw[seg]
            }
            .normalize_or_zero();
            // Swung from where the bone before carries it onto MW2's way, the least turn: no
            // twist about its own length (a frame on the hand's side axis comes apart as the
            // finger curls toward it, and twists the thick fingers into spikes).
            let carried = (follow * bind_dir).normalize_or_zero();
            follow = if carried == Vec3::ZERO || goal_dir == Vec3::ZERO {
                follow
            } else {
                (Quat::from_rotation_arc(carried, goal_dir) * follow).normalize()
            };
            closed_turn *= chain.closed.get(&b).copied().unwrap_or(Quat::IDENTITY);
            let q = follow.slerp(closed_turn, closed.clamp(0.0, 1.0));
            let r = Mat3::from_quat(q);
            out.insert(b, skin(joint, r, c(b)));
            if let Some(n) = next {
                joint += r * (c(n) - c(b)) * k;
            }
        }
    }
    // Any other bone under the hand moves with it.
    for &f in &chain.fingers {
        out.entry(f).or_insert(hand);
    }
    out
}

/// How far past MW2's shoulder the upper arm runs (drawn units, inches), into the body and out of
/// view: a gun's idle may hold the shoulder before the eye (a pump shotgun's forward hand), and the
/// arm's open end, ringed round its root, would show at a wide screen's edge.
pub const SHOULDER_PAST: f32 = 3.0;

/// How much a segment `bind` long (already at the hand's scale) stretches to span `goal`; 1 for a
/// degenerate one.
pub fn stretch_factor(bind: f32, goal: f32) -> f32 {
    if bind > 1e-4 && goal > 1e-4 {
        goal / bind
    } else {
        1.0
    }
}

/// Scale by `k`, and by `s` more along `axis`: a segment stretched along its length, its girth the
/// hand's scale.
pub fn stretch_along(axis: Vec3, s: f32, k: f32) -> Mat4 {
    let a = axis.normalize_or_zero();
    let m = Mat3::IDENTITY + (s - 1.0) * Mat3::from_cols(a * a.x, a * a.y, a * a.z);
    Mat4::from_mat3(m * k)
}

/// The scale that gives the character's hand MW2's: from the wrist to its farthest finger joint,
/// against MW2's wrist to the middle finger's last joint. Every race's hand fits the grip and its
/// arm keeps its own build around it (a forearm bone's length would not do: some races carry a
/// short twist bone there).
pub fn arm_scale(chain: &ArmChain, pivots: &[Vec3], target: &ArmTarget) -> f32 {
    let wow = chain
        .fingers
        .iter()
        .map(|&f| (pivots[f] - pivots[chain.hand]).length())
        .fold(0.0f32, f32::max);
    if wow <= 0.0 {
        return 1.0;
    }
    (target.mid[2] - target.wrist).length() / wow
}

/// A vertex skinned by the bones in `bones` (weights to other bones dropped, the rest
/// renormalised); `None` when none of its bones is there.
pub fn skin_point(
    p: Vec3,
    joints: [u16; 4],
    weights: [f32; 4],
    bones: &HashMap<usize, Mat4>,
) -> Option<Vec3> {
    let mut sum = Vec3::ZERO;
    let mut total = 0.0;
    for (j, w) in joints.iter().zip(weights) {
        if w > 0.0
            && let Some(m) = bones.get(&usize::from(*j))
        {
            sum += m.transform_point3(p) * w;
            total += w;
        }
    }
    (total > 0.0).then(|| sum / total)
}

/// Whether a triangle belongs to the drawn arm: each corner leans on the upper arm, forearm,
/// hand or fingers. The upper arm too: a knife's swing and a reload bring the elbow into view,
/// and an arm cut off there looks dislocated.
pub fn keeps_triangle(corners: [([u16; 4], [f32; 4]); 3], arm: &dyn Fn(usize) -> bool) -> bool {
    corners.iter().all(|(j, w)| {
        j.iter()
            .zip(w)
            .any(|(j, w)| *w > 0.05 && arm(usize::from(*j)))
    })
}

/// One vertex of the character's arms, in its bind pose (Bevy axes, model units).
#[derive(Debug, Clone, Copy)]
pub struct ArmVertex {
    pub pos: Vec3,
    pub normal: Vec3,
    pub uv: [f32; 2],
    pub joints: [u16; 4],
    pub weights: [f32; 4],
}

/// The character's arms as gathered: their bones, their triangles, their texture.
pub struct ArmSource {
    /// The right arm, then the left (either may be missing).
    pub chains: [Option<ArmChain>; 2],
    pub pivots: Vec<Vec3>,
    pub verts: Vec<ArmVertex>,
    pub indices: Vec<u32>,
    pub texture: Handle<Image>,
    /// The character's scale (its model units to yards).
    pub scale: f32,
}

/// The player's arms: gathered when their body or gear changes, posed each frame.
#[derive(Resource, Default)]
pub struct WowArms {
    pub source: Option<Arc<ArmSource>>,
    signature: u64,
}

/// The arms' mesh under a viewmodel, and its source.
#[derive(Component)]
pub struct ArmsMesh {
    source: Arc<ArmSource>,
    mesh: Handle<Mesh>,
}

/// Gather the player's arms from their body: the arm bones, the triangles of the body's skin
/// leaning on the forearms and hands (gloves and sleeves included: only worn geosets are
/// spawned), the composited texture; again whenever the body's parts change.
#[allow(clippy::type_complexity)]
pub fn gather(
    objects: Objects,
    roots: Query<(
        &RigPose,
        &RigSkin,
        &ModelAnimations,
        &GlobalTransform,
        &Children,
    )>,
    parts: Query<(Entity, &PickMesh, &MeshMaterial3d<WowModelMaterial>)>,
    materials: Res<Assets<WowModelMaterial>>,
    ibps: Res<Assets<SkinnedMeshInverseBindposes>>,
    mut arms: ResMut<WowArms>,
) {
    let Some((pose, skin, anims, gt, children)) = objects.player().and_then(|e| roots.get(e).ok())
    else {
        arms.source = None;
        return;
    };
    // The body's parts and their texture: rebuild when either changes (a re-dress).
    let body: Vec<(Entity, &PickMesh, Option<Handle<Image>>)> = children
        .iter()
        .filter_map(|c| parts.get(c).ok())
        .filter(|(_, p, _)| matches!(p.0.char_slot, Some(CharSkinSlot::Body)))
        .map(|(e, p, m)| {
            let tex = materials
                .get(&m.0)
                .and_then(|m| m.base.base_color_texture.clone());
            (e, p, tex)
        })
        .collect();
    let mut h = std::collections::hash_map::DefaultHasher::new();
    for (e, p, t) in &body {
        e.hash(&mut h);
        p.0.positions.len().hash(&mut h);
        t.as_ref().map(|t| t.id()).hash(&mut h);
    }
    let signature = h.finish();
    if signature == arms.signature && arms.source.is_some() {
        return;
    }
    let Some(texture) = body.iter().find_map(|(_, _, t)| t.clone()) else {
        return; // the composite is still pending
    };
    let Some(ibp) = ibps.get(skin.ibp()) else {
        return;
    };
    let pivots: Vec<Vec3> = ibp.iter().map(|m| m.inverse().w_axis.truncate()).collect();
    let mut chains =
        [3, 4].map(|bit| arm_chain(&pose.parents, &pivots, &anims.pose.bone_masks, bit));
    if chains.iter().all(Option::is_none) {
        warn_once!("benilla-mw2: the character's skeleton shows no hands; MW2's arms stay");
        arms.signature = signature;
        arms.source = None;
        return;
    }
    let drawn = |b: usize| {
        chains
            .iter()
            .flatten()
            .any(|c| b == c.upper || b == c.forearm || b == c.hand || c.fingers.contains(&b))
    };
    let mut verts = Vec::new();
    let mut indices = Vec::new();
    for (_, part, _) in &body {
        let sub = &part.0;
        let corner = |i: usize| {
            (
                sub.joints.get(i).copied().unwrap_or_default(),
                sub.weights.get(i).copied().unwrap_or_default(),
            )
        };
        let mut remap: HashMap<u32, u32> = HashMap::new();
        for tri in sub.indices.as_chunks::<3>().0 {
            let c = [tri[0], tri[1], tri[2]].map(|i| corner(i as usize));
            if !keeps_triangle(c, &drawn) {
                continue;
            }
            for &i in tri {
                let at = *remap.entry(i).or_insert_with(|| {
                    let i = i as usize;
                    verts.push(ArmVertex {
                        pos: benilla_assets::coords::wow_to_bevy(sub.positions[i]),
                        normal: sub
                            .normals
                            .get(i)
                            .map_or(Vec3::Y, |n| benilla_assets::coords::wow_to_bevy(*n)),
                        uv: sub.uvs.get(i).copied().unwrap_or_default(),
                        joints: corner(i).0,
                        weights: corner(i).1,
                    });
                    (verts.len() - 1) as u32
                });
                indices.push(at);
            }
        }
    }
    for (side, c) in chains.iter_mut().enumerate() {
        let Some(c) = c else { continue };
        c.dirs = tip_dirs(c, &pivots, &verts);
        c.closed = closed_hand(anims, side, &c.fingers);
    }
    let scale = gt.compute_transform().scale.x;
    let size = |c: &Option<ArmChain>| {
        c.as_ref().map_or(String::from("-"), |c| {
            format!(
                "forearm {:.3} hand {}",
                (pivots[c.hand] - pivots[c.forearm]).length(),
                c.hand
            )
        })
    };
    let (lo, hi) = verts.iter().fold((Vec3::MAX, Vec3::MIN), |(lo, hi), v| {
        (lo.min(v.pos), hi.max(v.pos))
    });
    info!(
        "benilla-mw2: the character's arms: {} triangles, scale {scale:.3}, right {}, left {}, bounds {lo:.2}..{hi:.2}",
        indices.len() / 3,
        size(&chains[0]),
        size(&chains[1]),
    );
    arms.signature = signature;
    arms.source = (!indices.is_empty()).then(|| {
        Arc::new(ArmSource {
            chains,
            pivots,
            verts,
            indices,
            texture,
            scale,
        })
    });
}

/// Pose the player's arms onto each viewmodel's MW2 arms and draw them in their place.
#[allow(clippy::type_complexity)]
pub fn skin(
    mut commands: Commands,
    arms: Res<WowArms>,
    roots: Query<(Entity, &Viewmodel, Option<&ArmsMesh>)>,
    mut parts: Query<(&ViewmodelPart, &ChildOf, &mut Visibility), Without<ArmsMeshEntity>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    view_light: Res<super::viewmodel::ViewLight>,
    old: Query<(Entity, &ChildOf), With<ArmsMeshEntity>>,
) {
    let source = arms.source.clone();
    for (root, vm, held) in &roots {
        let drawn = source
            .as_ref()
            .filter(|_| vm.arm_joints.iter().any(Option::is_some))
            // `MW2_NO_WOW_ARMS` keeps MW2's arms (a comparison).
            .filter(|_| std::env::var_os("MW2_NO_WOW_ARMS").is_none());
        // MW2's arms give way to the player's.
        for (part, parent, mut vis) in &mut parts {
            if part.0 == 0 && parent.parent() == root {
                let want = if drawn.is_some() {
                    Visibility::Hidden
                } else {
                    Visibility::Inherited
                };
                if *vis != want {
                    *vis = want;
                }
            }
        }
        let Some(src) = drawn else {
            if held.is_some() {
                commands.entity(root).remove::<ArmsMesh>();
                for (e, parent) in &old {
                    if parent.parent() == root {
                        commands.entity(e).despawn();
                    }
                }
            }
            continue;
        };
        // A mesh for these arms, made once per gathering.
        let mesh = match held.filter(|h| Arc::ptr_eq(&h.source, src)) {
            Some(h) => h.mesh.clone(),
            None => {
                for (e, parent) in &old {
                    if parent.parent() == root {
                        commands.entity(e).despawn();
                    }
                }
                let mut mesh = Mesh::new(
                    PrimitiveTopology::TriangleList,
                    RenderAssetUsages::default(),
                );
                let n = src.verts.len();
                mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, vec![[0.0f32; 3]; n]);
                mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, vec![[0.0f32, 1.0, 0.0]; n]);
                mesh.insert_attribute(
                    Mesh::ATTRIBUTE_UV_0,
                    src.verts.iter().map(|v| v.uv).collect::<Vec<_>>(),
                );
                mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, vec![[1.0f32; 4]; n]);
                mesh.insert_indices(Indices::U32(src.indices.clone()));
                let handle = meshes.add(mesh);
                let material = materials.add(StandardMaterial {
                    base_color_texture: Some(src.texture.clone()),
                    unlit: true,
                    cull_mode: None,
                    ..default()
                });
                commands.spawn((
                    Name::new("mw2 player arms"),
                    ArmsMeshEntity,
                    Mesh3d(handle.clone()),
                    MeshMaterial3d(material),
                    Transform::IDENTITY,
                    NoFrustumCulling,
                    ChildOf(root),
                ));
                commands.entity(root).insert(ArmsMesh {
                    source: src.clone(),
                    mesh: handle.clone(),
                });
                handle
            }
        };
        let Some(mesh) = meshes.get_mut(&mesh) else {
            continue;
        };
        // MW2's arm joints in the drawn (eye) space, and the WoW bones posed onto them.
        let at = |j: usize| {
            vm.pose
                .get(j)
                .map(|m| vm.eye_from_world.transform_point3(m.w_axis.truncate()))
        };
        // While the melee weapon is up, each hand holding something takes that pose's shape
        // (a fist, a hand opened for a spell) in place of the gun grip's fingers.
        let shapes = [vm.melee_grip.map(|g| g.hand), vm.left_grip.map(|g| g.hand)];
        let mut bones = HashMap::new();
        for ((chain, joints), shape) in src.chains.iter().zip(&vm.arm_joints).zip(shapes) {
            let (Some(chain), Some(joints)) = (chain, joints) else {
                continue;
            };
            let Some(p) = joints.iter().map(|&j| at(j)).collect::<Option<Vec<Vec3>>>() else {
                continue;
            };
            let mut target = ArmTarget {
                shoulder: p[0],
                elbow: p[1],
                wrist: p[2],
                mid: [p[3], p[4], p[5]],
                thumb: [p[6], p[7], p[8]],
            };
            // The character's arm, its hand MW2's size.
            let k = arm_scale(chain, &src.pivots, &target);
            if let Some(shape) = shape {
                (target.mid, target.thumb) = shape.place(p[2], p[3], p[6]);
            }
            // A hand round a hilt closes as the character's own does; the gun's fingers and a
            // spell's open hand follow their poses.
            let closed = shape.map_or(0.0, |s| s.closed());
            bones.extend(arm_bones(chain, &src.pivots, &target, k, closed));
        }
        let mut positions = Vec::with_capacity(src.verts.len());
        let mut colors = Vec::with_capacity(src.verts.len());
        for v in &src.verts {
            let p = skin_point(v.pos, v.joints, v.weights, &bones);
            let p = p.unwrap_or(Vec3::ZERO);
            let main = v
                .joints
                .iter()
                .zip(v.weights)
                .filter(|(_, w)| *w > 0.0)
                .max_by(|a, b| a.1.total_cmp(&b.1))
                .and_then(|(j, _)| bones.get(&usize::from(*j)));
            let normal = main.map_or(Vec3::Y, |m| {
                m.transform_vector3(v.normal).normalize_or_zero()
            });
            positions.push(p.to_array());
            colors.push(view_light.lit(normal));
        }
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, positions);
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, colors);
    }
}

/// The arms' drawn entity.
#[derive(Component)]
pub struct ArmsMeshEntity;

// Measured from the game installs, so only in a tree that has them (client/build.rs).
#[cfg(all(test, aw_arm_cut_fixtures))]
#[path = "arm_cut_tests.rs"]
mod cut_tests;

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// A T-posed right arm along +x: shoulder 0, upper 1, forearm 2, hand 3, fingers 4-8 (thumb 8
    /// pointing forward, +z), plus a spine bone 9.
    fn skeleton() -> (Vec<i16>, Vec<Vec3>, Vec<u64>) {
        let parents = vec![9, 0, 1, 2, 3, 3, 3, 3, 3, -1];
        let pivots = vec![
            Vec3::new(0.2, 1.5, 0.0),
            Vec3::new(0.3, 1.5, 0.0),
            Vec3::new(0.6, 1.5, 0.0),
            Vec3::new(0.9, 1.5, 0.0),
            Vec3::new(1.0, 1.5, 0.03),   // index
            Vec3::new(1.0, 1.5, 0.0),    // middle
            Vec3::new(1.0, 1.5, -0.02),  // ring
            Vec3::new(0.99, 1.5, -0.04), // pinky
            Vec3::new(0.95, 1.5, 0.06),  // thumb, apart
            Vec3::new(0.0, 1.0, 0.0),
        ];
        // Bit 3 clear on the fingers (the right hand's).
        let masks = (0..10)
            .map(|b| {
                if (4..=8).contains(&b) {
                    !(1u64 << 3)
                } else {
                    u64::MAX
                }
            })
            .collect();
        (parents, pivots, masks)
    }

    #[test]
    fn the_arm_chain_is_found_from_the_fingers() {
        let (parents, pivots, masks) = skeleton();
        let c = arm_chain(&parents, &pivots, &masks, 3).expect("a right hand");
        assert_eq!((c.upper, c.forearm, c.hand), (1, 2, 3));
        assert_eq!(c.thumb, 8);
        // Where the hand points: the fingers' way (+x here), not any one finger's.
        assert!(c.forward.dot(Vec3::X) > 0.95, "{}", c.forward);
        assert_eq!(c.fingers, vec![4, 5, 6, 7, 8]);
        assert!(arm_chain(&parents, &pivots, &masks, 4).is_none()); // no left fingers
    }

    /// A vanilla human's right hand as the game has it (bind pivots relative to the hand, Bevy
    /// axes): the four fingers one two-bone mitten 1-2 out at the knuckles, the thumb one bone 3
    /// near the wrist and off to the palm side, and an attachment point 4 (not a finger).
    pub(crate) fn vanilla_hand() -> (Vec<i16>, Vec<Vec3>, Vec<u64>) {
        let parents = vec![-1, 0, 1, 0, 0];
        let pivots = vec![
            Vec3::ZERO,
            Vec3::new(0.076, -0.111, 0.016),
            Vec3::new(0.063, -0.204, -0.010),
            Vec3::new(-0.012, -0.030, -0.048),
            Vec3::new(0.015, -0.138, -0.021),
        ];
        let masks = vec![u64::MAX, 0x12, 0x12, 0x12, 0x1a];
        // The hand needs a forearm and upper arm above it.
        let mut parents = parents
            .into_iter()
            .map(|p| if p < 0 { 5 } else { p })
            .collect::<Vec<_>>();
        parents.extend([6, -1]);
        let mut pivots = pivots;
        pivots.extend([Vec3::new(-0.09, 0.28, 0.05), Vec3::new(-0.2, 0.62, 0.0)]);
        let mut masks = masks;
        masks.extend([u64::MAX, u64::MAX]);
        (parents, pivots, masks)
    }

    #[test]
    fn a_vanilla_hand_has_its_thumb_by_the_wrist_and_its_fingers_at_the_knuckles() {
        let (parents, pivots, masks) = vanilla_hand();
        let c = arm_chain(&parents, &pivots, &masks, 3).expect("a right hand");
        assert_eq!(c.thumb, 3);
        assert!(c.finger_chains.contains(&vec![1, 2]));
        // The hand points from the wrist to the knuckles.
        assert!(
            c.forward.dot(pivots[1].normalize()) > 0.999,
            "{}",
            c.forward
        );
    }

    #[test]
    fn a_one_bone_thumb_points_along_mw2s_thumb() {
        let (parents, pivots, masks) = vanilla_hand();
        let mut chain = arm_chain(&parents, &pivots, &masks, 3).unwrap();
        // The thumb's skin runs from its pivot down and to the palm side.
        let thumb_way = Vec3::new(-0.023, -0.088, -0.035).normalize();
        chain.dirs.insert(3, thumb_way);
        let target = ArmTarget {
            shoulder: Vec3::new(0.0, 0.0, 30.0),
            elbow: Vec3::new(0.0, 0.0, 12.0),
            wrist: Vec3::ZERO,
            mid: [
                Vec3::new(0.0, 0.0, -4.0),
                Vec3::new(0.0, -1.0, -5.0),
                Vec3::new(0.0, -2.0, -5.0),
            ],
            thumb: [
                Vec3::new(0.0, 1.0, -1.0),
                Vec3::new(0.0, 1.5, -2.5),
                Vec3::new(0.5, 1.5, -4.0),
            ],
        };
        let bones = arm_bones(&chain, &pivots, &target, 36.0, 0.0);
        let along = bones[&3].transform_vector3(thumb_way).normalize();
        let want = (target.thumb[2] - target.thumb[0]).normalize();
        assert!(along.dot(want) > 0.999, "{along} vs {want}");
        // The fingers follow MW2's middle finger, not its thumb.
        let f = bones[&1].transform_point3(pivots[2]) - bones[&1].transform_point3(pivots[1]);
        assert!(
            f.normalize()
                .dot((target.mid[1] - target.mid[0]).normalize())
                > 0.999,
            "{f}"
        );
    }

    #[test]
    fn a_closed_hand_takes_wows_own_grip() {
        let (parents, pivots, masks) = vanilla_hand();
        let mut chain = arm_chain(&parents, &pivots, &masks, 3).unwrap();
        // WoW's grip: the mitten bent at its root and again at its middle.
        let q1 = Quat::from_rotation_x(1.2);
        let q2 = Quat::from_rotation_x(0.6);
        chain.closed.insert(1, q1);
        chain.closed.insert(2, q2);
        let target = ArmTarget {
            shoulder: Vec3::new(0.0, 0.0, 30.0),
            elbow: Vec3::new(0.0, 0.0, 12.0),
            wrist: Vec3::ZERO,
            mid: [
                Vec3::new(0.0, 0.0, -4.0),
                Vec3::new(0.0, 0.0, -5.0),
                Vec3::new(0.0, 0.0, -6.0),
            ],
            thumb: [
                Vec3::new(0.0, 1.0, -1.0),
                Vec3::new(0.0, 1.5, -2.5),
                Vec3::new(0.5, 1.5, -4.0),
            ],
        };
        let k = 36.0;
        let bones = arm_bones(&chain, &pivots, &target, k, 1.0);
        let local = |p: Vec3, q: Quat| {
            Mat4::from_translation(p) * Mat4::from_quat(q) * Mat4::from_translation(-p)
        };
        let want1 = bones[&0] * local(pivots[1], q1);
        let want2 = want1 * local(pivots[2], q2);
        assert!(
            bones[&1].abs_diff_eq(want1, 1e-4),
            "{:?} vs {want1:?}",
            bones[&1]
        );
        assert!(bones[&2].abs_diff_eq(want2, 1e-4));
        // A bone WoW's grip leaves alone (the thumb here) stays as in the bind pose.
        assert!(bones[&3].abs_diff_eq(bones[&0], 1e-4));
        // Open, the fingers follow the target as before.
        let open = arm_bones(&chain, &pivots, &target, k, 0.0);
        let f = open[&1].transform_point3(pivots[2]) - open[&1].transform_point3(pivots[1]);
        assert!(f.normalize().dot(Vec3::NEG_Z) > 0.999, "{f}");
    }

    #[test]
    fn a_curling_finger_swings_without_twisting() {
        let (parents, pivots, masks) = vanilla_hand();
        let chain = arm_chain(&parents, &pivots, &masks, 3).unwrap();
        // The grip: pointing ahead (-z), the thumb's root off to +y; the fingers curl a right
        // angle toward that side, where a frame on the hand's side axis comes apart.
        let wrist = Vec3::ZERO;
        let m0 = Vec3::new(0.0, 0.0, -4.0);
        let target = ArmTarget {
            shoulder: Vec3::new(0.0, 0.0, 30.0),
            elbow: Vec3::new(0.0, 0.0, 12.0),
            wrist,
            mid: [
                m0,
                m0 + Vec3::new(0.0, 1.0, -0.05),
                m0 + Vec3::new(0.0, 1.5, 0.8),
            ],
            thumb: [
                Vec3::new(0.0, 1.0, -1.0),
                Vec3::new(0.0, 1.5, -2.5),
                Vec3::new(0.5, 1.5, -4.0),
            ],
        };
        let bones = arm_bones(&chain, &pivots, &target, 36.0, 0.0);
        // The mitten's turn from the hand's: about an axis square to the way it pointed.
        let rot = |m: Mat4| Quat::from_mat4(&m).normalize();
        let rel = rot(bones[&1]) * rot(bones[&0]).inverse();
        let carried = bones[&0]
            .transform_vector3(pivots[2] - pivots[1])
            .normalize();
        let (axis, angle) = rel.to_axis_angle();
        assert!(angle > 0.5, "{angle}");
        assert!(
            axis.dot(carried).abs() < 0.05,
            "twisted: axis {axis} along {carried}"
        );
    }

    #[test]
    fn a_frame_is_square_and_right_handed() {
        let f = frame(Vec3::new(2.0, 0.0, 0.0), Vec3::new(0.3, 1.0, 0.0));
        assert!((f.x_axis - Vec3::X).length() < 1e-6);
        assert!((f.y_axis - Vec3::Y).length() < 1e-6);
        assert!((f.z_axis - Vec3::Z).length() < 1e-6);
        assert!((f.determinant() - 1.0).abs() < 1e-5);
    }

    #[test]
    fn the_hand_lands_on_the_wrist_facing_the_grip() {
        let (parents, pivots, masks) = skeleton();
        let chain = arm_chain(&parents, &pivots, &masks, 3).unwrap();
        // MW2's hand: wrist at (10, 0, -20), fingers pointing -z, thumb up (+y).
        let target = ArmTarget {
            shoulder: Vec3::new(14.0, -6.0, 4.0),
            elbow: Vec3::new(12.0, -4.0, -8.0),
            wrist: Vec3::new(10.0, 0.0, -20.0),
            mid: [
                Vec3::new(10.0, 0.0, -24.0),
                Vec3::new(10.0, 0.0, -26.0),
                Vec3::new(10.0, 0.0, -28.0),
            ],
            thumb: [
                Vec3::new(10.0, 2.0, -22.0),
                Vec3::new(10.0, 3.0, -23.0),
                Vec3::new(10.0, 4.0, -24.0),
            ],
        };
        let k = 36.0;
        let bones = arm_bones(&chain, &pivots, &target, k, 0.0);
        // The wrist pivot goes onto the wrist; the middle finger points along the grip.
        let hand = bones[&3];
        assert!((hand.transform_point3(pivots[3]) - target.wrist).length() < 1e-3);
        // The hand points along the grip.
        let along = hand.transform_vector3(chain.forward).normalize();
        assert!(along.dot(Vec3::NEG_Z) > 0.999, "{along}");
        // The thumb is to the grip's thumb side.
        let thumb = hand.transform_point3(pivots[8]) - target.wrist;
        assert!(thumb.dot(Vec3::Y) > 0.0);
        // The forearm runs from MW2's elbow to the wrist, stretched to fit.
        let fore = bones[&2];
        let elbow = fore.transform_point3(pivots[2]);
        let wrist_by_fore = fore.transform_point3(pivots[3]);
        assert!(
            (wrist_by_fore - target.wrist).length() < 1e-3,
            "{wrist_by_fore}"
        );
        assert!((elbow - target.elbow).length() < 1e-3, "{elbow}");
        // The upper arm meets the forearm at the elbow and ends on MW2's shoulder, where the
        // kept geometry's open end is out of view.
        let upper = bones[&1];
        assert!((upper.transform_point3(pivots[2]) - elbow).length() < 1e-3);
        let past = target.shoulder
            + (target.shoulder - target.elbow).normalize() * SHOULDER_PAST;
        assert!((upper.transform_point3(pivots[1]) - past).length() < 1e-3);
        // Stretched along the arm only: the girth keeps the hand's scale.
        let along = (pivots[2] - pivots[1]).normalize();
        let across = along.any_orthonormal_vector();
        assert!((upper.transform_vector3(across).length() - k).abs() < 1e-3);
        let stretched = upper.transform_vector3(along).length();
        let want = (target.elbow - past).length() / (pivots[2] - pivots[1]).length();
        assert!((stretched - want).abs() < 1e-3, "{stretched} vs {want}");
        // A finger's root stays on the hand.
        assert!(
            (bones[&5].transform_point3(pivots[5]) - hand.transform_point3(pivots[5])).length()
                < 1e-3
        );
    }

    #[test]
    fn the_fingers_follow_mw2s_grip() {
        // A hand 3 with a three-bone finger 4-5-6 along +x and a one-bone thumb 7 to its side.
        let parents = vec![-1, 0, 1, 2, 3, 4, 5, 3];
        let pivots = vec![
            Vec3::new(0.0, 0.0, 0.0),
            Vec3::new(0.3, 0.0, 0.0),
            Vec3::new(0.6, 0.0, 0.0),
            Vec3::new(0.9, 0.0, 0.0),
            Vec3::new(1.0, 0.0, 0.0),
            Vec3::new(1.04, 0.0, 0.0),
            Vec3::new(1.07, 0.0, 0.0),
            Vec3::new(0.95, 0.0, 0.07),
        ];
        let masks: Vec<u64> = (0..8)
            .map(|b| if b >= 4 { !(1u64 << 3) } else { u64::MAX })
            .collect();
        let chain = arm_chain(&parents, &pivots, &masks, 3).unwrap();
        assert_eq!(chain.thumb, 7);
        // MW2's middle finger curls down (-y) as it goes forward (-z).
        let m0 = Vec3::new(0.0, 0.0, -4.0);
        let m1 = m0 + Vec3::new(0.0, -1.0, -1.0);
        let m2 = m1 + Vec3::new(0.0, -1.0, 0.0);
        let target = ArmTarget {
            shoulder: Vec3::new(0.0, 0.0, 30.0),
            elbow: Vec3::new(0.0, 0.0, 12.0),
            wrist: Vec3::ZERO,
            mid: [m0, m1, m2],
            thumb: [
                Vec3::new(0.0, 2.0, -2.0),
                Vec3::new(0.0, 3.0, -3.0),
                Vec3::new(0.0, 4.0, -4.0),
            ],
        };
        let k = 36.0;
        let bones = arm_bones(&chain, &pivots, &target, k, 0.0);
        let joint = |b: usize| bones[&b].transform_point3(pivots[b]);
        // Each segment runs MW2's way, at the character's own length.
        let seg = |a: usize, b: usize| joint(b) - joint(a);
        let d1 = seg(4, 5);
        assert!((d1.length() - 0.04 * k).abs() < 1e-3, "{d1}");
        assert!(d1.normalize().dot((m1 - m0).normalize()) > 0.999, "{d1}");
        let d2 = seg(5, 6);
        assert!((d2.length() - 0.03 * k).abs() < 1e-3, "{d2}");
        assert!(d2.normalize().dot((m2 - m1).normalize()) > 0.999, "{d2}");
        // The finger's root stays on the hand.
        assert!((joint(4) - bones[&3].transform_point3(pivots[4])).length() < 1e-3);
    }

    #[test]
    fn every_race_holds_the_gun_with_mw2s_hand_size() {
        let (parents, pivots, masks) = skeleton();
        let chain = arm_chain(&parents, &pivots, &masks, 3).unwrap();
        let target = |tip: Vec3| ArmTarget {
            shoulder: Vec3::new(0.0, 0.0, 30.0),
            elbow: Vec3::new(0.0, 0.0, 11.0),
            wrist: Vec3::ZERO,
            mid: [Vec3::new(0.0, 0.0, -2.0), Vec3::new(0.0, 0.0, -3.0), tip],
            thumb: [Vec3::Y; 3],
        };
        // The hand's reach to its farthest finger joint, scaled to MW2's 4.5 in.
        let reach = chain
            .fingers
            .iter()
            .map(|&f| (pivots[f] - pivots[3]).length())
            .fold(0.0f32, f32::max);
        let k = arm_scale(&chain, &pivots, &target(Vec3::new(0.0, 0.0, -4.5)));
        assert!((k * reach - 4.5).abs() < 1e-3, "{k}");
        // A short forearm bone (a gnome's) does not blow the scale up: only the hand counts.
        let mut gnome = pivots.clone();
        gnome[2] = Vec3::new(0.88, 1.5, 0.0);
        let k2 = arm_scale(&chain, &gnome, &target(Vec3::new(0.0, 0.0, -4.5)));
        assert!((k2 - k).abs() < 1e-3);
    }

    #[test]
    fn a_vertex_skins_by_the_arm_bones_it_has() {
        let mut bones = HashMap::new();
        bones.insert(2, Mat4::from_translation(Vec3::X));
        bones.insert(3, Mat4::from_translation(Vec3::Y));
        let p = Vec3::ZERO;
        let both = skin_point(p, [2, 3, 0, 0], [0.5, 0.5, 0.0, 0.0], &bones).unwrap();
        assert!((both - Vec3::new(0.5, 0.5, 0.0)).length() < 1e-6);
        // A weight on a bone not drawn is dropped and the rest renormalised.
        let some = skin_point(p, [2, 9, 0, 0], [0.5, 0.5, 0.0, 0.0], &bones).unwrap();
        assert!((some - Vec3::X).length() < 1e-6);
        assert!(skin_point(p, [9, 9, 0, 0], [1.0, 0.0, 0.0, 0.0], &bones).is_none());
    }

    #[test]
    fn the_arm_systems_have_no_conflicting_queries() {
        let mut world = bevy::ecs::world::World::new();
        let mut schedule = bevy::ecs::schedule::Schedule::default();
        schedule.add_systems((gather, skin));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }

    #[test]
    fn the_whole_arm_is_drawn_not_the_body() {
        // Bone 0 the shoulder (the body), 1 the upper arm, 2.. the forearm, hand and fingers.
        let arm = |b: usize| (1..=8).contains(&b);
        let on = ([2, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]);
        let upper = ([1, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]);
        let body = ([0, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]);
        assert!(keeps_triangle([on, on, upper], &arm));
        assert!(!keeps_triangle([on, upper, body], &arm));
    }
}
