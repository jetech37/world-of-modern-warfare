//! Inscription and Jewelcrafting, the layer's two primary professions (the server's
//! `Fusion::Crafts`, the professions channel 12 ops 20-32): their skills and books as the server
//! sends them, the stock trade window's state for each, and the kits' glyphs and gems
//! ([`AugmentsMsg`], drawn by [`super::augments`]).
//!
//! - **Skills** (op 24): the tier, skill and cap, shown in the Skills tab under Professions.
//! - **Books** (op 23): every recipe of the trade, known or not; the trade window lists the known
//!   ones the skill reaches, under their headers (Milling, Inks, Scrolls, Major Glyphs...), each
//!   with its reagents and tool. Create sends op 21 Make.
//! - **The trainer's window** (op 22): train the tiers, learn recipes, buy goods, unlearn.

use std::collections::HashMap;

use benilla_ui::script::{TradeSkillDifficulty, TradeSkillReagent, TradeSkillRecipe, TradeSkillState};

use super::book::{ItemLook, Items, difficulty, tier_cap};
use crate::minstrel::{str_at, u16_at, u32_at};

pub const LINE_INSCRIPTION: u32 = 9003;
pub const LINE_JEWELCRAFTING: u32 = 9004;

pub const OP_ASK: u8 = 20;
pub const OP_ACT: u8 = 21;
pub const OP_WINDOW: u8 = 22;
pub const OP_BOOK: u8 = 23;
pub const OP_SKILL: u8 = 24;
pub const OP_ASK_AUGMENTS: u8 = 30;
pub const OP_AUGMENT: u8 = 31;
pub const OP_AUGMENTS: u8 = 32;

pub const ACT_TRAIN: u8 = 1;
pub const ACT_BUY: u8 = 2;
pub const ACT_LEARN: u8 = 3;
pub const ACT_MAKE: u8 = 4;
pub const ACT_UNLEARN: u8 = 5;

/// The two trades, by the server's `Crafts::Trade`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Trade {
    Inscription = 1,
    Jewelcrafting = 2,
}

impl Trade {
    pub const ALL: [Trade; 2] = [Trade::Inscription, Trade::Jewelcrafting];

    pub fn from_wire(v: u8) -> Option<Trade> {
        match v {
            1 => Some(Trade::Inscription),
            2 => Some(Trade::Jewelcrafting),
            _ => None,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Trade::Inscription => "Inscription",
            Trade::Jewelcrafting => "Jewelcrafting",
        }
    }

    pub fn line(self) -> u32 {
        match self {
            Trade::Inscription => LINE_INSCRIPTION,
            Trade::Jewelcrafting => LINE_JEWELCRAFTING,
        }
    }

    pub fn description(self) -> &'static str {
        match self {
            Trade::Inscription => {
                "Milling herbs into pigments, inks, scrolls, and the glyphs inscribed into your kits."
            }
            Trade::Jewelcrafting => {
                "Prospecting ore into gems, cutting them for your kits' guns, and making jewelry."
            }
        }
    }
}

/// What a recipe does with its reagents (the server's `Crafts::Kind`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Make,
    Mill,
    Prospect,
}

/// One recipe of a trade's book (`Crafts::BookEntry`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Recipe {
    pub id: u16,
    pub kind: Kind,
    pub required: u16,
    pub yellow: u16,
    pub green: u16,
    pub gray: u16,
    pub known: bool,
    pub product: u32,
    pub count: u8,
    pub group: String,
    pub name: String,
    pub reagents: Vec<(u32, u16)>,
    pub tool: u32,
}

/// One trade as the server last said of it.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TradeState {
    pub tier: u8,
    pub skill: u16,
    pub cap: u16,
    pub book: Vec<Recipe>,
}

/// [24, trade, tier, skill u16, cap u16]
pub fn decode_skill(b: &[u8]) -> Option<(Trade, u8, u16, u16)> {
    (b.len() == 7 && b[0] == OP_SKILL).then_some(())?;
    Some((Trade::from_wire(b[1])?, b[2], u16_at(b, 3)?, u16_at(b, 5)?))
}

/// [23, trade, n u16, n x (id u16, kind u8, required yellow green gray u16, known u8, product u32,
/// count u8, group str8, name str8, reagents u8, (item u32, count u16) x reagents, tool u32)]
pub fn decode_book(b: &[u8]) -> Option<(Trade, Vec<Recipe>)> {
    (b.first() == Some(&OP_BOOK)).then_some(())?;
    let trade = Trade::from_wire(*b.get(1)?)?;
    let n = u16_at(b, 2)?;
    let mut at = 4;
    let mut out = Vec::new();
    for _ in 0..n {
        let kind = match *b.get(at + 2)? {
            1 => Kind::Mill,
            2 => Kind::Prospect,
            _ => Kind::Make,
        };
        let id = u16_at(b, at)?;
        let required = u16_at(b, at + 3)?;
        let yellow = u16_at(b, at + 5)?;
        let green = u16_at(b, at + 7)?;
        let gray = u16_at(b, at + 9)?;
        let known = *b.get(at + 11)? != 0;
        let product = u32_at(b, at + 12)?;
        let count = *b.get(at + 16)?;
        let (group, next) = str_at(b, at + 17)?;
        let (name, next) = str_at(b, next)?;
        let k = *b.get(next)?;
        at = next + 1;
        let mut reagents = Vec::new();
        for _ in 0..k {
            reagents.push((u32_at(b, at)?, u16_at(b, at + 4)?));
            at += 6;
        }
        let tool = u32_at(b, at)?;
        at += 4;
        out.push(Recipe { id, kind, required, yellow, green, gray, known, product, count, group, name, reagents, tool });
    }
    (at == b.len()).then_some((trade, out))
}

pub fn encode_ask(t: Trade) -> Vec<u8> {
    vec![OP_ASK, t as u8]
}

pub fn encode_act(t: Trade, action: u8, arg: u32) -> Vec<u8> {
    let mut b = vec![OP_ACT, t as u8, action];
    b.extend_from_slice(&arg.to_le_bytes());
    b
}

/// A trade window's Create: the recipe in the low half, the count in the high.
pub fn encode_make(t: Trade, recipe: u32, count: u32) -> Vec<u8> {
    let count = count.clamp(1, 20);
    encode_act(t, ACT_MAKE, (recipe & 0xFFFF) | (count << 16))
}

/// A button of the trainer's window (`Crafts::Service`).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Service {
    pub action: u8,
    pub arg: u32,
    pub cost: u32,
    pub enabled: bool,
    pub hue: u8,
    pub label: String,
    pub note: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WindowMsg {
    pub trade: Trade,
    pub open: bool,
    pub trainer: bool,
    pub tier: u8,
    pub skill: u16,
    pub cap: u16,
    pub tone: u8,
    pub message: String,
    pub services: Vec<Service>,
}

/// [22, trade, open, trainer, tier, skill u16, cap u16, tone, message str8, n, services...]
pub fn decode_window(b: &[u8]) -> Option<WindowMsg> {
    (b.first() == Some(&OP_WINDOW)).then_some(())?;
    let mut w = WindowMsg {
        trade: Trade::from_wire(*b.get(1)?)?,
        open: *b.get(2)? != 0,
        trainer: *b.get(3)? != 0,
        tier: *b.get(4)?,
        skill: u16_at(b, 5)?,
        cap: u16_at(b, 7)?,
        tone: *b.get(9)?,
        message: String::new(),
        services: Vec::new(),
    };
    let (message, mut at) = str_at(b, 10)?;
    w.message = message;
    let n = *b.get(at)?;
    at += 1;
    for _ in 0..n {
        let action = *b.get(at)?;
        let arg = u32_at(b, at + 1)?;
        let cost = u32_at(b, at + 5)?;
        let enabled = *b.get(at + 9)? != 0;
        let hue = *b.get(at + 10)?;
        let (label, next) = str_at(b, at + 11)?;
        let (note, next) = str_at(b, next)?;
        at = next;
        w.services.push(Service { action, arg, cost, enabled, hue, label, note });
    }
    (at == b.len()).then_some(w)
}

/// The glyph and gem slots of the kit carried (`Crafts::Augments`).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AugmentsMsg {
    pub open: bool,
    /// The kit slot, `None` with no kit carried.
    pub kit: Option<u8>,
    pub can_edit: bool,
    pub level: u8,
    pub jewelcrafting: u16,
    pub majors: [u32; 3],
    pub minors: [u32; 3],
    pub primary: [u32; 4],
    pub secondary: [u32; 4],
    pub flags: u8,
    pub tone: u8,
    pub message: String,
}

pub const AUG_PRIMARY: u8 = 0x01;
pub const AUG_SECONDARY: u8 = 0x02;
pub const AUG_PRIMARY_SCOPED: u8 = 0x04;
pub const AUG_SECONDARY_SCOPED: u8 = 0x08;
pub const AUG_PRIMARY_META: u8 = 0x10;
pub const AUG_SECONDARY_META: u8 = 0x20;

pub fn decode_augments(b: &[u8]) -> Option<AugmentsMsg> {
    (b.first() == Some(&OP_AUGMENTS)).then_some(())?;
    let kit = *b.get(2)?;
    let mut m = AugmentsMsg {
        open: *b.get(1)? != 0,
        kit: (kit != 255).then_some(kit),
        can_edit: *b.get(3)? != 0,
        level: *b.get(4)?,
        jewelcrafting: u16_at(b, 5)?,
        ..Default::default()
    };
    let mut at = 7;
    for slots in [&mut m.majors[..], &mut m.minors[..], &mut m.primary[..], &mut m.secondary[..]] {
        for s in slots.iter_mut() {
            *s = u32_at(b, at)?;
            at += 4;
        }
    }
    m.flags = *b.get(at)?;
    m.tone = *b.get(at + 1)?;
    let (message, end) = str_at(b, at + 2)?;
    m.message = message;
    (end == b.len()).then_some(m)
}

/// [31, what (0 glyph, 1 gem), kind (glyph: 0 major 1 minor; gem: 0 primary 1 secondary), slot, item u32]
pub fn encode_augment(what: u8, kind: u8, slot: u8, item: u32) -> Vec<u8> {
    let mut b = vec![OP_AUGMENT, what, kind, slot];
    b.extend_from_slice(&item.to_le_bytes());
    b
}

/// The character levels the glyph slots open at (`Crafts::kMajorLevels`, `kMinorLevels`).
pub const MAJOR_LEVELS: [u8; 3] = [15, 30, 50];
pub const MINOR_LEVELS: [u8; 3] = [20, 40, 60];
/// The jeweler's own socket (the primary's third) asks this much Jewelcrafting.
pub const JEWELER_SOCKET_SKILL: u16 = 275;

/// The glyph items (`Crafts::Glyphs`): id, major, name. Mirrored for the window's bag scan.
pub const GLYPHS: [(u32, bool, &str); 19] = [
    (94330, true, "Glyph of the Quick Draw"),
    (94331, true, "Glyph of the Steady Hand"),
    (94332, true, "Glyph of Iron Skin"),
    (94333, true, "Glyph of Second Wind"),
    (94334, true, "Glyph of the Sapper"),
    (94335, true, "Glyph of the Quartermaster"),
    (94336, true, "Glyph of Last Rites"),
    (94337, true, "Glyph of the Hunter's Eye"),
    (94338, true, "Glyph of the Shadow"),
    (94339, true, "Glyph of the Heavy Gunner"),
    (94340, true, "Glyph of Quickened Words"),
    (94341, true, "Glyph of the Bladesworn"),
    (94342, true, "Glyph of the Tracker"),
    (94350, false, "Glyph of the Long Road"),
    (94351, false, "Glyph of the Feather"),
    (94352, false, "Glyph of the Gravedigger"),
    (94353, false, "Glyph of the Field Editor"),
    (94354, false, "Glyph of the Veteran"),
    (94355, false, "Glyph of the Sprinter"),
];

/// The cut gems (`Crafts::Gems`): id, meta, name.
pub const GEMS: [(u32, bool, &str); 17] = [
    (94450, false, "Bold Tigerseye"),
    (94451, false, "Solid Malachite"),
    (94452, false, "Shifting Shadowgem"),
    (94453, false, "Jagged Moss Agate"),
    (94454, false, "Lustrous Moonstone"),
    (94455, false, "Quick Jade"),
    (94456, false, "Rigid Citrine"),
    (94457, false, "Inscribed Citrine"),
    (94458, false, "Sparkling Aquamarine"),
    (94459, false, "Bright Star Ruby"),
    (94460, false, "Smooth Large Opal"),
    (94461, false, "Radiant Huge Emerald"),
    (94462, false, "Stormy Blue Sapphire"),
    (94463, false, "Runed Star Ruby"),
    (94464, false, "Prismatic Arcane Crystal"),
    (94465, true, "Relentless Azerothian Diamond"),
    (94466, true, "Swift Azerothian Diamond"),
];

pub fn glyph_name(item: u32) -> Option<&'static str> {
    GLYPHS.iter().find(|g| g.0 == item).map(|g| g.2)
}

pub fn gem_name(item: u32) -> Option<&'static str> {
    GEMS.iter().find(|g| g.0 == item).map(|g| g.2)
}

/// A trade's window state for the stock frame: the known recipes the skill reaches, in their
/// colours under their headers, with reagents, tools and how many the bags make.
pub fn trade_state(t: Trade, s: &TradeState, items: &Items) -> TradeSkillState {
    let mut recipes = Vec::new();
    let mut groups: HashMap<&str, u32> = HashMap::new();
    if s.tier > 0 {
        for r in s.book.iter().filter(|r| r.known && r.required <= s.skill.max(1)) {
            let next = groups.len() as u32 + 1;
            let group = *groups.entry(r.group.as_str()).or_insert(next);
            let mut rows = Vec::new();
            let mut can = u32::MAX;
            for &(item, need) in r.reagents.iter().filter(|(i, n)| *i != 0 && *n != 0) {
                let look = items.get(&item).cloned().unwrap_or_default();
                can = can.min(look.have / u32::from(need));
                rows.push(TradeSkillReagent { item, name: look.name, icon: look.icon, need: u32::from(need), have: look.have });
            }
            if rows.is_empty() {
                can = 0;
            }
            let mut tools = Vec::new();
            if r.tool != 0 {
                let look = items.get(&r.tool).cloned().unwrap_or_default();
                if look.have == 0 {
                    can = 0;
                }
                tools.push((look.name.unwrap_or_else(|| "Jeweler's Kit".into()), look.have > 0));
            }
            let product = items.get(&r.product).cloned().unwrap_or_default();
            let (min_made, max_made) = match r.kind {
                Kind::Make => (u32::from(r.count.max(1)), u32::from(r.count.max(1))),
                Kind::Mill => (2, 3),
                Kind::Prospect => (1, 2),
            };
            recipes.push(TradeSkillRecipe {
                group: Some((t.line(), group, r.group.clone())),
                spell_id: u32::from(r.id),
                name: r.name.clone(),
                difficulty: difficulty(s.skill, r.yellow, r.green, r.gray),
                num_available: can,
                icon: product.icon,
                min_made,
                max_made,
                cooldown_secs: None,
                product_item: if r.kind == Kind::Make { r.product } else { 0 },
                product_inv_type: 0,
                product_item_level: u32::from(r.required),
                reagents: rows,
                tools,
            });
        }
    }
    TradeSkillState {
        line: t.line(),
        line_name: t.name().into(),
        rank: u32::from(s.skill),
        max_rank: u32::from(if s.tier > 0 { s.cap.max(tier_cap(s.tier)) } else { 0 }),
        recipes,
        repeat_count: 0,
    }
}

/// Every item a trade's book names (reagents, products and tools), for the bag scan.
pub fn book_items(s: &TradeState) -> Vec<u32> {
    let mut out: Vec<u32> = s
        .book
        .iter()
        .flat_map(|r| {
            std::iter::once(r.product).chain(std::iter::once(r.tool)).chain(r.reagents.iter().map(|x| x.0))
        })
        .filter(|i| *i != 0)
        .collect();
    out.sort_unstable();
    out.dedup();
    out
}

/// The glyphs and gems a bag scan should count (the Glyphs & Gems window).
pub fn augment_items() -> Vec<u32> {
    GLYPHS.iter().map(|g| g.0).chain(GEMS.iter().map(|g| g.0)).collect()
}

/// The glyphs and gems carried, from a bag scan, in their tables' order.
pub fn carried(items: &Items, glyphs: bool) -> Vec<(u32, u32)> {
    let ids: Vec<u32> = if glyphs { GLYPHS.iter().map(|g| g.0).collect() } else { GEMS.iter().map(|g| g.0).collect() };
    ids.into_iter()
        .filter_map(|id| items.get(&id).filter(|l: &&ItemLook| l.have > 0).map(|l| (id, l.have)))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn str8(b: &mut Vec<u8>, s: &str) {
        b.push(s.len() as u8);
        b.extend_from_slice(s.as_bytes());
    }

    /// Crafts.cpp EncodeBook: a mill (known) and a glyph (not).
    fn book() -> Vec<u8> {
        let mut b = vec![OP_BOOK, 1, 2, 0];
        for (id, kind, req, known, product, group, name, tool) in [
            (1u16, 1u8, 1u16, 1u8, 94300u32, "Milling", "Mill Peacebloom", 0u32),
            (60, 0, 80, 0, 94330, "Major Glyphs", "Glyph of the Quick Draw", 0),
        ] {
            b.extend_from_slice(&id.to_le_bytes());
            b.push(kind);
            for v in [req, req + 25, req + 50, req + 75] {
                b.extend_from_slice(&v.to_le_bytes());
            }
            b.push(known);
            b.extend_from_slice(&product.to_le_bytes());
            b.push(1);
            str8(&mut b, group);
            str8(&mut b, name);
            b.push(1);
            b.extend_from_slice(&2447u32.to_le_bytes());
            b.extend_from_slice(&5u16.to_le_bytes());
            b.extend_from_slice(&tool.to_le_bytes());
        }
        b
    }

    #[test]
    fn the_book_reads_as_the_server_writes_it() {
        let (t, r) = decode_book(&book()).expect("book");
        assert_eq!(t, Trade::Inscription);
        assert_eq!(r.len(), 2);
        assert_eq!((r[0].kind, r[0].known, r[0].product), (Kind::Mill, true, 94300));
        assert_eq!(r[1].name, "Glyph of the Quick Draw");
        assert_eq!(r[1].reagents, vec![(2447, 5)]);
        let mut cut = book();
        cut.pop();
        assert_eq!(decode_book(&cut), None);
    }

    #[test]
    fn the_trade_window_lists_known_recipes_the_skill_reaches() {
        let (_, recipes) = decode_book(&book()).unwrap();
        let s = TradeState { tier: 1, skill: 10, cap: 75, book: recipes };
        let mut items = Items::new();
        items.insert(2447, ItemLook { have: 12, name: Some("Peacebloom".into()), icon: None });
        let st = trade_state(Trade::Inscription, &s, &items);
        assert_eq!((st.line, st.rank, st.max_rank), (LINE_INSCRIPTION, 10, 75));
        assert_eq!(st.recipes.len(), 1, "the unknown glyph is not listed");
        let mill = &st.recipes[0];
        assert_eq!(mill.num_available, 2, "12 herbs, five a milling");
        assert_eq!(mill.difficulty, TradeSkillDifficulty::Optimal);
        assert_eq!((mill.min_made, mill.max_made, mill.product_item), (2, 3, 0));
        assert_eq!(mill.group.as_ref().map(|g| g.2.as_str()), Some("Milling"));
        assert!(trade_state(Trade::Inscription, &TradeState::default(), &items).recipes.is_empty());
        assert_eq!(book_items(&s), vec![2447, 94300, 94330]);
    }

    #[test]
    fn a_tool_missing_greys_create() {
        let r = Recipe {
            id: 5,
            kind: Kind::Make,
            required: 20,
            yellow: 45,
            green: 70,
            gray: 95,
            known: true,
            product: 94450,
            count: 1,
            group: "Gems".into(),
            name: "Bold Tigerseye".into(),
            reagents: vec![(818, 1)],
            tool: 94484,
        };
        let s = TradeState { tier: 1, skill: 30, cap: 75, book: vec![r] };
        let mut items = Items::new();
        items.insert(818, ItemLook { have: 3, ..Default::default() });
        let st = trade_state(Trade::Jewelcrafting, &s, &items);
        assert_eq!(st.recipes[0].num_available, 0);
        assert_eq!(st.recipes[0].tools, vec![("Jeweler's Kit".to_string(), false)]);
        items.insert(94484, ItemLook { have: 1, name: Some("Jeweler's Kit".into()), icon: None });
        let st = trade_state(Trade::Jewelcrafting, &s, &items);
        assert_eq!(st.recipes[0].num_available, 3);
        assert_eq!(st.recipes[0].product_item, 94450);
    }

    #[test]
    fn the_skill_window_and_augments_read_back() {
        let mut b = vec![OP_SKILL, 2, 3];
        b.extend_from_slice(&200u16.to_le_bytes());
        b.extend_from_slice(&225u16.to_le_bytes());
        assert_eq!(decode_skill(&b), Some((Trade::Jewelcrafting, 3, 200, 225)));
        assert_eq!(decode_skill(&b[..6]), None);

        let mut w = vec![OP_WINDOW, 1, 1, 1, 0];
        w.extend_from_slice(&0u16.to_le_bytes());
        w.extend_from_slice(&0u16.to_le_bytes());
        w.push(0);
        str8(&mut w, "");
        w.push(1);
        w.push(ACT_TRAIN);
        w.extend_from_slice(&0u32.to_le_bytes());
        w.extend_from_slice(&100u32.to_le_bytes());
        w.push(1);
        w.push(0);
        str8(&mut w, "Train Apprentice Inscription");
        str8(&mut w, "Skill up to 75");
        let m = decode_window(&w).expect("window");
        assert!(m.open && m.trainer && m.trade == Trade::Inscription);
        assert_eq!(m.services[0].cost, 100);
        assert_eq!(encode_act(Trade::Jewelcrafting, ACT_LEARN, 7), vec![OP_ACT, 2, ACT_LEARN, 7, 0, 0, 0]);
        assert_eq!(encode_make(Trade::Inscription, 3, 50), encode_act(Trade::Inscription, ACT_MAKE, 3 | 20 << 16));
        assert_eq!(encode_ask(Trade::Inscription), vec![OP_ASK, 1]);

        // Crafts.cpp EncodeAugments.
        let mut a = vec![OP_AUGMENTS, 1, 2, 1, 60];
        a.extend_from_slice(&280u16.to_le_bytes());
        for v in [94330u32, 0, 0, 94350, 0, 0, 94450, 94455, 0, 94465, 94451, 0, 0, 0] {
            a.extend_from_slice(&v.to_le_bytes());
        }
        a.push(AUG_PRIMARY | AUG_PRIMARY_META);
        a.push(2);
        str8(&mut a, "Done.");
        let m = decode_augments(&a).expect("augments");
        assert_eq!((m.kit, m.can_edit, m.level, m.jewelcrafting), (Some(2), true, 60, 280));
        assert_eq!(m.majors, [94330, 0, 0]);
        assert_eq!(m.primary, [94450, 94455, 0, 94465]);
        assert_eq!(m.secondary[0], 94451);
        assert_eq!(m.message, "Done.");
        a[2] = 255;
        assert_eq!(decode_augments(&a).unwrap().kit, None);
        a.pop();
        assert_eq!(decode_augments(&a), None);
        assert_eq!(encode_augment(1, 0, 3, 94465), vec![OP_AUGMENT, 1, 0, 3, 0x01, 0x71, 0x01, 0]);
    }

    #[test]
    fn the_glyph_and_gem_tables_match_the_servers_ranges() {
        assert!(GLYPHS.iter().all(|g| (94330..=94379).contains(&g.0) && g.2.starts_with("Glyph of")));
        assert!(GEMS.iter().all(|g| (94450..=94479).contains(&g.0)));
        assert_eq!(GEMS.iter().filter(|g| g.1).count(), 2);
        assert_eq!(glyph_name(94336), Some("Glyph of Last Rites"));
        assert_eq!(gem_name(94464), Some("Prismatic Arcane Crystal"));
        let mut items = Items::new();
        items.insert(94351, ItemLook { have: 2, ..Default::default() });
        items.insert(94450, ItemLook { have: 0, ..Default::default() });
        assert_eq!(carried(&items, true), vec![(94351, 2)]);
        assert!(carried(&items, false).is_empty());
        assert_eq!(augment_items().len(), 36);
    }
}
