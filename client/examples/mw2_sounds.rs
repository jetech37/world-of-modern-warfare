//! Lists MW2's sound aliases matching a filter, with how each variant is stored:
//! `MW2_PATH=... cargo run --release --example mw2_sounds -- spas12 step_ gear_`

use asset_audio::{AssetNamespace, load_sound_catalog};
use asset_transport::find_zone_file;

fn main() {
    let filters: Vec<String> = std::env::args().skip(1).collect();
    let root = benilla_mw2::mw2::load::games_root().expect("MW2_PATH");
    let mut catalog = asset_audio::SoundCatalog::default();
    for zone in ["common_mp", "localized_common_mp", "patch_mp"] {
        match find_zone_file(&root, zone)
            .map_err(|e| e.to_string())
            .and_then(|f| load_sound_catalog(&f.path))
        {
            Ok(c) => catalog.absorb(c),
            Err(e) => eprintln!("{zone}: {e}"),
        }
    }
    println!(
        "{} aliases, {} loaded sounds",
        catalog.sounds.len(),
        catalog.loaded.len()
    );
    let mut rng = 1u32;
    for sound in &catalog.sounds {
        if !filters.is_empty() && !filters.iter().any(|f| sound.name.contains(f.as_str())) {
            continue;
        }
        let picked = catalog
            .pick_loaded_outcome(AssetNamespace::Iw4, &sound.name, &mut rng, None)
            .and_then(|o| o.picked);
        let pcm = picked.map(|p| {
            format!(
                "fmt {} {}Hz {}ch {}bit {} bytes",
                p.sound.format(),
                p.sound.rate,
                p.sound.channels(),
                p.sound.bits(),
                p.sound.encoded_bytes().len()
            )
        });
        let kinds: Vec<_> = sound
            .aliases
            .iter()
            .map(|a| {
                format!(
                    "{}:{}",
                    a.file_kind(),
                    a.loaded_name
                        .as_deref()
                        .or(a.file_name.as_deref())
                        .unwrap_or("-")
                )
            })
            .collect();
        println!(
            "{} [{}] {}",
            sound.name,
            kinds.join(" "),
            pcm.unwrap_or_else(|| "no loaded pick".into())
        );
    }
}
