//! Each LOD 0 surface of a viewmodel with its material and textures:
//! `cargo run --release --example mw2_surfaces -- viewmodel_m4`

use benilla_mw2::mw2::load::Mw2Library;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    for name in std::env::args().skip(1) {
        for line in lib.describe_surfaces(&name) {
            println!("{line}");
        }
    }
}
