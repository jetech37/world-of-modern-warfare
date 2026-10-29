//! Archive names containing every given substring (case-insensitive), from each `Data` archive's
//! `(listfile)`: `WOW_DATA=... cargo run --release --example wow_find -- darkshore sand`
fn main() {
    let data = benilla_formats::wow_data().expect("WOW_DATA");
    let words: Vec<String> = std::env::args().skip(1).map(|a| a.to_ascii_lowercase()).collect();
    let mut found = std::collections::BTreeSet::new();
    for e in std::fs::read_dir(&data).expect("Data").flatten() {
        let p = e.path();
        if !p.extension().is_some_and(|x| x.eq_ignore_ascii_case("mpq")) {
            continue;
        }
        let Ok(a) = benilla_mpq::Archive::open(&p) else { continue };
        let Ok(list) = a.read_file("(listfile)") else { continue };
        for n in String::from_utf8_lossy(&list).lines() {
            let l = n.to_ascii_lowercase();
            if words.iter().all(|w| l.contains(w.as_str())) {
                found.insert(n.trim().to_string());
            }
        }
    }
    for f in found {
        println!("{f}");
    }
}
