//! The WotLK 3.3.5a archive chain, read-only. The stock archives in their priority order; the
//! extra `patch-?.MPQ` files some installs add (HD texture packs, other mods) are left out on
//! purpose, so what is imported is the stock 3.3.5 art.

use std::path::{Path, PathBuf};

use anyhow::{Context, Result, bail};
use benilla_mpq::Archive;

/// The stock 3.3.5a base and patch archives, lowest priority first.
const BASE: [&str; 7] = [
    "common.MPQ",
    "common-2.MPQ",
    "expansion.MPQ",
    "lichking.MPQ",
    "patch.MPQ",
    "patch-2.MPQ",
    "patch-3.MPQ",
];
/// The locale archives (the DBCs live there), lowest priority first; `{}` is the locale.
const LOCALE: [&str; 6] = [
    "locale-{}.MPQ",
    "expansion-locale-{}.MPQ",
    "lichking-locale-{}.MPQ",
    "patch-{}.MPQ",
    "patch-{}-2.MPQ",
    "patch-{}-3.MPQ",
];

/// The default install location on the maintainer's machine; `WOTLK_DATA` names another.
pub const DEFAULT_DATA: &str = r"E:\Local Games\world of warcraft 3.3.5a hd\Data";

pub struct Source {
    archives: Vec<Archive>,
}

/// A file in `dir` named `name`, any case (the 3.3.5 installs mix `.mpq` and `.MPQ`).
fn find_ci(dir: &Path, name: &str) -> Option<PathBuf> {
    std::fs::read_dir(dir).ok()?.flatten().find_map(|e| {
        let n = e.file_name().into_string().ok()?;
        n.eq_ignore_ascii_case(name).then(|| e.path())
    })
}

impl Source {
    /// Open the chain of the `Data` folder at `data` for `locale` (e.g. `enUS`).
    pub fn open(data: &Path, locale: &str) -> Result<Self> {
        let mut archives = Vec::new();
        for name in BASE {
            if let Some(p) = find_ci(data, name) {
                archives.push(Archive::open(&p).with_context(|| format!("opening {}", p.display()))?);
            }
        }
        let locdir = find_ci(data, locale).with_context(|| format!("no {locale} folder in {}", data.display()))?;
        for pat in LOCALE {
            if let Some(p) = find_ci(&locdir, &pat.replace("{}", locale)) {
                archives.push(Archive::open(&p).with_context(|| format!("opening {}", p.display()))?);
            }
        }
        if archives.len() < 4 {
            bail!("{} does not look like a 3.3.5 Data folder", data.display());
        }
        Ok(Self { archives })
    }

    /// The default install, when present: `WOTLK_DATA` or [`DEFAULT_DATA`].
    pub fn open_default() -> Option<Self> {
        let dir = std::env::var("WOTLK_DATA").unwrap_or_else(|_| DEFAULT_DATA.to_string());
        let dir = Path::new(&dir);
        dir.is_dir().then(|| Self::open(dir, "enUS").ok()).flatten()
    }

    pub fn contains(&self, name: &str) -> bool {
        self.archives
            .iter()
            .rev()
            .find(|a| a.contains(name))
            .is_some_and(|a| !a.is_delete_marker(name))
    }

    /// Read `name` from the highest-priority archive holding it.
    pub fn read(&self, name: &str) -> Result<Vec<u8>> {
        let a = self
            .archives
            .iter()
            .rev()
            .find(|a| a.contains(name))
            .with_context(|| format!("{name} is not in the 3.3.5 chain"))?;
        a.read_file(name)
            .with_context(|| format!("reading {name} from {}", a.path().display()))
    }

    /// The union of the archives' `(listfile)`s.
    pub fn list(&self) -> Vec<String> {
        let mut seen = std::collections::HashSet::new();
        let mut out = Vec::new();
        for a in &self.archives {
            let Ok(l) = a.read_file("(listfile)") else {
                continue;
            };
            for n in String::from_utf8_lossy(&l).split([';', '\r', '\n']) {
                let n = n.trim();
                if !n.is_empty() && seen.insert(n.to_ascii_lowercase()) {
                    out.push(n.to_string());
                }
            }
        }
        out
    }
}
