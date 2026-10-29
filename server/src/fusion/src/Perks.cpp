/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Perks.h"

#include <algorithm>

namespace Fusion
{
    PerkFlags PerksOf(ClassLoadout const* l, bool pro)
    {
        PerkFlags p;
        if (!l)
            return p;
        uint16_t const slots[3] = {l->perk1, l->perk2, l->perk3};
        for (int i = 0; i < 3; ++i)
        {
            if (!slots[i] || slots[i] >= 32)
                continue;
            ++p.stacks[slots[i]];
            if (l->perkPro & (1u << i))
                p.proBits |= 1u << slots[i];
        }
        auto has = [&](uint16_t id) { return p.Count(id) > 0; };
        p.pro = pro;
        p.marathon = has(PerkId::Marathon);
        p.marathonPro = p.marathon && p.IsPro(PerkId::Marathon);
        p.sleightOfHand = has(PerkId::SleightOfHand);
        p.scavenger = has(PerkId::Scavenger);
        p.stoppingPower = has(PerkId::StoppingPower);
        p.lightweight = has(PerkId::Lightweight);
        p.hardline = has(PerkId::Hardline);
        p.coldBlooded = has(PerkId::ColdBlooded);
        p.commando = has(PerkId::Commando);
        p.steadyAim = has(PerkId::SteadyAim);
        p.ninja = has(PerkId::Ninja);
        p.oneManArmy = has(PerkId::OneManArmy);
        p.dangerClose = has(PerkId::DangerClose);
        p.lastStand = has(PerkId::LastStand);
        p.scrambler = has(PerkId::Scrambler);
        p.sitRep = has(PerkId::SitRep);
        p.deathstreak = l->deathstreak;
        return p;
    }

    uint8_t Copies(PerkFlags const& p, uint16_t id, bool flag)
    {
        // A flag set by hand (a test, an older caller) counts as one copy.
        return flag ? std::min<uint8_t>(std::max<uint8_t>(p.Count(id), 1), kMaxPerkStack) : 0;
    }

    float DiminishingShare(float first, uint8_t copies)
    {
        // Each copy past the first adds half what the one before it did: 1, 1.5, 1.75 times
        // the first's share.
        float share = 0.f, step = first;
        for (uint8_t i = 0; i < copies; ++i, step *= 0.5f)
            share += step;
        return share;
    }

    float BulletDamageMultiplier(PerkFlags const& p, AmmoType ammo)
    {
        return p.stoppingPower && ammo != AmmoType::Explosive ? 1.4f : 1.f;
    }

    float DetectionPerkMultiplier(PerkFlags const& p)
    {
        float m = p.ninja ? 0.7f : 1.f;
        // Cold-Blooded Pro: creatures are slower to pick the player out, too.
        if (p.coldBlooded && p.IsPro(PerkId::ColdBlooded))
            m *= kColdBloodedProDetection;
        return m;
    }

    float ExplosiveDamageMultiplier(PerkFlags const& p, bool airSupport)
    {
        if (!p.dangerClose || (airSupport && !p.IsPro(PerkId::DangerClose)))
            return 1.f;
        return 1.25f;
    }

    uint32_t DeathstreakDeaths(PerkFlags const& p, uint32_t needed)
    {
        return p.hardline && p.IsPro(PerkId::Hardline) && needed > 1 ? needed - 1 : needed;
    }

    float GoldLootMultiplier(PerkFlags const& p)
    {
        // A quarter more a copy; the Pro a quarter more again.
        uint8_t const n = Copies(p, PerkId::Scavenger, p.scavenger);
        return 1.f + 0.25f * n + (n && p.IsPro(PerkId::Scavenger) ? 0.25f : 0.f);
    }

    float CastTimeMultiplier(PerkFlags const& p)
    {
        // 15% quicker, then less a copy: 15, 22.5, 26.25%.
        return 1.f - DiminishingShare(0.15f, Copies(p, PerkId::SleightOfHand, p.sleightOfHand));
    }

    float CreatureCastAtMultiplier(PerkFlags const& p)
    {
        // 20% slower, then less a copy (20, 30, 35%); the Pro a tenth more.
        uint8_t const n = Copies(p, PerkId::Scrambler, p.scrambler);
        return 1.f + DiminishingShare(0.2f, n) + (n && p.IsPro(PerkId::Scrambler) ? 0.1f : 0.f);
    }

    float MeleePerkMultiplier(PerkFlags const& p)
    {
        if (!p.stoppingPower)
            return 1.f;
        return p.IsPro(PerkId::StoppingPower) ? 1.35f : 1.2f;
    }

    float FallDamageMultiplier(PerkFlags const& p)
    {
        if (p.commando && p.IsPro(PerkId::Commando))
            return 0.f;
        return p.lightweight ? 0.5f : 1.f;
    }

    float KillXpPerkMultiplier(PerkFlags const& p) { return p.hardline ? 1.1f : 1.f; }
    bool BlocksCallForHelp(PerkFlags const& p) { return p.coldBlooded; }
    float StealthConcealYards(PerkFlags const& p)
    {
        // 3 yards, then less a copy (3, 4.5, 5.25); the Pro 2 more.
        uint8_t const n = Copies(p, PerkId::Ninja, p.ninja);
        return DiminishingShare(3.f, n) + (n && p.IsPro(PerkId::Ninja) ? 2.f : 0.f);
    }

    float StealthDetectYards(PerkFlags const& p)
    {
        // 5 yards a copy; the Pro 3 more.
        uint8_t const n = Copies(p, PerkId::SitRep, p.sitRep);
        return 5.f * n + (n && p.IsPro(PerkId::SitRep) ? 3.f : 0.f);
    }

    float OutOfCombatSpeedMultiplier(PerkFlags const& p, bool inCombat)
    {
        if (inCombat)
            return 1.f;
        // A twentieth faster a copy; the Pro another twentieth.
        uint8_t const n = Copies(p, PerkId::Marathon, p.marathon);
        return 1.f + 0.05f * n + (n && p.IsPro(PerkId::Marathon) ? 0.05f : 0.f);
    }

    bool NoFallDamage(PerkFlags const& p)
    {
        return p.commando && p.IsPro(PerkId::Commando);
    }

    bool EquipWaitsForCombat(bool inCombat, bool oneManArmy)
    {
        return inCombat && !oneManArmy;
    }

    uint32_t FieldEditorCooldown(PerkFlags const& p, uint32_t normal)
    {
        return p.oneManArmy && p.IsPro(PerkId::OneManArmy) ? normal / 2 : normal;
    }

    uint32_t FieldRearmCooldownMs(PerkFlags const& p)
    {
        if (!p.oneManArmy)
            return 0;
        return p.IsPro(PerkId::OneManArmy) ? kFieldRearmCooldownMs / 2 : kFieldRearmCooldownMs;
    }

    bool FieldRearmReady(PerkFlags const& p, uint32_t nowMs, uint32_t readyAtMs)
    {
        return p.oneManArmy && (readyAtMs == 0 || int32_t(nowMs - readyAtMs) >= 0);
    }

    uint32_t LastStandExtraMs(PerkFlags const& p)
    {
        return p.lastStand && p.IsPro(PerkId::LastStand) ? uint32_t(kLastStandProSeconds * 1000.f) : 0u;
    }

    float DamageTakenMultiplier(bool painkillerActive)
    {
        return painkillerActive ? 1.f / 3.f : 1.f;
    }

    bool HoldsFatalHit(uint32_t health, uint32_t damage, bool ready)
    {
        return ready && damage >= health;
    }
}
