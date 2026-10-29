/*
 * World of Modern Warfare: Construction, woodcutting and plots (fusion/Construction.h, Plots.h)
 * in the game. Building becomes a secondary profession: its tier and skill are saved per
 * character, every piece costs its recipe's materials and may teach a point, lumber is cut from
 * the lumber nodes, and the homestead sites' plots (the Hyjal summit, Azshara Crater, the
 * Thalassian plateau) are plots a player buys, keeps with a weekly upkeep and nobody else may touch. FusionMgr.cpp calls in here at its decision points (placing,
 * taking down, doors, damage, raids); the trainer, steward (the plot coordinator), courier and
 * lumber node scripts (scripts/custom/custom_fusion.cpp) call the gossip and use hooks, and the
 * NPCs open the client's Construction window (fusion/Homestead.h) instead of gossip menus. See
 * src/fusion/docs/CONSTRUCTION.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "Group.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GridMap.h"
#include "Item.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Util.h"
#include "Utilities/Random.h"
#include "WorldSession.h"

#include <algorithm>
#include <cinttypes>
#include <ctime>
#include <sstream>

namespace
{
    using namespace Fusion::Construction;
    namespace Plots = Fusion::Plots;

    // The NPCs (03_construction.sql).
    constexpr uint32 kTrainerEntry = 91001;
    constexpr uint32 kStewardEntry = 91003;
    constexpr uint32 kCourierEntry = 91004;

    // A ghost this near a site's bounds rises at the site's nearest stop. (Within
    // Homestead::kHomesteadsNear a courier offers the way back and a steward sells that site's
    // plots.)
    constexpr float kSiteGhostNear = 50.f;
    // The upkeep sweep's period.
    constexpr int64 kSweepSeconds = 3600;
    // An anvil (spell focus 1) for the smiths' parts, as near as vanilla's own recipes ask.
    constexpr uint32 kAnvilFocus = 1;
    constexpr float kAnvilReach = 10.f;
    // A destroyed piece's salvage goes to the group members within this far of whoever broke it.
    constexpr float kSalvageShareRange = 100.f;

    int64 Now() { return int64(time(nullptr)); }

    std::string Money(uint32 copper)
    {
        std::ostringstream o;
        if (copper >= 10000)
            o << copper / 10000 << "g";
        if (copper % 10000 >= 100)
            o << (o.tellp() > 0 ? " " : "") << (copper % 10000) / 100 << "s";
        if (copper % 100 || copper == 0)
            o << (o.tellp() > 0 ? " " : "") << copper % 100 << "c";
        return o.str();
    }

    std::string ItemName(uint32 item)
    {
        if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item))
            if (proto->Name1)
                return proto->Name1;
        std::ostringstream o;
        o << "item " << item;
        return o.str();
    }

    std::string MaterialList(std::vector<Material> const& list)
    {
        std::ostringstream o;
        for (size_t i = 0; i < list.size(); ++i)
            o << (i ? ", " : "") << list[i].count << " " << ItemName(list[i].item);
        return o.str();
    }

    char const* SizeName(Plots::Size s)
    {
        switch (s)
        {
            case Plots::Size::Small: return "Small";
            case Plots::Size::Large: return "Large";
            default: return "Standard";
        }
    }

    char const* SkillLineName(uint16 line)
    {
        switch (line)
        {
            case kSkillBlacksmithing: return "Blacksmithing";
            case kSkillEngineering: return "Engineering";
            default: return "Construction";
        }
    }

    // The bag counts of just the items a list asks for, and of their salvaged items
    // (Construction.h: salvaged goods build; the trades' own checks ignore them).
    Inventory BagsFor(Player const* player, std::vector<Material> const& list)
    {
        Inventory bags;
        for (uint32 item : ItemsToCount(list))
            bags[item] = player->GetItemCount(item);
        return bags;
    }

    // Put up to `count` of an item in the bags: how many went in (the rest has no room).
    uint32 GiveItems(Player* player, uint32 item, uint32 count)
    {
        if (!count || !sObjectMgr.GetItemPrototype(item))
            return 0;
        ItemPosCountVec dest;
        uint32 noSpace = 0;
        uint32 fits = count;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item, count, &noSpace) != EQUIP_ERR_OK)
        {
            if (noSpace >= count)
                return 0;
            fits = count - noSpace;
            dest.clear();
            if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item, fits) != EQUIP_ERR_OK)
                return 0;
        }
        Item* stored = player->StoreNewItem(dest, item, true, Item::GenerateItemRandomPropertyId(item));
        if (!stored)
            return 0;
        player->SendNewItem(stored, fits, true, false);
        return fits;
    }

    // "Small homestead 301 in Azshara Crater".
    std::string PlotName(uint32 id)
    {
        Plots::District const* d = sFusionMgr.PlotGrid().DistrictOf(id);
        Plots::Site const* s = sFusionMgr.PlotGrid().SiteOf(id);
        std::ostringstream o;
        o << SizeName(d ? d->size : Plots::Size::Standard) << " homestead " << id;
        if (s && !s->where.empty())
            o << " " << s->where;
        return o.str();
    }

    void TeleportToSpot(Player* player, uint32 map, Plots::Spot const& at)
    {
        player->TeleportTo(map, at.x, at.y, at.z, at.o);
    }

    std::string PlotBounds(uint32 id)
    {
        std::optional<Plots::Rect> const b = sFusionMgr.PlotGrid().Bounds(id);
        if (!b)
            return std::string();
        char text[96];
        snprintf(text, sizeof(text), "x %.0f to %.0f, y %.0f to %.0f", b->x0, b->x1, b->y0, b->y1);
        return text;
    }

    std::string CharacterName(uint32 guid)
    {
        std::string name;
        if (!sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, guid), name) || name.empty())
        {
            std::ostringstream o;
            o << "character " << guid;
            name = o.str();
        }
        return name;
    }

    bool NearAnvil(Player* player)
    {
        GameObject* found = nullptr;
        MaNGOS::GameObjectFocusCheck check(player, kAnvilFocus);
        MaNGOS::GameObjectSearcher<MaNGOS::GameObjectFocusCheck> searcher(found, check);
        Cell::VisitGridObjects(player, searcher, kAnvilReach);
        // An anvil built as furniture serves as well (fusion/Furniture.h).
        return found != nullptr || sFusionMgr.FurnitureFocusNear(player, kAnvilFocus);
    }
}

// ── Loading ─────────────────────────────────────────────────────────────────────────────────

void FusionMgr::LoadPlotGrid()
{
    m_plotGrid = Plots::Default();
    auto has = [](char const* table) {
        std::unique_ptr<QueryResult> r = WorldDatabase.PQuery("SHOW TABLES LIKE '%s'", table);
        return r != nullptr;
    };
    if (!has("fusion_plot_site") || !has("fusion_plot_site_stop") || !has("fusion_plot_district"))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: no fusion_plot_* tables (sql 12): the built-in homestead sites are used.");
        return;
    }
    std::vector<Plots::Site> sites;
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(
            "SELECT `id`, `name`, `place`, `map`, `gate_x`, `gate_y`, `gate_z`, `gate_o`, "
            "`min_x`, `min_y`, `max_x`, `max_y`, `floor_z`, `ceiling_z` FROM `fusion_plot_site` ORDER BY `id`"))
    {
        do
        {
            Field* f = r->Fetch();
            Plots::Site s;
            s.id = f[0].GetUInt8();
            s.name = f[1].GetCppString();
            s.where = f[2].GetCppString();
            s.map = f[3].GetUInt32();
            s.gate = {f[4].GetFloat(), f[5].GetFloat(), f[6].GetFloat(), f[7].GetFloat()};
            s.bounds = {f[8].GetFloat(), f[9].GetFloat(), f[10].GetFloat(), f[11].GetFloat()};
            s.floorZ = f[12].GetFloat();
            s.ceilingZ = f[13].GetFloat();
            sites.push_back(std::move(s));
        } while (r->NextRow());
    }
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(
            "SELECT `site`, `name`, `x`, `y`, `z`, `o` FROM `fusion_plot_site_stop` ORDER BY `site`, `idx`"))
    {
        do
        {
            Field* f = r->Fetch();
            uint8 const site = f[0].GetUInt8();
            for (Plots::Site& s : sites)
                if (s.id == site)
                    s.stops.push_back({f[1].GetCppString(), {f[2].GetFloat(), f[3].GetFloat(), f[4].GetFloat(), f[5].GetFloat()}});
        } while (r->NextRow());
    }
    std::vector<Plots::District> districts;
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(
            "SELECT `first_id`, `site`, `size`, `map`, `origin_x`, `origin_y`, `cell`, `cols`, `rows`, `price`, `upkeep`, `deed_item` "
            "FROM `fusion_plot_district` ORDER BY `first_id`"))
    {
        do
        {
            Field* f = r->Fetch();
            Plots::District d;
            d.firstId = f[0].GetUInt32();
            d.site = f[1].GetUInt8();
            uint8 const size = f[2].GetUInt8();
            d.size = size == 1 ? Plots::Size::Small : size == 3 ? Plots::Size::Large : Plots::Size::Standard;
            d.map = f[3].GetUInt32();
            d.originX = f[4].GetFloat();
            d.originY = f[5].GetFloat();
            d.cell = f[6].GetFloat();
            d.cols = f[7].GetUInt16();
            d.rows = f[8].GetUInt16();
            d.priceCopper = f[9].GetUInt32();
            d.upkeepCopper = f[10].GetUInt32();
            d.deedItem = f[11].GetUInt32();
            districts.push_back(d);
        } while (r->NextRow());
    }
    if (sites.empty() || districts.empty())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: fusion_plot_* tables are empty: the built-in homestead sites are used.");
        return;
    }
    Plots::Grid grid(std::move(districts), std::move(sites));
    std::vector<std::string> const problems = grid.Problems();
    if (!problems.empty())
    {
        for (std::string const& p : problems)
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: fusion_plot_* tables: %s.", p.c_str());
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: the homestead tables are unsound: the built-in sites are used instead.");
        return;
    }
    // A plot id the built-in layout sells that the table dropped would strand its owner: say so.
    for (Plots::District const& d : Plots::Default().Districts())
        for (uint32 id = d.firstId; id < d.firstId + d.Count(); ++id)
            if (!grid.DistrictOf(id))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: fusion_plot_district has no plot %u (built in): an owner of it loses it.", id);
                break;
            }
    m_plotGrid = std::move(grid);
}

void FusionMgr::LoadConstruction()
{
    LoadPlotGrid();
    {
        uint32 count = 0;
        for (Plots::Site const& s : PlotGrid().Sites())
        {
            uint32 n = 0;
            for (Plots::District const& d : PlotGrid().Districts())
                if (d.site == s.id)
                    n += d.Count();
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: homestead site %u, %s (map %u): %u plots.", uint32(s.id), s.name.c_str(), s.map, n);
            count += n;
        }
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u homestead plots in %u sites.", count, uint32(PlotGrid().Sites().size()));
    }
    // The characters tables (docs/CONSTRUCTION.md), made here as no migration file may be.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_skill` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`skill` smallint(5) unsigned NOT NULL COMMENT '9001 Construction',"
        "`tier` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT 'Construction::Tier',"
        "`value` smallint(5) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`guid`, `skill`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_construction_kit` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`piece` tinyint(3) unsigned NOT NULL COMMENT 'Build::PieceType',"
        "`count` smallint(5) unsigned NOT NULL DEFAULT '0' COMMENT 'kits made and not placed',"
        "PRIMARY KEY (`guid`, `piece`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_plot` ("
        "`id` int(10) unsigned NOT NULL COMMENT 'Plots.h plot id',"
        "`account` int(10) unsigned NOT NULL,"
        "`owner` int(10) unsigned NOT NULL COMMENT 'the buying character',"
        "`bought_at` bigint(20) NOT NULL,"
        "`paid_until` bigint(20) NOT NULL,"
        "PRIMARY KEY (`id`), UNIQUE KEY `account` (`account`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_plot_permit` ("
        "`plot` int(10) unsigned NOT NULL,"
        "`guid` int(10) unsigned NOT NULL,"
        "`level` tinyint(3) unsigned NOT NULL COMMENT '1 guest, 2 builder',"
        "PRIMARY KEY (`plot`, `guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_reclaim` ("
        "`guid` int(10) unsigned NOT NULL,"
        "`item` int(10) unsigned NOT NULL,"
        "`count` int(10) unsigned NOT NULL,"
        "PRIMARY KEY (`guid`, `item`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");

    uint32 plots = 0;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        m_plots = Plots::Registry();
        if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query(
                "SELECT `id`, `account`, `owner`, `bought_at`, `paid_until` FROM `character_fusion_plot`"))
        {
            do
            {
                Field* f = r->Fetch();
                Plots::Plot p;
                p.id = f[0].GetUInt32();
                // A plot the grid no longer has (its district moved or shrank) is dropped.
                if (!PlotGrid().DistrictOf(p.id))
                    continue;
                p.ownerAccount = f[1].GetUInt32();
                p.ownerGuid = f[2].GetUInt32();
                p.boughtAt = f[3].GetInt64();
                p.paidUntil = f[4].GetInt64();
                m_plots.plots[p.id] = p;
                ++plots;
            } while (r->NextRow());
        }
        if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query(
                "SELECT `plot`, `guid`, `level` FROM `character_fusion_plot_permit`"))
        {
            do
            {
                Field* f = r->Fetch();
                uint8 const level = f[2].GetUInt8();
                if (Plots::Plot* p = m_plots.Find(f[0].GetUInt32());
                    p && (level == uint8(Plots::PermitLevel::Guest) || level == uint8(Plots::PermitLevel::Builder)))
                    Plots::Grant(*p, f[1].GetUInt32(), Plots::PermitLevel(level));
            } while (r->NextRow());
        }
        // One plot to a player: rows beyond one an account (from before the limit, or written by
        // hand) are given up and packed, the earliest bought kept.
        for (uint32 id : Fusion::Homestead::Excess(m_plots))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: plot %u is account %u's second; it is given up and packed.",
                id, m_plots.Find(id)->ownerAccount);
            m_plots.plots.erase(id);
            m_plotsToPack.push_back(id);
            --plots;
        }
    }

    // The smiths' and engineers' parts (world DB), reagents with a count only.
    m_crafts.clear();
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(
            "SELECT `id`, `name`, `skill_line`, `required`, `yellow`, `green`, `gray`, `product`, `product_count`, "
            "`reagent1`, `count1`, `reagent2`, `count2`, `reagent3`, `count3`, `reagent4`, `count4`, `needs_anvil` "
            "FROM `fusion_recipe` ORDER BY `id`"))
    {
        do
        {
            Field* f = r->Fetch();
            Craft c;
            c.recipe.id = f[0].GetUInt32();
            c.name = f[1].GetCppString();
            c.recipe.skillLine = f[2].GetUInt16();
            c.recipe.skill = {f[3].GetUInt16(), f[4].GetUInt16(), f[5].GetUInt16(), f[6].GetUInt16()};
            c.recipe.product = f[7].GetUInt32();
            c.recipe.productCount = std::max<uint32>(1, f[8].GetUInt32());
            for (int k = 0; k < 4; ++k)
                if (uint32 const item = f[9 + 2 * k].GetUInt32(), count = f[10 + 2 * k].GetUInt32(); item && count)
                    c.recipe.reagents.push_back({item, count});
            c.needsAnvil = f[17].GetUInt8() != 0;
            if (!sObjectMgr.GetItemPrototype(c.recipe.product))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: fusion_recipe %u makes item %u, which does not exist.", c.recipe.id, c.recipe.product);
                continue;
            }
            m_crafts.push_back(std::move(c));
        } while (r->NextRow());
    }
    // Only so many fit the trainer's window (a line is kept for training the next tier).
    if (m_crafts.size() > Fusion::Homestead::kMaxServices - 1)
        m_crafts.resize(Fusion::Homestead::kMaxServices - 1);

    LoadFurniture();
    m_recipesPacket = EncodeRecipes(Recipes());
    m_reinforcementsPacket = EncodeReinforcements();
    m_nextPlotSweep = Now() + kSweepSeconds;
    uint32 const lapsed = SweepPlots(nullptr);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: Construction: %u piece recipes, %u part recipes, %u plots owned (%u lapsed and packed).",
        uint32(Recipes().size()), uint32(m_crafts.size()), plots - std::min(plots, lapsed), lapsed);
}

void FusionMgr::LoadConstructionSkill(Player* player)
{
    if (!m_enabled)
        return;
    FusionPlayerData& d = player->GetFusion();
    d.constructionTier = Tier::None;
    d.constructionSkill = 0;
    d.constructionKits.clear();
    d.sentConstruction = false;
    d.plotHere = 0xFFFFFFFF;
    d.homesteadNpc.Clear();
    d.homesteadRole = 0;
    d.nearPlotsSent.clear();
    d.nearPlotsMap = 0xFFFFFFFF;
    d.nearPlotsGeneration = 0;
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `tier`, `value` FROM `character_fusion_skill` WHERE `guid` = %u AND `skill` = %u",
            player->GetGUIDLow(), uint32(kSkillConstruction)))
    {
        Field* f = r->Fetch();
        uint8 const tier = f[0].GetUInt8();
        if (tier <= uint8(Tier::Artisan))
        {
            d.constructionTier = Tier(tier);
            d.constructionSkill = std::min<uint16>(f[1].GetUInt16(), Cap(d.constructionTier));
        }
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `piece`, `count` FROM `character_fusion_construction_kit` WHERE `guid` = %u", player->GetGUIDLow()))
    {
        do
        {
            Field* f = r->Fetch();
            AddKits(d.constructionKits, Fusion::Build::PieceType(f[0].GetUInt8()), f[1].GetUInt32());
        } while (r->NextRow());
    }
}

// ── Every second ────────────────────────────────────────────────────────────────────────────

void FusionMgr::UpdateConstruction(Player* player, uint32 /*now*/)
{
    FusionPlayerData& d = player->GetFusion();
    int64 const t = Now();
    if (!d.sentConstruction)
    {
        d.sentConstruction = true;
        if (!m_recipesPacket.empty())
            SendChannel(player, Fusion::Packets::kChannelBuild, m_recipesPacket);
        if (!m_reinforcementsPacket.empty())
            SendChannel(player, Fusion::Packets::kChannelWorld, m_reinforcementsPacket);
        SendChannel(player, Fusion::Packets::kChannelBuild, EncodeSkill(d.constructionTier, d.constructionSkill));
        SendChannel(player, kChannelProfessions, EncodeKits(d.constructionKits));
        // The homestead's upkeep, once at login and only when already overdue (the window shows
        // how long is left otherwise).
        Plots::Plot owned;
        bool has = false;
        {
            std::lock_guard<std::mutex> lock(m_plotLock);
            if (Plots::Plot const* p = m_plots.OwnedBy(player->GetSession()->GetAccountId()))
            {
                owned = *p;
                has = true;
            }
        }
        if (has)
        {
            Plots::Standing const s = Plots::StandingOf(owned, t);
            if (s == Plots::Standing::Overdue)
                ChatHandler(player).PSendSysMessage("Your %s's upkeep is overdue: nothing new may go up there, and in %s it goes back on sale "
                    "(what stands there is packed for you at the Plot Steward).", PlotName(owned.id).c_str(),
                    secsToTimeString(time_t(owned.paidUntil + int64(Plots::kGraceWeeks) * Plots::kWeek - t)).c_str());
        }
    }

    // The plot underfoot: told when it changes, and again when anything about plots changed.
    uint32 const here = PlotGrid().PlotAt(player->GetMapId(), player->GetPositionX(), player->GetPositionY());
    if (here != d.plotHere || (here && d.plotGeneration != m_plotGeneration.load()))
        SendPlotHere(player, here);
    if (here)
        d.plotVisited = here;
    // The claimed plots near, for the client's markers.
    SendNearPlots(player, false);

    // The upkeep sweep, by the first player anywhere once an hour; a lapsed plot whose map is
    // loaded waits for a player on that map, so its pieces leave the world in that map's update.
    int64_t next = m_nextPlotSweep.load();
    if ((t >= next && m_nextPlotSweep.compare_exchange_strong(next, t + kSweepSeconds)) || m_plotsPending.load())
        SweepPlots(player->GetMap());
    // Decay (fusion/Maintenance.h): this map's pieces brought up to now, once an hour, by the first
    // player here (in this map's own update, so collapses leave the world for those who see them).
    if (Map* map = player->GetMap())
    {
        bool due = false;
        {
            std::lock_guard<std::mutex> lock(m_buildLock);
            int64& at = m_decayNext[map->GetId()];
            if (t >= at && m_pieces.count(map->GetId()))
            {
                at = t + Fusion::Maintenance::kDecaySweepSeconds;
                due = true;
            }
        }
        if (due && !map->Instanceable())
            DecayPieces(map);
    }
}

void FusionMgr::SendPlotHere(Player* player, uint32 plotId)
{
    FusionPlayerData& d = player->GetFusion();
    d.plotHere = plotId;
    d.plotGeneration = m_plotGeneration.load();
    std::vector<uint8_t> body;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        body = Plots::EncodePlot(PlotGrid(), m_plots, plotId, PlotActor(player), Now());
    }
    SendChannel(player, Fusion::Packets::kChannelBuild, body);
}

void FusionMgr::SetConstruction(Player* player, Tier tier, uint16 skill, bool announce)
{
    FusionPlayerData& d = player->GetFusion();
    skill = std::min<uint16>(skill, Cap(tier));
    bool const up = announce && skill > d.constructionSkill;
    d.constructionTier = tier;
    d.constructionSkill = skill;
    CharacterDatabase.PExecute("REPLACE INTO `character_fusion_skill` (`guid`, `skill`, `tier`, `value`) VALUES (%u, %u, %u, %u)",
        player->GetGUIDLow(), uint32(kSkillConstruction), uint32(tier), uint32(skill));
    SendChannel(player, Fusion::Packets::kChannelBuild, EncodeSkill(tier, skill));
    RecordStat(player, {Fusion::Stat::ProfessionSkill, kSkillConstruction}, skill);
    if (up)
        ChatHandler(player).PSendSysMessage("Your skill in Construction has increased to %u.", uint32(skill));
}

Plots::Actor FusionMgr::PlotActor(Player const* player) const
{
    return Plots::Actor::Player(player->GetGUIDLow(), player->GetSession() ? player->GetSession()->GetAccountId() : 0, IsAdmin(player));
}

// ── The build code's checks ─────────────────────────────────────────────────────────────────

bool FusionMgr::PlotAllowsBuild(Player* player, Fusion::Build::Piece const& piece)
{
    std::lock_guard<std::mutex> lock(m_plotLock);
    return Plots::CanBuild(PlotGrid(), m_plots, PlotActor(player), piece, Now()) != Plots::Verdict::Denied;
}

std::optional<Fusion::Build::Refusal> FusionMgr::PayForPiece(Player* player, Fusion::Build::PieceType type, bool free, bool& usedSalvage)
{
    using Fusion::Build::Refusal;
    usedSalvage = false;
    if (!RecipeFor(type))
        return Refusal::Unknown;
    // Free building (a game master, a free realm) asks nothing, as before.
    if (free)
        return std::nullopt;
    // Otherwise the piece was made ahead at the trade window (its materials paid and its skill-up
    // rolled there): placing takes one kit of it. Every check has passed by now.
    FusionPlayerData& d = player->GetFusion();
    if (!TakeKit(d.constructionKits, type))
        return Refusal::NoKit;
    SaveKit(player, type);
    return std::nullopt;
}

void FusionMgr::SaveKit(Player* player, Fusion::Build::PieceType type)
{
    FusionPlayerData const& d = player->GetFusionConst();
    uint32 const held = KitsOf(d.constructionKits, type);
    if (held)
        CharacterDatabase.PExecute("REPLACE INTO `character_fusion_construction_kit` (`guid`, `piece`, `count`) VALUES (%u, %u, %u)",
            player->GetGUIDLow(), uint32(type), held);
    else
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_construction_kit` WHERE `guid` = %u AND `piece` = %u",
            player->GetGUIDLow(), uint32(type));
    SendChannel(player, kChannelProfessions, EncodeKits(d.constructionKits));
}

void FusionMgr::CraftKits(Player* player, Fusion::Build::PieceType type, uint16 asked)
{
    Recipe const* recipe = RecipeFor(type);
    if (!recipe || !Buildable(type))
        return;
    bool const free = m_buildFree || IsAdmin(player);
    uint16 const count = CraftBatch(asked);
    uint32 made = 0;
    char const* why = nullptr;
    for (uint16 i = 0; i < count; ++i)
    {
        FusionPlayerData& d = player->GetFusion();
        Inventory const bags = BagsFor(player, recipe->materials);
        KitCheck const check = CheckKit(type, d.constructionTier, d.constructionSkill, bags, KitsOf(d.constructionKits, type), free);
        if (check != KitCheck::Ok)
        {
            switch (check)
            {
                case KitCheck::NotLearned: why = "Learn Construction from a Construction Trainer first."; break;
                case KitCheck::SkillTooLow: why = "Your Construction skill is too low for that."; break;
                case KitCheck::NoMaterials: why = "Missing reagents."; break;
                case KitCheck::Full: why = "You can't carry any more of those."; break;
                default: why = "You can't make that."; break;
            }
            break;
        }
        // A kit that could teach takes fresh goods first (so it does); one that can't spends
        // salvage first. Salvage never teaches.
        bool usedSalvage = false;
        if (!free)
        {
            Payment const pay = PlanPayment(recipe->materials, bags, CouldTeach(type, d.constructionSkill, d.constructionTier));
            if (!pay.ok)
            {
                why = "Missing reagents.";
                break;
            }
            for (Material const& m : pay.take)
                player->DestroyItemCount(m.item, m.count, true);
            usedSalvage = pay.usedSalvage;
        }
        AddKits(d.constructionKits, type, 1);
        ++made;
        PlacedSkillUp(player, type, !free && !usedSalvage);
    }
    if (made)
    {
        SaveKit(player, type);
        ChatHandler(player).PSendSysMessage("You create %s%s.", made > 1 ? (std::to_string(made) + " x ").c_str() : "",
            Fusion::Build::Def(type).name.c_str());
    }
    // The stock trade window's red error line for the first refusal.
    if (why && !made)
        player->GetSession()->SendNotification("%s", why);
}

void FusionMgr::PlacedSkillUp(Player* player, Fusion::Build::PieceType type, bool paidFresh)
{
    FusionPlayerData const& d = player->GetFusionConst();
    // Only fresh materials teach: salvage (a piece taken down, a plot packed) builds but never
    // levels Construction. Called for each kit made (CraftKits).
    uint16 const next = PlacementSkillUp(type, d.constructionSkill, d.constructionTier, paidFresh, rand_norm_f());
    if (next != d.constructionSkill)
        SetConstruction(player, d.constructionTier, next, true);
}

bool FusionMgr::MayRemovePiece(Player* player, Fusion::Build::Piece const& piece)
{
    Plots::Verdict v;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        v = Plots::CanRemove(PlotGrid(), m_plots, PlotActor(player), piece, Now());
    }
    if (v != Plots::Verdict::OpenWorld)
        return v == Plots::Verdict::Allowed;
    // The open world as before: the builder, or a game master.
    return piece.owner == player->GetGUIDLow() || IsAdmin(player);
}

void FusionMgr::RefundPiece(Player* player, Fusion::Build::Piece const& piece)
{
    // Salvaged (Construction.h): it builds again, but teaches no Construction. Only of what was
    // paid for (Build::Piece::paid).
    std::vector<Material> const refund = AsSalvaged(Refund(piece));
    // Another's piece (a plot owner taking down a builder's wall): its materials go back to the
    // one who paid for them, waiting at the Plot Steward.
    if (piece.owner && piece.owner != player->GetGUIDLow())
    {
        uint32 sent = 0;
        for (Material const& m : refund)
        {
            StashReclaim(piece.owner, m.item, m.count);
            sent += m.count;
        }
        if (sent)
            ChatHandler(player).PSendSysMessage("Its materials (%u) go back to %s, who built it.", sent, CharacterName(piece.owner).c_str());
        return;
    }
    uint32 stashed = 0;
    for (Material const& m : refund)
    {
        uint32 const given = GiveItems(player, m.item, m.count);
        if (given < m.count)
        {
            StashReclaim(player->GetGUIDLow(), m.item, m.count - given);
            stashed += m.count - given;
        }
    }
    if (stashed)
        ChatHandler(player).PSendSysMessage("Your bags are full: %u materials wait for you at the Plot Steward.", stashed);
}

void FusionMgr::SalvagePiece(Map* map, Fusion::Build::Piece const& piece, Plots::Actor const& by)
{
    if (!map || by.kind != Plots::Actor::Kind::Player || !by.guid)
        return;
    // Salvaged goods, as a refund is: breaking pieces is no way around the fresh-materials rule.
    // Only of what was paid for (a piece built free breaks for nothing, Build::Piece::paid).
    std::vector<Material> const salvage = AsSalvaged(Salvage(piece.type, piece.reinforcement, piece.paid));
    if (salvage.empty())
        return;
    Player* breaker = map->GetPlayer(ObjectGuid(HIGHGUID_PLAYER, by.guid));
    // A plot owner breaking a permitted builder's piece: it goes to the builder's stash, as taking
    // it down would send the refund (Plots::SalvageToBuilder).
    bool toBuilder;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        toBuilder = Plots::SalvageToBuilder(PlotGrid(), m_plots, by, piece, Now());
    }
    if (toBuilder)
    {
        uint32 sent = 0;
        for (Material const& m : salvage)
        {
            StashReclaim(piece.owner, m.item, m.count);
            sent += m.count;
        }
        if (breaker && sent)
            ChatHandler(breaker).PSendSysMessage("Its salvage (%u) goes back to %s, who built it.", sent, CharacterName(piece.owner).c_str());
        return;
    }
    if (!breaker)
    {
        // Gone from the map (a blast left behind): it waits at the Plot Steward.
        for (Material const& m : salvage)
            StashReclaim(by.guid, m.item, m.count);
        return;
    }
    // A group shares it, item by item, among those near enough to have been in the fight.
    std::vector<Player*> members;
    if (Group* group = breaker->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* m = ref->getSource())
                if (m->IsInWorld() && m->GetMap() == map && m->IsAlive() && m->IsWithinDistInMap(breaker, kSalvageShareRange))
                    members.push_back(m);
    }
    if (members.empty())
        members.push_back(breaker);
    // The deal starts with a different member each time, so the odd items go round.
    size_t const start = size_t(urand(0, uint32(members.size()) - 1));
    std::vector<std::vector<Material>> const shares = SplitSalvage(salvage, members.size(), start);
    for (size_t i = 0; i < members.size() && i < shares.size(); ++i)
    {
        Player* m = members[i];
        uint32 stashed = 0;
        for (Material const& item : shares[i])
        {
            uint32 const given = GiveItems(m, item.item, item.count);
            if (given < item.count)
            {
                StashReclaim(m->GetGUIDLow(), item.item, item.count - given);
                stashed += item.count - given;
            }
        }
        // The items' own loot lines say what came; only what didn't fit is told.
        if (stashed)
            ChatHandler(m).PSendSysMessage("Your bags are full: %u salvaged materials wait for you at the Plot Steward.", stashed);
    }
}

void FusionMgr::ReinforcePiece(Player* player, uint32 id)
{
    using Fusion::Build::Refusal;
    auto refuse = [&](Refusal why) { SendChannel(player, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeRefused(why)); };
    Map* map = player->GetMap();
    if (!map || !player->IsAlive())
        return;
    if (player->IsInCombat() && !IsAdmin(player))
        return refuse(Refusal::InCombat);
    Fusion::Build::Piece found;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(map->GetId());
        if (it == m_pieces.end())
            return;
        auto pit = std::find_if(it->second.begin(), it->second.end(), [id](Fusion::Build::Piece const& p) { return p.id == id; });
        if (pit == it->second.end())
            return;
        found = *pit;
    }
    // Only its builder (or a game master) pays to reinforce it.
    if (found.owner != player->GetGUIDLow() && !IsAdmin(player))
        return refuse(Refusal::NotYours);
    if ((Fusion::Build::Center(found) - Fusion::Vec3{player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()}).Length()
        > Fusion::Build::kReach + 5.f)
        return refuse(Refusal::TooFar);
    bool const free = m_buildFree || IsAdmin(player);
    FusionPlayerData const& d = player->GetFusionConst();
    uint8 const next = uint8(found.reinforcement + 1);
    ReinforceTier const* tier = ReinforceInfo(next);
    Inventory const bags = tier ? BagsFor(player, tier->materials) : Inventory();
    switch (CheckReinforce(found.type, found.reinforcement, d.constructionTier, d.constructionSkill, bags, free))
    {
        case ReinforceCheck::Ok:
            break;
        case ReinforceCheck::NotReinforceable:
            return refuse(Refusal::NotReinforceable);
        case ReinforceCheck::MaxTier:
            return refuse(Refusal::MaxReinforcement);
        case ReinforceCheck::NotLearned:
            return refuse(Refusal::NotLearned);
        case ReinforceCheck::SkillTooLow:
            return refuse(Refusal::SkillTooLow);
        case ReinforceCheck::NoMaterials:
            return refuse(Refusal::NoMaterials);
    }
    // Salvage pays for it as fresh goods do (reinforcing teaches nothing either way): it first.
    if (!free && tier)
    {
        Payment const pay = PlanPayment(tier->materials, bags, false);
        if (!pay.ok)
            return refuse(Refusal::NoMaterials);
        for (Material const& m : pay.take)
            player->DestroyItemCount(m.item, m.count, true);
    }
    // Up a tier, keeping its share of health: a damaged piece stays as damaged.
    Fusion::Build::Piece changed;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto& list = m_pieces[map->GetId()];
        auto pit = std::find_if(list.begin(), list.end(), [id](Fusion::Build::Piece const& p) { return p.id == id; });
        if (pit == list.end())
            return;
        float const share = pit->health / std::max(1.f, Fusion::Build::MaxHealth(*pit));
        pit->reinforcement = next;
        pit->health = share * Fusion::Build::MaxHealth(*pit);
        // A tier put on free gives nothing of itself back (Build::Piece::paid).
        if (free)
            pit->paid = uint8(pit->paid & ~Fusion::Build::PaidTier(next));
        else
            pit->paid = uint8(pit->paid | Fusion::Build::PaidTier(next));
        changed = *pit;
    }
    CharacterDatabase.PExecute("UPDATE `fusion_build_piece` SET `health` = %f, `reinforcement` = %u, `decayed_at` = %" PRId64 ", `paid` = %u WHERE `id` = %u",
        changed.health, uint32(changed.reinforcement), int64_t(changed.decayedAt), uint32(changed.paid), id);
    uint32 const mapId = map->GetId();
    Broadcast(map, changed.pos, [&](Player* viewer) {
        if (viewer->GetFusionConst().knownMap != mapId)
            return std::vector<uint8_t>();
        return Fusion::Build::EncodePieces({changed}, viewer->GetGUIDLow());
    });
    // No chat line: the build-mode hint panel names the aimed piece's new tier.
}

bool FusionMgr::PlotAllowsDoor(Player const* player, Fusion::Build::Piece const& door)
{
    std::lock_guard<std::mutex> lock(m_plotLock);
    return Plots::CanUseDoor(PlotGrid(), m_plots, PlotActor(player), door, Now()) == Plots::Verdict::Allowed;
}

bool FusionMgr::PlotAllowsDamage(Plots::Actor const& by, Fusion::Build::Piece const& piece)
{
    std::lock_guard<std::mutex> lock(m_plotLock);
    return Plots::CanDamage(PlotGrid(), m_plots, by, piece, Now()) != Plots::Verdict::Denied;
}

bool FusionMgr::PieceDamageableBy(uint32 mapId, uint32 pieceId, Plots::Actor const& by)
{
    Fusion::Build::Piece piece;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(mapId);
        if (it == m_pieces.end())
            return true;
        auto pit = std::find_if(it->second.begin(), it->second.end(), [pieceId](Fusion::Build::Piece const& p) { return p.id == pieceId; });
        if (pit == it->second.end())
            return true;
        piece = *pit;
    }
    return PlotAllowsDamage(by, piece);
}

// ── Woodcutting ─────────────────────────────────────────────────────────────────────────────

bool FusionMgr::UseLumberNode(Player* player, GameObject* go)
{
    NodeDef const* node = NodeFor(go->GetEntry());
    if (!node || !m_enabled || !player->HasFusion())
        return false;
    FusionPlayerData const& d = player->GetFusionConst();
    bool const gm = IsAdmin(player);
    switch (CanGather(go->GetEntry(), d.constructionTier, d.constructionSkill, player->HasItemCount(kWoodcuttingAxe, 1), gm))
    {
        case GatherCheck::Ok:
            break;
        case GatherCheck::NoAxe:
            player->GetSession()->SendNotification("Requires a Woodcutting Axe");
            return true;
        case GatherCheck::NotLearned:
            player->GetSession()->SendNotification("Requires Construction");
            return true;
        case GatherCheck::SkillTooLow:
            player->GetSession()->SendNotification("Requires Construction %u", uint32(node->skill.required));
            return true;
        default:
            return true;
    }
    if (!go->isSpawned() || go->getLootState() == GO_JUST_DEACTIVATED)
        return true;
    // The nodes carry no lock (a woodcutting lock type can't be added to the client's
    // Lock.dbc), and vmangos opens an unlocked chest's loot through neither its use nor the
    // opening spell, so the loot window is opened here.
    OpenedLumberNode(player, go);
    player->SendLoot(go->GetObjectGuid(), LOOT_CORPSE);
    return true;
}

void FusionMgr::OpenedLumberNode(Player* player, GameObject* go)
{
    NodeDef const* node = NodeFor(go->GetEntry());
    if (!node || !m_enabled || !player->HasFusion() || IsAdmin(player))
        return;
    // As vanilla's veins: one skill-up for a player until the node respawns.
    if (go->IsInSkillupList(player))
        return;
    go->AddToSkillupList(player);
    // Now and then a cut turns up a rustic furnishing (fusion/Furniture.h DropFor).
    {
        Fusion::Build::PieceType const found = Fusion::Furniture::DropFor(rand_norm_f(), rand_norm_f());
        FusionPlayerData& fd = player->GetFusion();
        if (found != Fusion::Build::PieceType::None && AddKits(fd.constructionKits, found, 1))
        {
            SaveKit(player, found);
            ChatHandler(player).PSendSysMessage("Among the lumber you find a %s kit.", Fusion::Build::Def(found).name.c_str());
        }
    }
    FusionPlayerData const& d = player->GetFusionConst();
    uint16 const next = GatherSkillUp(*node, d.constructionSkill, d.constructionTier, rand_norm_f());
    if (next != d.constructionSkill)
        SetConstruction(player, d.constructionTier, next, true);
}

// ── The NPCs: the Construction window ───────────────────────────────────────────────────────
// The trainer, the steward (the plot coordinator) and the courier open the client's
// Construction window with their access instead of a gossip menu; its buttons come back on the
// world channel (fusion/Homestead.h) and every answer is said in the window's status line.

namespace
{
    using Fusion::Homestead::Npc;

    Npc RoleOf(uint32 entry)
    {
        switch (entry)
        {
            case kTrainerEntry: return Npc::Trainer;
            case kStewardEntry: return Npc::Steward;
            case kCourierEntry: return Npc::Courier;
            default: return Npc::None;
        }
    }

    // A Construction colour as the window draws a line.
    Fusion::Homestead::Hue HueOf(Fusion::Construction::Color c)
    {
        switch (c)
        {
            case Fusion::Construction::Color::Orange: return Fusion::Homestead::Hue::Orange;
            case Fusion::Construction::Color::Yellow: return Fusion::Homestead::Hue::Yellow;
            case Fusion::Construction::Color::Green: return Fusion::Homestead::Hue::Green;
            case Fusion::Construction::Color::Gray: return Fusion::Homestead::Hue::Gray;
            default: return Fusion::Homestead::Hue::Red;
        }
    }

    // The window's buttons work while the player stays this near the NPC that opened it.
    constexpr float kHomesteadNpcReach = 12.f;
    // The claimed plots near are looked at again after a walk this far (or any sale or permit).
    constexpr float kNearPlotsMove = 40.f;
}

bool FusionMgr::ConstructionGossipHello(Player* player, Creature* creature)
{
    if (!m_enabled || !player->HasFusion())
        return false;
    Npc const role = RoleOf(creature->GetEntry());
    if (role == Npc::None)
        return false;
    FusionPlayerData& d = player->GetFusion();
    d.homesteadNpc = creature->GetObjectGuid();
    d.homesteadRole = uint8(role);
    player->PlayerTalkClass->ClearMenus();
    player->PlayerTalkClass->CloseGossip();
    SendHomestead(player, Fusion::Homestead::Open::Npc);
    return true;
}

bool FusionMgr::ConstructionGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 /*action*/)
{
    // No gossip menus any more: a stale one just closes.
    if (!m_enabled || !player->HasFusion() || RoleOf(creature->GetEntry()) == Npc::None)
        return false;
    player->PlayerTalkClass->CloseGossip();
    return true;
}

Fusion::Homestead::Npc FusionMgr::HomesteadNpcNear(Player* player, Creature** out)
{
    FusionPlayerData const& d = player->GetFusionConst();
    if (!d.homesteadRole || d.homesteadNpc.IsEmpty() || !player->IsInWorld())
        return Npc::None;
    Creature* c = player->GetMap()->GetCreature(d.homesteadNpc);
    if (!c || !c->IsAlive() || !player->IsWithinDistInMap(c, kHomesteadNpcReach))
        return Npc::None;
    if (out)
        *out = c;
    return Npc(d.homesteadRole);
}

uint32 FusionMgr::PiecesOnPlot(uint32 plotId)
{
    Plots::District const* district = PlotGrid().DistrictOf(plotId);
    if (!district)
        return 0;
    uint32 n = 0;
    std::lock_guard<std::mutex> lock(m_buildLock);
    auto it = m_pieces.find(district->map);
    if (it != m_pieces.end())
        for (Fusion::Build::Piece const& p : it->second)
            if (Plots::PlotOf(PlotGrid(), p) == plotId)
                ++n;
    return n;
}

void FusionMgr::SendHomestead(Player* player, Fusion::Homestead::Open open, Said const& said)
{
    using namespace Fusion::Homestead;
    Creature* npc = nullptr;
    Npc const role = HomesteadNpcNear(player, &npc);
    Window w;
    w.open = open == Open::Npc && role == Npc::None ? Open::Hud : open;
    w.npc = role;
    if (npc)
        w.npcName = npc->GetName();
    if (!said.text.empty())
    {
        w.tone = said.ok ? Tone::Done : Tone::Error;
        w.message = said.text;
    }
    uint32 const account = player->GetSession()->GetAccountId();
    int64 const t = Now();
    Plots::Registry const reg = PlotsCopy();
    if (Plots::Plot const* mine = reg.OwnedBy(account))
    {
        PlotView& v = w.plot;
        v.id = mine->id;
        v.standing = uint8(Plots::StandingOf(*mine, t));
        v.paidUntil = uint32(std::max<int64>(0, std::min<int64>(mine->paidUntil, 0xFFFFFFFFll)));
        v.permits = uint8(std::min<size_t>(mine->permits.size(), 255));
        if (Plots::District const* d = PlotGrid().DistrictOf(mine->id))
        {
            v.size = uint8(d->size);
            v.map = uint16(d->map);
            v.upkeep = d->upkeepCopper;
        }
        if (std::optional<Plots::Rect> const b = PlotGrid().Bounds(mine->id))
            v.bounds = *b;
        if (Plots::Site const* s = PlotGrid().SiteOf(mine->id))
            v.place = s->where;
        v.pieces = uint16(std::min<uint32>(PiecesOnPlot(mine->id), 0xFFFF));
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT SUM(`count`) FROM `character_fusion_reclaim` WHERE `guid` = %u", player->GetGUIDLow()))
        w.reclaim = r->Fetch()[0].GetUInt32();

    switch (role)
    {
        case Npc::Steward:
        {
            StewardAsk ask;
            ask.account = account;
            ask.guid = player->GetGUIDLow();
            ask.money = player->GetMoney();
            ask.gm = IsAdmin(player);
            // A steward sells the plots of the site it stands in (all of them elsewhere).
            if (Plots::Site const* s = PlotGrid().SiteAt(npc->GetMapId(), npc->GetPositionX(), npc->GetPositionY(), Fusion::Homestead::kHomesteadsNear))
                ask.site = s->id;
            ask.visited = player->GetFusionConst().plotVisited;
            ask.reclaim = w.reclaim;
            if (Player* target = ObjectAccessor::FindPlayer(player->GetSelectionGuid()); target && target != player)
            {
                ask.target = target->GetGUIDLow();
                ask.targetName = target->GetName();
            }
            player->GetFusion().homesteadPermitTarget = ask.target;
            ask.nameOf = [](uint32 guid) { return CharacterName(guid); };
            w.services = StewardServices(PlotGrid(), reg, ask, t);
            // Furnishings sold as kits (fusion/Furniture.h priceCopper).
            for (Fusion::Furniture::Def const* f : Fusion::Furniture::Sold())
            {
                if (w.services.size() >= Fusion::Homestead::kMaxServices)
                    break;
                Fusion::Homestead::Service s;
                s.action = Fusion::Homestead::Action::BuyDecor;
                s.arg = f->id;
                s.cost = f->priceCopper;
                s.label = std::string("Buy furnishing: ") + f->name;
                s.note = "One kit, placed with Build Structure";
                s.enabled = player->GetMoney() >= f->priceCopper || IsAdmin(player);
                w.services.push_back(std::move(s));
            }
            break;
        }
        case Npc::Courier:
        {
            CourierAsk ask;
            ask.account = account;
            ask.map = npc->GetMapId();
            ask.x = npc->GetPositionX();
            ask.y = npc->GetPositionY();
            ask.canGoBack = player->GetFusionConst().courierSaved;
            // The way back shares the hearthstone's cooldown (Portals.h).
            ask.backCooldownMs = HearthCooldownLeft(player);
            w.services = CourierServices(PlotGrid(), reg, ask);
            break;
        }
        case Npc::Trainer:
            w.services = TrainerServices(player);
            break;
        default:
            break;
    }
    SendChannel(player, Fusion::Packets::kChannelWorld, EncodeWindow(w));
}

void FusionMgr::HandleHomestead(Player* player, std::vector<uint8> const& body)
{
    using namespace Fusion::Homestead;
    if (body.empty())
        return;
    if (body[0] == kOpAsk)
    {
        // Opened from the HUD: the limited view, whoever was spoken to before.
        FusionPlayerData& d = player->GetFusion();
        d.homesteadRole = 0;
        d.homesteadNpc.Clear();
        SendHomestead(player, Open::Refresh);
        return;
    }
    Action a = Action::None;
    uint32 arg = 0;
    if (!DecodeAct(body, a, arg))
        return;
    Creature* npc = nullptr;
    Npc const at = HomesteadNpcNear(player, &npc);
    if (!AllowedAt(a, at))
    {
        char const* who = NeededFor(a) == Npc::Steward ? "a Plot Steward" : NeededFor(a) == Npc::Courier ? "a Homestead Courier"
            : "a Construction Trainer";
        SendHomestead(player, Open::Refresh, {false, std::string("Speak with ") + who + " for that."});
        return;
    }
    Said said;
    switch (a)
    {
        case Action::Claim:
        case Action::ClaimVisited:
            said = ClaimPlot(player, a, arg, npc);
            break;
        case Action::Release:
            said = AbandonPlot(player);
            break;
        case Action::Teleport:
            said = TeleportToPlot(player);
            break;
        case Action::Pay:
            said = PayPlot(player, arg);
            break;
        case Action::Collect:
            said = CollectReclaim(player);
            break;
        case Action::Train:
            said = Train(player);
            break;
        case Action::Craft:
            said = CraftPart(player, arg);
            break;
        case Action::BuyDecor:
        {
            Fusion::Furniture::Def const* f = Fusion::Furniture::Find(Fusion::Build::PieceType(uint8(arg)));
            FusionPlayerData& pd = player->GetFusion();
            if (!f || !f->priceCopper || arg > 0xFF)
                said = {false, "That isn't sold here."};
            else if (player->GetMoney() < f->priceCopper && !IsAdmin(player))
                said = {false, "You need " + MoneyText(f->priceCopper) + "."};
            else if (KitsOf(pd.constructionKits, Fusion::Build::PieceType(f->id)) >= kMaxKits)
                said = {false, "You can't carry any more of those."};
            else
            {
                if (!IsAdmin(player))
                    player->ModifyMoney(-int32(f->priceCopper));
                AddKits(pd.constructionKits, Fusion::Build::PieceType(f->id), 1);
                SaveKit(player, Fusion::Build::PieceType(f->id));
                said = {true, std::string("Bought a kit: ") + f->name + "."};
            }
            break;
        }
        case Action::Travel:
            said = Travel(player, arg);
            break;
        case Action::Permit:
        case Action::Revoke:
        {
            uint32 guid = arg;
            Plots::PermitLevel level = Plots::PermitLevel::None;
            if (a == Action::Permit)
            {
                // The one the window named, not whoever is targeted now.
                guid = player->GetFusionConst().homesteadPermitTarget;
                if (!guid || guid == player->GetGUIDLow())
                {
                    said = {false, "Target the player, then speak to the Plot Steward again."};
                    break;
                }
                level = arg == uint32(Plots::PermitLevel::Builder) ? Plots::PermitLevel::Builder
                    : arg == uint32(Plots::PermitLevel::Guest) ? Plots::PermitLevel::Guest : Plots::PermitLevel::None;
            }
            said = SetPermit(player, guid, level);
            break;
        }
        default:
            return;
    }
    SendHomestead(player, Open::Refresh, said);
}

void FusionMgr::SendNearPlots(Player* player, bool force)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const generation = m_plotGeneration.load();
    uint32 const map = player->GetMapId();
    float const x = player->GetPositionX(), y = player->GetPositionY();
    float const dx = x - d.nearPlotsX, dy = y - d.nearPlotsY;
    bool const moved = map != d.nearPlotsMap || dx * dx + dy * dy > kNearPlotsMove * kNearPlotsMove;
    if (!force && !moved && generation == d.nearPlotsGeneration)
        return;
    d.nearPlotsMap = map;
    d.nearPlotsX = x;
    d.nearPlotsY = y;
    d.nearPlotsGeneration = generation;
    std::vector<uint8_t> body;
    if (PlotGrid().InDistrict(map, x, y, Fusion::Homestead::kNearRadius))
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        body = Fusion::Homestead::EncodeNearPlots(Fusion::Homestead::PlotsNear(PlotGrid(), m_plots, PlotActor(player), map, x, y,
            Fusion::Homestead::kNearRadius, Now()));
    }
    else
        body = Fusion::Homestead::EncodeNearPlots({});
    if (body == d.nearPlotsSent && !force)
        return;
    d.nearPlotsSent = body;
    SendChannel(player, Fusion::Packets::kChannelWorld, body);
}

std::vector<Fusion::Homestead::Service> FusionMgr::TrainerServices(Player* player)
{
    using namespace Fusion::Homestead;
    std::vector<Service> out;
    FusionPlayerData const& d = player->GetFusionConst();
    bool const gm = IsAdmin(player);
    Tier const next = Next(d.constructionTier);
    if (next != Tier::None)
    {
        TierDef const& t = TierInfo(next);
        Service s;
        s.action = Action::Train;
        s.cost = t.costCopper;
        s.label = std::string("Train ") + t.name + " Construction";
        std::ostringstream note;
        note << "Skill up to " << t.cap << "  -  level " << uint32(t.levelToLearn);
        if (t.skillToLearn)
            note << ", Construction " << t.skillToLearn;
        s.note = note.str();
        switch (CanTrain(d.constructionTier, next, d.constructionSkill, uint8(player->GetLevel()), player->GetMoney(), gm))
        {
            case TrainResult::Ok:
                break;
            case TrainResult::SkillTooLow:
                s.enabled = false;
                s.note = "Requires Construction " + std::to_string(t.skillToLearn);
                break;
            case TrainResult::LevelTooLow:
                s.enabled = false;
                s.note = "Requires level " + std::to_string(t.levelToLearn);
                break;
            case TrainResult::NotEnoughMoney:
                s.enabled = false;
                s.note = "You need " + MoneyText(t.costCopper);
                break;
            default:
                s.enabled = false;
                break;
        }
        out.push_back(std::move(s));
    }
    // The parts the player's skills can make: each in its recipe colour, greyed while the skill
    // or the reagents are short.
    for (size_t i = 0; i < m_crafts.size(); ++i)
    {
        Craft const& c = m_crafts[i];
        uint16 const line = c.recipe.skillLine;
        bool const known = line == kSkillConstruction ? d.constructionTier != Tier::None : player->HasSkill(line);
        if (!gm && !known)
            continue;
        uint16 const skill = line == kSkillConstruction ? d.constructionSkill : known ? player->GetSkillValue(line) : 0;
        Service s;
        s.action = Action::Craft;
        s.arg = uint32(i);
        s.label = "Make " + c.name;
        if (c.recipe.productCount > 1)
            s.label += " x" + std::to_string(c.recipe.productCount);
        s.note = MaterialList(c.recipe.reagents) + (c.needsAnvil ? "  -  at an anvil" : "");
        s.hue = HueOf(ColorOf(skill, c.recipe.skill));
        switch (CanCraft(c.recipe, skill, BagsFor(player, c.recipe.reagents), gm))
        {
            case CraftCheck::Ok:
                break;
            case CraftCheck::SkillTooLow:
                s.enabled = false;
                s.note = "Requires " + std::string(SkillLineName(line)) + " " + std::to_string(c.recipe.skill.required);
                break;
            default:
                s.enabled = false;
                s.note = "Needs " + s.note;
                break;
        }
        out.push_back(std::move(s));
    }
    return out;
}

FusionMgr::Said FusionMgr::Train(Player* player)
{
    FusionPlayerData const& d = player->GetFusionConst();
    bool const gm = IsAdmin(player);
    Tier const have = d.constructionTier;
    Tier const want = Next(have);
    TierDef const& t = TierInfo(want);
    std::ostringstream o;
    switch (CanTrain(have, want, d.constructionSkill, uint8(player->GetLevel()), player->GetMoney(), gm))
    {
        case TrainResult::Ok:
            break;
        case TrainResult::SkillTooLow:
            o << "You need " << t.skillToLearn << " Construction to learn " << t.name << ".";
            return {false, o.str()};
        case TrainResult::LevelTooLow:
            o << "You must be level " << uint32(t.levelToLearn) << " to learn " << t.name << " Construction.";
            return {false, o.str()};
        case TrainResult::NotEnoughMoney:
            o << t.name << " Construction costs " << Money(t.costCopper) << ".";
            return {false, o.str()};
        default:
            return {false, "There is nothing more I can teach you."};
    }
    if (!gm)
        player->ModifyMoney(-int32(t.costCopper));
    SetConstruction(player, want, have == Tier::None ? kStartSkill : d.constructionSkill, false);
    o << "You have learned " << t.name << " Construction: your skill may now reach " << t.cap << ".";
    return {true, o.str()};
}

FusionMgr::Said FusionMgr::CraftPart(Player* player, size_t index)
{
    if (index >= m_crafts.size())
        return {false, "There is no such part."};
    Craft const& c = m_crafts[index];
    FusionPlayerData const& d = player->GetFusionConst();
    bool const gm = IsAdmin(player);
    uint16 const line = c.recipe.skillLine;
    bool const known = line == kSkillConstruction ? d.constructionTier != Tier::None : player->HasSkill(line);
    uint16 const skill = line == kSkillConstruction ? d.constructionSkill : known ? player->GetSkillValue(line) : 0;
    if (c.needsAnvil && !gm && !NearAnvil(player))
        return {false, c.name + " must be made at an anvil."};
    Inventory const bags = BagsFor(player, c.recipe.reagents);
    switch (CanCraft(c.recipe, skill, bags, gm))
    {
        case CraftCheck::Ok:
            break;
        case CraftCheck::SkillTooLow:
            return {false, c.name + " needs " + std::to_string(c.recipe.skill.required) + " " + SkillLineName(line) + "."};
        default:
            return {false, "You still need " + MaterialList(Missing(bags, c.recipe.reagents)) + "."};
    }
    // Room for what it makes, before anything is used up.
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, c.recipe.product, c.recipe.productCount) != EQUIP_ERR_OK)
        return {false, "Your bags are full."};
    for (Material const& m : c.recipe.reagents)
        player->DestroyItemCount(m.item, m.count, true);
    if (Item* made = player->StoreNewItem(dest, c.recipe.product, true, Item::GenerateItemRandomPropertyId(c.recipe.product)))
        player->SendNewItem(made, c.recipe.productCount, false, true);
    Said const done{true, "Made " + c.name + (c.recipe.productCount > 1 ? " x" + std::to_string(c.recipe.productCount) : std::string()) + "."};
    // The point, as vanilla's own crafting gives it (a game master's parts teach nothing).
    if (gm || !known)
        return done;
    float const roll = rand_norm_f();
    if (line == kSkillConstruction)
    {
        uint16 const next = ApplySkillUp(d.constructionSkill, Cap(d.constructionTier), c.recipe.skill, roll);
        if (next != d.constructionSkill)
            SetConstruction(player, d.constructionTier, next, true);
    }
    else
    {
        uint16 const pure = player->GetSkillValuePure(line);
        if (ApplySkillUp(pure, player->GetSkillMaxPure(line), c.recipe.skill, roll) > pure)
            player->UpdateSkill(line, 1);
    }
    return done;
}

FusionMgr::Said FusionMgr::Travel(Player* player, uint32 arg)
{
    using namespace Fusion::Homestead;
    if (!player->IsAlive())
        return {false, "Not while you are dead."};
    if (player->IsInCombat())
        return {false, "Not in the middle of a fight."};
    if (player->IsTaxiFlying() || player->GetTransport())
        return {false, "Not while travelling."};
    FusionPlayerData& d = player->GetFusion();
    if (arg == kTravelBack)
    {
        // No free hearth: the way back shares the hearthstone's cooldown and starts it.
        if (uint32 const left = HearthCooldownLeft(player))
            return {false, "That shares your hearthstone's cooldown: ready in " + Fusion::Portals::WaitText(left) + "."};
        if (d.courierSaved)
        {
            d.courierSaved = false;
            StartHearthCooldown(player);
            player->TeleportTo(d.courierMap, d.courierX, d.courierY, d.courierZ, d.courierO);
        }
        else
        {
            // Home to the inn, as the hearthstone goes: with its cooldown.
            StartHearthCooldown(player);
            player->TeleportToHomebind(0, false);
        }
        return {true, std::string()};
    }
    // Where to: a site's gate or stop, or the stop nearest the player's own plot.
    Plots::Site const* site = nullptr;
    Plots::Spot to;
    if (arg == kTravelMine)
    {
        uint32 id = 0;
        {
            std::lock_guard<std::mutex> lock(m_plotLock);
            if (Plots::Plot const* p = m_plots.OwnedBy(player->GetSession()->GetAccountId()))
                id = p->id;
        }
        site = id ? PlotGrid().SiteOf(id) : nullptr;
        std::optional<Plots::Rect> const b = PlotGrid().Bounds(id);
        if (!site || !b)
            return {false, "You hold no homestead."};
        to = Plots::NearestStop(*site, (b->x0 + b->x1) / 2.f, (b->y0 + b->y1) / 2.f, site->gate.z);
    }
    else if (!TravelStop(PlotGrid(), arg, site, to))
        return {false, "I don't go there."};
    RememberWayBack(player);
    TeleportToSpot(player, site->map, to);
    return {true, std::string()};
}

void FusionMgr::RememberWayBack(Player* player)
{
    // Coming from outside every site: remember where, for the courier's way back.
    if (PlotGrid().SiteAt(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), Fusion::Homestead::kHomesteadsNear))
        return;
    FusionPlayerData& d = player->GetFusion();
    d.courierSaved = true;
    d.courierMap = player->GetMapId();
    d.courierX = player->GetPositionX();
    d.courierY = player->GetPositionY();
    d.courierZ = player->GetPositionZ();
    d.courierO = player->GetOrientation();
}

// ── The steward (the plot coordinator): plots ───────────────────────────────────────────────

FusionMgr::Said FusionMgr::ClaimPlot(Player* player, Fusion::Homestead::Action how, uint32 arg, Creature* steward)
{
    // A lapsed plot stays taken until it is packed: pack any first, so it can be claimed.
    SweepPlots(player->GetMap());
    bool const gm = IsAdmin(player);
    uint32 const account = player->GetSession()->GetAccountId();
    // A steward sells the plots of the site it stands in.
    uint8 site = 0;
    if (steward)
        if (Plots::Site const* s = PlotGrid().SiteAt(steward->GetMapId(), steward->GetPositionX(), steward->GetPositionY(), Fusion::Homestead::kHomesteadsNear))
            site = s->id;
    Plots::Buyer const buyer{player->GetGUIDLow(), account, gm ? 0xFFFFFFFFu : player->GetMoney()};
    int64 const t = Now();
    uint32 charge = 0;
    uint32 plotId = 0;
    Plots::BuyResult res;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        if (how == Fusion::Homestead::Action::Claim)
            res = Fusion::Homestead::ClaimInDistrict(PlotGrid(), m_plots, arg, site, buyer, t, charge, plotId, m_plotsToPack);
        else
        {
            plotId = arg;
            Plots::District const* d = PlotGrid().DistrictOf(plotId);
            // The plot the player last walked on, not any plot the client names (a steward that
            // stands in no site would otherwise sell every plot on the server).
            if (!d || (site && d->site != site) || (!gm && plotId != player->GetFusionConst().plotVisited))
                res = Plots::BuyResult::NoSuchPlot;
            else if (m_plots.CountOwnedBy(account) >= Plots::kMaxPlotsPerAccount)
                res = Plots::BuyResult::LimitReached;
            else if (std::find(m_plotsToPack.begin(), m_plotsToPack.end(), plotId) != m_plotsToPack.end())
                res = Plots::BuyResult::Taken;
            else
                res = Fusion::Homestead::ClaimPlot(PlotGrid(), m_plots, plotId, buyer, t, charge);
        }
    }
    switch (res)
    {
        case Plots::BuyResult::Ok:
            break;
        case Plots::BuyResult::LimitReached:
            return {false, "You already hold a homestead: one to a player. Release it first."};
        case Plots::BuyResult::Taken:
            return {false, how == Fusion::Homestead::Action::Claim ? "Every homestead of that size is taken." : "That homestead is taken."};
        case Plots::BuyResult::NotEnoughMoney:
        {
            Plots::District const* d = how == Fusion::Homestead::Action::Claim
                ? (arg < PlotGrid().Districts().size() ? &PlotGrid().Districts()[arg] : nullptr) : PlotGrid().DistrictOf(plotId);
            if (d)
                return {false, "That homestead costs " + Money(d->priceCopper) + "."};
            return {false, "You don't have the money for that."};
        }
        default:
            return {false, "This steward has no such homestead."};
    }
    if (!gm)
        player->ModifyMoney(-int32(charge));
    CharacterDatabase.PExecute("INSERT INTO `character_fusion_plot` (`id`, `account`, `owner`, `bought_at`, `paid_until`) VALUES (%u, %u, %u, %lld, %lld)",
        plotId, account, player->GetGUIDLow(), (long long)t, (long long)(t + int64(Plots::kIncludedWeeks) * Plots::kWeek));
    ++m_plotGeneration;
    // The deed: kept for its holder at the steward when the bags are full.
    if (Plots::District const* d = PlotGrid().DistrictOf(plotId))
        if (d->deedItem && !GiveItems(player, d->deedItem, 1) && sObjectMgr.GetItemPrototype(d->deedItem))
            StashReclaim(player->GetGUIDLow(), d->deedItem, 1);
    return {true, "The " + PlotName(plotId) + " is yours. The first week's upkeep is paid; only you and those you permit may build or break anything there."};
}

FusionMgr::Said FusionMgr::TeleportToPlot(Player* player)
{
    if (!player->IsAlive())
        return {false, "Not while you are dead."};
    if (player->IsInCombat())
        return {false, "Not in the middle of a fight."};
    if (player->IsTaxiFlying() || player->GetTransport())
        return {false, "Not while travelling."};
    uint32 id = 0;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        if (Plots::Plot const* p = m_plots.OwnedBy(player->GetSession()->GetAccountId()))
            id = p->id;
    }
    Plots::District const* d = id ? PlotGrid().DistrictOf(id) : nullptr;
    if (!d)
        return {false, "You hold no homestead."};
    // On the edge nearest the gate, in the margin no piece may stand in, on the ground there.
    Plots::Spot at = Fusion::Homestead::Arrival(PlotGrid(), id);
    if (MapManager::IsValidMapCoord(d->map, at.x, at.y))
        if (TerrainInfo const* terrain = sTerrainMgr.LoadTerrain(d->map))
        {
            float const ground = terrain->GetWaterOrGroundLevel(at.x, at.y, MAX_HEIGHT);
            if (ground > INVALID_HEIGHT)
                at.z = ground;
        }
    RememberWayBack(player);
    player->TeleportTo(d->map, at.x, at.y, at.z + 0.5f, at.o);
    return {true, "Welcome home: your " + PlotName(id) + "."};
}

FusionMgr::Said FusionMgr::PayPlot(Player* player, uint32 weeks)
{
    bool const gm = IsAdmin(player);
    uint32 const account = player->GetSession()->GetAccountId();
    int64 const t = Now();
    uint32 charge = 0;
    uint32 id = 0;
    int64 until = 0;
    Plots::PayResult res = Plots::PayResult::NotOwner;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        if (Plots::Plot const* owned = m_plots.OwnedBy(account))
        {
            Plots::Plot* p = m_plots.Find(owned->id);
            // 0: as many weeks as may be paid ahead from where the paid time ends.
            if (!weeks)
                weeks = uint32(std::max<int64>(0, (t + int64(Plots::kMaxPrepaidWeeks) * Plots::kWeek - p->paidUntil) / Plots::kWeek));
            res = Plots::PayUpkeep(PlotGrid(), *p, account, weeks, gm ? 0xFFFFFFFFu : player->GetMoney(), t, charge);
            id = p->id;
            until = p->paidUntil;
        }
    }
    switch (res)
    {
        case Plots::PayResult::Ok:
            break;
        case Plots::PayResult::NotEnoughMoney:
            return {false, "You don't have the money for that."};
        case Plots::PayResult::TooFarAhead:
        case Plots::PayResult::NoWeeks:
            return {false, "Upkeep may be paid at most " + std::to_string(Plots::kMaxPrepaidWeeks) + " weeks ahead."};
        case Plots::PayResult::Lapsed:
            return {false, "Your homestead has lapsed."};
        default:
            return {false, "You hold no homestead."};
    }
    if (!gm)
        player->ModifyMoney(-int32(charge));
    CharacterDatabase.PExecute("UPDATE `character_fusion_plot` SET `paid_until` = %lld WHERE `id` = %u", (long long)until, id);
    ++m_plotGeneration;
    if (until <= t)
        return {true, "Paid " + std::to_string(weeks) + (weeks == 1 ? " week" : " weeks") + " (" + Money(charge)
            + "), but the upkeep is still behind: pay more to keep building."};
    return {true, "Paid " + std::to_string(weeks) + (weeks == 1 ? " week" : " weeks") + " (" + Money(charge) + "): kept until "
        + TimeToTimestampStr(time_t(until)) + "."};
}

FusionMgr::Said FusionMgr::SetPermit(Player* player, uint32 guid, Plots::PermitLevel level)
{
    uint32 id = 0;
    bool done = false;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        if (Plots::Plot const* owned = m_plots.OwnedBy(player->GetSession()->GetAccountId()))
        {
            Plots::Plot* p = m_plots.Find(owned->id);
            id = p->id;
            done = level == Plots::PermitLevel::None ? Plots::Revoke(*p, guid) : Plots::Grant(*p, guid, level);
        }
    }
    if (!id)
        return {false, "You hold no homestead."};
    std::string const name = CharacterName(guid);
    if (!done)
        return {false, level == Plots::PermitLevel::None ? name + " has no permit."
            : name + " can't be given a permit (at most " + std::to_string(Plots::kMaxPermits) + ")."};
    if (level == Plots::PermitLevel::None)
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_plot_permit` WHERE `plot` = %u AND `guid` = %u", id, guid);
    else
        CharacterDatabase.PExecute("REPLACE INTO `character_fusion_plot_permit` (`plot`, `guid`, `level`) VALUES (%u, %u, %u)", id, guid, uint32(level));
    ++m_plotGeneration;
    return {true, level == Plots::PermitLevel::None ? name + " may no longer enter your homestead."
        : level == Plots::PermitLevel::Builder ? name + " may now build on your homestead." : name + " may now use your homestead's doors."};
}

FusionMgr::Said FusionMgr::AbandonPlot(Player* player)
{
    uint32 const account = player->GetSession()->GetAccountId();
    uint32 id = 0;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        if (Plots::Plot const* p = m_plots.OwnedBy(account))
        {
            id = p->id;
            Plots::Abandon(m_plots, id, account);
            // Packed now if the plot is on this map, else by the next player on its map.
            m_plotsToPack.push_back(id);
        }
    }
    if (!id)
        return {false, "You hold no homestead."};
    // Its record goes at once, whenever its pieces are packed: the account may claim another
    // straight away (the table holds one plot to an account).
    ForgetPlotRows(id);
    SweepPlots(player->GetMap());
    ++m_plotGeneration;
    return {true, "The " + PlotName(id) + " is no longer yours. What stood on it came down as if taken down (about half, as salvaged goods): collect it here."};
}

FusionMgr::Said FusionMgr::CollectReclaim(Player* player)
{
    uint32 const guid = player->GetGUIDLow();
    std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
        "SELECT `item`, `count` FROM `character_fusion_reclaim` WHERE `guid` = %u AND `count` > 0", guid);
    if (!r)
        return {false, "Nothing waits here for you."};
    uint32 left = 0, got = 0;
    do
    {
        Field* f = r->Fetch();
        uint32 const item = f[0].GetUInt32();
        uint32 const count = f[1].GetUInt32();
        uint32 const given = GiveItems(player, item, count);
        left += count - given;
        got += given;
        // Taken off what is there, not set: a stash added to meanwhile keeps the addition. At
        // once, not queued: a second press read before a queued update ran would pay twice.
        if (given)
            CharacterDatabase.DirectPExecute("UPDATE `character_fusion_reclaim` SET `count` = `count` - %u WHERE `guid` = %u AND `item` = %u", given, guid, item);
    } while (r->NextRow());
    CharacterDatabase.DirectPExecute("DELETE FROM `character_fusion_reclaim` WHERE `guid` = %u AND `count` = 0", guid);
    if (left)
        return {got > 0, "Your bags are full: " + std::to_string(left) + " more wait here for you."};
    return {true, "Everything packed for you is in your bags."};
}

void FusionMgr::StashReclaim(uint32 guid, uint32 item, uint32 count)
{
    if (!guid || !item || !count)
        return;
    // At once, as CollectReclaim reads and takes from it at once: what was packed is there to
    // collect the moment the window says so, and never counted on a stale row.
    CharacterDatabase.DirectPExecute("INSERT INTO `character_fusion_reclaim` (`guid`, `item`, `count`) VALUES (%u, %u, %u) "
        "ON DUPLICATE KEY UPDATE `count` = `count` + VALUES(`count`)", guid, item, count);
}

// ── Upkeep ──────────────────────────────────────────────────────────────────────────────────

void FusionMgr::ForgetPlotRows(uint32 plotId)
{
    CharacterDatabase.DirectPExecute("DELETE FROM `character_fusion_plot` WHERE `id` = %u", plotId);
    CharacterDatabase.DirectPExecute("DELETE FROM `character_fusion_plot_permit` WHERE `plot` = %u", plotId);
}

uint32 FusionMgr::SweepPlots(Map* map)
{
    std::vector<uint32> lapsed, waiting;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        lapsed = m_plots.Lapse(Now());
        m_plotsToPack.insert(m_plotsToPack.end(), lapsed.begin(), lapsed.end());
        // This sweep takes the ones no other is packing. They stay on the list, so nobody claims
        // a plot while its pieces are still being packed, until each is done.
        for (uint32 id : m_plotsToPack)
            if (std::find(m_plotsPacking.begin(), m_plotsPacking.end(), id) == m_plotsPacking.end())
            {
                waiting.push_back(id);
                m_plotsPacking.push_back(id);
            }
    }
    for (uint32 id : lapsed)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: plot %u lapsed; its pieces are packed for their builders.", id);
        ForgetPlotRows(id);
    }
    // Pack each here when its map is this one, or isn't loaded (nobody sees its pieces), or at
    // startup; else it waits for a player on its map.
    std::vector<uint32> done;
    for (uint32 id : waiting)
    {
        Plots::District const* d = PlotGrid().DistrictOf(id);
        uint32 const mapId = d ? d->map : 0;
        if (!map || !d || map->GetId() == mapId || !sMapMgr.FindMap(mapId))
        {
            PackPlot(map && map->GetId() == mapId ? map : nullptr, id);
            done.push_back(id);
        }
    }
    uint32 const packed = uint32(done.size());
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        auto gone = [&](uint32 id) { return std::find(done.begin(), done.end(), id) != done.end(); };
        m_plotsToPack.erase(std::remove_if(m_plotsToPack.begin(), m_plotsToPack.end(), gone), m_plotsToPack.end());
        // Released, packed or not: the rest wait for a player on their map.
        m_plotsPacking.erase(std::remove_if(m_plotsPacking.begin(), m_plotsPacking.end(), [&](uint32 id) {
            return std::find(waiting.begin(), waiting.end(), id) != waiting.end(); }), m_plotsPacking.end());
        m_plotsPending = uint32(m_plotsToPack.size());
    }
    if (!lapsed.empty() || packed)
        ++m_plotGeneration;
    return uint32(lapsed.size());
}

bool FusionMgr::PlotBeingPacked(uint32 plotId) const
{
    std::lock_guard<std::mutex> lock(m_plotLock);
    return std::find(m_plotsToPack.begin(), m_plotsToPack.end(), plotId) != m_plotsToPack.end();
}

void FusionMgr::PackPlot(Map* map, uint32 plotId)
{
    using Fusion::Build::Piece;
    Plots::District const* district = PlotGrid().DistrictOf(plotId);
    uint32 const mapId = district ? district->map : 0;
    std::vector<Piece> pieces;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(mapId);
        if (it != m_pieces.end())
            for (Piece const& p : it->second)
                if (Plots::PlotOf(PlotGrid(), p) == plotId)
                    pieces.push_back(p);
    }
    // The pieces' rows go first, at once, then the stash fills: a crash between the two loses a
    // plot's pieces rather than paying them out twice (packed again at the next start).
    if (!pieces.empty())
    {
        std::ostringstream ids;
        for (size_t i = 0; i < pieces.size(); ++i)
            ids << (i ? "," : "") << pieces[i].id;
        CharacterDatabase.DirectPExecute("DELETE FROM `fusion_build_piece` WHERE `id` IN (%s)", ids.str().c_str());
    }
    ForgetPlotRows(plotId);
    // The plot's shared stash to its builder (FusionFurniture.cpp).
    PackStash(plotId, pieces);
    // Every material back to each piece's builder: nothing on a lapsed plot is destroyed.
    for (Plots::Reclaim const& r : Plots::Pack(pieces))
        StashReclaim(r.guid, r.item, r.count);
    if (map && map->GetId() == mapId)
    {
        // In the map's own update: the pieces leave the world for those who see them.
        for (Piece const& p : pieces)
            RemovePieces(map, p.id, Fusion::Build::RemoveCause::TakenDown);
    }
    else
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(mapId);
        if (it != m_pieces.end())
            it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
                [&](Piece const& q) { return Plots::PlotOf(PlotGrid(), q) == plotId; }), it->second.end());
    }
}

Plots::Registry FusionMgr::PlotsCopy() const
{
    std::lock_guard<std::mutex> lock(m_plotLock);
    return m_plots;
}

bool FusionMgr::RespawnInSite(Player* player)
{
    if (!m_enabled || player->GetTransport())
        return false;
    Plots::Site const* s = PlotGrid().SiteAt(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), kSiteGhostNear);
    if (!s)
        return false;
    // No graveyard serves the sites (WORLD_BUILDING.md §5.3): the nearest stop has a spirit healer.
    Plots::Spot const at = Plots::NearestStop(*s, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    player->TeleportTo(s->map, at.x, at.y, at.z, at.o, TELE_TO_NOT_UNSUMMON_PET);
    return true;
}

// ── The GM command ──────────────────────────────────────────────────────────────────────────

std::string FusionMgr::ConstructionReport(Player* player)
{
    FusionPlayerData const& d = player->GetFusionConst();
    std::ostringstream o;
    o << "Fusion: Construction " << TierInfo(d.constructionTier).name << " " << d.constructionSkill << "/" << Cap(d.constructionTier) << ".";
    uint32 const here = PlotGrid().PlotAt(player->GetMapId(), player->GetPositionX(), player->GetPositionY());
    Plots::Registry const r = PlotsCopy();
    int64 const t = Now();
    if (here)
    {
        Plots::Plot const* p = r.Find(here);
        o << " Standing on " << PlotName(here) << " (" << PlotBounds(here) << "): ";
        if (!p)
            o << "for sale.";
        else
            o << "owned by account " << p->ownerAccount << " (" << CharacterName(p->ownerGuid) << "), "
              << (Plots::StandingOf(*p, t) == Plots::Standing::Paid ? "paid" : Plots::StandingOf(*p, t) == Plots::Standing::Overdue ? "overdue" : "lapsed")
              << " until " << TimeToTimestampStr(time_t(p->paidUntil)) << ".";
    }
    else if (PlotGrid().InDistrict(player->GetMapId(), player->GetPositionX(), player->GetPositionY()))
        o << " On a homestead district's road.";
    if (Plots::Site const* s = PlotGrid().SiteAt(player->GetMapId(), player->GetPositionX(), player->GetPositionY()))
        o << " In site " << uint32(s->id) << ", " << s->name << " (floor z " << s->floorZ << ", ceiling z " << s->ceilingZ << ").";
    if (Plots::Plot const* mine = r.OwnedBy(player->GetSession()->GetAccountId()))
        o << " This account owns " << PlotName(mine->id) << ".";
    o << " " << r.plots.size() << " plots owned on the realm.";
    return o.str();
}

bool ChatHandler::HandleFusionConstructionCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player) || !player->HasFusion())
    {
        SendSysMessage("Fusion: only an admin may inspect or set Construction.");
        SetSentErrorMessage(true);
        return false;
    }
    std::istringstream in(args ? args : "");
    std::string what;
    in >> what;
    FusionPlayerData const& d = player->GetFusionConst();
    if (what == "skill" || what == "tier")
    {
        long value = -1;
        in >> value;
        if (value < 0 || (what == "tier" && value > long(Tier::Artisan)) || (what == "skill" && value > 300))
        {
            SendSysMessage("Usage: .fusion construction skill <0-300> | tier <0-4>");
            SetSentErrorMessage(true);
            return false;
        }
        if (what == "tier")
            sFusionMgr.SetConstruction(player, Tier(value), d.constructionTier == Tier::None ? kStartSkill : d.constructionSkill, false);
        else
        {
            // A skill above the tier's cap brings the tier that holds it.
            Tier tier = d.constructionTier == Tier::None ? Tier::Apprentice : d.constructionTier;
            while (Cap(tier) < uint16(value) && tier != Tier::Artisan)
                tier = Next(tier);
            sFusionMgr.SetConstruction(player, tier, uint16(value), false);
        }
    }
    else if (what == "sweep")
        PSendSysMessage("Fusion: %u lapsed plots packed.", sFusionMgr.SweepPlots(player->GetMap()));
    else if (what == "decay")
    {
        // Testing decay: the pieces within 60 yards age `value` days (1 by default), and this
        // map's decay is worked out now.
        long value = 0;
        in >> value;
        uint32 const n = sFusionMgr.AgePiecesNear(player, int32(value > 0 ? value : 1));
        PSendSysMessage("Fusion: %u pieces aged %d days; decay worked out.", n, int32(value > 0 ? value : 1));
    }
    else if (what == "overdue" || what == "expire")
    {
        // Testing upkeep: this account's plot runs out a day ago, or past its grace (the next
        // sweep packs it).
        int64 const until = Now() - (what == "overdue" ? 24 * 3600 : int64(Plots::kGraceWeeks + 1) * Plots::kWeek);
        if (!sFusionMgr.SetPlotPaidUntil(player->GetSession()->GetAccountId(), until))
        {
            SendSysMessage("Fusion: this account has no plot.");
            SetSentErrorMessage(true);
            return false;
        }
    }
    else if (!what.empty())
    {
        SendSysMessage("Usage: .fusion construction [skill <0-300> | tier <0-4> | sweep | decay <days> | overdue | expire]");
        SetSentErrorMessage(true);
        return false;
    }
    SendSysMessage(sFusionMgr.ConstructionReport(player).c_str());
    return true;
}

bool FusionMgr::SetPlotPaidUntil(uint32 account, int64 until)
{
    uint32 id = 0;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        if (Plots::Plot const* owned = m_plots.OwnedBy(account))
        {
            id = owned->id;
            m_plots.Find(id)->paidUntil = until;
        }
    }
    if (!id)
        return false;
    CharacterDatabase.PExecute("UPDATE `character_fusion_plot` SET `paid_until` = %lld WHERE `id` = %u", (long long)until, id);
    ++m_plotGeneration;
    return true;
}
