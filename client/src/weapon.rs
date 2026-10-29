//! The gun. Pulling the trigger on a unit targets it through benilla's interact path (what a
//! right click on it does) and casts the loadout's spell at it a couple of frames later, once the
//! selection has reached the server; the server decides whether it hits. On top of that the
//! weapon keeps the MW2 feel client side: a magazine and reload, recoil that kicks the camera,
//! crosshair bloom and aim down sights, which narrows the field of view.

use benilla_app::Interact;
use benilla_ui::script::UiScript;
use benilla_world::view::{CAM_FOVY, WorldCamera};
use bevy::prelude::*;

use crate::aim::Aim;
use crate::config::{FireMode, Loadout};
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::{Mw2Set, lua};

/// Frames between targeting a new unit and casting at it: the interact runs in benilla's target
/// chain and the cast reads the selection it leaves.
const SELECT_FRAMES: u8 = 2;
/// Seconds a reload takes until the MW2 viewmodel supplies its own reload clip's length.
const RELOAD_SECS: f32 = 1.6;
/// How far aim down sights narrows the field of view.
const ADS_ZOOM: f32 = 0.45;
/// Aim down sights, in seconds to full.
const ADS_SECS: f32 = 0.18;
/// How long a tapped auto-repeat weapon keeps firing: Auto Shot's windup.
const TAP_SECS: f32 = 0.7;
/// Seconds between recasts while a [`FireMode::Spam`] trigger is held.
const SPAM_RECAST: f32 = 0.25;

/// A unit was fired on: the combat tracker starts watching its health.
#[derive(Message, Clone, Debug)]
pub struct ShotAt {
    pub guid: u64,
    pub name: Option<String>,
    /// Yards from the camera when fired on.
    pub distance: f32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Action {
    Fire,
    Lethal,
    Knife,
}

#[derive(Clone, Copy, Debug)]
struct Pending {
    action: Action,
    guid: u64,
    distance: f32,
    frames: u8,
}

/// The weapon's state, read by the HUD.
#[derive(Resource, Debug)]
pub struct Weapon {
    /// Rounds in the magazine (akimbo: the right gun's).
    pub mag: u32,
    /// Akimbo: the left gun's magazine. MW2's pair each keep their own.
    pub mag_left: u32,
    /// A shell-by-shell reload in progress (the shotguns): each shell lands in the magazine as
    /// it goes in, and firing or sprinting stops the reload with what is loaded.
    pub shell: Option<ShellReload>,
    /// Shells loaded that the server has not heard of yet (`STATE_RELOAD_ONE`, crate::fusion).
    pub reload_one: u32,
    /// Reloads finished that the server has not heard of yet (`STATE_RELOADING`): it fills the
    /// magazine when told, so a reload cut short (a gun swap) fills nothing.
    pub reload_done: u32,
    /// Seconds a reload takes when the gun's WeaponDef gives no time (its reload clip's length).
    pub reload_secs: f32,
    /// The gun's own reload timing, from its WeaponDef.
    pub timing: ReloadTiming,
    /// The segments (shells) the reload in progress loads.
    pub reload_segments: u32,
    /// When the reload in progress finishes, in elapsed seconds.
    pub reload_until: Option<f32>,
    /// The auto-repeat spell is running.
    pub auto_on: bool,
    /// When it was started, so a tap still lets the first shot go.
    auto_started: f32,
    /// The unit last targeted through the interact path.
    pub targeted: Option<u64>,
    /// Camera kick still to recover, in degrees of pitch.
    pub recoil: f32,
    /// The spread cone now (half-angle, degrees): MW2's, from crate::spread. The crosshair opens
    /// with it and each pull's pellets scatter in it.
    pub spread: f32,
    /// Aim down sights, 0 (hip) to 1 (fully aimed).
    pub ads: f32,
    /// The gun's aimed field of view (MW2's `adsZoomFov`), when the viewmodel knows it.
    pub ads_fov: Option<f32>,
    /// The gun's full-screen scope (a sniper scope, the thermal sight), from its definition.
    pub scope: Option<std::sync::Arc<crate::mw2::load::Scope>>,
    /// The class's Sleight of Hand and Steady Aim (crate::cac::ClientPerks).
    pub sleight_of_hand: bool,
    pub steady_aim: bool,
    /// Steady Aim's Pro: the hip cone stays tight in the air.
    pub steady_aim_pro: bool,
    /// Commando's lunge reach.
    pub commando: bool,
    /// Real ammunition in the ammo slot (hunters), else `None`.
    pub reserve: Option<u32>,
    /// With an Azeroth Warfare server: the magazine its reload left (`SMSG_FUSION_EVENTS` Ammo),
    /// waiting for our reload to finish, and its reserve. The server's count wins: it refuses a
    /// shot from an empty magazine.
    pub server_mag: Option<u32>,
    /// The same for an akimbo pair's left gun.
    pub server_mag_left: Option<u32>,
    pub server_reserve: Option<u32>,
    /// The gun `server_reserve` was sent for.
    pub reserve_gun: Option<String>,
    /// Shots fired this session.
    pub shots: u64,
    /// Akimbo: the gun the last shot came from (0 the right, on the right button; 1 the left, on
    /// the left button), as MW2 gives each gun its own trigger.
    pub shot_hand: u8,
    /// Knife presses this session, and whether the last one lunged, for the viewmodel's clip.
    pub knife_swings: u64,
    /// The last swing was the off hand's (a dual wielder's right button, crate::hands).
    pub knife_left: bool,
    pub last_knife_lunged: bool,
    /// Trigger pulls on an empty gun with nothing to reload, for MW2's dry-fire click.
    pub dry_clicks: u64,
    /// MW2's own foley plays (crate::mw2::sound), so WoW's UI sounds no longer stand in for the
    /// reload and the click.
    pub foley: bool,
    /// Elapsed seconds of the last shot, for the HUD flash.
    pub last_shot: f32,
    pending: Option<Pending>,
    next_shot_at: f32,
    last_cast: f32,
    ranged_speed: Option<f32>,
    poll_at: f32,
    /// The magazine size it was filled for, so a new loadout refills it.
    filled_for: u32,
    /// The trigger's state for semi-automatic and burst guns.
    trigger: Trigger,
    /// The gun's MW2 spread numbers (set as its viewmodel comes up), the aim-spread scale, and
    /// last frame's aim for the turn rate.
    pub spread_stats: crate::spread::SpreadStats,
    /// Two guns in hand (akimbo): MW2 does not aim them.
    pub akimbo: bool,
    aim_spread: crate::spread::Spread,
    last_dir: Option<Vec3>,
    /// The knife's (and the shield bash's) recovery.
    pub(crate) knife_gate: KnifeGate,
    /// What the character's stats do with the gun, from the server (crate::handling).
    pub handling: crate::handling::Handling,
    /// The sprint (its end time) whose instant first aim has been used (150 Agility).
    instant_ads_spent_for: f32,
}

impl Default for Weapon {
    fn default() -> Self {
        Self {
            mag: 0,
            mag_left: 0,
            shell: None,
            reload_one: 0,
            reload_done: 0,
            reload_secs: RELOAD_SECS,
            timing: ReloadTiming::default(),
            reload_segments: 1,
            reload_until: None,
            auto_on: false,
            auto_started: 0.0,
            targeted: None,
            recoil: 0.0,
            spread: 0.0,
            ads: 0.0,
            ads_fov: None,
            scope: None,
            sleight_of_hand: false,
            steady_aim: false,
            steady_aim_pro: false,
            commando: false,
            reserve: None,
            server_mag: None,
            server_mag_left: None,
            server_reserve: None,
            reserve_gun: None,
            shots: 0,
            shot_hand: 0,
            knife_swings: 0,
            knife_left: false,
            last_knife_lunged: false,
            dry_clicks: 0,
            foley: false,
            last_shot: -10.0,
            pending: None,
            next_shot_at: 0.0,
            last_cast: -10.0,
            ranged_speed: None,
            poll_at: 0.0,
            filled_for: 0,
            trigger: Trigger::default(),
            spread_stats: crate::spread::SpreadStats::default(),
            akimbo: false,
            aim_spread: crate::spread::Spread::default(),
            last_dir: None,
            knife_gate: KnifeGate::default(),
            handling: crate::handling::Handling::default(),
            instant_ads_spent_for: f32::NAN,
        }
    }
}

impl Weapon {
    pub fn reloading(&self) -> bool {
        self.reload_until.is_some()
    }

    /// The server says the reserve is empty: a reload would bring nothing.
    pub fn dry(&self) -> bool {
        self.server_reserve == Some(0)
    }
}

/// A gun's reload timing, MW2's own (`WeaponDef`): the reload with rounds left and from empty,
/// or for a segmented reload (the shotguns) a start, one segment per `per_segment` rounds and an
/// end.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ReloadTiming {
    pub secs: f32,
    pub empty_secs: f32,
    pub segmented: bool,
    pub start_secs: f32,
    pub end_secs: f32,
    pub per_segment: u32,
}

impl ReloadTiming {
    /// The same reload played `scale` times as long (Sleight of Hand: 0.5).
    pub fn scaled(&self, scale: f32) -> Self {
        Self {
            secs: self.secs * scale,
            empty_secs: self.empty_secs * scale,
            start_secs: self.start_secs * scale,
            end_secs: self.end_secs * scale,
            ..*self
        }
    }

    /// The segments a reload from `mag` rounds of `mag_size` loads: one for a whole-magazine
    /// reload.
    pub fn segments(&self, mag: u32, mag_size: u32) -> u32 {
        if !self.segmented {
            return 1;
        }
        mag_size
            .saturating_sub(mag)
            .div_ceil(self.per_segment.max(1))
            .max(1)
    }

    /// How long a reload from `mag` rounds takes, `None` when the WeaponDef gives no time.
    pub fn duration(&self, mag: u32, mag_size: u32) -> Option<f32> {
        let secs = if self.segmented {
            self.start_secs + self.segments(mag, mag_size) as f32 * self.secs + self.end_secs
        } else if mag == 0 && self.empty_secs > 0.0 {
            self.empty_secs
        } else {
            self.secs
        };
        (secs > 0.0).then_some(secs)
    }
}

/// The magazine a finished reload leaves: what the server's reload left, when it said, else full.
pub fn reload_result(mag_size: u32, server_mag: Option<u32>) -> u32 {
    server_mag.map_or(mag_size, |m| m.min(mag_size))
}

/// A shell-by-shell reload: when it began, the shells it loads and how many are in.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ShellReload {
    pub started: f32,
    pub segments: u32,
    pub loaded: u32,
}

/// One gun's magazine: an akimbo pair's catalog magazine holds both guns' rounds.
pub fn hand_mag_size(mag_size: u32, akimbo: bool) -> u32 {
    if akimbo { mag_size / 2 } else { mag_size }
}

/// How many of a segmented reload's `segments` shells are in `elapsed` seconds after it began:
/// each lands at the end of its segment, after the start.
pub fn shells_loaded(elapsed: f32, timing: &ReloadTiming, segments: u32) -> u32 {
    if timing.secs <= 0.0 || elapsed < timing.start_secs {
        return 0;
    }
    (((elapsed - timing.start_secs) / timing.secs).floor() as u32).min(segments)
}

pub struct WeaponPlugin;

impl Plugin for WeaponPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Weapon>()
            .add_message::<ShotAt>()
            .add_systems(
                Update,
                (operate_weapon, aim_down_sights, kick_camera)
                    .chain()
                    .in_set(Mw2Set::Weapon),
            );
    }
}

/// The cast chunk for one action, guarded so a dead or friendly target casts nothing.
fn cast_chunk(spell: &str) -> String {
    format!(
        "if UnitExists(\"target\") and UnitCanAttack(\"player\", \"target\") \
         and not UnitIsDead(\"target\") then CastSpellByName({}) end",
        lua::quote(spell)
    )
}

/// Where a swing of the hands goes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SwingPath {
    /// The server's lunge (a grant's).
    Lunge,
    /// A tool's strike (crate::tools sends it).
    Tool,
    /// The server's own swing: a plain melee shot (crate::fusion::MeleeSwing).
    Strike,
    /// No Azeroth Warfare server: the loadout's knife spell (or WoW's attack) as before.
    Spell,
}

/// The path a swing takes. With the fusion on it never starts WoW's auto-attack.
pub fn swing_path(lunge: bool, tool: bool, fusion: bool) -> SwingPath {
    match (lunge, tool, fusion) {
        (true, ..) => SwingPath::Lunge,
        (_, true, _) => SwingPath::Tool,
        (_, _, true) => SwingPath::Strike,
        _ => SwingPath::Spell,
    }
}

fn operate_weapon(
    time: Res<Time>,
    state: Res<FpsState>,
    loadout: Res<Loadout>,
    input: Res<Mw2Input>,
    aim: Res<Aim>,
    mut weapon: ResMut<Weapon>,
    script: Option<NonSend<UiScript>>,
    mut interact: MessageWriter<Interact>,
    mut shot_at: MessageWriter<ShotAt>,
    mut fusion: ResMut<crate::fusion::Fusion>,
    mut rounds: MessageWriter<crate::fusion::FiredRound>,
    mut swings: MessageWriter<crate::fusion::MeleeSwing>,
    sprint: Res<crate::sprint::Sprint>,
    equip: Res<crate::equipment::EquipState>,
    held: Res<crate::melee::MeleeHeld>,
    hands: Res<crate::hands::Hands>,
) {
    let t = time.elapsed_secs();
    let dt = time.delta_secs();
    let w = &mut *weapon;

    let Some(script) = script.filter(|_| state.active) else {
        w.auto_on = false;
        w.pending = None;
        w.targeted = None;
        return;
    };

    // Each gun's magazine: an akimbo pair's catalog magazine is both guns'.
    let mag_size = hand_mag_size(loadout.mag_size, w.akimbo);
    // A new loadout (a new character, another gun) starts with a full magazine.
    let fill_key = loadout.mag_size * 2 + u32::from(w.akimbo);
    if w.filled_for != fill_key {
        w.filled_for = fill_key;
        w.mag = mag_size;
        w.mag_left = mag_size;
        w.reload_until = None;
        w.shell = None;
        w.server_mag = None;
        w.server_mag_left = None;
        // The server sends a gun's reserve once, when it comes into hand. A passing loadout
        // change (the WoW class's own, at login) must not drop the reserve of the gun still in
        // hand.
        if !keeps_reserve(w.reserve_gun.as_deref(), &loadout.mw2_weapon) {
            w.server_reserve = None;
        }
    }

    // The real ammunition and attack speed, twice a second.
    if t >= w.poll_at {
        w.poll_at = t + 0.5;
        if loadout.class.as_deref() == Some("HUNTER") {
            w.reserve =
                lua::eval::<Option<u32>>(&script, "return GetInventoryItemCount(\"player\", 0)")
                    .flatten();
        } else {
            w.reserve = None;
        }
        if loadout.fire_mode == FireMode::AutoRepeat {
            w.ranged_speed =
                lua::eval::<Option<f32>>(&script, "return UnitRangedDamage(\"player\")")
                    .flatten()
                    .filter(|s| *s > 0.1);
        }
    }

    // The reload: shell by shell as each goes in, or the whole magazine at the end.
    if let Some(mut sh) = w.shell {
        let timing = w.timing.scaled(w.reload_factor());
        let due = shells_loaded(t - sh.started, &timing, sh.segments);
        while sh.loaded < due {
            sh.loaded += 1;
            w.mag = (w.mag + timing.per_segment.max(1)).min(mag_size);
            w.reload_one += 1;
        }
        w.shell = Some(sh);
    }
    if w.reload_until.is_some_and(|end| t >= end) {
        w.reload_until = None;
        if w.shell.take().is_none() {
            w.reload_done += 1;
            w.mag = reload_result(mag_size, w.server_mag.take());
            if w.akimbo {
                w.mag_left = reload_result(mag_size, w.server_mag_left.take());
            }
        }
        if !w.foley {
            lua::sound(&script, "igBackPackClose");
        }
    }
    let melee = loadout.fire_mode == FireMode::Melee;
    // No trigger while sprinting nor in MW2's sprint-out (the server refuses those shots too).
    let trigger_ok = sprint.fire_ready(t);
    // Akimbo: each button its own gun (MW2 does not aim two guns): the left button the left
    // gun, the right button the right.
    // The melee weapon up (V, crate::melee): the trigger swings it and the gun stays quiet.
    // With a spell in the right hand the button casts it instead (crate::hands).
    let swing_right = held.0 && input.fire.pressed && hands.right_swings();
    // A dual wielder's off hand swings on the right button (a shield blocks there instead).
    let swing_left = held.0 && input.ads.pressed && hands.left_swings();
    let swing_pressed = (swing_right || swing_left) && trigger_ok;
    let gun_up = !held.0;
    let left_trigger = input.fire.held && gun_up;
    let right_trigger = w.akimbo && input.ads.held && gun_up;
    let fire_pressed =
        (input.fire.pressed || (w.akimbo && input.ads.pressed)) && trigger_ok && gun_up;
    let fire_held = (right_trigger || left_trigger) && trigger_ok;
    // The gun the next round comes from, None when every gun on a held trigger is empty.
    let hand = if w.akimbo {
        akimbo_hand(
            left_trigger,
            right_trigger,
            w.shot_hand,
            w.mag_left > 0,
            w.mag > 0,
        )
    } else {
        Some(0).filter(|_| w.mag > 0)
    };
    // A pair reloads when both are empty; a single gun when it is.
    let empty = w.mag == 0 && (!w.akimbo || w.mag_left == 0);
    // Firing or sprinting stops a shell-by-shell reload with the shells already in.
    if w.shell.is_some() && ((fire_pressed && w.mag > 0) || !trigger_ok) {
        w.shell = None;
        w.reload_until = None;
    }
    // With an Azeroth Warfare server the gun fires real rounds the server referees (crate::fusion)
    // rather than casting WoW's ranged spell; lethals, knives and melee keep their spells.
    // A riot shield fires nothing: its trigger bashes (below).
    let shield = fusion.enabled && loadout.shield;
    let rounds_mode = fusion.enabled && !melee && !shield;
    // Dry (the server's reserve is empty): nothing to reload until a rearm.
    let can_reload = !(rounds_mode && w.dry());
    let short = (w.mag < mag_size || (w.akimbo && w.mag_left < mag_size)) && gun_up;
    if input.reload.pressed && !w.reloading() && !melee && short && can_reload {
        start_reload(w, &script, t, mag_size);
    }

    // The trigger.
    let mut engage = |w: &mut Weapon, action: Action| match &aim.target {
        Some(target) if !target.service => {
            let fresh = w.targeted != Some(target.guid);
            // The melee path attacks through the interact itself, so it re-sends every press.
            if fresh
                || (action == Action::Fire && melee)
                || (action == Action::Knife && loadout.knife.is_none())
            {
                interact.write(Interact(target.entity));
                w.targeted = Some(target.guid);
                if fresh {
                    // A new target ends the old auto-repeat (the attack start cancels it).
                    w.auto_on = false;
                }
            }
            w.pending = Some(Pending {
                action,
                guid: target.guid,
                distance: target.distance,
                frames: if fresh { SELECT_FRAMES } else { 0 },
            });
            true
        }
        _ => false,
    };

    if fire_pressed {
        // Akimbo: the pressed gun's own magazine empty while the other still has rounds clicks.
        let dry_one = w.akimbo && hand.is_none() && !empty;
        if (w.reloading() || (empty && !can_reload) || dry_one) && !melee {
            if !w.reloading() {
                w.dry_clicks += 1;
            }
            if !w.foley {
                lua::sound(&script, "igMainMenuOptionCheckBoxOff");
            }
        } else if empty && !melee {
            start_reload(w, &script, t, mag_size);
        } else if rounds_mode {
            w.next_shot_at = w.next_shot_at.max(t);
        } else {
            engage(w, Action::Fire);
            w.next_shot_at = w.next_shot_at.max(t);
        }
    } else if fire_held && !w.reloading() && !rounds_mode {
        // Sweeping onto another unit while holding the trigger switches to it.
        if let Some(target) = aim.target.as_ref().filter(|a| !a.service)
            && w.targeted != Some(target.guid)
            && w.pending.is_none()
        {
            engage(w, Action::Fire);
        }
    }
    // A class with MW2 equipment throws it instead (crate::equipment).
    if input.lethal.pressed && loadout.lethal.is_some() && !equip.has_lethal {
        engage(w, Action::Lethal);
    }
    let bash = shield && fire_pressed;
    if swing_pressed || bash {
        // A swing is a swing: it lunges (the server picks the enemy in front, dashes and strikes)
        // only when the server grants it (a lunge time in the melee profile: a warrior's Charge
        // perk; nothing grants it today), standing, at a unit within the lunge reach. The shield
        // bashes where it stands.
        let close = aim
            .target
            .as_ref()
            .filter(|a| !a.service)
            .map(|a| a.distance);
        let in_range = crate::movement::lunge_in_range(close, w.commando);
        let granted = w.knife_gate.lunge_secs.is_some();
        // A tool in the hands strikes what it works (crate::tools): no lunge, no attack.
        let lunge = (rounds_mode || shield)
            && !bash
            && !input.tool
            && crate::melee::may_lunge(fusion.stance, in_range, granted);
        // Not while recovering from the last swing, nor in the air.
        if w.knife_gate.swing(t, fusion.airborne, lunge) {
            match swing_path(lunge, input.tool, fusion.enabled) {
                SwingPath::Lunge => {
                    fusion.lunge = true;
                }
                SwingPath::Tool => {}
                // The hands' own swing (the server's Strike: reach, cone, combos), never WoW's
                // auto-attack.
                SwingPath::Strike => {
                    if let Some(ray) = aim.ray {
                        swings.write(crate::fusion::MeleeSwing {
                            ray,
                            target: aim.target.as_ref().filter(|a| !a.service).map(|a| a.guid),
                        });
                    }
                }
                SwingPath::Spell => {
                    engage(w, Action::Knife);
                }
            }
            w.knife_swings += 1;
            w.last_knife_lunged = lunge;
            w.knife_left = swing_left && !swing_right;
        }
    }

    // Letting go stops an auto-repeat weapon (`SpellStopCasting` ends the auto-repeat first),
    // once the first shot has had its windup.
    if w.auto_on && (w.reloading() || (!fire_held && t - w.auto_started >= TAP_SECS)) {
        lua::run(&script, "SpellStopCasting()");
        w.auto_on = false;
    }

    // A queued action whose selection has settled.
    if let Some(mut p) = w.pending.take() {
        if p.frames > 0 {
            p.frames -= 1;
            w.pending = Some(p);
        } else {
            let fired = match p.action {
                Action::Fire => match loadout.fire_mode {
                    FireMode::AutoRepeat if !w.auto_on => {
                        lua::run(&script, &cast_chunk(&loadout.fire_spell));
                        w.auto_on = true;
                        w.auto_started = t;
                        true
                    }
                    FireMode::AutoRepeat => true,
                    FireMode::Spam => {
                        lua::run(&script, &cast_chunk(&loadout.fire_spell));
                        w.last_cast = t;
                        true
                    }
                    FireMode::Melee => true,
                },
                Action::Lethal => {
                    if let Some(spell) = &loadout.lethal {
                        lua::run(&script, &cast_chunk(spell));
                    }
                    true
                }
                Action::Knife => {
                    if let Some(spell) = &loadout.knife {
                        lua::run(&script, &cast_chunk(spell));
                    }
                    true
                }
            };
            if fired {
                let name =
                    lua::eval::<Option<String>>(&script, "return UnitName(\"target\")").flatten();
                shot_at.write(ShotAt {
                    guid: p.guid,
                    name,
                    distance: p.distance,
                });
            }
        }
    }

    // Spraying a cast weapon: recast while the trigger is held on the target.
    if loadout.fire_mode == FireMode::Spam
        && fire_held
        && !w.reloading()
        && w.pending.is_none()
        && w.targeted.is_some()
        && t - w.last_cast >= SPAM_RECAST
    {
        lua::run(&script, &cast_chunk(&loadout.fire_spell));
        w.last_cast = t;
    }

    // The shots the HUD plays: one per weapon cycle while the gun is firing.
    let firing = if shield {
        false
    } else if rounds_mode {
        let ready = !w.reloading() && hand.is_some();
        let next = w.next_shot_at;
        w.trigger
            .fire_now(loadout.fire_type, fire_pressed, fire_held, t, next, ready)
    } else {
        w.auto_on || (fire_held && w.targeted.is_some() && !w.reloading())
    };
    if firing && t >= w.next_shot_at && (melee || hand.is_some()) {
        let hand = hand.unwrap_or(0);
        // Akimbo's rate is both guns'; one gun alone fires at its own.
        let one_gun = w.akimbo && !(left_trigger && right_trigger);
        let interval = match loadout.fire_mode {
            _ if rounds_mode && one_gun => loadout.shot_interval * 2.0,
            _ if rounds_mode => loadout.shot_interval,
            FireMode::AutoRepeat => w.ranged_speed.unwrap_or(loadout.shot_interval),
            _ => loadout.shot_interval,
        };
        // The fusion fires at the gun's own rate; the cosmetic path keeps a 0.1 s floor.
        w.next_shot_at = t + if rounds_mode {
            interval.max(0.02)
        } else {
            interval.max(0.1)
        };
        if rounds_mode && let Some(ray) = aim.ray {
            let target = aim.target.as_ref().filter(|a| !a.service);
            rounds.write(crate::fusion::FiredRound {
                ray,
                target: target.map(|a| a.guid),
                ads: w.ads > 0.5,
                seed: benilla_app::fusion::pellet_hash(w.shots as u32, (t * 1000.0) as u32),
                spread_deg: w.spread,
                item: None,
                detonate: false,
                left: w.akimbo && hand == 1,
            });
            if let Some(a) = target {
                shot_at.write(ShotAt {
                    guid: a.guid,
                    name: None,
                    distance: a.distance,
                });
            }
        }
        w.shot_hand = hand;
        w.shots += 1;
        let burst_start = t - w.last_shot > crate::handling::BURST_GAP;
        w.last_shot = t;
        // Strength holds the kick (crate::handling).
        let kick = if melee {
            0.6
        } else {
            crate::handling::recoil_kick(1.6, &w.handling, burst_start)
        };
        w.recoil = (w.recoil + kick).min(6.0);
        let stats = spread_stats(w);
        w.aim_spread.fired(&stats);
        if !melee {
            if hand == 1 {
                w.mag_left -= 1;
            } else {
                w.mag -= 1;
            }
            if w.mag == 0 && (!w.akimbo || w.mag_left == 0) && can_reload {
                start_reload(w, &script, t, mag_size);
            }
        }
    }

    // MW2's spread: moving and turning open the cone, rest closes it (crate::spread).
    let turn_dps = match (aim.ray, w.last_dir) {
        (Some(r), Some(d)) if dt > 0.0 => r.direction.angle_between(d).to_degrees() / dt,
        _ => 0.0,
    };
    w.last_dir = aim.ray.map(|r| *r.direction);
    let stats = spread_stats(w);
    let move_frac = (fusion.speed / 7.0).clamp(0.0, 1.0);
    w.aim_spread
        .step(&stats, fusion.stance, dt, move_frac, turn_dps);
    w.spread = w.aim_spread.angle(
        &stats,
        fusion.stance,
        w.ads,
        w.steady_aim,
        fusion.airborne && !w.steady_aim_pro,
    ) * (1.0 + landing_bloom(t - fusion.landed_at));
}

/// The gun's spread numbers, or a rifle's when the install gave none, with Agility and Hit's
/// tighter hip cone and Hit's lighter bloom (crate::handling); the aimed cone is the gun's.
fn spread_stats(w: &Weapon) -> crate::spread::SpreadStats {
    let mut s = if w.spread_stats.stand_max > 0.0 {
        w.spread_stats
    } else {
        FALLBACK_SPREAD
    };
    let hip = w.handling.hip_spread;
    for v in [
        &mut s.stand_min,
        &mut s.stand_max,
        &mut s.ducked_min,
        &mut s.ducked_max,
        &mut s.prone_min,
        &mut s.prone_max,
    ] {
        *v *= hip;
    }
    s.fire_add *= w.handling.bloom;
    s
}

/// An M4's spread, for a gun without MW2 numbers.
const FALLBACK_SPREAD: crate::spread::SpreadStats = crate::spread::SpreadStats {
    stand_min: 3.0,
    stand_max: 8.0,
    ducked_min: 2.5,
    ducked_max: 7.0,
    prone_min: 2.0,
    prone_max: 6.0,
    decay_rate: 2.0,
    fire_add: 0.4,
    move_add: 1.5,
    turn_add: 0.5,
    ducked_decay: 1.05,
    prone_decay: 1.1,
    ads: 0.0,
};

/// Whether the server's reserve still applies after a loadout change: it does while the gun it
/// was sent for is the one in hand.
/// Akimbo: which gun fires the next round (1 the left, 0 the right), from the buttons held and
/// which guns have rounds: both in turn, an empty one skipped, None when none can fire.
pub fn akimbo_hand(
    left: bool,
    right: bool,
    last: u8,
    left_rounds: bool,
    right_rounds: bool,
) -> Option<u8> {
    match (left && left_rounds, right && right_rounds) {
        (true, true) => Some(1 - last.min(1)),
        (true, false) => Some(1),
        (false, true) => Some(0),
        (false, false) => None,
    }
}

pub fn keeps_reserve(reserve_gun: Option<&str>, gun_in_hand: &str) -> bool {
    reserve_gun == Some(gun_in_hand)
}

/// MW2's Sleight of Hand halves the reload.
pub fn reload_scale(sleight_of_hand: bool) -> f32 {
    if sleight_of_hand { 0.5 } else { 1.0 }
}

impl Weapon {
    /// The reload's share of the gun's own time: Sleight of Hand, then ranged attack power's
    /// handling (crate::handling).
    pub fn reload_factor(&self) -> f32 {
        reload_scale(self.sleight_of_hand) * self.handling.reload
    }
}

/// The aimed field of view as a share of the hip's: MW2's `adsZoomFov` against its 65 degrees,
/// else the layer's own zoom.
pub fn ads_ratio(ads_fov: Option<f32>) -> f32 {
    match ads_fov {
        Some(f) if f > 1.0 => (f / 65.0).clamp(0.1, 1.0),
        _ => 1.0 - ADS_ZOOM,
    }
}

/// MW2's landing penalty: the bloom widens by 0.35 for 0.3 s after touching down.
pub fn landing_bloom(since_land: f32) -> f32 {
    if (0.0..0.3).contains(&since_land) {
        0.35
    } else {
        0.0
    }
}

fn start_reload(w: &mut Weapon, script: &UiScript, t: f32, mag_size: u32) {
    let secs = w.timing.duration(w.mag, mag_size).unwrap_or(w.reload_secs) * w.reload_factor();
    w.reload_until = Some(t + secs);
    w.reload_segments = w.timing.segments(w.mag, mag_size);
    // An akimbo pair reloads both guns at once (the server's STATE_RELOADING).
    w.shell = (w.timing.segmented && !w.akimbo).then_some(ShellReload {
        started: t,
        segments: w.reload_segments,
        loaded: 0,
    });
    if w.auto_on {
        lua::run(script, "SpellStopCasting()");
        w.auto_on = false;
    }
    if !w.foley {
        lua::sound(script, "igBackPackOpen");
    }
}

/// Narrow the world camera's field of view while aiming, after benilla's spyglass has written
/// its own each frame.
fn aim_down_sights(
    time: Res<Time>,
    state: Res<FpsState>,
    input: Res<Mw2Input>,
    mut weapon: ResMut<Weapon>,
    mut cameras: Query<&mut Projection, With<WorldCamera>>,
    sprint: Res<crate::sprint::Sprint>,
    held: Res<crate::melee::MeleeHeld>,
) {
    // Nothing to aim with the melee weapon up.
    let aim_ok = sprint.fire_ready(time.elapsed_secs()) && !held.0;
    let goal = if state.active && input.ads.held && aim_ok && !weapon.reloading() && !weapon.akimbo
    {
        1.0
    } else {
        0.0
    };
    if std::env::var_os("MW2_PROBE_ADS").is_some() && input.ads.held {
        info!(
            "probe: ads active {} held {} ready {} reloading {} akimbo {} ads {:.2}",
            state.active,
            input.ads.held,
            aim_ok,
            weapon.reloading(),
            weapon.akimbo,
            weapon.ads
        );
    }
    // Agility aims quicker; with 150 Agility the first aim after a sprint is instant
    // (crate::handling).
    let t = time.elapsed_secs();
    let starting = goal > 0.0 && weapon.ads <= 0.0;
    let spent = weapon.instant_ads_spent_for == sprint.ended_at;
    let step = crate::handling::ads_step(
        time.delta_secs(),
        ADS_SECS,
        &weapon.handling,
        starting,
        t - sprint.ended_at,
        spent,
    );
    if starting && step >= 1.0 {
        weapon.instant_ads_spent_for = sprint.ended_at;
    }
    weapon.ads = if goal > weapon.ads {
        (weapon.ads + step).min(goal)
    } else {
        (weapon.ads - step).max(goal)
    };
    if !state.active || weapon.ads <= 0.0 {
        return;
    }
    for mut projection in &mut cameras {
        if let Projection::Perspective(p) = &mut *projection {
            // Scaled from what benilla wrote this frame, so a spyglass still stacks.
            let hip = p.fov.min(CAM_FOVY);
            // A test probe (crate::probe): MW2_PROBE_NO_ZOOM aims without the zoom, to see more.
            let ratio = if std::env::var_os("MW2_PROBE_NO_ZOOM").is_some() {
                1.0
            } else {
                ads_ratio(weapon.ads_fov)
            };
            p.fov = hip * (1.0 - (1.0 - ratio) * weapon.ads);
        }
    }
}

/// Kick the camera up by the recoil still to recover. benilla re-seats the camera from its own
/// yaw and pitch every frame, so the kick never accumulates into the aim.
fn kick_camera(
    time: Res<Time>,
    state: Res<FpsState>,
    mut weapon: ResMut<Weapon>,
    mut cameras: Query<&mut Transform, With<WorldCamera>>,
) {
    let dt = time.delta_secs();
    weapon.recoil *= (-9.0 * dt).exp();
    if !state.active || weapon.recoil < 0.01 {
        return;
    }
    let pitch = weapon.recoil.to_radians() * (1.0 - 0.5 * weapon.ads);
    for mut tf in &mut cameras {
        tf.rotate_local_x(pitch);
    }
}

/// Seconds before the knife swings again after a slash, and after a lunge (MW2's melee and
/// melee-charge times are about these).
pub const SLASH_SECS: f32 = 0.7;
pub const LUNGE_SECS: f32 = 1.0;

/// The knife's recovery: a swing waits for the last one to finish, and none starts in the air
/// (a lunge from a jump or a sprint-jump flung the player).
#[derive(Debug, Default, Clone, Copy)]
pub struct KnifeGate {
    ready_at: f32,
    /// The swing and lunge times of the weapon in hand (crate::melee), else the knife's.
    pub slash_secs: Option<f32>,
    pub lunge_secs: Option<f32>,
}

impl KnifeGate {
    /// Whether a swing (`lunge`: the lunge) may go out at `t`; if so it is taken.
    pub fn swing(&mut self, t: f32, airborne: bool, lunge: bool) -> bool {
        if airborne || t < self.ready_at {
            return false;
        }
        self.ready_at = t + if lunge {
            self.lunge_secs.unwrap_or(LUNGE_SECS)
        } else {
            self.slash_secs.unwrap_or(SLASH_SECS)
        };
        true
    }
}

/// MW2's pause after a burst before the next pull fires (`player_burstFireCooldown`).
pub const BURST_COOLDOWN: f32 = 0.2;

/// The trigger as MW2 reads it for the gun's fire type: automatic fires while held, a single-shot
/// gun once per pull (a pull a moment early fires when the gun is ready), a burst gun its rounds
/// per pull whether or not the trigger stays down, then cools down.
#[derive(Debug, Default, Clone, Copy)]
pub struct Trigger {
    queued: bool,
    burst_left: u8,
    cooldown_until: f32,
}

impl Trigger {
    /// Whether a round goes out now. `next_shot_at` is when the gun can fire again; `ready` is
    /// false while reloading or empty (which cancels a burst or a queued pull).
    pub fn fire_now(
        &mut self,
        fire_type: u8,
        pressed: bool,
        held: bool,
        t: f32,
        next_shot_at: f32,
        ready: bool,
    ) -> bool {
        if !ready {
            self.queued = false;
            self.burst_left = 0;
            return false;
        }
        let can = t >= next_shot_at;
        match fire_type {
            0 => held && can,
            rounds @ 2..=4 => {
                if pressed && self.burst_left == 0 && t >= self.cooldown_until {
                    self.burst_left = rounds;
                }
                let fire = self.burst_left > 0 && can;
                if fire {
                    self.burst_left -= 1;
                    if self.burst_left == 0 {
                        self.cooldown_until = t + BURST_COOLDOWN;
                    }
                }
                fire
            }
            // Single shot, and the double barrel (5): a round per pull.
            _ => {
                self.queued |= pressed;
                let fire = self.queued && can;
                if fire {
                    self.queued = false;
                }
                fire
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_swing_never_starts_wows_auto_attack_with_the_fusion_on() {
        use super::{SwingPath, swing_path};
        // The playtest bug: a Classic swing went out as the interact (WoW's auto-attack).
        assert_eq!(swing_path(false, false, true), SwingPath::Strike);
        assert_eq!(swing_path(true, false, true), SwingPath::Lunge);
        assert_eq!(swing_path(false, true, true), SwingPath::Tool);
        // Only without an Azeroth Warfare server does the old spell path stand.
        assert_eq!(swing_path(false, false, false), SwingPath::Spell);
    }

    #[test]
    fn the_knife_waits_between_swings_and_not_in_the_air() {
        let mut k = super::KnifeGate::default();
        assert!(k.swing(0.0, false, false));
        assert!(!k.swing(0.3, false, false)); // mid-slash
        assert!(k.swing(super::SLASH_SECS, false, false));
        let t = 5.0;
        assert!(!k.swing(t, true, false)); // airborne: no swing, no lunge
        assert!(k.swing(t, false, true)); // a lunge
        assert!(!k.swing(t + super::SLASH_SECS, false, false)); // a lunge takes longer
        assert!(k.swing(t + super::LUNGE_SECS, false, false));
    }

    #[test]
    fn each_akimbo_button_fires_its_own_gun() {
        use super::akimbo_hand;
        // (left button, right button, last hand, left gun has rounds, right gun has rounds)
        assert_eq!(akimbo_hand(true, false, 0, true, true), Some(1)); // left button: left gun
        assert_eq!(akimbo_hand(false, true, 1, true, true), Some(0)); // right button: right gun
        assert_eq!(akimbo_hand(true, true, 0, true, true), Some(1)); // both: in turn
        assert_eq!(akimbo_hand(true, true, 1, true, true), Some(0));
    }

    #[test]
    fn each_akimbo_gun_holds_half_the_pairs_rounds() {
        // The catalog doubles an akimbo pair's magazine; each gun holds its own (the server's
        // GunAmmo::Reset gives each hand half).
        assert_eq!(super::hand_mag_size(30, true), 15);
        assert_eq!(super::hand_mag_size(30, false), 30);
    }

    #[test]
    fn an_empty_akimbo_gun_leaves_the_other_firing() {
        use super::akimbo_hand;
        assert_eq!(akimbo_hand(true, true, 0, false, true), Some(0)); // left gun dry: right only
        assert_eq!(akimbo_hand(true, false, 0, false, true), None); // its own gun is dry: a click
        assert_eq!(akimbo_hand(false, false, 0, true, true), None); // no trigger
    }

    #[test]
    fn full_auto_fires_while_held() {
        let mut tr = super::Trigger::default();
        assert!(tr.fire_now(0, true, true, 0.0, 0.0, true));
        assert!(!tr.fire_now(0, false, true, 0.05, 0.07, true)); // not yet time
        assert!(tr.fire_now(0, false, true, 0.07, 0.07, true));
        assert!(!tr.fire_now(0, false, false, 0.2, 0.14, true));
    }

    #[test]
    fn semi_auto_fires_once_per_pull() {
        let mut tr = super::Trigger::default();
        assert!(tr.fire_now(1, true, true, 0.0, 0.0, true));
        assert!(!tr.fire_now(1, false, true, 0.5, 0.1, true)); // held: no more
        // A pull a moment early fires as soon as the gun is ready.
        assert!(!tr.fire_now(1, true, true, 0.6, 0.65, true));
        assert!(tr.fire_now(1, false, false, 0.65, 0.65, true));
    }

    #[test]
    fn a_burst_fires_its_rounds_then_cools_down() {
        let mut tr = super::Trigger::default();
        let gap = 0.06;
        let mut t = 0.0;
        let mut next = 0.0;
        let mut fired = 0;
        // One quick pull: three rounds, even with the trigger let go.
        for i in 0..40 {
            let pressed = i == 0;
            if tr.fire_now(3, pressed, pressed, t, next, true) {
                fired += 1;
                next = t + gap;
            }
            t += 0.01;
        }
        assert_eq!(fired, 3);
        // Held through: no second burst without a new pull.
        assert!(!tr.fire_now(3, false, true, 1.0, 0.0, true));
        // A new pull inside the cooldown waits it out.
        let mut tr = super::Trigger::default();
        for k in 0..3 {
            assert!(tr.fire_now(3, k == 0, true, k as f32 * gap, k as f32 * gap, true));
        }
        let end = 2.0 * gap;
        assert!(!tr.fire_now(3, true, true, end + 0.1, 0.0, true));
        assert!(tr.fire_now(3, true, true, end + super::BURST_COOLDOWN, 0.0, true));
    }

    #[test]
    fn a_double_barrel_fires_one_barrel_per_pull() {
        let mut tr = super::Trigger::default();
        assert!(tr.fire_now(5, true, true, 0.0, 0.0, true)); // MW2's fire type 5: the Ranger
        assert!(!tr.fire_now(5, false, true, 0.5, 0.15, true));
    }

    #[test]
    fn a_reload_cancels_the_burst() {
        let mut tr = super::Trigger::default();
        assert!(tr.fire_now(3, true, true, 0.0, 0.0, true));
        assert!(!tr.fire_now(3, false, true, 0.1, 0.0, false)); // reloading
        assert!(!tr.fire_now(3, false, true, 0.2, 0.0, true)); // the burst is gone
    }

    use super::cast_chunk;

    #[test]
    fn a_scaled_shotgun_reload_keeps_its_end() {
        use super::{ReloadTiming, reload_scale};
        let spas = ReloadTiming {
            secs: 0.667,
            segmented: true,
            start_secs: 1.0,
            end_secs: 1.0,
            per_segment: 1,
            ..Default::default()
        };
        let fast = spas.scaled(reload_scale(true));
        // The whole reload halves, end clip included, so the segments still fit inside it.
        let whole = spas.duration(5, 8).unwrap();
        assert!((fast.duration(5, 8).unwrap() - whole * 0.5).abs() < 1e-5);
    }

    #[test]
    fn a_passing_loadout_change_keeps_the_reserve() {
        use super::keeps_reserve;
        assert!(keeps_reserve(Some("m4_mp"), "m4_mp"));
        assert!(!keeps_reserve(Some("m4_mp"), "usp_mp"));
        assert!(!keeps_reserve(None, "m4_mp"));
    }

    #[test]
    fn sleight_of_hand_halves_the_reload() {
        use super::reload_scale;
        assert_eq!(reload_scale(true), 0.5);
        assert_eq!(reload_scale(false), 1.0);
    }

    #[test]
    fn the_sight_sets_the_zoom() {
        use super::ads_ratio;
        assert!((ads_ratio(Some(30.0)) - 30.0 / 65.0).abs() < 1e-6); // an ACOG
        assert!((ads_ratio(Some(50.0)) - 50.0 / 65.0).abs() < 1e-6); // iron sights
        assert!((ads_ratio(None) - 0.55).abs() < 1e-6);
        assert!((ads_ratio(Some(0.0)) - 0.55).abs() < 1e-6);
    }

    #[test]
    fn a_magazine_reload_takes_mw2s_times() {
        use super::ReloadTiming;
        let m4 = ReloadTiming {
            secs: 2.0,
            empty_secs: 2.5,
            ..Default::default()
        };
        assert_eq!(m4.duration(12, 30), Some(2.0));
        assert_eq!(m4.duration(0, 30), Some(2.5)); // from empty: the chamber too
        assert_eq!(m4.segments(0, 30), 1);
        assert_eq!(ReloadTiming::default().duration(0, 30), None); // no WeaponDef: the clip's
    }

    #[test]
    fn a_shotgun_loads_shell_by_shell() {
        use super::ReloadTiming;
        let spas = ReloadTiming {
            secs: 0.5,
            segmented: true,
            start_secs: 0.6,
            end_secs: 0.8,
            per_segment: 1,
            ..Default::default()
        };
        assert_eq!(spas.segments(5, 8), 3);
        assert!((spas.duration(5, 8).unwrap() - (0.6 + 3.0 * 0.5 + 0.8)).abs() < 1e-6);
        assert_eq!(spas.segments(8, 8), 1); // never less than one shell
        let two = ReloadTiming {
            per_segment: 2,
            ..spas
        };
        assert_eq!(two.segments(3, 8), 3); // 5 rounds, 2 a segment
    }

    #[test]
    fn shells_go_in_one_by_one() {
        use super::{ReloadTiming, shells_loaded};
        let spas = ReloadTiming {
            secs: 0.5,
            segmented: true,
            start_secs: 0.6,
            end_secs: 0.8,
            per_segment: 1,
            ..Default::default()
        };
        assert_eq!(shells_loaded(0.5, &spas, 3), 0); // still opening the gate
        assert_eq!(shells_loaded(1.0, &spas, 3), 0); // the first shell is on its way
        assert_eq!(shells_loaded(1.1, &spas, 3), 1); // in at the end of its segment
        assert_eq!(shells_loaded(1.65, &spas, 3), 2);
        assert_eq!(shells_loaded(9.0, &spas, 3), 3); // never more than the reload loads
    }

    #[test]
    fn reload_takes_the_servers_count() {
        use super::reload_result;
        assert_eq!(reload_result(8, None), 8); // no word from the server: a full magazine
        assert_eq!(reload_result(8, Some(3)), 3); // only 3 rounds left in reserve
        assert_eq!(reload_result(8, Some(0)), 0); // dry
        assert_eq!(reload_result(8, Some(40)), 8); // never more than the magazine holds
    }

    #[test]
    fn landing_widens_for_0_3s() {
        use super::landing_bloom;
        assert_eq!(landing_bloom(0.1), 0.35);
        assert_eq!(landing_bloom(0.29), 0.35);
        assert_eq!(landing_bloom(0.31), 0.0);
        assert_eq!(landing_bloom(-1.0), 0.0); // never landed yet
    }

    #[test]
    fn cast_is_guarded() {
        let c = cast_chunk("Auto Shot");
        assert!(c.contains("UnitCanAttack"));
        assert!(c.contains("CastSpellByName(\"Auto Shot\")"));
    }
}
