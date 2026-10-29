/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/WandererLives.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace Fusion::Wanderers
{
    namespace
    {
        uint32_t R(uint32_t seed, uint32_t salt) { return Mix(seed ^ (salt * 0x9E3779B9u)); }

        template <size_t N>
        char const* PickOf(std::array<char const*, N> const& list, uint32_t roll)
        {
            return list[roll % N];
        }

        uint8_t Clamp100(int v) { return uint8_t(std::clamp(v, 0, 100)); }

        // {token} filling: every token of the line from `vars`, unknown ones left (LineIsClean
        // catches them).
        std::string FillAll(std::string line, std::vector<std::pair<char const*, std::string>> const& vars)
        {
            for (auto const& [k, v] : vars)
            {
                std::string const key = std::string("{") + k + "}";
                for (size_t at = line.find(key); at != std::string::npos; at = line.find(key, at + v.size()))
                    line.replace(at, key.size(), v);
            }
            return line;
        }

        std::string Or(std::string const& s, char const* fallback) { return s.empty() ? std::string(fallback) : s; }

        std::vector<std::pair<char const*, std::string>> VarsOf(Talk const& t)
        {
            return {{"name", Or(t.name, "I")},
                {"player", Or(t.player, "friend")},
                {"home", Or(t.home, "home")},
                {"work", Or(t.work, "the work")},
                {"market", Or(t.market, "the market")},
                {"here", Or(t.here, "these parts")},
                {"heading", Or(t.heading, "the next town")},
                {"friend", Or(t.friendName, "an old friend")},
                {"rival", Or(t.rivalName, "a certain someone")},
                {"time", TimeOfDay(t.hour)},
                {"trade", OccupationInfo(t.occupation).trade},
                {"job", OccupationName(t.occupation)},
                {"doing", ActivityPhrase(t.doing, t.occupation)},
                {"what", Or(t.news.what, "something")},
                {"where", Or(t.news.where, Or(t.here, "these parts").c_str())},
                {"weather", Or(t.weather, "weather")}};
        }

        std::string Clean(std::string s)
        {
            s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '|' || c == ';' || c == ':' || c == ','; }), s.end());
            if (s.size() > 200)
                s.resize(200);
            return s;
        }

        // The backstory keeps its commas: only the field mark goes.
        std::string Bare(std::string s)
        {
            s.erase(std::remove(s.begin(), s.end(), '|'), s.end());
            if (s.size() > 400)
                s.resize(400);
            return s;
        }

        std::vector<std::string> Split(std::string const& s, char sep)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char c : s)
            {
                if (c == sep)
                {
                    out.push_back(cur);
                    cur.clear();
                }
                else
                    cur += c;
            }
            out.push_back(cur);
            return out;
        }

        bool ToU(std::string const& s, uint32_t& v, uint32_t max = 0xFFFFFFFFu)
        {
            if (s.empty() || s.size() > 10 || s.find_first_not_of("0123456789") != std::string::npos)
                return false;
            unsigned long long const x = std::strtoull(s.c_str(), nullptr, 10);
            if (x > max)
                return false;
            v = uint32_t(x);
            return true;
        }

        bool ToI(std::string const& s, int& v, int lo, int hi)
        {
            if (s.empty() || s.size() > 5)
                return false;
            size_t i = s[0] == '-' ? 1 : 0;
            if (i >= s.size() || s.find_first_not_of("0123456789", i) != std::string::npos)
                return false;
            long const x = std::strtol(s.c_str(), nullptr, 10);
            if (x < lo || x > hi)
                return false;
            v = int(x);
            return true;
        }
    }

    // ── Trades ──────────────────────────────────────────────────────────────────────────────

    OccupationDef const& OccupationInfo(Occupation o)
    {
        // Item entries are vanilla's. The fallbacks are used only where a workplace has no loot
        // table of the world's own; the rares are the trade's lucky finds.
        static std::array<OccupationDef, size_t(Occupation::Count)> const defs = [] {
            std::array<OccupationDef, size_t(Occupation::Count)> d{};
            auto set = [&](Occupation id, char const* name, char const* trade, NodeKind work, bool gathers, uint32_t minMs,
                           uint32_t maxMs, uint8_t brave, uint8_t bounty, uint16_t share) {
                OccupationDef& x = d[size_t(id)];
                x.id = id;
                x.name = name;
                x.trade = trade;
                x.work = work;
                x.gathers = gathers;
                x.workMinMs = minMs;
                x.workMaxMs = maxMs;
                x.brave = brave;
                x.bountyPercent = bounty;
                x.share = share;
                return &x;
            };
            constexpr uint32_t m = 60 * 1000;
            set(Occupation::None, "Wanderer", "wandering", NodeKind::Town, false, 2 * m, 5 * m, 60, 35, 0);
            OccupationDef* f = set(Occupation::Fisherman, "Fisherman", "fishing", NodeKind::Fishing, true, 8 * m, 20 * m, 35, 0, 14);
            f->fallback = {{6291, 30, 1, 3}, {6289, 25, 1, 2}, {6308, 15, 1, 2}, {6303, 15, 1, 2}, {6361, 10, 1, 2}, {6358, 5, 1, 1}};
            f->rares = {6529, 6530, 6533, 6365, 6367, 19972};
            OccupationDef* mi = set(Occupation::Miner, "Miner", "mining", NodeKind::Mine, true, 8 * m, 18 * m, 55, 5, 11);
            mi->fallback = {{2770, 40, 1, 3}, {2835, 30, 1, 3}, {2771, 15, 1, 2}, {2836, 10, 1, 2}, {2772, 5, 1, 1}};
            mi->rares = {774, 818, 1210, 1705, 1206, 1529};
            OccupationDef* h = set(Occupation::Herbalist, "Herbalist", "herb-gathering", NodeKind::Herbs, true, 6 * m, 15 * m, 30, 0, 11);
            h->fallback = {{2447, 30, 1, 3}, {765, 30, 1, 3}, {2449, 20, 1, 2}, {785, 10, 1, 2}, {2450, 10, 1, 2}};
            h->rares = {3357, 3818, 3821, 3358, 3356};
            OccupationDef* tr = set(Occupation::Trapper, "Trapper", "trapping", NodeKind::Wilds, true, 8 * m, 18 * m, 65, 15, 11);
            tr->fallback = {{2934, 30, 1, 3}, {2318, 25, 1, 2}, {769, 20, 1, 2}, {2672, 15, 1, 2}, {783, 10, 1, 1}};
            tr->rares = {4232, 4235, 2319};
            OccupationDef* p = set(Occupation::Peddler, "Peddler", "trading", NodeKind::Market, false, 4 * m, 9 * m, 40, 0, 10);
            p->fallback = {{159, 30, 2, 5}, {117, 30, 2, 5}, {2678, 15, 1, 3}, {2320, 15, 1, 3}, {4536, 10, 2, 4}};
            p->rares = {2996, 2457, 5173};
            set(Occupation::Courier, "Courier", "carrying the post", NodeKind::Town, false, 1 * m, 3 * m, 45, 0, 9);
            OccupationDef* s = set(Occupation::Sellsword, "Sellsword", "selling my sword", NodeKind::Board, false, 3 * m, 6 * m, 90, 70, 9);
            s->fallback = {{1251, 40, 1, 3}, {2512, 30, 20, 40}, {2516, 30, 20, 40}};
            s->rares = {118, 2454};
            OccupationDef* mn = set(Occupation::Minstrel, "Minstrel", "playing for my supper", NodeKind::Inn, false, 6 * m, 14 * m, 20, 0, 7);
            mn->fallback = {{2894, 40, 1, 2}, {2723, 30, 1, 1}, {2593, 30, 1, 1}};
            mn->rares = {2595};
            OccupationDef* pg = set(Occupation::Pilgrim, "Pilgrim", "seeing the world's wonders", NodeKind::Landmark, false, 3 * m, 8 * m, 40, 0, 7);
            pg->fallback = {{1251, 50, 1, 2}, {159, 50, 1, 2}};
            pg->rares = {6948};
            for (OccupationDef& x : d)
                if (x.gathers)
                    x.sessionsPerMarket = 3;
            d[size_t(Occupation::Fisherman)].sessionsPerMarket = 2;
            return d;
        }();
        size_t const i = size_t(o) < defs.size() ? size_t(o) : 0;
        return defs[i];
    }

    char const* OccupationName(Occupation o) { return OccupationInfo(o).name; }

    Occupation OccupationFor(uint32_t seed, std::function<bool(NodeKind)> const& hasKind)
    {
        uint32_t total = 0;
        std::vector<std::pair<Occupation, uint32_t>> open;
        for (uint8_t i = 1; i < uint8_t(Occupation::Count); ++i)
        {
            OccupationDef const& d = OccupationInfo(Occupation(i));
            if (!d.share || !hasKind(d.work))
                continue;
            open.push_back({d.id, d.share});
            total += d.share;
        }
        if (!total)
            return Occupation::None;
        uint32_t pick = R(seed, 0x0CC) % total;
        for (auto const& [o, w] : open)
        {
            if (pick < w)
                return o;
            pick -= w;
        }
        return open.back().first;
    }

    std::string Title(Occupation o, std::string const& home)
    {
        std::string const n = OccupationName(o);
        return home.empty() ? n : n + " of " + home;
    }

    // ── Personality ─────────────────────────────────────────────────────────────────────────

    Traits TraitsFor(uint32_t seed, Occupation o)
    {
        // Each a sum of two rolls (most folk are middling), courage pulled to the trade's.
        auto roll = [&](uint32_t salt) { return int(R(seed, salt) % 51) + int(R(seed, salt + 100) % 51); };
        Traits t;
        t.brave = Clamp100((roll(1) + int(OccupationInfo(o).brave)) / 2);
        t.chatty = Clamp100(roll(2));
        t.greedy = Clamp100(roll(3));
        t.curious = Clamp100(roll(4) + (o == Occupation::Pilgrim ? 25 : 0));
        t.lawful = Clamp100(roll(5) - (o == Occupation::Sellsword ? 10 : 0));
        t.sociable = Clamp100(roll(6) + (o == Occupation::Minstrel ? 25 : 0));
        return t;
    }

    std::string DescribeTraits(Traits const& t)
    {
        struct Axis
        {
            uint8_t v;
            char const* hi;
            char const* lo;
        };
        Axis const axes[] = {{t.brave, "brave", "cowardly"}, {t.chatty, "chatty", "reserved"}, {t.greedy, "greedy", "generous"},
            {t.curious, "curious", "incurious"}, {t.lawful, "lawful", "roguish"}, {t.sociable, "sociable", "a loner"}};
        // The most marked first.
        std::vector<std::pair<int, char const*>> marked;
        for (Axis const& a : axes)
            if (a.v > 65 || a.v < 35)
                marked.push_back({std::abs(int(a.v) - 50), a.v > 65 ? a.hi : a.lo});
        std::stable_sort(marked.begin(), marked.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
        if (marked.empty())
            return "even-tempered";
        std::string out;
        for (size_t i = 0; i < marked.size() && i < 4; ++i)
            out += (i ? ", " : "") + std::string(marked[i].second);
        return out;
    }

    char const* ActivityName(Activity a)
    {
        switch (a)
        {
            case Activity::None: return "nothing";
            case Activity::Sleep: return "sleep";
            case Activity::Eat: return "eat";
            case Activity::Rest: return "rest";
            case Activity::Work: return "work";
            case Activity::Sell: return "sell";
            case Activity::Stock: return "stock up";
            case Activity::Deliver: return "deliver";
            case Activity::Perform: return "perform";
            case Activity::Socialize: return "socialize";
            case Activity::Sightsee: return "sightsee";
            case Activity::Visit: return "visit a friend";
            case Activity::Bounty: return "bounty";
            case Activity::Count: break;
        }
        return "?";
    }

    char const* ActivityPhrase(Activity a, Occupation o)
    {
        switch (a)
        {
            case Activity::Sleep: return "getting some sleep";
            case Activity::Eat: return "having a bite";
            case Activity::Rest: return "resting my feet";
            case Activity::Work:
                switch (o)
                {
                    case Occupation::Fisherman: return "fishing";
                    case Occupation::Miner: return "working the veins";
                    case Occupation::Herbalist: return "gathering herbs";
                    case Occupation::Trapper: return "checking my traps";
                    case Occupation::Peddler: return "buying stock";
                    case Occupation::Courier: return "collecting the post";
                    case Occupation::Sellsword: return "looking for work";
                    case Occupation::Minstrel: return "playing";
                    case Occupation::Pilgrim: return "seeing the sights";
                    default: return "about my business";
                }
            case Activity::Sell: return o == Occupation::Fisherman ? "selling my catch" : "selling my goods";
            case Activity::Stock: return "buying stock";
            case Activity::Deliver: return "delivering a parcel";
            case Activity::Perform: return "playing for the room";
            case Activity::Socialize: return "having a drink with friends";
            case Activity::Sightsee: return "taking in the sights";
            case Activity::Visit: return "visiting a friend";
            case Activity::Bounty: return "after a bounty";
            default: return "on the road";
        }
    }

    void TickNeeds(Needs& n, Activity a, uint32_t ms)
    {
        // Per ten minutes.
        int const steps = int(ms / 600000u);
        if (steps <= 0)
            return;
        int fatigue = n.fatigue, hunger = n.hunger, mood = n.mood;
        switch (a)
        {
            case Activity::Sleep: fatigue -= 9 * steps; hunger += 1 * steps; break;
            case Activity::Rest:
            case Activity::Socialize:
            case Activity::Eat: fatigue -= 4 * steps; hunger += 1 * steps; mood += 1 * steps; break;
            case Activity::Work:
            case Activity::Perform: fatigue += 3 * steps; hunger += 3 * steps; break;
            default: fatigue += 2 * steps; hunger += 2 * steps; break;
        }
        if (a == Activity::Eat)
            hunger = 0;
        // Hungry and tired is grumpy; fed and rested drifts back to its usual cheer.
        if (hunger > 70 || fatigue > 80)
            mood -= 2 * steps;
        else if (mood < 60)
            mood += steps;
        n.fatigue = Clamp100(fatigue);
        n.hunger = Clamp100(hunger);
        n.mood = Clamp100(mood);
    }

    void Nudge(Needs& n, int delta) { n.mood = Clamp100(int(n.mood) + delta); }

    // ── Decisions by nature ─────────────────────────────────────────────────────────────────

    float FleeBelowPct(Traits const& t)
    {
        // A coward (10) runs at 60%, a middling one (50) at 25%, the brave (75 up) never.
        if (t.brave >= 75)
            return 0.f;
        return std::clamp(65.f - 0.8f * float(t.brave), 5.f, 65.f);
    }

    bool StartsFights(Traits const& t, Occupation o)
    {
        if (o == Occupation::Sellsword || o == Occupation::None)
            return true;
        if (o == Occupation::Trapper)
            return t.brave >= 40;
        return t.brave >= 70 || (t.brave >= 55 && t.lawful < 35);
    }

    bool TakesDetour(Traits const& t, uint32_t roll)
    {
        // A curious one (90) one trip in three; an incurious one (10) hardly ever.
        return Mix(roll ^ 0xDE70u) % 100 < uint32_t(t.curious) * 35 / 100;
    }

    bool StopsToChat(Traits const& a, Traits const& b, int affinity, uint32_t roll)
    {
        int chance = (int(a.sociable) + int(b.sociable)) / 4 + (int(a.chatty) + int(b.chatty)) / 8;
        if (affinity >= kFriendAt)
            chance += 30;
        else if (affinity <= kRivalAt)
            chance += 5;    // to trade barbs
        return int(Mix(roll ^ 0xC4A7u) % 100) < std::clamp(chance, 5, 90);
    }

    bool AcceptsHire(Traits const& t, Occupation o, int32_t bondPoints)
    {
        if (bondPoints < 0)
            return false;
        if (o == Occupation::Sellsword)
            return true;
        return t.brave >= 35;
    }

    uint32_t HirePrice(uint8_t level, Traits const& t, Occupation o, uint8_t bondTier)
    {
        float base = 20.f + 15.f * float(level) * float(level) / 4.f;
        base *= 0.6f + float(t.greedy) / 100.f;
        if (o == Occupation::Sellsword)
            base *= 0.8f;   // it is its trade
        if (bondTier >= 2)
            base *= 0.5f;
        else if (bondTier == 1)
            base *= 0.8f;
        return std::max<uint32_t>(10, uint32_t(base));
    }

    // ── The pack ────────────────────────────────────────────────────────────────────────────

    uint16_t AddStock(std::vector<Stock>& pack, uint32_t item, uint16_t count, bool rare)
    {
        if (!item)
            return count;
        for (Stock& s : pack)
            if (s.item == item && count)
            {
                uint16_t const room = uint16_t(kMaxStack - std::min(s.count, kMaxStack));
                uint16_t const n = std::min(room, count);
                s.count = uint16_t(s.count + n);
                s.rare = s.rare || rare;
                count = uint16_t(count - n);
            }
        while (count && pack.size() < kMaxStock)
        {
            uint16_t const n = std::min(count, kMaxStack);
            pack.push_back({item, n, rare});
            count = uint16_t(count - n);
        }
        return count;
    }

    uint16_t TakeStock(std::vector<Stock>& pack, uint32_t item, uint16_t count)
    {
        uint16_t taken = 0;
        for (Stock& s : pack)
            if (s.item == item && taken < count)
            {
                uint16_t const n = std::min<uint16_t>(s.count, uint16_t(count - taken));
                s.count = uint16_t(s.count - n);
                taken = uint16_t(taken + n);
            }
        pack.erase(std::remove_if(pack.begin(), pack.end(), [](Stock const& s) { return s.count == 0; }), pack.end());
        return taken;
    }

    uint32_t CountStock(std::vector<Stock> const& pack, uint32_t item)
    {
        uint32_t n = 0;
        for (Stock const& s : pack)
            if (s.item == item)
                n += s.count;
        return n;
    }

    uint32_t PackItems(std::vector<Stock> const& pack)
    {
        uint32_t n = 0;
        for (Stock const& s : pack)
            n += s.count;
        return n;
    }

    YieldTable YieldFor(Occupation o, std::vector<LootRow> rows)
    {
        YieldTable t;
        // Only real goods: rows with no item or no chance at all and no group are dropped.
        for (LootRow& r : rows)
            if (r.item && (r.chance > 0.f || r.group))
            {
                r.minCount = std::max<uint8_t>(1, r.minCount);
                r.maxCount = std::max(r.minCount, r.maxCount);
                t.rows.push_back(r);
            }
        OccupationDef const& d = OccupationInfo(o);
        t.fallback = d.fallback;
        t.rares = d.rares;
        return t;
    }

    std::vector<Stock> RollGather(YieldTable const& t, uint32_t roll)
    {
        std::vector<Stock> out;
        uint32_t salt = 1;
        auto next = [&]() { return Mix(roll ^ (salt++ * 0x85EBCA6Bu)); };
        auto count = [&](uint8_t lo, uint8_t hi) { return uint16_t(lo + (hi > lo ? next() % (hi - lo + 1u) : 0u)); };
        auto add = [&](uint32_t item, uint16_t n, bool rare) {
            for (Stock& s : out)
                if (s.item == item)
                {
                    s.count = uint16_t(s.count + n);
                    s.rare = s.rare || rare;
                    return;
                }
            out.push_back({item, n, rare});
        };
        if (!t.rows.empty())
        {
            // Ungrouped rows each by their chance; each group one of its rows: the explicit
            // chances first, then the rest shared by the rows of chance 0.
            std::map<uint8_t, std::vector<LootRow const*>> groups;
            for (LootRow const& r : t.rows)
            {
                if (r.group)
                {
                    groups[r.group].push_back(&r);
                    continue;
                }
                if (float(next() % 10000u) / 100.f < r.chance)
                    add(r.item, count(r.minCount, r.maxCount), false);
            }
            for (auto const& [g, list] : groups)
            {
                float explicitSum = 0.f;
                size_t equal = 0;
                for (LootRow const* r : list)
                {
                    explicitSum += r->chance;
                    equal += r->chance <= 0.f ? 1 : 0;
                }
                float pick = float(next() % 10000u) / 100.f;
                LootRow const* got = nullptr;
                for (LootRow const* r : list)
                    if (r->chance > 0.f)
                    {
                        if (pick < r->chance)
                        {
                            got = r;
                            break;
                        }
                        pick -= r->chance;
                    }
                if (!got && equal)
                {
                    float const share = std::max(0.f, 100.f - explicitSum) / float(equal);
                    for (LootRow const* r : list)
                        if (r->chance <= 0.f)
                        {
                            if (pick < share)
                            {
                                got = r;
                                break;
                            }
                            pick -= share;
                        }
                }
                if (got)
                    add(got->item, count(got->minCount, got->maxCount), false);
            }
        }
        else if (!t.fallback.empty())
        {
            uint32_t total = 0;
            for (Ware const& w : t.fallback)
                total += w.weight;
            uint32_t pick = total ? next() % total : 0;
            for (Ware const& w : t.fallback)
            {
                if (pick < w.weight)
                {
                    add(w.item, count(w.minCount, w.maxCount), false);
                    break;
                }
                pick -= w.weight;
            }
        }
        if (!t.rares.empty() && next() % 1000u < t.rarePermille)
            add(t.rares[next() % t.rares.size()], 1, true);
        return out;
    }

    std::vector<Stock> WorkYield(YieldTable const& t, uint32_t ms, uint32_t seed)
    {
        uint32_t const gathers = std::max<uint32_t>(1, ms / kGatherMs);
        std::vector<Stock> out;
        for (uint32_t i = 0; i < gathers; ++i)
            for (Stock const& s : RollGather(t, Mix(seed + i * 0x27D4EB2Fu)))
            {
                bool merged = false;
                for (Stock& o : out)
                    if (o.item == s.item)
                    {
                        o.count = uint16_t(std::min<uint32_t>(o.count + s.count, 0xFFFF));
                        o.rare = o.rare || s.rare;
                        merged = true;
                    }
                if (!merged)
                    out.push_back(s);
            }
        return out;
    }

    uint32_t StallPrice(Price p, Traits const& t, bool rare, uint8_t bondTier)
    {
        // Its base is a vendor's buying price where there is one, else four times its selling one
        // (vanilla's usual markup), else a copper or two.
        float base = p.buy ? float(p.buy) : float(p.sell) * 4.f;
        if (base <= 0.f)
            base = 2.f;
        float mul = 0.45f + float(t.greedy) / 125.f;        // 0.45 (generous) to 1.25 (grasping)
        if (rare)
            mul *= 1.6f;
        if (bondTier >= 2)
            mul *= 0.8f;
        else if (bondTier == 1)
            mul *= 0.92f;
        uint32_t price = uint32_t(std::lround(base * mul));
        // Never at or below what a vendor pays for it.
        return std::max<uint32_t>({price, p.sell + 1, 1});
    }

    uint16_t StallKeeps(Traits const& t) { return uint16_t(2 + t.greedy / 20); }

    Sale SellAtMarket(std::vector<Stock>& pack, std::function<Price(uint32_t)> const& priceOf, Traits const& t)
    {
        Sale s;
        uint16_t const keep = StallKeeps(t);
        for (Stock& st : pack)
        {
            if (st.rare)
                continue;
            Price const p = priceOf(st.item);
            uint16_t const kept = std::min(st.count, keep);
            uint16_t const sold = uint16_t(st.count - kept);
            if (!sold || !p.sell)
                continue;   // a thing no vendor buys stays for the stall
            s.copper += p.sell * sold;
            s.items += sold;
            st.count = kept;
        }
        pack.erase(std::remove_if(pack.begin(), pack.end(), [](Stock const& x) { return x.count == 0; }), pack.end());
        return s;
    }

    std::vector<Stock> StockUp(std::vector<std::pair<uint32_t, Price>> const& goods, uint32_t& money, Traits const& t,
        std::vector<Stock>& pack, uint32_t roll)
    {
        std::vector<Stock> bought;
        if (goods.empty())
            return bought;
        uint32_t budget = money / (t.greedy > 60 ? 3 : 2);
        size_t const kinds = std::min<size_t>(4, goods.size());
        size_t const start = Mix(roll) % goods.size();
        for (size_t k = 0; k < kinds; ++k)
        {
            auto const& [item, p] = goods[(start + k) % goods.size()];
            if (!p.buy || p.buy > budget)
                continue;
            uint16_t n = uint16_t(std::min<uint32_t>({budget / p.buy, 2u + Mix(roll + uint32_t(k)) % 5u, kMaxStack}));
            if (!n)
                continue;
            uint16_t const left = AddStock(pack, item, n);
            n = uint16_t(n - left);
            if (!n)
                continue;
            budget -= p.buy * n;
            money -= p.buy * n;
            bought.push_back({item, n, false});
        }
        return bought;
    }

    uint32_t InnCost(uint8_t level, Activity a)
    {
        uint32_t const l = std::max<uint8_t>(1, level);
        return a == Activity::Sleep ? 10 + 4 * l : a == Activity::Eat ? 5 + 2 * l : 0;
    }

    uint32_t Tips(uint8_t level, uint32_t listeners, uint32_t roll)
    {
        return (5 + 3 * uint32_t(std::max<uint8_t>(1, level))) * (1 + std::min<uint32_t>(listeners, 5)) + Mix(roll) % 20;
    }

    uint32_t CourierPay(uint8_t level, float yards)
    {
        return 20 + uint32_t(std::max<uint8_t>(1, level)) * 5 + uint32_t(std::max(0.f, yards) / 40.f);
    }

    // ── Its days ────────────────────────────────────────────────────────────────────────────

    uint32_t DayOf(int64_t unixLocal) { return uint32_t(std::max<int64_t>(0, unixLocal) / 86400); }
    uint8_t HourOf(int64_t unixLocal) { return uint8_t((std::max<int64_t>(0, unixLocal) % 86400) / 3600); }

    bool IsDayOff(uint32_t seed, uint32_t day)
    {
        uint32_t const cycle = 5 + R(seed, 0xD0FF) % 4;
        return (day + R(seed, 0xD1FF)) % cycle == 0;
    }

    char const* TimeOfDay(uint8_t hour)
    {
        if (hour < 5 || hour >= 22)
            return "night";
        if (hour < 12)
            return "morning";
        if (hour < 18)
            return "afternoon";
        return "evening";
    }

    Activity Scheduled(DayContext const& c, uint32_t roll)
    {
        OccupationDef const& d = OccupationInfo(c.occupation);
        Traits const& t = c.traits;
        uint8_t const h = c.hour;
        uint32_t const r = Mix(roll ^ 0x5C4Eu) % 100;
        // A night owl (sociable) keeps the inn till midnight; everyone sleeps by one.
        bool const owl = t.sociable >= 70;
        if (h < 5 || (h >= 23 && !owl) || (h == 0 && owl))
            return Activity::Sleep;
        if (c.needs.fatigue >= 85)
            return h >= 20 || h < 7 ? Activity::Sleep : Activity::Rest;
        if (c.needs.hunger >= 70 || (h == 7 && r < 60) || (h == 12 && r < 40))
            return Activity::Eat;
        // The evening: a minstrel's best hours; company for the sociable; else bed early.
        if (h >= 19)
        {
            if (c.occupation == Occupation::Minstrel)
                return Activity::Perform;
            if (r < 30 + t.sociable / 2)
                return Activity::Socialize;
            return h >= 21 ? Activity::Sleep : Activity::Rest;
        }
        // A day off: a friend, a sight, the inn.
        if (IsDayOff(c.seed, c.day))
        {
            if (c.friendKnown && r < 25 + t.sociable / 3)
                return Activity::Visit;
            if (r < 55 + t.curious / 4)
                return Activity::Sightsee;
            return Activity::Socialize;
        }
        // Now and then, a friend on the way.
        if (c.friendKnown && r < t.sociable / 8)
            return Activity::Visit;
        switch (c.occupation)
        {
            case Occupation::Fisherman:
            case Occupation::Miner:
            case Occupation::Herbalist:
            case Occupation::Trapper:
                // Work, and to market when the pack is full enough (or the sessions are done),
                // the afternoon a likelier market time.
                if (c.packItems && (c.sessions >= d.sessionsPerMarket || c.packItems >= 30 || (h >= 15 && c.sessions >= 1 && r < 50)))
                    return Activity::Sell;
                if (d.bountyPercent && r < d.bountyPercent / 2)
                    return Activity::Bounty;
                return Activity::Work;
            case Occupation::Peddler:
                return c.packItems >= 6 ? Activity::Sell : Activity::Stock;
            case Occupation::Courier:
                return c.parcel ? Activity::Deliver : Activity::Work;
            case Occupation::Sellsword:
                return r < d.bountyPercent ? Activity::Bounty : (c.packItems && r < 85 ? Activity::Sell : Activity::Work);
            case Occupation::Minstrel:
                return h >= 14 ? Activity::Perform : (r < 50 ? Activity::Sightsee : Activity::Rest);
            case Occupation::Pilgrim:
                return r < 75 ? Activity::Sightsee : Activity::Rest;
            case Occupation::None:
            case Occupation::Count:
                break;
        }
        return r < d.bountyPercent ? Activity::Bounty : Activity::Rest;
    }

    namespace
    {
        // The nearest of `list` to `at` by road (straight-line distance as the network's metric
        // is near enough for a choice), skipping `skip`; -1 none.
        int Nearest(RoadNet const& net, int at, std::vector<int> const& list, std::vector<int> const& skip = {})
        {
            int best = -1;
            float bestD = 0.f;
            Vec3 const from = at >= 0 && size_t(at) < net.nodes.size() ? net.nodes[size_t(at)].pos : Vec3();
            for (int i : list)
            {
                if (i < 0 || size_t(i) >= net.nodes.size() || std::find(skip.begin(), skip.end(), i) != skip.end())
                    continue;
                float const d = (net.nodes[size_t(i)].pos - from).Length();
                if (best < 0 || d < bestD)
                {
                    best = i;
                    bestD = d;
                }
            }
            return best;
        }

        // One of `list` not visited lately, by the roll; -1 none.
        int Fresh(std::vector<int> const& list, std::vector<int> const& recent, int at, uint32_t roll)
        {
            std::vector<int> fresh;
            for (int i : list)
                if (i >= 0 && i != at && std::find(recent.begin(), recent.end(), i) == recent.end())
                    fresh.push_back(i);
            if (fresh.empty())
                for (int i : list)
                    if (i >= 0 && i != at)
                        fresh.push_back(i);
            return fresh.empty() ? -1 : fresh[Mix(roll) % fresh.size()];
        }
    }

    Plan PlanNext(DayContext const& c, Haunts const& h, RoadNet const& net, int at, std::vector<int> const& recent, uint32_t roll)
    {
        Plan p;
        p.activity = Scheduled(c, roll);
        uint32_t const r2 = Mix(roll ^ 0xB0B0u);
        // A detour on the way to its day's business, for the curious (never two in a row).
        bool const errand = p.activity == Activity::Work || p.activity == Activity::Sell || p.activity == Activity::Deliver
            || p.activity == Activity::Stock;
        if (errand && !c.lastWasDetour && !h.landmarks.empty() && TakesDetour(c.traits, roll))
        {
            int const sight = Fresh(h.landmarks, recent, at, r2);
            if (sight >= 0)
            {
                p.activity = Activity::Sightsee;
                p.node = sight;
                p.detour = true;
                return p;
            }
        }
        auto innNear = [&]() {
            int const i = Nearest(net, at, h.inns);
            return i >= 0 ? i : (h.home >= 0 ? h.home : Nearest(net, at, h.settlements));
        };
        switch (p.activity)
        {
            case Activity::Sleep:
                // Home if it is near enough to walk before late, else the nearest inn.
                p.node = h.home;
                if (h.home >= 0 && at >= 0 && (net.nodes[size_t(h.home)].pos - net.nodes[size_t(at)].pos).Length() > 1500.f)
                    p.node = innNear();
                break;
            case Activity::Eat:
            case Activity::Rest:
            case Activity::Socialize:
                p.node = innNear();
                break;
            case Activity::Perform:
                // Its home inn mostly, another now and then.
                p.node = (r2 % 100 < 60 && h.home >= 0) ? h.home : Fresh(h.inns, recent, -1, r2);
                if (p.node < 0)
                    p.node = innNear();
                break;
            case Activity::Work:
                if (c.occupation == Occupation::Courier)
                    p.node = Fresh(h.settlements, recent, at, r2);
                else if (c.occupation == Occupation::Sellsword)
                    p.node = Nearest(net, at, h.boards);
                else if (c.occupation == Occupation::Minstrel)
                    p.node = innNear();
                else
                {
                    // Its own spot two days in three, another of its kind the third.
                    p.node = h.work;
                    if (h.work < 0 || (!h.works.empty() && r2 % 3 == 0))
                    {
                        int const other = Fresh(h.works, recent, at, r2);
                        if (other >= 0)
                            p.node = other;
                    }
                }
                break;
            case Activity::Sell:
                p.node = h.market >= 0 ? h.market : Nearest(net, at, h.markets);
                // A peddler sells away from where it bought.
                if (c.occupation == Occupation::Peddler && (p.node == at || r2 % 2))
                {
                    int const other = Fresh(h.markets, recent, at, r2);
                    if (other >= 0)
                        p.node = other;
                }
                break;
            case Activity::Stock:
                p.node = Nearest(net, at, h.markets);
                break;
            case Activity::Deliver:
                p.node = h.parcelTo;
                break;
            case Activity::Sightsee:
                p.node = Fresh(h.landmarks, recent, at, r2);
                if (p.node < 0)
                    p.node = Fresh(h.settlements, recent, at, r2);
                break;
            case Activity::Visit:
                p.node = h.friendAt;
                break;
            case Activity::Bounty:
                p.node = Nearest(net, at, h.boards);
                break;
            default:
                break;
        }
        // Wherever it cannot go, the nearest settlement, as a rest.
        if (p.node < 0)
        {
            p.activity = p.activity == Activity::Sleep ? Activity::Sleep : Activity::Rest;
            p.node = Nearest(net, at, h.settlements);
            if (p.node < 0)
                p.node = h.home;
            if (p.node < 0 && !net.nodes.empty())
                p.node = 0;
        }
        return p;
    }

    uint32_t ActivityMs(Activity a, OccupationDef const& d, Traits const& t, uint8_t hour, uint32_t roll)
    {
        constexpr uint32_t m = 60 * 1000;
        uint32_t const r = Mix(roll ^ 0xAC71u);
        auto span = [&](uint32_t lo, uint32_t hi) { return lo + (hi > lo ? r % (hi - lo + 1) : 0); };
        switch (a)
        {
            case Activity::Sleep:
            {
                // Until six in the morning (the hour it is, to the hour), at least ten minutes.
                uint32_t const hoursLeft = hour < 6 ? 6u - hour : 24u - hour + 6u;
                return std::max<uint32_t>(10 * m, std::min<uint32_t>(hoursLeft, 9) * 60 * m - span(0, 20) * m);
            }
            case Activity::Eat: return span(2 * m, 4 * m);
            case Activity::Rest: return span(3 * m, 8 * m);
            case Activity::Work: return span(d.workMinMs, d.workMaxMs);
            case Activity::Sell: return span(3 * m, 6 * m);
            case Activity::Stock: return span(2 * m, 4 * m);
            case Activity::Deliver: return span(1 * m, 2 * m);
            case Activity::Perform: return span(8 * m, 16 * m);
            case Activity::Socialize: return span(6 * m, 10 * m) + uint32_t(t.sociable) * 6 * 1000;
            case Activity::Sightsee: return span(2 * m, 5 * m) + uint32_t(t.curious) * 2 * 1000;
            case Activity::Visit: return span(5 * m, 10 * m);
            case Activity::Bounty: return 30 * 1000;
            default: return 2 * m;
        }
    }

    // ── Friends and rivals ──────────────────────────────────────────────────────────────────

    char const* TieName(int8_t a)
    {
        if (a >= 70)
            return "dear friend";
        if (a >= kFriendAt)
            return "friend";
        if (a <= -70)
            return "sworn rival";
        if (a <= kRivalAt)
            return "rival";
        return "acquaintance";
    }

    std::vector<Tie> FirstTies(uint32_t id, std::vector<uint32_t> const& others, uint32_t seed)
    {
        std::vector<Tie> ties;
        std::vector<uint32_t> pool;
        for (uint32_t o : others)
            if (o != id && std::find(pool.begin(), pool.end(), o) == pool.end())
                pool.push_back(o);
        std::sort(pool.begin(), pool.end());
        size_t const want = std::min<size_t>(pool.size(), 1 + R(seed, 0x71E) % 3);
        for (size_t k = 0; k < want; ++k)
        {
            size_t const i = R(seed, 0x720 + uint32_t(k)) % pool.size();
            Tie t;
            t.other = pool[i];
            uint32_t const r = R(seed, 0x730 + uint32_t(k)) % 100;
            t.affinity = r < 20 ? int8_t(-35 - int(r)) : int8_t(30 + int(r % 50));
            ties.push_back(t);
            pool.erase(pool.begin() + std::ptrdiff_t(i));
        }
        return ties;
    }

    void Meet(std::vector<Tie>& ties, uint32_t other, Traits const& mine, Traits const& theirs, uint32_t roll)
    {
        // Alike natures warm to each other; the lawful and the roguish grate.
        int const unlike = (std::abs(int(mine.chatty) - int(theirs.chatty)) + std::abs(int(mine.sociable) - int(theirs.sociable))
            + std::abs(int(mine.greedy) - int(theirs.greedy))) / 3;
        int delta = 4 - unlike / 12 + int(Mix(roll ^ 0x3EE7u) % 5) - 2;
        if ((mine.lawful > 65 && theirs.lawful < 35) || (mine.lawful < 35 && theirs.lawful > 65))
            delta -= 4;
        for (Tie& t : ties)
            if (t.other == other)
            {
                // A friendship or a rivalry deepens rather than flips at random.
                if (t.affinity >= kFriendAt)
                    delta = std::max(delta, 1);
                else if (t.affinity <= kRivalAt)
                    delta = std::min(delta, -1);
                t.affinity = int8_t(std::clamp(int(t.affinity) + delta, -100, 100));
                t.met = uint16_t(std::min<uint32_t>(t.met + 1u, 0xFFFF));
                return;
            }
        Tie t;
        t.other = other;
        t.affinity = int8_t(std::clamp(delta * 2, -100, 100));
        t.met = 1;
        ties.push_back(t);
        if (ties.size() > kMaxTies)
        {
            // The weakest (nearest 0) goes, never the one just made.
            auto weakest = ties.begin();
            for (auto it = ties.begin(); it + 1 != ties.end(); ++it)
                if (std::abs(int(it->affinity)) < std::abs(int(weakest->affinity)))
                    weakest = it;
            ties.erase(weakest);
        }
    }

    Tie const* TieWith(std::vector<Tie> const& ties, uint32_t other)
    {
        for (Tie const& t : ties)
            if (t.other == other)
                return &t;
        return nullptr;
    }

    uint32_t BestFriend(std::vector<Tie> const& ties)
    {
        Tie const* best = nullptr;
        for (Tie const& t : ties)
            if (t.affinity >= kFriendAt && (!best || t.affinity > best->affinity))
                best = &t;
        return best ? best->other : 0;
    }

    uint32_t WorstRival(std::vector<Tie> const& ties)
    {
        Tie const* worst = nullptr;
        for (Tie const& t : ties)
            if (t.affinity <= kRivalAt && (!worst || t.affinity < worst->affinity))
                worst = &t;
        return worst ? worst->other : 0;
    }

    void AddNews(std::vector<News>& news, News n)
    {
        if (n.kind == NewsKind::None)
            return;
        news.erase(std::remove_if(news.begin(), news.end(), [&](News const& o) { return o.kind == n.kind; }), news.end());
        news.insert(news.begin(), std::move(n));
        if (news.size() > kMaxNews)
            news.resize(kMaxNews);
    }

    // ── Talk ────────────────────────────────────────────────────────────────────────────────

    std::string NewsLine(Talk const& t, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0x4E55u);
        static std::array<char const*, 3> const caught{"Found {what} near {where} this {time}, can you believe it?",
            "Look at this: {what}, from {where}! My luck's turned.", "You'll not guess what I found near {where}: {what}!"};
        static std::array<char const*, 2> const caughtFish{"Caught {what} near {where} today! Biggest of the season.",
            "Pulled {what} out of the water near {where}. Still can't believe it."};
        static std::array<char const*, 2> const sold{"Good day at {where}: {what} in my purse.", "Sold the lot at {where}. {what}, all told."};
        static std::array<char const*, 2> const fight{"Put down {what} near {where}. Not my first.", "{what} came at me near {where}. It won't again."};
        static std::array<char const*, 2> const fled{"Ran from {what} near {where}. No shame in living.",
            "{what}, near {where}! I didn't stop running till I saw walls."};
        static std::array<char const*, 2> const died{"{what} laid me out near {where}. Still aching.", "Woke up in {where} after {what} had at me."};
        static std::array<char const*, 2> const bounty{"Turned in {what}. The board pays, eventually.", "{what}: done and paid."};
        static std::array<char const*, 2> const air{"Did you see that thing in the sky over {where}? Roaring like a dragon.",
            "Something flew over {where}, louder than thunder. Gnome work, I'd wager."};
        static std::array<char const*, 2> const build{"Someone's raising walls near {where}. Folk are settling.", "Saw a builder at work by {where}. Bold."};
        static std::array<char const*, 2> const met{"Ran into {what} at {where}. Good to see a friendly face.", "Shared a drink with {what} at {where}."};
        static std::array<char const*, 2> const gone{"{what} packed up and left for good. The roads feel emptier.",
            "Did you hear? {what} moved away. Never said goodbye."};
        static std::array<char const*, 2> const helped{"{what} saved my hide near {where}. Good people still walk these roads.",
            "If you see {what}, buy them a drink on me. They pulled me out of trouble."};
        char const* line = nullptr;
        switch (t.news.kind)
        {
            case NewsKind::Catch: line = t.occupation == Occupation::Fisherman ? PickOf(caughtFish, r) : PickOf(caught, r); break;
            case NewsKind::Sold: line = PickOf(sold, r); break;
            case NewsKind::Fight: line = PickOf(fight, r); break;
            case NewsKind::Fled: line = PickOf(fled, r); break;
            case NewsKind::Died: line = PickOf(died, r); break;
            case NewsKind::Bounty: line = PickOf(bounty, r); break;
            case NewsKind::Aircraft: line = PickOf(air, r); break;
            case NewsKind::Building: line = PickOf(build, r); break;
            case NewsKind::Friend: line = PickOf(met, r); break;
            case NewsKind::Gone: line = PickOf(gone, r); break;
            case NewsKind::Helped: line = PickOf(helped, r); break;
            default: return std::string();
        }
        return FillAll(line, VarsOf(t));
    }

    std::string ChatLine(Talk const& t, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0xC4A7u);
        // Its news first, half the time when it has some.
        if (t.news.kind != NewsKind::None && r % 2 == 0)
            return NewsLine(t, seed);
        std::vector<char const*> pool;
        static std::array<char const*, 4> const weather{"This {weather}'s soaking everything, {player}.",
            "{weather} again. Good for the fish, bad for the bones.", "Mind the road in this {weather}, {player}.",
            "Nothing like {weather} to make the inn look warm."};
        static std::array<char const*, 4> const time{"Fine {time} for it, {player}.", "Long {time} ahead of me, {player}.",
            "Quiet {time} on the road, isn't it?", "Another {time}, another mile."};
        static std::array<char const*, 3> const work{"Been {doing} since dawn. Honest work, {trade}.",
            "If you ever need a {job}, you know where to find me.", "{trade} pays poorly, but I love it."};
        static std::array<char const*, 3> const grumpy{"Don't ask, {player}. Long day.", "My feet ache and my purse is light.",
            "I'd sell my boots for a hot meal."};
        static std::array<char const*, 3> const cheer{"What a day, {player}! Can't stop smiling.", "Light's blessing on you, {player}.",
            "Life's good on the road today."};
        static std::array<char const*, 2> const friendly{"Have you seen {friend}? Owes me a drink.", "Tell {friend} I said hello, if you pass."};
        static std::array<char const*, 2> const rival{"If {rival} tells you otherwise, they're lying.", "Watch {rival}. Sharp tongue, sharper prices."};
        static std::array<char const*, 2> const reserved{"Hm. {time}.", "Road's long. Mine's that way."};
        if (!t.weather.empty())
            pool.push_back(PickOf(weather, r));
        if (t.traits.chatty < 30)
            pool.push_back(PickOf(reserved, r >> 3));
        pool.push_back(PickOf(time, r >> 5));
        pool.push_back(PickOf(work, r >> 7));
        if (t.needs.mood < 35 || t.needs.hunger > 70 || t.needs.fatigue > 80)
            pool.push_back(PickOf(grumpy, r >> 9));
        if (t.needs.mood > 75)
            pool.push_back(PickOf(cheer, r >> 11));
        if (!t.friendName.empty())
            pool.push_back(PickOf(friendly, r >> 13));
        if (!t.rivalName.empty())
            pool.push_back(PickOf(rival, r >> 15));
        return FillAll(pool[(r >> 17) % pool.size()], VarsOf(t));
    }

    std::string ReplyLine(Talk const& t, uint32_t seed)
    {
        static std::array<char const*, 6> const friendLines{"Ha! Same old {player}.", "Good to see you, {player}. Mind how you go.",
            "I'll drink to that, {player}.", "Safe roads, {player}. See you at {home}.", "You always say that, {player}.",
            "Light keep you, {player}."};
        static std::array<char const*, 4> const rivalLines{"Spare me, {player}.", "Is that so. Out of my way, {player}.",
            "Funny, I heard the same about you, {player}.", "Hmph."};
        static std::array<char const*, 4> const neutral{"Aye, {player}.", "Can't argue with that.", "Safe travels, {player}.",
            "Busy {time}, isn't it?"};
        uint32_t const r = Mix(seed ^ 0x3E91u);
        char const* line = t.bondTier >= 2 ? PickOf(friendLines, r) : PickOf(neutral, r);
        if (!t.rivalName.empty() && t.rivalName == t.player)
            line = PickOf(rivalLines, r);
        return FillAll(line, VarsOf(t));
    }

    std::string AboutLine(Talk const& t, std::string const& backstory, uint32_t seed)
    {
        static std::array<char const*, 3> const tail{" These days I'm {doing}, mostly between {work} and {market}.",
            " Most days you'll find me {doing} near {work}, and at {market} to sell.", " I keep to {home}, {work} and {market}, mostly."};
        uint32_t const r = Mix(seed ^ 0xAB0Bu);
        std::string line = backstory + FillAll(PickOf(tail, r), VarsOf(t));
        if (line.size() > 255)
            line = backstory.substr(0, 255);
        return line;
    }

    std::string HeadingLine(Talk const& t, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0x4EADu);
        static std::array<char const*, 3> const going{"Off to {heading}, {doing}.", "{heading}, for {doing}. Then who knows.",
            "Heading to {heading}. A {job}'s work is never done."};
        static std::array<char const*, 2> const sleepy{"To bed, at {heading}. It's late, {player}.", "Somewhere with a pillow: {heading}."};
        static std::array<char const*, 2> const here{"Right here, {doing}.", "Nowhere just now. I'm {doing}."};
        char const* line = t.doing == Activity::Sleep ? PickOf(sleepy, r) : t.heading.empty() ? PickOf(here, r) : PickOf(going, r);
        return FillAll(line, VarsOf(t));
    }

    std::string RumourLine(std::string const& what, std::string const& where, bool bounty, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0x2B0Bu);
        static std::array<char const*, 3> const b{"The board at {where} wants {what}. Good coin, if you're handy.",
            "Heard {where}'s posting {what}. Not for me, but you look able.", "There's a bounty up at {where}: {what}."};
        static std::array<char const*, 3> const c{"Folk say {what} prowls near {where}. I keep well clear.",
            "Saw {what} with my own eyes, out by {where}. Rare as hen's teeth.", "If you're hunting trophies, {what} was seen near {where}."};
        return FillAll(bounty ? PickOf(b, r) : PickOf(c, r), {{"what", Or(what, "something")}, {"where", Or(where, "the hills")}});
    }

    std::string NoRumourLine(uint32_t seed)
    {
        static std::array<char const*, 3> const l{"Quiet roads lately. Can't complain.", "Nothing worth repeating, friend.",
            "Only that the inn's ale is watered. Again."};
        return PickOf(l, Mix(seed ^ 0x4040u));
    }

    std::string EventLine(NewsKind k, Talk const& t, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0xE7E7u);
        static std::array<char const*, 3> const braveAir{"Look at that thing fly! Whose is it?", "Now that's a machine. Gnomes, I'll bet.",
            "Ha! Give them hell up there!"};
        static std::array<char const*, 3> const scaredAir{"Light preserve us! What is that?", "Get down! Something's in the sky!",
            "That noise! Is it a dragon?"};
        static std::array<char const*, 2> const build{"Building out here? Brave, {player}.", "Walls going up by {here}. The world's changing."};
        if (k == NewsKind::Aircraft)
            return FillAll(t.traits.brave >= 50 ? PickOf(braveAir, r) : PickOf(scaredAir, r), VarsOf(t));
        if (k == NewsKind::Building)
            return FillAll(PickOf(build, r), VarsOf(t));
        return NewsLine(t, seed);
    }

    std::string FleeLine(Talk const& t, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0xF1EEu);
        static std::array<char const*, 3> const named{"{player}! Help me, please!", "{player}, it's on me! Help!",
            "Help, {player}! I can't fight this!"};
        static std::array<char const*, 3> const anyone{"Help! Somebody!", "I'm not paid for this! Run!", "Guards! Anyone!"};
        return t.player.empty() ? std::string(PickOf(anyone, r)) : FillAll(PickOf(named, r), VarsOf(t));
    }

    std::string ThanksLine(Talk const& t, uint32_t seed)
    {
        static std::array<char const*, 3> const l{"Thank you, {player}. I owe you one.", "{player}, you saved my life. I won't forget it.",
            "Good shot, {player}! Drinks are on me at {home}."};
        return FillAll(PickOf(l, Mix(seed ^ 0x7A7Au)), VarsOf(t));
    }

    std::string ActivityLine(Talk const& t, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0xAC7Au);
        static std::array<char const*, 3> const fish{"Come on, bite...", "Shh, you'll scare them off.", "The big one's down there, I know it."};
        static std::array<char const*, 2> const mine{"Good vein, this.", "Mind your head, the roof's low."};
        static std::array<char const*, 2> const herbs{"Peacebloom, and there's more by the stream.", "Careful where you step."};
        static std::array<char const*, 2> const trap{"Fresh tracks. Good.", "Nothing in this one. Next."};
        static std::array<char const*, 3> const sell{"Fresh goods, fair prices!", "Come, look! Straight from {work}!",
            "Buy now, before the merchants do!"};
        static std::array<char const*, 3> const perform{"A song for the room! This one's about {home}.",
            "Requests? No? Then you'll have the one about the murloc.", "Join in on the chorus, you lot!"};
        static std::array<char const*, 2> const eat{"Pass the bread, would you?", "Best stew this side of {market}."};
        static std::array<char const*, 2> const social{"Another round! On {friend}!", "So there I was, {doing}..."};
        static std::array<char const*, 2> const sight{"Would you look at that.", "Worth the walk. Every step."};
        static std::array<char const*, 2> const rest{"Just a moment's rest.", "My feet thank me."};
        char const* line = nullptr;
        switch (t.doing)
        {
            case Activity::Work:
                switch (t.occupation)
                {
                    case Occupation::Fisherman: line = PickOf(fish, r); break;
                    case Occupation::Miner: line = PickOf(mine, r); break;
                    case Occupation::Herbalist: line = PickOf(herbs, r); break;
                    case Occupation::Trapper: line = PickOf(trap, r); break;
                    default: line = PickOf(rest, r); break;
                }
                break;
            case Activity::Sell:
            case Activity::Stock: line = PickOf(sell, r); break;
            case Activity::Perform: line = PickOf(perform, r); break;
            case Activity::Eat: line = PickOf(eat, r); break;
            case Activity::Socialize: line = PickOf(social, r); break;
            case Activity::Sightsee: line = PickOf(sight, r); break;
            case Activity::Sleep: return std::string("Zzz...");
            default: line = PickOf(rest, r); break;
        }
        return FillAll(line, VarsOf(t));
    }

    std::string HireLine(Talk const& t, bool accepted, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0x41E5u);
        static std::array<char const*, 3> const yes{"Lead on, {player}. I've got your back.", "Coin's good. Let's go, {player}.",
            "A {job} with company. Why not?"};
        static std::array<char const*, 3> const no{"Not for all the gold in Stormwind, {player}.", "I've my own road to walk.",
            "You? After what you did? No."};
        return FillAll(accepted ? PickOf(yes, r) : PickOf(no, r), VarsOf(t));
    }

    std::string Backstory(Occupation o, uint8_t race, std::string const& home, Traits const& t, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0xBAC5u);
        static std::array<char const*, 4> const roots{"Born in {home}, never far from it.", "I came to {home} as a child and stayed.",
            "{home} took me in when I'd nowhere else.", "Grew up on the road; {home} is the nearest thing to home."};
        static std::array<char const*, 3> const orcRoots{"Raised in the camps, I made {home} my own.",
            "My clan scattered; {home} is where I put down my pack.", "The Horde gave me {home}. I give it my work."};
        static std::array<char const*, 3> const undeadRoots{"I don't remember much before {home}. Better that way.",
            "I woke in the dark and walked till I found {home}.", "{home} doesn't stare. That's why I stay."};
        static std::array<char const*, 3> const why{"My father was a {job}, and his before him.", "Took up {trade} after the war took everything else.",
            "A {job} taught me the trade for a debt I owed."};
        static std::array<char const*, 3> const nature{"I like quiet roads and good company.", "I'll talk your ear off, given half a chance.",
            "Coin's coin, and I mean to have mine."};
        char const* root = race == kOrc || race == kTroll || race == kTauren ? PickOf(orcRoots, r)
            : race == kUndead ? PickOf(undeadRoots, r) : PickOf(roots, r);
        std::string line = std::string(root) + " " + PickOf(why, r >> 8);
        if (t.chatty > 70)
            line += std::string(" ") + nature[1];
        else if (t.greedy > 75)
            line += std::string(" ") + nature[2];
        else if (t.sociable > 70)
            line += std::string(" ") + nature[0];
        Talk talk;
        talk.home = home;
        talk.occupation = o == Occupation::None ? Occupation::Sellsword : o;
        return FillAll(line, VarsOf(talk));
    }

    bool LineIsClean(std::string const& line)
    {
        return !line.empty() && line.size() <= 255 && line.find('{') == std::string::npos && line.find('}') == std::string::npos;
    }

    bool MaySpeak(ChatBudget& b, uint32_t nowMs, uint32_t perMinute)
    {
        if (int32_t(nowMs - b.windowMs) >= 60000 || b.windowMs == 0)
        {
            b.windowMs = nowMs ? nowMs : 1;
            b.used = 0;
        }
        if (b.used >= perMinute)
            return false;
        ++b.used;
        return true;
    }

    // ── Its whole life, kept ────────────────────────────────────────────────────────────────

    Life NewLife(uint32_t seed, Occupation o, uint8_t race, std::string const& homeName, uint32_t today, uint32_t lifeDays)
    {
        Life l;
        l.occupation = o;
        l.traits = TraitsFor(seed, o);
        l.needs.mood = uint8_t(45 + R(seed, 0x300) % 35);
        l.needs.fatigue = uint8_t(R(seed, 0x301) % 30);
        l.needs.hunger = uint8_t(R(seed, 0x302) % 30);
        l.homeName = homeName;
        l.born = today;
        if (lifeDays)
        {
            uint32_t const third = std::max<uint32_t>(1, lifeDays / 3);
            l.retireDay = today + lifeDays - third + R(seed, 0x303) % (2 * third + 1);
        }
        l.backstory = Backstory(o, race, homeName, l.traits, seed);
        return l;
    }

    bool Retires(Life const& l, uint32_t today) { return l.retireDay && today >= l.retireDay; }

    std::string EncodeLife(Life const& l)
    {
        std::ostringstream s;
        Traits const& t = l.traits;
        s << "L1|" << uint32_t(l.occupation) << "|" << uint32_t(t.brave) << "," << uint32_t(t.chatty) << "," << uint32_t(t.greedy) << ","
          << uint32_t(t.curious) << "," << uint32_t(t.lawful) << "," << uint32_t(t.sociable) << "|" << uint32_t(l.needs.mood) << ","
          << uint32_t(l.needs.fatigue) << "," << uint32_t(l.needs.hunger) << "|" << l.home << "," << l.work << "," << l.market << "|"
          << Clean(l.homeName) << "|";
        for (size_t i = 0; i < l.pack.size(); ++i)
            s << (i ? ";" : "") << l.pack[i].item << "," << l.pack[i].count << "," << (l.pack[i].rare ? 1 : 0);
        s << "|" << uint32_t(l.doing) << "," << l.doingNode << "," << uint32_t(l.sessions) << "," << (l.lastWasDetour ? 1 : 0) << "|"
          << l.parcel.from << "," << l.parcel.to << "," << l.parcel.pay << "," << Clean(l.parcel.sender) << "|";
        for (size_t i = 0; i < l.news.size(); ++i)
            s << (i ? ";" : "") << uint32_t(l.news[i].kind) << "," << Clean(l.news[i].what) << "," << Clean(l.news[i].where) << ","
              << l.news[i].day;
        s << "|";
        for (size_t i = 0; i < l.ties.size(); ++i)
            s << (i ? ";" : "") << l.ties[i].other << "," << int(l.ties[i].affinity) << "," << l.ties[i].met;
        s << "|" << l.born << "," << l.retireDay << "," << l.generation << "," << l.earned << "|" << Bare(l.backstory);
        return s.str();
    }

    bool DecodeLife(std::string const& text, Life& l)
    {
        std::vector<std::string> const f = Split(text, '|');
        if (f.size() != 13 || f[0] != "L1")
            return false;
        Life out;
        uint32_t v = 0;
        if (!ToU(f[1], v, uint32_t(Occupation::Count) - 1))
            return false;
        out.occupation = Occupation(v);
        {
            std::vector<std::string> const t = Split(f[2], ',');
            uint32_t x[6];
            if (t.size() != 6)
                return false;
            for (size_t i = 0; i < 6; ++i)
                if (!ToU(t[i], x[i], 100))
                    return false;
            out.traits = {uint8_t(x[0]), uint8_t(x[1]), uint8_t(x[2]), uint8_t(x[3]), uint8_t(x[4]), uint8_t(x[5])};
        }
        {
            std::vector<std::string> const n = Split(f[3], ',');
            uint32_t a, b, c;
            if (n.size() != 3 || !ToU(n[0], a, 100) || !ToU(n[1], b, 100) || !ToU(n[2], c, 100))
                return false;
            out.needs = {uint8_t(a), uint8_t(b), uint8_t(c)};
        }
        {
            std::vector<std::string> const h = Split(f[4], ',');
            if (h.size() != 3 || !ToU(h[0], out.home) || !ToU(h[1], out.work) || !ToU(h[2], out.market))
                return false;
        }
        out.homeName = f[5];
        if (!f[6].empty())
            for (std::string const& e : Split(f[6], ';'))
            {
                std::vector<std::string> const p = Split(e, ',');
                uint32_t item, count, rare;
                if (p.size() != 3 || !ToU(p[0], item) || !ToU(p[1], count, kMaxStack) || !ToU(p[2], rare, 1) || out.pack.size() >= kMaxStock)
                    return false;
                if (item && count)
                    out.pack.push_back({item, uint16_t(count), rare == 1});
            }
        {
            std::vector<std::string> const d = Split(f[7], ',');
            uint32_t a, node, sessions, detour;
            if (d.size() != 4 || !ToU(d[0], a, uint32_t(Activity::Count) - 1) || !ToU(d[1], node) || !ToU(d[2], sessions, 255)
                || !ToU(d[3], detour, 1))
                return false;
            out.doing = Activity(a);
            out.doingNode = node;
            out.sessions = uint8_t(sessions);
            out.lastWasDetour = detour == 1;
        }
        {
            std::vector<std::string> const p = Split(f[8], ',');
            if (p.size() != 4 || !ToU(p[0], out.parcel.from) || !ToU(p[1], out.parcel.to) || !ToU(p[2], out.parcel.pay))
                return false;
            out.parcel.sender = p[3];
        }
        if (!f[9].empty())
            for (std::string const& e : Split(f[9], ';'))
            {
                std::vector<std::string> const p = Split(e, ',');
                uint32_t kind, day;
                if (p.size() != 4 || !ToU(p[0], kind, uint32_t(NewsKind::Count) - 1) || !ToU(p[3], day) || out.news.size() >= kMaxNews)
                    return false;
                out.news.push_back({NewsKind(kind), p[1], p[2], day});
            }
        if (!f[10].empty())
            for (std::string const& e : Split(f[10], ';'))
            {
                std::vector<std::string> const p = Split(e, ',');
                uint32_t other, met;
                int aff;
                if (p.size() != 3 || !ToU(p[0], other) || !ToI(p[1], aff, -100, 100) || !ToU(p[2], met, 0xFFFF) || out.ties.size() >= kMaxTies)
                    return false;
                out.ties.push_back({other, int8_t(aff), uint16_t(met)});
            }
        {
            std::vector<std::string> const b = Split(f[11], ',');
            if (b.size() != 4 || !ToU(b[0], out.born) || !ToU(b[1], out.retireDay) || !ToU(b[2], out.generation) || !ToU(b[3], out.earned))
                return false;
        }
        out.backstory = f[12];
        l = std::move(out);
        return true;
    }
}
