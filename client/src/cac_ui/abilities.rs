//! The kit window's Abilities page, in place of the stock spellbook (whose micro button is gone,
//! and whose key and toggle lead here): everything the character can use, each with its icon and
//! a stock-looking tooltip ([`super::tip`]), in five sections:
//!
//! - **Core Abilities**, every kit's three;
//! - **Class Options** bought at the class trainer, those in the kit in hand first;
//! - **Class Spells**, the stock class spells the class still uses (its Utility: resurrection,
//!   mounts and the like);
//! - **General and Professions**: the stock General tab (attacks, racials) and the trades;
//! - **Companion**, the summoned companion's pet bar abilities;
//! - **Modern** and **Utility**, the two bars' own abilities (crate::action_systems: the guns,
//!   the gear, the Armory's; bare hands, the tools, the instruments), with the tooltips their
//!   buttons show (crate::ability_tips). Dragged onto a button they go back on their bar (an
//!   entry on its bar moves from its slot, `PickupAction`; one off them is picked up afresh,
//!   `UiScript::hold_layer_action`); the bars' own rule keeps each on its system's bar.
//!
//! The list is the book benilla pushes to the stock spellbook (`UiScript::spellbook`), that is
//! what the server taught, matched against the class page (crate::classes) for the class entries.
//!
//! **One entry per ability.** A class spell exists once, like a gun (design/GAME_DESIGN.md
//! section 2, "No ranks"): the server keeps one entry of each chain in the book, the rank the
//! level allows, and grows it into the next as the level comes. The page lists each spell once
//! (should the book still hold an older rank, the highest stands for it), shows no "Rank N", and
//! its tooltips read the spell's numbers at the character's level (`$s1` and the like with their
//! per-level growth), the numbers it casts with.
//!
//! **Drag** an entry onto an action button to place it: the press only marks the entry (an icon
//! follows the pointer), and the release puts the spell on WoW's cursor (`PickupSpell`) and hands
//! it to the frame under the pointer as a stock drag would (its `OnReceiveDrag`: `PlaceAction` on
//! an action button, the pet action on the pet bar). A **click** leaves the spell on the cursor,
//! to click onto a button as from the stock spellbook.

use std::collections::HashMap;

use benilla_formats::{SpellCatalog, SpellDurationCatalog, SpellRadiusCatalog};
use benilla_ui::script::{SpellBookState, UiScript};
use bevy::prelude::*;
use bevy::ui::RelativeCursorPosition;
use bevy::window::PrimaryWindow;

use super::tip::{self, TipLine};
use super::{CacRoot, CacUi, spell_icon_path};
use crate::classes::{CORE_TIER, ClassPage, Kind, Page, TIERS};
use crate::companion::Moves;
use crate::panels::KitPage;
use crate::wow_ui::{
    self, BODY_SIZE, DIM, GOLD, GREY, Kind as ButtonKind, NOTE_SIZE, SLOT_EDGE, WowButton, WowTip,
    text, u,
};

/// The page's index in the kit window.
pub const PAGE: u8 = KitPage::Abilities.index();

/// A section of the page, in its order.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Section {
    Core,
    Options,
    Class,
    General,
    Companion,
    Modern,
    Utility,
}

impl Section {
    pub const ALL: [Section; 7] = [
        Section::Core,
        Section::Options,
        Section::Class,
        Section::General,
        Section::Companion,
        Section::Modern,
        Section::Utility,
    ];

    pub fn title(self) -> &'static str {
        match self {
            Section::Core => "Core Abilities",
            Section::Options => "Class Options",
            Section::Class => "Class Spells",
            Section::General => "General and Professions",
            Section::Companion => "Companion",
            Section::Modern => "Modern",
            Section::Utility => "Utility",
        }
    }
}

/// Where an entry is picked up from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Source {
    /// The player's book, 1-based (`PickupSpell(i, "spell")`).
    Book(u32),
    /// The pet's book, found by the spell's name (`PickupSpell(i, "pet")`).
    Pet(String),
    /// One of the Modern or Utility bars' abilities: its id, the slot (Lua action id) it is in,
    /// none when on no bar, and its icon.
    Layer {
        ability: u32,
        slot: Option<u32>,
        texture: String,
    },
    /// Nothing to put on a bar (a passive, an option out of the kit).
    None,
}

/// One entry of the page.
#[derive(Clone, Debug, PartialEq)]
pub struct Ability {
    pub section: Section,
    pub name: String,
    /// The line under the name: its rank, tier, or what it is.
    pub sub: String,
    /// The icon's texture path (`Interface\Icons\...`).
    pub icon: Option<String>,
    pub spell: u32,
    pub source: Source,
    /// Usable now (white), else greyed.
    pub usable: bool,
    /// The icon's edge: the stock quality colours, by what the entry is.
    pub edge: Color,
    pub tip: Vec<TipLine>,
}

/// The tooltip's last line for an entry that goes on a bar.
pub const DRAG_HINT: &str = "Drag it onto an action button, or click it to pick it up.";
/// The companion's: its abilities live on the pet bar.
pub const PET_HINT: &str =
    "On the Companion bar (C): Ctrl+1 to Ctrl+0 casts it at the crosshair. Drag it there to move it.";

/// What the page is made of.
pub struct Inputs<'a> {
    pub book: &'a SpellBookState,
    pub class: &'a ClassPage,
    pub spells: Option<&'a SpellCatalog>,
    /// The companion out's moveset, if one is out.
    pub pet: Option<&'a Moves>,
}

/// A spell's subtext as the page shows it: none for a rank ("Rank 3"; a class spell is one entry
/// that grows with the level), else as it is ("Apprentice", "Racial").
pub fn shown_subtext(rank: Option<&str>) -> Option<&str> {
    let r = rank?.trim();
    let is_rank = r
        .strip_prefix("Rank")
        .is_some_and(|n| !n.trim().is_empty() && n.trim().chars().all(|c| c.is_ascii_digit()));
    (!r.is_empty() && !is_rank).then_some(r)
}

/// The page's entries, in section order. `describe` gives a spell's resolved description.
pub fn abilities(i: &Inputs, describe: &mut dyn FnMut(u32) -> Option<String>) -> Vec<Ability> {
    let book = i.book;
    // Each slot's tab (0 the General tab); a book without tabs is all General.
    let mut tab_of = vec![0usize; book.slots.len()];
    for (t, tab) in book.tabs.iter().enumerate() {
        let start = tab.offset as usize;
        let end = (start + tab.num_spells as usize).min(book.slots.len());
        for slot in tab_of.iter_mut().take(end).skip(start) {
            *slot = t;
        }
    }
    let spell_name = |id: u32| i.spells.and_then(|s| s.get(id)).map(|d| d.name.as_str());
    let spell_icon = |id: u32| {
        i.spells
            .and_then(|s| s.get(id))
            .and_then(|d| d.icon.clone())
    };
    let mut used = vec![false; book.slots.len()];
    // The book slot that is a class entry's spell: the same id, else the same name (the book
    // holds the rank the level allows, the page the rank of the option's tier). Every slot of
    // that name is the entry's: an older rank the book still holds is no entry of its own, and
    // the highest (the last) is the one picked up.
    let find = |spell: u32, used: &mut Vec<bool>| -> Option<usize> {
        let name = spell_name(spell).filter(|n| !n.is_empty()).or_else(|| {
            book.slots
                .iter()
                .find(|s| s.spell_id == spell)
                .map(|s| s.name.as_str())
        });
        let k = match name {
            Some(n) => book.slots.iter().rposition(|s| s.name == n),
            None => None,
        }
        .or_else(|| book.slots.iter().position(|s| s.spell_id == spell))?;
        for (j, s) in book.slots.iter().enumerate() {
            if j == k || name.is_some_and(|n| s.name == n) {
                used[j] = true;
            }
        }
        Some(k)
    };
    let mut out = Vec::new();
    let page: Option<&Page> = i.class.page.as_ref();
    let picks = i.class.state.as_ref().map_or([0u16; TIERS], |s| s.picks);
    let picked = |id: u16| picks.contains(&id);
    if let Some(page) = page {
        // Core.
        for e in page.entries.iter().filter(|e| e.tier == CORE_TIER) {
            let slot = find(e.spell, &mut used);
            let passive = e.kind == Kind::Passive;
            let held = slot.map_or(e.spell, |k| book.slots[k].spell_id);
            let description = tip::description(e, describe(held));
            let mut lines =
                tip::talent_tip(e, "", tip::option_state(i.class, e, &picks), &description);
            let source = match slot {
                Some(k) if !book.slots[k].passive => {
                    lines.push(TipLine::new(DRAG_HINT, DIM, NOTE_SIZE));
                    Source::Book(k as u32 + 1)
                }
                _ => Source::None,
            };
            out.push(Ability {
                section: Section::Core,
                name: e.name.clone(),
                sub: if passive {
                    "Core ability, passive".into()
                } else {
                    "Core ability".into()
                },
                icon: slot
                    .and_then(|k| book.slots[k].texture.clone())
                    .or_else(|| spell_icon(e.spell)),
                spell: e.spell,
                source,
                usable: slot.is_some() || passive,
                edge: GOLD,
                tip: lines,
            });
        }
        // Options: those in the kit in hand by tier, then the rest bought.
        let mut options: Vec<_> = page
            .entries
            .iter()
            .filter(|e| e.tier != CORE_TIER && (picked(e.id) || i.class.learned(e.id)))
            .collect();
        options.sort_by_key(|e| (!picked(e.id), e.tier, e.archetype));
        for e in options {
            let in_kit = picked(e.id);
            let slot = find(e.spell, &mut used);
            let held = slot.map_or(e.spell, |k| book.slots[k].spell_id);
            let archetype = page
                .archetypes
                .get(usize::from(e.archetype))
                .map_or("", String::as_str);
            let description = tip::description(e, describe(held));
            let mut lines = tip::talent_tip(
                e,
                archetype,
                tip::option_state(i.class, e, &picks),
                &description,
            );
            let source = match slot {
                Some(k) if in_kit && !book.slots[k].passive => {
                    lines.push(TipLine::new(DRAG_HINT, DIM, NOTE_SIZE));
                    Source::Book(k as u32 + 1)
                }
                _ => {
                    if !in_kit {
                        lines.push(TipLine::new(
                            "Pick it in a kit at a trainer or innkeeper to use it.",
                            DIM,
                            NOTE_SIZE,
                        ));
                    }
                    Source::None
                }
            };
            let kind = match e.kind {
                Kind::Passive => ", passive",
                _ => "",
            };
            out.push(Ability {
                section: Section::Options,
                name: e.name.clone(),
                sub: format!(
                    "Tier {}, {archetype}{kind}{}",
                    e.tier,
                    if in_kit { "" } else { " (not in this kit)" }
                ),
                icon: slot
                    .and_then(|k| book.slots[k].texture.clone())
                    .or_else(|| spell_icon(e.spell)),
                spell: e.spell,
                source,
                usable: in_kit,
                edge: if in_kit {
                    wow_ui::quality_color(3)
                } else {
                    GREY
                },
                tip: lines,
            });
        }
    }
    // The rest of the book: the General tab, then the class's own tabs; a spell once, by its
    // highest rank (the last of its name in its tab).
    let mut rest: Vec<usize> = (0..book.slots.len())
        .filter(|&k| !used[k])
        .filter(|&k| {
            !(k + 1..book.slots.len()).any(|j| {
                !used[j] && tab_of[j] == tab_of[k] && book.slots[j].name == book.slots[k].name
            })
        })
        .collect();
    rest.sort_by_key(|&k| (tab_of[k] != 0, tab_of[k], k));
    for k in rest {
        let s = &book.slots[k];
        let general = tab_of[k] == 0;
        let (section, kind) = if general {
            (Section::General, "General")
        } else {
            (Section::Class, "Class spell")
        };
        let subtext = shown_subtext(s.rank.as_deref());
        let mut lines = tip::spell_tip(
            &s.name,
            subtext,
            kind,
            s.passive,
            &describe(s.spell_id).unwrap_or_default(),
        );
        if !s.passive {
            lines.push(TipLine::new(DRAG_HINT, DIM, NOTE_SIZE));
        }
        let mut sub = subtext.unwrap_or_default().to_owned();
        if s.passive {
            sub = if sub.is_empty() {
                "Passive".into()
            } else {
                format!("{sub}, passive")
            };
        }
        out.push(Ability {
            section,
            name: s.name.clone(),
            sub,
            icon: s.texture.clone().or_else(|| spell_icon(s.spell_id)),
            spell: s.spell_id,
            source: if s.passive {
                Source::None
            } else {
                Source::Book(k as u32 + 1)
            },
            usable: true,
            edge: SLOT_EDGE,
            tip: lines,
        });
    }
    // The companion out: each stage's chosen ability, and its mount form's.
    if let Some(m) = i.pet.filter(|m| m.item != 0) {
        let mut pet = |spell: u32, name: &str, sub: String| {
            if spell == 0 {
                return;
            }
            let name = if name.is_empty() {
                spell_name(spell).unwrap_or("").to_owned()
            } else {
                name.to_owned()
            };
            let mut lines = tip::spell_tip(
                &name,
                None,
                "Companion ability",
                false,
                &describe(spell).unwrap_or_default(),
            );
            lines.push(TipLine::new(PET_HINT, DIM, NOTE_SIZE));
            out.push(Ability {
                section: Section::Companion,
                name: name.clone(),
                sub,
                icon: spell_icon(spell),
                spell,
                source: Source::Pet(name),
                usable: true,
                edge: wow_ui::quality_color(2),
                tip: lines,
            });
        };
        for stage in 0..3 {
            let pick = m.picks[stage];
            if (1..=2).contains(&pick) {
                let o = usize::from(pick - 1);
                pet(
                    m.spells[stage][o],
                    &m.names[stage][o],
                    format!("Stage {}", stage + 1),
                );
            }
        }
        pet(m.mounted_spell, &m.mounted_name, "Mounted".into());
    }
    out.sort_by_key(|a| a.section);
    out
}

/// The Modern and Utility bars' entries: every Modern ability, bare hands and the tools the
/// character has and knows (all of them before the server says), with their buttons' tooltips.
pub fn layer_abilities(
    tips: &crate::ability_tips::AbilityTips,
    usable: Option<u16>,
) -> Vec<Ability> {
    use crate::action_systems::{Ability as Act, System, tool_bit};
    let mut out = Vec::new();
    for a in Act::ALL {
        if a.is_tool() && usable.is_some_and(|m| m & tool_bit(a) == 0) {
            continue;
        }
        let modern = a.systems().contains(&System::Modern);
        let Some(lines) = tips.tips.get(&a.id()) else {
            continue;
        };
        let mut tip: Vec<TipLine> = lines
            .iter()
            .enumerate()
            .map(|(i, l)| {
                let text = match &l.right {
                    Some((r, _)) => format!("{}    {r}", l.left),
                    None => l.left.clone(),
                };
                let [r, g, b, _] = l.left_color;
                TipLine::new(text, Color::srgb(r, g, b), if i == 0 { 14.0 } else { 12.0 })
            })
            .collect();
        tip.push(TipLine::new(DRAG_HINT, DIM, NOTE_SIZE));
        let slot = tips.slots.get(&a.id()).copied();
        out.push(Ability {
            section: if modern {
                Section::Modern
            } else {
                Section::Utility
            },
            name: lines
                .first()
                .map_or_else(|| a.name().to_owned(), |l| l.left.clone()),
            sub: if slot.is_some() {
                a.name().to_owned()
            } else {
                format!("{}, not on a bar", a.name())
            },
            icon: Some(a.icon().to_owned()),
            spell: 0,
            source: Source::Layer {
                ability: a.id(),
                slot,
                texture: a.icon().to_owned(),
            },
            usable: true,
            edge: if modern { GOLD } else { SLOT_EDGE },
            tip,
        });
    }
    out
}

/// An entry off the bars is put on the cursor from Rust (a host seam; 1.12 has no verb for it)
/// before [`pickup_lua`] runs.
pub fn hold_layer(script: &UiScript, source: &Source) {
    if let Source::Layer {
        ability,
        slot: None,
        texture,
    } = source
    {
        script.hold_layer_action(*ability, Some(texture.clone()));
    }
}

/// The Lua that picks an entry up and, when `drop`, hands it to the frame under the pointer as a
/// stock drag's release would. It answers what happened ("held", "dropped", "missing", or the
/// handler's error); `None` for an entry with nothing to pick up.
pub fn pickup_lua(source: &Source, drop: bool) -> Option<String> {
    let pick = match source {
        Source::Book(i) => format!("ClearCursor() PickupSpell({i}, \"spell\")\n"),
        Source::Pet(name) => format!(
            "local want, found = {}, nil\n\
             for i = 1, 1024 do\n\
               local n = GetSpellName(i, \"pet\")\n\
               if not n then break end\n\
               if n == want then found = i break end\n\
             end\n\
             if not found then return \"missing\" end\n\
             ClearCursor() PickupSpell(found, \"pet\")\n",
            crate::lua::quote(name)
        ),
        Source::Layer {
            slot: Some(slot), ..
        } => format!(
            "ClearCursor() PickupAction({slot})
"
        ),
        // Held already ([`hold_layer`]).
        Source::Layer { .. } => String::new(),
        Source::None => return None,
    };
    let release = if drop {
        "local f = GetMouseFocus()\n\
         local h = f and f.GetScript and f:GetScript(\"OnReceiveDrag\")\n\
         if not h then return \"held\" end\n\
         local was = this\n\
         this = f\n\
         local ok, err = pcall(h)\n\
         this = was\n\
         if not ok then return tostring(err) end\n\
         return \"dropped\""
    } else {
        "return \"held\""
    };
    Some(format!("{pick}{release}"))
}

// ── The page ─────────────────────────────────────────────────────────────────────────────────────

/// The list's view.
#[derive(Component)]
pub(super) struct AbilityList;

/// An entry's line: its index in [`AbilityBook`].
#[derive(Component, Clone, Copy)]
pub(super) struct AbilityRow(pub usize);

/// The icon that follows the pointer in a drag.
#[derive(Component)]
pub(super) struct DragGhost;

/// The page's entries as last drawn.
#[derive(Resource, Default)]
pub(super) struct AbilityBook {
    pub entries: Vec<Ability>,
    /// When the list was last read (elapsed seconds).
    read_at: Option<f32>,
    /// The list view it was drawn into.
    drawn: Option<Entity>,
}

/// A press on an entry, until the release.
#[derive(Resource, Default, Clone, Copy)]
pub(super) struct AbilityDrag {
    held: Option<usize>,
    from: Vec2,
    moved: bool,
}

/// An entry's width and height in UI units: two to a row.
const ENTRY_W: f32 = 268.0;
const ENTRY_H: f32 = 36.0;
const ICON: f32 = 30.0;
/// The list's height before it scrolls.
const LIST_H: f32 = 318.0;
/// Seconds between reads of the book while the page shows.
const READ_SECS: f32 = 0.5;
/// Pixels the pointer moves before a press is a drag.
const DRAG_PX: f32 = 5.0;

/// The page's column, hidden until its tab is chosen.
pub(super) fn page(row: &mut ChildSpawnerCommands, f: &Handle<Font>) {
    row.spawn((
        super::PagePanel(PAGE),
        Node {
            flex_direction: FlexDirection::Column,
            flex_grow: 1.0,
            margin: UiRect::left(u(12.0)),
            display: Display::None,
            ..default()
        },
    ))
    .with_children(|col| {
        col.spawn(text(
            f,
            "Everything you can use. Drag one onto an action button.",
            wow_ui::SMALL_SIZE,
            DIM,
        ));
        wow_ui::inset(
            col,
            Node {
                margin: UiRect::top(u(4.0)),
                flex_direction: FlexDirection::Column,
                ..default()
            },
            (),
            |c| {
                wow_ui::scroll_frame(c, LIST_H, None, AbilityList, |_| {});
            },
        );
    });
}

/// Read the book while the page shows and draw the list when it changed.
#[allow(clippy::too_many_arguments)]
pub(super) fn refresh(
    mut commands: Commands,
    time: Res<Time>,
    assets: Res<AssetServer>,
    ui: Res<CacUi>,
    class: Res<ClassPage>,
    spells: Res<crate::hands::SpellTable>,
    tables: Res<tip::TokenTables>,
    companion: Res<crate::companion::CompanionState>,
    layer: (
        Option<Res<crate::ability_tips::AbilityTips>>,
        Option<Res<crate::action_systems::ActionSystems>>,
    ),
    script: Option<NonSend<UiScript>>,
    mut book: ResMut<AbilityBook>,
    lists: Query<Entity, With<AbilityList>>,
    mut described: Local<(u8, HashMap<u32, Option<String>>)>,
) {
    if !ui.open || ui.page != PAGE {
        book.read_at = None;
        return;
    }
    let t = time.elapsed_secs();
    if book.read_at.is_some_and(|at| t - at < READ_SECS) {
        return;
    }
    book.read_at = Some(t);
    let Ok(list) = lists.single() else {
        return;
    };
    let spellbook = script
        .as_deref()
        .map(UiScript::spellbook)
        .unwrap_or_default();
    let catalog = spells.0.as_deref();
    let token_tables: Option<&(SpellDurationCatalog, SpellRadiusCatalog)> = tables.0.as_deref();
    // The numbers at the character's level (a level-up redraws them).
    let level = class.level();
    if described.0 != level {
        *described = (level, HashMap::new());
    }
    let mut describe = |id: u32| {
        described
            .1
            .entry(id)
            .or_insert_with(|| {
                catalog.and_then(|s| tip::spell_description_at(s, token_tables, id, level))
            })
            .clone()
    };
    let pet = companion.bar.out().then_some(&companion.moves);
    let mut entries = abilities(
        &Inputs {
            book: &spellbook,
            class: &class,
            spells: catalog,
            pet,
        },
        &mut describe,
    );
    // The two bars' own: once anything else is known (an empty list keeps its "loading" line).
    if !entries.is_empty()
        && let Some(tips) = layer.0.as_deref()
    {
        let usable = layer.1.as_deref().and_then(|s| s.usable_tools);
        entries.extend(layer_abilities(tips, usable));
    }
    // Drawn already (an empty list is drawn once too: its "nothing yet" line).
    if entries == book.entries && book.drawn == Some(list) {
        return;
    }
    book.drawn = Some(list);
    book.entries = entries;
    let font: Handle<Font> = assets.load(wow_ui::FONT);
    let f = &font;
    let entries = &book.entries;
    commands
        .entity(list)
        .despawn_related::<Children>()
        .with_children(|l| {
            if entries.is_empty() {
                l.spawn(text(
                    f,
                    "Your abilities are still loading.",
                    BODY_SIZE,
                    GREY,
                ));
                return;
            }
            for section in Section::ALL {
                let mut members = entries
                    .iter()
                    .enumerate()
                    .filter(|(_, a)| a.section == section)
                    .peekable();
                if members.peek().is_none() {
                    continue;
                }
                wow_ui::section_header(l, f, section.title());
                l.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    flex_wrap: FlexWrap::Wrap,
                    flex_shrink: 0.0,
                    margin: UiRect::top(u(2.0)),
                    ..default()
                })
                .with_children(|g| {
                    for (k, a) in members {
                        entry(g, f, &assets, k, a);
                    }
                });
            }
        });
}

/// One entry: its icon in its edge, its name and the line under it, the hover highlight, and its
/// tooltip.
fn entry(
    g: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    k: usize,
    a: &Ability,
) {
    let mut state = WowButton::new(ButtonKind::Row);
    state.disabled = a.source == Source::None && !a.usable;
    g.spawn((
        Button,
        state,
        AbilityRow(k),
        WowTip(a.tip.clone()),
        Node {
            width: u(ENTRY_W),
            height: u(ENTRY_H),
            padding: UiRect::axes(u(3.0), u(2.0)),
            margin: UiRect::all(u(1.0)),
            column_gap: u(6.0),
            align_items: AlignItems::Center,
            flex_shrink: 0.0,
            ..default()
        },
    ))
    .with_children(|b| {
        wow_ui::row_glow(b);
        let image = a.icon.as_deref().map(|i| assets.load(spell_icon_path(i)));
        b.spawn(wow_ui::icon_frame(image, ICON, a.edge));
        b.spawn(Node {
            flex_direction: FlexDirection::Column,
            min_width: u(0.0),
            overflow: Overflow::clip_x(),
            ..default()
        })
        .with_children(|c| {
            // The stock spellbook's look: the name in GameFontNormal's gold, the subtext under it.
            c.spawn(text(
                f,
                &a.name,
                BODY_SIZE,
                if a.usable { GOLD } else { GREY },
            ));
            if !a.sub.is_empty() {
                c.spawn(text(
                    f,
                    &a.sub,
                    NOTE_SIZE,
                    if a.usable { DIM } else { GREY },
                ));
            }
        });
    });
}

/// The drag: a press marks an entry, the pointer carries its icon once it moves, and the release
/// picks the spell up (a click) or places it where it was let go (a drag off the window).
#[allow(clippy::too_many_arguments)]
pub(super) fn drag(
    mut commands: Commands,
    assets: Res<AssetServer>,
    ui: Res<CacUi>,
    mouse: Res<ButtonInput<MouseButton>>,
    window: Query<&Window, With<PrimaryWindow>>,
    book: Res<AbilityBook>,
    mut held: ResMut<AbilityDrag>,
    rows: Query<(&AbilityRow, &Interaction), Changed<Interaction>>,
    roots: Query<&RelativeCursorPosition, With<CacRoot>>,
    mut ghosts: Query<(Entity, &mut Node, &mut ImageNode, &mut Visibility), With<DragGhost>>,
    script: Option<NonSend<UiScript>>,
) {
    let cursor = window.single().ok().and_then(Window::cursor_position);
    if !ui.open || ui.page != PAGE {
        held.held = None;
    }
    let mut clicked = None;
    for (row, i) in &rows {
        if *i != Interaction::Pressed {
            continue;
        }
        match cursor.filter(|_| mouse.pressed(MouseButton::Left)) {
            Some(c) => {
                *held = AbilityDrag {
                    held: Some(row.0),
                    from: c,
                    moved: false,
                };
            }
            // Pressed with no button down (a test probe's `click:`): a click, at once.
            None => clicked = Some(row.0),
        }
    }
    if let Some(k) = clicked
        && let Some(entry) = book.entries.get(k)
        && let Some(chunk) = pickup_lua(&entry.source, false)
        && let Some(script) = script.as_deref()
    {
        hold_layer(script, &entry.source);
        let what = crate::lua::eval::<Option<String>>(script, &chunk).flatten();
        info!(
            "benilla-mw2: ability {} picked up: {}",
            entry.name,
            what.as_deref().unwrap_or("?")
        );
    }
    let ghost = ghosts.single_mut().ok();
    let Some(k) = held.held else {
        if let Some((_, _, _, mut v)) = ghost
            && *v != Visibility::Hidden
        {
            *v = Visibility::Hidden;
        }
        return;
    };
    let Some(entry) = book.entries.get(k) else {
        held.held = None;
        return;
    };
    if let Some(c) = cursor
        && c.distance(held.from) > DRAG_PX
    {
        held.moved = true;
    }
    // The icon on the pointer while it is dragged.
    match ghost {
        None => {
            commands.spawn((
                Name::new("ability drag"),
                DragGhost,
                Node {
                    position_type: PositionType::Absolute,
                    width: u(ICON),
                    height: u(ICON),
                    ..default()
                },
                ImageNode::default(),
                GlobalZIndex(i32::MAX),
                Visibility::Hidden,
                Pickable::IGNORE,
            ));
        }
        Some((_, mut node, mut img, mut v)) => {
            let show =
                held.moved && entry.source != Source::None && mouse.pressed(MouseButton::Left);
            if show && let Some(c) = cursor {
                let half = wow_ui::uf(ICON) * 0.5;
                node.left = Val::Px(c.x - half);
                node.top = Val::Px(c.y - half);
                if let Some(i) = entry.icon.as_deref() {
                    let h: Handle<Image> = assets.load(spell_icon_path(i));
                    if img.image != h {
                        img.image = h;
                    }
                }
            }
            let want = if show {
                Visibility::Inherited
            } else {
                Visibility::Hidden
            };
            if *v != want {
                *v = want;
            }
        }
    }
    if !mouse.just_released(MouseButton::Left) {
        return;
    }
    let drag = *held;
    held.held = None;
    let over_window = roots.iter().any(RelativeCursorPosition::cursor_over);
    let drop = drag.moved && !over_window;
    let Some(chunk) = pickup_lua(&entry.source, drop) else {
        return;
    };
    let Some(script) = script else {
        return;
    };
    hold_layer(&script, &entry.source);
    let what = crate::lua::eval::<Option<String>>(&script, &chunk).flatten();
    info!(
        "benilla-mw2: ability {} {}: {}",
        entry.name,
        if drop { "dragged" } else { "picked up" },
        what.as_deref().unwrap_or("?")
    );
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::classes::{ClassState, PageEntry};
    use benilla_ui::script::{SpellSlotView, SpellTabView};

    fn slot(id: u32, name: &str, passive: bool) -> SpellSlotView {
        SpellSlotView {
            spell_id: id,
            name: name.into(),
            rank: Some("Rank 1".into()),
            texture: Some(format!("Interface\\Icons\\{name}")),
            passive,
            ..Default::default()
        }
    }

    fn entry(id: u16, tier: u8, arch: u8, spell: u32, name: &str) -> PageEntry {
        PageEntry {
            id,
            tier,
            archetype: arch,
            kind: Kind::Ability,
            capstone: false,
            spell,
            name: name.into(),
        }
    }

    /// A warrior's book after the kit apply: General (Attack, a racial, First Aid), then the
    /// class tab with a core ability, a picked option and a Utility spell.
    fn warrior() -> (SpellBookState, ClassPage) {
        let book = SpellBookState {
            tabs: vec![
                SpellTabView {
                    name: "General".into(),
                    texture: None,
                    offset: 0,
                    num_spells: 3,
                },
                SpellTabView {
                    name: "Arms".into(),
                    texture: None,
                    offset: 3,
                    num_spells: 3,
                },
            ],
            slots: vec![
                slot(6603, "Attack", false),
                slot(20600, "Perception", false),
                slot(3273, "First Aid", true),
                slot(11578, "Charge", false),
                slot(23894, "Bloodthirst", false),
                slot(2457, "Battle Stance", false),
            ],
        };
        let class = ClassPage {
            page: Some(Page {
                class: 1,
                archetypes: ["Berserker".into(), "Vanguard".into(), "Warlord".into()],
                entries: vec![
                    entry(101, 0, 0, 11578, "Charge"),
                    entry(102, 0, 1, 99, "Rally"),
                    entry(111, 1, 0, 23894, "Bloodthirst"),
                    entry(112, 1, 1, 2565, "Bulwark"),
                    entry(121, 2, 0, 1234, "Rampage"),
                ],
            }),
            state: Some(ClassState {
                class: 1,
                level: 20,
                kit: 0,
                flags: 0,
                picks: [111, 0, 0, 0, 0, 0],
                learned: vec![111, 112],
            }),
            ..Default::default()
        };
        (book, class)
    }

    #[test]
    fn the_page_sorts_the_book_into_its_sections() {
        let (book, class) = warrior();
        let list = abilities(
            &Inputs {
                book: &book,
                class: &class,
                spells: None,
                pet: None,
            },
            &mut |_| None,
        );
        let names: Vec<(Section, &str)> =
            list.iter().map(|a| (a.section, a.name.as_str())).collect();
        assert_eq!(
            names,
            [
                (Section::Core, "Charge"),
                (Section::Core, "Rally"),
                (Section::Options, "Bloodthirst"),
                (Section::Options, "Bulwark"),
                (Section::Class, "Battle Stance"),
                (Section::General, "Attack"),
                (Section::General, "Perception"),
                (Section::General, "First Aid"),
            ]
        );
        // A core ability the book lacks (a custom one) is listed, with nothing to pick up.
        assert_eq!(list[1].source, Source::None);
        let by = |n: &str| list.iter().find(|a| a.name == n).unwrap();
        // In the book: picked up from it, by its 1-based slot.
        assert_eq!(by("Charge").source, Source::Book(4));
        assert_eq!(by("Bloodthirst").source, Source::Book(5));
        assert!(by("Bloodthirst").sub.contains("Tier 1, Berserker"));
        // Bought, not in the kit: listed greyed, nothing to pick up, and the tooltip says why.
        let bulwark = by("Bulwark");
        assert!(!bulwark.usable && bulwark.source == Source::None);
        assert!(bulwark.sub.contains("not in this kit"));
        assert!(
            bulwark
                .tip
                .iter()
                .any(|l| l.text.contains("Pick it in a kit"))
        );
        // Not bought, not picked: not listed.
        assert!(!list.iter().any(|a| a.name == "Rampage"));
        // A passive is shown, not picked up.
        assert_eq!(by("First Aid").source, Source::None);
        assert!(by("First Aid").sub.to_lowercase().contains("passive"));
        // Every entry has a tooltip led by its name; what goes on a bar says how.
        for a in &list {
            assert_eq!(a.tip[0].text, a.name);
            if a.source != Source::None {
                assert!(a.tip.iter().any(|l| l.text == DRAG_HINT), "{}", a.name);
            }
        }
        assert_eq!(
            by("Battle Stance").icon.as_deref(),
            Some("Interface\\Icons\\Battle Stance")
        );
    }

    #[test]
    fn each_ability_is_one_entry_with_no_rank() {
        let (mut book, class) = warrior();
        // A book that still holds older ranks: Charge 1 and 2 before the entry, two ranks of a
        // Utility spell, and a trade's own subtext.
        let mut charge1 = slot(100, "Charge", false);
        charge1.rank = Some("Rank 1".into());
        let mut charge2 = slot(6178, "Charge", false);
        charge2.rank = Some("Rank 2".into());
        let mut charge3 = book.slots[3].clone();
        charge3.rank = Some("Rank 3".into());
        let mut stance2 = slot(2458, "Battle Stance", false);
        stance2.rank = Some("Rank 2".into());
        let mut aid = book.slots[2].clone();
        aid.rank = Some("Apprentice".into());
        book.slots = vec![
            book.slots[0].clone(),
            book.slots[1].clone(),
            aid,
            charge1,
            charge2,
            charge3,
            book.slots[4].clone(),
            book.slots[5].clone(),
            stance2,
        ];
        book.tabs[1].num_spells = 6;
        let list = abilities(
            &Inputs {
                book: &book,
                class: &class,
                spells: None,
                pet: None,
            },
            &mut |_| None,
        );
        let count = |n: &str| list.iter().filter(|a| a.name == n).count();
        assert_eq!(count("Charge"), 1);
        assert_eq!(count("Battle Stance"), 1);
        let by = |n: &str| list.iter().find(|a| a.name == n).unwrap();
        // The entry picked up is the highest rank held (slot 6, 1-based).
        assert_eq!(by("Charge").source, Source::Book(6));
        assert_eq!(by("Battle Stance").source, Source::Book(9));
        // No "Rank N" anywhere: not under the name, not in the tooltip.
        for a in &list {
            assert!(!a.sub.contains("Rank"), "{}: {}", a.name, a.sub);
            assert!(
                a.tip.iter().all(|l| !l.text.starts_with("Rank ")),
                "{}",
                a.name
            );
        }
        // A subtext that is no rank stays.
        assert!(by("First Aid").sub.contains("Apprentice"));
        assert_eq!(shown_subtext(Some("Rank 12")), None);
        assert_eq!(shown_subtext(Some("Racial")), Some("Racial"));
        assert_eq!(shown_subtext(Some("Ranked")), Some("Ranked"));
        assert_eq!(shown_subtext(None), None);
    }

    #[test]
    fn the_companion_out_brings_its_abilities() {
        let (book, class) = warrior();
        let mut moves = Moves {
            item: 7,
            picks: [1, 2, 0],
            mounted_spell: 500,
            mounted_name: "Gallop".into(),
            ..Default::default()
        };
        moves.spells = [[100, 101], [200, 201], [300, 301]];
        moves.names[0] = ["Bite".into(), "Claw".into()];
        moves.names[1] = ["Howl".into(), "Pounce".into()];
        let list = abilities(
            &Inputs {
                book: &book,
                class: &class,
                spells: None,
                pet: Some(&moves),
            },
            &mut |_| Some("Bites hard.".into()),
        );
        let pets: Vec<&Ability> = list
            .iter()
            .filter(|a| a.section == Section::Companion)
            .collect();
        let names: Vec<&str> = pets.iter().map(|a| a.name.as_str()).collect();
        assert_eq!(names, ["Bite", "Pounce", "Gallop"]);
        assert_eq!(pets[1].source, Source::Pet("Pounce".into()));
        assert!(pets[0].tip.iter().any(|l| l.text == "Bites hard."));
        assert!(pets[0].tip.iter().any(|l| l.text == PET_HINT));
        // The companion section comes last.
        assert_eq!(list.last().unwrap().section, Section::Companion);
    }

    /// A stand-in action button under the pointer, whose `OnReceiveDrag` places what the cursor
    /// holds as the stock one does.
    const BAR: &str = r#"
        Placed = nil
        local b = CreateFrame("Button", "ActionButton1", UIParent)
        b:SetWidth(36) b:SetHeight(36)
        b:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", 100, 100)
        b:EnableMouse(true)
        b:SetScript("OnReceiveDrag", function()
          Placed = this:GetName() .. ":" .. tostring(CursorHasSpell())
          ClearCursor()
        end)
    "#;

    fn harness() -> UiScript {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        let (book, _) = warrior();
        s.set_spellbook(book);
        s.run(BAR).expect("the bar");
        s.resolve();
        s
    }

    #[test]
    fn a_drag_onto_an_action_button_places_the_spell() {
        let mut s = harness();
        s.mouse_move(118.0, 118.0);
        let chunk = pickup_lua(&Source::Book(5), true).unwrap();
        assert_eq!(s.eval::<String>(&chunk).unwrap(), "dropped");
        let placed: String = s.eval("return Placed").unwrap();
        assert!(placed.starts_with("ActionButton1:"), "{placed}");
        assert_ne!(placed, "ActionButton1:nil", "the spell was on the cursor");
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn a_click_or_a_drop_on_nothing_leaves_the_spell_on_the_cursor() {
        let mut s = harness();
        s.mouse_move(600.0, 600.0);
        let chunk = pickup_lua(&Source::Book(4), true).unwrap();
        assert_eq!(s.eval::<String>(&chunk).unwrap(), "held");
        assert!(
            s.eval::<bool>("return CursorHasSpell() and true or false")
                .unwrap()
        );
        let click = pickup_lua(&Source::Book(5), false).unwrap();
        assert_eq!(s.eval::<String>(&click).unwrap(), "held");
        assert!(
            s.eval::<bool>("return CursorHasSpell() and true or false")
                .unwrap()
        );
        assert_eq!(s.eval::<Option<String>>("return Placed").unwrap(), None);
        // Nothing to pick up: no Lua at all.
        assert_eq!(pickup_lua(&Source::None, true), None);
        // A companion ability not in the pet's book is missing, not an error.
        let pet = pickup_lua(&Source::Pet("Bite".into()), true).unwrap();
        assert_eq!(s.eval::<String>(&pet).unwrap(), "missing");
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }
    #[test]
    fn the_two_bars_abilities_are_listed_with_their_tips_and_go_back_on_a_bar() {
        use crate::ability_tips::AbilityTips;
        use crate::action_systems::{Ability as Act, tool_bit};
        use benilla_ui::script::LayerTipLine;
        let mut tips = AbilityTips::default();
        for a in Act::ALL {
            tips.tips.insert(
                a.id(),
                vec![LayerTipLine {
                    left: format!("{} name", a.name()),
                    left_color: [1.0; 4],
                    right: None,
                    wrap: false,
                }],
            );
        }
        tips.slots.insert(Act::Primary.id(), 193);
        let list = layer_abilities(&tips, Some(tool_bit(Act::MiningPick)));
        let names: Vec<&str> = list.iter().map(|a| a.name.as_str()).collect();
        assert!(names.contains(&"Primary Weapon name"));
        assert!(names.contains(&"Mining Pick name"));
        assert!(
            !names.contains(&"Fishing Pole name"),
            "a tool not carried is left out"
        );
        let primary = &list[0];
        assert_eq!(primary.section, Section::Modern);
        assert_eq!(primary.tip.last().unwrap().text, DRAG_HINT);
        let bare = list.iter().find(|a| a.name == "Hands name").unwrap();
        assert_eq!(bare.section, Section::Utility);
        // On its bar: moved from its slot; off them: picked up afresh, and it places.
        assert_eq!(
            pickup_lua(&primary.source, false).unwrap(),
            "ClearCursor() PickupAction(193)
return \"held\""
        );
        let mut s = UiScript::new().unwrap();
        let chunk = pickup_lua(&bare.source, false).unwrap();
        hold_layer(&s, &bare.source);
        assert_eq!(s.eval::<String>(&chunk).unwrap(), "held");
        assert!(s.eval::<bool>("return PlaceAction(61)").unwrap());
        assert_eq!(
            s.take_action_sets(),
            vec![(61, 0x2000_0000 | Act::BareHands.id())]
        );
    }
}
