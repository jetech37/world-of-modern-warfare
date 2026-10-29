/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Damage.h"

namespace Fusion
{
    float ParTable::MaxHealth(uint32_t level) const
    {
        float l = float(level);
        return 40.f + 25.f * l + 0.6f * l * l;
    }

    float ParTable::Armor(uint32_t level) const
    {
        float l = float(level);
        return 25.f * l + 0.3f * l * l;
    }

    float ParTable::AttackPower(uint32_t level) const
    {
        float l = float(level);
        return 20.f + 6.f * l + 0.08f * l * l;
    }

    float ParTable::ItemLevel(uint32_t level) const
    {
        return std::max(1.f, float(level) + 3.f);
    }

    float ArmorReduction(float armor, uint32_t attackerLevel)
    {
        armor = std::max(0.f, armor);
        float dr = armor / (armor + 400.f + 85.f * float(attackerLevel));
        return Clamp(dr, 0.f, 0.75f);
    }

    float CodHealth(PlayerDefense const& d, ParTable const& par)
    {
        float effective = d.maxHealth / (1.f - ArmorReduction(d.armor, d.level));
        float parEffective = par.MaxHealth(d.level) / (1.f - ArmorReduction(par.Armor(d.level), d.level));
        if (parEffective <= 0.f)
            return 100.f;
        return Clamp(100.f * effective / parEffective, 75.f, 150.f);
    }

    float CreatureBodies(CreatureRank rank)
    {
        switch (rank)
        {
            case CreatureRank::Normal:      return 1.f;
            case CreatureRank::Rare:        return 2.f;
            case CreatureRank::Elite:       return 4.f;
            case CreatureRank::RareElite:   return 8.f;
            case CreatureRank::DungeonBoss: return 30.f;
            case CreatureRank::WorldBoss:   return 200.f;
        }
        return 1.f;
    }

    float LevelFactor(uint32_t attackerLevel, uint32_t targetLevel)
    {
        float diff = float(int(attackerLevel) - int(targetLevel));
        return Clamp(1.f + 0.08f * diff, 0.4f, 1.6f);
    }

    namespace
    {
        // A ratio's gain above 1, softened: the first 50% over par counts in full, the rest half.
        float SoftGain(float ratio)
        {
            float over = ratio - 1.f;
            if (over <= 0.5f)
                return over;
            return 0.5f + (over - 0.5f) * 0.5f;
        }
    }

    float GunPowerMultiplier(AttackerStats const& a, ParTable const& par, float gearCeiling)
    {
        float parAp = par.AttackPower(a.level);
        float apRatio = parAp > 0.f ? a.attackPower / parAp : 1.f;
        // Attack power moves gun damage by a fifth of its distance from par.
        float apMult = 1.f + 0.2f * SoftGain(apRatio);

        float item = a.rangedItemLevel > 0.f ? a.rangedItemLevel : a.mainHandItemLevel;
        float itemMult = 1.f;
        if (item > 0.f)
        {
            float parItem = par.ItemLevel(a.level);
            itemMult = 1.f + 0.5f * SoftGain(item / parItem);
        }
        float floor = 2.f - gearCeiling;  // as far below par as the ceiling is above it
        return Clamp(apMult * itemMult, floor, gearCeiling);
    }

    uint32_t AmmoUnlockLevel(AmmoType t)
    {
        switch (t)
        {
            case AmmoType::Basic:         return 1;
            case AmmoType::Incendiary:
            case AmmoType::IceThreaded:
            case AmmoType::HollowPoint:   return 10;
            case AmmoType::ArmorPiercing:
            case AmmoType::Explosive:     return 20;
            case AmmoType::Blessed:
            case AmmoType::Arcane:        return 30;
            case AmmoType::Count:         break;
        }
        return 1;
    }

    namespace
    {
        bool SoftTarget(TargetTraits const& t)
        {
            if (t.isPlayer)
                return t.armor == ArmorClass::Cloth || t.armor == ArmorClass::Leather;
            return t.creatureType == CreatureType::Beast || t.creatureType == CreatureType::Critter ||
                t.creatureType == CreatureType::Humanoid;
        }

        bool HardTarget(TargetTraits const& t)
        {
            if (t.isPlayer)
                return t.armor == ArmorClass::Plate || t.armor == ArmorClass::Mail;
            return t.creatureType == CreatureType::Mechanical || t.creatureType == CreatureType::Giant ||
                t.creatureType == CreatureType::Dragonkin;
        }
    }

    AmmoEffect AmmoAgainst(AmmoType type, TargetTraits const& t)
    {
        AmmoEffect e;
        switch (type)
        {
            case AmmoType::Basic:
            case AmmoType::Count:
                break;
            case AmmoType::Incendiary:
                // A short burn; strong on beasts, undead and cloth, nothing on fire elementals.
                if (t.fireElemental)
                    break;
                e.damageMultiplier = 0.95f;
                e.burnFraction = (t.creatureType == CreatureType::Beast || t.creatureType == CreatureType::Undead ||
                    (t.isPlayer && t.armor == ArmorClass::Cloth)) ? 0.30f : 0.15f;
                break;
            case AmmoType::IceThreaded:
                e.damageMultiplier = 0.95f;
                e.slowPercent = t.slowImmune ? 0.f : 0.30f;
                break;
            case AmmoType::Blessed:
                e.damageMultiplier = (!t.isPlayer && (t.creatureType == CreatureType::Undead ||
                    t.creatureType == CreatureType::Demon)) ? 1.30f : 0.95f;
                break;
            case AmmoType::Arcane:
                e.damageMultiplier = 0.95f;
                e.shieldBreakMultiplier = t.hasAbsorbShield ? 2.0f : 1.f;
                break;
            case AmmoType::ArmorPiercing:
                e.armorIgnore = HardTarget(t) ? 0.5f : 0.f;
                e.damageMultiplier = HardTarget(t) ? 1.f : 0.9f;
                break;
            case AmmoType::HollowPoint:
                e.damageMultiplier = SoftTarget(t) ? 1.20f : (HardTarget(t) ? 0.80f : 1.f);
                break;
            case AmmoType::Explosive:
                e.damageMultiplier = 0.85f;
                e.splashRadius = 2.f;
                break;
        }
        return e;
    }

    float CodDamage(HitInput const& in, uint32_t targetLevel, TargetTraits const& target,
        CombatContext ctx, ParTable const& par, Coefficients const& coef)
    {
        if (!in.weapon)
            return 0.f;
        WeaponProfile const& w = *in.weapon;

        float base = CodDamageAtRange(w, in.distance);
        HitLocation loc = in.location;
        // A crit on a body shot deals headshot damage (design doc, section 6).
        if (in.critRoll && loc != HitLocation::Head)
            loc = HitLocation::Head;
        float dmg = base * w.LocationMultiplier(loc);

        dmg *= GunPowerMultiplier(in.attacker, par);
        dmg *= AmmoAgainst(in.ammo, target).damageMultiplier;
        dmg *= LevelFactor(in.attacker.level, targetLevel);
        dmg *= coef.For(ctx);
        return std::max(0.f, dmg);
    }

    float CodHitDamage(HitInput const& in, TargetTraits const& target, CombatContext ctx, float multiplier,
        Coefficients const& coef)
    {
        if (!in.weapon)
            return 0.f;
        WeaponProfile const& w = *in.weapon;
        HitLocation const loc = in.critRoll ? HitLocation::Head : in.location;
        float dmg = CodDamageAtRange(w, in.distance) * w.LocationMultiplier(loc);
        dmg *= AmmoAgainst(in.ammo, target).damageMultiplier;
        dmg *= std::max(0.f, multiplier);
        dmg *= coef.For(ctx);
        return std::max(0.f, dmg);
    }

    float ToPlayerHealth(float codDamage, float targetCodHealth, float targetMaxHealth)
    {
        if (targetCodHealth <= 0.f)
            return 0.f;
        return codDamage / targetCodHealth * targetMaxHealth;
    }

    float ToCreatureHealth(float codDamage, float creatureMaxHealth, CreatureRank rank)
    {
        return codDamage / 100.f * creatureMaxHealth / CreatureBodies(rank);
    }

    float MinSpellValue(float gunDps, float castTime, float swapTime, float risk)
    {
        return gunDps * (std::max(0.f, castTime) + std::max(0.f, swapTime)) * risk;
    }
}
