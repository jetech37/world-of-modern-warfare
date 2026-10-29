//! The hands' charge on the stock classic casting bar (`CastingBarFrame`, FrameXML's
//! `CastingBarFrame.xml`/`.lua`), where 1.12 seats it above the action bar. The layer drives the
//! stock frame the way the game does, by its own events (`SPELLCAST_START`, `_STOP`, `_FAILED`,
//! `_INTERRUPTED`, `_CHANNEL_START`, `_CHANNEL_UPDATE`, `_CHANNEL_STOP`), so it looks and behaves
//! exactly as a classic cast: the gold fill and spark, the spell's name, the green completion flash
//! and fade, red "Failed" and "Interrupted".
//!
//! A free-aimed charge (crate::spellcast) fills the bar over the spell's charge time. Held on, the
//! bar refills a second time in violet ("Empowering") toward the full charge
//! ([`crate::spellcast::FULL_CHARGE_FACTOR`] times the charge, 2.25x as hard); reached, it holds full with
//! the stock gold flash border lit and reads "Full Charge". Let go: the stock completion. Let go
//! early: "Failed" (the fizzle). The hands put away mid-charge: "Interrupted". Each hand's charge
//! is its own: the stock bar follows the older one, and the other hand's charge fills a second,
//! smaller bar of its own just above it ([`second_charge`], `WomwHandCastBar`), with its own
//! spell, time and stage. Both hands charging the same spell read "(Dual)". A spray (Scorch, Cone of Cold) pours as a channel bar draining with the mana it
//! burns; refused by the server, it reads "Interrupted".

use benilla_ui::script::{ScriptValue, UiScript};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::hands::{Hand, Hands, SpellTable};
use crate::melee::MeleeHeld;
use crate::mode::FpsState;
use crate::spellcast::{FULL_CHARGE_FACTOR, HandSpells};

/// The violet of the second stage, refilling toward the full charge.
pub const EMPOWER_COLOR: (f32, f32, f32) = (0.7, 0.4, 1.0);
/// A spray's channel bar is this long at full mana, drained by the mana left.
pub const SPRAY_BAR_MS: u32 = 10_000;

/// What the hands show the bar this frame.
#[derive(Debug, Clone, PartialEq)]
pub enum View {
    Idle,
    /// A charge held (or let go fully charged and waiting for its pair, its time stopped at
    /// `until`): since when, how long.
    Charging {
        label: String,
        started: f32,
        secs: f32,
        until: Option<f32>,
    },
    /// A spray pouring, with the mana left (0..=1).
    Pouring {
        label: String,
        mana: f32,
    },
}

/// What happened to the hands this frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Outcome {
    /// A charge cast (its flash).
    Released,
    /// Let go early, or refused by the server.
    Fizzled,
}

/// The charge's stage on the bar.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Stage {
    /// Filling over the charge time (stock gold).
    Charging,
    /// Charged; refilling in violet toward the full charge.
    Empowering,
    /// The full charge: held full, the flash border lit.
    Full,
}

/// What the stock bar is doing, read back from its Lua (something else, a stray
/// `SPELLCAST_STOP` from the server's cast, may have ended our bar).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Seen {
    /// `CastingBarFrame.casting` (filling).
    pub casting: bool,
    /// `CastingBarFrame.fadeOut` (completed or failed, fading away).
    pub fading: bool,
}

/// One thing to do to the stock bar.
#[derive(Debug, Clone, PartialEq)]
pub enum Edge {
    /// `SPELLCAST_START(label, total)`, then the bar wound on by `elapsed_ms` and coloured.
    Start {
        label: String,
        total_ms: u32,
        elapsed_ms: u32,
        color: Option<(f32, f32, f32)>,
    },
    /// The label changed (a second hand joined or left).
    Text(String),
    /// The full charge: held full in violet, the stock flash lit.
    Full(String),
    Stop,
    Failed,
    Interrupted,
    ChannelStart {
        ms: u32,
        label: String,
    },
    ChannelLeft(u32),
    ChannelStop,
}

#[derive(Debug, Clone, PartialEq, Default)]
enum Phase {
    #[default]
    Idle,
    Charge {
        started: f32,
        secs: f32,
        stage: Stage,
        label: String,
    },
    Channel,
}

/// The bar's state machine: [`CastBar::step`] once a frame.
#[derive(Resource, Debug, Default)]
pub struct CastBar {
    phase: Phase,
}

fn stage_at(started: f32, secs: f32, t: f32) -> Stage {
    let held = t - started;
    if secs <= 0.0 || held >= secs * FULL_CHARGE_FACTOR {
        Stage::Full
    } else if held >= secs {
        Stage::Empowering
    } else {
        Stage::Charging
    }
}

fn ms(secs: f32) -> u32 {
    (secs.max(0.0) * 1000.0).round() as u32
}

fn staged_label(label: &str, stage: Stage) -> String {
    match stage {
        Stage::Charging => label.to_owned(),
        Stage::Empowering => format!("{label} - Empowering"),
        Stage::Full => format!("{label} - Full Charge"),
    }
}

/// The bar for `stage`, wound on to where the charge is at `t`.
fn start_edge(label: &str, started: f32, secs: f32, stage: Stage, t: f32) -> Edge {
    match stage {
        Stage::Charging => Edge::Start {
            label: staged_label(label, stage),
            total_ms: ms(secs),
            elapsed_ms: ms(t - started),
            color: None,
        },
        Stage::Empowering => Edge::Start {
            label: staged_label(label, stage),
            total_ms: ms(secs * (FULL_CHARGE_FACTOR - 1.0)),
            elapsed_ms: ms(t - started - secs),
            color: Some(EMPOWER_COLOR),
        },
        Stage::Full => Edge::Full(staged_label(label, stage)),
    }
}

impl CastBar {
    /// The stage of the charge shown, if one is.
    pub fn stage(&self) -> Option<Stage> {
        match self.phase {
            Phase::Charge { stage, .. } => Some(stage),
            _ => None,
        }
    }

    /// Whether a channel bar is shown.
    pub fn channeling(&self) -> bool {
        self.phase == Phase::Channel
    }

    /// This frame's edges for the hands' `view`, what befell them (`outcome`) and what the stock
    /// bar is doing (`seen`).
    pub fn step(&mut self, view: View, outcome: Option<Outcome>, seen: Seen, t: f32) -> Vec<Edge> {
        let mut out = Vec::new();
        match view {
            View::Idle => {
                match (&self.phase, outcome) {
                    (Phase::Charge { .. }, Some(Outcome::Released)) => out.push(Edge::Stop),
                    (Phase::Charge { .. }, Some(Outcome::Fizzled)) => out.push(Edge::Failed),
                    // Put away mid-charge: the hands down, another spell readied.
                    (Phase::Charge { .. }, None) => out.push(Edge::Interrupted),
                    (Phase::Channel, Some(Outcome::Fizzled)) => {
                        out.extend([Edge::ChannelStop, Edge::Interrupted]);
                    }
                    (Phase::Channel, _) => out.push(Edge::ChannelStop),
                    // The server refused a release already flashed.
                    (Phase::Idle, Some(Outcome::Fizzled)) => out.push(Edge::Failed),
                    (Phase::Idle, _) => {}
                }
                self.phase = Phase::Idle;
            }
            View::Charging {
                label,
                started,
                secs,
                until,
            } => {
                let t = until.map_or(t, |u| u.min(t));
                let stage = stage_at(started, secs, t);
                match &mut self.phase {
                    Phase::Charge {
                        started: s,
                        stage: shown,
                        label: l,
                        ..
                    } if *s == started => {
                        if *shown != stage {
                            *shown = stage;
                            out.push(start_edge(&label, started, secs, stage, t));
                        } else if (stage != Stage::Full && !seen.casting)
                            || (stage == Stage::Full && seen.fading)
                        {
                            // Something else ended our bar: put it back as it was.
                            out.push(start_edge(&label, started, secs, stage, t));
                        } else if *l != label {
                            out.push(Edge::Text(staged_label(&label, stage)));
                        }
                        *l = label;
                    }
                    phase => {
                        if *phase == Phase::Channel {
                            out.push(Edge::ChannelStop);
                        }
                        out.push(start_edge(&label, started, secs, stage, t));
                        *phase = Phase::Charge {
                            started,
                            secs,
                            stage,
                            label,
                        };
                    }
                }
            }
            View::Pouring { label, mana } => {
                if self.phase != Phase::Channel {
                    out.push(Edge::ChannelStart {
                        ms: SPRAY_BAR_MS,
                        label,
                    });
                    self.phase = Phase::Channel;
                }
                out.push(Edge::ChannelLeft(
                    ((mana.clamp(0.0, 1.0) * SPRAY_BAR_MS as f32) as u32).max(1),
                ));
            }
        }
        out
    }
}

/// Lua that reads [`Seen`] back.
pub const SEEN_LUA: &str = "local f = CastingBarFrame \
    if not f then return false, false end \
    return f.casting and true or false, f.fadeOut and true or false";

/// The full charge: the bar held full in violet, the stock flash border lit (its own `OnUpdate`
/// ramps it in and, with nothing else set, leaves it lit).
fn full_lua(label: &str) -> String {
    let (r, g, b) = EMPOWER_COLOR;
    format!(
        "local f = CastingBarFrame if f and f:IsShown() then \
         f.casting = nil f.channeling = nil f.fadeOut = nil f.holdTime = 0 \
         local _, mx = f:GetMinMaxValues() f:SetValue(mx) f:SetStatusBarColor({r}, {g}, {b}) \
         f:SetAlpha(1.0) CastingBarSpark:Hide() CastingBarFlash:SetAlpha(0) CastingBarFlash:Show() \
         f.flash = 1 CastingBarText:SetText({}) end",
        crate::lua::quote(label)
    )
}

/// Do `edges` to the stock bar in `script`.
pub fn apply(script: &mut UiScript, edges: Vec<Edge>) {
    for edge in edges {
        match edge {
            Edge::Start {
                label,
                total_ms,
                elapsed_ms,
                color,
            } => {
                script.fire_event(
                    "SPELLCAST_START",
                    vec![
                        ScriptValue::Str(label),
                        ScriptValue::Int(i64::from(total_ms.max(1))),
                    ],
                );
                let mut lua = format!(
                    "local f = CastingBarFrame if f and f.casting then \
                     f.startTime = GetTime() - {e} f.maxValue = f.startTime + {m} \
                     f:SetMinMaxValues(f.startTime, f.maxValue) f:SetValue(GetTime())",
                    e = f64::from(elapsed_ms) / 1000.0,
                    m = f64::from(total_ms.max(1)) / 1000.0,
                );
                if let Some((r, g, b)) = color {
                    lua.push_str(&format!(" f:SetStatusBarColor({r}, {g}, {b})"));
                }
                lua.push_str(" end");
                crate::lua::run(script, &lua);
            }
            Edge::Text(label) => crate::lua::run(
                script,
                &format!(
                    "if CastingBarText then CastingBarText:SetText({}) end",
                    crate::lua::quote(&label)
                ),
            ),
            Edge::Full(label) => crate::lua::run(script, &full_lua(&label)),
            Edge::Stop => script.fire_event("SPELLCAST_STOP", vec![]),
            Edge::Failed => script.fire_event("SPELLCAST_FAILED", vec![]),
            Edge::Interrupted => script.fire_event("SPELLCAST_INTERRUPTED", vec![]),
            Edge::ChannelStart { ms, label } => script.fire_event(
                "SPELLCAST_CHANNEL_START",
                vec![ScriptValue::Int(i64::from(ms)), ScriptValue::Str(label)],
            ),
            Edge::ChannelLeft(ms) => script.fire_event(
                "SPELLCAST_CHANNEL_UPDATE",
                vec![ScriptValue::Int(i64::from(ms))],
            ),
            Edge::ChannelStop => script.fire_event("SPELLCAST_CHANNEL_STOP", vec![]),
        }
    }
}

/// The other hand's charge, while both hands charge: the younger of the two, for the second bar.
/// Its label, when it began, its charge time and when it was let go (waiting for its pair).
pub fn second_charge(hands: &Hands, name: impl Fn(u32) -> String) -> Option<(String, f32, f32, Option<f32>)> {
    let mut charges: Vec<(f32, usize, Option<f32>)> = (0..2)
        .filter_map(|side| match (hands.charging[side], hands.pending[side]) {
            (Some(started), _) => Some((started, side, None)),
            (None, Some(at)) => Some((hands.charge_from[side], side, Some(at))),
            _ => None,
        })
        .collect();
    if charges.len() < 2 {
        return None;
    }
    charges.sort_by(|a, b| a.0.total_cmp(&b.0));
    let (started, side, until) = charges[1];
    let spell = hands.spell_in(side)?.1;
    let mut label = name(spell);
    if hands.spell_in(charges[0].1).map(|s| s.1) == Some(spell) {
        label = label_for(&[label.clone(), label]);
    }
    Some((label, started, hands.charge_secs[side], until))
}

/// The second bar's Lua for this frame: made once, then filled to the charge's stage at `t`, or
/// hidden (`None`).
pub fn second_bar_lua(charge: Option<&(String, f32, f32, Option<f32>)>, t: f32) -> String {
    const MAKE: &str = "local b = WomwHandCastBar         if not b and CastingBarFrame then           b = CreateFrame(\"StatusBar\", \"WomwHandCastBar\", UIParent)           b:SetWidth(160) b:SetHeight(8)           b:SetPoint(\"BOTTOM\", CastingBarFrame, \"TOP\", 0, 14)           b:SetStatusBarTexture(\"Interface\\TargetingFrame\\UI-StatusBar\")           b:SetMinMaxValues(0, 1)           local bg = b:CreateTexture(nil, \"BACKGROUND\") bg:SetAllPoints(b) bg:SetTexture(0, 0, 0, 0.6)           b.text = b:CreateFontString(nil, \"OVERLAY\", \"GameFontHighlightSmall\")           b.text:SetPoint(\"BOTTOM\", b, \"TOP\", 0, 1)           b:Hide()         end ";
    let Some((label, started, secs, until)) = charge else {
        return format!("{MAKE} if b then b:Hide() end");
    };
    let t = until.map_or(t, |u| u.min(t));
    let stage = stage_at(*started, *secs, t);
    let held = t - started;
    let (value, (r, g, b)) = match stage {
        Stage::Charging => ((held / secs.max(1e-3)).clamp(0.0, 1.0), (1.0, 0.7, 0.0)),
        Stage::Empowering => (
            ((held - secs) / (secs * (FULL_CHARGE_FACTOR - 1.0)).max(1e-3)).clamp(0.0, 1.0),
            EMPOWER_COLOR,
        ),
        Stage::Full => (1.0, (1.0, 0.85, 1.0)),
    };
    format!(
        "{MAKE} if b then b:SetValue({value}) b:SetStatusBarColor({r}, {g}, {b})          b.text:SetText({}) b:Show() end",
        crate::lua::quote(&staged_label(label, stage))
    )
}

/// The bar's label for the hands' spells: one name, the same in both hands "(Dual)", two names.
pub fn label_for(names: &[String]) -> String {
    match names {
        [] => String::new(),
        [a] => a.clone(),
        [a, b] if a == b => format!("{a} (Dual)"),
        [a, b, ..] => format!("{a} + {b}"),
    }
}

/// The hands as the bar sees them: the charges held (oldest first) or the sprays pouring.
pub fn view_of(hands: &Hands, name: impl Fn(u32) -> String, mana: f32) -> View {
    let spell = |side: usize| match if side == 0 { hands.right } else { hands.left } {
        Hand::Spell { spell_id, .. } => Some(spell_id),
        _ => None,
    };
    // A charge held, or let go fully charged and waiting a moment for its pair.
    let mut charges: Vec<(f32, usize, Option<f32>)> = (0..2)
        .filter_map(|side| match (hands.charging[side], hands.pending[side]) {
            (Some(started), _) => Some((started, side, None)),
            (None, Some(at)) => Some((hands.charge_from[side], side, Some(at))),
            _ => None,
        })
        .collect();
    charges.sort_by(|a, b| a.0.total_cmp(&b.0));
    if let Some(&(started, side, until)) = charges.first() {
        let mut label = spell(side).map(&name).unwrap_or_default();
        // The same spell in the other hand, charging too: one dual cast to come.
        if charges.len() > 1 && spell(charges[1].1) == spell(side) {
            label = label_for(&[label.clone(), label]);
        }
        return View::Charging {
            label,
            started,
            secs: hands.charge_secs[side],
            until,
        };
    }
    let pouring: Vec<String> = (0..2)
        .filter(|&side| hands.spraying[side].is_some())
        .map(|side| name(hands.spray_spell[side]))
        .collect();
    if pouring.is_empty() {
        View::Idle
    } else {
        View::Pouring {
            label: label_for(&pouring),
            mana,
        }
    }
}

pub struct CastBarPlugin;

impl Plugin for CastBarPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<CastBar>()
            .add_systems(Update, (drive, drive_second).in_set(Mw2Set::Hud));
    }
}

#[allow(clippy::too_many_arguments)]
fn drive(
    time: Res<Time>,
    state: Res<FpsState>,
    held: Res<MeleeHeld>,
    hands: Res<Hands>,
    spells: Res<HandSpells>,
    table: Res<SpellTable>,
    script: Option<NonSendMut<UiScript>>,
    mut bar: ResMut<CastBar>,
    mut last: Local<([Option<f32>; 2], [Option<f32>; 2])>,
) {
    let Some(mut script) = script else {
        return;
    };
    if !state.in_world {
        return;
    }
    let t = time.elapsed_secs();
    let (released, fizzled) = (hands.released_at, hands.fizzled_at);
    let outcome = if fizzled != last.1 {
        Some(Outcome::Fizzled)
    } else if released != last.0 {
        Some(Outcome::Released)
    } else {
        None
    };
    *last = (released, fizzled);
    let view = if state.active && held.0 {
        let pouring = hands.spraying.iter().any(Option::is_some);
        let mana = if pouring {
            script
                .eval::<(Option<f32>, Option<f32>)>(
                    "return UnitMana(\"player\"), UnitManaMax(\"player\")",
                )
                .ok()
                .and_then(|(m, mx)| Some((m?, mx?)))
                .filter(|&(_, mx)| mx > 0.0)
                .map_or(1.0, |(m, mx)| m / mx)
        } else {
            1.0
        };
        let name = |id: u32| {
            table
                .0
                .as_ref()
                .and_then(|c| c.get(id))
                .map(|d| d.name.clone())
                .unwrap_or_default()
        };
        let v = view_of(&hands, name, mana);
        // Only the spells the server charges (a bar button's cast at the press shows nothing).
        match &v {
            View::Charging { .. }
                if !(0..2).any(|s| {
                    hands
                        .spell_in(s)
                        .is_some_and(|(_, id)| spells.0.contains_key(&id))
                }) =>
            {
                View::Idle
            }
            _ => v,
        }
    } else {
        View::Idle
    };
    if view == View::Idle && bar.stage().is_none() && !bar.channeling() && outcome.is_none() {
        return;
    }
    let seen =
        script
            .eval::<(bool, bool)>(SEEN_LUA)
            .map_or(Seen::default(), |(casting, fading)| Seen {
                casting,
                fading,
            });
    let edges = bar.step(view, outcome, seen, t);
    apply(&mut script, edges);
}

/// The other hand's own bar (the stock one follows the older charge): filled each frame while
/// both hands charge, hidden once they don't.
#[allow(clippy::too_many_arguments)]
fn drive_second(
    time: Res<Time>,
    state: Res<FpsState>,
    held: Res<MeleeHeld>,
    hands: Res<Hands>,
    spells: Res<HandSpells>,
    table: Res<SpellTable>,
    script: Option<NonSend<UiScript>>,
    mut shown: Local<bool>,
) {
    let Some(script) = script else {
        return;
    };
    if !state.in_world {
        return;
    }
    let name = |id: u32| {
        table
            .0
            .as_ref()
            .and_then(|c| c.get(id))
            .map(|d| d.name.clone())
            .unwrap_or_default()
    };
    let second = (state.active && held.0)
        .then(|| second_charge(&hands, name))
        .flatten()
        .filter(|_| {
            (0..2).all(|s| {
                hands
                    .spell_in(s)
                    .is_some_and(|(_, id)| spells.0.contains_key(&id))
            })
        });
    if second.is_none() && !*shown {
        return;
    }
    *shown = second.is_some();
    crate::lua::run(
        &script,
        &second_bar_lua(second.as_ref(), time.elapsed_secs()),
    );
}

#[cfg(test)]
mod tests;
