//! Azeroth Warfare's own world packets, beside 1.12's (whose table ends at 827): the client's shot
//! batches and stance/state, and the server's hit, kill and streak events. The server side is
//! `src/fusion` in azeroth-warfare-server; both sides test against the same frozen byte vectors
//! (`tests/data/fusion_packets.txt`, a copy of the server's `src/fusion/vectors/packets.txt`).
//! Little-endian throughout.

use anyhow::{bail, Result};

use crate::world::WorldWriter;

pub const CMSG_FUSION_SHOTS: u16 = 828;
pub const SMSG_FUSION_EVENTS: u16 = 829;
pub const CMSG_FUSION_STATE: u16 = 830;
/// Create-a-Class: the classes to save and the one to equip, and the server's answer.
pub const CMSG_FUSION_LOADOUT: u16 = 831;
pub const SMSG_FUSION_LOADOUT: u16 = 832;
/// A general channel both ways: a channel byte and an opaque body, so a feature built on top
/// (building, the world events) adds its messages without a new opcode here.
pub const CMSG_FUSION_CHANNEL: u16 = 833;
pub const SMSG_FUSION_CHANNEL: u16 = 834;
/// A channel message's body is at most this long.
pub const MAX_CHANNEL_BODY: usize = 8192;

/// The channels.
pub const CHANNEL_BUILD: u8 = 1;
pub const CHANNEL_WORLD: u8 = 2;

pub const VERSION: u8 = 1;
/// `CMSG_FUSION_SHOTS` has its own: version 2 added each pull's seed and spread.
pub const SHOTS_VERSION: u8 = 2;
/// One batch carries at most this many shots (the client sends 20 batches a second).
pub const MAX_SHOTS_PER_BATCH: usize = 16;
pub const MAX_EVENTS_PER_PACKET: usize = 32;

pub const SHOT_ADS: u8 = 0x01;
pub const SHOT_CLAIMS_HEAD: u8 = 0x02;
pub const SHOT_AIRBORNE: u8 = 0x04;
/// The round came from the left gun of an akimbo pair: the server keeps each gun's magazine.
/// (0x08 is the server's C4 detonate.)
pub const SHOT_LEFT: u8 = 0x10;

pub const STATE_AIMING: u8 = 0x01;
/// Ask for the free-for-all flag on.
pub const STATE_FFA_ON: u8 = 0x02;
/// Ask for it off (it drops after 5 minutes out of combat).
pub const STATE_FFA_OFF: u8 = 0x04;
pub const STATE_RELOADING: u8 = 0x08;
/// Buy special rounds from the quartermaster; only honoured while resting (a city or an inn).
pub const STATE_REARM: u8 = 0x10;
/// MW2's mantle and knife lunge requests (the server checks and moves the player).
pub const STATE_MANTLE: u8 = 0x20;
pub const STATE_LUNGE: u8 = 0x40;
/// One round from the reserve into the magazine: a shell-by-shell reload sends one per shell, so
/// an interrupted reload leaves the rounds already loaded.
pub const STATE_RELOAD_ONE: u8 = 0x80;

pub const EVENT_HEADSHOT: u8 = 0x01;
pub const EVENT_FATAL: u8 = 0x02;
pub const EVENT_ASSIST: u8 = 0x04;
/// On `StreakEarned`: the streak's progress, `amount` in tenths of a kill, not a reward.
pub const EVENT_PROGRESS: u8 = 0x08;
/// On `Ammo`: the left gun's magazine of an akimbo pair.
pub const EVENT_LEFT: u8 = 0x10;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum Stance {
    #[default]
    Stand = 0,
    Crouch = 1,
    Prone = 2,
    Slide = 3,
}

impl Stance {
    fn from_u8(v: u8) -> Option<Self> {
        Some(match v {
            0 => Self::Stand,
            1 => Self::Crouch,
            2 => Self::Prone,
            3 => Self::Slide,
            _ => return None,
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum Gait {
    Still = 0,
    Walk = 1,
    #[default]
    Run = 2,
    Sprint = 3,
    Backpedal = 4,
}

impl Gait {
    fn from_u8(v: u8) -> Option<Self> {
        Some(match v {
            0 => Self::Still,
            1 => Self::Walk,
            2 => Self::Run,
            3 => Self::Sprint,
            4 => Self::Backpedal,
            _ => return None,
        })
    }
}

/// One shot as the client saw it: the server re-casts it against its own hitboxes, rewound to
/// `time_ms`, and only then deals damage.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Shot {
    /// The shooter's view time on the server clock.
    pub time_ms: u32,
    pub weapon_id: u16,
    pub flags: u8,
    pub stance: Stance,
    /// The unit the client's ray hit, 0 for none.
    pub target_guid: u64,
    /// Raw WoW yards, z up.
    pub origin: [f32; 3],
    pub direction: [f32; 3],
    /// The pull's pellet seed: each pellet's direction is [`pellet_direction`] of it.
    pub seed: u32,
    /// The cone the pellets scatter in (half-angle, degrees); the server holds it to the gun's
    /// own least.
    pub spread_deg: f32,
}

/// A pellet's random word, the same on client and server: `lowbias32` of the seed mixed with
/// the pellet's index.
pub fn pellet_hash(seed: u32, index: u32) -> u32 {
    let mut x = seed ^ index.wrapping_mul(0x9E37_79B9);
    x ^= x >> 16;
    x = x.wrapping_mul(0x7feb_352d);
    x ^= x >> 15;
    x = x.wrapping_mul(0x846c_a68b);
    x ^= x >> 16;
    x
}

/// Pellet `index`'s direction: MW2's `Bullet_Endpos`, a point uniform in the disc of radius
/// `tan(spread)` one unit along `dir` (WoW axes, z up; `dir` a unit vector).
pub fn pellet_direction(dir: [f32; 3], seed: u32, index: u32, spread_deg: f32) -> [f32; 3] {
    if spread_deg <= 0.0 {
        return dir;
    }
    let h = pellet_hash(seed, index);
    let u = (h & 0xFFFF) as f32 / 65536.0;
    let v = (h >> 16) as f32 / 65536.0;
    let r = spread_deg.to_radians().tan() * u.sqrt();
    let theta = v * std::f32::consts::TAU;
    let cross = |a: [f32; 3], b: [f32; 3]| {
        [
            a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0],
        ]
    };
    let norm = |a: [f32; 3]| {
        let l = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt();
        if l > 0.0 {
            a.map(|c| c / l)
        } else {
            a
        }
    };
    let side = cross(dir, [0.0, 0.0, 1.0]);
    let right = if side.iter().map(|c| c * c).sum::<f32>() > 1e-8 {
        norm(side)
    } else {
        [1.0, 0.0, 0.0]
    };
    let up = cross(right, dir);
    let (a, b) = (r * theta.cos(), r * theta.sin());
    norm([
        dir[0] + right[0] * a + up[0] * b,
        dir[1] + right[1] * a + up[1] * b,
        dir[2] + right[2] * a + up[2] * b,
    ])
}

#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct State {
    pub stance: Stance,
    pub gait: Gait,
    pub flags: u8,
    pub held_weapon_id: u16,
    /// The loaded special round (0 basic).
    pub loaded_ammo: u8,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum EventKind {
    /// You hit `guid` for `amount`.
    Hit = 0,
    /// You killed `guid`.
    Kill = 1,
    /// `guid` hit you for `amount`.
    Hurt = 2,
    /// `extra` is the reward id.
    StreakEarned = 3,
    /// A resync: `amount` the magazine, `extra` the reserve.
    Ammo = 4,
    Revived = 5,
    /// You struck `guid` with WoW's damage (a class spell, a technique): the client's
    /// target-on-hit makes it the target.
    Struck = 6,
}

impl EventKind {
    fn from_u8(v: u8) -> Option<Self> {
        Some(match v {
            0 => Self::Hit,
            1 => Self::Kill,
            2 => Self::Hurt,
            3 => Self::StreakEarned,
            4 => Self::Ammo,
            5 => Self::Revived,
            6 => Self::Struck,
            _ => return None,
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Event {
    pub kind: EventKind,
    pub flags: u8,
    pub guid: u64,
    pub amount: u32,
    pub extra: u16,
}

/// `CMSG_FUSION_SHOTS`' body; shots past [`MAX_SHOTS_PER_BATCH`] are dropped.
pub fn encode_shots(shots: &[Shot]) -> Vec<u8> {
    let n = shots.len().min(MAX_SHOTS_PER_BATCH);
    let mut out = Vec::with_capacity(2 + n * 48);
    out.push(SHOTS_VERSION);
    out.push(n as u8);
    for s in &shots[..n] {
        out.extend_from_slice(&s.time_ms.to_le_bytes());
        out.extend_from_slice(&s.weapon_id.to_le_bytes());
        out.push(s.flags);
        out.push(s.stance as u8);
        out.extend_from_slice(&s.target_guid.to_le_bytes());
        for v in s.origin.iter().chain(&s.direction) {
            out.extend_from_slice(&v.to_le_bytes());
        }
        out.extend_from_slice(&s.seed.to_le_bytes());
        out.extend_from_slice(&s.spread_deg.to_le_bytes());
    }
    out
}

/// `CMSG_FUSION_STATE`'s body.
pub fn encode_state(s: &State) -> Vec<u8> {
    let mut out = vec![VERSION, s.stance as u8, s.gait as u8, s.flags];
    out.extend_from_slice(&s.held_weapon_id.to_le_bytes());
    out.push(s.loaded_ammo);
    out
}

/// `SMSG_FUSION_EVENTS`' body (the server writes these; here for tests and the load-test bot).
pub fn encode_events(events: &[Event]) -> Vec<u8> {
    let n = events.len().min(MAX_EVENTS_PER_PACKET);
    let mut out = vec![VERSION, n as u8];
    for e in &events[..n] {
        out.push(e.kind as u8);
        out.push(e.flags);
        out.extend_from_slice(&e.guid.to_le_bytes());
        out.extend_from_slice(&e.amount.to_le_bytes());
        out.extend_from_slice(&e.extra.to_le_bytes());
    }
    out
}

struct Cursor<'a> {
    b: &'a [u8],
}

impl Cursor<'_> {
    fn take<const N: usize>(&mut self) -> Result<[u8; N]> {
        if self.b.len() < N {
            bail!("fusion packet too short");
        }
        let (head, rest) = self.b.split_at(N);
        self.b = rest;
        Ok(head.try_into().expect("split at N"))
    }
    fn u8(&mut self) -> Result<u8> {
        Ok(self.take::<1>()?[0])
    }
    fn u16(&mut self) -> Result<u16> {
        Ok(u16::from_le_bytes(self.take()?))
    }
    fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_le_bytes(self.take()?))
    }
    fn u64(&mut self) -> Result<u64> {
        Ok(u64::from_le_bytes(self.take()?))
    }
    fn f32(&mut self) -> Result<f32> {
        let v = f32::from_le_bytes(self.take()?);
        if !v.is_finite() {
            bail!("fusion packet carries a non-finite float");
        }
        Ok(v)
    }
    fn str(&mut self, max: usize) -> Result<String> {
        let n = self.u8()? as usize;
        if n > max {
            bail!("a {n}-byte string where at most {max} fit");
        }
        if self.b.len() < n {
            bail!("fusion packet too short");
        }
        let (head, rest) = self.b.split_at(n);
        self.b = rest;
        Ok(String::from_utf8_lossy(head).into_owned())
    }
    fn loadout_version(&mut self) -> Result<()> {
        let v = self.u8()?;
        if v != LOADOUT_VERSION {
            bail!("loadout message version {v}, expected {LOADOUT_VERSION}");
        }
        Ok(())
    }
    fn version(&mut self) -> Result<()> {
        let v = self.u8()?;
        if v != VERSION {
            bail!("fusion packet version {v}, expected {VERSION}");
        }
        Ok(())
    }
    fn done(&self) -> Result<()> {
        if !self.b.is_empty() {
            bail!("fusion packet has {} trailing bytes", self.b.len());
        }
        Ok(())
    }
}

pub fn decode_shots(body: &[u8]) -> Result<Vec<Shot>> {
    let mut r = Cursor { b: body };
    let v = r.u8()?;
    if v != SHOTS_VERSION {
        bail!("fusion shots version {v}, expected {SHOTS_VERSION}");
    }
    let n = r.u8()? as usize;
    if n > MAX_SHOTS_PER_BATCH {
        bail!("{n} shots in one batch");
    }
    let mut shots = Vec::with_capacity(n);
    for _ in 0..n {
        let time_ms = r.u32()?;
        let weapon_id = r.u16()?;
        let flags = r.u8()?;
        let Some(stance) = Stance::from_u8(r.u8()?) else {
            bail!("bad stance");
        };
        let target_guid = r.u64()?;
        let origin = [r.f32()?, r.f32()?, r.f32()?];
        let direction = [r.f32()?, r.f32()?, r.f32()?];
        let seed = r.u32()?;
        let spread_deg = r.f32()?;
        shots.push(Shot {
            time_ms,
            weapon_id,
            flags,
            stance,
            target_guid,
            origin,
            direction,
            seed,
            spread_deg,
        });
    }
    r.done()?;
    Ok(shots)
}

pub fn decode_state(body: &[u8]) -> Result<State> {
    let mut r = Cursor { b: body };
    r.version()?;
    let (Some(stance), Some(gait)) = (Stance::from_u8(r.u8()?), Gait::from_u8(r.u8()?)) else {
        bail!("bad stance or gait");
    };
    let flags = r.u8()?;
    let held_weapon_id = r.u16()?;
    let loaded_ammo = r.u8()?;
    r.done()?;
    Ok(State {
        stance,
        gait,
        flags,
        held_weapon_id,
        loaded_ammo,
    })
}

pub fn decode_events(body: &[u8]) -> Result<Vec<Event>> {
    let mut r = Cursor { b: body };
    r.version()?;
    let n = r.u8()? as usize;
    if n > MAX_EVENTS_PER_PACKET {
        bail!("{n} events in one packet");
    }
    let mut events = Vec::with_capacity(n);
    for _ in 0..n {
        let Some(kind) = EventKind::from_u8(r.u8()?) else {
            bail!("bad event kind");
        };
        events.push(Event {
            kind,
            flags: r.u8()?,
            guid: r.u64()?,
            amount: r.u32()?,
            extra: r.u16()?,
        });
    }
    r.done()?;
    Ok(events)
}

/// The loadout messages carry their own version byte (the others keep [`VERSION`]); version 2
/// added the secondary's second attachment (Bling Pro), version 3 each kit's six talent picks,
/// version 4 the primary's third attachment and the perk slots' Pro bits, version 5 the primary's
/// fourth attachment.
pub const LOADOUT_VERSION: u8 = 5;
/// The perk slots' Pro bits ([`ClassLoadout::perk_pro`]): bit 0 the first slot.
pub const PERK_PRO_MASK: u8 = 0x07;
/// Five kits (Create-a-Class classes); 255 is "none".
pub const CLASS_SLOTS: u8 = 5;
pub const NO_CLASS: u8 = 255;
pub const CLASS_NAME_MAX: usize = 24;
/// A kit's talent picks: one option id per tier (levels 10 to 60), 0 none.
pub const KIT_TALENT_TIERS: usize = 6;

/// One kit (a Create-a-Class class): guns by catalog id (0 none), attachments by MW2's id ("" none),
/// camo by catalog id (0 none); perks, equipment, streaks and talent picks by id.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct ClassLoadout {
    pub name: String,
    pub primary: u16,
    pub primary_att1: String,
    pub primary_att2: String,
    pub primary_camo: u8,
    pub secondary: u16,
    pub secondary_att: String,
    /// The secondary's second attachment: Bling Pro's ("" none).
    pub secondary_att2: String,
    pub lethal: u16,
    pub tactical: u16,
    pub perks: [u16; 3],
    pub deathstreak: u16,
    pub streaks: [u16; 3],
    /// The talent picks the class trainers' options fill (none can be slotted yet: the server
    /// refuses any but 0).
    pub talent_picks: [u16; KIT_TALENT_TIERS],
    /// The primary's third attachment ("" none): room for it takes Tinker's Rails twice.
    pub primary_att3: String,
    /// Which perk slots play their perk's Pro: bit 0 `perks[0]`, bit 1 `perks[1]`, bit 2
    /// `perks[2]` ([`PERK_PRO_MASK`]); the server checks each against the account's earned Pros.
    pub perk_pro: u8,
    /// The primary's fourth attachment ("" none): room for it takes Tinker's Rails in all three
    /// perk slots.
    pub primary_att4: String,
}

/// `CMSG_FUSION_LOADOUT`: the classes to save (slot, class) and the slot to equip.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LoadoutRequest {
    pub equip_slot: u8,
    pub classes: Vec<(u8, ClassLoadout)>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum LoadoutStatus {
    Saved = 0,
    /// Saved; the equip waits until out of combat, or the respawn.
    Pending = 1,
    /// The equipped class is in hand (also the login reply).
    Applied = 2,
    /// Nothing saved: `error` in `error_slot`.
    Rejected = 3,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum LoadoutError {
    None = 0,
    UnknownWeapon,
    WrongSlot,
    Locked,
    UnknownAttachment,
    AttachmentNotOnGun,
    TwoAttachmentsNeedBling,
    IncompatibleAttachments,
    UnknownCamo,
    NameTooLong,
    /// Not a perk of that slot's tier, or not a deathstreak.
    WrongPerk,
    /// Saved too often: the server takes a few at once, then one a second.
    TooFast,
    /// Kits are edited and switched only near a trainer or innkeeper, through the kit window's
    /// field editor, or by a GM.
    NotAtKitStation,
    /// A talent pick the kit can't hold.
    UnknownTalent,
    /// A killstreak not on the ladder, or chosen twice.
    UnknownStreak,
    /// A perk in more slots than its stacking allows.
    PerkDoesNotStack,
    /// A Pro the account has not earned, or one on a perk without a Pro.
    ProLocked,
}

/// `SMSG_FUSION_LOADOUT`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LoadoutReply {
    pub status: LoadoutStatus,
    pub error: LoadoutError,
    pub error_slot: u8,
    pub equipped_slot: u8,
    pub classes: Vec<(u8, ClassLoadout)>,
}

fn put_str(out: &mut Vec<u8>, s: &str) {
    let b = &s.as_bytes()[..s.len().min(255)];
    out.push(b.len() as u8);
    out.extend_from_slice(b);
}

fn put_classes(out: &mut Vec<u8>, classes: &[(u8, ClassLoadout)]) {
    let n = classes.len().min(CLASS_SLOTS as usize);
    out.push(n as u8);
    for (slot, l) in &classes[..n] {
        out.push(*slot);
        put_str(out, &l.name);
        out.extend_from_slice(&l.primary.to_le_bytes());
        put_str(out, &l.primary_att1);
        put_str(out, &l.primary_att2);
        out.push(l.primary_camo);
        out.extend_from_slice(&l.secondary.to_le_bytes());
        put_str(out, &l.secondary_att);
        put_str(out, &l.secondary_att2);
        for v in [l.lethal, l.tactical]
            .into_iter()
            .chain(l.perks)
            .chain([l.deathstreak])
            .chain(l.streaks)
            .chain(l.talent_picks)
        {
            out.extend_from_slice(&v.to_le_bytes());
        }
        put_str(out, &l.primary_att3);
        out.push(l.perk_pro);
        put_str(out, &l.primary_att4);
    }
}

fn read_classes(r: &mut Cursor<'_>) -> Result<Vec<(u8, ClassLoadout)>> {
    let n = r.u8()?;
    if n > CLASS_SLOTS {
        bail!("{n} classes in one loadout message");
    }
    let mut out = Vec::with_capacity(n as usize);
    for _ in 0..n {
        let slot = r.u8()?;
        if slot >= CLASS_SLOTS {
            bail!("class slot {slot}");
        }
        let name = r.str(CLASS_NAME_MAX)?;
        let primary = r.u16()?;
        let primary_att1 = r.str(16)?;
        let primary_att2 = r.str(16)?;
        let primary_camo = r.u8()?;
        let secondary = r.u16()?;
        let secondary_att = r.str(16)?;
        let secondary_att2 = r.str(16)?;
        let lethal = r.u16()?;
        let tactical = r.u16()?;
        let perks = [r.u16()?, r.u16()?, r.u16()?];
        let deathstreak = r.u16()?;
        let streaks = [r.u16()?, r.u16()?, r.u16()?];
        let mut talent_picks = [0u16; KIT_TALENT_TIERS];
        for p in &mut talent_picks {
            *p = r.u16()?;
        }
        let primary_att3 = r.str(16)?;
        let perk_pro = r.u8()?;
        let primary_att4 = r.str(16)?;
        out.push((
            slot,
            ClassLoadout {
                name,
                primary,
                primary_att1,
                primary_att2,
                primary_camo,
                secondary,
                secondary_att,
                secondary_att2,
                lethal,
                tactical,
                perks,
                deathstreak,
                streaks,
                talent_picks,
                primary_att3,
                perk_pro,
                primary_att4,
            },
        ));
    }
    Ok(out)
}

/// `CMSG_FUSION_LOADOUT`'s body.
pub fn encode_loadout_request(m: &LoadoutRequest) -> Vec<u8> {
    let mut out = vec![LOADOUT_VERSION, m.equip_slot];
    put_classes(&mut out, &m.classes);
    out
}

/// The server decodes these; here for tests.
pub fn decode_loadout_request(body: &[u8]) -> Result<LoadoutRequest> {
    let mut r = Cursor { b: body };
    r.loadout_version()?;
    let equip_slot = r.u8()?;
    if equip_slot >= CLASS_SLOTS && equip_slot != NO_CLASS {
        bail!("equip slot {equip_slot}");
    }
    let classes = read_classes(&mut r)?;
    r.done()?;
    Ok(LoadoutRequest {
        equip_slot,
        classes,
    })
}

/// The server writes these; here for tests.
pub fn encode_loadout_reply(m: &LoadoutReply) -> Vec<u8> {
    let mut out = vec![
        LOADOUT_VERSION,
        m.status as u8,
        m.error as u8,
        m.error_slot,
        m.equipped_slot,
    ];
    put_classes(&mut out, &m.classes);
    out
}

pub fn decode_loadout_reply(body: &[u8]) -> Result<LoadoutReply> {
    let mut r = Cursor { b: body };
    r.loadout_version()?;
    let status = match r.u8()? {
        0 => LoadoutStatus::Saved,
        1 => LoadoutStatus::Pending,
        2 => LoadoutStatus::Applied,
        3 => LoadoutStatus::Rejected,
        s => bail!("bad loadout status {s}"),
    };
    let error = match r.u8()? {
        0 => LoadoutError::None,
        1 => LoadoutError::UnknownWeapon,
        2 => LoadoutError::WrongSlot,
        3 => LoadoutError::Locked,
        4 => LoadoutError::UnknownAttachment,
        5 => LoadoutError::AttachmentNotOnGun,
        6 => LoadoutError::TwoAttachmentsNeedBling,
        7 => LoadoutError::IncompatibleAttachments,
        8 => LoadoutError::UnknownCamo,
        9 => LoadoutError::NameTooLong,
        10 => LoadoutError::WrongPerk,
        11 => LoadoutError::TooFast,
        12 => LoadoutError::NotAtKitStation,
        13 => LoadoutError::UnknownTalent,
        14 => LoadoutError::UnknownStreak,
        15 => LoadoutError::PerkDoesNotStack,
        16 => LoadoutError::ProLocked,
        e => bail!("bad loadout error {e}"),
    };
    let error_slot = r.u8()?;
    let equipped_slot = r.u8()?;
    let classes = read_classes(&mut r)?;
    r.done()?;
    Ok(LoadoutReply {
        status,
        error,
        error_slot,
        equipped_slot,
        classes,
    })
}

/// A channel message: the channel and its body.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ChannelMessage {
    pub channel: u8,
    pub body: Vec<u8>,
}

pub fn encode_channel(m: &ChannelMessage) -> Vec<u8> {
    let mut out = Vec::with_capacity(4 + m.body.len());
    out.push(VERSION);
    out.push(m.channel);
    out.extend_from_slice(&(m.body.len().min(MAX_CHANNEL_BODY) as u16).to_le_bytes());
    out.extend_from_slice(&m.body[..m.body.len().min(MAX_CHANNEL_BODY)]);
    out
}

pub fn decode_channel(body: &[u8]) -> Result<ChannelMessage> {
    if body.len() < 4 {
        bail!("short channel message");
    }
    if body[0] != VERSION {
        bail!("channel version {}", body[0]);
    }
    let channel = body[1];
    let len = u16::from_le_bytes([body[2], body[3]]) as usize;
    if len > MAX_CHANNEL_BODY || body.len() != 4 + len {
        bail!("channel body {} of {}", len, body.len() - 4);
    }
    Ok(ChannelMessage {
        channel,
        body: body[4..].to_vec(),
    })
}

/// [`CHANNEL_WORLD`] op 36: an area-loot window's source map (the server's
/// `Fusion::AreaLoot::SourceMap`), sent beside each combined loot window and again when a corpse
/// leaves it.
pub const OP_LOOT_SOURCES: u8 = 36;

/// One corpse an area-loot window gathers from.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LootSource {
    pub guid: u64,
    /// The creature template, for a corpse the client holds no object for.
    pub entry: u32,
}

/// Which corpse each row of an area-loot window came from: `[36, loot guid u64, n u8, n x (guid
/// u64, entry u32), m u8, m x (wire slot u8, source index u8)]`. The sources are in window order,
/// the opened corpse first; a corpse that adds only coin is listed with no row.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct LootSources {
    /// The window's guid, the opened corpse.
    pub loot_guid: u64,
    pub sources: Vec<LootSource>,
    /// `(wire loot slot, index into sources)`.
    pub rows: Vec<(u8, u8)>,
}

pub fn encode_loot_sources(m: &LootSources) -> Vec<u8> {
    let n = m.sources.len().min(255);
    let rows: Vec<(u8, u8)> = m
        .rows
        .iter()
        .copied()
        .filter(|&(_, i)| usize::from(i) < n)
        .take(255)
        .collect();
    let mut out = Vec::with_capacity(11 + 12 * n + 2 * rows.len());
    out.push(OP_LOOT_SOURCES);
    out.extend_from_slice(&m.loot_guid.to_le_bytes());
    out.push(n as u8);
    for s in &m.sources[..n] {
        out.extend_from_slice(&s.guid.to_le_bytes());
        out.extend_from_slice(&s.entry.to_le_bytes());
    }
    out.push(rows.len() as u8);
    for (slot, index) in rows {
        out.push(slot);
        out.push(index);
    }
    out
}

/// An op-36 body (the channel message's body, op byte first).
pub fn decode_loot_sources(body: &[u8]) -> Result<LootSources> {
    let mut r = Cursor { b: body };
    let op = r.u8()?;
    if op != OP_LOOT_SOURCES {
        bail!("op {op} is not a loot source map");
    }
    let loot_guid = r.u64()?;
    let n = usize::from(r.u8()?);
    let mut sources = Vec::with_capacity(n);
    for _ in 0..n {
        let guid = r.u64()?;
        let entry = r.u32()?;
        sources.push(LootSource { guid, entry });
    }
    let m = usize::from(r.u8()?);
    let mut rows = Vec::with_capacity(m);
    for _ in 0..m {
        let slot = r.u8()?;
        let index = r.u8()?;
        if usize::from(index) >= n {
            bail!("a loot row names source {index} of {n}");
        }
        rows.push((slot, index));
    }
    r.done()?;
    Ok(LootSources {
        loot_guid,
        sources,
        rows,
    })
}

impl WorldWriter {
    /// `CMSG_FUSION_CHANNEL`: a feature's message on its channel.
    pub fn fusion_channel(&mut self, m: &ChannelMessage) -> Result<()> {
        self.send(CMSG_FUSION_CHANNEL, &encode_channel(m))
    }

    /// `CMSG_FUSION_LOADOUT`: save classes and equip one.
    pub fn fusion_loadout(&mut self, m: &LoadoutRequest) -> Result<()> {
        self.send(CMSG_FUSION_LOADOUT, &encode_loadout_request(m))
    }

    /// `CMSG_FUSION_SHOTS`: this tick's shots (at most [`MAX_SHOTS_PER_BATCH`]).
    pub fn fusion_shots(&mut self, shots: &[Shot]) -> Result<()> {
        self.send(CMSG_FUSION_SHOTS, &encode_shots(shots))
    }

    /// `CMSG_FUSION_STATE`: stance, gait, held gun and the FFA flag requests, sent on change.
    pub fn fusion_state(&mut self, state: &State) -> Result<()> {
        self.send(CMSG_FUSION_STATE, &encode_state(state))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vector(name: &str) -> Vec<u8> {
        let text = include_str!("../tests/data/fusion_packets.txt");
        let hex = text
            .lines()
            .filter(|l| !l.starts_with('#'))
            .find_map(|l| l.strip_prefix(name)?.strip_prefix(' '))
            .unwrap_or_else(|| panic!("no vector {name}"));
        (0..hex.len() / 2)
            .map(|i| u8::from_str_radix(&hex[2 * i..2 * i + 2], 16).unwrap())
            .collect()
    }

    fn sample_shot() -> Shot {
        Shot {
            time_ms: 123_456,
            weapon_id: 7,
            flags: SHOT_ADS | SHOT_CLAIMS_HEAD,
            stance: Stance::Crouch,
            target_guid: 0xF1A2B3,
            origin: [10.5, -20.25, 30.0],
            direction: [0.0, 1.0, 0.0],
            seed: 0xDEAD_BEEF,
            spread_deg: 2.5,
        }
    }

    fn sample_state() -> State {
        State {
            stance: Stance::Prone,
            gait: Gait::Walk,
            flags: STATE_AIMING | STATE_FFA_ON,
            held_weapon_id: 42,
            loaded_ammo: 5,
        }
    }

    fn sample_events() -> Vec<Event> {
        vec![
            Event {
                kind: EventKind::Hit,
                flags: EVENT_HEADSHOT,
                guid: 0x1234,
                amount: 45,
                extra: 7,
            },
            Event {
                kind: EventKind::Kill,
                flags: EVENT_FATAL,
                guid: 0x1234,
                amount: 0,
                extra: 7,
            },
        ]
    }

    #[test]
    fn a_struck_event_round_trips() {
        let ev = vec![Event {
            kind: EventKind::Struck,
            flags: 0,
            guid: 0xF130_0000_0000_1234,
            amount: 0,
            extra: 0,
        }];
        assert_eq!(decode_events(&encode_events(&ev)).unwrap(), ev);
        let mut bad = encode_events(&ev);
        bad[2] = 7; // a kind past Struck
        assert!(decode_events(&bad).is_err());
    }

    fn sample_class() -> ClassLoadout {
        ClassLoadout {
            name: "Admin".into(),
            primary: 2,
            primary_att1: "acog".into(),
            primary_camo: 1,
            secondary: 30,
            streaks: [1, 5, 7],
            ..Default::default()
        }
    }

    #[test]
    fn loadout_messages_match_the_servers_bytes() {
        let req = LoadoutRequest {
            equip_slot: 0,
            classes: vec![(0, sample_class())],
        };
        assert_eq!(encode_loadout_request(&req), vector("loadout_request_one"));
        assert_eq!(
            decode_loadout_request(&vector("loadout_request_one")).unwrap(),
            req
        );
        let rep = LoadoutReply {
            status: LoadoutStatus::Applied,
            error: LoadoutError::None,
            error_slot: NO_CLASS,
            equipped_slot: 0,
            classes: vec![(0, sample_class())],
        };
        assert_eq!(encode_loadout_reply(&rep), vector("loadout_reply_applied"));
        assert_eq!(
            decode_loadout_reply(&vector("loadout_reply_applied")).unwrap(),
            rep
        );
    }

    #[test]
    fn the_wrong_perk_error_decodes() {
        let mut rep = vector("loadout_reply_applied");
        rep[1] = LoadoutStatus::Rejected as u8;
        rep[2] = 10;
        assert_eq!(
            decode_loadout_reply(&rep).unwrap().error,
            LoadoutError::WrongPerk
        );
    }

    #[test]
    fn the_too_fast_error_decodes() {
        let mut rep = vector("loadout_reply_applied");
        rep[1] = LoadoutStatus::Rejected as u8;
        rep[2] = 11;
        assert_eq!(
            decode_loadout_reply(&rep).unwrap().error,
            LoadoutError::TooFast
        );
    }

    #[test]
    fn the_kit_errors_decode_and_talent_picks_round_trip() {
        let mut rep = vector("loadout_reply_applied");
        rep[1] = LoadoutStatus::Rejected as u8;
        rep[2] = 12;
        assert_eq!(
            decode_loadout_reply(&rep).unwrap().error,
            LoadoutError::NotAtKitStation
        );
        rep[2] = 13;
        assert_eq!(
            decode_loadout_reply(&rep).unwrap().error,
            LoadoutError::UnknownTalent
        );
        rep[2] = 14;
        assert_eq!(
            decode_loadout_reply(&rep).unwrap().error,
            LoadoutError::UnknownStreak
        );
        rep[2] = 15;
        assert_eq!(
            decode_loadout_reply(&rep).unwrap().error,
            LoadoutError::PerkDoesNotStack
        );
        rep[2] = 16;
        assert_eq!(
            decode_loadout_reply(&rep).unwrap().error,
            LoadoutError::ProLocked
        );
        rep[2] = 17;
        assert!(decode_loadout_reply(&rep).is_err());
        let kit = ClassLoadout {
            talent_picks: [1, 0, 3, 0, 5, 65535],
            ..sample_class()
        };
        let req = LoadoutRequest {
            equip_slot: 2,
            classes: vec![(2, kit)],
        };
        assert_eq!(
            decode_loadout_request(&encode_loadout_request(&req)).unwrap(),
            req
        );
    }

    #[test]
    fn a_kits_third_attachment_and_pro_bits_match_the_servers_bytes() {
        let kit = ClassLoadout {
            primary_att3: "fmj".into(),
            perk_pro: 5,
            ..sample_class()
        };
        let req = LoadoutRequest {
            equip_slot: 0,
            classes: vec![(0, kit)],
        };
        assert_eq!(
            encode_loadout_request(&req),
            vector("loadout_request_rails")
        );
        assert_eq!(
            decode_loadout_request(&vector("loadout_request_rails")).unwrap(),
            req
        );
    }

    #[test]
    fn a_kits_fourth_attachment_matches_the_servers_bytes() {
        let kit = ClassLoadout {
            primary_att3: "fmj".into(),
            perk_pro: 5,
            primary_att4: "xmags".into(),
            ..sample_class()
        };
        let req = LoadoutRequest {
            equip_slot: 0,
            classes: vec![(0, kit)],
        };
        assert_eq!(encode_loadout_request(&req), vector("loadout_request_four"));
        assert_eq!(
            decode_loadout_request(&vector("loadout_request_four")).unwrap(),
            req
        );
        // A version 4 body (no fourth attachment) is refused.
        let mut old = vector("loadout_request_rails");
        old[0] = 4;
        old.pop();
        assert!(decode_loadout_request(&old).is_err());
    }

    #[test]
    fn a_malformed_loadout_reply_is_refused() {
        let good = vector("loadout_reply_applied");
        assert!(decode_loadout_reply(&good[..good.len() - 1]).is_err());
        let mut extra = good.clone();
        extra.push(0);
        assert!(decode_loadout_reply(&extra).is_err());
        let mut status = good.clone();
        status[1] = 9;
        assert!(decode_loadout_reply(&status).is_err());
        let mut long_name = good;
        long_name[7] = 25; // the name's length byte
        assert!(decode_loadout_reply(&long_name).is_err());
    }

    #[test]
    fn flag_values_match_the_server() {
        assert_eq!(
            (STATE_MANTLE, STATE_LUNGE, EVENT_PROGRESS),
            (0x20, 0x40, 0x08)
        );
        assert_eq!((CMSG_FUSION_LOADOUT, SMSG_FUSION_LOADOUT), (831, 832));
        assert_eq!((CMSG_FUSION_CHANNEL, SMSG_FUSION_CHANNEL), (833, 834));
        let m = ChannelMessage {
            channel: CHANNEL_BUILD,
            body: vec![1, 2, 3],
        };
        let bytes = encode_channel(&m);
        assert_eq!(bytes, vec![VERSION, CHANNEL_BUILD, 3, 0, 1, 2, 3]);
        assert_eq!(decode_channel(&bytes).unwrap(), m);
        assert!(decode_channel(&bytes[..6]).is_err());
        assert!(decode_channel(&[VERSION + 1, 1, 0, 0]).is_err());
    }

    #[test]
    fn encodes_the_servers_bytes() {
        assert_eq!(encode_shots(&[sample_shot()]), vector("shots_one"));
        assert_eq!(encode_shots(&[]), vector("shots_empty"));
        assert_eq!(encode_state(&sample_state()), vector("state"));
        assert_eq!(encode_events(&sample_events()), vector("events_hit_kill"));
    }

    #[test]
    fn decodes_the_servers_bytes() {
        assert_eq!(
            decode_shots(&vector("shots_one")).unwrap(),
            vec![sample_shot()]
        );
        assert_eq!(decode_state(&vector("state")).unwrap(), sample_state());
        assert_eq!(
            decode_events(&vector("events_hit_kill")).unwrap(),
            sample_events()
        );
    }

    #[test]
    fn pellets_hash_as_the_server_does() {
        // Shared with the server's tests (src/fusion/tests/BallisticsTests.cpp).
        assert_eq!(pellet_hash(0, 0), 0);
        assert_eq!(pellet_hash(1234, 0), 0x400c_726e);
        assert_eq!(pellet_hash(1234, 1), 0x2bf4_7b3e);
        assert_eq!(pellet_hash(1234, 7), 0x0087_797a);
        assert_eq!(pellet_hash(0xDEAD_BEEF, 3), 0xec23_9f07);
    }

    #[test]
    fn pellets_scatter_inside_the_cone() {
        let dir = [0.0, 1.0, 0.0];
        assert_eq!(pellet_direction(dir, 99, 0, 0.0), dir); // no spread: straight
        let cos_cone = (5.0f32).to_radians().cos();
        let mut spread_out = 0;
        for i in 0..64 {
            let d = pellet_direction(dir, 4321, i, 5.0);
            let len = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]).sqrt();
            assert!((len - 1.0).abs() < 1e-4);
            assert!(d[1] >= cos_cone - 1e-4, "pellet {i} outside the cone");
            if d[1] < (1.0f32).to_radians().cos() {
                spread_out += 1;
            }
        }
        assert!(spread_out > 32, "pellets bunch at the centre: {spread_out}");
        // Straight up or down still scatters (no degenerate basis).
        let up = pellet_direction([0.0, 0.0, 1.0], 7, 3, 5.0);
        assert!(up[2] < 1.0 && up[2] >= cos_cone - 1e-4);
    }

    #[test]
    fn rejects_malformed_bodies() {
        let body = vector("shots_one");
        assert!(decode_shots(&body[..body.len() - 1]).is_err());
        let mut long = body.clone();
        long.push(0);
        assert!(decode_shots(&long).is_err());
        let mut version = body.clone();
        version[0] = 1; // the old shot layout
        assert!(decode_shots(&version).is_err());
        let mut stance = body.clone();
        stance[2 + 7] = 9;
        assert!(decode_shots(&stance).is_err());
        assert!(decode_shots(&[SHOTS_VERSION, MAX_SHOTS_PER_BATCH as u8 + 1]).is_err());
        let mut nan = body;
        nan[2 + 28..2 + 32].copy_from_slice(&f32::NAN.to_le_bytes());
        assert!(decode_shots(&nan).is_err());
    }

    #[test]
    fn caps_a_batch() {
        let shots = vec![sample_shot(); 20];
        assert_eq!(
            decode_shots(&encode_shots(&shots)).unwrap().len(),
            MAX_SHOTS_PER_BATCH
        );
    }

    fn sample_loot_sources() -> LootSources {
        LootSources {
            loot_guid: 0xF130_000C_3500_002A,
            sources: vec![
                LootSource {
                    guid: 0xF130_000C_3500_002A,
                    entry: 3125,
                },
                LootSource {
                    guid: 0xF130_000C_3600_002B,
                    entry: 3126,
                },
                LootSource {
                    guid: 0xF130_000C_3700_002C,
                    entry: 3127,
                },
            ],
            rows: vec![(0, 0), (1, 0), (2, 1)],
        }
    }

    #[test]
    fn loot_sources_match_the_server_vector_both_ways() {
        let bytes = vector("area_loot_sources");
        assert_eq!(encode_loot_sources(&sample_loot_sources()), bytes);
        assert_eq!(decode_loot_sources(&bytes).unwrap(), sample_loot_sources());
    }

    #[test]
    fn loot_sources_refuse_a_malformed_body() {
        let bytes = vector("area_loot_sources");
        assert!(decode_loot_sources(&bytes[..bytes.len() - 1]).is_err());
        let mut long = bytes.clone();
        long.push(0);
        assert!(decode_loot_sources(&long).is_err());
        let mut other = bytes.clone();
        other[0] = 35;
        assert!(decode_loot_sources(&other).is_err());
        let mut stray = bytes.clone();
        *stray.last_mut().unwrap() = 3; // the row names a fourth source of three
        assert!(decode_loot_sources(&stray).is_err());
        // A row naming no source is never written.
        let mut odd = sample_loot_sources();
        odd.rows.push((5, 9));
        assert_eq!(encode_loot_sources(&odd), bytes);
    }
}
