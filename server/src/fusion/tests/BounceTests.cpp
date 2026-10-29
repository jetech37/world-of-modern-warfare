/*
 * Azeroth Warfare: tests for bouncing frags (Bounce.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Bounce.h"
#include "fusion/Movement.h"

#include <cmath>
#include <cstring>

using namespace Fusion;
using namespace Fusion::Bounce;

namespace
{
    float FloatAt(std::vector<uint8_t> const& b, size_t at)
    {
        float f;
        std::memcpy(&f, &b[at], 4);
        return f;
    }

    // Flat ground at z = 0.
    std::optional<Contact> Ground(Vec3 a, Vec3 b)
    {
        if (a.z < 0.f || b.z >= 0.f)
            return std::nullopt;
        return Contact{a.z / (a.z - b.z), {0.f, 0.f, 1.f}};
    }

    // Flat ground and a wall at x = 5 facing -x.
    std::optional<Contact> GroundAndWall(Vec3 a, Vec3 b)
    {
        std::optional<Contact> g = Ground(a, b);
        if (a.x <= 5.f && b.x > 5.f)
        {
            float const f = (5.f - a.x) / (b.x - a.x);
            if (!g || f < g->fraction)
                return Contact{f, {-1.f, 0.f, 0.f}};
        }
        return g;
    }

    bool Near(Vec3 a, Vec3 b, float eps) { return (a - b).Length() <= eps; }
}

TEST(a_bounce_mirrors_and_loses_speed_by_how_square_it_struck)
{
    // Straight down onto the floor: back up at the perpendicular share.
    Vec3 const up = Reflect({0.f, 0.f, -10.f}, {0.f, 0.f, 1.f}, kParallel, kPerpendicular);
    CHECK_NEAR(up.z, 4.f, 1e-4);
    CHECK_NEAR(up.x, 0.f, 1e-6);
    // A glancing floor hit keeps going forward, rises, and is slower.
    Vec3 const skim = Reflect({8.f, 0.f, -3.f}, {0.f, 0.f, 1.f}, kParallel, kPerpendicular);
    CHECK(skim.z > 0.f && skim.x > 0.f);
    CHECK(skim.Length() < Vec3(8.f, 0.f, -3.f).Length());
    // A wall turns it back.
    CHECK(Reflect({8.f, 1.f, 0.f}, {-1.f, 0.f, 0.f}, kParallel, kPerpendicular).x < 0.f);
    // An unnormalised normal is the same surface.
    Vec3 const big = Reflect({0.f, 0.f, -10.f}, {0.f, 0.f, 7.f}, kParallel, kPerpendicular);
    CHECK_NEAR(big.z, 4.f, 1e-4);
    // Nothing to bounce, or no surface: no speed at all.
    CHECK(Reflect({}, {0.f, 0.f, 1.f}, kParallel, kPerpendicular).Length() == 0.f);
    CHECK(Reflect({10.f, 10.f, 10.f}, {}, kParallel, kPerpendicular).Length() == 0.f);
}

TEST(a_frag_thrown_on_flat_ground_bounces_rolls_and_rests)
{
    Vec3 const origin{0.f, 0.f, 1.5f};
    Path const p = Fly(origin, {10.f, 0.f, 2.f}, kWowGravity, 3.5f, Ground);
    CHECK(p.rested);
    CHECK(p.fuseMs == 3500);
    CHECK(p.corners.size() >= 3 && p.corners.size() <= kMaxCorners);
    CHECK(p.corners.front().ms == 0 && Near(p.corners.front().pos, origin, 1e-6f));
    // Each corner later than the one before, none under the ground, the end backed off it.
    for (size_t i = 1; i < p.corners.size(); ++i)
    {
        CHECK(p.corners[i].ms > p.corners[i - 1].ms);
        CHECK(p.corners[i].pos.z >= 0.f);
    }
    CHECK_NEAR(p.end.z, kBackOff, 1e-3);
    CHECK(p.corners.back().ms < p.fuseMs);
    // It went on past its first landing.
    CHECK(p.end.x > p.corners[1].pos.x + 0.5f);
    CHECK(std::fabs(p.end.y) < 1e-4f);
}

TEST(the_rebuilt_path_passes_through_every_corner_and_follows_the_flight)
{
    Vec3 const origin{0.f, 0.f, 1.5f};
    Vec3 const v{10.f, 3.f, 4.f};
    Path const p = Fly(origin, v, kWowGravity, 3.5f, Ground);
    for (Corner const& c : p.corners)
        CHECK(Near(PathAt(p.corners, kWowGravity, c.ms), c.pos, 1e-3f));
    // Before the throw it is in the hand; after the last corner it lies still.
    CHECK(Near(PathAt(p.corners, kWowGravity, -5.f), origin, 1e-6f));
    CHECK(Near(PathAt(p.corners, kWowGravity, p.fuseMs), p.end, 1e-6f));
    // Up to its first landing, the free flight itself (to within the back-off at the corner).
    float const half = p.corners[1].ms * 0.5f;
    float const s = half / 1000.f;
    Vec3 const free = origin + v * s + Vec3{0.f, 0.f, -0.5f * kWowGravity * s * s};
    CHECK(Near(PathAt(p.corners, kWowGravity, half), free, 0.06f));
    // Between corners it never dips far under the ground.
    for (float ms = 0.f; ms <= p.fuseMs; ms += 5.f)
        CHECK(PathAt(p.corners, kWowGravity, ms).z > -0.05f);
}

TEST(a_frag_off_a_wall_comes_back)
{
    Path const p = Fly({0.f, 0.f, 1.5f}, {15.f, 0.f, 3.f}, kWowGravity, 3.5f, GroundAndWall);
    CHECK(p.corners.size() >= 3);
    // Its first bounce: on the wall, backed off it.
    CHECK_NEAR(p.corners[1].pos.x, 5.f - kBackOff, 1e-3);
    CHECK(p.end.x < 5.f);
    CHECK(p.corners[2].pos.x < p.corners[1].pos.x);
}

TEST(a_frag_with_nothing_to_meet_goes_off_in_the_air)
{
    Vec3 const origin{0.f, 0.f, 100.f};
    Vec3 const v{5.f, 0.f, 0.f};
    Path const p = Fly(origin, v, kWowGravity, 1.f, [](Vec3, Vec3) { return std::optional<Contact>(); });
    CHECK(!p.rested);
    CHECK(p.corners.size() == 2);
    CHECK(p.corners.back().ms == 1000);
    Vec3 const want = origin + v * 1.f + Vec3{0.f, 0.f, -0.5f * kWowGravity};
    CHECK(Near(p.end, want, 1e-3f));
    CHECK(Near(PathAt(p.corners, kWowGravity, 500.f), origin + v * 0.5f + Vec3{0.f, 0.f, -0.125f * kWowGravity}, 1e-3f));
}

TEST(a_frag_caught_in_a_corner_stops_at_forty_corners)
{
    // A wall that is always there: it bounces in place until the cap.
    Path const p = Fly({0.f, 0.f, 1.5f}, {10.f, 0.f, 0.f}, kWowGravity, 3.5f,
        [](Vec3, Vec3) { return std::optional<Contact>(Contact{0.f, {-1.f, 0.f, 0.f}}); });
    CHECK(p.corners.size() == kMaxCorners);
    CHECK(Near(p.end, p.corners.back().pos, 1e-6f));
    CHECK(EncodePath(1, 2, kWowGravity, p).size() == 18 + 14 * size_t(kMaxCorners));
}

TEST(a_cooked_frag_has_less_fuse_left)
{
    CHECK_NEAR(CookedFuse(3.5f, 0), 3.5f, 1e-6);
    CHECK_NEAR(CookedFuse(3.5f, 1000), 2.5f, 1e-5);
    CHECK_NEAR(CookedFuse(3.5f, 3500), kMinFuseS, 1e-6);
    CHECK_NEAR(CookedFuse(3.5f, 0xFFFFFFFFu), kMinFuseS, 1e-6);
    // A fuse cooked to nothing goes off at once, where it is.
    Path const p = Fly({0.f, 0.f, 1.5f}, {10.f, 0.f, 0.f}, kWowGravity, CookedFuse(3.5f, 3500), Ground);
    CHECK(p.fuseMs == 50);
    CHECK((p.end - Vec3{0.f, 0.f, 1.5f}).Length() < 1.f);
}

TEST(the_ground_and_walls_give_their_normals)
{
    auto flat = [](float, float, float) { return std::optional<float>(0.f); };
    CHECK(Near(GroundNormal({1.f, 2.f, 0.f}, flat), {0.f, 0.f, 1.f}, 1e-6f));
    // z = x / 2: a slope rising along x.
    auto slope = [](float x, float, float) { return std::optional<float>(0.5f * x); };
    CHECK(Near(GroundNormal({2.f, 0.f, 1.f}, slope), Vec3{-0.5f, 0.f, 1.f}.Normalized(), 1e-4f));
    // No ground at all: up.
    auto none = [](float, float, float) { return std::optional<float>(); };
    CHECK(Near(GroundNormal({0.f, 0.f, 0.f}, none), {0.f, 0.f, 1.f}, 1e-6f));

    // A wall along x + y = 8, met heading +x.
    auto wall = [](Vec3 a, Vec3 b) -> std::optional<float> {
        float const fa = a.x + a.y - 8.f, fb = b.x + b.y - 8.f;
        if (fa > 0.f || fb <= 0.f)
            return std::nullopt;
        return fa / (fa - fb) * (b - a).Length();
    };
    Vec3 const n = WallNormal({5.5f, 1.f, 1.f}, {6.5f, 1.f, 1.f}, wall);
    CHECK(Near(n, Vec3{-1.f, -1.f, 0.f}.Normalized(), 1e-3f));
    // Nothing for the probes: straight back.
    CHECK(Near(WallNormal({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, [](Vec3, Vec3) { return std::optional<float>(); }),
        {-1.f, 0.f, 0.f}, 1e-6f));
    // On the ground the heights decide; up off it, the probes.
    CHECK(Near(SurfaceNormal({0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}, {0.f, 0.f, 0.1f}, flat, wall), {0.f, 0.f, 1.f}, 1e-6f));
    CHECK(Near(SurfaceNormal({5.5f, 1.f, 3.f}, {6.5f, 1.f, 3.f}, {7.f, 1.f, 3.f}, flat, wall),
        Vec3{-1.f, -1.f, 0.f}.Normalized(), 1e-3f));
}

TEST(the_path_message_carries_every_corner)
{
    Path p;
    p.fuseMs = 3500;
    p.corners = {{0, {1.f, 2.f, 3.f}}, {420, {4.f, -5.f, 6.5f}}};
    std::vector<uint8_t> const b = EncodePath(0x1122334455667788ull, 0x0203, 19.5f, p);
    CHECK(b.size() == 18 + 14 * 2);
    std::vector<uint8_t> const head = {35, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x03, 0x02};
    CHECK(std::vector<uint8_t>(b.begin(), b.begin() + 11) == head);
    CHECK(FloatAt(b, 11) == 19.5f);
    CHECK(b[15] == (3500 & 0xFF) && b[16] == (3500 >> 8) && b[17] == 2);
    CHECK(b[18] == 0 && b[19] == 0 && FloatAt(b, 20) == 1.f && FloatAt(b, 24) == 2.f && FloatAt(b, 28) == 3.f);
    CHECK(b[32] == (420 & 0xFF) && b[33] == (420 >> 8));
    CHECK(FloatAt(b, 34) == 4.f && FloatAt(b, 38) == -5.f && FloatAt(b, 42) == 6.5f);
    CHECK(kOpPath == 35);
}
