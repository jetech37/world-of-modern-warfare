/*
 * World of Modern Warfare: the bounty boards. GPL-2.0-or-later. See FusionBounties.h.
 */

#include "FusionBounties.h"
#include "FusionMgr.h"
#include "Chat.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "GameObject.h"
#include "GridDefines.h"
#include "GridMap.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>

using namespace Fusion::Bounty;

namespace
{
    int64 UnixNow() { return int64(time(nullptr)); }

    LocalTime LocalOf(int64 unix)
    {
        time_t const t = time_t(unix);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        LocalTime lt;
        lt.year = tm.tm_year + 1900;
        lt.month = unsigned(tm.tm_mon + 1);
        lt.day = unsigned(tm.tm_mday);
        lt.hour = unsigned(tm.tm_hour);
        lt.minute = unsigned(tm.tm_min);
        lt.second = unsigned(std::min(tm.tm_sec, 59));
        return lt;
    }

    std::string AreaName(uint32 id)
    {
        AreaEntry const* a = id ? AreaEntry::GetById(id) : nullptr;
        return a && a->Name ? std::string(a->Name) : std::string();
    }

    std::string Money(uint32 copper)
    {
        std::ostringstream s;
        uint32 const g = copper / 10000, sv = (copper / 100) % 100, c = copper % 100;
        if (g)
            s << g << "g ";
        if (g || sv)
            s << sv << "s ";
        s << c << "c";
        return s.str();
    }

    // A spawn of the open world, for finding its zone in the map files.
    struct SpawnRec
    {
        uint32 map = 0;
        float x = 0.f, y = 0.f;
        std::array<uint32, MAX_CREATURE_IDS_PER_SPAWN> ids{};
        int fileX = 0, fileY = 0;
    };

    constexpr uint8 kTeamAlliance = 1, kTeamHorde = 2;

    // The quest in a log slot (Player keeps its own slot helpers private).
    uint32 SlotQuest(Player const* player, uint16 slot)
    {
        return player->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
    }

    bool InLog(Player const* player, uint32 questId)
    {
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            if (SlotQuest(player, slot) == questId)
                return true;
        return false;
    }
    constexpr uint32 kCheckEveryMs = 2000;
}

BountyMgr& BountyMgr::Instance()
{
    static BountyMgr instance;
    return instance;
}

// ── Startup ─────────────────────────────────────────────────────────────────────────────────────

void BountyMgr::Load()
{
    m_enabled = sConfig.GetBoolDefault("Bounty.Enable", true);
    m_resetHour = uint8(std::clamp(sConfig.GetIntDefault("Bounty.ResetHour", 6), 0, 23));
    if (!m_enabled)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bounty boards: off (Bounty.Enable).");
        return;
    }

    // The characters tables, made here as aw_feedback is (FusionFeedback.cpp).
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `aw_bounty_day` ("
        "`day` int(10) unsigned NOT NULL,"
        "`salt` int(10) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`day`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW bounty boards: a GM re-roll of a day'");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `aw_bounty_char` ("
        "`guid` int(10) unsigned NOT NULL,"
        "`quest` int(10) unsigned NOT NULL,"
        "`day` int(10) unsigned NOT NULL,"
        "`qkey` int(10) unsigned NOT NULL,"
        "`done_day` int(10) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`guid`, `quest`, `day`, `qkey`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW bounty boards: the bounties a character took'");

    Tick();
    uint32 const today = m_today;
    // A week is plenty: a bounty runs two days, the cap counts one.
    CharacterDatabase.PExecute("DELETE FROM `aw_bounty_char` WHERE `day` + 7 < %u AND `done_day` + 7 < %u", today, today);
    CharacterDatabase.PExecute("DELETE FROM `aw_bounty_day` WHERE `day` + 7 < %u", today);
    if (std::unique_ptr<QueryResult> result = CharacterDatabase.Query("SELECT `day`, `salt` FROM `aw_bounty_day`"))
    {
        do
        {
            Field* f = result->Fetch();
            m_salts[f[0].GetUInt32()] = f[1].GetUInt32();
        } while (result->NextRow());
    }

    LoadZones();
    BuildPools();
    MakeQuests();
    // The day before yesterday too: its bounties have expired, but a character logging in with
    // one still sees what it was when it is taken out.
    for (uint32 back = 3; back-- > 0;)
        if (today >= back)
            PostDay(today - back);

    size_t kills = 0, collects = 0, hunts = 0, camps = 0, empty = 0;
    for (auto const& [zone, pool] : m_pools)
    {
        kills += pool.kills.size();
        collects += pool.collects.size();
        hunts += pool.hunts.size();
        camps += pool.camps.size();
        empty += pool.Empty() ? 1 : 0;
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        "Bounty boards: %u zones (%u with nothing to post), %u boards; %u kill, %u collect, %u hunt and %u camp "
        "targets; day %u, reset at %02u:00.",
        uint32(m_zones.size()), uint32(empty), uint32(m_boards.size()), uint32(kills), uint32(collects), uint32(hunts),
        uint32(camps), today, uint32(m_resetHour));
}

void BountyMgr::LoadZones()
{
    m_zones.clear();
    m_boards.clear();
    if (std::unique_ptr<QueryResult> result = WorldDatabase.Query(
            "SELECT `zone`, `slot`, `min_level`, `max_level` FROM `aw_bounty_zone`"))
    {
        std::set<uint32> slots;
        do
        {
            Field* f = result->Fetch();
            ZoneRow z;
            z.zone = f[0].GetUInt32();
            z.slot = f[1].GetUInt32();
            z.band.minLevel = uint8(std::clamp<uint32>(f[2].GetUInt32(), 1, 60));
            z.band.maxLevel = uint8(std::clamp<uint32>(f[3].GetUInt32(), z.band.minLevel, 60));
            z.name = AreaName(z.zone);
            if (z.slot >= kMaxZones || !slots.insert(z.slot).second || z.name.empty())
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "aw_bounty_zone: zone %u (slot %u) left out: an unknown zone, or a slot "
                    "taken or past %u.", z.zone, z.slot, kMaxZones - 1);
                continue;
            }
            m_zones[z.zone] = z;
        } while (result->NextRow());
    }
    if (std::unique_ptr<QueryResult> result = WorldDatabase.Query(
            "SELECT `guid`, `zone`, `town`, `team`, `rep_faction` FROM `aw_bounty_board`"))
    {
        do
        {
            Field* f = result->Fetch();
            uint32 const guid = f[0].GetUInt32();
            BoardRow b;
            b.zone = f[1].GetUInt32();
            b.town = f[2].GetCppString();
            b.team = uint8(f[3].GetUInt32());
            b.repFaction = f[4].GetUInt32();
            if (!m_zones.count(b.zone))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "aw_bounty_board: board %u is in zone %u, which posts no bounties.", guid, b.zone);
                continue;
            }
            if (b.repFaction && !sObjectMgr.GetFactionEntry(b.repFaction))
                b.repFaction = 0;
            // The spawn should stand in its zone: a board placed by an anchor creature that moved
            // is reported, not trusted to be where the table says.
            if (GameObjectData const* d = sObjectMgr.GetGOData(guid))
            {
                uint32 const at = sTerrainMgr.GetZoneId(d->position.mapId, d->position.x, d->position.y, d->position.z);
                if (at != b.zone)
                    sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "aw_bounty_board: board %u (%s) stands in zone %u, not %u.",
                        guid, b.town.c_str(), at, b.zone);
            }
            m_boards[guid] = b;
        } while (result->NextRow());
    }
}

void BountyMgr::BuildPools()
{
    m_pools.clear();
    for (auto const& [zone, z] : m_zones)
    {
        ZonePool& p = m_pools[zone];
        p.zone = zone;
        p.zoneName = z.name;
        p.band = z.band;
    }

    // Game-event spawns (the Scourge Invasion, the holidays) come and go: never a target.
    std::set<uint32> eventGuids;
    if (std::unique_ptr<QueryResult> result = WorldDatabase.Query("SELECT `guid` FROM `game_event_creature`"))
    {
        do
            eventGuids.insert(result->Fetch()[0].GetUInt32());
        while (result->NextRow());
    }

    // Every creature's spawns, open world and instance.
    std::unordered_map<uint32, CreatureFacts> facts;
    std::vector<SpawnRec> spawns;
    auto gather = [&](CreatureDataPair const& pair) {
        if (eventGuids.count(pair.first))
            return false;
        CreatureData const& d = pair.second;
        uint32 const map = d.position.mapId;
        bool const open = map == 0 || map == 1;
        SpawnRec s;
        s.map = map;
        s.x = d.position.x;
        s.y = d.position.y;
        bool any = false;
        for (size_t i = 0; i < d.creature_id.size(); ++i)
        {
            uint32 const id = d.creature_id[i];
            if (!id)
                continue;
            CreatureFacts& c = facts[id];
            c.entry = id;
            if (open)
            {
                ++c.openWorldSpawns;
                c.longestRespawnSecs = std::max(c.longestRespawnSecs, d.spawntimesecsmax);
                s.ids[i] = id;
                any = true;
            }
            else
                ++c.instanceSpawns;
        }
        if (open && any)
        {
            s.fileX = int(32 - s.x / SIZE_OF_GRIDS);
            s.fileY = int(32 - s.y / SIZE_OF_GRIDS);
            if (s.fileX >= 0 && s.fileX < MAX_NUMBER_OF_GRIDS && s.fileY >= 0 && s.fileY < MAX_NUMBER_OF_GRIDS)
                spawns.push_back(s);
        }
        return false;
    };
    sObjectMgr.DoCreatureData(gather);

    // The template's side of the facts.
    for (auto& [entry, c] : facts)
    {
        CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(entry);
        if (!ci)
        {
            c.entry = 0;
            continue;
        }
        c.minLevel = uint8(std::min<uint32>(ci->level_min, 255));
        c.maxLevel = uint8(std::min<uint32>(ci->level_max, 255));
        c.rank = uint8(ci->rank);
        c.type = uint8(ci->type);
        c.townsfolk = ci->npc_flags != 0 || ci->civilian
            || (ci->flags_extra & (CREATURE_FLAG_EXTRA_GUARD | CREATURE_FLAG_EXTRA_INVISIBLE));
        FactionTemplateEntry const* ft = sObjectMgr.GetFactionTemplateEntry(ci->faction);
        c.hostile = ft && (ft->IsHostileToPlayers() || ft->IsNeutralToAll());
        c.unattackable = (ci->static_flags1 & (CREATURE_STATIC_FLAG_IMMUNE_TO_PC | CREATURE_STATIC_FLAG_UNINTERACTIBLE)) != 0;
    }

    // Each open-world spawn's zone and area, from the map files (one file loaded at a time; the
    // vmaps are not needed for a creature standing outdoors, and the server's own terrain cache
    // is left alone).
    std::sort(spawns.begin(), spawns.end(), [](SpawnRec const& a, SpawnRec const& b) {
        return std::tie(a.map, a.fileX, a.fileY) < std::tie(b.map, b.fileX, b.fileY);
    });
    std::map<uint32, std::map<uint32, uint32>> inZone;              // zone -> entry -> spawns
    std::map<uint32, std::vector<SpawnPoint>> points;               // zone -> spawn points
    std::unique_ptr<GridMap> grid;
    bool gridOk = false;
    int loadedMap = -1, loadedX = -1, loadedY = -1;
    for (SpawnRec const& s : spawns)
    {
        if (int(s.map) != loadedMap || s.fileX != loadedX || s.fileY != loadedY)
        {
            loadedMap = int(s.map);
            loadedX = s.fileX;
            loadedY = s.fileY;
            grid = std::make_unique<GridMap>();
            char name[32];
            snprintf(name, sizeof(name), "maps/%03u%02u%02u.map", s.map, uint32(s.fileX), uint32(s.fileY));
            gridOk = grid->loadData((sWorld.GetDataPath() + name).c_str());
        }
        if (!gridOk)
            continue;
        uint16 const flag = grid->getArea(s.x, s.y);
        uint32 const zone = TerrainManager::GetZoneIdByAreaFlag(flag, s.map);
        if (!m_pools.count(zone))
            continue;
        uint32 const area = TerrainManager::GetAreaIdByAreaFlag(flag, s.map);
        for (uint32 id : s.ids)
            if (id)
            {
                ++inZone[zone][id];
                points[zone].push_back({id, s.x, s.y, area});
            }
    }
    grid.reset();

    // Collect drops: plain rows of the loot tables, often enough not to be a grind.
    std::unordered_map<uint32, std::vector<std::pair<uint32, float>>> loot;   // loot id -> (item, chance 0-1)
    uint32 const patch = sWorld.GetWowPatch();
    if (std::unique_ptr<QueryResult> result = WorldDatabase.PQuery(
            "SELECT `entry`, `item`, `ChanceOrQuestChance` FROM `creature_loot_template` "
            "WHERE `groupid` = 0 AND `mincountOrRef` > 0 AND `ChanceOrQuestChance` >= %f "
            "AND `patch_min` <= %u AND `patch_max` >= %u",
            double(kMinDropChance * 100.f), patch, patch))
    {
        do
        {
            Field* f = result->Fetch();
            loot[f[0].GetUInt32()].emplace_back(f[1].GetUInt32(), std::min(1.f, f[2].GetFloat() / 100.f));
        } while (result->NextRow());
    }

    for (auto& [zone, pool] : m_pools)
    {
        std::map<uint32, uint32> const& counts = inZone[zone];
        for (auto const& [entry, n] : counts)
        {
            auto it = facts.find(entry);
            if (it == facts.end() || !it->second.entry)
                continue;
            CreatureFacts const& c = it->second;
            CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(entry);
            if (IsKillTarget(c, n, pool.band))
                pool.kills.push_back({entry, ci->name, LevelOf(c), n, ci->faction});
            else if (IsHuntTarget(c, n, pool.band))
                pool.hunts.push_back({entry, ci->name, LevelOf(c), c.rank == kRankElite || c.rank == kRankRareElite});
        }
        // A drop's dropper: the zone's kill target with most spawns that drops it.
        std::vector<KillCandidate> bySpawns = pool.kills;
        std::sort(bySpawns.begin(), bySpawns.end(), [](KillCandidate const& a, KillCandidate const& b) {
            return a.spawns != b.spawns ? a.spawns > b.spawns : a.entry < b.entry;
        });
        std::set<uint32> items;
        for (KillCandidate const& k : bySpawns)
        {
            CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(k.entry);
            auto rows = ci && ci->loot_id ? loot.find(ci->loot_id) : loot.end();
            if (rows == loot.end())
                continue;
            for (auto const& [item, chance] : rows->second)
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item);
                if (!proto || items.count(item))
                    continue;
                ItemFacts i;
                i.entry = item;
                i.itemClass = uint8(proto->Class);
                i.subClass = uint8(proto->SubClass);
                i.quality = uint8(proto->Quality);
                i.bonding = uint8(proto->Bonding);
                i.stackable = proto->Stackable;
                i.maxCount = proto->MaxCount;
                i.startsQuest = proto->StartQuest != 0;
                if (!IsCollectItem(i) || !proto->Name1)
                    continue;
                items.insert(item);
                pool.collects.push_back({item, proto->Name1, k.entry, k.name, k.level, chance});
            }
        }
        std::sort(pool.collects.begin(), pool.collects.end(), [](CollectCandidate const& a, CollectCandidate const& b) {
            return a.item < b.item;
        });
        std::sort(pool.hunts.begin(), pool.hunts.end(), [](HuntCandidate const& a, HuntCandidate const& b) {
            return a.entry < b.entry;
        });
        pool.camps = FindCamps(pool.kills, points[zone]);
        for (Camp& c : pool.camps)
            c.place = AreaName(c.area);
    }
}

void BountyMgr::MakeQuests()
{
    m_quests.clear();
    for (uint32 block = 0; block < kBlocks; ++block)
        for (auto const& [zone, z] : m_zones)
            for (uint32 i = 0; i < kPerZone; ++i)
            {
                uint32 const id = kQuestBase + block * kBlockSize + z.slot * kPerZone + i;
                auto q = std::make_unique<Quest>();
                q->QuestId = id;
                Quest* made = sObjectMgr.AddGeneratedQuest(std::move(q));
                Fill(made, nullptr);
                m_quests[id] = made;
            }
}

uint32 BountyMgr::SaltOf(uint32 day) const
{
    auto it = m_salts.find(day);
    return it == m_salts.end() ? 0 : it->second;
}

void BountyMgr::PostDay(uint32 day)
{
    uint32 const block = BlockOf(day);
    for (auto const& [zone, z] : m_zones)
    {
        std::vector<Bounty> posted = Post(m_pools[zone], z.slot, day, SaltOf(day));
        for (uint32 i = 0; i < kPerZone; ++i)
        {
            uint32 const id = kQuestBase + block * kBlockSize + z.slot * kPerZone + i;
            if (i < posted.size())
            {
                Posted& p = m_posted[id];
                p.bounty = posted[i];
                p.day = day;
            }
            else
                m_posted.erase(id);
            auto q = m_quests.find(id);
            if (q != m_quests.end())
            {
                auto p = m_posted.find(id);
                Fill(q->second, p == m_posted.end() ? nullptr : &p->second);
            }
        }
    }
    ++m_generation;
}

void BountyMgr::Fill(Quest* q, Posted const* p) const
{
    uint32 const id = q->QuestId;
    *q = Quest();                       // every objective, reward and count zero
    q->QuestId = id;
    q->QuestMethod = 2;
    q->m_SpecialFlags = QUEST_SPECIAL_FLAG_REPEATABLE;
    if (!p)
    {
        // A slot with nothing posted: a quest no one can take, named for whoever still sees it.
        q->Title = "Withdrawn Bounty";
        q->Objectives = "This bounty is no longer posted.";
        q->Details = q->Objectives;
        q->m_isActive = false;
        return;
    }
    Bounty const& b = p->bounty;
    q->ZoneOrSort = int32(b.zone);
    q->MinLevel = b.minLevel;
    q->QuestLevel = b.level;
    q->Type = b.elite ? QUEST_TYPE_ELITE : 0;
    q->Title = b.title;
    q->Details = b.details;
    q->Objectives = b.objectives;
    q->OfferRewardText = "The board's clerk checks your proof and counts out the purse.";
    q->RequestItemsText = "Bring the proof to the board.";
    q->RewXP = b.reward.xp;
    q->RewOrReqMoney = int32(b.reward.money);
    q->RewMoneyMaxLevel = b.reward.moneyMaxLevel;
    for (size_t i = 0; i < b.kills.size() && i < size_t(QUEST_OBJECTIVES_COUNT); ++i)
    {
        q->ReqCreatureOrGOId[i] = int32(b.kills[i].creature);
        q->ReqCreatureOrGOCount[i] = b.kills[i].count;
        ++q->m_reqCreatureOrGOcount;
    }
    if (b.item)
    {
        q->ReqItemId[0] = b.item;
        q->ReqItemCount[0] = b.itemCount;
        q->m_reqitemscount = 1;
        q->SetSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER);
    }
    if (!b.kills.empty())
        q->SetSpecialFlag(QuestSpecialFlags(QUEST_SPECIAL_FLAG_KILL_OR_CAST | QUEST_SPECIAL_FLAG_SPEAKTO));
    q->m_isActive = true;
}

// ── The day ─────────────────────────────────────────────────────────────────────────────────────

void BountyMgr::Tick()
{
    int64 const now = UnixNow();
    LocalTime const lt = LocalOf(now);
    m_today = DayOf(lt, m_resetHour);
    m_nextReset = now + SecondsToReset(lt, m_resetHour);
}

void BountyMgr::Update()
{
    if (!m_enabled || m_zones.empty())
        return;
    if (UnixNow() >= m_nextReset)
    {
        uint32 const was = m_today;
        Tick();
        if (m_today != was)
        {
            PostDay(m_today);
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bounty boards: day %u posted.", uint32(m_today));
        }
    }
    if (m_refreshAsked.exchange(false))
    {
        uint32 const day = m_today;
        uint32 const salt = SaltOf(day) + 1;
        m_salts[day] = salt;
        CharacterDatabase.PExecute("REPLACE INTO `aw_bounty_day` (`day`, `salt`) VALUES (%u, %u)", day, salt);
        PostDay(day);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bounty boards: day %u re-rolled by a GM (%u).", day, salt);
    }
}

// ── A character's bounties ──────────────────────────────────────────────────────────────────────

BountyMgr::Posted const* BountyMgr::PostedOf(uint32 questId) const
{
    auto it = m_posted.find(questId);
    return it == m_posted.end() ? nullptr : &it->second;
}

FusionBountyRow* BountyMgr::RowOf(Player* player, uint32 questId, Posted const& p) const
{
    for (FusionBountyRow& r : player->GetFusion().bounty.rows)
        if (r.quest == questId && r.day == p.day && r.key == p.bounty.key)
            return &r;
    return nullptr;
}

uint32 BountyMgr::DoneToday(Player* player) const
{
    uint32 n = 0;
    uint32 const today = m_today;
    for (FusionBountyRow const& r : player->GetFusion().bounty.rows)
        n += r.doneDay == today ? 1 : 0;
    return n;
}

uint32 BountyMgr::HeldCount(Player* player) const
{
    uint32 n = 0;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        n += IsBountyQuest(SlotQuest(player, slot)) ? 1 : 0;
    return n;
}

void BountyMgr::LoadRows(Player* player)
{
    FusionBountyData& d = player->GetFusion().bounty;
    d.loaded = true;
    d.rows.clear();
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery(
        "SELECT `quest`, `day`, `qkey`, `done_day` FROM `aw_bounty_char` WHERE `guid` = %u", player->GetGUIDLow());
    if (!result)
        return;
    do
    {
        Field* f = result->Fetch();
        FusionBountyRow r;
        r.quest = f[0].GetUInt32();
        r.day = f[1].GetUInt32();
        r.key = f[2].GetUInt32();
        r.doneDay = f[3].GetUInt32();
        d.rows.push_back(r);
    } while (result->NextRow());
}

void BountyMgr::UpdatePlayer(Player* player, uint32 diffMs)
{
    if (!m_enabled || m_zones.empty() || !player->IsInWorld())
        return;
    FusionBountyData& d = player->GetFusion().bounty;
    if (d.checkMs > diffMs)
    {
        d.checkMs -= diffMs;
        return;
    }
    d.checkMs = kCheckEveryMs;
    if (!d.loaded)
        LoadRows(player);
    uint32 const gen = m_generation;
    if (d.checkedGeneration != gen)
    {
        d.checkedGeneration = gen;
        CheckLog(player);
    }
}

// A bounty in the log that is no longer the one posted (expired, re-rolled, or never taken at a
// board) is taken out, as an abandoned quest is.
void BountyMgr::CheckLog(Player* player)
{
    uint32 const today = m_today;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const id = SlotQuest(player, slot);
        if (!IsBountyQuest(id))
            continue;
        Posted const* p = PostedOf(id);
        FusionBountyRow const* r = p ? RowOf(player, id, *p) : nullptr;
        char const* why = nullptr;
        if (!p || !r)
            why = "was withdrawn from the boards";
        else if (IsExpired(r->day, today))
            why = "has expired";
        if (!why)
            continue;
        Quest const* q = sObjectMgr.GetQuestTemplate(id);
        std::string const title = p ? p->bounty.title : q ? q->GetTitle() : std::string("A bounty");
        player->RemoveQuestAtSlot(slot);
        ChatHandler(player).PSendSysMessage("|cffffd000%s|r %s.", title.c_str(), why);
    }
}

// ── The board ───────────────────────────────────────────────────────────────────────────────────

BountyMgr::BoardRow const* BountyMgr::BoardOf(GameObject const* go) const
{
    auto it = m_boards.find(go->GetGUIDLow());
    return it == m_boards.end() ? nullptr : &it->second;
}

bool BountyMgr::TeamAllows(BoardRow const& b, Player const* player) const
{
    if (b.team == kTeamAlliance)
        return player->GetTeam() == ALLIANCE;
    if (b.team == kTeamHorde)
        return player->GetTeam() == HORDE;
    return true;
}

GameObject* BountyMgr::NearBoard(Player* player) const
{
    FusionBountyData const& d = player->GetFusion().bounty;
    if (d.board.IsEmpty())
        return nullptr;
    GameObject* go = player->GetMap()->GetGameObject(d.board);
    if (!go || !go->isSpawned() || !player->IsWithinDistInMap(go, kBoardReach, true, SizeFactor::None))
        return nullptr;
    return go;
}

bool BountyMgr::UseBoard(Player* player, GameObject* go)
{
    if (!m_enabled)
        return false;
    BoardRow const* b = BoardOf(go);
    if (!b)
    {
        ChatHandler(player).SendSysMessage("This board has nothing posted.");
        return true;
    }
    FusionBountyData& d = player->GetFusion().bounty;
    if (!d.loaded)
        LoadRows(player);
    d.board = go->GetObjectGuid();
    if (!TeamAllows(*b, player))
    {
        SendResult(player, false, RefusalText(Refusal::WrongTeam));
        return true;
    }
    SendBoard(player, go, true);
    return true;
}

void BountyMgr::SendResult(Player* player, bool ok, std::string const& text)
{
    sFusionMgr.SendChannel(player, kChannelBounty, EncodeResult(ok, text));
}

void BountyMgr::SendBoard(Player* player, GameObject* go, bool open)
{
    BoardRow const* b = BoardOf(go);
    if (!b)
        return;
    uint32 const today = m_today;
    uint32 const done = DoneToday(player);
    uint32 const held = HeldCount(player);
    Board w;
    w.open = open;
    w.zone = b->zone;
    w.town = b->town;
    w.zoneName = AreaName(b->zone);
    w.done = uint8(std::min<uint32>(done, 255));
    w.cap = uint8(kDailyCap);
    w.level = uint8(std::min<uint32>(player->GetLevel(), 255));
    w.secondsToReset = uint32(std::max<int64>(0, m_nextReset - UnixNow()));
    FactionEntry const* rep = b->repFaction ? sObjectMgr.GetFactionEntry(b->repFaction) : nullptr;

    auto rowFor = [&](uint32 id, Posted const& p) {
        Bounty const& x = p.bounty;
        Row r;
        r.questId = id;
        r.kind = x.kind;
        r.level = x.level;
        r.minLevel = x.minLevel;
        r.elite = x.elite;
        r.xp = x.reward.xp;
        r.money = x.reward.money;
        if (rep)
        {
            r.reputation = uint16(x.reward.reputation);
            r.repName = rep->name[0];
        }
        r.title = x.title;
        r.objective = x.objective;
        r.details = x.details;
        Quest const* q = sObjectMgr.GetQuestTemplate(id);
        FusionBountyRow const* row = RowOf(player, id, p);
        bool const inLog = InLog(player, id) && row;
        if (row && row->doneDay)
            r.state = RowState::Done;
        else if (inLog && x.zone != b->zone)
            r.state = RowState::Elsewhere;
        else if (inLog && q && player->CanRewardQuest(q, false))
            r.state = RowState::Ready;
        else if (inLog)
            r.state = RowState::Accepted;
        else if (player->GetLevel() < x.minLevel)
            r.state = RowState::Locked;
        else if (done + held >= kDailyCap)
            r.state = RowState::Full;
        else
            r.state = RowState::Available;
        if (inLog)
        {
            QuestStatusData const* s = player->GetQuestStatusData(id);
            for (size_t i = 0; i < x.kills.size() && i < size_t(QUEST_OBJECTIVES_COUNT); ++i)
                r.progress.push_back({uint8(std::min<uint32>(s ? s->m_creatureOrGOcount[i] : 0, x.kills[i].count)),
                    x.kills[i].count, x.kills[i].name});
            if (x.item)
                r.progress.push_back({uint8(std::min<uint32>(player->GetItemCount(x.item), x.itemCount)), x.itemCount,
                    x.itemName});
        }
        return r;
    };

    // Today's bounties for this zone, in posting order.
    std::vector<std::pair<uint32, Posted const*>> today_;
    std::vector<std::pair<uint32, Posted const*>> others;
    for (auto const& [id, p] : m_posted)
    {
        bool const inLog = InLog(player, id);
        if (p.day == today && p.bounty.zone == b->zone)
            today_.emplace_back(id, &p);
        // Still held from yesterday, or another zone's: shown so the player knows where it goes.
        else if (inLog && !IsExpired(p.day, today) && RowOf(player, id, p))
            others.emplace_back(id, &p);
    }
    std::sort(today_.begin(), today_.end());
    std::sort(others.begin(), others.end());
    for (auto const& [id, p] : today_)
        w.rows.push_back(rowFor(id, *p));
    for (auto const& [id, p] : others)
        w.rows.push_back(rowFor(id, *p));
    sFusionMgr.SendChannel(player, kChannelBounty, EncodeBoard(w));
}

void BountyMgr::HandleChannel(Player* player, std::vector<uint8> const& body)
{
    if (!m_enabled || !player->IsInWorld())
        return;
    uint8 op = 0;
    uint32 quest = 0;
    if (!DecodeRequest(body, op, quest))
        return;
    FusionBountyData& d = player->GetFusion().bounty;
    if (!d.loaded)
        LoadRows(player);
    if (op == kOpAsk)
    {
        if (GameObject* go = NearBoard(player))
            SendBoard(player, go, false);
        return;
    }
    if (op == kOpAccept)
        Accept(player, quest);
    else if (op == kOpTurnIn)
        TurnIn(player, quest);
}

void BountyMgr::Accept(Player* player, uint32 questId)
{
    GameObject* go = NearBoard(player);
    BoardRow const* b = go ? BoardOf(go) : nullptr;
    Posted const* p = PostedOf(questId);
    Quest const* q = sObjectMgr.GetQuestTemplate(questId);
    uint32 const today = m_today;
    AcceptCheck c;
    c.nearBoard = b != nullptr;
    c.teamAllowed = b && TeamAllows(*b, player);
    c.posted = b && p && q && q->IsActive() && p->day == today && p->bounty.zone == b->zone;
    c.playerLevel = uint8(std::min<uint32>(player->GetLevel(), 255));
    c.minLevel = p ? p->bounty.minLevel : 1;
    c.inLog = InLog(player, questId);
    FusionBountyRow* row = p ? RowOf(player, questId, *p) : nullptr;
    c.doneToday = row && row->doneDay != 0;
    c.doneCount = DoneToday(player);
    c.heldCount = HeldCount(player);
    c.logFull = !player->SatisfyQuestLog(false);
    Refusal const why = CanAccept(c);
    if (why != Refusal::None)
    {
        SendResult(player, false, RefusalText(why));
        if (go)
            SendBoard(player, go, false);
        return;
    }
    if (!row)
    {
        FusionBountyRow r;
        r.quest = questId;
        r.day = p->day;
        r.key = p->bounty.key;
        player->GetFusion().bounty.rows.push_back(r);
        CharacterDatabase.PExecute("INSERT IGNORE INTO `aw_bounty_char` (`guid`, `quest`, `day`, `qkey`, `done_day`) "
            "VALUES (%u, %u, %u, %u, 0)", player->GetGUIDLow(), questId, p->day, p->bounty.key);
    }
    player->AddQuest(q, go);
    // A collect bounty with the items already in the bags is done at once, as at a quest giver.
    if (player->CanCompleteQuest(questId))
        player->CompleteQuest(questId);
    SendResult(player, true, "Bounty taken: " + p->bounty.title);
    SendBoard(player, go, false);
}

void BountyMgr::TurnIn(Player* player, uint32 questId)
{
    GameObject* go = NearBoard(player);
    BoardRow const* b = go ? BoardOf(go) : nullptr;
    Posted const* p = PostedOf(questId);
    Quest const* q = sObjectMgr.GetQuestTemplate(questId);
    FusionBountyRow* row = p ? RowOf(player, questId, *p) : nullptr;
    uint32 const today = m_today;
    TurnInCheck c;
    c.nearBoard = b != nullptr;
    c.teamAllowed = b && TeamAllows(*b, player);
    c.held = p && q && row && !row->doneDay && InLog(player, questId);
    c.expired = row && IsExpired(row->day, today);
    c.sameZone = b && p && p->bounty.zone == b->zone;
    c.complete = c.held && player->CanRewardQuest(q, false);
    c.doneCount = DoneToday(player);
    Refusal const why = CanTurnIn(c);
    if (why != Refusal::None)
    {
        SendResult(player, false, RefusalText(why));
        if (go)
            SendBoard(player, go, false);
        return;
    }
    uint32 const xpBefore = player->GetUInt32Value(PLAYER_XP);
    uint32 const levelBefore = player->GetLevel();
    uint32 const moneyBefore = player->GetMoney();
    player->RewardQuest(q, 0, go, true);
    if (b->repFaction && p->bounty.reward.reputation > 0)
        if (FactionEntry const* f = sObjectMgr.GetFactionEntry(b->repFaction))
        {
            int32 const rep = player->CalculateReputationGain(REPUTATION_SOURCE_QUEST, p->bounty.reward.reputation,
                int32(b->repFaction), p->bounty.level);
            player->GetReputationMgr().ModifyReputation(f, rep);
        }
    row->doneDay = today;
    CharacterDatabase.PExecute("UPDATE `aw_bounty_char` SET `done_day` = %u WHERE `guid` = %u AND `quest` = %u AND `day` = %u "
        "AND `qkey` = %u", today, player->GetGUIDLow(), questId, row->day, row->key);
    sFusionMgr.CountStat(player, {Fusion::Stat::BountiesCompleted, Fusion::kAnyKey});

    std::ostringstream said;
    said << "Bounty collected: " << p->bounty.title << ".";
    uint32 const money = player->GetMoney() > moneyBefore ? player->GetMoney() - moneyBefore : 0;
    if (money)
        said << " " << Money(money) << ".";
    if (player->GetLevel() == levelBefore && player->GetUInt32Value(PLAYER_XP) > xpBefore)
        said << " " << (player->GetUInt32Value(PLAYER_XP) - xpBefore) << " experience.";
    SendResult(player, true, said.str());
    SendBoard(player, go, false);
}

// ── GM ──────────────────────────────────────────────────────────────────────────────────────────

std::vector<std::string> BountyMgr::Describe(uint32 zone) const
{
    std::vector<std::string> out;
    if (!m_enabled)
    {
        out.push_back("The bounty boards are off (Bounty.Enable).");
        return out;
    }
    uint32 const today = m_today;
    int64 const left = std::max<int64>(0, m_nextReset - UnixNow());
    std::ostringstream head;
    head << "Bounty day " << today << " (re-roll " << SaltOf(today) << "), next in " << left / 3600 << "h "
         << (left / 60) % 60 << "m; " << m_zones.size() << " zones, " << m_boards.size() << " boards.";
    out.push_back(head.str());
    auto z = m_zones.find(zone);
    if (z == m_zones.end())
    {
        out.push_back("This zone posts no bounties. Zones: ");
        std::string list;
        for (auto const& [id, row] : m_zones)
            list += (list.empty() ? "" : ", ") + row.name + " (" + std::to_string(id) + ")";
        out.back() += list;
        return out;
    }
    auto pool = m_pools.find(zone);
    if (pool != m_pools.end())
    {
        std::ostringstream s;
        s << z->second.name << " (" << uint32(z->second.band.minLevel) << "-" << uint32(z->second.band.maxLevel) << "): "
          << pool->second.kills.size() << " kill, " << pool->second.collects.size() << " collect, "
          << pool->second.hunts.size() << " hunt, " << pool->second.camps.size() << " camp targets.";
        out.push_back(s.str());
    }
    for (uint32 i = 0; i < kPerZone; ++i)
    {
        uint32 const id = QuestId(today, z->second.slot, i);
        Posted const* p = PostedOf(id);
        if (!p || p->day != today)
            continue;
        Bounty const& b = p->bounty;
        std::ostringstream s;
        s << id << " [" << KindName(b.kind) << ", level " << uint32(b.level) << (b.elite ? " elite" : "") << "] "
          << b.title << ": " << b.objective << " (" << b.reward.xp << " xp, " << Money(b.reward.money) << ")";
        out.push_back(s.str());
    }
    return out;
}

std::vector<BountyMgr::KillPosting> BountyMgr::KillPostings(uint32 zone) const
{
    std::vector<KillPosting> out;
    auto const z = m_zones.find(zone);
    if (!m_enabled || z == m_zones.end())
        return out;
    uint32 const today = m_today;
    for (uint32 i = 0; i < kPerZone; ++i)
    {
        Posted const* p = PostedOf(QuestId(today, z->second.slot, i));
        if (!p || p->day != today || p->bounty.kills.empty())
            continue;
        KillPosting k;
        k.questId = p->bounty.questId;
        k.level = p->bounty.level;
        k.elite = p->bounty.elite;
        for (Objective const& o : p->bounty.kills)
            k.kills.push_back({o.creature, o.count});
        k.title = p->bounty.title;
        out.push_back(std::move(k));
    }
    return out;
}

bool ChatHandler::HandleBountyCommand(char* args)
{
    std::istringstream in(args ? std::string(args) : std::string());
    std::string what;
    in >> what;
    if (what == "refresh")
    {
        sBountyMgr.AskRefresh();
        SendSysMessage("Today's bounties are re-rolled at the next world tick. Bounties taken from the old list are "
            "withdrawn from the quest logs.");
        return true;
    }
    if (what == "list" || what.empty())
    {
        uint32 zone = 0;
        std::string word;
        if (in >> word && word.find_first_not_of("0123456789") == std::string::npos && word.size() < 7)
            zone = uint32(std::stoul(word));
        else if (Player* p = GetSession() ? GetSession()->GetPlayer() : nullptr)
            zone = p->GetZoneId();
        for (std::string const& line : sBountyMgr.Describe(zone))
            SendSysMessage(line.c_str());
        return true;
    }
    SendSysMessage(".bounty list [zone id]: today's bounties for your zone (or the one given). "
        ".bounty refresh: re-roll today's bounties on every board.");
    return true;
}
