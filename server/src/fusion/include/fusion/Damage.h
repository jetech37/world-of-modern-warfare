/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_DAMAGE_H
#define FUSION_DAMAGE_H

#include "fusion/Common.h"
#include "fusion/Weapon.h"

namespace Fusion
{
    // ── Par: a typical character of a level ─────────────────────────────────────────────────
    // Every stat is judged against par for the character's level, so leveling never changes
    // time-to-kill and better gear always helps (design doc, section 6). Tunable.
    struct ParTable
    {
        float MaxHealth(uint32_t level) const;  // about 350 at 10, 3,700 at 60
        float Armor(uint32_t level) const;      // a mixed cloth/leather/mail/plate average
        float AttackPower(uint32_t level) const;
        float ItemLevel(uint32_t level) const;  // the item level a level is typically geared in
    };

    // 1.12's physical damage reduction from armor against an attacker of a level, capped at 75%.
    float ArmorReduction(float armor, uint32_t attackerLevel);

    // ── Player health ───────────────────────────────────────────────────────────────────────
    struct PlayerDefense
    {
        uint32_t level = 1;
        float maxHealth = 100.f;
        float armor = 0.f;
    };

    // A player's CoD health: their effective health (after armor, against an attacker of their
    // own level) over par's, times 100, clamped to [75, 150] (design doc, section 3).
    float CodHealth(PlayerDefense const& d, ParTable const& par);

    // ── Creatures ───────────────────────────────────────────────────────────────────────────
    // How many "MW2 players" of health a creature of a rank is worth.
    float CreatureBodies(CreatureRank rank);

    // ── The level gap ───────────────────────────────────────────────────────────────────────
    // About 8% more damage per level the attacker is above the target, less below, both ways.
    float LevelFactor(uint32_t attackerLevel, uint32_t targetLevel);

    // ── Gun power ───────────────────────────────────────────────────────────────────────────
    struct AttackerStats
    {
        uint32_t level = 1;
        float attackPower = 0.f;        // the higher of melee and ranged
        float rangedItemLevel = 0.f;    // the ranged slot's item, 0 = empty slot
        float mainHandItemLevel = 0.f;  // the fallback when the ranged slot is empty
        float critChance = 0.f;         // 0..1, from agility and gear
    };

    // The multiplier a character's attack power and ranged-slot item put on their guns, each
    // measured against par for their level and soft-capped; the product never exceeds the gear
    // ceiling (best-in-slot about 25% over par, design doc D35).
    float GunPowerMultiplier(AttackerStats const& a, ParTable const& par, float gearCeiling = 1.25f);

    // ── Special ammo ────────────────────────────────────────────────────────────────────────
    enum class AmmoType : uint8_t
    {
        Basic,
        Incendiary,
        IceThreaded,
        Blessed,
        Arcane,
        ArmorPiercing,
        HollowPoint,
        Explosive,
        Count,
    };

    // The level each round unlocks at quartermasters.
    uint32_t AmmoUnlockLevel(AmmoType t);

    // What the target is, for ammo matchups.
    struct TargetTraits
    {
        bool isPlayer = false;
        CreatureType creatureType = CreatureType::Humanoid;
        ArmorClass armor = ArmorClass::Leather;     // players; creatures use their type
        bool fireElemental = false;
        bool hasAbsorbShield = false;
        bool slowImmune = false;                     // bosses
    };

    struct AmmoEffect
    {
        float damageMultiplier = 1.f;
        float armorIgnore = 0.f;        // share of armor the round ignores
        float burnFraction = 0.f;       // extra damage over 3 s, as a share of the hit
        float slowPercent = 0.f;        // a brief slow
        float shieldBreakMultiplier = 1.f;
        float splashRadius = 0.f;       // yards
    };

    AmmoEffect AmmoAgainst(AmmoType t, TargetTraits const& target);

    // ── One hit ─────────────────────────────────────────────────────────────────────────────
    enum class CombatContext : uint8_t
    {
        PvE,
        PvP,
    };

    // Separate PvE and PvP coefficients: PvP time-to-kill is 2 to 3 times MW2's (0.6 to 1.0 s of
    // sustained accurate fire between equal players, design doc section 4).
    struct Coefficients
    {
        float pve = 1.0f;
        float pvp = 0.4f;
        float For(CombatContext c) const { return c == CombatContext::PvP ? pvp : pve; }
    };

    struct HitInput
    {
        WeaponProfile const* weapon = nullptr;
        float distance = 0.f;
        HitLocation location = HitLocation::TorsoUpper;
        AmmoType ammo = AmmoType::Basic;
        bool critRoll = false;          // a crit turns a body shot into head damage
        AttackerStats attacker;
    };

    // A hit's damage in CoD units: falloff, location (or crit), gun power, ammo, level gap and
    // the context coefficient. The caller converts it to the target's health below.
    float CodDamage(HitInput const& in, uint32_t targetLevel, TargetTraits const& target,
        CombatContext ctx, ParTable const& par, Coefficients const& coef = {});

    // A hit on the CoD scale (Scale.h), where every pool is MW2's: falloff, location (or crit),
    // ammo, the shooter's damage multiplier M (Scale::PlayerDamageMultiplier) and the context
    // coefficient. No gear or level-gap terms: M carries the shooter's, the pool the target's.
    float CodHitDamage(HitInput const& in, TargetTraits const& target, CombatContext ctx, float multiplier,
        Coefficients const& coef = {});

    // CoD damage to WoW health points (the scale before Scale.h; kept for reference).
    float ToPlayerHealth(float codDamage, float targetCodHealth, float targetMaxHealth);
    float ToCreatureHealth(float codDamage, float creatureMaxHealth, CreatureRank rank);

    // ── Spells against guns ─────────────────────────────────────────────────────────────────
    // The least a spell must deal to be worth lowering the gun for its cast and the swap: the
    // gunfire it costs, times a risk bonus (design doc, section 4).
    float MinSpellValue(float gunDps, float castTime, float swapTime = 0.4f, float risk = 1.3f);
}

#endif
