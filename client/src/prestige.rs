//! Prestige, the professions' field gear and MW2's modes (the server's `fusion/Prestige.h`,
//! `FieldGear.h` and `Match.h`), all on fusion channel 8:
//!
//! - the achievements window's **Prestige** tab: each track's rank and emblem, whether it can
//!   prestige now, the two-step confirm (the server names what is lost, a second click confirms),
//!   the titles and tracer colours to wear, the token shop, the milestones;
//! - field gear: a spare grenade used from the bags tops up the kit's count
//!   ([`crate::equipment::EquipState`]); an Adrenaline stim sprints without tiring
//!   ([`FieldEffects`]); the tracer colour worn ([`TracerLook`], crate::tracer);
//! - a match (a battleground, or Team Deathmatch in the Gurubashi Arena): the server's match
//!   messages are read and kept ([`Match`]), but no score HUD or scoreboard is drawn (the game
//!   keeps no points; Tab is WoW's).

use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::achievements::Achievements;
use crate::wow_ui::{self, DIM, FONT, GOLD, GREEN, GREY, Kind, RED, WHITE, WowButton, text, u};

/// The prestige channel (the server's `Prestige::kChannelPrestige`).
pub const CHANNEL_PRESTIGE: u8 = 8;
const VERSION: u8 = 1;
// Client to server.
const OP_REQUEST: u8 = 1;
const OP_ASK: u8 = 2;
const OP_CONFIRM: u8 = 3;
const OP_SET_TITLE: u8 = 4;
const OP_SET_TRACER: u8 = 5;
const OP_CANCEL: u8 = 6;
const OP_BUY: u8 = 7;
// Server to client.
const OP_STATE: u8 = 1;
const OP_PROMPT: u8 = 2;
const OP_RESULT: u8 = 3;
const OP_EQUIPMENT: u8 = 4;
const OP_STIM: u8 = 5;
const OP_LOOK: u8 = 6;
const OP_SHOP: u8 = 7;
const OP_HUD: u8 = 20;
const OP_SCOREBOARD: u8 = 21;
const OP_MATCH_END: u8 = 22;

/// The achievements window's tab for prestige (after the server's nine categories).
pub const PRESTIGE_TAB: u8 = 10;

/// The tracks (the server's `Prestige::Track`).
pub const TRACK_LEVEL: u8 = 1;
pub const TRACK_GUNS: u8 = 2;
pub const TRACK_PROFESSION: u8 = 3;

/// Tailoring's ghillie mantles (the server's `FieldGear`, items 93240-93243): enemy radar misses
/// whoever wears one.
pub fn is_ghillie(entry: u32) -> bool {
    (93240..=93243).contains(&entry)
}

/// The cloak's equipment slot (vmangos `EQUIPMENT_SLOT_BACK`).
pub const SLOT_BACK: u8 = 14;

/// Why a track cannot prestige now (the server's `Prestige::Refusal`), 0 when it can.
pub fn refusal_text(r: u8) -> &'static str {
    match r {
        0 => "Ready",
        1 => "Not maxed yet",
        2 => "Highest prestige",
        3 => "Not a profession",
        4 => "Not in combat, a battleground or a match",
        _ => "Not now",
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProfessionRow {
    pub skill: u16,
    pub value: u16,
    pub rank: u8,
    pub refusal: u8,
    pub name: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MilestoneRow {
    pub id: u8,
    pub min_rank: u8,
    pub claimed: bool,
    pub name: String,
    pub text: String,
}

/// What the Prestige tab shows (the server's `Prestige::EncodeState`).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PrestigeState {
    pub level_rank: u8,
    pub gun_rank: u8,
    pub tokens: u32,
    pub milestones: u32,
    pub cosmetics: u32,
    pub title: u16,
    pub tracer: u8,
    pub level: u8,
    pub guns_maxed: bool,
    pub busy: bool,
    pub level_refusal: u8,
    pub guns_refusal: u8,
    pub professions: Vec<ProfessionRow>,
    pub titles: Vec<(u16, String)>,
    pub milestone_rows: Vec<MilestoneRow>,
}

impl PrestigeState {
    pub fn owns_tracer(&self, t: u8) -> bool {
        t == 0 || (t < 32 && (self.cosmetics >> t) & 1 != 0)
    }
}

/// One good of the token shop (the server's `Prestige::Shop`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ShopGood {
    pub id: u8,
    pub price: u32,
    pub owned: bool,
    pub name: String,
}

/// The server's question before a prestige.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Prompt {
    pub nonce: u32,
    pub track: u8,
    pub key: u16,
    pub text: String,
}

/// The match HUD (the server's `Match::Hud`).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Hud {
    pub mode: u8,
    pub team: u8,
    pub phase: u8,
    pub limit: u32,
    pub ms_left: u32,
    pub team_score: [u32; 2],
    pub score: u32,
    pub kills: u16,
    pub deaths: u16,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ScoreRow {
    pub team: u8,
    pub score: u32,
    pub kills: u16,
    pub deaths: u16,
    pub objectives: u16,
    pub name: String,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Message {
    State(PrestigeState),
    Prompt(Prompt),
    Result { ok: bool, text: String },
    Equipment { slot: u8, count: u8 },
    Stim { kind: u8, ms: u32 },
    Look { tracer: u8, rgb: [u8; 3] },
    Hud(Hud),
    Scoreboard { mode: u8, rows: Vec<ScoreRow> },
    MatchEnd { winner: u8, text: String },
    Shop(Vec<ShopGood>),
}

struct Reader<'a> {
    b: &'a [u8],
    at: usize,
}

impl Reader<'_> {
    fn take(&mut self, n: usize) -> Option<&[u8]> {
        let s = self.b.get(self.at..self.at + n)?;
        self.at += n;
        Some(s)
    }
    fn u8(&mut self) -> Option<u8> {
        Some(self.take(1)?[0])
    }
    fn u16(&mut self) -> Option<u16> {
        Some(u16::from_le_bytes(self.take(2)?.try_into().ok()?))
    }
    fn u32(&mut self) -> Option<u32> {
        Some(u32::from_le_bytes(self.take(4)?.try_into().ok()?))
    }
    fn str8(&mut self) -> Option<String> {
        let n = usize::from(self.u8()?);
        Some(String::from_utf8_lossy(self.take(n)?).into_owned())
    }
    fn str16(&mut self) -> Option<String> {
        let n = usize::from(self.u16()?);
        Some(String::from_utf8_lossy(self.take(n)?).into_owned())
    }
}

fn read_state(r: &mut Reader) -> Option<PrestigeState> {
    let mut s = PrestigeState {
        level_rank: r.u8()?,
        gun_rank: r.u8()?,
        tokens: r.u32()?,
        milestones: r.u32()?,
        cosmetics: r.u32()?,
        title: r.u16()?,
        tracer: r.u8()?,
        level: r.u8()?,
        guns_maxed: r.u8()? != 0,
        busy: r.u8()? != 0,
        level_refusal: r.u8()?,
        guns_refusal: r.u8()?,
        ..default()
    };
    for _ in 0..r.u8()? {
        s.professions.push(ProfessionRow {
            skill: r.u16()?,
            value: r.u16()?,
            rank: r.u8()?,
            refusal: r.u8()?,
            name: r.str8()?,
        });
    }
    for _ in 0..r.u8()? {
        let id = r.u16()?;
        s.titles.push((id, r.str8()?));
    }
    for _ in 0..r.u8()? {
        s.milestone_rows.push(MilestoneRow {
            id: r.u8()?,
            min_rank: r.u8()?,
            claimed: r.u8()? != 0,
            name: r.str8()?,
            text: r.str16()?,
        });
    }
    Some(s)
}

/// Read one prestige-channel body.
pub fn decode(body: &[u8]) -> Option<Message> {
    let mut r = Reader { b: body, at: 0 };
    if r.u8()? != VERSION {
        return None;
    }
    Some(match r.u8()? {
        OP_STATE => Message::State(read_state(&mut r)?),
        OP_PROMPT => Message::Prompt(Prompt {
            nonce: r.u32()?,
            track: r.u8()?,
            key: r.u16()?,
            text: r.str16()?,
        }),
        OP_RESULT => Message::Result {
            ok: r.u8()? != 0,
            text: r.str16()?,
        },
        OP_EQUIPMENT => Message::Equipment {
            slot: r.u8()?,
            count: r.u8()?,
        },
        OP_STIM => Message::Stim {
            kind: r.u8()?,
            ms: r.u32()?,
        },
        OP_LOOK => Message::Look {
            tracer: r.u8()?,
            rgb: [r.u8()?, r.u8()?, r.u8()?],
        },
        OP_HUD => Message::Hud(Hud {
            mode: r.u8()?,
            team: r.u8()?,
            phase: r.u8()?,
            limit: r.u32()?,
            ms_left: r.u32()?,
            team_score: [r.u32()?, r.u32()?],
            score: r.u32()?,
            kills: r.u16()?,
            deaths: r.u16()?,
        }),
        OP_SCOREBOARD => {
            let mode = r.u8()?;
            let rows = (0..r.u8()?)
                .map(|_| {
                    Some(ScoreRow {
                        team: r.u8()?,
                        score: r.u32()?,
                        kills: r.u16()?,
                        deaths: r.u16()?,
                        objectives: r.u16()?,
                        name: r.str8()?,
                    })
                })
                .collect::<Option<Vec<_>>>()?;
            Message::Scoreboard { mode, rows }
        }
        OP_MATCH_END => Message::MatchEnd {
            winner: r.u8()?,
            text: r.str16()?,
        },
        OP_SHOP => Message::Shop(
            (0..r.u8()?)
                .map(|_| {
                    Some(ShopGood {
                        id: r.u8()?,
                        price: r.u32()?,
                        owned: r.u8()? != 0,
                        name: r.str8()?,
                    })
                })
                .collect::<Option<Vec<_>>>()?,
        ),
        _ => return None,
    })
}

/// What a click on the Prestige tab asks of the server.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    Ask {
        track: u8,
        key: u16,
    },
    Confirm(u32),
    Cancel,
    Title(u16),
    Tracer(u8),
    /// Buy a good from the token shop.
    Buy(u8),
}

/// The body a click sends.
pub fn encode(a: Action) -> Vec<u8> {
    let mut b = vec![VERSION];
    match a {
        Action::Ask { track, key } => {
            b.extend([OP_ASK, track]);
            b.extend(key.to_le_bytes());
        }
        Action::Confirm(nonce) => {
            b.push(OP_CONFIRM);
            b.extend(nonce.to_le_bytes());
        }
        Action::Cancel => b.push(OP_CANCEL),
        Action::Title(t) => {
            b.push(OP_SET_TITLE);
            b.extend(t.to_le_bytes());
        }
        Action::Tracer(t) => b.extend([OP_SET_TRACER, t]),
        Action::Buy(g) => b.extend([OP_BUY, g]),
    }
    b
}

/// The Prestige tab's state.
#[derive(Resource, Debug, Default)]
pub struct Prestige {
    pub state: Option<PrestigeState>,
    pub prompt: Option<Prompt>,
    pub result: Option<(bool, String)>,
    /// The token shop, as the server last listed it.
    pub shop: Vec<ShopGood>,
    dirty: bool,
}

/// The field gear in effect on this client: when each stim wears off (elapsed seconds).
#[derive(Resource, Debug, Default)]
pub struct FieldEffects {
    pub adrenaline_until: f32,
    pub focus_until: f32,
    pub second_wind_until: f32,
}

impl FieldEffects {
    /// An Adrenaline stim: the sprint does not tire (as Marathon).
    pub fn tireless(&self, t: f32) -> bool {
        t < self.adrenaline_until
    }
}

/// The tracer colour worn (None: MW2's own grey).
#[derive(Resource, Debug, Default, PartialEq)]
pub struct TracerLook(pub Option<[u8; 3]>);

/// A match on: its HUD, the scoreboard as last sent, an end message showing.
#[derive(Resource, Debug, Default)]
pub struct Match {
    pub hud: Option<Hud>,
    pub rows: Vec<ScoreRow>,
    pub end: Option<(String, f32)>,
    /// The elapsed time the HUD came, to count its clock down between messages.
    pub hud_at: f32,
}

impl Match {
    pub fn active(&self) -> bool {
        self.hud.as_ref().is_some_and(|h| h.mode != 0)
    }
}

pub struct PrestigePlugin;

impl Plugin for PrestigePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Prestige>()
            .init_resource::<FieldEffects>()
            .init_resource::<TracerLook>()
            .init_resource::<Match>()
            .add_systems(
                Update,
                (read, ask_on_open, draw_tab, click, prompt_popup)
                    .chain()
                    .after(crate::achievements::refresh)
                    .in_set(Mw2Set::Hud),
            );
    }
}

fn read(
    time: Res<Time>,
    mut channel: MessageReader<FusionChannelReceived>,
    mut prestige: ResMut<Prestige>,
    mut effects: ResMut<FieldEffects>,
    mut look: ResMut<TracerLook>,
    mut game: ResMut<Match>,
    mut equip: ResMut<crate::equipment::EquipState>,
) {
    let t = time.elapsed_secs();
    for FusionChannelReceived(m) in channel.read() {
        if m.channel != CHANNEL_PRESTIGE {
            continue;
        }
        let Some(msg) = decode(&m.body) else {
            warn!("benilla-mw2: a prestige message it can't read");
            continue;
        };
        match msg {
            Message::State(s) => {
                prestige.state = Some(s);
                prestige.dirty = true;
            }
            Message::Prompt(p) => {
                prestige.prompt = Some(p);
                prestige.dirty = true;
            }
            Message::Result { ok, text } => {
                if ok {
                    prestige.prompt = None;
                }
                prestige.result = Some((ok, text));
                prestige.dirty = true;
            }
            Message::Equipment { slot, count } => {
                if slot == 0 {
                    equip.lethal_left = u32::from(count);
                } else {
                    equip.tactical_left = u32::from(count);
                }
            }
            Message::Stim { kind, ms } => {
                let until = t + ms as f32 / 1000.0;
                match kind {
                    1 => effects.focus_until = until,
                    2 => effects.second_wind_until = until,
                    3 => effects.adrenaline_until = until,
                    _ => {}
                }
            }
            Message::Look { tracer, rgb } => {
                let want = TracerLook((tracer != 0).then_some(rgb));
                if *look != want {
                    *look = want;
                }
            }
            Message::Hud(h) => {
                game.hud = (h.mode != 0).then_some(h);
                game.hud_at = t;
            }
            Message::Scoreboard { rows, .. } => game.rows = rows,
            Message::MatchEnd { text, .. } => game.end = Some((text, t)),
            Message::Shop(goods) => {
                prestige.shop = goods;
                prestige.dirty = true;
            }
        }
    }
}

/// The Prestige tab opened: ask for the state as it stands.
fn ask_on_open(
    ach: Res<Achievements>,
    mut send: MessageWriter<SendFusionChannel>,
    mut was: Local<bool>,
    mut prestige: ResMut<Prestige>,
) {
    let on = ach.open && ach.tab == PRESTIGE_TAB;
    if on && !*was {
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_PRESTIGE,
            body: vec![VERSION, OP_REQUEST],
        }));
        prestige.dirty = true;
    }
    *was = on;
}

/// A button on the Prestige tab.
#[derive(Component)]
struct PrestigeButton(Action);

/// A panel button for the tab; greyed when the server would refuse it (or it is the one worn).
fn button(font: &Handle<Font>, label: &str, action: Action, enabled: bool) -> impl Bundle + use<> {
    wow_ui::button_fit(
        font,
        label,
        WowButton {
            disabled: !enabled,
            ..WowButton::new(Kind::Panel)
        },
        (PrestigeButton(action), action_tip(label, action)),
    )
}

/// What a Prestige button does, on hover (the confirm's own buttons say it already).
fn action_tip(label: &str, action: Action) -> wow_ui::WowTip {
    let body = match action {
        Action::Ask { .. } => {
            "Starts this track over for its next prestige rank and a prestige token. You are \
             told what you lose and asked to confirm first."
        }
        Action::Title(0) => "Wear no title.",
        Action::Title(_) => "Wear this title over your name.",
        Action::Tracer(_) => "Your tracers fly in this colour.",
        Action::Buy(_) => "Buy it with prestige tokens.",
        Action::Confirm(_) | Action::Cancel => return wow_ui::WowTip::default(),
    };
    wow_ui::WowTip::new(label, body)
}

/// An emblem: the rank in roman numerals on the track's colour (empty for none).
fn roman(n: u8) -> String {
    const R: [&str; 11] = [
        "-", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
    ];
    R.get(usize::from(n)).copied().unwrap_or("X").to_string()
}

fn emblem(font: &Handle<Font>, rank: u8, color: Color) -> impl Bundle + use<> {
    (
        Node {
            width: u(30.0),
            height: u(20.0),
            margin: UiRect::right(u(6.0)),
            justify_content: JustifyContent::Center,
            align_items: AlignItems::Center,
            border: UiRect::all(px(2)),
            ..default()
        },
        BorderColor::all(if rank > 0 { color } else { DIM.with_alpha(0.4) }),
        BackgroundColor(if rank > 0 {
            color.with_alpha(0.25)
        } else {
            Color::NONE
        }),
        children![text(font, &roman(rank), 11.0, WHITE)],
    )
}

const TRACERS: [(u8, &str); 7] = [
    (0, "Standard"),
    (1, "Red"),
    (2, "Green"),
    (3, "Blue"),
    (4, "Violet"),
    (5, "Gold"),
    (6, "White Phosphor"),
];

/// The popup asking before a prestige.
#[derive(Component)]
struct PromptPopup;

/// The server's prompt as a popup (`StaticPopup`'s look): what is lost, Yes or No. Closing the
/// achievements window (or Esc, which closes it) drops the question.
fn prompt_popup(
    mut commands: Commands,
    assets: Res<AssetServer>,
    ach: Res<Achievements>,
    mut prestige: ResMut<Prestige>,
    roots: Query<(Entity, &PromptPopup)>,
    mut asked: Local<Option<u32>>,
) {
    if !ach.open && prestige.prompt.is_some() {
        prestige.prompt = None;
    }
    let want = prestige.prompt.as_ref().map(|p| p.nonce);
    if want == *asked && roots.is_empty() == want.is_none() {
        return;
    }
    *asked = want;
    for (e, _) in &roots {
        commands.entity(e).despawn();
    }
    let Some(p) = prestige.prompt.as_ref() else {
        return;
    };
    let font: Handle<Font> = assets.load(FONT);
    wow_ui::spawn_popup(
        &mut commands,
        &font,
        "prestige confirm",
        &prompt_question(&p.text),
        PromptPopup,
        ("Prestige", PrestigeButton(Action::Confirm(p.nonce))),
        ("Cancel", PrestigeButton(Action::Cancel)),
    );
}

/// What the prestige popup asks: the server's account of what is lost, and that it is for good.
fn prompt_question(text: &str) -> String {
    format!("{text}

This cannot be undone. Prestige now?")
}

/// Draw the Prestige tab into the achievements window's list.
#[allow(clippy::too_many_arguments)]
fn draw_tab(
    mut commands: Commands,
    assets: Res<AssetServer>,
    ach: Res<Achievements>,
    mut prestige: ResMut<Prestige>,
    lists: Query<Entity, With<crate::achievements::AchList>>,
    mut shown: Local<bool>,
) {
    let on = ach.open && ach.tab == PRESTIGE_TAB;
    let Ok(list) = lists.single() else {
        *shown = false;
        return;
    };
    if !on {
        *shown = false;
        return;
    }
    if *shown && !prestige.dirty && !ach.is_changed() {
        return;
    }
    *shown = true;
    prestige.dirty = false;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    commands.entity(list).despawn_related::<Children>();
    let Some(s) = prestige.state.clone() else {
        commands
            .entity(list)
            .with_children(|l| _ = l.spawn(text(f, "Loading...", 12.0, GREY)));
        return;
    };
    let result = prestige.result.clone();
    let shop = prestige.shop.clone();
    let row = || Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        flex_wrap: FlexWrap::Wrap,
        margin: UiRect::vertical(u(3.0)),
        ..default()
    };
    commands.entity(list).with_children(|l| {
        let title = s
            .titles
            .iter()
            .find(|(id, _)| *id == s.title)
            .map_or("None", |(_, t)| t.as_str());
        l.spawn(text(
            f,
            &format!("Prestige tokens: {}    Title worn: {title}", s.tokens),
            14.0,
            GOLD,
        ));
        // The three tracks.
        wow_ui::section_header(l, f, "Tracks");
        l.spawn(row()).with_children(|r| {
            r.spawn(emblem(f, s.level_rank, GOLD));
            r.spawn(text(
                f,
                &format!("Level  {}/10   (level {})  ", s.level_rank, s.level),
                12.0,
                WHITE,
            ));
            r.spawn(button(
                f,
                "Prestige level",
                Action::Ask {
                    track: TRACK_LEVEL,
                    key: 0,
                },
                s.level_refusal == 0,
            ));
            if s.level_refusal != 0 {
                r.spawn(text(f, refusal_text(s.level_refusal), 12.0, DIM));
            }
        });
        l.spawn(row()).with_children(|r| {
            r.spawn(emblem(f, s.gun_rank, Color::srgb(0.9, 0.3, 0.25)));
            let maxed = if s.guns_maxed {
                "all unlocked"
            } else {
                "not all unlocked"
            };
            r.spawn(text(
                f,
                &format!("Guns  {}/10   ({maxed}; account-wide)  ", s.gun_rank),
                12.0,
                WHITE,
            ));
            r.spawn(button(
                f,
                "Prestige guns",
                Action::Ask {
                    track: TRACK_GUNS,
                    key: 0,
                },
                s.guns_refusal == 0,
            ));
            if s.guns_refusal != 0 {
                r.spawn(text(f, refusal_text(s.guns_refusal), 12.0, DIM));
            }
        });
        for p in &s.professions {
            l.spawn(row()).with_children(|r| {
                r.spawn(emblem(f, p.rank, Color::srgb(0.3, 0.8, 0.45)));
                r.spawn(text(
                    f,
                    &format!("{}  {}/10   (skill {}/300)  ", p.name, p.rank, p.value),
                    12.0,
                    WHITE,
                ));
                r.spawn(button(
                    f,
                    &format!("Prestige {}", p.name),
                    Action::Ask {
                        track: TRACK_PROFESSION,
                        key: p.skill,
                    },
                    p.refusal == 0,
                ));
            });
        }
        if let Some((ok, msg)) = &result {
            l.spawn(text(f, msg, 12.0, if *ok { GREEN } else { RED }));
        }
        // Titles and tracers to wear.
        wow_ui::section_header(l, f, "Titles");
        l.spawn(row()).with_children(|r| {
            r.spawn(button(f, "None", Action::Title(0), s.title != 0));
            for (id, name) in &s.titles {
                r.spawn(button(f, name, Action::Title(*id), *id != s.title));
            }
        });
        wow_ui::section_header(l, f, "Tracer Colour");
        l.spawn(row()).with_children(|r| {
            for (id, name) in TRACERS {
                if s.owns_tracer(id) {
                    r.spawn(button(f, name, Action::Tracer(id), id != s.tracer));
                }
            }
        });
        // The token shop: what tokens buy, here rather than from an NPC.
        if !shop.is_empty() {
            wow_ui::section_header(l, f, "Token Shop");
            for g in &shop {
                l.spawn(row()).with_children(|r| {
                    let can = !g.owned && s.tokens >= g.price;
                    r.spawn(button(f, "Buy", Action::Buy(g.id), can));
                    let label = if g.owned {
                        format!("  {} (owned)", g.name)
                    } else {
                        format!(
                            "  {}: {} token{}",
                            g.name,
                            g.price,
                            if g.price == 1 { "" } else { "s" }
                        )
                    };
                    r.spawn(text(f, &label, 12.0, if g.owned { DIM } else { WHITE }));
                });
            }
        }
        wow_ui::section_header(l, f, "Milestones");
        if s.milestone_rows.is_empty() {
            l.spawn(text(f, "No milestones yet.", 12.0, GREY));
        }
        for m in &s.milestone_rows {
            let (mark, color) = if m.claimed {
                ("  (Claimed)", GREEN)
            } else {
                ("", WHITE)
            };
            l.spawn(text(f, &format!("{}{mark}", m.name), 12.0, color));
            l.spawn(text(f, &m.text, 11.0, DIM));
        }
    });
}

fn click(
    buttons: Query<(&PrestigeButton, &Interaction), Changed<Interaction>>,
    mut send: MessageWriter<SendFusionChannel>,
    mut prestige: ResMut<Prestige>,
) {
    for (b, interaction) in &buttons {
        if *interaction == Interaction::Pressed {
            // Either answer closes the popup (a second Yes would only be refused).
            if matches!(b.0, Action::Cancel | Action::Confirm(_)) {
                prestige.prompt = None;
                prestige.dirty = true;
            }
            prestige.result = None;
            send.write(SendFusionChannel(ChannelMessage {
                channel: CHANNEL_PRESTIGE,
                body: encode(b.0),
            }));
        }
    }
}

/// The modes' names (the server's `Match::Mode`).
pub fn mode_name(mode: u8) -> &'static str {
    match mode {
        1 => "CAPTURE THE FLAG",
        2 => "DOMINATION",
        3 => "GROUND WAR",
        4 => "TEAM DEATHMATCH",
        _ => "",
    }
}

/// Minutes and seconds.
pub fn clock(ms: u32) -> String {
    let s = ms / 1000;
    format!("{}:{:02}", s / 60, s % 60)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mode::FpsState;

    #[test]
    fn the_prestige_tab_builds() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        world.init_resource::<Time>();
        world.init_resource::<Messages<SendFusionChannel>>();
        world.insert_resource(FpsState {
            active: true,
            ..default()
        });
        // The Prestige tab draws into the achievements list.
        world.spawn(crate::achievements::AchList);
        let mut ach = Achievements::default();
        ach.open = true;
        ach.tab = PRESTIGE_TAB;
        world.insert_resource(ach);
        world.insert_resource(Prestige {
            state: Some(PrestigeState {
                tokens: 5,
                ..default()
            }),
            shop: vec![
                ShopGood {
                    id: 20,
                    price: 4,
                    owned: false,
                    name: "Red tracers".into(),
                },
                ShopGood {
                    id: 30,
                    price: 10,
                    owned: false,
                    name: "Arctic camo".into(),
                },
            ],
            ..default()
        });
        world.run_system_once(draw_tab).unwrap();
        let mut buttons = world.query::<&PrestigeButton>();
        assert!(buttons.iter(&world).count() >= 3);
        // The shop: a Buy for each good, the one too dear greyed.
        let mut buys = world.query::<(&PrestigeButton, &WowButton)>();
        let buys: Vec<(Action, bool)> = buys
            .iter(&world)
            .filter(|(b, _)| matches!(b.0, Action::Buy(_)))
            .map(|(b, w)| (b.0, w.disabled))
            .collect();
        assert_eq!(buys.len(), 2);
        assert!(buys.contains(&(Action::Buy(20), false)));
        assert!(buys.contains(&(Action::Buy(30), true)));
    }

    fn state_body() -> Vec<u8> {
        // As the server's PrestigeTests write it: level rank 2, 300 tokens, title 2, level 60.
        let mut b = vec![1, 1, 2, 0];
        b.extend(300u32.to_le_bytes());
        b.extend(0u32.to_le_bytes());
        b.extend((1u32 << 1).to_le_bytes()); // owns Red
        b.extend(2u16.to_le_bytes());
        b.push(1); // wears Red
        b.extend([60, 1, 0, 0, 0]);
        b.push(1); // one profession
        b.extend(202u16.to_le_bytes());
        b.extend(300u16.to_le_bytes());
        b.extend([0, 0]);
        b.push(11);
        b.extend(b"Engineering");
        b.push(1); // one title
        b.extend(2u16.to_le_bytes());
        b.push(8);
        b.extend(b"Corporal");
        b.push(1); // one milestone
        b.extend([0, 0, 0]);
        b.push(3);
        b.extend(b"Duo");
        b.extend(4u16.to_le_bytes());
        b.extend(b"Text");
        b
    }

    #[test]
    fn the_state_reads_as_the_server_writes_it() {
        let Some(Message::State(s)) = decode(&state_body()) else {
            panic!("a state");
        };
        assert_eq!(
            (s.level_rank, s.gun_rank, s.tokens, s.title),
            (2, 0, 300, 2)
        );
        assert_eq!((s.level, s.guns_maxed, s.busy), (60, true, false));
        assert_eq!(s.professions[0].name, "Engineering");
        assert_eq!(s.professions[0].value, 300);
        assert_eq!(s.titles, vec![(2, "Corporal".to_string())]);
        assert_eq!(s.milestone_rows[0].text, "Text");
        assert!(s.owns_tracer(1) && s.owns_tracer(0) && !s.owns_tracer(2));
        let mut cut = state_body();
        cut.pop();
        assert_eq!(decode(&cut), None);
    }

    #[test]
    fn clicks_encode_as_the_server_reads_them() {
        assert_eq!(
            encode(Action::Ask {
                track: TRACK_PROFESSION,
                key: 171
            }),
            vec![1, 2, 3, 171, 0]
        );
        assert_eq!(
            encode(Action::Confirm(0xDEAD_BEEF)),
            vec![1, 3, 0xEF, 0xBE, 0xAD, 0xDE]
        );
        assert_eq!(encode(Action::Tracer(5)), vec![1, 5, 5]);
        assert_eq!(encode(Action::Title(201)), vec![1, 4, 201, 0]);
        assert_eq!(encode(Action::Cancel), vec![1, 6]);
        assert_eq!(encode(Action::Buy(20)), vec![1, 7, 20]);
    }

    #[test]
    fn the_token_shop_reads_as_the_server_writes_it() {
        // As the server's EncodeShop writes it: two goods, the second owned.
        let mut body = vec![VERSION, OP_SHOP, 2];
        body.extend([1, 2, 0, 0, 0, 0, 3]);
        body.extend(b"Abc");
        body.extend([20, 4, 0, 0, 0, 1, 3]);
        body.extend(b"Red");
        let Some(Message::Shop(goods)) = decode(&body) else {
            panic!("no shop");
        };
        assert_eq!(goods.len(), 2);
        assert_eq!(
            goods[1],
            ShopGood {
                id: 20,
                price: 4,
                owned: true,
                name: "Red".into()
            }
        );
        body.pop();
        assert_eq!(decode(&body), None);
    }

    #[test]
    fn the_match_hud_and_scoreboard_read() {
        let mut b = vec![1, 20, 4, 1, 1];
        for v in [30u32, 123_456, 7, 9, 900] {
            b.extend(v.to_le_bytes());
        }
        b.extend(9u16.to_le_bytes());
        b.extend(2u16.to_le_bytes());
        let Some(Message::Hud(h)) = decode(&b) else {
            panic!("a hud");
        };
        assert_eq!((h.mode, h.team, h.limit, h.ms_left), (4, 1, 30, 123_456));
        assert_eq!(
            (h.team_score, h.score, h.kills, h.deaths),
            ([7, 9], 900, 9, 2)
        );
        let mut s = vec![1, 21, 2, 1, 1];
        s.extend(200u32.to_le_bytes());
        s.extend([0, 0, 1, 0, 1, 0]);
        s.push(8);
        s.extend(b"Sylvanas");
        let Some(Message::Scoreboard { mode, rows }) = decode(&s) else {
            panic!("a scoreboard");
        };
        assert_eq!((mode, rows.len(), rows[0].score), (2, 1, 200));
        assert_eq!(rows[0].name, "Sylvanas");
        assert_eq!(clock(125_000), "2:05");
        assert_eq!(mode_name(4), "TEAM DEATHMATCH");
    }

    #[test]
    fn field_gear_messages_read() {
        assert_eq!(
            decode(&[1, 4, 0, 3]),
            Some(Message::Equipment { slot: 0, count: 3 })
        );
        let mut stim = vec![1, 5, 3];
        stim.extend(20_000u32.to_le_bytes());
        assert_eq!(
            decode(&stim),
            Some(Message::Stim {
                kind: 3,
                ms: 20_000
            })
        );
        assert_eq!(
            decode(&[1, 6, 5, 255, 200, 60]),
            Some(Message::Look {
                tracer: 5,
                rgb: [255, 200, 60]
            })
        );
        assert!(is_ghillie(93241) && !is_ghillie(93220));
        let e = FieldEffects {
            adrenaline_until: 10.0,
            ..default()
        };
        assert!(e.tireless(9.0) && !e.tireless(10.5));
    }
}
