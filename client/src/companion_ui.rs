//! The Companions window (the server's `fusion/CompanionIndex.h`, `FusionCompanionIndex.cpp`), in
//! the stock dialog look, as a monster tamer's: your companions and the Bestiary.
//!
//! - **Companions**: each one in the bags as a card (its family's icon in its rarity's edge, its
//!   name, level, health and experience bars, what it waits for: ready to evolve, a new move to
//!   learn, fainted). Its page shows it in 3D (the stable window's model pane, turned by a drag
//!   or the arrows), its stats, its moves with their spells' icons, its traits, its evolution
//!   path and what the next form needs, and what can be done: summon or put away, ride, evolve,
//!   learn an owed move, a Reviving Draught, a companion duel; at a stable master ([`Access::Npc`])
//!   also reviving, breeding and changing a stage's move. A game master changes level, stage,
//!   moves and traits here.
//! - **Bestiary**: every species by family, seen (targeted one) or caught (carried one); the rest
//!   are their family's `???` rows. An entry shows the creature in 3D (each form on its
//!   evolution path), where it is found, its moves per stage, its family trait, its mounted move
//!   and the traits it may roll. A game master sees all of it and puts any species in the bags.
//!
//! Each species' models (every form its own display, the server's look table), move spells and
//! where it is found come as op 34 ([`wire::Looks`]); the model is benilla's stable pane borrowed
//! while the stock stable is closed ([`benilla_app::fusion::LayerCreaturePreview`]), rendered live
//! (its idle loops) and framed to the whole model. A level or an evolution of the companion out
//! plays the stock level-up chime, and an evolution its banner.
//!
//! It opens from [`OpenPanel`] (`Panel::Companions`): the HUD's micro button (the limited view)
//! or the server when a stable master is spoken to (op 33). The server's companion lines come as
//! op 32 and go to the UI error frame. The item-name hook ([`item_name`]) names a companion's item
//! after its current form wherever benilla shows the item. The window drags by its title and
//! background and is kept where it was left across sessions, as every window is (crate::wow_ui).

use std::collections::{BTreeMap, HashMap};
use std::sync::{LazyLock, Mutex};

use benilla_app::fusion::{
    ChannelMessage, FusionChannelReceived, LayerCreaturePreview, LayerCreaturePreviewImage,
    SendFusionChannel,
};
use benilla_ui::script::UiScript;
use bevy::ecs::hierarchy::ChildSpawnerCommands;
use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use bevy::ui::FocusPolicy;

use crate::Mw2Set;
use crate::aim::Aim;
use crate::companion::{self, CHANNEL_COMPANION, DuelPhase, Request as BarRequest};
use crate::companion_icons as icons;
use crate::mode::FpsState;
use crate::panels::{Access, OpenPanel, Panel};
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, Kind, RED, WHITE, WindowSpec, WowButton,
    text, u,
};

pub use wire::*;

/// The window's messages (`Fusion::Companions::Index`), byte for byte the C++ encoders'.
pub mod wire {
    // Server to client.
    pub const OP_INDEX: u8 = 30;
    pub const OP_CARRIED: u8 = 31;
    pub const OP_NOTICE: u8 = 32;
    pub const OP_OPEN: u8 = 33;
    /// Each species' looks: its stages' models, its moves' spells, where it is found.
    pub const OP_LOOKS: u8 = 34;
    // Client to server.
    pub const OP_INDEX_REQUEST: u8 = 20;
    pub const OP_CARRIED_REQUEST: u8 = 21;
    pub const OP_GM_GIVE: u8 = 22;
    pub const OP_GM_EDIT: u8 = 23;
    pub const OP_BREED: u8 = 24;
    pub const OP_REPICK: u8 = 25;
    pub const OP_STABLE_REVIVE: u8 = 26;
    pub const OP_USE_DRAUGHT: u8 = 27;
    pub const OP_BUY_DRAUGHT: u8 = 28;

    pub const INDEX_GM: u8 = 0x01;
    pub const INDEX_FIRST: u8 = 0x02;
    pub const INDEX_LAST: u8 = 0x04;

    pub const CARRIED_OUT: u8 = 0x01;
    pub const CARRIED_RIDING: u8 = 0x02;
    pub const CARRIED_CAN_EVOLVE: u8 = 0x04;
    pub const CARRIED_FINAL: u8 = 0x08;
    pub const CARRIED_RIDEABLE: u8 = 0x10;
    /// Held at its owner's level with a full bar: its experience waits for the owner's next level.
    pub const CARRIED_CAPPED: u8 = 0x20;
    /// Fainted: it stays in its item until a stable master or a Reviving Draught revives it, or
    /// its wait is over.
    pub const CARRIED_FAINTED: u8 = 0x40;
    pub const CARRIED_GM: u8 = 0x01;
    pub const CARRIED_AT_STABLE: u8 = 0x02;

    /// How far a character has come with a species (`Index::Seen`).
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
    pub enum Seen {
        #[default]
        Unknown,
        Seen,
        Captured,
    }

    #[derive(Clone, Debug, Default, PartialEq, Eq)]
    pub struct StageEntry {
        pub creature: u32,
        pub name: String,
        pub reach: String,
        pub rideable: bool,
        pub abilities: [String; 2],
        pub kinds: [u8; 2],
        pub cooldowns: [u8; 2],
    }

    /// An index entry; an unknown one is its id and family alone.
    #[derive(Clone, Debug, Default, PartialEq, Eq)]
    pub struct Entry {
        pub id: u16,
        pub family: u8,
        pub seen: Seen,
        pub role: u8,
        pub item: u32,
        pub name: String,
        pub trait_name: String,
        pub mounted: String,
        /// The traits it may have, a bit per trait id (bit id - 1).
        pub eligible: u32,
        pub stages: Vec<StageEntry>,
    }

    /// A companion in the bags.
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct Carried {
        pub item: u64,
        pub species: u16,
        pub stage: u8,
        pub level: u8,
        pub xp: u32,
        pub xp_to_next: u32,
        pub traits: u32,
        pub rarity: u8,
        pub twin_stage: u8,
        pub picks: [u8; 3],
        /// The stage whose ability choice is owed, -1 none.
        pub pending: i8,
        pub flags: u8,
        /// `EvolveBlock`.
        pub block: u8,
        pub next_evolve_level: u8,
        pub breed_count: u8,
        pub breed_rest_secs: u32,
        pub generation: u8,
        /// Fainted: seconds until it comes round on its own.
        pub faint_secs: u32,
        /// The health it is missing, in percent (0: whole; 100: fainted).
        pub hurt_pct: u8,
        /// Its item's seed (`Companions::ItemSeed`), which the item tooltip is keyed by.
        pub seed: u32,
    }

    /// What the window shows of reviving, after the companions (`Index::CarriedCare`).
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct Care {
        /// Reviving Draughts in the bags.
        pub draughts: u8,
        /// A stable master's fee for every fainted one, in copper.
        pub revive_cost: u32,
        /// Companions fainted or hurt.
        pub needs_care: u8,
    }

    /// The UI error frame's kinds (`Index::NoticeKind`).
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum NoticeKind {
        Error,
        Info,
        Good,
    }

    struct Reader<'a> {
        b: &'a [u8],
        at: usize,
    }

    impl Reader<'_> {
        fn take(&mut self, n: usize) -> Option<&[u8]> {
            let s = self.b.get(self.at..self.at + n)?;
            self.at += n;
            Some(s)
        }
        fn u8(&mut self) -> Option<u8> {
            Some(self.take(1)?[0])
        }
        fn u16(&mut self) -> Option<u16> {
            Some(u16::from_le_bytes(self.take(2)?.try_into().ok()?))
        }
        fn u32(&mut self) -> Option<u32> {
            Some(u32::from_le_bytes(self.take(4)?.try_into().ok()?))
        }
        fn u64(&mut self) -> Option<u64> {
            Some(u64::from_le_bytes(self.take(8)?.try_into().ok()?))
        }
        fn str8(&mut self) -> Option<String> {
            let n = usize::from(self.u8()?);
            Some(String::from_utf8_lossy(self.take(n)?).into_owned())
        }
        fn done(&self) -> bool {
            self.at == self.b.len()
        }
    }

    /// Op 30: `(flags, entries)`.
    pub fn decode_index(body: &[u8]) -> Option<(u8, Vec<Entry>)> {
        let mut r = Reader { b: body, at: 0 };
        if r.u8()? != OP_INDEX {
            return None;
        }
        let flags = r.u8()?;
        let n = r.u8()?;
        let mut out = Vec::with_capacity(usize::from(n));
        for _ in 0..n {
            let mut e = Entry {
                id: r.u16()?,
                family: r.u8()?,
                ..Entry::default()
            };
            e.seen = match r.u8()? {
                0 => Seen::Unknown,
                1 => Seen::Seen,
                2 => Seen::Captured,
                _ => return None,
            };
            if e.seen != Seen::Unknown {
                e.role = r.u8()?;
                e.item = r.u32()?;
                e.name = r.str8()?;
                e.trait_name = r.str8()?;
                e.mounted = r.str8()?;
                e.eligible = r.u32()?;
                let stages = r.u8()?;
                if stages > 3 {
                    return None;
                }
                for _ in 0..stages {
                    let mut s = StageEntry {
                        creature: r.u32()?,
                        name: r.str8()?,
                        reach: r.str8()?,
                        rideable: r.u8()? != 0,
                        ..StageEntry::default()
                    };
                    for a in 0..2 {
                        s.abilities[a] = r.str8()?;
                        s.kinds[a] = r.u8()?;
                        s.cooldowns[a] = r.u8()?;
                    }
                    e.stages.push(s);
                }
            }
            out.push(e);
        }
        r.done().then_some((flags, out))
    }

    /// Op 31: `(header, companions, care)`.
    pub fn decode_carried(body: &[u8]) -> Option<(u8, Vec<Carried>, Care)> {
        let mut r = Reader { b: body, at: 0 };
        if r.u8()? != OP_CARRIED {
            return None;
        }
        let header = r.u8()?;
        let n = r.u8()?;
        let mut out = Vec::with_capacity(usize::from(n));
        for _ in 0..n {
            out.push(Carried {
                item: r.u64()?,
                species: r.u16()?,
                stage: r.u8()?,
                level: r.u8()?,
                xp: r.u32()?,
                xp_to_next: r.u32()?,
                traits: r.u32()?,
                rarity: r.u8()?,
                twin_stage: r.u8()?,
                picks: [r.u8()?, r.u8()?, r.u8()?],
                pending: r.u8()? as i8,
                flags: r.u8()?,
                block: r.u8()?,
                next_evolve_level: r.u8()?,
                breed_count: r.u8()?,
                breed_rest_secs: r.u32()?,
                generation: r.u8()?,
                faint_secs: r.u32()?,
                hurt_pct: 100u8.saturating_sub(r.u8()?.min(100)),
                seed: r.u32()?,
            });
        }
        let care = Care {
            draughts: r.u8()?,
            revive_cost: r.u32()?,
            needs_care: r.u8()?,
        };
        r.done().then_some((header, out, care))
    }

    /// Op 32: a line for the UI error frame.
    pub fn decode_notice(body: &[u8]) -> Option<(NoticeKind, String)> {
        let mut r = Reader { b: body, at: 0 };
        if r.u8()? != OP_NOTICE {
            return None;
        }
        let kind = match r.u8()? {
            0 => NoticeKind::Error,
            1 => NoticeKind::Info,
            2 => NoticeKind::Good,
            _ => return None,
        };
        let n = usize::from(r.u16()?);
        let t = String::from_utf8_lossy(r.take(n)?).into_owned();
        r.done().then_some((kind, t))
    }

    /// Op 33: the window opens at a stable master (true) or not.
    pub fn decode_open(body: &[u8]) -> Option<bool> {
        (body.len() == 2 && body[0] == OP_OPEN).then(|| body[1] == 1)
    }

    /// What a game master's edit changes (`Index::Edit`).
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum Edit {
        Level(u8),
        Stage(u8),
        Pick { stage: u8, option: u8 },
        Trait(u8),
        Twin(u8),
        ClearTraits,
    }

    /// A request of the window's.
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum Request {
        Index,
        Carried,
        GmGive {
            species: u16,
            stage: u8,
            level: u8,
        },
        GmEdit {
            item: u64,
            edit: Edit,
        },
        Breed {
            a: u64,
            b: u64,
        },
        Repick {
            item: u64,
            stage: u8,
            option: u8,
        },
        /// At a stable master: revive every fainted companion and heal the hurt.
        StableRevive,
        /// A Reviving Draught for this companion (0: the one that fainted last).
        UseDraught(u64),
        /// Buy a Reviving Draught at a stable master.
        BuyDraught,
    }

    pub fn encode_request(r: Request) -> Vec<u8> {
        match r {
            Request::Index => vec![OP_INDEX_REQUEST],
            Request::Carried => vec![OP_CARRIED_REQUEST],
            Request::GmGive {
                species,
                stage,
                level,
            } => {
                let mut o = vec![OP_GM_GIVE];
                o.extend_from_slice(&species.to_le_bytes());
                o.extend_from_slice(&[stage, level]);
                o
            }
            Request::GmEdit { item, edit } => {
                let (what, a, b) = match edit {
                    Edit::Level(l) => (1, l, 0),
                    Edit::Stage(s) => (2, s, 0),
                    Edit::Pick { stage, option } => (3, stage, option),
                    Edit::Trait(t) => (4, t, 0),
                    Edit::Twin(s) => (5, s, 0),
                    Edit::ClearTraits => (6, 0, 0),
                };
                let mut o = vec![OP_GM_EDIT];
                o.extend_from_slice(&item.to_le_bytes());
                o.extend_from_slice(&[what, a, b]);
                o
            }
            Request::Breed { a, b } => {
                let mut o = vec![OP_BREED];
                o.extend_from_slice(&a.to_le_bytes());
                o.extend_from_slice(&b.to_le_bytes());
                o
            }
            Request::Repick {
                item,
                stage,
                option,
            } => {
                let mut o = vec![OP_REPICK];
                o.extend_from_slice(&item.to_le_bytes());
                o.extend_from_slice(&[stage, option]);
                o
            }
            Request::StableRevive => vec![OP_STABLE_REVIVE],
            Request::UseDraught(item) => {
                let mut o = vec![OP_USE_DRAUGHT];
                o.extend_from_slice(&item.to_le_bytes());
                o
            }
            Request::BuyDraught => vec![OP_BUY_DRAUGHT],
        }
    }

    /// One stage's looks (`Index::StageLook`).
    #[derive(Clone, Debug, Default, PartialEq, Eq)]
    pub struct StageLook {
        /// Its creature's display: the model the window shows.
        pub display: u32,
        /// Its two ability options' vanilla spells (their icons).
        pub spells: [u32; 2],
    }

    /// Op 34's entry (`Index::Looks`).
    #[derive(Clone, Debug, Default, PartialEq, Eq)]
    pub struct Looks {
        pub id: u16,
        pub stages: Vec<StageLook>,
        pub basic_spell: u32,
        pub mounted_spell: u32,
        /// The display it is ridden with, 0: never ridden.
        pub ride_display: u32,
        /// Where it is found.
        pub found: String,
    }

    /// Op 34: `[34, count u8, each: id u16, stage count u8, each stage: display u32, two spells
    /// u32; basic spell u32, mounted spell u32, ride display u32, found (u8 length + bytes)]`.
    pub fn decode_looks(body: &[u8]) -> Option<Vec<Looks>> {
        let mut r = Reader { b: body, at: 0 };
        if r.u8()? != OP_LOOKS {
            return None;
        }
        let n = r.u8()?;
        let mut out = Vec::with_capacity(usize::from(n));
        for _ in 0..n {
            let id = r.u16()?;
            let stages = r.u8()?;
            if stages > 3 {
                return None;
            }
            let mut l = Looks {
                id,
                ..Looks::default()
            };
            for _ in 0..stages {
                l.stages.push(StageLook {
                    display: r.u32()?,
                    spells: [r.u32()?, r.u32()?],
                });
            }
            l.basic_spell = r.u32()?;
            l.mounted_spell = r.u32()?;
            l.ride_display = r.u32()?;
            l.found = r.str8()?;
            out.push(l);
        }
        r.done().then_some(out)
    }

    /// Op 34 as the server's `EncodeLooks` writes it.
    pub fn encode_looks(looks: &[Looks]) -> Vec<u8> {
        let mut o = vec![OP_LOOKS, looks.len().min(255) as u8];
        for l in looks.iter().take(255) {
            o.extend_from_slice(&l.id.to_le_bytes());
            o.push(l.stages.len().min(3) as u8);
            for s in l.stages.iter().take(3) {
                o.extend_from_slice(&s.display.to_le_bytes());
                o.extend_from_slice(&s.spells[0].to_le_bytes());
                o.extend_from_slice(&s.spells[1].to_le_bytes());
            }
            o.extend_from_slice(&l.basic_spell.to_le_bytes());
            o.extend_from_slice(&l.mounted_spell.to_le_bytes());
            o.extend_from_slice(&l.ride_display.to_le_bytes());
            let f = &l.found.as_bytes()[..l.found.len().min(255)];
            o.push(f.len() as u8);
            o.extend_from_slice(f);
        }
        o
    }
}

// ── Names ────────────────────────────────────────────────────────────────────────────────────────

/// `Companions::Family`'s names by id (the keeper's `FamilyName`).
const FAMILIES: [&str; 63] = [
    "Critter",
    "Dragon",
    "Faerie Dragon",
    "Wolf",
    "Cat",
    "Bear",
    "Spider",
    "Raptor",
    "Crocolisk",
    "Gorilla",
    "Owl",
    "Bat",
    "Tallstrider",
    "Scorpid",
    "Fire Elemental",
    "Water Elemental",
    "Earth Elemental",
    "Air Elemental",
    "Murloc",
    "Infernal",
    "Ooze",
    "Mechanical",
    "Kodo",
    "Horse",
    "Mechanostrider",
    "Skeletal Horse",
    "Ram",
    "Hyena",
    "Boar",
    "Crab",
    "Turtle",
    "Wind Serpent",
    "Carrion Bird",
    "Basilisk",
    "Threshadon",
    "Hydra",
    "Bog Beast",
    "Imp",
    "Felhound",
    "Felguard",
    "Skeleton",
    "Ghoul",
    "Banshee",
    "Gnoll",
    "Kobold",
    "Trogg",
    "Ogre",
    "Centaur",
    "Quilboar",
    "Furbolg",
    "Harpy",
    "Troll",
    "Naga",
    "Makrura",
    "Moonkin",
    "Worgen",
    "Poison Elemental",
    "Darkhound",
    "Crypt Fiend",
    "Gryphon",
    "Wind Rider",
    "Hippogryph",
    "Dragonhawk",
];

pub fn family_name(id: u8) -> &'static str {
    FAMILIES.get(usize::from(id)).copied().unwrap_or("Other")
}

fn role_name(id: u8) -> &'static str {
    ["Melee", "Ranged", "Tank"]
        .get(usize::from(id))
        .copied()
        .unwrap_or("Melee")
}

/// `AbilityKind`'s words.
fn kind_name(id: u8) -> &'static str {
    [
        "damage",
        "burst",
        "area",
        "taunt",
        "heal",
        "root",
        "interrupt",
        "stun",
        "area stun",
        "debuff",
        "buff",
        "defensive",
        "dispel",
        "gap closer",
        "fear",
        "mounted",
    ]
    .get(usize::from(id))
    .copied()
    .unwrap_or("")
}

const RARITIES: [&str; 5] = ["Common", "Uncommon", "Rare", "Epic", "Legendary"];

/// A rarity's colour: the item qualities' (Common white ... Legendary orange).
fn rarity_color(r: u8) -> Color {
    wow_ui::quality_color(r.min(4) + 1)
}

/// A stage's numeral, as the evolution path shows it.
pub fn stage_numeral(stage: u8) -> &'static str {
    ["I", "II", "III"]
        .get(usize::from(stage))
        .copied()
        .unwrap_or("?")
}

/// Each species' forms by stage, from the server's index (what this character may see): the item
/// name hook reads them, so a companion's item is named after its current form.
static FORMS: LazyLock<Mutex<HashMap<u16, Vec<String>>>> = LazyLock::new(Default::default);

pub fn form_name(species: u16, stage: u8) -> Option<String> {
    FORMS
        .lock()
        .ok()?
        .get(&species)?
        .get(usize::from(stage))
        .cloned()
}

/// A companion item's name at `stage`: its own name (`base`) at its first stage or when that
/// already names the form ("Horn of the War Wolf"), else the form's ("Elder Stranglethorn Tiger").
pub fn evolved_name(base: &str, stage: u8, form: &str) -> Option<String> {
    if stage == 0 || form.is_empty() || base.to_lowercase().contains(&form.to_lowercase()) {
        return None;
    }
    Some(form.to_string())
}

/// The item-name hook (`benilla_ui::script::set_item_name`): a companion item's name follows its
/// evolution, read off its suffix factor (the server's `ItemSeed`).
pub fn item_name(item_id: u32, seed: u32, base: &str) -> Option<String> {
    if !companion::is_companion_item(item_id) {
        return None;
    }
    let s = companion::decode_seed(seed)?;
    evolved_name(base, s.stage, &form_name(s.species, s.stage)?)
}

// ── State ────────────────────────────────────────────────────────────────────────────────────────

/// The window's pages, in its tabs' order.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Tab {
    /// The companions in the bags.
    #[default]
    Party,
    Bestiary,
}

/// What the server last sent, and the window's state.
#[derive(Resource, Debug)]
pub struct CompanionUi {
    pub open: bool,
    /// Opened at a stable master.
    pub npc: bool,
    pub tab: Tab,
    pub index: BTreeMap<u16, Entry>,
    /// Each species' models, move spells and where it is found (op 34).
    pub looks: HashMap<u16, Looks>,
    pub gm: bool,
    pub carried: Vec<Carried>,
    pub carried_header: u8,
    /// Reviving: draughts carried, the stable master's fee, how many need care.
    pub care: Care,
    pub species: Option<u16>,
    pub item: Option<u64>,
    /// The Bestiary entry's form on show (its model), 0-based.
    pub stage_shown: u8,
    /// The model's facing, radians (the stock pane's `SetFacing`).
    pub yaw: f32,
    /// The game master's give: stage (0-based) and level.
    pub give_stage: u8,
    pub give_level: u8,
    /// A card lit for a level or an evolution: its item, and until when (elapsed seconds).
    pub flash: Option<(u64, f32)>,
    /// A costly button pressed once (breeding, retraining): the next press on it sends it.
    armed: Option<Act>,
    built: bool,
    dirty: bool,
    asked_at: f32,
}

impl Default for CompanionUi {
    fn default() -> Self {
        Self {
            open: false,
            npc: false,
            // A new player's first look: their own companions (the Bestiary is the second tab).
            tab: Tab::Party,
            index: BTreeMap::new(),
            looks: HashMap::new(),
            gm: false,
            carried: Vec::new(),
            carried_header: 0,
            care: Care::default(),
            species: None,
            item: None,
            stage_shown: 0,
            yaw: LayerCreaturePreview::default().yaw,
            give_stage: 0,
            give_level: 1,
            flash: None,
            armed: None,
            built: false,
            dirty: true,
            asked_at: 0.0,
        }
    }
}

impl CompanionUi {
    /// The stable master's actions are on: opened at one, and the server says it is near.
    pub fn at_stable(&self) -> bool {
        self.npc && self.carried_header & CARRIED_AT_STABLE != 0
    }

    /// A carried companion's name: its current form's, else its species'.
    pub fn carried_name(&self, c: &Carried) -> String {
        self.index
            .get(&c.species)
            .and_then(|e| e.stages.get(usize::from(c.stage)))
            .map(|s| s.name.clone())
            .filter(|n| !n.is_empty())
            .unwrap_or_else(|| companion::species_name(c.species).to_string())
    }

    /// A species' family: the index's, else none known (the critters' row).
    pub fn family_of(&self, species: u16) -> u8 {
        self.index.get(&species).map_or(0, |e| e.family)
    }

    /// The model the window shows now: the chosen companion's current form, or the Bestiary
    /// entry's form on show. `None` while nothing is chosen or its looks have not come.
    pub fn preview_display(&self) -> Option<u32> {
        if !self.open {
            return None;
        }
        let (species, stage) = match self.tab {
            Tab::Party => {
                let c = self.selected()?;
                (c.species, c.stage)
            }
            Tab::Bestiary => {
                let id = self.species?;
                if self.index.get(&id).is_none_or(|e| e.seen == Seen::Unknown) {
                    return None;
                }
                (id, self.stage_shown)
            }
        };
        let looks = self.looks.get(&species)?;
        let stage = usize::from(stage).min(looks.stages.len().saturating_sub(1));
        looks
            .stages
            .get(stage)
            .map(|s| s.display)
            .filter(|&d| d != 0)
    }

    /// Apply one of the server's window messages; true when it was one.
    pub fn apply(&mut self, body: &[u8]) -> bool {
        match body.first() {
            Some(&OP_INDEX) => {
                let Some((flags, entries)) = decode_index(body) else {
                    return true;
                };
                if flags & INDEX_FIRST != 0 {
                    self.index.clear();
                }
                self.gm = flags & INDEX_GM != 0;
                if let Ok(mut forms) = FORMS.lock() {
                    for e in &entries {
                        if e.seen != Seen::Unknown {
                            forms.insert(e.id, e.stages.iter().map(|s| s.name.clone()).collect());
                        }
                    }
                }
                for e in entries {
                    self.index.insert(e.id, e);
                }
                self.dirty = true;
                true
            }
            Some(&OP_LOOKS) => {
                if let Some(list) = decode_looks(body) {
                    for l in list {
                        if self.looks.get(&l.id) != Some(&l) {
                            self.looks.insert(l.id, l);
                            self.dirty = true;
                        }
                    }
                }
                true
            }
            Some(&OP_CARRIED) => {
                if let Some((header, mut list, care)) = decode_carried(body) {
                    sort_carried(&mut list);
                    companion::note_fainted(list.iter().map(|c| {
                        (
                            c.seed,
                            (c.flags & CARRIED_FAINTED != 0).then_some(c.faint_secs),
                        )
                    }));
                    let changed = header != self.carried_header
                        || care != self.care
                        || list.len() != self.carried.len()
                        || list
                            .iter()
                            .zip(&self.carried)
                            .any(|(a, b)| !same_view(a, b));
                    self.carried_header = header;
                    self.care = care;
                    self.gm = header & CARRIED_GM != 0;
                    self.carried = list;
                    if self
                        .item
                        .is_some_and(|i| !self.carried.iter().any(|c| c.item == i))
                    {
                        self.item = None;
                    }
                    // The first look at the page: the first card is chosen.
                    if self.item.is_none() {
                        self.item = self.carried.first().map(|c| c.item);
                    }
                    if changed {
                        self.dirty = true;
                    }
                }
                true
            }
            _ => false,
        }
    }

    fn selected(&self) -> Option<&Carried> {
        let id = self.item?;
        self.carried.iter().find(|c| c.item == id)
    }
}

/// The list's order: the one out first, then those with something to do (an ability to learn,
/// ready to evolve), then by level (highest first), then as the bags hold them; the fainted
/// after the rest.
pub fn sort_carried(list: &mut [Carried]) {
    list.sort_by_key(|c| {
        let out = c.flags & (CARRIED_OUT | CARRIED_RIDING) != 0;
        let urgent = c.pending >= 0 || c.flags & CARRIED_CAN_EVOLVE != 0;
        let fainted = c.flags & CARRIED_FAINTED != 0;
        (!out, fainted, !urgent, std::cmp::Reverse(c.level))
    });
}

/// Whether two of a carried companion's lines draw the same: its breeding rest is shown in
/// hours and its faint in minutes, so their seconds ticking down are no change.
pub fn same_view(a: &Carried, b: &Carried) -> bool {
    let hours = |c: &Carried| c.breed_rest_secs.div_ceil(3600);
    let minutes = |c: &Carried| c.faint_secs.div_ceil(60);
    Carried {
        breed_rest_secs: 0,
        faint_secs: 0,
        ..*a
    } == Carried {
        breed_rest_secs: 0,
        faint_secs: 0,
        ..*b
    } && hours(a) == hours(b)
        && minutes(a) == minutes(b)
}

/// How long a wait still is, as the server words it (`Companions::WaitText`): "52 min",
/// "1 h 5 min", "40 s".
pub fn wait_text(secs: u32) -> String {
    if secs < 60 {
        return format!("{} s", secs.max(1));
    }
    let minutes = secs.div_ceil(60);
    if minutes < 60 {
        return format!("{minutes} min");
    }
    match minutes % 60 {
        0 => format!("{} h", minutes / 60),
        m => format!("{} h {m} min", minutes / 60),
    }
}

/// The price of a Reviving Draught at a stable master (`Companions::kReviveDraughtPrice`).
pub const DRAUGHT_PRICE: u32 = 1500;

/// The stable master's fee for breeding two companions (`Companions::BreedFee`): 10 silver a
/// level of their average, in copper.
pub fn breed_fee(level_a: u8, level_b: u8) -> u32 {
    1000 * (u32::from(level_a) + u32::from(level_b)) / 2
}

/// Copper as coins: "6g 5s".
pub fn coins_text(copper: u32) -> String {
    crate::build::homestead::money_text(copper)
}

/// Why two companions can't be bred now (None: they can, as far as the window knows; the
/// server checks the reagents and the gold).
pub fn breed_block(a: &Carried, b: &Carried) -> Option<String> {
    for c in [a, b] {
        if c.breed_count >= 3 {
            return Some("one of them has bred 3 times already".into());
        }
        if c.breed_rest_secs > 0 {
            return Some(format!(
                "one of them rests from its last litter ({} h)",
                c.breed_rest_secs.div_ceil(3600)
            ));
        }
        if c.flags & (CARRIED_OUT | CARRIED_RIDING) != 0 {
            return Some("put your companion away first".into());
        }
        if c.flags & CARRIED_FAINTED != 0 {
            return Some("one of them has fainted: revive it first".into());
        }
    }
    None
}

/// The server's lines, waiting for the UI error frame.
#[derive(Resource, Debug, Default)]
pub struct Notices(pub Vec<(NoticeKind, String)>);

/// A level or an evolution of the companion out, to cheer.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Cheer {
    Level { item: u64, species: u16, level: u8 },
    Evolved { item: u64, species: u16, stage: u8 },
}

/// The cheers waiting for [`cheer`].
#[derive(Resource, Debug, Default)]
pub struct Cheers(pub Vec<Cheer>);

/// A bar after the last one of the same companion: a level up when its level rose.
pub fn level_cheer(last: Option<&companion::Bar>, bar: &companion::Bar) -> Option<Cheer> {
    let last = last?;
    (bar.item != 0 && last.item == bar.item && bar.level > last.level).then_some(Cheer::Level {
        item: bar.item,
        species: bar.species,
        level: bar.level,
    })
}

/// The stock level-up chime for a level, the larger one for an evolution (the install's own).
pub fn cheer_sound(c: &Cheer) -> &'static str {
    match c {
        Cheer::Level { .. } => "Sound\\Spells\\ReputationLevelUp.wav",
        Cheer::Evolved { .. } => "Sound\\Spells\\LevelUp.wav",
    }
}

/// An evolution's banner: "EVOLVED", "Wolf Pup evolved into Dire Wolf!" (its forms by the
/// index, else its species).
pub fn evolve_banner(species: u16, stage: u8) -> crate::streaks::Banner {
    let name = |st: u8| form_name(species, st).filter(|n| !n.is_empty());
    let into = name(stage).unwrap_or_else(|| companion::species_name(species).to_string());
    let subtitle = match stage.checked_sub(1).and_then(name) {
        Some(from) => format!("{from} evolved into {into}!"),
        None => format!("Your companion evolved into {into}!"),
    };
    crate::streaks::Banner {
        title: "EVOLVED".into(),
        subtitle,
    }
}

/// The window's key in `WindowPositions`.
const WINDOW_KEY: &str = "companions";

// ── The plugin ───────────────────────────────────────────────────────────────────────────────────

pub(crate) fn plugin(app: &mut App) {
    benilla_ui::script::set_item_name(item_name);
    app.init_resource::<CompanionUi>()
        .init_resource::<Notices>()
        .init_resource::<Cheers>()
        .add_systems(
            Update,
            (
                read,
                ask_on_entering,
                toggle,
                build_window,
                refresh,
                click,
                turn_model,
                drive_preview,
                poll,
                show_notices,
                cheer,
                duel_prompt,
            )
                .chain()
                .in_set(Mw2Set::Hud),
        )
        .add_systems(PostUpdate, hold_panel);
}

fn send(out: &mut MessageWriter<SendFusionChannel>, body: Vec<u8>) {
    out.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL_COMPANION,
        body,
    }));
}

fn read(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut ui: ResMut<CompanionUi>,
    mut notices: ResMut<Notices>,
    mut cheers: ResMut<Cheers>,
    mut open: MessageWriter<OpenPanel>,
    mut last_bar: Local<Option<companion::Bar>>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_COMPANION {
            continue;
        }
        if ui.apply(&m.body) {
            continue;
        }
        match m.body.first() {
            Some(&OP_NOTICE) => {
                if let Some(n) = decode_notice(&m.body) {
                    notices.0.push(n);
                }
            }
            Some(&OP_OPEN) => {
                if let Some(npc) = decode_open(&m.body) {
                    open.write(OpenPanel {
                        panel: Panel::Companions,
                        access: if npc { Access::Npc } else { Access::Hud },
                    });
                }
            }
            // An evolution changes what the list shows; so does a bar whose companion or state
            // changed (a summon, a dismissal), not one whose health moved.
            Some(&companion::OP_EVOLVED) => {
                if let Some((item, species, stage)) = companion::decode_evolved(&m.body) {
                    cheers.0.push(Cheer::Evolved {
                        item,
                        species,
                        stage,
                    });
                }
                if ui.open {
                    ui.asked_at = f32::MIN;
                }
            }
            Some(&companion::OP_BAR) => {
                if let Some(bar) = companion::decode_bar(&m.body) {
                    if let Some(c) = level_cheer(last_bar.as_ref(), &bar) {
                        cheers.0.push(c);
                    }
                    let moved = last_bar.as_ref().is_none_or(|l| {
                        (l.item, l.flags, l.level) != (bar.item, bar.flags, bar.level)
                    });
                    if moved && ui.open {
                        ui.asked_at = f32::MIN;
                    }
                    *last_bar = Some(bar);
                }
            }
            // The duel buttons follow the duel.
            Some(&companion::OP_DUEL) if ui.open => {
                ui.dirty = true;
            }
            _ => {}
        }
    }
}

/// Entering the world: ask for the index, so items are named by their forms from the start.
fn ask_on_entering(
    state: Res<FpsState>,
    mut was: Local<bool>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    if state.in_world && !*was {
        send(&mut out, encode_request(Request::Index));
    }
    *was = state.in_world;
}

#[derive(Component)]
struct UiRoot;
#[derive(Component)]
struct UiClose;
#[derive(Component)]
struct ListView;
#[derive(Component)]
struct DetailView;
#[derive(Component)]
struct Summary;
#[derive(Component)]
struct TabMark(Tab);
/// The 3D model's picture (the stable pane's bake).
#[derive(Component)]
struct ModelPane;
/// What stands in the model's frame until its model is there: the family's icon.
#[derive(Component)]
struct ModelStandIn;
/// The model's frame: a drag across it turns the model.
#[derive(Component)]
struct ModelTurn;

#[allow(clippy::too_many_arguments)]
fn toggle(
    mut opens: MessageReader<OpenPanel>,
    input: Res<crate::input::Mw2Input>,
    mut ui: ResMut<CompanionUi>,
    mut state: ResMut<FpsState>,
    cac: Res<crate::cac_ui::CacUi>,
    ach: Res<crate::achievements::Achievements>,
    close: Query<&Interaction, (With<UiClose>, Changed<Interaction>)>,
    roots: Query<Entity, With<UiRoot>>,
    mut out: MessageWriter<SendFusionChannel>,
    mut commands: Commands,
) {
    let mut open = ui.open;
    for o in opens.read() {
        if o.panel != Panel::Companions {
            continue;
        }
        match o.access {
            // From the HUD: a toggle, the limited view.
            Access::Hud => {
                open = !open;
                ui.npc = false;
            }
            Access::Npc => {
                open = true;
                ui.npc = true;
                ui.tab = Tab::Party;
            }
        }
        ui.dirty = true;
    }
    if (input.escape && ui.open)
        || close.iter().any(|i| *i == Interaction::Pressed)
        || !crate::capture_keep::in_world(state.in_world, "companions")
    {
        open = false;
    }
    if crate::capture_keep::keeps("companions") {
        open = true;
    }
    if open && !ui.open {
        send(&mut out, encode_request(Request::Index));
        ui.asked_at = 0.0;
    }
    if open != ui.open {
        ui.open = open;
        ui.dirty = true;
        if !open {
            ui.npc = false;
            ui.armed = None;
            for e in &roots {
                commands.entity(e).despawn();
            }
            ui.built = false;
            state.panel = cac.open || ach.open;
        }
    }
}

/// The cursor out and the gun down while the window is open: the windows share
/// `FpsState::panel`, and Create-a-Class's toggle writes its own state there every frame, so the
/// flag is held after every window's systems have run.
fn hold_panel(ui: Res<CompanionUi>, mut state: ResMut<FpsState>) {
    if ui.open && !state.panel {
        state.panel = true;
    }
}

/// The window's width, the list's, and how tall the two columns are before they scroll, in UI
/// units. Every part of the window has a fixed size, so nothing chosen or shown moves it.
const WINDOW_W: f32 = 780.0;
const LIST_W: f32 = 250.0;
const VIEW_H: f32 = 400.0;
/// The summary line over the columns.
const SUMMARY_H: f32 = 20.0;
/// A companion's card in the list.
const CARD_H: f32 = 48.0;
/// The model's frame.
const MODEL: f32 = 150.0;

fn build_window(mut commands: Commands, assets: Res<AssetServer>, mut ui: ResMut<CompanionUi>) {
    if ui.built || !ui.open {
        return;
    }
    ui.built = true;
    ui.dirty = true;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: WINDOW_KEY,
        title: "Companions".into(),
        frame: Frame::Dialog,
        width: Some(WINDOW_W),
        movable: true,
        anchor: Anchor::CENTER,
        z: i32::MAX - 1,
    };
    wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        (UiRoot, wow_ui::WindowSounds::SPELLBOOK),
        Some(UiClose),
        |root| {
            // The summary and the reset, on one line of a fixed height.
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                align_items: AlignItems::Center,
                justify_content: JustifyContent::SpaceBetween,
                height: u(SUMMARY_H),
                flex_shrink: 0.0,
                column_gap: u(8.0),
                ..default()
            })
            .with_children(|r| {
                r.spawn(Node {
                    flex_grow: 1.0,
                    min_width: u(0.0),
                    height: u(SUMMARY_H),
                    align_items: AlignItems::Center,
                    overflow: Overflow::clip(),
                    ..default()
                })
                .with_children(|s| {
                    s.spawn((text(f, "", wow_ui::SMALL_SIZE, DIM), Summary));
                });
            });
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                column_gap: u(10.0),
                margin: UiRect::vertical(u(6.0)),
                ..default()
            })
            .with_children(|row| {
                wow_ui::inset(
                    row,
                    Node {
                        flex_direction: FlexDirection::Column,
                        flex_shrink: 0.0,
                        ..default()
                    },
                    (),
                    |c| {
                        wow_ui::scroll_frame(c, VIEW_H, Some(LIST_W), ListView, |_| {});
                    },
                );
                wow_ui::inset(
                    row,
                    Node {
                        flex_direction: FlexDirection::Column,
                        flex_grow: 1.0,
                        min_width: u(0.0),
                        ..default()
                    },
                    (),
                    |c| {
                        wow_ui::scroll_frame(c, VIEW_H, None, DetailView, |_| {});
                    },
                );
            });
        },
        |below| {
            wow_ui::tab_strip(below, |t| {
                t.spawn(wow_ui::tab(
                    f,
                    "Companions",
                    (Act::Tab(Tab::Party), TabMark(Tab::Party)),
                ));
                t.spawn(wow_ui::tab(
                    f,
                    "Bestiary",
                    (Act::Tab(Tab::Bestiary), TabMark(Tab::Bestiary)),
                ));
            });
        },
    );
}

/// What a button in the window does.
#[derive(Component, Clone, Copy, Debug, PartialEq, Eq)]
enum Act {
    Tab(Tab),
    Species(u16),
    /// The Bestiary entry's form to show.
    ShowStage(u8),
    Item(u64),
    Summon(u64),
    /// Its duel deck (crate::companion_battle's Companion Deck window).
    Deck(u64),
    Ride,
    Evolve(u64),
    Pick {
        item: u64,
        stage: u8,
        option: u8,
    },
    Repick {
        item: u64,
        stage: u8,
        option: u8,
    },
    Breed {
        a: u64,
        b: u64,
    },
    Challenge,
    Forfeit,
    GiveStage(i8),
    GiveLevel(i8),
    Give,
    Edit {
        item: u64,
        edit: Edit,
    },
    StableRevive,
    UseDraught(u64),
    BuyDraught,
    /// Turn the model a step (-1 left, 1 right).
    Turn(i8),
}

/// A line of text in a column.
fn line(p: &mut ChildSpawnerCommands, f: &Handle<Font>, s: &str, size: f32, color: Color) {
    p.spawn(text(f, s, size, color));
}

/// A row of buttons that wraps.
fn buttons(p: &mut ChildSpawnerCommands, fill: impl FnOnce(&mut ChildSpawnerCommands)) {
    p.spawn(Node {
        flex_direction: FlexDirection::Row,
        flex_wrap: FlexWrap::Wrap,
        align_items: AlignItems::Center,
        column_gap: u(2.0),
        row_gap: u(2.0),
        margin: UiRect::vertical(u(2.0)),
        ..default()
    })
    .with_children(fill);
}

fn button(f: &Handle<Font>, label: &str, enabled: bool, selected: bool, act: Act) -> impl Bundle {
    let mut b = WowButton::new(Kind::Panel);
    b.disabled = !enabled;
    b.selected = selected;
    wow_ui::button_fit(f, label, b, (act, act_tip(label, act)))
}

/// An action: its icon beside its button.
#[allow(clippy::too_many_arguments)]
fn action(
    b: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    icon: &str,
    label: &str,
    enabled: bool,
    selected: bool,
    act: Act,
) {
    b.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        column_gap: u(1.0),
        margin: UiRect::right(u(4.0)),
        ..default()
    })
    .with_children(|r| {
        r.spawn(icon_node(assets, icon, 22.0, wow_ui::SLOT_EDGE, enabled));
        r.spawn(button(f, label, enabled, selected, act));
    });
}

/// An icon from the install, `size` units, in a `border` edge, greyed when not `lit`.
fn icon_node(assets: &AssetServer, path: &str, size: f32, border: Color, lit: bool) -> impl Bundle {
    wow_ui::icon_frame_lit(
        Some(assets.load(crate::cac_ui::spell_icon_path(path))),
        size,
        border,
        lit,
    )
}

/// What a button does, on hover (empty for the plain ones: the GM's steps, the tabs).
/// A Bestiary line's tooltip: the kind, and how far the entry is filled in.
fn species_tip(name: &str, seen: Seen) -> wow_ui::WowTip {
    let body = match seen {
        Seen::Captured => "Caught. Click to see its entry.",
        Seen::Seen => "Seen in the wild. Carry one to mark it caught.",
        Seen::Unknown => "Not yet seen. Target one in the wild to add its entry.",
    };
    wow_ui::WowTip::new(
        if seen == Seen::Unknown {
            "Unknown"
        } else {
            name
        },
        body,
    )
}

fn act_tip(label: &str, act: Act) -> wow_ui::WowTip {
    let body = match act {
        Act::Deck(_) => {
            "Opens its duel deck: every card it knows for companion duels and how many of each              its deck holds. Change it with + and -, then Save."
        }
        Act::Summon(_) => {
            "Calls this companion to your side, or sends it back to its item. One is out at a \
             time; it comes back as hurt as it went and heals while it rests. A fainted one \
             can't be called until it is revived."
        }
        Act::Ride => {
            "Rides your companion out (its last form is a mount), or gets off it. Vanilla's \
             riding rules hold: level 40 and a riding skill, no mounts indoors."
        }
        Act::Evolve(_) => {
            "Grows it into its next form, in front of you: it must be out. What the next form \
             needs (level, place, hour, kills, catalyst) is written on its evolution path."
        }
        Act::Pick { .. } => {
            "Teaches it this move for the stage. A stable master can change it later."
        }
        Act::Repick { .. } => "Swaps the stage's move for this one, for a fee.",
        Act::Breed { .. } => {
            "Breeds the two into a new companion of the next generation. Each needs rest after."
        }
        Act::Challenge => "Challenges your target's companion to a companion duel.",
        Act::StableRevive => {
            "The stable master revives every fainted companion in your bags (a fee by level) and \
             heals every hurt one, free."
        }
        Act::UseDraught(_) => {
            "Gives this fainted companion a Reviving Draught from your bags: it is back on its \
             feet at half health."
        }
        Act::BuyDraught => {
            "Buys a Reviving Draught: it revives a fainted companion anywhere, at half health. \
             General goods vendors sell them too."
        }
        Act::Forfeit => "Gives up the companion duel in progress.",
        Act::Turn(_) => "Turns the model. Dragging across the picture turns it too.",
        Act::ShowStage(_) => "Shows this form.",
        _ => return wow_ui::WowTip::default(),
    };
    wow_ui::WowTip::new(label, body)
}

fn heading(p: &mut ChildSpawnerCommands, f: &Handle<Font>, s: &str) {
    wow_ui::section_header(p, f, s);
}

/// The traits in a mask, by id.
fn trait_list(mask: u32) -> Vec<u8> {
    (1..=30u8).filter(|t| mask & (1 << (t - 1)) != 0).collect()
}

fn trait_ids(mask: u32) -> [u8; 3] {
    let mut ids = [0u8; 3];
    for (slot, t) in trait_list(mask).into_iter().take(3).enumerate() {
        ids[slot] = t;
    }
    ids
}

/// "Evolves at level 30" for a carried companion.
fn evolve_text(c: &Carried) -> String {
    companion::block_text(&companion::Bar {
        block: c.block,
        next_evolve_level: c.next_evolve_level,
        catalyst_count: 1,
        ..companion::Bar::default()
    })
}

/// A carried companion's line: "Level 18 · stage 1/3 · out · ready to evolve" (its card's
/// tooltip). `stages` is its line's stage count when the index has it.
pub fn carried_status(c: &Carried, stages: Option<usize>) -> String {
    let stages = stages.map_or(usize::from(c.stage) + 1, |n| {
        n.max(usize::from(c.stage) + 1)
    });
    let mut s = format!("Level {} · stage {}/{}", c.level, c.stage + 1, stages);
    if c.flags & CARRIED_RIDING != 0 {
        s.push_str(" · ridden");
    } else if c.flags & CARRIED_OUT != 0 {
        s.push_str(" · out");
    } else if c.flags & CARRIED_FAINTED != 0 {
        s.push_str(&format!(" · fainted ({})", wait_text(c.faint_secs)));
    } else if c.hurt_pct > 0 {
        s.push_str(&format!(" · {}% health", 100 - u32::from(c.hurt_pct)));
    }
    if c.pending >= 0 {
        s.push_str(" · an ability to learn");
    } else if c.flags & CARRIED_CAN_EVOLVE != 0 {
        s.push_str(" · ready to evolve");
    }
    s
}

/// A card's badge line, the most pressing first: "Ready to evolve!", "New move to learn!",
/// "Fainted · 52 min", "Ridden", "Out", or nothing; with its colour.
pub fn card_badge(c: &Carried) -> (String, Color) {
    if c.flags & CARRIED_FAINTED != 0 && c.flags & (CARRIED_OUT | CARRIED_RIDING) == 0 {
        return (format!("Fainted · {}", wait_text(c.faint_secs)), RED);
    }
    if c.flags & CARRIED_CAN_EVOLVE != 0 {
        return ("Ready to evolve!".into(), GOLD);
    }
    if c.pending >= 0 {
        return ("New move to learn!".into(), GOLD);
    }
    if c.flags & CARRIED_RIDING != 0 {
        return ("Ridden".into(), GREEN);
    }
    if c.flags & CARRIED_OUT != 0 {
        return ("Out".into(), GREEN);
    }
    (String::new(), DIM)
}

/// Its health's share (0 fainted, 1 whole) and its bar's colour: the unit frames' green, yellow
/// under 60%, red under 35%.
pub fn health_of(c: &Carried) -> (f32, Color) {
    let share = if c.flags & CARRIED_FAINTED != 0 && c.flags & (CARRIED_OUT | CARRIED_RIDING) == 0 {
        0.0
    } else {
        f32::from(100 - c.hurt_pct.min(100)) / 100.0
    };
    let color = if share < 0.35 {
        Color::srgba(0.85, 0.15, 0.1, 0.95)
    } else if share < 0.6 {
        Color::srgba(0.9, 0.75, 0.1, 0.95)
    } else {
        Color::srgba(0.1, 0.75, 0.1, 0.95)
    };
    (share, color)
}

/// The experience bar's share and its words: "Experience 1234 / 7350 (16%)", and at the top
/// "Level 60: fully trained".
pub fn xp_progress(c: &Carried) -> (f32, String) {
    if c.xp_to_next == 0 {
        return (1.0, format!("Level {}: fully trained", c.level));
    }
    let share = (c.xp as f32 / c.xp_to_next as f32).clamp(0.0, 1.0);
    (
        share,
        format!(
            "Experience {} / {} ({}%)",
            c.xp,
            c.xp_to_next,
            (share * 100.0).floor() as u32
        ),
    )
}

/// What the next form needs, from the index ("Level 40, 1 Primal Beast Essence, in
/// Stranglethorn Vale"), with how many levels are still to go; `None` at the final stage.
pub fn next_form_line(c: &Carried, entry: Option<&Entry>) -> Option<String> {
    if c.flags & CARRIED_FINAL != 0 {
        return None;
    }
    let next = entry.and_then(|e| e.stages.get(usize::from(c.stage) + 1));
    let mut s = match next {
        Some(st) if !st.name.is_empty() => format!("Next form, {}: {}", st.name, st.reach),
        Some(st) => format!("Next form: {}", st.reach),
        None => evolve_text(c),
    };
    if c.next_evolve_level > c.level {
        let to_go = c.next_evolve_level - c.level;
        s.push_str(&format!(
            " ({to_go} level{} to go)",
            if to_go == 1 { "" } else { "s" }
        ));
    }
    Some(s)
}

/// A bar in the stock status bars' look, `h` units tall: `share` filled in `color`, `label` over
/// it (none on a thin one).
fn bar(
    p: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    share: f32,
    label: &str,
    color: Color,
    h: f32,
) {
    p.spawn((
        Node {
            width: percent(100),
            height: u(h),
            margin: UiRect::vertical(u(1.0)),
            border: UiRect::all(px(1)),
            justify_content: JustifyContent::Center,
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            overflow: Overflow::clip(),
            ..default()
        },
        BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.55)),
        BorderColor::all(Color::srgba(0.45, 0.45, 0.45, 0.9)),
    ))
    .with_children(|b| {
        b.spawn((
            Node {
                position_type: PositionType::Absolute,
                left: u(0.0),
                top: u(0.0),
                bottom: u(0.0),
                width: percent(share.clamp(0.0, 1.0) * 100.0),
                ..default()
            },
            BackgroundColor(color),
        ));
        if !label.is_empty() {
            b.spawn(text(f, label, wow_ui::NOTE_SIZE, WHITE));
        }
    });
}

/// The experience bar's purple, the stock rested-less experience bar's.
const XP_PURPLE: Color = Color::srgba(0.58, 0.0, 0.55, 0.9);
/// The evolution bar's green.
const EVOLVE_GREEN: Color = Color::srgba(0.1, 0.55, 0.15, 0.9);

/// The spell catalogue the move icons come from, when the install's is loaded.
type Spells<'a> = Option<&'a benilla_formats::SpellCatalog>;

#[allow(clippy::too_many_arguments)]
fn refresh(
    mut commands: Commands,
    assets: Res<AssetServer>,
    mut ui: ResMut<CompanionUi>,
    duel: Res<companion::CompanionState>,
    table: Option<Res<crate::hands::SpellTable>>,
    lists: Query<Entity, With<ListView>>,
    details: Query<Entity, With<DetailView>>,
    mut summary: Query<&mut Text, With<Summary>>,
    mut tabs: Query<(&TabMark, &mut WowButton)>,
) {
    if !ui.open {
        return;
    }
    for (t, mut b) in &mut tabs {
        let selected = t.0 == ui.tab;
        if b.selected != selected {
            b.selected = selected;
        }
    }
    if !ui.dirty {
        return;
    }
    let (Ok(list), Ok(detail)) = (lists.single(), details.single()) else {
        return;
    };
    ui.dirty = false;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let ui = &*ui;
    let spells: Spells = table.as_deref().and_then(|t| t.0.as_deref());
    if let Ok(mut s) = summary.single_mut() {
        **s = summary_text(ui);
    }
    commands.entity(list).despawn_related::<Children>();
    commands.entity(detail).despawn_related::<Children>();
    match ui.tab {
        Tab::Bestiary => {
            commands
                .entity(list)
                .with_children(|l| bestiary_list(l, f, &assets, ui));
            commands
                .entity(detail)
                .with_children(|d| bestiary_entry(d, f, &assets, ui, spells));
        }
        Tab::Party => {
            commands
                .entity(list)
                .with_children(|l| party_list(l, f, &assets, ui));
            commands
                .entity(detail)
                .with_children(|d| companion_page(d, f, &assets, ui, &duel, spells));
        }
    }
}

/// The line over the columns: "3 companions · Bestiary: 24 of 97 seen, 5 caught · at the
/// stable master".
pub fn summary_text(ui: &CompanionUi) -> String {
    if ui.index.is_empty() && ui.carried.is_empty() {
        return "Loading...".into();
    }
    let known = ui
        .index
        .values()
        .filter(|e| e.seen != Seen::Unknown)
        .count();
    let caught = ui
        .index
        .values()
        .filter(|e| e.seen == Seen::Captured)
        .count();
    let mut t = format!(
        "{} companion{}  ·  Bestiary: {known} of {} seen, {caught} caught",
        ui.carried.len(),
        if ui.carried.len() == 1 { "" } else { "s" },
        ui.index.len(),
    );
    if ui.at_stable() {
        t.push_str("  ·  at the stable master");
    }
    if ui.gm {
        t.push_str("  ·  game master");
    }
    t
}

// ── The Bestiary ─────────────────────────────────────────────────────────────────────────────────

/// The Bestiary by family: a family's heading, then its species (`???` for one not yet seen),
/// each with its family's icon and its mark (seen, caught).
fn bestiary_list(
    l: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    ui: &CompanionUi,
) {
    let mut families: BTreeMap<&str, (u8, Vec<&Entry>)> = BTreeMap::new();
    for e in ui.index.values() {
        families
            .entry(family_name(e.family))
            .or_insert_with(|| (e.family, Vec::new()))
            .1
            .push(e);
    }
    for (family, (id, entries)) in families {
        let seen = entries.iter().filter(|e| e.seen != Seen::Unknown).count();
        l.spawn(Node {
            flex_direction: FlexDirection::Row,
            align_items: AlignItems::Center,
            column_gap: u(4.0),
            margin: UiRect::top(u(wow_ui::SECTION_GAP * 0.5)),
            flex_shrink: 0.0,
            ..default()
        })
        .with_children(|h| {
            h.spawn(icon_node(
                assets,
                &icons::family_icon(id),
                16.0,
                wow_ui::SLOT_EDGE,
                seen > 0,
            ));
            h.spawn(text(
                f,
                &format!("{family}  ({seen}/{})", entries.len()),
                wow_ui::BODY_SIZE,
                GOLD,
            ));
        });
        for e in entries {
            let known = e.seen != Seen::Unknown;
            let label = if known { e.name.clone() } else { "???".into() };
            l.spawn((
                Button,
                WowButton {
                    selected: ui.species == Some(e.id),
                    disabled: !known && !ui.gm,
                    kind: Kind::Row,
                },
                Act::Species(e.id),
                species_tip(&label, e.seen),
                Node {
                    width: u(LIST_W - 24.0),
                    height: u(22.0),
                    padding: UiRect::horizontal(u(3.0)),
                    column_gap: u(5.0),
                    align_items: AlignItems::Center,
                    flex_shrink: 0.0,
                    ..default()
                },
            ))
            .with_children(|r| {
                wow_ui::row_glow(r);
                let portrait = if known {
                    icons::family_icon(e.family)
                } else {
                    icons::path(icons::UNKNOWN)
                };
                r.spawn(icon_node(assets, &portrait, 18.0, wow_ui::SLOT_EDGE, known));
                r.spawn((
                    Node {
                        flex_grow: 1.0,
                        min_width: u(0.0),
                        overflow: Overflow::clip(),
                        ..default()
                    },
                    children![text(
                        f,
                        &label,
                        wow_ui::BODY_SIZE,
                        if known { WHITE } else { GREY }
                    )],
                ));
                let mark = match e.seen {
                    Seen::Captured => Some(icons::CAUGHT),
                    Seen::Seen => Some(icons::SEEN),
                    Seen::Unknown => None,
                };
                if let Some(mark) = mark {
                    r.spawn(icon_node(
                        assets,
                        &icons::path(mark),
                        14.0,
                        wow_ui::SLOT_EDGE,
                        true,
                    ));
                }
            });
        }
    }
    if ui.index.is_empty() {
        line(l, f, "Nothing yet.", 12.0, GREY);
    }
}

/// A Bestiary entry: the creature in 3D, what it is, where it is found, its evolution path (each
/// form shown on a click), its moves per stage, its traits.
fn bestiary_entry(
    d: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    ui: &CompanionUi,
    spells: Spells,
) {
    let Some(e) = ui.species.and_then(|id| ui.index.get(&id)) else {
        line(
            d,
            f,
            "Choose a creature on the left. Target a creature to add it to the Bestiary; catch one to complete its entry.",
            12.0,
            DIM,
        );
        return;
    };
    if e.seen == Seen::Unknown {
        model_header(d, f, assets, &icons::path(icons::UNKNOWN), |info| {
            line(info, f, "???", wow_ui::TITLE_SIZE + 2.0, GREY);
            line(
                info,
                f,
                &format!("An unknown {}.", family_name(e.family)),
                wow_ui::SMALL_SIZE,
                DIM,
            );
            line(
                info,
                f,
                "Target one to add its entry.",
                wow_ui::SMALL_SIZE,
                DIM,
            );
        });
        return;
    }
    let looks = ui.looks.get(&e.id);
    let shown = usize::from(ui.stage_shown).min(e.stages.len().saturating_sub(1));
    model_header(d, f, assets, &icons::family_icon(e.family), |info| {
        let form = e
            .stages
            .get(shown)
            .map_or(e.name.as_str(), |s| s.name.as_str());
        line(info, f, form, wow_ui::TITLE_SIZE + 2.0, WHITE);
        if form != e.name {
            line(
                info,
                f,
                &format!("of the {} line", e.name),
                wow_ui::SMALL_SIZE,
                DIM,
            );
        }
        icon_line(
            info,
            f,
            assets,
            &icons::family_icon(e.family),
            &format!("{}  ·  {}", family_name(e.family), role_name(e.role)),
            DIM,
        );
        let (mark, words, color) = if e.seen == Seen::Captured {
            (icons::CAUGHT, "Caught", GREEN)
        } else {
            (icons::SEEN, "Seen", WHITE)
        };
        icon_line(info, f, assets, &icons::path(mark), words, color);
        if let Some(l) = looks.filter(|l| !l.found.is_empty()) {
            line(info, f, &l.found, wow_ui::NOTE_SIZE, DIM);
        }
    });
    heading(d, f, "Evolution");
    evolution_path(d, f, assets, e, None, true, ui.stage_shown);
    for (i, s) in e.stages.iter().enumerate() {
        if !s.reach.is_empty() && i > 0 {
            line(
                d,
                f,
                &format!("{}: {}", stage_numeral(i as u8), s.reach),
                wow_ui::NOTE_SIZE,
                DIM,
            );
        }
    }
    heading(d, f, "Moves");
    if !e.trait_name.is_empty() {
        icon_line(
            d,
            f,
            assets,
            &icons::path(icons::LEARN),
            &format!("Family trait: {}", e.trait_name),
            GREEN,
        );
    }
    for (i, s) in e.stages.iter().enumerate() {
        line(
            d,
            f,
            &format!("Stage {}: one of", stage_numeral(i as u8)),
            wow_ui::SMALL_SIZE,
            GOLD,
        );
        for o in 0..2 {
            if s.abilities[o].is_empty() {
                continue;
            }
            let spell = looks
                .and_then(|l| l.stages.get(i))
                .map_or(0, |l| l.spells[o]);
            move_row(
                d,
                f,
                assets,
                &icons::move_icon(spell, s.kinds[o], spells),
                &s.abilities[o],
                &format!("{}, {} s cooldown", kind_name(s.kinds[o]), s.cooldowns[o]),
                WHITE,
                true,
            );
        }
    }
    if !e.mounted.is_empty() {
        let spell = looks.map_or(0, |l| l.mounted_spell);
        move_row(
            d,
            f,
            assets,
            &icons::move_icon(spell, 15, spells),
            &e.mounted,
            "while ridden",
            WHITE,
            true,
        );
    }
    heading(d, f, "Rarity and Traits");
    line(
        d,
        f,
        "Rolled when it drops or is bred: Common, Uncommon (one trait), Rare (two), Epic (three), Legendary.",
        wow_ui::NOTE_SIZE,
        DIM,
    );
    trait_chips(d, f, assets, &trait_list(e.eligible), true);
    if ui.gm {
        gm_give(d, f, ui, e);
    }
}

/// A game master's give: any species into the bags at a stage and level.
fn gm_give(d: &mut ChildSpawnerCommands, f: &Handle<Font>, ui: &CompanionUi, e: &Entry) {
    heading(d, f, "Game Master: Put One in Your Bags");
    let stages = e.stages.len().max(1) as u8;
    let stage = ui.give_stage.min(stages - 1);
    let form = e
        .stages
        .get(usize::from(stage))
        .map_or("", |s| s.name.as_str());
    buttons(d, |b| {
        b.spawn(button(f, "<", stage > 0, false, Act::GiveStage(-1)));
        b.spawn(text(
            f,
            &format!("Stage {} ({form})", stage + 1),
            12.0,
            WHITE,
        ));
        b.spawn(button(f, ">", stage + 1 < stages, false, Act::GiveStage(1)));
    });
    buttons(d, |b| {
        b.spawn(button(
            f,
            "-10",
            ui.give_level > 1,
            false,
            Act::GiveLevel(-10),
        ));
        b.spawn(button(
            f,
            "-1",
            ui.give_level > 1,
            false,
            Act::GiveLevel(-1),
        ));
        b.spawn(text(f, &format!("Level {}", ui.give_level), 12.0, WHITE));
        b.spawn(button(
            f,
            "+1",
            ui.give_level < 60,
            false,
            Act::GiveLevel(1),
        ));
        b.spawn(button(
            f,
            "+10",
            ui.give_level < 60,
            false,
            Act::GiveLevel(10),
        ));
    });
    buttons(d, |b| {
        b.spawn(button(f, "Add to Bags", true, false, Act::Give));
    });
}

// ── The pieces of a page ─────────────────────────────────────────────────────────────────────────

/// A page's head: the model's frame on the left (its family's icon standing in until the model is
/// there, the turn arrows under it), `info` beside it.
fn model_header(
    d: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    stand_in: &str,
    info: impl FnOnce(&mut ChildSpawnerCommands),
) {
    d.spawn(Node {
        flex_direction: FlexDirection::Row,
        column_gap: u(10.0),
        flex_shrink: 0.0,
        ..default()
    })
    .with_children(|row| {
        row.spawn(Node {
            flex_direction: FlexDirection::Column,
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            row_gap: u(2.0),
            ..default()
        })
        .with_children(|m| {
            m.spawn((
                ModelTurn,
                Button,
                FocusPolicy::Block,
                wow_ui::WowTip::new("Model", "Drag across it to turn it."),
                Node {
                    width: u(MODEL),
                    height: u(MODEL),
                    border: UiRect::all(px(1)),
                    justify_content: JustifyContent::Center,
                    align_items: AlignItems::Center,
                    overflow: Overflow::clip(),
                    ..default()
                },
                BackgroundColor(Color::srgb(0.055, 0.045, 0.04)),
                BorderColor::all(GOLD.with_alpha(0.5)),
            ))
            .with_children(|pane| {
                pane.spawn((
                    ModelStandIn,
                    icon_node(assets, stand_in, 64.0, wow_ui::SLOT_EDGE, true),
                ));
                pane.spawn((
                    ModelPane,
                    Node {
                        position_type: PositionType::Absolute,
                        left: px(0),
                        top: px(0),
                        width: percent(100),
                        height: percent(100),
                        ..default()
                    },
                    ImageNode::default(),
                    Visibility::Hidden,
                    bevy::picking::Pickable::IGNORE,
                ));
            });
            m.spawn(Node {
                flex_direction: FlexDirection::Row,
                column_gap: u(4.0),
                ..default()
            })
            .with_children(|b| {
                b.spawn(wow_ui::button_sized(
                    f,
                    "<",
                    36.0,
                    20.0,
                    (Act::Turn(-1), act_tip("Turn left", Act::Turn(-1))),
                ));
                b.spawn(wow_ui::button_sized(
                    f,
                    ">",
                    36.0,
                    20.0,
                    (Act::Turn(1), act_tip("Turn right", Act::Turn(1))),
                ));
            });
        });
        row.spawn(Node {
            flex_direction: FlexDirection::Column,
            flex_grow: 1.0,
            min_width: u(0.0),
            row_gap: u(2.0),
            ..default()
        })
        .with_children(info);
    });
}

/// An icon with a line beside it.
fn icon_line(
    p: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    icon: &str,
    s: &str,
    color: Color,
) {
    p.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        column_gap: u(4.0),
        flex_shrink: 0.0,
        ..default()
    })
    .with_children(|r| {
        r.spawn(icon_node(assets, icon, 16.0, wow_ui::SLOT_EDGE, true));
        r.spawn((
            Node {
                min_width: u(0.0),
                flex_shrink: 1.0,
                ..default()
            },
            children![text(f, s, wow_ui::SMALL_SIZE, color)],
        ));
    });
}

/// A move: its icon, its name, what it is under it.
#[allow(clippy::too_many_arguments)]
fn move_row(
    p: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    icon: &str,
    name: &str,
    sub: &str,
    color: Color,
    lit: bool,
) {
    p.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        column_gap: u(6.0),
        margin: UiRect::vertical(u(1.0)),
        flex_shrink: 0.0,
        ..default()
    })
    .with_children(|r| {
        r.spawn(icon_node(assets, icon, 26.0, wow_ui::SLOT_EDGE, lit));
        r.spawn(Node {
            flex_direction: FlexDirection::Column,
            min_width: u(0.0),
            ..default()
        })
        .with_children(|t| {
            t.spawn(text(f, name, wow_ui::BODY_SIZE, color));
            if !sub.is_empty() {
                t.spawn(text(f, sub, wow_ui::NOTE_SIZE, DIM));
            }
        });
    });
}

/// Traits as chips: each one's icon and name, wrapping.
fn trait_chips(
    p: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    ids: &[u8],
    dim: bool,
) {
    if ids.is_empty() {
        return;
    }
    p.spawn(Node {
        flex_direction: FlexDirection::Row,
        flex_wrap: FlexWrap::Wrap,
        column_gap: u(8.0),
        row_gap: u(3.0),
        margin: UiRect::vertical(u(2.0)),
        ..default()
    })
    .with_children(|w| {
        for &t in ids {
            w.spawn(Node {
                flex_direction: FlexDirection::Row,
                align_items: AlignItems::Center,
                column_gap: u(3.0),
                ..default()
            })
            .with_children(|c| {
                c.spawn(icon_node(
                    assets,
                    &icons::trait_icon(t),
                    18.0,
                    wow_ui::SLOT_EDGE,
                    true,
                ));
                c.spawn(text(
                    f,
                    companion::trait_name(t),
                    wow_ui::SMALL_SIZE,
                    if dim { DIM } else { WHITE },
                ));
            });
        }
    });
}

/// A line's forms in order, each its icon (a ridden one its mount icon) with its numeral and
/// name, arrows between. `current`: the companion's stage (reached ones lit, the current one
/// edged in gold); `clickable`: each shows its form (the Bestiary), `shown` edged.
#[allow(clippy::too_many_arguments)]
fn evolution_path(
    d: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    e: &Entry,
    current: Option<u8>,
    clickable: bool,
    shown: u8,
) {
    d.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::FlexStart,
        column_gap: u(4.0),
        margin: UiRect::vertical(u(3.0)),
        flex_shrink: 0.0,
        ..default()
    })
    .with_children(|row| {
        for (i, s) in e.stages.iter().enumerate() {
            let st = i as u8;
            if i > 0 {
                row.spawn((
                    Node {
                        margin: UiRect::top(u(10.0)),
                        ..default()
                    },
                    children![text(f, "›", 16.0, GOLD)],
                ));
            }
            let reached = current.is_none_or(|c| st <= c);
            let edge = if current == Some(st) || (clickable && st == shown) {
                GOLD
            } else {
                wow_ui::SLOT_EDGE
            };
            let icon = if s.rideable {
                icons::mount_icon(e.family)
            } else {
                icons::family_icon(e.family)
            };
            let mut form = row.spawn(Node {
                flex_direction: FlexDirection::Column,
                align_items: AlignItems::Center,
                width: u(92.0),
                row_gap: u(1.0),
                padding: UiRect::all(u(2.0)),
                ..default()
            });
            if clickable {
                let act = Act::ShowStage(st);
                form.insert((
                    Button,
                    WowButton {
                        selected: st == shown,
                        disabled: false,
                        kind: Kind::Row,
                    },
                    act,
                    act_tip(&s.name, act),
                ));
            }
            form.with_children(|c| {
                if clickable {
                    wow_ui::row_glow(c);
                }
                c.spawn(icon_node(assets, &icon, 32.0, edge, reached));
                c.spawn(text(
                    f,
                    &format!(
                        "{}{}",
                        stage_numeral(st),
                        if s.rideable { " · mount" } else { "" }
                    ),
                    wow_ui::NOTE_SIZE,
                    if reached { GOLD } else { GREY },
                ));
                c.spawn((
                    Node {
                        max_width: u(88.0),
                        justify_content: JustifyContent::Center,
                        ..default()
                    },
                    children![text(
                        f,
                        &s.name,
                        wow_ui::NOTE_SIZE,
                        if reached { WHITE } else { GREY }
                    )],
                ));
            });
        }
    });
}

// ── Your companions ──────────────────────────────────────────────────────────────────────────────

/// The companions in the bags as cards: the portrait in its rarity's edge (grey when fainted),
/// the name, the level, the health and experience bars, and what it waits for.
fn party_list(
    l: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    ui: &CompanionUi,
) {
    if ui.carried.is_empty() {
        line(l, f, "No companions in your bags.", 12.0, GREY);
        line(
            l,
            f,
            "Companions drop rarely from creatures of every kind, critters too; rare and elite \
             ones drop them more often. Their items are Bind on Equip: trade them freely.",
            10.0,
            DIM,
        );
        return;
    }
    for c in &ui.carried {
        let fainted =
            c.flags & CARRIED_FAINTED != 0 && c.flags & (CARRIED_OUT | CARRIED_RIDING) == 0;
        let rarity = RARITIES[usize::from(c.rarity.min(4))];
        let name = ui.carried_name(c);
        let (badge, badge_color) = card_badge(c);
        let (health, health_color) = health_of(c);
        let (xp, _) = xp_progress(c);
        let flashed = ui.flash.is_some_and(|(item, _)| item == c.item);
        let urgent = c.pending >= 0 || c.flags & CARRIED_CAN_EVOLVE != 0;
        let stages = ui.index.get(&c.species).map(|e| e.stages.len());
        l.spawn((
            Button,
            WowButton {
                selected: ui.item == Some(c.item) || flashed,
                disabled: false,
                kind: Kind::Row,
            },
            Act::Item(c.item),
            wow_ui::WowTip::new(
                &name,
                &format!("{rarity} companion. {}.", carried_status(c, stages)),
            ),
            Node {
                width: u(LIST_W - 24.0),
                height: u(CARD_H),
                padding: UiRect::axes(u(3.0), u(2.0)),
                column_gap: u(6.0),
                align_items: AlignItems::Center,
                flex_shrink: 0.0,
                margin: UiRect::bottom(u(2.0)),
                ..default()
            },
        ))
        .with_children(|r| {
            wow_ui::row_glow(r);
            r.spawn(icon_node(
                assets,
                &icons::family_icon(ui.family_of(c.species)),
                36.0,
                rarity_color(c.rarity),
                !fainted,
            ));
            r.spawn(Node {
                flex_direction: FlexDirection::Column,
                flex_grow: 1.0,
                min_width: u(0.0),
                overflow: Overflow::clip(),
                ..default()
            })
            .with_children(|t| {
                t.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    justify_content: JustifyContent::SpaceBetween,
                    column_gap: u(4.0),
                    ..default()
                })
                .with_children(|n| {
                    n.spawn((
                        Node {
                            min_width: u(0.0),
                            flex_shrink: 1.0,
                            overflow: Overflow::clip(),
                            ..default()
                        },
                        children![text(
                            f,
                            &name,
                            wow_ui::SMALL_SIZE,
                            if fainted {
                                GREY
                            } else {
                                rarity_color(c.rarity)
                            }
                        )],
                    ));
                    n.spawn(text(
                        f,
                        &format!("Lv {}", c.level),
                        wow_ui::SMALL_SIZE,
                        WHITE,
                    ));
                });
                bar(t, f, health, "", health_color, 5.0);
                bar(t, f, xp, "", XP_PURPLE, 3.0);
                t.spawn(text(f, &badge, wow_ui::NOTE_SIZE, badge_color));
            });
            if urgent {
                let mark = if c.flags & CARRIED_CAN_EVOLVE != 0 {
                    icons::EVOLVE
                } else {
                    icons::LEARN
                };
                r.spawn(icon_node(assets, &icons::path(mark), 16.0, GOLD, true));
            } else if fainted {
                r.spawn(icon_node(
                    assets,
                    &icons::path(icons::REVIVE),
                    16.0,
                    RED,
                    false,
                ));
            }
        });
    }
}

/// A companion's page: the model and what it is, its bars, what can be done, its moves, its
/// traits, its evolution path; at a stable master its care and breeding; a game master's tools.
fn companion_page(
    d: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    ui: &CompanionUi,
    duel: &companion::CompanionState,
    spells: Spells,
) {
    let Some(c) = ui.selected() else {
        line(
            d,
            f,
            "Choose a companion on the left. Use its item from your bags to summon it; its abilities are on the Companion bar (C).",
            12.0,
            DIM,
        );
        line(
            d,
            f,
            "With it out, Ctrl+1 to Ctrl+0 work its Companion bar with your gun up: Ctrl+1 sends it at \
             what you aim at (the ground: hold there; twice: follow), Ctrl+4 to Ctrl+6 its abilities.",
            11.0,
            DIM,
        );
        if ui.npc && !ui.at_stable() {
            line(d, f, "You are too far from the stable master.", 11.0, RED);
        }
        stable_care(d, f, assets, ui);
        return;
    };
    let entry = ui.index.get(&c.species);
    let looks = ui.looks.get(&c.species);
    let family = ui.family_of(c.species);
    let stages = entry.map_or(1, |e| e.stages.len().max(1)) as u8;
    let out = c.flags & CARRIED_OUT != 0;
    let riding = c.flags & CARRIED_RIDING != 0;
    let fainted = !out && !riding && c.flags & CARRIED_FAINTED != 0;
    let rarity = RARITIES[usize::from(c.rarity.min(4))];
    model_header(d, f, assets, &icons::family_icon(family), |info| {
        line(
            info,
            f,
            &ui.carried_name(c),
            wow_ui::TITLE_SIZE + 2.0,
            rarity_color(c.rarity),
        );
        icon_line(
            info,
            f,
            assets,
            &icons::family_icon(family),
            &format!(
                "{}  ·  {}  ·  {}",
                companion::species_name(c.species),
                family_name(family),
                entry.map_or("", |e| role_name(e.role))
            ),
            DIM,
        );
        line(
            info,
            f,
            &format!(
                "Level {}  ·  Stage {} of {}  ·  {rarity}{}",
                c.level,
                stage_numeral(c.stage),
                stages.max(c.stage + 1),
                if riding {
                    "  ·  ridden"
                } else if out {
                    "  ·  out"
                } else {
                    ""
                }
            ),
            wow_ui::SMALL_SIZE,
            WHITE,
        );
        let (health, health_color) = health_of(c);
        let health_words = if fainted {
            format!("Fainted: comes round in {}", wait_text(c.faint_secs))
        } else {
            format!("Health {}%", (health * 100.0).round() as u32)
        };
        bar(info, f, health, &health_words, health_color, 14.0);
        let (share, words) = xp_progress(c);
        bar(info, f, share, &words, XP_PURPLE, 14.0);
        if c.flags & CARRIED_CAPPED != 0 {
            line(
                info,
                f,
                "Held at your level: its full bar is paid when you level.",
                wow_ui::NOTE_SIZE,
                GOLD,
            );
        }
        if c.flags & CARRIED_FINAL == 0 && c.next_evolve_level > 0 {
            let share =
                f32::from(c.level.min(c.next_evolve_level)) / f32::from(c.next_evolve_level);
            let ready = c.flags & CARRIED_CAN_EVOLVE != 0;
            bar(
                info,
                f,
                if ready { 1.0 } else { share },
                &if ready {
                    "Ready to evolve!".to_string()
                } else {
                    format!(
                        "Level {} of {} for its next form",
                        c.level, c.next_evolve_level
                    )
                },
                if ready {
                    GOLD.with_alpha(0.8)
                } else {
                    EVOLVE_GREEN
                },
                14.0,
            );
        }
        if c.generation > 0 || c.breed_count > 0 {
            let mut s = format!("Generation {}, bred {} of 3", c.generation, c.breed_count);
            if c.breed_rest_secs > 0 {
                s.push_str(&format!(", resting {} h", c.breed_rest_secs.div_ceil(3600)));
            }
            line(info, f, &s, wow_ui::NOTE_SIZE, DIM);
        }
    });

    // What can be done with it now.
    buttons(d, |b| {
        action(
            b,
            f,
            assets,
            &icons::path(if out { icons::DISMISS } else { icons::SUMMON }),
            if out { "Dismiss" } else { "Summon" },
            !fainted && !riding,
            false,
            Act::Summon(c.item),
        );
        if c.flags & CARRIED_RIDEABLE != 0 {
            action(
                b,
                f,
                assets,
                &icons::mount_icon(family),
                if riding { "Dismount" } else { "Ride" },
                out || riding,
                riding,
                Act::Ride,
            );
        }
        action(
            b,
            f,
            assets,
            &icons::path(icons::LEARN),
            "Deck",
            true,
            false,
            Act::Deck(c.item),
        );
        if c.flags & CARRIED_FINAL == 0 {
            action(
                b,
                f,
                assets,
                &icons::path(icons::EVOLVE),
                "Evolve",
                c.flags & CARRIED_CAN_EVOLVE != 0,
                c.flags & CARRIED_CAN_EVOLVE != 0,
                Act::Evolve(c.item),
            );
        }
        if fainted {
            action(
                b,
                f,
                assets,
                &icons::path(icons::DRAUGHT),
                &format!("Use Reviving Draught ({})", ui.care.draughts),
                ui.care.draughts > 0,
                false,
                Act::UseDraught(c.item),
            );
        }
        if out {
            let live = matches!(
                duel.duel.phase,
                DuelPhase::Challenged | DuelPhase::Countdown | DuelPhase::Fighting
            );
            if live {
                b.spawn(button(f, "Forfeit Duel", true, false, Act::Forfeit));
            } else {
                b.spawn(button(f, "Duel Target", true, false, Act::Challenge));
            }
        }
    });
    if fainted {
        line(
            d,
            f,
            "Revive it now at a stable master, or with a Reviving Draught (general goods \
             vendors and stable masters sell them).",
            wow_ui::NOTE_SIZE,
            DIM,
        );
    }
    stable_care(d, f, assets, ui);

    // Its moves: the choice per stage reached; an owed one is chosen here, anywhere; a stable
    // master changes one already made.
    heading(d, f, "Moves");
    if let Some(e) = entry {
        if !e.trait_name.is_empty() {
            icon_line(
                d,
                f,
                assets,
                &icons::path(icons::LEARN),
                &format!("Family trait: {}", e.trait_name),
                GREEN,
            );
        }
        for (i, s) in e.stages.iter().enumerate() {
            let st = i as u8;
            if st > c.stage {
                break;
            }
            let picked = c.picks.get(i).copied().unwrap_or(0);
            let owed = c.pending == st as i8;
            let spell = |o: usize| {
                looks
                    .and_then(|l| l.stages.get(i))
                    .map_or(0, |l| l.spells[o])
            };
            if owed {
                icon_line(
                    d,
                    f,
                    assets,
                    &icons::path(icons::LEARN),
                    &format!(
                        "Stage {}: a new move to learn! Choose one:",
                        stage_numeral(st)
                    ),
                    GOLD,
                );
            }
            for option in 1..=2u8 {
                let o = usize::from(option - 1);
                let name = s.abilities[o].as_str();
                if name.is_empty() {
                    continue;
                }
                let chosen = picked == option;
                if !owed && !chosen && !(picked != 0 && (ui.at_stable() || ui.gm)) {
                    continue;
                }
                d.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    align_items: AlignItems::Center,
                    justify_content: JustifyContent::SpaceBetween,
                    column_gap: u(6.0),
                    flex_shrink: 0.0,
                    ..default()
                })
                .with_children(|r| {
                    move_row(
                        r,
                        f,
                        assets,
                        &icons::move_icon(spell(o), s.kinds[o], spells),
                        name,
                        &format!(
                            "Stage {} · {}, {} s cooldown",
                            stage_numeral(st),
                            kind_name(s.kinds[o]),
                            s.cooldowns[o]
                        ),
                        if chosen || owed { WHITE } else { GREY },
                        chosen || owed,
                    );
                    if owed {
                        r.spawn(button(
                            f,
                            "Learn",
                            true,
                            false,
                            Act::Pick {
                                item: c.item,
                                stage: st,
                                option,
                            },
                        ));
                    } else if !chosen {
                        let act = Act::Repick {
                            item: c.item,
                            stage: st,
                            option,
                        };
                        let armed = ui.armed == Some(act);
                        r.spawn(button(
                            f,
                            if armed {
                                "Click Again (50s)"
                            } else {
                                "Change (50s)"
                            },
                            true,
                            armed,
                            act,
                        ));
                    }
                });
            }
            if picked == 0 && !owed {
                line(
                    d,
                    f,
                    &format!("Stage {}: not chosen", stage_numeral(st)),
                    11.0,
                    GOLD,
                );
            }
        }
        if !e.mounted.is_empty() && c.flags & CARRIED_RIDEABLE != 0 {
            move_row(
                d,
                f,
                assets,
                &icons::move_icon(looks.map_or(0, |l| l.mounted_spell), 15, spells),
                &e.mounted,
                "while ridden",
                WHITE,
                true,
            );
        }
    }

    // Its traits.
    heading(d, f, "Traits");
    let ids = trait_ids(c.traits);
    let mine: Vec<u8> = ids.iter().copied().filter(|&t| t != 0).collect();
    if mine.is_empty() {
        line(
            d,
            f,
            "Common: no traits.",
            wow_ui::SMALL_SIZE,
            rarity_color(c.rarity),
        );
    } else {
        line(d, f, rarity, wow_ui::SMALL_SIZE, rarity_color(c.rarity));
        trait_chips(d, f, assets, &mine, false);
    }
    let combos = companion::combo_line(&ids);
    if !combos.is_empty() {
        line(d, f, &combos, wow_ui::SMALL_SIZE, GOLD);
    }

    // Its evolution path, and what the next form needs.
    heading(d, f, "Evolution");
    if let Some(e) = entry {
        evolution_path(d, f, assets, e, Some(c.stage), false, c.stage);
    }
    if c.flags & CARRIED_FINAL == 0 {
        let ready = c.flags & CARRIED_CAN_EVOLVE != 0;
        line(
            d,
            f,
            &if ready {
                "Ready to evolve! Summon it and press Evolve.".to_string()
            } else {
                evolve_text(c)
            },
            wow_ui::SMALL_SIZE,
            if ready { GREEN } else { DIM },
        );
        if let Some(next) = next_form_line(c, entry) {
            line(
                d,
                f,
                &next,
                wow_ui::SMALL_SIZE,
                if ready { GREEN } else { WHITE },
            );
        }
    } else {
        line(d, f, "Fully grown.", wow_ui::SMALL_SIZE, DIM);
    }
    if c.flags & CARRIED_RIDEABLE != 0 {
        line(
            d,
            f,
            "A mount: Ride above, or its Companion bar's mount button.",
            wow_ui::NOTE_SIZE,
            DIM,
        );
    }
    if let Some(l) = looks.filter(|l| !l.found.is_empty()) {
        line(d, f, &l.found, wow_ui::NOTE_SIZE, DIM);
    }

    breeding(d, f, assets, ui, c);
    if ui.gm {
        gm_edit(d, f, ui, c, entry, stages);
    }
}

/// Breeding: two fully grown companions of a three-stage line, at a stable master.
fn breeding(
    d: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    ui: &CompanionUi,
    c: &Carried,
) {
    let adult = |x: &Carried| {
        x.flags & CARRIED_FINAL != 0 && ui.index.get(&x.species).is_none_or(|e| e.stages.len() >= 3)
    };
    if !adult(c) {
        return;
    }
    if !ui.at_stable() && !ui.gm {
        line(
            d,
            f,
            "A stable master breeds fully grown companions.",
            wow_ui::NOTE_SIZE,
            DIM,
        );
        return;
    }
    heading(d, f, "Breeding");
    let partners: Vec<&Carried> = ui
        .carried
        .iter()
        .filter(|x| x.item != c.item && adult(x))
        .collect();
    if partners.is_empty() {
        line(
            d,
            f,
            "No other fully grown companion in your bags.",
            11.0,
            DIM,
        );
        return;
    }
    line(
        d,
        f,
        "Each brings 5 of its family's breeding reagent (Forager companions find them); \
         the young one comes at level 1 and each parent rests 3 days after.",
        wow_ui::NOTE_SIZE,
        DIM,
    );
    buttons(d, |b| {
        for p in &partners {
            let act = Act::Breed {
                a: c.item,
                b: p.item,
            };
            let armed = ui.armed == Some(act);
            let fee = coins_text(breed_fee(c.level, p.level));
            action(
                b,
                f,
                assets,
                &icons::path(icons::BREED),
                &if armed {
                    format!("Click again to breed ({fee})")
                } else {
                    format!("Breed with {} ({}) · {fee}", ui.carried_name(p), p.level)
                },
                breed_block(c, p).is_none(),
                armed,
                act,
            );
        }
    });
    // Why a partner's button is grey.
    for p in &partners {
        if let Some(why) = breed_block(c, p) {
            line(
                d,
                f,
                &format!("With {}: {why}.", ui.carried_name(p)),
                10.0,
                GREY,
            );
        }
    }
}

/// At a stable master: revive and heal every companion, and buy a Reviving Draught.
fn stable_care(
    d: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    ui: &CompanionUi,
) {
    if !ui.at_stable() {
        return;
    }
    heading(d, f, "Stable Master");
    let fainted = ui
        .carried
        .iter()
        .filter(|c| c.flags & CARRIED_FAINTED != 0)
        .count();
    buttons(d, |b| {
        let label = match (fainted, ui.care.revive_cost) {
            (0, _) => "Heal My Companions".to_string(),
            (_, 0) => "Revive and Heal My Companions".to_string(),
            (_, cost) => format!("Revive and Heal My Companions ({})", coins_text(cost)),
        };
        action(
            b,
            f,
            assets,
            &icons::path(icons::REVIVE),
            &label,
            ui.care.needs_care > 0,
            false,
            Act::StableRevive,
        );
        action(
            b,
            f,
            assets,
            &icons::path(icons::DRAUGHT),
            &format!("Buy Reviving Draught ({})", coins_text(DRAUGHT_PRICE)),
            true,
            false,
            Act::BuyDraught,
        );
    });
    line(
        d,
        f,
        &if ui.care.needs_care == 0 {
            format!(
                "Your companions are all well. Reviving Draughts carried: {}.",
                ui.care.draughts
            )
        } else {
            format!(
                "{fainted} fainted, {} hurt. Reviving Draughts carried: {}.",
                usize::from(ui.care.needs_care).saturating_sub(fainted),
                ui.care.draughts
            )
        },
        wow_ui::NOTE_SIZE,
        DIM,
    );
}

/// A game master's changes to a carried companion: level, stage, abilities, traits.
fn gm_edit(
    d: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    ui: &CompanionUi,
    c: &Carried,
    entry: Option<&Entry>,
    stages: u8,
) {
    let _ = ui;
    heading(d, f, "Game Master");
    let item = c.item;
    let level = |delta: i16| (i16::from(c.level) + delta).clamp(1, 60) as u8;
    buttons(d, |b| {
        b.spawn(text(f, "Level", 12.0, WHITE));
        for delta in [-10i16, -1, 1, 10] {
            let to = level(delta);
            b.spawn(button(
                f,
                &format!("{delta:+}"),
                to != c.level,
                false,
                Act::Edit {
                    item,
                    edit: Edit::Level(to),
                },
            ));
        }
    });
    buttons(d, |b| {
        b.spawn(text(f, "Stage", 12.0, WHITE));
        for st in 0..stages {
            b.spawn(button(
                f,
                &format!("{}", st + 1),
                st != c.stage,
                st == c.stage,
                Act::Edit {
                    item,
                    edit: Edit::Stage(st),
                },
            ));
        }
    });
    if let Some(e) = entry {
        buttons(d, |b| {
            b.spawn(text(f, "Abilities", 12.0, WHITE));
            for (i, s) in e.stages.iter().enumerate().take(usize::from(c.stage) + 1) {
                for option in 1..=2u8 {
                    let name = &s.abilities[usize::from(option - 1)];
                    if name.is_empty() {
                        continue;
                    }
                    let chosen = c.picks[i] == option;
                    b.spawn(button(
                        f,
                        name,
                        !chosen,
                        chosen,
                        Act::Edit {
                            item,
                            edit: Edit::Pick {
                                stage: i as u8,
                                option,
                            },
                        },
                    ));
                }
            }
        });
        buttons(d, |b| {
            b.spawn(text(f, "Traits", 12.0, WHITE));
            for t in 1..=30u8 {
                let has = c.traits & (1 << (t - 1)) != 0;
                if e.eligible & (1 << (t - 1)) == 0 && !has {
                    continue;
                }
                b.spawn(button(
                    f,
                    companion::trait_name(t),
                    true,
                    has,
                    Act::Edit {
                        item,
                        edit: Edit::Trait(t),
                    },
                ));
            }
            b.spawn(button(
                f,
                "Clear Traits",
                c.traits != 0,
                false,
                Act::Edit {
                    item,
                    edit: Edit::ClearTraits,
                },
            ));
        });
    }
    // Twin-souled doubles one stage's abilities.
    if c.traits & (1 << 28) != 0 {
        buttons(d, |b| {
            b.spawn(text(f, "Twin-souled stage", 12.0, WHITE));
            for st in 1..=stages {
                b.spawn(button(
                    f,
                    &format!("{st}"),
                    st != c.twin_stage,
                    st == c.twin_stage,
                    Act::Edit {
                        item,
                        edit: Edit::Twin(st),
                    },
                ));
            }
        });
    }
}

// ── Acting ───────────────────────────────────────────────────────────────────────────────────────

/// Whether a press on `act` goes now: breeding and retraining cost gold and can't be taken back,
/// so the first press only arms them (the button then says "Click again").
fn press_goes(armed: &mut Option<Act>, act: Act) -> bool {
    let costly = matches!(act, Act::Breed { .. } | Act::Repick { .. });
    if !costly || *armed == Some(act) {
        *armed = None;
        return true;
    }
    *armed = Some(act);
    false
}

/// How far one press of an arrow turns the model, radians.
const TURN_STEP: f32 = 0.5;

#[allow(clippy::too_many_arguments)]
fn click(
    time: Res<Time>,
    mut ui: ResMut<CompanionUi>,
    mut duel: ResMut<companion::CompanionState>,
    aim: Res<Aim>,
    buttons: Query<(&Interaction, &Act, &WowButton), Changed<Interaction>>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    for (i, act, b) in &buttons {
        if *i != Interaction::Pressed || b.disabled {
            continue;
        }
        if !press_goes(&mut ui.armed, *act) {
            ui.dirty = true;
            continue;
        }
        let window = |r: Request| encode_request(r);
        let bar = |r: BarRequest| companion::encode_request(r);
        let body = match *act {
            Act::Tab(t) => {
                ui.tab = t;
                ui.dirty = true;
                None
            }
            Act::Species(id) => {
                if ui.species != Some(id) {
                    ui.stage_shown = 0;
                }
                ui.species = Some(id);
                ui.give_stage = 0;
                ui.dirty = true;
                None
            }
            Act::ShowStage(st) => {
                ui.stage_shown = st;
                ui.dirty = true;
                None
            }
            Act::Item(id) => {
                ui.item = Some(id);
                ui.dirty = true;
                None
            }
            Act::Turn(d) => {
                ui.yaw += f32::from(d) * TURN_STEP;
                None
            }
            Act::Summon(item) => Some(bar(BarRequest::Summon(item))),
            Act::Deck(item) => {
                out.write(SendFusionChannel(ChannelMessage {
                    channel: crate::companion_battle::CHANNEL,
                    body: crate::companion_battle::encode(&crate::companion_battle::Action::AskDeck(item)),
                }));
                None
            }
            Act::Ride => Some(bar(BarRequest::Ride)),
            Act::Evolve(item) => Some(bar(BarRequest::Evolve(item))),
            Act::Pick {
                item,
                stage,
                option,
            } => Some(bar(BarRequest::Pick {
                item,
                stage,
                option,
            })),
            Act::Repick {
                item,
                stage,
                option,
            } => Some(window(Request::Repick {
                item,
                stage,
                option,
            })),
            Act::Breed { a, b } => Some(window(Request::Breed { a, b })),
            Act::Challenge => {
                // The player under the crosshair, else the target the server knows.
                let target = aim
                    .target
                    .as_ref()
                    .filter(|t| t.player)
                    .map_or(0, |t| t.guid);
                // The server's "challenged" for both sides that follows is ours to wait on.
                duel.challenge_sent_at = Some(time.elapsed_secs());
                Some(bar(BarRequest::DuelChallenge(target)))
            }
            Act::Forfeit => Some(bar(BarRequest::DuelForfeit)),
            Act::GiveStage(d) => {
                ui.give_stage = (i16::from(ui.give_stage) + i16::from(d)).clamp(0, 2) as u8;
                ui.dirty = true;
                None
            }
            Act::GiveLevel(d) => {
                ui.give_level = (i16::from(ui.give_level) + i16::from(d)).clamp(1, 60) as u8;
                ui.dirty = true;
                None
            }
            Act::Give => ui.species.map(|species| {
                let stages = ui.index.get(&species).map_or(1, |e| e.stages.len().max(1)) as u8;
                window(Request::GmGive {
                    species,
                    stage: ui.give_stage.min(stages - 1),
                    level: ui.give_level,
                })
            }),
            Act::Edit { item, edit } => Some(window(Request::GmEdit { item, edit })),
            Act::StableRevive => Some(window(Request::StableRevive)),
            Act::UseDraught(item) => Some(window(Request::UseDraught(item))),
            Act::BuyDraught => Some(window(Request::BuyDraught)),
        };
        if let Some(body) = body {
            send(&mut out, body);
            // The list follows what the server did.
            ui.asked_at = f32::MIN;
        }
    }
}

/// Radians the model turns per logical pixel dragged across it.
const DRAG_TURN: f32 = 0.012;

/// A drag across the model's frame turns it, as the stock model panes' arrows do.
fn turn_model(
    mut ui: ResMut<CompanionUi>,
    mouse: Res<ButtonInput<MouseButton>>,
    motion: Option<Res<AccumulatedMouseMotion>>,
    frames: Query<&Interaction, With<ModelTurn>>,
    mut held: Local<bool>,
) {
    if !mouse.pressed(MouseButton::Left) {
        *held = false;
        return;
    }
    if mouse.just_pressed(MouseButton::Left) {
        *held = frames.iter().any(|i| *i == Interaction::Pressed);
    }
    if !*held {
        return;
    }
    let dx = motion.map_or(0.0, |m| m.delta.x);
    if dx != 0.0 {
        ui.yaw += dx * DRAG_TURN;
    }
}

/// The model on show: the stable pane is asked for the chosen creature's display at the window's
/// facing, and its picture goes in the frame; without a display (none chosen, its looks not come,
/// the window shut) the pane is let go and the frame shows the family's icon.
fn drive_preview(
    ui: Res<CompanionUi>,
    preview: Option<ResMut<LayerCreaturePreview>>,
    image: Option<Res<LayerCreaturePreviewImage>>,
    mut panes: Query<(&mut ImageNode, &mut Visibility), With<ModelPane>>,
    mut stand_ins: Query<&mut Visibility, (With<ModelStandIn>, Without<ModelPane>)>,
) {
    let display = ui.preview_display();
    if let Some(mut p) = preview {
        if p.display_id != display {
            p.display_id = display;
        }
        if display.is_some() && p.yaw != ui.yaw {
            p.yaw = ui.yaw;
        }
    }
    let picture = image
        .and_then(|i| i.0.clone())
        .filter(|_| display.is_some());
    let shown = picture.is_some();
    for (mut node, mut vis) in &mut panes {
        if let Some(h) = &picture
            && node.image != *h
        {
            node.image = h.clone();
        }
        let want = if shown {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *vis != want {
            *vis = want;
        }
    }
    for mut vis in &mut stand_ins {
        let want = if shown {
            Visibility::Hidden
        } else {
            Visibility::Inherited
        };
        if *vis != want {
            *vis = want;
        }
    }
}

/// Seconds between asks for the carried list while the window is open (experience, health,
/// the evolve button's place and hour).
const POLL_SECS: f32 = 3.0;

fn poll(time: Res<Time>, mut ui: ResMut<CompanionUi>, mut out: MessageWriter<SendFusionChannel>) {
    if !ui.open {
        return;
    }
    let now = time.elapsed_secs();
    if ui.asked_at == f32::MIN {
        // Just acted: ask a moment later, once the server has done it.
        ui.asked_at = now - POLL_SECS + 0.3;
    }
    if now - ui.asked_at >= POLL_SECS {
        ui.asked_at = now;
        send(&mut out, encode_request(Request::Carried));
    }
    if ui.flash.is_some_and(|(_, until)| now > until) {
        ui.flash = None;
        ui.dirty = true;
    }
}

/// The UI error frame's colour for a kind: the game's red refusal, yellow news, light blue success.
pub fn notice_color(kind: NoticeKind) -> (f32, f32, f32) {
    match kind {
        NoticeKind::Error => (1.0, 0.1, 0.1),
        NoticeKind::Info => (1.0, 0.82, 0.0),
        NoticeKind::Good => (0.44, 0.84, 1.0),
    }
}

fn show_notices(mut notices: ResMut<Notices>, script: Option<NonSend<UiScript>>) {
    if notices.0.is_empty() {
        return;
    }
    let Some(script) = script else {
        notices.0.clear();
        return;
    };
    for (kind, t) in notices.0.drain(..) {
        let (r, g, b) = notice_color(kind);
        crate::lua::run(
            &script,
            &format!(
                "if UIErrorsFrame then UIErrorsFrame:AddMessage({}, {r}, {g}, {b}, 1.0, UIERRORS_HOLD_TIME) end",
                crate::lua::quote(&t)
            ),
        );
    }
}

/// How long a cheered companion's card stays lit, seconds.
const FLASH_SECS: f32 = 4.0;

/// A level or an evolution: the install's level-up chime, an evolution's banner, and the
/// companion's card lit for a moment.
fn cheer(
    time: Res<Time>,
    mut cheers: ResMut<Cheers>,
    mut ui: ResMut<CompanionUi>,
    script: Option<NonSend<UiScript>>,
    mut banners: MessageWriter<crate::streaks::Banner>,
) {
    if cheers.0.is_empty() {
        return;
    }
    for c in cheers.0.drain(..) {
        if let Some(script) = script.as_deref() {
            crate::lua::run(
                script,
                &format!("PlaySoundFile({})", crate::lua::quote(cheer_sound(&c))),
            );
        }
        let item = match c {
            Cheer::Level { item, .. } => item,
            Cheer::Evolved {
                item,
                species,
                stage,
            } => {
                banners.write(evolve_banner(species, stage));
                item
            }
        };
        ui.flash = Some((item, time.elapsed_secs() + FLASH_SECS));
        ui.dirty = true;
    }
}

// ── The duel prompt ──────────────────────────────────────────────────────────────────────────────

#[derive(Component)]
struct DuelRoot;
#[derive(Component, Clone, Copy, PartialEq, Eq)]
enum DuelAct {
    Accept,
    Decline,
}

/// A challenge to the player's companion: the stock duel request's popup, Accept or Decline.
fn duel_prompt(
    mut commands: Commands,
    assets: Res<AssetServer>,
    duel: Res<companion::CompanionState>,
    roots: Query<Entity, With<DuelRoot>>,
    acts: Query<(&Interaction, &DuelAct), Changed<Interaction>>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    let want = duel.duel.phase == DuelPhase::Challenged && !duel.challenger;
    for (i, a) in &acts {
        if *i == Interaction::Pressed {
            send(
                &mut out,
                companion::encode_request(BarRequest::DuelAnswer(*a == DuelAct::Accept)),
            );
        }
    }
    let shown = !roots.is_empty();
    if want == shown {
        return;
    }
    if !want {
        for e in &roots {
            commands.entity(e).despawn();
        }
        return;
    }
    let font: Handle<Font> = assets.load(FONT);
    wow_ui::spawn_popup(
        &mut commands,
        &font,
        "companion duel",
        "Your companion is challenged to a duel.",
        DuelRoot,
        ("Accept", DuelAct::Accept),
        ("Decline", DuelAct::Decline),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The C++ `EncodeIndex` for one known and one unknown entry.
    fn index_body() -> Vec<u8> {
        let mut b = vec![OP_INDEX, INDEX_FIRST | INDEX_LAST, 2];
        // Unknown: id 7, family Cat (4), seen 0.
        b.extend(7u16.to_le_bytes());
        b.extend([4, 0]);
        // Known: id 106, Cat, caught.
        b.extend(106u16.to_le_bytes());
        b.extend([4, 2, 0]);
        b.extend(91_106u32.to_le_bytes());
        let s8 = |b: &mut Vec<u8>, s: &str| {
            b.push(s.len() as u8);
            b.extend(s.as_bytes());
        };
        s8(&mut b, "Stranglethorn Kitten");
        s8(&mut b, "Prowl");
        s8(&mut b, "Pounce");
        b.extend((1u32 << 22).to_le_bytes());
        b.push(3);
        for (c, n) in [
            (681u32, "Young Stranglethorn Tiger"),
            (682, "Stranglethorn Tiger"),
            (1085, "Elder Stranglethorn Tiger"),
        ] {
            b.extend(c.to_le_bytes());
            s8(&mut b, n);
            s8(&mut b, "Level 20");
            b.push(u8::from(c == 1085));
            for a in ["Claw", "Rake"] {
                s8(&mut b, a);
                b.extend([0, 6]);
            }
        }
        b
    }

    #[test]
    fn the_index_reads_as_the_server_writes_it() {
        let (flags, entries) = decode_index(&index_body()).unwrap();
        assert_eq!(flags, INDEX_FIRST | INDEX_LAST);
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0].seen, Seen::Unknown);
        assert!(entries[0].name.is_empty() && entries[0].stages.is_empty());
        assert_eq!(family_name(entries[0].family), "Cat");
        let e = &entries[1];
        assert_eq!((e.id, e.seen, e.item), (106, Seen::Captured, 91_106));
        assert_eq!(e.stages.len(), 3);
        assert!(e.stages[2].rideable && !e.stages[0].rideable);
        assert_eq!(e.stages[1].abilities[1], "Rake");
        let mut cut = index_body();
        cut.pop();
        assert_eq!(decode_index(&cut), None);
        cut.extend([0, 0]);
        assert_eq!(decode_index(&cut), None, "a byte too many");
    }

    #[test]
    fn a_companion_item_is_named_after_its_current_form() {
        let mut ui = CompanionUi::default();
        assert!(ui.apply(&index_body()));
        assert_eq!(ui.index.len(), 2);
        // Level 43, stage 3, species 106: the elder tiger, not the kitten's carrier.
        let seed = 43 | 2 << 6 | 106 << 8;
        assert_eq!(
            item_name(91_106, seed, "Stranglethorn Kitten Carrier").as_deref(),
            Some("Elder Stranglethorn Tiger")
        );
        // At its first stage the item keeps its own name; so does one that already names its
        // form (a mount's item), and any item that is no companion's.
        assert_eq!(
            item_name(91_106, 43 | 106 << 8, "Stranglethorn Kitten Carrier"),
            None
        );
        assert_eq!(evolved_name("Horn of the War Wolf", 2, "War Wolf"), None);
        assert_eq!(item_name(12_345, seed, "Linen Cloth"), None);
        // A species this character has not met: its item keeps its name.
        assert_eq!(
            item_name(90_007, 43 | 2 << 6 | 7 << 8, "Nightsaber Cub Carrier"),
            None
        );
    }

    #[test]
    fn carried_and_notices_read_as_the_server_writes_them() {
        // The C++ test's companion: 48 bytes after the 3-byte header, then the 6 of the care.
        let mut b = vec![OP_CARRIED, CARRIED_GM | CARRIED_AT_STABLE, 1];
        b.extend(0x0102_0304_0506_0708u64.to_le_bytes());
        b.extend(106u16.to_le_bytes());
        b.extend([2, 43]);
        b.extend(1234u32.to_le_bytes());
        b.extend(9999u32.to_le_bytes());
        b.extend(2u32.to_le_bytes());
        b.extend([
            1,
            0,
            1,
            2,
            0,
            2,
            CARRIED_FAINTED | CARRIED_RIDEABLE,
            1,
            0,
            1,
        ]);
        b.extend(3600u32.to_le_bytes());
        b.push(2);
        b.extend(3120u32.to_le_bytes());
        b.push(0);
        b.extend(0x8bad_f00du32.to_le_bytes());
        b.push(3);
        b.extend(430u32.to_le_bytes());
        b.push(2);
        assert_eq!(b.len(), 3 + 48 + 6);
        let (header, list, care) = decode_carried(&b).unwrap();
        assert_eq!(header, CARRIED_GM | CARRIED_AT_STABLE);
        let c = list[0];
        assert_eq!((c.species, c.stage, c.level, c.pending), (106, 2, 43, 2));
        assert_eq!(
            (c.picks, c.breed_rest_secs, c.generation),
            ([1, 2, 0], 3600, 2)
        );
        assert_eq!((c.faint_secs, c.hurt_pct, c.seed), (3120, 100, 0x8bad_f00d));
        assert_eq!(
            care,
            Care {
                draughts: 3,
                revive_cost: 430,
                needs_care: 2
            }
        );
        let mut cut = b.clone();
        cut.pop();
        assert!(decode_carried(&cut).is_none(), "the care cut short");
        let mut ui = CompanionUi {
            npc: true,
            ..CompanionUi::default()
        };
        ui.apply(&b);
        assert!(ui.at_stable() && ui.gm);
        ui.npc = false;
        assert!(!ui.at_stable(), "the limited view, even near one");
        assert_eq!(ui.carried_name(&c), "Stranglethorn Kitten");
        ui.apply(&index_body());
        assert_eq!(ui.carried_name(&c), "Elder Stranglethorn Tiger");

        let mut n = vec![OP_NOTICE, 2];
        n.extend(5u16.to_le_bytes());
        n.extend(b"Hello");
        assert_eq!(decode_notice(&n), Some((NoticeKind::Good, "Hello".into())));
        assert_eq!(decode_open(&[OP_OPEN, 1]), Some(true));
        assert_eq!(decode_open(&[OP_OPEN, 0]), Some(false));
    }

    /// `index_requests_decode_strictly` in CompanionIndexTests.cpp.
    #[test]
    fn requests_match_the_server() {
        assert_eq!(
            encode_request(Request::GmGive {
                species: 164,
                stage: 2,
                level: 60
            }),
            vec![OP_GM_GIVE, 164, 0, 2, 60]
        );
        let edit = encode_request(Request::GmEdit {
            item: 77,
            edit: Edit::Trait(28),
        });
        assert_eq!(edit.len(), 12);
        assert_eq!(&edit[9..], &[4, 28, 0]);
        assert_eq!(encode_request(Request::Breed { a: 1, b: 2 }).len(), 17);
        assert_eq!(
            encode_request(Request::Repick {
                item: 5,
                stage: 1,
                option: 2
            }),
            vec![OP_REPICK, 5, 0, 0, 0, 0, 0, 0, 0, 1, 2]
        );
        assert_eq!(encode_request(Request::Index), vec![OP_INDEX_REQUEST]);
        assert_eq!(encode_request(Request::Carried), vec![OP_CARRIED_REQUEST]);
        assert_eq!(encode_request(Request::StableRevive), vec![26]);
        assert_eq!(encode_request(Request::BuyDraught), vec![28]);
        assert_eq!(
            encode_request(Request::UseDraught(0x4000_0000_0000_0102)),
            vec![27, 2, 1, 0, 0, 0, 0, 0, 0x40]
        );
    }

    #[test]
    fn the_window_builds_with_its_tabs_and_both_pages() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        let mut ui = CompanionUi {
            open: true,
            tab: Tab::Bestiary,
            ..CompanionUi::default()
        };
        ui.apply(&index_body());
        ui.species = Some(106);
        world.insert_resource(ui);
        world.init_resource::<companion::CompanionState>();
        world.run_system_once(build_window).unwrap();
        world.run_system_once(refresh).unwrap();
        let mut tabs = world.query::<(&TabMark, &WowButton)>();
        assert_eq!(tabs.iter(&world).count(), 2);
        let mut acts = world.query::<&Act>();
        let species: Vec<u16> = acts
            .iter(&world)
            .filter_map(|a| match a {
                Act::Species(id) => Some(*id),
                _ => None,
            })
            .collect();
        assert!(species.contains(&7) && species.contains(&106));
        // A player sees no game master's controls.
        assert!(!acts.iter(&world).any(|a| matches!(a, Act::Give)));
        world.resource_mut::<CompanionUi>().gm = true;
        world.resource_mut::<CompanionUi>().dirty = true;
        world.run_system_once(refresh).unwrap();
        let mut acts = world.query::<&Act>();
        assert!(acts.iter(&world).any(|a| matches!(a, Act::Give)));
        // Your companions.
        {
            let mut ui = world.resource_mut::<CompanionUi>();
            ui.tab = Tab::Party;
            ui.carried = vec![Carried {
                item: 9,
                species: 106,
                stage: 1,
                level: 30,
                pending: 1,
                picks: [1, 0, 0],
                ..Carried::default()
            }];
            ui.item = Some(9);
            ui.dirty = true;
        }
        world.run_system_once(refresh).unwrap();
        let mut acts = world.query::<&Act>();
        let all: Vec<Act> = acts.iter(&world).copied().collect();
        assert!(all.contains(&Act::Summon(9)));
        assert!(all.contains(&Act::Deck(9)), "the duel deck");
        assert!(all.contains(&Act::Pick {
            item: 9,
            stage: 1,
            option: 2
        }));
        assert!(all.iter().any(|a| matches!(
            a,
            Act::Edit {
                edit: Edit::Stage(2),
                ..
            }
        )));
    }

    #[test]
    fn a_carried_companion_shows_its_progress() {
        let (_, entries) = decode_index(&index_body()).unwrap();
        let tiger = entries.iter().find(|e| e.id == 106).unwrap();
        let mut c = Carried {
            item: 9,
            species: 106,
            stage: 0,
            level: 18,
            xp: 1000,
            xp_to_next: 4000,
            pending: -1,
            block: 5,
            next_evolve_level: 20,
            flags: CARRIED_OUT,
            ..Carried::default()
        };
        assert_eq!(carried_status(&c, Some(3)), "Level 18 · stage 1/3 · out");
        let (share, words) = xp_progress(&c);
        assert!((share - 0.25).abs() < 1e-6);
        assert_eq!(words, "Experience 1000 / 4000 (25%)");
        assert_eq!(
            next_form_line(&c, Some(tiger)).as_deref(),
            Some("Next form, Stranglethorn Tiger: Level 20 (2 levels to go)")
        );
        // Ready, fainted, hurt, owed an ability, capped, grown.
        c.flags = CARRIED_CAN_EVOLVE;
        c.level = 20;
        assert_eq!(
            carried_status(&c, None),
            "Level 20 · stage 1/1 · ready to evolve"
        );
        assert_eq!(
            next_form_line(&c, Some(tiger)).as_deref(),
            Some("Next form, Stranglethorn Tiger: Level 20")
        );
        c.flags = CARRIED_FAINTED;
        c.faint_secs = 3120;
        c.pending = 0;
        assert_eq!(
            carried_status(&c, Some(3)),
            "Level 20 · stage 1/3 · fainted (52 min) · an ability to learn"
        );
        c.flags = 0;
        c.hurt_pct = 57;
        assert_eq!(
            carried_status(&c, Some(3)),
            "Level 20 · stage 1/3 · 43% health · an ability to learn"
        );
        c.hurt_pct = 0;
        // No index entry yet: the server's reason.
        assert_eq!(
            next_form_line(&c, None).as_deref(),
            Some("Evolves at level 20")
        );
        c.flags = CARRIED_FINAL;
        assert_eq!(next_form_line(&c, Some(tiger)), None);
        c.xp_to_next = 0;
        assert_eq!(
            xp_progress(&c),
            (1.0, "Level 20: fully trained".to_string())
        );
    }

    #[test]
    fn notices_keep_the_games_colours() {
        assert_eq!(notice_color(NoticeKind::Error), (1.0, 0.1, 0.1));
        assert_eq!(notice_color(NoticeKind::Info).0, 1.0);
    }

    /// A carried-list message, byte for byte as the server's `EncodeCarried`.
    fn encode_carried_for_test(header: u8, list: &[Carried]) -> Vec<u8> {
        let mut b = vec![OP_CARRIED, header, list.len() as u8];
        for c in list {
            b.extend_from_slice(&c.item.to_le_bytes());
            b.extend_from_slice(&c.species.to_le_bytes());
            b.extend_from_slice(&[c.stage, c.level]);
            b.extend_from_slice(&c.xp.to_le_bytes());
            b.extend_from_slice(&c.xp_to_next.to_le_bytes());
            b.extend_from_slice(&c.traits.to_le_bytes());
            b.extend_from_slice(&[c.rarity, c.twin_stage]);
            b.extend_from_slice(&c.picks);
            b.extend_from_slice(&[
                c.pending as u8,
                c.flags,
                c.block,
                c.next_evolve_level,
                c.breed_count,
            ]);
            b.extend_from_slice(&c.breed_rest_secs.to_le_bytes());
            b.push(c.generation);
            b.extend_from_slice(&c.faint_secs.to_le_bytes());
            b.push(100 - c.hurt_pct);
            b.extend_from_slice(&c.seed.to_le_bytes());
        }
        b.extend_from_slice(&[0, 0, 0, 0, 0, 0]);
        b
    }

    fn carried(item: u64, level: u8, flags: u8, pending: i8) -> Carried {
        Carried {
            item,
            level,
            flags,
            pending,
            ..Carried::default()
        }
    }

    #[test]
    fn the_list_puts_the_one_out_and_the_ready_first() {
        let mut list = vec![
            carried(1, 10, 0, -1),
            carried(2, 40, 0, -1),
            carried(3, 5, CARRIED_CAN_EVOLVE, -1),
            carried(4, 20, CARRIED_OUT, -1),
            carried(5, 8, 0, 1),
        ];
        sort_carried(&mut list);
        let order: Vec<u64> = list.iter().map(|c| c.item).collect();
        assert_eq!(order, vec![4, 5, 3, 2, 1]);
    }

    #[test]
    fn the_fainted_go_last() {
        let mut list = vec![
            carried(1, 60, CARRIED_FAINTED, -1),
            carried(2, 10, 0, -1),
            carried(3, 5, CARRIED_FAINTED, 1),
        ];
        sort_carried(&mut list);
        let order: Vec<u64> = list.iter().map(|c| c.item).collect();
        assert_eq!(order, vec![2, 3, 1]);
    }

    #[test]
    fn a_wait_reads_as_the_server_says_it() {
        assert_eq!(wait_text(3120), "52 min");
        assert_eq!(wait_text(3061), "52 min");
        assert_eq!(wait_text(3600), "1 h");
        assert_eq!(wait_text(3900), "1 h 5 min");
        assert_eq!(wait_text(40), "40 s");
        assert_eq!(wait_text(0), "1 s");
        // A faint's seconds ticking redraw nothing; its minute changing does.
        let mut a = carried(1, 10, CARRIED_FAINTED, -1);
        a.faint_secs = 3120;
        let mut b = a;
        b.faint_secs = 3101;
        assert!(same_view(&a, &b));
        b.faint_secs = 3000;
        assert!(!same_view(&a, &b));
    }

    #[test]
    fn a_fainted_companions_item_says_so_on_its_tooltip() {
        // Level 43 Wolf Pup at stage 3 (the companion.rs test's seed).
        let seed = 43 | 2 << 6 | 6 << 8;
        let other = 12 | 6 << 8;
        let note = companion::item_note_with(90_006, seed, Some(3120)).unwrap();
        assert!(
            note.starts_with("Level 43 Wolf Pup, stage 3 of 3\nFainted: comes round in 52 min")
        );
        assert!(note.ends_with("stable master or with a Reviving Draught"));
        assert_eq!(
            companion::item_note_with(90_006, seed, None).as_deref(),
            Some("Level 43 Wolf Pup, stage 3 of 3")
        );
        // The carried list's map: a fainted one by when it comes round, a whole one None, and a
        // seed shared by a fainted and a whole one left out.
        let now = std::time::Instant::now();
        let map = companion::faint_map([(seed, Some(60)), (other, None)], now);
        assert_eq!(
            map.get(&seed),
            Some(&Some(now + std::time::Duration::from_secs(60)))
        );
        assert_eq!(map.get(&other), Some(&None));
        let map = companion::faint_map([(seed, Some(60)), (seed, None)], now);
        assert!(!map.contains_key(&seed));
        let map = companion::faint_map([(seed, Some(60)), (seed, Some(90))], now);
        assert!(map.get(&seed).is_some_and(|v| v.is_some()));
    }

    #[test]
    fn a_fainted_companion_offers_a_draught_and_the_stable_master_revives() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        let mut ui = CompanionUi {
            open: true,
            npc: true,
            tab: Tab::Party,
            ..CompanionUi::default()
        };
        let mut fainted = carried(9, 30, CARRIED_FAINTED, -1);
        fainted.faint_secs = 600;
        let mut body = encode_carried_for_test(CARRIED_AT_STABLE, &[fainted]);
        let n = body.len();
        body[n - 6] = 2; // two draughts
        body[n - 5..n - 1].copy_from_slice(&300u32.to_le_bytes());
        body[n - 1] = 1;
        ui.apply(&body);
        assert_eq!(ui.care.draughts, 2);
        ui.item = Some(9);
        world.insert_resource(ui);
        world.init_resource::<companion::CompanionState>();
        world.run_system_once(build_window).unwrap();
        world.run_system_once(refresh).unwrap();
        let mut q = world.query::<(&Act, &WowButton)>();
        let all: Vec<(Act, bool)> = q.iter(&world).map(|(a, b)| (*a, b.disabled)).collect();
        assert!(
            all.contains(&(Act::Summon(9), true)),
            "a fainted one can't be summoned"
        );
        assert!(all.contains(&(Act::UseDraught(9), false)));
        assert!(all.contains(&(Act::StableRevive, false)));
        assert!(all.contains(&(Act::BuyDraught, false)));
        // Away from a stable master: no stable buttons; no draught: its button greys.
        {
            let mut ui = world.resource_mut::<CompanionUi>();
            ui.npc = false;
            ui.care.draughts = 0;
            ui.dirty = true;
        }
        world.run_system_once(refresh).unwrap();
        let mut q = world.query::<(&Act, &WowButton)>();
        let all: Vec<(Act, bool)> = q.iter(&world).map(|(a, b)| (*a, b.disabled)).collect();
        assert!(all.contains(&(Act::UseDraught(9), true)));
        assert!(
            !all.iter()
                .any(|(a, _)| matches!(a, Act::StableRevive | Act::BuyDraught))
        );
    }

    #[test]
    fn a_resting_companions_ticking_seconds_redraw_nothing() {
        let mut a = carried(1, 10, 0, -1);
        a.breed_rest_secs = 7200;
        let mut b = a;
        b.breed_rest_secs = 7197;
        assert!(same_view(&a, &b));
        b.breed_rest_secs = 3600; // the hour shown changed
        assert!(!same_view(&a, &b));
        b = a;
        b.level = 11;
        assert!(!same_view(&a, &b));
    }

    #[test]
    fn a_carried_list_like_the_last_does_not_redraw() {
        let mut ui = CompanionUi::default();
        let body = encode_carried_for_test(0, &[carried(1, 10, 0, -1)]);
        assert!(ui.apply(&body));
        assert!(ui.dirty);
        ui.dirty = false;
        assert!(ui.apply(&body));
        assert!(!ui.dirty);
        let body = encode_carried_for_test(0, &[carried(1, 11, 0, -1)]);
        ui.apply(&body);
        assert!(ui.dirty);
    }

    #[test]
    fn breeding_shows_its_fee_and_why_it_is_grey() {
        assert_eq!(breed_fee(60, 40), 50_000);
        assert_eq!(coins_text(breed_fee(60, 40)), "5g");
        let a = carried(1, 60, CARRIED_FINAL, -1);
        let mut b = carried(2, 60, CARRIED_FINAL, -1);
        assert!(breed_block(&a, &b).is_none());
        b.breed_rest_secs = 5000;
        assert!(breed_block(&a, &b).unwrap().contains("2 h"));
        b.breed_rest_secs = 0;
        b.breed_count = 3;
        assert!(breed_block(&a, &b).unwrap().contains("3 times"));
        b.breed_count = 0;
        b.flags |= CARRIED_OUT;
        assert!(
            breed_block(&a, &b)
                .unwrap()
                .contains("put your companion away")
        );
    }

    #[test]
    fn breeding_and_retraining_ask_first() {
        let mut armed = None;
        let breed = Act::Breed { a: 1, b: 2 };
        assert!(!press_goes(&mut armed, breed));
        assert!(press_goes(&mut armed, breed));
        assert!(armed.is_none());
        let repick = Act::Repick {
            item: 1,
            stage: 0,
            option: 2,
        };
        assert!(!press_goes(&mut armed, repick));
        // Anything else pressed between disarms it.
        assert!(press_goes(&mut armed, Act::Item(1)));
        assert!(!press_goes(&mut armed, repick));
        // A plain button goes at once.
        assert!(press_goes(&mut armed, Act::Summon(1)));
        assert_eq!(CompanionUi::default().tab, Tab::Party);
    }

    /// Op 34 for the tiger line of `index_body`: each stage's model and move spells.
    fn looks_body() -> Vec<u8> {
        encode_looks(&[Looks {
            id: 106,
            stages: vec![
                StageLook {
                    display: 598,
                    spells: [1822, 2649],
                },
                StageLook {
                    display: 320,
                    spells: [1822, 2649],
                },
                StageLook {
                    display: 698,
                    spells: [1822, 2649],
                },
            ],
            basic_spell: 0,
            mounted_spell: 23110,
            ride_display: 698,
            found: "Drops from Young Stranglethorn Tiger. Rare and elite ones drop it more often."
                .into(),
        }])
    }

    #[test]
    fn looks_read_as_the_server_writes_them_and_choose_the_model() {
        let body = looks_body();
        let looks = decode_looks(&body).unwrap();
        assert_eq!(looks.len(), 1);
        assert_eq!(looks[0].stages[2].display, 698);
        assert_eq!(looks[0].ride_display, 698);
        let mut cut = body.clone();
        cut.pop();
        assert!(decode_looks(&cut).is_none(), "cut short");
        let mut long = body.clone();
        long.push(0);
        assert!(decode_looks(&long).is_none(), "a byte too many");

        let mut ui = CompanionUi {
            open: true,
            ..CompanionUi::default()
        };
        assert!(ui.apply(&index_body()));
        assert!(ui.apply(&body));
        ui.dirty = false;
        assert!(ui.apply(&body));
        assert!(!ui.dirty, "the same looks again redraw nothing");
        // Your companion: its current form's model.
        ui.carried = vec![Carried {
            item: 9,
            species: 106,
            stage: 1,
            ..Carried::default()
        }];
        ui.item = Some(9);
        assert_eq!(ui.preview_display(), Some(320));
        // The Bestiary: the form on show; an unknown entry shows none.
        ui.tab = Tab::Bestiary;
        ui.species = Some(106);
        ui.stage_shown = 2;
        assert_eq!(ui.preview_display(), Some(698));
        ui.stage_shown = 9;
        assert_eq!(
            ui.preview_display(),
            Some(698),
            "past the last form: the last"
        );
        ui.species = Some(7);
        assert_eq!(ui.preview_display(), None);
        ui.open = false;
        ui.species = Some(106);
        assert_eq!(ui.preview_display(), None, "a shut window lets the pane go");
    }

    #[test]
    fn a_card_says_what_its_companion_waits_for() {
        let mut c = carried(1, 20, CARRIED_CAN_EVOLVE, 1);
        assert_eq!(card_badge(&c).0, "Ready to evolve!");
        c.flags = 0;
        assert_eq!(card_badge(&c).0, "New move to learn!");
        c.pending = -1;
        c.flags = CARRIED_OUT;
        assert_eq!(card_badge(&c).0, "Out");
        c.flags = CARRIED_FAINTED;
        c.faint_secs = 3120;
        assert_eq!(card_badge(&c), ("Fainted · 52 min".into(), RED));
        assert_eq!(health_of(&c).0, 0.0);
        c.flags = 0;
        c.hurt_pct = 70;
        let (share, color) = health_of(&c);
        assert!((share - 0.3).abs() < 1e-6 && color != health_of(&carried(1, 1, 0, -1)).1);
        assert_eq!(card_badge(&carried(1, 1, 0, -1)).0, "");
    }

    #[test]
    fn a_level_or_an_evolution_is_cheered() {
        let bar = |item, level| companion::Bar {
            item,
            species: 6,
            level,
            flags: companion::BAR_SUMMONED,
            ..companion::Bar::default()
        };
        assert_eq!(level_cheer(None, &bar(5, 10)), None);
        assert_eq!(
            level_cheer(Some(&bar(5, 10)), &bar(5, 11)),
            Some(Cheer::Level {
                item: 5,
                species: 6,
                level: 11
            })
        );
        assert_eq!(level_cheer(Some(&bar(5, 11)), &bar(5, 11)), None);
        assert_eq!(
            level_cheer(Some(&bar(4, 10)), &bar(5, 30)),
            None,
            "another companion"
        );
        assert_ne!(
            cheer_sound(&Cheer::Level {
                item: 1,
                species: 1,
                level: 2
            }),
            cheer_sound(&Cheer::Evolved {
                item: 1,
                species: 1,
                stage: 1
            })
        );
        let mut ui = CompanionUi::default();
        ui.apply(&index_body());
        let b = evolve_banner(106, 2);
        assert_eq!(b.title, "EVOLVED");
        assert_eq!(
            b.subtitle,
            "Stranglethorn Tiger evolved into Elder Stranglethorn Tiger!"
        );
    }

    #[test]
    fn the_bestiary_entry_shows_its_model_its_forms_and_its_marks() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        let mut ui = CompanionUi {
            open: true,
            tab: Tab::Bestiary,
            ..CompanionUi::default()
        };
        ui.apply(&index_body());
        ui.apply(&looks_body());
        ui.species = Some(106);
        world.insert_resource(ui);
        world.init_resource::<companion::CompanionState>();
        world.run_system_once(build_window).unwrap();
        world.run_system_once(refresh).unwrap();
        let mut acts = world.query::<&Act>();
        let all: Vec<Act> = acts.iter(&world).copied().collect();
        for st in 0..3 {
            assert!(all.contains(&Act::ShowStage(st)), "form {st} on the path");
        }
        assert!(all.contains(&Act::Turn(-1)) && all.contains(&Act::Turn(1)));
        let mut panes = world.query_filtered::<Entity, With<ModelPane>>();
        assert_eq!(panes.iter(&world).count(), 1, "one model frame");
        let mut texts = world.query::<&Text>();
        let words: Vec<String> = texts.iter(&world).map(|t| t.0.clone()).collect();
        assert!(
            words.iter().any(|t| t.starts_with("Drops from")),
            "where it is found"
        );
        assert!(words.iter().any(|t| t == "Caught"), "{words:?}");
        assert!(words.iter().any(|t| t == "Bestiary"), "the tab");
        // The model goes to the stable pane, at the window's facing.
        world.init_resource::<LayerCreaturePreview>();
        world.run_system_once(drive_preview).unwrap();
        assert_eq!(
            world.resource::<LayerCreaturePreview>().display_id,
            Some(598),
            "the first form"
        );
        world.resource_mut::<CompanionUi>().stage_shown = 2;
        world.resource_mut::<CompanionUi>().yaw = 1.25;
        world.run_system_once(drive_preview).unwrap();
        let p = *world.resource::<LayerCreaturePreview>();
        assert_eq!((p.display_id, p.yaw), (Some(698), 1.25));
        world.resource_mut::<CompanionUi>().open = false;
        world.run_system_once(drive_preview).unwrap();
        assert_eq!(world.resource::<LayerCreaturePreview>().display_id, None);
    }

    /// Laid out as in the game: the window keeps its size whatever its pages show (a short or a
    /// long list, a companion chosen or not, the Bestiary, a long name), so it never jumps.
    #[test]
    fn the_window_never_changes_size() {
        let mut app = crate::wow_ui::layout_app();
        let mut ui = CompanionUi {
            open: true,
            ..CompanionUi::default()
        };
        ui.apply(&index_body());
        ui.apply(&looks_body());
        app.insert_resource(ui);
        app.init_resource::<companion::CompanionState>();
        app.add_systems(Update, (build_window, refresh).chain());
        for _ in 0..3 {
            app.update();
        }
        crate::wow_ui::measure_text(&mut app);
        let size = |app: &mut App| {
            for _ in 0..4 {
                app.update();
            }
            let world = app.world_mut();
            let mut roots = world.query_filtered::<Entity, With<UiRoot>>();
            let root = roots.single(world).unwrap();
            crate::wow_ui::screen_rect(world, root).size()
        };
        let empty = size(&mut app);
        assert!(empty.x > 300.0 && empty.y > 200.0, "{empty:?}");
        let mut seen = vec![("empty", empty)];
        let many: Vec<Carried> = (1..=12u64)
            .map(|i| Carried {
                item: i,
                species: 106,
                stage: (i % 3) as u8,
                level: (i * 5) as u8,
                pending: if i == 2 { 1 } else { -1 },
                flags: match i {
                    1 => CARRIED_OUT | CARRIED_RIDEABLE,
                    3 => CARRIED_FAINTED,
                    4 => CARRIED_CAN_EVOLVE,
                    _ => 0,
                },
                traits: 0b111,
                rarity: 3,
                xp: 10,
                xp_to_next: 100,
                hurt_pct: 20,
                ..Carried::default()
            })
            .collect();
        type Step = Box<dyn Fn(&mut CompanionUi)>;
        let steps: Vec<(&str, Step)> = vec![
            (
                "many companions",
                Box::new(move |ui: &mut CompanionUi| {
                    ui.carried = many.clone();
                    ui.item = None;
                }),
            ),
            (
                "one chosen",
                Box::new(|ui: &mut CompanionUi| ui.item = Some(2)),
            ),
            (
                "another",
                Box::new(|ui: &mut CompanionUi| ui.item = Some(3)),
            ),
            (
                "the Bestiary",
                Box::new(|ui: &mut CompanionUi| {
                    ui.tab = Tab::Bestiary;
                    ui.species = Some(106);
                }),
            ),
            (
                "an unknown entry",
                Box::new(|ui: &mut CompanionUi| ui.species = Some(7)),
            ),
            (
                "a game master's tools",
                Box::new(|ui: &mut CompanionUi| {
                    ui.gm = true;
                    ui.species = Some(106);
                    ui.tab = Tab::Party;
                }),
            ),
        ];
        for (what, change) in steps {
            {
                let mut ui = app.world_mut().resource_mut::<CompanionUi>();
                change(&mut ui);
                ui.dirty = true;
            }
            seen.push((what, size(&mut app)));
        }
        for (what, s) in &seen {
            assert!(
                (s.x - empty.x).abs() < 0.5 && (s.y - empty.y).abs() < 0.5,
                "{what}: {s:?}, empty {empty:?}; all {seen:?}"
            );
        }
    }

    /// The server's look table (`29_companion_looks.sql`, every stage's display and scale): `(id,
    /// [(display, scale)])` in file order.
    fn look_table() -> Vec<(u16, Vec<(u32, f32)>)> {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../server/sql/custom/azeroth_warfare");
        // 29 has the lines' looks, 31 the vanilla pets'.
        let sql = ["29_companion_looks.sql", "31_vanilla_companions.sql", "35_flying_companions.sql"]
            .iter()
            .map(|f| std::fs::read_to_string(dir.join(f)).expect(f))
            .collect::<Vec<_>>()
            .join(
                "
",
            );
        let num = |s: &str| -> String {
            s.chars()
                .take_while(|c| c.is_ascii_digit() || *c == '.')
                .collect()
        };
        let mut out = Vec::new();
        for line in sql
            .lines()
            .filter(|l| l.starts_with("UPDATE `fusion_companion_species`"))
        {
            let id: u16 = num(&line[line.find("`id` = ").expect("an id") + 7..])
                .parse()
                .expect("the id");
            let mut stages = Vec::new();
            for st in 1..=3 {
                let d = format!("`stage{st}_display` = ");
                let s = format!("`stage{st}_scale` = ");
                let (Some(a), Some(b)) = (line.find(&d), line.find(&s)) else {
                    break;
                };
                stages.push((
                    num(&line[a + d.len()..]).parse().expect("a display"),
                    num(&line[b + s.len()..]).parse().expect("a scale"),
                ));
            }
            out.push((id, stages));
        }
        out
    }

    /// Each species' family, from the species rows (`02_companions.sql`, `08_companion_lines.sql`).
    fn species_families() -> HashMap<u16, u8> {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../server/sql/custom/azeroth_warfare");
        let mut out = HashMap::new();
        for file in [
            "02_companions.sql",
            "08_companion_lines.sql",
            "31_vanilla_companions.sql",
            "35_flying_companions.sql",
        ] {
            let sql = std::fs::read_to_string(dir.join(file)).expect(file);
            let mut rows = false;
            for line in sql.lines() {
                if line.starts_with("INSERT INTO `fusion_companion_species`") {
                    rows = true;
                } else if rows && line.starts_with('(') {
                    // (id, 'name', item, family, ...: the name holds no comma.
                    let f: Vec<&str> = line[1..].splitn(5, ", ").collect();
                    out.insert(f[0].parse().expect("id"), f[3].parse().expect("family"));
                    rows = !line.trim_end().ends_with(';');
                }
                // The column list between the INSERT and its rows keeps reading.
            }
        }
        out
    }

    /// The model files a family's forms may wear (lowercase pieces of `CreatureModelData`'s path),
    /// by `Companions::Family` id: its own creature, and the kin a line grows into (a whelp into a
    /// drake and a dragon, a cat into a riding sabercat, a fire spark from a red wisp).
    fn family_models(family: u8) -> &'static [&'static str] {
        match family {
            // The critters, the vanilla pets' models too (31_vanilla_companions.sql).
            0 => &[
                "\\cat\\",
                "\\rabbit\\",
                "\\parrot\\",
                "\\snake\\",
                "\\cockroach\\",
                "\\prariedog\\",
                "\\chicken\\",
                "\\frog\\",
                "\\zergling\\",
                "\\panda\\",
                "\\diablo\\",
                "\\cupid\\",
                "\\rat\\",
                "\\hippogryph\\",
                // What the pets grow into: a kitten into a tiger, a snake into a serpent, a
                // cockroach into a silithid, a chicken into a tallstrider, a panda into a bear,
                // Mini Diablo into a doomguard.
                "\\squirrel\\",
                "\\tiger\\",
                "\\serpent\\",
                "\\silithidscarab\\",
                "\\silithid\\",
                "\\tallstrider\\",
                "\\bear\\",
                "\\felguard\\",
                "\\doomguard\\",
            ],
            1 => &[
                "\\dragonwhelp\\",
                "\\drake\\",
                "\\dragon\\",
                "\\dragonspawn",
                "\\chinesedragon\\",
            ],
            2 => &["\\faeriedragon\\"],
            3 => &["\\wolf\\", "\\direwolf\\"],
            4 => &["\\tiger\\", "\\lion\\", "\\frostsabre\\"],
            5 => &["\\bear\\", "\\druidbear\\"],
            6 => &["\\minespider\\", "\\tarantula\\"],
            7 => &["\\raptor\\", "\\ridingraptor\\"],
            8 => &["\\crocodile\\"],
            9 => &["\\gorilla\\"],
            10 => &["\\owl\\"],
            11 => &["\\felbat\\"],
            12 => &["\\tallstrider\\"],
            13 => &["\\scorpion\\"],
            14 => &["\\fireelemental\\fireelemental.", "\\wisp\\wispred"],
            15 => &["\\waterelemental\\", "\\wisp\\wisp."],
            16 => &["\\elementalearth\\", "\\golemstone\\"],
            17 => &["\\airelemental\\"],
            18 => &["\\murloc\\"],
            19 => &["\\infernal\\"],
            20 => &["\\slime\\", "\\slimegiant\\"],
            21 => &[
                "\\squirrel\\",
                "\\golemharvest",
                "\\goblinshredder",
                "\\gnomespidertank",
                "\\chicken\\",
                "\\frog\\",
                "\\wendigo\\",
                "\\mechastrider\\",
            ],
            22 => &["\\kodobeast\\"],
            23 => &["\\horse\\", "\\ridinghorse\\"],
            24 => &["\\mechastrider\\"],
            25 => &["\\undeadhorse\\"],
            26 => &["\\ram\\"],
            27 => &["\\hyena\\"],
            28 => &["\\boar\\", "\\pig\\"],
            29 => &["\\crab\\"],
            30 => &["\\seaturtle\\", "\\turtle\\", "\\ridingturtle\\"],
            31 => &["\\windserpent\\"],
            32 => &["\\carrionbird\\"],
            33 => &["\\basilisk\\"],
            34 => &["\\threshadon\\", "\\shark\\"],
            35 => &["\\hydra\\"],
            36 => &["\\bogbeast\\"],
            37 => &["\\imp\\"],
            38 => &["\\felhound\\", "\\felbeast\\"],
            39 => &["\\felguard\\", "\\doomguard\\"],
            40 => &["\\skeleton\\", "\\skeletonnaked\\"],
            41 => &["\\ghoul\\", "\\fleshgolem\\"],
            42 => &["\\banshee\\"],
            43 => &["\\gnoll\\", "\\gnollmelee\\"],
            44 => &["\\kobold\\"],
            45 => &["\\troglodyte\\"],
            46 => &["\\ogre\\"],
            47 => &["\\centaur\\"],
            48 => &["\\quillboar\\"],
            49 => &["\\furbolg\\"],
            50 => &["\\harpy\\"],
            51 => &["\\troll\\", "\\trolldire\\"],
            52 => &["\\nagamale\\"],
            53 => &["\\lobstrok\\"],
            54 => &["\\forceofnature\\"],
            55 => &["\\worgen\\"],
            56 => &[
                "\\elementalpoison\\",
                "\\fireelemental\\fireelementalgreen",
                "\\slime\\slimelesser",
            ],
            57 => &["\\darkhound\\"],
            58 => &["\\cryptfiend\\"],
            _ => &[],
        }
    }

    /// Every display the look table names is a real 1.12 creature display of its line's family:
    /// in the install's `CreatureDisplayInfo.dbc`, its model file in the install, of the family's
    /// models. Consecutive forms look different: another model or skin, or (only on a model 1.12
    /// paints one way) clearly bigger; every form on the same model as the one before is clearly
    /// bigger; and the mount lines end on a riding model. Skipped without an install.
    #[test]
    fn every_stage_look_is_a_real_display_of_its_family() {
        let table = look_table();
        let families = species_families();
        // Every species the client names, each with all its stages.
        assert_eq!(
            table.len(),
            crate::companion::species_count(),
            "a species without its looks"
        );
        for (id, stages) in &table {
            assert_eq!(stages.len(), 3, "species {id}: every line has three forms");
            assert_eq!(
                stages.len(),
                usize::from(crate::companion::stages_of(*id)),
                "species {id}'s stages"
            );
            assert!(families.contains_key(id), "species {id} has no row");
        }
        let Some(data) = install() else {
            eprintln!("no WoW install (WOW_DATA or dist/AzerothWarfare/game/WoW/Data): skipped");
            return;
        };
        let mut chain = benilla_formats::open_chain(&data).expect("the install's chain");
        let catalog = benilla_formats::load_creature_catalog(&mut chain).expect("the displays");
        // The skins each model is painted with, to know a model with only one.
        let mut skins: HashMap<String, std::collections::HashSet<Option<String>>> = HashMap::new();
        for (display, _) in catalog.display_models() {
            if let Some(m) = catalog.model(display) {
                skins
                    .entry(m.model_path.to_lowercase())
                    .or_default()
                    .insert(m.textures[0].clone());
            }
        }
        // The riding forms the owner asked for (the lines whose family has a vanilla mount).
        let mounts: [u16; 19] = [
            6, 7, 8, 12, 29, 30, 31, 32, 33, 106, 107, 108, 109, 110, 111, 112, 113, 114, 130,
        ];
        // The longest side, in yards, a first form may have: a vanity pet's (the vanilla pets
        // stand 0.7 to 2.5).
        const MAX_FIRST_FORM: f32 = 2.5;
        let mut problems = Vec::new();
        for (id, stages) in &table {
            // A line wearing the WotLK patch layer's displays (40001+, not in a 1.12 install) is
            // held to these rules against the built layer by `wotlk::import`'s tests.
            if stages.iter().any(|&(d, _)| d >= crate::wotlk::import::DISPLAY_BASE) {
                continue;
            }
            let family = families[id];
            let mut prev: Option<(String, Option<String>, f32)> = None;
            let mut extents = Vec::new();
            let mut textures = Vec::new();
            for (st, &(display, scale)) in stages.iter().enumerate() {
                if let Some(m) = catalog.model(display) {
                    textures.push(m.textures[0].clone().unwrap_or_default());
                }
                let Some(m) = catalog.model(display) else {
                    problems.push(format!(
                        "{id} stage {}: display {display} is not in the install",
                        st + 1
                    ));
                    prev = None;
                    continue;
                };
                let path = m.model_path.to_lowercase();
                let file = path.replace(".mdx", ".m2");
                if !chain.contains(&file) {
                    problems.push(format!(
                        "{id} stage {}: {display}'s model {path} is not in the install",
                        st + 1
                    ));
                }
                if !family_models(family).iter().any(|k| path.contains(k)) {
                    problems.push(format!(
                        "{id} stage {}: {display}'s model {path} is not family {family}",
                        st + 1
                    ));
                }
                // A character-model NPC wears its own build: its display is its look.
                let skin = if m.npc_appearance.is_some() {
                    Some(format!("npc {display}"))
                } else {
                    m.textures[0].clone()
                };
                // Its size in the world: the stage's scale on top of the display's own.
                let size = m.scale * scale;
                if let Some((p_path, p_skin, p_size)) = &prev
                    && *p_path == path
                {
                    if size < 1.15 * p_size {
                        problems.push(format!(
                            "{id} stage {}: {display} on the same model is not clearly bigger ({p_size:.2} -> {size:.2})",
                            st + 1
                        ));
                    }
                    if *p_skin == skin && skins.get(&path).is_some_and(|s| s.len() > 1) {
                        problems.push(format!(
                            "{id} stage {}: {display} wears the stage before's skin where {path} has others",
                            st + 1
                        ));
                    }
                }
                prev = Some((path, skin, size));
                // Its size in yards: the model's own box (its longest side) at that scale.
                match benilla_formats::load_m2_bounds(&mut chain, &m.model_path) {
                    Ok(b) => {
                        let side = (0..3)
                            .map(|k| b.bbox_max[k] - b.bbox_min[k])
                            .fold(0.0f32, f32::max);
                        extents.push(side * size);
                    }
                    Err(e) => {
                        problems.push(format!("{id} stage {}: {display}'s bounds: {e}", st + 1))
                    }
                }
            }
            // A pet-sized first form, clearly bigger forms after it.
            if let [first, second, third] = extents[..] {
                if first > MAX_FIRST_FORM {
                    problems.push(format!(
                        "{id}: stage 1 is {first:.2} yards, more than a pet's {MAX_FIRST_FORM}"
                    ));
                }
                if first > 0.6 * third {
                    problems.push(format!(
                        "{id}: stage 1 ({first:.2}) is more than 0.6 of stage 3 ({third:.2})"
                    ));
                }
                for (st, (a, b)) in [(2, (first, second)), (3, (second, third))] {
                    if b < 1.2 * a {
                        problems.push(format!(
                            "{id} stage {st}: {b:.2} yards is not clearly bigger than {a:.2}"
                        ));
                    }
                }
            }
            // The first form is the young of the last: the same skin, or the same colour of it.
            if let [first, .., last] = &textures[..]
                && !FIRST_FORM_TONE_EXCEPTIONS.contains(id)
                && first != last
                && (skin_tone(first).is_none() || skin_tone(first) != skin_tone(last))
            {
                problems.push(format!(
                    "{id}: stage 1 wears {first}, not the colour of stage 3's {last}"
                ));
            }
            if mounts.contains(id) {
                let last = stages.last().and_then(|&(d, _)| catalog.model(d));
                let riding = last.is_some_and(|m| {
                    let p = m.model_path.to_lowercase();
                    p.contains("riding") || p.contains("mechastrider")
                });
                if !riding {
                    problems.push(format!("{id}: the last form is no riding model"));
                }
                // Walking, it is the size it is ridden at: the mount's own.
                if stages.last().is_some_and(|&(_, s)| (s - 1.0).abs() > 1e-4) {
                    problems.push(format!("{id}: the riding form is rescaled"));
                }
            }
        }
        assert!(problems.is_empty(), "{problems:#?}");
    }

    /// The colour a creature skin is painted, from its texture name (`TigerSkinBlack`,
    /// `RidingTigerSkinBlack` -> "black"): the last colour word in it, synonyms folded together.
    /// `None` for a skin with no colour in its name.
    fn skin_tone(texture: &str) -> Option<&'static str> {
        const WORDS: &[(&str, &str)] = &[
            ("black", "black"),
            ("dark", "dark"),
            ("white", "white"),
            ("snow", "white"),
            ("arctic", "white"),
            ("wooly", "white"),
            ("ivory", "white"),
            ("polar", "white"),
            ("red", "red"),
            ("crimson", "red"),
            ("tomato", "red"),
            ("blood", "red"),
            ("blue", "blue"),
            ("turquoise", "blue"),
            ("aqua", "blue"),
            ("green", "green"),
            ("emerald", "green"),
            ("jade", "green"),
            ("olive", "green"),
            ("brown", "brown"),
            ("yellow", "yellow"),
            ("gold", "yellow"),
            ("gray", "gray"),
            ("grey", "gray"),
            ("silver", "gray"),
            ("purple", "purple"),
            ("violet", "purple"),
            ("lavender", "purple"),
            ("orange", "orange"),
            ("pink", "pink"),
            ("magma", "magma"),
            ("jungle", "jungle"),
            ("copper", "copper"),
        ];
        let t: String = texture
            .chars()
            .filter(char::is_ascii_alphabetic)
            .collect::<String>()
            .to_lowercase();
        WORDS
            .iter()
            .filter_map(|(w, c)| t.rfind(w).map(|k| ((k + w.len(), w.len()), *c)))
            .max_by_key(|(key, _)| *key)
            .map(|(_, c)| c)
    }

    /// The lines whose first form cannot wear its last form's colour from the 1.12 install: the
    /// young model comes in one skin (or another creature is the young of the last form). A WotLK
    /// young model in the right skin replaces stage 1 here (`stage1_display` in
    /// `29_companion_looks.sql` / `31_vanilla_companions.sql` and the look table in
    /// `Companions.cpp`); then its id leaves this list.
    const FIRST_FORM_TONE_EXCEPTIONS: &[u16] = &[
        10, 27, 29, 32, 112, 146, 149, 150, 152, 156, 157, 160, 175, 179, 180, 181, 188, 192, 193,
        194, 209, 214,
    ];

    #[test]
    fn skin_tones_read_the_colour_word() {
        assert_eq!(skin_tone("RidingTigerSkinBlack"), Some("black"));
        assert_eq!(skin_tone("DireWolfSkinReddishBrown"), Some("brown"));
        assert_eq!(skin_tone("DragonSkin1Red"), Some("red"));
        assert_eq!(skin_tone("RidingRaptorSkinTomato"), Some("red"));
        assert_eq!(skin_tone("PolarBearCubSkin"), Some("white"));
        assert_eq!(skin_tone("InfernalSkin"), None);
    }

    fn install() -> Option<std::path::PathBuf> {
        if let Some(d) = std::env::var_os("WOW_DATA").map(std::path::PathBuf::from) {
            return d.is_dir().then_some(d);
        }
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .ancestors()
            .map(|a| a.join("dist/AzerothWarfare/game/WoW/Data"))
            .find(|d| d.is_dir())
    }

    /// The old name of the Bestiary is gone from everything the player reads.
    #[test]
    fn no_creature_index_is_left() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let mut files = vec![root.join("assets/guide.md")];
        let mut stack = vec![root.join("src")];
        while let Some(d) = stack.pop() {
            for e in std::fs::read_dir(&d).unwrap().flatten() {
                let p = e.path();
                if p.is_dir() {
                    stack.push(p);
                } else if p.extension().is_some_and(|x| x == "rs" || x == "md") {
                    files.push(p);
                }
            }
        }
        let needle = ["creature", "index"].join(" ");
        let left: Vec<String> = files
            .iter()
            .filter(|p| {
                std::fs::read_to_string(p)
                    .unwrap_or_default()
                    .to_lowercase()
                    .contains(&needle)
            })
            .map(|p| p.display().to_string())
            .collect();
        assert!(left.is_empty(), "{left:?}");
    }
}
