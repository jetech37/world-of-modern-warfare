//! The sound of a hand's charge (crate::spellcast, crate::hands): while a hand holds a spell the
//! server charges, a hum rises with it, grain by grain, each grain a little higher and louder. It
//! reaches its pitch as the charge fills, then keeps climbing through the second stage toward the
//! full charge ([`crate::spellcast::FULL_CHARGE_FACTOR`] times the charge), where it holds a bright
//! shimmer. Each tier sounds a cue of its own: a two-note chime when the spell is charged (a tap
//! may go), a rising arpeggio over a low swell at the full charge. Each hand sounds its own charge
//! (the off hand a little lower), so two hands charging are two hums. Let go, and the hum stops at
//! once (it is made of short grains, nothing to cut off).
//!
//! The sounds are made in code ([`grain_pcm`], [`charged_pcm`], [`full_pcm`]) and played through
//! MW2's output ([`crate::mw2::sound::Tone`]), at its volume and WoW's.

use std::f32::consts::TAU;
use std::sync::Arc;

use bevy::prelude::*;

use crate::hands::Hands;
use crate::melee::MeleeHeld;
use crate::mode::FpsState;
use crate::mw2::audio::Pcm;
use crate::mw2::sound::Tone;
use crate::spellcast::{FULL_CHARGE_FACTOR, HandSpells};

/// The made sounds' sample rate.
const RATE: u32 = 44_100;
/// A grain of the hum: its length, and how often one starts while a hand charges.
pub const GRAIN_SECS: f32 = 0.13;
pub const GRAIN_EVERY: f32 = 0.08;
/// The hum's pitch (times the grain's own) at the start, charged, and at the full charge.
pub const PITCH_START: f32 = 1.0;
pub const PITCH_CHARGED: f32 = 1.6;
pub const PITCH_FULL: f32 = 2.3;
/// The off hand's hum sits this much lower, so two hands charging read as two.
pub const OFF_HAND_PITCH: f32 = 0.89;

/// What a hand's charge sounds this frame.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Cue {
    /// A grain of the hum, at this pitch and loudness.
    Grain { pitch: f32, gain: f32 },
    /// The first tier: charged, a tap may go.
    Charged,
    /// The second tier: the full charge.
    Full,
}

/// One hand's charge as its sound follows it.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct HandSound {
    /// The charge being sounded: when it began.
    started: Option<f32>,
    /// When the next grain is due.
    next_grain: f32,
    /// The tiers already sounded for this charge (0 none, 1 charged, 2 full).
    tier: u8,
}

/// The hum's pitch and loudness `held` seconds into a charge of `secs`.
pub fn hum_at(held: f32, secs: f32) -> (f32, f32) {
    let full = secs * FULL_CHARGE_FACTOR;
    if held < secs {
        let k = (held / secs).clamp(0.0, 1.0);
        (
            PITCH_START + (PITCH_CHARGED - PITCH_START) * k,
            0.10 + 0.10 * k,
        )
    } else if held < full {
        let k = ((held - secs) / (full - secs)).clamp(0.0, 1.0);
        (
            PITCH_CHARGED + (PITCH_FULL - PITCH_CHARGED) * k,
            0.20 + 0.08 * k,
        )
    } else {
        // Held full: a steady bright shimmer, a little quieter than the climb's top.
        (PITCH_FULL, 0.22)
    }
}

impl HandSound {
    /// This frame's cues for a hand charging since `charging` (None: not charging) a spell of
    /// `secs` charge, at `t`; `pitch` is the hand's own (the off hand's lower).
    pub fn step(&mut self, charging: Option<f32>, secs: f32, pitch: f32, t: f32) -> Vec<Cue> {
        let Some(started) = charging.filter(|_| secs > 0.0) else {
            *self = Self::default();
            return Vec::new();
        };
        if self.started != Some(started) {
            *self = Self {
                started: Some(started),
                next_grain: started,
                tier: 0,
            };
        }
        let held = t - started;
        let mut out = Vec::new();
        if self.tier < 1 && held >= secs {
            self.tier = 1;
            out.push(Cue::Charged);
        }
        if self.tier < 2 && held >= secs * FULL_CHARGE_FACTOR {
            self.tier = 2;
            out.push(Cue::Full);
        }
        if t >= self.next_grain {
            let (p, gain) = hum_at(held, secs);
            out.push(Cue::Grain {
                pitch: p * pitch,
                gain,
            });
            // A frame late never bunches grains up.
            self.next_grain = (self.next_grain + GRAIN_EVERY).max(t + GRAIN_EVERY * 0.5);
        }
        out
    }
}

/// `secs` of mono samples from `f(time)`.
fn render(secs: f32, f: impl Fn(f32) -> f32) -> Pcm {
    let n = (secs * RATE as f32) as usize;
    Pcm {
        samples: (0..n)
            .map(|i| f(i as f32 / RATE as f32).clamp(-1.0, 1.0))
            .collect(),
        channels: 1,
        rate: RATE,
    }
}

/// A grain of the hum: a soft, slightly buzzing tone under a smooth swell (a Hann window).
pub fn grain_pcm() -> Pcm {
    const HZ: f32 = 196.0;
    render(GRAIN_SECS, |t| {
        let w = 0.5 - 0.5 * (TAU * t / GRAIN_SECS).cos();
        let ph = TAU * HZ * t;
        w * (0.6 * ph.sin() + 0.25 * (2.0 * ph).sin() + 0.12 * (3.0 * ph).sin())
    })
}

/// A struck note: `hz` with its octave, decaying from `at` seconds in.
fn bell(t: f32, at: f32, hz: f32, decay: f32) -> f32 {
    if t < at {
        return 0.0;
    }
    let u = t - at;
    let attack = (u / 0.006).min(1.0);
    attack * (-u / decay).exp() * (0.7 * (TAU * hz * u).sin() + 0.2 * (TAU * 2.0 * hz * u).sin())
}

/// The charged cue: two notes struck together (a fifth), short.
pub fn charged_pcm() -> Pcm {
    render(0.45, |t| 0.5 * (bell(t, 0.0, 784.0, 0.12) + bell(t, 0.0, 1175.0, 0.09)))
}

/// The full charge cue: a quick rising arpeggio over a low swell, longer and brighter.
pub fn full_pcm() -> Pcm {
    render(0.9, |t| {
        let notes = [523.0, 784.0, 1047.0, 1568.0];
        let arp: f32 = notes
            .iter()
            .enumerate()
            .map(|(i, &hz)| bell(t, i as f32 * 0.05, hz, 0.22))
            .sum();
        let swell = (t / 0.08).min(1.0) * (-t / 0.35).exp() * (TAU * 98.0 * t).sin();
        0.35 * arp + 0.45 * swell
    })
}

/// The made sounds, once.
#[derive(Resource)]
pub struct ChargeSounds {
    grain: Arc<Pcm>,
    charged: Arc<Pcm>,
    full: Arc<Pcm>,
}

impl Default for ChargeSounds {
    fn default() -> Self {
        Self {
            grain: Arc::new(grain_pcm()),
            charged: Arc::new(charged_pcm()),
            full: Arc::new(full_pcm()),
        }
    }
}

pub struct ChargeSoundPlugin;

impl Plugin for ChargeSoundPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ChargeSounds>()
            .add_systems(Update, sound_charges.in_set(crate::Mw2Set::Hud));
    }
}

/// Each hand's charge sounds its own hum and tier cues.
#[allow(clippy::too_many_arguments)]
fn sound_charges(
    time: Res<Time>,
    state: Res<FpsState>,
    held: Res<MeleeHeld>,
    hands: Res<Hands>,
    spells: Res<HandSpells>,
    sounds: Res<ChargeSounds>,
    mut tones: MessageWriter<Tone>,
    mut per_hand: Local<[HandSound; 2]>,
) {
    let t = time.elapsed_secs();
    let up = state.active && held.0;
    for side in 0..2 {
        // Only a spell the server charges (a bar button's press casts with no charge).
        let charged_spell = hands
            .spell_in(side)
            .is_some_and(|(_, id)| spells.0.contains_key(&id));
        let charging = hands.charging[side].filter(|_| up && charged_spell);
        let pitch = if side == 1 { OFF_HAND_PITCH } else { 1.0 };
        for cue in per_hand[side].step(charging, hands.charge_secs[side], pitch, t) {
            let (pcm, gain, pitch) = match cue {
                Cue::Grain { pitch, gain } => (sounds.grain.clone(), gain, pitch),
                Cue::Charged => (sounds.charged.clone(), 0.55, pitch),
                Cue::Full => (sounds.full.clone(), 0.8, pitch),
            };
            tones.write(Tone { pcm, gain, pitch });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn grains(c: &[Cue]) -> Vec<(f32, f32)> {
        c.iter()
            .filter_map(|c| match *c {
                Cue::Grain { pitch, gain } => Some((pitch, gain)),
                _ => None,
            })
            .collect()
    }

    #[test]
    fn the_hum_rises_through_both_tiers_and_each_tier_cues_once() {
        let mut h = HandSound::default();
        let secs = 0.6;
        let mut cues = Vec::new();
        let mut t = 1.0;
        while t < 1.0 + secs * FULL_CHARGE_FACTOR + 0.5 {
            cues.extend(h.step(Some(1.0), secs, 1.0, t));
            t += 1.0 / 60.0;
        }
        assert_eq!(cues.iter().filter(|c| **c == Cue::Charged).count(), 1);
        assert_eq!(cues.iter().filter(|c| **c == Cue::Full).count(), 1);
        let charged_at = cues.iter().position(|c| *c == Cue::Charged).unwrap();
        let full_at = cues.iter().position(|c| *c == Cue::Full).unwrap();
        assert!(charged_at < full_at, "charged, then full");
        // Rising: every grain up to the full charge at least as high as the one before.
        let g = grains(&cues[..full_at]);
        assert!(g.len() > 10, "grains all along: {}", g.len());
        assert!(g.windows(2).all(|w| w[1].0 >= w[0].0 - 1e-6));
        assert!(g.windows(2).all(|w| w[1].1 >= w[0].1 - 1e-6));
        assert!((g[0].0 - PITCH_START).abs() < 0.05);
        assert!(g.last().unwrap().0 > PITCH_CHARGED);
    }

    #[test]
    fn let_go_the_hum_stops_and_a_new_charge_starts_over() {
        let mut h = HandSound::default();
        h.step(Some(1.0), 0.5, 1.0, 1.0);
        h.step(Some(1.0), 0.5, 1.0, 1.6);
        assert!(h.step(None, 0.5, 1.0, 1.7).is_empty(), "let go: silent");
        // A new charge sounds its tiers again.
        let c = h.step(Some(2.0), 0.5, 1.0, 2.6);
        assert!(c.contains(&Cue::Charged));
        // No charge (a spray, a bar press): nothing.
        let mut h = HandSound::default();
        assert!(h.step(Some(1.0), 0.0, 1.0, 5.0).is_empty());
    }

    #[test]
    fn the_off_hand_hums_lower_and_apart() {
        let (mut a, mut b) = (HandSound::default(), HandSound::default());
        let ga = grains(&a.step(Some(1.0), 0.6, 1.0, 1.0));
        let gb = grains(&b.step(Some(1.3), 0.6, OFF_HAND_PITCH, 1.0));
        assert!(gb.is_empty() || gb[0].0 < ga[0].0);
        // Each hand's tiers come at its own charge's time.
        assert!(a.step(Some(1.0), 0.6, 1.0, 1.61).contains(&Cue::Charged));
        assert!(!b.step(Some(1.3), 0.6, OFF_HAND_PITCH, 1.61).contains(&Cue::Charged));
        assert!(b.step(Some(1.3), 0.6, OFF_HAND_PITCH, 1.91).contains(&Cue::Charged));
    }

    #[test]
    fn the_made_sounds_are_sound() {
        for p in [grain_pcm(), charged_pcm(), full_pcm()] {
            assert!(p.frames() > 1000);
            assert!(p.samples.iter().all(|s| s.abs() <= 1.0));
            assert!(p.samples.iter().any(|s| s.abs() > 0.1), "audible");
        }
        assert!(full_pcm().frames() > charged_pcm().frames(), "the full cue is the bigger");
    }
}
