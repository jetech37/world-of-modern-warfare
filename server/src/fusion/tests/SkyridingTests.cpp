/*
 * Azeroth Warfare: tests for skyriding (Skyriding.h, its phase in Riding.h). GPL-2.0-or-later,
 * see Common.h.
 */

#include "Test.h"
#include "fusion/Riding.h"
#include "fusion/Skyriding.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Fusion;
using namespace Fusion::Skyriding;

namespace
{
    constexpr float kDt = 1.f / 60.f;
    constexpr float kDeg = 0.017453293f;

    // A flight along x: position, the body, and the energy's worst rise over the steps.
    struct Run
    {
        Body b;
        float x = 0.f, z = 0.f;
        float worstRise = 0.f;
        void Fly(float pitch, float seconds, float dt = kDt)
        {
            int const n = int(std::lround(seconds / dt));
            for (int i = 0; i < n; ++i)
            {
                float const before = Energy(z, b.speed);
                Motion const m = Advance(b, pitch, dt);
                x += m.forward * dt;
                z += m.up * dt;
                if (b.ascentLeft <= 0.f)
                    worstRise = std::max(worstRise, Energy(z, b.speed) - before);
            }
        }
    };
}

TEST(skyriding_a_dive_trades_height_for_speed_and_never_makes_energy)
{
    Run r;
    r.b.speed = 14.f;
    r.z = 300.f;
    r.Fly(-45.f * kDeg, 3.f);
    CHECK(r.b.speed > 40.f);
    CHECK(r.z < 260.f);
    CHECK(r.worstRise <= 0.f);
    // Straight down, the speed caps.
    r.Fly(-90.f * kDeg, 4.f);
    CHECK(r.b.speed == kMaxSpeed);
    CHECK(r.worstRise <= 0.f);
}

TEST(skyriding_a_pull_up_trades_speed_for_height_and_cannot_hold_altitude)
{
    // From 40 yd/s, a climb tops out under the height its speed is worth (v^2 / 2g = 50).
    Run r;
    r.b.speed = 40.f;
    float top = 0.f;
    for (int i = 0; i < 600; ++i)
    {
        r.Fly(30.f * kDeg, kDt);
        top = std::max(top, r.z);
    }
    CHECK(top > 25.f && top < 50.f);
    CHECK(r.b.speed < 1.f);
    CHECK(r.worstRise <= 0.f);
    // Level, the drag and the sink bring it down: no hover, no free cruise.
    Run level;
    level.b.speed = 30.f;
    level.z = 100.f;
    level.Fly(0.f, 20.f);
    CHECK(level.z < 70.f);
    CHECK(level.b.speed < 10.f);
    // Even nose up at a standstill it sinks.
    Run stall;
    stall.z = 100.f;
    stall.Fly(60.f * kDeg, 5.f);
    CHECK(stall.z < 100.f - 5.f * kSink);
}

TEST(skyriding_range_comes_from_height)
{
    // The same shallow glide (-8 degrees) from three heights to the ground: higher goes further.
    auto range = [](float height) {
        Run r;
        r.b.speed = 14.f;
        r.z = height;
        while (r.z > 0.f && r.x < 100000.f)
            r.Fly(-8.f * kDeg, kDt);
        return r.x;
    };
    float const low = range(10.f), mid = range(100.f), high = range(400.f);
    CHECK(low < mid && mid < high);
    CHECK(low < 120.f);
    CHECK(high > 1000.f);
    std::printf("    skyride range at -8 degrees: 10 yd %.0f, 100 yd %.0f, 400 yd %.0f\n", low, mid, high);
}

TEST(skyriding_ascent_and_surge)
{
    Run r;
    r.b.speed = 0.f;
    Ascend(r.b);
    r.Fly(0.f, kAscentSeconds);
    CHECK_NEAR(r.z, kAscentRate * kAscentSeconds, 0.05f);
    CHECK(r.b.speed >= kAscentExitSpeed);
    CHECK(r.b.ascentLeft == 0.f);
    Body b;
    b.speed = 20.f;
    Surge(b);
    CHECK(b.speed == 36.f);
    b.speed = 45.f;
    Surge(b);
    CHECK(b.speed == kMaxSpeed);
    CHECK(ClampPitch(-2.f) == kMinPitch && ClampPitch(2.f) == kMaxPitch && ClampPitch(0.1f) == 0.1f);
}

// The client (client/src/riding.rs, `the_physics_matches_the_server`) flies the same steps to the
// same numbers.
TEST(skyriding_the_shared_vector)
{
    Run r;
    r.b.speed = 14.f;
    r.z = 200.f;
    r.Fly(-0.5f, 2.f, 1.f / 30.f);
    r.Fly(0.4f, 2.f, 1.f / 30.f);
    Ascend(r.b);
    r.Fly(0.f, 2.f, 1.f / 30.f);
    Surge(r.b);
    r.Fly(-0.2f, 1.f, 1.f / 30.f);
    std::printf("    skyride vector: speed %.4f, x %.4f, z %.4f\n", r.b.speed, r.x, r.z);
    CHECK_NEAR(r.b.speed, 36.1062f, 2e-3f);
    CHECK_NEAR(r.x, 136.4708f, 2e-2f);
    CHECK_NEAR(r.z, 212.9639f, 2e-2f);
}

TEST(skyriding_vigor_spends_and_refills_faster_on_the_ground)
{
    Vigor v;
    Recharge(v, 1000, false);
    CHECK(Spend(v, 1000) && v.charges == 2);
    CHECK(!Spend(v, 1500));                         // the gap
    CHECK(Spend(v, 2000) && Spend(v, 3000) && v.charges == 0);
    CHECK(!Spend(v, 9000) || v.charges < kMaxCharges);
    // In flight, one back after kChargeAirSeconds...
    Vigor a;
    a.charges = 0;
    Recharge(a, 1000, true);
    Recharge(a, 1000 + uint32_t(kChargeAirSeconds * 1000.f) - 100, true);
    CHECK(a.charges == 0 && a.progress > 0.9f);
    Recharge(a, 1000 + uint32_t(kChargeAirSeconds * 1000.f) + 100, true);
    CHECK(a.charges == 1);
    // ...on the ground, every kChargeGroundSeconds, up to the full three and no further.
    Vigor g;
    g.charges = 0;
    Recharge(g, 1000, false);
    Recharge(g, 1000 + uint32_t(2.5f * kChargeGroundSeconds * 1000.f), false);
    CHECK(g.charges == 2 && std::fabs(g.progress - 0.5f) < 1e-3f);
    Recharge(g, 1000 + uint32_t(20.f * kChargeGroundSeconds * 1000.f), false);
    CHECK(g.charges == kMaxCharges && g.progress == 0.f);
}

namespace
{
    // The server's look at a flight flown by the law: every `every` seconds, a report.
    struct Watched
    {
        Run r;
        Envelope e;
        uint32_t now = 1000;
        bool ok = true;
        void Start(float z, float speed)
        {
            r.z = z;
            r.b.speed = speed;
            Begin(e, now, r.x, 0.f, r.z, speed);
        }
        void Fly(float pitch, float seconds)
        {
            for (int i = 0; i < int(std::lround(seconds * 10.f)); ++i)
            {
                r.Fly(pitch, 0.1f, 1.f / 60.f);
                now += 100;
                ok = Check(e, now, r.x, 0.f, r.z) && ok;
            }
        }
    };
}

TEST(skyriding_the_server_passes_a_flight_flown_by_the_law)
{
    Watched w;
    w.Start(300.f, 14.f);
    w.Fly(-40.f * kDeg, 4.f);
    w.Fly(25.f * kDeg, 4.f);
    Ascend(w.r.b);
    Grant(w.e, Move::Ascent);
    w.Fly(0.f, 2.f);
    Surge(w.r.b);
    Grant(w.e, Move::Surge);
    w.Fly(-5.f * kDeg, 20.f);
    w.Fly(-60.f * kDeg, 3.f);
    w.Fly(20.f * kDeg, 6.f);
    CHECK(w.ok);
    // A take-off: an Ascent from the ground, then a long glide down.
    Watched t;
    t.Start(50.f, 14.f);
    Ascend(t.r.b);
    Grant(t.e, Move::Ascent);
    t.Fly(0.f, 1.5f);
    t.Fly(-10.f * kDeg, 30.f);
    CHECK(t.ok);
}

TEST(skyriding_the_server_ends_free_flight_and_speed_hacks)
{
    // Holding altitude at a cruise: the budget bleeds and the flight is ended.
    Envelope e;
    uint32_t now = 1000;
    Begin(e, now, 0.f, 0.f, 200.f, 30.f);
    bool ok = true;
    float x = 0.f;
    int caughtAt = -1;
    for (int i = 0; i < 600 && caughtAt < 0; ++i)
    {
        now += 100;
        x += 3.f;
        if (!Check(e, now, x, 0.f, 200.f))
            caughtAt = i;
    }
    CHECK(caughtAt > 0 && caughtAt < 300);         // inside 30 s
    // Climbing slowly from a standstill.
    Begin(e, now, 0.f, 0.f, 100.f, 0.f);
    ok = true;
    float z = 100.f;
    for (int i = 0; i < 100; ++i)
    {
        now += 100;
        z += 0.5f;
        ok = Check(e, now, 0.f, 0.f, z) && ok;
    }
    CHECK(!ok);
    // Level and far faster than the height pays for.
    Begin(e, now, 0.f, 0.f, 100.f, 14.f);
    ok = true;
    x = 0.f;
    for (int i = 0; i < 20; ++i)
    {
        now += 100;
        x += 6.f;
        ok = Check(e, now, x, 0.f, 100.f) && ok;
    }
    CHECK(!ok);
    // A charge's worth of climb passes with it granted, and fails without.
    auto climb = [&](bool granted) {
        Envelope c;
        uint32_t t = 1000;
        Begin(c, t, 0.f, 0.f, 50.f, 14.f);
        if (granted)
            Grant(c, Move::Ascent);
        bool good = true;
        for (int i = 0; i < 12; ++i)
        {
            t += 100;
            good = Check(c, t, 0.f, 0.f, 50.f + kAscentRate * 0.1f * float(i + 1)) && good;
        }
        return good;
    };
    CHECK(climb(true));
    CHECK(!climb(false));
}

namespace
{
    Riding::Sense Rider(uint32_t now, float z, float ground, bool airborne, bool sky = true)
    {
        Riding::Sense s;
        s.nowMs = now;
        s.mounted = true;
        s.skyrider = sky;
        s.airborne = airborne;
        s.map = 1;
        s.x = 100.f;
        s.y = 100.f;
        s.z = z;
        s.groundZ = ground;
        s.speed = 14.f;
        return s;
    }
}

TEST(skyriding_a_flying_line_skyrides_off_a_height_and_a_ground_mount_still_glides)
{
    Riding::Flight f;
    Riding::Step(f, Rider(1000, 70.f, 50.f, true));
    CHECK(f.phase == Riding::Phase::Skyride);
    // Flying on (the GM flight shows no airborne flag), then down.
    Riding::Step(f, Rider(1500, 66.f, 50.f, false));
    CHECK(f.phase == Riding::Phase::Skyride);
    Riding::Step(f, Rider(3000, 51.f, 50.f, false));
    CHECK(f.phase == Riding::Phase::None);
    // A ground mount off the same height: the old glide.
    Riding::Flight g;
    Riding::Step(g, Rider(1000, 70.f, 50.f, true, false));
    CHECK(g.phase == Riding::Phase::Glide);
    // A launch point: an updraft's skyride for a flying line, the burst for a ground mount.
    Riding::LaunchPoint const point{7, "test perch", 1, 100.f, 100.f, 50.f, 10.f, 15.f, 120.f, 200.f};
    Riding::Sense jump = Rider(1000, 50.f, 50.f, true);
    jump.jumped = true;
    jump.launch = &point;
    Riding::Flight up;
    Riding::Step(up, jump);
    CHECK(up.phase == Riding::Phase::Skyride && up.landAfterMs > 1000);
    jump.skyrider = false;
    Riding::Flight burst;
    Riding::Step(burst, jump);
    CHECK(burst.phase == Riding::Phase::Burst);
}

TEST(skyriding_ascent_takes_off_and_surge_needs_a_flight)
{
    Riding::Flight f;
    Riding::Sense ground = Rider(1000, 50.f, 50.f, false);
    Riding::Step(f, ground);
    CHECK(!Riding::SkyMove(f, ground, Move::Surge));
    CHECK(Riding::SkyMove(f, ground, Move::Ascent));
    CHECK(f.phase == Riding::Phase::Skyride && f.vigor.charges == kMaxCharges - 1);
    // Still on the ground a moment later: the take-off's grace keeps it flying.
    Riding::Step(f, Rider(1300, 50.f, 50.f, false));
    CHECK(f.phase == Riding::Phase::Skyride);
    Riding::Step(f, Rider(1800, 70.f, 50.f, false));
    CHECK(f.phase == Riding::Phase::Skyride);
    // Up; a Surge (after the gap), then nothing left but one.
    Riding::Sense high = Rider(2100, 80.f, 50.f, false);
    CHECK(Riding::SkyMove(f, high, Move::Surge) && f.vigor.charges == 1);
    high.nowMs = 2500;
    CHECK(!Riding::SkyMove(f, high, Move::Ascent));    // the gap
    // A ground mount, off the mount, or in water: never.
    Riding::Flight g;
    Riding::Sense walker = Rider(1000, 50.f, 50.f, false, false);
    CHECK(!Riding::SkyMove(g, walker, Move::Ascent));
    walker = Rider(1000, 50.f, 50.f, false);
    walker.inWater = true;
    CHECK(!Riding::SkyMove(g, walker, Move::Ascent));
    walker.inWater = false;
    walker.mounted = false;
    CHECK(!Riding::SkyMove(g, walker, Move::Ascent));
}

TEST(skyriding_a_flight_against_the_law_is_brought_down)
{
    Riding::Flight f;
    Riding::Step(f, Rider(1000, 100.f, 50.f, true));
    CHECK(f.phase == Riding::Phase::Skyride);
    // Rising with no charge spent.
    uint32_t now = 1000;
    float z = 100.f;
    for (int i = 0; i < 20 && f.phase == Riding::Phase::Skyride; ++i)
    {
        now += 200;
        z += 3.f;
        Riding::Step(f, Rider(now, z, 50.f, false));
    }
    CHECK(f.phase == Riding::Phase::Descent && f.broke);
    // Knocked off mid-flight: the glide down, not a drop.
    Riding::Flight k;
    Riding::Step(k, Rider(1000, 100.f, 50.f, true));
    Riding::Sense off = Rider(1200, 99.f, 50.f, false);
    off.mounted = false;
    Riding::Step(k, off);
    CHECK(k.phase == Riding::Phase::Descent && !k.broke);
    // Into water: over.
    Riding::Flight w;
    Riding::Step(w, Rider(1000, 100.f, 50.f, true));
    Riding::Sense wet = Rider(1200, 99.f, 50.f, false);
    wet.inWater = true;
    Riding::Step(w, wet);
    CHECK(w.phase == Riding::Phase::None);
}

TEST(skyriding_the_wire)
{
    Vigor v;
    v.charges = 2;
    v.progress = 0.25f;
    std::vector<uint8_t> const b = EncodeSky(SKY_FLYING | SKY_ASCENT | SKY_RIDER, v, 22.5f);
    CHECK(b.size() == 12 && b[0] == kOpSky && b[1] == 0x0b && b[2] == 2 && b[3] == kMaxCharges);
    float p = 0.f, s = 0.f;
    std::memcpy(&p, &b[4], 4);
    std::memcpy(&s, &b[8], 4);
    CHECK(p == 0.25f && s == 22.5f);
    Move m = Move::Ascent;
    CHECK(DecodeSkyMove({kOpSkyMove, 2}, m) && m == Move::Surge);
    CHECK(!DecodeSkyMove({kOpSkyMove, 3}, m));
    CHECK(!DecodeSkyMove({kOpSkyMove}, m));
    CHECK(!DecodeSkyMove({kOpSky, 1}, m));
    Riding::Flight f;
    f.phase = Riding::Phase::Skyride;
    CHECK(Riding::EncodeFlight(f, 0)[1] == 4);
}
