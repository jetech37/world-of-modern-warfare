//! What others see of the hands (WOMW): the state the third-person body is posed from
//! (crate::body_pose), ours and every other player's near.
//!
//! Ours is read each frame from what the first-person view does (the action system, the gun up
//! or the hands, aiming down the sights, reloading, sprinting, the stance, the aim's pitch, a
//! spell held in a hand, the shield up) and told to the server on the action-systems channel
//! (`Fusion::ActionSystems`, channel 10) when it changes, at most ten times a second and once a
//! second regardless (op 5), with each hand action as it happens (op 6: a swing, a cast, a throw,
//! a reload, a punch, a tool's swing). The server passes both on, with our guid, to every player
//! within 100 yards (ops 7 and 8). A shot needs no word of its own: every player's round already
//! reaches those near (world op 34, crate::tracer), and it kicks the shooter's body here.
//!
//! Each player we hear of: its gun in its hands (crate::npc_guns, as an NPC gunner's), WoW's
//! own clip for what it does (the rifle's ready stance and fire, each weapon's swing, a cast, the
//! shield's block, a tool's swing), and the layer on top. One not heard of for
//! [`FORGET_SECS`] goes back to plain WoW.

use std::collections::HashMap;

use benilla_app::Objects;
use benilla_app::fusion::{
    CHANNEL_WORLD, ChannelMessage, FusionChannelReceived, PlayUnitAnim, SendFusionChannel, Stance,
};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::action_systems::System;
use crate::body_pose::{BodyTargets, PoseTarget, ToolKind};
use crate::thirdperson::{anim, melee_ready_anim, off_swing_anim, swing_anim};

/// The action-systems channel's ops for the hands (`Fusion::ActionSystems`).
pub const CHANNEL: u8 = crate::action_systems::CHANNEL;
/// After the tools' ops 2-4 (crate::tools).
pub const OP_POSE: u8 = 5;
pub const OP_ACT: u8 = 6;
pub const OP_POSE_OF: u8 = 7;
pub const OP_ACT_OF: u8 = 8;

/// Pose flags (`kPose*`).
pub const POSE_GUN_UP: u8 = 0x01;
pub const POSE_ADS: u8 = 0x02;
pub const POSE_RELOADING: u8 = 0x04;
pub const POSE_SPRINT: u8 = 0x08;
pub const POSE_CAST_RIGHT: u8 = 0x10;
pub const POSE_CAST_LEFT: u8 = 0x20;
pub const POSE_BLOCK: u8 = 0x40;
pub const POSE_TWO_HANDED: u8 = 0x80;

/// The pose is told at most this often, and at least this often (seconds).
pub const SEND_MIN: f32 = 0.1;
pub const SEND_MAX: f32 = 1.0;
/// A player not heard of for this long goes back to plain WoW (seconds).
pub const FORGET_SECS: f32 = 3.0;
/// Seconds between replays of a held ready stance, and after a one-shot before it resumes.
const READY_REPLAY: f32 = 0.9;
const READY_AFTER: f32 = 0.6;

/// A body's pose on the wire: op 2's bytes after the op (and op 4's after the guid).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct HandsPose {
    pub system: System,
    pub flags: u8,
    pub stance: Stance,
    /// The aim's pitch in whole degrees, up positive.
    pub pitch: i8,
    /// The gun up (its catalog id), 0 for none.
    pub gun: u16,
    /// `ItemSubclassWeapon` of each hand's weapon, 255 for none.
    pub main_subclass: u8,
    pub off_subclass: u8,
    pub tool: ToolKind,
}

impl Default for HandsPose {
    fn default() -> Self {
        Self {
            system: System::Modern,
            flags: 0,
            stance: Stance::Stand,
            pitch: 0,
            gun: 0,
            main_subclass: 255,
            off_subclass: 255,
            tool: ToolKind::None,
        }
    }
}

impl HandsPose {
    pub fn has(&self, flag: u8) -> bool {
        self.flags & flag != 0
    }
}

fn system_from_wire(b: u8) -> Option<System> {
    System::ALL.into_iter().find(|s| s.wire() == b)
}

fn stance_wire(s: Stance) -> u8 {
    match s {
        Stance::Stand => 0,
        Stance::Crouch => 1,
        Stance::Prone => 2,
        Stance::Slide => 3,
    }
}

fn stance_from_wire(b: u8) -> Option<Stance> {
    Some(match b {
        0 => Stance::Stand,
        1 => Stance::Crouch,
        2 => Stance::Prone,
        3 => Stance::Slide,
        _ => return None,
    })
}

/// The pose's 9 bytes.
fn pose_bytes(p: &HandsPose) -> [u8; 9] {
    let g = p.gun.to_le_bytes();
    [
        p.system.wire(),
        p.flags,
        stance_wire(p.stance),
        p.pitch as u8,
        g[0],
        g[1],
        p.main_subclass,
        p.off_subclass,
        p.tool.wire(),
    ]
}

fn pose_from(b: &[u8]) -> Option<HandsPose> {
    let b: &[u8; 9] = b.try_into().ok()?;
    let pitch = b[3] as i8;
    if !(-90..=90).contains(&pitch) {
        return None;
    }
    Some(HandsPose {
        system: system_from_wire(b[0])?,
        flags: b[1],
        stance: stance_from_wire(b[2])?,
        pitch,
        gun: u16::from_le_bytes([b[4], b[5]]),
        main_subclass: b[6],
        off_subclass: b[7],
        tool: ToolKind::from_wire(b[8]),
    })
}

/// Op 5, 10 bytes: `[5, system, flags, stance, pitch i8, gun u16, main, off, tool]`.
pub fn encode_pose(p: &HandsPose) -> Vec<u8> {
    let mut v = vec![OP_POSE];
    v.extend_from_slice(&pose_bytes(p));
    v
}

/// Op 7, 18 bytes: `[7, guid u64, the pose's 9 bytes]`.
pub fn decode_pose_of(b: &[u8]) -> Option<(u64, HandsPose)> {
    if b.len() != 18 || b[0] != OP_POSE_OF {
        return None;
    }
    Some((
        u64::from_le_bytes(b[1..9].try_into().ok()?),
        pose_from(&b[9..])?,
    ))
}

/// A hand action (op 3's kind and argument).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Act {
    /// A weapon's swing, the left hand's or the right's.
    Swing { left: bool },
    /// A spell cast from a hand (0 right, 1 left).
    Cast { hand: u8 },
    /// A grenade: its pin pulled (the wind-up holds), or thrown.
    Throw { cooking: bool },
    /// A reload begun, lasting this many tenths of a second.
    Reload { tenths: u8 },
    /// A bare fist's punch.
    Punch { left: bool },
    /// A tool's swing.
    Tool(ToolKind),
}

impl Act {
    pub fn wire(self) -> [u8; 2] {
        match self {
            Act::Swing { left } => [1, left as u8],
            Act::Cast { hand } => [2, hand],
            Act::Throw { cooking } => [3, cooking as u8],
            Act::Reload { tenths } => [4, tenths],
            Act::Punch { left } => [5, left as u8],
            Act::Tool(t) => [6, t.wire()],
        }
    }

    pub fn from_wire(kind: u8, arg: u8) -> Option<Act> {
        Some(match kind {
            1 => Act::Swing { left: arg != 0 },
            2 => Act::Cast { hand: arg.min(1) },
            3 => Act::Throw { cooking: arg != 0 },
            4 => Act::Reload { tenths: arg },
            5 => Act::Punch { left: arg != 0 },
            6 => Act::Tool(ToolKind::from_wire(arg)),
            _ => return None,
        })
    }
}

/// Op 6, 3 bytes: `[6, kind, arg]`.
pub fn encode_act(a: Act) -> Vec<u8> {
    let [k, arg] = a.wire();
    vec![OP_ACT, k, arg]
}

/// Op 8, 11 bytes: `[8, guid u64, kind, arg]`.
pub fn decode_act_of(b: &[u8]) -> Option<(u64, Act)> {
    if b.len() != 11 || b[0] != OP_ACT_OF {
        return None;
    }
    Some((
        u64::from_le_bytes(b[1..9].try_into().ok()?),
        Act::from_wire(b[9], b[10])?,
    ))
}

/// Pitch radians to the wire's whole degrees.
pub fn pitch_degrees(pitch: f32) -> i8 {
    pitch.to_degrees().round().clamp(-90.0, 90.0) as i8
}

/// Whether a changed pose goes out now: a change after [`SEND_MIN`], or anything after
/// [`SEND_MAX`]. A pitch moving a degree or two is not a change worth a packet on its own.
pub fn should_send(last: Option<(f32, HandsPose)>, now: f32, pose: &HandsPose) -> bool {
    let Some((at, sent)) = last else {
        return true;
    };
    let since = now - at;
    if since >= SEND_MAX {
        return true;
    }
    if since < SEND_MIN {
        return false;
    }
    let mut a = sent;
    let mut b = *pose;
    let pitch_moved = (i16::from(a.pitch) - i16::from(b.pitch)).abs() >= 3;
    a.pitch = 0;
    b.pitch = 0;
    a != b || pitch_moved
}

/// The one-shots a body did lately (`Time::elapsed_secs`).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Recent {
    pub fired_at: Option<f32>,
    pub reload: Option<(f32, f32)>,
    pub throw_at: Option<f32>,
    pub cooking: bool,
    pub cast_at: Option<(usize, f32)>,
    pub tool: Option<(ToolKind, f32)>,
}

impl Recent {
    /// Take an action in at `now`.
    pub fn take(&mut self, act: Act, now: f32) {
        match act {
            Act::Throw { cooking: true } => {
                self.cooking = true;
                self.throw_at = Some(now);
            }
            Act::Throw { cooking: false } => {
                // Held, the wind-up is done: straight to the release.
                self.throw_at = Some(if self.cooking { now - 0.25 } else { now });
                self.cooking = false;
            }
            Act::Reload { tenths } => {
                self.reload = Some((now, (f32::from(tenths) / 10.0).max(0.5)));
            }
            Act::Cast { hand } => self.cast_at = Some((usize::from(hand.min(1)), now)),
            Act::Tool(t) => self.tool = Some((t, now)),
            Act::Swing { .. } | Act::Punch { .. } => {}
        }
    }
}

/// The layer's target from a pose and its recent one-shots; `pitch` in radians overrides the
/// wire's whole degrees (our own, unrounded).
pub fn target_of(p: &HandsPose, r: &Recent, pitch: Option<f32>) -> PoseTarget {
    PoseTarget {
        pitch: pitch.unwrap_or_else(|| f32::from(p.pitch).to_radians()),
        gun_up: p.has(POSE_GUN_UP),
        pistol: false, // set by the caller, which has the catalog (`is_pistol`)
        foregrip: None, // likewise (`foregrip_of`)
        ads: p.has(POSE_ADS),
        sprint: p.has(POSE_SPRINT),
        crouch: matches!(p.stance, Stance::Crouch | Stance::Slide),
        prone: p.stance == Stance::Prone,
        cast: [p.has(POSE_CAST_RIGHT), p.has(POSE_CAST_LEFT)],
        block: p.has(POSE_BLOCK),
        fired_at: r.fired_at,
        reload: r
            .reload
            .filter(|_| p.has(POSE_RELOADING) || r.reload.is_some()),
        throw_at: r.throw_at,
        cooking: r.cooking,
        cast_at: r.cast_at,
        tool: r.tool,
    }
}

/// The WoW clip a held pose keeps replaying (the ready stance), if any.
/// Where in a ready clip a standing body is held (a fraction of that model's own clip, so a
/// tauren's 2 s clip and a human's 1 s one settle alike); `MW2_READY_FRAC` overrides it.
pub fn ready_frac() -> f32 {
    static F: std::sync::OnceLock<f32> = std::sync::OnceLock::new();
    *F.get_or_init(|| {
        std::env::var("MW2_READY_FRAC")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(READY_FRAC)
    })
}

/// The default held fraction ([`ready_frac`]), chosen from renders of every race and sex.
pub const READY_FRAC: f32 = 0.5;

/// A standing body's held ready frame: the clip and where in it (crate::fusion's
/// `LayerUnitHold`), or none (a fresh shot or swing is playing, or no ready stance).
pub fn ready_hold(p: &HandsPose, now: f32, ready_at: f32) -> Option<(u16, f32)> {
    (now >= ready_at)
        .then(|| ready_clip(p))
        .flatten()
        .map(|id| (id, ready_frac()))
}

/// How far a long gun's foregrip is ahead of its grip (yards): a share of what the gun of its
/// class measures in the hand (crate::npc_guns `held_length`).
pub fn foregrip_of(catalog: &crate::catalog::Catalog, gun: u16) -> Option<f32> {
    catalog
        .gun(gun)
        .map(|g| FOREGRIP_SHARE * crate::npc_guns::held_length(&g.class))
}

/// Where along a long gun the support hand holds it, from the grip toward the muzzle.
pub const FOREGRIP_SHARE: f32 = 0.38;

/// The mount's side of a mounted rider's one-shot: its own attack (AttackUnarmed, every creature
/// model has one) for a shot or a swing, nothing for anything else. The stock client sends a
/// mounted rider's swings to its upper body only; the layer adds the mount's lunge, so a mounted
/// fight reads as one. Its wound (CombatWound 9) is already in the mount set.
pub fn mount_side(id: u16) -> Option<u16> {
    matches!(id, 16..=24 | 49 | 85..=88 | 57..=59 | 117 | 118).then_some(MOUNT_ATTACK)
}

/// AttackUnarmed, the attack every creature model authors.
pub const MOUNT_ATTACK: u16 = 16;

/// Whether a catalog gun is held as a pistol (crate::body_pose `gun_hold`).
pub fn is_pistol(catalog: &crate::catalog::Catalog, gun: u16) -> bool {
    catalog
        .gun(gun)
        .is_some_and(|g| matches!(g.class.as_str(), "pistol" | "machine_pistol"))
}

pub fn ready_clip(p: &HandsPose) -> Option<u16> {
    if p.has(POSE_GUN_UP) {
        return (!p.has(POSE_SPRINT)).then_some(anim::READY_RIFLE);
    }
    if p.has(POSE_BLOCK) {
        return Some(anim::SHIELD_BLOCK);
    }
    match p.system {
        System::Classic => Some(melee_ready_anim(p.main_subclass, p.has(POSE_CAST_RIGHT))),
        // Utility's weapons are sheathed (the server's sheath state shows it): WoW's own idle.
        _ => None,
    }
}

/// The WoW clip an action plays, if any (a throw and a reload are the layer's alone).
pub fn act_clip(p: &HandsPose, act: Act) -> Option<u16> {
    match act {
        Act::Swing { left: true } => Some(off_swing_anim(p.off_subclass)),
        Act::Swing { left: false } => Some(swing_anim(p.main_subclass, p.has(POSE_TWO_HANDED))),
        Act::Punch { left } => Some(if left {
            anim::ATTACK_OFF
        } else {
            anim::ATTACK_UNARMED
        }),
        Act::Cast { .. } => Some(anim::SPELL_CAST_DIRECTED),
        Act::Tool(t) => t.swing_anim(),
        Act::Throw { .. } | Act::Reload { .. } => None,
    }
}

/// A tool's swing, told by the tools' own code (the gathering tools) as it swings: our body
/// plays it in third person and everyone near sees it.
#[derive(Message, Clone, Copy, Debug)]
pub struct ToolSwing(pub ToolKind);

/// A tool the tools' code puts in the hands beyond Utility's own (crate::tools), if any; Utility's
/// readied tool is read from crate::action_systems.
#[derive(Resource, Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct HeldTool(pub ToolKind);

/// The body's swing for one of Utility's tools (crate::tools).
pub fn tool_kind(a: crate::action_systems::Ability) -> ToolKind {
    use crate::action_systems::Ability as A;
    match a {
        A::MiningPick => ToolKind::Pick,
        A::WoodcuttingAxe => ToolKind::Axe,
        A::SkinningKnife => ToolKind::Knife,
        A::Herbalism => ToolKind::Herb,
        A::FishingPole => ToolKind::FishingPole,
        A::BlacksmithHammer | A::RepairHammer => ToolKind::Hammer,
        a if a.is_instrument() => ToolKind::Instrument,
        _ => ToolKind::None,
    }
}

/// Our own pose and one-shots this frame (read by crate::thirdperson too).
#[derive(Resource, Debug, Default)]
pub struct OwnHands {
    pub pose: HandsPose,
    pub pitch: f32,
    pub recent: Recent,
    /// Actions this frame, in order.
    pub acts: Vec<Act>,
    sent: Option<(f32, HandsPose)>,
}

/// One other player we hear of.
#[derive(Debug)]
struct Remote {
    pose: HandsPose,
    recent: Recent,
    heard: f32,
    ready_at: f32,
    acts: Vec<Act>,
}

#[derive(Resource, Debug, Default)]
pub struct RemoteHands {
    players: HashMap<u64, Remote>,
}

impl RemoteHands {
    /// A player's last pose, if we hear of it.
    pub fn pose(&self, guid: u64) -> Option<&HandsPose> {
        self.players.get(&guid).map(|r| &r.pose)
    }
}

/// Our hands read, told and the others' heard and posed, each frame.
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct BodyStateSet;

pub struct BodyStatePlugin;

impl Plugin for BodyStatePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<OwnHands>()
            .init_resource::<RemoteHands>()
            .init_resource::<HeldTool>()
            .add_message::<ToolSwing>()
            .add_systems(
                Update,
                (read_own, tell_own, hear_others, pose_others, target_own, probe_pose)
                    .chain()
                    .in_set(Mw2Set::Hud)
                    .in_set(BodyStateSet),
            );
    }
}

/// What our hands do this frame, from the first-person view's own state.
#[allow(clippy::too_many_arguments)]
fn read_own(
    time: Res<Time>,
    state: Res<crate::mode::FpsState>,
    systems: Res<crate::action_systems::ActionSystems>,
    weapon: Res<crate::weapon::Weapon>,
    melee: Res<crate::melee::MeleeWeapon>,
    held: Res<crate::melee::MeleeHeld>,
    hands: Res<crate::hands::Hands>,
    input: Res<crate::input::Mw2Input>,
    fusion: Res<crate::fusion::Fusion>,
    equip: Res<crate::equipment::EquipState>,
    (form, loadout, catalog, aim, tool): (
        Res<crate::forms::PlayerForm>,
        Res<crate::config::Loadout>,
        Res<crate::catalog::Catalog>,
        Res<crate::aim::Aim>,
        Res<HeldTool>,
    ),
    mut tool_swings: MessageReader<ToolSwing>,
    mut own: ResMut<OwnHands>,
    mut seen: Local<Option<SeenOwn>>,
) {
    let now = time.elapsed_secs();
    let own = &mut *own;
    own.acts.clear();
    let held_tool = systems
        .tool_out()
        .map(tool_kind)
        .filter(|t| *t != ToolKind::None)
        .unwrap_or(tool.0);
    let gun_up = state.active
        && systems.active == System::Modern
        && !held.0
        && !state.building
        && !form.gunless();
    let gun = gun_up
        .then(|| loadout.class_gun.as_ref())
        .flatten()
        .and_then(|h| catalog.gun_named(&h.base))
        .map_or(0, |g| g.id);
    let mut flags = 0;
    let mut set = |on: bool, f: u8| {
        if on {
            flags |= f;
        }
    };
    set(gun_up, POSE_GUN_UP);
    set(gun_up && weapon.ads > 0.5, POSE_ADS);
    set(gun_up && weapon.reloading(), POSE_RELOADING);
    set(
        fusion.gait == benilla_app::fusion::Gait::Sprint,
        POSE_SPRINT,
    );
    let casting = |h: usize| hands.charging[h].is_some() || hands.spraying[h].is_some();
    set(held.0 && casting(0), POSE_CAST_RIGHT);
    set(held.0 && casting(1), POSE_CAST_LEFT);
    set(
        held.0 && hands.left_shield() && input.ads.pressed,
        POSE_BLOCK,
    );
    set(melee.two_handed, POSE_TWO_HANDED);
    own.pitch = aim
        .ray
        .map_or(0.0, |r| r.direction.y.clamp(-1.0, 1.0).asin());
    own.pose = HandsPose {
        system: systems.active,
        flags,
        stance: fusion.stance,
        pitch: pitch_degrees(own.pitch),
        gun,
        main_subclass: melee.subclass,
        off_subclass: melee.off_subclass,
        tool: held_tool,
    };
    // The one-shots, from the counters the first-person view keeps.
    let now_seen = SeenOwn {
        shots: weapon.shots,
        swings: weapon.knife_swings,
        cast_at: hands.cast_at,
        reloading: weapon.reloading(),
        cooking: equip.cooking.is_some(),
        thrown: equip.lethal_left + equip.tactical_left,
    };
    let Some(before) = seen.replace(now_seen) else {
        return;
    };
    if !state.active {
        return;
    }
    if now_seen.shots != before.shots {
        own.recent.fired_at = Some(now);
    }
    let mut acts = Vec::new();
    if now_seen.swings != before.swings {
        let left = weapon.knife_left;
        acts.push(if held_tool != ToolKind::None {
            Act::Tool(held_tool)
        } else if systems.active == System::Utility {
            Act::Punch { left }
        } else {
            Act::Swing { left }
        });
    }
    for h in 0..2 {
        if now_seen.cast_at[h] != before.cast_at[h] && now_seen.cast_at[h].is_some() {
            acts.push(Act::Cast { hand: h as u8 });
        }
    }
    if now_seen.reloading && !before.reloading {
        acts.push(Act::Reload {
            tenths: (weapon.reload_secs * 10.0).round().clamp(1.0, 255.0) as u8,
        });
    }
    if now_seen.cooking && !before.cooking {
        acts.push(Act::Throw { cooking: true });
    } else if (before.cooking && !now_seen.cooking) || now_seen.thrown < before.thrown {
        // The release and the count's fall may come a frame or two apart: one throw.
        let just_thrown =
            !own.recent.cooking && own.recent.throw_at.is_some_and(|at| now - at < 0.5);
        if !just_thrown {
            acts.push(Act::Throw { cooking: false });
        }
    }
    for ToolSwing(t) in tool_swings.read() {
        acts.push(Act::Tool(*t));
    }
    for a in acts {
        own.recent.take(a, now);
        own.acts.push(a);
    }
}

#[derive(Clone, Copy, Debug)]
struct SeenOwn {
    shots: u64,
    swings: u64,
    cast_at: [Option<f32>; 2],
    reloading: bool,
    cooking: bool,
    thrown: u32,
}

/// Tell the server our pose and actions (only with the fusion server and the layer up).
fn tell_own(
    time: Res<Time>,
    state: Res<crate::mode::FpsState>,
    fusion: Res<crate::fusion::Fusion>,
    mut own: ResMut<OwnHands>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    if !fusion.enabled || !state.active || !state.in_world {
        own.sent = None;
        return;
    }
    let now = time.elapsed_secs();
    for a in own.acts.clone() {
        out.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL,
            body: encode_act(a),
        }));
    }
    let pose = own.pose;
    if should_send(own.sent, now, &pose) {
        own.sent = Some((now, pose));
        out.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL,
            body: encode_pose(&pose),
        }));
    }
}

/// Other players' poses and actions (ops 4, 5) and rounds (world op 34).
fn hear_others(
    time: Res<Time>,
    state: Res<crate::mode::FpsState>,
    mut msgs: MessageReader<FusionChannelReceived>,
    mut remote: ResMut<RemoteHands>,
) {
    let now = time.elapsed_secs();
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel == CHANNEL {
            if let Some((guid, pose)) = decode_pose_of(&m.body) {
                if Some(guid) == state.self_guid {
                    continue;
                }
                let r = remote.players.entry(guid).or_insert(Remote {
                    pose,
                    recent: Recent::default(),
                    heard: now,
                    ready_at: 0.0,
                    acts: Vec::new(),
                });
                // A reload over (the flag down) ends the layer's too.
                if !pose.has(POSE_RELOADING) && r.pose.has(POSE_RELOADING) {
                    r.recent.reload = None;
                }
                r.pose = pose;
                r.heard = now;
            } else if let Some((guid, act)) = decode_act_of(&m.body)
                && let Some(r) = remote.players.get_mut(&guid)
            {
                r.recent.take(act, now);
                r.acts.push(act);
                r.heard = now;
            }
        } else if m.channel == CHANNEL_WORLD
            && let Some(round) = crate::tracer::decode_player_round(&m.body)
            && let Some(r) = remote.players.get_mut(&round.shooter)
        {
            // The kick, and the rifle's fire (pose_others plays it).
            r.recent.fired_at = Some(now);
            r.heard = now;
        }
    }
    remote.players.retain(|_, r| now - r.heard < FORGET_SECS);
}

/// Each player we hear of: its gun, its clips and its layer target.
#[allow(clippy::too_many_arguments)]
fn pose_others(
    time: Res<Time>,
    objects: Objects,
    mut remote: ResMut<RemoteHands>,
    mut targets: ResMut<BodyTargets>,
    mut guns: ResMut<crate::npc_guns::NpcGuns>,
    mut anims: MessageWriter<PlayUnitAnim>,
    mut last_fire: Local<HashMap<u64, f32>>,
    mut holds: ResMut<benilla_app::fusion::LayerUnitHold>,
    catalog: Res<crate::catalog::Catalog>,
    attach: benilla_app::fusion::UnitAttach,
) {
    let now = time.elapsed_secs();
    targets.0.clear();
    let mut held_guns = HashMap::new();
    for (&guid, r) in &mut remote.players {
        if r.pose.gun != 0 && r.pose.has(POSE_GUN_UP) {
            held_guns.insert(guid, r.pose.gun);
        }
        let Some(unit) = objects.entity(guid) else {
            r.acts.clear();
            continue;
        };
        let mount = attach.mount_of(unit);
        let mut play = |id: u16| {
            anims.write(PlayUnitAnim {
                entity: unit,
                anim_id: id,
            });
            // Mounted, the mount strikes with its rider (its wound already routes to it).
            if let (Some(m), Some(side)) = (mount, mount_side(id)) {
                anims.write(PlayUnitAnim {
                    entity: m,
                    anim_id: side,
                });
            }
        };
        // A new round: the rifle's fire.
        if let Some(at) = r.recent.fired_at
            && last_fire.get(&guid) != Some(&at)
        {
            last_fire.insert(guid, at);
            if r.pose.has(POSE_GUN_UP) {
                play(anim::ATTACK_RIFLE);
                r.ready_at = now + READY_AFTER;
            }
        }
        for act in std::mem::take(&mut r.acts) {
            if let Some(id) = act_clip(&r.pose, act) {
                play(id);
                r.ready_at = now + READY_AFTER;
            }
        }
        // Standing, the ready stance is held at a settled frame (no restart pops); moving, the
        // replays below carry it over the legs.
        match ready_hold(&r.pose, now, r.ready_at) {
            Some(h) => holds.0.insert(unit, h),
            None => holds.0.remove(&unit),
        };
        if now >= r.ready_at
            && let Some(id) = ready_clip(&r.pose)
        {
            play(id);
            r.ready_at = now + READY_REPLAY;
        }
        let mut t = target_of(&r.pose, &r.recent, None);
        t.pistol = is_pistol(&catalog, r.pose.gun);
        t.foregrip = foregrip_of(&catalog, r.pose.gun);
        targets.0.insert(unit, t);
    }
    last_fire.retain(|g, _| remote.players.contains_key(g));
    guns.set_player_guns(held_guns);
}

/// Our own body's target: in third person all of it; in first person, while the first-person
/// body is drawn (crate::fp_body), only the stance ([`first_person_target`]), so looking down
/// shows the crouch's bent knees and the prone body lying down; the viewmodel does the rest.
fn target_own(
    state: Res<crate::mode::FpsState>,
    third: Res<crate::thirdperson::ThirdPerson>,
    fp_body: Option<Res<crate::fp_body::FpBodySwitch>>,
    objects: Objects,
    own: Res<OwnHands>,
    mut targets: ResMut<BodyTargets>,
    catalog: Res<crate::catalog::Catalog>,
) {
    if !state.active || state.killcam {
        return;
    }
    let Some(me) = objects.player() else {
        return;
    };
    if third.on {
        targets
            .0
            .insert(me, {
                let mut t = target_of(&own.pose, &own.recent, Some(own.pitch));
                t.pistol = is_pistol(&catalog, own.pose.gun);
                t.foregrip = foregrip_of(&catalog, own.pose.gun);
                t
            });
    } else if fp_body.is_some_and(|s| s.on)
        && let Some(t) = first_person_target(own.pose.stance)
    {
        targets.0.insert(me, t);
    }
}

/// Our own body's target in first person: the stance alone, level, nothing in the hands (the
/// viewmodel's), and none standing (the body keeps its own clip).
pub fn first_person_target(stance: Stance) -> Option<PoseTarget> {
    let crouch = matches!(stance, Stance::Crouch | Stance::Slide);
    let prone = stance == Stance::Prone;
    (crouch || prone).then_some(PoseTarget {
        crouch,
        prone,
        ..PoseTarget::default()
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> HandsPose {
        HandsPose {
            system: System::Classic,
            flags: POSE_ADS | POSE_CAST_LEFT,
            stance: Stance::Crouch,
            pitch: -20,
            gun: 0x0102,
            main_subclass: 7,
            off_subclass: 6,
            tool: ToolKind::None,
        }
    }

    #[test]
    fn first_person_poses_only_the_stance() {
        assert_eq!(first_person_target(Stance::Stand), None);
        let c = first_person_target(Stance::Crouch).expect("crouch");
        assert!(c.crouch && !c.prone && !c.gun_up && c.pitch == 0.0);
        assert!(first_person_target(Stance::Slide).expect("slide").crouch);
        let p = first_person_target(Stance::Prone).expect("prone");
        assert!(p.prone && !p.crouch && p.cast == [false; 2]);
    }

    /// The server's own test bytes (`ActionSystemsTests.cpp`,
    /// `action_system_pose_and_acts_relay_byte_for_byte`).
    #[test]
    fn the_pose_goes_out_as_the_server_reads_it() {
        assert_eq!(
            encode_pose(&sample()),
            vec![
                5,
                1,
                POSE_ADS | POSE_CAST_LEFT,
                1,
                (-20i8) as u8,
                0x02,
                0x01,
                7,
                6,
                0
            ]
        );
        assert_eq!(encode_act(Act::Cast { hand: 1 }), vec![6, 2, 1]);
    }

    #[test]
    fn a_relayed_pose_and_act_decode() {
        let mut b = vec![OP_POSE_OF];
        b.extend_from_slice(&0x0102_0304_0506_0708u64.to_le_bytes());
        b.extend_from_slice(&encode_pose(&sample())[1..]);
        assert_eq!(decode_pose_of(&b), Some((0x0102_0304_0506_0708, sample())));
        assert_eq!(decode_pose_of(&b[..17]), None);
        let mut bad = b.clone();
        bad[9] = 3; // a fourth system
        assert_eq!(decode_pose_of(&bad), None);
        let mut bad = b.clone();
        bad[12] = 100; // past straight up
        assert_eq!(decode_pose_of(&bad), None);

        let a = [OP_ACT_OF, 5, 0, 0, 0, 0, 0, 0, 0, 2, 1];
        assert_eq!(decode_act_of(&a), Some((5, Act::Cast { hand: 1 })));
        assert_eq!(
            decode_act_of(&[OP_ACT_OF, 5, 0, 0, 0, 0, 0, 0, 0, 9, 0]),
            None
        );
    }

    #[test]
    fn each_of_utilitys_tools_swings_as_its_kind() {
        use crate::action_systems::Ability as A;
        assert_eq!(tool_kind(A::MiningPick), ToolKind::Pick);
        assert_eq!(tool_kind(A::RepairHammer), ToolKind::Hammer);
        assert_eq!(tool_kind(A::Herbalism), ToolKind::Herb);
        assert_eq!(tool_kind(A::Primary), ToolKind::None);
        for a in A::ALL.into_iter().filter(|a| a.is_tool()) {
            assert_ne!(tool_kind(a), ToolKind::None, "{a:?}");
        }
    }

    #[test]
    fn every_act_round_trips() {
        let acts = [
            Act::Swing { left: true },
            Act::Swing { left: false },
            Act::Cast { hand: 0 },
            Act::Cast { hand: 1 },
            Act::Throw { cooking: true },
            Act::Throw { cooking: false },
            Act::Reload { tenths: 23 },
            Act::Punch { left: true },
            Act::Tool(ToolKind::Pick),
            Act::Tool(ToolKind::FishingPole),
        ];
        for a in acts {
            let [k, arg] = a.wire();
            assert_eq!(Act::from_wire(k, arg), Some(a));
        }
    }

    #[test]
    fn the_pose_is_told_on_change_within_bounds() {
        let p = sample();
        assert!(should_send(None, 0.0, &p));
        // Nothing new: once a second.
        assert!(!should_send(Some((0.0, p)), 0.5, &p));
        assert!(should_send(Some((0.0, p)), 1.0, &p));
        // A change waits out the minimum, then goes.
        let mut q = p;
        q.flags |= POSE_RELOADING;
        assert!(!should_send(Some((0.0, p)), 0.05, &q));
        assert!(should_send(Some((0.0, p)), 0.1, &q));
        // A degree of pitch is not worth a packet; three are.
        let mut q = p;
        q.pitch += 1;
        assert!(!should_send(Some((0.0, p)), 0.2, &q));
        q.pitch += 2;
        assert!(should_send(Some((0.0, p)), 0.2, &q));
    }

    #[test]
    fn pitch_rounds_to_whole_degrees_within_straight_up_and_down() {
        assert_eq!(pitch_degrees(0.5f32.to_radians() * 0.9), 0);
        assert_eq!(pitch_degrees(30f32.to_radians()), 30);
        assert_eq!(pitch_degrees(-2.0), -90);
    }

    #[test]
    fn a_target_is_the_pose_with_its_one_shots() {
        let mut r = Recent::default();
        r.take(Act::Reload { tenths: 25 }, 4.0);
        r.take(Act::Throw { cooking: true }, 5.0);
        assert!(r.cooking);
        r.take(Act::Throw { cooking: false }, 6.0);
        assert_eq!((r.throw_at, r.cooking), (Some(5.75), false));
        let mut p = sample();
        p.stance = Stance::Prone;
        p.flags |= POSE_GUN_UP | POSE_BLOCK;
        let t = target_of(&p, &r, None);
        assert!(t.gun_up && t.ads && t.block && t.prone && !t.crouch);
        assert_eq!(t.cast, [false, true]);
        assert!((t.pitch - (-20f32).to_radians()).abs() < 1e-6);
        assert_eq!(t.reload, Some((4.0, 2.5)));
        assert_eq!(target_of(&p, &r, Some(0.3)).pitch, 0.3);
        let mut s = sample();
        s.stance = Stance::Slide;
        assert!(target_of(&s, &r, None).crouch);
    }

    #[test]
    fn each_system_holds_its_own_ready_stance_and_plays_its_own_acts() {
        let mut gun = HandsPose {
            flags: POSE_GUN_UP,
            ..default()
        };
        assert_eq!(ready_clip(&gun), Some(anim::READY_RIFLE));
        gun.flags |= POSE_SPRINT;
        assert_eq!(ready_clip(&gun), None); // the run carries it
        let sword = HandsPose {
            system: System::Classic,
            main_subclass: 7,
            ..default()
        };
        assert_eq!(ready_clip(&sword), Some(anim::READY_1H));
        let caster = HandsPose {
            flags: POSE_CAST_RIGHT,
            ..sword
        };
        assert_eq!(ready_clip(&caster), Some(anim::READY_SPELL_DIRECTED));
        let utility = HandsPose {
            system: System::Utility,
            ..default()
        };
        assert_eq!(ready_clip(&utility), None);
        assert_eq!(act_clip(&sword, Act::Swing { left: false }), Some(17));
        let great = HandsPose {
            main_subclass: 8,
            flags: POSE_TWO_HANDED,
            ..sword
        };
        assert_eq!(act_clip(&great, Act::Swing { left: false }), Some(18));
        let dual = HandsPose {
            off_subclass: 15,
            ..sword
        };
        assert_eq!(
            act_clip(&dual, Act::Swing { left: true }),
            Some(anim::ATTACK_OFF_PIERCE)
        );
        assert_eq!(
            act_clip(&utility, Act::Punch { left: false }),
            Some(anim::ATTACK_UNARMED)
        );
        assert_eq!(act_clip(&utility, Act::Tool(ToolKind::Pick)), Some(17));
        assert_eq!(act_clip(&gun, Act::Reload { tenths: 20 }), None);
        assert_eq!(
            act_clip(&caster, Act::Cast { hand: 0 }),
            Some(anim::SPELL_CAST_DIRECTED)
        );
    }

    #[test]
    fn a_mounted_riders_shot_and_swing_make_the_mount_strike_too() {
        assert_eq!(mount_side(anim::ATTACK_RIFLE), Some(MOUNT_ATTACK));
        assert_eq!(mount_side(17), Some(MOUNT_ATTACK), "a one-hander's swing");
        assert_eq!(mount_side(anim::READY_RIFLE), None, "a stance is the rider's alone");
        assert_eq!(mount_side(53), None, "a cast is the rider's alone");
    }

    #[test]
    fn a_standing_gun_holds_its_ready_frame_but_not_mid_shot() {
        let gun = HandsPose {
            gun: 1,
            flags: POSE_GUN_UP,
            ..Default::default()
        };
        assert_eq!(ready_hold(&gun, 5.0, 4.0), Some((anim::READY_RIFLE, ready_frac())));
        assert_eq!(ready_hold(&gun, 5.0, 5.5), None, "a shot plays out first");
        let sprint = HandsPose {
            flags: POSE_GUN_UP | POSE_SPRINT,
            ..gun
        };
        assert_eq!(ready_hold(&sprint, 5.0, 0.0), None, "the run carries it");
    }

    #[test]
    fn the_body_state_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((read_own, tell_own, hear_others, pose_others, target_own).chain());
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }
}

/// The guid benilla's capture gives its lighting-matrix subject (`WOW_SUBJECT_DISPLAY`).
pub const FXVIEW_UNIT_GUID: u64 = (0xF130u64 << 48) | (69u64 << 24) | 0x69;

/// `MW2_PROBE_POSE=crouch|gun|crouch,gun`: the capture subject takes that layer pose (a headless
/// look at the crouch and the held gun on any body, crate::npc_guns' `MW2_PROBE_HOLD_GUN`).
fn probe_pose(
    objects: Objects,
    time: Res<Time>,
    mut targets: ResMut<BodyTargets>,
    mut anims: MessageWriter<PlayUnitAnim>,
    mut next: Local<f32>,
    mut holds: ResMut<benilla_app::fusion::LayerUnitHold>,
    catalog: Res<crate::catalog::Catalog>,
    attach: benilla_app::fusion::UnitAttach,
) {
    static SPEC: std::sync::OnceLock<Option<String>> = std::sync::OnceLock::new();
    let Some(spec) = SPEC.get_or_init(|| std::env::var("MW2_PROBE_POSE").ok()) else {
        return;
    };
    let Some(unit) = objects.entity(FXVIEW_UNIT_GUID) else {
        return;
    };
    // While the capture holds the clock (elapsed 0) the body may not be built yet, so the ready
    // clip is asked for every frame until the clock runs; then on the game's own cadence.
    let held = time.elapsed_secs() == 0.0;
    let anim_probe = std::env::var_os("MW2_PROBE_ANIM").is_some();
    if spec.contains("gun") && !anim_probe && (held || time.elapsed_secs() >= *next) {
        // `MW2_PROBE_READY_EVERY=<secs>` (default the game's own cadence).
        let every = std::env::var("MW2_PROBE_READY_EVERY")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(READY_REPLAY);
        *next = time.elapsed_secs() + every;
        anims.write(PlayUnitAnim {
            entity: unit,
            anim_id: anim::READY_RIFLE,
        });
    }
    // `MW2_PROBE_ANIM=<id>`: that one-shot replayed on the subject (and its mount's side, as a
    // mounted rider's plays go), for a look at mounted attacks, shots and casts.
    let probe_anim: Option<u16> = std::env::var("MW2_PROBE_ANIM").ok().and_then(|v| v.parse().ok());
    if let Some(id) = probe_anim {
        if held || time.elapsed_secs() >= *next {
            *next = time.elapsed_secs() + 0.6;
            anims.write(PlayUnitAnim { entity: unit, anim_id: id });
            if let (Some(m), Some(side)) = (attach.mount_of(unit), mount_side(id)) {
                anims.write(PlayUnitAnim { entity: m, anim_id: side });
            }
        }
    } else if spec.contains("gun") {
        holds.0.insert(unit, (anim::READY_RIFLE, ready_frac()));
    }
    let probe_gun: Option<u16> = std::env::var("MW2_PROBE_HOLD_GUN").ok().and_then(|v| {
        v.trim().parse().ok().or_else(|| {
            catalog
                .gun_named(&crate::npc_guns::probe_gun_name(&v))
                .map(|g| g.id)
        })
    });
    targets.0.insert(
        unit,
        PoseTarget {
            crouch: spec.contains("crouch"),
            gun_up: spec.contains("gun"),
            pistol: probe_gun.is_some_and(|id| is_pistol(&catalog, id)),
            foregrip: probe_gun.and_then(|id| foregrip_of(&catalog, id)),
            ..default()
        },
    );
}
