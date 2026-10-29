//! The Minstrelsy trainer's window (the server's `Minstrelsy::Window`, channel 10 ops 60-62): a
//! Minstrel Trainer opens it with its buttons (train the next tier, learn an instrument's recipe,
//! buy a starter Hand Drum). Instruments are made in the stock trade window
//! (crate::professions, the Minstrelsy key or the Professions hub), so the server's "Make" lines
//! are left out here, as a stock trainer's window never crafts. It shows the
//! tier and skill, and every song by instrument in its skill colour (red: not yet; orange, yellow,
//! green: teaching; gray: no more), with what it does; each answer shows in its status line.

use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use bevy::prelude::*;

use super::rules::{self, Colour, Instrument};
use super::{CHANNEL, money_text, str_at, u16_at, u32_at};
use crate::Mw2Set;
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, Kind, RED, WHITE, WindowSpec, WowButton, WowTip, text, u,
};

pub const OP_ASK_WINDOW: u8 = 60;
pub const OP_ACT: u8 = 61;
pub const OP_WINDOW: u8 = 62;
pub const ACT_TRAIN: u8 = 1;
pub const ACT_BUY: u8 = 2;
pub const ACT_LEARN: u8 = 3;
/// Making an instrument works anywhere (the window from the key too).
pub const ACT_MAKE: u8 = 4;

/// Whether a line's button may be pressed in this window.
pub fn pressable(s: &Service, trainer: bool) -> bool {
    s.enabled && s.action != 0 && (trainer || s.action == ACT_MAKE)
}

/// Whether a service is a line of this window: the trainer's buttons, not the makes (the trade
/// window's Create).
pub fn shown(s: &Service) -> bool {
    s.action != 0 && s.action != ACT_MAKE
}

/// A button of the window (`Minstrelsy::Service`); a line with no action is a song's.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Service {
    pub action: u8,
    pub arg: u32,
    pub cost: u32,
    pub enabled: bool,
    pub hue: u8,
    pub label: String,
    pub note: String,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct WindowMsg {
    pub open: bool,
    pub trainer: bool,
    pub tier: u8,
    pub skill: u16,
    pub cap: u16,
    pub tone: u8,
    pub message: String,
    pub services: Vec<Service>,
}

pub fn decode_window(b: &[u8]) -> Option<WindowMsg> {
    (b.first() == Some(&OP_WINDOW)).then_some(())?;
    let mut w = WindowMsg {
        open: *b.get(1)? != 0,
        trainer: *b.get(2)? != 0,
        tier: *b.get(3)?,
        skill: u16_at(b, 4)?,
        cap: u16_at(b, 6)?,
        tone: *b.get(8)?,
        ..Default::default()
    };
    let (message, mut at) = str_at(b, 9)?;
    w.message = message;
    let n = *b.get(at)?;
    at += 1;
    for _ in 0..n {
        let action = *b.get(at)?;
        let arg = u32_at(b, at + 1)?;
        let cost = u32_at(b, at + 5)?;
        let enabled = *b.get(at + 9)? != 0;
        let hue = *b.get(at + 10)?;
        let (label, next) = str_at(b, at + 11)?;
        let (note, next) = str_at(b, next)?;
        at = next;
        w.services.push(Service { action, arg, cost, enabled, hue, label, note });
    }
    (at == b.len()).then_some(w)
}

pub fn encode_act(action: u8, arg: u32) -> Vec<u8> {
    let mut b = vec![OP_ACT, action];
    b.extend_from_slice(&arg.to_le_bytes());
    b
}

/// The tier's name (Construction's tiers).
pub fn tier_name(tier: u8) -> &'static str {
    match tier {
        1 => "Apprentice",
        2 => "Journeyman",
        3 => "Expert",
        4 => "Artisan",
        _ => "Not learned",
    }
}

pub fn colour_of(c: Colour) -> Color {
    match c {
        Colour::Red => RED,
        Colour::Orange => Color::srgb(1.0, 0.5, 0.25),
        Colour::Yellow => Color::srgb(1.0, 1.0, 0.0),
        Colour::Green => Color::srgb(0.25, 0.75, 0.25),
        Colour::Gray => GREY,
    }
}

/// A song's line in the window: its name in its colour, its instrument and what it does.
pub fn song_line(s: &rules::Song, tier: u8, skill: u16) -> (String, Color) {
    let c = if tier == 0 { Colour::Red } else { rules::colour(s, skill) };
    let need = s.required.max(s.instrument.def().skill);
    (
        format!("{} ({}, {})  -  {}", s.name, s.instrument.def().name, need, s.effect.text()),
        colour_of(c),
    )
}

#[derive(Resource, Default)]
pub struct MinstrelWindow {
    pub open: bool,
    pub msg: WindowMsg,
    pub version: u32,
    drawn: Option<u32>,
    held_cursor: bool,
}

#[derive(Component)]
struct WinRoot;
#[derive(Component)]
struct WinClose;
#[derive(Component)]
struct WinBody;
#[derive(Component, Clone, Copy)]
struct ActButton(u8, u32);
#[derive(Resource)]
struct WinFont(Handle<Font>);

pub struct MinstrelWindowPlugin;

impl Plugin for MinstrelWindowPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<MinstrelWindow>()
            .add_systems(Update, (spawn, receive, open_close, click, draw).chain().in_set(Mw2Set::Hud))
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

const WIDTH: f32 = 520.0;

fn spawn(mut commands: Commands, assets: Res<AssetServer>, state: Res<FpsState>, mut built: Local<bool>) {
    if *built || !crate::capture_keep::in_world(state.in_world, "minstrelsy") {
        return;
    }
    *built = true;
    let font: Handle<Font> = assets.load(FONT);
    let spec = WindowSpec {
        key: "minstrelsy",
        title: "Minstrelsy".into(),
        frame: Frame::Dialog,
        width: Some(WIDTH),
        movable: true,
        anchor: Anchor { at: Vec2::ZERO, pivot: Vec2::ZERO, offset: Vec2::new(16.0, 104.0) },
        z: i32::MAX - 3,
    };
    wow_ui::spawn_window(
        &mut commands,
        &font,
        spec,
        (WinRoot, Visibility::Hidden, wow_ui::WindowSounds::MENU),
        Some(WinClose),
        |root| {
            root.spawn((WinBody, Node { flex_direction: FlexDirection::Column, row_gap: u(2.0), ..default() }));
        },
        |_| {},
    );
    commands.insert_resource(WinFont(font));
}

fn receive(mut msgs: MessageReader<FusionChannelReceived>, mut w: ResMut<MinstrelWindow>) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL || m.body.first() != Some(&OP_WINDOW) {
            continue;
        }
        let Some(msg) = decode_window(&m.body) else {
            warn!("benilla-mw2: a Minstrelsy window it can't read");
            continue;
        };
        if msg.open {
            w.open = true;
        }
        w.msg = msg;
        w.version = w.version.wrapping_add(1);
    }
}

fn open_close(
    input: Res<Mw2Input>,
    state: Res<FpsState>,
    mut w: ResMut<MinstrelWindow>,
    close: Query<&Interaction, (With<WinClose>, Changed<Interaction>)>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    // The Minstrelsy key opens the trade window (crate::professions); asking the server for
    // this one keeps the book and skill fresh for it.
    if input.minstrelsy && state.in_world {
        send.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: vec![OP_ASK_WINDOW] }));
        return;
    }
    let pressed = close.iter().any(|i| *i == Interaction::Pressed);
    if w.open && (input.escape || pressed || !crate::capture_keep::in_world(state.in_world, "minstrelsy")) {
        w.open = false;
        w.version = w.version.wrapping_add(1);
    }
    if !w.open && crate::capture_keep::keeps("minstrelsy") {
        w.open = true;
        w.version = w.version.wrapping_add(1);
    }
}

fn click(
    w: Res<MinstrelWindow>,
    buttons: Query<(&Interaction, &ActButton), Changed<Interaction>>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    if !w.open {
        return;
    }
    for (i, b) in &buttons {
        if *i == Interaction::Pressed {
            send.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: encode_act(b.0, b.1) }));
        }
    }
}

fn draw(
    mut commands: Commands,
    mut w: ResMut<MinstrelWindow>,
    font: Option<Res<WinFont>>,
    mut root: Query<&mut Visibility, With<WinRoot>>,
    body: Query<(Entity, Option<&Children>), With<WinBody>>,
) {
    let Some(font) = font else { return };
    let want = if w.open { Visibility::Inherited } else { Visibility::Hidden };
    for mut v in &mut root {
        if *v != want {
            *v = want;
        }
    }
    if !w.open || w.drawn == Some(w.version) {
        return;
    }
    w.drawn = Some(w.version);
    let f = &font.0;
    let msg = w.msg.clone();
    let Ok((e, children)) = body.single() else { return };
    for c in children.into_iter().flatten() {
        commands.entity(*c).despawn();
    }
    commands.entity(e).with_children(|b| {
        let head = if msg.tier == 0 {
            "Minstrelsy: not learned. A Minstrel Trainer by the inns of the capitals teaches it.".to_owned()
        } else {
            format!("{} Minstrelsy: {} / {}", tier_name(msg.tier), msg.skill, msg.cap)
        };
        b.spawn(text(f, &head, 14.0, GOLD));
        b.spawn((
            Node { max_width: u(WIDTH - 40.0), margin: UiRect::bottom(u(4.0)), ..default() },
            children![text(
                f,
                "A secondary profession: ready an instrument in Utility (Z), fire to play the song \
                 picked, its key again for the next. Songs teach while they are orange, yellow or \
                 green, faster the better they are played.",
                10.0,
                DIM,
            )],
        ));
        for s in msg.services.iter().filter(|s| shown(s)) {
            b.spawn(Node { flex_direction: FlexDirection::Row, align_items: AlignItems::Center, column_gap: u(6.0), ..default() })
                .with_children(|r| {
                    r.spawn(wow_ui::button_fit(
                        f,
                        &s.label,
                        WowButton { disabled: !pressable(s, msg.trainer), ..WowButton::new(Kind::Panel) },
                        (ActButton(s.action, s.arg), WowTip::new(&s.label, &s.note)),
                    ));
                    let price = if s.cost > 0 { format!("{}  ", money_text(s.cost)) } else { String::new() };
                    r.spawn(text(f, &format!("{price}{}", s.note), 11.0, if s.enabled { WHITE } else { GREY }));
                });
        }
        for i in Instrument::ALL {
            let d = i.def();
            b.spawn((
                Node { margin: UiRect::top(u(4.0)), ..default() },
                children![text(f, &format!("{} (Minstrelsy {})", d.name, d.skill), 12.0, GOLD)],
            ));
            for s in rules::songs_of(i) {
                let (line, c) = song_line(s, msg.tier, msg.skill);
                b.spawn((Node { max_width: u(WIDTH - 40.0), ..default() }, children![text(f, &line, 10.0, c)]));
            }
        }
        if !msg.message.is_empty() {
            let c = match msg.tone {
                2 => GREEN,
                3 => RED,
                _ => WHITE,
            };
            b.spawn((Node { margin: UiRect::top(u(6.0)), ..default() }, children![text(f, &msg.message, 11.0, c)]));
        }
    });
}

fn hold_cursor(mut w: ResMut<MinstrelWindow>, mut state: ResMut<FpsState>) {
    if w.open {
        if !state.panel {
            state.panel = true;
        }
        w.held_cursor = true;
    } else if w.held_cursor {
        w.held_cursor = false;
        state.panel = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn str8(b: &mut Vec<u8>, s: &str) {
        b.push(s.len() as u8);
        b.extend_from_slice(s.as_bytes());
    }

    #[test]
    fn the_window_decodes_as_the_server_writes_it() {
        // Minstrelsy.cpp EncodeWindow.
        let mut b = vec![OP_WINDOW, 1, 1, 2];
        b.extend_from_slice(&99u16.to_le_bytes());
        b.extend_from_slice(&150u16.to_le_bytes());
        b.push(2);
        str8(&mut b, "Hello");
        b.push(1);
        b.push(ACT_BUY);
        b.extend_from_slice(&2u32.to_le_bytes());
        b.extend_from_slice(&1500u32.to_le_bytes());
        b.push(1);
        b.push(0);
        str8(&mut b, "Buy a Lute");
        str8(&mut b, "");
        let w = decode_window(&b).expect("window");
        assert!(w.open && w.trainer);
        assert_eq!((w.tier, w.skill, w.cap, w.tone), (2, 99, 150, 2));
        assert_eq!(w.message, "Hello");
        assert_eq!(w.services[0], Service { action: ACT_BUY, arg: 2, cost: 1500, enabled: true, hue: 0, label: "Buy a Lute".into(), note: String::new() });
        b.pop();
        assert_eq!(decode_window(&b), None);
        assert_eq!(encode_act(ACT_TRAIN, 0), vec![61, 1, 0, 0, 0, 0]);
        let make = Service { action: ACT_MAKE, enabled: true, ..Default::default() };
        let learn = Service { action: ACT_LEARN, enabled: true, ..Default::default() };
        assert!(pressable(&make, false) && pressable(&make, true));
        assert!(!pressable(&learn, false) && pressable(&learn, true));
        assert!(!pressable(&Service { enabled: false, ..make.clone() }, true));
        assert!(!shown(&make) && shown(&learn), "making is the trade window's");
    }

    #[test]
    fn the_songs_read_in_their_colours() {
        let road = rules::song_of(3).unwrap();
        let (line, c) = song_line(road, 2, 60);
        assert!(line.starts_with("Ballad of the Road (Lute, 50)") && line.contains("Travel song"), "{line}");
        assert_eq!(c, colour_of(Colour::Orange));
        assert_eq!(song_line(road, 0, 60).1, RED, "not learned: red");
        assert_eq!(song_line(road, 3, 200).1, GREY);
        assert_eq!(tier_name(4), "Artisan");
    }
}
