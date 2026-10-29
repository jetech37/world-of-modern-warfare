//! A test probe that drives the layer for measured runs, off unless its variables are set:
//!
//! - `MW2_PROBE_MOVE=1`: once the body is moving (hold W with benilla's own key probe,
//!   `WOW_PROBE_KEY=W@5:28`), cycle stand, crouch, prone, stand, sprint and stand every 4 s,
//!   logging the stance, gait, ground speed and stamina twice a second.
//! - `MW2_PROBE_FIRE=<secs>`: then hold the trigger that long, logging the magazine and the
//!   server's ammunition every 2 s.
//!
//! - `MW2_PROBE_CAC=<gun>[:<attachment>[:<camo id>[:<secondary>[:<perk ids,>]]]]`: once in the world, save that
//!   class as class 1 and equip it (Create-a-Class, crate::cac), e.g. `m4_mp:acog:1:usp_mp`.
//! - `MW2_PROBE_GUN=<variant>[@<camo model>]`: show that gun in hand without saving a class
//!   (crate::mw2), e.g. `m4_acog_mp@viewmodel_m4_woodland`.
//! - `MW2_PROBE_HAND=<0|1>@<secs>`: pick the primary or secondary that long after the gun is up.
//! - `MW2_PROBE_ADS=1`: hold aim down sights from 12 s after the gun is up; `MW2_PROBE_NO_ZOOM=1`
//!   aims without the sight's zoom (crate::weapon), to see more of the scene; `MW2_PROBE_BREATH=1`
//!   holds Shift (breath) from 4 to 10 s into the aim.
//! - `MW2_PROBE_CAC_UI=1`: open the Create-a-Class window once the gun is up (crate::cac_ui).
//! - `MW2_PROBE_SHOOT=<from>:<for>`: hold the trigger from `from` seconds after entering the
//!   world, for `for` seconds.
//! - `MW2_PROBE_SAY=<text>@<secs>[;<text>@<secs>...]`: say each line in chat (`lua:<chunk>`
//!   runs a Lua chunk instead) that long after
//!   entering the world (a GM command such as `.morph 59`).
//!
//! The server's side of a run is in `docker compose logs mangosd`.

use benilla_app::fusion::Stance;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::fusion::Fusion;
use crate::input::Mw2Input;
use crate::mode::FpsState;
use crate::sprint::Sprint;
use crate::weapon::Weapon;

pub struct ProbePlugin;

impl Plugin for ProbePlugin {
    fn build(&self, app: &mut App) {
        if std::env::var_os("MW2_PROBE_MOVE").is_some()
            || std::env::var_os("MW2_PROBE_FIRE").is_some()
        {
            app.add_systems(Update, drive.in_set(Mw2Set::Mode));
        }
        if let Some((from, hold)) = std::env::var("MW2_PROBE_SHOOT").ok().and_then(|s| {
            let (a, b) = s.split_once(':')?;
            Some((a.trim().parse::<f32>().ok()?, b.trim().parse::<f32>().ok()?))
        }) {
            app.insert_resource(ProbeShoot { from, hold })
                .add_systems(
                    Update,
                    shoot.in_set(Mw2Set::Mode).before(crate::forms::FormsSet),
                );
        }
        if let Ok(spec) = std::env::var("MW2_PROBE_SAY") {
            app.insert_resource(ProbeSay(parse_say(&spec)))
                .init_resource::<ProbeClick>()
                .add_systems(Update, say.in_set(Mw2Set::Mode))
                .add_systems(PreUpdate, click.after(bevy::ui::UiSystems::Focus))
                .init_resource::<ProbeMouse>()
                .add_systems(
                    PreUpdate,
                    nudge_mouse
                        .after(bevy::input::InputSystems)
                        .before(benilla_app::fusion::LayerInput),
                );
        }
        if let Ok(spec) = std::env::var("MW2_PROBE_BUILD") {
            app.insert_resource(ProbeBuild(parse_say(&spec)))
                .add_systems(
                    Update,
                    build.in_set(Mw2Set::Mode).before(crate::build::BuildInput),
                );
        }
        if std::env::var_os("MW2_PROBE_ADS").is_some() {
            app.add_systems(Update, hold_ads.in_set(Mw2Set::Mode));
        }
        if std::env::var_os("MW2_PROBE_HAND").is_some() {
            app.add_systems(
                Update,
                pick_hand.in_set(Mw2Set::Mode).before(crate::cac::CacSet),
            );
        }
        if let Ok(spec) = std::env::var("MW2_PROBE_CAC") {
            app.insert_resource(ProbeClass(Some(spec)))
                .add_systems(Update, send_class.in_set(Mw2Set::Mode));
        }
    }
}

/// The move phases: a name, the stance, and whether to sprint.
const PHASES: [(&str, Stance, bool); 6] = [
    ("stand", Stance::Stand, false),
    ("crouch", Stance::Crouch, false),
    ("prone", Stance::Prone, false),
    ("stand", Stance::Stand, false),
    ("sprint", Stance::Stand, true),
    ("stand", Stance::Stand, false),
];
const PHASE_SECS: f32 = 4.0;

#[allow(clippy::too_many_arguments)]
fn drive(
    time: Res<Time>,
    state: Res<FpsState>,
    mut fusion: ResMut<Fusion>,
    mut input: ResMut<Mw2Input>,
    sprint: Res<Sprint>,
    weapon: Res<Weapon>,
    mut start: Local<Option<f32>>,
    mut next_log: Local<f32>,
    mut was_firing: Local<bool>,
) {
    if !state.active {
        return;
    }
    let t = time.elapsed_secs();
    let moving = std::env::var_os("MW2_PROBE_MOVE").is_some();
    // With the move phases, start once the key probe has the body running.
    if start.is_none() && (!moving || fusion.speed > 1.0) {
        *start = Some(t + 1.0);
    }
    let Some(t0) = *start else {
        return;
    };
    if t < t0 {
        return;
    }
    let mut since = t - t0;
    let move_secs = if moving {
        PHASES.len() as f32 * PHASE_SECS
    } else {
        0.0
    };
    if moving && since < move_secs {
        let (name, stance, sprinting) = PHASES[(since / PHASE_SECS) as usize];
        fusion.stance = stance;
        input.sprint = sprinting;
        if t >= *next_log {
            *next_log = t + 0.5;
            info!(
                "probe: move phase={name} stance={:?} gait={:?} speed={:.2} sprinting={} stamina={:.2}",
                fusion.stance, fusion.gait, fusion.speed, sprint.sprinting, sprint.stamina
            );
        }
        return;
    }
    since -= move_secs;
    let fire_secs: f32 = std::env::var("MW2_PROBE_FIRE")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(0.0);
    if since < fire_secs {
        input.fire.held = true;
        input.fire.pressed = !*was_firing;
        *was_firing = true;
        if t >= *next_log {
            *next_log = t + 2.0;
            info!(
                "probe: fire t={since:.0} mag={} reloading={} server_mag={:?} server_reserve={:?} shots={}",
                weapon.mag,
                weapon.reloading(),
                weapon.server_mag,
                weapon.server_reserve,
                weapon.shots
            );
        }
    } else if *was_firing {
        input.fire.held = false;
        *was_firing = false;
        info!("probe: fire done shots={}", weapon.shots);
    }
}

#[derive(Resource)]
struct ProbeClass(Option<String>);

/// Save and equip the probe's class once the gun is up.
fn send_class(
    state: Res<FpsState>,
    catalog: Res<crate::catalog::Catalog>,
    mut probe: ResMut<ProbeClass>,
    mut classes: ResMut<crate::cac::Classes>,
    mut out: MessageWriter<benilla_app::fusion::SendFusionLoadout>,
) {
    if !state.active {
        return;
    }
    let Some(spec) = probe.0.take() else {
        return;
    };
    let mut parts = spec.split(':');
    let Some(gun) = parts.next().and_then(|g| catalog.gun_named(g)) else {
        warn!("probe: no gun in MW2_PROBE_CAC={spec}");
        return;
    };
    let class = benilla_app::fusion::ClassLoadout {
        name: "Probe".into(),
        primary: gun.id,
        primary_att1: parts.next().unwrap_or("").into(),
        primary_camo: parts.next().and_then(|c| c.parse().ok()).unwrap_or(0),
        secondary: parts
            .next()
            .and_then(|g| catalog.gun_named(g))
            .map_or(0, |g| g.id),
        perks: {
            let ids: Vec<u16> = parts
                .next()
                .map(|p| p.split(',').filter_map(|i| i.parse().ok()).collect())
                .unwrap_or_default();
            [0, 1, 2].map(|i| ids.get(i).copied().unwrap_or(0))
        },
        ..Default::default()
    };
    info!("probe: equipping {class:?}");
    crate::cac::send(&mut out, classes.equip(0, class));
}

/// `MW2_PROBE_HAND=<hand>@<secs>`: press 1 or 2 once, `secs` after the gun is first up.
fn pick_hand(
    time: Res<Time>,
    state: Res<FpsState>,
    mut input: ResMut<Mw2Input>,
    mut at: Local<Option<f32>>,
) {
    if !state.active {
        return;
    }
    let spec = std::env::var("MW2_PROBE_HAND").unwrap_or_default();
    let Some((hand, secs)) = spec.split_once('@') else {
        return;
    };
    let (Ok(hand), Ok(secs)) = (hand.parse::<u8>(), secs.parse::<f32>()) else {
        return;
    };
    let t = time.elapsed_secs();
    let when = *at.get_or_insert(t + secs);
    if when > 0.0 && t >= when {
        *at = Some(-1.0);
        input.hand = Some(hand);
        info!("probe: hand {hand}");
    }
}

/// `MW2_PROBE_ADS`: hold aim from 12 s after the gun is first up.
fn hold_ads(
    time: Res<Time>,
    state: Res<FpsState>,
    mut input: ResMut<Mw2Input>,
    mut from: Local<Option<f32>>,
) {
    if !state.active {
        return;
    }
    let t = time.elapsed_secs();
    let start = *from.get_or_insert(t + 12.0);
    if t >= start {
        input.ads.held = true;
        // MW2_PROBE_BREATH: hold Shift (breath, scoped) from 4 s to 10 s into the aim.
        if std::env::var_os("MW2_PROBE_BREATH").is_some() && (4.0..10.0).contains(&(t - start)) {
            input.sprint = true;
        }
    }
}

/// `MW2_PROBE_SAY`'s lines and when to say them (seconds after entering the world).
#[derive(Resource, Default)]
struct ProbeSay(Vec<(String, f32)>);

fn parse_say(spec: &str) -> Vec<(String, f32)> {
    spec.split(';')
        .filter_map(|part| {
            let (text, secs) = part.rsplit_once('@')?;
            Some((text.to_owned(), secs.trim().parse().ok()?))
        })
        .collect()
}

fn say(
    time: Res<Time>,
    state: Res<FpsState>,
    script: Option<NonSend<benilla_ui::script::UiScript>>,
    mut lines: ResMut<ProbeSay>,
    mut since: Local<Option<f32>>,
    objects: benilla_app::Objects,
    mut hello: MessageWriter<benilla_app::fusion::SendGossipHello>,
    mut clicks: ResMut<ProbeClick>,
    mut mouse: ResMut<ProbeMouse>,
) {
    let (Some(script), true) = (script, state.in_world) else {
        return;
    };
    let t = time.elapsed_secs();
    let start = *since.get_or_insert(t);
    lines.0.retain(|(text, at)| {
        if t - start < *at {
            return true;
        }
        info!("probe: say {text}");
        // `lua:<chunk>` runs the chunk instead (a bag item used, a frame clicked).
        if let Some((dx, dy)) = text
            .strip_prefix("mouse:")
            .and_then(|m| m.split_once(','))
            .and_then(|(x, y)| Some((x.trim().parse::<f32>().ok()?, y.trim().parse::<f32>().ok()?)))
        {
            mouse.0 += Vec2::new(dx, dy);
        } else if let Some(label) = text.strip_prefix("click:") {
            clicks.0.push(label.to_lowercase());
        } else if let Some(chunk) = text.strip_prefix("lua:") {
            crate::lua::run(&script, chunk);
        } else if let Some(entry) = text.strip_prefix("greet:").and_then(|e| e.parse::<u64>().ok()) {
            // `greet:<entry>`: open the gossip of that creature (the first one known), as a
            // right-click would; its guid carries the entry (0xF130 << 48 | entry << 24 | low).
            match objects
                .iter()
                .map(|(guid, _, _)| guid)
                .find(|g| g >> 48 == 0xF130 && (g >> 24) & 0xFF_FFFF == entry)
            {
                Some(guid) => {
                    hello.write(benilla_app::fusion::SendGossipHello(guid));
                }
                None => warn!("probe: no creature {entry} in sight"),
            }
        } else {
            crate::lua::run(
                &script,
                &format!("SendChatMessage({}, \"SAY\")", crate::lua::quote(text)),
            );
        }
        false
    });
}

/// `MW2_PROBE_SHOOT`: when to hold the trigger and for how long.
#[derive(Resource)]
struct ProbeShoot {
    from: f32,
    hold: f32,
}

fn shoot(
    time: Res<Time>,
    state: Res<FpsState>,
    probe: Res<ProbeShoot>,
    mut input: ResMut<Mw2Input>,
    mut since: Local<Option<f32>>,
    mut was: Local<bool>,
) {
    if !state.in_world {
        return;
    }
    let t = time.elapsed_secs();
    let at = t - *since.get_or_insert(t);
    let on = (probe.from..probe.from + probe.hold).contains(&at);
    if on {
        input.fire.held = true;
        input.fire.pressed = !*was;
        if !*was {
            info!("probe: shooting for {} s", probe.hold);
        }
    } else if *was {
        // Let go at the end: a charged spell in the hand casts (crate::spellcast).
        input.fire.held = false;
        input.fire.released = true;
        info!("probe: released the trigger");
    }
    *was = on;
}

/// `MW2_PROBE_BUILD="<action>@<secs>;..."`: build-mode presses at their times after entering the
/// world: `b` (in or out of build mode), `r` (turn), `place`, `remove`, `wheel<n>` (the wheel's
/// notches: the piece n yards farther out, back for negative), `next` and `prev` (] and [: pick).
#[derive(Resource)]
struct ProbeBuild(Vec<(String, f32)>);

fn build(
    time: Res<Time>,
    state: Res<FpsState>,
    mut actions: ResMut<ProbeBuild>,
    mut input: ResMut<Mw2Input>,
    mut since: Local<Option<f32>>,
    mut send: MessageWriter<benilla_app::fusion::SendFusionChannel>,
    script: Option<NonSend<benilla_ui::script::UiScript>>,
) {
    if !state.in_world {
        return;
    }
    let t = time.elapsed_secs();
    let at = t - *since.get_or_insert(t);
    while let Some((action, when)) = actions.0.first().cloned() {
        if at < when {
            break;
        }
        actions.0.remove(0);
        info!("probe: build {action}");
        match action.as_str() {
            "b" => input.build.pressed = true,
            "r" => input.reload.pressed = true,
            "place" => input.fire.pressed = true,
            "remove" => input.middle.pressed = true,
            "f" => input.interact.pressed = true,
            "next" => input.build_cycle += 1,
            "prev" => input.build_cycle -= 1,
            // Release the spirit (dead): the bedroll test.
            "release" => {
                if let Some(script) = &script {
                    crate::lua::run(script, "RepopMe()");
                }
            }
            // An admin's raid on their own base, now.
            "endraid" => {
                send.write(benilla_app::fusion::SendFusionChannel(
                    benilla_app::fusion::ChannelMessage {
                        channel: benilla_app::fusion::CHANNEL_WORLD,
                        body: vec![10],
                    },
                ));
            }
            "raid" => {
                send.write(benilla_app::fusion::SendFusionChannel(
                    benilla_app::fusion::ChannelMessage {
                        channel: benilla_app::fusion::CHANNEL_WORLD,
                        body: vec![9],
                    },
                ));
            }
            w => {
                if let Some(n) = w.strip_prefix("wheel").and_then(|n| n.parse::<f32>().ok()) {
                    input.wheel = n;
                }
            }
        }
    }
}

/// `click:<label>` lines waiting for the next UI pass.
#[derive(Resource, Default)]
struct ProbeClick(Vec<String>);

/// Press the first layer button whose text holds the label, as a mouse click would (the frame's
/// `Interaction` is `Pressed` when the windows' click systems run).
fn click(
    mut clicks: ResMut<ProbeClick>,
    mut buttons: Query<(Entity, &mut Interaction, &InheritedVisibility)>,
    children: Query<&Children>,
    texts: Query<&Text>,
) {
    if clicks.0.is_empty() {
        return;
    }
    for label in std::mem::take(&mut clicks.0) {
        let hit = buttons.iter().find_map(|(e, _, vis)| {
            if !vis.get() {
                return None;
            }
            let says = std::iter::once(e)
                .chain(children.iter_descendants(e))
                .filter_map(|c| texts.get(c).ok())
                .any(|t| t.0.to_lowercase().contains(&label));
            says.then_some(e)
        });
        match hit {
            Some(e) => {
                if let Ok((_, mut i, _)) = buttons.get_mut(e) {
                    *i = Interaction::Pressed;
                }
                info!("probe: clicked {label:?}");
            }
            None => warn!("probe: no button says {label:?}"),
        }
    }
}

/// `mouse:<dx>,<dy>`: raw mouse travel for the next frame, as a hand on the mouse (look down: +dy).
#[derive(Resource, Default)]
struct ProbeMouse(Vec2);

fn nudge_mouse(
    mut pending: ResMut<ProbeMouse>,
    mut motion: ResMut<bevy::input::mouse::AccumulatedMouseMotion>,
) {
    if pending.0 != Vec2::ZERO {
        motion.delta += pending.0;
        pending.0 = Vec2::ZERO;
    }
}
