//! Azeroth Warfare's class framework refits the class spells on the server (no stances, no
//! reagents, its own cooldowns and cast times, some cast on oneself); the client gates, greys and
//! times them from its own `Spell.dbc` copy ([`crate::ui_action::Spells`]), so the server sends the
//! refitted values on fusion channel 7, op 7 (`Fusion::Classes::EncodeSpellTweaks`), and this puts
//! them into that copy. Tweaks that arrive before the spell table loads wait for it.
//!
//! The druid forms come the same way, op 8 (`Fusion::DruidForms::EncodeForms`): the new forms are
//! donor spells the server rewrote into shapeshifts on empty `SpellShapeshiftForm.dbc` rows, and
//! every form has its own bar page, past the four 1.12 has. Each row makes its spell a form spell
//! (the stance bar admits it, the form byte lights it, a recast cancels it) and gives its form row
//! the bonus bar `GetBonusBarOffset` returns.

use bevy::prelude::*;

use crate::fusion::FusionChannelReceived;

/// The class framework's channel and the spell-tweak op (`Fusion::Classes`).
pub const CHANNEL_CLASSES: u8 = 7;
pub const OP_SPELL_TWEAKS: u8 = 7;
/// The druid forms (`Fusion::DruidForms::kOpForms`).
pub const OP_FORMS: u8 = 8;

pub const TWEAK_NO_REAGENTS: u8 = 0x01;
pub const TWEAK_NO_COST: u8 = 0x02;
pub const TWEAK_ON_SELF: u8 = 0x04;
pub const TWEAK_NO_ITEM: u8 = 0x08;
/// No creature type limit (Exorcism on anything, a donor cast on oneself).
pub const TWEAK_ANY_CREATURE: u8 = 0x10;
/// No caster or target aura state (Judgement without a seal).
pub const TWEAK_NO_AURA_STATE: u8 = 0x20;
/// No spell focus object nearby (a donor that wanted one).
pub const TWEAK_NO_FOCUS: u8 = 0x40;
/// One entry per ability: a class spell's ranks are one entry that grows with the level, so its
/// "Rank N" goes (the book, the bar's tooltip and `/cast Name(Rank N)` forgive it).
pub const TWEAK_NO_RANK: u8 = 0x80;

/// One refitted spell (`Fusion::Classes::SpellTweak`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct SpellTweak {
    pub spell: u32,
    pub attributes: u32,
    pub attributes_ex: u32,
    pub stances: u32,
    pub recovery_ms: u32,
    pub category_recovery_ms: u32,
    pub cast_index: u16,
    pub range_index: u16,
    pub flags: u8,
}

const TWEAK_BYTES: usize = 29;

/// `[7, n u16, n x 29 bytes]`, little-endian.
pub fn decode_spell_tweaks(b: &[u8]) -> Option<Vec<SpellTweak>> {
    if b.first() != Some(&OP_SPELL_TWEAKS) || b.len() < 3 {
        return None;
    }
    let n = usize::from(u16::from_le_bytes([b[1], b[2]]));
    if b.len() != 3 + n * TWEAK_BYTES {
        return None;
    }
    let u32_at = |o: usize| u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]]);
    let u16_at = |o: usize| u16::from_le_bytes([b[o], b[o + 1]]);
    Some(
        (0..n)
            .map(|i| {
                let o = 3 + i * TWEAK_BYTES;
                SpellTweak {
                    spell: u32_at(o),
                    attributes: u32_at(o + 4),
                    attributes_ex: u32_at(o + 8),
                    stances: u32_at(o + 12),
                    recovery_ms: u32_at(o + 16),
                    category_recovery_ms: u32_at(o + 20),
                    cast_index: u16_at(o + 24),
                    range_index: u16_at(o + 26),
                    flags: b[o + 28],
                }
            })
            .collect(),
    )
}

/// Refit one spell's row as the server did.
pub fn apply_tweak(d: &mut benilla_formats::SpellDisplay, t: &SpellTweak) {
    d.attributes = t.attributes;
    d.attributes_ex = t.attributes_ex;
    d.passive = t.attributes & 0x40 != 0;
    d.stances = t.stances;
    d.stances_not = 0;
    d.recovery_ms = t.recovery_ms;
    d.category_recovery_ms = t.category_recovery_ms;
    d.casting_time_index = u32::from(t.cast_index);
    d.range_index = u32::from(t.range_index);
    if t.flags & TWEAK_NO_REAGENTS != 0 {
        d.reagents = Default::default();
        d.totems = [0; 2];
    }
    if t.flags & TWEAK_NO_COST != 0 {
        d.mana_cost = 0;
        d.mana_cost_pct = 0;
        d.mana_cost_per_level = 0;
        d.mana_per_second = 0;
    }
    if t.flags & TWEAK_ON_SELF != 0 {
        d.targets = 0;
        // The cast arm's own copy of the first effect's target (`implicit_target_a1`), which
        // `cast_target_mask` builds the targeting word from: left at an enemy's 6, a self refit
        // still says "You have no target".
        if d.effects[0] != 0 {
            d.implicit_target_a1 = 1;
        }
        for i in 0..3 {
            if d.effects[i] != 0 {
                d.effect_implicit_target_a[i] = 1;
                d.effect_implicit_target_b[i] = 0;
            }
        }
    }
    if t.flags & TWEAK_NO_ITEM != 0 {
        d.equipped_item_class = -1;
        d.equipped_item_subclass_mask = 0;
        d.equipped_item_inventory_type_mask = 0;
    }
    if t.flags & TWEAK_ANY_CREATURE != 0 {
        d.target_creature_type = 0;
    }
    if t.flags & TWEAK_NO_AURA_STATE != 0 {
        d.caster_aura_state = 0;
        d.target_aura_state = 0;
    }
    if t.flags & TWEAK_NO_FOCUS != 0 {
        d.requires_spell_focus = 0;
    }
    if t.flags & TWEAK_NO_RANK != 0 {
        d.rank = None;
    }
}

/// One druid form as the server runs it (`Fusion::DruidForms::WireForm`).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct FormRow {
    pub spell: u32,
    pub form: u8,
    pub bonus_bar: u8,
    pub stance_bar_order: i8,
    /// `SpellShapeshiftForm.dbc` `flags1` for a row 1.12 ships empty.
    pub form_flags: u8,
    pub creature_type: i8,
    pub name: String,
}

/// `[8, n u8, n x (spell u32, form u8, bonus_bar u8, order i8, flags u8, creature_type i8,
/// name: len u8, bytes)]`, little-endian.
pub fn decode_forms(b: &[u8]) -> Option<Vec<FormRow>> {
    if b.first() != Some(&OP_FORMS) || b.len() < 2 {
        return None;
    }
    let n = usize::from(b[1]);
    let mut at = 2;
    let mut out = Vec::with_capacity(n);
    for _ in 0..n {
        let row = b.get(at..at + 10)?;
        let len = usize::from(row[9]);
        let name = b.get(at + 10..at + 10 + len)?;
        out.push(FormRow {
            spell: u32::from_le_bytes([row[0], row[1], row[2], row[3]]),
            form: row[4],
            bonus_bar: row[5],
            stance_bar_order: row[6] as i8,
            form_flags: row[7],
            creature_type: row[8] as i8,
            name: String::from_utf8_lossy(name).into_owned(),
        });
        at += 10 + len;
    }
    (at == b.len()).then_some(out)
}

/// `SPELL_EFFECT_APPLY_AURA` and `SPELL_AURA_MOD_SHAPESHIFT`.
const EFFECT_APPLY_AURA: u32 = 6;
const AURA_MOD_SHAPESHIFT: u32 = 36;
/// The stock forms' `ActiveIconID`: a lit form button, and a second press cancels.
const ACTIVE_ICON_FORM: u32 = 122;
/// `TARGET_SELF` (implicit target 1) and `SpellRange.dbc` row 1, "Self Only".
const TARGET_SELF: u32 = 1;
const RANGE_SELF: u32 = 1;

/// Make one spell the form spell the server runs, and give its form row the page. A row 1.12
/// ships empty (no name) takes the server's name, flags and creature type; a stock row keeps its
/// own. `active_icon` is a stock form's lit texture, for a donor that had none.
pub fn apply_form(
    catalog: &mut benilla_formats::SpellCatalog,
    forms: &mut std::collections::HashMap<u32, benilla_formats::ShapeshiftForm>,
    f: &FormRow,
    active_icon: Option<&str>,
) {
    if let Some(d) = catalog.get_mut(f.spell) {
        let form = u32::from(f.form);
        if d.shapeshift_form != Some(form) {
            // The donor's first effect is the form now (its transform, charm or stun gone), cast
            // on oneself with no target (Arugal's Curse wanted an enemy), under the form's name;
            // the donor's own text described the donor.
            d.effects = [EFFECT_APPLY_AURA, 0, 0];
            d.effect_apply_aura = [AURA_MOD_SHAPESHIFT, 0, 0];
            d.effect_misc_value = [i32::from(f.form), 0, 0];
            d.effect_implicit_target_a = [TARGET_SELF, 0, 0];
            d.effect_implicit_target_b = [0; 3];
            // The cast arm builds its targeting word from this column of its own
            // (`crate::spell::cast_target::cast_target_mask`): an enemy's 6 asks for a target.
            d.implicit_target_a1 = TARGET_SELF;
            d.targets = 0;
            d.range_index = RANGE_SELF;
            d.shapeshift_form = Some(form);
            d.rank = None;
            d.description = None;
            d.aura_description = None;
        }
        if !f.name.is_empty() {
            d.name = f.name.clone();
        }
        d.stance_bar_order = i32::from(f.stance_bar_order);
        // `AttributesEx2 & 0x2` would keep it off the stance bar.
        d.attributes_ex2 &= !0x2;
        if d.active_icon_id == 0 {
            d.active_icon_id = ACTIVE_ICON_FORM;
        }
        if d.active_icon.is_none() {
            d.active_icon = active_icon.map(str::to_string);
        }
    }
    let row = forms.entry(u32::from(f.form)).or_default();
    if row.name.is_empty() {
        row.name = f.name.clone();
        row.flags = u32::from(f.form_flags);
        row.creature_type = i32::from(f.creature_type);
    }
    row.bonus_bar = u32::from(f.bonus_bar);
}

/// The tweaks not yet put into the spell table.
#[derive(Resource, Default)]
pub(crate) struct PendingSpellTweaks(Vec<SpellTweak>);

/// The druid forms not yet put into the spell table.
#[derive(Resource, Default)]
pub(crate) struct PendingForms(Vec<FormRow>);

pub(crate) fn apply_forms(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut pending: ResMut<PendingForms>,
    spells: Option<ResMut<crate::ui_action::Spells>>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel == CHANNEL_CLASSES {
            if let Some(f) = decode_forms(&m.body) {
                pending.0.extend(f);
            }
        }
    }
    let Some(mut spells) = spells else {
        return;
    };
    if pending.0.is_empty() {
        return;
    }
    // Cat Form's lit texture, for a donor that never had one.
    let lit = spells.catalog.get(768).and_then(|d| d.active_icon.clone());
    let spells = &mut *spells;
    let n = pending.0.len();
    for f in pending.0.drain(..) {
        apply_form(&mut spells.catalog, &mut spells.forms, &f, lit.as_deref());
    }
    info!("fusion: {n} druid forms as the server runs them");
}

pub(crate) fn apply_spell_tweaks(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut pending: ResMut<PendingSpellTweaks>,
    spells: Option<ResMut<crate::ui_action::Spells>>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel == CHANNEL_CLASSES {
            if let Some(t) = decode_spell_tweaks(&m.body) {
                pending.0.extend(t);
            }
        }
    }
    let Some(mut spells) = spells else {
        return;
    };
    if pending.0.is_empty() {
        return;
    }
    let mut applied = 0;
    for t in pending.0.drain(..) {
        if let Some(d) = spells.catalog.get_mut(t.spell) {
            apply_tweak(d, &t);
            applied += 1;
        }
    }
    info!("fusion: {applied} class spells refitted by the server");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tweaks_decode_as_the_server_writes_them() {
        let mut b = vec![OP_SPELL_TWEAKS, 1, 0];
        for v in [11578u32, 0x0005_0010, 1024, 0, 15000, 0] {
            b.extend_from_slice(&v.to_le_bytes());
        }
        b.extend_from_slice(&1u16.to_le_bytes());
        b.extend_from_slice(&2u16.to_le_bytes());
        b.push(TWEAK_NO_COST | TWEAK_ON_SELF);
        let t = decode_spell_tweaks(&b).expect("tweaks");
        assert_eq!(t.len(), 1);
        assert_eq!(t[0].spell, 11578);
        assert_eq!(t[0].attributes_ex, 1024);
        assert_eq!((t[0].recovery_ms, t[0].cast_index, t[0].range_index), (15000, 1, 2));
        assert_eq!(t[0].flags, TWEAK_NO_COST | TWEAK_ON_SELF);
        b.pop();
        assert!(decode_spell_tweaks(&b).is_none());
    }

    #[test]
    fn a_ranked_class_spell_loses_its_rank_name() {
        let mut d = benilla_formats::SpellDisplay {
            rank: Some("Rank 4".into()),
            ..Default::default()
        };
        apply_tweak(
            &mut d,
            &SpellTweak {
                spell: 3140,
                flags: TWEAK_NO_RANK,
                ..Default::default()
            },
        );
        assert_eq!(d.rank, None);
        let mut kept = benilla_formats::SpellDisplay {
            rank: Some("Racial".into()),
            ..Default::default()
        };
        apply_tweak(&mut kept, &SpellTweak::default());
        assert_eq!(kept.rank.as_deref(), Some("Racial"));
    }

    /// The bytes `Fusion::DruidForms::EncodeForms` writes for Spider Form.
    fn spider_bytes() -> Vec<u8> {
        let mut b = vec![OP_FORMS, 1];
        b.extend_from_slice(&24084u32.to_le_bytes());
        b.extend_from_slice(&[11, 9, 7, 0x50, 1]);
        let name = b"Spider Form";
        b.push(name.len() as u8);
        b.extend_from_slice(name);
        b
    }

    #[test]
    fn forms_decode_as_the_server_writes_them() {
        let b = spider_bytes();
        let f = decode_forms(&b).expect("forms");
        assert_eq!(
            f,
            vec![FormRow {
                spell: 24084,
                form: 11,
                bonus_bar: 9,
                stance_bar_order: 7,
                form_flags: 0x50,
                creature_type: 1,
                name: "Spider Form".into(),
            }]
        );
        assert!(decode_forms(&b[..b.len() - 1]).is_none());
        assert!(decode_spell_tweaks(&b).is_none(), "op 8 is not op 7");
    }

    #[test]
    fn a_form_row_makes_its_donor_a_form_spell_with_its_own_page() {
        use benilla_formats::{ShapeshiftForm, SpellCatalog, SpellDisplay};
        use std::collections::HashMap;
        // Mar'li Transform as 1.12 ships it: a transform, no active icon; aimed, as Arugal's
        // Curse is (implicit target 6, a 30 yd range), to prove the form needs no target.
        let marli = SpellDisplay {
            name: "Mar'li Transform".into(),
            description: Some("Transforms Mar'li into a spider.".into()),
            effects: [6, 0, 0],
            effect_apply_aura: [56, 0, 0],
            effect_misc_value: [14967, 0, 0],
            effect_implicit_target_a: [6, 0, 0],
            targets: 2,
            range_index: 4,
            ..Default::default()
        };
        let cat = SpellDisplay {
            shapeshift_form: Some(1),
            active_icon_id: 122,
            ..Default::default()
        };
        let mut catalog = SpellCatalog::from_displays(HashMap::from([(24084, marli), (768, cat)]));
        let mut forms: HashMap<u32, ShapeshiftForm> = HashMap::from([
            (
                1,
                ShapeshiftForm {
                    bonus_bar: 1,
                    name: "Cat Form".into(),
                    flags: 0x70,
                    creature_type: 1,
                    attack_icon: None,
                },
            ),
            (11, ShapeshiftForm::default()),
        ]);
        let f = decode_forms(&spider_bytes()).unwrap().remove(0);
        apply_form(&mut catalog, &mut forms, &f, Some("Interface\\Icons\\Lit"));
        let d = catalog.get(24084).unwrap();
        assert_eq!(d.shapeshift_form, Some(11));
        assert_eq!(d.effect_apply_aura[0], 36);
        assert_eq!(d.active_icon_id, 122, "a second press cancels");
        assert_eq!(d.stance_bar_order, 7);
        // The class option's name on the stance bar and in the tooltip, not the donor's.
        assert_eq!(d.name, "Spider Form");
        assert_eq!(d.description, None);
        // Cast on oneself: no target wanted ("You have no target" was Arugal's Curse's).
        assert_eq!(d.effect_implicit_target_a, [1, 0, 0]);
        assert_eq!((d.targets, d.range_index), (0, 1));
        let row = &forms[&11];
        assert_eq!(row.bonus_bar, 9);
        assert_eq!(row.name, "Spider Form");
        assert!(row.cancelable() && !row.is_stance());
        // A stock row keeps its own flags and name; only its page is the server's.
        let cat_row = FormRow {
            spell: 768,
            form: 1,
            bonus_bar: 1,
            name: "ignored".into(),
            ..Default::default()
        };
        apply_form(&mut catalog, &mut forms, &cat_row, None);
        assert_eq!(forms[&1].flags, 0x70);
        assert_eq!(forms[&1].name, "Cat Form");
    }

    #[test]
    fn a_refit_lifts_the_gates_the_server_lifted() {
        // Judgement without a seal, Exorcism on anything, a donor that wanted a spell focus.
        let mut d = benilla_formats::SpellDisplay {
            caster_aura_state: 5,
            target_aura_state: 15,
            target_creature_type: 0x24,
            requires_spell_focus: 1346,
            ..Default::default()
        };
        let t = SpellTweak {
            spell: 20271,
            flags: TWEAK_ANY_CREATURE | TWEAK_NO_AURA_STATE | TWEAK_NO_FOCUS,
            ..Default::default()
        };
        apply_tweak(&mut d, &t);
        assert_eq!((d.caster_aura_state, d.target_aura_state), (0, 0));
        assert_eq!((d.target_creature_type, d.requires_spell_focus), (0, 0));
        // Without the flags they stay.
        let mut kept = benilla_formats::SpellDisplay {
            target_creature_type: 0x40,
            ..Default::default()
        };
        apply_tweak(&mut kept, &SpellTweak::default());
        assert_eq!(kept.target_creature_type, 0x40);
    }
}
