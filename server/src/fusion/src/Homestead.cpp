/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Homestead.h"
#include "fusion/Portals.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace Fusion { namespace Homestead
{
    namespace
    {
        void Put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
        void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
        void PutF(std::vector<uint8_t>& o, float f) { uint32_t v; std::memcpy(&v, &f, 4); Put32(o, v); }
        void PutStr(std::vector<uint8_t>& o, std::string const& s)
        {
            size_t const n = std::min<size_t>(s.size(), 255);
            o.push_back(uint8_t(n));
            o.insert(o.end(), s.begin(), s.begin() + n);
        }

        struct Reader
        {
            std::vector<uint8_t> const& b;
            size_t at = 0;
            bool ok = true;

            bool Need(size_t n)
            {
                if (at + n > b.size())
                    ok = false;
                return ok;
            }
            uint8_t U8() { return Need(1) ? b[at++] : 0; }
            uint16_t U16()
            {
                if (!Need(2))
                    return 0;
                uint16_t const v = uint16_t(b[at] | b[at + 1] << 8);
                at += 2;
                return v;
            }
            uint32_t U32()
            {
                if (!Need(4))
                    return 0;
                uint32_t const v = uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
                at += 4;
                return v;
            }
            float F()
            {
                uint32_t const v = U32();
                float f;
                std::memcpy(&f, &v, 4);
                return f;
            }
            std::string Str()
            {
                size_t const n = U8();
                if (!Need(n))
                    return std::string();
                std::string s(b.begin() + at, b.begin() + at + n);
                at += n;
                return s;
            }
        };

        char const* SizeWord(Plots::Size s)
        {
            switch (s)
            {
                case Plots::Size::Small: return "Small homestead";
                case Plots::Size::Large: return "Large homestead";
                default: return "Homestead";
            }
        }

        uint32_t FreeIn(Plots::District const& d, Plots::Registry const& r)
        {
            uint32_t n = 0;
            for (uint32_t id = d.firstId; id < d.firstId + d.Count(); ++id)
                if (!r.Find(id))
                    ++n;
            return n;
        }
    }

    Npc NeededFor(Action a)
    {
        switch (a)
        {
            case Action::Claim:
            case Action::ClaimVisited:
            case Action::Release:
            case Action::Teleport:
            case Action::Pay:
            case Action::Collect:
            case Action::Permit:
            case Action::Revoke:
            case Action::BuyDecor:
                return Npc::Steward;
            case Action::Train:
            case Action::Craft:
                return Npc::Trainer;
            case Action::Travel:
                return Npc::Courier;
            default:
                return Npc::None;
        }
    }

    bool AllowedAt(Action a, Npc at)
    {
        Npc const need = NeededFor(a);
        return a != Action::None && (need == Npc::None || need == at);
    }

    std::vector<uint8_t> EncodeWindow(Window const& w)
    {
        std::vector<uint8_t> o{kOpWindow, uint8_t(w.open), uint8_t(w.npc)};
        PutStr(o, w.npcName);
        o.push_back(uint8_t(w.tone));
        PutStr(o, w.message);
        PlotView const& p = w.plot;
        Put32(o, p.id);
        o.push_back(p.size);
        o.push_back(p.standing);
        Put16(o, p.map);
        PutF(o, p.bounds.x0);
        PutF(o, p.bounds.y0);
        PutF(o, p.bounds.x1);
        PutF(o, p.bounds.y1);
        Put32(o, p.paidUntil);
        Put32(o, p.upkeep);
        o.push_back(p.permits);
        Put16(o, p.pieces);
        PutStr(o, p.place);
        Put32(o, w.reclaim);
        size_t const n = std::min(w.services.size(), kMaxServices);
        o.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Service const& s = w.services[i];
            o.push_back(uint8_t(s.action));
            Put32(o, s.arg);
            Put32(o, s.cost);
            o.push_back(uint8_t((s.enabled ? 1 : 0) | (s.confirm ? 2 : 0)));
            o.push_back(uint8_t(s.hue));
            PutStr(o, s.label);
            PutStr(o, s.note);
        }
        return o;
    }

    bool DecodeWindow(std::vector<uint8_t> const& body, Window& out)
    {
        Reader r{body};
        if (r.U8() != kOpWindow)
            return false;
        Window w;
        w.open = Open(r.U8());
        w.npc = Npc(r.U8());
        w.npcName = r.Str();
        w.tone = Tone(r.U8());
        w.message = r.Str();
        PlotView& p = w.plot;
        p.id = r.U32();
        p.size = r.U8();
        p.standing = r.U8();
        p.map = r.U16();
        p.bounds.x0 = r.F();
        p.bounds.y0 = r.F();
        p.bounds.x1 = r.F();
        p.bounds.y1 = r.F();
        p.paidUntil = r.U32();
        p.upkeep = r.U32();
        p.permits = r.U8();
        p.pieces = r.U16();
        p.place = r.Str();
        w.reclaim = r.U32();
        size_t const n = r.U8();
        for (size_t i = 0; i < n && r.ok; ++i)
        {
            Service s;
            s.action = Action(r.U8());
            s.arg = r.U32();
            s.cost = r.U32();
            uint8_t const flags = r.U8();
            s.enabled = flags & 1;
            s.confirm = (flags & 2) != 0;
            s.hue = Hue(r.U8());
            s.label = r.Str();
            s.note = r.Str();
            w.services.push_back(std::move(s));
        }
        if (!r.ok || r.at != body.size())
            return false;
        out = std::move(w);
        return true;
    }

    std::vector<uint8_t> EncodeAct(Action a, uint32_t arg)
    {
        std::vector<uint8_t> o{kOpAct, uint8_t(a)};
        Put32(o, arg);
        return o;
    }

    bool DecodeAct(std::vector<uint8_t> const& body, Action& a, uint32_t& arg)
    {
        if (body.size() != 6 || body[0] != kOpAct)
            return false;
        uint8_t const v = body[1];
        if (v < uint8_t(Action::Claim) || v > uint8_t(Action::Revoke))
            return false;
        a = Action(v);
        arg = uint32_t(body[2]) | uint32_t(body[3]) << 8 | uint32_t(body[4]) << 16 | uint32_t(body[5]) << 24;
        return true;
    }

    std::string MoneyText(uint32_t copper)
    {
        std::string o;
        auto add = [&](uint32_t v, char const* unit) {
            if (!o.empty())
                o += ' ';
            o += std::to_string(v) + unit;
        };
        if (copper >= 10000)
            add(copper / 10000, "g");
        if (copper % 10000 >= 100)
            add((copper % 10000) / 100, "s");
        if (copper % 100 || copper == 0)
            add(copper % 100, "c");
        return o;
    }

    uint32_t FirstFree(Plots::Grid const& g, Plots::Registry const& r, size_t index, std::vector<uint32_t> const& busy)
    {
        if (index >= g.Districts().size())
            return 0;
        Plots::District const& d = g.Districts()[index];
        for (uint32_t id = d.firstId; id < d.firstId + d.Count(); ++id)
            if (!r.Find(id) && std::find(busy.begin(), busy.end(), id) == busy.end())
                return id;
        return 0;
    }

    Plots::BuyResult ClaimInDistrict(Plots::Grid const& g, Plots::Registry& r, size_t index, uint8_t site,
        Plots::Buyer const& b, int64_t now, uint32_t& charge, uint32_t& plotId, std::vector<uint32_t> const& busy)
    {
        charge = 0;
        plotId = 0;
        if (r.CountOwnedBy(b.account) >= Plots::kMaxPlotsPerAccount)
            return Plots::BuyResult::LimitReached;
        if (index >= g.Districts().size() || (site && g.Districts()[index].site != site))
            return Plots::BuyResult::NoSuchPlot;
        uint32_t const id = FirstFree(g, r, index, busy);
        if (!id)
            return Plots::BuyResult::Taken;
        Plots::BuyResult const res = Plots::Buy(g, r, id, b, now, charge);
        if (res == Plots::BuyResult::Ok)
            plotId = id;
        return res;
    }

    Plots::BuyResult ClaimPlot(Plots::Grid const& g, Plots::Registry& r, uint32_t plotId, Plots::Buyer const& b,
        int64_t now, uint32_t& charge)
    {
        charge = 0;
        if (r.CountOwnedBy(b.account) >= Plots::kMaxPlotsPerAccount)
            return Plots::BuyResult::LimitReached;
        return Plots::Buy(g, r, plotId, b, now, charge);
    }

    std::vector<uint32_t> Excess(Plots::Registry const& r)
    {
        // Each account's plots, earliest bought first (by id when bought together).
        std::map<uint32_t, std::vector<Plots::Plot const*>> by;
        for (auto const& kv : r.plots)
            if (kv.second.Owned())
                by[kv.second.ownerAccount].push_back(&kv.second);
        std::vector<uint32_t> out;
        for (auto& kv : by)
        {
            auto& v = kv.second;
            std::sort(v.begin(), v.end(), [](Plots::Plot const* a, Plots::Plot const* b) {
                return a->boughtAt != b->boughtAt ? a->boughtAt < b->boughtAt : a->id < b->id;
            });
            for (size_t i = Plots::kMaxPlotsPerAccount; i < v.size(); ++i)
                out.push_back(v[i]->id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    Plots::Spot Arrival(Plots::Grid const& g, uint32_t plotId)
    {
        std::optional<Plots::Rect> const b = g.Bounds(plotId);
        if (!b)
            return {};
        float const cx = (b->x0 + b->x1) / 2.f, cy = (b->y0 + b->y1) / 2.f;
        Plots::Site const* s = g.SiteOf(plotId);
        // Without a site, the low-y edge.
        float const gx = s ? s->gate.x : cx, gy = s ? s->gate.y : b->y0 - 1.f;
        // The four edges' middles, just inside.
        float const in = kArrivalInset;
        Plots::Spot const edges[4] = {
            {cx, b->y0 + in, 0.f, 0.f},
            {cx, b->y1 - in, 0.f, 0.f},
            {b->x0 + in, cy, 0.f, 0.f},
            {b->x1 - in, cy, 0.f, 0.f},
        };
        Plots::Spot best = edges[0];
        float bestD = 1e30f;
        for (Plots::Spot const& e : edges)
        {
            float const d = (e.x - gx) * (e.x - gx) + (e.y - gy) * (e.y - gy);
            if (d < bestD)
            {
                bestD = d;
                best = e;
            }
        }
        best.z = s ? s->gate.z : 0.f;
        best.o = std::atan2(cy - best.y, cx - best.x);
        if (best.o < 0.f)
            best.o += 6.2831853f;
        return best;
    }

    std::vector<Service> StewardServices(Plots::Grid const& g, Plots::Registry const& r, StewardAsk const& ask, int64_t now)
    {
        std::vector<Service> out;
        auto name = [&](uint32_t guid) { return ask.nameOf ? ask.nameOf(guid) : "character " + std::to_string(guid); };
        Plots::Plot const* mine = r.OwnedBy(ask.account);
        if (!mine)
        {
            auto const& ds = g.Districts();
            for (size_t i = 0; i < ds.size(); ++i)
            {
                Plots::District const& d = ds[i];
                if (ask.site && d.site != ask.site)
                    continue;
                Service s;
                s.action = Action::Claim;
                s.arg = uint32_t(i);
                s.confirm = true;   // gold, and the one plot a player may hold
                s.cost = d.priceCopper;
                s.label = std::string("Claim a ") + SizeWord(d.size) + " (" + std::to_string(int(d.cell)) + " yards)";
                if (!ask.site)
                    if (Plots::Site const* where = g.SiteById(d.site))
                        s.label += " " + where->where;
                uint32_t const free = FreeIn(d, r);
                s.note = "then " + MoneyText(d.upkeepCopper) + " a week  -  " + std::to_string(free) + " of " + std::to_string(d.Count()) + " free";
                if (!free)
                {
                    s.enabled = false;
                    s.note = "Every one is taken";
                }
                else if (!ask.gm && ask.money < d.priceCopper)
                {
                    s.enabled = false;
                    s.note = "You need " + MoneyText(d.priceCopper) + "  -  then " + MoneyText(d.upkeepCopper) + " a week";
                }
                out.push_back(std::move(s));
            }
            if (ask.visited && !r.Find(ask.visited))
                if (Plots::District const* d = g.DistrictOf(ask.visited); d && (!ask.site || d->site == ask.site))
                {
                    Service s;
                    s.action = Action::ClaimVisited;
                    s.arg = ask.visited;
                    s.confirm = true;
                    s.cost = d->priceCopper;
                    s.label = "Claim plot " + std::to_string(ask.visited) + ", the one you last walked on";
                    s.note = std::string(SizeWord(d->size)) + ", " + std::to_string(int(d->cell)) + " yards  -  then " + MoneyText(d->upkeepCopper) + " a week";
                    if (!ask.gm && ask.money < d->priceCopper)
                    {
                        s.enabled = false;
                        s.note = "You need " + MoneyText(d->priceCopper);
                    }
                    out.push_back(std::move(s));
                }
        }
        else
        {
            Plots::District const* d = g.DistrictOf(mine->id);
            Plots::Standing const standing = Plots::StandingOf(*mine, now);
            uint32_t const upkeep = d ? d->upkeepCopper : 0;
            {
                Service s;
                s.action = Action::Teleport;
                s.label = "Take me to my homestead";
                s.note = "Plot " + std::to_string(mine->id);
                if (Plots::Site const* site = g.SiteOf(mine->id))
                    s.note += " " + site->where;
                out.push_back(std::move(s));
            }
            // Upkeep: a week, four, or as far ahead as allowed, from where the paid time ends.
            int64_t const most = std::max<int64_t>(0, (now + int64_t(Plots::kMaxPrepaidWeeks) * Plots::kWeek - mine->paidUntil) / Plots::kWeek);
            struct { uint32_t weeks; char const* label; } const pays[3] = {
                {1, "Pay a week of upkeep"}, {4, "Pay four weeks of upkeep"}, {0, "Pay upkeep as far ahead as allowed"}};
            for (auto const& p : pays)
            {
                Service s;
                s.action = Action::Pay;
                s.arg = p.weeks;
                uint32_t const weeks = p.weeks ? p.weeks : uint32_t(most);
                s.cost = weeks * upkeep;
                s.label = p.label;
                if (standing == Plots::Standing::Lapsed)
                {
                    s.enabled = false;
                    s.note = "It has lapsed";
                }
                else if (weeks == 0 || int64_t(weeks) > most)
                {
                    s.enabled = false;
                    s.note = "At most " + std::to_string(Plots::kMaxPrepaidWeeks) + " weeks may be paid ahead";
                }
                else
                {
                    s.note = std::to_string(weeks) + (weeks == 1 ? " week" : " weeks");
                    if (!ask.gm && ask.money < s.cost)
                    {
                        s.enabled = false;
                        s.note += "  -  you need " + MoneyText(s.cost);
                    }
                }
                out.push_back(std::move(s));
            }
            // Permits: for the player targeted, then each one given.
            if (ask.target && ask.target != ask.guid)
            {
                Plots::PermitLevel const has = Plots::PermitOf(*mine, ask.target);
                Service guest;
                guest.action = Action::Permit;
                guest.arg = uint32_t(Plots::PermitLevel::Guest);
                guest.label = "Let " + ask.targetName + " use your doors";
                guest.note = "A guest";
                guest.enabled = has != Plots::PermitLevel::Guest;
                out.push_back(guest);
                Service builder;
                builder.action = Action::Permit;
                builder.arg = uint32_t(Plots::PermitLevel::Builder);
                builder.label = "Let " + ask.targetName + " build here";
                builder.note = "A builder: builds, and takes down what they built";
                builder.enabled = has != Plots::PermitLevel::Builder;
                out.push_back(builder);
            }
            for (Plots::Permit const& q : mine->permits)
            {
                Service s;
                s.action = Action::Revoke;
                s.arg = q.guid;
                s.confirm = true;
                s.label = "Revoke " + name(q.guid) + "'s permit";
                s.note = q.level == Plots::PermitLevel::Builder ? "Builder" : "Guest";
                out.push_back(std::move(s));
            }
            Service release;
            release.action = Action::Release;
            release.confirm = true;
            release.label = "Release my homestead";
            release.note = "It goes back on sale at once, no refund; what stands on it comes down as if taken down (about half, salvaged) into each builder's stash here";
            out.push_back(std::move(release));
        }
        if (ask.reclaim)
        {
            Service s;
            s.action = Action::Collect;
            s.label = "Collect my packed belongings";
            s.note = std::to_string(ask.reclaim) + (ask.reclaim == 1 ? " item waits" : " items wait") + " here";
            out.push_back(std::move(s));
        }
        return out;
    }

    std::vector<Service> CourierServices(Plots::Grid const& g, Plots::Registry const& r, CourierAsk const& ask)
    {
        std::vector<Service> out;
        auto const& sites = g.Sites();
        Plots::Site const* here = g.SiteAt(ask.map, ask.x, ask.y, kHomesteadsNear);
        if (here)
        {
            Service back;
            back.action = Action::Travel;
            back.arg = kTravelBack;
            back.label = "Take me back";
            back.note = ask.canGoBack ? "Where you came from" : "To your hearthstone's inn";
            // It shares the hearthstone's cooldown: no free teleport home.
            if (ask.backCooldownMs)
            {
                back.enabled = false;
                back.note = "Shares your hearthstone's cooldown: ready in " + Portals::WaitText(ask.backCooldownMs);
            }
            else
                back.note += " (starts your hearthstone's cooldown)";
            out.push_back(back);
            size_t const si = size_t(here - sites.data());
            std::vector<std::pair<std::string, Plots::Spot>> stops{{"the gate", here->gate}};
            for (Plots::Stop const& st : here->stops)
                stops.push_back({st.name, st.at});
            for (size_t k = 0; k < stops.size() && k < kStopsPerSite; ++k)
            {
                Plots::Spot const& at = stops[k].second;
                float const dx = at.x - ask.x, dy = at.y - ask.y;
                if (dx * dx + dy * dy <= kStopHere * kStopHere)
                    continue;
                Service s;
                s.action = Action::Travel;
                s.arg = uint32_t(si * kStopsPerSite + k);
                s.label = "Take me to " + stops[k].first;
                s.note = here->name;
                out.push_back(std::move(s));
            }
        }
        if (Plots::Plot const* mine = r.OwnedBy(ask.account); mine && g.SiteOf(mine->id))
        {
            Service s;
            s.action = Action::Travel;
            s.arg = kTravelMine;
            s.label = "Take me to my homestead";
            s.note = "The stop nearest plot " + std::to_string(mine->id) + " " + g.SiteOf(mine->id)->where;
            out.push_back(std::move(s));
        }
        for (size_t i = 0; i < sites.size(); ++i)
        {
            if (&sites[i] == here)
                continue;
            Service s;
            s.action = Action::Travel;
            s.arg = uint32_t(i * kStopsPerSite);
            s.label = "Take me to the homesteads " + sites[i].where;
            s.note = sites[i].name + "'s gate";
            out.push_back(std::move(s));
        }
        return out;
    }

    bool TravelStop(Plots::Grid const& g, uint32_t arg, Plots::Site const*& site, Plots::Spot& at)
    {
        site = nullptr;
        if (arg == kTravelBack || arg == kTravelMine)
            return false;
        size_t const si = arg / kStopsPerSite, k = arg % kStopsPerSite;
        if (si >= g.Sites().size())
            return false;
        Plots::Site const& s = g.Sites()[si];
        if (k == 0)
            at = s.gate;
        else if (k - 1 < s.stops.size())
            at = s.stops[k - 1].at;
        else
            return false;
        site = &s;
        return true;
    }

    std::vector<NearPlot> PlotsNear(Plots::Grid const& g, Plots::Registry const& r, Plots::Actor const& viewer,
        uint32_t map, float x, float y, float radius, int64_t now)
    {
        std::vector<std::pair<float, NearPlot>> found;
        for (auto const& kv : r.plots)
        {
            Plots::Plot const& p = kv.second;
            if (!p.Owned() || Plots::StandingOf(p, now) == Plots::Standing::Lapsed)
                continue;
            Plots::District const* d = g.DistrictOf(p.id);
            std::optional<Plots::Rect> const b = g.Bounds(p.id);
            if (!d || !b || d->map != map)
                continue;
            float const cx = (b->x0 + b->x1) / 2.f - x, cy = (b->y0 + b->y1) / 2.f - y;
            float const dist = std::sqrt(cx * cx + cy * cy);
            if (dist > radius)
                continue;
            NearPlot n;
            n.id = p.id;
            n.bounds = *b;
            Plots::Access const a = Plots::AccessOf(viewer, &p, now);
            n.relation = viewer.kind == Plots::Actor::Kind::Player && viewer.account == p.ownerAccount ? Relation::Mine
                : (a == Plots::Access::Builder || a == Plots::Access::Guest) ? Relation::Permitted
                : Relation::Claimed;
            // The viewer's own sorts first, so the cap never drops it.
            found.push_back({n.relation == Relation::Mine ? -1.f : dist, n});
        }
        std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) {
            return a.first != b.first ? a.first < b.first : a.second.id < b.second.id;
        });
        std::vector<NearPlot> out;
        for (size_t i = 0; i < found.size() && i < kMaxNear; ++i)
            out.push_back(found[i].second);
        return out;
    }

    std::vector<uint8_t> EncodeNearPlots(std::vector<NearPlot> const& plots)
    {
        size_t const n = std::min(plots.size(), kMaxNear);
        std::vector<uint8_t> o{kOpNearPlots, uint8_t(n)};
        for (size_t i = 0; i < n; ++i)
        {
            Put32(o, plots[i].id);
            PutF(o, plots[i].bounds.x0);
            PutF(o, plots[i].bounds.y0);
            PutF(o, plots[i].bounds.x1);
            PutF(o, plots[i].bounds.y1);
            o.push_back(uint8_t(plots[i].relation));
        }
        return o;
    }
}}
