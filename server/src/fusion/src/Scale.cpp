/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Scale.h"

namespace Fusion { namespace Scale
{
    namespace
    {
        // 0 at level 1, 1 at level 60 (and on past it for 61-63 bosses).
        float Span(uint32_t level)
        {
            return float(int32_t(std::max<uint32_t>(1, level)) - 1) / 59.f;
        }
    }

    float PlayerMaxHealth(uint32_t level, float gearStamina)
    {
        return 100.f + 200.f * Clamp(Span(level), 0.f, 1.f) + std::max(0.f, gearStamina);
    }

    float PlayerArmorCut(float armor, uint32_t level)
    {
        armor = std::max(0.f, armor);
        return Clamp(armor / (armor + 400.f + 85.f * float(level)), 0.f, kMaxArmorCut);
    }

    float LevelMultiplier(uint32_t level)
    {
        return 1.f + 2.f * Clamp(Span(level), 0.f, 1.f);
    }

    float ExpectedPower(uint32_t level)
    {
        return 40.f + 12.f * float(level);
    }

    float GearMultiplier(float power, uint32_t level)
    {
        return Clamp(power / ExpectedPower(level), 1.f, kMaxGearMultiplier);
    }

    float AttackPower(float meleeAp, float rangedAp, float spellBonus)
    {
        return std::max({meleeAp, rangedAp, 3.f * spellBonus, 0.f});
    }

    float PlayerDamageMultiplier(uint32_t level, float meleeAp, float rangedAp, float spellBonus)
    {
        return LevelMultiplier(level) * GearMultiplier(AttackPower(meleeAp, rangedAp, spellBonus), level);
    }

    float CreatureRankFactor(CreatureFacts const& f)
    {
        if (f.boss && f.place == Place::Raid)
            return 30.f;
        if (f.boss && f.place == Place::Dungeon)
            return 10.f;
        switch (f.rank)
        {
            case CreatureRank::Normal:      return 1.f;
            case CreatureRank::Rare:        return 1.5f;
            case CreatureRank::Elite:       return 3.f;
            case CreatureRank::RareElite:   return 4.f;
            case CreatureRank::WorldBoss:   return 30.f;
            case CreatureRank::DungeonBoss: return 10.f;
        }
        return 1.f;
    }

    float CreaturePlaceFactor(Place p)
    {
        switch (p)
        {
            case Place::Dungeon: return 1.25f;
            case Place::Raid:    return 1.5f;
            default:             return 1.f;
        }
    }

    float CreatureMaxHealth(CreatureFacts const& f)
    {
        return (100.f + 150.f * Span(f.level)) * CreatureRankFactor(f) * CreaturePlaceFactor(f.place);
    }

    float CreatureDamageRank(CreatureFacts const& f)
    {
        if (f.boss || f.rank == CreatureRank::WorldBoss || f.rank == CreatureRank::DungeonBoss)
            return 3.f;
        if (f.rank == CreatureRank::Elite || f.rank == CreatureRank::RareElite)
            return 1.5f;
        return 1.f;
    }

    float CreatureGunMultiplier(CreatureFacts const& f)
    {
        return (1.f + 0.5f * Span(f.level)) * CreatureDamageRank(f);
    }

    float CreatureMeleeBase(CreatureFacts const& f)
    {
        return (10.f + 25.f * Span(f.level)) * CreatureDamageRank(f);
    }

    float CreatureSpellBase(CreatureFacts const& f)
    {
        return 1.5f * CreatureMeleeBase(f);
    }

    float ExpectedWowSpell(uint32_t level, bool tick)
    {
        float const hit = 20.f + 10.f * float(level);
        return tick ? hit / 5.f : hit;
    }

    float WowToCod(float base, float wowDamage, float expectedWow)
    {
        if (wowDamage <= 0.f)
            return 0.f;
        float const ratio = expectedWow > 0.f ? wowDamage / expectedWow : 1.f;
        return base * Clamp(ratio, 0.3f, 3.f);
    }

    float PoolRatio(float newMax, float wowMax)
    {
        if (newMax <= 0.f || wowMax <= 0.f)
            return 1.f;
        return newMax / wowMax;
    }
}}
