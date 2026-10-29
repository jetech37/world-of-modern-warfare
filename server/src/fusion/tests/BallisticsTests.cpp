/*
 * Azeroth Warfare: tests for the pellet rays (Ballistics.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Ballistics.h"
#include "fusion/Gunfights.h"

#include <cmath>

using namespace Fusion;

TEST(pellets_hash_as_the_client_does)
{
    // Shared with benilla-protocol's `pellets_hash_as_the_server_does`.
    CHECK(PelletHash(0, 0) == 0u);
    CHECK(PelletHash(1234, 0) == 0x400c726eu);
    CHECK(PelletHash(1234, 1) == 0x2bf47b3eu);
    CHECK(PelletHash(1234, 7) == 0x0087797au);
    CHECK(PelletHash(0xDEADBEEFu, 3) == 0xec239f07u);
}

TEST(pellets_scatter_inside_the_cone)
{
    Vec3 const dir{0.f, 1.f, 0.f};
    Vec3 const straight = PelletDirection(dir, 99, 0, 0.f);
    CHECK_NEAR(straight.y, 1.f, 1e-6);
    float const cosCone = std::cos(5.f * 3.14159265f / 180.f);
    int spreadOut = 0;
    for (uint32_t i = 0; i < 64; ++i)
    {
        Vec3 const d = PelletDirection(dir, 4321, i, 5.f);
        CHECK_NEAR(d.Length(), 1.f, 1e-4);
        CHECK(d.y >= cosCone - 1e-4f);
        if (d.y < std::cos(3.14159265f / 180.f))
            ++spreadOut;
    }
    CHECK(spreadOut > 32);
    Vec3 const up = PelletDirection({0.f, 0.f, 1.f}, 7, 3, 5.f);
    CHECK(up.z < 1.f && up.z >= cosCone - 1e-4f);
}

TEST(the_server_holds_the_spread_to_the_guns_least)
{
    WeaponProfile w;
    w.hipSpreadMin = 4.f;
    w.adsSpread = 0.5f;
    // Hip: the client draws MW2's cone at kGunfight.hipSpreadScale; MW2's prone halves the stand
    // minimum and Steady Aim takes 35% off: the floor allows all three.
    CHECK_NEAR(LeastSpread(w, false), 4.f * kGunfight.hipSpreadScale * 0.5f * 0.65f, 1e-5);
    // Aimed: a rifle's held to the cap; another gun keeps its own.
    CHECK_NEAR(LeastSpread(w, true), kGunfight.adsCapDeg * 0.9f, 1e-5);
    WeaponProfile smg = w;
    smg.weaponClass = WeaponClass::Smg;
    CHECK_NEAR(LeastSpread(smg, true), 0.5f * 0.9f, 1e-5);
    CHECK_NEAR(ClampSpread(w, false, 0.f), LeastSpread(w, false), 1e-6);   // a zero-spread claim
    CHECK_NEAR(ClampSpread(w, false, 7.f), 7.f, 1e-6);
    CHECK_NEAR(ClampSpread(w, false, 500.f), kMaxSpreadDeg, 1e-6);
    CHECK_NEAR(ClampSpread(w, false, std::nanf("")), LeastSpread(w, false), 1e-6);
}

TEST(a_shotgun_pellet_stops_at_its_range)
{
    WeaponProfile spas;
    spas.weaponClass = WeaponClass::Shotgun;
    spas.pellets = 8;
    spas.minDamageRange = 19.44f;
    CHECK_NEAR(PelletRange(spas), 19.44f * kShotgunReach, 1e-4);   // min damage out to there
    CHECK_NEAR(kShotgunReach, 2.f, 1e-6);
    WeaponProfile m4;
    CHECK_NEAR(PelletRange(m4), kMaxShotRange, 1e-5);
}

TEST(the_nearest_body_on_the_ray_takes_the_pellet)
{
    std::vector<PelletCandidate> c = {{7, 30.f}, {3, 12.f}, {9, 50.f}};
    CHECK(NearestCandidate(c) == 1);
    CHECK(NearestCandidate({}) == -1);
}

TEST(a_flyer_is_hit_where_it_is_drawn)
{
    // A vulture (carrion bird) walks the ground on the server while its model hovers above it.
    CHECK_NEAR(BodyLift(7, false, false), kFlyerLift, 1e-6);
    CHECK_NEAR(BodyLift(24, false, false), kFlyerLift, 1e-6);      // bat
    CHECK_NEAR(BodyLift(0, true, false), kFlyerLift, 1e-6);        // inhabits the air
    CHECK_NEAR(BodyLift(0, false, true), kFlyerLift, 1e-6);        // hovering
    CHECK_NEAR(BodyLift(1, false, false), 0.f, 1e-6);              // a wolf
}

TEST(fmj_rounds_go_deeper)
{
    WeaponProfile plain;
    WeaponProfile fmj;
    fmj.fmj = true;
    CHECK(BodiesPierced(plain) == 1);
    CHECK(BodiesPierced(fmj) == 2);         // through the first body into the next
    CHECK_NEAR(PierceScale(0), 1.f, 1e-6);
    CHECK_NEAR(PierceScale(1), kFmjBodyScale, 1e-6);
    CHECK(!ThroughWall(plain, 0.3f));       // only FMJ goes through walls here
    CHECK(ThroughWall(fmj, 0.3f));
    CHECK(ThroughWall(fmj, kFmjWallDepth));
    CHECK(!ThroughWall(fmj, kFmjWallDepth + 0.5f));
}

TEST(a_blast_fades_to_its_edge)
{
    WeaponProfile gl;
    gl.explosionRadius = 8.33f;
    gl.explosionInner = 155.f;
    gl.explosionOuter = 25.f;
    CHECK(IsExplosive(gl));
    CHECK_NEAR(SplashDamage(gl, 0.f), 155.f, 1e-4);
    CHECK_NEAR(SplashDamage(gl, 8.33f / 2.f), 90.f, 1e-3);
    CHECK_NEAR(SplashDamage(gl, 8.33f), 25.f, 1e-4);
    CHECK_NEAR(SplashDamage(gl, 9.f), 0.f, 1e-6);       // past the edge
    WeaponProfile m4;
    CHECK(!IsExplosive(m4));
}
