/*
 * World of Modern Warfare: the rules behind the class options' custom effects. GPL-2.0-or-later,
 * see Common.h.
 */

#include "fusion/ClassEffects.h"
#include "fusion/Classes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Fusion::Classes
{
    uint8_t ImmunitiesOf(uint16_t option)
    {
        switch (option)
        {
            case OptionId(kWarrior, 4, 0): return IMMUNE_SLOW;                                  // Blood Frenzy's Frenzy
            case OptionId(kWarrior, 5, 0): return IMMUNE_FEAR;                                  // Death Wish
            case OptionId(kWarrior, 6, 0): return IMMUNE_STUN | IMMUNE_FEAR | IMMUNE_KNOCKBACK; // Rampage
            case OptionId(kWarrior, 6, 2): return IMMUNE_STUN | IMMUNE_FEAR;                    // Warlord's Command
            case OptionId(kPaladin, 6, 0): return IMMUNE_SLOW;                                  // Avenging Crusade
            case OptionId(kShaman, 6, 1): return IMMUNE_KNOCKBACK | IMMUNE_SLOW;                // Avatar of Stone
            case OptionId(kWarlock, 6, 1): return IMMUNE_FEAR | IMMUNE_SLOW;                    // Demon Form
            case OptionId(kDruid, 6, 0): return IMMUNE_STUN | IMMUNE_FEAR;                      // Worgen momentum
            default: return 0;
        }
    }

    DurationCap const* DurationCapOf(uint16_t option)
    {
        static std::array<DurationCap, 2> const caps = {{
            {OptionId(kWarrior, 0, 2), 4000, 3000},     // Disarm
            {OptionId(kPaladin, 0, 1), 5000, 3000},     // Hammer of Justice
        }};
        for (DurationCap const& c : caps)
            if (c.option == option)
                return &c;
        return nullptr;
    }

    uint32_t CappedMs(DurationCap const& c, bool player)
    {
        return player ? c.playerMs : c.npcMs;
    }

    bool NearSegment(Vec3 a, Vec3 b, Vec3 p, float radius)
    {
        float const dx = b.x - a.x, dy = b.y - a.y;
        float const len2 = dx * dx + dy * dy;
        float t = len2 > 1e-6f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.f;
        t = Clamp(t, 0.f, 1.f);
        float const cx = a.x + dx * t - p.x, cy = a.y + dy * t - p.y;
        return cx * cx + cy * cy <= radius * radius;
    }

    bool ChargePool::Use(uint32_t nowMs)
    {
        Update(nowMs);
        if (!value)
            return false;
        if (value == max)
            nextMs = nowMs + regenMs;
        --value;
        return true;
    }

    void ChargePool::Update(uint32_t nowMs)
    {
        while (value < max && nextMs && int32_t(nowMs - nextMs) >= 0)
        {
            ++value;
            nextMs = value < max ? nextMs + regenMs : 0;
        }
        if (value >= max)
            nextMs = 0;
    }

    bool Camouflage::Toggle(uint32_t nowMs)
    {
        if (on)
        {
            on = false;
            hidden = false;
            return true;
        }
        if (int32_t(nowMs - readyMs) < 0)
            return false;
        on = true;
        hidden = false;
        stillSinceMs = nowMs;
        return true;
    }

    Camouflage::Step Camouflage::Update(uint32_t nowMs, bool running, bool fired, bool sprinting)
    {
        if (!on)
            return Step::None;
        if (hidden)
        {
            // Walking keeps it; a shot or a sprint gives the hunter away.
            if (!fired && !sprinting)
                return Step::None;
            on = false;
            hidden = false;
            readyMs = nowMs + kCamouflageCooldownMs;
            return Step::Break;
        }
        if (running || fired || sprinting)
        {
            stillSinceMs = nowMs;
            return Step::None;
        }
        if (int32_t(nowMs - stillSinceMs) < int32_t(kCamouflageStillMs))
            return Step::None;
        hidden = true;
        return Step::Fade;
    }

    Rescue RescueOnDown(bool spiritReady, bool reincarnationReady, bool soulstone)
    {
        if (spiritReady)
            return Rescue::Spirit;
        if (reincarnationReady)
            return Rescue::Reincarnation;
        if (soulstone)
            return Rescue::Soulstone;
        return Rescue::None;
    }

    float TotemMultiplier(bool linked, bool circle)
    {
        return (linked ? 1.5f : 1.f) * (circle ? 2.f : 1.f);
    }

    uint8_t TotemLimit(bool circle)
    {
        return circle ? 5 : 3;
    }

    uint8_t ConstructLimit(bool grandArray)
    {
        return grandArray ? 6 : 3;
    }

    uint8_t TrapLimit(bool killingField)
    {
        return killingField ? 8 : 4;
    }

    float TrapChainYards(bool killingField)
    {
        return killingField ? 12.f : 6.f;
    }

    float WeaveBonus(uint32_t& lastSchool, uint32_t school)
    {
        bool const change = lastSchool && school && school != lastSchool;
        lastSchool = school;
        return change ? kWeaveBonus : 1.f;
    }

    uint8_t ZenithChance(uint16_t option)
    {
        switch (option)
        {
            case OptionId(kPaladin, 1, 2): return 10;      // Flash of Light
            case OptionId(kPaladin, 2, 2): return 20;      // Holy Light
            case OptionId(kPaladin, 3, 2): return 15;      // Holy Shock
            default: return 0;
        }
    }

    float EclipseChance(uint16_t option, bool blow)
    {
        switch (option)
        {
            case OptionId(kDruid, 1, 2):        // Moonfire
            case OptionId(kDruid, 3, 2):        // Faerie Fire
            case OptionId(kDruid, 2, 2):        // Lunar Strike
                return 0.2f;
            default:
                return blow ? 0.05f : 0.f;
        }
    }

    void AddDot(std::vector<Dot>& dots, uint64_t target, uint16_t code, uint32_t nowMs, uint32_t durationMs,
        float pctPerTick, uint8_t maxStacks)
    {
        for (Dot& d : dots)
            if (d.target == target && d.code == code)
            {
                d.stacks = uint8_t(std::min<uint32_t>(uint32_t(d.stacks) + 1, std::max<uint8_t>(1, maxStacks)));
                d.maxStacks = maxStacks;
                d.untilMs = nowMs + durationMs;
                d.pctPerTick = pctPerTick;
                return;
            }
        Dot d;
        d.target = target;
        d.code = code;
        d.untilMs = nowMs + durationMs;
        d.nextMs = nowMs + kDotTickMs;
        d.pctPerTick = pctPerTick;
        d.stacks = 1;
        d.maxStacks = std::max<uint8_t>(1, maxStacks);
        dots.push_back(d);
    }

    std::vector<std::pair<uint64_t, float>> DueTicks(std::vector<Dot>& dots, uint32_t nowMs)
    {
        std::vector<std::pair<uint64_t, float>> out;
        for (auto it = dots.begin(); it != dots.end();)
        {
            // A tick due at the very end still lands.
            while (int32_t(nowMs - it->nextMs) >= 0 && int32_t(it->untilMs - it->nextMs) >= 0)
            {
                out.push_back({it->target, it->pctPerTick * float(it->stacks)});
                it->nextMs += kDotTickMs;
            }
            if (int32_t(nowMs - it->untilMs) >= 0)
                it = dots.erase(it);
            else
                ++it;
        }
        return out;
    }

    uint8_t StacksOn(std::vector<Dot> const& dots, uint64_t target, uint16_t code)
    {
        for (Dot const& d : dots)
            if (d.target == target && d.code == code)
                return d.stacks;
        return 0;
    }


    // ── The rogue ──

    bool IsOpener(uint16_t option)
    {
        return option == OptionId(kRogue, 1, 0) || option == OptionId(kRogue, 2, 0) || option == OptionId(kRogue, 3, 0);
    }

    OpenerVerdict CanOpen(uint16_t option, bool stealthed, bool opening, bool behind, bool flanking)
    {
        if (!IsOpener(option))
            return OpenerVerdict::Ok;
        if (!stealthed && !opening)
            return OpenerVerdict::NeedsStealth;
        if (option == OptionId(kRogue, 1, 0) && !behind && !flanking)
            return OpenerVerdict::NeedsBehind;
        return OpenerVerdict::Ok;
    }

    uint32_t CheapShotMs(bool player, bool behind)
    {
        return (player ? 2500u : 4000u) + (behind ? 1000u : 0u);
    }

    uint32_t GarroteSilenceMs(bool behind)
    {
        return behind ? 4000u : 3000u;
    }

    std::vector<uint16_t> PreparationReadies()
    {
        std::vector<uint16_t> out = {OptionId(kRogue, 0, 0), OptionId(kRogue, 0, 1), OptionId(kRogue, 0, 2)};
        for (uint8_t tier = 1; tier <= kTiers; ++tier)
            if (tier != 5)
                out.push_back(OptionId(kRogue, tier, 0));
        return out;
    }

    Poison NextPoison(Poison p)
    {
        switch (p)
        {
            case Poison::Crippling: return Poison::MindNumbing;
            case Poison::MindNumbing: return Poison::Wound;
            default: return Poison::Crippling;
        }
    }

    char const* PoisonName(Poison p)
    {
        switch (p)
        {
            case Poison::Crippling: return "Crippling Poison";
            case Poison::MindNumbing: return "Mind-numbing Poison";
            case Poison::Wound: return "Wound Poison";
            default: return "";
        }
    }

    uint32_t PoisonDebuff(Poison p, uint8_t level)
    {
        struct R { uint32_t spell; uint8_t level; };
        static R const crippling[] = {{3409, 20}, {11201, 50}};
        static R const mind[] = {{5760, 24}, {8692, 38}, {11398, 52}};
        static R const wound[] = {{13218, 32}, {13222, 40}, {13223, 48}, {13224, 56}};
        auto pick = [level](R const* r, size_t n)
        {
            uint32_t s = r[0].spell;
            for (size_t i = 0; i < n; ++i)
                if (r[i].level <= level)
                    s = r[i].spell;
            return s;
        };
        switch (p)
        {
            case Poison::Crippling: return pick(crippling, 2);
            case Poison::MindNumbing: return pick(mind, 3);
            case Poison::Wound: return pick(wound, 4);
            default: return 0;
        }
    }

    PocketVerdict CanLift(PocketTarget const& t)
    {
        if (!t.creature || !t.alive || !t.hostile)
            return PocketVerdict::NoTarget;
        if (!t.hasPocketLoot && !t.humanoid)
            return PocketVerdict::NoPockets;
        if (t.alreadyPicked)
            return PocketVerdict::Picked;
        if (t.distance > kPocketReach)
            return PocketVerdict::TooFar;
        if (!t.lightFingers && (!t.stealthed || t.inCombat || !t.behind))
            return PocketVerdict::NeedsStealth;
        return PocketVerdict::Ok;
    }

    bool PickOpens(LockKind kind, uint16_t lockSkill, uint16_t lockpicking, bool lightFingers)
    {
        if (kind == LockKind::HomesteadDoor || kind == LockKind::HomesteadPortal)
            return false;
        uint32_t const skill = uint32_t(lockpicking) + (lightFingers ? kLightFingersLockBonus : 0u);
        return lockSkill <= skill;
    }

    uint32_t PickLockCastMs(uint32_t vanillaMs, bool lightFingers)
    {
        return lightFingers ? 0u : vanillaMs;
    }

    bool Parries(bool guarding, bool melee, bool fromFront)
    {
        return guarding && melee && fromFront;
    }

    float RangedSpellScale(float spellRangeYards, float power)
    {
        return spellRangeYards > kMeleeSpellReach ? std::max(0.f, power) : 1.f;
    }

    float WardAbsorb(float& left, float incoming, bool bullet, float efficiency)
    {
        if (left <= 0.f || incoming <= 0.f)
            return 0.f;
        float const k = bullet ? std::max(1.f, efficiency) : 1.f;
        float const taken = std::min(incoming, left * k);
        left = std::max(0.f, left - taken / k);
        return taken;
    }
}
