//! Prints weapons' sound alias names and their animations' notetracks:
//! `MW2_PATH=... cargo run --release --example mw2_weapon_sounds -- m4_mp spas12_mp`

use benilla_mw2::mw2::load::{Mw2Library, anim};

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    for name in std::env::args().skip(1) {
        let w = match lib.weapon(&name, &name) {
            Ok(w) => w,
            Err(e) => {
                println!("{name}: {e}");
                continue;
            }
        };
        println!("{name}: {:?}", w.sounds);
        println!("  reload {:?}", w.reload);
        for (slot, label) in [
            (anim::FIRE, "fire"),
            (anim::RELOAD, "reload"),
            (anim::RELOAD_EMPTY, "reload_empty"),
            (anim::RAISE, "raise"),
            (anim::FIRST_RAISE, "first_raise"),
            (anim::MELEE, "melee"),
            (anim::SPRINT_IN, "sprint_in"),
        ] {
            if let Some(c) = w.clips.get(slot).and_then(Option::as_ref) {
                let notes: Vec<_> = c
                    .notifies
                    .iter()
                    .map(|n| format!("{}@{:.2}", n.name, n.time))
                    .collect();
                println!("  {label} {:.2}s: {}", c.duration(), notes.join(" "));
            }
        }
    }
}
