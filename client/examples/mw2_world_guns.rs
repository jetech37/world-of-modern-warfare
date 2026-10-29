//! Prints each gun's world model (what an NPC carries) and its muzzle:
//! `MW2_PATH=... cargo run --release --example mw2_world_guns -- m4_mp ak47_mp`

use benilla_mw2::mw2::load::Mw2Library;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    for name in std::env::args().skip(1) {
        let model = lib
            .weapons
            .get(&name)
            .and_then(|r| r.stats.world_model.clone());
        match lib.world_gun(&name) {
            Some(g) => println!(
                "{name}: {model:?}, {} surfaces, muzzle {:?}",
                g.part.surfaces.len(),
                g.muzzle
            ),
            None => println!("{name}: {model:?}, no world model captured"),
        }
    }
}
