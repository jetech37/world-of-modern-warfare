/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Plots.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace Fusion { namespace Plots
{
    namespace
    {
        // Every corner of a piece's boxes in the world, and its middle.
        std::vector<Vec3> Corners(Build::Piece const& p)
        {
            std::vector<Vec3> out;
            for (Build::Box const& b : Build::BoxesOf(p))
            {
                Build::WorldBox const w = Build::Place(p, b);
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2)
                        for (int sz = -1; sz <= 1; sz += 2)
                            out.push_back(w.center + w.axis[0] * (w.half.x * float(sx)) + w.axis[1] * (w.half.y * float(sy))
                                + w.axis[2] * (w.half.z * float(sz)));
            }
            return out;
        }

        // Whether any part of a piece reaches into a district: one standing outside with a corner
        // over the line is judged by the plot rules, so nothing leans into a plot from the road.
        bool Touches(Grid const& g, Build::Piece const& p)
        {
            Vec3 const c = Build::Center(p);
            if (g.InDistrict(p.map, c.x, c.y))
                return true;
            for (Vec3 const& v : Corners(p))
                if (g.InDistrict(p.map, v.x, v.y))
                    return true;
            return false;
        }

        Plot const* PlotUnder(Grid const& g, Registry const& r, Build::Piece const& p)
        {
            uint32_t const id = PlotOf(g, p);
            return id ? r.Find(id) : nullptr;
        }

        void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
        void PutF(std::vector<uint8_t>& o, float f) { uint32_t v; std::memcpy(&v, &f, 4); Put32(o, v); }
    }

    uint32_t Grid::PlotAt(uint32_t map, float x, float y) const
    {
        for (District const& d : m_districts)
        {
            if (d.map != map || d.cell <= 0.f)
                continue;
            float const fc = std::floor((x - d.originX) / d.cell);
            float const fr = std::floor((y - d.originY) / d.cell);
            if (fc < 0.f || fr < 0.f || fc >= float(d.cols) || fr >= float(d.rows))
                continue;
            return d.firstId + uint32_t(fr) * d.cols + uint32_t(fc);
        }
        return 0;
    }

    District const* Grid::DistrictOf(uint32_t plotId) const
    {
        for (District const& d : m_districts)
            if (plotId >= d.firstId && plotId < d.firstId + d.Count())
                return &d;
        return nullptr;
    }

    std::optional<Rect> Grid::Bounds(uint32_t plotId) const
    {
        District const* d = DistrictOf(plotId);
        if (!d)
            return std::nullopt;
        uint32_t const k = plotId - d->firstId;
        float const x0 = d->originX + float(k % d->cols) * d->cell;
        float const y0 = d->originY + float(k / d->cols) * d->cell;
        return Rect{x0, y0, x0 + d->cell, y0 + d->cell};
    }

    Rect Grid::DistrictBounds(District const& d) const
    {
        return {d.originX, d.originY, d.originX + float(d.cols) * d.cell, d.originY + float(d.rows) * d.cell};
    }

    bool Grid::InDistrict(uint32_t map, float x, float y, float margin) const
    {
        for (District const& d : m_districts)
            if (d.map == map && DistrictBounds(d).Inset(-margin).Contains(x, y))
                return true;
        return false;
    }

    uint32_t Grid::Count() const
    {
        uint32_t n = 0;
        for (District const& d : m_districts)
            n += d.Count();
        return n;
    }

    Site const* Grid::SiteById(uint8_t id) const
    {
        for (Site const& s : m_sites)
            if (s.id == id)
                return &s;
        return nullptr;
    }

    Site const* Grid::SiteOf(uint32_t plotId) const
    {
        District const* d = DistrictOf(plotId);
        return d ? SiteById(d->site) : nullptr;
    }

    Site const* Grid::SiteAt(uint32_t map, float x, float y, float margin) const
    {
        for (Site const& s : m_sites)
            if (s.map == map && s.bounds.Inset(-margin).Contains(x, y))
                return &s;
        return nullptr;
    }

    Spot NearestStop(Site const& s, float x, float y, float z)
    {
        auto dist = [&](Spot const& p) { return (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) + (p.z - z) * (p.z - z); };
        Spot best = s.gate;
        for (Stop const& st : s.stops)
            if (dist(st.at) < dist(best))
                best = st.at;
        return best;
    }

    std::optional<Spot> Grid::FellThrough(uint32_t map, float x, float y, float z) const
    {
        Site const* s = SiteAt(map, x, y);
        if (!s || z >= s->floorZ)
            return std::nullopt;
        return NearestStop(*s, x, y, s->floorZ);
    }

    std::vector<std::string> Grid::Problems() const
    {
        std::vector<std::string> out;
        auto name = [](District const& d) { return "district " + std::to_string(d.firstId); };
        for (size_t i = 0; i < m_districts.size(); ++i)
        {
            District const& a = m_districts[i];
            if (a.cell <= 0.f || a.cols == 0 || a.rows == 0)
                out.push_back(name(a) + " is empty");
            if (a.firstId == 0)
                out.push_back(name(a) + " starts at id 0");
            Site const* s = SiteById(a.site);
            if (!s)
                out.push_back(name(a) + " has no site");
            else if (s->map != a.map || !s->bounds.Holds(DistrictBounds(a)))
                out.push_back(name(a) + " lies outside its site, " + s->name);
            for (size_t j = i + 1; j < m_districts.size(); ++j)
            {
                District const& b = m_districts[j];
                if (a.map == b.map && DistrictBounds(a).Overlaps(DistrictBounds(b)))
                    out.push_back(name(a) + " overlaps " + name(b));
                if (!(a.firstId + a.Count() <= b.firstId || b.firstId + b.Count() <= a.firstId))
                    out.push_back(name(a) + " shares ids with " + name(b));
            }
        }
        for (size_t i = 0; i < m_sites.size(); ++i)
        {
            Site const& s = m_sites[i];
            if (s.bounds.x1 <= s.bounds.x0 || s.bounds.y1 <= s.bounds.y0)
                out.push_back(s.name + " has no bounds");
            if (s.ceilingZ <= s.floorZ)
                out.push_back(s.name + " has its ceiling below its floor");
            std::vector<Spot> spots{s.gate};
            for (Stop const& st : s.stops)
                spots.push_back(st.at);
            for (Spot const& p : spots)
            {
                if (PlotAt(s.map, p.x, p.y))
                    out.push_back(s.name + ": a courier stop stands on a plot");
                if (!s.bounds.Contains(p.x, p.y) || p.z < s.floorZ || p.z > s.ceilingZ)
                    out.push_back(s.name + ": a courier stop is outside its bounds");
            }
            for (size_t j = i + 1; j < m_sites.size(); ++j)
                if (m_sites[j].id == s.id)
                    out.push_back(s.name + " shares its id");
                else if (m_sites[j].map == s.map && m_sites[j].bounds.Overlaps(s.bounds))
                    out.push_back(s.name + " overlaps " + m_sites[j].name);
        }
        return out;
    }

    Grid const& Default()
    {
        // Prices by size, the same in every site.
        constexpr uint32_t kSmallPrice = 50000, kSmallUpkeep = 2000;
        constexpr uint32_t kStandardPrice = 250000, kStandardUpkeep = 10000;
        constexpr uint32_t kLargePrice = 1000000, kLargeUpkeep = 30000;
        static Grid const grid(
            {
                // The Hyjal summit, re-laid off the hillsides (WORLD_BUILDING.md §4.1): the small
                // plots on the flat east terrace, the standard ones in the gate pocket, the large
                // ones on the crater floor, 900 yards on and 140 down.
                {Size::Small, 1, 5520.f, -3450.f, 24.f, 8, 4, 1, kSmallPrice, kSmallUpkeep, Construction::kDeedSmall, 1},
                {Size::Standard, 1, 5400.f, -3790.f, 40.f, 6, 4, 101, kStandardPrice, kStandardUpkeep, Construction::kDeedStandard, 1},
                {Size::Large, 1, 5360.f, -2870.f, 64.f, 2, 4, 201, kLargePrice, kLargeUpkeep, Construction::kDeedLarge, 1},
                // Azshara Crater (§4.2): market lots on the central floor, the Crater Commons on
                // the north arm, the large plots in the south canyon by the old Horde start.
                {Size::Small, 37, 150.f, 0.f, 24.f, 8, 4, 301, kSmallPrice, kSmallUpkeep, Construction::kDeedCraterSmall, 2},
                {Size::Standard, 37, 75.f, 600.f, 40.f, 6, 4, 401, kStandardPrice, kStandardUpkeep, Construction::kDeedCraterStandard, 2},
                {Size::Large, 37, -675.f, -325.f, 64.f, 2, 4, 501, kLargePrice, kLargeUpkeep, Construction::kDeedCraterLarge, 2},
                // The Thalassian plateau (§4.3), dead flat at z 129: the standard town with the
                // small plots across a 10 yard road north of it and the large ones south.
                {Size::Small, 0, 3750.f, -4295.f, 24.f, 16, 4, 601, kSmallPrice, kSmallUpkeep, Construction::kDeedPlateauSmall, 3},
                {Size::Standard, 0, 3750.f, -4625.f, 40.f, 12, 8, 701, kStandardPrice, kStandardUpkeep, Construction::kDeedPlateauStandard, 3},
                {Size::Large, 0, 3750.f, -4891.f, 64.f, 4, 4, 801, kLargePrice, kLargeUpkeep, Construction::kDeedPlateauLarge, 3},
            },
            {
                // The rim box; the shaft by the east terrace bottoms out near z 1140 and the
                // slopes off the rim run down to Felwood and Winterspring at 400-900.
                {1, "the Hyjal summit", "on the Hyjal summit", 1, {5385.f, -3710.f, 1594.2f, 0.f},
                    {{"the crater floor", {5500.f, -2790.f, 1462.7f, 3.1f}}},
                    {5300.f, -3850.f, 5800.f, -2600.f}, 1420.f, 1700.f},
                // The crater's inside: outside the rim the map is a filler at z -29.6.
                {2, "Azshara Crater", "in Azshara Crater", 37, {128.f, 135.f, 236.f, 1.6f},
                    {{"the south canyon", {-530.f, -200.f, 321.8f, 3.6f}}},
                    {-800.f, -1100.f, 700.f, 1450.f}, 100.f, 600.f},
                // North and east the plateau drops to no-height ocean at z -500; the ridge to the
                // Eastern Plaguelands (217-245) lies south of x 3700.
                {3, "the Thalassian plateau", "on the Thalassian plateau", 0, {3738.f, -4300.f, 129.f, 0.f},
                    {},
                    {3720.f, -5400.f, 4800.f, -3950.f}, 90.f, 260.f},
            });
        return grid;
    }

    Standing StandingOf(Plot const& p, int64_t now)
    {
        if (!p.Owned())
            return Standing::ForSale;
        if (now < p.paidUntil)
            return Standing::Paid;
        if (now < p.paidUntil + int64_t(kGraceWeeks) * kWeek)
            return Standing::Overdue;
        return Standing::Lapsed;
    }

    Plot const* Registry::Find(uint32_t id) const
    {
        auto it = plots.find(id);
        return it == plots.end() ? nullptr : &it->second;
    }

    Plot* Registry::Find(uint32_t id)
    {
        auto it = plots.find(id);
        return it == plots.end() ? nullptr : &it->second;
    }

    uint32_t Registry::CountOwnedBy(uint32_t account) const
    {
        uint32_t n = 0;
        for (auto const& kv : plots)
            if (kv.second.Owned() && kv.second.ownerAccount == account)
                ++n;
        return n;
    }

    Plot const* Registry::OwnedBy(uint32_t account) const
    {
        for (auto const& kv : plots)
            if (kv.second.Owned() && kv.second.ownerAccount == account)
                return &kv.second;
        return nullptr;
    }

    std::vector<uint32_t> Registry::Lapse(int64_t now)
    {
        std::vector<uint32_t> out;
        for (auto it = plots.begin(); it != plots.end();)
        {
            if (StandingOf(it->second, now) == Standing::Lapsed || !it->second.Owned())
            {
                out.push_back(it->first);
                it = plots.erase(it);
            }
            else
                ++it;
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool Grant(Plot& p, uint32_t guid, PermitLevel level)
    {
        if (guid == 0 || guid == p.ownerGuid || level == PermitLevel::None)
            return false;
        for (Permit& q : p.permits)
            if (q.guid == guid)
            {
                q.level = level;
                return true;
            }
        if (p.permits.size() >= kMaxPermits)
            return false;
        p.permits.push_back({guid, level});
        return true;
    }

    bool Revoke(Plot& p, uint32_t guid)
    {
        auto it = std::remove_if(p.permits.begin(), p.permits.end(), [guid](Permit const& q) { return q.guid == guid; });
        bool const found = it != p.permits.end();
        p.permits.erase(it, p.permits.end());
        return found;
    }

    PermitLevel PermitOf(Plot const& p, uint32_t guid)
    {
        for (Permit const& q : p.permits)
            if (q.guid == guid)
                return q.level;
        return PermitLevel::None;
    }

    BuyResult CanBuy(Grid const& g, Registry const& r, uint32_t plotId, Buyer const& b)
    {
        District const* d = g.DistrictOf(plotId);
        if (!d)
            return BuyResult::NoSuchPlot;
        // A lapsed plot is still taken until its pieces are packed (Registry::Lapse).
        if (Plot const* p = r.Find(plotId); p && p->Owned())
            return BuyResult::Taken;
        if (r.CountOwnedBy(b.account) >= kMaxPlotsPerAccount)
            return BuyResult::LimitReached;
        if (b.money < d->priceCopper)
            return BuyResult::NotEnoughMoney;
        return BuyResult::Ok;
    }

    BuyResult Buy(Grid const& g, Registry& r, uint32_t plotId, Buyer const& b, int64_t now, uint32_t& charge)
    {
        charge = 0;
        BuyResult const res = CanBuy(g, r, plotId, b);
        if (res != BuyResult::Ok)
            return res;
        Plot p;
        p.id = plotId;
        p.ownerAccount = b.account;
        p.ownerGuid = b.guid;
        p.boughtAt = now;
        p.paidUntil = now + int64_t(kIncludedWeeks) * kWeek;
        r.plots[plotId] = p;
        charge = g.DistrictOf(plotId)->priceCopper;
        return BuyResult::Ok;
    }

    PayResult PayUpkeep(Grid const& g, Plot& p, uint32_t account, uint32_t weeks, uint32_t money, int64_t now, uint32_t& charge)
    {
        charge = 0;
        District const* d = g.DistrictOf(p.id);
        if (!d || !p.Owned() || p.ownerAccount != account)
            return PayResult::NotOwner;
        if (StandingOf(p, now) == Standing::Lapsed)
            return PayResult::Lapsed;
        if (weeks == 0)
            return PayResult::NoWeeks;
        int64_t const until = p.paidUntil + int64_t(weeks) * kWeek;
        if (until > now + int64_t(kMaxPrepaidWeeks) * kWeek)
            return PayResult::TooFarAhead;
        uint64_t const cost = uint64_t(weeks) * d->upkeepCopper;
        if (cost > money)
            return PayResult::NotEnoughMoney;
        p.paidUntil = until;
        charge = uint32_t(cost);
        return PayResult::Ok;
    }

    bool Abandon(Registry& r, uint32_t plotId, uint32_t account)
    {
        Plot const* p = r.Find(plotId);
        if (!p || p->ownerAccount != account)
            return false;
        r.plots.erase(plotId);
        return true;
    }

    uint32_t PlotOf(Grid const& g, Build::Piece const& p)
    {
        Vec3 const c = Build::Center(p);
        return g.PlotAt(p.map, c.x, c.y);
    }

    bool Fits(Grid const& g, uint32_t plotId, Build::Piece const& p)
    {
        std::optional<Rect> const b = g.Bounds(plotId);
        District const* d = g.DistrictOf(plotId);
        if (!b || !d || d->map != p.map)
            return false;
        Rect const inner = b->Inset(kBuildMargin);
        for (Vec3 const& v : Corners(p))
            // The far edges count as inside here: a wall flush on the margin line fits.
            if (v.x < inner.x0 - 1e-3f || v.x > inner.x1 + 1e-3f || v.y < inner.y0 - 1e-3f || v.y > inner.y1 + 1e-3f)
                return false;
        return true;
    }

    std::vector<Reclaim> Pack(std::vector<Build::Piece> const& pieces)
    {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> sum;     // (guid, item) -> count
        for (Build::Piece const& p : pieces)
        {
            // As taking it down gives back (its reinforcement's share too, of what was paid for),
            // salvaged: a plot released and claimed again is no way to level Construction nor to
            // make anything.
            for (Construction::Material const& m : Construction::AsSalvaged(Construction::Refund(p)))
                sum[{p.owner, m.item}] += m.count;
        }
        std::vector<Reclaim> out;
        for (auto const& kv : sum)
            out.push_back({kv.first.first, kv.first.second, kv.second});
        return out;
    }

    Access AccessOf(Actor const& a, Plot const* p, int64_t now)
    {
        if (a.kind != Actor::Kind::Player)
            return Access::None;
        if (a.gm)
            return Access::Gm;
        if (!p || !p->Owned() || StandingOf(*p, now) == Standing::Lapsed)
            return Access::None;
        if (a.account == p->ownerAccount)
            return Access::Owner;
        switch (PermitOf(*p, a.guid))
        {
            case PermitLevel::Builder: return Access::Builder;
            case PermitLevel::Guest: return Access::Guest;
            default: return Access::None;
        }
    }

    Verdict CanBuild(Grid const& g, Registry const& r, Actor const& a, Build::Piece const& piece, int64_t now)
    {
        if (!Touches(g, piece))
            return Verdict::OpenWorld;
        if (a.kind == Actor::Kind::Player && a.gm)
            return Verdict::Allowed;
        uint32_t const id = PlotOf(g, piece);
        if (!id || !Fits(g, id, piece))
            return Verdict::Denied;
        Plot const* p = r.Find(id);
        Access const access = AccessOf(a, p, now);
        if (access != Access::Owner && access != Access::Builder)
            return Verdict::Denied;
        return StandingOf(*p, now) == Standing::Paid ? Verdict::Allowed : Verdict::Denied;
    }

    Verdict CanRemove(Grid const& g, Registry const& r, Actor const& a, Build::Piece const& piece, int64_t now)
    {
        if (!Touches(g, piece))
            return Verdict::OpenWorld;
        switch (AccessOf(a, PlotUnder(g, r, piece), now))
        {
            case Access::Gm:
            case Access::Owner:
                return Verdict::Allowed;
            case Access::Builder:
                return piece.owner == a.guid ? Verdict::Allowed : Verdict::Denied;
            default:
                return Verdict::Denied;
        }
    }

    Verdict CanDamage(Grid const& g, Registry const& r, Actor const& attacker, Build::Piece const& piece, int64_t now)
    {
        if (!Touches(g, piece))
            return Verdict::OpenWorld;
        // A builder breaks only what they built, as they take down only what they built: the
        // owner's walls are not theirs to shoot for salvage.
        switch (AccessOf(attacker, PlotUnder(g, r, piece), now))
        {
            case Access::Gm:
            case Access::Owner:
                return Verdict::Allowed;
            case Access::Builder:
                return piece.owner == attacker.guid ? Verdict::Allowed : Verdict::Denied;
            default:
                return Verdict::Denied;
        }
    }

    bool SalvageToBuilder(Grid const& g, Registry const& r, Actor const& breaker, Build::Piece const& piece, int64_t now)
    {
        if (breaker.kind != Actor::Kind::Player || !piece.owner || piece.owner == breaker.guid || !Touches(g, piece))
            return false;
        Access const a = AccessOf(breaker, PlotUnder(g, r, piece), now);
        return a == Access::Owner || a == Access::Gm || a == Access::Builder;
    }

    Verdict CanUseDoor(Grid const& g, Registry const& r, Actor const& a, Build::Piece const& door, int64_t now)
    {
        if (!Touches(g, door))
            return Verdict::OpenWorld;
        return AccessOf(a, PlotUnder(g, r, door), now) != Access::None ? Verdict::Allowed : Verdict::Denied;
    }

    bool RaidsAllowedAt(Grid const& g, uint32_t map, float x, float y)
    {
        return !g.InDistrict(map, x, y, kRaidBuffer) && !g.SiteAt(map, x, y);
    }

    std::vector<uint8_t> EncodePlot(Grid const& g, Registry const& r, uint32_t plotId, Actor const& viewer, int64_t now)
    {
        std::vector<uint8_t> o{kOpPlot};
        District const* d = g.DistrictOf(plotId);
        std::optional<Rect> const b = g.Bounds(plotId);
        if (!d || !b)
        {
            Put32(o, 0);
            o.insert(o.end(), {0, 0, 0});
            for (int i = 0; i < 4; ++i)
                PutF(o, 0.f);
            Put32(o, 0);
            o.push_back(0);
            return o;
        }
        Plot const* p = r.Find(plotId);
        Put32(o, plotId);
        o.push_back(uint8_t(d->size));
        o.push_back(uint8_t(p ? StandingOf(*p, now) : Standing::ForSale));
        o.push_back(uint8_t(AccessOf(viewer, p, now)));
        PutF(o, b->x0);
        PutF(o, b->y0);
        PutF(o, b->x1);
        PutF(o, b->y1);
        Put32(o, p ? uint32_t(std::max<int64_t>(0, std::min<int64_t>(p->paidUntil, 0xFFFFFFFFll))) : 0);
        o.push_back(uint8_t(p ? std::min<size_t>(p->permits.size(), 255) : 0));
        return o;
    }
}}
