//! Melee with the weapon in hand: the server names the character's main-hand weapon (the world
//! channel, op 20) with its swing and lunge times, the two-hand way — daggers quick and light,
//! swords then axes then maces slower and heavier, two-handers slowest and hardest — and the knife
//! key swings it at that pace. The viewmodel draws that weapon in the hand for the swing
//! (crate::mw2) instead of MW2's knife.

use bevy::prelude::*;

use benilla_app::fusion::{CHANNEL_WORLD, FusionChannelReceived};

use crate::Mw2Set;

pub const OP_MELEE: u8 = 20;

/// The weapon in hand for melee, as the server sent it.
#[derive(Resource, Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MeleeWeapon {
    /// WoW's weapon subclass (`ItemSubclassWeapon`), 255 for none.
    pub subclass: u8,
    pub two_handed: bool,
    pub swing_ms: u16,
    pub lunge_ms: u16,
    pub item: u32,
    /// Its `ItemDisplayInfo` row, for the model in hand (0: none, the knife).
    pub display: u32,
    /// What the off hand holds (the left hand: a second weapon, a shield, a book or an orb).
    pub off: OffHand,
    /// The off-hand weapon's subclass, 255 for none.
    pub off_subclass: u8,
    /// The off hand's `ItemDisplayInfo` row, 0 for none.
    pub off_display: u32,
}

/// The off hand's contents (the server's `Melee::OffHandKind`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum OffHand {
    #[default]
    Empty,
    Weapon,
    Shield,
    Held,
}

pub fn decode(b: &[u8]) -> Option<MeleeWeapon> {
    let u32_at = |i: usize| u32::from_le_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]]);
    // 21 bytes with the off hand; 15 from a server before it.
    let full = b.len() == 21;
    ((b.len() == 15 || full) && b[0] == OP_MELEE).then(|| MeleeWeapon {
        subclass: b[1],
        two_handed: b[2] != 0,
        swing_ms: u16::from_le_bytes([b[3], b[4]]),
        lunge_ms: u16::from_le_bytes([b[5], b[6]]),
        item: u32_at(7),
        display: u32_at(11),
        off: match full.then(|| b[15]) {
            Some(1) => OffHand::Weapon,
            Some(2) => OffHand::Shield,
            Some(3) => OffHand::Held,
            _ => OffHand::Empty,
        },
        off_subclass: if full { b[16] } else { 255 },
        off_display: if full { u32_at(17) } else { 0 },
    })
}

/// What to call the weapon class on the HUD (`ItemSubclassWeapon`).
pub fn kind_name(subclass: u8, two_handed: bool) -> &'static str {
    match (subclass, two_handed) {
        (15, _) => "Dagger",
        (13, _) => "Fist weapon",
        (7, false) => "Sword",
        (8, _) | (7, true) => "Two-handed sword",
        (0, false) => "Axe",
        (1, _) | (0, true) => "Two-handed axe",
        (4, false) => "Mace",
        (5, _) | (4, true) => "Two-handed mace",
        (6, _) => "Polearm",
        (10, _) => "Staff",
        _ => "Knife",
    }
}

/// The hands are up instead of the gun: in the Classic and Utility action systems
/// (crate::action_systems, X and Z). While they are up the trigger swings what they hold (bare
/// fists in Utility, as the server's profile then says), and the gun neither fires nor aims.
#[derive(Resource, Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MeleeHeld(pub bool);

/// Whether the hands are up in `system`.
pub fn melee_held(system: crate::action_systems::System) -> bool {
    system != crate::action_systems::System::Modern
}

/// Whether a swing lunges at the enemy in front. A swing is a swing: only a grant lunges (the
/// server's `Fusion::SwingLunges`, a warrior's Charge perk), which the server says by sending a
/// lunge time (`lunge_ms` > 0; 0 or no profile yet: no lunge). Even then it is MW2's standing move:
/// crouched, prone or sliding, the swing stays a slash.
pub fn may_lunge(stance: benilla_app::fusion::Stance, in_range: bool, granted: bool) -> bool {
    granted && in_range && stance == benilla_app::fusion::Stance::Stand
}

pub struct MeleePlugin;

impl Plugin for MeleePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<MeleeWeapon>()
            .init_resource::<MeleeHeld>()
            .add_systems(
                Update,
                (read_profile, hold_melee)
                    .in_set(Mw2Set::Mode)
                    .after(crate::action_systems::ActionSystemsSet),
            );
    }
}

fn hold_melee(
    state: Res<crate::mode::FpsState>,
    systems: Res<crate::action_systems::ActionSystems>,
    form: Res<crate::forms::PlayerForm>,
    fusion: Res<crate::fusion::Fusion>,
    mut held: ResMut<MeleeHeld>,
    no_swing: Option<ResMut<benilla_app::fusion_bar::FusionNoSpellSwing>>,
) {
    // A form's limbs hold no weapon (crate::forms): the hands stay down.
    let next = state.active && !form.handless() && melee_held(systems.active);
    if held.0 != next {
        held.0 = next;
    }
    // The hands swing through the server (crate::weapon's MeleeSwing): a strike or a technique
    // pressed never starts WoW's auto-attack beside them. Attack (6603) itself still does.
    if let Some(mut n) = no_swing {
        let want = suppress_spell_swing(held.0, fusion.enabled);
        if n.0 != want {
            n.0 = want;
        }
    }
}

/// Whether a spell may not start WoW's auto-attack: the hands up with an Azeroth Warfare server.
pub fn suppress_spell_swing(hands_up: bool, fusion: bool) -> bool {
    hands_up && fusion
}

fn read_profile(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut melee: ResMut<MeleeWeapon>,
    mut weapon: ResMut<crate::weapon::Weapon>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        if let Some(p) = decode(&m.body) {
            info!(
                "benilla-mw2: melee with {} (item {}, swing {} ms)",
                kind_name(p.subclass, p.two_handed),
                p.item,
                p.swing_ms
            );
            *melee = p;
            let gate = &mut weapon.knife_gate;
            gate.slash_secs = (p.swing_ms > 0).then(|| f32::from(p.swing_ms) / 1000.0);
            gate.lunge_secs = (p.lunge_ms > 0).then(|| f32::from(p.lunge_ms) / 1000.0);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_hands_are_up_in_classic_and_utility() {
        use crate::action_systems::System;
        assert!(!melee_held(System::Modern));
        assert!(melee_held(System::Classic));
        assert!(melee_held(System::Utility));
    }

    #[test]
    fn spells_never_start_wows_swing_while_the_hands_swing() {
        assert!(suppress_spell_swing(true, true));
        assert!(!suppress_spell_swing(false, true), "Modern: the gun, WoW's rules");
        assert!(!suppress_spell_swing(true, false), "no fusion: WoW's swing is the swing");
    }

    #[test]
    fn only_a_standing_player_lunges() {
        use benilla_app::fusion::Stance;
        assert!(may_lunge(Stance::Stand, true, true));
        assert!(!may_lunge(Stance::Crouch, true, true));
        assert!(!may_lunge(Stance::Prone, true, true));
        assert!(!may_lunge(Stance::Slide, true, true));
        assert!(!may_lunge(Stance::Stand, false, true));
    }

    #[test]
    fn a_swing_never_lunges_without_the_servers_grant() {
        use benilla_app::fusion::Stance;
        // The lunge bug: every standing swing at an enemy within 20 yards dashed at it. Without a
        // grant (the server's lunge time 0, or no profile yet) a swing is a swing.
        assert!(!may_lunge(Stance::Stand, true, false));
        let mut b = vec![OP_MELEE, 15, 0];
        b.extend_from_slice(&450u16.to_le_bytes());
        b.extend_from_slice(&0u16.to_le_bytes()); // lunge_ms 0: not granted
        b.extend_from_slice(&1234u32.to_le_bytes());
        b.extend_from_slice(&5678u32.to_le_bytes());
        let p = decode(&b).expect("profile");
        assert_eq!(p.lunge_ms, 0);
        let mut gate = crate::weapon::KnifeGate::default();
        gate.lunge_secs = (p.lunge_ms > 0).then(|| f32::from(p.lunge_ms) / 1000.0);
        assert!(!may_lunge(Stance::Stand, true, gate.lunge_secs.is_some()));
        // And before any profile came: none either.
        assert!(crate::weapon::KnifeGate::default().lunge_secs.is_none());
    }

    #[test]
    fn the_profile_decodes_as_the_server_writes_it() {
        let mut b = vec![OP_MELEE, 4, 0];
        b.extend_from_slice(&800u16.to_le_bytes());
        b.extend_from_slice(&1120u16.to_le_bytes());
        b.extend_from_slice(&12345u32.to_le_bytes());
        b.extend_from_slice(&20190u32.to_le_bytes());
        let p = decode(&b).unwrap();
        assert_eq!(
            (p.subclass, p.two_handed, p.swing_ms, p.lunge_ms),
            (4, false, 800, 1120)
        );
        assert_eq!((p.item, p.display), (12345, 20190));
        assert_eq!(kind_name(p.subclass, p.two_handed), "Mace");
        assert!(decode(&b[..14]).is_none());
        assert_eq!(p.off, OffHand::Empty); // the 15-byte form: nothing in the off hand
        b.extend_from_slice(&[2, 255]);
        b.extend_from_slice(&0x1122_3344u32.to_le_bytes());
        let p = decode(&b).unwrap();
        assert_eq!((p.off, p.off_display), (OffHand::Shield, 0x1122_3344));
    }
}
