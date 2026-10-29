//! The four action systems (WOMW): what the hands are doing and which action bar shows.
//!
//! - **Modern**: the MW2 gun and its gear. Its bar holds the layer's own abilities: the
//!   primary, the secondary, the underbarrel, the killstreak call-in, the lethal and the tactical,
//!   special ammo and rearm ([`Ability`]).
//! - **Classic**: WoW's combat, the two-hand way (crate::hands): melee, spells and class abilities.
//!   Its bar is WoW's own saved slots: a slot key readies a spell in the main hand, with Alt in
//!   the off hand.
//! - **Utility**: the weapons put away, bare hands (the server swings them unarmed), and the
//!   trades' tools ([`Ability::is_tool`]): a slot key readies one in the hands (bare hands put it
//!   away), and the fire button works with it (crate::tools).
//! - **Companion**: the battle companion's bar (crate::companion_bar), a view over the hands'
//!   system: the hands keep what they hold (the gun stays up in Modern), and the six slots are the
//!   companion's pet bar ([`Ability::CompanionSlot1`]..10: its moves, the ride or the mounted
//!   move, its commands and stances). A slot key or a click presses that pet bar slot; a right
//!   click turns its autocast on or off. The stock pet bar is not shown.
//!
//! Each system has its own bar of 6 slots a page and several pages, saved per character as WoW's
//! action slots are (the server stores the layer's entries as their own slot kind,
//! `benilla_app::fusion::ACTION_KIND_FUSION`): Classic in the Lua actions 1-60 (WoW's own first
//! pages, where the server lays the kit out), Utility in 61-72, Modern in 193-216 (the two bonus
//! pages no form uses), Companion in 217-228 (two pages past the 216 the forms fill, `ACTION_SLOTS` 228). Only the active system's bar shows, on the main bar's first six buttons;
//! an ability goes only on its own system's bar ([`write_allowed`], and the server's
//! `Fusion::ActionSystems`).
//!
//! The keys (crate::keybinds): the slot keys (1, 2, 3, 4, Q, E) press the current page's slots,
//! Alt with the wheel turns the page (the bare and the Shift wheel never do), X switches between
//! Modern and Classic, Z puts the weapons away into Utility and back to the system before, C shows
//! the Companion bar and back (X and Z leave it too).

use std::collections::HashMap;

use benilla_app::fusion::{
    ACTION_KIND_FUSION, ChannelMessage, FusionActionBar, FusionChannelReceived, FusionLook,
    MainBarView, ReadiedAction, SendFusionChannel,
};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::input::{Control, Mw2Input};
use crate::keybinds::{Keymap, SLOTS as SLOT_ACTIONS};
use crate::mode::FpsState;

/// Slots on a page.
pub const SLOTS: usize = 6;

/// The action-systems channel (`Fusion::ActionSystems::kChannel`).
pub const CHANNEL: u8 = 10;
/// Client to server: the active system (`kOpSystem`), one byte [`System::wire`].
pub const OP_SYSTEM: u8 = 1;
/// Client to server: the tool readied in Utility's hands (`kOpTool`), its ability id (0 bare
/// hands).
pub const OP_TOOL: u8 = 2;
/// Server to client: the tools Utility's bar shows (`kOpTools`), a u16 mask, bit n the tool
/// ability `Ability::MiningPick` + n: those the character carries (bags or equipped) and is
/// trained for (the trade known; the Repair Hammer asks only owning it). At login and on a change.
pub const OP_TOOLS: u8 = 9;

/// A tool's bit in the tools mask ([`OP_TOOLS`]); 0 for anything that is no tool.
pub fn tool_bit(ability: Ability) -> u16 {
    if ability.is_tool() {
        1 << (ability.id() - Ability::MiningPick.id())
    } else {
        0
    }
}

/// The tools op's mask, `None` for another op or a malformed one.
pub fn decode_tools(body: &[u8]) -> Option<u16> {
    match body {
        [OP_TOOLS, lo, hi] => Some(u16::from_le_bytes([*lo, *hi])),
        _ => None,
    }
}

/// One action system.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum System {
    #[default]
    Modern,
    Classic,
    Utility,
    /// The companion's bar: a view, never the hands' system (the server is never told it).
    Companion,
}

impl System {
    pub const ALL: [System; 4] = [
        System::Modern,
        System::Classic,
        System::Utility,
        System::Companion,
    ];

    /// Its bar's pages' first Lua action ids. Utility's third page is the last six of bonus bar
    /// 12 (211-216), apart from its first two: Minstrelsy's instruments needed the room.
    pub fn page_starts(self) -> &'static [u32] {
        match self {
            System::Classic => &[1, 7, 13, 19, 25, 31, 37, 43, 49, 55],
            System::Utility => &[61, 67, 211],
            System::Modern => &[193, 199, 205],
            System::Companion => &[217, 223],
        }
    }

    /// Every Lua action id of its bar, page by page.
    pub fn actions(self) -> impl Iterator<Item = u32> {
        self.page_starts()
            .iter()
            .flat_map(|s| *s..*s + SLOTS as u32)
    }

    /// Whether its bar holds a Lua action id.
    pub fn holds(self, action: u32) -> bool {
        self.place_of(action).is_some()
    }

    /// Its bar's pages.
    pub fn pages(self) -> u8 {
        self.page_starts().len() as u8
    }

    /// The Lua action id of a page's first slot.
    pub fn first(self, page: u8) -> u32 {
        self.page_starts()[usize::from(page % self.pages())]
    }

    /// Where an action id is on its bar: its page and slot.
    pub fn place_of(self, action: u32) -> Option<(u8, usize)> {
        self.page_starts().iter().enumerate().find_map(|(p, s)| {
            (*s..*s + SLOTS as u32)
                .contains(&action)
                .then(|| (p as u8, (action - s) as usize))
        })
    }

    /// The system whose bar holds a Lua action id, none for the rest (the form pages).
    pub fn of_action(action: u32) -> Option<System> {
        System::ALL.into_iter().find(|s| s.holds(action))
    }

    /// Its byte on the wire (`Fusion::ActionSystems::System`). The Companion bar is no system of
    /// the hands: never sent ([`ActionSystems::active`] is never it).
    pub fn wire(self) -> u8 {
        match self {
            System::Modern | System::Companion => 0,
            System::Classic => 1,
            System::Utility => 2,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            System::Modern => "Modern",
            System::Classic => "Classic",
            System::Utility => "Utility",
            System::Companion => "Companion",
        }
    }

    /// The other armed system (X).
    pub fn other(self) -> System {
        match self {
            System::Modern => System::Classic,
            _ => System::Modern,
        }
    }
}

/// The layer's own abilities, by id (`Fusion::ActionSystems::Ability`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Ability {
    Primary = 1,
    Secondary = 2,
    Underbarrel = 3,
    Killstreak = 4,
    Lethal = 5,
    Tactical = 6,
    /// The next unlocked special round (the Armory's ammo).
    SpecialAmmo = 7,
    /// The rearm: free resting in a city or an inn, anywhere with Field Quartermaster.
    Rearm = 8,
    BareHands = 9,
    // Utility's tools (`Fusion::Gathering`), held in the hands.
    MiningPick = 10,
    SkinningKnife = 11,
    Herbalism = 12,
    FishingPole = 13,
    WoodcuttingAxe = 14,
    BlacksmithHammer = 15,
    RepairHammer = 16,
    // Minstrelsy's instruments (`Fusion::Minstrelsy`), played rather than swung (crate::minstrel).
    HandDrum = 17,
    Lute = 18,
    Flute = 19,
    Harp = 20,
    Bagpipes = 21,
    ThunderDrums = 22,
    /// Construction's Build Structure (the server's `Ability::BuildStructure`): build mode, a kit
    /// made at Construction's trade window placed in the world. Utility's alone, no tool.
    BuildStructure = 23,
    /// The Companion bar's entries (the server's `CompanionSlot1`..`10`): the companion's pet bar
    /// slots 1-10 (crate::companion_bar shows them; a press presses that slot).
    CompanionSlot1 = 24,
    CompanionSlot2 = 25,
    CompanionSlot3 = 26,
    CompanionSlot4 = 27,
    CompanionSlot5 = 28,
    CompanionSlot6 = 29,
    CompanionSlot7 = 30,
    CompanionSlot8 = 31,
    CompanionSlot9 = 32,
    CompanionSlot10 = 33,
}

/// The Companion bar's entries, by pet bar slot (0-9).
pub const COMPANION_SLOTS: [Ability; 10] = [
    Ability::CompanionSlot1,
    Ability::CompanionSlot2,
    Ability::CompanionSlot3,
    Ability::CompanionSlot4,
    Ability::CompanionSlot5,
    Ability::CompanionSlot6,
    Ability::CompanionSlot7,
    Ability::CompanionSlot8,
    Ability::CompanionSlot9,
    Ability::CompanionSlot10,
];

impl Ability {
    pub const ALL: [Ability; 33] = [
        Ability::Primary,
        Ability::Secondary,
        Ability::Underbarrel,
        Ability::Killstreak,
        Ability::Lethal,
        Ability::Tactical,
        Ability::SpecialAmmo,
        Ability::Rearm,
        Ability::BareHands,
        Ability::MiningPick,
        Ability::SkinningKnife,
        Ability::Herbalism,
        Ability::FishingPole,
        Ability::WoodcuttingAxe,
        Ability::BlacksmithHammer,
        Ability::RepairHammer,
        Ability::HandDrum,
        Ability::Lute,
        Ability::Flute,
        Ability::Harp,
        Ability::Bagpipes,
        Ability::ThunderDrums,
        Ability::BuildStructure,
        Ability::CompanionSlot1,
        Ability::CompanionSlot2,
        Ability::CompanionSlot3,
        Ability::CompanionSlot4,
        Ability::CompanionSlot5,
        Ability::CompanionSlot6,
        Ability::CompanionSlot7,
        Ability::CompanionSlot8,
        Ability::CompanionSlot9,
        Ability::CompanionSlot10,
    ];

    pub fn id(self) -> u32 {
        self as u32
    }

    pub fn from_id(id: u32) -> Option<Ability> {
        Ability::ALL.into_iter().find(|a| a.id() == id)
    }

    /// The bars it may go on. Bare hands are Classic's and Utility's, the tools Utility's alone.
    pub fn systems(self) -> &'static [System] {
        match self {
            Ability::BareHands => &[System::Classic, System::Utility],
            a if a.is_tool() || a == Ability::BuildStructure => &[System::Utility],
            a if a.pet_slot().is_some() => &[System::Companion],
            _ => &[System::Modern],
        }
    }

    /// A Utility tool (a gathering tool, the smith's hammer, the Repair Hammer): it takes the
    /// hands as bare hands do, held in them.
    pub fn is_tool(self) -> bool {
        self.id() >= Ability::MiningPick.id() && self.id() <= Ability::ThunderDrums.id()
    }

    /// The companion pet bar slot (0-9) a Companion bar entry presses.
    pub fn pet_slot(self) -> Option<u8> {
        let id = self.id();
        (Ability::CompanionSlot1.id()..=Ability::CompanionSlot10.id())
            .contains(&id)
            .then(|| (id - Ability::CompanionSlot1.id()) as u8)
    }

    /// One of Minstrelsy's instruments (a tool too).
    pub fn is_instrument(self) -> bool {
        self.id() >= Ability::HandDrum.id() && self.id() <= Ability::ThunderDrums.id()
    }

    pub fn name(self) -> &'static str {
        match self {
            Ability::Primary => "Primary Weapon",
            Ability::Secondary => "Secondary Weapon",
            Ability::Underbarrel => "Underbarrel Attachment",
            Ability::Killstreak => "Call in Killstreak",
            Ability::Lethal => "Lethal Grenade",
            Ability::Tactical => "Tactical Grenade",
            Ability::SpecialAmmo => "Special Ammo",
            Ability::Rearm => "Rearm",
            Ability::BareHands => "Hands",
            Ability::MiningPick => "Mining Pick",
            Ability::SkinningKnife => "Skinning Knife",
            Ability::Herbalism => "Herbalism",
            Ability::FishingPole => "Fishing Pole",
            Ability::WoodcuttingAxe => "Woodcutting Axe",
            Ability::BlacksmithHammer => "Blacksmith Hammer",
            Ability::RepairHammer => "Repair Hammer",
            Ability::HandDrum => "Hand Drum",
            Ability::Lute => "Lute",
            Ability::Flute => "Flute",
            Ability::Harp => "Harp",
            Ability::Bagpipes => "Bagpipes",
            Ability::ThunderDrums => "Thunder Drums",
            Ability::BuildStructure => "Build Structure",
            // Their live names are the companion's (crate::companion_bar).
            _ => "Companion",
        }
    }

    /// Its icon (`Interface\Icons\`).
    pub fn icon(self) -> &'static str {
        match self {
            Ability::Primary => "Interface\\Icons\\INV_Weapon_Rifle_06",
            Ability::Secondary => "Interface\\Icons\\INV_Weapon_Rifle_01",
            Ability::Underbarrel => "Interface\\Icons\\INV_Misc_Bomb_05",
            Ability::Killstreak => "Interface\\Icons\\INV_Misc_Spyglass_02",
            Ability::Lethal => "Interface\\Icons\\INV_Misc_Bomb_08",
            Ability::Tactical => "Interface\\Icons\\INV_Misc_Bomb_06",
            Ability::SpecialAmmo => "Interface\\Icons\\INV_Misc_Ammo_Bullet_01",
            Ability::Rearm => "Interface\\Icons\\INV_Crate_01",
            Ability::BareHands => "Interface\\Icons\\INV_Gauntlets_04",
            Ability::MiningPick => "Interface\\Icons\\INV_Pick_02",
            Ability::SkinningKnife => "Interface\\Icons\\INV_Misc_Pelt_Wolf_01",
            Ability::Herbalism => "Interface\\Icons\\Trade_Herbalism",
            Ability::FishingPole => "Interface\\Icons\\Trade_Fishing",
            Ability::WoodcuttingAxe => "Interface\\Icons\\INV_Axe_04",
            Ability::BlacksmithHammer => "Interface\\Icons\\Trade_BlackSmithing",
            Ability::RepairHammer => "Interface\\Icons\\INV_Hammer_20",
            // Vanilla has no instrument icons: a shout, a note, calm, a chime, a horn and a war
            // cry (VERIFY in game).
            Ability::HandDrum => "Interface\\Icons\\Ability_Warrior_BattleShout",
            Ability::Lute => "Interface\\Icons\\INV_Misc_Note_01",
            Ability::Flute => "Interface\\Icons\\Spell_Nature_Tranquility",
            Ability::Harp => "Interface\\Icons\\Spell_Holy_Heal02",
            Ability::Bagpipes => "Interface\\Icons\\INV_Misc_Horn_01",
            Ability::ThunderDrums => "Interface\\Icons\\Ability_Warrior_RallyingCry",
            Ability::BuildStructure => "Interface\\Icons\\Trade_Engineering",
            // Their live icons are the companion's (crate::companion_bar); none out, empty.
            _ => "",
        }
    }
}

/// Where each ability starts on a fresh bar: its system, page and slot. Utility's first page: bare
/// hands, the Repair Hammer and the gatherers' tools; its second the axe, the smith's hammer and
/// the first four instruments; its third the last two. A tool goes on only while the character
/// has it and knows its trade (an instrument: Minstrelsy to its skill) ([`seed_writes`]).
///
/// The Companion bar: its moves first (Command, the three stage picks, the ride or the mounted
/// move, a Twin-souled stage's other), then its commands and stances (Assist, Hold, Follow, Stay).
pub const DEFAULT_LAYOUT: [(Ability, System, u8, u8); 33] = [
    (Ability::Primary, System::Modern, 0, 0),
    (Ability::Secondary, System::Modern, 0, 1),
    (Ability::Underbarrel, System::Modern, 0, 2),
    (Ability::Killstreak, System::Modern, 0, 3),
    (Ability::Tactical, System::Modern, 0, 4),
    (Ability::Lethal, System::Modern, 0, 5),
    (Ability::SpecialAmmo, System::Modern, 1, 0),
    (Ability::Rearm, System::Modern, 1, 1),
    (Ability::BareHands, System::Utility, 0, 0),
    (Ability::RepairHammer, System::Utility, 0, 1),
    (Ability::MiningPick, System::Utility, 0, 2),
    (Ability::Herbalism, System::Utility, 0, 3),
    (Ability::SkinningKnife, System::Utility, 0, 4),
    (Ability::FishingPole, System::Utility, 0, 5),
    (Ability::WoodcuttingAxe, System::Utility, 1, 0),
    (Ability::BlacksmithHammer, System::Utility, 1, 1),
    (Ability::HandDrum, System::Utility, 1, 2),
    (Ability::Lute, System::Utility, 1, 3),
    (Ability::Flute, System::Utility, 1, 4),
    (Ability::Harp, System::Utility, 1, 5),
    (Ability::Bagpipes, System::Utility, 2, 0),
    (Ability::ThunderDrums, System::Utility, 2, 1),
    (Ability::BuildStructure, System::Utility, 2, 2),
    (Ability::CompanionSlot1, System::Companion, 0, 0),
    (Ability::CompanionSlot4, System::Companion, 0, 1),
    (Ability::CompanionSlot5, System::Companion, 0, 2),
    (Ability::CompanionSlot6, System::Companion, 0, 3),
    (Ability::CompanionSlot7, System::Companion, 0, 4),
    (Ability::CompanionSlot8, System::Companion, 0, 5),
    (Ability::CompanionSlot2, System::Companion, 1, 0),
    (Ability::CompanionSlot3, System::Companion, 1, 1),
    (Ability::CompanionSlot9, System::Companion, 1, 2),
    (Ability::CompanionSlot10, System::Companion, 1, 3),
];

/// Whether a slot write may stand: the layer's abilities only on their own systems' bars, and
/// WoW's own entries (spells, items, macros) only on Classic's bar and the form pages.
pub fn write_allowed(action: u32, packed: u32) -> bool {
    let kind = (packed >> 24) as u8;
    let id = packed & 0x00FF_FFFF;
    match (System::of_action(action), kind == ACTION_KIND_FUSION) {
        (Some(system), true) => Ability::from_id(id).is_some_and(|a| a.systems().contains(&system)),
        (Some(system), false) => system == System::Classic,
        (None, fusion) => !fusion,
    }
}

/// The packed slot word of an ability.
pub fn packed(ability: Ability) -> u32 {
    (u32::from(ACTION_KIND_FUSION) << 24) | ability.id()
}

/// The writes that make a loaded bar whole: WoW entries off the Modern and Utility bars, and each
/// ability missing from its bar put back, in its default slot when that is free, else in the
/// bar's first free one. `tools` is the server's tools mask ([`OP_TOOLS`]): the tools in it are
/// put on Utility's bar, those not in it taken off (no dead buttons); bare hands always stay. Not
/// told yet (`None`), the tools are left as they are.
pub fn seed_writes(saved: &HashMap<u32, u32>, tools: Option<u16>) -> Vec<(u32, u32)> {
    let mut slots: HashMap<u32, u32> = saved.clone();
    let mut writes = Vec::new();
    let unusable = |p: u32| {
        let kind = (p >> 24) as u8;
        let ability = Ability::from_id(p & 0x00FF_FFFF);
        match (tools, ability) {
            (Some(mask), Some(a)) if kind == ACTION_KIND_FUSION && a.is_tool() => {
                mask & tool_bit(a) == 0
            }
            _ => false,
        }
    };
    for system in [System::Modern, System::Utility, System::Companion] {
        for action in system.actions() {
            if let Some(&p) = slots.get(&action)
                && (!write_allowed(action, p) || unusable(p))
            {
                slots.remove(&action);
                writes.push((action, 0));
            }
        }
    }
    for (ability, system, page, slot) in DEFAULT_LAYOUT {
        if ability.is_tool() && tools.is_none_or(|mask| mask & tool_bit(ability) == 0) {
            continue;
        }
        let word = packed(ability);
        if system.actions().any(|a| slots.get(&a) == Some(&word)) {
            continue;
        }
        let home = system.first(page) + u32::from(slot);
        let Some(at) = std::iter::once(home)
            .chain(system.actions())
            .find(|a| !slots.contains_key(a))
        else {
            continue;
        };
        slots.insert(at, word);
        writes.push((at, word));
    }
    writes
}

/// The systems' state: which is active, the armed one Z returns to, and each bar's page.
#[derive(Resource, Debug, Clone, PartialEq, Eq)]
pub struct ActionSystems {
    pub active: System,
    /// The armed system (Modern or Classic) Z draws again, and X goes from in Utility.
    pub armed: System,
    /// Each system's page, in [`System::ALL`] order.
    pub pages: [u8; 4],
    /// The Companion bar shows (C) over the hands' system ([`Self::active`], which stays).
    pub companion: bool,
    /// A form's page half (its bonus bar's slots 1-6 or 7-12).
    pub form_half: u8,
    /// The tool Utility's hands hold (none: bare hands), kept while the weapons are out.
    pub tool: Option<Ability>,
    /// The tools the character has and is trained for, as the server last said ([`OP_TOOLS`]);
    /// `None` until it does.
    pub usable_tools: Option<u16>,
    /// The readied instrument's key pressed again (crate::minstrel picks its next song).
    pub tool_again: Option<Ability>,
    /// Build Structure pressed (crate::build enters build mode and takes it).
    pub build_structure: bool,
    /// Utility's Hands equipped: the fists raised for a fight. Off (the default on every way into
    /// Utility), the hands hang relaxed and the trigger does nothing (crate::hands `relaxed`).
    pub fists_up: bool,
    /// A grenade readied in a hand (Modern): the lethal or the tactical, and the hand (0 the main
    /// hand, the left mouse button's; 1 the off hand, the right button's). That button throws it
    /// (cooks it, held, for a frag) instead of firing or aiming the gun ([`grenade_controls`]).
    pub grenade: Option<(Ability, usize)>,
}

impl Default for ActionSystems {
    fn default() -> Self {
        Self {
            active: System::Modern,
            armed: System::Modern,
            pages: [0; 4],
            companion: false,
            form_half: 0,
            tool: None,
            usable_tools: None,
            tool_again: None,
            build_structure: false,
            fists_up: false,
            grenade: None,
        }
    }
}

impl ActionSystems {
    /// X: Modern and Classic back and forth; from Utility, the armed system's other.
    pub fn switch(&mut self) {
        self.fists_up = false;
        self.grenade = None;
        let next = match self.active {
            System::Utility => self.armed.other(),
            s => s.other(),
        };
        self.active = next;
        self.armed = next;
        self.companion = false;
    }

    /// C: the Companion bar over the hands' system, or back to that system's bar.
    pub fn toggle_companion(&mut self) {
        self.companion = !self.companion;
    }

    /// The system whose bar shows: the Companion bar while on, else the hands'.
    pub fn shown(&self) -> System {
        if self.companion {
            System::Companion
        } else {
            self.active
        }
    }

    /// Z: the weapons away into Utility, or drawn again into the system before.
    pub fn sheathe(&mut self) {
        self.fists_up = false;
        self.grenade = None;
        self.companion = false;
        if self.active == System::Utility {
            self.active = self.armed;
        } else {
            self.armed = self.active;
            self.active = System::Utility;
        }
    }

    fn index(system: System) -> usize {
        System::ALL.iter().position(|s| *s == system).expect("listed")
    }

    /// The shown bar's page.
    pub fn page(&self) -> u8 {
        self.pages[Self::index(self.shown())]
    }

    /// Turn the shown bar's page (`delta` pages, wrapping).
    pub fn turn_page(&mut self, delta: i32) {
        let n = i32::from(self.shown().pages());
        let i = Self::index(self.shown());
        self.pages[i] = (i32::from(self.pages[i]) + delta).rem_euclid(n) as u8;
    }

    /// The tool in the hands now: Utility's readied tool, none in the armed systems.
    pub fn tool_out(&self) -> Option<Ability> {
        self.tool.filter(|_| self.active == System::Utility)
    }

    /// The Lua action id of the shown page's slot (0..6).
    pub fn action_of(&self, slot: usize) -> u32 {
        self.shown().first(self.page()) + slot as u32
    }
}

/// What a slot key does.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SlotUse {
    /// One of the layer's abilities.
    Ability(Ability),
    /// A spell readied in a hand (the off hand with Alt).
    Ready(ReadiedAction),
    /// Used as `UseAction` would (an item, a macro, a form's ability).
    Use(u32),
    Nothing,
}

/// What a slot key does now: `form_page` is a gunless form's bar page (its bonus bar offset).
pub fn slot_use(
    systems: &ActionSystems,
    bar: &FusionActionBar,
    form_page: Option<u8>,
    slot: usize,
    alt: bool,
) -> SlotUse {
    if let Some(page) = form_page {
        if page == 0 {
            return SlotUse::Nothing;
        }
        let first = 73 + 12 * u32::from(page - 1) + 6 * u32::from(systems.form_half % 2);
        return SlotUse::Use(first + slot as u32);
    }
    let action = systems.action_of(slot);
    if let Some(a) = bar.fusion_slots.get(&action).and_then(|&id| Ability::from_id(id)) {
        return SlotUse::Ability(a);
    }
    if systems.shown() != System::Classic {
        return SlotUse::Nothing;
    }
    if let Some(&spell_id) = bar.spells.get(&action) {
        return SlotUse::Ready(ReadiedAction {
            action,
            spell_id,
            right_button: alt,
        });
    }
    if bar.saved.contains_key(&action) {
        SlotUse::Use(action)
    } else {
        SlotUse::Nothing
    }
}

/// An ability's control this frame: what its slot key (or a click on its button) did.
fn apply_ability(
    input: &mut Mw2Input,
    systems: &mut ActionSystems,
    ability: Ability,
    c: Control,
    _off_hand: bool,
) {
    let mirror = |into: &mut Control| {
        into.pressed |= c.pressed;
        into.held |= c.held;
        into.released |= c.released;
    };
    match ability {
        // A gun picked puts a readied grenade back.
        Ability::Primary | Ability::Secondary | Ability::Underbarrel if c.pressed => {
            systems.grenade = None;
            input.hand = Some(match ability {
                Ability::Primary => 0,
                Ability::Secondary => 1,
                _ => 2,
            });
        }
        Ability::Killstreak => mirror(&mut input.killstreak),
        // A grenade's key readies it in the main hand (never the off hand: no dual-wielded
        // grenades) or puts it back; the main hand's button then throws it ([`grenade_controls`]).
        Ability::Lethal | Ability::Tactical if c.pressed => {
            systems.grenade = match systems.grenade {
                Some((a, _)) if a == ability => None,
                _ => Some((ability, 0)),
            };
        }
        Ability::SpecialAmmo if c.pressed => input.armory_ammo.pressed = true,
        Ability::Rearm if c.pressed => input.rearm.pressed = true,
        // The Companion bar: its pet bar slot pressed (crate::companion aims it).
        a if c.pressed && a.pet_slot().is_some() => input.pet_slot = a.pet_slot(),
        // Bare hands on the Classic bar put the weapons away; on Utility's they put the tool away.
        Ability::BareHands if c.pressed && systems.active == System::Classic => systems.sheathe(),
        // Utility's Hands: a tool put away into raised fists; else the fists up or back down to
        // the relaxed hands.
        Ability::BareHands if c.pressed && systems.active == System::Utility => {
            systems.fists_up = systems.tool.is_some() || !systems.fists_up;
            systems.tool = None;
        }
        // Build Structure: build mode (crate::build), the hands put away first.
        Ability::BuildStructure if c.pressed && systems.active == System::Utility => {
            systems.tool = None;
            systems.build_structure = true;
        }
        // A tool's key readies it in the hands (Utility's bar is the only one that holds it).
        a if a.is_tool() && c.pressed && systems.active == System::Utility => {
            if a.is_instrument() && systems.tool == Some(a) {
                systems.tool_again = Some(a);
            }
            systems.fists_up = false;
            systems.tool = Some(a)
        }
        _ => {}
    }
}

/// The set the systems' keys are read in, before anything reads the controls they stand for.
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct ActionSystemsSet;

pub struct ActionSystemsPlugin;

impl Plugin for ActionSystemsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ActionSystems>().add_systems(
            Update,
            (read_tools, switch_systems, use_slots, show_bar)
                .chain()
                .in_set(Mw2Set::Mode)
                .in_set(ActionSystemsSet)
                .after(crate::mode::update_mode)
                .before(crate::forms::FormsSet)
                .before(crate::build::BuildInput)
                .before(crate::cac::CacSet),
        );
    }
}

/// The server's tools mask: kept, and a readied tool the character no longer has or knows goes
/// back to bare hands.
fn read_tools(mut msgs: MessageReader<FusionChannelReceived>, mut systems: ResMut<ActionSystems>) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL {
            continue;
        }
        if let Some(mask) = decode_tools(&m.body) {
            take_tools(&mut systems, mask);
        }
    }
}

/// Take the server's tools mask (see [`read_tools`]).
pub fn take_tools(systems: &mut ActionSystems, mask: u16) {
    systems.usable_tools = Some(mask);
    if systems.tool.is_some_and(|t| mask & tool_bit(t) == 0) {
        systems.tool = None;
    }
}

/// X, Z and the page keys; the server told the system whenever it changes (and at world entry).
fn switch_systems(
    state: Res<FpsState>,
    input: Res<Mw2Input>,
    form: Res<crate::forms::PlayerForm>,
    downed: Res<crate::downed::Downed>,
    mut systems: ResMut<ActionSystems>,
    mut out: MessageWriter<SendFusionChannel>,
    mut told: Local<Option<(u64, System)>>,
    mut told_tool: Local<Option<(u64, Option<Ability>)>>,
) {
    if !state.in_world {
        *systems = ActionSystems::default();
        *told = None;
        *told_tool = None;
        return;
    }
    // Down, the hands hold Last Stand's pistol or nothing: Modern, and no switching.
    if downed.active {
        if systems.active != System::Modern {
            systems.active = System::Modern;
            systems.armed = System::Modern;
        }
        systems.companion = false;
    } else {
        if input.companion_system {
            systems.toggle_companion();
        }
        if input.switch_system {
            systems.switch();
        }
        if input.sheathe {
            systems.sheathe();
        }
    }
    if input.page != 0 {
        if form.gunless() {
            systems.form_half = (systems.form_half + 1) % 2;
        } else {
            systems.turn_page(input.page);
        }
    }
    let Some(me) = state.self_guid else { return };
    if *told != Some((me, systems.active)) {
        *told = Some((me, systems.active));
        info!("benilla-mw2: {} system", systems.active.name());
        out.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL,
            body: vec![OP_SYSTEM, systems.active.wire()],
        }));
    }
    // The tool readied (the server puts it in the hands' melee profile, for its model).
    if *told_tool != Some((me, systems.tool)) {
        *told_tool = Some((me, systems.tool));
        out.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL,
            body: tool_body(systems.tool),
        }));
    }
}

/// The tool op's body: the readied tool's id, 0 for bare hands.
pub fn tool_body(tool: Option<Ability>) -> Vec<u8> {
    vec![OP_TOOL, tool.map_or(0, |a| a.id() as u8)]
}

/// The slot keys and the clicks on the layer's buttons, as what they stand for; the gun's own
/// picks (the wheel) only in Modern.
#[allow(clippy::too_many_arguments)]
fn use_slots(
    state: Res<FpsState>,
    mut input: ResMut<Mw2Input>,
    mut systems: ResMut<ActionSystems>,
    form: Res<crate::forms::PlayerForm>,
    aim: Res<crate::aim::Aim>,
    equip: Res<crate::equipment::EquipState>,
    bar: Option<ResMut<FusionActionBar>>,
) {
    let input = &mut *input;
    if systems.active != System::Modern {
        input.wheel_swap = false;
        input.hand = None;
    }
    let Some(mut bar) = bar else { return };
    let clicks = std::mem::take(&mut bar.fusion_pressed);
    if !state.active {
        return;
    }
    let form_page = form.gunless().then(|| form.rules.map_or(0, |r| r.bar_page));
    for slot in 0..SLOTS {
        let c = input.slots[slot];
        if !(c.pressed || c.held || c.released) {
            continue;
        }
        match slot_use(&systems, &bar, form_page, slot, input.slot_alt[slot]) {
            SlotUse::Ability(a) => apply_ability(input, &mut systems, a, c, input.slot_alt[slot]),
            SlotUse::Ready(r) if c.pressed => bar.readied.push(r),
            SlotUse::Use(action) if c.pressed => {
                if let Some(a) = aim.target.as_ref().filter(|a| !a.service) {
                    bar.select = Some((a.entity, a.guid));
                }
                bar.use_now.push(action);
            }
            _ => {}
        }
    }
    // A click on one of the layer's buttons is a tap of it, on its own system's bar.
    for p in clicks {
        let Some(a) = Ability::from_id(p.ability) else {
            continue;
        };
        if System::of_action(p.action) != Some(systems.shown()) {
            continue;
        }
        // A right click on the Companion bar is its autocast on or off, as the pet bar's is.
        if p.right_button
            && let Some(slot) = a.pet_slot()
        {
            input.pet_autocast = Some(slot);
            continue;
        }
        let tap = Control {
            pressed: true,
            held: false,
            released: false,
        };
        apply_ability(input, &mut systems, a, tap, p.right_button);
    }
    grenade_controls(input, &mut systems, &equip);
}

/// A readied grenade's hand: its mouse button throws it (pressed, held to cook, let go) in place
/// of the gun's fire or aim, and it goes back (the gun in hand again) once none is left, outside
/// Modern, or while the hands are busy elsewhere. C4 stays readied while it is out, so the same
/// button sets it off. The server checks every throw as before (the count, the cook, the arc).
pub fn grenade_controls(
    input: &mut Mw2Input,
    systems: &mut ActionSystems,
    equip: &crate::equipment::EquipState,
) {
    let Some((ability, side)) = systems.grenade else {
        return;
    };
    let left = match ability {
        Ability::Lethal => equip.lethal_left,
        _ => equip.tactical_left,
    };
    let c4_waiting = ability == Ability::Lethal && equip.c4_out;
    if systems.active != System::Modern
        || (left == 0 && !c4_waiting && equip.cooking.is_none() && !equip.throwing)
    {
        systems.grenade = None;
        return;
    }
    let button = if side == 1 { input.ads } else { input.fire };
    let into = if ability == Ability::Lethal {
        &mut input.lethal
    } else {
        &mut input.tactical
    };
    into.pressed |= button.pressed;
    into.held |= button.held;
    into.released |= button.released;
    // The gun neither fires nor aims with that hand's button.
    if side == 1 {
        input.ads = Control::default();
    } else {
        input.fire = Control::default();
    }
}

/// The active system's bar on the main bar's first six buttons, ready mode in Classic, the
/// abilities' looks, the write rule, and a loaded bar made whole once a session.
#[allow(clippy::too_many_arguments)]
fn show_bar(
    state: Res<FpsState>,
    systems: Res<ActionSystems>,
    keymap: Res<Keymap>,
    form: Res<crate::forms::PlayerForm>,
    gun_hand: Res<crate::cac::Hand>,
    equip: Res<crate::equipment::EquipState>,
    streak: Res<crate::streaks::Streak>,
    companion: Res<crate::companion_bar::CompanionBarLooks>,
    bar: Option<ResMut<FusionActionBar>>,
    mut seeded: Local<Option<(u64, Option<u16>)>>,
) {
    let Some(mut bar) = bar else { return };
    if bar.write_rule.is_none() {
        bar.write_rule = Some(write_allowed);
    }
    let view = state.active.then(|| MainBarView {
        first: systems.shown().first(systems.page()),
        shown: SLOTS as u8,
        keys: SLOT_ACTIONS
            .iter()
            .map(|a| {
                keymap
                    .keys(*a)
                    .first()
                    .map_or_else(String::new, |k| short_key(k))
            })
            .collect(),
        page_text: format!("{}{}", &systems.shown().name()[..1], systems.page() + 1),
    });
    if bar.main_bar != view {
        bar.main_bar = view;
    }
    let ready = state.active && systems.shown() == System::Classic && !form.handless();
    if bar.ready_mode != ready {
        bar.ready_mode = ready;
    }
    let live = SlotLive {
        hand: gun_hand.0,
        streak_ready: !streak.ready.is_empty(),
        lethal_left: equip.lethal_left,
        tactical_left: equip.tactical_left,
        grenade: systems.grenade.map(|g| g.0),
        companion: &companion.looks,
    };
    let looks: HashMap<u32, FusionLook> = Ability::ALL
        .into_iter()
        .map(|a| (a.id(), slot_look(a, &live)))
        .collect();
    if bar.looks != looks {
        bar.looks = looks;
    }
    // Once per character and session after the saved bar arrives, and again whenever the tools
    // the character has and knows change (a tool looted or sold, a trade learned).
    let key = state.self_guid.map(|me| (me, systems.usable_tools));
    match (key, bar.loaded) {
        // Not while earlier writes wait to be saved: the saved slots don't show them yet.
        (Some(key), true) if *seeded != Some(key) && bar.writes.is_empty() => {
            *seeded = Some(key);
            let writes = seed_writes(&bar.saved, systems.usable_tools);
            if !writes.is_empty() {
                info!("benilla-mw2: action bars: {} slot(s) set", writes.len());
                bar.writes.extend(writes);
            }
        }
        (_, false) => *seeded = None,
        _ => {}
    }
}

/// What a slot's look reads of the game this frame.
pub struct SlotLive<'a> {
    /// The gun in hand (0 primary, 1 secondary, 2 underbarrel).
    pub hand: u8,
    pub streak_ready: bool,
    pub lethal_left: u32,
    pub tactical_left: u32,
    /// The grenade readied in a hand ([`ActionSystems::grenade`]): its slot lit, the gun's not.
    /// Which hand, and every slot's equip outline, is crate::hand_slots.
    pub grenade: Option<Ability>,
    /// The Companion slots' looks (crate::companion_bar).
    pub companion: &'a HashMap<u32, FusionLook>,
}

/// How one of the layer's abilities looks on its button (icon, count, lit, usable), in every
/// system: the one place a layer slot is rendered.
pub fn slot_look(a: Ability, live: &SlotLive) -> FusionLook {
    if a.pet_slot().is_some() {
        return live.companion.get(&a.id()).cloned().unwrap_or(FusionLook {
            texture: String::new(),
            count: 0,
            checked: false,
            usable: false,
        });
    }
    let (checked, usable, count) = match a {
        Ability::Primary => (live.hand == 0 && live.grenade.is_none(), true, 0),
        Ability::Secondary => (live.hand == 1 && live.grenade.is_none(), true, 0),
        Ability::Underbarrel => (live.hand == 2 && live.grenade.is_none(), true, 0),
        Ability::Killstreak => (false, live.streak_ready, 0),
        Ability::Lethal => (live.grenade == Some(a), live.lethal_left > 0, live.lethal_left),
        Ability::Tactical => (live.grenade == Some(a), live.tactical_left > 0, live.tactical_left),
        _ => (false, true, 0),
    };
    FusionLook {
        texture: a.icon().to_string(),
        count,
        checked,
        usable,
    }
}

/// How the interface names the key that uses an ability: its slot's key on the bar, with the page
/// when it is not the first ("Q", "1 (page 2)"); its button by name when it is on none.
pub fn ability_key(bar: Option<&FusionActionBar>, keymap: &Keymap, ability: Ability) -> String {
    let found = bar.and_then(|bar| {
        ability
            .systems()
            .iter()
            .flat_map(|s| s.actions().map(move |a| (*s, a)))
            .find(|(_, a)| bar.fusion_slots.get(a) == Some(&ability.id()))
    });
    let Some((system, action)) = found else {
        return format!("the {} button", ability.name());
    };
    let Some((page, slot)) = system.place_of(action) else {
        return format!("the {} button", ability.name());
    };
    let key = keymap.label(SLOT_ACTIONS[slot]);
    if page == 0 {
        key
    } else {
        format!("{key} (page {})", page + 1)
    }
}

/// A key as the button's corner shows it: short (`Alt+MWU` would not fit; the slot keys are
/// plain keys).
pub(crate) fn short_key(chord: &str) -> String {
    let text = crate::keybinds::key_text(chord);
    if text.chars().count() <= 4 {
        text
    } else {
        text.chars().take(4).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_bars_are_six_slot_pages_in_their_own_ranges() {
        assert_eq!(System::Classic.pages(), 10);
        assert_eq!(System::Utility.pages(), 3);
        assert_eq!(System::Modern.pages(), 3);
        assert_eq!(System::Classic.first(0), 1);
        assert_eq!(System::Classic.first(1), 7, "the kit's 12 spells are pages 1 and 2");
        assert_eq!(System::Utility.first(1), 67);
        assert_eq!(System::Modern.first(2), 205);
        assert_eq!(System::Modern.first(3), 193, "wraps");
        assert_eq!(System::Utility.first(2), 211, "Utility's third page");
        for s in System::ALL {
            for a in s.actions() {
                assert_eq!(System::of_action(a), Some(s));
            }
        }
        assert_eq!(System::Utility.actions().count(), 18);
        // The druid's form pages (bonus bars 1-10: 73..192) are no system's.
        assert_eq!(System::of_action(73), None);
        assert_eq!(System::of_action(192), None);
        // Inside the server's 216 slots.
        assert_eq!(System::Utility.actions().max(), Some(216));
        assert_eq!(System::Modern.actions().max(), Some(210));
    }

    #[test]
    fn x_switches_and_z_puts_away_and_draws_again() {
        let mut s = ActionSystems::default();
        assert_eq!(s.active, System::Modern);
        s.switch();
        assert_eq!(s.active, System::Classic);
        s.switch();
        assert_eq!(s.active, System::Modern);
        // Z from Modern: Utility, and Z again: Modern.
        s.sheathe();
        assert_eq!(s.active, System::Utility);
        s.sheathe();
        assert_eq!(s.active, System::Modern);
        // Z from Classic returns to Classic.
        s.switch();
        s.sheathe();
        assert_eq!(s.active, System::Utility);
        s.sheathe();
        assert_eq!(s.active, System::Classic);
        // X in Utility draws the other armed system; Z then returns to that one.
        s.sheathe();
        s.switch();
        assert_eq!(s.active, System::Modern);
        s.sheathe();
        s.sheathe();
        assert_eq!(s.active, System::Modern);
    }

    #[test]
    fn each_system_pages_its_own_bar() {
        let mut s = ActionSystems::default();
        s.turn_page(1);
        assert_eq!((s.page(), s.action_of(0)), (1, 199));
        s.switch();
        assert_eq!(s.page(), 0, "Classic keeps its own page");
        assert_eq!(s.action_of(5), 6);
        s.turn_page(-1);
        assert_eq!(s.page(), 9, "wraps backwards");
        assert_eq!(s.action_of(0), 55);
        s.switch();
        assert_eq!(s.page(), 1, "Modern's page kept");
        s.sheathe();
        s.turn_page(3);
        assert_eq!(s.page(), 0, "Utility's three pages wrap");
    }

    #[test]
    fn abilities_go_only_on_their_own_bar() {
        let gun = packed(Ability::Primary);
        let fists = packed(Ability::BareHands);
        let fireball = 133; // a spell: kind 0
        let item = (0x80 << 24) | 6948;
        assert!(write_allowed(193, gun));
        assert!(!write_allowed(1, gun), "no gun on Classic's bar");
        assert!(!write_allowed(61, gun), "nor on Utility's");
        assert!(!write_allowed(80, gun), "nor on a form page");
        assert!(write_allowed(61, fists) && write_allowed(1, fists));
        assert!(!write_allowed(193, fists), "no bare hands on Modern's bar");
        assert!(write_allowed(1, fireball) && write_allowed(60, item));
        assert!(!write_allowed(193, fireball), "no spell on Modern's bar");
        assert!(!write_allowed(70, item), "nor an item on Utility's");
        assert!(write_allowed(80, fireball), "a form page is WoW's");
        assert!(!write_allowed(193, packed(Ability::Primary) + 100), "no unknown ability");
    }

    #[test]
    fn a_fresh_bar_is_seeded_and_a_saved_one_round_trips() {
        let mut saved: HashMap<u32, u32> = HashMap::new();
        saved.insert(1, 133); // the kit's first spell
        saved.insert(200, 133); // a spell left on what is now Modern's bar
        let writes = seed_writes(&saved, None);
        assert!(writes.contains(&(200, 0)), "the stray spell leaves");
        let mut bars = saved.clone();
        for (a, p) in &writes {
            if *p == 0 {
                bars.remove(a);
            } else {
                bars.insert(*a, *p);
            }
        }
        // Modern's first page as the defaults have it, and bare hands on Utility's.
        let expect = [
            (193, Ability::Primary),
            (194, Ability::Secondary),
            (195, Ability::Underbarrel),
            (196, Ability::Killstreak),
            (197, Ability::Tactical),
            (198, Ability::Lethal),
            (199, Ability::SpecialAmmo),
            (200, Ability::Rearm),
            (61, Ability::BareHands),
        ];
        for (at, a) in expect {
            assert_eq!(bars.get(&at), Some(&packed(a)), "{a:?}");
        }
        assert_eq!(bars.get(&1), Some(&133), "Classic untouched");
        // Saved and loaded back (the server stores the slots): nothing more to write.
        assert!(seed_writes(&bars, None).is_empty());
        // A player's own order survives: the lethal moved to page 2, the primary to slot 6.
        let mut moved = bars.clone();
        moved.remove(&198);
        moved.remove(&193);
        moved.insert(205, packed(Ability::Lethal));
        moved.insert(198, packed(Ability::Primary));
        assert!(seed_writes(&moved, None).is_empty());
        // One dragged off the bar comes back in its home slot, or the first free one.
        let mut lost = moved.clone();
        lost.remove(&205);
        assert_eq!(seed_writes(&lost, None), vec![(193, packed(Ability::Lethal))]);
        lost.insert(193, packed(Ability::Underbarrel));
        lost.remove(&195);
        lost.insert(195, (0x40 << 24) | 1); // a macro is no Modern entry
        assert_eq!(
            seed_writes(&lost, None),
            vec![(195, 0), (195, packed(Ability::Lethal))],
            "the macro leaves, and the lethal takes the first free slot"
        );
    }

    /// The bar after the writes.
    fn applied(saved: &HashMap<u32, u32>, writes: &[(u32, u32)]) -> HashMap<u32, u32> {
        let mut bars = saved.clone();
        for (a, p) in writes {
            if *p == 0 {
                bars.remove(a);
            } else {
                bars.insert(*a, *p);
            }
        }
        bars
    }

    /// The tools on Utility's bar.
    fn tools_on(bars: &HashMap<u32, u32>) -> Vec<Ability> {
        let mut out: Vec<Ability> = System::Utility
            .actions()
            .filter_map(|a| bars.get(&a))
            .filter_map(|p| Ability::from_id(p & 0x00FF_FFFF))
            .filter(|a| a.is_tool())
            .collect();
        out.sort_by_key(|a| a.id());
        out
    }

    #[test]
    fn utility_shows_only_the_tools_had_and_trained() {
        // The server's mask: bit n the tool MiningPick + n.
        assert_eq!(tool_bit(Ability::MiningPick), 1);
        assert_eq!(tool_bit(Ability::RepairHammer), 1 << 6);
        assert_eq!(tool_bit(Ability::BareHands), 0);
        assert_eq!(decode_tools(&[OP_TOOLS, 0x41, 0]), Some(0x41));
        assert_eq!(decode_tools(&[OP_TOOLS, 1]), None);
        assert_eq!(decode_tools(&[OP_TOOL, 1, 0]), None);
        // A fresh character with nothing: bare hands alone.
        let fresh = applied(&HashMap::new(), &seed_writes(&HashMap::new(), Some(0)));
        assert!(tools_on(&fresh).is_empty());
        assert_eq!(fresh.get(&61), Some(&packed(Ability::BareHands)));
        // A miner with a pick, and a builder's Repair Hammer.
        let mask = tool_bit(Ability::MiningPick) | tool_bit(Ability::RepairHammer);
        let miner = applied(&fresh, &seed_writes(&fresh, Some(mask)));
        assert_eq!(
            tools_on(&miner),
            [Ability::MiningPick, Ability::RepairHammer]
        );
        assert!(seed_writes(&miner, Some(mask)).is_empty(), "nothing more to do");
        // The pick sold: its button goes, no dead button left; the hammer stays where it is.
        let hammer = tool_bit(Ability::RepairHammer);
        let writes = seed_writes(&miner, Some(hammer));
        let sold = applied(&miner, &writes);
        assert_eq!(tools_on(&sold), [Ability::RepairHammer]);
        assert!(writes.iter().all(|(_, p)| *p == 0), "{writes:?}");
        assert_eq!(sold.get(&61), Some(&packed(Ability::BareHands)));
        // Herbalism learned: it comes on (it asks no item; the server says so in the mask).
        let herbs = hammer | tool_bit(Ability::Herbalism);
        let herbalist = applied(&sold, &seed_writes(&sold, Some(herbs)));
        assert_eq!(
            tools_on(&herbalist),
            [Ability::Herbalism, Ability::RepairHammer]
        );
        // Every tool had: every tool and instrument, as the defaults lay them.
        let all = applied(&herbalist, &seed_writes(&herbalist, Some(0x1FFF)));
        assert_eq!(tools_on(&all).len(), 13);
        assert_eq!(all.get(&212), Some(&packed(Ability::ThunderDrums)));
        assert_eq!(all.get(&69), Some(&packed(Ability::HandDrum)));
        // Not told yet: the tools are left as they are.
        assert!(seed_writes(&miner, None).is_empty());
    }

    #[test]
    fn a_tool_lost_from_the_hands_goes_back_to_bare_hands() {
        let mut s = ActionSystems::default();
        s.tool = Some(Ability::MiningPick);
        take_tools(&mut s, tool_bit(Ability::MiningPick));
        assert_eq!(s.tool, Some(Ability::MiningPick));
        take_tools(&mut s, tool_bit(Ability::FishingPole));
        assert_eq!(s.tool, None);
        assert_eq!(s.usable_tools, Some(tool_bit(Ability::FishingPole)));
    }

    fn bar() -> FusionActionBar {
        let mut b = FusionActionBar::default();
        b.spells.insert(1, 133);
        b.saved.insert(1, 133);
        b.saved.insert(2, (0x80 << 24) | 6948); // an item
        b.fusion_slots.insert(193, Ability::Lethal.id());
        b.saved.insert(193, packed(Ability::Lethal));
        b.fusion_slots.insert(61, Ability::BareHands.id());
        b
    }

    #[test]
    fn a_slot_key_does_what_its_system_holds() {
        let b = bar();
        let mut s = ActionSystems::default();
        assert_eq!(
            slot_use(&s, &b, None, 0, false),
            SlotUse::Ability(Ability::Lethal)
        );
        assert_eq!(slot_use(&s, &b, None, 1, false), SlotUse::Nothing);
        s.switch();
        // Classic: the spell into the main hand, with Alt the off hand.
        assert_eq!(
            slot_use(&s, &b, None, 0, false),
            SlotUse::Ready(ReadiedAction {
                action: 1,
                spell_id: 133,
                right_button: false
            })
        );
        assert_eq!(
            slot_use(&s, &b, None, 0, true),
            SlotUse::Ready(ReadiedAction {
                action: 1,
                spell_id: 133,
                right_button: true
            })
        );
        assert_eq!(slot_use(&s, &b, None, 1, false), SlotUse::Use(2), "the item is used");
        assert_eq!(slot_use(&s, &b, None, 2, false), SlotUse::Nothing);
        s.sheathe();
        assert_eq!(
            slot_use(&s, &b, None, 0, false),
            SlotUse::Ability(Ability::BareHands)
        );
        // A cat (bonus bar 1): its page's slots, the second half after a page turn.
        assert_eq!(slot_use(&s, &b, Some(1), 0, false), SlotUse::Use(73));
        s.form_half = 1;
        assert_eq!(slot_use(&s, &b, Some(1), 5, false), SlotUse::Use(84));
        assert_eq!(slot_use(&s, &b, Some(3), 0, false), SlotUse::Use(103));
    }

    #[test]
    fn an_abilitys_key_is_its_slots() {
        let k = Keymap::defaults();
        let mut b = FusionActionBar::default();
        assert_eq!(
            ability_key(Some(&b), &k, Ability::Killstreak),
            "the Call in Killstreak button"
        );
        b.fusion_slots.insert(196, Ability::Killstreak.id());
        assert_eq!(ability_key(Some(&b), &k, Ability::Killstreak), "4");
        b.fusion_slots.clear();
        b.fusion_slots.insert(203, Ability::Killstreak.id());
        assert_eq!(ability_key(Some(&b), &k, Ability::Killstreak), "Q (page 2)");
    }

    #[test]
    fn a_held_grenade_key_cooks_and_a_click_taps() {
        let mut input = Mw2Input::default();
        let mut s = ActionSystems::default();
        let down = Control {
            pressed: true,
            held: true,
            released: false,
        };
        // A grenade's key readies it in the main hand; the slot key itself throws nothing.
        apply_ability(&mut input, &mut s, Ability::Lethal, down, false);
        assert!(!input.lethal.pressed);
        assert_eq!(s.grenade, Some((Ability::Lethal, 0)));
        apply_ability(&mut input, &mut s, Ability::Primary, down, false);
        assert_eq!(input.hand, Some(0));
        assert_eq!(s.grenade, None, "a gun picked puts the grenade back");
        apply_ability(&mut input, &mut s, Ability::SpecialAmmo, down, false);
        assert!(input.armory_ammo.pressed);
        apply_ability(&mut input, &mut s, Ability::Rearm, down, false);
        assert!(input.rearm.pressed);
        // Bare hands on the Classic bar sheathe; on Utility's they are already out.
        s.switch();
        apply_ability(&mut input, &mut s, Ability::BareHands, down, false);
        assert_eq!(s.active, System::Utility);
        assert!(!s.fists_up, "into Utility the hands are relaxed");
        apply_ability(&mut input, &mut s, Ability::BareHands, down, false);
        assert_eq!(s.active, System::Utility);
        assert!(s.fists_up, "Hands equipped: fists up");
        apply_ability(&mut input, &mut s, Ability::BareHands, down, false);
        assert!(!s.fists_up, "and toggled back down");
        apply_ability(&mut input, &mut s, Ability::MiningPick, down, false);
        apply_ability(&mut input, &mut s, Ability::BareHands, down, false);
        assert_eq!((s.tool, s.fists_up), (None, true), "a tool put away into fists");
    }

    #[test]
    fn a_readied_grenade_is_thrown_by_its_hands_button() {
        let mut s = ActionSystems::default();
        let mut equip = crate::equipment::EquipState::default();
        equip.lethal_left = 2;
        equip.tactical_left = 1;
        let tap = Control {
            pressed: true,
            held: true,
            released: false,
        };
        let mut input = Mw2Input::default();
        apply_ability(&mut input, &mut s, Ability::Tactical, tap, true);
        assert_eq!(s.grenade, Some((Ability::Tactical, 0)), "Alt too: the main hand");
        // The left button throws it; the right button still aims.
        input.fire = tap;
        grenade_controls(&mut input, &mut s, &equip);
        assert!(input.tactical.pressed && !input.fire.pressed);
        // The last one thrown: the hand stays up through the throw's motion...
        equip.tactical_left = 0;
        equip.throwing = true;
        grenade_controls(&mut Mw2Input::default(), &mut s, &equip);
        assert_eq!(s.grenade, Some((Ability::Tactical, 0)), "the throw plays out");
        // ...then, none left, the grenade goes back, the gun in hand.
        equip.throwing = false;
        grenade_controls(&mut Mw2Input::default(), &mut s, &equip);
        assert_eq!(s.grenade, None);
        // The lethal in the main hand: the left button cooks it, held.
        apply_ability(&mut input, &mut s, Ability::Lethal, tap, false);
        let mut input = Mw2Input::default();
        input.fire = tap;
        grenade_controls(&mut input, &mut s, &equip);
        assert!(input.lethal.pressed && input.lethal.held && !input.fire.pressed);
        // Its key again puts it back.
        apply_ability(&mut input, &mut s, Ability::Lethal, tap, false);
        assert_eq!(s.grenade, None);
        // Leaving Modern puts it back too.
        apply_ability(&mut input, &mut s, Ability::Lethal, tap, false);
        s.switch();
        assert_eq!(s.grenade, None);
    }
}
