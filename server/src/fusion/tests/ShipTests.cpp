/*
 * Azeroth Warfare: tests for player ships (Ships.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Ships.h"

#include <cmath>

using namespace Fusion::Ships;

namespace
{
    ClassInfo const& Sloop() { return *Info(Class::Sloop); }
    ClassInfo const& Airship() { return *Info(Class::Airship); }

    // Open sea everywhere: water at 0, the bottom 30 yd down.
    Probe Sea(float, float, float)
    {
        Probe p;
        p.water = true;
        p.waterZ = 0.f;
        p.groundZ = -30.f;
        return p;
    }

    // Flat land at z 50, no water.
    Probe Land(float, float, float)
    {
        Probe p;
        p.groundZ = 50.f;
        return p;
    }

    // Run `seconds` of steps at 50 ms.
    template <class P>
    StepResult Run(ClassInfo const& info, Motion& m, Helm const* h, float seconds, P probe, bool anchored = false)
    {
        StepResult all;
        for (float t = 0.f; t < seconds; t += 0.05f)
        {
            StepResult r = Step(info, m, h, anchored, SpeedCap(info, false, info.maxHull), 0.05f, probe);
            all.flags |= r.flags;
            all.hullLoss += r.hullLoss;
        }
        return all;
    }
}

TEST(ships_two_classes_with_distinct_entries_deeds_and_numbers)
{
    CHECK(Classes().size() == 2);
    CHECK(InfoByEntry(191800) == &Sloop());
    CHECK(InfoByEntry(191801) == &Airship());
    CHECK(InfoByDeed(93800) == &Sloop());
    CHECK(InfoByDeed(93801) == &Airship());
    CHECK(Sloop().climbRate == 0.f && Airship().climbRate > 0.f);
    CHECK(Airship().buildCopper > Sloop().buildCopper);
    // Prohibitively expensive: hundreds of gold before materials.
    CHECK(Sloop().buildCopper + Sloop().deedPrice >= 500 * kGold);
    CHECK(Airship().buildCopper + Airship().deedPrice >= 1500 * kGold);
    CHECK(!Sloop().materials.empty() && !Airship().materials.empty());
}

TEST(ships_the_telegraph_moves_one_notch_per_press_and_holds)
{
    Helm h;
    h.Press(KEY_AHEAD);
    CHECK(h.notch == 1);
    h.Press(KEY_AHEAD);         // still held: no new press
    CHECK(h.notch == 1);
    h.Press(0);
    h.Press(KEY_AHEAD);
    h.Press(0);
    h.Press(KEY_AHEAD);
    h.Press(0);
    h.Press(KEY_AHEAD);         // past Full Ahead: held there
    CHECK(h.notch == kNotchMax);
    for (int i = 0; i < 8; ++i)
    {
        h.Press(KEY_ASTERN);
        h.Press(0);
    }
    CHECK(h.notch == kNotchMin);
    CHECK(std::string(NotchName(-1)) == "Full Astern");
    // Both at once: neither.
    Helm b;
    b.Press(KEY_AHEAD | KEY_ASTERN);
    CHECK(b.notch == 0);
}

TEST(ships_a_sloop_reaches_top_speed_and_keeps_to_the_water)
{
    Motion m;
    m.z = 5.f;      // spawned a little high: the first step sets it on the water
    Helm h;
    for (int i = 0; i < 3; ++i)
    {
        h.Press(KEY_AHEAD);
        h.Press(0);
    }
    Run(Sloop(), m, &h, 20.f, Sea);
    CHECK_NEAR(m.speed, Sloop().topSpeed, 0.01f);
    CHECK_NEAR(m.z, 0.f, 0.001f);
    CHECK(m.x > 150.f);
    CHECK_NEAR(m.y, 0.f, 0.01f);
}

TEST(ships_a_sloop_stops_at_the_shore_and_scrapes_at_speed)
{
    // Water for x < 100, land beyond.
    auto coast = [](float x, float y, float z) { return x < 100.f ? Sea(x, y, z) : Land(x, y, z); };
    Motion m;
    Helm h;
    for (int i = 0; i < 3; ++i)
    {
        h.Press(KEY_AHEAD);
        h.Press(0);
    }
    StepResult r = Run(Sloop(), m, &h, 30.f, coast);
    CHECK(m.x < 100.f);
    CHECK(r.flags & STEP_BLOCKED);
    CHECK(r.flags & STEP_SCRAPED);
    CHECK(r.hullLoss > 0);
    CHECK(r.hullLoss < Sloop().maxHull / 10);
}

TEST(ships_a_shoal_shallower_than_the_draft_is_refused)
{
    Probe p = Sea(0, 0, 0);
    float z = 0.f;
    CHECK(Sailable(Sloop(), p, z));
    p.groundZ = -1.f;
    CHECK(!Sailable(Sloop(), p, z));
}

TEST(ships_turning_wraps_the_heading_and_is_slower_dead_in_the_water)
{
    Motion m;
    Helm h;
    h.Press(KEY_PORT);
    Run(Sloop(), m, &h, 1.f, Sea);
    float const slow = m.o;
    CHECK(slow > 0.f);
    CHECK_NEAR(slow, Sloop().turnRate * 0.5f, 0.02f);
    Helm r;
    r.Press(KEY_STARBOARD);
    Motion n;
    Run(Sloop(), n, &r, 1.f, Sea);
    CHECK(n.o > 6.f && n.o < 6.2832f);
}

TEST(ships_an_unpiloted_ship_coasts_to_a_stop)
{
    Motion m;
    m.speed = 10.f;
    Run(Sloop(), m, nullptr, 10.f, Sea);
    CHECK(m.speed == 0.f);
}

TEST(ships_an_anchored_ship_does_not_move)
{
    Motion m;
    Helm h;
    h.Press(KEY_AHEAD);
    Run(Sloop(), m, &h, 5.f, Sea, true);
    CHECK(m.x == 0.f && m.speed == 0.f);
    CHECK(CanAnchor(1.f) == Refusal::None);
    CHECK(CanAnchor(10.f) == Refusal::TooFast);
}

TEST(ships_an_airship_climbs_and_is_held_under_the_ceilings)
{
    Motion m;
    m.z = 70.f;
    Helm h;
    h.Press(KEY_CLIMB);
    Run(Airship(), m, &h, 5.f, Land);
    CHECK_NEAR(m.z, 70.f + 5.f * Airship().climbRate, 0.5f);
    // A ward's ceiling at 120.
    auto ward = [](float x, float y, float z) {
        Probe p = Land(x, y, z);
        p.ceilingZ = 120.f;
        return p;
    };
    StepResult r = Run(Airship(), m, &h, 30.f, ward);
    CHECK_NEAR(m.z, 120.f, 0.01f);
    CHECK(r.flags & STEP_CEILING);
    // Over open ground, no more than kAirMaxAboveGround over it.
    Run(Airship(), m, &h, 200.f, Land);
    CHECK(m.z <= 50.f + kAirMaxAboveGround + 0.01f);
}

TEST(ships_an_airship_keeps_its_clearance_and_cannot_dive_into_the_ground)
{
    Motion m;
    m.z = 80.f;
    Helm h;
    h.Press(KEY_DIVE);
    Run(Airship(), m, &h, 30.f, Land);
    CHECK_NEAR(m.z, 50.f + kAirClearance, 0.01f);
}

TEST(ships_an_airship_is_stopped_by_a_cliff_it_cannot_clear)
{
    // A cliff at x 100, 200 yd high.
    auto cliff = [](float x, float y, float z) {
        Probe p = Land(x, y, z);
        if (x > 100.f)
            p.groundZ = 250.f;
        return p;
    };
    Motion m;
    m.z = 80.f;
    Helm h;
    for (int i = 0; i < 3; ++i)
    {
        h.Press(KEY_AHEAD);
        h.Press(0);
    }
    StepResult r = Run(Airship(), m, &h, 20.f, cliff);
    CHECK(m.x <= 100.f);
    CHECK(r.flags & STEP_BLOCKED);
}

TEST(ships_unfinished_airspace_and_the_map_edge_are_refused)
{
    auto edge = [](float x, float y, float z) {
        Probe p = Land(x, y, z);
        p.inBounds = x < 50.f;
        return p;
    };
    Motion m;
    m.z = 80.f;
    Helm h;
    for (int i = 0; i < 3; ++i)
    {
        h.Press(KEY_AHEAD);
        h.Press(0);
    }
    Run(Airship(), m, &h, 20.f, edge);
    CHECK(m.x < 50.f);
    Probe p = Land(0, 0, 0);
    p.unfinished = true;
    float z = 0.f;
    uint8_t f = 0;
    CHECK(!Flyable(p, 100.f, z, f));
}

TEST(ships_neglect_and_listing_halve_the_speed)
{
    CHECK(SpeedCap(Sloop(), false, Sloop().maxHull) == Sloop().topSpeed);
    CHECK(SpeedCap(Sloop(), true, Sloop().maxHull) == Sloop().topSpeed * 0.5f);
    CHECK(SpeedCap(Sloop(), false, Sloop().maxHull / 5) == Sloop().topSpeed * 0.5f);
    CHECK(SpeedCap(Sloop(), true, Sloop().maxHull / 5) == Sloop().topSpeed * 0.25f);
}

TEST(ships_upkeep_wear_and_impounding_count_whole_days_overdue)
{
    uint32_t const paid = 1000000;
    CHECK(!Neglected(paid, paid));
    CHECK(Neglected(paid + 1, paid));
    CHECK(DaysOverdue(paid + kDaySec - 1, paid) == 0);
    CHECK(DaysOverdue(paid + 3 * kDaySec, paid) == 3);
    CHECK(NeglectWear(Sloop(), paid, paid, paid + kDaySec - 1) == 0);
    CHECK(NeglectWear(Sloop(), paid, paid, paid + 2 * kDaySec) == uint32_t(2 * 0.04f * Sloop().maxHull));
    // Swept again the same day: nothing more.
    CHECK(NeglectWear(Sloop(), paid, paid + 2 * kDaySec, paid + 2 * kDaySec + 100) == 0);
    CHECK(!Impounded(paid + 13 * kDaySec, paid));
    CHECK(Impounded(paid + 14 * kDaySec, paid));
    CHECK(UpkeepBlockCost(Sloop()) == 35 * kGold);
    CHECK(UpkeepBlockCost(Airship()) == 105 * kGold);
}

TEST(ships_repair_and_salvage_costs)
{
    CHECK(RepairCost(Sloop(), Sloop().maxHull) == 0);
    CHECK(RepairCost(Sloop(), 0) == 100 * Sloop().repairPerPct);
    CHECK(RepairCost(Sloop(), Sloop().maxHull - 1) == Sloop().repairPerPct);   // a part percent
    CHECK(SalvageCost(Sloop()) == 120 * kGold);
    CHECK(SalvageCost(Airship()) == 360 * kGold);
    CHECK(HullPct(Sloop(), Sloop().maxHull) == 100);
    CHECK(HullPct(Sloop(), 1) == 1);
    CHECK(HullPct(Sloop(), 0) == 0);
}

TEST(ships_commissioning_needs_deed_gold_and_every_material)
{
    Purse p;
    p.copper = 10000 * kGold;
    p.hasDeed = true;
    for (Material const& m : Sloop().materials)
        p.items.push_back({m.item, m.count});
    CHECK(CanCommission(Sloop(), p) == Refusal::None);
    Purse noDeed = p;
    noDeed.hasDeed = false;
    CHECK(CanCommission(Sloop(), noDeed) == Refusal::NoDeed);
    Purse poor = p;
    poor.copper = Sloop().buildCopper - 1;
    CHECK(CanCommission(Sloop(), poor) == Refusal::NoGold);
    Purse shy = p;
    shy.items.back().second -= 1;
    CHECK(CanCommission(Sloop(), shy) == Refusal::NoMaterials);
    // Split over stacks still counts.
    Purse split = p;
    split.items.back().second -= 1;
    split.items.push_back({Sloop().materials.back().item, 1});
    CHECK(CanCommission(Sloop(), split) == Refusal::None);
    Purse owns = p;
    owns.ownsClass = true;
    CHECK(CanCommission(Sloop(), owns) == Refusal::AlreadyOwns);
}

TEST(ships_permissions_for_helm_bell_summon_and_repair)
{
    CHECK(CanTakeHelm(true, true, State::Afloat, false) == Refusal::None);
    CHECK(CanTakeHelm(false, true, State::Afloat, false) == Refusal::NotOwner);
    CHECK(CanTakeHelm(true, false, State::Afloat, false) == Refusal::NotAboard);
    CHECK(CanTakeHelm(true, true, State::Afloat, true) == Refusal::Piloted);
    CHECK(CanTakeHelm(true, true, State::Wrecked, false) == Refusal::Wrecked);
    CHECK(CanRing(true, State::Afloat, 5000, 0) == Refusal::None);
    CHECK(CanRing(true, State::Afloat, 5000, 4000) == Refusal::Cooldown);
    CHECK(CanRing(true, State::Afloat, 4000 + kBellCooldownSec, 4000) == Refusal::None);
    CHECK(CanSummon(true, State::Afloat, false, true) == Refusal::Passengers);
    CHECK(CanSummon(true, State::Afloat, false, false) == Refusal::None);
    CHECK(CanRepair(true, State::Afloat, false, 10, 100) == Refusal::NotDocked);
    CHECK(CanRepair(true, State::Afloat, true, 100, 100) == Refusal::Repaired);
    CHECK(CanRepair(true, State::Afloat, true, 10, 100) == Refusal::None);
    for (uint8_t r = 0; r <= uint8_t(Refusal::Repaired); ++r)
        CHECK(r == 0 || *RefusalText(Refusal(r)));
}

TEST(ships_the_wire_round_trips_and_refuses_the_malformed)
{
    Pose p;
    p.guid = 0xF110002E9F0001ull;
    p.cls = 2;
    p.flags = POSE_PILOTED | POSE_MINE;
    p.notch = -1;
    p.hullPct = 73;
    p.x = -1000.5f;
    p.y = 2000.25f;
    p.z = 120.f;
    p.o = 3.f;
    p.vx = 1.f;
    p.vy = -2.f;
    p.vz = 0.5f;
    p.yawRate = -0.3f;
    p.bellSec = 1200;
    p.name = "Thrall's Airship";
    std::vector<uint8_t> b = EncodePose(p);
    CHECK(b.size() == 48 + p.name.size());
    Pose q;
    CHECK(DecodePose(b, q));
    CHECK(q.guid == p.guid && q.cls == 2 && q.flags == p.flags && q.notch == -1 && q.hullPct == 73);
    CHECK(q.x == p.x && q.y == p.y && q.z == p.z && q.o == p.o && q.yawRate == p.yawRate);
    CHECK(q.vx == 1.f && q.vy == -2.f && q.vz == 0.5f && q.bellSec == 1200 && q.name == p.name);
    b.pop_back();
    CHECK(!DecodePose(b, q));

    uint8_t op = 0, keys = 0;
    CHECK(DecodeClient({kOpKeys, KEY_AHEAD | KEY_CLIMB}, op, keys) && op == kOpKeys && keys == (KEY_AHEAD | KEY_CLIMB));
    CHECK(!DecodeClient({kOpKeys, 0x80}, op, keys));
    CHECK(!DecodeClient({kOpKeys}, op, keys));
    CHECK(DecodeClient({kOpTakeHelm}, op, keys) && op == kOpTakeHelm);
    CHECK(!DecodeClient({kOpTakeHelm, 0}, op, keys));
    CHECK(!DecodeClient({99}, op, keys));
    CHECK(!DecodeClient({}, op, keys));
    CHECK(EncodeKeys(KEY_PORT) == std::vector<uint8_t>({kOpKeys, KEY_PORT}));
    CHECK(EncodeHelm(0x0102030405060708ull) == std::vector<uint8_t>({kOpHelm, 8, 7, 6, 5, 4, 3, 2, 1}));
    CHECK(EncodeGone(1).size() == 9 && EncodeGone(1)[0] == kOpGone);
}

TEST(ships_the_channel_is_its_own)
{
    CHECK(kChannel == 14);
}

TEST(ships_the_pilot_must_be_alive_up_aboard_on_foot_and_out_of_a_duel)
{
    HelmBody ok;
    CHECK(HelmBodyRefusal(ok) == Refusal::None);
    HelmBody dead;
    dead.alive = false;
    CHECK(HelmBodyRefusal(dead) == Refusal::Dead);
    HelmBody downed;
    downed.downed = true;
    CHECK(HelmBodyRefusal(downed) == Refusal::Downed);
    HelmBody riding;
    riding.mounted = true;
    CHECK(HelmBodyRefusal(riding) == Refusal::Mounted);
    HelmBody duel;
    duel.dueling = true;
    CHECK(HelmBodyRefusal(duel) == Refusal::Dueling);
    HelmBody ashore;
    ashore.aboard = false;
    CHECK(HelmBodyRefusal(ashore) == Refusal::NotAboard);
    for (Refusal r : {Refusal::Dead, Refusal::Downed, Refusal::Mounted, Refusal::Dueling})
        CHECK(*RefusalText(r) != '\0');
    // No cast or shot from the helm; a triggered effect (a passive's proc) still lands.
    CHECK(!CastsAtHelm(true, false));
    CHECK(CastsAtHelm(true, true));
    CHECK(CastsAtHelm(false, false));
}

// `.ship test airship|sail`: the GM's words pick a class, and the ship lands beside them.
TEST(ships_test_command_names_pick_a_class)
{
    CHECK(InfoByName("sail") == &Sloop());
    CHECK(InfoByName("Sloop") == &Sloop());
    CHECK(InfoByName("1") == &Sloop());
    CHECK(InfoByName("AIRSHIP") == &Airship());
    CHECK(InfoByName("zeppelin") == &Airship());
    CHECK(InfoByName("2") == &Airship());
    CHECK(InfoByName("") == nullptr);
    CHECK(InfoByName("rowboat") == nullptr);
}

TEST(ships_test_ship_lands_to_the_gms_right)
{
    float x = 0.f, y = 0.f;
    BesideSpot(100.f, 200.f, 0.f, x, y);    // facing +x: the right hand is -y
    CHECK(std::fabs(x - 100.f) < 0.01f);
    CHECK(std::fabs(y - (200.f - kTestBesideYards)) < 0.01f);
    BesideSpot(0.f, 0.f, 1.5707963f, x, y); // facing +y: the right hand is +x
    CHECK(std::fabs(x - kTestBesideYards) < 0.01f);
    CHECK(std::fabs(y) < 0.01f);
}
