/*
 * World of Modern Warfare: the wanderers. GPL-2.0-or-later. See FusionWanderers.h.
 */

#include "FusionWanderers.h"
#include "FusionBounties.h"
#include "FusionMgr.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Config/Config.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "GridMap.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "MoveSpline.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"
#include "fusion/ActionSystems.h"
#include "fusion/NpcGuns.h"
#include "fusion/Scale.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <set>
#include <sstream>

using namespace Fusion::Wanderers;
using Fusion::Vec3;

namespace
{
    // The id of a wanderer's own point moves (MovementInform is not listened to).
    constexpr uint32 kWandererPointId = 0x57A0;
    // A corpse lies this long before its wanderer is sent out of the world (it comes back at a
    // town after Wanderers.h kRespawnMs).
    constexpr uint32 kCorpseMs = 30000;
    // A GM's `.wanderer despawn` keeps it out this long.
    constexpr uint32 kHiddenMs = 5 * 60 * 1000;
    // How far it looks about it for monsters, players and attackers.
    constexpr float kLookRadius = 45.f;
    // It shoots at what it sees within this.
    constexpr float kShootRange = 60.f;
    // An auto-found town (an innkeeper, a flight master) this near an authored or board node is
    // the same place.
    constexpr float kSamePlace = 40.f;
    // A region's nodes must stand within its box; auto-found towns within this of it.
    constexpr float kBoxMargin = 600.f;
    // The unseen hunt: one of its bounty's creatures every so often.
    constexpr uint32 kUnseenKillMs = 45000;

    Vec3 V(float x, float y, float z) { return {x, y, z}; }
    Vec3 PosOf(WorldObject const* o) { return V(o->GetPositionX(), o->GetPositionY(), o->GetPositionZ()); }
    float Dist2d(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }
    bool Due(uint32 now, uint32 at) { return int32(now - at) >= 0; }

    char const* RaceName(uint8 race)
    {
        switch (race)
        {
            case kHuman: return "human";
            case kOrc: return "orc";
            case kDwarf: return "dwarf";
            case kNightElf: return "night elf";
            case kUndead: return "forsaken";
            case kTauren: return "tauren";
            case kGnome: return "gnome";
            case kTroll: return "troll";
            default: return "?";
        }
    }

    std::string AreaName(uint32 id)
    {
        AreaEntry const* a = id ? AreaEntry::GetById(id) : nullptr;
        return a && a->Name ? std::string(a->Name) : std::to_string(id);
    }

    // Names NPC looks carry that are no wanderer's: rulers, leaders, guards, ghosts.
    bool UnfitLook(std::string const& name)
    {
        static char const* const words[] = {"King", "Queen", "Prince", "Lord", "Lady", "Highlord", "Warchief", "Thrall",
            "Bolvar", "Magni", "Jaina", "Sylvanas", "Cairne", "Vol'jin", "Tyrande", "Fandral", "Archmage", "Guard", "Grunt",
            "Sentinel", "Watcher", "Deathguard", "Brave", "Peacekeeper", "Ghost", "Spirit", "Image", "Illusion",
            "Apparition", "Disguise", "Invisible", "Trigger", "(", "["};
        for (char const* w : words)
            if (name.find(w) != std::string::npos)
                return true;
        return false;
    }

    // A tidy random offset about a point (a loiter spot).
    Vec3 Near(Vec3 p, float radius)
    {
        float const a = frand(0.f, 2.f * float(M_PI));
        float const r = frand(radius * 0.3f, radius);
        return V(p.x + std::cos(a) * r, p.y + std::sin(a) * r, p.z);
    }
}

WandererMgr& WandererMgr::Instance()
{
    static WandererMgr instance;
    return instance;
}

// ── Startup ─────────────────────────────────────────────────────────────────────────────────────

void WandererMgr::Load()
{
    m_enabled = sConfig.GetBoolDefault("Wanderers.Enable", false);
    if (!m_enabled)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Wanderers: off (Wanderers.Enable).");
        return;
    }
    if (!sFusionMgr.IsEnabled())
    {
        m_enabled = false;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Wanderers: off (they need the fusion rules, Fusion.Enable).");
        return;
    }
    m_perZone = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.PerZone", 3), 0, int(kMaxRoster)));
    m_maxSpawned = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.MaxSpawned", 24), 0, int(kMaxRoster)));
    m_perPlayer = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.PerPlayer", 8), 1, int(kMaxRoster)));
    m_spawnRadius = std::clamp(sConfig.GetFloatDefault("Wanderers.SpawnRadius", 200.f), 50.f, 500.f);
    m_despawnRadius = std::clamp(sConfig.GetFloatDefault("Wanderers.DespawnRadius", 300.f), m_spawnRadius + 50.f, 700.f);
    m_unseenPerTick = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.UnseenPerTick", 25), 1, int(kMaxRoster)));
    m_saveEveryMs = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.SaveSeconds", 60), 10, 3600)) * 1000;
    m_chat = sConfig.GetBoolDefault("Wanderers.Chat", true);
    m_bounties = sConfig.GetBoolDefault("Wanderers.Bounties", true);
    m_lives = sConfig.GetBoolDefault("Wanderers.Lives", true);
    m_trade = sConfig.GetBoolDefault("Wanderers.Trade", true);
    m_hire = sConfig.GetBoolDefault("Wanderers.Hire", true);
    m_lifeDays = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.LifeDays", 60), 0, 3650));
    m_linesPerMinute = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.LinesPerMinute", 20), 0, 600));
    m_lineGapMs = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.LineGapSeconds", 40), 5, 3600)) * 1000;
    m_spotsPerZone = uint32(std::clamp(sConfig.GetIntDefault("Wanderers.WorkSpotsPerZone", 3), 0, 12));
    m_zoneFilter.clear();
    {
        std::istringstream in(sConfig.GetStringDefault("Wanderers.Zones", "12 40 44 10 1519 14 17 1637 215 1638 85 130 1497"));
        uint32 z;
        while (in >> z)
            m_zoneFilter.push_back(z);
    }

    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `aw_wanderer` ("
        "`id` int(10) unsigned NOT NULL,"
        "`name` varchar(16) NOT NULL,"
        "`race` tinyint(3) unsigned NOT NULL,"
        "`gender` tinyint(3) unsigned NOT NULL,"
        "`archetype` tinyint(3) unsigned NOT NULL,"
        "`side` tinyint(3) unsigned NOT NULL,"
        "`home_zone` int(10) unsigned NOT NULL,"
        "`level` tinyint(3) unsigned NOT NULL,"
        "`xp` int(10) unsigned NOT NULL DEFAULT '0',"
        "`money` int(10) unsigned NOT NULL DEFAULT '0',"
        "`kills` int(10) unsigned NOT NULL DEFAULT '0',"
        "`deaths` int(10) unsigned NOT NULL DEFAULT '0',"
        "`bounties` int(10) unsigned NOT NULL DEFAULT '0',"
        "`seed` int(10) unsigned NOT NULL,"
        "`display` int(10) unsigned NOT NULL DEFAULT '0',"
        "`state` varchar(1024) NOT NULL DEFAULT '',"
        "`saved_at` int(10) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`id`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW wanderers: who each one is and what it is doing'");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `aw_wanderer_bond` ("
        "`wanderer` int(10) unsigned NOT NULL,"
        "`player` int(10) unsigned NOT NULL,"
        "`points` int(11) NOT NULL DEFAULT '0',"
        "`greets` int(10) unsigned NOT NULL DEFAULT '0',"
        "`last_met` int(10) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`wanderer`, `player`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW wanderers: how well each one knows a player'");
    // Phase 2's columns on a Phase 1 table (MySQL has no ADD COLUMN IF NOT EXISTS).
    auto addColumn = [](char const* table, char const* column, char const* def) {
        std::unique_ptr<QueryResult> has = CharacterDatabase.PQuery(
            "SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '%s' AND COLUMN_NAME = '%s'",
            table, column);
        if (!has)
            CharacterDatabase.DirectPExecute("ALTER TABLE `%s` ADD COLUMN `%s` %s", table, column, def);
    };
    addColumn("aw_wanderer", "life", "TEXT NULL");
    addColumn("aw_wanderer_bond", "helped", "int(10) unsigned NOT NULL DEFAULT '0'");
    addColumn("aw_wanderer_bond", "harmed", "int(10) unsigned NOT NULL DEFAULT '0'");
    addColumn("aw_wanderer_bond", "traded", "int(10) unsigned NOT NULL DEFAULT '0'");
    // A deleted character's bonds go with it.
    CharacterDatabase.DirectExecute(
        "DELETE b FROM `aw_wanderer_bond` b LEFT JOIN `characters` c ON c.`guid` = b.`player` WHERE c.`guid` IS NULL");

    LoadZones();
    if (m_regions.empty())
    {
        m_enabled = false;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Wanderers: off (no zones: aw_wanderer_zone and Wanderers.Zones).");
        return;
    }
    // One chat budget a map, made now (the map threads only look theirs up).
    for (Region const& r : m_regions)
        m_budget[r.map];
    LoadNodes();
    IndexSpawns();
    BuildLooks();
    LoadRoster();
    LoadBonds();
    SaveAll(true);

    size_t nodes = 0, roads = 0, looks = 0;
    for (Region const& r : m_regions)
    {
        nodes += r.net.nodes.size();
        roads += r.net.LinkCount();
    }
    for (auto const& g : m_looks)
        for (auto const& kv : g)
            looks += kv.second.size();
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        "Wanderers: %u in %u regions (%u a zone), %u road nodes and %u roads, %u looks; at most %u in the world "
        "(%u a player), in within %.0f yd of a player and out past %.0f.",
        uint32(m_roster.size()), uint32(m_regions.size()), m_perZone, uint32(nodes), uint32(roads), uint32(looks), m_maxSpawned,
        m_perPlayer, m_spawnRadius, m_despawnRadius);
}

void WandererMgr::LoadZones()
{
    m_regions.clear();
    std::unique_ptr<QueryResult> result = WorldDatabase.Query(
        "SELECT `zone`, `map`, `side`, `min_level`, `max_level` FROM `aw_wanderer_zone` ORDER BY `zone`");
    if (!result)
    {
        sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Wanderers: no aw_wanderer_zone table or rows (28_wanderers.sql).");
        return;
    }
    do
    {
        Field* f = result->Fetch();
        uint32 const zone = f[0].GetUInt32();
        uint32 const map = f[1].GetUInt32();
        uint32 const side = f[2].GetUInt32();
        if (!m_zoneFilter.empty() && std::find(m_zoneFilter.begin(), m_zoneFilter.end(), zone) == m_zoneFilter.end())
            continue;
        if (side != uint32(Side::Alliance) && side != uint32(Side::Horde))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "aw_wanderer_zone: zone %u has side %u (1 Alliance, 2 Horde): left out.", zone, side);
            continue;
        }
        ZoneBand b;
        b.zone = zone;
        b.minLevel = uint8(std::clamp<uint32>(f[3].GetUInt32(), 1, 60));
        b.maxLevel = uint8(std::clamp<uint32>(f[4].GetUInt32(), b.minLevel, 60));
        // A region: the zones of one map and one side.
        Region* r = nullptr;
        for (Region& g : m_regions)
            if (g.map == map && g.side == Side(side))
                r = &g;
        if (!r)
        {
            m_regions.emplace_back();
            r = &m_regions.back();
            r->map = map;
            r->side = Side(side);
        }
        r->zones.push_back(b);
        r->zoneNames.push_back(AreaName(zone));
    } while (result->NextRow());
    for (Region& r : m_regions)
    {
        std::string n;
        for (std::string const& z : r.zoneNames)
            n += (n.empty() ? "" : " and ") + z;
        r.name = n;
    }
}

void WandererMgr::LoadNodes()
{
    struct Raw
    {
        Node node;
        uint32 anchor = 0;
        float dx = 0.f, dy = 0.f;
    };
    auto regionOfZone = [this](uint32 zone) -> Region* {
        for (Region& r : m_regions)
            for (ZoneBand const& b : r.zones)
                if (b.zone == zone)
                    return &r;
        return nullptr;
    };

    std::vector<Raw> raws;
    std::set<uint32> anchors;
    if (std::unique_ptr<QueryResult> result = WorldDatabase.Query(
            "SELECT `id`, `zone`, `kind`, `name`, `anchor`, `dx`, `dy`, `x`, `y`, `z` FROM `aw_wanderer_node`"))
    {
        do
        {
            Field* f = result->Fetch();
            Raw r;
            r.node.id = f[0].GetUInt32();
            r.node.zone = f[1].GetUInt32();
            r.node.kind = NodeKind(std::min<uint32>(f[2].GetUInt32(), uint32(NodeKind::Count) - 1));
            r.node.name = f[3].GetCppString();
            r.anchor = f[4].GetUInt32();
            r.dx = f[5].GetFloat();
            r.dy = f[6].GetFloat();
            r.node.pos = V(f[7].GetFloat(), f[8].GetFloat(), f[9].GetFloat());
            if (r.node.id == 0 || r.node.id >= 10000)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "aw_wanderer_node: node %u left out (ids 1-9999).", r.node.id);
                continue;
            }
            if (!regionOfZone(r.node.zone))
                continue;
            if (r.anchor)
                anchors.insert(r.anchor);
            raws.push_back(r);
        } while (result->NextRow());
    }
    m_roads.clear();
    if (std::unique_ptr<QueryResult> result = WorldDatabase.Query("SELECT `a`, `b` FROM `aw_wanderer_road`"))
    {
        do
        {
            Field* f = result->Fetch();
            m_roads.push_back({f[0].GetUInt32(), f[1].GetUInt32()});
        } while (result->NextRow());
    }

    // The anchors' first spawns, and every innkeeper and flight master on the regions' maps.
    std::set<uint32> maps;
    for (Region const& r : m_regions)
        maps.insert(r.map);
    std::map<uint32, std::pair<uint32, WorldLocation>> firstSpawn;      // entry -> (guid, where)
    struct Keeper
    {
        uint32 entry;
        WorldLocation at;
        bool inn;
    };
    std::vector<Keeper> keepers;
    auto gather = [&](CreatureDataPair const& pair) {
        CreatureData const& d = pair.second;
        for (uint32 id : d.creature_id)
        {
            if (!id)
                continue;
            if (anchors.count(id))
            {
                auto it = firstSpawn.find(id);
                if (it == firstSpawn.end() || pair.first < it->second.first)
                    firstSpawn[id] = {pair.first, d.position};
            }
            if (maps.count(d.position.mapId))
                if (CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(id))
                    if (ci->npc_flags & (UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_FLIGHTMASTER))
                        keepers.push_back({id, d.position, (ci->npc_flags & UNIT_NPC_FLAG_INNKEEPER) != 0});
        }
        return false;
    };
    sObjectMgr.DoCreatureData(gather);

    std::map<Region*, std::vector<Node>> nodes;
    for (Raw& r : raws)
    {
        Region* g = regionOfZone(r.node.zone);
        if (r.anchor)
        {
            auto it = firstSpawn.find(r.anchor);
            if (it == firstSpawn.end() || it->second.second.mapId != g->map)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "aw_wanderer_node: node %u (%s): anchor %u is not spawned on map %u.",
                    r.node.id, r.node.name.c_str(), r.anchor, g->map);
                continue;
            }
            WorldLocation const& at = it->second.second;
            r.node.pos = V(at.x + r.dx, at.y + r.dy, at.z);
        }
        // It must stand in its zone: a wrong anchor never puts a town somewhere strange.
        uint32 const in = sTerrainMgr.GetZoneId(g->map, r.node.pos.x, r.node.pos.y, r.node.pos.z);
        if (in != r.node.zone)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "aw_wanderer_node: node %u (%s) stands in zone %u, not %u: left out.",
                r.node.id, r.node.name.c_str(), in, r.node.zone);
            continue;
        }
        nodes[g].push_back(r.node);
    }

    // The bounty boards of the regions' zones.
    if (std::unique_ptr<QueryResult> result = WorldDatabase.Query("SELECT `guid`, `zone`, `town` FROM `aw_bounty_board`"))
    {
        do
        {
            Field* f = result->Fetch();
            uint32 const guid = f[0].GetUInt32();
            Region* g = regionOfZone(f[1].GetUInt32());
            GameObjectData const* d = g ? sObjectMgr.GetGOData(guid) : nullptr;
            if (!d || d->position.mapId != g->map)
                continue;
            Node n;
            n.id = 10000 + guid % 10000;
            n.zone = f[1].GetUInt32();
            n.kind = NodeKind::Board;
            n.pos = V(d->position.x, d->position.y, d->position.z);
            n.name = f[2].GetCppString() + " bounty board";
            nodes[g].push_back(n);
        } while (result->NextRow());
    }

    for (Region& g : m_regions)
    {
        std::vector<Node>& list = nodes[&g];
        if (list.empty())
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Wanderers: %s has no road nodes (aw_wanderer_node): nobody walks there.",
                g.name.c_str());
            continue;
        }
        g.minX = g.maxX = list.front().pos.x;
        g.minY = g.maxY = list.front().pos.y;
        for (Node const& n : list)
        {
            g.minX = std::min(g.minX, n.pos.x);
            g.maxX = std::max(g.maxX, n.pos.x);
            g.minY = std::min(g.minY, n.pos.y);
            g.maxY = std::max(g.maxY, n.pos.y);
        }
        // The towns the world itself has there: its innkeepers and flight masters, friendly to
        // the side (a neutral town counts), in the region's zones.
        FactionTemplateEntry const* mine = sObjectMgr.GetFactionTemplateEntry(RaceFaction(g.side == Side::Alliance ? kHuman : kOrc));
        uint32 autoId = 20000 + uint32(&g - &m_regions[0]) * 1000;
        for (Keeper const& k : keepers)
        {
            if (k.at.mapId != g.map || k.at.x < g.minX - kBoxMargin || k.at.x > g.maxX + kBoxMargin
                || k.at.y < g.minY - kBoxMargin || k.at.y > g.maxY + kBoxMargin)
                continue;
            CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(k.entry);
            FactionTemplateEntry const* theirs = ci ? sObjectMgr.GetFactionTemplateEntry(ci->faction) : nullptr;
            if (!theirs || !mine || theirs->IsHostileTo(*mine))
                continue;
            Vec3 const p = V(k.at.x, k.at.y, k.at.z);
            bool same = false;
            for (Node const& n : list)
                same = same || Dist2d(n.pos, p) < kSamePlace;
            if (same)
                continue;
            uint32 const zone = sTerrainMgr.GetZoneId(g.map, p.x, p.y, p.z);
            bool inRegion = false;
            for (ZoneBand const& b : g.zones)
                inRegion = inRegion || b.zone == zone;
            if (!inRegion)
                continue;
            Node n;
            n.id = autoId++;
            n.zone = zone;
            n.kind = k.inn ? NodeKind::Inn : NodeKind::Town;
            n.pos = p;
            n.name = ci->name;
            list.push_back(n);
        }
        // Phase 2: the workplaces the world has (veins, herbs, game, fishing folk), its markets.
        std::map<uint32, Spot> spots;
        if (m_lives)
            DiscoverPlaces(g, list, uint32(&g - &m_regions[0]), spots);
        g.net = BuildRoads(list, m_roads);
        g.boards.clear();
        g.boardZone.clear();
        for (size_t i = 0; i < g.net.nodes.size(); ++i)
            if (g.net.nodes[i].kind == NodeKind::Board)
            {
                g.boards.push_back(int(i));
                g.boardZone[int(i)] = g.net.nodes[i].zone;
            }
        IndexPlaces(g);
        if (m_lives)
            LoadYields(g, spots);
        g.minX = g.maxX = g.net.nodes.front().pos.x;
        g.minY = g.maxY = g.net.nodes.front().pos.y;
        for (Node const& n : g.net.nodes)
        {
            g.minX = std::min(g.minX, n.pos.x);
            g.maxX = std::max(g.maxX, n.pos.x);
            g.minY = std::min(g.minY, n.pos.y);
            g.maxY = std::max(g.maxY, n.pos.y);
        }
    }
}

void WandererMgr::IndexSpawns()
{
    // Where each creature of the regions lives: a bounty's hunting ground is the spawn of its
    // creature nearest the board.
    m_spawnsOf.clear();
    auto gather = [&](CreatureDataPair const& pair) {
        CreatureData const& d = pair.second;
        for (Region const& r : m_regions)
        {
            if (d.position.mapId != r.map || r.net.nodes.empty() || d.position.x < r.minX - kBoxMargin
                || d.position.x > r.maxX + kBoxMargin || d.position.y < r.minY - kBoxMargin || d.position.y > r.maxY + kBoxMargin)
                continue;
            for (uint32 id : d.creature_id)
                if (id)
                    m_spawnsOf[id].push_back(V(d.position.x, d.position.y, d.position.z));
        }
        return false;
    };
    sObjectMgr.DoCreatureData(gather);
}

void WandererMgr::BuildLooks()
{
    // A wanderer looks like a person of its race: the dressed NPC models (a display with its own
    // race, gender and gear, CreatureDisplayInfoExtra) of ordinary humanoid NPCs friendly to its
    // side, rulers, guards and ghosts left out.
    std::set<uint32> seen[2][9];
    for (auto const& kv : sObjectMgr.GetCreatureInfoMap())
    {
        CreatureInfo const* ci = kv.second.get();
        if (!ci || ci->entry >= 90000 || ci->type != CREATURE_TYPE_HUMANOID || ci->rank != 0
            || (ci->flags_extra & (CREATURE_FLAG_EXTRA_GUARD | CREATURE_FLAG_EXTRA_INVISIBLE)) || UnfitLook(ci->name))
            continue;
        FactionTemplateEntry const* theirs = sObjectMgr.GetFactionTemplateEntry(ci->faction);
        for (uint32 display : ci->display_id)
        {
            CreatureDisplayInfoEntry const* d = display ? sCreatureDisplayInfoStore.LookupEntry(display) : nullptr;
            CreatureDisplayInfoExtraEntry const* x = d && d->ExtendedDisplayInfoID
                ? sCreatureDisplayInfoExtraStore.LookupEntry(d->ExtendedDisplayInfoID) : nullptr;
            if (!x || x->Race > 8 || x->Gender > 1 || !SideOfRace(uint8(x->Race)))
                continue;
            FactionTemplateEntry const* mine = sObjectMgr.GetFactionTemplateEntry(RaceFaction(uint8(x->Race)));
            if (!theirs || !mine || theirs->IsHostileTo(*mine))
                continue;
            seen[x->Gender][x->Race].insert(display);
        }
    }
    for (uint8 g = 0; g < 2; ++g)
    {
        m_looks[g].clear();
        for (uint8 r = 1; r <= 8; ++r)
            if (!seen[g][r].empty())
                m_looks[g][r] = std::vector<uint32>(seen[g][r].begin(), seen[g][r].end());
    }
}

uint32 WandererMgr::RaceFaction(uint8 race) const
{
    ChrRacesEntry const* r = sChrRacesStore.LookupEntry(race);
    return r ? r->FactionID : 35;
}

uint32 WandererMgr::PickDisplay(uint8 race, uint8& gender, uint32 seed) const
{
    for (uint8 tries = 0; tries < 2; ++tries)
    {
        auto it = m_looks[gender & 1].find(race);
        if (it != m_looks[gender & 1].end() && !it->second.empty())
            return it->second[Mix(seed ^ 0x9B05688Cu) % it->second.size()];
        gender ^= 1;
    }
    // None dressed: the race's own body.
    ChrRacesEntry const* r = sChrRacesStore.LookupEntry(race);
    return r ? (gender ? r->model_f : r->model_m) : 0;
}

ZoneBand WandererMgr::BandOf(FusionWanderer const& w) const
{
    Region const& r = m_regions[w.region];
    for (ZoneBand const& b : r.zones)
        if (b.zone == w.rec.homeZone)
            return b;
    return r.zones.empty() ? ZoneBand() : r.zones.front();
}

void WandererMgr::LoadRoster()
{
    m_roster.clear();
    std::map<uint32, Record> rows;
    std::map<uint32, std::string> states;
    std::map<uint32, std::string> lives;
    if (std::unique_ptr<QueryResult> result = CharacterDatabase.Query(
            "SELECT `id`, `name`, `race`, `gender`, `archetype`, `side`, `home_zone`, `level`, `xp`, `money`, `kills`, "
            "`deaths`, `bounties`, `seed`, `display`, `state`, IFNULL(`life`, '') FROM `aw_wanderer` ORDER BY `id`"))
    {
        do
        {
            Field* f = result->Fetch();
            Record r;
            r.id = f[0].GetUInt32();
            r.name = f[1].GetCppString();
            r.race = uint8(f[2].GetUInt32());
            r.gender = uint8(f[3].GetUInt32() & 1);
            r.archetype = Archetype(std::min<uint32>(f[4].GetUInt32(), uint32(Archetype::Count) - 1));
            r.side = f[5].GetUInt32() == uint32(Side::Horde) ? Side::Horde : Side::Alliance;
            r.homeZone = f[6].GetUInt32();
            r.level = uint8(std::clamp<uint32>(f[7].GetUInt32(), 1, 60));
            r.xp = f[8].GetUInt32();
            r.money = f[9].GetUInt32();
            r.kills = f[10].GetUInt32();
            r.deaths = f[11].GetUInt32();
            r.bounties = f[12].GetUInt32();
            r.seed = f[13].GetUInt32();
            r.display = f[14].GetUInt32();
            if (r.id == 0 || r.id > kMaxRoster)
                continue;
            states[r.id] = f[15].GetCppString();
            lives[r.id] = f[16].GetCppString();
            rows[r.id] = r;
        } while (result->NextRow());
    }

    std::set<uint32> used;
    for (auto const& kv : rows)
        used.insert(kv.first);
    std::set<std::string> names;
    for (auto const& kv : rows)
        names.insert(kv.second.name);
    uint32 const now = WorldTimer::getMSTime();
    uint32 made = 0, loaded = 0;

    for (uint32 ri = 0; ri < m_regions.size(); ++ri)
    {
        Region& region = m_regions[ri];
        if (region.net.nodes.empty())
            continue;
        for (ZoneBand const& band : region.zones)
        {
            uint32 have = 0;
            // Those already living here, the lowest ids first.
            for (auto& [id, r] : rows)
            {
                if (have >= m_perZone || r.homeZone != band.zone)
                    continue;
                if (SideOfRace(r.race) != region.side || !ValidName(r.name))
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Wanderers: #%u (%s) does not fit %s any more: left resting.", id,
                        r.name.c_str(), region.name.c_str());
                    continue;
                }
                auto w = std::make_shared<FusionWanderer>();
                w->rec = r;
                w->region = ri;
                Record& rec = w->rec;
                if (!DecodeState(states[id], rec))
                    rec.trip = Trip();
                // A trip through nodes the network no longer has starts afresh.
                for (uint32 node : rec.trip.route)
                    if (region.net.IndexOf(node) < 0)
                    {
                        rec.trip = Trip();
                        break;
                    }
                rec.level = std::min(rec.level, LevelCap(band));
                ++have;
                ++loaded;
                m_roster.push_back(w);
            }
            // And new ones for the rest.
            while (have < m_perZone && used.size() < kMaxRoster)
            {
                uint32 id = 1;
                while (used.count(id))
                    ++id;
                used.insert(id);
                Record r;
                r.id = id;
                r.seed = Seed(id);
                r.side = region.side;
                r.homeZone = band.zone;
                r.race = RaceFor(region.side, r.seed);
                r.gender = uint8(Mix(r.seed ^ 0x1F83D9ABu) & 1);
                // Its look first: a race and gender with no dressed model takes the other gender.
                r.display = PickDisplay(r.race, r.gender, r.seed);
                r.archetype = ArchetypeFor(r.seed);
                r.level = StartLevel(band, r.seed);
                // A name no wanderer and no character has.
                for (uint32 salt = 0; salt < 64; ++salt)
                {
                    std::string const n = MakeName(r.race, r.gender, Seed(id, salt));
                    if (!names.count(n) && sObjectMgr.GetPlayerGuidByName(n).IsEmpty())
                    {
                        r.name = n;
                        break;
                    }
                }
                if (r.name.empty())
                    r.name = MakeName(r.race, r.gender, Seed(id, 999));
                names.insert(r.name);
                auto w = std::make_shared<FusionWanderer>();
                w->rec = r;
                w->region = ri;
                w->dirty = true;
                ++have;
                ++made;
                m_roster.push_back(w);
            }
        }
    }

    // Phase 2: each one's life: read back, or new (a Phase 1 roamer takes up a trade too); one
    // whose span is over moves away and someone new takes its place (and its id).
    uint32 retired = 0;
    if (m_lives)
    {
        uint32 const today = Fusion::Wanderers::DayOf(int64(time(nullptr)));
        std::set<uint32> fresh;
        for (std::shared_ptr<FusionWanderer> const& wp : m_roster)
        {
            FusionWanderer& w = *wp;
            if (!DecodeLife(lives[w.rec.id], w.life))
            {
                AssignLife(w, true);
                fresh.insert(w.rec.id);
            }
            else if (!FixHaunts(w))
                AssignLife(w, false);
            if (m_lifeDays && Retires(w.life, today))
            {
                Reborn(w, names);
                fresh.insert(w.rec.id);
                ++retired;
            }
        }
        // Friends and rivals for the new, among their region's others.
        for (std::shared_ptr<FusionWanderer> const& wp : m_roster)
        {
            if (!fresh.count(wp->rec.id))
                continue;
            std::vector<uint32> others;
            for (std::shared_ptr<FusionWanderer> const& o : m_roster)
                if (o->region == wp->region && o.get() != wp.get())
                    others.push_back(o->rec.id);
            wp->life.ties = FirstTies(wp->rec.id, others, Seed(wp->rec.seed, 0x7135));
            // And they know it back.
            for (Tie const& t : wp->life.ties)
                if (FusionWanderer* o = ById(t.other))
                {
                    o->life.ties.erase(std::remove_if(o->life.ties.begin(), o->life.ties.end(),
                        [&](Tie const& x) { return x.other == wp->rec.id; }), o->life.ties.end());
                    if (o->life.ties.size() < kMaxTies)
                        o->life.ties.push_back({wp->rec.id, t.affinity, 0});
                    o->dirty = true;
                }
        }
    }

    for (std::shared_ptr<FusionWanderer> const& wp : m_roster)
    {
        FusionWanderer& w = *wp;
        Region const& region = m_regions[w.region];
        Record& rec = w.rec;
        // A look of its race (a model the data no longer has is picked again).
        if (!rec.display || !sCreatureDisplayInfoStore.LookupEntry(rec.display))
        {
            rec.display = PickDisplay(rec.race, rec.gender, rec.seed);
            w.dirty = true;
        }
        // No trip yet (or a broken one): it starts at one of its home zone's places.
        if (rec.trip.Points() == 0)
        {
            std::vector<int> places;
            for (size_t i = 0; i < region.net.nodes.size(); ++i)
                if (region.net.nodes[i].kind != NodeKind::Road && region.net.nodes[i].zone == rec.homeZone)
                    places.push_back(int(i));
            if (places.empty())
                for (size_t i = 0; i < region.net.nodes.size(); ++i)
                    places.push_back(int(i));
            int const at = places[Mix(rec.seed ^ 0x5BE0CD19u) % places.size()];
            rec.trip = Trip();
            rec.trip.route = {region.net.nodes[size_t(at)].id};
            rec.lastNode = rec.trip.route.front();
            w.dirty = true;
        }
        for (uint32 node : rec.recent)
            if (int const i = region.net.IndexOf(node); i >= 0)
                w.recentIdx.push_back(i);
        // What it was doing: its goal and its bounty (on the mind's clock again), or its death.
        w.mind.goal = rec.goal;
        w.mind.errand = rec.errand;
        if (w.mind.goal != Goal::Roam)
            w.mind.errand.untilMs = now + kErrandMs;
        w.mind.nextBountyMs = now + urand(0, kBountyCooldownMs);
        if (rec.respawnInMs)
        {
            Died(w.mind, now);
            w.mind.untilMs = now + std::min(rec.respawnInMs, kRespawnMs);
        }
        else if (!rec.trip.Done())
            w.mind.state = State::Travel;
        w.lastAdvanceMs = now;
        MakeTemplate(w);
    }
    std::sort(m_roster.begin(), m_roster.end(), [](auto const& a, auto const& b) { return a->rec.id < b->rec.id; });
    if (made || retired)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Wanderers: %u new to the roads, %u back on them, %u moved away (and were replaced).",
            made, loaded, retired);
}

void WandererMgr::LoadBonds()
{
    std::unique_ptr<QueryResult> result = CharacterDatabase.Query(
        "SELECT `wanderer`, `player`, `points`, `greets`, `last_met`, `helped`, `harmed`, `traded` FROM `aw_wanderer_bond`");
    if (!result)
        return;
    do
    {
        Field* f = result->Fetch();
        if (FusionWanderer* w = ById(f[0].GetUInt32()))
        {
            FusionWandererBond& b = w->bonds[f[1].GetUInt32()];
            b.points = f[2].GetInt32();
            b.greets = f[3].GetUInt32();
            b.lastMet = f[4].GetUInt32();
            b.helped = f[5].GetUInt32();
            b.harmed = f[6].GetUInt32();
            b.traded = f[7].GetUInt32();
        }
    } while (result->NextRow());
}

void WandererMgr::MakeTemplate(FusionWanderer& w)
{
    // Its own creature template (no row in creature_template): its name, its title, its look,
    // its race's own faction (friend and foe as a player of the race), a fighter's body and no
    // brain of WoW's (NullAI: this file thinks for it). No experience or loot for its killer.
    Record const& r = w.rec;
    w.entry = kFirstEntry + r.id;
    auto ci = std::make_unique<CreatureInfo>();
    ci->entry = w.entry;
    ci->name = r.name;
    // Its trade and home (Phase 2), or the Phase 1 roamer's fighting style.
    ci->subname = w.life.occupation != Occupation::None ? Title(w.life.occupation, w.life.homeName)
        : std::string("Wandering ") + ArchetypeTitle(r.archetype);
    // Talk to it, and buy from its pack (FusionWandererLives.cpp answers both).
    if (m_lives)
        ci->npc_flags = UNIT_NPC_FLAG_GOSSIP | (m_trade ? UNIT_NPC_FLAG_VENDOR : 0);
    ci->level_min = ci->level_max = r.level;
    ci->faction = RaceFaction(r.race);
    ci->display_id[0] = r.display;
    ci->display_scale[0] = 1.f;
    ci->display_probability[0] = 100;
    ci->display_total_probability = 100;
    ci->speed_walk = 1.f;
    ci->speed_run = 1.14286f;
    ci->type = CREATURE_TYPE_HUMANOID;
    ci->unit_class = CLASS_WARRIOR;
    ci->rank = 0;
    ci->xp_multiplier = 0.f;
    ci->damage_multiplier = 0.5f;
    ci->ai_name = "NullAI";
    ci->inhabit_type = INHABIT_GROUND | INHABIT_WATER;
    sObjectMgr.AddGeneratedCreatureTemplate(std::move(ci));
    std::string const gun = GunFor(r.archetype, r.level, r.seed);
    Fusion::WeaponProfile const* wp = sFusionMgr.Weapons().Find(gun);
    if (!wp)
        wp = sFusionMgr.Weapons().Find(std::string("m4_mp"));
    w.gun = wp ? wp->id : 0;
}

FusionWanderer* WandererMgr::ById(uint32 id) const
{
    for (auto const& w : m_roster)
        if (w->rec.id == id)
            return w.get();
    return nullptr;
}

uint32 WandererMgr::IdOf(Creature const* c)
{
    return c && c->m_wanderer ? c->m_wanderer->rec.id : 0;
}

// ── The world thread ────────────────────────────────────────────────────────────────────────────

void WandererMgr::Update(uint32 diff)
{
    if (!m_enabled)
        return;
    m_tickMs += diff;
    if (m_tickMs < 1000)
        return;
    m_tickMs = 0;
    uint32 const now = WorldTimer::getMSTime();

    // Where the players are, by map.
    std::map<uint32, std::vector<Vec3>> players;
    uint32 playerCount = 0;
    for (Region const& r : m_regions)
    {
        if (players.count(r.map))
            continue;
        std::vector<Vec3>& list = players[r.map];
        if (Map* map = sMapMgr.FindMap(r.map, 0))
            for (auto it = map->GetPlayers().begin(); it != map->GetPlayers().end(); ++it)
                if (Player* p = it->getSource(); p && p->IsInWorld())
                {
                    list.push_back(PosOf(p));
                    ++playerCount;
                }
    }

    std::vector<Presence> presence(m_roster.size());
    for (size_t i = 0; i < m_roster.size(); ++i)
    {
        FusionWanderer& w = *m_roster[i];
        Region const& r = m_regions[w.region];
        Map* map = sMapMgr.FindMap(r.map, 0);
        Creature* c = w.spawned && map ? map->GetCreature(w.guid) : nullptr;
        if (w.spawned && !c)
        {
            // Gone with its grid (or a GM's .npc delete): unseen again from where its trip is.
            w.spawned = false;
            w.guid.Clear();
            w.steps.clear();
            w.lastAdvanceMs = now;
        }
        if (c && w.mind.state == State::Dead && now - w.diedAtMs >= kCorpseMs)
        {
            SpawnOut(w);
            c = nullptr;
        }
        Vec3 const at = c ? PosOf(c) : TripPos(r.net, w.rec.trip);
        float nearest = 1e9f;
        for (Vec3 const& p : players[r.map])
            nearest = std::min(nearest, Dist2d(p, at));
        Presence& pr = presence[i];
        pr.nearestPlayer = nearest;
        pr.spawned = w.spawned;
        pr.busy = c && (c->IsInCombat() || w.mind.state == State::Fight || w.mind.state == State::Dead || w.hiredBy
            || !Due(now, w.talkUntilMs));
        pr.dead = !w.spawned && (w.mind.state == State::Dead || !Due(now, w.hiddenUntilMs));
    }
    std::vector<size_t> const wanted = ChoosePresent(presence, SpawnCap(m_maxSpawned, m_perPlayer, playerCount),
        m_spawnRadius, m_despawnRadius);
    std::vector<bool> want(m_roster.size(), false);
    for (size_t i : wanted)
        want[i] = true;
    for (size_t i = 0; i < m_roster.size(); ++i)
    {
        FusionWanderer& w = *m_roster[i];
        if (w.spawned && !want[i])
            SpawnOut(w);
        else if (!w.spawned && want[i])
            SpawnIn(w);
    }

    // Phase 2: what they noticed, their needs, their meetings on the road.
    if (m_lives)
    {
        TakeEvents();
        TickLives(now);
        MeetOnPaper(now);
    }

    // The unseen walk on, a few a tick.
    std::vector<size_t> unseen;
    for (size_t i = 0; i < m_roster.size(); ++i)
        if (!m_roster[i]->spawned)
            unseen.push_back(i);
    for (size_t k : TakeTurns(unseen.size(), m_unseenPerTick, m_unseenCursor))
        AdvanceUnseen(*m_roster[unseen[k]], now);

    m_saveMs += 1000;
    if (m_saveMs >= m_saveEveryMs)
    {
        m_saveMs = 0;
        SaveAll(false);
    }
}

void WandererMgr::SpawnIn(FusionWanderer& w, Vec3 const* at)
{
    Region const& r = m_regions[w.region];
    Map* map = sMapMgr.FindMap(r.map, 0);
    if (!map || r.net.nodes.empty())
        return;
    Vec3 p = at ? *at : TripPos(r.net, w.rec.trip);
    float const ground = map->GetHeight(p.x, p.y, p.z + 20.f, true, 60.f);
    if (ground > INVALID_HEIGHT)
        p.z = ground;
    // Facing on along its way.
    Vec3 const next = w.rec.trip.Done() ? p : TripPoint(r.net, w.rec.trip, w.rec.trip.leg + 1);
    float const o = std::atan2(next.y - p.y, next.x - p.x);
    Creature* c = map->SummonCreature(w.entry, p.x, p.y, p.z, o, TEMPSUMMON_MANUAL_DESPAWN, 0, false);
    if (!c)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Wanderers: #%u (%s) could not be summoned at %.0f %.0f %.0f on map %u.",
            w.rec.id, w.rec.name.c_str(), p.x, p.y, p.z, r.map);
        w.hiddenUntilMs = WorldTimer::getMSTime() + 60000;
        return;
    }
    uint32 const now = WorldTimer::getMSTime();
    uint8 const level = w.rec.level;
    c->m_wanderer = w.shared_from_this();
    c->SetLevel(level);
    // A fresh player's pool of its level on the CoD scale, and WoW's own for a player of it, so
    // a monster's blow costs it what it would cost a player (FusionMgr::HealScale).
    uint32 const maxHp = std::max<uint32>(1, uint32(Fusion::Scale::PlayerMaxHealth(level, 0.f) + 0.5f));
    c->m_fusionWowMaxHealth = std::max<uint32>(1, uint32(sFusionMgr.Par().MaxHealth(level) + 0.5f));
    c->SetMaxHealth(maxHp);
    c->SetHealth(std::max<uint32>(1, uint32(float(maxHp) * std::clamp(w.healthPct, 1.f, 100.f) / 100.f)));
    // One of the gunners as far as the client is told (the gun in its hands, op 33), but its own
    // trigger (UpdateCreatureGun leaves wanderers alone).
    c->m_fusionGunRole = uint8(Fusion::GunRole::Gunner);
    c->m_fusionGunId = w.gun;
    c->SetVirtualItem(RANGED_ATTACK, Fusion::NpcGunItemFor(level));
    c->SetSheath(SHEATH_STATE_RANGED);
    c->SetWalk(true);
    w.guid = c->GetObjectGuid();
    w.mapId = r.map;
    w.spawned = true;
    w.steps.clear();
    w.moving = false;
    w.stuck = 0;
    w.attacker = w.bestTarget = w.playerNear = w.killed = w.helpFor = 0;
    w.nextThinkMs = now + FirstThinkMs(w.rec.id);
    w.nextLookMs = now;
    w.nextPoseMs = now;
    w.lastPose = 0xFFFF;
    // It comes in on its way, whatever it was doing on paper.
    if (w.mind.state != State::Dead)
    {
        w.mind.state = w.rec.trip.Done() ? State::Idle : State::Travel;
        w.mind.target = w.mind.corpse = w.mind.greet = 0;
        w.mind.kneeling = false;
    }
    // Or, at its life's business (fishing, asleep at the inn), still at it as you come up.
    if (m_lives && w.activityOn && w.mind.state != State::Dead && w.rec.trip.Done())
    {
        w.mind.state = State::Rest;
        w.mind.untilMs = std::max(now + 5000, w.doingStartMs + w.doingMs);
        ActivityPose(c, w);
    }
}

void WandererMgr::SpawnOut(FusionWanderer& w)
{
    Region const& r = m_regions[w.region];
    Map* map = sMapMgr.FindMap(r.map, 0);
    if (Creature* c = map && w.spawned ? map->GetCreature(w.guid) : nullptr)
    {
        // Its trip goes on from where it stands: as far along its leg as it got.
        Trip& t = w.rec.trip;
        if (!t.Done())
        {
            Vec3 const a = TripPoint(r.net, t, t.leg);
            Vec3 const b = TripPoint(r.net, t, t.leg + 1);
            Vec3 const ab = b - a;
            float const len = ab.Length();
            t.along = len > 0.1f ? std::clamp((PosOf(c) - a).Dot(ab) / len, 0.f, len) : 0.f;
        }
        w.healthPct = c->IsAlive() ? c->GetHealthPercent() : 100.f;
        c->m_wanderer.reset();
        if (c->IsTemporarySummon())
            static_cast<TemporarySummon*>(c)->UnSummon();
        else
            c->AddObjectToRemoveList();
    }
    w.spawned = false;
    w.guid.Clear();
    w.steps.clear();
    w.moving = false;
    w.lastAdvanceMs = WorldTimer::getMSTime();
    w.chatWith = 0;
    w.following = false;
    // Its business goes on on paper for the time it has left.
    if (w.activityOn)
        w.unseenUntilMs = w.doingStartMs + w.doingMs;
    if (w.mind.state != State::Dead)
    {
        w.mind.state = w.rec.trip.Done() ? State::Idle : State::Travel;
        w.mind.target = w.mind.corpse = w.mind.greet = 0;
        w.mind.kneeling = false;
    }
    w.dirty = true;
}

void WandererMgr::AdvanceUnseen(FusionWanderer& w, uint32 now)
{
    Region const& r = m_regions[w.region];
    uint32 const elapsed = std::min<uint32>(now - w.lastAdvanceMs, 120000);
    w.lastAdvanceMs = now;
    if (r.net.nodes.empty())
        return;
    if (w.mind.state == State::Dead)
    {
        if (!Due(now, w.mind.untilMs))
            return;
        // Back on its feet at the town nearest where it fell.
        int const town = NearestNode(r.net, TripPos(r.net, w.rec.trip), true);
        if (town >= 0)
        {
            w.rec.trip = Trip();
            w.rec.trip.route = {r.net.nodes[size_t(town)].id};
            w.rec.lastNode = w.rec.trip.route.front();
        }
        Revived(w.mind);
        w.rec.respawnInMs = 0;
        w.healthPct = 100.f;
        w.dirty = true;
        return;
    }
    if (!Due(now, w.unseenUntilMs))
        return;
    Trip& t = w.rec.trip;
    if (!t.Done())
    {
        if (Advance(r.net, t, kWalkSpeed * float(elapsed) / 1000.f))
            ArriveAt(w);
        w.dirty = true;
        return;
    }
    // Arrived: what it came for, on paper.
    uint32 const roll = urand(0, 0x7FFFFFFF);
    Vec3 const here = TripPos(r.net, t);
    // Phase 2: a life's plan: do what it came to do, and when it is done, the next plan.
    if (m_lives && w.mind.goal == Goal::Roam && w.life.occupation != Occupation::None)
    {
        if (w.activityOn)
            EndActivity(w, now, nullptr);
        else if (w.life.doing != Activity::None && w.rec.lastNode == w.life.doingNode)
        {
            w.unseenUntilMs = now + StartActivity(w, now, nullptr);
            return;
        }
        PlanNext(w, here, roll);
        return;
    }
    int const boardIdx = r.net.IndexOf(w.mind.errand.boardNode);
    bool const atBoard = boardIdx >= 0 && Dist2d(here, r.net.nodes[size_t(boardIdx)].pos) < 15.f;
    switch (w.mind.goal)
    {
        case Goal::TakeBounty:
            if (atBoard)
                TakeErrand(w, now, roll);
            else
                w.mind.goal = Goal::Roam;
            break;
        case Goal::TurnIn:
            if (atBoard)
                TurnInErrand(w, roll);
            break;
        case Goal::Hunt:
            if (w.mind.errand.Complete() || Due(now, w.mind.errand.untilMs))
                w.mind.goal = w.mind.errand.Complete() ? Goal::TurnIn : Goal::Roam;
            else if (w.mind.errand.hasGround && Dist2d(here, w.mind.errand.ground) < 40.f)
            {
                // At its hunting ground: a kill now and then.
                ++w.mind.errand.done;
                CreditKill(w, 0, w.rec.level);
                w.unseenUntilMs = now + kUnseenKillMs;
                w.dirty = true;
                return;
            }
            break;
        case Goal::Roam:
        {
            if (BoardHasBounty(w) && Due(now, w.mind.nextBountyMs) && roll % 100 < kBountyChancePercent)
            {
                w.mind.goal = Goal::TakeBounty;
                w.mind.nextBountyMs = now + kBountyCooldownMs;
            }
            else
            {
                int const last = r.net.IndexOf(w.rec.lastNode);
                NodeKind const kind = last >= 0 ? r.net.nodes[size_t(last)].kind : NodeKind::Town;
                if (kind != NodeKind::Road)
                    w.unseenUntilMs = now + RestMsAt(kind, roll);
            }
            break;
        }
    }
    PlanNext(w, here, roll);
}

void WandererMgr::Save(FusionWanderer& w)
{
    Record& r = w.rec;
    r.goal = w.mind.goal;
    r.errand = w.mind.errand;
    r.recent.clear();
    Region const& region = m_regions[w.region];
    for (int i : w.recentIdx)
        if (i >= 0 && size_t(i) < region.net.nodes.size())
            r.recent.push_back(region.net.nodes[size_t(i)].id);
    // Dead: back this long after a restart (at least a moment, so it still comes back at a town).
    r.respawnInMs = 0;
    if (w.mind.state == State::Dead)
    {
        int32 const left = int32(w.mind.untilMs - WorldTimer::getMSTime());
        r.respawnInMs = left > 0 ? uint32(left) : 1;
    }
    std::string name = r.name;
    std::string state = EncodeState(r);
    std::string life = m_lives ? EncodeLife(w.life) : std::string();
    CharacterDatabase.escape_string(name);
    CharacterDatabase.escape_string(state);
    CharacterDatabase.escape_string(life);
    CharacterDatabase.PExecute(
        "REPLACE INTO `aw_wanderer` (`id`, `name`, `race`, `gender`, `archetype`, `side`, `home_zone`, `level`, `xp`, `money`, "
        "`kills`, `deaths`, `bounties`, `seed`, `display`, `state`, `saved_at`, `life`) VALUES "
        "(%u, '%s', %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, '%s', %u, '%s')",
        r.id, name.c_str(), uint32(r.race), uint32(r.gender), uint32(r.archetype), uint32(r.side), r.homeZone, uint32(r.level),
        r.xp, r.money, r.kills, r.deaths, r.bounties, r.seed, r.display, state.c_str(), uint32(time(nullptr)), life.c_str());
    for (auto& [player, b] : w.bonds)
        if (b.dirty)
        {
            b.dirty = false;
            CharacterDatabase.PExecute(
                "REPLACE INTO `aw_wanderer_bond` (`wanderer`, `player`, `points`, `greets`, `last_met`, `helped`, `harmed`, `traded`) "
                "VALUES (%u, %u, %d, %u, %u, %u, %u, %u)",
                r.id, player, b.points, b.greets, b.lastMet, b.helped, b.harmed, b.traded);
        }
    w.dirty = false;
}

void WandererMgr::SaveAll(bool all)
{
    for (auto const& w : m_roster)
        if (all || w->dirty)
            Save(*w);
}

// ── Plans and errands (either thread, the wanderer's own state) ────────────────────────────────

void WandererMgr::ArriveAt(FusionWanderer& w)
{
    Region const& r = m_regions[w.region];
    Trip const& t = w.rec.trip;
    if (t.route.empty())
        return;
    // A trip that ends beyond its last node (a hunting ground) leaves it nearest that node.
    uint32 const node = t.route.back();
    w.rec.lastNode = node;
    // A place it came to (a trip to a spot beyond it is not a visit).
    int const i = r.net.IndexOf(node);
    if (i >= 0 && r.net.nodes[size_t(i)].kind != NodeKind::Road && (!t.hasEnd || Dist2d(t.end, r.net.nodes[size_t(i)].pos) < 5.f))
        Remember(w.recentIdx, i);
    w.dirty = true;
}

bool WandererMgr::BoardHasBounty(FusionWanderer const& w) const
{
    if (!m_bounties || !sBountyMgr.IsEnabled())
        return false;
    Region const& r = m_regions[w.region];
    for (auto const& [idx, zone] : r.boardZone)
        for (BountyMgr::KillPosting const& k : sBountyMgr.KillPostings(zone))
            if (!k.elite && k.level <= w.rec.level + 2)
                return true;
    return false;
}

bool WandererMgr::TakeErrand(FusionWanderer& w, uint32 now, uint32 roll)
{
    Region const& r = m_regions[w.region];
    int const board = r.net.IndexOf(w.mind.errand.boardNode);
    w.mind.goal = Goal::Roam;
    if (board < 0)
        return false;
    auto const z = r.boardZone.find(board);
    if (z == r.boardZone.end())
        return false;
    // A bounty for its level (never an elite's), by the roll.
    std::vector<BountyMgr::KillPosting> fits;
    for (BountyMgr::KillPosting& k : sBountyMgr.KillPostings(z->second))
        if (!k.elite && k.level <= w.rec.level + 2)
            fits.push_back(std::move(k));
    if (fits.empty())
        return false;
    BountyMgr::KillPosting const& k = fits[roll % fits.size()];
    Errand e;
    e.questId = k.questId;
    uint32 total = 0;
    for (size_t i = 0; i < k.kills.size() && i < 4; ++i)
    {
        e.entries[i] = k.kills[i].first;
        total += k.kills[i].second;
    }
    e.needed = uint8(std::clamp<uint32>(total, 1, kMaxErrandKills));
    e.boardNode = r.net.nodes[size_t(board)].id;
    e.untilMs = now + kErrandMs;
    e.title = k.title;
    if (std::optional<Vec3> const g = GroundOf(r, e.entries[0], r.net.nodes[size_t(board)].pos))
    {
        e.hasGround = true;
        e.ground = *g;
    }
    else
        return false;
    w.mind.errand = e;
    w.mind.goal = Goal::Hunt;
    w.dirty = true;
    return true;
}

void WandererMgr::TurnInErrand(FusionWanderer& w, uint32 roll)
{
    // Its own purse and experience: about what a player is paid for a kill bounty.
    w.rec.money += LootCopper(w.rec.level, roll) * 6;
    uint8 const cap = LevelCap(BandOf(w));
    GainXp(w.rec.level, w.rec.xp, KillXp(w.rec.level, w.rec.level) * 4, cap);
    ++w.rec.bounties;
    w.mind.goal = Goal::Roam;
    w.mind.errand = Errand();
    w.dirty = true;
}

void WandererMgr::CreditKill(FusionWanderer& w, uint32 entry, uint8 level)
{
    ++w.rec.kills;
    GainXp(w.rec.level, w.rec.xp, KillXp(w.rec.level, level), LevelCap(BandOf(w)));
    if (entry && w.mind.goal == Goal::Hunt && w.mind.errand.Wants(entry) && w.mind.errand.done < w.mind.errand.needed)
        ++w.mind.errand.done;
    w.dirty = true;
}

std::optional<Vec3> WandererMgr::GroundOf(Region const& r, uint32 entry, Vec3 around) const
{
    auto const it = m_spawnsOf.find(entry);
    if (it == m_spawnsOf.end() || it->second.empty())
        return std::nullopt;
    Vec3 best = it->second.front();
    for (Vec3 const& p : it->second)
        if (Dist2d(p, around) < Dist2d(best, around))
            best = p;
    (void)r;
    return best;
}

void WandererMgr::PlanNext(FusionWanderer& w, Vec3 from, uint32 roll)
{
    Region const& r = m_regions[w.region];
    RoadNet const& net = r.net;
    if (net.nodes.empty())
        return;
    int const at = NearestNode(net, from);
    Trip t;
    // Straight from where it stands to a spot (its walk goes from where it is to the trip's
    // second point; its first is only where it starts on paper).
    auto direct = [&](Vec3 end) {
        t = Trip();
        t.route = {net.nodes[size_t(at)].id};
        t.hasEnd = true;
        t.end = end;
    };
    Goal& goal = w.mind.goal;
    // Phase 2: a life's plan (which may be a bounty: the Phase 1 errand from here on).
    int lifeDest = -1;
    if (w.activityOn)
        EndActivity(w, WorldTimer::getMSTime(), nullptr);
    if (m_lives && goal == Goal::Roam && w.life.occupation != Occupation::None)
        lifeDest = PlanLife(w, from, roll);
    if (goal == Goal::TakeBounty)
    {
        // The nearest board.
        int best = -1;
        for (int b : r.boards)
            if (best < 0 || Dist2d(net.nodes[size_t(b)].pos, from) < Dist2d(net.nodes[size_t(best)].pos, from))
                best = b;
        if (best < 0)
            goal = Goal::Roam;
        else
            w.mind.errand.boardNode = net.nodes[size_t(best)].id;
    }
    if (goal == Goal::TakeBounty || goal == Goal::TurnIn)
    {
        int const board = net.IndexOf(w.mind.errand.boardNode);
        if (board < 0)
            goal = Goal::Roam;
        else
            t = PlanTrip(net, at, board);
    }
    if (goal == Goal::Hunt)
    {
        if (!w.mind.errand.hasGround)
            goal = Goal::Roam;
        else if (Dist2d(from, w.mind.errand.ground) < 35.f)
            direct(Near(w.mind.errand.ground, 25.f));
        else
            t = PlanTrip(net, at, NearestNode(net, w.mind.errand.ground), w.mind.errand.ground);
    }
    if (goal == Goal::Roam && lifeDest >= 0)
    {
        Node const& dest = net.nodes[size_t(lifeDest)];
        if (lifeDest == at || Dist2d(dest.pos, from) < 20.f)
        {
            // There already: a step to its spot, arriving at that node.
            t = Trip();
            t.route = {dest.id};
            t.hasEnd = true;
            t.end = Near(dest.pos, 4.f);
        }
        else
            t = PlanTrip(net, at, lifeDest);
    }
    else if (goal == Goal::Roam)
    {
        int const place = NearestNode(net, from, true);
        int const dest = ChooseDestination(net, place >= 0 ? place : at, w.recentIdx, w.rec.homeZone, roll);
        if (dest >= 0)
            t = PlanTrip(net, at, dest);
    }
    if (t.Points() == 0)
        direct(Near(from, 15.f));
    // A one-node trip walks to that node.
    if (t.route.size() == 1 && !t.hasEnd)
    {
        t.hasEnd = true;
        t.end = net.nodes[size_t(net.IndexOf(t.route.front()))].pos;
    }
    w.rec.trip = t;
    w.steps.clear();
    w.moving = false;
    w.stuck = 0;
    w.dirty = true;
}

// ── The map thread ──────────────────────────────────────────────────────────────────────────────

void WandererMgr::UpdateCreature(Creature* c, uint32 /*diff*/)
{
    if (!m_enabled || !c->IsAlive())
        return;
    FusionWanderer& w = *c->m_wanderer;
    uint32 const now = WorldTimer::getMSTime();
    // Resting heals, a little more than WoW's own regeneration out of a fight.
    if (Due(now, w.nextHealMs))
    {
        w.nextHealMs = now + 1000;
        if (w.mind.state == State::Rest && !c->IsInCombat() && c->GetHealth() < c->GetMaxHealth())
            c->SetHealth(std::min(c->GetMaxHealth(), c->GetHealth() + std::max<uint32>(1, c->GetMaxHealth() / 25)));
    }
    if (Due(now, w.nextLookMs))
    {
        w.nextLookMs = now + kLookMs;
        Look(c, w, now);
    }
    // Phase 2: talk, a hire, a chat on the road, remarks at its work. It holds still while
    // spoken to or chatting; hired, it walks with its employer and thinks only to fight.
    bool const hold = m_lives && LifeInWorld(c, w, now);
    bool const escort = w.hiredBy && !w.attacker && !w.bestTarget
        && (w.mind.state == State::Idle || w.mind.state == State::Travel || w.mind.state == State::Greet || w.mind.state == State::Rest);
    if (escort)
        w.nextThinkMs = now + kThinkMs;
    if (Due(now, w.nextThinkMs))
    {
        w.nextThinkMs = now + kThinkMs;
        Region const& r = m_regions[w.region];
        Trip const& t = w.rec.trip;
        Vec3 const me = PosOf(c);
        Sense s;
        s.nowMs = now;
        s.healthPct = c->GetHealthPercent();
        s.attacker = w.attacker;
        s.bestTarget = w.bestTarget;
        s.killed = w.killed;
        s.playerNear = m_chat ? w.playerNear : 0;
        s.arrived = t.Done();
        s.roll = urand(0, 0x7FFFFFFF);
        if (int const board = r.net.IndexOf(w.mind.errand.boardNode); board >= 0)
            s.atBoard = Dist2d(me, r.net.nodes[size_t(board)].pos) <= kBoardReach
                || (s.arrived && t.Points() && Dist2d(TripPoint(r.net, t, t.Points() - 1), r.net.nodes[size_t(board)].pos) < 15.f
                    && Dist2d(me, r.net.nodes[size_t(board)].pos) < 25.f);
        if (s.arrived)
            if (int const last = r.net.IndexOf(w.rec.lastNode); last >= 0)
            {
                Node const& n = r.net.nodes[size_t(last)];
                s.atPlace = n.kind != NodeKind::Road && Dist2d(me, n.pos) < 30.f;
                s.placeKind = n.kind;
            }
        s.atCorpse = w.mind.corpse && Dist2d(me, w.corpseAt) < 3.5f;
        if (s.arrived && w.mind.goal == Goal::Roam && Due(now, w.mind.nextBountyMs)
            && (!m_lives || w.life.occupation == Occupation::None))
            s.boardHasBounty = BoardHasBounty(w);
        if (m_lives)
        {
            s.fleeBelowPct = w.hiredBy ? 0.f : FleeBelowPct(w.life.traits);
            // Arrived where its plan sent it: its activity (StartActivity sets how long).
            if (s.arrived && !w.activityOn && w.mind.goal == Goal::Roam && w.life.occupation != Occupation::None
                && w.life.doing != Activity::None && w.rec.lastNode == w.life.doingNode)
                s.activityMs = 1;
        }
        Act const act = Think(w.mind, s);
        if (act != Act::None)
            Do(c, w, act, now);
        if (w.mind.state != State::Fight)
            w.killed = 0;
    }
    // Out of a fight, no victim kept.
    if (w.mind.state != State::Fight && c->GetVictim())
    {
        c->AttackStop();
        c->SetTargetGuid(ObjectGuid());
    }
    switch (w.mind.state)
    {
        case State::Fight: Fight(c, w, now); break;
        case State::Travel:
            if (!hold && !escort)
                Walk(c, w, now);
            break;
        default: break;
    }
    SendPose(c, w, now, false);
}

void WandererMgr::Look(Creature* c, FusionWanderer& w, uint32 now)
{
    (void)now;
    std::list<Unit*> units;
    MaNGOS::AnyUnitInObjectRangeCheck check(c, kLookRadius);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(c, searcher, kLookRadius);

    std::vector<Candidate> candidates;
    uint64 attacker = 0;
    float attackerDist = 1e9f;
    uint64 greet = 0;
    float greetDist = 1e9f;
    w.nearestPlayer = 1e9f;
    bool helpSeen = false;
    uint64 nearWanderer = 0;
    float nearWandererDist = 1e9f;
    for (Unit* u : units)
    {
        if (u == c || !u->IsAlive() || !u->IsInWorld())
            continue;
        float const d = c->GetDistance(u);
        // On it: swinging at it, or having hurt it (a gun sets no victim; the threat list knows).
        bool const onMe = u->GetVictim() == c || c->GetThreatManager().getThreat(u) > 0.f
            || (w.hiredBy && u->GetVictim() && u->GetVictim()->GetObjectGuid().GetRawValue() == w.hiredBy);
        if (u->GetObjectGuid().GetRawValue() == w.helpFor)
            helpSeen = true;
        if (Player* p = u->ToPlayer())
        {
            if (p->IsGameMaster() && !p->IsGMVisible())
                continue;
            w.nearestPlayer = std::min(w.nearestPlayer, d);
            if (!c->IsHostileTo(p) && d <= kGreetRange && d < greetDist && c->IsWithinLOSInMap(p))
            {
                greet = p->GetObjectGuid().GetRawValue();
                greetDist = d;
            }
            if (onMe && d < attackerDist)
            {
                attacker = p->GetObjectGuid().GetRawValue();
                attackerDist = d;
            }
            continue;
        }
        Creature* uc = u->ToCreature();
        if (uc && uc->m_wanderer)
        {
            // Wanderers never fight each other (yet); one near may stop for a word.
            if (d <= kChatRange && d < nearWandererDist && uc->IsAlive())
            {
                nearWanderer = uc->GetObjectGuid().GetRawValue();
                nearWandererDist = d;
            }
            continue;
        }
        if (!uc)
            continue;
        if (onMe && d < attackerDist)
        {
            attacker = uc->GetObjectGuid().GetRawValue();
            attackerDist = d;
        }
        if (uc->IsPet() || uc->IsTotem() || uc->IsCivilian() || uc->GetCreatureType() == CREATURE_TYPE_CRITTER
            || uc->IsInEvadeMode())
            continue;
        if (!onMe && (!c->IsHostileTo(uc) || !c->IsValidAttackTarget(uc)))
            continue;
        Candidate k;
        k.guid = uc->GetObjectGuid().GetRawValue();
        k.distance = d;
        k.level = uint8(std::min<uint32>(uc->GetLevel(), 255));
        k.elite = uc->IsElite();
        k.attackingMe = onMe;
        k.tappedByOther = uc->HasLootRecipient();
        k.bountyTarget = w.mind.goal == Goal::Hunt && w.mind.errand.Wants(uc->GetEntry());
        k.healthPct = uc->GetHealthPercent();
        // Sight is a ray through the world: only for what it might shoot.
        k.inSight = d <= kShootRange && c->IsWithinLOSInMap(uc);
        candidates.push_back(k);
    }
    // A friend's call: its attacker counts as its own while it is near.
    if (!attacker && w.helpFor && helpSeen)
        attacker = w.helpFor;
    if (!helpSeen)
        w.helpFor = 0;
    TargetRules rules;
    rules.selfLevel = w.rec.level;
    rules.engageRange = std::max(30.f, HoldRange(w.rec.archetype) + 8.f);
    rules.hunting = w.mind.goal == Goal::Hunt;
    rules.startsFights = !m_lives || StartsFights(w.life.traits, w.life.occupation);
    int const pick = ChooseTarget(candidates, rules);
    uint64 best = pick >= 0 ? candidates[size_t(pick)].guid : 0;
    // A fight already on stays on its target while it lives and is near.
    if (w.mind.state == State::Fight && w.mind.target && !attacker)
        for (Candidate const& k : candidates)
            if (k.guid == w.mind.target && k.distance <= kShootRange)
                best = k.guid;
    w.attacker = attacker;
    w.bestTarget = best;
    w.playerNear = greet;
    w.nearWanderer = nearWanderer;
}

void WandererMgr::Do(Creature* c, FusionWanderer& w, Act act, uint32 now)
{
    Region const& r = m_regions[w.region];
    Mind& m = w.mind;
    switch (act)
    {
        case Act::Engage:
        {
            w.steps.clear();
            w.moving = false;
            c->SetStandState(UNIT_STAND_STATE_STAND);
            c->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
            w.chatUntilMs = 0;
            w.following = false;
            // Its victim, without WoW's melee swing (its gun does the work): the fight's
            // bookkeeping, and the friends a call for help brings, follow it.
            if (Unit* t = c->GetMap()->GetUnit(ObjectGuid(m.target)))
                c->Attack(t, false);
            c->SetTargetGuid(ObjectGuid(m.target));
            c->SetSheath(SHEATH_STATE_RANGED);
            c->m_fusionGunId = w.gun;
            w.aimTarget = m.target;
            w.aimSinceMs = now;
            w.nextMoveMs = now;
            break;
        }
        case Act::CallForHelp:
        {
            Say(c, w, HelpLine(w.rec.seed + w.rec.deaths), nullptr, true);
            c->CallForHelp(kHelpRadius);
            // Its fellow wanderers near turn on its attacker (same map: same thread).
            for (auto const& o : m_roster)
            {
                if (o.get() == &w || !o->spawned || o->mapId != w.mapId || o->mind.state == State::Dead
                    || o->rec.side != w.rec.side)
                    continue;
                Creature* oc = c->GetMap()->GetCreature(o->guid);
                if (oc && oc->IsAlive() && oc->GetDistance(c) <= kHelpRadius)
                    o->helpFor = m.target;
            }
            break;
        }
        case Act::LootCorpse:
            StepTo(c, w.corpseAt, false);
            w.killed = 0;
            break;
        case Act::Kneel:
            c->StopMoving();
            c->SetStandState(UNIT_STAND_STATE_KNEEL);
            break;
        case Act::TakeLoot:
            c->SetStandState(UNIT_STAND_STATE_STAND);
            w.rec.money += LootCopper(w.killedLevel, urand(0, 1000));
            m.corpse = 0;
            w.dirty = true;
            break;
        case Act::Stand:
            if (w.activityOn)
                EndActivity(w, now, c);
            c->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
            c->SetStandState(UNIT_STAND_STATE_STAND);
            c->SetSheath(SHEATH_STATE_RANGED);
            c->m_fusionGunId = w.gun;
            break;
        case Act::Sit:
        {
            c->StopMoving();
            c->GetMotionMaster()->MoveIdle();
            w.moving = false;
            c->SetStandState(UNIT_STAND_STATE_SIT);
            c->SetSheath(SHEATH_STATE_UNARMED);
            c->m_fusionGunId = 0;
            int const last = r.net.IndexOf(w.rec.lastNode);
            if (last >= 0 && w.nearestPlayer < 25.f && urand(0, 2) == 0)
                Say(c, w, RestLine(w.rec.seed + now, r.net.nodes[size_t(last)].kind));
            break;
        }
        case Act::Greet:
        {
            Player* p = c->GetMap()->GetPlayer(ObjectGuid(m.greet));
            if (!p)
                break;
            c->StopMoving();
            w.moving = false;
            c->SetFacingToObject(p);
            c->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
            FusionWandererBond& b = w.bonds[p->GetGUIDLow()];
            // A chatty one says more than hello: its news, the weather, its day.
            if (m_lives && w.life.traits.chatty >= 55 && urand(0, 99) < w.life.traits.chatty)
            {
                Talk talk = TalkOf(w, p->GetName(), c);
                talk.bondTier = BondTier(b.points);
                Speak(c, w, ChatLine(talk, w.rec.seed + b.greets + now), p, false, true);
            }
            else
                Say(c, w, GreetLine(w.rec.seed, BondTier(b.points), p->GetName(), b.greets), p);
            b.points += kGreetPoints;
            ++b.greets;
            b.lastMet = uint32(time(nullptr));
            b.dirty = true;
            w.dirty = true;
            break;
        }
        case Act::PlanTrip:
            c->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
            PlanNext(w, PosOf(c), urand(0, 0x7FFFFFFF));
            break;
        case Act::Work:
        {
            // Arrived where its plan sent it: its work, a meal, a sale, a song.
            uint32 const ms = StartActivity(w, now, c);
            m.untilMs = now + ms;
            c->StopMoving();
            c->GetMotionMaster()->MoveIdle();
            w.moving = false;
            ActivityPose(c, w);
            if (w.nearestPlayer < 25.f && urand(0, 1) == 0)
                Speak(c, w, ActivityLine(TalkOf(w, std::string(), c), w.rec.seed + now));
            break;
        }
        case Act::Flee:
        {
            // To the nearest settlement at a run (or straight away from its attacker), crying for
            // help, by name to a player near.
            c->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
            c->SetStandState(UNIT_STAND_STATE_STAND);
            c->AttackStop();
            Vec3 const me = PosOf(c);
            Vec3 to = me;
            Unit* foe = c->GetMap()->GetUnit(ObjectGuid(m.target));
            if (foe)
                to = me + (me - PosOf(foe)).Normalized() * 30.f;
            int const town = NearestNode(r.net, me, true);
            if (town >= 0 && Dist2d(r.net.nodes[size_t(town)].pos, me) < 150.f)
                to = r.net.nodes[size_t(town)].pos;
            StepTo(c, to, true);
            w.steps.clear();
            w.moving = false;
            Player* helper = c->GetMap()->GetPlayer(ObjectGuid(w.playerNear));
            Talk talk = TalkOf(w, helper ? helper->GetName() : std::string(), c);
            Speak(c, w, FleeLine(talk, w.rec.seed + now), nullptr, true, true);
            c->CallForHelp(kHelpRadius);
            for (auto const& o : m_roster)
            {
                if (o.get() == &w || !o->spawned || o->mapId != w.mapId || o->mind.state == State::Dead || o->rec.side != w.rec.side)
                    continue;
                Creature* oc = c->GetMap()->GetCreature(o->guid);
                if (oc && oc->IsAlive() && oc->GetDistance(c) <= kHelpRadius)
                    o->helpFor = m.target;
            }
            if (foe)
                News(w, NewsKind::Fled, foe->GetName(), PlaceNear(r, me));
            Nudge(w.life.needs, -8);
            break;
        }
        case Act::TakeBounty:
            c->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
            if (TakeErrand(w, now, urand(0, 0x7FFFFFFF)) && w.nearestPlayer < 30.f)
                Say(c, w, BountyTakenLine(w.rec.seed + now, m.errand.title));
            break;
        case Act::TurnIn:
        {
            std::string const title = m.errand.title;
            c->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
            uint8 const before = w.rec.level;
            TurnInErrand(w, urand(0, 1000));
            if (w.nearestPlayer < 30.f)
                Say(c, w, BountyDoneLine(w.rec.seed + now, title));
            if (w.rec.level != before)
                c->SetLevel(w.rec.level);
            break;
        }
        case Act::Walk:
            w.steps.clear();
            w.moving = false;
            break;
        case Act::None:
            break;
    }
}

void WandererMgr::StepTo(Creature* c, Vec3 p, bool run)
{
    float const ground = c->GetMap()->GetHeight(p.x, p.y, p.z + 30.f, true, 60.f);
    if (ground > INVALID_HEIGHT)
        p.z = ground;
    c->SetStandState(UNIT_STAND_STATE_STAND);
    c->GetMotionMaster()->MovePoint(kWandererPointId, p.x, p.y, p.z, MOVE_PATHFINDING | (run ? MOVE_RUN_MODE : MOVE_WALK_MODE));
}

void WandererMgr::Walk(Creature* c, FusionWanderer& w, uint32 now)
{
    Region const& r = m_regions[w.region];
    Trip& t = w.rec.trip;
    if (t.Done())
    {
        w.moving = false;
        return;
    }
    Vec3 const me = PosOf(c);
    if (w.steps.empty())
    {
        w.steps = Steps(me, TripPoint(r.net, t, t.leg + 1), kStepYards);
        w.step = 0;
        w.moving = false;
    }
    if (w.step >= w.steps.size())
    {
        w.steps.clear();
        return;
    }
    float const d = Dist2d(me, w.steps[w.step]);
    // Stopped short: its move has had time to start (a path is not always ready at once) and
    // has ended away from the step.
    bool const stopped = w.moving && c->movespline->Finalized() && now - w.stepAtMs > 1500;
    if (d < 3.f || stopped)
    {
        if (d >= 3.f)
        {
            // Its path ended short of the step (water, a cliff): on to the next, and after a few
            // misses on to the next point of its trip.
            if (++w.stuck > 4)
                w.step = w.steps.size();
        }
        else
            w.stuck = 0;
        w.moving = false;
        if (++w.step >= w.steps.size())
        {
            ++t.leg;
            t.along = 0.f;
            w.steps.clear();
            w.stuck = 0;
            w.dirty = true;
            if (t.Done())
                ArriveAt(w);
            return;
        }
    }
    if (!w.moving)
    {
        StepTo(c, w.steps[w.step], false);
        w.moving = true;
        w.stepAtMs = now;
    }
}

void WandererMgr::Fight(Creature* c, FusionWanderer& w, uint32 now)
{
    Unit* t = c->GetMap()->GetUnit(ObjectGuid(w.mind.target));
    if (!t || !t->IsAlive() || !t->IsInWorld())
        return;
    Fusion::WeaponProfile const* weapon = sFusionMgr.Weapons().Find(w.gun);
    if (!weapon)
        return;
    float const dist = c->GetDistance(t);
    bool const sight = dist <= kShootRange && c->IsWithinLOSInMap(t);
    float const hold = HoldRange(w.rec.archetype);
    bool const stopped = c->movespline->Finalized();
    if (Due(now, w.nextMoveMs))
    {
        if (!sight || dist > hold + 6.f)
        {
            // Closer: to its standing-off distance in the open, or right up when it cannot see.
            Vec3 const tp = PosOf(t);
            Vec3 const away = (PosOf(c) - tp).Normalized();
            StepTo(c, tp + away * (sight ? hold * 0.8f : 4.f), true);
            w.nextMoveMs = now + 1200;
        }
        else if (!stopped && dist <= hold)
        {
            c->StopMoving();
            c->GetMotionMaster()->MoveIdle();
            w.nextMoveMs = now + 600;
        }
        else
            w.nextMoveMs = now + 600;
    }
    if (!sight)
        return;
    if (stopped && !c->HasInArc(t, M_PI_F / 3.f))
        c->SetFacingToObject(t);
    if (w.aimTarget != w.mind.target)
    {
        w.aimTarget = w.mind.target;
        w.aimSinceMs = now;
    }
    // Its trigger, as an NPC gunner's: bursts by the gun's class.
    uint32 burst = 3, gap = std::max<uint32>(60, uint32(weapon->fireTime * 1000.f)), pause = 1100;
    if (weapon->weaponClass == Fusion::WeaponClass::Shotgun || weapon->weaponClass == Fusion::WeaponClass::Pistol)
        burst = 1, pause = 700;
    else if (weapon->weaponClass == Fusion::WeaponClass::Sniper)
        burst = 1, pause = 2200;
    else if (weapon->weaponClass == Fusion::WeaponClass::Lmg)
        burst = 5;
    Fusion::NpcTrigger trigger{w.burstLeft, w.nextShotMs, w.triggerStarted};
    bool const fires = trigger.Fire(now, burst, gap, pause);
    w.burstLeft = trigger.burstLeft;
    w.nextShotMs = trigger.nextMs;
    w.triggerStarted = trigger.started;
    if (fires)
        sFusionMgr.WandererRound(c, t, *weapon, now - w.aimSinceMs);
}

void WandererMgr::SendPose(Creature* c, FusionWanderer& w, uint32 now, bool force)
{
    namespace AS = Fusion::ActionSystems;
    AS::Pose p;
    p.system = uint8(AS::System::Modern);
    p.stance = 0;
    p.mainSubclass = 255;
    p.offSubclass = 255;
    p.gun = w.gun;
    switch (w.mind.state)
    {
        case State::Fight:
        {
            p.flags = AS::kPoseGunUp;
            if (Unit* t = c->GetMap()->GetUnit(ObjectGuid(w.mind.target)))
            {
                float const d2 = std::max(0.5f, std::hypot(t->GetPositionX() - c->GetPositionX(), t->GetPositionY() - c->GetPositionY()));
                float const dz = (t->GetPositionZ() + 1.f) - (c->GetPositionZ() + 1.4f);
                p.pitch = int8(std::clamp(std::atan2(dz, d2) * 180.f / float(M_PI), -89.f, 89.f));
                if (c->movespline->Finalized() && d2 > 12.f)
                    p.flags |= AS::kPoseAds;
            }
            break;
        }
        case State::Rest:
            p.flags = 0;
            p.gun = 0;
            break;
        case State::Loot:
            p.flags = w.mind.kneeling ? 0 : AS::kPoseGunUp | AS::kPoseSprint;
            break;
        default:
            // On the road the gun is carried low, as at a sprint.
            p.flags = AS::kPoseGunUp | AS::kPoseSprint;
            break;
    }
    uint16 const key = uint16(p.flags | (p.gun ? 0x100 : 0));
    if (!force && key == w.lastPose && !Due(now, w.nextPoseMs))
        return;
    w.lastPose = key;
    w.nextPoseMs = now + 1000;
    sFusionMgr.TellNearOn(c->GetMap(), AS::kChannel, PosOf(c), AS::EncodePoseOf(c->GetObjectGuid().GetRawValue(), p), nullptr,
        AS::kTellRadius);
}

void WandererMgr::Say(Creature* c, FusionWanderer const& w, std::string const& line, Unit const* to, bool yell) const
{
    (void)w;
    if (!m_chat || line.empty())
        return;
    if (yell)
        c->MonsterYell(line.c_str(), LANG_UNIVERSAL, to);
    else
        c->MonsterSay(line.c_str(), LANG_UNIVERSAL, to);
}

void WandererMgr::OnKill(Creature* c, Unit* victim)
{
    if (!m_enabled || !victim)
        return;
    FusionWanderer& w = *c->m_wanderer;
    w.killed = victim->GetObjectGuid().GetRawValue();
    w.corpseAt = PosOf(victim);
    w.killedLevel = uint8(std::min<uint32>(victim->GetLevel(), 255));
    uint8 const before = w.rec.level;
    CreditKill(w, victim->IsCreature() ? victim->GetEntry() : 0, w.killedLevel);
    if (m_lives)
    {
        Nudge(w.life.needs, 2);
        if (w.killedLevel + 1 >= w.rec.level || (victim->IsCreature() && victim->ToCreature()->IsElite()))
            News(w, NewsKind::Fight, victim->GetName(), PlaceNear(m_regions[w.region], PosOf(c)));
    }
    if (w.rec.level != before)
    {
        // A level: a bigger pool, perhaps a better gun.
        c->SetLevel(w.rec.level);
        float const pct = c->GetHealthPercent();
        uint32 const maxHp = std::max<uint32>(1, uint32(Fusion::Scale::PlayerMaxHealth(w.rec.level, 0.f) + 0.5f));
        c->m_fusionWowMaxHealth = std::max<uint32>(1, uint32(sFusionMgr.Par().MaxHealth(w.rec.level) + 0.5f));
        c->SetMaxHealth(maxHp);
        c->SetHealthPercent(pct);
        if (Fusion::WeaponProfile const* wp = sFusionMgr.Weapons().Find(GunFor(w.rec.archetype, w.rec.level, w.rec.seed)))
        {
            w.gun = wp->id;
            c->m_fusionGunId = w.gun;
        }
    }
}

void WandererMgr::OnDeath(Creature* c, Unit* killer)
{
    if (!m_enabled)
        return;
    FusionWanderer& w = *c->m_wanderer;
    uint32 const now = WorldTimer::getMSTime();
    Died(w.mind, now);
    w.diedAtMs = now;
    w.healthPct = 100.f;
    w.steps.clear();
    w.moving = false;
    ++w.rec.deaths;
    if (m_lives)
    {
        Nudge(w.life.needs, -20);
        if (killer)
            News(w, NewsKind::Died, killer->GetName(), PlaceNear(m_regions[w.region], PosOf(c)));
        if (w.hiredBy)
            EndHire(c, w, false);
    }
    // A player who killed it is remembered for it.
    if (Player* p = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr)
    {
        FusionWandererBond& b = w.bonds[p->GetGUIDLow()];
        ++b.harmed;
        b.points -= 5;
        b.lastMet = uint32(time(nullptr));
        b.dirty = true;
    }
    w.dirty = true;
}

// ── GM commands ─────────────────────────────────────────────────────────────────────────────────

std::vector<std::string> WandererMgr::List(Player const* viewer) const
{
    std::vector<std::string> out;
    if (!m_enabled)
    {
        out.push_back("Wanderers are off (Wanderers.Enable).");
        return out;
    }
    uint32 spawned = 0;
    for (auto const& wp : m_roster)
        spawned += wp->spawned ? 1 : 0;
    out.push_back(std::to_string(m_roster.size()) + " wanderers, " + std::to_string(spawned) + " in the world:");
    for (auto const& wp : m_roster)
    {
        FusionWanderer const& w = *wp;
        Region const& r = m_regions[w.region];
        Vec3 at = TripPos(r.net, w.rec.trip);
        if (w.spawned && viewer && viewer->GetMapId() == r.map)
            if (Creature const* c = viewer->GetMap()->GetCreature(w.guid))
                at = PosOf(c);
        std::ostringstream s;
        s << "#" << w.rec.id << " " << w.rec.name << ", level " << uint32(w.rec.level) << " " << RaceName(w.rec.race) << " ";
        if (m_lives && w.life.occupation != Occupation::None)
            s << Title(w.life.occupation, w.life.homeName) << ": " << StateName(w.mind.state) << ", " << ActivityName(w.life.doing);
        else
            s << ArchetypeTitle(w.rec.archetype) << " of " << AreaName(w.rec.homeZone) << ": " << StateName(w.mind.state) << ", "
              << GoalName(w.mind.goal);
        s << (w.spawned ? ", in the world" : ", unseen");
        if (viewer && viewer->GetMapId() == r.map)
            s << ", " << uint32(Dist2d(PosOf(viewer), at)) << " yd away";
        out.push_back(s.str());
    }
    return out;
}

std::vector<std::string> WandererMgr::Info(uint32 id, Player const* gm) const
{
    std::vector<std::string> out;
    FusionWanderer const* w = ById(id);
    if (!m_enabled || !w)
    {
        out.push_back(m_enabled ? "No such wanderer (.wanderer list)." : "Wanderers are off (Wanderers.Enable).");
        return out;
    }
    Region const& r = m_regions[w->region];
    Record const& rec = w->rec;
    std::ostringstream a;
    Fusion::WeaponProfile const* gun = sFusionMgr.Weapons().Find(w->gun);
    a << "#" << rec.id << " " << rec.name << " <Wandering " << ArchetypeTitle(rec.archetype) << ">, " << (rec.gender ? "female " : "male ")
      << RaceName(rec.race) << ", level " << uint32(rec.level) << " (" << rec.xp << "/" << XpToLevel(rec.level) << " xp), "
      << (gun ? gun->name : std::string("no gun")) << "; creature " << w->entry << ", model " << rec.display << ".";
    out.push_back(a.str());
    std::ostringstream b;
    b << "Home " << AreaName(rec.homeZone) << " (" << r.name << "). " << rec.kills << " kills, " << rec.deaths << " deaths, "
      << rec.bounties << " bounties, " << rec.money / 10000 << "g " << (rec.money / 100) % 100 << "s " << rec.money % 100 << "c.";
    out.push_back(b.str());
    std::ostringstream c;
    c << "Now: " << StateName(w->mind.state) << ", " << GoalName(w->mind.goal);
    if (w->mind.goal != Goal::Roam && w->mind.errand.questId)
        c << " (" << w->mind.errand.title << ", " << uint32(w->mind.errand.done) << "/" << uint32(w->mind.errand.needed) << ")";
    Trip const& t = rec.trip;
    if (!t.Done())
    {
        size_t const next = t.leg + 1;
        if (next < t.route.size())
        {
            int const i = r.net.IndexOf(t.route[next]);
            c << "; walking to " << (i >= 0 ? r.net.nodes[size_t(i)].name : std::string("?"));
        }
        else
            c << "; walking to a spot";
        c << " (point " << next << " of " << t.Points() << ")";
    }
    c << (w->spawned ? "; in the world" : "; unseen") << ".";
    out.push_back(c.str());
    if (gm)
    {
        auto const it = w->bonds.find(gm->GetGUIDLow());
        std::ostringstream d;
        if (it == w->bonds.end())
            d << "It has not met you.";
        else
            d << "It knows you: " << it->second.points << " points (tier " << uint32(BondTier(it->second.points)) << "), greeted "
              << it->second.greets << " times; you helped it " << it->second.helped << ", harmed it " << it->second.harmed
              << " and traded with it " << it->second.traded << " times.";
        out.push_back(d.str());
    }
    if (m_lives)
        for (std::string const& line : LifeInfo(*w))
            out.push_back(line);
    return out;
}

bool WandererMgr::Summon(Player* gm, uint32 id, std::string& why)
{
    if (!m_enabled)
    {
        why = "Wanderers are off (Wanderers.Enable).";
        return false;
    }
    FusionWanderer* pick = nullptr;
    if (id)
    {
        pick = ById(id);
        if (!pick)
        {
            why = "No such wanderer (.wanderer list).";
            return false;
        }
        if (m_regions[pick->region].map != gm->GetMapId())
        {
            why = "That wanderer lives on another continent.";
            return false;
        }
    }
    else
    {
        // The nearest unseen one of a region on the GM's map.
        float best = 1e9f;
        for (auto const& wp : m_roster)
        {
            Region const& r = m_regions[wp->region];
            if (wp->spawned || r.map != gm->GetMapId())
                continue;
            float const d = Dist2d(TripPos(r.net, wp->rec.trip), PosOf(gm));
            if (d < best)
            {
                best = d;
                pick = wp.get();
            }
        }
        if (!pick)
        {
            why = "Every wanderer of this continent is in the world already (.wanderer list).";
            return false;
        }
    }
    if (pick->spawned)
        SpawnOut(*pick);
    if (pick->mind.state == State::Dead)
        Revived(pick->mind);
    pick->hiddenUntilMs = 0;
    pick->healthPct = 100.f;
    float const o = gm->GetOrientation();
    Vec3 const front = V(gm->GetPositionX() + std::cos(o) * 3.f, gm->GetPositionY() + std::sin(o) * 3.f, gm->GetPositionZ());
    PlanNext(*pick, front, urand(0, 0x7FFFFFFF));
    SpawnIn(*pick, &front);
    if (!pick->spawned)
    {
        why = "It could not be summoned here (see the server log).";
        return false;
    }
    why = "#" + std::to_string(pick->rec.id) + " " + pick->rec.name + " is here, and off " +
        std::string(GoalName(pick->mind.goal)) + ".";
    return true;
}

uint32 WandererMgr::Dismiss(uint32 id)
{
    uint32 n = 0;
    uint32 const now = WorldTimer::getMSTime();
    for (auto const& wp : m_roster)
    {
        if (id && wp->rec.id != id)
            continue;
        if (wp->spawned)
        {
            SpawnOut(*wp);
            ++n;
        }
        wp->hiddenUntilMs = now + kHiddenMs;
    }
    return n;
}

bool ChatHandler::HandleWandererCommand(char* args)
{
    std::istringstream in(args ? std::string(args) : std::string());
    std::string what, arg;
    in >> what >> arg;
    Player* gm = GetSession() ? GetSession()->GetPlayer() : nullptr;
    auto idArg = [&]() -> uint32 {
        if (!arg.empty() && arg.find_first_not_of("0123456789") == std::string::npos && arg.size() < 4)
            return uint32(std::stoul(arg));
        return WandererMgr::IdOf(GetSelectedCreature());
    };
    if (what == "list" || what.empty())
    {
        for (std::string const& line : sWandererMgr.List(gm))
            SendSysMessage(line.c_str());
        return true;
    }
    if (what == "info")
    {
        uint32 const id = idArg();
        if (!id)
        {
            SendSysMessage("Select a wanderer, or give its id: .wanderer info <id>");
            return true;
        }
        for (std::string const& line : sWandererMgr.Info(id, gm))
            SendSysMessage(line.c_str());
        return true;
    }
    if (what == "spawn")
    {
        if (!gm)
            return false;
        std::string why;
        uint32 const id = !arg.empty() ? idArg() : 0;
        sWandererMgr.Summon(gm, id, why);
        SendSysMessage(why.c_str());
        return true;
    }
    if (what == "goto")
    {
        if (!gm)
            return false;
        std::string why;
        uint32 const id = idArg();
        if (!id)
        {
            SendSysMessage("Give its id: .wanderer goto <id>");
            return true;
        }
        sWandererMgr.Goto(gm, id, why);
        SendSysMessage(why.c_str());
        return true;
    }
    if (what == "despawn")
    {
        uint32 const id = arg == "all" ? 0 : idArg();
        if (!id && arg != "all")
        {
            SendSysMessage("Select a wanderer, or: .wanderer despawn <id|all>");
            return true;
        }
        uint32 const n = sWandererMgr.Dismiss(id);
        PSendSysMessage("%u wanderer(s) sent out of the world; they stay out for five minutes.", n);
        return true;
    }
    SendSysMessage(".wanderer list: the roster. .wanderer info [id]: one of them (or the one selected): its trade, nature, "
        "day, pack, friends and news. .wanderer goto <id>: go to it. .wanderer spawn [id]: bring one to you. "
        ".wanderer despawn <id|all>: send them out of the world.");
    return true;
}
