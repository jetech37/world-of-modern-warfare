//! Minstrelsy (WOMW design section 12, "Minstrelsy"; the server's `fusion/Minstrelsy.h`): music
//! as a secondary profession every class may learn from a Minstrel Trainer.
//!
//! The six instruments are Utility tools (crate::action_systems): on Utility's bar only while
//! carried and while the skill reaches the instrument's own. With one readied, the fire button
//! plays the song picked, and pressing the instrument's key again picks its next song (the line
//! under the crosshair names it). The strike goes to the server as a tool's (channel 10 op 3,
//! the song as its target); the server answers with the song's seed (op 51), the chart is made
//! from it here as the server makes it ([`rules`]), and a compact note highway rises over the
//! action bar, which in Utility is Utility's bar: one lane per key the instrument uses, right over
//! that key's button (by default the rightmost: Q and E for the Hand Drum, all six for the
//! Thunder Drums; `WOMW_MINSTREL_KEYS=12QE` picks others). The slot keys play the lanes for as
//! long as the song lasts; movement and the mouse stay the player's. The presses go to the
//! server a couple of times a second (op 50), which judges them itself; its verdict (op 53) shows
//! for a moment when the song ends. Switching the hands (X, Z), fire again or another tool stops
//! the song; a fight stops any song but the combat ones.
//!
//! Also here: the songs on the player (op 55, a list at the screen's right), the skill (op 56),
//! a song heard and its tip buttons (ops 57-59), and the Minstrelsy window ([`window`]).
//!
//! Sound: WoW's own from the install (a tick on every beat, the instrument's strike on every
//! note struck, a flourish and a last chime); nothing depends on them. `WOMW_MINSTREL_METRONOME=0`
//! keeps the ticks quiet.

pub mod form;
pub mod rules;
pub mod window;

use std::collections::HashMap;

use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use benilla_ui::script::UiScript;
use bevy::picking::Pickable;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::action_systems::{ActionSystems, ActionSystemsSet};
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::wow_ui::{FONT, GOLD, WHITE};
use rules::{Chart, Instrument, InstrumentDef, Judgement, NoteState, Press, Tally};

/// The action systems' channel (`Fusion::ActionSystems::kChannel`): the tools' and Minstrelsy's.
pub const CHANNEL: u8 = crate::action_systems::CHANNEL;
pub const OP_SONG_HITS: u8 = 50;
pub const OP_SONG_START: u8 = 51;
pub const OP_SONG_STOP: u8 = 52;
pub const OP_SONG_RESULT: u8 = 53;
pub const OP_BUFFS: u8 = 55;
pub const OP_SKILL: u8 = 56;
pub const OP_HEARD: u8 = 57;
pub const OP_TIP: u8 = 58;
pub const OP_TIPPED: u8 = 59;
const LANE_DOWN: u8 = 0x80;
/// How often the presses go to the server (it allows five costly asks a second, shared).
const SEND_SECS: f32 = 0.4;
const MAX_BATCH: usize = 64;

pub const RESULT_FLAT: u8 = 0x01;
pub const RESULT_REJECTED: u8 = 0x02;
pub const RESULT_ROBOTIC: u8 = 0x04;
pub const RESULT_CUT: u8 = 0x08;
pub const RESULT_SKILLUP: u8 = 0x10;

/// The tips the heard line offers, copper (the server allows up to 1 gold a tip).
pub const TIPS: [u32; 3] = [100, 1000, 10000];

// ── The wire ──

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Start {
    pub perf: u32,
    pub song: u16,
    pub instrument: u8,
    pub seed: u32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SongResult {
    pub perf: u32,
    pub song: u16,
    pub flags: u8,
    pub perfect: u16,
    pub good: u16,
    pub miss: u16,
    pub kept: u16,
    pub dropped: u16,
    pub strays: u16,
    pub max_combo: u16,
    pub potency_pm: u16,
    pub quality_pm: u16,
}

/// A song on the player (op 55): its value and the seconds left when told.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Buff {
    pub song: u16,
    pub value: f32,
    pub ms_left: u32,
}

/// A song heard (op 57): who played it, for a tip.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Heard {
    pub performer: u64,
    pub song: u16,
    pub potency_pm: u16,
    pub name: String,
}

pub(crate) fn u16_at(b: &[u8], at: usize) -> Option<u16> {
    Some(u16::from_le_bytes(b.get(at..at + 2)?.try_into().ok()?))
}
pub(crate) fn u32_at(b: &[u8], at: usize) -> Option<u32> {
    Some(u32::from_le_bytes(b.get(at..at + 4)?.try_into().ok()?))
}
fn u64_at(b: &[u8], at: usize) -> Option<u64> {
    Some(u64::from_le_bytes(b.get(at..at + 8)?.try_into().ok()?))
}
/// A length-prefixed string at `at`, and where it ends.
pub(crate) fn str_at(b: &[u8], at: usize) -> Option<(String, usize)> {
    let n = usize::from(*b.get(at)?);
    let s = b.get(at + 1..at + 1 + n)?;
    Some((String::from_utf8_lossy(s).into_owned(), at + 1 + n))
}

pub fn decode_start(b: &[u8]) -> Option<Start> {
    (b.len() == 12 && b[0] == OP_SONG_START).then_some(())?;
    Some(Start {
        perf: u32_at(b, 1)?,
        song: u16_at(b, 5)?,
        instrument: b[7],
        seed: u32_at(b, 8)?,
    })
}

pub fn decode_result(b: &[u8]) -> Option<SongResult> {
    (b.len() == 26 && b[0] == OP_SONG_RESULT).then_some(())?;
    let v = |i: usize| u16_at(b, 8 + 2 * i);
    Some(SongResult {
        perf: u32_at(b, 1)?,
        song: u16_at(b, 5)?,
        flags: b[7],
        perfect: v(0)?,
        good: v(1)?,
        miss: v(2)?,
        kept: v(3)?,
        dropped: v(4)?,
        strays: v(5)?,
        max_combo: v(6)?,
        potency_pm: v(7)?,
        quality_pm: v(8)?,
    })
}

pub fn decode_buffs(b: &[u8]) -> Option<Vec<Buff>> {
    (b.first() == Some(&OP_BUFFS)).then_some(())?;
    let n = usize::from(*b.get(1)?);
    (b.len() == 2 + 8 * n).then_some(())?;
    (0..n)
        .map(|i| {
            let at = 2 + 8 * i;
            Some(Buff {
                song: u16_at(b, at)?,
                value: f32::from(u16_at(b, at + 2)?) / 10000.0,
                ms_left: u32_at(b, at + 4)?,
            })
        })
        .collect()
}

/// The skill (op 56): tier, skill, cap.
pub fn decode_skill(b: &[u8]) -> Option<(u8, u16, u16)> {
    (b.len() == 6 && b[0] == OP_SKILL).then_some(())?;
    Some((b[1], u16_at(b, 2)?, u16_at(b, 4)?))
}

pub fn decode_heard(b: &[u8]) -> Option<Heard> {
    (b.first() == Some(&OP_HEARD)).then_some(())?;
    let (name, end) = str_at(b, 13)?;
    (end == b.len()).then_some(())?;
    Some(Heard {
        performer: u64_at(b, 1)?,
        song: u16_at(b, 9)?,
        potency_pm: u16_at(b, 11)?,
        name,
    })
}

pub fn encode_tip(performer: u64, copper: u32) -> Vec<u8> {
    let mut b = vec![OP_TIP];
    b.extend_from_slice(&performer.to_le_bytes());
    b.extend_from_slice(&copper.to_le_bytes());
    b
}

/// The tip's answer (op 59): ok, copper, text.
pub fn decode_tipped(b: &[u8]) -> Option<(bool, u32, String)> {
    (b.first() == Some(&OP_TIPPED)).then_some(())?;
    let ok = *b.get(1)? != 0;
    let copper = u32_at(b, 2)?;
    let (text, end) = str_at(b, 6)?;
    (end == b.len()).then_some((ok, copper, text))
}

pub fn encode_hits(perf: u32, presses: &[Press]) -> Vec<u8> {
    let n = presses.len().min(255);
    let mut b = vec![OP_SONG_HITS];
    b.extend_from_slice(&perf.to_le_bytes());
    b.push(n as u8);
    for p in &presses[..n] {
        b.extend_from_slice(&p.ms.to_le_bytes());
        b.push((p.lane & 0x7F) | if p.down { LANE_DOWN } else { 0 });
    }
    b
}

pub fn encode_stop(perf: u32) -> Vec<u8> {
    let mut b = vec![OP_SONG_STOP];
    b.extend_from_slice(&perf.to_le_bytes());
    b
}

/// "1g 20s", "50s", "3c".
pub fn money_text(copper: u32) -> String {
    let (g, s, c) = (copper / 10000, (copper / 100) % 100, copper % 100);
    let mut parts = Vec::new();
    if g > 0 {
        parts.push(format!("{g}g"));
    }
    if s > 0 {
        parts.push(format!("{s}s"));
    }
    if c > 0 || parts.is_empty() {
        parts.push(format!("{c}c"));
    }
    parts.join(" ")
}

// ── The keys ──

/// The slot keys' letters, slot 0..5 (the default binds: 1, 2, 3, 4, Q, E).
pub const SLOT_KEYS: [char; 6] = ['1', '2', '3', '4', 'Q', 'E'];

/// The slots an instrument's lanes are played on, left to right: `keys` (as
/// `WOMW_MINSTREL_KEYS` spells them, `12QE`) when it names enough distinct ones, else the bar's
/// rightmost `lanes` (Q and E for two: the fingers on WASD reach them without moving).
pub fn lane_slots(lanes: u8, keys: Option<&str>) -> Vec<usize> {
    let n = usize::from(lanes).clamp(1, 6);
    if let Some(k) = keys {
        let mut out: Vec<usize> = Vec::new();
        for c in k.chars() {
            if let Some(i) = SLOT_KEYS.iter().position(|s| s.eq_ignore_ascii_case(&c))
                && !out.contains(&i)
            {
                out.push(i);
            }
        }
        if out.len() >= n {
            out.truncate(n);
            return out;
        }
    }
    (6 - n..6).collect()
}

/// The song an instrument plays next: the one picked when it can still be played, else the first
/// the skill can play. None: no song yet.
pub fn picked_song(i: Instrument, skill: u16, picked: Option<u16>) -> Option<&'static rules::Song> {
    let songs = rules::playable(i, skill);
    picked
        .and_then(|id| songs.iter().find(|s| s.id == id).copied())
        .or_else(|| songs.first().copied())
}

/// The next song after `current` among those the skill can play (wrapping).
pub fn next_song(i: Instrument, skill: u16, current: Option<u16>) -> Option<&'static rules::Song> {
    let songs = rules::playable(i, skill);
    let at = current.and_then(|id| songs.iter().position(|s| s.id == id));
    match at {
        Some(k) => songs.get((k + 1) % songs.len()).copied(),
        None => songs.first().copied(),
    }
}

// ── The layout ──

/// Where the highway goes, logical pixels from the top-left.
#[derive(Clone, Debug, PartialEq)]
pub struct HighwayLayout {
    pub rect: Rect,
    /// Each lane's centre, x, inside `rect` (from its left).
    pub lane_x: Vec<f32>,
    pub lane_w: f32,
    /// The hit line, y inside `rect` (from its top).
    pub hit_y: f32,
}

/// The highway's height in UI units, and its room under the hit line for the keys.
pub const HEIGHT_UNITS: f32 = 190.0;
pub const KEY_ROOM_UNITS: f32 = 16.0;
const PAD_UNITS: f32 = 6.0;

/// Lay the highway out: over the bar buttons of `slots` when they were measured (`buttons`,
/// logical pixels), its bottom just above them; otherwise centred above the bottom of the screen.
/// `k` is pixels per UI unit. The highway stays on screen and never covers the buttons.
pub fn layout(slots: &[usize], buttons: Option<&[Rect; 6]>, viewport: Vec2, k: f32) -> HighwayLayout {
    let n = slots.len().max(1);
    let pad = PAD_UNITS * k;
    let mut height = HEIGHT_UNITS * k;
    let (left, right, bottom, lane_w, centres) = match buttons {
        Some(b) => {
            let used: Vec<Rect> = slots.iter().map(|&s| b[s.min(5)]).collect();
            let w = used.iter().map(|r| r.width()).fold(0.0, f32::max).max(8.0);
            let left = used.iter().map(|r| r.min.x).fold(f32::MAX, f32::min) - pad;
            let right = used.iter().map(|r| r.max.x).fold(f32::MIN, f32::max) + pad;
            let top = used.iter().map(|r| r.min.y).fold(f32::MAX, f32::min);
            let c: Vec<f32> = used.iter().map(|r| r.center().x).collect();
            (left, right, top - 4.0 * k, w, c)
        }
        None => {
            let step = 42.0 * k;
            let w = 36.0 * k;
            let width = step * n as f32 - (step - w) + 2.0 * pad;
            let left = (viewport.x - width) / 2.0;
            let c = (0..n).map(|i| left + pad + w / 2.0 + step * i as f32).collect();
            (left, left + width, viewport.y - 110.0 * k, w, c)
        }
    };
    let bottom = bottom.clamp(height.min(viewport.y), viewport.y);
    height = height.min(bottom);
    let rect = Rect::new(left.max(0.0), bottom - height, right.min(viewport.x), bottom);
    HighwayLayout {
        lane_x: centres.into_iter().map(|x| x - rect.min.x).collect(),
        lane_w,
        hit_y: height - KEY_ROOM_UNITS * k,
        rect,
    }
}

/// A note's top inside the highway: at the hit line when due, `scroll_ms` before it at the top.
pub fn note_y(note_ms: u32, now_ms: f32, scroll_ms: u16, hit_y: f32) -> f32 {
    let ahead = (note_ms as f32 - now_ms) / f32::from(scroll_ms.max(1));
    hit_y - ahead * hit_y
}

/// The meter's fill (0..1) for a potency: the floor empty, the ceiling full.
pub fn meter_fill(d: &InstrumentDef, potency: f32) -> f32 {
    ((potency - d.floor) / (d.ceiling - d.floor)).clamp(0.0, 1.0)
}

/// The Guitar Hero lane colours, left to right.
pub const LANE_COLOURS: [Color; 6] = [
    Color::srgb(0.2, 0.85, 0.25),
    Color::srgb(0.9, 0.2, 0.2),
    Color::srgb(0.95, 0.85, 0.2),
    Color::srgb(0.25, 0.5, 0.95),
    Color::srgb(0.95, 0.55, 0.15),
    Color::srgb(0.7, 0.35, 0.9),
];

// ── Sounds (WoW's own) ──

/// The metronome's tick (an action button's click), and the count-in bar's first beat.
pub const TICK_FILE: &str = "Sound\\interface\\uChatScrollButton.wav";
pub const ACCENT_KIT: &str = "igMainMenuOptionCheckBoxOn";

/// A struck note's sound, per instrument (SoundEntries kits by name).
pub fn strike_kit(i: Instrument) -> &'static str {
    match i {
        Instrument::HandDrum => "Unarmed_ShieldWood",
        Instrument::Lute => "BowRelease",
        Instrument::Flute => "PickUpRing",
        Instrument::Harp => "PickUpRing",
        Instrument::Bagpipes => "Mace1H_ArmorWood",
        Instrument::ThunderDrums => "Mace2H_ArmorWoodShield",
    }
}

/// What plays as the song starts (its instrument's flourish), a SoundEntries kit.
pub fn start_kit(i: Instrument) -> &'static str {
    match i {
        Instrument::HandDrum | Instrument::ThunderDrums => "TrollDrumLoop",
        Instrument::Lute | Instrument::Harp => "LEVELUPSOUND",
        Instrument::Flute => "FluteRun",
        Instrument::Bagpipes => "HornofEcheyakee",
    }
}

/// The song's end: a gong for the Thunder Drums, the quest chime otherwise.
pub fn end_kit(i: Instrument) -> &'static str {
    if i == Instrument::ThunderDrums { "G_GongTroll01" } else { "QUESTCOMPLETED" }
}

// ── The state ──

/// A song being played.
#[derive(Clone, Debug)]
pub struct Performance {
    pub perf: u32,
    pub song: u16,
    pub chart: Chart,
    pub slots: Vec<usize>,
    /// The app clock (s) at the song's zero.
    pub start: f32,
    pub presses: Vec<Press>,
    pub sent: usize,
    pub last_send: f32,
    pub tally: Tally,
    /// The last beat ticked (-1 none yet).
    pub beat: i32,
    pub stopped: bool,
}

impl Performance {
    pub fn def(&self) -> &'static InstrumentDef {
        self.chart.instrument.def()
    }
    pub fn now_ms(&self, t: f32) -> f32 {
        ((t - self.start) * 1000.0).max(0.0)
    }
    pub fn song_name(&self) -> &'static str {
        rules::song_of(self.song).map_or("Song", |s| s.name)
    }
}

#[derive(Resource, Default)]
pub struct Minstrel {
    pub playing: Option<Performance>,
    /// The last verdict and when it came (s), shown a few seconds.
    pub result: Option<(SongResult, f32)>,
    /// The bar buttons 1-6, measured (logical px), and when next.
    pub buttons: Option<[Rect; 6]>,
    next_measure: f32,
    sounds: Vec<(bool, &'static str)>,
    /// The skill (op 56): tier (0 none), skill, cap.
    pub skill: (u8, u16, u16),
    /// The instrument whose form the bar is in (crate::minstrel::form), if any.
    pub form: Option<Instrument>,
    /// Each instrument's song picked (by its wire value).
    pub picks: HashMap<u8, u16>,
    /// The songs on the player, and their end (the app clock, s).
    pub buffs: Vec<(Buff, f32)>,
    /// The last song heard, until when its tip buttons show, and the tip's answer.
    pub heard: Option<(Heard, f32)>,
    pub tipped: Option<(bool, String)>,
}

impl Minstrel {
    pub fn performing(&self) -> bool {
        self.playing.as_ref().is_some_and(|p| !p.stopped)
    }
}

const RESULT_SECS: f32 = 4.0;
const HEARD_SECS: f32 = 60.0;
const NOTE_POOL: usize = 64;

#[derive(Component)]
struct Root;
#[derive(Component)]
struct LaneCol(usize);
#[derive(Component)]
struct LaneKey(usize);
#[derive(Component)]
struct HitLine;
#[derive(Component)]
struct NoteHead(usize);
#[derive(Component)]
struct NoteTail(usize);
#[derive(Component)]
struct Title;
#[derive(Component)]
struct Combo;
#[derive(Component)]
struct Flash;
#[derive(Component)]
struct MeterFill;
#[derive(Component)]
struct MeterText;
/// The line under the crosshair while an instrument is readied: the song picked.
#[derive(Component)]
struct PickLine;
/// The songs on the player, at the screen's right.
#[derive(Component)]
struct BuffList;
/// A song heard: the line and its tip buttons.
#[derive(Component)]
struct HeardBox;
#[derive(Component)]
struct HeardText;
#[derive(Component, Clone, Copy)]
struct TipButton(u32);

pub struct MinstrelPlugin;

impl Plugin for MinstrelPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Minstrel>()
            .add_plugins((window::MinstrelWindowPlugin, form::FormPlugin))
            .add_systems(Startup, (spawn_highway, spawn_hud))
            .add_systems(
                Update,
                play_lanes
                    .in_set(Mw2Set::Mode)
                    .after(crate::mode::update_mode)
                    .before(ActionSystemsSet),
            )
            .add_systems(
                Update,
                perform.in_set(Mw2Set::Mode).after(ActionSystemsSet),
            )
            .add_systems(
                Update,
                (read_channel, measure_bar, draw_highway, draw_hud, tip_clicks, play_sounds)
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

fn read_channel(time: Res<Time>, mut msgs: MessageReader<FusionChannelReceived>, mut m: ResMut<Minstrel>) {
    let t = time.elapsed_secs();
    for FusionChannelReceived(msg) in msgs.read() {
        if msg.channel != CHANNEL {
            continue;
        }
        let b = &msg.body;
        match b.first() {
            Some(&OP_SONG_START) => {
                let Some(s) = decode_start(b) else { continue };
                let Some(i) = Instrument::from_wire(s.instrument) else { continue };
                let chart = rules::make_chart(i, s.seed);
                let keys = std::env::var("WOMW_MINSTREL_KEYS").ok();
                info!("benilla-mw2: minstrel: {} ({} notes), song {}", i.def().name, chart.notes.len(), s.song);
                m.sounds.push((true, start_kit(i)));
                m.playing = Some(Performance {
                    perf: s.perf,
                    song: s.song,
                    slots: lane_slots(i.def().lanes, keys.as_deref()),
                    tally: rules::score(&chart, &[], 0),
                    chart,
                    start: t,
                    presses: Vec::new(),
                    sent: 0,
                    last_send: t,
                    beat: -1,
                    stopped: false,
                });
                m.result = None;
            }
            Some(&OP_SONG_RESULT) => {
                let Some(r) = decode_result(b) else { continue };
                if m.playing.as_ref().is_some_and(|p| p.perf == r.perf) {
                    if let Some(p) = &m.playing {
                        let end = end_kit(p.chart.instrument);
                        m.sounds.push((true, end));
                    }
                    m.playing = None;
                }
                m.result = Some((r, t));
            }
            Some(&OP_BUFFS) => {
                if let Some(list) = decode_buffs(b) {
                    m.buffs = list.into_iter().map(|x| (x, t + x.ms_left as f32 / 1000.0)).collect();
                }
            }
            Some(&OP_SKILL) => {
                if let Some(s) = decode_skill(b) {
                    m.skill = s;
                }
            }
            Some(&OP_HEARD) => {
                if let Some(h) = decode_heard(b) {
                    m.heard = Some((h, t + HEARD_SECS));
                    m.tipped = None;
                }
            }
            Some(&OP_TIPPED) => {
                if let Some((ok, _, text)) = decode_tipped(b) {
                    m.tipped = Some((ok, text));
                }
            }
            _ => {}
        }
    }
}

/// With an instrument in the hands: its key again picks the next song, fire plays the one
/// picked (or stops the song playing). Fire never swings an instrument.
fn perform(
    state: Res<FpsState>,
    mut systems: ResMut<ActionSystems>,
    mut input: ResMut<Mw2Input>,
    mut m: ResMut<Minstrel>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    let again = systems.tool_again.take();
    let Some(tool) = systems.tool_out().filter(|a| a.is_instrument() && state.active && !state.building) else {
        return;
    };
    let Some(inst) = rules::def_of_tool(tool.id()).map(|d| d.id) else { return };
    let skill = m.skill.1;
    if again == Some(tool)
        && let Some(next) = next_song(inst, skill, m.picks.get(&(inst as u8)).copied())
    {
        m.picks.insert(inst as u8, next.id);
    }
    let fire = input.fire.pressed;
    input.fire = Default::default();
    if !fire {
        return;
    }
    if let Some(p) = m.playing.as_mut().filter(|p| !p.stopped) {
        let presses = std::mem::take(&mut p.presses);
        let rest = &presses[p.sent.min(presses.len())..];
        for chunk in rest.chunks(MAX_BATCH) {
            out.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: encode_hits(p.perf, chunk) }));
        }
        p.presses = presses;
        p.sent = p.presses.len();
        out.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: encode_stop(p.perf) }));
        p.stopped = true;
        return;
    }
    if let Some(song) = picked_song(inst, skill, m.picks.get(&(inst as u8)).copied()) {
        out.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL,
            body: crate::tools::encode_strike(tool, u64::from(song.id)),
        }));
    }
}

/// The lanes: the slot keys while a song plays (taken from the bar), the batches to the server,
/// the song's end.
fn play_lanes(
    time: Res<Time>,
    state: Res<FpsState>,
    mut input: ResMut<Mw2Input>,
    mut m: ResMut<Minstrel>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    let t = time.elapsed_secs();
    let m = &mut *m;
    let Some(p) = m.playing.as_mut() else { return };
    if !state.in_world {
        m.playing = None;
        return;
    }
    if p.stopped {
        // Waiting for the verdict; give up on it after a while.
        if p.now_ms(t) > (p.chart.length_ms + 6000) as f32 {
            m.playing = None;
        }
        return;
    }
    let now = p.now_ms(t) as u32;
    let metronome = std::env::var("WOMW_MINSTREL_METRONOME").map_or(true, |v| v != "0");
    // The metronome: a tick each beat, the bar's first one marked.
    let beat = (now / p.chart.beat_ms.max(1)) as i32;
    if beat != p.beat && now < p.chart.length_ms {
        p.beat = beat;
        if metronome {
            m.sounds.push((beat % 4 != 0, if beat % 4 == 0 { ACCENT_KIT } else { TICK_FILE }));
        }
    }
    let typing = state.chat_focus || state.typing;
    for (lane, &slot) in p.slots.iter().enumerate() {
        let c = input.slots[slot];
        if typing {
            continue;
        }
        if c.pressed {
            p.presses.push(Press { ms: now, lane: lane as u8, down: true });
        }
        if c.released {
            p.presses.push(Press { ms: now, lane: lane as u8, down: false });
        }
    }
    if !typing {
        // The keys play the song, not the bar.
        for c in &mut input.slots {
            *c = Default::default();
        }
    }
    let before = (p.tally.perfect, p.tally.good);
    p.tally = rules::score(&p.chart, &p.presses, now);
    if (p.tally.perfect, p.tally.good) != before {
        m.sounds.push((true, strike_kit(p.chart.instrument)));
    }
    let over = now > p.chart.length_ms + 400;
    let quit = input.switch_system || input.sheathe;
    if (t - p.last_send >= SEND_SECS || over || quit) && p.sent < p.presses.len() {
        while p.sent < p.presses.len() {
            let end = (p.sent + MAX_BATCH).min(p.presses.len());
            out.write(SendFusionChannel(ChannelMessage {
                channel: CHANNEL,
                body: encode_hits(p.perf, &p.presses[p.sent..end]),
            }));
            p.sent = end;
        }
        p.last_send = t;
    }
    if over || quit {
        out.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: encode_stop(p.perf) }));
        p.stopped = true;
    }
}

/// The bar buttons 1-6 where the stock bar has them (`ActionButton1`-`6`), as screen fractions.
pub const BUTTONS_LUA: &str = r#"
local sw = UIParent:GetWidth() * UIParent:GetEffectiveScale()
local sh = UIParent:GetHeight() * UIParent:GetEffectiveScale()
local out = { sw, sh }
for i = 1, 6 do
  local f = getglobal("ActionButton" .. i)
  if not f or not f:IsVisible() or not f:GetLeft() then return nil end
  local s = f:GetEffectiveScale()
  table.insert(out, f:GetLeft() * s) table.insert(out, f:GetBottom() * s)
  table.insert(out, f:GetRight() * s) table.insert(out, f:GetTop() * s)
end
return table.concat(out, ",")
"#;

/// [`BUTTONS_LUA`]'s answer on a viewport: each button's rectangle, logical pixels from the
/// top-left. `None` when anything is off.
pub fn buttons_from_lua(s: &str, viewport: Vec2) -> Option<[Rect; 6]> {
    let v: Vec<f32> = s.split(',').map(|x| x.trim().parse::<f32>().ok()).collect::<Option<_>>()?;
    if v.len() != 26 || !v.iter().all(|x| x.is_finite()) || v[0] <= 0.0 || v[1] <= 0.0 {
        return None;
    }
    let (sw, sh) = (v[0], v[1]);
    let mut out = [Rect::default(); 6];
    for (i, r) in out.iter_mut().enumerate() {
        let [l, b, rt, tp] = [v[2 + 4 * i], v[3 + 4 * i], v[4 + 4 * i], v[5 + 4 * i]];
        if rt <= l || tp <= b {
            return None;
        }
        *r = Rect::new(
            l / sw * viewport.x,
            (1.0 - tp / sh) * viewport.y,
            rt / sw * viewport.x,
            (1.0 - b / sh) * viewport.y,
        );
    }
    Some(out)
}

fn measure_bar(
    time: Res<Time>,
    script: Option<NonSend<UiScript>>,
    window: Query<&Window, With<bevy::window::PrimaryWindow>>,
    mut m: ResMut<Minstrel>,
) {
    let t = time.elapsed_secs();
    if (m.playing.is_none() && m.form.is_none()) || t < m.next_measure {
        return;
    }
    m.next_measure = t + 0.5;
    let (Some(script), Ok(w)) = (script, window.single()) else { return };
    let s = script.eval::<Option<String>>(BUTTONS_LUA).ok().flatten();
    let b = s.and_then(|s| buttons_from_lua(&s, Vec2::new(w.width(), w.height())));
    if m.buttons != b {
        m.buttons = b;
    }
}

fn play_sounds(state: Res<FpsState>, script: Option<NonSend<UiScript>>, mut m: ResMut<Minstrel>) {
    if m.sounds.is_empty() {
        return;
    }
    let sounds = std::mem::take(&mut m.sounds);
    let Some(script) = script.filter(|_| state.in_world) else { return };
    let mut played = Vec::new();
    for (kit, s) in sounds {
        if played.contains(&s) {
            continue;
        }
        played.push(s);
        if kit {
            crate::lua::sound(&script, s);
        } else {
            crate::lua::run(&script, &format!("PlaySoundFile({})", crate::lua::quote(s)));
        }
    }
}

fn spawn_highway(mut commands: Commands, assets: Res<AssetServer>) {
    let font: Handle<Font> = assets.load(FONT);
    let text = |s: &str, size: f32, c: Color| {
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
            Name::new("minstrel highway"),
            Root,
            Node {
                position_type: PositionType::Absolute,
                display: Display::None,
                border: UiRect::all(px(1)),
                ..default()
            },
            BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.55)),
            BorderColor::all(Color::srgba(0.75, 0.6, 0.2, 0.9)),
            Pickable::IGNORE,
        ))
        .with_children(|r| {
            for i in 0..6 {
                r.spawn((
                    LaneCol(i),
                    Node { position_type: PositionType::Absolute, top: px(0), ..default() },
                    BackgroundColor(LANE_COLOURS[i].with_alpha(0.08)),
                    Pickable::IGNORE,
                ));
                r.spawn((LaneKey(i), Node { position_type: PositionType::Absolute, ..default() }))
                    .insert(text("", 11.0, WHITE));
            }
            r.spawn((
                HitLine,
                Node { position_type: PositionType::Absolute, left: px(0), height: px(2), ..default() },
                BackgroundColor(GOLD.with_alpha(0.9)),
                Pickable::IGNORE,
            ));
            for i in 0..NOTE_POOL {
                r.spawn((
                    NoteTail(i),
                    Node { position_type: PositionType::Absolute, display: Display::None, ..default() },
                    BackgroundColor(Color::WHITE),
                    Pickable::IGNORE,
                ));
                r.spawn((
                    NoteHead(i),
                    Node {
                        position_type: PositionType::Absolute,
                        display: Display::None,
                        border: UiRect::all(px(1)),
                        ..default()
                    },
                    BackgroundColor(Color::WHITE),
                    BorderColor::all(Color::srgba(0.0, 0.0, 0.0, 0.9)),
                    Pickable::IGNORE,
                ));
            }
            r.spawn((Title, Node { position_type: PositionType::Absolute, left: px(4), top: px(-18), ..default() }))
                .insert(text("", 12.0, GOLD));
            r.spawn((Combo, Node { position_type: PositionType::Absolute, right: px(4), top: px(4), ..default() }))
                .insert(text("", 12.0, WHITE));
            r.spawn((Flash, Node { position_type: PositionType::Absolute, left: px(4), top: px(22), ..default() }))
                .insert(text("", 14.0, WHITE));
            // The potency meter: a bar down the highway's right edge, outside it.
            r.spawn((
                Node {
                    position_type: PositionType::Absolute,
                    right: px(-12),
                    top: px(0),
                    bottom: px(0),
                    width: px(8),
                    border: UiRect::all(px(1)),
                    flex_direction: FlexDirection::ColumnReverse,
                    ..default()
                },
                BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.7)),
                BorderColor::all(Color::srgba(0.75, 0.6, 0.2, 0.9)),
                Pickable::IGNORE,
            ))
            .with_children(|m| {
                m.spawn((
                    MeterFill,
                    Node { width: percent(100), height: percent(0), ..default() },
                    BackgroundColor(GOLD),
                    Pickable::IGNORE,
                ));
            });
            r.spawn((MeterText, Node { position_type: PositionType::Absolute, right: px(-12), top: px(-18), ..default() }))
                .insert(text("", 11.0, GOLD));
        });
}

/// The pick line, the songs on the player and the heard line with its tips.
fn spawn_hud(mut commands: Commands, assets: Res<AssetServer>) {
    let font: Handle<Font> = assets.load(FONT);
    let shadow = TextShadow { offset: Vec2::splat(1.5), color: Color::srgba(0.0, 0.0, 0.0, 0.85) };
    commands.spawn((
        Name::new("minstrel pick line"),
        PickLine,
        Node {
            position_type: PositionType::Absolute,
            left: percent(50),
            top: percent(66),
            margin: UiRect::left(px(-300)),
            width: px(600),
            justify_content: JustifyContent::Center,
            ..default()
        },
        Text::new(""),
        TextFont::from_font_size(14.0).with_font(font.clone()),
        TextColor(GOLD),
        TextLayout::new_with_justify(Justify::Center),
        shadow,
        Pickable::IGNORE,
    ));
    commands.spawn((
        Name::new("minstrel songs on you"),
        BuffList,
        Node { position_type: PositionType::Absolute, right: px(16), top: px(230), ..default() },
        Text::new(""),
        TextFont::from_font_size(12.0).with_font(font.clone()),
        TextColor(Color::srgb(0.6, 0.85, 1.0)),
        TextLayout::new_with_justify(Justify::Right),
        shadow,
        Pickable::IGNORE,
    ));
    commands
        .spawn((
            Name::new("minstrel heard"),
            HeardBox,
            Node {
                position_type: PositionType::Absolute,
                right: px(16),
                top: px(340),
                flex_direction: FlexDirection::Column,
                align_items: AlignItems::FlexEnd,
                row_gap: px(2),
                ..default()
            },
            Visibility::Hidden,
        ))
        .with_children(|b| {
            b.spawn((
                HeardText,
                Text::new(""),
                TextFont::from_font_size(12.0).with_font(font.clone()),
                TextColor(WHITE),
                TextLayout::new_with_justify(Justify::Right),
                shadow,
                Pickable::IGNORE,
            ));
            b.spawn(Node { flex_direction: FlexDirection::Row, ..default() }).with_children(|r| {
                for c in TIPS {
                    let tip = crate::wow_ui::WowTip::new("Tip", &format!("Give {} to the minstrel.", money_text(c)));
                    r.spawn(crate::wow_ui::button(&font, &format!("Tip {}", money_text(c)), 64.0, (TipButton(c), tip)));
                }
            });
        });
}

/// The line under the crosshair for a readied instrument: the song picked, or why none.
pub fn pick_text(inst: Instrument, skill: (u8, u16, u16), picked: Option<u16>, playing: bool) -> String {
    let d = inst.def();
    if skill.0 == 0 {
        return format!("{}: learn Minstrelsy from a Minstrel Trainer", d.name);
    }
    if playing {
        return format!("{}: fire again to stop", d.name);
    }
    match picked_song(inst, skill.1, picked) {
        Some(s) => {
            let kind = match s.effect {
                rules::Effect::Travel => "travel song",
                rules::Effect::Rested => "inn song",
                rules::Effect::Campfire => "fire song",
                _ => "combat song",
            };
            let more = rules::playable(inst, skill.1).len() > 1;
            format!(
                "{}: {} ({kind})  -  fire to play{}",
                d.name,
                s.name,
                if more { ", the Song button for the next song" } else { "" }
            )
        }
        None => format!("{}: no song yet (Minstrelsy {})", d.name, d.skill),
    }
}

/// A song on the player as its line reads: the name, what it gives, the time left.
pub fn buff_line(b: &Buff, secs_left: f32) -> Option<String> {
    let s = rules::song_of(b.song)?;
    let left = secs_left.max(0.0) as u32;
    let time = if left >= 60 { format!("{}m", left.div_ceil(60)) } else { format!("{left}s") };
    let what = if s.effect == rules::Effect::Rested {
        "rested (no more songs' rest until it fades)".to_owned()
    } else {
        rules::effect_line(s, b.value)
    };
    Some(format!("{}: {what}  {time}", s.name))
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn draw_hud(
    time: Res<Time>,
    state: Res<FpsState>,
    systems: Res<ActionSystems>,
    m: Res<Minstrel>,
    mut pick: Query<&mut Text, (With<PickLine>, Without<BuffList>, Without<HeardText>)>,
    mut list: Query<&mut Text, (With<BuffList>, Without<PickLine>, Without<HeardText>)>,
    mut heard: Query<(&mut Text, &mut TextColor), (With<HeardText>, Without<PickLine>, Without<BuffList>)>,
    mut heard_box: Query<&mut Visibility, With<HeardBox>>,
) {
    let t = time.elapsed_secs();
    let line = systems
        .tool_out()
        .filter(|a| a.is_instrument() && state.active)
        .and_then(|a| rules::def_of_tool(a.id()))
        .map(|d| pick_text(d.id, m.skill, m.picks.get(&(d.id as u8)).copied(), m.performing()))
        .unwrap_or_default();
    for mut text in &mut pick {
        if text.0 != line {
            text.0 = line.clone();
        }
    }
    let buffs = if state.active {
        m.buffs
            .iter()
            .filter(|(_, until)| *until > t)
            .filter_map(|(b, until)| buff_line(b, until - t))
            .collect::<Vec<_>>()
            .join("\n")
    } else {
        String::new()
    };
    for mut text in &mut list {
        if text.0 != buffs {
            text.0 = buffs.clone();
        }
    }
    let shown = state.in_world && m.heard.as_ref().is_some_and(|(_, until)| *until > t);
    for mut v in &mut heard_box {
        let want = if shown { Visibility::Inherited } else { Visibility::Hidden };
        if *v != want {
            *v = want;
        }
    }
    if let Some((h, _)) = m.heard.as_ref().filter(|_| shown) {
        let song = rules::song_of(h.song).map_or("a song", |s| s.name);
        let (s, c) = match &m.tipped {
            Some((ok, text)) => (text.clone(), if *ok { crate::wow_ui::GREEN } else { crate::wow_ui::RED }),
            None => (
                format!("{} played {song} ({}%). Tip? (Alt frees the cursor)", h.name, (u32::from(h.potency_pm) + 5) / 10),
                WHITE,
            ),
        };
        for (mut text, mut colour) in &mut heard {
            if text.0 != s {
                text.0 = s.clone();
            }
            if colour.0 != c {
                colour.0 = c;
            }
        }
    }
}

fn tip_clicks(
    m: Res<Minstrel>,
    buttons: Query<(&Interaction, &TipButton), Changed<Interaction>>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    let Some((h, _)) = m.heard.as_ref() else { return };
    for (i, b) in &buttons {
        if *i == Interaction::Pressed {
            out.write(SendFusionChannel(ChannelMessage { channel: CHANNEL, body: encode_tip(h.performer, b.0) }));
        }
    }
}

/// An instrument as its tooltip tells it: the keys, the tempo, the difficulty and the range.
pub fn instrument_line(d: &InstrumentDef) -> String {
    let keys: Vec<String> = lane_slots(d.lanes, None).iter().map(|&s| SLOT_KEYS[s].to_string()).collect();
    let mut extras = Vec::new();
    if d.max_chord > 1 {
        extras.push("chords");
    }
    if d.hold > 0 {
        extras.push("held notes");
    }
    if d.run > 0 {
        extras.push("runs");
    }
    let extras = if extras.is_empty() { String::new() } else { format!(", {}", extras.join(", ")) };
    format!(
        "Minstrelsy {}. Played as a rhythm song: {} keys ({}), {} beats a minute, {} bars{extras}. Difficulty {} of 6. Its songs run from {}% played poorly to {}% played perfectly.",
        d.skill,
        d.lanes,
        keys.join(" "),
        d.bpm,
        d.bars,
        rules::stars(d),
        (d.floor * 100.0).round() as u32,
        (d.ceiling * 100.0).round() as u32
    )
}

/// The verdict's line: the potency, the counts, and why it fell flat.
pub fn result_line(r: &SongResult) -> String {
    let name = rules::song_of(r.song).map_or("Song", |s| s.name);
    if r.flags & RESULT_REJECTED != 0 {
        return format!("{name}: the song falls apart");
    }
    if r.flags & RESULT_CUT != 0 && r.flags & RESULT_FLAT != 0 {
        return format!("{name}: the song is cut short");
    }
    if r.flags & RESULT_FLAT != 0 {
        return format!("{name}: the song falls flat");
    }
    let mut s = format!(
        "{name} {}%  -  {} perfect, {} good, {} missed, best combo {}",
        (u32::from(r.potency_pm) + 5) / 10,
        r.perfect,
        r.good,
        r.miss + r.dropped,
        r.max_combo
    );
    if r.flags & RESULT_ROBOTIC != 0 {
        s.push_str(" (too even: capped)");
    }
    if r.flags & RESULT_SKILLUP != 0 {
        s.push_str("  -  Minstrelsy up");
    }
    s
}

#[allow(clippy::too_many_arguments)]
fn draw_highway(
    time: Res<Time>,
    state: Res<FpsState>,
    bard: Res<Minstrel>,
    seam: Option<Res<crate::wow_ui::UiSeam>>,
    keymap: Res<crate::keybinds::Keymap>,
    window: Query<&Window, With<bevy::window::PrimaryWindow>>,
    mut root: Query<&mut Node, With<Root>>,
    mut cols: Query<(&LaneCol, &mut Node, &mut Visibility), Without<Root>>,
    mut keys: Query<(&LaneKey, &mut Node, &mut Text, &mut Visibility), (Without<Root>, Without<LaneCol>)>,
    mut hit: Query<&mut Node, (With<HitLine>, Without<Root>, Without<LaneCol>, Without<LaneKey>)>,
    mut heads: Query<
        (&NoteHead, &mut Node, &mut BackgroundColor),
        (Without<Root>, Without<LaneCol>, Without<LaneKey>, Without<HitLine>),
    >,
    mut tails: Query<
        (&NoteTail, &mut Node, &mut BackgroundColor),
        (Without<Root>, Without<LaneCol>, Without<LaneKey>, Without<HitLine>, Without<NoteHead>),
    >,
    mut texts: Query<(&mut Text, &mut TextColor, Has<Title>, Has<Combo>, Has<Flash>, Has<MeterText>), (Without<LaneKey>, Without<PickLine>, Without<BuffList>, Without<HeardText>)>,
    mut fill: Query<&mut Node, (With<MeterFill>, Without<Root>, Without<LaneCol>, Without<LaneKey>, Without<HitLine>, Without<NoteHead>, Without<NoteTail>)>,
) {
    let t = time.elapsed_secs();
    let Ok(mut rn) = root.single_mut() else { return };
    let showing_result = bard.result.as_ref().is_some_and(|(_, at)| t - at < RESULT_SECS);
    let shown = state.in_world && (bard.playing.is_some() || showing_result);
    let want = if shown { Display::Flex } else { Display::None };
    if rn.display != want {
        rn.display = want;
    }
    if !shown {
        return;
    }
    let Ok(w) = window.single() else { return };
    let k = crate::wow_ui::seam_scale(w.height(), seam.map_or(0.9, |s| s.ui_scale)).clamp(0.4, 4.0);
    let viewport = Vec2::new(w.width(), w.height());
    let Some(p) = bard.playing.as_ref() else {
        // The verdict alone: a slim panel over the bar.
        let lay = layout(&[4, 5], bard.buttons.as_ref(), viewport, k);
        rn.left = px(lay.rect.min.x);
        rn.top = px(lay.rect.max.y - 30.0 * k);
        rn.width = px(lay.rect.width().max(300.0 * k));
        rn.height = px(30.0 * k);
        for (_, _, mut v) in &mut cols {
            *v = Visibility::Hidden;
        }
        for (_, _, _, mut v) in &mut keys {
            *v = Visibility::Hidden;
        }
        for (_, mut n, _) in &mut heads {
            n.display = Display::None;
        }
        for (_, mut n, _) in &mut tails {
            n.display = Display::None;
        }
        for (mut text, mut colour, title, combo, flash, meter) in &mut texts {
            let s = match (title, combo, flash, meter, &bard.result) {
                (true, _, _, _, Some((r, _))) => {
                    colour.0 = if r.flags & (RESULT_FLAT | RESULT_REJECTED) != 0 { crate::wow_ui::RED } else { GOLD };
                    result_line(r)
                }
                _ => String::new(),
            };
            if text.0 != s {
                text.0 = s;
            }
        }
        return;
    };
    let d = p.def();
    let lay = layout(&p.slots, bard.buttons.as_ref(), viewport, k);
    rn.left = px(lay.rect.min.x);
    rn.top = px(lay.rect.min.y);
    rn.width = px(lay.rect.width());
    rn.height = px(lay.rect.height());
    let now = p.now_ms(t);
    let lanes = p.slots.len();
    let lane_w = lay.lane_w * 0.9;
    for (c, mut n, mut v) in &mut cols {
        *v = if c.0 < lanes { Visibility::Inherited } else { Visibility::Hidden };
        if c.0 < lanes {
            n.left = px(lay.lane_x[c.0] - lane_w / 2.0);
            n.width = px(lane_w);
            n.height = px(lay.hit_y);
        }
    }
    for (c, mut n, mut text, mut v) in &mut keys {
        *v = if c.0 < lanes { Visibility::Inherited } else { Visibility::Hidden };
        if c.0 < lanes {
            let slot = p.slots[c.0];
            let label = keymap
                .keys(crate::keybinds::SLOTS[slot])
                .first()
                .map_or_else(|| SLOT_KEYS[slot].to_string(), |s| crate::action_systems::short_key(s));
            if text.0 != label {
                text.0 = label;
            }
            n.left = px(lay.lane_x[c.0] - 4.0 * k);
            n.top = px(lay.hit_y + 2.0);
        }
    }
    if let Ok(mut h) = hit.single_mut() {
        h.top = px(lay.hit_y - 1.0);
        h.width = px(lay.rect.width());
    }
    // The notes in view: from just past the hit line to the top.
    let head_h = (10.0 * k).max(6.0);
    let mut shown_notes = Vec::new();
    for (i, n) in p.chart.notes.iter().enumerate() {
        let y = note_y(n.ms, now, d.scroll_ms, lay.hit_y);
        let tail_top = note_y(n.ms + n.hold_ms, now, d.scroll_ms, lay.hit_y);
        if tail_top > lay.hit_y + head_h || y < -head_h {
            continue;
        }
        let state = p.tally.states.get(i).copied().unwrap_or_default();
        // A struck tap is gone; a struck hold shows its tail down to the line while held.
        if matches!(state, NoteState::Struck(_)) && !(n.hold_ms > 0 && p.tally.holding.get(i) == Some(&true)) {
            continue;
        }
        shown_notes.push((i, y, tail_top, state));
        if shown_notes.len() >= NOTE_POOL {
            break;
        }
    }
    for (h, mut node, mut bg) in &mut heads {
        match shown_notes.get(h.0) {
            Some(&(i, y, _, state)) => {
                let n = p.chart.notes[i];
                let colour = LANE_COLOURS[usize::from(n.lane) % 6];
                let held = matches!(state, NoteState::Struck(_));
                node.display = if held { Display::None } else { Display::Flex };
                node.left = px(lay.lane_x[usize::from(n.lane).min(lanes - 1)] - lane_w / 2.0);
                node.width = px(lane_w);
                node.height = px(head_h);
                node.top = px(y.min(lay.hit_y + head_h) - head_h / 2.0);
                bg.0 = if state == NoteState::Missed { colour.with_alpha(0.25) } else { colour };
            }
            None => {
                if node.display != Display::None {
                    node.display = Display::None;
                }
            }
        }
    }
    for (h, mut node, mut bg) in &mut tails {
        match shown_notes.get(h.0).filter(|s| p.chart.notes[s.0].hold_ms > 0) {
            Some(&(i, y, tail_top, state)) => {
                let n = p.chart.notes[i];
                let bottom = if matches!(state, NoteState::Struck(_)) { lay.hit_y } else { y };
                let top = tail_top.max(0.0);
                node.display = Display::Flex;
                node.left = px(lay.lane_x[usize::from(n.lane).min(lanes - 1)] - lane_w / 6.0);
                node.width = px(lane_w / 3.0);
                node.top = px(top);
                node.height = px((bottom - top).max(0.0));
                let colour = LANE_COLOURS[usize::from(n.lane) % 6];
                bg.0 = if state == NoteState::Missed { colour.with_alpha(0.2) } else { colour.with_alpha(0.75) };
            }
            None => {
                if node.display != Display::None {
                    node.display = Display::None;
                }
            }
        }
    }
    let live = rules::live_potency(d, &p.tally);
    for (mut text, mut colour, title, combo, flash, meter) in &mut texts {
        let s = if title {
            colour.0 = GOLD;
            if now < p.chart.count_in_ms as f32 {
                format!("{}: {}  ({})", d.name, p.song_name(), 4 - (now as u32 / p.chart.beat_ms.max(1)).min(3))
            } else {
                format!("{}: {}", d.name, p.song_name())
            }
        } else if combo {
            format!("x{}  {}", rules::multiplier(u32::from(p.tally.combo)), p.tally.combo)
        } else if flash {
            match p.tally.last {
                Some((at, j)) if now - (at as f32) < 400.0 => {
                    let (s, c) = match j {
                        Judgement::Perfect => ("Perfect", GOLD),
                        Judgement::Good => ("Good", crate::wow_ui::GREEN),
                        Judgement::Miss => ("Miss", crate::wow_ui::RED),
                    };
                    colour.0 = c;
                    s.to_owned()
                }
                _ => String::new(),
            }
        } else if meter {
            format!("{}%", (live * 100.0).round() as u32)
        } else {
            String::new()
        };
        if text.0 != s {
            text.0 = s;
        }
    }
    if let Ok(mut f) = fill.single_mut() {
        f.height = percent(meter_fill(d, live) * 100.0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_wire_is_the_servers() {
        // The server's minstrel_messages_round_trip_on_the_tools_channel_and_the_notes_are_throttled.
        assert_eq!(CHANNEL, 10);
        let mut b = vec![OP_SONG_START];
        b.extend_from_slice(&77u32.to_le_bytes());
        b.extend_from_slice(&7u16.to_le_bytes());
        b.push(3);
        b.extend_from_slice(&0xCAFEBABEu32.to_le_bytes());
        assert_eq!(decode_start(&b), Some(Start { perf: 77, song: 7, instrument: 3, seed: 0xCAFEBABE }));
        b.pop();
        assert_eq!(decode_start(&b), None);
        let hits = encode_hits(9, &[Press { ms: 1500, lane: 3, down: false }, Press { ms: 1501, lane: 0, down: true }]);
        assert_eq!(hits.len(), 1 + 4 + 1 + 2 * 5);
        assert_eq!(hits[6..10], 1500u32.to_le_bytes());
        assert_eq!(hits[10], 3);
        assert_eq!(hits[15], LANE_DOWN);
        assert_eq!(encode_stop(41), vec![OP_SONG_STOP, 41, 0, 0, 0]);
        let mut r = vec![OP_SONG_RESULT];
        r.extend_from_slice(&5u32.to_le_bytes());
        r.extend_from_slice(&12u16.to_le_bytes());
        r.push(RESULT_ROBOTIC | RESULT_SKILLUP);
        for v in [30u16, 4, 2, 3, 1, 6, 25, 1234, 876] {
            r.extend_from_slice(&v.to_le_bytes());
        }
        let res = decode_result(&r).expect("result");
        assert_eq!((res.perf, res.song, res.perfect, res.strays, res.max_combo), (5, 12, 30, 6, 25));
        assert_eq!((res.potency_pm, res.quality_pm), (1234, 876));
        let line = result_line(&res);
        assert!(line.contains("Hymn of Warding 123%") && line.contains("capped") && line.contains("Minstrelsy up"), "{line}");
        let flat = SongResult { song: 1, flags: RESULT_FLAT, ..Default::default() };
        assert!(result_line(&flat).contains("falls flat"));
        let cut = SongResult { song: 1, flags: RESULT_FLAT | RESULT_CUT, ..Default::default() };
        assert!(result_line(&cut).contains("cut short"));
        // The skill, the buffs, a song heard, a tip and its answer.
        assert_eq!(decode_skill(&[OP_SKILL, 3, 201, 0, 225, 0]), Some((3, 201, 225)));
        let mut bf = vec![OP_BUFFS, 1];
        bf.extend_from_slice(&13u16.to_le_bytes());
        bf.extend_from_slice(&1450u16.to_le_bytes());
        bf.extend_from_slice(&60000u32.to_le_bytes());
        let buffs = decode_buffs(&bf).expect("buffs");
        assert_eq!((buffs[0].song, buffs[0].ms_left), (13, 60000));
        assert!((buffs[0].value - 0.145).abs() < 1e-6);
        assert_eq!(decode_buffs(&bf[..bf.len() - 1]), None);
        let mut hb = vec![OP_HEARD];
        hb.extend_from_slice(&42u64.to_le_bytes());
        hb.extend_from_slice(&4u16.to_le_bytes());
        hb.extend_from_slice(&1100u16.to_le_bytes());
        hb.push(4);
        hb.extend_from_slice(b"Lyra");
        assert_eq!(decode_heard(&hb), Some(Heard { performer: 42, song: 4, potency_pm: 1100, name: "Lyra".into() }));
        let tip = encode_tip(0x0102030405060708, 2500);
        assert_eq!(tip.len(), 13);
        assert_eq!(tip[0], OP_TIP);
        let mut tb = vec![OP_TIPPED, 1];
        tb.extend_from_slice(&2500u32.to_le_bytes());
        tb.push(2);
        tb.extend_from_slice(b"ok");
        assert_eq!(decode_tipped(&tb), Some((true, 2500, "ok".into())));
        assert_eq!(money_text(10250), "1g 2s 50c");
        assert_eq!(money_text(100), "1s");
        assert_eq!(money_text(0), "0c");
    }

    #[test]
    fn an_instruments_key_again_picks_its_next_song() {
        // Nothing playable under the Harp's 150; at 180 two songs, cycling.
        assert_eq!(picked_song(Instrument::Harp, 149, None), None);
        let first = picked_song(Instrument::Harp, 180, None).expect("a song");
        assert_eq!(first.name, "Hymn of Rest");
        let second = next_song(Instrument::Harp, 180, Some(first.id)).expect("next");
        assert_eq!(second.name, "Hymn of the Hearth");
        assert_eq!(next_song(Instrument::Harp, 180, Some(second.id)).map(|s| s.id), Some(first.id), "wraps");
        // A pick the skill no longer reaches falls back to the first.
        assert_eq!(picked_song(Instrument::Harp, 180, Some(12)).map(|s| s.id), Some(first.id));
        let line = pick_text(Instrument::Harp, (2, 180, 225), Some(second.id), false);
        assert!(line.contains("Hymn of the Hearth (fire song)") && line.contains("next song"), "{line}");
        assert!(pick_text(Instrument::HandDrum, (0, 0, 0), None, false).contains("Minstrel Trainer"));
        assert!(pick_text(Instrument::HandDrum, (1, 5, 75), None, true).contains("stop"));
    }

    #[test]
    fn the_songs_on_you_read_plainly() {
        let travel = Buff { song: 13, value: 0.145, ms_left: 0 };
        assert_eq!(buff_line(&travel, 125.0).as_deref(), Some("March of the Clans: 14.5% run speed out of combat  3m"));
        let rest = Buff { song: 2, value: 0.01, ms_left: 0 };
        assert!(buff_line(&rest, 30.0).unwrap().contains("rested"));
        assert_eq!(buff_line(&Buff { song: 99, value: 0.0, ms_left: 0 }, 1.0), None);
    }

    #[test]
    fn the_lanes_take_the_rightmost_keys_or_the_ones_asked() {
        assert_eq!(lane_slots(2, None), vec![4, 5]);
        assert_eq!(lane_slots(3, None), vec![3, 4, 5]);
        assert_eq!(lane_slots(6, None), vec![0, 1, 2, 3, 4, 5]);
        assert_eq!(lane_slots(2, Some("12")), vec![0, 1]);
        assert_eq!(lane_slots(3, Some("q1e2")), vec![4, 0, 5]);
        // Too few keys named: the default.
        assert_eq!(lane_slots(4, Some("QQE")), vec![2, 3, 4, 5]);
    }

    fn bar(viewport: Vec2, k: f32) -> [Rect; 6] {
        // The stock bar: 36-unit buttons 6 apart from x = 8, 4 up, on a 1024-wide main bar centred.
        let left0 = (viewport.x - 1024.0 * k) / 2.0;
        std::array::from_fn(|i| {
            let l = left0 + (8.0 + 42.0 * i as f32) * k;
            Rect::new(l, viewport.y - 40.0 * k, l + 36.0 * k, viewport.y - 4.0 * k)
        })
    }

    #[test]
    fn the_highway_sits_over_utilitys_buttons_and_clear_of_the_bar() {
        for (vp, k) in [(Vec2::new(1280.0, 720.0), 0.84), (Vec2::new(1920.0, 1080.0), 1.27), (Vec2::new(1024.0, 768.0), 0.9)] {
            let b = bar(vp, k);
            for lanes in 2..=6u8 {
                let slots = lane_slots(lanes, None);
                let lay = layout(&slots, Some(&b), vp, k);
                assert_eq!(lay.lane_x.len(), slots.len());
                // Each lane is right over its key's button.
                for (x, &s) in lay.lane_x.iter().zip(&slots) {
                    assert!((lay.rect.min.x + x - b[s].center().x).abs() < 0.5);
                }
                // Over the bar, not on it; on screen; tall enough to read the notes coming.
                assert!(lay.rect.max.y <= b[0].min.y);
                assert!(lay.rect.min.x >= 0.0 && lay.rect.max.x <= vp.x && lay.rect.min.y >= 0.0);
                assert!(lay.rect.height() >= 150.0 * k);
                assert!(lay.hit_y > 0.0 && lay.hit_y < lay.rect.height());
                for w in lay.lane_x.windows(2) {
                    assert!(w[1] - w[0] >= lay.lane_w);
                }
            }
            let lay = layout(&[0, 1, 2, 3, 4, 5], None, vp, k);
            assert!((lay.rect.center().x - vp.x / 2.0).abs() < 1.0);
            assert!(lay.rect.max.y < vp.y && lay.rect.min.y > 0.0);
        }
        let lay = layout(&[4, 5], None, Vec2::new(300.0, 120.0), 1.0);
        assert!(lay.rect.min.y >= 0.0 && lay.rect.max.y <= 120.0);
    }

    #[test]
    fn notes_fall_onto_the_line_and_the_meter_fills() {
        assert_eq!(note_y(2000, 2000.0, 1500, 160.0), 160.0);
        assert_eq!(note_y(3500, 2000.0, 1500, 160.0), 0.0);
        assert!(note_y(1900, 2000.0, 1500, 160.0) > 160.0);
        let d = Instrument::Harp.def();
        assert_eq!(meter_fill(d, d.floor), 0.0);
        assert_eq!(meter_fill(d, d.ceiling), 1.0);
        assert_eq!(meter_fill(d, 9.0), 1.0);
    }

    #[test]
    fn the_buttons_are_read_off_the_interface() {
        let vp = Vec2::new(1024.0, 768.0);
        let mut v = vec![1024.0f32, 768.0];
        for i in 0..6 {
            let l = 8.0 + 42.0 * i as f32;
            v.extend([l, 4.0, l + 36.0, 40.0]);
        }
        let s = v.iter().map(|x| x.to_string()).collect::<Vec<_>>().join(",");
        let b = buttons_from_lua(&s, vp).expect("measured");
        assert_eq!(b[0], Rect::new(8.0, 728.0, 44.0, 764.0));
        assert!(buttons_from_lua("1,2,3", vp).is_none());
    }

    #[test]
    fn every_instrument_has_its_sounds_and_its_line() {
        for i in Instrument::ALL {
            assert!(!strike_kit(i).is_empty() && !start_kit(i).is_empty() && !end_kit(i).is_empty());
        }
        let drum = instrument_line(Instrument::HandDrum.def());
        assert!(drum.contains("2 keys (Q E)") && drum.contains("110%") && !drum.contains("chords"), "{drum}");
        let thunder = instrument_line(Instrument::ThunderDrums.def());
        assert!(thunder.contains("Minstrelsy 275") && thunder.contains("6 keys (1 2 3 4 Q E), 132 beats a minute"), "{thunder}");
    }
}
