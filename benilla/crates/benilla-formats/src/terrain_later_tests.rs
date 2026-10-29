//! The later-format terrain chunks (MH2O liquid, MCCV vertex colours, 8-bit alpha) on real
//! tiles: none appears on a 1.12 tile, so every 1.12 tile reads exactly as before, and a
//! 1.12-format tile made with later tools (`BENILLA_COMMUNITY_DATA`: a community client's `Data`
//! folder, its content patches `patch-3`..`patch-9`) builds its water and colours.

use std::io::Cursor;
use std::path::PathBuf;

use benilla_adt::{parse_adt, ParsedAdt};
use benilla_mpq::Archive;

use super::adt_to_tile_mesh;

fn root(bytes: &[u8]) -> Box<benilla_adt::RootAdt> {
    let ParsedAdt::Root(r) = parse_adt(&mut Cursor::new(bytes)).expect("parses");
    r
}

/// Stock tiles across both continents, ocean, rivers, lava and a river mouth among them.
const STOCK_TILES: [(&str, u32, u32); 8] = [
    ("Azeroth", 32, 48),  // Elwynn
    ("Azeroth", 31, 49),  // Goldshire, a lake
    ("Azeroth", 35, 52),  // Burning Steppes, magma
    ("Azeroth", 30, 31),  // Alterac (our Dalaran tiles)
    ("Kalimdor", 40, 30), // Ashenvale
    ("Kalimdor", 43, 33), // the Barrens coast
    ("Kalimdor", 39, 26), // Felwood
    ("Kalimdor", 31, 42), // Feralas coast, ocean
];

#[test]
fn no_stock_tile_has_a_later_format_chunk() {
    if crate::skip_unless_stock("no_stock_tile_has_a_later_format_chunk") {
        return;
    }
    let data = crate::wow_data_or_skip!();
    let chain = crate::Chain::open(&data).expect("chain");
    let mut read = 0;
    for (map, x, y) in STOCK_TILES {
        let Ok(bytes) = chain.read(&format!("World\\Maps\\{map}\\{map}_{x}_{y}.adt")) else {
            continue;
        };
        read += 1;
        let r = root(&bytes);
        assert!(!r.has_mh2o, "{map}_{x}_{y}");
        for c in &r.mcnk_chunks {
            assert!(c.vertex_colors.is_none(), "{map}_{x}_{y} MCCV");
            assert!(
                !c.big_alpha,
                "{map}_{x}_{y} chunk {} {}",
                c.header.index_x, c.header.index_y
            );
        }
        let mesh = adt_to_tile_mesh(&bytes).expect("meshes");
        assert!(mesh.chunks.iter().all(|c| c.vertex_colors.is_none()));
    }
    assert!(read >= 6, "only {read} stock tiles read");
}

/// The community client's content patches, lowest first; `None` without the corpus.
fn community() -> Option<Vec<Archive>> {
    let dir = PathBuf::from(std::env::var_os("BENILLA_COMMUNITY_DATA")?);
    let mut out = Vec::new();
    for n in 3..=9 {
        let p = dir.join(format!("patch-{n}.MPQ"));
        if p.is_file() {
            out.push(Archive::open(&p).expect("opens"));
        }
    }
    (!out.is_empty()).then_some(out)
}

fn community_read(archives: &[Archive], name: &str) -> Vec<u8> {
    archives
        .iter()
        .rev()
        .find(|a| a.contains(name))
        .unwrap_or_else(|| panic!("{name} not in the community patches"))
        .read_file(name)
        .expect("reads")
}

/// A new island tile (Tel'Abim, Kalimdor 44_47) keeps its sea in MH2O alone: the blocks built
/// from it mesh as ocean, flat at the tile's sea level, and the island's dry chunks stay dry.
#[test]
fn an_mh2o_tile_builds_its_sea_from_mh2o() {
    let Some(a) = community() else {
        eprintln!("skipped: BENILLA_COMMUNITY_DATA unset");
        return;
    };
    let bytes = community_read(&a, "World\\Maps\\Kalimdor\\Kalimdor_44_47.adt");
    let r = root(&bytes);
    assert!(r.has_mh2o);
    let wet = r
        .mcnk_chunks
        .iter()
        .filter(|c| !c.liquids.is_empty())
        .count();
    let dry = r.mcnk_chunks.len() - wet;
    assert!(wet > 16 && dry > 16, "{wet} wet, {dry} dry chunks");
    let mesh = adt_to_tile_mesh(&bytes).expect("meshes");
    let oceans: Vec<_> = mesh
        .chunks
        .iter()
        .flat_map(|c| c.liquids.iter())
        .filter(|l| l.kind == crate::liquid::LiquidKind::Ocean)
        .collect();
    assert!(!oceans.is_empty(), "the sea is ocean");
    // Every ocean surface sits at one sea level, within a yard.
    let zs: Vec<f32> = oceans
        .iter()
        .flat_map(|l| l.positions.iter().map(|p| p[2]))
        .collect();
    let (lo, hi) = zs
        .iter()
        .fold((f32::MAX, f32::MIN), |(lo, hi), &z| (lo.min(z), hi.max(z)));
    assert!(hi - lo < 1.0, "sea level spans {lo}..{hi}");
}

/// Tiles that carry MCCV read 145 colours per chunk as `2·c/255` multipliers and hand them to the
/// mesh per vertex: Kalimdor 43_16 (Moonwhisper Coast) all neutral `0x7f`, Kalimdor 35_49
/// (Tanaris) painted.
#[test]
fn an_mccv_tile_hands_its_vertex_colours_to_the_mesh() {
    let Some(a) = community() else {
        eprintln!("skipped: BENILLA_COMMUNITY_DATA unset");
        return;
    };
    let neutral = 254.0 / 255.0;
    for (tile, painted) in [("Kalimdor_43_16", false), ("Kalimdor_35_49", true)] {
        let bytes = community_read(&a, &format!("World\\Maps\\Kalimdor\\{tile}.adt"));
        let r = root(&bytes);
        let coloured = r
            .mcnk_chunks
            .iter()
            .filter(|c| c.vertex_colors.is_some())
            .count();
        assert!(coloured > 0, "{tile}");
        let mesh = adt_to_tile_mesh(&bytes).expect("meshes");
        let vc: Vec<&Vec<[f32; 3]>> = mesh
            .chunks
            .iter()
            .filter_map(|c| c.vertex_colors.as_ref())
            .collect();
        assert_eq!(vc.len(), coloured, "{tile}");
        for v in &vc {
            assert_eq!(v.len(), 145);
            assert!(v.iter().flatten().all(|&m| (0.0..=2.0).contains(&m)));
        }
        let off = vc
            .iter()
            .flat_map(|v| v.iter())
            .filter(|c| c.iter().any(|&m| (m - neutral).abs() > 0.05))
            .count();
        assert_eq!(off > 0, painted, "{tile}: {off} painted vertices");
    }
}
