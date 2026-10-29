//! The screens a built archive carries, as PNGs for a look by eye (a logo in a loading screen is
//! pixels, which the text gate cannot read): every `Interface\Glues\` and loading-screen texture,
//! at a small mip level.
//!
//! `cargo run --release --example brand_screens -- <patch-A.MPQ> <out dir>`

use std::path::PathBuf;

use anyhow::{Context, Result};

fn main() -> Result<()> {
    let mut a = std::env::args().skip(1);
    let archive = PathBuf::from(a.next().context("<archive>")?);
    let out = PathBuf::from(a.next().context("<out dir>")?);
    std::fs::create_dir_all(&out)?;
    let mpq = benilla_mpq::Archive::open(&archive)?;
    let list = mpq.read_file("(listfile)")?;
    let mut n = 0;
    for name in String::from_utf8_lossy(&list).lines() {
        let l = name.to_ascii_lowercase();
        let screen = l.starts_with("interface\\glues\\") || l.contains("loadingscreen") || l.contains("credits");
        if !screen || !l.ends_with(".blp") {
            continue;
        }
        let d = benilla_blp::decode(&mpq.read_file(name)?).with_context(|| name.to_string())?;
        let m = d.mips.iter().find(|m| m.width <= 512).unwrap_or(&d.mips[0]);
        let file = out.join(format!("{n:03}_{}.png", l.rsplit('\\').next().unwrap()));
        image::save_buffer(&file, &m.rgba, m.width, m.height, image::ExtendedColorType::Rgba8)?;
        println!("SCREEN {name} -> {}", file.display());
        n += 1;
    }
    eprintln!("{n} screens");
    Ok(())
}
