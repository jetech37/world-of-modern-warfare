/*
 * Azeroth Warfare: tests for the wanderers' lives (WandererLives.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/WandererLives.h"

#include <map>
#include <set>
#include <string>

using namespace Fusion;
using namespace Fusion::Wanderers;

namespace
{
    Node P(uint32_t id, float x, float y, NodeKind kind, std::string name = "")
    {
        Node n;
        n.id = id;
        n.zone = 12;
        n.kind = kind;
        n.pos = Vec3(x, y, 0.f);
        n.name = name.empty() ? "place " + std::to_string(id) : name;
        return n;
    }

    // Lakeshire (home inn), a fishing spot by it, Stormwind's market far off, a landmark, a
    // board, a mine and a second fishing spot.
    struct World
    {
        RoadNet net;
        Haunts h;
        World()
        {
            net = BuildRoads({P(1, 0, 0, NodeKind::Inn, "Lakeshire"), P(2, 150, 0, NodeKind::Fishing), P(3, 3000, 0, NodeKind::Market, "Stormwind"),
                                 P(4, 1500, 600, NodeKind::Landmark), P(5, 200, 200, NodeKind::Board), P(6, 900, -500, NodeKind::Mine),
                                 P(7, 400, -300, NodeKind::Fishing), P(8, 2900, 100, NodeKind::Inn, "Stormwind inn")},
                {});
            h.home = net.IndexOf(1);
            h.work = net.IndexOf(2);
            h.market = net.IndexOf(3);
            h.works = {net.IndexOf(7)};
            h.markets = {net.IndexOf(3)};
            h.inns = {net.IndexOf(1), net.IndexOf(8)};
            h.landmarks = {net.IndexOf(4)};
            h.boards = {net.IndexOf(5)};
            h.settlements = {net.IndexOf(1), net.IndexOf(3), net.IndexOf(5), net.IndexOf(8)};
        }
    };

    DayContext Day(Occupation o, uint8_t hour)
    {
        DayContext c;
        c.occupation = o;
        c.hour = hour;
        c.day = 3;
        // A seed whose day 3 is a working day.
        for (uint32_t s = 1; s < 1000; ++s)
            if (!IsDayOff(s, 3))
            {
                c.seed = s;
                break;
            }
        c.traits = Traits{};
        c.traits.curious = 0;       // no detours unless a test asks
        c.traits.sociable = 40;
        return c;
    }

    Talk TalkOf(Occupation o)
    {
        Talk t;
        t.name = "Bren";
        t.player = "Jeton";
        t.home = "Lakeshire";
        t.work = "Lake Everstill";
        t.market = "Stormwind";
        t.here = "Lakeshire";
        t.heading = "Stormwind";
        t.occupation = o;
        t.doing = Activity::Work;
        return t;
    }
}

TEST(life_occupations_have_workplaces_and_wares)
{
    for (uint8_t i = 1; i < uint8_t(Occupation::Count); ++i)
    {
        OccupationDef const& d = OccupationInfo(Occupation(i));
        CHECK(d.id == Occupation(i));
        CHECK(std::string(d.name).size() > 2);
        CHECK(d.workMaxMs >= d.workMinMs && d.workMinMs > 0);
        if (d.gathers)
        {
            CHECK(!d.fallback.empty());
            CHECK(!d.rares.empty());
            CHECK(IsWorkSpot(d.work) || d.work == NodeKind::Fishing);
        }
    }
    CHECK(Title(Occupation::Fisherman, "Lakeshire") == "Fisherman of Lakeshire");
    CHECK(Title(Occupation::Miner, "") == "Miner");
}

TEST(life_occupation_needs_its_workplace)
{
    // A region with no water has no fishermen; one with nothing has none of the gatherers.
    std::map<Occupation, int> seen;
    for (uint32_t s = 0; s < 3000; ++s)
        ++seen[OccupationFor(Seed(s), [](NodeKind k) { return k != NodeKind::Fishing; })];
    CHECK(seen[Occupation::Fisherman] == 0);
    CHECK(seen[Occupation::Miner] > 100);
    CHECK(seen[Occupation::Courier] > 100);
    CHECK(OccupationFor(7, [](NodeKind) { return false; }) == Occupation::None);
    // With everything, every trade turns up.
    std::set<Occupation> all;
    for (uint32_t s = 0; s < 2000; ++s)
        all.insert(OccupationFor(Seed(s), [](NodeKind) { return true; }));
    CHECK(all.size() == size_t(Occupation::Count) - 1);
}

TEST(life_traits_are_stable_and_lean_by_trade)
{
    Traits const a = TraitsFor(1234, Occupation::Sellsword);
    Traits const b = TraitsFor(1234, Occupation::Sellsword);
    CHECK(a.brave == b.brave && a.greedy == b.greedy && a.sociable == b.sociable);
    int swords = 0, minstrels = 0;
    for (uint32_t s = 0; s < 500; ++s)
    {
        swords += TraitsFor(Seed(s), Occupation::Sellsword).brave;
        minstrels += TraitsFor(Seed(s), Occupation::Minstrel).brave;
    }
    CHECK(swords > minstrels + 500 * 20);
    Traits plain;
    CHECK(DescribeTraits(plain) == "even-tempered");
    Traits marked;
    marked.brave = 95;
    marked.greedy = 5;
    CHECK(DescribeTraits(marked) == "brave, generous");
}

TEST(life_schedule_follows_the_clock)
{
    DayContext c = Day(Occupation::Fisherman, 2);
    CHECK(Scheduled(c, 1) == Activity::Sleep);
    c.hour = 23;
    CHECK(Scheduled(c, 1) == Activity::Sleep);
    // A night owl is still up at eleven.
    c.traits.sociable = 90;
    CHECK(Scheduled(c, 1) != Activity::Sleep);
    c = Day(Occupation::Fisherman, 10);
    CHECK(Scheduled(c, 1) == Activity::Work);
    // Hungry: a meal first; spent: a rest (by day) or bed (at night).
    c.needs.hunger = 90;
    CHECK(Scheduled(c, 1) == Activity::Eat);
    c.needs.hunger = 10;
    c.needs.fatigue = 95;
    CHECK(Scheduled(c, 1) == Activity::Rest);
    c.hour = 21;
    CHECK(Scheduled(c, 1) == Activity::Sleep);
    // A minstrel plays the evening.
    DayContext m = Day(Occupation::Minstrel, 20);
    CHECK(Scheduled(m, 5) == Activity::Perform);
}

TEST(life_gatherer_goes_to_market_after_its_sessions)
{
    DayContext c = Day(Occupation::Fisherman, 10);
    c.sessions = 0;
    c.packItems = 4;
    CHECK(Scheduled(c, 3) == Activity::Work);
    c.sessions = OccupationInfo(Occupation::Fisherman).sessionsPerMarket;
    CHECK(Scheduled(c, 3) == Activity::Sell);
    // An empty pack has nothing to sell.
    c.packItems = 0;
    CHECK(Scheduled(c, 3) == Activity::Work);
    // A courier delivers what it carries; a peddler sells a full pack and stocks an empty one.
    DayContext k = Day(Occupation::Courier, 11);
    k.parcel = true;
    CHECK(Scheduled(k, 3) == Activity::Deliver);
    DayContext p = Day(Occupation::Peddler, 11);
    p.packItems = 0;
    CHECK(Scheduled(p, 3) == Activity::Stock);
    p.packItems = 20;
    CHECK(Scheduled(p, 3) == Activity::Sell);
}

TEST(life_days_off_come_round)
{
    uint32_t off = 0;
    for (uint32_t d = 0; d < 80; ++d)
        off += IsDayOff(99, d) ? 1 : 0;
    CHECK(off >= 80 / 8 && off <= 80 / 5 + 1);
    // On a day off a fisherman does not fish.
    DayContext c = Day(Occupation::Fisherman, 10);
    for (uint32_t d = 0; d < 30; ++d)
        if (IsDayOff(c.seed, d))
        {
            c.day = d;
            break;
        }
    for (uint32_t r = 0; r < 50; ++r)
        CHECK(Scheduled(c, r) != Activity::Work);
}

TEST(life_plans_go_to_the_right_places)
{
    World w;
    int const lakeshire = w.net.IndexOf(1);
    DayContext c = Day(Occupation::Fisherman, 10);
    Plan p = PlanNext(c, w.h, w.net, lakeshire, {}, 4);
    CHECK(p.activity == Activity::Work);
    CHECK(w.net.nodes[size_t(p.node)].kind == NodeKind::Fishing);
    c.sessions = 5;
    c.packItems = 10;
    p = PlanNext(c, w.h, w.net, w.net.IndexOf(2), {}, 4);
    CHECK(p.activity == Activity::Sell);
    CHECK(p.node == w.net.IndexOf(3));
    // At night, far from home in Stormwind: the inn there, not the long walk.
    c.hour = 2;
    p = PlanNext(c, w.h, w.net, w.net.IndexOf(3), {}, 4);
    CHECK(p.activity == Activity::Sleep);
    CHECK(p.node == w.net.IndexOf(8));
    // At night near home: home.
    p = PlanNext(c, w.h, w.net, w.net.IndexOf(2), {}, 4);
    CHECK(p.node == lakeshire);
    // A sellsword's work is the board.
    DayContext s = Day(Occupation::Sellsword, 10);
    bool board = false;
    for (uint32_t r = 0; r < 20; ++r)
        board = board || PlanNext(s, w.h, w.net, lakeshire, {}, r).node == w.net.IndexOf(5);
    CHECK(board);
    // Every plan lands somewhere.
    for (uint8_t o = 0; o < uint8_t(Occupation::Count); ++o)
        for (uint8_t h = 0; h < 24; h += 3)
        {
            DayContext x = Day(Occupation(o), h);
            Plan const q = PlanNext(x, w.h, w.net, lakeshire, {}, h * 31u + o);
            CHECK(q.node >= 0 && size_t(q.node) < w.net.nodes.size());
        }
}

TEST(life_curious_take_detours_and_not_twice)
{
    World w;
    DayContext c = Day(Occupation::Fisherman, 10);
    c.traits.curious = 100;
    int detours = 0;
    for (uint32_t r = 0; r < 300; ++r)
    {
        Plan const p = PlanNext(c, w.h, w.net, w.net.IndexOf(1), {}, r);
        if (p.detour)
        {
            ++detours;
            CHECK(p.activity == Activity::Sightsee);
            CHECK(w.net.nodes[size_t(p.node)].kind == NodeKind::Landmark);
        }
    }
    CHECK(detours > 60 && detours < 160);
    c.lastWasDetour = true;
    for (uint32_t r = 0; r < 100; ++r)
        CHECK(!PlanNext(c, w.h, w.net, w.net.IndexOf(1), {}, r).detour);
    c.lastWasDetour = false;
    c.traits.curious = 0;
    for (uint32_t r = 0; r < 100; ++r)
        CHECK(!PlanNext(c, w.h, w.net, w.net.IndexOf(1), {}, r).detour);
}

TEST(life_activity_lengths)
{
    OccupationDef const& f = OccupationInfo(Occupation::Fisherman);
    Traits t;
    for (uint32_t r = 0; r < 50; ++r)
    {
        uint32_t const work = ActivityMs(Activity::Work, f, t, 10, r);
        CHECK(work >= f.workMinMs && work <= f.workMaxMs);
    }
    // Sleep at ten at night lasts until about six (at most nine hours), at five a short nap.
    uint32_t const night = ActivityMs(Activity::Sleep, f, t, 22, 1);
    CHECK(night >= 7u * 3600 * 1000 && night <= 8u * 3600 * 1000);
    CHECK(ActivityMs(Activity::Sleep, f, t, 5, 1) >= 10u * 60 * 1000);
}

TEST(life_nature_decides_fight_or_flight)
{
    Traits coward;
    coward.brave = 10;
    Traits hero;
    hero.brave = 90;
    Traits middling;
    CHECK(FleeBelowPct(hero) == 0.f);
    CHECK(FleeBelowPct(coward) > FleeBelowPct(middling));
    CHECK(FleeBelowPct(middling) > 0.f);
    CHECK(StartsFights(hero, Occupation::Fisherman));
    CHECK(!StartsFights(coward, Occupation::Fisherman));
    CHECK(StartsFights(coward, Occupation::Sellsword));
    CHECK(!AcceptsHire(coward, Occupation::Fisherman, 0));
    CHECK(AcceptsHire(hero, Occupation::Fisherman, 0));
    CHECK(!AcceptsHire(hero, Occupation::Fisherman, -5));
    CHECK(HirePrice(20, middling, Occupation::Courier, 2) < HirePrice(20, middling, Occupation::Courier, 0));

    // In the mind: a coward attacked and hurt runs, once, then fights cornered.
    Mind m;
    Sense s;
    s.nowMs = 1000;
    s.attacker = 77;
    s.healthPct = 50.f;
    s.fleeBelowPct = FleeBelowPct(coward);
    CHECK(Think(m, s) == Act::Flee);
    CHECK(m.state == State::Flee);
    s.nowMs = 2000;
    CHECK(Think(m, s) == Act::None);
    s.nowMs = 1000 + kFleeMs + 10;
    CHECK(Think(m, s) == Act::Engage);
    // The brave in the same spot fights at once.
    Mind b;
    s.nowMs = 1000;
    s.fleeBelowPct = FleeBelowPct(hero);
    CHECK(Think(b, s) == Act::Engage);
    // A peaceable one does not start fights.
    std::vector<Candidate> c(1);
    c[0].guid = 5;
    c[0].distance = 10.f;
    TargetRules r;
    r.startsFights = false;
    CHECK(ChooseTarget(c, r) == -1);
    c[0].attackingMe = true;
    CHECK(ChooseTarget(c, r) == 0);
}

TEST(life_arrival_does_the_planned_thing)
{
    Mind m;
    m.state = State::Travel;
    Sense s;
    s.nowMs = 5000;
    s.arrived = true;
    s.atPlace = true;
    s.roll = 99;            // no bounty
    s.activityMs = 600000;
    CHECK(Think(m, s) == Act::Work);
    CHECK(m.state == State::Rest);
    s.nowMs = 5000 + 600000;
    CHECK(Think(m, s) == Act::Stand);
}

TEST(life_needs_rise_and_fall)
{
    Needs n;
    n.fatigue = 50;
    n.hunger = 50;
    TickNeeds(n, Activity::Work, 60 * 60 * 1000);
    CHECK(n.fatigue > 50 && n.hunger > 50);
    TickNeeds(n, Activity::Sleep, 8 * 60 * 60 * 1000);
    CHECK(n.fatigue == 0);
    TickNeeds(n, Activity::Eat, 10 * 60 * 1000);
    CHECK(n.hunger == 0);
    // Starving and spent sours its mood.
    Needs g;
    g.hunger = 95;
    g.fatigue = 95;
    g.mood = 60;
    TickNeeds(g, Activity::Work, 60 * 60 * 1000);
    CHECK(g.mood < 60);
    Nudge(g, -500);
    CHECK(g.mood == 0);
    // Less than ten minutes changes nothing.
    Needs h = n;
    TickNeeds(h, Activity::Work, 5 * 60 * 1000);
    CHECK(h.fatigue == n.fatigue);
}

TEST(life_pack_holds_what_fits)
{
    std::vector<Stock> pack;
    CHECK(AddStock(pack, 6291, 10) == 0);
    CHECK(AddStock(pack, 6291, 35) == 0);
    CHECK(CountStock(pack, 6291) == 45);
    CHECK(pack.size() == 2);
    for (uint32_t i = 0; i < 20; ++i)
        AddStock(pack, 1000 + i, 1);
    CHECK(pack.size() == kMaxStock);
    CHECK(AddStock(pack, 5000, 3) == 3);
    CHECK(TakeStock(pack, 6291, 50) == 45);
    CHECK(CountStock(pack, 6291) == 0);
    CHECK(pack.size() == kMaxStock - 2);
}

TEST(life_gathers_follow_the_loot_table)
{
    // A vanilla-shaped fishing table: a group of three fish (two explicit chances, one share),
    // and an ungrouped 5% extra.
    std::vector<LootRow> rows{{6291, 50.f, 1, 1, 1}, {6289, 30.f, 1, 1, 1}, {6308, 0.f, 1, 1, 1}, {6529, 5.f, 0, 1, 1}};
    YieldTable t = YieldFor(Occupation::Fisherman, rows);
    t.rares = {};
    std::map<uint32_t, uint32_t> got;
    uint32_t const n = 20000;
    for (uint32_t r = 0; r < n; ++r)
        for (Stock const& s : RollGather(t, r))
            got[s.item] += s.count;
    CHECK_NEAR(double(got[6291]) / n, 0.50, 0.03);
    CHECK_NEAR(double(got[6289]) / n, 0.30, 0.03);
    CHECK_NEAR(double(got[6308]) / n, 0.20, 0.03);
    CHECK_NEAR(double(got[6529]) / n, 0.05, 0.01);
    // The same roll, the same goods.
    CHECK(RollGather(t, 42).size() == RollGather(t, 42).size());
    // No table: the trade's own fallback wares, every gather something.
    YieldTable f = YieldFor(Occupation::Miner, {});
    f.rares = {};
    for (uint32_t r = 0; r < 200; ++r)
    {
        std::vector<Stock> const g = RollGather(f, r);
        CHECK(g.size() == 1 && g[0].count >= 1);
    }
}

TEST(life_rare_finds_are_rare)
{
    YieldTable t = YieldFor(Occupation::Fisherman, {});
    uint32_t rares = 0;
    uint32_t const n = 50000;
    for (uint32_t r = 0; r < n; ++r)
        for (Stock const& s : RollGather(t, r))
            rares += s.rare ? 1 : 0;
    CHECK_NEAR(double(rares) / n, t.rarePermille / 1000.0, 0.004);
    // A day's work (several sessions) turns up one now and then, not every time.
    uint32_t sessionsWithRare = 0;
    for (uint32_t s = 0; s < 1000; ++s)
    {
        bool rare = false;
        for (Stock const& k : WorkYield(t, 15 * 60 * 1000, s * 7919u))
            rare = rare || k.rare;
        sessionsWithRare += rare ? 1 : 0;
    }
    CHECK(sessionsWithRare > 80 && sessionsWithRare < 350);
}

TEST(life_work_yield_scales_with_time)
{
    YieldTable t = YieldFor(Occupation::Herbalist, {});
    uint32_t shortTotal = 0, longTotal = 0;
    for (uint32_t s = 0; s < 200; ++s)
    {
        shortTotal += PackItems(WorkYield(t, 2 * kGatherMs, s));
        longTotal += PackItems(WorkYield(t, 10 * kGatherMs, s));
    }
    CHECK(longTotal > shortTotal * 4);
    CHECK(PackItems(WorkYield(t, 1, 5)) >= 1);
}

TEST(life_prices_are_sane)
{
    Traits generous;
    generous.greedy = 0;
    Traits greedy;
    greedy.greedy = 100;
    Price const fish{1, 4};          // Raw Brilliant Smallfish's
    Price const ore{5, 0};           // no vendor sells it
    Price const nothing{0, 0};
    for (Price p : {fish, ore, nothing, Price{250, 1000}})
        for (Traits t : {generous, greedy})
            for (uint8_t tier = 0; tier < 3; ++tier)
                for (bool rare : {false, true})
                {
                    uint32_t const price = StallPrice(p, t, rare, tier);
                    CHECK(price > p.sell);          // never a profit selling it on
                    CHECK(price >= 1);
                    CHECK(price <= std::max<uint32_t>(p.buy ? p.buy : p.sell * 4, 2) * 2 + 2);
                }
    CHECK(StallPrice({250, 1000}, greedy, false, 0) > StallPrice({250, 1000}, generous, false, 0));
    CHECK(StallPrice({250, 1000}, greedy, true, 0) > StallPrice({250, 1000}, greedy, false, 0));
    CHECK(StallPrice({250, 1000}, greedy, false, 2) < StallPrice({250, 1000}, greedy, false, 0));
}

TEST(life_gather_then_sell_flow)
{
    // A day: three sessions of fishing, then the market. The purse grows by exactly what the
    // vendor paid, the stall keeps a few of each and every rare find.
    YieldTable t = YieldFor(Occupation::Fisherman, {});
    std::vector<Stock> pack;
    for (uint32_t s = 0; s < 3; ++s)
        for (Stock const& k : WorkYield(t, 12 * kGatherMs, 100 + s))
            AddStock(pack, k.item, k.count, k.rare);
    AddStock(pack, 6533, 1, true);
    uint32_t const before = PackItems(pack);
    CHECK(before > 12);
    auto price = [](uint32_t item) { return item == 6533 ? Price{100, 400} : Price{2, 8}; };
    Traits t1;
    t1.greedy = 50;
    Sale const sale = SellAtMarket(pack, price, t1);
    CHECK(sale.items > 0);
    CHECK(sale.copper == sale.items * 2);
    CHECK(PackItems(pack) + sale.items == before);
    CHECK(CountStock(pack, 6533) == 1);
    for (Stock const& s : pack)
        CHECK(s.rare || s.count <= StallKeeps(t1));
    // A peddler buys low within its purse and carries it.
    uint32_t money = 1000;
    std::vector<Stock> wares;
    std::vector<Stock> const bought = StockUp({{159, {1, 5}}, {117, {6, 25}}, {2678, {10, 40}}}, money, t1, wares, 3);
    CHECK(!bought.empty());
    CHECK(money >= 500 && money < 1000);
    uint32_t spent = 0;
    for (Stock const& b : bought)
        spent += b.count * (b.item == 159 ? 5u : b.item == 117 ? 25u : 40u);
    CHECK(spent == 1000 - money);
    CHECK(PackItems(wares) == PackItems(bought));
    // A pauper buys nothing.
    uint32_t poor = 3;
    std::vector<Stock> none;
    CHECK(StockUp({{117, {6, 25}}}, poor, t1, none, 1).empty() && poor == 3);
}

TEST(life_ties_and_meetings)
{
    std::vector<Tie> ties = FirstTies(3, {1, 2, 3, 4, 5, 6}, 77);
    CHECK(!ties.empty() && ties.size() <= 3);
    for (Tie const& t : ties)
        CHECK(t.other != 3 && (t.affinity >= kFriendAt || t.affinity <= kRivalAt));
    CHECK(FirstTies(3, {3}, 77).empty());
    // Alike natures warm with each meeting.
    Traits a, b;
    std::vector<Tie> mine;
    for (uint32_t r = 0; r < 20; ++r)
        Meet(mine, 9, a, b, r);
    CHECK(TieWith(mine, 9) && TieWith(mine, 9)->affinity >= kFriendAt);
    CHECK(TieWith(mine, 9)->met == 20);
    CHECK(BestFriend(mine) == 9);
    // A lawful one and a roguish one sour.
    Traits law, rogue;
    law.lawful = 95;
    rogue.lawful = 5;
    law.chatty = 0;
    rogue.chatty = 100;
    std::vector<Tie> sour;
    for (uint32_t r = 0; r < 20; ++r)
        Meet(sour, 4, law, rogue, r);
    CHECK(TieWith(sour, 4)->affinity <= kRivalAt);
    CHECK(WorstRival(sour) == 4);
    CHECK(std::string(TieName(-90)) == "sworn rival");
    // No more than kMaxTies; the newest stays.
    std::vector<Tie> many;
    for (uint32_t o = 1; o <= 10; ++o)
        Meet(many, o, a, b, o);
    CHECK(many.size() == kMaxTies);
    CHECK(TieWith(many, 10) != nullptr);
}

TEST(life_chat_and_budget)
{
    Traits sociable;
    sociable.sociable = 100;
    sociable.chatty = 100;
    Traits loner;
    loner.sociable = 0;
    loner.chatty = 0;
    uint32_t warm = 0, cold = 0;
    for (uint32_t r = 0; r < 1000; ++r)
    {
        warm += StopsToChat(sociable, sociable, 60, r) ? 1 : 0;
        cold += StopsToChat(loner, loner, 0, r) ? 1 : 0;
    }
    CHECK(warm > 700 && cold < 150);
    ChatBudget b;
    uint32_t said = 0;
    for (uint32_t i = 0; i < 100; ++i)
        said += MaySpeak(b, 1000 + i * 100, 10) ? 1 : 0;
    CHECK(said == 10);
    CHECK(MaySpeak(b, 1000 + 61000, 10));
}

TEST(life_dialogue_has_no_broken_templates)
{
    // Every line kind, every trade, every news kind, many seeds, with full and with empty facts.
    for (int full = 0; full < 2; ++full)
        for (uint8_t o = 0; o < uint8_t(Occupation::Count); ++o)
            for (uint8_t k = 0; k < uint8_t(NewsKind::Count); ++k)
                for (uint8_t a = 0; a < uint8_t(Activity::Count); ++a)
                    for (uint32_t seed = 0; seed < 12; ++seed)
                    {
                        Talk t = full ? TalkOf(Occupation(o)) : Talk();
                        t.occupation = Occupation(o);
                        t.news.kind = NewsKind(k);
                        t.doing = Activity(a);
                        if (full)
                        {
                            t.news.what = "a Mightfish";
                            t.news.where = "Lakeshire";
                            t.friendName = seed % 2 ? "Dalia" : "";
                            t.rivalName = seed % 3 ? "Gormok" : "";
                            t.weather = seed % 4 ? "" : "rain";
                        }
                        t.hour = uint8_t(seed * 2);
                        t.needs.mood = uint8_t(seed * 8);
                        t.traits.chatty = uint8_t(seed * 9);
                        t.bondTier = uint8_t(seed % 3);
                        CHECK(LineIsClean(ChatLine(t, seed)));
                        CHECK(LineIsClean(ReplyLine(t, seed)));
                        CHECK(LineIsClean(HeadingLine(t, seed)));
                        CHECK(LineIsClean(ActivityLine(t, seed)));
                        CHECK(LineIsClean(FleeLine(t, seed)));
                        CHECK(LineIsClean(ThanksLine(t, seed)));
                        CHECK(LineIsClean(HireLine(t, seed % 2 == 0, seed)));
                        CHECK(LineIsClean(EventLine(NewsKind::Aircraft, t, seed)));
                        CHECK(LineIsClean(EventLine(NewsKind::Building, t, seed)));
                        std::string const news = NewsLine(t, seed);
                        CHECK(k == 0 ? news.empty() : LineIsClean(news));
                        if (a == 0 && k == 0)
                            for (uint8_t race = 1; race <= 8; ++race)
                            {
                                std::string const back = Backstory(Occupation(o), race, full ? "Lakeshire" : "", t.traits, seed);
                                CHECK(LineIsClean(back));
                                CHECK(LineIsClean(AboutLine(t, back, seed)));
                            }
                    }
    for (uint32_t seed = 0; seed < 20; ++seed)
    {
        CHECK(LineIsClean(RumourLine("Mother Fang", "the Jasperlode Mine", false, seed)));
        CHECK(LineIsClean(RumourLine("Defias Trapper", "Sentinel Hill", true, seed)));
        CHECK(LineIsClean(RumourLine("", "", true, seed)));
        CHECK(LineIsClean(NoRumourLine(seed)));
    }
    // The news line says what happened, where.
    Talk t = TalkOf(Occupation::Fisherman);
    t.news = {NewsKind::Catch, "a Mightfish", "Lakeshire", 1};
    std::string const line = NewsLine(t, 3);
    CHECK(line.find("Mightfish") != std::string::npos && line.find("Lakeshire") != std::string::npos);
    CHECK(!LineIsClean("Hello {player}"));
    CHECK(!LineIsClean(""));
}

TEST(life_news_keeps_the_latest)
{
    std::vector<News> news;
    AddNews(news, {NewsKind::Catch, "a", "x", 1});
    AddNews(news, {NewsKind::Sold, "b", "x", 1});
    AddNews(news, {NewsKind::Catch, "c", "x", 2});
    CHECK(news.size() == 2);
    CHECK(news[0].what == "c");
    for (uint8_t k = 1; k < uint8_t(NewsKind::Count); ++k)
        AddNews(news, {NewsKind(k), "n", "x", 3});
    CHECK(news.size() == kMaxNews);
    AddNews(news, {NewsKind::None, "", "", 3});
    CHECK(news.size() == kMaxNews);
}

TEST(life_round_trips_through_its_text)
{
    Life l = NewLife(Seed(12), Occupation::Fisherman, kHuman, "Lakeshire", 20000, 60);
    l.home = 31;
    l.work = 40017;
    l.market = 20003;
    AddStock(l.pack, 6291, 12);
    AddStock(l.pack, 6533, 1, true);
    l.doing = Activity::Work;
    l.doingNode = 40017;
    l.sessions = 2;
    l.lastWasDetour = true;
    l.parcel = {3, 9, 140, "the smith, of Goldshire"};
    AddNews(l.news, {NewsKind::Catch, "Aquadynamic Fish Attractor", "Lakeshire", 20001});
    AddNews(l.news, {NewsKind::Friend, "Dalia", "Stormwind", 20001});
    l.ties = {{4, 55, 3}, {9, -40, 1}};
    l.generation = 2;
    l.earned = 4321;
    std::string const text = EncodeLife(l);
    Life back;
    CHECK(DecodeLife(text, back));
    CHECK(back.occupation == Occupation::Fisherman);
    CHECK(back.traits.brave == l.traits.brave && back.traits.sociable == l.traits.sociable);
    CHECK(back.needs.mood == l.needs.mood);
    CHECK(back.home == 31 && back.work == 40017 && back.market == 20003);
    CHECK(back.homeName == "Lakeshire");
    CHECK(back.pack.size() == 2 && CountStock(back.pack, 6291) == 12 && back.pack[1].rare);
    CHECK(back.doing == Activity::Work && back.doingNode == 40017 && back.sessions == 2 && back.lastWasDetour);
    CHECK(back.parcel.to == 9 && back.parcel.pay == 140 && back.parcel.sender == "the smith of Goldshire");
    CHECK(back.news.size() == 2 && back.news[0].kind == NewsKind::Friend && back.news[1].what == "Aquadynamic Fish Attractor");
    CHECK(back.ties.size() == 2 && back.ties[1].affinity == -40 && back.ties[0].met == 3);
    CHECK(back.born == 20000 && back.retireDay == l.retireDay && back.generation == 2 && back.earned == 4321);
    CHECK(back.backstory == l.backstory);
    CHECK(EncodeLife(back) == text);
    // Broken text leaves it alone.
    Life keep;
    keep.earned = 7;
    CHECK(!DecodeLife("L1|99|", keep));
    CHECK(!DecodeLife("", keep));
    CHECK(!DecodeLife(text.substr(0, text.size() / 2), keep));
    std::string bad = text;
    bad.replace(3, 1, "42");
    CHECK(!DecodeLife(bad, keep));
    CHECK(keep.earned == 7);
}

TEST(life_retirement_comes_in_its_span)
{
    uint32_t lo = 0xFFFFFFFF, hi = 0;
    for (uint32_t s = 0; s < 300; ++s)
    {
        Life const l = NewLife(Seed(s), Occupation::Miner, kDwarf, "Thelsamar", 1000, 60);
        CHECK(!Retires(l, 1000));
        lo = std::min(lo, l.retireDay - 1000);
        hi = std::max(hi, l.retireDay - 1000);
        CHECK(Retires(l, l.retireDay));
    }
    CHECK(lo >= 40 && hi <= 80 && hi > lo);
    CHECK(!Retires(NewLife(1, Occupation::Miner, kDwarf, "", 1000, 0), 999999));
}

TEST(life_clock)
{
    CHECK(DayOf(86400 * 3 + 5) == 3);
    CHECK(HourOf(86400 * 3 + 3600 * 14 + 59) == 14);
    CHECK(std::string(TimeOfDay(3)) == "night");
    CHECK(std::string(TimeOfDay(9)) == "morning");
    CHECK(std::string(TimeOfDay(15)) == "afternoon");
    CHECK(std::string(TimeOfDay(20)) == "evening");
}

TEST(life_budget_model_holds)
{
    // Phase 2 adds no creatures: the cap and the round robin are Phase 1's, so a full roster of
    // 99 with lives still costs at most UnseenPerTick paper turns a tick.
    size_t cursor = 0;
    std::set<size_t> walked;
    for (int tick = 0; tick < 4; ++tick)
    {
        std::vector<size_t> const turn = TakeTurns(kMaxRoster, 25, cursor);
        CHECK(turn.size() == 25);
        walked.insert(turn.begin(), turn.end());
    }
    CHECK(walked.size() == kMaxRoster);
    CHECK(SpawnCap(24, 8, 1) == 8 && SpawnCap(24, 8, 10) == 24);
    // A life's text stays well inside its column (TEXT), packed full.
    Life l = NewLife(1, Occupation::Peddler, kGnome, "Ironforge", 1, 60);
    for (uint32_t i = 0; i < kMaxStock; ++i)
        AddStock(l.pack, 100000 + i, kMaxStack);
    for (uint8_t k = 1; k <= kMaxNews; ++k)
        AddNews(l.news, {NewsKind(k), std::string(60, 'x'), std::string(40, 'y'), 99999});
    for (uint32_t i = 0; i < kMaxTies; ++i)
        l.ties.push_back({i + 1, -100, 65535});
    CHECK(EncodeLife(l).size() < 4000);
}
