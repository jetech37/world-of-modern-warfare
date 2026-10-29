/*
 * Azeroth Warfare: tests for launchers' flight and the blast messages (Launch.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Launch.h"

#include <cmath>
#include <cstring>

using namespace Fusion;
using namespace Fusion::Launch;

namespace
{
    float FloatAt(std::vector<uint8_t> const& b, size_t at)
    {
        uint32_t v = uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
}

TEST(rockets_and_grenades_fly_at_their_speeds)
{
    WeaponProfile rpg;
    rpg.name = "rpg_mp";
    rpg.weaponClass = WeaponClass::Launcher;
    WeaponProfile m79 = rpg;
    m79.name = "m79_mp";
    WeaponProfile gl = rpg;
    gl.name = "gl_m4_mp";
    gl.slot = WeaponSlot::Alternate;
    CHECK_NEAR(ProjectileSpeed(rpg), 40.f, 1e-6);
    CHECK_NEAR(ProjectileSpeed(m79), 45.f, 1e-6);
    CHECK_NEAR(ProjectileSpeed(gl), 45.f, 1e-6);
    // 90 yards: 2.25 seconds for a rocket, two for a grenade.
    CHECK(FlightMs(90.f, ProjectileSpeed(rpg)) == 2250);
    CHECK(FlightMs(90.f, ProjectileSpeed(gl)) == 2000);
    CHECK(FlightMs(0.f, 40.f) == 1);
    CHECK(FlightMs(1e6f, 1.f) == 65535);
}

TEST(the_explosion_message_is_nineteen_bytes)
{
    std::vector<uint8_t> const b = EncodeExplosion({1.5f, -2.f, 300.25f}, 11.11f, 42);
    CHECK(b.size() == 19);
    CHECK(b[0] == 30);
    CHECK_NEAR(FloatAt(b, 1), 1.5f, 1e-6);
    CHECK_NEAR(FloatAt(b, 5), -2.f, 1e-6);
    CHECK_NEAR(FloatAt(b, 9), 300.25f, 1e-6);
    CHECK_NEAR(FloatAt(b, 13), 11.11f, 1e-6);
    CHECK(b[17] == 42 && b[18] == 0);
}

TEST(the_launch_message_is_thirty_seven_bytes)
{
    std::vector<uint8_t> const b = EncodeLaunch(0x1122334455667788ull, 1002, {1.f, 2.f, 3.f}, {4.f, 5.f, 6.f}, 2000);
    CHECK(b.size() == 37);
    CHECK(b[0] == 31);
    CHECK(b[1] == 0x88 && b[8] == 0x11);
    CHECK(b[9] == (1002 & 0xFF) && b[10] == (1002 >> 8));
    CHECK_NEAR(FloatAt(b, 11), 1.f, 1e-6);
    CHECK_NEAR(FloatAt(b, 19), 3.f, 1e-6);
    CHECK_NEAR(FloatAt(b, 23), 4.f, 1e-6);
    CHECK_NEAR(FloatAt(b, 31), 6.f, 1e-6);
    CHECK(b[35] == (2000 & 0xFF) && b[36] == (2000 >> 8));
}

TEST(a_grenade_falls_under_gravity)
{
    Vec3 const from{10.f, 20.f, 30.f};
    Vec3 const v{25.f, 0.f, 0.f};
    // After two seconds: 50 yards on and 30 yards down (half of 15 times four).
    Vec3 const p = ArcAt(from, v, kGrenadeGravity, 2.f);
    CHECK_NEAR(p.x, 60.f, 1e-4);
    CHECK_NEAR(p.y, 20.f, 1e-4);
    CHECK_NEAR(p.z, 0.f, 1e-4);
    CHECK_NEAR(ArcAt(from, v, kGrenadeGravity, 0.f).z, 30.f, 1e-6);
}

TEST(a_grenade_fired_level_lands_on_flat_ground)
{
    // Muzzle 1.5 yards over flat ground at z = 0: it lands after sqrt(2 * 1.5 / 15) s.
    Vec3 const from{0.f, 0.f, 1.5f};
    Vec3 const v{kGrenadeSpeed, 0.f, 0.f};
    auto ground = [](Vec3 a, Vec3 b, float& f) {
        if (b.z >= 0.f)
            return false;
        f = a.z / (a.z - b.z);
        return true;
    };
    ArcEnd const e = FlyArc(from, v, kGrenadeGravity, 0.02f, 6.f, 0.f, ground);
    CHECK(e.contact);
    float const t = std::sqrt(2.f * 1.5f / 15.f);
    CHECK_NEAR(e.flightMs, t * 1000.f, 2.f);
    CHECK_NEAR(e.at.z, 0.f, 0.05f);
    CHECK_NEAR(e.at.x, kGrenadeSpeed * t, 0.05f);
}

TEST(a_grenade_lobbed_high_carries_far)
{
    // MW2's launcher reaches across a map: fired 45 degrees up it carries well past 120 yards.
    Vec3 const from{0.f, 0.f, 1.5f};
    float const c = std::sqrt(0.5f);
    Vec3 const v{kGrenadeSpeed * c, 0.f, kGrenadeSpeed * c};
    auto ground = [](Vec3 a, Vec3 b, float& f) {
        if (b.z >= 0.f)
            return false;
        f = a.z / (a.z - b.z);
        return true;
    };
    ArcEnd const e = FlyArc(from, v, kGrenadeGravity, 0.02f, 12.f, 0.f, ground);
    CHECK(e.contact);
    CHECK(e.at.x > 120.f);
}

TEST(the_burst_point_is_exactly_where_the_client_draws_it)
{
    // The client rebuilds the arc from [from, to, flight_ms] alone: v0 = (to - from - g T^2 / 2) / T.
    Vec3 const from{0.f, 0.f, 10.f};
    Vec3 const v = Vec3{1.f, 0.f, 1.f}.Normalized() * kGrenadeSpeed;
    auto ground = [](Vec3 a, Vec3 b, float& f) {
        if (b.z >= 0.f)
            return false;
        f = a.z / (a.z - b.z);
        return true;
    };
    ArcEnd const e = FlyArc(from, v, kGrenadeGravity, 0.02f, 6.f, 0.2f, ground);
    float const T = e.flightMs / 1000.f;
    Vec3 const g{0.f, 0.f, -kGrenadeGravity};
    Vec3 const v0 = (e.at - from - g * (0.5f * T * T)) * (1.f / T);
    // Halfway along, the client's arc and the server's agree.
    Vec3 const client = from + v0 * (T / 2.f) + g * (0.5f * (T / 2.f) * (T / 2.f));
    Vec3 const server = ArcAt(from, v, kGrenadeGravity, T / 2.f);
    CHECK_NEAR(client.x, server.x, 1e-3);
    CHECK_NEAR(client.z, server.z, 1e-3);
    // The burst sits a little short of the ground, so the blast's sight lines start in the open.
    CHECK(e.at.z > 0.f && e.at.z < 0.3f);
}

TEST(a_grenade_that_touches_nothing_bursts_at_the_cap)
{
    auto nothing = [](Vec3, Vec3, float&) { return false; };
    ArcEnd const e = FlyArc({0.f, 0.f, 0.f}, {0.f, 0.f, kGrenadeSpeed}, kGrenadeGravity, 0.02f, 6.f, 0.2f, nothing);
    CHECK(!e.contact);
    CHECK(e.flightMs == 6000);
    // Straight up for six seconds: v t - g t^2 / 2.
    CHECK_NEAR(e.at.z, kGrenadeSpeed * 6.f - 0.5f * kGrenadeGravity * 36.f, 1e-2);
}

TEST(a_grenade_stops_at_the_first_contact_along_its_path)
{
    // A wall at x = 10, which the contact test reports as a fraction of each step.
    auto wall = [](Vec3 a, Vec3 b, float& f) {
        if (a.x > 10.f || b.x < 10.f)
            return false;
        f = (10.f - a.x) / (b.x - a.x);
        return true;
    };
    ArcEnd const e = FlyArc({0.f, 0.f, 5.f}, {kGrenadeSpeed, 0.f, 0.f}, kGrenadeGravity, 0.02f, 6.f, 0.f, wall);
    CHECK(e.contact);
    CHECK_NEAR(e.flightMs, 10.f / kGrenadeSpeed * 1000.f, 1.f);
    CHECK_NEAR(e.at.x, 10.f, 0.03f);
}

TEST(a_lob_lands_where_it_was_aimed)
{
    Vec3 const from{0.f, 0.f, 1.4f};
    Vec3 const to{12.f, -5.f, -2.f};
    Vec3 const v = LobVelocity(from, to, kGrenadeGravity, 1.5f);
    Vec3 const p = ArcAt(from, v, kGrenadeGravity, 1.5f);
    CHECK_NEAR(p.x, to.x, 1e-4);
    CHECK_NEAR(p.y, to.y, 1e-4);
    CHECK_NEAR(p.z, to.z, 1e-4);
    CHECK(v.z > 0.f);       // lobbed up
}
