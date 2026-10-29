//! The Companion action system (crate::action_systems, C): the battle companion's pet bar on the
//! main bar's six large buttons, two pages, in place of the stock pet bar (which is no longer
//! shown, crate::main_bar). Each entry is one of the companion's pet bar slots
//! ([`Ability::CompanionSlot1`]..10, saved in Lua actions 217-228 like the other systems'
//! entries): a slot key or a click presses that slot over the companion channel, aimed by the
//! crosshair as Ctrl+1..0 is (crate::companion `pet_keys`); a right click turns an ability's
//! autocast on or off, as the stock pet bar's right click does.
//!
//! Page 1 holds its moves: Command (attack the crosshair's enemy, move to the ground under it, a
//! double tap follows), the three stage picks, the ride (or, ridden, the mounted move), a
//! Twin-souled stage's other move. Page 2 its commands and stances: Assist/Aggressive, Hold,
//! Follow, Stay. With no companion out the buttons are empty and C says so.
//!
//! Every slot's look (name, icon, lit while autocast is on, greyed while it cools down or cannot
//! be used) is [`entry`]'s, the one place a Companion slot is rendered; crate::action_systems'
//! `show_bar` takes the looks from [`CompanionBarLooks`] and crate::ability_tips the tooltips.
//!
//! **The ride's look.** A companion is ridden on a vanilla mount spell (every flying line, and the
//! war wolf, on the Timber Wolf's 580 / 23250), whose own name and icon would show on the buff
//! and on the bar. While riding, the player's mount aura is re-dressed client-side through
//! benilla's [`LayerMountLook`] seam as "Ride <form>" with the family's icon, and the bar's ride
//! entry is named the same. Chosen over dedicated server spell ids plus client DBC rows: the
//! mount spells bring vanilla's speed auras, rules and the server's ride bookkeeping (`LookAtRide`,
//! `StrayRideAura`) unchanged, there is no DBC to ship or keep in step, and it covers every mount
//! line (species 215-223 included) with one rule (any helpful mounting aura while the companion
//! is ridden).

use std::collections::HashMap;

use benilla_app::fusion::{FusionLook, LayerMountLook, MountLook, SendFusionChannel};
use benilla_formats::SpellCatalog;
use benilla_ui::script::LayerTipLine;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::ability_tips::{GOLD, GREEN, GREY, WHITE};
use crate::action_systems::{Ability, ActionSystems, COMPANION_SLOTS, System};
use crate::companion::{
    BAR_RIDEABLE, BAR_RIDING, CompanionState, Request, encode_request,
};
use crate::companion_icons as icons;

/// The pet bar slots (`Companions::PetBarSlot`, 0-based).
pub mod slot {
    pub const COMMAND: u8 = 0;
    pub const STANCE: u8 = 1;
    pub const HOLD: u8 = 2;
    pub const ABILITY1: u8 = 3;
    pub const ABILITY3: u8 = 5;
    pub const MOUNTED: u8 = 6;
    pub const TWIN: u8 = 7;
    pub const FOLLOW: u8 = 8;
    pub const STAY: u8 = 9;
}

/// The mounted move that is a flying line's take-off and climb (`CompanionMoves.h` kSkySurge).
pub const SKY_SURGE: &str = "Launch";

/// The stock pet bar's command and stance icons (`PetActionBarFrame.lua`'s textures).
const ATTACK_ICON: &str = "Interface\\Icons\\Ability_GhoulFrenzy";
const FOLLOW_ICON: &str = "Interface\\Icons\\Ability_Tracking";
const STAY_ICON: &str = "Interface\\Icons\\Spell_Nature_TimeStop";
const AGGRESSIVE_ICON: &str = "Interface\\Icons\\Ability_Racial_BloodRage";
const PASSIVE_ICON: &str = "Interface\\Icons\\Ability_Seal";

/// One Companion slot as the bar shows it.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Entry {
    pub name: String,
    pub icon: String,
    /// What it does, for the tooltip.
    pub does: String,
    /// Autocast: `None` when the slot has none, else whether it is on.
    pub autocast: Option<bool>,
    /// It may be pressed now (a companion out, the slot filled, not cooling down).
    pub usable: bool,
}

/// What [`entry`] reads of the companion and the world.
pub struct Ctx<'a> {
    pub st: &'a CompanionState,
    /// The companion's family (the Bestiary's), 0 unknown.
    pub family: u8,
    /// Its current form's name ("War Wolf"), else its species'.
    pub form: String,
    /// The kinds of its stage picks (`AbilityKind`), by stage and option, for icons.
    pub kinds: [[u8; 2]; 3],
    pub spells: Option<&'a SpellCatalog>,
    pub now: f32,
}

/// "Ride <form>": the ride's name on the bar and on the buff.
pub fn ride_name(form: &str) -> String {
    format!("Ride {form}")
}

/// The pick a stage's slot holds (`Companions::AbilityInSlot`): its stage and option (0-based).
fn pick_of(st: &CompanionState, slot: u8) -> Option<(usize, usize)> {
    let m = &st.moves;
    let stage = if (slot::ABILITY1..=slot::ABILITY3).contains(&slot) {
        usize::from(slot - slot::ABILITY1)
    } else if slot == slot::TWIN && (1..=3).contains(&m.twin_stage) {
        usize::from(m.twin_stage - 1)
    } else {
        return None;
    };
    let pick = *m.picks.get(stage)?;
    if stage > usize::from(st.bar.stage) || !(1..=2).contains(&pick) {
        return None;
    }
    let option = usize::from(pick - 1);
    // A Twin-souled stage's other move: the option not picked.
    let option = if slot == slot::TWIN { 1 - option } else { option };
    (m.spells[stage][option] != 0).then_some((stage, option))
}

/// One pet bar slot (0-9) as the Companion bar shows it: the one place a Companion slot is
/// rendered.
pub fn entry(slot: u8, c: &Ctx) -> Entry {
    let st = c.st;
    let out = st.bar.out();
    let riding = st.bar.flags & BAR_RIDING != 0;
    let ready = st
        .ready_at
        .get(usize::from(slot))
        .is_none_or(|&t| t <= c.now);
    let autocast_on = st.moves.autocast & (1 << slot) != 0;
    let mut e = match slot {
        slot::COMMAND => Entry {
            name: "Command".into(),
            icon: ATTACK_ICON.into(),
            does: "Attacks the enemy under the crosshair, or moves to the ground there and holds \
                   it. Press twice quickly to call it back to follow."
                .into(),
            ..Default::default()
        },
        slot::STANCE => Entry {
            name: "Assist / Aggressive".into(),
            icon: AGGRESSIVE_ICON.into(),
            does: "Switches between Assist (it fights what you fight, and what attacks it) and \
                   Aggressive (it attacks enemies in range)."
                .into(),
            ..Default::default()
        },
        slot::HOLD => Entry {
            name: "Hold".into(),
            icon: PASSIVE_ICON.into(),
            does: "Passive: it stays where it is and does not fight back.".into(),
            ..Default::default()
        },
        slot::FOLLOW => Entry {
            name: "Follow".into(),
            icon: FOLLOW_ICON.into(),
            does: "It stops what it is doing and follows you.".into(),
            ..Default::default()
        },
        slot::STAY => Entry {
            name: "Stay".into(),
            icon: STAY_ICON.into(),
            does: "It stays where it stands.".into(),
            ..Default::default()
        },
        slot::MOUNTED if riding && !st.moves.mounted_name.is_empty() => {
            let surge = st.moves.mounted_name == SKY_SURGE;
            Entry {
                name: st.moves.mounted_name.clone(),
                icon: icons::move_icon(st.moves.mounted_spell, 15, c.spells),
                does: if surge {
                    "Spends a vigor charge: takes off from the ground, or climbs straight up in \
                     the air. Also Ctrl+Space."
                        .into()
                } else {
                    "Its mounted move, used while you ride it.".into()
                },
                ..Default::default()
            }
        }
        slot::MOUNTED if st.bar.flags & BAR_RIDEABLE != 0 => Entry {
            name: ride_name(&c.form),
            icon: icons::mount_icon(c.family),
            does: format!("Rides your {} (its last form is a mount).", c.form),
            ..Default::default()
        },
        s => match pick_of(st, s) {
            Some((stage, option)) => Entry {
                name: st.moves.names[stage][option].clone(),
                icon: icons::move_icon(
                    st.moves.spells[stage][option],
                    c.kinds[stage][option],
                    c.spells,
                ),
                does: if s == slot::TWIN {
                    "Twin-souled: the move its stage did not pick, as well.".into()
                } else {
                    format!("Its stage {} move.", stage + 1)
                },
                autocast: Some(autocast_on),
                ..Default::default()
            },
            None => Entry::default(),
        },
    };
    // The pet is away while ridden: only the mounted move and the ride work.
    let away = riding && slot != slot::MOUNTED;
    e.usable = out && ready && !away && !e.name.is_empty();
    if !out {
        e.icon.clear();
    }
    e
}

/// The Companion slots' looks and tooltips, by ability id, for crate::action_systems and
/// crate::ability_tips.
#[derive(Resource, Debug, Default, Clone, PartialEq)]
pub struct CompanionBarLooks {
    pub looks: HashMap<u32, FusionLook>,
    pub tips: HashMap<u32, Vec<LayerTipLine>>,
}

fn tip_of(e: &Entry, out: bool) -> Vec<LayerTipLine> {
    let line = |t: &str, color: [f32; 4], wrap: bool| LayerTipLine {
        left: t.to_string(),
        left_color: color,
        right: None,
        wrap,
    };
    if !out {
        return vec![
            line("Companion", WHITE, false),
            line(
                "No companion out. Summon one by right-clicking its item in your bags (the \
                 Companions window: N).",
                GOLD,
                true,
            ),
        ];
    }
    if e.name.is_empty() {
        return vec![line("Empty", GREY, false)];
    }
    let mut v = vec![line(&e.name, WHITE, false), line(&e.does, GOLD, true)];
    if let Some(on) = e.autocast {
        v.push(line(
            if on {
                "Autocast on (right-click to turn it off)"
            } else {
                "Autocast off (right-click to turn it on)"
            },
            if on { GREEN } else { GREY },
            false,
        ));
    }
    v
}

pub struct CompanionBarPlugin;

impl Plugin for CompanionBarPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<CompanionBarLooks>()
            .add_systems(
                Update,
                build_looks
                    .in_set(Mw2Set::Mode)
                    .before(crate::action_systems::ActionSystemsSet),
            )
            .add_systems(Update, mount_cast_bar.in_set(Mw2Set::Hud))
            .add_systems(
                Update,
                (autocast_clicks, no_companion_hint)
                    .in_set(Mw2Set::Mode)
                    .after(crate::action_systems::ActionSystemsSet),
            );
    }
}

/// The looks, the tooltips and the ride's buff look, from what the server said.
fn build_looks(
    time: Res<Time>,
    st: Res<CompanionState>,
    ui: Res<crate::companion_ui::CompanionUi>,
    table: Option<Res<crate::hands::SpellTable>>,
    mut bar_looks: ResMut<CompanionBarLooks>,
    mut mount: ResMut<LayerMountLook>,
) {
    let species = st.bar.species;
    let entry_of = ui.index.get(&species);
    let form = crate::companion_ui::form_name(species, st.bar.stage)
        .filter(|n| !n.is_empty())
        .unwrap_or_else(|| crate::companion::species_name(species).to_string());
    let mut kinds = [[0u8; 2]; 3];
    if let Some(e) = entry_of {
        for (i, s) in e.stages.iter().take(3).enumerate() {
            kinds[i] = s.kinds;
        }
    }
    let c = Ctx {
        st: &st,
        family: ui.family_of(species),
        form: form.clone(),
        kinds,
        spells: table.as_deref().and_then(|t| t.0.as_deref()),
        now: time.elapsed_secs(),
    };
    let out = st.bar.out();
    let mut built = CompanionBarLooks::default();
    for (i, a) in COMPANION_SLOTS.iter().enumerate() {
        let e = entry(i as u8, &c);
        built.tips.insert(a.id(), tip_of(&e, out));
        built.looks.insert(
            a.id(),
            FusionLook {
                texture: e.icon.clone(),
                count: 0,
                checked: e.autocast == Some(true),
                usable: e.usable,
            },
        );
    }
    if *bar_looks != built {
        *bar_looks = built;
    }
    let look = (st.bar.flags & BAR_RIDING != 0).then(|| MountLook {
        name: ride_name(&form),
        icon: icons::mount_icon(c.family),
    });
    if mount.0 != look {
        mount.0 = look;
    }
}

/// The stock cast bar for the mount cast (op 7): "Ride <form>" filling while the companion
/// runs to its rider, completed in the saddle, "Interrupted" when it breaks.
pub fn mount_cast_edges(news: (u8, u32), form: &str) -> Vec<crate::cast_bar::Edge> {
    use crate::cast_bar::Edge;
    use crate::companion::{MOUNT_CAST_DONE, MOUNT_CAST_START};
    match news {
        (MOUNT_CAST_START, ms) => vec![Edge::Start {
            label: ride_name(form),
            total_ms: ms.max(1),
            elapsed_ms: 0,
            color: None,
        }],
        (MOUNT_CAST_DONE, _) => vec![Edge::Stop],
        _ => vec![Edge::Interrupted],
    }
}

fn mount_cast_bar(
    mut st: ResMut<CompanionState>,
    script: Option<NonSendMut<benilla_ui::script::UiScript>>,
) {
    let Some(news) = st.mount_cast.take() else {
        return;
    };
    let Some(mut script) = script else {
        return;
    };
    let species = st.bar.species;
    let form = crate::companion_ui::form_name(species, st.bar.stage)
        .filter(|n| !n.is_empty())
        .unwrap_or_else(|| crate::companion::species_name(species).to_string());
    crate::cast_bar::apply(&mut script, mount_cast_edges(news, &form));
}

/// A right click on a Companion slot: its autocast on or off (the server answers with op 4).
fn autocast_clicks(
    input: Res<crate::input::Mw2Input>,
    st: Res<CompanionState>,
    mut out: MessageWriter<SendFusionChannel>,
) {
    let Some(slot) = input.pet_autocast else {
        return;
    };
    if !st.bar.out() || pick_of(&st, slot).is_none() {
        return;
    }
    let on = st.moves.autocast & (1 << slot) == 0;
    out.write(SendFusionChannel(benilla_app::fusion::ChannelMessage {
        channel: crate::companion::CHANNEL_COMPANION,
        body: encode_request(Request::Autocast { slot, on }),
    }));
}

/// C with no companion out: the bar shows empty, and the error frame says why.
fn no_companion_hint(
    systems: Res<ActionSystems>,
    st: Res<CompanionState>,
    script: Option<NonSend<benilla_ui::script::UiScript>>,
    mut was: Local<bool>,
) {
    let now = systems.shown() == System::Companion;
    if now && !*was && !st.bar.out()
        && let Some(script) = script
    {
        crate::lua::run(
            &script,
            "if UIErrorsFrame then UIErrorsFrame:AddMessage(\"No companion out: summon one from \
             your bags (Companions window: N)\", 1.0, 0.82, 0.0, 1.0, UIERRORS_HOLD_TIME) end",
        );
    }
    *was = now;
}

/// Whether Ctrl+Space is Launch now: riding a flying line (its mounted move is Launch).
pub fn sky_surge_ready(st: &CompanionState) -> bool {
    st.bar.flags & BAR_RIDING != 0 && st.moves.mounted_name == SKY_SURGE
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::companion::{BAR_SUMMONED, Bar, Moves};

    #[test]
    fn mounting_shows_a_cast_bar_named_for_the_ride() {
        use crate::cast_bar::Edge;
        use crate::companion::{MOUNT_CAST_BROKEN, MOUNT_CAST_DONE, MOUNT_CAST_START};
        assert_eq!(
            mount_cast_edges((MOUNT_CAST_START, 2300), "Dire Wolf"),
            vec![Edge::Start {
                label: "Ride Dire Wolf".into(),
                total_ms: 2300,
                elapsed_ms: 0,
                color: None
            }]
        );
        assert_eq!(mount_cast_edges((MOUNT_CAST_DONE, 0), "x"), vec![Edge::Stop]);
        assert_eq!(
            mount_cast_edges((MOUNT_CAST_BROKEN, 0), "x"),
            vec![Edge::Interrupted]
        );
    }

    fn state(flags: u8) -> CompanionState {
        CompanionState {
            bar: Bar {
                item: 7,
                species: 1,
                stage: 2,
                flags,
                ..Default::default()
            },
            moves: Moves {
                picks: [1, 2, 0],
                spells: [[100, 101], [200, 201], [300, 301]],
                names: [
                    ["Bite".into(), "Claw".into()],
                    ["Howl".into(), "Rend".into()],
                    ["Pounce".into(), "Leap".into()],
                ],
                autocast: 1 << 3,
                mounted_spell: 130,
                mounted_name: SKY_SURGE.into(),
                ..Default::default()
            },
            ..Default::default()
        }
    }

    fn ctx(st: &CompanionState) -> Ctx<'_> {
        Ctx {
            st,
            family: 3,
            form: "War Wolf".into(),
            kinds: [[0; 2]; 3],
            spells: None,
            now: 10.0,
        }
    }

    #[test]
    fn the_bar_shows_the_companions_moves_commands_and_autocast() {
        let st = state(BAR_SUMMONED | BAR_RIDEABLE);
        let c = ctx(&st);
        assert_eq!(entry(slot::COMMAND, &c).name, "Command");
        assert_eq!(entry(slot::FOLLOW, &c).name, "Follow");
        assert_eq!(entry(slot::STAY, &c).name, "Stay");
        let bite = entry(3, &c);
        assert_eq!(bite.name, "Bite", "stage 1 picked option 1");
        assert_eq!(bite.autocast, Some(true));
        assert!(bite.usable);
        let rend = entry(4, &c);
        assert_eq!(rend.name, "Rend", "stage 2 picked option 2");
        assert_eq!(rend.autocast, Some(false));
        assert_eq!(entry(5, &c), Entry::default(), "stage 3 not picked: empty");
        assert_eq!(entry(slot::COMMAND, &c).autocast, None, "commands have no autocast");
    }

    #[test]
    fn the_ride_is_named_after_the_companion_never_the_wolf_spell() {
        let st = state(BAR_SUMMONED | BAR_RIDEABLE);
        let ride = entry(slot::MOUNTED, &ctx(&st));
        assert_eq!(ride.name, "Ride War Wolf");
        assert_eq!(ride.icon, icons::mount_icon(3));
        // Ridden: the mounted move takes its place, and the pet's own moves wait.
        let st = state(BAR_RIDING | BAR_RIDEABLE);
        let c = ctx(&st);
        let surge = entry(slot::MOUNTED, &c);
        assert_eq!(surge.name, SKY_SURGE);
        assert!(surge.usable);
        assert!(!entry(3, &c).usable, "the pet is away while ridden");
        assert!(sky_surge_ready(&st));
        assert!(!sky_surge_ready(&state(BAR_SUMMONED | BAR_RIDEABLE)));
    }

    #[test]
    fn no_companion_out_shows_empty_buttons_and_a_hint() {
        let st = state(0);
        let c = ctx(&st);
        for s in 0..10 {
            let e = entry(s, &c);
            assert!(e.icon.is_empty() && !e.usable, "slot {s}");
        }
        let tip = tip_of(&entry(0, &c), false);
        assert!(tip[1].left.contains("No companion out"));
    }

    #[test]
    fn a_slot_cooling_down_is_greyed() {
        let mut st = state(BAR_SUMMONED);
        st.ready_at[3] = 20.0;
        assert!(!entry(3, &ctx(&st)).usable);
    }

    #[test]
    fn the_twin_slot_is_the_option_not_picked() {
        let mut st = state(BAR_SUMMONED);
        st.moves.twin_stage = 2;
        assert_eq!(entry(slot::TWIN, &ctx(&st)).name, "Howl");
    }

    #[test]
    fn every_companion_slot_has_its_own_ability_on_the_companion_bar() {
        for (i, a) in COMPANION_SLOTS.iter().enumerate() {
            assert_eq!(a.pet_slot(), Some(i as u8));
            assert_eq!(a.systems(), &[System::Companion]);
        }
        assert_eq!(Ability::Primary.pet_slot(), None);
    }
}
