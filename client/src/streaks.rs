//! Killstreaks, MW2's rules: kills in a row without dying earn the rewards below. There are no
//! points: a kill shows in the kill feed and counts toward the streak and the record. What a reward does is only what a client can do on
//! its own: the UAV lights up every hostile on the radar, and the tactical nuke gets its countdown
//! and flash. The air support, the rocket and the turret are the server's craft
//! (crate::lore_streaks steers the rocket and places the turret).

use bevy::prelude::*;

use crate::Mw2Set;
use crate::combat::CombatEvent;
use crate::mode::FpsState;

/// The rewards, at the kills in a row that earn them.
pub const REWARDS: [(u32, &str, &str); 8] = [
    (3, "UAV", "Enemy positions revealed on the radar"),
    (5, "SENTRY GUN", "Set the turret down within 6 yards"),
    (5, "PREDATOR MISSILE", "Steer the rocket down from the sky"),
    (
        7,
        "HARRIER STRIKE",
        "Bombs on your mark, then a hunter overhead",
    ),
    (7, "ATTACK HELICOPTER", "A gunship circles you for a minute"),
    (9, "STEALTH BOMBER", "A bombing run along your aim"),
    (
        11,
        "CHOPPER GUNNER",
        "A heavy gunship circles you for a minute",
    ),
    (25, "TACTICAL NUKE", "Tactical nuke incoming"),
];

/// Seconds a UAV sweeps for.
const UAV_SECS: f32 = 30.0;
/// The nuke's countdown.
const NUKE_SECS: f32 = 10.0;

/// A big centered announcement.
#[derive(Message, Clone, Debug)]
pub struct Banner {
    pub title: String,
    pub subtitle: String,
}

/// A kill feed line.
#[derive(Message, Clone, Debug)]
pub struct FeedLine(pub String);

/// A sound kit to play, by name.
#[derive(Message, Clone, Debug)]
pub struct Sound(pub &'static str);

#[derive(Resource, Debug)]
pub struct Streak {
    /// Kills since the last death.
    pub kills: u32,
    /// The same in tenths of a kill: with an Azeroth Warfare server, its count (PvP only: an
    /// enemy player is worth one kill, a tenth less per level below the killer, at least a
    /// tenth; creatures add nothing), else ten a kill.
    pub tenths: u32,
    /// Hardline: every reward a kill sooner.
    pub hardline: bool,
    pub total_kills: u32,
    pub deaths: u32,
    pub best: u32,
    /// The UAV sweeps until this elapsed time.
    pub uav_until: f32,
    /// Rewards earned and not yet called in (4 calls in the last), as `REWARDS` names them.
    pub ready: Vec<&'static str>,
    /// The nuke detonates at this elapsed time.
    pub nuke_at: Option<f32>,
    /// The detonation's white-out, 1 to 0.
    pub flash: f32,
    /// A hit landed at this elapsed time; `fatal` marks a kill shot, for the hitmarker colour.
    pub hit_at: f32,
    pub hit_fatal: bool,
}

impl Default for Streak {
    fn default() -> Self {
        Self {
            kills: 0,
            tenths: 0,
            hardline: false,
            total_kills: 0,
            deaths: 0,
            best: 0,
            uav_until: -1.0,
            ready: Vec::new(),
            nuke_at: None,
            flash: 0.0,
            hit_at: -10.0,
            hit_fatal: false,
        }
    }
}

impl Streak {
    /// The next reward still to earn this life.
    pub fn next_reward(&self) -> Option<&'static (u32, &'static str, &'static str)> {
        REWARDS
            .iter()
            .find(|(n, ..)| reward_need(*n, self.hardline) > self.kills)
    }

    /// A reward called in at `t`: what the client itself does with it (the UAV's sweep, the
    /// nuke's countdown); the server strikes for the rest.
    pub fn call_in(&mut self, reward: &str, t: f32) {
        match reward {
            "UAV" => self.uav_until = t + UAV_SECS,
            "TACTICAL NUKE" => self.nuke_at = Some(t + NUKE_SECS),
            _ => {}
        }
    }

    pub fn uav_active(&self, t: f32) -> bool {
        t < self.uav_until
    }

    /// The streak as the HUD shows it: whole kills, or "3.4" with a fraction.
    pub fn label(&self) -> String {
        if self.tenths.is_multiple_of(10) {
            format!("{}", self.tenths / 10)
        } else {
            format!("{}.{}", self.tenths / 10, self.tenths % 10)
        }
    }

    /// Set the streak to `tenths`.
    pub fn set_tenths(&mut self, tenths: u32) {
        self.tenths = tenths;
        self.kills = tenths / 10;
        self.best = self.best.max(self.kills);
    }
}

/// A reward's id on the server (its `Killstreak.h` StreakId), as the `StreakEarned` event names
/// it.
pub fn reward_id(reward: &str) -> Option<u16> {
    Some(match reward {
        "UAV" => 1,
        "SENTRY GUN" => 4,
        "PREDATOR MISSILE" => 5,
        "HARRIER STRIKE" => 7,
        "ATTACK HELICOPTER" => 8,
        "STEALTH BOMBER" => 11,
        "CHOPPER GUNNER" => 12,
        "TACTICAL NUKE" => 15,
        _ => return None,
    })
}

/// The kills a reward takes: one fewer with Hardline (never fewer than one).
pub fn reward_need(kills: u32, hardline: bool) -> u32 {
    if hardline && kills > 1 {
        kills - 1
    } else {
        kills
    }
}

pub struct StreakPlugin;

impl Plugin for StreakPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Streak>()
            .add_message::<Banner>()
            .add_message::<FeedLine>()
            .add_message::<Sound>()
            .add_systems(Update, score.in_set(Mw2Set::Streaks));
    }
}

/// The kill feed's line for our death: plain, naming the killer when known.
pub fn died_line(killer: Option<&str>) -> String {
    match killer {
        Some(name) => format!("You were killed by {name}"),
        None => "You died".into(),
    }
}

/// The rewards a streak of `kills` earns: with the server's reward id, that one; on our own, every
/// reward at that count (the sentry and the missile share five).
pub fn rewards_at(kills: u32, id: Option<u16>) -> Vec<&'static (u32, &'static str, &'static str)> {
    REWARDS
        .iter()
        .filter(|(n, name, _)| match id {
            Some(id) => reward_id(name) == Some(id),
            None => *n == kills,
        })
        .collect()
}

/// The reward banner for a streak of `kills`, and what the client can do with the reward itself.
fn announce(
    streak: &mut Streak,
    kills: u32,
    id: Option<u16>,
    t: f32,
    key: &str,
    banners: &mut MessageWriter<Banner>,
    sounds: &mut MessageWriter<Sound>,
) {
    for (_, reward, line) in rewards_at(kills, id) {
        banners.write(Banner {
            title: format!("{} READY", crate::arsenal::streak_label(reward)),
            subtitle: format!("{kills} kill streak. Press {key}: {line}"),
        });
        sounds.write(Sound("RaidWarning"));
        // MW2 calls a reward in when the player is ready (crate::equipment's 4).
        streak.ready.push(reward);
        let _ = t;
    }
}

fn score(
    time: Res<Time>,
    state: Res<FpsState>,
    mut streak: ResMut<Streak>,
    mut events: MessageReader<CombatEvent>,
    mut banners: MessageWriter<Banner>,
    mut feed: MessageWriter<FeedLine>,
    mut sounds: MessageWriter<Sound>,
    loadout: Res<crate::config::Loadout>,
    fusion: Res<crate::fusion::Fusion>,
    mut server: MessageReader<crate::fusion::ServerStreak>,
    keymap: Res<crate::keybinds::Keymap>,
    bar: Option<Res<benilla_app::fusion::FusionActionBar>>,
    killcam: Res<crate::killcam::Killcam>,
    combat: Res<crate::combat::Combat>,
) {
    let t = time.elapsed_secs();
    let key = crate::action_systems::ability_key(
        bar.as_deref(),
        &keymap,
        crate::action_systems::Ability::Killstreak,
    );
    let dt = time.delta_secs();
    streak.flash = (streak.flash - dt / 2.5).max(0.0);

    if let Some(at) = streak.nuke_at
        && t >= at
    {
        streak.nuke_at = None;
        streak.flash = 1.0;
        banners.write(Banner {
            title: "TACTICAL NUKE".into(),
            subtitle: "Detonated".into(),
        });
        sounds.write(Sound("LEVELUPSOUND"));
    }

    // With an Azeroth Warfare server, the streak is the server's.
    for report in server.read() {
        match *report {
            crate::fusion::ServerStreak::Progress(tenths) => {
                streak.set_tenths(tenths);
                info!("benilla-mw2: streak {}", streak.label());
            }
            crate::fusion::ServerStreak::Reward { kills, id } => {
                info!("benilla-mw2: killstreak {id} earned at {kills}");
                if state.active {
                    announce(
                        &mut streak,
                        kills,
                        Some(id),
                        t,
                        &key,
                        &mut banners,
                        &mut sounds,
                    );
                }
            }
        }
    }

    for event in events.read() {
        if !state.active {
            continue;
        }
        match event {
            CombatEvent::Hit { fatal, .. } => {
                streak.hit_at = t;
                streak.hit_fatal = *fatal;
                sounds.write(Sound("igMainMenuOptionCheckBoxOn"));
            }
            CombatEvent::Kill { name, .. } => {
                streak.total_kills += 1;
                if !fusion.enabled {
                    let tenths = streak.tenths + 10;
                    streak.set_tenths(tenths);
                }
                feed.write(FeedLine(format!("You  [{}]  {name}", loadout.weapon)));
                if !fusion.enabled {
                    let kills = streak.kills;
                    announce(&mut streak, kills, None, t, &key, &mut banners, &mut sounds);
                }
            }
            CombatEvent::Hurt { .. } => {}
            CombatEvent::Died => {
                streak.deaths += 1;
                streak.set_tenths(0);
                // Whoever last shot us, named when we were fighting them.
                let killer = killcam.recent_killer(t).and_then(|g| combat.name_of(g));
                feed.write(FeedLine(died_line(killer)));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_reward_waits_to_be_called_in() {
        let mut s = Streak::default();
        assert!(!s.uav_active(1.0));
        s.call_in("UAV", 1.0);
        assert!(s.uav_active(2.0) && !s.uav_active(1.0 + UAV_SECS + 0.1));
        s.call_in("TACTICAL NUKE", 5.0);
        assert_eq!(s.nuke_at, Some(5.0 + NUKE_SECS));
    }

    use super::*;

    /// A kill counts toward the streak and the record (the achievements' kills), shows in the
    /// feed and earns no points; a death resets the streak and says so plainly.
    #[test]
    fn a_kill_counts_without_points_and_a_death_reads_plainly() {
        use bevy::ecs::system::RunSystemOnce;
        let mut world = World::new();
        world.init_resource::<Time>();
        world.insert_resource(FpsState {
            active: true,
            ..Default::default()
        });
        world.init_resource::<Streak>();
        world.init_resource::<crate::config::Loadout>();
        world.init_resource::<crate::fusion::Fusion>();
        world.init_resource::<crate::killcam::Killcam>();
        world.init_resource::<crate::combat::Combat>();
        world.insert_resource(crate::keybinds::Keymap::defaults());
        world.init_resource::<Messages<CombatEvent>>();
        world.init_resource::<Messages<Banner>>();
        world.init_resource::<Messages<FeedLine>>();
        world.init_resource::<Messages<Sound>>();
        world.init_resource::<Messages<crate::fusion::ServerStreak>>();
        world.write_message(CombatEvent::Kill {
            guid: 7,
            name: "Hogger".into(),
            distance: 40.0,
            one_shot: true,
        });
        world.run_system_once(score).unwrap();
        let s = world.resource::<Streak>();
        assert_eq!(s.total_kills, 1, "the record counts it");
        let feed: Vec<String> = world
            .resource_mut::<Messages<FeedLine>>()
            .drain()
            .map(|l| l.0)
            .collect();
        assert_eq!(feed.len(), 1);
        assert!(feed[0].ends_with("Hogger"), "{feed:?}");
        assert!(feed.iter().all(|l| !l.contains('+')), "no points: {feed:?}");
        world.write_message(CombatEvent::Died);
        world.run_system_once(score).unwrap();
        assert_eq!(world.resource::<Streak>().deaths, 1);
        let feed: Vec<String> = world
            .resource_mut::<Messages<FeedLine>>()
            .drain()
            .map(|l| l.0)
            .collect();
        assert_eq!(feed.last().map(String::as_str), Some("You died"));
    }

    #[test]
    fn a_death_names_the_killer_when_known() {
        assert_eq!(died_line(None), "You died");
        assert_eq!(died_line(Some("Hogger")), "You were killed by Hogger");
    }

    #[test]
    fn the_streak_shows_its_tenths() {
        let mut s = Streak::default();
        s.set_tenths(34);
        assert_eq!((s.kills, s.label()), (3, "3.4".to_owned()));
        s.set_tenths(50);
        assert_eq!(s.label(), "5");
        s.set_tenths(0);
        assert_eq!((s.kills, s.best), (0, 5)); // the best outlives the death
    }

    #[test]
    fn hardline_lights_a_reward_a_kill_sooner() {
        assert_eq!(reward_need(3, true), 2);
        assert_eq!(reward_need(3, false), 3);
        assert_eq!(reward_need(1, true), 1);
        let mut s = Streak {
            hardline: true,
            ..Default::default()
        };
        s.set_tenths(20);
        assert_eq!(s.next_reward().unwrap().1, "SENTRY GUN"); // the UAV is in hand at 2
    }

    #[test]
    fn rewards_climb() {
        assert!(REWARDS.windows(2).all(|w| w[0].0 <= w[1].0));
        let mut s = Streak::default();
        assert_eq!(s.next_reward().unwrap().1, "UAV");
        s.kills = 3;
        assert_eq!(s.next_reward().unwrap().1, "SENTRY GUN");
        s.kills = 25;
        assert!(s.next_reward().is_none());
    }

    #[test]
    fn five_kills_bring_the_sentry_and_the_missile() {
        let names = |v: Vec<&'static (u32, &'static str, &'static str)>| {
            v.iter().map(|r| r.1).collect::<Vec<&'static str>>()
        };
        assert_eq!(
            names(rewards_at(5, None)),
            vec!["SENTRY GUN", "PREDATOR MISSILE"]
        );
        // The server names the one it paid.
        assert_eq!(names(rewards_at(5, Some(4))), vec!["SENTRY GUN"]);
        assert_eq!(names(rewards_at(5, Some(5))), vec!["PREDATOR MISSILE"]);
        for (_, name, _) in REWARDS {
            assert!(reward_id(name).is_some(), "{name}");
        }
    }
}
