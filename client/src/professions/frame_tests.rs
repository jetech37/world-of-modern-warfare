//! Each of the layer's professions in the stock `Blizzard_TradeSkillUI` (read off the player's own
//! install, never committed): the book each one feeds, pushed through the same
//! `UiScript::set_trade_skill` the app uses, opened by `TRADE_SKILL_SHOW` as a stock trade's is,
//! then read back off the stock frame: the title and skill bar, the headers and recipe rows in
//! their difficulty colours, the reagents with have/need counts, the Requires line, the
//! collapse-all tab and the filters, and what Create and Create All ask the server for.

use std::cell::RefCell;

use benilla_ui::script::{TradeSkillState, UiScript};

use super::book::{self, ItemLook, Items, MinstrelRecipe};
use super::crafts::{self, Kind, Recipe, Trade, TradeState};
use super::{Book, CHANNEL, OP_CRAFT_KIT, craft_message};
use crate::build::Building;
use crate::build::pieces::PieceType;
use crate::build::wire;

/// The trade window's load prefix, in the app's order (benilla's `tests/tradeskill_frame.rs`).
const FILES: &[&str] = &[
    r"Interface\FrameXML\GlobalStrings.lua",
    r"Interface\FrameXML\Fonts.xml",
    r"Interface\FrameXML\BasicControls.xml",
    r"Interface\FrameXML\LocaleProperties.lua",
    r"Interface\FrameXML\UIParent.xml",
    r"Interface\FrameXML\MoneyFrame.lua",
    r"Interface\FrameXML\MoneyFrame.xml",
    r"Interface\FrameXML\GameTooltip.xml",
    r"Interface\FrameXML\UIDropDownMenu.xml",
    r"Interface\FrameXML\UIPanelTemplates.lua",
    r"Interface\FrameXML\UIPanelTemplates.xml",
    r"Interface\FrameXML\StaticPopup.xml",
    r"Interface\FrameXML\ItemButtonTemplate.xml",
    r"Interface\FrameXML\QuestFrameTemplates.xml",
    r"Interface\FrameXML\ClassTrainerFrameTemplates.xml",
    "ScrollTemplates.xml",
    r"Interface\AddOns\Blizzard_TradeSkillUI\Blizzard_TradeSkillUI.xml",
];

/// The stock trade window off the install's chain; a bare name is benilla's own `assets/ui` file.
fn stock_vm(data: &std::path::Path) -> UiScript {
    let chain = RefCell::new(benilla_formats::open_chain(data).expect("the patch chain"));
    let ours = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../benilla/crates/benilla-app/assets/ui");
    let read = |p: &str| {
        if p.contains('\\') || p.contains('/') {
            chain.borrow_mut().read_file(&p.replace('/', "\\")).ok()
        } else {
            std::fs::read(ours.join(p)).ok()
        }
    };
    let mut s = UiScript::new().expect("a VM");
    s.set_screen_size(1024.0, 768.0);
    for file in FILES {
        let path = file.replace('\\', "/");
        let bytes = read(file).unwrap_or_else(|| panic!("{file}: not found"));
        if path.ends_with(".lua") {
            s.run_chunk_named(&bytes, &format!("@{file}")).unwrap_or_else(|e| panic!("{file}: {e}"));
            continue;
        }
        let doc = benilla_ui::framexml::parse(&benilla_ui::source::decode(&bytes))
            .unwrap_or_else(|e| panic!("{file}: {e}"));
        let report = benilla_ui::loader::load_in(&s, &doc, &path, &read);
        assert!(report.errors.is_empty(), "{file}: {:?}", report.errors);
    }
    s
}

fn q<T: mlua::FromLuaMulti>(s: &UiScript, chunk: &str) -> T {
    s.eval::<T>(chunk).unwrap_or_else(|e| panic!("{chunk}: {e}"))
}

fn look(have: u32, name: &str) -> ItemLook {
    ItemLook { have, name: Some(name.into()), icon: Some("Interface\\Icons\\INV_Misc_Gem_01".into()) }
}

/// What the app does after a list mutator: drain the touched flag and fire `TRADE_SKILL_UPDATE`.
fn pump(s: &mut UiScript) {
    if s.take_trade_skill_touched() {
        s.fire_event("TRADE_SKILL_UPDATE", vec![]);
    }
}

/// Open `state` as the app opens a book.
fn open(s: &mut UiScript, state: TradeSkillState) {
    s.set_trade_skill(Some(state));
    s.fire_event("TRADE_SKILL_SHOW", vec![]);
    assert!(q::<bool>(s, "return TradeSkillFrame:IsShown() and true or false"), "the stock window opens");
    s.resolve();
}

/// A visible row's label and its colour as drawn (the row's text quad), `None` off the list.
fn row(s: &mut UiScript, n: i64) -> (String, [f32; 4]) {
    let text = q::<String>(s, &format!("return TradeSkillSkill{n}:GetText() or ''"));
    s.resolve();
    let color = s
        .extract()
        .into_iter()
        .find_map(|q| match q.content {
            benilla_ui::script::QuadContent::Text { text: Some(t), color, .. } if t == text => color,
            _ => None,
        })
        .unwrap_or_else(|| panic!("no text quad for row {n} ({text:?})"));
    (text, color)
}

/// `TradeSkillTypeColor[key]` from the stock Lua: the colours the rows must wear.
fn type_color(s: &UiScript, key: &str) -> [f32; 3] {
    let (r, g, b) = q::<(f32, f32, f32)>(
        s,
        &format!("local c = TradeSkillTypeColor[\"{key}\"] return c.r, c.g, c.b"),
    );
    [r, g, b]
}

fn close(a: [f32; 4], b: [f32; 3]) -> bool {
    (a[0] - b[0]).abs() < 0.02 && (a[1] - b[1]).abs() < 0.02 && (a[2] - b[2]).abs() < 0.02
}

/// The rows the stock frame drew: (label, type) per visible row.
fn infos(s: &UiScript) -> Vec<(String, String)> {
    let n = q::<i64>(s, "return GetNumTradeSkills()");
    (1..=n)
        .map(|i| q::<(String, String)>(s, &format!("local n, t = GetTradeSkillInfo({i}) return n, t")))
        .collect()
}

/// The checks every book shares: the title, the bar, the first recipe's detail, Create, Create
/// All and the collapse-all tab.
struct Expect<'a> {
    book: Book,
    title: &'a str,
    rank: &'a str,
    header: &'a str,
    first: &'a str,
    reagent: (&'a str, u32, u32),
    can: u32,
    recipe: u32,
}

fn common(s: &mut UiScript, e: &Expect) {
    let title = q::<String>(s, "return TradeSkillFrameTitleText:GetText()");
    assert!(title.contains(e.title), "{:?}: the title {title:?}", e.book);
    assert_eq!(q::<String>(s, "return TradeSkillRankFrameSkillRank:GetText()"), e.rank, "{:?}: the skill bar", e.book);
    assert!(q::<bool>(s, "return TradeSkillRankFrame:IsShown() and true or false"));
    let rows = infos(s);
    assert_eq!(rows[0], (e.header.to_string(), "header".to_string()), "{:?}: the first row is a header", e.book);
    // Opening selects the first recipe and draws its detail.
    assert_eq!(q::<i64>(s, "return GetTradeSkillSelectionIndex()"), 2, "{:?}", e.book);
    assert_eq!(q::<String>(s, "return TradeSkillSkillName:GetText()"), e.first, "{:?}: the detail's name", e.book);
    assert!(q::<bool>(s, "return TradeSkillSkillIcon:IsShown() and true or false"), "{:?}: the result's icon", e.book);
    let (name, have, need) = e.reagent;
    assert_eq!(q::<String>(s, "return TradeSkillReagent1Name:GetText()"), name, "{:?}", e.book);
    let count = q::<String>(s, "return TradeSkillReagent1Count:GetText()");
    assert_eq!(count, format!("{have} /{need}"), "{:?}: the have/need count", e.book);
    assert!(q::<bool>(s, "return TradeSkillReagent1:IsShown() and true or false"));
    // The tooltips the stock result icon and reagent slots ask for (their OnEnter's calls).
    let tip = |s: &UiScript, args: &str| {
        q::<String>(
            s,
            &format!(
                "GameTooltip:SetOwner(UIParent, \"ANCHOR_NONE\") GameTooltip:SetTradeSkillItem({args}) \
                 return GameTooltipTextLeft1:GetText() or ''"
            ),
        )
    };
    assert_eq!(tip(s, "TradeSkillFrame.selectedSkill, 1"), name, "{:?}: the reagent's tooltip", e.book);
    assert_eq!(tip(s, "TradeSkillFrame.selectedSkill"), e.first, "{:?}: the result's tooltip", e.book);
    // Create and Create All are live when the bags make one, and ask for the right recipe.
    assert_eq!(q::<bool>(s, "return TradeSkillCreateButton:IsEnabled() == 1"), e.can > 0, "{:?}", e.book);
    if e.can > 0 {
        s.run("TradeSkillCreateButton:Click()").unwrap();
        assert_eq!(s.take_trade_skill_dos(), vec![(e.recipe, 1)], "{:?}: Create", e.book);
        s.run("TradeSkillCreateAllButton:Click()").unwrap();
        assert_eq!(s.take_trade_skill_dos(), vec![(e.recipe, e.can)], "{:?}: Create All", e.book);
        assert_eq!(q::<String>(s, "return TradeSkillInputBox:GetText()"), e.can.to_string(), "the count box");
    }
    // Collapse all folds the list to its headers and back.
    let headers = rows.iter().filter(|r| r.1 == "header").count() as i64;
    s.run("TradeSkillCollapseAllButton:Click()").unwrap();
    pump(s);
    assert_eq!(q::<i64>(s, "return GetNumTradeSkills()"), headers, "{:?}: collapse all", e.book);
    s.run("TradeSkillCollapseAllButton:Click()").unwrap();
    pump(s);
    assert_eq!(q::<i64>(s, "return GetNumTradeSkills()") as usize, rows.len());
    // "All Subclasses", or the one subclass a single-header book has (the stock initializer).
    assert!(!q::<String>(s, "return TradeSkillSubClassDropDownText:GetText() or ''").is_empty());
    assert!(q::<bool>(s, "return TradeSkillInvSlotDropDown:IsShown() and true or false"));
}

fn construction() -> (Building, Items) {
    let mut b = Building { tier: 1, skill: 50, cap: 75, ..Default::default() };
    let r = |kind: PieceType, required, y, g, gr| wire::Recipe {
        kind,
        required,
        yellow: y,
        green: g,
        gray: gr,
        materials: vec![(91001, 5), (91020, 2)],
    };
    b.recipes.insert(PieceType::WoodWall.0, r(PieceType::WoodWall, 1, 20, 35, 50));
    b.recipes.insert(PieceType::WoodFloor.0, r(PieceType::WoodFloor, 20, 45, 70, 95));
    b.kits.insert(PieceType::WoodWall.0, 3);
    let mut items = Items::new();
    items.insert(91001, look(11, "Rough Lumber"));
    items.insert(91020, look(9, "Iron Nails"));
    (b, items)
}

#[test]
fn construction_opens_in_the_stock_trade_window() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = stock_vm(&data);
    let (b, items) = construction();
    let state = book::construction_state(&b, &items);
    let first = state.recipes.iter().find(|r| r.spell_id == u32::from(PieceType::WoodFloor.0)).unwrap().clone();
    open(&mut s, state);
    // The engine sorts by difficulty: the orange-yellow floor before the gray wall.
    let rows = infos(&s);
    assert_eq!(rows.len(), 3, "{rows:?}");
    assert_eq!(rows[1].1, "medium");
    assert_eq!(rows[2].1, "trivial");
    assert!(!rows[2].0.contains('['), "one count per row, the stock one: {rows:?}");
    let (_, c) = row(&mut s, 3);
    assert!(close(c, type_color(&s, "trivial")), "the gray row is drawn gray: {c:?}");
    common(&mut s, &Expect {
        book: Book::Construction,
        title: "Construction",
        rank: "50/75",
        header: &rows[0].0,
        first: &first.name,
        reagent: ("Rough Lumber", 11, 5),
        can: 2,
        recipe: first.spell_id,
    });
    let m = craft_message(Book::Construction, first.spell_id, 1);
    assert_eq!((m.channel, m.body[0], m.body[1]), (CHANNEL, OP_CRAFT_KIT, PieceType::WoodFloor.0));
}

#[test]
fn minstrelsy_opens_in_the_stock_trade_window() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = stock_vm(&data);
    let r = |id, instrument, required: u16, item| MinstrelRecipe {
        id,
        instrument,
        required,
        yellow: required + 25,
        green: required + 50,
        gray: required + 75,
        known: true,
        item,
        reagents: vec![(91001, 4)],
    };
    let book_rows = vec![r(1, 1, 1, 94101), r(2, 2, 60, 94102)];
    let mut items = Items::new();
    items.insert(91001, look(8, "Rough Lumber"));
    items.insert(94102, look(0, "Lute"));
    items.insert(94101, look(0, "Hand Drum"));
    open(&mut s, book::minstrel_state(&book_rows, 2, 60, &items));
    let rows = infos(&s);
    assert_eq!(rows.iter().map(|r| r.1.as_str()).collect::<Vec<_>>(), ["header", "optimal", "easy"]);
    let (_, c) = row(&mut s, 3);
    assert!(close(c, type_color(&s, "easy")), "the green row is drawn green: {c:?}");
    common(&mut s, &Expect {
        book: Book::Minstrelsy,
        title: "Minstrelsy",
        rank: "60/150",
        header: "Instruments",
        first: "Lute",
        reagent: ("Rough Lumber", 8, 4),
        can: 2,
        recipe: 2,
    });
}

fn crafts_book(trade: Trade) -> (TradeState, Items) {
    let tool = if trade == Trade::Jewelcrafting { 20815 } else { 0 };
    let rec = |id, group: &str, name: &str, required, product| Recipe {
        id,
        kind: Kind::Make,
        required,
        yellow: required + 10,
        green: required + 20,
        gray: required + 30,
        known: true,
        product,
        count: 1,
        group: group.into(),
        name: name.into(),
        reagents: vec![(95001, 3)],
        tool,
    };
    let state = TradeState {
        tier: 1,
        skill: 15,
        cap: 75,
        book: vec![
            rec(11, "Glyphs", "Glyph of Warding", 1, 95100),
            rec(12, "Glyphs", "Glyph of Haste", 10, 95101),
            rec(13, "Trade Goods", "Fine Ink", 5, 95102),
        ],
    };
    let mut items = Items::new();
    items.insert(95001, look(7, "Moonglow Ink"));
    for (i, n) in [(95100, "Glyph of Warding"), (95101, "Glyph of Haste"), (95102, "Fine Ink")] {
        items.insert(i, look(0, n));
    }
    if tool != 0 {
        items.insert(tool, look(1, "Jeweler's Kit"));
    }
    (state, items)
}

#[test]
fn inscription_and_jewelcrafting_open_in_the_stock_trade_window() {
    let data = benilla_formats::wow_data_or_skip!();
    for trade in Trade::ALL {
        let mut s = stock_vm(&data);
        let (state, mut items) = crafts_book(trade);
        open(&mut s, crafts::trade_state(trade, &state, &items));
        let rows = infos(&s);
        // Two headers, each with its recipes; the orange Haste first under Glyphs.
        assert_eq!(rows.iter().filter(|r| r.1 == "header").count(), 2, "{rows:?}");
        assert_eq!(rows[1], ("Glyph of Haste".into(), "optimal".into()), "{rows:?}");
        let book = if trade == Trade::Inscription { Book::Inscription } else { Book::Jewelcrafting };
        common(&mut s, &Expect {
            book,
            title: trade.name(),
            rank: "15/75",
            header: "Glyphs",
            first: "Glyph of Haste",
            reagent: ("Moonglow Ink", 7, 3),
            can: 2,
            recipe: 12,
        });
        let m = craft_message(book, 12, 1);
        assert_eq!((m.channel, m.body), (CHANNEL, crafts::encode_make(trade, 12, 1)));
        if trade == Trade::Jewelcrafting {
            // The tool on the Requires line, white when carried, red and Create dead without it.
            let req = q::<String>(&s, "return TradeSkillRequirementText:GetText() or ''");
            assert!(req.contains("Jeweler's Kit"), "{req:?}");
            items.get_mut(&20815).unwrap().have = 0;
            s.set_trade_skill(Some(crafts::trade_state(trade, &state, &items)));
            s.fire_event("TRADE_SKILL_UPDATE", vec![]);
            let req = q::<String>(&s, "return TradeSkillRequirementText:GetText() or ''");
            assert!(req.contains("|cffff2020") || req.to_lowercase().contains("ff2020"), "red when missing: {req:?}");
            let (avail, en) = q::<(i64, bool)>(&s, "TradeSkillFrame_SetSelection(TradeSkillFrame.selectedSkill) local _, _, n = GetTradeSkillInfo(TradeSkillFrame.selectedSkill) return n, TradeSkillCreateButton:IsEnabled() == 1");
            // None to make (the stock frame leaves Create to the server, which refuses in red).
            assert_eq!(avail, 0, "no kit, none to make ({en})");
        }
    }
}
