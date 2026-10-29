//! The Companions window's icons, every one the player's own (`Interface\Icons` in the install):
//! a family's (the hunter pet family icons where 1.12 has one, a fitting creature or spell icon
//! where it has none), a ridden form's mount icon, a move's own spell icon (`Spell.dbc` joined
//! with `SpellIcon.dbc`, through crate::hands::SpellTable) or its kind's, each trait's, and the
//! window's own (seen, caught, the Reviving Draught, evolving, learning).
//!
//! Every name here is checked against the install by the tests (`every_icon_is_in_the_install`).

use benilla_formats::SpellCatalog;

/// Where the stock icons live.
const DIR: &str = "Interface\\Icons\\";

/// An icon's path in the install, as `cac_ui::spell_icon_path` takes it.
pub fn path(name: &str) -> String {
    format!("{DIR}{name}")
}

/// Each `Companions::Family` (the window's `FAMILIES`, by id).
const FAMILY: [&str; 63] = [
    "Spell_Nature_Polymorph",            // Critter
    "INV_Misc_Head_Dragon_Black",        // Dragon
    "Spell_Nature_FaerieFire",           // Faerie Dragon
    "Ability_Hunter_Pet_Wolf",           // Wolf
    "Ability_Hunter_Pet_Cat",            // Cat
    "Ability_Hunter_Pet_Bear",           // Bear
    "Ability_Hunter_Pet_Spider",         // Spider
    "Ability_Hunter_Pet_Raptor",         // Raptor
    "Ability_Hunter_Pet_Crocolisk",      // Crocolisk
    "Ability_Hunter_Pet_Gorilla",        // Gorilla
    "Ability_Hunter_Pet_Owl",            // Owl
    "Ability_Hunter_Pet_Bat",            // Bat
    "Ability_Hunter_Pet_TallStrider",    // Tallstrider
    "Ability_Hunter_Pet_Scorpid",        // Scorpid
    "Spell_Fire_Fire",                   // Fire Elemental
    "Spell_Frost_SummonWaterElemental",  // Water Elemental
    "Spell_Nature_Earthquake",           // Earth Elemental
    "Spell_Nature_Cyclone",              // Air Elemental
    "INV_Misc_Head_Murloc_01",           // Murloc
    "Spell_Shadow_SummonInfernal",       // Infernal
    "INV_Misc_Slime_01",                 // Ooze
    "INV_Misc_Gear_01",                  // Mechanical
    "Ability_Mount_Kodo_01",             // Kodo
    "Ability_Mount_RidingHorse",         // Horse
    "Ability_Mount_MechaStrider",        // Mechanostrider
    "Ability_Mount_Undeadhorse",         // Skeletal Horse
    "Ability_Mount_MountainRam",         // Ram
    "Ability_Hunter_Pet_Hyena",          // Hyena
    "Ability_Hunter_Pet_Boar",           // Boar
    "Ability_Hunter_Pet_Crab",           // Crab
    "Ability_Hunter_Pet_Turtle",         // Turtle
    "Ability_Hunter_Pet_WindSerpent",    // Wind Serpent
    "Ability_Hunter_Pet_Vulture",        // Carrion Bird
    "INV_Stone_15",                      // Basilisk
    "INV_Misc_MonsterTail_02",           // Threshadon
    "INV_Misc_MonsterHead_02",           // Hydra
    "Spell_Nature_Thorns",               // Bog Beast
    "Spell_Shadow_SummonImp",            // Imp
    "Spell_Shadow_SummonFelHunter",      // Felhound
    "Spell_Shadow_Metamorphosis",        // Felguard
    "INV_Misc_Bone_HumanSkull_01",       // Skeleton
    "Ability_GhoulFrenzy",               // Ghoul
    "Spell_Shadow_DeathScream",          // Banshee
    "INV_Misc_Head_Gnoll_01",            // Gnoll
    "INV_Misc_Head_Kobold_01",           // Kobold
    "INV_Ore_Copper_01",                 // Trogg
    "INV_Mace_13",                       // Ogre
    "INV_Misc_Head_Centaur_01",          // Centaur
    "INV_Misc_Head_Quillboar_01",        // Quilboar
    "INV_Misc_Pelt_Bear_01",             // Furbolg
    "INV_Feather_12",                    // Harpy
    "INV_Misc_Head_Troll_01",            // Troll
    "INV_Misc_MonsterScales_08",         // Naga
    "INV_Misc_MonsterClaw_02",           // Makrura
    "Spell_Nature_ForceOfNature",        // Moonkin
    "INV_Misc_Pelt_Wolf_01",             // Worgen
    "Ability_Creature_Poison_03",        // Poison Elemental
    "Spell_Shadow_GatherShadows",        // Darkhound
    "INV_Misc_MonsterSpiderCarapace_01", // Crypt Fiend
    "INV_Misc_Birdbeck_01",              // Gryphon
    "INV_Misc_Birdbeck_02",              // Wind Rider
    "INV_Feather_07",                    // Hippogryph
    "INV_Feather_13",                    // Dragonhawk
];

/// A species' icon, by its family: its portrait in the lists.
pub fn family_icon(family: u8) -> String {
    path(FAMILY.get(usize::from(family)).copied().unwrap_or(UNKNOWN))
}

/// A ridden form's icon: its family's stock mount icon, else its family's.
pub fn mount_icon(family: u8) -> String {
    let name = match family {
        3 => "Ability_Mount_BlackDireWolf",
        4 => "Ability_Mount_JungleTiger",
        7 => "Ability_Mount_Raptor",
        22 => "Ability_Mount_Kodo_01",
        23 => "Ability_Mount_RidingHorse",
        24 => "Ability_Mount_MechaStrider",
        25 => "Ability_Mount_Undeadhorse",
        26 => "Ability_Mount_MountainRam",
        _ => return family_icon(family),
    };
    path(name)
}

/// Each `AbilityKind`'s icon, for a move whose spell has none.
const KIND: [&str; 16] = [
    "Ability_MeleeDamage",         // damage
    "Ability_BackStab",            // burst
    "Ability_Whirlwind",           // area
    "Spell_Nature_Reincarnation",  // taunt
    "Spell_Holy_Heal",             // heal
    "Spell_Nature_StrangleVines",  // root
    "Ability_Kick",                // interrupt
    "Ability_ThunderBolt",         // stun
    "Ability_WarStomp",            // area stun
    "Ability_Warrior_Sunder",      // debuff
    "Ability_Warrior_BattleShout", // buff
    "Ability_Defend",              // defensive
    "Spell_Holy_DispelMagic",      // dispel
    "Ability_Warrior_Charge",      // gap closer
    "Spell_Shadow_PsychicScream",  // fear
    "Ability_Mount_RidingHorse",   // mounted
];

pub fn kind_icon(kind: u8) -> String {
    path(KIND.get(usize::from(kind)).copied().unwrap_or(KIND[0]))
}

/// A move's icon: its vanilla spell's own (the server casts that spell for it), else its kind's.
pub fn move_icon(spell: u32, kind: u8, spells: Option<&SpellCatalog>) -> String {
    spells
        .filter(|_| spell != 0)
        .and_then(|t| t.get(spell))
        .and_then(|d| d.icon.clone())
        .filter(|i| !i.is_empty())
        .unwrap_or_else(|| kind_icon(kind))
}

/// Each trait's icon (`Companions::Trait`, 1-30).
const TRAIT: [&str; 30] = [
    "INV_Shield_06",                // Ironhide
    "Ability_Rogue_Sprint",         // Swift
    "Ability_Druid_Rake",           // Ferocious
    "Spell_Nature_BloodLust",       // Bloodthirsty
    "Ability_Racial_BloodRage",     // Berserk
    "Ability_PoisonSting",          // Venomous
    "INV_Misc_Bone_TaurenSkull_01", // Thick-skulled
    "Spell_Fire_Immolation",        // Fire-touched
    "Spell_Frost_FrostBolt02",      // Frost-touched
    "Spell_Shadow_ShadowBolt",      // Shadow-touched
    "Spell_Nature_Lightning",       // Storm-touched
    "Spell_Holy_MagicalSentry",     // Spellward
    "Ability_Hunter_BeastSoothe",   // Loyal
    "Ability_Tracking",             // Tracker
    "INV_Misc_Bag_10",              // Scavenger
    "INV_Misc_Herb_07",             // Forager
    "INV_Misc_Bag_08",              // Packmule
    "Spell_Nature_Strength",        // Giant
    "Ability_Hunter_BeastTraining", // Runt
    "INV_Misc_Pelt_Wolf_02",        // Albino
    "Ability_Stealth",              // Shadowcoat
    "INV_Misc_Coin_02",             // Gilded
    "Spell_Nature_Swiftness",       // Surefooted
    "Spell_Magic_FeatherFall",      // Skybound
    "Spell_Nature_Rejuvenation",    // Tireless
    "INV_Weapon_Rifle_06",          // Gunner
    "Spell_Fire_Incinerate",        // Phoenix-blooded
    "Ability_Hunter_BeastCall",     // Alpha
    "Spell_Shadow_Twilight",        // Twin-souled
    "Spell_Nature_SpiritWolf",      // Primal
];

pub fn trait_icon(id: u8) -> String {
    path(
        TRAIT
            .get(usize::from(id).wrapping_sub(1))
            .copied()
            .unwrap_or(UNKNOWN),
    )
}

/// A role's icon: melee, ranged, tank.
pub fn role_icon(role: u8) -> String {
    path(match role {
        1 => "Ability_Hunter_SniperShot",
        2 => "Ability_Defend",
        _ => "Ability_MeleeDamage",
    })
}

/// A species not met yet.
pub const UNKNOWN: &str = "INV_Misc_QuestionMark";
/// Seen (targeted one) and caught (carried one), the Bestiary's marks.
pub const SEEN: &str = "INV_Misc_Spyglass_02";
pub const CAUGHT: &str = "INV_Box_PetCarrier_01";
/// The Reviving Draught (item 93470 wears the Free Action Potion's look).
pub const DRAUGHT: &str = "INV_Potion_04";
/// Ready to evolve, and the Evolve button.
pub const EVOLVE: &str = "Ability_Hunter_BeastTaming";
/// A move to learn.
pub const LEARN: &str = "INV_Misc_Book_09";
/// Fainted, and the stable master's revive.
pub const REVIVE: &str = "Spell_Holy_Resurrection";
/// Summon and dismiss.
pub const SUMMON: &str = "Ability_Hunter_BeastCall";
pub const DISMISS: &str = "Spell_Shadow_UnsummonBuilding";
/// Breeding at a stable master.
pub const BREED: &str = "INV_Egg_02";
/// Experience.
pub const EXPERIENCE: &str = "Spell_Holy_MindVision";

/// Every icon the window names, for the install test.
pub fn all() -> Vec<String> {
    let mut out: Vec<String> = FAMILY
        .iter()
        .chain(KIND.iter())
        .chain(TRAIT.iter())
        .chain(
            [
                UNKNOWN, SEEN, CAUGHT, DRAUGHT, EVOLVE, LEARN, REVIVE, SUMMON, DISMISS, BREED,
                EXPERIENCE,
            ]
            .iter(),
        )
        .map(|n| path(n))
        .collect();
    out.extend((0..=58u8).map(mount_icon));
    out.extend((0..3u8).map(role_icon));
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_family_kind_and_trait_has_an_icon() {
        // One per family the window names, per ability kind, per trait.
        assert_eq!(FAMILY.len(), 63);
        for f in 0..63u8 {
            assert!(
                !crate::companion_ui::family_name(f).is_empty() && family_icon(f).len() > DIR.len(),
                "family {f}"
            );
        }
        assert_eq!(KIND.len(), 16);
        assert_eq!(TRAIT.len(), 30);
        for t in 1..=30u8 {
            assert!(!crate::companion::trait_name(t).is_empty());
            assert_ne!(trait_icon(t), path(UNKNOWN), "trait {t}");
        }
        // Out of range falls back, never panics.
        assert_eq!(family_icon(200), path(UNKNOWN));
        assert_eq!(trait_icon(0), path(UNKNOWN));
        assert_eq!(kind_icon(99), path(KIND[0]));
        // A mount family rides under its stock mount icon; the rest under their own.
        assert_eq!(mount_icon(3), path("Ability_Mount_BlackDireWolf"));
        assert_eq!(mount_icon(6), family_icon(6));
        // No spell table, or a spell without an icon: the kind's.
        assert_eq!(move_icon(2649, 3, None), kind_icon(3));
    }

    /// Every move in the server's catalogue (`CompanionMoves.cpp`'s `A(spell, "Name", K::Kind`
    /// rows) gets an icon: its spell's own from the install where `Spell.dbc` has one, its kind's
    /// otherwise; and every icon this module names is in the install. Skipped without one.
    #[test]
    fn every_icon_is_in_the_install() {
        let moves = catalogue_moves();
        assert!(moves.len() > 100, "the catalogue's moves: {}", moves.len());
        let Some(data) = install() else {
            eprintln!("no WoW install (WOW_DATA or dist/AzerothWarfare/game/WoW/Data): skipped");
            return;
        };
        let mut chain = benilla_formats::open_chain(&data).expect("the install's chain");
        let has = |chain: &benilla_formats::Chain, p: &str| chain.contains(&format!("{p}.blp"));
        let mut missing: Vec<String> = all().into_iter().filter(|p| !has(&chain, p)).collect();
        let spells = benilla_formats::load_spell_catalog(&mut chain).expect("Spell.dbc");
        let mut own = 0;
        for (spell, name, kind) in &moves {
            let icon = move_icon(*spell, *kind, Some(&spells));
            if icon != kind_icon(*kind) {
                own += 1;
            }
            if !has(&chain, &icon) {
                missing.push(format!("{name} ({spell}): {icon}"));
            }
        }
        assert!(missing.is_empty(), "not in the install: {missing:?}");
        // Nearly every move is a real spell with its own icon.
        assert!(
            own * 10 >= moves.len() * 9,
            "{own} of {} moves have their spell's icon",
            moves.len()
        );
    }

    /// `(spell, name, kind)` of every move in the server's catalogue.
    fn catalogue_moves() -> Vec<(u32, String, u8)> {
        const KINDS: [&str; 16] = [
            "Damage",
            "Burst",
            "AoE",
            "Taunt",
            "Heal",
            "Root",
            "Interrupt",
            "Stun",
            "AoEStun",
            "Debuff",
            "Buff",
            "Defensive",
            "Dispel",
            "GapCloser",
            "Fear",
            "Mounted",
        ];
        let src = std::fs::read_to_string(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../server/src/fusion/src/CompanionMoves.cpp"),
        )
        .expect("CompanionMoves.cpp");
        let mut out = Vec::new();
        // Every `A(` call, named (`kGrowl = A(...)`) or inline in a moveset's table.
        let calls = src.match_indices("A(").filter_map(|(at, _)| {
            let before = src[..at].chars().next_back();
            (!before.is_some_and(|c| c.is_alphanumeric() || c == '_')).then_some(at + 2)
        });
        for start in calls {
            let args: Vec<&str> = src[start..].splitn(4, ',').map(str::trim).collect();
            let (Some(spell), Some(name), Some(kind)) = (
                args.first().and_then(|s| s.parse::<u32>().ok()),
                args.get(1),
                args.get(2).and_then(|k| k.strip_prefix("K::")),
            ) else {
                continue;
            };
            let kind = KINDS.iter().position(|k| *k == kind).expect(kind) as u8;
            out.push((spell, name.trim_matches('"').to_string(), kind));
        }
        out
    }

    fn install() -> Option<std::path::PathBuf> {
        if let Some(d) = std::env::var_os("WOW_DATA").map(std::path::PathBuf::from) {
            return d.is_dir().then_some(d);
        }
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .ancestors()
            .map(|a| a.join("dist/AzerothWarfare/game/WoW/Data"))
            .find(|d| d.is_dir())
    }
}
