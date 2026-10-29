//! Which open-world tiles the merge takes: every Azeroth (map 0) and Kalimdor (map 1) ADT the
//! community patches hold, new or changed against ours, minus the tiles we own.

use std::collections::BTreeMap;
use std::io::Cursor;

use anyhow::Result;
use benilla_adt::ParsedAdt;

use crate::source::{Layer, Ours};

/// A continent the merge covers: map id and `World\Maps` directory.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Continent {
    pub map: u32,
    pub dir: &'static str,
}

pub const AZEROTH: Continent = Continent {
    map: 0,
    dir: "Azeroth",
};
pub const KALIMDOR: Continent = Continent {
    map: 1,
    dir: "Kalimdor",
};
pub const CONTINENTS: [Continent; 2] = [AZEROTH, KALIMDOR];

/// Our tiles, which win over the community's: Alterac Mountains on Azeroth, where the WotLK layer
/// places Dalaran (`client/src/wotlk/dalaran.rs` `TILES`, the city and its terrain blend).
pub const OURS_WIN: [(u32, u32, u32); 9] = [
    (0, 30, 30),
    (0, 30, 31),
    (0, 30, 32),
    (0, 31, 30),
    (0, 31, 31),
    (0, 31, 32),
    (0, 32, 30),
    (0, 32, 31),
    (0, 32, 32),
];

pub fn adt_path(c: Continent, x: u32, y: u32) -> String {
    format!("World\\Maps\\{0}\\{0}_{1}_{2}.adt", c.dir, x, y)
}

pub fn wdt_path(c: Continent) -> String {
    format!("World\\Maps\\{0}\\{0}.wdt", c.dir)
}

/// Whether tile `(map, x, y)` is one we keep over the community's.
pub fn ours_wins(map: u32, x: u32, y: u32) -> bool {
    OURS_WIN.contains(&(map, x, y))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TileKind {
    /// No tile of ours at this spot.
    New,
    /// Ours exists and the community's bytes differ.
    Changed,
    /// The same bytes as ours: nothing to merge.
    Same,
}

/// One community tile and what it carries.
#[derive(Debug, Clone)]
pub struct Tile {
    pub continent: Continent,
    pub x: u32,
    pub y: u32,
    pub kind: TileKind,
    /// Kept out of the merge because the spot is ours ([`OURS_WIN`]).
    pub ours_wins: bool,
    pub has_mh2o: bool,
    pub has_mccv: bool,
    pub big_alpha: bool,
    /// `AreaTable` ids of the tile's 256 chunks, with chunk counts.
    pub areas: BTreeMap<u32, u32>,
    /// Per chunk `(index_x, index_y) -> area id`, for placing spawns.
    pub chunk_areas: Vec<(u32, u32, u32)>,
    pub textures: Vec<String>,
    pub models: Vec<String>,
    pub wmos: Vec<String>,
}

impl Tile {
    pub fn path(&self) -> String {
        adt_path(self.continent, self.x, self.y)
    }

    /// Whether the merge takes this tile.
    pub fn merged(&self) -> bool {
        self.kind != TileKind::Same && !self.ours_wins
    }
}

/// Read one community ADT's census and references.
pub fn inspect(continent: Continent, x: u32, y: u32, bytes: &[u8], kind: TileKind) -> Result<Tile> {
    let ParsedAdt::Root(root) = benilla_adt::parse_adt(&mut Cursor::new(bytes))
        .map_err(|e| anyhow::anyhow!("{}: {e}", adt_path(continent, x, y)))?;
    let mut areas = BTreeMap::new();
    let mut chunk_areas = Vec::new();
    for c in &root.mcnk_chunks {
        *areas.entry(c.header.area_id).or_insert(0) += 1;
        chunk_areas.push((c.header.index_x, c.header.index_y, c.header.area_id));
    }
    Ok(Tile {
        continent,
        x,
        y,
        kind,
        ours_wins: ours_wins(continent.map, x, y),
        has_mh2o: root.has_mh2o,
        has_mccv: root.mcnk_chunks.iter().any(|c| c.vertex_colors.is_some()),
        big_alpha: root.mcnk_chunks.iter().any(|c| c.big_alpha),
        areas,
        chunk_areas,
        textures: root.textures.clone(),
        models: root.models.clone(),
        wmos: root.wmos.clone(),
    })
}

/// Every Azeroth/Kalimdor tile the community patches hold, probed by name over the 64×64 grid
/// (some of their archives have no listfile).
pub fn community_tiles(community: &Layer, ours: &Ours) -> Result<Vec<Tile>> {
    let mut out = Vec::new();
    for c in CONTINENTS {
        for x in 0..64 {
            for y in 0..64 {
                let p = adt_path(c, x, y);
                if !community.contains(&p) {
                    continue;
                }
                let theirs = community.read(&p)?;
                let kind = if !ours.contains(&p) {
                    TileKind::New
                } else if ours.read(&p)? == theirs {
                    TileKind::Same
                } else {
                    TileKind::Changed
                };
                out.push(inspect(c, x, y, &theirs, kind)?);
            }
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_alterac_tiles_are_ours_and_nothing_else_is() {
        for (m, x, y) in OURS_WIN {
            assert!(ours_wins(m, x, y));
        }
        assert!(!ours_wins(1, 31, 31), "the same spot on Kalimdor is theirs");
        assert!(!ours_wins(0, 29, 31));
        assert!(!ours_wins(0, 33, 31));
        assert_eq!(
            adt_path(KALIMDOR, 40, 39),
            "World\\Maps\\Kalimdor\\Kalimdor_40_39.adt"
        );
    }
}
