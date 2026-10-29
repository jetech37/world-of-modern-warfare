//! The kit window (Create-a-Class, MW2 design spec 2.1; a kit is WOMW's name for a class), drawn
//! with bevy_ui over the world in the stock dialog look. It opens two ways ([`crate::panels`]):
//!
//! - the HUD's Kits button ([`Access::Hud`]): anywhere, to look. Editing, saving and switching are
//!   greyed, and a line says to visit a trainer or innkeeper; the field editor (once an hour)
//!   turns the window to editing for a short while;
//! - "Edit your kits" in any trainer's or innkeeper's gossip ([`Access::Npc`], the server's world
//!   op 40): everything enabled. The server still checks every save and switch where the player
//!   stands; walking away greys the window again.
//!
//! **K** opens and closes it from anywhere (as the Kits micro button does), **N** opens it on
//! its Talents page and **P** on its Abilities page, in place of the stock talent frame and
//! spellbook. **Esc** closes it (an open list first), and while it is open the cursor is out and
//! the gun lowered. The window keeps one size whatever is chosen (WOMW design, section 3).
//!
//! Over the pages, the kit's name is a dropdown (`UIDropDownMenuTemplate`): the five kits, then
//! Rename, New, Copy and Delete. Four pages hang under the frame:
//!
//! - **Loadout**: on the left every part of the kit ([`parts::PARTS`]: the guns, their
//!   attachments and camo, the equipment, the three perks, the deathstreak and the three
//!   killstreaks), each with its pick's icon and arrows; on the right the sub-menu of the part
//!   last clicked: all its options (icons, names, locked ones in red), and the highlighted one's
//!   details ([`describe`]): what it does, its numbers against the current pick, how a perk
//!   stacks and what its Pro does, and what unlocks it, with **Assign to Kit** (and Regular / Pro
//!   for a perk). Up and down walk the parts, left and right (and the row's arrows) move the
//!   highlight, Enter assigns it (or, on the current pick, saves and equips the kit);
//! - **Talents** picks the kit's six class options, one per tier, from those bought at the class
//!   trainer (crate::classes), each with a stock-looking tooltip ([`tip`]);
//! - **Abilities** lists everything the character can use, to drag onto the action bar
//!   ([`abilities`]);
//! - **Killstreaks** shows the kit's three rewards and their kill costs against the streak this
//!   life.
//!
//! **Save & Equip** sends the kit to the server, which validates, saves and equips it
//! (crate::cac); **Save** keeps it without equipping. What the server says (refusals, a wait for
//! combat to end) shows in the window, never the chat.
//!
//! This window is the reference for the layer's others: its sections are headed and set in
//! insets, its lists show icons, and every control that is not plain says what it does on hover
//! (crate::wow_ui).

mod abilities;
pub mod describe;
pub mod parts;
pub(crate) mod tip;

use std::collections::HashMap;

use benilla_app::fusion::{
    CHANNEL_BUILD, ChannelMessage, ClassLoadout, LoadoutError, SendFusionChannel, SendFusionLoadout,
};
use bevy::prelude::*;
use bevy::ui::RelativeCursorPosition;

use crate::Mw2Set;
use crate::cac::Classes;
use crate::catalog::{Catalog, Gun};
use crate::classes::{ClassPage, Kind, TIERS, tier_level, toggle_pick};
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::panels::{Access, KitPage, OpenPanel, Panel, ShowKitPage};

use describe::{Ctx, Details, LOCKED};
use parts::{Choice, PARTS, Part};

use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, Kind as ButtonKind, WHITE, WindowSpec,
    WowButton, WowTip, text, u,
};

/// The pages' indices (`CacUi::page`), in their tabs' order.
const LOADOUT: u8 = KitPage::Loadout.index();
const TALENTS: u8 = KitPage::Talents.index();
const KILLSTREAKS: u8 = KitPage::Killstreaks.index();

pub use tip::{TokenTables, option_text, talent_tip};

/// Where kits may change, as the window last learnt it: what opened it, then where the player
/// walked and what the server said.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub enum KitAccess {
    /// The HUD's button: look only.
    #[default]
    View,
    /// A trainer's or innkeeper's "Edit your kits".
    Station,
    /// The field editor's window.
    Field,
}

impl KitAccess {
    /// The access a world op 40 message grants, and the field editor's seconds: `[40, 0]` the
    /// HUD's view, `[40, 2, secs u16]` the field editor, else (1, or a bare `[40]`) a station.
    pub fn of_open(body: &[u8]) -> (Self, u16) {
        match body.get(1) {
            Some(0) => (Self::View, 0),
            Some(2) => {
                let secs = match body.get(2..4) {
                    Some(b) => u16::from_le_bytes([b[0], b[1]]),
                    None => 0,
                };
                (Self::Field, secs)
            }
            _ => (Self::Station, 0),
        }
    }

    /// The panel access the opener stands for.
    pub fn panel_access(self) -> Access {
        match self {
            Self::View => Access::Hud,
            Self::Station | Self::Field => Access::Npc,
        }
    }
}

/// World op 40, `[40, opener, ...]`: a station or the field editor opens the window (the server's
/// `Fusion::kOpOpenClasses`, `Fusion::OpenKitEditor`).
pub const OP_OPEN_CLASSES: u8 = 40;
/// World op 42: the kit status (the server's `Fusion::kOpKitStatus`).
pub const OP_KIT_STATUS: u8 = 42;
/// Build channel ops (the server's `Fusion::kOpFieldEdit`, `kOpAskKitStatus`).
pub const OP_FIELD_EDIT: u8 = 31;
pub const OP_ASK_KIT_STATUS: u8 = 32;
/// The kit status flags (the server's `kKitStatus*`).
pub const STATUS_CAN_EDIT: u8 = 0x01;
pub const STATUS_FIELD_OPEN: u8 = 0x02;
pub const STATUS_GM: u8 = 0x04;

/// What the server says of where kits change (world op 42).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct KitStatus {
    pub flags: u8,
    /// Seconds until the field editor can be used again (0: ready).
    pub field_ready_in: u32,
    /// Seconds left in the field editor's open window.
    pub field_open_for: u16,
}

/// `[42, flags u8, ready in u32, open for u16]`.
pub fn decode_kit_status(b: &[u8]) -> Option<KitStatus> {
    if b.len() != 8 || b[0] != OP_KIT_STATUS {
        return None;
    }
    Some(KitStatus {
        flags: b[1],
        field_ready_in: u32::from_le_bytes([b[2], b[3], b[4], b[5]]),
        field_open_for: u16::from_le_bytes([b[6], b[7]]),
    })
}

/// The kit being edited.
#[derive(Resource, Debug, Default)]
pub struct CacUi {
    pub open: bool,
    pub slot: u8,
    pub draft: ClassLoadout,
    /// The kit's name is being typed.
    pub renaming: bool,
    /// The part the sub-menu shows (an index into [`PARTS`]).
    pub part: usize,
    /// The highlighted option of that part's sub-menu (an index into [`parts::options`]).
    pub cursor: usize,
    /// The Pro toggle for the highlighted perk (Regular or Pro).
    pub want_pro: bool,
    /// The kits dropdown's list is open.
    pub dropdown: bool,
    /// Where kits may change.
    pub access: KitAccess,
    /// When the field editor's window closes (elapsed seconds).
    pub field_until: f32,
    /// The server's last kit status, and when it came.
    pub status: Option<KitStatus>,
    pub status_at: f32,
    /// What the window says of its own (the station left behind, the field editor's window shut).
    pub note: Option<String>,
    /// The page shown ([`KitPage`]'s index).
    pub page: u8,
    /// The access the server's last open message granted, for the `OpenPanel` it wrote.
    pending: Option<(KitAccess, u16)>,
    /// When the access was last granted, and the status last asked for (elapsed seconds).
    granted_at: f32,
    asked_at: f32,
    /// The window was built.
    built: bool,
    /// Delete was pressed: the popup asks before the kit goes.
    pub confirm_delete: bool,
}

impl CacUi {
    /// A GM's status: kits change anywhere.
    pub fn gm(&self) -> bool {
        self.status.is_some_and(|s| s.flags & STATUS_GM != 0)
    }

    /// Whether the window edits: opened by a station or the field editor (or a GM's).
    pub fn can_edit(&self) -> bool {
        self.access != KitAccess::View || self.gm()
    }

    /// Seconds until the field editor is ready again at `t` (elapsed seconds); 0 ready or unknown.
    pub fn field_ready_in(&self, t: f32) -> u32 {
        self.status.map_or(0, |s| {
            (s.field_ready_in as f32 - (t - self.status_at))
                .max(0.0)
                .ceil() as u32
        })
    }

    /// The part the sub-menu shows.
    pub fn focused_part(&self) -> Part {
        PARTS[self.part.min(PARTS.len() - 1)]
    }

    /// Show another kit slot: the saved kit there, or a fresh one under its default name.
    fn show_slot(&mut self, slot: u8, classes: &Classes) {
        self.confirm_delete = false;
        self.slot = slot;
        self.draft = classes.classes[usize::from(slot)]
            .clone()
            .unwrap_or_else(|| ClassLoadout {
                name: empty_kit_name(slot),
                ..Default::default()
            });
        self.renaming = false;
    }

    /// Turn the sub-menu to `part`, the highlight on its current pick.
    pub fn focus_part(&mut self, catalog: &Catalog, part: usize) {
        self.part = part.min(PARTS.len() - 1);
        self.cursor = cursor_on_current(catalog, &self.draft, self.focused_part());
        self.want_pro = self.pro_of_current();
    }

    /// Whether the focused perk slot plays its Pro now.
    fn pro_of_current(&self) -> bool {
        match parts::current(&self.draft, self.focused_part()) {
            Choice::Perk(p) if matches!(self.focused_part(), Part::Perk(_)) => {
                parts::plays_pro(&self.draft, p)
            }
            _ => false,
        }
    }

    /// The highlighted option of the sub-menu.
    pub fn highlighted(&self, catalog: &Catalog) -> Choice {
        let opts = parts::options(catalog, &self.draft, self.focused_part());
        opts.get(self.cursor.min(opts.len().saturating_sub(1)))
            .cloned()
            .unwrap_or_else(|| parts::current(&self.draft, self.focused_part()))
    }

    /// Move the highlight `dir` options along the sub-menu (wrapping).
    pub fn step_cursor(&mut self, catalog: &Catalog, dir: i32) {
        let n = parts::options(catalog, &self.draft, self.focused_part())
            .len()
            .max(1) as i32;
        self.cursor = ((self.cursor.min(n as usize - 1) as i32 + dir).rem_euclid(n)) as usize;
        // A perk's toggle follows the perk the highlight is on: its Pro if the kit plays it.
        if let Choice::Perk(p) = self.highlighted(catalog) {
            self.want_pro = parts::plays_pro(&self.draft, p);
        }
    }
}

/// The index of a part's current pick in its options (0, "none", when not among them).
pub fn cursor_on_current(catalog: &Catalog, kit: &ClassLoadout, part: Part) -> usize {
    let cur = parts::current(kit, part);
    parts::options(catalog, kit, part)
        .iter()
        .position(|c| *c == cur)
        .unwrap_or(0)
}

/// Why the highlighted option can't be assigned now, `None` when it can: viewing only, a
/// part closed (no room, no gun), an option blocked (doesn't stack, doesn't go with) or locked,
/// or a Pro not yet earned.
pub fn assign_refusal(
    catalog: &Catalog,
    unlocks: &crate::achievements::Unlocks,
    kit: &ClassLoadout,
    part: Part,
    choice: &Choice,
    pro: bool,
) -> Option<String> {
    if let Some(why) = parts::blocked(catalog, kit, part, choice) {
        return Some(why);
    }
    if !parts::unlocked(unlocks, kit, part, choice) {
        return Some("Locked.".into());
    }
    if let (Part::Perk(_), Choice::Perk(p)) = (part, choice)
        && pro
        && !unlocks.perk_pro(*p)
    {
        return Some("Its Pro is not earned yet.".into());
    }
    None
}

/// Put the highlighted option in the kit (with the Pro toggle for a perk); false when it can't
/// go in.
pub fn assign_highlighted(
    ui: &mut CacUi,
    catalog: &Catalog,
    unlocks: &crate::achievements::Unlocks,
) -> bool {
    let part = ui.focused_part();
    let choice = ui.highlighted(catalog);
    let pro = ui.want_pro && matches!(part, Part::Perk(_));
    if assign_refusal(catalog, unlocks, &ui.draft, part, &choice, pro).is_some() {
        return false;
    }
    parts::assign(catalog, &mut ui.draft, part, &choice, pro);
    // The options may have moved (a new gun's attachments): the highlight stays on the pick.
    ui.cursor = cursor_on_current(catalog, &ui.draft, part);
    true
}

/// A kit slot's name when it holds none yet.
pub fn empty_kit_name(slot: u8) -> String {
    format!("Kit {}", slot + 1)
}

/// What a button does.
#[derive(Component, Clone, Copy, Debug, PartialEq, Eq)]
enum Action {
    /// Show a page ([`KitPage`]'s index).
    Page(u8),
    /// Pick (or drop) a tier's option: tier 1..=6, archetype 0..3.
    Talent(u8, u8),
    /// Cycle a killstreak slot (0..3) on the Killstreaks page.
    Streak(u8, i32),
    Equip,
    /// Save without equipping.
    Save,
    Close,
    /// Ask the server for the field editor.
    FieldEdit,
    /// Open or close the kits dropdown.
    DropDown,
    /// Show a kit (the dropdown's lines).
    KitSlot(u8),
    Rename,
    NewKit,
    CopyKit,
    DeleteKit,
    /// The delete popup's Yes and No.
    ConfirmDelete,
    CancelDelete,
    /// Show a part's sub-menu (an index into [`PARTS`]).
    FocusPart(u8),
    /// A part's arrows: show its sub-menu and move the highlight.
    Step(u8, i32),
    /// Highlight a sub-menu option (its index).
    PickOption(u16),
    /// Put the highlighted option in the kit.
    Assign,
    /// The perk toggle: Regular (false) or Pro (true).
    Pro(bool),
}

impl Action {
    /// Whether it changes a kit (greyed when the window only views).
    fn edits(self) -> bool {
        matches!(
            self,
            Self::Talent(..)
                | Self::Streak(..)
                | Self::Equip
                | Self::Save
                | Self::Rename
                | Self::NewKit
                | Self::CopyKit
                | Self::DeleteKit
                | Self::ConfirmDelete
                | Self::Assign
                | Self::Pro(_)
        )
    }

    /// Whether it belongs to the kits dropdown (pressing anything else closes its list).
    fn in_dropdown(self) -> bool {
        matches!(
            self,
            Self::DropDown
                | Self::KitSlot(_)
                | Self::Rename
                | Self::NewKit
                | Self::CopyKit
                | Self::DeleteKit
        )
    }
}

#[derive(Component)]
struct CacRoot;

/// The popup asking before a kit is deleted.
#[derive(Component)]
struct DeletePopup;

/// The delete popup (`StaticPopup`'s look) while [`CacUi::confirm_delete`] asks.
fn delete_popup(
    mut commands: Commands,
    assets: Res<AssetServer>,
    ui: Res<CacUi>,
    roots: Query<Entity, With<DeletePopup>>,
) {
    let want = ui.open && ui.confirm_delete;
    if want == !roots.is_empty() {
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
        "kit delete",
        &delete_question(&ui.draft.name),
        DeletePopup,
        ("Yes", Action::ConfirmDelete),
        ("No", Action::CancelDelete),
    );
}

/// What the delete popup asks.
fn delete_question(name: &str) -> String {
    format!("Delete the kit \"{name}\"? Its guns, perks and talents are cleared.")
}

/// A text the window keeps up to date, by what it shows.
#[derive(Component, Clone, Copy, Debug, PartialEq, Eq, Hash)]
enum Txt {
    /// A part's pick, on its row.
    PartPick(u8),
    /// The sub-menu's title and the line under it.
    SubTitle,
    SubHint,
    /// A kits dropdown line.
    DropRow(u8),
    Access,
    Status,
}

/// The kits dropdown's list.
#[derive(Component)]
struct DropList;

/// A part's pick's icon, on its row.
#[derive(Component)]
struct PartIcon(u8);

/// The sub-menu's options list (its scroll view), and its rows' contents by option index.
#[derive(Component)]
struct OptionList;
#[derive(Component)]
struct OptionText(u16);
#[derive(Component)]
struct OptionIcon(u16);

/// The sub-menu's details, rebuilt when the highlight changes.
#[derive(Component)]
struct DetailsBox;

/// The gun's stat bars in the details: a bar's fill, this option's (false) or the current
/// pick's under it (true).
#[derive(Component)]
struct BarFill(usize, bool);

/// The bars' block (hidden unless a gun is highlighted).
#[derive(Component)]
struct DetailBars;

/// The Regular / Pro toggle's row (shown for perks).
#[derive(Component)]
struct ProRow;

/// A class name after this frame's typing: printable characters appended, Backspace taking the
/// last one, at most `CLASS_NAME_MAX` bytes.
pub fn edit_name(name: &str, typed: &str, backspace: bool) -> String {
    let mut out = name.to_owned();
    if backspace {
        out.pop();
    }
    for c in typed.chars().filter(|c| !c.is_control()) {
        if out.len() + c.len_utf8() > benilla_app::fusion::CLASS_NAME_MAX {
            break;
        }
        out.push(c);
    }
    out
}

/// A page's column (one shows at a time), by [`KitPage`]'s index.
#[derive(Component)]
struct PagePanel(u8);

/// An option's button on the talent page, its name and its spell icon: tier 1..=6, archetype.
#[derive(Component)]
struct TalentButton(u8, u8);

#[derive(Component)]
struct TalentLabel(u8, u8);

#[derive(Component)]
struct TalentIcon(u8, u8);

/// One of the class's three core abilities over the tiers (hover for its tooltip).
#[derive(Component)]
struct CoreLabel(u8);

/// What hovering a node describes in the tooltip.
#[derive(Component, Clone, Copy, Debug, PartialEq, Eq)]
enum TipSource {
    Talent(u8, u8),
    Core(u8),
}

/// The tooltip, the window root's last child, so it draws over the rest.
#[derive(Component)]
struct TipBox;

/// The killstreak page's text: a slot's choice, a ladder row, the streak, the note.
#[derive(Component, Clone, Copy, Debug, PartialEq, Eq)]
enum StreakText {
    Slot(u8),
    Row(u8),
    Progress,
    Note,
}

/// A page tab's button, by [`KitPage`]'s index.
#[derive(Component)]
struct PageTab(u8);

/// MW2 icons decoded from the install, by material name (`None`: not in the install).
#[derive(Resource, Default)]
struct Icons(std::collections::HashMap<String, Option<Handle<Image>>>);

impl Icons {
    fn get(
        &mut self,
        name: &str,
        assets: &crate::mw2::Mw2Assets,
        images: &mut Assets<Image>,
    ) -> Option<Handle<Image>> {
        if let Some(h) = self.0.get(name) {
            return h.clone();
        }
        let crate::mw2::Mw2Assets::Ready(lib) = assets else {
            return None;
        };
        let h = lib
            .material_image(name)
            .map(|img| images.add(crate::scope::upload(&img)));
        self.0.insert(name.to_owned(), h.clone());
        h
    }
}

const STATS: [&str; 4] = ["Damage", "Range", "Fire Rate", "Mobility"];

/// The gun's stat bars, 0 to 1: damage against a kill, and range, fire rate and mobility against
/// the best in the catalog.
pub fn stat_bars(catalog: &Catalog, gun: &Gun) -> [f32; 4] {
    let bullet = |g: &Gun| !matches!(g.class.as_str(), "launcher" | "riot_shield");
    let max = |f: &dyn Fn(&Gun) -> f32| {
        catalog
            .weapons
            .iter()
            .filter(|g| bullet(g))
            .map(f)
            .fold(f32::EPSILON, f32::max)
    };
    let damage = |g: &Gun| g.damage * g.pellets as f32;
    let range = |g: &Gun| g.max_damage_range.min(150.0);
    let rate = |g: &Gun| 1.0 / g.fire_time.max(0.01);
    let mobility = |g: &Gun| g.move_speed_scale;
    [
        // Against a kill (100 health), as MW2's bar: a shotgun's pellets together fill it.
        (damage(gun) / 100.0).clamp(0.0, 1.0),
        (range(gun) / max(&range)).clamp(0.0, 1.0),
        (rate(gun) / max(&rate)).clamp(0.0, 1.0),
        (mobility(gun) / max(&mobility)).clamp(0.0, 1.0),
    ]
}

/// The choices for an attachment slot, "" first (none): the gun's attachments that are not the
/// other slot's, not incompatible with it, and that make a combination the install has.
pub fn attachment_choices(catalog: &Catalog, gun: &Gun, other: &str) -> Vec<String> {
    let mut out = vec![String::new()];
    for id in &gun.attachments {
        let ok = other.is_empty() || parts::compatible(catalog, id, other);
        if ok && catalog.variant(gun, &[id, other]).is_some() {
            out.push(id.clone());
        }
    }
    out
}

fn step<T: PartialEq + Clone>(choices: &[T], current: &T, dir: i32) -> T {
    let i = choices.iter().position(|c| c == current).unwrap_or(0) as i32;
    let n = choices.len().max(1) as i32;
    choices[((i + dir).rem_euclid(n)) as usize].clone()
}

// ── Killstreaks ──────────────────────────────────────────────────────────────────────────────────

/// The killstreak page's ladder rows: one per reward of the ladder.
pub const LADDER_ROWS: u8 = crate::streaks::REWARDS.len() as u8;

/// One reward a kit may choose (the server's `DefaultStreakLadder`, the HUD's `REWARDS`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StreakChoice {
    /// The server's `StreakId`.
    pub id: u16,
    /// The kills in a row it takes.
    pub kills: u32,
    /// The HUD's label ("UAV", "CHOPPER GUNNER").
    pub label: &'static str,
}

/// The ladder, in kill order.
pub fn streak_ladder() -> Vec<StreakChoice> {
    crate::streaks::REWARDS
        .iter()
        .filter_map(|(kills, label, _)| {
            Some(StreakChoice {
                id: crate::streaks::reward_id(label)?,
                kills: *kills,
                label,
            })
        })
        .collect()
}

/// A killstreak's name in the world (crate::arsenal), else MW2's.
pub fn streak_name(id: u16) -> String {
    let label = streak_ladder()
        .into_iter()
        .find(|c| c.id == id)
        .map_or("None", |c| c.label);
    crate::arsenal::arsenal()
        .streak(u32::from(id))
        .map_or_else(|| label.to_owned(), |l| l.name.clone())
}

/// The rewards a kit plays with (the server's `Fusion::KitStreaks`): the ones it chose, in kill
/// order, else every one the account has unlocked.
pub fn kit_streaks(kit: &ClassLoadout, unlocked: impl Fn(u16) -> bool) -> Vec<StreakChoice> {
    let chose = kit.streaks.iter().any(|&s| s != 0);
    streak_ladder()
        .into_iter()
        .filter(|c| {
            if chose {
                kit.streaks.contains(&c.id)
            } else {
                unlocked(c.id)
            }
        })
        .collect()
}

/// Cycle a kit's killstreak slot (0..3): none, then each reward the other two slots don't hold.
pub fn cycle_streak(kit: &mut ClassLoadout, slot: usize, dir: i32) {
    if slot >= kit.streaks.len() {
        return;
    }
    let others: Vec<u16> = (0..kit.streaks.len())
        .filter(|&i| i != slot)
        .map(|i| kit.streaks[i])
        .collect();
    let choices: Vec<u16> = std::iter::once(0)
        .chain(
            streak_ladder()
                .into_iter()
                .map(|c| c.id)
                .filter(|id| !others.contains(id)),
        )
        .collect();
    kit.streaks[slot] = step(&choices, &kit.streaks[slot], dir);
}

/// An equipment's name in the world (crate::arsenal): the frag is a Thorium Grenade.
pub fn equipment_name(catalog: &Catalog, id: u16) -> String {
    crate::arsenal::gun_name_by_id(catalog, id).unwrap_or_else(|| "None".into())
}

pub(crate) fn error_text(e: LoadoutError) -> &'static str {
    match e {
        LoadoutError::None => "",
        LoadoutError::UnknownWeapon => "That gun is not in the catalog",
        LoadoutError::WrongSlot => "That gun does not go in that slot",
        LoadoutError::Locked => "Not unlocked yet",
        LoadoutError::UnknownAttachment => "Unknown attachment",
        LoadoutError::AttachmentNotOnGun => "That gun has no such attachment combination",
        LoadoutError::TwoAttachmentsNeedBling => {
            "More attachments need Tinker's Rails in more perk slots"
        }
        LoadoutError::IncompatibleAttachments => "Those attachments do not go together",
        LoadoutError::UnknownCamo => "That gun takes no such camo",
        LoadoutError::NameTooLong => "The name is too long",
        LoadoutError::WrongPerk => "That perk does not go in that slot",
        LoadoutError::TooFast => "Saving too fast: wait a moment",
        LoadoutError::NotAtKitStation => {
            "Kits change only near a trainer or innkeeper, or with the field editor"
        }
        LoadoutError::UnknownTalent => "That talent can't go in this kit",
        LoadoutError::UnknownStreak => "That killstreak can't go in this kit",
        LoadoutError::PerkDoesNotStack => "That perk doesn't stack in that many slots",
        LoadoutError::ProLocked => "That Pro is not earned yet",
    }
}

/// `m:ss`.
fn clock(secs: u32) -> String {
    format!("{}:{:02}", secs / 60, secs % 60)
}

/// The line under the pages on where kits change: viewing from the HUD says to visit a trainer or
/// innkeeper (and when the field editor is ready); a station or the field editor says it edits.
pub fn access_line(access: KitAccess, gm: bool, field_ready_in: u32, field_left: u32) -> String {
    match access {
        KitAccess::View if gm => "GM: kits can change anywhere.".into(),
        KitAccess::View if field_ready_in > 0 => format!(
            "Viewing only. Visit a trainer or innkeeper to change kits. Field editor ready in {} min.",
            field_ready_in.div_ceil(60)
        ),
        KitAccess::View => {
            "Viewing only. Visit a trainer or innkeeper to change kits, or use the field editor (once an hour).".into()
        }
        KitAccess::Station => {
            "Kits can change here. To rearm, talk to a trainer or innkeeper.".into()
        }
        KitAccess::Field => format!(
            "Field editor: kits can change anywhere for {}. The first change starts its hour.",
            clock(field_left)
        ),
    }
}

pub struct CacUiPlugin;

impl Plugin for CacUiPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<CacUi>()
            .init_resource::<Icons>()
            .init_resource::<TokenTables>()
            .init_resource::<abilities::AbilityBook>()
            .init_resource::<abilities::AbilityDrag>()
            .add_systems(
                Update,
                (
                    tip::load_token_tables,
                    server_messages,
                    toggle,
                    build_window,
                    press,
                    delete_popup,
                    refresh,
                    refresh_submenu,
                    grey_controls,
                    show_icons,
                    refresh_talents,
                    show_talent_icons,
                    refresh_streaks,
                    talent_tooltip,
                    abilities::refresh,
                    abilities::drag,
                )
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

/// The server's kit messages on the world channel: op 40 opens the window (as an
/// [`OpenPanel`]), op 42 is its status.
fn server_messages(
    time: Res<Time>,
    mut channel: MessageReader<benilla_app::fusion::FusionChannelReceived>,
    mut ui: ResMut<CacUi>,
    mut panels: MessageWriter<OpenPanel>,
) {
    let t = time.elapsed_secs();
    for benilla_app::fusion::FusionChannelReceived(m) in channel.read() {
        if m.channel != benilla_app::fusion::CHANNEL_WORLD {
            continue;
        }
        match m.body.first() {
            Some(&OP_OPEN_CLASSES) => {
                let (access, secs) = KitAccess::of_open(&m.body);
                // The view (the old ledger cooling down) only opens a closed window.
                if access == KitAccess::View && ui.open {
                    continue;
                }
                if access != KitAccess::View {
                    ui.pending = Some((access, secs));
                }
                panels.write(OpenPanel {
                    panel: Panel::Kits,
                    access: access.panel_access(),
                });
            }
            Some(&OP_KIT_STATUS) => {
                if let Some(s) = decode_kit_status(&m.body) {
                    ui.status = Some(s);
                    ui.status_at = t;
                }
            }
            _ => {}
        }
    }
}

/// Ask the server for the kit status (where kits change, the field editor's clock).
fn ask_kit_status(out: &mut MessageWriter<SendFusionChannel>) {
    out.write(SendFusionChannel(ChannelMessage {
        channel: CHANNEL_BUILD,
        body: vec![OP_ASK_KIT_STATUS],
    }));
}

/// The window's open state and page after a page is asked for from the HUD (N, P, the stock
/// toggles): open on it; already open on it, closed; open on another, turned to it.
pub fn show_page(open: bool, page: u8, want: u8) -> (bool, u8) {
    if open && page == want {
        (false, page)
    } else {
        (true, want)
    }
}

#[allow(clippy::too_many_arguments)]
fn toggle(
    mut panels: MessageReader<OpenPanel>,
    mut pages: MessageReader<ShowKitPage>,
    mut out: MessageWriter<SendFusionChannel>,
    input: Res<Mw2Input>,
    catalog: Res<Catalog>,
    achievements: Res<crate::achievements::Achievements>,
    mut ui: ResMut<CacUi>,
    mut state: ResMut<FpsState>,
    mut classes: ResMut<Classes>,
    time: Res<Time>,
    mut probe_at: Local<Option<f32>>,
) {
    // A test probe (crate::probe): MW2_PROBE_CAC_UI opens the window once, 10 s after the gun
    // is first up (the login settles first).
    let t = time.elapsed_secs();
    let mut probe = false;
    if state.active && std::env::var_os("MW2_PROBE_CAC_UI").is_some() {
        let at = *probe_at.get_or_insert(t + 10.0);
        if t >= at && at > 0.0 {
            probe = true;
            *probe_at = Some(-1.0);
        }
    }
    // The HUD's button opens it to look (and, open, closes it); a station or the field editor
    // opens it to edit (already open: it stays, and may now edit).
    let mut open = ui.open;
    let mut grant = None;
    // K is the micro button's toggle.
    let hud = input
        .cac
        .then_some(OpenPanel {
            panel: Panel::Kits,
            access: Access::Hud,
        })
        .into_iter();
    for p in panels
        .read()
        .copied()
        .chain(hud)
        .filter(|p| p.panel == Panel::Kits)
    {
        match p.access {
            Access::Hud => {
                open = !open;
                grant = open.then_some((KitAccess::View, 0));
            }
            Access::Npc => {
                open = true;
                grant = Some(ui.pending.take().unwrap_or((KitAccess::Station, 0)));
            }
        }
    }
    // N and P, and the stock talent frame's and spellbook's toggles: a page.
    let keyed = [
        input.talents.then_some(KitPage::Talents),
        input.abilities.then_some(KitPage::Abilities),
    ];
    for page in pages.read().map(|p| p.0).chain(keyed.into_iter().flatten()) {
        let was = open;
        let (now, p) = show_page(open, ui.page, page.index());
        open = now;
        ui.page = p;
        if open && !was {
            grant = Some((KitAccess::View, 0));
        }
    }
    if (probe || crate::capture_keep::keeps("kits")) && !open {
        open = true;
        grant = Some((KitAccess::View, 0));
    }
    // Esc closes an open list or the delete popup first, ends the typing next, else the window.
    if input.escape && ui.dropdown {
        ui.dropdown = false;
    } else if input.escape && ui.confirm_delete {
        ui.confirm_delete = false;
    } else if !crate::capture_keep::in_world(state.active, "kits") || (input.escape && !ui.renaming) {
        open = false;
    }
    if open && !ui.open {
        // Open on the equipped kit, else the first.
        let slot = classes.equipped.unwrap_or(0);
        ui.show_slot(slot, &classes);
        let part = ui.part;
        ui.focus_part(&catalog, part);
        ui.access = KitAccess::View;
        ui.note = None;
        ui.dropdown = false;
        // The class state again (what was bought, the level), and where kits change.
        crate::classes::ask_state(&mut out);
        ask_kit_status(&mut out);
        // The achievements, for what unlocks each locked option (once a session).
        if achievements.rows.is_empty() {
            out.write(SendFusionChannel(crate::achievements::request_message()));
        }
        ui.asked_at = t;
    }
    if let Some((access, secs)) = grant.filter(|_| open) {
        ui.access = access;
        ui.granted_at = t;
        ui.field_until = t + f32::from(secs);
        if access != KitAccess::View {
            // An old refusal ("not near a station") no longer stands here.
            ui.note = None;
            if classes.rejected.is_some() {
                classes.rejected = None;
            }
        }
    }
    if open {
        follow_access(&mut ui, &classes, t, &mut out);
    }
    if ui.open != open {
        info!(
            "benilla-mw2: kit window {} ({:?})",
            if open { "open" } else { "closed" },
            ui.access
        );
    }
    ui.open = open;
    if !open {
        ui.renaming = false;
        ui.dropdown = false;
        ui.confirm_delete = false;
    }
    if ui.renaming {
        if input.enter || input.escape {
            ui.renaming = false;
        } else if !input.typed.is_empty() || input.backspace {
            ui.draft.name = edit_name(&ui.draft.name, &input.typed, input.backspace);
        }
    }
    if !ui.can_edit() {
        ui.renaming = false;
    }
    let typing = ui.open && ui.renaming;
    if state.typing != typing {
        state.typing = typing;
    }
    if state.panel != open {
        state.panel = open;
    }
}

/// Seconds between status checks while a station's window is open (walking away greys it).
const STATION_POLL_SECS: f32 = 3.0;

/// Keep the window's access to where the player is: the field editor's window runs out, a station
/// left behind (the server's status, or a refused save) turns it back to viewing.
fn follow_access(
    ui: &mut CacUi,
    classes: &Classes,
    t: f32,
    out: &mut MessageWriter<SendFusionChannel>,
) {
    match ui.access {
        KitAccess::Field => {
            // The server's clock when it has spoken since the window opened.
            if let Some(s) = ui.status.filter(|_| ui.status_at >= ui.granted_at)
                && s.flags & STATUS_FIELD_OPEN != 0
            {
                ui.field_until = ui.status_at + f32::from(s.field_open_for);
            }
            if t >= ui.field_until {
                ui.access = KitAccess::View;
                ui.note = Some("The field editor's window has closed.".into());
            }
        }
        KitAccess::Station => {
            if t - ui.asked_at >= STATION_POLL_SECS {
                ask_kit_status(out);
                ui.asked_at = t;
            }
            let left = ui
                .status
                .is_some_and(|s| ui.status_at > ui.granted_at && s.flags & STATUS_CAN_EDIT == 0);
            let refused = matches!(classes.rejected, Some((LoadoutError::NotAtKitStation, _)));
            if left || refused {
                ui.access = KitAccess::View;
                ui.note = Some("You left the trainer or innkeeper: viewing only.".into());
            }
        }
        KitAccess::View => {
            // The field editor's window still open (the window was closed and opened again
            // within it): it edits again for what is left.
            if let Some(s) = ui.status.filter(|_| ui.status_at >= ui.granted_at)
                && s.flags & STATUS_FIELD_OPEN != 0
                && ui.status_at + f32::from(s.field_open_for) > t + 1.0
            {
                ui.access = KitAccess::Field;
                ui.granted_at = ui.status_at;
                ui.field_until = ui.status_at + f32::from(s.field_open_for);
            }
        }
    }
}

/// A small arrow button.
fn arrow(font: &Handle<Font>, label: &str, action: Action) -> impl Bundle {
    wow_ui::button_sized(font, label, 20.0, 18.0, action)
}

/// The kit window's width in UI units, set, so a long line wraps rather than widening it.
const WIDTH: f32 = 800.0;
/// The pages' height in UI units: every page fits in it, and the window never changes height
/// whatever is chosen or shown.
pub const CONTENT_H: f32 = 446.0;
/// The parts column's width, and a part row's height.
const PARTS_W: f32 = 320.0;
const ROW_H: f32 = 22.0;
/// The sub-menu's options list: its height, a cell's size.
const LIST_H: f32 = 132.0;
const CELL_W: f32 = 200.0;
const CELL_H: f32 = 24.0;
/// The details' height.
const DETAILS_H: f32 = 196.0;
/// The tooltip's width in UI units (`GameTooltip`'s for a talent, the shared tooltip's).
const TIP_WIDTH: f32 = wow_ui::TIP_WIDTH;

fn build_window(
    mut commands: Commands,
    assets: Res<AssetServer>,
    mut ui: ResMut<CacUi>,
    keymap: Res<crate::keybinds::Keymap>,
) {
    if ui.built || !ui.open {
        return;
    }
    ui.built = true;
    info!("benilla-mw2: kit window built");
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: "kits",
        title: "Kits".into(),
        frame: Frame::Dialog,
        width: Some(WIDTH),
        movable: true,
        anchor: Anchor::CENTER,
        z: i32::MAX,
    };
    let root = wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        (CacRoot, wow_ui::WindowSounds::CHARACTER),
        Some(Action::Close),
        |root| {
            // The kit's name, a dropdown of the kits and what may be done with them; over the
            // pages (its list hangs over them).
            root.spawn((
                Node {
                    flex_direction: FlexDirection::Row,
                    align_items: AlignItems::Center,
                    height: u(30.0),
                    flex_shrink: 0.0,
                    ..default()
                },
                ZIndex(10),
            ))
            .with_children(|r| {
                r.spawn((
                    Node {
                        margin: UiRect::right(u(8.0)),
                        ..default()
                    },
                    children![text(f, "Kit:", 12.0, GOLD)],
                ));
                r.spawn(Node {
                    position_type: PositionType::Relative,
                    ..default()
                })
                .with_children(|d| {
                    wow_ui::dropdown_box(
                        d,
                        f,
                        300.0,
                        (),
                        (
                            Action::DropDown,
                            WowTip::new(
                                "Your kits",
                                "Pick a kit to show, or rename, add, copy or delete one. Up to five.",
                            ),
                        ),
                    );
                    wow_ui::dropdown_list(d, 300.0, 28.0, DropList, |l| {
                        for slot in 0..5u8 {
                            l.spawn(wow_ui::dropdown_item(
                                f,
                                "",
                                272.0,
                                (Action::KitSlot(slot), Txt::DropRow(slot)),
                            ));
                        }
                        wow_ui::dropdown_separator(l);
                        for (label, action, tip) in [
                            (
                                "Rename",
                                Action::Rename,
                                "Type the kit's new name, then press Enter.",
                            ),
                            (
                                "New Kit",
                                Action::NewKit,
                                "Starts a new kit in the first free slot and saves it.",
                            ),
                            (
                                "Copy Kit",
                                Action::CopyKit,
                                "Saves a copy of this kit in the first free slot.",
                            ),
                            (
                                "Delete Kit",
                                Action::DeleteKit,
                                "Empties this slot. The kit you carry can't be deleted.",
                            ),
                        ] {
                            l.spawn(wow_ui::dropdown_item(
                                f,
                                label,
                                272.0,
                                (action, WowTip::new(label, tip)),
                            ));
                        }
                    });
                });
            });
            // The pages, all one height.
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                margin: UiRect::top(u(6.0)),
                height: u(CONTENT_H),
                flex_shrink: 0.0,
                overflow: Overflow::clip(),
                ..default()
            })
            .with_children(|row| {
                loadout_page(row, f);
                talent_page(row, f);
                abilities::page(row, f);
                streak_page(row, f);
            });
            // Where kits change, and what the server said: two lines each at most.
            for (which, size, colour) in [(Txt::Access, 11.0, DIM), (Txt::Status, 12.0, WHITE)] {
                root.spawn((
                    Node {
                        height: u(28.0),
                        margin: UiRect::top(u(4.0)),
                        overflow: Overflow::clip(),
                        flex_shrink: 0.0,
                        ..default()
                    },
                    children![(text(f, "", size, colour), which)],
                ));
            }
            // Save, the field editor, close.
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                align_items: AlignItems::Center,
                margin: UiRect::top(u(2.0)),
                flex_shrink: 0.0,
                ..default()
            })
            .with_children(|r| {
                r.spawn(wow_ui::button(
                    f,
                    "Save & Equip",
                    120.0,
                    (
                        Action::Equip,
                        WowTip::new(
                            "Save & Equip (Enter)",
                            "Saves this kit and makes it the one you carry. Near a trainer or \
                             innkeeper, or with the field editor; in combat it waits until you leave.",
                        ),
                    ),
                ));
                r.spawn(wow_ui::button(
                    f,
                    "Save",
                    80.0,
                    (
                        Action::Save,
                        WowTip::new("Save", "Saves this kit without carrying it."),
                    ),
                ));
                r.spawn(wow_ui::button(
                    f,
                    "Field Edit",
                    100.0,
                    (
                        Action::FieldEdit,
                        WowTip::new(
                            "Field Edit",
                            "Lets you change kits anywhere for a short while, once an hour.",
                        ),
                    ),
                ));
                r.spawn(Node {
                    flex_grow: 1.0,
                    ..default()
                });
                r.spawn(wow_ui::button(f, "Close", 90.0, Action::Close));
            });
        },
        |below| {
            wow_ui::tab_strip(below, |tabs| {
                for (page, label, tip) in [
                    (
                        KitPage::Loadout,
                        "Loadout",
                        "The kit's guns, attachments, equipment, perks and killstreaks.",
                    ),
                    (
                        KitPage::Talents,
                        "Talents",
                        "The kit's class options, one per tier.",
                    ),
                    (
                        KitPage::Abilities,
                        "Abilities",
                        "Everything you can use, to drag onto your action bar.",
                    ),
                    (
                        KitPage::Killstreaks,
                        "Killstreaks",
                        "The rewards this kit earns for kills in a row.",
                    ),
                ] {
                    let i = page.index();
                    // The page's key, where it has one bound.
                    let bound = match page {
                        KitPage::Talents => Some(crate::keybinds::Action::Talents),
                        KitPage::Abilities => Some(crate::keybinds::Action::Abilities),
                        _ => None,
                    }
                    .and_then(|a| keymap.keys(a).first())
                    .map(|k| crate::keybinds::key_text(k));
                    let tip = match bound {
                        Some(k) => format!("{tip} ({k})"),
                        None => tip.to_string(),
                    };
                    tabs.spawn(wow_ui::tab(
                        f,
                        label,
                        (Action::Page(i), PageTab(i), WowTip::new(label, &tip)),
                    ));
                }
            });
        },
    );
    // The talent tooltip: the root's last child, so it draws over the frame and the tabs; it
    // never takes the pointer.
    commands.entity(root).with_children(|r| {
        r.spawn((
            TipBox,
            Node {
                position_type: PositionType::Absolute,
                width: u(TIP_WIDTH),
                flex_direction: FlexDirection::Column,
                padding: UiRect::all(u(9.0)),
                row_gap: u(2.0),
                ..default()
            },
            Visibility::Hidden,
            Pickable::IGNORE,
        ));
    });
}

/// The loadout: the kit's parts on the left, the sub-menu of the part last clicked on the right.
fn loadout_page(row: &mut ChildSpawnerCommands, f: &Handle<Font>) {
    row.spawn((
        PagePanel(LOADOUT),
        Node {
            flex_direction: FlexDirection::Row,
            flex_grow: 1.0,
            ..default()
        },
    ))
    .with_children(|panel| {
        // The parts.
        panel
            .spawn(Node {
                flex_direction: FlexDirection::Column,
                width: u(PARTS_W),
                flex_shrink: 0.0,
                ..default()
            })
            .with_children(|col| {
                wow_ui::section_header(col, f, "This Kit");
                wow_ui::inset(
                    col,
                    Node {
                        flex_direction: FlexDirection::Column,
                        margin: UiRect::top(u(3.0)),
                        padding: UiRect::all(u(4.0)),
                        ..default()
                    },
                    (),
                    |list| {
                        for (i, part) in PARTS.iter().enumerate() {
                            part_row(list, f, i as u8, *part);
                        }
                    },
                );
            });
        // The sub-menu.
        panel
            .spawn(Node {
                flex_direction: FlexDirection::Column,
                flex_grow: 1.0,
                flex_shrink: 1.0,
                min_width: u(0.0),
                margin: UiRect::left(u(12.0)),
                ..default()
            })
            .with_children(|col| submenu(col, f));
    });
}

/// One part of the kit: its label, the arrows either side of its pick (its icon and name), which
/// shows its sub-menu when clicked.
fn part_row(col: &mut ChildSpawnerCommands, f: &Handle<Font>, i: u8, part: Part) {
    col.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        height: u(ROW_H),
        flex_shrink: 0.0,
        ..default()
    })
    .with_children(|r| {
        r.spawn((
            Node {
                width: u(80.0),
                flex_shrink: 0.0,
                ..default()
            },
            children![text(f, part.label(), 11.0, GOLD)],
        ));
        r.spawn(arrow(f, "<", Action::Step(i, -1)));
        r.spawn((
            Button,
            WowButton::new(ButtonKind::Row),
            Action::FocusPart(i),
            // Filled with the pick's name and what it does (refresh).
            WowTip::default(),
            Node {
                width: u(178.0),
                height: u(ROW_H - 2.0),
                padding: UiRect::horizontal(u(3.0)),
                align_items: AlignItems::Center,
                overflow: Overflow::clip_x(),
                flex_shrink: 0.0,
                ..default()
            },
        ))
        .with_children(|b| {
            wow_ui::row_glow(b);
            b.spawn((
                PartIcon(i),
                Node {
                    width: u(16.0),
                    height: u(16.0),
                    margin: UiRect::right(u(4.0)),
                    flex_shrink: 0.0,
                    ..default()
                },
                ImageNode::default(),
                Visibility::Hidden,
                Pickable::IGNORE,
            ));
            b.spawn((text(f, "", 11.0, WHITE), Txt::PartPick(i)));
        });
        r.spawn(arrow(f, ">", Action::Step(i, 1)));
    });
}

/// The sub-menu: the part's title and line, its options, the highlighted one's details, and
/// Regular / Pro with Assign to Kit.
fn submenu(col: &mut ChildSpawnerCommands, f: &Handle<Font>) {
    col.spawn((
        Node {
            flex_shrink: 0.0,
            ..default()
        },
        children![(text(f, "", wow_ui::BODY_SIZE, GOLD), Txt::SubTitle)],
    ));
    col.spawn((
        Node {
            height: u(26.0),
            flex_shrink: 0.0,
            overflow: Overflow::clip(),
            ..default()
        },
        children![(text(f, "", 10.0, DIM), Txt::SubHint)],
    ));
    wow_ui::inset(
        col,
        Node {
            flex_direction: FlexDirection::Column,
            padding: UiRect::all(u(3.0)),
            flex_shrink: 0.0,
            ..default()
        },
        (),
        |c| {
            wow_ui::scroll_frame(c, LIST_H, None, OptionList, |_| {});
        },
    );
    wow_ui::inset(
        col,
        Node {
            flex_direction: FlexDirection::Column,
            margin: UiRect::top(u(4.0)),
            height: u(DETAILS_H),
            flex_shrink: 0.0,
            overflow: Overflow::clip(),
            ..default()
        },
        (),
        |c| {
            c.spawn((
                DetailsBox,
                Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: u(1.0),
                    ..default()
                },
            ));
            // A gun's bars: this option's in gold over the current pick's in grey.
            c.spawn((
                DetailBars,
                Node {
                    flex_direction: FlexDirection::Column,
                    margin: UiRect::top(u(4.0)),
                    display: Display::None,
                    ..default()
                },
            ))
            .with_children(|bars| {
                for (i, name) in STATS.iter().enumerate() {
                    bars.spawn(Node {
                        flex_direction: FlexDirection::Row,
                        align_items: AlignItems::Center,
                        margin: UiRect::top(u(2.0)),
                        ..default()
                    })
                    .with_children(|r| {
                        r.spawn((
                            Node {
                                width: u(64.0),
                                flex_shrink: 0.0,
                                ..default()
                            },
                            children![text(f, name, 10.0, DIM)],
                        ));
                        r.spawn((
                            Node {
                                flex_grow: 1.0,
                                height: u(8.0),
                                border: UiRect::all(px(1)),
                                ..default()
                            },
                            BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.5)),
                            BorderColor::all(GREY.with_alpha(0.6)),
                        ))
                        .with_children(|trough| {
                            for current in [true, false] {
                                trough.spawn((
                                    BarFill(i, current),
                                    Node {
                                        position_type: PositionType::Absolute,
                                        left: px(0),
                                        top: if current { percent(55) } else { px(0) },
                                        width: percent(0),
                                        height: if current { percent(45) } else { percent(55) },
                                        ..default()
                                    },
                                    BackgroundColor(if current { GREY } else { GOLD }),
                                ));
                            }
                        });
                    });
                }
            });
        },
    );
    col.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        margin: UiRect::top(u(4.0)),
        flex_shrink: 0.0,
        ..default()
    })
    .with_children(|r| {
        r.spawn((
            ProRow,
            Node {
                flex_direction: FlexDirection::Row,
                align_items: AlignItems::Center,
                ..default()
            },
        ))
        .with_children(|p| {
            p.spawn(wow_ui::button(
                f,
                "Regular",
                80.0,
                (
                    Action::Pro(false),
                    WowTip::new("Regular", "Play the perk as it is."),
                ),
            ));
            p.spawn(wow_ui::button(
                f,
                "Pro",
                60.0,
                (
                    Action::Pro(true),
                    WowTip::new(
                        "Pro",
                        "Play the perk's Pro version, once its challenge is done.",
                    ),
                ),
            ));
        });
        r.spawn(Node {
            flex_grow: 1.0,
            ..default()
        });
        r.spawn(wow_ui::button(
            f,
            "Assign to Kit",
            120.0,
            (
                Action::Assign,
                WowTip::new(
                    "Assign to Kit (Enter)",
                    "Puts the highlighted choice in this kit. Save & Equip to carry it.",
                ),
            ),
        ));
    });
}

/// The talents: the core abilities, then six tiers of three options.
fn talent_page(row: &mut ChildSpawnerCommands, f: &Handle<Font>) {
    row.spawn((
        PagePanel(TALENTS),
        Node {
            flex_direction: FlexDirection::Column,
            display: Display::None,
            ..default()
        },
    ))
    .with_children(|col| {
        col.spawn(text(
            f,
            "Options are bought once at your class trainer. Pick one per tier.",
            11.0,
            DIM,
        ));
        col.spawn(Node {
            flex_direction: FlexDirection::Row,
            align_items: AlignItems::Center,
            margin: UiRect::vertical(u(3.0)),
            ..default()
        })
        .with_children(|r| {
            r.spawn((
                Node {
                    width: u(44.0),
                    ..default()
                },
                children![text(f, "Core", 12.0, GOLD)],
            ));
            for i in 0..3u8 {
                r.spawn((
                    Interaction::default(),
                    TipSource::Core(i),
                    Node {
                        margin: UiRect::right(u(14.0)),
                        ..default()
                    },
                    children![(text(f, "", 11.0, WHITE), CoreLabel(i))],
                ));
            }
        });
        wow_ui::inset(
            col,
            Node {
                flex_direction: FlexDirection::Column,
                padding: UiRect::all(u(4.0)),
                ..default()
            },
            (),
            |tiers| {
                for tier in 1..=TIERS as u8 {
                    tiers
                        .spawn(Node {
                            flex_direction: FlexDirection::Row,
                            align_items: AlignItems::Center,
                            margin: UiRect::vertical(u(1.0)),
                            ..default()
                        })
                        .with_children(|r| {
                            r.spawn((
                                Node {
                                    width: u(44.0),
                                    ..default()
                                },
                                children![text(f, &format!("Lv {}", tier_level(tier)), 12.0, GOLD)],
                            ));
                            for arch in 0..3u8 {
                                talent_option(r, f, tier, arch);
                            }
                        });
                }
            },
        );
    });
}

/// The killstreaks: the kit's three, then the ladder it plays against the streak this life.
fn streak_page(row: &mut ChildSpawnerCommands, f: &Handle<Font>) {
    row.spawn((
        PagePanel(KILLSTREAKS),
        Node {
            flex_direction: FlexDirection::Column,
            display: Display::None,
            ..default()
        },
    ))
    .with_children(|col| {
        col.spawn(text(
            f,
            "Kills in a row without dying earn these. Choose up to three (here, or on the Loadout page).",
            11.0,
            DIM,
        ));
        wow_ui::section_header(col, f, "Your Killstreaks");
        for slot in 0..3u8 {
            col.spawn(Node {
                flex_direction: FlexDirection::Row,
                align_items: AlignItems::Center,
                height: u(24.0),
                margin: UiRect::top(u(if slot == 0 { 6.0 } else { 0.0 })),
                ..default()
            })
            .with_children(|r| {
                r.spawn((
                    Node {
                        width: u(112.0),
                        ..default()
                    },
                    children![text(f, &format!("Killstreak {}", slot + 1), 11.0, GOLD)],
                ));
                r.spawn(arrow(f, "<", Action::Streak(slot, -1)));
                r.spawn((
                    Node {
                        width: u(240.0),
                        justify_content: JustifyContent::Center,
                        overflow: Overflow::clip_x(),
                        ..default()
                    },
                    children![(text(f, "", 12.0, WHITE), StreakText::Slot(slot))],
                ));
                r.spawn(arrow(f, ">", Action::Streak(slot, 1)));
            });
        }
        col.spawn((
            Node {
                margin: UiRect::vertical(u(4.0)),
                min_height: u(12.0),
                ..default()
            },
            children![(text(f, "", 11.0, DIM), StreakText::Note)],
        ));
        wow_ui::section_header(col, f, "This Kit's Ladder");
        wow_ui::inset(
            col,
            Node {
                flex_direction: FlexDirection::Column,
                margin: UiRect::top(u(3.0)),
                min_width: u(380.0),
                ..default()
            },
            (),
            |list| {
                for i in 0..LADDER_ROWS {
                    list.spawn((
                        Node {
                            margin: UiRect::vertical(u(1.0)),
                            ..default()
                        },
                        children![(text(f, "", 12.0, WHITE), StreakText::Row(i))],
                    ));
                }
            },
        );
        col.spawn((
            Node {
                margin: UiRect::top(u(8.0)),
                ..default()
            },
            children![(text(f, "", 12.0, GOLD), StreakText::Progress)],
        ));
    });
}

/// One talent option: its spell's icon, name and state, on a list line that lights when picked.
fn talent_option(r: &mut ChildSpawnerCommands, f: &Handle<Font>, tier: u8, arch: u8) {
    r.spawn((
        Button,
        WowButton::new(ButtonKind::Row),
        Action::Talent(tier, arch),
        TalentButton(tier, arch),
        TipSource::Talent(tier, arch),
        Node {
            width: u(166.0),
            height: u(40.0),
            margin: UiRect::all(u(2.0)),
            padding: UiRect::all(u(3.0)),
            align_items: AlignItems::Center,
            border: UiRect::all(px(1)),
            ..default()
        },
        BorderColor::all(GREY.with_alpha(0.5)),
        BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.35)),
    ))
    .with_children(|b| {
        wow_ui::row_glow(b);
        b.spawn((
            TalentIcon(tier, arch),
            Node {
                width: u(30.0),
                height: u(30.0),
                margin: UiRect::right(u(5.0)),
                flex_shrink: 0.0,
                ..default()
            },
            ImageNode::default(),
            Visibility::Hidden,
            Pickable::IGNORE,
        ));
        b.spawn((text(f, "", 10.0, WHITE), TalentLabel(tier, arch)));
    });
}

/// The keyboard's action in the window: up and down walk the parts, left and right move the
/// sub-menu's highlight, Enter assigns the highlight, or (on the current pick) saves and equips.
/// Off the Loadout page only Enter does anything.
pub fn nav_action(ui: &CacUi, catalog: &Catalog, nav: crate::input::Nav) -> Option<KeyAction> {
    use crate::input::Nav;
    if ui.page != LOADOUT {
        return (nav == Nav::Enter).then_some(KeyAction::Equip);
    }
    Some(match nav {
        Nav::Up => KeyAction::Part(-1),
        Nav::Down => KeyAction::Part(1),
        Nav::Left => KeyAction::Cursor(-1),
        Nav::Right => KeyAction::Cursor(1),
        Nav::Enter => {
            let part = ui.focused_part();
            let on_current = ui.highlighted(catalog) == parts::current(&ui.draft, part);
            let pro_same = !matches!(part, Part::Perk(_)) || ui.want_pro == ui.pro_of_current();
            if on_current && pro_same {
                KeyAction::Equip
            } else {
                KeyAction::Assign
            }
        }
    })
}

/// What a key does in the window ([`nav_action`]).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KeyAction {
    Part(i32),
    Cursor(i32),
    Assign,
    Equip,
}

/// The part after `dir` steps from `part`, wrapping.
pub fn step_part(part: usize, dir: i32) -> usize {
    let n = PARTS.len() as i32;
    (part as i32 + dir).rem_euclid(n) as usize
}

#[allow(clippy::too_many_arguments)]
fn press(
    catalog: Res<Catalog>,
    class_page: Res<ClassPage>,
    unlocks: Res<crate::achievements::Unlocks>,
    input: Res<crate::input::Mw2Input>,
    mouse: Option<Res<ButtonInput<MouseButton>>>,
    mut ui: ResMut<CacUi>,
    mut classes: ResMut<Classes>,
    mut out: MessageWriter<SendFusionLoadout>,
    mut channel: MessageWriter<SendFusionChannel>,
    buttons: Query<(&Interaction, &Action), Changed<Interaction>>,
    hovered: Query<(&Interaction, &Action)>,
) {
    if !ui.open {
        return;
    }
    // The keyboard.
    let keyed = input
        .nav
        .filter(|_| !ui.renaming)
        .and_then(|nav| nav_action(&ui, &catalog, nav));
    match keyed {
        Some(KeyAction::Part(dir)) => {
            let p = step_part(ui.part, dir);
            ui.focus_part(&catalog, p);
        }
        Some(KeyAction::Cursor(dir)) => ui.step_cursor(&catalog, dir),
        _ => {}
    }
    let keyed = match keyed {
        Some(KeyAction::Assign) => Some(Action::Assign),
        Some(KeyAction::Equip) => Some(Action::Equip),
        _ => None,
    };
    let clicked: Vec<Action> = buttons
        .iter()
        .filter(|(i, _)| **i == Interaction::Pressed)
        .map(|(_, a)| *a)
        .collect();
    // A press anywhere but on the dropdown closes its list.
    if ui.dropdown
        && mouse.is_some_and(|m| m.just_pressed(MouseButton::Left))
        && !hovered
            .iter()
            .any(|(i, a)| *i != Interaction::None && a.in_dropdown())
    {
        ui.dropdown = false;
    }
    for action in keyed.into_iter().chain(clicked) {
        if !action.in_dropdown() {
            ui.dropdown = false;
        }
        // Viewing (the HUD's button, away from a station): nothing changes a kit.
        if action.edits() && !ui.can_edit() {
            continue;
        }
        match action {
            Action::DropDown => ui.dropdown = !ui.dropdown,
            Action::KitSlot(slot) => {
                ui.dropdown = false;
                ui.show_slot(slot, &classes);
                let part = ui.part;
                ui.focus_part(&catalog, part);
            }
            Action::Rename => {
                ui.dropdown = false;
                ui.renaming = !ui.renaming;
            }
            Action::NewKit | Action::CopyKit => {
                ui.dropdown = false;
                let Some(slot) = parts::first_empty_slot(&classes.classes) else {
                    continue;
                };
                let kit = if action == Action::CopyKit {
                    ClassLoadout {
                        name: parts::copy_name(&ui.draft.name),
                        ..ui.draft.clone()
                    }
                } else {
                    ClassLoadout {
                        name: empty_kit_name(slot),
                        ..Default::default()
                    }
                };
                ui.note = None;
                ui.slot = slot;
                ui.draft = kit.clone();
                let part = ui.part;
                ui.focus_part(&catalog, part);
                let request = classes.save(slot, kit);
                crate::cac::send(&mut out, request);
            }
            Action::DeleteKit => {
                ui.dropdown = false;
                ui.confirm_delete = classes.equipped != Some(ui.slot);
            }
            Action::CancelDelete => ui.confirm_delete = false,
            Action::ConfirmDelete => {
                ui.confirm_delete = false;
                if classes.equipped == Some(ui.slot) {
                    continue;
                }
                let slot = ui.slot;
                let bare = ClassLoadout {
                    name: empty_kit_name(slot),
                    ..Default::default()
                };
                ui.draft = bare.clone();
                ui.note = None;
                let request = classes.save(slot, bare);
                crate::cac::send(&mut out, request);
            }
            Action::FocusPart(i) => ui.focus_part(&catalog, usize::from(i)),
            Action::Step(i, dir) => {
                if ui.part != usize::from(i) {
                    ui.focus_part(&catalog, usize::from(i));
                }
                ui.step_cursor(&catalog, dir);
            }
            Action::PickOption(i) => {
                ui.cursor = usize::from(i);
                if let Choice::Perk(p) = ui.highlighted(&catalog) {
                    ui.want_pro = parts::plays_pro(&ui.draft, p);
                }
            }
            Action::Assign => {
                assign_highlighted(&mut ui, &catalog, &unlocks);
            }
            Action::Pro(on) => {
                ui.want_pro = on;
                // On the perk the slot holds, the toggle is the kit's at once.
                let part = ui.focused_part();
                if let (Part::Perk(_), Choice::Perk(p)) = (part, ui.highlighted(&catalog))
                    && parts::current(&ui.draft, part) == Choice::Perk(p)
                    && (!on || unlocks.perk_pro(p))
                {
                    parts::set_pro(&catalog, &mut ui.draft, p, on);
                }
            }
            Action::Page(p) => ui.page = p,
            Action::Talent(tier, arch) => {
                if let Some(id) = class_page
                    .page
                    .as_ref()
                    .and_then(|p| p.option(tier, arch))
                    .map(|e| e.id)
                {
                    toggle_pick(&mut ui.draft.talent_picks, &class_page, id);
                }
            }
            Action::Streak(slot, dir) => cycle_streak(&mut ui.draft, usize::from(slot), dir),
            Action::Equip => {
                ui.note = None;
                let request = classes.equip(ui.slot, ui.draft.clone());
                crate::cac::send(&mut out, request);
            }
            Action::Save => {
                ui.note = None;
                let request = classes.save(ui.slot, ui.draft.clone());
                crate::cac::send(&mut out, request);
            }
            Action::Close => ui.open = false,
            Action::FieldEdit => {
                if ui.access == KitAccess::View {
                    channel.write(SendFusionChannel(ChannelMessage {
                        channel: CHANNEL_BUILD,
                        body: vec![OP_FIELD_EDIT],
                    }));
                }
            }
        }
    }
}

/// The status line: a locked pick's unlock, the server's refusal, a wait for combat, the window's
/// own note, "Equipped".
fn status_text(
    lock: Option<String>,
    rejected: Option<LoadoutError>,
    pending: bool,
    note: Option<&str>,
    equipped: bool,
) -> (String, Color) {
    if let Some(how) = lock {
        return (how, Color::srgb(1.0, 0.6, 0.3));
    }
    if let Some(e) = rejected {
        return (error_text(e).to_owned(), Color::srgb(1.0, 0.35, 0.3));
    }
    if pending {
        return ("Equips when you leave combat".into(), DIM);
    }
    if let Some(n) = note {
        return (n.to_owned(), Color::srgb(1.0, 0.6, 0.3));
    }
    if equipped {
        return ("Equipped".into(), GOLD);
    }
    (String::new(), WHITE)
}

/// A kit's line in the dropdown (and its box): its number, name, and whether it is carried.
pub fn kit_line(slot: u8, name: &str, equipped: bool) -> String {
    let mark = if equipped { "  (equipped)" } else { "" };
    format!("{}. {name}{mark}", slot + 1)
}

/// The texts the window keeps up to date (rows, titles, lines), from the kit being edited.
#[allow(clippy::too_many_arguments)]
pub fn window_texts(
    ui: &CacUi,
    classes: &Classes,
    catalog: &Catalog,
    unlocks: &crate::achievements::Unlocks,
    achievements: &crate::achievements::Achievements,
    journal_key: Option<&str>,
    t: f32,
) -> HashMap<TxtId, (String, Color)> {
    let mut out = HashMap::new();
    for (i, part) in PARTS.iter().enumerate() {
        let choice = parts::current(&ui.draft, *part);
        let mut s = parts::choice_name(catalog, *part, &choice);
        let closed = parts::part_closed(catalog, &ui.draft, *part);
        let locked = !parts::unlocked(unlocks, &ui.draft, *part, &choice);
        if let Part::Perk(_) = part
            && let Choice::Perk(p) = choice
            && parts::plays_pro(&ui.draft, p)
        {
            s.push_str(" Pro");
        }
        if locked {
            s.push_str("  (Locked)");
        }
        let colour = if locked {
            LOCKED
        } else if closed.is_some() && choice.is_none() {
            GREY
        } else if !ui.can_edit() {
            DIM
        } else {
            WHITE
        };
        out.insert(TxtId::PartPick(i as u8), (s, colour));
    }
    let part = ui.focused_part();
    out.insert(TxtId::SubTitle, (part.title(), GOLD));
    let hint = parts::part_closed(catalog, &ui.draft, part).map_or_else(
        || {
            (
                format!("{} Left and right choose, Enter assigns.", part.blurb()),
                DIM,
            )
        },
        |why| (why, Color::srgb(1.0, 0.6, 0.3)),
    );
    out.insert(TxtId::SubHint, hint);
    for slot in 0..5u8 {
        let name = classes.classes[usize::from(slot)]
            .as_ref()
            .map_or_else(|| empty_kit_name(slot), |c| c.name.clone());
        out.insert(
            TxtId::DropRow(slot),
            (kit_line(slot, &name, classes.equipped == Some(slot)), WHITE),
        );
    }
    let field_left = (ui.field_until - t).max(0.0).ceil() as u32;
    let line = access_line(ui.access, ui.gm(), ui.field_ready_in(t), field_left);
    out.insert(
        TxtId::Access,
        (line, if ui.can_edit() { GOLD } else { DIM }),
    );
    // The focused part's pick is locked: say what unlocks it.
    let choice = parts::current(&ui.draft, part);
    let how = (ui.page == LOADOUT && !parts::unlocked(unlocks, &ui.draft, part, &choice))
        .then(|| parts::unlock_key(&ui.draft, part, &choice))
        .flatten()
        .map(|(k, id, camo, att)| {
            achievements.unlocked_by(k, id, camo, &att).map_or_else(
                || match journal_key {
                    Some(k) => format!("Locked: earn it through achievements ({k})"),
                    None => "Locked: earn it through achievements".to_string(),
                },
                |a| format!("Locked: {}: {}", a.name, a.description),
            )
        });
    let status = status_text(
        how,
        classes.rejected.map(|(e, _)| e),
        classes.pending.is_some(),
        ui.note.as_deref(),
        classes.equipped == Some(ui.slot),
    );
    out.insert(TxtId::Status, status);
    out
}

/// Which text of the window ([`window_texts`]' key; the component's public mirror).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TxtId {
    PartPick(u8),
    SubTitle,
    SubHint,
    DropRow(u8),
    Access,
    Status,
}

impl From<Txt> for TxtId {
    fn from(t: Txt) -> Self {
        match t {
            Txt::PartPick(i) => Self::PartPick(i),
            Txt::SubTitle => Self::SubTitle,
            Txt::SubHint => Self::SubHint,
            Txt::DropRow(s) => Self::DropRow(s),
            Txt::Access => Self::Access,
            Txt::Status => Self::Status,
        }
    }
}

/// The kits dropdown's box text: the kit shown, or its name being typed.
pub fn drop_box_text(ui: &CacUi, equipped: bool) -> String {
    if ui.renaming {
        format!("{}_", ui.draft.name)
    } else {
        kit_line(ui.slot, &ui.draft.name, equipped)
    }
}

#[allow(clippy::too_many_arguments)]
fn refresh(
    time: Res<Time>,
    unlocks: Res<crate::achievements::Unlocks>,
    achievements: Res<crate::achievements::Achievements>,
    ui: Res<CacUi>,
    classes: Res<Classes>,
    catalog: Res<Catalog>,
    keymap: Res<crate::keybinds::Keymap>,
    mut roots: Query<&mut Visibility, With<CacRoot>>,
    mut texts: Query<(&Txt, &mut Text, &mut TextColor), Without<wow_ui::DropDownText>>,
    rows: Query<(&Txt, &Children), Without<Text>>,
    mut captions: Query<
        &mut Text,
        (
            With<wow_ui::ButtonText>,
            Without<Txt>,
            Without<wow_ui::DropDownText>,
        ),
    >,
    mut drop_box: Query<&mut Text, (With<wow_ui::DropDownText>, Without<Txt>)>,
    mut lists: Query<&mut Node, With<DropList>>,
    mut tips: Query<(&Action, &mut WowTip)>,
) {
    for mut v in &mut roots {
        let want = if ui.open {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *v != want {
            *v = want;
        }
    }
    if !ui.open {
        return;
    }
    let journal = keymap
        .keys(crate::keybinds::Action::Journal)
        .first()
        .map(|k| crate::keybinds::key_text(k));
    let lines = window_texts(
        &ui,
        &classes,
        &catalog,
        &unlocks,
        &achievements,
        journal.as_deref(),
        time.elapsed_secs(),
    );
    for (which, mut t, mut c) in &mut texts {
        let Some((s, colour)) = lines.get(&TxtId::from(*which)) else {
            continue;
        };
        if t.0 != *s {
            t.0 = s.clone();
        }
        if c.0 != *colour {
            c.0 = *colour;
        }
    }
    // The dropdown's kit lines: their captions.
    for (which, children) in &rows {
        let Some((s, _)) = lines.get(&TxtId::from(*which)) else {
            continue;
        };
        for c in children.iter() {
            if let Ok(mut t) = captions.get_mut(c)
                && t.0 != *s
            {
                t.0 = s.clone();
            }
        }
    }
    let boxed = drop_box_text(&ui, classes.equipped == Some(ui.slot));
    for mut t in &mut drop_box {
        if t.0 != boxed {
            t.0 = boxed.clone();
        }
    }
    let display = if ui.dropdown {
        Display::Flex
    } else {
        Display::None
    };
    for mut n in &mut lists {
        if n.display != display {
            n.display = display;
        }
    }
    // Each part's row says what its pick does on hover.
    let ctx = Ctx {
        catalog: &catalog,
        unlocks: &unlocks,
        achievements: &achievements,
        journal_key: journal.as_deref(),
    };
    for (action, mut tip) in &mut tips {
        let Action::FocusPart(i) = action else {
            continue;
        };
        let part = PARTS[usize::from(*i).min(PARTS.len() - 1)];
        let (title, body) = describe::part_tip(&ctx, &ui.draft, part);
        let want = WowTip::new(&title, &body);
        if *tip != want {
            *tip = want;
        }
    }
}

/// The sub-menu: its options rebuilt when the part or its options change, each option's name in
/// its state's colour, the highlight kept in view, the details rebuilt when the highlight or the
/// kit changes, the gun's bars, and the Pro row shown for perks.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn refresh_submenu(
    mut commands: Commands,
    assets: Res<AssetServer>,
    ui: Res<CacUi>,
    catalog: Res<Catalog>,
    unlocks: Res<crate::achievements::Unlocks>,
    achievements: Res<crate::achievements::Achievements>,
    keymap: Res<crate::keybinds::Keymap>,
    mut lists: Query<(Entity, &mut ScrollPosition, &ComputedNode), With<OptionList>>,
    mut cell_texts: Query<(&OptionText, &mut Text, &mut TextColor)>,
    details_box: Query<Entity, With<DetailsBox>>,
    mut bars: Query<
        (&mut Node, &mut Visibility),
        (With<DetailBars>, Without<BarFill>, Without<ProRow>),
    >,
    mut fills: Query<(&BarFill, &mut Node), (Without<DetailBars>, Without<ProRow>)>,
    mut pro_rows: Query<&mut Node, (With<ProRow>, Without<DetailBars>, Without<BarFill>)>,
    mut built: Local<Option<(usize, Vec<Choice>)>>,
    mut shown: Local<Option<Details>>,
    mut kept: Local<Option<(usize, usize)>>,
) {
    if !ui.open || ui.page != LOADOUT {
        return;
    }
    let part = ui.focused_part();
    let opts = parts::options(&catalog, &ui.draft, part);
    let Ok((list, mut scroll, view)) = lists.single_mut() else {
        return;
    };
    let font: Handle<Font> = assets.load(FONT);
    // The options, as cells two to a row.
    let key = (ui.part, opts.clone());
    if built.as_ref() != Some(&key) {
        *built = Some(key);
        *kept = None;
        let names: Vec<String> = opts
            .iter()
            .map(|c| parts::choice_name(&catalog, part, c))
            .collect();
        commands
            .entity(list)
            .despawn_related::<Children>()
            .with_children(|l| {
                l.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    flex_wrap: FlexWrap::Wrap,
                    ..default()
                })
                .with_children(|grid| {
                    for (i, name) in names.iter().enumerate() {
                        option_cell(grid, &font, i as u16, name);
                    }
                });
            });
        scroll.0.y = 0.0;
    }
    // Each option's name: the kit's pick in green, a locked one red, a blocked one grey.
    let current = parts::current(&ui.draft, part);
    for (cell, mut t, mut c) in &mut cell_texts {
        let Some(choice) = opts.get(usize::from(cell.0)) else {
            continue;
        };
        let locked = !parts::unlocked(&unlocks, &ui.draft, part, choice);
        let blocked = parts::blocked(&catalog, &ui.draft, part, choice).is_some();
        let mut s = parts::choice_name(&catalog, part, choice);
        if locked {
            s.push_str("  (Locked)");
        }
        let colour = if *choice == current {
            GREEN
        } else if locked {
            LOCKED
        } else if blocked {
            GREY
        } else {
            WHITE
        };
        if t.0 != s {
            t.0 = s;
        }
        if c.0 != colour {
            c.0 = colour;
        }
    }
    // The highlight in view when it moves (the wheel's scroll left alone otherwise).
    if *kept != Some((ui.part, ui.cursor)) && !view.is_empty() {
        *kept = Some((ui.part, ui.cursor));
        let unit = wow_ui::uf(1.0);
        let row_top = (ui.cursor / 2) as f32 * CELL_H * unit;
        let row_bottom = row_top + CELL_H * unit;
        let view_h = view.size.y * view.inverse_scale_factor;
        if row_top < scroll.0.y {
            scroll.0.y = row_top;
        } else if row_bottom > scroll.0.y + view_h {
            scroll.0.y = row_bottom - view_h;
        }
    }
    // The details.
    let journal = keymap
        .keys(crate::keybinds::Action::Journal)
        .first()
        .map(|k| crate::keybinds::key_text(k));
    let ctx = Ctx {
        catalog: &catalog,
        unlocks: &unlocks,
        achievements: &achievements,
        journal_key: journal.as_deref(),
    };
    let choice = ui.highlighted(&catalog);
    let mut details = describe::details(&ctx, &ui.draft, part, &choice);
    let pro = ui.want_pro && matches!(part, Part::Perk(_));
    if let Some(why) = assign_refusal(&catalog, &unlocks, &ui.draft, part, &choice, pro)
        .filter(|_| choice != current)
    {
        details.lines.insert(
            1.min(details.lines.len()),
            describe::Line {
                text: why,
                color: Color::srgb(1.0, 0.6, 0.3),
                size: 11.0,
            },
        );
    }
    if shown.as_ref() != Some(&details) {
        if let Ok(b) = details_box.single() {
            let lines = details.lines.clone();
            commands
                .entity(b)
                .despawn_related::<Children>()
                .with_children(|d| {
                    for l in &lines {
                        d.spawn((
                            Node {
                                max_width: u(WIDTH - PARTS_W - 70.0),
                                ..default()
                            },
                            children![text(&font, &l.text, l.size, l.color)],
                        ));
                    }
                });
        }
        for (mut node, mut vis) in &mut bars {
            let (d, v) = if details.bars.is_some() {
                (Display::Flex, Visibility::Inherited)
            } else {
                (Display::None, Visibility::Hidden)
            };
            if node.display != d {
                node.display = d;
            }
            if *vis != v {
                *vis = v;
            }
        }
        if let Some(b) = details.bars {
            for (fill, mut node) in &mut fills {
                let (now, was) = b[fill.0.min(3)];
                node.width = percent(if fill.1 { was } else { now } * 100.0);
            }
        }
        *shown = Some(details);
    }
    let display = if matches!(part, Part::Perk(_)) {
        Display::Flex
    } else {
        Display::None
    };
    for mut node in &mut pro_rows {
        if node.display != display {
            node.display = display;
        }
    }
}

/// One option of a sub-menu: its icon and name on a list line that lights when highlighted.
fn option_cell(grid: &mut ChildSpawnerCommands, f: &Handle<Font>, i: u16, name: &str) {
    grid.spawn((
        Button,
        WowButton::new(ButtonKind::Row),
        Action::PickOption(i),
        Node {
            width: u(CELL_W),
            height: u(CELL_H),
            padding: UiRect::horizontal(u(3.0)),
            align_items: AlignItems::Center,
            overflow: Overflow::clip_x(),
            flex_shrink: 0.0,
            ..default()
        },
    ))
    .with_children(|b| {
        wow_ui::row_glow(b);
        b.spawn((
            OptionIcon(i),
            Node {
                width: u(20.0),
                height: u(20.0),
                margin: UiRect::right(u(4.0)),
                flex_shrink: 0.0,
                ..default()
            },
            ImageNode::default(),
            Visibility::Hidden,
            Pickable::IGNORE,
        ));
        b.spawn((text(f, name, 11.0, WHITE), OptionText(i)));
    });
}

/// Grey what the window can't do here and light what is chosen: every edit while viewing; the
/// field editor unless viewing and ready; Assign when the highlight can't go in; New and Copy
/// without a free slot; Delete on the kit carried; Pro until it is earned. Lit: the focused
/// part's row, the kit shown in the dropdown, the highlighted option, the perk's toggle.
#[allow(clippy::too_many_arguments)]
fn grey_controls(
    time: Res<Time>,
    ui: Res<CacUi>,
    catalog: Res<Catalog>,
    unlocks: Res<crate::achievements::Unlocks>,
    classes: Res<Classes>,
    mut buttons: Query<(&Action, &mut WowButton)>,
) {
    if !ui.open {
        return;
    }
    let t = time.elapsed_secs();
    let edit = ui.can_edit();
    let field = ui.access == KitAccess::View && !ui.gm() && ui.field_ready_in(t) == 0;
    let part = ui.focused_part();
    let choice = ui.highlighted(&catalog);
    let current = parts::current(&ui.draft, part);
    let pro = ui.want_pro && matches!(part, Part::Perk(_));
    let pro_same = !matches!(part, Part::Perk(_)) || ui.want_pro == ui.pro_of_current();
    let assignable = assign_refusal(&catalog, &unlocks, &ui.draft, part, &choice, pro).is_none()
        && !(choice == current && pro_same);
    let free = parts::first_empty_slot(&classes.classes).is_some();
    let perk = match &choice {
        Choice::Perk(p) => *p,
        _ => 0,
    };
    let has_pro = catalog.perk(perk).is_some_and(|p| p.has_pro());
    for (action, mut b) in &mut buttons {
        let disabled = match action {
            a if a.edits() && !edit => true,
            Action::FieldEdit => !field,
            Action::Assign => !assignable,
            Action::NewKit | Action::CopyKit => !free,
            Action::DeleteKit => classes.equipped == Some(ui.slot),
            Action::Pro(true) => !has_pro || !unlocks.perk_pro(perk),
            Action::Pro(false) => perk == 0,
            Action::Step(i, _) | Action::FocusPart(i) => {
                // A part with nothing to take (no room, no gun) still opens, greyed.
                let p = PARTS[usize::from(*i).min(PARTS.len() - 1)];
                parts::part_closed(&catalog, &ui.draft, p).is_some()
                    && matches!(action, Action::Step(..))
            }
            _ => false,
        };
        if b.disabled != disabled {
            b.disabled = disabled;
        }
        let selected = match action {
            Action::FocusPart(i) => Some(usize::from(*i) == ui.part),
            Action::KitSlot(s) => Some(*s == ui.slot),
            Action::PickOption(i) => Some(usize::from(*i) == ui.cursor),
            Action::Pro(on) => Some(*on == (ui.want_pro && has_pro)),
            _ => None,
        };
        if let Some(s) = selected
            && b.selected != s
        {
            b.selected = s;
        }
    }
}

/// Each part's pick's icon and each option's, from the install.
#[allow(clippy::type_complexity)]
fn show_icons(
    ui: Res<CacUi>,
    catalog: Res<Catalog>,
    assets: Res<crate::mw2::Mw2Assets>,
    mut icons: ResMut<Icons>,
    mut images: ResMut<Assets<Image>>,
    mut rows: Query<(&PartIcon, &mut ImageNode, &mut Visibility), Without<OptionIcon>>,
    mut cells: Query<(&OptionIcon, &mut ImageNode, &mut Visibility), Without<PartIcon>>,
) {
    if !ui.open || ui.page != LOADOUT {
        return;
    }
    let lib = match &*assets {
        crate::mw2::Mw2Assets::Ready(lib) => Some(lib.as_ref()),
        _ => None,
    };
    let mut set = |node: &mut ImageNode, vis: &mut Visibility, name: Option<String>| {
        let handle = name.and_then(|n| icons.get(&n, &assets, &mut images));
        let want = if handle.is_some() {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if let Some(h) = handle
            && node.image != h
        {
            node.image = h;
        }
        if *vis != want {
            *vis = want;
        }
    };
    for (r, mut node, mut vis) in &mut rows {
        let part = PARTS[usize::from(r.0).min(PARTS.len() - 1)];
        let choice = parts::current(&ui.draft, part);
        set(
            &mut node,
            &mut vis,
            parts::choice_icon(&catalog, lib, &choice),
        );
    }
    let opts = parts::options(&catalog, &ui.draft, ui.focused_part());
    for (c, mut node, mut vis) in &mut cells {
        let name = opts
            .get(usize::from(c.0))
            .and_then(|o| parts::choice_icon(&catalog, lib, o));
        set(&mut node, &mut vis, name);
    }
}

/// The talent page: which page shows, each option's name and state (picked, bought, its tier
/// reached), the core abilities.
#[allow(clippy::type_complexity)]
fn refresh_talents(
    ui: Res<CacUi>,
    class_page: Res<ClassPage>,
    mut panels: Query<(&mut Node, &mut Visibility, &PagePanel)>,
    mut labels: Query<(&TalentLabel, &mut Text, &mut TextColor)>,
    mut buttons: Query<(&TalentButton, &mut WowButton), Without<PageTab>>,
    mut tabs: Query<(&PageTab, &mut WowButton), Without<TalentButton>>,
    mut shown: Local<Option<u8>>,
) {
    if !ui.open {
        return;
    }
    if *shown != Some(ui.page) {
        info!("benilla-mw2: kit window page {}", ui.page);
        *shown = Some(ui.page);
    }
    // Out of the layout and hidden both, so a page left behind never draws.
    for (mut node, mut vis, panel) in &mut panels {
        let on = panel.0 == ui.page;
        let display = if on { Display::Flex } else { Display::None };
        if node.display != display {
            node.display = display;
        }
        let visibility = if on {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *vis != visibility {
            *vis = visibility;
        }
    }
    for (tab, mut button) in &mut tabs {
        let want = tab.0 == ui.page;
        if button.selected != want {
            button.selected = want;
        }
    }
    let Some(page) = class_page.page.as_ref() else {
        for (_, mut t, _) in &mut labels {
            if t.0 != "..." {
                t.0 = "...".into();
            }
        }
        return;
    };
    let picks = ui.draft.talent_picks;
    for (l, mut t, mut c) in &mut labels {
        let Some(e) = page.option(l.0, l.1) else {
            continue;
        };
        let picked = picks[usize::from(l.0) - 1] == e.id;
        let note = if picked {
            "picked"
        } else if !class_page.learned(e.id) && !class_page.admin() {
            "not bought"
        } else if class_page.level() < tier_level(l.0) && !class_page.admin() {
            "level too low"
        } else if e.kind == Kind::Passive {
            "passive"
        } else {
            ""
        };
        let archetype = page
            .archetypes
            .get(usize::from(l.1))
            .map_or("", String::as_str);
        let s = if note.is_empty() {
            format!("{}\n{archetype}", e.name)
        } else {
            format!("{}\n{archetype}, {note}", e.name)
        };
        if t.0 != s {
            t.0 = s;
        }
        let want = if picked {
            GOLD
        } else if class_page.can_pick(e) {
            WHITE
        } else {
            GREY
        };
        if c.0 != want {
            c.0 = want;
        }
    }
    for (b, mut button) in &mut buttons {
        let picked = page
            .option(b.0, b.1)
            .is_some_and(|e| picks[usize::from(b.0) - 1] == e.id);
        if button.selected != picked {
            button.selected = picked;
        }
    }
}

/// A spell's icon in the install (`Spell.dbc` joined with `SpellIcon.dbc`), as an asset path.
pub fn spell_icon_path(icon: &str) -> String {
    format!("mpq://{}.blp", icon.replace('\\', "/"))
}

/// Each option's spell icon on the talent page.
fn show_talent_icons(
    ui: Res<CacUi>,
    class_page: Res<ClassPage>,
    table: Res<crate::hands::SpellTable>,
    assets: Res<AssetServer>,
    mut cache: Local<HashMap<u32, Option<Handle<Image>>>>,
    mut icons: Query<(&TalentIcon, &mut ImageNode, &mut Visibility)>,
) {
    if !ui.open || ui.page != TALENTS {
        return;
    }
    let (Some(page), Some(spells)) = (class_page.page.as_ref(), table.0.as_ref()) else {
        return;
    };
    for (i, mut node, mut vis) in &mut icons {
        let handle = page.option(i.0, i.1).and_then(|e| {
            cache
                .entry(e.spell)
                .or_insert_with(|| {
                    spells
                        .get(e.spell)
                        .and_then(|d| d.icon.as_deref())
                        .map(|icon| assets.load(spell_icon_path(icon)))
                })
                .clone()
        });
        let want = if handle.is_some() {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if let Some(h) = handle
            && node.image != h
        {
            node.image = h;
        }
        if *vis != want {
            *vis = want;
        }
    }
}

/// The killstreak page's lines for a kit: each slot's choice, the ladder it plays (kills, and
/// how far the streak this life has come), the streak, and the note.
pub fn streak_lines(
    kit: &ClassLoadout,
    unlocked: impl Fn(u16) -> bool + Copy,
    tenths: u32,
    hardline: bool,
) -> HashMap<StreakTextId, (String, Color)> {
    let mut out = HashMap::new();
    let red = Color::srgb(1.0, 0.35, 0.3);
    for slot in 0..3u8 {
        let id = kit.streaks[usize::from(slot)];
        let line = if id == 0 {
            ("None".to_owned(), DIM)
        } else {
            let kills = streak_ladder()
                .into_iter()
                .find(|c| c.id == id)
                .map_or(0, |c| crate::streaks::reward_need(c.kills, hardline));
            let mut s = format!("{}  ({kills} kills)", streak_name(id));
            let colour = if unlocked(id) {
                WHITE
            } else {
                s.push_str("  (Locked)");
                red
            };
            (s, colour)
        };
        out.insert(StreakTextId::Slot(slot), line);
    }
    let chose = kit.streaks.iter().any(|&s| s != 0);
    out.insert(
        StreakTextId::Note,
        (
            if chose {
                String::new()
            } else {
                "None chosen: this kit plays every killstreak you have unlocked.".into()
            },
            DIM,
        ),
    );
    let ladder = kit_streaks(kit, unlocked);
    for i in 0..LADDER_ROWS {
        let line = ladder.get(usize::from(i)).map_or_else(
            || (String::new(), WHITE),
            |c| {
                let need = crate::streaks::reward_need(c.kills, hardline);
                let (state, colour) = if !unlocked(c.id) {
                    ("locked".to_owned(), red)
                } else if tenths >= need * 10 {
                    ("earned this life".to_owned(), wow_ui::GREEN)
                } else {
                    let left = need * 10 - tenths;
                    (format!("{}.{} to go", left / 10, left % 10), WHITE)
                };
                (
                    format!("{}   {need} kills   {state}", streak_name(c.id)),
                    colour,
                )
            },
        );
        out.insert(StreakTextId::Row(i), line);
    }
    let streak = if tenths.is_multiple_of(10) {
        format!("{}", tenths / 10)
    } else {
        format!("{}.{}", tenths / 10, tenths % 10)
    };
    let mut progress = format!("Your streak this life: {streak} kills.");
    if hardline {
        progress.push_str(" Hardline: every reward one kill sooner.");
    }
    out.insert(StreakTextId::Progress, (progress, GOLD));
    out
}

/// Which line of the killstreak page ([`streak_lines`]' key; the component's public mirror).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum StreakTextId {
    Slot(u8),
    Row(u8),
    Progress,
    Note,
}

impl From<StreakText> for StreakTextId {
    fn from(t: StreakText) -> Self {
        match t {
            StreakText::Slot(s) => Self::Slot(s),
            StreakText::Row(r) => Self::Row(r),
            StreakText::Progress => Self::Progress,
            StreakText::Note => Self::Note,
        }
    }
}

/// The killstreak page, from the kit being edited and the streak the server counts.
fn refresh_streaks(
    ui: Res<CacUi>,
    unlocks: Res<crate::achievements::Unlocks>,
    streak: Res<crate::streaks::Streak>,
    mut texts: Query<(&StreakText, &mut Text, &mut TextColor)>,
) {
    if !ui.open || ui.page != KILLSTREAKS {
        return;
    }
    let hardline = ui.draft.perks.contains(&crate::catalog::perk_id::HARDLINE);
    let lines = streak_lines(
        &ui.draft,
        |id| unlocks.killstreak(u32::from(id)),
        streak.tenths,
        hardline,
    );
    for (which, mut t, mut c) in &mut texts {
        let Some((s, colour)) = lines.get(&StreakTextId::from(*which)) else {
            continue;
        };
        if t.0 != *s {
            t.0 = s.clone();
        }
        if c.0 != *colour {
            c.0 = *colour;
        }
    }
}

/// The talent tooltip: what the pointer is over on the talent page, beside the pointer, in the
/// stock tooltip's look.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn talent_tooltip(
    mut commands: Commands,
    assets: Res<AssetServer>,
    ui: Res<CacUi>,
    class_page: Res<ClassPage>,
    spells: Res<crate::hands::SpellTable>,
    tables: Res<TokenTables>,
    sources: Query<(&Interaction, &TipSource)>,
    roots: Query<(&ComputedNode, &RelativeCursorPosition), With<CacRoot>>,
    mut tips: Query<(Entity, &mut Node, &ComputedNode, &mut Visibility), With<TipBox>>,
    mut core: Query<(&CoreLabel, &mut Text)>,
    mut shown: Local<Vec<tip::TipLine>>,
    mut described: Local<HashMap<u16, String>>,
) {
    let Ok((tip_entity, mut node, tip_size, mut vis)) = tips.single_mut() else {
        return;
    };
    let page = class_page.page.as_ref();
    // The core abilities' names on their hover labels.
    if let Some(page) = page {
        let cores: Vec<&str> = page
            .entries
            .iter()
            .filter(|e| e.tier == crate::classes::CORE_TIER)
            .map(|e| e.name.as_str())
            .collect();
        for (label, mut t) in &mut core {
            let s = cores.get(usize::from(label.0)).copied().unwrap_or("");
            if t.0 != s {
                t.0 = s.to_owned();
            }
        }
    }
    let hovered = sources
        .iter()
        .find(|(i, _)| **i != Interaction::None)
        .map(|(_, s)| *s)
        .filter(|_| ui.open && ui.page == TALENTS);
    let entry = hovered.zip(page).and_then(|(s, page)| match s {
        TipSource::Talent(tier, arch) => page.option(tier, arch),
        TipSource::Core(i) => page
            .entries
            .iter()
            .filter(|e| e.tier == crate::classes::CORE_TIER)
            .nth(usize::from(i)),
    });
    let Some((entry, page)) = entry.zip(page) else {
        if *vis != Visibility::Hidden {
            *vis = Visibility::Hidden;
        }
        return;
    };
    let description = described
        .entry(entry.id)
        .or_insert_with(|| {
            let spell_text = spells
                .0
                .as_ref()
                .and_then(|s| tip::spell_description(s, tables.0.as_deref(), entry.spell));
            tip::description(entry, spell_text)
        })
        .clone();
    let archetype = page
        .archetypes
        .get(usize::from(entry.archetype))
        .map_or("", String::as_str);
    let state = tip::option_state(&class_page, entry, &ui.draft.talent_picks);
    let lines = talent_tip(entry, archetype, state, &description);
    if *shown != lines {
        *shown = lines.clone();
        let font: Handle<Font> = assets.load(FONT);
        commands
            .entity(tip_entity)
            .despawn_related::<Children>()
            .with_children(|t| wow_ui::tip_body(t, &font, &lines));
    }
    // Beside the pointer, in the window's own (unscaled) space: right and below, or left and
    // above where the window's edge comes first.
    let Ok((root, cursor)) = roots.single() else {
        return;
    };
    let Some(at) = cursor.normalized else {
        return;
    };
    let size = root.size * root.inverse_scale_factor;
    let local = (at + Vec2::splat(0.5)) * size;
    let tip = tip_size.size * tip_size.inverse_scale_factor;
    let w = wow_ui::uf(TIP_WIDTH);
    let gap = wow_ui::uf(16.0);
    let x = if local.x + gap + w > size.x + w * 0.5 {
        local.x - gap - w
    } else {
        local.x + gap
    };
    let y = if local.y + gap + tip.y > size.y {
        (local.y - gap - tip.y).max(0.0)
    } else {
        local.y + gap
    };
    if node.left != Val::Px(x) {
        node.left = Val::Px(x);
    }
    if node.top != Val::Px(y) {
        node.top = Val::Px(y);
    }
    if *vis != Visibility::Inherited {
        *vis = Visibility::Inherited;
    }
}

#[cfg(test)]
mod tests;
