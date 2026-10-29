//! Which `Data` archives hold a file: `WOW_DATA=... cargo run --release --example
//! wow_which_archive -- Creature\Gryphon\Gryphon.m2 ...`
fn main() {
    let data = benilla_formats::wow_data().expect("WOW_DATA");
    let mut archives: Vec<_> = std::fs::read_dir(&data)
        .expect("Data")
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.extension().is_some_and(|x| x.eq_ignore_ascii_case("mpq")))
        .collect();
    archives.sort();
    for name in std::env::args().skip(1) {
        let mut hits = Vec::new();
        for a in &archives {
            if let Ok(arc) = benilla_mpq::Archive::open(a) {
                if let Ok(bytes) = arc.read_file(&name) {
                    hits.push(format!("{} ({} bytes)", a.file_name().unwrap().to_string_lossy(), bytes.len()));
                }
            }
        }
        println!("{name}: {hits:?}");
    }
}
