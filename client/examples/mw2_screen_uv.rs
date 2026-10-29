//! The heartbeat screen surface's vertices: model position (inches) and uv, to map the radar:
//! `cargo run --release --example mw2_screen_uv -- m4_heartbeat_mp`

use benilla_mw2::mw2::load::Mw2Library;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    for name in std::env::args().skip(1) {
        let w = lib.weapon(&name, &name).expect("weapon");
        for part in &w.parts {
            let skel = &part.skel;
            for s in part.surfaces.iter().filter(|s| s.motion_tracker) {
                let (v0, vn) = s.vertices;
                println!("{}: {vn} vertices", skel.name);
                for i in v0..v0 + vn {
                    println!("  pos {:?} uv {:?}", skel.positions[i], skel.uvs.get(i));
                }
            }
        }
    }
}
