//! The Glyphs & Gems window (the server's `Crafts::Augments`, the professions channel 12 ops
//! 30-32): the kit carried's three Major and three Minor glyph slots and its guns' gem sockets
//! (the primary's two, the jeweler's third and the meta; the secondary's one), each with what is
//! in it and the glyphs or gems in the bags that may go there. Glyphs and gems go in where kits
//! change (a trainer or an innkeeper, the field editor); anywhere else the window only shows.
//! The server checks every rule (slot levels, classes, one of each kind, scopes, the meta).
//! Opened from the Professions hub (Inscription's and Jewelcrafting's rows).

use benilla_app::fusion::{ChannelMessage, SendFusionChannel};
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use super::book::Items;
use super::crafts::{
    self, AUG_PRIMARY, AUG_PRIMARY_META, AUG_PRIMARY_SCOPED, AUG_SECONDARY, AUG_SECONDARY_META, AugmentsMsg,
    JEWELER_SOCKET_SKILL, MAJOR_LEVELS, MINOR_LEVELS,
};
use super::{CHANNEL, Crafts};
use crate::Mw2Set;
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, Kind, RED, WHITE, WindowSpec, WowButton, WowTip, text, u,
};

/// Open (or close) the Glyphs & Gems window.
#[derive(Message, Clone, Copy, Debug, PartialEq, Eq)]
pub struct OpenAugments;

/// One slot of the window: a glyph slot (`what` 0, `kind` 0 major 1 minor) or a socket (`what`
/// 1, `kind` 0 primary 1 secondary), its slot index, and whether it is there to use.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SlotLine {
    pub what: u8,
    pub kind: u8,
    pub slot: u8,
    pub label: String,
    pub item: u32,
    /// Why it can't take anything now ("Opens at level 30"), or None.
    pub locked: Option<String>,
    /// Glyphs: major ones in a major slot; gems: meta ones in the meta socket.
    pub wants_special: bool,
}

/// The window's lines, in order: Major glyphs, Minor glyphs, the primary's sockets, the
/// secondary's.
pub fn slot_lines(m: &AugmentsMsg) -> Vec<SlotLine> {
    let mut out = Vec::new();
    for (i, item) in m.majors.iter().enumerate() {
        out.push(SlotLine {
            what: 0,
            kind: 0,
            slot: i as u8,
            label: format!("Major glyph {}", i + 1),
            item: *item,
            locked: (m.level < MAJOR_LEVELS[i]).then(|| format!("Opens at level {}", MAJOR_LEVELS[i])),
            wants_special: true,
        });
    }
    for (i, item) in m.minors.iter().enumerate() {
        out.push(SlotLine {
            what: 0,
            kind: 1,
            slot: i as u8,
            label: format!("Minor glyph {}", i + 1),
            item: *item,
            locked: (m.level < MINOR_LEVELS[i]).then(|| format!("Opens at level {}", MINOR_LEVELS[i])),
            wants_special: false,
        });
    }
    for (gun, present, sockets) in [(0u8, m.flags & AUG_PRIMARY != 0, m.primary), (1, m.flags & AUG_SECONDARY != 0, m.secondary)] {
        let name = if gun == 0 { "Primary" } else { "Secondary" };
        let slots: &[u8] = if gun == 0 { &[0, 1, 2, 3] } else { &[0] };
        for &s in slots {
            let label = match s {
                3 => format!("{name}: meta socket"),
                2 => format!("{name}: jeweler's socket"),
                _ => format!("{name}: socket {}", s + 1),
            };
            let locked = if !present {
                Some("No gun in the kit there".to_string())
            } else if s == 2 && m.jewelcrafting < JEWELER_SOCKET_SKILL {
                Some(format!("Your own Jewelcrafting {JEWELER_SOCKET_SKILL}"))
            } else {
                None
            };
            out.push(SlotLine {
                what: 1,
                kind: gun,
                slot: s,
                label,
                item: sockets[usize::from(s)],
                locked,
                wants_special: s == 3,
            });
        }
    }
    out
}

/// What may go in a line from the bags: glyphs of its kind, gems (meta ones in the meta socket).
pub fn candidates(line: &SlotLine, items: &Items) -> Vec<(u32, &'static str)> {
    if line.what == 0 {
        crafts::GLYPHS
            .iter()
            .filter(|g| g.1 == line.wants_special)
            .filter(|g| items.get(&g.0).is_some_and(|l| l.have > 0))
            .map(|g| (g.0, g.2))
            .collect()
    } else {
        crafts::GEMS
            .iter()
            .filter(|g| g.1 == line.wants_special)
            .filter(|g| items.get(&g.0).is_some_and(|l| l.have > 0))
            .map(|g| (g.0, g.2))
            .collect()
    }
}

fn item_name(what: u8, item: u32) -> String {
    let n = if what == 0 { crafts::glyph_name(item) } else { crafts::gem_name(item) };
    n.map_or_else(|| format!("item {item}"), str::to_string)
}

#[derive(Resource, Default)]
struct AugWindow {
    open: bool,
    items: Items,
    scan_at: f32,
    drawn: Option<(u32, usize)>,
    held_cursor: bool,
}

#[derive(Component)]
struct WinRoot;
#[derive(Component)]
struct WinClose;
#[derive(Component)]
struct WinBody;
/// Put `item` (0: clear) in a slot.
#[derive(Component, Clone, Copy)]
struct SetButton(u8, u8, u8, u32);
#[derive(Resource)]
struct WinFont(Handle<Font>);

pub struct AugmentsPlugin;

impl Plugin for AugmentsPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<OpenAugments>()
            .init_resource::<AugWindow>()
            .add_systems(Update, (spawn, open_close, scan, click, draw).chain().in_set(Mw2Set::Hud))
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

const WIDTH: f32 = 560.0;

fn spawn(mut commands: Commands, assets: Res<AssetServer>, state: Res<FpsState>, mut built: Local<bool>) {
    if *built || !crate::capture_keep::in_world(state.in_world, "augments") {
        return;
    }
    *built = true;
    let font: Handle<Font> = assets.load(FONT);
    let spec = WindowSpec {
        key: "glyphs_gems",
        title: "Glyphs & Gems".into(),
        frame: Frame::Dialog,
        width: Some(WIDTH),
        movable: true,
        anchor: Anchor::CENTER,
        z: i32::MAX - 3,
    };
    wow_ui::spawn_window(
        &mut commands,
        &font,
        spec,
        (WinRoot, Visibility::Hidden, wow_ui::WindowSounds::MENU),
        Some(WinClose),
        |root| {
            wow_ui::scroll_frame(root, 440.0, None, WinBody, |_| {});
        },
        |_| {},
    );
    commands.insert_resource(WinFont(font));
}

fn open_close(
    input: Res<Mw2Input>,
    state: Res<FpsState>,
    mut w: ResMut<AugWindow>,
    mut opens: MessageReader<OpenAugments>,
    close: Query<&Interaction, (With<WinClose>, Changed<Interaction>)>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    for _ in opens.read() {
        w.open = !w.open;
        w.scan_at = 0.0;
        w.drawn = None;
        if w.open {
            send.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: vec![crafts::OP_ASK_AUGMENTS] }));
        }
    }
    let pressed = close.iter().any(|i| *i == Interaction::Pressed);
    if w.open && (input.escape || pressed || !crate::capture_keep::in_world(state.in_world, "augments")) {
        w.open = false;
    }
    if !w.open && crate::capture_keep::keeps("augments") {
        w.open = true;
    }
}

fn scan(time: Res<Time>, script: Option<NonSend<UiScript>>, mut w: ResMut<AugWindow>) {
    if !w.open {
        return;
    }
    let t = time.elapsed_secs();
    if t >= w.scan_at
        && let Some(s) = script.as_deref()
    {
        w.scan_at = t + 0.5;
        if let Some(out) = crate::lua::eval::<String>(s, &super::scan_lua(&crafts::augment_items())) {
            let items = super::parse_scan(&out);
            if items != w.items {
                w.items = items;
                w.drawn = None;
            }
        }
    }
}

fn click(
    w: Res<AugWindow>,
    buttons: Query<(&Interaction, &SetButton), Changed<Interaction>>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    if !w.open {
        return;
    }
    for (i, b) in &buttons {
        if *i == Interaction::Pressed {
            send.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: crafts::encode_augment(b.0, b.1, b.2, b.3) }));
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn draw(
    mut commands: Commands,
    mut w: ResMut<AugWindow>,
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
    let key = (crafts.augments_version, w.items.len());
    if !w.open || w.drawn == Some(key) {
        return;
    }
    w.drawn = Some(key);
    let f = &font.0;
    let m = crafts.augments.clone();
    let items = w.items.clone();
    let Ok((e, children)) = body.single() else { return };
    for c in children.into_iter().flatten() {
        commands.entity(*c).despawn();
    }
    commands.entity(e).with_children(|b| {
        let Some(kit) = m.kit else {
            b.spawn(text(f, "Carry a kit to see its glyphs and gems.", 12.0, GREY));
            return;
        };
        b.spawn(text(f, &format!("Kit {}", kit + 1), 14.0, GOLD));
        b.spawn((
            Node { max_width: u(WIDTH - 40.0), margin: UiRect::bottom(u(4.0)), ..default() },
            children![text(
                f,
                if m.can_edit {
                    "Inscribing a glyph or setting a gem uses it up; one put over another destroys the old. \
                     A kit holds a glyph once; a gun takes one gem of each kind; a sway gem asks a scope; \
                     the meta gem wakes with gems of two colours in the gun."
                } else {
                    "Glyphs and gems go in where kits change: at a trainer or an innkeeper, or with the field editor."
                },
                10.0,
                DIM,
            )],
        ));
        let mut section = 255u16;
        for line in slot_lines(&m) {
            let head = u16::from(line.what) << 8 | u16::from(line.kind);
            if head != section {
                section = head;
                let title = match (line.what, line.kind) {
                    (0, 0) => "Major Glyphs".to_string(),
                    (0, _) => "Minor Glyphs".to_string(),
                    (_, 0) => format!(
                        "Primary gems{}{}",
                        if m.flags & AUG_PRIMARY_SCOPED != 0 { " (scoped)" } else { "" },
                        if m.flags & AUG_PRIMARY_META != 0 { ", meta awake" } else { "" }
                    ),
                    _ => format!("Secondary gems{}", if m.flags & AUG_SECONDARY_META != 0 { ", meta awake" } else { "" }),
                };
                wow_ui::section_header(b, f, &title);
            }
            b.spawn(Node {
                flex_direction: FlexDirection::Row,
                flex_wrap: FlexWrap::Wrap,
                align_items: AlignItems::Center,
                column_gap: u(6.0),
                row_gap: u(2.0),
                max_width: u(WIDTH - 40.0),
                ..default()
            })
            .with_children(|r| {
                r.spawn(text(f, &format!("{}:", line.label), 11.0, WHITE));
                if let Some(why) = &line.locked {
                    r.spawn(text(f, why, 11.0, GREY));
                    return;
                }
                if line.item != 0 {
                    r.spawn(text(f, &item_name(line.what, line.item), 11.0, GREEN));
                    r.spawn(wow_ui::button_fit(
                        f,
                        "Clear",
                        WowButton { disabled: !m.can_edit, ..WowButton::new(Kind::Panel) },
                        (
                            SetButton(line.what, line.kind, line.slot, 0),
                            WowTip::new("Clear", "Destroys what is in it."),
                        ),
                    ));
                } else {
                    r.spawn(text(f, "empty", 11.0, GREY));
                }
                for (item, name) in candidates(&line, &items) {
                    if item == line.item {
                        continue;
                    }
                    r.spawn(wow_ui::button_fit(
                        f,
                        name,
                        WowButton { disabled: !m.can_edit, ..WowButton::new(Kind::Panel) },
                        (
                            SetButton(line.what, line.kind, line.slot, item),
                            WowTip::new(name, if line.what == 0 { "Inscribe it here." } else { "Set it here." }),
                        ),
                    ));
                }
            });
        }
        if !m.message.is_empty() {
            let c = match m.tone {
                2 => GREEN,
                3 => RED,
                _ => WHITE,
            };
            b.spawn((Node { margin: UiRect::top(u(6.0)), ..default() }, children![text(f, &m.message, 11.0, c)]));
        }
    });
}

fn hold_cursor(mut w: ResMut<AugWindow>, mut state: ResMut<FpsState>) {
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
    use crate::professions::book::ItemLook;

    fn msg() -> AugmentsMsg {
        AugmentsMsg {
            open: true,
            kit: Some(0),
            can_edit: true,
            level: 35,
            jewelcrafting: 100,
            majors: [94330, 0, 0],
            minors: [0, 0, 0],
            primary: [94450, 0, 0, 0],
            secondary: [0; 4],
            flags: AUG_PRIMARY,
            ..Default::default()
        }
    }

    #[test]
    fn the_slots_open_with_level_skill_and_guns() {
        let lines = slot_lines(&msg());
        assert_eq!(lines.len(), 3 + 3 + 4 + 1);
        assert_eq!(lines[0].item, 94330);
        assert!(lines[1].locked.is_none() && lines[2].locked.as_deref() == Some("Opens at level 50"));
        assert!(lines[3].locked.is_none() && lines[4].locked.is_some(), "minors open at 20 and 40");
        assert_eq!(lines[6].label, "Primary: socket 1");
        assert!(lines[8].locked.is_some(), "the jeweler's socket asks Jewelcrafting 275");
        assert!(lines[9].wants_special && lines[9].locked.is_none(), "the meta socket");
        assert_eq!(lines[10].locked.as_deref(), Some("No gun in the kit there"));
        let mut jeweler = msg();
        jeweler.jewelcrafting = 280;
        assert!(slot_lines(&jeweler)[8].locked.is_none());
    }

    #[test]
    fn only_what_fits_is_offered_from_the_bags() {
        let mut items = Items::new();
        for id in [94331u32, 94350, 94451, 94465] {
            items.insert(id, ItemLook { have: 1, ..Default::default() });
        }
        let lines = slot_lines(&msg());
        assert_eq!(candidates(&lines[0], &items), vec![(94331, "Glyph of the Steady Hand")]);
        assert_eq!(candidates(&lines[3], &items), vec![(94350, "Glyph of the Long Road")]);
        assert_eq!(candidates(&lines[6], &items), vec![(94451, "Solid Malachite")]);
        assert_eq!(candidates(&lines[9], &items), vec![(94465, "Relentless Azerothian Diamond")]);
        assert_eq!(item_name(1, 94450), "Bold Tigerseye");
    }
}
