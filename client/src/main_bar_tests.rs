//! The re-fitted main bar over the player's own stock bar files (read off their install's patch
//! chain, never committed): its size and seat, the system's emblem and name, the options, the way
//! back to the stock bar, and a layout sweep (screen shapes from 4:3 to ultrawide, `uiScale`
//! 0.64 to 1, the large and the stock-sized buttons, the reputation bar and the bottom multibars
//! on and off) in which nothing of the bar, the XP bar, the micro menu, the bags, the emblem, the
//! system's name, the chat frame's default seat or the gun HUD's column overlaps another.

use std::cell::RefCell;

use benilla_ui::script::UiScript;

use super::*;
use crate::hud::{BAG_BAR_LUA, COLUMN_UNITS, GAP_UNITS, ammo_seat, bag_bar_from_lua};

/// The stock files the bar and what stands round it need, in `FrameXML.toc` order.
const STOCK: &[&str] = &[
    r"Interface\FrameXML\GlobalStrings.lua",
    r"Interface\FrameXML\Fonts.xml",
    r"Interface\FrameXML\BasicControls.xml",
    r"Interface\FrameXML\LocaleProperties.lua",
    r"Interface\FrameXML\UIParent.xml",
    r"Interface\FrameXML\MoneyFrame.lua",
    r"Interface\FrameXML\MoneyFrame.xml",
    r"Interface\FrameXML\GameTooltip.xml",
    r"Interface\FrameXML\UIMenu.xml",
    r"Interface\FrameXML\UIDropDownMenu.xml",
    r"Interface\FrameXML\UIPanelTemplates.lua",
    r"Interface\FrameXML\UIPanelTemplates.xml",
    r"Interface\FrameXML\OptionsFrameTemplates.xml",
    r"Interface\FrameXML\StaticPopup.xml",
    r"Interface\FrameXML\TextStatusBar.lua",
    r"Interface\FrameXML\TextStatusBar.xml",
    r"Interface\FrameXML\MainMenuBar.xml",
    r"Interface\FrameXML\MainMenuBarMicroButtons.xml",
    r"Interface\FrameXML\Cooldown.xml",
    r"Interface\FrameXML\ActionButtonTemplate.xml",
    r"Interface\FrameXML\ActionBarFrame.xml",
    r"Interface\FrameXML\MultiActionBars.xml",
    r"Interface\FrameXML\ChatFrame.xml",
    r"Interface\FrameXML\FloatingChatFrame.xml",
    r"Interface\FrameXML\ItemButtonTemplate.xml",
    r"Interface\FrameXML\PaperDollFrame.xml",
    r"Interface\FrameXML\MainMenuBarBagButtons.xml",
    r"Interface\FrameXML\ContainerFrame.xml",
    r"Interface\FrameXML\BankFrame.xml",
    r"Interface\FrameXML\ReputationFrame.xml",
    r"Interface\FrameXML\CastingBarFrame.xml",
    r"Interface\FrameXML\BonusActionBarFrame.xml",
];

/// What the files above owe the parts of the manifest this kit leaves out, as benilla's own test
/// kits seat them: no-op functions, and the frames `UIParent_ManageFramePositions` reads seated
/// on its first call.
const STAND_INS: &str = r#"
    FCF_OnUpdate = FCF_OnUpdate or function() end
    FCF_DockUpdate = FCF_DockUpdate or function() end
    UnitPopup_OnUpdate = UnitPopup_OnUpdate or function() end
    BattlefieldFrame_OnUpdate = BattlefieldFrame_OnUpdate or function() end
    MultiActionBar_Update = MultiActionBar_Update or function() end
    RaidOptionsFrame_UpdatePartyFrames = RaidOptionsFrame_UpdatePartyFrames or function() end
    LocalizeFrames = LocalizeFrames or function() end
    UpdateNameplates = UpdateNameplates or function() end
    CloseAllBags = CloseAllBags or function() end
    OpenBackpack = OpenBackpack or function() end
    CloseBackpack = CloseBackpack or function() end
    NUM_CONTAINER_FRAMES = NUM_CONTAINER_FRAMES or 0
    CONTAINER_OFFSET_X = CONTAINER_OFFSET_X or 0
    CONTAINER_OFFSET_Y = CONTAINER_OFFSET_Y or 70
    BATTLEFIELD_TAB_OFFSET_Y = BATTLEFIELD_TAB_OFFSET_Y or 210
    PETACTIONBAR_YPOS = PETACTIONBAR_YPOS or 98
    PETACTIONBAR_XPOS = PETACTIONBAR_XPOS or 36
    UIOptionsFrameCheckButtons = UIOptionsFrameCheckButtons or {}
    for _, key in ipairs({ "SHOW_MULTIBAR1_TEXT", "SHOW_MULTIBAR2_TEXT", "SHOW_MULTIBAR3_TEXT",
                           "SHOW_MULTIBAR4_TEXT", "ALWAYS_SHOW_MULTIBARS_TEXT" }) do
        UIOptionsFrameCheckButtons[key] = UIOptionsFrameCheckButtons[key] or {}
    end
    local function seat(names)
        for _, name in ipairs(names) do
            if not getglobal(name) then local f = CreateFrame("Frame") f:Hide() setglobal(name, f) end
        end
    end
    local real_manage = UIParent_ManageFramePositions
    function UIParent_ManageFramePositions()
        seat({ "MainMenuBar", "MultiBarLeft", "MultiBarRight", "MultiBarBottomLeft",
            "PetActionBarFrame", "ShapeshiftBarFrame", "ReputationWatchBar", "MainMenuExpBar",
            "MainMenuBarMaxLevelBar", "CastingBarFrame", "QuestTimerFrame", "QuestWatchFrame",
            "DurabilityFrame", "DurabilityShield", "DurabilityOffWeapon", "DurabilityRanged",
            "MinimapCluster", "ChatFrame1", "ChatFrame2", "ShapeshiftBarLeft",
            "ShapeshiftBarMiddle", "ShapeshiftBarRight", "BattlefieldMinimapTab",
            "GroupLootFrame1", "TutorialFrameParent", "FramerateLabel" })
        for _, name in ipairs({ "SlidingActionBarTexture0", "SlidingActionBarTexture1" }) do
            if not getglobal(name) then setglobal(name, UIParent:CreateTexture()) end
        end
        return real_manage()
    end
    local real_micro = UpdateMicroButtons
    function UpdateMicroButtons()
        seat({ "CharacterFrame", "SpellBookFrame", "QuestLogFrame", "GameMenuFrame", "OptionsFrame",
            "SoundOptionsFrame", "UIOptionsFrame", "FriendsFrame", "WorldMapFrame", "HelpFrame" })
        if not KEYRING_CONTAINER then KEYRING_CONTAINER = -2 end
        if not IsBagOpen then function IsBagOpen() return nil end end
        if real_micro then return real_micro() end
    end
    UIErrorsFrame = UIErrorsFrame or { said = {} }
    function UIErrorsFrame:AddMessage(text) table.insert(self.said, text) end
    -- The options window's Interface page, as the layer's OptionsFrame.xml builds it.
    local page = CreateFrame("Frame", "BenillaOptionsFrameContainerBodyInterface", UIParent)
    CreateFrame("Frame", "BenillaOptionsFrameContainerBodyInterfaceRowNewbieTips", page)
    BENILLA_OPTIONS_PAGE_ROWS = { Interface = { "RowNewbieTips" } }
    function BenillaOptionsStrFuncRow_OnLoad(row, get, set, default, label, tip)
      row.strGetFunc, row.strSetFunc, row.strDefaultFunc = get, set, default
      row.label, row.tip = label, tip
    end
"#;

/// A VM with the stock bar loaded off the player's chain, on a screen `w`×`h` UI units.
fn stock_vm(data: &std::path::Path, w: f32, h: f32) -> UiScript {
    let chain = RefCell::new(benilla_formats::open_chain(data).expect("the patch chain"));
    let read = |p: &str| chain.borrow_mut().read_file(&p.replace('/', "\\")).ok();
    let mut s = UiScript::new().expect("a VM");
    s.set_screen_size(w, h);
    for file in STOCK {
        let path = file.replace('\\', "/");
        let bytes = read(&path).unwrap_or_else(|| panic!("{file}: not in the chain"));
        if path.ends_with(".lua") {
            s.run_chunk_named(&bytes, &format!("@{file}"))
                .unwrap_or_else(|e| panic!("{file}: {e}"));
        } else {
            let doc = benilla_ui::framexml::parse(&benilla_ui::source::decode(&bytes))
                .unwrap_or_else(|e| panic!("{file}: {e}"));
            let report = benilla_ui::loader::load_in(&s, &doc, &path, &read);
            assert!(report.errors.is_empty(), "{file}: {:?}", report.errors);
        }
        if path.ends_with("UIParent.xml") {
            s.run(STAND_INS).expect("the stand-ins");
        }
    }
    s.resolve();
    let errors = s.take_errors();
    assert!(errors.is_empty(), "stock load: {errors:?}");
    s
}

/// The stock bar with the micro menu and the bar's re-fit installed, as the first world frame
/// installs them, showing `system`'s first page.
fn harness(data: &std::path::Path, w: f32, h: f32, large: bool, system: System) -> UiScript {
    let mut s = stock_vm(data, w, h);
    s.run(&format!(
        "RegisterCVar(\"womwLargeActionButtons\", \"1\") SetCVar(\"womwLargeActionButtons\", \"{}\")",
        if large { 1 } else { 0 }
    ))
    .unwrap();
    s.run(crate::micro_menu::INSTALL_LUA)
        .expect("the micro menu");
    s.run(&install_lua()).expect("the bar's install");
    assert_eq!(
        s.eval::<Option<String>>(INSTALL_ERRORS_LUA).unwrap(),
        None,
        "every step went in"
    );
    show(&mut s, system);
    s
}

fn show(s: &mut UiScript, system: System) {
    s.run(&set_lua(Some(&BarView {
        system,
        page: 1,
        pages: system.pages(),
    })))
    .unwrap();
    s.resolve();
}

fn eval<T: mlua::FromLuaMulti>(s: &UiScript, chunk: &str) -> T {
    s.eval::<T>(chunk)
        .unwrap_or_else(|e| panic!("`{chunk}`: {e}"))
}

/// A frame's rectangle in screen units: left, bottom, right, top.
#[derive(Clone, Copy, Debug)]
struct R {
    l: f32,
    b: f32,
    r: f32,
    t: f32,
}

impl R {
    fn overlaps(&self, o: &R) -> bool {
        // Touching edges (a shared side) is no overlap; 0.01 for rounding.
        self.l < o.r - 0.01 && o.l < self.r - 0.01 && self.b < o.t - 0.01 && o.b < self.t - 0.01
    }
    fn w(&self) -> f32 {
        self.r - self.l
    }
}

fn rect(s: &UiScript, name: &str) -> R {
    let (l, b, r, t): (f32, f32, f32, f32) = eval(
        s,
        &format!(
            "local f = {name} local e = f:GetEffectiveScale() \
             return f:GetLeft() * e, f:GetBottom() * e, f:GetRight() * e, f:GetTop() * e"
        ),
    );
    R { l, b, r, t }
}

/// A texture's rectangle (its parent's scale).
fn region_rect(s: &UiScript, name: &str, parent: &str) -> R {
    let (l, b, r, t): (f32, f32, f32, f32) = eval(
        s,
        &format!(
            "local f = {name} local e = {parent}:GetEffectiveScale() \
             return f:GetLeft() * e, f:GetBottom() * e, f:GetRight() * e, f:GetTop() * e"
        ),
    );
    R { l, b, r, t }
}

fn shown(s: &UiScript, name: &str) -> bool {
    eval(s, &format!("return {name}:IsVisible() and true or false"))
}

const MICRO: [&str; 8] = [
    "CharacterMicroButton",
    "BenillaMw2KitsMicroButton",
    "QuestLogMicroButton",
    "SocialsMicroButton",
    "BenillaMw2ProfessionsMicroButton",
    "BenillaMw2CompanionsMicroButton",
    "WorldMapMicroButton",
    "MainMenuMicroButton",
];
const BAGS: [&str; 5] = [
    "MainMenuBarBackpackButton",
    "CharacterBag0Slot",
    "CharacterBag1Slot",
    "CharacterBag2Slot",
    "CharacterBag3Slot",
];

/// Every element the sweep keeps apart, by name, with its rectangle.
fn elements(s: &UiScript, sw: f32, sh: f32) -> Vec<(String, R)> {
    let mut out = Vec::new();
    for i in 1..=6 {
        let n = format!("ActionButton{i}");
        out.push((n.clone(), rect(s, &n)));
    }
    for n in ["MainMenuExpBar", "BenillaMw2SystemButton", "ChatFrame1"] {
        out.push((n.to_string(), rect(s, n)));
    }
    if shown(s, "BenillaMw2SystemLabelFrame") {
        out.push(("label".into(), rect(s, "BenillaMw2SystemLabelFrame")));
    }
    for n in [
        "ReputationWatchBar",
        "MultiBarBottomLeft",
        "MultiBarBottomRight",
    ] {
        if shown(s, n) {
            out.push((n.to_string(), rect(s, n)));
        }
    }
    // The micro buttons' art: 58 tall over a transparent top the hit rect leaves out (18), and
    // each laps its neighbour by 3 as stock; their drawn faces, inset.
    for n in MICRO {
        let r = rect(s, n);
        out.push((
            n.to_string(),
            R {
                l: r.l + 2.0,
                b: r.b,
                r: r.r - 2.0,
                t: r.t - 20.0,
            },
        ));
    }
    for n in BAGS {
        out.push((n.to_string(), rect(s, n)));
    }
    // The gun HUD's column, seated as crate::hud seats it, in UI units (one pixel a unit).
    type O = Option<f32>;
    let (a, b, c, d, e, f, g) = eval::<(O, O, O, O, O, O, O)>(s, BAG_BAR_LUA);
    let bags = bag_bar_from_lua([a, b, c, d, e, f, g].map(|x| x.expect("measured")));
    let (right, bottom) = ammo_seat(bags, bevy::math::Vec2::new(sw, sh), GAP_UNITS);
    let edge = sw - right;
    out.push((
        "gun HUD".into(),
        R {
            l: edge - COLUMN_UNITS.x,
            b: bottom,
            r: edge,
            t: bottom + COLUMN_UNITS.y,
        },
    ));
    out
}

/// Whether two named elements may touch: the micro buttons lap each other as stock, and the
/// reputation's bar laps the XP bar's top edge by 3 as stock stacks them.
fn may_overlap(a: &str, b: &str) -> bool {
    let pair = |x: &str, y: &str| (a == x && b == y) || (a == y && b == x);
    (a.ends_with("MicroButton") && b.ends_with("MicroButton"))
        || pair("MainMenuExpBar", "ReputationWatchBar")
}

fn assert_apart(s: &UiScript, sw: f32, sh: f32, what: &str) {
    let all = elements(s, sw, sh);
    for (i, (an, a)) in all.iter().enumerate() {
        assert!(
            a.l >= -0.01 && a.r <= sw + 0.01 && a.b >= -0.01 && a.t <= sh + 0.01,
            "{what}: {an} off the screen: {a:?} on {sw}x{sh}"
        );
        for (bn, b) in &all[i + 1..] {
            assert!(
                may_overlap(an, bn) || !a.overlaps(b),
                "{what}: {an} {a:?} overlaps {bn} {b:?}"
            );
        }
    }
    // The wells' art reaches into the XP bar no further than the stock art's top lip (3 units,
    // the XP bar's black under its own art): the large wells stand beside it.
    let art = region_rect(s, "MainMenuBarTexture0", "MainMenuBarArtFrame");
    let xp = rect(s, "MainMenuExpBar");
    let side_by_side = art.r <= xp.l + 0.01 || xp.r <= art.l + 0.01;
    assert!(
        side_by_side || art.t <= xp.b + 3.01,
        "{what}: the wells' art {art:?} into the XP bar {xp:?}"
    );
}

/// Screen shapes (window pixels) and `uiScale`s: 4:3, 16:9 at three sizes, 21:9 ultrawides.
const SCREENS: [(f32, f32); 6] = [
    (1024.0, 768.0),
    (1280.0, 720.0),
    (1920.0, 1080.0),
    (2560.0, 1440.0),
    (2560.0, 1080.0),
    (3440.0, 1440.0),
];
const UI_SCALES: [f32; 3] = [0.64, 0.9, 1.0];

/// The screen in UI units for a window and a `uiScale` (benilla's seam: `768/uiScale` tall).
fn units(w: f32, h: f32, ui_scale: f32) -> (f32, f32) {
    let uh = 768.0 / ui_scale;
    (uh * w / h, uh)
}

#[test]
fn nothing_overlaps_at_any_screen_scale_or_option() {
    let data = benilla_formats::wow_data_or_skip!();
    for large in [true, false] {
        // One VM a layout: the screen is resized under it, as a window resize does.
        let mut s = harness(&data, 1024.0, 768.0, large, System::Modern);
        for (w, h) in SCREENS {
            for ui_scale in UI_SCALES {
                let (sw, sh) = units(w, h, ui_scale);
                s.set_screen_size(sw, sh);
                for (rep, multibars) in [(false, false), (true, false), (false, true), (true, true)]
                {
                    s.run(&format!(
                        "if {rep} then ReputationWatchBar:Show() else ReputationWatchBar:Hide() end \
                         BenillaMw2Bar.fit_rep() \
                         SHOW_MULTI_ACTIONBAR_1 = {m} SHOW_MULTI_ACTIONBAR_2 = {m} \
                         if {multibars} then MultiBarBottomLeft:Show() MultiBarBottomRight:Show() \
                         else MultiBarBottomLeft:Hide() MultiBarBottomRight:Hide() end \
                         UIParent_ManageFramePositions()",
                        m = if multibars { "1" } else { "nil" }
                    ))
                    .unwrap();
                    s.resolve();
                    let what = format!(
                        "{w}x{h} at {ui_scale}, large {large}, reputation {rep}, multibars {multibars}"
                    );
                    assert_apart(&s, sw, sh, &what);
                }
            }
        }
        let errors = s.take_errors();
        assert!(errors.is_empty(), "script errors: {errors:?}");
    }
}

#[test]
fn six_larger_buttons_centred_on_the_six_wells() {
    let data = benilla_formats::wow_data_or_skip!();
    let s = harness(&data, 1024.0, 768.0, true, System::Modern);
    let g = layout(true);
    let (sw, _): (f32, f32) = eval(&s, "return GetScreenWidth(), GetScreenHeight()");
    let bar = rect(&s, "MainMenuBar");
    assert!((bar.w() - g.width).abs() < 0.01, "{bar:?}");
    assert!(
        ((bar.l + bar.r) / 2.0 - sw / 2.0).abs() < 0.01,
        "centred: {bar:?}"
    );
    assert!(bar.b.abs() < 0.01, "at the bottom");
    let art = region_rect(&s, "MainMenuBarTexture0", "MainMenuBarArtFrame");
    assert!((art.w() - 256.0 * LARGE_SCALE).abs() < 0.01);
    assert!((art.t - art.b - 43.0 * LARGE_SCALE).abs() < 0.01);
    assert!(
        !shown(&s, "MainMenuBarTexture1"),
        "buttons 7-12's wells are gone"
    );
    for i in 1..=6 {
        let b = rect(&s, &format!("ActionButton{i}"));
        assert!(
            (b.w() - 36.0 * LARGE_SCALE).abs() < 0.01,
            "button {i}: {b:?}"
        );
        // On its well: the stock 8 + 42 a button, at the wells' scale.
        let x = art.l + (8.0 + 42.0 * (i - 1) as f32) * LARGE_SCALE;
        assert!((b.l - x).abs() < 0.01, "button {i} at {x}: {b:?}");
        assert!((b.b - 4.0 * LARGE_SCALE).abs() < 0.01);
        assert!(b.r <= art.r, "button {i} inside the wells");
    }
    // The stock page piece and the micro menu follow the wells, the bags close the bar.
    let page = region_rect(&s, "MainMenuBarTexture2", "MainMenuBarArtFrame");
    assert!((page.l - art.r).abs() < 0.01 && (page.t - page.b - 43.0).abs() < 0.01);
    let micro = rect(&s, "CharacterMicroButton");
    assert!((micro.l - (art.r + 40.0)).abs() < 0.01, "{micro:?}");
    let bags = rect(&s, "MainMenuBarBackpackButton");
    assert!((bags.r - (bar.r - 6.0)).abs() < 0.01);
    // The XP bar over the page piece and the bags piece, its art fitted to it.
    let xp = rect(&s, "MainMenuExpBar");
    assert!(
        (xp.l - art.r).abs() < 0.01 && (xp.r - bar.r).abs() < 0.01,
        "{xp:?}"
    );
    assert!((xp.t - 53.0).abs() < 0.01);
    let piece = region_rect(&s, "MainMenuXPBarTexture3", "MainMenuExpBar");
    assert!((piece.r - xp.r).abs() < 0.01 && (piece.w() - 128.0).abs() < 0.01);
    // The stock-sized buttons: the stock wells, and the XP bar over the whole bar again.
    let mut small = harness(&data, 1024.0, 768.0, false, System::Modern);
    small.resolve();
    assert!((rect(&small, "ActionButton1").w() - 36.0).abs() < 0.01);
    let bar = rect(&small, "MainMenuBar");
    assert!((bar.w() - 768.0).abs() < 0.01);
    let xp = rect(&small, "MainMenuExpBar");
    assert!((xp.l - bar.l).abs() < 0.01 && (xp.r - bar.r).abs() < 0.01);
}

#[test]
fn the_system_shows_and_a_switch_fades_in() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data, 1024.0, 768.0, true, System::Modern);
    let label = |s: &UiScript| eval::<String>(s, "return BenillaMw2SystemLabel:GetText()");
    let icon = |s: &UiScript| eval::<String>(s, "return BenillaMw2SystemButtonIcon:GetTexture()");
    assert!(label(&s).starts_with("Modern"), "{}", label(&s));
    assert!(label(&s).contains("1/3"), "{}", label(&s));
    assert!(
        icon(&s).eq_ignore_ascii_case(emblem(System::Modern)),
        "{}",
        icon(&s)
    );
    assert!(shown(&s, "BenillaMw2SystemButton"));
    assert!(shown(&s, "BenillaMw2SystemLabelFrame"));
    let alpha = |s: &UiScript| eval::<f64>(s, "return ActionButton1:GetAlpha()");
    assert!((alpha(&s) - 1.0).abs() < 1e-6, "no fade at the first show");
    // X: Classic. The buttons come up from faint, the name flashes, both settle.
    show(&mut s, System::Classic);
    assert!(label(&s).starts_with("Classic") && label(&s).contains("1/10"));
    assert!(icon(&s).eq_ignore_ascii_case(emblem(System::Classic)));
    assert!(alpha(&s) < 0.5, "faded at the switch: {}", alpha(&s));
    for _ in 0..10 {
        s.tick(0.05);
    }
    assert!(
        (alpha(&s) - 1.0).abs() < 1e-6,
        "in after the fade: {}",
        alpha(&s)
    );
    // Z: Utility, on its second page.
    s.run(&set_lua(Some(&BarView {
        system: System::Utility,
        page: 2,
        pages: System::Utility.pages(),
    })))
    .unwrap();
    assert!(label(&s).starts_with("Utility") && label(&s).contains("2/3"));
    assert!(icon(&s).eq_ignore_ascii_case(emblem(System::Utility)));
    // The emblem's tooltip names the system, its page and the keys.
    s.run("SetBinding(\"X\", \"WOMW_SWITCH\") SetBinding(\"Z\", \"WOMW_SHEATHE\") SetBinding(\"H\", \"NAMEPLATES\") BenillaMw2Bar.Tooltip()")
        .unwrap();
    let tip: Vec<String> = (1..=12)
        .map(|i| {
            eval::<Option<String>>(&s, &format!("return GameTooltipTextLeft{i}:GetText()"))
                .unwrap_or_default()
        })
        .collect();
    assert_eq!(tip[0], "Utility System", "{tip:?}");
    assert_eq!(tip[1], "Page 2 of 3");
    assert!(
        tip.iter().any(|l| l.contains("Modern / Classic: X")),
        "{tip:?}"
    );
    assert!(tip.iter().any(|l| l.contains("(Utility): Z")), "{tip:?}");
    assert!(tip.iter().any(|l| l.contains("Nameplates: H")), "{tip:?}");
    assert!(tip.iter().any(|l| l == "Next page: Unbound"), "{tip:?}");
    // Removing a binding must not advertise its old default. Either page key can stand alone.
    s.run("SetBinding(\"X\") SetBinding(\"J\", \"WOMW_PAGEPREV\") BenillaMw2Bar.Tooltip()")
        .unwrap();
    assert_eq!(
        eval::<String>(&s, "return GameTooltipTextLeft3:GetText()"),
        "Next page: Unbound"
    );
    assert_eq!(
        eval::<String>(&s, "return GameTooltipTextLeft4:GetText()"),
        "Previous page: J"
    );
    assert_eq!(
        eval::<String>(&s, "return GameTooltipTextLeft5:GetText()"),
        "Modern / Classic: Unbound"
    );
    let errors = s.take_errors();
    assert!(errors.is_empty(), "script errors: {errors:?}");
}

#[test]
fn the_options_resize_label_and_dress_the_bar() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data, 1024.0, 768.0, true, System::Modern);
    let rows: Vec<String> = eval(&s, "return BENILLA_OPTIONS_PAGE_ROWS.Interface");
    assert_eq!(
        rows,
        [
            "RowNewbieTips",
            "RowLargeActionButtons",
            "RowShowSystemLabel",
            "RowShowGryphons"
        ],
        "three rows on the Interface page, in order"
    );
    let row = |key: &str| format!("BenillaOptionsFrameContainerBodyInterface{key}");
    let defaults: (String, String, String) = eval(
        &s,
        &format!(
            "return {}.strDefaultFunc(), {}.strDefaultFunc(), {}.strDefaultFunc()",
            row("RowLargeActionButtons"),
            row("RowShowSystemLabel"),
            row("RowShowGryphons")
        ),
    );
    assert_eq!(defaults, ("1".into(), "1".into(), "0".into()));
    // Large Action Buttons off: the stock size, and back.
    s.run(&format!(
        "{}.strSetFunc(\"0\")",
        row("RowLargeActionButtons")
    ))
    .unwrap();
    s.resolve();
    assert!((rect(&s, "ActionButton1").w() - 36.0).abs() < 0.01);
    s.run(&format!(
        "{}.strSetFunc(\"1\")",
        row("RowLargeActionButtons")
    ))
    .unwrap();
    s.resolve();
    assert!((rect(&s, "ActionButton1").w() - 36.0 * LARGE_SCALE).abs() < 0.01);
    // The system's name off and on.
    s.run(&format!("{}.strSetFunc(\"0\")", row("RowShowSystemLabel")))
        .unwrap();
    assert!(!shown(&s, "BenillaMw2SystemLabelFrame"));
    s.run(&format!("{}.strSetFunc(\"1\")", row("RowShowSystemLabel")))
        .unwrap();
    assert!(shown(&s, "BenillaMw2SystemLabelFrame"));
    // Show Gryphons: off by default, at the bar's ends when on.
    let caps = |s: &UiScript| -> (bool, bool) {
        eval(
            s,
            "return MainMenuBarLeftEndCap:IsShown() and true or false, \
             MainMenuBarRightEndCap:IsShown() and true or false",
        )
    };
    assert_eq!(caps(&s), (false, false), "no gryphons by default");
    s.run(&format!("{}.strSetFunc(\"1\")", row("RowShowGryphons")))
        .unwrap();
    s.resolve();
    assert_eq!(caps(&s), (true, true));
    let bar = rect(&s, "MainMenuBar");
    let left = region_rect(&s, "MainMenuBarLeftEndCap", "MainMenuBarArtFrame");
    let right = region_rect(&s, "MainMenuBarRightEndCap", "MainMenuBarArtFrame");
    assert!((left.r - (bar.l + 32.0)).abs() < 0.01, "{left:?} {bar:?}");
    assert!((right.l - (bar.r - 32.0)).abs() < 0.01, "{right:?} {bar:?}");
    s.run(&format!("{}.strSetFunc(\"0\")", row("RowShowGryphons")))
        .unwrap();
    assert_eq!(caps(&s), (false, false));
    let errors = s.take_errors();
    assert!(errors.is_empty(), "script errors: {errors:?}");
}

#[test]
fn the_layer_off_puts_the_stock_bar_back() {
    let data = benilla_formats::wow_data_or_skip!();
    let fresh = stock_vm(&data, 1024.0, 768.0);
    let stock_places = |s: &UiScript| -> Vec<f32> {
        let mut v = Vec::new();
        for n in [
            "MainMenuBar",
            "ActionButton1",
            "ActionButton12",
            "MainMenuExpBar",
            "CharacterMicroButton",
            "MainMenuBarBackpackButton",
            "MultiBarBottomLeft",
            "ChatFrame1",
        ] {
            let r = rect(s, n);
            v.extend([r.l, r.b, r.r, r.t]);
        }
        let art = region_rect(s, "MainMenuBarTexture3", "MainMenuBarArtFrame");
        v.extend([art.l, art.r]);
        let xp = region_rect(s, "MainMenuXPBarTexture0", "MainMenuExpBar");
        v.extend([xp.l, xp.r]);
        v
    };
    let mut fresh = fresh;
    fresh.run("UIParent_ManageFramePositions()").unwrap();
    fresh.resolve();
    let want = stock_places(&fresh);
    let mut s = harness(&data, 1024.0, 768.0, true, System::Classic);
    s.run(&set_lua(None)).unwrap();
    s.resolve();
    let got = stock_places(&s);
    for (i, (a, b)) in want.iter().zip(&got).enumerate() {
        assert!((a - b).abs() < 0.01, "place {i}: stock {a}, back {b}");
    }
    assert!(shown(&s, "MainMenuBarTexture1"));
    assert!(!shown(&s, "BenillaMw2SystemButton"));
    assert!(!shown(&s, "BenillaMw2SystemLabelFrame"));
    assert_eq!(
        eval::<f64>(
            &s,
            "return UIPARENT_MANAGED_FRAME_POSITIONS.CastingBarFrame.baseY"
        ),
        60.0
    );
    // And on again.
    show(&mut s, System::Modern);
    assert!((rect(&s, "ActionButton1").w() - 36.0 * LARGE_SCALE).abs() < 0.01);
    let errors = s.take_errors();
    assert!(errors.is_empty(), "script errors: {errors:?}");
}

/// The frames the bar's growth pushes up keep clear of it: the casting bar's and the stance bar's
/// managed seats, and a form's bonus bar over the six wells at their scale.
#[test]
fn what_stacks_on_the_bar_clears_the_wells() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data, 1024.0, 768.0, true, System::Modern);
    let g = layout(true);
    let base: f64 = eval(
        &s,
        "return UIPARENT_MANAGED_FRAME_POSITIONS.CastingBarFrame.baseY",
    );
    assert!(base as f32 >= 60.0 + g.big_h - BAR_H, "{base}");
    s.run(
        "ShowBonusActionBar() for i = 1, 20 do BonusActionBarFrame:GetScript(\"OnUpdate\")() end",
    )
    .ok();
    s.run("BonusActionBarFrame:Show() BonusActionBarFrame:SetPoint(\"TOPLEFT\", MainMenuBar, \"BOTTOMLEFT\", BONUSACTIONBAR_XPOS, BONUSACTIONBAR_YPOS)")
        .unwrap();
    s.resolve();
    let bonus = rect(&s, "BonusActionBarFrame");
    let art = region_rect(&s, "MainMenuBarTexture0", "MainMenuBarArtFrame");
    assert!(
        (bonus.t - art.t).abs() < 0.01,
        "level with the wells: {bonus:?} {art:?}"
    );
    assert!(
        bonus.r <= art.r + 0.01,
        "no wider than the wells: {bonus:?} {art:?}"
    );
    let first = rect(&s, "BonusActionButton1");
    assert!((first.w() - 36.0 * LARGE_SCALE).abs() < 0.01);
    assert!(
        eval::<f64>(&s, "return BonusActionButton7:GetAlpha()") < 0.01,
        "its buttons 7-12 are not drawn"
    );
    let micro = rect(&s, "CharacterMicroButton");
    assert!(bonus.r <= micro.l, "the micro menu stays clear of it");
}

/// H says what it did, once the layer is in the world.
#[test]
fn the_nameplate_key_says_what_it_did() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = stock_vm(&data, 1024.0, 768.0);
    s.run("ShowNameplates = function() end HideNameplates = function() end")
        .unwrap();
    s.run(crate::micro_menu::INSTALL_LUA).unwrap();
    s.run(&install_lua()).unwrap();
    show(&mut s, System::Modern);
    s.run("BenillaMw2Bar.installed = -100 SetBinding(\"H\", \"NAMEPLATES\") ShowNameplates() HideNameplates()")
        .unwrap();
    let said: Vec<String> = eval(&s, "return UIErrorsFrame.said");
    assert_eq!(said, ["Nameplates shown (H)", "Nameplates hidden (H)"]);
}

#[test]
fn the_layouts_numbers() {
    let g = layout(true);
    assert!((g.big_w - 363.52).abs() < 0.01 && (g.big_h - 61.06).abs() < 0.01);
    assert!((g.width - (g.big_w + 512.0)).abs() < 0.01);
    assert!(g.width < BAR_W, "shorter than the stock bar");
    assert_eq!((g.xp_left, g.xp_width), (g.big_w, 512.0));
    assert!(g.multibar_bottom >= g.left_top + LABEL_ROOM);
    assert!(
        g.raise >= g.big_h - BAR_H,
        "the managed frames clear the wells"
    );
    assert!(
        g.raise + g.raise_stack >= g.multibar_bottom - STOCK_MULTIBAR_BOTTOM,
        "and the risen multibars"
    );
    let small = layout(false);
    assert_eq!(small.scale, 1.0);
    assert_eq!((small.xp_left, small.xp_width), (0.0, 768.0));
    assert!(small.multibar_bottom >= REP_TOP + LABEL_ROOM);
    // Lua literals.
    assert!(g.lua().contains("scale = 1.42"));
    assert_eq!(
        set_lua(Some(&BarView {
            system: System::Classic,
            page: 3,
            pages: 10
        })),
        r#"if BenillaMw2Bar then BenillaMw2Bar.Set(true, "Classic", "Interface\\Icons\\INV_Sword_04", 3, 10) end"#
    );
}
