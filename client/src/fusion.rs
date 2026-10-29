//! Azeroth Warfare's fusion on the client: with an Azeroth Warfare server, the gun fires real
//! rounds the server referees instead of casting WoW's Auto Shot. Each round goes up as a ray
//! (`CMSG_FUSION_SHOTS`) with the unit under the crosshair; the server re-casts it against its own
//! hitboxes, rewound to when we fired, and deals MW2 damage. Stance, sprint and the FFA flag go up
//! as `CMSG_FUSION_STATE` when they change, and the server's verdicts come back as
//! `SMSG_FUSION_EVENTS`.
//!
//! `MW2_FUSION=0` turns it off (a stock vmangos server never answers the fusion packets).

use std::time::Instant;

use benilla_app::Objects;
use benilla_app::fusion::{
    EVENT_FATAL, EVENT_HEADSHOT, EVENT_LEFT, EventKind, FusionEventsReceived, Gait, SHOT_ADS,
    SHOT_AIRBORNE, SHOT_LEFT, STATE_AIMING, STATE_FFA_OFF, STATE_FFA_ON, STATE_REARM,
    STATE_RELOAD_ONE, STATE_RELOADING, SendFusionShots, SendFusionState, Shot, Stance, State,
};
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::config::Loadout;
use crate::input::Mw2Input;
use crate::lua;
use crate::mode::FpsState;
use crate::stance_view::{must_stand, stance_input};
use crate::weapon::Weapon;
use benilla_ui::script::UiScript;

/// MW2 movement requests on `CMSG_FUSION_STATE`, the server's `Packets::STATE_MANTLE` and
/// `STATE_LUNGE`: climb the ledge ahead; knife-lunge at the enemy in front.
pub const STATE_MANTLE: u8 = 0x20;
pub const STATE_LUNGE: u8 = 0x40;

/// The state message's flags.
pub fn state_flags(
    aiming: bool,
    ffa: bool,
    reload_started: bool,
    rearm: bool,
    mantle: bool,
    lunge: bool,
) -> u8 {
    let mut flags = if ffa { STATE_FFA_ON } else { STATE_FFA_OFF };
    for (on, bit) in [
        (aiming, STATE_AIMING),
        (reload_started, STATE_RELOADING),
        (rearm, STATE_REARM),
        (mantle, STATE_MANTLE),
        (lunge, STATE_LUNGE),
    ] {
        if on {
            flags |= bit;
        }
    }
    flags
}

/// The move requests one state message carries: the server takes one of them, so with both the
/// mantle goes now and the lunge waits for the next message. Returns (mantle, lunge, kept lunge).
pub fn split_moves(mantle: bool, lunge: bool) -> (bool, bool, bool) {
    if mantle && lunge {
        (true, false, true)
    } else {
        (mantle, lunge, false)
    }
}

/// The weapon table the server loads; the client needs each gun's id, fire time and magazine.
const SEED_WEAPONS: &str = include_str!(env!("AW_FUSION_CATALOG"));

/// One gun as the server knows it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GunProfile {
    pub id: u16,
    pub fire_time: f32,
    pub mag_size: u32,
    pub pellets: u8,
    /// MW2's unlock rank; the level is `ceil(rank * 60 / 70)` (the server's `UnlockLevel`).
    pub unlock_rank: u8,
    /// One per class is unlocked at level 1.
    pub starter: bool,
    /// Not a gun of its own (an underbarrel weapon, equipment, a killstreak): never the Armory's
    /// pick.
    pub alternate: bool,
}

impl GunProfile {
    pub fn unlock_level(&self) -> u32 {
        (u32::from(self.unlock_rank) * 60).div_ceil(70).clamp(1, 60)
    }

    pub fn unlocked(&self, level: u32) -> bool {
        self.starter || level >= self.unlock_level()
    }
}

/// The special rounds and the level each unlocks at (the server's `AmmoUnlockLevel`), by the
/// server's `AmmoType` order.
pub const ROUNDS: [(&str, u32); 8] = [
    ("Basic", 1),
    ("Incendiary", 10),
    ("Ice-threaded", 10),
    ("Blessed", 30),
    ("Arcane", 30),
    ("Armor-piercing", 20),
    ("Hollow-point", 10),
    ("Explosive", 20),
];

/// A round the weapon fired this frame, for [`send_rounds`].
#[derive(Message, Clone, Debug)]
pub struct FiredRound {
    /// The camera ray in Bevy space.
    pub ray: Ray3d,
    /// The unit under the crosshair, if any.
    pub target: Option<u64>,
    pub ads: bool,
    /// The pull's pellet seed and the cone they scatter in (degrees, crate::spread).
    pub seed: u32,
    pub spread_deg: f32,
    /// Equipment thrown or a killstreak called in (its catalog name) instead of the gun in
    /// hand; `detonate` sets off C4 already out.
    pub item: Option<String>,
    pub detonate: bool,
    /// Akimbo: the left gun's round (the server keeps each gun's magazine).
    pub left: bool,
}

/// The weapon id a plain melee swing rides `CMSG_FUSION_SHOTS` with (the server's
/// `Fusion::Melee::kSwingWeaponId`): the server picks the enemy in reach, in the cone the eye
/// aims, and strikes it with the hand's weapon (its combos and positionals), never WoW's
/// auto-attack.
pub const SWING_WEAPON_ID: u16 = 4999;

/// A melee swing of the hands this frame (crate::weapon), for [`send_rounds`].
#[derive(Message, Clone, Copy, Debug)]
pub struct MeleeSwing {
    /// The eye's ray in Bevy space (the swing's aim).
    pub ray: Ray3d,
    /// The unit under the crosshair, if any.
    pub target: Option<u64>,
}

/// The shot a swing goes up as.
pub fn swing_shot(now_ms: u32, stance: Stance, airborne: bool, swing: &MeleeSwing) -> Shot {
    Shot {
        time_ms: now_ms,
        weapon_id: SWING_WEAPON_ID,
        flags: if airborne { SHOT_AIRBORNE } else { 0 },
        stance,
        target_guid: swing.target.unwrap_or(0),
        origin: to_wow(swing.ray.origin),
        direction: to_wow(*swing.ray.direction),
        seed: 0,
        spread_deg: 0.0,
    }
}

/// `Shot::flags` for C4's trigger (the server's `SHOT_DETONATE`).
pub const SHOT_DETONATE: u8 = 0x08;

/// The fusion's client state, read by the HUD.
#[derive(Resource, Debug)]
pub struct Fusion {
    pub enabled: bool,
    pub stance: Stance,
    pub gait: Gait,
    pub ffa: bool,
    /// Elapsed seconds of the last server-confirmed hit and headshot, for the hitmarker.
    pub last_hit: f32,
    pub last_headshot: f32,
    /// Server-confirmed kills this session.
    pub kills: u32,
    /// The Armory's picks: the gun in hand (overriding the class default) and the loaded round.
    pub selected: Option<String>,
    pub round: u8,
    rearm: bool,
    /// One-shot MW2 movement requests for the next state message (`crate::movement`).
    pub mantle: bool,
    pub lunge: bool,
    guns: HashMap<String, GunProfile>,
    epoch: Instant,
    sent: Option<State>,
    last_pos: Option<Vec3>,
    /// Off the ground (rising or falling faster than a step), for spread and the shot flag.
    pub airborne: bool,
    /// Elapsed seconds of the last landing, for the landing spread and the gun's dip.
    pub landed_at: f32,
    /// Horizontal speed in yards per second, for the view bob.
    pub speed: f32,
    last_vy: f32,
    fall: Fall,
}

impl Default for Fusion {
    fn default() -> Self {
        let enabled = std::env::var("MW2_FUSION")
            .map(|v| v != "0")
            .unwrap_or(true);
        let text = std::env::var("MW2_FUSION_WEAPONS")
            .ok()
            .and_then(|p| std::fs::read_to_string(p).ok());
        Self {
            enabled,
            stance: Stance::Stand,
            gait: Gait::Still,
            ffa: false,
            last_hit: -10.0,
            last_headshot: -10.0,
            kills: 0,
            selected: None,
            round: 0,
            rearm: false,
            mantle: false,
            lunge: false,
            guns: parse_guns(text.as_deref().unwrap_or(SEED_WEAPONS)),
            epoch: Instant::now(),
            sent: None,
            last_pos: None,
            airborne: false,
            landed_at: -10.0,
            speed: 0.0,
            last_vy: 0.0,
            fall: Fall::default(),
        }
    }
}

impl Fusion {
    pub fn gun(&self, name: &str) -> Option<GunProfile> {
        self.guns.get(name).copied()
    }

    /// Our millisecond clock; the server learns its offset from the shots themselves.
    fn now_ms(&self) -> u32 {
        self.epoch.elapsed().as_millis() as u32
    }
}

/// The ids, fire times, magazines and pellets out of the server's `catalog.json` (or a version 1
/// `weapons.json`): one object per weapon, `"id"` first, so a scan per `"id":` over the weapons
/// array is enough without a JSON crate. The attachments and camos after it have ids too.
pub fn parse_guns(text: &str) -> HashMap<String, GunProfile> {
    let text = text
        .rfind("\"attachments\": [")
        .map_or(text, |end| &text[..end]);
    fn field<'a>(chunk: &'a str, key: &str) -> Option<&'a str> {
        let at = chunk.find(&format!("\"{key}\""))?;
        let rest = chunk[at + key.len() + 2..]
            .trim_start()
            .strip_prefix(':')?
            .trim_start();
        let end = rest.find([',', '}', '\n']).unwrap_or(rest.len());
        Some(rest[..end].trim().trim_matches('"'))
    }
    let mut out = HashMap::default();
    for chunk in text.split("\"id\"").skip(1) {
        let chunk = format!("\"id\"{chunk}");
        let (Some(id), Some(name)) = (field(&chunk, "id"), field(&chunk, "name")) else {
            continue;
        };
        let Ok(id) = id.parse::<u16>() else { continue };
        out.insert(
            name.to_string(),
            GunProfile {
                id,
                fire_time: field(&chunk, "fire_time")
                    .and_then(|v| v.parse().ok())
                    .unwrap_or(0.1),
                mag_size: field(&chunk, "mag_size")
                    .and_then(|v| v.parse().ok())
                    .unwrap_or(30),
                pellets: field(&chunk, "pellets")
                    .and_then(|v| v.parse().ok())
                    .unwrap_or(1),
                unlock_rank: field(&chunk, "unlock_rank")
                    .and_then(|v| v.parse().ok())
                    .unwrap_or(1),
                starter: field(&chunk, "starter") == Some("true"),
                alternate: matches!(
                    field(&chunk, "slot"),
                    Some("alternate" | "lethal" | "tactical" | "killstreak")
                ),
            },
        );
    }
    out
}

/// WoW's axes from Bevy's (`bevy = (−y, z, −x)`, benilla-assets' transform).
pub(crate) fn to_wow(v: Vec3) -> [f32; 3] {
    [-v.z, -v.x, v.y]
}

pub struct FusionPlugin;

impl Plugin for FusionPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Fusion>()
            .init_resource::<crate::sprint::Sprint>()
            .add_message::<FiredRound>()
            .add_message::<MeleeSwing>()
            .add_message::<ServerStreak>()
            .add_message::<IncomingFire>()
            .add_systems(
                Update,
                (armory, apply_gun, update_stance)
                    .chain()
                    .after(Mw2Set::Mode)
                    .before(Mw2Set::Weapon),
            )
            .add_systems(
                Update,
                (send_state, send_rounds, read_events)
                    .after(Mw2Set::Weapon)
                    .before(Mw2Set::Combat),
            );
    }
}

/// The Armory, as the Modern bar's abilities (crate::action_systems): special ammo, the next
/// unlocked round, and rearm, the quartermaster's (a city or an inn). Each answers in the chat
/// frame.
fn armory(
    state: Res<FpsState>,
    input: Res<Mw2Input>,
    mut fusion: ResMut<Fusion>,
    script: Option<NonSend<benilla_ui::script::UiScript>>,
) {
    if !fusion.enabled || !state.active {
        return;
    }
    let Some(script) = script else { return };
    if !(input.armory_ammo.pressed || input.rearm.pressed) {
        return;
    }
    let level = lua::eval::<Option<u32>>(&script, "return UnitLevel(\"player\")")
        .flatten()
        .unwrap_or(1);
    let say = |msg: &str| {
        lua::run(
            &script,
            &format!("DEFAULT_CHAT_FRAME:AddMessage(\"|cffffd200Armory:|r {msg}\")"),
        );
    };
    if input.armory_ammo.pressed {
        fusion.round = next_round(fusion.round, level);
        say(&format!(
            "{} rounds loaded",
            ROUNDS[fusion.round as usize].0
        ));
    }
    if input.rearm.pressed {
        fusion.rearm = true;
        say("rearming (free in a city or an inn; Field Quartermaster: anywhere on its cooldown)");
    }
}

/// The special round after `round` among those unlocked at `level` (wrapping to the plain one).
pub fn next_round(round: u8, level: u32) -> u8 {
    let unlocked: Vec<u8> = (0..ROUNDS.len() as u8)
        .filter(|&r| level >= ROUNDS[r as usize].1)
        .collect();
    let at = unlocked.iter().position(|&r| r == round).unwrap_or(0);
    unlocked[(at + 1) % unlocked.len()]
}

/// The free-for-all PvP flag on or off: on at once, off five minutes out of combat (the server's
/// rule). No key: the honor window's toggle calls it.
pub fn toggle_ffa(fusion: &mut Fusion) {
    fusion.ffa = !fusion.ffa;
    info!(
        "benilla-mw2: free-for-all flag {}",
        if fusion.ffa {
            "on"
        } else {
            "off (in 5 minutes out of combat)"
        }
    );
}

/// The server's magazine and fire rate for the gun in hand.
fn apply_gun(
    fusion: Res<Fusion>,
    catalog: Res<crate::catalog::Catalog>,
    mut loadout: ResMut<Loadout>,
    rounds: Res<crate::rounds::EnchantedRounds>,
    time: Res<Time>,
) {
    if !fusion.enabled {
        return;
    }
    if let Some(name) = &fusion.selected
        && loadout.mw2_weapon != *name
    {
        loadout.mw2_weapon = name.clone();
        loadout.weapon = crate::arsenal::weapon_name(name);
    }
    let Some(gun) = fusion.gun(&loadout.mw2_weapon) else {
        return;
    };
    // A class gun with attachments takes their magazine and fire time, as the server does.
    let (mag, fire) = match (&loadout.class_gun, catalog.gun_named(&loadout.mw2_weapon)) {
        (Some(h), Some(g)) if h.base == loadout.mw2_weapon => {
            let atts: Vec<&str> = h.attachments.iter().map(String::as_str).collect();
            if atts.is_empty() {
                catalog.variant_mag_and_fire(g, &h.variant)
            } else {
                catalog.mag_and_fire(g, &atts)
            }
        }
        _ => (gun.mag_size, gun.fire_time),
    };
    let listed = catalog.gun_named(&loadout.mw2_weapon);
    // Frost rounds (crate::rounds) cycle the gun slower, as the server holds it to.
    let fire = fire * rounds.fire_time_multiplier(listed.map_or(0, |g| g.id), time.elapsed_secs());
    let fire_type = listed.map_or(0, |g| g.fire_type);
    let shield = listed.is_some_and(|g| g.class == "riot_shield");
    if loadout.mag_size != mag
        || loadout.shot_interval != fire
        || loadout.fire_type != fire_type
        || loadout.shield != shield
    {
        loadout.mag_size = mag;
        loadout.shot_interval = fire;
        loadout.fire_type = fire_type;
        loadout.shield = shield;
    }
}

/// MW2's stances (`crate::stance_view::stance_input`): a tap of Left Ctrl crouches and holding it
/// goes prone (the Crouch binding likewise), Space rises a stance, and sprinting stands you up.
/// Dead, mounted or on a taxi, you stand.
#[allow(clippy::too_many_arguments)]
pub(crate) fn update_stance(
    time: Res<Time>,
    state: Res<FpsState>,
    mut input: ResMut<Mw2Input>,
    mut fusion: ResMut<Fusion>,
    objects: Objects,
    bodies: Query<&GlobalTransform>,
    script: Option<NonSend<UiScript>>,
    mut poll: Local<(f32, bool)>,
    mut c_held: Local<f32>,
    mut sprint: ResMut<crate::sprint::Sprint>,
    perks: Res<crate::cac::ClientPerks>,
    downed: Res<crate::downed::Downed>,
    effects: Option<Res<crate::prestige::FieldEffects>>,
) {
    let t = time.elapsed_secs();
    let dt = time.delta_secs();
    // Dead or on a taxi, polled twice a second as the weapon polls its ammunition.
    if let Some(script) = script.as_ref().filter(|_| state.active)
        && t >= poll.0
    {
        let blocked: Option<u32> = lua::eval(
            script,
            "return (UnitIsDeadOrGhost(\"player\") or UnitOnTaxi(\"player\")) and 1 or 0",
        );
        *poll = (t + 0.5, blocked == Some(1));
    }
    let mounted = state
        .self_guid
        .and_then(|g| objects.object(g))
        .is_some_and(|f| f.unit_mount_display_id() != 0);
    // Down (crate::downed): the body reads dead to the interface, but it crawls, prone.
    let dead = poll.1 && !downed.active;
    // No public swimming state on the client; the server ignores stance moves in water.
    if !fusion.enabled || must_stand(state.active, dead, mounted, false) {
        fusion.stance = Stance::Stand;
        fusion.gait = gait_without_mw2(fusion.gait);
        input.blank_jump = false;
        *c_held = 0.0;
        sprint.update(dt, false, false, t);
        return;
    }
    let f = &mut *fusion;
    *c_held = if input.crouch.held { *c_held + dt } else { 0.0 };
    // A held Crouch binding goes prone once; after that the hold does nothing until it is let go.
    let hold = if f.stance == Stance::Prone {
        0.0
    } else {
        *c_held
    };
    // Space while standing and moving asks to mantle; the WoW jump still happens, and the
    // server's knock-back replaces its arc when there is a ledge.
    if input.jump.pressed && std::env::var_os("MW2_PROBE_LOG_MOVE").is_some() {
        info!(
            "probe: jump pressed, stance {:?} speed {:.2} -> mantle {}",
            f.stance,
            f.speed,
            f.stance == Stance::Stand && f.speed >= 0.3
        );
    }
    if input.jump.pressed && f.stance == Stance::Stand && f.speed >= 0.3 {
        f.mantle = true;
    }
    f.stance = stance_input(
        f.stance,
        input.crouch.pressed,
        hold,
        input.prone.pressed,
        input.jump.pressed,
    );
    if downed.active {
        f.stance = Stance::Prone;
    }
    input.blank_jump = matches!(f.stance, Stance::Crouch | Stance::Prone);
    let me = objects
        .player()
        .and_then(|e| bodies.get(e).ok())
        .map(|g| g.translation());
    let (measured, measured_vy) = match (me, f.last_pos) {
        (Some(p), Some(q)) if dt > 0.0 => {
            let d = (p - q) / dt;
            (Vec2::new(d.x, d.z).length(), d.y)
        }
        _ => (0.0, 0.0),
    };
    f.last_pos = me;
    // The body's position changes read through frame-time jitter: smoothed, or the noise
    // drives the gun's bob and the gait every frame (the gun shakes).
    let horizontal = smooth_toward(f.speed, measured, dt, SPEED_TAU);
    let vy = smooth_toward(f.last_vy, measured_vy, dt, SPEED_TAU);
    if f.fall.step(vy, t) {
        f.landed_at = t;
    }
    f.last_vy = vy;
    f.airborne = vy.abs() > 2.0;
    f.speed = horizontal;
    // Shift while moving sprints, as long as the stamina lasts; aiming or firing ends it.
    let wants = input.sprint
        && horizontal >= 0.3
        && f.stance != Stance::Prone
        && !input.ads.held
        && !input.fire.held;
    // An Adrenaline stim sprints as Marathon does while it lasts (crate::prestige).
    let tireless = perks.marathon || effects.is_some_and(|e| e.tireless(t));
    sprint.update(dt, wants, tireless, t);
    f.gait = if horizontal < 0.3 {
        Gait::Still
    } else if sprint.sprinting {
        Gait::Sprint
    } else if horizontal < 3.5 {
        Gait::Walk
    } else {
        Gait::Run
    };
    if f.gait == Gait::Sprint && f.stance != Stance::Stand {
        f.stance = Stance::Stand;
    }
    // A test probe (crate::probe): MW2_PROBE_LOG_MOVE logs the body ten times a second.
    if std::env::var_os("MW2_PROBE_LOG_MOVE").is_some() && (t * 10.0).fract() < dt * 10.0 {
        info!(
            "probe: move gait {:?} stance {:?} speed {:.1} vy {:.1} airborne {} jump {} sprint_key {}",
            f.gait, f.stance, f.speed, vy, f.airborne, input.jump.pressed, input.sprint
        );
    }
}

/// The gait while MW2 mode is off, dead or mounted: no sprint, or the server would keep giving
/// one (and spend its stamina) in plain WoW.
fn gait_without_mw2(gait: Gait) -> Gait {
    if gait == Gait::Sprint {
        Gait::Run
    } else {
        gait
    }
}

fn send_state(
    state: Res<FpsState>,
    input: Res<Mw2Input>,
    mut weapon: ResMut<Weapon>,
    loadout: Res<Loadout>,
    mut fusion: ResMut<Fusion>,
    mut out: MessageWriter<SendFusionState>,
) {
    if !fusion.enabled || !state.in_world {
        return;
    }
    // A reload is told when it finishes (the server fills the magazine then), so one cut short
    // by a gun swap fills nothing; a shell-by-shell reload tells each shell (STATE_RELOAD_ONE).
    let reload_started = weapon.reload_done > 0;
    if reload_started {
        weapon.reload_done -= 1;
    }
    let one_shell = weapon.reload_one > 0;
    if one_shell {
        weapon.reload_one -= 1;
    }

    // One-shot requests go up once, on the next state message.
    let rearm = std::mem::take(&mut fusion.rearm);
    let (mantle, lunge, later) = split_moves(
        std::mem::take(&mut fusion.mantle),
        std::mem::take(&mut fusion.lunge),
    );
    fusion.lunge = later;
    let flags = state_flags(
        state.active && input.ads.held,
        fusion.ffa,
        reload_started,
        rearm,
        mantle,
        lunge,
    );
    let flags = if one_shell {
        flags | STATE_RELOAD_ONE
    } else {
        flags
    };
    let next = State {
        stance: fusion.stance,
        gait: fusion.gait,
        flags,
        held_weapon_id: fusion.gun(&loadout.mw2_weapon).map_or(0, |g| g.id),
        loaded_ammo: fusion.round,
    };
    if fusion.sent != Some(next) || reload_started || one_shell || rearm || mantle || lunge {
        fusion.sent = Some(next);
        out.write(SendFusionState(next));
    }
}

fn send_rounds(
    mut rounds: MessageReader<FiredRound>,
    mut swings: MessageReader<MeleeSwing>,
    fusion: Res<Fusion>,
    loadout: Res<Loadout>,
    mut out: MessageWriter<SendFusionShots>,
) {
    if !fusion.enabled {
        rounds.clear();
        swings.clear();
        return;
    }
    let now = fusion.now_ms();
    // The hands' swings need no gun.
    let mut shots: Vec<Shot> = swings
        .read()
        .map(|s| swing_shot(now, fusion.stance, fusion.airborne, s))
        .collect();
    let Some(gun) = fusion.gun(&loadout.mw2_weapon) else {
        rounds.clear();
        if !shots.is_empty() {
            out.write(SendFusionShots(shots));
        }
        return;
    };
    for r in rounds.read() {
        // Equipment and rewards are shots of their own catalog id.
        let weapon_id = match &r.item {
            Some(name) => match fusion.gun(name) {
                Some(g) => g.id,
                None => continue,
            },
            None => gun.id,
        };
        let mut flags = if r.detonate { SHOT_DETONATE } else { 0 };
        if r.ads {
            flags |= SHOT_ADS;
        }
        if fusion.airborne {
            flags |= SHOT_AIRBORNE;
        }
        if r.left {
            flags |= SHOT_LEFT;
        }
        let shot = Shot {
            time_ms: now,
            weapon_id,
            flags,
            stance: fusion.stance,
            target_guid: r.target.unwrap_or(0),
            origin: to_wow(r.ray.origin),
            direction: to_wow(*r.ray.direction),
            seed: r.seed,
            spread_deg: r.spread_deg,
        };
        // One shot per pull: the server draws every pellet from its seed.
        shots.push(shot);
    }
    if !shots.is_empty() {
        out.write(SendFusionShots(shots));
    }
}

/// `StreakEarned`'s flag for a progress report (`amount` in tenths of a kill) rather than a
/// reward; the server's `EVENT_PROGRESS`.
pub const EVENT_PROGRESS: u8 = 0x08;

/// The server's word on the killstreak (MW2 design spec 4.1): its progress in tenths of a kill
/// (a player 1, a creature worth XP 0.1, an elite split between its damagers), or a reward
/// earned at `reward_kills`.
#[derive(Message, Clone, Copy, Debug, PartialEq)]
pub enum ServerStreak {
    Progress(u32),
    Reward { kills: u32, id: u16 },
}

/// A round fired at us by someone else (an NPC's gun, a player's): the server's `Hurt` event.
#[derive(Message, Clone, Copy, Debug, PartialEq)]
pub struct IncomingFire {
    /// The shooter.
    pub guid: u64,
    /// Its catalog gun.
    pub gun: u16,
    /// The round hit us (a miss carries no damage).
    pub hit: bool,
}

/// A `StreakEarned` event as the streak report it carries.
pub fn server_streak(flags: u8, amount: u32, extra: u16) -> ServerStreak {
    if flags & EVENT_PROGRESS != 0 {
        ServerStreak::Progress(amount)
    } else {
        ServerStreak::Reward {
            kills: amount,
            id: extra,
        }
    }
}

fn read_events(
    time: Res<Time>,
    mut events: MessageReader<FusionEventsReceived>,
    mut fusion: ResMut<Fusion>,
    mut weapon: ResMut<Weapon>,
    mut streak: MessageWriter<ServerStreak>,
    loadout: Res<Loadout>,
    mut weapon_hits: MessageWriter<crate::equipment::WeaponHit>,
    mut incoming: MessageWriter<IncomingFire>,
) {
    let t = time.elapsed_secs();
    for FusionEventsReceived(list) in events.read() {
        for e in list {
            match e.kind {
                EventKind::Hit => {
                    weapon_hits.write(crate::equipment::WeaponHit(e.extra));
                    fusion.last_hit = t;
                    if e.flags & EVENT_HEADSHOT != 0 {
                        fusion.last_headshot = t;
                    }
                }
                EventKind::Kill => {
                    fusion.kills += 1;
                    debug!(
                        "fusion: kill {:#x}{}",
                        e.guid,
                        if e.flags & EVENT_HEADSHOT != 0 {
                            " (headshot)"
                        } else {
                            ""
                        }
                    );
                }
                EventKind::Hurt => {
                    incoming.write(IncomingFire {
                        guid: e.guid,
                        gun: e.extra,
                        hit: e.amount > 0,
                    });
                    if e.flags & EVENT_FATAL != 0 {
                        debug!("fusion: killed by {:#x}", e.guid);
                    }
                }
                EventKind::StreakEarned => {
                    streak.write(server_streak(e.flags, e.amount, e.extra));
                }
                // The server's count wins: it is the one that refuses an empty magazine.
                EventKind::Ammo => {
                    let left = e.flags & EVENT_LEFT != 0;
                    // The reply to our reload: the magazine it left lands when our reload ends.
                    // A shell-by-shell reload's shells land as they go in.
                    match (weapon.reloading() && weapon.shell.is_none(), left) {
                        (true, false) => weapon.server_mag = Some(e.amount),
                        (true, true) => weapon.server_mag_left = Some(e.amount),
                        (false, false) => weapon.mag = e.amount,
                        (false, true) => weapon.mag_left = e.amount,
                    }
                    weapon.server_reserve = Some(u32::from(e.extra));
                    weapon.reserve_gun = Some(loadout.mw2_weapon.clone());
                }
                EventKind::Revived => {}
                // For the target-on-hit (crate::shot_target).
                EventKind::Struck => {}
            }
        }
    }
}

/// How quickly the measured speed follows the body (seconds).
pub const SPEED_TAU: f32 = 0.06;

/// `prev` eased toward `target` over `dt` with time constant `tau`.
pub fn smooth_toward(prev: f32, target: f32, dt: f32, tau: f32) -> f32 {
    if dt <= 0.0 {
        return prev;
    }
    prev + (target - prev) * (1.0 - (-dt / tau).exp())
}

/// A fall in progress: a landing is a fall that stops after at least `MIN_FALL_SECS`, not a
/// frame of descent on a slope or a bump.
#[derive(Debug, Default, Clone, Copy)]
pub struct Fall {
    since: Option<f32>,
}

const MIN_FALL_SECS: f32 = 0.15;

impl Fall {
    /// This frame's vertical speed: whether it lands.
    pub fn step(&mut self, vy: f32, t: f32) -> bool {
        if vy < -2.0 {
            self.since.get_or_insert(t);
            false
        } else {
            self.since.take().is_some_and(|s| t - s >= MIN_FALL_SECS)
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_swing_goes_up_as_the_servers_plain_melee_shot() {
        let swing = super::MeleeSwing {
            ray: bevy::math::Ray3d::new(bevy::math::Vec3::ZERO, bevy::math::Dir3::X),
            target: Some(0xF130_0000_0000_0042),
        };
        let shot = super::swing_shot(1234, super::Stance::Stand, true, &swing);
        assert_eq!(shot.weapon_id, super::SWING_WEAPON_ID);
        assert_eq!(super::SWING_WEAPON_ID, 4999, "Fusion::Melee::kSwingWeaponId");
        assert_eq!(shot.target_guid, 0xF130_0000_0000_0042);
        assert_eq!(shot.flags & super::SHOT_AIRBORNE, super::SHOT_AIRBORNE);
        assert_eq!(shot.time_ms, 1234);
    }

    #[test]
    fn speed_noise_is_smoothed() {
        use super::smooth_toward;
        // Frame-time jitter alternates the measured speed 5 and 9 around a steady 7.
        let mut v = 7.0;
        for i in 0..60 {
            v = smooth_toward(
                v,
                if i % 2 == 0 { 5.0 } else { 9.0 },
                1.0 / 144.0,
                super::SPEED_TAU,
            );
        }
        assert!((v - 7.0).abs() < 0.3, "{v}");
        // A real stop still settles within a quarter second.
        for _ in 0..36 {
            v = smooth_toward(v, 0.0, 1.0 / 144.0, super::SPEED_TAU);
        }
        assert!(v < 0.5, "{v}");
    }

    #[test]
    fn a_landing_needs_a_real_fall() {
        let mut fall = super::Fall::default();
        assert!(!fall.step(-3.0, 0.00)); // a bump: one falling frame
        assert!(!fall.step(0.0, 0.01));
        assert!(!fall.step(-5.0, 1.0)); // a drop
        assert!(!fall.step(-8.0, 1.3));
        assert!(fall.step(0.0, 1.35)); // lands
        assert!(!fall.step(0.0, 1.4));
    }

    #[test]
    fn leaving_mw2_mode_ends_the_sprint() {
        use super::{Gait, gait_without_mw2};
        assert_eq!(gait_without_mw2(Gait::Sprint), Gait::Run);
        assert_eq!(gait_without_mw2(Gait::Walk), Gait::Walk);
    }

    #[test]
    fn streak_events_are_progress_or_rewards() {
        use super::{ServerStreak, server_streak};
        assert_eq!(server_streak(0x08, 34, 0), ServerStreak::Progress(34));
        assert_eq!(
            server_streak(0, 5, 5),
            ServerStreak::Reward { kills: 5, id: 5 }
        );
    }
    use super::*;

    #[test]
    fn a_mantle_and_a_lunge_go_up_one_at_a_time() {
        assert_eq!(split_moves(true, true), (true, false, true)); // the lunge waits a message
        assert_eq!(split_moves(false, true), (false, true, false));
        assert_eq!(split_moves(true, false), (true, false, false));
    }

    #[test]
    fn state_flags_carry_the_move_requests() {
        let plain = state_flags(false, false, false, false, false, false);
        assert_eq!(plain, STATE_FFA_OFF);
        let mantle = state_flags(false, false, false, false, true, false);
        assert_eq!(mantle & STATE_MANTLE, STATE_MANTLE);
        assert_eq!(mantle & STATE_LUNGE, 0);
        let lunge = state_flags(true, true, false, false, false, true);
        assert_eq!(lunge, STATE_AIMING | STATE_FFA_ON | STATE_LUNGE);
        // The server's values (Packets.h).
        assert_eq!((STATE_MANTLE, STATE_LUNGE), (0x20, 0x40));
    }

    #[test]
    fn the_seed_table_parses() {
        let guns = parse_guns(SEED_WEAPONS);
        assert!(guns.len() >= 40);
        let m4 = guns["m4_mp"];
        assert_eq!(m4.mag_size, 30);
        assert!((m4.fire_time - 0.07).abs() < 1e-4); // MW2's own 857 rounds a minute
        assert_eq!(guns["spas12_mp"].pellets, 8);
        assert!(guns["famas_mp"].starter && guns["famas_mp"].unlocked(1));
        assert!(!guns["ak47_mp"].unlocked(59) && guns["ak47_mp"].unlocked(60));
        assert_eq!(guns["m4_mp"].unlock_level(), 4);
        let mut ids: Vec<u16> = guns.values().map(|g| g.id).collect();
        ids.sort_unstable();
        ids.dedup();
        assert_eq!(ids.len(), guns.len());
        // 43 guns, their 18 underbarrel weapons, 8 equipment, 8 killstreaks; no camo or
        // attachment taken for a gun.
        assert_eq!(guns.len(), 77);
    }

    #[test]
    fn axes_match_benilla() {
        // WoW north (+X) is Bevy −Z; WoW up (+Z) is Bevy +Y.
        assert_eq!(to_wow(Vec3::new(0.0, 0.0, -1.0)), [1.0, 0.0, 0.0]);
        assert_eq!(to_wow(Vec3::new(0.0, 1.0, 0.0)), [0.0, 0.0, 1.0]);
        assert_eq!(to_wow(Vec3::new(-1.0, 0.0, 0.0)), [0.0, 1.0, 0.0]);
    }
}
