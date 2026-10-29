//! Creature displays whose texture variations (the model's replaceable skin slots) name a file no
//! archive ships: before benilla's fallback (entities::display `fallback_skin`) each drew
//! invisible. Counts and names only. `WOW_DATA=... cargo run --release --example
//! wow_missing_textures`
fn main() {
    let data = benilla_formats::wow_data().expect("WOW_DATA");
    let mut chain = benilla_formats::open_chain(&data).expect("chain");
    let catalog = benilla_formats::load_creature_catalog(&mut chain).expect("catalog");
    if let Some(d) = std::env::args().nth(1).and_then(|a| a.parse::<u32>().ok()) {
        let m = catalog.model(d);
        println!("{d}: {:?}", m.map(|m| (m.model_path, m.textures)));
        return;
    }
    let sep = std::path::MAIN_SEPARATOR.to_string();
    let bs = if sep == "\\" { sep } else { "\\".to_string() };
    let mut ids: Vec<u32> = catalog.display_models().map(|(d, _)| d).collect();
    ids.sort();
    let (mut checked, mut broken) = (0, Vec::new());
    for d in ids {
        let Some(m) = catalog.model(d) else { continue };
        let dir = m
            .model_path
            .rsplit_once(bs.as_str())
            .map_or(String::new(), |(d, _)| d.to_string());
        let mut missing = Vec::new();
        for name in m.textures.iter().flatten() {
            checked += 1;
            let file = if dir.is_empty() {
                format!("{name}.blp")
            } else {
                format!("{dir}{bs}{name}.blp")
            };
            if !chain.contains(&file) {
                missing.push(file);
            }
        }
        if !missing.is_empty() {
            broken.push((d, m.model_path.clone(), missing));
        }
    }
    println!(
        "{checked} display texture variations checked, {} displays name a missing one",
        broken.len()
    );
    for (d, m, t) in &broken {
        println!("{d} {m}: {t:?}");
    }
}
