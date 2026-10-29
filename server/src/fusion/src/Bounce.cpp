/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * Reflect's bounce is incidence-weighted.
 */

#include "fusion/Bounce.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fusion { namespace Bounce
{
    namespace
    {
        Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

        // One step of free flight: exact under constant gravity.
        Vec3 Fall(Vec3 p, Vec3 v, float gravity, float s)
        {
            return p + v * s + Vec3{0.f, 0.f, -0.5f * gravity * s * s};
        }

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
    }

    Vec3 Reflect(Vec3 v, Vec3 normal, float parallel, float perpendicular)
    {
        float const speed = v.Length();
        float const nl = normal.Length();
        if (speed < 1e-6f || nl < 1e-6f)
            return {};
        Vec3 const n = normal * (1.f / nl);
        float const dot = v.Dot(n);
        float const incidence = Clamp(-dot / speed, 0.f, 1.f);
        float const factor = parallel + (perpendicular - parallel) * incidence;
        return (v - n * (2.f * dot)) * factor;
    }

    Path Fly(Vec3 origin, Vec3 velocity, float gravity, float fuseS, SegmentHit const& hit)
    {
        Path out;
        out.fuseMs = uint16_t(Clamp(std::max(kMinFuseS, fuseS) * 1000.f + 0.5f, 1.f, 65535.f));
        float const fuse = out.fuseMs / 1000.f;
        out.corners.push_back({0, origin});
        Vec3 p = origin;
        Vec3 v = velocity;
        float t = 0.f;
        uint16_t lastMs = 0;
        while (fuse - t > 1e-6f)
        {
            float const step = std::min(kStepS, fuse - t);
            Vec3 const next = Fall(p, v, gravity, step);
            std::optional<Contact> const c = hit ? hit(p, next) : std::nullopt;
            if (!c)
            {
                p = next;
                v.z -= gravity * step;
                t += step;
                continue;
            }
            // The contact on the arc, and the velocity there.
            float const s = Clamp(c->fraction, 0.f, 1.f) * step;
            Vec3 const at = Fall(p, v, gravity, s);
            Vec3 const vc = v + Vec3{0.f, 0.f, -gravity * s};
            Vec3 n = c->normal.Normalized();
            if (n.Length() < 0.5f)
                n = vc.Length() > 1e-6f ? vc.Normalized() * -1.f : Vec3{0.f, 0.f, 1.f};
            if (n.Dot(vc) > 0.f)
                n = n * -1.f;               // the side it came from
            Vec3 const pos = at + n * kBackOff;
            uint16_t ms = uint16_t(Clamp((t + s) * 1000.f + 0.5f, 0.f, 65535.f));
            ms = std::min<uint16_t>(out.fuseMs, std::max<uint16_t>(ms, uint16_t(lastMs + 1)));
            out.corners.push_back({ms, pos});
            lastMs = ms;
            Vec3 const r = Reflect(vc, n, kParallel, kPerpendicular);
            bool const rests = n.z >= kRestNormalZ && r.Length() < kRestSpeed;
            if (rests || out.corners.size() >= kMaxCorners || ms >= out.fuseMs)
            {
                out.end = pos;
                out.rested = ms < out.fuseMs;
                return out;
            }
            p = pos;
            v = r;
            t = ms / 1000.f;
        }
        // The fuse ran out in the air.
        if (out.corners.back().ms < out.fuseMs)
            out.corners.push_back({out.fuseMs, p});
        out.end = out.corners.back().pos;
        return out;
    }

    Vec3 PathAt(std::vector<Corner> const& corners, float gravity, float ms)
    {
        if (corners.empty())
            return {};
        if (ms <= corners.front().ms)
            return corners.front().pos;
        for (size_t k = 0; k + 1 < corners.size(); ++k)
        {
            Corner const& a = corners[k];
            Corner const& b = corners[k + 1];
            if (ms >= b.ms)
                continue;
            float const T = (b.ms - a.ms) / 1000.f;
            if (T <= 0.f)
                return b.pos;
            float const s = (ms - a.ms) / 1000.f;
            // p(T) = a + v T - g T^2 / 2 z, solved for v.
            Vec3 const v = (b.pos - a.pos + Vec3{0.f, 0.f, 0.5f * gravity * T * T}) * (1.f / T);
            return Fall(a.pos, v, gravity, s);
        }
        return corners.back().pos;
    }

    float CookedFuse(float fuseS, uint32_t cookMs)
    {
        float const cooked = std::min(cookMs / 1000.f, std::max(0.f, fuseS));
        return std::max(kMinFuseS, fuseS - cooked);
    }

    Vec3 GroundNormal(Vec3 at, Height const& height)
    {
        std::optional<float> const h0 = height(at.x, at.y, at.z);
        if (!h0)
            return {0.f, 0.f, 1.f};
        float const d = 0.5f;
        auto h = [&](float x, float y) { return height(x, y, at.z).value_or(*h0); };
        float const dx = (h(at.x + d, at.y) - h(at.x - d, at.y)) / (2.f * d);
        float const dy = (h(at.x, at.y + d) - h(at.x, at.y - d)) / (2.f * d);
        return Vec3{-dx, -dy, 1.f}.Normalized();
    }

    Vec3 WallNormal(Vec3 a, Vec3 b, Probe const& probe)
    {
        float const len = (b - a).Length();
        if (len < 1e-4f)
            return {0.f, 0.f, 1.f};
        Vec3 const dir = (b - a) * (1.f / len);
        Vec3 const back = dir * -1.f;
        Vec3 u = Cross(dir, {0.f, 0.f, 1.f});
        u = u.Length() > 1e-3f ? u.Normalized() : Vec3{1.f, 0.f, 0.f};
        Vec3 const w = Cross(u, dir).Normalized();
        // Three probes side by side along the segment, from a little behind it to a little past.
        float const behind = 0.5f, reach = behind + len + 1.f, apart = 0.15f;
        Vec3 points[3];
        Vec3 const offsets[3] = {{}, u * apart, w * apart};
        for (int i = 0; i < 3; ++i)
        {
            Vec3 const s = a - dir * behind + offsets[i];
            std::optional<float> const d = probe(s, s + dir * reach);
            if (!d)
                return back;
            points[i] = s + dir * *d;
        }
        Vec3 n = Cross(points[1] - points[0], points[2] - points[0]);
        if (n.Length() < 1e-6f)
            return back;
        n = n.Normalized();
        return n.Dot(dir) > 0.f ? n * -1.f : n;
    }

    Vec3 SurfaceNormal(Vec3 a, Vec3 b, Vec3 at, Height const& height, Probe const& probe)
    {
        if (std::optional<float> const g = height(at.x, at.y, at.z); g && std::fabs(at.z - *g) <= 0.25f)
            return GroundNormal(at, height);
        return WallNormal(a, b, probe);
    }

    std::vector<uint8_t> EncodePath(uint64_t thrower, uint16_t weapon, float gravity, Path const& path)
    {
        size_t const n = std::min<size_t>(path.corners.size(), kMaxCorners);
        std::vector<uint8_t> o{kOpPath};
        Put32(o, uint32_t(thrower));
        Put32(o, uint32_t(thrower >> 32));
        Put16(o, weapon);
        PutF(o, gravity);
        Put16(o, path.fuseMs);
        o.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Put16(o, path.corners[i].ms);
            PutF(o, path.corners[i].pos.x);
            PutF(o, path.corners[i].pos.y);
            PutF(o, path.corners[i].pos.z);
        }
        return o;
    }
}}
