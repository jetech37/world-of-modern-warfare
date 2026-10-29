/*
 * World of Modern Warfare: the druid's shapeshift forms, stock and new, as one table
 * (design/CLASS_CATALOG.md section 9). GPL-2.0-or-later, see Common.h.
 *
 * Every form is a real shapeshift: a SPELL_AURA_MOD_SHAPESHIFT aura that sets the form byte
 * (UNIT_FIELD_BYTES_1 byte 2), so the stance bar shows it, its button and a recast leave it,
 * another form replaces it, the buff right-clicks off and death removes it. The new forms (Stag,
 * Crow, Spider, Worgen) take empty SpellShapeshiftForm.dbc rows (9..12) and a donor spell the
 * server rewrites in memory (FitFormSpell); their model comes from a creature template.
 *
 * Each form has its own action bar page, as cat and bear do in 1.12: the client's
 * GetBonusBarOffset is the form's bonus bar, and the bar shows action slots
 * 72 + 12 * (bonusBar - 1) onwards. 1.12 stops at 120 slots (four bonus pages); the forms need
 * more, so the action button array is kActionButtons long on both ends.
 */

#ifndef FUSION_DRUID_FORMS_H
#define FUSION_DRUID_FORMS_H

#include "fusion/Common.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Fusion::DruidForms
{
    // Shapeshift form ids: the form byte and the SpellShapeshiftForm.dbc row.
    constexpr uint8_t kCat = 1;
    constexpr uint8_t kTree = 2;        // no form here: Tree of Life borrows the treant's look only
    constexpr uint8_t kTravel = 3;
    constexpr uint8_t kAquatic = 4;
    constexpr uint8_t kBear = 5;
    constexpr uint8_t kDireBear = 8;
    constexpr uint8_t kStag = 9;        // an empty 1.12 row
    constexpr uint8_t kCrow = 10;       // an empty 1.12 row
    constexpr uint8_t kSpider = 11;     // an empty 1.12 row
    constexpr uint8_t kWorgen = 12;     // an empty 1.12 row
    constexpr uint8_t kMoonkin = 31;

    // ── The action bar ──
    constexpr uint8_t kSlotsPerPage = 12;
    constexpr uint8_t kMainSlots = 72;          // pages 1..6: the main bar's pages and the multibars
    constexpr uint8_t kMaxBonusBar = 12;        // 1.12 has 4
    constexpr uint16_t kActionButtons = kMainSlots + kSlotsPerPage * kMaxBonusBar;     // 216; 1.12 has 120
    // The first (0-based) action slot of a bonus bar page (1..kMaxBonusBar).
    constexpr uint16_t FirstSlotOfBar(uint8_t bonusBar)
    {
        return uint16_t(kMainSlots + kSlotsPerPage * (bonusBar - 1));
    }

    // What the first person shows of the form's front limbs (the creature model's own forelimb
    // bones and triangles, the client's crate::forms).
    enum class Limbs : uint8_t
    {
        None,
        Paws,
        Hooves,
        Talons,
        Legs,           // a spider's front legs
        Flippers,
        Claws,          // a moonkin's clawed wings
        ClawedHands,    // a worgen keeps hands, clawed
        Branches,       // a treant's arms
    };

    // The ways a form is left; every druid form has all of the stock ones.
    enum Leave : uint8_t
    {
        LEAVE_RECAST = 0x01,        // casting the form's spell again (the action bar's toggle)
        LEAVE_BUTTON = 0x02,        // clicking its stance bar button
        LEAVE_BUFF = 0x04,          // right-clicking its buff
        LEAVE_OTHER_FORM = 0x08,    // shifting into another form
        LEAVE_NON_FORM_SPELL = 0x10,// casting a spell that is not the form's (stock: never for druid forms)
        LEAVE_DEATH = 0x20,         // dying
        LEAVE_TIMED = 0x40,         // it runs out (no druid form does)
    };
    constexpr uint8_t kStockLeave = LEAVE_RECAST | LEAVE_BUTTON | LEAVE_BUFF | LEAVE_OTHER_FORM | LEAVE_DEATH;

    struct Form
    {
        uint8_t form = 0;
        uint32_t spell = 0;             // the spell that enters it (the catalog's, top rank)
        char const* name = "";
        bool stock = false;             // its own 1.12 SpellShapeshiftForm row and model
        bool fit = false;               // the server rewrites its spell (FitFormSpell)
        uint32_t creature = 0;          // a new form's model: this creature template's display
        uint32_t display = 0;           // the display it wears (Alliance's for a stock form)
        uint8_t bonusBar = 0;           // its action bar page (GetBonusBarOffset)
        int8_t stanceBarOrder = 0;
        Limbs limbs = Limbs::None;
        bool limbsAlways = false;       // shown at rest too (the worgen's hands), else only in action
        bool guns = false;
        bool heldWeapon = false;
        bool humanoidBody = false;
        uint8_t leave = kStockLeave;
        uint32_t durationMs = 0;        // 0: until left
        int32_t speedPct = 0;           // a new form's own run speed bonus
        bool slowFall = false;          // a new form falls slowly (the crow's glide)
        uint16_t option = 0;            // the class option it is (Classes.h), 0 for Utility
        int8_t creatureType = 1;        // the form's creature type (1 beast, 7 humanoid, -1 none)
    };

    // Every druid form, stock and new.
    std::vector<Form> const& Forms();
    Form const* ByForm(uint8_t form);
    Form const* BySpell(uint32_t spell);
    // A new form's spell, which the server rewrites (FitFormSpell): cast on oneself, no target.
    bool IsFitted(uint32_t spell);
    // A druid form (any of the table's) is in effect: no gun fires.
    bool BlocksGuns(uint8_t form);
    // The form's abilities from the class catalog (an option's spells after its first).
    std::vector<uint32_t> Abilities(Form const& f);
    // The form's bar page as the server lays it out: its abilities, then the core abilities
    // usable in any form (Barkskin), at most 12; catalog spell ids, 0 empty.
    std::array<uint32_t, kSlotsPerPage> BarLayout(Form const& f);

    // ── The form spell as the server fits it ──
    constexpr uint32_t kEffectApplyAura = 6;
    constexpr uint32_t kAuraModShapeshift = 36;
    constexpr uint32_t kAuraModIncreaseSpeed = 31;
    constexpr uint32_t kAuraFeatherFall = 105;
    constexpr uint32_t kAuraModStun = 12;
    constexpr uint32_t kAuraTransform = 56;
    constexpr uint32_t kAuraModCharm = 6;
    constexpr uint32_t kTargetCaster = 1;
    // Cat Form's own attribute words: an ability, not while shapeshifted (so a form needs the
    // current one cancelled first, as stock), keeps the weapons drawn; immunity purges.
    constexpr uint32_t kFormAttributes = 0x00050010;
    constexpr uint32_t kFormAttributesEx = 0x00008000;
    constexpr uint32_t kDurationInfinite = 21;      // SpellDuration.dbc: -1
    constexpr uint32_t kDuration30s = 9;            // SpellDuration.dbc: 30 000 ms
    constexpr uint32_t kActiveIconForm = 122;       // the forms' ActiveIconID (the lit border)

    struct FittedEffect
    {
        uint32_t effect = 0;
        uint32_t aura = 0;
        int32_t misc = 0;
        int32_t basePoints = 0;     // the value is basePoints + 1 (dice 1, sides 1)
        uint32_t target = 0;
    };
    struct FittedSpell
    {
        std::array<FittedEffect, 3> effects{};
        uint32_t attributes = 0;
        uint32_t attributesEx = 0;
        uint32_t durationIndex = 0;
        uint32_t auraInterruptFlags = 0;
        uint32_t activeIconId = 0;
        uint32_t dispel = 0;
        uint32_t mechanic = 0;
    };
    // The spell a fitted form casts: one MOD_SHAPESHIFT aura on oneself into its form, its speed
    // and slow fall, positive (no charm, stun or transform), with a buff to right-click.
    FittedSpell FitFormSpell(Form const& f);
    // Whether an effect list leaves a form as the rules want: exactly one MOD_SHAPESHIFT into
    // `form` on the caster and nothing negative (charm, stun, transform).
    bool IsCleanFormSpell(FittedSpell const& s, uint8_t form);

    // ── The wire: SMSG_FUSION_CHANNEL on Classes::kChannelClasses ──
    // op 8: [8, n u8, n x (spell u32, form u8, bonusBar u8, stanceBarOrder i8, formFlags u8,
    // creatureType i8, name: len u8, bytes)]: the forms the client's own Spell.dbc and
    // SpellShapeshiftForm.dbc do not know (or know with no bar), so its stance bar, form byte and
    // bonus bar read them as the server runs them.
    constexpr uint8_t kOpForms = 8;
    constexpr uint8_t kFormFlagsShapeshift = 0x50;  // Bear's flags1: a shapeshift, cancelable
    struct WireForm
    {
        uint32_t spell = 0;
        uint8_t form = 0;
        uint8_t bonusBar = 0;
        int8_t stanceBarOrder = 0;
        uint8_t formFlags = 0;
        int8_t creatureType = 0;
        std::string name;
    };
    std::vector<WireForm> WireForms();
    std::vector<uint8_t> EncodeForms(std::vector<WireForm> const& forms);
    bool DecodeForms(std::vector<uint8_t> const& body, std::vector<WireForm>& out);
}

#endif
