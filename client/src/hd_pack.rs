//! The optional HD pack as World of Modern Warfare ships it (`design/HD_PACK.md`): built by
//! `community-merge hd` into `private-assets/womw-hd/`, packaged to `<package>\hd\`, mounted by
//! benilla at boot ([`benilla_app::hd_pack`]) above the stock archives and below our layers
//! (`patch-9`, `patch-A`), switched by Options > Graphics > HD Textures and Models. These tests
//! hold the precedence (ours always wins) and that the built pack's art loads through benilla.

#[cfg(test)]
mod tests {
    use std::path::{Path, PathBuf};

    use benilla_app::hd_pack::{self, MANIFEST};
    use benilla_formats::Chain;

    use crate::wotlk::mpq_write::write_mpq;

    fn temp_dir(tag: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("womw-hd-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn files(list: &[(&str, &str)]) -> Vec<(String, Vec<u8>)> {
        list.iter()
            .map(|(n, b)| (n.to_string(), b.as_bytes().to_vec()))
            .collect()
    }

    /// The packaged layout: `game\WoW\Data` with the stock archives and our two layers, and `hd\`
    /// beside it with a pack that also holds every path our layers hold.
    fn package(tag: &str) -> (PathBuf, PathBuf) {
        let pkg = temp_dir(tag);
        let data = pkg.join("game").join("WoW").join("Data");
        std::fs::create_dir_all(&data).unwrap();
        std::fs::write(
            data.join("model.MPQ"),
            write_mpq(&files(&[
                ("Creature\\Bear\\Bear.m2", "stock bear"),
                ("Creature\\Wolf\\Wolf.blp", "stock wolf"),
                ("World\\Dalaran\\Dome.m2", "stock dome"),
            ])),
        )
        .unwrap();
        std::fs::write(data.join("patch.MPQ"), write_mpq(&files(&[("p.txt", "p")]))).unwrap();
        std::fs::write(
            data.join("patch-9.MPQ"),
            write_mpq(&files(&[("World\\Dalaran\\Dome.m2", "our wotlk dome")])),
        )
        .unwrap();
        std::fs::write(
            data.join("patch-A.MPQ"),
            write_mpq(&files(&[(
                "Creature\\Wolf\\Wolf.blp",
                "our community wolf",
            )])),
        )
        .unwrap();
        let hd = pkg.join("hd");
        std::fs::create_dir_all(&hd).unwrap();
        let bytes = write_mpq(&files(&[
            ("Creature\\Bear\\Bear.m2", "hd bear"),
            ("Creature\\Wolf\\Wolf.blp", "hd wolf"),
            ("World\\Dalaran\\Dome.m2", "hd dome"),
        ]));
        std::fs::write(hd.join("womw-hd-1.MPQ"), &bytes).unwrap();
        std::fs::write(
            hd.join(MANIFEST),
            format!("womw-hd-1.MPQ\t{}\n", bytes.len()),
        )
        .unwrap();
        (pkg, data)
    }

    fn chain_for(data: &Path, cvar: Option<&str>) -> (hd_pack::HdPack, Chain) {
        // The package's own `hd\` (as `candidates` finds it without `$WOMW_HD_DIR`).
        let pkg = data.ancestors().nth(3).unwrap();
        let plan = hd_pack::plan(&[pkg.join("hd")], cvar);
        let chain =
            Chain::open_with_inserted(data, &|_| true, &plan.mounted, &hd_pack::is_built_layer)
                .unwrap();
        (plan, chain)
    }

    #[test]
    fn our_layers_win_over_the_pack_and_the_pack_wins_over_stock() {
        let (pkg, data) = package("precedence");
        let (plan, chain) = chain_for(&data, Some("1"));
        assert!(plan.installed());
        assert_eq!(plan.mounted, vec![pkg.join("hd").join("womw-hd-1.MPQ")]);
        // A stock file our layers leave alone: the HD copy.
        assert_eq!(chain.read("Creature\\Bear\\Bear.m2").unwrap(), b"hd bear");
        // Our WotLK and community layers always win.
        assert_eq!(
            chain.read("World\\Dalaran\\Dome.m2").unwrap(),
            b"our wotlk dome"
        );
        assert_eq!(
            chain.read("Creature\\Wolf\\Wolf.blp").unwrap(),
            b"our community wolf"
        );
        // Mounted between them: stock, the pack, then ours.
        let order: Vec<String> = chain
            .archive_paths()
            .iter()
            .map(|p| p.file_name().unwrap().to_string_lossy().into_owned())
            .collect();
        assert_eq!(
            order,
            [
                "model.MPQ",
                "patch.MPQ",
                "womw-hd-1.MPQ",
                "patch-9.MPQ",
                "patch-A.MPQ"
            ]
        );
        let _ = std::fs::remove_dir_all(&pkg);
    }

    #[test]
    fn the_pack_off_or_absent_is_the_data_folder_alone() {
        let (pkg, data) = package("off");
        let plain: Vec<PathBuf> = Chain::open(&data)
            .unwrap()
            .archive_paths()
            .iter()
            .map(|p| p.to_path_buf())
            .collect();
        let (plan, chain) = chain_for(&data, Some("0"));
        assert!(plan.installed() && plan.mounted.is_empty());
        assert_eq!(
            chain.read("Creature\\Bear\\Bear.m2").unwrap(),
            b"stock bear"
        );
        let off: Vec<PathBuf> = chain
            .archive_paths()
            .iter()
            .map(|p| p.to_path_buf())
            .collect();
        assert_eq!(off, plain);
        std::fs::remove_dir_all(pkg.join("hd")).unwrap();
        let (plan, chain) = chain_for(&data, Some("1"));
        assert!(!plan.installed());
        assert_eq!(
            chain.read("Creature\\Bear\\Bear.m2").unwrap(),
            b"stock bear"
        );
        let none: Vec<PathBuf> = chain
            .archive_paths()
            .iter()
            .map(|p| p.to_path_buf())
            .collect();
        assert_eq!(none, plain);
        let _ = std::fs::remove_dir_all(&pkg);
    }

    /// The built pack (`private-assets/womw-hd`) over the packaged game: sample models and
    /// textures of each kind read through the chain the client mounts and decode with benilla's
    /// own readers (the M2 loader's submesh and skeleton parse, the BLP decoder). Skipped when
    /// either is absent.
    #[test]
    fn sample_hd_models_and_textures_load_through_benilla() {
        let Some(data) = crate::wotlk::vanilla_install() else {
            eprintln!("no WoW install: skipped");
            return;
        };
        let hd = std::env::var_os(hd_pack::DIR_ENV)
            .map(PathBuf::from)
            .unwrap_or_else(|| {
                Path::new(env!("CARGO_MANIFEST_DIR")).join("../private-assets/womw-hd")
            });
        let Some(archives) = hd_pack::read_manifest(&hd) else {
            eprintln!("no built HD pack in {}: skipped", hd.display());
            return;
        };
        let chain =
            Chain::open_with_inserted(&data, &|_| true, &archives, &hd_pack::is_built_layer)
                .unwrap();
        let mut checked = std::collections::BTreeMap::<&str, usize>::new();
        for a in &archives {
            let archive = benilla_mpq::Archive::open(a).unwrap();
            let list = String::from_utf8(archive.read_file("(listfile)").unwrap()).unwrap();
            // Every 97th file: a spread over the whole archive, quick enough for the suite.
            for name in list.lines().filter(|n| !n.is_empty()).step_by(97) {
                let lower = name.to_ascii_lowercase();
                let kind = if lower.ends_with(".m2") {
                    "model"
                } else if lower.ends_with(".blp") {
                    "texture"
                } else {
                    continue;
                };
                // Through the client's chain: the pack's copy unless one of our layers holds it.
                let bytes = chain.read(name).unwrap();
                if !chain.find_file_archive(name).unwrap().starts_with(&hd) {
                    continue;
                }
                if kind == "model" {
                    benilla_formats::parse_m2_render_submeshes(&bytes, "", &[])
                        .unwrap_or_else(|e| panic!("{name}: {e:#}"));
                    benilla_formats::parse_m2_skeleton(&bytes)
                        .unwrap_or_else(|e| panic!("{name}: {e:#}"));
                } else {
                    benilla_formats::blp_bytes_to_mip_chain(&bytes)
                        .unwrap_or_else(|e| panic!("{name}: {e:#}"));
                }
                *checked.entry(kind).or_default() += 1;
            }
        }
        eprintln!("HD pack samples decoded: {checked:?}");
        assert!(
            checked.get("model").copied().unwrap_or(0) > 0,
            "no model sampled"
        );
        assert!(
            checked.get("texture").copied().unwrap_or(0) > 0,
            "no texture sampled"
        );
    }
}
