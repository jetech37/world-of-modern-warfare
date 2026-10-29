/*
 * Azeroth Warfare: tests for warded airspace (Airspace.h, its hold in Riding.h). GPL-2.0-or-later,
 * see Common.h.
 */

#include "Test.h"
#include "fusion/Airspace.h"
#include "fusion/Riding.h"

using namespace Fusion;
using namespace Fusion::Wards;

TEST(airspace_the_table_is_sound)
{
    std::vector<Zone> const& z = Zones();
    CHECK(z.size() >= 11);
    for (size_t i = 0; i < z.size(); ++i)
    {
        CHECK(z[i].radius > 0.f);
        CHECK(z[i].aboveGround > 0.f || z[i].maxZ < kNoZ);
        for (size_t j = i + 1; j < z.size(); ++j)
            CHECK(z[i].id != z[j].id);
        for (Pad const& p : z[i].pads)
            CHECK(Contains(z[i], z[i].map, p.x, p.y, 0, 0));
    }
    // The client's push always beats the server's hold.
    CHECK(kHoldRate < kPushMin);
}

TEST(airspace_lookups_find_the_cities_and_the_lowest_ceiling)
{
    Here h;
    // Stormwind's trade district, 60 over the ground at 100: the ceiling is ground + 30.
    CHECK(Find(Zones(), 0, -8830.f, 640.f, 100.f, 0, 0, h));
    CHECK(h.zone->id == 1 && h.ceilingZ == 130.f && !h.pad);
    // The gryphon tower is a pad: its headroom.
    CHECK(Find(Zones(), 0, -8840.f, 490.f, 109.6f, 0, 0, h));
    CHECK(h.pad && h.ceilingZ >= 109.6f + kPadHeadroom);
    // Wrong map, far away: nothing.
    CHECK(!Find(Zones(), 1, -8830.f, 640.f, 100.f, 0, 0, h));
    CHECK(!Find(Zones(), 0, 0.f, -3000.f, 100.f, 0, 0, h));
    // An AreaTable id counts just outside the circle, not across the world.
    Zone const& sw = Zones()[0];
    CHECK(!Contains(sw, 0, sw.x + sw.radius * 1.2f, sw.y, 0, 0));
    CHECK(Contains(sw, 0, sw.x + sw.radius * 1.2f, sw.y, 1519, 12));
    CHECK(!Contains(sw, 0, sw.x + sw.radius * 2.f, sw.y, 1519, 12));
    // Thunder Bluff: the fixed cap under a high ground ceiling.
    CHECK(Find(Zones(), 1, -1200.f, 0.f, 230.f, 0, 0, h));
    CHECK(h.ceilingZ == 260.f);
    // Hyjal: the fixed invisible ceiling.
    CHECK(Find(Zones(), 1, 5000.f, -3000.f, 1200.f, 0, 0, h));
    CHECK(h.zone->kind == Kind::Unfinished && h.ceilingZ == 1700.f);
    // Darnassus inside the Teldrassil canopy: the lower of the two.
    CHECK(Find(Zones(), 1, 9950.f, 2300.f, 1300.f, 0, 0, h));
    CHECK(h.zone->id == 6 && h.ceilingZ == 1325.f);
    CHECK(Find(Zones(), 1, 9950.f, 2300.f, 1410.f, 0, 0, h));
    CHECK(h.zone->id == 21 && h.ceilingZ == 1420.f);
}

// The client (client/src/riding.rs, `the_ward_matches_the_server`) gives the same numbers.
TEST(airspace_the_push_eases_under_and_pushes_over)
{
    CHECK_NEAR(AllowedUp(97.f, 100.f), 6.f, 1e-5f);
    CHECK_NEAR(AllowedUp(100.f, 100.f), 0.f, 1e-5f);
    CHECK_NEAR(AllowedUp(120.f, 100.f), -14.f, 1e-5f);
    CHECK_NEAR(AllowedUp(500.f, 100.f), -kPushMax, 1e-5f);
    CHECK(AllowedUp(0.f, 100.f) > 100.f);
}

TEST(airspace_the_hold_lets_a_pushed_rider_down_and_catches_a_hoverer)
{
    bool entered = false;
    // Come in 100 over the ceiling, pushed down as the client does: never caught.
    Ward w;
    float z = 200.f;
    CHECK(Hold(w, 1, 100.f, z, 1000, entered));
    CHECK(entered);
    for (uint32_t t = 1100; t < 30000; t += 100)
    {
        z += AllowedUp(z, 100.f) * 0.1f;
        CHECK(Hold(w, 1, 100.f, z, t, entered));
    }
    CHECK(z <= 100.5f);
    // Hovering there instead: caught within a few seconds.
    Ward h;
    CHECK(Hold(h, 1, 100.f, 200.f, 1000, entered));
    bool held = true;
    uint32_t t = 1000;
    while (held && t < 60000)
    {
        t += 100;
        held = Hold(h, 1, 100.f, 200.f, t, entered);
    }
    CHECK(!held && t < 6000);
    // A ceiling that drops under the rider (off a cliff edge) is not held against them.
    Ward c;
    CHECK(Hold(c, 1, 100.f, 95.f, 1000, entered));
    CHECK(Hold(c, 1, 40.f, 95.f, 1100, entered));
    CHECK(entered);
    CHECK(Hold(c, 1, 40.f, 94.f, 1200, entered));
    // Out of the airspace: nothing held.
    CHECK(Hold(c, 0, 0.f, 9999.f, 1300, entered) && c.zone == 0);
    // The wire.
    std::vector<uint8_t> const b = EncodeWard(WARD_IN | WARD_PAD, 130.f);
    CHECK(b.size() == 6 && b[0] == kOpWard && b[1] == (WARD_IN | WARD_PAD));
}

TEST(airspace_a_skyride_hovering_over_a_city_is_brought_down)
{
    Riding::Sense s;
    s.mounted = true;
    s.skyrider = true;
    s.airborne = true;
    s.map = 0;
    s.z = 300.f;
    s.groundZ = 100.f;
    s.speed = 14.f;
    s.nowMs = 1000;
    Riding::Flight f;
    Riding::Step(f, s);
    CHECK(f.phase == Riding::Phase::Skyride);
    s.airborne = false;
    s.wardZone = 1;
    s.wardCeilingZ = 130.f;
    bool pushed = false;
    // Falling as the law allows, but holding far over the ceiling.
    for (int i = 0; i < 60 && f.phase == Riding::Phase::Skyride; ++i)
    {
        s.nowMs += 200;
        s.z -= 0.4f;
        Riding::Step(f, s);
        pushed = pushed || f.wardPushed;
    }
    CHECK(pushed);
    CHECK(f.phase == Riding::Phase::Descent && f.wardBroke);
}
