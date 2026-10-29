/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Killstreak.h"
#include "fusion/SkyStreaks.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace Fusion;
using namespace Fusion::Sky;

namespace
{
    float Flat(Vec3 a, Vec3 b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    }

    Sight const kClear = [](Vec3, Vec3) { return true; };

    std::vector<uint32_t> Ladder()
    {
        std::vector<uint32_t> ids;
        for (StreakReward const& r : DefaultStreakLadder())
            ids.push_back(r.id);
        return ids;
    }
}

TEST(each_lore_streak_flies_its_own_craft_for_its_side)
{
    CHECK(CraftOf(StreakId::PredatorMissile) == Craft::Rocket);
    CHECK(CraftOf(StreakId::AttackHelicopter) == Craft::Rider);
    CHECK(CraftOf(StreakId::ChopperGunner) == Craft::Gyrocopter);
    CHECK(CraftOf(StreakId::HarrierStrike) == Craft::Hornet);
    CHECK(CraftOf(StreakId::StealthBomber) == Craft::Bomber);
    CHECK(CraftOf(StreakId::SentryGun) == Craft::Turret);
    CHECK(!CraftOf(StreakId::Uav) && !CraftOf(StreakId::TacticalNuke));
    // A gryphon under a dwarf for the Alliance, a wind rider under an orc for the Horde.
    Look const a = LookOf(Craft::Rider, Team::Alliance);
    Look const h = LookOf(Craft::Rider, Team::Horde);
    CHECK(a.mount == Display::kGryphonMount && a.display == Display::kDwarfRider);
    CHECK(h.mount == Display::kWindRiderMount && h.display == Display::kOrcRider);
    CHECK(LookOf(Craft::Bomber, Team::Alliance).display != LookOf(Craft::Bomber, Team::Horde).display);
    // The rocket and the turret are the same for both.
    CHECK(LookOf(Craft::Rocket, Team::Horde).display == Display::kGoblinRocketCar);
    CHECK(LookOf(Craft::Turret, Team::Horde).display == LookOf(Craft::Turret, Team::Alliance).display);
    for (Craft c : {Craft::Rocket, Craft::Rider, Craft::Gyrocopter, Craft::Hornet, Craft::Bomber, Craft::Turret})
    {
        uint32_t const e = LookOf(c, Team::Alliance).entry;
        CHECK(e >= 93500 && e <= 93505);
        // Each craft its own entry, so a creature names its craft; and each craft its streak.
        CHECK(CraftOfEntry(e) == std::optional<Craft>(c));
        CHECK(CraftOf(StreakOf(c)) == std::optional<Craft>(c));
    }
    CHECK(!CraftOfEntry(93506) && !CraftOfEntry(0));
    // The flyers are drawn larger than the model's own size (a speck at 50 yards), the turret
    // a little under a field cannon's.
    CHECK(LookOf(Craft::Rider, Team::Alliance).scale >= 1.5f);
    CHECK(LookOf(Craft::Gyrocopter, Team::Alliance).scale > LookOf(Craft::Hornet, Team::Alliance).scale);
    CHECK(LookOf(Craft::Turret, Team::Alliance).scale < 1.f);
}

TEST(the_gunship_circles_its_mark_at_its_height_and_pace)
{
    Orbit o;
    o.centre = {100.f, 200.f, 10.f};
    o.radius = kRiderRadius;
    o.height = kRiderHeight;
    o.speed = kRiderSpeed;
    o.startAngle = 0.f;
    Vec3 const p0 = OrbitAt(o, 0);
    CHECK_NEAR(p0.x, 130.f, 1e-3f);
    CHECK_NEAR(p0.y, 200.f, 1e-3f);
    CHECK_NEAR(p0.z, 50.f, 1e-3f);
    for (uint32_t ms = 0; ms <= kRiderMs; ms += 1700)
    {
        Vec3 const p = OrbitAt(o, ms);
        CHECK_NEAR(Flat(p, o.centre), kRiderRadius, 1e-2f);
        CHECK_NEAR(p.z, 50.f, 1e-3f);
    }
    // A second's flying is kRiderSpeed yards of ring (the chord, a hair shorter).
    float const step = (OrbitAt(o, 1000) - OrbitAt(o, 0)).Length();
    CHECK(step > kRiderSpeed * 0.98f && step <= kRiderSpeed + 1e-3f);
    // Counter-clockwise: a quarter turn on, it is at +y, facing -x.
    uint32_t const quarter = uint32_t(kPi / 2.f * kRiderRadius / kRiderSpeed * 1000.f);
    CHECK_NEAR(OrbitAt(o, quarter).y, 230.f, 0.05f);
    CHECK_NEAR(std::cos(OrbitFacing(o, quarter)), -1.f, 1e-3f);
    // The path: both ends in, on the ring.
    std::vector<Vec3> const path = OrbitPath(o, 500, 4000, 500);
    CHECK(path.size() == 9);
    CHECK_NEAR((path.front() - OrbitAt(o, 500)).Length(), 0.f, 1e-3f);
    CHECK_NEAR((path.back() - OrbitAt(o, 4500)).Length(), 0.f, 1e-3f);
    // Two callers' rings start apart.
    CHECK(std::fabs(StartAngle(0.f, 1) - StartAngle(0.f, 2)) > 1.f);
}

TEST(a_gunship_shoots_the_exposed_and_the_fighting_first)
{
    Vec3 const centre{0.f, 0.f, 0.f};
    Vec3 const gun{30.f, 0.f, 40.f};
    std::vector<Target> ts = {
        {1, {25.f, 0.f, 1.f}},                  // near the gun, but in cover
        {2, {-20.f, 0.f, 1.f}},                 // farther, in the open
        {3, {200.f, 0.f, 1.f}},                 // off the patrol
        {4, {20.f, 5.f, 1.f}, false},           // dead
        {5, {22.f, 0.f, 1.f}, true, false},     // not hostile
    };
    Sight const coverOne = [](Vec3, Vec3 to) { return std::fabs(to.x - 25.f) > 0.1f; };
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, coverOne) == std::optional<uint64_t>(2));
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, kClear) == std::optional<uint64_t>(1));
    // One already fighting goes first even when farther.
    ts[1].inCombat = true;
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, kClear) == std::optional<uint64_t>(2));
    // Nobody in the open: no shot.
    Sight const blind = [](Vec3, Vec3) { return false; };
    CHECK(!AirTarget(gun, centre, kPatrolReach, kGunReach, ts, blind));
    // Out of the gun's reach.
    CHECK(!AirTarget({0.f, 0.f, 500.f}, centre, kPatrolReach, kGunReach, ts, kClear));
}

TEST(the_turret_covers_its_arc_and_range_only)
{
    Vec3 const at{0.f, 0.f, 0.f};
    float const facing = 0.f;   // +x
    CHECK(TurretCovers(at, facing, {20.f, 0.f, 1.f}));
    CHECK(TurretCovers(at, facing, {10.f, 10.f * std::tan(65.f * kPi / 180.f), 1.f}));    // 65 degrees off
    CHECK(!TurretCovers(at, facing, {10.f, 10.f * std::tan(75.f * kPi / 180.f), 1.f}));   // 75 off
    CHECK(!TurretCovers(at, facing, {-20.f, 0.f, 1.f}));                                // behind
    CHECK(!TurretCovers(at, facing, {kTurretRange + 1.f, 0.f, 0.f}));                   // too far
    // Facing across the wrap (pi): still the arc either side.
    CHECK(TurretCovers(at, kPi - 0.1f, {-20.f, -2.f, 0.f}));
    std::vector<Target> ts = {
        {1, {30.f, 0.f, 1.f}},
        {2, {10.f, 0.f, 1.f}},
        {3, {-5.f, 0.f, 1.f}},      // behind: never
    };
    CHECK(TurretTarget(at, facing, ts, kClear) == std::optional<uint64_t>(2));
    ts[0].inCombat = true;
    CHECK(TurretTarget(at, facing, ts, kClear) == std::optional<uint64_t>(1));
    Sight const wallNear = [](Vec3, Vec3 to) { return to.x > 20.f; };
    ts[0].inCombat = false;
    CHECK(TurretTarget(at, facing, ts, wallNear) == std::optional<uint64_t>(1));
}

TEST(a_turret_stands_only_on_near_flat_ground_in_sight)
{
    Spot s;
    s.feet = {0.f, 0.f, 10.f};
    s.spot = {4.f, 0.f, 10.2f};
    s.ground = 10.2f;
    for (float& c : s.corners)
        c = 10.3f;
    CHECK(CheckPlacement(s) == Placement::Ok);
    Spot far = s;
    far.spot = {7.f, 0.f, 10.2f};
    CHECK(CheckPlacement(far) == Placement::TooFar);
    Spot air = s;
    air.ground.reset();
    CHECK(CheckPlacement(air) == Placement::NoGround);
    air.ground = 7.f;               // the spot hangs 3 yards over the ground
    CHECK(CheckPlacement(air) == Placement::NoGround);
    Spot steep = s;
    steep.corners[2] = 11.5f;
    CHECK(CheckPlacement(steep) == Placement::TooSteep);
    Spot ledge = s;
    ledge.corner[1] = false;
    CHECK(CheckPlacement(ledge) == Placement::TooSteep);
    Spot cliff = s;
    cliff.spot.z = cliff.feet.z + 4.f;
    cliff.ground = cliff.spot.z;
    for (float& c : cliff.corners)
        c = cliff.spot.z;
    CHECK(CheckPlacement(cliff) == Placement::TooSteep);
    Spot wall = s;
    wall.sight = false;
    CHECK(CheckPlacement(wall) == Placement::NoSight);
    Spot many = s;
    many.standing = kMaxTurrets;
    CHECK(CheckPlacement(many) == Placement::TooMany);
    CHECK(std::string(PlacementText(Placement::TooFar)).find("6 yards") != std::string::npos);
}

TEST(the_bombing_run_lays_nine_bombs_eight_yards_apart_along_the_facing)
{
    Vec3 const mark{100.f, 50.f, 20.f};
    BombRun const r = MakeBombRun(mark, kPi / 2.f);    // flying +y
    CHECK(r.bombs == 9);
    CHECK_NEAR(BombOffset(r, 0), -32.f, 1e-4f);
    CHECK_NEAR(BombOffset(r, 8), 32.f, 1e-4f);
    for (uint32_t i = 1; i < r.bombs; ++i)
    {
        CHECK_NEAR(BombOffset(r, i) - BombOffset(r, i - 1), 8.f, 1e-4f);
        CHECK(ReleaseMs(r, i) > ReleaseMs(r, i - 1));
    }
    CHECK_NEAR(BombPoint(r, 4).x, 100.f, 1e-3f);
    CHECK_NEAR(BombPoint(r, 4).y, 50.f, 1e-3f);
    CHECK_NEAR(BombPoint(r, 0).y, 18.f, 1e-3f);
    CHECK_NEAR(BombPoint(r, 0).x, 100.f, 1e-3f);
    // The craft is over each bomb at its release, at its height.
    for (uint32_t i = 0; i < r.bombs; ++i)
    {
        Vec3 const over = BomberAt(r, ReleaseMs(r, i));
        CHECK(Flat(over, BombPoint(r, i)) < 0.06f);
        CHECK_NEAR(over.z, mark.z + kBomberHeight, 1e-3f);
    }
    // It starts lead yards before the line and ends lead yards past it, at its speed.
    CHECK_NEAR(Flat(RunStart(r), BombPoint(r, 0)), kBomberLead, 0.01f);
    CHECK_NEAR(Flat(RunEnd(r), BombPoint(r, 8)), kBomberLead, 0.1f);
    CHECK_NEAR(float(RunMs(r)), (2.f * kBomberLead + kBombLine) / kBomberSpeed * 1000.f, 1.f);
    // The harrier's pass: three, over twelve yards.
    BombRun const h = MakeBombRun(mark, 0.f, kHornetBombs, kHornetLine, kHornetPassHeight, kHornetPassSpeed, kHornetLead);
    CHECK_NEAR(BombOffset(h, 1) - BombOffset(h, 0), 6.f, 1e-4f);
    CHECK_NEAR(BombOffset(h, 2), 6.f, 1e-4f);
}

TEST(the_rocket_starts_over_the_mark_and_steers_down_only)
{
    Vec3 const mark{0.f, 0.f, 0.f};
    RocketPose p = RocketStart(mark, 0.f);     // the caller faces +x
    CHECK_NEAR(p.pos.z, kRocketHeight, 1e-3f);
    CHECK_NEAR(p.pos.x, -kRocketBack, 1e-3f);  // back toward the caller
    // Its nose is on the mark.
    Vec3 const toMark = (mark - p.pos).Normalized();
    CHECK(RocketForward(p).Dot(toMark) > 0.999f);
    // Flying straight on, it comes down on the mark.
    RocketPose q = p;
    float const secs = (mark - p.pos).Length() / kRocketSpeed;
    for (int i = 0; i < 100; ++i)
        q = SteerRocket(q, 0.f, 0.f, secs / 100.f);
    CHECK((q.pos - mark).Length() < 0.05f);
    // Right turns right (toward -y when facing +x), down noses down; never above the level.
    RocketPose r = SteerRocket(p, 50.f, 0.f, 0.1f);
    CHECK(r.yaw < p.yaw);
    RocketPose up = p;
    for (int i = 0; i < 200; ++i)
        up = SteerRocket(up, 0.f, -200.f, 0.05f);
    CHECK_NEAR(up.pitch, kRocketPitchMax, 1e-5f);
    CHECK(RocketForward(up).z < 0.f);
    RocketPose down = p;
    for (int i = 0; i < 200; ++i)
        down = SteerRocket(down, 0.f, 200.f, 0.05f);
    CHECK_NEAR(down.pitch, kRocketPitchMin, 1e-5f);
    // A flick turns no faster than the cap.
    RocketPose flick = SteerRocket(p, 100000.f, 0.f, 0.1f);
    CHECK_NEAR(std::fabs(flick.yaw - p.yaw), kRocketMaxTurnRate * 0.1f, 1e-4f);
    // The boost.
    RocketPose b = p;
    b.boost = true;
    CHECK_NEAR((SteerRocket(b, 0.f, 0.f, 1.f).pos - b.pos).Length(), kRocketBoost, 1e-3f);
}

TEST(the_server_holds_the_rocket_to_its_speed)
{
    Vec3 const a{0.f, 0.f, 100.f};
    CHECK(RocketStepOk(a, a + Vec3{0.f, 0.f, -5.f}, 100));
    CHECK(RocketStepOk(a, a + Vec3{0.f, 0.f, -kRocketBoost}, 1000));
    CHECK(!RocketStepOk(a, a + Vec3{0.f, 0.f, -200.f}, 1000));
    // Two reports landing together are not a teleport.
    CHECK(RocketStepOk(a, a + Vec3{0.f, 0.f, -10.f}, 0));
    CHECK(!RocketStepOk(a, a + Vec3{0.f, 300.f, 0.f}, 0));
    CHECK(RocketReachOk(a, a + Vec3{0.f, 0.f, -100.f}, 3000));
    CHECK(!RocketReachOk(a, a + Vec3{0.f, 900.f, 0.f}, 3000));
}

TEST(the_rocket_wire_round_trips_and_refuses_junk)
{
    RocketPose p;
    p.pos = {1.5f, -2.f, 300.f};
    p.yaw = 1.f;
    p.pitch = -0.7f;
    std::vector<uint8_t> const b = EncodeRocket(true, 77, 0x1122334455667788ull, p, 22000);
    CHECK(b.size() == 36);
    CHECK(b[0] == kOpRocket && b[1] == 1);
    CHECK(b[2] == 77 && b[6] == 0x88 && b[13] == 0x11);
    CHECK(b[34] == uint8_t(22000 & 0xFF) && b[35] == uint8_t(22000 >> 8));
    CHECK(EncodeRocket(false, 77, 0, p, 0)[1] == 0);

    std::vector<uint8_t> r{kOpRocketReport, 77, 0, 0, 0, uint8_t(RocketReport::Impact)};
    auto putF = [&](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        for (int i = 0; i < 4; ++i)
            r.push_back(uint8_t(v >> (8 * i)));
    };
    putF(10.f);
    putF(-20.f);
    putF(5.5f);
    std::optional<RocketReportMsg> const m = DecodeRocketReport(r);
    CHECK(m && m->ticket == 77 && m->kind == RocketReport::Impact);
    CHECK(m && m->at.x == 10.f && m->at.y == -20.f && m->at.z == 5.5f);
    std::vector<uint8_t> bad = r;
    bad[5] = 9;
    CHECK(!DecodeRocketReport(bad));
    bad = r;
    bad.pop_back();
    CHECK(!DecodeRocketReport(bad));
    bad = r;
    float const nan = std::nanf("");
    uint32_t v;
    std::memcpy(&v, &nan, 4);
    for (int i = 0; i < 4; ++i)
        bad[6 + i] = uint8_t(v >> (8 * i));
    CHECK(!DecodeRocketReport(bad));
    // 36 and up are the world channel's new ops; 37 and 38 are free of the old ones.
    CHECK(kOpRocket == 37 && kOpRocketReport == 38);
}

TEST(the_gm_names_a_streak_any_way)
{
    std::vector<uint32_t> const ladder = Ladder();
    CHECK(StreakByName("predator", ladder) == std::optional<uint32_t>(StreakId::PredatorMissile));
    CHECK(StreakByName("Goblin Rocket", ladder) == std::optional<uint32_t>(StreakId::PredatorMissile));
    CHECK(StreakByName("predator_missile", ladder) == std::optional<uint32_t>(StreakId::PredatorMissile));
    CHECK(StreakByName("5", ladder) == std::optional<uint32_t>(StreakId::PredatorMissile));
    CHECK(StreakByName("TURRET", ladder) == std::optional<uint32_t>(StreakId::SentryGun));
    CHECK(StreakByName("gunship", ladder) == std::optional<uint32_t>(StreakId::AttackHelicopter));
    CHECK(StreakByName("sky rider gunship", ladder) == std::optional<uint32_t>(StreakId::AttackHelicopter));
    CHECK(StreakByName("chopper", ladder) == std::optional<uint32_t>(StreakId::ChopperGunner));
    CHECK(StreakByName("Gyrocopter Gunship", ladder) == std::optional<uint32_t>(StreakId::ChopperGunner));
    CHECK(StreakByName("bomber", ladder) == std::optional<uint32_t>(StreakId::StealthBomber));
    CHECK(StreakByName("Siege Bomber", ladder) == std::optional<uint32_t>(StreakId::StealthBomber));
    CHECK(StreakByName("hornets", ladder) == std::optional<uint32_t>(StreakId::HarrierStrike));
    CHECK(StreakByName("nuke", ladder) == std::optional<uint32_t>(StreakId::TacticalNuke));
    CHECK(StreakByName("uav", ladder) == std::optional<uint32_t>(StreakId::Uav));
    // Off the ladder (the client knows no care package), or nothing at all.
    CHECK(!StreakByName("airdrop", ladder));
    CHECK(!StreakByName("2", ladder));
    CHECK(!StreakByName("99999999", ladder));
    CHECK(!StreakByName("", ladder));
    CHECK(!StreakByName("banana", ladder));
}

TEST(a_granted_reward_is_held_until_called_in)
{
    Streak s;
    s.SetLoadout(DefaultStreakLadder(), false);
    s.Grant({StreakId::StealthBomber, 9, true});
    CHECK(s.Earned().size() == 1 && s.Tenths() == 0);
    CHECK(s.Use(StreakId::StealthBomber));
    CHECK(!s.Use(StreakId::StealthBomber));
}

TEST(a_craft_stands_up_to_fire_by_its_owners_health_and_the_rocket_cannot_be_shot)
{
    // A level 60 player has 250 on the CoD scale.
    CHECK(CraftMaxHealth(Craft::Rider, 250.f) == 2000);
    CHECK(CraftMaxHealth(Craft::Gyrocopter, 250.f) == 3000);
    CHECK(CraftMaxHealth(Craft::Hornet, 250.f) == 1500);
    CHECK(CraftMaxHealth(Craft::Bomber, 250.f) == 2500);
    CHECK(CraftMaxHealth(Craft::Turret, 250.f) == 1250);
    // The heavy gunship is the toughest, the turret the lightest of them.
    CHECK(CraftToughness(Craft::Gyrocopter) > CraftToughness(Craft::Rider));
    CHECK(CraftToughness(Craft::Rider) > CraftToughness(Craft::Hornet));
    CHECK(CraftToughness(Craft::Turret) < CraftToughness(Craft::Hornet));
    // Lower level, less health: a level 1's 100.
    CHECK(CraftMaxHealth(Craft::Rider, 100.f) == 800);
    CHECK(!CraftShootable(Craft::Rocket) && CraftMaxHealth(Craft::Rocket, 250.f) == 0);
    for (Craft c : {Craft::Rider, Craft::Gyrocopter, Craft::Hornet, Craft::Bomber, Craft::Turret})
        CHECK(CraftShootable(c));
    CHECK(kCraftBlastScale > 1.f);
    // The HUD's share: never 0 while any is left, never over 100.
    CHECK(HealthPercent(2000, 2000) == 100);
    CHECK(HealthPercent(1000, 2000) == 50);
    CHECK(HealthPercent(1, 2000) == 1);
    CHECK(HealthPercent(0, 2000) == 0);
    CHECK(HealthPercent(5, 0) == 0);
    CHECK(HealthPercent(3000, 2000) == 100);
    // The body a shot sees: bigger than a person's, growing with the drawing.
    Hitbox const rider = CraftHitbox(Craft::Rider, LookOf(Craft::Rider, Team::Horde).scale);
    CHECK(rider.radius > 2.f && rider.height > 3.f);
    CHECK(CraftHitbox(Craft::Gyrocopter, 2.f).radius > CraftHitbox(Craft::Gyrocopter, 1.f).radius);
    CHECK(CraftHitbox(Craft::Turret, 0.7f).radius >= 0.7f);
}

TEST(each_craft_is_heard_by_wow_sounds_of_its_own)
{
    for (Team t : {Team::Alliance, Team::Horde})
    {
        // Every flyer has an engine or wings, and a burst when it is shot down.
        for (Craft c : {Craft::Rider, Craft::Gyrocopter, Craft::Hornet, Craft::Bomber})
        {
            CraftSounds const s = SoundsOf(c, t);
            CHECK(s.loop || (s.beat && s.beatMs >= 500));
            CHECK(s.destroyed != 0);
        }
        CHECK(SoundsOf(Craft::Turret, t).destroyed == Sound::kCannonDeath);
    }
    CHECK(SoundsOf(Craft::Rider, Team::Alliance).arrive == Sound::kGryphonAggro);
    CHECK(SoundsOf(Craft::Rider, Team::Horde).arrive == Sound::kWyvernAggro);
    CHECK(SoundsOf(Craft::Gyrocopter, Team::Horde).loop == Sound::kFlyingMachineLoop);
    // The Horde's bomber is a wind rider: wings, not an engine.
    CHECK(SoundsOf(Craft::Bomber, Team::Horde).beat && !SoundsOf(Craft::Bomber, Team::Horde).loop);
    CHECK(SoundsOf(Craft::Bomber, Team::Alliance).loop);
    CHECK(kLoopForget > kLoopHear);
}

TEST(a_gunship_flies_in_from_the_horizon_and_out_again)
{
    Orbit o;
    o.centre = {100.f, 200.f, 10.f};
    o.radius = kRiderRadius;
    o.height = kRiderHeight;
    o.startAngle = kPi / 2.f;
    Vec3 const from = ArriveFrom(o);
    // Out along the ring's start, past the ring, higher than it.
    CHECK_NEAR(Flat(from, o.centre), kArriveFrom, 1e-2f);
    CHECK(from.y > o.centre.y + kArriveFrom - 0.1f);
    CHECK_NEAR(from.z, o.centre.z + o.height + kArriveRise, 1e-3f);
    uint32_t const in = FlightMs(from, OrbitAt(o, 0), kArriveSpeed);
    CHECK(in > 3000 && in < 6000);
    // Away along its heading, climbing.
    Vec3 const at = OrbitAt(o, 5000);
    Vec3 const to = LeaveTo(at, OrbitFacing(o, 5000));
    CHECK_NEAR(Flat(to, at), kLeaveTo, 1e-2f);
    CHECK_NEAR(to.z - at.z, kLeaveRise, 1e-3f);
    CHECK(Flat(to, o.centre) > kLeaveTo - o.radius - 1.f);
    CHECK(FlightMs(at, at, 30.f) == 0);
    CHECK(FlightMs(Vec3{}, Vec3{30.f, 0.f, 0.f}, 30.f) == 1000);
}

TEST(a_craft_turns_on_whoever_is_on_its_owner_first)
{
    Vec3 const centre{0.f, 0.f, 0.f};
    Vec3 const gun{30.f, 0.f, 40.f};
    std::vector<Target> ts(5);
    for (size_t i = 0; i < ts.size(); ++i)
    {
        ts[i].guid = i + 1;
        ts[i].chest = {28.f - float(i) * 9.f, 0.f, 1.f};     // 1 nearest, 5 farthest
    }
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, kClear) == std::optional<uint64_t>(1));
    ts[2].inCombat = true;
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, kClear) == std::optional<uint64_t>(3));
    ts[3].player = true;            // a player over a creature in a fight
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, kClear) == std::optional<uint64_t>(4));
    ts[1].atCraft = true;           // shooting at the craft, over a player who isn't
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, kClear) == std::optional<uint64_t>(2));
    ts[4].threat = true;            // on the owner, over all of them, though farthest
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, kClear) == std::optional<uint64_t>(5));
    // In cover, the next best.
    Sight const coverFive = [](Vec3, Vec3 to) { return to.x > -5.f; };
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, ts, coverFive) == std::optional<uint64_t>(2));

    // The target in the sights keeps it over a near tie, not over a clearly better one.
    std::vector<Target> two(2);
    two[0].guid = 10;
    two[0].chest = {20.f, 0.f, 1.f};
    two[1].guid = 11;
    two[1].chest = {22.f, 0.f, 1.f};    // half a yard nearer the gun
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, two, kClear) == std::optional<uint64_t>(11));
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, two, kClear, 10) == std::optional<uint64_t>(10));
    two[1].chest = {-10.f, 0.f, 1.f};
    two[1].threat = true;
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, two, kClear, 10) == std::optional<uint64_t>(11));

    // A crowd: only the best few are tested for sight, best first.
    std::vector<Target> crowd(30);
    for (size_t i = 0; i < crowd.size(); ++i)
    {
        crowd[i].guid = 100 + i;
        crowd[i].chest = {25.f - float(i), 0.f, 1.f};
    }
    uint32_t tests = 0;
    Sight const counting = [&tests](Vec3, Vec3) { ++tests; return false; };
    CHECK(!AirTarget(gun, centre, kPatrolReach, kGunReach, crowd, counting));
    CHECK(tests == kMaxSightTests);
    tests = 0;
    Sight const third = [&tests](Vec3, Vec3) { return ++tests == 3; };
    CHECK(AirTarget(gun, centre, kPatrolReach, kGunReach, crowd, third) == std::optional<uint64_t>(102));

    // The turret ranks the same way within its arc.
    std::vector<Target> front = {{1, {10.f, 0.f, 1.f}}, {2, {30.f, 5.f, 1.f}}, {3, {-10.f, 0.f, 1.f}}};
    front[1].threat = true;
    front[2].threat = true;     // behind it: never
    CHECK(TurretTarget({0.f, 0.f, 0.f}, 0.f, front, kClear) == std::optional<uint64_t>(2));
}

TEST(a_gun_walks_its_fire_onto_a_new_target)
{
    CHECK_NEAR(WalkInChance(75.f, 0), 30.f, 1e-3f);
    CHECK(WalkInChance(75.f, 1) > WalkInChance(75.f, 0));
    CHECK(WalkInChance(75.f, 2) > WalkInChance(75.f, 1));
    CHECK_NEAR(WalkInChance(75.f, kWalkInRounds), 75.f, 1e-4f);
    CHECK_NEAR(WalkInChance(75.f, 50), 75.f, 1e-4f);
}

TEST(the_turret_turns_to_its_target_and_sweeps_its_arc_when_idle)
{
    Vec3 const at{0.f, 0.f, 0.f};
    CHECK_NEAR(TurretAimYaw(at, {0.f, 10.f, 0.f}), kPi / 2.f, 1e-5f);
    CHECK_NEAR(TurretAimYaw(at, {-10.f, 0.f, 0.f}), kPi, 1e-5f);
    // The sweep: from the facing, out to most of the half arc either way, and back.
    float const facing = 0.5f;
    CHECK_NEAR(TurretIdleYaw(facing, 0), facing, 1e-5f);
    CHECK_NEAR(TurretIdleYaw(facing, kTurretSweepMs / 4), facing + kTurretHalfArc * kTurretSweep, 1e-4f);
    CHECK_NEAR(TurretIdleYaw(facing, 3 * kTurretSweepMs / 4), facing - kTurretHalfArc * kTurretSweep, 1e-4f);
    CHECK_NEAR(TurretIdleYaw(facing, kTurretSweepMs), facing, 1e-4f);
    for (uint32_t ms = 0; ms < kTurretSweepMs; ms += 137)
    {
        float const yaw = TurretIdleYaw(facing, ms);
        CHECK(TurretCovers(at, facing, at + Vec3{std::cos(yaw), std::sin(yaw), 0.f} * 10.f));
    }
    // Only a turn worth sending is sent, across the wrap too.
    CHECK(!TurretShouldTurn(1.f, 1.f + kTurretTurnStep * 0.5f));
    CHECK(TurretShouldTurn(1.f, 1.f + kTurretTurnStep * 1.5f));
    CHECK(!TurretShouldTurn(kPi - 0.01f, -kPi + 0.01f));
    CHECK(TurretShouldTurn(kPi - 0.2f, -kPi + 0.2f));
    // It sets itself up before it fires, and spins up within a breath of seeing someone.
    CHECK(kTurretDeployMs >= 1000 && kTurretSpinUpMs >= 300 && kTurretSpinUpMs <= 600);
}

TEST(a_bombing_run_marks_where_its_bombs_land_before_they_do)
{
    BombRun const r = MakeBombRun({0.f, 0.f, 0.f}, 0.f);
    // Every bomb lands its fall after its release, in order along the line.
    for (uint32_t i = 0; i < r.bombs; ++i)
    {
        CHECK(LandMs(r, i) == ReleaseMs(r, i) + kBombFallMs);
        if (i)
            CHECK(LandMs(r, i) > LandMs(r, i - 1));
    }
    // Over three seconds of warning from the call to the first blast, the hornets' too.
    CHECK(LandMs(r, 0) > 3000);
    BombRun const h = MakeBombRun({0.f, 0.f, 0.f}, 0.f, kHornetBombs, kHornetLine, kHornetPassHeight, kHornetPassSpeed, kHornetLead);
    CHECK(LandMs(h, 0) > 3000);

    std::vector<Vec3> pts;
    for (uint32_t i = 0; i < r.bombs; ++i)
        pts.push_back(BombPoint(r, i));
    std::vector<uint8_t> const b = EncodeStrikeLine(kLineHostile, uint16_t(LandMs(r, 0)), uint16_t(LandMs(r, 8)), pts);
    CHECK(b.size() == 8 + 9 * 12);
    CHECK(b[0] == kOpCraft && b[1] == uint8_t(News::StrikeLine));
    std::optional<NewsMsg> const m = DecodeNews(b);
    CHECK(m && m->kind == News::StrikeLine && m->flags == kLineHostile);
    CHECK(m && m->firstMs == LandMs(r, 0) && m->lastMs == LandMs(r, 8));
    CHECK(m && m->points.size() == 9);
    CHECK(m && std::fabs(m->points[8].x - 32.f) < 1e-4f);
    // Junk: short, a bad count, a NaN.
    std::vector<uint8_t> bad = b;
    bad.pop_back();
    CHECK(!DecodeNews(bad));
    bad = b;
    bad[7] = 200;
    CHECK(!DecodeNews(bad));
    bad = b;
    float const nan = std::nanf("");
    std::memcpy(&bad[8], &nan, 4);
    CHECK(!DecodeNews(bad));
    // No more points than the wire carries.
    std::vector<Vec3> many(40);
    CHECK(DecodeNews(EncodeStrikeLine(0, 0, 0, many))->points.size() == kMaxLinePoints);
}

TEST(the_craft_news_round_trips_and_refuses_junk)
{
    std::vector<uint8_t> const up = EncodeCraftUp(StreakId::AttackHelicopter, 0x1122334455667788ull, 59, 87);
    CHECK(up.size() == 14 && up[0] == kOpCraft && up[1] == uint8_t(News::Up));
    std::optional<NewsMsg> m = DecodeNews(up);
    CHECK(m && m->streak == StreakId::AttackHelicopter && m->guid == 0x1122334455667788ull);
    CHECK(m && m->seconds == 59 && m->health == 87);
    CHECK(DecodeNews(EncodeCraftUp(4, 1, 1, 250))->health == 100);

    m = DecodeNews(EncodeCraftGone(77, GoneWhy::ShotDown));
    CHECK(m && m->kind == News::Gone && m->guid == 77 && m->why == GoneWhy::ShotDown);
    std::vector<uint8_t> gone = EncodeCraftGone(77, GoneWhy::Done);
    gone[10] = 9;
    CHECK(!DecodeNews(gone));

    m = DecodeNews(EncodeCraftKill(StreakId::SentryGun, "Hogger"));
    CHECK(m && m->kind == News::Kill && m->streak == StreakId::SentryGun && m->name == "Hogger");
    m = DecodeNews(EncodeCraftDowned(StreakId::ChopperGunner, true, "Thrall"));
    CHECK(m && m->kind == News::Downed && m->yours && m->name == "Thrall");
    CHECK(!DecodeNews(EncodeCraftDowned(StreakId::ChopperGunner, false, "Jaina"))->yours);
    // A long name is cut to the wire's most; a bad length is refused.
    std::string const longName(200, 'x');
    CHECK(DecodeNews(EncodeCraftKill(8, longName))->name.size() == kNewsNameMax);
    std::vector<uint8_t> kill = EncodeCraftKill(8, "Hogger");
    kill.push_back('!');
    CHECK(!DecodeNews(kill));
    CHECK(!DecodeNews({kOpCraft}));
    CHECK(!DecodeNews({kOpCraft, 9}));
    CHECK(!DecodeNews({kOpRocket, uint8_t(News::Gone)}));
    // 39 follows the rocket's ops; 3099 is no catalog gun.
    CHECK(kOpCraft == kOpRocketReport + 1);
    CHECK(kBombVisual > 3000 + StreakId::TacticalNuke && kBombVisual < 4000);
}
