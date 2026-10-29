//! Hits, kills and damage taken, read back from the object mirror the server keeps current: a
//! unit we fired on whose health drops took a hit, and one whose health reaches zero while we
//! were on it is a kill. Our own health dropping is damage taken. This needs nothing from the
//! combat log and so nothing private to benilla.

use benilla_app::Objects;
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::mode::FpsState;
use crate::weapon::{ShotAt, Weapon};

/// Seconds after the last shot a unit's health is still credited to us.
const CREDIT_SECS: f32 = 10.0;

/// What the HUD and the streaks react to.
#[derive(Message, Clone, Debug)]
pub enum CombatEvent {
    /// A unit we fired on lost health.
    Hit { guid: u64, amount: u32, fatal: bool },
    /// A unit we hit died.
    Kill {
        guid: u64,
        name: String,
        /// Yards when last fired on.
        distance: f32,
        /// Every point of its health went in one hit.
        one_shot: bool,
    },
    /// We lost health; `fraction` is what is left.
    Hurt { fraction: f32 },
    /// We died.
    Died,
}

#[derive(Debug)]
struct Engagement {
    name: Option<String>,
    distance: f32,
    last_shot: f32,
    health: Option<u32>,
    hits: u32,
}

/// The units we are fighting, and our own health.
#[derive(Resource, Debug, Default)]
pub struct Combat {
    engaged: HashMap<u64, Engagement>,
    self_health: Option<u32>,
    self_dead: bool,
    /// Our health over our maximum, 1 when unknown.
    pub health_fraction: f32,
    /// Elapsed seconds of the last hit taken.
    pub last_hurt: f32,
}

impl Combat {
    /// The name of a unit we are fighting, as it was when we fired on it.
    pub fn name_of(&self, guid: u64) -> Option<&str> {
        self.engaged.get(&guid).and_then(|e| e.name.as_deref())
    }

    /// The units fired on recently, for the radar.
    pub fn engaged(&self) -> impl Iterator<Item = u64> + '_ {
        self.engaged.keys().copied()
    }
}

pub struct CombatPlugin;

impl Plugin for CombatPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(Combat {
            health_fraction: 1.0,
            last_hurt: -10.0,
            ..default()
        })
        .add_message::<CombatEvent>()
        .add_systems(Update, track_combat.in_set(Mw2Set::Combat));
    }
}

fn track_combat(
    time: Res<Time>,
    state: Res<FpsState>,
    weapon: Res<Weapon>,
    mut combat: ResMut<Combat>,
    mut shots: MessageReader<ShotAt>,
    objects: Objects,
    mut events: MessageWriter<CombatEvent>,
) {
    let t = time.elapsed_secs();
    if !state.in_world {
        combat.engaged.clear();
        combat.self_health = None;
        combat.self_dead = false;
        combat.health_fraction = 1.0;
        shots.clear();
        return;
    }
    let combat = &mut *combat;

    for shot in shots.read() {
        let health = objects.object(shot.guid).and_then(|f| f.unit_health());
        let e = combat.engaged.entry(shot.guid).or_insert(Engagement {
            name: None,
            distance: shot.distance,
            last_shot: t,
            health,
            hits: 0,
        });
        e.last_shot = t;
        e.distance = shot.distance;
        if shot.name.is_some() {
            e.name.clone_from(&shot.name);
        }
    }
    // A weapon still firing keeps its target credited.
    if weapon.auto_on
        && let Some(e) = weapon.targeted.and_then(|g| combat.engaged.get_mut(&g))
    {
        e.last_shot = t;
    }

    combat.engaged.retain(|&guid, e| {
        let Some(fields) = objects.object(guid) else {
            // Out of range or despawned.
            return false;
        };
        let now = fields.unit_health();
        if let (Some(before), Some(now)) = (e.health, now)
            && now < before
        {
            e.hits += 1;
            events.write(CombatEvent::Hit {
                guid,
                amount: before - now,
                fatal: now == 0,
            });
            if now == 0 {
                let max = fields.unit_max_health().unwrap_or(before);
                events.write(CombatEvent::Kill {
                    guid,
                    name: e.name.clone().unwrap_or_else(|| "Enemy".into()),
                    distance: e.distance,
                    one_shot: e.hits == 1 && before >= max,
                });
                return false;
            }
        }
        e.health = now;
        t - e.last_shot < CREDIT_SECS
    });

    // Our own health.
    let mine = state
        .self_guid
        .and_then(|g| objects.object(g))
        .and_then(|f| Some((f.unit_health()?, f.unit_max_health().unwrap_or(1).max(1))));
    if let Some((health, max)) = mine {
        let fraction = health as f32 / max as f32;
        combat.health_fraction = fraction;
        if combat.self_health.is_some_and(|before| health < before) {
            combat.last_hurt = t;
            events.write(CombatEvent::Hurt { fraction });
        }
        if health == 0 && !combat.self_dead {
            combat.self_dead = true;
            events.write(CombatEvent::Died);
        } else if health > 0 {
            combat.self_dead = false;
        }
        combat.self_health = Some(health);
    }
}
