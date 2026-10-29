/*
 * Azeroth Warfare: tests for player ships' crew, guns, hull, cargo and respawn (ShipCrew.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/ShipCrew.h"

using namespace Fusion;
using namespace Fusion::Ships;

TEST(ships_float_on_water_and_the_ocean_never_lava_or_slime)
{
    CHECK(SailableLiquid(kLiquidWater));
    CHECK(SailableLiquid(kLiquidOcean));
    CHECK(SailableLiquid(kLiquidOcean | 0x10));     // deep water
    CHECK(!SailableLiquid(kLiquidMagma));
    CHECK(!SailableLiquid(kLiquidSlime));
    CHECK(!SailableLiquid(kLiquidWater | kLiquidSlime));
    CHECK(!SailableLiquid(0));
}

TEST(ships_crew_rights_by_standing_and_duty)
{
    Standing owner{true, false, false}, party{false, true, false}, guild{false, false, true}, stranger;
    for (Duty d : {Duty::Helm, Duty::Guns, Duty::Respawn})
        CHECK(May(owner, 0, d));
    CHECK(!May(party, kDefaultRights, Duty::Helm));
    CHECK(May(party, kDefaultRights | RIGHT_PARTY_HELM, Duty::Helm));
    CHECK(!May(guild, RIGHT_PARTY_HELM, Duty::Helm));
    CHECK(May(guild, kDefaultRights, Duty::Guns));
    CHECK(May(party, kDefaultRights, Duty::Respawn));
    CHECK(!May(stranger, kDefaultRights, Duty::Guns));
    CHECK(May(stranger, RIGHT_ANYONE_GUNS, Duty::Guns));
    CHECK(!May(stranger, RIGHT_ALL, Duty::Helm));
    CHECK(!May(stranger, RIGHT_ALL, Duty::Respawn));
    for (uint8_t b = 1; b < 0x80; b <<= 1)
        CHECK(*RightName(b));
}

TEST(ships_each_class_has_gun_mountings_in_the_frame)
{
    CHECK(MountsOf(Class::Sloop).size() == 3);
    CHECK(MountsOf(Class::Airship).size() == 3);
    CHECK(MountsOf(Class::None).empty());
    Mount const& port = MountsOf(Class::Sloop)[0];
    // A ship facing +y (o = pi/2): its port side (+y in its frame) is world -x.
    Vec3 const w = MountWorld(port, 100.f, 200.f, 0.f, 1.5707963f);
    CHECK_NEAR(w.x, 100.f - port.y, 0.01f);
    CHECK_NEAR(w.y, 200.f + port.x, 0.01f);
    CHECK_NEAR(w.z, port.z, 0.001f);
}

TEST(ships_a_ray_hits_the_hull_box_and_misses_beside_it)
{
    HullShape const& h = HullOf(Class::Sloop);
    // From 100 yd off the beam, level with the deck.
    std::optional<float> t = RayVsHull({0.f, 100.f, 5.f}, {0.f, -1.f, 0.f}, 300.f, h, 0.f, 0.f, 0.f, 0.f);
    CHECK(t && std::fabs(*t - (100.f - h.halfBeam)) < 0.01f);
    // Past the bow.
    CHECK(!RayVsHull({h.halfLength + 5.f, 100.f, 5.f}, {0.f, -1.f, 0.f}, 300.f, h, 0.f, 0.f, 0.f, 0.f));
    // Out of reach.
    CHECK(!RayVsHull({0.f, 100.f, 5.f}, {0.f, -1.f, 0.f}, 50.f, h, 0.f, 0.f, 0.f, 0.f));
    // Turned a quarter: now the bow faces the shooter, so the near face is halfLength away.
    t = RayVsHull({0.f, 100.f, 5.f}, {0.f, -1.f, 0.f}, 300.f, h, 0.f, 0.f, 0.f, 1.5707963f);
    CHECK(t && std::fabs(*t - (100.f - h.halfLength)) < 0.05f);
    // A ray fired from inside (a crewman's own deck) hits at once: the glue skips the own ship.
    t = RayVsHull({0.f, 0.f, 5.f}, {1.f, 0.f, 0.f}, 300.f, h, 0.f, 0.f, 0.f, 0.f);
    CHECK(t && *t == 0.f);
}

TEST(ships_a_blast_falls_off_from_the_hull)
{
    HullShape const& h = HullOf(Class::Sloop);
    CHECK(BlastShare({0.f, 0.f, 5.f}, 6.f, h, 0.f, 0.f, 0.f, 0.f) == 1.f);
    CHECK_NEAR(BlastShare({0.f, h.halfBeam + 3.f, 5.f}, 6.f, h, 0.f, 0.f, 0.f, 0.f), 0.5f, 0.001f);
    CHECK(BlastShare({0.f, h.halfBeam + 7.f, 5.f}, 6.f, h, 0.f, 0.f, 0.f, 0.f) == 0.f);
    CHECK(HullDamage(150.f, true) == 600);
    CHECK(HullDamage(50.f, false) == 50);
    CHECK(HullDamage(150.f, true, 0.5f) == 300);
    // A Sloop takes a couple of hundred M240 rounds, or seventeen rockets.
    CHECK(Info(Class::Sloop)->maxHull / HullDamage(50.f, false) == 200);
}

TEST(ships_the_hold_stows_into_part_stacks_then_free_slots)
{
    std::vector<Stack> hold;
    CHECK(Stow(hold, 2, 2589, 15, 20) == 15);       // Linen Cloth, stacks of 20
    CHECK(Stow(hold, 2, 2589, 10, 20) == 10);
    CHECK(hold.size() == 2 && hold[0].count == 20 && hold[1].count == 5);
    // Full: only what fits in the part stack.
    CHECK(Stow(hold, 2, 2589, 30, 20) == 15);
    CHECK(Stow(hold, 2, 2592, 1, 20) == 0);
    Stack s;
    CHECK(Take(hold, 0, s) && s.entry == 2589 && s.count == 20);
    CHECK(hold.size() == 1);
    CHECK(!Take(hold, 5, s));
    // An unstackable item takes a slot of its own.
    CHECK(Stow(hold, 3, 1234, 2, 1) == 2 && hold.size() == 3);
    CHECK(Stow(hold, 3, 0, 5, 20) == 0);
    CHECK(CargoSlots(Class::Sloop) == 16 && CargoSlots(Class::Airship) == 24);
}

TEST(ships_respawn_aboard_needs_crew_an_anchored_ship_calm_and_near)
{
    uint32_t const now = 100000;
    CHECK(CanRespawnAboard(true, State::Afloat, true, now, 0, 0, 100.f) == RespawnRefusal::None);
    CHECK(CanRespawnAboard(false, State::Afloat, true, now, 0, 0, 100.f) == RespawnRefusal::NotCrew);
    CHECK(CanRespawnAboard(true, State::Wrecked, true, now, 0, 0, 100.f) == RespawnRefusal::Wrecked);
    CHECK(CanRespawnAboard(true, State::Afloat, false, now, 0, 0, 100.f) == RespawnRefusal::NotAnchored);
    CHECK(CanRespawnAboard(true, State::Afloat, true, now, now - 10, 0, 100.f) == RespawnRefusal::UnderFire);
    CHECK(CanRespawnAboard(true, State::Afloat, true, now, now - kUnderFireSec, 0, 100.f) == RespawnRefusal::None);
    CHECK(CanRespawnAboard(true, State::Afloat, true, now, 0, now - 60, 100.f) == RespawnRefusal::Cooldown);
    CHECK(CanRespawnAboard(true, State::Afloat, true, now, 0, 0, kRespawnReach + 1.f) == RespawnRefusal::TooFar);
}

TEST(ships_the_crew_wire_round_trips)
{
    std::vector<CargoLine> hold{{2589, 20, "Linen Cloth"}}, bags{{2592, 7, "Wool Cloth"}, {4306, 1, ""}};
    std::vector<uint8_t> b = EncodeCargo(16, hold, bags);
    uint8_t slots = 0;
    std::vector<CargoLine> h2, b2;
    CHECK(DecodeCargo(b, slots, h2, b2));
    CHECK(slots == 16 && h2.size() == 1 && h2[0].name == "Linen Cloth" && h2[0].count == 20);
    CHECK(b2.size() == 2 && b2[0].entry == 2592 && b2[1].name.empty());
    b.push_back(0);
    CHECK(!DecodeCargo(b, slots, h2, b2));

    CrewAsk a;
    CHECK(DecodeCrew({kOpManGun, 2}, a) && a.op == kOpManGun && a.index == 2);
    CHECK(DecodeCrew({kOpLeaveGun}, a) && a.op == kOpLeaveGun);
    CHECK(DecodeCrew({kOpStow, 0x1D, 0x0A, 0, 0, 5, 0, 0, 0}, a) && a.entry == 2589 && a.count == 5);
    CHECK(!DecodeCrew({kOpStow, 0, 0, 0, 0, 5, 0, 0, 0}, a));
    CHECK(DecodeCrew({kOpTake, 3}, a) && a.index == 3);
    CHECK(!DecodeCrew({kOpCargo, 1}, a));
    CHECK(!DecodeCrew({kOpPose}, a));
    CHECK(EncodeGun(1, kNoMount) == std::vector<uint8_t>({kOpGun, 1, 0, 0, 0, 0, 0, 0, 0, kNoMount}));
    CHECK(EncodeMounts(1, {true, false}) == std::vector<uint8_t>({kOpMounts, 1, 0, 0, 0, 0, 0, 0, 0, 2, 1, 0}));
}
