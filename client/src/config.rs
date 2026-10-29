//! The loadout: which of the class's own spells play the part of the gun, the lethal and the
//! knife. Picked from the class when a character enters the world; every field can be overridden
//! from the environment (`MW2_FIRE`, `MW2_FIRE_MODE`, `MW2_LETHAL`, `MW2_KNIFE`, `MW2_WEAPON`,
//! `MW2_MAG`).

use bevy::prelude::*;

/// How the primary fire drives the class's attack.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FireMode {
    /// An auto-repeat spell (`Auto Shot`, wand `Shoot`): started on the press, stopped on the
    /// release with `SpellStopCasting()`.
    AutoRepeat,
    /// A plain spell recast for as long as the trigger is held, the GCD and cast time permitting.
    Spam,
    /// Melee auto attack, started by targeting the unit (the right-click attack path).
    Melee,
}

impl FireMode {
    fn parse(s: &str) -> Option<Self> {
        match s.to_ascii_lowercase().as_str() {
            "auto" | "autorepeat" | "auto-repeat" => Some(Self::AutoRepeat),
            "spam" | "semi" | "cast" => Some(Self::Spam),
            "melee" => Some(Self::Melee),
            _ => None,
        }
    }
}

/// The loadout for the character in the world.
#[derive(Resource, Clone, Debug)]
pub struct Loadout {
    /// The class token (`UnitClass` second return, e.g. `HUNTER`), `None` until in the world.
    pub class: Option<String>,
    /// The weapon name the HUD shows.
    pub weapon: String,
    /// The MW2 weapon whose viewmodel is drawn, by its asset name (`MW2_GUN` overrides).
    pub mw2_weapon: String,
    pub fire_mode: FireMode,
    /// The primary fire's spell; unused by [`FireMode::Melee`].
    pub fire_spell: String,
    /// The lethal (`G`), a spell cast at the unit under the crosshair.
    pub lethal: Option<String>,
    /// The knife (`V`); `None` swings the melee weapon.
    pub knife: Option<String>,
    /// The cosmetic magazine size; `R` refills it.
    pub mag_size: u32,
    /// Seconds between shots the HUD and recoil play while firing a [`FireMode::Spam`] or
    /// [`FireMode::Melee`] weapon; an auto-repeat weapon uses its real ranged speed.
    pub shot_interval: f32,
    /// With a Create-a-Class class equipped (crate::cac): its primary, whose attachments and camo
    /// the viewmodel shows while `mw2_weapon` is that gun.
    pub class_gun: Option<crate::cac::InHand>,
    /// MW2's fire type for the gun in hand (`WeaponDef::fireType`): 0 automatic, 1 one round per
    /// pull, 2-4 that many rounds per pull, 5 a double barrel (one barrel per pull).
    pub fire_type: u8,
    /// The gun in hand is a riot shield: the trigger bashes, nothing is fired.
    pub shield: bool,
}

impl Default for Loadout {
    fn default() -> Self {
        Self::for_class("")
    }
}

/// The MW2 asset behind each HUD weapon name.
fn mw2_weapon_for(weapon: &str) -> &'static str {
    match weapon {
        "INTERVENTION" => "cheytac_mp",
        "FAL" => "fal_mp",
        "M9" => "beretta_mp",
        "AK-47" => "ak47_mp",
        "SPAS-12" => "spas12_mp",
        "UMP45" => "ump45_mp",
        "RPD" => "rpd_mp",
        "TAR-21" => "tavor_mp",
        "RIOT SHIELD" => "riotshield_mp",
        _ => "m4_mp",
    }
}

impl Loadout {
    /// The stock loadout for a class token, before environment overrides.
    pub fn for_class(class: &str) -> Self {
        let (weapon, fire_mode, fire_spell, lethal, knife, mag_size, shot_interval) = match class {
            "HUNTER" => (
                "INTERVENTION",
                FireMode::AutoRepeat,
                "Auto Shot",
                Some("Arcane Shot"),
                Some("Raptor Strike"),
                5,
                2.0,
            ),
            "MAGE" => (
                "FAL",
                FireMode::AutoRepeat,
                "Shoot",
                Some("Fire Blast"),
                None,
                20,
                1.5,
            ),
            "PRIEST" => (
                "M9",
                FireMode::AutoRepeat,
                "Shoot",
                Some("Shadow Word: Pain"),
                None,
                15,
                1.5,
            ),
            "WARLOCK" => (
                "AK-47",
                FireMode::AutoRepeat,
                "Shoot",
                Some("Immolate"),
                None,
                30,
                1.5,
            ),
            "WARRIOR" => (
                "SPAS-12",
                FireMode::Spam,
                "Shoot Gun",
                Some("Charge"),
                Some("Heroic Strike"),
                8,
                1.0,
            ),
            "ROGUE" => (
                "UMP45",
                FireMode::Spam,
                "Shoot Gun",
                Some("Throw"),
                Some("Sinister Strike"),
                32,
                1.0,
            ),
            "SHAMAN" => (
                "RPD",
                FireMode::Spam,
                "Lightning Bolt",
                Some("Earth Shock"),
                None,
                100,
                0.6,
            ),
            "DRUID" => (
                "TAR-21",
                FireMode::Spam,
                "Wrath",
                Some("Moonfire"),
                None,
                30,
                0.6,
            ),
            "PALADIN" => (
                "RIOT SHIELD",
                FireMode::Melee,
                "",
                Some("Judgement"),
                None,
                1,
                1.2,
            ),
            _ => ("M4A1", FireMode::Melee, "", None, None, 30, 1.0),
        };
        Self {
            class: (!class.is_empty()).then(|| class.to_string()),
            mw2_weapon: mw2_weapon_for(weapon).into(),
            weapon: weapon.into(),
            fire_mode,
            fire_spell: fire_spell.into(),
            lethal: lethal.map(Into::into),
            knife: knife.map(Into::into),
            mag_size,
            shot_interval,
            class_gun: None,
            fire_type: 0,
            shield: false,
        }
        .with_env()
    }

    /// Apply the `MW2_*` environment overrides.
    fn with_env(mut self) -> Self {
        let var = |k: &str| std::env::var(k).ok().filter(|v| !v.trim().is_empty());
        if let Some(v) = var("MW2_WEAPON") {
            self.weapon = v;
        }
        if let Some(v) = var("MW2_GUN") {
            self.mw2_weapon = v;
        }
        if let Some(v) = var("MW2_FIRE") {
            self.fire_spell = v;
            // A named spell with no mode is cast, not swung.
            if self.fire_mode == FireMode::Melee {
                self.fire_mode = FireMode::Spam;
            }
        }
        if let Some(m) = var("MW2_FIRE_MODE").and_then(|v| FireMode::parse(&v)) {
            self.fire_mode = m;
        }
        if let Some(v) = var("MW2_LETHAL") {
            self.lethal = Some(v);
        }
        if let Some(v) = var("MW2_KNIFE") {
            self.knife = Some(v);
        }
        if let Some(n) = var("MW2_MAG").and_then(|v| v.parse::<u32>().ok()) {
            self.mag_size = n.max(1);
        }
        self
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_class_has_a_gun() {
        for class in [
            "HUNTER", "MAGE", "PRIEST", "WARLOCK", "WARRIOR", "ROGUE", "SHAMAN", "DRUID", "PALADIN",
        ] {
            let l = Loadout::for_class(class);
            assert_eq!(l.class.as_deref(), Some(class));
            assert!(!l.weapon.is_empty());
            assert!(l.mw2_weapon.ends_with("_mp"));
            assert!(l.mag_size >= 1);
            if l.fire_mode != FireMode::Melee {
                assert!(!l.fire_spell.is_empty(), "{class} fires nothing");
            }
        }
    }

    #[test]
    fn modes_parse() {
        assert_eq!(FireMode::parse("Auto"), Some(FireMode::AutoRepeat));
        assert_eq!(FireMode::parse("spam"), Some(FireMode::Spam));
        assert_eq!(FireMode::parse("melee"), Some(FireMode::Melee));
        assert_eq!(FireMode::parse("laser"), None);
    }
}
