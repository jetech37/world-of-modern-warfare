//! The player's own body in first person: look down and see your torso and legs walking under
//! you, and the mount you ride. benilla's first-person fade hides the whole character at the eye;
//! this draws a copy of the body's own parts in its place (the same triangles, texture, light and
//! animation), with the head taken away (the camera sits at the eyes) and the arms (the
//! first-person arms, crate::mw2::wow_arms, are the character's own arms, posed onto the gun or
//! the melee weapon; the body's would swing a second pair at its sides).
//!
//! How it stands ([`pose_body`]):
//!
//! - **skinned from the rig's own pose**: its model-space bone frames (`RigPose::model`,
//!   benilla's model pass) by the inverse bind poses, the product the GPU palette rows hold before
//!   the rig root's frame. Each copy is a child of the rig root (`RigPose::joints_root`) at
//!   identity, so the root's own frame (its place, turn and scale) puts it where the body is;
//! - **seated under the eye** ([`seat_offset`], [`NECK_BELOW`]): from its posed neck, so it follows
//!   a crouch, a prone body and a rider's seat, the copy is moved so its neck stands under the
//!   eye, low enough that the chest is never a hand's breadth under it;
//! - **cut at the eye's clip plane** ([`behind_eye_clip`], [`CLIP_LEAN`]): a plane through the eye
//!   leaning just past the view's reach behind straight down takes what is behind the eye (the
//!   neck, the collar, the back of the shoulders); a triangle across it is folded onto it
//!   ([`fold_onto_clip`]), and the plane holds the eye, so the cut is never seen;
//! - **closed where the head and arms came away** ([`pinch_rings`]): the neck's and each
//!   shoulder's cut ring is drawn pinched to its own centre, so looking down there is no hole in
//!   the body, whatever pose the animation takes;
//! - **out of the view ahead** ([`under_level_view`]): any triangle reaching within [`EYE_FLOOR`]
//!   of the eye's height or into a level look straight ahead goes, so a cloak's collar, a tabard
//!   or a bow never comes up into the view;
//! - **drawn past the gun** ([`push_from_eye`]): each vertex slides out along its own ray from the
//!   eye to beyond the gun's and the arms' reach. The picture is the same, but the gun and arms
//!   always draw over the body, never in it, and nothing of it is near the near plane.
//!
//! It once read the palette rows by the slot in the body parts' tags and placed a world root at
//! that slot's origin; live, that drew the body enormous round the camera. Live, the rig-pose
//! placement measures 0.00 yd from those rows ([`log_placement`]); headless, the same maths puts
//! the copy on the body in every pose (`fp_body_real.rs`). Then it leaned the upper body back for
//! the widest look (straight down), which put the torso behind the lower edge of every real view:
//! at 70 degrees down nothing but the feet showed, under the gun.
//!
//! Live, look down with `WOW_PROBE_CAM="0,-75@40"` (an absolute camera pitch), not
//! `WOW_PROBE_PITCH`, which aims the mover's swim pitch and leaves the camera where it was (45
//! degrees down at most, where nothing of the body is in view), nor a probe `mouse:` line, which
//! an unfocused window drops. A run looking down so once read as "the body never draws".
//!
//! The mount is benilla's own model, kept drawn through its first-person fade
//! ([`benilla_app::fusion::FirstPersonBody`]) whenever we ride in first person, and so does its
//! blob shadow; the camera goes to the rider's eye on its seat ([`rider_eye`]), so looking down
//! shows the mount's head and neck ahead and the saddle under you.
//!
//! On by default; `MW2_FP_BODY=0` at launch or the CVar `mw2FpBody 0` turns it off ([`FpBodySwitch`]).

use std::collections::HashSet;
use std::hash::{Hash, Hasher};

use benilla_app::Objects;
use benilla_app::fusion::{FirstPersonBody, Stance, UnitAttach};
use benilla_assets::ModelAnimations;
use benilla_assets::materials::WowModelMaterial;
use benilla_formats::CharSkinSlot;
use benilla_world::interact::PickMesh;
use benilla_world::interior::InteriorLit;
use benilla_world::model_fade::FadeMaterials;
use benilla_world::rig_anim::RigPose;
use benilla_world::rig_palette::{RigPalettes, RigPalettesWritten, RigSkin};
use benilla_world::view::{CAM_FOVY, NearClipOverride, WorldCamera};
use bevy::asset::RenderAssetUsages;
use bevy::camera::visibility::NoFrustumCulling;
use bevy::math::Affine3A;
use bevy::mesh::skinning::SkinnedMeshInverseBindposes;
use bevy::mesh::{Indices, MeshTag, PrimitiveTopology};
use bevy::prelude::*;

use crate::mode::FpsState;

/// The M2 attachment point on the head (the helm's): its bone's parent, and everything under
/// that, is the head.
pub const ATTACH_HELM: u16 = 11;

/// A corner leaning this much on a head bone takes its triangle away (the neck's top ring too).
const HEAD_LEAN: f32 = 0.05;
/// A corner leaning at least this much on an arm bone takes its triangle away: the shoulder's cap
/// stays with the chest, the arm goes.
const ARM_LEAN: f32 = 0.5;

/// The near plane first person draws with while the hands are up (yards): the stock `nearclip`
/// default, 0.1, slices an arm swung within a hand's breadth of the eye; 0.03 is within the CVar's
/// own range (0.01-0.33), and the reverse-Z depth keeps its precision.
pub const FP_NEARCLIP: f32 = 0.03;

/// Nothing nearer the eye than this (yards) is drawn: the near plane (0.1 yd, `nearclip`) would
/// slice it ([`too_near`]; the form limbs' rule, crate::form_limbs). The body is pushed far past
/// it ([`push_from_eye`]).
pub const NEAR_CLEAR: f32 = 0.2;

/// Whether a triangle of the body is kept: no corner on the head, none mostly on an arm.
/// `head(b)` and `arm(b)` classify global bone indices.
pub fn keeps_body_triangle(
    corners: [([u16; 4], [f32; 4]); 3],
    head: &dyn Fn(usize) -> bool,
    arm: &dyn Fn(usize) -> bool,
) -> bool {
    corners.iter().all(|(j, w)| {
        let lean = |f: &dyn Fn(usize) -> bool| {
            j.iter()
                .zip(w)
                .filter(|(j, _)| f(usize::from(**j)))
                .map(|(_, w)| *w)
                .sum::<f32>()
        };
        lean(head) < HEAD_LEAN && lean(arm) < ARM_LEAN
    })
}

/// One part's triangles for [`openings`]: its vertex positions, its triangles and which are kept.
pub struct PartTris<'a> {
    pub positions: &'a [[f32; 3]],
    pub tris: &'a [[u32; 3]],
    pub kept: &'a [bool],
}

/// Each part's vertices on the edge where kept triangles meet dropped ones: the openings. Matched
/// by position, as a body's parts (its geosets) each carry their own copy of a seam's vertices.
pub fn openings(parts: &[PartTris]) -> Vec<Vec<u32>> {
    let at = |p: [f32; 3]| p.map(f32::to_bits);
    let mut dropped = HashSet::new();
    for part in parts {
        for (t, &k) in part.tris.iter().zip(part.kept) {
            if !k {
                dropped.extend(t.iter().map(|&v| at(part.positions[v as usize])));
            }
        }
    }
    parts
        .iter()
        .map(|part| {
            let mut out: Vec<u32> = part
                .tris
                .iter()
                .zip(part.kept)
                .filter(|(_, k)| **k)
                .flat_map(|(t, _)| t.iter().copied())
                .filter(|&v| dropped.contains(&at(part.positions[v as usize])))
                .collect();
            out.sort_unstable();
            out.dedup();
            out
        })
        .collect()
}

/// How far under the eye the copy's highest corner stays (yards): nothing of it at the eye or
/// over it, whatever an animation lifts (a cloak's collar, a shrug, a jump).
pub const EYE_FLOOR: f32 = 0.1;
/// How far under a level look's lower edge the copy stays (yards).
pub const VIEW_MARGIN: f32 = 0.03;

/// Where the copy's neck stands, ahead of the eye (yards; behind it, negative): the camera sits
/// over the model's origin (benilla's pivot, attachment 17), but a head is not there, a hunched
/// orc's or tauren's well forward of it, a human's about over it. The copy is set so its neck, as
/// posed, is right under the eye: a neck further back leaves only the feet in a look 70 degrees
/// down (the fit was swept against the real models, `fp_body_real.rs`' `probe_fit`).
pub const NECK_AHEAD: f32 = 0.0;
/// The most the copy is moved to seat its neck (yards).
pub const MAX_SEAT: f32 = 0.7;
/// How far under the eye the copy's neck stands (yards), lower than a human's under benilla's
/// camera (0.17): seated so, the top of the chest stays more than a hand's breadth under the eye
/// (from 0.18 yd it drew as a stretched pale wedge over half the view), and the legs are
/// shortened a few percent, which looking down along them is unseen. A tauren's camera pivot is under its own neck: its copy is squashed down about the
/// feet ([`seat_squash`]), which looking down along the body is all but unseen.
pub const NECK_BELOW: f32 = 0.3;

/// The vertical scale about the feet that puts a neck `neck_y` over them [`NECK_BELOW`] under an
/// eye `eye_y` over them: never a stretch, never under 0.6.
pub fn seat_squash(neck_y: Option<f32>, eye_y: f32) -> f32 {
    seat_squash_below(neck_y, eye_y, NECK_BELOW)
}

/// [`seat_squash`] with the neck `below` the eye.
pub fn seat_squash_below(neck_y: Option<f32>, eye_y: f32, below: f32) -> f32 {
    neck_y
        .filter(|&n| n > 0.1)
        .map_or(1.0, |n| ((eye_y - below) / n).clamp(0.6, 1.0))
}

/// How far to move the copy forward (along the level forward `fwd`) to put the centre of its
/// neck `neck` (root-relative, world axes) `ahead` of the eye ([`BodyFit::neck_ahead`]).
pub fn seat_offset(neck: Option<Vec3>, eye: Vec3, fwd: Vec3, ahead: f32) -> f32 {
    neck.map_or(0.0, |n| {
        (ahead - (n - eye).dot(fwd)).clamp(-MAX_SEAT, MAX_SEAT)
    })
}

/// How far behind straight down the eye's clip plane leans (radians): the plane holds the eye and
/// the view's sideways axis, and the stock 45 degree view looking 89 degrees down (the camera's
/// pitch limit) reaches 21.5 degrees behind straight down, so nothing behind the plane is ever in
/// view, at any pitch. The neck, the collar and the top of the chest a hand's breadth under the
/// eye lie behind it once the body is seated back ([`BodyFit::neck_ahead`]).
pub const CLIP_LEAN: f32 = CAM_FOVY * 0.5 + 0.026;

/// The eye clip plane's normal for the level forward `fwd`: forward and down, [`CLIP_LEAN`] off
/// level forward. A point `d` from the eye with `d · n < 0` is behind the plane.
pub fn clip_normal(fwd: Vec3, lean: f32) -> Vec3 {
    fwd * lean.cos() - Vec3::Y * lean.sin()
}

/// Whether a triangle (corners `d` from the eye) lies wholly behind the eye's clip plane `n`
/// ([`clip_normal`]): it goes. One with a corner in front stays, so the edge where the body is
/// cut away is made of corners behind the plane, out of every view: no hole is ever seen, and the
/// plane holds the eye, so no cut can be looked into.
pub fn behind_eye_clip(d: [Vec3; 3], n: Vec3) -> bool {
    d.iter().all(|c| c.dot(n) < 0.0)
}

/// Fold every corner behind the clip plane `n` (through `eye`) of a drawn triangle (`indices`, a
/// collapsed triangle not drawn) onto the plane, square to it. A triangle across the plane then
/// ends on it, which no view sees, so the collar's corners a hand's breadth behind the eye no
/// longer stretch the chest's triangles into a wedge; a vertex is moved once for all its
/// triangles, so nothing cracks.
pub fn fold_onto_clip(pts: &mut [Vec3], indices: &[u32], eye: Vec3, n: Vec3) {
    for t in indices.chunks_exact(3) {
        if t[0] == t[1] && t[1] == t[2] {
            continue;
        }
        for &v in t {
            let q = &mut pts[v as usize];
            let s = (*q - eye).dot(n);
            if s < 0.0 {
                *q -= n * s;
            }
        }
    }
}

/// How the copy is fitted under the eye: where its neck stands and how the clip plane leans.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BodyFit {
    /// The neck's place ahead of the eye (yards, behind negative), [`seat_offset`]'s `ahead`.
    pub neck_ahead: f32,
    /// [`CLIP_LEAN`].
    pub clip_lean: f32,
    /// Fold the corners behind the plane of the triangles kept onto it ([`fold_onto_clip`]).
    pub fold: bool,
    /// How far under the eye the neck stands ([`NECK_BELOW`]).
    pub neck_below: f32,
}

impl BodyFit {
    pub const DEFAULT: Self = Self {
        neck_ahead: NECK_AHEAD,
        clip_lean: CLIP_LEAN,
        fold: true,
        neck_below: NECK_BELOW,
    };
}

/// How far from the eye the held gun and the first-person arms reach (yards): MW2's hip pose,
/// hands to muzzle (`mw2::arm_cut_tests`' idles). Looking down, the chest lies where the gun is.
pub const GUN_REACH: f32 = 1.0;
/// The copy is drawn no nearer the eye than this (yards), just beyond [`GUN_REACH`]...
pub const PUSH_NEAR: f32 = 1.05;
/// ...and as it is from this far out: in between, each vertex slides out along its own ray from
/// the eye, which keeps the picture the same and puts the gun and arms in front of it.
pub const PUSH_FAR: f32 = 1.6;

/// Where a vertex `d` from the eye is drawn: out along its own ray to at least [`PUSH_NEAR`],
/// ordered as before (a near vertex stays nearer than a far one) and unmoved from [`PUSH_FAR`]
/// out. Seen from the eye nothing changes, the triangle's image included, but the body is never
/// in front of the gun or the arms, and never near the near plane. `height` is the eye's over
/// the feet: the push stays short of the ground under them (a gnome's eye is about a yard up, so
/// its body gives way less).
pub fn push_from_eye(d: Vec3, height: f32) -> Vec3 {
    let near = PUSH_NEAR.min(0.7 * height).max(0.3);
    let far = PUSH_FAR.min(0.95 * height).max(near + 0.1);
    let r = d.length();
    if r >= far || r < 1e-5 {
        return d;
    }
    d * ((near + r * (far - near) / far) / r)
}

/// Whether a point `d` from the eye (yards, world axes) is out of a level look straight ahead,
/// `fwd` the level forward: under the eye's floor and under the view's lower edge. A triangle
/// with every corner so is below that edge whole, so no view ray straight ahead meets it.
pub fn under_level_view(d: Vec3, fwd: Vec3, tan_half_v: f32) -> bool {
    let below = -d.y;
    below >= EYE_FLOOR && below >= d.dot(fwd) * tan_half_v + VIEW_MARGIN
}

/// Draw each cut ring pinched to its own centre: every vertex of `ring` (a ring id per vertex
/// per part, `0` none) moves to the mean of its ring's vertices across all parts, so the opening
/// closes. `pts` are the parts' posed vertices.
pub fn pinch_rings(pts: &mut [Vec<Vec3>], rings: &[&[u8]]) {
    let mut sum = [(Vec3::ZERO, 0u32); 4];
    for (p, r) in pts.iter().zip(rings) {
        for (q, &k) in p.iter().zip(*r) {
            if k != 0 {
                let s = &mut sum[usize::from(k)];
                s.0 += *q;
                s.1 += 1;
            }
        }
    }
    for (p, r) in pts.iter_mut().zip(rings) {
        for (q, &k) in p.iter_mut().zip(*r) {
            if k != 0 {
                let (s, n) = sum[usize::from(k)];
                *q = s / n as f32;
            }
        }
    }
}

/// The point of triangle `t` nearest `p` (Ericson, Real-Time Collision Detection 5.1.5).
pub(crate) fn nearest_on_triangle(p: Vec3, [a, b, c]: [Vec3; 3]) -> Vec3 {
    let (ab, ac, ap) = (b - a, c - a, p - a);
    let (d1, d2) = (ab.dot(ap), ac.dot(ap));
    if d1 <= 0.0 && d2 <= 0.0 {
        return a;
    }
    let bp = p - b;
    let (d3, d4) = (ab.dot(bp), ac.dot(bp));
    if d3 >= 0.0 && d4 <= d3 {
        return b;
    }
    let vc = d1 * d4 - d3 * d2;
    if vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 {
        return a + ab * (d1 / (d1 - d3));
    }
    let cp = p - c;
    let (d5, d6) = (ab.dot(cp), ac.dot(cp));
    if d6 >= 0.0 && d5 <= d6 {
        return c;
    }
    let vb = d5 * d2 - d1 * d6;
    if vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 {
        return a + ac * (d2 / (d2 - d6));
    }
    let va = d3 * d6 - d5 * d4;
    if va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0 {
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }
    let denom = va + vb + vc;
    if denom.abs() < f32::EPSILON {
        return a; // degenerate
    }
    a + ab * (vb / denom) + ac * (vc / denom)
}

/// Whether a triangle comes too near the eye to draw: any point of it, not only a corner, within
/// [`NEAR_CLEAR`], so a wide panel of armour passing by the eye goes too.
pub fn too_near(corners: [Vec3; 3], eye: Vec3) -> bool {
    nearest_on_triangle(eye, corners).distance_squared(eye) < NEAR_CLEAR * NEAR_CLEAR
}

/// The widest angle any edge of a body triangle may span from the eye (radians): a triangle
/// passing by the eye spans half the view whatever its distance, and the push keeps its image.
pub const MAX_SPAN: f32 = 1.6;
/// Nothing of the body comes nearer the eye than this (yards), before the push.
pub const EYE_CLEAR: f32 = 0.05;

/// Whether a triangle wraps round the eye: armour passing beside the camera, which fills the view
/// however small it is ([`MAX_SPAN`], [`EYE_CLEAR`]). A collar a hand's breadth under the eye
/// spans little and stays.
pub fn wraps_eye(corners: [Vec3; 3], eye: Vec3) -> bool {
    if nearest_on_triangle(eye, corners).distance_squared(eye) < EYE_CLEAR * EYE_CLEAR {
        return true;
    }
    let d = corners.map(|c| (c - eye).normalize_or_zero());
    (0..3).any(|k| d[k].dot(d[(k + 1) % 3]) < MAX_SPAN.cos())
}

/// The cut rings ([`CopySource::rings`]): the neck, and the arms' at either shoulder.
pub const RING_NECK: u8 = 1;
pub const RING_ARM_POS_X: u8 = 2;
pub const RING_ARM_NEG_X: u8 = 3;

/// One body part's copy, in the model's own space: its vertices, what is kept of its triangles
/// and which cut ring each vertex is on.
#[derive(Clone, Debug, Default)]
pub struct CopySource {
    pub positions: Vec<Vec3>,
    pub normals: Vec<Vec3>,
    pub joints: Vec<[u16; 4]>,
    pub weights: Vec<[f32; 4]>,
    pub tris: Vec<[u32; 3]>,
    /// Per vertex: the cut ring it is on ([`RING_NECK`], [`RING_ARM_POS_X`], [`RING_ARM_NEG_X`]),
    /// `0` for none.
    pub rings: Vec<u8>,
}

/// The copies of a body's parts: the head and arms taken away (`head(b)`, `arm(b)` classify
/// bones) and the cut rings found across all parts, index-parallel with `parts`.
pub fn copy_sources(
    parts: &[&benilla_formats::RenderSubmesh],
    head: &dyn Fn(usize) -> bool,
    arm: &dyn Fn(usize) -> bool,
) -> Vec<CopySource> {
    let corner = |sub: &benilla_formats::RenderSubmesh, i: u32| {
        let i = i as usize;
        (
            sub.joints.get(i).copied().unwrap_or_default(),
            sub.weights.get(i).copied().unwrap_or_default(),
        )
    };
    let tris: Vec<Vec<[u32; 3]>> = parts
        .iter()
        .map(|sub| {
            sub.indices
                .as_chunks::<3>()
                .0
                .iter()
                .map(|t| [t[0], t[1], t[2]])
                .collect()
        })
        .collect();
    // Which triangles stay, and which would with only the head (or only the arms) taken away: a
    // ring is where the kept body meets what the one cut took.
    let keep = |head: &dyn Fn(usize) -> bool, arm: &dyn Fn(usize) -> bool| -> Vec<Vec<bool>> {
        parts
            .iter()
            .zip(&tris)
            .map(|(sub, ts)| {
                ts.iter()
                    .map(|t| keeps_body_triangle(t.map(|v| corner(sub, v)), head, arm))
                    .collect()
            })
            .collect()
    };
    let none = |_: usize| false;
    let kept = keep(head, arm);
    let ring_of = |only: &[Vec<bool>]| {
        openings(
            &parts
                .iter()
                .zip(&tris)
                .zip(only)
                .map(|((sub, ts), k)| PartTris {
                    positions: &sub.positions,
                    tris: ts,
                    kept: k,
                })
                .collect::<Vec<_>>(),
        )
    };
    let neck = ring_of(&keep(head, &none));
    let arms = ring_of(&keep(&none, arm));
    parts
        .iter()
        .zip(tris)
        .zip(kept)
        .zip(neck.into_iter().zip(arms))
        .map(|(((sub, ts), kept), (neck, arms))| {
            let n = sub.positions.len();
            let positions: Vec<Vec3> = sub
                .positions
                .iter()
                .map(|p| benilla_assets::coords::wow_to_bevy(*p))
                .collect();
            let tris: Vec<[u32; 3]> = ts
                .into_iter()
                .zip(&kept)
                .filter_map(|(t, k)| k.then_some(t))
                .collect();
            // Only the rings round what is still drawn.
            let on_kept: HashSet<u32> = tris.iter().flatten().copied().collect();
            let mut rings = vec![0u8; n];
            for v in arms.into_iter().filter(|v| on_kept.contains(v)) {
                rings[v as usize] = if positions[v as usize].x >= 0.0 {
                    RING_ARM_POS_X
                } else {
                    RING_ARM_NEG_X
                };
            }
            for v in neck.into_iter().filter(|v| on_kept.contains(v)) {
                rings[v as usize] = RING_NECK;
            }
            CopySource {
                normals: (0..n)
                    .map(|i| {
                        sub.normals
                            .get(i)
                            .map_or(Vec3::Y, |v| benilla_assets::coords::wow_to_bevy(*v))
                    })
                    .collect(),
                joints: (0..n).map(|i| corner(sub, i as u32).0).collect(),
                weights: (0..n).map(|i| corner(sub, i as u32).1).collect(),
                positions,
                tris,
                rings,
            }
        })
        .collect()
}

/// The camera the copy is posed for: its world position and rotation, and the tangent of half
/// its vertical field of view.
#[derive(Clone, Copy, Debug)]
pub struct EyeView {
    pub eye: Vec3,
    pub look: Quat,
    pub tan_half_v: f32,
}

/// One part posed, in the model's own space (the rig root's frame), with its index list, the
/// same length every frame (a triangle taken away is collapsed onto one corner).
#[derive(Clone, Debug, Default)]
pub struct PosedPart {
    /// Where the body stands (seated, its rings pinched): what the placement guard and the tests
    /// read.
    pub placed: Vec<Vec3>,
    /// What is drawn: `placed` pushed out from the eye ([`push_from_eye`]).
    pub positions: Vec<Vec3>,
    pub normals: Vec<Vec3>,
    pub indices: Vec<u32>,
}

/// A vertex skinned by its weighted bones, `skin[b]` the bone's model-space matrix
/// (`model[b] × inverse bind pose[b]`).
fn skin_point(skin: &[Mat4], p: Vec3, j: [u16; 4], w: [f32; 4]) -> Option<Vec3> {
    let mut sum = Vec3::ZERO;
    let mut total = 0.0;
    for (j, w) in j.iter().zip(w) {
        if w > 0.0
            && let Some(m) = skin.get(usize::from(*j))
        {
            sum += m.transform_point3(p) * w;
            total += w;
        }
    }
    (total > 0.0).then(|| sum / total)
}

/// Pose the copy (the module's list): skin every part in model space, seat it under the eye,
/// pinch its cut rings shut, take away what would rise into a level look, and draw what is left
/// pushed out past the gun. `root` is the rig root's world frame (`RigPose::joints_root`), whose
/// child the copy is. Index-parallel with `sources`.
pub fn pose_body(
    sources: &[CopySource],
    skin: &[Mat4],
    root: Affine3A,
    view: &EyeView,
) -> Vec<PosedPart> {
    pose_body_fit(sources, skin, root, view, BodyFit::DEFAULT)
}

/// [`pose_body`] with its fit spelled out (the probes sweep it).
pub fn pose_body_fit(
    sources: &[CopySource],
    skin: &[Mat4],
    root: Affine3A,
    view: &EyeView,
    fit: BodyFit,
) -> Vec<PosedPart> {
    // The root's linear part: model space to world directions; world offsets from the root's
    // origin are measured in yards whatever the model's scale.
    let lin = Mat3::from(root.matrix3);
    let unlin = lin.inverse();
    let eye = view.eye - Vec3::from(root.translation);
    let fwd = {
        let f = view.look * Vec3::NEG_Z;
        Vec3::new(f.x, 0.0, f.z)
            .try_normalize()
            .unwrap_or(Vec3::NEG_Z)
    };
    // Posed, root-relative, world-oriented.
    let mut posed: Vec<Vec<Vec3>> = sources
        .iter()
        .map(|p| {
            (0..p.positions.len())
                .map(|i| {
                    lin * skin_point(skin, p.positions[i], p.joints[i], p.weights[i])
                        .unwrap_or(Vec3::ZERO)
                })
                .collect()
        })
        .collect();
    // Seated from the neck as posed: the eye stays over the chest however the body moves (a
    // crouch's bent knees and dropped hips, a prone body lying down, a rider's seat).
    let neck = {
        let ring: Vec<Vec3> = sources
            .iter()
            .zip(&posed)
            .flat_map(|(p, pts)| {
                p.rings
                    .iter()
                    .zip(pts)
                    .filter(|(r, _)| **r == RING_NECK)
                    .map(|(_, q)| *q)
            })
            .collect();
        (!ring.is_empty()).then(|| ring.iter().sum::<Vec3>() / ring.len() as f32)
    };
    let seat = fwd * seat_offset(neck, eye, fwd, fit.neck_ahead);
    let squash = Vec3::new(
        1.0,
        seat_squash_below(neck.map(|n| n.y), eye.y, fit.neck_below),
        1.0,
    );
    for pts in &mut posed {
        for q in pts.iter_mut() {
            *q = *q * squash + seat;
        }
    }
    let clip = clip_normal(fwd, fit.clip_lean);
    pinch_rings(
        &mut posed,
        &sources
            .iter()
            .map(|p| p.rings.as_slice())
            .collect::<Vec<_>>(),
    );
    sources
        .iter()
        .zip(posed)
        .map(|(p, mut pts)| {
            let indices: Vec<u32> = p
                .tris
                .iter()
                .flat_map(|t| {
                    let corners = t.map(|v| pts[v as usize]);
                    let rel = corners.map(|q| q - eye);
                    let drawn = !behind_eye_clip(rel, clip)
                        && !wraps_eye(corners, eye)
                        && corners
                            .iter()
                            .all(|&q| under_level_view(q - eye, fwd, view.tan_half_v));
                    if drawn { *t } else { [t[0]; 3] }
                })
                .collect();
            if fit.fold {
                fold_onto_clip(&mut pts, &indices, eye, clip);
            }
            let normals = (0..p.normals.len())
                .map(|i| {
                    p.joints[i]
                        .iter()
                        .zip(p.weights[i])
                        .filter(|(_, w)| *w > 0.0)
                        .max_by(|a, b| a.1.total_cmp(&b.1))
                        .and_then(|(j, _)| skin.get(usize::from(*j)))
                        .map_or(Vec3::Y, |m| {
                            m.transform_vector3(p.normals[i]).normalize_or_zero()
                        })
                })
                .collect();
            PosedPart {
                positions: pts
                    .iter()
                    .map(|&q| unlin * (eye + push_from_eye(q - eye, eye.y)))
                    .collect(),
                placed: pts.into_iter().map(|q| unlin * q).collect(),
                normals,
                indices,
            }
        })
        .collect()
}

/// What decides whether the body is drawn in first person.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct BodyView {
    /// The layer is on and in the world.
    pub active: bool,
    /// Third person (crate::thirdperson): the real body is drawn.
    pub third_person: bool,
    /// The killcam has the camera.
    pub killcam: bool,
    pub stance: Stance,
    pub mounted: bool,
    /// Dead or a ghost.
    pub dead: bool,
    /// Down (crate::downed): the body crawls, prone.
    pub downed: bool,
    /// In a form (crate::forms): the humanoid body is not the form's; nothing is drawn.
    pub in_form: bool,
}

/// The body is drawn alive in first person, in every stance and riding: crouched or prone our
/// own body takes the stance's pose as others see it (crate::body_state's first-person target)
/// and the copy is seated from its posed neck, so the eye never sinks into it.
pub fn body_shown(v: BodyView) -> bool {
    v.active && !v.third_person && !v.killcam && !v.dead && !v.downed && !v.in_form
}

/// One body part's copy: the source part, and the copy's entity and mesh.
struct PartCopy {
    source: Entity,
    copy: Entity,
    mesh: Handle<Mesh>,
}

/// The body's copies, rebuilt when the body's parts change.
#[derive(Resource, Default)]
pub struct FpBody {
    parts: Vec<PartCopy>,
    /// Each part's geometry, index-parallel with `parts`.
    geometry: Vec<CopySource>,
    signature: u64,
    /// The rig root the copies hang from (`RigPose::joints_root`).
    root: Option<Entity>,
    /// Drawn this frame.
    pub shown: bool,
    /// The placement has been measured against the palette rows (once a gathering).
    checked: bool,
}

#[derive(Component)]
pub struct FpBodyPart;

pub struct FpBodyPlugin;

/// The CVar that turns the first-person body off (`/console mw2FpBody 0`, saved with the others).
const SWITCH_CVAR: &str = "mw2FpBody";
/// The CVar poll (seconds).
const SWITCH_POLL_SECS: f32 = 2.0;

/// Whether the first-person body is drawn. On by default. `MW2_FP_BODY=0` (or `1`) at launch
/// decides it outright; otherwise the CVar [`SWITCH_CVAR`] is read every two seconds.
#[derive(Resource, Debug, Clone)]
pub struct FpBodySwitch {
    pub on: bool,
    /// The environment's word, which the CVar does not override.
    forced: Option<bool>,
    next_poll: f32,
    /// Lua answered (a VM with no `GetCVar` stops the poll).
    lua: bool,
}

impl FpBodySwitch {
    pub fn from_env(env: Option<&str>) -> Self {
        let forced = env.and_then(parse_switch);
        Self {
            on: forced.unwrap_or(true),
            forced,
            next_poll: 0.0,
            lua: true,
        }
    }
}

/// `"0"`/`"1"` (and the usual spellings) to off/on.
pub fn parse_switch(v: &str) -> Option<bool> {
    match v.trim().to_ascii_lowercase().as_str() {
        "0" | "off" | "false" | "no" => Some(false),
        "1" | "on" | "true" | "yes" => Some(true),
        _ => None,
    }
}

fn body_on(switch: Res<FpBodySwitch>) -> bool {
    switch.on
}

fn poll_switch(
    time: Res<Time>,
    mut switch: ResMut<FpBodySwitch>,
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
    switch.next_poll = t + SWITCH_POLL_SECS;
    match crate::lua::eval::<Option<String>>(
        &script,
        &format!("RegisterCVar(\"{SWITCH_CVAR}\", \"1\") return GetCVar(\"{SWITCH_CVAR}\")"),
    ) {
        Some(v) => {
            let on = v.as_deref().and_then(parse_switch).unwrap_or(true);
            if switch.on != on {
                info!(
                    "benilla-mw2: the first-person body {}",
                    if on { "on" } else { "off" }
                );
                switch.on = on;
            }
        }
        None => switch.lua = false,
    }
}

impl Plugin for FpBodyPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<FpBody>()
            .init_resource::<FpBodyInputs>()
            .init_resource::<NearClipOverride>()
            .insert_resource(FpBodySwitch::from_env(
                std::env::var("MW2_FP_BODY").ok().as_deref(),
            ))
            .add_systems(
                Update,
                (
                    poll_switch,
                    read_inputs,
                    gather.run_if(body_on),
                    keep_mount,
                    near_clip,
                )
                    .chain()
                    .after(crate::Mw2Set::Hud),
            )
            // After benilla seats the camera and the stance lowers it, before the crosshair's ray
            // is cast: a rider's eye is on its seat.
            .add_systems(
                Update,
                seat_rider_camera
                    .after(crate::stance_view::apply_stance_view)
                    .before(crate::Mw2Set::Aim),
            )
            // After propagation and the rig passes (`RigPalettesWritten` follows both): the rig
            // root's frame and the rig's model pose are this frame's. It runs switched off too, to
            // take the copies down.
            .add_systems(PostUpdate, skin.after(RigPalettesWritten));
    }
}

/// What `gather` and `skin` read of the player and the layer, gathered once a frame
/// ([`read_inputs`]): the player's entity, its helm point's bone and the view state.
#[derive(Resource, Default, Clone, Debug)]
pub struct FpBodyInputs {
    pub me: Option<Entity>,
    pub helm: Option<u16>,
    pub view: BodyView,
}

fn read_inputs(inputs: ViewInputs, attach: UnitAttach, mut out: ResMut<FpBodyInputs>) {
    let me = inputs.objects.player();
    out.me = me;
    out.helm = me
        .and_then(|e| attach.point(e, ATTACH_HELM))
        .map(|(b, _)| b);
    out.view = inputs.view();
}

/// The parts drawn in the copy: the body's own skin, cape and fur, never its hair.
fn copied_slot(slot: Option<CharSkinSlot>) -> bool {
    matches!(
        slot,
        Some(CharSkinSlot::Body | CharSkinSlot::Object | CharSkinSlot::SkinExtra)
    )
}

/// The head bone from the helm's attachment bone: its parent (the attachment bones are leaves on
/// the head in every race's model, `examples/wow_arm_cut.rs`), else itself.
pub fn head_of(parents: &[i16], helm: usize) -> usize {
    parents
        .get(helm)
        .and_then(|&p| usize::try_from(p).ok())
        .unwrap_or(helm)
}

/// Every bone at or under `root`.
fn subtree(parents: &[i16], root: usize) -> Vec<bool> {
    let mut under = vec![false; parents.len()];
    for (b, flag) in under.iter_mut().enumerate() {
        let mut cur = Some(b);
        for _ in 0..parents.len() {
            match cur {
                Some(c) if c == root => {
                    *flag = true;
                    break;
                }
                Some(c) => cur = usize::try_from(*parents.get(c).unwrap_or(&-1)).ok(),
                None => break,
            }
        }
    }
    under
}

/// Take the copies down.
fn clear_copies(commands: &mut Commands, body: &mut FpBody) {
    for p in body.parts.drain(..) {
        // The rig root's teardown may have taken it already.
        commands.entity(p.copy).try_despawn();
    }
    body.geometry.clear();
    body.signature = 0;
    body.root = None;
    body.checked = false;
}

/// Build the copies from the player's body parts, again when they or the rig root change. Each
/// copy is a child of the rig root (`RigPose::joints_root`, the frame the GPU skins the body in),
/// at identity, its vertices in the model's own space.
#[allow(clippy::type_complexity)]
pub fn gather(
    mut commands: Commands,
    inputs: Res<FpBodyInputs>,
    roots: Query<(&RigPose, &ModelAnimations, &Children)>,
    parts: Query<(Entity, &PickMesh, &FadeMaterials, &MeshTag)>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut body: ResMut<FpBody>,
) {
    let Some(me) = inputs.me else {
        if !body.parts.is_empty() || body.signature != 0 {
            clear_copies(&mut commands, &mut body);
        }
        return;
    };
    let Ok((pose, anims, children)) = roots.get(me) else {
        return;
    };
    let sources: Vec<(Entity, &PickMesh, &FadeMaterials, &MeshTag)> = children
        .iter()
        .filter_map(|c| parts.get(c).ok())
        .filter(|(_, p, ..)| copied_slot(p.0.char_slot))
        .collect();
    let mut h = std::collections::hash_map::DefaultHasher::new();
    me.hash(&mut h);
    pose.joints_root.hash(&mut h);
    for (e, p, fm, _) in &sources {
        e.hash(&mut h);
        p.0.positions.len().hash(&mut h);
        fm.cutout.id().hash(&mut h);
    }
    let signature = h.finish();
    if signature == body.signature {
        return;
    }
    clear_copies(&mut commands, &mut body);
    body.signature = signature;
    let masks = &anims.pose.bone_masks;
    // The arms: masks 0 and 1 hold every bone outside the right and the left arm, whose root is
    // the shoulder key-bone (the clavicle); it stays with the chest, the upper arm down goes.
    let in_arm = |b: usize| masks.get(b).is_some_and(|m| m & 0b11 != 0b11);
    let arm = |b: usize| {
        in_arm(b)
            && pose
                .parents
                .get(b)
                .and_then(|&p| usize::try_from(p).ok())
                .is_some_and(in_arm)
    };
    // The helm's point hangs off the head bone (KeyBoneID 6) in every race's model.
    let head_root = inputs.helm.map(|b| head_of(&pose.parents, usize::from(b)));
    let head_bones = head_root.map_or_else(Vec::new, |r| subtree(&pose.parents, r));
    let head = |b: usize| head_bones.get(b).copied().unwrap_or(false);
    if head_root.is_none() {
        warn_once!("benilla-mw2: the body has no helm point; its head stays out by the view rules");
    }
    let subs: Vec<&benilla_formats::RenderSubmesh> =
        sources.iter().map(|(_, p, ..)| &*p.0).collect();
    let geometry = copy_sources(&subs, &head, &arm);
    let mut kept_tris = 0;
    for ((source, pick, fm, tag), geo) in sources.into_iter().zip(geometry) {
        if geo.tris.is_empty() {
            continue;
        }
        kept_tris += geo.tris.len();
        let sub = &pick.0;
        let mut mesh = Mesh::new(
            PrimitiveTopology::TriangleList,
            RenderAssetUsages::default(),
        );
        mesh.insert_attribute(
            Mesh::ATTRIBUTE_POSITION,
            geo.positions
                .iter()
                .map(|p| p.to_array())
                .collect::<Vec<_>>(),
        );
        mesh.insert_attribute(
            Mesh::ATTRIBUTE_NORMAL,
            geo.normals.iter().map(|p| p.to_array()).collect::<Vec<_>>(),
        );
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, sub.uvs.clone());
        if sub.vertex_colors.len() == geo.positions.len() {
            mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, sub.vertex_colors.clone());
        }
        // The index list keeps its length for good: `skin` collapses a triangle it takes away.
        mesh.insert_indices(Indices::U32(geo.tris.iter().flatten().copied().collect()));
        let handle = meshes.add(mesh);
        let copy = commands
            .spawn((
                Name::new("mw2 first-person body"),
                FpBodyPart,
                Mesh3d(handle.clone()),
                MeshMaterial3d::<WowModelMaterial>(fm.cutout.clone()),
                MeshTag(benilla_world::mesh_tag::with_alpha(tag.0, 1.0)),
                Transform::IDENTITY,
                Visibility::Hidden,
                NoFrustumCulling,
                ChildOf(pose.joints_root),
            ))
            .id();
        body.parts.push(PartCopy {
            source,
            copy,
            mesh: handle,
        });
        body.geometry.push(geo);
    }
    body.root = Some(pose.joints_root);
    info!(
        "benilla-mw2: the first-person body: {} parts, {kept_tris} triangles (head {}, arms and head taken away)",
        body.parts.len(),
        head_root.map_or_else(|| "-".into(), |b| b.to_string()),
    );
}

/// Whether first person draws with [`FP_NEARCLIP`]: the layer's own first-person view is up with
/// the hands raised (a melee weapon, a spell, a shield), whose swings pass close by the eye. A gun
/// keeps the stock plane, which MW2's own view model and its sights are made for.
pub fn first_person_near(active: bool, third_person: bool, killcam: bool, hands_up: bool) -> bool {
    active && !third_person && !killcam && hands_up
}

/// The layer's near plane while the hands are up in first person, the stock `nearclip` otherwise.
fn near_clip(
    state: Res<FpsState>,
    third: Res<crate::thirdperson::ThirdPerson>,
    hands_up: Res<crate::melee::MeleeHeld>,
    mut near: ResMut<NearClipOverride>,
) {
    let want = first_person_near(state.active, third.on, state.view_away(), hands_up.0)
        .then_some(FP_NEARCLIP);
    if near.0 != want {
        near.0 = want;
    }
}

/// The mount stays drawn in first person while the body is (its fade's release, in benilla), and
/// the body's blob shadow is cast.
fn keep_mount(body: Res<FpBody>, inputs: Res<FpBodyInputs>, mut keep: ResMut<FirstPersonBody>) {
    let want = body.shown || rides_in_first_person(inputs.view);
    if keep.0 != want {
        keep.0 = want;
    }
}

/// Riding, alive, in the layer's own first-person view: the mount stays drawn under the eye
/// (its head, neck and saddle), whether or not the rider's body is, and the eye is on its seat.
pub fn rides_in_first_person(v: BodyView) -> bool {
    v.active && v.mounted && !v.third_person && !v.killcam && !v.dead
}

/// How far over the face point (attachment 17, the head's front) the eye is (yards, model
/// space): benilla's camera pivot is that point's height plus this.
pub const EYE_OVER_FACE: f32 = 0.0972;
/// The face attachment.
pub const ATTACH_FACE: u16 = 17;

/// The rider's eye in the world: the face point `face` (its seat anchor's model space, as the
/// rider is posed on the mount) plus [`EYE_OVER_FACE`], carried by the seat. `seat_prev` and
/// `body_prev` are last frame's world frames of the seat anchor and of the unit (the seat rides
/// the mount, which rides the unit); `body_now` is the unit's frame this frame, so the eye moves
/// with the mount this frame, never a frame behind it.
pub fn rider_eye(body_now: Affine3A, body_prev: Affine3A, seat_prev: Affine3A, face: Vec3) -> Vec3 {
    let seat_in_body = body_prev.inverse() * seat_prev;
    (body_now * seat_in_body).transform_point3(face + Vec3::Y * EYE_OVER_FACE)
}

/// In first person on a mount, the camera goes to the rider's eye: benilla seats it at the
/// standing pivot over the unit's feet, inside the mount's back. There, looking down shows the
/// mount's head and neck ahead and the saddle under you.
#[allow(clippy::type_complexity)]
fn seat_rider_camera(
    state: Res<FpsState>,
    third: Res<crate::thirdperson::ThirdPerson>,
    objects: Objects,
    attach: UnitAttach,
    bodies: Query<(&Transform, &GlobalTransform, &RigPose), Without<WorldCamera>>,
    globals: Query<&GlobalTransform>,
    mut cameras: Query<&mut Transform, With<WorldCamera>>,
) {
    if !state.active || third.on || state.view_away() {
        return;
    }
    let Some(me) = objects.player() else {
        return;
    };
    let mounted = state
        .self_guid
        .and_then(|g| objects.object(g))
        .is_some_and(|f| f.unit_mount_display_id() != 0 && !f.is_dead_or_ghost());
    if !mounted {
        return;
    }
    let Ok((now, prev, pose)) = bodies.get(me) else {
        return;
    };
    // Seated: the rig's frame is the seat anchor under the mount, not the unit.
    if pose.joints_root == me {
        return;
    }
    let (Ok(seat), Some((bone, offset))) =
        (globals.get(pose.joints_root), attach.point(me, ATTACH_FACE))
    else {
        return;
    };
    let Some(frame) = pose.model.get(usize::from(bone)) else {
        return;
    };
    let eye = rider_eye(
        now.compute_affine(),
        prev.affine(),
        seat.affine(),
        frame.transform_point3(offset),
    );
    if !eye.is_finite() {
        return;
    }
    let display = state
        .self_guid
        .and_then(|g| objects.object(g))
        .map_or(0, |f| f.unit_mount_display_id());
    if let Ok(mut cam) = cameras.single_mut() {
        let fwd = cam.rotation * Vec3::NEG_Z;
        cam.translation = eye + mounted_eye_lift(display, fwd);
    }
}

/// The rider's eye lifted and drawn back from the face point (yards): seated, the face point is
/// down among the mount's crest (a gryphon's neck feathers, a drake's horns filled the view
/// looking down, and the near plane sliced them). Up and back along the level look, the neck and
/// head sit ahead and below with the ears or horns in view, the rider's own chest under the eye.
/// The big flyers sit the eye higher and further back; ground mounts barely move it.
pub fn mounted_eye_lift(display: u32, look: Vec3) -> Vec3 {
    let (up, back) = match display {
        // The drakes, proto-drakes and frost wyrms: long necks, crest and horns.
        40027..=40035 | 40037 => (0.45, 0.45),
        // Gryphon, wind rider, hippogryph, dragonhawk, phoenix: a tall feathered neck.
        40019..=40026 | 40036 => (0.35, 0.35),
        0 => (0.0, 0.0),
        _ => (0.12, 0.12),
    };
    let level = Vec3::new(look.x, 0.0, look.z).normalize_or_zero();
    Vec3::Y * up - level * back
}

/// Whether the body shows this frame, from the layer's state and the player's.
#[derive(bevy::ecs::system::SystemParam)]
struct ViewInputs<'w, 's> {
    state: Res<'w, FpsState>,
    third: Res<'w, crate::thirdperson::ThirdPerson>,
    fusion: Res<'w, crate::fusion::Fusion>,
    downed: Res<'w, crate::downed::Downed>,
    form: Res<'w, crate::forms::PlayerForm>,
    objects: Objects<'w, 's>,
}

impl ViewInputs<'_, '_> {
    fn view(&self) -> BodyView {
        let me = self.state.self_guid.and_then(|g| self.objects.object(g));
        BodyView {
            active: self.state.active,
            third_person: self.third.on,
            killcam: self.state.view_away(),
            stance: self.fusion.stance,
            mounted: me.is_some_and(|f| f.unit_mount_display_id() != 0),
            dead: me.is_some_and(|f| f.is_dead_or_ghost() || f.unit_health() == Some(0)),
            downed: self.downed.active,
            in_form: self.form.hides_humanoid_body(),
        }
    }
}

/// The copy's world box over its drawn triangles: `root` places the model-space vertices.
fn drawn_box(posed: &[PosedPart], root: Affine3A) -> Option<(Vec3, Vec3)> {
    let mut b: Option<(Vec3, Vec3)> = None;
    for p in posed {
        for t in p.indices.as_chunks::<3>().0 {
            if t[0] == t[1] && t[1] == t[2] {
                continue; // taken away
            }
            for &v in t {
                let q = root.transform_point3(p.placed[v as usize]);
                b = Some(b.map_or((q, q), |(lo, hi)| (lo.min(q), hi.max(q))));
            }
        }
    }
    b
}

/// Whether the drawn copy (world box `lo..hi`) stands where a body can: its floor within the
/// eye's height of the rig root (a rider's root is the saddle, the feet hang under it), its box
/// round the root and no bigger than one and a half of that height, its top under the eye. A copy
/// outside it is never drawn: the guard against a copy off its body, as live once drew it
/// enormous round the camera.
pub fn placed_sanely(lo: Vec3, hi: Vec3, root: Vec3, eye: Vec3) -> bool {
    let centre = (lo + hi) * 0.5;
    let reach = (eye.y - root.y).max(1.2);
    hi.y <= eye.y
        && (lo.y - root.y).abs() <= reach
        && Vec2::new(centre.x - root.x, centre.z - root.z).length() <= reach
        && (hi - lo).max_element() <= 1.5 * reach
}

/// Pose each copy ([`pose_body`]) from the rig's own pose (its model-space bone frames, as
/// benilla's model pass composes them, by the inverse bind poses), and show it where benilla's
/// fade hides the body. The copy hangs from the rig root at identity, so the root's frame places
/// it as the GPU's palette rows place the body.
#[allow(clippy::type_complexity, clippy::too_many_arguments)]
pub fn skin(
    inputs: Res<FpBodyInputs>,
    rigs: Query<(&RigPose, &RigSkin)>,
    globals: Query<&GlobalTransform>,
    ibps: Res<Assets<SkinnedMeshInverseBindposes>>,
    palettes: Res<RigPalettes>,
    camera: Query<&Transform, (With<WorldCamera>, Without<FpBodyPart>)>,
    sources: Query<
        (&MeshTag, &Visibility, &FadeMaterials, Option<&InteriorLit>),
        Without<FpBodyPart>,
    >,
    mut copies: Query<
        (
            &mut Visibility,
            &mut MeshTag,
            &mut MeshMaterial3d<WowModelMaterial>,
        ),
        With<FpBodyPart>,
    >,
    mut meshes: ResMut<Assets<Mesh>>,
    mut body: ResMut<FpBody>,
    switch: Option<Res<FpBodySwitch>>,
) {
    let body = &mut *body;
    let view = inputs.view;
    let on = switch.is_none_or(|s| s.on);
    let cam = camera.single().ok();
    let rig = inputs
        .me
        .and_then(|me| rigs.get(me).ok())
        .filter(|(pose, _)| body.root == Some(pose.joints_root));
    let root = rig.and_then(|(pose, _)| globals.get(pose.joints_root).ok());
    let ibp = rig.and_then(|(_, s)| ibps.get(s.ibp()));
    // Drawn only where benilla's fade has the body hidden outright: never both at once.
    let faded = body.parts.iter().all(|p| {
        sources
            .get(p.source)
            .is_ok_and(|(_, vis, ..)| *vis == Visibility::Hidden)
    });
    let ready = on && !body.parts.is_empty() && faded && body_shown(view);
    let posed = match (cam, rig, root, ibp) {
        (Some(cam), Some((pose, _)), Some(root), Some(ibp)) if ready => {
            let root = root.affine();
            let skin: Vec<Mat4> = pose
                .model
                .iter()
                .zip(ibp.iter())
                .map(|(m, i)| Mat4::from(*m) * *i)
                .collect();
            let eye = EyeView {
                eye: cam.translation,
                look: cam.rotation,
                tan_half_v: (CAM_FOVY * 0.5).tan(),
            };
            let posed = pose_body(&body.geometry, &skin, root, &eye);
            let sane = drawn_box(&posed, root).is_some_and(|(lo, hi)| {
                let ok = placed_sanely(lo, hi, Vec3::from(root.translation), cam.translation);
                if !ok {
                    warn_once!(
                        "benilla-mw2: the first-person body would stand off the body (box {lo:.2}..{hi:.2}, rig root {:.2}, eye {:.2}); not drawn",
                        Vec3::from(root.translation),
                        cam.translation
                    );
                }
                ok
            });
            if !body.checked {
                body.checked = true;
                log_placement(
                    body,
                    &posed,
                    &skin,
                    root,
                    cam.translation,
                    &sources,
                    &palettes,
                );
            }
            sane.then_some(posed)
        }
        _ => None,
    };
    let Some(posed) = posed else {
        body.shown = false;
        for p in &body.parts {
            if let Ok((mut vis, ..)) = copies.get_mut(p.copy)
                && *vis != Visibility::Hidden
            {
                *vis = Visibility::Hidden;
            }
        }
        return;
    };
    body.shown = true;
    for (p, posed) in body.parts.iter().zip(posed) {
        let Ok((mut vis, mut tag, mut mat)) = copies.get_mut(p.copy) else {
            continue;
        };
        let Ok((src_tag, _, fm, lit)) = sources.get(p.source) else {
            if *vis != Visibility::Hidden {
                *vis = Visibility::Hidden;
            }
            continue;
        };
        if let Some(mesh) = meshes.get_mut(&p.mesh) {
            mesh.insert_attribute(
                Mesh::ATTRIBUTE_POSITION,
                posed
                    .positions
                    .iter()
                    .map(|v| v.to_array())
                    .collect::<Vec<_>>(),
            );
            mesh.insert_attribute(
                Mesh::ATTRIBUTE_NORMAL,
                posed
                    .normals
                    .iter()
                    .map(|v| v.to_array())
                    .collect::<Vec<_>>(),
            );
            mesh.insert_indices(Indices::U32(posed.indices));
        }
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

/// Once a gathering: where the copy stands, and how far the palette rows (the slot in the body
/// parts' tags, the old placement) would have put it from there. A gap says the rows the parts'
/// tags name are not this rig's pose.
#[allow(clippy::type_complexity)]
fn log_placement(
    body: &FpBody,
    posed: &[PosedPart],
    skin: &[Mat4],
    root: Affine3A,
    eye: Vec3,
    sources: &Query<
        (&MeshTag, &Visibility, &FadeMaterials, Option<&InteriorLit>),
        Without<FpBodyPart>,
    >,
    palettes: &RigPalettes,
) {
    let (lo, hi) = drawn_box(posed, root).unwrap_or_default();
    let mut gap = 0.0f32;
    let mut slots = Vec::new();
    for (p, geo) in body.parts.iter().zip(&body.geometry) {
        let Ok((tag, ..)) = sources.get(p.source) else {
            continue;
        };
        let slot = benilla_world::mesh_tag::rig_of(tag.0);
        slots.push(slot);
        let (Some(rows), Some(origin)) = (
            palettes.rig_rows(slot, usize::MAX),
            palettes.slot_origin(slot),
        ) else {
            continue;
        };
        for i in 0..geo.positions.len() {
            let (p, j, w) = (geo.positions[i], geo.joints[i], geo.weights[i]);
            if let (Some(a), Some(b)) = (skin_point(skin, p, j, w), skin_point(&rows, p, j, w)) {
                gap = gap.max(root.transform_point3(a).distance(origin + b));
            }
        }
    }
    let line = format!(
        "benilla-mw2: the first-person body stands at {lo:.2}..{hi:.2} (rig root {:.2}, eye {eye:.2}); the palette rows of slot {slots:?} would put it {gap:.2} yd off",
        Vec3::from(root.translation)
    );
    if gap > 0.25 {
        warn!("{line}");
    } else {
        info!("{line}");
    }
}

#[cfg(test)]
#[path = "fp_body_real.rs"]
mod real;

#[cfg(test)]
mod tests {
    use super::*;

    const ON: ([u16; 4], [f32; 4]) = ([1, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]);

    #[test]
    fn the_head_and_the_arms_come_away_the_chest_and_shoulder_cap_stay() {
        // Bone 1 the chest, 2 the head, 3 an arm.
        let head = |b: usize| b == 2;
        let arm = |b: usize| b == 3;
        let neck = ([1, 2, 0, 0], [0.9, 0.1, 0.0, 0.0]);
        let cap = ([1, 3, 0, 0], [0.6, 0.4, 0.0, 0.0]);
        let upper_arm = ([1, 3, 0, 0], [0.3, 0.7, 0.0, 0.0]);
        assert!(keeps_body_triangle([ON, ON, ON], &head, &arm));
        assert!(!keeps_body_triangle([ON, ON, neck], &head, &arm));
        assert!(keeps_body_triangle([ON, cap, cap], &head, &arm));
        assert!(!keeps_body_triangle([ON, cap, upper_arm], &head, &arm));
    }

    #[test]
    fn the_openings_are_where_kept_meets_dropped_across_parts() {
        let p = |x: f32| [x, 0.0, 0.0];
        let positions = [p(0.0), p(1.0), p(2.0), p(3.0), p(4.0), p(5.0)];
        let tris = [[0, 1, 2], [1, 2, 3], [3, 4, 5]];
        let one = |kept: &[bool]| {
            openings(&[PartTris {
                positions: &positions,
                tris: &tris,
                kept,
            }])
        };
        assert_eq!(one(&[true, false, false]), vec![vec![1, 2]]);
        assert_eq!(one(&[true, true, true]), vec![Vec::<u32>::new()]);
        // A seam between two parts: the second part's own copies of vertices 1 and 2 dropped.
        let other = [p(1.0), p(2.0), p(9.0)];
        let both = openings(&[
            PartTris {
                positions: &positions,
                tris: &tris[..1],
                kept: &[true],
            },
            PartTris {
                positions: &other,
                tris: &[[0, 1, 2]],
                kept: &[false],
            },
        ]);
        assert_eq!(both, vec![vec![1, 2], vec![]]);
    }

    #[test]
    fn a_cut_ring_is_drawn_shut() {
        // Two parts: the neck's ring split across them, and a shoulder's ring in the first.
        let mut pts = vec![
            vec![
                Vec3::new(0.1, 1.5, 0.0),
                Vec3::new(-0.1, 1.5, 0.0),
                Vec3::new(0.3, 1.4, 0.0),
                Vec3::new(0.3, 1.2, 0.0),
                Vec3::new(0.0, 1.0, 0.0),
            ],
            vec![Vec3::new(0.0, 1.5, 0.1), Vec3::new(0.0, 1.5, -0.1)],
        ];
        let rings: [&[u8]; 2] = [
            &[RING_NECK, RING_NECK, RING_ARM_POS_X, RING_ARM_POS_X, 0],
            &[RING_NECK, RING_NECK],
        ];
        pinch_rings(&mut pts, &rings);
        let neck = Vec3::new(0.0, 1.5, 0.0);
        for p in [pts[0][0], pts[0][1], pts[1][0], pts[1][1]] {
            assert!(p.distance(neck) < 1e-6, "{p}");
        }
        assert!(pts[0][2].distance(Vec3::new(0.3, 1.3, 0.0)) < 1e-6);
        assert_eq!(pts[0][2], pts[0][3]);
        // Off every ring: where it was.
        assert_eq!(pts[0][4], Vec3::new(0.0, 1.0, 0.0));
    }

    #[test]
    fn the_neck_is_seated_under_a_face() {
        let eye = Vec3::new(0.0, 1.9, 0.0);
        let fwd = Vec3::NEG_Z;
        // A human's neck about under the eye goes a little back, behind the face; an orc's
        // well ahead comes back further; none moves past the bound.
        assert!(
            (seat_offset(Some(Vec3::new(0.0, 1.7, -0.02)), eye, fwd, NECK_AHEAD) + 0.02).abs()
                < 1e-5
        );
        assert!(
            (seat_offset(Some(Vec3::new(0.0, 1.7, -0.4)), eye, fwd, NECK_AHEAD) + 0.4).abs() < 1e-5
        );
        assert_eq!(
            seat_offset(Some(Vec3::new(0.0, 1.7, -3.0)), eye, fwd, NECK_AHEAD),
            -MAX_SEAT
        );
        assert_eq!(seat_offset(None, eye, fwd, NECK_AHEAD), 0.0);
        // A human's neck is where it should be under the eye: no squash. A tauren's, over its
        // camera's pivot, comes down; nothing is stretched.
        assert_eq!(seat_squash(Some(1.5), 1.9), 1.0);
        assert!((seat_squash(Some(1.73), 1.9) - 1.6 / 1.73).abs() < 1e-5);
        assert!((seat_squash(Some(2.0), 1.83) - 0.765).abs() < 1e-5);
        assert_eq!(seat_squash(Some(1.0), 1.9), 1.0);
        assert_eq!(seat_squash(None, 1.9), 1.0);
    }

    #[test]
    fn nothing_touches_the_eye() {
        let eye = Vec3::ZERO;
        assert!(too_near([Vec3::new(0.1, 0.0, 0.0), Vec3::X, Vec3::Y], eye));
        assert!(!too_near([Vec3::X, Vec3::Y, Vec3::Z], eye));
        // A wide panel passing a hand's breadth from the eye, every corner a yard off: too near.
        let panel = [
            Vec3::new(-1.0, -1.0, -0.15),
            Vec3::new(1.0, -1.0, -0.15),
            Vec3::new(0.0, 1.0, -0.15),
        ];
        assert!(too_near(panel, eye));
        assert!(!too_near(panel.map(|p| p - Vec3::Z), eye));
    }

    #[test]
    fn the_copy_keeps_under_a_level_look() {
        let tan = (CAM_FOVY * 0.5).tan();
        let fwd = Vec3::NEG_Z;
        // The chest under the eye: out of the view ahead.
        assert!(under_level_view(Vec3::new(0.0, -0.5, -0.1), fwd, tan));
        // A collar at the eye's height behind it: over the floor.
        assert!(!under_level_view(Vec3::new(0.0, -0.05, 0.2), fwd, tan));
        // A shoulder a yard ahead and only a little under the eye: in the view.
        assert!(!under_level_view(Vec3::new(0.0, -0.3, -1.0), fwd, tan));
        // Far ahead and low (the feet of a long stride): under the view's lower edge.
        assert!(under_level_view(Vec3::new(0.0, -1.8, -1.0), fwd, tan));
    }

    #[test]
    fn the_body_is_drawn_past_the_gun_and_looks_the_same() {
        let h = 1.8;
        let chest = Vec3::new(0.05, -0.5, -0.1);
        let p = push_from_eye(chest, h);
        // Out along its own ray, past the gun's reach.
        assert!(p.length() > GUN_REACH);
        assert!(p.normalize().distance(chest.normalize()) < 1e-5);
        // Order kept: the nearer stays nearer.
        let belly = Vec3::new(0.1, -0.9, -0.1);
        assert!(push_from_eye(belly, h).length() > p.length());
        // The feet, as far as the ground, are where they are.
        let feet = Vec3::new(0.0, -1.75, -0.1);
        assert_eq!(push_from_eye(feet, h), feet);
        // Continuous where the push ends.
        let edge = Vec3::new(0.0, -PUSH_FAR + 1e-4, 0.0);
        assert!((push_from_eye(edge, h).length() - edge.length()).abs() < 1e-3);
        // A gnome's eye is about a yard up: its body stays over the ground.
        let knee = Vec3::new(0.0, -0.6, -0.1);
        assert!(push_from_eye(knee, 1.0).length() < 1.0);
    }

    #[test]
    fn a_copy_off_its_body_is_not_drawn() {
        let root = Vec3::new(-8913.0, 51.0, 571.0);
        let eye = root + Vec3::Y * 1.9;
        let body = (
            root + Vec3::new(-0.4, 0.0, -0.3),
            root + Vec3::new(0.4, 1.7, 0.3),
        );
        assert!(placed_sanely(body.0, body.1, root, eye));
        // A rider: the feet hang under the saddle.
        let seat = root + Vec3::Y;
        let rider = (
            seat + Vec3::new(-0.4, -0.8, -0.3),
            seat + Vec3::new(0.4, 0.7, 0.3),
        );
        assert!(placed_sanely(rider.0, rider.1, seat, seat + Vec3::Y * 1.0));
        // Enormous round the camera; up through the eye; yards away.
        assert!(!placed_sanely(
            root - Vec3::splat(3.0),
            root + Vec3::splat(3.0),
            root,
            eye
        ));
        assert!(!placed_sanely(body.0, eye + Vec3::Y * 0.2, root, eye));
        assert!(!placed_sanely(
            body.0 + Vec3::X * 5.0,
            body.1 + Vec3::X * 5.0,
            root,
            eye
        ));
    }

    #[test]
    fn the_body_shows_standing_or_riding_in_first_person_alive() {
        let v = BodyView {
            active: true,
            stance: Stance::Stand,
            ..BodyView::default()
        };
        assert!(body_shown(v));
        assert!(!body_shown(BodyView { active: false, ..v }));
        assert!(!body_shown(BodyView {
            third_person: true,
            ..v
        }));
        assert!(!body_shown(BodyView { killcam: true, ..v }));
        assert!(!body_shown(BodyView { dead: true, ..v }));
        assert!(!body_shown(BodyView { downed: true, ..v }));
        // A form (crate::forms) never shows the humanoid body.
        assert!(!body_shown(BodyView { in_form: true, ..v }));
        assert!(!body_shown(BodyView {
            in_form: true,
            mounted: true,
            ..v
        }));
        // Crouched and prone the body takes the stance's pose and is seated from it: drawn.
        assert!(body_shown(BodyView {
            stance: Stance::Crouch,
            ..v
        }));
        assert!(body_shown(BodyView {
            stance: Stance::Prone,
            ..v
        }));
        // Riding: drawn, and the mount with it.
        assert!(body_shown(BodyView { mounted: true, ..v }));
    }

    #[test]
    fn a_riders_eye_rides_its_seat_this_frame() {
        let yaw = Quat::from_rotation_y(0.9);
        let prev = Affine3A::from_rotation_translation(yaw, Vec3::new(100.0, 20.0, -50.0));
        // The seat on the mount's back: 1.4 yd up, a little ahead, along the unit's facing.
        let seat_local = Vec3::new(0.0, 1.4, -0.2);
        let seat = prev * Affine3A::from_translation(seat_local);
        // The face point, seated: up the rider and a hand forward of its hips.
        let face = Vec3::new(0.0, 0.85, -0.12);
        let eye = rider_eye(prev, prev, seat, face);
        let want = prev.transform_point3(seat_local + face + Vec3::Y * EYE_OVER_FACE);
        assert!(eye.distance(want) < 1e-4, "{eye} vs {want}");
        // Over the mount's back, well above benilla's standing pivot on foot.
        assert!(eye.y - 20.0 > 2.2);
        // The mount moves on (and turns) this frame: the eye goes with it, not a frame behind.
        let now = Affine3A::from_rotation_translation(
            Quat::from_rotation_y(1.0),
            Vec3::new(100.4, 20.1, -50.3),
        );
        let moved = rider_eye(now, prev, seat, face);
        let want = now.transform_point3(seat_local + face + Vec3::Y * EYE_OVER_FACE);
        assert!(moved.distance(want) < 1e-4, "{moved} vs {want}");
        // A scaled unit (a big companion mount) scales the seat with it.
        let big = prev * Affine3A::from_scale(Vec3::splat(2.0));
        let seat_big = big * Affine3A::from_translation(seat_local);
        let e = rider_eye(big, big, seat_big, face);
        assert!((e.y - 20.0 - 2.0 * (1.4 + 0.85 + EYE_OVER_FACE)).abs() < 1e-3);
    }

    #[test]
    fn the_rider_eye_rises_and_draws_back_over_a_flyer() {
        let look = Vec3::new(0.0, -0.5, -1.0);
        let ground = mounted_eye_lift(2410, look);
        let gryphon = mounted_eye_lift(40020, look);
        let drake = mounted_eye_lift(40037, look);
        assert_eq!(mounted_eye_lift(0, look), Vec3::ZERO, "on foot, nothing");
        assert!(gryphon.y > ground.y && drake.y > gryphon.y);
        // Back along the level look, whatever the pitch.
        assert!(gryphon.z > 0.3 && gryphon.x.abs() < 1e-6);
    }

    #[test]
    fn the_mount_stays_drawn_riding_in_first_person() {
        let v = BodyView {
            active: true,
            mounted: true,
            ..BodyView::default()
        };
        assert!(rides_in_first_person(v));
        assert!(!rides_in_first_person(BodyView {
            mounted: false,
            ..v
        }));
        assert!(!rides_in_first_person(BodyView {
            third_person: true,
            ..v
        }));
        assert!(!rides_in_first_person(BodyView { killcam: true, ..v }));
        assert!(!rides_in_first_person(BodyView { dead: true, ..v }));
        assert!(!rides_in_first_person(BodyView { active: false, ..v }));
    }

    #[test]
    fn the_body_is_on_unless_switched_off() {
        assert!(FpBodySwitch::from_env(None).on);
        assert!(FpBodySwitch::from_env(Some("junk")).on);
        assert!(!FpBodySwitch::from_env(Some("0")).on);
        assert!(!FpBodySwitch::from_env(Some(" off ")).on);
        assert!(FpBodySwitch::from_env(Some("1")).on);
        assert_eq!(parse_switch("No"), Some(false));
        assert_eq!(parse_switch("yes"), Some(true));
    }

    #[test]
    fn the_eye_clip_plane_takes_the_collar_and_never_shows_its_cut() {
        let fwd = Vec3::NEG_Z;
        let n = clip_normal(fwd, CLIP_LEAN);
        let tri = |d: Vec3| [d, d + Vec3::X * 0.02, d + Vec3::Y * 0.02];
        // Behind the eye a hand's breadth under it (a neck, a collar seated back): gone.
        assert!(behind_eye_clip(tri(Vec3::new(0.0, -0.12, 0.2)), n));
        assert!(behind_eye_clip(tri(Vec3::new(0.15, -0.2, 0.15)), n));
        // Over the eye (a shrug, a cloak's collar): gone.
        assert!(behind_eye_clip(tri(Vec3::new(0.0, 0.05, 0.05)), n));
        // Straight down, ahead and below: kept (the belly, the legs, the feet).
        assert!(!behind_eye_clip(tri(Vec3::new(0.0, -0.8, -0.02)), n));
        assert!(!behind_eye_clip(tri(Vec3::new(0.1, -1.7, -0.3)), n));
        assert!(!behind_eye_clip(tri(Vec3::new(0.0, -0.4, -0.3)), n));
        // A triangle across the plane stays: the cut's edge is behind it.
        assert!(!behind_eye_clip(
            [
                Vec3::new(0.0, -0.3, 0.3),
                Vec3::new(0.0, -0.3, -0.1),
                Vec3::new(0.1, -0.3, 0.3)
            ],
            n
        ));
        // Nothing behind the plane is in any view: the stock 45 degree view at every pitch down to
        // the camera's limit (89 degrees), its lower edge (the one reaching back) and corners.
        let tan_v = (CAM_FOVY * 0.5).tan();
        let tan_h = tan_v * 16.0 / 9.0;
        for deg in (0..=89).step_by(1) {
            let look = Quat::from_rotation_x(-(deg as f32).to_radians());
            for (x, y) in [
                (0.0, -1.0),
                (1.0, -1.0),
                (-1.0, -1.0),
                (0.0, 0.0),
                (1.0, 1.0),
            ] {
                let ray = (look * Vec3::new(x * tan_h, y * tan_v, -1.0)).normalize();
                assert!(
                    ray.dot(n) >= -1e-4,
                    "pitch -{deg}: ray {ray} behind the plane"
                );
            }
        }
        // A triangle across the plane is folded onto it: its corner behind lies on the plane, the
        // others stay; a collapsed (taken away) triangle moves nothing.
        let eye = Vec3::new(5.0, 1.9, 3.0);
        let mut pts = vec![
            eye + Vec3::new(0.0, -0.19, 0.2),
            eye + Vec3::new(0.2, -0.36, -0.08),
            eye + Vec3::new(-0.2, -0.36, -0.08),
            eye + Vec3::new(0.0, -0.1, 0.3),
        ];
        let before = pts.clone();
        fold_onto_clip(&mut pts, &[0, 1, 2, 3, 3, 3], eye, n);
        assert!((pts[0] - eye).dot(n).abs() < 1e-5);
        assert_eq!(pts[1], before[1]);
        assert_eq!(pts[2], before[2]);
        assert_eq!(pts[3], before[3]);
    }

    #[test]
    fn first_person_draws_with_the_layers_near_plane() {
        assert!(first_person_near(true, false, false, true));
        // A gun up keeps the stock plane: its sights are made for it.
        assert!(!first_person_near(true, false, false, false));
        assert!(!first_person_near(false, false, false, true));
        assert!(!first_person_near(true, true, false, true));
        assert!(!first_person_near(true, false, true, true));
        // Within the stock CVar's own range.
        assert!(benilla_world::view::NEARCLIP_RANGE.contains(&FP_NEARCLIP));
    }

    #[test]
    fn a_subtree_is_its_root_and_everything_under() {
        // 0 root, 1 spine, 2 neck, 3 head, 4 jaw, 5 arm.
        let parents = [-1, 0, 1, 2, 3, 1];
        let under = subtree(&parents, 3);
        assert_eq!(under, vec![false, false, false, true, true, false]);
    }

    #[test]
    fn the_head_is_the_helm_points_parent() {
        // 0 root, 1 spine, 2 neck, 3 head, 4 helm point.
        let parents = [-1, 0, 1, 2, 3];
        assert_eq!(head_of(&parents, 4), 3);
        assert_eq!(head_of(&parents, 0), 0);
    }

    #[test]
    fn hair_is_never_copied() {
        assert!(copied_slot(Some(CharSkinSlot::Body)));
        assert!(copied_slot(Some(CharSkinSlot::Object)));
        assert!(!copied_slot(Some(CharSkinSlot::Hair)));
        assert!(!copied_slot(None));
    }
}
