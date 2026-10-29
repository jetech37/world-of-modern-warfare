/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Launch.h"

#include <algorithm>
#include <cstring>

namespace Fusion { namespace Launch
{
    namespace
    {
        void Put16(std::vector<uint8_t>& o, uint16_t v)
        {
            o.push_back(uint8_t(v));
            o.push_back(uint8_t(v >> 8));
        }

        void Put32(std::vector<uint8_t>& o, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }

        void PutF(std::vector<uint8_t>& o, float f)
        {
            uint32_t v;
            std::memcpy(&v, &f, 4);
            Put32(o, v);
        }

        void PutV(std::vector<uint8_t>& o, Vec3 v)
        {
            PutF(o, v.x);
            PutF(o, v.y);
            PutF(o, v.z);
        }
    }

    bool IsGrenadeLauncher(WeaponProfile const& w)
    {
        return w.slot == WeaponSlot::Alternate || w.name == "m79_mp" || w.name.rfind("gl_", 0) == 0;
    }

    float ProjectileSpeed(WeaponProfile const& w)
    {
        return IsGrenadeLauncher(w) ? kGrenadeSpeed : kRocketSpeed;
    }

    uint16_t FlightMs(float distance, float speed)
    {
        if (speed <= 0.f)
            return 1;
        float const ms = std::max(0.f, distance) / speed * 1000.f;
        return uint16_t(Clamp(ms + 0.5f, 1.f, 65535.f));
    }

    Vec3 ArcAt(Vec3 origin, Vec3 velocity, float gravity, float t)
    {
        return origin + velocity * t + Vec3{0.f, 0.f, -gravity} * (0.5f * t * t);
    }

    ArcEnd FlyArc(Vec3 origin, Vec3 velocity, float gravity, float step, float maxSeconds, float backOff,
        SegmentContact const& contact)
    {
        ArcEnd e;
        step = std::max(0.001f, step);
        maxSeconds = Clamp(maxSeconds, 0.001f, 65.535f);
        float end = maxSeconds;
        for (float t = 0.f; t < maxSeconds; t += step)
        {
            float const t1 = std::min(maxSeconds, t + step);
            float fraction = 0.f;
            if (contact && contact(ArcAt(origin, velocity, gravity, t), ArcAt(origin, velocity, gravity, t1), fraction))
            {
                e.contact = true;
                end = t + Clamp(fraction, 0.f, 1.f) * (t1 - t);
                // Back off along the arc by the round's speed there.
                Vec3 const v = velocity + Vec3{0.f, 0.f, -gravity * end};
                if (backOff > 0.f && v.Length() > 1e-3f)
                    end = std::max(0.f, end - backOff / v.Length());
                break;
            }
        }
        e.flightMs = uint16_t(Clamp(end * 1000.f + 0.5f, 1.f, 65535.f));
        e.at = ArcAt(origin, velocity, gravity, e.flightMs / 1000.f);
        return e;
    }

    Vec3 LobVelocity(Vec3 from, Vec3 to, float gravity, float seconds)
    {
        // p(T) = from + v T + g T^2 / 2, solved for v.
        float const t = std::max(0.05f, seconds);
        return (to - from - Vec3{0.f, 0.f, -gravity} * (0.5f * t * t)) * (1.f / t);
    }

    std::vector<uint8_t> EncodeExplosion(Vec3 at, float radius, uint16_t weapon)
    {
        std::vector<uint8_t> o{kOpExplosion};
        PutV(o, at);
        PutF(o, radius);
        Put16(o, weapon);
        return o;
    }

    std::vector<uint8_t> EncodeLaunch(uint64_t shooter, uint16_t weapon, Vec3 from, Vec3 to, uint16_t flightMs)
    {
        std::vector<uint8_t> o{kOpLaunch};
        Put32(o, uint32_t(shooter));
        Put32(o, uint32_t(shooter >> 32));
        Put16(o, weapon);
        PutV(o, from);
        PutV(o, to);
        Put16(o, flightMs);
        return o;
    }
}}
