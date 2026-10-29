//! Build the World of Modern Warfare patch layer from the owner's WotLK 3.3.5a install.
//!
//! ```text
//! cargo run --release --example wotlk_import -- [--wotlk <3.3.5 Data>] [--vanilla <1.12 Data>] [--out <dir>] [--no-dalaran] [--no-holes]
//! ```
//!
//! Defaults: `WOTLK_DATA` (or the maintainer's install), `WOW_DATA`, and the repository's
//! gitignored `private-assets/womw-patch`. It writes:
//!
//! - `<out>/patch-9.MPQ`: the converted models, their textures, Wrath's Dalaran over the Alterac crater (patched Azeroth tiles) and the 1.12 DBCs
//!   with the new rows. `package/build.ps1` copies it into the packaged `game\WoW\Data`.
//! - `<out>/server-dbc/*.dbc`: the tables the server reads (`data\5875\dbc`).
//! - `<out>/report.txt`: what was imported, each display's size, what is missing.
//!
//! Never commit anything it writes: the files are Blizzard's art (private-assets.md).

use std::path::PathBuf;

use anyhow::{Context, Result};
use benilla_mw2::wotlk;

fn main() -> Result<()> {
    let mut args = std::env::args().skip(1);
    let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..");
    let mut wotlk_dir = std::env::var("WOTLK_DATA")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from(wotlk::source::DEFAULT_DATA));
    let mut vanilla = std::env::var("WOW_DATA").map(PathBuf::from).ok();
    let mut out = repo.join("private-assets").join("womw-patch");
    let mut dalaran = true;
    let mut holes = true;
    while let Some(a) = args.next() {
        match a.as_str() {
            "--wotlk" => wotlk_dir = args.next().context("--wotlk <dir>")?.into(),
            "--vanilla" => vanilla = Some(args.next().context("--vanilla <dir>")?.into()),
            "--out" => out = args.next().context("--out <dir>")?.into(),
            "--no-dalaran" => dalaran = false,
            "--no-holes" => holes = false,
            other => anyhow::bail!("unknown argument {other}"),
        }
    }
    let vanilla = vanilla.context("set WOW_DATA or pass --vanilla <1.12 Data folder>")?;
    let src = wotlk::source::Source::open(&wotlk_dir, "enUS")?;
    let mut chain = wotlk::stock_chain(&vanilla)?;

    let mut built = wotlk::import::build(&src, &mut chain)?;
    if dalaran {
        let d = wotlk::dalaran::build(&src, &mut chain)?;
        built.report.extend(d.report);
        for (name, bytes) in d.files {
            if let Some(slot) = built.files.iter_mut().find(|(n, _)| n.eq_ignore_ascii_case(&name)) {
                slot.1 = bytes;
            } else {
                built.files.push((name, bytes));
            }
        }
    }

    // The void holes round the warded cities, closed (over Dalaran's tiles where they meet).
    if holes {
        for (map, x, y) in wotlk::holes::city_tiles() {
            let name = wotlk::holes::tile_path(map, x, y);
            let raw = match built.files.iter().find(|(n, _)| n.eq_ignore_ascii_case(&name)) {
                Some((_, b)) => b.clone(),
                None => match chain.read(&name) {
                    Ok(b) => b,
                    Err(_) => continue,
                },
            };
            let around = wotlk::holes::volumes_around(map, x, y, &|p| chain.read(p).ok());
            if let Some((bytes, n)) = wotlk::holes::close_tile(map, &raw, &around) {
                built.report.push(format!("  {name}: {n} void hole cells closed"));
                if let Some(slot) = built.files.iter_mut().find(|(f, _)| f.eq_ignore_ascii_case(&name)) {
                    slot.1 = bytes;
                } else {
                    built.files.push((name, bytes));
                }
            }
        }
    }

    std::fs::create_dir_all(out.join("server-dbc"))?;
    let archive = wotlk::mpq_write::write_mpq(&built.files);
    let path = out.join(wotlk::PATCH_ARCHIVE);
    std::fs::write(&path, &archive).with_context(|| format!("writing {}", path.display()))?;
    for (name, bytes) in &built.tables {
        let file = name.rsplit('\\').next().unwrap_or(name);
        std::fs::write(out.join("server-dbc").join(file), bytes)?;
    }
    let mut report = built.report.join("\n");
    report.push_str(&format!(
        "\n\n{} files, {:.1} MB in {}\n",
        built.files.len(),
        archive.len() as f64 / 1e6,
        path.display()
    ));
    std::fs::write(out.join("report.txt"), &report)?;
    println!("{report}");
    Ok(())
}
