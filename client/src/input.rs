//! The layer's controls, and taking them away from WoW.
//!
//! Every key of the layer is a key binding (crate::keybinds): a `WOMW_*` command in the stock
//! Key Bindings window, read back as [`Keymap`], so a press is matched against what is bound now,
//! not a fixed key. The layer is always on in the world. While [`FpsState::owns_input`] holds,
//! the gun's keys and buttons (fire, aim, reload, crouch, use, build) belong to this layer: their
//! raw messages are blanked in `PreUpdate`, the same place benilla's loading cover cuts input, and
//! they are cut from benilla's button planes, so no WoW binding or world click sees them.
//! Whenever in the world (the chat and a layer window's typing aside), the action systems' keys
//! are the layer's too (crate::action_systems: the slot keys, Alt with the wheel for the page,
//! X and Z), and so are the windows' keys and the view. Everything else (movement, jump, chat,
//! Tab's targeting) stays WoW's. Holding Left Alt hands the mouse back to the interface while
//! held; Ctrl+Alt frees it until pressed again. While the Key Bindings window is open the layer
//! takes no key at all, so any key can be bound there.

use std::collections::HashMap;

use benilla_app::Objects;
use benilla_app::fusion::{LayoutNames, Stance};
use bevy::input::keyboard::{Key, KeyboardInput, NativeKey, NativeKeyCode};
use bevy::input::mouse::{
    AccumulatedMouseMotion, AccumulatedMouseScroll, MouseButtonInput, MouseWheel,
};
use bevy::input::{ButtonState, InputSystems};
use bevy::prelude::*;

use crate::keybinds::{Action, Keymap, Mods, SLOTS};
use crate::mode::FpsState;

/// Held: the cursor goes back to the interface; with Ctrl, it stays out until Ctrl+Alt again.
/// Fixed: a bare modifier cannot be bound.
pub const CURSOR: KeyCode = KeyCode::AltLeft;
/// Crouch (toggles; again stands), on a bare tap: it goes down on Ctrl's release, and only when
/// nothing else (a key, a button, Alt) was pressed while Ctrl was held, so Ctrl+key chords (the
/// companion's Ctrl+1..0, Ctrl+Space's Launch, Ctrl+Alt's locked cursor, a Ctrl-click) keep
/// working and never crouch. Held [`CTRL_HOLD_FOR_PRONE`] with nothing else pressed, it goes prone
/// instead (MW2's held crouch), while still held; its release is then nothing. Fixed, as a bare
/// modifier.
pub const CROUCH_KEY: KeyCode = KeyCode::ControlLeft;
/// A bare Left Ctrl held this long goes prone (or, already prone, stands).
pub const CTRL_HOLD_FOR_PRONE: f32 = 0.35;
/// Sprint while held (read, not taken: WoW keeps Shift as its modifier). Fixed, as a modifier.
pub const SPRINT: KeyCode = KeyCode::ShiftLeft;

/// The actions taken while the layer owns input, as the gun's keys.
const GUN_ACTIONS: [Action; 6] = [
    Action::Fire,
    Action::Aim,
    Action::Reload,
    Action::Crouch,
    Action::Interact,
    Action::Deconstruct,
];

/// One control's state this frame, edge-tracked from the raw messages so a cut button plane does
/// not lose the release.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Control {
    pub held: bool,
    pub pressed: bool,
    pub released: bool,
}

impl Control {
    fn begin_frame(&mut self) {
        self.pressed = false;
        self.released = false;
    }

    fn press(&mut self) {
        if !self.held {
            self.pressed = true;
        }
        self.held = true;
    }

    fn release(&mut self) {
        if self.held {
            self.released = true;
        }
        self.held = false;
    }

    fn apply(&mut self, state: ButtonState) {
        match state {
            ButtonState::Pressed => self.press(),
            ButtonState::Released => self.release(),
        }
    }
}

/// This frame's controls. The gun's are only meaningful while the layer owns input; otherwise
/// they read released.
#[derive(Resource, Debug, Default)]
pub struct Mw2Input {
    pub fire: Control,
    pub ads: Control,
    pub reload: Control,
    /// The Modern bar's grenades and call-in (crate::action_systems sets them from their slots).
    pub lethal: Control,
    pub tactical: Control,
    pub killstreak: Control,
    pub crouch: Control,
    /// A held bare Left Ctrl reached [`CTRL_HOLD_FOR_PRONE`] this frame: prone (again stands).
    pub prone: Control,
    /// The Modern bar's special ammo and rearm (crate::action_systems).
    pub armory_ammo: Control,
    pub rearm: Control,
    /// Unpressed by any key: B opens the Professions hub (crate::professions), and Build Structure
    /// on Utility's bar enters build mode.
    pub build: Control,
    /// Unset by any key: the build menu opens with build mode and closes with Esc.
    pub build_menu: bool,
    /// The piece keys ([ and ]) in build mode: the piece before (-1) or after (+1) the one in hand.
    pub build_cycle: i32,
    /// Use (F).
    pub interact: Control,
    /// Take a piece down (the middle mouse button) in build mode.
    pub middle: Control,
    /// The wheel's turn this frame (building sets how far out the piece goes), up positive.
    pub wheel: f32,
    /// WoW's jump key (Space): read while the layer owns input, taken only while `blank_jump`
    /// holds.
    pub jump: Control,
    /// Set by the stance (crouched): Space stands up instead of jumping.
    pub blank_jump: bool,
    /// Left Shift is held.
    pub sprint: bool,
    /// The current page's slots 1-6 (1, 2, 3, 4, Q, E), and whether Alt was held at each press
    /// (the off hand in Classic).
    pub slots: [Control; 6],
    pub slot_alt: [bool; 6],
    /// X: Modern and Classic back and forth.
    pub switch_system: bool,
    /// C: the Companion system's bar on and off (crate::action_systems).
    pub companion_system: bool,
    /// Ctrl+Space this frame: Launch while riding a flying companion (crate::companion).
    pub sky_surge: bool,
    /// Z: the weapons away (Utility) and back.
    pub sheathe: bool,
    /// The current system's page turned this frame (Alt with the wheel): -1 back, +1 on.
    pub page: i32,
    /// The primary (0), the secondary (1) or the underbarrel (2) was picked this frame (the Modern
    /// bar's slots, crate::action_systems).
    pub hand: Option<u8>,
    /// Utility's hands hold a tool this frame (crate::tools): a swing is the tool's strike, never
    /// an attack.
    pub tool: bool,
    /// The mouse wheel turned this frame: swap guns (in Modern).
    pub wheel_swap: bool,
    /// Open or close the kit window (K).
    pub cac: bool,
    /// The kit window's Talents page (open on it, or close it from it); unbound by default.
    pub talents: bool,
    /// The kit window's Abilities page, likewise.
    pub abilities: bool,
    /// Open or close the achievements (unbound by default).
    pub achievements: bool,
    /// Open or close the Companions window (N).
    pub companions: bool,
    /// Open or close the Construction window (the homestead; unbound by default).
    pub construction: bool,
    /// Open or close the Professions hub (B): every profession, its trade window and abilities.
    pub professions: bool,
    /// Open or close the new-player guide (unbound by default).
    pub guide: bool,
    /// Open or close Minstrelsy's trade window (unbound by default; a Minstrel Trainer opens the
    /// trainer's).
    pub minstrelsy: bool,
    /// First or third person (V).
    pub view: bool,
    /// Esc was pressed while a layer window was open (and kept from WoW's menu).
    pub escape: bool,
    /// Esc at a ship's helm with no window open: leave the helm (crate::ships).
    pub leave_helm: bool,
    /// The arrows and Enter while a layer window is open (and not typing): its keyboard
    /// navigation.
    pub nav: Option<Nav>,
    /// Typed this frame while a layer window takes typing (`FpsState::typing`): the text, and
    /// Backspace and Enter.
    pub typed: String,
    pub backspace: bool,
    /// The duel window's keys pressed this frame (1-0 and E while `FpsState::duel_keys`).
    pub duel: Vec<KeyCode>,
    pub enter: bool,
    /// Left Alt is held.
    pub cursor: bool,
    /// Ctrl+Alt went down this frame: the locked free cursor on or off (crate::mode).
    pub cursor_lock: bool,
    /// The pet bar's keys (`BONUSACTIONBUTTON1`-`10`, Ctrl+1..Ctrl+0) this frame, or a press on
    /// the Companion system's bar: the companion's pet bar slot 0-9, aimed by the crosshair
    /// (crate::companion).
    pub pet_slot: Option<u8>,
    /// A right click on the Companion system's bar this frame: that pet bar slot's autocast on or
    /// off (crate::companion).
    pub pet_autocast: Option<u8>,
    /// Left Ctrl is held (it is cut from the planes while the layer owns input).
    ctrl_held: bool,
    /// Something else went down while Ctrl was held: its release is no crouch.
    crouch_chord: bool,
    /// When Left Ctrl went down (the layer's clock), for the hold that goes prone.
    ctrl_down_at: f32,
    /// This hold of Ctrl already went prone: its release is no crouch.
    ctrl_went_prone: bool,
    /// While a killstreak has the controls (`FpsState::placing` or `piloting`,
    /// crate::lore_streaks): the left and right buttons, taken from the gun, and Escape.
    pub streak_fire: Control,
    pub streak_alt: Control,
    pub streak_cancel: bool,
    /// The mouse's move this frame while steering the rocket, taken from mouselook (counts,
    /// right and down positive).
    pub pilot_look: Vec2,
}

pub struct InputPlugin;

impl Plugin for InputPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Mw2Input>().add_systems(
            PreUpdate,
            (damp_prone_turn, capture_input)
                .chain()
                .in_set(benilla_app::fusion::LayerInput)
                .after(InputSystems)
                .before(bevy::ui::UiSystems::Focus),
        );
    }
}

/// Prone turning (design spec 5.1): free within ±60° of the facing at go-prone, a quarter speed
/// beyond, as MW2's prone body turns slowly.
pub fn prone_yaw_scale(offset_deg: f32) -> f32 {
    if offset_deg.abs() <= 60.0 { 1.0 } else { 0.25 }
}

/// An angle difference folded into [-180, 180) degrees.
fn wrap_deg(d: f32) -> f32 {
    (d + 180.0).rem_euclid(360.0) - 180.0
}

/// Seconds a wheel swap (or page turn) holds: a smooth scroll (a trackpad) is one, not one a
/// frame.
const WHEEL_SWAP_SECS: f32 = 0.3;

/// Whether the wheel may swap guns (or turn a page) now, `last` the elapsed time of the last.
pub fn wheel_swap_allowed(now: f32, last: Option<f32>) -> bool {
    last.is_none_or(|l| now - l >= WHEEL_SWAP_SECS)
}

/// A key message no binding answers.
/// The keys a ship's helm takes: W, S, A, D (ahead, astern, port, starboard), Space and X (climb,
/// dive) and Esc (leave). Movement keys only by their US position, as crate::ships reads them.
pub fn is_helm_key(code: KeyCode) -> bool {
    matches!(
        code,
        KeyCode::KeyW
            | KeyCode::KeyS
            | KeyCode::KeyA
            | KeyCode::KeyD
            | KeyCode::Space
            | KeyCode::KeyX
            | KeyCode::Escape
    )
}

/// The keys a companion duel takes: 1-0 play cards, E ends the turn.
pub fn is_duel_key(code: KeyCode) -> bool {
    matches!(
        code,
        KeyCode::Digit0
            | KeyCode::Digit1
            | KeyCode::Digit2
            | KeyCode::Digit3
            | KeyCode::Digit4
            | KeyCode::Digit5
            | KeyCode::Digit6
            | KeyCode::Digit7
            | KeyCode::Digit8
            | KeyCode::Digit9
            | KeyCode::KeyE
    )
}

fn blank(ev: &mut KeyboardInput) {
    ev.key_code = KeyCode::Unidentified(NativeKeyCode::Unidentified);
    ev.logical_key = Key::Unidentified(NativeKey::Unidentified);
    ev.text = None;
}

/// The control an action holds, for the actions pressed and held.
fn control_mut(input: &mut Mw2Input, action: Action) -> Option<&mut Control> {
    Some(match action {
        Action::Fire => &mut input.fire,
        Action::Aim => &mut input.ads,
        Action::Reload => &mut input.reload,
        Action::Crouch => &mut input.crouch,
        Action::Interact => &mut input.interact,
        Action::Deconstruct => &mut input.middle,
        _ => return None,
    })
}

/// The slot (0..6) a slot action presses.
pub fn slot_of(action: Action) -> Option<usize> {
    SLOTS.iter().position(|a| *a == action)
}

/// Whether the world's keys are the layer's now: in the world, unless WoW's chat or a layer
/// window has the keyboard.
pub fn takes_world_keys(in_world: bool, chat_focus: bool, typing: bool) -> bool {
    in_world && !chat_focus && !typing
}

/// What a window, view or system key asks for this frame, if `state` lets it. `None`: the key is
/// not the layer's now.
fn window_key(state: &FpsState, action: Action) -> Option<fn(&mut Mw2Input)> {
    if !takes_world_keys(state.in_world, state.chat_focus, state.typing) {
        return None;
    }
    let f: fn(&mut Mw2Input) = match action {
        Action::Journal => |i| i.achievements = true,
        Action::Companions => |i| i.companions = true,
        // B (the build key) opens and closes the Professions hub, never build mode itself: build
        // mode is Construction's Build Structure ability (Utility's bar).
        Action::Build => |i| i.professions = true,
        Action::Construction => |i| i.construction = true,
        Action::Guide => |i| i.guide = true,
        Action::Minstrelsy => |i| i.minstrelsy = true,
        Action::Kits => |i| i.cac = true,
        Action::Talents => |i| i.talents = true,
        Action::Abilities => |i| i.abilities = true,
        Action::View => |i| i.view = true,
        Action::Switch => |i| i.switch_system = true,
        Action::Sheathe => |i| i.sheathe = true,
        Action::Companion => |i| i.companion_system = true,
        Action::PagePrev => |i| i.page -= 1,
        Action::PageNext => |i| i.page += 1,
        _ => return None,
    };
    Some(f)
}

/// The presses the layer took and is waiting to see let go: the release goes to the action the
/// press was (a modifier let go first does not change it), and the key stays cut from the
/// planes while held.
#[derive(Default)]
pub struct Latches {
    keys: HashMap<KeyCode, (Action, Key)>,
    mouse: HashMap<MouseButton, Action>,
    /// Keys held as Space's stance rise (`JUMP`'s), cut from the planes while `blank_jump`.
    jump: HashMap<KeyCode, Key>,
    /// The elapsed time of the last wheel gun swap and the last wheel page turn.
    wheel: (Option<f32>, Option<f32>),
}

/// Prone: damp the mouse's turn once the body is 60° off its go-prone facing.
fn damp_prone_turn(
    state: Res<FpsState>,
    fusion: Res<crate::fusion::Fusion>,
    objects: Objects,
    bodies: Query<&GlobalTransform>,
    mut motion: ResMut<AccumulatedMouseMotion>,
    mut prone_yaw0: Local<Option<f32>>,
) {
    let yaw = objects
        .player()
        .and_then(|e| bodies.get(e).ok())
        .map(|g| g.rotation().to_euler(EulerRot::YXZ).0.to_degrees());
    match yaw.filter(|_| state.owns_input && fusion.stance == Stance::Prone) {
        Some(yaw) => {
            let start = *prone_yaw0.get_or_insert(yaw);
            motion.delta.x *= prone_yaw_scale(wrap_deg(yaw - start));
        }
        None => *prone_yaw0 = None,
    }
}

/// A slot key or button went down or up: its control, and Alt as it stood at the press.
fn slot_input(input: &mut Mw2Input, slot: usize, state: ButtonState, alt: bool) {
    if state == ButtonState::Pressed && !input.slots[slot].held {
        input.slot_alt[slot] = alt;
    }
    input.slots[slot].apply(state);
}

#[allow(clippy::too_many_arguments)]
fn capture_input(
    state: Res<FpsState>,
    mut input: ResMut<Mw2Input>,
    mut mouse_raw: MessageReader<MouseButtonInput>,
    mut wheel_raw: MessageMutator<MouseWheel>,
    time: Res<Time>,
    mut keys_raw: MessageMutator<KeyboardInput>,
    mut mouse: ResMut<ButtonInput<MouseButton>>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut logical: ResMut<ButtonInput<Key>>,
    mut motion: ResMut<AccumulatedMouseMotion>,
    (keymap, layout, mut latches, third, scroll): (
        Res<Keymap>,
        Option<Res<LayoutNames>>,
        Local<Latches>,
        Option<Res<crate::thirdperson::ThirdPerson>>,
        Option<ResMut<AccumulatedMouseScroll>>,
    ),
) {
    let owns = state.owns_input;
    let world = takes_world_keys(state.in_world, state.chat_focus, state.typing)
        && !state.key_bindings;
    let us_layout = LayoutNames::default();
    let layout = layout.as_deref().unwrap_or(&us_layout);

    let input = &mut *input;
    for c in [
        &mut input.fire,
        &mut input.ads,
        &mut input.reload,
        &mut input.lethal,
        &mut input.tactical,
        &mut input.killstreak,
        &mut input.crouch,
        &mut input.prone,
        &mut input.armory_ammo,
        &mut input.rearm,
        &mut input.build,
        &mut input.interact,
        &mut input.middle,
        &mut input.jump,
    ] {
        c.begin_frame();
    }
    for c in &mut input.slots {
        c.begin_frame();
    }
    // The ability controls the slots stand for are set afresh each frame (crate::action_systems).
    for c in [
        &mut input.lethal,
        &mut input.tactical,
        &mut input.killstreak,
        &mut input.armory_ammo,
        &mut input.rearm,
    ] {
        c.held = false;
    }
    input.streak_cancel = false;
    input.pilot_look = Vec2::ZERO;
    input.cac = false;
    input.talents = false;
    input.abilities = false;
    input.achievements = false;
    input.companions = false;
    input.construction = false;
    input.professions = false;
    input.guide = false;
    input.minstrelsy = false;
    input.view = false;
    input.switch_system = false;
    input.companion_system = false;
    input.sky_surge = false;
    input.pet_autocast = None;
    input.sheathe = false;
    input.page = 0;
    input.cursor_lock = false;
    input.build_menu = false;
    input.build_cycle = 0;
    input.escape = false;
    input.leave_helm = false;
    input.nav = None;
    input.typed.clear();
    input.duel.clear();
    input.backspace = false;
    input.enter = false;
    input.hand = None;
    input.pet_slot = None;
    input.wheel_swap = false;
    input.wheel = 0.0;
    input.cursor = keys.pressed(CURSOR);
    input.sprint = owns && keys.pressed(SPRINT);
    // The chord's modifiers. Left Ctrl is crouch's, cut from the planes while the layer owns
    // input, so its own held state stands in.
    let mut mods = Mods {
        alt: keys.pressed(KeyCode::AltLeft) || keys.pressed(KeyCode::AltRight),
        ctrl: keys.pressed(KeyCode::ControlLeft)
            || keys.pressed(KeyCode::ControlRight)
            || input.ctrl_held,
        shift: keys.pressed(KeyCode::ShiftLeft) || keys.pressed(KeyCode::ShiftRight),
    };

    let now = time.elapsed_secs();
    let third_person = third.is_some_and(|t| t.on);
    // A wheel the layer takes is cut from the accumulated scroll too: benilla's bindings (WoW's
    // camera zoom) read that, not the messages.
    let mut ate_wheel = false;
    for ev in wheel_raw.read() {
        if ev.y == 0.0 || state.key_bindings {
            continue;
        }
        // Alt with the wheel turns the page (whoever has the mouse); the bare and the Shift
        // wheel never do.
        let token = if ev.y > 0.0 {
            "MOUSEWHEELUP"
        } else {
            "MOUSEWHEELDOWN"
        };
        if let Some(set) = keymap
            .action(mods, token)
            .filter(|a| matches!(a, Action::PagePrev | Action::PageNext))
            .and_then(|a| window_key(&state, a))
        {
            if wheel_swap_allowed(now, latches.wheel.1) {
                set(input);
                latches.wheel.1 = Some(now);
            }
            ev.y = 0.0;
            ate_wheel = true;
            continue;
        }
        // Alt with the wheel is the page's alone: never WoW's camera zoom, bound or not.
        if mods.alt {
            ev.y = 0.0;
            ate_wheel = true;
            continue;
        }
        // Third person: the bare wheel is the camera's (WoW's zoom), never a weapon swap. Build
        // mode keeps its own use of the wheel (the piece's distance).
        if owns && third_person && !state.building {
            continue;
        }
        if owns {
            input.wheel += ev.y.signum();
            if wheel_swap_allowed(now, latches.wheel.0) {
                input.wheel_swap = true;
                latches.wheel.0 = Some(now);
            }
            // Not WoW's camera zoom: first person stays.
            ev.y = 0.0;
            ate_wheel = true;
        }
    }
    // Alt held through the frame takes the wheel even with no message read (a test harness, or
    // the scroll accumulated before this system ran).
    if (ate_wheel || mods.alt)
        && !state.key_bindings
        && let Some(mut scroll) = scroll
        && scroll.delta != Vec2::ZERO
    {
        scroll.delta = Vec2::ZERO;
    }

    for ev in mouse_raw.read() {
        if state.key_bindings {
            continue;
        }
        let pressed = ev.state == ButtonState::Pressed;
        let action = if pressed {
            crate::keybinds::mouse_token(ev.button).and_then(|t| keymap.action(mods, t))
        } else {
            latches.mouse.remove(&ev.button).or_else(|| {
                crate::keybinds::mouse_token(ev.button).and_then(|t| keymap.action(mods, t))
            })
        };
        // A Ctrl-click is a chord: Ctrl's release is no crouch.
        if pressed && input.ctrl_held {
            input.crouch_chord = true;
        }
        let Some(action) = action else { continue };
        // At a ship's helm the hands are on the wheel: no shot, no sights.
        if pressed && state.helm && matches!(action, Action::Fire | Action::Aim) {
            continue;
        }
        if pressed {
            latches.mouse.insert(ev.button, action);
        }
        if let Some(c) = control_mut(input, action) {
            c.apply(ev.state);
        } else if let Some(slot) = slot_of(action).filter(|_| world || !pressed) {
            slot_input(input, slot, ev.state, mods.alt);
        } else if let Some(set) = window_key(&state, action).filter(|_| pressed) {
            set(input);
        }
    }

    for ev in keys_raw.read() {
        let code = ev.key_code;
        // The Key Bindings window is taking a key: every key is the window's.
        if state.key_bindings {
            continue;
        }
        // The duel window's keys are its own: no action bar, hotkey or slot gets them.
        if state.duel_keys && !state.chat_focus && is_duel_key(code) {
            if ev.state == ButtonState::Pressed && !ev.repeat {
                input.duel.push(code);
            }
            blank(ev);
            continue;
        }
        // A layer window taking typing gets every key; WoW's bindings get none.
        if state.typing {
            if ev.state == ButtonState::Pressed {
                match code {
                    KeyCode::Backspace => input.backspace = true,
                    KeyCode::Enter | KeyCode::NumpadEnter => input.enter = true,
                    KeyCode::Escape => input.escape = true,
                    _ => {
                        if let Some(t) = &ev.text {
                            input.typed.push_str(t);
                        }
                    }
                }
            }
            blank(ev);
            continue;
        }
        let pressed = ev.state == ButtonState::Pressed;
        // At a ship's helm its keys steer (crate::ships reads them off the plane): none of them is
        // a jump, Launch (Ctrl+Space), the X switch or WoW's movement. Esc leaves the helm unless
        // a window is open to close first.
        if state.helm && !state.chat_focus && is_helm_key(code) {
            if code != KeyCode::Escape {
                blank(ev);
                continue;
            }
            if !(state.panel || state.build_menu) {
                if pressed && !ev.repeat {
                    input.leave_helm = true;
                }
                blank(ev);
                continue;
            }
        }
        // Ctrl+Alt (Left Alt: AltGr is Ctrl with Right Alt): the locked free cursor, on or off.
        // Neither is then crouch's Ctrl nor the off hand's Alt.
        let ctrl_key = matches!(code, KeyCode::ControlLeft | KeyCode::ControlRight);
        let alt_key = matches!(code, KeyCode::AltLeft | KeyCode::AltRight);
        let left_alt = keys.pressed(CURSOR) || code == CURSOR;
        if pressed
            && !ev.repeat
            && !state.chat_focus
            && state.in_world
            && ((ctrl_key && left_alt) || (code == CURSOR && mods.ctrl))
        {
            input.cursor_lock = true;
            input.crouch_chord = true;
        }
        // Any other key going down while Ctrl is held makes Ctrl a chord's modifier.
        if pressed && !ev.repeat && input.ctrl_held && code != CROUCH_KEY {
            input.crouch_chord = true;
        }
        // Ctrl+Space: Launch (crate::companion), never a jump nor a crouch.
        if code == KeyCode::Space && mods.ctrl && world && !state.chat_focus {
            if pressed && !ev.repeat {
                input.sky_surge = true;
            }
            keys.reset(code);
            logical.reset(ev.logical_key.clone());
            blank(ev);
            continue;
        }
        if ctrl_key {
            mods.ctrl = pressed;
        }
        if matches!(code, KeyCode::ShiftLeft | KeyCode::ShiftRight) {
            mods.shift = pressed;
        }
        if alt_key {
            mods.alt = pressed;
        }
        let token = benilla_app::fusion::binding_key_name(code, layout);
        let latched = (!pressed)
            .then(|| latches.keys.remove(&code).map(|(a, _)| a))
            .flatten();
        let action = latched.or_else(|| token.as_deref().and_then(|t| keymap.action(mods, t)));
        if !pressed {
            latches.jump.remove(&code);
        }
        // In a companion duel the companion is the duel's: C shows no Companion bar to command it.
        if state.duel_keys && !state.chat_focus && action == Some(Action::Companion) {
            blank(ev);
            continue;
        }
        // A killstreak in hand: Escape lets it go (the rocket bails, the turret goes back);
        // steering the rocket, the body takes no keys at all.
        if (state.placing || state.piloting) && !state.chat_focus {
            if code == KeyCode::Escape {
                if pressed && !ev.repeat {
                    input.streak_cancel = true;
                }
                blank(ev);
                continue;
            }
            if state.piloting {
                blank(ev);
                continue;
            }
        }
        // Building: the piece keys go through the pieces (never WoW's bindings).
        if state.building
            && !state.chat_focus
            && let Some(a @ (Action::BuildPrev | Action::BuildNext)) = action
        {
            if pressed {
                input.build_cycle += if a == Action::BuildPrev { -1 } else { 1 };
            }
            blank(ev);
            continue;
        }
        // The pet bar's keys (Ctrl+1..Ctrl+0): the companion's, the gun up or not, the cursor out
        // or not (never the gun's slot 1 with Ctrl held).
        if world && let Some(slot) = token.as_deref().and_then(|t| keymap.pet_slot(mods, t)) {
            if pressed && !ev.repeat {
                input.pet_slot = Some(slot);
            }
            keys.reset(code);
            logical.reset(ev.logical_key.clone());
            blank(ev);
            continue;
        }
        // The action bar's slot keys, whoever has the mouse (Alt held: the off hand); a release
        // always reaches the slot its press went to.
        if let Some(slot) = action.and_then(slot_of)
            && (world || latched.is_some())
        {
            if !ev.repeat {
                slot_input(input, slot, ev.state, mods.alt);
            }
            if pressed {
                latches.keys.insert(code, (action.expect("a slot"), ev.logical_key.clone()));
            }
            keys.reset(code);
            logical.reset(ev.logical_key.clone());
            blank(ev);
            continue;
        }
        // The windows' keys, the view and the systems' keys, whoever owns input.
        if let Some(set) = action.and_then(|a| window_key(&state, a)) {
            if pressed && !ev.repeat {
                set(input);
            }
            // The window it opens takes input from the gun, so the owned-key reset below would
            // skip it. Clear it here whoever owns input.
            keys.reset(code);
            logical.reset(ev.logical_key.clone());
            blank(ev);
            continue;
        }
        if state.panel
            && !state.chat_focus
            && let Some(nav) = nav_key(code)
        {
            if pressed {
                input.nav = Some(nav);
            }
            blank(ev);
            continue;
        }
        if code == KeyCode::Escape
            && escape_closes_panel(
                state.panel || state.build_menu || state.building,
                state.chat_focus,
            )
        {
            if pressed && !ev.repeat {
                input.escape = true;
            }
            blank(ev);
            continue;
        }
        if owns && token.as_deref().is_some_and(|t| keymap.is_jump(mods, t)) {
            if !ev.repeat {
                input.jump.apply(ev.state);
            }
            if pressed {
                latches.jump.insert(code, ev.logical_key.clone());
            }
            if input.blank_jump {
                blank(ev);
            }
            continue;
        }
        if owns && code == CROUCH_KEY {
            // Crouch toggles on the release, unless Ctrl was a chord's modifier.
            if !ev.repeat {
                if pressed {
                    input.ctrl_held = true;
                    input.crouch_chord = mods.alt;
                    input.ctrl_down_at = now;
                    input.ctrl_went_prone = false;
                } else {
                    if input.ctrl_held && !input.crouch_chord && !input.ctrl_went_prone {
                        input.crouch.pressed = true;
                    }
                    input.ctrl_held = false;
                }
            }
            blank(ev);
            continue;
        }
        let Some(action) = action.filter(|a| GUN_ACTIONS.contains(a)) else {
            continue;
        };
        if !owns {
            continue;
        }
        if !ev.repeat
            && let Some(c) = control_mut(input, action)
        {
            c.apply(ev.state);
        }
        if pressed {
            latches.keys.insert(code, (action, ev.logical_key.clone()));
        }
        blank(ev);
    }

    // The mouse buttons the gun's actions are bound to (the left, right and middle by default).
    let gun_buttons: Vec<MouseButton> = GUN_ACTIONS
        .iter()
        .flat_map(|a| keymap.mouse_buttons(*a))
        .collect();
    if owns {
        // Cut from benilla's planes: no world click, no camera drag, no both-button run.
        for b in &gun_buttons {
            mouse.reset(*b);
        }
        if input.blank_jump {
            for (code, key) in &latches.jump {
                keys.reset(*code);
                logical.reset(key.clone());
            }
        }
        keys.reset(CROUCH_KEY);
        // A bare Ctrl held long enough goes prone while still held (MW2's held crouch).
        if input.ctrl_held
            && !input.crouch_chord
            && !input.ctrl_went_prone
            && now - input.ctrl_down_at >= CTRL_HOLD_FOR_PRONE
        {
            input.prone.pressed = true;
            input.ctrl_went_prone = true;
        }
        for (code, (_, key)) in &latches.keys {
            keys.reset(*code);
            logical.reset(key.clone());
        }
    } else {
        // A press made while the interface had the mouse is not a shot.
        for c in [
            &mut input.fire,
            &mut input.ads,
            &mut input.reload,
            &mut input.crouch,
            &mut input.prone,
            &mut input.build,
            &mut input.interact,
            &mut input.middle,
            &mut input.jump,
        ] {
            *c = Control::default();
        }
        input.ctrl_held = false;
    }
    // A killstreak in hand takes the buttons from the gun (and, steering, the mouse from the
    // body's turn and every key from its feet).
    let streak_buttons: Vec<MouseButton> = [Action::Fire, Action::Aim]
        .iter()
        .flat_map(|a| keymap.mouse_buttons(*a))
        .collect();
    streak_controls(
        &state,
        input,
        &mut motion,
        &mut mouse,
        &mut keys,
        &mut logical,
        &streak_buttons,
    );
}

/// While a killstreak has the controls: the gun's buttons become the streak's; steering the rocket
/// the mouse's move is the rocket's (not mouselook's turn) and no key or button reaches the body.
fn streak_controls(
    state: &FpsState,
    input: &mut Mw2Input,
    motion: &mut AccumulatedMouseMotion,
    mouse: &mut ButtonInput<MouseButton>,
    keys: &mut ButtonInput<KeyCode>,
    logical: &mut ButtonInput<Key>,
    buttons: &[MouseButton],
) {
    if !state.placing && !state.piloting {
        input.streak_fire = Control::default();
        input.streak_alt = Control::default();
        return;
    }
    input.streak_fire = std::mem::take(&mut input.fire);
    input.streak_alt = std::mem::take(&mut input.ads);
    for b in buttons {
        mouse.reset(*b);
    }
    if state.piloting {
        input.pilot_look = motion.delta;
        motion.delta = Vec2::ZERO;
        input.killstreak = Control::default();
        if !state.chat_focus {
            mouse.reset_all();
            keys.reset_all();
            logical.reset_all();
        }
    }
}

/// A layer window's keyboard navigation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Nav {
    Up,
    Down,
    Left,
    Right,
    Enter,
}

/// The navigation a key gives in a layer window.
pub fn nav_key(code: KeyCode) -> Option<Nav> {
    match code {
        KeyCode::ArrowUp => Some(Nav::Up),
        KeyCode::ArrowDown => Some(Nav::Down),
        KeyCode::ArrowLeft => Some(Nav::Left),
        KeyCode::ArrowRight => Some(Nav::Right),
        KeyCode::Enter | KeyCode::NumpadEnter => Some(Nav::Enter),
        _ => None,
    }
}

/// Whether Esc closes a layer window: not while WoW's chat has the keyboard (Esc leaves the
/// chat line first, as in WoW).
pub fn escape_closes_panel(panel: bool, chat_focus: bool) -> bool {
    panel && !chat_focus
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::keybinds::Keymap;

    #[test]
    fn the_world_keys_are_taken_in_the_world_unless_typing() {
        let mut s = FpsState {
            in_world: true,
            active: true,
            ..Default::default()
        };
        // Whoever has the mouse: the windows, the view, X, Z and the pages.
        for a in [
            Action::Journal,
            Action::Companions,
            Action::Construction,
            Action::Build,
            Action::Guide,
            Action::Kits,
            Action::Talents,
            Action::Abilities,
            Action::View,
            Action::Switch,
            Action::Sheathe,
            Action::PagePrev,
            Action::PageNext,
        ] {
            assert!(window_key(&s, a).is_some(), "{a:?}");
        }
        s.chat_focus = true;
        assert!(window_key(&s, Action::Switch).is_none(), "typing in chat");
        assert!(window_key(&s, Action::Kits).is_none());
        s.chat_focus = false;
        s.typing = true;
        assert!(window_key(&s, Action::Companions).is_none(), "naming a kit");
        s.typing = false;
        assert!(
            window_key(&s, Action::Reload).is_none(),
            "a gun key is no window's"
        );
        assert!(window_key(&s, Action::Slot1).is_none(), "a slot is a control");
        s.in_world = false;
        assert!(window_key(&s, Action::View).is_none(), "the glue screens");
    }

    #[test]
    fn escape_leaves_chat_before_it_closes_a_window() {
        assert!(escape_closes_panel(true, false));
        assert!(!escape_closes_panel(true, true)); // WoW's chat takes it first
        assert!(!escape_closes_panel(false, false));
    }

    #[test]
    fn a_smooth_scroll_swaps_once() {
        assert!(wheel_swap_allowed(10.0, None));
        assert!(!wheel_swap_allowed(10.1, Some(10.0))); // the same flick, still turning
        assert!(wheel_swap_allowed(10.31, Some(10.0)));
    }

    #[test]
    fn prone_turning_damped_past_60() {
        assert_eq!(prone_yaw_scale(30.0), 1.0);
        assert_eq!(prone_yaw_scale(-59.0), 1.0);
        assert_eq!(prone_yaw_scale(75.0), 0.25);
        assert_eq!(prone_yaw_scale(-120.0), 0.25);
    }

    #[test]
    fn edges_fire_once() {
        let mut c = Control::default();
        c.begin_frame();
        c.apply(ButtonState::Pressed);
        assert!(c.pressed && c.held && !c.released);
        c.begin_frame();
        c.apply(ButtonState::Pressed);
        assert!(!c.pressed && c.held);
        c.begin_frame();
        c.apply(ButtonState::Released);
        assert!(c.released && !c.held);
        c.begin_frame();
        c.apply(ButtonState::Released);
        assert!(!c.released);
    }

    /// The capture pass on its own, over a [`Keymap`].
    fn app(keymap: Keymap, state: FpsState) -> App {
        let mut app = App::new();
        app.add_message::<KeyboardInput>()
            .add_message::<MouseButtonInput>()
            .add_message::<MouseWheel>()
            .init_resource::<Mw2Input>()
            .init_resource::<Time>()
            .init_resource::<ButtonInput<KeyCode>>()
            .init_resource::<ButtonInput<MouseButton>>()
            .init_resource::<ButtonInput<Key>>()
            .init_resource::<AccumulatedMouseMotion>()
            .insert_resource(keymap)
            .insert_resource(state)
            .add_systems(Update, capture_input);
        app
    }

    fn owning() -> FpsState {
        FpsState {
            in_world: true,
            active: true,
            owns_input: true,
            ..Default::default()
        }
    }

    fn key(app: &mut App, code: KeyCode, ch: &str, state: ButtonState) {
        let window = Entity::PLACEHOLDER;
        // Bevy's own pass would keep the plane: what the layer reads a held modifier from.
        let mut plane = app.world_mut().resource_mut::<ButtonInput<KeyCode>>();
        match state {
            ButtonState::Pressed => plane.press(code),
            ButtonState::Released => plane.release(code),
        }
        app.world_mut().write_message(KeyboardInput {
            key_code: code,
            logical_key: Key::Character(ch.into()),
            state,
            text: None,
            repeat: false,
            window,
        });
        app.update();
    }

    fn input(app: &App) -> &Mw2Input {
        app.world().resource::<Mw2Input>()
    }

    fn keymap(pairs: &[(&str, &str)]) -> Keymap {
        let mut all: Vec<(&str, &str)> = crate::keybinds::ACTIONS
            .iter()
            .filter_map(|d| d.default.map(|k| (k, d.command)))
            .filter(|(_, c)| !pairs.iter().any(|(_, p)| p == c))
            .collect();
        all.extend_from_slice(pairs);
        Keymap::from_pairs(all)
    }

    #[test]
    fn the_default_keys_work_the_gun_the_bar_and_the_windows() {
        let mut a = app(Keymap::defaults(), owning());
        key(&mut a, KeyCode::KeyR, "r", ButtonState::Pressed);
        assert!(input(&a).reload.pressed);
        key(&mut a, KeyCode::KeyR, "r", ButtonState::Released);
        assert!(input(&a).reload.released);
        key(&mut a, KeyCode::KeyN, "n", ButtonState::Pressed);
        assert!(input(&a).companions, "N opens Companions");
        assert!(!input(&a).talents, "N is not talents");
        key(&mut a, KeyCode::KeyB, "b", ButtonState::Pressed);
        assert!(input(&a).professions, "B opens the Professions hub");
        assert!(!input(&a).construction, "B is not the homestead window");
        assert!(!input(&a).build.pressed, "B never enters build mode itself");
        key(&mut a, KeyCode::KeyK, "k", ButtonState::Pressed);
        assert!(input(&a).cac, "K: the kits");
        // The slot keys: 1-4, Q and E.
        for (i, (code, ch)) in [
            (KeyCode::Digit1, "1"),
            (KeyCode::Digit2, "2"),
            (KeyCode::Digit3, "3"),
            (KeyCode::Digit4, "4"),
            (KeyCode::KeyQ, "q"),
            (KeyCode::KeyE, "e"),
        ]
        .into_iter()
        .enumerate()
        {
            key(&mut a, code, ch, ButtonState::Pressed);
            assert!(input(&a).slots[i].pressed, "{ch} is slot {}", i + 1);
            assert!(!input(&a).slot_alt[i]);
            key(&mut a, code, ch, ButtonState::Released);
            assert!(input(&a).slots[i].released);
        }
        key(&mut a, KeyCode::KeyX, "x", ButtonState::Pressed);
        assert!(input(&a).switch_system, "X switches");
        key(&mut a, KeyCode::KeyZ, "z", ButtonState::Pressed);
        assert!(input(&a).sheathe, "Z puts away");
        key(&mut a, KeyCode::KeyV, "v", ButtonState::Pressed);
        assert!(input(&a).view, "V: the view");
        // The freed keys do nothing of ours.
        for (code, ch) in [
            (KeyCode::KeyG, "g"),
            (KeyCode::KeyT, "t"),
            (KeyCode::KeyY, "y"),
            (KeyCode::KeyH, "h"),
            (KeyCode::Tab, ""),
            (KeyCode::F5, ""),
            (KeyCode::F10, ""),
        ] {
            key(&mut a, code, ch, ButtonState::Pressed);
            let i = input(&a);
            assert!(
                !i.achievements && !i.view && !i.switch_system,
                "{code:?}"
            );
            assert!(i.slots.iter().all(|c| !c.pressed), "{code:?}");
        }
    }

    /// The duel window open: 1-0 and E are its, blanked for WoW's bindings and never a slot,
    /// the page or a hotkey; other keys work as ever.
    #[test]
    fn the_duel_window_takes_its_keys() {
        let mut a = app(
            Keymap::defaults(),
            FpsState {
                duel_keys: true,
                ..owning()
            },
        );
        for (code, ch) in [
            (KeyCode::Digit1, "1"),
            (KeyCode::Digit0, "0"),
            (KeyCode::KeyE, "e"),
        ] {
            key(&mut a, code, ch, ButtonState::Pressed);
            let i = input(&a);
            assert_eq!(i.duel, vec![code], "{ch} is the duel's");
            assert!(i.slots.iter().all(|s| !s.pressed), "{ch} is no slot");
            assert_eq!(i.page, 0);
            key(&mut a, code, ch, ButtonState::Released);
        }
        // What reaches WoW's bindings is blanked.
        let mut events = a.world_mut().resource_mut::<Messages<KeyboardInput>>();
        let left: Vec<_> = events.drain().map(|e| e.key_code).collect();
        assert!(
            left.iter().all(|k| !is_duel_key(*k)),
            "a duel key got through: {left:?}"
        );
        // Other keys still work; chat keeps its digits.
        key(&mut a, KeyCode::KeyR, "r", ButtonState::Pressed);
        assert!(input(&a).reload.pressed);
        a.world_mut().resource_mut::<FpsState>().chat_focus = true;
        key(&mut a, KeyCode::Digit2, "2", ButtonState::Pressed);
        assert!(input(&a).duel.is_empty());
    }

    /// What one key press did, as the matrix names it.
    fn did(i: &Mw2Input) -> Vec<&'static str> {
        let mut v = Vec::new();
        if i.jump.pressed {
            v.push("jump");
        }
        if i.sky_surge {
            v.push("launch");
        }
        if i.switch_system {
            v.push("switch");
        }
        if i.companion_system {
            v.push("companion bar");
        }
        if i.sheathe {
            v.push("sheathe");
        }
        if i.reload.pressed {
            v.push("reload");
        }
        if i.leave_helm {
            v.push("leave helm");
        }
        if i.escape {
            v.push("escape");
        }
        if !i.duel.is_empty() {
            v.push("duel");
        }
        if i.pet_slot.is_some() {
            v.push("pet slot");
        }
        if i.build_cycle != 0 {
            v.push("build cycle");
        }
        if i.slots.iter().any(|s| s.pressed) {
            v.push("slot");
        }
        if !i.typed.is_empty() {
            v.push("typed");
        }
        v
    }

    /// Every mode the layer's input knows, by every key that has meant two things: what each one
    /// does there. The rows are the modes, the columns the keys (Ctrl+ is a chord).
    #[test]
    fn the_mode_by_key_matrix() {
        let foot = owning();
        let modes: [(&str, FpsState); 7] = [
            ("on foot", foot.clone()),
            ("at the helm", FpsState { helm: true, ..foot.clone() }),
            ("in a duel", FpsState { duel_keys: true, panel: true, ..foot.clone() }),
            ("building", FpsState { building: true, ..foot.clone() }),
            ("in chat", FpsState { chat_focus: true, owns_input: false, ..foot.clone() }),
            ("typing in a window", FpsState { typing: true, ..foot.clone() }),
            ("the cursor out (Alt, a window, dead)", FpsState { owns_input: false, ..foot.clone() }),
        ];
        // (key, ctrl held, [expected per mode, in the modes' order]).
        type Row = (KeyCode, &'static str, bool, [&'static [&'static str]; 7]);
        let rows: &[Row] = &[
            (KeyCode::Space, " ", false, [&["jump"], &[], &["jump"], &["jump"], &[], &["typed"], &[]]),
            (KeyCode::Space, " ", true, [&["launch"], &[], &["launch"], &["launch"], &[], &["typed"], &["launch"]]),
            (KeyCode::KeyX, "x", false, [&["switch"], &[], &["switch"], &["switch"], &[], &["typed"], &["switch"]]),
            (KeyCode::KeyC, "c", false, [&["companion bar"], &["companion bar"], &[], &["companion bar"], &[], &["typed"], &["companion bar"]]),
            (KeyCode::KeyZ, "z", false, [&["sheathe"], &["sheathe"], &["sheathe"], &["sheathe"], &[], &["typed"], &["sheathe"]]),
            (KeyCode::KeyE, "e", false, [&["slot"], &["slot"], &["duel"], &["slot"], &[], &["typed"], &["slot"]]),
            (KeyCode::Digit1, "1", false, [&["slot"], &["slot"], &["duel"], &["slot"], &[], &["typed"], &["slot"]]),
            (KeyCode::Digit1, "1", true, [&["pet slot"], &["pet slot"], &["duel"], &["pet slot"], &[], &["typed"], &["pet slot"]]),
            (KeyCode::KeyR, "r", false, [&["reload"], &["reload"], &["reload"], &["reload"], &[], &["typed"], &[]]),
            (KeyCode::Escape, "", false, [&[], &["leave helm"], &["escape"], &["escape"], &[], &["escape"], &[]]),
        ];
        let mut wrong = Vec::new();
        for (code, ch, ctrl, want) in rows {
            for ((name, mode), want) in modes.iter().zip(want.iter()) {
                let mut a = app(Keymap::defaults(), mode.clone());
                if *ctrl {
                    key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
                }
                let typed = if mode.typing { *ch } else { "" };
                let window = Entity::PLACEHOLDER;
                a.world_mut().resource_mut::<ButtonInput<KeyCode>>().press(*code);
                a.world_mut().write_message(KeyboardInput {
                    key_code: *code,
                    logical_key: Key::Character((*ch).into()),
                    state: ButtonState::Pressed,
                    text: (!typed.is_empty()).then(|| typed.into()),
                    repeat: false,
                    window,
                });
                a.update();
                let got = did(input(&a));
                if got.as_slice() != *want {
                    let chord = if *ctrl { "Ctrl+" } else { "" };
                    wrong.push(format!("{name}: {chord}{code:?} did {got:?}, wanted {want:?}"));
                }
            }
        }
        assert!(wrong.is_empty(), "mode x key mismatches:
{}", wrong.join("
"));
    }

    #[test]
    fn alt_with_a_slot_key_is_the_off_hand_and_works_with_the_cursor_out() {
        let mut s = owning();
        s.owns_input = false; // Alt held: the cursor is out
        let mut a = app(Keymap::defaults(), s);
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::Digit3, "3", ButtonState::Pressed);
        assert!(input(&a).slots[2].pressed && input(&a).slot_alt[2]);
        // Alt let go first: the release still ends slot 3's press, held as the off hand's.
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Released);
        key(&mut a, KeyCode::Digit3, "3", ButtonState::Released);
        assert!(input(&a).slots[2].released && input(&a).slot_alt[2]);
        key(&mut a, KeyCode::Digit3, "3", ButtonState::Pressed);
        assert!(!input(&a).slot_alt[2], "a plain press is the main hand");
        // Shift (sprint) held: still the slot, never another page.
        key(&mut a, KeyCode::ShiftLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::KeyQ, "Q", ButtonState::Pressed);
        assert!(input(&a).slots[4].pressed && input(&a).page == 0);
        // Typing in chat, the keys are WoW's.
        let mut s = owning();
        s.chat_focus = true;
        let mut a = app(Keymap::defaults(), s);
        key(&mut a, KeyCode::Digit1, "1", ButtonState::Pressed);
        assert!(!input(&a).slots[0].pressed);
    }

    fn wheel(app: &mut App, y: f32) {
        app.world_mut()
            .resource_mut::<Time>()
            .advance_by(std::time::Duration::from_secs(1));
        app.world_mut().write_message(MouseWheel {
            unit: bevy::input::mouse::MouseScrollUnit::Line,
            x: 0.0,
            y,
            window: Entity::PLACEHOLDER,
        });
        app.update();
    }

    #[test]
    fn only_alt_with_the_wheel_turns_the_page() {
        let mut a = app(Keymap::defaults(), owning());
        wheel(&mut a, 1.0);
        assert_eq!(input(&a).page, 0, "the bare wheel swaps guns");
        assert!(input(&a).wheel_swap);
        key(&mut a, KeyCode::ShiftLeft, "", ButtonState::Pressed);
        wheel(&mut a, -1.0);
        assert_eq!(input(&a).page, 0, "Shift (sprint) with the wheel: no page");
        key(&mut a, KeyCode::ShiftLeft, "", ButtonState::Released);
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Pressed);
        wheel(&mut a, -1.0);
        assert_eq!(input(&a).page, 1, "Alt and the wheel down: the next page");
        assert!(!input(&a).wheel_swap, "and no gun swap");
        wheel(&mut a, 1.0);
        assert_eq!(input(&a).page, -1, "up: the page before");
        // With the cursor out (Alt held) too.
        let mut s = owning();
        s.owns_input = false;
        let mut a = app(Keymap::defaults(), s);
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Pressed);
        wheel(&mut a, -1.0);
        assert_eq!(input(&a).page, 1);
    }

    /// The last wheel message as the camera would read it after the capture pass.
    fn last_wheel(app: &App) -> Option<f32> {
        let messages = app.world().resource::<Messages<MouseWheel>>();
        messages.get_cursor().read(messages).last().map(|m| m.y)
    }

    /// The wheel as Bevy's input pass leaves it: the message and the accumulated scroll benilla's
    /// camera zoom reads.
    fn scroll(app: &mut App, y: f32) -> f32 {
        app.world_mut()
            .resource_mut::<AccumulatedMouseScroll>()
            .delta = Vec2::new(0.0, y);
        wheel(app, y);
        app.world().resource::<AccumulatedMouseScroll>().delta.y
    }

    #[test]
    fn alt_wheel_never_reaches_the_third_person_zoom() {
        let mut a = app(Keymap::defaults(), owning());
        a.init_resource::<AccumulatedMouseScroll>();
        let mut t = crate::thirdperson::ThirdPerson::default();
        t.on = true;
        a.insert_resource(t);
        assert_eq!(scroll(&mut a, 1.0), 1.0, "the bare wheel zooms in third person");
        assert!(!input(&a).wheel_swap, "and swaps no weapon");
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Pressed);
        assert_eq!(scroll(&mut a, -1.0), 0.0, "Alt+wheel: the page, no zoom");
        assert_eq!(input(&a).page, 1);
        // The cursor out (Alt frees it) changes nothing.
        let mut s = owning();
        s.owns_input = false;
        a.insert_resource(s);
        assert_eq!(scroll(&mut a, 1.0), 0.0, "never a zoom while Alt is held");
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Released);
        a.insert_resource(owning());
        assert_eq!(scroll(&mut a, 1.0), 1.0, "Alt let go: the zoom is back");
    }

    #[test]
    fn third_person_wheel_zooms_and_never_swaps() {
        let mut a = app(Keymap::defaults(), owning());
        let mut t = crate::thirdperson::ThirdPerson::default();
        t.on = true;
        a.insert_resource(t);
        wheel(&mut a, 1.0);
        assert!(!input(&a).wheel_swap, "no weapon swap in third person");
        assert_eq!(input(&a).wheel, 0.0);
        // The wheel event is left for WoW's camera zoom.
        let left = last_wheel(&a);
        assert_eq!(left, Some(1.0));
        // Alt with the wheel still turns the page, and the camera does not zoom.
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Pressed);
        wheel(&mut a, -1.0);
        assert_eq!(input(&a).page, 1);
        let left = last_wheel(&a);
        assert_eq!(left, Some(0.0), "Alt+wheel never zooms");
    }

    #[test]
    fn alt_wheel_never_zooms_even_unbound() {
        let mut s = owning();
        s.owns_input = false; // Alt held: the cursor is out
        let mut a = app(Keymap::default(), s);
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Pressed);
        wheel(&mut a, 1.0);
        let left = last_wheel(&a);
        assert_eq!(left, Some(0.0));
    }

    #[test]
    fn ctrl_alt_locks_the_cursor_without_crouching() {
        let mut a = app(Keymap::defaults(), owning());
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Pressed);
        assert!(input(&a).cursor_lock, "Ctrl then Alt");
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Released);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        assert!(!input(&a).crouch.pressed, "no crouch from the chord's Ctrl");
        assert!(!input(&a).cursor_lock, "once");
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Pressed);
        assert!(!input(&a).cursor_lock, "a bare Alt only frees it while held");
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        assert!(input(&a).cursor_lock, "Alt then Ctrl");
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        key(&mut a, KeyCode::AltLeft, "", ButtonState::Released);
        assert!(!input(&a).crouch.pressed);
        assert!(input(&a).slots.iter().all(|c| !c.pressed), "no off-hand slot");
        // A bare Ctrl still crouches.
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        assert!(input(&a).crouch.pressed);
    }

    #[test]
    fn a_rebound_key_moves_what_the_layer_reacts_to() {
        let map = keymap(&[
            ("T", "WOMW_RELOAD"),
            ("SHIFT-N", "WOMW_COMPANIONS"),
            ("N", "WOMW_TALENTS"),
            ("J", "WOMW_BUILD"),
            ("O", "WOMW_GUIDE"),
        ]);
        let mut a = app(map, owning());
        key(&mut a, KeyCode::KeyR, "r", ButtonState::Pressed);
        assert!(!input(&a).reload.pressed, "R is no reload now");
        key(&mut a, KeyCode::KeyT, "t", ButtonState::Pressed);
        assert!(input(&a).reload.pressed, "T is");
        key(&mut a, KeyCode::KeyN, "n", ButtonState::Pressed);
        assert!(input(&a).talents && !input(&a).companions);
        key(&mut a, KeyCode::KeyN, "n", ButtonState::Released);
        key(&mut a, KeyCode::ShiftLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::KeyN, "N", ButtonState::Pressed);
        assert!(input(&a).companions, "Shift+N");
        key(&mut a, KeyCode::ShiftLeft, "", ButtonState::Released);
        key(&mut a, KeyCode::KeyB, "b", ButtonState::Pressed);
        assert!(!input(&a).professions);
        key(&mut a, KeyCode::KeyJ, "j", ButtonState::Pressed);
        assert!(input(&a).professions);
        key(&mut a, KeyCode::KeyO, "o", ButtonState::Pressed);
        assert!(input(&a).guide);
        // An unbound action reacts to nothing.
        let mut a = app(keymap(&[]), owning());
        key(&mut a, KeyCode::KeyO, "o", ButtonState::Pressed);
        assert!(!input(&a).guide);
    }

    #[test]
    fn the_key_bindings_window_takes_every_key() {
        let mut s = owning();
        s.key_bindings = true;
        let mut a = app(Keymap::defaults(), s);
        for (code, ch) in [
            (KeyCode::KeyR, "r"),
            (KeyCode::KeyX, "x"),
            (KeyCode::Digit1, "1"),
        ] {
            key(&mut a, code, ch, ButtonState::Pressed);
        }
        let i = input(&a);
        assert!(!i.reload.pressed && !i.switch_system && !i.slots[0].pressed);
    }

    #[test]
    fn ctrl_with_a_number_is_the_pet_bar_and_no_crouch() {
        let mut a = app(Keymap::defaults(), owning());
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::Digit1, "1", ButtonState::Pressed);
        assert_eq!(input(&a).pet_slot, Some(0));
        assert!(!input(&a).slots[0].pressed, "not slot 1");
        key(&mut a, KeyCode::Digit1, "1", ButtonState::Released);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        assert!(!input(&a).crouch.pressed);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        assert!(input(&a).crouch.pressed, "a bare Ctrl crouches");
    }

    #[test]
    fn a_held_ctrl_goes_prone_and_a_tap_crouches() {
        let mut a = app(Keymap::defaults(), owning());
        let step = |a: &mut App, secs: f32| {
            a.world_mut()
                .resource_mut::<Time>()
                .advance_by(std::time::Duration::from_secs_f32(secs));
            a.update();
        };
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        step(&mut a, 0.1);
        assert!(!input(&a).prone.pressed, "not yet");
        step(&mut a, CTRL_HOLD_FOR_PRONE);
        assert!(input(&a).prone.pressed, "held: prone, while still held");
        step(&mut a, 0.5);
        assert!(!input(&a).prone.pressed, "once per hold");
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        assert!(!input(&a).crouch.pressed, "the held release is no crouch");
        // A tap still crouches.
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        step(&mut a, 0.1);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        assert!(input(&a).crouch.pressed);
        assert!(!input(&a).prone.pressed);
        // A held chord (Ctrl+1) never goes prone.
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::Digit1, "1", ButtonState::Pressed);
        step(&mut a, 1.0);
        assert!(!input(&a).prone.pressed, "a chord's Ctrl is no prone");
        key(&mut a, KeyCode::Digit1, "1", ButtonState::Released);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
    }

    #[test]
    fn ctrl_space_is_sky_surge_never_a_crouch_or_a_jump() {
        let mut a = app(Keymap::defaults(), owning());
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::Space, " ", ButtonState::Pressed);
        assert!(input(&a).sky_surge);
        assert!(!input(&a).jump.pressed, "no jump");
        key(&mut a, KeyCode::Space, " ", ButtonState::Released);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        assert!(!input(&a).crouch.pressed, "no crouch from the chord's Ctrl");
    }

    #[test]
    fn c_is_the_companion_bar_and_a_ctrl_click_no_crouch() {
        let mut a = app(Keymap::defaults(), owning());
        key(&mut a, KeyCode::KeyC, "c", ButtonState::Pressed);
        assert!(input(&a).companion_system, "C: the Companion bar");
        assert!(!input(&a).crouch.pressed, "C crouches no more");
        key(&mut a, KeyCode::KeyC, "c", ButtonState::Released);
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Pressed);
        a.world_mut().write_message(MouseButtonInput {
            button: MouseButton::Left,
            state: ButtonState::Pressed,
            window: Entity::PLACEHOLDER,
        });
        a.update();
        assert!(input(&a).fire.pressed, "Ctrl held still fires");
        key(&mut a, KeyCode::ControlLeft, "", ButtonState::Released);
        assert!(!input(&a).crouch.pressed, "a Ctrl-click is no crouch");
    }

    #[test]
    fn a_release_after_its_modifier_still_ends_the_press() {
        let mut a = app(Keymap::defaults(), owning());
        key(&mut a, KeyCode::ShiftLeft, "", ButtonState::Pressed);
        key(&mut a, KeyCode::KeyR, "R", ButtonState::Pressed);
        assert!(input(&a).reload.held, "Shift (sprint) held: still the reload");
        key(&mut a, KeyCode::ShiftLeft, "", ButtonState::Released);
        key(&mut a, KeyCode::KeyR, "r", ButtonState::Released);
        assert!(input(&a).reload.released && !input(&a).reload.held);
    }
}
