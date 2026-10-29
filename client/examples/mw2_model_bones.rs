//! Prints a captured model's bones (index, name, parent-relative bind) and each surface's bones:
//! `MW2_PATH=... cargo run --release --example mw2_model_bones -- sentry_minigun`

use benilla_mw2::mw2::load::Mw2Library;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    for name in std::env::args().skip(1) {
        let Some(part) = lib.world_model(&name) else {
            println!("{name}: not captured");
            continue;
        };
        let skel = &part.skel;
        for (i, n) in skel.bone_names.iter().enumerate() {
            let b = skel.bones.get(i);
            println!("bone {i:2} {n:24} trans {:?}", b.map(|b| b.trans));
        }
        for (k, s) in part.surfaces.iter().enumerate() {
            let bones: Vec<usize> = (0..skel.bone_names.len())
                .filter(|&b| s.part_bits[b / 32] & (0x8000_0000 >> (b % 32)) != 0)
                .collect();
            let (v0, vn) = s.vertices;
            let xs = &skel.positions[v0..v0 + vn];
            let max_z = xs.iter().map(|p| p[2]).fold(f32::MIN, f32::max);
            println!("surface {k}: {vn} verts, bones {bones:?}, top z {max_z:.1}");
        }
    }
}
