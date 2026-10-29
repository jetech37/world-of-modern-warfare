//! `community-merge survey|areas|build|verify`; the steps are in `design/COMMUNITY_MERGE.md`.
//!
//! - survey: what the community patches change on Azeroth and Kalimdor, per tile, zone and format.
//! - areas: the chosen zones' tiles and their chunk-to-area table (the server import's input).
//! - build: the patch archive, the server's tables and a report.
//! - verify: the built archive read back the way the game mounts it.
//! - hd: the optional HD pack from the community client's HD archives (`design/HD_PACK.md`).
//! - brand-scan: the branding gate on its own over files, folders and archives (`brand`).

use std::collections::BTreeMap;
use std::path::PathBuf;

use anyhow::{bail, Context, Result};
use community_merge::dbc::Dbc;
use community_merge::source::{Layer, Ours};
use community_merge::tiles::{self, TileKind};

struct Args {
    community: PathBuf,
    ours: PathBuf,
    above: Vec<PathBuf>,
    rest: Vec<String>,
}

fn args() -> Result<(String, Args)> {
    let mut it = std::env::args().skip(1);
    let cmd = it.next().context(
        "usage: community-merge survey|build --community <Data> --ours <Data> [--above <mpq>]...",
    )?;
    let (mut community, mut ours, mut above, mut rest) = (None, None, Vec::new(), Vec::new());
    while let Some(a) = it.next() {
        match a.as_str() {
            "--community" => community = it.next().map(PathBuf::from),
            "--ours" => ours = it.next().map(PathBuf::from),
            "--above" => above.extend(it.next().map(PathBuf::from)),
            _ => rest.push(a),
        }
    }
    Ok((
        cmd,
        Args {
            community: community.unwrap_or_default(),
            ours: ours.context("--ours <our Data folder>")?,
            above,
            rest,
        },
    ))
}

fn main() -> Result<()> {
    if std::env::args().nth(1).as_deref() == Some("hd") {
        let rest: Vec<String> = std::env::args().skip(2).collect();
        return community_merge::hd::run(&rest);
    }
    // `brand-scan <file|folder|archive>...`: the branding gate on its own (the SQL, the HD pack's
    // survey and archives, a built folder). Fails when anything is left.
    if std::env::args().nth(1).as_deref() == Some("brand-scan") {
        let args: Vec<String> = std::env::args().skip(2).collect();
        let brand = community_merge::brand::Rules::find(&args)?;
        let mut paths: Vec<PathBuf> = Vec::new();
        let mut it = args.iter();
        while let Some(p) = it.next() {
            if p == "--brand-rules" {
                it.next();
            } else {
                paths.push(PathBuf::from(p));
            }
        }
        if paths.is_empty() {
            bail!("usage: community-merge brand-scan [--brand-rules <file>] <file|folder|archive>...");
        }
        let hits = brand.scan_paths(&paths)?;
        for h in &hits {
            println!("BRANDING LEFT: {h}");
        }
        if !hits.is_empty() {
            bail!("{} branded files or strings left", hits.len());
        }
        println!("brand gate: clean ({} paths)", paths.len());
        return Ok(());
    }
    let (cmd, a) = args()?;
    if cmd == "verify" {
        return verify(&a);
    }
    let community = Layer::open_community(&a.community)?;
    let ours = Ours::open(&a.ours, &a.above)?;
    match cmd.as_str() {
        "survey" => survey(&community, &ours),
        "build" => community_merge::build::run(&community, &ours, &a.rest),
        "areas" => community_merge::build::areas(&community, &ours, &a.rest),
        _ => bail!("unknown command {cmd}"),
    }
}

/// `verify --archive <patch-A.MPQ>`: our side with the built archive mounted on top, read the way
/// the game reads it: every tile it carries meshes (terrain and liquid) through benilla, the WDTs
/// flag them, and every file it lists reads back.
fn verify(a: &Args) -> Result<()> {
    let brand = community_merge::brand::Rules::find(&a.rest)?;
    let mut above = a.above.clone();
    let archive = a
        .rest
        .iter()
        .position(|r| r == "--archive")
        .and_then(|i| a.rest.get(i + 1))
        .map(PathBuf::from)
        .context("--archive <patch-A.MPQ>")?;
    above.push(archive.clone());
    let game = Ours::open(&a.ours, &above)?;
    let list = benilla_mpq::Archive::open(&archive)?.read_file("(listfile)")?;
    let names: Vec<String> = String::from_utf8_lossy(&list)
        .lines()
        .map(str::to_string)
        .collect();
    let (mut tiles_ok, mut wet, mut files) = (0, 0, 0);
    for n in &names {
        let b = game.read(n).with_context(|| format!("reading {n}"))?;
        files += 1;
        if n.to_ascii_lowercase().ends_with(".adt") {
            let mesh =
                benilla_formats::adt_to_tile_mesh(&b).with_context(|| format!("meshing {n}"))?;
            wet += mesh.chunks.iter().filter(|c| !c.liquids.is_empty()).count();
            tiles_ok += 1;
            // The tile's WDT says it exists.
            let parts: Vec<&str> = n
                .rsplit('\\')
                .next()
                .unwrap()
                .trim_end_matches(".adt")
                .split('_')
                .collect();
            let (x, y): (usize, usize) = (parts[1].parse()?, parts[2].parse()?);
            let wdt = format!("World\\Maps\\{0}\\{0}.wdt", parts[0]);
            let w = benilla_wdt::WdtReader::new(
                std::io::Cursor::new(game.read(&wdt)?),
                benilla_wdt::WowVersion::Classic,
            )
            .read()?;
            // Their instanced maps keep their own WDTs (some ship tiles their WDT never loads).
            let ours_wdt = parts[0].eq_ignore_ascii_case("Azeroth")
                || parts[0].eq_ignore_ascii_case("Kalimdor");
            if ours_wdt && !w.get_tile(x, y).is_some_and(|t| t.has_adt) {
                bail!("{wdt} does not flag tile {x}_{y}");
            }
        }
    }
    println!("verified {files} files: {tiles_ok} tiles mesh through benilla ({wet} wet chunks), WDTs flag them all");
    let hits = brand.scan_archive(&archive)?;
    for h in &hits {
        println!("BRANDING LEFT: {h}");
    }
    if !hits.is_empty() {
        bail!("{} branded files or strings left in {}", hits.len(), archive.display());
    }
    println!("brand gate: clean");
    Ok(())
}

fn survey(community: &Layer, ours: &Ours) -> Result<()> {
    let areas = Dbc::parse(&community.read("DBFilesClient\\AreaTable.dbc")?)?;
    let stock_areas = Dbc::parse(&ours.read("DBFilesClient\\AreaTable.dbc")?)?;
    let stock_ids = stock_areas.ids();
    println!("AreaTable: {} fields", areas.field_count);
    let zone_of = |id: u32| -> u32 {
        match areas.row(id) {
            Some(r) if r[2] != 0 => r[2],
            _ => id,
        }
    };
    let name = |id: u32| {
        areas
            .row(id)
            .map(|r| areas.string(r[11]).to_string())
            .unwrap_or_default()
    };

    for c in tiles::CONTINENTS {
        let wdt = tiles::wdt_path(c);
        for (side, bytes) in [
            ("community", community.read(&wdt).ok()),
            ("ours", ours.read(&wdt).ok()),
        ] {
            if let Some(b) = bytes {
                let f = benilla_bytes::chunks(&b)
                    .find(|(m, _)| m == b"DHPM")
                    .and_then(|(_, d)| benilla_bytes::ByteExt::u32_at(d, 0));
                println!("{wdt} ({side}): MPHD flags {f:?}");
            }
        }
    }

    let all = tiles::community_tiles(community, ours)?;
    for t in &all {
        let zs: Vec<String> = t
            .areas
            .iter()
            .map(|(a, n)| format!("{}:{a}x{n}", zone_of(*a)))
            .collect();
        println!(
            "tile {} {:>2} {:>2} {:?}{}{} {}",
            t.continent.map,
            t.x,
            t.y,
            t.kind,
            if t.has_mh2o { " MH2O" } else { "" },
            if t.has_mccv { " MCCV" } else { "" },
            zs.join(" ")
        );
    }
    let mut zones: BTreeMap<(u32, u32), [u32; 3]> = BTreeMap::new();
    for t in &all {
        let k = match t.kind {
            TileKind::New => 0,
            TileKind::Changed => 1,
            TileKind::Same => 2,
        };
        let mut zs: Vec<u32> = t.areas.keys().map(|&a| zone_of(a)).collect();
        zs.sort();
        zs.dedup();
        for z in zs {
            zones.entry((t.continent.map, z)).or_default()[k] += 1;
        }
    }
    for c in tiles::CONTINENTS {
        let ts: Vec<_> = all.iter().filter(|t| t.continent == c).collect();
        let count = |k: TileKind| ts.iter().filter(|t| t.kind == k).count();
        println!(
            "{}: {} tiles: {} new, {} changed, {} same; ours win {}; MH2O {}, MCCV {}, big alpha {}",
            c.dir,
            ts.len(),
            count(TileKind::New),
            count(TileKind::Changed),
            count(TileKind::Same),
            ts.iter().filter(|t| t.ours_wins && t.kind != TileKind::Same).count(),
            ts.iter().filter(|t| t.has_mh2o).count(),
            ts.iter().filter(|t| t.has_mccv).count(),
            ts.iter().filter(|t| t.big_alpha).count(),
        );
    }
    println!("zone (map, id, new/stock area) : tiles new / changed / same");
    for ((m, z), [n, ch, s]) in &zones {
        println!(
            "  {m} {z:>5} {:<5} {:<32} {n:>3} {ch:>3} {s:>3}",
            if stock_ids.contains(z) {
                "stock"
            } else {
                "new"
            },
            name(*z)
        );
    }
    // MH2O instance types and MHDR offsets, for the server extractor.
    let mut kinds: BTreeMap<(u16, u16), u32> = BTreeMap::new();
    let mut mhdr_points = 0;
    let mut mh2o_tiles = 0;
    for t in all.iter().filter(|t| t.has_mh2o) {
        mh2o_tiles += 1;
        let b = community.read(&t.path())?;
        for (m, d) in benilla_bytes::chunks(&b) {
            if &m == b"RDHM" && benilla_bytes::ByteExt::u32_at(d, 0x28).unwrap_or(0) != 0 {
                mhdr_points += 1;
            }
            if &m == b"O2HM" {
                for i in 0..256 {
                    let ofs = benilla_bytes::ByteExt::u32_at(d, i * 12).unwrap_or(0) as usize;
                    let n = benilla_bytes::ByteExt::u32_at(d, i * 12 + 4).unwrap_or(0) as usize;
                    for k in 0..n.min(8) {
                        if let (Some(ty), Some(f)) = (
                            benilla_bytes::ByteExt::u16_at(d, ofs + k * 24),
                            benilla_bytes::ByteExt::u16_at(d, ofs + k * 24 + 2),
                        ) {
                            *kinds.entry((ty, f)).or_default() += 1;
                        }
                    }
                }
            }
        }
    }
    println!(
        "MH2O tiles {mh2o_tiles}, MHDR.offsMH2O set in {mhdr_points}; (type, format): {kinds:?}"
    );
    Ok(())
}
