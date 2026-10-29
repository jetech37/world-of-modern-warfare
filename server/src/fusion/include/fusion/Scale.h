/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_SCALE_H
#define FUSION_SCALE_H

#include "fusion/Common.h"

#include <cstdint>

// The CoD scale: every health pool on MW2's 100. A level 1 has 100, a fresh level 60 300, a
// geared tank 600 to 1,000 (stamina from gear counts one for one); creatures sit on the same
// scale by level, rank and where they stand. A player's guns hit 1x at level 1 and 3x at 60, and
// gear lifts that to 6-10x; NPCs hit by their level and rank only, so a low one stays dangerous.
namespace Fusion { namespace Scale
{
    // ── Players ──
    // 100 + 200 (level - 1) / 59 + the stamina gear (and buffs) add over the character's own.
    float PlayerMaxHealth(uint32_t level, float gearStamina);
    // Armor's cut of fusion gun, creature melee and creature spell damage to a player:
    // armor / (armor + 400 + 85 level), at most kMaxArmorCut.
    constexpr float kMaxArmorCut = 0.30f;
    float PlayerArmorCut(float armor, uint32_t level);

    // A player's outgoing damage multiplier M = L x G: L = 1 + 2 (level - 1) / 59 (1 to 3), and
    // G = power / expectedPower(level) held to [1, kMaxGearMultiplier], power the best of melee
    // attack power, ranged attack power and three times the spell damage bonus.
    constexpr float kMaxGearMultiplier = 3.33f;
    float LevelMultiplier(uint32_t level);
    float ExpectedPower(uint32_t level);                // 40 + 12 level
    float GearMultiplier(float power, uint32_t level);
    float AttackPower(float meleeAp, float rangedAp, float spellBonus);
    float PlayerDamageMultiplier(uint32_t level, float meleeAp, float rangedAp, float spellBonus);

    // ── Creatures ──
    enum class Place : uint8_t { World, Dungeon, Raid };

    struct CreatureFacts
    {
        uint32_t level = 1;
        CreatureRank rank = CreatureRank::Normal;   // the template's (1.12) rank
        bool boss = false;                          // a boss: the boss flag, or a world boss rank
        Place place = Place::World;
    };

    // (100 + 150 (level - 1) / 59) x rank (normal 1, rare 1.5, elite 3, rare elite 4, world boss
    // 30; a boss in a dungeon 10, in a raid 30, instead) x place (dungeon 1.25, raid 1.5).
    float CreatureRankFactor(CreatureFacts const& f);
    float CreaturePlaceFactor(Place p);
    float CreatureMaxHealth(CreatureFacts const& f);

    // What a creature's hits are worth beside its level: an elite 1.5, a boss 3.
    float CreatureDamageRank(CreatureFacts const& f);
    // An NPC gun's multiplier on the gun's CoD damage: (1 + 0.5 (level - 1) / 59) x rank.
    float CreatureGunMultiplier(CreatureFacts const& f);
    // A creature's melee hit on the CoD scale: 10 + 25 (level - 1) / 59, times its rank; spells
    // 1.5 times that.
    float CreatureMeleeBase(CreatureFacts const& f);
    float CreatureSpellBase(CreatureFacts const& f);
    // The spell damage a creature of a level is expected to deal in WoW: 20 + 10 level (a damage
    // over time tick a fifth of it).
    float ExpectedWowSpell(uint32_t level, bool tick);
    // WoW damage to the CoD scale: base x clamp(wow / expected, 0.3, 3).
    float WowToCod(float base, float wowDamage, float expectedWow);

    // How much a WoW amount (a heal, a regeneration tick, a player's spell) is worth on a pool
    // of `newMax` that WoW made `wowMax`.
    float PoolRatio(float newMax, float wowMax);
}}

#endif
