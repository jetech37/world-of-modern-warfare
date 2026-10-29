/*
 * Azeroth Warfare: tests for plots (Plots.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Plots.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace Fusion;
using namespace Fusion::Plots;
using Fusion::Build::Piece;
using Fusion::Build::PieceType;

namespace
{
    // Two small districts on map 1: 3 by 2 plots of 40 from (1000, 2000), ids 1 to 6; and one of
    // 64 from (2000, 2000), ids 101 and 102.
    Grid TestGrid()
    {
        return Grid({
            {Size::Standard, 1, 1000.f, 2000.f, 40.f, 3, 2, 1, 250000, 10000, 0},
            {Size::Large, 1, 2000.f, 2000.f, 64.f, 2, 1, 101, 1000000, 30000, 0},
        });
    }

    Piece At(PieceType t, float x, float y, uint32_t owner = 0, float yaw = 0.f)
    {
        Piece p;
        p.id = 1;
        p.type = t;
        p.map = 1;
        p.pos = {x, y, 100.f};
        p.yaw = yaw;
        p.owner = owner;
        p.health = Build::Def(t).health;
        return p;
    }

    constexpr int64_t kNow = 1700000000;
    // Account 7 owns plot 1 through character 70; character 71 is a builder there, 72 a guest,
    // 80 (account 8) a stranger.
    Registry Owned()
    {
        Registry r;
        Plot p;
        p.id = 1;
        p.ownerAccount = 7;
        p.ownerGuid = 70;
        p.boughtAt = kNow;
        p.paidUntil = kNow + kWeek;
        Grant(p, 71, PermitLevel::Builder);
        Grant(p, 72, PermitLevel::Guest);
        r.plots[1] = p;
        return r;
    }
}

TEST(plots_are_found_by_position_and_edges_belong_to_one)
{
    Grid const g = TestGrid();
    CHECK(g.Count() == 8);
    CHECK(g.PlotAt(1, 1000.f, 2000.f) == 1);           // the very corner
    CHECK(g.PlotAt(1, 1039.9f, 2039.9f) == 1);
    CHECK(g.PlotAt(1, 1040.f, 2000.f) == 2);           // the line between 1 and 2 is 2's
    CHECK(g.PlotAt(1, 1100.f, 2010.f) == 3);
    CHECK(g.PlotAt(1, 1000.f, 2040.f) == 4);           // the next row
    CHECK(g.PlotAt(1, 1119.99f, 2079.99f) == 6);
    CHECK(g.PlotAt(1, 1120.f, 2010.f) == 0);           // the far edge is outside
    CHECK(g.PlotAt(1, 1010.f, 2080.f) == 0);
    CHECK(g.PlotAt(1, 999.99f, 2010.f) == 0);
    CHECK(g.PlotAt(1, 1010.f, 1999.99f) == 0);
    CHECK(g.PlotAt(0, 1010.f, 2010.f) == 0);           // another map
    CHECK(g.PlotAt(1, 2070.f, 2010.f) == 102);
    auto b = g.Bounds(5);
    CHECK(b && b->x0 == 1040.f && b->y0 == 2040.f && b->x1 == 1080.f && b->y1 == 2080.f);
    CHECK(!g.Bounds(7) && !g.Bounds(0) && !g.Bounds(103));
    CHECK(g.DistrictOf(102) && g.DistrictOf(102)->size == Size::Large);
    CHECK(g.InDistrict(1, 1050.f, 2050.f) && !g.InDistrict(1, 1130.f, 2050.f));
    CHECK(g.InDistrict(1, 1130.f, 2050.f, 15.f));
}

TEST(the_default_districts_do_not_overlap_and_ids_are_unique)
{
    Grid const& g = Default();
    CHECK(g.Districts().size() == 9);
    CHECK(g.Count() == (32 + 24 + 8) + (32 + 24 + 8) + (64 + 96 + 16));
    CHECK(g.Problems().empty());
    for (std::string const& p : g.Problems())
        std::printf("    layout: %s\n", p.c_str());
    for (size_t i = 0; i < g.Districts().size(); ++i)
        for (size_t j = i + 1; j < g.Districts().size(); ++j)
        {
            District const& a = g.Districts()[i];
            District const& b = g.Districts()[j];
            Rect const ra = g.DistrictBounds(a), rb = g.DistrictBounds(b);
            bool const apart = ra.x1 <= rb.x0 || rb.x1 <= ra.x0 || ra.y1 <= rb.y0 || rb.y1 <= ra.y0;
            CHECK(apart);
            bool const idsApart = a.firstId + a.Count() <= b.firstId || b.firstId + b.Count() <= a.firstId;
            CHECK(idsApart);
        }
    // Each plot's middle finds it again, and prices rise with size.
    for (District const& d : g.Districts())
    {
        CHECK(d.deedItem != 0 && g.SiteById(d.site) && g.SiteById(d.site)->map == d.map);
        for (uint32_t id = d.firstId; id < d.firstId + d.Count(); ++id)
        {
            Rect const r = *g.Bounds(id);
            CHECK(g.PlotAt(d.map, (r.x0 + r.x1) / 2.f, (r.y0 + r.y1) / 2.f) == id);
        }
    }
    // Prices rise with size, and each site's deeds are its own.
    for (size_t i = 0; i < g.Districts().size(); ++i)
        for (size_t j = 0; j < g.Districts().size(); ++j)
        {
            District const& a = g.Districts()[i];
            District const& b = g.Districts()[j];
            if (a.size < b.size)
                CHECK(a.priceCopper < b.priceCopper);
            if (i != j)
                CHECK(a.deedItem != b.deedItem);
        }
}

TEST(the_hyjal_re_layout_keeps_every_plot_id)
{
    // Plots were on sale before the move: no id may be renumbered or vanish.
    Grid const& g = Default();
    for (uint32_t id = 1; id <= 32; ++id)
        CHECK(g.DistrictOf(id) && g.DistrictOf(id)->size == Size::Small && g.SiteOf(id)->id == 1);
    for (uint32_t id = 101; id <= 124; ++id)
        CHECK(g.DistrictOf(id) && g.DistrictOf(id)->size == Size::Standard && g.SiteOf(id)->id == 1);
    for (uint32_t id = 201; id <= 208; ++id)
        CHECK(g.DistrictOf(id) && g.DistrictOf(id)->size == Size::Large && g.SiteOf(id)->id == 1);
    CHECK(!g.DistrictOf(33) && !g.DistrictOf(125) && !g.DistrictOf(209));
    // The measured spots: the flat east terrace, the gate pocket, the crater floor.
    CHECK(g.PlotAt(1, 5521.f, -3449.f) == 1);
    CHECK(g.PlotAt(1, 5401.f, -3789.f) == 101);
    CHECK(g.PlotAt(1, 5361.f, -2869.f) == 201);
    CHECK(g.PlotAt(1, 5361.f, -2869.f + 64.f * 3.f) == 207);     // 2 by 4 now
    // The old gate (5476, -3729) is under the standard plots now; the gate moved south of them.
    CHECK(g.PlotAt(1, 5476.f, -3729.f) != 0);
    Site const* hyjal = g.SiteById(1);
    CHECK(hyjal && g.PlotAt(1, hyjal->gate.x, hyjal->gate.y) == 0);
}

TEST(azshara_crater_and_the_plateau_have_their_measured_plots)
{
    Grid const& g = Default();
    auto count = [&](uint8_t site, Size size) {
        uint32_t n = 0;
        for (District const& d : g.Districts())
            if (d.site == site && d.size == size)
                n += d.Count();
        return n;
    };
    CHECK(count(2, Size::Standard) == 24 && count(2, Size::Small) == 32 && count(2, Size::Large) == 8);
    CHECK(count(3, Size::Standard) == 96 && count(3, Size::Small) == 64 && count(3, Size::Large) == 16);
    CHECK(g.SiteById(2)->map == 37 && g.SiteById(3)->map == 0);
    CHECK(g.PlotAt(37, 151.f, 1.f) == 301 && g.PlotAt(37, 76.f, 601.f) == 401 && g.PlotAt(37, -674.f, -324.f) == 501);
    CHECK(g.PlotAt(0, 3751.f, -4294.f) == 601 && g.PlotAt(0, 3751.f, -4624.f) == 701 && g.PlotAt(0, 3751.f, -4890.f) == 801);
    CHECK(g.SiteOf(424)->name == "Azshara Crater" && g.SiteOf(816)->id == 3);
    // A 10 yard road between the plateau's blocks: the gate stands in it.
    CHECK(g.PlotAt(0, 3760.f, -4300.f) == 0);
}

TEST(sites_put_back_whoever_falls_through_and_keep_raids_out)
{
    Grid const& g = Default();
    // Down the shaft by Hyjal's east terrace: back to the gate.
    std::optional<Spot> back = g.FellThrough(1, 5470.f, -3500.f, 1150.f);
    CHECK(back && back->x == 5385.f && back->z > 1590.f);
    CHECK(!g.FellThrough(1, 5470.f, -3500.f, 1590.f));
    // Off the bottom of the large plots, the crater floor's own stop is the nearer.
    back = g.FellThrough(1, 5400.f, -2700.f, 1300.f);
    CHECK(back && back->x == 5500.f);
    // Azshara Crater's filler outside the rim at z -29.6; Quel'Thalas's void ocean at -500.
    CHECK(g.FellThrough(37, 650.f, 1400.f, -29.6f));
    CHECK(g.FellThrough(0, 4600.f, -4500.f, -500.f));
    CHECK(!g.FellThrough(0, 3800.f, -4400.f, 129.f));
    // Felwood far below Hyjal is outside the site: the open world.
    CHECK(!g.FellThrough(1, 5000.f, -1500.f, 500.f));
    CHECK(g.SiteAt(1, 5299.f, -3700.f) == nullptr && g.SiteAt(1, 5299.f, -3700.f, 2.f) == g.SiteById(1));
    // No raid anywhere in a site, even far from its plots.
    CHECK(!RaidsAllowedAt(g, 37, -300.f, 1300.f));
    CHECK(RaidsAllowedAt(g, 1, 5000.f, -1500.f));
}

TEST(a_layout_with_mistakes_is_reported)
{
    Site s{1, "Test Site", "at the test site", 1, {990.f, 2010.f, 100.f, 0.f}, {}, {900.f, 1900.f, 1300.f, 2300.f}, 50.f, 200.f};
    Grid const good({{Size::Standard, 1, 1000.f, 2000.f, 40.f, 3, 2, 1, 1, 1, 1, 1}}, {s});
    CHECK(good.Problems().empty());
    // Two districts on top of each other, sharing ids.
    Grid const overlap({{Size::Standard, 1, 1000.f, 2000.f, 40.f, 3, 2, 1, 1, 1, 1, 1},
                        {Size::Small, 1, 1020.f, 2020.f, 24.f, 2, 2, 5, 1, 1, 1, 1}}, {s});
    CHECK(overlap.Problems().size() == 2);
    // A district outside its site, one with no site, a gate on a plot.
    Grid const outside({{Size::Standard, 1, 1280.f, 2000.f, 40.f, 3, 2, 1, 1, 1, 1, 1}}, {s});
    CHECK(outside.Problems().size() == 1);
    Grid const orphan({{Size::Standard, 1, 1000.f, 2000.f, 40.f, 3, 2, 1, 1, 1, 1, 9}}, {s});
    CHECK(orphan.Problems().size() == 1);
    Site onPlot = s;
    onPlot.gate = {1010.f, 2010.f, 100.f, 0.f};
    Grid const gate({{Size::Standard, 1, 1000.f, 2000.f, 40.f, 3, 2, 1, 1, 1, 1, 1}}, {onPlot});
    CHECK(gate.Problems().size() == 1);
}

TEST(buying_checks_the_plot_the_limit_and_the_gold)
{
    Grid const g = TestGrid();
    Registry r;
    uint32_t charge = 99;
    Buyer const rich{70, 7, 10000000};
    CHECK(Buy(g, r, 7, rich, kNow, charge) == BuyResult::NoSuchPlot && charge == 0);
    CHECK(Buy(g, r, 1, {70, 7, 249999}, kNow, charge) == BuyResult::NotEnoughMoney);
    CHECK(Buy(g, r, 1, rich, kNow, charge) == BuyResult::Ok);
    CHECK(charge == 250000);
    Plot const* p = r.Find(1);
    CHECK(p && p->ownerAccount == 7 && p->ownerGuid == 70 && p->paidUntil == kNow + int64_t(kIncludedWeeks) * kWeek);
    CHECK(StandingOf(*p, kNow) == Standing::Paid);
    // Taken; and one to an account, alts included.
    CHECK(Buy(g, r, 1, {80, 8, 10000000}, kNow, charge) == BuyResult::Taken);
    CHECK(Buy(g, r, 2, {71, 7, 10000000}, kNow, charge) == BuyResult::LimitReached);
    CHECK(Buy(g, r, 101, {80, 8, 1000000}, kNow, charge) == BuyResult::Ok && charge == 1000000);
    CHECK(r.CountOwnedBy(7) == 1 && r.CountOwnedBy(8) == 1 && r.OwnedBy(8)->id == 101);
    // Given up, it's for sale again and the account may buy another.
    CHECK(!Abandon(r, 1, 8));
    CHECK(Abandon(r, 1, 7));
    CHECK(Buy(g, r, 2, {71, 7, 10000000}, kNow, charge) == BuyResult::Ok);
}

TEST(upkeep_runs_on_from_the_paid_time_and_a_plot_lapses_after_grace)
{
    Grid const g = TestGrid();
    Registry r = Owned();
    Plot& p = *r.Find(1);
    uint32_t charge = 0;
    CHECK(StandingOf(p, kNow + kWeek - 1) == Standing::Paid);
    CHECK(StandingOf(p, kNow + kWeek) == Standing::Overdue);
    CHECK(StandingOf(p, kNow + kWeek * (1 + kGraceWeeks) - 1) == Standing::Overdue);
    CHECK(StandingOf(p, kNow + kWeek * (1 + kGraceWeeks)) == Standing::Lapsed);

    CHECK(PayUpkeep(g, p, 8, 1, 1000000, kNow, charge) == PayResult::NotOwner);
    CHECK(PayUpkeep(g, p, 7, 0, 1000000, kNow, charge) == PayResult::NoWeeks);
    CHECK(PayUpkeep(g, p, 7, 3, 29999, kNow, charge) == PayResult::NotEnoughMoney);
    CHECK(PayUpkeep(g, p, 7, kMaxPrepaidWeeks, 10000000, kNow, charge) == PayResult::TooFarAhead);
    CHECK(PayUpkeep(g, p, 7, 3, 30000, kNow, charge) == PayResult::Ok && charge == 30000);
    CHECK(p.paidUntil == kNow + 4 * kWeek);
    // Overdue: a week after the paid time ran out, paying one week only covers the week missed.
    int64_t const late = p.paidUntil + kWeek + 3600;
    CHECK(StandingOf(p, late) == Standing::Overdue);
    CHECK(PayUpkeep(g, p, 7, 1, 10000, late, charge) == PayResult::Ok);
    CHECK(StandingOf(p, late) == Standing::Overdue);
    CHECK(PayUpkeep(g, p, 7, 1, 10000, late, charge) == PayResult::Ok);
    CHECK(StandingOf(p, late) == Standing::Paid);
    // Lapsed: no paying it back; the registry clears it and it's for sale.
    int64_t const gone = p.paidUntil + int64_t(kGraceWeeks) * kWeek;
    CHECK(PayUpkeep(g, p, 7, 1, 10000, gone, charge) == PayResult::Lapsed);
    CHECK(r.Lapse(gone - 1).empty());
    std::vector<uint32_t> const lapsed = r.Lapse(gone);
    CHECK(lapsed.size() == 1 && lapsed[0] == 1);
    CHECK(!r.Find(1));
    CHECK(Buy(g, r, 1, {80, 8, 250000}, gone, charge) == BuyResult::Ok);
}

TEST(a_lapsed_plots_pieces_are_packed_for_their_builders)
{
    std::vector<Piece> pieces = {At(PieceType::WoodFloor, 0, 0, 70), At(PieceType::WoodFloor, 3, 0, 70),
        At(PieceType::WoodDoor, 0, 3, 71), At(PieceType::WoodBeam, 6, 0, 70)};
    std::vector<Reclaim> const back = Pack(pieces);
    using Construction::SalvagedOf;
    uint32_t lumber70 = 0, sturdy71 = 0, hinge71 = 0;
    for (Reclaim const& k : back)
    {
        if (k.guid == 70 && k.item == SalvagedOf(Construction::kRoughLumber)) lumber70 += k.count;
        if (k.guid == 71 && k.item == SalvagedOf(Construction::kSturdyLumber)) sturdy71 += k.count;
        if (k.guid == 71 && k.item == SalvagedOf(Construction::kBronzeHinge)) hinge71 += k.count;
        CHECK(k.guid == 70 || k.guid == 71);
        // Only salvaged goods come back: they build, but teach no Construction.
        CHECK(Construction::FreshOf(k.item) != 0);
    }
    CHECK(lumber70 == 6 + 6 + 3);       // what taking each down gives: half, rounded down
    CHECK(sturdy71 == 7 && hinge71 == 1);
    // As taking down: scaled by what is left of it.
    Piece hurt = At(PieceType::WoodDoor, 0, 3, 73);
    hurt.health = Build::MaxHealth(hurt) * 0.5f;
    uint32_t sturdy = 0;
    for (Reclaim const& k : Pack({hurt}))
        if (k.item == SalvagedOf(Construction::kSturdyLumber)) sturdy += k.count;
    CHECK(sturdy == 3);
    // A reinforced piece's reinforcement comes back at the same share.
    Piece braced = At(PieceType::WoodWall, 9, 0, 72);
    braced.reinforcement = 1;
    braced.health = Build::MaxHealth(braced);
    uint32_t brackets = 0;
    for (Reclaim const& k : Pack({braced}))
        if (k.item == SalvagedOf(Construction::kIronBracket)) brackets += k.count;
    CHECK(brackets == Construction::ReinforceInfo(1)->materials[0].count / 2);
    CHECK(Pack({}).empty());
    // Only what was paid for comes back (security review 2026-10, #14): a piece built free gives
    // nothing, a tier put on free nothing of that tier.
    Piece free = At(PieceType::WoodWall, 9, 0, 72);
    free.paid = uint8_t(Build::kPaidAll & ~Build::kPaidPiece);
    CHECK(Pack({free}).empty());
    Piece freeTier = braced;
    freeTier.paid = uint8_t(Build::kPaidAll & ~Build::PaidTier(1));
    brackets = 0;
    uint32_t lumber = 0;
    for (Reclaim const& k : Pack({freeTier}))
    {
        if (k.item == SalvagedOf(Construction::kIronBracket)) brackets += k.count;
        if (k.item == SalvagedOf(Construction::kRoughLumber)) lumber += k.count;
    }
    CHECK(brackets == 0 && lumber > 0);
}

TEST(permits_are_granted_changed_and_revoked)
{
    Registry r = Owned();
    Plot& p = *r.Find(1);
    CHECK(PermitOf(p, 71) == PermitLevel::Builder && PermitOf(p, 72) == PermitLevel::Guest);
    CHECK(PermitOf(p, 80) == PermitLevel::None);
    CHECK(!Grant(p, 70, PermitLevel::Builder));         // the owner needs none
    CHECK(!Grant(p, 80, PermitLevel::None));
    CHECK(Grant(p, 72, PermitLevel::Builder) && PermitOf(p, 72) == PermitLevel::Builder && p.permits.size() == 2);
    CHECK(Revoke(p, 72) && !Revoke(p, 72) && PermitOf(p, 72) == PermitLevel::None);
    for (uint32_t i = 0; i < kMaxPermits; ++i)
        Grant(p, 1000 + i, PermitLevel::Guest);
    CHECK(p.permits.size() == kMaxPermits);
    CHECK(!Grant(p, 5000, PermitLevel::Guest));
}

TEST(only_the_owner_and_permitted_touch_a_plot)
{
    Grid const g = TestGrid();
    Registry const r = Owned();
    Actor const owner = Actor::Player(70, 7);
    Actor const alt = Actor::Player(77, 7);              // the owner's other character
    Actor const builder = Actor::Player(71, 9);
    Actor const guest = Actor::Player(72, 10);
    Actor const stranger = Actor::Player(80, 8);
    Actor const gm = Actor::Player(90, 11, true);
    Actor const creature = Actor::Creature();
    Actor const world = Actor::World();

    Piece const mine = At(PieceType::WoodWall, 1020.f, 2020.f, 70);
    Piece const theirs = At(PieceType::WoodWall, 1020.f, 2025.f, 71);
    Piece door = At(PieceType::WoodDoor, 1020.f, 2030.f, 70);

    CHECK(AccessOf(owner, r.Find(1), kNow) == Access::Owner);
    CHECK(AccessOf(alt, r.Find(1), kNow) == Access::Owner);
    CHECK(AccessOf(builder, r.Find(1), kNow) == Access::Builder);
    CHECK(AccessOf(guest, r.Find(1), kNow) == Access::Guest);
    CHECK(AccessOf(stranger, r.Find(1), kNow) == Access::None);
    CHECK(AccessOf(gm, r.Find(1), kNow) == Access::Gm);
    CHECK(AccessOf(creature, r.Find(1), kNow) == Access::None);

    // Building.
    CHECK(CanBuild(g, r, owner, mine, kNow) == Verdict::Allowed);
    CHECK(CanBuild(g, r, alt, mine, kNow) == Verdict::Allowed);
    CHECK(CanBuild(g, r, builder, theirs, kNow) == Verdict::Allowed);
    CHECK(CanBuild(g, r, guest, mine, kNow) == Verdict::Denied);
    CHECK(CanBuild(g, r, stranger, mine, kNow) == Verdict::Denied);
    CHECK(CanBuild(g, r, gm, mine, kNow) == Verdict::Allowed);
    // Taking down: the owner anything, a builder only their own.
    CHECK(CanRemove(g, r, owner, theirs, kNow) == Verdict::Allowed);
    CHECK(CanRemove(g, r, builder, theirs, kNow) == Verdict::Allowed);
    CHECK(CanRemove(g, r, builder, mine, kNow) == Verdict::Denied);
    CHECK(CanRemove(g, r, guest, mine, kNow) == Verdict::Denied);
    CHECK(CanRemove(g, r, stranger, theirs, kNow) == Verdict::Denied);
    CHECK(CanRemove(g, r, gm, mine, kNow) == Verdict::Allowed);
    // Damage: never a creature, a raid, a sentry or a stray blast.
    CHECK(CanDamage(g, r, owner, mine, kNow) == Verdict::Allowed);
    // A builder breaks only their own (no shooting the owner's walls for salvage).
    CHECK(CanDamage(g, r, builder, mine, kNow) == Verdict::Denied);
    CHECK(CanDamage(g, r, builder, theirs, kNow) == Verdict::Allowed);
    CHECK(CanDamage(g, r, owner, theirs, kNow) == Verdict::Allowed);
    CHECK(CanDamage(g, r, guest, mine, kNow) == Verdict::Denied);
    CHECK(CanDamage(g, r, stranger, mine, kNow) == Verdict::Denied);
    CHECK(CanDamage(g, r, creature, mine, kNow) == Verdict::Denied);
    CHECK(CanDamage(g, r, world, mine, kNow) == Verdict::Denied);
    CHECK(CanDamage(g, r, gm, mine, kNow) == Verdict::Allowed);
    // Doors: anyone permitted.
    CHECK(CanUseDoor(g, r, owner, door, kNow) == Verdict::Allowed);
    CHECK(CanUseDoor(g, r, builder, door, kNow) == Verdict::Allowed);
    CHECK(CanUseDoor(g, r, guest, door, kNow) == Verdict::Allowed);
    CHECK(CanUseDoor(g, r, stranger, door, kNow) == Verdict::Denied);
    CHECK(CanUseDoor(g, r, creature, door, kNow) == Verdict::Denied);
    CHECK(CanUseDoor(g, r, gm, door, kNow) == Verdict::Allowed);
}

TEST(security_a_plot_owner_breaking_a_builders_piece_sends_the_salvage_to_the_builder)
{
    // Security review 2026-10, #25: the owner shooting down a permitted builder's wall kept 40%.
    Grid const g = TestGrid();
    Registry const r = Owned();
    Actor const owner = Actor::Player(70, 7);
    Actor const alt = Actor::Player(77, 7);
    Actor const builder = Actor::Player(71, 9);
    Actor const gm = Actor::Player(90, 11, true);
    Piece const ownersWall = At(PieceType::WoodWall, 1020.f, 2020.f, 70);
    Piece const buildersWall = At(PieceType::WoodWall, 1020.f, 2025.f, 71);
    CHECK(SalvageToBuilder(g, r, owner, buildersWall, kNow));
    CHECK(SalvageToBuilder(g, r, alt, buildersWall, kNow));
    CHECK(SalvageToBuilder(g, r, gm, buildersWall, kNow));
    CHECK(SalvageToBuilder(g, r, alt, ownersWall, kNow));           // to the character who built it
    // Their own pieces: the breaker keeps it, as before.
    CHECK(!SalvageToBuilder(g, r, owner, ownersWall, kNow));
    CHECK(!SalvageToBuilder(g, r, builder, buildersWall, kNow));
    // Not a player, no builder, or out in the world: as before.
    CHECK(!SalvageToBuilder(g, r, Actor::Creature(), buildersWall, kNow));
    Piece nobodys = buildersWall;
    nobodys.owner = 0;
    CHECK(!SalvageToBuilder(g, r, owner, nobodys, kNow));
    Piece const out = At(PieceType::WoodWall, 500.f, 500.f, 71);
    CHECK(!SalvageToBuilder(g, r, owner, out, kNow));
    CHECK(!SalvageToBuilder(g, r, Actor::Player(80, 8), out, kNow));
}

TEST(outside_the_districts_the_open_world_rules_stand)
{
    Grid const g = TestGrid();
    Registry const r = Owned();
    Piece const out = At(PieceType::WoodWall, 500.f, 500.f, 80);
    Piece other = out;
    other.map = 0;
    other.pos = {1020.f, 2020.f, 0.f};                  // a plot's x and y, but another map
    for (Actor const& a : {Actor::Player(80, 8), Actor::Creature(), Actor::World()})
    {
        CHECK(CanBuild(g, r, a, out, kNow) == Verdict::OpenWorld);
        CHECK(CanRemove(g, r, a, out, kNow) == Verdict::OpenWorld);
        CHECK(CanDamage(g, r, a, out, kNow) == Verdict::OpenWorld);
        CHECK(CanUseDoor(g, r, a, out, kNow) == Verdict::OpenWorld);
        CHECK(CanDamage(g, r, a, other, kNow) == Verdict::OpenWorld);
    }
}

TEST(a_piece_must_sit_wholly_inside_a_plot_and_the_roads_are_closed)
{
    Grid const g = TestGrid();
    Registry r = Owned();
    Actor const owner = Actor::Player(70, 7);
    // A wall (3 wide along y, its half thickness along x) in from the edge fits; one over the
    // margin, or over the line into plot 2, doesn't.
    CHECK(Fits(g, 1, At(PieceType::WoodWall, 1020.f, 2020.f)));
    CHECK(Fits(g, 1, At(PieceType::WoodWall, 1038.85f, 2020.f)));
    CHECK(!Fits(g, 1, At(PieceType::WoodWall, 1039.f, 2020.f)));
    CHECK(!Fits(g, 1, At(PieceType::WoodWall, 1020.f, 2001.f)));
    CHECK(!Fits(g, 2, At(PieceType::WoodWall, 1020.f, 2020.f)));
    CHECK(CanBuild(g, r, owner, At(PieceType::WoodWall, 1039.5f, 2020.f), kNow) == Verdict::Denied);
    // Leaning in from outside the district is judged by the plot rules too.
    CHECK(CanBuild(g, r, owner, At(PieceType::WoodWall, 1020.f, 1999.f, 0, 0.f), kNow) == Verdict::Denied);
    // A plot for sale: nobody but a game master builds.
    Piece const onTwo = At(PieceType::WoodFloor, 1060.f, 2020.f);
    CHECK(CanBuild(g, r, owner, onTwo, kNow) == Verdict::Denied);
    CHECK(CanBuild(g, r, Actor::Player(90, 11, true), onTwo, kNow) == Verdict::Allowed);
    CHECK(CanDamage(g, r, owner, onTwo, kNow) == Verdict::Denied);
    // Overdue: what stands is safe and the owner may still take it down or open its doors, but
    // nothing new goes up.
    int64_t const overdue = kNow + kWeek + 60;
    Piece const mine = At(PieceType::WoodWall, 1020.f, 2020.f, 70);
    CHECK(CanBuild(g, r, owner, mine, overdue) == Verdict::Denied);
    CHECK(CanRemove(g, r, owner, mine, overdue) == Verdict::Allowed);
    CHECK(CanUseDoor(g, r, Actor::Player(72, 10), At(PieceType::WoodDoor, 1020.f, 2030.f), overdue) == Verdict::Allowed);
    CHECK(CanDamage(g, r, Actor::Player(80, 8), mine, overdue) == Verdict::Denied);
    // Lapsed (before the sweep packs it): nobody but a game master.
    int64_t const lapsed = kNow + kWeek * (1 + kGraceWeeks);
    CHECK(CanRemove(g, r, owner, mine, lapsed) == Verdict::Denied);
    CHECK(CanUseDoor(g, r, owner, mine, lapsed) == Verdict::Denied);
}

TEST(raids_are_never_called_on_or_near_a_district)
{
    Grid const g = TestGrid();
    CHECK(!RaidsAllowedAt(g, 1, 1020.f, 2020.f));
    CHECK(!RaidsAllowedAt(g, 1, 1060.f, 2060.f));      // a plot for sale too
    CHECK(!RaidsAllowedAt(g, 1, 1000.f - kRaidBuffer + 1.f, 2020.f));
    CHECK(RaidsAllowedAt(g, 1, 1000.f - kRaidBuffer - 1.f, 2020.f));
    CHECK(RaidsAllowedAt(g, 0, 1020.f, 2020.f));
    CHECK(!RaidsAllowedAt(Default(), 1, 5600.f, -3700.f));
}

TEST(the_plot_wire_tells_the_client_where_it_stands)
{
    Grid const g = TestGrid();
    Registry const r = Owned();
    std::vector<uint8_t> const b = EncodePlot(g, r, 1, Actor::Player(71, 9), kNow);
    CHECK(b.size() == 1 + 4 + 3 + 16 + 4 + 1);
    CHECK(b[0] == kOpPlot && b[1] == 1 && b[2] == 0);
    CHECK(b[5] == uint8_t(Size::Standard) && b[6] == uint8_t(Standing::Paid) && b[7] == uint8_t(Access::Builder));
    float x1 = 0.f;
    std::memcpy(&x1, &b[16], 4);
    CHECK(x1 == 1040.f);
    uint32_t until = uint32_t(b[24]) | uint32_t(b[25]) << 8 | uint32_t(b[26]) << 16 | uint32_t(b[27]) << 24;
    CHECK(until == uint32_t(kNow + kWeek));
    CHECK(b[28] == 2);
    // For sale, and nowhere.
    std::vector<uint8_t> const sale = EncodePlot(g, r, 2, Actor::Player(80, 8), kNow);
    CHECK(sale.size() == b.size() && sale[6] == uint8_t(Standing::ForSale) && sale[7] == uint8_t(Access::None));
    std::vector<uint8_t> const none = EncodePlot(g, r, 0, Actor::Player(80, 8), kNow);
    CHECK(none.size() == b.size() && none[1] == 0);
}
