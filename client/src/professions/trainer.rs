//! The Inscription and Jewelcrafting trainers' window (the server's `Crafts::Window`, the
//! professions channel 12 op 22): the trade's page at its trainer. Its buttons train the next
//! tier (the first takes a primary profession slot), buy the trade's goods (parchment, the
//! Jeweler's Kit), learn recipes in their skill colours, and unlearn; each answer shows in its
//! status line. Making is the trade window's (the Professions hub, or this window's button).

use benilla_app::fusion::{ChannelMessage, SendFusionChannel};
use bevy::prelude::*;

use super::crafts::{self, ACT_LEARN, Service, Trade};
use super::{Book, CHANNEL, Crafts, OpenBook};
use crate::Mw2Set;
use crate::input::Mw2Input;
use crate::minstrel::money_text;
use crate::minstrel::window::tier_name;
use crate::mode::FpsState;
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, Kind, RED, WHITE, WindowSpec, WowButton, WowTip, text, u,
};

/// A service's line colour: a recipe in its skill colour (the server's `Homestead::Hue`).
pub fn hue_colour(hue: u8) -> Color {
    match hue {
        1 => Color::srgb(1.0, 0.5, 0.25),
        2 => Color::srgb(1.0, 1.0, 0.0),
        3 => Color::srgb(0.25, 0.75, 0.25),
        4 => GREY,
        5 => RED,
        _ => WHITE,
    }
}

/// Whether a line's button may be pressed: every action asks the trainer (the server checks).
pub fn pressable(s: &Service, trainer: bool) -> bool {
    s.enabled && s.action != 0 && trainer
}

#[derive(Resource, Default)]
struct TrainerWindow {
    open: bool,
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
struct ActButton(Trade, u8, u32);
#[derive(Component, Clone, Copy)]
struct BookButton(Trade);
#[derive(Resource)]
struct WinFont(Handle<Font>);

pub struct TrainerPlugin;

impl Plugin for TrainerPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<TrainerWindow>()
            .add_systems(Update, (spawn, open_close, click, draw).chain().in_set(Mw2Set::Hud))
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

const WIDTH: f32 = 520.0;

fn spawn(mut commands: Commands, assets: Res<AssetServer>, state: Res<FpsState>, mut built: Local<bool>) {
    if *built || !crate::capture_keep::in_world(state.in_world, "trainer") {
        return;
    }
    *built = true;
    let font: Handle<Font> = assets.load(FONT);
    let spec = WindowSpec {
        key: "craft_trainer",
        title: "Trainer".into(),
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
            wow_ui::scroll_frame(root, 420.0, None, WinBody, |_| {});
        },
        |_| {},
    );
    commands.insert_resource(WinFont(font));
}

fn open_close(
    input: Res<Mw2Input>,
    state: Res<FpsState>,
    crafts: Res<Crafts>,
    mut w: ResMut<TrainerWindow>,
    mut seen: Local<u32>,
    close: Query<&Interaction, (With<WinClose>, Changed<Interaction>)>,
) {
    if crafts.window_version != *seen {
        *seen = crafts.window_version;
        if crafts.window.as_ref().is_some_and(|m| m.open) {
            w.open = true;
        }
        w.drawn = None;
    }
    let pressed = close.iter().any(|i| *i == Interaction::Pressed);
    if w.open && (input.escape || pressed || !crate::capture_keep::in_world(state.in_world, "trainer")) {
        w.open = false;
        w.drawn = None;
    }
    if !w.open && crate::capture_keep::keeps("trainer") {
        w.open = true;
    }
}

fn click(
    mut w: ResMut<TrainerWindow>,
    buttons: Query<(&Interaction, &ActButton), Changed<Interaction>>,
    books: Query<(&Interaction, &BookButton), Changed<Interaction>>,
    mut send: MessageWriter<SendFusionChannel>,
    mut open: MessageWriter<OpenBook>,
) {
    if !w.open {
        return;
    }
    for (i, b) in &buttons {
        if *i == Interaction::Pressed {
            send.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: crafts::encode_act(b.0, b.1, b.2) }));
        }
    }
    for (i, b) in &books {
        if *i == Interaction::Pressed {
            open.write(OpenBook(match b.0 {
                Trade::Inscription => Book::Inscription,
                Trade::Jewelcrafting => Book::Jewelcrafting,
            }));
            w.open = false;
        }
    }
}

fn draw(
    mut commands: Commands,
    mut w: ResMut<TrainerWindow>,
    crafts: Res<Crafts>,
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
    let version = crafts.window_version;
    if !w.open || w.drawn == Some(version) {
        return;
    }
    w.drawn = Some(version);
    let Some(msg) = crafts.window.clone() else { return };
    let f = &font.0;
    let Ok((e, children)) = body.single() else { return };
    for c in children.into_iter().flatten() {
        commands.entity(*c).despawn();
    }
    let name = msg.trade.name();
    commands.entity(e).with_children(|b| {
        let head = if msg.tier == 0 {
            format!("{name}: not learned.")
        } else {
            format!("{} {name}: {} / {}", tier_name(msg.tier), msg.skill, msg.cap)
        };
        b.spawn(text(f, &head, 14.0, GOLD));
        b.spawn((
            Node { max_width: u(WIDTH - 40.0), margin: UiRect::bottom(u(4.0)), ..default() },
            children![text(f, msg.trade.description(), 10.0, DIM)],
        ));
        if msg.tier > 0 {
            b.spawn(wow_ui::button_fit(
                f,
                "Open the trade window",
                WowButton::new(Kind::Panel),
                (BookButton(msg.trade), WowTip::new(name, "Its recipes, what they take, Create and Create All.")),
            ));
        }
        if !msg.trainer {
            b.spawn(text(f, "Speak with the trainer to train, learn or buy.", 11.0, GREY));
        }
        let mut last_learn = false;
        for s in msg.services.iter().filter(|s| s.action != 0) {
            let learn = s.action == ACT_LEARN;
            if learn && !last_learn {
                wow_ui::section_header(b, f, "Recipes");
            }
            last_learn = learn;
            b.spawn(Node { flex_direction: FlexDirection::Row, align_items: AlignItems::Center, column_gap: u(6.0), ..default() })
                .with_children(|r| {
                    r.spawn(wow_ui::button_fit(
                        f,
                        &s.label,
                        WowButton { disabled: !pressable(s, msg.trainer), ..WowButton::new(Kind::Panel) },
                        (ActButton(msg.trade, s.action, s.arg), WowTip::new(&s.label, &s.note)),
                    ));
                    let price = if s.cost > 0 { format!("{}  ", money_text(s.cost)) } else { String::new() };
                    let colour = if !s.enabled { GREY } else if learn { hue_colour(s.hue) } else { WHITE };
                    r.spawn(text(f, &format!("{price}{}", s.note), 11.0, colour));
                });
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

fn hold_cursor(mut w: ResMut<TrainerWindow>, mut state: ResMut<FpsState>) {
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

    #[test]
    fn lines_press_at_the_trainer_only_and_read_in_their_colours() {
        let s = Service { action: ACT_LEARN, enabled: true, ..Default::default() };
        assert!(pressable(&s, true) && !pressable(&s, false));
        assert!(!pressable(&Service { enabled: false, ..s.clone() }, true));
        assert!(!pressable(&Service { action: 0, ..s }, true));
        assert_eq!(hue_colour(5), RED);
        assert_eq!(hue_colour(4), GREY);
        assert_eq!(hue_colour(0), WHITE);
    }
}
