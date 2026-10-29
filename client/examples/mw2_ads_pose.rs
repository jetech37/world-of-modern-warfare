//! Poses each gun at the end of its aim clip and prints where its muzzle sits relative to the
//! eye (MW2 inches, X forward, Y left, Z up): an aimed gun's muzzle is near Y 0, Z a little low.
//! `MW2_PATH=... cargo run --release --example mw2_ads_pose -- ump45_mp m4_mp`

use asset_anim::{AnimInstance, Attach, DObj};
use benilla_mw2::mw2::load::{Mw2Library, anim};
use bevy::math::Mat4;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    for name in std::env::args().skip(1) {
        let (name, camo) = match name.split_once('@') {
            Some((n, c)) => (n.to_owned(), Some(c.to_owned())),
            None => (name.clone(), None),
        };
        let base = format!("{}_mp", name.split('_').next().unwrap());
        let w = match lib.weapon_with(&name, &base, camo.as_deref()) {
            Ok(w) => w,
            Err(e) => {
                println!("{name}: {e}");
                continue;
            }
        };
        let poses: Vec<_> = w
            .parts
            .iter()
            .map(|p| p.skel.pose.as_ref().unwrap())
            .collect();
        let dobj = DObj::build(
            &poses
                .iter()
                .enumerate()
                .map(|(i, p)| {
                    let attach = (i > 0).then(|| Attach {
                        parent_model: 0,
                        tag: "tag_weapon".to_owned(),
                    });
                    (*p, attach)
                })
                .collect::<Vec<_>>(),
        )
        .unwrap();
        println!(
            "{name} camo {camo:?}: parts {:?}",
            w.parts
                .iter()
                .map(|p| p.skel.name.clone())
                .collect::<Vec<_>>()
        );
        let find = |n: &str| {
            dobj.bones
                .iter()
                .position(|b| b.name.eq_ignore_ascii_case(n))
        };
        let view = find("tag_view").unwrap();
        let flash = find("tag_flash");
        let gun = find("j_gun");
        for (label, ads) in [("hip", 0.0f32), ("ads", 1.0)] {
            let mut inst = Vec::new();
            let idle = w.clips[anim::IDLE].clone().unwrap();
            let it = dobj.tracks_for(&idle);
            inst.push(AnimInstance {
                clip: &idle,
                tracks: &it,
                time: 0.0,
                weight: (1.0 - ads).max(1e-3),
                parts: None,
            });
            let up = w.clips[anim::ADS_UP].clone().unwrap();
            let ut = dobj.tracks_for(&up);
            if ads > 0.0 {
                inst.push(AnimInstance {
                    clip: &up,
                    tracks: &ut,
                    time: up.duration(),
                    weight: ads,
                    parts: None,
                });
            }
            let world = dobj.pose(&inst, &dobj.all_parts(), Mat4::IDENTITY);
            let inv = world[view].inverse();
            let at = |b: Option<usize>| b.map(|b| (inv * world[b]).w_axis.truncate());
            println!(
                "{name:24} {label}: flash {:?} j_gun {:?}  (ads clip {} {:.2}s, {} tracks, idle {} tracks)",
                at(flash).map(|v| (v * 10.0).round() / 10.0),
                at(gun).map(|v| (v * 10.0).round() / 10.0),
                up.name,
                up.duration(),
                up.tracks.len(),
                idle.tracks.len()
            );
        }
    }
}
