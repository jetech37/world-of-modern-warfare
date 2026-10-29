//! Kits (Create-a-Class) with an Azeroth Warfare server (MW2 design spec 2, WOMW design section 3):
//! the five kits the server saved for this character, the one equipped and any waiting to be, and
//! the equipped kit's guns in hand. The window that edits them (loadout, talent picks,
//! killstreaks) is `crate::cac_ui`; the server validates and saves (`CMSG/SMSG_FUSION_LOADOUT`).

use benilla_app::fusion::{
    ClassLoadout, FusionLoadoutReceived, LoadoutError, LoadoutReply, LoadoutRequest, LoadoutStatus,
    NO_CLASS, SendFusionLoadout,
};
use bevy::prelude::*;

use crate::Mw2Set;
use crate::catalog::Catalog;
use crate::config::Loadout;
use crate::fusion::Fusion;

/// The gun a class puts in hand: the catalog gun (the server's id, magazine and fire rate), the
/// MW2 weapon definition for its attachments (the viewmodel) and its camo's model.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InHand {
    pub base: String,
    pub variant: String,
    pub camo_model: Option<String>,
    /// The gun variant whose model and attachments the viewmodel wears, when not its own: the
    /// primary's underbarrel weapon is the primary rifle in hand, sights and all.
    pub look: Option<String>,
    /// The kit's attachments on it (its magazine and fire time come from all of them, a third
    /// and fourth too: [`Catalog::mag_and_fire`]).
    pub attachments: Vec<String>,
}

/// How many of a kit's three perk slots hold `perk` (0 for none).
pub fn perk_count(kit: &ClassLoadout, perk: u16) -> u8 {
    if perk == 0 {
        return 0;
    }
    kit.perks.iter().filter(|&&p| p == perk).count() as u8
}

/// Whether any slot holding `perk` plays its Pro.
pub fn slot_pro(kit: &ClassLoadout, perk: u16) -> bool {
    perk != 0
        && kit
            .perks
            .iter()
            .enumerate()
            .any(|(i, &p)| p == perk && kit.perk_pro & (1 << i) != 0)
}

/// How many attachments the primary and the secondary may carry, as the server's
/// `AttachmentsAllowed`: the primary one, and one more for each Tinker's Rails (four with the
/// Rails in all three perk slots); the secondary one, and its second with the Rails' Pro.
pub fn attachment_room(kit: &ClassLoadout) -> (u8, u8) {
    let rails = perk_count(kit, crate::catalog::perk_id::BLING);
    let primary = (1 + rails).min(MAX_PRIMARY_ATTACHMENTS);
    let pro = u8::from(rails > 0 && slot_pro(kit, crate::catalog::perk_id::BLING));
    let secondary = (1 + pro).min(MAX_SECONDARY_ATTACHMENTS);
    (primary, secondary)
}

pub const MAX_PRIMARY_ATTACHMENTS: u8 = 4;
pub const MAX_SECONDARY_ATTACHMENTS: u8 = 2;

/// A kit's primary attachments, empty slots left out.
pub fn primary_attachments(kit: &ClassLoadout) -> Vec<&str> {
    [
        &kit.primary_att1,
        &kit.primary_att2,
        &kit.primary_att3,
        &kit.primary_att4,
    ]
    .into_iter()
    .map(String::as_str)
    .filter(|a| !a.is_empty())
    .collect()
}

/// A kit's secondary attachments, empty slots left out.
pub fn secondary_attachments(kit: &ClassLoadout) -> Vec<&str> {
    [&kit.secondary_att, &kit.secondary_att2]
        .into_iter()
        .map(String::as_str)
        .filter(|a| !a.is_empty())
        .collect()
}

/// The classes as the server last said.
#[derive(Resource, Debug, Default)]
pub struct Classes {
    pub classes: [Option<ClassLoadout>; 5],
    pub equipped: Option<u8>,
    /// The class in hand as the server applied it: a saved edit of it waits for the server's
    /// Applied (it holds an equip until out of combat).
    pub applied: Option<ClassLoadout>,
    /// The class the server holds until out of combat (its Pending reply).
    pub pending: Option<u8>,
    /// The slot of the request in flight.
    requested: Option<u8>,
    /// The last refusal and the slot it was for.
    pub rejected: Option<(LoadoutError, u8)>,
    /// A reply came since the window last looked.
    pub fresh: bool,
}

impl Classes {
    /// Take the server's reply.
    pub fn apply(&mut self, reply: &LoadoutReply) {
        self.fresh = true;
        let requested = self.requested.take();
        if reply.status == LoadoutStatus::Rejected {
            // Refused: the server's state, an equip waiting on combat included, stands.
            self.rejected = Some((reply.error, reply.error_slot));
            return;
        }
        self.rejected = None;
        self.classes = Default::default();
        for (slot, class) in &reply.classes {
            if let Some(c) = self.classes.get_mut(*slot as usize) {
                *c = Some(class.clone());
            }
        }
        self.equipped = (reply.equipped_slot != NO_CLASS).then_some(reply.equipped_slot);
        if reply.status == LoadoutStatus::Applied || self.applied.is_none() {
            self.applied = self
                .equipped
                .and_then(|s| self.classes.get(s as usize).cloned().flatten());
        }
        self.pending = match reply.status {
            LoadoutStatus::Pending => requested.or(self.pending),
            _ => None,
        };
    }

    pub fn equipped_class(&self) -> Option<&ClassLoadout> {
        self.applied.as_ref()
    }

    /// Ask the server to save `class` in `slot` without equipping it (the kit window's Save, New,
    /// Copy and Delete).
    pub fn save(&mut self, slot: u8, class: ClassLoadout) -> LoadoutRequest {
        self.requested = None;
        LoadoutRequest {
            equip_slot: NO_CLASS,
            classes: vec![(slot, class)],
        }
    }

    /// Ask the server to save `class` in `slot` and equip it.
    pub fn equip(&mut self, slot: u8, class: ClassLoadout) -> LoadoutRequest {
        self.requested = Some(slot);
        LoadoutRequest {
            equip_slot: slot,
            classes: vec![(slot, class)],
        }
    }
}

/// The equipped class's perks the client plays itself (the server plays the rest).
#[derive(Resource, Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct ClientPerks {
    pub marathon: bool,
    pub sleight_of_hand: bool,
    pub commando: bool,
    pub steady_aim: bool,
    pub hardline: bool,
    /// Steady Aim's Pro: a jump no longer widens the hip cone.
    pub steady_aim_pro: bool,
}

impl ClientPerks {
    pub fn of(class: Option<&ClassLoadout>) -> Self {
        let has =
            |id: u16| class.is_some_and(|c| [c.perks[0], c.perks[1], c.perks[2]].contains(&id));
        use crate::catalog::perk_id::*;
        Self {
            marathon: has(MARATHON),
            sleight_of_hand: has(SLEIGHT_OF_HAND),
            commando: has(COMMANDO),
            steady_aim: has(STEADY_AIM),
            hardline: has(HARDLINE),
            steady_aim_pro: class.is_some_and(|c| slot_pro(c, STEADY_AIM)),
        }
    }
}

/// Which of the class's guns is up: 0 the primary, 1 the secondary, 2 the primary's underbarrel
/// weapon.
#[derive(Resource, Debug, Default)]
pub struct Hand(pub u8);

/// The hand after this frame's keys: 1 or 2 pick the primary or the secondary, 3 raises the
/// primary's underbarrel weapon (2) and lowers it again, the wheel swaps the primary (or its
/// underbarrel weapon) for the secondary; a class without the other gun keeps the one it has.
pub fn next_hand(current: u8, pick: Option<u8>, wheel: bool, has: [bool; 3]) -> u8 {
    // The underbarrel weapon is the primary's: the primary's side of a swap.
    let base = if current == 2 { 0 } else { current.min(1) };
    let want = match (pick, wheel) {
        (Some(2), _) if current == 2 => 0,
        (Some(2), _) => 2,
        (Some(h), _) => h.min(1),
        (None, true) => 1 - base,
        (None, false) => current.min(2),
    };
    if has[want as usize] {
        want
    } else if has[base as usize] {
        base
    } else {
        u8::from(has[1] && !has[0])
    }
}

/// The gun a class puts in a hand (0 primary, 1 secondary, 2 the primary's underbarrel
/// launcher or shotgun), `None` when that slot is empty.
pub fn in_hand(catalog: &Catalog, class: &ClassLoadout, hand: u8) -> Option<InHand> {
    if hand == 0 {
        return primary_in_hand(catalog, class);
    }
    if hand == 2 {
        let alt = catalog.alternate(class.primary, &primary_attachments(class))?;
        let primary = primary_in_hand(catalog, class)?;
        return Some(InHand {
            base: alt.name.clone(),
            variant: alt.name.clone(),
            camo_model: primary.camo_model,
            look: Some(primary.variant),
            attachments: Vec::new(),
        });
    }
    let gun = catalog.gun(class.secondary)?;
    let atts = secondary_attachments(class);
    let variant = catalog.held_variant(gun, &atts)?;
    Some(InHand {
        base: gun.name.clone(),
        variant,
        camo_model: None,
        look: None,
        attachments: atts.into_iter().map(str::to_owned).collect(),
    })
}

/// The primary a class puts in hand, `None` without one (the per-WoW-class gun stays).
pub fn primary_in_hand(catalog: &Catalog, class: &ClassLoadout) -> Option<InHand> {
    let gun = catalog.gun(class.primary)?;
    let atts = primary_attachments(class);
    let variant = catalog.held_variant(gun, &atts)?;
    Some(InHand {
        base: gun.name.clone(),
        variant,
        camo_model: catalog.camo_model(gun, class.primary_camo),
        look: None,
        attachments: atts.into_iter().map(str::to_owned).collect(),
    })
}

/// The class systems, for ordering a test probe before them.
#[derive(SystemSet, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct CacSet;

pub struct CacPlugin;

impl Plugin for CacPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Classes>()
            .init_resource::<Hand>()
            .init_resource::<ClientPerks>()
            .add_systems(
                Update,
                (read_replies, put_in_hand)
                    .chain()
                    .in_set(Mw2Set::Mode)
                    .in_set(CacSet),
            );
    }
}

fn read_replies(mut replies: MessageReader<FusionLoadoutReceived>, mut classes: ResMut<Classes>) {
    for FusionLoadoutReceived(reply) in replies.read() {
        // A refusal shows in the kit window's status line (kept until the next reply, so one
        // made at login is there when the window opens), never the chat.
        info!(
            "benilla-mw2: Create-a-Class {:?}{}, equipped {:?}, {} saved",
            reply.status,
            if reply.status == LoadoutStatus::Rejected {
                format!(" ({:?} in class {})", reply.error, reply.error_slot + 1)
            } else {
                String::new()
            },
            (reply.equipped_slot != NO_CLASS).then_some(reply.equipped_slot + 1),
            reply.classes.len()
        );
        classes.apply(reply);
    }
}

/// The equipped class's primary in hand: the Armory's pick becomes its gun, and the viewmodel
/// shows its attachments and camo (`Loadout::class_gun`).
fn put_in_hand(
    classes: Res<Classes>,
    catalog: Res<Catalog>,
    input: Res<crate::input::Mw2Input>,
    mut up: ResMut<Hand>,
    mut perks: ResMut<ClientPerks>,
    mut fusion: ResMut<Fusion>,
    mut loadout: ResMut<Loadout>,
    mut weapon: ResMut<crate::weapon::Weapon>,
    mut streak: ResMut<crate::streaks::Streak>,
    mut picked: ResMut<crate::pickups::PickedUp>,
) {
    // 1, 2 and the wheel pick the class's primary or secondary; a gun picked up off the ground
    // (crate::pickups) takes its slot's place for this life, and is raised when it comes.
    let raise = if picked.raise.is_some() {
        picked.raise.take()
    } else {
        None
    };
    let overlaid = picked.overlay(classes.equipped_class());
    let class = overlaid.as_ref();
    let p = ClientPerks::of(classes.equipped_class());
    if *perks != p {
        *perks = p;
        weapon.sleight_of_hand = p.sleight_of_hand;
        weapon.steady_aim = p.steady_aim;
        weapon.commando = p.commando;
        weapon.steady_aim_pro = p.steady_aim_pro;
        streak.hardline = p.hardline;
    }
    let has = [0u8, 1, 2].map(|h| class.is_some_and(|c| in_hand(&catalog, c, h).is_some()));
    let next = next_hand(up.0, input.hand.or(raise), input.wheel_swap, has);
    let swapped = next != up.0;
    if swapped {
        up.0 = next;
    }
    // Every frame for the class gun: the loadout is rebuilt for the WoW class at login, which can
    // land after the server's reply. The gun picked only when the classes or the hand change.
    let hand = class.and_then(|c| in_hand(&catalog, c, up.0));
    if (classes.is_changed() || picked.is_changed() || swapped)
        && let Some(h) = &hand
    {
        fusion.selected = Some(h.base.clone());
    }
    if loadout.class_gun != hand {
        loadout.class_gun = hand;
    }
}

/// Send a request (the window calls it through `SendFusionLoadout`).
pub fn send(out: &mut MessageWriter<SendFusionLoadout>, request: LoadoutRequest) {
    out.write(SendFusionLoadout(request));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn class() -> ClassLoadout {
        ClassLoadout {
            name: "Admin".into(),
            primary: 2,
            primary_att1: "acog".into(),
            primary_camo: 1,
            secondary: 30,
            ..Default::default()
        }
    }

    fn catalog() -> Catalog {
        Catalog::parse(include_str!(env!("AW_FUSION_CATALOG"))).unwrap()
    }

    #[test]
    fn the_underbarrel_weapon_wears_the_primarys_look() {
        let cat = catalog();
        let mut cls = class();
        cls.primary = cat.gun_named("m4_mp").unwrap().id;
        cls.primary_att1 = "gl".into();
        cls.primary_att2 = "reflex".into();
        let Some(alt) = in_hand(&cat, &cls, 2) else {
            return; // a catalog without the M4's launcher has nothing to check
        };
        let primary = primary_in_hand(&cat, &cls).unwrap();
        // Its own clips, the rifle's model, camo and attachments (no stray sights).
        assert_eq!(alt.look.as_deref(), Some(primary.variant.as_str()));
        assert_eq!(alt.camo_model, primary.camo_model);
        assert_eq!(primary.look, None);
    }

    #[test]
    fn an_applied_reply_equips_the_class() {
        let mut c = Classes::default();
        let request = c.equip(0, class());
        assert_eq!(request.equip_slot, 0);
        c.apply(&LoadoutReply {
            status: LoadoutStatus::Applied,
            error: LoadoutError::None,
            error_slot: NO_CLASS,
            equipped_slot: 0,
            classes: vec![(0, class())],
        });
        assert_eq!((c.equipped, c.pending), (Some(0), None));
        let cat = catalog();
        let m4 = cat.gun_named("m4_mp").unwrap().id;
        let mut cls = class();
        cls.primary = m4;
        let hand = primary_in_hand(&cat, &cls).expect("in hand");
        assert_eq!(hand.base, "m4_mp");
        assert_eq!(hand.variant, "m4_acog_mp");
        assert_eq!(hand.camo_model.as_deref(), Some("viewmodel_m4_woodland"));
    }

    #[test]
    fn a_class_is_pending_only_when_the_server_says_so() {
        let mut c = Classes::default();
        let reply = |status, error| LoadoutReply {
            status,
            error,
            error_slot: 1,
            equipped_slot: NO_CLASS,
            classes: vec![(1, class())],
        };
        c.equip(1, class());
        assert_eq!(c.pending, None); // asked, not yet waiting on combat
        c.apply(&reply(LoadoutStatus::Rejected, LoadoutError::TooFast));
        assert_eq!(c.pending, None); // a refusal leaves nothing waiting
        c.equip(1, class());
        c.apply(&reply(LoadoutStatus::Pending, LoadoutError::None));
        assert_eq!(c.pending, Some(1));
        // A refused later save leaves the earlier equip waiting, as on the server.
        c.equip(2, class());
        c.apply(&reply(LoadoutStatus::Rejected, LoadoutError::TooFast));
        assert_eq!(c.pending, Some(1));
    }

    #[test]
    fn editing_the_equipped_class_waits_for_the_server() {
        let mut c = Classes::default();
        let applied = |classes: Vec<(u8, ClassLoadout)>| LoadoutReply {
            status: LoadoutStatus::Applied,
            error: LoadoutError::None,
            error_slot: NO_CLASS,
            equipped_slot: 0,
            classes,
        };
        c.apply(&applied(vec![(0, class())]));
        let mut edited = class();
        edited.primary = 9;
        // In combat the server saves the edit but keeps the old class in hand.
        c.apply(&LoadoutReply {
            status: LoadoutStatus::Pending,
            ..applied(vec![(0, edited.clone())])
        });
        assert_eq!(c.equipped_class().map(|k| k.primary), Some(2));
        assert_eq!(c.classes[0].as_ref().map(|k| k.primary), Some(9));
        // Out of combat it applies.
        c.apply(&applied(vec![(0, edited)]));
        assert_eq!(c.equipped_class().map(|k| k.primary), Some(9));
    }

    #[test]
    fn a_rejection_keeps_the_classes() {
        let mut c = Classes::default();
        c.apply(&LoadoutReply {
            status: LoadoutStatus::Applied,
            error: LoadoutError::None,
            error_slot: NO_CLASS,
            equipped_slot: 0,
            classes: vec![(0, class())],
        });
        c.apply(&LoadoutReply {
            status: LoadoutStatus::Rejected,
            error: LoadoutError::Locked,
            error_slot: 1,
            equipped_slot: 0,
            classes: vec![],
        });
        assert_eq!(c.rejected, Some((LoadoutError::Locked, 1)));
        assert!(c.classes[0].is_some() && c.equipped == Some(0));
    }

    #[test]
    fn one_two_and_the_wheel_pick_the_gun() {
        let both = [true, true, false];
        assert_eq!(next_hand(0, Some(1), false, both), 1);
        assert_eq!(next_hand(1, Some(0), false, both), 0);
        assert_eq!(next_hand(0, None, true, both), 1);
        assert_eq!(next_hand(1, None, true, both), 0);
        assert_eq!(next_hand(1, None, false, both), 1);
        // No secondary: the primary stays up.
        assert_eq!(next_hand(0, Some(1), false, [true, false, false]), 0);
        assert_eq!(next_hand(0, None, true, [true, false, false]), 0);
        // Only a secondary: it is up.
        assert_eq!(next_hand(0, None, false, [false, true, false]), 1);
    }

    #[test]
    fn three_raises_the_underbarrel_weapon_and_lowers_it() {
        let all = [true, true, true];
        assert_eq!(next_hand(0, Some(2), false, all), 2);
        assert_eq!(next_hand(2, Some(2), false, all), 0); // again: back to the gun
        assert_eq!(next_hand(1, Some(2), false, all), 2);
        assert_eq!(next_hand(2, None, true, all), 1); // the wheel: the secondary
        assert_eq!(next_hand(2, Some(1), false, all), 1);
        // No launcher or shotgun on the gun: nothing to raise.
        assert_eq!(next_hand(0, Some(2), false, [true, true, false]), 0);
        // The class changed under it: back to the primary.
        assert_eq!(next_hand(2, None, false, [true, true, false]), 0);
    }

    #[test]
    fn the_secondary_takes_its_attachment() {
        let cat = catalog();
        let usp = cat.gun_named("usp_mp").unwrap().id;
        let cls = ClassLoadout {
            secondary: usp,
            secondary_att: "silencer".into(),
            ..Default::default()
        };
        let h = in_hand(&cat, &cls, 1).expect("secondary");
        assert_eq!(
            (h.base.as_str(), h.variant.as_str()),
            ("usp_mp", "usp_silencer_mp")
        );
        assert_eq!(in_hand(&cat, &cls, 0), None);
    }

    #[test]
    fn no_primary_keeps_the_class_gun() {
        let cat = catalog();
        let cls = ClassLoadout::default();
        assert_eq!(primary_in_hand(&cat, &cls), None);
    }
}
