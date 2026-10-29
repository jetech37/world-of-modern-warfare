//! `areas` and `build`: pick the tiles of the chosen zones, write the chunk-to-area table the
//! server import places spawns with, and pack the patch (tiles, our WDTs with the new tiles
//! flagged, every new file they and the imported displays load, and the merged tables).

use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::fmt::Write as _;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};

use crate::dbc::{merge_rows, string_columns, Dbc};
use crate::refs::{self, normalize};
use crate::source::{Layer, Ours};
use crate::tiles::{self, Tile, TileKind};

/// The community tables whose new rows the patch carries. Their layouts equal stock 1.12's (a
/// different field count is refused). `Spell` is left out on purpose: its new rows would be
/// cherry-picked per spell, never merged wholesale.
pub const TABLES: &[&str] = &[
    "Map",
    "AreaTable",
    "AreaPOI",
    "AreaTrigger",
    "WorldMapArea",
    "WorldMapContinent",
    "WorldMapOverlay",
    "LoadingScreens",
    "Light",
    "LightParams",
    "LightIntBand",
    "LightFloatBand",
    "LightSkybox",
    "ZoneMusic",
    "ZoneIntroMusicTable",
    "SoundAmbience",
    "SoundEntries",
    "WMOAreaTable",
    "TaxiNodes",
    "TaxiPath",
    "TaxiPathNode",
    "CreatureDisplayInfo",
    "CreatureDisplayInfoExtra",
    "CreatureModelData",
    "CreatureSoundData",
    "GameObjectDisplayInfo",
    "ItemDisplayInfo",
    "Faction",
    "FactionTemplate",
    "GroundEffectTexture",
    "GroundEffectDoodad",
    "Spell",
    "SpellIcon",
    "SpellVisual",
    "SpellVisualKit",
    "SpellVisualEffectName",
    "SpellDuration",
    "SpellRange",
    "SpellCastTimes",
    "SpellRadius",
    "SpellItemEnchantment",
    "SpellFocusObject",
    "ItemSet",
    "ItemRandomProperties",
    "Lock",
];

/// Map ids the community's row wins on: the owner keeps the community client's maps 800-822 (our
/// WotLK layer's first Dalaran map row 800 gives way).
/// Files the community's tiles and buildings name that neither side ships (`client/examples/
/// wow_missing_refs`, the Wrath ones aside, which the WotLK import adds): each is written into the
/// patch under its missing name as a copy of the closest stock file, so the placement draws
/// something fitting instead of nothing (or, for `InvisibleStalker`, a model that draws nothing:
/// the placement stripped). Rewriting the names in the tiles would shift their offset tables.
/// `error_N.blp` are placeholder names baked into community tiles: one stock ground texture
/// stands in for all three, as an alias cannot vary by tile.
pub const SUBSTITUTES: &[(&str, &str)] = &[
    (r"error_1.blp", r"Tileset\Elwynn\ElwynnDirtBase.blp"),
    (r"error_2.blp", r"Tileset\Elwynn\ElwynnDirtBase.blp"),
    (r"error_3.blp", r"Tileset\Elwynn\ElwynnDirtBase.blp"),
    (r"Textures\wetlands_grimbatol\MM_IRNFG_TRIM_02Flip", r"Dungeons\Textures\trim\MM_IRNFG_TRIM_02.blp"),
    (r"Tileset\Darkshore\DarkshoreSand2.blp", r"Tileset\Darkshore\DarkshoreSand.blp"),
    (r"Tileset\Kalidar\KalidarRootsAlpha.blp", r"Tileset\Kalidar\KalidarRoots.blp"),
    (r"World\Generic\Human\Buildings\Houses\Reflection.blp", r"Creature\Gyrocopter\SmoothReflect01.blp"),
    (r"World\Environment\Doodad\Dragonblight\RedDragonflight\Red_Dragon_Shrine\Tex\DB_Red_Dragon_Tex_Trunk2_Dead.blp", r"World\Environment\Doodad\Dragonblight\RedDragonflight\Red_Dragon_Shrine\Tex\DB_Red_Dragon_Tex_Trunk2.blp"),
    (r"Dungeons\Textures\Windows\MM_NE_DS_Windows_03.blp", r"Dungeons\Textures\Windows\MM_NE_DS_Windows_01.blp"),
    (r"Dungeons\Textures\NightElfRuins\NightElf_TreeTrunk_Restored.blp", r"World\Generic\NightElf\Passive Doodads\TeleportTree\KalidarTreeTrunk01.blp"),
    (r"Tileset\Expansion01\Silvermyst\SilvermystGrass05e.blp", r"Tileset\Expansion01\Silvermyst\SilvermystGrass04e.blp"),
    (r"Creature\AllianceBrassCannon\AllianceBrassCannon_Flat.m2", r"World\Expansion02\Doodads\Icecrown\Alliance_Gunship\AGS_BrassCannon.m2"),
    (r"World\Azeroth\Karazahn\ActiveDoodads\Karazhan_Red_Curtain.m2", r"Creature\InvisibleStalker\InvisibleStalker.m2"),
    (r"World\Goober\G_PirateFlag_01.m2", r"Creature\InvisibleStalker\InvisibleStalker.m2"),
    (r"World\Goober\g_runegroundgreen01b.m2", r"World\Goober\G_RuneGroundGreen01.m2"),
    (r"World\Goober\G_MummyTroll.m2", r"World\Generic\Troll\Passive Doodads\MummyTrolls\MummyTroll01.m2"),
    (r"World\Generic\Human\Passive Doodads\Outposts\GeneralOutpost08.m2", r"World\Generic\Human\Passive Doodads\Outposts\GeneralOutpost04.m2"),
    (r"World\Kalimdor\Feralas\PassiveDoodads\Tree\FeralasTreeRoots05.m2", r"World\Kalimdor\Feralas\PassiveDoodads\Tree\FeralasTreeRoots03.m2"),
    (r"World\Lordaeron\Stratholme\PassiveDoodads\FX\StratholmeFloatingEmbers_02.m2", r"World\Lordaeron\Stratholme\PassiveDoodads\FX\StratholmeFloatingEmbers.m2"),
];

pub fn community_map_wins(id: u32) -> bool {
    (800..=822).contains(&id)
}

pub struct Options {
    /// Zone ids (`AreaTable` rows with no parent); empty: every zone.
    pub zones: BTreeSet<u32>,
    pub out: PathBuf,
    /// The display ids the server import uses (`creature|gameobject|item <id>` lines).
    pub displays: Option<PathBuf>,
    /// Their instanced maps to bring whole (WDT, WDL, every tile, the global WMO).
    pub maps: BTreeSet<u32>,
    /// The import's id map (`kind old new`): renumbered spells change their `Spell` row ids too.
    pub idmap: Option<PathBuf>,
    /// Close the void holes in their copies of the warded cities' tiles (`holes`).
    pub close_city_holes: bool,
}

fn options(rest: &[String]) -> Result<Options> {
    let mut zones = BTreeSet::new();
    let (mut out, mut displays, mut idmap) = (None, None, None);
    let mut close_city_holes = false;
    let mut maps = BTreeSet::new();
    let mut it = rest.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--zones" => {
                let v = it.next().context("--zones <id,id|all>")?;
                if v != "all" {
                    for z in v.split(',') {
                        zones.insert(z.trim().parse().context("a zone id")?);
                    }
                }
            }
            "--out" => out = it.next().map(PathBuf::from),
            "--displays" => displays = it.next().map(PathBuf::from),
            "--idmap" => idmap = it.next().map(PathBuf::from),
            "--close-city-holes" => close_city_holes = true,
            "--brand-rules" => {
                it.next();
            }
            "--maps" => {
                for part in it.next().context("--maps <a-b,c>")?.split(',') {
                    let (lo, hi) = part.split_once('-').unwrap_or((part, part));
                    maps.extend(lo.trim().parse::<u32>()?..=hi.trim().parse::<u32>()?);
                }
            }
            _ => bail!("unknown argument {a}"),
        }
    }
    Ok(Options {
        zones,
        out: out.context("--out <dir>")?,
        displays,
        maps,
        idmap,
        close_city_holes,
    })
}

/// The chosen tiles that are warded-city tiles, with their void holes closed: `(path, bytes,
/// cells closed)`. `read` gives any archive file (a tile, a WMO root).
pub fn close_city_holes(chosen: &[&Tile], read: &dyn Fn(&str) -> Option<Vec<u8>>) -> Vec<(String, Vec<u8>, usize)> {
    let cities = crate::holes::city_tiles();
    let mut out = Vec::new();
    for t in chosen {
        let (map, x, y) = (t.continent.map, t.x, t.y);
        if !cities.contains(&(map, x, y)) {
            continue;
        }
        let Some(raw) = read(&t.path()) else { continue };
        let vols = crate::holes::volumes_around(map, x, y, read);
        if let Some((bytes, n)) = crate::holes::close_tile(map, &raw, &vols) {
            out.push((t.path(), bytes, n));
        }
    }
    out
}

/// `AreaTable` zone lookup: an area's zone is its parent, or itself at the top.
pub struct Zones {
    table: Dbc,
}

impl Zones {
    pub fn new(table: Dbc) -> Self {
        Self { table }
    }

    pub fn zone_of(&self, area: u32) -> u32 {
        match self.table.row(area) {
            Some(r) if r[2] != 0 => r[2],
            _ => area,
        }
    }

    pub fn name(&self, area: u32) -> String {
        self.table
            .row(area)
            .map(|r| self.table.string(r[11]).to_string())
            .unwrap_or_default()
    }
}

/// The merged tiles for `zones`: every changed or new tile with a chunk in one of them, plus the
/// new open-sea tiles (area 0 only) touching those, so the sea around an island has no hole.
pub fn select_tiles<'a>(all: &'a [Tile], zones: &Zones, wanted: &BTreeSet<u32>) -> Vec<&'a Tile> {
    let in_zone =
        |t: &Tile| wanted.is_empty() || t.areas.keys().any(|&a| wanted.contains(&zones.zone_of(a)));
    let core: HashSet<(u32, u32, u32)> = all
        .iter()
        .filter(|t| t.merged() && in_zone(t))
        .map(|t| (t.continent.map, t.x, t.y))
        .collect();
    let near = |t: &Tile| {
        (-1i32..=1).any(|dx| {
            (-1i32..=1).any(|dy| {
                core.contains(&(
                    t.continent.map,
                    (t.x as i32 + dx) as u32,
                    (t.y as i32 + dy) as u32,
                ))
            })
        })
    };
    all.iter()
        .filter(|t| {
            t.merged()
                && (core.contains(&(t.continent.map, t.x, t.y))
                    || (t.kind == TileKind::New && t.areas.keys().all(|&a| a == 0) && near(t)))
        })
        .collect()
}

/// Our WDT with `tiles` flagged in `MAIN` (bit 0, "has an ADT"); every other byte is ours.
pub fn flag_tiles(wdt: &[u8], tiles: &[(u32, u32)]) -> Result<Vec<u8>> {
    let mut out = wdt.to_vec();
    let mut pos = 0;
    while pos + 8 <= out.len() {
        let size = u32::from_le_bytes(out[pos + 4..pos + 8].try_into().unwrap()) as usize;
        if &out[pos..pos + 4] == b"NIAM" {
            if size < 64 * 64 * 8 || pos + 8 + size > out.len() {
                bail!("short MAIN");
            }
            for &(x, y) in tiles {
                let at = pos + 8 + ((y as usize) * 64 + x as usize) * 8;
                out[at] |= 1;
            }
            return Ok(out);
        }
        pos += 8 + size;
    }
    bail!("WDT has no MAIN")
}

/// The chunk-to-area table for the import: `map tile_x tile_y chunk_x chunk_y area zone`.
/// [`areas_tsv`] over every tile of both continents: the merged tiles as theirs, the rest as ours
/// (a zone of theirs on terrain we kept, such as GM Island, still places its spawns).
pub fn all_areas_tsv(chosen: &[&Tile], ours: &Ours, zones: &Zones) -> Result<String> {
    let have: HashSet<(u32, u32, u32)> =
        chosen.iter().map(|t| (t.continent.map, t.x, t.y)).collect();
    let mut kept = Vec::new();
    for c in tiles::CONTINENTS {
        for x in 0..64 {
            for y in 0..64 {
                let p = tiles::adt_path(c, x, y);
                if have.contains(&(c.map, x, y)) || !ours.contains(&p) {
                    continue;
                }
                kept.push(tiles::inspect(c, x, y, &ours.read(&p)?, TileKind::Same)?);
            }
        }
    }
    let all: Vec<&Tile> = chosen.iter().copied().chain(kept.iter()).collect();
    Ok(areas_tsv(&all, zones))
}

pub fn areas_tsv(tiles: &[&Tile], zones: &Zones) -> String {
    let mut s = String::from("map\ttile_x\ttile_y\tchunk_x\tchunk_y\tarea\tzone\n");
    for t in tiles {
        for &(cx, cy, a) in &t.chunk_areas {
            let _ = writeln!(
                s,
                "{}\t{}\t{}\t{cx}\t{cy}\t{a}\t{}",
                t.continent.map,
                t.x,
                t.y,
                zones.zone_of(a)
            );
        }
    }
    s
}

/// The server's `area_template` rows for the community areas we lack, from their 1.12-layout
/// `AreaTable` (0 id, 1 map, 2 parent zone, 3 explore bit, 4 flags, 10 exploration level, 11 name,
/// 20 faction group, 21 liquid type): `entry map zone explore flags level name team liquid`.
pub fn area_template_tsv(theirs: &Dbc, ours: &HashSet<u32>, wanted: &BTreeSet<u32>) -> String {
    let mut s = String::from(
        "entry\tmap_id\tzone_id\texplore_flag\tflags\tarea_level\tname\tteam\tliquid_type\n",
    );
    for r in &theirs.records {
        let zone = if r[2] != 0 { r[2] } else { r[0] };
        if ours.contains(&r[0]) || !(wanted.is_empty() || wanted.contains(&zone)) {
            continue;
        }
        let name = theirs.string(r[11]).replace(['\t', '\n'], " ");
        let _ = writeln!(
            s,
            "{}\t{}\t{}\t{}\t{}\t{}\t{name}\t{}\t{}",
            r[0], r[1], r[2], r[3], r[4], r[10], r[20], r[21]
        );
    }
    s
}

/// The files the display rows the import uses load: creature models and skins, baked NPC
/// textures, game object models, item models, textures and icons.
pub fn display_roots(
    displays: &BTreeMap<String, BTreeSet<u32>>,
    community: &Layer,
    tables: &BTreeMap<String, Dbc>,
) -> Vec<String> {
    let mut roots = Vec::new();
    let table = |n: &str| tables.get(n);
    let strings = |t: &Dbc, r: &Vec<u32>| -> Vec<String> {
        string_columns(t, t)
            .into_iter()
            .map(|c| t.string(r[c]).to_string())
            .filter(|s| !s.is_empty())
            .collect()
    };
    if let (Some(cdi), Some(cmd)) = (table("CreatureDisplayInfo"), table("CreatureModelData")) {
        let extra = table("CreatureDisplayInfoExtra");
        for id in displays.get("creature").into_iter().flatten() {
            let Some(r) = cdi.row(*id) else { continue };
            let model = cmd
                .row(r[1])
                .and_then(|m| strings(cmd, m).into_iter().next())
                .map(|p| normalize(&p));
            if let Some(m) = &model {
                roots.push(m.clone());
                let dir = m.rsplit_once('\\').map(|(d, _)| d).unwrap_or("");
                for skin in strings(cdi, r) {
                    roots.push(format!("{dir}\\{skin}.blp"));
                }
            }
            if let (Some(x), Some(e)) = (r.get(3).filter(|&&e| e != 0), extra) {
                if let Some(er) = e.row(*x) {
                    for bake in strings(e, er) {
                        roots.push(format!("Textures\\BakedNpcTextures\\{bake}"));
                    }
                }
            }
        }
    }
    if let Some(god) = table("GameObjectDisplayInfo") {
        for id in displays.get("gameobject").into_iter().flatten() {
            if let Some(r) = god.row(*id) {
                roots.extend(strings(god, r).into_iter().take(1).map(|p| normalize(&p)));
            }
        }
    }
    if let Some(idi) = table("ItemDisplayInfo") {
        const OBJ: [&str; 7] = [
            "Weapon", "Shield", "Head", "Shoulder", "Cape", "Quiver", "Ammo",
        ];
        const TEX: [&str; 8] = [
            "ArmUpperTexture",
            "ArmLowerTexture",
            "HandTexture",
            "TorsoUpperTexture",
            "TorsoLowerTexture",
            "LegUpperTexture",
            "LegLowerTexture",
            "FootTexture",
        ];
        for id in displays.get("item").into_iter().flatten() {
            let Some(r) = idi.row(*id) else { continue };
            for s in strings(idi, r) {
                let base = s.trim_end_matches(".blp").trim_end_matches(".mdx");
                let mut cands = vec![format!("Interface\\Icons\\{base}.blp")];
                for d in OBJ {
                    cands.push(format!("Item\\ObjectComponents\\{d}\\{base}.m2"));
                    cands.push(format!("Item\\ObjectComponents\\{d}\\{base}.blp"));
                }
                for d in TEX {
                    for g in ["U", "M", "F"] {
                        cands.push(format!("Item\\TextureComponents\\{d}\\{base}_{g}.blp"));
                    }
                }
                roots.extend(cands.into_iter().filter(|c| community.contains(c)));
            }
        }
    }
    roots
}

/// `WorldMapArea` (0 id, 1 map, 2 area, 3 directory, 4-7 bounds): the rows whose art and bounds
/// we take from them, the continents (area 0 on maps 0 and 1) and the merged zones (`zones`, empty:
/// all of them on maps 0 and 1). A continent row of theirs under another id than ours takes our
/// id first, so a map never gets a second continent row.
pub fn align_world_map_areas(theirs: &mut Dbc, ours: &Dbc, zones: &BTreeSet<u32>) -> HashSet<u32> {
    let mut won = HashSet::new();
    let dir = |t: &Dbc, r: &[u32]| t.string(r[3]).to_ascii_lowercase();
    // A map-0/1 area-0 row that is not the continent itself (their "World" overview) is dropped.
    let ours_dirs: Vec<(u32, String, u32)> = ours
        .records
        .iter()
        .filter(|o| o[2] == 0 && o[1] <= 1)
        .map(|o| (o[1], dir(ours, o), o[0]))
        .collect();
    let extra: Vec<usize> = theirs
        .records
        .iter()
        .enumerate()
        .filter(|(_, r)| {
            r[1] <= 1
                && r[2] == 0
                && !ours_dirs
                    .iter()
                    .any(|(m, d, _)| *m == r[1] && *d == dir(theirs, r))
        })
        .map(|(i, _)| i)
        .collect();
    for i in extra.into_iter().rev() {
        theirs.records.remove(i);
    }
    for r in &mut theirs.records {
        if r[1] > 1 {
            continue;
        }
        if r[2] == 0 {
            if let Some(o) = ours.records.iter().find(|o| o[1] == r[1] && o[2] == 0) {
                r[0] = o[0];
            }
            won.insert(r[0]);
        } else if zones.is_empty() || zones.contains(&r[2]) {
            won.insert(r[0]);
        }
    }
    won
}

fn refs_is_group(p: &str) -> bool {
    let l = p.to_ascii_lowercase();
    l.len() > 8 && l.ends_with(".wmo") && l.as_bytes()[l.len() - 8] == b'_'
}

/// `TaxiPathNode` (0 id, 1 path, 2 index, ...): a path we have keeps exactly its nodes. Their new
/// rows on one of our paths (they renumber nodes, so their ids do not line up with ours) would give
/// it a second set of nodes: flights and the zeppelins and boats that ride those paths then run
/// through the world. Only nodes of paths new to us come in.
pub fn keep_our_paths(theirs: &mut Dbc, ours: &Dbc) {
    let paths: HashSet<u32> = ours.records.iter().map(|r| r[1]).collect();
    theirs.records.retain(|r| !paths.contains(&r[1]));
}

/// `AreaTable` ids (zones and their subareas) of the zones whose `WorldMapArea` row on maps 0/1 is
/// new to us: their rows replace ours (sound, music, parent), so the zone lists as theirs.
pub fn new_map_zone_areas(community: &Layer, ours: &Ours, areas: &Dbc) -> Result<HashSet<u32>> {
    let p = "DBFilesClient\\WorldMapArea.dbc";
    let (t, o) = (
        Dbc::parse(&community.read(p)?)?,
        Dbc::parse(&ours.read(p)?)?,
    );
    let zones: HashSet<u32> = t
        .records
        .iter()
        .filter(|r| {
            r[1] <= 1
                && r[2] != 0
                && o.row(r[0]).is_none()
                && !o.records.iter().any(|q| q[2] == r[2])
        })
        .map(|r| r[2])
        .collect();
    Ok(areas
        .records
        .iter()
        .filter(|r| zones.contains(&r[0]) || zones.contains(&r[2]))
        .map(|r| r[0])
        .collect())
}

/// The art files of the given `WorldMapArea` rows: tiles, highlight, overlays and the `.zmp`.
pub fn world_map_candidates(wma: &Dbc, wmo: Option<&Dbc>, rows: &HashSet<u32>) -> Vec<String> {
    let mut out = Vec::new();
    for r in wma.records.iter().filter(|r| rows.contains(&r[0])) {
        let dir = wma.string(r[3]).to_string();
        if dir.is_empty() {
            continue;
        }
        out.push(format!("Interface\\WorldMap\\{dir}.zmp"));
        out.push(format!("Interface\\WorldMap\\{dir}\\{dir}Highlight.blp"));
        for i in 1..=12 {
            out.push(format!("Interface\\WorldMap\\{dir}\\{dir}{i}.blp"));
        }
        if let Some(wmo) = wmo {
            let cols = string_columns(wmo, wmo);
            for o in wmo.records.iter().filter(|o| o[1] == r[0]) {
                for &c in &cols {
                    let n = wmo.string(o[c]);
                    if !n.is_empty() {
                        for i in 1..=12 {
                            out.push(format!("Interface\\WorldMap\\{dir}\\{n}{i}.blp"));
                        }
                    }
                }
            }
        }
    }
    out
}

/// Of `candidates`, those the community has with bytes unlike ours, and not from one of our own
/// patch layers (our layers' art always wins).
pub fn edited_world_map_files(
    candidates: &[String],
    community: &Layer,
    ours: &Ours,
) -> Vec<String> {
    candidates
        .iter()
        .filter(|p| community.contains(p) && !ours.in_our_layers(p))
        .filter(|p| !ours.contains(p) || community.read(p).ok() != ours.read(p).ok())
        .cloned()
        .collect()
}

/// One kind's `old -> new` pairs from the import's id map (empty without a map).
pub fn read_idmap(p: Option<&Path>, kind: &str) -> Result<BTreeMap<u32, u32>> {
    let mut out = BTreeMap::new();
    let Some(p) = p else { return Ok(out) };
    for line in std::fs::read_to_string(p)?.lines() {
        let mut w = line.split('\t');
        if let (Some(k), Some(a), Some(b)) = (w.next(), w.next(), w.next()) {
            if k == kind {
                if let (Ok(a), Ok(b)) = (a.parse(), b.parse()) {
                    out.insert(a, b);
                }
            }
        }
    }
    Ok(out)
}

/// Give rows (by field 0) the numbers `map` assigns.
pub fn renumber_rows(t: &mut Dbc, map: &BTreeMap<u32, u32>) {
    for r in &mut t.records {
        if let Some(&n) = map.get(&r[0]) {
            r[0] = n;
        }
    }
}

fn read_displays(p: &Path) -> Result<BTreeMap<String, BTreeSet<u32>>> {
    let mut out: BTreeMap<String, BTreeSet<u32>> = BTreeMap::new();
    for line in std::fs::read_to_string(p)?.lines() {
        let mut w = line.split_whitespace();
        if let (Some(k), Some(v)) = (w.next(), w.next()) {
            if let Ok(id) = v.parse() {
                out.entry(k.to_string()).or_default().insert(id);
            }
        }
    }
    Ok(out)
}

fn write_area_template(community: &Layer, ours: &Ours, o: &Options) -> Result<()> {
    let theirs = Dbc::parse(&community.read("DBFilesClient\\AreaTable.dbc")?)?;
    let mine = Dbc::parse(&ours.read("DBFilesClient\\AreaTable.dbc")?)?.ids();
    std::fs::write(
        o.out.join("area_template.tsv"),
        area_template_tsv(&theirs, &mine, &o.zones),
    )?;
    Ok(())
}

/// `areas`: the tile choice and the chunk-to-area table only (the server import's input).
pub fn areas(community: &Layer, ours: &Ours, rest: &[String]) -> Result<()> {
    let o = options(rest)?;
    let zones = Zones::new(Dbc::parse(
        &community.read("DBFilesClient\\AreaTable.dbc")?,
    )?);
    let all = tiles::community_tiles(community, ours)?;
    let chosen = select_tiles(&all, &zones, &o.zones);
    std::fs::create_dir_all(&o.out)?;
    std::fs::write(
        o.out.join("areas.tsv"),
        all_areas_tsv(&chosen, ours, &zones)?,
    )?;
    write_area_template(community, ours, &o)?;
    println!(
        "{} tiles; {}",
        chosen.len(),
        o.out.join("areas.tsv").display()
    );
    Ok(())
}

/// `build`: the patch archive, the server's tables and a report.
pub fn run(community: &Layer, ours: &Ours, rest: &[String]) -> Result<()> {
    let o = options(rest)?;
    // The branding rules first: a build never runs without them.
    let brand = crate::brand::Rules::find(rest)?;
    std::fs::create_dir_all(&o.out)?;
    let zones = Zones::new(Dbc::parse(
        &community.read("DBFilesClient\\AreaTable.dbc")?,
    )?);
    let all = tiles::community_tiles(community, ours)?;
    let chosen = select_tiles(&all, &zones, &o.zones);
    if chosen.is_empty() {
        bail!("no tiles for zones {:?}", o.zones);
    }
    std::fs::write(
        o.out.join("areas.tsv"),
        all_areas_tsv(&chosen, ours, &zones)?,
    )?;
    write_area_template(community, ours, &o)?;
    // The server's path tiles to rebuild (aw.ps1 Update-CommunityMapData): `map x y`.
    let list: String = chosen
        .iter()
        .map(|t| format!("{} {} {}\n", t.continent.map, t.x, t.y))
        .collect();
    // A whole instanced map is one line: `map`.
    let list: String = list + &o.maps.iter().map(|m| format!("{m}\n")).collect::<String>();
    std::fs::write(o.out.join("tiles.txt"), list)?;
    let mut report = String::new();
    let _ = writeln!(report, "zones: {:?}", o.zones);

    // Files: (path, source). A tile or table we write ourselves is held as bytes.
    let mut made: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let mut copied: BTreeSet<String> = BTreeSet::new();
    let mut roots: Vec<String> = Vec::new();
    for t in &chosen {
        copied.insert(t.path());
        roots.extend(t.textures.iter().cloned());
        roots.extend(t.models.iter().cloned());
        roots.extend(t.wmos.iter().cloned());
        let _ = writeln!(report, "tile {} {:?}", t.path(), t.kind);
    }
    // Their copies of the warded cities' tiles: the void holes closed (theirs win over patch-9,
    // which closes ours). The WMOs that keep a hole are read through theirs, then ours.
    if o.close_city_holes {
        let read = |p: &str| community.read(p).ok().or_else(|| ours.read(p).ok());
        for (path, bytes, n) in close_city_holes(&chosen, &read) {
            let _ = writeln!(report, "tile {path}: {n} void hole cells closed");
            made.insert(path, bytes);
        }
    }
    // Our WDTs, with the new tiles flagged.
    for c in tiles::CONTINENTS {
        let new: Vec<(u32, u32)> = chosen
            .iter()
            .filter(|t| t.continent == c && t.kind == TileKind::New)
            .map(|t| (t.x, t.y))
            .collect();
        if !new.is_empty() {
            let wdt = tiles::wdt_path(c);
            made.insert(wdt.clone(), flag_tiles(&ours.read(&wdt)?, &new)?);
        }
    }

    // Tables: ours (with our layers) plus the community's new rows.
    let mut tables: BTreeMap<String, Dbc> = BTreeMap::new();
    let mut theirs_tables: BTreeMap<String, Dbc> = BTreeMap::new();
    for name in TABLES {
        let path = format!("DBFilesClient\\{name}.dbc");
        let (Ok(t), Ok(mine)) = (community.read(&path), ours.read(&path)) else {
            let _ = writeln!(report, "table {name}: not on both sides, skipped");
            continue;
        };
        let mut theirs = Dbc::parse(&t)?;
        if *name == "Spell" {
            renumber_rows(&mut theirs, &read_idmap(o.idmap.as_deref(), "spell")?);
        }
        let mut merged = Dbc::parse(&mine)?;
        let mut wma_win = HashSet::new();
        if *name == "WorldMapArea" {
            wma_win = align_world_map_areas(&mut theirs, &merged, &o.zones);
        }
        if *name == "TaxiPathNode" {
            keep_our_paths(&mut theirs, &merged);
        }
        if *name == "AreaTable" {
            wma_win = new_map_zone_areas(community, ours, &theirs)?;
        }
        let r = match merge_rows(
            &mut merged,
            &theirs,
            |_| true,
            |id| (*name == "Map" && community_map_wins(id)) || wma_win.contains(&id),
        ) {
            Ok(r) => r,
            Err(e) => {
                let _ = writeln!(report, "table {name}: {e}, skipped");
                continue;
            }
        };
        let _ = writeln!(
            report,
            "table {name}: +{} rows, {} conflicts kept ours, {} replaced {:?}; string columns {:?}",
            r.added.len(),
            r.conflicts.len() - r.replaced.len(),
            r.replaced.len(),
            r.replaced,
            r.string_columns
        );
        if !r.conflicts.is_empty() {
            let _ = writeln!(report, "  conflicting ids {name}: {:?}", r.conflicts);
        }
        if !r.added.is_empty() || !r.replaced.is_empty() {
            made.insert(path, merged.to_bytes());
        }
        tables.insert(name.to_string(), merged);
        theirs_tables.insert(name.to_string(), theirs);
    }

    // Loading screens and world-map art the new rows name.
    if let Some(ls) = theirs_tables.get("LoadingScreens") {
        for r in &ls.records {
            for c in string_columns(ls, ls) {
                let s = ls.string(r[c]);
                if s.to_ascii_lowercase().ends_with(".blp") {
                    roots.push(s.to_string());
                }
            }
        }
    }
    if let (Some(wma), Some(wmo)) = (
        theirs_tables.get("WorldMapArea"),
        theirs_tables.get("WorldMapOverlay"),
    ) {
        let cols = string_columns(wma, wma);
        for r in &wma.records {
            let Some(&c) = cols.first() else { break };
            let dir = wma.string(r[c]).to_string();
            if dir.is_empty() {
                continue;
            }
            for i in 1..=12 {
                roots.push(format!("Interface\\WorldMap\\{dir}\\{dir}{i}.blp"));
            }
            for orow in wmo.records.iter().filter(|o| o[1] == r[0]) {
                for oc in string_columns(wmo, wmo) {
                    let n = wmo.string(orow[oc]);
                    if n.is_empty() {
                        continue;
                    }
                    for i in 1..=12 {
                        roots.push(format!("Interface\\WorldMap\\{dir}\\{n}{i}.blp"));
                    }
                }
            }
        }
    }
    if let Some(map) = theirs_tables.get("Map") {
        for &id in &o.maps {
            let Some(r) = map.row(id) else {
                let _ = writeln!(report, "map {id}: not in their Map table");
                continue;
            };
            let dir = map.string(r[1]).to_string();
            let wdt = format!("World\\Maps\\{dir}\\{dir}.wdt");
            roots.push(wdt.clone());
            roots.push(format!("World\\Maps\\{dir}\\{dir}.wdl"));
            if let Ok(b) = community.read(&wdt) {
                for (m, d) in benilla_bytes::chunks(&b) {
                    if &m == b"OMWM" {
                        roots.extend(
                            d.split(|&c| c == 0)
                                .filter(|s| !s.is_empty())
                                .map(|s| String::from_utf8_lossy(s).into_owned()),
                        );
                    }
                }
            }
            let mut n = 0;
            for x in 0..64 {
                for y in 0..64 {
                    let p = format!("World\\Maps\\{dir}\\{dir}_{x}_{y}.adt");
                    if !community.contains(&p) {
                        continue;
                    }
                    n += 1;
                    let c = tiles::Continent { map: id, dir: "" };
                    if let Ok(t) = tiles::inspect(c, x, y, &community.read(&p)?, TileKind::New) {
                        roots.extend(t.textures.into_iter().chain(t.models).chain(t.wmos));
                    }
                    roots.push(p);
                }
            }
            let _ = writeln!(report, "map {id} {dir}: {n} tiles");
        }
    }
    // New spell effect models.
    if let Some(t) = theirs_tables.get("SpellVisualEffectName") {
        for r in &t.records {
            for c in string_columns(t, t) {
                let s = t.string(r[c]);
                if !s.is_empty() {
                    roots.push(s.to_string());
                }
            }
        }
    }
    if let Some(p) = &o.displays {
        let d = read_displays(p)?;
        roots.extend(display_roots(&d, community, &theirs_tables));
    }

    let walk = refs::walk(roots, community, ours, true);
    let _ = writeln!(
        report,
        "edited buildings of ours taken from them: {} files",
        walk.edited.len()
    );
    for e in walk.edited.iter().filter(|e| !refs_is_group(e)) {
        let _ = writeln!(report, "  edited {e}");
    }
    copied.extend(walk.new_files.iter().cloned());
    // Their edited world-map art for the continents and the merged zones (the walk takes new
    // files only): their art draws to their bounds, which the WorldMapArea rows above carry.
    if let Some(wma) = theirs_tables.get("WorldMapArea") {
        let won = align_world_map_areas(&mut wma.clone(), wma, &o.zones);
        let edited = edited_world_map_files(
            &world_map_candidates(wma, theirs_tables.get("WorldMapOverlay"), &won),
            community,
            ours,
        );
        let _ = writeln!(
            report,
            "world map: {} edited files of theirs replace ours",
            edited.len()
        );
        copied.extend(edited);
    }
    let _ = writeln!(
        report,
        "files: {} new, {} of ours the community edits (kept ours), {} missing on both sides",
        walk.new_files.len(),
        walk.overridden.len(),
        walk.missing.len()
    );
    for m in walk
        .missing
        .iter()
        .filter(|m| !m.contains("Interface\\WorldMap"))
    {
        let _ = writeln!(report, "missing {m}");
    }

    // Stand-ins for the names neither side ships.
    for &(missing, stock) in SUBSTITUTES {
        if community.contains(missing) || ours.read(missing).is_ok() || made.contains_key(missing) {
            continue;
        }
        match ours.read(stock).or_else(|_| community.read(stock)) {
            Ok(b) => {
                let _ = writeln!(report, "substitute {missing} <- {stock}");
                made.insert(missing.to_string(), b);
            }
            Err(_) => {
                let _ = writeln!(report, "substitute {missing}: its stand-in {stock} is missing too");
            }
        }
    }

    // The branding scrub (`brand`): the tables we write, and every copied file whose rewrite can
    // change its length (text, tables), are rewritten here; binaries are rewritten as they are
    // packed (same length). Every file is stored under its scrubbed name.
    let mut scrubbed_tables = 0;
    for (p, b) in made.iter_mut() {
        if p.to_ascii_lowercase().ends_with(".dbc") {
            let mut t = Dbc::parse(b)?;
            let r = brand.scrub_dbc(&mut t).with_context(|| format!("scrubbing {p}"))?;
            if r.strings_changed > 0 {
                let _ = writeln!(report, "brand: {p}: {} strings rewritten", r.strings_changed);
                scrubbed_tables += 1;
                *b = t.to_bytes();
            }
        }
    }
    for p in &copied {
        let l = p.to_ascii_lowercase();
        if !made.contains_key(p) && (crate::brand::is_text_name(p) || l.ends_with(".dbc")) {
            let b = community.read(p)?;
            let s = brand.scrub_file(p, b.clone());
            if s != b {
                let _ = writeln!(report, "brand: {p}: text rewritten");
                made.insert(p.clone(), s);
            }
        }
    }
    // Their branded pictures: a stock stand-in stored under the same name.
    for (p, stock) in &brand.screen {
        if let Some(c) = copied.iter().find(|c| c.eq_ignore_ascii_case(p)).cloned() {
            let b = ours.read(stock).with_context(|| format!("the stand-in {stock} for {p}"))?;
            let _ = writeln!(report, "brand: screen {p} <- {stock}");
            made.insert(c, b);
        }
    }
    // Their credit, branding and connection-help screens never ship, referenced or not.
    copied.retain(|p| {
        let keep = !brand.is_branding_file(p);
        if !keep {
            let _ = writeln!(report, "brand: dropped {p}");
        }
        keep
    });
    made.retain(|p, _| !brand.is_branding_file(p));

    // Pack.
    let mut entries: Vec<(String, u64)> = Vec::new();
    // Stored name -> source name (an alias under the scrubbed name reads its original).
    let mut source_of: BTreeMap<String, String> = BTreeMap::new();
    let mut aliased = 0;
    for p in &copied {
        if made.contains_key(p) {
            continue;
        }
        let size = community.size(p).with_context(|| format!("size of {p}"))?;
        let stored = brand.scrub_path(p);
        if stored != *p {
            aliased += 1;
            let _ = writeln!(report, "brand: alias {stored} <- {p}");
        }
        source_of.insert(stored.to_ascii_lowercase(), p.clone());
        entries.push((stored, size));
    }
    for (p, b) in &made {
        let stored = brand.scrub_path(p);
        if stored != *p {
            aliased += 1;
            let _ = writeln!(report, "brand: alias {stored} <- {p}");
        }
        source_of.insert(stored.to_ascii_lowercase(), p.clone());
        entries.push((stored, b.len() as u64));
    }
    let _ = writeln!(
        report,
        "brand: {scrubbed_tables} tables rewritten, {aliased} files stored under a neutral name"
    );
    let archive = o.out.join(crate::ARCHIVE_NAME);
    let tmp = archive.with_extension("tmp");
    let mut f = std::io::BufWriter::new(std::fs::File::create(&tmp)?);
    let made_lc: BTreeMap<String, &Vec<u8>> = made
        .iter()
        .map(|(k, v)| (k.to_ascii_lowercase(), v))
        .collect();
    let size = crate::mpq_write::write_mpq_with(&entries, &mut f, |name| {
        let src = source_of
            .get(&name.to_ascii_lowercase())
            .map_or(name, String::as_str);
        if let Some(b) = made_lc.get(&src.to_ascii_lowercase()) {
            return Ok(brand.scrub_file(src, (*b).clone()));
        }
        let b = community.read(src).map_err(std::io::Error::other)?;
        Ok(brand.scrub_file(src, b))
    })?;
    drop(f);
    std::fs::rename(&tmp, &archive)?;

    // The server reads the same tables from its own dbc folder.
    let sdbc = o.out.join("server-dbc");
    std::fs::create_dir_all(&sdbc)?;
    for (p, b) in &made {
        if let Some(n) = p.strip_prefix("DBFilesClient\\") {
            std::fs::write(sdbc.join(n), b)?;
        }
    }
    // The tsv and text outputs beside the archive.
    for n in ["areas.tsv", "area_template.tsv", "tiles.txt"] {
        let p = o.out.join(n);
        if let Ok(t) = std::fs::read(&p) {
            let s = brand.scrub_text_bytes(&t);
            if s != t {
                std::fs::write(&p, s)?;
            }
        }
    }
    let _ = writeln!(
        report,
        "{} files, {:.1} MB in {}",
        entries.len(),
        size as f64 / 1e6,
        archive.display()
    );
    // The gate: the archive read back, the server's tables and the text outputs.
    let hits = brand.scan_outputs(&archive, &o.out)?;
    for h in &hits {
        eprintln!("BRANDING LEFT: {h}");
        let _ = writeln!(report, "BRANDING LEFT: {h}");
    }
    let report = brand.scrub_text(&report);
    std::fs::write(o.out.join("report.txt"), &report)?;
    print!("{report}");
    if !hits.is_empty() {
        bail!("{} branded files or strings left (report.txt)", hits.len());
    }
    println!("brand gate: clean");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    fn tile(map: u32, x: u32, y: u32, kind: TileKind, areas: &[u32]) -> Tile {
        Tile {
            continent: if map == 0 {
                tiles::AZEROTH
            } else {
                tiles::KALIMDOR
            },
            x,
            y,
            kind,
            ours_wins: tiles::ours_wins(map, x, y),
            has_mh2o: false,
            has_mccv: false,
            big_alpha: false,
            areas: areas.iter().map(|&a| (a, 1)).collect::<BTreeMap<_, _>>(),
            chunk_areas: Vec::new(),
            textures: Vec::new(),
            models: Vec::new(),
            wmos: Vec::new(),
        }
    }

    fn zones() -> Zones {
        // AreaTable rows: id, continent, parent, ... name at 11; 5122 is a subzone of 5121.
        let mut t = Dbc {
            field_count: 25,
            records: Vec::new(),
            strings: vec![0],
        };
        for (id, parent) in [(5121u32, 0u32), (5122, 5121), (440, 0), (2317, 440)] {
            let mut r = vec![0u32; 25];
            r[0] = id;
            r[2] = parent;
            t.records.push(r);
        }
        Zones::new(t)
    }

    #[test]
    fn every_substitute_is_a_distinct_missing_name_with_a_model_or_texture_stand_in() {
        let mut seen = BTreeSet::new();
        for &(missing, stock) in SUBSTITUTES {
            assert!(seen.insert(missing.to_ascii_lowercase()), "{missing} twice");
            assert_ne!(missing.to_ascii_lowercase(), stock.to_ascii_lowercase());
            let (m, s) = (missing.to_ascii_lowercase(), stock.to_ascii_lowercase());
            assert_eq!(m.ends_with(".m2"), s.ends_with(".m2"), "{missing}: a model for a model");
        }
    }

    #[test]
    fn a_zone_takes_its_tiles_and_the_open_sea_around_them_only() {
        let all = vec![
            tile(1, 44, 46, TileKind::New, &[5122]),
            tile(1, 43, 46, TileKind::Changed, &[2317, 5121]),
            tile(1, 45, 45, TileKind::New, &[0]), // sea beside the island
            tile(1, 50, 50, TileKind::New, &[0]), // far sea
            tile(1, 43, 40, TileKind::Changed, &[2317]), // the mainland coast, not the zone
            tile(1, 44, 47, TileKind::Same, &[5121]), // unchanged: nothing to merge
            tile(0, 31, 31, TileKind::Changed, &[5121]), // ours win (Alterac)
        ];
        let want: BTreeSet<u32> = [5121].into();
        let got: Vec<(u32, u32, u32)> = select_tiles(&all, &zones(), &want)
            .iter()
            .map(|t| (t.continent.map, t.x, t.y))
            .collect();
        assert_eq!(got, vec![(1, 44, 46), (1, 43, 46), (1, 45, 45)]);
        let every = select_tiles(&all, &zones(), &BTreeSet::new());
        assert_eq!(
            every.len(),
            5,
            "all zones: every merged tile, still never ours"
        );
    }

    #[test]
    fn flagging_sets_only_the_main_bits_of_the_new_tiles() {
        let mut wdt = Vec::new();
        wdt.extend_from_slice(b"REVM");
        wdt.extend_from_slice(&4u32.to_le_bytes());
        wdt.extend_from_slice(&18u32.to_le_bytes());
        wdt.extend_from_slice(b"NIAM");
        wdt.extend_from_slice(&((64 * 64 * 8) as u32).to_le_bytes());
        wdt.extend(vec![0u8; 64 * 64 * 8]);
        let out = flag_tiles(&wdt, &[(44, 47)]).unwrap();
        let at = 12 + 8 + (47 * 64 + 44) * 8;
        assert_eq!(out[at], 1);
        assert_eq!(
            out.iter().filter(|&&b| b != 0).count(),
            wdt.iter().filter(|&&b| b != 0).count() + 1
        );
        let parsed = benilla_wdt::WdtReader::new(
            std::io::Cursor::new(out),
            benilla_wdt::WowVersion::Classic,
        )
        .read()
        .unwrap();
        assert!(parsed.get_tile(44, 47).unwrap().has_adt);
        assert!(!parsed.get_tile(47, 44).unwrap().has_adt);
    }

    fn wma(rows: &[(u32, u32, u32, &str)]) -> Dbc {
        let mut t = Dbc {
            field_count: 8,
            records: Vec::new(),
            strings: vec![0],
        };
        for &(id, map, area, dir) in rows {
            let s = t.add_string(dir.as_bytes());
            t.records.push(vec![id, map, area, s, 1, 2, 3, 4]);
        }
        t
    }

    #[test]
    fn their_continent_rows_take_our_ids_and_only_merged_zones_win() {
        let ours = wma(&[
            (13, 1, 0, "Kalimdor"),
            (14, 0, 0, "Azeroth"),
            (41, 1, 440, "Tanaris"),
        ]);
        let mut theirs = wma(&[
            (13, 1, 0, "Kalimdor"),
            (600, 0, 0, "Azeroth"), // their continent row under another id
            (41, 1, 440, "Tanaris"),
            (650, 1, 5121, "TelAbim"),
            (700, 815, 0, "Giln"), // an instanced map: not a continent row of ours
            (694, 0, 0, "World"),  // their world overview: dropped
        ]);
        let won = align_world_map_areas(&mut theirs, &ours, &[5121].into());
        assert!(theirs.row(694).is_none(), "their World row is dropped");
        assert_eq!(theirs.records[1][0], 14, "takes our Azeroth id");
        assert_eq!(won, [13, 14, 650].into_iter().collect());
        let mut merged = ours.clone();
        merge_rows(&mut merged, &theirs, |_| true, |id| won.contains(&id)).unwrap();
        let continents = merged
            .records
            .iter()
            .filter(|r| r[2] == 0 && r[1] <= 1)
            .count();
        assert_eq!(continents, 2, "one continent row per map");
    }

    #[test]
    fn edited_world_map_art_is_taken_and_our_layers_win() {
        use crate::mpq_write::write_mpq;
        let dir = std::env::temp_dir().join(format!("womw-wm-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(dir.join("ours")).unwrap();
        let put = |p: std::path::PathBuf, files: &[(&str, &[u8])]| {
            let f: Vec<(String, Vec<u8>)> = files
                .iter()
                .map(|(n, b)| (n.to_string(), b.to_vec()))
                .collect();
            std::fs::write(p, write_mpq(&f)).unwrap();
        };
        let a1 = "Interface\\WorldMap\\Kalimdor\\Kalimdor1.blp";
        let a2 = "Interface\\WorldMap\\Kalimdor\\Kalimdor2.blp";
        let zmp = "Interface\\WorldMap\\Kalimdor.zmp";
        put(
            dir.join("ours").join("interface.MPQ"),
            &[(a1, b"stock"), (a2, b"same"), (zmp, b"stock")],
        );
        put(dir.join("patch-9.MPQ"), &[(zmp, b"our layer")]);
        put(
            dir.join("theirs.MPQ"),
            &[(a1, b"edited"), (a2, b"same"), (zmp, b"edited")],
        );
        let community = Layer::open_files(&[dir.join("theirs.MPQ")]).unwrap();
        let ours = Ours::open(&dir.join("ours"), &[dir.join("patch-9.MPQ")]).unwrap();
        let t = wma(&[(13, 1, 0, "Kalimdor")]);
        let c = world_map_candidates(&t, None, &[13].into());
        assert!(c.contains(&zmp.to_string()) && c.len() == 14);
        assert_eq!(
            edited_world_map_files(&c, &community, &ours),
            vec![a1.to_string()]
        );
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn our_taxi_paths_keep_their_own_nodes() {
        // Path 302 (the Orgrimmar-Undercity zeppelin) is ours; their path 900 is new.
        let ours = Dbc {
            field_count: 3,
            records: vec![vec![1, 302, 0], vec![2, 302, 1]],
            strings: vec![0],
        };
        let mut theirs = Dbc {
            field_count: 3,
            records: vec![vec![1, 302, 0], vec![5000, 302, 2], vec![5001, 900, 0]],
            strings: vec![0],
        };
        keep_our_paths(&mut theirs, &ours);
        let mut merged = ours.clone();
        merge_rows(&mut merged, &theirs, |_| true, |_| false).unwrap();
        assert_eq!(
            merged.records.iter().filter(|r| r[1] == 302).count(),
            2,
            "no extra zeppelin node"
        );
        assert!(merged.row(5001).is_some(), "a new path comes in");
    }

    #[test]
    fn renumbered_spells_change_their_row_ids_only() {
        let mut t = Dbc {
            field_count: 2,
            records: vec![vec![61001, 7], vec![5, 61001]],
            strings: vec![0],
        };
        renumber_rows(&mut t, &[(61001, 65000)].into_iter().collect());
        assert_eq!(t.records, vec![vec![65000, 7], vec![5, 61001]]);
    }

    #[test]
    fn the_community_maps_win_only_800_to_822() {
        assert!(community_map_wins(800) && community_map_wins(822));
        assert!(!community_map_wins(799) && !community_map_wins(823) && !community_map_wins(0));
    }

    #[test]
    fn their_city_tiles_get_the_void_holes_closed() {
        // Azeroth_30_48 is a Stormwind tile; a chunk there with two open cells and no WMO.
        let sw = tile(0, 30, 48, TileKind::Changed, &[1519]);
        let far = tile(0, 10, 10, TileKind::Changed, &[12]);
        let raw = crate::holes::tests::tile([-8800.0, 600.0], 0b0010_0001, None);
        let path = sw.path();
        let read = |p: &str| (p.eq_ignore_ascii_case(&path) || p.ends_with("_10_10.adt")).then(|| raw.clone());
        let out = close_city_holes(&[&sw, &far], &read);
        assert_eq!(out.len(), 1);
        assert_eq!((out[0].0.as_str(), out[0].2), (path.as_str(), 2));
        assert_eq!(out[0].1.len(), raw.len());
    }
}