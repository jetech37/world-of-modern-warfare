//! The Construction window (`Panel::Construction`): construction and the homestead in one classic
//! window, in place of gossip menus and chat lines. From the HUD (the micro menu's Construction
//! button or B) it shows the player's Construction, their plot (where, how big, what stands on it,
//! the upkeep) and, on its second tab, the build catalogue, from which a piece goes straight into
//! build mode. Opened by a homestead NPC (the server says so on the world channel) it also lists
//! what that NPC does as priced lines, as a gossip window lists its options: the Plot Steward,
//! the plot coordinator, claims, keeps and releases a plot and takes the player to it; the
//! courier travels; the trainer trains and makes parts. Every answer shows in the window's status
//! line, never in chat.

use benilla_app::fusion::{
    CHANNEL_WORLD, ChannelMessage, FusionChannelReceived, SendFusionChannel,
};
use bevy::prelude::*;

use super::menu::{self, BuildMenu, CATEGORIES, Category, in_category};
use super::pieces::{Choice, choice, def};
use super::wire::{self, HomeService, HomeWindow};
use super::{Building, choice_name, locked_reason, skill_text};
use crate::Mw2Set;
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::panels::{Access, OpenPanel, Panel};
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, Kind, RED, WHITE, WindowSpec, WowButton,
    text, u,
};

/// The window's pages, as tabs under it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum HomeTab {
    #[default]
    Homestead,
    Catalogue,
}

/// The window's state.
#[derive(Resource, Default)]
pub struct Homestead {
    pub open: bool,
    /// Who opened it: the HUD, or an NPC (whose buttons it then shows).
    pub access: Option<Access>,
    pub tab: HomeTab,
    /// The catalogue's category.
    pub category: Category,
    /// The window as the server last sent it.
    pub window: HomeWindow,
    /// Bumped on every change the page draws.
    pub version: u32,
    /// What the page was last drawn for: tab, version, Construction's version, category, style.
    drawn: Option<(HomeTab, u32, u32, Category, u8)>,
    /// A button that asks first, pressed once: (action, arg).
    pub armed: Option<(u8, u32)>,
    /// A note of the window's own (a locked piece, a press to confirm), over the server's.
    pub note: Option<(String, Color)>,
    /// The cursor flag was ours to set.
    held_cursor: bool,
}

#[derive(Component)]
struct HomeRoot;
#[derive(Component)]
struct HomeClose;
#[derive(Component)]
struct HomeHeader;
#[derive(Component)]
struct HomeList;
#[derive(Component)]
struct HomeCats;
/// The catalogue's style row (crate::build::menu::style_row), shown on its page.
#[derive(Component)]
struct HomeStyles;
#[derive(Component)]
struct HomeStatus;
#[derive(Component, Clone, Copy)]
struct HomeTabButton(HomeTab);
#[derive(Component, Clone, Copy)]
struct ServiceRow(usize);
#[derive(Component, Clone, Copy)]
struct CatalogueRow(usize);
#[derive(Component, Clone, Copy)]
struct CategoryButton(Category);
#[derive(Component)]
struct BuildButton;
#[derive(Component)]
struct CloseButton;

#[derive(Resource)]
struct HomeFont(Handle<Font>);

pub struct HomesteadPlugin;

impl Plugin for HomesteadPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Homestead>()
            .add_systems(
                Update,
                (spawn_window, receive, open_close, click, confirm_popup, draw)
                    .chain()
                    .in_set(Mw2Set::Hud),
            )
            // After every window's own systems: the cursor stays out while this one is open.
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

/// How tall the list may grow, in UI units, before it scrolls.
const LIST_HEIGHT: f32 = 250.0;
/// The window's width in UI units, and a list line's.
const WIDTH: f32 = 440.0;
/// (inside the list's inset, its scroll bar beside).
const ROW_WIDTH: f32 = 372.0;

fn spawn_window(
    mut commands: Commands,
    assets: Res<AssetServer>,
    state: Res<FpsState>,
    mut built: Local<bool>,
    keymap: Res<crate::keybinds::Keymap>,
) {
    if *built || !state.in_world {
        return;
    }
    *built = true;
    let build_key = keymap.label(crate::keybinds::Action::Build);
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: "construction",
        title: "Construction".into(),
        frame: Frame::Dialog,
        width: Some(WIDTH),
        movable: true,
        // Where the stock UIParent sets its left-hand panels.
        anchor: Anchor {
            at: Vec2::ZERO,
            pivot: Vec2::ZERO,
            offset: Vec2::new(16.0, 104.0),
        },
        z: i32::MAX - 3,
    };
    wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        (
            HomeRoot,
            Visibility::Hidden,
            wow_ui::WindowSounds::CHARACTER,
        ),
        Some(HomeClose),
        |root| {
            root.spawn((
                HomeHeader,
                Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: u(2.0),
                    margin: UiRect::bottom(u(6.0)),
                    ..default()
                },
            ));
            root.spawn((
                HomeCats,
                Node {
                    flex_direction: FlexDirection::Row,
                    flex_wrap: FlexWrap::Wrap,
                    margin: UiRect::bottom(u(4.0)),
                    ..default()
                },
            ));
            root.spawn((
                HomeStyles,
                Node {
                    flex_direction: FlexDirection::Column,
                    display: Display::None,
                    ..default()
                },
            ))
            .with_children(|c| menu::style_row(c, f));
            wow_ui::inset(
                root,
                Node {
                    flex_direction: FlexDirection::Column,
                    ..default()
                },
                (),
                |c| {
                    wow_ui::scroll_frame(c, LIST_HEIGHT, None, HomeList, |_| {});
                },
            );
            root.spawn((
                Node {
                    min_height: u(28.0),
                    margin: UiRect::top(u(6.0)),
                    ..default()
                },
                children![(HomeStatus, text(f, "", 11.0, WHITE))],
            ));
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                justify_content: JustifyContent::FlexEnd,
                column_gap: u(4.0),
                ..default()
            })
            .with_children(|row| {
                row.spawn(wow_ui::button(
                    f,
                    "Build",
                    100.0,
                    (
                        BuildButton,
                        wow_ui::WowTip::new(
                            "Build",
                            &format!(
                                "Takes the hammer out: build mode, with its own menu of pieces. \
                                 {build_key} opens this window again for another piece; Esc \
                                 leaves build mode."
                            ),
                        ),
                    ),
                ));
                row.spawn(wow_ui::button(f, "Close", 80.0, CloseButton));
            });
        },
        |below| {
            wow_ui::tab_strip(below, |tabs| {
                tabs.spawn(wow_ui::tab(
                    f,
                    "Homestead",
                    HomeTabButton(HomeTab::Homestead),
                ));
                tabs.spawn(wow_ui::tab(
                    f,
                    "Catalog",
                    HomeTabButton(HomeTab::Catalogue),
                ));
            });
        },
    );
    commands.insert_resource(HomeFont(font));
}

/// The server's window and its word to open it.
fn receive(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut home: ResMut<Homestead>,
    mut open: MessageWriter<OpenPanel>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD || m.body.first() != Some(&wire::OP_HOMESTEAD_WINDOW) {
            continue;
        }
        let Some(w) = wire::decode_home_window(&m.body) else {
            warn!("benilla-mw2: a Construction window that does not decode");
            continue;
        };
        match w.open {
            wire::HOME_OPEN_NPC => {
                open.write(OpenPanel {
                    panel: Panel::Construction,
                    access: Access::Npc,
                });
            }
            wire::HOME_OPEN_HUD if !home.open => {
                home.open = true;
                home.access = Some(Access::Hud);
            }
            _ => {}
        }
        home.window = w;
        home.armed = None;
        home.note = None;
        home.version = home.version.wrapping_add(1);
    }
}

/// `OpenPanel{Construction}`: from the HUD it opens (asking the server for the window) or closes;
/// from an NPC it opens with the NPC's buttons. Esc and the close buttons close it.
fn open_close(
    input: Res<Mw2Input>,
    state: Res<FpsState>,
    build: Res<Building>,
    mut home: ResMut<Homestead>,
    mut panels: MessageReader<OpenPanel>,
    mut send: MessageWriter<SendFusionChannel>,
    close: Query<&Interaction, (With<HomeClose>, Changed<Interaction>)>,
) {
    for p in panels.read().filter(|p| p.panel == Panel::Construction) {
        match p.access {
            Access::Hud if home.open => home.open = false,
            Access::Hud => {
                home.open = true;
                home.access = Some(Access::Hud);
                // In build mode B is for another piece: the catalogue first.
                home.tab = if build.on {
                    HomeTab::Catalogue
                } else {
                    HomeTab::Homestead
                };
                // The window as it stands, with no NPC's buttons.
                home.window.services.clear();
                home.window.npc = wire::NPC_NONE;
                send.write(SendFusionChannel(ChannelMessage {
                    channel: CHANNEL_WORLD,
                    body: wire::encode_home_ask(),
                }));
            }
            Access::Npc => {
                home.open = true;
                home.access = Some(Access::Npc);
                home.tab = HomeTab::Homestead;
            }
        }
        home.armed = None;
        home.note = None;
        home.version = home.version.wrapping_add(1);
    }
    let closed = close.iter().any(|i| *i == Interaction::Pressed);
    // Esc answers an open question first (No), as the stock popups take it before the windows.
    if input.escape && home.armed.is_some() {
        home.armed = None;
    } else if home.open && (input.escape || closed || !state.in_world) {
        home.open = false;
    }
    if !home.open {
        home.access = None;
        home.armed = None;
    }
}

/// Building needs the combat layer, which is up whenever a character is in the world: the note
/// for the moment before it is.
pub fn layer_off() -> String {
    "Building opens once you are in the world.".into()
}

#[allow(clippy::too_many_arguments)]
fn click(
    state: Res<FpsState>,
    mut home: ResMut<Homestead>,
    mut build: ResMut<Building>,
    mut menu: ResMut<BuildMenu>,
    mut send: MessageWriter<SendFusionChannel>,
    tabs: Query<(&Interaction, &HomeTabButton), Changed<Interaction>>,
    services: Query<(&Interaction, &ServiceRow), Changed<Interaction>>,
    rows: Query<(&Interaction, &CatalogueRow), Changed<Interaction>>,
    cats: Query<(&Interaction, &CategoryButton), Changed<Interaction>>,
    builds: Query<&Interaction, (With<BuildButton>, Changed<Interaction>)>,
    closes: Query<&Interaction, (With<CloseButton>, Changed<Interaction>)>,
) {
    if !home.open {
        return;
    }
    let layer_on = state.active;
    let pressed = |i: &Interaction| *i == Interaction::Pressed;
    for (i, tab) in &tabs {
        if pressed(i) && home.tab != tab.0 {
            home.tab = tab.0;
            home.armed = None;
        }
    }
    for (i, cat) in &cats {
        if pressed(i) {
            home.category = cat.0;
        }
    }
    if closes.iter().any(pressed) {
        home.open = false;
        return;
    }
    if builds.iter().any(pressed) {
        // Into build mode with the build menu up; with the layer off there is no build
        // mode to go into, so say how to get it.
        if !layer_on {
            home.note = Some((layer_off(), RED));
            home.version = home.version.wrapping_add(1);
            return;
        }
        build.on = true;
        home.open = false;
        return;
    }
    for (i, row) in &services {
        if !pressed(i) {
            continue;
        }
        let Some(s) = home.window.services.get(row.0).cloned() else {
            continue;
        };
        match press(&s, home.armed) {
            Press::Locked => {}
            Press::Arm => {
                // The popup asks (confirm_popup).
                home.armed = Some((s.action, s.arg));
            }
            Press::Send => {
                home.armed = None;
                home.note = None;
                send.write(SendFusionChannel(ChannelMessage {
                    channel: CHANNEL_WORLD,
                    body: wire::encode_home_act(s.action, s.arg),
                }));
            }
        }
    }
    for (i, row) in &rows {
        if !pressed(i) {
            continue;
        }
        // Out of the player's Construction: say why, and stay.
        if let Some(why) = locked_reason(&build, choice(row.0)) {
            home.note = Some((why, RED));
            home.version = home.version.wrapping_add(1);
            continue;
        }
        if !layer_on {
            home.note = Some((layer_off(), RED));
            home.version = home.version.wrapping_add(1);
            continue;
        }
        // The piece in hand in build mode, the menu kept down: the ghost is ready to place.
        build.selected = row.0;
        build.turns = 0;
        build.on = true;
        menu.skip_open = true;
        home.open = false;
    }
}

/// The popup asking before a service that asks first.
#[derive(Component)]
struct ConfirmPopup;

/// The popup's Yes (true) and No.
#[derive(Component, Clone, Copy)]
struct ConfirmAnswer(bool);

/// A service that asks first ([`Homestead::armed`]) as a popup in `StaticPopup`'s look: Yes sends
/// it, No (or Esc) lets it go.
#[allow(clippy::type_complexity)]
fn confirm_popup(
    mut commands: Commands,
    assets: Res<AssetServer>,
    mut home: ResMut<Homestead>,
    mut send: MessageWriter<SendFusionChannel>,
    answers: Query<(&Interaction, &ConfirmAnswer), Changed<Interaction>>,
    roots: Query<Entity, With<ConfirmPopup>>,
    mut asked: Local<Option<(u8, u32)>>,
) {
    for (i, a) in &answers {
        if *i != Interaction::Pressed {
            continue;
        }
        if let (true, Some((action, arg))) = (a.0, home.armed) {
            home.note = None;
            send.write(SendFusionChannel(ChannelMessage {
                channel: CHANNEL_WORLD,
                body: wire::encode_home_act(action, arg),
            }));
        }
        home.armed = None;
    }
    let want = home.armed.filter(|_| home.open);
    if want == *asked {
        return;
    }
    *asked = want;
    for e in &roots {
        commands.entity(e).despawn();
    }
    let Some((action, arg)) = want else {
        return;
    };
    let label = home
        .window
        .services
        .iter()
        .find(|s| (s.action, s.arg) == (action, arg))
        .map_or_else(String::new, |s| s.label.clone());
    let font: Handle<Font> = assets.load(FONT);
    wow_ui::spawn_popup(
        &mut commands,
        &font,
        "homestead confirm",
        &confirm_question(&label),
        ConfirmPopup,
        ("Yes", ConfirmAnswer(true)),
        ("No", ConfirmAnswer(false)),
    );
}

/// What the popup asks before a service.
pub fn confirm_question(label: &str) -> String {
    let label = label.trim_end_matches(['.', '?', '!']);
    if label.is_empty() {
        "Are you sure?".into()
    } else {
        format!("{label}?
Are you sure?")
    }
}

/// What a press on an NPC's line does.
#[derive(Debug, PartialEq, Eq)]
pub enum Press {
    /// Greyed: nothing.
    Locked,
    /// It asks first: armed, the next press sends it.
    Arm,
    Send,
}

pub fn press(s: &HomeService, armed: Option<(u8, u32)>) -> Press {
    if !s.enabled {
        Press::Locked
    } else if s.confirm && armed != Some((s.action, s.arg)) {
        Press::Arm
    } else {
        Press::Send
    }
}

/// Under a Plot Steward's lines for a plot's holder with no one targeted: how to let a friend in.
pub fn steward_hint(w: &HomeWindow) -> Option<&'static str> {
    let permits_shown = w.services.iter().any(|s| s.action == wire::ACT_PERMIT);
    (w.npc == wire::NPC_STEWARD && w.plot.id != 0 && !permits_shown).then_some(
        "To let a friend in or build with you, target them, then speak to the Plot Steward again.",
    )
}

/// The plot's lines for the Homestead page: what and where, the upkeep, what stands on it.
pub fn plot_lines(w: &HomeWindow, now_unix: u64) -> Vec<(String, Color)> {
    let p = &w.plot;
    let mut out = Vec::new();
    if p.id == 0 {
        out.push(("You hold no homestead.".into(), WHITE));
        out.push((
            "A Plot Steward at a homestead gate grants one plot to a player; a Homestead \
             Courier in each capital takes you there."
                .into(),
            DIM,
        ));
    } else {
        let size = match p.size {
            1 => "Small homestead",
            3 => "Large homestead",
            _ => "Homestead",
        };
        let side = (p.bounds[2] - p.bounds[0]).round();
        out.push((
            format!("{size} {} {}", p.id, p.place)
                .trim_end()
                .to_string(),
            WHITE,
        ));
        out.push((
            format!(
                "{side:.0} yards a side  ·  x {:.0} to {:.0}, y {:.0} to {:.0}",
                p.bounds[0], p.bounds[2], p.bounds[1], p.bounds[3]
            ),
            DIM,
        ));
        let (upkeep, color) = match p.standing {
            wire::PLOT_PAID => {
                let days = u64::from(p.paid_until).saturating_sub(now_unix) / 86_400;
                (
                    format!(
                        "Upkeep paid {days} more {}  ·  {} a week",
                        if days == 1 { "day" } else { "days" },
                        money_text(p.upkeep)
                    ),
                    GREEN,
                )
            }
            wire::PLOT_OVERDUE => (
                "Upkeep overdue: nothing new may go up. Pay a Plot Steward.".into(),
                RED,
            ),
            _ => ("Lapsed: it goes back on sale.".into(), RED),
        };
        out.push((upkeep, color));
        out.push((
            format!(
                "{} {} built  ·  {} {}",
                p.pieces,
                if p.pieces == 1 { "piece" } else { "pieces" },
                p.permits,
                if p.permits == 1 { "permit" } else { "permits" }
            ),
            DIM,
        ));
    }
    if w.reclaim > 0 {
        out.push((
            format!(
                "{} {} packed for you {} at a Plot Steward.",
                w.reclaim,
                if w.reclaim == 1 { "item" } else { "items" },
                if w.reclaim == 1 { "waits" } else { "wait" }
            ),
            GOLD,
        ));
    }
    out
}

/// Copper as text, for a line that names a price.
pub fn money_text(copper: u32) -> String {
    let (g, s, c) = wire::coins(copper);
    let mut parts = Vec::new();
    if g > 0 {
        parts.push(format!("{g}g"));
    }
    if s > 0 {
        parts.push(format!("{s}s"));
    }
    if c > 0 || parts.is_empty() {
        parts.push(format!("{c}c"));
    }
    parts.join(" ")
}

/// The status line: the window's own note, else the server's last word in its tone.
pub fn status_line(home: &Homestead) -> (String, Color) {
    if let Some((s, c)) = &home.note {
        return (s.clone(), *c);
    }
    let w = &home.window;
    let color = match w.tone {
        wire::TONE_DONE => GREEN,
        wire::TONE_ERROR => RED,
        _ => WHITE,
    };
    (w.message.clone(), color)
}

/// A line's colour: a recipe's, as WoW draws a trade skill's; grey when it can't be pressed.
fn hue_color(s: &HomeService) -> Color {
    if !s.enabled {
        return GREY;
    }
    match s.hue {
        1 => menu::skill_color(wire::SkillColor::Orange),
        2 => menu::skill_color(wire::SkillColor::Yellow),
        3 => menu::skill_color(wire::SkillColor::Green),
        4 => menu::skill_color(wire::SkillColor::Gray),
        5 => menu::skill_color(wire::SkillColor::Red),
        _ => GOLD,
    }
}

/// What an NPC is to the list's heading.
fn npc_title(w: &HomeWindow) -> String {
    let role = match w.npc {
        wire::NPC_STEWARD => "Plot Steward",
        wire::NPC_COURIER => "Homestead Courier",
        wire::NPC_TRAINER => "Construction Trainer",
        _ => return String::new(),
    };
    if w.npc_name.is_empty() {
        role.into()
    } else {
        format!("{}, {role}", w.npc_name)
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn draw(
    mut commands: Commands,
    mut home: ResMut<Homestead>,
    build: Res<Building>,
    font: Option<Res<HomeFont>>,
    assets: Res<AssetServer>,
    mut root: Query<&mut Visibility, With<HomeRoot>>,
    header: Query<(Entity, Option<&Children>), With<HomeHeader>>,
    cats: Query<(Entity, Option<&Children>), With<HomeCats>>,
    mut list: Query<(Entity, Option<&Children>, &mut ScrollPosition), With<HomeList>>,
    mut styles: Query<&mut Node, With<HomeStyles>>,
    mut status: Query<(&mut Text, &mut TextColor), With<HomeStatus>>,
    mut tabs: Query<(&HomeTabButton, &mut WowButton), Without<CategoryButton>>,
    mut cat_buttons: Query<(&CategoryButton, &mut WowButton), Without<HomeTabButton>>,
    keymap: Res<crate::keybinds::Keymap>,
) {
    let Some(font) = font else {
        return;
    };
    let want = if home.open {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut root {
        if *v != want {
            *v = want;
        }
    }
    if !home.open {
        home.drawn = None;
        return;
    }
    let f = &font.0;
    let key = (
        home.tab,
        home.version,
        build.construction_version,
        home.category,
        build.style,
    );
    // The styles show on the catalogue's page, over its pieces.
    let want_styles = if home.tab == HomeTab::Catalogue {
        Display::Flex
    } else {
        Display::None
    };
    for mut n in &mut styles {
        if n.display != want_styles {
            n.display = want_styles;
        }
    }
    if home.drawn != Some(key) {
        let new_page = home.drawn.map(|d| (d.0, d.3)) != Some((home.tab, home.category));
        home.drawn = Some(key);
        if let Ok((e, children)) = header.single() {
            for c in children.into_iter().flatten() {
                commands.entity(*c).despawn();
            }
            commands
                .entity(e)
                .with_children(|h| draw_header(h, f, &home, &build, &keymap));
        }
        if let Ok((e, children)) = cats.single() {
            for c in children.into_iter().flatten() {
                commands.entity(*c).despawn();
            }
            if home.tab == HomeTab::Catalogue {
                commands.entity(e).with_children(|row| {
                    for cat in CATEGORIES {
                        let mut state = WowButton::new(Kind::Panel);
                        state.selected = cat == home.category;
                        row.spawn(wow_ui::button_fit(
                            f,
                            cat.title(),
                            state,
                            CategoryButton(cat),
                        ));
                    }
                });
            }
        }
        if let Ok((e, children, mut scroll)) = list.single_mut() {
            if new_page {
                scroll.0 = Vec2::ZERO;
            }
            for c in children.into_iter().flatten() {
                commands.entity(*c).despawn();
            }
            commands.entity(e).with_children(|l| match home.tab {
                HomeTab::Homestead => draw_services(l, f, &assets, &home),
                HomeTab::Catalogue => draw_catalogue(l, f, &assets, &build, home.category),
            });
        }
    }
    if let Ok((mut t, mut c)) = status.single_mut() {
        let (s, color) = status_line(&home);
        if t.0 != s {
            t.0 = s;
        }
        if c.0 != color {
            c.0 = color;
        }
    }
    for (tab, mut b) in &mut tabs {
        let selected = tab.0 == home.tab;
        if b.selected != selected {
            b.selected = selected;
        }
    }
    for (cat, mut b) in &mut cat_buttons {
        let selected = cat.0 == home.category;
        if b.selected != selected {
            b.selected = selected;
        }
    }
}

/// The page's head: the player's Construction with its rank bar, then (on the Homestead page)
/// their plot.
fn draw_header(
    h: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    home: &Homestead,
    build: &Building,
    keymap: &crate::keybinds::Keymap,
) {
    let skill = skill_text(build);
    let skill = if skill.is_empty() {
        "Construction".to_string()
    } else {
        skill
    };
    h.spawn(text(f, &skill, 12.0, GOLD));
    // The trade skill window's rank bar.
    let fill = if build.cap > 0 {
        f32::from(build.skill) / f32::from(build.cap)
    } else {
        0.0
    };
    h.spawn((
        Node {
            width: u(ROW_WIDTH),
            height: u(10.0),
            border: UiRect::all(px(1)),
            margin: UiRect::vertical(u(2.0)),
            ..default()
        },
        BorderColor::all(Color::srgba(0.6, 0.6, 0.6, 0.8)),
        BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.5)),
        children![(
            Node {
                width: percent(fill.clamp(0.0, 1.0) * 100.0),
                height: percent(100),
                ..default()
            },
            BackgroundColor(Color::srgba(0.0, 0.0, 0.75, 0.8)),
        )],
    ));
    if home.tab != HomeTab::Homestead {
        h.spawn(text(
            f,
            &format!(
                "Pick a style, then click a piece to take it in hand: build mode (each takes a kit \
                 made at Construction's trade window; {} opens Professions). Build Structure on \
                 Utility's bar enters build mode too; Esc leaves it.",
                keymap.label(crate::keybinds::Action::Build)
            ),
            10.0,
            DIM,
        ));
        return;
    }
    wow_ui::section_header(h, f, "Your Homestead");
    for (s, c) in plot_lines(&home.window, super::now_unix()) {
        h.spawn(text(f, &s, 11.0, c));
    }
}

/// The NPC's lines, as a gossip window lists its options; from the HUD, where to find them.
fn draw_services(
    l: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    home: &Homestead,
) {
    let w = &home.window;
    let npc = home.access == Some(Access::Npc) && w.npc != wire::NPC_NONE;
    if !npc {
        for line in [
            "Plot Stewards stand at the homestead gates: on the Hyjal summit, in Azshara Crater \
             and on the Thalassian plateau. They claim, keep and release your plot and take you \
             to it.",
            "Homestead Couriers in each capital take you to the gates; Construction Trainers \
             teach the trade and make its parts.",
        ] {
            l.spawn((
                Node {
                    width: u(ROW_WIDTH),
                    margin: UiRect::bottom(u(6.0)),
                    ..default()
                },
                children![text(f, line, 11.0, DIM)],
            ));
        }
        return;
    }
    wow_ui::section_header(l, f, &npc_title(w));
    if w.services.is_empty() {
        l.spawn(text(f, "Nothing more for you here.", 11.0, DIM));
    }
    for (i, s) in w.services.iter().enumerate() {
        service_row(l, f, assets, i, s, home.armed == Some((s.action, s.arg)));
    }
    if let Some(hint) = steward_hint(w) {
        l.spawn((
            Node {
                width: u(ROW_WIDTH),
                margin: UiRect::top(u(6.0)),
                ..default()
            },
            children![text(f, hint, 10.0, DIM)],
        ));
    }
}

/// One of the NPC's lines: its name in its colour, its note under it, its price at the right in
/// coins.
fn service_row(
    l: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    i: usize,
    s: &HomeService,
    armed: bool,
) {
    let mut state = WowButton::new(Kind::Row);
    state.disabled = !s.enabled;
    state.selected = armed;
    l.spawn((
        Button,
        state,
        ServiceRow(i),
        Node {
            width: u(ROW_WIDTH),
            min_height: u(32.0),
            padding: UiRect::axes(u(6.0), u(3.0)),
            flex_direction: FlexDirection::Row,
            justify_content: JustifyContent::SpaceBetween,
            align_items: AlignItems::Center,
            margin: UiRect::bottom(u(2.0)),
            flex_shrink: 0.0,
            ..default()
        },
    ))
    .with_children(|r| {
        wow_ui::row_glow(r);
        r.spawn(Node {
            flex_direction: FlexDirection::Column,
            flex_shrink: 1.0,
            max_width: u(ROW_WIDTH - 90.0),
            ..default()
        })
        .with_children(|c| {
            c.spawn(text(f, &s.label, 12.0, hue_color(s)));
            if !s.note.is_empty() {
                c.spawn(text(f, &s.note, 10.0, if s.enabled { DIM } else { GREY }));
            }
        });
        if s.cost > 0 {
            money(r, f, assets, s.cost);
        }
    });
}

/// A price as the money frame shows it: each coin's count before its icon.
fn money(r: &mut ChildSpawnerCommands, f: &Handle<Font>, assets: &AssetServer, copper: u32) {
    let (g, s, c) = wire::coins(copper);
    r.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        column_gap: u(2.0),
        flex_shrink: 0.0,
        ..default()
    })
    .with_children(|m| {
        for (n, icon) in [(g, "Gold"), (s, "Silver"), (c, "Copper")] {
            if n == 0 && !(icon == "Copper" && g == 0 && s == 0) {
                continue;
            }
            m.spawn(text(f, &n.to_string(), 11.0, WHITE));
            m.spawn((
                ImageNode::new(
                    assets.load(format!("mpq://Interface/MoneyFrame/UI-{icon}Icon.blp")),
                ),
                Node {
                    width: u(13.0),
                    height: u(13.0),
                    margin: UiRect::right(u(3.0)),
                    ..default()
                },
                bevy::picking::Pickable::IGNORE,
            ));
        }
    });
}

/// The plots' sides (yards) and a builder's margin in from their edges (Plots.h).
const PLOT_SIDES: [(f32, &str); 3] = [
    (24.0, "a small homestead"),
    (40.0, "a homestead"),
    (64.0, "a large homestead"),
];
const PLOT_MARGIN: f32 = 1.0;

/// How much ground a structure or a big object takes and the least plot it goes on: "33 × 37
/// yd · a homestead or larger". None for the small pieces.
pub fn footprint_text(c: Choice) -> Option<String> {
    let Choice::Piece(t) = c else {
        return None;
    };
    if !t.is_prop() {
        return None;
    }
    let (mut lo, mut hi) = ([f32::MAX; 2], [f32::MIN; 2]);
    for b in def(t).boxes {
        for k in 0..2 {
            lo[k] = lo[k].min(b.center[k] - b.half[k]);
            hi[k] = hi[k].max(b.center[k] + b.half[k]);
        }
    }
    let (w, d) = (hi[0] - lo[0], hi[1] - lo[1]);
    if w.max(d) < 8.0 {
        return None;
    }
    let need = w.max(d) + 2.0 * PLOT_MARGIN;
    let fits = PLOT_SIDES.iter().find(|(side, _)| need <= *side).map_or(
        "too big for a plot: the open world only".to_string(),
        |(side, name)| {
            if *side >= 64.0 {
                format!("{name} only")
            } else {
                format!("{name} or larger")
            }
        },
    );
    Some(format!("{:.0} × {:.0} yd  ·  {fits}", w.round(), d.round()))
}

/// The build catalogue: the category's pieces as a trade skill lists its recipes.
fn draw_catalogue(
    l: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    build: &Building,
    cat: Category,
) {
    for i in in_category(cat) {
        let c = choice(i);
        let (cost, needs, color) = menu::tile_lines(build, c);
        let locked = locked_reason(build, c).is_some();
        let mut state = WowButton::new(Kind::Row);
        state.disabled = locked;
        l.spawn((
            Button,
            state,
            CatalogueRow(i),
            menu::choice_tip(build, c),
            Node {
                width: u(ROW_WIDTH),
                min_height: u(30.0),
                padding: UiRect::axes(u(6.0), u(2.0)),
                flex_direction: FlexDirection::Row,
                justify_content: JustifyContent::SpaceBetween,
                align_items: AlignItems::Center,
                margin: UiRect::bottom(u(1.0)),
                flex_shrink: 0.0,
                ..default()
            },
        ))
        .with_children(|r| {
            wow_ui::row_glow(r);
            r.spawn(Node {
                flex_direction: FlexDirection::Row,
                align_items: AlignItems::Center,
                column_gap: u(6.0),
                max_width: u(ROW_WIDTH - 110.0),
                ..default()
            })
            .with_children(|left| {
                // A look at the piece (its face in the style, or its thumbnail), greyed while
                // it is out of reach.
                left.spawn(wow_ui::icon_frame_lit(
                    Some(assets.load(menu::preview_path(build, c))),
                    32.0,
                    wow_ui::SLOT_EDGE,
                    !locked,
                ));
                left.spawn(Node {
                    flex_direction: FlexDirection::Column,
                    min_width: u(0.0),
                    ..default()
                })
                .with_children(|col| {
                    col.spawn(text(
                        f,
                        &choice_name(build, c),
                        12.0,
                        if locked { GREY } else { GOLD },
                    ));
                    col.spawn(text(f, &cost, 10.0, if locked { GREY } else { DIM }));
                    if let Some(size) = footprint_text(c) {
                        col.spawn(text(f, &size, 10.0, GREY));
                    }
                });
            });
            if !needs.is_empty() {
                r.spawn(text(f, &needs, 10.0, color));
            }
        });
    }
}

/// While the window is open the cursor is out and the gun down (the flag Create-a-Class and the
/// achievements share, set after them so it holds).
fn hold_cursor(mut home: ResMut<Homestead>, mut state: ResMut<FpsState>) {
    if home.open {
        if !state.panel {
            state.panel = true;
        }
        home.held_cursor = true;
    } else if home.held_cursor {
        home.held_cursor = false;
        state.panel = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn service(enabled: bool, confirm: bool) -> HomeService {
        HomeService {
            action: 3,
            arg: 0,
            cost: 0,
            enabled,
            confirm,
            hue: 0,
            label: "Release my homestead".into(),
            note: String::new(),
        }
    }

    #[test]
    fn a_line_that_asks_first_asks_in_a_popup() {
        assert_eq!(
            confirm_question("Release my homestead"),
            "Release my homestead?
Are you sure?"
        );
        assert_eq!(confirm_question("Abandon it."), "Abandon it?
Are you sure?");
        assert_eq!(confirm_question(""), "Are you sure?");
    }

    #[test]
    fn a_line_that_asks_first_takes_two_presses() {
        assert_eq!(press(&service(false, false), None), Press::Locked);
        assert_eq!(press(&service(true, false), None), Press::Send);
        assert_eq!(press(&service(true, true), None), Press::Arm);
        assert_eq!(press(&service(true, true), Some((3, 0))), Press::Send);
        // Armed on another line: this one asks again.
        assert_eq!(press(&service(true, true), Some((5, 1))), Press::Arm);
    }

    #[test]
    fn the_steward_says_how_to_permit_a_friend() {
        let mut w = HomeWindow::default();
        w.npc = wire::NPC_STEWARD;
        assert!(steward_hint(&w).is_none()); // no plot: nothing to permit
        w.plot.id = 5;
        assert!(steward_hint(&w).unwrap().contains("target them"));
        let mut s = service(true, false);
        s.action = wire::ACT_PERMIT;
        w.services.push(s);
        assert!(steward_hint(&w).is_none()); // the target's lines are there
        w.services.clear();
        w.npc = wire::NPC_COURIER;
        assert!(steward_hint(&w).is_none());
    }

    #[test]
    fn the_homestead_page_reads_plainly() {
        let mut w = HomeWindow::default();
        let lines = plot_lines(&w, 0);
        assert_eq!(lines[0].0, "You hold no homestead.");
        w.plot = wire::HomePlot {
            id: 301,
            size: 1,
            standing: wire::PLOT_PAID,
            map: 37,
            bounds: [150.0, 0.0, 174.0, 24.0],
            paid_until: 1_000_000 + 5 * 86_400 + 10,
            upkeep: 2000,
            permits: 1,
            pieces: 41,
            place: "in Azshara Crater".into(),
        };
        w.reclaim = 3;
        let lines = plot_lines(&w, 1_000_000);
        assert_eq!(lines[0].0, "Small homestead 301 in Azshara Crater");
        assert_eq!(lines[1].0, "24 yards a side  ·  x 150 to 174, y 0 to 24");
        assert_eq!(lines[2].0, "Upkeep paid 5 more days  ·  20s a week");
        assert_eq!(lines[3].0, "41 pieces built  ·  1 permit");
        assert_eq!(lines[4].0, "3 items packed for you wait at a Plot Steward.");
        w.plot.standing = wire::PLOT_OVERDUE;
        assert_eq!(plot_lines(&w, 1_000_000)[2].1, RED);
        assert_eq!(money_text(12_345), "1g 23s 45c");
        assert_eq!(money_text(0), "0c");
    }

    #[test]
    fn the_status_line_says_the_servers_word_in_its_tone() {
        let mut home = Homestead::default();
        home.window.tone = wire::TONE_ERROR;
        home.window.message = "You already hold a homestead.".into();
        assert_eq!(status_line(&home), (home.window.message.clone(), RED));
        home.window.tone = wire::TONE_DONE;
        assert_eq!(status_line(&home).1, GREEN);
        home.note = Some(("Requires Construction 125".into(), RED));
        assert_eq!(status_line(&home).0, "Requires Construction 125");
        home.window.npc = wire::NPC_STEWARD;
        home.window.npc_name = "Warden Oakhallow".into();
        assert_eq!(npc_title(&home.window), "Warden Oakhallow, Plot Steward");
    }

    #[test]
    fn the_window_builds_and_draws_both_pages() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        world.insert_resource(FpsState {
            in_world: true,
            ..default()
        });
        world.init_resource::<Homestead>();
        world.init_resource::<Building>();
        world.run_system_once(spawn_window).unwrap();
        {
            let mut home = world.resource_mut::<Homestead>();
            home.open = true;
            home.access = Some(Access::Npc);
            home.window.npc = wire::NPC_STEWARD;
            home.window.services = vec![service(true, true), service(false, false)];
        }
        world.run_system_once(draw).unwrap();
        let mut rows = world.query::<&ServiceRow>();
        assert_eq!(rows.iter(&world).count(), 2);
        world.resource_mut::<Homestead>().tab = HomeTab::Catalogue;
        world.run_system_once(draw).unwrap();
        let mut rows = world.query::<&CatalogueRow>();
        assert_eq!(
            rows.iter(&world).count(),
            in_category(Category::default()).len()
        );
        let mut cats = world.query::<&CategoryButton>();
        assert_eq!(cats.iter(&world).count(), CATEGORIES.len());
    }
}
