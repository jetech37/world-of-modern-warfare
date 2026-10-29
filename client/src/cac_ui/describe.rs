//! What the kit window's sub-menu says of an option: its name, what it does in words and in
//! numbers (next to the kit's current pick, better in green and worse in red), how it stacks and
//! what its Pro does (perks), where it comes from in the world, and what unlocks it.

use benilla_app::fusion::ClassLoadout;
use bevy::prelude::Color;

use super::parts::{Choice, Part, attachment_name, choice_name, current};
use crate::achievements::{Achievements, Unlocks};
use crate::catalog::{Catalog, Gun, Stack};
use crate::wow_ui::{DIM, GOLD, GREEN, GREY, WHITE};

/// The red the stock UI writes a shortfall in (`RED_FONT_COLOR`).
pub const WORSE: Color = Color::srgb(1.0, 0.13, 0.13);
/// A locked choice's red, softer than an error's.
pub const LOCKED: Color = Color::srgb(1.0, 0.35, 0.3);

/// One line of the details.
#[derive(Debug, Clone, PartialEq)]
pub struct Line {
    pub text: String,
    pub color: Color,
    pub size: f32,
}

impl Line {
    fn new(text: impl Into<String>, color: Color, size: f32) -> Self {
        Self {
            text: text.into(),
            color,
            size,
        }
    }
}

/// The sub-menu's details for an option.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Details {
    pub lines: Vec<Line>,
    /// A gun's stat bars: (this option's, the current pick's), 0 to 1, in [`super::STATS`]' order.
    pub bars: Option<[(f32, f32); 4]>,
}

/// What a part's "none" means.
fn none_text(part: Part) -> &'static str {
    match part {
        Part::Primary => "No primary: you carry your class's own gun.",
        Part::Secondary => "No secondary.",
        Part::Lethal | Part::Tactical => "No equipment in this slot.",
        Part::Camo => "The gun's own finish.",
        Part::PrimaryAtt(_) | Part::SecondaryAtt(_) => "No attachment in this slot.",
        Part::Perk(_) => "No perk in this slot.",
        Part::Deathstreak => "No deathstreak.",
        Part::Streak(_) => {
            "No killstreak in this slot. With none chosen at all, the kit plays every one you have unlocked."
        }
    }
}

/// A gun class as words.
fn class_words(class: &str) -> &'static str {
    match class {
        "assault_rifle" => "Assault rifle",
        "smg" => "Submachine gun",
        "lmg" => "Light machine gun",
        "sniper" => "Sniper rifle",
        "shotgun" => "Shotgun",
        "pistol" => "Pistol",
        "machine_pistol" => "Machine pistol",
        "launcher" => "Launcher",
        "riot_shield" => "Riot shield",
        "lethal" => "Lethal equipment",
        "tactical" => "Tactical equipment",
        _ => "Weapon",
    }
}

fn fire_words(fire_type: u8) -> &'static str {
    match fire_type {
        0 => "Automatic",
        1 => "Single shot",
        _ => "Burst",
    }
}

/// `new` against `cur`: a signed difference in green (better) or red (worse), "" when even.
fn delta(new: f32, cur: f32, higher_better: bool, unit: &str) -> (String, Color) {
    let d = new - cur;
    if d.abs() < 0.5 {
        return (String::new(), WHITE);
    }
    let better = (d > 0.0) == higher_better;
    let sign = if d > 0.0 { "+" } else { "-" };
    (
        format!("  ({sign}{:.0}{unit})", d.abs()),
        if better { GREEN } else { WORSE },
    )
}

/// A number line, compared with the current pick's when there is one.
fn stat_line(label: &str, value: String, cmp: Option<(f32, f32, bool, &str)>) -> Line {
    let (suffix, color) = cmp.map_or((String::new(), WHITE), |(n, c, hb, unit)| {
        delta(n, c, hb, unit)
    });
    Line::new(format!("{label}: {value}{suffix}"), color, 11.0)
}

fn rpm(fire_time: f32) -> f32 {
    60.0 / fire_time.max(0.01)
}

/// A gun's numbers against `cur` (the kit's current pick in the slot, if any).
fn gun_lines(gun: &Gun, cur: Option<&Gun>, out: &mut Vec<Line>) {
    let pellets = gun.pellets.max(1);
    let dmg = if pellets > 1 {
        format!("{:.0}-{:.0} x{pellets}", gun.damage, gun.min_damage)
    } else {
        format!("{:.0}-{:.0}", gun.damage, gun.min_damage)
    };
    let total = |g: &Gun| g.damage * g.pellets.max(1) as f32;
    out.push(stat_line(
        "Damage",
        dmg,
        cur.map(|c| (total(gun), total(c), true, "")),
    ));
    out.push(stat_line(
        "Full damage to",
        format!("{:.0} yd", gun.max_damage_range),
        cur.map(|c| (gun.max_damage_range, c.max_damage_range, true, " yd")),
    ));
    out.push(stat_line(
        "Fire rate",
        format!(
            "{:.0} a minute, {}",
            rpm(gun.fire_time),
            fire_words(gun.fire_type).to_lowercase()
        ),
        cur.map(|c| (rpm(gun.fire_time), rpm(c.fire_time), true, "")),
    ));
    let mag = if gun.max_reserve > 0 {
        format!("{} ({} spare)", gun.mag_size, gun.max_reserve)
    } else {
        gun.mag_size.to_string()
    };
    out.push(stat_line(
        "Magazine",
        mag,
        cur.map(|c| (gun.mag_size as f32, c.mag_size as f32, true, "")),
    ));
    out.push(stat_line(
        "Mobility",
        format!("{:.0}%", gun.move_speed_scale * 100.0),
        cur.map(|c| {
            (
                gun.move_speed_scale * 100.0,
                c.move_speed_scale * 100.0,
                true,
                "%",
            )
        }),
    ));
}

/// What an attachment changes on a gun, in MW2's numbers (its own combination on the bare gun).
fn attachment_lines(catalog: &Catalog, gun: &Gun, id: &str, out: &mut Vec<Line>) {
    let stats = catalog.attachment_stats(gun, &[id]);
    let akimbo = id == "akimbo";
    let mut changed = false;
    if let Some(&m) = stats.get("mag_size") {
        out.push(stat_line(
            "Magazine",
            format!("{} to {m:.0}", gun.mag_size),
            Some((m, gun.mag_size as f32, true, "")),
        ));
        changed = true;
    } else if akimbo {
        out.push(Line::new(
            format!("Magazine: {} to {}", gun.mag_size, gun.mag_size * 2),
            GREEN,
            11.0,
        ));
        changed = true;
    }
    if let Some(&f) = stats.get("fire_time") {
        let (n, c) = (rpm(f), rpm(gun.fire_time));
        out.push(stat_line(
            "Fire rate",
            format!("{c:.0} to {n:.0} a minute"),
            Some((n, c, true, "")),
        ));
        changed = true;
    } else if akimbo {
        out.push(Line::new(
            format!(
                "Fire rate: {:.0} to {:.0} a minute",
                rpm(gun.fire_time),
                rpm(gun.fire_time) * 2.0
            ),
            GREEN,
            11.0,
        ));
        changed = true;
    }
    if let Some(&r) = stats.get("max_damage_range") {
        out.push(stat_line(
            "Full damage to",
            format!("{:.0} to {r:.0} yd", gun.max_damage_range),
            Some((r, gun.max_damage_range, true, " yd")),
        ));
        changed = true;
    }
    if let Some(&d) = stats.get("damage") {
        out.push(stat_line(
            "Damage",
            format!("{:.0} to {d:.0}", gun.damage),
            Some((d, gun.damage, true, "")),
        ));
        changed = true;
    }
    if !changed {
        out.push(Line::new("No change to the gun's numbers.", DIM, 11.0));
    }
    // What it never goes with on this gun.
    let never: Vec<String> = catalog
        .attachment(id)
        .map(|a| {
            a.incompatible
                .iter()
                .filter(|x| gun.attachments.contains(x))
                .map(|x| attachment_name(catalog, x))
                .collect()
        })
        .unwrap_or_default();
    if !never.is_empty() {
        out.push(Line::new(
            format!("Not with: {}", never.join(", ")),
            DIM,
            10.0,
        ));
    }
    // Its kind: a gun carries one of each (MW2's Bling).
    let kind = crate::catalog::AttachmentKind::of(id);
    if kind != crate::catalog::AttachmentKind::Other {
        out.push(Line::new(
            format!("Kind: {} (one a gun)", kind.label()),
            DIM,
            10.0,
        ));
    }
}

/// The unlock line(s): unlocked, or the achievement that unlocks it and how far along.
pub fn unlock_lines(
    achievements: &Achievements,
    key: Option<(crate::achievements::UnlockKind, u32, u8, String)>,
    have: bool,
    journal_key: Option<&str>,
    out: &mut Vec<Line>,
) {
    let Some((kind, id, camo, att)) = key else {
        return;
    };
    if have {
        out.push(Line::new("Unlocked", GREEN, 11.0));
        return;
    }
    match achievements.unlocked_by(kind, id, camo, &att) {
        Some(row) => {
            out.push(Line::new(
                format!("Locked. Unlock: {}", row.name),
                LOCKED,
                11.0,
            ));
            if !row.description.is_empty() {
                out.push(Line::new(row.description.clone(), DIM, 10.0));
            }
            for r in &row.requirements {
                out.push(Line::new(
                    format!("Progress: {} / {}", r.current.min(r.needed), r.needed),
                    DIM,
                    10.0,
                ));
            }
        }
        None => out.push(Line::new(
            match journal_key {
                Some(k) => format!("Locked: earn it through achievements ({k})."),
                None => "Locked: earn it through achievements.".into(),
            },
            LOCKED,
            11.0,
        )),
    }
}

/// The context the details read beyond the catalog: the account's unlocks and achievements,
/// whether Hardline is in the kit (killstreak costs), the Achievements key's name.
pub struct Ctx<'a> {
    pub catalog: &'a Catalog,
    pub unlocks: &'a Unlocks,
    pub achievements: &'a Achievements,
    pub journal_key: Option<&'a str>,
}

/// The details of `choice` in `part` of `kit`.
pub fn details(ctx: &Ctx, kit: &ClassLoadout, part: Part, choice: &Choice) -> Details {
    let catalog = ctx.catalog;
    let lore = crate::arsenal::arsenal();
    let mut d = Details::default();
    let name = choice_name(catalog, part, choice);
    d.lines.push(Line::new(name, WHITE, 13.0));
    if choice.is_none() {
        d.lines.push(Line::new(none_text(part), DIM, 11.0));
        return d;
    }
    let have = super::parts::unlocked(ctx.unlocks, kit, part, choice);
    let cur = current(kit, part);
    match choice {
        Choice::Gun(id) => {
            let Some(gun) = catalog.gun(*id) else {
                return d;
            };
            let slot = match part {
                Part::Primary => ", primary",
                Part::Secondary => ", secondary",
                _ => "",
            };
            d.lines.push(Line::new(
                format!("{}{slot}", class_words(&gun.class)),
                GOLD,
                10.0,
            ));
            if !gun.effect.is_empty() {
                d.lines.push(Line::new(gun.effect.clone(), WHITE, 11.0));
            }
            if let Some(l) = lore.weapon(&gun.name) {
                d.lines.push(Line::new(l.origin.clone(), DIM, 10.0));
            }
            if matches!(part, Part::Primary | Part::Secondary) {
                let cur_gun = match &cur {
                    Choice::Gun(c) if *c != *id => catalog.gun(*c),
                    _ => None,
                };
                gun_lines(gun, cur_gun, &mut d.lines);
                let now = super::stat_bars(catalog, gun);
                let was = cur_gun.map_or(now, |c| super::stat_bars(catalog, c));
                d.bars = Some([0, 1, 2, 3].map(|i| (now[i], was[i])));
            } else {
                if gun.damage > 0.0 && gun.class == "lethal" {
                    d.lines.push(Line::new(
                        format!("Damage: up to {:.0}", gun.damage.max(gun.min_damage)),
                        WHITE,
                        11.0,
                    ));
                }
                d.lines
                    .push(Line::new(format!("Carried: {}", gun.mag_size), WHITE, 11.0));
            }
        }
        Choice::Attachment(a) => {
            d.lines.push(Line::new("Attachment", GOLD, 10.0));
            if let Some(att) = catalog.attachment(a)
                && !att.effect.is_empty()
            {
                d.lines.push(Line::new(att.effect.clone(), WHITE, 11.0));
            }
            if let Some(gun) = match part {
                Part::SecondaryAtt(_) => catalog.gun(kit.secondary),
                _ => catalog.gun(kit.primary),
            } {
                attachment_lines(catalog, gun, a, &mut d.lines);
            }
            if let Some(l) = lore.attachment(a) {
                d.lines.push(Line::new(l.origin.clone(), DIM, 10.0));
            }
        }
        Choice::Camo(c) => {
            d.lines.push(Line::new("Camo: looks only", GOLD, 10.0));
            if let Some(l) = catalog.camo(*c).and_then(|c| lore.camo(&c.suffix)) {
                d.lines.push(Line::new(l.origin.clone(), DIM, 10.0));
            }
        }
        Choice::Perk(p) => {
            let Some(perk) = catalog.perk(*p) else {
                return d;
            };
            if perk.tier == 0 {
                d.lines.push(Line::new(
                    format!("Deathstreak, after {} deaths without a kill", perk.deaths),
                    GOLD,
                    10.0,
                ));
            } else {
                d.lines.push(Line::new(
                    format!("Perk (MW2's {}, tier {})", perk.name, perk.tier),
                    GOLD,
                    10.0,
                ));
            }
            if !perk.effect.is_empty() {
                d.lines.push(Line::new(perk.effect.clone(), WHITE, 11.0));
            }
            if perk.has_pro() {
                let earned = ctx.unlocks.perk_pro(perk.id);
                d.lines.push(Line::new(
                    format!("Pro: {}", perk.pro_effect),
                    if earned { GREEN } else { GREY },
                    11.0,
                ));
                if !earned {
                    let progress = ctx
                        .achievements
                        .unlocked_by(
                            crate::achievements::UnlockKind::PerkPro,
                            u32::from(perk.id),
                            0,
                            "",
                        )
                        .and_then(|r| {
                            r.requirements
                                .first()
                                .map(|q| format!(" ({} / {})", q.current.min(q.needed), q.needed))
                        })
                        .unwrap_or_default();
                    d.lines.push(Line::new(
                        format!(
                            "Pro unlocks after {} kills with it in your kit{progress}.",
                            perk.pro_kills
                        ),
                        DIM,
                        10.0,
                    ));
                }
            }
            if perk.tier != 0 {
                let rule = match perk.stack {
                    Stack::None => "Stacking: none",
                    Stack::Additive => "Stacking: additive",
                    Stack::Diminishing => "Stacking: diminishing",
                    Stack::Special => "Stacking: special",
                };
                let held = crate::cac::perk_count(kit, perk.id);
                let note = if perk.stack == Stack::None {
                    String::new()
                } else {
                    format!(" (in {held} of {} slots it may fill)", perk.most_slots())
                };
                d.lines.push(Line::new(format!("{rule}{note}"), GOLD, 10.0));
                if !perk.stack_effect.is_empty() && perk.stack != Stack::None {
                    d.lines
                        .push(Line::new(perk.stack_effect.clone(), DIM, 10.0));
                }
            }
            if let Some(l) = lore.perk(&perk.icon) {
                d.lines.push(Line::new(l.origin.clone(), DIM, 10.0));
            }
        }
        Choice::Streak(s) => {
            let kills = super::streak_ladder()
                .into_iter()
                .find(|c| c.id == *s)
                .map_or(0, |c| {
                    let hardline = kit.perks.contains(&crate::catalog::perk_id::HARDLINE);
                    crate::streaks::reward_need(c.kills, hardline)
                });
            d.lines.push(Line::new(
                format!("Killstreak: {kills} kills in a row"),
                GOLD,
                10.0,
            ));
            if let Some(l) = lore.streak(u32::from(*s)) {
                d.lines.push(Line::new(l.origin.clone(), WHITE, 11.0));
            }
        }
    }
    unlock_lines(
        ctx.achievements,
        super::parts::unlock_key(kit, part, choice),
        have,
        ctx.journal_key,
        &mut d.lines,
    );
    d
}

/// The hover text of a part's row: its name, what the slot is for, the pick's effect line.
pub fn part_tip(ctx: &Ctx, kit: &ClassLoadout, part: Part) -> (String, String) {
    let choice = current(kit, part);
    let dets = details(ctx, kit, part, &choice);
    let effect = dets
        .lines
        .iter()
        .skip(1)
        .find(|l| l.color == WHITE)
        .map_or_else(|| part.blurb().to_owned(), |l| l.text.clone());
    (
        format!(
            "{}: {}",
            part.label(),
            choice_name(ctx.catalog, part, &choice)
        ),
        effect,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn catalog() -> Catalog {
        Catalog::parse(include_str!(env!("AW_FUSION_CATALOG"))).unwrap()
    }

    fn texts(d: &Details) -> String {
        d.lines
            .iter()
            .map(|l| l.text.as_str())
            .collect::<Vec<_>>()
            .join("\n")
    }

    #[test]
    fn every_option_of_every_part_explains_itself() {
        let c = catalog();
        let unlocks = Unlocks::default();
        let ach = Achievements::default();
        let ctx = Ctx {
            catalog: &c,
            unlocks: &unlocks,
            achievements: &ach,
            journal_key: Some("L"),
        };
        let kit = ClassLoadout {
            primary: c.gun_named("m4_mp").unwrap().id,
            secondary: c.gun_named("usp_mp").unwrap().id,
            ..Default::default()
        };
        for part in super::super::parts::PARTS {
            for choice in super::super::parts::options(&c, &kit, part) {
                let d = details(&ctx, &kit, part, &choice);
                assert!(d.lines.len() >= 2, "{part:?} {choice:?}: {}", texts(&d));
                if !choice.is_none() {
                    // Something past the name and its kind line: what it does or its numbers.
                    assert!(d.lines.len() >= 3, "{part:?} {choice:?}: {}", texts(&d));
                }
            }
        }
    }

    #[test]
    fn a_gun_compares_with_the_current_pick() {
        let c = catalog();
        let unlocks = Unlocks::default();
        let ach = Achievements::default();
        let ctx = Ctx {
            catalog: &c,
            unlocks: &unlocks,
            achievements: &ach,
            journal_key: None,
        };
        let kit = ClassLoadout {
            primary: c.gun_named("m4_mp").unwrap().id,
            ..Default::default()
        };
        let barrett = Choice::Gun(c.gun_named("barrett_mp").unwrap().id);
        let d = details(&ctx, &kit, Part::Primary, &barrett);
        let t = texts(&d);
        assert!(t.contains("Sniper rifle, primary"), "{t}");
        let range = d
            .lines
            .iter()
            .find(|l| l.text.starts_with("Full damage to"))
            .unwrap();
        assert_eq!(range.color, GREEN, "{}", range.text); // reaches farther than the M4
        let rate = d
            .lines
            .iter()
            .find(|l| l.text.starts_with("Fire rate"))
            .unwrap();
        assert_eq!(rate.color, WORSE); // and fires slower
        let bars = d.bars.unwrap();
        assert!(bars[1].0 > bars[1].1 && bars[2].0 < bars[2].1);
        assert!(t.contains("Unlocked"));
    }

    #[test]
    fn a_perk_says_its_pro_and_how_it_stacks() {
        let c = catalog();
        let unlocks = Unlocks {
            everything: false,
            held: Default::default(),
        };
        let ach = Achievements::default();
        let ctx = Ctx {
            catalog: &c,
            unlocks: &unlocks,
            achievements: &ach,
            journal_key: None,
        };
        let mut kit = ClassLoadout::default();
        kit.perks = [crate::catalog::perk_id::BLING; 3];
        let d = details(
            &ctx,
            &kit,
            Part::Perk(1),
            &Choice::Perk(crate::catalog::perk_id::BLING),
        );
        let t = texts(&d);
        assert!(t.contains("Tinker's Rails"), "{t}");
        assert!(
            t.contains("Pro: Your secondary takes a second attachment"),
            "{t}"
        );
        assert!(t.contains("Pro unlocks after 200 kills"), "{t}");
        assert!(t.contains("Stacking: special (in 3 of 3 slots"), "{t}");
        assert!(t.contains("up to four"), "{t}");
        assert!(t.contains("Locked"), "{t}");
        let sp = details(
            &ctx,
            &kit,
            Part::Perk(1),
            &Choice::Perk(crate::catalog::perk_id::STOPPING_POWER),
        );
        assert!(texts(&sp).contains("Stacking: none"));
    }

    #[test]
    fn an_attachment_shows_what_it_changes() {
        let c = catalog();
        let unlocks = Unlocks::default();
        let ach = Achievements::default();
        let ctx = Ctx {
            catalog: &c,
            unlocks: &unlocks,
            achievements: &ach,
            journal_key: None,
        };
        let kit = ClassLoadout {
            primary: c.gun_named("m4_mp").unwrap().id,
            ..Default::default()
        };
        let t = texts(&details(
            &ctx,
            &kit,
            Part::PrimaryAtt(0),
            &Choice::Attachment("xmags".into()),
        ));
        assert!(t.contains("Magazine: 30 to 45"), "{t}");
        let t = texts(&details(
            &ctx,
            &kit,
            Part::PrimaryAtt(0),
            &Choice::Attachment("reflex".into()),
        ));
        assert!(t.contains("Not with:"), "{t}");
        assert!(t.contains("Kind: Sight (one a gun)"), "{t}");
    }
}
