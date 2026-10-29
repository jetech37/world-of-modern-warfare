//! Which `AnimationData` ids a WoW model has: `WOW_DATA=... cargo run --release --example
//! wow_anim_ids -- Character\Tauren\Male\TaurenMale.m2 [id ...]`
fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next().expect("model path");
    let want: Vec<u16> = args.filter_map(|a| a.parse().ok()).collect();
    let data = benilla_formats::wow_data().expect("WOW_DATA");
    let mut chain = benilla_formats::open_chain(&data).expect("chain");
    let bytes = chain.read_file(&path).expect("model");
    let anims = benilla_formats::parse_m2_animations(&bytes);
    for a in anims.iter().filter(|a| want.contains(&a.anim_id)) {
        println!("{} secs {:.2} looping {}", a.anim_id, a.duration, a.looping);
    }
    let mut ids: Vec<u16> = anims
        .iter()
        .map(|a| a.anim_id)
        .collect();
    ids.sort();
    ids.dedup();
    if want.is_empty() {
        println!("{ids:?}");
    }
    for w in want {
        println!("{w}: {}", ids.contains(&w));
    }
}
