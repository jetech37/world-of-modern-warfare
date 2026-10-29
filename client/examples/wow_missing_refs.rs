//! Every file a map's terrain and buildings name that no archive in the chain ships: per map id,
//! each ADT tile's textures (MTEX), models (MMDX) and buildings (MWMO), and each building root's
//! textures (MOTX) and props (MODN). Names only.
//! `WOW_DATA=... cargo run --release --example wow_missing_refs -- [--hd <hd dir>] [--strict] 0 1 26 27`
//!
//! `--hd` mounts the HD pack as the client does (`benilla_app::hd_pack`: its manifest's archives
//! under our built layers), so an HD-only gap shows; `--strict` exits 1 when anything is missing
//! (the verify gate after a rebuild).
use std::collections::{BTreeMap, BTreeSet};

/// The top-level chunks of a chunked file: (magic as read, reversed to text order, payload).
fn chunks(b: &[u8]) -> Vec<(String, &[u8])> {
    let mut out = Vec::new();
    let mut o = 0;
    while o + 8 <= b.len() {
        let magic: String = b[o..o + 4].iter().rev().map(|&c| c as char).collect();
        let size = u32::from_le_bytes(b[o + 4..o + 8].try_into().unwrap()) as usize;
        let end = (o + 8 + size).min(b.len());
        out.push((magic, &b[o + 8..end]));
        o = o + 8 + size;
    }
    out
}

fn names(payload: &[u8]) -> Vec<String> {
    payload
        .split(|&c| c == 0)
        .filter(|s| !s.is_empty())
        .map(|s| String::from_utf8_lossy(s).to_string())
        .collect()
}

/// A model reference as its archive file (`.mdx`/`.mdl` are `.m2` in the archives).
fn m2(name: &str) -> String {
    let lower = name.to_ascii_lowercase();
    for ext in [".mdx", ".mdl"] {
        if lower.ends_with(ext) {
            return format!("{}.m2", &name[..name.len() - ext.len()]);
        }
    }
    name.to_string()
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let strict = args.iter().any(|a| a == "--strict");
    let hd = args
        .iter()
        .position(|a| a == "--hd")
        .and_then(|i| args.get(i + 1))
        .map(std::path::PathBuf::from);
    let data = benilla_formats::wow_data().expect("WOW_DATA");
    let mut chain = match &hd {
        Some(dir) => {
            let archives = benilla_app::hd_pack::read_manifest(dir).expect("an HD pack manifest");
            println!("HD pack: {} archives from {}", archives.len(), dir.display());
            benilla_formats::Chain::open_with_inserted(
                &data,
                &|_| true,
                &archives,
                &benilla_app::hd_pack::is_built_layer,
            )
            .expect("chain with the HD pack")
        }
        None => benilla_formats::open_chain(&data).expect("chain"),
    };
    let maps = benilla_formats::load_map_catalog(&mut chain).expect("Map.dbc");
    // missing file -> the first map that named it, and how many references
    let mut missing: BTreeMap<String, (u32, usize)> = BTreeMap::new();
    let mut wmos_seen = BTreeSet::new();
    for id in args.iter().filter_map(|a| a.parse::<u32>().ok()) {
        let Some(dir) = maps.directory(id).map(str::to_owned) else {
            println!("map {id}: not in Map.dbc");
            continue;
        };
        let (mut tiles, mut refs) = (0, 0);
        let mut check = |file: String, chain: &mut benilla_formats::Chain, refs: &mut usize| {
            *refs += 1;
            if !chain.contains(&file) {
                missing.entry(file).or_insert((id, 0)).1 += 1;
            }
        };
        let mut wmos = Vec::new();
        for x in 0..64 {
            for y in 0..64 {
                let adt = format!(r"World\Maps\{dir}\{dir}_{x}_{y}.adt");
                let Ok(b) = chain.read_file(&adt) else { continue };
                tiles += 1;
                for (magic, p) in chunks(&b) {
                    match magic.as_str() {
                        "MTEX" => names(p).into_iter().for_each(|n| check(n, &mut chain, &mut refs)),
                        "MMDX" => names(p).into_iter().for_each(|n| check(m2(&n), &mut chain, &mut refs)),
                        "MWMO" => {
                            for n in names(p) {
                                check(n.clone(), &mut chain, &mut refs);
                                wmos.push(n);
                            }
                        }
                        _ => {}
                    }
                }
            }
        }
        for w in wmos {
            if !wmos_seen.insert(w.to_ascii_lowercase()) {
                continue;
            }
            let Ok(b) = chain.read_file(&w) else { continue };
            for (magic, p) in chunks(&b) {
                match magic.as_str() {
                    "MOTX" => names(p).into_iter().for_each(|n| check(n, &mut chain, &mut refs)),
                    "MODN" => names(p).into_iter().for_each(|n| check(m2(&n), &mut chain, &mut refs)),
                    _ => {}
                }
            }
        }
        println!("map {id} ({dir}): {tiles} tiles, {refs} references");
    }
    println!("{} missing files:", missing.len());
    for (f, (map, n)) in &missing {
        println!("  map {map:4} x{n:<4} {f}");
    }
    if strict && !missing.is_empty() {
        std::process::exit(1);
    }
}
