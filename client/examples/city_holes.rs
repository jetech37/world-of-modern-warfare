//! Try the city void-hole pass (`wotlk::holes`) on a real `Data` folder without building the patch
//! layer: per tile, the open hole cells, the cells it closes and the cells a WMO keeps, and
//! optionally a test archive of the closed tiles to mount for before/after captures.
//!
//! ```text
//! cargo run --release --example city_holes -- --data <1.12 Data> [--full] [--mpq <out.MPQ>]
//! ```
//!
//! By default the stock chain is read (what `wotlk_import` patches); `--full` reads every archive
//! (the tiles the game actually shows, patch-A's copies included).

use anyhow::{Context, Result};
use benilla_mw2::wotlk;

fn main() -> Result<()> {
    let mut args = std::env::args().skip(1);
    let mut data = std::env::var("WOW_DATA").ok().map(std::path::PathBuf::from);
    let mut full = false;
    let mut mpq = None;
    while let Some(a) = args.next() {
        match a.as_str() {
            "--data" => data = Some(args.next().context("--data <dir>")?.into()),
            "--full" => full = true,
            "--mpq" => mpq = Some(std::path::PathBuf::from(args.next().context("--mpq <file>")?)),
            other => anyhow::bail!("unknown argument {other}"),
        }
    }
    let data = data.context("set WOW_DATA or pass --data")?;
    let chain = if full {
        benilla_formats::Chain::open(&data)?
    } else {
        wotlk::stock_chain(&data)?
    };
    if let Ok(p) = std::env::var("CITY_ROOT") {
        let b = chain.read(&p)?;
        let mut o = 0;
        while o + 8 <= b.len() {
            let n = u32::from_le_bytes([b[o + 4], b[o + 5], b[o + 6], b[o + 7]]) as usize;
            println!("{} {n}", String::from_utf8_lossy(&[b[o + 3], b[o + 2], b[o + 1], b[o]]));
            o += 8 + n;
        }
        return Ok(());
    }
    if std::env::var("CITY_PLACES").is_ok() {
        // The real places to measure the airspace table by: flight masters, areas, city WMOs.
        let taxi = wotlk::dbc::Dbc::parse(&chain.read("DBFilesClient\\TaxiNodes.dbc")?)?;
        for r in &taxi.records {
            println!(
                "taxi {} map {} ({:.1}, {:.1}, {:.1}) {}",
                r[0],
                r[1],
                f32::from_bits(r[2]),
                f32::from_bits(r[3]),
                f32::from_bits(r[4]),
                taxi.string(r[5])
            );
        }
        let areas = wotlk::dbc::Dbc::parse(&chain.read("DBFilesClient\\AreaTable.dbc")?)?;
        for r in &areas.records {
            let name = areas.string(r[11]);
            let l = name.to_ascii_lowercase();
            if ["dalaran", "lordaeron", "stormwind", "ironforge", "orgrimmar", "undercity", "thunder bluff", "darnassus", "hyjal", "blackrock", "teldrassil", "airport", "harbor"]
                .iter()
                .any(|k| l.contains(k))
            {
                println!("area {} map {} zone {} {}", r[0], r[1], r[2], name);
            }
        }
        for (map, x, y) in wotlk::holes::city_tiles() {
            let Ok(raw) = chain.read(&wotlk::holes::tile_path(map, x, y)) else { continue };
            for w in wotlk::holes::wmos(&raw) {
                // The heading check: the group boxes turned into the world should fill the MODF box.
                if let Ok(root) = chain.read(&w.name) {
                    let g = wotlk::holes::group_volumes(&w, &root);
                    let u = g.iter().fold([f32::MAX, f32::MAX, f32::MIN, f32::MIN], |a, v| [a[0].min(v[0]), a[1].min(v[1]), a[2].max(v[3]), a[3].max(v[4])]);
                    println!("  groups {} union x {:.0}..{:.0} y {:.0}..{:.0}", g.len(), u[0], u[2], u[1], u[3]);
                }
                println!(
                    "wmo map {map} tile {x}_{y} ({:.1}, {:.1}, {:.1}) heading {:.0} box x {:.0}..{:.0} y {:.0}..{:.0} z {:.0}..{:.0} {}",
                    w.pos[0], w.pos[1], w.pos[2], w.heading, w.bbox[0], w.bbox[2], w.bbox[1], w.bbox[3], w.zs[0], w.zs[1], w.name
                );
            }
        }
        return Ok(());
    }
    let mut files = Vec::new();
    let (mut open, mut closed) = (0, 0);
    for (map, x, y) in wotlk::holes::city_tiles() {
        let name = wotlk::holes::tile_path(map, x, y);
        let Ok(raw) = chain.read(&name) else {
            continue;
        };
        let around = wotlk::holes::volumes_around(map, x, y, &|p| chain.read(p).ok());
        let s = wotlk::holes::survey(map, &raw, &around);
        open += s.open;
        closed += s.closed.len();
        println!(
            "{name}: {} open hole cells in the city circles, {} close, {} kept under a WMO",
            s.open,
            s.closed.len(),
            s.kept
        );
        for c in &s.closed {
            println!("    close cell at world ({:.0}, {:.0}), ground z {:.0}", c[0], c[1], c[2]);
        }
        if let Some((bytes, _)) = wotlk::holes::close_tile(map, &raw, &around) {
            files.push((name, bytes));
        }
    }
    println!("{open} open, {closed} close, {} tiles change", files.len());
    if let Some(p) = mpq {
        std::fs::write(&p, wotlk::mpq_write::write_mpq(&files))?;
        println!("wrote {}", p.display());
    }
    Ok(())
}
