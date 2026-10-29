//! Lists weapons that load without an aim (ADS-up) clip, to find guns that can't aim down
//! sights: `MW2_PATH=... cargo run --release --example mw2_ads_audit [-- --all]`

use benilla_mw2::mw2::load::{Mw2Library, anim};

fn main() {
    let all = std::env::args().any(|a| a == "--all");
    let lib = Mw2Library::open().expect("MW2 install");
    let mut names: Vec<&String> = lib.weapons.keys().filter(|n| n.ends_with("_mp")).collect();
    names.sort();
    let (mut ok, mut bad) = (0, 0);
    for name in names {
        let base = format!("{}_mp", name.split('_').next().unwrap_or(name));
        let Ok(w) = lib.weapon_with(name, &base, None) else {
            continue;
        };
        let has = |s: usize| w.clips.get(s).is_some_and(Option::is_some);
        let aims = has(anim::ADS_UP);
        if aims {
            ok += 1
        } else {
            bad += 1
        }
        if all || !aims {
            let r = &lib.weapons[name];
            let named = r.anims.iter().filter(|a| a.is_some()).count();
            println!(
                "{name:32} ads {aims:5} idle {:5} fire {:5} record_anims {named:2} akimbo {}",
                has(anim::IDLE),
                has(anim::FIRE),
                w.akimbo()
            );
        }
    }
    println!("aims {ok}, no aim {bad}");
}
