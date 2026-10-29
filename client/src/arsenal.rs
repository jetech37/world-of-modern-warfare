//! The arsenal's names inside the world (design doc section 5, stage 1): every weapon,
//! attachment, camo, perk, deathstreak, killstreak and piece of equipment has a name of its own
//! and a line or two of origin, shown in Create-a-Class and on the HUD. The data is one file,
//! `assets/arsenal_names.json`, keyed by MW2's internal names (weapons by weapon name,
//! attachments by id, camos by model suffix, perks by specialty, killstreaks by MW2's streak
//! name), so names can be swapped without touching code. The server's table is generated from
//! the same file (`server/contrib/fusion/make_arsenal_names.py`).

use std::collections::BTreeMap;
use std::sync::OnceLock;

use serde::Deserialize;

use crate::catalog::{Catalog, Gun};

const NAMES: &str = include_str!("../assets/arsenal_names.json");

/// One entry: its name and where it comes from.
#[derive(Debug, Clone, Deserialize, PartialEq, Eq)]
pub struct Lore {
    pub name: String,
    pub origin: String,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Arsenal {
    pub weapons: BTreeMap<String, Lore>,
    pub attachments: BTreeMap<String, Lore>,
    pub camos: BTreeMap<String, Lore>,
    pub perks: BTreeMap<String, Lore>,
    pub killstreaks: BTreeMap<String, Lore>,
}

/// MW2's own streak names, by the server's `StreakId` (1 UAV ... 15 Tactical Nuke).
pub const STREAK_KEYS: [&str; 15] = [
    "uav",
    "airdrop",
    "counter_uav",
    "sentry",
    "predator_missile",
    "precision_airstrike",
    "harrier_airstrike",
    "helicopter",
    "airdrop_mega",
    "helicopter_flares",
    "stealth_airstrike",
    "helicopter_minigun",
    "ac130",
    "emp",
    "nuke",
];

/// The names, parsed once.
pub fn arsenal() -> &'static Arsenal {
    static A: OnceLock<Arsenal> = OnceLock::new();
    A.get_or_init(|| serde_json::from_str(NAMES).expect("the arsenal names parse"))
}

impl Arsenal {
    /// A catalog weapon (a gun, equipment or a streak's weapon) by MW2 name; an underbarrel
    /// weapon (`gl_m4_mp`, `m4_shotgun_attach_mp`) goes by its attachment's.
    pub fn weapon(&self, name: &str) -> Option<&Lore> {
        self.weapons.get(name).or_else(|| {
            if name.starts_with("gl_") {
                self.attachments.get("gl")
            } else if name.ends_with("_shotgun_attach_mp") {
                self.attachments.get("shotgun")
            } else {
                None
            }
        })
    }

    pub fn attachment(&self, id: &str) -> Option<&Lore> {
        self.attachments.get(id)
    }

    pub fn camo(&self, suffix: &str) -> Option<&Lore> {
        self.camos.get(suffix)
    }

    /// A perk or deathstreak by its MW2 specialty (the catalog's icon).
    pub fn perk(&self, specialty: &str) -> Option<&Lore> {
        self.perks.get(specialty)
    }

    /// A killstreak by the server's `StreakId`.
    pub fn streak(&self, id: u32) -> Option<&Lore> {
        STREAK_KEYS
            .get((id as usize).wrapping_sub(1))
            .and_then(|k| self.killstreaks.get(*k))
    }

    /// A killstreak by the HUD ladder's label (`crate::streaks::REWARDS`: "UAV", "CHOPPER GUNNER").
    pub fn streak_by_label(&self, label: &str) -> Option<&Lore> {
        let key = match label {
            "UAV" => "uav",
            "CARE PACKAGE" => "airdrop",
            "COUNTER-UAV" => "counter_uav",
            "SENTRY GUN" => "sentry",
            "PREDATOR MISSILE" => "predator_missile",
            "PRECISION AIRSTRIKE" => "precision_airstrike",
            "HARRIER STRIKE" => "harrier_airstrike",
            "ATTACK HELICOPTER" => "helicopter",
            "EMERGENCY AIRDROP" => "airdrop_mega",
            "PAVE LOW" => "helicopter_flares",
            "STEALTH BOMBER" => "stealth_airstrike",
            "CHOPPER GUNNER" => "helicopter_minigun",
            "AC-130" => "ac130",
            "EMP" => "emp",
            "TACTICAL NUKE" => "nuke",
            _ => return None,
        };
        self.killstreaks.get(key)
    }
}

/// A weapon's name as the world knows it, else MW2's ("m4_mp": M4).
pub fn weapon_name(name: &str) -> String {
    arsenal().weapon(name).map_or_else(
        || {
            name.trim_end_matches("_mp")
                .replace('_', " ")
                .to_uppercase()
        },
        |l| l.name.clone(),
    )
}

/// A catalog gun's name.
pub fn gun_name(gun: &Gun) -> String {
    weapon_name(&gun.name)
}

/// A catalog id's name, `None` for an id the catalog lacks.
pub fn gun_name_by_id(catalog: &Catalog, id: u16) -> Option<String> {
    catalog.gun(id).map(gun_name)
}

/// A killstreak's name for a HUD ladder label, else the label.
pub fn streak_label(label: &str) -> String {
    arsenal()
        .streak_by_label(label)
        .map_or_else(|| label.to_owned(), |l| l.name.to_uppercase())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn seed() -> Catalog {
        Catalog::parse(include_str!(env!("AW_FUSION_CATALOG"))).unwrap()
    }

    #[test]
    fn every_catalog_item_has_a_name_and_an_origin() {
        let a = arsenal();
        let c = seed();
        for g in &c.weapons {
            let l = a.weapon(&g.name);
            assert!(l.is_some(), "no lore for {}", g.name);
            assert!(l.is_some_and(|l| !l.name.is_empty() && l.origin.len() > 20));
        }
        for at in &c.attachments {
            assert!(a.attachment(&at.id).is_some(), "{}", at.id);
        }
        for camo in &c.camos {
            assert!(a.camo(&camo.suffix).is_some(), "{}", camo.suffix);
        }
        for p in c.perks.iter().chain(&c.deathstreaks) {
            assert!(a.perk(&p.icon).is_some(), "{}", p.icon);
        }
        for id in 1..=15 {
            assert!(a.streak(id).is_some(), "streak {id}");
        }
        for (_, label, _) in crate::streaks::REWARDS {
            assert!(a.streak_by_label(label).is_some(), "{label}");
        }
    }

    #[test]
    fn the_examples_hold() {
        assert_eq!(weapon_name("spas12_mp"), "Bilgewater Scattercannon");
        assert_eq!(weapon_name("barrett_mp"), "Thorium Siege Rifle");
        assert_eq!(weapon_name("c4_mp"), "Seaforium Charge");
        assert_eq!(weapon_name("gl_m4_mp"), "Underslung Bombard");
        assert_eq!(weapon_name("nothing_mp"), "NOTHING");
        assert_eq!(arsenal().streak(1).unwrap().name, "Gnomish Spy Gyro");
        assert_eq!(streak_label("CHOPPER GUNNER"), "GYROCOPTER GUNSHIP");
        assert_eq!(streak_label("SOMETHING"), "SOMETHING");
    }
}
