//! An instrument as a form (as a druid's form takes the bar over): readying an instrument in
//! Utility turns the action bar into that instrument's notes. Its lanes' buttons, right over the
//! bar's own (the slots the rhythm game plays on, [`super::lane_slots`]), read the note or chord
//! each plays; the bar's other slots go quiet. Outside a song a note's key (or a click on it)
//! plays it for fun with WoW's own sounds, judged by nobody; in a song the keys are the lanes the
//! server judges, as before. Beside the bar: **Song** (the next song the skill can play) and
//! **Leave** (bare hands again, the bar back). Fire still plays the song picked.

use bevy::picking::Pickable;
use bevy::prelude::*;

use super::rules::{self, Instrument};
use super::{Minstrel, lane_slots, next_song, picked_song};
use crate::Mw2Set;
use crate::action_systems::{ActionSystems, ActionSystemsSet};
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::wow_ui::{FONT, GOLD, WHITE};

/// The note (or drum) each lane of an instrument plays, left to right.
pub fn note_names(i: Instrument) -> &'static [&'static str] {
    match i {
        Instrument::HandDrum => &["Low", "High"],
        Instrument::Lute => &["G", "C", "D"],
        Instrument::Flute => &["C", "E", "G", "A"],
        Instrument::Harp => &["C", "D", "E", "G", "A"],
        Instrument::Bagpipes => &["Drone", "G", "A", "B", "D", "E"],
        Instrument::ThunderDrums => &["Boom", "Low", "Mid", "High", "Rim", "Crash"],
    }
}

/// A lane's sound in free play (SoundEntries kits by name): each instrument's own strike, the
/// higher lanes brighter.
pub fn note_sound(i: Instrument, lane: usize) -> &'static str {
    let palette: &[&'static str] = match i {
        Instrument::HandDrum => &["Unarmed_ShieldWood", "Mace1H_ArmorWood"],
        Instrument::ThunderDrums => &["Mace2H_ArmorWoodShield", "Unarmed_ShieldWood", "Mace1H_ArmorWood"],
        Instrument::Bagpipes => &["Mace1H_ArmorWood", "PickUpRing", "BowRelease"],
        Instrument::Lute => &["BowRelease", "PickUpRing"],
        Instrument::Flute | Instrument::Harp => &["PickUpRing", "BowRelease"],
    };
    palette[lane % palette.len()]
}

/// Which lane each of the bar's six slots plays in the form (None: a quiet slot).
pub fn form_slots(i: Instrument, keys: Option<&str>) -> [Option<usize>; 6] {
    let mut out = [None; 6];
    for (lane, slot) in lane_slots(i.def().lanes, keys).into_iter().enumerate() {
        out[slot] = Some(lane);
    }
    out
}

/// The instrument whose form the bar is in: one readied in Utility, the layer on, not building.
pub fn form_of(systems: &ActionSystems, state: &FpsState) -> Option<Instrument> {
    systems
        .tool_out()
        .filter(|a| a.is_instrument() && state.active && !state.building)
        .and_then(|a| rules::def_of_tool(a.id()))
        .map(|d| d.id)
}

#[derive(Component)]
struct FormRoot;
#[derive(Component, Clone, Copy)]
struct FormSlot(usize);
#[derive(Component, Clone, Copy)]
struct FormSlotText(usize);
#[derive(Component)]
struct LeaveButton;
#[derive(Component)]
struct SongButton;
#[derive(Component)]
struct SongText;

pub struct FormPlugin;

impl Plugin for FormPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Startup, spawn)
            .add_systems(
                Update,
                (free_play, clicks)
                    .chain()
                    .in_set(Mw2Set::Mode)
                    .after(super::play_lanes)
                    .before(ActionSystemsSet),
            )
            .add_systems(Update, draw.in_set(Mw2Set::Hud));
    }
}

/// In the form: the slot keys are the notes. Outside a song each plays its note's sound; none
/// reaches the bar (a song's own keys are taken by the lanes first).
fn free_play(
    state: Res<FpsState>,
    systems: Res<ActionSystems>,
    mut input: ResMut<Mw2Input>,
    mut m: ResMut<Minstrel>,
) {
    let form = form_of(&systems, &state);
    if m.form != form {
        m.form = form;
    }
    let Some(i) = form else { return };
    if state.chat_focus || state.typing {
        return;
    }
    let keys = std::env::var("WOMW_MINSTREL_KEYS").ok();
    let slots = form_slots(i, keys.as_deref());
    let performing = m.performing();
    for (slot, c) in input.slots.iter_mut().enumerate() {
        if c.pressed && !performing
            && let Some(lane) = slots[slot]
        {
            m.sounds.push((true, note_sound(i, lane)));
        }
        *c = Default::default();
    }
    // The page keys turn no page in the form.
    input.page = 0;
}

fn clicks(
    state: Res<FpsState>,
    mut systems: ResMut<ActionSystems>,
    mut m: ResMut<Minstrel>,
    slots: Query<(&Interaction, &FormSlot), Changed<Interaction>>,
    leave: Query<&Interaction, (With<LeaveButton>, Changed<Interaction>)>,
    song: Query<&Interaction, (With<SongButton>, Changed<Interaction>)>,
) {
    let Some(i) = form_of(&systems, &state) else { return };
    let pressed = |x: &Interaction| *x == Interaction::Pressed;
    let keys = std::env::var("WOMW_MINSTREL_KEYS").ok();
    let lanes = form_slots(i, keys.as_deref());
    for (x, s) in &slots {
        if pressed(x) && !m.performing()
            && let Some(lane) = lanes[s.0]
        {
            m.sounds.push((true, note_sound(i, lane)));
        }
    }
    if song.iter().any(pressed)
        && let Some(next) = next_song(i, m.skill.1, m.picks.get(&(i as u8)).copied())
    {
        m.picks.insert(i as u8, next.id);
    }
    if leave.iter().any(pressed) {
        systems.tool = None;
    }
}

const SLOT_FILL: Color = Color::srgba(0.04, 0.03, 0.02, 0.96);
const SLOT_EDGE: Color = Color::srgba(0.75, 0.6, 0.2, 0.95);

fn spawn(mut commands: Commands, assets: Res<AssetServer>) {
    let font: Handle<Font> = assets.load(FONT);
    let label = |s: &str, size: f32, c: Color| {
        (
            Text::new(s),
            TextFont::from_font_size(size).with_font(font.clone()),
            TextColor(c),
            TextShadow::default(),
            Pickable::IGNORE,
        )
    };
    commands
        .spawn((
            Name::new("instrument form bar"),
            FormRoot,
            Node { position_type: PositionType::Absolute, width: percent(100), height: percent(100), display: Display::None, ..default() },
            Pickable::IGNORE,
            GlobalZIndex(i32::MAX - 20),
        ))
        .with_children(|r| {
            for i in 0..6 {
                r.spawn((
                    Button,
                    FormSlot(i),
                    Node {
                        position_type: PositionType::Absolute,
                        border: UiRect::all(px(1)),
                        justify_content: JustifyContent::Center,
                        align_items: AlignItems::Center,
                        ..default()
                    },
                    BackgroundColor(SLOT_FILL),
                    BorderColor::all(SLOT_EDGE),
                ))
                .with_children(|b| {
                    b.spawn((FormSlotText(i), label("", 13.0, WHITE)));
                });
            }
            r.spawn((
                Button,
                SongButton,
                Node {
                    position_type: PositionType::Absolute,
                    border: UiRect::all(px(1)),
                    padding: UiRect::axes(px(6), px(2)),
                    ..default()
                },
                BackgroundColor(SLOT_FILL),
                BorderColor::all(SLOT_EDGE),
            ))
            .with_children(|b| {
                b.spawn((SongText, label("Song", 11.0, GOLD)));
            });
            r.spawn((
                Button,
                LeaveButton,
                Node {
                    position_type: PositionType::Absolute,
                    border: UiRect::all(px(1)),
                    padding: UiRect::axes(px(6), px(2)),
                    ..default()
                },
                BackgroundColor(SLOT_FILL),
                BorderColor::all(SLOT_EDGE),
            ))
            .with_children(|b| {
                b.spawn(label("Leave", 11.0, GOLD));
            });
        });
}

/// Where the form's slots go: over the bar's own buttons when measured, else a row centred
/// near the bottom of the screen.
pub fn slot_rects(buttons: Option<&[Rect; 6]>, viewport: Vec2) -> [Rect; 6] {
    if let Some(b) = buttons {
        return *b;
    }
    let (w, step) = (36.0, 42.0);
    let left = (viewport.x - (step * 5.0 + w)) / 2.0;
    let top = viewport.y - 60.0;
    std::array::from_fn(|i| Rect::new(left + step * i as f32, top, left + step * i as f32 + w, top + w))
}

#[allow(clippy::type_complexity)]
fn draw(
    m: Res<Minstrel>,
    window: Query<&Window, With<bevy::window::PrimaryWindow>>,
    mut root: Query<&mut Node, (With<FormRoot>, Without<FormSlot>, Without<SongButton>, Without<LeaveButton>)>,
    mut slots: Query<(&FormSlot, &mut Node, &mut BorderColor), (Without<FormRoot>, Without<SongButton>, Without<LeaveButton>)>,
    mut texts: Query<(&FormSlotText, &mut Text), Without<SongText>>,
    mut song: Query<&mut Node, (With<SongButton>, Without<FormRoot>, Without<FormSlot>, Without<LeaveButton>)>,
    mut song_text: Query<&mut Text, (With<SongText>, Without<FormSlotText>)>,
    mut leave: Query<&mut Node, (With<LeaveButton>, Without<FormRoot>, Without<FormSlot>, Without<SongButton>)>,
) {
    let Ok(mut r) = root.single_mut() else { return };
    let Some(i) = m.form else {
        if r.display != Display::None {
            r.display = Display::None;
        }
        return;
    };
    if r.display != Display::Flex {
        r.display = Display::Flex;
    }
    let viewport = window.single().map_or(Vec2::new(1280.0, 720.0), |w| Vec2::new(w.width(), w.height()));
    let rects = slot_rects(m.buttons.as_ref(), viewport);
    let keys = std::env::var("WOMW_MINSTREL_KEYS").ok();
    let lanes = form_slots(i, keys.as_deref());
    let names = note_names(i);
    for (s, mut n, mut edge) in &mut slots {
        let rc = rects[s.0];
        n.left = px(rc.min.x);
        n.top = px(rc.min.y);
        n.width = px(rc.width());
        n.height = px(rc.height());
        let lit = lanes[s.0].is_some();
        let want = if lit { SLOT_EDGE } else { SLOT_EDGE.with_alpha(0.25) };
        if edge.top != want {
            *edge = BorderColor::all(want);
        }
    }
    for (s, mut t) in &mut texts {
        let want = lanes[s.0].and_then(|l| names.get(l)).map_or(String::new(), |n| n.to_string());
        if t.0 != want {
            t.0 = want;
        }
    }
    let left = rects[0].min.x;
    let top = rects[0].min.y;
    let h = rects[0].height();
    if let Ok(mut n) = leave.single_mut() {
        n.left = px(left - 64.0);
        n.top = px(top + h / 2.0 - 2.0);
    }
    if let Ok(mut n) = song.single_mut() {
        n.left = px(left);
        n.top = px(top - 22.0);
    }
    let name = picked_song(i, m.skill.1, m.picks.get(&(i as u8)).copied())
        .map_or_else(|| format!("{}: no song yet", i.def().name), |s| format!("Song: {} (next)", s.name));
    for mut t in &mut song_text {
        if t.0 != name {
            t.0 = name.clone();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::action_systems::{Ability, System};

    #[test]
    fn every_lane_has_a_note_and_a_sound() {
        for i in Instrument::ALL {
            assert_eq!(note_names(i).len(), usize::from(i.def().lanes), "{i:?}");
            for lane in 0..usize::from(i.def().lanes) {
                assert!(!note_sound(i, lane).is_empty());
            }
        }
    }

    #[test]
    fn the_form_puts_the_lanes_on_their_slots() {
        // The Hand Drum's two lanes on Q and E, the rest quiet.
        assert_eq!(form_slots(Instrument::HandDrum, None), [None, None, None, None, Some(0), Some(1)]);
        assert!(form_slots(Instrument::ThunderDrums, None).iter().all(|s| s.is_some()));
        assert_eq!(form_slots(Instrument::HandDrum, Some("12")), [Some(0), Some(1), None, None, None, None]);
    }

    #[test]
    fn readying_an_instrument_switches_the_bar_and_leaving_switches_it_back() {
        let state = FpsState { in_world: true, active: true, ..Default::default() };
        let mut systems = ActionSystems { active: System::Utility, ..Default::default() };
        assert_eq!(form_of(&systems, &state), None, "bare hands: the bar");
        systems.tool = Some(Ability::Lute);
        assert_eq!(form_of(&systems, &state), Some(Instrument::Lute));
        systems.tool = Some(Ability::MiningPick);
        assert_eq!(form_of(&systems, &state), None, "a tool is no form");
        systems.tool = Some(Ability::Harp);
        systems.active = System::Modern;
        assert_eq!(form_of(&systems, &state), None, "the weapons out: no form");
        systems.active = System::Utility;
        let building = FpsState { in_world: true, active: true, building: true, ..Default::default() };
        assert_eq!(form_of(&systems, &building), None);
        systems.tool = None;
        assert_eq!(form_of(&systems, &state), None, "Leave: the bar again");
    }

    #[test]
    fn the_form_takes_the_slot_keys_from_the_bar() {
        let mut app = App::new();
        app.insert_resource(FpsState { in_world: true, active: true, ..Default::default() })
            .insert_resource(ActionSystems { active: System::Utility, tool: Some(Ability::HandDrum), ..Default::default() })
            .init_resource::<Mw2Input>()
            .init_resource::<Minstrel>()
            .add_systems(Update, free_play);
        app.world_mut().resource_mut::<Mw2Input>().slots[5].pressed = true;
        app.world_mut().resource_mut::<Mw2Input>().slots[0].pressed = true;
        app.update();
        let input = app.world().resource::<Mw2Input>();
        assert!(input.slots.iter().all(|c| !c.pressed), "no slot reaches the bar");
        let m = app.world().resource::<Minstrel>();
        assert_eq!(m.form, Some(Instrument::HandDrum));
        assert_eq!(m.sounds, vec![(true, note_sound(Instrument::HandDrum, 1))], "E plays the high drum; 1 is quiet");
        // Bare hands: the keys are the bar's again.
        app.world_mut().resource_mut::<ActionSystems>().tool = None;
        app.world_mut().resource_mut::<Mw2Input>().slots[5].pressed = true;
        app.update();
        assert!(app.world().resource::<Mw2Input>().slots[5].pressed);
        assert_eq!(app.world().resource::<Minstrel>().form, None);
    }
}
