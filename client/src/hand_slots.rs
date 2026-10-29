//! What the hands hold, shown on the action bar (WOMW), in every action system: the one place
//! that says which slots hold something a hand can take, and which hand has it now.
//!
//! The look (classic WoW's own: the stock button's `Border` glow, as an equipped item wears in
//! green, tinted; benilla's crate::fusion_bar draws it):
//!
//! - **Equippable, not in a hand:** a faint grey glow. A spell that readies into a hand, a gun,
//!   a grenade, Utility's Hands, a tool, an instrument.
//! - **In the main hand** (the right hand, the left mouse button's): a gold glow and a gold "R"
//!   at the button's foot.
//! - **In the off hand** (the left hand, the right mouse button's; Alt with the key): a pale blue
//!   glow and a blue "L".
//! - **In both hands** (the same spell in each, a dual cast; raised fists; a two-hander): the
//!   gold glow and "R L".
//! - **Not equippable** (an instant: a shout, a strike, a killstreak, the rearm): stock, no glow.
//!
//! The tooltip gains a line: "Equipped: Main Hand" (Off Hand, Both Hands), or for an equippable
//! slot not in hand how to equip it.
//!
//! A new action system (Companion) adds its abilities to [`ability_mark`]; nothing else changes.

use std::collections::HashMap;

use benilla_app::fusion::{FusionActionBar, SlotMark};
use bevy::prelude::*;

use crate::action_systems::{Ability, ActionSystems, System};
use crate::hands::{Hand, Hands, Readied};

/// Which hand holds a slot's thing, if any.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Held {
    /// Equippable, in no hand now.
    No,
    Main,
    Off,
    Both,
}

/// Colours: WoW's gold (`NORMAL_FONT_COLOR`), a pale blue for the off hand, a dim grey.
pub const GOLD: [f32; 3] = [1.0, 0.82, 0.0];
pub const OFF_BLUE: [f32; 3] = [0.55, 0.75, 1.0];
pub const DIM: [f32; 3] = [0.6, 0.6, 0.6];

/// The mark a slot wears for what it holds.
pub fn mark(held: Held) -> SlotMark {
    let gold = "|cffffd100R|r";
    let blue = "|cff8cbfffL|r";
    match held {
        Held::No => SlotMark {
            border: [DIM[0], DIM[1], DIM[2], 0.45],
            label: String::new(),
            tip: Some((
                "Equippable: its key or a click for the main hand, Alt or a right click for the \
                 off hand"
                    .into(),
                DIM,
            )),
        },
        Held::Main => SlotMark {
            border: [GOLD[0], GOLD[1], GOLD[2], 1.0],
            label: gold.into(),
            tip: Some(("Equipped: Main Hand".into(), GOLD)),
        },
        Held::Off => SlotMark {
            border: [OFF_BLUE[0], OFF_BLUE[1], OFF_BLUE[2], 1.0],
            label: blue.into(),
            tip: Some(("Equipped: Off Hand".into(), OFF_BLUE)),
        },
        Held::Both => SlotMark {
            border: [GOLD[0], GOLD[1], GOLD[2], 1.0],
            label: format!("{gold} {blue}"),
            tip: Some(("Equipped: Both Hands".into(), GOLD)),
        },
    }
}

/// From which hands hold a thing.
fn held_by(main: bool, off: bool) -> Held {
    match (main, off) {
        (true, true) => Held::Both,
        (true, false) => Held::Main,
        (false, true) => Held::Off,
        (false, false) => Held::No,
    }
}

/// What the layer's own abilities hold now, `None` for one that is not equippable. `gun_hand` is
/// the gun in hand (crate::cac::Hand: 0 primary, 1 secondary, 2 underbarrel).
pub fn ability_mark(
    a: Ability,
    on: System,
    systems: &ActionSystems,
    gun_hand: u8,
    two_handed_tool: bool,
) -> Option<Held> {
    let active = systems.active == on;
    let grenade = |which: Ability| match systems.grenade {
        Some((g, side)) if g == which && active => held_by(side == 0, side == 1),
        _ => Held::No,
    };
    let gun = |n: u8| {
        // A grenade in the main hand takes the gun's place there.
        let main_busy = matches!(systems.grenade, Some((_, 0)));
        held_by(active && gun_hand == n && !main_busy, false)
    };
    Some(match a {
        Ability::Primary => gun(0),
        Ability::Secondary => gun(1),
        Ability::Underbarrel => gun(2),
        Ability::Lethal | Ability::Tactical => grenade(a),
        // Utility's Hands: raised fists in both. On Classic's bar it only puts the weapons away.
        Ability::BareHands if on == System::Utility => {
            let up = active && systems.tool.is_none() && systems.fists_up;
            held_by(up, up)
        }
        a if a.is_tool() => {
            let out = active && systems.tool == Some(a);
            held_by(out, out && (two_handed_tool || a.is_instrument()))
        }
        _ => return None,
    })
}

/// What a Classic slot's spell holds now, `None` for one that never goes into a hand.
pub fn spell_mark(action: u32, spell_id: u32, kind: Readied, hands: &Hands, armed: bool) -> Option<Held> {
    if spell_id == crate::hands::benilla_app_attack() {
        // Attack readies the weapon itself.
        let main = armed && hands.right == Hand::Weapon;
        let off = armed && (hands.left == Hand::Weapon);
        return Some(held_by(main, off));
    }
    if kind != Readied::InHand {
        return None;
    }
    let holds = |side: usize| armed && hands.spell_in(side).is_some_and(|(a, _)| a == action);
    Some(held_by(holds(0), holds(1)))
}

/// Every slot's mark, by Lua action id.
#[allow(clippy::too_many_arguments)]
pub fn marks(
    bar: &FusionActionBar,
    systems: &ActionSystems,
    hands: &Hands,
    gun_hand: u8,
    kind_of: impl Fn(u32) -> Readied,
) -> HashMap<u32, SlotMark> {
    let mut out = HashMap::new();
    for (&action, &id) in &bar.fusion_slots {
        let (Some(a), Some(on)) = (Ability::from_id(id), System::of_action(action)) else {
            continue;
        };
        if let Some(h) = ability_mark(a, on, systems, gun_hand, a == Ability::FishingPole) {
            out.insert(action, mark(h));
        }
    }
    let armed = systems.active == System::Classic;
    for (&action, &spell) in &bar.spells {
        if System::of_action(action) != Some(System::Classic) {
            continue;
        }
        if let Some(h) = spell_mark(action, spell, kind_of(spell), hands, armed) {
            out.insert(action, mark(h));
        }
    }
    out
}

pub struct HandSlotsPlugin;

impl Plugin for HandSlotsPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(
            Update,
            show_marks.in_set(crate::Mw2Set::Hud),
        );
    }
}

#[allow(clippy::too_many_arguments)]
fn show_marks(
    systems: Res<ActionSystems>,
    hands: Res<Hands>,
    gun_hand: Res<crate::cac::Hand>,
    table: Res<crate::hands::SpellTable>,
    class_page: Res<crate::classes::ClassPage>,
    hand_spells: Res<crate::spellcast::HandSpells>,
    state: Res<crate::mode::FpsState>,
    bar: Option<ResMut<FusionActionBar>>,
) {
    let Some(mut bar) = bar else { return };
    let next = if state.active {
        marks(&bar, &systems, &hands, gun_hand.0, |spell| {
            crate::hands::readied_kind(spell, &table, &class_page, &hand_spells)
        })
    } else {
        HashMap::new()
    };
    if bar.marks != next {
        bar.marks = next;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn main_off_and_both_hands_read_apart() {
        let (m, o, b, n) = (
            mark(Held::Main),
            mark(Held::Off),
            mark(Held::Both),
            mark(Held::No),
        );
        assert_ne!(m.border, o.border, "main and off hand differ in colour");
        assert!(m.label.contains('R') && !m.label.contains('L'));
        assert!(o.label.contains('L') && !o.label.contains('R'));
        assert!(b.label.contains('R') && b.label.contains('L'));
        assert!(n.label.is_empty() && n.border[3] < m.border[3], "unequipped is dim");
        assert_eq!(m.tip.unwrap().0, "Equipped: Main Hand");
        assert_eq!(o.tip.unwrap().0, "Equipped: Off Hand");
    }

    #[test]
    fn every_system_marks_its_equippables_and_not_its_instants() {
        let mut s = ActionSystems::default();
        // Modern: the gun in hand, a grenade readied in the off hand, a killstreak is no hand's.
        s.grenade = Some((Ability::Lethal, 1));
        assert_eq!(ability_mark(Ability::Primary, System::Modern, &s, 0, false), Some(Held::Main));
        assert_eq!(ability_mark(Ability::Secondary, System::Modern, &s, 0, false), Some(Held::No));
        assert_eq!(ability_mark(Ability::Lethal, System::Modern, &s, 0, false), Some(Held::Off));
        assert_eq!(ability_mark(Ability::Tactical, System::Modern, &s, 0, false), Some(Held::No));
        assert_eq!(ability_mark(Ability::Killstreak, System::Modern, &s, 0, false), None);
        assert_eq!(ability_mark(Ability::Rearm, System::Modern, &s, 0, false), None);
        // In the main hand, the grenade takes the gun's place.
        s.grenade = Some((Ability::Tactical, 0));
        assert_eq!(ability_mark(Ability::Primary, System::Modern, &s, 0, false), Some(Held::No));
        assert_eq!(ability_mark(Ability::Tactical, System::Modern, &s, 0, false), Some(Held::Main));
        // Utility: Hands relaxed is equippable, raised is both hands; a tool in hand.
        s.sheathe();
        assert_eq!(ability_mark(Ability::BareHands, System::Utility, &s, 0, false), Some(Held::No));
        s.fists_up = true;
        assert_eq!(ability_mark(Ability::BareHands, System::Utility, &s, 0, false), Some(Held::Both));
        s.fists_up = false;
        s.tool = Some(Ability::MiningPick);
        assert_eq!(ability_mark(Ability::MiningPick, System::Utility, &s, 0, false), Some(Held::Main));
        assert_eq!(ability_mark(Ability::BareHands, System::Utility, &s, 0, false), Some(Held::No));
        assert_eq!(ability_mark(Ability::BuildStructure, System::Utility, &s, 0, false), None);
        // Classic's Hands only sheathes: an instant.
        assert_eq!(ability_mark(Ability::BareHands, System::Classic, &s, 0, false), None);
    }

    #[test]
    fn a_spell_shows_the_hand_it_is_in() {
        let mut h = Hands::default();
        h.right = Hand::Spell { action: 3, spell_id: 133 };
        h.left = Hand::Spell { action: 4, spell_id: 116 };
        assert_eq!(spell_mark(3, 133, Readied::InHand, &h, true), Some(Held::Main));
        assert_eq!(spell_mark(4, 116, Readied::InHand, &h, true), Some(Held::Off));
        assert_eq!(spell_mark(5, 2136, Readied::InHand, &h, true), Some(Held::No));
        assert_eq!(spell_mark(6, 6673, Readied::CastNow, &h, true), None, "a shout");
        assert_eq!(spell_mark(7, 78, Readied::Technique, &h, true), None);
        h.left = Hand::Spell { action: 3, spell_id: 133 };
        assert_eq!(spell_mark(3, 133, Readied::InHand, &h, true), Some(Held::Both), "dual cast");
        // Outside Classic nothing is in hand.
        assert_eq!(spell_mark(3, 133, Readied::InHand, &h, false), Some(Held::No));
    }
}
