//! Tracks the right wrist through a gun's knife clip (eye space, MW2 inches: X forward, Y left,
//! Z up), to pick the held-weapon ready pose: `cargo run --release --example mw2_melee_pose -- m4_mp`

use asset_anim::{AnimInstance, Attach, DObj};
use benilla_mw2::mw2::load::{Mw2Library, anim};
use bevy::math::Mat4;

const STEPS: usize = 30;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    for name in std::env::args().skip(1) {
        let w = lib.weapon(&name, &name).expect("weapon");
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
        let (view, wrist) = (
            find("tag_view").unwrap(),
            find(&std::env::var("WRIST").unwrap_or("j_wrist_ri".into())).unwrap(),
        );
        let gun = find("j_gun");
        for slot in [anim::MELEE, anim::MELEE_CHARGE] {
            let Some(clip) = w.clips[slot].clone() else {
                continue;
            };
            let tracks = dobj.tracks_for(&clip);
            let idle = w.clips[anim::IDLE].clone().unwrap();
            let it = dobj.tracks_for(&idle);
            println!(
                "{name} slot {slot:#x} {} {:.2}s",
                clip.name,
                clip.duration()
            );
            for i in 0..=STEPS {
                let t = clip.duration() * i as f32 / STEPS as f32;
                let inst = [
                    AnimInstance {
                        clip: &idle,
                        tracks: &it,
                        time: 0.0,
                        weight: 1e-3,
                        parts: None,
                    },
                    AnimInstance {
                        clip: &clip,
                        tracks: &tracks,
                        time: t,
                        weight: 1.0,
                        parts: None,
                    },
                ];
                let world = dobj.pose(&inst, &dobj.all_parts(), Mat4::IDENTITY);
                let inv = world[view].inverse();
                let at = |b: usize| {
                    let v = (inv * world[b]).w_axis.truncate();
                    format!("({:5.1} {:5.1} {:5.1})", v.x, v.y, v.z)
                };
                println!(
                    "  {:.2}: wrist {} gun {}",
                    t,
                    at(wrist),
                    gun.map(at).unwrap_or_default()
                );
            }
        }
    }
}
