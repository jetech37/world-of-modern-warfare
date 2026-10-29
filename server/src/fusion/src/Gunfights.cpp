/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Gunfights.h"
#include "fusion/Weapon.h"

#include <algorithm>
#include <cmath>

namespace Fusion
{
    namespace
    {
        constexpr float kDegToRad = 3.14159265f / 180.f;
    }

    float FalloffRangeScale(WeaponClass c)
    {
        switch (c)
        {
            case WeaponClass::AssaultRifle: return kGunfight.falloffRifle;
            case WeaponClass::Lmg: return kGunfight.falloffLmg;
            case WeaponClass::Smg:
            case WeaponClass::MachinePistol:
            case WeaponClass::Pistol: return kGunfight.falloffSmg;
            case WeaponClass::Sniper: return kGunfight.falloffSniper;
            case WeaponClass::Shotgun: return kGunfight.falloffShotgun;
            default: return 1.f;
        }
    }

    float NpcAimCone(NpcAimInput const& in)
    {
        GunfightTuning const& t = kGunfight;
        float const settled = Clamp(float(in.heldMs) / float(std::max<uint32_t>(1, t.settleMs)), 0.f, 1.f);
        float cone = Lerp(t.acquireConeDeg, t.settledConeDeg, settled);
        cone += std::max(0.f, in.distance - t.coneWidensFrom) * t.coneDegPerYard;
        Gait const gait = in.sprinting ? Gait::Sprint : in.gait;
        switch (gait)
        {
            case Gait::Walk: cone *= t.walkCone; break;
            case Gait::Run: cone *= t.runCone; break;
            case Gait::Sprint: cone *= t.sprintCone; break;
            case Gait::Backpedal: cone *= t.backpedalCone; break;
            default: break;
        }
        if (in.partialCover)
            cone *= t.partialCoverCone;
        switch (in.rank)
        {
            case CreatureRank::Elite: cone *= t.eliteCone; break;
            case CreatureRank::Rare: cone *= t.rareCone; break;
            case CreatureRank::RareElite: cone *= t.rareEliteCone; break;
            case CreatureRank::WorldBoss:
            case CreatureRank::DungeonBoss: cone *= t.bossCone; break;
            default: break;
        }
        return std::min(cone, 45.f);
    }

    float NpcTargetBody(Stance stance, bool partialCover)
    {
        GunfightTuning const& t = kGunfight;
        float body;
        switch (stance)
        {
            case Stance::Crouch:
            case Stance::Slide: body = t.crouchBody; break;
            case Stance::Prone: body = t.proneBody; break;
            default: body = t.standBody; break;
        }
        return partialCover ? body * t.partialCoverBody : body;
    }

    float NpcConeHitChance(float coneDeg, float distance, float body, int32_t levelGap)
    {
        GunfightTuning const& t = kGunfight;
        float const spread = std::max(0.f, distance) * std::tan(Clamp(coneDeg, 0.f, 80.f) * kDegToRad);
        float chance = spread > 1e-4f ? body / spread : 1.f;
        chance = Clamp(chance, t.minHitChance, t.maxHitChance);
        chance *= Clamp(1.f + t.levelGapPerLevel * float(levelGap), 0.15f, 1.3f);
        return Clamp(chance, t.minHitChance, t.maxHitChance);
    }

    float NpcMissOffset(float coneDeg, float distance, float body, float roll01)
    {
        float const spread = std::max(0.f, distance) * std::tan(Clamp(coneDeg, 0.f, 80.f) * kDegToRad);
        float const inner = body + kGunfight.missClearance;
        // A cone no wider than the body (the miss came of the level gap): just past the body.
        float const outer = std::max(spread, inner + 0.6f);
        return Lerp(inner, outer, Clamp(roll01, 0.f, 1.f));
    }

    void NpcAimTrack::Update(uint64_t targetGuid, bool sight, uint32_t nowMs)
    {
        if (targetGuid != target)
        {
            target = targetGuid;
            holding = false;
            keptMs = 0;
            sinceMs = nowMs;
        }
        if (sight && !holding)
        {
            // Back in sight: a short break keeps half of what had settled.
            uint32_t const lost = nowMs - sinceMs;
            keptMs = lost <= kGunfight.sightGraceMs ? keptMs / 2 : 0;
            holding = true;
            sinceMs = nowMs;
        }
        else if (!sight && holding)
        {
            keptMs = HeldMs(nowMs);
            holding = false;
            sinceMs = nowMs;
        }
    }

    uint32_t NpcAimTrack::HeldMs(uint32_t nowMs) const
    {
        return holding ? keptMs + (nowMs - sinceMs) : 0;
    }

    float GunfireHearing(bool silenced, bool coldBlooded)
    {
        float const r = silenced ? kGunfight.hearingSilenced : kGunfight.hearingOpen;
        return coldBlooded ? r * kGunfight.coldBloodedHearing : r;
    }

    std::vector<HeardBy> WhoHearsTheShot(std::vector<Listener> listeners, float radius, uint32_t budget, uint32_t roll)
    {
        GunfightTuning const& t = kGunfight;
        std::vector<HeardBy> out;
        uint32_t const cap = std::min(budget, t.hearingPerShot);
        if (cap == 0 || !(radius > 0.f))
            return out;
        std::sort(listeners.begin(), listeners.end(), [](Listener const& a, Listener const& b) { return a.distance < b.distance; });
        uint32_t const span = t.hearingDelayMaxMs > t.hearingDelayMinMs ? t.hearingDelayMaxMs - t.hearingDelayMinMs : 0;
        for (Listener const& l : listeners)
        {
            if (out.size() >= cap)
                break;
            float const heard = l.lineOfSight ? radius : radius * t.wallMuffle;
            if (l.distance > heard)
                continue;
            HeardBy h;
            h.guid = l.guid;
            if (!l.lineOfSight)
            {
                // Each its own beat, from the roll and its place in the queue.
                uint32_t x = roll ^ (uint32_t(out.size()) * 0x9E3779B9u);
                x ^= x >> 15;
                x *= 0x2c1b3c6du;
                x ^= x >> 12;
                h.delayMs = t.hearingDelayMinMs + (span ? x % (span + 1) : 0);
            }
            out.push_back(h);
        }
        return out;
    }

    uint32_t HearingBudget::Left(uint32_t nowMs)
    {
        if (!started || nowMs - windowStartMs >= kGunfight.hearingWindowMs)
        {
            started = true;
            windowStartMs = nowMs;
            used = 0;
        }
        return used >= kGunfight.hearingPerWindow ? 0 : kGunfight.hearingPerWindow - used;
    }

    void HearingBudget::Spend(uint32_t n, uint32_t nowMs)
    {
        Left(nowMs);
        used += n;
    }

    float ChaseSpeed(float distance, float reach, bool fastNow)
    {
        GunfightTuning const& t = kGunfight;
        // Fast until within its reach plus the slow band; at its pace until past the fast band.
        float const edge = reach + (fastNow ? t.chaseSlowBand : t.chaseFastBand);
        return distance > edge ? t.chaseSpeed : 1.f;
    }
}
