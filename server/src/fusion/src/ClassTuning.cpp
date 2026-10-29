/*
 * World of Modern Warfare: the class identity pass's numbers (fusion/ClassTuning.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "fusion/ClassTuning.h"

#include <algorithm>
#include <array>

namespace Fusion::ClassTuning
{
    using namespace Fusion::Classes;

    std::vector<Row> const& Rows()
    {
        // Fitted against the duel model (Balance::Duel): each archetype's own kit beats or trades
        // evenly with an equal AK-47 gunner from its natural range, without a gun.
        static std::vector<Row> const rows = {
            // Paladin: the front-line hybrid's ranged punch.
            {OptionId(kPaladin, 2, 0), 1.25f, false, "Judgement: the Crusader's finisher lands from 20 yd"},
            {OptionId(kPaladin, 3, 2), 1.5f, false, "Holy Shock: the Lightwarden's one instant smite"},
            // Hunter
            {OptionId(kHunter, 2, 2), 2.f, false, "Throwing Axes: the Slayer's reach while it closes"},
            {OptionId(kHunter, 4, 1), 2.f, false, "Bear Trap: the Trapper's kill is the trap"},
            // Priest: Shadow is the priest's damage; its afflictions are its pressure.
            {OptionId(kPriest, 1, 0), 1.4f, false, "Shadow Word: Pain: the Shadow priest's pressure"},
            {OptionId(kPriest, 2, 0), 1.25f, false, "Mind Blast"},
            {OptionId(kPriest, 4, 0), 1.4f, false, "Devouring Plague"},
            // Shaman
            {OptionId(kShaman, 3, 0), 1.2f, false, "Frost Shock: the kite"},
            {OptionId(kShaman, 3, 1), 1.3f, false, "Earth Shock: the Earthbreaker's reach"},
            // Mage: the Wizard is burst, Arcane Missiles homes (classic: the missiles always found
            // their mark).
            {OptionId(kMage, 3, 0), 1.0f, true, "Arcane Missiles home: the Wizard's sure damage"},
            {OptionId(kMage, 4, 0), 1.3f, false, "Blast Wave: the close-range answer"},
            // Warlock: the Afflicter's DoTs and drains.
            {OptionId(kWarlock, 1, 0), 1.6f, false, "Corruption: the Afflicter's clock"},
            {OptionId(kWarlock, 3, 0), 1.6f, false, "Drain Life: the attrition win"},
            {OptionId(kWarlock, 6, 0), 1.0f, false, "Curse of Doom"},
            {OptionId(kWarlock, 4, 1), 1.3f, false, "Death Coil"},
            // Druid: the Starcaller's moon.
            {OptionId(kDruid, 1, 2), 1.8f, true, "Moonfire: homes, the Starcaller's opener"},
            {OptionId(kDruid, 4, 2), 1.4f, false, "Hurricane: zone control"},
        };
        return rows;
    }

    Row const* RowOf(uint16_t option)
    {
        for (Row const& r : Rows())
            if (r.option == option)
                return &r;
        return nullptr;
    }

    float DamageScale(uint16_t option)
    {
        Row const* r = RowOf(option);
        return r ? r->damage : 1.f;
    }

    bool GrantsHoming(uint16_t option)
    {
        Row const* r = RowOf(option);
        return r && r->homing;
    }

    int LeadArchetype(uint8_t cls, Picks const& picks)
    {
        std::array<int, kArchetypes> count{};
        std::array<int, kArchetypes> topTier{};
        for (uint16_t id : picks)
        {
            Option const* o = FindOption(id);
            if (!o || o->cls != cls || o->archetype >= kArchetypes)
                continue;
            ++count[o->archetype];
            topTier[o->archetype] = std::max<int>(topTier[o->archetype], o->tier);
        }
        int best = -1;
        for (int a = 0; a < kArchetypes; ++a)
        {
            if (!count[size_t(a)])
                continue;
            if (best < 0 || count[size_t(a)] > count[size_t(best)]
                || (count[size_t(a)] == count[size_t(best)] && topTier[size_t(a)] > topTier[size_t(best)]))
                best = a;
        }
        return best;
    }

    bool IsHealerArchetype(uint8_t cls, int archetype)
    {
        switch (cls)
        {
            case kPaladin: return archetype == 2;                       // Lightwarden
            case kPriest: return archetype == 1 || archetype == 2;      // Cleric, Oracle
            case kShaman: return archetype == 2;                        // Totemist
            case kDruid: return archetype == 1;                         // Grovekeeper
            default: return false;
        }
    }

    float BasicSpellScale(uint8_t cls, int archetype)
    {
        if (archetype < 0)
            return 1.f;
        // A healer's basic spell is its only damage: strong enough to win a long trade with
        // Atonement under it. The casters' basics carry their rotation.
        if (IsHealerArchetype(cls, archetype))
            return cls == kDruid ? 1.9f : 1.7f;                         // Wrath starts weaker than Smite
        switch (cls)
        {
            case kPriest: return archetype == 0 ? 1.2f : 1.f;          // Shadow's Smite filler
            case kWarlock: return 1.2f;                                 // Shadow Bolt
            case kDruid: return archetype == 2 ? 1.7f : 1.f;           // Starcaller's Wrath
            case kShaman: return archetype == 0 ? 1.15f : 1.f;         // Elementalist's bolt
            default: return 1.f;
        }
    }

    float AtonementHeal(float dealt, bool healerLead)
    {
        return healerLead && dealt > 0.f ? dealt * kAtonementShare : 0.f;
    }

    float ShatterMultiplier(bool wizardLead, bool targetHeld)
    {
        return wizardLead && targetHeld ? kShatterBonus : 1.f;
    }

    bool IsGapCloser(uint16_t option)
    {
        switch (option)
        {
            case OptionId(kWarrior, 0, 0):      // Charge
            case OptionId(kWarrior, 2, 0):      // Intercept
            case OptionId(kWarrior, 5, 1):      // Shield Charge
            case OptionId(kRogue, 2, 2):        // Lunge
            case OptionId(kMage, 2, 1):         // Blade Blink
            case OptionId(kWarlock, 2, 1):      // Fel Leap
                return true;
            default:
                return false;
        }
    }

    bool HunterBond(uint8_t cls)
    {
        return cls == kHunter;
    }

    float DrainHealMultiplier(uint32_t afflictions)
    {
        return 1.f - std::min(kDrainHealCutMax, kDrainHealCutPerAffliction * float(afflictions));
    }
}
