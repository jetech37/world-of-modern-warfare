//! The layer's professions as the stock `TradeSkillFrame` shows a book: each recipe the player
//! knows, in its difficulty colour (orange, yellow, green, gray, vanilla's bands from the
//! recipe's thresholds), grouped under headers, with its reagents and how many the bags make.
//! Pure: the bags, names and icons come in through [`ItemLook`].

use std::collections::HashMap;

use benilla_ui::script::{
    TradeSkillDifficulty, TradeSkillReagent, TradeSkillRecipe, TradeSkillState,
};

use crate::build::menu::{category_icon, category_of};
use crate::build::pieces::{Choice, PieceType, def};
use crate::build::{Building, look, wire};
use crate::minstrel::rules::Instrument;

/// The layer's skill-line ids (the server's `character_fusion_skill` keys).
pub const LINE_CONSTRUCTION: u32 = 9001;
pub const LINE_MINSTRELSY: u32 = 9002;

/// The cap each tier lets the skill reach (Apprentice 75 .. Artisan 300).
pub fn tier_cap(tier: u8) -> u16 {
    match tier {
        1 => 75,
        2 => 150,
        3 => 225,
        4 => 300,
        _ => 0,
    }
}

/// What the bags and the item cache say of an item: how many are carried, its name and icon
/// (`None` until the template arrives).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ItemLook {
    pub have: u32,
    pub name: Option<String>,
    pub icon: Option<String>,
}

pub type Items = HashMap<u32, ItemLook>;

/// The stock difficulty band of a recipe with vanilla's four thresholds at `skill`.
pub fn difficulty(skill: u16, yellow: u16, green: u16, gray: u16) -> TradeSkillDifficulty {
    if skill >= gray {
        TradeSkillDifficulty::Trivial
    } else if skill >= green {
        TradeSkillDifficulty::Easy
    } else if skill >= yellow {
        TradeSkillDifficulty::Medium
    } else {
        TradeSkillDifficulty::Optimal
    }
}

/// The reagent rows and how many the bags make (the least `have / need`; 0 with no reagents).
fn reagents(list: &[(u32, u16)], items: &Items) -> (Vec<TradeSkillReagent>, u32) {
    let mut out = Vec::new();
    let mut can = u32::MAX;
    for &(item, need) in list.iter().filter(|(i, n)| *i != 0 && *n != 0) {
        let look = items.get(&item).cloned().unwrap_or_default();
        // Salvaged goods build as their fresh ones do (the server pays from either).
        let salvaged = wire::SALVAGE
            .iter()
            .find(|(f, _)| *f == item)
            .and_then(|(_, s)| items.get(s))
            .map_or(0, |l| l.have);
        let have = look.have + salvaged;
        can = can.min(have / u32::from(need));
        out.push(TradeSkillReagent {
            item,
            name: look.name.or_else(|| Some(wire::item_name(item))),
            icon: look.icon,
            need: u32::from(need),
            have,
        });
    }
    if out.is_empty() {
        can = 0;
    }
    (out, can)
}

/// Every item a book's reagents (and products) name, for the bag scan; salvaged ones too.
pub fn construction_items(build: &Building) -> Vec<u32> {
    let mut out: Vec<u32> = Vec::new();
    for r in build.recipes.values() {
        for &(item, _) in &r.materials {
            out.push(item);
            if let Some((_, s)) = wire::SALVAGE.iter().find(|(f, _)| *f == item) {
                out.push(*s);
            }
        }
    }
    out.sort_unstable();
    out.dedup();
    out
}

/// A piece's name in the book: its style named when a style redraws it.
pub fn piece_name(t: PieceType) -> String {
    if t.style() != 0 {
        format!("{} · {}", def(t).name, look::style_of(t).name)
    } else {
        def(t).name.to_string()
    }
}

/// Construction's book: each piece the skill reaches, made into a kit (no item: the product is
/// placed with Build Structure), under its build-menu category.
pub fn construction_state(build: &Building, items: &Items) -> TradeSkillState {
    let skill = build.skill;
    let mut recipes = Vec::new();
    if build.tier > 0 {
        let mut kinds: Vec<&u8> = build.recipes.keys().collect();
        kinds.sort_unstable();
        for k in kinds {
            let r = &build.recipes[k];
            if r.required > skill {
                continue;
            }
            let t = PieceType(*k);
            let cat = category_of(Choice::Piece(t.base()));
            let (reagent_rows, can) = reagents(&r.materials, items);
            // The name alone: the stock row already ends in "[n]", how many the bags make, so
            // the kits held stay in the build menu rather than as a second count here.
            let name = piece_name(t);
            recipes.push(TradeSkillRecipe {
                group: Some((LINE_CONSTRUCTION, cat as u32, cat.title().to_string())),
                spell_id: u32::from(*k),
                name,
                difficulty: difficulty(skill, r.yellow, r.green, r.gray),
                num_available: can,
                icon: Some(category_icon(cat).to_string()),
                min_made: 1,
                max_made: 1,
                cooldown_secs: None,
                product_item: 0,
                product_inv_type: 0,
                product_item_level: u32::from(r.required),
                reagents: reagent_rows,
                tools: Vec::new(),
            });
        }
    }
    TradeSkillState {
        line: LINE_CONSTRUCTION,
        line_name: "Construction".into(),
        rank: u32::from(skill),
        max_rank: u32::from(if build.tier > 0 { build.cap.max(tier_cap(build.tier)) } else { 0 }),
        recipes,
        repeat_count: 0,
    }
}

/// One recipe of Minstrelsy's book (the server's `Minstrelsy::BookEntry`, op 63).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MinstrelRecipe {
    pub id: u16,
    pub instrument: u8,
    pub required: u16,
    pub yellow: u16,
    pub green: u16,
    pub gray: u16,
    pub known: bool,
    pub item: u32,
    pub reagents: Vec<(u32, u16)>,
}

pub const OP_BOOK: u8 = 63;

/// The server's book: [63, n, n x (id u16, instrument u8, required yellow green gray u16,
/// known u8, item u32, reagents u8, reagents x (item u32, count u16))].
pub fn decode_minstrel_book(b: &[u8]) -> Option<Vec<MinstrelRecipe>> {
    let u16_at = |at: usize| Some(u16::from_le_bytes([*b.get(at)?, *b.get(at + 1)?]));
    let u32_at = |at: usize| {
        Some(u32::from_le_bytes([*b.get(at)?, *b.get(at + 1)?, *b.get(at + 2)?, *b.get(at + 3)?]))
    };
    (b.first() == Some(&OP_BOOK)).then_some(())?;
    let n = *b.get(1)?;
    let mut at = 2;
    let mut out = Vec::new();
    for _ in 0..n {
        let mut r = MinstrelRecipe {
            id: u16_at(at)?,
            instrument: *b.get(at + 2)?,
            required: u16_at(at + 3)?,
            yellow: u16_at(at + 5)?,
            green: u16_at(at + 7)?,
            gray: u16_at(at + 9)?,
            known: *b.get(at + 11)? != 0,
            item: u32_at(at + 12)?,
            reagents: Vec::new(),
        };
        let k = *b.get(at + 16)?;
        at += 17;
        for _ in 0..k {
            r.reagents.push((u32_at(at)?, u16_at(at + 4)?));
            at += 6;
        }
        out.push(r);
    }
    (at == b.len()).then_some(out)
}

/// Minstrelsy's book: the instruments whose recipes are known, each made as an item.
pub fn minstrel_state(book: &[MinstrelRecipe], tier: u8, skill: u16, items: &Items) -> TradeSkillState {
    let mut recipes = Vec::new();
    if tier > 0 {
        for r in book.iter().filter(|r| r.known) {
            let name = Instrument::from_wire(r.instrument).map_or("Instrument", |i| i.def().name);
            let (reagent_rows, can) = reagents(&r.reagents, items);
            let look = items.get(&r.item).cloned().unwrap_or_default();
            recipes.push(TradeSkillRecipe {
                group: Some((LINE_MINSTRELSY, 0, "Instruments".into())),
                spell_id: u32::from(r.id),
                name: look.name.clone().unwrap_or_else(|| name.to_string()),
                difficulty: difficulty(skill, r.yellow, r.green, r.gray),
                num_available: can,
                icon: look.icon.clone(),
                min_made: 1,
                max_made: 1,
                cooldown_secs: None,
                product_item: r.item,
                product_inv_type: 0,
                product_item_level: u32::from(r.required),
                reagents: reagent_rows,
                tools: Vec::new(),
            });
        }
    }
    TradeSkillState {
        line: LINE_MINSTRELSY,
        line_name: "Minstrelsy".into(),
        rank: u32::from(skill),
        max_rank: u32::from(tier_cap(tier)),
        recipes,
        repeat_count: 0,
    }
}

/// Every item Minstrelsy's book names (reagents and products), for the bag scan.
pub fn minstrel_items(book: &[MinstrelRecipe]) -> Vec<u32> {
    let mut out: Vec<u32> = book
        .iter()
        .flat_map(|r| std::iter::once(r.item).chain(r.reagents.iter().map(|x| x.0)))
        .collect();
    out.sort_unstable();
    out.dedup();
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn wall() -> wire::Recipe {
        wire::Recipe {
            kind: PieceType::WoodWall,
            required: 20,
            yellow: 45,
            green: 70,
            gray: 95,
            materials: vec![(91001, 5), (91020, 2)],
        }
    }

    fn builder(tier: u8, skill: u16) -> Building {
        let mut b = Building { tier, skill, cap: tier_cap(tier), ..Default::default() };
        b.recipes.insert(PieceType::WoodWall.0, wall());
        b
    }

    #[test]
    fn the_bands_follow_the_thresholds() {
        assert_eq!(difficulty(20, 45, 70, 95), TradeSkillDifficulty::Optimal);
        assert_eq!(difficulty(45, 45, 70, 95), TradeSkillDifficulty::Medium);
        assert_eq!(difficulty(70, 45, 70, 95), TradeSkillDifficulty::Easy);
        assert_eq!(difficulty(95, 45, 70, 95), TradeSkillDifficulty::Trivial);
    }

    #[test]
    fn construction_lists_what_the_skill_reaches_with_reagents_and_counts() {
        let mut items = Items::new();
        items.insert(91001, ItemLook { have: 11, name: Some("Rough Lumber".into()), icon: None });
        items.insert(91020, ItemLook { have: 9, ..Default::default() });
        // Too low: nothing listed. Unlearned: nothing, and no bar.
        assert!(construction_state(&builder(1, 10), &items).recipes.is_empty());
        let none = construction_state(&builder(0, 0), &items);
        assert!(none.recipes.is_empty() && none.max_rank == 0);
        let s = construction_state(&builder(1, 50), &items);
        assert_eq!((s.line, s.rank, s.max_rank), (LINE_CONSTRUCTION, 50, 75));
        assert_eq!(s.line_name, "Construction");
        let r = &s.recipes[0];
        assert_eq!(r.difficulty, TradeSkillDifficulty::Medium);
        assert_eq!(r.num_available, 2, "11 lumber / 5, 9 nails / 2");
        assert_eq!(r.reagents.len(), 2);
        assert_eq!(r.reagents[0].name.as_deref(), Some("Rough Lumber"));
        assert_eq!((r.reagents[1].need, r.reagents[1].have), (2, 9));
        assert_eq!(r.spell_id, u32::from(PieceType::WoodWall.0));
        assert_eq!(r.product_item, 0, "a kit, placed with Build Structure");
        assert!(r.group.is_some());
        // Short of lumber: none can be made (Create greys).
        items.get_mut(&91001).unwrap().have = 4;
        assert_eq!(construction_state(&builder(1, 50), &items).recipes[0].num_available, 0);
        // Salvaged lumber counts as lumber.
        let salvaged = wire::SALVAGE.iter().find(|(f, _)| *f == 91001).unwrap().1;
        items.insert(salvaged, ItemLook { have: 6, ..Default::default() });
        assert_eq!(construction_state(&builder(1, 50), &items).recipes[0].num_available, 2);
        assert!(construction_items(&builder(1, 50)).contains(&salvaged));
    }

    #[test]
    fn kits_held_leave_the_name_to_the_stock_count() {
        let mut b = builder(1, 50);
        b.kits.insert(PieceType::WoodWall.0, 3);
        let s = construction_state(&b, &Items::new());
        assert!(!s.recipes[0].name.contains('['), "{}", s.recipes[0].name);
        assert_eq!(s.recipes[0].difficulty, TradeSkillDifficulty::Medium);
        let gray = construction_state(&builder(2, 120), &Items::new());
        assert_eq!(gray.recipes[0].difficulty, TradeSkillDifficulty::Trivial);
        assert_eq!(gray.max_rank, 150);
    }

    fn book_bytes() -> Vec<u8> {
        // Minstrelsy.cpp EncodeBook: a known Lute, an unknown Flute.
        let mut b = vec![OP_BOOK, 2];
        for (id, inst, req, known, item) in [(2u16, 2u8, 50u16, 1u8, 94102u32), (3, 3, 100, 0, 94103)] {
            b.extend_from_slice(&id.to_le_bytes());
            b.push(inst);
            for v in [req, req + 25, req + 50, req + 75] {
                b.extend_from_slice(&v.to_le_bytes());
            }
            b.push(known);
            b.extend_from_slice(&item.to_le_bytes());
            b.push(1);
            b.extend_from_slice(&91001u32.to_le_bytes());
            b.extend_from_slice(&4u16.to_le_bytes());
        }
        b
    }

    #[test]
    fn minstrelsy_lists_the_known_instruments_as_items() {
        let book = decode_minstrel_book(&book_bytes()).expect("book");
        assert_eq!(book.len(), 2);
        assert_eq!((book[0].id, book[0].known, book[0].item), (2, true, 94102));
        assert_eq!(book[1].reagents, vec![(91001, 4)]);
        let mut short = book_bytes();
        short.pop();
        assert_eq!(decode_minstrel_book(&short), None);
        let mut items = Items::new();
        items.insert(91001, ItemLook { have: 8, ..Default::default() });
        let s = minstrel_state(&book, 2, 60, &items);
        assert_eq!((s.line, s.rank, s.max_rank), (LINE_MINSTRELSY, 60, 150));
        assert_eq!(s.recipes.len(), 1, "only the known recipe");
        assert_eq!(s.recipes[0].name, "Lute");
        assert_eq!(s.recipes[0].product_item, 94102);
        assert_eq!(s.recipes[0].num_available, 2);
        assert_eq!(s.recipes[0].difficulty, TradeSkillDifficulty::Optimal);
        assert!(minstrel_state(&book, 0, 0, &items).recipes.is_empty());
        assert_eq!(minstrel_items(&book), vec![91001, 94102, 94103]);
    }
}
