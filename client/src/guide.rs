//! The new-player guide: a welcome window in the classic look, its pages listed down the left
//! (the words live in `assets/guide.md`, one easy file to edit). It opens by itself the first
//! time each character enters the world, unless the player ticked "Don't show at login"; `/help`,
//! `/womw` and `/guide`, and the Escape menu's **Guide** rung, open it any time.
//!
//! What is remembered rides two CVars, saved with the others: `womwGuideSeen` (the characters,
//! `Name@Realm`, that have had it) and `womwGuideAuto` (0 once "Don't show at login" is ticked).
//!
//! Lua to Rust as crate::micro_menu does it: the slash command, the menu rung and the first-login
//! check leave a request in `BenillaMw2Guide.open`, which [`drive`] takes each frame.

use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::mode::FpsState;
use crate::wow_ui::{self, Anchor, DIM, FONT, Frame, GOLD, WHITE, WindowSpec, WowButton, text, u};

/// The guide's text: `# ` a page, `## ` a heading, `- ` a bullet, `//` a note for editors.
pub const GUIDE: &str = include_str!("../assets/guide.md");

/// A piece of a page.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Block {
    Heading(String),
    Bullet(String),
    Para(String),
}

/// A page: its name in the list and what it says.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Page {
    pub title: String,
    pub blocks: Vec<Block>,
}

/// Read the guide's text into pages.
pub fn parse(text: &str) -> Vec<Page> {
    let mut pages: Vec<Page> = Vec::new();
    for line in text.lines() {
        let line = line.trim_end();
        let trimmed = line.trim_start();
        if trimmed.is_empty() || trimmed.starts_with("//") {
            continue;
        }
        if let Some(title) = line.strip_prefix("# ") {
            pages.push(Page {
                title: title.trim().to_string(),
                blocks: Vec::new(),
            });
            continue;
        }
        let Some(page) = pages.last_mut() else {
            continue;
        };
        let block = if let Some(h) = line.strip_prefix("## ") {
            Block::Heading(h.trim().to_string())
        } else if let Some(b) = trimmed.strip_prefix("- ") {
            Block::Bullet(b.trim().to_string())
        } else {
            Block::Para(trimmed.to_string())
        };
        page.blocks.push(block);
    }
    pages
}

/// Installs the slash commands, the Escape menu rung and the CVars once per world VM, and
/// checks once whether this character has had the guide. `BenillaMw2Guide.open` is the request.
pub const INSTALL_LUA: &str = r#"
if BenillaMw2Guide then return end
BenillaMw2Guide = { open = nil, checked = nil, errors = {} }
local G = BenillaMw2Guide
local function step(f)
  local ok, err = pcall(f)
  if not ok then table.insert(G.errors, tostring(err)) end
end
step(function()
  RegisterCVar("womwGuideAuto", "1")
  RegisterCVar("womwGuideSeen", "")
end)
-- /womw and /guide; /help keeps the stock command list and opens the guide beside it.
step(function()
  if SlashCmdList then
    SLASH_BENILLAMW2GUIDE1 = "/womw"
    SLASH_BENILLAMW2GUIDE2 = "/guide"
    SlashCmdList["BENILLAMW2GUIDE"] = function() G.open = 1 end
  end
  local stock_help = ChatFrame_DisplayHelpText
  ChatFrame_DisplayHelpText = function(frame)
    G.open = 1
    if stock_help then stock_help(frame) end
  end
end)
-- The Escape menu's Edit Mode rung (benilla's GameMenuAdapters.xml), disabled there, opens it.
step(function()
  local b = BenillaGameMenuButtonEditMode
  if not b then return end
  b:SetText("Guide")
  b:Enable()
  b:SetScript("OnClick", function()
    PlaySound("igMainMenuOption")
    HideUIPanel(GameMenuFrame)
    G.open = 1
  end)
end)
"#;

/// Once the character's name is known: open for a character that has not had the guide (unless
/// the player turned it off), and remember it. At most 60 characters are remembered.
pub const FIRST_LOGIN_LUA: &str = r#"
local G = BenillaMw2Guide
if not G or G.checked then return end
local name = UnitName and UnitName("player")
if not name or name == "" or name == UNKNOWNOBJECT then return end
G.checked = 1
local key = name .. "@" .. (GetRealmName and GetRealmName() or "")
local seen = GetCVar("womwGuideSeen") or ""
if string.find(";" .. seen .. ";", ";" .. key .. ";", 1, true) then return end
local list = {}
for k in string.gfind(seen, "[^;]+") do table.insert(list, k) end
table.insert(list, key)
while table.getn(list) > 60 do table.remove(list, 1) end
SetCVar("womwGuideSeen", table.concat(list, ";"))
if GetCVar("womwGuideAuto") ~= "0" then G.open = 1 end
"#;

/// Take the request: whether the VM has the install, and whether the guide was asked for.
const POLL_LUA: &str = "local G = BenillaMw2Guide if not G then return false, false end \
     local o = G.open G.open = nil return true, o ~= nil";

const ERRORS_LUA: &str = "local e = BenillaMw2Guide and BenillaMw2Guide.errors \
     if not e or table.getn(e) == 0 then return nil end return table.concat(e, \"\\n\")";

/// The guide window's state.
#[derive(Resource, Debug)]
pub struct Guide {
    pub pages: Vec<Page>,
    pub open: bool,
    pub page: usize,
    /// Shown at a new character's first login ("Don't show at login" unticked).
    pub auto: bool,
    built: bool,
    /// The panel flag is ours to drop when the window closes.
    held_cursor: bool,
}

impl Default for Guide {
    fn default() -> Self {
        Self {
            pages: parse(GUIDE),
            open: false,
            page: 0,
            auto: true,
            built: false,
            held_cursor: false,
        }
    }
}

pub struct GuidePlugin;

impl Plugin for GuidePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Guide>()
            .add_systems(
                Update,
                (drive, toggle, build_window, click, mark_page)
                    .chain()
                    .in_set(Mw2Set::Hud),
            )
            .add_systems(PostUpdate, hold_cursor);
    }
}

/// Install once per world VM, run the first-login check until it has run, take requests.
fn drive(state: Res<FpsState>, script: Option<NonSend<UiScript>>, mut guide: ResMut<Guide>) {
    let Some(script) = script.filter(|_| crate::capture_keep::in_world(state.in_world, "guide")) else {
        return;
    };
    let Some((installed, asked)) = crate::lua::eval::<(bool, bool)>(&script, POLL_LUA) else {
        return;
    };
    if !installed {
        crate::lua::run(&script, INSTALL_LUA);
        if let Some(Some(e)) = crate::lua::eval::<Option<String>>(&script, ERRORS_LUA) {
            warn!("benilla-mw2: guide install: {e}");
        }
        guide.auto =
            crate::lua::eval::<Option<String>>(&script, "return GetCVar(\"womwGuideAuto\")")
                .flatten()
                .is_none_or(|v| v != "0");
        return;
    }
    crate::lua::run(&script, FIRST_LOGIN_LUA);
    if (asked || crate::capture_keep::keeps("guide")) && !guide.open {
        info!("benilla-mw2: guide opens");
        guide.open = true;
        guide.page = 0;
        guide.built = false;
    }
}

#[derive(Component)]
struct GuideRoot;
#[derive(Component)]
struct GuideClose;
#[derive(Component)]
struct GuideCloseButton;
#[derive(Component)]
struct GuideNext;
#[derive(Component)]
struct GuideAuto;
#[derive(Component)]
struct GuidePage(usize);

/// Escape, the X, the Close button and leaving the world close it.
#[allow(clippy::too_many_arguments)]
fn toggle(
    input: Res<crate::input::Mw2Input>,
    mut guide: ResMut<Guide>,
    mut state: ResMut<FpsState>,
    cac: Res<crate::cac_ui::CacUi>,
    ach: Res<crate::achievements::Achievements>,
    close: Query<
        &Interaction,
        (
            Or<(With<GuideClose>, With<GuideCloseButton>)>,
            Changed<Interaction>,
        ),
    >,
    roots: Query<Entity, With<GuideRoot>>,
    mut commands: Commands,
) {
    if !guide.open {
        // The guide's key (unbound by default) opens it as /help does.
        if input.guide && state.in_world {
            guide.open = true;
            guide.page = 0;
            guide.built = false;
        }
        return;
    }
    let closed = close.iter().any(|i| *i == Interaction::Pressed);
    if closed || ((input.escape || input.guide) && guide.built) || !crate::capture_keep::in_world(state.in_world, "guide") {
        guide.open = false;
        guide.built = false;
        for e in &roots {
            commands.entity(e).despawn();
        }
        state.panel = cac.open || ach.open;
        guide.held_cursor = false;
    }
}

/// How tall the page is before it scrolls, and the column widths, in UI units.
const VIEW_H: f32 = 330.0;
const LIST_W: f32 = 150.0;
const TEXT_W: f32 = 380.0;

fn build_window(
    mut commands: Commands,
    assets: Res<AssetServer>,
    mut guide: ResMut<Guide>,
    roots: Query<Entity, With<GuideRoot>>,
    keymap: Res<crate::keybinds::Keymap>,
) {
    if guide.built || !guide.open {
        return;
    }
    for e in &roots {
        commands.entity(e).despawn();
    }
    guide.built = true;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: "guide",
        title: "Welcome to World of Modern Warfare".into(),
        frame: Frame::Dialog,
        width: Some(LIST_W + TEXT_W + 80.0),
        movable: true,
        anchor: Anchor::CENTER,
        z: i32::MAX - 1,
    };
    let page = guide.pages.get(guide.page).map(|p| with_keys(p, &keymap));
    let titles: Vec<String> = guide.pages.iter().map(|p| p.title.clone()).collect();
    let last = guide.page + 1 >= guide.pages.len();
    let auto = guide.auto;
    wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        (GuideRoot, wow_ui::WindowSounds::MENU),
        Some(GuideClose),
        |root| {
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                margin: UiRect::vertical(u(6.0)),
                ..default()
            })
            .with_children(|row| {
                wow_ui::inset(
                    row,
                    Node {
                        flex_direction: FlexDirection::Column,
                        flex_shrink: 0.0,
                        align_self: AlignSelf::FlexStart,
                        margin: UiRect::right(u(8.0)),
                        padding: UiRect::all(u(3.0)),
                        ..default()
                    },
                    (),
                    |list| {
                        for (i, title) in titles.iter().enumerate() {
                            list.spawn(wow_ui::row_button(f, title, LIST_W, 18.0, GuidePage(i)));
                        }
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
                        wow_ui::scroll_frame(c, VIEW_H, None, (), |v| {
                            if let Some(page) = &page {
                                spawn_page(v, f, page);
                            }
                        });
                    },
                );
            });
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                align_items: AlignItems::Center,
                justify_content: JustifyContent::SpaceBetween,
                ..default()
            })
            .with_children(|bar| {
                let tick = if auto { "[  ]" } else { "[X]" };
                bar.spawn(wow_ui::row_button(
                    f,
                    &format!("{tick}  Don't show at login"),
                    200.0,
                    20.0,
                    GuideAuto,
                ));
                bar.spawn(Node {
                    flex_direction: FlexDirection::Row,
                    ..default()
                })
                .with_children(|b| {
                    // Greyed on the last page, so the buttons do not shift.
                    b.spawn(wow_ui::button(f, "Next", 90.0, GuideNext))
                        .insert(wow_ui::WowButton {
                            disabled: last,
                            ..wow_ui::WowButton::new(wow_ui::Kind::Panel)
                        });
                    b.spawn(wow_ui::button(f, "Close", 90.0, GuideCloseButton));
                });
            });
        },
        |_| {},
    );
}

/// A page with its `{WOMW_X}` keys filled in as they are bound now (crate::keybinds::fill_keys).
pub fn with_keys(page: &Page, keys: &crate::keybinds::Keymap) -> Page {
    let fill = |t: &String| crate::keybinds::fill_keys(t, keys);
    Page {
        title: page.title.clone(),
        blocks: page
            .blocks
            .iter()
            .map(|b| match b {
                Block::Heading(t) => Block::Heading(fill(t)),
                Block::Bullet(t) => Block::Bullet(fill(t)),
                Block::Para(t) => Block::Para(fill(t)),
            })
            .collect(),
    }
}

/// One page's words, wrapped to the text column.
fn spawn_page(v: &mut ChildSpawnerCommands, f: &Handle<Font>, page: &Page) {
    let line =
        |v: &mut ChildSpawnerCommands, s: &str, size: f32, color: Color, top: f32, left: f32| {
            v.spawn((
                text(f, s, size, color),
                Node {
                    width: u(TEXT_W - left - 24.0),
                    margin: UiRect::new(u(left), u(0.0), u(top), u(2.0)),
                    ..default()
                },
            ));
        };
    for block in &page.blocks {
        match block {
            Block::Heading(h) => line(v, h, 14.0, GOLD, 8.0, 0.0),
            Block::Bullet(b) => line(v, &format!("\u{2022}  {b}"), 12.0, WHITE, 2.0, 8.0),
            Block::Para(p) => line(v, p, 12.0, DIM, 3.0, 0.0),
        }
    }
}

/// The page list, Next and the login tick.
fn click(
    mut guide: ResMut<Guide>,
    pages: Query<(&Interaction, &GuidePage), Changed<Interaction>>,
    next: Query<&Interaction, (With<GuideNext>, Changed<Interaction>)>,
    auto: Query<&Interaction, (With<GuideAuto>, Changed<Interaction>)>,
    script: Option<NonSend<UiScript>>,
) {
    if !guide.open {
        return;
    }
    let mut to = None;
    for (i, p) in &pages {
        if *i == Interaction::Pressed {
            to = Some(p.0);
        }
    }
    if next.iter().any(|i| *i == Interaction::Pressed) {
        to = Some((guide.page + 1).min(guide.pages.len().saturating_sub(1)));
    }
    if let Some(to) = to.filter(|t| *t != guide.page) {
        guide.page = to;
        guide.built = false;
        if let Some(s) = script.as_deref() {
            crate::lua::sound(s, "igCharacterInfoTab");
        }
    }
    if auto.iter().any(|i| *i == Interaction::Pressed) {
        guide.auto = !guide.auto;
        guide.built = false;
        if let Some(s) = script.as_deref() {
            let v = if guide.auto { "1" } else { "0" };
            crate::lua::run(s, &format!("SetCVar(\"womwGuideAuto\", \"{v}\")"));
            crate::lua::sound(
                s,
                if guide.auto {
                    "igMainMenuOptionCheckBoxOn"
                } else {
                    "igMainMenuOptionCheckBoxOff"
                },
            );
        }
    }
}

/// The shown page's line in the list is lit.
fn mark_page(guide: Res<Guide>, mut rows: Query<(&GuidePage, &mut WowButton)>) {
    for (p, mut b) in &mut rows {
        let want = p.0 == guide.page;
        if b.selected != want {
            b.selected = want;
        }
    }
}

/// The cursor out and the gun down while it is open, after every other window has written the
/// shared flag.
fn hold_cursor(mut guide: ResMut<Guide>, mut state: ResMut<FpsState>) {
    if guide.open {
        if !state.panel {
            state.panel = true;
        }
        guide.held_cursor = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The renamed things go by their new names everywhere the player reads them: the guide,
    /// the menu bar and quest log, the Escape menu and the dialogs it opens. No score is shown.
    #[test]
    fn the_old_names_and_scores_are_gone() {
        for (what, text) in [
            ("the guide", GUIDE),
            ("the micro menu", crate::micro_menu::INSTALL_LUA),
            ("the feedback dialog", crate::feedback::INSTALL_LUA),
        ] {
            for old in [
                "Journal",
                "Report Bug",
                "Suggest\"",
                "GM Ticket",
                "scoreboard",
                "Scoreboard",
                "{WOMW_SCOREBOARD}",
                "finished off",
                "+100",
            ] {
                assert!(!text.contains(old), "{what} still says {old}");
            }
        }
        assert!(crate::micro_menu::INSTALL_LUA.contains("\"Achievements\""));
        assert!(crate::micro_menu::INSTALL_LUA.contains("SetText(\"Support\")"));
        assert!(crate::feedback::INSTALL_LUA.contains("\"Bugs / Feedback\""));
        assert!(GUIDE.contains("Bugs / Feedback") && GUIDE.contains("Esc then Support"));
    }

    #[test]
    fn the_guide_reads_into_pages() {
        let pages = parse(GUIDE);
        let titles: Vec<&str> = pages.iter().map(|p| p.title.as_str()).collect();
        assert_eq!(
            titles,
            [
                "Welcome",
                "Controls",
                "Hands and spells",
                "Utility and gathering",
                "Progression",
                "Companions",
                "Building",
                "Looting and survival",
                "Bugs and help",
            ]
        );
        assert!(pages.iter().all(|p| !p.blocks.is_empty()));
        // The editors' notes stay out.
        assert!(
            pages
                .iter()
                .flat_map(|p| &p.blocks)
                .all(|b| !matches!(b, Block::Para(t) if t.starts_with("//")))
        );
        let help = pages.last().unwrap();
        let all: String = help.blocks.iter().map(|b| format!("{b:?}")).collect();
        assert!(all.contains("https://discord.gg/PgzdqFmHv"));
        assert!(all.contains("https://www.reddit.com/r/womw/"));
        assert!(all.contains("womw.net"));
    }

    #[test]
    fn the_shapes_parse() {
        let pages = parse("// note\n# One\n## Head\n- a bullet\nA line.\n\n# Two\nB");
        assert_eq!(
            pages,
            vec![
                Page {
                    title: "One".into(),
                    blocks: vec![
                        Block::Heading("Head".into()),
                        Block::Bullet("a bullet".into()),
                        Block::Para("A line.".into()),
                    ],
                },
                Page {
                    title: "Two".into(),
                    blocks: vec![Block::Para("B".into())],
                },
            ]
        );
    }

    /// The guide's keys are the layer's bindings: each `{WOMW_X}` is a real command, and the
    /// pages show the keys bound now (the defaults here, then a rebound one).
    #[test]
    fn the_guide_names_the_keys_bound_now() {
        use crate::keybinds::{ACTIONS, Keymap};
        // Every placeholder names a command (or the pet bar).
        let mut rest = GUIDE;
        while let Some(i) = rest.find('{') {
            let after = &rest[i + 1..];
            let close = after.find('}').expect("a closed placeholder");
            let name = &after[..close];
            assert!(
                name == "PETBAR" || ACTIONS.iter().any(|d| d.command == name),
                "{{{name}}} is no command"
            );
            rest = &after[close + 1..];
        }
        let words = |keys: &Keymap| -> String {
            parse(GUIDE)
                .iter()
                .map(|p| with_keys(p, keys))
                .flat_map(|p| p.blocks)
                .map(|b| format!("{b:?}"))
                .collect()
        };
        let all = words(&Keymap::defaults());
        assert!(!all.contains("{WOMW_"), "every key filled in");
        for key in [
            "X switches between Modern",
            "Z puts your weapons away",
            "Ctrl+Alt to keep it out",
            "R: reload",
            "1, 2, 3, 4, Q and E",
            "Hold Alt and turn the mouse wheel",
            "B: open or close the Construction window",
            "check box on the Honor tab",
            "F: use",
            "V: first",
            "Tab: target the nearest enemy",
            "K: your kits",
            "N: your companions",
            "[ and ]: previous",
            "Middle Mouse takes one",
            "Right Mouse reinforces",
            "Ctrl+1 to Ctrl+0 use its Companion bar",
            "Left Shift",
            "Left Ctrl",
        ] {
            assert!(all.contains(key), "the guide says {key}");
        }
        // No F key and no removed key is named.
        for gone in ["F10", "F5", "F9", "F8", "F7", "F6", "Shift+"] {
            assert!(!all.contains(gone), "the guide still says {gone}");
        }
        let rebound = Keymap::from_pairs([("T", "WOMW_RELOAD"), ("H", "WOMW_SLOT5")]);
        let all = words(&rebound);
        assert!(all.contains("T: reload") && all.contains("Not Bound, H and Not Bound"));
        assert!(all.contains("Not Bound: first"), "an unbound key says so");
    }

    /// The Lua runs in a 1.12 VM: the slash command, /help, the menu rung and the first-login
    /// check, which remembers the character and honours "Don't show at login".
    #[test]
    fn the_lua_installs_and_remembers_characters() {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        s.run(
            r#"
            HelpShown = 0
            SlashCmdList = {}
            function ChatFrame_DisplayHelpText() HelpShown = HelpShown + 1 end
            GameMenuFrame = CreateFrame("Frame", "GameMenuFrame", UIParent)
            local b = CreateFrame("Button", "BenillaGameMenuButtonEditMode", GameMenuFrame)
            b:SetWidth(144) b:SetHeight(21) b:Disable()
            function HideUIPanel(f) f:Hide() end
            PlayerName = "Testa"
            function UnitName() return PlayerName end
            function GetRealmName() return "WOMW" end
            "#,
        )
        .unwrap();
        let poll = |s: &UiScript| s.eval::<(bool, bool)>(POLL_LUA).unwrap();
        assert_eq!(poll(&s), (false, false));
        s.run(INSTALL_LUA).unwrap();
        assert_eq!(s.eval::<Option<String>>(ERRORS_LUA).unwrap(), None);
        // The first login of a character opens it once.
        s.run(FIRST_LOGIN_LUA).unwrap();
        assert_eq!(poll(&s), (true, true));
        assert_eq!(poll(&s), (true, false));
        s.run("BenillaMw2Guide.checked = nil").unwrap();
        s.run(FIRST_LOGIN_LUA).unwrap();
        assert_eq!(
            poll(&s),
            (true, false),
            "the same character again: not shown"
        );
        // Another character is new, unless the player said not at login.
        s.run("BenillaMw2Guide.checked = nil PlayerName = \"Other\" SetCVar(\"womwGuideAuto\", \"0\")")
            .unwrap();
        s.run(FIRST_LOGIN_LUA).unwrap();
        assert_eq!(poll(&s), (true, false));
        assert_eq!(
            s.eval::<String>("return GetCVar(\"womwGuideSeen\")")
                .unwrap(),
            "Testa@WOMW;Other@WOMW"
        );
        // /help (the stock list stays), /womw and the menu rung.
        s.run("ChatFrame_DisplayHelpText(nil)").unwrap();
        assert_eq!(poll(&s), (true, true));
        assert_eq!(s.eval::<i64>("return HelpShown").unwrap(), 1);
        s.run("SlashCmdList[\"BENILLAMW2GUIDE\"](\"\")").unwrap();
        assert_eq!(poll(&s), (true, true));
        let (text, enabled): (String, bool) = s
            .eval(
                "local b = BenillaGameMenuButtonEditMode \
                 return b:GetText(), b:IsEnabled() == 1 or b:IsEnabled() == true",
            )
            .unwrap();
        assert_eq!((text.as_str(), enabled), ("Guide", true));
        s.run("BenillaGameMenuButtonEditMode:Click()").unwrap();
        assert_eq!(poll(&s), (true, true));
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }

    #[test]
    fn the_window_builds_on_every_page() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        let pages = parse(GUIDE).len();
        for page in 0..pages {
            world.insert_resource(Guide {
                open: true,
                page,
                ..default()
            });
            world.run_system_once(build_window).unwrap();
            let mut roots = world.query_filtered::<Entity, With<GuideRoot>>();
            assert_eq!(roots.iter(&world).count(), 1, "page {page}");
            assert!(world.resource::<Guide>().built);
        }
    }
}
