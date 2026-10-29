//! The bounty boards (the server's `fusion/Bounties.h`, `game/Fusion/FusionBounties.cpp`), on
//! fusion channel 11.
//!
//! A Bounty Board stands in the towns and outposts of the open-world zones. Using one opens the
//! **Bounty Board** window, laid out as the stock quest giver's: the day's bounties for the zone
//! as a list (each with its kind's icon and its level in the quest log's difficulty colour), and
//! under it the chosen bounty's text, objective, progress and purse, with Accept (or Complete
//! Bounty when it is done) at the foot. A bounty taken is a quest like any other: it sits in the
//! quest log, and the window puts it in the quest tracker (`AddQuestWatch`) so its kills and items
//! count down on screen. Every answer shows in the window's status line.

use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, Kind, RED, WHITE, WindowSpec, WowButton,
    WowTip, text, u,
};

/// The bounty channel (the server's `Bounty::kChannelBounty`).
pub const CHANNEL_BOUNTY: u8 = 11;
const VERSION: u8 = 1;
// Client to server.
const OP_ASK: u8 = 1;
const OP_ACCEPT: u8 = 2;
const OP_TURN_IN: u8 = 3;
// Server to client.
const OP_BOARD: u8 = 1;
const OP_RESULT: u8 = 2;

/// What a bounty asks (the server's `Bounty::Kind`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum BountyKind {
    #[default]
    Kill,
    Collect,
    Hunt,
    Camp,
}

impl BountyKind {
    fn from_u8(k: u8) -> Option<Self> {
        Some(match k {
            1 => Self::Kill,
            2 => Self::Collect,
            3 => Self::Hunt,
            4 => Self::Camp,
            _ => return None,
        })
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::Kill => "Kill",
            Self::Collect => "Collect",
            Self::Hunt => "Hunt",
            Self::Camp => "Clear Out",
        }
    }

    /// The kind's icon, a stock one in the install's `Interface\Icons`.
    pub fn icon(self, elite: bool) -> &'static str {
        match self {
            Self::Kill => "Interface\\Icons\\Ability_DualWield",
            Self::Collect => "Interface\\Icons\\INV_Misc_Bag_10",
            Self::Hunt if elite => "Interface\\Icons\\INV_Misc_Head_Dragon_01",
            Self::Hunt => "Interface\\Icons\\Ability_Hunter_SniperShot",
            Self::Camp => "Interface\\Icons\\Spell_Fire_Fire",
        }
    }
}

/// A row's state for this player (the server's `Bounty::RowState`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum RowState {
    #[default]
    Available,
    Accepted,
    Ready,
    Done,
    Locked,
    Elsewhere,
    Full,
}

impl RowState {
    fn from_u8(s: u8) -> Option<Self> {
        Some(match s {
            0 => Self::Available,
            1 => Self::Accepted,
            2 => Self::Ready,
            3 => Self::Done,
            4 => Self::Locked,
            5 => Self::Elsewhere,
            6 => Self::Full,
            _ => return None,
        })
    }
}

/// One objective's count.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Progress {
    pub have: u8,
    pub need: u8,
    pub label: String,
}

/// A bounty as the board lists it.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Row {
    pub quest: u32,
    pub kind: BountyKind,
    pub state: RowState,
    pub level: u8,
    pub min_level: u8,
    pub elite: bool,
    pub xp: u32,
    pub money: u32,
    pub reputation: u16,
    pub rep_name: String,
    pub title: String,
    pub objective: String,
    pub details: String,
    pub progress: Vec<Progress>,
}

/// The board's window (the server's `Bounty::Board`).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Board {
    /// A board was used: open the window (else the open one is refreshed).
    pub open: bool,
    pub zone: u32,
    pub town: String,
    pub zone_name: String,
    pub done: u8,
    pub cap: u8,
    /// The player's level, for the difficulty colours.
    pub level: u8,
    pub seconds_to_reset: u32,
    pub rows: Vec<Row>,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Message {
    Board(Board),
    Result { ok: bool, text: String },
}

struct Reader<'a> {
    b: &'a [u8],
    at: usize,
}

impl Reader<'_> {
    fn take(&mut self, n: usize) -> Option<&[u8]> {
        let s = self.b.get(self.at..self.at.checked_add(n)?)?;
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

fn read_row(r: &mut Reader) -> Option<Row> {
    let mut row = Row {
        quest: r.u32()?,
        kind: BountyKind::from_u8(r.u8()?)?,
        state: RowState::from_u8(r.u8()?)?,
        level: r.u8()?,
        min_level: r.u8()?,
        elite: r.u8()? != 0,
        xp: r.u32()?,
        money: r.u32()?,
        reputation: r.u16()?,
        rep_name: r.str8()?,
        title: r.str8()?,
        objective: r.str16()?,
        details: r.str16()?,
        progress: Vec::new(),
    };
    for _ in 0..r.u8()? {
        row.progress.push(Progress {
            have: r.u8()?,
            need: r.u8()?,
            label: r.str8()?,
        });
    }
    Some(row)
}

/// Read one bounty-channel body; `None` for anything malformed or cut short.
pub fn decode(body: &[u8]) -> Option<Message> {
    let mut r = Reader { b: body, at: 0 };
    if r.u8()? != VERSION {
        return None;
    }
    let msg = match r.u8()? {
        OP_BOARD => {
            let mut b = Board {
                open: r.u8()? != 0,
                zone: r.u32()?,
                town: r.str8()?,
                zone_name: r.str8()?,
                done: r.u8()?,
                cap: r.u8()?,
                level: r.u8()?,
                seconds_to_reset: r.u32()?,
                rows: Vec::new(),
            };
            for _ in 0..r.u8()? {
                b.rows.push(read_row(&mut r)?);
            }
            Message::Board(b)
        }
        OP_RESULT => Message::Result {
            ok: r.u8()? != 0,
            text: r.str8()?,
        },
        _ => return None,
    };
    (r.at == body.len()).then_some(msg)
}

/// What the window asks of the server.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    /// The board again (its rows' progress).
    Ask,
    Accept(u32),
    TurnIn(u32),
}

pub fn encode(a: Action) -> Vec<u8> {
    match a {
        Action::Ask => vec![VERSION, OP_ASK],
        Action::Accept(q) => {
            let mut b = vec![VERSION, OP_ACCEPT];
            b.extend(q.to_le_bytes());
            b
        }
        Action::TurnIn(q) => {
            let mut b = vec![VERSION, OP_TURN_IN];
            b.extend(q.to_le_bytes());
            b
        }
    }
}

// ── What the window says ────────────────────────────────────────────────────────────────────────

/// The quest log's difficulty colour for a bounty of `level` to a player of `player`
/// (`GetDifficultyColor`: red five or more above, orange three, yellow within two, green down to
/// the grey level, grey below it).
pub fn difficulty_color(level: u8, player: u8) -> Color {
    let diff = i32::from(level) - i32::from(player);
    let p = i32::from(player);
    let grey_below = if p <= 5 {
        5
    } else if p <= 39 {
        p / 10 + 5
    } else if p <= 59 {
        p / 5 + 1
    } else {
        9
    };
    if diff >= 5 {
        Color::srgb(1.0, 0.1, 0.1)
    } else if diff >= 3 {
        Color::srgb(1.0, 0.5, 0.25)
    } else if diff >= -2 {
        Color::srgb(1.0, 0.82, 0.0)
    } else if -diff <= grey_below {
        Color::srgb(0.25, 0.75, 0.25)
    } else {
        GREY
    }
}

/// A row's level tag, as the quest log's: `[12]`, `[12+]` for an elite.
pub fn level_tag(row: &Row) -> String {
    format!("[{}{}]", row.level, if row.elite { "+" } else { "" })
}

/// A row's state, said under its title (empty when it can be taken).
pub fn state_text(row: &Row) -> String {
    match row.state {
        RowState::Available => String::new(),
        RowState::Accepted => "In progress".into(),
        RowState::Ready => "Complete: turn it in".into(),
        RowState::Done => "Collected today".into(),
        RowState::Locked => format!("Requires level {}", row.min_level),
        RowState::Elsewhere => "Held: turn it in at its own zone's boards".into(),
        RowState::Full => "Daily limit reached".into(),
    }
}

fn state_color(s: RowState) -> Color {
    match s {
        RowState::Available | RowState::Accepted => DIM,
        RowState::Ready => GREEN,
        RowState::Locked => RED,
        RowState::Done | RowState::Elsewhere | RowState::Full => GREY,
    }
}

/// The foot button for the chosen row: its caption, and what it does (None: greyed).
pub fn foot_button(row: Option<&Row>) -> (&'static str, Option<Action>) {
    match row {
        Some(r) if r.state == RowState::Available => ("Accept", Some(Action::Accept(r.quest))),
        Some(r) if r.state == RowState::Ready => ("Complete Bounty", Some(Action::TurnIn(r.quest))),
        Some(r) if matches!(r.state, RowState::Accepted | RowState::Elsewhere) => {
            ("Complete Bounty", None)
        }
        _ => ("Accept", None),
    }
}

/// A quest text's `$B` line breaks (the server writes the quest log's markup).
pub fn quest_text(s: &str) -> String {
    s.replace("$B", "\n").replace("$b", "\n")
}

/// "3h 20m" until the boards post again.
pub fn reset_text(secs: u32) -> String {
    let h = secs / 3600;
    let m = (secs / 60) % 60;
    if h > 0 {
        format!("{h}h {m}m")
    } else {
        format!("{}m", m.max(1))
    }
}

/// Copper as gold, silver and copper.
pub fn coins(copper: u32) -> (u32, u32, u32) {
    (copper / 10_000, (copper / 100) % 100, copper % 100)
}

/// The kind's tooltip on a row's icon.
fn kind_tip(row: &Row) -> WowTip {
    let body = match row.kind {
        BountyKind::Kill => "Kill the creatures named. Any that you or your group kill count.",
        BountyKind::Collect => "Bring the items: the creatures named carry them.",
        BountyKind::Hunt if row.elite => {
            "A named elite: dangerous alone. It pays twice a kill bounty's purse."
        }
        BountyKind::Hunt => "A rare creature: seldom seen, quick to vanish. It pays half again.",
        BountyKind::Camp => "Clear a camp of the creatures named. It pays a quarter more.",
    };
    WowTip::new(&format!("{} Bounty", row.kind.name()), body)
}

// ── The window ──────────────────────────────────────────────────────────────────────────────────

/// The window's state.
#[derive(Resource, Default)]
pub struct BountyBoard {
    pub open: bool,
    /// The board as the server last sent it.
    pub board: Board,
    /// The chosen bounty's quest id.
    pub selected: Option<u32>,
    /// The server's last answer: (ok, text).
    pub status: Option<(bool, String)>,
    /// When the board came (elapsed seconds), for the reset countdown.
    pub received_at: f32,
    /// Bumped on every change the window draws.
    pub version: u32,
    drawn: Option<u32>,
    held_cursor: bool,
    /// A bounty just taken, to put in the quest tracker once the quest log has it: its title and
    /// the time to stop trying.
    pub watch: Option<(String, f32)>,
}

impl BountyBoard {
    pub fn chosen(&self) -> Option<&Row> {
        let id = self.selected?;
        self.board.rows.iter().find(|r| r.quest == id)
    }
}

#[derive(Component)]
struct BountyRoot;
#[derive(Component)]
struct BountyClose;
#[derive(Component)]
struct BountyHeader;
#[derive(Component)]
struct BountyTimer;
#[derive(Component)]
struct BountyList;
#[derive(Component)]
struct BountyDetails;
#[derive(Component)]
struct BountyStatus;
#[derive(Component)]
struct BountyFoot;
#[derive(Component, Clone, Copy)]
struct RowButton(u32);
#[derive(Component, Clone, Copy)]
struct FootAction(Option<Action>);
#[derive(Component)]
struct CloseButton;

#[derive(Resource)]
struct BountyFont(Handle<Font>);

pub struct BountyPlugin;

impl Plugin for BountyPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<BountyBoard>()
            .add_systems(
                Update,
                (spawn_window, receive, open_close, click, draw, watch_taken)
                    .chain()
                    .in_set(Mw2Set::Hud),
            )
            // After every window's own systems: the cursor stays out while this one is open.
            .add_systems(Update, hold_cursor.after(Mw2Set::Hud));
    }
}

/// The window's width in UI units, the list's height, and a list line's width.
const WIDTH: f32 = 384.0;
const LIST_HEIGHT: f32 = 150.0;
const ROW_WIDTH: f32 = 318.0;
/// How long a taken bounty is tried for the tracker (seconds).
const WATCH_FOR: f32 = 10.0;

fn spawn_window(
    mut commands: Commands,
    assets: Res<AssetServer>,
    state: Res<FpsState>,
    mut built: Local<bool>,
) {
    if *built || !crate::capture_keep::in_world(state.in_world, "bounty") {
        return;
    }
    *built = true;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: "bounty board",
        title: "Bounty Board".into(),
        frame: Frame::Dialog,
        width: Some(WIDTH),
        movable: true,
        // Where the stock UIParent sets the quest giver's frame.
        anchor: Anchor {
            at: Vec2::ZERO,
            pivot: Vec2::ZERO,
            offset: Vec2::new(16.0, 104.0),
        },
        z: i32::MAX - 3,
    };
    wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        (BountyRoot, Visibility::Hidden, wow_ui::WindowSounds::MENU),
        Some(BountyClose),
        |root| {
            root.spawn((
                BountyHeader,
                Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: u(2.0),
                    margin: UiRect::bottom(u(6.0)),
                    ..default()
                },
            ));
            root.spawn((
                Node {
                    margin: UiRect::bottom(u(4.0)),
                    ..default()
                },
                children![(BountyTimer, text(f, "", 10.0, DIM))],
            ));
            wow_ui::inset(
                root,
                Node {
                    flex_direction: FlexDirection::Column,
                    ..default()
                },
                (),
                |c| {
                    wow_ui::scroll_frame(c, LIST_HEIGHT, None, BountyList, |_| {});
                },
            );
            root.spawn((
                BountyDetails,
                Node {
                    flex_direction: FlexDirection::Column,
                    row_gap: u(3.0),
                    min_height: u(150.0),
                    margin: UiRect::top(u(6.0)),
                    ..default()
                },
            ));
            root.spawn((
                Node {
                    min_height: u(28.0),
                    margin: UiRect::top(u(4.0)),
                    ..default()
                },
                children![(BountyStatus, text(f, "", 11.0, WHITE))],
            ));
            root.spawn((
                BountyFoot,
                Node {
                    flex_direction: FlexDirection::Row,
                    justify_content: JustifyContent::SpaceBetween,
                    ..default()
                },
            ));
        },
        |_| {},
    );
    commands.insert_resource(BountyFont(font));
}

/// The server's board and answers.
fn receive(
    time: Res<Time>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut bb: ResMut<BountyBoard>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_BOUNTY {
            continue;
        }
        let Some(msg) = decode(&m.body) else {
            warn!("benilla-mw2: a bounty board message it can't read");
            continue;
        };
        match msg {
            Message::Board(b) => {
                if b.open {
                    bb.open = true;
                    bb.status = None;
                }
                // Keep the chosen bounty if it is still listed, else the first.
                let keep = bb.selected.filter(|q| b.rows.iter().any(|r| r.quest == *q));
                bb.selected = keep.or_else(|| b.rows.first().map(|r| r.quest));
                bb.board = b;
                bb.received_at = time.elapsed_secs();
            }
            Message::Result { ok, text } => {
                if ok && let Some(title) = text.strip_prefix("Bounty taken: ") {
                    bb.watch = Some((title.to_string(), time.elapsed_secs() + WATCH_FOR));
                }
                bb.status = Some((ok, text));
            }
        }
        bb.version = bb.version.wrapping_add(1);
    }
}

/// Esc, the close buttons and leaving the world close it.
fn open_close(
    input: Res<Mw2Input>,
    state: Res<FpsState>,
    mut bb: ResMut<BountyBoard>,
    close: Query<&Interaction, (With<BountyClose>, Changed<Interaction>)>,
    closes: Query<&Interaction, (With<CloseButton>, Changed<Interaction>)>,
) {
    let pressed = close
        .iter()
        .chain(closes.iter())
        .any(|i| *i == Interaction::Pressed);
    if bb.open && (input.escape || pressed || !crate::capture_keep::in_world(state.in_world, "bounty")) {
        bb.open = false;
        bb.version = bb.version.wrapping_add(1);
    }
    if !bb.open && crate::capture_keep::keeps("bounty") {
        bb.open = true;
        bb.version = bb.version.wrapping_add(1);
    }
}

fn click(
    mut bb: ResMut<BountyBoard>,
    mut send: MessageWriter<SendFusionChannel>,
    rows: Query<(&Interaction, &RowButton), Changed<Interaction>>,
    foot: Query<(&Interaction, &FootAction), Changed<Interaction>>,
) {
    if !bb.open {
        return;
    }
    for (i, r) in &rows {
        if *i == Interaction::Pressed && bb.selected != Some(r.0) {
            bb.selected = Some(r.0);
            bb.version = bb.version.wrapping_add(1);
        }
    }
    for (i, a) in &foot {
        if *i != Interaction::Pressed {
            continue;
        }
        if let Some(action) = a.0 {
            bb.status = None;
            bb.version = bb.version.wrapping_add(1);
            send.write(SendFusionChannel(ChannelMessage {
                channel: CHANNEL_BOUNTY,
                body: encode(action),
            }));
        }
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn draw(
    mut commands: Commands,
    time: Res<Time>,
    mut bb: ResMut<BountyBoard>,
    font: Option<Res<BountyFont>>,
    assets: Res<AssetServer>,
    mut root: Query<&mut Visibility, With<BountyRoot>>,
    header: Query<(Entity, Option<&Children>), With<BountyHeader>>,
    list: Query<(Entity, Option<&Children>), With<BountyList>>,
    details: Query<(Entity, Option<&Children>), With<BountyDetails>>,
    foot: Query<(Entity, Option<&Children>), With<BountyFoot>>,
    mut status: Query<(&mut Text, &mut TextColor), (With<BountyStatus>, Without<BountyTimer>)>,
    mut timer: Query<&mut Text, (With<BountyTimer>, Without<BountyStatus>)>,
) {
    let Some(font) = font else {
        return;
    };
    let want = if bb.open {
        Visibility::Inherited
    } else {
        Visibility::Hidden
    };
    for mut v in &mut root {
        if *v != want {
            *v = want;
        }
    }
    if !bb.open {
        bb.drawn = None;
        return;
    }
    let f = &font.0;
    // The countdown, every frame (it only changes its text when the minute does).
    let left = (bb.board.seconds_to_reset as f32 - (time.elapsed_secs() - bb.received_at)).max(0.0);
    let clock = format!(
        "Bounties collected today: {}/{}    New bounties in {}",
        bb.board.done,
        bb.board.cap,
        reset_text(left as u32)
    );
    for mut t in &mut timer {
        if t.0 != clock {
            t.0 = clock.clone();
        }
    }
    if bb.drawn == Some(bb.version) {
        return;
    }
    bb.drawn = Some(bb.version);
    let board = bb.board.clone();
    let chosen = bb.chosen().cloned();
    if let Some(e) = replace(&mut commands, &header) {
        commands.entity(e).with_children(|h| {
            let place = if board.town.is_empty() {
                board.zone_name.clone()
            } else {
                format!("{}, {}", board.town, board.zone_name)
            };
            h.spawn(text(f, &place, 14.0, GOLD));
            h.spawn((
                Node {
                    max_width: u(WIDTH - 40.0),
                    ..default()
                },
                children![text(
                    f,
                    "Bounties posted for this zone today. Take one, do it anywhere in the zone, \
                     and claim the purse at any of its boards.",
                    10.0,
                    DIM,
                )],
            ));
        });
    }
    if let Some(e) = replace(&mut commands, &list) {
        commands.entity(e).with_children(|l| {
            if board.rows.is_empty() {
                l.spawn(text(f, "Nothing is posted here today.", 12.0, GREY));
            }
            for row in &board.rows {
                list_row(
                    l,
                    f,
                    &assets,
                    row,
                    board.level,
                    bb.selected == Some(row.quest),
                );
            }
        });
    }
    if let Some(e) = replace(&mut commands, &details) {
        commands
            .entity(e)
            .with_children(|d| draw_details(d, f, &assets, chosen.as_ref(), board.level));
    }
    if let Some(e) = replace(&mut commands, &foot) {
        let (caption, action) = foot_button(chosen.as_ref());
        commands.entity(e).with_children(|r| {
            let tip = match action {
                Some(Action::TurnIn(_)) => WowTip::new(
                    "Complete Bounty",
                    "Claim the purse. A bounty is turned in at any board in its zone.",
                ),
                Some(_) => WowTip::new(
                    "Accept",
                    "Take the bounty: it goes in your quest log and the quest tracker.",
                ),
                None => WowTip::default(),
            };
            r.spawn(wow_ui::button_fit(
                f,
                caption,
                WowButton {
                    disabled: action.is_none(),
                    ..WowButton::new(Kind::Panel)
                },
                (FootAction(action), tip),
            ));
            r.spawn(wow_ui::button(f, "Close", 80.0, CloseButton));
        });
    }
    if let Ok((mut t, mut c)) = status.single_mut() {
        let (s, color) = match &bb.status {
            Some((ok, s)) => (s.clone(), if *ok { GREEN } else { RED }),
            None => (String::new(), WHITE),
        };
        if t.0 != s {
            t.0 = s;
        }
        if c.0 != color {
            c.0 = color;
        }
    }
}

/// A container's children despawned, for drawing it again; the container.
fn replace<F: bevy::ecs::query::QueryFilter>(
    commands: &mut Commands,
    q: &Query<(Entity, Option<&Children>), F>,
) -> Option<Entity> {
    q.single().ok().map(|(e, children)| {
        for c in children.into_iter().flatten() {
            commands.entity(*c).despawn();
        }
        e
    })
}

/// One line of the list: the kind's icon, the level tag and title in the difficulty colour, its
/// state under it.
fn list_row(
    l: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    row: &Row,
    player: u8,
    selected: bool,
) {
    let mut state = WowButton::new(Kind::Row);
    state.selected = selected;
    let title_color = if matches!(row.state, RowState::Done | RowState::Full) {
        GREY
    } else {
        difficulty_color(row.level, player)
    };
    l.spawn((
        Button,
        state,
        RowButton(row.quest),
        kind_tip(row),
        Node {
            width: u(ROW_WIDTH),
            min_height: u(30.0),
            padding: UiRect::axes(u(4.0), u(2.0)),
            flex_direction: FlexDirection::Row,
            align_items: AlignItems::Center,
            column_gap: u(6.0),
            margin: UiRect::bottom(u(2.0)),
            flex_shrink: 0.0,
            ..default()
        },
    ))
    .with_children(|r| {
        wow_ui::row_glow(r);
        r.spawn(wow_ui::icon_frame_lit(
            Some(assets.load(crate::cac_ui::spell_icon_path(row.kind.icon(row.elite)))),
            24.0,
            wow_ui::SLOT_EDGE,
            !matches!(row.state, RowState::Done | RowState::Locked),
        ));
        r.spawn(Node {
            flex_direction: FlexDirection::Column,
            max_width: u(ROW_WIDTH - 40.0),
            ..default()
        })
        .with_children(|c| {
            c.spawn(text(
                f,
                &format!("{} {}", level_tag(row), row.title),
                12.0,
                title_color,
            ));
            let said = state_text(row);
            if !said.is_empty() {
                c.spawn(text(f, &said, 10.0, state_color(row.state)));
            }
        });
    });
}

/// The chosen bounty, as the quest giver's detail page: title, objective, text, progress, purse.
fn draw_details(
    d: &mut ChildSpawnerCommands,
    f: &Handle<Font>,
    assets: &AssetServer,
    row: Option<&Row>,
    player: u8,
) {
    let Some(row) = row else {
        return;
    };
    let wrap = |d: &mut ChildSpawnerCommands, s: &str, size: f32, color: Color| {
        d.spawn((
            Node {
                max_width: u(WIDTH - 40.0),
                ..default()
            },
            children![text(f, s, size, color)],
        ));
    };
    wrap(d, &row.title, 14.0, GOLD);
    let difficulty = if row.elite {
        format!("{}  ·  Level {} elite", row.kind.name(), row.level)
    } else {
        format!("{}  ·  Level {}", row.kind.name(), row.level)
    };
    wrap(d, &difficulty, 11.0, difficulty_color(row.level, player));
    wrap(d, &row.objective, 12.0, WHITE);
    for p in &row.progress {
        let done = p.have >= p.need;
        wrap(
            d,
            &format!("{}: {}/{}", p.label, p.have, p.need),
            11.0,
            if done { GREEN } else { WHITE },
        );
    }
    wrap(d, &quest_text(&row.details), 11.0, DIM);
    wow_ui::section_header(d, f, "Rewards");
    d.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        column_gap: u(10.0),
        ..default()
    })
    .with_children(|r| {
        r.spawn(text(f, &format!("{} experience", row.xp), 11.0, WHITE));
        money(r, f, assets, row.money);
    });
    if row.reputation > 0 && !row.rep_name.is_empty() {
        wrap(
            d,
            &format!("+{} reputation with {}", row.reputation, row.rep_name),
            11.0,
            WHITE,
        );
    }
}

/// A purse as the money frame shows it: each coin's count before its icon.
fn money(r: &mut ChildSpawnerCommands, f: &Handle<Font>, assets: &AssetServer, copper: u32) {
    let (g, s, c) = coins(copper);
    r.spawn(Node {
        flex_direction: FlexDirection::Row,
        align_items: AlignItems::Center,
        column_gap: u(2.0),
        ..default()
    })
    .with_children(|m| {
        for (n, icon) in [(g, "Gold"), (s, "Silver"), (c, "Copper")] {
            if n == 0 && !(icon == "Copper" && g == 0 && s == 0) {
                continue;
            }
            m.spawn(text(f, &n.to_string(), 11.0, WHITE));
            m.spawn((
                ImageNode::new(
                    assets.load(format!("mpq://Interface/MoneyFrame/UI-{icon}Icon.blp")),
                ),
                Node {
                    width: u(13.0),
                    height: u(13.0),
                    margin: UiRect::right(u(3.0)),
                    ..default()
                },
                bevy::picking::Pickable::IGNORE,
            ));
        }
    });
}

/// The Lua that puts the quest titled `title` in the quest tracker: true once the quest log
/// has it (watched now, or already, or the tracker full).
pub fn watch_lua(title: &str) -> String {
    format!(
        "local t = {}\n\
         for i = 1, GetNumQuestLogEntries() do\n\
         \x20 local title, _, _, header = GetQuestLogTitle(i)\n\
         \x20 if not header and title == t then\n\
         \x20   if not IsQuestWatched(i) and GetNumQuestWatches() < (MAX_WATCHABLE_QUESTS or 5) then\n\
         \x20     AddQuestWatch(i)\n\
         \x20     if QuestWatch_Update then QuestWatch_Update() end\n\
         \x20   end\n\
         \x20   return true\n\
         \x20 end\n\
         end\n\
         return false",
        crate::lua::quote(title)
    )
}

/// A bounty just taken: into the quest tracker once the quest log lists it.
fn watch_taken(
    time: Res<Time>,
    mut bb: ResMut<BountyBoard>,
    script: Option<NonSend<UiScript>>,
    mut next: Local<f32>,
) {
    let Some((title, until)) = bb.watch.clone() else {
        return;
    };
    let t = time.elapsed_secs();
    if t > until {
        bb.watch = None;
        return;
    }
    if t < *next {
        return;
    }
    *next = t + 0.5;
    let Some(script) = script else {
        return;
    };
    if crate::lua::eval::<bool>(&script, &watch_lua(&title)).unwrap_or(false) {
        bb.watch = None;
    }
}

fn hold_cursor(mut bb: ResMut<BountyBoard>, mut state: ResMut<FpsState>) {
    if bb.open {
        if !state.panel {
            state.panel = true;
        }
        bb.held_cursor = true;
    } else if bb.held_cursor {
        bb.held_cursor = false;
        state.panel = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A board body as the server's `Bounty::EncodeBoard` writes it (BountyTests.cpp's layout).
    fn board_body() -> Vec<u8> {
        let mut b = vec![VERSION, OP_BOARD, 1];
        b.extend(40u32.to_le_bytes());
        b.push(13);
        b.extend(b"Sentinel Hill");
        b.push(8);
        b.extend(b"Westfall");
        b.extend([2, 5, 14]); // done, cap, level
        b.extend(3600u32.to_le_bytes());
        b.push(1); // one row
        b.extend(95012u32.to_le_bytes());
        b.extend([3, 1, 16, 14, 1]); // hunt, accepted, level 16, min 14, elite
        b.extend(1_920u32.to_le_bytes());
        b.extend(2_880u32.to_le_bytes());
        b.extend(150u16.to_le_bytes());
        b.push(9);
        b.extend(b"Stormwind");
        b.push(23);
        b.extend(b"Wanted: Foe Reaper 4000");
        b.extend(20u16.to_le_bytes());
        b.extend(b"Kill Foe Reaper 4000");
        b.extend(7u16.to_le_bytes());
        b.extend(b"A$BB c.");
        b.push(1); // one objective
        b.extend([0, 1, 15]);
        b.extend(b"Foe Reaper 4000");
        b
    }

    #[test]
    fn the_board_reads_as_the_server_writes_it() {
        let Some(Message::Board(b)) = decode(&board_body()) else {
            panic!("a board");
        };
        assert!(b.open);
        assert_eq!(
            (b.zone, b.town.as_str(), b.zone_name.as_str()),
            (40, "Sentinel Hill", "Westfall")
        );
        assert_eq!(
            (b.done, b.cap, b.level, b.seconds_to_reset),
            (2, 5, 14, 3600)
        );
        let r = &b.rows[0];
        assert_eq!(
            (r.quest, r.kind, r.state),
            (95012, BountyKind::Hunt, RowState::Accepted)
        );
        assert_eq!((r.level, r.min_level, r.elite), (16, 14, true));
        assert_eq!((r.xp, r.money, r.reputation), (1_920, 2_880, 150));
        assert_eq!(r.rep_name, "Stormwind");
        assert_eq!(r.title, "Wanted: Foe Reaper 4000");
        assert_eq!(r.details, "A$BB c.");
        assert_eq!(
            r.progress,
            vec![Progress {
                have: 0,
                need: 1,
                label: "Foe Reaper 4000".into()
            }]
        );
        // Cut short, or with a byte too many: not a board.
        let mut cut = board_body();
        cut.pop();
        assert_eq!(decode(&cut), None);
        let mut long = board_body();
        long.push(0);
        assert_eq!(decode(&long), None);
        // Another version, or an unknown kind.
        let mut v2 = board_body();
        v2[0] = 2;
        assert_eq!(decode(&v2), None);
    }

    #[test]
    fn an_answer_reads_and_clicks_encode_as_the_server_reads_them() {
        let mut body = vec![VERSION, OP_RESULT, 0, 5];
        body.extend(b"Nope.");
        assert_eq!(
            decode(&body),
            Some(Message::Result {
                ok: false,
                text: "Nope.".into()
            })
        );
        assert_eq!(encode(Action::Ask), vec![1, 1]);
        assert_eq!(encode(Action::Accept(95012)), vec![1, 2, 0x24, 0x73, 1, 0]);
        assert_eq!(encode(Action::TurnIn(96001)), vec![1, 3, 0x01, 0x77, 1, 0]);
        // Hostile bytes never panic.
        for len in 0..64 {
            for fill in [0u8, 0xFF, 1] {
                let mut b = vec![fill; len];
                if !b.is_empty() {
                    b[0] = VERSION;
                }
                let _ = decode(&b);
            }
        }
    }

    #[test]
    fn rows_say_their_state_and_difficulty() {
        let mut r = Row {
            level: 12,
            min_level: 8,
            quest: 7,
            ..default()
        };
        assert_eq!(level_tag(&r), "[12]");
        r.elite = true;
        assert_eq!(level_tag(&r), "[12+]");
        assert_eq!(foot_button(Some(&r)), ("Accept", Some(Action::Accept(7))));
        r.state = RowState::Ready;
        assert_eq!(
            foot_button(Some(&r)),
            ("Complete Bounty", Some(Action::TurnIn(7)))
        );
        r.state = RowState::Accepted;
        assert_eq!(foot_button(Some(&r)), ("Complete Bounty", None));
        r.state = RowState::Locked;
        assert_eq!(state_text(&r), "Requires level 8");
        assert_eq!(foot_button(Some(&r)), ("Accept", None));
        assert_eq!(foot_button(None), ("Accept", None));
        // The quest log's colours: red far above, yellow near, green below, grey far below.
        assert_eq!(difficulty_color(20, 12), Color::srgb(1.0, 0.1, 0.1));
        assert_eq!(difficulty_color(13, 12), Color::srgb(1.0, 0.82, 0.0));
        assert_eq!(difficulty_color(7, 12), Color::srgb(0.25, 0.75, 0.25));
        assert_eq!(difficulty_color(2, 12), GREY);
        assert_eq!(quest_text("One.$BTwo."), "One.\nTwo.");
        assert_eq!(reset_text(3 * 3600 + 20 * 60 + 5), "3h 20m");
        assert_eq!(reset_text(10), "1m");
        assert_eq!(coins(12_345), (1, 23, 45));
    }

    #[test]
    fn the_tracker_is_asked_for_the_title_taken() {
        let lua = watch_lua("Bounty: \"Big\" Wolf");
        assert!(lua.contains(r#"local t = "Bounty: \"Big\" Wolf""#));
        assert!(lua.contains("AddQuestWatch(i)"));
    }

    #[test]
    fn the_window_builds_and_lists_the_board() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        world.init_resource::<Time>();
        world.insert_resource(FpsState {
            in_world: true,
            ..default()
        });
        world.init_resource::<BountyBoard>();
        world.run_system_once(spawn_window).unwrap();
        let Some(Message::Board(mut b)) = decode(&board_body()) else {
            panic!("a board");
        };
        let mut second = b.rows[0].clone();
        second.quest = 95013;
        second.state = RowState::Available;
        second.elite = false;
        second.kind = BountyKind::Collect;
        b.rows.push(second);
        {
            let mut bb = world.resource_mut::<BountyBoard>();
            bb.open = true;
            bb.selected = Some(95013);
            bb.board = b;
            bb.version = 1;
        }
        world.run_system_once(draw).unwrap();
        let mut rows = world.query::<&RowButton>();
        assert_eq!(rows.iter(&world).count(), 2);
        // The chosen bounty can be taken: Accept, live.
        let mut foot = world.query::<(&FootAction, &WowButton)>();
        let foot: Vec<_> = foot.iter(&world).map(|(a, w)| (a.0, w.disabled)).collect();
        assert_eq!(foot, vec![(Some(Action::Accept(95013)), false)]);
        let vis = world
            .query_filtered::<&Visibility, With<BountyRoot>>()
            .single(&world)
            .copied()
            .unwrap();
        assert_eq!(vis, Visibility::Inherited);
    }
}
