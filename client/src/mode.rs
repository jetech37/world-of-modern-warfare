//! The layer's state and the FPS camera: in the world (the layer is always on), the camera is held at
//! first person (`CameraZoomIn`, where benilla's self fade hides the body) and mouselook is held
//! on with no button (`MouselookStart`, the TurnOrAction channel the right button drives), so the
//! mouse aims and turns the character. Both are the stock 1.12 camera verbs, re-asserted every
//! frame, so nothing is persisted and a right-button release or a loading screen cannot end them.

use benilla_app::Objects;
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::config::Loadout;
use crate::input::Mw2Input;
use crate::{Mw2Set, lua};

/// Where the layer stands this frame.
#[derive(Resource, Debug, Clone)]
pub struct FpsState {
    /// A character is in the world.
    pub in_world: bool,
    /// The free cursor locked out (Ctrl+Alt, again to aim): the mouse is the interface's.
    pub cursor_locked: bool,
    /// The layer is on and in the world: the HUD shows and the camera is first person.
    pub active: bool,
    /// The layer has the mouse: [`FpsState::active`], no chat box focused, no window that needs
    /// the cursor open, Left Alt up and the cursor not locked out.
    pub owns_input: bool,
    /// A layer window (Create-a-Class) is open: the cursor is out and the gun lowered.
    pub panel: bool,
    /// The chat box has the keyboard.
    pub chat_focus: bool,
    /// A layer window takes typing (a class's name): keys go to it, not WoW's bindings.
    pub typing: bool,
    /// Our own player's guid, once in the world.
    pub self_guid: Option<u64>,
    /// Build mode (crate::build): the gun is away and the hammer out.
    pub building: bool,
    /// The killcam has the view (crate::killcam).
    pub killcam: bool,
    /// The build menu is open (crate::build::menu): the cursor is out.
    pub build_menu: bool,
    /// A killstreak turret is being set down (crate::lore_streaks): the buttons place it.
    pub placing: bool,
    /// The Goblin Rocket is being steered (crate::lore_streaks): the view is the rocket's, the
    /// mouse steers it and the body stands still.
    pub piloting: bool,
    /// The stock Key Bindings window is open: every key is its to bind (crate::input takes none).
    pub key_bindings: bool,
    /// The companion duel window is open (crate::companion_battle): 1-0 and E are its own, taken
    /// from WoW's bindings and the layer's hotkeys alike.
    pub duel_keys: bool,
    /// We hold a ship's helm (crate::ships): W, S, A, D, Space and X steer it, Esc leaves it.
    pub helm: bool,
}

impl FpsState {
    /// The view is away from the body (the killcam, the rocket): no gun, no first-person body.
    pub fn view_away(&self) -> bool {
        self.killcam || self.piloting
    }
}

impl Default for FpsState {
    fn default() -> Self {
        Self {
            in_world: false,
            cursor_locked: false,
            active: false,
            owns_input: false,
            panel: false,
            chat_focus: false,
            duel_keys: false,
            typing: false,
            self_guid: None,
            building: false,
            killcam: false,
            build_menu: false,
            placing: false,
            piloting: false,
            key_bindings: false,
            helm: false,
        }
    }
}

pub struct ModePlugin;

impl Plugin for ModePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<FpsState>()
            .init_resource::<Loadout>()
            .add_systems(
                Update,
                update_mode
                    .in_set(Mw2Set::Mode)
                    .before(crate::build::BuildInput),
            );
    }
}

/// The stock windows that need the cursor: while one is shown the gun is lowered and the mouse
/// goes back to the interface, as holding Left Alt does.
const CURSOR_PANELS: &[&str] = &[
    "GameMenuFrame",
    "StaticPopup1",
    "MerchantFrame",
    "LootFrame",
    "GossipFrame",
    "QuestFrame",
    "TaxiFrame",
    "TradeFrame",
    "BankFrame",
    "AuctionFrame",
    "MailFrame",
    "ClassTrainerFrame",
    "TradeSkillFrame",
    "CraftFrame",
    "PetStableFrame",
    "CharacterFrame",
    "SpellBookFrame",
    "QuestLogFrame",
    "FriendsFrame",
    // Not `WorldMapFrame`: as the layer's map window (crate::world_map) it leaves the gun the
    // mouse; as the stock full-screen map it is the panel manager's, which [`UI_PANEL_LUA`] reads.
    "ContainerFrame1",
    "TalentFrame",
    "KeyBindingFrame",
    "HelpFrame",
    "MacroFrame",
    "StaticPopup2",
    "StaticPopup3",
    "StaticPopup4",
    "ColorPickerFrame",
    // benilla's options window, which the Escape menu's Options rung opens.
    "BenillaOptionsFrame",
    // The Escape menu's Bugs / Feedback dialog (crate::feedback).
    "BenillaMw2FeedbackFrame",
    // The lock, keypad and portal dialogs (crate::build::locks).
    "BenillaMw2LockFrame",
    "BenillaMw2KeypadFrame",
    "BenillaMw2PortalFrame",
];

/// Any stock window the panel manager placed (`UIParent.lua`'s left, centre, double-wide and
/// full-screen slots), whatever its name: an addon's or one this list lacks still frees the
/// cursor, as every menu does.
pub const UI_PANEL_LUA: &str = "local f = (GetLeftFrame and GetLeftFrame()) \
     or (GetCenterFrame and GetCenterFrame()) or (GetDoublewideFrame and GetDoublewideFrame()) \
     or (GetFullScreenFrame and GetFullScreenFrame()) \
     return f ~= nil and f:IsVisible() and true or false";

/// Whether a window that needs the cursor is open: one of [`CURSOR_PANELS`], or any the panel
/// manager placed.
pub fn cursor_panel_open(script: &UiScript) -> bool {
    CURSOR_PANELS.iter().any(|n| script.frame_visible(n))
        || script.eval::<bool>(UI_PANEL_LUA).unwrap_or(false)
}

/// Seconds between scans for [`CURSOR_PANELS`]; a scan walks the frame arena per name.
const PANEL_SCAN_SECS: f32 = 0.1;

/// Yards to zoom in per frame: more than the 1.12 maximum distance, so one call reaches zero.
const ZOOM_IN: &str = "CameraZoomIn(50)";

pub(crate) fn update_mode(
    third: Res<crate::thirdperson::ThirdPerson>,
    time: Res<Time>,
    mut panel_scan: Local<(f32, bool, bool)>,
    mut state: ResMut<FpsState>,
    mut loadout: ResMut<Loadout>,
    input: Res<Mw2Input>,
    objects: Objects,
    script: Option<NonSend<UiScript>>,
) {
    if input.cursor_lock {
        state.cursor_locked = !state.cursor_locked;
        info!(
            "benilla-mw2: cursor {}",
            if state.cursor_locked { "locked out" } else { "back to aiming" }
        );
    }

    let me = objects.player();
    state.self_guid = me.and_then(|e| objects.iter().find(|&(_, en, _)| en == e).map(|(g, ..)| g));
    let in_world = me.is_some();

    // The glue screens run a VM of their own; only the world's is ours to drive.
    let script = match script {
        Some(script) if in_world => script,
        script => {
            if state.owns_input {
                // The frame the player leaves: stop our mouselook on whatever VM is still up, so a
                // world VM that outlives the player ends the session itself. benilla frees the
                // cursor at `OnExit(InWorld)` for a world that is already gone.
                if let Some(script) = script {
                    lua::run(&script, "MouselookStop()");
                }
                state.owns_input = false;
            }
            state.in_world = in_world;
            state.active = false;
            state.key_bindings = false;
            state.cursor_locked = false;
            if !in_world {
                loadout.class = None;
            }
            return;
        }
    };

    if loadout.class.is_none() {
        let class: Option<(Option<String>, Option<String>)> =
            lua::eval(&script, "return UnitClass(\"player\")");
        if let Some((_, Some(token))) = class {
            *loadout = Loadout::for_class(&token);
            info!(
                "benilla-mw2: {token} loadout, {} ({:?} {:?})",
                loadout.weapon, loadout.fire_mode, loadout.fire_spell
            );
        }
    }

    let t = time.elapsed_secs();
    if t >= panel_scan.0 {
        *panel_scan = (
            t + PANEL_SCAN_SECS,
            cursor_panel_open(&script),
            script.frame_visible("KeyBindingFrame"),
        );
    }
    let panel_open = panel_scan.1;
    state.key_bindings = panel_scan.2;
    state.chat_focus = script.has_keyboard_focus();
    let owns = !state.chat_focus
        && !input.cursor
        && !state.cursor_locked
        && !panel_open
        && !state.panel
        && !state.build_menu;

    // Third person (crate::thirdperson) sets its own distance once; first person is held.
    let zoom = if third.on { "" } else { ZOOM_IN };
    if owns {
        lua::run(&script, &format!("MouselookStart() {zoom}"));
    } else if state.owns_input {
        lua::run(&script, "MouselookStop()");
    }
    if !owns && !third.on {
        // The cursor is out but the gun stays up: keep first person.
        lua::run(&script, ZOOM_IN);
    }

    state.in_world = true;
    state.active = true;
    state.owns_input = owns;
}

#[cfg(test)]
mod tests {
    use super::*;

    fn frame(s: &UiScript, name: &str) {
        s.run(&format!(
            "local f = CreateFrame(\"Frame\", \"{name}\", UIParent) \
             f:SetWidth(100) f:SetHeight(100) \
             f:SetPoint(\"CENTER\", UIParent, \"CENTER\", 0, 0) f:Hide()"
        ))
        .unwrap();
    }

    /// Every menu keeps the cursor free: the Escape menu, the options window its Options rung
    /// opens (aiming came back there), the layer's dialogs, and any window the stock panel
    /// manager placed.
    #[test]
    fn every_menu_keeps_the_cursor_free() {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        for name in [
            "GameMenuFrame",
            "BenillaOptionsFrame",
            "BenillaMw2FeedbackFrame",
            "BenillaMw2LockFrame",
            "BenillaMw2KeypadFrame",
            "BenillaMw2PortalFrame",
            "MacroFrame",
            "SomeAddonFrame",
        ] {
            frame(&s, name);
        }
        s.resolve();
        assert!(
            !cursor_panel_open(&s),
            "nothing open: the gun has the mouse"
        );
        // The Escape menu, then Options: the menu hides as the options window shows.
        s.run("GameMenuFrame:Show()").unwrap();
        assert!(cursor_panel_open(&s), "the Escape menu");
        s.run("GameMenuFrame:Hide() BenillaOptionsFrame:Show()")
            .unwrap();
        assert!(
            cursor_panel_open(&s),
            "the options window keeps the cursor free"
        );
        s.run("BenillaOptionsFrame:Hide() BenillaMw2KeypadFrame:Show()")
            .unwrap();
        assert!(cursor_panel_open(&s), "a keypad");
        s.run("BenillaMw2KeypadFrame:Hide()").unwrap();
        assert!(!cursor_panel_open(&s));
        // A window only the panel manager knows (an addon's), in the centre slot.
        s.run("function GetCenterFrame() return SomeAddonFrame end SomeAddonFrame:Show()")
            .unwrap();
        assert!(cursor_panel_open(&s), "a panel the manager placed");
        s.run("SomeAddonFrame:Hide()").unwrap();
        assert!(!cursor_panel_open(&s));
        assert!(s.errors().is_empty(), "script errors: {:?}", s.errors());
    }
}
