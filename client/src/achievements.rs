//! Achievements and unlocks (the server's `fusion/Achievements.h`): every account starts with the
//! USP .45 alone and earns the rest of its guns, attachments, camos, perks, deathstreaks,
//! equipment and killstreaks through achievements. The server counts and decides; this module
//! reads what it sends on fusion channel 3 and shows it:
//!
//! - [`Unlocks`], the account's unlock set (op 5), which Create-a-Class (crate::cac_ui) marks
//!   locked entries by, with the achievement that unlocks each ([`Achievements::unlocked_by`]);
//! - the **Achievements** window ([`Panel::Journal`]: **Y**, or the quest log's Achievements tab), the
//!   quest log's neighbour, with the same two tabs under it (Quests, Achievements): the record (kills,
//!   deaths, K/D and best streak, the account's from the server's op 6 and this session's), the
//!   achievements by category, each with its progress bars and what it unlocks, and Prestige
//!   (crate::prestige); it asks for the list when it opens (op 1) and keeps it current from the
//!   pages (op 2) and progress updates (op 4);
//! - a toast for each achievement earned (op 3).

use std::collections::{BTreeMap, HashSet};

use benilla_app::fusion::{ChannelMessage, FusionChannelReceived, SendFusionChannel};
use bevy::prelude::*;

use benilla_ui::script::UiScript;

use crate::Mw2Set;
use crate::catalog::Catalog;
use crate::mode::FpsState;
use crate::panels::{OpenPanel, Panel};
use crate::streaks::Streak;
use crate::wow_ui::{
    self, Anchor, DIM, FONT, Frame, GOLD, GREEN, GREY, WHITE, WindowSpec, WowButton, text, u,
};

/// The achievements channel (the server's `Packets::kChannelAchievements`).
pub const CHANNEL_ACHIEVEMENTS: u8 = 3;
/// 2: every row carries its points (Wrath's categories and meta-achievements).
const VERSION: u8 = 2;
const OP_REQUEST: u8 = 1;
const OP_PAGE: u8 = 2;
const OP_EARNED: u8 = 3;
const OP_PROGRESS: u8 = 4;
const OP_UNLOCKS: u8 = 5;
const OP_RECORD: u8 = 6;
const FLAG_EARNED: u8 = 1;
const FLAG_HIDDEN: u8 = 2;

/// The message asking the server for the achievement list, the unlocks and the record (the
/// window does when it opens; the kit window too, for its unlock lines).
pub fn request_message() -> ChannelMessage {
    ChannelMessage {
        channel: CHANNEL_ACHIEVEMENTS,
        body: vec![VERSION, OP_REQUEST],
    }
}

/// What an unlock is (the server's `UnlockKind`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum UnlockKind {
    Gun = 1,
    Attachment = 2,
    Camo = 3,
    Perk = 4,
    Deathstreak = 5,
    Lethal = 6,
    Tactical = 7,
    Killstreak = 8,
    Title = 9,
    /// A perk's Pro (its challenge done).
    PerkPro = 10,
}

impl UnlockKind {
    fn from_u8(v: u8) -> Option<Self> {
        Some(match v {
            1 => Self::Gun,
            2 => Self::Attachment,
            3 => Self::Camo,
            4 => Self::Perk,
            5 => Self::Deathstreak,
            6 => Self::Lethal,
            7 => Self::Tactical,
            8 => Self::Killstreak,
            9 => Self::Title,
            10 => Self::PerkPro,
            _ => return None,
        })
    }
}

/// One unlock: a kind, its id (a gun for attachments and camos), a camo id, an attachment id.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Unlock {
    pub kind: UnlockKind,
    pub id: u32,
    pub camo: u8,
    pub attachment: String,
}

/// The account's unlocks. Until the server says, everything is open (an old server, or no
/// server: the layer plays as before).
#[derive(Resource, Debug, Clone)]
pub struct Unlocks {
    pub everything: bool,
    pub held: HashSet<Unlock>,
}

impl Default for Unlocks {
    fn default() -> Self {
        Self {
            everything: true,
            held: HashSet::new(),
        }
    }
}

impl Unlocks {
    fn has(&self, kind: UnlockKind, id: u32, camo: u8, attachment: &str) -> bool {
        self.everything
            || self.held.contains(&Unlock {
                kind,
                id,
                camo,
                attachment: attachment.to_string(),
            })
    }
    /// "None" is always allowed (an empty slot).
    pub fn gun(&self, id: u16) -> bool {
        id == 0 || self.has(UnlockKind::Gun, u32::from(id), 0, "")
    }
    pub fn attachment(&self, gun: u16, attachment: &str) -> bool {
        attachment.is_empty() || self.has(UnlockKind::Attachment, u32::from(gun), 0, attachment)
    }
    pub fn camo(&self, gun: u16, camo: u8) -> bool {
        camo == 0 || self.has(UnlockKind::Camo, u32::from(gun), camo, "")
    }
    pub fn perk(&self, id: u16) -> bool {
        id == 0 || self.has(UnlockKind::Perk, u32::from(id), 0, "")
    }
    pub fn deathstreak(&self, id: u16) -> bool {
        id == 0 || self.has(UnlockKind::Deathstreak, u32::from(id), 0, "")
    }
    pub fn lethal(&self, id: u16) -> bool {
        id == 0 || self.has(UnlockKind::Lethal, u32::from(id), 0, "")
    }
    pub fn tactical(&self, id: u16) -> bool {
        id == 0 || self.has(UnlockKind::Tactical, u32::from(id), 0, "")
    }
    pub fn killstreak(&self, id: u32) -> bool {
        id == 0 || self.has(UnlockKind::Killstreak, id, 0, "")
    }
    /// A perk's Pro: earned through its challenge (no perk has none).
    pub fn perk_pro(&self, id: u16) -> bool {
        id != 0 && self.has(UnlockKind::PerkPro, u32::from(id), 0, "")
    }
}

/// One requirement's progress: the counter it reads, how far along, how far it needs.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Requirement {
    pub stat: u16,
    pub key: u32,
    pub current: u32,
    pub needed: u32,
}

/// One achievement as the window shows it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Row {
    pub id: u32,
    pub category: u8,
    pub earned: bool,
    pub hidden: bool,
    /// Wrath's achievement points (Feats of Strength have none).
    pub points: u16,
    pub name: String,
    pub description: String,
    pub requirements: Vec<Requirement>,
    pub rewards: Vec<Unlock>,
}

impl Row {
    /// How far along, 0..1 (the weakest requirement).
    pub fn progress(&self) -> f32 {
        if self.earned {
            return 1.0;
        }
        self.requirements
            .iter()
            .map(|r| r.current as f32 / r.needed.max(1) as f32)
            .fold(1.0f32, f32::min)
            .clamp(0.0, 1.0)
    }
}

/// The Achievements's own page: the kills and deaths record (after Prestige's tab id).
pub const RECORD_TAB: u8 = 11;

/// The Achievements's pages down its left, as Wrath's category list: the record, Wrath's
/// categories (the server's `AchievementCategory` 20-31), the arsenal's unlock tabs indented
/// under their own heading (1-9), then Prestige (crate::prestige). The bool: an indented child.
pub const CATEGORIES: [(u8, &str, bool); 24] = [
    (RECORD_TAB, "Record", false),
    (20, "General", false),
    (21, "Quests", false),
    (22, "Exploration", false),
    (23, "Player vs. Player", false),
    (24, "Dungeons & Raids", false),
    (25, "Professions", false),
    (26, "Reputation", false),
    (27, "World Events", false),
    (28, "Guns & Kits", false),
    (29, "Companions", false),
    (30, "Wanderers", false),
    (31, "Feats of Strength", false),
    (ARSENAL_HEADING, "Arsenal Unlocks", false),
    (1, "Weapons", true),
    (2, "Attachments", true),
    (3, "Camos", true),
    (4, "Perks", true),
    (5, "Deathstreaks", true),
    (6, "Lethals", true),
    (7, "Tacticals", true),
    (8, "Killstreaks", true),
    (9, "Milestones", true),
    (crate::prestige::PRESTIGE_TAB, "Prestige", false),
];

/// The arsenal's heading in the category list: it shows its first child, Weapons.
pub const ARSENAL_HEADING: u8 = 200;

/// The tab a category button opens (the heading opens its first child).
pub fn tab_of(id: u8) -> u8 {
    if id == ARSENAL_HEADING { 1 } else { id }
}

/// The titles an achievement may grant, by the server's `TitleName` ids (1-based).
pub const TITLE_NAMES: [&str; 29] = [
    "the Explorer",
    "Veteran",
    "the Dutiful",
    "the Architect",
    "Beastmaster",
    "Headhunter",
    "Kingslayer",
    "the Untouchable",
    "the Relentless",
    "the Bounty Hunter",
    "Manhunter",
    "the Tamer",
    "Tamer of the East",
    "Tamer of the West",
    "Grand Master Tamer",
    "the Scribe",
    "the Gemcutter",
    "the Bard",
    "Master Builder",
    "the Seeker",
    "the Exalted",
    "the Loremaster",
    "Dungeon Master",
    "Battlemaster",
    "the Wayfarer",
    "Patron of the Wanderers",
    "the Insane",
    "the Walking Arsenal",
    "Jack of All Trades",
];

/// The points earned and the points there are, over every row.
pub fn points(rows: &BTreeMap<u32, Row>) -> (u32, u32) {
    rows.values().fold((0, 0), |(e, t), r| {
        (e + if r.earned { u32::from(r.points) } else { 0 }, t + u32::from(r.points))
    })
}

/// MW2's killstreak rewards by the server's `StreakId` (1 UAV ... 15 Tactical Nuke).
const STREAK_NAMES: [&str; 15] = [
    "UAV",
    "Care Package",
    "Counter-UAV",
    "Sentry Gun",
    "Predator Missile",
    "Precision Airstrike",
    "Harrier Strike",
    "Attack Helicopter",
    "Emergency Airdrop",
    "Pave Low",
    "Stealth Bomber",
    "Chopper Gunner",
    "AC-130",
    "EMP",
    "Tactical Nuke",
];

/// What the server sent, decoded.
#[derive(Debug, PartialEq, Eq)]
pub enum Message {
    Page {
        page: u16,
        pages: u16,
        rows: Vec<Row>,
    },
    Earned(Vec<u32>),
    Progress(Vec<(u32, Vec<u32>)>),
    Unlocks {
        everything: bool,
        held: Vec<Unlock>,
    },
    /// The account's record: (the server's `Stat`, its value) pairs.
    Record(Vec<(u16, u32)>),
}

/// The server's `Stat` ids a record carries (`AchievementWire::kRecordStats`).
const STAT_PLAYER_KILLS: u16 = 1;
const STAT_CREATURE_KILLS: u16 = 2;
const STAT_HEADSHOTS: u16 = 9;
const STAT_DEATHS: u16 = 27;
const STAT_PLAYER_DEATHS: u16 = 28;
const STAT_BEST_PLAYER_STREAK: u16 = 40;

/// The account's kills and deaths as the server counts them (every character, all time).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Record {
    pub player_kills: u32,
    pub creature_kills: u32,
    pub headshots: u32,
    pub deaths: u32,
    pub player_deaths: u32,
    pub best_streak: u32,
}

impl Record {
    /// From a Record message's pairs; a stat it does not know is skipped.
    pub fn from_stats(stats: &[(u16, u32)]) -> Self {
        let mut r = Self::default();
        for &(stat, v) in stats {
            match stat {
                STAT_PLAYER_KILLS => r.player_kills = v,
                STAT_CREATURE_KILLS => r.creature_kills = v,
                STAT_HEADSHOTS => r.headshots = v,
                STAT_DEATHS => r.deaths = v,
                STAT_PLAYER_DEATHS => r.player_deaths = v,
                STAT_BEST_PLAYER_STREAK => r.best_streak = v,
                _ => {}
            }
        }
        r
    }
}

/// MW2's kill/death ratio, two places: kills over deaths, the kills themselves with no deaths.
pub fn kd_ratio(kills: u32, deaths: u32) -> String {
    format!("{:.2}", kills as f64 / f64::from(deaths.max(1)))
}

/// This session's count, as the layer keeps it (crate::streaks).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Session {
    pub kills: u32,
    pub deaths: u32,
    pub best: u32,
}

impl From<Option<(u32, u32, u32)>> for Session {
    fn from(v: Option<(u32, u32, u32)>) -> Self {
        let (kills, deaths, best) = v.unwrap_or_default();
        Self {
            kills,
            deaths,
            best,
        }
    }
}

/// The record page's lines: (heading or label, value), a heading with an empty value. The
/// account's record when the server sent it, then this session's as the layer counted it.
pub fn record_lines(career: Option<&Record>, session: Session) -> Vec<(String, String)> {
    let mut out = Vec::new();
    let mut line = |l: &str, v: String| out.push((l.to_string(), v));
    if let Some(r) = career {
        line("Career", String::new());
        line("Enemy players killed", r.player_kills.to_string());
        line("Killed by enemy players", r.player_deaths.to_string());
        line("K/D ratio", kd_ratio(r.player_kills, r.player_deaths));
        line("Best killstreak", r.best_streak.to_string());
        line("Headshots", r.headshots.to_string());
        line("Creatures killed", r.creature_kills.to_string());
        line("Deaths", r.deaths.to_string());
    }
    line("This session", String::new());
    line("Kills", session.kills.to_string());
    line("Deaths", session.deaths.to_string());
    line("K/D ratio", kd_ratio(session.kills, session.deaths));
    line("Best killstreak", session.best.to_string());
    out
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
    fn unlock(&mut self) -> Option<Unlock> {
        let kind = UnlockKind::from_u8(self.u8()?)?;
        let id = self.u32()?;
        let camo = self.u8()?;
        let attachment = self.str8()?;
        Some(Unlock {
            kind,
            id,
            camo,
            attachment,
        })
    }
    fn row(&mut self) -> Option<Row> {
        let id = self.u32()?;
        let category = self.u8()?;
        let flags = self.u8()?;
        let points = self.u16()?;
        let name = self.str8()?;
        let description = self.str16()?;
        let requirements = (0..self.u8()?)
            .map(|_| {
                Some(Requirement {
                    stat: self.u16()?,
                    key: self.u32()?,
                    current: self.u32()?,
                    needed: self.u32()?,
                })
            })
            .collect::<Option<Vec<_>>>()?;
        let rewards = (0..self.u8()?)
            .map(|_| self.unlock())
            .collect::<Option<Vec<_>>>()?;
        Some(Row {
            id,
            category,
            earned: flags & FLAG_EARNED != 0,
            hidden: flags & FLAG_HIDDEN != 0,
            points,
            name,
            description,
            requirements,
            rewards,
        })
    }
}

/// Read one achievements-channel body.
pub fn decode(body: &[u8]) -> Option<Message> {
    let mut r = Reader { b: body, at: 0 };
    if r.u8()? != VERSION {
        return None;
    }
    let msg = match r.u8()? {
        OP_PAGE => {
            let page = r.u16()?;
            let pages = r.u16()?;
            let n = r.u16()?;
            let rows = (0..n).map(|_| r.row()).collect::<Option<Vec<_>>>()?;
            Message::Page { page, pages, rows }
        }
        OP_EARNED => Message::Earned((0..r.u16()?).map(|_| r.u32()).collect::<Option<_>>()?),
        OP_PROGRESS => Message::Progress(
            (0..r.u16()?)
                .map(|_| {
                    let id = r.u32()?;
                    let n = r.u8()?;
                    Some((id, (0..n).map(|_| r.u32()).collect::<Option<Vec<_>>>()?))
                })
                .collect::<Option<_>>()?,
        ),
        OP_UNLOCKS => {
            let everything = r.u8()? != 0;
            let held = (0..r.u16()?)
                .map(|_| r.unlock())
                .collect::<Option<Vec<_>>>()?;
            Message::Unlocks { everything, held }
        }
        OP_RECORD => Message::Record(
            (0..r.u8()?)
                .map(|_| Some((r.u16()?, r.u32()?)))
                .collect::<Option<_>>()?,
        ),
        _ => return None,
    };
    Some(msg)
}

/// The list as the server last sent it, and the window's state.
#[derive(Resource, Debug, Default)]
pub struct Achievements {
    pub rows: BTreeMap<u32, Row>,
    /// The account's record, once the server sent it.
    pub record: Option<Record>,
    pub open: bool,
    pub tab: u8,
    built: bool,
    dirty: bool,
}

impl Achievements {
    /// The achievement that unlocks something, for Create-a-Class's "locked: ..." line.
    pub fn unlocked_by(
        &self,
        kind: UnlockKind,
        id: u32,
        camo: u8,
        attachment: &str,
    ) -> Option<&Row> {
        self.rows.values().find(|r| {
            r.rewards.iter().any(|u| {
                u.kind == kind && u.id == id && u.camo == camo && u.attachment == attachment
            })
        })
    }

    fn apply(&mut self, msg: Message, unlocks: &mut Unlocks, toasts: &mut Toasts) {
        match msg {
            Message::Page { rows, .. } => {
                for row in rows {
                    self.rows.insert(row.id, row);
                }
            }
            Message::Earned(ids) => {
                for id in ids {
                    let name = self.rows.get_mut(&id).map(|r| {
                        r.earned = true;
                        r.name.clone()
                    });
                    toasts
                        .0
                        .push((name.unwrap_or_else(|| "Achievement".into()), 0.0));
                }
            }
            Message::Progress(entries) => {
                for (id, current) in entries {
                    if let Some(row) = self.rows.get_mut(&id) {
                        for (req, c) in row.requirements.iter_mut().zip(current) {
                            req.current = c;
                        }
                    }
                }
            }
            Message::Unlocks { everything, held } => {
                *unlocks = Unlocks {
                    everything,
                    held: held.into_iter().collect(),
                };
            }
            Message::Record(stats) => self.record = Some(Record::from_stats(&stats)),
        }
        self.dirty = true;
    }
}

/// Achievements earned and not yet shown: the name and how long it has shown.
#[derive(Resource, Debug, Default)]
pub struct Toasts(pub Vec<(String, f32)>);

pub struct AchievementsPlugin;

impl Plugin for AchievementsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Achievements>()
            .init_resource::<Unlocks>()
            .init_resource::<Toasts>()
            .add_systems(
                Update,
                (read, toggle, build_window, refresh, show_toasts)
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

fn read(
    mut channel: MessageReader<FusionChannelReceived>,
    mut ach: ResMut<Achievements>,
    mut unlocks: ResMut<Unlocks>,
    mut toasts: ResMut<Toasts>,
) {
    for FusionChannelReceived(m) in channel.read() {
        if m.channel != CHANNEL_ACHIEVEMENTS {
            continue;
        }
        debug!("benilla-mw2: achievements message, {} bytes", m.body.len());
        match decode(&m.body) {
            Some(msg) => ach.apply(msg, &mut unlocks, &mut toasts),
            None => warn!("benilla-mw2: an achievements message it can't read"),
        }
    }
}

/// Whether the Achievements is open next frame: shut outside the world; asked for (Y, the micro
/// menu, the quest log's Achievements tab), it toggles; otherwise `shut` (Esc, its X, its Quests tab,
/// the quest log taking its place) closes it.
pub fn journal_next(open: bool, in_world: bool, asked: bool, shut: bool) -> bool {
    if !in_world {
        false
    } else if asked {
        !open
    } else if shut {
        false
    } else {
        open
    }
}

/// Hide the stock quest log: the Achievements takes its place.
const HIDE_QUEST_LOG: &str =
    "if QuestLogFrame and QuestLogFrame:IsVisible() then HideUIPanel(QuestLogFrame) end";
/// Back to the quest log from the Achievements's Quests tab.
const SHOW_QUEST_LOG: &str = "if QuestLogFrame then ShowUIPanel(QuestLogFrame) end";

/// The stock quest log's top-left corner as screen fractions from the top-left (`GetLeft`/
/// `GetTop` in scale-1 units over the screen's), shown or not; nil when it has no rect yet.
pub const QUEST_LOG_CORNER_LUA: &str = r#"
local f = QuestLogFrame
if not f or not f:GetLeft() or not f:GetTop() then return nil end
local s, ps = f:GetEffectiveScale(), UIParent:GetEffectiveScale()
local sw, sh = UIParent:GetWidth() * ps, UIParent:GetHeight() * ps
if sw <= 0 or sh <= 0 then return nil end
return f:GetLeft() * s / sw, 1 - f:GetTop() * s / sh
"#;

/// The quest log's frame (`QuestLogFrame.xml`) is a 384x512 texture whose visible panel starts
/// about this many units in from its left; the Achievements's border goes there, its header plate
/// overhanging the top as the quest log's portrait does.
const QUEST_LOG_INSET_UNITS: f32 = 12.0;
/// The stock left UIPanel slot (`QuestLogFrame`'s own anchor, TOPLEFT 0, -104), in UI units.
const QUEST_LOG_SLOT_UNITS: Vec2 = Vec2::new(0.0, 104.0);

/// Where the Achievements's top-left goes (logical pixels): over the quest log's corner when the
/// interface measured it (screen fractions from the top-left), else the stock left-panel slot;
/// `seam` pixels per UI unit.
pub fn journal_seat(corner: Option<Vec2>, viewport: Vec2, seam: f32) -> Vec2 {
    let corner = corner
        .filter(|c| c.is_finite() && c.x >= 0.0 && c.y >= 0.0 && c.x < 1.0 && c.y < 1.0)
        .map_or(QUEST_LOG_SLOT_UNITS * seam, |c| c * viewport);
    corner + Vec2::new(QUEST_LOG_INSET_UNITS * seam, 0.0)
}

#[allow(clippy::too_many_arguments)]
fn toggle(
    input: Res<crate::input::Mw2Input>,
    mut ach: ResMut<Achievements>,
    mut state: ResMut<FpsState>,
    cac: Res<crate::cac_ui::CacUi>,
    mut send: MessageWriter<SendFusionChannel>,
    mut panels: MessageReader<OpenPanel>,
    mut roots: Query<Entity, With<AchRoot>>,
    close: Query<&Interaction, (With<AchClose>, Changed<Interaction>)>,
    quests: Query<&Interaction, (With<QuestsTab>, Changed<Interaction>)>,
    script: Option<NonSend<UiScript>>,
    window: Query<&Window, With<bevy::window::PrimaryWindow>>,
    seam: Option<Res<wow_ui::UiSeam>>,
    mut positions: ResMut<wow_ui::WindowPositions>,
    mut commands: Commands,
) {
    let closed = close.iter().any(|i| *i == Interaction::Pressed);
    let to_quests = quests.iter().any(|i| *i == Interaction::Pressed);
    let asked = panels
        .read()
        .fold(input.achievements, |a, p| a | (p.panel == Panel::Journal));
    let in_world = crate::capture_keep::in_world(state.in_world, "achievements");
    let script = script.filter(|_| in_world);
    // The quest log opened over it (L, its micro button): the two share a place.
    let quest_log = ach.open
        && !to_quests
        && script
            .as_deref()
            .is_some_and(|s| s.frame_visible("QuestLogFrame"));
    let open = journal_next(
        ach.open,
        in_world,
        asked,
        input.escape || closed || to_quests || quest_log,
    ) || crate::capture_keep::keeps("achievements");
    if open && !ach.open {
        // Ask for the list and the record as they stand; the record page first.
        info!("benilla-mw2: Achievements open; asking the server");
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_ACHIEVEMENTS,
            body: vec![VERSION, OP_REQUEST],
        }));
        if ach.tab == 0 {
            ach.tab = RECORD_TAB;
        }
        // Seat it on the quest log's own corner, measured before the quest log steps aside.
        if let Ok(w) = window.single() {
            let viewport = Vec2::new(w.width(), w.height());
            let k = wow_ui::seam_scale(w.height(), seam.as_deref().map_or(0.9, |s| s.ui_scale));
            let corner = script.as_deref().and_then(|s| {
                let (x, y) = s
                    .eval::<(Option<f32>, Option<f32>)>(QUEST_LOG_CORNER_LUA)
                    .ok()?;
                Some(Vec2::new(x?, y?))
            });
            let at = journal_seat(corner, viewport, k);
            info!(
                "benilla-mw2: Achievements seated at {at:?} (quest log corner {corner:?}, viewport \
                 {viewport:?}, {k:.3} px per unit)"
            );
            positions.seat(JOURNAL_KEY, at);
        }
        if let Some(s) = script.as_deref() {
            crate::lua::run(s, HIDE_QUEST_LOG);
            crate::lua::sound(s, "igQuestLogOpen");
        }
    }
    if !open && ach.open {
        if let Some(s) = script.as_deref() {
            if to_quests {
                crate::lua::run(s, SHOW_QUEST_LOG);
            } else if !quest_log {
                crate::lua::sound(s, "igQuestLogClose");
            }
        }
    }
    if open != ach.open {
        ach.open = open;
        ach.dirty = true;
        if !open {
            for e in &mut roots {
                commands.entity(e).despawn();
            }
            ach.built = false;
        }
        // The cursor out and the gun down while it is open (Create-a-Class shares the flag).
        state.panel = open || cac.open;
    }
}

/// How tall the list may grow, in UI units, before it scrolls.
const LIST_HEIGHT: f32 = 380.0;

#[derive(Component)]
struct AchRoot;
/// The window's list: crate::prestige draws the Prestige tab into it.
#[derive(Component)]
pub(crate) struct AchList;
#[derive(Component)]
pub(crate) struct TabButton(u8);
#[derive(Component)]
pub(crate) struct Summary;
/// The window's close button.
#[derive(Component)]
struct AchClose;
/// The tab under the Achievements that goes back to the quest log.
#[derive(Component)]
struct QuestsTab;
/// The Achievements's own tab under it, always the shown one.
#[derive(Component)]
pub(crate) struct JournalTab;

/// The Achievements's window key: its seat is written under it on every open ([`journal_seat`]).
const JOURNAL_KEY: &str = "journal";

/// Where the Achievements sits before a seat is written (tests, no window): the stock left slot.
const JOURNAL_ANCHOR: Anchor = Anchor {
    at: Vec2::ZERO,
    pivot: Vec2::ZERO,
    offset: Vec2::new(QUEST_LOG_INSET_UNITS, 104.0),
};

fn build_window(mut commands: Commands, assets: Res<AssetServer>, mut ach: ResMut<Achievements>) {
    if ach.built || !ach.open {
        return;
    }
    ach.built = true;
    ach.dirty = true;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let spec = WindowSpec {
        key: JOURNAL_KEY,
        title: "Achievements".into(),
        frame: Frame::Dialog,
        width: Some(660.0),
        movable: true,
        anchor: JOURNAL_ANCHOR,
        z: i32::MAX - 1,
    };
    wow_ui::spawn_window(
        &mut commands,
        f,
        spec,
        AchRoot,
        Some(AchClose),
        |root| {
            root.spawn((text(f, "", 12.0, DIM), Summary));
            root.spawn(Node {
                flex_direction: FlexDirection::Row,
                margin: UiRect::vertical(u(6.0)),
                ..default()
            })
            .with_children(|row| {
                // The categories, as a list down the left.
                wow_ui::inset(
                    row,
                    Node {
                        flex_direction: FlexDirection::Column,
                        flex_shrink: 0.0,
                        align_self: AlignSelf::FlexStart,
                        margin: UiRect::right(u(8.0)),
                        padding: UiRect::all(u(3.0)),
                        ..default()
                    },
                    (),
                    |tabs| {
                        for (id, name, child) in CATEGORIES {
                            // Wrath's tree: the arsenal's tabs indented under their heading.
                            tabs.spawn(Node { padding: UiRect::left(u(if child { 10.0 } else { 0.0 })), ..default() })
                                .with_children(|t| {
                                    t.spawn(wow_ui::row_button(f, name, if child { 128.0 } else { 138.0 }, 16.0, TabButton(id)));
                                });
                        }
                    },
                );
                wow_ui::inset(
                    row,
                    Node {
                        flex_direction: FlexDirection::Column,
                        flex_grow: 1.0,
                        min_width: u(0.0),
                        ..default()
                    },
                    (),
                    |c| {
                        wow_ui::scroll_frame(c, LIST_HEIGHT, None, AchList, |_| {});
                    },
                );
            });
        },
        |below| {
            // The quest log's two tabs, as under the quest log itself (crate::micro_menu).
            wow_ui::tab_strip(below, |tabs| {
                tabs.spawn(wow_ui::tab(f, "Quests", QuestsTab));
                tabs.spawn(wow_ui::tab(f, "Achievements", JournalTab));
            });
        },
    );
}

/// A record line, and its value's cell.
#[derive(Component)]
struct RecordRow;
#[derive(Component)]
struct RecordValue;

/// The record's label and value columns, UI units.
const RECORD_LABEL_W: f32 = 210.0;
const RECORD_VALUE_W: f32 = 80.0;

/// A category's icon (a stock one in the install's `Interface\Icons`), shown beside its
/// achievements.
pub fn category_icon(category: u8) -> Option<&'static str> {
    Some(match category {
        1 => "Interface\\Icons\\INV_Weapon_Rifle_01",
        2 => "Interface\\Icons\\INV_Misc_Gear_01",
        3 => "Interface\\Icons\\INV_Fabric_Wool_01",
        4 => "Interface\\Icons\\Spell_Holy_SealOfMight",
        5 => "Interface\\Icons\\Spell_Shadow_DeathScream",
        6 => "Interface\\Icons\\INV_Misc_Bomb_08",
        7 => "Interface\\Icons\\INV_Misc_Bomb_05",
        8 => "Interface\\Icons\\Ability_Hunter_EagleEye",
        9 => "Interface\\Icons\\INV_Misc_Ribbon_01",
        20 => "Interface\\Icons\\INV_Misc_Note_02",
        21 => "Interface\\Icons\\INV_Misc_Book_08",
        22 => "Interface\\Icons\\INV_Misc_Map_01",
        23 => "Interface\\Icons\\INV_BannerPVP_02",
        24 => "Interface\\Icons\\INV_Misc_Head_Dragon_01",
        25 => "Interface\\Icons\\Trade_BlackSmithing",
        26 => "Interface\\Icons\\INV_Misc_Token_ArgentDawn",
        27 => "Interface\\Icons\\INV_Misc_Gift_01",
        28 => "Interface\\Icons\\INV_Weapon_Rifle_07",
        29 => "Interface\\Icons\\Ability_Hunter_BeastTaming",
        30 => "Interface\\Icons\\INV_Misc_Bag_10",
        31 => "Interface\\Icons\\Spell_Holy_ChampionsBond",
        _ => return None,
    })
}

fn reward_text(catalog: Option<&Catalog>, u: &Unlock) -> String {
    let gun = |id: u32| {
        catalog
            .and_then(|c| c.gun(id as u16))
            .map_or_else(|| format!("gun {id}"), crate::arsenal::gun_name)
    };
    let lore = crate::arsenal::arsenal();
    match u.kind {
        UnlockKind::Gun | UnlockKind::Lethal | UnlockKind::Tactical => gun(u.id),
        UnlockKind::Attachment => format!(
            "{} ({})",
            lore.attachment(&u.attachment)
                .map_or(u.attachment.as_str(), |l| l.name.as_str()),
            gun(u.id)
        ),
        UnlockKind::Camo => {
            let name = catalog.and_then(|c| c.camo(u.camo)).map_or_else(
                || format!("camo {}", u.camo),
                |c| {
                    lore.camo(&c.suffix)
                        .map_or(c.name.clone(), |l| l.name.clone())
                },
            );
            format!("{name} camo ({})", gun(u.id))
        }
        UnlockKind::Perk | UnlockKind::Deathstreak => {
            catalog.and_then(|c| c.perk(u.id as u16)).map_or_else(
                || format!("perk {}", u.id),
                |p| {
                    lore.perk(&p.icon)
                        .map_or(p.name.clone(), |l| l.name.clone())
                },
            )
        }
        UnlockKind::Killstreak => lore.streak(u.id).map_or_else(
            || {
                STREAK_NAMES
                    .get((u.id as usize).wrapping_sub(1))
                    .map_or_else(|| format!("killstreak {}", u.id), |n| n.to_string())
            },
            |l| l.name.clone(),
        ),
        UnlockKind::Title => TITLE_NAMES
            .get((u.id as usize).wrapping_sub(1))
            .map_or_else(|| format!("title {}", u.id), |n| format!("the title \"{n}\"")),
        UnlockKind::PerkPro => {
            let name = catalog.and_then(|c| c.perk(u.id as u16)).map_or_else(
                || format!("perk {}", u.id),
                |p| {
                    lore.perk(&p.icon)
                        .map_or(p.name.clone(), |l| l.name.clone())
                },
            );
            format!("{name} Pro")
        }
    }
}

#[allow(clippy::too_many_arguments)]
pub(crate) fn refresh(
    mut commands: Commands,
    assets: Res<AssetServer>,
    mut ach: ResMut<Achievements>,
    catalog: Option<Res<Catalog>>,
    mut lists: Query<(Entity, &mut ScrollPosition), With<AchList>>,
    mut summary: Query<&mut Text, With<Summary>>,
    tabs: Query<(&TabButton, &Interaction), Changed<Interaction>>,
    mut all_tabs: Query<(&TabButton, &mut WowButton), Without<JournalTab>>,
    mut journal_tab: Query<&mut WowButton, (With<JournalTab>, Without<TabButton>)>,
    streak: Option<Res<Streak>>,
    mut session_seen: Local<Option<(u32, u32, u32)>>,
) {
    if !ach.open {
        return;
    }
    for mut b in &mut journal_tab {
        if !b.selected {
            b.selected = true;
        }
    }
    // The record page follows this session's count as it moves.
    let session = streak
        .as_deref()
        .map(|s| (s.total_kills, s.deaths, s.best.max(s.kills)));
    if *session_seen != session {
        *session_seen = session;
        if ach.tab == RECORD_TAB {
            ach.dirty = true;
        }
    }
    for (tab, interaction) in &tabs {
        if *interaction == Interaction::Pressed {
            ach.tab = tab_of(tab.0);
            ach.dirty = true;
            // A new category starts at its top.
            for (_, mut pos) in &mut lists {
                pos.0 = Vec2::ZERO;
            }
        }
    }
    for (tab, mut button) in &mut all_tabs {
        let selected = tab.0 == ach.tab || (tab.0 == ARSENAL_HEADING && (1..=9).contains(&ach.tab));
        if button.selected != selected {
            button.selected = selected;
        }
    }
    if !ach.dirty {
        return;
    }
    let Ok((list, _)) = lists.single() else {
        return;
    };
    ach.dirty = false;
    let font: Handle<Font> = assets.load(FONT);
    let f = &font;
    let earned = ach.rows.values().filter(|r| r.earned).count();
    if let Ok(mut s) = summary.single_mut() {
        **s = if ach.rows.is_empty() {
            "Loading...".into()
        } else {
            let (got, all) = points(&ach.rows);
            format!("{earned} of {} achievements earned  -  {got} of {all} points", ach.rows.len())
        };
    }
    // The Prestige tab is crate::prestige's to draw.
    if ach.tab == crate::prestige::PRESTIGE_TAB {
        commands.entity(list).despawn_related::<Children>();
        return;
    }
    if ach.tab == RECORD_TAB {
        let lines = record_lines(ach.record.as_ref(), Session::from(session));
        commands.entity(list).despawn_related::<Children>();
        commands.entity(list).with_children(|l| {
            for (label, value) in &lines {
                if value.is_empty() {
                    // A heading, as the character frame's stat group titles.
                    wow_ui::section_header(l, f, label);
                    continue;
                }
                // The label in a column of its own, the value in the next: each a set width,
                // so neither is ever squeezed or pushed out of the list.
                l.spawn((
                    RecordRow,
                    Node {
                        flex_direction: FlexDirection::Row,
                        align_items: AlignItems::Center,
                        padding: UiRect::horizontal(u(6.0)),
                        margin: UiRect::vertical(u(1.0)),
                        flex_shrink: 0.0,
                        ..default()
                    },
                ))
                .with_children(|r| {
                    r.spawn((
                        Node {
                            width: u(RECORD_LABEL_W),
                            flex_shrink: 0.0,
                            ..default()
                        },
                        children![text(f, label, 12.0, DIM)],
                    ));
                    r.spawn((
                        RecordValue,
                        Node {
                            width: u(RECORD_VALUE_W),
                            flex_shrink: 0.0,
                            justify_content: JustifyContent::FlexEnd,
                            ..default()
                        },
                        children![text(f, value, 12.0, WHITE)],
                    ));
                });
            }
            if ach.record.is_none() {
                l.spawn(text(
                    f,
                    "Your record is not available on this realm.",
                    11.0,
                    GREY,
                ));
            }
        });
        return;
    }
    // Earned last, then the nearest to done first: what to chase next is on top.
    let mut rows: Vec<&Row> = ach
        .rows
        .values()
        .filter(|r| r.category == ach.tab)
        .collect();
    rows.sort_by(|a, b| {
        a.earned
            .cmp(&b.earned)
            .then(b.progress().total_cmp(&a.progress()))
            .then(a.id.cmp(&b.id))
    });
    commands.entity(list).despawn_related::<Children>();
    commands.entity(list).with_children(|l| {
        for row in &rows {
            let color = if row.earned { GREEN } else { WHITE };
            let rewards: Vec<String> = row
                .rewards
                .iter()
                .map(|u| reward_text(catalog.as_deref(), u))
                .collect();
            l.spawn(Node {
                flex_direction: FlexDirection::Row,
                align_items: AlignItems::FlexStart,
                column_gap: u(8.0),
                flex_shrink: 0.0,
                margin: UiRect::vertical(u(wow_ui::LINE_GAP)),
                padding: UiRect::all(u(4.0)),
                border: UiRect::bottom(px(1)),
                ..default()
            })
            .insert(BorderColor::all(GREY.with_alpha(0.4)))
            .with_children(|line| {
                // The category's icon, lit once earned.
                let icon = category_icon(row.category)
                    .map(|i| assets.load(crate::cac_ui::spell_icon_path(i)));
                line.spawn(wow_ui::icon_frame_lit(
                    icon,
                    30.0,
                    if row.earned { GOLD } else { wow_ui::SLOT_EDGE },
                    row.earned,
                ));
                line.spawn(Node {
                    flex_direction: FlexDirection::Column,
                    flex_grow: 1.0,
                    min_width: u(0.0),
                    ..default()
                })
                .with_children(|r| {
                // Earned: the name in its colour (Friz Quadrata has no tick to put by it), and
                // its points beside it, as Wrath's shield.
                r.spawn(Node { flex_direction: FlexDirection::Row, column_gap: u(8.0), ..default() })
                    .with_children(|h| {
                        h.spawn(text(f, &row.name, 13.0, color));
                        if row.points > 0 {
                            h.spawn(text(f, &format!("{} pts", row.points), 11.0, if row.earned { GOLD } else { DIM }));
                        }
                    });
                r.spawn(text(f, &row.description, 11.0, DIM));
                // The progress bar.
                if !row.earned && !row.requirements.is_empty() {
                    let p = row.progress();
                    let counts: Vec<String> = row
                        .requirements
                        .iter()
                        .map(|q| format!("{}/{}", q.current.min(q.needed), q.needed))
                        .collect();
                    r.spawn(Node {
                        flex_direction: FlexDirection::Row,
                        align_items: AlignItems::Center,
                        margin: UiRect::top(u(2.0)),
                        ..default()
                    })
                    .with_children(|bar| {
                        bar.spawn((
                            Node {
                                width: u(200.0),
                                height: u(9.0),
                                margin: UiRect::right(u(8.0)),
                                border: UiRect::all(px(1)),
                                ..default()
                            },
                            BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.5)),
                            BorderColor::all(GREY.with_alpha(0.6)),
                            children![(
                                Node {
                                    width: percent(p * 100.0),
                                    height: percent(100),
                                    ..default()
                                },
                                BackgroundColor(GOLD),
                            )],
                        ));
                        bar.spawn(text(f, &counts.join("  "), 11.0, DIM));
                    });
                }
                if !rewards.is_empty() {
                    r.spawn(text(
                        f,
                        &format!("Unlocks: {}", rewards.join(", ")),
                        11.0,
                        GOLD,
                    ));
                }
                });
            });
        }
        if rows.is_empty() {
            l.spawn(text(f, "Nothing here yet.", 12.0, GREY));
        }
    });
}

#[derive(Component)]
struct ToastNode;

/// Seconds a toast shows.
const TOAST_SECS: f32 = 4.0;

fn show_toasts(
    mut commands: Commands,
    assets: Res<AssetServer>,
    time: Res<Time>,
    mut toasts: ResMut<Toasts>,
    nodes: Query<Entity, With<ToastNode>>,
    mut sfx: MessageWriter<crate::mw2::sound::Sfx>,
    mut shown: Local<Option<String>>,
) {
    let dt = time.delta_secs();
    let head = toasts.0.first().map(|t| t.0.clone());
    if head != *shown {
        for e in &nodes {
            commands.entity(e).despawn();
        }
        if let Some(name) = &head {
            let font: Handle<Font> = assets.load(FONT);
            let spec = WindowSpec {
                key: "achievement toast",
                title: String::new(),
                frame: Frame::Tooltip,
                width: Some(300.0),
                movable: false,
                anchor: Anchor {
                    at: Vec2::new(0.5, 0.18),
                    pivot: Vec2::new(0.5, 0.0),
                    offset: Vec2::ZERO,
                },
                z: i32::MAX,
            };
            wow_ui::spawn_window(
                &mut commands,
                &font,
                spec,
                ToastNode,
                None::<()>,
                |t| {
                    t.spawn(Node {
                        flex_direction: FlexDirection::Column,
                        align_items: AlignItems::Center,
                        ..default()
                    })
                    .with_children(|c| {
                        c.spawn(text(&font, "Achievement Earned", 12.0, GOLD));
                        c.spawn(text(&font, name, 14.0, WHITE));
                    });
                },
                |_| {},
            );
            sfx.write(crate::mw2::sound::Sfx::new("mp_level_up"));
        }
        *shown = head;
    }
    if let Some(t) = toasts.0.first_mut() {
        t.1 += dt;
        if t.1 >= TOAST_SECS {
            toasts.0.remove(0);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_window_and_its_list_build_and_the_toast_shows() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        world.insert_resource(Achievements {
            open: true,
            tab: 1,
            ..default()
        });
        world.init_resource::<Toasts>();
        world.run_system_once(build_window).unwrap();
        world.run_system_once(refresh).unwrap();
        let mut lists = world.query_filtered::<Entity, With<AchList>>();
        assert_eq!(lists.iter(&world).count(), 1);
        let mut tabs = world.query::<(&TabButton, &WowButton)>();
        let selected: Vec<u8> = tabs
            .iter(&world)
            .filter(|(_, b)| b.selected)
            .map(|(t, _)| t.0)
            .collect();
        // The tab and its heading in the category tree (the arsenal's).
        let mut selected = selected;
        selected.sort_unstable();
        assert_eq!(selected, vec![1, ARSENAL_HEADING]);
        let mut toasts = world.query_filtered::<Entity, With<ToastNode>>();
        world
            .resource_mut::<Toasts>()
            .0
            .push(("First Blood".into(), 0.0));
        world.init_resource::<Time>();
        world.init_resource::<Messages<crate::mw2::sound::Sfx>>();
        world.run_system_once(show_toasts).unwrap();
        assert_eq!(toasts.iter(&world).count(), 1);
    }

    #[test]
    fn the_journal_hangs_the_quest_logs_tabs_and_opens_on_the_record() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = crate::wow_ui::test_world();
        world.insert_resource(Achievements {
            open: true,
            tab: RECORD_TAB,
            record: Some(Record {
                player_kills: 9,
                player_deaths: 3,
                ..default()
            }),
            ..default()
        });
        world.init_resource::<Streak>();
        world.run_system_once(build_window).unwrap();
        world.run_system_once(refresh).unwrap();
        let mut quests = world.query_filtered::<Entity, With<QuestsTab>>();
        assert_eq!(quests.iter(&world).count(), 1, "a Quests tab under it");
        let mut journal = world.query_filtered::<&WowButton, With<JournalTab>>();
        assert!(journal.single(&world).unwrap().selected, "the Achievements tab shown");
        let mut texts = world.query::<&Text>();
        let all: Vec<String> = texts.iter(&world).map(|t| t.0.clone()).collect();
        assert!(all.iter().any(|t| t == "Achievements"), "titled Achievements");
        assert!(all.iter().all(|t| t != "Journal"), "no Journal left");
        assert!(all.iter().any(|t| t == "3.00"), "the career K/D: {all:?}");
    }

    /// Laid out as in the game: every record value sits inside the list, right of its label.
    #[test]
    fn the_record_values_are_laid_out_inside_the_list() {
        let mut app = wow_ui::layout_app();
        app.insert_resource(Achievements {
            open: true,
            tab: RECORD_TAB,
            record: Some(Record {
                player_kills: 9,
                player_deaths: 3,
                creature_kills: 26,
                deaths: 2,
                ..default()
            }),
            ..default()
        });
        app.init_resource::<Streak>();
        app.add_systems(Update, (build_window, refresh).chain());
        for _ in 0..3 {
            app.update();
        }
        let measured = wow_ui::measure_text(&mut app);
        for _ in 0..6 {
            app.update();
        }
        let world = app.world_mut();
        let mut lists = world.query_filtered::<Entity, With<AchList>>();
        let list = lists.single(world).unwrap();
        let view = wow_ui::screen_rect(world, list);
        assert!(view.width() > 200.0, "the list is {view:?}");
        let mut texts = world.query::<(Entity, &Text)>();
        let found: Vec<(Entity, String)> =
            texts.iter(world).map(|(e, t)| (e, t.0.clone())).collect();
        let rect_of = |world: &World, want: &str| {
            let (e, _) = found
                .iter()
                .find(|(_, t)| t == want)
                .unwrap_or_else(|| panic!("no {want} in {found:?}"));
            assert!(wow_ui::drawn(world, *e), "{want} is drawn");
            wow_ui::screen_rect(world, *e)
        };
        for (label, value) in [
            ("Creatures killed", "26"),
            ("Killed by enemy players", "3"),
            ("K/D ratio", "3.00"),
        ] {
            let l = rect_of(world, label);
            let v = rect_of(world, value);
            assert!(
                v.min.x >= view.min.x - 0.5 && v.max.x <= view.max.x + 0.5,
                "{value} at {v:?}, the list {view:?}"
            );
            // Beside its label, on its line, and measured to a width (with a font to hand).
            assert!(v.min.x > l.max.x, "{value} at {v:?} right of {label} at {l:?}");
            assert!((v.center().y - l.center().y).abs() < 4.0, "{value} on {label}'s line");
            if measured {
                assert!(v.width() > 0.0 && l.width() > 0.0, "{value} is measured");
            }
        }
    }

    #[test]
    fn the_journal_sits_on_the_quest_logs_corner() {
        let viewport = Vec2::new(1600.0, 900.0);
        let k = wow_ui::seam_scale(900.0, 0.9);
        // Measured: the corner as a screen fraction, the border inset in.
        let at = journal_seat(Some(Vec2::new(0.0, 0.1219)), viewport, k);
        assert!((at - Vec2::new(12.0 * k, 0.1219 * 900.0)).length() < 0.01, "{at:?}");
        // Not measured (or nonsense): the stock left slot, 104 units down.
        for bad in [None, Some(Vec2::new(f32::NAN, 0.1)), Some(Vec2::new(0.0, 1.5))] {
            let at = journal_seat(bad, viewport, k);
            assert!((at - Vec2::new(12.0, 104.0) * k).length() < 0.01, "{bad:?}: {at:?}");
        }
    }

    /// The corner Lua runs in benilla's VM and reads a quest log at the stock slot.
    #[test]
    fn the_quest_log_corner_is_measured_from_its_frame() {
        let mut s = UiScript::new().expect("a VM");
        s.set_screen_size(1024.0, 768.0);
        s.run(
            r#"
            local root = CreateFrame("Frame", "UIParent")
            root:SetWidth(1024) root:SetHeight(768)
            root:SetPoint("BOTTOMLEFT", nil, "BOTTOMLEFT", 0, 0)
            UIParent = root
            local q = CreateFrame("Frame", "QuestLogFrame", UIParent)
            q:SetWidth(384) q:SetHeight(512)
            q:SetPoint("TOPLEFT", UIParent, "TOPLEFT", 0, -104)
            q:Hide()
            "#,
        )
        .unwrap();
        s.resolve();
        let (x, y) = s
            .eval::<(Option<f32>, Option<f32>)>(QUEST_LOG_CORNER_LUA)
            .unwrap();
        let (x, y) = (x.expect("x"), y.expect("y"));
        assert!(x.abs() < 1e-4, "{x}");
        assert!((y - 104.0 / 768.0).abs() < 1e-3, "{y}");
    }

    #[test]
    fn the_journal_toggles_and_gives_way_to_the_quest_log() {
        assert!(journal_next(false, true, true, false)); // Y, the micro menu or the tab
        assert!(!journal_next(true, true, true, false)); // asked again: closes
        assert!(!journal_next(true, true, false, true)); // Esc, X, Quests, the quest log
        assert!(journal_next(true, true, false, false)); // stays
        assert!(!journal_next(false, false, true, false)); // not in the world
    }

    #[test]
    fn the_record_reads_as_the_server_writes_it_and_shows_kd() {
        // The server's journal-record test: version 2, op 6, then (u16 stat, u32 value) pairs.
        let mut body = vec![2, 6, 3];
        for (stat, v) in [(1u16, 12u32), (28, 4), (40, 7)] {
            body.extend(stat.to_le_bytes());
            body.extend(v.to_le_bytes());
        }
        let Some(Message::Record(stats)) = decode(&body) else {
            panic!("a record");
        };
        let r = Record::from_stats(&stats);
        assert_eq!((r.player_kills, r.player_deaths, r.best_streak), (12, 4, 7));
        assert_eq!(decode(&body[..body.len() - 1]), None, "short");
        assert_eq!(kd_ratio(12, 4), "3.00");
        assert_eq!(kd_ratio(5, 0), "5.00"); // no deaths: the kills
        assert_eq!(kd_ratio(0, 0), "0.00");
        let session = Session {
            kills: 3,
            deaths: 2,
            best: 3,
        };
        let lines = record_lines(Some(&r), session);
        let get = |label: &str| {
            lines
                .iter()
                .filter(|(l, _)| l == label)
                .map(|(_, v)| v.as_str())
                .collect::<Vec<_>>()
        };
        assert_eq!(get("K/D ratio"), vec!["3.00", "1.50"]); // the career's, then the session's
        assert_eq!(get("Best killstreak"), vec!["7", "3"]);
        assert!(get("Score").is_empty(), "no points are kept");
        // Without a server, the session alone.
        assert_eq!(record_lines(None, session)[0].0, "This session");
    }

    #[test]
    fn earned_ids_read_as_the_server_writes_them() {
        // The server's AchievementTests: version 2, op 3, count 2, the ids little-endian.
        let body = [2, 3, 2, 0, 0x00, 0x02, 0x00, 0x01, 7, 0, 0, 0];
        assert_eq!(decode(&body), Some(Message::Earned(vec![0x0100_0200, 7])));
        assert_eq!(decode(&[1, 3, 0, 0]), None); // the old version
    }

    fn page_body() -> Vec<u8> {
        let mut b = vec![2, 2, 0, 0, 1, 0, 1, 0];
        b.extend(7u32.to_le_bytes());
        b.push(1); // Weapons
        b.push(0); // not earned, not hidden
        b.extend(10u16.to_le_bytes()); // points
        b.push(3);
        b.extend(b"UMP");
        b.extend(5u16.to_le_bytes());
        b.extend(b"kills");
        b.push(1);
        b.extend(7u16.to_le_bytes());
        b.extend(6u32.to_le_bytes());
        b.extend(10u32.to_le_bytes());
        b.extend(25u32.to_le_bytes());
        b.push(1);
        b.push(1); // a gun
        b.extend(42u32.to_le_bytes());
        b.push(0);
        b.push(0);
        b
    }

    #[test]
    fn a_page_reads_and_its_progress_follows() {
        let Some(Message::Page { page, pages, rows }) = decode(&page_body()) else {
            panic!("a page");
        };
        assert_eq!((page, pages, rows.len()), (0, 1, 1));
        assert_eq!(rows[0].name, "UMP");
        assert_eq!(rows[0].points, 10);
        assert_eq!(rows[0].requirements[0].needed, 25);
        assert!((rows[0].progress() - 0.4).abs() < 1e-6);
        let mut ach = Achievements::default();
        let (mut unlocks, mut toasts) = (Unlocks::default(), Toasts::default());
        ach.apply(
            Message::Page { page, pages, rows },
            &mut unlocks,
            &mut toasts,
        );
        let mut progress = vec![2, 4, 1, 0];
        progress.extend(7u32.to_le_bytes());
        progress.push(1);
        progress.extend(20u32.to_le_bytes());
        ach.apply(decode(&progress).unwrap(), &mut unlocks, &mut toasts);
        assert_eq!(ach.rows[&7].requirements[0].current, 20);
        assert_eq!(
            ach.unlocked_by(UnlockKind::Gun, 42, 0, "").map(|r| r.id),
            Some(7)
        );
        ach.apply(Message::Earned(vec![7]), &mut unlocks, &mut toasts);
        assert!(ach.rows[&7].earned && toasts.0.len() == 1);
    }

    #[test]
    fn unlocks_gate_everything_but_empty_slots() {
        let mut body = vec![2, 5, 0, 2, 0];
        body.extend([1]);
        body.extend(5u32.to_le_bytes());
        body.extend([0, 0]);
        body.extend([2]);
        body.extend(5u32.to_le_bytes());
        body.extend([0, 4]);
        body.extend(b"acog");
        let Some(Message::Unlocks { everything, held }) = decode(&body) else {
            panic!("unlocks");
        };
        let u = Unlocks {
            everything,
            held: held.into_iter().collect(),
        };
        assert!(u.gun(5) && !u.gun(6) && u.gun(0));
        assert!(u.attachment(5, "acog") && !u.attachment(5, "reflex") && u.attachment(6, ""));
        assert!(!u.perk(1) && u.perk(0));
        assert!(Unlocks::default().gun(99)); // no word from a server: all open
    }
}
