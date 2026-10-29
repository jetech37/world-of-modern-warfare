/*
 * Azeroth Warfare: tests for the wanderers (Wanderers.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Wanderers.h"
#include "fusion/Weapon.h"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace Fusion;
using namespace Fusion::Wanderers;

namespace
{
    Node N(uint32_t id, float x, float y, NodeKind kind = NodeKind::Town, uint32_t zone = 12)
    {
        Node n;
        n.id = id;
        n.zone = zone;
        n.kind = kind;
        n.pos = Vec3(x, y, 0.f);
        n.name = "node " + std::to_string(id);
        return n;
    }

    // A line of towns 1000 yards apart (too far for neighbour roads), and one off to the side.
    RoadNet Line()
    {
        return BuildRoads({N(1, 0, 0), N(2, 1000, 0), N(3, 2000, 0), N(4, 3000, 0, NodeKind::Inn), N(5, 1000, 900)}, {});
    }

    Sense At(uint32_t now)
    {
        Sense s;
        s.nowMs = now;
        s.arrived = true;
        return s;
    }

    Candidate Mob(uint64_t guid, float distance, uint8_t level = 10)
    {
        Candidate c;
        c.guid = guid;
        c.distance = distance;
        c.level = level;
        return c;
    }
}

// ── Identity ────────────────────────────────────────────────────────────────────────────────

TEST(wanderer_race_matches_side)
{
    for (uint32_t seed = 0; seed < 200; ++seed)
    {
        CHECK(SideOfRace(RaceFor(Side::Alliance, seed)) == Side::Alliance);
        CHECK(SideOfRace(RaceFor(Side::Horde, seed)) == Side::Horde);
    }
    CHECK(!SideOfRace(9).has_value());
    // Every race of a side turns up.
    std::set<uint8_t> races;
    for (uint32_t seed = 0; seed < 200; ++seed)
        races.insert(RaceFor(Side::Horde, seed));
    CHECK(races.size() == 4);
}

TEST(wanderer_names_are_stable_and_valid)
{
    std::set<std::string> seen;
    for (uint8_t race : {kHuman, kOrc, kDwarf, kNightElf, kUndead, kTauren, kGnome, kTroll})
        for (uint8_t gender = 0; gender < 2; ++gender)
            for (uint32_t seed = 0; seed < 60; ++seed)
            {
                std::string const name = MakeName(race, gender, seed);
                CHECK(ValidName(name));
                CHECK(name == MakeName(race, gender, seed));
                seen.insert(name);
            }
    // Plenty of different names.
    CHECK(seen.size() > 300);
    CHECK(!ValidName("x"));
    CHECK(!ValidName("lowercase"));
    CHECK(!ValidName("Has Space"));
    CHECK(!ValidName("Waytoolongname"));
    CHECK(ValidName("Bramin"));
}

TEST(wanderer_guns_are_in_the_catalog)
{
    std::ifstream in(FUSION_SEED_WEAPONS);
    std::stringstream text;
    text << in.rdbuf();
    WeaponTable t;
    std::string err;
    CHECK(t.LoadJson(text.str(), err));
    for (uint32_t a = 0; a < uint32_t(Archetype::Count); ++a)
        for (uint8_t level = 1; level <= 60; level += 3)
            for (uint32_t seed = 0; seed < 12; ++seed)
            {
                std::string const gun = GunFor(Archetype(a), level, seed);
                CHECK(t.Find(gun) != nullptr);
            }
    // A sharpshooter carries a sniper from 20, a brawler a shotgun.
    CHECK(t.Find(GunFor(Archetype::Sharpshooter, 45, 3))->weaponClass == WeaponClass::Sniper);
    CHECK(t.Find(GunFor(Archetype::Brawler, 30, 3))->weaponClass == WeaponClass::Shotgun);
    CHECK(HoldRange(Archetype::Sharpshooter) > HoldRange(Archetype::Rifleman));
    CHECK(HoldRange(Archetype::Brawler) < HoldRange(Archetype::Scout));
}

TEST(wanderer_archetype_shares)
{
    uint32_t counts[uint32_t(Archetype::Count)] = {};
    for (uint32_t seed = 0; seed < 2000; ++seed)
        ++counts[uint32_t(ArchetypeFor(Seed(seed)))];
    for (uint32_t c : counts)
        CHECK(c > 150);
    CHECK(counts[uint32_t(Archetype::Rifleman)] > counts[uint32_t(Archetype::Scout)]);
}

TEST(wanderer_levels)
{
    ZoneBand const westfall{40, 10, 20};
    for (uint32_t seed = 0; seed < 100; ++seed)
    {
        uint8_t const l = StartLevel(westfall, seed);
        CHECK(l >= 10 && l <= 20);
    }
    CHECK(LevelCap(westfall) == 22);
    CHECK(LevelCap({0, 55, 60}) == 60);
    // Grey kills give nothing, harder ones more.
    CHECK(KillXp(20, 15) == 0);
    CHECK(KillXp(20, 22) > KillXp(20, 20));
    CHECK(KillXp(20, 19) < KillXp(20, 20));
    uint8_t level = 10;
    uint32_t xp = 0;
    CHECK(!GainXp(level, xp, XpToLevel(10) - 1, 22));
    CHECK(level == 10);
    CHECK(GainXp(level, xp, 1, 22));
    CHECK(level == 11 && xp == 0);
    // At the cap it keeps nothing.
    level = 22;
    xp = 0;
    CHECK(!GainXp(level, xp, 100000, 22));
    CHECK(level == 22 && xp == 0);
    // A big gain stops at the cap.
    level = 20;
    CHECK(GainXp(level, xp, 10000000, 22));
    CHECK(level == 22 && xp == 0);
    CHECK(LootCopper(60, 0) > LootCopper(10, 0));
}

// ── Roads and routes ────────────────────────────────────────────────────────────────────────

TEST(wanderer_roads_connect_every_node)
{
    RoadNet const net = Line();
    // The spanning tree: 5 nodes, 4 roads (none near enough for neighbour roads).
    CHECK(net.LinkCount() == 4);
    for (int a = 0; a < 5; ++a)
        for (int b = 0; b < 5; ++b)
            CHECK(!Route(net, a, b).empty());
    // The side town hangs off the nearest, town 2.
    CHECK(net.Linked(net.IndexOf(5), net.IndexOf(2)));
    CHECK(!net.Linked(net.IndexOf(1), net.IndexOf(3)));
}

TEST(wanderer_route_is_the_shortest)
{
    RoadNet const net = Line();
    std::vector<int> const r = Route(net, net.IndexOf(1), net.IndexOf(4));
    CHECK(r.size() == 4);
    CHECK(r.front() == net.IndexOf(1) && r.back() == net.IndexOf(4));
    CHECK(Route(net, 0, 0).size() == 1);
    CHECK(Route(net, -1, 2).empty());
    // An authored road is a shortcut the tree would not make.
    RoadNet const shortcut = BuildRoads({N(1, 0, 0), N(2, 1000, 0), N(3, 2000, 0), N(4, 2000, 600)}, {{1, 4}, {9, 1}});
    CHECK(shortcut.Linked(shortcut.IndexOf(1), shortcut.IndexOf(4)));
    CHECK(Route(shortcut, shortcut.IndexOf(1), shortcut.IndexOf(4)).size() == 2);
    // Near neighbours get roads of their own.
    RoadNet const near = BuildRoads({N(1, 0, 0), N(2, 200, 0), N(3, 100, 150)}, {});
    CHECK(near.LinkCount() == 3);
    // Duplicate ids keep the first.
    RoadNet const dup = BuildRoads({N(1, 0, 0), N(1, 50, 0)}, {});
    CHECK(dup.nodes.size() == 1);
}

TEST(wanderer_destination_choice)
{
    RoadNet net = BuildRoads({N(1, 0, 0), N(2, 300, 0), N(3, 600, 0, NodeKind::Road), N(4, 900, 0, NodeKind::Board),
                                 N(5, 4000, 0, NodeKind::Town, 40)},
        {});
    std::set<int> picked;
    std::vector<int> recent;
    uint32_t near = 0, far = 0;
    for (uint32_t roll = 0; roll < 600; ++roll)
    {
        int const d = ChooseDestination(net, net.IndexOf(1), recent, 12, roll);
        CHECK(d >= 0);
        CHECK(d != net.IndexOf(1));
        CHECK(net.nodes[size_t(d)].kind != NodeKind::Road);
        picked.insert(d);
        if (d == net.IndexOf(2))
            ++near;
        if (d == net.IndexOf(5))
            ++far;
    }
    CHECK(picked.size() == 3);
    // The near town in its home zone far more often than the far one in another.
    CHECK(near > far * 3);
    // Not back where it just was.
    recent = {net.IndexOf(2), net.IndexOf(4)};
    for (uint32_t roll = 0; roll < 50; ++roll)
        CHECK(ChooseDestination(net, net.IndexOf(1), recent, 12, roll) == net.IndexOf(5));
    // Nothing else: anywhere but here, even where it has been.
    recent = {net.IndexOf(2), net.IndexOf(4), net.IndexOf(5)};
    CHECK(ChooseDestination(net, net.IndexOf(1), recent, 12, 7) >= 0);
    RoadNet const lonely = BuildRoads({N(1, 0, 0)}, {});
    CHECK(ChooseDestination(lonely, 0, {}, 12, 1) == -1);
    std::vector<int> mem;
    for (int i = 0; i < 6; ++i)
        Remember(mem, i);
    Remember(mem, 4);
    CHECK(mem.size() == kRecentPlaces);
    CHECK(mem.back() == 4);
}

TEST(wanderer_steps_split_a_leg)
{
    std::vector<Vec3> const s = Steps(Vec3(0, 0, 0), Vec3(100, 0, 0), 45.f);
    CHECK(s.size() == 3);
    CHECK_NEAR(s.back().x, 100.f, 1e-4);
    for (size_t i = 1; i < s.size(); ++i)
        CHECK((s[i] - s[i - 1]).Length() <= 45.f + 1e-3f);
    CHECK(Steps(Vec3(1, 1, 1), Vec3(1, 1, 1), 45.f).size() == 1);
}

TEST(wanderer_trip_advances_unseen)
{
    RoadNet const net = Line();
    Trip t = PlanTrip(net, net.IndexOf(1), net.IndexOf(3), Vec3(2000, 100, 0));
    CHECK(t.route.size() == 3);
    CHECK(t.Points() == 4);
    CHECK(!t.Done());
    CHECK_NEAR(TripPos(net, t).x, 0.f, 1e-3);
    CHECK(!Advance(net, t, 1500.f));
    CHECK(t.leg == 1);
    CHECK_NEAR(TripPos(net, t).x, 1500.f, 1e-2);
    CHECK(!Advance(net, t, 500.f));
    CHECK(t.leg == 2);
    CHECK(Advance(net, t, 1000.f));
    CHECK(t.Done());
    CHECK_NEAR(TripPos(net, t).y, 100.f, 1e-3);
    // No route, no trip.
    CHECK(PlanTrip(net, -1, 2).Points() == 0);
    CHECK(PlanTrip(net, -1, 2).Done());
}

// ── The state machine ───────────────────────────────────────────────────────────────────────

TEST(wanderer_fights_first)
{
    Mind m;
    m.state = State::Travel;
    Sense s = At(1000);
    s.arrived = false;
    s.bestTarget = 77;
    CHECK(Think(m, s) == Act::Engage);
    CHECK(m.state == State::Fight && m.target == 77);
    CHECK(Think(m, s) == Act::None);
    // Attacked by another while fighting: it turns on its attacker.
    s.attacker = 88;
    CHECK(Think(m, s) == Act::Engage);
    CHECK(m.target == 88);
    // Losing: it calls for help once.
    s.healthPct = 30.f;
    CHECK(Think(m, s) == Act::CallForHelp);
    CHECK(Think(m, s) == Act::None);
    // The fight over, its kill looted.
    s.attacker = 0;
    s.bestTarget = 0;
    s.killed = 88;
    s.healthPct = 90.f;
    CHECK(Think(m, s) == Act::LootCorpse);
    CHECK(m.state == State::Loot && m.corpse == 88);
    s.killed = 0;
    s.nowMs = 1500;
    CHECK(Think(m, s) == Act::None);
    s.atCorpse = true;
    CHECK(Think(m, s) == Act::Kneel);
    s.nowMs = 1500 + kKneelMs;
    CHECK(Think(m, s) == Act::TakeLoot);
    CHECK(m.state == State::Idle);
}

TEST(wanderer_gives_up_an_unreachable_corpse)
{
    Mind m;
    m.state = State::Fight;
    m.target = 5;
    Sense s = At(0);
    s.killed = 5;
    CHECK(Think(m, s) == Act::LootCorpse);
    s.killed = 0;
    s.nowMs = kLootWalkMs + 1;
    CHECK(Think(m, s) == Act::Stand);
    CHECK(m.corpse == 0 && m.state == State::Idle);
}

TEST(wanderer_rests_when_hurt)
{
    Mind m;
    Sense s = At(0);
    s.healthPct = 40.f;
    CHECK(Think(m, s) == Act::Sit);
    CHECK(m.state == State::Rest);
    s.healthPct = 100.f;
    s.nowMs = kRestMs - 1;
    CHECK(Think(m, s) == Act::None);
    s.nowMs = kRestMs;
    CHECK(Think(m, s) == Act::Stand);
    // A monster interrupts a rest.
    m.state = State::Rest;
    m.untilMs = 999999;
    s.attacker = 9;
    CHECK(Think(m, s) == Act::Engage);
}

TEST(wanderer_greets_once_in_a_while)
{
    Mind m;
    m.state = State::Travel;
    Sense s = At(1000);
    s.arrived = false;
    s.playerNear = 4242;
    CHECK(Think(m, s) == Act::Greet);
    CHECK(m.state == State::Greet && m.greet == 4242);
    s.nowMs = 1000 + kGreetMs;
    CHECK(Think(m, s) == Act::Walk);
    CHECK(m.state == State::Travel);
    // Not the same player again soon.
    s.nowMs = 1000 + kGreetMs + 1000;
    CHECK(Think(m, s) == Act::None);
    CHECK(!ShouldGreet(m, 4242, 1000 + kGreetAgainMs - 1));
    CHECK(ShouldGreet(m, 4242, 1000 + kGreetAgainMs));
    CHECK(ShouldGreet(m, 1, 0));
    // It remembers a bounded few.
    for (uint64_t p = 0; p < 40; ++p)
        NoteGreeted(m, 100 + p, 5);
    CHECK(m.greeted.size() == kGreetMemory);
}

TEST(wanderer_travels_and_rests_at_places)
{
    Mind m;
    Sense s = At(0);
    // Idle with nowhere to be: a trip.
    CHECK(Think(m, s) == Act::PlanTrip);
    CHECK(m.state == State::Travel);
    s.arrived = false;
    CHECK(Think(m, s) == Act::None);
    // Arrived at an inn: it sits a while.
    s.arrived = true;
    s.atPlace = true;
    s.placeKind = NodeKind::Inn;
    CHECK(Think(m, s) == Act::Sit);
    CHECK(m.state == State::Rest);
    CHECK(m.untilMs >= kInnRestMinMs && m.untilMs <= kInnRestMaxMs);
    s.nowMs = m.untilMs;
    CHECK(Think(m, s) == Act::Stand);
    CHECK(Think(m, s) == Act::PlanTrip);
    for (uint32_t r = 0; r < 50; ++r)
    {
        uint32_t const town = RestMsAt(NodeKind::Town, r);
        CHECK(town >= kTownRestMinMs && town <= kTownRestMaxMs);
    }
}

TEST(wanderer_picks_up_its_trip_again)
{
    // After a fight, a loot or a rest it is idle mid-trip: it walks on rather than planning anew.
    Mind m;
    m.state = State::Idle;
    Sense s = At(0);
    s.arrived = false;
    CHECK(Think(m, s) == Act::Walk);
    CHECK(m.state == State::Travel);
    CHECK(Think(m, s) == Act::None);
    m.state = State::Fight;
    m.target = 9;
    CHECK(Think(m, s) == Act::Walk);
    CHECK(m.state == State::Travel && m.target == 0);
}

TEST(wanderer_bounty_errand)
{
    Mind m;
    Sense s = At(0);
    s.boardHasBounty = true;
    s.roll = 3;     // under kBountyChancePercent
    CHECK(Think(m, s) == Act::PlanTrip);
    CHECK(m.goal == Goal::TakeBounty);
    // On the way, then at the board.
    s.arrived = false;
    CHECK(Think(m, s) == Act::None);
    s.arrived = true;
    s.atBoard = true;
    CHECK(Think(m, s) == Act::TakeBounty);
    // The game takes one: off to hunt.
    m.goal = Goal::Hunt;
    m.errand.questId = 95001;
    m.errand.entries[0] = 116;
    m.errand.needed = 2;
    m.errand.untilMs = kErrandMs;
    CHECK(m.errand.Wants(116) && !m.errand.Wants(0) && !m.errand.Wants(5));
    s.atBoard = false;
    CHECK(Think(m, s) == Act::PlanTrip);
    CHECK(m.goal == Goal::Hunt);
    m.errand.done = 2;
    CHECK(m.errand.Complete());
    s.nowMs = 100;
    CHECK(Think(m, s) == Act::PlanTrip);
    CHECK(m.goal == Goal::TurnIn);
    s.atBoard = true;
    CHECK(Think(m, s) == Act::TurnIn);
    // Not straight back to a board: the cooldown.
    m.goal = Goal::Roam;
    s.atBoard = false;
    s.nowMs = 200;
    CHECK(Think(m, s) == Act::PlanTrip);
    CHECK(m.goal == Goal::Roam);
}

TEST(wanderer_gives_up_a_stale_bounty)
{
    Mind m;
    m.goal = Goal::Hunt;
    m.errand.questId = 1;
    m.errand.needed = 3;
    m.errand.untilMs = 5000;
    Sense s = At(5000);
    CHECK(Think(m, s) == Act::PlanTrip);
    CHECK(m.goal == Goal::Roam);
    CHECK(m.errand.questId == 0);
}

TEST(wanderer_dies_and_comes_back)
{
    Mind m;
    m.state = State::Fight;
    m.target = 3;
    m.goal = Goal::Hunt;
    m.errand.questId = 9;
    Died(m, 100);
    CHECK(m.state == State::Dead && m.target == 0);
    CHECK(m.untilMs == 100 + kRespawnMs);
    Sense s = At(200);
    s.attacker = 5;
    CHECK(Think(m, s) == Act::None);
    Revived(m);
    CHECK(m.state == State::Idle);
    // It keeps its errand.
    CHECK(m.goal == Goal::Hunt && m.errand.questId == 9);
}

// ── Targets ─────────────────────────────────────────────────────────────────────────────────

TEST(wanderer_target_rules)
{
    TargetRules r;
    r.selfLevel = 10;
    std::vector<Candidate> c{Mob(1, 25), Mob(2, 12), Mob(3, 40)};
    CHECK(ChooseTarget(c, r) == 1);         // the nearest in range
    CHECK(ChooseTarget({}, r) == -1);
    // Never a player, a tagged kill, an elite, one too high, one unseen.
    Candidate p = Mob(10, 5);
    p.player = true;
    Candidate tagged = Mob(11, 5);
    tagged.tappedByOther = true;
    Candidate elite = Mob(12, 5);
    elite.elite = true;
    Candidate high = Mob(13, 5, 13);
    Candidate hidden = Mob(14, 5);
    hidden.inSight = false;
    std::vector<Candidate> none{p, tagged, elite, high, hidden, Mob(15, 31)};
    CHECK(ChooseTarget(none, r) == -1);
    // Two above is fine.
    CHECK(ChooseTarget({Mob(16, 5, 12)}, r) == 0);
    // But it fights back against any of them.
    for (Candidate k : none)
    {
        k.attackingMe = true;
        CHECK(ChooseTarget({Mob(1, 2), k}, r) == 1);
    }
    // The wounded first at the same range; the lowest guid on a tie.
    Candidate hurt = Mob(20, 12);
    hurt.healthPct = 20.f;
    CHECK(ChooseTarget({Mob(19, 12), hurt}, r) == 1);
    CHECK(ChooseTarget({Mob(31, 12), Mob(30, 12)}, r) == 1);
}

TEST(wanderer_hunts_its_bounty_first)
{
    TargetRules r;
    r.selfLevel = 15;
    r.hunting = true;
    Candidate wanted = Mob(5, 40);
    wanted.bountyTarget = true;
    std::vector<Candidate> c{Mob(1, 10), wanted};
    CHECK(ChooseTarget(c, r) == 1);
    // Not hunting: the bounty's range does not count.
    r.hunting = false;
    CHECK(ChooseTarget(c, r) == 0);
    CHECK(ChooseTarget({wanted}, r) == -1);
}

// ── Budget and scaling ──────────────────────────────────────────────────────────────────────

TEST(wanderer_roster_and_cap)
{
    CHECK(RosterFor(3, 4, 0) == 12);
    CHECK(RosterFor(50, 4, 0) == kMaxRoster);
    CHECK(RosterFor(3, 4, kMaxRoster - 5) == 5);
    CHECK(RosterFor(3, 4, kMaxRoster + 5) == 0);
    CHECK(SpawnCap(24, 6, 0) == 6);
    CHECK(SpawnCap(24, 6, 2) == 12);
    CHECK(SpawnCap(24, 6, 100) == 24);
    CHECK(kFirstEntry + kMaxRoster < 93800);   // the reserved creature entries
}

TEST(wanderer_presence_hysteresis_and_cap)
{
    std::vector<Presence> w(6);
    w[0].nearestPlayer = 150.f;                 // comes in
    w[1].nearestPlayer = 250.f;                 // too far to come in
    w[2].nearestPlayer = 250.f;
    w[2].spawned = true;                        // but in already: it stays (inside the despawn radius)
    w[3].nearestPlayer = 350.f;
    w[3].spawned = true;                        // goes
    w[4].nearestPlayer = 900.f;
    w[4].spawned = true;
    w[4].busy = true;                           // fighting: stays wherever it is
    w[5].nearestPlayer = 10.f;
    w[5].dead = true;                           // dead: never
    std::vector<size_t> const in = ChoosePresent(w, 10, 200.f, 300.f);
    CHECK(in == (std::vector<size_t>{0, 2, 4}));
    // Under a cap of 2: the fighter, then the one already in, before a newcomer.
    std::vector<size_t> const capped = ChoosePresent(w, 2, 200.f, 300.f);
    CHECK(capped == (std::vector<size_t>{2, 4}));
    // The cap counts the nearest first among newcomers.
    std::vector<Presence> fresh(3);
    fresh[0].nearestPlayer = 190.f;
    fresh[1].nearestPlayer = 20.f;
    fresh[2].nearestPlayer = 100.f;
    CHECK(ChoosePresent(fresh, 2, 200.f, 300.f) == (std::vector<size_t>{1, 2}));
    CHECK(ChoosePresent(fresh, 0, 200.f, 300.f).empty());
}

TEST(wanderer_turns_round_robin)
{
    size_t cursor = 0;
    CHECK(TakeTurns(5, 2, cursor) == (std::vector<size_t>{0, 1}));
    CHECK(TakeTurns(5, 2, cursor) == (std::vector<size_t>{2, 3}));
    CHECK(TakeTurns(5, 2, cursor) == (std::vector<size_t>{4, 0}));
    CHECK(cursor == 1);
    CHECK(TakeTurns(3, 10, cursor).size() == 3);
    CHECK(TakeTurns(0, 10, cursor).empty());
    CHECK(cursor == 0);
    // Shrunk under the cursor: it wraps.
    cursor = 9;
    CHECK(TakeTurns(4, 1, cursor) == (std::vector<size_t>{1}));
    // Staggered first thoughts.
    std::set<uint32_t> firsts;
    for (uint32_t id = 1; id <= 20; ++id)
    {
        CHECK(FirstThinkMs(id) < kThinkMs);
        firsts.insert(FirstThinkMs(id));
    }
    CHECK(firsts.size() > 10);
}

// ── Lines ───────────────────────────────────────────────────────────────────────────────────

TEST(wanderer_lines)
{
    CHECK(BondTier(0) == 0 && BondTier(3) == 1 && BondTier(10) == 2 && BondTier(-5) == 0);
    for (uint32_t c = 0; c < 20; ++c)
    {
        std::string const friendLine = GreetLine(7, 2, "Jeton", c);
        CHECK(friendLine.find("Jeton") != std::string::npos);
        CHECK(friendLine.find("{name}") == std::string::npos);
        CHECK(GreetLine(7, 0, "Jeton", c).find("{name}") == std::string::npos);
    }
    CHECK(GreetLine(7, 1, "A", 3) == GreetLine(7, 1, "A", 3));
    CHECK(BountyTakenLine(1, "Bounty: Wolf").find("Bounty: Wolf") != std::string::npos);
    CHECK(BountyDoneLine(1, "Bounty: Wolf").find("Bounty: Wolf") != std::string::npos);
    CHECK(!HelpLine(3).empty());
    CHECK(!RestLine(3, NodeKind::Inn).empty() && !RestLine(3, NodeKind::Town).empty());
}

// ── Persistence ─────────────────────────────────────────────────────────────────────────────

TEST(wanderer_state_round_trip)
{
    Record r;
    r.id = 7;
    r.name = "Bramin";
    r.trip.route = {3, 9, 12};
    r.trip.hasEnd = true;
    r.trip.end = Vec3(-9464.5f, 62.3f, 55.9f);
    r.trip.leg = 1;
    r.trip.along = 33.3f;
    r.goal = Goal::Hunt;
    r.errand.questId = 95012;
    r.errand.entries[0] = 116;
    r.errand.entries[2] = 117;
    r.errand.needed = 6;
    r.errand.done = 2;
    r.errand.boardNode = 4;
    r.errand.hasGround = true;
    r.errand.ground = Vec3(-10500.f, 1100.f, 40.f);
    r.errand.title = "Bounty: Defias|Trapper;";
    r.lastNode = 9;
    r.recent = {3, 9};
    r.respawnInMs = 4000;
    std::string const text = EncodeState(r);
    Record back;
    back.id = 7;
    CHECK(DecodeState(text, back));
    CHECK(back.trip.route == r.trip.route);
    CHECK(back.trip.hasEnd);
    CHECK_NEAR(back.trip.end.x, -9464.5f, 0.06);
    CHECK_NEAR(back.trip.end.y, 62.3f, 0.06);
    CHECK(back.trip.leg == 1);
    CHECK_NEAR(back.trip.along, 33.3f, 0.06);
    CHECK(back.goal == Goal::Hunt);
    CHECK(back.errand.questId == 95012);
    CHECK(back.errand.entries[0] == 116 && back.errand.entries[1] == 0 && back.errand.entries[2] == 117);
    CHECK(back.errand.needed == 6 && back.errand.done == 2);
    CHECK(back.errand.boardNode == 4 && back.errand.hasGround);
    CHECK_NEAR(back.errand.ground.y, 1100.f, 0.06);
    CHECK(back.errand.title == "Bounty: DefiasTrapper");
    CHECK(back.lastNode == 9);
    CHECK(back.recent == r.recent);
    CHECK(back.respawnInMs == 4000);
    // Again, the same text.
    Record again;
    CHECK(DecodeState(EncodeState(back), again));
    CHECK(EncodeState(again) == EncodeState(back));
    // An empty one too.
    Record empty;
    Record emptyBack;
    CHECK(DecodeState(EncodeState(empty), emptyBack));
    CHECK(emptyBack.trip.route.empty() && emptyBack.goal == Goal::Roam);
}

TEST(wanderer_state_rejects_bad_text)
{
    Record r;
    r.goal = Goal::TurnIn;
    r.lastNode = 5;
    for (std::string const bad : {std::string(), std::string("v2|"), std::string("v1|1,2"),
             std::string("v1|x|0|0,0,0|0|0|0|0|0,0,0,0|0|0|0|0|0,0,0||0||0"),
             std::string("v1||0|0,0,0|0|0|9|0|0,0,0,0|0|0|0|0|0,0,0||0||0"),
             std::string("v1||0|0,0,0|0|0|0|0|0,0,0|0|0|0|0|0,0,0||0||0"),
             std::string("v1||0|nan,0,0|0|0|0|0|0,0,0,0|0|0|0|0|0,0,0||0||0"),
             std::string("v1||0|0,0,0|0|0|0|0|0,0,0,0|0|0|0|0|0,0,0||0|1,2,3,4|0")})
    {
        CHECK(!DecodeState(bad, r));
        CHECK(r.goal == Goal::TurnIn && r.lastNode == 5);
    }
    // A leg past the end is clamped.
    Record t;
    t.trip.route = {1, 2};
    t.trip.leg = 9;
    Record back;
    CHECK(DecodeState(EncodeState(t), back));
    CHECK(back.trip.leg == 1 && back.trip.Done());
}
