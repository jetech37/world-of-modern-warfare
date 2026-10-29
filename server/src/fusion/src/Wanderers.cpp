/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Wanderers.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace Fusion::Wanderers
{
    namespace
    {
        bool Due(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }

        float Dist(Vec3 a, Vec3 b) { return (a - b).Length(); }

        template <size_t N>
        char const* Pick(std::array<char const*, N> const& list, uint32_t roll)
        {
            return list[roll % N];
        }

        // ── Names ───────────────────────────────────────────────────────────────────────────
        // Each race's sound: a start, and a male or a female ending. Ours, not lore names.
        struct Syllables
        {
            std::array<char const*, 12> starts;
            std::array<char const*, 8> male;
            std::array<char const*, 8> female;
        };

        Syllables const& SyllablesOf(uint8_t race)
        {
            static Syllables const human{{"Al", "Bren", "Cor", "Da", "Ed", "Gar", "Hal", "Jor", "Ker", "Lan", "Mar", "Tam"},
                {"ric", "den", "win", "ton", "las", "mond", "bert", "an"},
                {"a", "ine", "elle", "wyn", "ra", "issa", "eth", "ora"}};
            static Syllables const dwarf{{"Bram", "Dur", "Gor", "Thra", "Mag", "Bel", "Kor", "Hur", "Brog", "Dag", "Grim", "Tor"},
                {"in", "ar", "dan", "grim", "nor", "mek", "ok", "ur"},
                {"a", "ra", "hild", "wyn", "ga", "dis", "na", "li"}};
            static Syllables const gnome{{"Fiz", "Bim", "Nix", "Tink", "Gim", "Wiz", "Pip", "Dob", "Zap", "Mek", "Lim", "Spin"},
                {"zle", "bin", "wick", "ick", "ock", "nob", "ble", "set"},
                {"ka", "pie", "sy", "ette", "li", "bell", "ni", "wee"}};
            static Syllables const elf{{"Ael", "Ily", "Syl", "Thal", "Fen", "Lor", "Ara", "Ely", "Mae", "Shan", "Vel", "Ner"},
                {"dris", "anor", "ion", "ras", "thil", "aran", "oth", "ael"},
                {"dra", "ia", "wen", "lia", "ise", "ana", "riel", "ssa"}};
            static Syllables const orc{{"Gro", "Kar", "Thu", "Mog", "Dru", "Gash", "Urk", "Rag", "Zug", "Bru", "Nar", "Gol"},
                {"nak", "gar", "tok", "rash", "dak", "gul", "mok", "zug"},
                {"ka", "sha", "ga", "ra", "ena", "ma", "tha", "zi"}};
            static Syllables const troll{{"Zun", "Jak", "Vo", "Ra", "Tak", "Zen", "Mun", "Kal", "Ji", "Sen", "Za", "Rok"},
                {"jin", "tak", "zal", "ku", "ro", "dan", "jo", "mon"},
                {"ja", "zi", "la", "ka", "shi", "na", "ri", "ya"}};
            static Syllables const undead{{"Mor", "Gra", "Vex", "Sor", "Ves", "Cal", "Dra", "Mal", "Ren", "Lys", "Ab", "Ul"},
                {"dred", "vin", "ric", "eth", "as", "ius", "ard", "ok"},
                {"a", "eth", "ys", "ine", "ra", "is", "elle", "anne"}};
            static Syllables const tauren{{"Hu", "Ta", "Ma", "Ka", "Tu", "Ba", "Ha", "Mo", "Na", "Ru", "Sha", "Wa"},
                {"rn", "kan", "tah", "ru", "nok", "mah", "gan", "ho"},
                {"na", "wa", "li", "ra", "ha", "ki", "shi", "ya"}};
            switch (race)
            {
                case kDwarf: return dwarf;
                case kGnome: return gnome;
                case kNightElf: return elf;
                case kOrc: return orc;
                case kTroll: return troll;
                case kUndead: return undead;
                case kTauren: return tauren;
                default: return human;
            }
        }

        // Names the generator could make that belong to the world's own characters.
        bool LoreName(std::string const& n)
        {
            static char const* const lore[] = {"Zunjin", "Voljin", "Thrall", "Rexxar", "Garona", "Durotan", "Malfurion",
                "Tyrande", "Jaina", "Anduin", "Varian", "Magni", "Gelbin", "Sylvanas", "Cairne", "Baine", "Grom"};
            for (char const* l : lore)
                if (n == l)
                    return true;
            return false;
        }

        // ── The saved state's text ──────────────────────────────────────────────────────────
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

        bool ToU32(std::string const& s, uint32_t& v)
        {
            if (s.empty() || s.size() > 10 || s.find_first_not_of("0123456789") != std::string::npos)
                return false;
            unsigned long long const x = std::strtoull(s.c_str(), nullptr, 10);
            if (x > 0xFFFFFFFFull)
                return false;
            v = uint32_t(x);
            return true;
        }

        bool ToF(std::string const& s, float& v)
        {
            if (s.empty() || s.size() > 16 || s.find_first_not_of("-0123456789.") != std::string::npos)
                return false;
            char* end = nullptr;
            double const x = std::strtod(s.c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(x) || std::fabs(x) > 1e6)
                return false;
            v = float(x);
            return true;
        }

        std::string F1(float v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", double(v));
            return buf;
        }

        bool ToList(std::string const& s, std::vector<uint32_t>& out, size_t max)
        {
            out.clear();
            if (s.empty())
                return true;
            for (std::string const& p : Split(s, ','))
            {
                uint32_t v = 0;
                if (!ToU32(p, v) || out.size() >= max)
                    return false;
                out.push_back(v);
            }
            return true;
        }

        std::string List(std::vector<uint32_t> const& v)
        {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (i)
                    s += ',';
                s += std::to_string(v[i]);
            }
            return s;
        }

        bool ToVec(std::string const& s, Vec3& v)
        {
            std::vector<std::string> const p = Split(s, ',');
            return p.size() == 3 && ToF(p[0], v.x) && ToF(p[1], v.y) && ToF(p[2], v.z);
        }

        std::string Vec(Vec3 v) { return F1(v.x) + "," + F1(v.y) + "," + F1(v.z); }

        std::string Clean(std::string const& s)
        {
            std::string out;
            for (char c : s)
                if (c != '|' && c != ';' && c != ',' && c != '\n' && c != '\r')
                    out += c;
            return out.substr(0, 96);
        }

        // Every node's route length from `from` (Dijkstra; the networks are small).
        std::vector<float> Distances(RoadNet const& net, int from, std::vector<int>* previous = nullptr)
        {
            size_t const n = net.nodes.size();
            std::vector<float> d(n, std::numeric_limits<float>::infinity());
            std::vector<int> prev(n, -1);
            std::vector<bool> done(n, false);
            if (from < 0 || size_t(from) >= n)
                return d;
            d[size_t(from)] = 0.f;
            for (size_t round = 0; round < n; ++round)
            {
                int best = -1;
                for (size_t i = 0; i < n; ++i)
                    if (!done[i] && std::isfinite(d[i]) && (best < 0 || d[i] < d[size_t(best)]))
                        best = int(i);
                if (best < 0)
                    break;
                done[size_t(best)] = true;
                for (Link const& l : net.links[size_t(best)])
                {
                    float const nd = d[size_t(best)] + l.length;
                    if (nd < d[size_t(l.to)])
                    {
                        d[size_t(l.to)] = nd;
                        prev[size_t(l.to)] = best;
                    }
                }
            }
            if (previous)
                *previous = std::move(prev);
            return d;
        }

        bool IsPlace(NodeKind k) { return IsSettlement(k); }
    }

    bool IsSettlement(NodeKind k)
    {
        return k == NodeKind::Town || k == NodeKind::Inn || k == NodeKind::Board || k == NodeKind::Market;
    }

    bool IsWorkSpot(NodeKind k)
    {
        return k == NodeKind::Fishing || k == NodeKind::Mine || k == NodeKind::Herbs || k == NodeKind::Wilds || k == NodeKind::Landmark;
    }

    char const* NodeKindName(NodeKind k)
    {
        switch (k)
        {
            case NodeKind::Town: return "town";
            case NodeKind::Inn: return "inn";
            case NodeKind::Board: return "board";
            case NodeKind::Road: return "road";
            case NodeKind::Fishing: return "fishing";
            case NodeKind::Mine: return "mine";
            case NodeKind::Herbs: return "herbs";
            case NodeKind::Wilds: return "wilds";
            case NodeKind::Market: return "market";
            case NodeKind::Landmark: return "landmark";
            case NodeKind::Count: break;
        }
        return "?";
    }

    // ── Who a wanderer is ───────────────────────────────────────────────────────────────────

    std::optional<Side> SideOfRace(uint8_t race)
    {
        switch (race)
        {
            case kHuman: case kDwarf: case kNightElf: case kGnome: return Side::Alliance;
            case kOrc: case kUndead: case kTauren: case kTroll: return Side::Horde;
            default: return std::nullopt;
        }
    }

    uint8_t RaceFor(Side side, uint32_t seed)
    {
        static std::array<uint8_t, 4> const alliance{kHuman, kDwarf, kNightElf, kGnome};
        static std::array<uint8_t, 4> const horde{kOrc, kUndead, kTauren, kTroll};
        uint32_t const r = Mix(seed ^ 0x51ED270Bu) % 4;
        return side == Side::Horde ? horde[r] : alliance[r];
    }

    char const* ArchetypeTitle(Archetype a)
    {
        switch (a)
        {
            case Archetype::Rifleman: return "Rifleman";
            case Archetype::Scout: return "Scout";
            case Archetype::Sharpshooter: return "Sharpshooter";
            case Archetype::Brawler: return "Brawler";
            case Archetype::Gunslinger: return "Gunslinger";
            default: return "Wanderer";
        }
    }

    Archetype ArchetypeFor(uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0xA5A5F00Du) % 100;
        if (r < 35)
            return Archetype::Rifleman;
        if (r < 55)
            return Archetype::Scout;
        if (r < 70)
            return Archetype::Sharpshooter;
        if (r < 85)
            return Archetype::Brawler;
        return Archetype::Gunslinger;
    }

    std::string GunFor(Archetype a, uint8_t level, uint32_t seed)
    {
        uint32_t const r = Mix(seed ^ 0x6A09E667u);
        auto from = [r](std::initializer_list<char const*> list) {
            return std::string(*(list.begin() + r % list.size()));
        };
        switch (a)
        {
            case Archetype::Scout:
                return level < 20 ? from({"pp2000_mp", "mp5k_mp", "ump45_mp", "uzi_mp"}) : from({"kriss_mp", "ump45_mp", "p90_mp", "mp5k_mp"});
            case Archetype::Sharpshooter:
                if (level < 20)
                    return from({"fal_mp", "m16_mp"});
                return level < 40 ? from({"m21_mp", "wa2000_mp"}) : from({"barrett_mp", "cheytac_mp", "wa2000_mp"});
            case Archetype::Brawler:
                return level < 20 ? from({"spas12_mp", "model1887_mp"}) : from({"spas12_mp", "m1014_mp", "striker_mp", "aa12_mp"});
            case Archetype::Gunslinger:
                return level < 20 ? from({"usp_mp", "beretta_mp", "glock_mp"}) : from({"deserteagle_mp", "coltanaconda_mp", "tmp_mp"});
            case Archetype::Rifleman:
            default:
                if (level < 10)
                    return from({"famas_mp", "m16_mp", "m4_mp"});
                return level < 30 ? from({"m4_mp", "famas_mp", "fal_mp", "aug_mp"})
                                  : from({"ak47_mp", "scar_mp", "tavor_mp", "fn2000_mp", "masada_mp"});
        }
    }

    float HoldRange(Archetype a)
    {
        switch (a)
        {
            case Archetype::Scout: return 14.f;
            case Archetype::Sharpshooter: return 32.f;
            case Archetype::Brawler: return 8.f;
            case Archetype::Gunslinger: return 14.f;
            default: return 22.f;
        }
    }

    uint32_t Mix(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }

    uint32_t Seed(uint32_t id, uint32_t salt)
    {
        return Mix(id * 2654435761u + salt * 40503u + 0x9E3779B9u);
    }

    std::string MakeName(uint8_t race, uint8_t gender, uint32_t seed)
    {
        Syllables const& s = SyllablesOf(race);
        for (uint32_t tries = 0;; ++tries)
        {
            uint32_t const a = Mix(seed + tries * 7919u);
            uint32_t const b = Mix(a ^ 0x2545F491u);
            std::string name = Pick(s.starts, a);
            name += gender ? Pick(s.female, b) : Pick(s.male, b);
            // Now and then a third, joining syllable (a longer name), if it stays within 12.
            if (Mix(b) % 4 == 0)
            {
                std::string const mid = gender ? Pick(s.female, Mix(b + 1)) : Pick(s.male, Mix(b + 1));
                if (name.size() + mid.size() <= 12)
                    name = Pick(s.starts, a) + mid + (gender ? Pick(s.female, b) : Pick(s.male, b));
            }
            for (size_t i = 1; i < name.size(); ++i)
                name[i] = char(std::tolower(static_cast<unsigned char>(name[i])));
            if (name.size() > 12)
                name.resize(12);
            if ((ValidName(name) && !LoreName(name)) || tries > 16)
                return name;
        }
    }

    bool ValidName(std::string const& name)
    {
        if (name.size() < 2 || name.size() > 12 || !std::isupper(static_cast<unsigned char>(name[0])))
            return false;
        for (size_t i = 1; i < name.size(); ++i)
            if (!std::islower(static_cast<unsigned char>(name[i])))
                return false;
        return true;
    }

    // ── Levels ──────────────────────────────────────────────────────────────────────────────

    uint8_t StartLevel(ZoneBand const& band, uint32_t seed)
    {
        uint8_t const lo = std::max<uint8_t>(1, std::min(band.minLevel, band.maxLevel));
        uint8_t const hi = std::min<uint8_t>(60, std::max(band.minLevel, band.maxLevel));
        return uint8_t(lo + Mix(seed ^ 0xBB67AE85u) % uint32_t(hi - lo + 1));
    }

    uint8_t LevelCap(ZoneBand const& band)
    {
        return uint8_t(std::min<uint32_t>(60, uint32_t(band.maxLevel) + 2));
    }

    uint32_t KillXp(uint8_t level, uint8_t victimLevel)
    {
        if (uint32_t(victimLevel) + 5 <= level)
            return 0;
        float xp = 45.f + 5.f * float(victimLevel);
        if (victimLevel > level)
            xp *= 1.f + 0.05f * float(std::min(4, int(victimLevel) - int(level)));
        else
            xp *= 1.f - 0.15f * float(int(level) - int(victimLevel));
        return uint32_t(std::max(0.f, xp) + 0.5f);
    }

    uint32_t XpToLevel(uint8_t level)
    {
        uint32_t const l = std::max<uint32_t>(1, level);
        return 40u * l * l + 360u * l;
    }

    bool GainXp(uint8_t& level, uint32_t& xp, uint32_t gained, uint8_t cap)
    {
        if (level >= cap)
        {
            xp = 0;
            return false;
        }
        bool leveled = false;
        xp += gained;
        while (level < cap && xp >= XpToLevel(level))
        {
            xp -= XpToLevel(level);
            ++level;
            leveled = true;
        }
        if (level >= cap)
            xp = 0;
        return leveled;
    }

    uint32_t LootCopper(uint8_t victimLevel, uint32_t roll)
    {
        float const l = float(victimLevel);
        float const base = 2.f + l + 0.15f * l * l;
        return uint32_t(base * float(75 + roll % 51) / 100.f + 0.5f);
    }

    // ── Roads ───────────────────────────────────────────────────────────────────────────────

    int RoadNet::IndexOf(uint32_t id) const
    {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].id == id)
                return int(i);
        return -1;
    }

    bool RoadNet::Linked(int a, int b) const
    {
        if (a < 0 || size_t(a) >= links.size())
            return false;
        for (Link const& l : links[size_t(a)])
            if (l.to == b)
                return true;
        return false;
    }

    size_t RoadNet::LinkCount() const
    {
        size_t n = 0;
        for (auto const& l : links)
            n += l.size();
        return n / 2;
    }

    RoadNet BuildRoads(std::vector<Node> nodes, std::vector<std::pair<uint32_t, uint32_t>> const& authored)
    {
        RoadNet net;
        // One node per id (a later duplicate is dropped).
        for (Node& n : nodes)
            if (net.IndexOf(n.id) < 0)
                net.nodes.push_back(std::move(n));
        size_t const count = net.nodes.size();
        net.links.assign(count, {});
        auto join = [&](int a, int b) {
            if (a < 0 || b < 0 || a == b || net.Linked(a, b))
                return;
            float const len = Dist(net.nodes[size_t(a)].pos, net.nodes[size_t(b)].pos);
            net.links[size_t(a)].push_back({b, len});
            net.links[size_t(b)].push_back({a, len});
        };
        for (auto const& [a, b] : authored)
            join(net.IndexOf(a), net.IndexOf(b));
        if (count < 2)
            return net;
        // A spanning tree of the shortest legs (Prim): every node reachable.
        std::vector<bool> in(count, false);
        std::vector<float> best(count, std::numeric_limits<float>::infinity());
        std::vector<int> from(count, -1);
        best[0] = 0.f;
        for (size_t round = 0; round < count; ++round)
        {
            int pick = -1;
            for (size_t i = 0; i < count; ++i)
                if (!in[i] && (pick < 0 || best[i] < best[size_t(pick)]))
                    pick = int(i);
            in[size_t(pick)] = true;
            if (from[size_t(pick)] >= 0)
                join(from[size_t(pick)], pick);
            for (size_t i = 0; i < count; ++i)
            {
                if (in[i])
                    continue;
                float const d = Dist(net.nodes[size_t(pick)].pos, net.nodes[i].pos);
                if (d < best[i])
                {
                    best[i] = d;
                    from[i] = pick;
                }
            }
        }
        // And the near neighbours, so a town has more than one way out.
        for (size_t a = 0; a < count; ++a)
            for (size_t b = a + 1; b < count; ++b)
                if (Dist(net.nodes[a].pos, net.nodes[b].pos) <= kNeighbourLink)
                    join(int(a), int(b));
        return net;
    }

    std::vector<int> Route(RoadNet const& net, int from, int to)
    {
        size_t const n = net.nodes.size();
        if (from < 0 || to < 0 || size_t(from) >= n || size_t(to) >= n)
            return {};
        if (from == to)
            return {from};
        std::vector<int> prev;
        std::vector<float> const d = Distances(net, from, &prev);
        if (!std::isfinite(d[size_t(to)]))
            return {};
        std::vector<int> path;
        for (int at = to; at >= 0; at = prev[size_t(at)])
        {
            path.push_back(at);
            if (at == from)
                break;
        }
        std::reverse(path.begin(), path.end());
        return path.front() == from ? path : std::vector<int>{};
    }

    int NearestNode(RoadNet const& net, Vec3 p, bool placesOnly)
    {
        int best = -1;
        float bestD = 0.f;
        for (size_t i = 0; i < net.nodes.size(); ++i)
        {
            if (placesOnly && !IsPlace(net.nodes[i].kind))
                continue;
            float const d = Dist(net.nodes[i].pos, p);
            if (best < 0 || d < bestD)
            {
                best = int(i);
                bestD = d;
            }
        }
        return best;
    }

    int ChooseDestination(RoadNet const& net, int at, std::vector<int> const& recent, uint32_t homeZone, uint32_t roll)
    {
        std::vector<float> const d = Distances(net, at);
        std::vector<std::pair<int, float>> weights;
        float total = 0.f;
        auto collect = [&](bool skipRecent) {
            weights.clear();
            total = 0.f;
            for (size_t i = 0; i < net.nodes.size(); ++i)
            {
                Node const& n = net.nodes[i];
                if (int(i) == at || !IsPlace(n.kind) || (at >= 0 && !std::isfinite(d[i])))
                    continue;
                if (skipRecent && std::find(recent.begin(), recent.end(), int(i)) != recent.end())
                    continue;
                float w = 1.f / (1.f + (at >= 0 ? d[i] : 0.f) / 400.f);
                if (homeZone && n.zone == homeZone)
                    w *= 3.f;
                weights.push_back({int(i), w});
                total += w;
            }
        };
        collect(true);
        // Everywhere it can go it has just been: anywhere but here.
        if (weights.empty())
            collect(false);
        if (weights.empty() || total <= 0.f)
            return -1;
        float pick = float(Mix(roll) % 100000u) / 100000.f * total;
        for (auto const& [i, w] : weights)
        {
            if (pick < w)
                return i;
            pick -= w;
        }
        return weights.back().first;
    }

    void Remember(std::vector<int>& recent, int node)
    {
        recent.erase(std::remove(recent.begin(), recent.end(), node), recent.end());
        recent.push_back(node);
        while (recent.size() > kRecentPlaces)
            recent.erase(recent.begin());
    }

    std::vector<Vec3> Steps(Vec3 a, Vec3 b, float step)
    {
        float const len = Dist(a, b);
        if (len < 1e-3f || step <= 0.f)
            return {b};
        uint32_t const n = std::max<uint32_t>(1, uint32_t(std::ceil(len / step)));
        std::vector<Vec3> out;
        out.reserve(n);
        for (uint32_t i = 1; i <= n; ++i)
            out.push_back(i == n ? b : a + (b - a) * (float(i) / float(n)));
        return out;
    }

    Vec3 TripPoint(RoadNet const& net, Trip const& t, size_t i)
    {
        if (i < t.route.size())
        {
            int const idx = net.IndexOf(t.route[i]);
            if (idx >= 0)
                return net.nodes[size_t(idx)].pos;
            return net.nodes.empty() ? Vec3{} : net.nodes.front().pos;
        }
        if (t.hasEnd)
            return t.end;
        return t.route.empty() ? Vec3{} : TripPoint(net, t, t.route.size() - 1);
    }

    Vec3 TripPos(RoadNet const& net, Trip const& t)
    {
        if (t.Points() == 0)
            return {};
        if (t.Done())
            return TripPoint(net, t, t.Points() - 1);
        Vec3 const a = TripPoint(net, t, t.leg);
        Vec3 const b = TripPoint(net, t, t.leg + 1);
        float const len = Dist(a, b);
        return len < 1e-3f ? b : a + (b - a) * Clamp(t.along / len, 0.f, 1.f);
    }

    bool Advance(RoadNet const& net, Trip& t, float yards)
    {
        while (yards > 0.f && !t.Done())
        {
            float const len = Dist(TripPoint(net, t, t.leg), TripPoint(net, t, t.leg + 1));
            float const left = len - t.along;
            if (yards >= left)
            {
                yards -= std::max(0.f, left);
                ++t.leg;
                t.along = 0.f;
            }
            else
            {
                t.along += yards;
                yards = 0.f;
            }
        }
        return t.Done();
    }

    Trip PlanTrip(RoadNet const& net, int fromNode, int to, std::optional<Vec3> end)
    {
        Trip t;
        for (int i : Route(net, fromNode, to))
            t.route.push_back(net.nodes[size_t(i)].id);
        if (end && !t.route.empty())
        {
            t.hasEnd = true;
            t.end = *end;
        }
        return t;
    }

    // ── What it does ────────────────────────────────────────────────────────────────────────

    char const* StateName(State s)
    {
        switch (s)
        {
            case State::Idle: return "idle";
            case State::Travel: return "travelling";
            case State::Fight: return "fighting";
            case State::Loot: return "looting";
            case State::Rest: return "resting";
            case State::Greet: return "greeting";
            case State::Dead: return "dead";
            case State::Flee: return "fleeing";
        }
        return "?";
    }

    char const* GoalName(Goal g)
    {
        switch (g)
        {
            case Goal::Roam: return "roaming";
            case Goal::TakeBounty: return "to a bounty board";
            case Goal::Hunt: return "on a bounty hunt";
            case Goal::TurnIn: return "turning a bounty in";
        }
        return "?";
    }

    char const* ActName(Act a)
    {
        switch (a)
        {
            case Act::None: return "none";
            case Act::Engage: return "engage";
            case Act::CallForHelp: return "call for help";
            case Act::LootCorpse: return "loot corpse";
            case Act::Kneel: return "kneel";
            case Act::TakeLoot: return "take loot";
            case Act::Stand: return "stand";
            case Act::Sit: return "sit";
            case Act::Greet: return "greet";
            case Act::PlanTrip: return "plan trip";
            case Act::TakeBounty: return "take bounty";
            case Act::TurnIn: return "turn in";
            case Act::Walk: return "walk";
            case Act::Flee: return "flee";
            case Act::Work: return "work";
        }
        return "?";
    }

    bool Errand::Wants(uint32_t entry) const
    {
        if (!entry)
            return false;
        for (uint32_t e : entries)
            if (e == entry)
                return true;
        return false;
    }

    bool ShouldGreet(Mind const& m, uint64_t player, uint32_t nowMs)
    {
        for (auto const& [p, at] : m.greeted)
            if (p == player)
                return nowMs - at >= kGreetAgainMs;
        return true;
    }

    void NoteGreeted(Mind& m, uint64_t player, uint32_t nowMs)
    {
        for (auto& g : m.greeted)
            if (g.first == player)
            {
                g.second = nowMs;
                return;
            }
        m.greeted.push_back({player, nowMs});
        if (m.greeted.size() > kGreetMemory)
            m.greeted.erase(m.greeted.begin());
    }

    uint32_t RestMsAt(NodeKind kind, uint32_t roll)
    {
        uint32_t const r = Mix(roll ^ 0x3C6EF372u);
        if (kind == NodeKind::Inn)
            return kInnRestMinMs + r % (kInnRestMaxMs - kInnRestMinMs + 1);
        return kTownRestMinMs + r % (kTownRestMaxMs - kTownRestMinMs + 1);
    }

    Act Think(Mind& m, Sense const& s)
    {
        uint32_t const now = s.nowMs;
        if (m.state == State::Dead)
            return Act::None;

        // A fight first: whoever is on it, else what it chose to shoot.
        uint64_t const foe = s.attacker ? s.attacker : s.bestTarget;
        // Running: until it is clear, or it has run long enough (then, cornered, it fights).
        if (m.state == State::Flee)
        {
            if (!Due(now, m.untilMs) && s.attacker)
                return Act::None;
            m.state = State::Idle;
            m.target = 0;
        }
        // A fight it will not win, by its nature: it runs (once a fight), crying for help.
        if (s.attacker && !m.fled && s.fleeBelowPct > 0.f && s.healthPct < s.fleeBelowPct)
        {
            m.fled = true;
            m.calledForHelp = true;
            m.state = State::Flee;
            m.target = s.attacker;
            m.untilMs = now + kFleeMs;
            m.kneeling = false;
            m.corpse = 0;
            m.greet = 0;
            return Act::Flee;
        }
        if (!foe)
            m.fled = false;
        if (foe)
        {
            if (m.state != State::Fight || m.target != foe)
            {
                bool const wasFighting = m.state == State::Fight;
                m.state = State::Fight;
                m.target = foe;
                m.kneeling = false;
                m.corpse = 0;
                m.greet = 0;
                if (!wasFighting)
                    m.calledForHelp = false;
                return Act::Engage;
            }
            if (!m.calledForHelp && s.attacker && s.healthPct < kHelpBelowPct)
            {
                m.calledForHelp = true;
                return Act::CallForHelp;
            }
            return Act::None;
        }
        if (m.state == State::Fight)
        {
            m.target = 0;
            m.calledForHelp = false;
            if (s.killed)
            {
                m.state = State::Loot;
                m.corpse = s.killed;
                m.kneeling = false;
                m.untilMs = now + kLootWalkMs;
                return Act::LootCorpse;
            }
            m.state = State::Idle;
        }

        switch (m.state)
        {
            case State::Loot:
                if (!m.kneeling)
                {
                    if (s.atCorpse)
                    {
                        m.kneeling = true;
                        m.untilMs = now + kKneelMs;
                        return Act::Kneel;
                    }
                    if (!Due(now, m.untilMs))
                        return Act::None;
                    // It could not get to it: leave it.
                    m.state = State::Idle;
                    m.corpse = 0;
                    return Act::Stand;
                }
                if (!Due(now, m.untilMs))
                    return Act::None;
                m.kneeling = false;
                m.state = State::Idle;
                return Act::TakeLoot;
            case State::Rest:
                if (!Due(now, m.untilMs))
                    return Act::None;
                m.state = State::Idle;
                return Act::Stand;
            case State::Greet:
                if (!Due(now, m.untilMs))
                    return Act::None;
                m.greet = 0;
                m.state = s.arrived ? State::Idle : State::Travel;
                return Act::Walk;
            default:
                break;
        }

        // Idle or on the road.
        if (s.healthPct < kRestBelowPct)
        {
            m.state = State::Rest;
            m.untilMs = now + kRestMs;
            return Act::Sit;
        }
        if (s.playerNear && ShouldGreet(m, s.playerNear, now))
        {
            NoteGreeted(m, s.playerNear, now);
            m.state = State::Greet;
            m.greet = s.playerNear;
            m.untilMs = now + kGreetMs;
            return Act::Greet;
        }
        // A bounty given up: back to its own life.
        if ((m.goal == Goal::Hunt || m.goal == Goal::TurnIn) && Due(now, m.errand.untilMs))
        {
            m.goal = Goal::Roam;
            m.errand = Errand();
        }
        // On a trip: on with it (after a fight, a loot or a rest it picks it up again).
        if (!s.arrived)
        {
            if (m.state == State::Travel)
                return Act::None;
            m.state = State::Travel;
            return Act::Walk;
        }

        bool const fromTrip = m.state == State::Travel;
        m.state = State::Idle;
        switch (m.goal)
        {
            case Goal::TakeBounty:
                if (s.atBoard)
                    return Act::TakeBounty;
                break;
            case Goal::TurnIn:
                if (s.atBoard)
                    return Act::TurnIn;
                break;
            case Goal::Hunt:
                if (m.errand.Complete())
                    m.goal = Goal::TurnIn;
                break;
            case Goal::Roam:
                if (s.boardHasBounty && Due(now, m.nextBountyMs) && s.roll % 100 < kBountyChancePercent)
                {
                    m.goal = Goal::TakeBounty;
                    m.nextBountyMs = now + kBountyCooldownMs;
                }
                else if (fromTrip && s.atPlace && s.activityMs)
                {
                    // Where its life's plan sent it: the work, the sale, the meal, the sleep.
                    m.state = State::Rest;
                    m.untilMs = now + s.activityMs;
                    return Act::Work;
                }
                else if (fromTrip && s.atPlace)
                {
                    m.state = State::Rest;
                    m.untilMs = now + RestMsAt(s.placeKind, s.roll);
                    return Act::Sit;
                }
                break;
        }
        m.state = State::Travel;
        return Act::PlanTrip;
    }

    void Died(Mind& m, uint32_t nowMs)
    {
        m.state = State::Dead;
        m.untilMs = nowMs + kRespawnMs;
        m.target = 0;
        m.corpse = 0;
        m.greet = 0;
        m.kneeling = false;
        m.calledForHelp = false;
    }

    void Revived(Mind& m)
    {
        m.state = State::Idle;
        m.untilMs = 0;
    }

    // ── What it shoots ──────────────────────────────────────────────────────────────────────

    int ChooseTarget(std::vector<Candidate> const& c, TargetRules const& r)
    {
        int best = -1;
        float bestScore = 0.f;
        for (size_t i = 0; i < c.size(); ++i)
        {
            Candidate const& k = c[i];
            if (!k.guid)
                continue;
            if (!k.attackingMe)
            {
                if (!r.startsFights && !(r.hunting && k.bountyTarget))
                    continue;
                if (k.player || k.tappedByOther || k.elite || !k.inSight)
                    continue;
                if (int(k.level) > int(r.selfLevel) + int(r.maxLevelAbove))
                    continue;
                float const range = r.hunting && k.bountyTarget ? r.errandRange : r.engageRange;
                if (k.distance > range)
                    continue;
            }
            float score = -2.f * k.distance - 0.2f * k.healthPct;
            if (k.attackingMe)
                score += 1000.f;
            if (r.hunting && k.bountyTarget)
                score += 200.f;
            if (best < 0 || score > bestScore || (score == bestScore && k.guid < c[size_t(best)].guid))
            {
                best = int(i);
                bestScore = score;
            }
        }
        return best;
    }

    // ── How many, and how much thinking ─────────────────────────────────────────────────────

    uint32_t RosterFor(uint32_t perZone, uint32_t zones, uint32_t alreadyUsed)
    {
        uint32_t const left = alreadyUsed >= kMaxRoster ? 0 : kMaxRoster - alreadyUsed;
        uint64_t const want = uint64_t(perZone) * zones;
        return uint32_t(std::min<uint64_t>(want, left));
    }

    uint32_t SpawnCap(uint32_t maxSpawned, uint32_t perPlayer, uint32_t players)
    {
        uint64_t const want = uint64_t(perPlayer) * std::max<uint32_t>(1, players);
        return uint32_t(std::min<uint64_t>(want, maxSpawned));
    }

    std::vector<size_t> ChoosePresent(std::vector<Presence> const& w, uint32_t cap, float spawnRadius, float despawnRadius)
    {
        std::vector<size_t> out;
        // Those fighting stay, whatever the cap.
        for (size_t i = 0; i < w.size(); ++i)
            if (w[i].spawned && w[i].busy && !w[i].dead)
                out.push_back(i);
        auto byNearest = [&](std::vector<size_t>& v) {
            std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) { return w[a].nearestPlayer < w[b].nearestPlayer; });
        };
        std::vector<size_t> staying, coming;
        for (size_t i = 0; i < w.size(); ++i)
        {
            if (w[i].dead || (w[i].spawned && w[i].busy))
                continue;
            if (w[i].spawned && w[i].nearestPlayer <= despawnRadius)
                staying.push_back(i);
            else if (!w[i].spawned && w[i].nearestPlayer <= spawnRadius)
                coming.push_back(i);
        }
        byNearest(staying);
        byNearest(coming);
        for (std::vector<size_t> const* list : {&staying, &coming})
            for (size_t i : *list)
                if (out.size() < cap)
                    out.push_back(i);
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<size_t> TakeTurns(size_t count, size_t maxPerTick, size_t& cursor)
    {
        std::vector<size_t> out;
        if (!count)
        {
            cursor = 0;
            return out;
        }
        cursor %= count;
        size_t const n = std::min(count, maxPerTick);
        for (size_t k = 0; k < n; ++k)
            out.push_back((cursor + k) % count);
        cursor = (cursor + n) % count;
        return out;
    }

    uint32_t FirstThinkMs(uint32_t id)
    {
        return (id * 137u) % kThinkMs;
    }

    // ── What it says ────────────────────────────────────────────────────────────────────────

    uint8_t BondTier(int32_t points)
    {
        if (points >= 10)
            return 2;
        if (points >= 3)
            return 1;
        return 0;
    }

    namespace
    {
        std::string Fill(std::string line, std::string const& key, std::string const& value)
        {
            size_t at;
            while ((at = line.find(key)) != std::string::npos)
                line.replace(at, key.size(), value);
            return line;
        }
    }

    std::string GreetLine(uint32_t seed, uint8_t tier, std::string const& player, uint32_t count)
    {
        static std::array<char const*, 6> const strangers{"Well met, traveller.", "Safe roads to you, {name}.",
            "Keep your powder dry out here.", "Watch yourself, the wilds are restless today.",
            "Hail, {name}. Quiet on the road so far.", "Another gun on the road. Good."};
        static std::array<char const*, 5> const met{"{name}! Good to see you again.", "Back on the road, {name}?",
            "Still in one piece, {name}? Glad of it.", "We keep crossing paths, {name}.",
            "Ho, {name}! Any trouble your way?"};
        static std::array<char const*, 5> const friends{"{name}, my friend! Share the road a while?",
            "There's my favourite sharpshooter. Hello, {name}!", "{name}! I owe you a drink at the next inn.",
            "Always glad to see you, {name}.", "{name}! The roads are safer with you on them."};
        uint32_t const r = Mix(seed + count * 2246822519u);
        char const* line = tier >= 2 ? Pick(friends, r) : tier == 1 ? Pick(met, r) : Pick(strangers, r);
        return Fill(line, "{name}", player);
    }

    std::string HelpLine(uint32_t seed)
    {
        static std::array<char const*, 4> const lines{"Help! I'm pinned down here!", "Need a hand, anyone! I'm hit!",
            "Taking fire! Help me!", "Someone, cover me! I can't hold them!"};
        return Pick(lines, Mix(seed ^ 0xC0FFEEu));
    }

    std::string BountyTakenLine(uint32_t seed, std::string const& title)
    {
        static std::array<char const*, 3> const lines{"{title}... That one's mine.", "Coin for {title}? I'll take it.",
            "{title}. Shouldn't take long."};
        return Fill(Pick(lines, Mix(seed ^ 0xB0B0u)), "{title}", title);
    }

    std::string BountyDoneLine(uint32_t seed, std::string const& title)
    {
        static std::array<char const*, 3> const lines{"{title}: done. Pay up.", "That's {title} seen to.",
            "{title}, finished. Easy coin."};
        return Fill(Pick(lines, Mix(seed ^ 0xD0D0u)), "{title}", title);
    }

    std::string RestLine(uint32_t seed, NodeKind at)
    {
        static std::array<char const*, 3> const inn{"A warm fire and a cold drink. Finally.", "I could sleep a week.",
            "Innkeeper! Something strong."};
        static std::array<char const*, 3> const town{"A breather, then on.", "Good to see walls again.",
            "Let me catch my breath."};
        return at == NodeKind::Inn ? Pick(inn, Mix(seed ^ 0x1111u)) : Pick(town, Mix(seed ^ 0x2222u));
    }

    // ── What it keeps ───────────────────────────────────────────────────────────────────────

    std::string EncodeState(Record const& r)
    {
        std::ostringstream s;
        Errand const& e = r.errand;
        std::vector<uint32_t> const entries{e.entries[0], e.entries[1], e.entries[2], e.entries[3]};
        s << "v1"
          << "|" << List(r.trip.route)
          << "|" << (r.trip.hasEnd ? 1 : 0) << "|" << Vec(r.trip.end)
          << "|" << r.trip.leg << "|" << F1(r.trip.along)
          << "|" << uint32_t(r.goal)
          << "|" << e.questId << "|" << List(entries) << "|" << uint32_t(e.needed) << "|" << uint32_t(e.done)
          << "|" << e.boardNode << "|" << (e.hasGround ? 1 : 0) << "|" << Vec(e.ground) << "|" << Clean(e.title)
          << "|" << r.lastNode << "|" << List(r.recent) << "|" << r.respawnInMs;
        return s.str();
    }

    bool DecodeState(std::string const& text, Record& r)
    {
        std::vector<std::string> const f = Split(text, '|');
        if (f.size() != 18 || f[0] != "v1")
            return false;
        Record out = r;
        uint32_t v = 0;
        std::vector<uint32_t> entries;
        if (!ToList(f[1], out.trip.route, 64))
            return false;
        if (!ToU32(f[2], v) || v > 1)
            return false;
        out.trip.hasEnd = v == 1;
        if (!ToVec(f[3], out.trip.end) || !ToU32(f[4], out.trip.leg) || !ToF(f[5], out.trip.along))
            return false;
        if (!ToU32(f[6], v) || v > uint32_t(Goal::TurnIn))
            return false;
        out.goal = Goal(v);
        Errand e;
        if (!ToU32(f[7], e.questId) || !ToList(f[8], entries, 4) || entries.size() != 4)
            return false;
        for (size_t i = 0; i < 4; ++i)
            e.entries[i] = entries[i];
        if (!ToU32(f[9], v) || v > 255)
            return false;
        e.needed = uint8_t(v);
        if (!ToU32(f[10], v) || v > 255)
            return false;
        e.done = uint8_t(v);
        if (!ToU32(f[11], e.boardNode) || !ToU32(f[12], v) || v > 1)
            return false;
        e.hasGround = v == 1;
        if (!ToVec(f[13], e.ground))
            return false;
        e.title = f[14];
        if (!ToU32(f[15], out.lastNode) || !ToList(f[16], out.recent, kRecentPlaces) || !ToU32(f[17], out.respawnInMs))
            return false;
        // A leg past the trip's end is the trip's end.
        if (out.trip.Points() && out.trip.leg >= out.trip.Points())
            out.trip.leg = uint32_t(out.trip.Points() - 1);
        out.errand = e;
        r = std::move(out);
        return true;
    }
}
