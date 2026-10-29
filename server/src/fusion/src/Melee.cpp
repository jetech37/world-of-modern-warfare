/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Melee.h"

#include <cmath>

namespace Fusion { namespace Melee
{
    namespace
    {
        Profile Make(uint8_t subclass, bool twoHanded, uint16_t swingMs, float damage)
        {
            Profile p;
            p.subclass = subclass;
            p.twoHanded = twoHanded;
            p.swingMs = swingMs;
            p.lungeMs = uint16_t(std::min(65535.f, float(swingMs) * 1.4f + 0.5f));
            p.baseDamage = damage;
            return p;
        }
    }

    Profile ProfileFor(uint8_t subclass)
    {
        switch (subclass)
        {
            case Dagger:      return Make(subclass, false, 450, 35.f);
            case Fist:        return Make(subclass, false, 400, 30.f);
            case Sword:
            case Exotic:      return Make(subclass, false, 600, 60.f);
            case Axe:         return Make(subclass, false, 700, 70.f);
            case Mace:        return Make(subclass, false, 800, 85.f);
            case Staff:
            case FishingPole: return Make(subclass, true, 900, 75.f);
            case Polearm:
            case Spear:       return Make(subclass, true, 1050, 110.f);
            case Sword2:
            case Exotic2:     return Make(subclass, true, 1000, 120.f);
            case Axe2:        return Make(subclass, true, 1100, 135.f);
            case Mace2:       return Make(subclass, true, 1250, 150.f);
            case kNoWeapon:   return Make(kNoWeapon, false, 500, 25.f);
            default:          return Make(subclass, false, 500, 25.f);  // a wand or a bow in hand: fists
        }
    }

    float WeaponFactor(float weaponDps, uint32_t level, bool hasWeapon)
    {
        if (!hasWeapon)
            return 1.f;
        return Clamp(weaponDps / (3.f + 0.9f * float(level)), 0.6f, 2.f);
    }

    float Damage(Profile const& p, float weaponDps, uint32_t level, bool hasWeapon, float multiplier)
    {
        return p.baseDamage * WeaponFactor(weaponDps, level, hasWeapon) * std::max(0.f, multiplier);
    }

    bool Ready(uint32_t sinceLastMs, uint16_t recoveryMs)
    {
        return float(sinceLastMs) >= float(recoveryMs) * 0.9f;
    }

    float ComboMultiplier(uint8_t step)
    {
        if (step == 2)
            return kComboSecond;
        if (step >= kComboLength)
            return kComboFinisher;
        return 1.f;
    }

    float ComboHit(Combo& c, uint32_t nowMs, uint16_t swingMs)
    {
        bool const inTime = c.step > 0 && nowMs - c.lastHitMs <= uint32_t(swingMs) + kComboWindowMs;
        c.step = inTime && c.step < kComboLength ? uint8_t(c.step + 1) : 1;
        c.lastHitMs = nowMs;
        return ComboMultiplier(c.step);
    }

    float PositionalMultiplier(float offFacing)
    {
        float const a = std::fabs(offFacing);
        if (a > 2.0944f)        // 120 degrees
            return kBehindMultiplier;
        if (a > 1.0472f)        // 60 degrees
            return kFlankMultiplier;
        return 1.f;
    }

    void GuardUpdate(Guard& g, bool up, uint32_t nowMs)
    {
        if (up && !g.up)
        {
            g.perfectArmed = !g.raisedBefore || nowMs - g.lastRaiseMs >= kPerfectBlockRearmMs;
            g.upSinceMs = nowMs;
            g.lastRaiseMs = nowMs;
            g.raisedBefore = true;
        }
        g.up = up;
    }

    Block GuardBlocks(Guard const& g, bool fromFront, uint32_t nowMs)
    {
        if (!g.up || !fromFront)
            return Block::None;
        if (g.perfectArmed && nowMs - g.upSinceMs <= kPerfectBlockMs)
            return Block::Perfect;
        return Block::Partial;
    }

    float BlockMultiplier(Block b, bool bullet)
    {
        switch (b)
        {
            case Block::Perfect: return 0.f;
            case Block::Partial: return 1.f - (bullet ? kGuardBulletCut : kGuardBlowCut);
            default: return 1.f;
        }
    }

    std::vector<uint8_t> EncodeProfile(Profile const& p, uint32_t itemEntry, uint32_t displayId, OffHand const& off)
    {
        std::vector<uint8_t> o{kOpProfile, p.subclass, uint8_t(p.twoHanded ? 1 : 0)};
        auto put16 = [&](uint16_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); };
        auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); };
        put16(p.swingMs);
        put16(p.lungeMs);
        put32(itemEntry);
        put32(displayId);
        o.push_back(uint8_t(off.kind));
        o.push_back(off.subclass);
        put32(off.displayId);
        return o;
    }
}}
