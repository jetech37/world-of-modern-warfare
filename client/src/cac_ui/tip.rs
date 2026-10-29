//! The kit window's talent tooltips, as the stock talent frame's: the option's name, its tier and
//! level, what kind it is, its trainer cost while not bought, what keeps it out of the kit (in
//! red), and its description in gold.
//!
//! The description is the option's spell's (`Spell.dbc`, its `$s1`-style tokens resolved by
//! benilla's token engine) when the option is that spell as is (route A of
//! `design/CLASS_CATALOG.md`); a custom option (routes B and C: a donor spell for the look, the
//! server's own logic) is described by the class catalog's text instead
//! (`client/assets/class_options.json`, made by `client/scripts/gen_class_options.py`), as is any
//! spell whose text does not resolve.

use std::collections::HashMap;
use std::sync::{Arc, OnceLock};

use benilla_assets::{LockRecover, WorldAssets};
use benilla_formats::{
    SpellCatalog, SpellDisplay, SpellDurationCatalog, SpellRadiusCatalog, TokenContext, TokenNumber,
};
use bevy::prelude::*;
use serde::Deserialize;

use crate::classes::{CORE_TIER, ClassPage, Kind, PageEntry, tier_level};
use crate::wow_ui::{GOLD, GREEN, RED, WHITE};

/// One option's words in the class catalog.
#[derive(Debug, Clone, Deserialize, PartialEq, Eq)]
pub struct OptionText {
    /// How it is built: "A" a vanilla spell as is (or tweaked), "B"/"C" custom.
    pub route: String,
    /// The catalog's type column: "ability", "passive", "ability (capstone)"...
    #[serde(rename = "type")]
    pub kind: String,
    /// What it does.
    pub text: String,
    /// The audit (`Fusion::Classes::Spec`, CLASS_CATALOG.md section 15): "working", "partial" or
    /// "unavailable".
    #[serde(default)]
    pub status: String,
    /// What it does differently from the catalog, or why it is unavailable.
    #[serde(default)]
    pub note: String,
    /// A vanilla spell whose effects the server replaces: its own words would mislead.
    #[serde(default)]
    pub replaced: bool,
    /// What the class identity pass adds (the server's `fusion/ClassTuning.h`): its damage scale,
    /// homing, a gap-closer's guard, Ambush's sundering, a trap's snap.
    #[serde(default)]
    pub identity: String,
}

/// The class catalog's text by option id.
pub fn option_text(id: u16) -> Option<&'static OptionText> {
    static TABLE: OnceLock<HashMap<u16, OptionText>> = OnceLock::new();
    TABLE
        .get_or_init(|| {
            let raw: HashMap<String, OptionText> =
                serde_json::from_str(include_str!("../../assets/class_options.json"))
                    .unwrap_or_default();
            raw.into_iter()
                .filter_map(|(k, v)| Some((k.parse().ok()?, v)))
                .collect()
        })
        .get(&id)
}

/// A tier's trainer price in gold (the server's `Classes::TierPrice`).
pub fn tier_gold(tier: u8) -> u32 {
    [0, 1, 5, 12, 22, 35, 60]
        .get(usize::from(tier))
        .copied()
        .unwrap_or(0)
}

/// The tables the token engine reads besides the spells (`$d` durations, `$a` radii).
#[derive(Resource, Default)]
pub struct TokenTables(pub Option<Arc<(SpellDurationCatalog, SpellRadiusCatalog)>>);

/// Load the duration and radius tables from the install, once.
pub fn load_token_tables(
    mut tables: ResMut<TokenTables>,
    world: Option<Res<WorldAssets>>,
    mut tried: Local<bool>,
) {
    if *tried || tables.0.is_some() {
        return;
    }
    let Some(world) = world else {
        return;
    };
    *tried = true;
    let mut chain = world.chain.lock_recover();
    let durations = benilla_formats::load_spell_durations(&mut chain);
    let radii = benilla_formats::load_spell_radii(&mut chain);
    match (durations, radii) {
        (Ok(d), Ok(r)) => tables.0 = Some(Arc::new((d, r))),
        (Err(e), _) | (_, Err(e)) => {
            warn!("benilla-mw2: talent tooltips without spell durations: {e:#}")
        }
    }
}

/// The 1.12 `GlobalStrings` the token engine asks for.
fn global(key: &str) -> Option<String> {
    Some(
        match key {
            "SPELL_DURATION_UNTIL_CANCELLED" => "until cancelled",
            "INT_SPELL_DURATION_SEC" | "INT_SPELL_DURATION_SEC_P1" => "%d sec",
            "INT_SPELL_DURATION_MIN" | "INT_SPELL_DURATION_MIN_P1" => "%d min",
            "INT_SPELL_DURATION_HOURS" => "%d hour",
            "INT_SPELL_DURATION_HOURS_P1" => "%d hours",
            "INT_SPELL_DURATION_DAYS" => "%d day",
            "INT_SPELL_DURATION_DAYS_P1" => "%d days",
            "SPELL_DURATION_SEC" => "%.2f sec",
            "SPELL_DURATION_MIN" => "%.2f min",
            "SPELL_DURATION_HOURS" => "%.2f hours",
            "SPELL_DURATION_DAYS" => "%.2f days",
            "INT_SPELL_POINTS_SPREAD_TEMPLATE" => "%d to %d",
            "SPELL_POINTS_SPREAD_TEMPLATE" => "%.1f to %.1f",
            _ => return None,
        }
        .to_owned(),
    )
}

fn printf(template: &str, args: &[TokenNumber]) -> String {
    let args: Vec<benilla_ui::strings::Arg> = args
        .iter()
        .map(|n| match n {
            TokenNumber::Int(v) => benilla_ui::strings::Arg::D(*v),
            TokenNumber::Float(v) => benilla_ui::strings::Arg::F(*v),
        })
        .collect();
    benilla_ui::strings::fill(template, &args)
}

/// A spell's description with its tokens resolved; `None` when it has none, or a token stays
/// unresolved (a raw `$` would read as a fault).
pub fn spell_description(
    spells: &SpellCatalog,
    tables: Option<&(SpellDurationCatalog, SpellRadiusCatalog)>,
    spell: u32,
) -> Option<String> {
    spell_description_at(spells, tables, spell, 0)
}

/// [`spell_description`] at a caster's level: the per-level terms of its numbers grow with it (up
/// to the spell's own top level), as the server's cast does. Level 0 reads the spell's flat
/// numbers (no caster).
pub fn spell_description_at(
    spells: &SpellCatalog,
    tables: Option<&(SpellDurationCatalog, SpellRadiusCatalog)>,
    spell: u32,
    level: u8,
) -> Option<String> {
    let d: &SpellDisplay = spells.get(spell)?;
    let raw = d.description.as_deref()?.trim();
    if raw.is_empty() {
        return None;
    }
    let empty = (
        SpellDurationCatalog::default(),
        SpellRadiusCatalog::default(),
    );
    let (durations, radii) = tables.unwrap_or(&empty);
    let lookup = |id: u32| spells.get(id);
    let ctx = TokenContext {
        durations,
        radii,
        ranges: None,
        // The token engine reads the level off the skill in the spell's line (skill / 5).
        skill: &|_| u32::from(level) * 5,
        lookup: &lookup,
        mods: None,
        unmodified_points: false,
        home_area: &|| None,
        global: &global,
        printf: &printf,
    };
    let text = benilla_formats::substitute(raw, d, &ctx);
    (!text.contains('$') && !text.trim().is_empty()).then(|| text.trim().to_owned())
}

/// The description a tooltip shows: the spell's for an option that is its spell, else the class
/// catalog's; then what the audit says differs ("Partial: no slow yet.").
pub fn description(entry: &PageEntry, spell_text: Option<String>) -> String {
    // No class option is an instrument any more (the rogue's Bard became the Duelist; music is
    // the Minstrelsy profession), so the tooltip is the catalog's alone.
    catalog_description(entry, spell_text)
}

fn catalog_description(entry: &PageEntry, spell_text: Option<String>) -> String {
    let catalog = option_text(entry.id);
    let vanilla = catalog.is_none_or(|t| t.route == "A" && !t.replaced);
    let text = match (vanilla, spell_text, catalog) {
        (true, Some(s), _) => s,
        (_, _, Some(t)) => t.text.clone(),
        (_, Some(s), None) => s,
        _ => String::new(),
    };
    let text = match catalog.filter(|t| !t.identity.is_empty()) {
        Some(t) if text.is_empty() => t.identity.clone(),
        Some(t) => format!("{text}
{}", t.identity),
        None => text,
    };
    let Some(t) = catalog.filter(|t| !t.note.is_empty()) else {
        return text;
    };
    let note = match t.status.as_str() {
        "partial" => format!("Partial: {}", t.note),
        "unavailable" => format!("Unavailable: {}", t.note),
        _ => t.note.clone(),
    };
    if text.is_empty() {
        note
    } else {
        format!("{text}\n{note}")
    }
}

/// One line of a tooltip (the shared tooltip's, crate::wow_ui).
pub use crate::wow_ui::TipLine;

fn line(text: impl Into<String>, color: Color, size: f32) -> TipLine {
    TipLine::new(text, color, size)
}

/// What the kit stands to do with an option, for the tooltip's state lines.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct OptionState {
    pub picked: bool,
    pub learned: bool,
    pub admin: bool,
    pub level: u8,
}

/// The tooltip's lines for one core ability or option.
pub fn talent_tip(
    entry: &PageEntry,
    archetype: &str,
    state: OptionState,
    description: &str,
) -> Vec<TipLine> {
    let mut out = vec![line(&entry.name, WHITE, 14.0)];
    if entry.tier == CORE_TIER {
        out.push(line("Core ability: every kit has it", WHITE, 11.0));
    } else {
        out.push(line(
            format!(
                "Tier {} (level {}), {archetype}",
                entry.tier,
                tier_level(entry.tier)
            ),
            WHITE,
            11.0,
        ));
    }
    let kind = match entry.kind {
        Kind::Passive => "Passive",
        Kind::Form => "Form",
        Kind::Ability => "Ability",
    };
    out.push(line(
        if entry.capstone {
            format!("{kind}, capstone")
        } else {
            kind.to_owned()
        },
        WHITE,
        11.0,
    ));
    if entry.tier != CORE_TIER {
        if state.picked {
            out.push(line("In this kit", GREEN, 11.0));
        }
        if !state.learned && !state.admin {
            out.push(line(
                format!("Costs {} gold at your class trainer", tier_gold(entry.tier)),
                WHITE,
                11.0,
            ));
            out.push(line("Not bought yet", RED, 11.0));
        }
        if state.level < tier_level(entry.tier) && !state.admin {
            out.push(line(
                format!("Requires level {}", tier_level(entry.tier)),
                RED,
                11.0,
            ));
        }
    }
    if !description.is_empty() {
        out.push(line(description, GOLD, 12.0));
    }
    out
}

/// The tooltip of a spell outside the class catalog (a stock class spell the class keeps, a
/// racial or a trade's, the companion's), as the stock spellbook's: its name, its rank, what it
/// is, passive, and its description in gold.
pub fn spell_tip(
    name: &str,
    rank: Option<&str>,
    kind: &str,
    passive: bool,
    description: &str,
) -> Vec<TipLine> {
    let mut out = vec![line(name, WHITE, 14.0)];
    if let Some(r) = rank.filter(|r| !r.trim().is_empty()) {
        out.push(line(r, crate::wow_ui::GREY, 11.0));
    }
    if !kind.is_empty() {
        out.push(line(kind, WHITE, 11.0));
    }
    if passive {
        out.push(line("Passive", WHITE, 11.0));
    }
    if !description.is_empty() {
        out.push(line(description, GOLD, 12.0));
    }
    out
}

/// The state lines' inputs for an entry, from what the server last said.
pub fn option_state(page: &ClassPage, entry: &PageEntry, picks: &[u16]) -> OptionState {
    OptionState {
        picked: entry.tier != CORE_TIER
            && picks.get(usize::from(entry.tier).wrapping_sub(1)) == Some(&entry.id),
        learned: page.learned(entry.id),
        admin: page.admin(),
        level: page.level(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(id: u16, tier: u8, spell: u32) -> PageEntry {
        PageEntry {
            id,
            tier,
            archetype: 0,
            kind: Kind::Ability,
            capstone: false,
            spell,
            name: "Bulwark".into(),
        }
    }

    #[test]
    fn every_class_option_has_catalog_text() {
        // 9 classes x (3 core + 18 options).
        for cls in [1u16, 2, 3, 4, 5, 7, 8, 9, 11] {
            for tier in 0..=6u16 {
                for arch in 0..3u16 {
                    let id = cls * 100 + tier * 10 + arch + 1;
                    let t = option_text(id).unwrap_or_else(|| panic!("no text for {id}"));
                    assert!(!t.text.is_empty() && !t.text.contains("**"), "{id}");
                    assert!(["A", "B", "C"].contains(&t.route.as_str()), "{id}");
                }
            }
        }
        // Warrior tier 1: Bloodthirst (a vanilla spell), Bulwark (custom).
        assert_eq!(option_text(111).unwrap().route, "A");
        assert_eq!(option_text(112).unwrap().route, "B");
    }

    #[test]
    fn a_custom_option_is_described_by_the_catalog_a_vanilla_one_by_its_spell() {
        // Bulwark (112) is custom: the donor spell's words would mislead.
        let custom = entry(112, 1, 2565);
        let d = description(&custom, Some("Increases your chance to block.".into()));
        assert!(d.contains("frontal cone"), "{d}");
        // Bloodthirst (111) is its spell: the resolved spell text, then the audit's note.
        let vanilla = entry(111, 1, 23894);
        let d = description(&vanilla, Some("Instantly attack the target.".into()));
        assert!(d.starts_with("Instantly attack the target.\n"), "{d}");
        // Battle Standard (113) is working with nothing to add: the words alone.
        let plain = entry(113, 1, 23034);
        assert!(!description(&plain, None).contains('\n'));
        // Blade Blink (822) carries the identity pass's line under its words.
        let closer = entry(822, 2, 1953);
        let d = description(&closer, None);
        assert!(
            d.contains("strike for 20 %EH") && d.contains("\nCharging through fire: 50% fewer bullets"),
            "{d}"
        );
        // Charge (101) is its vanilla spell, then the pass's line.
        let charge = entry(101, 0, 11578);
        let d = description(&charge, Some("Charge an enemy.".into()));
        assert!(d.starts_with("Charge an enemy.\nCharging through fire"), "{d}");
        // No spell text: the catalog's.
        assert!(description(&vanilla, None).contains("Frenzy"));
        // Divine Protection (201) is a vanilla spell the server replaces: the catalog's words.
        let replaced = entry(201, 0, 5573);
        let d = description(&replaced, Some("Immune to all attacks.".into()));
        assert!(!d.contains("Immune") && d.contains("50%"), "{d}");
        // A partial option says what is missing.
        let partial = entry(452, 5, 17773);
        assert!(description(&partial, None).contains("Partial: "));
    }

    #[test]
    fn every_option_carries_its_audit() {
        for cls in [1u16, 2, 3, 4, 5, 7, 8, 9, 11] {
            for tier in 0..=6u16 {
                for arch in 0..3u16 {
                    let id = cls * 100 + tier * 10 + arch + 1;
                    let t = option_text(id).unwrap();
                    assert!(
                        ["working", "partial", "unavailable"].contains(&t.status.as_str()),
                        "{id}"
                    );
                    assert!(t.status == "working" || !t.note.is_empty(), "{id}");
                }
            }
        }
    }

    /// A spell of 42 damage at its base level 10, 2 more a level up to level 20.
    fn spells_one_growing() -> HashMap<u32, SpellDisplay> {
        let mut m = HashMap::new();
        m.insert(
            7,
            SpellDisplay {
                description: Some("Deals $s1 damage.".into()),
                effects: [2, 0, 0],
                effect_base_points: [41, 0, 0],
                effect_base_dice: [1, 0, 0],
                effect_die_sides: [1, 0, 0],
                effect_real_points_per_level: [2.0, 0.0, 0.0],
                base_level: 10,
                max_level: 20,
                ..Default::default()
            },
        );
        m
    }

    #[test]
    fn spell_tokens_resolve_or_fall_back() {
        let mut spells = HashMap::new();
        spells.insert(
            1,
            SpellDisplay {
                description: Some("Deals $s1 damage.".into()),
                effect_base_points: [41, 0, 0],
                effect_base_dice: [1, 0, 0],
                effect_die_sides: [1, 0, 0],
                ..Default::default()
            },
        );
        spells.insert(
            2,
            SpellDisplay {
                description: Some("Your home is $z.".into()),
                ..Default::default()
            },
        );
        let catalog = SpellCatalog::from_displays(spells);
        assert_eq!(
            spell_description(&catalog, None, 1).as_deref(),
            Some("Deals 42 damage.")
        );
        // At a caster's level the per-level growth counts (to the spell's top level).
        let mut grows = spells_one_growing();
        let catalog_growing = SpellCatalog::from_displays(std::mem::take(&mut grows));
        assert_eq!(
            spell_description_at(&catalog_growing, None, 7, 0).as_deref(),
            Some("Deals 42 damage.")
        );
        assert_eq!(
            spell_description_at(&catalog_growing, None, 7, 15).as_deref(),
            Some("Deals 52 damage.")
        );
        assert_eq!(
            spell_description_at(&catalog_growing, None, 7, 60).as_deref(),
            Some("Deals 62 damage.")
        );
        // A token left raw: none (the catalog's text stands in).
        assert_eq!(spell_description(&catalog, None, 2), None);
        assert_eq!(spell_description(&catalog, None, 3), None);
    }

    #[test]
    fn the_tooltip_reads_as_a_stock_talents() {
        let e = entry(112, 3, 2565);
        let lines = talent_tip(
            &e,
            "Vanguard",
            OptionState {
                level: 20,
                ..Default::default()
            },
            "Blocks bullets.",
        );
        let texts: Vec<&str> = lines.iter().map(|l| l.text.as_str()).collect();
        assert_eq!(texts[0], "Bulwark");
        assert_eq!(texts[1], "Tier 3 (level 30), Vanguard");
        assert!(texts.contains(&"Costs 12 gold at your class trainer"));
        assert!(texts.contains(&"Requires level 30"));
        let last = lines.last().unwrap();
        assert_eq!((last.text.as_str(), last.color), ("Blocks bullets.", GOLD));
        // Bought, picked and reached: no cost, no red.
        let ok = talent_tip(
            &e,
            "Vanguard",
            OptionState {
                picked: true,
                learned: true,
                admin: false,
                level: 60,
            },
            "",
        );
        assert!(ok.iter().all(|l| l.color != RED));
        assert!(ok.iter().any(|l| l.text == "In this kit"));
        assert!(!ok.iter().any(|l| l.text.starts_with("Costs")));
        // A core ability says so and sells nothing.
        let core = talent_tip(&entry(101, 0, 11578), "", OptionState::default(), "");
        assert_eq!(core[1].text, "Core ability: every kit has it");
        assert!(!core.iter().any(|l| l.color == RED));
    }

    #[test]
    fn tier_prices_are_the_trainers() {
        assert_eq!(
            (1..=6).map(tier_gold).collect::<Vec<_>>(),
            [1, 5, 12, 22, 35, 60]
        );
    }
}
