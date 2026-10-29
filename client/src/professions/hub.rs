//! The Professions hub (B, and the micro menu's Professions button): every profession the
//! character knows, the stock ones (Mining, Herbalism, Blacksmithing...) and the layer's
//! (Construction, Minstrelsy), each with its rank bar as the Skills tab draws it, a button that
//! opens its trade window (the stock `TradeSkillFrame`; Mining's is Smelting), and the abilities
//! it puts on Utility's bar (its tools, Construction's Build Structure, Minstrelsy's
//! instruments). A click on an ability puts it on Utility's bar if it is not there yet.
//! Inscription's and Jewelcrafting's rows (the layer's primary professions) also open the
//! Glyphs & Gems window, where their glyphs and gems go into the kit carried.

use benilla_app::fusion::FusionActionBar;
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use super::augments::OpenAugments;
use super::crafts::Trade;
use super::{Book, Crafts, OpenBook};
use crate::Mw2Set;
use crate::action_systems::{Ability, ActionSystems, System, packed, tool_bit};
use crate::build::Building;
use crate::input::Mw2Input;
use crate::minstrel::Minstrel;
use crate::mode::FpsState;
use crate::panels::{Access, OpenPanel, Panel};
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREY, Kind, WHITE, WindowSpec, WowButton, WowTip, text,
    u,
};

/// What a profession's Open button does.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Opener {
    /// A stock trade: its opener spell cast by name (`CastSpellByName`), as the spellbook does.
    Spell(&'static str),
    /// One of the layer's trade windows.
    Layer(Book),
}

/// One profession as the hub knows it: its name, icon, opener and Utility abilities.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProfessionDef {
    pub name: &'static str,
    pub icon: &'static str,
    pub opener: Option<Opener>,
    pub abilities: &'static [Ability],
}

const INSTRUMENTS: [Ability; 6] = [
    Ability::HandDrum,
    Ability::Lute,
    Ability::Flute,
    Ability::Harp,
    Ability::Bagpipes,
    Ability::ThunderDrums,
];

/// Every profession of 1.12 and the layer's four.
pub const PROFESSIONS: [ProfessionDef; 16] = [
    ProfessionDef { name: "Alchemy", icon: "Interface\\Icons\\Trade_Alchemy", opener: Some(Opener::Spell("Alchemy")), abilities: &[] },
    ProfessionDef {
        name: "Blacksmithing",
        icon: "Interface\\Icons\\Trade_BlackSmithing",
        opener: Some(Opener::Spell("Blacksmithing")),
        abilities: &[Ability::BlacksmithHammer, Ability::RepairHammer],
    },
    ProfessionDef { name: "Enchanting", icon: "Interface\\Icons\\Trade_Engraving", opener: Some(Opener::Spell("Enchanting")), abilities: &[] },
    ProfessionDef { name: "Engineering", icon: "Interface\\Icons\\Trade_Engineering", opener: Some(Opener::Spell("Engineering")), abilities: &[] },
    ProfessionDef { name: "Herbalism", icon: "Interface\\Icons\\Trade_Herbalism", opener: None, abilities: &[Ability::Herbalism] },
    ProfessionDef { name: "Leatherworking", icon: "Interface\\Icons\\INV_Misc_ArmorKit_17", opener: Some(Opener::Spell("Leatherworking")), abilities: &[] },
    ProfessionDef { name: "Mining", icon: "Interface\\Icons\\Trade_Mining", opener: Some(Opener::Spell("Smelting")), abilities: &[Ability::MiningPick] },
    ProfessionDef { name: "Skinning", icon: "Interface\\Icons\\INV_Misc_Pelt_Wolf_01", opener: None, abilities: &[Ability::SkinningKnife] },
    ProfessionDef { name: "Tailoring", icon: "Interface\\Icons\\Trade_Tailoring", opener: Some(Opener::Spell("Tailoring")), abilities: &[] },
    ProfessionDef { name: "Cooking", icon: "Interface\\Icons\\INV_Misc_Food_15", opener: Some(Opener::Spell("Cooking")), abilities: &[] },
    ProfessionDef { name: "First Aid", icon: "Interface\\Icons\\Spell_Holy_SealOfSacrifice", opener: Some(Opener::Spell("First Aid")), abilities: &[] },
    ProfessionDef { name: "Fishing", icon: "Interface\\Icons\\Trade_Fishing", opener: None, abilities: &[Ability::FishingPole] },
    ProfessionDef {
        name: "Construction",
        icon: "Interface\\Icons\\INV_Hammer_20",
        opener: Some(Opener::Layer(Book::Construction)),
        abilities: &[Ability::BuildStructure, Ability::WoodcuttingAxe, Ability::RepairHammer],
    },
    ProfessionDef {
        name: "Minstrelsy",
        icon: "Interface\\Icons\\INV_Misc_Note_01",
        opener: Some(Opener::Layer(Book::Minstrelsy)),
        abilities: &INSTRUMENTS,
    },
    ProfessionDef {
        name: "Inscription",
        icon: "Interface\\Icons\\INV_Feather_05",
        opener: Some(Opener::Layer(Book::Inscription)),
        abilities: &[],
    },
    ProfessionDef {
        name: "Jewelcrafting",
        icon: "Interface\\Icons\\INV_Misc_Gem_01",
        opener: Some(Opener::Layer(Book::Jewelcrafting)),
        abilities: &[],
    },
];

/// Whether a profession's row offers the Glyphs & Gems window.
pub fn has_augments(name: &str) -> bool {
    name == "Inscription" || name == "Jewelcrafting"
}

pub fn profession(name: &str) -> Option<&'static ProfessionDef> {
    PROFESSIONS.iter().find(|p| p.name == name)
}

/// A line of the stock Skills tab as the scan reads it: its header, name, rank and max.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SkillLine {
    pub header: String,
    pub name: String,
    pub rank: u32,
    pub max: u32,
}

/// One row of the hub.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HubRow {
    pub def: ProfessionDef,
    /// A primary profession (the Professions header) rather than a secondary one.
    pub primary: bool,
    pub rank: u32,
    pub max: u32,
}

/// The hub's rows: the Skills tab's professions (primary first, then secondary, in the tab's
/// order), and the layer's four from their own state when the tab doesn't list them yet
/// (Inscription and Jewelcrafting among the primary ones).
pub fn hub_rows(
    skills: &[SkillLine],
    construction: Option<(u16, u16)>,
    minstrelsy: Option<(u16, u16)>,
    crafts: [Option<(u16, u16)>; 2],
) -> Vec<HubRow> {
    let mut rows: Vec<HubRow> = Vec::new();
    for primary in [true, false] {
        for s in skills {
            let is_primary = s.header == "Professions";
            if is_primary != primary || !(is_primary || s.header == "Secondary Skills") {
                continue;
            }
            let Some(def) = profession(&s.name) else { continue };
            if rows.iter().any(|r| r.def.name == def.name) {
                continue;
            }
            rows.push(HubRow { def: *def, primary, rank: s.rank, max: s.max });
        }
        // The layer's primary trades, last of the primaries.
        if primary {
            for (name, state) in [("Inscription", crafts[0]), ("Jewelcrafting", crafts[1])] {
                if let Some((rank, max)) = state
                    && !rows.iter().any(|r| r.def.name == name)
                    && let Some(def) = profession(name)
                {
                    rows.push(HubRow { def: *def, primary: true, rank: u32::from(rank), max: u32::from(max) });
                }
            }
        }
    }
    for (name, state) in [("Construction", construction), ("Minstrelsy", minstrelsy)] {
        if let Some((rank, max)) = state
            && !rows.iter().any(|r| r.def.name == name)
            && let Some(def) = profession(name)
        {
            rows.push(HubRow { def: *def, primary: false, rank: u32::from(rank), max: u32::from(max) });
        }
    }
    rows
}

/// The scan of the Skills tab: every line under each header, folded headers opened for the
/// read and folded again, a line each as `header \t name \t rank \t max`.
pub const SKILLS_LUA: &str = r#"if not GetNumSkillLines then return "" end
local out, fold, cur = {}, {}, nil
local i = 1
while i <= GetNumSkillLines() do
  local name, header, expanded, rank, _, _, max = GetSkillLineInfo(i)
  if header then
    cur = name
    if not expanded and ExpandSkillHeader then
      ExpandSkillHeader(i)
      table.insert(fold, name)
    end
  elseif cur and name then
    table.insert(out, cur .. "\t" .. name .. "\t" .. (rank or 0) .. "\t" .. (max or 0))
  end
  i = i + 1
end
for _, h in ipairs(fold) do
  for j = 1, GetNumSkillLines() do
    local n, hd = GetSkillLineInfo(j)
    if hd and n == h then CollapseSkillHeader(j) break end
  end
end
return table.concat(out, "\n")"#;

pub fn parse_skills(s: &str) -> Vec<SkillLine> {
    s.lines()
        .filter_map(|l| {
            let mut f = l.split('\t');
            Some(SkillLine {
                header: f.next()?.to_string(),
                name: f.next()?.to_string(),
                rank: f.next()?.parse().ok()?,
                max: f.next()?.parse().ok()?,
            })
        })
        .collect()
}

/// Whether an ability can be used now: a tool carried and its trade known (the server's tools
/// mask), Build Structure with Construction learned.
pub fn ability_usable(a: Ability, tools: Option<u16>, construction: bool) -> bool {
    match a {
        Ability::BuildStructure => construction,
        a if a.is_tool() => tools.is_some_and(|m| m & tool_bit(a) != 0),
        _ => true,
    }
}

/// The write that puts an ability on Utility's bar: none when it is there already, or the bar is
/// full.
pub fn place_on_utility(saved: &std::collections::HashMap<u32, u32>, a: Ability) -> Option<(u32, u32)> {
    let word = packed(a);
    if System::Utility.actions().any(|x| saved.get(&x) == Some(&word)) {
        return None;
    }
    System::Utility
        .actions()
        .find(|x| !saved.contains_key(x))
        .map(|x| (x, word))
}

#[derive(Resource, Default)]
pub struct Hub {
    pub open: bool,
    pub rows: Vec<HubRow>,
    pub skills: Vec<SkillLine>,
    scan_at: f32,
    drawn: Option<Vec<HubRow>>,
    drawn_tools: Option<Option<u16>>,
    held_cursor: bool,
}

#[derive(Component)]
struct HubRoot;
#[derive(Component)]
struct HubClose;
#[derive(Component)]
struct HubBody;
#[derive(Component, Clone, Copy)]
struct OpenButton(Opener);
#[derive(Component)]
struct HomesteadButton;
#[derive(Component)]
struct AugmentsButton;
#[derive(Component, Clone, Copy)]
struct AbilityButton(Ability);
#[derive(Resource)]
struct HubFont(Handle<Font>);

pub struct HubPlugin;

impl Plugin for HubPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Hub>()
            .add_systems(Update, (spawn, open_close, scan, click, draw).chain().in_set(Mw2Set::Hud))
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

const WIDTH: f32 = 430.0;
const ICON: f32 = 30.0;
const SMALL_ICON: f32 = 24.0;
const BAR_W: f32 = 210.0;

fn spawn(mut commands: Commands, assets: Res<AssetServer>, state: Res<FpsState>, mut built: Local<bool>) {
    if *built || !crate::capture_keep::in_world(state.in_world, "professions") {
        return;
    }
    *built = true;
    let font: Handle<Font> = assets.load(FONT);
    let spec = WindowSpec {
        key: "professions",
        title: "Professions".into(),
        frame: Frame::Dialog,
        width: Some(WIDTH),
        movable: true,
        anchor: Anchor::CENTER,
        z: i32::MAX - 3,
    };
    wow_ui::spawn_window(
        &mut commands,
        &font,
        spec,
        (HubRoot, Visibility::Hidden, wow_ui::WindowSounds::SPELLBOOK),
        Some(HubClose),
        |root| {
            wow_ui::scroll_frame(root, 420.0, None, HubBody, |_| {});
        },
        |_| {},
    );
    commands.insert_resource(HubFont(font));
}

fn open_close(
    input: Res<Mw2Input>,
    state: Res<FpsState>,
    mut hub: ResMut<Hub>,
    mut panels: MessageReader<OpenPanel>,
    close: Query<&Interaction, (With<HubClose>, Changed<Interaction>)>,
) {
    for p in panels.read().filter(|p| p.panel == Panel::Professions) {
        hub.open = !(hub.open && p.access == Access::Hud);
        hub.scan_at = 0.0;
    }
    let closed = close.iter().any(|i| *i == Interaction::Pressed);
    if hub.open && (input.escape || closed || !crate::capture_keep::in_world(state.in_world, "professions")) {
        hub.open = false;
    }
    if !hub.open && crate::capture_keep::keeps("professions") {
        hub.open = true;
    }
}

fn scan(
    time: Res<Time>,
    script: Option<NonSend<UiScript>>,
    build: Res<Building>,
    minstrel: Res<Minstrel>,
    crafts: Res<Crafts>,
    mut hub: ResMut<Hub>,
) {
    if !hub.open {
        return;
    }
    let t = time.elapsed_secs();
    if t >= hub.scan_at
        && let Some(s) = script.as_deref()
    {
        hub.scan_at = t + 1.0;
        if let Some(out) = crate::lua::eval::<String>(s, SKILLS_LUA) {
            hub.skills = parse_skills(&out);
        }
    }
    let construction = (build.tier > 0).then_some((build.skill, build.cap.max(super::book::tier_cap(build.tier))));
    let (mt, ms, mc) = minstrel.skill;
    let minstrelsy = (mt > 0).then_some((ms, mc.max(super::book::tier_cap(mt))));
    let rows = hub_rows(
        &hub.skills,
        construction,
        minstrelsy,
        [crafts.learned(Trade::Inscription), crafts.learned(Trade::Jewelcrafting)],
    );
    if hub.rows != rows {
        hub.rows = rows;
    }
}

#[allow(clippy::too_many_arguments)]
fn click(
    mut hub: ResMut<Hub>,
    script: Option<NonSend<UiScript>>,
    bar: Option<ResMut<FusionActionBar>>,
    mut books: MessageWriter<OpenBook>,
    mut panels: MessageWriter<OpenPanel>,
    mut augments: MessageWriter<OpenAugments>,
    opens: Query<(&Interaction, &OpenButton), Changed<Interaction>>,
    homes: Query<&Interaction, (With<HomesteadButton>, Changed<Interaction>)>,
    augment_buttons: Query<&Interaction, (With<AugmentsButton>, Changed<Interaction>)>,
    abilities: Query<(&Interaction, &AbilityButton), Changed<Interaction>>,
) {
    if !hub.open {
        return;
    }
    let pressed = |i: &Interaction| *i == Interaction::Pressed;
    for (i, b) in &opens {
        if !pressed(i) {
            continue;
        }
        match b.0 {
            Opener::Spell(name) => {
                if let Some(s) = script.as_deref() {
                    crate::lua::run(s, &format!("CastSpellByName({})", crate::lua::quote(name)));
                }
            }
            Opener::Layer(book) => {
                books.write(OpenBook(book));
            }
        }
        hub.open = false;
    }
    if homes.iter().any(pressed) {
        panels.write(OpenPanel { panel: Panel::Construction, access: Access::Hud });
        hub.open = false;
    }
    if augment_buttons.iter().any(pressed) {
        augments.write(OpenAugments);
        hub.open = false;
    }
    let Some(mut bar) = bar else { return };
    for (i, b) in &abilities {
        if !pressed(i) {
            continue;
        }
        if let Some(w) = place_on_utility(&bar.saved, b.0) {
            info!("benilla-mw2: professions: {} put on Utility's bar", b.0.name());
            bar.writes.push(w);
            if let Some(s) = script.as_deref() {
                crate::lua::sound(s, "igAbilityIconDrop");
            }
        }
        // Drawn again: the tooltip says it is there now.
        hub.drawn = None;
    }
}

#[allow(clippy::too_many_arguments)]
fn draw(
    mut commands: Commands,
    mut hub: ResMut<Hub>,
    font: Option<Res<HubFont>>,
    assets: Res<AssetServer>,
    systems: Res<ActionSystems>,
    build: Res<Building>,
    bar: Option<Res<FusionActionBar>>,
    mut root: Query<&mut Visibility, With<HubRoot>>,
    body: Query<(Entity, Option<&Children>), With<HubBody>>,
) {
    let Some(font) = font else { return };
    let want = if hub.open { Visibility::Inherited } else { Visibility::Hidden };
    for mut v in &mut root {
        if *v != want {
            *v = want;
        }
    }
    if !hub.open {
        hub.drawn = None;
        return;
    }
    if hub.drawn.as_ref() == Some(&hub.rows) && hub.drawn_tools == Some(systems.usable_tools) {
        return;
    }
    hub.drawn = Some(hub.rows.clone());
    hub.drawn_tools = Some(systems.usable_tools);
    let f = &font.0;
    let Ok((e, children)) = body.single() else { return };
    for c in children.into_iter().flatten() {
        commands.entity(*c).despawn();
    }
    let rows = hub.rows.clone();
    let saved = bar.as_deref().map(|b| b.saved.clone()).unwrap_or_default();
    let construction = build.tier > 0;
    commands.entity(e).with_children(|b| {
        if rows.is_empty() {
            b.spawn(text(
                f,
                "You know no professions yet. Trainers in the cities teach them: Construction's, \
                 Minstrelsy's, Inscription's and Jewelcrafting's stand by the inns.",
                11.0,
                DIM,
            ));
        }
        let mut last_primary: Option<bool> = None;
        for row in &rows {
            if last_primary != Some(row.primary) {
                last_primary = Some(row.primary);
                wow_ui::section_header(b, f, if row.primary { "Professions" } else { "Secondary Skills" });
            }
            b.spawn(Node {
                flex_direction: FlexDirection::Row,
                column_gap: u(8.0),
                padding: UiRect::all(u(3.0)),
                align_items: AlignItems::FlexStart,
                ..default()
            })
            .with_children(|r| {
                r.spawn(wow_ui::icon_frame(
                    Some(assets.load(crate::cac_ui::spell_icon_path(row.def.icon))),
                    ICON,
                    GREY,
                ));
                r.spawn(Node { flex_direction: FlexDirection::Column, row_gap: u(3.0), ..default() })
                    .with_children(|c| {
                        c.spawn(Node { flex_direction: FlexDirection::Row, column_gap: u(6.0), align_items: AlignItems::Center, ..default() })
                            .with_children(|h| {
                                h.spawn(text(f, row.def.name, 13.0, GOLD));
                                if let Some(o) = row.def.opener {
                                    h.spawn(wow_ui::button_fit(
                                        f,
                                        "Open",
                                        WowButton::new(Kind::Panel),
                                        (
                                            OpenButton(o),
                                            WowTip::new(row.def.name, "Open its trade window: its recipes, what they take, Create and Create All."),
                                        ),
                                    ));
                                }
                                if has_augments(row.def.name) {
                                    h.spawn(wow_ui::button_fit(
                                        f,
                                        "Glyphs & Gems",
                                        WowButton::new(Kind::Panel),
                                        (
                                            AugmentsButton,
                                            WowTip::new(
                                                "Glyphs & Gems",
                                                "The kit you carry: its glyph slots and its guns' gem sockets.",
                                            ),
                                        ),
                                    ));
                                }
                                if row.def.name == "Construction" {
                                    h.spawn(wow_ui::button_fit(
                                        f,
                                        "Homestead",
                                        WowButton::new(Kind::Panel),
                                        (
                                            HomesteadButton,
                                            WowTip::new("Homestead", "Your plot, its upkeep and the pieces catalog (the Construction window)."),
                                        ),
                                    ));
                                }
                            });
                        skill_bar(c, f, row.rank, row.max);
                        if !row.def.abilities.is_empty() {
                            c.spawn(Node { flex_direction: FlexDirection::Row, column_gap: u(3.0), ..default() })
                                .with_children(|a| {
                                    for &ab in row.def.abilities {
                                        let usable = ability_usable(ab, systems.usable_tools, construction);
                                        let placed = place_on_utility(&saved, ab).is_none()
                                            && System::Utility.actions().any(|x| saved.get(&x) == Some(&packed(ab)));
                                        let body = if placed {
                                            "On Utility's bar (Z puts the weapons away into Utility)."
                                        } else if usable {
                                            "Click to put it on Utility's bar."
                                        } else {
                                            "Not usable yet: carry its tool and know its trade (an instrument: Minstrelsy to its skill)."
                                        };
                                        a.spawn((
                                            Button,
                                            AbilityButton(ab),
                                            WowTip::new(ab.name(), body),
                                            Node { width: u(SMALL_ICON), height: u(SMALL_ICON), ..default() },
                                        ))
                                        .with_children(|i| {
                                            i.spawn(wow_ui::icon_frame_lit(
                                                Some(assets.load(crate::cac_ui::spell_icon_path(ab.icon()))),
                                                SMALL_ICON,
                                                GREY,
                                                usable,
                                            ));
                                        });
                                    }
                                });
                        }
                    });
            });
        }
        b.spawn((
            Node { margin: UiRect::top(u(6.0)), max_width: u(WIDTH - 40.0), ..default() },
            children![text(
                f,
                "Abilities go on Utility's bar (Z): tools are held and swung, Build Structure puts up \
                 the pieces made at Construction's trade window, and an instrument turns the bar \
                 into its notes.",
                10.0,
                DIM,
            )],
        ));
    });
}

/// A rank bar as the Skills tab draws one: blue, the rank over the max on it.
fn skill_bar(c: &mut ChildSpawnerCommands, f: &Handle<Font>, rank: u32, max: u32) {
    let share = if max == 0 { 0.0 } else { (rank as f32 / max as f32).clamp(0.0, 1.0) };
    c.spawn((
        Node {
            width: u(BAR_W),
            height: u(14.0),
            border: UiRect::all(u(1.0)),
            justify_content: JustifyContent::Center,
            align_items: AlignItems::Center,
            ..default()
        },
        BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.6)),
        BorderColor::all(Color::srgb(0.45, 0.45, 0.45)),
    ))
    .with_children(|b| {
        b.spawn((
            Node {
                position_type: PositionType::Absolute,
                left: u(0.0),
                top: u(0.0),
                bottom: u(0.0),
                width: Val::Percent(share * 100.0),
                ..default()
            },
            BackgroundColor(Color::srgb(0.0, 0.0, 0.75)),
        ));
        b.spawn(text(f, &format!("{rank}/{max}"), 10.0, WHITE));
    });
}

fn hold_cursor(mut hub: ResMut<Hub>, mut state: ResMut<FpsState>) {
    if hub.open {
        if !state.panel {
            state.panel = true;
        }
        hub.held_cursor = true;
    } else if hub.held_cursor {
        hub.held_cursor = false;
        state.panel = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    fn line(header: &str, name: &str, rank: u32, max: u32) -> SkillLine {
        SkillLine { header: header.into(), name: name.into(), rank, max }
    }

    #[test]
    fn the_hub_lists_every_profession_primary_first() {
        let skills = vec![
            line("Weapon Skills", "Swords", 10, 15),
            line("Secondary Skills", "Fishing", 40, 75),
            line("Professions", "Mining", 120, 150),
            line("Professions", "Blacksmithing", 90, 150),
            line("Secondary Skills", "Construction", 30, 75),
        ];
        let rows = hub_rows(&skills, Some((30, 75)), Some((5, 75)), [None, Some((40, 75))]);
        let names: Vec<&str> = rows.iter().map(|r| r.def.name).collect();
        assert_eq!(names, ["Mining", "Blacksmithing", "Jewelcrafting", "Fishing", "Construction", "Minstrelsy"]);
        assert!(rows[0].primary && rows[2].primary && !rows[3].primary);
        assert_eq!((rows[0].rank, rows[0].max), (120, 150));
        assert_eq!((rows[2].rank, rows[2].max), (40, 75));
        // The layer's lines from their own state when the tab doesn't list them.
        assert_eq!((rows[5].rank, rows[5].max), (5, 75));
        // Nothing learned: nothing listed.
        assert!(hub_rows(&[], None, None, [None, None]).is_empty());
        // The tab listing Inscription already: one row, under Professions.
        let tab = vec![line("Professions", "Inscription", 60, 75)];
        let rows = hub_rows(&tab, None, None, [Some((60, 75)), None]);
        assert_eq!(rows.len(), 1);
        assert!(rows[0].primary && has_augments(rows[0].def.name));
    }

    #[test]
    fn each_profession_has_its_opener_and_abilities() {
        let c = profession("Construction").unwrap();
        assert_eq!(c.opener, Some(Opener::Layer(Book::Construction)));
        assert!(c.abilities.contains(&Ability::BuildStructure));
        let m = profession("Minstrelsy").unwrap();
        assert_eq!(m.abilities.len(), 6);
        assert!(m.abilities.iter().all(|a| a.is_instrument()));
        assert_eq!(profession("Mining").unwrap().opener, Some(Opener::Spell("Smelting")));
        assert_eq!(profession("Herbalism").unwrap().opener, None);
        assert_eq!(profession("Inscription").unwrap().opener, Some(Opener::Layer(Book::Inscription)));
        assert_eq!(profession("Jewelcrafting").unwrap().opener, Some(Opener::Layer(Book::Jewelcrafting)));
        assert!(!has_augments("Mining"));
        // Every Utility ability belongs to some profession (bare hands aside).
        for a in Ability::ALL.into_iter().filter(|a| a.systems() == [System::Utility]) {
            assert!(PROFESSIONS.iter().any(|p| p.abilities.contains(&a)), "{a:?}");
        }
    }

    #[test]
    fn abilities_go_on_utilitys_bar_once() {
        let mut saved: HashMap<u32, u32> = HashMap::new();
        let w = place_on_utility(&saved, Ability::BuildStructure).expect("a free slot");
        assert_eq!(w, (61, packed(Ability::BuildStructure)));
        saved.insert(w.0, w.1);
        assert_eq!(place_on_utility(&saved, Ability::BuildStructure), None);
        assert!(ability_usable(Ability::BuildStructure, None, true));
        assert!(!ability_usable(Ability::BuildStructure, None, false));
        assert!(ability_usable(Ability::MiningPick, Some(tool_bit(Ability::MiningPick)), false));
        assert!(!ability_usable(Ability::MiningPick, Some(0), true));
    }

    #[test]
    fn the_skills_scan_reads_back_and_runs() {
        let s = parse_skills("Professions\tMining\t120\t150\nbroken\n");
        assert_eq!(s, vec![line("Professions", "Mining", 120, 150)]);
        let vm = UiScript::new().expect("a VM");
        crate::lua::run(
            &vm,
            r#"local rows = {{"Professions", 1, 1}, {"Mining", nil, nil, 120, 150}, {"Secondary Skills", 1, nil}}
               local open = {false, false, false}
               function GetNumSkillLines() return 3 end
               function GetSkillLineInfo(i) local r = rows[i]
                 if r[2] then return r[1], 1, r[3] end
                 return r[1], nil, nil, r[4], 0, 0, r[5] end
               function ExpandSkillHeader(i) end
               function CollapseSkillHeader(i) end"#,
        );
        let out = crate::lua::eval::<String>(&vm, SKILLS_LUA).expect("scan");
        assert_eq!(parse_skills(&out), vec![line("Professions", "Mining", 120, 150)]);
    }
}
