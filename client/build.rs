//! Stamps the commit this binary was built from, as benilla's own launcher does, and finds the
//! data that is made from the maintainer's own game installs, which the public source tree does
//! not carry (`private-assets.md` at the repository root):
//!
//! - the fusion catalog (`assets/fusion_catalog.json`, MW2's own weapon numbers): required. A
//!   checkout without it fails here with how to make one.
//! - the first-person hand tracks (`assets/hand_anims/*.json`): optional. Without them the
//!   hands use the hand-made keyframes (`crate::melee_pose`), and the tests that need the tracks
//!   are ignored (`cfg(aw_hand_anim_tracks)` is off).
//!
//! Each is looked for in the client's own `assets/`, then in `<repo>/private-assets/client/assets/`
//! (or `$AW_PRIVATE_ASSETS/client/assets/`).

use std::env;
use std::fmt::Write as _;
use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    benilla_buildstamp::emit();

    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let repo = manifest.parent().unwrap_or(&manifest).to_path_buf();
    let private = env::var_os("AW_PRIVATE_ASSETS")
        .map(PathBuf::from)
        .unwrap_or_else(|| repo.join("private-assets"));
    println!("cargo:rerun-if-env-changed=AW_PRIVATE_ASSETS");
    let own = manifest.join("assets");
    let kept = private.join("client").join("assets");
    for dir in [&own, &kept] {
        if dir.is_dir() {
            println!("cargo:rerun-if-changed={}", dir.display());
        }
    }

    // The fusion catalog.
    let catalog = [
        own.join("fusion_catalog.json"),
        kept.join("fusion_catalog.json"),
    ]
    .into_iter()
    .find(|p| p.is_file());
    match catalog {
        Some(path) => println!("cargo:rustc-env=AW_FUSION_CATALOG={}", path.display()),
        None => panic!(
            "\n\nNo fusion catalog (client/assets/fusion_catalog.json or \
             private-assets/client/assets/fusion_catalog.json).\n\
             It holds numbers read from an MW2 install, so the public tree doesn't carry one. \
             Make it (see private-assets.md):\n\
             - from your own MW2 install (the full catalog): \
             cargo run --release --example mw2_export > weapons.tsv, then \
             cargo run --release --example mw2_export -- --models > models.txt, then \
             python server/contrib/fusion/make_seed_catalog.py server/contrib/fusion/weapons.json \
             client/assets/fusion_catalog.json weapons.tsv models.txt\n\
             - or without an install (guns only, approximate numbers): \
             python server/contrib/fusion/make_seed_catalog.py server/contrib/fusion/weapons.json \
             client/assets/fusion_catalog.json\n\
             (the examples need a catalog to build: make the seed one first.)\n"
        ),
    }

    // The hand tracks: every <name>.json of the first folder that has any.
    let tracks = [own.join("hand_anims"), kept.join("hand_anims")]
        .into_iter()
        .map(|dir| json_files(&dir))
        .find(|files| !files.is_empty())
        .unwrap_or_default();
    println!("cargo::rustc-check-cfg=cfg(aw_hand_anim_tracks)");
    if !tracks.is_empty() {
        println!("cargo:rustc-cfg=aw_hand_anim_tracks");
    }
    let mut code = String::from(
        "/// An embedded track's JSON by name (`None`: not embedded in this build).\n\
         fn embedded_source(name: &str) -> Option<&'static str> {\n    match name {\n",
    );
    for (name, path) in &tracks {
        let _ = writeln!(
            code,
            "        {name:?} => Some(include_str!({:?})),",
            path.display().to_string()
        );
    }
    code.push_str("        _ => None,\n    }\n}\n");
    let out = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR")).join("hand_anim_tracks.rs");
    fs::write(&out, code).expect("writing hand_anim_tracks.rs");

    // The arms' cut-edge test fixtures (`src/mw2/arm_cut_tests.rs`: poses measured from the game
    // installs by `examples/wow_arm_cut.rs`), which the public tree doesn't carry either.
    let fixtures = manifest.join("src").join("mw2").join("arm_cut_tests.rs");
    println!("cargo::rustc-check-cfg=cfg(aw_arm_cut_fixtures)");
    println!("cargo:rerun-if-changed={}", fixtures.parent().unwrap_or(&manifest).display());
    if fixtures.is_file() {
        println!("cargo:rustc-cfg=aw_arm_cut_fixtures");
    }
}

/// The `.json` files of `dir` as (file stem, path), sorted; none when there is no such folder.
fn json_files(dir: &Path) -> Vec<(String, PathBuf)> {
    let mut out: Vec<(String, PathBuf)> = fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|x| x == "json"))
        .filter_map(|p| Some((p.file_stem()?.to_str()?.to_string(), p)))
        .collect();
    out.sort();
    out
}
