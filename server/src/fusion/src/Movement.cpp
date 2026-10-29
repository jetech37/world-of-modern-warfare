/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Movement.h"

#include <algorithm>
#include <cmath>

namespace Fusion
{
    float StanceHeightScale(Stance s)
    {
        switch (s)
        {
            case Stance::Stand:  return 1.f;
            case Stance::Crouch:
            case Stance::Slide:  return 0.65f;
            case Stance::Prone:  return 0.30f;
        }
        return 1.f;
    }

    float SpeedMultiplier(MovementState const& m, WeaponProfile const* held)
    {
        float stance = 1.f;
        switch (m.stance)
        {
            case Stance::Stand:  stance = 1.f; break;
            case Stance::Crouch: stance = 0.6f; break;
            case Stance::Prone:  stance = 0.15f; break; // MW2 player_proneSpeedScale
            case Stance::Slide:  stance = 1.5f; break;
        }

        float gait = 1.f;
        switch (m.gait)
        {
            case Gait::Still:     gait = 0.f; break;
            case Gait::Walk:      gait = 0.5f; break;
            case Gait::Run:       gait = 1.f; break;
            case Gait::Backpedal: gait = 0.85f; break;
            case Gait::Sprint:
                // No sprinting while aiming, crouched or prone: fall back to a run.
                gait = (m.aiming || m.stance != Stance::Stand) ? 1.f : kSprintSpeedScale;
                break;
        }

        float weapon = held ? held->moveSpeedScale : 1.f;
        if (m.aiming && held)
            weapon *= held->adsMoveSpeedScale;
        float perk = m.lightweight ? kLightweightSpeedScale : 1.f;
        return stance * gait * weapon * perk;
    }

    bool SprintStamina::Update(float dt, bool sprinting, bool marathon)
    {
        if (marathon)
        {
            current = max;
            return true;
        }
        if (sprinting)
        {
            current = std::max(0.f, current - dt);
            return current > 0.f;
        }
        current = std::min(max, current + regenPerSecond * dt);
        return true;
    }

    bool SprintOutReady(float secondsSinceSprint, bool sprinting)
    {
        return !sprinting && secondsSinceSprint >= kSprintOutTime;
    }

    bool CanMoveAction(ActorState const& s, MoveAction a)
    {
        if (!s.alive || s.mounted || s.taxi || s.swimming || s.inSpline)
            return false;
        if (s.stunned || s.rooted || s.confused || s.fleeing)
            return false;
        // The lunge is a ground move: from a jump or a fall its arc flings the player, and from a
        // crouch, prone or a slide it dashes a body that is not up to strike.
        return a != MoveAction::Lunge || (!s.pacified && !s.airborne && s.stance == Stance::Stand);
    }

    bool CanFire(ActorState const& s)
    {
        return s.alive && !s.stunned && !s.confused && !s.fleeing;
    }

    std::optional<float> MantleRise(MantleProbe const& p)
    {
        if (p.airSeconds > kMantleMaxAirSeconds || !p.headroomClear || p.startHeight > kMantleMaxStartHeight)
            return std::nullopt;
        float best = 0.f;
        for (float ground : p.groundAhead)
            best = std::max(best, ground - p.feetZ);
        if (best < kMantleMinRise || best > kMantleMaxRise)
            return std::nullopt;
        return best;
    }

    Impulse MantleImpulse(float rise)
    {
        // Up fast enough to peak 0.4 yd over the ledge, and forward fast enough to be over it by
        // the peak (on the way down the body is already above the ledge, not against its face),
        // and never slower than a run.
        float const vertical = std::sqrt(2.f * kWowGravity * (rise + 0.4f));
        float const timeToPeak = vertical / kWowGravity;
        return {std::max(kMantleMinForward, kMantleReach / timeToPeak), vertical};
    }

    float LungeRange(PerkFlags const& perks)
    {
        // Five times MW2's 4 yards (8 with Commando), by request.
        return perks.commando ? 40.f : 20.f;
    }

    bool InLungeCone(Vec3 from, float facing, Vec3 to)
    {
        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.f)
            return true;
        float const cosAngle = (dx * std::cos(facing) + dy * std::sin(facing)) / len;
        return cosAngle >= std::cos(30.f * 3.14159265f / 180.f);
    }

    Impulse LungeImpulse(float distance)
    {
        // Quick over the short reach, capped over the long one; the arc is sized so its airtime
        // carries the body the whole way, since a knock-back moves it only while airborne.
        float const horizontal = std::clamp(distance / 0.35f, 12.f, 40.f);
        float const airtime = distance / horizontal;
        return {horizontal, kWowGravity * airtime / 2.f};
    }

    float LungeCreatureDamage(float bodyShotHealth)
    {
        return 3.f * bodyShotHealth;
    }

    bool SwingLunges(LungeGrant const& grant)
    {
        return grant.charge;
    }
}
