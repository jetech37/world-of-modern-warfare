/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/HitValidation.h"

#include <cmath>

namespace Fusion
{
    namespace
    {
        constexpr Vec3 kUp{0.f, 0.f, 1.f};  // the server's world is z-up

        // Ray against a capsule from a to b (Inigo Quilez's closed form); dir is normalized.
        // Returns the distance along the ray, or a negative number on a miss.
        float RayCapsule(Vec3 ro, Vec3 rd, Vec3 pa, Vec3 pb, float r)
        {
            Vec3 ba = pb - pa;
            Vec3 oa = ro - pa;
            float baba = ba.Dot(ba);
            float bard = ba.Dot(rd);
            float baoa = ba.Dot(oa);
            float rdoa = rd.Dot(oa);
            float oaoa = oa.Dot(oa);
            float a = baba - bard * bard;
            float b = baba * rdoa - baoa * bard;
            float c = baba * oaoa - baoa * baoa - r * r * baba;
            float h = b * b - a * c;
            if (std::fabs(a) > 1e-8f && h >= 0.f)
            {
                float t = (-b - std::sqrt(h)) / a;
                float y = baoa + t * bard;
                if (y > 0.f && y < baba && t >= 0.f)
                    return t;
            }
            // The caps: whichever sphere the ray meets first.
            float best = -1.f;
            for (Vec3 centre : {pa, pb})
            {
                Vec3 oc = ro - centre;
                float bb = rd.Dot(oc);
                float cc = oc.Dot(oc) - r * r;
                float hh = bb * bb - cc;
                if (hh < 0.f)
                    continue;
                float t = -bb - std::sqrt(hh);
                if (t >= 0.f && (best < 0.f || t < best))
                    best = t;
            }
            return best;
        }

        float RaySphere(Vec3 ro, Vec3 rd, Vec3 centre, float r)
        {
            Vec3 oc = ro - centre;
            float b = rd.Dot(oc);
            float c = oc.Dot(oc) - r * r;
            float h = b * b - c;
            if (h < 0.f)
                return -1.f;
            float t = -b - std::sqrt(h);
            return t >= 0.f ? t : -1.f;
        }

        struct Geometry
        {
            Vec3 a, b;          // the capsule's axis, feet end to head end
            float radius;
            Vec3 head;
            float headRadius;
            bool prone;
            float length;       // axis length plus both radii
        };

        Geometry Build(BodyPose const& pose, HitboxShape const& s)
        {
            Geometry g{};
            g.headRadius = s.headRadius;
            float scale = StanceHeightScale(pose.stance);
            if (pose.stance == Stance::Prone)
            {
                // Lying along the facing, head forward, a body's width off the ground.
                Vec3 forward{std::cos(pose.orientation), std::sin(pose.orientation), 0.f};
                float r = std::min(s.radius, s.height * scale * 0.5f);
                g.radius = r;
                g.prone = true;
                Vec3 base = pose.feet + kUp * r;
                g.a = base + forward * r;
                g.b = base + forward * (s.height - r);
                g.head = base + forward * (s.height - s.headOffset);
                g.length = s.height;
                return g;
            }
            float h = s.height * scale;
            g.radius = s.radius;
            g.prone = false;
            g.a = pose.feet + kUp * s.radius;
            g.b = pose.feet + kUp * std::max(s.radius, h - s.radius);
            g.head = pose.feet + kUp * (h - s.headOffset * scale);
            g.length = h;
            return g;
        }

        HitLocation Classify(Geometry const& g, BodyPose const& pose, Vec3 origin, Vec3 rd, Vec3 point)
        {
            // How far along the body (feet 0, top 1) the hit landed.
            float along;
            float off;  // distance from the axis
            if (g.prone)
            {
                Vec3 axis = (g.b - g.a).Normalized();
                Vec3 base = pose.feet + kUp * g.radius;
                along = Clamp((point - base).Dot(axis) / g.length, 0.f, 1.f);
                Vec3 onAxis = base + axis * (point - base).Dot(axis);
                off = (point - onAxis).Length();
            }
            else
            {
                along = Clamp((point.z - pose.feet.z) / g.length, 0.f, 1.f);
                // How far off-centre the ray passed, seen along it: a graze at the side is an arm,
                // a hit through the middle the torso, whichever side it came from.
                Vec3 d{rd.x, rd.y, 0.f};
                float dl = d.Length();
                if (dl > 1e-3f)
                {
                    d = d * (1.f / dl);
                    Vec3 v{pose.feet.x - origin.x, pose.feet.y - origin.y, 0.f};
                    off = std::fabs(v.x * d.y - v.y * d.x);
                }
                else
                {
                    off = Vec3{point.x - pose.feet.x, point.y - pose.feet.y, 0.f}.Length();
                }
            }
            if (along >= 0.82f)
                return HitLocation::Neck;
            bool outer = off > g.radius * 0.75f;
            if (along >= 0.55f)
                return outer && !g.prone ? HitLocation::Arm : HitLocation::TorsoUpper;
            if (along >= 0.42f)
                return outer && !g.prone ? HitLocation::Arm : HitLocation::TorsoLower;
            return HitLocation::Leg;
        }
    }

    std::optional<RayHit> RayVsBody(Vec3 origin, Vec3 dir, BodyPose const& pose, HitboxShape const& shape)
    {
        Vec3 rd = dir.Normalized();
        Geometry g = Build(pose, shape);
        float tHead = RaySphere(origin, rd, g.head, g.headRadius);
        float tBody = RayCapsule(origin, rd, g.a, g.b, g.radius);
        if (tHead < 0.f && tBody < 0.f)
            return std::nullopt;
        Vec3 point = origin + rd * std::max(tBody, 0.f);
        HitLocation where = tBody >= 0.f ? Classify(g, pose, origin, rd, point) : HitLocation::Head;
        // The head sits inside the capsule's top: a ray that meets the head sphere having entered
        // the body at the neck or above (or not at all) is a headshot. One that enters through the
        // chest and climbs into the head from inside is not.
        if (tHead >= 0.f && (tBody < 0.f || where == HitLocation::Neck || tHead <= tBody + g.headRadius))
            return RayHit{tBody >= 0.f ? std::min(tBody, tHead) : tHead, HitLocation::Head};
        if (tBody < 0.f)
            return std::nullopt;
        return RayHit{tBody, where};
    }

    float RayHeadMiss(Vec3 origin, Vec3 dir, BodyPose const& pose, HitboxShape const& shape)
    {
        Vec3 rd = dir.Normalized();
        Vec3 head = Build(pose, shape).head;
        float t = std::max(0.f, (head - origin).Dot(rd));
        return (origin + rd * t - head).Length();
    }

    void PoseHistory::Record(uint32_t timeMs, BodyPose const& pose)
    {
        if (!m_samples.empty() && int32_t(timeMs - m_samples.back().time) < 0)
            return;  // out of order: keep the timeline monotonic
        m_samples.push_back({timeMs, pose});
        while (m_samples.size() > 1 && int32_t(timeMs - m_samples.front().time) > int32_t(kWindowMs))
            m_samples.pop_front();
    }

    std::optional<BodyPose> PoseHistory::At(uint32_t timeMs) const
    {
        if (m_samples.empty())
            return std::nullopt;
        Sample const& newest = m_samples.back();
        int32_t ahead = int32_t(timeMs - newest.time);
        if (ahead >= 0)
            return ahead <= 100 ? std::optional<BodyPose>(newest.pose) : std::nullopt;
        if (int32_t(timeMs - m_samples.front().time) < 0)
            return std::nullopt;
        for (size_t i = m_samples.size() - 1; i > 0; --i)
        {
            Sample const& hi = m_samples[i];
            Sample const& lo = m_samples[i - 1];
            if (int32_t(timeMs - lo.time) >= 0)
            {
                float span = float(int32_t(hi.time - lo.time));
                float t = span > 0.f ? float(int32_t(timeMs - lo.time)) / span : 0.f;
                BodyPose p = lo.pose;
                p.feet = lo.pose.feet + (hi.pose.feet - lo.pose.feet) * t;
                p.orientation = t < 0.5f ? lo.pose.orientation : hi.pose.orientation;
                p.stance = t < 0.5f ? lo.pose.stance : hi.pose.stance;
                return p;
            }
        }
        return m_samples.front().pose;
    }

    ShotResult ValidateShot(ShotClaim const& shot, ShotContext const& ctx, PoseHistory const& target)
    {
        ShotResult r;
        if (!ctx.weapon)
            return r;
        WeaponProfile const& w = *ctx.weapon;

        // Fire rate, with 20% slack for packet timing.
        if (ctx.hasLastShot)
        {
            int32_t gap = int32_t(shot.shooterTimeMs - ctx.lastShotTimeMs);
            if (gap < int32_t(w.fireTime * 1000.f * 0.8f))
            {
                r.verdict = ShotVerdict::RejectedFireRate;
                return r;
            }
        }
        if (!ctx.magazineHasRound)
        {
            r.verdict = ShotVerdict::RejectedEmpty;
            return r;
        }
        if ((shot.origin - ctx.shooterPosition).Length() > ctx.originTolerance)
        {
            r.verdict = ShotVerdict::RejectedOrigin;
            return r;
        }
        if (int32_t(ctx.serverTimeMs - shot.shooterTimeMs) > int32_t(PoseHistory::kWindowMs))
        {
            r.verdict = ShotVerdict::RejectedTooOld;
            return r;
        }
        std::optional<BodyPose> pose = target.At(shot.shooterTimeMs);
        if (!pose)
        {
            r.verdict = ShotVerdict::RejectedTooOld;
            return r;
        }

        std::optional<RayHit> hit = RayVsBody(shot.origin, shot.direction, *pose, ctx.targetShape);
        if (!hit)
        {
            r.verdict = ShotVerdict::Miss;
            return r;
        }
        if (hit->distance > kMaxShotRange)
        {
            r.verdict = ShotVerdict::RejectedRange;
            return r;
        }
        Vec3 point = shot.origin + shot.direction.Normalized() * hit->distance;
        if (ctx.lineOfSight && !ctx.lineOfSight(shot.origin, point))
        {
            r.verdict = ShotVerdict::RejectedNoSight;
            return r;
        }

        r.verdict = ShotVerdict::Hit;
        r.distance = hit->distance;
        r.location = hit->location;
        // The client hit the animated mesh; accept its headshot when the ray came within the
        // server's head zone plus the tolerance.
        if (shot.claimsHead && r.location != HitLocation::Head)
        {
            float miss = RayHeadMiss(shot.origin, shot.direction, *pose, ctx.targetShape);
            if (miss <= ctx.targetShape.headRadius + kHeadClaimTolerance)
                r.location = HitLocation::Head;
        }
        r.headAccepted = r.location == HitLocation::Head;
        return r;
    }

    Vec3 ShotOrigin(Vec3 claimed, Vec3 eye, float tolerance)
    {
        bool const finite = std::isfinite(claimed.x) && std::isfinite(claimed.y) && std::isfinite(claimed.z);
        if (!finite || !((claimed - eye).Length() <= tolerance))
            return eye;
        return claimed;
    }

    bool GunInHand(uint16_t gunId, uint16_t classPrimary, uint16_t classSecondary, bool pickedUp,
        uint16_t lastStandPistol)
    {
        if (!gunId)
            return false;
        return pickedUp || gunId == classPrimary || gunId == classSecondary || gunId == lastStandPistol;
    }

    bool ShotFresh(uint32_t shotTimeMs, uint32_t nowMs)
    {
        return int32_t(nowMs - shotTimeMs) <= int32_t(PoseHistory::kWindowMs);
    }

    float FireRateSlack(WeaponProfile const& w)
    {
        return w.explosionRadius > 0.f ? kExplosiveFireRateSlack : kFireRateSlack;
    }
}
