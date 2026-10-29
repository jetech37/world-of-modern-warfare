/*
 * Azeroth Warfare: tests for the Construction window and claiming (Homestead.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Homestead.h"

#include <cstring>
#include <string>

using namespace Fusion;
using namespace Fusion::Homestead;
using Fusion::Plots::BuyResult;
using Fusion::Plots::Buyer;
using Fusion::Plots::Registry;

namespace
{
    constexpr int64_t kNow = 1700000000;

    // One site (id 1, map 1, gate at (1000, 1990)) with two districts: three standard plots of
    // 40 along x from (1000, 2000), ids 1-3; and one large of 64 at (1200, 2000), id 101.
    Plots::Grid TestGrid()
    {
        Plots::Site s;
        s.id = 1;
        s.name = "the test site";
        s.where = "on the test site";
        s.map = 1;
        s.gate = {1000.f, 1990.f, 50.f, 0.f};
        s.stops = {{"the far end", {1300.f, 1990.f, 40.f, 0.f}}};
        s.bounds = {900.f, 1900.f, 1400.f, 2200.f};
        s.floorZ = 0.f;
        s.ceilingZ = 200.f;
        return Plots::Grid({
            {Plots::Size::Standard, 1, 1000.f, 2000.f, 40.f, 3, 1, 1, 250000, 10000, 0, 1},
            {Plots::Size::Large, 1, 1200.f, 2000.f, 64.f, 1, 1, 101, 1000000, 30000, 0, 1},
        }, {s});
    }

    Buyer Rich(uint32_t guid, uint32_t account) { return {guid, account, 100000000}; }

    Service const* Find(std::vector<Service> const& v, Action a, uint32_t arg)
    {
        for (Service const& s : v)
            if (s.action == a && s.arg == arg)
                return &s;
        return nullptr;
    }

    bool Has(std::vector<Service> const& v, Action a)
    {
        for (Service const& s : v)
            if (s.action == a)
                return true;
        return false;
    }
}

TEST(a_player_holds_one_plot_whatever_the_way_in)
{
    Plots::Grid const g = TestGrid();
    Registry r;
    uint32_t charge = 0, id = 0;
    // Claimed by district: the first free one.
    CHECK(ClaimInDistrict(g, r, 0, 1, Rich(70, 7), kNow, charge, id) == BuyResult::Ok);
    CHECK(id == 1 && charge == 250000 && r.OwnedBy(7)->id == 1);
    // A second plot, by any way in, by the same character or an alt of the account: refused,
    // and refused as the limit (never "taken" or "full").
    CHECK(ClaimInDistrict(g, r, 0, 1, Rich(70, 7), kNow, charge, id) == BuyResult::LimitReached && id == 0 && charge == 0);
    CHECK(ClaimInDistrict(g, r, 1, 1, Rich(71, 7), kNow, charge, id) == BuyResult::LimitReached);
    CHECK(ClaimPlot(g, r, 2, Rich(71, 7), kNow, charge) == BuyResult::LimitReached);
    CHECK(ClaimPlot(g, r, 1, Rich(70, 7), kNow, charge) == BuyResult::LimitReached);
    CHECK(Plots::Buy(g, r, 3, Rich(72, 7), kNow, charge) == BuyResult::LimitReached);
    // Game masters too: the limit is the realm's, not a price.
    CHECK(ClaimPlot(g, r, 3, {72, 7, 0xFFFFFFFFu}, kNow, charge) == BuyResult::LimitReached);
    CHECK(r.CountOwnedBy(7) == 1 && r.plots.size() == 1);
    // Another player claims the next one, passing one still being packed; a district run out
    // says so.
    CHECK(FirstFree(g, r, 0) == 2 && FirstFree(g, r, 0, {2}) == 3 && FirstFree(g, r, 0, {2, 3}) == 0);
    CHECK(ClaimInDistrict(g, r, 0, 1, Rich(80, 8), kNow, charge, id, {2, 3}) == BuyResult::Taken);
    CHECK(ClaimInDistrict(g, r, 0, 1, Rich(80, 8), kNow, charge, id) == BuyResult::Ok && id == 2);
    CHECK(ClaimInDistrict(g, r, 0, 1, Rich(90, 9), kNow, charge, id) == BuyResult::Ok && id == 3);
    CHECK(ClaimInDistrict(g, r, 0, 1, Rich(95, 10), kNow, charge, id) == BuyResult::Taken);
    // Another site's district, or none at all.
    CHECK(ClaimInDistrict(g, r, 1, 2, Rich(95, 10), kNow, charge, id) == BuyResult::NoSuchPlot);
    CHECK(ClaimInDistrict(g, r, 7, 0, Rich(95, 10), kNow, charge, id) == BuyResult::NoSuchPlot);
    // Released, the account may claim again.
    CHECK(Plots::Abandon(r, 1, 7));
    CHECK(ClaimPlot(g, r, 101, Rich(71, 7), kNow, charge) == BuyResult::Ok && charge == 1000000);
    CHECK(r.CountOwnedBy(7) == 1 && r.OwnedBy(7)->id == 101);
}

TEST(rows_beyond_one_plot_an_account_are_found_for_packing)
{
    Registry r;
    auto own = [&](uint32_t id, uint32_t account, int64_t at) {
        Plots::Plot p;
        p.id = id;
        p.ownerAccount = account;
        p.ownerGuid = account * 10;
        p.boughtAt = at;
        p.paidUntil = at + Plots::kWeek;
        r.plots[id] = p;
    };
    own(5, 7, kNow + 100);
    own(3, 7, kNow);            // the earliest: kept
    own(9, 7, kNow + 50);
    own(4, 8, kNow);
    own(6, 9, kNow);
    own(2, 9, kNow);            // bought together: the lower id kept
    std::vector<uint32_t> const extra = Excess(r);
    CHECK((extra == std::vector<uint32_t>{5, 6, 9}));
    for (uint32_t id : extra)
        r.plots.erase(id);
    CHECK(r.CountOwnedBy(7) == 1 && r.OwnedBy(7)->id == 3);
    CHECK(r.CountOwnedBy(9) == 1 && r.OwnedBy(9)->id == 2);
    CHECK(Excess(r).empty());
}

TEST(the_coordinator_sets_a_player_down_on_the_edge_by_the_gate)
{
    Plots::Grid const g = TestGrid();
    // Plot 1 is x 1000-1040, y 2000-2040; the gate is south of it at y 1990: its low-y edge.
    Plots::Spot const a = Arrival(g, 1);
    CHECK_NEAR(a.x, 1020.f, 1e-3);
    CHECK_NEAR(a.y, 2000.f + kArrivalInset, 1e-3);
    CHECK_NEAR(a.z, 50.f, 1e-3);
    // Facing into the plot (north, +y).
    CHECK_NEAR(a.o, 3.14159265f / 2.f, 1e-3);
    CHECK(g.PlotAt(1, a.x, a.y) == 1);
    // Inside the plot but in its margin, where no piece may stand.
    std::optional<Plots::Rect> const b = g.Bounds(1);
    CHECK(b->Contains(a.x, a.y) && !b->Inset(Plots::kBuildMargin).Contains(a.x, a.y));
    // No such plot: nowhere.
    Plots::Spot const none = Arrival(g, 77);
    CHECK(none.x == 0.f && none.y == 0.f);
}

TEST(the_steward_offers_a_claim_or_the_plots_upkeep_never_both)
{
    Plots::Grid const g = TestGrid();
    Registry r;
    StewardAsk ask;
    ask.account = 7;
    ask.guid = 70;
    ask.money = 300000;
    ask.site = 1;
    ask.visited = 2;
    std::vector<Service> v = StewardServices(g, r, ask, kNow);
    // A claim a district, the plot last walked on; the large one greyed for the gold.
    Service const* standard = Find(v, Action::Claim, 0);
    Service const* large = Find(v, Action::Claim, 1);
    CHECK(standard && standard->enabled && standard->cost == 250000);
    CHECK(standard->label == "Claim a Homestead (40 yards)");
    CHECK(standard->note.find("1g a week") != std::string::npos && standard->note.find("3 of 3 free") != std::string::npos);
    CHECK(large && !large->enabled && large->note.find("100g") != std::string::npos);
    CHECK(Find(v, Action::ClaimVisited, 2) && Find(v, Action::ClaimVisited, 2)->enabled);
    // Claiming spends gold and takes the one plot a player may hold: asked first.
    CHECK(standard->confirm && Find(v, Action::ClaimVisited, 2)->confirm);
    CHECK(!Has(v, Action::Teleport) && !Has(v, Action::Release) && !Has(v, Action::Pay) && !Has(v, Action::Collect));

    // Holding one: no claim at all; going there, upkeep, release (asked first).
    uint32_t charge = 0, id = 0;
    CHECK(ClaimInDistrict(g, r, 0, 1, Rich(70, 7), kNow, charge, id) == BuyResult::Ok);
    ask.reclaim = 3;
    ask.money = 25000;
    ask.target = 80;
    ask.targetName = "Brann";
    v = StewardServices(g, r, ask, kNow);
    CHECK(!Has(v, Action::Claim) && !Has(v, Action::ClaimVisited));
    CHECK(Has(v, Action::Teleport));
    Service const* release = Find(v, Action::Release, 0);
    CHECK(release && release->confirm);
    Service const* week = Find(v, Action::Pay, 1);
    Service const* four = Find(v, Action::Pay, 4);
    Service const* most = Find(v, Action::Pay, 0);
    CHECK(week && week->enabled && week->cost == 10000);
    CHECK(four && !four->enabled && four->cost == 40000);      // short of gold
    // Bought with a week paid: eleven more may go ahead.
    CHECK(most && most->cost == 11 * 10000 && most->note.find("11 weeks") != std::string::npos);
    Service const* guest = Find(v, Action::Permit, uint32_t(Plots::PermitLevel::Guest));
    CHECK(guest && guest->enabled && guest->label == "Let Brann use your doors");
    Service const* collect = Find(v, Action::Collect, 0);
    CHECK(collect && collect->note == "3 items wait here");
    // A permit given shows as a revoke, with the character's name.
    Plots::Grant(*r.Find(1), 80, Plots::PermitLevel::Builder);
    ask.nameOf = [](uint32_t guid) { return guid == 80 ? std::string("Brann") : std::string("?"); };
    v = StewardServices(g, r, ask, kNow);
    CHECK(Find(v, Action::Revoke, 80) && Find(v, Action::Revoke, 80)->label == "Revoke Brann's permit");
    CHECK(Find(v, Action::Revoke, 80)->confirm);
    // Paying upkeep is no question: one press.
    CHECK(!Find(v, Action::Pay, 1)->confirm);
    CHECK(!Find(v, Action::Permit, uint32_t(Plots::PermitLevel::Builder))->enabled);
}

TEST(each_action_needs_its_npc)
{
    CHECK(AllowedAt(Action::Claim, Npc::Steward) && !AllowedAt(Action::Claim, Npc::None));
    CHECK(AllowedAt(Action::Teleport, Npc::Steward) && !AllowedAt(Action::Teleport, Npc::Courier) && !AllowedAt(Action::Teleport, Npc::None));
    CHECK(AllowedAt(Action::Release, Npc::Steward) && !AllowedAt(Action::Release, Npc::Trainer));
    CHECK(AllowedAt(Action::Travel, Npc::Courier) && !AllowedAt(Action::Travel, Npc::Steward));
    CHECK(AllowedAt(Action::Train, Npc::Trainer) && AllowedAt(Action::Craft, Npc::Trainer) && !AllowedAt(Action::Craft, Npc::None));
    CHECK(!AllowedAt(Action::None, Npc::Steward));
}

TEST(the_courier_goes_back_to_the_stops_and_to_the_players_plot)
{
    Plots::Grid const g = TestGrid();
    Registry r;
    CourierAsk ask;
    ask.account = 7;
    ask.map = 1;
    ask.x = 1002.f;
    ask.y = 1992.f;             // at the gate
    ask.canGoBack = true;
    std::vector<Service> v = CourierServices(g, r, ask);
    CHECK(Find(v, Action::Travel, kTravelBack));
    CHECK(!Find(v, Action::Travel, 0));                 // the gate: it stands there
    CHECK(Find(v, Action::Travel, 1) && Find(v, Action::Travel, 1)->label == "Take me to the far end");
    CHECK(!Find(v, Action::Travel, kTravelMine));
    uint32_t charge = 0;
    CHECK(Plots::Buy(g, r, 101, Rich(70, 7), kNow, charge) == BuyResult::Ok);
    v = CourierServices(g, r, ask);
    CHECK(Find(v, Action::Travel, kTravelMine));
    // Far from every site: no way back, but the site's gate.
    ask.map = 0;
    v = CourierServices(g, r, ask);
    CHECK(!Find(v, Action::Travel, kTravelBack) && Find(v, Action::Travel, 0) && Find(v, Action::Travel, kTravelMine));
    Plots::Site const* site = nullptr;
    Plots::Spot at;
    CHECK(TravelStop(g, 1, site, at) && site && at.x == 1300.f);
    CHECK(TravelStop(g, 0, site, at) && at.x == 1000.f && at.y == 1990.f);
    CHECK(!TravelStop(g, 2, site, at) && !TravelStop(g, kStopsPerSite, site, at) && !TravelStop(g, kTravelBack, site, at));
}

TEST(the_window_round_trips_on_the_wire)
{
    Window w;
    w.open = Open::Npc;
    w.npc = Npc::Steward;
    w.npcName = "Warden Oakhallow";
    w.tone = Tone::Error;
    w.message = "You already hold a homestead.";
    w.plot.id = 301;
    w.plot.size = 1;
    w.plot.standing = 1;
    w.plot.map = 37;
    w.plot.bounds = {150.f, 0.f, 174.f, 24.f};
    w.plot.paidUntil = 1700604800;
    w.plot.upkeep = 2000;
    w.plot.permits = 2;
    w.plot.pieces = 41;
    w.plot.place = "in Azshara Crater";
    w.reclaim = 5;
    Service s;
    s.action = Action::Pay;
    s.arg = 4;
    s.cost = 8000;
    s.enabled = false;
    s.confirm = true;
    s.hue = Hue::Green;
    s.label = "Pay four weeks of upkeep";
    s.note = "4 weeks";
    w.services = {s, Service{}};
    std::vector<uint8_t> const b = EncodeWindow(w);
    CHECK(b[0] == kOpWindow && b[1] == uint8_t(Open::Npc) && b[2] == uint8_t(Npc::Steward));
    Window back;
    CHECK(DecodeWindow(b, back));
    CHECK(back.open == Open::Npc && back.npc == Npc::Steward && back.npcName == w.npcName);
    CHECK(back.tone == Tone::Error && back.message == w.message);
    CHECK(back.plot.id == 301 && back.plot.map == 37 && back.plot.bounds.x1 == 174.f && back.plot.pieces == 41);
    CHECK(back.plot.place == "in Azshara Crater" && back.reclaim == 5 && back.plot.paidUntil == 1700604800u);
    CHECK(back.services.size() == 2 && back.services[0].label == s.label && back.services[0].note == s.note);
    CHECK(!back.services[0].enabled && back.services[0].confirm && back.services[0].hue == Hue::Green && back.services[0].cost == 8000);
    CHECK(back.services[1].enabled && !back.services[1].confirm);
    // A byte short, or one over: nothing.
    std::vector<uint8_t> shorter(b.begin(), b.end() - 1);
    CHECK(!DecodeWindow(shorter, back));
    std::vector<uint8_t> longer = b;
    longer.push_back(0);
    CHECK(!DecodeWindow(longer, back));
    // A long text is cut at 255 bytes, never overrunning.
    Window big;
    big.message = std::string(400, 'x');
    big.services.assign(kMaxServices + 10, s);
    Window cut;
    CHECK(DecodeWindow(EncodeWindow(big), cut));
    CHECK(cut.message.size() == 255 && cut.services.size() == kMaxServices);
    CHECK(EncodeWindow(big).size() < 8192);
}

TEST(a_button_press_decodes_and_nothing_else_does)
{
    std::vector<uint8_t> const b = EncodeAct(Action::Travel, kTravelMine);
    CHECK(b.size() == 6 && b[0] == kOpAct && b[1] == uint8_t(Action::Travel));
    Action a = Action::None;
    uint32_t arg = 0;
    CHECK(DecodeAct(b, a, arg) && a == Action::Travel && arg == kTravelMine);
    CHECK(!DecodeAct({kOpAct, 0, 0, 0, 0, 0}, a, arg));     // no action
    CHECK(!DecodeAct({kOpAct, 12, 0, 0, 0, 0}, a, arg));    // past the last
    CHECK(!DecodeAct({kOpAct, 1, 0, 0, 0}, a, arg));
    CHECK(!DecodeAct({kOpAsk}, a, arg));
    CHECK(MoneyText(0) == "0c" && MoneyText(250000) == "25g" && MoneyText(12345) == "1g 23s 45c" && MoneyText(2000) == "20s");
}

TEST(nearby_claimed_plots_are_marked_the_players_own_first)
{
    Plots::Grid const g = TestGrid();
    Registry r;
    uint32_t charge = 0;
    CHECK(Plots::Buy(g, r, 1, Rich(70, 7), kNow, charge) == BuyResult::Ok);
    CHECK(Plots::Buy(g, r, 2, Rich(80, 8), kNow, charge) == BuyResult::Ok);
    CHECK(Plots::Buy(g, r, 101, Rich(90, 9), kNow, charge) == BuyResult::Ok);
    Plots::Grant(*r.Find(2), 71, Plots::PermitLevel::Guest);
    // Character 71 of account 7, standing by plot 2.
    Plots::Actor const me = Plots::Actor::Player(71, 7);
    std::vector<NearPlot> v = PlotsNear(g, r, me, 1, 1060.f, 2020.f, kNearRadius, kNow);
    CHECK(v.size() == 3);
    CHECK(v[0].id == 1 && v[0].relation == Relation::Mine);
    CHECK(v[1].id == 2 && v[1].relation == Relation::Permitted);
    CHECK(v[2].id == 101 && v[2].relation == Relation::Claimed && v[2].bounds.x1 == 1264.f);
    // Out of range, another map, a plot for sale or lapsed: none.
    CHECK(PlotsNear(g, r, me, 1, 5000.f, 5000.f, kNearRadius, kNow).empty());
    CHECK(PlotsNear(g, r, me, 0, 1060.f, 2020.f, kNearRadius, kNow).empty());
    int64_t const later = kNow + int64_t(Plots::kIncludedWeeks + Plots::kGraceWeeks) * Plots::kWeek + 1;
    CHECK(PlotsNear(g, r, me, 1, 1060.f, 2020.f, kNearRadius, later).empty());
    // On the wire: count, then each plot's 21 bytes.
    std::vector<uint8_t> const b = EncodeNearPlots(v);
    CHECK(b.size() == 2 + 3 * 21 && b[0] == kOpNearPlots && b[1] == 3);
    CHECK(b[2] == 1 && b[22] == uint8_t(Relation::Mine) && b[43] == uint8_t(Relation::Permitted));
    float x1;
    std::memcpy(&x1, &b[2 + 21 * 2 + 4 + 8], 4);
    CHECK(x1 == 1264.f);
    CHECK(EncodeNearPlots({}) == (std::vector<uint8_t>{kOpNearPlots, 0}));
}
