//! The reference walk: from the merged tiles (and the display rows the imported spawns use) to
//! every model, WMO group and texture they load, so the patch carries each file the community
//! adds and nothing we already have.

use std::collections::{BTreeSet, VecDeque};

use benilla_bytes::{chunks, ByteExt};

use crate::source::{Layer, Ours};

/// A path in the form the archives hash it: `\` separators. Model names in ADTs and WMOs say
/// `.mdx`/`.mdl`, and 1.12 loads the `.m2` of the same name.
pub fn normalize(path: &str) -> String {
    let p = path.trim().replace('/', "\\");
    let lower = p.to_ascii_lowercase();
    if lower.ends_with(".mdx") || lower.ends_with(".mdl") {
        format!("{}.m2", &p[..p.len() - 4])
    } else {
        p
    }
}

fn split_names(blob: &[u8]) -> Vec<String> {
    blob.split(|&c| c == 0)
        .filter(|s| !s.is_empty())
        .map(|s| String::from_utf8_lossy(s).into_owned())
        .collect()
}

/// A WMO root's references: its groups (`<root>_NNN.wmo`), textures (MOTX), doodads (MODN) and
/// skybox (MOSB).
pub fn wmo_root_refs(path: &str, b: &[u8]) -> Vec<String> {
    let mut out = Vec::new();
    let stem = &path[..path.len().saturating_sub(4)];
    for (magic, data) in chunks(b) {
        match &magic {
            b"DHOM" => {
                let groups = data.u32_at(4).unwrap_or(0).min(512);
                for g in 0..groups {
                    out.push(format!("{stem}_{g:03}.wmo"));
                }
            }
            b"XTOM" | b"NDOM" | b"BSOM" => out.extend(split_names(data)),
            _ => {}
        }
    }
    out
}

/// An M2 (version 256) model's hard-coded textures (type 0; the others come from display rows).
pub fn m2_texture_refs(b: &[u8]) -> Vec<String> {
    if b.get(..4) != Some(b"MD20") {
        return Vec::new();
    }
    let (Some(n), Some(ofs)) = (b.u32_at(0x5C), b.u32_at(0x60)) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for i in 0..(n as usize).min(256) {
        let at = ofs as usize + i * 16;
        let (Some(kind), Some(len), Some(sofs)) =
            (b.u32_at(at), b.u32_at(at + 8), b.u32_at(at + 12))
        else {
            break;
        };
        if kind != 0 || len == 0 {
            continue;
        }
        if let Some(s) = b.bytes_at(sofs as usize, len as usize) {
            let s = s.split(|&c| c == 0).next().unwrap_or(&[]);
            if !s.is_empty() {
                out.push(String::from_utf8_lossy(s).into_owned());
            }
        }
    }
    out
}

/// What the walk found.
#[derive(Debug, Default)]
pub struct Walk {
    /// Files only the community has: the patch carries these.
    pub new_files: BTreeSet<String>,
    /// Files both have with different bytes: ours kept (the community's edits of our files are
    /// not merged), listed for the report.
    pub overridden: BTreeSet<String>,
    /// Files of ours the community edited that come from them (edited WMOs and their groups).
    pub edited: BTreeSet<String>,
    /// Referenced files neither side has.
    pub missing: BTreeSet<String>,
}

/// Walk from `roots` (paths any case): each new file is read and its own references followed.
/// With `edited_wmos`, a WMO the community edited (root bytes differ from ours) is taken from
/// them with every group, unless one of our own patch layers holds it.
pub fn walk(
    roots: impl IntoIterator<Item = String>,
    community: &Layer,
    ours: &Ours,
    edited_wmos: bool,
) -> Walk {
    let mut seen = BTreeSet::new();
    let mut queue: VecDeque<String> = roots.into_iter().map(|r| normalize(&r)).collect();
    let mut w = Walk::default();
    let mut taken_roots: BTreeSet<String> = BTreeSet::new();
    while let Some(p) = queue.pop_front() {
        if p.is_empty() || !seen.insert(p.to_ascii_lowercase()) {
            continue;
        }
        let theirs = community.contains(&p);
        let have = ours.contains(&p);
        let bytes = match (theirs, have) {
            (true, false) => {
                w.new_files.insert(p.clone());
                community.read(&p).ok()
            }
            (true, true) => {
                let t = community.read(&p).ok();
                let ours_b = ours.read(&p).ok();
                let lower = p.to_ascii_lowercase();
                let wmo = lower.ends_with(".wmo");
                let group_of_taken =
                    is_wmo_group(&lower) && taken_roots.contains(&wmo_stem(&lower));
                if t.as_deref() == ours_b.as_deref() {
                    ours_b
                } else if wmo
                    && !ours.in_our_layers(&p)
                    && (group_of_taken || (!is_wmo_group(&lower) && edited_wmos))
                {
                    // Their edit of a building we have (Stormwind opened onto its harbour): the
                    // root and its groups come together, and our own layers still win.
                    if !is_wmo_group(&lower) {
                        taken_roots.insert(lower[..lower.len() - 4].to_string());
                    }
                    w.new_files.insert(p.clone());
                    w.edited.insert(p.clone());
                    t
                } else {
                    w.overridden.insert(p.clone());
                    // A file we have can still name new ones (an edited WMO's new textures):
                    // follow our copy, which is what the game loads.
                    ours_b
                }
            }
            (false, true) => ours.read(&p).ok(),
            (false, false) => {
                w.missing.insert(p.clone());
                None
            }
        };
        let Some(b) = bytes else { continue };
        let lower = p.to_ascii_lowercase();
        let next = if lower.ends_with(".wmo") && !is_wmo_group(&lower) {
            wmo_root_refs(&p, &b)
        } else if lower.ends_with(".wmo") {
            // A group's own textures and doodads are named by its root.
            Vec::new()
        } else if lower.ends_with(".m2") {
            m2_texture_refs(&b)
        } else {
            Vec::new()
        };
        // Only new (or taken) files' children are needed; a stock file's children are stock.
        if theirs && (!have || w.new_files.contains(&p)) {
            queue.extend(next.into_iter().map(|n| normalize(&n)));
        }
    }
    w
}

/// A group's root stem: `x\keep_001.wmo` -> `x\keep`.
fn wmo_stem(lower: &str) -> String {
    lower[..lower.len() - 8].to_string()
}

/// `name_NNN.wmo`: a WMO group file.
fn is_wmo_group(lower: &str) -> bool {
    let stem = &lower[..lower.len() - 4];
    stem.len() > 4
        && stem.as_bytes()[stem.len() - 4] == b'_'
        && stem[stem.len() - 3..].bytes().all(|c| c.is_ascii_digit())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Their edited Stormwind comes in with all its groups (one more than ours); an identical
    /// group or one our own layer holds stays ours; without the switch the edit stays out.
    #[test]
    fn an_edited_wmo_comes_with_its_groups_and_our_layers_win() {
        use crate::mpq_write::write_mpq;
        let dir = std::env::temp_dir().join(format!("womw-wmo-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(dir.join("ours")).unwrap();
        let root = |groups: u32, tag: &[u8]| {
            let mut b = Vec::new();
            b.extend_from_slice(b"DHOM");
            b.extend_from_slice(&64u32.to_le_bytes());
            let mut mohd = vec![0u8; 64];
            mohd[4..8].copy_from_slice(&groups.to_le_bytes());
            mohd[8..8 + tag.len()].copy_from_slice(tag);
            b.extend(mohd);
            b
        };
        let put = |p: std::path::PathBuf, files: Vec<(String, Vec<u8>)>| {
            std::fs::write(p, write_mpq(&files)).unwrap()
        };
        let sw = "World\\wmo\\Stormwind.wmo";
        let g = |i: u32| format!("World\\wmo\\Stormwind_{i:03}.wmo");
        let ogre = "World\\wmo\\Ogre.wmo";
        put(
            dir.join("ours").join("wmo.MPQ"),
            vec![
                (sw.into(), root(2, b"ours")),
                (g(0), b"g0".to_vec()),
                (g(1), b"g1".to_vec()),
                (ogre.into(), root(0, b"ours")),
            ],
        );
        put(
            dir.join("patch-9.MPQ"),
            vec![(ogre.into(), root(0, b"wotlk"))],
        );
        put(
            dir.join("theirs.MPQ"),
            vec![
                (sw.into(), root(3, b"them")),
                (g(0), b"g0".to_vec()),
                (g(1), b"G1".to_vec()),
                (g(2), b"g2".to_vec()),
                (ogre.into(), root(0, b"them")),
            ],
        );
        let community = Layer::open_files(&[dir.join("theirs.MPQ")]).unwrap();
        let ours = Ours::open(&dir.join("ours"), &[dir.join("patch-9.MPQ")]).unwrap();
        let w = walk([sw.to_string(), ogre.to_string()], &community, &ours, true);
        let got: Vec<&str> = w.new_files.iter().map(|s| s.as_str()).collect();
        assert_eq!(got, vec![sw, g(1).as_str(), g(2).as_str()]);
        assert!(w.overridden.contains(ogre), "our layer's copy wins");
        let off = walk([sw.to_string()], &community, &ours, false);
        assert_eq!(off.new_files.len(), 0);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn model_names_load_as_m2_and_separators_fold() {
        assert_eq!(
            normalize("World/Generic/Tree01.MDX"),
            "World\\Generic\\Tree01.m2"
        );
        assert_eq!(normalize("a\\b.mdl"), "a\\b.m2");
        assert_eq!(normalize("a\\b.blp"), "a\\b.blp");
    }

    #[test]
    fn a_wmo_root_names_its_groups_textures_and_doodads() {
        let mut b = Vec::new();
        let mut put = |m: &[u8; 4], d: &[u8]| {
            b.extend_from_slice(m);
            b.extend_from_slice(&(d.len() as u32).to_le_bytes());
            b.extend_from_slice(d);
        };
        let mut mohd = vec![0u8; 64];
        mohd[4..8].copy_from_slice(&2u32.to_le_bytes());
        put(b"DHOM", &mohd);
        put(b"XTOM", b"t\\a.blp\0\0t\\b.blp\0");
        put(b"NDOM", b"d\\x.mdx\0");
        let r = wmo_root_refs("World\\wmo\\Keep.wmo", &b);
        assert_eq!(
            r,
            vec![
                "World\\wmo\\Keep_000.wmo",
                "World\\wmo\\Keep_001.wmo",
                "t\\a.blp",
                "t\\b.blp",
                "d\\x.mdx"
            ]
        );
        assert!(is_wmo_group("world\\wmo\\keep_001.wmo"));
        assert!(!is_wmo_group("world\\wmo\\keep.wmo"));
    }

    #[test]
    fn an_m2_names_its_hard_coded_textures_only() {
        let mut b = vec![0u8; 0x100];
        b[..4].copy_from_slice(b"MD20");
        b[0x5C..0x60].copy_from_slice(&2u32.to_le_bytes());
        b[0x60..0x64].copy_from_slice(&0x80u32.to_le_bytes());
        // Texture 0: type 0, "a.blp" at 0xC0; texture 1: type 11 (a skin, from the display row).
        b[0x88..0x8C].copy_from_slice(&6u32.to_le_bytes());
        b[0x8C..0x90].copy_from_slice(&0xC0u32.to_le_bytes());
        b[0x90..0x94].copy_from_slice(&11u32.to_le_bytes());
        b[0xC0..0xC6].copy_from_slice(b"a.blp\0");
        assert_eq!(m2_texture_refs(&b), vec!["a.blp"]);
    }
}
