//! The professions as WoW 1.12 has them (WOMW design section 12): every profession the character
//! knows, stock or the layer's own, in one place, and the layer's two (Construction and
//! Minstrelsy) working as the stock trades do.
//!
//! - **Skill lines.** Construction and Minstrelsy show in the stock Skills tab, under Secondary
//!   Skills with their rank bars (`benilla_app::fusion::LayerSkillLines`): the client can't learn a
//!   `SkillLine.dbc` row, so the layer hands the engine its own lines.
//! - **Trade windows.** Each opens in the stock `TradeSkillFrame` itself
//!   (`benilla_app::fusion::LayerTradeSkill`): the recipe list in its difficulty colours under
//!   headers, the reagents with their counts, Create and Create All, the stock sounds. Its recipes
//!   are the layer's ([`book`]): Construction's pieces (made into kits, placed later with the
//!   Build Structure ability; the server's `Construction::Kits`, the professions channel 12) and
//!   Minstrelsy's instruments (the server's book, channel 10 op 63; made as items). Create casts
//!   as a stock trade does ([`making`]): the stock casting bar fills with the recipe's name, one
//!   item asked of the server as each cast completes, Create All counting down in the count box.
//!   `frame_tests` drive each book through the stock frame; [`capture`] pins one open for a
//!   screenshot.
//! - **The hub** ([`hub`], B and the micro menu's Professions button): every profession with its
//!   bar, a button to open its trade window, and the abilities it puts on Utility's bar.
//! - **Inscription and Jewelcrafting** ([`crafts`]): the layer's two primary professions, in the
//!   Skills tab under Professions, with their trainers' windows ([`trainer`]) and the Glyphs &
//!   Gems window ([`augments`]) that puts their glyphs and gems into the kit carried.

pub mod augments;
pub mod book;
pub mod capture;
pub mod crafts;
pub mod hub;
pub mod making;
pub mod trainer;

#[cfg(test)]
mod frame_tests;

use benilla_app::fusion::{
    ChannelMessage, FusionChannelReceived, LayerSkillLine, LayerSkillLines, LayerTradeSkill,
    SendFusionChannel,
};
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::build::Building;
use crate::input::Mw2Input;
use crate::minstrel::Minstrel;
use crate::mode::FpsState;
use book::{Items, ItemLook, LINE_CONSTRUCTION, LINE_MINSTRELSY, MinstrelRecipe};
use crafts::{Trade, TradeState};

/// The professions channel (the server's `Construction::kChannelProfessions`).
pub const CHANNEL: u8 = 12;
/// Client: [1, piece u8, count u16]: make kits of a piece.
pub const OP_CRAFT_KIT: u8 = 1;
/// Server: [2, n u16, n x (piece u8, kits u16)]: the kits held.
pub const OP_KITS: u8 = 2;

pub fn encode_craft_kit(piece: u8, count: u16) -> Vec<u8> {
    let mut b = vec![OP_CRAFT_KIT, piece];
    b.extend_from_slice(&count.to_le_bytes());
    b
}

pub fn decode_kits(b: &[u8]) -> Option<Vec<(u8, u32)>> {
    if b.len() < 3 || b[0] != OP_KITS {
        return None;
    }
    let n = usize::from(u16::from_le_bytes([b[1], b[2]]));
    if b.len() != 3 + n * 3 {
        return None;
    }
    Some(
        (0..n)
            .map(|i| {
                let at = 3 + i * 3;
                (b[at], u32::from(u16::from_le_bytes([b[at + 1], b[at + 2]])))
            })
            .collect(),
    )
}

/// One of the layer's trade windows.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Book {
    Construction,
    Minstrelsy,
    Inscription,
    Jewelcrafting,
}

impl Book {
    pub fn line(self) -> u32 {
        match self {
            Book::Construction => LINE_CONSTRUCTION,
            Book::Minstrelsy => LINE_MINSTRELSY,
            Book::Inscription => crafts::LINE_INSCRIPTION,
            Book::Jewelcrafting => crafts::LINE_JEWELCRAFTING,
        }
    }

    /// One of the layer's two primary trades (the server's `Crafts`).
    pub fn trade(self) -> Option<Trade> {
        match self {
            Book::Inscription => Some(Trade::Inscription),
            Book::Jewelcrafting => Some(Trade::Jewelcrafting),
            _ => None,
        }
    }
}

/// Inscription and Jewelcrafting as the server last sent them (ops 22-24, 32).
#[derive(Resource, Default, Debug, Clone)]
pub struct Crafts {
    pub inscription: TradeState,
    pub jewelcrafting: TradeState,
    /// The trainer's window, last sent.
    pub window: Option<crafts::WindowMsg>,
    pub window_version: u32,
    /// The kit carried's glyphs and gems, last sent.
    pub augments: crafts::AugmentsMsg,
    pub augments_version: u32,
}

impl Crafts {
    pub fn of(&self, t: Trade) -> &TradeState {
        match t {
            Trade::Inscription => &self.inscription,
            Trade::Jewelcrafting => &self.jewelcrafting,
        }
    }
    pub fn of_mut(&mut self, t: Trade) -> &mut TradeState {
        match t {
            Trade::Inscription => &mut self.inscription,
            Trade::Jewelcrafting => &mut self.jewelcrafting,
        }
    }
    /// A trade's (skill, cap) once learned.
    pub fn learned(&self, t: Trade) -> Option<(u16, u16)> {
        let s = self.of(t);
        (s.tier > 0).then_some((s.skill, s.cap.max(book::tier_cap(s.tier))))
    }
}

/// Apply one of the professions channel's crafts messages; false for any other body.
pub fn apply_crafts(c: &mut Crafts, body: &[u8]) -> bool {
    match body.first() {
        Some(&crafts::OP_SKILL) => {
            let Some((t, tier, skill, cap)) = crafts::decode_skill(body) else { return false };
            let s = c.of_mut(t);
            s.tier = tier;
            s.skill = skill;
            s.cap = cap;
        }
        Some(&crafts::OP_BOOK) => {
            let Some((t, book)) = crafts::decode_book(body) else { return false };
            c.of_mut(t).book = book;
        }
        Some(&crafts::OP_WINDOW) => {
            let Some(w) = crafts::decode_window(body) else { return false };
            let s = c.of_mut(w.trade);
            s.tier = w.tier;
            s.skill = w.skill;
            s.cap = w.cap;
            // A trainer opens it; an answer refreshes the one up.
            let keep = w.open || c.window.as_ref().is_some_and(|o| o.trade == w.trade);
            if keep {
                c.window = Some(w);
                c.window_version = c.window_version.wrapping_add(1);
            }
        }
        Some(&crafts::OP_AUGMENTS) => {
            let Some(a) = crafts::decode_augments(body) else { return false };
            c.augments = a;
            c.augments_version = c.augments_version.wrapping_add(1);
        }
        _ => return false,
    }
    true
}

/// Open (or, when it is up, close) one of the layer's trade windows.
#[derive(Message, Clone, Copy, Debug, PartialEq, Eq)]
pub struct OpenBook(pub Book);

/// Minstrelsy's recipes as the server last sent them (op 63).
#[derive(Resource, Default, Debug, Clone)]
pub struct MinstrelBook(pub Vec<MinstrelRecipe>);

/// The trade window up, and the bags as last scanned for it.
#[derive(Resource, Default, Debug)]
pub struct TradeBook {
    pub open: Option<Book>,
    pub items: Items,
    pub scan_at: f32,
    /// The craft cast under way (Create, Create All), whichever window it began in.
    pub making: Option<making::Making>,
    /// A capture's book, held open in place of any other ([`capture`]).
    pub pinned: Option<benilla_ui::script::TradeSkillState>,
}

/// How often the bags are counted again while a trade window is up.
const SCAN_SECS: f32 = 0.4;

pub struct ProfessionsPlugin;

impl Plugin for ProfessionsPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<OpenBook>()
            .init_resource::<MinstrelBook>()
            .init_resource::<Crafts>()
            .add_plugins((augments::AugmentsPlugin, trainer::TrainerPlugin))
            .init_resource::<TradeBook>()
            .add_plugins(hub::HubPlugin)
            .add_systems(
                Update,
                (receive, skill_lines, keys, capture::pin, trade_book)
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

/// The kits held (channel 12) and Minstrelsy's book (channel 10 op 63).
fn receive(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut build: ResMut<Building>,
    mut book: ResMut<MinstrelBook>,
    mut crafts: ResMut<Crafts>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel == CHANNEL {
            if apply_crafts(&mut crafts, &m.body) {
                continue;
            }
            if let Some(kits) = decode_kits(&m.body) {
                build.kits = kits.into_iter().collect();
                build.kits_known = true;
                build.construction_version += 1;
            }
        } else if m.channel == crate::minstrel::CHANNEL
            && m.body.first() == Some(&book::OP_BOOK)
            && let Some(b) = book::decode_minstrel_book(&m.body)
        {
            book.0 = b;
        }
    }
}

/// The layer's skill lines the Skills tab shows, from what the server last said of each:
/// Inscription and Jewelcrafting under Professions (11), Construction and Minstrelsy under
/// Secondary Skills (9).
pub fn layer_lines(build: &Building, minstrel: (u8, u16, u16), crafts: &Crafts) -> Vec<LayerSkillLine> {
    let mut out = Vec::new();
    for t in Trade::ALL {
        if let Some((skill, max)) = crafts.learned(t) {
            out.push(LayerSkillLine {
                id: t.line(),
                name: t.name().into(),
                value: u32::from(skill),
                max: u32::from(max),
                description: t.description().into(),
                category_id: 11,
            });
        }
    }
    if build.tier > 0 {
        out.push(LayerSkillLine {
            id: LINE_CONSTRUCTION,
            name: "Construction".into(),
            value: u32::from(build.skill),
            max: u32::from(build.cap.max(book::tier_cap(build.tier))),
            description: "Building as a trade: pieces made at its trade window and put up with \
                          Build Structure, lumber cut with a Woodcutting Axe."
                .into(),
            category_id: 9,
        });
    }
    let (tier, skill, cap) = minstrel;
    if tier > 0 {
        out.push(LayerSkillLine {
            id: LINE_MINSTRELSY,
            name: "Minstrelsy".into(),
            value: u32::from(skill),
            max: u32::from(cap.max(book::tier_cap(tier))),
            description: "Music as a trade: instruments made at its trade window, songs played \
                          from an instrument's own bar."
                .into(),
            category_id: 9,
        });
    }
    out
}

fn skill_lines(
    build: Res<Building>,
    minstrel: Res<Minstrel>,
    crafts: Res<Crafts>,
    lines: Option<ResMut<LayerSkillLines>>,
) {
    let Some(mut lines) = lines else { return };
    let want = layer_lines(&build, minstrel.skill, &crafts);
    if lines.0 != want {
        lines.0 = want;
    }
}

/// The Minstrelsy key opens its trade window (a Minstrel Trainer still opens the trainer's).
fn keys(input: Res<Mw2Input>, state: Res<FpsState>, mut open: MessageWriter<OpenBook>) {
    if input.minstrelsy && state.in_world {
        open.write(OpenBook(Book::Minstrelsy));
    }
}

/// The bag scan's Lua: for each id, `id \t count \t name \t icon`, a line each. The fork's Lua
/// has the bag calls but no `GetItemCount`.
pub fn scan_lua(ids: &[u32]) -> String {
    let list = ids.iter().map(|i| i.to_string()).collect::<Vec<_>>().join(",");
    format!(
        r#"local ids = {{{list}}}
local have = {{}}
for b = 0, 4 do
  for s = 1, (GetContainerNumSlots(b) or 0) do
    local l = GetContainerItemLink(b, s)
    if l then
      local _, _, id = string.find(l, "item:(%d+)")
      if id then
        local _, c = GetContainerItemInfo(b, s)
        id = tonumber(id)
        have[id] = (have[id] or 0) + (c or 1)
      end
    end
  end
end
local out = {{}}
for _, id in ipairs(ids) do
  local n, _, _, _, _, _, _, _, tex = GetItemInfo(id)
  table.insert(out, id .. "\t" .. (have[id] or 0) .. "\t" .. (n or "") .. "\t" .. (tex or ""))
end
return table.concat(out, "\n")"#
    )
}

/// The scan's answer.
pub fn parse_scan(s: &str) -> Items {
    let mut out = Items::new();
    for line in s.lines() {
        let mut f = line.split('\t');
        let (Some(id), Some(have)) = (f.next(), f.next()) else { continue };
        let (Ok(id), Ok(have)) = (id.parse::<u32>(), have.parse::<u32>()) else { continue };
        let name = f.next().filter(|n| !n.is_empty()).map(str::to_string);
        let icon = f.next().filter(|n| !n.is_empty()).map(str::to_string);
        out.insert(id, ItemLook { have, name, icon });
    }
    out
}

/// What a trade window's Create sends: Construction's kits on channel 12, Minstrelsy's make on
/// its window's act (the recipe in the low half, the count in the high).
pub fn craft_message(book: Book, recipe: u32, count: u32) -> ChannelMessage {
    let count = count.clamp(1, u32::from(u16::MAX)) as u16;
    match book {
        Book::Construction => ChannelMessage {
            channel: CHANNEL,
            body: encode_craft_kit(recipe as u8, count),
        },
        Book::Minstrelsy => ChannelMessage {
            channel: crate::minstrel::CHANNEL,
            body: crate::minstrel::window::encode_act(
                crate::minstrel::window::ACT_MAKE,
                (recipe & 0xFFFF) | (u32::from(count) << 16),
            ),
        },
        Book::Inscription | Book::Jewelcrafting => ChannelMessage {
            channel: CHANNEL,
            body: crafts::encode_make(book.trade().unwrap_or(Trade::Inscription), recipe, u32::from(count)),
        },
    }
}

/// The open trade window: opened and closed, fed to the stock frame while up, its Creates sent.
#[allow(clippy::too_many_arguments)]
fn trade_book(
    time: Res<Time>,
    state: Res<FpsState>,
    build: Res<Building>,
    minstrel: Res<Minstrel>,
    minstrel_book: Res<MinstrelBook>,
    crafts: Res<Crafts>,
    mut script: Option<NonSendMut<UiScript>>,
    mut trade: ResMut<TradeBook>,
    mut opens: MessageReader<OpenBook>,
    layer: Option<ResMut<LayerTradeSkill>>,
    mut send: MessageWriter<SendFusionChannel>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
) {
    let Some(mut layer) = layer else { return };
    if let Some(p) = &trade.pinned {
        if layer.book.as_ref() != Some(p) {
            layer.book = Some(p.clone());
        }
        layer.dos.clear();
        return;
    }
    let t = time.elapsed_secs();
    let at = cameras.iter().next().map_or(Vec3::ZERO, GlobalTransform::translation);
    if std::mem::take(&mut layer.closed) {
        trade.open = None;
    }
    for OpenBook(b) in opens.read() {
        if trade.open == Some(*b) {
            trade.open = None;
        } else {
            trade.open = Some(*b);
            trade.scan_at = 0.0;
            // The trade's skill and book fresh from the server.
            if let Some(t) = b.trade() {
                send.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: crafts::encode_ask(t) }));
            }
            if let Some(s) = script.as_deref() {
                crate::lua::sound(s, "igCharacterInfoOpen");
            }
        }
    }
    let mut acts = Vec::new();
    if !state.in_world {
        trade.open = None;
        acts.extend(making::cancel(&mut trade.making));
    }
    // Create / Create All on the frame: a cast per item, as a stock trade's (a click while one
    // is under way is ignored, as "Another action is in progress").
    if let Some(b) = trade.open {
        for (recipe, count) in std::mem::take(&mut layer.dos) {
            if trade.making.is_some() {
                continue;
            }
            let name = layer
                .book
                .as_ref()
                .and_then(|s| s.recipes.iter().find(|r| r.spell_id == recipe))
                .map_or_else(|| "Crafting".to_string(), |r| r.name.clone());
            let (m, first) = making::Making::begin(b, recipe, &name, count, t, at);
            trade.making = Some(m);
            acts.push(first);
        }
    } else {
        layer.dos.clear();
    }
    // Whether the bags still make one more: the open book's count, else the server decides.
    let can = trade.making.as_ref().is_none_or(|m| {
        trade.open != Some(m.book)
            || layer.book.as_ref().is_none_or(|s| {
                s.recipes
                    .iter()
                    .find(|r| r.spell_id == m.recipe)
                    .is_none_or(|r| r.reagents.is_empty() || r.num_available > 0)
            })
    });
    acts.extend(making::step(&mut trade.making, t, at, can));
    for a in acts {
        match a {
            making::Act::Make { book, recipe } => {
                send.write(SendFusionChannel(craft_message(book, recipe, 1)));
            }
            making::Act::Bar(e) => {
                if let Some(s) = script.as_deref_mut() {
                    crate::cast_bar::apply(s, vec![e]);
                }
            }
        }
    }
    let repeat = trade
        .making
        .as_ref()
        .filter(|m| Some(m.book) == trade.open && m.left > 1)
        .map_or(0, |m| m.left);
    let Some(b) = trade.open else {
        if layer.book.is_some() {
            layer.book = None;
        }
        return;
    };
    if t >= trade.scan_at
        && let Some(s) = script.as_deref()
    {
        trade.scan_at = t + SCAN_SECS;
        let ids = match b {
            Book::Construction => book::construction_items(&build),
            Book::Minstrelsy => book::minstrel_items(&minstrel_book.0),
            Book::Inscription | Book::Jewelcrafting => {
                crafts::book_items(crafts.of(b.trade().unwrap_or(Trade::Inscription)))
            }
        };
        if let Some(out) = crate::lua::eval::<String>(s, &scan_lua(&ids)) {
            trade.items = parse_scan(&out);
        }
    }
    let fresh = match b {
        Book::Construction => book::construction_state(&build, &trade.items),
        Book::Minstrelsy => {
            book::minstrel_state(&minstrel_book.0, minstrel.skill.0, minstrel.skill.1, &trade.items)
        }
        Book::Inscription | Book::Jewelcrafting => {
            let t = b.trade().unwrap_or(Trade::Inscription);
            crafts::trade_state(t, crafts.of(t), &trade.items)
        }
    };
    let mut fresh = fresh;
    fresh.repeat_count = repeat;
    if layer.book.as_ref() != Some(&fresh) {
        layer.book = Some(fresh);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_kit_wire_reads_as_the_server_writes_it() {
        // Construction.cpp EncodeKits: two pieces.
        let b = vec![OP_KITS, 2, 0, 7, 3, 0, 9, 1, 0];
        assert_eq!(decode_kits(&b), Some(vec![(7, 3), (9, 1)]));
        assert_eq!(decode_kits(&b[..8]), None);
        assert_eq!(encode_craft_kit(7, 5), vec![OP_CRAFT_KIT, 7, 5, 0]);
    }

    #[test]
    fn create_goes_to_each_professions_own_server_op() {
        let c = craft_message(Book::Construction, 7, 3);
        assert_eq!((c.channel, c.body), (CHANNEL, vec![OP_CRAFT_KIT, 7, 3, 0]));
        let m = craft_message(Book::Minstrelsy, 2, 4);
        assert_eq!(m.channel, crate::minstrel::CHANNEL);
        assert_eq!(m.body, crate::minstrel::window::encode_act(crate::minstrel::window::ACT_MAKE, 2 | 4 << 16));
        assert_eq!(Book::Minstrelsy.line(), 9002);
        let i = craft_message(Book::Inscription, 12, 5);
        assert_eq!((i.channel, i.body), (CHANNEL, crafts::encode_make(Trade::Inscription, 12, 5)));
        assert_eq!(Book::Jewelcrafting.line(), 9004);
        assert_eq!(Book::Jewelcrafting.trade(), Some(Trade::Jewelcrafting));
        assert_eq!(Book::Construction.trade(), None);
    }

    #[test]
    fn crafts_messages_land_in_their_trade() {
        let mut c = Crafts::default();
        let mut skill = vec![crafts::OP_SKILL, 1, 1];
        skill.extend_from_slice(&40u16.to_le_bytes());
        skill.extend_from_slice(&75u16.to_le_bytes());
        assert!(apply_crafts(&mut c, &skill));
        assert_eq!(c.learned(Trade::Inscription), Some((40, 75)));
        assert_eq!(c.learned(Trade::Jewelcrafting), None);
        assert!(!apply_crafts(&mut c, &[OP_KITS, 0, 0]), "Construction's kits are not the crafts'");
        assert!(!apply_crafts(&mut c, &[crafts::OP_SKILL, 9]));
    }

    #[test]
    fn the_bag_scan_reads_back() {
        let items = parse_scan("91001\t12\tRough Lumber\tInterface\\Icons\\INV_X\n91020\t0\t\t\nbad\n");
        assert_eq!(items[&91001].have, 12);
        assert_eq!(items[&91001].name.as_deref(), Some("Rough Lumber"));
        assert_eq!(items[&91020], ItemLook { have: 0, name: None, icon: None });
        assert_eq!(items.len(), 2);
        assert!(scan_lua(&[1, 2]).contains("local ids = {1,2}"));
    }

    #[test]
    fn the_layers_lines_are_its_learned_professions() {
        let mut b = Building::default();
        let none = Crafts::default();
        assert!(layer_lines(&b, (0, 0, 0), &none).is_empty());
        b.tier = 2;
        b.skill = 80;
        b.cap = 150;
        let l = layer_lines(&b, (1, 12, 75), &none);
        assert_eq!(l.len(), 2);
        assert_eq!((l[0].id, l[0].value, l[0].max, l[0].category_id), (9001, 80, 150, 9));
        assert_eq!((l[1].name.as_str(), l[1].value, l[1].max), ("Minstrelsy", 12, 75));
        // Inscription and Jewelcrafting are primary professions: the Professions header (11).
        let mut c = Crafts::default();
        c.jewelcrafting = TradeState { tier: 3, skill: 180, cap: 225, book: Vec::new() };
        let l = layer_lines(&b, (0, 0, 0), &c);
        assert_eq!((l[0].id, l[0].name.as_str(), l[0].value, l[0].max, l[0].category_id), (9004, "Jewelcrafting", 180, 225, 11));
        assert_eq!(l.len(), 2);
    }

    #[test]
    fn the_scan_runs_against_a_vm() {
        let s = UiScript::new().expect("a VM");
        // A bare VM: no bags; the stubs answer as an empty character would.
        crate::lua::run(
            &s,
            "function GetContainerNumSlots() return 0 end function GetItemInfo() return nil end",
        );
        let out = crate::lua::eval::<String>(&s, &scan_lua(&[91001])).expect("scan");
        assert_eq!(parse_scan(&out)[&91001].have, 0);
    }
}
