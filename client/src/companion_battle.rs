//! Companion duels (the server's `fusion/CompanionBattles.h`, `game/Fusion/FusionCompanionBattles.cpp`;
//! design/COMPANION_BATTLES.md), on fusion channel 13, wire version 2.
//!
//! A turn-based card duel after Slay the Spire, against a companion tamer, a wanderer or another
//! player. The **Companion Duel** window is laid out in the stock dialog look: the opponent's
//! companion on top (portrait, health, block, statuses and, for an NPC, its **intent**: what it
//! will do on its turn), your companion below with the energy orb and the draw, discard and
//! exhaust piles, then your hand as cards on parchment with their spell icons, and End Turn.
//! Keys: 1-0 play the hand's cards, E ends the turn. Cards deal in when a hand is drawn, lift
//! under the pointer and fly off when played.
//!
//! The **Companion Deck** window (op 4, `.fusion petbattle deck` or the duel window's Deck
//! button) lists what a companion knows and how many of each its deck holds; + and - change it
//! and Save asks the server, which checks it against the deck rules (12-30 cards, copies).
//!
//! Every play and every deck is the server's to judge; the windows only ask.

use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, Kind, RED, WHITE, WindowSpec, WowButton,
    WowTip, text, u, uf,
};

/// The companion duel channel (the server's `CompanionBattles::kChannel`).
pub const CHANNEL: u8 = 13;
/// The card duel's wire (the Pokémon-style battle was version 1).
const VERSION: u8 = 2;
// Server to client.
const OP_STATE: u8 = 1;
const OP_CHALLENGED: u8 = 2;
const OP_TELL: u8 = 3;
const OP_DECK: u8 = 4;
// Client to server.
const OP_CHALLENGE: u8 = 10;
const OP_ANSWER: u8 = 11;
const OP_ACTION: u8 = 12;
const OP_ASK_DECK: u8 = 14;
const OP_SAVE_DECK: u8 = 15;

/// `CompanionBattles::Phase`.
pub const PHASE_PLAYING: u8 = 0;
pub const PHASE_OVER: u8 = 2;

/// `CompanionBattles::CardType`.
pub const CARD_ATTACK: u8 = 0;
pub const CARD_SKILL: u8 = 1;
pub const CARD_POWER: u8 = 2;

/// Blizzard's ten battle families (`CompanionBattles::BattleFamily`), the window's label.
pub const FAMILIES: [&str; 10] = [
    "Humanoid",
    "Dragonkin",
    "Flying",
    "Undead",
    "Critter",
    "Magic",
    "Elemental",
    "Beast",
    "Aquatic",
    "Mechanical",
];

/// `CompanionBattles::Archetype`: how a family's deck plays.
pub const ARCHETYPES: [(&str, &str); 13] = [
    ("Feline", "bleeds and combos"),
    ("Ursine", "blocks and thorns"),
    ("Avian", "evades and draws"),
    ("Draconic", "powers that grow"),
    ("Undead", "exhausts and returns"),
    ("Venom", "poisons"),
    ("Pack", "strength and many hits"),
    ("Elemental", "energy and big spells"),
    ("Fel", "curses and drains"),
    ("Humanoid", "tactics and draw"),
    ("Shell", "block that hits back"),
    ("Mechanical", "overclock and plating"),
    ("Critter", "cheap and quick"),
];

/// `CompanionBattles::Status`: name, icon, what it does.
pub const STATUSES: [(&str, &str, &str); 19] = [
    ("Exposed", "Ability_Warrior_Sunder", "Takes 50% more damage from attacks."),
    ("Weakened", "Spell_Shadow_CurseOfMannoroth", "Deals 25% less damage with attacks."),
    ("Poison", "Ability_PoisonSting", "Loses this much health at the start of its turn, then 1 less. Block doesn't help."),
    ("Bleed", "Ability_Druid_Rake", "Loses this much health at the start of its turn, then half as much."),
    ("Strength", "Spell_Nature_Strength", "Each hit deals this much more."),
    ("Agility", "Ability_Rogue_Sprint", "Each block card gives this much more Block."),
    ("Thorns", "Spell_Nature_Thorns", "Whoever hits it takes this much."),
    ("Evasion", "Spell_Magic_FeatherFall", "The next hits on it miss, one per point."),
    ("Regeneration", "Spell_Nature_Rejuvenation", "Heals this much at the end of its turn, then 1 less."),
    ("Draconic Might", "INV_Misc_Head_Dragon_01", "Gains this much Strength at the start of each turn."),
    ("Fortify", "Ability_Defend", "Gains this much Block at the start of each turn."),
    ("Plague", "Spell_Nature_AbolishMagic", "Poisons its enemy this much at the start of each turn."),
    ("Insight", "Ability_Hunter_EagleEye", "Draws this many more cards each turn."),
    ("Plating", "INV_Shield_06", "Its Block no longer falls away."),
    ("Frenzy", "Ability_Druid_DemoralizingRoar", "Each attack after its first each turn deals this much more."),
    ("Undying", "Spell_Shadow_RaiseDead", "Gains this much Block whenever one of its cards is exhausted."),
    ("Drained", "Ability_ThunderBolt", "Has this much less energy next turn."),
    ("Attunement", "Spell_Frost_SummonWaterElemental", "Gains this much more energy each turn."),
    ("Bloodlust", "Spell_Nature_BloodLust", "Gains this much Strength each turn, for 2 health."),
];

/// The window's own icons.
const ICON_ENERGY: &str = "Spell_Holy_MagicalSentry";
const ICON_DRAW: &str = "INV_Misc_Bag_10";
const ICON_DISCARD: &str = "INV_Misc_Bag_08";
const ICON_EXHAUST: &str = "Spell_Fire_Incinerate";
const ICON_ATTACK: &str = "Ability_MeleeDamage";
const ICON_BLOCK: &str = "Ability_Defend";
const ICON_BUFF: &str = "Ability_Warrior_BattleShout";
const ICON_DEBUFF: &str = "Spell_Shadow_PsychicScream";
const ICON_UNKNOWN: &str = "INV_Misc_QuestionMark";
/// The mail's stationery: the cards' parchment.
const PARCHMENT: &str = "mpq://Interface/Stationery/StationeryTest1.blp";

// ── The wire ────────────────────────────────────────────────────────────────────────────────

#[derive(Clone, Debug, Default, PartialEq)]
pub struct CardView {
    pub id: u16,
    pub kind: u8,
    pub cost: u8,
    pub playable: bool,
    /// What it deals per hit and blocks now, with the statuses.
    pub damage: u16,
    pub block: u16,
    pub spell: u32,
    pub icon: String,
    pub name: String,
    pub text: String,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct PetView {
    pub species: u16,
    pub family: u8,
    pub battle_family: u8,
    pub archetype: u8,
    pub stage: u8,
    pub level: u8,
    pub health: u16,
    pub max_health: u16,
    pub beaten: bool,
    pub name: String,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Intent {
    pub known: bool,
    pub damage: u16,
    pub hits: u8,
    pub block: u16,
    pub buff: bool,
    pub debuff: bool,
    pub power: bool,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct SideView {
    pub owner: String,
    pub active: u8,
    pub hand: u8,
    pub draw: u8,
    pub discard: u8,
    pub exhaust: u8,
    pub block: u16,
    pub statuses: Vec<(u8, i16)>,
    pub intent: Intent,
    pub pets: Vec<PetView>,
}

impl SideView {
    pub fn fighting(&self) -> Option<&PetView> {
        self.pets.get(usize::from(self.active))
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct State {
    pub battle: u32,
    pub phase: u8,
    pub turn: u16,
    pub end: u8,
    /// 0 you, 1 them, -1 nobody (yet).
    pub winner: i8,
    pub your_turn: bool,
    pub seconds: u8,
    pub energy: u8,
    pub max_energy: u8,
    pub hand: Vec<CardView>,
    /// Yours first.
    pub sides: [SideView; 2],
    pub lines: Vec<String>,
    pub rewards: String,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct LibraryEntry {
    pub card: CardView,
    pub copies: u8,
    pub source: u8,
    pub in_deck: u8,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Deck {
    pub item: u64,
    pub name: String,
    pub min: u8,
    pub max: u8,
    pub library: Vec<LibraryEntry>,
    pub note: String,
}

impl Deck {
    pub fn size(&self) -> usize {
        self.library.iter().map(|l| usize::from(l.in_deck)).sum()
    }
    /// The deck as the server takes it: each card as many times as it holds.
    pub fn cards(&self) -> Vec<u16> {
        self.library
            .iter()
            .flat_map(|l| std::iter::repeat_n(l.card.id, usize::from(l.in_deck)))
            .collect()
    }
    /// Whether the deck rules hold (the server checks again).
    pub fn valid(&self) -> bool {
        let n = self.size();
        n >= usize::from(self.min)
            && n <= usize::from(self.max)
            && self.library.iter().all(|l| l.in_deck <= l.copies)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Message {
    State(Box<State>),
    Challenged { guid: u64, name: String },
    Tell(String),
    Deck(Box<Deck>),
}

struct Reader<'a> {
    b: &'a [u8],
    at: usize,
}

impl<'a> Reader<'a> {
    fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        let s = self.b.get(self.at..self.at + n)?;
        self.at += n;
        Some(s)
    }
    fn u8(&mut self) -> Option<u8> {
        self.take(1).map(|s| s[0])
    }
    fn u16(&mut self) -> Option<u16> {
        self.take(2).map(|s| u16::from_le_bytes([s[0], s[1]]))
    }
    fn u32(&mut self) -> Option<u32> {
        self.take(4)
            .map(|s| u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
    }
    fn u64(&mut self) -> Option<u64> {
        let lo = u64::from(self.u32()?);
        let hi = u64::from(self.u32()?);
        Some(lo | hi << 32)
    }
    fn str8(&mut self) -> Option<String> {
        let n = usize::from(self.u8()?);
        Some(String::from_utf8_lossy(self.take(n)?).into_owned())
    }
    fn str16(&mut self) -> Option<String> {
        let n = usize::from(self.u16()?);
        Some(String::from_utf8_lossy(self.take(n)?).into_owned())
    }
}

fn read_card(r: &mut Reader) -> Option<CardView> {
    Some(CardView {
        id: r.u16()?,
        kind: r.u8()?,
        cost: r.u8()?,
        playable: r.u8()? != 0,
        damage: r.u16()?,
        block: r.u16()?,
        spell: r.u32()?,
        icon: r.str8()?,
        name: r.str8()?,
        text: r.str16()?,
    })
}

fn read_side(r: &mut Reader) -> Option<SideView> {
    let mut s = SideView {
        owner: r.str8()?,
        active: r.u8()?,
        hand: r.u8()?,
        draw: r.u8()?,
        discard: r.u8()?,
        exhaust: r.u8()?,
        block: r.u16()?,
        ..default()
    };
    for _ in 0..r.u8()? {
        s.statuses.push((r.u8()?, r.u16()? as i16));
    }
    let known = r.u8()? != 0;
    let damage = r.u16()?;
    let hits = r.u8()?;
    let block = r.u16()?;
    let flags = r.u8()?;
    s.intent = Intent {
        known,
        damage,
        hits,
        block,
        buff: flags & 1 != 0,
        debuff: flags & 2 != 0,
        power: flags & 4 != 0,
    };
    for _ in 0..r.u8()? {
        s.pets.push(PetView {
            species: r.u16()?,
            family: r.u8()?,
            battle_family: r.u8()?,
            archetype: r.u8()?,
            stage: r.u8()?,
            level: r.u8()?,
            health: r.u16()?,
            max_health: r.u16()?,
            beaten: r.u8()? != 0,
            name: r.str8()?,
        });
    }
    Some(s)
}

pub fn decode(body: &[u8]) -> Option<Message> {
    let mut r = Reader { b: body, at: 0 };
    if r.u8()? != VERSION {
        return None;
    }
    match r.u8()? {
        OP_STATE => {
            let mut s = State {
                battle: r.u32()?,
                phase: r.u8()?,
                turn: r.u16()?,
                end: r.u8()?,
                winner: r.u8()? as i8,
                your_turn: r.u8()? != 0,
                seconds: r.u8()?,
                energy: r.u8()?,
                max_energy: r.u8()?,
                ..default()
            };
            for _ in 0..r.u8()? {
                s.hand.push(read_card(&mut r)?);
            }
            for side in &mut s.sides {
                *side = read_side(&mut r)?;
            }
            for _ in 0..r.u8()? {
                s.lines.push(r.str16()?);
            }
            s.rewards = r.str16()?;
            Some(Message::State(Box::new(s)))
        }
        OP_CHALLENGED => Some(Message::Challenged {
            guid: r.u64()?,
            name: r.str8()?,
        }),
        OP_TELL => Some(Message::Tell(r.str16()?)),
        OP_DECK => {
            let mut d = Deck {
                item: r.u64()?,
                name: r.str8()?,
                min: r.u8()?,
                max: r.u8()?,
                ..default()
            };
            for _ in 0..r.u8()? {
                d.library.push(LibraryEntry {
                    card: read_card(&mut r)?,
                    copies: r.u8()?,
                    source: r.u8()?,
                    in_deck: r.u8()?,
                });
            }
            d.note = r.str16()?;
            Some(Message::Deck(Box::new(d)))
        }
        _ => None,
    }
}

/// What the windows ask the server.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Action {
    /// A hand slot and the card the window sees there (the server refuses a stale one).
    Play { index: u8, card: u16 },
    EndTurn,
    Forfeit,
    Accept,
    Decline,
    Challenge(u64),
    /// 0: the companion out.
    AskDeck(u64),
    SaveDeck(u64, Vec<u16>),
}

pub fn encode(a: &Action) -> Vec<u8> {
    match a {
        Action::Play { index, card } => {
            let [lo, hi] = card.to_le_bytes();
            vec![VERSION, OP_ACTION, 1, *index, lo, hi]
        }
        Action::EndTurn => vec![VERSION, OP_ACTION, 3, 0, 0, 0],
        Action::Forfeit => vec![VERSION, OP_ACTION, 4, 0, 0, 0],
        Action::Accept => vec![VERSION, OP_ANSWER, 1],
        Action::Decline => vec![VERSION, OP_ANSWER, 0],
        Action::Challenge(g) => {
            let mut v = vec![VERSION, OP_CHALLENGE];
            v.extend(g.to_le_bytes());
            v
        }
        Action::AskDeck(g) => {
            let mut v = vec![VERSION, OP_ASK_DECK];
            v.extend(g.to_le_bytes());
            v
        }
        Action::SaveDeck(g, cards) => {
            let mut v = vec![VERSION, OP_SAVE_DECK];
            v.extend(g.to_le_bytes());
            let n = cards.len().min(30);
            v.push(n as u8);
            for c in &cards[..n] {
                v.extend(c.to_le_bytes());
            }
            v
        }
    }
}

// ── Words ───────────────────────────────────────────────────────────────────────────────────

pub fn family_name(f: u8) -> &'static str {
    FAMILIES.get(usize::from(f)).copied().unwrap_or("Critter")
}

pub fn archetype_name(a: u8) -> &'static str {
    ARCHETYPES.get(usize::from(a)).map_or("Companion", |a| a.0)
}

pub fn card_type_name(k: u8) -> &'static str {
    match k {
        CARD_ATTACK => "Attack",
        CARD_SKILL => "Skill",
        CARD_POWER => "Power",
        _ => "Card",
    }
}

pub fn status_name(k: u8) -> &'static str {
    STATUSES.get(usize::from(k)).map_or("Effect", |s| s.0)
}

/// An intent as the window says it: "Attack 12 x2, block 8".
pub fn intent_text(i: &Intent) -> String {
    if !i.known {
        return "Unknown".into();
    }
    let mut parts = Vec::new();
    if i.damage > 0 {
        if i.hits > 1 {
            parts.push(format!("Attack {} x{}", i.damage, i.hits));
        } else {
            parts.push(format!("Attack {}", i.damage));
        }
    }
    if i.block > 0 {
        parts.push(format!("Block {}", i.block));
    }
    if i.power {
        parts.push("Power".into());
    }
    if i.buff {
        parts.push("Buff".into());
    }
    if i.debuff {
        parts.push("Debuff".into());
    }
    if parts.is_empty() {
        "Waiting".into()
    } else {
        parts.join(", ")
    }
}

/// The icon an intent shows (its strongest part).
pub fn intent_icon(i: &Intent) -> &'static str {
    if !i.known {
        ICON_UNKNOWN
    } else if i.damage > 0 {
        ICON_ATTACK
    } else if i.debuff {
        ICON_DEBUFF
    } else if i.block > 0 {
        ICON_BLOCK
    } else if i.buff || i.power {
        ICON_BUFF
    } else {
        ICON_UNKNOWN
    }
}

/// What the window says the duel is waiting on.
pub fn prompt(s: &State) -> String {
    match s.phase {
        PHASE_OVER => match s.winner {
            0 => "Victory!".into(),
            1 => "Defeat.".into(),
            _ => "The duel is over.".into(),
        },
        _ if s.your_turn => format!("Turn {}: your turn.", s.turn),
        _ => format!("Turn {}: {} is playing...", s.turn, s.sides[1].owner),
    }
}

/// The window's top line: the prompt, and on your turn the seconds left.
pub fn header(s: &State, secs: u32) -> String {
    if s.phase == PHASE_OVER || !s.your_turn {
        prompt(s)
    } else {
        format!("{}    {secs}s", prompt(s))
    }
}

/// A card's numbers now, as its face's last line: "Deals 9", "Blocks 7".
pub fn card_now(c: &CardView) -> String {
    let mut parts = Vec::new();
    if c.kind == CARD_ATTACK {
        parts.push(format!("Deals {}", c.damage));
    }
    if c.block > 0 {
        parts.push(format!("Blocks {}", c.block));
    }
    parts.join(", ")
}

/// The key that plays hand slot `i`: 1-9, then 0.
pub fn slot_key(i: usize) -> Option<&'static str> {
    ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"]
        .get(i)
        .copied()
}

const DIGITS: [KeyCode; 10] = [
    KeyCode::Digit1,
    KeyCode::Digit2,
    KeyCode::Digit3,
    KeyCode::Digit4,
    KeyCode::Digit5,
    KeyCode::Digit6,
    KeyCode::Digit7,
    KeyCode::Digit8,
    KeyCode::Digit9,
    KeyCode::Digit0,
];

/// What a key asks in this state: 1-0 a playable card, E the end of the turn.
pub fn key_action(s: &State, key: KeyCode) -> Option<Action> {
    if s.phase != PHASE_PLAYING || !s.your_turn {
        return None;
    }
    if key == KeyCode::KeyE {
        return Some(Action::EndTurn);
    }
    let i = DIGITS.iter().position(|k| *k == key)?;
    let c = s.hand.get(i)?;
    c.playable.then_some(Action::Play {
        index: i as u8,
        card: c.id,
    })
}

// ── The windows ─────────────────────────────────────────────────────────────────────────────

const LOG_KEEP: usize = 200;
const LOG_SHOWN: usize = 5;
const WIDTH: f32 = 760.0;
const CARD_W: f32 = 98.0;
const CARD_H: f32 = 150.0;
/// The cards' ink and parchment.
const INK: Color = Color::srgb(0.22, 0.12, 0.04);
const INK_SOFT: Color = Color::srgb(0.38, 0.24, 0.10);
const PAPER: Color = Color::srgb(0.86, 0.77, 0.58);
const PAPER_EDGE: Color = Color::srgb(0.42, 0.30, 0.16);
const READY_EDGE: Color = Color::srgb(1.0, 0.82, 0.25);
const ORB: Color = Color::srgb(0.12, 0.22, 0.55);

/// A card leaving the hand: its face, flying up and away.
#[derive(Clone, Debug)]
struct Fly {
    icon: String,
    spell: u32,
    name: String,
    slot: usize,
    at: f32,
}

#[derive(Resource, Default)]
pub struct BattleUi {
    pub open: bool,
    pub state: Option<State>,
    pub log: Vec<String>,
    pub challenge: Option<String>,
    pub status: Option<String>,
    pub received_at: f32,
    pub version: u32,
    /// When the shown hand was dealt (its cards slide in), and which turn's it is.
    pub dealt: Option<(u32, u16)>,
    pub deal_at: f32,
    flying: Vec<Fly>,
    drawn: Option<u32>,
    shown_secs: u32,
    held_cursor: bool,
}

impl BattleUi {
    fn bump(&mut self) {
        self.version = self.version.wrapping_add(1);
    }
    fn fighting(&self) -> bool {
        self.state.as_ref().is_some_and(|s| s.phase != PHASE_OVER)
    }
}

#[derive(Resource, Default)]
pub struct DeckUi {
    pub open: bool,
    pub deck: Option<Deck>,
    pub version: u32,
    drawn: Option<u32>,
}

#[derive(Component)]
struct BattleRoot;
#[derive(Component)]
struct BattleClose;
#[derive(Component)]
struct BattleBody;
#[derive(Component)]
struct DeckRoot;
#[derive(Component)]
struct DeckClose;
#[derive(Component)]
struct DeckBody;
/// A button that asks the server (None: close the window).
#[derive(Component, Clone)]
struct BattleButton(Option<Action>);
/// A card in the hand: its slot.
#[derive(Component, Clone, Copy)]
struct HandCard {
    slot: usize,
    deal: bool,
}
/// A card's face flying off (an overlay on the window).
#[derive(Component)]
struct Flying(f32);
#[derive(Component, Clone, Copy, Debug)]
enum DeckButton {
    Less(usize),
    More(usize),
    Save,
}
/// The header line with the turn's clock (updated in place every second, not rebuilt).
#[derive(Component)]
struct ClockText;
#[derive(Resource)]
struct BattleFont(Handle<Font>);

pub struct CompanionBattlePlugin;

impl Plugin for CompanionBattlePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<BattleUi>()
            .init_resource::<DeckUi>()
            .add_systems(
                Update,
                (
                    spawn_windows,
                    receive,
                    open_close,
                    keys,
                    click,
                    deck_click,
                    draw,
                    draw_deck,
                    animate,
                )
                    .chain()
                    .in_set(Mw2Set::Hud),
            )
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

/// A test probe (crate::probe): `MW2_PROBE_DUEL_UI=1` opens the duel window on a sample duel,
/// in the world or not (a headless capture: client/docs/CAPTURE.md).
fn probing() -> bool {
    std::env::var_os("MW2_PROBE_DUEL_UI").is_some() || crate::capture_keep::keeps("duel")
}

fn sample_card(id: u16, kind: u8, cost: u8, icon: &str, name: &str, text: &str, damage: u16, block: u16) -> CardView {
    CardView {
        id,
        kind,
        cost,
        playable: true,
        damage,
        block,
        spell: 0,
        icon: icon.into(),
        name: name.into(),
        text: text.into(),
    }
}

/// A duel as a tamer's might look on turn 5: a cat against a wolf pup that means to bite twice.
pub fn sample_state() -> State {
    let pet = |name: &str, family: u8, archetype: u8, health: u16, beaten: bool| PetView {
        species: 6,
        family,
        battle_family: 7,
        archetype,
        stage: 1,
        level: 24,
        health,
        max_health: 84,
        beaten,
        name: name.into(),
    };
    let mut hand = vec![
        sample_card(100, CARD_ATTACK, 1, "Ability_Druid_Disembowel", "Rake", "Deal 3 damage. Apply 3 Bleed.", 4, 0),
        sample_card(101, CARD_ATTACK, 0, "Ability_Druid_SupriseAttack", "Pounce", "Deal 3 damage. Combo: +4 after another attack.", 4, 0),
        sample_card(2, CARD_ATTACK, 1, "Ability_Druid_Rake", "Claw", "Deal 6 damage.", 9, 0),
        sample_card(10, CARD_SKILL, 1, "Ability_Defend", "Guard", "Gain 5 Block.", 0, 5),
        sample_card(105, CARD_POWER, 1, "Ability_Druid_DemoralizingRoar", "Savage Roar", "Each attack after your first each turn deals 2 more damage.", 0, 0),
        sample_card(104, CARD_ATTACK, 2, "Ability_GhoulFrenzy", "Rip", "Deal 8 damage. + the target's Bleed.", 15, 0),
    ];
    hand[5].playable = false;
    State {
        battle: 1,
        phase: PHASE_PLAYING,
        turn: 5,
        end: 0,
        winner: -1,
        your_turn: true,
        seconds: 38,
        energy: 2,
        max_energy: 3,
        hand,
        sides: [
            SideView {
                owner: "Ellandra".into(),
                active: 0,
                hand: 6,
                draw: 4,
                discard: 6,
                exhaust: 1,
                block: 0,
                statuses: vec![(4, 1), (7, 1)],
                intent: Intent::default(),
                pets: vec![pet("Nightsaber Cub", 4, 0, 61, false), pet("Strigid Owlet", 10, 2, 78, false)],
            },
            SideView {
                owner: "Julia Stevens".into(),
                active: 0,
                hand: 5,
                draw: 7,
                discard: 3,
                exhaust: 0,
                block: 6,
                statuses: vec![(0, 1), (3, 2)],
                intent: Intent {
                    known: true,
                    damage: 6,
                    hits: 2,
                    block: 5,
                    ..default()
                },
                pets: vec![pet("Wolf Pup", 3, 6, 47, false), pet("Tabby Cat", 4, 0, 1, true)],
            },
        ],
        lines: vec![
            "Wolf Pup plays Pack Tactics.".into(),
            "Nightsaber Cub takes 8.".into(),
            "Nightsaber Cub plays Rake.".into(),
            "Wolf Pup takes 4.".into(),
        ],
        rewards: String::new(),
    }
}

fn spawn_windows(
    mut commands: Commands,
    assets: Res<AssetServer>,
    state: Res<FpsState>,
    mut built: Local<bool>,
    mut ui: ResMut<BattleUi>,
    time: Res<Time>,
) {
    if *built || !(state.in_world || probing()) {
        return;
    }
    if probing() {
        info!("benilla-mw2: duel probe: the sample duel's window is open");
        apply_state(&mut ui, sample_state(), time.elapsed_secs() - 5.0);
        ui.log = ui.state.as_ref().map(|s| s.lines.clone()).unwrap_or_default();
        ui.bump();
    }
    *built = true;
    let font: Handle<Font> = assets.load(FONT);
    wow_ui::spawn_window(
        &mut commands,
        &font,
        WindowSpec {
            key: "companion duel",
            title: "Companion Duel".into(),
            frame: Frame::Dialog,
            width: Some(WIDTH),
            movable: true,
            anchor: Anchor {
                at: Vec2::new(0.5, 0.0),
                pivot: Vec2::new(0.5, 0.0),
                offset: Vec2::new(0.0, 40.0),
            },
            z: i32::MAX - 3,
        },
        (BattleRoot, Visibility::Hidden, wow_ui::WindowSounds::MENU),
        Some(BattleClose),
        |root| {
            root.spawn((
                BattleBody,
                Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: u(4.0),
                    ..default()
                },
            ));
        },
        |_| {},
    );
    wow_ui::spawn_window(
        &mut commands,
        &font,
        WindowSpec {
            key: "companion deck",
            title: "Companion Deck".into(),
            frame: Frame::Dialog,
            width: Some(640.0),
            movable: true,
            anchor: Anchor {
                at: Vec2::new(0.5, 0.5),
                pivot: Vec2::new(0.5, 0.5),
                offset: Vec2::ZERO,
            },
            z: i32::MAX - 4,
        },
        (DeckRoot, Visibility::Hidden, wow_ui::WindowSounds::MENU),
        Some(DeckClose),
        |root| {
            root.spawn((
                DeckBody,
                Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: u(4.0),
                    ..default()
                },
            ));
        },
        |_| {},
    );
    commands.insert_resource(BattleFont(font));
}

fn receive(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut ui: ResMut<BattleUi>,
    mut deck: ResMut<DeckUi>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL {
            continue;
        }
        let Some(msg) = decode(&m.body) else {
            warn!("benilla-mw2: a companion duel message it can't read");
            continue;
        };
        let now = time.elapsed_secs();
        match msg {
            Message::State(s) => apply_state(&mut ui, *s, now),
            Message::Challenged { name, .. } => {
                if !ui.fighting() {
                    ui.state = None;
                    ui.log.clear();
                }
                ui.challenge = Some(name);
                ui.open = true;
            }
            Message::Tell(t) => {
                ui.status = Some(t);
                deck.version = deck.version.wrapping_add(1);
            }
            Message::Deck(d) => {
                deck.deck = Some(*d);
                deck.open = true;
                deck.version = deck.version.wrapping_add(1);
            }
        }
        ui.bump();
    }
}

/// A new state: its log lines kept, and a new hand dealt in when the turn turns to you.
fn apply_state(ui: &mut BattleUi, s: State, now: f32) {
    let fresh = ui.state.as_ref().is_none_or(|o| o.battle != s.battle);
    if fresh {
        ui.log.clear();
        ui.status = None;
        ui.dealt = None;
        ui.flying.clear();
    }
    ui.log.extend(s.lines.iter().cloned());
    let over = ui.log.len().saturating_sub(LOG_KEEP);
    ui.log.drain(..over);
    let hand_of = (s.battle, s.turn);
    if s.your_turn && ui.dealt != Some(hand_of) {
        ui.dealt = Some(hand_of);
        ui.deal_at = now;
    }
    ui.open = true;
    ui.challenge = None;
    ui.state = Some(s);
    ui.received_at = now;
}

/// Esc and the close buttons close them; a duel in progress keeps its window (forfeit to leave).
fn open_close(
    input: Res<Mw2Input>,
    state: Res<FpsState>,
    mut ui: ResMut<BattleUi>,
    mut deck: ResMut<DeckUi>,
    close: Query<&Interaction, (With<BattleClose>, Changed<Interaction>)>,
    close_deck: Query<&Interaction, (With<DeckClose>, Changed<Interaction>)>,
) {
    let pressed = close.iter().any(|i| *i == Interaction::Pressed);
    let fighting = ui.fighting();
    let in_world = state.in_world || probing();
    if ui.open && (!in_world || (!fighting && (input.escape || pressed))) {
        ui.open = false;
        ui.challenge = None;
        ui.bump();
    }
    let pressed = close_deck.iter().any(|i| *i == Interaction::Pressed);
    if deck.open && (!state.in_world || input.escape || pressed) {
        deck.open = false;
        deck.version = deck.version.wrapping_add(1);
    }
}

fn send(out: &mut MessageWriter<SendFusionChannel>, a: &Action) {
    out.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL,
        body: encode(a),
    }));
}

/// A played card's face takes off from its slot.
fn launch(ui: &mut BattleUi, index: u8, now: f32) {
    if let Some(c) = ui
        .state
        .as_ref()
        .and_then(|s| s.hand.get(usize::from(index)))
    {
        let fly = Fly {
            icon: c.icon.clone(),
            spell: c.spell,
            name: c.name.clone(),
            slot: usize::from(index),
            at: now,
        };
        ui.flying.push(fly);
    }
}

/// 1-0 play the hand's cards, E ends the turn. While a duel is on, crate::input hands these
/// keys to this window alone (`FpsState::duel_keys`): no action bar or hotkey sees them.
fn keys(
    time: Res<Time>,
    input: Res<Mw2Input>,
    mut state: ResMut<FpsState>,
    mut ui: ResMut<BattleUi>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    let take = ui.open && ui.fighting() && ui.challenge.is_none();
    if state.duel_keys != take {
        state.duel_keys = take;
    }
    if !take {
        return;
    }
    let Some(s) = ui.state.clone() else {
        return;
    };
    for key in &input.duel {
        if let Some(a) = key_action(&s, *key) {
            if let Action::Play { index, .. } = a {
                launch(&mut ui, index, time.elapsed_secs());
            }
            ui.status = None;
            ui.bump();
            send(&mut out, &a);
            return;
        }
    }
}

fn click(
    time: Res<Time>,
    mut ui: ResMut<BattleUi>,
    mut deck: ResMut<DeckUi>,
    mut out: MessageWriter<SendFusionChannel>,
    buttons: Query<(&Interaction, &BattleButton, Option<&WowButton>), Changed<Interaction>>,
    cards: Query<(&Interaction, &HandCard), Changed<Interaction>>,
) {
    if !ui.open {
        return;
    }
    for (i, c) in &cards {
        if *i != Interaction::Pressed {
            continue;
        }
        let Some(s) = ui.state.clone() else {
            continue;
        };
        let Some(card) = s.hand.get(c.slot) else {
            continue;
        };
        if !(card.playable && s.your_turn && s.phase == PHASE_PLAYING) {
            ui.status = Some(if s.your_turn {
                "Not enough energy.".into()
            } else {
                "It isn't your turn.".into()
            });
            ui.bump();
            continue;
        }
        let a = Action::Play {
            index: c.slot as u8,
            card: card.id,
        };
        launch(&mut ui, c.slot as u8, time.elapsed_secs());
        ui.status = None;
        ui.bump();
        send(&mut out, &a);
    }
    for (i, b, w) in &buttons {
        if *i != Interaction::Pressed || w.is_some_and(|w| w.disabled) {
            continue;
        }
        let Some(action) = b.0.clone() else {
            ui.open = false;
            ui.bump();
            continue;
        };
        if matches!(action, Action::Accept | Action::Decline) {
            ui.challenge = None;
            if action == Action::Decline {
                ui.open = false;
            }
        }
        if matches!(action, Action::AskDeck(_)) {
            deck.open = true;
        }
        ui.status = None;
        ui.bump();
        send(&mut out, &action);
    }
}

fn deck_click(
    mut deck: ResMut<DeckUi>,
    mut out: MessageWriter<SendFusionChannel>,
    buttons: Query<(&Interaction, &DeckButton, &WowButton), Changed<Interaction>>,
) {
    if !deck.open {
        return;
    }
    for (i, b, w) in &buttons {
        if *i != Interaction::Pressed || w.disabled {
            continue;
        }
        let Some(d) = deck.deck.as_mut() else {
            continue;
        };
        match *b {
            DeckButton::Less(k) => {
                if let Some(l) = d.library.get_mut(k) {
                    l.in_deck = l.in_deck.saturating_sub(1);
                }
                d.note.clear();
            }
            DeckButton::More(k) => {
                let full = d.size() >= usize::from(d.max);
                if let Some(l) = d.library.get_mut(k)
                    && l.in_deck < l.copies
                    && !full
                {
                    l.in_deck += 1;
                }
                d.note.clear();
            }
            DeckButton::Save => {
                let a = Action::SaveDeck(d.item, d.cards());
                d.note = "Saving...".into();
                send(&mut out, &a);
            }
        }
        deck.version = deck.version.wrapping_add(1);
    }
}

fn show(root: &mut Query<&mut Visibility, With<BattleRoot>>, open: bool) {
    let want = if open {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in root.iter_mut() {
        if *v != want {
            *v = want;
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn draw(
    mut commands: Commands,
    time: Res<Time>,
    mut ui: ResMut<BattleUi>,
    font: Option<Res<BattleFont>>,
    assets: Res<AssetServer>,
    mut root: Query<&mut Visibility, With<BattleRoot>>,
    body: Query<(Entity, Option<&Children>), With<BattleBody>>,
    mut clock: Query<&mut Text, With<ClockText>>,
) {
    let Some(font) = font else {
        return;
    };
    show(&mut root, ui.open);
    if !ui.open {
        ui.drawn = None;
        return;
    }
    if let Some(s) = ui.state.clone() {
        let secs = (f32::from(s.seconds) - (time.elapsed_secs() - ui.received_at)).max(0.0) as u32;
        if secs != ui.shown_secs {
            ui.shown_secs = secs;
            let line = header(&s, secs);
            for mut t in &mut clock {
                if t.0 != line {
                    t.0 = line.clone();
                }
            }
        }
    }
    if ui.drawn == Some(ui.version) {
        return;
    }
    ui.drawn = Some(ui.version);
    let Ok((e, children)) = body.single() else {
        return;
    };
    for c in children.into_iter().flatten() {
        commands.entity(*c).despawn();
    }
    let f = &font.0;
    let secs = ui.shown_secs;
    let now = time.elapsed_secs();
    let dealing = now - ui.deal_at < 1.0;
    commands.entity(e).with_children(|b| {
        if let Some(name) = &ui.challenge {
            b.spawn(text(
                f,
                &format!("{name} challenges you to a companion duel."),
                14.0,
                GOLD,
            ));
            b.spawn(text(
                f,
                "Your companion out and your strongest others duel with their decks, levelled to the weaker side.",
                11.0,
                DIM,
            ));
            b.spawn(Node {
                flex_direction: FlexDirection::Row,
                column_gap: u(8.0),
                ..default()
            })
            .with_children(|r| {
                for (label, a) in [("Accept", Action::Accept), ("Decline", Action::Decline)] {
                    r.spawn(wow_ui::button(
                        f,
                        label,
                        90.0,
                        (BattleButton(Some(a)), WowButton::new(Kind::Panel)),
                    ));
                }
            });
            return;
        }
        let Some(s) = ui.state.clone() else {
            if let Some(t) = &ui.status {
                b.spawn(text(f, t, 12.0, WHITE));
            }
            return;
        };
        draw_duel(
            b,
            f,
            &assets,
            &s,
            &ui.log,
            ui.status.as_deref(),
            secs,
            dealing,
        );
    });
}

fn icon(assets: &AssetServer, name: &str) -> Handle<Image> {
    assets.load(crate::cac_ui::spell_icon_path(&crate::companion_icons::path(
        name,
    )))
}

/// Dark ink on parchment: no shadow.
fn ink(f: &Handle<Font>, s: &str, size: f32, color: Color) -> impl Bundle + use<> {
    (
        Text::new(s),
        TextFont::from_font_size(uf(size)).with_font(f.clone()),
        TextColor(color),
        TextLayout::new_with_justify(Justify::Center),
        Pickable::IGNORE,
    )
}

#[allow(clippy::too_many_arguments)]
fn draw_duel(
    b: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    s: &State,
    log: &[String],
    status: Option<&str>,
    secs: u32,
    dealing: bool,
) {
    let over = s.phase == PHASE_OVER;
    let color = match (over, s.winner) {
        (true, 0) => GREEN,
        (true, 1) => RED,
        _ => GOLD,
    };
    b.spawn((text(f, &header(s, secs), 14.0, color), ClockText));
    // Theirs on top, with its intent; yours below with the energy and the piles.
    side_panel(b, f, assets, &s.sides[1], false, s);
    wow_ui::inset(
        b,
        Node {
            flex_direction: FlexDirection::Column,
            padding: UiRect::all(u(4.0)),
            min_height: u(72.0),
            ..default()
        },
        (),
        |c| {
            let from = log.len().saturating_sub(LOG_SHOWN);
            for line in &log[from..] {
                c.spawn((
                    Node {
                        max_width: u(WIDTH - 50.0),
                        ..default()
                    },
                    children![text(f, line, 11.0, WHITE)],
                ));
            }
        },
    );
    side_panel(b, f, assets, &s.sides[0], true, s);
    // The hand.
    b.spawn(Node {
        flex_direction: FlexDirection::Row,
        flex_wrap: FlexWrap::Wrap,
        column_gap: u(6.0),
        row_gap: u(6.0),
        justify_content: JustifyContent::Center,
        min_height: u(CARD_H + 10.0),
        padding: UiRect::top(u(8.0)),
        ..default()
    })
    .with_children(|r| {
        if s.hand.is_empty() && !over {
            r.spawn(text(f, "No cards in hand.", 11.0, GREY));
        }
        for (i, c) in s.hand.iter().enumerate() {
            let ready = !over && s.your_turn && c.playable;
            card_face(r, f, assets, c, Some(i), ready, dealing);
        }
    });
    // Turn, forfeit, deck, close.
    b.spawn(Node {
        flex_direction: FlexDirection::Row,
        column_gap: u(6.0),
        justify_content: JustifyContent::Center,
        ..default()
    })
    .with_children(|r| {
        if over {
            r.spawn(wow_ui::button_fit(
                f,
                "Deck",
                WowButton::new(Kind::Panel),
                (
                    BattleButton(Some(Action::AskDeck(0))),
                    WowTip::new(
                        "Deck",
                        "Your companion's deck: what it knows and what it plays.",
                    ),
                ),
            ));
            r.spawn(wow_ui::button_fit(
                f,
                "Close",
                WowButton::new(Kind::Panel),
                BattleButton(None),
            ));
        } else {
            r.spawn(wow_ui::button_fit(
                f,
                "End Turn (E)",
                WowButton {
                    disabled: !s.your_turn,
                    ..WowButton::new(Kind::Panel)
                },
                (
                    BattleButton(Some(Action::EndTurn)),
                    WowTip::new(
                        "End Turn",
                        "Discard your hand and let the other side play. Your Block falls away at the start of your next turn.",
                    ),
                ),
            ));
            r.spawn(wow_ui::button_fit(
                f,
                "Forfeit",
                WowButton::new(Kind::Panel),
                (
                    BattleButton(Some(Action::Forfeit)),
                    WowTip::new("Forfeit", "Give up the duel. Nobody gains anything."),
                ),
            ));
        }
    });
    if !s.rewards.is_empty() {
        for line in s.rewards.lines() {
            b.spawn(text(f, line, 11.0, GREEN));
        }
    }
    if let Some(t) = status {
        b.spawn(text(f, t, 11.0, RED));
    }
}

/// One side: its fighting companion (portrait, health and block, statuses), its team, and either
/// its intent (theirs) or the energy and piles (yours).
fn side_panel(
    b: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    side: &SideView,
    mine: bool,
    s: &State,
) {
    wow_ui::inset(
        b,
        Node {
            flex_direction: FlexDirection::Row,
            justify_content: JustifyContent::SpaceBetween,
            align_items: AlignItems::Center,
            padding: UiRect::all(u(6.0)),
            ..default()
        },
        (),
        |row| {
            row.spawn(Node {
                flex_direction: FlexDirection::Row,
                column_gap: u(8.0),
                align_items: AlignItems::Center,
                ..default()
            })
            .with_children(|l| {
                let Some(p) = side.fighting() else {
                    return;
                };
                let portrait = assets.load(crate::cac_ui::spell_icon_path(
                    &crate::companion_icons::family_icon(p.family),
                ));
                l.spawn((
                    Node::default(),
                    children![wow_ui::icon_frame(Some(portrait), 44.0, GOLD)],
                    Interaction::default(),
                    WowTip::new(
                        &p.name,
                        &format!(
                            "{} {} (level {}): {}.",
                            family_name(p.battle_family),
                            archetype_name(p.archetype),
                            p.level,
                            ARCHETYPES
                                .get(usize::from(p.archetype))
                                .map_or("", |a| a.1)
                        ),
                    ),
                ));
                l.spawn(Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: u(2.0),
                    ..default()
                })
                .with_children(|c| {
                    let who = if mine {
                        format!("{} (you)", side.owner)
                    } else {
                        side.owner.clone()
                    };
                    c.spawn(text(
                        f,
                        &format!("{}  ·  {} level {}", who, p.name, p.level),
                        12.0,
                        GOLD,
                    ));
                    health_bar(c, f, p, side.block);
                    statuses(c, f, assets, side);
                    team_pips(c, assets, side);
                });
            });
            if mine {
                energy_and_piles(row, f, assets, side, s);
            } else {
                intent_box(row, f, assets, &side.intent, s.phase == PHASE_OVER);
            }
        },
    );
}

fn health_bar(c: &mut ChildSpawnerCommands, f: &Handle<Font>, p: &PetView, block: u16) {
    let share = if p.max_health > 0 {
        (f32::from(p.health) / f32::from(p.max_health)).clamp(0.0, 1.0)
    } else {
        0.0
    };
    c.spawn(Node {
        flex_direction: FlexDirection::Row,
        column_gap: u(6.0),
        align_items: AlignItems::Center,
        ..default()
    })
    .with_children(|r| {
        r.spawn((
            Node {
                width: u(260.0),
                height: u(12.0),
                border: UiRect::all(px(1)),
                ..default()
            },
            BorderColor::all(Color::srgba(0.0, 0.0, 0.0, 0.9)),
            BackgroundColor(Color::srgba(0.1, 0.1, 0.1, 0.9)),
            children![(
                Node {
                    width: Val::Percent(share * 100.0),
                    height: Val::Percent(100.0),
                    ..default()
                },
                BackgroundColor(if share > 0.5 {
                    Color::srgb(0.1, 0.75, 0.1)
                } else if share > 0.25 {
                    Color::srgb(0.9, 0.7, 0.0)
                } else {
                    Color::srgb(0.85, 0.1, 0.1)
                }),
            )],
        ));
        r.spawn(text(
            f,
            &format!("{} / {}", p.health, p.max_health),
            11.0,
            WHITE,
        ));
        if block > 0 {
            r.spawn((
                Node {
                    padding: UiRect::axes(u(4.0), u(1.0)),
                    border: UiRect::all(px(1)),
                    ..default()
                },
                BorderColor::all(Color::srgb(0.55, 0.65, 0.85)),
                BackgroundColor(Color::srgba(0.15, 0.25, 0.45, 0.9)),
                Interaction::default(),
                WowTip::new(
                    "Block",
                    "Stops that much attack damage. It falls away at the start of its owner's turn.",
                ),
                children![text(f, &format!("Block {block}"), 11.0, WHITE)],
            ));
        }
        if p.beaten {
            r.spawn(text(f, "Beaten", 11.0, RED));
        }
    });
}

fn statuses(c: &mut ChildSpawnerCommands, f: &Handle<Font>, assets: &AssetServer, side: &SideView) {
    if side.statuses.is_empty() {
        return;
    }
    c.spawn(Node {
        flex_direction: FlexDirection::Row,
        column_gap: u(3.0),
        ..default()
    })
    .with_children(|r| {
        for (k, n) in &side.statuses {
            let (name, ic, what) = STATUSES
                .get(usize::from(*k))
                .copied()
                .unwrap_or(("Effect", ICON_UNKNOWN, ""));
            let bad = matches!(*k, 0..=3 | 16);
            r.spawn((
                Node {
                    flex_direction: FlexDirection::Column,
                    align_items: AlignItems::Center,
                    ..default()
                },
                Interaction::default(),
                WowTip::new(&format!("{name} {n}"), what),
                children![
                    wow_ui::icon_frame(
                        Some(icon(assets, ic)),
                        20.0,
                        if bad { RED } else { wow_ui::SLOT_EDGE }
                    ),
                    text(f, &n.to_string(), 9.0, if bad { RED } else { WHITE }),
                ],
            ));
        }
    });
}

fn team_pips(c: &mut ChildSpawnerCommands, assets: &AssetServer, side: &SideView) {
    if side.pets.len() < 2 {
        return;
    }
    c.spawn(Node {
        flex_direction: FlexDirection::Row,
        column_gap: u(3.0),
        ..default()
    })
    .with_children(|r| {
        for (i, p) in side.pets.iter().enumerate() {
            let active = i == usize::from(side.active);
            let pip = assets.load(crate::cac_ui::spell_icon_path(
                &crate::companion_icons::family_icon(p.family),
            ));
            r.spawn((
                Node::default(),
                children![wow_ui::icon_frame_lit(
                    Some(pip),
                    16.0,
                    if active { GOLD } else { wow_ui::SLOT_EDGE },
                    !p.beaten,
                )],
                Interaction::default(),
                WowTip::new(
                    &p.name,
                    if p.beaten {
                        "Beaten: out of this duel (it is whole again afterwards)."
                    } else if active {
                        "Fighting now."
                    } else {
                        "Waiting to step in."
                    },
                ),
            ));
        }
    });
}

fn intent_box(
    row: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    i: &Intent,
    over: bool,
) {
    if over {
        return;
    }
    let what = if i.known {
        "What it will do on its turn (with the statuses now)."
    } else {
        "Another player: you can't see what they will play."
    };
    row.spawn((
        Node {
            flex_direction: FlexDirection::Column,
            align_items: AlignItems::Center,
            min_width: u(120.0),
            ..default()
        },
        Interaction::default(),
        WowTip::new("Intent", what),
    ))
    .with_children(|c| {
        c.spawn(text(f, "Intent", 10.0, DIM));
        c.spawn(wow_ui::icon_frame(
            Some(icon(assets, intent_icon(i))),
            34.0,
            if i.damage > 0 { RED } else { GOLD },
        ));
        c.spawn(text(
            f,
            &intent_text(i),
            12.0,
            if i.damage > 0 { RED } else { WHITE },
        ));
    });
}

fn energy_and_piles(
    row: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    side: &SideView,
    s: &State,
) {
    row.spawn(Node {
        flex_direction: FlexDirection::Row,
        column_gap: u(10.0),
        align_items: AlignItems::Center,
        ..default()
    })
    .with_children(|r| {
        // The energy orb: the icon in a blue ring, its count over it.
        r.spawn((
            Node {
                width: u(48.0),
                height: u(48.0),
                border: UiRect::all(px(2)),
                justify_content: JustifyContent::Center,
                align_items: AlignItems::Center,
                ..default()
            },
            BorderColor::all(GOLD),
            BackgroundColor(ORB),
            ImageNode {
                image: icon(assets, ICON_ENERGY),
                color: Color::srgba(0.6, 0.75, 1.0, 0.85),
                ..default()
            },
            Interaction::default(),
            WowTip::new(
                "Energy",
                "Playing a card costs its energy. You get it back at the start of your turn.",
            ),
            children![text(
                f,
                &format!("{}/{}", s.energy, s.max_energy),
                15.0,
                WHITE
            )],
        ));
        for (ic, n, name, what) in [
            (
                ICON_DRAW,
                side.draw,
                "Draw pile",
                "Cards still to draw. When it runs out, the discard pile is shuffled into it.",
            ),
            (
                ICON_DISCARD,
                side.discard,
                "Discard pile",
                "Cards played and hands ended.",
            ),
            (
                ICON_EXHAUST,
                side.exhaust,
                "Exhausted",
                "Cards gone for the rest of the duel.",
            ),
        ] {
            r.spawn((
                Node {
                    flex_direction: FlexDirection::Column,
                    align_items: AlignItems::Center,
                    ..default()
                },
                Interaction::default(),
                WowTip::new(name, what),
                children![
                    wow_ui::icon_frame(Some(icon(assets, ic)), 26.0, wow_ui::SLOT_EDGE),
                    text(f, &n.to_string(), 11.0, WHITE),
                ],
            ));
        }
    });
}

/// A card's face on parchment: the cost gem, its key, its spell icon, its name and type, its text
/// and its numbers now. `slot` makes it a hand card (clickable, keyed); `ready`: playable now.
fn card_face(
    r: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    c: &CardView,
    slot: Option<usize>,
    ready: bool,
    deal: bool,
) {
    let tip = {
        let mut t = WowTip::new(
            &c.name,
            &format!(
                "{} · costs {} energy\n{}",
                card_type_name(c.kind),
                c.cost,
                c.text
            ),
        );
        let now = card_now(c);
        if !now.is_empty() {
            t.0.push(wow_ui::TipLine::new(&format!("Now: {now}"), GREEN, 11.0));
        }
        if let Some(k) = slot.and_then(slot_key) {
            t.0.push(wow_ui::TipLine::new(&format!("Key: {k}"), DIM, 10.0));
        }
        t
    };
    let mut e = r.spawn((
        Node {
            width: u(CARD_W),
            height: u(CARD_H),
            flex_direction: FlexDirection::Column,
            align_items: AlignItems::Center,
            padding: UiRect::all(u(4.0)),
            row_gap: u(2.0),
            border: UiRect::all(px(2)),
            position_type: PositionType::Relative,
            ..default()
        },
        BorderColor::all(if ready { READY_EDGE } else { PAPER_EDGE }),
        BackgroundColor(PAPER),
        ImageNode {
            image: assets.load(PARCHMENT),
            color: if ready || slot.is_none() {
                Color::srgba(1.0, 0.95, 0.85, 0.85)
            } else {
                Color::srgba(0.6, 0.55, 0.5, 0.9)
            },
            ..default()
        },
        tip,
    ));
    if let Some(slot) = slot {
        e.insert((Button, Interaction::default(), HandCard { slot, deal }));
    }
    e.with_children(|card| {
        // The cost gem and the key.
        card.spawn(Node {
            width: Val::Percent(100.0),
            flex_direction: FlexDirection::Row,
            justify_content: JustifyContent::SpaceBetween,
            ..default()
        })
        .with_children(|top| {
            top.spawn((
                Node {
                    width: u(18.0),
                    height: u(18.0),
                    border: UiRect::all(px(1)),
                    justify_content: JustifyContent::Center,
                    align_items: AlignItems::Center,
                    ..default()
                },
                BorderColor::all(GOLD),
                BackgroundColor(ORB),
                children![text(f, &c.cost.to_string(), 11.0, WHITE)],
            ));
            if let Some(k) = slot.and_then(slot_key) {
                top.spawn(ink(f, k, 10.0, INK_SOFT));
            }
        });
        let face = crate::companion_icons::path(&c.icon);
        card.spawn(wow_ui::icon_frame_lit(
            Some(assets.load(crate::cac_ui::spell_icon_path(&face))),
            36.0,
            match c.kind {
                CARD_ATTACK => Color::srgb(0.6, 0.12, 0.08),
                CARD_POWER => Color::srgb(0.55, 0.35, 0.75),
                _ => Color::srgb(0.15, 0.3, 0.6),
            },
            ready || slot.is_none(),
        ));
        card.spawn((
            Node {
                max_width: u(CARD_W - 8.0),
                ..default()
            },
            children![ink(f, &c.name, 10.5, INK)],
        ));
        card.spawn(ink(f, card_type_name(c.kind), 8.5, INK_SOFT));
        card.spawn((
            Node {
                max_width: u(CARD_W - 10.0),
                flex_grow: 1.0,
                ..default()
            },
            children![ink(f, &c.text, 8.5, INK)],
        ));
        let now = card_now(c);
        if !now.is_empty() && slot.is_some() {
            card.spawn(ink(f, &now, 9.0, Color::srgb(0.45, 0.05, 0.02)));
        }
    });
}

#[allow(clippy::too_many_arguments)]
fn draw_deck(
    mut commands: Commands,
    mut ui: ResMut<DeckUi>,
    font: Option<Res<BattleFont>>,
    assets: Res<AssetServer>,
    mut root: Query<&mut Visibility, With<DeckRoot>>,
    body: Query<(Entity, Option<&Children>), With<DeckBody>>,
) {
    let Some(font) = font else {
        return;
    };
    let want = if ui.open {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut root {
        if *v != want {
            *v = want;
        }
    }
    if !ui.open {
        ui.drawn = None;
        return;
    }
    if ui.drawn == Some(ui.version) {
        return;
    }
    ui.drawn = Some(ui.version);
    let Ok((e, children)) = body.single() else {
        return;
    };
    for c in children.into_iter().flatten() {
        commands.entity(*c).despawn();
    }
    let f = &font.0;
    let Some(d) = ui.deck.clone() else {
        commands
            .entity(e)
            .with_children(|b| {
                b.spawn(text(f, "Asking for the deck...", 12.0, DIM));
            });
        return;
    };
    commands.entity(e).with_children(|b| {
        b.spawn(text(f, &format!("{}'s deck", d.name), 14.0, GOLD));
        let n = d.size();
        b.spawn(text(
            f,
            &format!(
                "{n} cards (a deck holds {} to {}). + and - change it; Save keeps it for every duel.",
                d.min, d.max
            ),
            11.0,
            if d.valid() { WHITE } else { RED },
        ));
        b.spawn(Node {
            flex_direction: FlexDirection::Row,
            flex_wrap: FlexWrap::Wrap,
            column_gap: u(8.0),
            row_gap: u(3.0),
            ..default()
        })
        .with_children(|grid| {
            for (k, l) in d.library.iter().enumerate() {
                deck_row(grid, f, &assets, k, l, n >= usize::from(d.max));
            }
        });
        b.spawn(Node {
            flex_direction: FlexDirection::Row,
            column_gap: u(6.0),
            justify_content: JustifyContent::Center,
            ..default()
        })
        .with_children(|r| {
            r.spawn(wow_ui::button_fit(
                f,
                "Save",
                WowButton {
                    disabled: !d.valid(),
                    ..WowButton::new(Kind::Panel)
                },
                (
                    DeckButton::Save,
                    WowTip::new("Save", "Keep this deck. The server checks it first."),
                ),
            ));
        });
        if !d.note.is_empty() {
            b.spawn(text(
                f,
                &d.note,
                11.0,
                if d.note == "Deck saved." { GREEN } else { RED },
            ));
        }
    });
}

/// Where a card in the library came from (`CompanionBattles::CardSource`).
pub fn source_name(s: u8) -> &'static str {
    match s {
        0 => "Basic",
        1 => "Family",
        2 => "Learned by level",
        3 => "A chosen ability",
        4 => "A trait",
        5 => "From its parents",
        6 => "A lesson or a reward",
        _ => "",
    }
}

fn deck_row(
    grid: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    k: usize,
    l: &LibraryEntry,
    full: bool,
) {
    let c = &l.card;
    grid.spawn((
        Node {
            width: u(296.0),
            flex_direction: FlexDirection::Row,
            column_gap: u(5.0),
            align_items: AlignItems::Center,
            padding: UiRect::all(u(2.0)),
            border: UiRect::all(px(1)),
            ..default()
        },
        BorderColor::all(if l.in_deck > 0 { GOLD } else { wow_ui::SLOT_EDGE }),
        BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.35)),
        Interaction::default(),
        WowTip::new(
            &c.name,
            &format!(
                "{} · costs {}\n{}\n{} · at most {} in a deck",
                card_type_name(c.kind),
                c.cost,
                c.text,
                source_name(l.source),
                l.copies
            ),
        ),
    ))
    .with_children(|row| {
        row.spawn(wow_ui::icon_frame(
            Some(assets.load(crate::cac_ui::spell_icon_path(
                &crate::companion_icons::path(&c.icon),
            ))),
            24.0,
            wow_ui::SLOT_EDGE,
        ));
        row.spawn(Node {
            flex_direction: FlexDirection::Column,
            width: u(178.0),
            ..default()
        })
        .with_children(|col| {
            col.spawn(text(
                f,
                &format!("{} ({})  {}", c.name, c.cost, card_type_name(c.kind)),
                10.5,
                if l.in_deck > 0 { GOLD } else { WHITE },
            ));
            col.spawn(text(f, &c.text, 8.5, DIM));
        });
        row.spawn(wow_ui::button_fit(
            f,
            "-",
            WowButton {
                disabled: l.in_deck == 0,
                ..WowButton::new(Kind::Panel)
            },
            DeckButton::Less(k),
        ));
        row.spawn(text(f, &format!("{}/{}", l.in_deck, l.copies), 10.0, WHITE));
        row.spawn(wow_ui::button_fit(
            f,
            "+",
            WowButton {
                disabled: l.in_deck >= l.copies || full,
                ..WowButton::new(Kind::Panel)
            },
            DeckButton::More(k),
        ));
    });
}

/// The cards' motion: a new hand slides up in turn, a card lifts under the pointer, a played card
/// flies up and fades.
#[allow(clippy::type_complexity)]
fn animate(
    mut commands: Commands,
    time: Res<Time>,
    mut ui: ResMut<BattleUi>,
    font: Option<Res<BattleFont>>,
    assets: Res<AssetServer>,
    roots: Query<Entity, With<BattleRoot>>,
    mut cards: Query<(&HandCard, &Interaction, &mut Node), Without<Flying>>,
    mut flying: Query<(Entity, &Flying, &mut Node, &mut BackgroundColor), Without<HandCard>>,
) {
    let now = time.elapsed_secs();
    for (c, i, mut node) in &mut cards {
        let t = now - ui.deal_at - c.slot as f32 * 0.07;
        let rise = if c.deal && t < 0.3 {
            let k = (t / 0.3).clamp(0.0, 1.0);
            (1.0 - k * (2.0 - k)) * 70.0
        } else {
            0.0
        };
        let lift = if *i == Interaction::Hovered { -10.0 } else { 0.0 };
        let want = Val::Px(uf(rise + lift));
        if node.top != want {
            node.top = want;
        }
    }
    // New flights: an overlay card over its slot.
    if let (Some(font), Ok(root)) = (font, roots.single()) {
        for fly in ui.flying.drain(..) {
            let x = 20.0 + fly.slot as f32 * (CARD_W + 6.0);
            let face = crate::companion_icons::path(&fly.icon);
            let _ = fly.spell;
            commands.entity(root).with_children(|r| {
                r.spawn((
                    Flying(fly.at),
                    Node {
                        position_type: PositionType::Absolute,
                        left: u(x),
                        bottom: u(60.0),
                        width: u(CARD_W),
                        height: u(CARD_H),
                        flex_direction: FlexDirection::Column,
                        align_items: AlignItems::Center,
                        justify_content: JustifyContent::Center,
                        border: UiRect::all(px(2)),
                        ..default()
                    },
                    BorderColor::all(READY_EDGE),
                    BackgroundColor(PAPER),
                    Pickable::IGNORE,
                    GlobalZIndex(i32::MAX - 2),
                    children![
                        wow_ui::icon_frame(
                            Some(assets.load(crate::cac_ui::spell_icon_path(&face))),
                            36.0,
                            GOLD
                        ),
                        ink(&font.0, &fly.name, 10.5, INK),
                    ],
                ));
            });
        }
    }
    for (e, fl, mut node, mut bg) in &mut flying {
        let t = now - fl.0;
        if t > 0.45 {
            commands.entity(e).despawn();
            continue;
        }
        let k = t / 0.45;
        node.bottom = u(60.0 + 160.0 * k);
        bg.0 = PAPER.with_alpha(1.0 - k);
    }
}

fn hold_cursor(mut ui: ResMut<BattleUi>, deck: Res<DeckUi>, mut state: ResMut<FpsState>) {
    if ui.open || deck.open {
        if !state.panel {
            state.panel = true;
        }
        ui.held_cursor = true;
    } else if ui.held_cursor {
        ui.held_cursor = false;
        state.panel = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn str8(b: &mut Vec<u8>, s: &str) {
        b.push(s.len() as u8);
        b.extend(s.as_bytes());
    }
    fn str16(b: &mut Vec<u8>, s: &str) {
        b.extend((s.len() as u16).to_le_bytes());
        b.extend(s.as_bytes());
    }

    /// A card as the server's `WriteCard` writes it.
    fn card(b: &mut Vec<u8>, id: u16, kind: u8, cost: u8, playable: bool, name: &str, text: &str) {
        b.extend(id.to_le_bytes());
        b.extend([kind, cost, u8::from(playable)]);
        b.extend(6u16.to_le_bytes());
        b.extend(0u16.to_le_bytes());
        b.extend(0u32.to_le_bytes());
        str8(b, "Ability_Druid_Rake");
        str8(b, name);
        str16(b, text);
    }

    /// A state as the server's `CompanionBattles::EncodeState` writes it (CompanionBattleTests'
    /// layout): your turn, three cards in hand, the NPC attacking 12 twice.
    pub(super) fn state_body() -> Vec<u8> {
        let mut b = vec![VERSION, OP_STATE];
        b.extend(77u32.to_le_bytes());
        b.push(PHASE_PLAYING);
        b.extend(3u16.to_le_bytes());
        b.push(0); // end
        b.push(0xFF); // winner
        b.extend([1, 40, 2, 3]); // your turn, seconds, energy, max
        b.push(3);
        card(&mut b, 100, CARD_ATTACK, 1, true, "Rake", "Deal 3 damage. Apply 3 Bleed.");
        card(&mut b, 10, CARD_SKILL, 1, true, "Guard", "Gain 5 Block.");
        card(&mut b, 104, CARD_ATTACK, 3, false, "Rip", "Deal 8 damage.");
        for (owner, name, known) in [("Bob", "Tabby Cat", 0u8), ("Julia Stevens", "Wolf Pup", 1)] {
            str8(&mut b, owner);
            b.extend([0, 5, 7, 2, 1]); // active, hand, draw, discard, exhaust
            b.extend(4u16.to_le_bytes()); // block
            b.push(1);
            b.push(3); // bleed
            b.extend(2u16.to_le_bytes());
            b.push(known);
            b.extend(12u16.to_le_bytes());
            b.push(2);
            b.extend(0u16.to_le_bytes());
            b.push(2); // a debuff
            b.push(2); // two companions
            for (i, n) in [(0, name), (1, "Second")] {
                b.extend(6u16.to_le_bytes());
                b.extend([4, 7, 0, 1, 12]); // family, battle family, archetype, stage, level
                b.extend(50u16.to_le_bytes());
                b.extend(70u16.to_le_bytes());
                b.push(u8::from(i == 1 && known == 1));
                str8(&mut b, n);
            }
        }
        b.push(2);
        for l in ["Tabby Cat plays Rake.", "Wolf Pup takes 3."] {
            str16(&mut b, l);
        }
        str16(&mut b, "");
        b
    }

    fn state() -> State {
        let Some(Message::State(s)) = decode(&state_body()) else {
            panic!("a state");
        };
        *s
    }

    #[test]
    fn the_state_reads_as_the_server_writes_it() {
        let s = state();
        assert_eq!((s.battle, s.turn, s.winner), (77, 3, -1));
        assert!(s.your_turn);
        assert_eq!((s.energy, s.max_energy, s.seconds), (2, 3, 40));
        assert_eq!(s.hand.len(), 3);
        assert_eq!(s.hand[0].name, "Rake");
        assert!(s.hand[0].playable && !s.hand[2].playable);
        assert_eq!(s.sides[0].owner, "Bob");
        assert_eq!(s.sides[1].owner, "Julia Stevens");
        assert_eq!(
            (s.sides[0].draw, s.sides[0].discard, s.sides[0].exhaust),
            (7, 2, 1)
        );
        assert_eq!(s.sides[0].statuses, vec![(3, 2)]);
        assert!(!s.sides[0].intent.known && s.sides[1].intent.known);
        assert_eq!(intent_text(&s.sides[1].intent), "Attack 12 x2, Debuff");
        assert_eq!(intent_icon(&s.sides[1].intent), ICON_ATTACK);
        assert_eq!(intent_text(&s.sides[0].intent), "Unknown");
        let p = s.sides[0].fighting().unwrap();
        assert_eq!((p.health, p.max_health, p.level), (50, 70, 12));
        assert_eq!(archetype_name(p.archetype), "Feline");
        assert_eq!(family_name(p.battle_family), "Beast");
        assert!(s.sides[1].pets[1].beaten);
        assert_eq!(s.lines.len(), 2);
        assert_eq!(prompt(&s), "Turn 3: your turn.");
        assert_eq!(status_name(3), "Bleed");
        assert_eq!(card_now(&s.hand[0]), "Deals 6");
        // Cut short, or the old battle's version: refused.
        assert!(decode(&state_body()[..40]).is_none());
        let mut old = state_body();
        old[0] = 1;
        assert!(decode(&old).is_none());
    }

    #[test]
    fn requests_encode_as_the_server_reads_them() {
        assert_eq!(
            encode(&Action::Play {
                index: 2,
                card: 0x4123
            }),
            vec![2, 12, 1, 2, 0x23, 0x41]
        );
        assert_eq!(encode(&Action::EndTurn), vec![2, 12, 3, 0, 0, 0]);
        assert_eq!(encode(&Action::Forfeit), vec![2, 12, 4, 0, 0, 0]);
        assert_eq!(encode(&Action::Accept), vec![2, 11, 1]);
        assert_eq!(encode(&Action::Decline), vec![2, 11, 0]);
        let c = encode(&Action::Challenge(0x0102));
        assert_eq!(&c[..4], &[2, 10, 2, 1]);
        assert_eq!(c.len(), 10);
        let a = encode(&Action::AskDeck(7));
        assert_eq!((a.len(), a[1], a[2]), (10, 14, 7));
        let s = encode(&Action::SaveDeck(7, vec![1, 10, 0x4009]));
        assert_eq!(s.len(), 11 + 6);
        assert_eq!((s[1], s[2], s[10]), (15, 7, 3));
        assert_eq!(&s[11..], &[1, 0, 10, 0, 0x09, 0x40]);
        let mut t = vec![VERSION, OP_TELL];
        str16(&mut t, "Hi");
        assert_eq!(decode(&t), Some(Message::Tell("Hi".into())));
    }

    fn deck_body() -> Vec<u8> {
        let mut b = vec![VERSION, OP_DECK];
        b.extend(9u64.to_le_bytes());
        str8(&mut b, "Tabby Cat");
        b.extend([12, 30, 2]);
        card(&mut b, 2, CARD_ATTACK, 1, true, "Claw", "Deal 6 damage.");
        b.extend([5, 0, 5]);
        card(&mut b, 10, CARD_SKILL, 1, true, "Guard", "Gain 5 Block.");
        b.extend([5, 0, 4]);
        str16(&mut b, "");
        b
    }

    #[test]
    fn a_deck_reads_and_keeps_its_rules() {
        let Some(Message::Deck(d)) = decode(&deck_body()) else {
            panic!("a deck");
        };
        assert_eq!((d.item, d.min, d.max), (9, 12, 30));
        assert_eq!(d.size(), 9);
        assert!(!d.valid(), "nine is too few");
        assert_eq!(d.cards(), vec![2, 2, 2, 2, 2, 10, 10, 10, 10]);
        let mut big = (*d).clone();
        big.library[1].in_deck = 5;
        big.library.push(LibraryEntry {
            card: CardView {
                id: 100,
                ..default()
            },
            copies: 2,
            source: 1,
            in_deck: 2,
        });
        assert!(big.valid());
        big.library[2].in_deck = 3;
        assert!(!big.valid(), "more copies than allowed");
        assert_eq!(source_name(3), "A chosen ability");
    }

    #[test]
    fn keys_play_cards_and_end_the_turn() {
        let s = state();
        assert_eq!(
            key_action(&s, KeyCode::Digit1),
            Some(Action::Play {
                index: 0,
                card: 100
            })
        );
        assert_eq!(
            key_action(&s, KeyCode::Digit2),
            Some(Action::Play { index: 1, card: 10 })
        );
        assert_eq!(key_action(&s, KeyCode::Digit3), None, "not enough energy");
        assert_eq!(key_action(&s, KeyCode::Digit0), None, "no tenth card");
        assert_eq!(key_action(&s, KeyCode::KeyE), Some(Action::EndTurn));
        assert_eq!(key_action(&s, KeyCode::KeyQ), None);
        let mut theirs = s.clone();
        theirs.your_turn = false;
        assert_eq!(key_action(&theirs, KeyCode::Digit1), None);
        assert_eq!(key_action(&theirs, KeyCode::KeyE), None);
        assert_eq!(slot_key(9), Some("0"));
    }

    fn built() -> World {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        world.init_resource::<Time>();
        world.insert_resource(FpsState {
            in_world: true,
            ..default()
        });
        world.init_resource::<BattleUi>();
        world.init_resource::<DeckUi>();
        world.run_system_once(spawn_windows).unwrap();
        world
    }

    #[test]
    fn the_window_builds_the_duel() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = built();
        {
            let mut ui = world.resource_mut::<BattleUi>();
            apply_state(&mut ui, state(), 0.0);
            ui.version = 1;
        }
        world.run_system_once(draw).unwrap();
        // A card for each in hand, in slot order.
        let mut q = world.query::<&HandCard>();
        let mut slots: Vec<usize> = q.iter(&world).map(|c| c.slot).collect();
        slots.sort();
        assert_eq!(slots, vec![0, 1, 2]);
        // End Turn and Forfeit, live.
        let mut q = world.query::<(&BattleButton, &WowButton)>();
        let buttons: Vec<_> = q.iter(&world).map(|(b, w)| (b.0.clone(), w.disabled)).collect();
        assert!(buttons.contains(&(Some(Action::EndTurn), false)));
        assert!(buttons.contains(&(Some(Action::Forfeit), false)));
        // The intent, the energy, the piles and the statuses say what they are.
        let mut q = world.query::<&Text>();
        let texts: Vec<String> = q.iter(&world).map(|t| t.0.clone()).collect();
        for want in ["Attack 12 x2, Debuff", "2/3", "Rake", "Deals 6", "Block 4", "7"] {
            assert!(texts.iter().any(|t| t == want), "{want} in {texts:?}");
        }
        let mut q = world.query::<&WowTip>();
        let tips: Vec<String> = q.iter(&world).map(|t| t.0[0].text.clone()).collect();
        for want in ["Intent", "Energy", "Draw pile", "Bleed 2", "Rake", "End Turn"] {
            assert!(tips.iter().any(|t| t == want), "{want} in {tips:?}");
        }
        let vis = world
            .query_filtered::<&Visibility, With<BattleRoot>>()
            .single(&world)
            .copied()
            .unwrap();
        assert_eq!(vis, Visibility::Inherited);
        // Their turn: End Turn is off, the cards are not ready.
        {
            let mut ui = world.resource_mut::<BattleUi>();
            let mut s = state();
            s.your_turn = false;
            apply_state(&mut ui, s, 1.0);
            ui.version = 2;
        }
        world.run_system_once(draw).unwrap();
        let mut q = world.query::<(&BattleButton, &WowButton)>();
        assert!(
            q.iter(&world)
                .any(|(b, w)| b.0 == Some(Action::EndTurn) && w.disabled)
        );
    }

    #[test]
    fn the_deck_window_lists_what_it_knows() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = built();
        {
            let Some(Message::Deck(d)) = decode(&deck_body()) else {
                panic!("a deck");
            };
            let mut ui = world.resource_mut::<DeckUi>();
            ui.deck = Some(*d);
            ui.open = true;
            ui.version = 1;
        }
        world.run_system_once(draw_deck).unwrap();
        let mut q = world.query::<(&DeckButton, &WowButton)>();
        let b: Vec<_> = q
            .iter(&world)
            .map(|(b, w)| (format!("{b:?}"), w.disabled))
            .collect();
        assert!(b.contains(&("More(0)".into(), true)), "full copies: {b:?}");
        assert!(b.contains(&("More(1)".into(), false)));
        assert!(b.contains(&("Less(0)".into(), false)));
        assert!(b.contains(&("Save".into(), true)), "nine cards can't be saved");
    }

    /// Laid out as in the game on a 1280x720 screen: the duel with a full hand fits.
    #[test]
    fn the_duel_fits_the_screen() {
        let mut app = crate::wow_ui::layout_app();
        app.insert_resource(FpsState {
            in_world: true,
            ..default()
        });
        app.init_resource::<BattleUi>();
        app.init_resource::<DeckUi>();
        app.add_systems(Update, (spawn_windows, draw).chain());
        app.update();
        crate::wow_ui::measure_text(&mut app);
        {
            let mut s = state();
            let c = s.hand[0].clone();
            s.hand = (0..10).map(|_| c.clone()).collect();
            let mut ui = app.world_mut().resource_mut::<BattleUi>();
            apply_state(&mut ui, s, 0.0);
            ui.version = 5;
        }
        for _ in 0..4 {
            app.update();
        }
        let world = app.world_mut();
        let mut roots = world.query_filtered::<Entity, With<BattleRoot>>();
        let root = roots.single(world).unwrap();
        let r = crate::wow_ui::screen_rect(world, root);
        assert!(r.width() > 600.0 && r.height() > 300.0, "{r:?}");
        assert!(r.min.y >= 0.0 && r.max.y <= 720.0, "off the screen: {r:?}");
        assert!(r.min.x >= 0.0 && r.max.x <= 1280.0, "off the screen: {r:?}");
    }

    /// Every icon the window and the server's cards name is in the install.
    #[test]
    fn every_card_and_status_icon_is_in_the_install() {
        let src = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../server/src/fusion/src/CompanionBattles.cpp"
        ))
        .expect("the server's rules");
        let mut names: Vec<String> = Vec::new();
        for line in src.lines() {
            let l = line.trim();
            if let Some(rest) = l.strip_prefix("B(") {
                // B(id, "Name", T, cost, "Icon"...
                let parts: Vec<&str> = rest.split('"').collect();
                if parts.len() > 3 {
                    names.push(parts[3].to_string());
                }
            } else if let Some(rest) = l.strip_prefix("c.icon = \"") {
                names.push(rest.trim_end_matches("\";").to_string());
            }
        }
        assert!(names.len() > 120, "the cards' icons: {}", names.len());
        names.extend(STATUSES.iter().map(|s| s.1.to_string()));
        names.extend(
            [
                ICON_ENERGY,
                ICON_DRAW,
                ICON_DISCARD,
                ICON_EXHAUST,
                ICON_ATTACK,
                ICON_BLOCK,
                ICON_BUFF,
                ICON_DEBUFF,
                ICON_UNKNOWN,
            ]
            .map(String::from),
        );
        let data = std::env::var("WOW_DATA").ok().map(std::path::PathBuf::from).or_else(|| {
            let p = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../dist/AzerothWarfare/game/WoW/Data");
            p.exists().then_some(p)
        }).or_else(|| {
            let p = std::path::PathBuf::from("C:\\Projects\\azeroth-warfare\\dist\\AzerothWarfare\\game\\WoW\\Data");
            p.exists().then_some(p)
        });
        let Some(data) = data else {
            eprintln!("no WoW install: skipped");
            return;
        };
        let chain = benilla_formats::open_chain(&data).expect("the install's chain");
        let mut missing: Vec<String> = names
            .iter()
            .filter(|n| !chain.contains(&format!("Interface\\Icons\\{n}.blp")))
            .cloned()
            .collect();
        if !chain.contains("Interface\\Stationery\\StationeryTest1.blp") {
            missing.push("the parchment".into());
        }
        missing.sort();
        missing.dedup();
        assert!(missing.is_empty(), "not in the install: {missing:?}");
    }
}
