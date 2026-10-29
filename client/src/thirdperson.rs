//! Third person (WOMW): **V** pulls the camera back over the character and back in. Everything
//! still works from there: the crosshair aims, guns fire from the character's eyes toward what
//! the crosshair is on (crate::aim re-aims the shot), the character holds its MW2 gun (as NPC
//! gunners do, crate::npc_guns) or its own weapons with the hands up, and its body plays what the
//! first-person view does: the rifle's fire, each weapon's swing, a spell's cast, the shield's
//! block, and drawing or putting away the weapon.

use benilla_app::Objects;
use benilla_app::fusion::PlayUnitAnim;
use benilla_ui::script::UiScript;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::mode::FpsState;

/// The camera's distance behind the character in third person (yards).
pub const DISTANCE: f32 = 4.5;

#[derive(Resource, Debug, Default)]
pub struct ThirdPerson {
    pub on: bool,
    /// Frames until the camera pulls back (crate::thirdperson::toggle).
    settle: u8,
}

/// `AnimationData.dbc` ids the body plays.
pub mod anim {
    pub const ATTACK_RIFLE: u16 = 49;
    /// The rifle held up at the ready (looped by replaying it).
    pub const READY_RIFLE: u16 = 48;
    pub const SHIELD_BLOCK: u16 = 24;
    /// A spell thrown at the target. (51, played before, is `ReadySpellDirected`: the hands
    /// held up ready, no cast.)
    pub const SPELL_CAST_DIRECTED: u16 = 53;
    /// Held up ready to cast (looped by replaying it while a spell is in the right hand).
    pub const READY_SPELL_DIRECTED: u16 = 51;
    /// Ready stances with the hands up: unarmed, a one-hander, a two-hander, a polearm or staff.
    pub const READY_UNARMED: u16 = 25;
    pub const READY_1H: u16 = 26;
    pub const READY_2H: u16 = 27;
    pub const READY_2HL: u16 = 28;
    /// Drawing the weapon and putting it away (the sheath's clip: 89, the hip's 90).
    pub const SHEATH: u16 = 89;
    pub const HIP_SHEATH: u16 = 90;
    pub const ATTACK_UNARMED: u16 = 16;
    pub const ATTACK_OFF: u16 = 87;
    pub const ATTACK_OFF_PIERCE: u16 = 88;
}

/// Seconds between replays of the rifle's ready stance, and after a shot before it resumes.
const READY_REPLAY: f32 = 0.9;
const READY_AFTER_FIRE: f32 = 0.5;
/// Seconds after a swing or a cast before the melee ready stance resumes: the swing's own clip
/// plays out first.
const READY_AFTER_SWING: f32 = 0.7;

/// The stance the body holds between swings with the hands up, as the 1.12 client picks its
/// engaged idle by the main hand (`0x5fcdc0`, benilla's `creature_anim::select::ready_anim`): a
/// spell held ready, else the weapon's. Before, the body stood at ease, weapons and all, between
/// swings.
pub fn melee_ready_anim(subclass: u8, right_spell: bool) -> u16 {
    if right_spell {
        return anim::READY_SPELL_DIRECTED;
    }
    match subclass {
        0 | 4 | 7 | 11 | 13 | 14 | 15 => anim::READY_1H,
        1 | 5 | 8 | 12 => anim::READY_2H,
        6 | 10 | 17 => anim::READY_2HL,
        _ => anim::READY_UNARMED,
    }
}

/// The main hand's swing by its weapon (`ItemSubclassWeapon`), as the 1.12 client picks it.
pub fn swing_anim(subclass: u8, two_handed: bool) -> u16 {
    match (subclass, two_handed) {
        (15, _) => 85,                         // a dagger stabs
        (6 | 10 | 17 | 20, _) => 19,           // polearm, staff, spear: Attack2HL
        (1 | 5 | 8 | 12, _) | (_, true) => 18, // two-handers
        (0 | 4 | 7 | 11 | 14, _) => 17,        // one-handers
        _ => anim::ATTACK_UNARMED,
    }
}

/// The off hand's swing: a dagger stabs, anything else slashes.
pub fn off_swing_anim(subclass: u8) -> u16 {
    if subclass == 15 {
        anim::ATTACK_OFF_PIERCE
    } else {
        anim::ATTACK_OFF
    }
}

pub struct ThirdPersonPlugin;

impl Plugin for ThirdPersonPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ThirdPerson>()
            .add_systems(
                Update,
                toggle.in_set(Mw2Set::Mode).after(crate::mode::update_mode),
            )
            .add_systems(
                Update,
                (
                    carry_gun,
                    body_anims.after(crate::body_state::BodyStateSet),
                    tell_first_person,
                )
                    .in_set(Mw2Set::Hud),
            );
    }
}

/// V: in and out of third person; the camera is set once each way (crate::mode keeps first
/// person only while it is off).
fn toggle(
    input: Res<crate::input::Mw2Input>,
    state: Res<FpsState>,
    mut third: ResMut<ThirdPerson>,
    script: Option<NonSend<UiScript>>,
    mut snap: ResMut<benilla_app::fusion::LayerZoomSnap>,
) {
    // The switch is a cut both ways: the camera is at its new distance the frame it is asked,
    // the frame the hands come up or go (no glide in with the viewmodel already up).
    if snap.0 != state.active {
        snap.0 = state.active;
    }
    if !state.active {
        third.on = false;
        return;
    }
    if third.settle > 0 {
        third.settle -= 1;
        if third.settle == 0
            && third.on
            && let Some(script) = &script
        {
            crate::lua::run(script, &format!("CameraZoomOut({DISTANCE})"));
        }
    }
    if !input.view {
        return;
    }
    third.on = !third.on;
    info!(
        "benilla-mw2: {} person",
        if third.on { "third" } else { "first" }
    );
    // Into third person: pull back in two frames, once first person's zoom-in has stopped (the
    // zoom verbs of one frame sum); out: first person zooms back in itself (crate::mode).
    third.settle = if third.on { 2 } else { 0 };
}

/// The character holds the gun in hand while the gun is up in third person (none with the hands
/// up, nor in first person, where the viewmodel has it).
fn carry_gun(
    third: Res<ThirdPerson>,
    state: Res<FpsState>,
    held: Res<crate::melee::MeleeHeld>,
    form: Res<crate::forms::PlayerForm>,
    loadout: Res<crate::config::Loadout>,
    catalog: Res<crate::catalog::Catalog>,
    mut guns: ResMut<crate::npc_guns::NpcGuns>,
) {
    let gun = (third.on && state.active && !held.0 && !state.building && !form.gunless())
        .then(|| loadout.class_gun.as_ref())
        .flatten()
        .and_then(|h| catalog.gun_named(&h.base))
        .map(|g| g.id);
    let me = state.self_guid.zip(gun);
    if guns.self_gun() != me {
        guns.set_self_gun(me);
    }
}

/// The body plays what the first-person view does (the third-person camera sees it; others see
/// it through crate::body_state): the rifle's fire, each action's clip (a swing, a punch, a cast,
/// a tool), the held pose's ready stance kept by replaying it (crate::body_state::ready_clip),
/// and drawing or putting away the weapon. crate::body_pose lays the aim, the kick, the reload,
/// the throw, the stance and the arms over it.
#[allow(clippy::too_many_arguments)]
fn body_anims(
    third: Res<ThirdPerson>,
    state: Res<FpsState>,
    objects: Objects,
    weapon: Res<crate::weapon::Weapon>,
    held: Res<crate::melee::MeleeHeld>,
    own: Res<crate::body_state::OwnHands>,
    mut anims: MessageWriter<PlayUnitAnim>,
    mut seen: Local<(u64, bool)>,
    time: Res<Time>,
    mut ready_at: Local<f32>,
    mut holds: ResMut<benilla_app::fusion::LayerUnitHold>,
    attach: benilla_app::fusion::UnitAttach,
) {
    let Some(me) = objects.player() else {
        return;
    };
    let (shots, was_held) = *seen;
    *seen = (weapon.shots, held.0);
    if !state.active || !third.on {
        holds.0.remove(&me);
        return;
    }
    let mount = attach.mount_of(me);
    let mut play = |id: u16| {
        anims.write(PlayUnitAnim {
            entity: me,
            anim_id: id,
        });
        if let (Some(m), Some(side)) = (mount, crate::body_state::mount_side(id)) {
            anims.write(PlayUnitAnim {
                entity: m,
                anim_id: side,
            });
        }
    };
    let t = time.elapsed_secs();
    if held.0 != was_held {
        play(anim::SHEATH);
        // The draw or sheath plays out before a ready stance takes over.
        *ready_at = t + READY_AFTER_SWING;
    }
    let gun_up = own.pose.has(crate::body_state::POSE_GUN_UP);
    let mut acted = false;
    if weapon.shots != shots && gun_up {
        play(anim::ATTACK_RIFLE);
        *ready_at = t + READY_AFTER_FIRE;
        acted = true;
    }
    for &act in &own.acts {
        if let Some(id) = crate::body_state::act_clip(&own.pose, act) {
            play(id);
            *ready_at = t + READY_AFTER_SWING;
            acted = true;
        }
    }
    // Standing, the ready stance is held at a settled frame of the model's own clip.
    let hold = (!acted && !state.building)
        .then(|| crate::body_state::ready_hold(&own.pose, t, *ready_at))
        .flatten();
    match hold {
        Some(h) => holds.0.insert(me, h),
        None => holds.0.remove(&me),
    };
    if !acted
        && !state.building
        && t >= *ready_at
        && let Some(id) = crate::body_state::ready_clip(&own.pose)
    {
        play(id);
        *ready_at = t + READY_REPLAY;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn each_weapon_swings_its_own_way() {
        assert_eq!(swing_anim(7, false), 17); // a sword
        assert_eq!(swing_anim(8, true), 18); // a two-handed sword
        assert_eq!(swing_anim(10, true), 19); // a staff
        assert_eq!(swing_anim(15, false), 85); // a dagger
        assert_eq!(swing_anim(255, false), anim::ATTACK_UNARMED); // fists
        assert_eq!(off_swing_anim(15), anim::ATTACK_OFF_PIERCE);
        assert_eq!(off_swing_anim(7), anim::ATTACK_OFF);
    }

    /// `AnimationData.dbc` (5875): 25 ReadyUnarmed, 26 Ready1H, 27 Ready2H, 28 Ready2HL,
    /// 51 ReadySpellDirected, 53 SpellCastDirected.
    #[test]
    fn the_hands_up_hold_the_weapons_ready_stance_and_a_cast_casts() {
        assert_eq!(anim::SPELL_CAST_DIRECTED, 53);
        assert_eq!(melee_ready_anim(7, false), 26); // a sword
        assert_eq!(melee_ready_anim(15, false), 26); // a dagger
        assert_eq!(melee_ready_anim(13, false), 26); // a fist weapon
        assert_eq!(melee_ready_anim(8, false), 27); // a two-handed sword
        assert_eq!(melee_ready_anim(10, false), 28); // a staff
        assert_eq!(melee_ready_anim(255, false), 25); // fists
        assert_eq!(melee_ready_anim(7, true), 51); // a spell in the right hand
    }
}

/// Tell benilla when the view is first person (the layer on, third person off), so it skips
/// effects hung on our own body.
fn tell_first_person(
    state: Res<FpsState>,
    third: Res<ThirdPerson>,
    mut fp: ResMut<benilla_app::fusion::LayerFirstPerson>,
    mut quiet: ResMut<benilla_app::fusion::LayerQuietSpellbook>,
) {
    let want = state.active && !third.on;
    if fp.0 != want {
        fp.0 = want;
    }
    // The kit system owns the spellbook while the layer is on: no learn/unlearn chat lines.
    if quiet.0 != state.active {
        quiet.0 = state.active;
    }
}
