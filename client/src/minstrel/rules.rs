//! Minstrelsy's instruments, their charts and the judging, and the profession's songs: the
//! server's `fusion/Minstrelsy.h` line for line (integer arithmetic and the same random stream),
//! so a seed makes the very chart the server judges. The client judges too, but only to draw: the
//! server's verdict is the result (op 53).

/// An instrument, by its wire value (`Fusion::Minstrelsy::Instrument`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Instrument {
    HandDrum = 1,
    Lute = 2,
    Flute = 3,
    Harp = 4,
    Bagpipes = 5,
    ThunderDrums = 6,
}

impl Instrument {
    pub const ALL: [Instrument; 6] = [
        Instrument::HandDrum,
        Instrument::Lute,
        Instrument::Flute,
        Instrument::Harp,
        Instrument::Bagpipes,
        Instrument::ThunderDrums,
    ];

    pub fn from_wire(v: u8) -> Option<Instrument> {
        Instrument::ALL.into_iter().find(|i| *i as u8 == v)
    }

    pub fn def(self) -> &'static InstrumentDef {
        &INSTRUMENTS[self as usize - 1]
    }
}

/// One instrument (`InstrumentDef`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct InstrumentDef {
    pub id: Instrument,
    pub name: &'static str,
    /// The Minstrelsy it asks (to play it, and to show on Utility's bar).
    pub skill: u16,
    /// Its Utility ability (crate::action_systems::Ability).
    pub tool: u32,
    pub lanes: u8,
    pub bpm: u16,
    pub bars: u8,
    pub grid: u8,
    pub density: u8,
    pub accent: u8,
    pub chord: u8,
    pub max_chord: u8,
    pub hold: u8,
    pub hold_min: u8,
    pub hold_max: u8,
    pub run: u8,
    pub perfect_ms: u16,
    pub good_ms: u16,
    pub scroll_ms: u16,
    pub floor: f32,
    pub ceiling: f32,
}

/// The six, in skill order (`Fusion::Minstrelsy::Instruments`).
pub static INSTRUMENTS: [InstrumentDef; 6] = [
    InstrumentDef {
        id: Instrument::HandDrum,
        name: "Hand Drum",
        skill: 1,
        tool: 17,
        lanes: 2,
        bpm: 88,
        bars: 4,
        grid: 4,
        density: 200,
        accent: 56,
        chord: 0,
        max_chord: 1,
        hold: 0,
        hold_min: 0,
        hold_max: 0,
        run: 0,
        perfect_ms: 60,
        good_ms: 140,
        scroll_ms: 1800,
        floor: 0.5,
        ceiling: 1.1,
    },
    InstrumentDef {
        id: Instrument::Lute,
        name: "Lute",
        skill: 50,
        tool: 18,
        lanes: 3,
        bpm: 96,
        bars: 4,
        grid: 2,
        density: 110,
        accent: 110,
        chord: 40,
        max_chord: 2,
        hold: 0,
        hold_min: 0,
        hold_max: 0,
        run: 40,
        perfect_ms: 50,
        good_ms: 120,
        scroll_ms: 1600,
        floor: 0.5,
        ceiling: 1.2,
    },
    InstrumentDef {
        id: Instrument::Flute,
        name: "Flute",
        skill: 100,
        tool: 19,
        lanes: 4,
        bpm: 100,
        bars: 4,
        grid: 2,
        density: 115,
        accent: 105,
        chord: 0,
        max_chord: 1,
        hold: 70,
        hold_min: 2,
        hold_max: 6,
        run: 60,
        perfect_ms: 45,
        good_ms: 110,
        scroll_ms: 1500,
        floor: 0.5,
        ceiling: 1.3,
    },
    InstrumentDef {
        id: Instrument::Harp,
        name: "Harp",
        skill: 150,
        tool: 20,
        lanes: 5,
        bpm: 108,
        bars: 4,
        grid: 2,
        density: 150,
        accent: 100,
        chord: 60,
        max_chord: 3,
        hold: 40,
        hold_min: 2,
        hold_max: 4,
        run: 90,
        perfect_ms: 40,
        good_ms: 95,
        scroll_ms: 1350,
        floor: 0.5,
        ceiling: 1.35,
    },
    InstrumentDef {
        id: Instrument::Bagpipes,
        name: "Bagpipes",
        skill: 225,
        tool: 21,
        lanes: 6,
        bpm: 120,
        bars: 4,
        grid: 1,
        density: 90,
        accent: 120,
        chord: 30,
        max_chord: 2,
        hold: 50,
        hold_min: 4,
        hold_max: 8,
        run: 140,
        perfect_ms: 35,
        good_ms: 85,
        scroll_ms: 1200,
        floor: 0.5,
        ceiling: 1.45,
    },
    InstrumentDef {
        id: Instrument::ThunderDrums,
        name: "Thunder Drums",
        skill: 275,
        tool: 22,
        lanes: 6,
        bpm: 132,
        bars: 4,
        grid: 1,
        density: 120,
        accent: 110,
        chord: 90,
        max_chord: 3,
        hold: 20,
        hold_min: 2,
        hold_max: 4,
        run: 100,
        perfect_ms: 30,
        good_ms: 75,
        scroll_ms: 1100,
        floor: 0.5,
        ceiling: 1.5,
    },
];

/// The instrument of a Utility ability.
pub fn def_of_tool(tool: u32) -> Option<&'static InstrumentDef> {
    INSTRUMENTS.iter().find(|d| d.tool == tool)
}

// ── The profession's songs (`Fusion::Minstrelsy::Songs`) ──

/// What a song does (`Effect`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Effect {
    Travel = 1,
    Rested = 2,
    Campfire = 3,
    Ward = 4,
    Rally = 5,
    Mend = 6,
}

impl Effect {
    /// Played in combat too (from Minstrelsy 225).
    pub fn combat(self) -> bool {
        matches!(self, Effect::Ward | Effect::Rally | Effect::Mend)
    }

    /// Where it is played and what it does, for the window, the tooltips and the guide.
    pub fn text(self) -> &'static str {
        match self {
            Effect::Travel => "Travel song: the party runs faster out of combat.",
            Effect::Rested => "Inn song (an inn or a city): everyone listening gains rested experience, once each half hour.",
            Effect::Campfire => "Fire song (by a campfire): the party regenerates health and mana out of combat, and their other songs last half again as long.",
            Effect::Ward => "Combat song: the party takes a little less damage.",
            Effect::Rally => "Combat song: the party moves a little faster, in combat too.",
            Effect::Mend => "Combat song: the party regenerates a little health, in combat too.",
        }
    }

    /// The magnitude's cap (`CapOf`).
    pub fn cap(self) -> f32 {
        match self {
            Effect::Travel => 0.15,
            Effect::Rested => 0.05,
            Effect::Campfire => 0.6,
            Effect::Ward | Effect::Rally => 0.05,
            Effect::Mend => 0.4,
        }
    }
}

/// A song (`SongDef`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Song {
    pub id: u16,
    pub instrument: Instrument,
    pub name: &'static str,
    pub effect: Effect,
    pub required: u16,
    pub base: f32,
    pub duration_ms: u32,
}

const MIN: u32 = 60_000;
const REST_MS: u32 = 30 * MIN;

const fn song(id: u16, instrument: Instrument, name: &'static str, effect: Effect, required: u16, base: f32, duration_ms: u32) -> Song {
    Song { id, instrument, name, effect, required, base, duration_ms }
}

/// Every song, by instrument and skill (the server's table).
pub static SONGS: [Song; 16] = [
    song(1, Instrument::HandDrum, "Marching Cadence", Effect::Travel, 1, 0.06, 10 * MIN),
    song(2, Instrument::HandDrum, "Hearthside Rhythm", Effect::Rested, 20, 0.01, REST_MS),
    song(3, Instrument::Lute, "Ballad of the Road", Effect::Travel, 50, 0.07, 15 * MIN),
    song(4, Instrument::Lute, "Tavern Ballad", Effect::Rested, 70, 0.015, REST_MS),
    song(5, Instrument::Lute, "Fireside Air", Effect::Campfire, 90, 0.25, 10 * MIN),
    song(6, Instrument::Flute, "Wanderer's Air", Effect::Travel, 100, 0.08, 15 * MIN),
    song(7, Instrument::Flute, "Lullaby of the Hearth", Effect::Rested, 125, 0.02, REST_MS),
    song(8, Instrument::Flute, "Ember Song", Effect::Campfire, 140, 0.3, 15 * MIN),
    song(9, Instrument::Harp, "Hymn of Rest", Effect::Rested, 150, 0.025, REST_MS),
    song(10, Instrument::Harp, "Hymn of the Hearth", Effect::Campfire, 175, 0.35, 15 * MIN),
    song(11, Instrument::Harp, "Starlight Nocturne", Effect::Travel, 200, 0.09, 20 * MIN),
    song(12, Instrument::Harp, "Hymn of Warding", Effect::Ward, 225, 0.03, 20_000),
    song(13, Instrument::Bagpipes, "March of the Clans", Effect::Travel, 225, 0.1, 20 * MIN),
    song(14, Instrument::Bagpipes, "Skirl of the Clans", Effect::Rally, 250, 0.03, 20_000),
    song(15, Instrument::ThunderDrums, "Grand Revel", Effect::Rested, 275, 0.03, REST_MS),
    song(16, Instrument::ThunderDrums, "Heartbeat of the Earth", Effect::Mend, 285, 0.3, 20_000),
];

pub fn song_of(id: u16) -> Option<&'static Song> {
    SONGS.iter().find(|s| s.id == id)
}

/// An instrument's songs, by the skill they ask.
pub fn songs_of(i: Instrument) -> impl Iterator<Item = &'static Song> {
    SONGS.iter().filter(move |s| s.instrument == i)
}

/// The songs a skill can play on an instrument (its own requirement and each song's).
pub fn playable(i: Instrument, skill: u16) -> Vec<&'static Song> {
    songs_of(i)
        .filter(|s| skill >= s.required.max(i.def().skill))
        .collect()
}

/// A song's magnitude at a potency (`Magnitude`).
pub fn magnitude(s: &Song, potency: f32) -> f32 {
    if potency <= 0.0 { 0.0 } else { (s.base * potency).min(s.effect.cap()) }
}

/// A song's colour to a skill (Construction's spacing: orange from the requirement, yellow 25
/// past it, green 50, gray 75).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Colour {
    Red,
    Orange,
    Yellow,
    Green,
    Gray,
}

pub fn colour(s: &Song, skill: u16) -> Colour {
    let r = s.required.max(s.instrument.def().skill);
    match skill {
        k if k < r => Colour::Red,
        k if k < s.required + 25 => Colour::Orange,
        k if k < s.required + 50 => Colour::Yellow,
        k if k < s.required + 75 => Colour::Green,
        _ => Colour::Gray,
    }
}

/// What a song does at a magnitude, as its tooltip reads.
pub fn effect_line(s: &Song, value: f32) -> String {
    let pct = |v: f32| (v * 1000.0).round() / 10.0;
    match s.effect {
        Effect::Travel => format!("{}% run speed out of combat", pct(value)),
        Effect::Rested => format!("{}% of a level in rested experience", pct(value)),
        Effect::Campfire => format!("{}% health and mana a second out of combat", (value * 100.0).round() / 100.0),
        Effect::Ward => format!("{}% less damage taken", pct(value)),
        Effect::Rally => format!("{}% movement speed", pct(value)),
        Effect::Mend => format!("{}% health a second", (value * 100.0).round() / 100.0),
    }
}

/// A sixteenth's place on the song's clock (`TickMs`).
pub fn tick_ms(bpm: u16, tick: u32) -> u32 {
    if bpm == 0 {
        0
    } else {
        (u64::from(tick) * 15000 / u64::from(bpm)) as u32
    }
}

/// SplitMix64, its upper half (the server's `Rng`).
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u32 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^= z >> 31;
        (z >> 32) as u32
    }
    fn below(&mut self, n: u32) -> u32 {
        if n == 0 { 0 } else { self.next() % n }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Note {
    pub ms: u32,
    pub hold_ms: u32,
    pub lane: u8,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Chart {
    pub instrument: Instrument,
    pub seed: u32,
    pub count_in_ms: u32,
    pub beat_ms: u32,
    pub length_ms: u32,
    pub notes: Vec<Note>,
}

/// A song's chart (`MakeChart`).
pub fn make_chart(i: Instrument, seed: u32) -> Chart {
    let d = i.def();
    let mut c = Chart {
        instrument: i,
        seed,
        count_in_ms: tick_ms(d.bpm, 16),
        beat_ms: tick_ms(d.bpm, 4),
        length_ms: tick_ms(d.bpm, 16 * (u32::from(d.bars) + 1)),
        notes: Vec::new(),
    };
    let mut rng = Rng((u64::from(seed) << 8) ^ u64::from(i as u8) ^ 0xA076_1D64_78BD_642F);
    let grid = u32::from(d.grid);
    let steps = u32::from(d.bars) * (16 / grid);
    let lanes = d.lanes as usize;
    // (tick, hold ticks, lane)
    let mut raw: Vec<(u32, u32, u8)> = Vec::new();
    let mut busy = [0u32; 6];
    let mut prev: i32 = -1;
    let mut dir: i32 = 1;
    let free_lanes =
        |busy: &[u32; 6], tick: u32| -> Vec<u8> { (0..lanes as u8).filter(|&l| busy[l as usize] <= tick).collect() };
    for step in 0..steps {
        let tick = step * grid;
        let downbeat = tick % 16 == 0;
        let on_beat = tick % 4 == 0;
        let chance = u32::from(d.density) + if on_beat { u32::from(d.accent) } else { 0 };
        let roll = rng.below(256);
        if !downbeat && roll >= chance {
            continue;
        }
        let free = free_lanes(&busy, tick);
        if free.is_empty() {
            continue;
        }
        let mut lane: i32 = -1;
        if prev >= 0 && d.run > 0 && rng.below(256) < u32::from(d.run) {
            for _ in 0..2 {
                if lane >= 0 {
                    break;
                }
                let cand = prev + dir;
                if cand >= 0 && (cand as usize) < lanes && busy[cand as usize] <= tick {
                    lane = cand;
                } else {
                    dir = -dir;
                }
            }
        }
        if lane < 0 {
            lane = i32::from(free[rng.below(free.len() as u32) as usize]);
        }
        let mut hold_steps = 0;
        if d.hold > 0 && rng.below(256) < u32::from(d.hold) {
            let want = u32::from(d.hold_min) + rng.below(u32::from(d.hold_max - d.hold_min) + 1);
            hold_steps = want.min(steps - step - 1);
        }
        raw.push((tick, hold_steps * grid, lane as u8));
        busy[lane as usize] = tick + (hold_steps + 1) * grid;
        if d.max_chord > 1 && d.chord > 0 && rng.below(256) < u32::from(d.chord) {
            let extra = 1 + rng.below(u32::from(d.max_chord) - 1);
            for _ in 0..extra {
                let more = free_lanes(&busy, tick);
                if more.is_empty() {
                    break;
                }
                let l2 = more[rng.below(more.len() as u32) as usize];
                raw.push((tick, 0, l2));
                busy[l2 as usize] = tick + grid;
            }
        }
        prev = lane;
    }
    raw.sort_by_key(|r| (r.0, r.2));
    c.notes = raw
        .into_iter()
        .map(|(tick, hold, lane)| {
            let ms = tick_ms(d.bpm, 16 + tick);
            Note {
                ms,
                hold_ms: if hold > 0 { tick_ms(d.bpm, 16 + tick + hold) - ms } else { 0 },
                lane,
            }
        })
        .collect();
    c
}

/// FNV-1a over the notes (`ChartHash`).
pub fn chart_hash(c: &Chart) -> u32 {
    let mut h: u32 = 2166136261;
    let mut eat = |b: u8| {
        h ^= u32::from(b);
        h = h.wrapping_mul(16777619);
    };
    for n in &c.notes {
        for b in n.ms.to_le_bytes() {
            eat(b);
        }
        for b in n.hold_ms.to_le_bytes() {
            eat(b);
        }
        eat(n.lane);
    }
    h
}

/// How hard the chart is (`Difficulty`).
pub fn difficulty(c: &Chart) -> f32 {
    let d = c.instrument.def();
    if c.length_ms <= c.count_in_ms {
        return 0.0;
    }
    let holds = c.notes.iter().filter(|n| n.hold_ms > 0).count() as f32;
    let seconds = (c.length_ms - c.count_in_ms) as f32 / 1000.0;
    let rate = (c.notes.len() as f32 + 0.5 * holds) / seconds;
    rate * (1.0 + 0.15 * f32::from(d.lanes - 2)) * (120.0 / f32::from(d.good_ms))
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Judgement {
    Perfect,
    Good,
    Miss,
}

pub fn judge(delta_ms: i32, d: &InstrumentDef) -> Judgement {
    let off = delta_ms.unsigned_abs();
    if off <= u32::from(d.perfect_ms) {
        Judgement::Perfect
    } else if off <= u32::from(d.good_ms) {
        Judgement::Good
    } else {
        Judgement::Miss
    }
}

/// The combo's multiplier before a hit (`Multiplier`).
pub fn multiplier(combo: u32) -> u8 {
    1 + (combo / 8).min(3) as u8
}

pub const POINTS_PERFECT: u32 = 3;
pub const POINTS_GOOD: u32 = 2;
pub const POINTS_HOLD: u32 = 3;
pub const SONG_OVER: u32 = u32::MAX;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Press {
    pub ms: u32,
    pub lane: u8,
    pub down: bool,
}

/// What happened to one note (for the highway's drawing).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum NoteState {
    #[default]
    Pending,
    Struck(bool),
    Missed,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Tally {
    pub notes: u16,
    pub holds: u16,
    pub perfect: u16,
    pub good: u16,
    pub miss: u16,
    pub kept: u16,
    pub dropped: u16,
    pub strays: u16,
    pub combo: u16,
    pub max_combo: u16,
    pub score: u32,
    pub max_score: u32,
    pub judged: u16,
    pub hits: u16,
    pub sum_delta: i64,
    pub sum_delta_sq: i64,
    /// Each note's state (head), and whether its hold is being held now.
    pub states: Vec<NoteState>,
    pub holding: Vec<bool>,
    /// The last judgement made, for the flash (ms on the song's clock, what it was).
    pub last: Option<(u32, Judgement)>,
}

/// Judge the presses (in time order) up to `up_to_ms` (`Score`).
pub fn score(c: &Chart, presses: &[Press], up_to_ms: u32) -> Tally {
    let d = c.instrument.def();
    let n = c.notes.len();
    let mut t = Tally {
        notes: n as u16,
        holds: c.notes.iter().filter(|x| x.hold_ms > 0).count() as u16,
        states: vec![NoteState::Pending; n],
        holding: vec![false; n],
        ..Default::default()
    };
    let mut tail = vec![false; n];
    let mut active = [-1i32; 6];
    let good = u64::from(d.good_ms);
    let mut next = 0usize;
    let lanes = d.lanes as usize;

    fn judged(t: &mut Tally, success: bool, points: u32) {
        t.max_score += POINTS_PERFECT * u32::from(multiplier(u32::from(t.judged)));
        t.judged += 1;
        if success {
            t.score += points * u32::from(multiplier(u32::from(t.combo)));
            t.combo += 1;
            t.max_combo = t.max_combo.max(t.combo);
        } else {
            t.combo = 0;
        }
    }
    let end_of = |j: usize| u64::from(c.notes[j].ms) + u64::from(c.notes[j].hold_ms);

    let advance = |now: u32, t: &mut Tally, next: &mut usize, active: &mut [i32; 6], tail: &mut Vec<bool>| loop {
        while *next < n && t.states[*next] != NoteState::Pending {
            *next += 1;
        }
        let head_at = if *next < n { u64::from(c.notes[*next].ms) + good } else { u64::MAX };
        let mut keep_lane: i32 = -1;
        let mut keep_at = u64::MAX;
        for (l, a) in active.iter().enumerate().take(lanes) {
            if *a >= 0 && end_of(*a as usize) + good < keep_at {
                keep_at = end_of(*a as usize) + good;
                keep_lane = l as i32;
            }
        }
        let at = head_at.min(keep_at);
        if at == u64::MAX || at >= u64::from(now) {
            return;
        }
        if head_at <= keep_at {
            let j = *next;
            t.states[j] = NoteState::Missed;
            t.miss += 1;
            t.last = Some(((head_at) as u32, Judgement::Miss));
            judged(t, false, 0);
            if c.notes[j].hold_ms > 0 {
                tail[j] = true;
                t.dropped += 1;
                judged(t, false, 0);
            }
        } else {
            let j = active[keep_lane as usize] as usize;
            tail[j] = true;
            t.holding[j] = false;
            t.kept += 1;
            judged(t, true, POINTS_HOLD);
            active[keep_lane as usize] = -1;
        }
    };

    for p in presses {
        if usize::from(p.lane) >= lanes {
            continue;
        }
        if p.ms >= up_to_ms {
            break;
        }
        advance(p.ms, &mut t, &mut next, &mut active, &mut tail);
        let l = usize::from(p.lane);
        if p.down {
            if active[l] >= 0 {
                continue;
            }
            let mut found: Option<usize> = None;
            let mut j = next;
            while j < n && u64::from(c.notes[j].ms) <= u64::from(p.ms) + good {
                let note = &c.notes[j];
                let delta = i64::from(p.ms) - i64::from(note.ms);
                if t.states[j] == NoteState::Pending && note.lane == p.lane && delta.unsigned_abs() <= good {
                    found = Some(j);
                    break;
                }
                j += 1;
            }
            let Some(j) = found else {
                t.strays += 1;
                t.combo = 0;
                continue;
            };
            let note = c.notes[j];
            let delta = (i64::from(p.ms) - i64::from(note.ms)) as i32;
            t.hits += 1;
            t.sum_delta += i64::from(delta);
            t.sum_delta_sq += i64::from(delta) * i64::from(delta);
            let jd = judge(delta, d);
            t.last = Some((p.ms, jd));
            if jd == Judgement::Perfect {
                t.perfect += 1;
                t.states[j] = NoteState::Struck(true);
                judged(&mut t, true, POINTS_PERFECT);
            } else {
                t.good += 1;
                t.states[j] = NoteState::Struck(false);
                judged(&mut t, true, POINTS_GOOD);
            }
            if note.hold_ms > 0 {
                active[l] = j as i32;
                t.holding[j] = true;
            }
        } else {
            let j = active[l];
            if j < 0 {
                continue;
            }
            let j = j as usize;
            active[l] = -1;
            tail[j] = true;
            t.holding[j] = false;
            if u64::from(p.ms) + good >= end_of(j) {
                t.kept += 1;
                judged(&mut t, true, POINTS_HOLD);
            } else {
                t.dropped += 1;
                judged(&mut t, false, 0);
            }
        }
    }
    advance(up_to_ms, &mut t, &mut next, &mut active, &mut tail);
    if up_to_ms == SONG_OVER {
        for a in active.iter_mut().take(lanes) {
            if *a >= 0 {
                tail[*a as usize] = true;
                t.holding[*a as usize] = false;
                t.kept += 1;
                judged(&mut t, true, POINTS_HOLD);
                *a = -1;
            }
        }
    }
    t
}

pub fn quality(t: &Tally) -> f32 {
    let units = f32::from(t.judged) + 0.5 * f32::from(t.strays);
    if units <= 0.0 {
        return 0.0;
    }
    let acc = (f32::from(t.perfect) + f32::from(t.good) * (2.0 / 3.0) + f32::from(t.kept)) / units;
    let sc = if t.max_score > 0 { t.score as f32 / t.max_score as f32 } else { 0.0 };
    (0.6 * acc + 0.4 * sc).clamp(0.0, 1.0)
}

pub const FAIL_QUALITY: f32 = 0.2;

/// The potency the meter shows while the song plays (`LivePotency`).
pub fn live_potency(d: &InstrumentDef, t: &Tally) -> f32 {
    if t.judged == 0 {
        d.floor
    } else {
        d.floor + (d.ceiling - d.floor) * quality(t)
    }
}

/// The potency the song would land at (`Potency`, without the server's Robotic cap).
pub fn potency(d: &InstrumentDef, t: &Tally) -> f32 {
    let q = quality(t);
    if q < FAIL_QUALITY { 0.0 } else { d.floor + (d.ceiling - d.floor) * q }
}

/// A difficulty for the tooltips: 1 to 6 by tier.
pub fn stars(d: &InstrumentDef) -> usize {
    d.id as usize
}

#[cfg(test)]
mod tests {
    use super::*;

    fn play(c: &Chart, offset: impl Fn(usize) -> i32) -> Vec<Press> {
        let mut out = Vec::new();
        for (i, n) in c.notes.iter().enumerate() {
            let at = (i64::from(n.ms) + i64::from(offset(i))).max(0);
            out.push(Press { ms: at as u32, lane: n.lane, down: true });
            let up = if n.hold_ms > 0 { i64::from(n.ms + n.hold_ms) } else { at + 60 };
            out.push(Press { ms: up.max(at + 1) as u32, lane: n.lane, down: false });
        }
        out.sort_by_key(|p| p.ms);
        out
    }

    #[test]
    fn the_charts_are_the_servers() {
        // fusion/tests/MinstrelsyTests.cpp holds the same hashes: both sides make the same charts.
        let want = [
            (Instrument::HandDrum, 4073638693u32, 16usize),
            (Instrument::Lute, 1187863032, 26),
            (Instrument::Flute, 2148115026, 22),
            (Instrument::Harp, 1699884994, 37),
            (Instrument::Bagpipes, 2300773996, 44),
            (Instrument::ThunderDrums, 3222150876, 44),
        ];
        for (i, hash, n) in want {
            let c = make_chart(i, 12345);
            assert_eq!(chart_hash(&c), hash, "{i:?}");
            assert_eq!(c.notes.len(), n, "{i:?}");
            assert_eq!(make_chart(i, 12345), c, "the same seed, the same chart");
        }
        assert_eq!(chart_hash(&make_chart(Instrument::ThunderDrums, 0xDEADBEEF)), 3175862811);
        assert_ne!(make_chart(Instrument::Harp, 1), make_chart(Instrument::Harp, 2));
    }

    #[test]
    fn harder_instruments_have_harder_charts() {
        let mut last = 0.0;
        for i in Instrument::ALL {
            let avg: f32 = (1..=30).map(|s| difficulty(&make_chart(i, s))).sum::<f32>() / 30.0;
            assert!(avg > last * 1.1, "{i:?}: {avg} after {last}");
            last = avg;
            assert_eq!(def_of_tool(i.def().tool).map(|d| d.id), Some(i));
            assert_eq!(Instrument::from_wire(i as u8), Some(i));
        }
        for w in INSTRUMENTS.windows(2) {
            assert!(w[1].lanes >= w[0].lanes && w[1].good_ms < w[0].good_ms && w[1].ceiling > w[0].ceiling);
        }
    }

    #[test]
    fn the_songs_are_the_servers() {
        // Minstrelsy.cpp Songs: ids 1-16, each instrument two or more, combat songs from 225.
        for (k, s) in SONGS.iter().enumerate() {
            assert_eq!(usize::from(s.id), k + 1);
            assert!(s.required >= s.instrument.def().skill);
            assert_eq!(s.effect.combat(), s.required >= 225 && matches!(s.effect, Effect::Ward | Effect::Rally | Effect::Mend));
            assert!(magnitude(s, 1.5) <= s.effect.cap() + 1e-6);
        }
        for i in Instrument::ALL {
            assert!(songs_of(i).count() >= 2, "{i:?}");
        }
        let skills: Vec<u16> = INSTRUMENTS.iter().map(|d| d.skill).collect();
        assert_eq!(skills, [1, 50, 100, 150, 225, 275]);
        let tools: Vec<u32> = INSTRUMENTS.iter().map(|d| d.tool).collect();
        assert_eq!(tools, [17, 18, 19, 20, 21, 22]);
        // The playable songs grow with the skill; a song turns gray 75 past its requirement.
        assert_eq!(playable(Instrument::Harp, 149).len(), 0);
        assert_eq!(playable(Instrument::Harp, 180).len(), 2);
        assert_eq!(playable(Instrument::Harp, 300).len(), 4);
        let road = song_of(3).unwrap();
        assert_eq!(colour(road, 49), Colour::Red);
        assert_eq!(colour(road, 60), Colour::Orange);
        assert_eq!(colour(road, 80), Colour::Yellow);
        assert_eq!(colour(road, 110), Colour::Green);
        assert_eq!(colour(road, 125), Colour::Gray);
        assert!((magnitude(song_of(13).unwrap(), 1.45) - 0.145).abs() < 1e-5);
        assert_eq!(effect_line(song_of(13).unwrap(), 0.145), "14.5% run speed out of combat");
    }

    #[test]
    fn the_windows_and_the_combo() {
        let d = Instrument::Lute.def();
        assert_eq!(judge(50, d), Judgement::Perfect);
        assert_eq!(judge(-51, d), Judgement::Good);
        assert_eq!(judge(120, d), Judgement::Good);
        assert_eq!(judge(121, d), Judgement::Miss);
        assert_eq!((multiplier(7), multiplier(8), multiplier(24), multiplier(99)), (1, 2, 4, 4));
    }

    #[test]
    fn the_potency_runs_from_flat_to_the_ceiling() {
        let c = make_chart(Instrument::Harp, 4242);
        let d = c.instrument.def();
        let perfect = score(&c, &play(&c, |k| (k as i32 * 7) % 21 - 10), SONG_OVER);
        assert_eq!(usize::from(perfect.perfect), c.notes.len());
        assert_eq!(perfect.kept, perfect.holds);
        assert!((quality(&perfect) - 1.0).abs() < 1e-6);
        assert!((potency(d, &perfect) - d.ceiling).abs() < 1e-5);
        let good = score(&c, &play(&c, |_| 60), SONG_OVER);
        assert_eq!(usize::from(good.good), c.notes.len());
        let none = score(&c, &[], SONG_OVER);
        assert_eq!(usize::from(none.miss), c.notes.len());
        assert_eq!(potency(d, &none), 0.0);
        assert!(potency(d, &perfect) > potency(d, &good) && potency(d, &good) > 1.0);
        assert!(none.states.iter().all(|s| *s == NoteState::Missed));
        // While it plays: the floor until anything is judged.
        assert_eq!(live_potency(d, &score(&c, &[], 0)), d.floor);
    }
}
