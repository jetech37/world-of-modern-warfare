//! What the patch layer imports from 3.3.5 and the build of [`super::PATCH_ARCHIVE`].
//!
//! Each [`Look`] copies one 3.3.5 `CreatureDisplayInfo` row (its model, skins and scale) under a
//! new 1.12 display id in [`DISPLAY_BASE`]`..`. The model is converted (`super::m2conv`) and gets
//! a new `CreatureModelData` id from [`MODEL_BASE`]. What a 3.3.5 row names that 1.12 has no
//! table for (its sound, blood and footprint rows) is borrowed from a 1.12 `donor` display of the
//! same kind of creature. A file that already exists in the 1.12 chain is never overridden: the
//! layer only adds paths, so no 1.12 creature changes.
//!
//! The ids here are the contract with the server (`33_wotlk_looks.sql`, the companion look
//! overrides in `Companions.cpp`); a test holds the SQL to this table.

use std::collections::{BTreeMap, HashMap};

use anyhow::{Context, Result};
use benilla_formats::Chain;

use super::dbc::{Dbc, Field};
use super::m2conv;
use super::source::Source;

/// The first new 1.12 display id (1.12's highest is 17548).
pub const DISPLAY_BASE: u32 = 40001;
/// The first new `CreatureModelData` id (1.12's highest is 2365).
pub const MODEL_BASE: u32 = 4001;

/// One imported creature display.
pub struct Look {
    /// The new 1.12 display id.
    pub display: u32,
    /// The 3.3.5 `CreatureDisplayInfo` row it copies.
    pub source: u32,
    /// A 1.12 display of the same kind of creature: its sound, blood and footprint rows.
    pub donor: u32,
    pub what: &'static str,
}

/// The imported displays (design/COMPANION_MODEL_RESEARCH.md's candidates that 3.3.5 has).
pub const LOOKS: &[Look] = &[
    Look { display: 40001, source: 29805, donor: 1959, what: "raptor hatchling, orange" },
    Look { display: 40002, source: 29803, donor: 1747, what: "raptor hatchling, mottled blue-green" },
    Look { display: 40003, source: 29804, donor: 949, what: "raptor hatchling, mottled dark green" },
    Look { display: 40004, source: 29806, donor: 788, what: "raptor hatchling, red" },
    Look { display: 40005, source: 23507, donor: 2548, what: "baby crocolisk, green" },
    Look { display: 40006, source: 22389, donor: 2548, what: "baby crocolisk, brown" },
    Look { display: 40007, source: 26709, donor: 8574, what: "proto-drake, black" },
    Look { display: 40008, source: 26713, donor: 2718, what: "proto-drake, red" },
    Look { display: 40009, source: 26712, donor: 7863, what: "proto-drake, green" },
    Look { display: 40010, source: 26710, donor: 1687, what: "proto-drake, blue" },
    Look { display: 40011, source: 27650, donor: 762, what: "armored bear, brown" },
    Look { display: 40012, source: 27649, donor: 762, what: "armored bear, black" },
    Look { display: 40013, source: 26358, donor: 913, what: "armored bear, white" },
    Look { display: 40014, source: 21973, donor: 6080, what: "spectral tiger" },
    Look { display: 40015, source: 17784, donor: 1405, what: "phoenix" },
    Look { display: 40016, source: 17158, donor: 5027, what: "riding turtle" },
    Look { display: 40017, source: 21382, donor: 1221, what: "hawkstrider hatchling" },
    Look { display: 40018, source: 23574, donor: 16587, what: "phoenix hatchling" },
    // The flying mounts' lines (35_flying_companions.sql): a young form, a grown one where 1.12 has
    // none, and the riding model the last form is ridden as.
    Look { display: 40019, source: 30412, donor: 1149, what: "gryphon hatchling" },
    Look { display: 40020, source: 17697, donor: 1149, what: "riding gryphon, armored" },
    Look { display: 40021, source: 30413, donor: 295, what: "wind rider cub" },
    Look { display: 40022, source: 17699, donor: 295, what: "riding wind rider, tawny" },
    Look { display: 40023, source: 22472, donor: 10890, what: "riding hippogryph, blue" },
    Look { display: 40024, source: 20026, donor: 2305, what: "dragonhawk hatchling, red" },
    Look { display: 40025, source: 21831, donor: 2305, what: "dragonhawk, golden" },
    Look { display: 40026, source: 28402, donor: 2305, what: "riding dragonhawk, red" },
    Look { display: 40027, source: 18946, donor: 6288, what: "netherwhelp" },
    Look { display: 40028, source: 20540, donor: 9586, what: "nether drake, purple" },
    Look { display: 40029, source: 21523, donor: 9586, what: "riding nether drake, purple" },
    Look { display: 40030, source: 26735, donor: 2718, what: "proto-drake whelp, red" },
    Look { display: 40031, source: 26714, donor: 2718, what: "proto-drake, violet" },
    Look { display: 40032, source: 26718, donor: 2718, what: "riding proto-drake, red" },
    Look { display: 40033, source: 25750, donor: 9586, what: "frost wyrm whelp" },
    Look { display: 40034, source: 27066, donor: 9586, what: "frost wyrm" },
    Look { display: 40035, source: 25593, donor: 9586, what: "riding frost wyrm, white" },
    Look { display: 40036, source: 17890, donor: 1405, what: "riding phoenix" },
    Look { display: 40037, source: 25833, donor: 13972, what: "riding drake, bronze" },
];

const CDI: &str = "DBFilesClient\\CreatureDisplayInfo.dbc";
const CMD: &str = "DBFilesClient\\CreatureModelData.dbc";

/// What a build makes.
pub struct Built {
    /// The archive's files, `(path, bytes)`.
    pub files: Vec<(String, Vec<u8>)>,
    /// The two tables the server reads too (its `dbc` folder).
    pub tables: Vec<(String, Vec<u8>)>,
    /// One line per display: its model and its size in yards (the longest side of the model's
    /// box at the display's own scale), for the look table's size rules.
    pub report: Vec<String>,
    /// `(display, longest side in yards at the display's scale)`.
    pub sizes: Vec<(u32, f32)>,
}

/// A `.mdx` model path as the `.m2` file it names.
pub fn m2_path(mdx: &str) -> String {
    let lower = mdx.to_ascii_lowercase();
    if lower.ends_with(".mdx") || lower.ends_with(".mdl") {
        format!("{}.m2", &mdx[..mdx.len() - 4])
    } else {
        mdx.to_string()
    }
}

/// Convert the 3.3.5 model at `path` (`.m2`) with its skins and external animations.
pub fn convert_model(src: &Source, path: &str, anims: &benilla_formats::AnimDataCatalog) -> Result<Vec<u8>> {
    let m2 = src.read(path)?;
    let skins: Vec<Vec<u8>> = (0..m2conv::skin_count(&m2)?.clamp(1, 4))
        .map_while(|n| src.read(&m2conv::skin_file_name(path, n)).ok())
        .collect();
    let mut ext = HashMap::new();
    for (id, var) in m2conv::external_anims(&m2)? {
        if let Ok(b) = src.read(&m2conv::anim_file_name(path, id, var)) {
            ext.insert((id, var), b);
        }
    }
    let fallback = |id: u16| anims.fallback(id);
    m2conv::convert(&m2conv::Input {
        m2: &m2,
        skins: skins.iter().map(Vec::as_slice).collect(),
        anims: ext,
        playable_count: 203,
        fallback: &fallback,
    })
    .with_context(|| format!("converting {path}"))
}

/// World assets the 1.12 maps (stock tiles patched by the community layer, and its zones) name but
/// no 1.12 archive ships, found by `examples/wow_missing_refs`: TBC and Wrath doodads and
/// textures the 3.3.5 data has. Models are converted as the looks' are, with the files they
/// reference; textures are copied. A name 3.3.5 lacks is reported, not fatal.
pub const WORLD_FILES: &[&str] = &[
    r"World\Expansion02\Doodads\Generic\Human\HU_Brick_Pile01.m2",
    r"World\Expansion02\Doodads\Generic\Destructible\GuardTower\Dest_Tower_Damaged.m2",
    r"World\Expansion02\Doodads\Generic\Destructible\GuardTower\Dest_Tower_Destroyed.m2",
    r"World\Expansion02\Doodads\Zuldrak\Decorations\Zuldrak_Bats_Lower_01.m2",
    r"World\Expansion02\Doodads\Generic\Human\ND_Human_Gate_ClosedFX_Door.m2",
    r"World\Expansion01\Doodads\Netherstorm\CollectorTubes\CollectorTubes_Straight_Medium_Off.m2",
    r"World\Expansion01\Doodads\Netherstorm\CollectorTubes\CollectorTubes_Straight_Short.m2",
    r"World\Expansion01\Doodads\Netherstorm\FloatingRocks\Netherstorm_OverhangRock_Large_02.m2",
    r"World\Expansion01\Doodads\Netherstorm\Float_Particles\Netherstorm_Particles.m2",
    r"World\Expansion01\Doodads\Netherstorm\Lightning\NetherstormLightning01.m2",
    r"World\Expansion01\Doodads\Netherstorm\Lightning\NetherstormLightning05.m2",
    r"World\Expansion01\Doodads\Zangar\Road\ZangarWalkwayEnd01.m2",
    r"World\Expansion01\Doodads\Zangar\Road\ZangarWalkwayEnd02.m2",
    r"Creature\DemolisherCannonball\DemolisherCannonball.m2",
    r"Spells\Dest_Tower_Damaged_On_Hit_Smoke_FX.m2",
    r"Spells\Dest_Tower_Damaged_Smoke_FX.m2",
    r"World\Expansion02\Doodads\LakeWintergrasp\Trees\Wintergrasp_Leaves_03.blp",
    r"Dungeons\Textures\Utgarde\Exterior\ND_UtgardeExt_Base.blp",
    r"Dungeons\Textures\Rock\TD_KilJaeden_Rock_01.blp",
    r"Dungeons\Textures\Wood\MM_Support_Front_01XX.blp",
    r"Dungeons\Textures\Draenei\Shatrath\WD_ShattrathFacade1a.blp",
    r"Tileset\Expansion01\Silvermyst\SilvermystLeaf03.blp",
    r"Tileset\Expansion02\GrizzlyHills\GH_Sawdust1.blp",
];

/// Build the patch layer from the 3.3.5 chain `src` and the player's 1.12 chain `vanilla`.
pub fn build(src: &Source, vanilla: &mut Chain) -> Result<Built> {
    let cdi335 = Dbc::parse(&src.read(CDI)?)?;
    let cmd335 = Dbc::parse(&src.read(CMD)?)?;
    let mut cdi = Dbc::parse(&vanilla.read(CDI)?)?;
    let mut cmd = Dbc::parse(&vanilla.read(CMD)?)?;
    let anims = benilla_formats::load_anim_data_catalog(vanilla)?;

    let mut files: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let mut model_ids: HashMap<String, u32> = HashMap::new();
    let mut next_model = MODEL_BASE;
    let mut report = Vec::new();
    let mut sizes = Vec::new();
    let add_file = |files: &mut BTreeMap<String, Vec<u8>>, vanilla: &Chain, name: &str, report: &mut Vec<String>| {
        if vanilla.contains(name) || files.keys().any(|k| k.eq_ignore_ascii_case(name)) {
            return;
        }
        match src.read(name) {
            Ok(b) => {
                files.insert(name.to_string(), b);
            }
            Err(_) => report.push(format!("  missing in 3.3.5: {name}")),
        }
    };

    for look in LOOKS {
        let row = cdi335
            .row(look.source)
            .with_context(|| format!("3.3.5 display {} not found", look.source))?
            .clone();
        let mrow = cmd335
            .row(row[1])
            .with_context(|| format!("3.3.5 model {} not found", row[1]))?
            .clone();
        let mdx = cmd335.string(mrow[2]).to_string();
        let path = m2_path(&mdx);
        let donor = cdi.row(look.donor).with_context(|| format!("donor display {} not in 1.12", look.donor))?.clone();
        let donor_model = cmd.row(donor[1]).with_context(|| format!("donor model {}", donor[1]))?.clone();
        let dir = path.rsplit_once('\\').map_or("", |(d, _)| d).to_string();

        let key = path.to_ascii_lowercase();
        let (model_id, m2) = if let Some(&id) = model_ids.get(&key) {
            (id, files.iter().find(|(k, _)| k.eq_ignore_ascii_case(&path)).map(|(_, v)| v.clone()))
        } else if vanilla.contains(&path) {
            // Already a 1.12 model: point at its row.
            let id = cmd
                .records
                .iter()
                .find(|r| m2_path(cmd.string(r[2])).eq_ignore_ascii_case(&path))
                .map(|r| r[0])
                .with_context(|| format!("{path} is in 1.12 but has no CreatureModelData row"))?;
            model_ids.insert(key.clone(), id);
            (id, vanilla.read(&path).ok())
        } else {
            let m2 = convert_model(src, &path, &anims)?;
            let raw = src.read(&path)?;
            for f in m2conv::referenced_files(&raw)? {
                add_file(&mut files, vanilla, &f, &mut report);
            }
            files.insert(path.clone(), m2.clone());
            let id = next_model;
            next_model += 1;
            // CreatureModelData: the 3.3.5 row's first 16 fields, its 1.12-less ids borrowed.
            cmd.upsert(&[
                Field::U32(id),
                Field::U32(mrow[1]),
                Field::Str(mdx.clone()),
                Field::U32(mrow[3]),
                Field::U32(mrow[4]),
                Field::U32(donor_model[5]),
                Field::U32(donor_model[6]),
                Field::U32(mrow[7]),
                Field::U32(mrow[8]),
                Field::U32(mrow[9]),
                Field::U32(donor_model[10]),
                Field::U32(mrow[11]),
                Field::U32(mrow[12]),
                Field::U32(donor_model[13]),
                Field::U32(mrow[14]),
                Field::U32(mrow[15]),
            ])?;
            model_ids.insert(key.clone(), id);
            (id, Some(m2))
        };
        // The display's skins, beside the model.
        let mut tex = Vec::new();
        for f in 6..9 {
            let t = cdi335.string(row[f]).to_string();
            if !t.is_empty() {
                add_file(&mut files, vanilla, &format!("{dir}\\{t}.blp"), &mut report);
            }
            tex.push(t);
        }
        cdi.upsert(&[
            Field::U32(look.display),
            Field::U32(model_id),
            Field::U32(donor[2]),
            Field::U32(0),
            Field::U32(row[4]),
            Field::U32(row[5]),
            Field::Str(tex[0].clone()),
            Field::Str(tex[1].clone()),
            Field::Str(tex[2].clone()),
            Field::U32(donor[9]),
            Field::U32(donor[10]),
            Field::U32(donor[11]),
        ])?;
        let scale = f32::from_bits(row[4]);
        let side = m2
            .as_deref()
            .and_then(|b| benilla_formats::parse_m2_bounds(b).ok())
            .map_or(0.0, |b| (0..3).map(|k| b.bbox_max[k] - b.bbox_min[k]).fold(0.0f32, f32::max));
        sizes.push((look.display, side * scale));
        report.push(format!(
            "{} {:<40} {path} (3.3.5 display {}, model {model_id}) {:.2} yd at scale {scale}",
            look.display,
            look.what,
            look.source,
            side * scale
        ));
    }

    let tables = vec![(CDI.to_string(), cdi.to_bytes()), (CMD.to_string(), cmd.to_bytes())];
    for &name in WORLD_FILES {
        if vanilla.contains(name) || files.keys().any(|k| k.eq_ignore_ascii_case(name)) {
            continue;
        }
        if name.to_ascii_lowercase().ends_with(".m2") {
            match (convert_model(src, name, &anims), src.read(name)) {
                (Ok(m2), Ok(raw)) => {
                    for f in m2conv::referenced_files(&raw)? {
                        add_file(&mut files, vanilla, &f, &mut report);
                    }
                    files.insert(name.to_string(), m2);
                }
                _ => report.push(format!("  world model not converted from 3.3.5: {name}")),
            }
        } else {
            add_file(&mut files, vanilla, name, &mut report);
        }
    }
    let mut files: Vec<(String, Vec<u8>)> = files.into_iter().collect();
    files.extend(tables.iter().cloned());
    Ok(Built {
        files,
        tables,
        report,
        sizes,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn world_files_are_unique_wotlk_paths() {
        let mut seen = std::collections::HashSet::new();
        for f in super::WORLD_FILES {
            assert!(seen.insert(f.to_ascii_lowercase()), "{f} twice");
            let l = f.to_ascii_lowercase();
            assert!(l.ends_with(".m2") || l.ends_with(".blp"), "{f}");
        }
    }

    #[test]
    fn display_ids_are_unique_and_in_range() {
        let mut seen = std::collections::HashSet::new();
        for l in LOOKS {
            assert!(l.display >= DISPLAY_BASE && l.display < DISPLAY_BASE + 1000, "{}", l.display);
            assert!(seen.insert(l.display), "{} twice", l.display);
        }
        assert_eq!(m2_path("Creature\\Raptor\\RaptorPet.mdx"), "Creature\\Raptor\\RaptorPet.m2");
        assert_eq!(m2_path("CREATURE\\X\\Y.MDX"), "CREATURE\\X\\Y.m2");
    }

    /// The server's display rows (`33_wotlk_looks.sql`, `35_flying_companions.sql`) name exactly
    /// this table's displays.
    #[test]
    fn the_server_rows_match_the_table() {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../server/sql/custom/azeroth_warfare");
        let sql = ["33_wotlk_looks.sql", "35_flying_companions.sql"]
            .iter()
            .map(|f| std::fs::read_to_string(dir.join(f)).unwrap())
            .collect::<Vec<_>>()
            .join("\n");
        let mut ids: Vec<u32> = sql
            .lines()
            .filter(|l| l.starts_with("(4"))
            .map(|l| l[1..].split(',').next().unwrap().trim().parse().unwrap())
            .collect();
        ids.sort_unstable();
        let mut want: Vec<u32> = LOOKS.iter().map(|l| l.display).collect();
        want.sort_unstable();
        assert_eq!(ids, want);
    }

    /// Every imported model converts from the owner's 3.3.5 install and reads back through
    /// benilla's 1.12 readers: the mesh, the skin, the bones, the clips, particles and ribbons.
    /// Skipped without the install (`WOTLK_DATA`) or the 1.12 install (`WOW_DATA`).
    #[test]
    fn every_import_converts_and_reads_back() {
        let Some(src) = Source::open_default() else {
            eprintln!("no 3.3.5 install: skipped");
            return;
        };
        let Some(data) = super::super::vanilla_install() else {
            eprintln!("no 1.12 install: skipped");
            return;
        };
        let mut chain = super::super::stock_chain(&data).unwrap();
        let anims = benilla_formats::load_anim_data_catalog(&mut chain).unwrap();
        let cdi = Dbc::parse(&src.read(CDI).unwrap()).unwrap();
        let cmd = Dbc::parse(&src.read(CMD).unwrap()).unwrap();
        let mut done = std::collections::HashSet::new();
        for look in LOOKS {
            let row = cdi.row(look.source).unwrap();
            let path = m2_path(cmd.string(cmd.row(row[1]).unwrap()[2]));
            if !done.insert(path.clone()) {
                continue;
            }
            let raw = src.read(&path).unwrap();
            let out = convert_model(&src, &path, &anims).unwrap();
            let parsed = benilla_m2::parse_m2(&mut std::io::Cursor::new(out.as_slice()))
                .unwrap_or_else(|e| panic!("{path}: {e}"));
            let m = parsed.model();
            let rd = |b: &[u8], o: usize| u32::from_le_bytes(b[o..o + 4].try_into().unwrap());
            assert_eq!(m.vertices.len() as u32, rd(&raw, 0x3c), "{path} vertices");
            assert_eq!(m.bones.len() as u32, rd(&raw, 0x2c), "{path} bones");
            let skin = m.parse_embedded_skin(&out, 0).unwrap();
            assert!(!skin.batches().is_empty(), "{path} batches");
            let clips = benilla_formats::parse_m2_animations(&out);
            assert!(!clips.is_empty(), "{path} clips");
            assert!(m.owns_animation(0), "{path} stands");
            let emitters = benilla_formats::parse_m2_particle_emitters(&out).unwrap();
            assert_eq!(emitters.len() as u32, rd(&raw, 0x128), "{path} particles");
            let ribbons = benilla_formats::parse_m2_ribbon_emitters(&out).unwrap();
            assert_eq!(ribbons.len() as u32, rd(&raw, 0x120), "{path} ribbons");
            benilla_formats::parse_m2_bounds(&out).unwrap();
            // Every bone's rotation keeps all its keys across the sequences.
            let (nb, ob) = (rd(&raw, 0x2c) as usize, rd(&raw, 0x30) as usize);
            let obo = rd(&out, 0x38) as usize;
            for b in 0..nb {
                let t = ob + b * 0x58 + 0x24;
                let (n, outer) = (rd(&raw, t + 4) as usize, rd(&raw, t + 8) as usize);
                let mut keys = 0;
                let gseq = u16::from_le_bytes([raw[t + 2], raw[t + 3]]) != 0xffff;
                for j in 0..n {
                    keys += rd(&raw, outer + j * 8);
                }
                let got = rd(&out, obo + b * 0x6c + 0x28 + 0x0c);
                if gseq {
                    assert_eq!(got, keys, "{path} bone {b} global rotation keys");
                } else {
                    // Rest keys may be added for sequences without any, and keys of a missing
                    // .anim file dropped: never more keys than lists + sequences.
                    assert!(got <= keys + rd(&raw, 0x1c), "{path} bone {b}: {got} > {keys}");
                }
            }
        }
    }

    /// The companion looks with this layer's overrides (`33_wotlk_looks.sql` over 29 and 31):
    /// `id -> [(display, scale); 3]`.
    fn looks() -> (HashMap<u16, Vec<(u32, f32)>>, Vec<u16>) {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../server/sql/custom/azeroth_warfare");
        let mut out: HashMap<u16, Vec<(u32, f32)>> = HashMap::new();
        let mut overridden = Vec::new();
        for f in ["29_companion_looks.sql", "31_vanilla_companions.sql", "33_wotlk_looks.sql", "35_flying_companions.sql"] {
            for line in std::fs::read_to_string(dir.join(f)).unwrap().lines() {
                if !line.starts_with("UPDATE `fusion_companion_species`") {
                    continue;
                }
                let num = |s: &str| -> String { s.chars().take_while(|c| c.is_ascii_digit() || *c == '.').collect() };
                let id: u16 = num(&line[line.find("`id` = ").unwrap() + 7..]).parse().unwrap();
                let e = out.entry(id).or_insert_with(|| vec![(0, 1.0); 3]);
                for st in 1..=3 {
                    let d = format!("`stage{st}_display` = ");
                    let sc = format!("`stage{st}_scale` = ");
                    if let (Some(a), Some(b)) = (line.find(&d), line.find(&sc)) {
                        e[st - 1] = (num(&line[a + d.len()..]).parse().unwrap(), num(&line[b + sc.len()..]).parse().unwrap());
                    }
                }
                if f.starts_with("33") || f.starts_with("35") {
                    overridden.push(id);
                }
            }
        }
        (out, overridden)
    }

    /// Every line this layer changes still keeps the size rules of the look table (a pet-sized
    /// first form, each form clearly bigger), every new display is in the built layer's table
    /// and its model reads. Skipped without the built layer (private-assets/womw-patch).
    #[test]
    fn the_new_forms_keep_the_size_rules() {
        let patch = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../private-assets/womw-patch")
            .join(super::super::PATCH_ARCHIVE);
        let (Ok(layer), Some(data)) = (benilla_mpq::Archive::open(&patch), super::super::vanilla_install()) else {
            eprintln!("no built patch layer or 1.12 install: skipped");
            return;
        };
        let chain = benilla_formats::open_chain(&data).unwrap();
        let read = |n: &str| layer.read_file(n).or_else(|_| chain.read(n).map_err(|e| benilla_mpq::Error::NotFound(e.to_string())));
        let cdi = Dbc::parse(&read(CDI).unwrap()).unwrap();
        let cmd = Dbc::parse(&read(CMD).unwrap()).unwrap();
        let size = |display: u32| -> Result<f32, String> {
            let r = cdi.row(display).ok_or(format!("display {display} missing"))?;
            let m = cmd.row(r[1]).ok_or(format!("model {} missing", r[1]))?;
            let bytes = read(&m2_path(cmd.string(m[2]))).map_err(|e| format!("{display}: {e}"))?;
            let b = benilla_formats::parse_m2_bounds(&bytes).map_err(|e| format!("{display}: {e}"))?;
            let side = (0..3).map(|k| b.bbox_max[k] - b.bbox_min[k]).fold(0.0f32, f32::max);
            Ok(side * f32::from_bits(r[4]))
        };
        let (table, ids) = looks();
        let mut problems = Vec::new();
        for id in ids {
            let s: Vec<Result<f32, String>> = table[&id].iter().map(|&(d, sc)| size(d).map(|v| v * sc)).collect();
            let s: Vec<f32> = match s.into_iter().collect() {
                Ok(v) => v,
                Err(e) => {
                    problems.push(format!("{id}: {e}"));
                    continue;
                }
            };
            if s[0] > 2.5 || s[0] > 0.6 * s[2] || s[1] < 1.2 * s[0] || s[2] < 1.2 * s[1] {
                problems.push(format!("{id}: sizes {:.2} {:.2} {:.2}", s[0], s[1], s[2]));
            }
        }
        assert!(problems.is_empty(), "{problems:#?}");
    }

    /// The flying mounts' lines (`35_flying_companions.sql`): each is ridden as its last form, a
    /// riding model of this layer at scale 1, on the wolf's spells, and every form is a display
    /// 1.12 or this table has. With the built layer, the ridden model is a mount's (its path names
    /// a mount or a riding model, which authors the seat).
    #[test]
    fn the_flying_lines_ride_their_riding_models() {
        let sql = std::fs::read_to_string(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../server/sql/custom/azeroth_warfare/35_flying_companions.sql"),
        )
        .unwrap();
        let num = |s: &str| -> String { s.chars().take_while(|c| c.is_ascii_digit() || *c == '.').collect() };
        let field = |line: &str, key: &str| -> Option<String> { line.find(key).map(|a| num(&line[a + key.len()..])) };
        let mut lines = 0;
        for line in sql.lines().filter(|l| l.starts_with("UPDATE `fusion_companion_species`")) {
            let id = field(line, "`id` = ").unwrap();
            let ride: u32 = field(line, "`ride_display` = ").expect("a ride display").parse().unwrap();
            let last: u32 = field(line, "`stage3_display` = ").unwrap().parse().unwrap();
            let scale: f32 = field(line, "`stage3_scale` = ").unwrap().parse().unwrap();
            assert_eq!(ride, last, "species {id} rides as its last form");
            assert_eq!(scale, 1.0, "species {id}'s ridden form is at the size it is ridden at");
            let look = LOOKS.iter().find(|l| l.display == ride).unwrap_or_else(|| panic!("{ride} is not imported"));
            assert!(look.what.starts_with("riding "), "species {id} rides {}", look.what);
            for st in 1..=2 {
                let d: u32 = field(line, &format!("`stage{st}_display` = ")).unwrap().parse().unwrap();
                assert!(d < DISPLAY_BASE || LOOKS.iter().any(|l| l.display == d), "species {id} stage {st}: {d}");
            }
            lines += 1;
        }
        assert_eq!(lines, 9);
        // The species rows ride on the wolf's spells (580 / 23250) at stage 3, with that display.
        let rows: Vec<&str> = sql.lines().filter(|l| l.starts_with("(2") && l.contains(", '")).collect();
        assert_eq!(rows.len(), 9);
        for row in rows {
            let tail: Vec<&str> = row.trim_end_matches([',', ';', ')']).rsplit(", ").take(4).collect();
            assert_eq!(&tail[1..], ["23250", "580", "3"], "{row}");
            assert!(LOOKS.iter().any(|l| l.display.to_string() == tail[0]), "{row}");
        }

        let patch = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../private-assets/womw-patch")
            .join(super::super::PATCH_ARCHIVE);
        let Ok(layer) = benilla_mpq::Archive::open(&patch) else {
            eprintln!("no built patch layer: model paths skipped");
            return;
        };
        let cdi = Dbc::parse(&layer.read_file(CDI).unwrap()).unwrap();
        let cmd = Dbc::parse(&layer.read_file(CMD).unwrap()).unwrap();
        for look in LOOKS.iter().filter(|l| l.display >= 40019 && l.what.starts_with("riding ")) {
            let r = cdi.row(look.display).unwrap();
            let path = cmd.string(cmd.row(r[1]).unwrap()[2]).to_ascii_lowercase();
            // The Netherwing riding drakes are the one riding model not named so in 3.3.5.
            let netherwing = path.ends_with("netherdrake\\netherdrake.mdx");
            assert!(path.contains("mount") || path.contains("riding") || netherwing, "{}: {path}", look.display);
        }
    }
}
