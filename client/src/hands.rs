//! The hands, the two-hand way (WOMW): in the Classic action system (crate::action_systems, X),
//! each hand holds a weapon, a spell or a shield, and the mouse uses them: the left button the
//! right hand, the right button the left. A one-hander sits in the right fist, a two-hander in
//! both, a shield or a second weapon in the left. In Utility (Z) both hands are bare fists.
//!
//! WoW's own action bar picks what a hand holds (crate::fusion_bar in the fork): Classic's bar
//! is the character's spells and abilities, and pressing one readies it rather than casting it:
//!
//! - a spell with a reach (Fireball, Flash Heal) goes into a hand: its slot key alone the main
//!   (right) hand's, with Alt the off (left) hand's, a click the hand of the mouse button it was
//!   clicked with; that button charges it and casts it at the crosshair when let go
//!   (crate::spellcast);
//! - a melee ability (Heroic Strike, Sinister Strike: "combat range") is a technique for the next
//!   swing of the weapon in hand;
//! - a spell on oneself (a shout, a stance, a buff) simply casts, as it always did.

use std::sync::Arc;

use benilla_app::fusion::FusionActionBar;
use benilla_assets::{LockRecover, WorldAssets};
use benilla_formats::SpellCatalog;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::aim::Aim;
use crate::input::Mw2Input;
use crate::melee::{MeleeHeld, MeleeWeapon, OffHand};
use crate::mode::FpsState;

/// What one hand holds.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Hand {
    /// The equipped weapon (or the fist).
    #[default]
    Weapon,
    /// A readied spell: its Lua action id and spell id.
    Spell {
        action: u32,
        spell_id: u32,
    },
    Shield,
    /// A held off-hand item (a book, an orb) or nothing: free for a spell.
    Empty,
}

/// `SpellRange.dbc`: 1 is self only, 2 is combat range (a melee ability).
const RANGE_SELF: u32 = 1;
#[cfg(test)]
const RANGE_COMBAT: u32 = 2;

/// What a pressed ability does. Only spells take a hand; a move (a charge, a strike, a hook)
/// just goes off when its button or key is pressed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Readied {
    /// Cast at once on yourself (a buff, a shout, a stance).
    CastNow,
    /// A technique for the next swing (Heroic Strike, Cleave: the "on next swing" ones).
    Technique,
    /// A move: goes off at once at the unit under the crosshair, the hands as they are.
    Move,
    /// A spell, held in a hand until the button casts it.
    InHand,
}

/// `Spell.dbc` attributes: the "on next swing" bits.
const ATTR_ON_NEXT_SWING: u32 = 0x4 | 0x400;
/// `SpellSchools`: 0 is physical (normal), the rest magic.
const SCHOOL_PHYSICAL: u32 = 0;

/// What pressing an ability does, from its range, school and attributes.
pub fn readied_as(range_index: u32, school: u32, attributes: u32) -> Readied {
    if range_index == RANGE_SELF {
        Readied::CastNow
    } else if attributes & ATTR_ON_NEXT_SWING != 0 {
        Readied::Technique
    } else if school == SCHOOL_PHYSICAL {
        Readied::Move
    } else {
        Readied::InHand
    }
}

/// What pressing a spell does, from the spell table, the server's refit of it (crate::classes)
/// and the server's hand spells (crate::spellcast): the action bar's marks read it too
/// (crate::hand_slots), so a spell that goes into a hand is shown as one that does.
pub fn readied_kind(
    spell_id: u32,
    table: &SpellTable,
    class_page: &crate::classes::ClassPage,
    hand_spells: &crate::spellcast::HandSpells,
) -> Readied {
    // The server's hand spells first: an area spell cast on oneself before (Frost Nova) is aimed
    // from a hand now.
    if hand_spells.0.contains_key(&spell_id) {
        return Readied::InHand;
    }
    let spell = table.0.as_ref().and_then(|c| c.get(spell_id));
    // The server's refit first (crate::classes): a class option's donor may be a self cast.
    let range = class_page
        .ranges
        .get(&spell_id)
        .copied()
        .unwrap_or_else(|| spell.map_or(0, |s| s.range_index));
    let school = spell.map_or(1, |s| s.school);
    let attributes = class_page
        .attributes
        .get(&spell_id)
        .copied()
        .unwrap_or_else(|| spell.map_or(0, |s| s.attributes));
    readied_as(range, school, attributes)
}

/// The hands' contents and what they last did, for the viewmodel (crate::mw2::viewmodel).
#[derive(Resource, Debug, Default)]
pub struct Hands {
    pub right: Hand,
    pub left: Hand,
    /// A melee ability for the next swing: its Lua action id and spell id.
    pub technique: Option<(u32, u32)>,
    /// When each hand (0 right, 1 left) last cast, for the thrust of the cast.
    pub cast_at: [Option<f32>; 2],
    /// The school of the spell in each hand (`Spell.dbc` School), for its glow.
    pub school: [u32; 2],
    /// The weapon takes both hands.
    pub two_handed: bool,
    /// When each hand's button went down on a spell the server charges (crate::spellcast), while
    /// it stays down.
    pub charging: [Option<f32>; 2],
    /// That spell's charge, in seconds.
    pub charge_secs: [f32; 2],
    /// When each hand's current (or last) charge began, kept while a release waits for its pair
    /// (crate::cast_bar).
    pub charge_from: [f32; 2],
    /// A hand let go fully charged, waiting a moment for the other to pair with it.
    pub pending: [Option<f32>; 2],
    /// ... and whether it was held to a full charge (crate::spellcast::FULL_CHARGE_FACTOR).
    pub full: [bool; 2],
    /// When each hand last cast from a charge (its flash) or fizzled.
    pub released_at: [Option<f32>; 2],
    pub fizzled_at: [Option<f32>; 2],
    /// When each hand began pouring a spray (crate::spellcast, Scorch, Cone of Cold), while its
    /// button stays down; the spell it pours and when its aim last went to the server.
    pub spraying: [Option<f32>; 2],
    pub spray_spell: [u32; 2],
    pub spray_sent: [f32; 2],
    /// Utility's hands at rest (crate::action_systems `fists_up` off, no tool): lowered, open,
    /// out of a fight; the trigger does not punch until Hands is equipped (fists up).
    pub relaxed: bool,
}

impl Hands {
    /// The spell in a hand (0 the left button's, 1 the right's): its Lua action and spell id.
    pub fn spell_in(&self, side: usize) -> Option<(u32, u32)> {
        match if side == 0 { self.right } else { self.left } {
            Hand::Spell { action, spell_id } => Some((action, spell_id)),
            _ => None,
        }
    }
    /// The right hand swings its weapon (the left button's melee).
    pub fn right_swings(&self) -> bool {
        self.right == Hand::Weapon && !self.relaxed
    }
    /// Both hands on the two-hander's hilt: a two-hander held, neither hand holding a spell (a
    /// spell readied in either hand puts the two-hander away, the two-hand way).
    pub fn grips_two_handed(&self) -> bool {
        self.two_handed && self.right == Hand::Weapon && self.left == Hand::Weapon
    }
    /// The left hand swings a second weapon (not a two-hander's second grip).
    pub fn left_swings(&self) -> bool {
        self.left == Hand::Weapon && !self.two_handed && !self.relaxed
    }
    /// The left hand holds a shield, and the right button raises it.
    pub fn left_shield(&self) -> bool {
        self.left == Hand::Shield
    }
}

/// Readies a spell into an explicit hand: `side` 0 is the left mouse button's hand (the right
/// fist), 1 the right button's (the left fist, the off hand). The action bar's clicks and keys go
/// through here (a right click, or Alt with the slot key, picks side 1); another binding sends
/// [`ReadyInHand`] with the side it wants, and the same button then charges and
/// casts it (crate::spellcast's release takes the hand explicitly: `hand_aim(aim, spell, hand, ..)`
/// with `HAND_LEFT_BUTTON`, `HAND_RIGHT_BUTTON` or `HAND_BOTH`).
pub fn put_in_hand(hands: &mut Hands, side: usize, action: u32, spell_id: u32, school: u32) {
    let side = side.min(1);
    let hand = Hand::Spell { action, spell_id };
    hands.school[side] = school;
    hands.charging[side] = None;
    hands.pending[side] = None;
    if side == 1 {
        hands.left = hand;
    } else {
        hands.right = hand;
    }
    // The rule: a spell in either hand puts a two-hander away, and the other hand is its own,
    // free (never left gripping a hilt that is no longer there, beside the spell hand).
    if hands.two_handed {
        let other = if side == 1 {
            &mut hands.right
        } else {
            &mut hands.left
        };
        if *other == Hand::Weapon {
            *other = Hand::Empty;
        }
    }
}

/// Ready a spell into this hand (`side` as [`put_in_hand`]), whatever key or click asked: the
/// off-hand binding's way in. A spell that is not held in a hand (a self cast, a move) is not
/// readied by this; the bar's own press does those.
#[derive(Message, Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReadyInHand {
    pub side: usize,
    pub action: u32,
    pub spell_id: u32,
}

/// What the off hand holds by default, from the server's melee profile: a shield or a second
/// weapon as equipped; nothing (free for a spell) otherwise, or under a two-hander.
pub fn default_left(melee: &MeleeWeapon) -> Hand {
    if melee.two_handed {
        return Hand::Weapon;
    }
    match melee.off {
        OffHand::Shield => Hand::Shield,
        OffHand::Weapon => Hand::Weapon,
        OffHand::Held | OffHand::Empty => Hand::Empty,
    }
}

/// The spell table, loaded once from the WoW install (`Spell.dbc`); the kit editor's talent page
/// reads its names and icons too.
#[derive(Resource, Default)]
pub struct SpellTable(pub Option<Arc<SpellCatalog>>);

pub struct HandsPlugin;

impl Plugin for HandsPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins(crate::hand_slots::HandSlotsPlugin)
            .init_resource::<Hands>()
            .init_resource::<SpellTable>()
            .init_resource::<crate::spellcast::HandSpells>()
            .add_message::<crate::spellcast::HandReleased>()
            .add_message::<ReadyInHand>()
            .add_systems(
                Update,
                (load_spells, ready, use_hands)
                    .chain()
                    // Before the weapon reads the keys these stand for (crate::weapon).
                    .in_set(Mw2Set::Aim),
            );
    }
}

fn load_spells(
    mut table: ResMut<SpellTable>,
    world: Option<Res<WorldAssets>>,
    mut tried: Local<bool>,
) {
    if *tried || table.0.is_some() {
        return;
    }
    let Some(world) = world else {
        return;
    };
    *tried = true;
    let mut chain = world.chain.lock_recover();
    match benilla_formats::load_spell_catalog(&mut chain) {
        Ok(c) => table.0 = Some(Arc::new(c)),
        Err(e) => warn!("benilla-mw2: no spell table for the hands: {e:#}"),
    }
}

/// The readied spells into the hands.
#[allow(clippy::too_many_arguments)]
fn ready(
    time: Res<Time>,
    held: Res<MeleeHeld>,
    mut hands: ResMut<Hands>,
    melee: Res<MeleeWeapon>,
    table: Res<SpellTable>,
    class_page: Res<crate::classes::ClassPage>,
    aim: Res<Aim>,
    hand_spells: Res<crate::spellcast::HandSpells>,
    bar: Option<ResMut<FusionActionBar>>,
    systems: Res<crate::action_systems::ActionSystems>,
    mut was_held: Local<(bool, crate::action_systems::System)>,
    mut explicit: MessageReader<ReadyInHand>,
) {
    // The hands go up as the equipment has them (bare in Utility, as the server's profile then
    // says): the weapon in the right, a shield or a second weapon in the left, no technique.
    if held.0 && (!was_held.0 || was_held.1 != systems.active) {
        hands.right = Hand::Weapon;
        hands.left = if systems.active == crate::action_systems::System::Utility {
            Hand::Empty
        } else {
            default_left(&melee)
        };
        hands.technique = None;
    }
    *was_held = (held.0, systems.active);
    hands.two_handed = melee.two_handed;
    // Utility's hands hang relaxed until Hands is equipped (crate::action_systems `fists_up`).
    hands.relaxed = held.0
        && systems.active == crate::action_systems::System::Utility
        && systems.tool.is_none()
        && !systems.fists_up;
    // `MW2_PROBE_HANDS=<school>,<school>`: a spell of each school in the right and left hands,
    // to see them (live tests; a character without spells).
    if held.0
        && let Ok(v) = std::env::var("MW2_PROBE_HANDS")
    {
        let schools: Vec<u32> = v.split(',').filter_map(|s| s.trim().parse().ok()).collect();
        for (side, &school) in schools.iter().take(2).enumerate() {
            let spell = Hand::Spell {
                action: 0,
                spell_id: 0,
            };
            hands.school[side] = school;
            if side == 0 {
                hands.right = spell;
            } else {
                hands.left = spell;
            }
        }
    }
    // A hand named by the caller (the off-hand binding): straight into it.
    for r in explicit.read() {
        let school = table
            .0
            .as_ref()
            .and_then(|c| c.get(r.spell_id))
            .map_or(0, |s| s.school);
        put_in_hand(&mut *hands, r.side, r.action, r.spell_id, school);
        info!(
            "benilla-mw2: hand {} readies spell {} (asked for that hand)",
            r.side.min(1),
            r.spell_id
        );
    }
    let Some(mut bar) = bar else {
        return;
    };
    let t = time.elapsed_secs();
    for r in std::mem::take(&mut bar.readied) {
        // The off (left) hand: a right click, or Alt with the slot key (crate::action_systems).
        let left = r.right_button;
        let spell = table.0.as_ref().and_then(|c| c.get(r.spell_id));
        let kind = readied_kind(r.spell_id, &table, &class_page, &hand_spells);
        match kind {
            // A move or a self cast goes off at once, the hands as they are, at the crosshair's
            // unit: a class option cast on oneself aims by the selection too (Shadowstep and
            // Blade Blink at an enemy, Holy Shield and Pain Suppression on an ally, Earthquake
            // and Meteor at a spot).
            Readied::CastNow | Readied::Move => {
                if let Some(a) = aim.target.as_ref().filter(|a| !a.service) {
                    bar.select = Some((a.entity, a.guid));
                }
                bar.use_now.push(r.action);
            }
            // The Attack slot (6603) readies the weapon itself.
            _ if r.spell_id == benilla_app_attack() => {
                if melee.two_handed {
                    // The two-hander back in both hands.
                    hands.right = Hand::Weapon;
                    hands.left = Hand::Weapon;
                } else if left {
                    hands.left = default_left(&melee);
                } else {
                    hands.right = Hand::Weapon;
                }
            }
            Readied::Technique if hands.right == Hand::Weapon && !left => {
                hands.technique = Some((r.action, r.spell_id));
                info!("benilla-mw2: technique readied: spell {}", r.spell_id);
            }
            // A next-swing technique with no weapon in hand goes off at once, as a move does.
            Readied::Technique => {
                if let Some(a) = aim.target.as_ref().filter(|a| !a.service) {
                    bar.select = Some((a.entity, a.guid));
                }
                bar.use_now.push(r.action);
            }
            _ => {
                put_in_hand(
                    &mut *hands,
                    usize::from(left),
                    r.action,
                    r.spell_id,
                    spell.map_or(0, |s| s.school),
                );
                info!(
                    "benilla-mw2: {} hand readies spell {} at {t:.1}",
                    if left { "left" } else { "right" },
                    r.spell_id
                );
            }
        }
    }
}

/// `Attack`, the auto-attack pseudo-spell: on the bar it readies the weapon.
pub const fn benilla_app_attack() -> u32 {
    6603
}

/// The buttons use the hands: a spell the server charges (crate::spellcast) charges while its
/// button is down and casts at the crosshair when it comes up fully charged (early, it fizzles);
/// another spell casts at the crosshair's unit on the press; the weapon's swing (crate::weapon)
/// carries a readied technique.
#[allow(clippy::too_many_arguments)]
fn use_hands(
    time: Res<Time>,
    state: Res<FpsState>,
    input: Res<Mw2Input>,
    held: Res<MeleeHeld>,
    aim: Res<Aim>,
    weapon: Res<crate::weapon::Weapon>,
    keys: Res<ButtonInput<KeyCode>>,
    hand_spells: Res<crate::spellcast::HandSpells>,
    mut hands: ResMut<Hands>,
    bar: Option<ResMut<FusionActionBar>>,
    mut out: MessageWriter<benilla_app::fusion::SendFusionChannel>,
    mut released: MessageWriter<crate::spellcast::HandReleased>,
    mut last_swings: Local<u64>,
    mut was_down: Local<[bool; 2]>,
) {
    use crate::spellcast::{
        CAST_FULL, HAND_BOTH, HandReleased, Release, SPRAY_AIM_EVERY_SECS, Shape, charge_fraction,
        full_charge, hand_aim, resolve_releases, send_aim, send_spray, spray_aim,
    };
    let Some(mut bar) = bar else {
        return;
    };
    // A spray stops when its hand stops holding it: let go, another spell readied, the hands down.
    let stop_spray = |hands: &mut Hands,
                      side: usize,
                      out: &mut MessageWriter<benilla_app::fusion::SendFusionChannel>| {
        if hands.spraying[side].take().is_some() {
            send_spray(out, &spray_aim(&aim, hands.spray_spell[side], side as u8, false));
        }
    };
    if !state.active || !held.0 {
        *last_swings = weapon.knife_swings;
        *was_down = [false; 2];
        hands.charging = [None; 2];
        hands.pending = [None; 2];
        for side in 0..2 {
            stop_spray(&mut *hands, side, &mut out);
        }
        return;
    }
    let t = time.elapsed_secs();
    let target = aim
        .target
        .as_ref()
        .filter(|a| !a.service)
        .map(|a| (a.entity, a.guid));
    let cast = |bar: &mut FusionActionBar, action: u32| {
        if let Some(sel) = target {
            bar.select = Some(sel);
        }
        bar.use_now.push(action);
    };
    for (side, control) in [(0usize, input.fire), (1, input.ads)] {
        let down = control.held;
        let pressed = control.pressed || (down && !was_down[side]);
        let released = !down && was_down[side];
        was_down[side] = down;
        let Some((action, spell_id)) = hands.spell_in(side) else {
            hands.charging[side] = None;
            hands.pending[side] = None;
            stop_spray(&mut *hands, side, &mut out);
            continue;
        };
        if hands.spraying[side].is_some() && hands.spray_spell[side] != spell_id {
            stop_spray(&mut *hands, side, &mut out);
        }
        match hand_spells.0.get(&spell_id) {
            // A spray pours from the press while the button stays down, its aim going to the
            // server every tenth of a second; letting go stops it.
            Some(info) if info.shape == Shape::Spray => {
                hands.charging[side] = None;
                hands.pending[side] = None;
                let hand = side as u8;
                if pressed && hands.spraying[side].is_none() {
                    hands.spraying[side] = Some(t);
                    hands.spray_spell[side] = spell_id;
                    hands.spray_sent[side] = t;
                    send_spray(&mut out, &spray_aim(&aim, spell_id, hand, true));
                    hands.cast_at[side] = Some(t);
                } else if down
                    && hands.spraying[side].is_some()
                    && t - hands.spray_sent[side] >= SPRAY_AIM_EVERY_SECS
                {
                    hands.spray_sent[side] = t;
                    send_spray(&mut out, &spray_aim(&aim, spell_id, hand, true));
                }
                if !down {
                    stop_spray(&mut *hands, side, &mut out);
                }
            }
            // Not one the server charges: cast at the press, as a bar button would.
            None => {
                if pressed {
                    cast(&mut bar, action);
                    hands.cast_at[side] = Some(t);
                }
            }
            Some(info) => {
                if pressed {
                    hands.charging[side] = Some(t);
                    hands.charge_from[side] = t;
                    hands.charge_secs[side] = info.charge_secs();
                    hands.pending[side] = None;
                }
                if released && let Some(started) = hands.charging[side].take() {
                    if charge_fraction(started, t, hands.charge_secs[side]) >= 1.0 {
                        hands.pending[side] = Some(t);
                        hands.full[side] = full_charge(started, t, hands.charge_secs[side]);
                    } else {
                        // Let go early: the spell fizzles, nothing cast, nothing spent.
                        hands.fizzled_at[side] = Some(t);
                    }
                }
            }
        }
    }
    // The releases due: alone, or both hands at once with the same spell.
    let same = matches!(
        (hands.spell_in(0), hands.spell_in(1)),
        (Some((_, a)), Some((_, b))) if a == b
    );
    let charged_held = [0, 1].map(|side| {
        hands.charging[side].is_some_and(|s| charge_fraction(s, t, hands.charge_secs[side]) >= 1.0)
    });
    let self_key = keys.pressed(KeyCode::AltLeft) || keys.pressed(KeyCode::AltRight);
    let mut pending = hands.pending;
    for release in resolve_releases(&mut pending, charged_held, same, t) {
        let (sides, hand): (&[usize], u8) = match release {
            Release::Dual => (&[0, 1], HAND_BOTH),
            Release::Single(0) => (&[0], 0),
            Release::Single(_) => (&[1], 1),
        };
        let Some((_, spell_id)) = hands.spell_in(sides[0]) else {
            continue;
        };
        // With the crosshair's ray: a bolt flies along it, at whatever is there. Held to a full
        // charge (both hands, for a dual cast): the stronger release.
        let mut release_aim = hand_aim(&aim, spell_id, hand, self_key);
        if sides.iter().all(|&side| hands.full[side]) {
            release_aim.cast.flags |= CAST_FULL;
        }
        for &side in sides {
            hands.full[side] = false;
        }
        send_aim(&mut out, &release_aim);
        released.write(HandReleased {
            spell: spell_id,
            hand,
        });
        for &side in sides {
            hands.cast_at[side] = Some(t);
            hands.released_at[side] = Some(t);
        }
        info!(
            "benilla-mw2: hand {hand} casts spell {spell_id} at the crosshair (unit {:?}, ground {:?})",
            aim.target.as_ref().map(|a| a.guid),
            aim.ground
        );
    }
    hands.pending = pending;
    // A swing went out (crate::weapon counts them): the readied technique rides it.
    if weapon.knife_swings != *last_swings {
        *last_swings = weapon.knife_swings;
        if let Some((action, _)) = hands.technique.take() {
            cast(&mut bar, action);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_spell_readies_by_its_reach() {
        // A shout or a stance: at once, on yourself.
        assert_eq!(readied_as(RANGE_SELF, SCHOOL_PHYSICAL, 0), Readied::CastNow);
        // Heroic Strike (on next swing): rides the swing.
        assert_eq!(readied_as(RANGE_COMBAT, SCHOOL_PHYSICAL, 0x400), Readied::Technique);
        // Bloodthirst, Charge, a hook: physical moves go off at once, no hand taken.
        assert_eq!(readied_as(RANGE_COMBAT, SCHOOL_PHYSICAL, 0), Readied::Move);
        assert_eq!(readied_as(95, SCHOOL_PHYSICAL, 0), Readied::Move); // Charge's 8-25 yards
        // Fireball (fire, thirty yards): a spell, held in a hand.
        assert_eq!(readied_as(4, 2, 0), Readied::InHand);
        // Maul as the server refits it (op 7: its next-swing bits gone): a move at once.
        let maul = 0x414_u32;
        assert_eq!(readied_as(RANGE_COMBAT, SCHOOL_PHYSICAL, maul), Readied::Technique);
        assert_eq!(readied_as(RANGE_COMBAT, SCHOOL_PHYSICAL, maul & !ATTR_ON_NEXT_SWING), Readied::Move);
    }

    #[test]
    fn a_caller_puts_a_spell_in_the_hand_it_names() {
        let mut h = Hands {
            charging: [Some(1.0), Some(1.0)],
            ..Default::default()
        };
        // The off hand (side 1: the right button's, the left fist), as an Alt binding asks.
        put_in_hand(&mut h, 1, 7, 133, 4);
        assert_eq!(h.left, Hand::Spell { action: 7, spell_id: 133 });
        assert_eq!(h.right, Hand::Weapon);
        assert_eq!((h.school[1], h.charging[1], h.charging[0]), (4, None, Some(1.0)));
        assert_eq!(h.spell_in(1), Some((7, 133)));
        // The main hand; a side past 1 is the off hand.
        put_in_hand(&mut h, 0, 8, 116, 16);
        assert_eq!(h.spell_in(0), Some((8, 116)));
        put_in_hand(&mut h, 5, 9, 2948, 4);
        assert_eq!(h.spell_in(1), Some((9, 2948)));
    }

    #[test]
    fn the_left_hand_holds_what_is_equipped() {
        let mut m = MeleeWeapon {
            off: OffHand::Shield,
            ..Default::default()
        };
        assert_eq!(default_left(&m), Hand::Shield);
        m.off = OffHand::Weapon;
        assert_eq!(default_left(&m), Hand::Weapon);
        m.off = OffHand::Held; // a book or an orb: the hand is free for a spell
        assert_eq!(default_left(&m), Hand::Empty);
        m.two_handed = true; // a two-hander takes both hands
        assert_eq!(default_left(&m), Hand::Weapon);
    }

    #[test]
    fn a_spell_readied_under_a_two_hander_frees_the_other_hand() {
        let mut h = Hands {
            right: Hand::Weapon,
            left: Hand::Weapon,
            two_handed: true,
            ..Default::default()
        };
        assert!(h.grips_two_handed());
        put_in_hand(&mut h, 0, 1, 133, 2);
        assert_eq!(h.left, Hand::Empty, "the left hand is its own, not on a hilt");
        assert!(!h.grips_two_handed());
        let now = crate::hand_anim::HandsNow {
            two: true,
            right_spell: true,
            ..Default::default()
        };
        assert!(!now.gripped_two());
    }

}
