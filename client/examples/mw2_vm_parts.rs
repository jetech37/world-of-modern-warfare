//! A weapon's viewmodel gun bones and the surfaces skinned to each, with their hide tags:
//! `cargo run --release --example mw2_vm_parts -- rpg_mp`

use benilla_mw2::mw2::load::Mw2Library;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    for name in std::env::args().skip(1) {
        let w = lib.weapon(&name, &name).expect("weapon");
        let gun = &w.parts[1];
        println!("{name}: model {}, hide {:?}", gun.skel.name, w.hide_tags);
        for (i, n) in gun.skel.bone_names.iter().enumerate() {
            let surfaces: Vec<usize> = gun
                .surfaces
                .iter()
                .enumerate()
                .filter(|(_, s)| anim_iw4::hide_part_bit(&s.part_bits, i))
                .map(|(k, _)| k)
                .collect();
            println!("  bone {i:2} {n:24} surfaces {surfaces:?}");
        }
        for slot in [1usize, 3, 5, 9, 0xA] {
            if let Some(c) = w.clips.get(slot).and_then(Option::as_ref) {
                let rocket: Vec<_> = c
                    .tracks
                    .iter()
                    .map(|t| t.name.as_str())
                    .filter(|n| n.contains("rocket") || n.contains("clip"))
                    .collect();
                println!("  clip slot {slot:#x} {} keys {:?}", c.name, rocket);
            }
        }
    }
}
