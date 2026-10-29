/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Riding.h"

#include <cstring>

namespace Fusion { namespace Riding
{
    float OriginTolerance(bool mounted)
    {
        return mounted ? kMountedOriginTolerance : 3.f;
    }

    float KnockOffChance(RiderHit const& h)
    {
        if (h.damage <= 0.f)
            return 0.f;
        if (h.sniper || h.explosive)
            return 1.f;
        float const share = h.damage / std::max(1.f, h.maxHealth);
        if (share < kKnockOffMinShare)
            return 0.f;
        return Clamp(share * kKnockOffPerShare, 0.f, 1.f);
    }

    bool KnockedOff(RiderHit const& h, float roll)
    {
        return roll < KnockOffChance(h);
    }

    std::vector<LaunchPoint> const& DefaultLaunchPoints()
    {
        // Heights from the server's height maps (or the perch's flight master where it stands
        // on a building); ceilings well under where each place's sky gives out. VERIFY in game.
        static std::vector<LaunchPoint> const points{
            {1, "the Stormwind gryphon tower", 0, -8835.8f, 490.1f, 109.7f, 10.f, 18.f, 190.f, 300.f},
            {2, "the Dun Morogh airfield", 0, -4790.f, -1660.f, 504.f, 30.f, 20.f, 640.f, 450.f},
            {3, "the Orgrimmar wind rider tower", 1, 1676.2f, -4313.5f, 61.7f, 10.f, 18.f, 150.f, 300.f},
            {4, "the Thunder Bluff wind rider rise", 1, -1196.8f, 26.1f, 177.0f, 10.f, 18.f, 260.f, 300.f},
            {5, "the ruins of Lordaeron", 0, 1630.f, 238.f, 61.7f, 15.f, 15.f, 140.f, 250.f},
            {6, "Rut'theran Village", 1, 8640.6f, 841.1f, 23.4f, 10.f, 15.f, 120.f, 300.f},
            {7, "the Hyjal homesteads' gate", 1, 5385.f, -3710.f, 1594.2f, 15.f, 15.f, 1700.f, 600.f},
            {8, "the Hyjal ledge over Felwood", 1, 5234.f, -1220.f, 1366.8f, 15.f, 20.f, 1420.f, 500.f},
            {9, "the Azshara Crater hub", 37, 128.f, 135.f, 236.f, 15.f, 20.f, 600.f, 900.f},
            {10, "the Thalassian plateau's gate", 0, 3738.f, -4300.f, 129.f, 15.f, 20.f, 260.f, 700.f},
            {11, "Light's Hope Chapel", 0, 2269.9f, -5345.4f, 87.f, 10.f, 15.f, 200.f, 300.f},
        };
        return points;
    }

    LaunchPoint const* LaunchAt(std::vector<LaunchPoint> const& points, uint32_t map, float x, float y, float z)
    {
        LaunchPoint const* best = nullptr;
        float bestD = 0.f;
        for (LaunchPoint const& p : points)
        {
            if (p.map != map || std::fabs(z - p.z) > kLaunchSlackZ)
                continue;
            float const d = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
            if (d <= p.radius * p.radius && (!best || d < bestD))
            {
                best = &p;
                bestD = d;
            }
        }
        return best;
    }

    std::vector<std::string> Problems(std::vector<LaunchPoint> const& points)
    {
        std::vector<std::string> out;
        for (size_t i = 0; i < points.size(); ++i)
        {
            LaunchPoint const& p = points[i];
            std::string const name = "launch point " + std::to_string(p.id);
            if (p.radius <= 0.f || p.range <= p.radius)
                out.push_back(name + " has no room");
            if (p.burstSeconds <= 0.f || p.burstSeconds > kMaxBurstSeconds)
                out.push_back(name + " flies too long or not at all");
            if (p.ceilingZ <= p.z + 5.f)
                out.push_back(name + " has its ceiling under it");
            for (size_t j = i + 1; j < points.size(); ++j)
                if (points[j].id == p.id)
                    out.push_back(name + " shares its id");
        }
        return out;
    }

    namespace
    {
        void Enter(Flight& f, Phase phase, uint32_t now, float seconds)
        {
            f.phase = phase;
            f.sinceMs = now;
            f.untilMs = now + uint32_t(seconds * 1000.f);
        }

        bool Due(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }

        // Into a skyride at the mount's speed; with an Ascent, its energy granted and the landing
        // held off till the mount is up.
        void BeginSky(Flight& f, Sense const& s, bool ascent)
        {
            Enter(f, Phase::Skyride, s.nowMs, Skyriding::kMaxFlightSeconds);
            f.map = s.map;
            f.broke = false;
            f.wardBroke = false;
            f.ward = Wards::Ward{};
            Skyriding::Begin(f.sky, s.nowMs, s.x, s.y, s.z, s.speed);
            f.landAfterMs = s.nowMs;
            if (ascent)
            {
                Skyriding::Grant(f.sky, Skyriding::Move::Ascent);
                f.landAfterMs = s.nowMs + uint32_t(kTakeOffGraceSeconds * 1000.f);
            }
        }
    }

    void Step(Flight& f, Sense const& s)
    {
        float const height = s.z - s.groundZ;
        bool const landed = !s.airborne && height < kLandedHeight;
        if (landed || !s.alive)
            f.glideSpent = false;
        Skyriding::Recharge(f.vigor, s.nowMs, f.phase == Phase::Skyride);
        f.wardPushed = false;
        if (f.phase != Phase::Skyride)
            f.ward = Wards::Ward{};
        if (!s.alive || s.exempt)
        {
            f.phase = Phase::None;
            return;
        }
        switch (f.phase)
        {
            case Phase::None:
                if (!s.mounted)
                    return;
                if (s.skyrider)
                {
                    // A launch point is an updraft: a free Ascent. Off a height, the skyride.
                    if (s.jumped && s.launch && Due(s.nowMs, f.nextLaunchMs))
                    {
                        BeginSky(f, s, true);
                        f.point = s.launch->id;
                        f.nextLaunchMs = s.nowMs + uint32_t(kLaunchCooldownSeconds * 1000.f);
                    }
                    else if (s.airborne && height >= kGlideMinHeight && !f.glideSpent && !s.inWater)
                        BeginSky(f, s, false);
                    return;
                }
                if (s.jumped && s.launch && Due(s.nowMs, f.nextLaunchMs))
                {
                    LaunchPoint const& p = *s.launch;
                    Enter(f, Phase::Burst, s.nowMs, std::min(p.burstSeconds, kMaxBurstSeconds));
                    f.point = p.id;
                    f.map = s.map;
                    f.originX = p.x;
                    f.originY = p.y;
                    f.range = p.range;
                    f.ceilingZ = p.ceilingZ;
                    f.bounded = s.site != nullptr;
                    if (s.site)
                    {
                        f.bounds = s.site->bounds;
                        f.ceilingZ = std::min(f.ceilingZ, s.site->ceilingZ);
                    }
                    return;
                }
                if (s.airborne && height >= kGlideMinHeight && !f.glideSpent)
                    Enter(f, Phase::Glide, s.nowMs, kGlideSeconds);
                return;
            case Phase::Glide:
                if (!s.mounted || landed || Due(s.nowMs, f.untilMs))
                {
                    f.glideSpent = !landed;
                    f.phase = Phase::None;
                }
                return;
            case Phase::Burst:
            {
                float const dx = s.x - f.originX, dy = s.y - f.originY;
                bool const out = s.map != f.map || dx * dx + dy * dy > f.range * f.range
                    || (f.bounded && !f.bounds.Contains(s.x, s.y));
                if (!s.mounted || out || s.z > f.ceilingZ || Due(s.nowMs, f.untilMs))
                {
                    // Down gently, wherever it ended; no take-off again for a while.
                    Enter(f, Phase::Descent, s.nowMs, kDescentSeconds);
                    f.nextLaunchMs = s.nowMs + uint32_t(kLaunchCooldownSeconds * 1000.f);
                }
                return;
            }
            case Phase::Skyride:
                if (s.inWater)
                {
                    f.phase = Phase::None;
                    return;
                }
                // Down: the flight is over (the client sees no airborne flag in GM flight, so
                // only the height counts), once the take-off has had its time.
                if (height < kLandedHeight && Due(s.nowMs, f.landAfterMs))
                {
                    f.glideSpent = false;
                    f.phase = Phase::None;
                    return;
                }
                // Off the mount, out of time, or flying against the law: down gently.
                f.broke = !Skyriding::Check(f.sky, s.nowMs, s.x, s.y, s.z);
                // Warded airspace: down under the ceiling at the push's pace, or down for good.
                if (!Wards::Hold(f.ward, s.wardZone, s.wardCeilingZ, s.z, s.nowMs, f.wardPushed))
                {
                    f.broke = true;
                    f.wardBroke = true;
                }
                if (!s.mounted || f.broke || s.map != f.map || Due(s.nowMs, f.untilMs))
                    Enter(f, Phase::Descent, s.nowMs, kDescentSeconds);
                return;
            case Phase::Descent:
                if (Due(s.nowMs, f.untilMs) || (landed && Due(s.nowMs, f.sinceMs + uint32_t(kMinDescentSeconds * 1000.f))))
                {
                    f.glideSpent = !landed;
                    f.phase = Phase::None;
                }
                return;
        }
    }

    bool SkyMove(Flight& f, Sense const& s, Skyriding::Move m)
    {
        if (!s.alive || s.exempt || !s.mounted || !s.skyrider || s.inWater)
            return false;
        bool const flying = f.phase == Phase::Skyride;
        // A Surge needs a skyride under way; an Ascent takes off from anything but a burst.
        if (m == Skyriding::Move::Surge ? !flying : f.phase == Phase::Burst)
            return false;
        Skyriding::Recharge(f.vigor, s.nowMs, flying);
        if (!Skyriding::Spend(f.vigor, s.nowMs))
            return false;
        if (flying)
        {
            Skyriding::Grant(f.sky, m);
            if (m == Skyriding::Move::Ascent)
                f.landAfterMs = s.nowMs + uint32_t(kTakeOffGraceSeconds * 1000.f);
        }
        else
            BeginSky(f, s, true);
        return true;
    }

    float SecondsLeft(Flight const& f, uint32_t nowMs)
    {
        if (f.phase == Phase::None || Due(nowMs, f.untilMs))
            return 0.f;
        return float(f.untilMs - nowMs) / 1000.f;
    }

    std::vector<uint8_t> EncodeFlight(Flight const& f, uint32_t nowMs)
    {
        std::vector<uint8_t> o{kOpFlight, uint8_t(f.phase)};
        auto putF = [&o](float v) {
            uint32_t u;
            std::memcpy(&u, &v, 4);
            for (int i = 0; i < 4; ++i)
                o.push_back(uint8_t(u >> (8 * i)));
        };
        putF(SecondsLeft(f, nowMs));
        putF(f.phase == Phase::Burst ? f.ceilingZ : 0.f);
        return o;
    }
}}
