/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Skyriding.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fusion { namespace Skyriding
{
    namespace
    {
        bool Due(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }
    }

    float ClampPitch(float pitch)
    {
        return std::min(std::max(pitch, kMinPitch), kMaxPitch);
    }

    Motion Advance(Body& b, float pitch, float dt)
    {
        if (dt <= 0.f)
            return {};
        pitch = ClampPitch(pitch);
        float const sp = std::sin(pitch), cp = std::cos(pitch);
        if (b.ascentLeft > 0.f)
        {
            // Straight up, the speed kept but for the drag; out of it at the exit speed at least.
            float const t = std::min(dt, b.ascentLeft);
            b.ascentLeft -= t;
            b.speed = std::max(0.f, b.speed - kDrag * b.speed * dt);
            if (b.ascentLeft <= 1e-4f)
            {
                b.ascentLeft = 0.f;
                b.speed = std::max(b.speed, kAscentExitSpeed);
            }
            return {b.speed * cp, kAscentRate * t / dt};
        }
        // Along the path: gravity pulls a dive on and a climb back, drag bleeds the speed.
        b.speed = std::min(std::max(b.speed + (-kGravity * sp - kDrag * b.speed) * dt, 0.f), kMaxSpeed);
        float const sink = kSink + std::max(0.f, kStallSpeed - b.speed) * kStallSink;
        return {b.speed * cp, b.speed * sp - sink};
    }

    void Ascend(Body& b)
    {
        b.ascentLeft = kAscentSeconds;
    }

    void Surge(Body& b)
    {
        b.speed = std::min(b.speed + kSurgeSpeed, kMaxSpeed);
    }

    float Energy(float z, float speed)
    {
        return kGravity * z + 0.5f * speed * speed;
    }

    void Recharge(Vigor& v, uint32_t nowMs, bool flying)
    {
        if (v.lastMs == 0)
        {
            v.lastMs = nowMs;
            return;
        }
        float const dt = float(int32_t(nowMs - v.lastMs)) / 1000.f;
        v.lastMs = nowMs;
        if (v.charges >= kMaxCharges || dt <= 0.f)
        {
            if (v.charges >= kMaxCharges)
                v.progress = 0.f;
            return;
        }
        v.progress += dt / (flying ? kChargeAirSeconds : kChargeGroundSeconds);
        while (v.progress >= 1.f && v.charges < kMaxCharges)
        {
            ++v.charges;
            v.progress -= 1.f;
        }
        if (v.charges >= kMaxCharges)
            v.progress = 0.f;
    }

    bool Spend(Vigor& v, uint32_t nowMs)
    {
        if (v.charges == 0 || !Due(nowMs, v.readyMs))
            return false;
        --v.charges;
        v.readyMs = nowMs + uint32_t(kMoveGapSeconds * 1000.f);
        return true;
    }

    void Begin(Envelope& e, uint32_t nowMs, float x, float y, float z, float speed)
    {
        e = Envelope{};
        speed = std::min(std::max(speed, 0.f), kMaxSpeed);
        e.budget = Energy(z, speed);
        e.sampleMs = nowMs;
        e.x = x;
        e.y = y;
        e.z = z;
        e.speed = e.lastSpeed = speed;
    }

    float Gain(Move m, float speed)
    {
        if (m == Move::Ascent)
            return kGravity * kAscentRate * kAscentSeconds + 0.5f * kAscentExitSpeed * kAscentExitSpeed;
        // (v + s)^2 / 2 - v^2 / 2 at the measured speed and its slack.
        return kSurgeSpeed * (std::max(speed, 0.f) + kSlackSpeed) + 0.5f * kSurgeSpeed * kSurgeSpeed;
    }

    void Grant(Envelope& e, Move m)
    {
        e.budget += Gain(m, e.speed);
    }

    bool Check(Envelope& e, uint32_t nowMs, float x, float y, float z)
    {
        uint32_t const dtMs = nowMs - e.sampleMs;
        if (int32_t(dtMs) < int32_t(kSampleMs))
            return e.strikes < kStrikes;
        float const dt = float(dtMs) / 1000.f;
        float const d = std::sqrt((x - e.x) * (x - e.x) + (y - e.y) * (y - e.y) + (z - e.z) * (z - e.z));
        float const v = std::min(d / dt, kMaxSpeed * 1.5f);
        e.lastSpeed = e.speed;
        e.speed = v;
        // The least the law takes: most of the sink, half the drag.
        float const drag = std::min(v, kMaxSpeed);
        e.budget -= (0.8f * kGravity * kSink + 0.5f * kDrag * drag * drag) * dt;
        e.sampleMs = nowMs;
        e.x = x;
        e.y = y;
        e.z = z;
        float const fast = std::max(0.f, std::min(e.speed, e.lastSpeed) - kSlackSpeed);
        bool const over = kGravity * z + 0.5f * fast * fast > e.budget + kGravity * kSlackHeight;
        e.strikes = over ? uint8_t(std::min<int>(e.strikes + 1, 255)) : uint8_t(0);
        return e.strikes < kStrikes;
    }

    std::vector<uint8_t> EncodeSky(uint8_t flags, Vigor const& v, float speed)
    {
        std::vector<uint8_t> o{kOpSky, flags, v.charges, kMaxCharges};
        auto putF = [&o](float f) {
            uint32_t u;
            std::memcpy(&u, &f, 4);
            for (int i = 0; i < 4; ++i)
                o.push_back(uint8_t(u >> (8 * i)));
        };
        putF(v.progress);
        putF(speed);
        return o;
    }

    bool DecodeSkyMove(std::vector<uint8_t> const& body, Move& out)
    {
        if (body.size() != 2 || body[0] != kOpSkyMove || (body[1] != 1 && body[1] != 2))
            return false;
        out = Move(body[1]);
        return true;
    }
}}
