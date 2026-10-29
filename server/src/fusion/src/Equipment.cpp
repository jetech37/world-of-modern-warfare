/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Equipment.h"
#include "fusion/Movement.h"

#include <cmath>

namespace Fusion
{
    EquipmentKind KindOf(std::string const& n)
    {
        if (n == "frag_grenade_mp") return EquipmentKind::Frag;
        if (n == "semtex_mp") return EquipmentKind::Semtex;
        if (n == "throwingknife_mp") return EquipmentKind::ThrowingKnife;
        if (n == "claymore_mp") return EquipmentKind::Claymore;
        if (n == "c4_mp") return EquipmentKind::C4;
        if (n == "flash_grenade_mp") return EquipmentKind::Flash;
        if (n == "concussion_grenade_mp") return EquipmentKind::Stun;
        if (n == "smoke_grenade_mp") return EquipmentKind::Smoke;
        return EquipmentKind::None;
    }

    bool IsThrown(EquipmentKind k)
    {
        return k != EquipmentKind::None && k != EquipmentKind::Claymore;
    }

    float FuseSeconds(EquipmentKind k)
    {
        switch (k)
        {
            case EquipmentKind::Frag: return 3.5f;
            case EquipmentKind::Semtex: return 2.5f;
            case EquipmentKind::Flash: return 1.5f;
            case EquipmentKind::Stun: return 1.5f;
            case EquipmentKind::Smoke: return 1.f;
            default: return -1.f;
        }
    }

    Throw ThrowArc(Vec3 origin, Vec3 dir, float speed,
        std::function<std::optional<Vec3>(Vec3, Vec3)> const& hit)
    {
        float const step = 0.02f;
        Vec3 p = origin;
        Vec3 v = dir.Normalized() * speed;
        Throw out;
        for (float t = 0.f; t < kMaxFlightSeconds; t += step)
        {
            Vec3 const next = p + v * step + Vec3{0.f, 0.f, -0.5f * kWowGravity * step * step};
            if (std::optional<Vec3> const at = hit(p, next))
            {
                out.at = *at;
                out.seconds = t + step * ((*at - p).Length() / std::max(1e-4f, (next - p).Length()));
                out.landed = true;
                return out;
            }
            p = next;
            v.z -= kWowGravity * step;
        }
        out.at = p;
        out.seconds = kMaxFlightSeconds;
        return out;
    }

    bool ClaymoreTripped(Vec3 at, float facing, Vec3 body, float range)
    {
        float const dx = body.x - at.x;
        float const dy = body.y - at.y;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (d > range || d < 1e-3f || std::fabs(body.z - at.z) > 2.5f)
            return false;
        float const cosAngle = (dx * std::cos(facing) + dy * std::sin(facing)) / d;
        return cosAngle >= std::cos(kClaymoreHalfAngle * 3.14159265f / 180.f);
    }
}
