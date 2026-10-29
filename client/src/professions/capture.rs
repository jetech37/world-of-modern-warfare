//! A capture hook for the trade windows: with `$AW_CAPTURE_BOOK` set (`construction`,
//! `minstrelsy`, `inscription`, `jewelcrafting`, or `blacksmithing` for the stock comparison) the
//! named book is pinned open in the stock `TradeSkillFrame`, so a `$WOW_CAPTURE` shot of the
//! client (e.g. `ui-unitframes`) shows it. The layer's books go through their real builders
//! ([`book`], [`crafts`]); Blacksmithing is a hand-made 1.12 book of the same shape.

use benilla_ui::script::{TradeSkillDifficulty, TradeSkillReagent, TradeSkillRecipe, TradeSkillState};
use bevy::prelude::*;

use super::book::{self, ItemLook, Items, MinstrelRecipe};
use super::crafts::{self, Kind, Recipe, Trade, TradeState};
use super::TradeBook;
use crate::build::Building;
use crate::build::pieces::PieceType;
use crate::build::wire;

fn look(have: u32, name: &str, icon: &str) -> ItemLook {
    ItemLook { have, name: Some(name.into()), icon: Some(format!("Interface\\Icons\\{icon}")) }
}

fn construction() -> TradeSkillState {
    let mut b = Building { tier: 2, skill: 92, cap: 150, ..Default::default() };
    let pieces = [
        (PieceType::WoodFloor, 1u16, 25u16),
        (PieceType::WoodWall, 1, 25),
        (PieceType::WoodHalfWall, 10, 35),
        (PieceType::WoodDoorway, 40, 70),
        (PieceType::WoodWindow, 60, 90),
        (PieceType::WoodRoof, 75, 105),
        (PieceType::WoodStairs, 85, 115),
        (PieceType::WoodBeam, 90, 120),
    ];
    for (kind, required, yellow) in pieces {
        b.recipes.insert(
            kind.0,
            wire::Recipe {
                kind,
                required,
                yellow,
                green: yellow + 20,
                gray: yellow + 40,
                materials: vec![(91001, 4), (91020, 2)],
            },
        );
    }
    b.kits.insert(PieceType::WoodWall.0, 6);
    let mut items = Items::new();
    items.insert(91001, look(23, "Rough Lumber", "INV_TradeskillItem_01"));
    items.insert(91020, look(9, "Iron Nails", "INV_Misc_Gear_01"));
    book::construction_state(&b, &items)
}

fn minstrelsy() -> TradeSkillState {
    let r = |id, instrument, required: u16, item| MinstrelRecipe {
        id,
        instrument,
        required,
        yellow: required + 25,
        green: required + 50,
        gray: required + 75,
        known: true,
        item,
        reagents: vec![(91001, 4), (4304, 2)],
    };
    let rows = vec![r(1, 1, 1, 94101), r(2, 2, 50, 94102), r(3, 3, 75, 94103), r(4, 4, 100, 94104)];
    let mut items = Items::new();
    items.insert(91001, look(13, "Rough Lumber", "INV_TradeskillItem_01"));
    items.insert(4304, look(5, "Thick Leather", "INV_Misc_LeatherScrap_08"));
    items.insert(94101, look(0, "Hand Drum", "INV_Misc_Drum_01"));
    items.insert(94102, look(0, "Lute", "INV_Misc_Bone_HumanSkull_01"));
    items.insert(94103, look(0, "Flute", "INV_Wand_07"));
    items.insert(94104, look(0, "Harp", "INV_Misc_Gear_02"));
    book::minstrel_state(&rows, 2, 110, &items)
}

fn crafted(trade: Trade) -> TradeSkillState {
    let jc = trade == Trade::Jewelcrafting;
    let tool = if jc { 20815 } else { 0 };
    let rec = |id, group: &str, name: &str, required, product| Recipe {
        id,
        kind: Kind::Make,
        required,
        yellow: required + 15,
        green: required + 30,
        gray: required + 45,
        known: true,
        product,
        count: 1,
        group: group.into(),
        name: name.into(),
        reagents: vec![(95001, 3), (95002, 1)],
        tool,
    };
    let book = if jc {
        vec![
            rec(21, "Gems", "Bold Bloodstone", 1, 95201),
            rec(22, "Gems", "Solid Azure Moonstone", 20, 95202),
            rec(23, "Gems", "Brilliant Golden Draenite", 40, 95203),
            rec(24, "Rings", "Copper Ring", 10, 95204),
            rec(25, "Rings", "Silver Band", 55, 95205),
        ]
    } else {
        vec![
            rec(11, "Glyphs", "Glyph of Warding", 1, 95101),
            rec(12, "Glyphs", "Glyph of Haste", 25, 95102),
            rec(13, "Glyphs", "Glyph of the Hunt", 50, 95103),
            rec(14, "Inks", "Ivory Ink", 5, 95104),
            rec(15, "Inks", "Moonglow Ink", 40, 95105),
        ]
    };
    let s = TradeState { tier: 1, skill: 48, cap: 75, book };
    let mut items = Items::new();
    if jc {
        items.insert(95001, look(10, "Rough Bloodstone", "INV_Misc_Gem_Ruby_01"));
        items.insert(95002, look(4, "Copper Bar", "INV_Ingot_02"));
        items.insert(20815, look(1, "Jeweler's Kit", "INV_Misc_Bag_10"));
    } else {
        items.insert(95001, look(10, "Peacebloom Pigment", "INV_Misc_Dust_02"));
        items.insert(95002, look(4, "Light Parchment", "INV_Misc_Note_01"));
    }
    for r in &s.book {
        items.insert(r.product, look(0, &r.name, if jc { "INV_Misc_Gem_01" } else { "INV_Scroll_03" }));
    }
    crafts::trade_state(trade, &s, &items)
}

/// A 1.12 Blacksmithing book of the same size, for the side-by-side.
fn blacksmithing() -> TradeSkillState {
    let r = |spell_id, name: &str, group: (u32, u32, &str), d, inv, icon: &str, reagents: Vec<(u32, &str, &str, u32, u32)>| {
        TradeSkillRecipe {
            group: Some((group.0, group.1, group.2.to_string())),
            spell_id,
            name: name.into(),
            difficulty: d,
            num_available: reagents.iter().map(|x| x.4 / x.3).min().unwrap_or(0),
            icon: Some(format!("Interface\\Icons\\{icon}")),
            min_made: 1,
            max_made: 1,
            cooldown_secs: None,
            product_item: 0,
            product_inv_type: inv,
            product_item_level: 0,
            reagents: reagents
                .into_iter()
                .map(|(item, n, i, need, have)| TradeSkillReagent {
                    item,
                    name: Some(n.into()),
                    icon: Some(format!("Interface\\Icons\\{i}")),
                    need,
                    have,
                })
                .collect(),
            tools: vec![("Blacksmith Hammer".into(), true), ("Anvil".into(), true)],
        }
    };
    let bar = |need, have| (2840, "Copper Bar", "INV_Ingot_02", need, have);
    let stone = |need, have| (2835, "Rough Stone", "INV_Stone_04", need, have);
    use TradeSkillDifficulty::*;
    TradeSkillState {
        line: 164,
        line_name: "Blacksmithing".into(),
        rank: 92,
        max_rank: 150,
        recipes: vec![
            r(2667, "Runed Copper Breastplate", (4, 3, "Mail"), Optimal, 5, "INV_Chest_Chain_15", vec![bar(12, 23)]),
            r(2666, "Runed Copper Belt", (4, 3, "Mail"), Medium, 6, "INV_Belt_03", vec![bar(10, 23)]),
            r(2664, "Runed Copper Bracers", (4, 3, "Mail"), Easy, 9, "INV_Bracer_03", vec![bar(10, 23)]),
            r(2663, "Copper Bracers", (4, 3, "Mail"), Trivial, 9, "INV_Bracer_03", vec![bar(2, 23)]),
            r(2660, "Rough Sharpening Stone", (7, 0, "Trade Goods"), Trivial, 0, "INV_Stone_SharpeningStone_01", vec![stone(1, 9)]),
            r(3115, "Rough Weightstone", (7, 0, "Trade Goods"), Trivial, 0, "INV_Stone_WeightStone_01", vec![stone(1, 9)]),
            r(3116, "Coarse Sharpening Stone", (7, 0, "Trade Goods"), Medium, 0, "INV_Stone_SharpeningStone_02", vec![stone(1, 9)]),
        ],
        repeat_count: 0,
    }
}

/// The book `$AW_CAPTURE_BOOK` names.
pub fn book_named(name: &str) -> Option<TradeSkillState> {
    Some(match name {
        "construction" => construction(),
        "minstrelsy" => minstrelsy(),
        "inscription" => crafted(Trade::Inscription),
        "jewelcrafting" => crafted(Trade::Jewelcrafting),
        "blacksmithing" => blacksmithing(),
        _ => return None,
    })
}

/// Pin the named book once the UI has had a few frames to load.
pub fn pin(mut trade: ResMut<TradeBook>, mut frames: Local<u32>, mut name: Local<Option<Option<String>>>) {
    let name = name.get_or_insert_with(|| std::env::var("AW_CAPTURE_BOOK").ok());
    let Some(n) = name.as_deref() else { return };
    *frames += 1;
    if *frames == 60 {
        trade.pinned = book_named(n);
        if trade.pinned.is_none() {
            warn!("AW_CAPTURE_BOOK={n}: no such book");
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn every_capture_book_has_rows_reagents_and_its_bar() {
        for n in ["construction", "minstrelsy", "inscription", "jewelcrafting", "blacksmithing"] {
            let s = super::book_named(n).unwrap();
            assert!(s.recipes.len() >= 4, "{n}");
            assert!(s.max_rank > 0 && s.rank > 0, "{n}");
            assert!(s.recipes.iter().all(|r| !r.reagents.is_empty() && r.group.is_some()), "{n}");
        }
        assert!(super::book_named("nope").is_none());
    }
}
