//! A small mixer for MW2's first-person sounds, on its own output stream.
//!
//! benilla's mixer is private to benilla-app, so MW2's sounds play through a stream of their own
//! (cpal, at the version benilla's own output uses). Every sound here is the player's own and
//! unpositioned, so the mixer only sums voices: each one resampled to the device rate, mono
//! spread to every channel, at its own gain.

use std::sync::{Arc, Mutex};

/// At most this many voices play at once; a new one past it ends the oldest.
pub const MAX_VOICES: usize = 32;

/// A decoded sound: interleaved samples in -1..1.
#[derive(Debug)]
pub struct Pcm {
    pub samples: Vec<f32>,
    pub channels: u16,
    pub rate: u32,
}

impl Pcm {
    /// Decode MW2's loaded PCM (`format` 1, 8 or 16 bit); `None` for anything else.
    pub fn decode(format: i32, bits: i32, channels: i32, rate: u32, bytes: &[u8]) -> Option<Self> {
        if format != 1 || !(1..=2).contains(&channels) || rate == 0 {
            return None;
        }
        let samples = match bits {
            16 => bytes
                .as_chunks::<2>()
                .0
                .iter()
                .map(|&b| f32::from(i16::from_le_bytes(b)) / 32768.0)
                .collect(),
            8 => bytes
                .iter()
                .map(|&b| (f32::from(b) - 128.0) / 128.0)
                .collect(),
            _ => return None,
        };
        Some(Self {
            samples,
            channels: channels as u16,
            rate,
        })
    }

    pub fn frames(&self) -> usize {
        self.samples.len() / usize::from(self.channels.max(1))
    }
}

struct Voice {
    pcm: Arc<Pcm>,
    /// Position in source frames.
    pos: f64,
    /// Source frames per output frame.
    step: f64,
    gain: f32,
}

impl Voice {
    /// The source's sample `ch` at the current position, linearly interpolated.
    fn sample(&self, ch: usize) -> f32 {
        let chans = usize::from(self.pcm.channels.max(1));
        let ch = ch.min(chans - 1);
        let i = self.pos as usize;
        let frac = (self.pos - i as f64) as f32;
        let at = |f: usize| self.pcm.samples.get(f * chans + ch).copied().unwrap_or(0.0);
        at(i) + (at(i + 1) - at(i)) * frac
    }
}

/// The voices playing, summed into the device's buffers.
#[derive(Default)]
pub struct Mixer {
    voices: Vec<Voice>,
    /// The device's sample rate, once known.
    pub out_rate: u32,
}

impl Mixer {
    pub fn new(out_rate: u32) -> Self {
        Self {
            voices: Vec::new(),
            out_rate,
        }
    }

    /// Start a sound at `gain`, played `pitch` times as fast.
    pub fn play(&mut self, pcm: Arc<Pcm>, gain: f32, pitch: f32) {
        if pcm.frames() == 0 || self.out_rate == 0 {
            return;
        }
        if self.voices.len() >= MAX_VOICES {
            self.voices.remove(0);
        }
        let step = f64::from(pcm.rate) / f64::from(self.out_rate) * f64::from(pitch.max(0.01));
        self.voices.push(Voice {
            pcm,
            pos: 0.0,
            step,
            gain,
        });
    }

    pub fn voices(&self) -> usize {
        self.voices.len()
    }

    /// Fill `out` (interleaved, `channels` per frame) with the voices' sum, dropping finished
    /// voices. A mono voice plays on every channel; a stereo voice on a mono device is averaged.
    pub fn mix(&mut self, out: &mut [f32], channels: usize) {
        out.fill(0.0);
        let channels = channels.max(1);
        for voice in &mut self.voices {
            let src_chans = usize::from(voice.pcm.channels.max(1));
            let end = voice.pcm.frames() as f64;
            for frame in out.chunks_exact_mut(channels) {
                if voice.pos >= end {
                    break;
                }
                if channels == 1 && src_chans == 2 {
                    frame[0] += 0.5 * (voice.sample(0) + voice.sample(1)) * voice.gain;
                } else {
                    for (ch, s) in frame.iter_mut().enumerate() {
                        *s += voice.sample(ch) * voice.gain;
                    }
                }
                voice.pos += voice.step;
            }
        }
        self.voices.retain(|v| v.pos < v.pcm.frames() as f64);
        for s in out.iter_mut() {
            *s = s.clamp(-1.0, 1.0);
        }
    }
}

/// The open output: the mixer the device's callback reads, shared with the game.
#[derive(Clone)]
pub struct Output {
    pub mixer: Arc<Mutex<Mixer>>,
}

impl Output {
    /// Open the default output device on a thread that keeps the stream alive. `None` when there
    /// is no device.
    pub fn open() -> Option<Self> {
        let mixer = Arc::new(Mutex::new(Mixer::default()));
        let (tx, rx) = std::sync::mpsc::channel();
        let shared = mixer.clone();
        std::thread::Builder::new()
            .name("mw2-audio".into())
            .spawn(move || match start_stream(shared) {
                Ok(stream) => {
                    let _ = tx.send(true);
                    // The stream plays while it lives; this thread only keeps it.
                    loop {
                        std::thread::park();
                        let _ = &stream;
                    }
                }
                Err(e) => {
                    bevy::log::warn!("benilla-mw2: no audio output for MW2 sounds: {e}");
                    let _ = tx.send(false);
                }
            })
            .ok()?;
        rx.recv().ok()?.then_some(Self { mixer })
    }

    /// A mixer no device reads, for runs without sound (`WOW_NOSOUND`): sounds start and are
    /// picked as usual but nothing is heard.
    pub fn silent() -> Self {
        Self {
            mixer: Arc::new(Mutex::new(Mixer::new(44100))),
        }
    }

    pub fn play(&self, pcm: Arc<Pcm>, gain: f32, pitch: f32) {
        if let Ok(mut m) = self.mixer.lock() {
            m.play(pcm, gain, pitch);
        }
    }
}

fn start_stream(mixer: Arc<Mutex<Mixer>>) -> Result<cpal::Stream, String> {
    use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
    let host = cpal::default_host();
    let device = host
        .default_output_device()
        .ok_or("no default output device")?;
    let config = device.default_output_config().map_err(|e| e.to_string())?;
    if config.sample_format() != cpal::SampleFormat::F32 {
        return Err(format!(
            "unsupported sample format {:?}",
            config.sample_format()
        ));
    }
    let config: cpal::StreamConfig = config.into();
    let channels = usize::from(config.channels);
    if let Ok(mut m) = mixer.lock() {
        m.out_rate = config.sample_rate;
    }
    let stream = device
        .build_output_stream(
            &config,
            move |out: &mut [f32], _| match mixer.lock() {
                Ok(mut m) => m.mix(out, channels),
                Err(_) => out.fill(0.0),
            },
            |e| bevy::log::warn!("benilla-mw2: audio stream: {e}"),
            None,
        )
        .map_err(|e| e.to_string())?;
    stream.play().map_err(|e| e.to_string())?;
    Ok(stream)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pcm(samples: Vec<f32>, channels: u16, rate: u32) -> Arc<Pcm> {
        Arc::new(Pcm {
            samples,
            channels,
            rate,
        })
    }

    #[test]
    fn decodes_16_bit_pcm() {
        let bytes = [0x00, 0x40, 0x00, 0xC0]; // 16384, -16384
        let p = Pcm::decode(1, 16, 1, 44100, &bytes).expect("pcm");
        assert_eq!(p.samples, vec![0.5, -0.5]);
        assert_eq!(p.frames(), 2);
        assert!(Pcm::decode(2, 16, 1, 44100, &bytes).is_none()); // ADPCM: not here
    }

    #[test]
    fn mono_plays_on_both_channels_at_its_gain() {
        let mut m = Mixer::new(44100);
        m.play(pcm(vec![0.5, 0.5], 1, 44100), 0.5, 1.0);
        let mut out = [0.0; 4];
        m.mix(&mut out, 2);
        assert_eq!(out, [0.25, 0.25, 0.25, 0.25]);
    }

    #[test]
    fn stereo_keeps_its_channels() {
        let mut m = Mixer::new(44100);
        m.play(pcm(vec![0.1, -0.1], 2, 44100), 1.0, 1.0);
        let mut out = [0.0; 2];
        m.mix(&mut out, 2);
        assert_eq!(out, [0.1, -0.1]);
    }

    #[test]
    fn a_finished_voice_is_dropped() {
        let mut m = Mixer::new(44100);
        m.play(pcm(vec![0.2; 3], 1, 44100), 1.0, 1.0);
        let mut out = [0.0; 8];
        m.mix(&mut out, 2);
        assert_eq!(m.voices(), 0);
        assert_eq!(&out[6..], &[0.0, 0.0]); // silence after the sound's 3 frames
    }

    #[test]
    fn a_lower_rate_is_stretched_to_the_device() {
        // 22050 Hz into 44100: 4 source frames last 8 output frames.
        let mut m = Mixer::new(44100);
        m.play(pcm(vec![0.0, 1.0, 0.0, 1.0], 1, 22050), 1.0, 1.0);
        let mut out = [0.0; 7];
        m.mix(&mut out, 1);
        assert_eq!(m.voices(), 1);
        assert!((out[1] - 0.5).abs() < 1e-6); // halfway between the first two frames
        let mut out = [0.0; 1];
        m.mix(&mut out, 1);
        assert_eq!(m.voices(), 0);
    }

    #[test]
    fn the_sum_is_clamped() {
        let mut m = Mixer::new(44100);
        for _ in 0..3 {
            m.play(pcm(vec![0.9], 1, 44100), 1.0, 1.0);
        }
        let mut out = [0.0; 1];
        m.mix(&mut out, 1);
        assert_eq!(out, [1.0]);
    }

    #[test]
    fn past_the_cap_the_oldest_voice_ends() {
        let mut m = Mixer::new(44100);
        for _ in 0..MAX_VOICES + 5 {
            m.play(pcm(vec![0.0; 10], 1, 44100), 1.0, 1.0);
        }
        assert_eq!(m.voices(), MAX_VOICES);
    }
}
