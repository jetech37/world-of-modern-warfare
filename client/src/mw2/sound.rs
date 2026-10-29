//! MW2's own first-person sounds, read from the install's sound zones: the gun's shots, dry
//! clicks and knife, the reload foley its animations' notetracks name, and the gear rattle of
//! moving with it. Every sound is a [`Sfx`] message naming an MW2 sound alias; an alias the
//! install lacks (or that only streams) plays nothing.

use std::collections::HashMap;
use std::sync::Arc;

use asset_audio::{AssetNamespace, SoundCatalog, load_sound_catalog};
use asset_transport::find_zone_file;
use benilla_app::fusion::Stance;
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use super::audio::{Output, Pcm};
use super::{Job, spawn_job, take};
use crate::lua;
use crate::weapon::Weapon;

/// The zones holding the multiplayer sounds: the localized zone has the loaded foley, footsteps
/// and weapon fire.
const ZONES: [&str; 3] = ["common_mp", "localized_common_mp", "patch_mp"];

/// The loudness of MW2's sounds against WoW's, before WoW's own volume settings; `MW2_VOLUME`
/// overrides it.
const BASE_VOLUME: f32 = 0.6;

/// Play an MW2 sound alias.
#[derive(Message, Clone, Debug, PartialEq)]
pub struct Sfx {
    pub alias: String,
    pub gain: f32,
}

impl Sfx {
    pub fn new(alias: impl Into<String>) -> Self {
        Self {
            alias: alias.into(),
            gain: 1.0,
        }
    }

    pub fn at(alias: impl Into<String>, gain: f32) -> Self {
        Self {
            alias: alias.into(),
            gain,
        }
    }
}

/// Play a sound made in code (crate::charge_sound's charge hum and cues), on the same output and
/// volume as MW2's: its samples, loudness and playback rate (1 as made).
#[derive(Message, Clone, Debug)]
pub struct Tone {
    pub pcm: Arc<Pcm>,
    pub gain: f32,
    pub pitch: f32,
}

/// The sound catalog and the output, once both are open.
#[derive(Resource, Default)]
pub struct Mw2Sound {
    pub ready: bool,
    job: Option<Job<(SoundCatalog, Output)>>,
    catalog: Option<Arc<SoundCatalog>>,
    out: Option<Output>,
    /// Decoded sounds by loaded-sound name.
    cache: HashMap<String, Arc<Pcm>>,
    rng: u32,
    base: f32,
    /// WoW's master and effects volume (0..1), polled from its CVars.
    wow_volume: f32,
    next_poll: f32,
    poll_cvars: bool,
    /// A probe run (crate::probe) logs every sound.
    log: bool,
}

pub struct Mw2SoundPlugin;

impl Plugin for Mw2SoundPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Mw2Sound>()
            .add_message::<Sfx>()
            .add_message::<Tone>()
            .add_systems(Startup, open_sound)
            .add_systems(
                Update,
                play_sfx
                    .after(super::viewmodel::animate)
                    .in_set(crate::Mw2Set::Hud),
            );
    }
}

fn open_sound(mut sound: ResMut<Mw2Sound>) {
    if std::env::var_os("MW2_PATH").is_none() && std::env::var_os("IW4L_GAMES").is_none() {
        return;
    }
    sound.base = std::env::var("MW2_VOLUME")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(BASE_VOLUME);
    sound.wow_volume = 1.0;
    sound.poll_cvars = true;
    sound.log = std::env::var_os("MW2_PROBE_MOVE").is_some()
        || std::env::var_os("MW2_PROBE_FIRE").is_some();
    sound.rng = 0x9e37_79b9;
    sound.job = Some(spawn_job(|| {
        let root = super::load::games_root()?;
        let mut catalog = SoundCatalog::default();
        for zone in ZONES {
            if let Ok(file) = find_zone_file(&root, zone)
                && let Ok(c) = load_sound_catalog(&file.path)
            {
                catalog.absorb(c);
            }
        }
        if catalog.loaded.is_empty() {
            return Err("no MW2 sounds found".into());
        }
        // A run without sound (benilla's `WOW_NOSOUND`) opens no device.
        let out = if std::env::var_os("WOW_NOSOUND").is_some() {
            Output::silent()
        } else {
            Output::open().ok_or("no audio output")?
        };
        Ok((catalog, out))
    }));
}

/// Play this frame's sounds, and tell the gun whether MW2's foley stands in for WoW's UI sounds.
fn play_sfx(
    time: Res<Time>,
    mut sound: ResMut<Mw2Sound>,
    mut sfx: MessageReader<Sfx>,
    mut tones: MessageReader<Tone>,
    mut weapon: ResMut<Weapon>,
    script: Option<NonSend<UiScript>>,
    viewmodels: Query<(), With<super::viewmodel::Viewmodel>>,
) {
    let sound = &mut *sound;
    if let Some(job) = &sound.job
        && let Some(result) = take(job)
    {
        sound.job = None;
        match result {
            Ok((catalog, out)) => {
                info!(
                    "benilla-mw2: MW2 sounds ready, {} aliases",
                    catalog.sounds.len()
                );
                sound.catalog = Some(Arc::new(catalog));
                sound.out = Some(out);
                sound.ready = true;
            }
            Err(e) => warn!("benilla-mw2: MW2 sounds off: {e}"),
        }
    }
    let foley = sound.ready && !viewmodels.is_empty();
    if weapon.foley != foley {
        weapon.foley = foley;
    }
    let t = time.elapsed_secs();
    if sound.poll_cvars
        && t >= sound.next_poll
        && let Some(script) = &script
    {
        sound.next_poll = t + 1.0;
        match lua::eval::<(Option<String>, Option<String>, Option<String>)>(
            script,
            "return GetCVar(\"MasterSoundEffects\"), GetCVar(\"MasterVolume\"), GetCVar(\"SoundVolume\")",
        ) {
            Some((on, master, sfx)) => sound.wow_volume = wow_volume(on, master, sfx),
            None => sound.poll_cvars = false,
        }
    }
    let (Some(catalog), Some(out)) = (sound.catalog.clone(), sound.out.clone()) else {
        sfx.clear();
        tones.clear();
        return;
    };
    for tone in tones.read() {
        out.play(
            tone.pcm.clone(),
            tone.gain * sound.base * sound.wow_volume,
            tone.pitch,
        );
    }
    for s in sfx.read() {
        let Some(picked) = catalog
            .pick_loaded_outcome(AssetNamespace::Iw4, &s.alias, &mut sound.rng, None)
            .and_then(|o| o.picked)
        else {
            if sound.log {
                info!("probe: sfx {} not loaded", s.alias);
            }
            continue;
        };
        let pcm = match sound.cache.get(&picked.sound.name) {
            Some(p) => p.clone(),
            None => {
                let l = picked.sound;
                let Some(p) = Pcm::decode(
                    l.format(),
                    l.bits(),
                    l.channels(),
                    l.rate,
                    l.encoded_bytes(),
                ) else {
                    continue;
                };
                let p = Arc::new(p);
                sound.cache.insert(l.name.clone(), p.clone());
                p
            }
        };
        let gain = s.gain * picked.volume * sound.base * sound.wow_volume;
        if sound.log {
            info!(
                "probe: sfx {} -> {} gain {gain:.2}",
                s.alias, picked.sound.name
            );
        }
        out.play(pcm, gain, picked.pitch);
    }
}

/// WoW's effects loudness from its CVars (`MasterSoundEffects`, `MasterVolume`, `SoundVolume`);
/// a missing one counts as its default (on, 1.0).
pub fn wow_volume(on: Option<String>, master: Option<String>, sfx: Option<String>) -> f32 {
    let num = |v: Option<String>| {
        v.and_then(|s| s.trim().parse::<f32>().ok())
            .unwrap_or(1.0)
            .clamp(0.0, 1.0)
    };
    if on.as_deref().map(str::trim) == Some("0") {
        return 0.0;
    }
    num(master) * num(sfx)
}

/// The gear rattle of a step: MW2's per-gait player foley.
pub fn gear_alias(stance: Stance, sprinting: bool, speed_ips: f32) -> &'static str {
    match stance {
        Stance::Prone => "gear_rattle_plr_prone",
        _ if sprinting => "gear_rattle_plr_sprint",
        // MW2's walk: crouched, or slower than about two thirds of a run.
        Stance::Crouch => "gear_rattle_plr_walk",
        _ if speed_ips < 120.0 => "gear_rattle_plr_walk",
        _ => "gear_rattle_plr_run",
    }
}

/// A step lands each half of MW2's 256-step bob cycle: did the phase cross one?
pub fn step_crossed(old_phase: f32, new_phase: f32) -> bool {
    let half = |p: f32| (p / 128.0).floor() as i32;
    if new_phase < old_phase {
        // Wrapped past 256.
        return true;
    }
    half(new_phase) != half(old_phase)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wow_volume_follows_the_cvars() {
        let s = |v: &str| Some(v.to_owned());
        assert_eq!(wow_volume(s("1"), s("0.5"), s("0.5")), 0.25);
        assert_eq!(wow_volume(s("0"), s("1"), s("1")), 0.0);
        assert_eq!(wow_volume(None, None, None), 1.0);
        assert_eq!(wow_volume(s("1"), s("junk"), s("2")), 1.0);
    }

    #[test]
    fn gear_rattle_follows_the_gait() {
        assert_eq!(
            gear_alias(Stance::Prone, false, 30.0),
            "gear_rattle_plr_prone"
        );
        assert_eq!(
            gear_alias(Stance::Stand, true, 250.0),
            "gear_rattle_plr_sprint"
        );
        assert_eq!(
            gear_alias(Stance::Crouch, false, 110.0),
            "gear_rattle_plr_walk"
        );
        assert_eq!(
            gear_alias(Stance::Stand, false, 190.0),
            "gear_rattle_plr_run"
        );
        assert_eq!(
            gear_alias(Stance::Stand, false, 80.0),
            "gear_rattle_plr_walk"
        );
    }

    #[test]
    fn two_steps_per_bob_cycle() {
        assert!(!step_crossed(10.0, 100.0));
        assert!(step_crossed(120.0, 130.0));
        assert!(step_crossed(250.0, 4.0));
        assert!(!step_crossed(130.0, 250.0));
    }
}
