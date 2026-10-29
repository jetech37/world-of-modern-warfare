/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Ballistics.h"
#include "fusion/Gunfights.h"

#include <algorithm>
#include <cmath>

namespace Fusion
{
    uint32_t PelletHash(uint32_t seed, uint32_t index)
    {
        uint32_t x = seed ^ (index * 0x9E3779B9u);
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    Vec3 PelletDirection(Vec3 dir, uint32_t seed, uint32_t index, float spreadDeg)
    {
        if (!(spreadDeg > 0.f))
            return dir;
        uint32_t const h = PelletHash(seed, index);
        float const u = float(h & 0xFFFFu) / 65536.f;
        float const v = float(h >> 16) / 65536.f;
        float const r = std::tan(spreadDeg * 3.14159265f / 180.f) * std::sqrt(u);
        float const theta = v * 6.28318531f;
        auto cross = [](Vec3 a, Vec3 b) {
            return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        };
        Vec3 const side = cross(dir, {0.f, 0.f, 1.f});
        Vec3 const right = side.Dot(side) > 1e-8f ? side.Normalized() : Vec3{1.f, 0.f, 0.f};
        Vec3 const up = cross(right, dir);
        return (dir + right * (r * std::cos(theta)) + up * (r * std::sin(theta))).Normalized();
    }

    float AimedSpread(WeaponProfile const& w)
    {
        switch (w.weaponClass)
        {
            case WeaponClass::AssaultRifle:
            case WeaponClass::Lmg:
            case WeaponClass::Sniper:
                return std::min(w.adsSpread, kGunfight.adsCapDeg);
            default:
                return w.adsSpread;
        }
    }

    float LeastSpread(WeaponProfile const& w, bool ads)
    {
        return ads ? AimedSpread(w) * 0.9f : w.hipSpreadMin * kGunfight.hipSpreadScale * 0.5f * 0.65f;
    }

    float ClampSpread(WeaponProfile const& w, bool ads, float claimedDeg)
    {
        float const least = LeastSpread(w, ads);
        if (!(claimedDeg >= least))     // also a NaN
            return least;
        return std::min(claimedDeg, kMaxSpreadDeg);
    }

    float PelletRange(WeaponProfile const& w)
    {
        return w.weaponClass == WeaponClass::Shotgun ? w.minDamageRange * FalloffRangeScale(w.weaponClass) * kShotgunReach : kMaxShotRange;
    }

    float BodyLift(uint32_t family, bool canFly, bool hovering)
    {
        // CreatureFamily.dbc: carrion bird 7, bat 24, owl 26, wind serpent 27.
        bool const winged = family == 7 || family == 24 || family == 26 || family == 27;
        return (winged || canFly || hovering) ? kFlyerLift : 0.f;
    }

    int BodiesPierced(WeaponProfile const& w)
    {
        return w.fmj ? 2 : 1;
    }

    bool IsExplosive(WeaponProfile const& w)
    {
        return w.explosionRadius > 0.f;
    }

    float SplashDamage(WeaponProfile const& w, float distance)
    {
        if (!IsExplosive(w) || distance > w.explosionRadius)
            return 0.f;
        return Lerp(w.explosionInner, w.explosionOuter, Clamp(distance / w.explosionRadius, 0.f, 1.f));
    }

    float PierceScale(int index)
    {
        return index == 0 ? 1.f : kFmjBodyScale;
    }

    bool ThroughWall(WeaponProfile const& w, float thickness)
    {
        return w.fmj && thickness <= kFmjWallDepth;
    }

    int NearestCandidate(std::vector<PelletCandidate> const& c)
    {
        int best = -1;
        for (size_t i = 0; i < c.size(); ++i)
            if (best < 0 || c[i].distance < c[size_t(best)].distance)
                best = int(i);
        return best;
    }
}
