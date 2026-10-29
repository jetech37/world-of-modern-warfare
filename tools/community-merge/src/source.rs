//! The two sides of the merge, read with benilla's own readers and never written:
//! - the **community layer**: the content patches of a 1.12-format community client, opened one by
//!   one in ascending priority (an explicit list, so its optional HD texture packs stay out);
//! - **ours**: World of Modern Warfare's `Data` folder as benilla mounts it (stock 1.12), plus our
//!   own patch layers stacked above it (`private-assets/womw-patch/patch-9.MPQ`), the base every
//!   new row and file is checked against.

use std::path::Path;

use anyhow::{bail, Context, Result};
use benilla_mpq::Archive;

/// The community client's content archives, lowest priority first. `patch-A` and later letters
/// are its HD texture/model pack and launcher fixes, which the merge leaves out.
pub const CONTENT_PATCHES: [&str; 7] = [
    "patch-3.MPQ",
    "patch-4.MPQ",
    "patch-5.MPQ",
    "patch-6.MPQ",
    "patch-7.MPQ",
    "patch-8.MPQ",
    "patch-9.MPQ",
];

/// An ordered stack of archives, highest priority last.
pub struct Layer {
    archives: Vec<Archive>,
}

impl Layer {
    /// Open the community client's [`CONTENT_PATCHES`] from its `Data` folder (any case).
    pub fn open_community(data: &Path) -> Result<Self> {
        let names: Vec<String> = std::fs::read_dir(data)
            .with_context(|| format!("listing {}", data.display()))?
            .filter_map(|e| e.ok()?.file_name().into_string().ok())
            .collect();
        let mut archives = Vec::new();
        for want in CONTENT_PATCHES {
            if let Some(n) = names.iter().find(|n| n.eq_ignore_ascii_case(want)) {
                let p = data.join(n);
                archives
                    .push(Archive::open(&p).with_context(|| format!("opening {}", p.display()))?);
            }
        }
        if archives.is_empty() {
            bail!(
                "no community content patches (patch-3..patch-9) in {}",
                data.display()
            );
        }
        Ok(Self { archives })
    }

    /// Open archive files in the order given (lowest first).
    pub fn open_files(paths: &[impl AsRef<Path>]) -> Result<Self> {
        let mut archives = Vec::new();
        for p in paths {
            let p = p.as_ref();
            archives.push(Archive::open(p).with_context(|| format!("opening {}", p.display()))?);
        }
        Ok(Self { archives })
    }

    fn resolve(&self, name: &str) -> Option<&Archive> {
        self.archives.iter().rev().find(|a| a.contains(name))
    }

    /// Whether a layer archive holds `name` as a readable file (a delete marker is not one).
    pub fn contains(&self, name: &str) -> bool {
        self.resolve(name)
            .is_some_and(|a| !a.is_delete_marker(name))
    }

    /// Whether a layer archive tombstones `name`.
    pub fn deletes(&self, name: &str) -> bool {
        self.resolve(name).is_some_and(|a| a.is_delete_marker(name))
    }

    /// `name` from the highest archive holding it.
    pub fn read(&self, name: &str) -> Result<Vec<u8>> {
        let a = self
            .resolve(name)
            .filter(|a| !a.is_delete_marker(name))
            .with_context(|| format!("not in the layer: {name}"))?;
        a.read_file(name)
            .with_context(|| format!("reading {name} from {}", a.path().display()))
    }

    /// The uncompressed size of `name` in the archive it resolves to.
    pub fn size(&self, name: &str) -> Option<u64> {
        self.resolve(name)
            .filter(|a| !a.is_delete_marker(name))
            .and_then(|a| a.file_size(name))
            .map(u64::from)
    }

    /// Every name the layer lists in a `(listfile)`; archives without one are read by name only.
    pub fn listed_names(&self) -> Vec<String> {
        let mut out = Vec::new();
        let mut seen = std::collections::HashSet::new();
        for a in &self.archives {
            let Ok(list) = a.read_file("(listfile)") else {
                continue;
            };
            for raw in String::from_utf8_lossy(&list).split([';', '\r', '\n']) {
                let n = raw.trim();
                if !n.is_empty()
                    && n != "(listfile)"
                    && seen.insert(n.replace('/', "\\").to_ascii_lowercase())
                {
                    out.push(n.to_string());
                }
            }
        }
        out
    }
}

/// The archives of a `Data` folder in benilla's mount order ([`benilla_formats::Chain`]: the
/// base archives, `patch.MPQ`, every one-character `patch-?.MPQ` by case-folded name), leaving out
/// `skip` (our own community archive, so a rebuild never reads its previous output).
pub fn mount_order(dir_names: &[String], skip: &str) -> Vec<String> {
    let find = |want: &str| {
        dir_names
            .iter()
            .find(|n| n.eq_ignore_ascii_case(want))
            .cloned()
    };
    let mut order: Vec<String> = benilla_formats::VANILLA_BASE_ORDER
        .iter()
        .filter_map(|b| find(b))
        .collect();
    order.extend(find("patch.MPQ"));
    let mut patches: Vec<String> = dir_names
        .iter()
        .filter(|n| {
            let l = n.to_ascii_lowercase();
            l.strip_prefix("patch-")
                .and_then(|r| r.strip_suffix(".mpq"))
                .is_some_and(|m| m.chars().count() == 1)
                && !n.eq_ignore_ascii_case(skip)
        })
        .cloned()
        .collect();
    patches.sort_by_key(|n| n.to_ascii_lowercase());
    order.extend(patches);
    order.extend(find("speech2.MPQ"));
    order
}

/// Our side: our `Data` folder in benilla's order, with our private patch layers that are not
/// (yet) installed in it stacked above.
pub struct Ours {
    stock: Layer,
    above: Layer,
}

impl Ours {
    /// `data`: our `Data` folder; `above`: our private patch archives, lowest first (the WotLK
    /// layer `private-assets/womw-patch/patch-9.MPQ`).
    pub fn open(data: &Path, above: &[impl AsRef<Path>]) -> Result<Self> {
        let names: Vec<String> = std::fs::read_dir(data)
            .with_context(|| format!("listing {}", data.display()))?
            .filter_map(|e| {
                let e = e.ok()?;
                e.path()
                    .is_file()
                    .then(|| e.file_name().into_string().ok())?
            })
            .collect();
        let order = mount_order(&names, crate::ARCHIVE_NAME);
        if order.is_empty() {
            bail!("no 1.12 archives in {}", data.display());
        }
        let paths: Vec<_> = order.iter().map(|n| data.join(n)).collect();
        Ok(Self {
            stock: Layer::open_files(&paths)?,
            above: Layer::open_files(above)?,
        })
    }

    /// Over already-open layers (tests).
    pub fn from_layers(stock: Layer, above: Layer) -> Self {
        Self { stock, above }
    }

    pub fn contains(&self, name: &str) -> bool {
        if self.above.deletes(name) {
            return false;
        }
        self.above.contains(name) || self.stock.contains(name)
    }

    /// Whether one of our own private layers (not the `Data` folder) holds `name`.
    pub fn in_our_layers(&self, name: &str) -> bool {
        self.above.contains(name)
    }

    pub fn read(&self, name: &str) -> Result<Vec<u8>> {
        if self.above.contains(name) {
            return self.above.read(name);
        }
        self.stock.read(name)
    }
}
