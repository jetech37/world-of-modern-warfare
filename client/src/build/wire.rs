//! Building's messages on the fusion channel (`CHANNEL_BUILD`), the server's `Build.h` wire.
//! Little-endian.

use bevy::math::Vec3;

use super::pieces::PieceType;

pub const OP_PLACE: u8 = 1;
pub const OP_REMOVE: u8 = 2;
pub const OP_PIECES: u8 = 10;
pub const OP_REMOVED: u8 = 11;
pub const OP_HEALTH: u8 = 12;
pub const OP_REFUSED: u8 = 13;
pub const OP_STATUS: u8 = 14;
pub const OP_SENTRY_FIRE: u8 = 15;
/// Construction (the server's `Construction.h`): every piece's recipe, and the player's skill.
pub const OP_RECIPES: u8 = 16;
pub const OP_SKILL: u8 = 17;
/// The plot underfoot (the server's `Plots.h`).
pub const OP_PLOT: u8 = 18;
pub const PIECE_MINE: u8 = 0x01;
pub const PIECE_OPEN: u8 = 0x02;
/// Bits 2 and 3 of a piece's flags: its reinforcement tier (the server's `Build.h`).
pub const PIECE_REINFORCE_SHIFT: u8 = 2;
/// Bits 4-6 of a door's or portal's flags: its lock (the server's `Locks.h`), [`LockMode`].
pub const PIECE_LOCK_SHIFT: u8 = 4;
/// Bit 7: a paid plot keeps the piece, so it doesn't decay (the server's `Maintenance.h`).
pub const PIECE_KEPT: u8 = 0x80;
pub const OP_TOGGLE: u8 = 3;

/// Locks (the server's `Locks.h`): ours, set a lock and try a code; the server's, a code wanted.
pub const OP_SET_LOCK: u8 = 4;
pub const OP_TRY_CODE: u8 = 5;
pub const OP_ASK_CODE: u8 = 19;
/// Portals (the server's `Portals.h`): ours, open its list and step through; the server's, the
/// list.
pub const OP_PORTAL_ASK: u8 = 6;
pub const OP_PORTAL_TRAVEL: u8 = 7;
pub const OP_PORTAL_MENU: u8 = 20;
/// Furniture (the server's `Furniture.h`): F on a piece, the stash worked, a page shown, the
/// stash as it stands.
pub const OP_FURNITURE_USE: u8 = 8;
pub const OP_STASH: u8 = 9;
pub const OP_FURNITURE_PAGE: u8 = 21;
pub const OP_STASH_VIEW: u8 = 22;
pub const STASH_PUT: u8 = 1;
pub const STASH_TAKE: u8 = 2;

/// F on a piece of furniture.
pub fn encode_furniture_use(id: u32) -> Vec<u8> {
    let mut o = vec![OP_FURNITURE_USE];
    o.extend_from_slice(&id.to_le_bytes());
    o
}

/// Put bag `bag` slot `slot` in the stash (`count` 0: the whole stack).
pub fn encode_stash_put(piece: u32, bag: u8, slot: u8, count: u16) -> Vec<u8> {
    let mut o = vec![OP_STASH];
    o.extend_from_slice(&piece.to_le_bytes());
    o.extend_from_slice(&[STASH_PUT, bag, slot]);
    o.extend_from_slice(&count.to_le_bytes());
    o
}

/// Take stash slot `index` out (`count` 0: all of it).
pub fn encode_stash_take(piece: u32, index: u16, count: u16) -> Vec<u8> {
    let mut o = vec![OP_STASH];
    o.extend_from_slice(&piece.to_le_bytes());
    o.extend_from_slice(&[STASH_TAKE, index as u8, (index >> 8) as u8]);
    o.extend_from_slice(&count.to_le_bytes());
    o
}

/// A page the server shows: a book's, a dummy's report, a stash's word.
#[derive(Clone, Debug, PartialEq)]
pub struct FurniturePage {
    pub piece: u32,
    pub kind: u8,
    pub title: String,
    pub lines: Vec<String>,
}

/// A plot's stash and what the bags hold that may go in.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct StashView {
    pub piece: u32,
    pub slots: u16,
    /// (item, count)
    pub stash: Vec<(u32, u16)>,
    /// (bag, slot, item, count)
    pub bags: Vec<(u8, u8, u32, u16)>,
}
/// The portal's own destination for the player's homestead (`Portals::kHomestead`).
pub const PORTAL_HOMESTEAD: u8 = 100;
/// A code: 4 to 8 digits (`Locks::kMinCode`, `kMaxCode`).
pub const CODE_MIN: usize = 4;
pub const CODE_MAX: usize = 8;

/// A door's or portal's lock, as the server numbers them.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum LockMode {
    Unlocked = 0,
    Owner = 1,
    /// The default for a new door or portal.
    #[default]
    Party = 2,
    Guild = 3,
    Code = 4,
}

impl LockMode {
    pub const ALL: [LockMode; 5] = [
        LockMode::Unlocked,
        LockMode::Owner,
        LockMode::Party,
        LockMode::Guild,
        LockMode::Code,
    ];

    pub fn from_u8(v: u8) -> LockMode {
        match v {
            0 => LockMode::Unlocked,
            1 => LockMode::Owner,
            3 => LockMode::Guild,
            4 => LockMode::Code,
            _ => LockMode::Party,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            LockMode::Unlocked => "Unlocked",
            LockMode::Owner => "Owner only",
            LockMode::Party => "Owner and party",
            LockMode::Guild => "Owner and guild",
            LockMode::Code => "Code",
        }
    }

    /// Who it lets in, for the dialog and the bar.
    pub fn blurb(self) -> &'static str {
        match self {
            LockMode::Unlocked => "Anyone may use it.",
            LockMode::Owner => "Only you (and your other characters).",
            LockMode::Party => "You and your party or raid.",
            LockMode::Guild => "You and your guild.",
            LockMode::Code => "Anyone who knows the code (4 to 8 digits).",
        }
    }
}

/// Whether `code` is one a lock takes: 4 to 8 digits.
pub fn valid_code(code: &str) -> bool {
    (CODE_MIN..=CODE_MAX).contains(&code.len()) && code.bytes().all(|b| b.is_ascii_digit())
}

/// A code's digits as they go on the wire: digits only, at most [`CODE_MAX`].
fn code_digits(code: &str) -> String {
    code.chars()
        .filter(char::is_ascii_digit)
        .take(CODE_MAX)
        .collect()
}

/// Op 4: set a lock. `code` only for [`LockMode::Code`] (empty keeps the code it has).
pub fn encode_set_lock(piece: u32, mode: LockMode, code: &str) -> Vec<u8> {
    let code = code_digits(code);
    let mut o = vec![OP_SET_LOCK];
    o.extend_from_slice(&piece.to_le_bytes());
    o.push(mode as u8);
    o.push(code.len() as u8);
    o.extend_from_slice(code.as_bytes());
    o
}

/// Op 5: a code tried at a lock.
pub fn encode_try_code(piece: u32, code: &str) -> Vec<u8> {
    let code = code_digits(code);
    let mut o = vec![OP_TRY_CODE];
    o.extend_from_slice(&piece.to_le_bytes());
    o.push(code.len() as u8);
    o.extend_from_slice(code.as_bytes());
    o
}

/// Op 6: a portal's list, asked for.
pub fn encode_portal_ask(piece: u32) -> Vec<u8> {
    let mut o = vec![OP_PORTAL_ASK];
    o.extend_from_slice(&piece.to_le_bytes());
    o
}

/// Op 7: through a portal to a destination of its list.
pub fn encode_portal_travel(piece: u32, destination: u8) -> Vec<u8> {
    let mut o = vec![OP_PORTAL_TRAVEL];
    o.extend_from_slice(&piece.to_le_bytes());
    o.push(destination);
    o
}

/// A portal's list as the server sends it: where it goes for this player, and how long the
/// hearthstone's cooldown (which it shares) still runs.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct PortalMenu {
    pub piece: u32,
    pub cooldown_secs: u32,
    /// (destination id, name)
    pub choices: Vec<(u8, String)>,
}

/// Reinforcement, on the world channel (the server's `Construction.h`): our request to put a
/// piece up a tier, and the tiers the server offers.
pub const OP_REINFORCE: u8 = 50;
pub const OP_REINFORCEMENTS: u8 = 51;

/// The refusals Construction and the plots added.
pub const REFUSED_SKILL_TOO_LOW: u8 = 9;
pub const REFUSED_NOT_LEARNED: u8 = 10;
pub const REFUSED_PROTECTED: u8 = 11;
pub const REFUSED_NOT_REINFORCEABLE: u8 = 12;
pub const REFUSED_MAX_REINFORCEMENT: u8 = 13;
/// It would shut a player inside it (the builder, or someone standing there): Build.h `Traps`.
pub const REFUSED_TRAPS: u8 = 14;
/// The builder can't see where it would go (through a wall, a hill or someone's base).
pub const REFUSED_OUT_OF_SIGHT: u8 = 15;
/// Too near a vendor, a trainer, a flight master, a Plot Steward or the like.
pub const REFUSED_CROWDS_NPC: u8 = 16;
/// Locks and portals (the server's `Locks.h`, `Portals.h`).
pub const REFUSED_LOCKED: u8 = 17;
pub const REFUSED_WRONG_CODE: u8 = 18;
pub const REFUSED_TOO_MANY_TRIES: u8 = 19;
pub const REFUSED_COOLING_DOWN: u8 = 20;
pub const REFUSED_PORTAL_LIMIT: u8 = 21;
/// No kit of the piece made yet (`Refusal::NoKit`): Construction's trade window makes them.
pub const REFUSED_NO_KIT: u8 = 22;

/// A reinforcement tier: the Construction skill it asks, what it does to a piece (health times
/// `health_x100` / 100, bullets and blows cut by `bullet_cut` percent) and its materials.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReinforceTier {
    pub tier: u8,
    pub skill: u16,
    pub health_x100: u16,
    pub bullet_cut: u8,
    /// (item entry, count)
    pub materials: Vec<(u32, u16)>,
}

/// The tiers' names, as the server's.
pub fn reinforce_name(tier: u8) -> &'static str {
    match tier {
        1 => "Braced",
        2 => "Plated",
        3 => "Fortified",
        _ => "Unreinforced",
    }
}

pub fn encode_reinforce(id: u32) -> Vec<u8> {
    let mut o = vec![OP_REINFORCE];
    o.extend_from_slice(&id.to_le_bytes());
    o
}

/// The server's tiers (world channel op 51).
pub fn decode_reinforcements(body: &[u8]) -> Option<Vec<ReinforceTier>> {
    let mut r = Reader(body);
    if r.u8()? != OP_REINFORCEMENTS {
        return None;
    }
    let n = r.u8()?;
    let mut v = Vec::with_capacity(usize::from(n));
    for _ in 0..n {
        let tier = r.u8()?;
        let skill = r.u16()?;
        let health_x100 = r.u16()?;
        let bullet_cut = r.u8()?;
        let m = r.u8()?;
        let mut materials = Vec::with_capacity(usize::from(m));
        for _ in 0..m {
            materials.push((r.u32()?, r.u16()?));
        }
        v.push(ReinforceTier {
            tier,
            skill,
            health_x100,
            bullet_cut,
            materials,
        });
    }
    r.0.is_empty().then_some(v)
}

/// A piece's recipe: the Construction skill it unlocks at and its colours (orange from
/// `required`, then yellow, green and gray, as vanilla's recipes), and the materials it costs.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Recipe {
    pub kind: PieceType,
    pub required: u16,
    pub yellow: u16,
    pub green: u16,
    pub gray: u16,
    /// (item entry, count)
    pub materials: Vec<(u32, u16)>,
}

/// A recipe's colour at a skill, as the server's `Construction::ColorOf`: red is out of reach,
/// gray teaches nothing.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SkillColor {
    Red,
    Orange,
    Yellow,
    Green,
    Gray,
}

impl Recipe {
    pub fn color(&self, skill: u16) -> SkillColor {
        if skill < self.required {
            SkillColor::Red
        } else if skill >= self.gray {
            SkillColor::Gray
        } else if skill >= self.green {
            SkillColor::Green
        } else if skill >= self.yellow {
            SkillColor::Yellow
        } else {
            SkillColor::Orange
        }
    }
}

/// The Construction tiers, as the server numbers them (0 not learned).
pub fn tier_name(tier: u8) -> &'static str {
    match tier {
        1 => "Apprentice",
        2 => "Journeyman",
        3 => "Expert",
        4 => "Artisan",
        _ => "Not learned",
    }
}

/// The plot the player stands on, as the server's `Plots::EncodePlot` tells it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlotInfo {
    /// 0: no plot here.
    pub id: u32,
    /// 1 small, 2 standard, 3 large.
    pub size: u8,
    /// `PLOT_FOR_SALE`, `PLOT_PAID`, `PLOT_OVERDUE` or 3 lapsed.
    pub standing: u8,
    /// What the player may do there: 0 nothing, then the `ACCESS_` levels.
    pub access: u8,
    /// Its corners in WoW coordinates, (x0, y0) to (x1, y1).
    pub bounds: [f32; 4],
    /// Unix seconds.
    pub paid_until: u32,
    pub permits: u8,
}

pub const PLOT_FOR_SALE: u8 = 0;
pub const PLOT_PAID: u8 = 1;
pub const PLOT_OVERDUE: u8 = 2;
pub const ACCESS_GUEST: u8 = 1;
pub const ACCESS_BUILDER: u8 = 2;
pub const ACCESS_OWNER: u8 = 3;
pub const ACCESS_GM: u8 = 4;

/// A piece as the server sends it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WirePiece {
    pub id: u32,
    pub kind: PieceType,
    pub pos: Vec3,
    pub yaw: f32,
    /// What is left of its health, 0..1.
    pub health: f32,
    pub support: f32,
    pub mine: bool,
    /// A door stands open.
    pub open: bool,
    /// Its reinforcement tier, 0 to 3.
    pub reinforcement: u8,
    /// A door's or portal's lock (anything else says none: Unlocked).
    pub lock: LockMode,
    /// A paid plot keeps it from decaying.
    pub kept: bool,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ServerMsg {
    Pieces(Vec<WirePiece>),
    /// (id, cause: 0 taken down, 1 destroyed, 2 collapsed)
    Removed(Vec<(u32, u8)>),
    /// (id, health 0..1, support 0..1)
    Health(Vec<(u32, f32, f32)>),
    Refused(u8),
    Status {
        owned: u16,
        max: u16,
        free: bool,
    },
    /// A sentry's round at a target.
    SentryFire {
        piece: u32,
        target: u64,
        hit: bool,
    },
    Recipes(Vec<Recipe>),
    /// The player's Construction: tier (0 not learned), skill, and the tier's cap.
    Skill {
        tier: u8,
        skill: u16,
        cap: u16,
    },
    Plot(PlotInfo),
    /// A lock wants a code: show the keypad (`portal`: it is a portal's).
    AskCode {
        piece: u32,
        portal: bool,
    },
    PortalMenu(PortalMenu),
    FurniturePage(FurniturePage),
    Stash(StashView),
}

/// What a refusal means, for the build bar.
pub fn refusal_text(reason: u8) -> &'static str {
    match reason {
        1 => "Nothing holds it up there: build it on the ground, a floor or a wall",
        2 => "Something is already there",
        3 => "You have built all you may: take something down first",
        4 => "Too far away",
        5 => "Not enough materials",
        6 => "That is not yours",
        8 => "Not in the middle of a fight",
        REFUSED_SKILL_TOO_LOW => "Your Construction skill is too low for that",
        REFUSED_NOT_LEARNED => "Learn Construction from a Construction Trainer first",
        REFUSED_PROTECTED => {
            "Protected: only the plot's owner and those they permit may do that here"
        }
        REFUSED_NOT_REINFORCEABLE => "That can't be reinforced",
        REFUSED_MAX_REINFORCEMENT => "Already reinforced as far as it goes",
        REFUSED_TRAPS => "Someone is standing where it would go: they'd be shut inside",
        REFUSED_OUT_OF_SIGHT => "You can't see where it would go: build only what you can see",
        REFUSED_CROWDS_NPC => {
            "Too close to someone the world needs (a vendor, trainer, flight master...): keep 6 yards clear"
        }
        REFUSED_LOCKED => "Locked: its owner keeps it shut to you",
        REFUSED_WRONG_CODE => "Wrong code",
        REFUSED_TOO_MANY_TRIES => "Too many wrong codes: wait a little before trying again",
        REFUSED_COOLING_DOWN => {
            "Not yet: portals and the courier's way back share your hearthstone's cooldown"
        }
        REFUSED_PORTAL_LIMIT => "You may keep two portals, and a homestead holds one",
        REFUSED_NO_KIT => "You have no kit of that: make one at Construction's trade window (Professions)",
        _ => "You can't build that here",
    }
}

pub fn encode_place(kind: PieceType, pos: Vec3, yaw: f32) -> Vec<u8> {
    let mut o = vec![OP_PLACE, kind.0];
    for f in [pos.x, pos.y, pos.z, yaw] {
        o.extend_from_slice(&f.to_le_bytes());
    }
    o
}

pub fn encode_toggle(id: u32) -> Vec<u8> {
    let mut o = vec![OP_TOGGLE];
    o.extend_from_slice(&id.to_le_bytes());
    o
}

pub fn encode_remove(id: u32) -> Vec<u8> {
    let mut o = vec![OP_REMOVE];
    o.extend_from_slice(&id.to_le_bytes());
    o
}

struct Reader<'a>(&'a [u8]);

impl Reader<'_> {
    fn take<const N: usize>(&mut self) -> Option<[u8; N]> {
        let (head, rest) = self.0.split_at_checked(N)?;
        self.0 = rest;
        head.try_into().ok()
    }
    fn u8(&mut self) -> Option<u8> {
        self.take::<1>().map(|b| b[0])
    }
    fn u16(&mut self) -> Option<u16> {
        self.take().map(u16::from_le_bytes)
    }
    fn u32(&mut self) -> Option<u32> {
        self.take().map(u32::from_le_bytes)
    }
    fn f32(&mut self) -> Option<f32> {
        self.take().map(f32::from_le_bytes)
    }
    /// A length byte and that many bytes of text.
    fn str8(&mut self) -> Option<String> {
        let n = usize::from(self.u8()?);
        let (head, rest) = self.0.split_at_checked(n)?;
        self.0 = rest;
        Some(String::from_utf8_lossy(head).into_owned())
    }
}

// ── The Construction window and the plots near (world channel, the server's `Homestead.h`) ──

/// Ours: the window as it stands (opened from the HUD), and a button pressed.
pub const OP_HOMESTEAD_ASK: u8 = 52;
pub const OP_HOMESTEAD_ACT: u8 = 53;
/// The server's: the window, and the claimed plots near.
pub const OP_HOMESTEAD_WINDOW: u8 = 54;
pub const OP_NEAR_PLOTS: u8 = 55;

/// What a window message asks of the client (`Homestead::Open`).
pub const HOME_REFRESH: u8 = 0;
pub const HOME_OPEN_HUD: u8 = 1;
pub const HOME_OPEN_NPC: u8 = 2;
/// Who opened it (`Homestead::Npc`).
pub const NPC_NONE: u8 = 0;
pub const NPC_STEWARD: u8 = 1;
pub const NPC_COURIER: u8 = 2;
pub const NPC_TRAINER: u8 = 3;
/// A Plot Steward's permit line (`Homestead::Action::Permit`).
pub const ACT_PERMIT: u8 = 10;
/// How the status line reads (`Homestead::Tone`).
pub const TONE_INFO: u8 = 1;
pub const TONE_DONE: u8 = 2;
pub const TONE_ERROR: u8 = 3;
/// A plot near: whose it is to us (`Homestead::Relation`).
pub const NEAR_MINE: u8 = 1;
pub const NEAR_PERMITTED: u8 = 2;
pub const NEAR_CLAIMED: u8 = 3;

/// A button of the NPC's: its action and argument, its price in copper, whether it may be
/// pressed now (else `note` says why), whether it asks first, a recipe's colour (`hue`: 0
/// normal, then orange, yellow, green, gray, red) and its two lines.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct HomeService {
    pub action: u8,
    pub arg: u32,
    pub cost: u32,
    pub enabled: bool,
    pub confirm: bool,
    pub hue: u8,
    pub label: String,
    pub note: String,
}

/// The player's plot as the window shows it (id 0: none).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct HomePlot {
    pub id: u32,
    pub size: u8,
    pub standing: u8,
    pub map: u16,
    pub bounds: [f32; 4],
    pub paid_until: u32,
    /// Copper a week.
    pub upkeep: u32,
    pub permits: u8,
    pub pieces: u16,
    /// "in Azshara Crater".
    pub place: String,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct HomeWindow {
    pub open: u8,
    pub npc: u8,
    pub npc_name: String,
    pub tone: u8,
    pub message: String,
    pub plot: HomePlot,
    /// Items packed from a released or lapsed plot, waiting at a steward.
    pub reclaim: u32,
    pub services: Vec<HomeService>,
}

/// A claimed plot near, for the world's markers.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct NearPlot {
    pub id: u32,
    /// x0, y0, x1, y1 (WoW).
    pub bounds: [f32; 4],
    pub relation: u8,
}

pub fn encode_home_ask() -> Vec<u8> {
    vec![OP_HOMESTEAD_ASK]
}

pub fn encode_home_act(action: u8, arg: u32) -> Vec<u8> {
    let mut o = vec![OP_HOMESTEAD_ACT, action];
    o.extend_from_slice(&arg.to_le_bytes());
    o
}

/// The server's `Homestead::EncodeWindow`.
pub fn decode_home_window(body: &[u8]) -> Option<HomeWindow> {
    let mut r = Reader(body);
    if r.u8()? != OP_HOMESTEAD_WINDOW {
        return None;
    }
    let open = r.u8()?;
    let npc = r.u8()?;
    let npc_name = r.str8()?;
    let tone = r.u8()?;
    let message = r.str8()?;
    let plot = HomePlot {
        id: r.u32()?,
        size: r.u8()?,
        standing: r.u8()?,
        map: r.u16()?,
        bounds: [r.f32()?, r.f32()?, r.f32()?, r.f32()?],
        paid_until: r.u32()?,
        upkeep: r.u32()?,
        permits: r.u8()?,
        pieces: r.u16()?,
        place: r.str8()?,
    };
    let reclaim = r.u32()?;
    let n = r.u8()?;
    let mut services = Vec::with_capacity(usize::from(n));
    for _ in 0..n {
        let action = r.u8()?;
        let arg = r.u32()?;
        let cost = r.u32()?;
        let flags = r.u8()?;
        let hue = r.u8()?;
        services.push(HomeService {
            action,
            arg,
            cost,
            enabled: flags & 1 != 0,
            confirm: flags & 2 != 0,
            hue,
            label: r.str8()?,
            note: r.str8()?,
        });
    }
    r.0.is_empty().then_some(HomeWindow {
        open,
        npc,
        npc_name,
        tone,
        message,
        plot,
        reclaim,
        services,
    })
}

/// The server's `Homestead::EncodeNearPlots`.
pub fn decode_near_plots(body: &[u8]) -> Option<Vec<NearPlot>> {
    let mut r = Reader(body);
    if r.u8()? != OP_NEAR_PLOTS {
        return None;
    }
    let n = r.u8()?;
    let mut v = Vec::with_capacity(usize::from(n));
    for _ in 0..n {
        v.push(NearPlot {
            id: r.u32()?,
            bounds: [r.f32()?, r.f32()?, r.f32()?, r.f32()?],
            relation: r.u8()?,
        });
    }
    r.0.is_empty().then_some(v)
}

/// Copper as the money frame counts it: (gold, silver, copper).
pub fn coins(copper: u32) -> (u32, u32, u32) {
    (copper / 10_000, (copper % 10_000) / 100, copper % 100)
}

pub fn decode(body: &[u8]) -> Option<ServerMsg> {
    let mut r = Reader(body);
    let msg = match r.u8()? {
        OP_PIECES => {
            let n = r.u16()?;
            let mut v = Vec::with_capacity(usize::from(n));
            for _ in 0..n {
                let id = r.u32()?;
                let kind = PieceType::from_u8(r.u8()?);
                let pos = Vec3::new(r.f32()?, r.f32()?, r.f32()?);
                let yaw = r.f32()?;
                let health = f32::from(r.u16()?) / 1000.0;
                let support = f32::from(r.u8()?) / 255.0;
                let flags = r.u8()?;
                if let Some(kind) = kind {
                    v.push(WirePiece {
                        id,
                        kind,
                        pos,
                        yaw,
                        health,
                        support,
                        mine: flags & PIECE_MINE != 0,
                        open: flags & PIECE_OPEN != 0,
                        reinforcement: (flags >> PIECE_REINFORCE_SHIFT) & 3,
                        lock: LockMode::from_u8((flags >> PIECE_LOCK_SHIFT) & 7),
                        kept: flags & PIECE_KEPT != 0,
                    });
                }
            }
            ServerMsg::Pieces(v)
        }
        OP_REMOVED => {
            let n = r.u16()?;
            let mut v = Vec::with_capacity(usize::from(n));
            for _ in 0..n {
                v.push((r.u32()?, r.u8()?));
            }
            ServerMsg::Removed(v)
        }
        OP_HEALTH => {
            let n = r.u16()?;
            let mut v = Vec::with_capacity(usize::from(n));
            for _ in 0..n {
                let id = r.u32()?;
                let health = f32::from(r.u16()?) / 1000.0;
                let support = f32::from(r.u8()?) / 255.0;
                v.push((id, health, support));
            }
            ServerMsg::Health(v)
        }
        OP_REFUSED => ServerMsg::Refused(r.u8()?),
        OP_SENTRY_FIRE => {
            let piece = r.u32()?;
            let lo = u64::from(r.u32()?);
            let hi = u64::from(r.u32()?);
            ServerMsg::SentryFire {
                piece,
                target: lo | hi << 32,
                hit: r.u8()? != 0,
            }
        }
        OP_STATUS => ServerMsg::Status {
            owned: r.u16()?,
            max: r.u16()?,
            free: r.u8()? != 0,
        },
        OP_RECIPES => {
            let n = r.u16()?;
            let mut v = Vec::with_capacity(usize::from(n));
            for _ in 0..n {
                let kind = PieceType(r.u8()?);
                let (required, yellow, green, gray) = (r.u16()?, r.u16()?, r.u16()?, r.u16()?);
                let count = r.u8()?;
                let mut materials = Vec::with_capacity(usize::from(count));
                for _ in 0..count {
                    materials.push((r.u32()?, r.u16()?));
                }
                v.push(Recipe {
                    kind,
                    required,
                    yellow,
                    green,
                    gray,
                    materials,
                });
            }
            ServerMsg::Recipes(v)
        }
        OP_SKILL => ServerMsg::Skill {
            tier: r.u8()?,
            skill: r.u16()?,
            cap: r.u16()?,
        },
        OP_PLOT => ServerMsg::Plot(PlotInfo {
            id: r.u32()?,
            size: r.u8()?,
            standing: r.u8()?,
            access: r.u8()?,
            bounds: [r.f32()?, r.f32()?, r.f32()?, r.f32()?],
            paid_until: r.u32()?,
            permits: r.u8()?,
        }),
        OP_ASK_CODE => ServerMsg::AskCode {
            piece: r.u32()?,
            portal: r.u8()? != 0,
        },
        OP_PORTAL_MENU => {
            let piece = r.u32()?;
            let cooldown_secs = r.u32()?;
            let n = r.u8()?;
            let mut choices = Vec::with_capacity(usize::from(n));
            for _ in 0..n {
                let id = r.u8()?;
                choices.push((id, r.str8()?));
            }
            ServerMsg::PortalMenu(PortalMenu {
                piece,
                cooldown_secs,
                choices,
            })
        }
        OP_FURNITURE_PAGE => {
            let piece = r.u32()?;
            let kind = r.u8()?;
            let title = r.str8()?;
            let n = r.u8()?;
            let mut lines = Vec::with_capacity(usize::from(n));
            for _ in 0..n {
                let len = usize::from(r.u16()?);
                let (head, rest) = r.0.split_at_checked(len)?;
                r.0 = rest;
                lines.push(String::from_utf8_lossy(head).into_owned());
            }
            ServerMsg::FurniturePage(FurniturePage {
                piece,
                kind,
                title,
                lines,
            })
        }
        OP_STASH_VIEW => {
            let piece = r.u32()?;
            let slots = r.u16()?;
            let n = r.u16()?;
            let mut stash = Vec::with_capacity(usize::from(n));
            for _ in 0..n {
                stash.push((r.u32()?, r.u16()?));
            }
            let m = r.u8()?;
            let mut bags = Vec::with_capacity(usize::from(m));
            for _ in 0..m {
                bags.push((r.u8()?, r.u8()?, r.u32()?, r.u16()?));
            }
            ServerMsg::Stash(StashView {
                piece,
                slots,
                stash,
                bags,
            })
        }
        _ => return None,
    };
    r.0.is_empty().then_some(msg)
}

/// The name of a material the recipes use (the server's world DB items); others by number.
pub fn item_name(item: u32) -> String {
    let name = match item {
        91001 => "Rough Lumber",
        91002 => "Sturdy Lumber",
        91003 => "Hardwood Plank",
        91004 => "Ironwood Plank",
        91010 => "Woodcutting Axe",
        91020 => "Copper Nails",
        91021 => "Bronze Hinge",
        91022 => "Iron Bracket",
        91023 => "Reinforced Plate",
        91024 => "Fortified Plate",
        91025 => "Sentry Barrel Assembly",
        91026 => "Sentry Targeting Unit",
        91027 => "Sentry Tripod",
        2318 => "Light Leather",
        2589 => "Linen Cloth",
        2592 => "Wool Cloth",
        2835 => "Rough Stone",
        2836 => "Coarse Stone",
        2838 => "Heavy Stone",
        2840 => "Copper Bar",
        2841 => "Bronze Bar",
        3575 => "Iron Bar",
        3860 => "Mithril Bar",
        4305 => "Bolt of Silk Cloth",
        4339 => "Bolt of Mageweave",
        7912 => "Solid Stone",
        12359 => "Thorium Bar",
        12363 => "Arcane Crystal",
        12365 => "Dense Stone",
        14344 => "Large Brilliant Shard",
        _ => {
            // A salvaged material (the server's `Construction::SalvageTable`): its fresh one's
            // name.
            return match fresh_of(item) {
                Some(fresh) => format!("Salvaged {}", item_name(fresh)),
                None => format!("item {item}"),
            };
        }
    };
    name.to_string()
}

/// Each fresh material and its salvaged item, as the server's `Construction::SalvageTable`:
/// what comes back out of a piece builds like the fresh goods but teaches no Construction.
pub const SALVAGE: [(u32, u32); 26] = [
    (91001, 93600),
    (91002, 93601),
    (91003, 93602),
    (91004, 93603),
    (91020, 93604),
    (91021, 93605),
    (91022, 93606),
    (91023, 93607),
    (91024, 93608),
    (2835, 93610),
    (2836, 93611),
    (2838, 93612),
    (7912, 93613),
    (12365, 93614),
    (2840, 93615),
    (2841, 93616),
    (3575, 93617),
    (3860, 93618),
    (12359, 93619),
    (2589, 93620),
    (2592, 93621),
    (4305, 93622),
    (4339, 93623),
    (2318, 93624),
    (12363, 93625),
    (14344, 93626),
];

/// The fresh material a salvaged item stands for.
pub fn fresh_of(item: u32) -> Option<u32> {
    SALVAGE.iter().find(|(_, s)| *s == item).map(|(f, _)| *f)
}

/// What the build menu and tooltips say of salvage.
pub const SALVAGE_NOTE: &str = "Fresh materials can teach Construction; salvaged ones (from pieces taken down) build it but teach nothing.";

/// A recipe's materials as the menu lists them: "4 Rough Lumber, 2 Copper Nails".
pub fn materials_text(materials: &[(u32, u16)]) -> String {
    materials
        .iter()
        .map(|(item, count)| format!("{count} {}", item_name(*item)))
        .collect::<Vec<_>>()
        .join(", ")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recipes_decode_as_the_server_writes_them() {
        // The server's the_build_menu_wire_round_trips: the wood floor's recipe alone, at skill 1
        // (orange to 26, yellow to 51, green to 76), 4 Rough Lumber (91001).
        let mut b = vec![OP_RECIPES, 1, 0, 1, 1, 0, 26, 0, 51, 0, 76, 0, 1];
        b.extend_from_slice(&91001u32.to_le_bytes());
        b.extend_from_slice(&4u16.to_le_bytes());
        assert_eq!(b.len(), 1 + 2 + 1 + 8 + 1 + 6);
        assert_eq!(b[13], (91001u32 & 0xFF) as u8);
        assert_eq!(b[17], 4);
        let Some(ServerMsg::Recipes(v)) = decode(&b) else {
            panic!("decode");
        };
        let floor = Recipe {
            kind: PieceType::WoodFloor,
            required: 1,
            yellow: 26,
            green: 51,
            gray: 76,
            materials: vec![(91001, 4)],
        };
        assert_eq!(v, vec![floor.clone()]);
        assert_eq!(floor.color(0), SkillColor::Red);
        assert_eq!(floor.color(1), SkillColor::Orange);
        assert_eq!(floor.color(26), SkillColor::Yellow);
        assert_eq!(floor.color(51), SkillColor::Green);
        assert_eq!(floor.color(76), SkillColor::Gray);
        assert_eq!(materials_text(&floor.materials), "4 Rough Lumber");
        // A byte short, or one over, is no message.
        assert!(decode(&b[..b.len() - 1]).is_none());
        b.push(0);
        assert!(decode(&b).is_none());
    }

    #[test]
    fn the_skill_decodes() {
        // The server's EncodeSkill(Tier::Journeyman, 120).
        assert_eq!(
            decode(&[OP_SKILL, 2, 120, 0, 150, 0]),
            Some(ServerMsg::Skill {
                tier: 2,
                skill: 120,
                cap: 150
            })
        );
        assert_eq!(tier_name(2), "Journeyman");
        assert!(decode(&[OP_SKILL, 2, 120, 0, 150]).is_none());
    }

    #[test]
    fn the_plot_decodes() {
        // The server's the_plot_wire_tells_the_client_where_it_stands: plot 1, standard, paid,
        // the viewer a builder, x 1000 to 1040 and y 2000 to 2040, two permits.
        let mut b = vec![OP_PLOT, 1, 0, 0, 0, 2, PLOT_PAID, ACCESS_BUILDER];
        for f in [1000.0f32, 2000.0, 1040.0, 2040.0] {
            b.extend_from_slice(&f.to_le_bytes());
        }
        b.extend_from_slice(&1_700_604_800u32.to_le_bytes());
        b.push(2);
        assert_eq!(b.len(), 1 + 4 + 3 + 16 + 4 + 1);
        assert_eq!(&b[16..20], &1040.0f32.to_le_bytes());
        assert_eq!(
            decode(&b),
            Some(ServerMsg::Plot(PlotInfo {
                id: 1,
                size: 2,
                standing: PLOT_PAID,
                access: ACCESS_BUILDER,
                bounds: [1000.0, 2000.0, 1040.0, 2040.0],
                paid_until: 1_700_604_800,
                permits: 2,
            }))
        );
        // Nowhere: id 0 and zeros, the same length.
        let none = [vec![OP_PLOT], vec![0; 28]].concat();
        assert!(matches!(decode(&none), Some(ServerMsg::Plot(p)) if p.id == 0));
    }

    fn str8(b: &mut Vec<u8>, s: &str) {
        b.push(s.len() as u8);
        b.extend_from_slice(s.as_bytes());
    }

    #[test]
    fn the_homestead_window_decodes_as_the_server_writes_it() {
        // The server's EncodeWindow: opened by a steward, an error said, plot 301 (small, paid,
        // map 37, x 150 to 174, y 0 to 24), 41 pieces, 5 items waiting, one greyed button that
        // asks first (pay four weeks, 80s, green).
        let mut b = vec![OP_HOMESTEAD_WINDOW, HOME_OPEN_NPC, NPC_STEWARD];
        str8(&mut b, "Warden Oakhallow");
        b.push(TONE_ERROR);
        str8(&mut b, "You already hold a homestead.");
        b.extend_from_slice(&301u32.to_le_bytes());
        b.extend_from_slice(&[1, 1]);
        b.extend_from_slice(&37u16.to_le_bytes());
        for f in [150.0f32, 0.0, 174.0, 24.0] {
            b.extend_from_slice(&f.to_le_bytes());
        }
        b.extend_from_slice(&1_700_604_800u32.to_le_bytes());
        b.extend_from_slice(&2000u32.to_le_bytes());
        b.push(2);
        b.extend_from_slice(&41u16.to_le_bytes());
        str8(&mut b, "in Azshara Crater");
        b.extend_from_slice(&5u32.to_le_bytes());
        b.push(1);
        b.push(5);
        b.extend_from_slice(&4u32.to_le_bytes());
        b.extend_from_slice(&8000u32.to_le_bytes());
        b.push(2);
        b.push(3);
        str8(&mut b, "Pay four weeks of upkeep");
        str8(&mut b, "4 weeks");
        let w = decode_home_window(&b).expect("decodes");
        assert_eq!(
            (w.open, w.npc, w.tone),
            (HOME_OPEN_NPC, NPC_STEWARD, TONE_ERROR)
        );
        assert_eq!(w.npc_name, "Warden Oakhallow");
        assert_eq!(w.plot.id, 301);
        assert_eq!(w.plot.bounds, [150.0, 0.0, 174.0, 24.0]);
        assert_eq!((w.plot.map, w.plot.pieces, w.plot.upkeep), (37, 41, 2000));
        assert_eq!(w.plot.place, "in Azshara Crater");
        assert_eq!(w.reclaim, 5);
        assert_eq!(
            w.services,
            vec![HomeService {
                action: 5,
                arg: 4,
                cost: 8000,
                enabled: false,
                confirm: true,
                hue: 3,
                label: "Pay four weeks of upkeep".into(),
                note: "4 weeks".into(),
            }]
        );
        assert!(decode_home_window(&b[..b.len() - 1]).is_none());
        b.push(0);
        assert!(decode_home_window(&b).is_none());
        assert_eq!(encode_home_ask(), vec![OP_HOMESTEAD_ASK]);
        assert_eq!(
            encode_home_act(9, 0xFFFF_FFFE),
            vec![OP_HOMESTEAD_ACT, 9, 0xFE, 0xFF, 0xFF, 0xFF]
        );
        assert_eq!(coins(12_345), (1, 23, 45));
    }

    #[test]
    fn the_plots_near_decode() {
        // The server's nearby_claimed_plots_are_marked_the_players_own_first: count, then each
        // plot's id, bounds and relation, 21 bytes.
        let mut b = vec![OP_NEAR_PLOTS, 2];
        for (id, x0, rel) in [(1u32, 1000.0f32, NEAR_MINE), (101, 1200.0, NEAR_CLAIMED)] {
            b.extend_from_slice(&id.to_le_bytes());
            for f in [x0, 2000.0, x0 + 40.0, 2040.0] {
                b.extend_from_slice(&f.to_le_bytes());
            }
            b.push(rel);
        }
        assert_eq!(b.len(), 2 + 2 * 21);
        let v = decode_near_plots(&b).expect("decodes");
        assert_eq!(v.len(), 2);
        assert_eq!(v[0].relation, NEAR_MINE);
        assert_eq!(v[1].bounds, [1200.0, 2000.0, 1240.0, 2040.0]);
        assert_eq!(decode_near_plots(&[OP_NEAR_PLOTS, 0]), Some(vec![]));
        assert!(decode_near_plots(&b[..b.len() - 1]).is_none());
    }

    #[test]
    fn the_new_refusals_say_why() {
        assert!(refusal_text(REFUSED_SKILL_TOO_LOW).contains("skill"));
        assert!(refusal_text(REFUSED_NOT_LEARNED).contains("Construction Trainer"));
        assert!(refusal_text(REFUSED_PROTECTED).contains("Protected"));
        assert!(refusal_text(REFUSED_TRAPS).contains("shut inside"));
        assert!(refusal_text(REFUSED_OUT_OF_SIGHT).contains("can't see"));
        assert!(refusal_text(REFUSED_CROWDS_NPC).contains("6 yards"));
    }

    #[test]
    fn a_place_request_is_eighteen_bytes() {
        let b = encode_place(PieceType::WoodWall, Vec3::new(1.0, 2.0, 3.0), 0.5);
        assert_eq!(b.len(), 18);
        assert_eq!(&b[..2], &[OP_PLACE, 2]);
        assert_eq!(&b[6..10], &2.0f32.to_le_bytes());
        assert_eq!(encode_remove(7), vec![OP_REMOVE, 7, 0, 0, 0]);
    }

    #[test]
    fn pieces_decode_as_the_server_writes_them() {
        // The server's the_wire_round_trips: one sandbag line, mine, at 123 of 1500 health.
        let mut b = vec![OP_PIECES, 1, 0];
        b.extend_from_slice(&7u32.to_le_bytes());
        b.push(12);
        for f in [1.5f32, -2.0, 30.25, 1.0] {
            b.extend_from_slice(&f.to_le_bytes());
        }
        b.extend_from_slice(&82u16.to_le_bytes());
        b.push(255);
        b.push(PIECE_MINE);
        let Some(ServerMsg::Pieces(v)) = decode(&b) else {
            panic!("decode");
        };
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].kind, PieceType::Sandbags);
        assert!(v[0].mine && (v[0].health - 0.082).abs() < 1e-6 && v[0].support == 1.0);
        // A byte short, or one over, is no message.
        assert!(decode(&b[..b.len() - 1]).is_none());
        b.push(0);
        assert!(decode(&b).is_none());
    }

    #[test]
    fn a_pieces_reinforcement_rides_in_its_flags() {
        let mut b = vec![OP_PIECES, 1, 0];
        b.extend_from_slice(&9u32.to_le_bytes());
        b.push(10);
        for f in [0.0f32, 0.0, 0.0, 0.0] {
            b.extend_from_slice(&f.to_le_bytes());
        }
        b.extend_from_slice(&500u16.to_le_bytes());
        b.push(255);
        b.push(PIECE_MINE | (2 << PIECE_REINFORCE_SHIFT));
        let Some(ServerMsg::Pieces(v)) = decode(&b) else {
            panic!("decode");
        };
        assert_eq!((v[0].reinforcement, v[0].mine, v[0].open), (2, true, false));
    }

    #[test]
    fn the_reinforcement_tiers_decode_and_a_request_is_five_bytes() {
        // The server's EncodeReinforcements: count, then tier, skill, health x100, cut, materials.
        let mut b = vec![OP_REINFORCEMENTS, 1, 1];
        b.extend_from_slice(&150u16.to_le_bytes());
        b.extend_from_slice(&200u16.to_le_bytes());
        b.push(25);
        b.push(1);
        b.extend_from_slice(&91022u32.to_le_bytes());
        b.extend_from_slice(&4u16.to_le_bytes());
        let tiers = decode_reinforcements(&b).expect("decodes");
        assert_eq!(
            tiers,
            vec![ReinforceTier {
                tier: 1,
                skill: 150,
                health_x100: 200,
                bullet_cut: 25,
                materials: vec![(91022, 4)],
            }]
        );
        assert!(decode_reinforcements(&b[..b.len() - 1]).is_none());
        assert_eq!(encode_reinforce(7), vec![OP_REINFORCE, 7, 0, 0, 0]);
        assert_eq!(reinforce_name(3), "Fortified");
    }

    #[test]
    fn the_short_messages_decode() {
        assert_eq!(decode(&[OP_REFUSED, 5]), Some(ServerMsg::Refused(5)));
        assert_eq!(
            decode(&[OP_STATUS, 12, 0, 88, 2, 1]),
            Some(ServerMsg::Status {
                owned: 12,
                max: 600,
                free: true
            })
        );
        assert_eq!(
            decode(&[OP_REMOVED, 1, 0, 9, 0, 0, 0, 2]),
            Some(ServerMsg::Removed(vec![(9, 2)]))
        );
        assert!(decode(&[99]).is_none());
        // The server's sentries_and_bedrolls_are_pieces bytes.
        let mut s = vec![OP_SENTRY_FIRE, 9, 0, 0, 0];
        s.extend_from_slice(&0x1122334455667788u64.to_le_bytes());
        s.push(1);
        assert_eq!(
            decode(&s),
            Some(ServerMsg::SentryFire {
                piece: 9,
                target: 0x1122334455667788,
                hit: true
            })
        );
    }

    #[test]
    fn a_doors_lock_rides_in_its_flags() {
        // The server's a_lockable_pieces_mode_rides_in_its_flags: a code lock (4) on a door of ours.
        let mut b = vec![OP_PIECES, 1, 0];
        b.extend_from_slice(&5u32.to_le_bytes());
        b.push(PieceType::WoodDoor.0);
        for f in [0.0f32, 0.0, 0.0, 0.0] {
            b.extend_from_slice(&f.to_le_bytes());
        }
        b.extend_from_slice(&1000u16.to_le_bytes());
        b.push(255);
        b.push(PIECE_MINE | PIECE_OPEN | (4 << PIECE_LOCK_SHIFT));
        let Some(ServerMsg::Pieces(v)) = decode(&b) else {
            panic!("decode");
        };
        assert_eq!(v[0].lock, LockMode::Code);
        assert!(v[0].mine && v[0].open && v[0].reinforcement == 0);
        // No bits: unlocked; the default for a new door is the owner's party.
        assert_eq!(LockMode::from_u8(0), LockMode::Unlocked);
        assert_eq!(LockMode::default(), LockMode::Party);
        for m in LockMode::ALL {
            assert_eq!(LockMode::from_u8(m as u8), m);
            assert!(!m.name().is_empty() && !m.blurb().is_empty());
        }
    }

    #[test]
    fn lock_and_portal_messages_are_the_servers() {
        // The server's lock_messages_round_trip: op 4, piece, mode, the code's length and digits.
        assert_eq!(
            encode_set_lock(0x0102_0304, LockMode::Code, "123456"),
            vec![
                OP_SET_LOCK,
                4,
                3,
                2,
                1,
                4,
                6,
                b'1',
                b'2',
                b'3',
                b'4',
                b'5',
                b'6'
            ]
        );
        assert_eq!(
            encode_set_lock(9, LockMode::Guild, ""),
            vec![OP_SET_LOCK, 9, 0, 0, 0, 3, 0]
        );
        // Never more than 8 digits, never anything but digits.
        assert_eq!(
            encode_set_lock(1, LockMode::Code, "12a34567890").len(),
            7 + 8
        );
        assert_eq!(
            encode_try_code(77, "4711"),
            vec![OP_TRY_CODE, 77, 0, 0, 0, 4, b'4', b'7', b'1', b'1']
        );
        assert!(valid_code("1234") && valid_code("12345678"));
        assert!(!valid_code("123") && !valid_code("123456789") && !valid_code("12a4"));
        assert_eq!(
            decode(&[OP_ASK_CODE, 77, 0, 0, 0, 1]),
            Some(ServerMsg::AskCode {
                piece: 77,
                portal: true
            })
        );
        assert_eq!(encode_portal_ask(1), vec![OP_PORTAL_ASK, 1, 0, 0, 0]);
        assert_eq!(
            encode_portal_travel(12, PORTAL_HOMESTEAD),
            vec![OP_PORTAL_TRAVEL, 12, 0, 0, 0, PORTAL_HOMESTEAD]
        );
        // The server's portal_messages_round_trip: piece, cooldown, count, each id and name.
        let mut m = vec![OP_PORTAL_MENU];
        m.extend_from_slice(&44u32.to_le_bytes());
        m.extend_from_slice(&1800u32.to_le_bytes());
        m.push(2);
        m.push(1);
        str8(&mut m, "Stormwind");
        m.push(PORTAL_HOMESTEAD);
        str8(&mut m, "My homestead");
        assert_eq!(
            decode(&m),
            Some(ServerMsg::PortalMenu(PortalMenu {
                piece: 44,
                cooldown_secs: 1800,
                choices: vec![
                    (1, "Stormwind".into()),
                    (PORTAL_HOMESTEAD, "My homestead".into())
                ],
            }))
        );
        assert!(decode(&m[..m.len() - 1]).is_none());
        // The new refusals say why.
        assert!(refusal_text(REFUSED_LOCKED).contains("Locked"));
        assert!(refusal_text(REFUSED_WRONG_CODE).contains("Wrong code"));
        assert!(refusal_text(REFUSED_TOO_MANY_TRIES).contains("wait"));
        assert!(refusal_text(REFUSED_COOLING_DOWN).contains("hearthstone"));
        assert!(refusal_text(REFUSED_PORTAL_LIMIT).contains("two portals"));
    }

    #[test]
    fn salvaged_materials_name_their_fresh_ones() {
        assert_eq!(item_name(93600), "Salvaged Rough Lumber");
        assert_eq!(item_name(93617), "Salvaged Iron Bar");
        assert_eq!(item_name(93625), "Salvaged Arcane Crystal");
        assert_eq!(fresh_of(93606), Some(91022));
        assert_eq!(fresh_of(91022), None);
        // Each once, in the server's range.
        for (i, (f, s)) in SALVAGE.iter().enumerate() {
            assert!((93600..=93649).contains(s));
            assert!(SALVAGE[i + 1..].iter().all(|(g, t)| g != f && t != s));
            assert!(!item_name(*f).starts_with("item "), "{f}");
        }
        assert!(SALVAGE_NOTE.contains("teach"));
    }
}
