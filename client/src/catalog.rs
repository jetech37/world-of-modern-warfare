//! The content catalog the server and the client share (`catalog.json` version 2, MW2 design spec
//! 1.1): the guns with MW2's numbers, the attachments each allows and the weapon definition for
//! every combination, the attachments and the camos. The seed is compiled in
//! (`assets/fusion_catalog.json`); `MW2_FUSION_WEAPONS` reads another.

use std::collections::BTreeMap;

use bevy::prelude::*;
use serde::Deserialize;

const SEED: &str = include_str!(env!("AW_FUSION_CATALOG"));

#[derive(Debug, Clone, Deserialize)]
pub struct Gun {
    pub id: u16,
    pub name: String,
    pub class: String,
    #[serde(default)]
    pub slot: Option<String>,
    #[serde(default)]
    pub damage: f32,
    #[serde(default)]
    pub min_damage: f32,
    #[serde(default)]
    pub max_damage_range: f32,
    #[serde(default)]
    pub min_damage_range: f32,
    #[serde(default = "one")]
    pub pellets: u32,
    /// MW2's `fireType`: 0 automatic, 1 single shot, 2-4 burst.
    #[serde(default)]
    pub fire_type: u8,
    /// An underbarrel weapon's gun and the attachment that carries it (slot `alternate`).
    #[serde(default)]
    pub alternate_of: Option<u16>,
    #[serde(default)]
    pub attachment: Option<String>,
    #[serde(default)]
    pub fire_time: f32,
    #[serde(default)]
    pub mag_size: u32,
    /// Spare rounds beside the magazine.
    #[serde(default)]
    pub max_reserve: u32,
    #[serde(default)]
    pub move_speed_scale: f32,
    #[serde(default = "one_u8")]
    pub unlock_rank: u8,
    /// The attachment ids it allows, in the catalog's order.
    #[serde(default)]
    pub attachments: Vec<String>,
    /// Attachment ids joined with `+` in the catalog's order, to the MW2 weapon definition.
    #[serde(default)]
    pub variants: BTreeMap<String, String>,
    /// What each combination's definition changes (MW2's numbers): key as in `variants`.
    #[serde(default)]
    pub variant_stats: BTreeMap<String, BTreeMap<String, f32>>,
    #[serde(default)]
    pub camos: bool,
    /// The viewmodel a camo's suffix goes on (`viewmodel_fn2000` + `_woodland`).
    #[serde(default)]
    pub camo_model: Option<String>,
    /// What a piece of equipment does, for the kit window (WOMW's text,
    /// `server/contrib/fusion/womw_kit_data.py`).
    #[serde(default)]
    pub effect: String,
}

fn one() -> u32 {
    1
}

fn one_u8() -> u8 {
    1
}

/// Where an attachment goes on a gun, as the server's `Fusion::AttachmentKind` (MW2's groups, as
/// its Bling allowed them): a gun carries at most one of each kind, never two sights or two
/// underbarrels. FMJ and extended mags are kinds of their own (MW2 pairs them).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AttachmentKind {
    Sight,
    Underbarrel,
    Muzzle,
    Ammo,
    Magazine,
    FireRate,
    Akimbo,
    Knife,
    Sensor,
    /// An id the table doesn't know: shares a kind with nothing.
    Other,
}

impl AttachmentKind {
    /// An MW2 attachment id's kind (the server's `KindOfAttachment`).
    pub fn of(id: &str) -> AttachmentKind {
        use AttachmentKind::*;
        match id {
            "reflex" | "eotech" | "acog" | "thermal" => Sight,
            "gl" | "shotgun" | "grip" => Underbarrel,
            "silencer" => Muzzle,
            "fmj" => Ammo,
            "xmags" => Magazine,
            "rof" => FireRate,
            "akimbo" => Akimbo,
            "tactical" => Knife,
            "heartbeat" => Sensor,
            _ => Other,
        }
    }

    /// How the kit window names it in "Already has a sight" (the server's `KindName`).
    pub fn name(self) -> &'static str {
        use AttachmentKind::*;
        match self {
            Sight => "a sight",
            Underbarrel => "an underbarrel attachment",
            Muzzle => "a muzzle attachment",
            Ammo => "special rounds",
            Magazine => "a magazine attachment",
            FireRate => "rapid fire",
            Akimbo => "akimbo",
            Knife => "a tactical knife",
            Sensor => "a heartbeat sensor",
            Other => "an attachment of that kind",
        }
    }

    /// The kind's own name, for the kit window's details ("Sight").
    pub fn label(self) -> &'static str {
        use AttachmentKind::*;
        match self {
            Sight => "Sight",
            Underbarrel => "Underbarrel",
            Muzzle => "Muzzle",
            Ammo => "Ammo",
            Magazine => "Magazine",
            FireRate => "Fire rate",
            Akimbo => "Akimbo",
            Knife => "Tactical knife",
            Sensor => "Sensor",
            Other => "Other",
        }
    }

    /// Whether it shows on the gun's model (FMJ, extended mags and rapid fire don't).
    pub fn shows(self) -> bool {
        !matches!(
            self,
            AttachmentKind::Ammo | AttachmentKind::Magazine | AttachmentKind::FireRate
        )
    }

    /// Whether two attachments of these kinds can't share a gun.
    pub fn clashes(self, other: AttachmentKind) -> bool {
        self == other && self != AttachmentKind::Other
    }
}

#[derive(Debug, Clone, Deserialize)]
pub struct Attachment {
    pub id: String,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub icon: String,
    #[serde(default)]
    pub incompatible: Vec<String>,
    #[serde(default = "one_u8")]
    pub unlock_rank: u8,
    /// What it does, for the kit window.
    #[serde(default)]
    pub effect: String,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Camo {
    pub id: u8,
    pub name: String,
    pub suffix: String,
}

/// How a perk taken in several of a kit's perk slots adds up (the server's `PerkStack`; WOMW
/// design, section 3).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Stack {
    /// One slot only.
    #[default]
    None,
    /// Each copy adds the same again.
    Additive,
    /// Each copy adds less.
    Diminishing,
    /// A rule of its own (Tinker's Rails: an attachment a copy).
    Special,
}

/// A kit's perk slots hold one perk at most this many times.
pub const MAX_PERK_STACK: u8 = 3;

/// An MW2 perk (tier 1-3) or deathstreak (tier 0), with WOMW's stacking, Pro challenge and the
/// kit window's texts (`server/contrib/fusion/womw_kit_data.py`).
#[derive(Debug, Clone, Deserialize)]
pub struct Perk {
    pub id: u16,
    #[serde(default)]
    pub tier: u8,
    pub name: String,
    #[serde(default)]
    pub icon: String,
    #[serde(default)]
    pub deaths: u8,
    #[serde(default = "one_u8")]
    pub unlock_rank: u8,
    #[serde(default)]
    pub stack: Stack,
    #[serde(default = "one_u8")]
    pub max_stack: u8,
    /// Kills with it in the kit that earn its Pro; 0 for none.
    #[serde(default)]
    pub pro_kills: u32,
    #[serde(default)]
    pub effect: String,
    #[serde(default)]
    pub pro_effect: String,
    #[serde(default)]
    pub stack_effect: String,
}

impl Perk {
    /// The most slots it may fill (the server's rule: 1 unless it stacks).
    pub fn most_slots(&self) -> u8 {
        if self.stack == Stack::None || self.tier == 0 {
            1
        } else {
            self.max_stack.clamp(1, MAX_PERK_STACK)
        }
    }

    pub fn has_pro(&self) -> bool {
        self.pro_kills > 0 && self.tier != 0
    }
}

/// The perk ids (the server's `Fusion::PerkId`).
pub mod perk_id {
    pub const SCAVENGER: u16 = 3;
    pub const ONE_MAN_ARMY: u16 = 5;
    pub const STOPPING_POWER: u16 = 6;
    pub const MARATHON: u16 = 1;
    pub const SLEIGHT_OF_HAND: u16 = 2;
    pub const BLING: u16 = 4;
    pub const HARDLINE: u16 = 8;
    pub const COMMANDO: u16 = 11;
    pub const STEADY_AIM: u16 = 12;
}

#[derive(Resource, Debug, Clone, Deserialize)]
pub struct Catalog {
    pub weapons: Vec<Gun>,
    #[serde(default)]
    pub attachments: Vec<Attachment>,
    #[serde(default)]
    pub camos: Vec<Camo>,
    #[serde(default)]
    pub perks: Vec<Perk>,
    #[serde(default)]
    pub deathstreaks: Vec<Perk>,
}

impl Default for Catalog {
    fn default() -> Self {
        let text = std::env::var("MW2_FUSION_WEAPONS")
            .ok()
            .and_then(|p| std::fs::read_to_string(p).ok());
        text.as_deref()
            .and_then(|t| Self::parse(t).ok())
            .unwrap_or_else(|| Self::parse(SEED).expect("the seed catalog parses"))
    }
}

/// Which Create-a-Class slot a gun fills.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Slot {
    Primary,
    Secondary,
    /// An underbarrel weapon, raised from its gun, never a class's pick.
    Alternate,
    /// A class's lethal and tactical equipment.
    Lethal,
    Tactical,
    /// A killstreak, called in (never a class's pick).
    Killstreak,
}

impl Catalog {
    pub fn parse(text: &str) -> Result<Self, serde_json::Error> {
        serde_json::from_str(text)
    }

    pub fn gun(&self, id: u16) -> Option<&Gun> {
        self.weapons.iter().find(|g| g.id == id)
    }

    pub fn gun_named(&self, name: &str) -> Option<&Gun> {
        self.weapons.iter().find(|g| g.name == name)
    }

    pub fn attachment(&self, id: &str) -> Option<&Attachment> {
        self.attachments.iter().find(|a| a.id == id)
    }

    /// A perk or deathstreak by id.
    pub fn perk(&self, id: u16) -> Option<&Perk> {
        self.perks
            .iter()
            .chain(&self.deathstreaks)
            .find(|p| p.id == id)
    }

    /// The choices for a perk slot of `tier` (0: the deathstreak), 0 (none) first.
    /// What a class's perk row can hold: for the deathstreak row (`tier` 0) the deathstreaks; for
    /// any of the three perk rows every perk, of any tier (WOMW: any three perks, not one per
    /// tier), less those the other rows (`taken`) already hold as often as the perk may stack.
    pub fn perk_choices(&self, tier: u8, taken: &[u16]) -> Vec<u16> {
        let ids: Vec<u16> = if tier == 0 {
            self.deathstreaks.iter().map(|p| p.id).collect()
        } else {
            self.perks
                .iter()
                .filter(|p| (1..=3).contains(&p.tier))
                .filter(|p| {
                    let held = taken.iter().filter(|&&t| t == p.id).count();
                    held < usize::from(p.most_slots())
                })
                .map(|p| p.id)
                .collect()
        };
        std::iter::once(0).chain(ids).collect()
    }

    /// A combination's own numbers (MW2's, by its key), else with three or four attachments (no
    /// MW2 definition) each one's own, in the catalog's order, as the server's `Catalog::Effective`.
    pub fn attachment_stats(&self, gun: &Gun, attachments: &[&str]) -> BTreeMap<String, f32> {
        let mut atts: Vec<&str> = attachments
            .iter()
            .copied()
            .filter(|a| !a.is_empty())
            .collect();
        let rank = |id: &str| self.attachments.iter().position(|a| a.id == id);
        atts.sort_by_key(|a| rank(a));
        let mut out = BTreeMap::new();
        if atts.is_empty() {
            return out;
        }
        if let Some(stats) = gun.variant_stats.get(&atts.join("+")) {
            return stats.clone();
        }
        if atts.len() > 2 {
            for a in atts {
                if let Some(stats) = gun.variant_stats.get(a) {
                    out.extend(stats.iter().map(|(k, v)| (k.clone(), *v)));
                }
            }
        }
        out
    }

    /// The definition a gun with these attachments is held as (the viewmodel): `variant`'s for
    /// up to two; for three or four (Tinker's Rails; MW2 has none) two of them, as the server's
    /// `Catalog::HeldVariant`: sights first, then underbarrels, then the other kinds that show,
    /// those that don't (FMJ, extended mags, rapid fire) last, each kind in the catalog's order;
    /// the first pair in that order the gun has.
    pub fn held_variant(&self, gun: &Gun, attachments: &[&str]) -> Option<String> {
        let atts: Vec<&str> = attachments
            .iter()
            .copied()
            .filter(|a| !a.is_empty())
            .collect();
        if atts.len() <= 2 {
            return self.variant(gun, &atts);
        }
        let rank = |id: &str| {
            self.attachments
                .iter()
                .position(|a| a.id == id)
                .unwrap_or(self.attachments.len())
        };
        let place = |id: &str| {
            let k = AttachmentKind::of(id);
            let group = match k {
                AttachmentKind::Sight => 0,
                AttachmentKind::Underbarrel => 1,
                k if k.shows() => 2,
                _ => 3,
            };
            (group, rank(id))
        };
        let mut order = atts.clone();
        order.sort_by_key(|a| place(a));
        (0..order.len()).find_map(|i| {
            (i + 1..order.len()).find_map(|j| self.variant(gun, &[order[i], order[j]]))
        })
    }

    /// A gun's magazine and fire time with these attachments (any number), as the server holds
    /// them: [`attachment_stats`](Self::attachment_stats), akimbo doubling both.
    pub fn mag_and_fire(&self, gun: &Gun, attachments: &[&str]) -> (u32, f32) {
        let stats = self.attachment_stats(gun, attachments);
        let hands = if attachments.contains(&"akimbo") {
            2
        } else {
            1
        };
        let mag = stats.get("mag_size").map_or(gun.mag_size, |&m| m as u32);
        let fire = stats.get("fire_time").copied().unwrap_or(gun.fire_time);
        (mag * hands, fire / hands as f32)
    }

    /// The underbarrel weapon a gun's attachments carry (its grenade launcher or shotgun).
    pub fn alternate(&self, gun: u16, attachments: &[&str]) -> Option<&Gun> {
        self.weapons.iter().find(|g| {
            g.alternate_of == Some(gun)
                && g.attachment
                    .as_deref()
                    .is_some_and(|a| attachments.contains(&a))
        })
    }

    pub fn camo(&self, id: u8) -> Option<&Camo> {
        self.camos.iter().find(|c| c.id == id)
    }

    /// The gun's slot: the catalog's, else MW2's (pistols, machine pistols, shotguns and
    /// launchers are secondaries), as the server decides it.
    pub fn slot(gun: &Gun) -> Slot {
        match gun.slot.as_deref() {
            Some("primary") => Slot::Primary,
            Some("secondary") => Slot::Secondary,
            Some("alternate") => Slot::Alternate,
            Some("lethal") => Slot::Lethal,
            Some("tactical") => Slot::Tactical,
            Some("killstreak") => Slot::Killstreak,
            _ => match gun.class.as_str() {
                "pistol" | "machine_pistol" | "shotgun" | "launcher" => Slot::Secondary,
                _ => Slot::Primary,
            },
        }
    }

    /// The MW2 weapon definition for a gun with these attachments (any order), as the server's
    /// `Catalog::VariantName`: the gun's own name with none, `None` for a combination it lacks.
    pub fn variant(&self, gun: &Gun, attachments: &[&str]) -> Option<String> {
        let mut atts: Vec<&str> = attachments
            .iter()
            .copied()
            .filter(|a| !a.is_empty())
            .collect();
        if atts.is_empty() {
            return Some(gun.name.clone());
        }
        let rank = |id: &str| self.attachments.iter().position(|a| a.id == id);
        if atts.iter().any(|a| rank(a).is_none()) {
            return None;
        }
        atts.sort_by_key(|a| rank(a));
        gun.variants.get(&atts.join("+")).cloned()
    }

    /// A combination's magazine and fire time (MW2's numbers for its definition, `variant` its
    /// weapon name): the gun's own where the combination changes nothing.
    pub fn variant_mag_and_fire(&self, gun: &Gun, variant: &str) -> (u32, f32) {
        let key = gun
            .variants
            .iter()
            .find(|(_, v)| v.as_str() == variant)
            .map(|(k, _)| k);
        let stats = key.and_then(|k| gun.variant_stats.get(k));
        // Akimbo: a magazine in each hand, fired in turn (the server's Catalog::Effective).
        let hands = if key.is_some_and(|k| k.split('+').any(|a| a == "akimbo")) {
            2
        } else {
            1
        };
        let mag = stats
            .and_then(|s| s.get("mag_size"))
            .map_or(gun.mag_size, |&m| m as u32);
        let fire = stats
            .and_then(|s| s.get("fire_time"))
            .copied()
            .unwrap_or(gun.fire_time);
        (mag * hands, fire / hands as f32)
    }

    /// The camo's model for a gun, when the gun takes camos.
    pub fn camo_model(&self, gun: &Gun, camo: u8) -> Option<String> {
        let c = self.camo(camo)?;
        gun.camo_model.as_ref().map(|m| format!("{m}{}", c.suffix))
    }
}

pub struct CatalogPlugin;

impl Plugin for CatalogPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Catalog>();
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn akimbo_doubles_the_magazine_and_the_rate() {
        let c = Catalog::parse(include_str!(env!("AW_FUSION_CATALOG"))).unwrap();
        let usp = c.gun_named("usp_mp").unwrap();
        let (mag, fire) = c.variant_mag_and_fire(usp, "usp_mp");
        let (two_mag, two_fire) = c.variant_mag_and_fire(usp, "usp_akimbo_mp");
        assert_eq!(two_mag, 2 * mag); // a magazine in each hand, as the server counts it
        assert!((two_fire - fire / 2.0).abs() < 1e-6);
    }

    #[test]
    fn a_gun_carries_its_underbarrel_weapon() {
        let c = Catalog::parse(include_str!(env!("AW_FUSION_CATALOG"))).unwrap();
        let m4 = c.gun_named("m4_mp").unwrap().id;
        let gl = c.alternate(m4, &["acog", "gl"]).expect("the M4's launcher");
        assert_eq!(gl.name, "gl_m4_mp");
        assert_eq!(Catalog::slot(gl), Slot::Alternate);
        assert!(c.alternate(m4, &["acog"]).is_none());
    }

    use super::*;

    fn seed() -> Catalog {
        Catalog::parse(SEED).expect("seed")
    }

    #[test]
    fn catalog_reads_the_seed() {
        let c = seed();
        assert_eq!(c.weapons.len(), 77); // 43 guns, 18 underbarrel, 8 equipment, 8 streaks
        let m4 = c.gun_named("m4_mp").expect("m4");
        assert!(m4.attachments.iter().any(|a| a == "acog"));
        assert_eq!(Catalog::slot(m4), Slot::Primary);
        assert_eq!(
            Catalog::slot(c.gun_named("usp_mp").unwrap()),
            Slot::Secondary
        );
        assert_eq!(c.camos.len(), 8);
    }

    #[test]
    fn variant_names_match_the_server() {
        let c = seed();
        let m4 = c.gun_named("m4_mp").unwrap();
        assert_eq!(
            c.variant(m4, &["silencer", "acog"]).as_deref(),
            Some("m4_acog_silencer_mp")
        );
        assert_eq!(c.variant(m4, &[]).as_deref(), Some("m4_mp"));
        assert_eq!(c.variant(m4, &["gl", "shotgun"]), None);
        assert_eq!(c.variant(m4, &["laser"]), None);
    }

    #[test]
    fn extended_mags_hold_more() {
        let c = seed();
        let m4 = c.gun_named("m4_mp").unwrap();
        assert_eq!(c.variant_mag_and_fire(m4, "m4_xmags_mp").0, 45);
        assert_eq!(c.variant_mag_and_fire(m4, "m4_mp").0, 30);
        assert_eq!(c.variant_mag_and_fire(m4, "m4_silencer_mp").0, 30);
    }

    #[test]
    fn a_camo_names_its_model() {
        let c = seed();
        let fn2000 = c.gun_named("fn2000_mp").unwrap();
        assert_eq!(
            c.camo_model(fn2000, 1).as_deref(),
            Some("viewmodel_fn2000_woodland")
        );
        assert_eq!(c.camo_model(c.gun_named("usp_mp").unwrap(), 1), None);
    }
}
