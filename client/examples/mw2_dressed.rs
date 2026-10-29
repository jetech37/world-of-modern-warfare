//! Prints a weapon's hidden attachment tags alone and dressed in another's look:
//! `cargo run --release --example mw2_dressed -- gl_m4_mp m4_gl_mp`

use benilla_mw2::mw2::load::Mw2Library;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    let mut a = std::env::args().skip(1);
    let (variant, look) = (a.next().expect("variant"), a.next().expect("look"));
    let look_base = format!("{}_mp", look.split('_').next().unwrap());
    let alone = lib.weapon_with(&variant, &variant, None).expect("alone");
    let dressed = lib
        .weapon_dressed(&variant, &variant, &look, &look_base, None)
        .expect("dressed");
    println!("{variant} alone hides {:?}", alone.hide_tags);
    println!("{variant} as {look} hides {:?}", dressed.hide_tags);
}
