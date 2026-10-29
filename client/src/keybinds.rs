//! The layer's keys as key bindings: every key of ours is a command in the stock Key Bindings
//! window, under **World of Modern Warfare**, rebound, unbound, saved and reset there like WoW's
//! own (benilla's `benilla-config/bindings/` files). The commands live in a binding plane of their
//! own (`WOMW_*`, [`benilla_app::fusion::LayerBindings`]): a gun key may be a WoW key too (R
//! reloads with the gun up and replies with it down), and a key is unique only among ours.
//!
//! [`Keymap`] is the live table read back ([`benilla_app::fusion::LayerBoundKeys`]): what
//! crate::input matches presses against, and what tooltips, hints and the guide show a key as.
//! Two of the layer's keys follow WoW's own bindings instead of having commands: Space's stance
//! rise is `JUMP`'s key, and the companion's pet bar is `BONUSACTIONBUTTON1`-`10`'s (Ctrl+1 to
//! Ctrl+0). The modifiers (Left Shift sprint, a bare tap of Left Ctrl crouch and its hold prone, Ctrl+Space Sky
//! Surge, Left Alt the free cursor and the off hand, Ctrl+Alt the locked cursor), Escape and the arrows in a window are fixed: the window
//! cannot bind a bare modifier. No default of ours is an F key or a Shift chord.

use std::collections::HashMap;

use benilla_app::fusion::{LayerBindings, LayerBoundKeys};
use bevy::prelude::*;

/// Every layer command's name starts with this: its binding plane.
pub const PREFIX: &str = "WOMW_";
/// The window's section: `BINDING_HEADER_WOMW`.
pub const HEADER: &str = "WOMW";
pub const HEADER_TEXT: &str = "World of Modern Warfare";

/// One of the layer's bindable actions.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Action {
    Fire,
    Aim,
    Reload,
    /// The current action bar page's slots 1-6 (crate::action_systems).
    Slot1,
    Slot2,
    Slot3,
    Slot4,
    Slot5,
    Slot6,
    /// The current system's previous and next bar page.
    PagePrev,
    PageNext,
    /// Modern and Classic back and forth.
    Switch,
    /// Weapons away (Utility) and back.
    Sheathe,
    /// The Companion system's bar on and off.
    Companion,
    /// A key for crouch besides Left Ctrl's tap (unbound by default).
    Crouch,
    Interact,
    View,
    Build,
    BuildPrev,
    BuildNext,
    Deconstruct,
    Kits,
    Talents,
    Abilities,
    Companions,
    Construction,
    Journal,
    Guide,
    Minstrelsy,
}

/// An action's command, its name in the window and its default chord (none: unbound).
pub struct Def {
    pub action: Action,
    pub command: &'static str,
    pub label: &'static str,
    pub default: Option<&'static str>,
}

const fn def(
    action: Action,
    command: &'static str,
    label: &'static str,
    default: Option<&'static str>,
) -> Def {
    Def {
        action,
        command,
        label,
        default,
    }
}

/// The six slot actions, in slot order.
pub const SLOTS: [Action; 6] = [
    Action::Slot1,
    Action::Slot2,
    Action::Slot3,
    Action::Slot4,
    Action::Slot5,
    Action::Slot6,
];

/// Every action, in the window's order.
pub const ACTIONS: [Def; 29] = [
    def(
        Action::Fire,
        "WOMW_FIRE",
        "Fire / right hand",
        Some("BUTTON1"),
    ),
    def(
        Action::Aim,
        "WOMW_AIM",
        "Aim down sights / left hand (building: reinforce)",
        Some("BUTTON2"),
    ),
    def(
        Action::Reload,
        "WOMW_RELOAD",
        "Reload (building: turn the piece)",
        Some("R"),
    ),
    def(Action::Slot1, "WOMW_SLOT1", "Action bar slot 1", Some("1")),
    def(Action::Slot2, "WOMW_SLOT2", "Action bar slot 2", Some("2")),
    def(Action::Slot3, "WOMW_SLOT3", "Action bar slot 3", Some("3")),
    def(Action::Slot4, "WOMW_SLOT4", "Action bar slot 4", Some("4")),
    def(Action::Slot5, "WOMW_SLOT5", "Action bar slot 5", Some("Q")),
    def(Action::Slot6, "WOMW_SLOT6", "Action bar slot 6", Some("E")),
    def(
        Action::PagePrev,
        "WOMW_PAGEPREV",
        "Action bar: previous page",
        Some("ALT-MOUSEWHEELUP"),
    ),
    def(
        Action::PageNext,
        "WOMW_PAGENEXT",
        "Action bar: next page",
        Some("ALT-MOUSEWHEELDOWN"),
    ),
    def(
        Action::Switch,
        "WOMW_SWITCH",
        "Switch Modern / Classic",
        Some("X"),
    ),
    def(
        Action::Sheathe,
        "WOMW_SHEATHE",
        "Put weapons away (Utility) / draw them",
        Some("Z"),
    ),
    def(
        Action::Companion,
        "WOMW_COMPANION",
        "Companion action bar / back",
        Some("C"),
    ),
    def(
        Action::Crouch,
        "WOMW_CROUCH",
        "Crouch, hold: prone (also Left Ctrl)",
        None,
    ),
    def(
        Action::Interact,
        "WOMW_INTERACT",
        "Use / interact",
        Some("F"),
    ),
    def(
        Action::View,
        "WOMW_VIEW",
        "First / third person",
        Some("V"),
    ),
    def(
        Action::Build,
        "WOMW_BUILD",
        "Professions",
        Some("B"),
    ),
    def(
        Action::BuildPrev,
        "WOMW_BUILDPREV",
        "Build: previous piece",
        Some("["),
    ),
    def(
        Action::BuildNext,
        "WOMW_BUILDNEXT",
        "Build: next piece",
        Some("]"),
    ),
    def(
        Action::Deconstruct,
        "WOMW_DECONSTRUCT",
        "Build: take a piece down",
        Some("BUTTON3"),
    ),
    def(Action::Kits, "WOMW_KITS", "Kits", Some("K")),
    def(Action::Talents, "WOMW_TALENTS", "Kits: Talents page", None),
    def(
        Action::Abilities,
        "WOMW_ABILITIES",
        "Kits: Abilities page",
        None,
    ),
    def(
        Action::Companions,
        "WOMW_COMPANIONS",
        "Companions",
        Some("N"),
    ),
    def(
        Action::Construction,
        "WOMW_CONSTRUCTION",
        "Construction: homestead window",
        None,
    ),
    def(Action::Journal, "WOMW_JOURNAL", "Achievements", None),
    def(Action::Guide, "WOMW_GUIDE", "New-player guide", None),
    def(
        Action::Minstrelsy,
        "WOMW_MINSTRELSY",
        "Minstrelsy window",
        None,
    ),
];

/// Stock default keys moved for the layer, as `(key, command)` over the stock file, an empty
/// command unbinding the key (each still bindable in the window). The layer's keys take B, N, X,
/// Z, V, C, K, Q and E for good; the main bar's own keys 5 to = and its page keys (Shift+1-6,
/// Shift+arrows, Shift+wheel) would press or page hidden buttons; and the windows move: J the
/// character sheet (C is the Companion bar), P the social window (O freed), H the nameplates (V is the view).
pub const STOCK_DEFAULTS: [(&str, &str); 31] = [
    ("B", ""),
    ("N", ""),
    ("X", ""),
    ("Z", ""),
    ("V", ""),
    ("C", ""),
    ("K", ""),
    ("Q", ""),
    ("E", ""),
    ("O", ""),
    ("5", ""),
    ("6", ""),
    ("7", ""),
    ("8", ""),
    ("9", ""),
    ("0", ""),
    ("-", ""),
    ("=", ""),
    ("SHIFT-1", ""),
    ("SHIFT-2", ""),
    ("SHIFT-3", ""),
    ("SHIFT-4", ""),
    ("SHIFT-5", ""),
    ("SHIFT-6", ""),
    ("SHIFT-UP", ""),
    ("SHIFT-DOWN", ""),
    ("SHIFT-MOUSEWHEELUP", ""),
    ("SHIFT-MOUSEWHEELDOWN", ""),
    ("J", "TOGGLECHARACTER0"),
    ("P", "TOGGLESOCIAL"),
    ("H", "NAMEPLATES"),
];

/// The stock command whose keys Space's stance rise follows.
pub const JUMP_COMMAND: &str = "JUMP";
/// The stock commands whose keys work the companion's pet bar slots 0-9.
pub const PET_COMMANDS: [&str; 10] = [
    "BONUSACTIONBUTTON1",
    "BONUSACTIONBUTTON2",
    "BONUSACTIONBUTTON3",
    "BONUSACTIONBUTTON4",
    "BONUSACTIONBUTTON5",
    "BONUSACTIONBUTTON6",
    "BONUSACTIONBUTTON7",
    "BONUSACTIONBUTTON8",
    "BONUSACTIONBUTTON9",
    "BONUSACTIONBUTTON10",
];

impl Action {
    pub fn def(self) -> &'static Def {
        ACTIONS
            .iter()
            .find(|d| d.action == self)
            .expect("every action has a row")
    }

    pub fn command(self) -> &'static str {
        self.def().command
    }

    pub fn from_command(command: &str) -> Option<Action> {
        ACTIONS
            .iter()
            .find(|d| d.command.eq_ignore_ascii_case(command))
            .map(|d| d.action)
    }
}

/// What benilla is told at start: the commands, their names, and the defaults over the stock file.
pub fn layer_bindings() -> LayerBindings {
    let mut defaults: Vec<(String, String)> = STOCK_DEFAULTS
        .iter()
        .map(|(k, c)| (k.to_string(), c.to_string()))
        .collect();
    defaults.extend(
        ACTIONS
            .iter()
            .filter_map(|d| d.default.map(|k| (k.to_string(), d.command.to_string()))),
    );
    LayerBindings {
        prefix: PREFIX.into(),
        header: (HEADER.into(), HEADER_TEXT.into()),
        commands: ACTIONS
            .iter()
            .map(|d| (d.command.to_string(), d.label.to_string()))
            .collect(),
        defaults,
    }
}

/// A chord's modifiers.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Mods {
    pub alt: bool,
    pub ctrl: bool,
    pub shift: bool,
}

/// A chord string as the table spells it: `[ALT-][CTRL-][SHIFT-]<TOKEN>`.
pub fn chord(mods: Mods, token: &str) -> String {
    let mut s = String::new();
    if mods.alt {
        s.push_str("ALT-");
    }
    if mods.ctrl {
        s.push_str("CTRL-");
    }
    if mods.shift {
        s.push_str("SHIFT-");
    }
    s.push_str(token);
    s
}

/// A chord's base token, its modifiers dropped (`CTRL-SHIFT-R` is `R`, `CTRL--` is `-`).
pub fn base_token(chord: &str) -> &str {
    let mut rest = chord;
    while let Some(r) = ["ALT-", "CTRL-", "SHIFT-"]
        .iter()
        .find_map(|p| rest.strip_prefix(p).filter(|r| !r.is_empty()))
    {
        rest = r;
    }
    rest
}

/// A mouse button's token (`BUTTON1` the left).
pub fn mouse_token(b: MouseButton) -> Option<&'static str> {
    Some(match b {
        MouseButton::Left => "BUTTON1",
        MouseButton::Right => "BUTTON2",
        MouseButton::Middle => "BUTTON3",
        MouseButton::Forward => "BUTTON4",
        MouseButton::Back => "BUTTON5",
        _ => return None,
    })
}

/// The live keys, read back from the table whenever it changes.
#[derive(Resource, Debug, Default, Clone)]
pub struct Keymap {
    version: Option<u64>,
    /// Layer chord to action.
    actions: HashMap<String, Action>,
    /// Each action's chords, in bind order.
    keys: HashMap<Action, Vec<String>>,
    /// `JUMP`'s chords.
    jump: Vec<String>,
    /// The pet bar's chords and slots.
    pet: Vec<(String, u8)>,
}

impl Keymap {
    /// The table as `(chord, command)` pairs, both planes.
    pub fn from_pairs<'a>(pairs: impl IntoIterator<Item = (&'a str, &'a str)>) -> Keymap {
        let mut map = Keymap::default();
        for (key, command) in pairs {
            if let Some(a) = Action::from_command(command) {
                map.actions.insert(key.to_string(), a);
                map.keys.entry(a).or_default().push(key.to_string());
            } else if command.eq_ignore_ascii_case(JUMP_COMMAND) {
                map.jump.push(key.to_string());
            } else if let Some(i) = PET_COMMANDS
                .iter()
                .position(|c| c.eq_ignore_ascii_case(command))
            {
                map.pet.push((key.to_string(), i as u8));
            }
        }
        map
    }

    /// The defaults as a fresh install has them (the stock file's JUMP and pet bar keys
    /// included): for tests and before the first world entry.
    pub fn defaults() -> Keymap {
        let mut pairs: Vec<(&str, &str)> = ACTIONS
            .iter()
            .filter_map(|d| d.default.map(|k| (k, d.command)))
            .collect();
        pairs.extend([("SPACE", "JUMP"), ("NUMPAD0", "JUMP")]);
        const PET_KEYS: [&str; 10] = [
            "CTRL-1", "CTRL-2", "CTRL-3", "CTRL-4", "CTRL-5", "CTRL-6", "CTRL-7", "CTRL-8",
            "CTRL-9", "CTRL-0",
        ];
        pairs.extend(PET_KEYS.iter().zip(PET_COMMANDS).map(|(k, c)| (*k, c)));
        Keymap::from_pairs(pairs)
    }

    /// The action a press of `token` with `mods` held is: the exact chord, else the bare key, so
    /// Shift (sprint) or Ctrl (crouch) held does not stop a gun key.
    pub fn action(&self, mods: Mods, token: &str) -> Option<Action> {
        self.actions
            .get(&chord(mods, token))
            .or_else(|| self.actions.get(token))
            .copied()
    }

    /// Whether the press is Space's stance rise (`JUMP`'s key).
    pub fn is_jump(&self, mods: Mods, token: &str) -> bool {
        let exact = chord(mods, token);
        self.jump.iter().any(|k| *k == exact || k == token)
    }

    /// The pet bar slot a press works, exact chord only (Ctrl+1 is the pet's, 1 the gun's).
    pub fn pet_slot(&self, mods: Mods, token: &str) -> Option<u8> {
        let exact = chord(mods, token);
        self.pet.iter().find(|(k, _)| *k == exact).map(|(_, s)| *s)
    }

    /// The chords bound to `action`, in bind order.
    pub fn keys(&self, action: Action) -> &[String] {
        self.keys.get(&action).map_or(&[], Vec::as_slice)
    }

    /// The mouse buttons bound (bare or with modifiers) to `action`.
    pub fn mouse_buttons(&self, action: Action) -> Vec<MouseButton> {
        self.keys(action)
            .iter()
            .filter_map(|k| match base_token(k) {
                "BUTTON1" => Some(MouseButton::Left),
                "BUTTON2" => Some(MouseButton::Right),
                "BUTTON3" => Some(MouseButton::Middle),
                "BUTTON4" => Some(MouseButton::Forward),
                "BUTTON5" => Some(MouseButton::Back),
                _ => None,
            })
            .collect()
    }

    /// How the interface shows an action's key: its first binding, `"Not Bound"` without one.
    pub fn label(&self, action: Action) -> String {
        self.keys(action)
            .first()
            .map_or_else(|| "Not Bound".to_string(), |k| key_text(k))
    }

    /// WoW's jump key (Space's stance rise and climb), as [`Keymap::label`] shows a key.
    pub fn jump_label(&self) -> String {
        self.jump
            .first()
            .map_or_else(|| "Jump".to_string(), |k| key_text(k))
    }

    /// The pet bar's first and last slot keys, as "Ctrl+1 to Ctrl+0".
    pub fn pet_range(&self) -> String {
        let first = self
            .pet
            .iter()
            .find(|(_, s)| *s == 0)
            .map(|(k, _)| key_text(k));
        let last = self
            .pet
            .iter()
            .find(|(_, s)| *s == 9)
            .map(|(k, _)| key_text(k));
        match (first, last) {
            (Some(a), Some(b)) => format!("{a} to {b}"),
            (Some(a), None) => a,
            _ => "the Companion bar keys".into(),
        }
    }
}

/// A chord as a player reads it: `SHIFT-R` is `Shift+R`, `BUTTON3` is `Middle Mouse`.
pub fn key_text(chord: &str) -> String {
    let base = base_token(chord);
    let mods = &chord[..chord.len() - base.len()];
    let mut out = String::new();
    for m in mods.split('-').filter(|m| !m.is_empty()) {
        out.push_str(match m {
            "ALT" => "Alt+",
            "CTRL" => "Ctrl+",
            _ => "Shift+",
        });
    }
    let named = match base {
        "BUTTON1" => "Left Mouse",
        "BUTTON2" => "Right Mouse",
        "BUTTON3" => "Middle Mouse",
        "BUTTON4" => "Mouse Button 4",
        "BUTTON5" => "Mouse Button 5",
        "MOUSEWHEELUP" => "Mouse Wheel Up",
        "MOUSEWHEELDOWN" => "Mouse Wheel Down",
        "SPACE" => "Space",
        "TAB" => "Tab",
        "ESCAPE" => "Esc",
        "ENTER" => "Enter",
        "BACKSPACE" => "Backspace",
        "INSERT" => "Insert",
        "DELETE" => "Delete",
        "HOME" => "Home",
        "END" => "End",
        "PAGEUP" => "Page Up",
        "PAGEDOWN" => "Page Down",
        "UP" => "Up Arrow",
        "DOWN" => "Down Arrow",
        "LEFT" => "Left Arrow",
        "RIGHT" => "Right Arrow",
        "CAPSLOCK" => "Caps Lock",
        "NUMLOCK" => "Num Lock",
        _ => "",
    };
    if !named.is_empty() {
        out.push_str(named);
    } else if let Some(n) = base.strip_prefix("NUMPAD") {
        out.push_str("Num Pad ");
        out.push_str(&n[..1].to_uppercase());
        out.push_str(&n[1..].to_lowercase());
    } else {
        out.push_str(base);
    }
    out
}

/// Fill a text's `{WOMW_X}` placeholders with the keys bound now (`{PETBAR}`: the pet bar's).
pub fn fill_keys(text: &str, keys: &Keymap) -> String {
    let mut out = String::with_capacity(text.len());
    let mut rest = text;
    while let Some(open) = rest.find('{') {
        out.push_str(&rest[..open]);
        let after = &rest[open + 1..];
        let Some(close) = after.find('}') else {
            out.push_str(&rest[open..]);
            return out;
        };
        let name = &after[..close];
        match Action::from_command(name) {
            Some(a) => out.push_str(&keys.label(a)),
            None if name == "PETBAR" => out.push_str(&keys.pet_range()),
            None => {
                out.push('{');
                out.push_str(name);
                out.push('}');
            }
        }
        rest = &after[close + 1..];
    }
    out.push_str(rest);
    out
}

pub struct KeybindsPlugin;

impl Plugin for KeybindsPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(layer_bindings())
            .insert_resource(Keymap::defaults())
            .add_systems(
                PreUpdate,
                read_keymap.before(benilla_app::fusion::LayerInput),
            )
            .add_systems(Update, open_windows.in_set(crate::Mw2Set::Mode));
    }
}

/// The Companions, Construction and Professions keys open their windows as the micro buttons do.
fn open_windows(
    input: Res<crate::input::Mw2Input>,
    mut open: MessageWriter<crate::panels::OpenPanel>,
) {
    use crate::panels::{Access, OpenPanel, Panel};
    for (asked, panel) in [
        (input.companions, Panel::Companions),
        (input.construction, Panel::Construction),
        (input.professions, Panel::Professions),
    ] {
        if asked {
            open.write(OpenPanel {
                panel,
                access: Access::Hud,
            });
        }
    }
}

/// Take the table when it changed.
fn read_keymap(bound: Option<Res<LayerBoundKeys>>, mut map: ResMut<Keymap>) {
    let Some(bound) = bound else { return };
    if bound.version == 0 || map.version == Some(bound.version) {
        return;
    }
    let mut fresh = Keymap::from_pairs(bound.keys.iter().map(|(k, c)| (k.as_str(), c.as_str())));
    fresh.version = Some(bound.version);
    *map = fresh;
}

#[cfg(test)]
mod tests {
    use super::*;
    use benilla_ui::script::UiScript;
    use benilla_ui::script::keybind::parse_bindings_wtf;

    /// The stock defaults that matter here, as `WTF\DefaultBindings.wtf` has them.
    const STOCK: &str = "bind SPACE JUMP\nbind NUMPAD0 JUMP\nbind R REPLY\nbind Q STRAFELEFT\n\
        bind 1 ACTIONBUTTON1\nbind 4 ACTIONBUTTON4\nbind G TARGETLASTHOSTILE\nbind F ASSISTTARGET\n\
        bind V NAMEPLATES\nbind C TOGGLECHARACTER0\nbind B TOGGLEBACKPACK\nbind F12 TOGGLEBACKPACK\n\
        bind SHIFT-B OPENALLBAGS\nbind P TOGGLESPELLBOOK\nbind N TOGGLETALENTS\n\
        bind K TOGGLECHARACTER1\nbind F5 TARGETPARTYMEMBER4\nbind F8 TOGGLEBAG1\nbind F9 TOGGLEBAG2\n\
        bind F10 TOGGLEBAG3\nbind TAB TARGETNEARESTENEMY\nbind CTRL-1 BONUSACTIONBUTTON1\n\
        bind CTRL-0 BONUSACTIONBUTTON10\nbind BUTTON1 CAMERAORSELECTORMOVE\n\
        bind BUTTON2 TURNORACTION\nbind BUTTON3 MOVEANDSTEER\nbind L TOGGLEQUESTLOG\n\
        bind M TOGGLEWORLDMAP\nbind O TOGGLESOCIAL\nbind X SITORSTAND\nbind Z TOGGLESHEATH\n\
        bind E STRAFERIGHT\nbind 5 ACTIONBUTTON5\nbind = ACTIONBUTTON12\nbind H TOGGLECHARACTER4\n\
        bind SHIFT-1 ACTIONPAGE1\nbind SHIFT-UP PREVIOUSACTIONPAGE\nbind SHIFT-DOWN NEXTACTIONPAGE\n\
        bind SHIFT-MOUSEWHEELUP PREVIOUSACTIONPAGE\nbind SHIFT-MOUSEWHEELDOWN NEXTACTIONPAGE\n";

    /// A VM seeded as benilla seeds one at world entry with our layer on.
    fn vm() -> UiScript {
        let layer = layer_bindings();
        let mut s = UiScript::new().unwrap();
        s.set_layer_binding_prefix(Some(&layer.prefix));
        let mut defaults = parse_bindings_wtf(STOCK);
        defaults.extend(layer.defaults.clone());
        s.set_default_bindings(defaults);
        s.load_binding_set(1);
        s
    }

    fn action_of(s: &UiScript, key: &str) -> String {
        s.eval(&format!(r#"return GetBindingAction("{key}")"#))
            .unwrap()
    }

    fn map(s: &UiScript) -> Keymap {
        let keys = s.binding_keys();
        Keymap::from_pairs(keys.iter().map(|(k, c)| (k.as_str(), c.as_str())))
    }

    #[test]
    fn every_action_is_a_command_of_the_window() {
        let layer = layer_bindings();
        assert_eq!(layer.commands.len(), ACTIONS.len());
        for d in &ACTIONS {
            assert!(d.command.starts_with(PREFIX), "{}", d.command);
            assert_eq!(Action::from_command(d.command), Some(d.action));
            assert!(!d.label.is_empty());
            assert!(
                layer
                    .commands
                    .iter()
                    .any(|(c, l)| c == d.command && l == d.label)
            );
        }
        // Every action exactly once.
        for (i, a) in ACTIONS.iter().enumerate() {
            assert!(
                ACTIONS[i + 1..]
                    .iter()
                    .all(|b| b.action != a.action && b.command != a.command)
            );
        }
    }

    #[test]
    fn the_defaults_are_the_owners() {
        let k = Keymap::defaults();
        assert_eq!(k.keys(Action::Build), ["B"]);
        assert_eq!(k.keys(Action::Companions), ["N"]);
        assert!(k.keys(Action::Talents).is_empty(), "N is not talents");
        assert_eq!(k.keys(Action::Kits), ["K"]);
        assert!(k.keys(Action::Journal).is_empty(), "L is the stock quest log");
        assert_eq!(k.keys(Action::Switch), ["X"]);
        assert_eq!(k.keys(Action::Sheathe), ["Z"]);
        assert_eq!(k.keys(Action::View), ["V"]);
        assert!(
            k.action(Mods::default(), "TAB").is_none(),
            "Tab targets, as in WoW"
        );
        for (slot, key) in SLOTS.iter().zip(["1", "2", "3", "4", "Q", "E"]) {
            assert_eq!(k.keys(*slot), [key]);
        }
        assert_eq!(k.keys(Action::PagePrev), ["ALT-MOUSEWHEELUP"]);
        assert_eq!(k.keys(Action::PageNext), ["ALT-MOUSEWHEELDOWN"]);
        // No F key, no Shift (or Ctrl) chord, and no key twice; Alt only on the wheel's pages.
        let mut seen = Vec::new();
        for d in &ACTIONS {
            let Some(key) = d.default else { continue };
            let base = base_token(key);
            assert!(
                !(base.len() >= 2
                    && base.starts_with('F')
                    && base[1..].chars().all(|c| c.is_ascii_digit())),
                "{} defaults to an F key",
                d.command
            );
            assert!(!key.contains("SHIFT-") && !key.contains("CTRL-"), "{key}");
            assert!(
                base == key || base.starts_with("MOUSEWHEEL"),
                "{} defaults to a plain key",
                d.command
            );
            assert!(!seen.contains(&key), "{key} is two actions' default");
            seen.push(key);
        }
        // The keys the owner freed carry nothing of ours.
        for key in [
            "G", "T", "Y", "U", "I", "O", "H", "TAB", "F5", "F6", "F7", "F8", "F9", "F10",
        ] {
            assert!(
                k.action(Mods::default(), key).is_none(),
                "{key} is no layer key"
            );
        }
    }

    #[test]
    fn the_stock_keys_move_to_their_new_homes() {
        let s = vm();
        // Taken by the layer for good, or pressing the hidden half of the main bar.
        for key in [
            "B", "N", "X", "Z", "V", "C", "K", "Q", "E", "O", "5", "=", "SHIFT-1",
        ] {
            assert_eq!(action_of(&s, key), "", "{key}");
        }
        // Shift (sprint) and its wheel never page the stock bar.
        for key in [
            "SHIFT-MOUSEWHEELUP",
            "SHIFT-MOUSEWHEELDOWN",
            "SHIFT-UP",
            "SHIFT-DOWN",
        ] {
            assert_eq!(action_of(&s, key), "", "{key}");
        }
        assert_eq!(action_of(&s, "J"), "TOGGLECHARACTER0");
        assert_eq!(action_of(&s, "P"), "TOGGLESOCIAL");
        assert_eq!(action_of(&s, "H"), "NAMEPLATES");
        // Classic WoW's own keys stay: Tab targets the nearest enemy, L the quest log, M the map,
        // and F10 opens a bag again with the layer's switch gone.
        for (key, stock) in [
            ("TAB", "TARGETNEARESTENEMY"),
            ("L", "TOGGLEQUESTLOG"),
            ("M", "TOGGLEWORLDMAP"),
            ("F10", "TOGGLEBAG3"),
            ("F12", "TOGGLEBACKPACK"),
            ("SHIFT-B", "OPENALLBAGS"),
            ("R", "REPLY"),
            ("1", "ACTIONBUTTON1"),
            ("SPACE", "JUMP"),
            ("F5", "TARGETPARTYMEMBER4"),
        ] {
            assert_eq!(action_of(&s, key), stock, "{key}");
        }
        let k = map(&s);
        assert_eq!(k.action(Mods::default(), "B"), Some(Action::Build));
        assert_eq!(k.action(Mods::default(), "N"), Some(Action::Companions));
        assert_eq!(k.action(Mods::default(), "R"), Some(Action::Reload));
        assert_eq!(k.action(Mods::default(), "Q"), Some(Action::Slot5));
        assert!(k.is_jump(Mods::default(), "SPACE"));
        let ctrl = Mods {
            ctrl: true,
            ..default()
        };
        assert_eq!(k.pet_slot(ctrl, "1"), Some(0));
        assert_eq!(k.pet_slot(ctrl, "0"), Some(9));
        assert_eq!(k.pet_slot(Mods::default(), "1"), None);
        // Shift (sprint) held: still a reload, still slot 1 (no Shift variants).
        let shift = Mods {
            shift: true,
            ..default()
        };
        assert_eq!(k.action(shift, "R"), Some(Action::Reload));
        assert_eq!(k.action(shift, "1"), Some(Action::Slot1));
        // Alt with a slot key is that slot (the off hand); Alt with the wheel pages, the bare and
        // the Shift wheel do not.
        let alt = Mods {
            alt: true,
            ..default()
        };
        assert_eq!(k.action(alt, "1"), Some(Action::Slot1));
        assert_eq!(k.action(alt, "MOUSEWHEELUP"), Some(Action::PagePrev));
        assert_eq!(k.action(alt, "MOUSEWHEELDOWN"), Some(Action::PageNext));
        assert_eq!(k.action(Mods::default(), "MOUSEWHEELUP"), None);
        assert_eq!(k.action(shift, "MOUSEWHEELDOWN"), None);
    }

    #[test]
    fn rebinding_moves_what_the_layer_reacts_to() {
        let s = vm();
        // The window binding Reload to T: with a layer row selected, as the window has it.
        s.run(
            r#"KeyBindingFrame = { selected = "WOMW_RELOAD" }
               SetBinding("R")
               SetBinding("T", "WOMW_RELOAD")
               KeyBindingFrame.selected = nil"#,
        )
        .unwrap();
        let k = map(&s);
        assert_eq!(k.action(Mods::default(), "T"), Some(Action::Reload));
        assert_eq!(k.action(Mods::default(), "R"), None);
        assert_eq!(action_of(&s, "R"), "REPLY", "WoW keeps R");
        assert_eq!(k.label(Action::Reload), "T");
        // Companions to Shift+N; the build key unbound.
        s.run(r#"SetBinding("SHIFT-N", "WOMW_COMPANIONS") SetBinding("B", "WOMW_JOURNAL")"#)
            .unwrap();
        let k = map(&s);
        assert_eq!(
            k.action(
                Mods {
                    shift: true,
                    ..default()
                },
                "N"
            ),
            Some(Action::Companions)
        );
        assert_eq!(k.action(Mods::default(), "B"), Some(Action::Journal));
        assert!(k.keys(Action::Build).is_empty());
        assert_eq!(k.label(Action::Build), "Not Bound");
        assert_eq!(k.label(Action::Companions), "N", "the first of its keys");
        // Jump moved: the stance rise follows.
        s.run(r#"SetBinding("X", "JUMP")"#).unwrap();
        assert!(map(&s).is_jump(Mods::default(), "X"));
    }

    #[test]
    fn keys_read_as_a_player_reads_them() {
        assert_eq!(key_text("BUTTON3"), "Middle Mouse");
        assert_eq!(key_text("SHIFT-R"), "Shift+R");
        assert_eq!(key_text("CTRL--"), "Ctrl+-");
        assert_eq!(key_text("NUMPAD0"), "Num Pad 0");
        assert_eq!(key_text("F5"), "F5");
        assert_eq!(key_text("TAB"), "Tab");
        let k = Keymap::defaults();
        assert_eq!(k.pet_range(), "Ctrl+1 to Ctrl+0");
        assert_eq!(
            fill_keys("{WOMW_BUILD}: build. {WOMW_GUIDE}. {PETBAR}. {nope}", &k),
            "B: build. Not Bound. Ctrl+1 to Ctrl+0. {nope}"
        );
    }
}
