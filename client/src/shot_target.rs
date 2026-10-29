//! Targeting on hit (WOMW): the player's own blow that damages a creature or a player makes it the
//! current target, as clicking it would. The server knows the hits and tells the striker:
//! the `Hit` events of `SMSG_FUSION_EVENTS` (a gun's round, one a pellet; a swing of the hands;
//! a thrown weapon; a grenade's blast) and the `Struck` events (WoW's own damage dealt by the
//! player: a class spell's bolt, splash or spray, a technique). The last unit struck is selected,
//! through the layer's selection (`FusionActionBar::select`), in every action system.
//!
//! The rules ([`pick`]):
//! - every direct blow of the player's own counts; a killstreak's (an airstrike, a sentry, a
//!   craft) does not: those are not the player's aim;
//! - a pellet spread or a splash striking several units selects the last one struck;
//! - a unit the blow killed is passed over (the last living one struck stands);
//! - never a friendly unit ([`benilla_app::Hostility::is_enemy`]): a stray round into a friend
//!   does not steal the target;
//! - at most once per frame's batches, and not when the unit is already the target;
//! - a unit not yet streamed to the client when its confirmation lands is selected once it
//!   appears (within [`PENDING_SECS`]), unless a later blow has picked another.
//!
//! Before this, a hit selected only in the Modern system and only for a gun id the client's own
//! catalog knew (a class gun or an unknown variant selected nothing), and a hit landing before the
//! unit had streamed in was lost: the target "sometimes" did not change.

use benilla_app::fusion::{Event, EventKind, FusionActionBar, FusionEventsReceived};
use benilla_app::{GroupMembers, Hostility, Objects};
use bevy::prelude::*;

use crate::catalog::{Catalog, Slot};
use crate::mode::FpsState;

/// The server's `EVENT_FATAL`.
const EVENT_FATAL: u8 = 0x02;

/// Seconds a struck unit not yet streamed in stays due for selection.
pub const PENDING_SECS: f32 = 1.0;

/// Whether a `Hit` of this weapon id is the player's own blow: anything but a killstreak's (an
/// unknown id, a class gun the seed catalog lacks, a swing or a thrown weapon, counts).
pub fn is_own_blow(catalog: &Catalog, id: u16) -> bool {
    !catalog
        .gun(id)
        .is_some_and(|g| Catalog::slot(g) == Slot::Killstreak)
}

/// The unit a batch of confirmations selects: the last living unit a blow of the player's own
/// struck that `enemy` allows, none for a batch with no such blow.
pub fn pick(
    events: &[Event],
    own_blow: impl Fn(u16) -> bool,
    enemy: impl Fn(u64) -> bool,
) -> Option<u64> {
    events
        .iter()
        .rev()
        .filter(|e| e.guid != 0 && e.flags & EVENT_FATAL == 0)
        .filter(|e| match e.kind {
            EventKind::Hit => own_blow(e.extra),
            EventKind::Struck => true,
            _ => false,
        })
        .map(|e| e.guid)
        .find(|&g| enemy(g))
}

/// Whether a unit the server says we struck may be selected: alive (never a corpse), and an
/// enemy by the client's reaction, or a player (the server's PvP rules, a free-for-all or a duel,
/// let the blow land where the client's flags may not have caught up). Our own side is checked
/// by the caller.
fn struck_enemy(hostility: &Hostility, objects: &Objects, entity: Entity, guid: u64) -> bool {
    let fields = objects.iter().find(|(g, ..)| *g == guid).map(|(_, _, f)| f);
    if fields.is_some_and(|f| f.unit_health() == Some(0)) {
        return false;
    }
    let player = fields.is_some_and(|f| f.object_type() == Some(benilla_app::ObjectType::Player));
    selectable_struck(hostility.is_enemy(entity), player, fields.and_then(|f| f.unit_health()))
}

/// [`struck_enemy`]'s rule over its facts: `health` 0 is a corpse.
pub fn selectable_struck(enemy: bool, player: bool, health: Option<u32>) -> bool {
    health != Some(0) && (enemy || player)
}

/// A struck unit still to select: its guid and when it was struck.
#[derive(Default)]
struct Pending(Option<(u64, f32)>);

pub struct ShotTargetPlugin;

impl Plugin for ShotTargetPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, target_hits);
    }
}

#[allow(clippy::too_many_arguments)]
fn target_hits(
    time: Res<Time>,
    state: Res<FpsState>,
    catalog: Res<Catalog>,
    objects: Objects,
    hostility: Hostility,
    mut events: MessageReader<FusionEventsReceived>,
    bar: Option<ResMut<FusionActionBar>>,
    group: Option<Res<GroupMembers>>,
    mut pending: Local<Pending>,
) {
    let batches: Vec<&FusionEventsReceived> = events.read().collect();
    if !state.in_world {
        pending.0 = None;
        return;
    }
    let Some(mut bar) = bar else { return };
    let now = time.elapsed_secs();
    // Hostility needs the unit's entity; one not streamed yet is taken on trust here and checked
    // once it appears.
    let side = crate::aim::own_side(state.self_guid, group.as_deref().map_or(&[][..], |g| &g.0));
    let enemy = |g: u64| {
        !side.contains(&g) && objects.entity(g).is_none_or(|e| struck_enemy(&hostility, &objects, e, g))
    };
    // The last batch's pick of the frame: one selection at most.
    let chosen = batches.iter().rev().find_map(|FusionEventsReceived(list)| {
        pick(list, |id| is_own_blow(&catalog, id), enemy)
    });
    if let Some(g) = chosen {
        pending.0 = Some((g, now));
    }
    let Some((guid, at)) = pending.0 else { return };
    if now - at > PENDING_SECS {
        pending.0 = None;
        return;
    }
    let Some(entity) = objects.entity(guid) else {
        return; // not streamed in yet: wait
    };
    pending.0 = None;
    if side.contains(&guid) || !struck_enemy(&hostility, &objects, entity, guid) {
        return;
    }
    // Our UNIT_FIELD_TARGET is not the test: it lags the local selection (a target cleared or
    // changed locally still reads the old unit until the server echoes), so a blow on the unit it
    // names was dropped. The selection's own commit dedups a re-select.
    if bar.select.is_some_and(|(_, g)| g == guid) {
        return;
    }
    bar.select = Some((entity, guid));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hit(guid: u64, weapon: u16, flags: u8) -> Event {
        Event {
            kind: EventKind::Hit,
            flags,
            guid,
            amount: 10,
            extra: weapon,
        }
    }

    const GUN: u16 = 1;
    const GRENADE: u16 = 2;

    const STREAK: u16 = 3;

    fn round(id: u16) -> bool {
        id != STREAK
    }

    #[test]
    fn corpses_are_never_selected_and_struck_players_are() {
        assert!(!selectable_struck(true, false, Some(0)), "a corpse");
        assert!(selectable_struck(true, false, Some(50)));
        assert!(selectable_struck(false, true, Some(50)), "a player the server let us hurt");
        assert!(!selectable_struck(false, false, Some(50)), "a friendly creature");
        assert!(selectable_struck(true, false, None), "health not streamed yet");
    }

    #[test]
    fn the_last_unit_a_spread_hits_is_the_target() {
        let events = [hit(10, GUN, 0), hit(11, GUN, 0), hit(12, GUN, 0)];
        assert_eq!(pick(&events, round, |_| true), Some(12));
    }

    #[test]
    fn friends_kills_and_other_hits_are_passed_over() {
        // 12 is a friend, 13 died to the shot, 14 was hit by a killstreak: 11 stands.
        let events = [
            hit(11, GUN, 0),
            hit(12, GUN, 0),
            hit(13, GUN, EVENT_FATAL),
            hit(14, STREAK, 0),
            Event {
                kind: EventKind::Kill,
                flags: EVENT_FATAL,
                guid: 13,
                amount: 0,
                extra: GUN,
            },
        ];
        assert_eq!(pick(&events, round, |g| g != 12), Some(11));
        // Only a friend hit: nothing selected.
        assert_eq!(pick(&[hit(12, GUN, 0)], round, |g| g != 12), None);
        // A batch with no hit (ammo, hurt) selects nothing.
        let hurt = Event {
            kind: EventKind::Hurt,
            flags: 0,
            guid: 20,
            amount: 5,
            extra: GUN,
        };
        assert_eq!(pick(&[hurt], round, |_| true), None);
        assert_eq!(pick(&[hit(0, GUN, 0)], round, |_| true), None, "no unit");
    }

    #[test]
    fn struck_events_and_every_own_blow_select_but_not_a_killstreaks() {
        let struck = |guid: u64, flags: u8| Event {
            kind: EventKind::Struck,
            flags,
            guid,
            amount: 0,
            extra: 0,
        };
        // A bolt (Struck), then a grenade's blast: the blast's unit, the last.
        assert_eq!(
            pick(&[struck(20, 0), hit(21, GRENADE, 0)], round, |_| true),
            Some(21)
        );
        // A spell's splash over two units: the last living enemy.
        assert_eq!(
            pick(&[struck(30, 0), struck(31, EVENT_FATAL)], round, |_| true),
            Some(30)
        );
        // An airstrike's hit never retargets.
        assert_eq!(pick(&[hit(40, STREAK, 0)], round, |_| true), None);
        // A swing's (the server's kSwingWeaponId) counts.
        assert_eq!(pick(&[hit(41, 4999, 0)], round, |_| true), Some(41));
    }

    #[test]
    fn only_killstreaks_are_not_own_blows() {
        let catalog = Catalog::default();
        let of = |slot: Slot| {
            catalog
                .weapons
                .iter()
                .find(|g| Catalog::slot(g) == slot)
                .map(|g| g.id)
        };
        for s in [Slot::Primary, Slot::Secondary, Slot::Lethal] {
            if let Some(id) = of(s) {
                assert!(is_own_blow(&catalog, id), "{s:?}");
            }
        }
        let streak = of(Slot::Killstreak).expect("the seed has a killstreak");
        assert!(!is_own_blow(&catalog, streak));
        // Unknown to the client's catalog (a class gun, a swing): still the player's blow.
        assert!(is_own_blow(&catalog, 0xFFFF));
        assert!(is_own_blow(&catalog, 4999));
    }
}
