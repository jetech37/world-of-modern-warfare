/*
 * Azeroth Warfare: tests for the gunfight rules (Gunfights.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Ballistics.h"
#include "fusion/Gunfights.h"
#include "fusion/HitValidation.h"
#include "fusion/Weapon.h"

using namespace Fusion;

namespace
{
    WeaponProfile M4()
    {
        WeaponProfile w;
        w.name = "m4_mp";
        w.weaponClass = WeaponClass::AssaultRifle;
        w.damage = 30.f;
        w.minDamage = 20.f;
        w.maxDamageRange = 41.67f;
        w.minDamageRange = 55.56f;
        w.hipSpreadMin = 3.f;
        w.hipSpreadMax = 7.f;
        w.adsSpread = 0.f;
        return w;
    }

    NpcAimInput Aim(float distance, uint32_t heldMs)
    {
        NpcAimInput in;
        in.distance = distance;
        in.heldMs = heldMs;
        return in;
    }
}

TEST(player_rounds_carry_past_what_a_client_draws)
{
    CHECK_NEAR(kMaxShotRange, kGunfight.shotRange, 1e-6);
    CHECK(kMaxShotRange >= 150.f);
    WeaponProfile const m4 = M4();
    CHECK_NEAR(PelletRange(m4), kMaxShotRange, 1e-6);
}

TEST(falloff_is_mw2s_stretched_for_wow_and_never_a_cap)
{
    WeaponProfile const m4 = M4();
    float const k = FalloffRangeScale(WeaponClass::AssaultRifle);
    CHECK_NEAR(k, kGunfight.falloffRifle, 1e-6);
    // Full damage out to the stretched max-damage range, the least from the stretched
    // min-damage range, linear between, and still the least far beyond it.
    CHECK_NEAR(CodDamageAtRange(m4, 41.67f * k - 0.1f), 30.f, 1e-4);
    CHECK_NEAR(CodDamageAtRange(m4, (41.67f + 55.56f) * 0.5f * k), 25.f, 1e-3);
    CHECK_NEAR(CodDamageAtRange(m4, 55.56f * k + 0.1f), 20.f, 1e-4);
    CHECK_NEAR(CodDamageAtRange(m4, 140.f), 20.f, 1e-4);
    // An SMG falls off sooner than a rifle, a sniper not at all short of its own range.
    CHECK(FalloffRangeScale(WeaponClass::Sniper) <= FalloffRangeScale(WeaponClass::AssaultRifle));
    // A shotgun's pellets stop at its stretched least-damage range times the reach.
    WeaponProfile spas;
    spas.weaponClass = WeaponClass::Shotgun;
    spas.minDamageRange = 19.44f;
    CHECK_NEAR(PelletRange(spas), 19.44f * FalloffRangeScale(WeaponClass::Shotgun) * kShotgunReach, 1e-4);
}

TEST(the_hip_is_tighter_and_the_sights_are_on)
{
    WeaponProfile m4 = M4();
    // The server's floor follows the client's narrower hip cone.
    CHECK_NEAR(LeastSpread(m4, false), 3.f * kGunfight.hipSpreadScale * 0.5f * 0.65f, 1e-5);
    CHECK(kGunfight.hipSpreadScale < 1.f && kGunfight.hipSpreadScale >= 0.5f);
    // A rifle, LMG or sniper down the sights is held near perfect, whatever its WeaponDef says.
    m4.adsSpread = 1.5f;
    CHECK_NEAR(AimedSpread(m4), kGunfight.adsCapDeg, 1e-6);
    CHECK(ClampSpread(m4, true, 0.f) <= kGunfight.adsCapDeg);
    WeaponProfile sniper = M4();
    sniper.weaponClass = WeaponClass::Sniper;
    sniper.adsSpread = 0.5f;
    CHECK(AimedSpread(sniper) <= 0.1f);
    // A shotgun keeps its aimed cone (its pellets spread).
    WeaponProfile spas = M4();
    spas.weaponClass = WeaponClass::Shotgun;
    spas.adsSpread = 5.5f;
    CHECK_NEAR(AimedSpread(spas), 5.5f, 1e-6);
}

TEST(an_npcs_aim_starts_wide_and_settles)
{
    float const first = NpcAimCone(Aim(20.f, 0));
    float const half = NpcAimCone(Aim(20.f, kGunfight.settleMs / 2));
    float const settled = NpcAimCone(Aim(20.f, kGunfight.settleMs));
    CHECK(first > half && half > settled);
    CHECK_NEAR(settled, NpcAimCone(Aim(20.f, 60000)), 1e-6);
    // It settles in one to two seconds.
    CHECK(kGunfight.settleMs >= 1000 && kGunfight.settleMs <= 2000);
    float const body = NpcTargetBody(Stance::Stand, false);
    // The first round at 20 yards mostly misses; a settled aim is dangerous at medium range.
    CHECK(NpcConeHitChance(first, 20.f, body, 0) < 0.25f);
    CHECK(NpcConeHitChance(settled, 20.f, body, 0) > 0.4f);
    CHECK(NpcConeHitChance(settled, 10.f, body, 0) >= 0.7f);
}

TEST(an_npcs_aim_widens_with_range_movement_and_cover)
{
    CHECK(NpcAimCone(Aim(40.f, 5000)) > NpcAimCone(Aim(20.f, 5000)));
    CHECK_NEAR(NpcAimCone(Aim(5.f, 5000)), NpcAimCone(Aim(15.f, 5000)), 1e-6);   // flat up close
    NpcAimInput run = Aim(20.f, 5000);
    run.gait = Gait::Run;
    NpcAimInput sprint = run;
    sprint.sprinting = true;
    CHECK(NpcAimCone(sprint) > NpcAimCone(run));
    CHECK(NpcAimCone(run) > NpcAimCone(Aim(20.f, 5000)));
    NpcAimInput cover = Aim(20.f, 5000);
    cover.partialCover = true;
    CHECK(NpcAimCone(cover) > NpcAimCone(Aim(20.f, 5000)));
    CHECK(NpcTargetBody(Stance::Stand, true) < NpcTargetBody(Stance::Stand, false));
    CHECK(NpcTargetBody(Stance::Prone, false) < NpcTargetBody(Stance::Crouch, false));
    // The farther, the less it hits.
    float const body = NpcTargetBody(Stance::Stand, false);
    CHECK(NpcConeHitChance(NpcAimCone(Aim(40.f, 5000)), 40.f, body, 0) < NpcConeHitChance(NpcAimCone(Aim(20.f, 5000)), 20.f, body, 0));
}

TEST(elites_and_rares_aim_tighter)
{
    NpcAimInput normal = Aim(25.f, 5000);
    NpcAimInput elite = normal;
    elite.rank = CreatureRank::Elite;
    NpcAimInput rareElite = normal;
    rareElite.rank = CreatureRank::RareElite;
    NpcAimInput rare = normal;
    rare.rank = CreatureRank::Rare;
    CHECK(NpcAimCone(elite) < NpcAimCone(normal));
    CHECK(NpcAimCone(rare) < NpcAimCone(normal));
    CHECK(NpcAimCone(rareElite) < NpcAimCone(elite));
}

TEST(a_miss_passes_close_but_clear)
{
    float const body = NpcTargetBody(Stance::Stand, false);
    for (float roll : {0.f, 0.3f, 1.f})
    {
        float const off = NpcMissOffset(6.f, 25.f, body, roll);
        CHECK(off >= body + kGunfight.missClearance - 1e-5f);
        CHECK(off <= 25.f * std::tan(6.f * 3.14159265f / 180.f) + 1e-3f);
    }
    // A cone narrower than the body still puts the miss just outside it.
    CHECK(NpcMissOffset(0.5f, 10.f, body, 1.f) > body);
}

TEST(an_npcs_aim_resets_when_the_target_leaves_sight)
{
    NpcAimTrack t;
    t.Update(7, true, 1000);
    CHECK(t.HeldMs(1000) == 0);
    t.Update(7, true, 2000);
    CHECK(t.HeldMs(2000) == 1000);
    // A short break keeps half.
    t.Update(7, false, 2000);
    CHECK(t.HeldMs(2300) == 0);
    t.Update(7, true, 2300);
    CHECK(t.HeldMs(2300) == 500);
    CHECK(t.HeldMs(2800) == 1000);
    // A long one starts over.
    t.Update(7, false, 3000);
    t.Update(7, true, 3000 + kGunfight.sightGraceMs + 1);
    CHECK(t.HeldMs(3000 + kGunfight.sightGraceMs + 1) == 0);
    // A new target too.
    t.Update(7, true, 6000);
    CHECK(t.HeldMs(6000) > 0);
    t.Update(8, true, 6000);
    CHECK(t.HeldMs(6000) == 0);
}

TEST(gunfire_draws_the_near_in_sight_first_and_only_a_few)
{
    std::vector<Listener> ls = {
        {1, 50.f, true},        // in sight within 60: comes at once
        {2, 25.f, false},       // behind a wall within 30: comes after a beat
        {3, 40.f, false},       // behind a wall past 30: does not hear it
        {4, 70.f, true},        // past 60
        {5, 10.f, true},
        {6, 12.f, true},
    };
    std::vector<HeardBy> const heard = WhoHearsTheShot(ls, 60.f, 100, 1234);
    CHECK(heard.size() == kGunfight.hearingPerShot);
    // Nearest first: 5, 6, then 2 (after a beat).
    CHECK(heard.size() >= 3 && heard[0].guid == 5 && heard[1].guid == 6 && heard[2].guid == 2);
    CHECK(heard[0].delayMs == 0 && heard[1].delayMs == 0);
    CHECK(heard[2].delayMs >= kGunfight.hearingDelayMinMs && heard[2].delayMs <= kGunfight.hearingDelayMaxMs);
    // The budget caps it too, and nothing heard draws nothing.
    CHECK(WhoHearsTheShot(ls, 60.f, 1, 1).size() == 1);
    CHECK(WhoHearsTheShot(ls, 60.f, 0, 1).empty());
    CHECK(WhoHearsTheShot({{3, 40.f, false}, {4, 70.f, true}}, 60.f, 10, 1).empty());
    // A suppressed shot reaches only the near.
    std::vector<HeardBy> const quiet = WhoHearsTheShot(ls, GunfireHearing(true, false), 100, 1);
    CHECK(quiet.size() == 2 && quiet[0].guid == 5 && quiet[1].guid == 6);
}

TEST(a_shooter_draws_only_so_many_in_a_while)
{
    HearingBudget b;
    CHECK(b.Left(1000) == kGunfight.hearingPerWindow);
    b.Spend(3, 1000);
    b.Spend(3, 2000);
    CHECK(b.Left(3000) == kGunfight.hearingPerWindow - 6);
    b.Spend(5, 3000);
    CHECK(b.Left(4000) == 0);
    // A new window, a new allowance.
    CHECK(b.Left(1000 + kGunfight.hearingWindowMs) == kGunfight.hearingPerWindow);
}

TEST(the_gunfight_table_reads_as_the_design_says)
{
    // Design notes (IDEAS.md): heard at about 60 yards, 15 suppressed; a hit creature calls its
    // own within about 20; aggroed creatures rush at about 1.6x.
    CHECK_NEAR(kGunfight.hearingOpen, 60.f, 1e-6);
    CHECK_NEAR(kGunfight.hearingSilenced, 15.f, 1e-6);
    CHECK_NEAR(kGunfight.callForHelpRadius, 20.f, 1e-6);
    CHECK_NEAR(kGunfight.chaseSpeed, 1.6f, 1e-6);
    CHECK(kGunfight.chaseFastBand > kGunfight.chaseSlowBand);
    CHECK(kGunfight.hearingPerShot <= kGunfight.hearingPerWindow);
    CHECK(kGunfight.hearingDelayMinMs <= kGunfight.hearingDelayMaxMs);
}
