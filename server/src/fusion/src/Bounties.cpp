/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Bounties.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

namespace Fusion::Bounty
{
    char const* KindName(Kind k)
    {
        switch (k)
        {
            case Kind::Kill: return "kill";
            case Kind::Collect: return "collect";
            case Kind::Hunt: return "hunt";
            case Kind::Camp: return "camp";
        }
        return "?";
    }

    // ── Days ────────────────────────────────────────────────────────────────────────────────

    int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
    {
        // Howard Hinnant's days_from_civil: exact for every Gregorian date.
        y -= m <= 2;
        int64_t const era = (y >= 0 ? y : y - 399) / 400;
        unsigned const yoe = unsigned(y - era * 400);
        unsigned const doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        unsigned const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + int64_t(doe) - 719468;
    }

    namespace
    {
        // Seconds since 1970 on the local clock, the reset hour taken off.
        int64_t ShiftedSeconds(LocalTime const& t, uint8_t resetHour)
        {
            return DaysFromCivil(t.year, t.month, t.day) * 86400 + int64_t(t.hour) * 3600 + int64_t(t.minute) * 60
                + int64_t(t.second) - int64_t(resetHour % 24) * 3600;
        }

        int64_t FloorDiv(int64_t a, int64_t b)
        {
            int64_t q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
                --q;
            return q;
        }
    }

    uint32_t DayOf(LocalTime const& t, uint8_t resetHour)
    {
        int64_t const day = FloorDiv(ShiftedSeconds(t, resetHour), 86400);
        return day < 0 ? 0 : uint32_t(day);
    }

    uint32_t SecondsToReset(LocalTime const& t, uint8_t resetHour)
    {
        int64_t const s = ShiftedSeconds(t, resetHour);
        int64_t const into = s - FloorDiv(s, 86400) * 86400;
        return uint32_t(86400 - into);
    }

    // ── Quest ids ───────────────────────────────────────────────────────────────────────────

    uint32_t QuestId(uint32_t day, uint32_t zoneSlot, uint32_t index)
    {
        if (zoneSlot >= kMaxZones || index >= kPerZone)
            return 0;
        return kQuestBase + BlockOf(day) * kBlockSize + zoneSlot * kPerZone + index;
    }

    bool SlotOf(uint32_t questId, QuestSlot& out)
    {
        if (!IsBountyQuest(questId))
            return false;
        uint32_t const off = questId - kQuestBase;
        out.block = off / kBlockSize;
        uint32_t const inBlock = off % kBlockSize;
        out.zoneSlot = inBlock / kPerZone;
        out.index = inBlock % kPerZone;
        return out.zoneSlot < kMaxZones;
    }

    // ── What may be a bounty ────────────────────────────────────────────────────────────────

    bool InBand(uint8_t level, ZoneBand band)
    {
        int const lo = int(band.minLevel) - kBandSlackBelow;
        int const hi = int(band.maxLevel) + kBandSlackAbove;
        return int(level) >= lo && int(level) <= hi;
    }

    namespace
    {
        // What every bounty target shares: a hostile, attackable creature of the open world that
        // is nobody's townsfolk, not a critter or a totem, and not our own content.
        bool FitTarget(CreatureFacts const& c, ZoneBand band)
        {
            return c.entry != 0 && c.entry < 90000 && !c.townsfolk && c.hostile && !c.unattackable
                && c.instanceSpawns == 0 && c.type != kTypeCritter && c.type != kTypeTotem
                && c.type != kTypeNotSpecified && InBand(LevelOf(c), band);
        }
    }

    bool IsKillTarget(CreatureFacts const& c, uint32_t spawnsInZone, ZoneBand band)
    {
        return FitTarget(c, band) && c.rank == kRankNormal && c.openWorldSpawns >= 4 && spawnsInZone >= kMinZoneSpawns;
    }

    bool IsHuntTarget(CreatureFacts const& c, uint32_t spawnsInZone, ZoneBand band)
    {
        if (!FitTarget(c, band) || spawnsInZone == 0 || c.longestRespawnSecs > kHuntRespawnMax)
            return false;
        if (c.rank == kRankRare || c.rank == kRankRareElite)
            return true;
        return c.rank == kRankElite && c.openWorldSpawns <= kNamedEliteSpawns;
    }

    bool IsCollectItem(ItemFacts const& i)
    {
        bool const kind = i.itemClass == kItemClassTradeGoods || (i.itemClass == kItemClassMisc && i.subClass == 0);
        return i.entry != 0 && i.entry < 90000 && kind && i.quality <= 1 && i.bonding == 0 && i.stackable >= 5
            && i.maxCount == 0 && !i.startsQuest;
    }

    // ── Camps ───────────────────────────────────────────────────────────────────────────────

    std::vector<Camp> FindCamps(std::vector<KillCandidate> const& kills, std::vector<SpawnPoint> const& spawns)
    {
        std::map<uint32_t, KillCandidate const*> byEntry;
        for (KillCandidate const& k : kills)
            byEntry[k.entry] = &k;
        std::vector<SpawnPoint> points;
        for (SpawnPoint const& s : spawns)
            if (byEntry.count(s.entry))
                points.push_back(s);

        float const r2 = kCampRadius * kCampRadius;
        std::set<std::vector<uint32_t>> seen;
        std::vector<Camp> camps;
        for (SpawnPoint const& centre : points)
        {
            KillCandidate const& a = *byEntry[centre.entry];
            std::map<uint32_t, uint32_t> near;
            for (SpawnPoint const& s : points)
            {
                float const dx = s.x - centre.x, dy = s.y - centre.y;
                if (dx * dx + dy * dy <= r2)
                    ++near[s.entry];
            }
            if (near[a.entry] < kCampMinEach)
                continue;
            std::vector<std::pair<uint32_t, uint32_t>> others;     // (spawns near, entry)
            for (auto const& [entry, n] : near)
            {
                if (entry == a.entry || n < kCampMinEach)
                    continue;
                KillCandidate const& b = *byEntry[entry];
                if (b.family != a.family || std::abs(int(b.level) - int(a.level)) > kCampLevelSpread)
                    continue;
                others.emplace_back(n, entry);
            }
            if (others.empty())
                continue;
            std::sort(others.begin(), others.end(), [](auto const& x, auto const& y) {
                return x.first != y.first ? x.first > y.first : x.second < y.second;
            });
            std::vector<uint32_t> members = {a.entry};
            for (size_t i = 0; i < others.size() && members.size() < 3; ++i)
                members.push_back(others[i].second);
            std::sort(members.begin(), members.end());
            if (!seen.insert(members).second)
                continue;
            Camp c;
            for (uint32_t e : members)
            {
                c.members.push_back(*byEntry[e]);
                c.level = std::max(c.level, byEntry[e]->level);
            }
            c.area = centre.area;
            camps.push_back(std::move(c));
        }
        std::sort(camps.begin(), camps.end(), [](Camp const& x, Camp const& y) {
            if (x.level != y.level)
                return x.level < y.level;
            std::vector<uint32_t> xe, ye;
            for (auto const& m : x.members)
                xe.push_back(m.entry);
            for (auto const& m : y.members)
                ye.push_back(m.entry);
            return xe < ye;
        });
        return camps;
    }

    // ── Rewards ─────────────────────────────────────────────────────────────────────────────

    namespace
    {
        uint32_t RoundTo(double v, uint32_t step)
        {
            if (v <= 0.0)
                return 0;
            return std::max<uint32_t>(step, uint32_t(std::lround(v / step)) * step);
        }
    }

    Reward RewardFor(Kind kind, uint8_t level, bool elite)
    {
        double const l = std::clamp<double>(level, 1, 60);
        double mult = 1.0;
        Reward r;
        r.reputation = 75;
        switch (kind)
        {
            case Kind::Kill:
            case Kind::Collect:
                break;
            case Kind::Camp:
                mult = 1.25;
                r.reputation = 100;
                break;
            case Kind::Hunt:
                mult = elite ? 2.0 : 1.5;
                r.reputation = 150;
                break;
        }
        r.xp = RoundTo((60.0 * l + 0.6 * l * l) * mult, 5);
        r.money = RoundTo((5.0 * l * l + 50.0 * l) * mult, 5);
        // Vanilla gives about six copper for each point of a quest's experience at the cap.
        r.moneyMaxLevel = r.xp * 6;
        return r;
    }

    uint8_t MinLevelFor(Kind kind, uint8_t level)
    {
        int const below = kind == Kind::Hunt ? 2 : 4;
        return uint8_t(std::max(1, int(level) - below));
    }

    // ── Words ───────────────────────────────────────────────────────────────────────────────

    namespace
    {
        std::string Lower(std::string s)
        {
            for (char& c : s)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool EndsWith(std::string const& s, char const* tail)
        {
            std::string const t(tail);
            return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
        }

        std::string PluralWord(std::string const& w)
        {
            if (w.empty())
                return w;
            std::string const l = Lower(w);
            static char const* const kSame[] = {"vermin", "deer", "sheep", "fish", "moose", "folk"};
            for (char const* s : kSame)
                if (l == s || EndsWith(l, s))
                    return w;
            static char const* const kManS[] = {"shaman", "human", "talisman", "caiman"};
            bool manS = false;
            for (char const* s : kManS)
                manS = manS || EndsWith(l, s);
            if (EndsWith(l, "man") && !manS)
                return w.substr(0, w.size() - 3) + (std::isupper(static_cast<unsigned char>(w[w.size() - 3])) ? "Men" : "men");
            if (EndsWith(l, "f") && !EndsWith(l, "ff") && !EndsWith(l, "ief"))
                return w.substr(0, w.size() - 1) + "ves";
            if (EndsWith(l, "ief"))
                return EndsWith(l, "thief") ? w.substr(0, w.size() - 1) + "ves" : w + "s";
            if (EndsWith(l, "y") && l.size() >= 2 && !std::strchr("aeiou", l[l.size() - 2]))
                return w.substr(0, w.size() - 1) + "ies";
            if (EndsWith(l, "s") || EndsWith(l, "x") || EndsWith(l, "z") || EndsWith(l, "ch") || EndsWith(l, "sh"))
                return w + "es";
            return w + "s";
        }
    }

    std::string Plural(std::string const& name)
    {
        // "Servant of Arugal" -> "Servants of Arugal": the word before " of ".
        size_t const of = name.find(" of ");
        std::string head = of == std::string::npos ? name : name.substr(0, of);
        std::string const tail = of == std::string::npos ? std::string() : name.substr(of);
        size_t const space = head.find_last_of(' ');
        std::string const last = space == std::string::npos ? head : head.substr(space + 1);
        head = (space == std::string::npos ? std::string() : head.substr(0, space + 1)) + PluralWord(last);
        return head + tail;
    }

    // ── The day's bounties ──────────────────────────────────────────────────────────────────

    namespace
    {
        // SplitMix64: small, fast and the same on every platform.
        struct Rng
        {
            uint64_t state;
            uint64_t Next()
            {
                uint64_t z = (state += 0x9E3779B97F4A7C15ull);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                return z ^ (z >> 31);
            }
            uint32_t Below(uint32_t n) { return n ? uint32_t(Next() % n) : 0; }
        };

        uint32_t Fnv(std::vector<uint32_t> const& words)
        {
            uint32_t h = 2166136261u;
            for (uint32_t w : words)
                for (int i = 0; i < 4; ++i)
                {
                    h ^= (w >> (8 * i)) & 0xFF;
                    h *= 16777619u;
                }
            return h ? h : 1;
        }

        uint8_t Clamp63(uint32_t n) { return uint8_t(std::clamp<uint32_t>(n, 1, kMaxObjectiveCount)); }

        // A kill bounty's count by the target's level, a little more each way by the day.
        uint8_t KillCount(uint8_t level, Rng& rng)
        {
            uint32_t const base = level <= 5 ? 8 : level <= 15 ? 10 : 12;
            return Clamp63(base + rng.Below(5));
        }

        std::string const& Pick(std::vector<std::string> const& v, Rng& rng) { return v[rng.Below(uint32_t(v.size()))]; }

        void Finish(Bounty& b)
        {
            b.minLevel = MinLevelFor(b.kind, b.level);
            b.reward = RewardFor(b.kind, b.level, b.elite);
            std::vector<uint32_t> words = {uint32_t(b.kind), b.zone, b.level, b.item, b.itemCount};
            for (Objective const& o : b.kills)
            {
                words.push_back(o.creature);
                words.push_back(o.count);
            }
            b.key = Fnv(words);
        }

        std::string AnyBoard(std::string const& zone) { return "any bounty board in " + zone; }

        Bounty MakeKill(ZonePool const& pool, KillCandidate const& k, Rng& rng)
        {
            Bounty b;
            b.kind = Kind::Kill;
            b.zone = pool.zone;
            b.level = k.level;
            b.kills.push_back({k.entry, KillCount(k.level, rng), k.name});
            uint8_t const n = b.kills[0].count;
            std::string const many = Plural(k.name);
            b.title = "Bounty: " + k.name;
            b.objective = "Kill " + std::to_string(n) + " " + many;
            b.objectives = "Kill " + std::to_string(n) + " " + many + " in " + pool.zoneName + ", then return to "
                + AnyBoard(pool.zoneName) + ".";
            static std::vector<std::string> const kOpen = {
                "The watch pays for every %s put down. There are more of them every week, and the patrols cannot keep up.",
                "Travellers keep running into %s on the roads. The board pays to thin them out before somebody gets hurt.",
                "Too many %s, too close to town. Coin for anyone with a steady aim and a full magazine.",
            };
            std::string open = Pick(kOpen, rng);
            std::string const what = open.find("every %s") != std::string::npos ? k.name : many;
            open.replace(open.find("%s"), 2, what);
            b.details = open + "$B$BKill " + std::to_string(n) + " " + many + " and claim the purse at " + AnyBoard(pool.zoneName) + ".";
            Finish(b);
            return b;
        }

        Bounty MakeCollect(ZonePool const& pool, CollectCandidate const& c, Rng& rng)
        {
            Bounty b;
            b.kind = Kind::Collect;
            b.zone = pool.zone;
            b.level = c.level;
            b.item = c.item;
            b.itemName = c.itemName;
            // About fourteen kills of the dropper.
            b.itemCount = uint8_t(std::clamp<long>(std::lround(14.0 * c.chance), 3, 10));
            std::string const n = std::to_string(b.itemCount);
            std::string const from = Plural(c.dropperName);
            b.title = "Bounty: " + c.itemName;
            b.objective = "Collect " + n + " " + c.itemName;
            b.objectives = "Collect " + n + " " + c.itemName + " from the " + from + " of " + pool.zoneName
                + " and bring them to " + AnyBoard(pool.zoneName) + ".";
            static std::vector<std::string> const kOpen = {
                "The quartermasters are short of %s and pay well for it. The %s hereabouts carry plenty.",
                "A trader in town has buyers waiting for %s. Take it from the %s and bring it to the board.",
            };
            std::string open = Pick(kOpen, rng);
            open.replace(open.find("%s"), 2, c.itemName);
            open.replace(open.find("%s"), 2, from);
            b.details = open + "$B$BBring " + n + " " + c.itemName + " to " + AnyBoard(pool.zoneName) + ".";
            Finish(b);
            return b;
        }

        Bounty MakeHunt(ZonePool const& pool, HuntCandidate const& h, Rng& rng)
        {
            Bounty b;
            b.kind = Kind::Hunt;
            b.zone = pool.zone;
            b.level = h.level;
            b.elite = h.elite;
            b.kills.push_back({h.entry, 1, h.name});
            b.title = "Wanted: " + h.name;
            b.objective = "Kill " + h.name;
            b.objectives = "Hunt down " + h.name + " in " + pool.zoneName + " and return to " + AnyBoard(pool.zoneName) + ".";
            static std::vector<std::string> const kOpen = {
                "WANTED, dead: %s. Last seen in %s. The reward is paid on proof at any board in the zone.",
                "A price is on the head of %s, who has troubled %s long enough. Find it, finish it, collect.",
            };
            std::string open = Pick(kOpen, rng);
            open.replace(open.find("%s"), 2, h.name);
            open.replace(open.find("%s"), 2, pool.zoneName);
            b.details = open + (h.elite ? "$B$BIt is dangerous: bring friends." : "$B$BIt is seldom seen and quick to vanish: keep looking.");
            Finish(b);
            return b;
        }

        Bounty MakeCamp(ZonePool const& pool, Camp const& c, Rng& rng)
        {
            Bounty b;
            b.kind = Kind::Camp;
            b.zone = pool.zone;
            b.level = c.level;
            size_t const m = std::min<size_t>(c.members.size(), 3);
            std::vector<std::string> parts;
            for (size_t i = 0; i < m; ++i)
            {
                uint8_t const n = Clamp63(m == 2 ? 6 + rng.Below(3) : 5 + rng.Below(2));
                b.kills.push_back({c.members[i].entry, n, c.members[i].name});
                parts.push_back(std::to_string(n) + " " + Plural(c.members[i].name));
            }
            std::string list;
            for (size_t i = 0; i < parts.size(); ++i)
                list += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
            std::string const place = c.place.empty() ? pool.zoneName : c.place;
            b.title = "Clear Out: " + place;
            b.objective = "Kill " + list;
            b.objectives = "Clear the camp at " + place + ": kill " + list + ", then return to " + AnyBoard(pool.zoneName) + ".";
            static std::vector<std::string> const kOpen = {
                "A camp has dug in at %s and grows bolder by the day. Break it before it becomes a fort.",
                "Scouts report a camp at %s. The board pays for every one of them that stops breathing.",
            };
            std::string open = Pick(kOpen, rng);
            open.replace(open.find("%s"), 2, place);
            b.details = open + "$B$BKill " + list + ".";
            Finish(b);
            return b;
        }
    }

    std::vector<Bounty> Post(ZonePool const& pool, uint32_t zoneSlot, uint32_t day, uint32_t salt)
    {
        std::vector<Bounty> out;
        if (pool.Empty() || zoneSlot >= kMaxZones)
            return out;
        Rng rng{(uint64_t(day) << 32) ^ (uint64_t(pool.zone) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(salt) * 0xD1B54A32D192ED03ull)};

        std::vector<KillCandidate> kills = pool.kills;
        std::sort(kills.begin(), kills.end(), [](KillCandidate const& a, KillCandidate const& b) {
            return a.level != b.level ? a.level < b.level : a.entry < b.entry;
        });
        std::set<uint32_t> usedKills, usedItems;
        std::set<size_t> usedCamps, usedHunts;

        // A kill from each third of the zone's level range.
        size_t const n = kills.size();
        if (n >= 3)
        {
            for (size_t tier = 0; tier < 3; ++tier)
            {
                size_t const lo = n * tier / 3, hi = n * (tier + 1) / 3;
                KillCandidate const& k = kills[lo + rng.Below(uint32_t(hi - lo))];
                usedKills.insert(k.entry);
                out.push_back(MakeKill(pool, k, rng));
            }
        }
        else
        {
            for (KillCandidate const& k : kills)
            {
                usedKills.insert(k.entry);
                out.push_back(MakeKill(pool, k, rng));
            }
        }
        // A collect.
        if (!pool.collects.empty())
        {
            CollectCandidate const& c = pool.collects[rng.Below(uint32_t(pool.collects.size()))];
            usedItems.insert(c.item);
            out.push_back(MakeCollect(pool, c, rng));
        }
        // A hunt or a camp: a hunt when there are both, every other day or so.
        bool const haveHunt = !pool.hunts.empty(), haveCamp = !pool.camps.empty();
        bool const hunt = haveHunt && (!haveCamp || rng.Below(2) == 0);
        if (hunt)
        {
            size_t const i = rng.Below(uint32_t(pool.hunts.size()));
            usedHunts.insert(i);
            out.push_back(MakeHunt(pool, pool.hunts[i], rng));
        }
        else if (haveCamp)
        {
            size_t const i = rng.Below(uint32_t(pool.camps.size()));
            usedCamps.insert(i);
            out.push_back(MakeCamp(pool, pool.camps[i], rng));
        }
        // Short of five: more of what the pool has.
        for (int guard = 0; out.size() < kPerZone && guard < 4; ++guard)
        {
            bool added = false;
            for (KillCandidate const& k : kills)
                if (out.size() < kPerZone && !usedKills.count(k.entry))
                {
                    usedKills.insert(k.entry);
                    out.push_back(MakeKill(pool, k, rng));
                    added = true;
                    break;
                }
            for (CollectCandidate const& c : pool.collects)
                if (out.size() < kPerZone && !usedItems.count(c.item))
                {
                    usedItems.insert(c.item);
                    out.push_back(MakeCollect(pool, c, rng));
                    added = true;
                    break;
                }
            for (size_t i = 0; i < pool.camps.size(); ++i)
                if (out.size() < kPerZone && !usedCamps.count(i))
                {
                    usedCamps.insert(i);
                    out.push_back(MakeCamp(pool, pool.camps[i], rng));
                    added = true;
                    break;
                }
            for (size_t i = 0; i < pool.hunts.size(); ++i)
                if (out.size() < kPerZone && !usedHunts.count(i))
                {
                    usedHunts.insert(i);
                    out.push_back(MakeHunt(pool, pool.hunts[i], rng));
                    added = true;
                    break;
                }
            if (!added)
                break;
        }
        if (out.size() > kPerZone)
            out.resize(kPerZone);
        for (size_t i = 0; i < out.size(); ++i)
            out[i].questId = QuestId(day, zoneSlot, uint32_t(i));
        return out;
    }

    // ── Taking and turning in ───────────────────────────────────────────────────────────────

    char const* RefusalText(Refusal r)
    {
        switch (r)
        {
            case Refusal::None: return "";
            case Refusal::NotPosted: return "That bounty is not on this board.";
            case Refusal::OtherZone: return "Turn that bounty in at a board in its own zone.";
            case Refusal::Level: return "You are not experienced enough for that bounty.";
            case Refusal::Held: return "You already hold that bounty.";
            case Refusal::DoneToday: return "You have already collected that bounty today.";
            case Refusal::Cap: return "You have taken all the bounties you may finish today. The boards post new ones at the daily reset.";
            case Refusal::LogFull: return "Your quest log is full.";
            case Refusal::Expired: return "That bounty has expired.";
            case Refusal::NotComplete: return "That bounty is not finished yet.";
            case Refusal::TooFar: return "You are too far from the board.";
            case Refusal::WrongTeam: return "This board posts for the other faction.";
            case Refusal::NotHeld: return "You do not hold that bounty.";
        }
        return "";
    }

    Refusal CanAccept(AcceptCheck const& c)
    {
        if (!c.nearBoard)
            return Refusal::TooFar;
        if (!c.teamAllowed)
            return Refusal::WrongTeam;
        if (!c.posted)
            return Refusal::NotPosted;
        if (c.inLog)
            return Refusal::Held;
        if (c.doneToday)
            return Refusal::DoneToday;
        if (c.playerLevel < c.minLevel)
            return Refusal::Level;
        if (c.doneCount + c.heldCount >= kDailyCap)
            return Refusal::Cap;
        if (c.logFull)
            return Refusal::LogFull;
        return Refusal::None;
    }

    Refusal CanTurnIn(TurnInCheck const& c)
    {
        if (!c.nearBoard)
            return Refusal::TooFar;
        if (!c.teamAllowed)
            return Refusal::WrongTeam;
        if (!c.held)
            return Refusal::NotHeld;
        if (c.expired)
            return Refusal::Expired;
        if (!c.sameZone)
            return Refusal::OtherZone;
        if (!c.complete)
            return Refusal::NotComplete;
        if (c.doneCount >= kDailyCap)
            return Refusal::Cap;
        return Refusal::None;
    }

    // ── The wire ────────────────────────────────────────────────────────────────────────────

    namespace
    {
        struct Writer
        {
            std::vector<uint8_t> out;
            void U8(uint8_t v) { out.push_back(v); }
            void U16(uint16_t v)
            {
                U8(uint8_t(v));
                U8(uint8_t(v >> 8));
            }
            void U32(uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    U8(uint8_t(v >> (8 * i)));
            }
            void Str8(std::string const& s)
            {
                size_t const n = std::min<size_t>(s.size(), 255);
                U8(uint8_t(n));
                out.insert(out.end(), s.begin(), s.begin() + long(n));
            }
            void Str16(std::string const& s, size_t cap)
            {
                size_t const n = std::min<size_t>(s.size(), cap);
                U16(uint16_t(n));
                out.insert(out.end(), s.begin(), s.begin() + long(n));
            }
        };

        struct Reader
        {
            std::vector<uint8_t> const& in;
            size_t pos = 0;
            bool ok = true;
            bool Need(size_t n)
            {
                if (!ok || in.size() - pos < n)
                    ok = false;
                return ok;
            }
            uint8_t U8() { return Need(1) ? in[pos++] : 0; }
            uint16_t U16()
            {
                if (!Need(2))
                    return 0;
                uint16_t const v = uint16_t(in[pos] | in[pos + 1] << 8);
                pos += 2;
                return v;
            }
            uint32_t U32()
            {
                if (!Need(4))
                    return 0;
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(in[pos + size_t(i)]) << (8 * i);
                pos += 4;
                return v;
            }
            std::string Str(size_t n)
            {
                if (!Need(n))
                    return {};
                std::string s(in.begin() + long(pos), in.begin() + long(pos + n));
                pos += n;
                return s;
            }
            std::string Str8() { return Str(U8()); }
            std::string Str16() { return Str(U16()); }
        };

        // The rows a board message carries, and the most a row's details may take, so a message
        // stays well under the channel's 8 KB (Packets.h kMaxChannelBody).
        constexpr size_t kMaxRows = 16;
        constexpr size_t kMaxDetails = 320;
        constexpr size_t kMaxObjective = 200;
    }

    std::vector<uint8_t> EncodeBoard(Board const& b)
    {
        Writer w;
        w.U8(kVersion);
        w.U8(kOpBoard);
        w.U8(b.open ? 1 : 0);
        w.U32(b.zone);
        w.Str8(b.town);
        w.Str8(b.zoneName);
        w.U8(b.done);
        w.U8(b.cap);
        w.U8(b.level);
        w.U32(b.secondsToReset);
        size_t const rows = std::min(b.rows.size(), kMaxRows);
        w.U8(uint8_t(rows));
        for (size_t i = 0; i < rows; ++i)
        {
            Row const& r = b.rows[i];
            w.U32(r.questId);
            w.U8(uint8_t(r.kind));
            w.U8(uint8_t(r.state));
            w.U8(r.level);
            w.U8(r.minLevel);
            w.U8(r.elite ? 1 : 0);
            w.U32(r.xp);
            w.U32(r.money);
            w.U16(r.reputation);
            w.Str8(r.repName);
            w.Str8(r.title);
            w.Str16(r.objective, kMaxObjective);
            w.Str16(r.details, kMaxDetails);
            size_t const p = std::min<size_t>(r.progress.size(), 4);
            w.U8(uint8_t(p));
            for (size_t k = 0; k < p; ++k)
            {
                w.U8(r.progress[k].have);
                w.U8(r.progress[k].need);
                w.Str8(r.progress[k].label);
            }
        }
        return w.out;
    }

    bool DecodeBoard(std::vector<uint8_t> const& body, Board& out)
    {
        Reader r{body};
        if (r.U8() != kVersion || r.U8() != kOpBoard)
            return false;
        Board b;
        b.open = r.U8() != 0;
        b.zone = r.U32();
        b.town = r.Str8();
        b.zoneName = r.Str8();
        b.done = r.U8();
        b.cap = r.U8();
        b.level = r.U8();
        b.secondsToReset = r.U32();
        uint8_t const rows = r.U8();
        for (uint8_t i = 0; i < rows && r.ok; ++i)
        {
            Row row;
            row.questId = r.U32();
            row.kind = Kind(r.U8());
            row.state = RowState(r.U8());
            row.level = r.U8();
            row.minLevel = r.U8();
            row.elite = r.U8() != 0;
            row.xp = r.U32();
            row.money = r.U32();
            row.reputation = r.U16();
            row.repName = r.Str8();
            row.title = r.Str8();
            row.objective = r.Str16();
            row.details = r.Str16();
            uint8_t const p = r.U8();
            for (uint8_t k = 0; k < p && r.ok; ++k)
            {
                Progress pr;
                pr.have = r.U8();
                pr.need = r.U8();
                pr.label = r.Str8();
                row.progress.push_back(std::move(pr));
            }
            b.rows.push_back(std::move(row));
        }
        if (!r.ok || r.pos != body.size())
            return false;
        out = std::move(b);
        return true;
    }

    std::vector<uint8_t> EncodeResult(bool ok, std::string const& text)
    {
        Writer w;
        w.U8(kVersion);
        w.U8(kOpResult);
        w.U8(ok ? 1 : 0);
        w.Str8(text);
        return w.out;
    }

    bool DecodeResult(std::vector<uint8_t> const& body, bool& ok, std::string& text)
    {
        Reader r{body};
        if (r.U8() != kVersion || r.U8() != kOpResult)
            return false;
        bool const o = r.U8() != 0;
        std::string const t = r.Str8();
        if (!r.ok || r.pos != body.size())
            return false;
        ok = o;
        text = t;
        return true;
    }

    std::vector<uint8_t> EncodeAsk() { return {kVersion, kOpAsk}; }

    std::vector<uint8_t> EncodeAccept(uint32_t questId)
    {
        Writer w;
        w.U8(kVersion);
        w.U8(kOpAccept);
        w.U32(questId);
        return w.out;
    }

    std::vector<uint8_t> EncodeTurnIn(uint32_t questId)
    {
        Writer w;
        w.U8(kVersion);
        w.U8(kOpTurnIn);
        w.U32(questId);
        return w.out;
    }

    bool DecodeRequest(std::vector<uint8_t> const& body, uint8_t& op, uint32_t& questId)
    {
        Reader r{body};
        if (r.U8() != kVersion)
            return false;
        uint8_t const o = r.U8();
        uint32_t q = 0;
        if (o == kOpAccept || o == kOpTurnIn)
            q = r.U32();
        else if (o != kOpAsk)
            return false;
        if (!r.ok || r.pos != body.size())
            return false;
        op = o;
        questId = q;
        return true;
    }
}
