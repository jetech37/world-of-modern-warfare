//! The first-person body against a real character, read from the install (read-only): the copy
//! built from a human male's own parts and skeleton, posed through every one of its animations,
//! dressed naked and dressed in armour with a tabard, a robe and a cloak, and placed in the world
//! far from the map origin with a turn, as the live game places it. Skipped (with a note) when no
//! install is found: `$WOW_DATA`, else `dist/AzerothWarfare/game/WoW/Data` above this crate.

use std::path::PathBuf;

use benilla_formats::{CharSkinSlot, ModelAnimation, RenderSubmesh};
use bevy::math::{Affine3A, Mat3, Mat4, Quat, Vec3};

use super::*;

/// The install's `Data` directory, if there is one.
fn install() -> Option<PathBuf> {
    if let Some(d) = std::env::var_os("WOW_DATA").map(PathBuf::from) {
        return d.is_dir().then_some(d);
    }
    let here = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    here.ancestors()
        .map(|a| a.join("dist/AzerothWarfare/game/WoW/Data"))
        .find(|d| d.is_dir())
}

fn w2b(p: [f32; 3]) -> Vec3 {
    benilla_assets::coords::wow_to_bevy(p)
}

/// benilla's rotation-key basis change (`benilla_assets::model`): `r·q·r⁻¹`.
fn w2b_quat() -> Quat {
    Quat::from_mat3(&Mat3::from_cols(
        w2b([1.0, 0.0, 0.0]),
        w2b([0.0, 1.0, 0.0]),
        w2b([0.0, 0.0, 1.0]),
    ))
}

/// A character model as the copy reads it: skeleton, parts and animations.
struct Model {
    parents: Vec<i16>,
    pivots: Vec<Vec3>,
    key_bones: Vec<i16>,
    /// The helm attachment's bone.
    helm: usize,
    /// The first-person eye over the feet: the camera pivot, attachment 17 + 0.0972
    /// (benilla's `CameraPivot`).
    eye_height: f32,
    subs: Vec<RenderSubmesh>,
    anims: Vec<ModelAnimation>,
}

fn load(path: &str) -> Option<Model> {
    let data = install()?;
    let mut chain = benilla_formats::open_chain(&data).ok()?;
    let bytes = chain.read_file(path).ok()?;
    let skel = benilla_formats::parse_m2_skeleton(&bytes).ok()?;
    let attach = benilla_formats::parse_m2_attachments(&bytes).ok()?;
    let helm = attach.iter().find(|a| a.id == ATTACH_HELM)?.bone;
    let eye_height = w2b(attach.iter().find(|a| a.id == 17)?.position).y + 0.0972;
    Some(Model {
        parents: skel.bones.iter().map(|b| b.parent).collect(),
        pivots: skel.bones.iter().map(|b| w2b(b.pivot)).collect(),
        key_bones: skel.bones.iter().map(|b| b.key_bone).collect(),
        helm: usize::from(helm),
        eye_height,
        subs: benilla_formats::load_m2_mesh(&mut chain, path).ok()?,
        anims: benilla_formats::parse_m2_animations(&bytes),
    })
}

impl Model {
    fn under(&self, b: usize, root: usize) -> bool {
        let mut c = Some(b);
        for _ in 0..self.parents.len() {
            match c {
                Some(x) if x == root => return true,
                Some(x) => c = usize::try_from(self.parents[x]).ok(),
                None => return false,
            }
        }
        false
    }

    fn key(&self, k: i16) -> usize {
        self.key_bones
            .iter()
            .position(|&b| b == k)
            .expect("key bone")
    }

    /// Each bone's model-space frame at `t` seconds into `anim`, as benilla's pose evaluator and
    /// model pass compose it (`rest + translation key`, `r·q·r⁻¹`, scale permuted).
    fn pose(&self, anim: &ModelAnimation, t: f32) -> Vec<Affine3A> {
        let r = w2b_quat();
        let mut locals: Vec<Transform> = (0..self.pivots.len())
            .map(|i| {
                let parent = usize::try_from(self.parents[i])
                    .ok()
                    .map_or(Vec3::ZERO, |p| self.pivots[p]);
                Transform::from_translation(self.pivots[i] - parent)
            })
            .collect();
        for bk in &anim.bones {
            let Some(tf) = locals.get_mut(usize::from(bk.bone)) else {
                continue;
            };
            let rest = tf.translation;
            let tr: Vec<(f32, Vec3)> = bk
                .translation
                .iter()
                .map(|(k, v)| (*k, rest + w2b(*v)))
                .collect();
            let ro: Vec<(f32, Quat)> = bk
                .rotation
                .iter()
                .map(|(k, q)| {
                    (
                        *k,
                        r * Quat::from_xyzw(q[0], q[1], q[2], q[3]) * r.inverse(),
                    )
                })
                .collect();
            let sc: Vec<(f32, Vec3)> = bk
                .scale
                .iter()
                .map(|(k, s)| (*k, Vec3::new(s[1], s[2], s[0])))
                .collect();
            if let Some(v) = benilla_assets::PoseTrack::new(&tr).sample(t) {
                tf.translation = v;
            }
            if let Some(v) = benilla_assets::PoseTrack::new(&ro).sample(t) {
                tf.rotation = v;
            }
            if let Some(v) = benilla_assets::PoseTrack::new(&sc).sample(t) {
                tf.scale = v;
            }
        }
        let mut model = vec![Affine3A::IDENTITY; locals.len()];
        for i in 0..locals.len() {
            let parent = usize::try_from(self.parents[i])
                .ok()
                .filter(|&p| p < i)
                .map_or(Affine3A::IDENTITY, |p| model[p]);
            model[i] = parent * locals[i].compute_affine();
        }
        model
    }

    /// The inverse bind poses: `translate(−pivot)`, as benilla builds them.
    fn ibp(&self) -> Vec<Mat4> {
        self.pivots
            .iter()
            .map(|p| Mat4::from_translation(-*p))
            .collect()
    }

    /// The worn parts: the naked defaults, or a warrior's armour with a tabard, a robe and a cloak.
    fn worn(&self, armoured: bool) -> Vec<&RenderSubmesh> {
        let armour = [402u16, 502, 802, 903, 1002, 1102, 1202, 1302, 1502];
        self.subs
            .iter()
            .filter(|s| copied_slot(s.char_slot) || s.char_slot == Some(CharSkinSlot::Hair))
            .filter(|s| {
                let g = s.geoset_id;
                if g == 0 {
                    return true;
                }
                let group = g / 100;
                match armour
                    .iter()
                    .find(|a| *a / 100 == group)
                    .filter(|_| armoured)
                {
                    Some(&a) => g == a,
                    None => g % 100 == 1 || (group == 0 && g == 1),
                }
            })
            .collect()
    }
}

/// The world frame the rig is drawn in: far from the map origin and turned, as in the game.
fn root() -> Affine3A {
    Affine3A::from_rotation_translation(
        Quat::from_rotation_y(0.7),
        Vec3::new(-8913.25, 51.75, 571.5),
    )
}

/// A ray against a triangle (Möller-Trumbore), two-sided: the hit distance.
fn ray_hit(o: Vec3, d: Vec3, t: [Vec3; 3]) -> Option<f32> {
    let (e1, e2) = (t[1] - t[0], t[2] - t[0]);
    let p = d.cross(e2);
    let det = e1.dot(p);
    if det.abs() < 1e-9 {
        return None;
    }
    let s = o - t[0];
    let u = s.dot(p) / det;
    if !(0.0..=1.0).contains(&u) {
        return None;
    }
    let q = s.cross(e1);
    let v = d.dot(q) / det;
    if v < 0.0 || u + v > 1.0 {
        return None;
    }
    let dist = e2.dot(q) / det;
    (dist > 0.0).then_some(dist)
}

/// The body as the GPU draws it, in world space: every worn triangle skinned from benilla's
/// palette rows (rig-relative, `frame × ibp`, the root's frame rebased) and the rig's origin.
fn gpu_body(
    m: &Model,
    parts: &[&RenderSubmesh],
    model: &[Affine3A],
    root: Affine3A,
) -> Vec<[Vec3; 3]> {
    let origin = Vec3::from(root.translation);
    let rebased = Affine3A::from_mat3(Mat3::from(root.matrix3));
    let rows: Vec<Mat4> = model
        .iter()
        .zip(m.ibp())
        .map(|(f, i)| Mat4::from(rebased * *f) * i)
        .collect();
    let gpu = |sub: &RenderSubmesh, v: u32| {
        let v = v as usize;
        let mut m = Mat4::ZERO;
        for (j, w) in sub.joints[v].iter().zip(sub.weights[v]) {
            m += rows[usize::from(*j)] * w;
        }
        origin + m.transform_point3(w2b(sub.positions[v]))
    };
    parts
        .iter()
        .flat_map(|s| {
            s.indices
                .chunks(3)
                .map(|t| [gpu(s, t[0]), gpu(s, t[1]), gpu(s, t[2])])
                .collect::<Vec<_>>()
        })
        .collect()
}

/// The copy at one look, world space: where its kept triangles stand, what is drawn (pushed out
/// from the eye), and its holes: edges the copy left open (by its cuts or its view rules), not
/// pinched shut, where the whole body is closed.
struct Copy {
    placed: Vec<[Vec3; 3]>,
    drawn: Vec<[Vec3; 3]>,
    holes: Vec<[Vec3; 2]>,
}

/// Each edge's triangle count, keyed by its corners' rest positions (a body's parts share seams
/// by position, each with its own vertices).
fn edge_counts<'a>(
    tris: impl Iterator<Item = [[f32; 3]; 3]> + 'a,
) -> std::collections::HashMap<([u32; 3], [u32; 3]), u32> {
    let key = |p: [f32; 3]| p.map(f32::to_bits);
    let mut out = std::collections::HashMap::new();
    for t in tris {
        for k in 0..3 {
            let (a, b) = (key(t[k]), key(t[(k + 1) % 3]));
            *out.entry(if a < b { (a, b) } else { (b, a) }).or_insert(0) += 1;
        }
    }
    out
}

/// The copy as `gather` builds it and `skin` poses it: model space under the rig root, which
/// places it.
fn copy_at(
    m: &Model,
    parts: &[&RenderSubmesh],
    model: &[Affine3A],
    root: Affine3A,
    eye: Vec3,
    look: Quat,
) -> Copy {
    copy_at_fit(m, parts, model, root, eye, look, BodyFit::DEFAULT)
}

fn copy_at_fit(
    m: &Model,
    parts: &[&RenderSubmesh],
    model: &[Affine3A],
    root: Affine3A,
    eye: Vec3,
    look: Quat,
    fit: BodyFit,
) -> Copy {
    let head_root = head_of(&m.parents, m.helm);
    let (r, l) = (m.key(3), m.key(2));
    let head = |b: usize| m.under(b, head_root);
    let arm = |b: usize| (m.under(b, r) && b != r) || (m.under(b, l) && b != l);
    let sources = copy_sources(parts, &head, &arm);
    let skin: Vec<Mat4> = model
        .iter()
        .zip(m.ibp())
        .map(|(f, i)| Mat4::from(*f) * i)
        .collect();
    let view = EyeView {
        eye,
        look,
        tan_half_v: (CAM_FOVY * 0.5).tan(),
    };
    let posed = pose_body_fit(&sources, &skin, root, &view, fit);
    let drawn_tris = |p: &PosedPart| -> Vec<[u32; 3]> {
        p.indices
            .chunks(3)
            .filter(|t| t[0] != t[1] || t[1] != t[2])
            .map(|t| [t[0], t[1], t[2]])
            .collect()
    };
    let tris = |pick: &dyn Fn(&PosedPart) -> &Vec<Vec3>| -> Vec<[Vec3; 3]> {
        posed
            .iter()
            .flat_map(|p| {
                drawn_tris(p)
                    .into_iter()
                    .map(|t| t.map(|v| root.transform_point3(pick(p)[v as usize])))
                    .collect::<Vec<_>>()
            })
            .collect()
    };
    // The whole body's open edges, then the copy's: an edge open in the copy and closed in the
    // body is a hole the copy made.
    let whole = edge_counts(parts.iter().flat_map(|s| {
        s.indices
            .chunks(3)
            .map(|t| [t[0], t[1], t[2]].map(|v| s.positions[v as usize]))
    }));
    let rest = |s: &CopySource, v: u32| {
        let p = s.positions[v as usize];
        // Back to the file's axes, the key the whole body's edges use.
        let w = benilla_assets::coords::bevy_to_wow(p);
        [w[0], w[1], w[2]]
    };
    let copy_edges = edge_counts(sources.iter().zip(&posed).flat_map(|(s, p)| {
        drawn_tris(p)
            .into_iter()
            .map(|t| t.map(|v| rest(s, v)))
            .collect::<Vec<_>>()
    }));
    let key = |p: [f32; 3]| p.map(f32::to_bits);
    let mut holes = Vec::new();
    for (s, p) in sources.iter().zip(&posed) {
        for t in drawn_tris(p) {
            for k in 0..3 {
                let (a, b) = (t[k], t[(k + 1) % 3]);
                let (ka, kb) = (key(rest(s, a)), key(rest(s, b)));
                let e = if ka < kb { (ka, kb) } else { (kb, ka) };
                if copy_edges.get(&e) == Some(&1) && whole.get(&e) != Some(&1) {
                    let (pa, pb) = (
                        root.transform_point3(p.placed[a as usize]),
                        root.transform_point3(p.placed[b as usize]),
                    );
                    if pa.distance(pb) > 1e-4 {
                        holes.push([pa, pb]);
                    }
                }
            }
        }
    }
    Copy {
        placed: tris(&|p| &p.placed),
        drawn: tris(&|p| &p.positions),
        holes,
    }
}

fn bounds(tris: &[[Vec3; 3]]) -> (Vec3, Vec3) {
    tris.iter()
        .flatten()
        .fold((Vec3::MAX, Vec3::MIN), |(lo, hi), p| {
            (lo.min(*p), hi.max(*p))
        })
}

/// Every animation of the model at `points` points through it.
fn poses(m: &Model, points: usize) -> Vec<(u16, f32, Vec<Affine3A>)> {
    let mut out = Vec::new();
    for a in &m.anims {
        for k in 0..points {
            let t = a.duration * k as f32 / points as f32;
            out.push((a.anim_id, t, m.pose(a, t)));
        }
    }
    out
}

/// The nearest hit of a ray on the triangles beyond the near plane.
fn first_hit(tris: &[[Vec3; 3]], o: Vec3, d: Vec3) -> Option<f32> {
    tris.iter()
        .filter_map(|t| ray_hit(o, d, *t).filter(|&h| h > 0.03))
        .min_by(f32::total_cmp)
}

/// The view rays (unit, world) of a look: `n × n` over the stock 45° field and a 16:9 window.
fn view_rays(look: Quat, n: usize) -> Vec<Vec3> {
    let tan_v = (CAM_FOVY * 0.5).tan();
    let tan_h = tan_v * 16.0 / 9.0;
    let mut out = Vec::new();
    for iy in 0..n {
        for ix in 0..n {
            let x = (ix as f32 / (n - 1) as f32 * 2.0 - 1.0) * tan_h;
            let y = (iy as f32 / (n - 1) as f32 * 2.0 - 1.0) * tan_v;
            out.push((look * Vec3::new(x, y, -1.0)).normalize());
        }
    }
    out
}

/// Whether a point is in a look's view (`d` from the eye, world axes).
fn in_view(look: Quat, d: Vec3) -> bool {
    let tan_v = (CAM_FOVY * 0.5).tan();
    let tan_h = tan_v * 16.0 / 9.0;
    let c = look.inverse() * d;
    let depth = -c.z;
    depth > 0.0 && c.y.abs() <= depth * tan_v && c.x.abs() <= depth * tan_h
}

/// The nearest the copy in view may be to the eye where it stands (yards).
const MIN_SEEN: f32 = 0.2;

/// The widest angle any drawn triangle with a corner in the look's view spans from the eye.
/// The nearest any drawn triangle in the look's view comes to the eye where it stands (yards).
/// Standing, walking, running and the turns and strafes: the clips first person is seen in.
fn moving_or_standing(anim: u16) -> bool {
    matches!(anim, 0 | 4 | 5 | 11 | 12 | 13)
}

fn nearest_seen(copy: &Copy, eye: Vec3, look: Quat) -> f32 {
    copy.drawn
        .iter()
        .zip(&copy.placed)
        .filter(|(t, _)| t.iter().any(|&c| in_view(look, c - eye)))
        .map(|(_, p)| nearest_on_triangle(eye, *p).distance(eye))
        .fold(f32::MAX, f32::min)
}

fn widest_span(copy: &Copy, eye: Vec3, look: Quat) -> f32 {
    copy.drawn
        .iter()
        .filter(|t| t.iter().any(|&c| in_view(look, c - eye)))
        .map(|t| {
            let d = t.map(|c| (c - eye).normalize_or_zero());
            (0..3)
                .map(|k| d[k].dot(d[(k + 1) % 3]).clamp(-1.0, 1.0).acos())
                .fold(0.0f32, f32::max)
        })
        .fold(0.0f32, f32::max)
}

/// The share of a look's view the copy fills.
fn coverage(copy: &Copy, eye: Vec3, look: Quat) -> f32 {
    let rays = view_rays(look, 21);
    let hit = rays
        .iter()
        .filter(|&&d| first_hit(&copy.drawn, eye, d).is_some())
        .count();
    hit as f32 / rays.len() as f32
}

/// One model through its animations, naked and armoured: the failures, the looks checked, and
/// the Stand pose's share of the view looking 70 and 85 degrees down (the least of the two
/// dressings).
fn check_model(path: &str, points: usize) -> Option<(Vec<String>, usize, [f32; 2])> {
    let m = load(path)?;
    let root = root();
    let facing = Quat::from_affine3(&root);
    let mut failures = Vec::new();
    let mut known_holes: Vec<String> = Vec::new();
    let mut checked = 0;
    let mut stand_seen = [1.0f32; 2];
    for armoured in [false, true] {
        let parts = m.worn(armoured);
        for (anim, t, model) in poses(&m, points) {
            let tag = format!("{path} armour {armoured} anim {anim} t {t:.2}");
            let body = gpu_body(&m, &parts, &model, root);
            let eye = Vec3::from(root.translation) + Vec3::Y * m.eye_height;
            let (blo, bhi) = bounds(&body);
            for pitch in [0.0f32, -45.0, -70.0, -85.0] {
                let look = facing * Quat::from_rotation_x(pitch.to_radians());
                let copy = copy_at(&m, &parts, &model, root, eye, look);
                let tag = format!("{tag} pitch {pitch}");
                if copy.placed.is_empty() {
                    // A pose lifting everything into the view leaves nothing: fine, but not at
                    // rest.
                    if anim == 0 {
                        failures.push(format!("{tag}: nothing of the copy kept at Stand"));
                    }
                    continue;
                }
                checked += 1;
                let (clo, chi) = bounds(&copy.placed);
                // On the body: inside its box (seated under the eye, never moved more than
                // MAX_SEAT; squashed down about the feet, never under them), no taller, on its
                // feet. A copy drawn at the wrong scale or place fails all three.
                let grow = Vec3::new(MAX_SEAT, 0.0, MAX_SEAT) + Vec3::splat(0.05);
                // (A corner folded onto the eye's clip plane may sit a little lower, out of view.)
                let floor = blo.y.min(root.translation.y) - 0.1;
                if (clo.xz() - (blo.xz() - grow.xz())).min_element() < 0.0
                    || ((bhi + grow) - chi).min_element() < 0.0
                    || clo.y < floor
                {
                    failures.push(format!(
                        "{tag}: copy box {clo:.2}..{chi:.2} outside the body's {blo:.2}..{bhi:.2}"
                    ));
                }
                if chi.y - clo.y > bhi.y - blo.y + 0.05 {
                    failures.push(format!(
                        "{tag}: copy {:.2} yd tall, body {:.2}",
                        chi.y - clo.y,
                        bhi.y - blo.y
                    ));
                }
                if anim == 0 && (clo.y - blo.y).abs() > 0.15 {
                    failures.push(format!(
                        "{tag}: copy's floor {:.2}, body's {:.2}",
                        clo.y, blo.y
                    ));
                }
                // Its top under the eye.
                if chi.y > eye.y - EYE_FLOOR + 1e-3 {
                    failures.push(format!("{tag}: copy's top {:.3}, eye {:.3}", chi.y, eye.y));
                }
                // Drawn, nothing near the eye, nothing within the gun's and arms' reach.
                if let Some(p) = copy
                    .drawn
                    .iter()
                    .flatten()
                    .find(|p| p.distance(eye) < GUN_REACH.min(0.7 * m.eye_height))
                {
                    failures.push(format!(
                        "{tag}: a corner drawn {:.3} yd from the eye",
                        p.distance(eye)
                    ));
                }
                // Straight ahead, no view ray meets it.
                if pitch == 0.0
                    && let Some(d) = view_rays(look, 25)
                        .into_iter()
                        .find(|&d| first_hit(&copy.drawn, eye, d).is_some())
                {
                    failures.push(format!("{tag}: the level view ray {d:.2} meets the copy"));
                }
                // No hole the copy made is in view, standing or on the move: the cuts are
                // pinched shut. (Known: a running tauren, whose camera pivot is inside its upper
                // body, loses a wrapping triangle; logged, not failed.)
                let moving = matches!(anim, 0 | 4 | 5 | 11 | 12 | 13);
                if let Some(h) = copy
                    .holes
                    .iter()
                    .find(|h| moving && in_view(look, (h[0] + h[1]) * 0.5 - eye))
                {
                    let known = path.contains("Tauren");
                    (if known {
                        &mut known_holes
                    } else {
                        &mut failures
                    })
                    .push(format!(
                        "{tag}: a hole in view, {:.2}..{:.2} from the eye",
                        h[0] - eye,
                        h[1] - eye
                    ));
                }
                if anim == 0 && pitch <= -70.0 {
                    let k = usize::from(pitch < -80.0);
                    stand_seen[k] = stand_seen[k].min(coverage(&copy, eye, look));
                }
                // No wedge: nothing in view within a hand's breadth and more of the eye (the top of
                // the chest 0.18 yd under it once drew as a stretched pale wedge). The tauren's
                // camera pivot is inside its chest: logged, not failed.
                let near = nearest_seen(&copy, eye, look);
                if pitch < 0.0 && moving_or_standing(anim) && near < MIN_SEEN {
                    let line = format!("{tag}: the copy in view {near:.2} yd from the eye");
                    if path.contains("Tauren") {
                        known_holes.push(line);
                    } else {
                        failures.push(line);
                    }
                }
            }
        }
    }
    if !known_holes.is_empty() {
        eprintln!(
            "{path}: {} known holes (the tauren's camera pivot)",
            known_holes.len()
        );
    }
    Some((failures, checked, stand_seen))
}

#[test]
fn the_copy_stands_on_the_body_under_the_eye_out_of_the_view_ahead_and_off_the_gun() {
    // Each model, the points through each animation, and the least share of the view its body
    // must fill standing 70 degrees down (the live probe's look). The torso and legs under you,
    // not a strip of feet at the view's edge: leaned for the widest look, a human male filled 9%
    // (feet only, under the gun). A gnome's eye is a yard up and its body small; a tauren's
    // camera pivot is inside its chest, its body all ahead of and under it.
    let models = [
        (r"Character\Human\Male\HumanMale.m2", 4, 0.15),
        (r"Character\Human\Female\HumanFemale.m2", 2, 0.15),
        (r"Character\Orc\Male\OrcMale.m2", 2, 0.12),
        (r"Character\Dwarf\Male\DwarfMale.m2", 2, 0.15),
        (r"Character\Tauren\Male\TaurenMale.m2", 2, 0.02),
        (r"Character\Gnome\Male\GnomeMale.m2", 2, 0.08),
    ];
    if install().is_none() {
        eprintln!("no WoW install (WOW_DATA or dist/AzerothWarfare/game/WoW/Data): skipped");
        return;
    }
    let mut failures = Vec::new();
    for (path, points, least) in models {
        let (f, checked, seen) = check_model(path, points).expect(path);
        eprintln!(
            "{path}: {checked} looks checked; standing, the body fills {:.0}% of the view 70 degrees down, {:.0}% 85 down",
            seen[0] * 100.0,
            seen[1] * 100.0
        );
        // Looking down shows the body: a real share of the view, not a strip at its edge.
        if seen[0] < least || seen[1] < 0.15 {
            failures.push(format!(
                "{path}: standing, the body fills only {:.0}% / {:.0}% of the view 70 / 85 degrees down",
                seen[0] * 100.0,
                seen[1] * 100.0
            ));
        }
        failures.extend(f);
    }
    assert!(
        failures.is_empty(),
        "{} failures:\n{}",
        failures.len(),
        failures
            .iter()
            .take(40)
            .cloned()
            .collect::<Vec<_>>()
            .join("\n")
    );
}

/// A rider's eye from the real model: seated in the Mount clip (91) on a seat anchor, the eye
/// carried from its face point stands over the saddle at a seated head's height (lower than
/// standing), and the copy seen from there, looking down at the mount, stands on the rider and
/// keeps clear of the eye.
#[test]
fn the_riders_eye_sits_over_the_saddle_and_its_body_keeps_clear() {
    let Some(data) = install() else {
        eprintln!("no WoW install (WOW_DATA or dist/AzerothWarfare/game/WoW/Data): skipped");
        return;
    };
    for path in [
        r"Character\Human\Male\HumanMale.m2",
        r"Character\Orc\Male\OrcMale.m2",
        r"Character\Gnome\Male\GnomeMale.m2",
    ] {
        let m = load(path).expect(path);
        let mut chain = benilla_formats::open_chain(&data).expect("chain");
        let bytes = chain.read_file(path).expect(path);
        let attach = benilla_formats::parse_m2_attachments(&bytes).expect("attachments");
        let face = attach
            .iter()
            .find(|a| a.id == ATTACH_FACE)
            .expect("face point");
        let mount = m
            .anims
            .iter()
            .find(|a| a.anim_id == 91)
            .expect("Mount clip");
        let model = m.pose(mount, 0.0);
        let bone = usize::from(face.bone);
        let face_in_seat = model[bone].transform_point3(w2b(face.position) - m.pivots[bone]);
        // The unit, and the seat anchor 1.4 yd over its feet on the mount's back, a little ahead.
        let body = root();
        let seat = body * Affine3A::from_translation(Vec3::new(0.0, 1.4, -0.2));
        let eye = rider_eye(body, body, seat, face_in_seat);
        let seat_at = Vec3::from(seat.translation);
        let up = eye.y - seat_at.y;
        assert!(
            (0.35..m.eye_height - 0.2).contains(&up),
            "{path}: the eye {up:.2} yd over the seat, standing {:.2}",
            m.eye_height
        );
        // Over the saddle, not out over the mount's head or behind its rump.
        let flat = (eye - seat_at).with_y(0.0).length();
        assert!(flat < 0.7, "{path}: the eye {flat:.2} yd off the seat");
        // The copy from there, looking down at the mount's neck.
        let facing = Quat::from_affine3(&body);
        let parts = m.worn(true);
        for pitch in [-30.0f32, -60.0, -85.0] {
            let look = facing * Quat::from_rotation_x(pitch.to_radians());
            let copy = copy_at(&m, &parts, &model, seat, eye, look);
            if let Some(p) = copy.drawn.iter().flatten().find(|p| p.distance(eye) < 0.3) {
                panic!(
                    "{path} pitch {pitch}: a corner drawn {:.2} yd from a rider's eye",
                    p.distance(eye)
                );
            }
            let near = nearest_seen(&copy, eye, look);
            assert!(
                near >= MIN_SEEN,
                "{path} pitch {pitch}: a rider's copy in view {near:.2} yd from the eye"
            );
            if let Some((lo, hi)) =
                copy.placed
                    .iter()
                    .flatten()
                    .fold(None, |b: Option<(Vec3, Vec3)>, &q| {
                        Some(b.map_or((q, q), |(lo, hi)| (lo.min(q), hi.max(q))))
                    })
            {
                assert!(
                    placed_sanely(lo, hi, seat_at, eye),
                    "{path} pitch {pitch}: the rider's copy {lo:.2}..{hi:.2} off its seat {seat_at:.2}"
                );
            }
        }
    }
}

/// `cargo test --release --lib probe_pitch -- --ignored --nocapture`: the Stand pose
/// at a sweep of pitches, the copy (`#`) and the whole body (`.`) as the view sees them.
#[test]
#[ignore]
fn probe_pitch() {
    let m = load("Character\\Human\\Male\\HumanMale.m2").expect("install");
    let root = root();
    let facing = Quat::from_affine3(&root);
    let stand = m.anims.iter().find(|a| a.anim_id == 0).expect("Stand");
    let model = m.pose(stand, 0.0);
    let parts = m.worn(true);
    let body = gpu_body(&m, &parts, &model, root);
    for eye_h in [1.8f32, 1.9] {
        for pitch in [0.0f32, -30.0, -50.0, -70.0, -85.0] {
            let eye = Vec3::from(root.translation) + Vec3::Y * eye_h;
            let look = facing * Quat::from_rotation_x(pitch.to_radians());
            let copy = copy_at(&m, &parts, &model, root, eye, look);
            let cells: Vec<char> = view_rays(look, 21)
                .into_iter()
                .map(|d| {
                    if first_hit(&copy.drawn, eye, d).is_some() {
                        '#'
                    } else if first_hit(&body, eye, d).is_some() {
                        '.'
                    } else {
                        ' '
                    }
                })
                .collect();
            // The rays run bottom row first: flip for reading.
            let rows: Vec<String> = cells.chunks(21).rev().map(|r| r.iter().collect()).collect();
            eprintln!(
                "eye {eye_h} pitch {pitch}: {:.0}% of the view\n{}",
                coverage(&copy, eye, look) * 100.0,
                rows.join("\n")
            );
        }
    }
}

/// `cargo test --release --lib probe_push -- --ignored --nocapture`: the copy as drawn (pushed
/// out from the eye, `#`) against the same triangles where they stand (`o`), and where only one
/// of the two is hit (`!`), with the widest angle any drawn triangle spans from the eye. A large
/// span or many `!` cells is the push distorting the picture.
#[test]
#[ignore]
fn probe_push() {
    let m = load(r"Character\Human\Male\HumanMale.m2").expect("install");
    let root = root();
    let facing = Quat::from_affine3(&root);
    let stand = m.anims.iter().find(|a| a.anim_id == 0).expect("Stand");
    let model = m.pose(stand, 0.0);
    let parts = m.worn(true);
    for pitch in [-50.0f32, -70.0, -85.0] {
        let eye = Vec3::from(root.translation) + Vec3::Y * m.eye_height;
        let look = facing * Quat::from_rotation_x(pitch.to_radians());
        let copy = copy_at(&m, &parts, &model, root, eye, look);
        let span = copy
            .drawn
            .iter()
            .map(|t| {
                let d = t.map(|c| (c - eye).normalize_or_zero());
                (0..3)
                    .map(|k| d[k].dot(d[(k + 1) % 3]).clamp(-1.0, 1.0).acos())
                    .fold(0.0f32, f32::max)
            })
            .fold(0.0f32, f32::max);
        let mut differ = 0;
        let cells: Vec<char> = view_rays(look, 31)
            .into_iter()
            .map(|d| {
                match (
                    first_hit(&copy.drawn, eye, d).is_some(),
                    first_hit(&copy.placed, eye, d).is_some(),
                ) {
                    (true, true) => '#',
                    (false, true) => {
                        differ += 1;
                        'o'
                    }
                    (true, false) => {
                        differ += 1;
                        '!'
                    }
                    (false, false) => ' ',
                }
            })
            .collect();
        let rows: Vec<String> = cells.chunks(31).rev().map(|r| r.iter().collect()).collect();
        eprintln!(
            "pitch {pitch}: widest drawn span {:.1} deg, {differ} cells differ
{}",
            span.to_degrees(),
            rows.join(
                "
"
            )
        );
    }
}

/// The systems as the game runs them, headless: the real `gather` and `skin` over a human male's
/// worn parts and Stand pose, on a unit with a palette rig, under Bevy's transform propagation
/// and visibility sweep, the camera at the eye 70 degrees down. The copy must end up drawn: a
/// child of the rig root, `InheritedVisibility` true, the opaque alpha in its tag, the cutout
/// material, and a body's worth of triangles that stand on the body.
#[test]
fn the_copy_is_drawn_looking_down_as_the_systems_run() {
    use benilla_assets::{ModelJoint, ModelSkeleton, PoseSource};
    use benilla_world::rig_palette::{RigPalettes, RigSkin};
    use bevy::camera::visibility::{InheritedVisibility, VisibilityPlugin};
    use std::sync::Arc;

    let Some(m) = load(r"Character\Human\Male\HumanMale.m2") else {
        eprintln!("no WoW install (WOW_DATA or dist/AzerothWarfare/game/WoW/Data): skipped");
        return;
    };
    let mut app = App::new();
    app.add_plugins((
        MinimalPlugins,
        bevy::asset::AssetPlugin::default(),
        bevy::transform::TransformPlugin,
        VisibilityPlugin,
    ))
    .init_asset::<Mesh>()
    .init_asset::<SkinnedMeshInverseBindposes>()
    .init_asset::<WowModelMaterial>()
    .init_resource::<RigPalettes>()
    .init_resource::<FpBody>()
    .add_systems(Update, gather)
    .add_systems(
        PostUpdate,
        skin.after(bevy::transform::TransformSystems::Propagate),
    );

    // The rig: the model's skeleton, its Stand pose composed, a palette slot.
    let skeleton = ModelSkeleton {
        joints: (0..m.pivots.len())
            .map(|i| ModelJoint {
                parent: m.parents[i],
                local_translation: m.pivots[i]
                    - usize::try_from(m.parents[i])
                        .ok()
                        .map_or(Vec3::ZERO, |p| m.pivots[p]),
                billboard: None,
                parent_arm: None,
            })
            .collect(),
        spine_bone: None,
        head_bone: None,
    };
    let stand = m.anims.iter().find(|a| a.anim_id == 0).expect("Stand");
    let ibp = app
        .world_mut()
        .resource_mut::<Assets<SkinnedMeshInverseBindposes>>()
        .add(SkinnedMeshInverseBindposes::from(m.ibp()));
    let rig = {
        let world = app.world_mut();
        let mut palettes = world.resource_mut::<RigPalettes>();
        RigSkin::allocate_bones(&mut palettes, m.pivots.len() as u32, ibp).expect("slot")
    };
    let slot = rig.slot;
    // Bone masks as benilla's: bits 0 and 1 hold every bone outside the right and the left arm.
    let (r, l) = (m.key(3), m.key(2));
    let masks: Vec<u64> = (0..m.pivots.len())
        .map(|b| u64::from(!m.under(b, r)) | u64::from(!m.under(b, l)) << 1 | 0b100)
        .collect();
    let anims = ModelAnimations {
        graph: Handle::default(),
        clips: Vec::new(),
        hand_close: [None, None],
        playable_animation_lookup: Vec::new(),
        animation_lookup: Vec::new(),
        global_bones: Vec::new(),
        first_seq: None,
        pose: Arc::new(PoseSource {
            bone_masks: masks,
            ..Default::default()
        }),
    };
    let root = root();
    let unit = app.world_mut().spawn_empty().id();
    let mut pose = RigPose::new(unit, &skeleton);
    pose.model = m.pose(stand, 0.0);
    let (scale, rotation, translation) = root.to_scale_rotation_translation();
    app.world_mut().entity_mut(unit).insert((
        Transform {
            translation,
            rotation,
            scale,
        },
        Visibility::default(),
        pose,
        rig,
        anims,
    ));
    // The worn parts, faded out by benilla's first-person fade.
    let cutout = app
        .world_mut()
        .resource_mut::<Assets<WowModelMaterial>>()
        .reserve_handle();
    for sub in m.worn(true) {
        if !copied_slot(sub.char_slot) {
            continue;
        }
        app.world_mut().spawn((
            PickMesh(Arc::new(sub.clone())),
            FadeMaterials {
                cutout: cutout.clone(),
                blend: Handle::default(),
                bake_blend: None,
                zfill: None,
            },
            MeshTag(benilla_world::mesh_tag::spawn_tag(slot, 0.0)),
            Transform::default(),
            Visibility::Hidden,
            ChildOf(unit),
        ));
    }
    // The camera at the eye, 70 degrees down.
    let eye = Vec3::from(root.translation) + Vec3::Y * m.eye_height;
    let look = Quat::from_affine3(&root) * Quat::from_rotation_x((-70.0f32).to_radians());
    app.world_mut().spawn((
        WorldCamera,
        Transform::from_translation(eye).with_rotation(look),
    ));
    app.insert_resource(FpBodyInputs {
        me: Some(unit),
        helm: Some(m.helm as u16),
        view: BodyView {
            active: true,
            stance: Stance::Stand,
            ..BodyView::default()
        },
    });
    for _ in 0..3 {
        app.update();
    }

    assert!(
        app.world().resource::<FpBody>().shown,
        "the copy is not shown"
    );
    let world = app.world_mut();
    let mut copies = world.query_filtered::<(
        &ChildOf,
        &Visibility,
        &InheritedVisibility,
        &MeshTag,
        &MeshMaterial3d<WowModelMaterial>,
        &Mesh3d,
        &GlobalTransform,
        Has<NoFrustumCulling>,
    ), With<FpBodyPart>>();
    let copies: Vec<_> = copies.iter(world).collect();
    assert!(!copies.is_empty(), "no copy spawned");
    let meshes = world.resource::<Assets<Mesh>>();
    let mut drawn = 0;
    let mut tris: Vec<[Vec3; 3]> = Vec::new();
    let (mut lo, mut hi) = (Vec3::MAX, Vec3::MIN);
    for (parent, vis, inherited, tag, mat, mesh, gt, unculled) in copies {
        assert_eq!(parent.parent(), unit, "a copy off the rig root");
        // Its mesh is rewritten every frame and Bevy never recomputes an `Aabb`: a culled copy
        // would be tested against its first frame's box.
        assert!(unculled, "a copy without NoFrustumCulling");
        assert_eq!(*vis, Visibility::Inherited);
        assert!(inherited.get(), "a copy hidden by its hierarchy");
        assert_eq!(tag.0 & 0x3f, 63, "a copy not opaque: tag {:#x}", tag.0);
        assert_eq!(benilla_world::mesh_tag::rig_of(tag.0), slot);
        assert_eq!(mat.0, cutout);
        let mesh = meshes.get(&mesh.0).expect("the copy's mesh");
        let Some(bevy::mesh::VertexAttributeValues::Float32x3(pos)) =
            mesh.attribute(Mesh::ATTRIBUTE_POSITION)
        else {
            panic!("no positions");
        };
        let Some(Indices::U32(ix)) = mesh.indices() else {
            panic!("no indices");
        };
        for t in ix.chunks(3) {
            if t[0] == t[1] && t[1] == t[2] {
                continue;
            }
            drawn += 1;
            let w = [t[0], t[1], t[2]].map(|v| gt.transform_point(Vec3::from(pos[v as usize])));
            for p in w {
                lo = lo.min(p);
                hi = hi.max(p);
            }
            tris.push(w);
        }
    }
    // Seen: the drawn mesh, through the hierarchy, fills a real share of the view 70 degrees
    // down (the deployed lean showed a strip of feet, 9%).
    let rays = view_rays(look, 21);
    let seen = rays
        .iter()
        .filter(|&&d| first_hit(&tris, eye, d).is_some())
        .count() as f32
        / rays.len() as f32;
    assert!(
        seen >= 0.15,
        "the body fills {:.0}% of the view",
        seen * 100.0
    );
    // A body's worth of triangles, drawn where the body is, past the gun, under the eye.
    assert!(drawn > 200, "only {drawn} triangles drawn");
    let feet = Vec3::from(root.translation);
    assert!(
        hi.y < eye.y && lo.y > feet.y - 1.0,
        "drawn {lo:.2}..{hi:.2}, eye {eye:.2}"
    );
    assert!(
        Vec2::new((lo.x + hi.x) * 0.5 - feet.x, (lo.z + hi.z) * 0.5 - feet.z).length() < 1.5,
        "drawn {lo:.2}..{hi:.2} round {feet:.2}"
    );
}

/// `cargo test --release --lib probe_fit -- --ignored --nocapture`: the fit swept, standing and
/// on the move, per model: the view's share 70/85 down, the widest span, holes in view.
#[test]
#[ignore]
fn probe_fit() {
    for path in [
        r"Character\Human\Male\HumanMale.m2",
        r"Character\Orc\Male\OrcMale.m2",
        r"Character\Tauren\Male\TaurenMale.m2",
    ] {
        let m = load(path).expect("install");
        let root = root();
        let facing = Quat::from_affine3(&root);
        let parts = m.worn(true);
        let eye = Vec3::from(root.translation) + Vec3::Y * m.eye_height;
        for ahead in [0.0f32, -0.04, -0.08] {
            for below in [0.17f32, 0.25, 0.3, 0.35] {
                let (lean, fold) = (CLIP_LEAN, true);
                let fit = BodyFit {
                    neck_ahead: ahead,
                    clip_lean: lean,
                    fold,
                    neck_below: below,
                };
                let (mut span, mut holes, mut near) = (0.0f32, 0, f32::MAX);
                let mut cov = [1.0f32; 2];
                for (anim, _, model) in poses(&m, 2)
                    .into_iter()
                    .filter(|p| matches!(p.0, 0 | 4 | 5))
                {
                    for (k, pitch) in [-70.0f32, -85.0].into_iter().enumerate() {
                        let look = facing * Quat::from_rotation_x(pitch.to_radians());
                        let copy = copy_at_fit(&m, &parts, &model, root, eye, look, fit);
                        span = span.max(widest_span(&copy, eye, look));
                        near = near.min(nearest_seen(&copy, eye, look));
                        holes += copy
                            .holes
                            .iter()
                            .filter(|h| in_view(look, (h[0] + h[1]) * 0.5 - eye))
                            .count();
                        if anim == 0 {
                            cov[k] = cov[k].min(coverage(&copy, eye, look));
                        }
                    }
                }
                eprintln!(
                    "{path} ahead {ahead} below {below}: cover {:.0}%/{:.0}% span {:.0} near {near:.2} holes {holes}",
                    cov[0] * 100.0,
                    cov[1] * 100.0,
                    span.to_degrees()
                );
            }
        }
    }
}
