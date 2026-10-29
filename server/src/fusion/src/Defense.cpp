/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Defense.h"

#include <algorithm>

namespace Fusion { namespace Defense
{
    bool SentryCovers(Vec3 at, float facing, Vec3 to)
    {
        Vec3 const d = to - at;
        float const flat = std::sqrt(d.x * d.x + d.y * d.y);
        if (flat < 0.3f)
            return std::fabs(d.z) < 0.3f;   // on top of it: straight up or down is out of its arc
        float yaw = std::atan2(d.y, d.x) - facing;
        yaw = std::remainder(yaw, 2.f * 3.14159265f);
        float const pitch = std::atan2(d.z, flat);
        return std::fabs(yaw) <= kSentryYawArc && std::fabs(pitch) <= kSentryPitchArc;
    }

    Vec3 StreakSentrySpot(Vec3 feet, float facing)
    {
        return {feet.x + std::cos(facing) * kStreakSentryAhead, feet.y + std::sin(facing) * kStreakSentryAhead, feet.z};
    }

    bool BaseQualifies(uint32_t piecesNear, uint32_t playerLevel)
    {
        return piecesNear >= kBasePieces && playerLevel >= 5;
    }

    uint32_t RaidSize(uint32_t piecesNear)
    {
        return std::min<uint32_t>(8, 3 + piecesNear / 12);
    }

    uint32_t NextRaidDelayMs(float roll)
    {
        float const r = Clamp(roll, 0.f, 1.f);
        return kRaidMinMs + uint32_t(float(kRaidMaxMs - kRaidMinMs) * r);
    }

    RaidStep Advance(RaidClock& clock, uint32_t nowMs, bool hasBase, uint32_t waveLeft, float roll)
    {
        auto due = [&] { return int32_t(nowMs - clock.nextMs) >= 0; };
        switch (clock.phase)
        {
            case RaidPhase::Quiet:
                if (!hasBase)
                {
                    clock.armed = false;
                    return RaidStep::None;
                }
                if (!clock.armed)
                {
                    clock.armed = true;
                    clock.nextMs = nowMs + kFirstRaidMs;
                    return RaidStep::None;
                }
                if (!due())
                    return RaidStep::None;
                clock.phase = RaidPhase::Warned;
                clock.nextMs = nowMs + kRaidWarningMs;
                return RaidStep::Warn;
            case RaidPhase::Warned:
                if (!due())
                    return RaidStep::None;
                clock.phase = RaidPhase::Fighting;
                clock.nextMs = nowMs + kRaidLengthMs;
                return RaidStep::Spawn;
            case RaidPhase::Fighting:
                if (waveLeft > 0 && !due())
                    return RaidStep::None;
                clock.phase = RaidPhase::Quiet;
                clock.nextMs = nowMs + NextRaidDelayMs(roll);
                return RaidStep::End;
        }
        return RaidStep::None;
    }
}}
