//! The windowed map over the player's own `WorldMapFrame.xml` (read off their install's patch
//! chain, never committed): opening and closing, dragging and its memory, the coordinates, zone
//! navigation, fading on the move, sizing, and the switch back to the stock full-screen map.
//! The stock-file tests need client data.

use std::cell::RefCell;

use benilla_ui::script::{UiScript, WorldMapContinentView, WorldMapZoneView};

use super::*;

/// The stock files the map needs, in `FrameXML.toc` order (as benilla's own world-map tests load
/// them): its OnLoad initializes its dropdowns, the update walks the party and raid sizes.
const STOCK: &[&str] = &[
    r"Interface\FrameXML\GlobalStrings.lua",
    r"Interface\FrameXML\Fonts.xml",
    r"Interface\FrameXML\BasicControls.xml",
    r"Interface\FrameXML\LocaleProperties.lua",
    r"Interface\FrameXML\UIParent.xml",
    r"Interface\FrameXML\MoneyFrame.lua",
    r"Interface\FrameXML\MoneyFrame.xml",
    r"Interface\FrameXML\GameTooltip.xml",
    r"Interface\FrameXML\UIDropDownMenu.xml",
    r"Interface\FrameXML\UIPanelTemplates.lua",
    r"Interface\FrameXML\UIPanelTemplates.xml",
    r"Interface\FrameXML\StaticPopup.xml",
    r"Interface\FrameXML\PartyMemberFrame.lua",
    r"Interface\FrameXML\RaidFrame.lua",
    r"Interface\FrameXML\WorldMapFrame.xml",
];

/// A VM with the stock map loaded off the player's chain; `None` without an install.
fn stock_vm(data: &std::path::Path) -> UiScript {
    let chain = RefCell::new(benilla_formats::open_chain(data).expect("the patch chain"));
    let read = |p: &str| chain.borrow_mut().read_file(&p.replace('/', "\\")).ok();
    let mut s = UiScript::new().expect("a VM");
    s.set_screen_size(1024.0, 768.0);
    for file in STOCK {
        let path = file.replace('\\', "/");
        let bytes = read(&path).unwrap_or_else(|| panic!("{file}: not in the chain"));
        if path.ends_with(".lua") {
            s.run_chunk_named(&bytes, &format!("@{file}"))
                .unwrap_or_else(|e| panic!("{file}: {e}"));
            continue;
        }
        let doc = benilla_ui::framexml::parse(&benilla_ui::source::decode(&bytes))
            .unwrap_or_else(|e| panic!("{file}: {e}"));
        let report = benilla_ui::loader::load_in(&s, &doc, &path, &read);
        assert!(report.errors.is_empty(), "{file}: {:?}", report.errors);
    }
    assert!(s.errors().is_empty(), "stock load: {:?}", s.errors());
    // `CloseAllWindows` walks the bags, which this kit does not load.
    // The map's OnShow and OnHide light the menu bar's button; UIParent's OnUpdate runs the chat
    // frames' fades. Neither file is in this kit.
    s.run(
        "NUM_CONTAINER_FRAMES = NUM_CONTAINER_FRAMES or 0 \
         UpdateMicroButtons = UpdateMicroButtons or function() end \
         FCF_OnUpdate = FCF_OnUpdate or function() end \n         UnitPopup_OnUpdate = UnitPopup_OnUpdate or function() end",
    )
    .unwrap();
    // The player the arrow's tooltip names.
    s.set_unit(
        "player",
        Some(benilla_ui::script::UnitState {
            exists: true,
            name: Some("Probe".into()),
            level: 60,
            ..Default::default()
        }),
    );
    s.resolve();
    s
}

/// The stock map with the reshape installed, as the first world frame installs it.
fn harness(data: &std::path::Path) -> UiScript {
    let s = stock_vm(data);
    assert!(install(&s), "the first check installs");
    assert!(!install(&s), "and only once a VM");
    assert!(s.errors().is_empty(), "install: {:?}", s.errors());
    s
}

fn eval<T: mlua::FromLuaMulti>(s: &UiScript, chunk: &str) -> T {
    s.eval::<T>(chunk)
        .unwrap_or_else(|e| panic!("`{chunk}`: {e}"))
}

fn run(s: &mut UiScript, chunk: &str) {
    s.run(chunk).unwrap_or_else(|e| panic!("`{chunk}`: {e}"));
    s.resolve();
}

fn shown(s: &UiScript, name: &str) -> bool {
    eval(s, &format!("return {name}:IsVisible() and true or false"))
}

/// The map's top-left in screen units.
fn top_left(s: &UiScript) -> (f64, f64) {
    eval(
        s,
        "local e = WorldMapFrame:GetEffectiveScale() \
         return WorldMapFrame:GetLeft() * e, WorldMapFrame:GetTop() * e",
    )
}

fn close(a: (f64, f64), b: (f64, f64), what: &str) {
    assert!(
        (a.0 - b.0).abs() < 0.5 && (a.1 - b.1).abs() < 0.5,
        "{what}: {a:?} vs {b:?}"
    );
}

/// Move the cursor to a share of `WorldMapButton` (u east, v south), in screen units.
fn point_at(s: &mut UiScript, u: f64, v: f64) {
    let (x, y): (f64, f64) = eval(
        s,
        &format!(
            "local b = WorldMapButton local e = b:GetEffectiveScale() \
             return (b:GetLeft() + b:GetWidth() * {u}) * e, (b:GetTop() - b:GetHeight() * {v}) * e"
        ),
    );
    s.mouse_move(x as f32, y as f32);
}

fn zone(name: &str, area_id: u32) -> WorldMapZoneView {
    WorldMapZoneView {
        name: name.into(),
        area_id,
        map_file: name.replace(' ', ""),
        loc_rect: (1000.0, -1000.0, 1000.0, -1000.0),
        overlays: Vec::new(),
    }
}

/// Two continents side by side on the world sheet, each with two zones; every cell of a
/// continent's area grid is its first zone, so a click anywhere on it zooms there.
fn catalog() -> Vec<WorldMapContinentView> {
    let cont = |name: &str, world_rect, zones| WorldMapContinentView {
        name: name.into(),
        map_file: name.replace(' ', ""),
        world_rect,
        loc_rect: (1000.0, -1000.0, 1000.0, -1000.0),
        zone_grid: vec![1; 128 * 128],
        zones,
    };
    vec![
        cont(
            "Kalimdor",
            (0.0, 0.0, 0.5, 1.0),
            vec![zone("Ashenvale", 331), zone("Durotar", 14)],
        ),
        cont(
            "Eastern Kingdoms",
            (0.5, 0.0, 1.0, 1.0),
            vec![zone("Elwynn Forest", 12), zone("Westfall", 40)],
        ),
    ]
}

fn selection(s: &UiScript) -> (i64, i64) {
    eval(s, "return GetCurrentMapContinent(), GetCurrentMapZone()")
}

#[test]
fn m_opens_a_window_and_leaves_the_game_up() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data);
    assert!(!shown(&s, "WorldMapFrame"));
    run(&mut s, "ToggleWorldMap()");
    assert!(shown(&s, "WorldMapFrame"), "M opens it");
    assert!(shown(&s, "UIParent"), "the interface stays up");
    assert!(
        eval::<bool>(&s, "return GetFullScreenFrame() == nil"),
        "no full-screen panel"
    );
    assert!(!shown(&s, "BlackoutWorld"), "the world is not blacked out");
    assert!(
        !eval::<bool>(
            &s,
            "return WorldMapFrame:IsKeyboardEnabled() and true or false"
        ),
        "the keyboard stays the game's"
    );
    assert_eq!(
        eval::<String>(&s, "return WorldMapFrame:GetFrameStrata()"),
        "MEDIUM"
    );
    let (w, h, scale): (f64, f64, f64) = eval(
        &s,
        "return WorldMapFrame:GetWidth(), WorldMapFrame:GetHeight(), WorldMapFrame:GetScale()",
    );
    assert_eq!((w, h), (1024.0, 768.0), "the stock map's size, scaled");
    assert!((scale - 0.65).abs() < 1e-6, "the default size: {scale}");
    assert!(
        shown(&s, "BenillaMw2WorldMapBar"),
        "our pieces show on the window"
    );
    // The default seat: on the right of the screen, wholly on it.
    let (l, t) = top_left(&s);
    let (sw, sh): (f64, f64) = eval(&s, "return GetScreenWidth(), GetScreenHeight()");
    assert!(
        l > sw / 2.0 - 1024.0 * 0.65 && l + 1024.0 * 0.65 <= sw,
        "{l}"
    );
    assert!(t <= sh && t - 768.0 * 0.65 >= 0.0, "{t}");
    // Esc closes it, as a special frame.
    assert!(eval::<bool>(
        &s,
        "for _, n in ipairs(UISpecialFrames) do if n == \"WorldMapFrame\" then return true end end return false"
    ));
    run(&mut s, "CloseWindows()");
    assert!(!shown(&s, "WorldMapFrame"), "Esc's CloseWindows closes it");
    run(&mut s, "ToggleWorldMap()");
    run(&mut s, "ToggleWorldMap()");
    assert!(!shown(&s, "WorldMapFrame"), "M again closes it");
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

#[test]
fn a_dragged_map_keeps_its_place_and_reset_puts_it_back() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data);
    run(&mut s, "ToggleWorldMap()");
    let home = top_left(&s);

    // A drag by the frame: the first move starts it (OnDragStart, StartMoving), the pump follows
    // the cursor from there, and the stop saves.
    let (x0, y0) = (home.0 + 300.0, home.1 - 10.0); // the top band, above the map art
    s.mouse_move(x0 as f32, y0 as f32);
    s.mouse_button(x0 as f32, y0 as f32, "LeftButton", true);
    s.mouse_move((x0 - 10.0) as f32, (y0 - 10.0) as f32);
    s.mouse_move((x0 - 210.0) as f32, (y0 - 110.0) as f32);
    s.mouse_button(
        (x0 - 210.0) as f32,
        (y0 - 110.0) as f32,
        "LeftButton",
        false,
    );
    s.resolve();
    let moved = top_left(&s);
    close(
        moved,
        (home.0 - 200.0, home.1 - 100.0),
        "the map follows the drag",
    );
    let saved: String = eval(&s, "return GetCVar(\"womwMapPos\")");
    assert!(!saved.is_empty(), "the place is saved");

    // Closed and opened: still there; and through a new size, the top-left holds.
    run(&mut s, "ToggleWorldMap() ToggleWorldMap()");
    close(top_left(&s), moved, "reopened in place");
    run(&mut s, "BenillaMw2WorldMap.SetScale(0.5)");
    close(top_left(&s), moved, "sized about its top-left");
    assert_eq!(
        eval::<String>(&s, "return GetCVar(\"womwMapScale\")"),
        "0.5"
    );

    // Locked: a drag does nothing.
    run(&mut s, "BenillaMw2WorldMap.Toggle(\"womwMapLock\")");
    let (x1, y1) = (moved.0 + 300.0, moved.1 - 10.0);
    s.mouse_button(x1 as f32, y1 as f32, "LeftButton", true);
    s.mouse_move((x1 + 150.0) as f32, (y1 - 50.0) as f32);
    s.mouse_button((x1 + 150.0) as f32, (y1 - 50.0) as f32, "LeftButton", false);
    s.resolve();
    close(top_left(&s), moved, "locked in place");
    run(&mut s, "BenillaMw2WorldMap.Toggle(\"womwMapLock\")");

    // The next session: a new VM reads the CVars back.
    let pos: String = eval(&s, "return GetCVar(\"womwMapPos\")");
    let mut next = stock_vm(&data);
    run(
        &mut next,
        &format!(
            "RegisterCVar(\"womwMapPos\", \"\") SetCVar(\"womwMapPos\", \"{pos}\") \
             RegisterCVar(\"womwMapScale\", \"0.65\") SetCVar(\"womwMapScale\", \"0.5\")"
        ),
    );
    assert!(install(&next));
    run(&mut next, "ToggleWorldMap()");
    close(top_left(&next), moved, "the next session opens it there");

    // Reset: home, at the default size, the place forgotten.
    run(&mut next, "BenillaMw2WorldMapResetButton:Click()");
    close(top_left(&next), home, "reset puts it home");
    assert_eq!(eval::<String>(&next, "return GetCVar(\"womwMapPos\")"), "");
    assert!((eval::<f64>(&next, "return WorldMapFrame:GetScale()") - 0.65).abs() < 1e-6);
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    assert!(
        next.errors().is_empty(),
        "script errors: {:?}",
        next.errors()
    );
}

#[test]
fn the_grip_and_the_wheel_size_the_map() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data);
    run(&mut s, "ToggleWorldMap()");
    let start = top_left(&s);
    // The grip to a cursor 512 x 384 screen units from the top-left: half the stock map.
    run(&mut s, "BenillaMw2WorldMap.BeginSizing()");
    s.mouse_move((start.0 + 512.0) as f32, (start.1 - 384.0) as f32);
    run(
        &mut s,
        "BenillaMw2WorldMap.OnUpdate(0.01) BenillaMw2WorldMap.EndSizing()",
    );
    assert!((eval::<f64>(&s, "return WorldMapFrame:GetScale()") - 0.5).abs() < 0.01);
    close(top_left(&s), start, "the grip holds the top-left");
    // Clamped both ways.
    run(&mut s, "BenillaMw2WorldMap.SetScale(5)");
    assert_eq!(eval::<f64>(&s, "return WorldMapFrame:GetScale()"), 1.0);
    run(&mut s, "BenillaMw2WorldMap.SetScale(0.01)");
    assert!((eval::<f64>(&s, "return WorldMapFrame:GetScale()") - 0.3).abs() < 1e-6);
    // Ctrl + wheel.
    run(
        &mut s,
        "IsControlKeyDown = function() return 1 end BenillaMw2WorldMap.OnWheel(1)",
    );
    assert!((eval::<f64>(&s, "return WorldMapFrame:GetScale()") - 0.35).abs() < 1e-6);
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

/// The player's map position and the cursor's, as percent, along the bottom edge.
#[test]
fn the_coordinates_show_the_player_and_the_cursor() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data);
    run(&mut s, "ToggleWorldMap()");
    s.set_world_map_feed(
        None,
        Some((0.438, 0.658)),
        0.0,
        None,
        Vec::new(),
        Vec::new(),
    );
    point_at(&mut s, 0.25, 0.75);
    run(&mut s, "BenillaMw2WorldMap.UpdateCoords()");
    let text = |s: &UiScript| -> (String, String) {
        eval(
            s,
            "return BenillaMw2WorldMapPlayerCoords:GetText() or \"\", \
             BenillaMw2WorldMapCursorCoords:GetText() or \"\"",
        )
    };
    assert_eq!(
        text(&s),
        ("Player: 43.8, 65.8".into(), "Cursor: 25.0, 75.0".into())
    );
    // The cursor law against the stock one at the corners and inside.
    for (u, v) in [(0.01, 0.01), (0.99, 0.99), (0.5, 0.5), (0.123, 0.877)] {
        point_at(&mut s, u, v);
        let (cu, cv): (f64, f64) = eval(&s, "return BenillaMw2WorldMap.CursorUV()");
        assert!(
            (cu - u).abs() < 1e-3 && (cv - v).abs() < 1e-3,
            "({u},{v}) read ({cu},{cv})"
        );
    }
    // Off the map: no cursor line; the player off the displayed map: dashes.
    let (l, t) = top_left(&s);
    s.mouse_move((l - 50.0) as f32, (t + 50.0) as f32);
    s.set_world_map_feed(None, None, 0.0, None, Vec::new(), Vec::new());
    run(&mut s, "BenillaMw2WorldMap.UpdateCoords()");
    assert_eq!(text(&s), ("Player: --".into(), String::new()));
    // Mouselook (the gun has the mouse): no cursor line even over the map.
    point_at(&mut s, 0.5, 0.5);
    run(&mut s, &state_lua(false, false));
    run(&mut s, "BenillaMw2WorldMap.UpdateCoords()");
    assert_eq!(text(&s).1, "");
    // Switched off.
    run(&mut s, "BenillaMw2WorldMap.Toggle(\"womwMapCoords\")");
    assert_eq!(text(&s), (String::new(), String::new()));
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

/// World to map and back against known places, on the player's own `WorldMapArea.dbc`: Innkeeper
/// Farley in Goldshire's inn (world -9462.66, 16.19) is at 43.8, 65.8 on the Elwynn Forest map.
#[test]
fn world_and_map_coordinates_convert_at_known_points() {
    use benilla_world::map_proj::{ZoneRect, zone_uv, zone_world};
    let data = benilla_formats::wow_data_or_skip!();
    let mut chain = benilla_formats::open_chain(&data).expect("the patch chain");
    let wma = benilla_formats::load_world_map_area_catalog(&mut chain).expect("WorldMapArea");
    let rect = |name: &str| {
        wma.iter()
            .find(|(_, a)| a.name == name)
            .map(|(_, a)| ZoneRect {
                left: a.loc_left,
                right: a.loc_right,
                top: a.loc_top,
                bottom: a.loc_bottom,
            })
            .unwrap_or_else(|| panic!("{name} in WorldMapArea"))
    };
    let elwynn = rect("Elwynn");
    let (u, v) = zone_uv(elwynn, -9462.66, 16.19);
    assert!(
        (u * 100.0 - 43.8).abs() < 0.2 && (v * 100.0 - 65.8).abs() < 0.2,
        "Farley at ({:.2}, {:.2})",
        u * 100.0,
        v * 100.0
    );
    // Back to the world, one axis per call as the reference's callers use it.
    let (_, wy) = zone_world(elwynn, u);
    let (wx, _) = zone_world(elwynn, v);
    assert!(
        (wx + 9462.66).abs() < 0.05 && (wy - 16.19).abs() < 0.05,
        "({wx}, {wy})"
    );
    // The map's corners are the rect's corners; a point off the zone is the off-map (0, 0).
    assert_eq!(zone_world(elwynn, 0.0), (elwynn.top, elwynn.left));
    assert_eq!(zone_uv(elwynn, elwynn.top + 50.0, 16.19), (0.0, 0.0));

    // The map window prints that position as players trade it.
    let s = stock_vm(&data);
    install(&s);
    assert_eq!(
        eval::<String>(
            &s,
            &format!("return BenillaMw2WorldMap.FormatCoords({u}, {v})")
        ),
        "43.8, 65.8"
    );
}

/// The stock navigation inside the window: the dropdown's zoom, a click or the wheel into the
/// zone under the cursor, a right click or the wheel back out; and the map follows the player.
#[test]
fn zones_navigate_by_dropdown_click_and_wheel() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data);
    s.set_world_map_catalog(catalog());
    s.set_world_map_feed(
        Some((2, 1)),
        Some((0.5, 0.5)),
        0.0,
        None,
        Vec::new(),
        Vec::new(),
    );
    run(&mut s, "ToggleWorldMap()");
    assert_eq!(selection(&s), (2, 1), "it opens on the player's zone");

    // Right click: out to the continent, then the world.
    run(
        &mut s,
        "WorldMapButton_OnClick(\"RightButton\", WorldMapButton)",
    );
    assert_eq!(selection(&s), (2, 0));
    run(&mut s, "BenillaMw2WorldMap.OnWheel(-1)");
    assert_eq!(selection(&s), (0, 0), "the wheel zooms out too");
    // The wheel in at the cursor: the left half of the world sheet is the first continent.
    point_at(&mut s, 0.25, 0.5);
    run(&mut s, "BenillaMw2WorldMap.OnWheel(1)");
    assert_eq!(selection(&s), (1, 0));
    // A click on the continent zooms into the zone under it.
    point_at(&mut s, 0.5, 0.5);
    run(
        &mut s,
        "WorldMapButton_OnClick(\"LeftButton\", WorldMapButton)",
    );
    assert_eq!(selection(&s), (1, 1));
    // The zone dropdown's pick, as WorldMapZoneButton_OnClick makes it.
    run(&mut s, "SetMapZoom(GetCurrentMapContinent(), 2)");
    assert_eq!(selection(&s), (1, 2));
    assert_eq!(eval::<String>(&s, "return (GetMapInfo())"), "Durotar");

    // Following: shown on the player's zone, the map goes with them into the next one...
    run(&mut s, "ToggleWorldMap() ToggleWorldMap()");
    assert_eq!(selection(&s), (2, 1));
    s.set_world_map_feed(
        Some((2, 2)),
        Some((0.5, 0.5)),
        0.0,
        None,
        Vec::new(),
        Vec::new(),
    );
    s.fire_event("ZONE_CHANGED_NEW_AREA", Vec::new());
    s.resolve();
    assert_eq!(selection(&s), (2, 2), "followed into Westfall");
    // ...but not away from a map the player chose.
    run(&mut s, "SetMapZoom(1, 1)");
    s.set_world_map_feed(
        Some((2, 1)),
        Some((0.5, 0.5)),
        0.0,
        None,
        Vec::new(),
        Vec::new(),
    );
    s.fire_event("ZONE_CHANGED_NEW_AREA", Vec::new());
    s.resolve();
    assert_eq!(selection(&s), (1, 1), "the chosen map stays");
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

#[test]
fn the_map_fades_while_moving() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data);
    run(&mut s, "ToggleWorldMap()");
    let alpha = |s: &UiScript| eval::<f64>(s, "return WorldMapFrame:GetAlpha()");
    let (l, t) = top_left(&s);
    s.mouse_move((l - 40.0).max(0.0) as f32, (t + 40.0) as f32); // off the window
    let step = |s: &mut UiScript, secs: f64| {
        run(s, &format!("BenillaMw2WorldMap.OnUpdate({secs})"));
    };
    assert_eq!(alpha(&s), 1.0, "standing: full");
    run(&mut s, &state_lua(true, true));
    step(&mut s, 0.1);
    let mid = alpha(&s);
    assert!(mid < 1.0 && mid > 0.35, "fading: {mid}");
    step(&mut s, 1.0);
    assert!((alpha(&s) - 0.35).abs() < 1e-6, "faded to the set opacity");
    // Pointing at it brings it back while moving.
    point_at(&mut s, 0.5, 0.5);
    step(&mut s, 1.0);
    assert_eq!(alpha(&s), 1.0, "under the cursor");
    // Stopping brings it back.
    s.mouse_move((l - 40.0).max(0.0) as f32, (t + 40.0) as f32);
    step(&mut s, 1.0);
    assert!(alpha(&s) < 1.0);
    run(&mut s, &state_lua(false, true));
    step(&mut s, 1.0);
    assert_eq!(alpha(&s), 1.0, "stopped");
    // The opacity is a setting; the fade can be switched off.
    run(
        &mut s,
        "SetCVar(\"womwMapFadeAlpha\", \"0.6\") BenillaMw2WorldMap.Apply()",
    );
    run(&mut s, &state_lua(true, true));
    step(&mut s, 2.0);
    assert!((alpha(&s) - 0.6).abs() < 1e-6);
    run(&mut s, "BenillaMw2WorldMap.Toggle(\"womwMapFade\")");
    step(&mut s, 2.0);
    assert_eq!(alpha(&s), 1.0, "no fade with it off");
    // The driver's own OnUpdate, a child of the shown map, drives it.
    run(&mut s, "BenillaMw2WorldMap.Toggle(\"womwMapFade\")");
    assert!(shown(&s, "BenillaMw2WorldMapDriver"));
    run(
        &mut s,
        "this = BenillaMw2WorldMapDriver arg1 = 0.5 this:GetScript(\"OnUpdate\")() this = nil",
    );
    assert!(alpha(&s) < 1.0, "the map's own OnUpdate fades it");
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

#[test]
fn the_group_icons_switch_off() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = harness(&data);
    run(&mut s, "ToggleWorldMap()");
    let blips = |s: &UiScript| -> (f64, f64) {
        eval(
            s,
            "return WorldMapParty1:GetAlpha(), WorldMapRaid40:GetAlpha()",
        )
    };
    assert_eq!(blips(&s), (1.0, 1.0));
    run(&mut s, "BenillaMw2WorldMap.Toggle(\"womwMapGroup\")");
    assert_eq!(blips(&s), (0.0, 0.0));
    run(&mut s, "BenillaMw2WorldMap.Toggle(\"womwMapGroup\")");
    assert_eq!(blips(&s), (1.0, 1.0));
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

/// Stand-ins for the options window's Interface page, as micro_menu's tests seat them.
const OPTIONS_PAGE: &str = r#"
    local page = CreateFrame("Frame", "BenillaOptionsFrameContainerBodyInterface", UIParent)
    CreateFrame("Frame", "BenillaOptionsFrameContainerBodyInterfaceRowNewbieTips", page)
    BENILLA_OPTIONS_PAGE_ROWS = { Interface = { "RowNewbieTips" } }
    function BenillaOptionsStrFuncRow_OnLoad(row, get, set, default, label, tip)
      row.strGetFunc, row.strSetFunc, row.strDefaultFunc = get, set, default
      row.label, row.tip = label, tip
    end
"#;

/// The Interface page's Full-Screen World Map row puts the stock map back exactly: the panel
/// manager's full-screen slot, the interface hidden, the keyboard the map's.
#[test]
fn an_interface_option_brings_back_the_stock_full_screen_map() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = stock_vm(&data);
    run(&mut s, OPTIONS_PAGE);
    assert!(install(&s));
    let rows: Vec<String> = eval(&s, "return BENILLA_OPTIONS_PAGE_ROWS.Interface");
    assert_eq!(
        rows,
        [
            "RowNewbieTips",
            "RowWorldMapFullScreen",
            "RowWorldMapFade",
            "RowWorldMapFadeAlpha",
            "RowWorldMapScale",
            "RowWorldMapCoords",
            "RowWorldMapGroup",
        ]
    );
    let row = "BenillaOptionsFrameContainerBodyInterfaceRowWorldMapFullScreen";
    assert_eq!(
        eval::<(String, String, String)>(
            &s,
            &format!("return {row}.label, {row}.strDefaultFunc(), {row}.strGetFunc()")
        ),
        ("Full-Screen World Map".into(), "0".into(), "0".into())
    );
    run(&mut s, "ToggleWorldMap()");
    assert!(eval::<bool>(&s, "return GetFullScreenFrame() == nil"));
    run(&mut s, "ToggleWorldMap()");

    run(&mut s, &format!("{row}.strSetFunc(\"1\")"));
    run(&mut s, "ToggleWorldMap()");
    assert!(shown(&s, "WorldMapFrame"));
    assert!(
        eval::<bool>(&s, "return GetFullScreenFrame() == WorldMapFrame"),
        "the full-screen panel"
    );
    assert!(!shown(&s, "UIParent"), "the interface is hidden behind it");
    assert!(shown(&s, "BlackoutWorld"));
    assert!(!shown(&s, "BenillaMw2WorldMapBar"), "none of ours on it");
    assert!(eval::<bool>(
        &s,
        "return WorldMapFrame:IsKeyboardEnabled() and true or false"
    ));
    assert_eq!(
        eval::<String>(&s, "return WorldMapFrame:GetFrameStrata()"),
        "FULLSCREEN"
    );
    let (w, h, sw, sh): (f64, f64, f64, f64) = eval(
        &s,
        "local e = WorldMapFrame:GetEffectiveScale() \
         return WorldMapFrame:GetWidth() * e, WorldMapFrame:GetHeight() * e, \
         UIParent:GetWidth() * UIParent:GetEffectiveScale(), UIParent:GetHeight() * UIParent:GetEffectiveScale()",
    );
    close((w, h), (sw, sh), "the whole screen");
    // The gun gives the cursor back for it, as for any full-screen panel.
    assert!(crate::mode::cursor_panel_open(&s));
    run(&mut s, "ToggleWorldMap()");
    assert!(!shown(&s, "WorldMapFrame"));
    assert!(shown(&s, "UIParent"), "the interface is back");

    // And back to the window.
    run(&mut s, &format!("{row}.strSetFunc(\"0\")"));
    run(&mut s, "ToggleWorldMap()");
    assert!(eval::<bool>(&s, "return GetFullScreenFrame() == nil"));
    assert!(shown(&s, "UIParent") && shown(&s, "BenillaMw2WorldMapBar"));
    // The window does not take the mouse from the gun.
    assert!(!crate::mode::cursor_panel_open(&s));
    // The size row.
    let scale = "BenillaOptionsFrameContainerBodyInterfaceRowWorldMapScale";
    run(&mut s, &format!("{scale}.strSetFunc(\"0.9\")"));
    assert!((eval::<f64>(&s, "return WorldMapFrame:GetScale()") - 0.9).abs() < 1e-6);
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

/// A session that starts on the stock map leaves the stock frame as it loaded.
#[test]
fn the_stock_setting_leaves_the_stock_frame_alone() {
    let data = benilla_formats::wow_data_or_skip!();
    let mut s = stock_vm(&data);
    run(
        &mut s,
        "RegisterCVar(\"womwMapFullScreen\", \"0\") SetCVar(\"womwMapFullScreen\", \"1\")",
    );
    assert!(install(&s));
    assert_eq!(
        eval::<String>(&s, "return WorldMapFrame:GetFrameStrata()"),
        "FULLSCREEN"
    );
    assert!(eval::<bool>(
        &s,
        "return UIPanelWindows[\"WorldMapFrame\"] ~= nil"
    ));
    run(&mut s, "ToggleWorldMap()");
    assert!(eval::<bool>(
        &s,
        "return GetFullScreenFrame() == WorldMapFrame"
    ));
    assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
}

#[test]
fn moving_is_ground_speed_with_a_short_stop_delay() {
    let mut w = MoveWatch::default();
    let dt = 1.0 / 60.0;
    assert!(!w.update(Vec2::ZERO, dt), "the first sample is still");
    assert!(w.update(Vec2::new(0.12, 0.0), dt), "7 yd/s runs");
    // A stop: still moving for the delay, then stopped.
    let mut frames = 0;
    while w.update(Vec2::new(0.12, 0.0), dt) {
        frames += 1;
        assert!(frames < 60, "never stopped");
    }
    assert!(
        (frames as f32 * dt - STOP_DELAY).abs() < 2.0 * dt,
        "{frames} frames"
    );
    // A slow drift (under a yard a second) is standing.
    let mut p = Vec2::new(0.12, 0.0);
    for _ in 0..60 {
        p.x += 0.5 * dt;
        assert!(!w.update(p, dt));
    }
    assert_eq!(
        state_lua(true, false),
        "if BenillaMw2WorldMap then BenillaMw2WorldMap.SetState(1, nil) end"
    );
}
