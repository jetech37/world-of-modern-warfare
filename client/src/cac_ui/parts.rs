//! The parts of a kit the kit window edits, and the rules for what each may hold: every option a
//! part offers (for its sub-menu), why an option can't go in right now (another attachment of its
//! kind or one it doesn't go with, a perk that doesn't stack, no room without Tinker's Rails),
//! whether the account
//! has unlocked it, and assigning one while keeping the rest of the kit valid. The server checks
//! the same rules when the kit is saved (`Fusion::ValidateLoadout`); these only keep the window
//! from offering what it would refuse.

use benilla_app::fusion::{ClassLoadout, PERK_PRO_MASK};

use crate::achievements::{UnlockKind, Unlocks};
use crate::cac::attachment_room;
use crate::catalog::{AttachmentKind, Catalog, Gun, Slot};

/// One part of a kit: a row on the window's left, with a sub-menu on its right.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Part {
    Primary,
    /// The primary's attachment slot 0..4 (a second takes Tinker's Rails, a third two, a fourth
    /// three).
    PrimaryAtt(u8),
    Camo,
    Secondary,
    /// The secondary's attachment slot 0..2 (a second takes the Rails' Pro).
    SecondaryAtt(u8),
    Lethal,
    Tactical,
    /// Perk slot 1..=3.
    Perk(u8),
    Deathstreak,
    /// Killstreak slot 0..3.
    Streak(u8),
}

/// The parts in the window's order.
pub const PARTS: [Part; 18] = [
    Part::Primary,
    Part::PrimaryAtt(0),
    Part::PrimaryAtt(1),
    Part::PrimaryAtt(2),
    Part::PrimaryAtt(3),
    Part::Camo,
    Part::Secondary,
    Part::SecondaryAtt(0),
    Part::SecondaryAtt(1),
    Part::Lethal,
    Part::Tactical,
    Part::Perk(1),
    Part::Perk(2),
    Part::Perk(3),
    Part::Deathstreak,
    Part::Streak(0),
    Part::Streak(1),
    Part::Streak(2),
];

impl Part {
    /// The row's label.
    pub fn label(self) -> &'static str {
        match self {
            Part::Primary => "Primary",
            Part::PrimaryAtt(0) | Part::SecondaryAtt(0) => "Attachment",
            Part::PrimaryAtt(1) | Part::SecondaryAtt(1) => "Attachment 2",
            Part::PrimaryAtt(2) | Part::SecondaryAtt(_) => "Attachment 3",
            Part::PrimaryAtt(_) => "Attachment 4",
            Part::Camo => "Camo",
            Part::Secondary => "Secondary",
            Part::Lethal => "Lethal",
            Part::Tactical => "Tactical",
            Part::Perk(1) => "Perk 1",
            Part::Perk(2) => "Perk 2",
            Part::Perk(_) => "Perk 3",
            Part::Deathstreak => "Deathstreak",
            Part::Streak(0) => "Killstreak 1",
            Part::Streak(1) => "Killstreak 2",
            Part::Streak(_) => "Killstreak 3",
        }
    }

    /// The sub-menu's title.
    pub fn title(self) -> String {
        match self {
            Part::Primary => "Primary Weapon".into(),
            Part::PrimaryAtt(i) => format!("Primary Attachment {}", i + 1),
            Part::Camo => "Primary Camo".into(),
            Part::Secondary => "Secondary Weapon".into(),
            Part::SecondaryAtt(i) => format!("Secondary Attachment {}", i + 1),
            Part::Lethal => "Lethal Equipment".into(),
            Part::Tactical => "Tactical Equipment".into(),
            Part::Perk(t) => format!("Perk {t}"),
            Part::Deathstreak => "Deathstreak".into(),
            Part::Streak(i) => format!("Killstreak {}", i + 1),
        }
    }

    /// What the slot is for, under the sub-menu's title.
    pub fn blurb(self) -> &'static str {
        match self {
            Part::Primary => "Your main gun (key 1). None keeps your class's own gun.",
            Part::PrimaryAtt(_) => {
                "Changes how the primary handles. Tinker's Rails make room for more."
            }
            Part::Camo => "The primary's finish. Looks only.",
            Part::Secondary => "Your sidearm or launcher (key 2).",
            Part::SecondaryAtt(_) => "Changes how the secondary handles.",
            Part::Lethal => "Thrown or placed to kill.",
            Part::Tactical => "Thrown to blind, slow or hide.",
            Part::Perk(_) => {
                "Any perk, from any tier. Some stack when taken again; each has a Pro."
            }
            Part::Deathstreak => "Helps you after several deaths without a kill.",
            Part::Streak(_) => "A reward for kills in a row. None chosen: every unlocked one.",
        }
    }
}

/// One option of a part.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum Choice {
    /// A gun or a piece of equipment by catalog id (0: none).
    Gun(u16),
    /// An attachment by MW2 id ("" none).
    Attachment(String),
    /// A camo by catalog id (0 none).
    Camo(u8),
    /// A perk or deathstreak by id (0 none).
    Perk(u16),
    /// A killstreak by the server's id (0 none).
    Streak(u16),
}

impl Choice {
    pub fn is_none(&self) -> bool {
        match self {
            Choice::Gun(i) | Choice::Perk(i) | Choice::Streak(i) => *i == 0,
            Choice::Camo(c) => *c == 0,
            Choice::Attachment(a) => a.is_empty(),
        }
    }
}

/// The guns for a slot, in catalog order, 0 (none) first.
pub fn guns_for(catalog: &Catalog, slot: Slot) -> Vec<u16> {
    std::iter::once(0)
        .chain(
            catalog
                .weapons
                .iter()
                .filter(|g| Catalog::slot(g) == slot)
                .map(|g| g.id),
        )
        .collect()
}

fn primary_att(kit: &ClassLoadout, i: u8) -> &String {
    match i {
        0 => &kit.primary_att1,
        1 => &kit.primary_att2,
        2 => &kit.primary_att3,
        _ => &kit.primary_att4,
    }
}

fn primary_att_mut(kit: &mut ClassLoadout, i: u8) -> &mut String {
    match i {
        0 => &mut kit.primary_att1,
        1 => &mut kit.primary_att2,
        2 => &mut kit.primary_att3,
        _ => &mut kit.primary_att4,
    }
}

fn secondary_att(kit: &ClassLoadout, i: u8) -> &String {
    if i == 0 {
        &kit.secondary_att
    } else {
        &kit.secondary_att2
    }
}

fn secondary_att_mut(kit: &mut ClassLoadout, i: u8) -> &mut String {
    if i == 0 {
        &mut kit.secondary_att
    } else {
        &mut kit.secondary_att2
    }
}

/// What a part holds now.
pub fn current(kit: &ClassLoadout, part: Part) -> Choice {
    match part {
        Part::Primary => Choice::Gun(kit.primary),
        Part::PrimaryAtt(i) => Choice::Attachment(primary_att(kit, i).clone()),
        Part::Camo => Choice::Camo(kit.primary_camo),
        Part::Secondary => Choice::Gun(kit.secondary),
        Part::SecondaryAtt(i) => Choice::Attachment(secondary_att(kit, i).clone()),
        Part::Lethal => Choice::Gun(kit.lethal),
        Part::Tactical => Choice::Gun(kit.tactical),
        Part::Perk(t) => Choice::Perk(kit.perks[usize::from(t.clamp(1, 3) - 1)]),
        Part::Deathstreak => Choice::Perk(kit.deathstreak),
        Part::Streak(i) => Choice::Streak(kit.streaks[usize::from(i.min(2))]),
    }
}

/// The gun an attachment or camo part belongs to.
fn part_gun<'a>(catalog: &'a Catalog, kit: &ClassLoadout, part: Part) -> Option<&'a Gun> {
    match part {
        Part::PrimaryAtt(_) | Part::Camo => catalog.gun(kit.primary),
        Part::SecondaryAtt(_) => catalog.gun(kit.secondary),
        _ => None,
    }
}

/// Every option a part offers, "none" first (its sub-menu's list, locked and blocked ones too).
pub fn options(catalog: &Catalog, kit: &ClassLoadout, part: Part) -> Vec<Choice> {
    match part {
        Part::Primary | Part::Secondary | Part::Lethal | Part::Tactical => {
            let slot = match part {
                Part::Primary => Slot::Primary,
                Part::Secondary => Slot::Secondary,
                Part::Lethal => Slot::Lethal,
                _ => Slot::Tactical,
            };
            guns_for(catalog, slot)
                .into_iter()
                .map(Choice::Gun)
                .collect()
        }
        Part::PrimaryAtt(_) | Part::SecondaryAtt(_) => std::iter::once(String::new())
            .chain(
                part_gun(catalog, kit, part)
                    .map(|g| g.attachments.clone())
                    .unwrap_or_default(),
            )
            .map(Choice::Attachment)
            .collect(),
        Part::Camo => {
            let takes = part_gun(catalog, kit, part).is_some_and(|g| g.camos);
            std::iter::once(0)
                .chain(catalog.camos.iter().map(|c| c.id).filter(|_| takes))
                .map(Choice::Camo)
                .collect()
        }
        Part::Perk(_) => std::iter::once(0)
            .chain(
                catalog
                    .perks
                    .iter()
                    .filter(|p| (1..=3).contains(&p.tier))
                    .map(|p| p.id),
            )
            .map(Choice::Perk)
            .collect(),
        Part::Deathstreak => std::iter::once(0)
            .chain(catalog.deathstreaks.iter().map(|p| p.id))
            .map(Choice::Perk)
            .collect(),
        Part::Streak(_) => std::iter::once(0)
            .chain(super::streak_ladder().into_iter().map(|c| c.id))
            .map(Choice::Streak)
            .collect(),
    }
}

/// Why a part can take nothing now (it needs a gun, or room from Tinker's Rails); `None` when it
/// can.
pub fn part_closed(catalog: &Catalog, kit: &ClassLoadout, part: Part) -> Option<String> {
    let (room_p, room_s) = attachment_room(kit);
    match part {
        Part::Camo => {
            let Some(gun) = catalog.gun(kit.primary) else {
                return Some("Choose a primary first.".into());
            };
            (!gun.camos).then(|| "This gun takes no camo.".into())
        }
        Part::PrimaryAtt(i) => {
            let Some(gun) = catalog.gun(kit.primary) else {
                return Some("Choose a primary first.".into());
            };
            if gun.attachments.is_empty() {
                return Some("This gun takes no attachments.".into());
            }
            match i {
                i if i < room_p => None,
                1 => Some("Needs Tinker's Rails in a perk slot.".into()),
                2 => Some("Needs Tinker's Rails in two perk slots.".into()),
                _ => Some("Needs Tinker's Rails in all three perk slots.".into()),
            }
        }
        Part::SecondaryAtt(i) => {
            let Some(gun) = catalog.gun(kit.secondary) else {
                return Some("Choose a secondary first.".into());
            };
            if gun.attachments.is_empty() {
                return Some("This gun takes no attachments.".into());
            }
            (i >= room_s).then(|| "Needs Tinker's Rails Pro.".into())
        }
        _ => None,
    }
}

/// The other attachments on the gun a part's attachment goes with.
fn other_attachments(kit: &ClassLoadout, part: Part) -> Vec<String> {
    match part {
        Part::PrimaryAtt(i) => (0..crate::cac::MAX_PRIMARY_ATTACHMENTS)
            .filter(|&j| j != i)
            .map(|j| primary_att(kit, j).clone())
            .filter(|a| !a.is_empty())
            .collect(),
        Part::SecondaryAtt(i) => (0..2u8)
            .filter(|&j| j != i)
            .map(|j| secondary_att(kit, j).clone())
            .filter(|a| !a.is_empty())
            .collect(),
        _ => Vec::new(),
    }
}

/// An attachment's name in the world (crate::arsenal), else MW2's, else its id.
pub fn attachment_name(catalog: &Catalog, id: &str) -> String {
    if id.is_empty() {
        return "None".into();
    }
    crate::arsenal::arsenal()
        .attachment(id)
        .map(|l| l.name.clone())
        .or_else(|| catalog.attachment(id).map(|a| a.name.clone()))
        .unwrap_or_else(|| id.to_owned())
}

/// Whether two attachments go together on one gun (neither lists the other, not the same one).
pub fn compatible(catalog: &Catalog, a: &str, b: &str) -> bool {
    a != b
        && !catalog
            .attachment(a)
            .is_some_and(|x| x.incompatible.iter().any(|i| i == b))
        && !catalog
            .attachment(b)
            .is_some_and(|x| x.incompatible.iter().any(|i| i == a))
}

/// Whether a gun has a definition for this set (up to two), or for every two of three or four.
pub fn combination_exists(catalog: &Catalog, gun: &Gun, atts: &[&str]) -> bool {
    if atts.len() <= 2 {
        return catalog.variant(gun, atts).is_some();
    }
    (0..atts.len())
        .all(|i| (i + 1..atts.len()).all(|j| catalog.variant(gun, &[atts[i], atts[j]]).is_some()))
}

/// Why an option can't go in the part now (the lock aside); `None` when it can.
pub fn blocked(
    catalog: &Catalog,
    kit: &ClassLoadout,
    part: Part,
    choice: &Choice,
) -> Option<String> {
    if choice.is_none() {
        return None;
    }
    if let Some(why) = part_closed(catalog, kit, part) {
        return Some(why);
    }
    match (part, choice) {
        (Part::PrimaryAtt(_) | Part::SecondaryAtt(_), Choice::Attachment(a)) => {
            let others = other_attachments(kit, part);
            if others.iter().any(|o| o == a) {
                return Some("Already on this gun.".into());
            }
            // MW2's Bling: one of each kind (a sight, an underbarrel, ...).
            let kind = AttachmentKind::of(a);
            if others.iter().any(|o| kind.clashes(AttachmentKind::of(o))) {
                return Some(format!("Already has {}.", kind.name()));
            }
            if let Some(o) = others.iter().find(|o| !compatible(catalog, a, o)) {
                return Some(format!("Doesn't go with {}.", attachment_name(catalog, o)));
            }
            let gun = part_gun(catalog, kit, part)?;
            let mut set: Vec<&str> = others.iter().map(String::as_str).collect();
            set.push(a);
            (!combination_exists(catalog, gun, &set))
                .then(|| "This gun has no such combination.".into())
        }
        (Part::Perk(t), Choice::Perk(p)) => {
            let perk = catalog.perk(*p)?;
            let elsewhere = (1..=3u8)
                .filter(|&s| s != t && kit.perks[usize::from(s - 1)] == *p)
                .count();
            let most = usize::from(perk.most_slots());
            (elsewhere >= most).then(|| {
                if most == 1 {
                    "Already in another perk slot, and it doesn't stack.".into()
                } else {
                    format!("Already in {elsewhere} slots: it stacks up to {most}.")
                }
            })
        }
        (Part::Streak(i), Choice::Streak(s)) => {
            let taken = (0..3u8).any(|j| j != i && kit.streaks[usize::from(j)] == *s);
            taken.then(|| "Already in another killstreak slot.".into())
        }
        _ => None,
    }
}

/// The unlock an option needs (the server's `UnlockKind`, id, camo, attachment); `None` for
/// "none", which is always allowed.
pub fn unlock_key(
    kit: &ClassLoadout,
    part: Part,
    choice: &Choice,
) -> Option<(UnlockKind, u32, u8, String)> {
    if choice.is_none() {
        return None;
    }
    let gun = match part {
        Part::PrimaryAtt(_) | Part::Camo => kit.primary,
        Part::SecondaryAtt(_) => kit.secondary,
        _ => 0,
    };
    Some(match (part, choice) {
        (Part::Primary | Part::Secondary, Choice::Gun(id)) => {
            (UnlockKind::Gun, u32::from(*id), 0, String::new())
        }
        (Part::Lethal, Choice::Gun(id)) => (UnlockKind::Lethal, u32::from(*id), 0, String::new()),
        (Part::Tactical, Choice::Gun(id)) => {
            (UnlockKind::Tactical, u32::from(*id), 0, String::new())
        }
        (_, Choice::Attachment(a)) => (UnlockKind::Attachment, u32::from(gun), 0, a.clone()),
        (_, Choice::Camo(c)) => (UnlockKind::Camo, u32::from(gun), *c, String::new()),
        (Part::Deathstreak, Choice::Perk(id)) => {
            (UnlockKind::Deathstreak, u32::from(*id), 0, String::new())
        }
        (_, Choice::Perk(id)) => (UnlockKind::Perk, u32::from(*id), 0, String::new()),
        (_, Choice::Streak(id)) => (UnlockKind::Killstreak, u32::from(*id), 0, String::new()),
        (_, Choice::Gun(id)) => (UnlockKind::Gun, u32::from(*id), 0, String::new()),
    })
}

/// Whether the account has an option.
pub fn unlocked(unlocks: &Unlocks, kit: &ClassLoadout, part: Part, choice: &Choice) -> bool {
    let Some((kind, id, camo, att)) = unlock_key(kit, part, choice) else {
        return true;
    };
    match kind {
        UnlockKind::Gun => unlocks.gun(id as u16),
        UnlockKind::Attachment => unlocks.attachment(id as u16, &att),
        UnlockKind::Camo => unlocks.camo(id as u16, camo),
        UnlockKind::Perk => unlocks.perk(id as u16),
        UnlockKind::Deathstreak => unlocks.deathstreak(id as u16),
        UnlockKind::Lethal => unlocks.lethal(id as u16),
        UnlockKind::Tactical => unlocks.tactical(id as u16),
        UnlockKind::Killstreak => unlocks.killstreak(id),
        UnlockKind::Title | UnlockKind::PerkPro => true,
    }
}

/// Drop the attachments the kit has no room for any more (fewer Tinker's Rails), from the last.
pub fn fit_attachments(kit: &mut ClassLoadout) {
    let (p, s) = attachment_room(kit);
    for i in (0..crate::cac::MAX_PRIMARY_ATTACHMENTS).rev() {
        if i >= p {
            primary_att_mut(kit, i).clear();
        }
    }
    if s < 2 {
        kit.secondary_att2.clear();
    }
}

/// Play a perk's Pro in every slot that holds it (or none), when the perk has one.
pub fn set_pro(catalog: &Catalog, kit: &mut ClassLoadout, perk: u16, on: bool) {
    let has = catalog.perk(perk).is_some_and(|p| p.has_pro());
    for (i, &p) in kit.perks.iter().enumerate() {
        if p == perk && perk != 0 {
            if on && has {
                kit.perk_pro |= 1 << i;
            } else {
                kit.perk_pro &= !(1 << i);
            }
        }
    }
    kit.perk_pro &= PERK_PRO_MASK;
}

/// Whether the kit plays `perk`'s Pro.
pub fn plays_pro(kit: &ClassLoadout, perk: u16) -> bool {
    crate::cac::slot_pro(kit, perk)
}

/// Put an option in a part, keeping the rest of the kit valid: a new gun starts bare, a perk
/// slot takes `pro` for its perk (where the perk has one; a stacked perk's copies agree), and
/// losing Tinker's Rails drops the attachments past the room left.
pub fn assign(catalog: &Catalog, kit: &mut ClassLoadout, part: Part, choice: &Choice, pro: bool) {
    match (part, choice) {
        (Part::Primary, Choice::Gun(id)) => {
            if kit.primary != *id {
                kit.primary = *id;
                kit.primary_att1.clear();
                kit.primary_att2.clear();
                kit.primary_att3.clear();
                kit.primary_att4.clear();
                kit.primary_camo = 0;
            }
        }
        (Part::Secondary, Choice::Gun(id)) => {
            if kit.secondary != *id {
                kit.secondary = *id;
                kit.secondary_att.clear();
                kit.secondary_att2.clear();
            }
        }
        (Part::Lethal, Choice::Gun(id)) => kit.lethal = *id,
        (Part::Tactical, Choice::Gun(id)) => kit.tactical = *id,
        (Part::PrimaryAtt(i), Choice::Attachment(a)) => *primary_att_mut(kit, i) = a.clone(),
        (Part::SecondaryAtt(i), Choice::Attachment(a)) => *secondary_att_mut(kit, i) = a.clone(),
        (Part::Camo, Choice::Camo(c)) => kit.primary_camo = *c,
        (Part::Perk(t), Choice::Perk(id)) => {
            let slot = usize::from(t.clamp(1, 3) - 1);
            kit.perks[slot] = *id;
            kit.perk_pro &= !(1 << slot);
            if *id != 0 {
                set_pro(catalog, kit, *id, pro);
            }
            fit_attachments(kit);
        }
        (Part::Deathstreak, Choice::Perk(id)) => kit.deathstreak = *id,
        (Part::Streak(i), Choice::Streak(id)) => kit.streaks[usize::from(i.min(2))] = *id,
        _ => {}
    }
}

/// A gun's name in the world (crate::arsenal).
pub fn gun_name(catalog: &Catalog, id: u16, part: Part) -> String {
    crate::arsenal::gun_name_by_id(catalog, id).unwrap_or_else(|| match part {
        Part::Primary => "None (class gun)".into(),
        _ => "None".into(),
    })
}

/// A perk's or deathstreak's name in the world.
pub fn perk_name(catalog: &Catalog, id: u16) -> String {
    catalog.perk(id).map_or("None".into(), |p| {
        crate::arsenal::arsenal()
            .perk(&p.icon)
            .map_or(p.name.clone(), |l| l.name.clone())
    })
}

/// A camo's name in the world.
pub fn camo_name(catalog: &Catalog, id: u8) -> String {
    catalog.camo(id).map_or("None".into(), |c| {
        crate::arsenal::arsenal()
            .camo(&c.suffix)
            .map_or(c.name.clone(), |l| l.name.clone())
    })
}

/// An option's name, as its row and the sub-menu show it.
pub fn choice_name(catalog: &Catalog, part: Part, choice: &Choice) -> String {
    match choice {
        Choice::Gun(id) => gun_name(catalog, *id, part),
        Choice::Attachment(a) => attachment_name(catalog, a),
        Choice::Camo(c) => camo_name(catalog, *c),
        Choice::Perk(p) => perk_name(catalog, *p),
        Choice::Streak(0) => "None".into(),
        Choice::Streak(s) => super::streak_name(*s),
    }
}

/// An option's icon material in the install (a gun's HUD icon from its definition, an
/// attachment's or perk's), `None` for none or without the install.
pub fn choice_icon(
    catalog: &Catalog,
    lib: Option<&crate::mw2::load::Mw2Library>,
    choice: &Choice,
) -> Option<String> {
    match choice {
        Choice::Gun(id) => {
            let gun = catalog.gun(*id)?;
            lib?.weapons.get(&gun.name)?.stats.hud_icon.clone()
        }
        Choice::Attachment(a) => catalog.attachment(a).map(|a| a.icon.clone()),
        Choice::Perk(p) => catalog.perk(*p).map(|p| p.icon.clone()),
        Choice::Camo(_) | Choice::Streak(_) => None,
    }
    .filter(|n| !n.is_empty())
}

/// A kit slot that holds no kit of its own: never saved, or saved bare under its default name (a
/// deleted kit).
pub fn kit_is_empty(kit: Option<&ClassLoadout>, slot: u8) -> bool {
    kit.is_none_or(|k| {
        *k == ClassLoadout {
            name: super::empty_kit_name(slot),
            ..Default::default()
        }
    })
}

/// The first kit slot holding no kit (for New and Copy).
pub fn first_empty_slot(kits: &[Option<ClassLoadout>]) -> Option<u8> {
    (0..kits.len() as u8).find(|&s| kit_is_empty(kits[usize::from(s)].as_ref(), s))
}

/// A copy's name: the kit's own with " copy", held to the name's limit.
pub fn copy_name(name: &str) -> String {
    super::edit_name("", &format!("{name} copy"), false)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn catalog() -> Catalog {
        Catalog::parse(include_str!(env!("AW_FUSION_CATALOG"))).unwrap()
    }

    fn rails(n: usize) -> ClassLoadout {
        let mut kit = ClassLoadout::default();
        for i in 0..n {
            kit.perks[i] = crate::catalog::perk_id::BLING;
        }
        kit
    }

    #[test]
    fn every_part_lists_its_options_none_first() {
        let c = catalog();
        let mut kit = ClassLoadout::default();
        kit.primary = c.gun_named("m4_mp").unwrap().id;
        kit.secondary = c.gun_named("usp_mp").unwrap().id;
        for part in PARTS {
            let opts = options(&c, &kit, part);
            assert!(opts[0].is_none(), "{part:?}");
            assert!(opts.len() > 1, "{part:?} offers something");
            assert!(opts.contains(&current(&kit, part)), "{part:?}");
        }
        assert_eq!(options(&c, &ClassLoadout::default(), Part::Camo).len(), 1);
        assert_eq!(options(&c, &kit, Part::Perk(2)).len(), 1 + 16);
        assert_eq!(options(&c, &kit, Part::Deathstreak).len(), 1 + 4);
        assert_eq!(options(&c, &kit, Part::Streak(0)).len(), 1 + 8);
    }

    #[test]
    fn tinkers_rails_open_the_attachment_slots() {
        let c = catalog();
        let mut kit = rails(0);
        kit.primary = c.gun_named("m4_mp").unwrap().id;
        kit.secondary = c.gun_named("usp_mp").unwrap().id;
        assert!(part_closed(&c, &kit, Part::PrimaryAtt(0)).is_none());
        assert!(
            part_closed(&c, &kit, Part::PrimaryAtt(1))
                .unwrap()
                .contains("Tinker's Rails")
        );
        kit.perks = rails(2).perks;
        assert!(part_closed(&c, &kit, Part::PrimaryAtt(2)).is_none());
        assert!(
            part_closed(&c, &kit, Part::PrimaryAtt(3))
                .unwrap()
                .contains("all three perk slots")
        );
        assert!(part_closed(&c, &kit, Part::SecondaryAtt(1)).is_some());
        kit.perks = rails(3).perks;
        assert!(part_closed(&c, &kit, Part::PrimaryAtt(3)).is_none());
        // Three Rails no longer open the secondary's second; the Rails' Pro does.
        assert!(
            part_closed(&c, &kit, Part::SecondaryAtt(1))
                .unwrap()
                .contains("Pro")
        );
        kit.perk_pro = 0b111;
        assert!(part_closed(&c, &kit, Part::SecondaryAtt(1)).is_none());
        kit.perk_pro = 0;
        // Rails x3: four on the primary, one of each kind, every two of them a combination it has.
        for (i, a) in ["reflex", "silencer", "gl", "fmj"].iter().enumerate() {
            let choice = Choice::Attachment((*a).into());
            assert_eq!(
                blocked(&c, &kit, Part::PrimaryAtt(i as u8), &choice),
                None,
                "{a}"
            );
            assign(&c, &mut kit, Part::PrimaryAtt(i as u8), &choice, false);
        }
        assert_eq!(
            crate::cac::primary_attachments(&kit),
            ["reflex", "silencer", "gl", "fmj"]
        );
        // The gun shows two: the sight and the launcher.
        let m4 = c.gun(kit.primary).unwrap();
        assert_eq!(
            c.held_variant(m4, &crate::cac::primary_attachments(&kit)),
            c.variant(m4, &["reflex", "gl"])
        );
        // A second sight or underbarrel never goes on, whatever slot it's tried in.
        let why = |kit: &ClassLoadout, slot: u8, a: &str| {
            blocked(
                &c,
                kit,
                Part::PrimaryAtt(slot),
                &Choice::Attachment(a.into()),
            )
        };
        assert_eq!(
            why(&kit, 3, "acog").as_deref(),
            Some("Already has a sight.")
        );
        assert_eq!(
            why(&kit, 3, "shotgun").as_deref(),
            Some("Already has an underbarrel attachment.")
        );
        assert_eq!(
            why(&kit, 3, "silencer").as_deref(),
            Some("Already on this gun.")
        );
        // Its own slot may change to another of its kind.
        assert_eq!(why(&kit, 0, "eotech"), None);
        // A fourth of a kind not yet on it, pairing with each of the three: offered.
        assign(
            &c,
            &mut kit,
            Part::PrimaryAtt(3),
            &Choice::Attachment(String::new()),
            false,
        );
        assert!(why(&kit, 3, "xmags").is_none());
        // Dropping a Rails drops the fourth, two the third.
        assign(
            &c,
            &mut kit,
            Part::PrimaryAtt(3),
            &Choice::Attachment("xmags".into()),
            false,
        );
        assign(&c, &mut kit, Part::Perk(3), &Choice::Perk(0), false);
        assert!(kit.primary_att4.is_empty() && !kit.primary_att3.is_empty());
        assign(&c, &mut kit, Part::Perk(2), &Choice::Perk(0), false);
        assert!(kit.primary_att3.is_empty() && !kit.primary_att2.is_empty());
    }

    #[test]
    fn a_perk_goes_in_another_slot_only_where_it_stacks() {
        let c = catalog();
        let mut kit = ClassLoadout::default();
        let sp = crate::catalog::perk_id::STOPPING_POWER;
        assign(&c, &mut kit, Part::Perk(1), &Choice::Perk(sp), false);
        assert!(
            blocked(&c, &kit, Part::Perk(2), &Choice::Perk(sp))
                .unwrap()
                .contains("doesn't stack")
        );
        assert_eq!(blocked(&c, &kit, Part::Perk(1), &Choice::Perk(sp)), None); // its own slot
        let marathon = crate::catalog::perk_id::MARATHON;
        assign(&c, &mut kit, Part::Perk(1), &Choice::Perk(marathon), false);
        assign(&c, &mut kit, Part::Perk(2), &Choice::Perk(marathon), false);
        assert_eq!(
            blocked(&c, &kit, Part::Perk(3), &Choice::Perk(marathon)),
            None
        );
        // The same streak twice is not offered either.
        kit.streaks = [1, 0, 0];
        assert!(blocked(&c, &kit, Part::Streak(1), &Choice::Streak(1)).is_some());
        assert_eq!(blocked(&c, &kit, Part::Streak(0), &Choice::Streak(1)), None);
    }

    #[test]
    fn a_pro_follows_its_perk_through_every_slot() {
        let c = catalog();
        let mut kit = ClassLoadout::default();
        let bling = crate::catalog::perk_id::BLING;
        assign(&c, &mut kit, Part::Perk(1), &Choice::Perk(bling), true);
        assert_eq!(kit.perk_pro, 0b001);
        assign(&c, &mut kit, Part::Perk(3), &Choice::Perk(bling), true);
        assert_eq!(kit.perk_pro, 0b101);
        set_pro(&c, &mut kit, bling, false);
        assert_eq!(kit.perk_pro, 0);
        assert!(!plays_pro(&kit, bling));
        // Another perk in the slot takes its own Pro bit, not the old one's.
        assign(
            &c,
            &mut kit,
            Part::Perk(1),
            &Choice::Perk(crate::catalog::perk_id::MARATHON),
            false,
        );
        assert_eq!(kit.perk_pro, 0);
        // None takes no Pro.
        set_pro(&c, &mut kit, 0, true);
        assert_eq!(kit.perk_pro, 0);
    }

    #[test]
    fn locked_options_say_what_unlocks_them() {
        let c = catalog();
        let kit = ClassLoadout {
            primary: c.gun_named("m4_mp").unwrap().id,
            ..Default::default()
        };
        let mut unlocks = Unlocks {
            everything: false,
            held: Default::default(),
        };
        let m4 = Choice::Gun(kit.primary);
        assert!(!unlocked(&unlocks, &kit, Part::Primary, &m4));
        assert!(unlocked(&unlocks, &kit, Part::Primary, &Choice::Gun(0)));
        let key = unlock_key(
            &kit,
            Part::PrimaryAtt(0),
            &Choice::Attachment("acog".into()),
        )
        .unwrap();
        assert_eq!(
            key,
            (
                UnlockKind::Attachment,
                u32::from(kit.primary),
                0,
                "acog".to_string()
            )
        );
        unlocks.held.insert(crate::achievements::Unlock {
            kind: UnlockKind::Gun,
            id: u32::from(kit.primary),
            camo: 0,
            attachment: String::new(),
        });
        assert!(unlocked(&unlocks, &kit, Part::Primary, &m4));
    }

    #[test]
    fn new_copy_and_delete_find_their_slots() {
        let mut kits: [Option<ClassLoadout>; 5] = Default::default();
        assert_eq!(first_empty_slot(&kits), Some(0));
        kits[0] = Some(ClassLoadout {
            name: "Rifle".into(),
            primary: 2,
            ..Default::default()
        });
        assert_eq!(first_empty_slot(&kits), Some(1));
        // A deleted kit (saved bare under its default name) is empty again.
        kits[1] = Some(ClassLoadout {
            name: super::super::empty_kit_name(1),
            ..Default::default()
        });
        assert_eq!(first_empty_slot(&kits), Some(1));
        for (s, k) in kits.iter_mut().enumerate() {
            *k = Some(ClassLoadout {
                name: format!("K{s}"),
                ..Default::default()
            });
        }
        assert_eq!(first_empty_slot(&kits), None);
        assert_eq!(copy_name("Rifle"), "Rifle copy");
        assert_eq!(copy_name(&"x".repeat(24)).len(), 24);
    }
}
