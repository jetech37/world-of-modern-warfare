/*
 * Azeroth Warfare: the fusion rules' tests. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"

#include "fusion/HitValidation.h"

using namespace Fusion;

namespace
{
    BodyPose StandingAt(Vec3 feet, Stance stance = Stance::Stand)
    {
        BodyPose p;
        p.feet = feet;
        p.stance = stance;
        return p;
    }

    WeaponProfile Rifle()
    {
        WeaponProfile w;
        w.id = 1;
        w.fireTime = 0.1f;
        return w;
    }
}

TEST(ray_through_the_head_is_a_headshot)
{
    HitboxShape s;
    BodyPose p = StandingAt({10.f, 0.f, 0.f});
    // Eye height of the head centre: 2.0 - 0.18.
    auto hit = RayVsBody({0.f, 0.f, 1.82f}, {1.f, 0.f, 0.f}, p, s);
    CHECK(hit.has_value());
    CHECK(hit->location == HitLocation::Head);
    CHECK(hit->distance > 9.4f && hit->distance < 10.f);
}

TEST(height_bands_split_the_body)
{
    HitboxShape s;
    BodyPose p = StandingAt({10.f, 0.f, 0.f});
    auto chest = RayVsBody({0.f, 0.f, 1.3f}, {1.f, 0.f, 0.f}, p, s);
    auto belly = RayVsBody({0.f, 0.f, 0.95f}, {1.f, 0.f, 0.f}, p, s);
    auto leg = RayVsBody({0.f, 0.f, 0.4f}, {1.f, 0.f, 0.f}, p, s);
    CHECK(chest && chest->location == HitLocation::TorsoUpper);
    CHECK(belly && belly->location == HitLocation::TorsoLower);
    CHECK(leg && leg->location == HitLocation::Leg);
    // A graze at the edge of the chest is an arm.
    auto side = RayVsBody({10.f, -10.f, 1.3f}, {0.f, 1.f, 0.f}, p, s);
    CHECK(side && side->location == HitLocation::TorsoUpper);
    auto arm = RayVsBody({0.f, 0.4f, 1.3f}, {1.f, 0.f, 0.f}, p, s);
    CHECK(arm && arm->location == HitLocation::Arm);
}

TEST(ray_past_the_body_misses)
{
    HitboxShape s;
    BodyPose p = StandingAt({10.f, 0.f, 0.f});
    CHECK(!RayVsBody({0.f, 2.f, 1.f}, {1.f, 0.f, 0.f}, p, s));
    CHECK(!RayVsBody({0.f, 0.f, 3.f}, {1.f, 0.f, 0.f}, p, s));
    // Behind the shooter.
    CHECK(!RayVsBody({0.f, 0.f, 1.f}, {-1.f, 0.f, 0.f}, p, s));
}

TEST(crouching_and_proning_lower_the_target)
{
    HitboxShape s;
    BodyPose crouch = StandingAt({10.f, 0.f, 0.f}, Stance::Crouch);
    // Standing head height flies over a crouched target.
    CHECK(!RayVsBody({0.f, 0.f, 1.82f}, {1.f, 0.f, 0.f}, crouch, s));
    auto low = RayVsBody({0.f, 0.f, 1.1f}, {1.f, 0.f, 0.f}, crouch, s);
    CHECK(low.has_value());

    BodyPose prone = StandingAt({10.f, 0.f, 0.f}, Stance::Prone);
    prone.orientation = 3.14159265f;  // facing the shooter: head first
    CHECK(!RayVsBody({0.f, 0.f, 1.0f}, {1.f, 0.f, 0.f}, prone, s));
    auto lying = RayVsBody({0.f, 0.f, 0.3f}, {1.f, 0.f, 0.f}, prone, s);
    CHECK(lying && lying->location == HitLocation::Head);
}

TEST(pose_history_interpolates_and_forgets)
{
    PoseHistory h;
    h.Record(1000, StandingAt({0.f, 0.f, 0.f}));
    h.Record(1100, StandingAt({10.f, 0.f, 0.f}));
    auto mid = h.At(1050);
    CHECK(mid.has_value());
    CHECK_NEAR(mid->feet.x, 5.f, 1e-4);
    CHECK(h.At(1150).has_value());       // a little ahead is the newest pose
    CHECK(!h.At(1300).has_value());      // too far ahead
    CHECK(!h.At(900).has_value());       // before the first sample
    h.Record(2500, StandingAt({20.f, 0.f, 0.f}));
    CHECK(h.Size() == 1);
    CHECK(!h.At(1050).has_value());
}

TEST(validate_accepts_a_clean_shot)
{
    WeaponProfile w = Rifle();
    PoseHistory target;
    target.Record(900, StandingAt({20.f, 0.f, 0.f}));
    target.Record(1000, StandingAt({20.f, 0.f, 0.f}));
    ShotContext ctx;
    ctx.weapon = &w;
    ctx.shooterPosition = {0.f, 0.f, 1.6f};
    ctx.serverTimeMs = 1080;
    ShotClaim shot;
    shot.shooterTimeMs = 950;
    shot.origin = {0.f, 0.f, 1.6f};
    shot.direction = {1.f, 0.f, -0.01f};
    ShotResult r = ValidateShot(shot, ctx, target);
    CHECK(r.verdict == ShotVerdict::Hit);
    CHECK(!r.headAccepted);
}

TEST(validate_rewinds_a_moving_target)
{
    WeaponProfile w = Rifle();
    PoseHistory target;
    target.Record(1000, StandingAt({20.f, 0.f, 0.f}));
    target.Record(1200, StandingAt({20.f, 8.f, 0.f}));
    ShotContext ctx;
    ctx.weapon = &w;
    ctx.serverTimeMs = 1200;
    ShotClaim shot;
    shot.shooterTimeMs = 1000;  // the shooter saw it where it was 200 ms ago
    shot.origin = {0.f, 0.f, 1.2f};
    shot.direction = {1.f, 0.f, 0.f};
    CHECK(ValidateShot(shot, ctx, target).verdict == ShotVerdict::Hit);
    shot.shooterTimeMs = 1200;
    CHECK(ValidateShot(shot, ctx, target).verdict == ShotVerdict::Miss);
}

TEST(validate_rejects_cheats)
{
    WeaponProfile w = Rifle();
    PoseHistory target;
    target.Record(1000, StandingAt({20.f, 0.f, 0.f}));
    ShotContext ctx;
    ctx.weapon = &w;
    ctx.serverTimeMs = 1000;
    ShotClaim shot;
    shot.shooterTimeMs = 1000;
    shot.origin = {0.f, 0.f, 1.2f};
    shot.direction = {1.f, 0.f, 0.f};

    ShotContext fast = ctx;
    fast.hasLastShot = true;
    fast.lastShotTimeMs = 970;  // 30 ms after the last shot on a 100 ms rifle
    CHECK(ValidateShot(shot, fast, target).verdict == ShotVerdict::RejectedFireRate);

    ShotContext empty = ctx;
    empty.magazineHasRound = false;
    CHECK(ValidateShot(shot, empty, target).verdict == ShotVerdict::RejectedEmpty);

    ShotClaim teleported = shot;
    teleported.origin = {15.f, 0.f, 1.2f};
    CHECK(ValidateShot(teleported, ctx, target).verdict == ShotVerdict::RejectedOrigin);

    ShotContext late = ctx;
    late.serverTimeMs = 2500;
    CHECK(ValidateShot(shot, late, target).verdict == ShotVerdict::RejectedTooOld);

    ShotContext wall = ctx;
    wall.lineOfSight = [](Vec3, Vec3) { return false; };
    CHECK(ValidateShot(shot, wall, target).verdict == ShotVerdict::RejectedNoSight);

    PoseHistory far;
    far.Record(1000, StandingAt({200.f, 0.f, 0.f}));
    CHECK(ValidateShot(shot, ctx, far).verdict == ShotVerdict::RejectedRange);
}

TEST(claimed_headshots_get_a_tolerance_and_no_more)
{
    WeaponProfile w = Rifle();
    PoseHistory target;
    target.Record(1000, StandingAt({20.f, 0.f, 0.f}));
    ShotContext ctx;
    ctx.weapon = &w;
    ctx.serverTimeMs = 1000;
    ShotClaim shot;
    shot.shooterTimeMs = 1000;
    shot.claimsHead = true;
    shot.origin = {0.f, 0.f, 1.55f};  // a neck shot, 0.27 below the head's centre
    shot.direction = {1.f, 0.f, 0.f};
    ShotResult close = ValidateShot(shot, ctx, target);
    CHECK(close.verdict == ShotVerdict::Hit);
    CHECK(close.headAccepted);
    shot.origin = {0.f, 0.f, 1.0f};   // a stomach shot claiming head
    ShotResult far = ValidateShot(shot, ctx, target);
    CHECK(far.verdict == ShotVerdict::Hit);
    CHECK(!far.headAccepted);
}

TEST(rounds_pass_through_the_shooters_own_and_group)
{
    uint64_t const me = 1, friendP = 2;
    CHECK(Fusion::ShotPassesThrough(me, me, false));          // my companion, pet or mount
    CHECK(Fusion::ShotPassesThrough(me, friendP, true));      // a party or raid member, or theirs
    CHECK(!Fusion::ShotPassesThrough(me, friendP, false));    // a stranger blocks
    CHECK(!Fusion::ShotPassesThrough(me, 0, false));          // a wild creature blocks
}
