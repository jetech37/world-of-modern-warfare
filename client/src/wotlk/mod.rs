//! Later-version WoW art for World of Modern Warfare: models read from the owner's own WotLK 3.3.5a
//! (build 12340) install, converted to the 1.12 formats benilla renders, and packed with the DBC
//! rows that name them into one patch archive, [`PATCH_ARCHIVE`], that mounts above the 1.12
//! chain. Nothing here, or made here, is ever committed: the archive is built into the gitignored
//! `private-assets/womw-patch/` and copied into the packaged game data (`package/build.ps1`).
//!
//! - [`source`]: the 3.3.5 archives, read with benilla's own MPQ reader (their files are V1,
//!   zlib-sectored with sector CRCs, which it reads).
//! - [`m2conv`]: the WotLK M2 (264) to 1.12 M2 (256) down-port.
//! - [`dbc`], [`mpq_write`]: the table and archive writers.
//! - [`import`]: what is imported, under which ids, and the build of the archive.
//! - [`dalaran`]: Wrath's Dalaran in place of classic Dalaran's dome (Alterac, map 0); [`adt`]
//!   edits the 1.12 terrain tiles for it.

pub mod adt;
pub mod dalaran;
pub mod dbc;
pub mod holes;
pub mod import;
pub mod m2conv;
pub mod mpq_write;
pub mod source;

/// The patch layer's archive name: one `patch-?.MPQ` character, so the stock chain mounts it, and
/// `9` sorts after every stock 1.12 patch (`patch.MPQ`, `patch-2.MPQ`, `patch-3.MPQ`), so it wins.
/// A digit, not a letter: the server's vmap extractor scans `patch-2` to `patch-99` only, so it
/// reads the patched Alterac tiles from this archive too.
pub const PATCH_ARCHIVE: &str = "patch-9.MPQ";

/// Whether a `Data` archive is one of the layers built over the stock chain: ours,
/// [`PATCH_ARCHIVE`], and the community's `patch-A` (any `patch-?` past the stock `patch-3`).
pub fn is_built_layer(archive: &str) -> bool {
    let n = archive.to_ascii_lowercase();
    n.strip_prefix("patch-")
        .and_then(|r| r.strip_suffix(".mpq"))
        .is_some_and(|c| c.len() == 1 && c > "3")
}

/// The 1.12 chain of `data` as the stock client mounts it, without the built layers
/// ([`is_built_layer`]): the patch layer is always built from stock, never from its own last
/// build (a packaged `Data` folder holds `patch-9` and `patch-A` too; read through them, the
/// Alterac tiles would be reshaped twice and every converted model would look already shipped).
pub fn stock_chain(data: &std::path::Path) -> anyhow::Result<benilla_formats::Chain> {
    benilla_formats::Chain::open_filtered(data, &|n| !is_built_layer(n))
}

/// The 1.12 `Data` folder the tests read: `WOW_DATA`, or the packaged game beside the checkout.
#[cfg(test)]
pub(crate) fn vanilla_install() -> Option<std::path::PathBuf> {
    if let Some(d) = std::env::var_os("WOW_DATA").map(std::path::PathBuf::from) {
        return d.is_dir().then_some(d);
    }
    std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .map(|a| a.join("dist/AzerothWarfare/game/WoW/Data"))
        .find(|d| d.is_dir())
}
