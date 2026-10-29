/*
 * Azeroth Warfare: tests for portals and the shared hearthstone cooldown (Portals.h, and the
 * courier's way back in Homestead.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Homestead.h"
#include "fusion/Portals.h"

using namespace Fusion;
using namespace Fusion::Portals;

namespace
{
    bool Lists(std::vector<Choice> const& v, uint8_t id)
    {
        for (Choice const& c : v)
            if (c.id == id)
                return true;
        return false;
    }
}

TEST(a_portal_opens_on_its_users_own_capitals_never_the_enemys)
{
    CHECK(Capitals().size() == 6);
    std::vector<Choice> const alliance = ChoicesFor(Team::Alliance, false);
    std::vector<Choice> const horde = ChoicesFor(Team::Horde, true);
    CHECK(alliance.size() == 3 && horde.size() == 4);
    CHECK(alliance[0].name == "Stormwind" && Lists(alliance, 2) && Lists(alliance, 3));
    CHECK(!Lists(alliance, 4) && !Lists(alliance, 5) && !Lists(alliance, 6) && !Lists(alliance, kHomestead));
    CHECK(Lists(horde, 4) && Lists(horde, 5) && Lists(horde, 6) && Lists(horde, kHomestead));
    CHECK(!Lists(horde, 1) && !Lists(horde, 2) && !Lists(horde, 3));
    // The server checks again whatever the client sends.
    CHECK(Allowed(Team::Alliance, 1, false) && !Allowed(Team::Alliance, 4, false));
    CHECK(Allowed(Team::Horde, 6, false) && !Allowed(Team::Horde, 2, true));
    CHECK(Allowed(Team::Horde, kHomestead, true) && !Allowed(Team::Horde, kHomestead, false));
    CHECK(!Allowed(Team::Alliance, 0, true) && !Allowed(Team::Alliance, 7, true));
    // Each capital names the mage teleport whose landing it uses, on the right continent.
    for (Destination const& d : Capitals())
    {
        CHECK(d.spell >= 3561 && d.spell <= 3567);
        CHECK(d.map == 0 || d.map == 1);
        CHECK(CapitalById(d.id) == &d);
    }
    CHECK(CapitalById(1)->map == 0 && CapitalById(4)->map == 1);   // Stormwind, Orgrimmar
    CHECK(CapitalById(kHomestead) == nullptr);
}

TEST(travel_waits_on_the_hearthstones_cooldown)
{
    Traveller t;
    CHECK(CanTravel(t) == TravelCheck::Ok);
    t.cooldownLeftMs = 1;
    CHECK(CanTravel(t) == TravelCheck::CoolingDown);
    t.cooldownLeftMs = 0;
    t.inCombat = true;
    CHECK(CanTravel(t) == TravelCheck::InCombat);
    t.inCombat = false;
    t.travelling = true;
    CHECK(CanTravel(t) == TravelCheck::Travelling);
    t.alive = false;
    CHECK(CanTravel(t) == TravelCheck::Dead);
    CHECK(kHearthSpell == 8690 && kHearthItem == 6948 && kHearthCooldownMs == 3600000);
    CHECK(WaitText(10 * 1000) == "under a minute");
    CHECK(WaitText(61 * 1000) == "2 min");
    CHECK(WaitText(kHearthCooldownMs) == "60 min");
}

TEST(the_couriers_way_back_shares_the_hearthstones_cooldown)
{
    Plots::Site site;
    site.id = 1;
    site.name = "Test";
    site.map = 1;
    site.gate = {1000.f, 1990.f, 100.f, 0.f};
    site.bounds = {900.f, 1900.f, 1400.f, 2200.f};
    Plots::District d;
    d.map = 1;
    d.originX = 1000.f;
    d.originY = 2000.f;
    d.cols = 2;
    d.rows = 1;
    d.firstId = 1;
    d.site = 1;
    Plots::Grid const g({d}, {site});
    Plots::Registry r;
    Homestead::CourierAsk ask;
    ask.map = 1;
    ask.x = 1000.f;
    ask.y = 1990.f;
    ask.canGoBack = true;
    auto back = [&]() -> Homestead::Service const* {
        static std::vector<Homestead::Service> v;
        v = Homestead::CourierServices(g, r, ask);
        for (Homestead::Service const& s : v)
            if (s.action == Homestead::Action::Travel && s.arg == Homestead::kTravelBack)
                return &s;
        return nullptr;
    };
    Homestead::Service const* ready = back();
    CHECK(ready && ready->enabled && ready->note.find("hearthstone's cooldown") != std::string::npos);
    ask.backCooldownMs = 25 * 60 * 1000;
    Homestead::Service const* waiting = back();
    CHECK(waiting && !waiting->enabled && waiting->note.find("25 min") != std::string::npos);
}

TEST(a_player_keeps_two_portals_and_a_plot_holds_one)
{
    CHECK(MayPlace(0, false, 0, false) && MayPlace(1, false, 0, false));
    CHECK(!MayPlace(kMaxPerPlayer, false, 0, false));
    CHECK(MayPlace(1, true, 0, false) && !MayPlace(1, true, kMaxPerPlot, false));
    CHECK(MayPlace(9, true, 9, true));      // a game master
}

TEST(portal_messages_round_trip)
{
    uint32_t piece = 0;
    uint8_t dest = 0;
    CHECK(DecodeAsk(EncodeAsk(0x0A0B0C0D), piece) && piece == 0x0A0B0C0D);
    CHECK(EncodeAsk(1) == std::vector<uint8_t>({kOpAsk, 1, 0, 0, 0}));
    CHECK(DecodeTravel(EncodeTravel(12, kHomestead), piece, dest) && piece == 12 && dest == kHomestead);
    CHECK(!DecodeTravel({kOpTravel, 1, 0, 0, 0}, piece, dest));
    Menu m;
    m.piece = 44;
    m.cooldownSeconds = 1800;
    m.choices = ChoicesFor(Team::Alliance, true);
    std::vector<uint8_t> const b = EncodeMenu(m);
    CHECK(b[0] == kOpMenu && b[9] == 4);
    Menu back;
    CHECK(DecodeMenu(b, back) && back.piece == 44 && back.cooldownSeconds == 1800 && back.choices.size() == 4);
    CHECK(back.choices[3].id == kHomestead && back.choices[3].name == "My homestead");
    std::vector<uint8_t> cut(b.begin(), b.end() - 1);
    CHECK(!DecodeMenu(cut, back));
    // Free ops on the build channel: 6 and 7 after the locks' (4, 5), 20 after theirs (19).
    CHECK(kOpAsk == 6 && kOpTravel == 7 && kOpMenu == 20);
}
