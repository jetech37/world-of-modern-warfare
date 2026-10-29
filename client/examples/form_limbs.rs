//! Measures each form's front limbs as crate::form_limbs finds them on the form's creature model:
//! the hand attachment bones, the limb roots `forms::forelimb_bones` walks up to, the triangles
//! kept, and the limb's reach (root pivot to hand pivot, yards at the display's scale). Prints
//! numbers only: `WOW_DATA=... cargo run --release --example form_limbs`

use benilla_mw2::forms::{FORMS, LimbSkeleton, find_forelimbs, keeps_limb_triangle};

fn main() {
    let Some(data) = benilla_formats::wow_data() else {
        println!("no WoW install (WOW_DATA)");
        return;
    };
    let mut chain = benilla_formats::open_chain(&data).expect("chain");
    let catalog = benilla_formats::load_creature_catalog(&mut chain).expect("creatures");
    for f in FORMS.iter() {
        let Some(model) = catalog.model(f.display) else {
            println!("{} (display {}): no model", f.name, f.display);
            continue;
        };
        let path = model.model_path.replace(".mdx", ".m2").replace(".MDX", ".m2");
        let Ok(bytes) = chain.read_file(&path) else {
            println!("{}: {path} missing", f.name);
            continue;
        };
        let skel = benilla_formats::parse_m2_skeleton(&bytes).expect("skeleton");
        let attach = benilla_formats::parse_m2_attachments(&bytes).unwrap_or_default();
        let point = |id: u16| attach.iter().find(|a| a.id == id).map(|a| usize::from(a.bone));
        let parents: Vec<i16> = skel.bones.iter().map(|b| b.parent).collect();
        let hands = [point(1), point(2)];
        let head = point(11);
        let pivots: Vec<[f32; 3]> = skel.bones.iter().map(|b| b.pivot).collect();
        let key_bones: Vec<i16> = skel.bones.iter().map(|b| b.key_bone).collect();
        let (limbs, source) = find_forelimbs(&LimbSkeleton {
            parents: &parents,
            pivots: &pivots,
            key_bones: &key_bones,
            hands,
            head,
        });
        println!("  limbs from {source:?}");
        if std::env::var_os("FORM_BONES").is_some() {
            for (i, b) in skel.bones.iter().enumerate() {
                let mut depth = 0;
                let mut c = b.parent;
                while c >= 0 { depth += 1; c = skel.bones[c as usize].parent; }
                let kids = parents.iter().filter(|&&p| p == i as i16).count();
                println!("    bone {i}: parent {}, key {}, depth {depth}, kids {kids}, pivot {:?}", b.parent, b.key_bone, b.pivot.map(|v| (v * 100.0).round() / 100.0));
            }
        }
        let subs = benilla_formats::load_m2_mesh(&mut chain, &path).unwrap_or_default();
        println!(
            "== {} (form {}, display {}): {path}, scale {:.2}, {} bones, hands {:?}, head {:?}, key bones {:?}",
            f.name,
            f.form,
            f.display,
            model.scale,
            skel.bones.len(),
            hands,
            head,
            skel.bones
                .iter()
                .enumerate()
                .filter(|(_, b)| b.key_bone >= 0)
                .map(|(i, b)| (b.key_bone, i))
                .collect::<Vec<_>>()
        );
        for (side, limb) in ["right", "left"].iter().zip(&limbs) {
            let bones: Vec<usize> = limb.iter().enumerate().filter(|(_, k)| **k).map(|(b, _)| b).collect();
            let root = bones.iter().copied().find(|&b| {
                usize::try_from(parents[b]).ok().is_none_or(|p| !limb[p])
            });
            let in_limb = |b: usize| limb.get(b).copied().unwrap_or(false);
            let mut tris = 0;
            for s in &subs {
                for t in s.indices.chunks_exact(3) {
                    let c = |i: u32| {
                        let i = i as usize;
                        (s.joints.get(i).copied().unwrap_or_default(), s.weights.get(i).copied().unwrap_or_default())
                    };
                    if keeps_limb_triangle([c(t[0]), c(t[1]), c(t[2])], &in_limb) {
                        tris += 1;
                    }
                }
            }
            let tip = bones.iter().copied().min_by(|&a, &b| skel.bones[a].pivot[2].total_cmp(&skel.bones[b].pivot[2]));
            let reach = match (root, tip) {
                (Some(r), Some(h)) => {
                    let a = skel.bones[r].pivot;
                    let b = skel.bones[h].pivot;
                    let d = ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2]).powi(2)).sqrt();
                    d * model.scale
                }
                _ => 0.0,
            };
            let at = |b: Option<usize>| b.map(|b| skel.bones[b].pivot.map(|v| (v * model.scale * 100.0).round() / 100.0));
            println!(
                "  {side}: {} bones from root {root:?} (at {:?}) to its lowest bone (at {:?}), {tris} triangles, reach {reach:.2} yd",
                bones.len(),
                at(root),
                at(tip),
            );
        }
    }
}
