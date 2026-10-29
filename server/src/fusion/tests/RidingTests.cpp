/*
 * Azeroth Warfare: tests for riding (Riding.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Riding.h"

#include <cstdio>
#include <cstring>

using namespace Fusion;
using namespace Fusion::Riding;

TEST(gunfire_knocks_a_rider_off_by_its_share_of_their_health)
{
    // A tenth of their health: 15%; a third: half; two thirds or more: always.
    CHECK_NEAR(KnockOffChance({100.f, 1000.f, false, false}), 0.15f, 1e-5f);
    CHECK_NEAR(KnockOffChance({333.f, 1000.f, false, false}), 0.4995f, 1e-4f);
    CHECK_NEAR(KnockOffChance({700.f, 1000.f, false, false}), 1.f, 1e-6f);
    // A pellet or a graze never does; nothing for no damage, even a sniper's.
    CHECK(KnockOffChance({30.f, 1000.f, false, false}) == 0.f);
    CHECK(KnockOffChance({0.f, 1000.f, true, true}) == 0.f);
    // A sniper's round or a blast always does.
    CHECK(KnockOffChance({30.f, 1000.f, true, false}) == 1.f);
    CHECK(KnockOffChance({30.f, 1000.f, false, true}) == 1.f);
    CHECK(KnockedOff({100.f, 1000.f, false, false}, 0.14f));
    CHECK(!KnockedOff({100.f, 1000.f, false, false}, 0.16f));
    CHECK(KnockedOff({1.f, 1000.f, true, false}, 0.999f));
    // A rider's shot may start further from the server's eye.
    CHECK(OriginTolerance(true) > OriginTolerance(false));
}

TEST(the_default_launch_points_are_sound)
{
    std::vector<LaunchPoint> const& points = DefaultLaunchPoints();
    CHECK(points.size() >= 10);
    CHECK(Problems(points).empty());
    for (std::string const& p : Problems(points))
        std::printf("    launch: %s\n", p.c_str());
    // Every point in a site flies under the site's ceiling and starts inside it.
    Plots::Grid const& g = Plots::Default();
    for (LaunchPoint const& p : points)
        if (Plots::Site const* s = g.SiteAt(p.map, p.x, p.y))
            CHECK(p.z < s->ceilingZ && p.z > s->floorZ);
    // Each new district has one.
    CHECK(LaunchAt(points, 37, 130.f, 133.f, 236.f) != nullptr);
    CHECK(LaunchAt(points, 0, 3740.f, -4300.f, 129.f) != nullptr);
    CHECK(LaunchAt(points, 1, 5386.f, -3709.f, 1594.f) != nullptr);
    // Not from below or beside it, nor on another map.
    CHECK(LaunchAt(points, 0, -8835.8f, 490.1f, 78.f) == nullptr);
    CHECK(LaunchAt(points, 0, -8800.f, 490.1f, 109.7f) == nullptr);
    CHECK(LaunchAt(points, 1, -8835.8f, 490.1f, 109.7f) == nullptr);
    std::vector<LaunchPoint> bad{{1, "a", 0, 0.f, 0.f, 100.f, 10.f, 30.f, 90.f, 200.f}, {1, "b", 0, 0.f, 0.f, 0.f, 0.f, 10.f, 50.f, 200.f}};
    CHECK(Problems(bad).size() == 4);
}

namespace
{
    LaunchPoint const kPoint{7, "test perch", 1, 100.f, 100.f, 50.f, 10.f, 15.f, 120.f, 200.f};

    Sense Mounted(uint32_t now, float z, float ground, bool airborne)
    {
        Sense s;
        s.nowMs = now;
        s.mounted = true;
        s.airborne = airborne;
        s.map = 1;
        s.x = 100.f;
        s.y = 100.f;
        s.z = z;
        s.groundZ = ground;
        return s;
    }
}

TEST(a_mounted_fall_from_height_glides_until_it_lands)
{
    Flight f;
    // A hop on flat ground: nothing.
    Step(f, Mounted(1000, 51.f, 50.f, true));
    CHECK(f.phase == Phase::None);
    // Off a ledge: the ground is 20 yards down.
    Step(f, Mounted(1100, 70.f, 50.f, true));
    CHECK(f.phase == Phase::Glide && f.untilMs == 1100 + uint32_t(kGlideSeconds * 1000.f));
    Step(f, Mounted(2000, 64.f, 50.f, true));
    CHECK(f.phase == Phase::Glide);
    // Down.
    Step(f, Mounted(4000, 50.f, 50.f, false));
    CHECK(f.phase == Phase::None);
    // Too long a drop: the glide gives out, and doesn't start again on the same fall.
    Step(f, Mounted(5000, 400.f, 50.f, true));
    CHECK(f.phase == Phase::Glide);
    Step(f, Mounted(5000 + uint32_t(kGlideSeconds * 1000.f), 344.f, 50.f, true));
    CHECK(f.phase == Phase::None);
    Step(f, Mounted(14000, 300.f, 50.f, true));
    CHECK(f.phase == Phase::None);
    Step(f, Mounted(30000, 50.f, 50.f, false));
    Step(f, Mounted(31000, 80.f, 50.f, true));
    CHECK(f.phase == Phase::Glide);
    // On foot nothing glides; knocked off mid-glide, it stops.
    Sense walker = Mounted(20000, 70.f, 50.f, true);
    walker.mounted = false;
    Flight g;
    Step(g, walker);
    CHECK(g.phase == Phase::None);
    Step(g, Mounted(21000, 70.f, 50.f, true));
    CHECK(g.phase == Phase::Glide);
    Sense off = Mounted(21500, 66.f, 50.f, true);
    off.mounted = false;
    Step(g, off);
    CHECK(g.phase == Phase::None);
}

TEST(a_launch_point_gives_a_burst_of_flight_then_a_glide_down)
{
    Flight f;
    Sense s = Mounted(1000, 50.f, 50.f, true);
    s.jumped = true;
    s.launch = &kPoint;
    Step(f, s);
    CHECK(f.phase == Phase::Burst && f.point == 7 && f.ceilingZ == 120.f);
    CHECK_NEAR(SecondsLeft(f, 1000), 15.f, 1e-3f);
    // Flying about under the ceiling.
    Sense up = Mounted(5000, 110.f, 50.f, false);
    up.x = 200.f;
    Step(f, up);
    CHECK(f.phase == Phase::Burst);
    // Time's up: a glide down, and a cooldown on the next take-off.
    Step(f, Mounted(16000, 110.f, 50.f, false));
    CHECK(f.phase == Phase::Descent && f.nextLaunchMs == 16000 + uint32_t(kLaunchCooldownSeconds * 1000.f));
    // Not down the moment the flight ends (the client hasn't reported falling yet)...
    Step(f, Mounted(16100, 109.f, 50.f, false));
    CHECK(f.phase == Phase::Descent);
    // ...but once on the ground.
    Step(f, Mounted(26000, 50.5f, 50.f, false));
    CHECK(f.phase == Phase::None);
    // Too soon to fly again.
    s.nowMs = 30000;
    Step(f, s);
    CHECK(f.phase == Phase::None);
    s.nowMs = 47000;
    Step(f, s);
    CHECK(f.phase == Phase::Burst);
}

TEST(a_flight_ends_at_its_ceiling_its_range_and_its_site)
{
    auto takeOff = [](Plots::Site const* site) {
        Flight f;
        Sense s = Mounted(1000, 50.f, 50.f, true);
        s.jumped = true;
        s.launch = &kPoint;
        s.site = site;
        Step(f, s);
        return f;
    };
    Flight f = takeOff(nullptr);
    Step(f, Mounted(2000, 121.f, 50.f, false));
    CHECK(f.phase == Phase::Descent);
    f = takeOff(nullptr);
    Sense far = Mounted(3000, 80.f, 50.f, false);
    far.x = 100.f + 201.f;
    Step(f, far);
    CHECK(f.phase == Phase::Descent);
    // In a site: its lower ceiling, and its bounds.
    Plots::Site site;
    site.bounds = {0.f, 0.f, 150.f, 400.f};
    site.ceilingZ = 90.f;
    f = takeOff(&site);
    CHECK(f.bounded && f.ceilingZ == 90.f);
    Step(f, Mounted(2000, 95.f, 50.f, false));
    CHECK(f.phase == Phase::Descent);
    f = takeOff(&site);
    Sense out = Mounted(2000, 80.f, 50.f, false);
    out.x = 151.f;
    Step(f, out);
    CHECK(f.phase == Phase::Descent);
    // Knocked off: down it comes, gently.
    f = takeOff(nullptr);
    Sense off = Mounted(2000, 80.f, 50.f, false);
    off.mounted = false;
    Step(f, off);
    CHECK(f.phase == Phase::Descent);
    // Dead, or a game master's own flight: nothing at all.
    f = takeOff(nullptr);
    Sense dead = Mounted(2000, 80.f, 50.f, false);
    dead.alive = false;
    Step(f, dead);
    CHECK(f.phase == Phase::None);
}

TEST(the_flight_wire)
{
    Flight f;
    f.phase = Phase::Burst;
    f.untilMs = 11000;
    f.ceilingZ = 260.f;
    std::vector<uint8_t> const b = EncodeFlight(f, 1000);
    CHECK(b.size() == 10 && b[0] == kOpFlight && b[1] == uint8_t(Phase::Burst));
    float left = 0.f, ceiling = 0.f;
    std::memcpy(&left, &b[2], 4);
    std::memcpy(&ceiling, &b[6], 4);
    CHECK_NEAR(left, 10.f, 1e-4f);
    CHECK(ceiling == 260.f);
    CHECK(EncodeFlight(Flight{}, 1000)[1] == 0);
}

TEST(riding_grenades_fly_from_the_ground_but_not_from_a_skyride)
{
    using namespace Fusion::Riding;
    CHECK(ThrowsFromSaddle(false, 0.f));
    CHECK(ThrowsFromSaddle(false, 40.f));   // a ground mount falling off a cliff still may
    CHECK(ThrowsFromSaddle(true, 0.5f));    // a flyer standing on the ground
    CHECK(!ThrowsFromSaddle(true, 10.f));   // skyriding
}
