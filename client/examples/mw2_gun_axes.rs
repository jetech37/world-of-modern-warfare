//! Every MW2 world gun's barrel direction two ways: the drawn mesh's (crate::mw2::load
//! `grip_and_muzzle`, what the hand holds by) and the model's `tag_flash` bone. A gun where they
//! disagree is held backwards by one of them. Names only, no game data written:
//! `MW2_PATH=... cargo run --release --example mw2_gun_axes`
use benilla_mw2::mw2::load::Mw2Library;

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    let mut names: Vec<&String> = lib
        .weapons
        .keys()
        .filter(|n| n.ends_with("_mp") && !n.contains("_akimbo"))
        .collect();
    names.sort();
    let (mut seen, mut disagree) = (std::collections::HashSet::new(), 0);
    for name in names {
        let Some(model) = lib.weapons[name].stats.world_model.clone() else { continue };
        if !seen.insert(model.to_ascii_lowercase()) {
            continue;
        }
        let Some(g) = lib.world_gun(name) else { continue };
        let mesh = (g.muzzle - g.grip).x.signum(); // now the flash tag where present
        let flash = lib.world_model(&model).and_then(|p| {
            let i = p.skel.bone_names.iter().position(|b| b.eq_ignore_ascii_case("tag_flash"))?;
            p.skel.bones.get(i).map(|b| b.trans[0])
        });
        let tag = flash.map(|f| (f - g.grip.x).signum());
        let bad = tag.is_some_and(|t| t != mesh);
        disagree += usize::from(bad);
        println!(
            "{}{name:28} {model:32} mesh {mesh:+} tag_flash {flash:?} grip {:?} muzzle {:?}",
            if bad { "!! " } else { "   " },
            g.grip,
            g.muzzle
        );
    }
    println!("{} models, {disagree} disagree", seen.len());
}
