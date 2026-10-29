//! The build menu: entering build mode (from the Construction window) opens a picker over the world: categories along the
//! top (Wood, Stone, Defence, Camp, Objects, Structures, Blueprints), the builder's style
//! (Elwynn, Stormwind, Ironforge, Orcish, Night elf: crate::build::look) above the pieces, and a
//! tile for each piece with a look at it, its materials and the Construction skill it asks,
//! coloured as vanilla's recipes (orange, yellow, green, gray) and greyed out of reach; a click
//! takes the piece in hand and closes the menu so the ghost can be placed. The title shows the
//! player's Construction. Tab opens it again while building, Esc or a second Tab closes it; B
//! opens the Construction window (crate::build::homestead) over it.

use bevy::prelude::*;

use super::look::{self, STYLES};
use super::pieces::{self, Choice, Material, PieceType, STRUCTURE_BASE, choice, choices, def};
use super::wire::SkillColor;
use super::{
    Building, choice_name, cost_text, in_style, locked_reason, required_skill, skill_text,
};
use crate::Mw2Set;
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREY, Kind, WHITE, WindowSpec, WowButton, text, u,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum Category {
    #[default]
    Wood,
    Stone,
    Defence,
    Camp,
    Objects,
    Structures,
    Furniture,
    Blueprints,
}

pub const CATEGORIES: [Category; 8] = [
    Category::Wood,
    Category::Stone,
    Category::Defence,
    Category::Camp,
    Category::Objects,
    Category::Structures,
    Category::Furniture,
    Category::Blueprints,
];

impl Category {
    pub fn label(self) -> &'static str {
        match self {
            Category::Wood => "WOOD",
            Category::Stone => "STONE",
            Category::Defence => "DEFENCE",
            Category::Camp => "CAMP",
            Category::Objects => "OBJECTS",
            Category::Structures => "STRUCTURES",
            Category::Furniture => "FURNITURE",
            Category::Blueprints => "BLUEPRINTS",
        }
    }

    /// Its tab's caption.
    pub fn title(self) -> &'static str {
        match self {
            Category::Wood => "Wood",
            Category::Stone => "Stone",
            Category::Defence => "Defense",
            Category::Camp => "Camp",
            Category::Objects => "Objects",
            Category::Structures => "Structures",
            Category::Furniture => "Furniture",
            Category::Blueprints => "Blueprints",
        }
    }
}

/// Which category a bar choice sits in.
/// A category's icon (a stock one in the install's `Interface\Icons`), beside its pieces.
pub fn category_icon(cat: Category) -> &'static str {
    match cat {
        Category::Wood => "Interface\\Icons\\INV_Misc_Branch_01",
        Category::Stone => "Interface\\Icons\\INV_Stone_04",
        Category::Defence => "Interface\\Icons\\INV_Shield_06",
        Category::Camp => "Interface\\Icons\\Spell_Fire_Fire",
        Category::Objects => "Interface\\Icons\\INV_Misc_Gear_01",
        Category::Structures => "Interface\\Icons\\INV_Hammer_20",
        Category::Furniture => "Interface\\Icons\\INV_Misc_Bag_10",
        Category::Blueprints => "Interface\\Icons\\INV_Misc_Note_01",
    }
}

/// A choice's look for its tile and its catalogue line: the face of a piece in the builder's
/// style (its wall, its roof, its boards), else its category's icon.
pub fn preview_path(build: &Building, c: Choice) -> String {
    if let Choice::Piece(t) = c
        && let Some(tex) = look::swatch(in_style(build, t))
    {
        return look::mpq_url(tex.path);
    }
    crate::cac_ui::spell_icon_path(category_icon(category_of(c)))
}

pub fn category_of(c: Choice) -> Category {
    match c {
        Choice::Blueprint(_) => Category::Blueprints,
        Choice::Piece(t) if pieces::is_furniture(t) => Category::Furniture,
        Choice::Piece(t) if t.0 >= STRUCTURE_BASE => Category::Structures,
        Choice::Piece(t) if t.is_prop() => Category::Objects,
        Choice::Piece(t) => match t {
            PieceType::Sandbags => Category::Defence,
            PieceType::Campfire | PieceType::Torch | PieceType::Bedroll => Category::Camp,
            _ if def(t).material == Material::Stone => Category::Stone,
            _ => Category::Wood,
        },
    }
}

/// The bar indices in a category, in bar order.
pub fn in_category(cat: Category) -> Vec<usize> {
    (0..choices())
        .filter(|&i| category_of(choice(i)) == cat)
        .collect()
}

#[derive(Resource, Default)]
pub struct BuildMenu {
    pub open: bool,
    pub category: Category,
    /// The category, Construction's version and the style the tiles were last made for.
    drawn: Option<(Category, u32, u8)>,
    was_building: bool,
    /// Entering build mode with a piece already in hand (from the Construction window's
    /// catalogue): the menu stays down this once.
    pub skip_open: bool,
}

#[derive(Component)]
struct MenuRoot;

#[derive(Component)]
struct TileGrid;

#[derive(Component, Clone, Copy)]
struct Tile(usize);

#[derive(Component, Clone, Copy)]
struct Tab(Category);

#[derive(Component)]
struct MenuTitle;

/// A style's button.
#[derive(Component, Clone, Copy)]
pub struct StyleButton(pub u8);

/// The line under the styles saying what the chosen one is.
#[derive(Component)]
struct StyleBlurb;

const LOCKED: Color = Color::srgb(0.45, 0.45, 0.43);

/// A recipe's colour as WoW draws a trade skill's: red out of reach, then orange, yellow, green,
/// gray.
pub fn skill_color(c: SkillColor) -> Color {
    match c {
        SkillColor::Red => Color::srgb(0.9, 0.2, 0.2),
        SkillColor::Orange => Color::srgb(1.0, 0.5, 0.25),
        SkillColor::Yellow => Color::srgb(1.0, 1.0, 0.0),
        SkillColor::Green => Color::srgb(0.25, 0.75, 0.25),
        SkillColor::Gray => Color::srgb(0.5, 0.5, 0.5),
    }
}

/// A tile's lines under its name and the skill line's colour: the materials, and the skill asked
/// (or why it's locked).
pub fn tile_lines(build: &Building, c: Choice) -> (String, String, Color) {
    let cost = if build.kits_known && !build.free {
        format!("{} kit(s) held", super::kits_held(build, c))
    } else {
        cost_text(build, c)
    };
    if build.free || build.recipes.is_empty() {
        return (cost, String::new(), DIM);
    }
    if let Some(why) = locked_reason(build, c) {
        return (cost, why, LOCKED);
    }
    let color = match c {
        Choice::Piece(t) => build
            .recipes
            .get(&t.0)
            .map_or(DIM, |r| skill_color(r.color(build.skill))),
        Choice::Blueprint(_) => DIM,
    };
    let needs = match required_skill(build, c) {
        0 => String::new(),
        n => format!("Construction {n}"),
    };
    (cost, needs, color)
}

/// A choice's tooltip, as a trade skill's recipe shows on hover: its name, what it takes, the
/// Construction it asks (or why it's locked), how much ground it covers, and what a click does.
pub fn choice_tip(build: &Building, c: Choice) -> wow_ui::WowTip {
    use crate::wow_ui::{SMALL_SIZE, TipLine};
    let (cost, needs, color) = tile_lines(build, c);
    let mut lines = vec![TipLine::new(choice_name(build, c), WHITE, 13.0)];
    if !cost.is_empty() {
        lines.push(TipLine::new(format!("Takes: {cost}"), WHITE, SMALL_SIZE));
    }
    if !needs.is_empty() {
        lines.push(TipLine::new(needs, color, SMALL_SIZE));
    }
    if let Some(size) = super::homestead::footprint_text(c) {
        lines.push(TipLine::new(size, DIM, SMALL_SIZE));
    }
    for line in piece_notes(c) {
        lines.push(TipLine::new(line, WHITE, SMALL_SIZE));
    }
    // Only fresh materials teach: what came back out of a piece builds but teaches nothing.
    if !build.free && !build.recipes.is_empty() {
        lines.push(TipLine::new(super::wire::SALVAGE_NOTE, DIM, SMALL_SIZE));
    }
    let what = if locked_reason(build, c).is_some() {
        "Out of reach for now."
    } else if matches!(c, Choice::Blueprint(_)) {
        "Click to take it in hand: it goes up piece by piece, each taking its kit as it is placed."
    } else {
        "Click to take it in hand, then left-click to place it."
    };
    lines.push(TipLine::new(what, GOLD, SMALL_SIZE));
    wow_ui::WowTip(lines)
}

/// What a piece does beyond standing there, for its tooltip: a door's and a portal's lock, and
/// where a portal goes.
pub fn piece_notes(c: Choice) -> Vec<&'static str> {
    let Choice::Piece(t) = c else {
        return Vec::new();
    };
    let mut out = Vec::new();
    if t.is_portal() {
        out.push(
            "Opens on your faction's capitals and your homestead ({WOMW_INTERACT} to use). Shares your hearthstone's cooldown. Two to a player, one to a homestead.",
        );
    }
    if super::pieces::lockable(t) {
        out.push(
            "Takes a lock: you, your party (the default), your guild, a code, or anyone. {WOMW_INTERACT} on it in build mode sets it.",
        );
    }
    out
}

pub struct BuildMenuPlugin;

impl Plugin for BuildMenuPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<BuildMenu>().add_systems(
            Update,
            (spawn_menu, open_close, click, pick_style, show_style, draw)
                .chain()
                .in_set(Mw2Set::Mode)
                .after(super::BuildInput)
                .before(crate::cac::CacSet),
        );
    }
}

/// How tall the tile list may be, in UI units, before it scrolls.
const GRID_HEIGHT: f32 = 300.0;

/// The menu's window, built at the first world frame (at the session's UI scale) and hidden
/// until build mode opens it.
fn spawn_menu(
    mut commands: Commands,
    assets: Res<AssetServer>,
    state: Res<FpsState>,
    mut built: Local<bool>,
    keymap: Res<crate::keybinds::Keymap>,
) {
    if *built || !crate::capture_keep::in_world(state.in_world, "build") {
        return;
    }
    *built = true;
    let build_key = keymap.label(crate::keybinds::Action::Build);
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: "build",
        title: "Build".into(),
        frame: Frame::Dialog,
        width: Some(600.0),
        movable: true,
        anchor: Anchor::CENTER,
        z: i32::MAX - 1,
    };
    wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        (
            MenuRoot,
            Visibility::Hidden,
            wow_ui::WindowSounds::CHARACTER,
        ),
        Some(MenuClose),
        |root| {
            root.spawn((
                Node {
                    min_height: u(14.0),
                    margin: UiRect::bottom(u(6.0)),
                    ..default()
                },
                children![(MenuTitle, text(f, "", 12.0, WHITE))],
            ));
            style_row(root, f);
            wow_ui::inset(
                root,
                Node {
                    flex_direction: FlexDirection::Column,
                    ..default()
                },
                (),
                |c| {
                    wow_ui::scroll_frame(c, GRID_HEIGHT, None, TileGrid, |_| {});
                },
            );
            root.spawn((
                Node {
                    margin: UiRect::top(u(6.0)),
                    ..default()
                },
                children![text(
                    f,
                    &format!(
                        "Click a piece to take it in hand  ·  {build_key}: Construction  ·  Esc: close"
                    ),
                    10.0,
                    GREY,
                )],
            ));
        },
        |below| {
            wow_ui::tab_strip(below, |tabs| {
                for cat in CATEGORIES {
                    tabs.spawn(wow_ui::tab(f, cat.title(), Tab(cat)));
                }
            });
        },
    );
    commands.insert_resource(MenuFont(font));
}

#[derive(Resource)]
struct MenuFont(Handle<Font>);

/// The builder's styles as a row of buttons with a line on the chosen one: the Build menu's and
/// the Construction window's catalogue's.
pub fn style_row(root: &mut ChildSpawnerCommands, f: &Handle<Font>) {
    root.spawn(Node {
        flex_direction: FlexDirection::Row,
        flex_wrap: FlexWrap::Wrap,
        align_items: AlignItems::Center,
        ..default()
    })
    .with_children(|row| {
        row.spawn((
            Node {
                margin: UiRect::right(u(4.0)),
                ..default()
            },
            children![text(f, "Style:", 11.0, GREY)],
        ));
        for (i, s) in STYLES.iter().enumerate() {
            row.spawn(wow_ui::button_fit(
                f,
                s.name,
                WowButton::new(Kind::Panel),
                (StyleButton(i as u8), wow_ui::WowTip::new(s.name, s.blurb)),
            ));
        }
    });
    root.spawn((
        Node {
            min_height: u(13.0),
            margin: UiRect::new(u(2.0), u(0.0), u(2.0), u(6.0)),
            ..default()
        },
        children![(StyleBlurb, text(f, "", 10.0, DIM))],
    ));
}

/// A style button pressed: the builder's style (walls, floors, roofs, stairs, beams, masonry,
/// sandbags and the camp's things go up in it).
fn pick_style(
    mut build: ResMut<Building>,
    buttons: Query<(&Interaction, &StyleButton), Changed<Interaction>>,
) {
    for (i, b) in &buttons {
        if *i == Interaction::Pressed && build.style != b.0 {
            build.style = b.0.min(look::STYLE_COUNT - 1);
        }
    }
}

/// Every style button shows the builder's style, and the line under them says what it is.
fn show_style(
    build: Res<Building>,
    mut buttons: Query<(&StyleButton, &mut WowButton)>,
    mut blurbs: Query<&mut Text, With<StyleBlurb>>,
) {
    for (b, mut state) in &mut buttons {
        let selected = b.0 == build.style;
        if state.selected != selected {
            state.selected = selected;
        }
    }
    let want = STYLES[usize::from(build.style).min(STYLES.len() - 1)].blurb;
    for mut t in &mut blurbs {
        if t.0 != want {
            t.0 = want.to_string();
        }
    }
}

/// The menu's close button.
#[derive(Component)]
struct MenuClose;

fn open_close(
    input: Res<Mw2Input>,
    build: Res<Building>,
    mut menu: ResMut<BuildMenu>,
    mut state: ResMut<FpsState>,
    close: Query<&Interaction, (With<MenuClose>, Changed<Interaction>)>,
) {
    // Entering build mode opens the menu; leaving closes it.
    if build.on && !menu.was_building {
        menu.open = !menu.skip_open;
    }
    menu.skip_open = false;
    menu.was_building = build.on;
    // The capture switch shows the menu with no build mode (client/docs/CAPTURE.md).
    let kept = crate::capture_keep::keeps("build");
    if kept {
        menu.open = true;
    } else if !build.on {
        menu.open = false;
    } else if input.build_menu {
        menu.open = !menu.open;
    } else if (input.escape || close.iter().any(|i| *i == Interaction::Pressed)) && menu.open {
        menu.open = false;
    }
    let want = menu.open && (build.on || kept);
    if state.build_menu != want {
        state.build_menu = want;
    }
}

fn click(
    time: Res<Time>,
    mut menu: ResMut<BuildMenu>,
    mut build: ResMut<Building>,
    tiles: Query<(&Interaction, &Tile), Changed<Interaction>>,
    tabs: Query<(&Interaction, &Tab), Changed<Interaction>>,
) {
    if !menu.open {
        return;
    }
    for (i, tab) in &tabs {
        if *i == Interaction::Pressed {
            menu.category = tab.0;
        }
    }
    for (i, tile) in &tiles {
        if *i == Interaction::Pressed {
            // Out of the player's Construction: say why, and keep the menu open.
            if let Some(why) = locked_reason(&build, choice(tile.0)) {
                build.toast = Some((why, time.elapsed_secs() + 2.5));
                continue;
            }
            build.selected = tile.0;
            build.turns = 0;
            menu.open = false;
        }
    }
}

#[allow(clippy::type_complexity)]
fn draw(
    mut commands: Commands,
    mut menu: ResMut<BuildMenu>,
    build: Res<Building>,
    font: Option<Res<MenuFont>>,
    assets: Res<AssetServer>,
    mut root: Query<&mut Visibility, With<MenuRoot>>,
    mut grid: Query<(Entity, Option<&Children>, &mut ScrollPosition), With<TileGrid>>,
    mut tiles: Query<(&Tile, &mut WowButton), Without<Tab>>,
    mut tabs: Query<(&Tab, &mut WowButton), Without<Tile>>,
    mut title: Query<&mut Text, With<MenuTitle>>,
) {
    let Some(font) = font else {
        return;
    };
    let want = if menu.open && (build.on || crate::capture_keep::keeps("build")) {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut root {
        if *v != want {
            *v = want;
        }
    }
    if want == Visibility::Hidden {
        return;
    }
    if let Ok(mut text) = title.single_mut() {
        let want = skill_text(&build);
        if text.0 != want {
            text.0 = want;
        }
    }
    if menu.drawn != Some((menu.category, build.construction_version, build.style)) {
        let new_category = menu.drawn.map(|d| d.0) != Some(menu.category);
        menu.drawn = Some((menu.category, build.construction_version, build.style));
        if let Ok((grid, children, mut scroll)) = grid.single_mut() {
            if new_category {
                scroll.0 = Vec2::ZERO;
            }
            for c in children.into_iter().flatten() {
                commands.entity(*c).despawn();
            }
            commands.entity(grid).with_children(|g| {
                // The tiles wrap three to a row.
                g.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    flex_wrap: FlexWrap::Wrap,
                    flex_shrink: 0.0,
                    column_gap: u(6.0),
                    row_gap: u(6.0),
                    align_content: AlignContent::FlexStart,
                    ..default()
                })
                .with_children(|g| {
                    let mut any = false;
                    for i in in_category(menu.category) {
                        any = true;
                        tile(g, &font.0, &assets, &build, i);
                    }
                    if !any {
                        g.spawn(text(&font.0, "Nothing to build here yet.", 12.0, GREY));
                    }
                });
            });
        }
    }
    for (tile, mut button) in &mut tiles {
        let locked = locked_reason(&build, choice(tile.0)).is_some();
        let selected = tile.0 == build.selected;
        if button.disabled != locked || button.selected != selected {
            button.disabled = locked;
            button.selected = selected;
        }
    }
    for (tab, mut button) in &mut tabs {
        let selected = tab.0 == menu.category;
        if button.selected != selected {
            button.selected = selected;
        }
    }
}

/// One piece's tile: its name, its materials and the Construction it asks, as a trade skill
/// lists a recipe.
fn tile(
    g: &mut ChildSpawnerCommands,
    font: &Handle<Font>,
    assets: &AssetServer,
    build: &Building,
    i: usize,
) {
    let c = choice(i);
    let (cost, needs, color) = tile_lines(build, c);
    let locked = locked_reason(build, c).is_some();
    g.spawn((
        Button,
        WowButton::new(Kind::Row),
        Tile(i),
        choice_tip(build, c),
        Node {
            width: u(172.0),
            height: u(62.0),
            padding: UiRect::all(u(6.0)),
            flex_direction: FlexDirection::Column,
            justify_content: JustifyContent::SpaceBetween,
            border: UiRect::all(px(1)),
            ..default()
        },
        BorderColor::all(Color::srgba(0.5, 0.5, 0.5, 0.5)),
        BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.35)),
    ))
    .with_children(|t| {
        wow_ui::row_glow(t);
        t.spawn(Node {
            flex_direction: FlexDirection::Row,
            align_items: AlignItems::Center,
            column_gap: u(5.0),
            ..default()
        })
        .with_children(|head| {
            head.spawn(wow_ui::icon_frame_lit(
                Some(assets.load(preview_path(build, c))),
                22.0,
                wow_ui::SLOT_EDGE,
                !locked,
            ));
            head.spawn(text(
                font,
                &choice_name(build, c),
                12.0,
                if locked { LOCKED } else { GOLD },
            ));
        });
        t.spawn(text(font, &cost, 10.0, if locked { LOCKED } else { DIM }));
        if !needs.is_empty() {
            t.spawn(text(font, &needs, 10.0, color));
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_menu_builds_in_the_world_and_draws_its_tiles() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        world.insert_resource(FpsState {
            in_world: true,
            ..default()
        });
        world.init_resource::<BuildMenu>();
        world.insert_resource(Building {
            on: true,
            ..default()
        });
        world.run_system_once(spawn_menu).unwrap();
        world.resource_mut::<BuildMenu>().open = true;
        world.run_system_once(draw).unwrap();
        let mut tabs = world.query::<&Tab>();
        assert_eq!(tabs.iter(&world).count(), CATEGORIES.len());
        let mut tiles = world.query::<&Tile>();
        assert_eq!(
            tiles.iter(&world).count(),
            in_category(Category::default()).len()
        );
    }

    #[test]
    fn tiles_show_the_recipe_and_grey_what_is_out_of_reach() {
        let mut b = Building::default();
        b.recipes.insert(
            PieceType::WoodDoor.0,
            super::super::wire::Recipe {
                kind: PieceType::WoodDoor,
                required: 125,
                yellow: 150,
                green: 175,
                gray: 200,
                materials: vec![(91002, 6), (91021, 2)],
            },
        );
        b.tier = 2;
        b.skill = 100;
        let door = Choice::Piece(PieceType::WoodDoor);
        let (cost, needs, color) = tile_lines(&b, door);
        assert_eq!(cost, "6 Sturdy Lumber, 2 Bronze Hinge");
        assert_eq!(needs, "Requires Construction 125");
        assert_eq!(color, LOCKED);
        b.skill = 160;
        let (_, needs, color) = tile_lines(&b, door);
        assert_eq!(needs, "Construction 125");
        assert_eq!(color, skill_color(SkillColor::Yellow));
    }

    #[test]
    fn every_tile_has_a_tooltip_with_its_cost_and_what_a_click_does() {
        let b = Building::default();
        for i in 0..choices() {
            let tip = choice_tip(&b, choice(i));
            assert!(tip.0.len() >= 2);
            assert_eq!(tip.0[0].text, choice_name(&b, choice(i)));
            assert!(tip.0.last().unwrap().text.contains("Click"));
        }
    }

    #[test]
    fn doors_and_the_portal_say_what_they_do_and_salvage_is_named() {
        let mut b = Building::default();
        let portal = Choice::Piece(PieceType(super::super::pieces::PORTAL));
        let notes = piece_notes(portal);
        assert!(
            notes.iter().any(|n| n.contains("capitals"))
                && notes.iter().any(|n| n.contains("lock"))
        );
        assert!(piece_notes(Choice::Piece(PieceType::WoodDoor)).len() == 1);
        assert!(piece_notes(Choice::Piece(PieceType::WoodWall)).is_empty());
        // The portal is on the menu, among the objects.
        assert!((0..choices()).any(|i| matches!(choice(i), Choice::Piece(t) if t.is_portal())));
        assert_eq!(super::category_of(portal), Category::Objects);
        // With recipes from the server, every tooltip says only fresh materials teach.
        b.recipes.insert(
            PieceType::WoodWall.0,
            super::super::wire::Recipe {
                kind: PieceType::WoodWall,
                required: 1,
                yellow: 25,
                green: 50,
                gray: 75,
                materials: vec![(91001, 8)],
            },
        );
        b.tier = 1;
        b.skill = 1;
        let tip = choice_tip(&b, Choice::Piece(PieceType::WoodWall));
        assert!(tip.0.iter().any(|l| l.text.contains("salvaged")));
        assert!(tip.0.last().unwrap().text.contains("Click"));
    }

    #[test]
    fn every_choice_has_one_category() {
        let total: usize = CATEGORIES.iter().map(|c| in_category(*c).len()).sum();
        assert_eq!(total, choices());
        assert!(in_category(Category::Stone).iter().all(|&i| matches!(
            choice(i),
            Choice::Piece(t) if def(t).material == Material::Stone
        )));
        assert!(!in_category(Category::Blueprints).is_empty());
        // The sentry gun is a killstreak now: nowhere on the menu, in no blueprint.
        assert!((0..choices()).all(|i| match choice(i) {
            Choice::Piece(t) => t != PieceType::SentryGun,
            Choice::Blueprint(b) => b.parts.iter().all(|p| p.0 != PieceType::SentryGun),
        }));
        assert!(
            in_category(Category::Defence)
                .iter()
                .any(|&i| matches!(choice(i), Choice::Piece(PieceType::Sandbags)))
        );
    }
}
