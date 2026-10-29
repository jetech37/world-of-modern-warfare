/*
 * World of Modern Warfare: the wanderers' lives (Phase 2). GPL-2.0-or-later. See FusionWanderers.h.
 *
 * Each wanderer's trade, day and nature (fusion/WandererLives.h) put in the world: the workplaces
 * the world itself has (ore veins and herbs by their locks, game by its skinning loot, fishing folk
 * by the water, vendors' markets), what they yield (the world's own loot tables), what markets sell
 * (the vendors' own lists) and at what price (item_template's); its plans along the roads; its
 * work, sales, meals and sleep, on paper and in sight; talk with players (a gossip menu) and with
 * other wanderers; its stall (its pack, as a vendor's list); a hire; the world's events noticed;
 * and its moving away when its span is over.
 */

#include "FusionWanderers.h"
#include "FusionBounties.h"
#include "FusionMgr.h"
#include "fusion/CompanionBattles.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "GossipDef.h"
#include "GridMap.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "Weather.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <set>
#include <sstream>

using namespace Fusion::Wanderers;
using Fusion::Vec3;

namespace
{
    // The gossip menu's sender (its own, and the actions under it).
    constexpr uint32 kTalkSender = 0x5741;
    enum TalkAction : uint32
    {
        kAbout = 1,
        kHeading = 2,
        kWares = 3,
        kRumours = 4,
        kHire = 5,
        kNews = 6,
        kFarewell = 7,
        kDismiss = 8,
        kBattle = 9,
    };
    // A player talking to it keeps it standing this long.
    constexpr uint32 kTalkHoldMs = 30000;
    // Two wanderers chat no more than once in this long.
    constexpr uint32 kChatAgainMs = 10 * 60 * 1000;
    // Clusters of veins, herbs, game and vendors: this big a cell, this many in one.
    constexpr float kSpotCell = 140.f;
    constexpr uint32 kMinVeins = 3, kMinBeasts = 4, kMinVendors = 3;
    // A world event is noticed this far away.
    constexpr float kEventRadius = 200.f;
    // Auto-found workplaces' and markets' node ids (authored 1-9999, boards 10000+, inns 20000+).
    constexpr uint32 kSpotIds = 40000;

    Vec3 V(float x, float y, float z) { return {x, y, z}; }
    Vec3 PosOf(WorldObject const* o) { return V(o->GetPositionX(), o->GetPositionY(), o->GetPositionZ()); }
    float Dist2d(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }
    bool Due(uint32 now, uint32 at) { return int32(now - at) >= 0; }

    std::string AreaNameOf(uint32 id)
    {
        AreaEntry const* a = id ? AreaEntry::GetById(id) : nullptr;
        return a && a->Name ? std::string(a->Name) : std::string();
    }

    uint8 HourNow()
    {
        time_t const t = time(nullptr);
        tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        return uint8(lt.tm_hour);
    }

    uint32 TodayNow() { return Fusion::Wanderers::DayOf(int64(time(nullptr))); }

    std::string MoneyText(uint32 copper)
    {
        std::ostringstream s;
        if (copper >= 10000)
            s << copper / 10000 << "g ";
        if (copper >= 100)
            s << (copper / 100) % 100 << "s ";
        s << copper % 100 << "c";
        return s.str();
    }

    std::string ItemName(uint32 item)
    {
        ItemPrototype const* p = sObjectMgr.GetItemPrototype(item);
        return p ? p->Name1 : std::to_string(item);
    }

    // A loot table's rows (one level of references followed), this patch's only.
    std::vector<LootRow> LootRows(char const* table, uint32 entry, int depth = 0)
    {
        std::vector<LootRow> rows;
        if (!entry || depth > 1)
            return rows;
        uint32 const patch = sWorld.GetWowPatch();
        std::unique_ptr<QueryResult> r = WorldDatabase.PQuery(
            "SELECT `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount` FROM `%s` WHERE `entry` = %u "
            "AND `patch_min` <= %u AND `patch_max` >= %u AND `ChanceOrQuestChance` >= 0",
            table, entry, patch, patch);
        if (!r)
            return rows;
        do
        {
            Field* f = r->Fetch();
            int32 const minOrRef = f[3].GetInt32();
            if (minOrRef < 0)
            {
                // A reference: its rows, at the referring row's chance (as one more group).
                std::vector<LootRow> const sub = LootRows("reference_loot_template", uint32(-minOrRef), depth + 1);
                float const chance = f[1].GetFloat();
                if (chance >= 100.f || chance <= 0.f)
                    rows.insert(rows.end(), sub.begin(), sub.end());
                continue;
            }
            LootRow row;
            row.item = f[0].GetUInt32();
            row.chance = f[1].GetFloat();
            row.group = uint8(std::min<uint32>(f[2].GetUInt32() + uint32(depth) * 50, 255));
            row.minCount = uint8(std::clamp(minOrRef, 1, 255));
            row.maxCount = uint8(std::clamp<uint32>(f[4].GetUInt32(), row.minCount, 255));
            if (sObjectMgr.GetItemPrototype(row.item))
                rows.push_back(row);
        } while (r->NextRow());
        return rows;
    }

    // What the kind of a work spot is called after the place it is in.
    std::string SpotName(NodeKind k, std::string const& area)
    {
        std::string const a = area.empty() ? std::string("the wilds") : area;
        switch (k)
        {
            case NodeKind::Fishing: return "the waters by " + a;
            case NodeKind::Mine:
                return a.find("Mine") != std::string::npos || a.find("Quarry") != std::string::npos || a.find("Cave") != std::string::npos
                    ? a : "the veins near " + a;
            case NodeKind::Herbs: return "the meadows of " + a;
            case NodeKind::Wilds: return "the hunting grounds of " + a;
            case NodeKind::Market: return a + " market";
            default: return a;
        }
    }
}

// ── Startup: the places of their lives ─────────────────────────────────────────────────────────

void WandererMgr::DiscoverPlaces(Region& g, std::vector<Node>& list, uint32 regionIndex, std::map<uint32, Spot>& spots)
{
    FactionTemplateEntry const* mine = sObjectMgr.GetFactionTemplateEntry(RaceFaction(g.side == Side::Alliance ? kHuman : kOrc));
    float const margin = 300.f;
    auto inBox = [&](uint32 map, float x, float y) {
        return map == g.map && x >= g.minX - margin && x <= g.maxX + margin && y >= g.minY - margin && y <= g.maxY + margin;
    };
    auto zoneIn = [&](Vec3 p) -> uint32 {
        uint32 const z = sTerrainMgr.GetZoneId(g.map, p.x, p.y, p.z);
        for (ZoneBand const& b : g.zones)
            if (b.zone == z)
                return z;
        return 0;
    };
    uint8 minLevel = 60, maxLevel = 1;
    for (ZoneBand const& b : g.zones)
    {
        minLevel = std::min(minLevel, b.minLevel);
        maxLevel = std::max(maxLevel, b.maxLevel);
    }

    struct Cell
    {
        std::vector<Vec3> points;
        std::map<uint32, uint32> loot;      // loot id -> how many
        std::vector<uint32> entries;        // vendors
    };
    auto key = [](float x, float y) { return (int64(std::floor(x / kSpotCell)) << 32) ^ int64(uint32(int32(std::floor(y / kSpotCell)))); };
    std::map<int64, Cell> veins, herbs, game, vendors;
    struct Folk
    {
        Vec3 pos;
        uint32 entry;
    };
    std::vector<Folk> fishers, allVendors;

    // Ore and herbs: chests with a mining or a herbalism lock.
    auto goWorker = [&](GameObjectDataPair const& pair) {
        GameObjectData const& d = pair.second;
        if (!inBox(d.position.mapId, d.position.x, d.position.y))
            return false;
        GameObjectInfo const* gi = sObjectMgr.GetGameObjectTemplate(d.id);
        if (!gi || gi->type != GAMEOBJECT_TYPE_CHEST)
            return false;
        LockEntry const* lock = sLockStore.LookupEntry(gi->chest.lockId);
        if (!lock)
            return false;
        for (uint32 i = 0; i < MAX_LOCK_CASE; ++i)
            if (lock->Type[i] == LOCK_KEY_SKILL && (lock->Index[i] == LOCKTYPE_MINING || lock->Index[i] == LOCKTYPE_HERBALISM))
            {
                Cell& c = (lock->Index[i] == LOCKTYPE_MINING ? veins : herbs)[key(d.position.x, d.position.y)];
                c.points.push_back(V(d.position.x, d.position.y, d.position.z));
                if (gi->chest.lootId)
                    ++c.loot[gi->chest.lootId];
                break;
            }
        return false;
    };
    sObjectMgr.DoGOData(goWorker);

    // Game (skinnable beasts of the region's levels), fishing folk, vendors.
    auto creatureWorker = [&](CreatureDataPair const& pair) {
        CreatureData const& d = pair.second;
        if (!inBox(d.position.mapId, d.position.x, d.position.y))
            return false;
        uint32 const id = d.creature_id[0];
        CreatureInfo const* ci = id ? sObjectMgr.GetCreatureTemplate(id) : nullptr;
        if (!ci)
            return false;
        Vec3 const p = V(d.position.x, d.position.y, d.position.z);
        if (ci->type == CREATURE_TYPE_BEAST && ci->skinning_loot_id && ci->rank == 0 && ci->level_min >= minLevel && ci->level_max <= maxLevel + 3)
        {
            Cell& c = game[key(p.x, p.y)];
            c.points.push_back(p);
            ++c.loot[ci->skinning_loot_id];
            return false;
        }
        FactionTemplateEntry const* theirs = sObjectMgr.GetFactionTemplateEntry(ci->faction);
        if (!theirs || !mine || theirs->IsHostileTo(*mine))
            return false;
        if (ci->subname.find("Fish") != std::string::npos || ci->name.find("Fisherman") != std::string::npos)
            fishers.push_back({p, id});
        if (ci->npc_flags & UNIT_NPC_FLAG_VENDOR)
        {
            allVendors.push_back({p, id});
            Cell& c = vendors[key(p.x, p.y)];
            c.points.push_back(p);
            c.entries.push_back(id);
        }
        return false;
    };
    sObjectMgr.DoCreatureData(creatureWorker);

    uint32 next = kSpotIds + regionIndex * 1000;
    // The densest cells of each zone become places.
    auto place = [&](std::map<int64, Cell> const& cells, NodeKind kind, uint32 minCount, uint32 perZone) {
        struct Pick
        {
            Vec3 at;
            Cell const* cell;
            uint32 zone;
        };
        std::vector<Pick> picks;
        for (auto const& [k, c] : cells)
        {
            if (c.points.size() < minCount)
                continue;
            Vec3 mean;
            for (Vec3 const& p : c.points)
                mean = mean + p;
            mean = mean * (1.f / float(c.points.size()));
            Vec3 best = c.points.front();
            for (Vec3 const& p : c.points)
                if (Dist2d(p, mean) < Dist2d(best, mean))
                    best = p;
            uint32 const zone = zoneIn(best);
            if (!zone)
                continue;
            bool close = false;
            for (Node const& n : list)
                close = close || (n.kind == kind && Dist2d(n.pos, best) < kSpotCell);
            if (!close)
                picks.push_back({best, &c, zone});
        }
        std::stable_sort(picks.begin(), picks.end(), [](Pick const& a, Pick const& b) { return a.cell->points.size() > b.cell->points.size(); });
        std::map<uint32, uint32> perZoneCount;
        for (Pick const& p : picks)
        {
            if (perZoneCount[p.zone]++ >= perZone || next >= kSpotIds + regionIndex * 1000 + 999)
                continue;
            Node n;
            n.id = next++;
            n.zone = p.zone;
            n.kind = kind;
            n.pos = p.at;
            n.name = SpotName(kind, AreaNameOf(sTerrainMgr.GetAreaId(g.map, p.at.x, p.at.y, p.at.z)));
            list.push_back(n);
            Spot s;
            s.kind = kind;
            s.pos = p.at;
            s.name = n.name;
            std::vector<std::pair<uint32, uint32>> loot(p.cell->loot.begin(), p.cell->loot.end());
            std::stable_sort(loot.begin(), loot.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
            for (auto const& l : loot)
                s.lootIds.push_back(l.first);
            s.vendors = uint32(p.cell->entries.size());
            s.vendorEntries = p.cell->entries;
            spots[n.id] = s;
        }
    };
    place(veins, NodeKind::Mine, kMinVeins, m_spotsPerZone);
    place(herbs, NodeKind::Herbs, kMinVeins, m_spotsPerZone);
    place(game, NodeKind::Wilds, kMinBeasts, m_spotsPerZone);
    place(vendors, NodeKind::Market, kMinVendors, 2);

    // Fishing folk stand by the water: one place by each (not two near).
    std::map<uint32, uint32> fishPerZone;
    for (Folk const& f : fishers)
    {
        uint32 const zone = zoneIn(f.pos);
        bool close = false;
        for (Node const& n : list)
            close = close || (n.kind == NodeKind::Fishing && Dist2d(n.pos, f.pos) < 150.f);
        if (!zone || close || fishPerZone[zone]++ >= m_spotsPerZone + 1)
            continue;
        Node n;
        n.id = next++;
        n.zone = zone;
        n.kind = NodeKind::Fishing;
        n.pos = f.pos;
        n.name = SpotName(NodeKind::Fishing, AreaNameOf(sTerrainMgr.GetAreaId(g.map, f.pos.x, f.pos.y, f.pos.z)));
        list.push_back(n);
        Spot s;
        s.kind = NodeKind::Fishing;
        s.pos = f.pos;
        s.name = n.name;
        spots[n.id] = s;
    }

    // Authored markets (and every market): the vendors within reach of it.
    for (Node const& n : list)
    {
        if (n.kind != NodeKind::Market)
            continue;
        Spot& s = spots[n.id];
        s.kind = NodeKind::Market;
        s.pos = n.pos;
        s.name = n.name;
        if (!s.vendorEntries.empty())
            continue;
        for (Folk const& v : allVendors)
            if (Dist2d(v.pos, n.pos) < 70.f)
                s.vendorEntries.push_back(v.entry);
        s.vendors = uint32(s.vendorEntries.size());
    }
}

void WandererMgr::IndexPlaces(Region& g)
{
    g.placeName.assign(g.net.nodes.size(), std::string());
    for (auto& list : g.byKind)
        list.clear();
    for (size_t i = 0; i < g.net.nodes.size(); ++i)
    {
        Node const& n = g.net.nodes[i];
        std::string area = AreaNameOf(sTerrainMgr.GetAreaId(g.map, n.pos.x, n.pos.y, n.pos.z));
        g.placeName[i] = area.empty() ? n.name : area;
        g.byKind[size_t(n.kind)].push_back(int(i));
    }
}

void WandererMgr::LoadYields(Region& g, std::map<uint32, Spot> const& spots)
{
    g.yields.clear();
    g.goods.clear();
    g.marketSize.clear();
    g.rares.clear();
    for (size_t i = 0; i < g.net.nodes.size(); ++i)
    {
        Node const& n = g.net.nodes[i];
        auto const s = spots.find(n.id);
        std::vector<LootRow> rows;
        switch (n.kind)
        {
            case NodeKind::Fishing:
            {
                // Fishing loot is kept by area, then by zone (vanilla's own fallback).
                uint32 const area = sTerrainMgr.GetAreaId(g.map, n.pos.x, n.pos.y, n.pos.z);
                rows = LootRows("fishing_loot_template", area);
                if (rows.empty())
                    rows = LootRows("fishing_loot_template", n.zone);
                g.yields[int(i)] = YieldFor(Occupation::Fisherman, rows);
                break;
            }
            case NodeKind::Mine:
            case NodeKind::Herbs:
                if (s != spots.end() && !s->second.lootIds.empty())
                    rows = LootRows("gameobject_loot_template", s->second.lootIds.front());
                g.yields[int(i)] = YieldFor(n.kind == NodeKind::Mine ? Occupation::Miner : Occupation::Herbalist, rows);
                break;
            case NodeKind::Wilds:
                if (s != spots.end() && !s->second.lootIds.empty())
                    rows = LootRows("skinning_loot_template", s->second.lootIds.front());
                g.yields[int(i)] = YieldFor(Occupation::Trapper, rows);
                break;
            case NodeKind::Market:
            {
                // What its vendors sell for ever (no limited stock): food, drink, trade goods.
                std::vector<std::pair<uint32, Price>>& list = g.goods[int(i)];
                std::set<uint32> seen;
                if (s != spots.end())
                {
                    g.marketSize[int(i)] = s->second.vendors;
                    for (uint32 entry : s->second.vendorEntries)
                    {
                        CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(entry);
                        VendorItemData const* lists[2] = {sObjectMgr.GetNpcVendorItemList(entry),
                            ci && ci->vendor_id ? sObjectMgr.GetNpcVendorTemplateItemList(ci->vendor_id) : nullptr};
                        for (VendorItemData const* v : lists)
                            for (uint8 k = 0; v && k < v->GetItemCount(); ++k)
                            {
                                VendorItem const* it = v->GetItem(k);
                                ItemPrototype const* p = it ? sObjectMgr.GetItemPrototype(it->item) : nullptr;
                                if (!p || it->maxcount || !p->BuyPrice || p->Quality > 1 || (p->Class != 0 && p->Class != 7)
                                    || !seen.insert(p->ItemId).second || list.size() >= 16)
                                    continue;
                                list.push_back({p->ItemId, {p->SellPrice, p->BuyPrice}});
                            }
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    // The rare creatures that live in the region (for rumours).
    auto rareWorker = [&](CreatureDataPair const& pair) {
        CreatureData const& d = pair.second;
        if (d.position.mapId != g.map || d.position.x < g.minX - 300.f || d.position.x > g.maxX + 300.f
            || d.position.y < g.minY - 300.f || d.position.y > g.maxY + 300.f || g.rares.size() >= 64)
            return false;
        CreatureInfo const* ci = d.creature_id[0] ? sObjectMgr.GetCreatureTemplate(d.creature_id[0]) : nullptr;
        if (ci && (ci->rank == CREATURE_ELITE_RARE || ci->rank == CREATURE_ELITE_RAREELITE))
            g.rares.push_back({ci->name, V(d.position.x, d.position.y, d.position.z)});
        return false;
    };
    sObjectMgr.DoCreatureData(rareWorker);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        "Wanderers: %s: %u fishing waters, %u mines, %u herb meadows, %u hunting grounds, %u markets, %u landmarks, %u rare creatures.",
        g.name.c_str(), uint32(g.byKind[size_t(NodeKind::Fishing)].size()), uint32(g.byKind[size_t(NodeKind::Mine)].size()),
        uint32(g.byKind[size_t(NodeKind::Herbs)].size()), uint32(g.byKind[size_t(NodeKind::Wilds)].size()),
        uint32(g.byKind[size_t(NodeKind::Market)].size()), uint32(g.byKind[size_t(NodeKind::Landmark)].size()), uint32(g.rares.size()));
}

// ── Who it is: a life given, kept, and passed on ───────────────────────────────────────────────

bool WandererMgr::FixHaunts(FusionWanderer& w)
{
    Region const& r = m_regions[w.region];
    Life const& l = w.life;
    if (l.occupation == Occupation::None)
        return r.net.IndexOf(l.home) >= 0;
    return r.net.IndexOf(l.home) >= 0 && r.net.IndexOf(l.work) >= 0 && r.net.IndexOf(l.market) >= 0;
}

void WandererMgr::AssignLife(FusionWanderer& w, bool fresh)
{
    Region const& r = m_regions[w.region];
    RoadNet const& net = r.net;
    Record const& rec = w.rec;
    if (net.nodes.empty())
        return;
    uint32 const gen = w.life.generation;
    uint32 const seed = Mix(rec.seed ^ (0x11FEu + gen * 7919u));
    std::vector<int> settlements;
    for (size_t i = 0; i < net.nodes.size(); ++i)
        if (IsSettlement(net.nodes[i].kind))
            settlements.push_back(int(i));
    auto has = [&](NodeKind k) {
        if (k == NodeKind::Town)
            return !settlements.empty();
        if (k == NodeKind::Board)
            return !r.boards.empty();
        return !r.byKind[size_t(k)].empty();
    };
    Occupation o = w.life.occupation;
    if (fresh || !has(OccupationInfo(o).work))
        o = OccupationFor(seed, has);

    // Home: an inn of its home zone, else a town of it, else any settlement.
    int home = net.IndexOf(w.life.home);
    if (fresh || home < 0)
    {
        std::vector<int> inns, towns;
        for (int i : settlements)
            if (net.nodes[size_t(i)].zone == rec.homeZone)
                (net.nodes[size_t(i)].kind == NodeKind::Inn ? inns : towns).push_back(i);
        std::vector<int> const& pool = !inns.empty() ? inns : !towns.empty() ? towns : settlements;
        home = pool.empty() ? 0 : pool[Mix(seed ^ 0x40E) % pool.size()];
    }
    Vec3 const homeAt = net.nodes[size_t(home)].pos;
    auto byDistance = [&](std::vector<int> list) {
        std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
            return Dist2d(net.nodes[size_t(a)].pos, homeAt) < Dist2d(net.nodes[size_t(b)].pos, homeAt);
        });
        return list;
    };
    // Work: one of the three nearest of its kind to home.
    OccupationDef const& d = OccupationInfo(o);
    int work = net.IndexOf(w.life.work);
    if (fresh || work < 0 || net.nodes[size_t(work)].kind != d.work)
    {
        std::vector<int> const list = byDistance(d.work == NodeKind::Town ? settlements : d.work == NodeKind::Board ? r.boards
            : r.byKind[size_t(d.work)]);
        work = list.empty() ? home : list[Mix(seed ^ 0x3E0) % std::min<size_t>(3, list.size())];
    }
    // Market: the bigger and the nearer likelier.
    int market = net.IndexOf(w.life.market);
    if (fresh || market < 0)
    {
        std::vector<int> const& markets = r.byKind[size_t(NodeKind::Market)];
        float total = 0.f;
        std::vector<std::pair<int, float>> weights;
        for (int m : markets)
        {
            auto const size = r.marketSize.find(m);
            float const wgt = float(1 + (size != r.marketSize.end() ? size->second : 0))
                / (1.f + Dist2d(net.nodes[size_t(m)].pos, homeAt) / 1500.f);
            weights.push_back({m, wgt});
            total += wgt;
        }
        market = home;
        if (total > 0.f)
        {
            float pick = float(Mix(seed ^ 0x3A2) % 10000u) / 10000.f * total;
            for (auto const& [m, wgt] : weights)
            {
                market = m;
                if (pick < wgt)
                    break;
                pick -= wgt;
            }
        }
    }
    std::string const homeName = r.placeName.size() > size_t(home) ? r.placeName[size_t(home)] : net.nodes[size_t(home)].name;
    if (fresh)
    {
        w.life = NewLife(seed, o, rec.race, homeName, TodayNow(), m_lifeDays);
        w.life.generation = gen;
        // A start in its trade: a few of its goods to sell.
        for (Ware const& ware : d.fallback)
            if (Mix(seed ^ ware.item) % 3 == 0 && sObjectMgr.GetItemPrototype(ware.item))
                AddStock(w.life.pack, ware.item, ware.minCount);
    }
    w.life.occupation = o;
    w.life.home = net.nodes[size_t(home)].id;
    w.life.work = net.nodes[size_t(work)].id;
    w.life.market = net.nodes[size_t(market)].id;
    w.life.homeName = homeName;
    w.dirty = true;
}

void WandererMgr::Reborn(FusionWanderer& w, std::set<std::string>& names)
{
    Record& rec = w.rec;
    std::string const oldName = rec.name;
    uint32 const today = TodayNow();
    // Its friends hear it has gone.
    for (auto const& o : m_roster)
    {
        if (o.get() == &w || o->region != w.region)
            continue;
        auto& ties = o->life.ties;
        bool knew = false;
        ties.erase(std::remove_if(ties.begin(), ties.end(), [&](Tie const& t) {
            knew = knew || (t.other == rec.id && t.affinity >= kFriendAt);
            return t.other == rec.id;
        }), ties.end());
        if (knew)
            AddNews(o->life.news, {NewsKind::Gone, oldName, w.life.homeName, today});
        o->dirty = true;
    }
    // Someone new in its place, with its id.
    uint32 const gen = w.life.generation + 1;
    Region const& region = m_regions[w.region];
    ZoneBand const band = BandOf(w);
    rec.seed = Seed(rec.id, 1000 + gen);
    rec.race = RaceFor(region.side, rec.seed);
    rec.gender = uint8(Mix(rec.seed ^ 0x1F83D9ABu) & 1);
    rec.display = PickDisplay(rec.race, rec.gender, rec.seed);
    rec.archetype = ArchetypeFor(rec.seed);
    rec.level = StartLevel(band, rec.seed);
    rec.xp = 0;
    rec.money = 100 + Mix(rec.seed) % 400;
    rec.kills = rec.deaths = rec.bounties = 0;
    rec.trip = Trip();
    rec.goal = Goal::Roam;
    rec.errand = Errand();
    rec.recent.clear();
    rec.respawnInMs = 0;
    rec.lastNode = 0;
    names.erase(oldName);
    rec.name.clear();
    for (uint32 salt = 0; salt < 64 && rec.name.empty(); ++salt)
    {
        std::string const n = MakeName(rec.race, rec.gender, Seed(rec.id, gen * 100 + salt));
        if (!names.count(n) && sObjectMgr.GetPlayerGuidByName(n).IsEmpty())
            rec.name = n;
    }
    if (rec.name.empty())
        rec.name = MakeName(rec.race, rec.gender, Seed(rec.id, gen * 100 + 999));
    names.insert(rec.name);
    w.mind = Mind();
    w.recentIdx.clear();
    w.bonds.clear();
    CharacterDatabase.DirectPExecute("DELETE FROM `aw_wanderer_bond` WHERE `wanderer` = %u", rec.id);
    w.life = Life();
    w.life.generation = gen;
    AssignLife(w, true);
    w.dirty = true;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Wanderers: %s moved away from %s; %s, a %s, has come to live there (#%u).", oldName.c_str(),
        w.life.homeName.c_str(), rec.name.c_str(), OccupationName(w.life.occupation), rec.id);
}

// ── Its day ────────────────────────────────────────────────────────────────────────────────────

Haunts WandererMgr::HauntsOf(FusionWanderer const& w) const
{
    Region const& r = m_regions[w.region];
    RoadNet const& net = r.net;
    Haunts h;
    h.home = net.IndexOf(w.life.home);
    h.work = net.IndexOf(w.life.work);
    h.market = net.IndexOf(w.life.market);
    h.parcelTo = w.life.parcel.Carried() ? net.IndexOf(w.life.parcel.to) : -1;
    NodeKind const workKind = OccupationInfo(w.life.occupation).work;
    if (IsWorkSpot(workKind))
    {
        // Its other spots: the nearest few of its kind to home.
        std::vector<int> list = r.byKind[size_t(workKind)];
        Vec3 const homeAt = h.home >= 0 ? net.nodes[size_t(h.home)].pos : Vec3();
        std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
            return Dist2d(net.nodes[size_t(a)].pos, homeAt) < Dist2d(net.nodes[size_t(b)].pos, homeAt);
        });
        for (int i : list)
            if (i != h.work && h.works.size() < 3)
                h.works.push_back(i);
    }
    h.markets = r.byKind[size_t(NodeKind::Market)];
    h.inns = r.byKind[size_t(NodeKind::Inn)];
    h.landmarks = r.byKind[size_t(NodeKind::Landmark)];
    h.boards = r.boards;
    for (size_t i = 0; i < net.nodes.size(); ++i)
        if (IsSettlement(net.nodes[i].kind))
            h.settlements.push_back(int(i));
    // Its best friend's whereabouts: the settlement it is at, if it is at one.
    if (uint32 const f = BestFriend(w.life.ties))
        if (FusionWanderer const* o = ById(f); o && o->region == w.region && o->mind.state != State::Dead && o->rec.trip.Done())
            if (int const at = net.IndexOf(o->rec.lastNode); at >= 0 && IsSettlement(net.nodes[size_t(at)].kind))
                h.friendAt = at;
    return h;
}

DayContext WandererMgr::DayFor(FusionWanderer const& w) const
{
    DayContext c;
    c.occupation = w.life.occupation;
    c.traits = w.life.traits;
    c.needs = w.life.needs;
    c.hour = HourNow();
    c.day = TodayNow();
    c.seed = w.rec.seed;
    c.sessions = w.life.sessions;
    c.packItems = PackItems(w.life.pack);
    c.parcel = w.life.parcel.Carried();
    c.lastWasDetour = w.life.lastWasDetour;
    c.friendKnown = BestFriend(w.life.ties) != 0;
    return c;
}

int WandererMgr::PlanLife(FusionWanderer& w, Vec3 from, uint32 roll)
{
    Region const& r = m_regions[w.region];
    RoadNet const& net = r.net;
    int const at = NearestNode(net, from);
    DayContext c = DayFor(w);
    Haunts const h = HauntsOf(w);
    if (h.friendAt < 0)
        c.friendKnown = false;
    Fusion::Wanderers::Plan p = Fusion::Wanderers::PlanNext(c, h, net, at, w.recentIdx, roll);
    if (p.activity == Activity::Bounty)
    {
        uint32 const now = WorldTimer::getMSTime();
        if (m_bounties && Due(now, w.mind.nextBountyMs) && BoardHasBounty(w))
        {
            // The Phase 1 errand: the board, the hunt, the turn-in; its life after.
            w.mind.goal = Goal::TakeBounty;
            w.mind.nextBountyMs = now + kBountyCooldownMs;
            w.life.doing = Activity::Bounty;
            w.life.doingNode = 0;
            w.dirty = true;
            return -1;
        }
        p.activity = Activity::Rest;
    }
    if (p.node < 0)
        return -1;
    w.life.doing = p.activity;
    w.life.doingNode = net.nodes[size_t(p.node)].id;
    w.life.lastWasDetour = p.detour;
    w.dirty = true;
    return p.node;
}

uint32 WandererMgr::StartActivity(FusionWanderer& w, uint32 now, Creature* c)
{
    (void)c;
    uint32 const ms = std::max<uint32>(5000, ActivityMs(w.life.doing, OccupationInfo(w.life.occupation), w.life.traits, HourNow(),
        urand(0, 0x7FFFFFFF)));
    w.activityOn = true;
    w.doingStartMs = now;
    w.doingMs = ms;
    w.nextLineMs = now + urand(10000, 40000);
    return ms;
}

void WandererMgr::EndActivity(FusionWanderer& w, uint32 now, Creature* c)
{
    if (!w.activityOn)
        return;
    w.activityOn = false;
    Region const& r = m_regions[w.region];
    Life& l = w.life;
    uint32 const elapsed = std::min<uint32>(now - w.doingStartMs, w.doingMs);
    bool const full = elapsed * 2 >= w.doingMs;
    uint32 const roll = urand(0, 0x7FFFFFFF);
    int const node = r.net.IndexOf(l.doingNode);
    std::string const place = node >= 0 && size_t(node) < r.placeName.size() ? r.placeName[size_t(node)] : std::string();
    OccupationDef const& d = OccupationInfo(l.occupation);
    switch (l.doing)
    {
        case Activity::Work:
            if (d.gathers)
            {
                // Its gathers, from the spot's own loot table (or its trade's wares).
                static std::map<Occupation, YieldTable> fallbacks;
                YieldTable const* t = nullptr;
                if (auto const it = r.yields.find(node); it != r.yields.end())
                    t = &it->second;
                else
                {
                    if (!fallbacks.count(l.occupation))
                        fallbacks[l.occupation] = YieldFor(l.occupation, {});
                    t = &fallbacks[l.occupation];
                }
                for (Stock const& s : WorkYield(*t, elapsed, roll))
                {
                    if (!sObjectMgr.GetItemPrototype(s.item))
                        continue;
                    AddStock(l.pack, s.item, s.count, s.rare);
                    if (s.rare)
                    {
                        News(w, NewsKind::Catch, "a " + ItemName(s.item), place);
                        Nudge(l.needs, 10);
                    }
                }
                if (full)
                    l.sessions = uint8(std::min<uint32>(l.sessions + 1u, 255));
            }
            else if (l.occupation == Occupation::Courier && full)
            {
                // A parcel from here to a settlement some way off.
                std::vector<int> distant;
                for (size_t i = 0; i < r.net.nodes.size(); ++i)
                    if (IsSettlement(r.net.nodes[i].kind) && node >= 0 && Dist2d(r.net.nodes[i].pos, r.net.nodes[size_t(node)].pos) > 600.f)
                        distant.push_back(int(i));
                if (!distant.empty())
                {
                    int const to = distant[roll % distant.size()];
                    float const yards = Dist2d(r.net.nodes[size_t(to)].pos, r.net.nodes[size_t(node)].pos);
                    l.parcel = {l.doingNode, r.net.nodes[size_t(to)].id, CourierPay(w.rec.level, yards), "the folk of " + place};
                }
            }
            break;
        case Activity::Sell:
        {
            Sale const s = SellAtMarket(l.pack, [this](uint32 item) { return PriceOf(item); }, l.traits);
            w.rec.money += s.copper;
            l.earned += s.copper;
            l.sessions = 0;
            if (s.copper >= 50)
            {
                News(w, NewsKind::Sold, MoneyText(s.copper), place);
                Nudge(l.needs, 5);
            }
            break;
        }
        case Activity::Stock:
            if (auto const g = r.goods.find(node); g != r.goods.end() && full)
                StockUp(g->second, w.rec.money, l.traits, l.pack, roll);
            break;
        case Activity::Deliver:
            if (l.parcel.Carried() && l.parcel.to == l.doingNode)
            {
                w.rec.money += l.parcel.pay;
                l.earned += l.parcel.pay;
                l.parcel = Parcel();
                Nudge(l.needs, 4);
            }
            break;
        case Activity::Perform:
        {
            uint32 listeners = urand(0, 3);
            if (c)
            {
                listeners = 0;
                for (auto it = c->GetMap()->GetPlayers().begin(); it != c->GetMap()->GetPlayers().end(); ++it)
                    if (Player* p = it->getSource(); p && p->IsWithinDistInMap(c, 25.f))
                        ++listeners;
            }
            uint32 const tips = full ? Tips(w.rec.level, listeners, roll) : 0;
            // Players resting near hear it as Minstrelsy's inn song (FusionMinstrelsy.cpp).
            if (c && full)
                sMinstrelMgr.WandererSong(c);
            w.rec.money += tips;
            l.earned += tips;
            break;
        }
        case Activity::Eat:
        case Activity::Sleep:
        {
            uint32 const cost = InnCost(w.rec.level, l.doing);
            if (w.rec.money >= cost)
                w.rec.money -= cost;
            else
                Nudge(l.needs, -5);
            if (l.doing == Activity::Eat)
                l.needs.hunger = 0;
            else
                l.needs.fatigue = full ? 0 : uint8(l.needs.fatigue / 2);
            break;
        }
        case Activity::Rest: l.needs.fatigue = uint8(std::max(0, int(l.needs.fatigue) - 25)); break;
        case Activity::Socialize: Nudge(l.needs, 8); break;
        case Activity::Sightsee: Nudge(l.needs, 6); break;
        case Activity::Visit: Nudge(l.needs, 4); break;
        default: break;
    }
    l.doing = Activity::None;
    w.dirty = true;
}

void WandererMgr::ActivityPose(Creature* c, FusionWanderer const& w) const
{
    // Its gun put away, and its body at the work.
    c->SetSheath(SHEATH_STATE_UNARMED);
    c->m_fusionGunId = 0;
    uint32 emote = 0;
    uint8 stand = UNIT_STAND_STATE_STAND;
    switch (w.life.doing)
    {
        case Activity::Sleep: stand = UNIT_STAND_STATE_SLEEP; break;
        case Activity::Eat:
        case Activity::Rest:
        case Activity::Socialize: stand = UNIT_STAND_STATE_SIT; break;
        case Activity::Perform: emote = EMOTE_STATE_DANCE; break;
        case Activity::Work:
            switch (w.life.occupation)
            {
                case Occupation::Fisherman: emote = EMOTE_STATE_USESTANDING; break;
                case Occupation::Miner: emote = EMOTE_STATE_WORK; break;
                case Occupation::Herbalist:
                case Occupation::Trapper: emote = EMOTE_STATE_KNEEL; break;
                default: break;
            }
            break;
        default: break;
    }
    c->SetStandState(stand);
    c->SetUInt32Value(UNIT_NPC_EMOTESTATE, emote);
    if (w.life.doing == Activity::Eat)
        c->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
    else if (w.life.doing == Activity::Sell || w.life.doing == Activity::Stock)
        c->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
    else if (w.life.doing == Activity::Sightsee)
        c->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
}

// ── Talk ───────────────────────────────────────────────────────────────────────────────────────

std::string WandererMgr::NodeName(FusionWanderer const& w, uint32 nodeId) const
{
    Region const& r = m_regions[w.region];
    int const i = r.net.IndexOf(nodeId);
    if (i < 0)
        return std::string();
    return size_t(i) < r.placeName.size() && !r.placeName[size_t(i)].empty() ? r.placeName[size_t(i)] : r.net.nodes[size_t(i)].name;
}

std::string WandererMgr::PlaceNear(Region const& r, Vec3 p) const
{
    int const i = NearestNode(r.net, p);
    return i >= 0 && size_t(i) < r.placeName.size() ? r.placeName[size_t(i)] : std::string();
}

Price WandererMgr::PriceOf(uint32 item) const
{
    ItemPrototype const* p = sObjectMgr.GetItemPrototype(item);
    return p ? Price{p->SellPrice, p->BuyPrice} : Price{};
}

void WandererMgr::News(FusionWanderer& w, NewsKind kind, std::string const& what, std::string const& where)
{
    AddNews(w.life.news, {kind, what, where, TodayNow()});
    w.dirty = true;
}

Talk WandererMgr::TalkOf(FusionWanderer const& w, std::string const& to, Creature const* c) const
{
    Region const& r = m_regions[w.region];
    Talk t;
    t.name = w.rec.name;
    t.player = to;
    t.home = w.life.homeName;
    t.work = NodeName(w, w.life.work);
    t.market = NodeName(w, w.life.market);
    Vec3 const at = c ? PosOf(c) : TripPos(r.net, w.rec.trip);
    t.here = PlaceNear(r, at);
    if (!w.rec.trip.Done() || !w.activityOn)
        t.heading = NodeName(w, w.life.doingNode);
    if (uint32 const f = BestFriend(w.life.ties))
        if (FusionWanderer const* o = ById(f))
            t.friendName = o->rec.name;
    if (uint32 const e = WorstRival(w.life.ties))
        if (FusionWanderer const* o = ById(e))
            t.rivalName = o->rec.name;
    if (c && c->GetMap()->GetWeatherSystem())
        if (Weather const* wt = c->GetMap()->GetWeatherSystem()->FindWeather(c->GetZoneId()); wt && wt->GetGrade() > 0.25f)
            switch (wt->GetType())
            {
                case WEATHER_TYPE_RAIN: t.weather = "rain"; break;
                case WEATHER_TYPE_SNOW: t.weather = "snow"; break;
                case WEATHER_TYPE_STORM: t.weather = "storm"; break;
                default: break;
            }
    t.occupation = w.life.occupation;
    t.doing = w.life.doing;
    t.traits = w.life.traits;
    t.needs = w.life.needs;
    t.hour = HourNow();
    // Its news while it is fresh (two days).
    if (!w.life.news.empty() && w.life.news.front().day + 2 >= TodayNow())
        t.news = w.life.news.front();
    return t;
}

bool WandererMgr::Speak(Creature* c, FusionWanderer& w, std::string const& line, Unit const* to, bool yell, bool force)
{
    if (!m_chat || !LineIsClean(line) || !c)
        return false;
    uint32 const now = WorldTimer::getMSTime();
    if (!force && !Due(now, w.nextLineMs))
        return false;
    auto const b = m_budget.find(c->GetMapId());
    if (b == m_budget.end() || !MaySpeak(const_cast<ChatBudget&>(b->second), now, m_linesPerMinute))
        return false;
    w.nextLineMs = now + m_lineGapMs;
    Say(c, w, line, to, yell);
    return true;
}

// ── In sight: talk, chats, a hire ──────────────────────────────────────────────────────────────

bool WandererMgr::LifeInWorld(Creature* c, FusionWanderer& w, uint32 now)
{
    State const st = w.mind.state;
    bool const fighting = st == State::Fight || st == State::Flee || st == State::Dead || st == State::Loot;
    // A hire: walking with its employer until the time is up or they part.
    if (w.hiredBy)
    {
        Player* p = c->GetMap()->GetPlayer(ObjectGuid(w.hiredBy));
        if (!p || !p->IsAlive() || Due(now, w.hiredUntilMs) || c->GetDistance(p) > 100.f)
            EndHire(c, w, p != nullptr);
        else if (!fighting && !w.attacker && !w.bestTarget && !w.following)
        {
            c->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
            c->SetStandState(UNIT_STAND_STATE_STAND);
            c->SetSheath(SHEATH_STATE_RANGED);
            c->m_fusionGunId = w.gun;
            if (w.activityOn)
                EndActivity(w, now, c);
            w.mind.state = State::Travel;
            c->GetMotionMaster()->MoveFollow(p, 2.5f, float(M_PI) * 0.75f);
            w.following = true;
        }
        if (w.hiredBy)
            return true;
    }
    if (fighting)
    {
        w.chatWith = 0;
        return false;
    }
    // Spoken to: it stands and faces them.
    if (!Due(now, w.talkUntilMs))
    {
        if (w.moving || !c->movespline->Finalized())
        {
            c->StopMoving();
            w.moving = false;
        }
        return true;
    }
    // A chat with another wanderer.
    if (w.chatWith)
    {
        if (!Due(now, w.chatUntilMs))
        {
            if (w.replyAtMs && Due(now, w.replyAtMs))
            {
                w.replyAtMs = 0;
                Speak(c, w, w.replyLine, nullptr, false, true);
                c->HandleEmoteCommand(urand(0, 1) ? EMOTE_ONESHOT_LAUGH : EMOTE_ONESHOT_TALK);
            }
            return true;
        }
        w.chatWith = 0;
        return false;
    }
    if (w.nearWanderer && (st == State::Travel || st == State::Idle) && Due(now, w.lastChatMs + kChatAgainMs))
    {
        Creature* oc = c->GetMap()->GetCreature(ObjectGuid(w.nearWanderer));
        FusionWanderer* o = oc && oc->IsAlive() ? oc->m_wanderer.get() : nullptr;
        if (o && o != &w && !o->chatWith && !o->hiredBy && o->rec.side == w.rec.side && Due(now, o->lastChatMs + kChatAgainMs)
            && (o->mind.state == State::Travel || o->mind.state == State::Idle) && Due(now, o->talkUntilMs))
        {
            w.lastChatMs = o->lastChatMs = now;
            Tie const* tie = TieWith(w.life.ties, o->rec.id);
            int const aff = tie ? tie->affinity : 0;
            if (StopsToChat(w.life.traits, o->life.traits, aff, urand(0, 0x7FFFFFFF)))
            {
                uint32 const roll = urand(0, 0x7FFFFFFF);
                Meet(w.life.ties, o->rec.id, w.life.traits, o->life.traits, roll);
                Meet(o->life.ties, w.rec.id, o->life.traits, w.life.traits, roll + 1);
                std::string const where = PlaceNear(m_regions[w.region], PosOf(c));
                if (Tie const* t = TieWith(w.life.ties, o->rec.id); t && t->affinity >= kFriendAt)
                {
                    News(w, NewsKind::Friend, o->rec.name, where);
                    News(*o, NewsKind::Friend, w.rec.name, where);
                }
                w.chatWith = o->rec.id;
                o->chatWith = w.rec.id;
                w.chatUntilMs = o->chatUntilMs = now + kChatMs;
                c->StopMoving();
                oc->StopMoving();
                w.moving = o->moving = false;
                c->SetFacingToObject(oc);
                oc->SetFacingToObject(c);
                c->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                if (w.nearestPlayer < 40.f)
                {
                    Talk a = TalkOf(w, o->rec.name, c);
                    a.bondTier = aff >= kFriendAt ? 2 : 0;
                    Speak(c, w, ChatLine(a, roll), nullptr, false, true);
                    Talk b = TalkOf(*o, w.rec.name, oc);
                    Tie const* back = TieWith(o->life.ties, w.rec.id);
                    b.bondTier = back && back->affinity >= kFriendAt ? 2 : 0;
                    if (back && back->affinity <= kRivalAt)
                        b.rivalName = w.rec.name;
                    o->replyLine = ReplyLine(b, roll + 7);
                    o->replyAtMs = now + 3500;
                }
                w.dirty = o->dirty = true;
                return true;
            }
        }
    }
    // A remark at its work now and then, for a player near.
    if (w.activityOn && st == State::Rest && w.nearestPlayer < 20.f && Due(now, w.nextLineMs))
    {
        w.nextLineMs = now + m_lineGapMs + urand(0, m_lineGapMs);
        if (urand(0, 2) == 0)
            Speak(c, w, ActivityLine(TalkOf(w, std::string(), c), urand(0, 0x7FFFFFFF)), nullptr, false, true);
    }
    return false;
}

void WandererMgr::EndHire(Creature* c, FusionWanderer& w, bool sayBye)
{
    w.hiredBy = 0;
    w.hiredUntilMs = 0;
    w.following = false;
    if (!c || !c->IsAlive())
        return;
    c->GetMotionMaster()->MoveIdle();
    if (sayBye)
        Speak(c, w, "This is where we part. Safe roads.", nullptr, false, true);
    w.mind.state = State::Travel;
    PlanNext(w, PosOf(c), urand(0, 0x7FFFFFFF));
}

// ── The world thread: events, needs, meetings ──────────────────────────────────────────────────

void WandererMgr::NoteWorldEvent(uint32 mapId, Vec3 at, NewsKind kind, std::string const& who)
{
    if (!m_enabled || !m_lives)
        return;
    std::lock_guard<std::mutex> lock(m_eventLock);
    if (m_events.size() < 64)
        m_events.push_back({mapId, at, kind, who});
}

void WandererMgr::TakeEvents()
{
    std::vector<WorldEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_eventLock);
        events.swap(m_events);
    }
    for (WorldEvent const& e : events)
        for (auto const& wp : m_roster)
        {
            FusionWanderer& w = *wp;
            Region const& r = m_regions[w.region];
            if (r.map != e.map || w.mind.state == State::Dead)
                continue;
            Map* map = sMapMgr.FindMap(r.map, 0);
            Creature* c = w.spawned && map ? map->GetCreature(w.guid) : nullptr;
            Vec3 const at = c ? PosOf(c) : TripPos(r.net, w.rec.trip);
            if (Dist2d(at, e.at) > kEventRadius)
                continue;
            std::string const where = PlaceNear(r, e.at);
            News(w, e.kind, e.kind == NewsKind::Building ? e.who : std::string(), where);
            if (c && c->IsAlive() && w.nearestPlayer < 60.f && w.mind.state != State::Fight)
            {
                Speak(c, w, EventLine(e.kind, TalkOf(w, e.who, c), urand(0, 0x7FFFFFFF)), nullptr, false, true);
                c->HandleEmoteCommand(w.life.traits.brave >= 50 ? EMOTE_ONESHOT_POINT : EMOTE_ONESHOT_EXCLAMATION);
            }
            if (e.kind == NewsKind::Aircraft)
                Nudge(w.life.needs, w.life.traits.brave >= 50 ? 2 : -4);
        }
}

void WandererMgr::TickLives(uint32 now)
{
    (void)now;
    m_lifeTickMs += 1000;
    if (m_lifeTickMs < 600000)
        return;
    m_lifeTickMs = 0;
    for (auto const& wp : m_roster)
    {
        FusionWanderer& w = *wp;
        if (w.mind.state == State::Dead)
            continue;
        TickNeeds(w.life.needs, w.activityOn ? w.life.doing : Activity::None, 600000);
        w.dirty = true;
    }
}

void WandererMgr::MeetOnPaper(uint32 now)
{
    m_meetMs += 1000;
    if (m_meetMs < 30000)
        return;
    m_meetMs = 0;
    // Two unseen at the same place, both stopped there: a word, and their tie grows.
    for (size_t i = 0; i < m_roster.size(); ++i)
    {
        FusionWanderer& a = *m_roster[i];
        if (a.spawned || a.mind.state == State::Dead || !a.rec.trip.Done() || !Due(now, a.lastChatMs + kChatAgainMs))
            continue;
        for (size_t j = i + 1; j < m_roster.size(); ++j)
        {
            FusionWanderer& b = *m_roster[j];
            if (b.spawned || b.region != a.region || b.mind.state == State::Dead || !b.rec.trip.Done() || b.rec.lastNode != a.rec.lastNode
                || !Due(now, b.lastChatMs + kChatAgainMs))
                continue;
            a.lastChatMs = b.lastChatMs = now;
            Tie const* tie = TieWith(a.life.ties, b.rec.id);
            if (!StopsToChat(a.life.traits, b.life.traits, tie ? tie->affinity : 0, urand(0, 0x7FFFFFFF)))
                continue;
            uint32 const roll = urand(0, 0x7FFFFFFF);
            Meet(a.life.ties, b.rec.id, a.life.traits, b.life.traits, roll);
            Meet(b.life.ties, a.rec.id, b.life.traits, a.life.traits, roll + 1);
            if (Tie const* t = TieWith(a.life.ties, b.rec.id); t && t->affinity >= kFriendAt)
            {
                std::string const where = NodeName(a, a.rec.lastNode);
                News(a, NewsKind::Friend, b.rec.name, where);
                News(b, NewsKind::Friend, a.rec.name, where);
                Nudge(a.life.needs, 3);
                Nudge(b.life.needs, 3);
            }
            a.dirty = b.dirty = true;
            break;
        }
    }
}

// ── Talking to one ─────────────────────────────────────────────────────────────────────────────

bool WandererMgr::GossipHello(Player* player, Creature* c)
{
    if (!m_enabled || !m_lives || !c || !c->m_wanderer || !player)
        return false;
    FusionWanderer& w = *c->m_wanderer;
    PlayerMenu* menu = player->PlayerTalkClass;
    menu->ClearMenus();
    if (c->IsHostileTo(player) || w.mind.state == State::Fight || w.mind.state == State::Flee || w.mind.state == State::Dead)
    {
        menu->CloseGossip();
        return true;
    }
    uint32 const now = WorldTimer::getMSTime();
    bool const first = Due(now, w.talkUntilMs) || w.talkTo != player->GetObjectGuid().GetRawValue();
    w.talkUntilMs = now + kTalkHoldMs;
    w.talkTo = player->GetObjectGuid().GetRawValue();
    c->StopMoving();
    w.moving = false;
    c->SetFacingToObject(player);
    FusionWandererBond& b = w.bonds[player->GetGUIDLow()];
    b.lastMet = uint32(time(nullptr));
    b.dirty = true;
    w.dirty = true;
    if (first)
    {
        // Woken, it is grumpy; otherwise a hello by how well it knows them.
        if (w.activityOn && w.life.doing == Activity::Sleep)
            Speak(c, w, "Mm? It's the middle of the night...", player, false, true);
        else
            Speak(c, w, GreetLine(w.rec.seed, BondTier(b.points), player->GetName(), b.greets), player, false, true);
    }
    GossipMenu& g = menu->GetGossipMenu();
    g.AddMenuItem(GOSSIP_ICON_CHAT, "Tell me about yourself.", kTalkSender, kAbout, "", false);
    g.AddMenuItem(GOSSIP_ICON_CHAT, "Where are you headed?", kTalkSender, kHeading, "", false);
    g.AddMenuItem(GOSSIP_ICON_CHAT, "What's the news?", kTalkSender, kNews, "", false);
    g.AddMenuItem(GOSSIP_ICON_TALK, "Heard any rumours?", kTalkSender, kRumours, "", false);
    if (m_trade && !w.life.pack.empty())
        g.AddMenuItem(GOSSIP_ICON_VENDOR, "Let me see your wares.", kTalkSender, kWares, "", false);
    if (m_hire && w.hiredBy == player->GetObjectGuid().GetRawValue())
        g.AddMenuItem(GOSSIP_ICON_CHAT, "That's far enough. Go your own way.", kTalkSender, kDismiss, "", false);
    else if (m_hire && !w.hiredBy)
    {
        uint32 const price = HirePrice(w.rec.level, w.life.traits, w.life.occupation, BondTier(b.points));
        std::string const text = "Walk with me a while. (" + MoneyText(price) + ")";
        std::string const box = "Pay " + w.rec.name + " " + MoneyText(price) + " to walk with you for ten minutes?";
        g.AddMenuItem(GOSSIP_ICON_MONEY_BAG, text, kTalkSender, kHire, box, false);
    }
    // Some wanderers battle companions (fusion/CompanionBattles.h, design/COMPANION_BATTLES.md).
    if (Fusion::CompanionBattles::IsWandererTamer(w.rec.seed) && sFusionMgr.IsEnabled())
        g.AddMenuItem(GOSSIP_ICON_BATTLE, "Fancy a companion duel?", kTalkSender, kBattle, "", false);
    g.AddMenuItem(GOSSIP_ICON_CHAT, "Farewell.", kTalkSender, kFarewell, "", false);
    menu->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, c->GetObjectGuid());
    return true;
}

bool WandererMgr::GossipSelect(Player* player, Creature* c, uint32 sender, uint32 action)
{
    if (sender != kTalkSender || !m_enabled || !m_lives || !c || !c->m_wanderer)
        return false;
    FusionWanderer& w = *c->m_wanderer;
    uint32 const now = WorldTimer::getMSTime();
    FusionWandererBond& b = w.bonds[player->GetGUIDLow()];
    Talk talk = TalkOf(w, player->GetName(), c);
    talk.bondTier = BondTier(b.points);
    uint32 const seed = urand(0, 0x7FFFFFFF);
    bool again = true;
    switch (action)
    {
        case kAbout: Speak(c, w, AboutLine(talk, w.life.backstory, seed), player, false, true); break;
        case kHeading: Speak(c, w, HeadingLine(talk, seed), player, false, true); break;
        case kNews:
        {
            std::string line = NewsLine(talk, seed);
            if (line.empty())
                line = ChatLine(talk, seed);
            Speak(c, w, line, player, false, true);
            break;
        }
        case kRumours:
        {
            // A bounty on the nearest board, or a rare creature seen near a place.
            Region const& r = m_regions[w.region];
            std::string what, where;
            bool bounty = false;
            if (seed % 2 == 0 && sBountyMgr.IsEnabled())
            {
                int best = -1;
                for (int bi : r.boards)
                    if (best < 0 || Dist2d(r.net.nodes[size_t(bi)].pos, PosOf(c)) < Dist2d(r.net.nodes[size_t(best)].pos, PosOf(c)))
                        best = bi;
                if (best >= 0)
                {
                    std::vector<BountyMgr::KillPosting> const posts = sBountyMgr.KillPostings(r.net.nodes[size_t(best)].zone);
                    if (!posts.empty())
                    {
                        what = posts[seed % posts.size()].title;
                        where = r.placeName[size_t(best)];
                        bounty = true;
                    }
                }
            }
            if (what.empty() && !r.rares.empty())
            {
                Region::Rare const& rare = r.rares[seed % r.rares.size()];
                what = rare.name;
                where = PlaceNear(r, rare.pos);
            }
            Speak(c, w, what.empty() ? NoRumourLine(seed) : RumourLine(what, where, bounty, seed), player, false, true);
            break;
        }
        case kWares:
            player->PlayerTalkClass->CloseGossip();
            SendWares(player, c);
            return true;
        case kHire:
        {
            again = false;
            uint32 const price = HirePrice(w.rec.level, w.life.traits, w.life.occupation, talk.bondTier);
            if (!m_hire || w.hiredBy)
                break;
            if (!AcceptsHire(w.life.traits, w.life.occupation, b.points))
            {
                Speak(c, w, HireLine(talk, false, seed), player, false, true);
                break;
            }
            if (player->GetMoney() < price)
            {
                Speak(c, w, std::string("Come back when your purse is heavier, ") + player->GetName() + ".", player, false, true);
                break;
            }
            player->ModifyMoney(-int32(price));
            w.rec.money += price;
            w.life.earned += price;
            w.hiredBy = player->GetObjectGuid().GetRawValue();
            w.hiredUntilMs = now + kHireMs;
            w.following = false;
            w.talkUntilMs = 0;
            ++b.traded;
            b.points += 2;
            b.dirty = true;
            w.dirty = true;
            Speak(c, w, HireLine(talk, true, seed), player, false, true);
            c->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
            sFusionMgr.CountStat(player, {Fusion::Stat::WanderersHired, Fusion::kAnyKey});
            break;
        }
        case kBattle:
            again = false;
            player->PlayerTalkClass->CloseGossip();
            if (Fusion::CompanionBattles::IsWandererTamer(w.rec.seed)
                && sFusionMgr.StartWandererBattle(player, c, w.rec.name, w.rec.level, w.rec.seed))
            {
                // It stands for the battle; the battle keeps it standing (HoldForBattle).
                w.talkUntilMs = now + kTalkHoldMs;
                w.talkTo = player->GetObjectGuid().GetRawValue();
            }
            return true;
        case kDismiss:
            again = false;
            if (w.hiredBy == player->GetObjectGuid().GetRawValue())
                EndHire(c, w, true);
            break;
        default:
            again = false;
            w.talkUntilMs = now + 2000;
            c->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
            break;
    }
    if (again)
        GossipHello(player, c);
    else
        player->PlayerTalkClass->CloseGossip();
    return true;
}

void WandererMgr::HoldForBattle(Creature* c, uint64 player, uint32 ms)
{
    if (!c || !c->m_wanderer)
        return;
    FusionWanderer& w = *c->m_wanderer;
    w.talkUntilMs = WorldTimer::getMSTime() + ms;
    w.talkTo = player;
    w.moving = false;
}

bool WandererMgr::SendWares(Player* player, Creature* c)
{
    if (!m_enabled || !m_lives || !c || !c->m_wanderer)
        return false;
    FusionWanderer& w = *c->m_wanderer;
    w.talkUntilMs = WorldTimer::getMSTime() + kTalkHoldMs;
    w.talkTo = player->GetObjectGuid().GetRawValue();
    uint8 const tier = BondTier(w.bonds.count(player->GetGUIDLow()) ? w.bonds[player->GetGUIDLow()].points : 0);
    WorldPacket data(SMSG_LIST_INVENTORY, 8 + 1 + w.life.pack.size() * 7 * 4);
    data << c->GetObjectGuid();
    size_t const countPos = data.wpos();
    data << uint8(0);
    uint8 count = 0;
    if (m_trade)
        for (Stock const& s : w.life.pack)
        {
            ItemPrototype const* p = sObjectMgr.GetItemPrototype(s.item);
            if (!p || count >= MAX_VENDOR_ITEMS)
                continue;
            ++count;
            data << uint32(count);
            data << uint32(s.item);
            data << uint32(p->DisplayInfoID);
            data << uint32(s.count);
            data << uint32(StallPrice({p->SellPrice, p->BuyPrice}, w.life.traits, s.rare, tier));
            data << uint32(p->MaxDurability);
            data << uint32(1);
        }
    if (!count)
    {
        data << uint8(0);   // "Vendor has no inventory"
        player->GetSession()->SendPacket(&data);
        return true;
    }
    data.put<uint8>(countPos, count);
    player->GetSession()->SendPacket(&data);
    return true;
}

bool WandererMgr::BuyFrom(Player* player, ObjectGuid vendor, uint32 item, uint8 count)
{
    if (!m_enabled || !m_lives || !player)
        return false;
    Creature* c = player->GetNPCIfCanInteractWith(vendor, UNIT_NPC_FLAG_VENDOR);
    if (!c || !c->m_wanderer)
        return false;
    FusionWanderer& w = *c->m_wanderer;
    if (!m_trade)
    {
        player->SendBuyError(BUY_ERR_CANT_FIND_ITEM, c, item, 0);
        return true;
    }
    count = std::max<uint8>(1, count);
    ItemPrototype const* p = sObjectMgr.GetItemPrototype(item);
    uint32 const have = CountStock(w.life.pack, item);
    if (!p || have == 0)
    {
        player->SendBuyError(BUY_ERR_ITEM_ALREADY_SOLD, c, item, 0);
        return true;
    }
    if (have < count)
    {
        player->SendBuyError(BUY_ERR_ITEM_SOLD_OUT, c, item, 0);
        return true;
    }
    bool rare = false;
    for (Stock const& s : w.life.pack)
        rare = rare || (s.item == item && s.rare);
    FusionWandererBond& b = w.bonds[player->GetGUIDLow()];
    uint32 const total = StallPrice({p->SellPrice, p->BuyPrice}, w.life.traits, rare, BondTier(b.points)) * count;
    if (player->GetMoney() < total)
    {
        player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, c, item, 0);
        return true;
    }
    ItemPosCountVec dest;
    InventoryResult const msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item, count);
    if (msg != EQUIP_ERR_OK)
    {
        player->SendEquipError(msg, nullptr, nullptr, 0, item);
        return true;
    }
    player->ModifyMoney(-int32(total));
    if (Item* it = player->StoreNewItem(dest, item, true, Item::GenerateItemRandomPropertyId(item)))
        player->SendNewItem(it, count, true, false);
    TakeStock(w.life.pack, item, count);
    sFusionMgr.CountStat(player, {Fusion::Stat::WandererPurchases, Fusion::kAnyKey});
    w.rec.money += total;
    w.life.earned += total;
    ++b.traded;
    b.points += 1;
    b.dirty = true;
    w.dirty = true;
    Nudge(w.life.needs, 2);
    if (urand(0, 2) == 0)
        Speak(c, w, rare ? "A rare one, that. You've a good eye." : "Pleasure doing business.", player, false, true);
    SendWares(player, c);
    return true;
}

void WandererMgr::OnHelpKill(Unit* killer, Unit* victim)
{
    if (!m_enabled || !m_lives || !killer || !victim)
        return;
    Player* p = killer->GetCharmerOrOwnerPlayerOrPlayerItself();
    Creature* vc = victim->ToCreature();
    if (!p || !vc || vc->m_wanderer)
        return;
    Unit* on = vc->GetVictim();
    Creature* wc = on ? on->ToCreature() : nullptr;
    if (!wc || !wc->m_wanderer || !wc->IsAlive() || wc->IsHostileTo(p))
        return;
    FusionWanderer& w = *wc->m_wanderer;
    FusionWandererBond& b = w.bonds[p->GetGUIDLow()];
    ++b.helped;
    b.points += 3;
    b.lastMet = uint32(time(nullptr));
    b.dirty = true;
    News(w, NewsKind::Helped, p->GetName(), PlaceNear(m_regions[w.region], PosOf(wc)));
    Nudge(w.life.needs, 5);
    Talk t = TalkOf(w, p->GetName(), wc);
    Speak(wc, w, ThanksLine(t, urand(0, 0x7FFFFFFF)), p, false, true);
}

// ── GM ─────────────────────────────────────────────────────────────────────────────────────────

bool WandererMgr::Goto(Player* gm, uint32 id, std::string& why)
{
    FusionWanderer* w = ById(id);
    if (!m_enabled || !w)
    {
        why = m_enabled ? "No such wanderer (.wanderer list)." : "Wanderers are off (Wanderers.Enable).";
        return false;
    }
    Region const& r = m_regions[w->region];
    Vec3 at = TripPos(r.net, w->rec.trip);
    if (w->spawned)
        if (Map* map = sMapMgr.FindMap(r.map, 0))
            if (Creature* c = map->GetCreature(w->guid))
                at = PosOf(c);
    float z = at.z;
    float const ground = sTerrainMgr.LoadTerrain(r.map)->GetHeightStatic(at.x, at.y, at.z + 30.f, true, 80.f);
    if (ground > INVALID_HEIGHT)
        z = ground;
    gm->TeleportTo(r.map, at.x, at.y, z + 1.f, gm->GetOrientation());
    why = "To #" + std::to_string(id) + " " + w->rec.name + ", near " + PlaceNear(r, at) + (w->spawned ? "." : " (it comes into the world as you arrive).");
    return true;
}

std::vector<std::string> WandererMgr::LifeInfo(FusionWanderer const& w) const
{
    std::vector<std::string> out;
    Region const& r = m_regions[w.region];
    Life const& l = w.life;
    Traits const& t = l.traits;
    uint32 const today = TodayNow();
    {
        std::ostringstream s;
        s << "Trade: " << Title(l.occupation, l.homeName) << " (generation " << l.generation + 1 << ", day " << (today >= l.born ? today - l.born + 1 : 1);
        if (l.retireDay)
            s << ", moves away in " << (l.retireDay > today ? l.retireDay - today : 0) << " days";
        s << "). Nature: " << DescribeTraits(t) << " (brave " << uint32(t.brave) << ", chatty " << uint32(t.chatty) << ", greedy "
          << uint32(t.greedy) << ", curious " << uint32(t.curious) << ", lawful " << uint32(t.lawful) << ", sociable " << uint32(t.sociable)
          << "; runs below " << uint32(FleeBelowPct(t)) << "% health, " << (StartsFights(t, l.occupation) ? "starts fights" : "only fights back")
          << ").";
        out.push_back(s.str());
    }
    {
        std::ostringstream s;
        s << "Needs: mood " << uint32(l.needs.mood) << ", fatigue " << uint32(l.needs.fatigue) << ", hunger " << uint32(l.needs.hunger)
          << ". Purse " << MoneyText(w.rec.money) << " (" << MoneyText(l.earned) << " earned at its trade).";
        out.push_back(s.str());
    }
    out.push_back("Haunts: home " + NodeName(w, l.home) + ", works at " + NodeName(w, l.work) + ", sells at " + NodeName(w, l.market) + ".");
    {
        std::ostringstream s;
        uint32 const now = WorldTimer::getMSTime();
        s << "Now (" << uint32(HourNow()) << " o'clock): " << ActivityName(l.doing);
        if (l.doingNode)
            s << " at " << NodeName(w, l.doingNode);
        if (w.activityOn)
            s << ", " << (std::min<uint32>(now - w.doingStartMs, w.doingMs) / 60000) << " of " << w.doingMs / 60000 << " min";
        else if (!w.rec.trip.Done())
            s << ", on its way";
        s << "; " << uint32(l.sessions) << " work sessions since market";
        if (l.parcel.Carried())
            s << "; a parcel for " << NodeName(w, l.parcel.to) << " (" << MoneyText(l.parcel.pay) << ", from " << l.parcel.sender << ")";
        if (w.hiredBy)
            s << "; hired, " << (w.hiredUntilMs - now) / 1000 << " s more";
        s << ".";
        out.push_back(s.str());
    }
    {
        // The day as its schedule sees it (its needs as they are now).
        DayContext c = DayFor(w);
        std::ostringstream s;
        s << "Schedule today:";
        for (uint8 h = 0; h < 24; h += 3)
        {
            c.hour = h;
            s << " " << uint32(h) << "h " << ActivityName(Scheduled(c, today * 24u + h));
            if (h < 21)
                s << ",";
        }
        s << (IsDayOff(w.rec.seed, today) ? " (a day off)." : ".");
        out.push_back(s.str());
    }
    {
        std::ostringstream s;
        s << "Pack (" << PackItems(l.pack) << "):";
        if (l.pack.empty())
            s << " empty";
        for (size_t i = 0; i < l.pack.size(); ++i)
        {
            Stock const& k = l.pack[i];
            s << (i ? "," : "") << " " << k.count << " x " << ItemName(k.item) << (k.rare ? " (rare)" : "") << " at "
              << MoneyText(StallPrice(PriceOf(k.item), t, k.rare, 0));
        }
        out.push_back(s.str() + ".");
    }
    {
        std::ostringstream s;
        s << "Ties:";
        if (l.ties.empty())
            s << " none";
        for (size_t i = 0; i < l.ties.size(); ++i)
        {
            FusionWanderer const* o = ById(l.ties[i].other);
            s << (i ? "," : "") << " #" << l.ties[i].other << " " << (o ? o->rec.name : std::string("?")) << " ("
              << TieName(l.ties[i].affinity) << " " << int(l.ties[i].affinity) << ", met " << l.ties[i].met << ")";
        }
        out.push_back(s.str() + ".");
    }
    if (!l.news.empty())
    {
        Talk talk = TalkOf(w, std::string(), nullptr);
        std::ostringstream s;
        s << "News:";
        for (size_t i = 0; i < l.news.size(); ++i)
        {
            talk.news = l.news[i];
            s << (i ? " /" : "") << " " << NewsLine(talk, w.rec.seed) << " (day " << l.news[i].day << ")";
        }
        out.push_back(s.str());
    }
    out.push_back("Backstory: " + l.backstory);
    (void)r;
    return out;
}
