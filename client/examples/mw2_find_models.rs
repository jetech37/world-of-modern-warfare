//! Lists captured viewmodel names containing each pattern:
//! `cargo run --release --example mw2_find_models -- rpg rocket`

use benilla_mw2::mw2::load::Mw2Library;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    let pats: Vec<String> = std::env::args().skip(1).map(|p| p.to_lowercase()).collect();
    let mut names: Vec<&str> = lib
        .viewmodel_names()
        .filter(|n| pats.iter().any(|p| n.to_lowercase().contains(p)))
        .collect();
    names.sort();
    for n in names {
        println!("{n}");
    }
}
