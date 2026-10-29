//! Measures where the first-person arms' kept geometry ends: each race's arm chain (the bones
//! from the shoulder key-bone to the hand), the bones `wow_arms` draws, and the ring of vertices
//! where kept triangles meet dropped ones; then MW2's view-arm joints at the idle, in the eye's
//! frame (Bevy axes, inches). Prints numbers only:
//! `MW2_PATH=... WOW_DATA=... cargo run --release --example wow_arm_cut -- [weapon]`

use std::collections::{HashMap, HashSet};

use asset_anim::{AnimInstance, Attach, DObj};
use benilla_mw2::mw2::load::{Mw2Library, anim};
use bevy::math::{Mat3, Mat4, Vec3, Vec4};

const MODELS: &[&str] = &[
    "Character\\Human\\Male\\HumanMale.m2",
    "Character\\Human\\Female\\HumanFemale.m2",
    "Character\\Orc\\Male\\OrcMale.m2",
    "Character\\Dwarf\\Male\\DwarfMale.m2",
    "Character\\NightElf\\Male\\NightElfMale.m2",
    "Character\\Scourge\\Male\\ScourgeMale.m2",
    "Character\\Tauren\\Male\\TaurenMale.m2",
    "Character\\Gnome\\Male\\GnomeMale.m2",
    "Character\\Troll\\Male\\TrollMale.m2",
];

fn wow_to_bevy(p: [f32; 3]) -> Vec3 {
    Vec3::new(-p[1], p[2], -p[0])
}

fn main() {
    if let Some(data) = benilla_formats::wow_data() {
        let mut chain = benilla_formats::open_chain(&data).expect("chain");
        for path in MODELS {
            wow_model(&mut chain, path);
        }
    } else {
        println!("no WoW install (WOW_DATA)");
    }
    let weapons: Vec<String> = std::env::args().skip(1).collect();
    match Mw2Library::open() {
        Ok(lib) => {
            for w in &weapons {
                mw2_arms(&lib, w);
            }
        }
        Err(e) => println!("no MW2 install: {e}"),
    }
}

fn wow_model(chain: &mut benilla_formats::Chain, path: &str) {
    let Ok(bytes) = chain.read_file(path) else {
        println!("{path}: missing");
        return;
    };
    let skel = benilla_formats::parse_m2_skeleton(&bytes).expect("skeleton");
    let subs = benilla_formats::load_m2_mesh(chain, path).expect("mesh");
    let parents: Vec<i16> = skel.bones.iter().map(|b| b.parent).collect();
    let pivots: Vec<Vec3> = skel.bones.iter().map(|b| wow_to_bevy(b.pivot)).collect();
    let parent = |b: usize| usize::try_from(parents[b]).ok();
    let under = |b: usize, root: usize| {
        let mut c = Some(b);
        while let Some(x) = c {
            if x == root {
                return true;
            }
            c = parent(x);
        }
        false
    };
    let key = |k: i16| skel.bones.iter().position(|b| b.key_bone == k);
    println!("== {path}: {} bones", skel.bones.len());
    fp_body(&bytes, &skel, &subs, &pivots, &parents);
    let helm = benilla_formats::parse_m2_attachments(&bytes)
        .ok()
        .and_then(|a| a.iter().find(|a| a.id == 11).map(|a| a.bone));
    println!(
        "  head key bone {:?} (parent {:?}), helm attachment bone {helm:?} (parent {:?})",
        key(6),
        key(6).map(|h| skel.bones[h].parent),
        helm.map(|h| skel.bones[usize::from(h)].parent)
    );
    for (side, (shoulder_key, finger_lo)) in [(3i16, 8i16), (2, 13)].into_iter().enumerate() {
        let Some(root) = key(shoulder_key) else {
            println!("  side {side}: no shoulder key bone");
            continue;
        };
        // The finger roots: key bones finger_lo..finger_lo+4.
        let fingers: Vec<usize> = (finger_lo..finger_lo + 5).filter_map(key).collect();
        let hand = fingers.first().and_then(|&f| parent(f));
        let Some(hand) = hand else {
            println!("  side {side}: no fingers");
            continue;
        };
        let mut chain_bones = vec![hand];
        while let Some(p) = parent(*chain_bones.last().unwrap()) {
            chain_bones.push(p);
            if p == root {
                break;
            }
        }
        chain_bones.reverse();
        print!("  side {side}: chain");
        for &b in &chain_bones {
            print!(
                " {b}(key {}, {:.3} {:.3} {:.3})",
                skel.bones[b].key_bone, pivots[b].x, pivots[b].y, pivots[b].z
            );
        }
        println!();
        if let Some(p) = parent(root) {
            println!(
                "    shoulder's parent {p} key {} at {:.3?}",
                skel.bones[p].key_bone, pivots[p]
            );
        }
        let reach = (0..skel.bones.len())
            .filter(|&b| under(b, hand) && b != hand)
            .map(|b| (pivots[b] - pivots[hand]).length())
            .fold(0.0f32, f32::max);
        let seg: Vec<f32> = chain_bones
            .windows(2)
            .map(|w| (pivots[w[1]] - pivots[w[0]]).length())
            .collect();
        println!("    segment lengths {seg:.3?}");
        // Each segment's thickness: the dominant-weight vertices' distance from the bone line.
        for w in chain_bones.windows(2).skip(1) {
            let (a, b) = (pivots[w[0]], pivots[w[1]]);
            let ab = b - a;
            let mut ds: Vec<f32> = Vec::new();
            for s in subs
                .iter()
                .filter(|s| s.geoset_id == 0 || s.geoset_id % 100 == 1)
            {
                for (i, p) in s.positions.iter().enumerate() {
                    let (j, wt) = (s.joints[i], s.weights[i]);
                    let main = j
                        .iter()
                        .zip(wt)
                        .max_by(|x, y| x.1.total_cmp(&y.1))
                        .map(|(j, _)| usize::from(*j));
                    if main != Some(w[0]) {
                        continue;
                    }
                    let p = wow_to_bevy(*p);
                    let t = ((p - a).dot(ab) / ab.length_squared()).clamp(0.0, 1.0);
                    ds.push((p - (a + ab * t)).length());
                }
            }
            ds.sort_by(|x, y| x.total_cmp(y));
            if let (Some(&mx), false) = (ds.last(), ds.is_empty()) {
                let med = ds[ds.len() / 2];
                println!(
                    "    bone {} radius median {med:.3} max {mx:.3} (in inches at the hand's scale: {:.2} / {:.2})",
                    w[0],
                    med * 3.84 / reach.max(1e-3),
                    mx * 3.84 / reach.max(1e-3)
                );
            }
        }
        println!("    hand reach {reach:.3}");
        // What wow_arms draws today: the hand's parent ("forearm"), its parent ("upper").
        let forearm = parent(hand).unwrap();
        let upper = parent(forearm).unwrap();
        let drawn_now: HashSet<usize> = (0..skel.bones.len())
            .filter(|&b| b == upper || b == forearm || under(b, hand))
            .collect();
        let arm_all: HashSet<usize> = (0..skel.bones.len()).filter(|&b| under(b, root)).collect();
        for (label, drawn) in [("drawn today", &drawn_now), ("whole arm", &arm_all)] {
            ring(label, &subs, drawn, &chain_bones, &pivots);
        }
    }
}

/// The first-person body (`crate::fp_body`) at the bind pose: what it keeps, where its openings
/// are, and how far the upper body leans back to keep them out of view, with the camera where
/// benilla seats it at zoom 0 (attachment 17's height + 0.0972, over the model's origin).
fn fp_body(
    bytes: &[u8],
    skel: &benilla_formats::Skeleton,
    subs: &[benilla_formats::RenderSubmesh],
    _pivots: &[Vec3],
    parents: &[i16],
) {
    use benilla_mw2::fp_body as fp;
    let parent = |b: usize| usize::try_from(parents[b]).ok();
    let under = |b: usize, root: usize| {
        let mut c = Some(b);
        while let Some(x) = c {
            if x == root {
                return true;
            }
            c = parent(x);
        }
        false
    };
    let key = |k: i16| skel.bones.iter().position(|b| b.key_bone == k);
    let attach = benilla_formats::parse_m2_attachments(bytes).unwrap_or_default();
    let point = |id: u16| attach.iter().find(|a| a.id == id);
    let Some(helm) = point(fp::ATTACH_HELM) else {
        println!("  fp body: no helm point");
        return;
    };
    let head_root = fp::head_of(parents, usize::from(helm.bone));
    let (Some(r), Some(l), Some(spine)) = (key(3), key(2), key(4)) else {
        println!("  fp body: no shoulders or spine");
        return;
    };
    let eye_y = point(17).map_or(1.8, |a| wow_to_bevy(a.position).y) + 0.0972;
    let eye = Vec3::new(0.0, eye_y, 0.0);
    let head = |b: usize| under(b, head_root);
    // The clavicle (the shoulder key-bone) stays with the chest.
    let arm = |b: usize| (under(b, r) && b != r) || (under(b, l) && b != l);
    let upper = |b: usize| under(b, spine);
    let parts: Vec<&benilla_formats::RenderSubmesh> = subs
        .iter()
        .filter(|s| s.geoset_id == 0 || s.geoset_id % 100 == 1)
        .filter(|s| {
            matches!(
                s.char_slot,
                Some(
                    benilla_formats::CharSkinSlot::Body
                        | benilla_formats::CharSkinSlot::Object
                        | benilla_formats::CharSkinSlot::SkinExtra
                )
            )
        })
        .collect();
    let split: Vec<(Vec<[u32; 3]>, Vec<bool>)> = parts
        .iter()
        .map(|s| {
            let corner = |i: u32| (s.joints[i as usize], s.weights[i as usize]);
            let tris: Vec<[u32; 3]> = s.indices.chunks(3).map(|t| [t[0], t[1], t[2]]).collect();
            let keep = tris
                .iter()
                .map(|t| fp::keeps_body_triangle(t.map(corner), &head, &arm))
                .collect();
            (tris, keep)
        })
        .collect();
    let open = fp::openings(
        &parts
            .iter()
            .zip(&split)
            .map(|(s, (tris, kept))| fp::PartTris {
                positions: &s.positions,
                tris,
                kept,
            })
            .collect::<Vec<_>>(),
    );
    let (mut kept, mut dropped, mut openings) = (0usize, 0usize, Vec::new());
    let mut near = 0usize;
    for ((s, (tris, keep)), open) in parts.iter().zip(&split).zip(&open) {
        let corner = |i: u32| (s.joints[i as usize], s.weights[i as usize]);
        kept += keep.iter().filter(|k| **k).count();
        dropped += keep.iter().filter(|k| !**k).count();
        for &v in open {
            let (j, w) = corner(v);
            let share: f32 = j
                .iter()
                .zip(w)
                .filter(|(j, _)| upper(usize::from(**j)))
                .map(|(_, w)| w)
                .sum();
            if share > 0.5 {
                openings.push(wow_to_bevy(s.positions[v as usize]));
            }
        }
        for (t, k) in tris.iter().zip(keep) {
            if *k && fp::too_near(t.map(|v| wow_to_bevy(s.positions[v as usize])), eye) {
                near += 1;
            }
        }
    }
    let ahead = openings
        .iter()
        .map(|p| (-p.z, eye.y - p.y))
        .fold((f32::MIN, 0.0), |a, b| if b.0 > a.0 { b } else { a });
    println!(
        "  fp body: eye {eye_y:.3}, kept {kept} dropped {dropped} triangles, {} opening vertices \
         (the most forward {:.3} ahead, {:.3} below), {near} kept triangles within the eye's clear",
        openings.len(),
        ahead.0,
        ahead.1
    );
}

/// The boundary between kept and dropped triangles, as `wow_arms::keeps_triangle` keeps them,
/// over the naked body's geosets (0 and each group's first variant).
fn ring(
    label: &str,
    subs: &[benilla_formats::RenderSubmesh],
    drawn: &HashSet<usize>,
    chain: &[usize],
    pivots: &[Vec3],
) {
    let mut kept_edges: HashMap<(u32, u32, usize), u32> = HashMap::new();
    let mut dropped_verts: HashSet<(usize, u32)> = HashSet::new();
    let mut kept_verts: HashSet<(usize, u32)> = HashSet::new();
    for (si, s) in subs.iter().enumerate() {
        if !(s.geoset_id == 0 || s.geoset_id % 100 == 1) {
            continue;
        }
        if !matches!(s.char_slot, Some(benilla_formats::CharSkinSlot::Body)) {
            continue;
        }
        for tri in s.indices.chunks(3) {
            let keep = tri.iter().all(|&i| {
                let (j, w) = (s.joints[i as usize], s.weights[i as usize]);
                j.iter()
                    .zip(w)
                    .any(|(j, w)| w > 0.05 && drawn.contains(&usize::from(*j)))
            });
            let any_arm = tri.iter().any(|&i| {
                let (j, w) = (s.joints[i as usize], s.weights[i as usize]);
                j.iter()
                    .zip(w)
                    .any(|(j, w)| w > 0.05 && drawn.contains(&usize::from(*j)))
            });
            if keep {
                for k in 0..3 {
                    let (a, b) = (tri[k], tri[(k + 1) % 3]);
                    let e = (a.min(b), a.max(b), si);
                    *kept_edges.entry(e).or_default() += 1;
                    kept_verts.insert((si, tri[k]));
                }
            } else {
                let _ = any_arm;
                for &i in tri {
                    dropped_verts.insert((si, i));
                }
            }
        }
    }
    // Open edges of the kept surface whose both ends also touch a dropped triangle (matched by
    // position: a submesh duplicates its vertices): the cut.
    let key = |si: usize, i: u32| {
        let p = subs[si].positions[i as usize];
        [p[0].to_bits(), p[1].to_bits(), p[2].to_bits()]
    };
    let dropped_at: HashSet<[u32; 3]> = dropped_verts.iter().map(|&(si, i)| key(si, i)).collect();
    let open = kept_edges.values().filter(|&&n| n == 1).count();
    let mut cut: Vec<Vec3> = Vec::new();
    for (&(a, b, si), &n) in &kept_edges {
        if n == 1 && dropped_at.contains(&key(si, a)) && dropped_at.contains(&key(si, b)) {
            for i in [a, b] {
                cut.push(wow_to_bevy(subs[si].positions[i as usize]));
            }
        }
    }
    println!(
        "    {label}: {} kept vertices, {open} open edges",
        kept_verts.len()
    );
    let side_x = pivots[chain[0]].x.signum();
    cut.retain(|p| p.x.signum() == side_x || p.x.abs() < 0.02);
    if cut.is_empty() {
        println!("    {label}: no cut edge on this side");
        return;
    }
    // Where along the chain each cut vertex sits: its nearest chain segment and the share along.
    let mut by_seg: HashMap<usize, Vec<f32>> = HashMap::new();
    for p in &cut {
        let (seg, along, _) = chain
            .windows(2)
            .enumerate()
            .map(|(i, w)| {
                let (a, b) = (pivots[w[0]], pivots[w[1]]);
                let ab = b - a;
                let t = ((*p - a).dot(ab) / ab.length_squared()).clamp(-0.5, 1.5);
                let d = (*p - (a + ab * t.clamp(0.0, 1.0))).length();
                (i, t, d)
            })
            .min_by(|a, b| a.2.total_cmp(&b.2))
            .unwrap();
        by_seg.entry(seg).or_default().push(along);
    }
    let mut segs: Vec<_> = by_seg.into_iter().collect();
    segs.sort_by_key(|(s, _)| *s);
    let rel: Vec<String> = cut
        .iter()
        .map(|p| {
            let q = *p - pivots[chain[1]];
            format!("({:.3},{:.3},{:.3})", q.x, q.y, q.z)
        })
        .collect();
    println!(
        "    {label}: ring relative to the upper arm's pivot: {}",
        rel.join(" ")
    );
    print!("    {label}: {} cut vertices;", cut.len());
    for (s, v) in segs {
        let lo = v.iter().copied().fold(f32::MAX, f32::min);
        let hi = v.iter().copied().fold(f32::MIN, f32::max);
        print!(" seg {s} ({} at {lo:.2}..{hi:.2})", v.len());
    }
    println!();
}

fn tag_view_to_camera() -> Mat4 {
    let f = Mat3::from_cols(
        Vec3::new(0.0, 0.0, -1.0),
        Vec3::new(-1.0, 0.0, 0.0),
        Vec3::new(0.0, 1.0, 0.0),
    );
    Mat4::from_cols(
        f.x_axis.extend(0.0),
        f.y_axis.extend(0.0),
        f.z_axis.extend(0.0),
        Vec4::W,
    )
}

fn mw2_arms(lib: &Mw2Library, name: &str) {
    let Ok(w) = lib.weapon(name, name) else {
        println!("{name}: no such weapon");
        return;
    };
    let poses: Vec<_> = w
        .parts
        .iter()
        .map(|p| p.skel.pose.as_ref().unwrap())
        .collect();
    let parts: Vec<_> = poses
        .iter()
        .enumerate()
        .map(|(i, p)| {
            let attach = (i > 0).then(|| Attach {
                parent_model: 0,
                tag: "tag_weapon".to_owned(),
            });
            (*p, attach)
        })
        .collect();
    let dobj = DObj::build(&parts).unwrap();
    let find = |n: &str| {
        dobj.bones
            .iter()
            .position(|b| b.name.eq_ignore_ascii_case(n))
    };
    let view = find("tag_view").unwrap();
    let idle = w.clips[anim::IDLE].clone().unwrap();
    let it = dobj.tracks_for(&idle);
    let inst = [AnimInstance {
        clip: &idle,
        tracks: &it,
        time: 0.0,
        weight: 1.0,
        parts: None,
    }];
    let world = dobj.pose(&inst, &dobj.all_parts(), Mat4::IDENTITY);
    let eye = tag_view_to_camera() * world[view].inverse();
    println!("== MW2 {name} idle, eye frame (inches, x right, y up, z back)");
    for side in ["ri", "le"] {
        for j in ["shoulder", "elbow", "wrist"] {
            let n = format!("j_{j}_{side}");
            if let Some(b) = find(&n) {
                let p = eye.transform_point3(world[b].w_axis.truncate());
                println!("  {n:16} {:7.2} {:7.2} {:7.2}", p.x, p.y, p.z);
            }
        }
        let names = [
            "shoulder",
            "elbow",
            "wrist",
            "mid_SIDE_0",
            "mid_SIDE_1",
            "mid_SIDE_2",
            "thumb_SIDE_0",
            "thumb_SIDE_1",
            "thumb_SIDE_2",
        ];
        println!("  {side} matrices (eye frame, columns x y z w):");
        for n in names {
            let n = if n.contains("SIDE") {
                format!("j_{}", n.replace("SIDE", side))
            } else {
                format!("j_{n}_{side}")
            };
            if let Some(b) = find(&n) {
                let m = eye * world[b];
                let c = |v: Vec4| format!("[{:.4}, {:.4}, {:.4}]", v.x, v.y, v.z);
                println!(
                    "    {n}: {} {} {} {}",
                    c(m.x_axis),
                    c(m.y_axis),
                    c(m.z_axis),
                    c(m.w_axis)
                );
                let p = dobj.bones[b].parent.map(|p| dobj.bones[p].name.clone());
                println!("      parent {p:?}");
            }
        }
        let n = format!("j_mid_{side}_2");
        if let (Some(b), Some(wr)) = (find(&n), find(&format!("j_wrist_{side}"))) {
            let d = (world[b].w_axis - world[wr].w_axis).truncate().length();
            println!("  hand (wrist to mid_2) {d:.2}");
        }
    }
    // Which bones the MW2 arm surfaces lean on near the shoulder: the clavicle and the shoulder.
    for n in [
        "j_clavicle_ri",
        "j_clavicle_le",
        "j_shouldertwist_ri",
        "j_elbow_bulge_ri",
    ] {
        if let Some(b) = find(n) {
            let p = eye.transform_point3(world[b].w_axis.truncate());
            println!("  {n:16} {:7.2} {:7.2} {:7.2}", p.x, p.y, p.z);
        }
    }
}
