/*
 * Azeroth Warfare: tests for decay and repair (Maintenance.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Maintenance.h"

using namespace Fusion;
using namespace Fusion::Maintenance;
using Build::PieceType;

namespace
{
    Build::Piece Wall(float health)
    {
        Build::Piece p;
        p.id = 42;
        p.type = PieceType::WoodWall;
        p.health = health;
        return p;
    }

    uint32_t CountOf(std::vector<Construction::Material> const& list, uint32_t item)
    {
        uint32_t n = 0;
        for (Construction::Material const& m : list)
            if (m.item == item)
                n += m.count;
        return n;
    }
}

TEST(maintenance_decay_is_two_percent_a_day)
{
    float const max = 600.f;
    CHECK_NEAR(DecayLoss(max, 1000, 1000 + kDay, 0), 12.f, 0.01f);
    CHECK_NEAR(DecayLoss(max, 1000, 1000 + kDay / 24, 0), 0.5f, 0.001f);
    Decayed const d = Decay(max, max, 1000, 1000 + 10 * kDay, 0);
    CHECK(d.changed && !d.collapsed);
    CHECK_NEAR(d.health, max * 0.8f, 0.01f);
    CHECK(d.decayedAt == 1000 + 10 * kDay);
    // A whole piece left alone falls in 50 days.
    CHECK_NEAR(DaysLeft(max, max), 50.f, 0.001f);
    CHECK(!Decay(max, max, 1000, 1000 + 49 * kDay, 0).collapsed);
    CHECK(Decay(max, max, 1000, 1000 + 50 * kDay, 0).collapsed);
    // In hourly steps or at once, the same.
    float h = max;
    int64_t at = 1000;
    for (int i = 0; i < 24 * 7; ++i)
    {
        Decayed const s = Decay(h, max, at, at + kDecaySweepSeconds, 0);
        h = s.health;
        at = s.decayedAt;
    }
    CHECK_NEAR(h, Decay(max, max, 1000, 1000 + 7 * kDay, 0).health, 0.05f);
}

TEST(maintenance_long_gaps_and_odd_clocks)
{
    float const max = 3600.f;   // a fortified wall's worth
    // A year offline: nothing left, never below 0, and it collapses.
    Decayed const y = Decay(max, max, 1000, 1000 + 365 * kDay, 0);
    CHECK(y.collapsed && y.health == 0.f);
    // A gap of decades is no overflow.
    CHECK(Decay(max, max, 1, int64_t(40) * 365 * kDay, 0).collapsed);
    // Half health: gone after 25 days, not after 24.
    CHECK(!Decay(max / 2, max, 1000, 1000 + 24 * kDay, 0).collapsed);
    CHECK(Decay(max / 2, max, 1000, 1000 + 25 * kDay, 0).collapsed);
    // A piece from before decay starts its clock and loses nothing.
    Decayed const fresh = Decay(100.f, max, 0, 5000, 0);
    CHECK(!fresh.changed && fresh.health == 100.f && fresh.decayedAt == 5000);
    // A clock set back takes nothing and keeps the time.
    Decayed const back = Decay(100.f, max, 5000, 4000, 0);
    CHECK(!back.changed && back.health == 100.f && back.decayedAt == 5000);
}

TEST(maintenance_a_paid_plot_stops_decay)
{
    float const max = 600.f;
    int64_t const t0 = 100000;
    // Paid for the whole gap: nothing.
    Decayed const kept = Decay(max, max, t0, t0 + 30 * kDay, t0 + 60 * kDay);
    CHECK(!kept.changed && kept.health == max);
    CHECK(kept.decayedAt == t0 + 30 * kDay);
    // Paid for 10 of 30 days, then overdue: 20 days' decay.
    Decayed const part = Decay(max, max, t0, t0 + 30 * kDay, t0 + 10 * kDay);
    CHECK_NEAR(part.health, max * 0.6f, 0.01f);
    CHECK(DecayingSeconds(t0, t0 + 30 * kDay, t0 + 10 * kDay) == 20 * kDay);
    // Paid only up to before the gap: the open world's pace.
    CHECK(DecayingSeconds(t0, t0 + kDay, t0 - kDay) == kDay);
    CHECK(DecayingSeconds(t0, t0 + kDay, 0) == kDay);
    // A long offline stretch on a plot whose upkeep ran out part way: only the unpaid part counts.
    Decayed const lapsed = Decay(max, max, t0, t0 + 200 * kDay, t0 + 160 * kDay);
    CHECK_NEAR(lapsed.health, max * 0.2f, 0.01f);
    CHECK(!lapsed.collapsed);
}

TEST(maintenance_repair_mends_a_step_and_costs_its_share)
{
    using namespace Construction;
    Build::Piece p = Wall(0.f);
    float const max = Build::MaxHealth(p);
    p.health = max * 0.5f;
    Recipe const* r = RecipeFor(PieceType::WoodWall);
    CHECK(r != nullptr);
    uint32_t const lumber = CountOf(r->materials, kRoughLumber);
    Inventory bags{{kRoughLumber, 100}, {kCopperNails, 100}};
    RepairStep const s = PlanRepair(p, Credit{}, bags, false, 1000, 0);
    CHECK(s.check == RepairCheck::Ok);
    CHECK_NEAR(s.gain, max * kRepairTick, 0.01f);
    // A tenth of 60% of the lumber, rounded up, the rest credited.
    float const share = float(lumber) * kRepairShare * kRepairTick;
    CHECK(CountOf(s.cost, kRoughLumber) == uint32_t(std::ceil(share - 1e-4f)));
    CHECK(s.payment.ok);
    // Strike after strike to full: what is paid is the share of the half mended, at most one
    // item over per material.
    Build::Piece q = p;
    Credit c;
    uint32_t paidLumber = 0, paidNails = 0;
    int strikes = 0;
    for (; strikes < 20; ++strikes)
    {
        RepairStep const k = PlanRepair(q, c, bags, false, 1000, 0);
        if (k.check != RepairCheck::Ok)
            break;
        q.health += k.gain;
        c = k.credit;
        paidLumber += CountOf(k.cost, kRoughLumber);
        paidNails += CountOf(k.cost, kCopperNails);
    }
    CHECK(strikes == 5);
    CHECK_NEAR(q.health, max, 0.01f);
    CHECK(PlanRepair(q, c, bags, false, 1000, 0).check == RepairCheck::NotDamaged);
    float const wantLumber = float(lumber) * kRepairShare * 0.5f;
    CHECK(float(paidLumber) >= wantLumber - 1e-3f && float(paidLumber) < wantLumber + 1.f);
    float const wantNails = float(CountOf(r->materials, kCopperNails)) * kRepairShare * 0.5f;
    CHECK(float(paidNails) >= wantNails - 1e-3f && float(paidNails) < wantNails + 1.f);
    // A full repair costs less than building anew, and more than taking it down gives back.
    CHECK(kRepairShare < 1.f && kRepairShare > kRefundShare);
}

TEST(maintenance_repair_refusals)
{
    using namespace Construction;
    Build::Piece p = Wall(100.f);
    Inventory none;
    // Nothing in the bags.
    CHECK(PlanRepair(p, Credit{}, none, false, 1000, 0).check == RepairCheck::NoMaterials);
    // Salvage pays too.
    Inventory salvage{{SalvagedOf(kRoughLumber), 50}, {SalvagedOf(kCopperNails), 50}};
    RepairStep const s = PlanRepair(p, Credit{}, salvage, false, 1000, 0);
    CHECK(s.check == RepairCheck::Ok && s.payment.usedSalvage);
    // Hit within 30 s: not yet.
    CHECK(PlanRepair(p, Credit{}, salvage, false, 1000, 990).check == RepairCheck::RecentlyDamaged);
    CHECK(PlanRepair(p, Credit{}, salvage, false, 1000, 1000 - kRepairAfterDamage).check == RepairCheck::Ok);
    // A game master pays nothing.
    RepairStep const gm = PlanRepair(p, Credit{}, none, true, 1000, 0);
    CHECK(gm.check == RepairCheck::Ok && gm.cost.empty());
    // Whole: nothing to do.
    Build::Piece whole = Wall(Build::MaxHealth(p));
    CHECK(PlanRepair(whole, Credit{}, salvage, false, 1000, 0).check == RepairCheck::NotDamaged);
    // The killstreak's sentry has no recipe: no repair.
    Build::Piece sentry = p;
    sentry.type = PieceType::SentryGun;
    CHECK(PlanRepair(sentry, Credit{}, salvage, false, 1000, 0).check == RepairCheck::NotRepairable);
}

TEST(maintenance_repair_credit_is_per_piece_and_reinforcement_costs_too)
{
    using namespace Construction;
    Build::Piece p = Wall(10.f);
    Inventory bags{{kRoughLumber, 100}, {kCopperNails, 100}};
    RepairStep const a = PlanRepair(p, Credit{}, bags, false, 1000, 0);
    CHECK(a.credit.piece == p.id);
    // Another piece drops the credit: paid again in full, never less.
    Build::Piece other = p;
    other.id = 43;
    RepairStep const b = PlanRepair(other, a.credit, bags, false, 1000, 0);
    CHECK(CountOf(b.cost, kRoughLumber) == CountOf(a.cost, kRoughLumber));
    CHECK(b.credit.piece == 43);
    // A failed strike keeps the credit it came with.
    RepairStep const broke = PlanRepair(p, a.credit, Inventory{}, false, 1000, 0);
    CHECK(broke.check == RepairCheck::NoMaterials && broke.credit.piece == a.credit.piece);
    // A reinforced stone wall's repair asks its reinforcement's goods.
    bool asksPlate = false;
    for (Owed const& o : RepairBasis(PieceType::StoneWall, 3))
        if (o.item == kFortifiedPlate && o.count > 0.f)
            asksPlate = true;
    CHECK(asksPlate);
    CHECK(RepairBasis(PieceType::SentryGun, 0).empty());
}
