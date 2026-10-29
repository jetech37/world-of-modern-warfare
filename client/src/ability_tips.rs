//! The Modern and Utility bars' abilities as inspectable abilities (WOMW): each button's tooltip,
//! as WoW's own spell tooltips look (`GameTooltip:SetAction` on the layer's slot kind, benilla's
//! `LayerTipLine`s): the name in white; the cast line with the range on its right; the count,
//! charges or ammo; what it does, in the spell description's gold, from the catalogue with the
//! arsenal's names and the numbers it fires with now (the gun's damage, fire rate, magazine and
//! attachments; the tool's trade); and the key that uses it. No rank: an ability is one entry.
//!
//! The same lines are the kit window's Abilities page entries for the two bars
//! (crate::cac_ui::abilities), where they can be dragged back onto a bar.

use std::collections::HashMap;

use benilla_app::fusion::ClassLoadout;
use benilla_app::fusion::FusionActionBar;
use benilla_ui::script::LayerTipLine;
use bevy::prelude::*;

use crate::action_systems::{Ability, ActionSystems, System, tool_bit};
use crate::arsenal::{arsenal, gun_name};
use crate::catalog::{AttachmentKind, Catalog, Gun};
use crate::keybinds::{Keymap, SLOTS as SLOT_ACTIONS};

pub const WHITE: [f32; 4] = [1.0, 1.0, 1.0, 1.0];
/// The description's colour (`NORMAL_FONT_COLOR`).
pub const GOLD: [f32; 4] = [1.0, 0.82, 0.0, 1.0];
/// An attachment, as an item's effect line.
pub const GREEN: [f32; 4] = [0.1, 1.0, 0.1, 1.0];
/// An unmet requirement.
pub const RED: [f32; 4] = [1.0, 0.125, 0.125, 1.0];
pub const GREY: [f32; 4] = [0.5, 0.5, 0.5, 1.0];

/// A round's reach in yards (the server's `kGunfight.shotRange`).
pub const GUN_RANGE: f32 = crate::aim::MAX_RANGE;
/// MW2's ranges are in inches.
const INCHES_PER_YARD: f32 = 36.0;

/// What the tooltips are made of, as the client last knew it.
pub struct Context<'a> {
    pub catalog: &'a Catalog,
    /// The class in hand (Create-a-Class), none before the server applies one.
    pub class: Option<&'a ClassLoadout>,
    /// The gun hand out (0 primary, 1 secondary, 2 underbarrel).
    pub hand: u8,
    /// The magazine and the spare rounds of the gun in hand.
    pub mag: u32,
    pub reserve: Option<u32>,
    pub lethal_left: u32,
    pub tactical_left: u32,
    /// Killstreak rewards earned and not called in (`crate::streaks::REWARDS` labels).
    pub streaks_ready: &'a [&'static str],
    /// The special round loaded (`crate::fusion::ROUNDS`) and the level that unlocks them.
    pub round: u8,
    pub level: u32,
    /// The server's tools mask (`OP_TOOLS`), none until told.
    pub usable_tools: Option<u16>,
    /// The Minstrelsy skill.
    pub minstrelsy: u16,
}

fn line(text: impl Into<String>, color: [f32; 4]) -> LayerTipLine {
    LayerTipLine {
        left: text.into(),
        left_color: color,
        right: None,
        wrap: false,
    }
}

fn double(left: impl Into<String>, right: impl Into<String>) -> LayerTipLine {
    LayerTipLine {
        left: left.into(),
        left_color: WHITE,
        right: Some((right.into(), WHITE)),
        wrap: false,
    }
}

fn text(text: impl Into<String>, color: [f32; 4]) -> LayerTipLine {
    LayerTipLine {
        left: text.into(),
        left_color: color,
        right: None,
        wrap: true,
    }
}

/// An attachment's name in the world, else the catalogue's, else its id.
pub fn attachment_name(catalog: &Catalog, id: &str) -> String {
    arsenal()
        .attachment(id)
        .map(|l| l.name.clone())
        .or_else(|| {
            catalog
                .attachment(id)
                .map(|a| a.name.clone())
                .filter(|n| !n.is_empty())
        })
        .unwrap_or_else(|| id.to_owned())
}

/// Rounds a minute from seconds between rounds.
pub fn rounds_per_minute(fire_time: f32) -> u32 {
    if fire_time <= 0.0 {
        0
    } else {
        (60.0 / fire_time).round() as u32
    }
}

/// A gun's lines: its stats with these attachments, and the ammo when it is the one in hand.
fn gun_lines(
    out: &mut Vec<LayerTipLine>,
    catalog: &Catalog,
    gun: &Gun,
    atts: &[&str],
    in_hand: Option<(u32, Option<u32>)>,
) {
    let stats = catalog.attachment_stats(gun, atts);
    let (mag, fire) = catalog.mag_and_fire(gun, atts);
    let damage = stats.get("damage").copied().unwrap_or(gun.damage);
    let min_damage = stats.get("min_damage").copied().unwrap_or(gun.min_damage);
    out.push(double("Instant", format!("{} yd range", GUN_RANGE as u32)));
    match in_hand {
        Some((m, Some(r))) => out.push(line(format!("Ammo: {m} / {r}"), WHITE)),
        Some((m, None)) => out.push(line(format!("Ammo: {m} / {mag}"), WHITE)),
        None => {}
    }
    let pellets = if gun.pellets > 1 {
        format!(" x{} pellets", gun.pellets)
    } else {
        String::new()
    };
    let falloff = if min_damage > 0.0 && (min_damage - damage).abs() > f32::EPSILON {
        format!(
            "{} to {} damage{pellets} (full to {} yd, least past {} yd)",
            damage.round(),
            min_damage.round(),
            (gun.max_damage_range / INCHES_PER_YARD).round(),
            (gun.min_damage_range / INCHES_PER_YARD).round()
        )
    } else {
        format!("{} damage{pellets}", damage.round())
    };
    out.push(line(falloff, WHITE));
    let mode = match gun.fire_type {
        0 => "Automatic".to_owned(),
        1 => "Single shot".to_owned(),
        5 => "Double barrel".to_owned(),
        n => format!("{n}-round burst"),
    };
    out.push(line(
        format!("{mode}, {} rounds a minute", rounds_per_minute(fire)),
        WHITE,
    ));
    if mag > 0 {
        let spare = if gun.max_reserve > 0 {
            format!(", {} spare", gun.max_reserve)
        } else {
            String::new()
        };
        out.push(line(format!("Magazine: {mag}{spare}"), WHITE));
    }
    for a in atts.iter().filter(|a| !a.is_empty()) {
        out.push(line(
            format!(
                "{}: {}",
                AttachmentKind::of(a).label(),
                attachment_name(catalog, a)
            ),
            GREEN,
        ));
    }
    if let Some(l) = arsenal().weapon(&gun.name)
        && !l.origin.is_empty()
    {
        out.push(text(l.origin.clone(), GOLD));
    }
}

/// A piece of equipment's lines: its name, what's left, what it does.
fn equipment_lines(
    out: &mut Vec<LayerTipLine>,
    catalog: &Catalog,
    id: Option<u16>,
    left: u32,
    none: &str,
) -> Option<String> {
    let Some(gun) = id.and_then(|i| catalog.gun(i)).filter(|_| id != Some(0)) else {
        out.push(text(none, GOLD));
        return None;
    };
    out.push(double("Thrown", format!("{left} left")));
    let effect = if gun.effect.is_empty() {
        arsenal()
            .weapon(&gun.name)
            .map(|l| l.origin.clone())
            .unwrap_or_default()
    } else {
        gun.effect.clone()
    };
    if !effect.is_empty() {
        out.push(text(effect, GOLD));
    }
    out.push(text(
        "Readies it in hand (Alt: the off hand); that hand's mouse button throws it where you aim          (held: a frag cooks). Press again to put it back.",
        GOLD,
    ));
    Some(gun_name(gun))
}

/// The trade a tool works for, and what a strike does.
pub fn tool_trade(a: Ability) -> Option<(&'static str, &'static str)> {
    Some(match a {
        Ability::MiningPick => (
            "Mining",
            "Strike an ore vein two to four times, by your skill, to mine it.",
        ),
        Ability::SkinningKnife => (
            "Skinning",
            "Strike a skinnable corpse two to four times, by your skill, to skin it.",
        ),
        Ability::Herbalism => (
            "Herbalism",
            "Work a herb two to four times, by your skill, to gather it.",
        ),
        Ability::FishingPole => (
            "Fishing",
            "Strike to cast the line out ahead; strike again to reel it in.",
        ),
        Ability::WoodcuttingAxe => (
            "Woodcutting",
            "Strike a lumber tree two to four times, by your skill, to fell its wood.",
        ),
        Ability::BlacksmithHammer => (
            "Blacksmithing",
            "Strike an anvil or a forge to work Blacksmithing there.",
        ),
        _ => return None,
    })
}

/// An ability's tooltip. `key` is the key that uses it, none when it is on no bar.
pub fn tip(a: Ability, c: &Context, key: Option<&str>) -> Vec<LayerTipLine> {
    let mut out = Vec::new();
    let mut name = a.name().to_owned();
    let class = c.class;
    match a {
        Ability::Primary | Ability::Secondary => {
            let gun_id = class.map(|k| {
                if a == Ability::Primary {
                    k.primary
                } else {
                    k.secondary
                }
            });
            let atts: Vec<&str> = match (a, class) {
                (Ability::Primary, Some(k)) => crate::cac::primary_attachments(k),
                (_, Some(k)) => crate::cac::secondary_attachments(k),
                _ => Vec::new(),
            };
            let hand = if a == Ability::Primary { 0 } else { 1 };
            match gun_id.and_then(|g| c.catalog.gun(g)) {
                Some(gun) => {
                    name = gun_name(gun);
                    let ammo = (c.hand == hand).then_some((c.mag, c.reserve));
                    gun_lines(&mut out, c.catalog, gun, &atts, ammo);
                }
                None => out.push(text(
                    if a == Ability::Primary {
                        "Draws the primary weapon. Your class's own gun until a kit puts one in it."
                    } else {
                        "Draws the secondary weapon. The kit in hand has none."
                    },
                    GOLD,
                )),
            }
        }
        Ability::Underbarrel => {
            let alt = class.and_then(|k| {
                c.catalog
                    .alternate(k.primary, &crate::cac::primary_attachments(k))
            });
            match alt {
                Some(gun) => {
                    name = gun_name(gun);
                    let ammo = (c.hand == 2).then_some((c.mag, c.reserve));
                    gun_lines(&mut out, c.catalog, gun, &[], ammo);
                }
                None => out.push(text(
                    "Raises the primary's underbarrel launcher or shotgun. The primary carries none.",
                    GOLD,
                )),
            }
        }
        Ability::Killstreak => {
            out.push(double(
                "Instant",
                format!("{} ready", c.streaks_ready.len()),
            ));
            match c.streaks_ready.last() {
                Some(r) => out.push(line(
                    format!("Next: {}", crate::arsenal::streak_label(r)),
                    WHITE,
                )),
                None => out.push(line("No killstreak earned", GREY)),
            }
            if let Some(k) = class {
                let names: Vec<String> = k
                    .streaks
                    .iter()
                    .filter(|&&s| s != 0)
                    .filter_map(|&s| arsenal().streak(u32::from(s)).map(|l| l.name.clone()))
                    .collect();
                if !names.is_empty() {
                    out.push(line(format!("Kit: {}", names.join(", ")), GREEN));
                }
            }
            out.push(text(
                "Calls in the killstreak reward earned last. Kills in a row earn them; a death \
                 resets the count.",
                GOLD,
            ));
        }
        Ability::Lethal => {
            if let Some(n) = equipment_lines(
                &mut out,
                c.catalog,
                class.map(|k| k.lethal),
                c.lethal_left,
                "Throws the kit's lethal. The kit in hand has none.",
            ) {
                name = n;
            }
        }
        Ability::Tactical => {
            if let Some(n) = equipment_lines(
                &mut out,
                c.catalog,
                class.map(|k| k.tactical),
                c.tactical_left,
                "Throws the kit's tactical. The kit in hand has none.",
            ) {
                name = n;
            }
        }
        Ability::SpecialAmmo => {
            let rounds = crate::fusion::ROUNDS;
            let now = rounds.get(usize::from(c.round)).map_or("Basic", |r| r.0);
            out.push(line("Instant", WHITE));
            out.push(line(format!("Loaded: {now} rounds"), WHITE));
            let next = crate::fusion::next_round(c.round, c.level);
            if next != c.round {
                out.push(line(
                    format!("Next: {} rounds", rounds[usize::from(next)].0),
                    WHITE,
                ));
            }
            for (round, lvl) in rounds.iter().skip(1) {
                if c.level < *lvl {
                    out.push(line(format!("{round} rounds: level {lvl}"), GREY));
                }
            }
            out.push(text(
                "Loads the next special round the Armory has unlocked for you.",
                GOLD,
            ));
        }
        Ability::Rearm => {
            out.push(line("Instant", WHITE));
            out.push(text(
                "Refills your ammunition and equipment, free, while resting in a city or an inn \
                 (special rounds for a small fee). With the Field Quartermaster perk, anywhere \
                 every 10 minutes.",
                GOLD,
            ));
        }
        Ability::BareHands => {
            out.push(double("Instant", "Melee range"));
            out.push(text(
                "Your own hands, relaxed and out of a fight. Equip them to raise your fists: the                  attack button punches. Press again to lower them. Hands pick herbs.",
                GOLD,
            ));
        }
        a if a.is_instrument() => {
            let def = crate::minstrel::rules::def_of_tool(a.id());
            out.push(line("Instant", WHITE));
            if let Some(d) = def {
                let color = if c.minstrelsy >= d.skill { WHITE } else { RED };
                out.push(line(format!("Requires Minstrelsy ({})", d.skill), color));
                out.push(line(
                    format!("{} lanes, {} beats a minute", d.lanes, d.bpm),
                    WHITE,
                ));
            }
            out.push(text(
                "Plays the song picked; press its key again to pick the next song. The slot keys \
                 play the notes.",
                GOLD,
            ));
        }
        Ability::BuildStructure => {
            out.push(line("Instant", WHITE));
            out.push(text(
                "Enters build mode. Placing a piece uses one of its kits, crafted in the \
                 Construction window.",
                GOLD,
            ));
        }
        Ability::RepairHammer => {
            out.push(double("Instant", "Melee range"));
            out.push(text(
                "Mends a damaged built piece a tenth a strike, paid with its own materials. \
                 Aimed at a piece, it shows its health and decay.",
                GOLD,
            ));
        }
        // The live tooltip is the companion's (crate::companion_bar); this stands in before it.
        a if a.pet_slot().is_some() => {
            out.push(text(
                "One of your companion's slots on the Companion bar (C).",
                GOLD,
            ));
        }
        a => {
            out.push(double("Instant", "Melee range"));
            if let Some((trade, does)) = tool_trade(a) {
                out.push(line(trade, WHITE));
                out.push(text(does, GOLD));
            }
        }
    }
    if a.is_tool() && c.usable_tools.is_some_and(|m| m & tool_bit(a) == 0) {
        out.push(line("You lack this tool or its trade", RED));
    }
    match key {
        Some(k) => out.push(line(format!("Key: {k}"), GREY)),
        None => out.push(line("Not on your bars", GREY)),
    }
    out.insert(0, line(name, WHITE));
    out
}

/// Where an ability sits on its bars (the first found), as a Lua action id.
pub fn slot_of(bar: &FusionActionBar, a: Ability) -> Option<u32> {
    let mut found: Vec<u32> = a
        .systems()
        .iter()
        .flat_map(|s| s.actions())
        .filter(|act| bar.fusion_slots.get(act) == Some(&a.id()))
        .collect();
    found.sort_unstable();
    found.first().copied()
}

/// The key that uses an ability on its bar: its slot's key, with the page when not the first.
pub fn key_of(bar: &FusionActionBar, keymap: &Keymap, a: Ability) -> Option<String> {
    let action = slot_of(bar, a)?;
    let system = System::of_action(action)?;
    let (page, slot) = system.place_of(action)?;
    let key = keymap.label(SLOT_ACTIONS[slot]);
    Some(if page == 0 {
        format!("{key} ({})", system.name())
    } else {
        format!("{key} ({} page {})", system.name(), page + 1)
    })
}

/// Every ability's tooltip, as last built.
#[derive(Resource, Default, Debug, Clone)]
pub struct AbilityTips {
    pub tips: HashMap<u32, Vec<LayerTipLine>>,
    /// Each ability's slot (Lua action id) on its bar, none off them.
    pub slots: HashMap<u32, u32>,
}

/// Seconds between rebuilds (the ammo and counts move with play).
const REBUILD_SECS: f32 = 0.2;

pub struct AbilityTipsPlugin;

impl Plugin for AbilityTipsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<AbilityTips>().add_systems(
            Update,
            build_tips.after(crate::action_systems::ActionSystemsSet),
        );
    }
}

#[allow(clippy::too_many_arguments)]
fn build_tips(
    time: Res<Time>,
    catalog: Res<Catalog>,
    classes: Res<crate::cac::Classes>,
    hand: Res<crate::cac::Hand>,
    weapon: Res<crate::weapon::Weapon>,
    equip: Res<crate::equipment::EquipState>,
    streak: Res<crate::streaks::Streak>,
    fusion: Res<crate::fusion::Fusion>,
    page: Res<crate::classes::ClassPage>,
    systems: Res<ActionSystems>,
    minstrel: Res<crate::minstrel::Minstrel>,
    keymap: Res<Keymap>,
    companion: Res<crate::companion_bar::CompanionBarLooks>,
    bar: Option<ResMut<FusionActionBar>>,
    mut tips: ResMut<AbilityTips>,
    mut last: Local<Option<f32>>,
) {
    let Some(mut bar) = bar else { return };
    let t = time.elapsed_secs();
    if last.is_some_and(|l| t - l < REBUILD_SECS) {
        return;
    }
    *last = Some(t);
    let c = Context {
        catalog: &catalog,
        class: classes.equipped_class(),
        hand: hand.0,
        mag: weapon.mag,
        reserve: weapon.reserve.or(weapon.server_reserve),
        lethal_left: equip.lethal_left,
        tactical_left: equip.tactical_left,
        streaks_ready: &streak.ready,
        round: fusion.round,
        level: u32::from(page.level()).max(1),
        usable_tools: systems.usable_tools,
        minstrelsy: minstrel.skill.1,
    };
    let mut built = HashMap::new();
    let mut slots = HashMap::new();
    for a in Ability::ALL {
        let key = key_of(&bar, &keymap, a);
        // A Companion slot's tooltip is the companion's (crate::companion_bar), with its key.
        let lines = match companion.tips.get(&a.id()) {
            Some(lines) if a.pet_slot().is_some() => {
                let mut v = lines.clone();
                if let Some(k) = &key {
                    v.push(line(format!("Key: {k}"), GREY));
                }
                v
            }
            _ => tip(a, &c, key.as_deref()),
        };
        built.insert(a.id(), lines);
        if let Some(s) = slot_of(&bar, a) {
            slots.insert(a.id(), s);
        }
    }
    if bar.tips != built {
        bar.tips = built.clone();
    }
    if tips.tips != built || tips.slots != slots {
        tips.tips = built;
        tips.slots = slots;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn seed() -> Catalog {
        Catalog::parse(include_str!(env!("AW_FUSION_CATALOG"))).unwrap()
    }

    fn kit(catalog: &Catalog) -> ClassLoadout {
        // The first primary with a grenade launcher, the first secondary, a lethal and a tactical.
        let primary = catalog
            .weapons
            .iter()
            .find(|g| {
                catalog
                    .alternate(g.id, &["gl"])
                    .is_some_and(|a| a.alternate_of == Some(g.id))
            })
            .expect("a gun takes a launcher");
        let of = |slot: crate::catalog::Slot| {
            catalog
                .weapons
                .iter()
                .find(|g| Catalog::slot(g) == slot)
                .map_or(0, |g| g.id)
        };
        ClassLoadout {
            primary: primary.id,
            primary_att1: "gl".into(),
            secondary: of(crate::catalog::Slot::Secondary),
            lethal: of(crate::catalog::Slot::Lethal),
            tactical: of(crate::catalog::Slot::Tactical),
            streaks: [1, 0, 0],
            ..Default::default()
        }
    }

    fn ctx<'a>(catalog: &'a Catalog, class: Option<&'a ClassLoadout>) -> Context<'a> {
        Context {
            catalog,
            class,
            hand: 0,
            mag: 17,
            reserve: Some(90),
            lethal_left: 1,
            tactical_left: 2,
            streaks_ready: &[],
            round: 0,
            level: 60,
            usable_tools: Some(tool_bit(Ability::MiningPick)),
            minstrelsy: 0,
        }
    }

    fn texts(lines: &[LayerTipLine]) -> Vec<String> {
        lines
            .iter()
            .map(|l| match &l.right {
                Some((r, _)) => format!("{} | {r}", l.left),
                None => l.left.clone(),
            })
            .collect()
    }

    #[test]
    fn every_button_has_a_name_a_description_and_its_key() {
        let catalog = seed();
        let k = kit(&catalog);
        let c = ctx(&catalog, Some(&k));
        for a in Ability::ALL {
            let lines = tip(a, &c, Some("Q (Modern)"));
            assert!(lines.len() >= 3, "{a:?}: {:?}", texts(&lines));
            assert_eq!(lines[0].left_color, WHITE, "{a:?}: the name in white");
            assert!(!lines[0].left.is_empty(), "{a:?}");
            assert!(
                !lines.iter().any(|l| l.left.starts_with("Rank")
                    || l.right.as_ref().is_some_and(|r| r.0.starts_with("Rank"))),
                "{a:?}: rank-less"
            );
            assert!(
                lines.iter().any(|l| l.left_color == GOLD && l.wrap),
                "{a:?}: what it does, {:?}",
                texts(&lines)
            );
            assert_eq!(lines.last().unwrap().left, "Key: Q (Modern)", "{a:?}");
        }
        let off = tip(Ability::Rearm, &c, None);
        assert_eq!(off.last().unwrap().left, "Not on your bars");
    }

    #[test]
    fn a_gun_shows_its_world_name_stats_attachments_and_ammo_in_hand() {
        let catalog = seed();
        let k = kit(&catalog);
        let c = ctx(&catalog, Some(&k));
        let gun = catalog.gun(k.primary).unwrap();
        let lines = tip(Ability::Primary, &c, None);
        let t = texts(&lines);
        assert_eq!(
            t[0],
            gun_name(gun),
            "the arsenal's name, not Primary Weapon"
        );
        assert_eq!(t[1], "Instant | 150 yd range");
        assert!(t.contains(&"Ammo: 17 / 90".to_owned()), "{t:?}");
        let (mag, fire) = catalog.mag_and_fire(gun, &["gl"]);
        assert!(
            t.iter()
                .any(|l| l.contains(&format!("{} rounds a minute", rounds_per_minute(fire)))),
            "{t:?}"
        );
        assert!(
            t.iter().any(|l| l.starts_with(&format!("Magazine: {mag}"))),
            "{t:?}"
        );
        assert!(t.iter().any(|l| l.contains("damage")), "{t:?}");
        let att = lines
            .iter()
            .find(|l| l.left_color == GREEN)
            .expect("the attachment line");
        assert_eq!(
            att.left,
            format!("Underbarrel: {}", attachment_name(&catalog, "gl"))
        );
        // The secondary is not in hand: no ammo line.
        let sec = texts(&tip(Ability::Secondary, &c, None));
        assert!(!sec.iter().any(|l| l.starts_with("Ammo:")), "{sec:?}");
        // The underbarrel is the launcher the attachment carries.
        let alt = catalog.alternate(k.primary, &["gl"]).unwrap();
        assert_eq!(
            texts(&tip(Ability::Underbarrel, &c, None))[0],
            gun_name(alt)
        );
    }

    #[test]
    fn equipment_shows_what_is_left_and_streaks_what_is_ready() {
        let catalog = seed();
        let k = kit(&catalog);
        let mut c = ctx(&catalog, Some(&k));
        let lethal = texts(&tip(Ability::Lethal, &c, None));
        assert_eq!(lethal[0], gun_name(catalog.gun(k.lethal).unwrap()));
        assert_eq!(lethal[1], "Thrown | 1 left");
        assert_eq!(
            texts(&tip(Ability::Tactical, &c, None))[1],
            "Thrown | 2 left"
        );
        let none = texts(&tip(Ability::Killstreak, &c, None));
        assert_eq!(none[1], "Instant | 0 ready");
        assert!(none.contains(&"No killstreak earned".to_owned()));
        let ready = ["UAV"];
        c.streaks_ready = &ready;
        let one = texts(&tip(Ability::Killstreak, &c, None));
        assert_eq!(one[1], "Instant | 1 ready");
        assert!(one.iter().any(|l| l.starts_with("Next: ")), "{one:?}");
        // No kit: plain names and what the button would do.
        let bare = ctx(&catalog, None);
        assert_eq!(
            texts(&tip(Ability::Lethal, &bare, None))[0],
            "Lethal Grenade"
        );
    }

    #[test]
    fn special_ammo_names_the_loaded_round_and_the_locked_ones() {
        let catalog = seed();
        let mut c = ctx(&catalog, None);
        c.level = 10;
        let t = texts(&tip(Ability::SpecialAmmo, &c, None));
        assert!(t.contains(&"Loaded: Basic rounds".to_owned()), "{t:?}");
        assert!(t.iter().any(|l| l.starts_with("Next: ")), "{t:?}");
        assert!(t.contains(&"Blessed rounds: level 30".to_owned()), "{t:?}");
    }

    #[test]
    fn tools_show_their_trade_and_whether_they_can_be_used() {
        let catalog = seed();
        let c = ctx(&catalog, None);
        let pick = tip(Ability::MiningPick, &c, Some("1 (Utility)"));
        let t = texts(&pick);
        assert_eq!(t[0], "Mining Pick");
        assert_eq!(t[1], "Instant | Melee range");
        assert_eq!(t[2], "Mining");
        assert!(!pick.iter().any(|l| l.left_color == RED), "usable");
        let fish = tip(Ability::FishingPole, &c, None);
        assert!(texts(&fish).contains(&"Fishing".to_owned()));
        assert!(fish.iter().any(|l| l.left_color == RED), "not in the mask");
        let drum = tip(Ability::HandDrum, &c, None);
        let d = crate::minstrel::rules::def_of_tool(Ability::HandDrum.id()).unwrap();
        let req = drum
            .iter()
            .find(|l| l.left == format!("Requires Minstrelsy ({})", d.skill))
            .expect("the skill asked");
        assert_eq!(req.left_color, if d.skill == 0 { WHITE } else { RED });
        assert_eq!(texts(&tip(Ability::BareHands, &c, None))[0], "Hands");
    }

    #[test]
    fn the_key_names_the_slot_and_its_page() {
        let mut bar = FusionActionBar::default();
        let keymap = Keymap::default();
        assert_eq!(key_of(&bar, &keymap, Ability::Rearm), None);
        bar.fusion_slots
            .insert(System::Modern.first(1) + 1, Ability::Rearm.id());
        bar.fusion_slots
            .insert(System::Utility.first(0), Ability::BareHands.id());
        assert_eq!(slot_of(&bar, Ability::Rearm), Some(200));
        let k = key_of(&bar, &keymap, Ability::Rearm).unwrap();
        assert!(k.ends_with("(Modern page 2)"), "{k}");
        let b = key_of(&bar, &keymap, Ability::BareHands).unwrap();
        assert!(b.ends_with("(Utility)"), "{b}");
    }
}
