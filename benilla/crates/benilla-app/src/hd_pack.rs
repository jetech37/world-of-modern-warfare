//! World of Modern Warfare's optional HD pack: higher-resolution models, textures and sounds of
//! stock files, in archives of their own (`womw-hd-N.MPQ`, named by the manifest [`MANIFEST`]) in
//! an `hd` folder beside the game, never in `Data`. Deviation: 1.12 mounts only its `Data`
//! folder; the pack mounts above the stock archives and below the layers built over them
//! ([`is_built_layer`]: our WotLK `patch-9` and community `patch-A`), so those always win.
//!
//! The switch is the CVar [`CVAR`] (Options > Graphics, saved with the others), read once at boot
//! because the `mpq://` source is built before the app runs: a change applies at the next start.
//! [`INSTALLED_CVAR`] tells the options window whether a pack was found this session. Without a
//! pack the chain is exactly the `Data` folder's.

use std::path::{Path, PathBuf};

use bevy::prelude::Resource;

/// On (`"1"`, the default) mounts the pack when it is installed; `"0"` leaves it out.
pub const CVAR: &str = "womwHdPack";
/// Session-only: `"1"` when a complete pack was found at boot.
pub const INSTALLED_CVAR: &str = "womwHdInstalled";
/// The pack's manifest: one archive file name per line (a tab and its size may follow), `#`
/// lines ignored.
pub const MANIFEST: &str = "womw-hd.txt";
/// `$WOMW_HD_DIR` names the pack's folder outright.
pub const DIR_ENV: &str = "WOMW_HD_DIR";

/// Whether a `Data` archive is a layer built over the stock chain (`patch-?` past `patch-3`).
pub fn is_built_layer(archive: &str) -> bool {
    let n = archive.to_ascii_lowercase();
    n.strip_prefix("patch-")
        .and_then(|r| r.strip_suffix(".mpq"))
        .is_some_and(|c| c.len() == 1 && c > "3")
}

/// What boot found and decided.
#[derive(Resource, Clone, Debug, Default, PartialEq, Eq)]
pub struct HdPack {
    /// The pack's folder, when a complete pack was found.
    pub dir: Option<PathBuf>,
    /// The archives mounted this session (empty when off or absent).
    pub mounted: Vec<PathBuf>,
}

impl HdPack {
    pub fn installed(&self) -> bool {
        self.dir.is_some()
    }
}

/// The archives a manifest in `dir` lists, when every one is there; `None` otherwise (a partial
/// download never mounts).
pub fn read_manifest(dir: &Path) -> Option<Vec<PathBuf>> {
    let text = std::fs::read_to_string(dir.join(MANIFEST)).ok()?;
    let mut out = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut fields = line.split('\t');
        let name = fields.next()?.trim();
        // A name only: never a path out of the folder.
        if name.contains(['/', '\\']) || name.contains("..") {
            return None;
        }
        let p = dir.join(name);
        let meta = std::fs::metadata(&p).ok()?;
        if !meta.is_file() {
            return None;
        }
        if let Some(size) = fields.next().and_then(|s| s.trim().parse::<u64>().ok()) {
            if meta.len() != size {
                return None;
            }
        }
        out.push(p);
    }
    (!out.is_empty()).then_some(out)
}

/// Where a pack may be: `$WOMW_HD_DIR`, else an `hd` folder beside the game (`<pkg>\hd` for
/// `<pkg>\game\WoW\Data`) or the client (`<pkg>\client\..\hd`).
pub fn candidates(data_dir: &Path, exe_dir: Option<&Path>) -> Vec<PathBuf> {
    if let Some(d) = std::env::var_os(DIR_ENV) {
        return vec![PathBuf::from(d)];
    }
    let mut out: Vec<PathBuf> = data_dir
        .ancestors()
        .skip(1)
        .take(4)
        .map(|a| a.join("hd"))
        .collect();
    if let Some(e) = exe_dir {
        out.extend(e.ancestors().take(2).map(|a| a.join("hd")));
    }
    out
}

/// The boot decision: the first complete pack among `candidates`, mounted unless `cvar` is `"0"`.
pub fn plan(candidates: &[PathBuf], cvar: Option<&str>) -> HdPack {
    let Some((dir, archives)) = candidates
        .iter()
        .find_map(|d| read_manifest(d).map(|a| (d.clone(), a)))
    else {
        return HdPack::default();
    };
    let on = cvar.map(str::trim) != Some("0");
    HdPack {
        dir: Some(dir),
        mounted: if on { archives } else { Vec::new() },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pack(dir: &Path, names: &[&str]) {
        std::fs::create_dir_all(dir).unwrap();
        let mut m = String::new();
        for n in names {
            std::fs::write(dir.join(n), b"MPQ").unwrap();
            m.push_str(&format!("{n}\t3\n"));
        }
        m.push_str("# 2 files\n");
        std::fs::write(dir.join(MANIFEST), m).unwrap();
    }

    fn scratch(tag: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("benilla-hd-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }

    #[test]
    fn no_pack_mounts_nothing_and_reports_absent() {
        let d = scratch("none");
        let p = plan(&[d.join("hd")], Some("1"));
        assert_eq!(p, HdPack::default());
        assert!(!p.installed());
    }

    #[test]
    fn pack_on_mounts_every_listed_archive_in_order() {
        let d = scratch("on").join("hd");
        pack(&d, &["womw-hd-1.MPQ", "womw-hd-2.MPQ"]);
        for cvar in [None, Some("1")] {
            let p = plan(std::slice::from_ref(&d), cvar);
            assert!(p.installed());
            assert_eq!(
                p.mounted,
                vec![d.join("womw-hd-1.MPQ"), d.join("womw-hd-2.MPQ")]
            );
        }
    }

    #[test]
    fn pack_off_is_installed_but_unmounted() {
        let d = scratch("off").join("hd");
        pack(&d, &["womw-hd-1.MPQ"]);
        let p = plan(std::slice::from_ref(&d), Some("0"));
        assert!(p.installed());
        assert!(p.mounted.is_empty());
    }

    #[test]
    fn a_partial_or_resized_pack_never_mounts() {
        let d = scratch("partial").join("hd");
        pack(&d, &["womw-hd-1.MPQ", "womw-hd-2.MPQ"]);
        std::fs::remove_file(d.join("womw-hd-2.MPQ")).unwrap();
        assert!(!plan(std::slice::from_ref(&d), None).installed());
        pack(&d, &["womw-hd-1.MPQ"]);
        std::fs::write(d.join("womw-hd-1.MPQ"), b"MPQ-truncated?").unwrap();
        assert!(!plan(std::slice::from_ref(&d), None).installed());
        std::fs::write(d.join(MANIFEST), "..\\evil.MPQ\n").unwrap();
        assert!(!plan(std::slice::from_ref(&d), None).installed());
    }

    #[test]
    fn the_pack_folder_sits_beside_the_packaged_game() {
        let pkg = Path::new("C:\\pkg");
        let c = candidates(&pkg.join("game\\WoW\\Data"), Some(&pkg.join("client")));
        if std::env::var_os(DIR_ENV).is_none() {
            assert!(c.contains(&pkg.join("hd")));
        }
    }

    #[test]
    fn built_layers_are_ours_past_the_stock_patches() {
        for n in ["patch-9.MPQ", "patch-A.MPQ", "PATCH-a.mpq"] {
            assert!(is_built_layer(n), "{n}");
        }
        for n in [
            "patch.MPQ",
            "patch-2.MPQ",
            "patch-3.MPQ",
            "womw-hd-1.MPQ",
            "speech2.MPQ",
        ] {
            assert!(!is_built_layer(n), "{n}");
        }
    }
}
