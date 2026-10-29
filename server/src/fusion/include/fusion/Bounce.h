/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * Reflect's bounce is incidence-weighted.
 */

#ifndef FUSION_BOUNCE_H
#define FUSION_BOUNCE_H

#include "fusion/Common.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace Fusion { namespace Bounce
{
    // A frag as MW2 throws it: it flies under gravity, bounces off what it meets (the ground, a
    // wall, a roof) losing speed each time, rolls to rest and goes off when its fuse runs out,
    // wherever it is then. The server flies it once at the throw; everyone near is told its
    // corners (op 35 below) and rebuilds the same path from them.

    // IW4's bounce: the velocity mirrored in the surface, scaled between `parallel` (a glancing
    // touch) and `perpendicular` (straight in) by how square it struck. A zero velocity or normal
    // gives zero: a bounce never adds speed.
    Vec3 Reflect(Vec3 v, Vec3 normal, float parallel, float perpendicular);
    constexpr float kParallel = 0.6f;
    constexpr float kPerpendicular = 0.4f;

    constexpr float kStepS = 0.02f;         // the flight's steps (exact under gravity)
    constexpr float kBackOff = 0.05f;       // yards off the surface after a bounce, along its normal
    constexpr float kRestNormalZ = 0.65f;   // a surface this flat (normal z) can hold it at rest
    constexpr float kRestSpeed = 1.f;       // yards a second: slower than this off such a surface, it rests
    constexpr uint8_t kMaxCorners = 40;     // the path's corners at most (its start and end among them)
    constexpr float kMinFuseS = 0.05f;

    // A segment's first contact: how far along it (0..1) and the surface's normal there.
    struct Contact
    {
        float fraction = 0.f;
        Vec3 normal{0.f, 0.f, 1.f};
    };
    using SegmentHit = std::function<std::optional<Contact>(Vec3 a, Vec3 b)>;

    // A point the path turns at: its start, each bounce (backed off the surface), and its end.
    struct Corner
    {
        uint16_t ms = 0;        // from the throw
        Vec3 pos;
    };

    struct Path
    {
        std::vector<Corner> corners;    // from the throw (ms 0) to the end
        Vec3 end;                       // where it is when the fuse runs out
        uint16_t fuseMs = 0;            // when it goes off, from the throw
        bool rested = false;            // it came to rest before then
    };

    // Flies a throw from `origin` at `velocity` under `gravity` (yd/s^2, down along z) until
    // `fuseS` runs out: kStepS steps tested with `hit`, a bounce (Reflect, kParallel,
    // kPerpendicular) at each contact, at rest on a flat enough surface once slower than
    // kRestSpeed, or after kMaxCorners corners.
    Path Fly(Vec3 origin, Vec3 velocity, float gravity, float fuseS, SegmentHit const& hit);

    // Where the path is `ms` after the throw: between two corners, the arc under `gravity` that
    // joins them in their time; before the first, the first; after the last, the last.
    Vec3 PathAt(std::vector<Corner> const& corners, float gravity, float ms);

    // A cooked frag's fuse: what is left after `cookMs` held in the hand, at least kMinFuseS.
    float CookedFuse(float fuseS, uint32_t cookMs);

    // The surface's normal where a segment a -> b met it at `at`.
    //  `height(x, y, z)`: the ground's height under (x, y) near z, if any.
    //  `probe(a, b)`: how far along a -> b (yards) it first meets the world, if it does.
    // On the ground (`at` within a hand of it), from the heights half a yard either side; else a
    // wall's, from three parallel probes along the segment; else, failing those, straight back
    // along the segment.
    using Height = std::function<std::optional<float>(float x, float y, float z)>;
    using Probe = std::function<std::optional<float>(Vec3 a, Vec3 b)>;
    Vec3 GroundNormal(Vec3 at, Height const& height);
    Vec3 WallNormal(Vec3 a, Vec3 b, Probe const& probe);
    Vec3 SurfaceNormal(Vec3 a, Vec3 b, Vec3 at, Height const& height, Probe const& probe);

    // SMSG_FUSION_CHANNEL (kChannelWorld) body, little-endian, raw WoW coordinates:
    // op 35, 18 + 14 n bytes: [35, thrower guid u64, weapon u16, gravity f32, fuse_ms u16, n u8,
    //   then n x (ms u16, x y z f32)]: a thrown frag's path (its corners, PathAt), going off at
    //   fuse_ms where the last corner leaves it. Told to everyone near, the thrower too.
    constexpr uint8_t kOpPath = 35;
    std::vector<uint8_t> EncodePath(uint64_t thrower, uint16_t weapon, float gravity, Path const& path);
}}

#endif
