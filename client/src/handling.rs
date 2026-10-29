//! What the character's stats do with the gun (the server's `fusion/Stats.h`, design doc section
//! 7). Gear keeps its WoW stats; the server turns them into handling and sends it on fusion
//! channel 6 whenever it changes (equip, level, buffs, login): Agility aims, swaps and comes out
//! of a sprint quicker, Strength holds the recoil, ranged attack power steadies the sway and
//! speeds the reload, Hit tightens the hip cone and its bloom, Defense and armor take the flinch
//! out of a hit. Nothing here raises damage; the server deals that.
//!
//! The wire is the C++ encoder's byte for byte (see [`wire`]); the tests use the C++ tests'
//! bytes.

use benilla_app::fusion::FusionChannelReceived;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::fusion::IncomingFire;
use crate::weapon::Weapon;

pub use wire::*;

/// The handling message's wire (`Fusion::Stats::EncodeHandling` in `Stats.cpp`).
pub mod wire {
    /// The fusion channel of stats (`kChannelStats`).
    pub const CHANNEL_STATS: u8 = 6;
    /// Server to client: the handling (`kOpHandling`).
    pub const OP_HANDLING: u8 = 1;
    pub const HANDLING_VERSION: u8 = 1;
    pub const FLAG_INSTANT_ADS: u8 = 0x01;
    pub const FLAG_STEADY_FIRST_SHOT: u8 = 0x02;
    /// The outputs (`Stats::Out`), each a u16 in thousandths, then four more, the thresholds
    /// and the flags.
    const OUTPUTS: usize = 14;
    const HANDLING_BYTES: usize = 2 + 2 * (OUTPUTS + 4) + 2 + 1;

    /// The character's handling: multipliers on MW2's own (1 = MW2's, lower is quicker or
    /// steadier), and what the server applies itself, for the readout.
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct Handling {
        pub ads: f32,
        pub swap: f32,
        pub sprint_out: f32,
        pub recoil: f32,
        pub hip_spread: f32,
        pub bloom: f32,
        pub sway: f32,
        pub reload: f32,
        pub flinch: f32,
        /// Shares the server plays: a heavy gun's weight lifted, equipment strength, the regen
        /// delay taken off and rate added, weak-spot hits taken away.
        pub heavy_relief: f32,
        pub equipment: f32,
        pub regen_delay_share: f32,
        pub regen_rate_share: f32,
        pub weak_spot_taken: f32,
        /// Seconds after a hit before health comes back, and the share of the pool a second.
        pub regen_delay: f32,
        pub regen_rate: f32,
        /// A body shot of ours counting as a weak-spot hit (crit).
        pub weak_spot: f32,
        /// The share of a bullet our armor takes.
        pub bullet_cut: f32,
        /// Bit i: the server's `kThresholds[i]` is met.
        pub thresholds: u16,
        /// 150 Agility: the first aim after a sprint is instant.
        pub instant_ads_after_sprint: bool,
        /// 150 Strength: the first round of a burst does not kick.
        pub steady_first_shot: bool,
    }

    impl Default for Handling {
        fn default() -> Self {
            Self {
                ads: 1.0,
                swap: 1.0,
                sprint_out: 1.0,
                recoil: 1.0,
                hip_spread: 1.0,
                bloom: 1.0,
                sway: 1.0,
                reload: 1.0,
                flinch: 1.0,
                heavy_relief: 0.0,
                equipment: 0.0,
                regen_delay_share: 0.0,
                regen_rate_share: 0.0,
                weak_spot_taken: 0.0,
                regen_delay: 5.0,
                regen_rate: 0.125,
                weak_spot: 0.0,
                bullet_cut: 0.0,
                thresholds: 0,
                instant_ads_after_sprint: false,
                steady_first_shot: false,
            }
        }
    }

    /// Decode the server's handling; None for another op, version or length. Multipliers come
    /// back held to (0.1, 1] so a broken message can never freeze the gun.
    pub fn decode_handling(body: &[u8]) -> Option<Handling> {
        if body.len() != HANDLING_BYTES || body[0] != OP_HANDLING || body[1] != HANDLING_VERSION {
            return None;
        }
        let v =
            |i: usize| f32::from(u16::from_le_bytes([body[2 + 2 * i], body[3 + 2 * i]])) / 1000.0;
        let m = |i: usize| v(i).clamp(0.1, 1.0);
        let at = 2 + 2 * (OUTPUTS + 4);
        let flags = body[at + 2];
        Some(Handling {
            ads: m(0),
            swap: m(1),
            sprint_out: m(2),
            recoil: m(3),
            hip_spread: m(4),
            bloom: m(5),
            sway: m(6),
            reload: m(7),
            flinch: m(8),
            heavy_relief: v(9),
            equipment: v(10),
            regen_delay_share: v(11),
            regen_rate_share: v(12),
            weak_spot_taken: v(13),
            regen_delay: v(14),
            regen_rate: v(15),
            weak_spot: v(16),
            bullet_cut: v(17),
            thresholds: u16::from_le_bytes([body[at], body[at + 1]]),
            instant_ads_after_sprint: flags & FLAG_INSTANT_ADS != 0,
            steady_first_shot: flags & FLAG_STEADY_FIRST_SHOT != 0,
        })
    }
}

/// The view kick of a round hitting us, in degrees of pitch, before Defense and armor
/// (the server's `kFlinchDegrees`).
pub const FLINCH_DEGREES: f32 = 1.2;

/// Seconds without a shot that start a new burst (for the steady first shot).
pub const BURST_GAP: f32 = 0.3;

/// How long after a sprint the instant aim still counts as the first aim after it.
pub const INSTANT_ADS_WINDOW: f32 = 1.5;

/// The handling as one readable block: what the stats give over MW2's own.
pub fn summary(h: &Handling) -> Vec<String> {
    let pct = |m: f32| ((1.0 - m) * 100.0).round() as i32;
    let share = |s: f32| (s * 100.0).round() as i32;
    let mut lines = vec![
        format!(
            "Aim {}% quicker, swaps {}%, sprint-out {}%, reload {}%",
            pct(h.ads),
            pct(h.swap),
            pct(h.sprint_out),
            pct(h.reload)
        ),
        format!(
            "Recoil {}% less, hip spread {}%, bloom {}%, sway {}%, flinch {}%",
            pct(h.recoil),
            pct(h.hip_spread),
            pct(h.bloom),
            pct(h.sway),
            pct(h.flinch)
        ),
        format!(
            "Armor stops {}% of a bullet, weak-spot hits {}%, {}% fewer taken",
            share(h.bullet_cut),
            share(h.weak_spot),
            share(h.weak_spot_taken)
        ),
        format!(
            "Health back after {:.1} s at {}% a second; equipment +{}%, heavy guns {}% lighter",
            h.regen_delay,
            share(h.regen_rate),
            share(h.equipment),
            share(h.heavy_relief)
        ),
    ];
    if h.instant_ads_after_sprint {
        lines.push("150 Agility: the first aim after a sprint is instant".into());
    }
    if h.steady_first_shot {
        lines.push("150 Strength: the first round of a burst does not kick".into());
    }
    lines
}

/// The aim step this frame: quicker with Agility, and the whole way at once for the first aim
/// out of a sprint with 150 Agility. `since_sprint` is the time since the last sprint ended and
/// `spent` whether that sprint's instant aim was already used.
pub fn ads_step(
    dt: f32,
    ads_secs: f32,
    h: &Handling,
    starting: bool,
    since_sprint: f32,
    spent: bool,
) -> f32 {
    if h.instant_ads_after_sprint
        && starting
        && !spent
        && (0.0..INSTANT_ADS_WINDOW).contains(&since_sprint)
    {
        return 1.0;
    }
    dt / (ads_secs * h.ads.max(0.1))
}

/// One round's camera kick: Strength's share of MW2's, none for a burst's first with 150
/// Strength.
pub fn recoil_kick(base: f32, h: &Handling, burst_start: bool) -> f32 {
    if h.steady_first_shot && burst_start {
        0.0
    } else {
        base * h.recoil
    }
}

/// The Create-a-Class window's "Combat stats" block (crate::cac_ui).
#[derive(Component)]
pub struct CombatStatsText;

pub struct HandlingPlugin;

impl Plugin for HandlingPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(
            Update,
            (read_channel, flinch).chain().in_set(Mw2Set::Combat),
        )
        .add_systems(Update, show_combat_stats.in_set(Mw2Set::Hud));
    }
}

/// The block's text, written when the handling changes (or the window is built).
fn show_combat_stats(
    weapon: Res<Weapon>,
    mut shown: Local<Option<Handling>>,
    mut texts: Query<(Ref<CombatStatsText>, &mut Text)>,
) {
    let changed = *shown != Some(weapon.handling);
    *shown = Some(weapon.handling);
    for (marker, mut text) in &mut texts {
        if marker.is_added() || changed {
            let block = format!("Combat stats\n{}", summary(&weapon.handling).join("\n"));
            if text.0 != block {
                text.0 = block;
            }
        }
    }
}

/// The server's handling into the gun and the sprint.
fn read_channel(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut weapon: ResMut<Weapon>,
    mut sprint: ResMut<crate::sprint::Sprint>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_STATS || m.body.first() != Some(&OP_HANDLING) {
            continue;
        }
        if let Some(h) = decode_handling(&m.body) {
            if weapon.handling != h {
                info!("benilla-mw2: combat stats: {}", summary(&h).join("; "));
            }
            weapon.handling = h;
            sprint.sprint_out = crate::sprint::SPRINT_OUT * h.sprint_out;
        }
    }
}

/// A round hitting us kicks the view (MW2's flinch), less with Defense and armor.
fn flinch(mut incoming: MessageReader<IncomingFire>, mut weapon: ResMut<Weapon>) {
    for f in incoming.read() {
        if f.hit {
            weapon.recoil = (weapon.recoil + FLINCH_DEGREES * weapon.handling.flinch).min(6.0);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn from_hex(s: &str) -> Vec<u8> {
        (0..s.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
            .collect()
    }

    /// `stats_handling_bytes_match_the_client` in StatsTests.cpp: the same bytes.
    #[test]
    fn handling_decodes_the_servers_bytes() {
        let bytes = from_hex(
            "0101ee02e803e803e803e803e803e803e803e8030000fa0000000000000088137d006400c800050001",
        );
        let h = decode_handling(&bytes).expect("decodes");
        assert_eq!(h.ads, 0.75);
        assert_eq!(h.swap, 1.0);
        assert_eq!(h.flinch, 1.0);
        assert_eq!(h.equipment, 0.25);
        assert_eq!(h.heavy_relief, 0.0);
        assert_eq!(h.regen_delay, 5.0);
        assert_eq!(h.regen_rate, 0.125);
        assert!((h.weak_spot - 0.1).abs() < 1e-6);
        assert!((h.bullet_cut - 0.2).abs() < 1e-6);
        assert_eq!(h.thresholds, 0b101);
        assert!(h.instant_ads_after_sprint);
        assert!(!h.steady_first_shot);
        // Short, another op, another version: refused.
        assert_eq!(decode_handling(&bytes[..bytes.len() - 1]), None);
        let mut bad = bytes.clone();
        bad[0] = 2;
        assert_eq!(decode_handling(&bad), None);
        let mut bad = bytes;
        bad[1] = 9;
        assert_eq!(decode_handling(&bad), None);
    }

    #[test]
    fn a_broken_multiplier_never_freezes_the_gun() {
        let mut bytes = from_hex(
            "0101ee02e803e803e803e803e803e803e803e8030000fa0000000000000088137d006400c800050001",
        );
        bytes[2] = 0;
        bytes[3] = 0; // ads 0
        bytes[4] = 0xff;
        bytes[5] = 0xff; // swap 65.5
        let h = decode_handling(&bytes).unwrap();
        assert_eq!(h.ads, 0.1);
        assert_eq!(h.swap, 1.0);
    }

    #[test]
    fn mw2s_own_handling_by_default() {
        let h = Handling::default();
        assert!((ads_step(0.018, 0.18, &h, true, 0.2, false) - 0.1).abs() < 1e-6);
        assert_eq!(recoil_kick(1.6, &h, true), 1.6);
    }

    #[test]
    fn agility_aims_quicker_and_the_threshold_snaps_the_first_aim() {
        let h = Handling {
            ads: 0.5,
            ..Handling::default()
        };
        assert!((ads_step(0.018, 0.18, &h, true, 0.2, false) - 0.2).abs() < 1e-6);
        let quick = Handling {
            instant_ads_after_sprint: true,
            ..h
        };
        // The first aim within the window after a sprint: all at once; not twice, not later.
        assert_eq!(ads_step(0.018, 0.18, &quick, true, 0.4, false), 1.0);
        assert!(ads_step(0.018, 0.18, &quick, true, 0.4, true) < 1.0);
        assert!(ads_step(0.018, 0.18, &quick, true, 3.0, false) < 1.0);
        assert!(ads_step(0.018, 0.18, &quick, false, 0.4, false) < 1.0);
    }

    #[test]
    fn strength_holds_the_kick() {
        let h = Handling {
            recoil: 0.7,
            ..Handling::default()
        };
        assert!((recoil_kick(1.6, &h, true) - 1.12).abs() < 1e-6);
        let steady = Handling {
            steady_first_shot: true,
            ..h
        };
        assert_eq!(recoil_kick(1.6, &steady, true), 0.0);
        assert!((recoil_kick(1.6, &steady, false) - 1.12).abs() < 1e-6);
    }

    #[test]
    fn the_summary_reads_the_numbers() {
        let h = Handling {
            ads: 0.8,
            instant_ads_after_sprint: true,
            ..Handling::default()
        };
        let lines = summary(&h);
        assert!(lines[0].starts_with("Aim 20% quicker"));
        assert!(lines.iter().any(|l| l.contains("150 Agility")));
    }
}
