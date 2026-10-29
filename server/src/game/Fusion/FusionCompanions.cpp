/*
 * World of Modern Warfare: battle companions (fusion/Companions.h) in the game. A companion is a
 * rare item; using it summons a guardian that follows its owner and fights what the owner fights.
 * Its level, experience and stage belong to the item instance (character_fusion_companion, by
 * item guid), so a levelled companion is traded, mailed and sold with all its progress. It earns
 * experience from the kills it takes part in, never fights above its owner's level, evolves into
 * larger forms, and can fight another player's companion in a companion duel. See
 * src/fusion/docs/COMPANIONS.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "fusion/ClassTuning.h"
#include "FusionMgr.h"

#include "fusion/CompanionIndex.h"
#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"

#include "AuctionHouseMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStores.h"
#include "Item.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "PetAI.h"
#include "Player.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "VMapFactory.h"
#include "World.h"
#include "WorldSession.h"

#include <ctime>
#include <sstream>

namespace Companions = Fusion::Companions;

// FusionCompanionBar.cpp: `.fusion pet`'s trait list.
uint32 FusionCompanionTraitsFromText(std::string const& text, uint8& twin, uint8& gen, bool& any);

// A companion duel as the game runs it: the rules' state machine, the two owners and their
// companions, and where the fight began.
struct FusionCompanionDuel
{
    Companions::CompanionDuel rules;
    std::array<uint64, 2> owner{};      // challenger, challenged (player guids)
    std::array<uint64, 2> pet{};        // their companions, fixed when the challenge is accepted
    Fusion::Vec3 center;
    uint32 mapId = 0;                   // where it was challenged: an owner gone to another map fled
    std::array<float, 2> preHealth{{1.f, 1.f}};   // each companion's health share before the fight
    uint32 lastDriveMs = 0;
    Companions::DuelPhase told = Companions::DuelPhase::Idle;
    bool fighting = false;              // the two companions are set on each other
    bool over = false;
};

namespace
{
    // How often a player's companion is looked at (the bar, its item, the owner's level).
    constexpr uint32 kLookMs = 500;
    // How often the bags' fainted companions are looked at (one whose wait is over says so).
    constexpr uint32 kFaintLookMs = 5000;
    // A fainted companion's item used again this soon gives it a Reviving Draught.
    constexpr uint32 kDraughtAskMs = 10000;

    Fusion::Vec3 PosOf(WorldObject const* o) { return {o->GetPositionX(), o->GetPositionY(), o->GetPositionZ()}; }

    // 1.12's creature type and rank as the rules key them (an instance boss by its script).
    Fusion::CreatureType TypeOf(Creature const* c)
    {
        uint32 const t = c->GetCreatureType();
        return t <= 11 ? Fusion::CreatureType(t) : Fusion::CreatureType::NotSpecified;
    }

    Fusion::CreatureRank RankOf(Creature const* c)
    {
        CreatureInfo const* ci = c->GetCreatureInfo();
        uint32 const rank = ci ? ci->rank : 0;
        Fusion::CreatureRank r = rank <= 4 ? Fusion::CreatureRank(rank) : Fusion::CreatureRank::Normal;
        Map const* map = c->FindMap();
        if (map && map->IsDungeon() && (rank == CREATURE_ELITE_ELITE || rank == CREATURE_ELITE_RAREELITE)
            && c->GetScriptName().rfind("boss_", 0) == 0)
            r = Fusion::CreatureRank::DungeonBoss;
        return r;
    }

    // The server's game clock as a local hour, for night and day evolutions.
    uint8 GameHour()
    {
        time_t const t = sWorld.GetGameTime();
        tm local{};
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        return uint8(local.tm_hour);
    }

    char const* TypeName(Fusion::CreatureType t)
    {
        switch (t)
        {
            case Fusion::CreatureType::Beast: return "beasts";
            case Fusion::CreatureType::Dragonkin: return "dragonkin";
            case Fusion::CreatureType::Demon: return "demons";
            case Fusion::CreatureType::Elemental: return "elementals";
            case Fusion::CreatureType::Giant: return "giants";
            case Fusion::CreatureType::Undead: return "undead";
            case Fusion::CreatureType::Humanoid: return "humanoids";
            case Fusion::CreatureType::Critter: return "critters";
            case Fusion::CreatureType::Mechanical: return "mechanicals";
            default: return "creatures";
        }
    }

    std::string ItemName(uint32 entry)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
        return proto ? proto->Name1 : "a catalyst";
    }

    // Why a companion cannot evolve, as a chat line (the bar shows the same as a tooltip).
    std::string BlockText(Companions::EvolveBlock block, Companions::EvolutionRule const* rule,
        Companions::CompanionState const& s)
    {
        using Companions::EvolveBlock;
        switch (block)
        {
            case EvolveBlock::None: return "it is ready to evolve";
            case EvolveBlock::FinalStage: return "it is fully grown";
            case EvolveBlock::NotSummoned: return "it must be out: summon it first";
            case EvolveBlock::InCombat: return "not while you or it are in combat";
            case EvolveBlock::InDuel: return "not during a companion duel";
            case EvolveBlock::LevelTooLow:
                return "it needs level " + std::to_string(rule ? rule->level : 0) + " (and you too)";
            case EvolveBlock::WrongPlace:
            {
                std::string where;
                if (rule)
                    for (uint32 id : rule->areas)
                        if (AreaEntry const* a = AreaEntry::GetById(id))
                            where += (where.empty() ? "" : ", ") + std::string(a->Name);
                return "it must be in the right place" + (where.empty() ? std::string() : ": " + where);
            }
            case EvolveBlock::WrongTime:
                return rule && rule->condition == Companions::Condition::Night
                    ? "it evolves only at night (20:00 to 06:00 server time)"
                    : "it evolves only by day (06:00 to 20:00 server time)";
            case EvolveBlock::NeedKills:
                return rule ? "it needs " + std::to_string(rule->conditionCount > s.evolveKills ? rule->conditionCount - s.evolveKills : 0)
                        + " more kills of " + TypeName(rule->conditionType) + " while it is out"
                    : "it needs more kills";
            case EvolveBlock::MissingItem:
                return rule ? "it needs " + std::to_string(std::max<uint8>(rule->itemCount, 1)) + " " + ItemName(rule->item)
                    : "it needs a catalyst";
        }
        return "it cannot evolve now";
    }

    // The ids in a comma-separated list column.
    std::vector<uint32_t> IdList(std::string const& text)
    {
        std::vector<uint32_t> out;
        std::stringstream in(text);
        std::string part;
        while (std::getline(in, part, ','))
            if (!part.empty())
                out.push_back(uint32_t(std::strtoul(part.c_str(), nullptr, 10)));
        return out;
    }

    uint64 PairKey(uint64 a, uint64 b)
    {
        uint32 const lo = uint32(std::min(ObjectGuid(a).GetCounter(), ObjectGuid(b).GetCounter()));
        uint32 const hi = uint32(std::max(ObjectGuid(a).GetCounter(), ObjectGuid(b).GetCounter()));
        return (uint64(hi) << 32) | lo;
    }
}

// ── Loading and saving ──────────────────────────────────────────────────────────────────────

// The vanilla pet items are retired (31_vanilla_companions.sql): every copy a character still has,
// in the bags, the bank, the mail or the auction house, becomes its companion item before anyone
// logs in (World.cpp calls this before the auction house loads its items). A copy with no
// companion row is a fresh stage-1 companion; a second run finds nothing to swap.
void FusionMgr::SwapRetiredPetItems()
{
    for (std::string const& sql : Companions::VanillaPetSwapSql())
        CharacterDatabase.DirectExecute(sql.c_str());
}

void FusionMgr::LoadCompanions()
{
    m_companionSpecies.clear();
    // 08_companion_lines.sql adds ride_display; without it every line rides its spell's display.
    // 29_companion_looks.sql adds stageN_display; without them each stage wears its creature's.
    auto hasColumn = [](char const* column)
    {
        return std::unique_ptr<QueryResult>(WorldDatabase.PQuery(
            "SHOW COLUMNS FROM `fusion_companion_species` LIKE '%s'", column)) != nullptr;
    };
    bool const rideDisplay = hasColumn("ride_display");
    bool const stageDisplays = hasColumn("stage1_display") && hasColumn("stage2_display") && hasColumn("stage3_display");
    std::string const query = std::string(
        "SELECT `id`, `name`, `item_entry`, `family`, `role`, "
        "`stage1_creature`, `stage1_scale`, `stage2_creature`, `stage2_scale`, `stage3_creature`, `stage3_scale`, "
        "`evo1_level`, `evo1_item`, `evo1_item_count`, `evo1_areas`, `evo1_condition`, `evo1_condition_type`, `evo1_condition_count`, "
        "`evo2_level`, `evo2_item`, `evo2_item_count`, `evo2_areas`, `evo2_condition`, `evo2_condition_type`, `evo2_condition_count`, "
        "`drop_entries`, `drop_type`, `drop_family`, `ride_stage`, `ride_spell`, `ride_swift_spell`, ") +
        (rideDisplay ? "`ride_display`, " : "0, ") +
        (stageDisplays ? "`stage1_display`, `stage2_display`, `stage3_display` " : "0, 0, 0 ") +
        "FROM `fusion_companion_species`";
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(query.c_str()))
    {
        do
        {
            Field* f = r->Fetch();
            Companions::CompanionSpecies sp;
            sp.id = f[0].GetUInt16();
            sp.name = f[1].GetCppString();
            sp.item = f[2].GetUInt32();
            sp.family = Companions::Family(f[3].GetUInt8());
            sp.role = Companions::Role(std::min<uint8>(f[4].GetUInt8(), 2));
            sp.stageCount = 0;
            // A zero creature ends the stages.
            for (int i = 0; i < Companions::kMaxStages; ++i)
            {
                uint32 const creature = f[5 + 2 * i].GetUInt32();
                if (!creature || sp.stageCount != i)
                    break;
                if (!sObjectMgr.GetCreatureTemplate(creature))
                {
                    sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "fusion_companion_species %u: stage %d creature %u does not exist.",
                        uint32(sp.id), i + 1, creature);
                    break;
                }
                sp.stages[i].creature = creature;
                sp.stages[i].scale = f[6 + 2 * i].GetFloat() > 0.f ? f[6 + 2 * i].GetFloat() : 1.f;
                // The look table's display; one the client does not have is no look.
                uint32 const display = f[32 + i].GetUInt32();
                if (display && !sCreatureDisplayInfoStore.LookupEntry(display))
                    sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "fusion_companion_species %u: stage %d display %u does not exist.",
                        uint32(sp.id), i + 1, display);
                else
                    sp.stages[i].display = display;
                ++sp.stageCount;
            }
            if (!sp.stageCount)
                continue;
            for (int e = 0; e < Companions::kMaxStages - 1; ++e)
            {
                int const b = 11 + 7 * e;
                Companions::EvolutionRule& rule = sp.evolutions[e];
                rule.level = f[b].GetUInt8();
                rule.item = f[b + 1].GetUInt32();
                rule.itemCount = f[b + 2].GetUInt8();
                rule.areas = IdList(f[b + 3].GetCppString());
                rule.condition = Companions::Condition(std::min<uint8>(f[b + 4].GetUInt8(), 3));
                rule.conditionType = Fusion::CreatureType(f[b + 5].GetUInt8());
                rule.conditionCount = f[b + 6].GetUInt16();
                // A rule into a stage that does not exist is no rule.
                if (e + 1 >= sp.stageCount)
                    rule = Companions::EvolutionRule{};
            }
            sp.drops.entries = IdList(f[25].GetCppString());
            sp.drops.type = Fusion::CreatureType(f[26].GetUInt8());
            sp.drops.petFamily = f[27].GetUInt8();
            // The mount stage: ridden with a vanilla mount spell (its model, speed and rules).
            uint8 const rideStage = f[28].GetUInt8();
            if (rideStage >= 1 && rideStage <= sp.stageCount && f[29].GetUInt32())
            {
                Companions::StageInfo& stage = sp.stages[rideStage - 1];
                stage.rideable = true;
                stage.mountSpell = f[29].GetUInt32();
                stage.swiftMountSpell = f[30].GetUInt32();
                stage.rideDisplay = f[31].GetUInt32();
            }
            m_companionSpecies.push_back(std::move(sp));
        } while (r->NextRow());
    }
    m_companionItems.clear();
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(
            "SELECT `item_entry`, `species`, `stage`, `level` FROM `fusion_companion_item`"))
    {
        do
        {
            Field* f = r->Fetch();
            m_companionItems.push_back({f[0].GetUInt32(), f[1].GetUInt16(), f[2].GetUInt8(), f[3].GetUInt8()});
        } while (r->NextRow());
    }
    if (m_companionSpecies.empty())
    {
        // The world SQL is not applied: the library's starter set keeps companions working.
        m_companionSpecies = Companions::StarterSpecies();
        m_companionItems = Companions::StarterItems();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: no fusion_companion_species rows; using the %u starter species "
            "(apply sql/custom/azeroth_warfare/02_companions.sql for the items).", uint32(m_companionSpecies.size()));
    }

    // The progress of every companion item, by item guid: a destroyed item's row goes with it
    // (Item::DeleteAllFromDB); this sweeps any a crash left behind before the guids are reused.
    // Paid companion duel wins today, per account (Companions::PayDuelWin).
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `account_fusion_companion_duel` ("
        "`account` int(10) unsigned NOT NULL, `day` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'unix days', "
        "`wins` int(10) unsigned NOT NULL DEFAULT '0', "
        "PRIMARY KEY (`account`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_companion` ("
        "`item_guid` int(10) unsigned NOT NULL COMMENT 'item_instance.guid', "
        "`species` smallint(5) unsigned NOT NULL, `stage` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "`level` tinyint(3) unsigned NOT NULL DEFAULT '1', `xp` int(10) unsigned NOT NULL DEFAULT '0', "
        "`evolve_kills` smallint(5) unsigned NOT NULL DEFAULT '0', "
        "PRIMARY KEY (`item_guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    // Traits, ability picks and breeding (fusion/CompanionTraits.h, CompanionMoves.h).
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `character_fusion_companion` "
        "ADD COLUMN IF NOT EXISTS `traits` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT 'Companions::Trait bitmask', "
        "ADD COLUMN IF NOT EXISTS `rarity` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "ADD COLUMN IF NOT EXISTS `twin_stage` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "ADD COLUMN IF NOT EXISTS `picks` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'one byte a stage, 0 none 1 2', "
        "ADD COLUMN IF NOT EXISTS `breed_count` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "ADD COLUMN IF NOT EXISTS `breed_ready` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'unix time', "
        "ADD COLUMN IF NOT EXISTS `generation` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "ADD COLUMN IF NOT EXISTS `parent_a` smallint(5) unsigned NOT NULL DEFAULT '0', "
        "ADD COLUMN IF NOT EXISTS `parent_b` smallint(5) unsigned NOT NULL DEFAULT '0', "
        "ADD COLUMN IF NOT EXISTS `hybrid` smallint(5) unsigned NOT NULL DEFAULT '0'");
    CharacterDatabase.DirectExecute(
        "DELETE c FROM `character_fusion_companion` c LEFT JOIN `item_instance` i ON i.`guid` = c.`item_guid` "
        "WHERE i.`guid` IS NULL");
    // The Bestiary's seen set (FusionCompanionIndex.cpp, fusion/CompanionIndex.h).
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_companion_seen` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'characters.guid', "
        "`species` smallint(5) unsigned NOT NULL, "
        "`captured` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT '0 seen (targeted), 1 caught (carried)', "
        "PRIMARY KEY (`guid`, `species`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "DELETE s FROM `character_fusion_companion_seen` s LEFT JOIN `characters` c ON c.`guid` = s.`guid` "
        "WHERE c.`guid` IS NULL");
    // The companion a character had out when it logged out (or died): it comes back at login.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_companion_out` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'characters.guid', "
        "`item_guid` int(10) unsigned NOT NULL COMMENT 'item_instance.guid of the companion out', "
        "PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "DELETE o FROM `character_fusion_companion_out` o LEFT JOIN `characters` c ON c.`guid` = o.`guid` "
        "LEFT JOIN `item_instance` i ON i.`guid` = o.`item_guid` WHERE c.`guid` IS NULL OR i.`guid` IS NULL");
    // Each companion item's wounds and faint (fusion/CompanionRest.h, FusionCompanionRevive.cpp):
    // a row only for one hurt or fainted. Made here, as the tables above are.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_companion_rest` ("
        "`item_guid` int(10) unsigned NOT NULL COMMENT 'item_instance.guid', "
        "`health` smallint(5) unsigned NOT NULL DEFAULT '1000' COMMENT 'health share when put away, thousandths', "
        "`away_at` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'unix time it went back into its item', "
        "`fainted_at` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'unix time it fainted, 0 none', "
        "PRIMARY KEY (`item_guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "DELETE r FROM `character_fusion_companion_rest` r LEFT JOIN `item_instance` i ON i.`guid` = r.`item_guid` "
        "WHERE i.`guid` IS NULL");
    m_companionCreatures = Companions::Index::MapCreatures(m_companionSpecies);
    std::lock_guard<std::mutex> lock(m_companionLock);
    m_companionRecovery.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query(
            "SELECT `item_guid`, `health`, `away_at`, `fainted_at` FROM `character_fusion_companion_rest`"))
    {
        do
        {
            Field* f = r->Fetch();
            Companions::RecoveryRow row;
            row.healthPermille = f[1].GetUInt16();
            row.awayAt = f[2].GetUInt32();
            row.faintedAt = f[3].GetUInt32();
            m_companionRecovery[f[0].GetUInt32()] = Companions::FromRow(row);
        } while (r->NextRow());
    }
    m_companionStates.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query(
            "SELECT `item_guid`, `species`, `stage`, `level`, `xp`, `evolve_kills`, `traits`, `rarity`, `twin_stage`, `picks`, "
            "`breed_count`, `breed_ready`, `generation`, `parent_a`, `parent_b`, `hybrid` FROM `character_fusion_companion`"))
    {
        do
        {
            Field* f = r->Fetch();
            Companions::CompanionState s;
            s.species = f[1].GetUInt16();
            s.stage = f[2].GetUInt8();
            s.level = std::max<uint8>(1, std::min<uint8>(f[3].GetUInt8(), Companions::kMaxLevel));
            s.xp = f[4].GetUInt32();
            s.evolveKills = f[5].GetUInt16();
            s.traits = uint32(f[6].GetUInt64());
            s.rarity = f[7].GetUInt8();
            s.twinStage = f[8].GetUInt8();
            uint32 const picks = f[9].GetUInt32();
            for (int i = 0; i < Companions::kMaxStages; ++i)
                s.picks[i] = uint8(std::min<uint32>(picks >> (8 * i) & 0xFF, 2));
            s.breedCount = f[10].GetUInt8();
            s.breedReadyAt = f[11].GetUInt32();
            s.generation = f[12].GetUInt8();
            s.parentA = f[13].GetUInt16();
            s.parentB = f[14].GetUInt16();
            s.hybrid = f[15].GetUInt16();
            m_companionStates[f[0].GetUInt32()] = s;
        } while (r->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u companion species, %u companions with progress, %u hurt or fainted "
        "(a faint lasts %u min).", uint32(m_companionSpecies.size()), uint32(m_companionStates.size()),
        uint32(m_companionRecovery.size()), m_faint.recoverSeconds / 60);
    // The auction house's items loaded before the companions did: stamp them now.
    for (auto const& [guid, item] : sAuctionMgr.GetAItems())
        StampCompanionItem(item);
}

void FusionMgr::StampCompanionItem(Item* item)
{
    if (!item || !Companions::IsCompanionItem(item->GetEntry()) || m_companionSpecies.empty())
        return;
    // A new companion item rolls its traits the first time it is seen (fusion/CompanionTraits.h).
    bool known;
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        known = m_companionStates.count(item->GetGUIDLow()) != 0;
    }
    if (!known && item->GetGUIDLow())
        RollFreshCompanion(item);
    Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    if (s.species && item->GetUInt32Value(ITEM_FIELD_PROPERTY_SEED) != Companions::ItemSeed(s))
        item->SetUInt32Value(ITEM_FIELD_PROPERTY_SEED, Companions::ItemSeed(s));
}

Companions::CompanionSpecies const* FusionMgr::CompanionSpeciesOfItem(uint32 itemEntry) const
{
    Companions::CompanionState fresh;
    return Companions::FreshState(m_companionSpecies, m_companionItems, itemEntry, fresh);
}

Companions::CompanionState FusionMgr::CompanionStateOf(uint32 itemGuidLow, uint32 itemEntry) const
{
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        auto it = m_companionStates.find(itemGuidLow);
        if (it != m_companionStates.end())
            return it->second;
    }
    // A fresh drop: level 1, stage 0, no experience (a mount's item: its rideable stage at 40).
    Companions::CompanionState s;
    Companions::FreshState(m_companionSpecies, m_companionItems, itemEntry, s);
    return s;
}

void FusionMgr::SaveCompanionState(uint32 itemGuidLow, Companions::CompanionState const& s)
{
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        m_companionStates[itemGuidLow] = s;
    }
    uint32 const picks = uint32(s.picks[0]) | uint32(s.picks[1]) << 8 | uint32(s.picks[2]) << 16;
    CharacterDatabase.PExecute(
        "REPLACE INTO `character_fusion_companion` (`item_guid`, `species`, `stage`, `level`, `xp`, `evolve_kills`, "
        "`traits`, `rarity`, `twin_stage`, `picks`, `breed_count`, `breed_ready`, `generation`, `parent_a`, `parent_b`, `hybrid`) "
        "VALUES (%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u)",
        itemGuidLow, uint32(s.species), uint32(s.stage), uint32(s.level), s.xp, uint32(s.evolveKills),
        s.traits, uint32(s.rarity), uint32(s.twinStage), picks, uint32(s.breedCount), s.breedReadyAt,
        uint32(s.generation), uint32(s.parentA), uint32(s.parentB), uint32(s.hybrid));
}

void FusionMgr::ForgetCompanion(uint32 itemGuidLow)
{
    bool hadRest;
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        m_companionStates.erase(itemGuidLow);
        hadRest = m_companionRecovery.erase(itemGuidLow) != 0;
    }
    CharacterDatabase.PExecute("DELETE FROM `character_fusion_companion` WHERE `item_guid` = %u", itemGuidLow);
    // Its duel deck and the cards it learned (FusionCompanionBattles.cpp).
    bool hadDeck;
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        hadDeck = m_companionDecks.erase(itemGuidLow) != 0;
    }
    if (hadDeck)
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_companion_deck` WHERE `item_guid` = %u", itemGuidLow);
    if (hadRest)
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_companion_rest` WHERE `item_guid` = %u", itemGuidLow);
}

// ── The companion out ───────────────────────────────────────────────────────────────────────

Pet* FusionMgr::CompanionPet(Player* player) const
{
    if (!player->HasFusion() || !player->IsInWorld())
        return nullptr;
    FusionCompanionOut const& c = player->GetFusion().companion;
    if (c.pet.IsEmpty())
        return nullptr;
    return player->GetMap()->GetPet(c.pet);
}

Player* FusionMgr::CompanionOwner(Unit* unit) const
{
    Creature* c = unit ? unit->ToCreature() : nullptr;
    if (!c || !c->IsPet() || !static_cast<Pet*>(c)->IsFusionCompanion() || !c->GetOwnerGuid().IsPlayer())
        return nullptr;
    Player* owner = c->GetMap()->GetPlayer(c->GetOwnerGuid());
    if (!owner || !owner->HasFusion() || owner->GetFusion().companion.pet != c->GetObjectGuid())
        return nullptr;
    return owner;
}

Companions::EvolveContext FusionMgr::CompanionContext(Player* player, Companions::CompanionSpecies const& sp,
    Companions::CompanionState const& s) const
{
    Companions::EvolveContext ctx;
    ctx.ownerLevel = uint8(player->GetLevel());
    Pet* pet = CompanionPet(player);
    ctx.summoned = pet != nullptr;
    ctx.inCombat = player->IsInCombat() || (pet && pet->IsInCombat());
    ctx.inDuel = CompanionDuelHolds(player->GetObjectGuid().GetRawValue());
    ctx.zoneId = player->GetZoneId();
    ctx.areaId = player->GetAreaId();
    if (Companions::EvolutionRule const* rule = Companions::NextRule(sp, s))
        if (rule->item)
            ctx.itemsHeld = player->GetItemCount(rule->item);
    ctx.hour = GameHour();
    return ctx;
}

void FusionMgr::BuildCompanion(Pet* pet, Companions::CompanionSpecies const& sp, Companions::CompanionState const& s,
    uint8 level, bool fullHealth, bool bond)
{
    // The rules' numbers, not the display creature's: a dragon's model, a pup's purse. Health
    // and armor are set through the base modifiers, so the pet's stat system keeps them (and
    // still adds a buff on top) whenever it recomputes.
    Companions::CompanionStats st = Companions::StatsWithTraits(sp, s, level);
    // A hunter's companion is the hunter pet it stands in for (fusion/ClassTuning.h, the bond);
    // never in a companion duel, where the rules' own stats meet.
    if (Unit* owner = pet->GetOwner(); bond && owner && Fusion::ClassTuning::HunterBond(owner->GetClass()))
    {
        st.health = uint32(float(st.health) * Fusion::ClassTuning::kHunterBondHealth + 0.5f);
        st.damageMin *= Fusion::ClassTuning::kHunterBondDamage;
        st.damageMax *= Fusion::ClassTuning::kHunterBondDamage;
    }
    float const share = pet->GetMaxHealth() ? float(pet->GetHealth()) / float(pet->GetMaxHealth()) : 1.f;
    pet->SetLevel(level);
    pet->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, st.damageMin);
    pet->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, st.damageMax);
    pet->SetAttackTime(BASE_ATTACK, st.attackMs);
    // A ranged basic attack (a Shoot or a Throw, Companions::RangedAttackSpell) hits for one
    // basic hit off the ranged weapon damage, not the display creature's.
    pet->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, st.damageMin);
    pet->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, st.damageMax);
    pet->SetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE, st.damageMin);
    pet->SetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE, st.damageMax);
    pet->SetAttackTime(RANGED_ATTACK, st.attackMs);
    pet->UpdateAllStats();
    pet->UpdateMaxHealth();
    pet->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE,
        pet->GetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE) + float(st.health) - float(pet->GetMaxHealth()));
    pet->UpdateMaxHealth();
    pet->UpdateArmor();
    pet->SetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE,
        pet->GetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE) + float(st.armor) - float(pet->GetArmor()));
    pet->UpdateArmor();
    pet->UpdateDamagePhysical(BASE_ATTACK);
    pet->SetHealth(fullHealth ? pet->GetMaxHealth() : std::max<uint32>(1, uint32(share * float(pet->GetMaxHealth()) + 0.5f)));
}

bool FusionMgr::SummonCompanion(Player* player, Item* item)
{
    Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry());
    if (!sp)
        return false;
    // A fainted companion stays in its item (FusionCompanionRevive.cpp): callers say why.
    Companions::Recovery const recovery = SettleCompanion(player, item, true);
    uint32 const faintWait = CompanionFaintWait(item);
    if (Companions::IsFainted(recovery, uint32(time(nullptr)), faintWait))
        return false;
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    s.species = sp->id;
    s.stage = std::min<uint8>(s.stage, uint8(sp->stageCount - 1));
    uint8 const ownerLevel = uint8(player->GetLevel());
    // A bar banked while its owner was lower is paid now that the owner has caught up.
    if (Companions::GainXp(s, 0, ownerLevel).levels)
        SaveCompanionState(item->GetGUIDLow(), s);
    StampCompanionItem(item);
    uint8 const level = Companions::EffectiveLevel(s, ownerLevel);
    CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(sp->stages[s.stage].creature);
    if (!cInfo)
        return false;

    Map* map = player->GetMap();
    // The companion takes the pet slot, the one every class has now: a hunter's or warlock's
    // old pet still on record (from before class pets went) is put away first, kept in its
    // stable record but never brought back as current. A companion of ours the bookkeeping lost
    // (it is no longer the one out) goes too, so two never stand side by side.
    DismissOldPet(player);
    if (Pet* stray = player->GetPet(); stray && stray->IsFusionCompanion() && stray->IsInWorld())
        stray->Unsummon(PET_SAVE_AS_DELETED, player);
    if (!player->GetPetGuid().IsEmpty())
        return false;
    Pet* pet = new Pet(SUMMON_PET);
    pet->SetFusionCompanion(true);
    // At its follow spot behind its owner, as a hunter's pet appears, never in its owner.
    CreatureCreatePos pos(player, player->GetOrientation(), Companions::FollowDistance(0.5f), Companions::kFollowAngle);
    uint32 const petNumber = sObjectMgr.GeneratePetNumber();
    if (!pet->Create(map->GenerateLocalLowGuid(HIGHGUID_PET), pos, cInfo, petNumber))
    {
        delete pet;
        return false;
    }
    pet->SetSummonPoint(pos);
    pet->SetOwnerGuid(player->GetObjectGuid());
    pet->SetCreatorGuid(player->GetObjectGuid());
    pet->SetFactionTemplateId(player->GetFactionTemplateId());
    pet->SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, 0);
    pet->SetUInt32Value(UNIT_NPC_FLAGS, 0);     // a vendor's model is still only a companion
    pet->InitStatsForLevel(level, player);
    pet->GetCharmInfo()->SetPetNumber(petNumber, false);
    // The display creature's flags must not make it untouchable or harmless.
    pet->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_IMMUNE_TO_PLAYER |
        UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_NON_ATTACKABLE_2 | UNIT_FLAG_PACIFIED | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_SKINNABLE);
    // Always the same look for the same companion: its stage's own display (the look table),
    // else the template's display picked by the item (never a new roll at every summon), a
    // hybrid's other line where the two share a model.
    uint32 const display = CompanionDisplay(*sp, s, item->GetGUIDLow());
    if (display)
    {
        pet->SetNativeDisplayId(display);
        pet->SetDisplayId(display);
    }
    FusionCompanionOut& c = player->GetFusion().companion;
    uint32 const now = WorldTimer::getMSTime();
    // It comes back as hurt as it went, healed for the time it rested (its item's Recovery, saved:
    // a relog or a swap is no heal), with the cooldowns it still had: the one last out's are
    // readyAtMs, any other's in the rest book (none if it was never out). Swapping companions is
    // no heal and no cooldown reset.
    float const health = std::max(0.01f, Companions::HealthAt(recovery, uint32(time(nullptr)), faintWait));
    if (c.lastItem != item->GetObjectGuid())
    {
        if (!c.lastItem.IsEmpty())
        {
            Companions::Rest went;
            went.awayMs = now;
            went.readyAtMs = c.readyAtMs;
            c.rests.Put(c.lastItem.GetRawValue(), went);
        }
        c.readyAtMs = c.rests.Take(item->GetObjectGuid().GetRawValue(), now).readyAtMs;
    }
    BuildCompanion(pet, *sp, s, level, true);
    pet->SetHealth(std::max<uint32>(1, uint32(health * float(pet->GetMaxHealth()) + 0.5f)));
    pet->SetObjectScale(CompanionScale(*sp, s, display ? display : pet->GetNativeDisplayId()));
    // Its reach and bounding radius follow the size it is drawn at.
    pet->UpdateModelData();
    pet->InitializeDefaultName();
    // PetAI whatever the template runs (a critter's, a boss script's): follow, assist, defend.
    // Its basic attack is its catalogue's: a caster or a Gunner keeps its range and shoots
    // (UpdateCompanionCombat), the rest swing, whatever the display creature's flags say.
    pet->AIM_Initialize();
    delete pet->AI();
    PetAI* ai = new PetAI(pet);
    pet->SetAI(ai);
    ai->SetFixedMeleeAttack(Companions::RangedAttackSpell(*sp, s) == 0, Companions::kRangedReach);
    // The stance its owner left it in.
    ReactStates const react = c.react == REACT_AGGRESSIVE ? REACT_AGGRESSIVE
        : c.react == REACT_PASSIVE ? REACT_PASSIVE : REACT_DEFENSIVE;
    pet->SetReactState(react);
    if (player->IsPvP())
        pet->SetPvP(true);
    map->Add(static_cast<Creature*>(pet));
    player->SetPet(pet);
    // Following from the start, as a hunter's pet does (its follow spot, pathing around what is
    // in the way); a passive or defensive one stays out of fights it is not drawn into.
    pet->GetCharmInfo()->SetCommandState(COMMAND_FOLLOW);
    pet->GetMotionMaster()->MoveFollow(player, pet->GetFollowDistance(), pet->GetFollowAngle());

    c.pet = pet->GetObjectGuid();
    c.item = item->GetObjectGuid();
    c.mapId = player->GetMapId();
    c.builtLevel = ownerLevel;
    c.duelStats = false;
    c.nextLookMs = 0;
    c.suspended = false;
    c.returnItem.Clear();
    c.seenHealth = health;
    c.lastItem = c.item;
    SaveCompanionOut(player, c.item);
    // Its abilities on the game's own pet bar (SMSG_PET_SPELLS), and the cooldowns it still has.
    SetupCompanionBar(player, pet, *sp, s);
    for (uint8 slot = 0; slot < Companions::PetBarSlot::Count; ++slot)
        if (int32 const left = int32(c.readyAtMs[slot] - now); left > 0)
        {
            Companions::Ability const a = Companions::AbilityInSlot(*sp, s, slot);
            if (a.Exists())
                player->SendSpellCooldown(a.spell, Milliseconds(left), pet->GetObjectGuid());
            SendChannel(player, Companions::kChannelCompanion, Companions::EncodeCooldown(slot, uint32(left)));
        }
    SendCompanionBar(player, true);
    SendCompanionMoves(player);
    // A pick owed (stage 1 at the first summon, or the stage it just reached): said once, not at
    // every summon, zoning and dismount.
    int const pending = Companions::PendingPick(*sp, s);
    if (pending >= 0 && (c.pickToldItem != c.item || c.pickToldStage != pending))
    {
        c.pickToldItem = c.item;
        c.pickToldStage = int8(pending);
        Companions::MovesetInfo const& m = Companions::MovesetData(Companions::MovesetOf(*sp));
        FusionCompanionTell(player).PSendSysMessage("|cffffd200Your %s can learn %s or %s: choose in the Companions window.|r",
            sp->name.c_str(), m.stages[pending][0].name, m.stages[pending][1].name);
    }
    RecordCompanionSeen(player, sp->id, Companions::Index::Seen::Captured);
    // Traits that act through its owner or its looks.
    using Companions::Trait;
    if (Companions::HasTrait(s.traits, Trait::Tracker))
    {
        uint32 spell = 1494;    // Track Beasts
        switch (Companions::FamilyType(sp->family))
        {
            case Fusion::CreatureType::Humanoid: spell = 19883; break;
            case Fusion::CreatureType::Undead: spell = 19884; break;
            case Fusion::CreatureType::Elemental: spell = 19880; break;
            case Fusion::CreatureType::Demon: spell = 19878; break;
            case Fusion::CreatureType::Dragonkin: spell = 19879; break;
            case Fusion::CreatureType::Mechanical: spell = 19885; break;
            default: break;
        }
        if (sSpellMgr.GetSpellEntry(spell))
        {
            // Recorded even when already on (kept over a logout), so putting it away ends it.
            if (!player->HasAura(spell))
                player->CastSpell(player, spell, true);
            c.trackingSpell = spell;
        }
    }
    if (Companions::HasTrait(s.traits, Trait::Gunner) && Companions::TraitEligible(Trait::Gunner, *sp))
        pet->SetVirtualItem(RANGED_ATTACK, Fusion::NpcGunItemFor(level));
    return true;
}

uint32 FusionMgr::CompanionDisplay(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s,
    uint32 itemGuidLow) const
{
    if (uint32 const own = Companions::LookDisplay(m_companionSpecies, sp, s))
        return own;
    CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(Companions::LookCreature(m_companionSpecies, sp, s));
    if (!ci)
        ci = sObjectMgr.GetCreatureTemplate(sp.stages[std::min<uint8>(s.stage, uint8(sp.stageCount - 1))].creature);
    if (!ci)
        return 0;
    // The models the template may show (one with a weight, when the template weighs them).
    std::vector<uint32> models;
    for (uint32 i = 0; i < MAX_DISPLAY_IDS_PER_CREATURE; ++i)
        if (ci->display_id[i] && (!ci->display_total_probability || ci->display_probability[i]))
            models.push_back(ci->display_id[i]);
    if (models.empty())
        return 0;
    return models[itemGuidLow % models.size()];
}

void FusionMgr::SaveCompanionOut(Player* player, ObjectGuid item)
{
    if (item.IsEmpty())
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_companion_out` WHERE `guid` = %u", player->GetGUIDLow());
    else
        CharacterDatabase.PExecute("REPLACE INTO `character_fusion_companion_out` (`guid`, `item_guid`) VALUES (%u, %u)",
            player->GetGUIDLow(), item.GetCounter());
}

void FusionMgr::DismissOldPet(Player* player)
{
    Pet* old = player->GetPet();
    if (!old || old->IsFusionCompanion())
        return;
    // Not saved as current: it is not loaded at the next login either.
    old->Unsummon(old->IsControlled() ? PET_SAVE_NOT_IN_SLOT : PET_SAVE_AS_DELETED, player);
}

void FusionMgr::DismissCompanion(Player* player, bool beaten, bool tell, bool comeBack)
{
    if (!player->HasFusion())
        return;
    FusionCompanionOut& c = player->GetFusion().companion;
    ObjectGuid const item = c.item;
    bool const wasOut = !item.IsEmpty() && (!c.pet.IsEmpty() || c.riding || c.suspended);
    // Put away dead (before the half-second look saw it, or by its owner's hand: its item,
    // Dismiss, dying with it): it faints all the same, whatever sent it back.
    if (wasOut && !beaten && !c.riding)
    {
        Pet* dead = CompanionPet(player);
        beaten = c.petDied || (dead && !dead->IsAlive());
    }
    // How it goes back: fainted, or as hurt as it is (ridden, as it was when mounted), and in
    // the stance it was left in. Saved with its item (FusionCompanionRevive.cpp).
    if (wasOut)
    {
        float share = c.riding ? c.rideHealth : c.seenHealth;
        Pet* pet = CompanionPet(player);
        if (pet && pet->IsAlive() && pet->GetMaxHealth())
        {
            share = float(pet->GetHealth()) / float(pet->GetMaxHealth());
            c.react = uint8(pet->GetReactState());
        }
        if (beaten)
        {
            FaintCompanion(player, item);
            comeBack = false;
        }
        else
            PutCompanionAway(item, share);
        c.lastItem = item;
    }
    if (Pet* pet = CompanionPet(player))
        if (pet->IsInWorld())
            pet->Unsummon(PET_SAVE_AS_DELETED, player);
    // Ridden: its owner gets off, and it goes back into its item.
    if (c.riding && c.rideSpell && player->HasAura(c.rideSpell))
        player->RemoveAurasDueToSpell(c.rideSpell);
    if (c.trackingSpell && player->HasAura(c.trackingSpell))
        player->RemoveAurasDueToSpell(c.trackingSpell);
    // What outlives the summon (FusionCompanionOut's last block), the rest starts over.
    FusionCompanionOut const keep = c;
    c = FusionCompanionOut();
    c.lineageSent = keep.lineageSent;
    c.rests = keep.rests;
    c.indexAskedMs = keep.indexAskedMs;
    c.autocast = keep.autocast;
    c.readyAtMs = keep.readyAtMs;
    c.react = keep.react;
    c.lastItem = keep.lastItem;
    c.nextFaintLookMs = keep.nextFaintLookMs;
    c.draughtAskItem = keep.draughtAskItem;
    c.draughtAskMs = keep.draughtAskMs;
    c.pickToldItem = keep.pickToldItem;
    c.pickToldStage = keep.pickToldStage;
    c.nextOldPetLookMs = keep.nextOldPetLookMs;
    // Put away for good, or to come back once its owner is up again (the out row stays, so a
    // logout meanwhile still brings it back).
    if (comeBack && wasOut)
        c.returnItem = item;
    else if (wasOut || !keep.returnItem.IsEmpty())
        SaveCompanionOut(player, ObjectGuid());
    if (tell)
    {
        SendCompanionBar(player, true);
        SendCompanionMoves(player);
    }
    // The window and the item tooltips learn of the faint.
    if (beaten && wasOut)
        SendCompanionCarried(player);
}

void FusionMgr::UseCompanionItem(Player* player, Item* item)
{
    FusionCompanionTell chat(player);
    if (!m_enabled || !player->HasFusion() || !item)
        return;
    Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry());
    if (!sp)
    {
        chat.SendSysMessage("This companion is not known to the server.");
        return;
    }
    // From the bags only: not the bank, not a trade window.
    if (!Player::IsInventoryPos(item->GetPos()) || item->IsInTrade())
    {
        player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, item, nullptr);
        return;
    }
    InventoryResult const msg = player->CanUseItem(item);
    if (msg != EQUIP_ERR_OK)
    {
        player->SendEquipError(msg, item, nullptr);
        return;
    }
    FusionCompanionOut& c = player->GetFusion().companion;
    // Out on foot, ridden, or put away by the game for a moment (a taxi, another mount).
    bool const sameOut = c.item == item->GetObjectGuid() && ((!c.pet.IsEmpty() && CompanionPet(player)) || c.suspended);
    if (CompanionDuelHolds(player->GetObjectGuid().GetRawValue()))
    {
        chat.SendSysMessage("Not during a companion duel (forfeit it first).");
        return;
    }
    // A fainted one only answers (below); a summon, a put-away or a ride waits its turn.
    if (!CompanionFaintLeft(player, item) && !player->GetFusion().companionSummons.Allow(WorldTimer::getMSTime()))
    {
        player->SendEquipError(EQUIP_ERR_CANT_DO_RIGHT_NOW, item, nullptr);
        return;
    }
    // Using the item of the companion out (on foot or ridden) puts it back in its item.
    if (sameOut || (c.riding && c.item == item->GetObjectGuid()))
    {
        DismissCompanion(player);
        return;
    }
    // Fainted: it stays in its item until revived (FusionCompanionRevive.cpp). With a Reviving
    // Draught in the bags, using its item again soon gives it one.
    if (uint32 const left = CompanionFaintLeft(player, item))
    {
        uint32 const now = WorldTimer::getMSTime();
        uint32 const draughts = player->GetItemCount(Companions::kReviveDraughtItem);
        if (draughts && c.draughtAskItem == item->GetObjectGuid() && now - c.draughtAskMs < kDraughtAskMs)
        {
            c.draughtAskItem.Clear();
            UseReviveDraught(player, item->GetObjectGuid().GetRawValue());
            return;
        }
        std::string text = "Your " + CompanionName(item) + " has fainted: it comes round in " + Companions::WaitText(left)
            + ". A stable master or a Reviving Draught revives it now.";
        if (draughts)
        {
            text += " Use its item again to give it a draught (you have " + std::to_string(draughts) + ").";
            c.draughtAskItem = item->GetObjectGuid();
            c.draughtAskMs = now;
        }
        CompanionNotice(player, Companions::Index::NoticeKind::Error, text);
        return;
    }
    if (!player->IsAlive() || player->IsTaxiFlying())
    {
        player->SendEquipError(EQUIP_ERR_CANT_DO_RIGHT_NOW, item, nullptr);
        return;
    }
    // One companion at a time: another one out (or ridden, or put away for a moment) goes back
    // first.
    if (!c.pet.IsEmpty() || c.riding || c.suspended)
        DismissCompanion(player, false, false);
    if (!SummonCompanion(player, item))
    {
        chat.SendSysMessage("Your companion could not be summoned here.");
        SendCompanionBar(player, true);
        SendCompanionMoves(player);
    }
    SendCompanionCarried(player);
}

void FusionMgr::SendCompanionBar(Player* player, bool force)
{
    if (!player->HasFusion() || !player->GetSession())
        return;
    FusionCompanionOut& c = player->GetFusion().companion;
    Companions::Bar bar;    // item 0, flags 0: nothing out
    Pet* pet = CompanionPet(player);
    Item* item = pet || c.riding ? player->GetItemByGuid(c.item) : nullptr;
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (sp)
    {
        Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
        float const share = pet ? pet->GetHealthPercent() / 100.f : c.rideHealth;
        uint8 const health = uint8(std::min(100.f, share * 100.f + 0.5f));
        bar = Companions::MakeBar(c.item.GetRawValue(), *sp, s, CompanionContext(player, *sp, s), health);
        bar.flags |= Companions::RideFlags(*sp, s, RideContextOf(player), c.riding);
    }
    if (!force && bar.flags == c.sentFlags && uint8(bar.block) == c.sentBlock && bar.healthPct == c.sentHealth)
        return;
    c.sentFlags = bar.flags;
    c.sentBlock = uint8(bar.block);
    c.sentHealth = bar.healthPct;
    SendChannel(player, Companions::kChannelCompanion, Companions::EncodeBar(bar));
}

void FusionMgr::UpdateCompanion(Player* player, uint32 now)
{
    FusionCompanionOut& c = player->GetFusion().companion;
    if (std::shared_ptr<FusionCompanionDuel> duel = CompanionDuelOf(player->GetObjectGuid().GetRawValue()))
        DriveCompanionDuel(player, duel, now);
    // A class pet still on record from before companions took the pet slot (a hunter's or a
    // warlock's, loaded at login) is put away; the slot is the companion's.
    if (m_enabled && int32(now - c.nextOldPetLookMs) >= 0)
    {
        c.nextOldPetLookMs = now + 2000;
        DismissOldPet(player);
    }
    // A fainted companion in the bags whose wait is over says so (FusionCompanionRevive.cpp).
    if (m_enabled && int32(now - c.nextFaintLookMs) >= 0)
    {
        c.nextFaintLookMs = now + kFaintLookMs;
        LookAtFaintedCompanions(player, now);
    }
    // A mount cast is watched every update: a step breaks it, its end puts its owner on.
    UpdateMountCast(player, now);
    if (int32(now - c.nextLookMs) < 0)
        return;
    c.nextLookMs = now + kLookMs;
    // A companion ride's mount aura that no ride accounts for comes off (Companions::
    // StrayRideAura): left alone, it shows its spell's own mount (a wolf for a tiger).
    if (m_enabled && player->IsMounted())
        TakeOffStrayRideAuras(player);
    if (c.pet.IsEmpty() && !c.riding && !c.suspended)
    {
        UpdateCompanionReturn(player, now);
        return;
    }
    UpdateCompanionCombat(player, now);

    Item* item = player->GetItemByGuid(c.item);
    bool const itemHere = item && Player::IsInventoryPos(item->GetPos()) && !item->IsInTrade();
    // Beaten: it faints, and stays in its item until revived or its wait is over.
    auto beaten = [&]() { DismissCompanion(player, true); };
    // The item went out of the bags (traded, mailed, sold, banked): the companion goes with it.
    auto itemGone = [&]() {
        DismissCompanion(player);
        CompanionNotice(player, Companions::Index::NoticeKind::Info, "Your companion went back into its item: the item left your bags.");
    };
    if (c.riding)
    {
        if (!itemHere)
        {
            itemGone();
            return;
        }
        // Its owner died in the saddle: it is back at their side once they are up again.
        if (!player->IsAlive())
        {
            DismissCompanion(player, false, true, true);
            return;
        }
        // Off it for a flight path or a teleport: back on foot once that is over.
        if (player->IsTaxiFlying() || player->IsBeingTeleported())
            return;
        bool const hasAura = player->HasAura(c.rideSpell);
        // The mount spell is cast triggered: it keeps its cast time but sits in no spell slot, so
        // only its clock says it may still land (Companions::LookAtRide).
        switch (Companions::LookAtRide(hasAura, c.rideMounted, now - c.rideSinceMs, c.rideCastMs, c.rideLeaving))
        {
            case Companions::RideLook::Mounted:
                // Still in the saddle: it shows as itself.
                c.rideMounted = true;
                if (c.rideDisplay && player->GetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID) != c.rideDisplay)
                    player->SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, c.rideDisplay);
                SendCompanionBar(player, false);
                return;
            case Companions::RideLook::Waiting:
                SendCompanionBar(player, false);
                return;
            case Companions::RideLook::Off:
                // Asked off while the spell was still being cast: its aura came after all.
                if (hasAura)
                    player->RemoveAurasDueToSpell(c.rideSpell);
                break;
            case Companions::RideLook::NeverTook:
                break;
        }
        // Off it, by choice or knocked off: back on foot at its owner's side, in its owner's
        // fight if there is one. A mount spell that never took (indoors, say) says so.
        if (!c.rideMounted && !c.rideLeaving)
            CompanionNotice(player, Companions::Index::NoticeKind::Error, "You can't ride your companion here.");
        float const health = c.rideHealth;
        c.riding = false;
        c.rideSpell = 0;
        c.rideMounted = false;
        c.rideLeaving = false;
        if (!SummonCompanion(player, item))
        {
            DismissCompanion(player);
            return;
        }
        if (Pet* pet = CompanionPet(player))
        {
            pet->SetHealth(std::max<uint32>(1, uint32(health * float(pet->GetMaxHealth()) + 0.5f)));
            c.seenHealth = health;
            Unit* foe = player->GetVictim();
            if (!foe && !player->GetAttackers().empty())
                foe = *player->GetAttackers().begin();
            if (foe && foe->IsAlive() && pet->IsValidAttackTarget(foe) && pet->AI())
                pet->AI()->AttackStart(foe);
        }
        SendCompanionBar(player, true);
        return;
    }
    Pet* pet = CompanionPet(player);
    if (!pet)
    {
        if (!itemHere)
        {
            itemGone();
            return;
        }
        if (c.petDied)
        {
            beaten();
            return;
        }
        if (!player->IsAlive())
        {
            DismissCompanion(player, false, true, true);
            return;
        }
        // The game put it away: a teleport (to another map, or far on this one, a hearthstone),
        // a taxi, another mount, or its owner left it more than 120 yards behind (Pet::Update).
        // It comes back at its owner's side once that is over, as hurt as it was.
        if (!c.suspended)
        {
            c.suspended = true;
            c.pet.Clear();
            c.lastItem = c.item;
            PutCompanionAway(c.item, c.seenHealth);
        }
        if (!player->IsInWorld() || player->IsBeingTeleported() || player->IsTaxiFlying() || player->IsMounted() ||
            !player->GetPetGuid().IsEmpty())
            return;
        if (!SummonCompanion(player, item))
        {
            DismissCompanion(player);
            CompanionNotice(player, Companions::Index::NoticeKind::Error, "Your companion could not come back here; summon it again later.");
        }
        return;
    }
    // Beaten (even as its owner fell with it): it rests before it can be summoned again.
    if (!pet->IsAlive() || c.petDied)
    {
        beaten();
        return;
    }
    // Its owner died: it goes back, and comes back once they are up again.
    if (!player->IsAlive())
    {
        DismissCompanion(player, false, true, true);
        return;
    }
    if (!itemHere)
    {
        itemGone();
        return;
    }
    c.seenHealth = pet->GetMaxHealth() ? float(pet->GetHealth()) / float(pet->GetMaxHealth()) : 1.f;
    // Left far behind out of a fight (a fall, a ledge, a door it could not path through): it
    // catches up, set down at its follow spot, and follows on (Companions::ShouldCatchUp).
    if (pet->GetMap() == player->GetMap() && !player->IsBeingTeleported() && !player->IsTaxiFlying()
        && pet->GetCharmInfo() && pet->GetCharmInfo()->HasCommandState(COMMAND_FOLLOW)
        && Companions::ShouldCatchUp(pet->GetDistance(player), pet->IsInCombat() || player->IsInCombat()))
    {
        float x, y, z;
        player->GetNearPoint(pet, x, y, z, pet->GetObjectBoundingRadius(), pet->GetFollowDistance(),
            player->GetOrientation() + pet->GetFollowAngle());
        pet->NearTeleportTo(x, y, z, player->GetOrientation());
        pet->GetMotionMaster()->Clear(false);
        pet->GetMotionMaster()->MoveFollow(player, pet->GetFollowDistance(), pet->GetFollowAngle());
    }
    // The owner levelled: a banked bar is paid and the companion fights at the new level.
    uint8 const ownerLevel = uint8(player->GetLevel());
    if (c.builtLevel != ownerLevel && !c.duelStats)
    {
        c.builtLevel = ownerLevel;
        if (Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry()))
        {
            Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
            uint8 const was = s.level;
            if (Companions::GainXp(s, 0, ownerLevel).levels)
            {
                SaveCompanionState(item->GetGUIDLow(), s);
                FusionCompanionTell(player).PSendSysMessage("|cff71d5ffYour %s reached level %u.|r", sp->name.c_str(),
                    uint32(s.level));
            }
            StampCompanionItem(item);
            BuildCompanion(pet, *sp, s, Companions::EffectiveLevel(s, ownerLevel), false);
            if (s.level != was)
                SendCompanionCarried(player);
        }
        SendCompanionBar(player, true);
        return;
    }
    // Health, a new place or hour, a catalyst picked up: the bar follows when it changes.
    SendCompanionBar(player, false);
}

void FusionMgr::UpdateCompanionReturn(Player* player, uint32 now)
{
    FusionCompanionOut& c = player->GetFusion().companion;
    // One to bring back: the one out at logout, or the one its owner's death sent back.
    if (c.returnItem.IsEmpty() || !m_enabled)
        return;
    if (!player->IsInWorld() || !player->IsAlive() || player->IsBeingTeleported() || player->IsTaxiFlying() ||
        player->IsMounted() || !player->GetPetGuid().IsEmpty())
        return;
    if (CompanionDuelHolds(player->GetObjectGuid().GetRawValue()))
        return;
    Item* item = player->GetItemByGuid(c.returnItem);
    c.returnItem.Clear();
    if (!item || !Player::IsInventoryPos(item->GetPos()) || item->IsInTrade())
    {
        SaveCompanionOut(player, ObjectGuid());
        CompanionNotice(player, Companions::Index::NoticeKind::Info, "Your companion stays in its item: the item is not in your bags.");
    }
    else if (uint32 const left = CompanionFaintLeft(player, item))
    {
        // Fainted (beaten before the logout or its owner's death): it stays put.
        SaveCompanionOut(player, ObjectGuid());
        CompanionNotice(player, Companions::Index::NoticeKind::Info, "Your " + CompanionName(item)
            + " has fainted and stays in its item: it comes round in " + Companions::WaitText(left) + ".");
    }
    else if (!SummonCompanion(player, item))
    {
        SaveCompanionOut(player, ObjectGuid());
        CompanionNotice(player, Companions::Index::NoticeKind::Error, "Your companion could not come back here; summon it again later.");
    }
}

// ── Experience ──────────────────────────────────────────────────────────────────────────────

void FusionMgr::OnCompanionDamage(Unit* attacker, Unit* victim)
{
    if (attacker == victim)
        return;
    uint32 const now = WorldTimer::getMSTime();
    // A companion's own hit: it takes part in that kill.
    if (Player* owner = CompanionOwner(attacker))
    {
        if (Creature* c = victim->ToCreature(); c && !c->IsPet() && !c->IsTotem())
        {
            std::lock_guard<std::mutex> lock(m_companionLock);
            if (now - m_companionCreditExpiredMs > 10000)
            {
                m_companionCreditExpiredMs = now;
                m_companionCredit.Expire(now);
            }
            m_companionCredit.Add(c->GetObjectGuid().GetRawValue(), owner->GetObjectGuid().GetRawValue(), now);
        }
        // Its traits' on-hit effects (venom, an element, its own healing).
        if (attacker->IsCreature() && static_cast<Creature*>(attacker)->IsPet())
            OnCompanionHit(owner, static_cast<Pet*>(attacker), victim, 0);
        return;
    }
    // Who last hit an owner (its companion's taunts answer that).
    if (Player* hurt = victim->ToPlayer(); hurt && hurt->HasFusion() && !hurt->GetFusion().companion.pet.IsEmpty() &&
        attacker != hurt && !attacker->IsPlayer())
    {
        hurt->GetFusion().companion.ownerHitBy = attacker->GetObjectGuid().GetRawValue();
        hurt->GetFusion().companion.ownerHitMs = now;
    }

    // An owner in a companion duel who hits the other owner or either companion loses it.
    if (m_duelFoesCount.load(std::memory_order_relaxed))
    {
        if (Player* by = attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
        {
            if (std::shared_ptr<FusionCompanionDuel> duel = CompanionDuelOf(by->GetObjectGuid().GetRawValue()))
            {
                uint64 const v = victim->GetObjectGuid().GetRawValue();
                std::lock_guard<std::mutex> lock(m_companionLock);
                if (duel->fighting && !duel->over && v != by->GetObjectGuid().GetRawValue() &&
                    (v == duel->owner[0] || v == duel->owner[1] || v == duel->pet[0] || v == duel->pet[1]))
                    duel->rules.Interfered(by->GetObjectGuid().GetRawValue());
            }
        }
    }

    // The owner's fight is the companion's: it joins what the owner hits, and turns on what hits
    // the owner (PetAI alone waits until it is in combat itself).
    auto join = [&](Player* owner, Unit* target) {
        Pet* pet = CompanionPet(owner);
        if (!pet || !pet->IsAlive() || pet == target || owner->GetFusion().companion.duelStats)
            return;
        if (pet->GetVictim() && pet->GetVictim()->IsAlive())
            return;
        if (pet->IsValidAttackTarget(target) && pet->AI())
            pet->AI()->AttackStart(target);
    };
    if (Player* p = attacker->ToPlayer(); p && p->HasFusion() && !p->GetFusion().companion.pet.IsEmpty())
        join(p, victim);
    else if (Player* hurt = victim->ToPlayer(); hurt && hurt->HasFusion() && !hurt->GetFusion().companion.pet.IsEmpty())
        join(hurt, attacker);
}

void FusionMgr::OnCompanionKill(Unit* killer, Unit* victim)
{
    // A companion killed: the next look puts it to rest, even if its corpse is already gone.
    if (Player* owner = CompanionOwner(victim))
    {
        // It faints now, saved (a logout before its look still finds it fainted).
        owner->GetFusion().companion.petDied = true;
        FaintCompanion(owner, owner->GetFusion().companion.item);
        return;
    }
    Creature* c = victim->ToCreature();
    if (!c || c->IsPet() || c->IsTotem())
        return;
    std::vector<uint64> owners;
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        owners = m_companionCredit.Take(c->GetObjectGuid().GetRawValue());
    }
    // A companion out and in the fight when it ended counts even if it never landed a hit.
    if (Player* p = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr)
        if (Pet* pet = CompanionPet(p))
            if (pet->GetVictim() == victim || victim->GetVictim() == pet)
                if (std::find(owners.begin(), owners.end(), p->GetObjectGuid().GetRawValue()) == owners.end())
                    owners.push_back(p->GetObjectGuid().GetRawValue());
    for (uint64 guid : owners)
        if (Player* owner = c->GetMap()->GetPlayer(ObjectGuid(guid)))
            CreditCompanionKill(owner, c);
}

void FusionMgr::CreditCompanionKill(Player* owner, Creature* victim)
{
    Pet* pet = CompanionPet(owner);
    FusionCompanionOut& out = owner->GetFusion().companion;
    Item* item = pet ? owner->GetItemByGuid(out.item) : nullptr;
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp || out.duelStats)
        return;
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    uint8 const ownerLevel = uint8(owner->GetLevel());
    bool changed = Companions::RecordKill(*sp, s, TypeOf(victim));
    // Critters are always gray; KillXp gives nothing for gray creatures either.
    uint32 const xp = victim->GetCreatureType() == CREATURE_TYPE_CRITTER ? 0
        : Companions::KillXp(Companions::EffectiveLevel(s, ownerLevel), uint8(victim->GetLevel()), victim->IsElite());
    uint8 const wasLevel = s.level;
    uint32 const wasXp = s.xp;
    Companions::XpGain const gain = Companions::GainXp(s, xp, ownerLevel);
    changed = changed || s.xp != wasXp || s.level != wasLevel;
    if (!changed)
        return;
    SaveCompanionState(item->GetGUIDLow(), s);
    StampCompanionItem(item);
    if (gain.levels)
    {
        BuildCompanion(pet, *sp, s, Companions::EffectiveLevel(s, ownerLevel), false);
        FusionCompanionTell(owner).PSendSysMessage("|cff71d5ffYour %s reached level %u.|r", sp->name.c_str(), uint32(s.level));
    }
    SendCompanionBar(owner, true);
}

void FusionMgr::GiveCompanionXp(Player* owner, uint32 xp)
{
    Pet* pet = CompanionPet(owner);
    FusionCompanionOut& out = owner->GetFusion().companion;
    Item* item = pet ? owner->GetItemByGuid(out.item) : nullptr;
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp || !xp)
        return;
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    uint8 const ownerLevel = uint8(owner->GetLevel());
    Companions::XpGain const gain = Companions::GainXp(s, xp, ownerLevel);
    SaveCompanionState(item->GetGUIDLow(), s);
    StampCompanionItem(item);
    if (gain.levels)
        FusionCompanionTell(owner).PSendSysMessage("|cff71d5ffYour %s reached level %u.|r", sp->name.c_str(), uint32(s.level));
    if (!out.duelStats)
        BuildCompanion(pet, *sp, s, Companions::EffectiveLevel(s, ownerLevel), false);
    SendCompanionBar(owner, true);
}

// ── Drops ───────────────────────────────────────────────────────────────────────────────────

void FusionMgr::AddCompanionLoot(Creature* creature, Loot& loot)
{
    if (!m_enabled || creature->IsPet() || creature->IsTotem() || m_companionSpecies.empty())
        return;
    CreatureInfo const* ci = creature->GetCreatureInfo();
    if (!ci)
        return;
    Player* looter = creature->GetLootRecipient();
    Companions::DropVictim v;
    v.entry = creature->GetEntry();
    v.type = TypeOf(creature);
    v.petFamily = uint8(ci->pet_family);
    v.rank = RankOf(creature);
    // No companion goods from a summon (anyone's: a warlock's imp, a boss's endless adds) nor from
    // a creature gray to the highest level of whoever tapped it (Companions::DropsCompanionGoods).
    v.summoned = creature->IsTemporarySummon() || !creature->GetOwnerGuid().IsEmpty();
    if (looter)
    {
        uint32 killerLevel = looter->GetLevel();
        if (Group* group = looter->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* m = ref->getSource())
                    if (m->IsInWorld() && m->GetMap() == creature->GetMap())
                        killerLevel = std::max<uint32>(killerLevel, m->GetLevel());
        v.grayToKiller = creature->GetLevel() <= Companions::GrayLevel(uint8(std::min<uint32>(killerLevel, 255)));
    }
    if (uint16 id = Companions::RollDrop(m_companionSpecies, v, float(rand_norm()), float(rand_norm())))
        if (Companions::CompanionSpecies const* sp = Companions::FindSpecies(m_companionSpecies, id))
            if (sObjectMgr.GetItemPrototype(sp->item))
                loot.AddItem(LootStoreItem(sp->item, 100.f, 0, 0, 1, 1));
    if (uint32 catalyst = Companions::CatalystFor(v.type))
        if (rand_norm() < Companions::CatalystDropChance(v, uint8(creature->GetLevel())) &&
            sObjectMgr.GetItemPrototype(catalyst))
            loot.AddItem(LootStoreItem(catalyst, 100.f, 0, 0, 1, 1));

    // Forager: the looter's companion was in the fight.
    Pet* pet = looter && looter->HasFusion() ? CompanionPet(looter) : nullptr;
    if (!pet || !pet->IsWithinDistInMap(creature, 40.f))
        return;
    Item* item = looter->GetItemByGuid(looter->GetFusion().companion.item);
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
        return;
    uint32 const traits = CompanionStateOf(item->GetGUIDLow(), item->GetEntry()).traits;
    if (Companions::HasTrait(traits, Companions::Trait::Forager) &&
        rand_norm() < (Companions::HasCombo(traits, Companions::Combo::Pathfinder) ? 0.05 : 0.03))
        if (uint32 reagent = Companions::BreedReagent(*sp))
            if (sObjectMgr.GetItemPrototype(reagent))
                loot.AddItem(LootStoreItem(reagent, 100.f, 0, 0, 1, 1));
}

void FusionMgr::ScavengeCompanionGold(Creature* creature, Loot& loot)
{
    // Scavenger: the looter's companion was in the fight. Called once the corpse's coin is rolled
    // (Creature::GenerateLootForBody), which comes after AddCompanionLoot.
    if (!m_enabled || !loot.gold || creature->IsPet())
        return;
    Player* looter = creature->GetLootRecipient();
    Pet* pet = looter && looter->HasFusion() ? CompanionPet(looter) : nullptr;
    if (!pet || !pet->IsWithinDistInMap(creature, 40.f))
        return;
    Item* item = looter->GetItemByGuid(looter->GetFusion().companion.item);
    if (!item || !CompanionSpeciesOfItem(item->GetEntry()))
        return;
    loot.gold = Companions::ScavengedGold(loot.gold, CompanionStateOf(item->GetGUIDLow(), item->GetEntry()).traits);
}

// ── Evolution ───────────────────────────────────────────────────────────────────────────────

void FusionMgr::EvolveCompanion(Player* player, uint64 itemGuid)
{
    FusionCompanionTell chat(player);
    Item* item = player->GetItemByGuid(ObjectGuid(itemGuid));
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
        return;
    if (!Player::IsInventoryPos(item->GetPos()) || item->IsInTrade())
    {
        chat.SendSysMessage(item->IsInTrade() ? "Not while it is in a trade window." : "That companion must be in your bags.");
        return;
    }
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    Companions::EvolveContext ctx = CompanionContext(player, *sp, s);
    // The companion out must be this one: it evolves in front of you.
    ctx.summoned = ctx.summoned && player->GetFusion().companion.item == item->GetObjectGuid();
    Companions::EvolutionRule const* rule = Companions::NextRule(*sp, s);
    Companions::EvolveBlock const block = Companions::CanEvolve(*sp, s, ctx);
    if (block != Companions::EvolveBlock::None || !rule)
    {
        chat.PSendSysMessage("Your %s cannot evolve: %s.", sp->name.c_str(), BlockText(block, rule, s).c_str());
        SendCompanionBar(player, true);
        return;
    }
    if (rule->item)
        player->DestroyItemCount(rule->item, std::max<uint8>(rule->itemCount, 1), true);
    Companions::Evolve(s);
    SaveCompanionState(item->GetGUIDLow(), s);
    StampCompanionItem(item);
    CountStat(player, {Fusion::Stat::CompanionsEvolved, Fusion::kAnyKey});
    SendChannel(player, Companions::kChannelCompanion, Companions::EncodeEvolved(itemGuid, sp->id, s.stage));
    // The new form takes the old one's place, whole.
    DismissCompanion(player, false, false);
    PutCompanionAway(item->GetObjectGuid(), 1.f);
    if (!SummonCompanion(player, item))
    {
        SendCompanionBar(player, true);
        SendCompanionMoves(player);
    }
    std::string form = sp->name;
    if (CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(sp->stages[s.stage].creature))
        form = ci->name;
    chat.PSendSysMessage("|cff71d5ffYour %s evolved into its next form: %s!|r", sp->name.c_str(), form.c_str());
    SendCompanionCarried(player);
}

// ── Riding ──────────────────────────────────────────────────────────────────────────────────

Companions::RideContext FusionMgr::RideContextOf(Player* player) const
{
    Companions::RideContext ctx;
    ctx.ownerLevel = uint8(player->GetLevel());
    // Any of vanilla's riding skills counts: the companion is the mount, not a racial one. An
    // admin rides at full speed to test.
    for (uint16 skill : {SKILL_RIDING_HORSE, SKILL_RIDING_WOLF, SKILL_RIDING_TIGER, SKILL_RIDING_RAM,
             SKILL_RIDING_RAPTOR, SKILL_RIDING_MECHANOSTRIDER, SKILL_RIDING_UNDEAD_HORSE, SKILL_RIDING_KODO})
        ctx.ridingSkill = std::max(ctx.ridingSkill, player->GetSkillValue(skill));
    if (IsAdmin(player))
        ctx.ridingSkill = std::max<uint16>(ctx.ridingSkill, Companions::kSwiftRideSkill);
    Pet* pet = CompanionPet(player);
    ctx.summoned = pet != nullptr;
    ctx.inCombat = player->IsInCombat() || (pet && pet->IsInCombat());
    ctx.inDuel = CompanionDuelHolds(player->GetObjectGuid().GetRawValue());
    // Where vanilla takes no mount (a dungeon, a raid, indoors, a ship's hold), no companion one
    // either: the mount spell is cast triggered, which skips those checks itself.
    MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(player->GetMapId());
    ctx.mountAllowed = mapEntry && mapEntry->IsMountAllowed() && player->GetAreaId() != 35
        && (!player->GetTransport() || player->IsOutdoorOnTransport());
    if (ctx.mountAllowed && sWorld.getConfig(CONFIG_BOOL_VMAP_INDOOR_CHECK)
        && VMAP::VMapFactory::createOrGetVMapManager()->isLineOfSightCalcEnabled())
        ctx.mountAllowed = player->GetTerrain()->IsOutdoors(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    ctx.healthShare = pet && pet->GetMaxHealth() ? float(pet->GetHealth()) / float(pet->GetMaxHealth()) : 0.f;
    return ctx;
}

namespace
{
    // Why a companion can't be ridden now, as its owner is told; empty: it can.
    std::string RideRefusal(Player* player, Companions::CompanionSpecies const& sp, Companions::CompanionState const& s,
        Companions::RideContext const& ctx)
    {
        char buf[160];
        switch (Companions::CanRide(sp, s, ctx))
        {
            case Companions::RideBlock::None:
                break;
            case Companions::RideBlock::NotRideable:
                snprintf(buf, sizeof(buf), "Your %s cannot be ridden in this form.", sp.name.c_str());
                return buf;
            case Companions::RideBlock::InCombat:
                return "You cannot mount in combat.";
            case Companions::RideBlock::InDuel:
                return "Not during a companion duel.";
            case Companions::RideBlock::LevelTooLow:
                snprintf(buf, sizeof(buf), "You must be level %u to ride.", uint32(Companions::kRideLevel));
                return buf;
            case Companions::RideBlock::NoSkill:
                snprintf(buf, sizeof(buf), "You need %u in a riding skill to ride.", uint32(Companions::kRideSkill));
                return buf;
            case Companions::RideBlock::Worn:
                snprintf(buf, sizeof(buf), "Your %s is too worn to carry you (%u%% health at least); let it heal.", sp.name.c_str(),
                    uint32(Companions::RideMinHealth(s.traits) * 100.f + 0.5f));
                return buf;
            case Companions::RideBlock::NotSummoned:
                snprintf(buf, sizeof(buf), "Summon your %s first.", sp.name.c_str());
                return buf;
            case Companions::RideBlock::NotHere:
                return "You can't ride your companion here.";
            default:
                return "You can't ride your companion now.";
        }
        if (player->IsMounted() || player->IsTaxiFlying())
            return "You are already mounted.";
        if (player->IsInWater())
            return "You can't mount while swimming.";
        return {};
    }
}

void FusionMgr::RideCompanion(Player* player)
{
    FusionCompanionTell chat(player);
    FusionCompanionOut& c = player->GetFusion().companion;
    if (!player->GetFusion().companionSummons.Allow(WorldTimer::getMSTime()))
        return;
    if (c.riding)
    {
        Dismount(player);
        return;
    }
    // The ride key again mid-cast calls it off.
    if (c.mountUntilMs)
    {
        BreakMountCast(player, "");
        return;
    }
    Pet* pet = CompanionPet(player);
    Item* item = pet ? player->GetItemByGuid(c.item) : nullptr;
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
    {
        chat.SendSysMessage("Summon a mount companion first.");
        return;
    }
    Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    Companions::RideContext const ctx = RideContextOf(player);
    if (std::string const no = RideRefusal(player, *sp, s, ctx); !no.empty())
    {
        chat.SendSysMessage(no.c_str());
        return;
    }
    uint32 const spell = Companions::RideSpell(sp->stages[s.stage], Companions::MountSpeed(ctx.ownerLevel, ctx.ridingSkill));
    if (!spell || !sSpellMgr.GetSpellEntry(spell))
    {
        chat.PSendSysMessage("Your %s can't be ridden on this server yet.", sp->name.c_str());
        return;
    }
    // A cast: its owner stands still under a cast bar while the companion runs to the spot just in
    // front of them, and the ride begins as it arrives (UpdateMountCast, FinishRide).
    Fusion::Vec3 const owner{player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()};
    Fusion::Vec3 spot = Companions::MountSpot(owner, player->GetOrientation(), pet->GetObjectBoundingRadius());
    // On the ground under it (a slope, a step), never through a wall: its owner's own spot if it
    // can't be seen from there.
    float const ground = player->GetMap()->GetHeight(spot.x, spot.y, spot.z + 2.f);
    if (ground > INVALID_HEIGHT && std::fabs(ground - spot.z) < 3.f)
        spot.z = ground;
    if (!player->IsWithinLOS(spot.x, spot.y, spot.z + 1.f))
        spot = owner;
    float const distance = (Fusion::Vec3{pet->GetPositionX(), pet->GetPositionY(), pet->GetPositionZ()} - spot).Length();
    float const run = pet->GetSpeed(MOVE_RUN);
    uint32 const ms = Companions::MountCastMs(distance, run);
    uint32 const now = WorldTimer::getMSTime();
    c.mountUntilMs = (now + ms) | 1;
    c.mountFromX = owner.x;
    c.mountFromY = owner.y;
    c.mountFromZ = owner.z;
    // Facing its owner once there, a mount offered to be climbed on.
    float const facing = std::atan2(owner.y - spot.y, owner.x - spot.x);
    pet->GetMotionMaster()->Clear();
    pet->GetMotionMaster()->MovePoint(pet->GetGUIDLow(), spot.x, spot.y, spot.z, MOVE_PATHFINDING,
        Companions::MountRunSpeed(distance, run, ms), facing);
    // It answers in its own voice (its CreatureSoundData pet-order bark), never a mount spell's.
    pet->SendPetTalk(0);
    SendChannel(player, Companions::kChannelCompanion, Companions::EncodeMountCast(Companions::MountCastPhase::Start, ms));
}

void FusionMgr::BreakMountCast(Player* player, char const* why)
{
    FusionCompanionOut& c = player->GetFusion().companion;
    if (!c.mountUntilMs)
        return;
    c.mountUntilMs = 0;
    SendChannel(player, Companions::kChannelCompanion, Companions::EncodeMountCast(Companions::MountCastPhase::Broken, 0));
    if (why && *why)
        FusionCompanionTell(player).SendSysMessage(why);
    // Back to its owner's heel.
    if (Pet* pet = CompanionPet(player))
    {
        pet->GetMotionMaster()->Clear();
        pet->GetMotionMaster()->MoveFollow(player, pet->GetFollowDistance(), pet->GetFollowAngle());
    }
}

void FusionMgr::UpdateMountCast(Player* player, uint32 now)
{
    FusionCompanionOut& c = player->GetFusion().companion;
    if (!c.mountUntilMs)
        return;
    Pet* pet = CompanionPet(player);
    if (!pet || !pet->IsAlive() || !player->IsAlive() || !player->IsInWorld() || player->IsBeingTeleported())
    {
        BreakMountCast(player, "");
        return;
    }
    Fusion::Vec3 const from{c.mountFromX, c.mountFromY, c.mountFromZ};
    Fusion::Vec3 const at{player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()};
    if (!Companions::MountCastHolds(from, at))
    {
        BreakMountCast(player, "Interrupted: hold still to mount.");
        return;
    }
    if (player->IsInCombat())
    {
        BreakMountCast(player, "You cannot mount in combat.");
        return;
    }
    if (player->IsMounted() || player->IsTaxiFlying())
    {
        BreakMountCast(player, "");
        return;
    }
    if (int32(now - c.mountUntilMs) < 0)
        return;
    c.mountUntilMs = 0;
    FinishRide(player);
}

void FusionMgr::FinishRide(Player* player)
{
    FusionCompanionTell chat(player);
    FusionCompanionOut& c = player->GetFusion().companion;
    auto broke = [&](std::string const& why)
    {
        SendChannel(player, Companions::kChannelCompanion, Companions::EncodeMountCast(Companions::MountCastPhase::Broken, 0));
        if (!why.empty())
            chat.SendSysMessage(why.c_str());
        if (Pet* pet = CompanionPet(player))
            pet->GetMotionMaster()->MoveFollow(player, pet->GetFollowDistance(), pet->GetFollowAngle());
    };
    Pet* pet = CompanionPet(player);
    Item* item = pet ? player->GetItemByGuid(c.item) : nullptr;
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
        return broke("");
    // Everything asked again: the cast took its time.
    Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    Companions::RideContext const ctx = RideContextOf(player);
    if (std::string const no = RideRefusal(player, *sp, s, ctx); !no.empty())
        return broke(no);
    uint32 const spell = Companions::RideSpell(sp->stages[s.stage], Companions::MountSpeed(ctx.ownerLevel, ctx.ridingSkill));
    if (!spell || !sSpellMgr.GetSpellEntry(spell))
        return broke("");
    // The companion becomes the mount: off its feet, its owner on the vanilla mount spell's aura
    // (the speed, and vanilla's rules for getting knocked off and not fighting mounted).
    float const share = ctx.healthShare;
    // It shows as itself: the display it walks with (its stage's own: a mount line's last form is
    // a mount model), never the mount spell's. Aura::HandleAuraMounted asks CompanionMountDisplay
    // as the aura lands, and the look holds it.
    uint32 creatureDisplay = 0;
    if (CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(sp->stages[s.stage].creature))
        creatureDisplay = Creature::ChooseDisplayId(ci);
    uint32 walked = CompanionDisplay(*sp, s, item->GetGUIDLow());
    if (!walked)
        walked = pet->GetNativeDisplayId();
    uint32 const display = Companions::Index::MountDisplay(*sp, s, walked, creatureDisplay);
    c.react = uint8(pet->GetReactState());
    pet->Unsummon(PET_SAVE_AS_DELETED, player);
    c.pet.Clear();
    c.riding = true;
    c.rideMounted = false;
    c.rideSpell = spell;
    c.rideDisplay = display;
    c.rideHealth = share;
    c.rideSinceMs = WorldTimer::getMSTime();
    c.rideLeaving = false;
    c.rideCastMs = 0;
    c.lastItem = c.item;
    c.nextLookMs = c.rideSinceMs + kLookMs;
    // The cast was ours: the aura goes on at once, with none of the mount spell's own cast (its
    // seconds of waiting with no companion, its summoning sound, its name on a cast bar).
    player->AddAura(spell, 0, player);
    if (!player->HasAura(spell))
    {
        // Refused as an aura (some rule of the spell's): its own cast, as before.
        SpellEntry const* const rideInfo = sSpellMgr.GetSpellEntry(spell);
        c.rideCastMs = rideInfo ? rideInfo->GetCastTime(player) : 0;
        player->CastSpell(player, spell, true);
    }
    if (display && player->HasAura(spell))
        player->SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, display);
    SendChannel(player, Companions::kChannelCompanion, Companions::EncodeMountCast(Companions::MountCastPhase::Done, 0));
    SendCompanionBar(player, true);
    SendCompanionMoves(player);
}

void FusionMgr::TakeOffStrayRideAuras(Player* player)
{
    FusionCompanionOut const& c = player->GetFusion().companion;
    std::vector<uint32> stray;
    for (Aura const* aura : player->GetAurasByType(SPELL_AURA_MOUNTED))
    {
        uint32 const spell = aura->GetId();
        if (Companions::StrayRideAura(Companions::IsCompanionRideSpell(m_companionSpecies, spell), c.riding, c.rideSpell,
                spell, !aura->GetCastItemGuid().IsEmpty(), player->HasSpell(spell)))
            stray.push_back(spell);
    }
    for (uint32 spell : stray)
        player->RemoveAurasDueToSpell(spell);
}

void FusionMgr::Dismount(Player* player)
{
    FusionCompanionOut& c = player->GetFusion().companion;
    if (!c.riding)
        return;
    // Off the saddle; the next look puts it back on foot beside its owner (or, while the mount
    // spell is still being cast, takes its aura off when it lands).
    if (c.rideSpell && player->HasAura(c.rideSpell))
        player->RemoveAurasDueToSpell(c.rideSpell);
    c.rideLeaving = true;
    c.nextLookMs = 0;
}

// ── Companion duels ─────────────────────────────────────────────────────────────────────────

std::shared_ptr<FusionCompanionDuel> FusionMgr::CompanionDuelOf(uint64 owner) const
{
    std::lock_guard<std::mutex> lock(m_companionLock);
    for (std::shared_ptr<FusionCompanionDuel> const& d : m_companionDuels)
        if (!d->over && d->rules.Involves(owner))
            return d;
    return nullptr;
}

bool FusionMgr::CompanionDuelHolds(uint64 owner) const
{
    std::lock_guard<std::mutex> lock(m_companionLock);
    for (std::shared_ptr<FusionCompanionDuel> const& d : m_companionDuels)
        if (!d->over && d->rules.Involves(owner))
            return Companions::DuelHolds(d->rules.Phase());
    return false;
}

bool FusionMgr::CompanionsDuelling(Unit const* a, Unit const* b) const
{
    if (!m_duelFoesCount.load(std::memory_order_relaxed) || !a || !b || !a->IsCreature() || !b->IsCreature())
        return false;
    std::lock_guard<std::mutex> lock(m_duelFoesLock);
    auto it = m_duelFoes.find(a->GetObjectGuid().GetRawValue());
    return it != m_duelFoes.end() && it->second.first == b->GetObjectGuid().GetRawValue();
}

uint32 FusionMgr::CompanionDuelDamage(Unit* victim, uint32 damage)
{
    if (!victim->IsCreature())
        return damage;
    uint32 const health = victim->GetHealth();
    if (damage < health)
        return damage;
    // Phoenix-blooded: once in ten minutes a killing blow is a rebirth in fire instead (Ashborn:
    // at 60% and the burst stuns).
    // Not in its own companion duel (that ends at 1 health); anyone else's duel is no matter.
    bool duelling = false;
    if (m_duelFoesCount.load(std::memory_order_relaxed))
    {
        std::lock_guard<std::mutex> lock(m_duelFoesLock);
        duelling = m_duelFoes.count(victim->GetObjectGuid().GetRawValue()) != 0;
    }
    if (static_cast<Creature*>(victim)->IsPet() && static_cast<Pet*>(victim)->IsFusionCompanion() && !duelling)
        if (Player* owner = CompanionOwner(victim))
            if (Item* item = owner->GetItemByGuid(owner->GetFusion().companion.item))
            {
                Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
                uint32 const now = WorldTimer::getMSTime();
                bool rise = false;
                if (Companions::HasTrait(s.traits, Companions::Trait::PhoenixBlooded))
                {
                    std::lock_guard<std::mutex> lock(m_companionLock);
                    uint32& ready = m_companionPhoenixMs[item->GetGUIDLow()];
                    if (!ready || int32(now - ready) >= 0)
                    {
                        ready = now + 10 * 60 * 1000;
                        rise = true;
                    }
                }
                if (rise)
                {
                    bool const ashborn = Companions::HasCombo(s.traits, Companions::Combo::Ashborn);
                    victim->SetHealth(std::max<uint32>(1, uint32(float(victim->GetMaxHealth()) * (ashborn ? 0.6f : 0.4f))));
                    if (Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry()))
                    {
                        Companions::CompanionStats const st = Companions::StatsWithTraits(*sp, s,
                            Companions::EffectiveLevel(s, uint8(owner->GetLevel())));
                        victim->CastCustomSpell(victim, 11969, int32(Companions::BasicHit(st)), {}, {}, true);
                        if (ashborn)
                            victim->CastSpell(victim, 11876, true);     // War Stomp's 2 s stun
                    }
                    FusionCompanionTell(owner).SendSysMessage("|cffff8020Your companion rises from its ashes!|r");
                    return 0;
                }
            }
    if (!m_duelFoesCount.load(std::memory_order_relaxed))
        return damage;
    uint64 owner = 0;
    {
        std::lock_guard<std::mutex> lock(m_duelFoesLock);
        auto it = m_duelFoes.find(victim->GetObjectGuid().GetRawValue());
        if (it == m_duelFoes.end())
            return damage;
        owner = it->second.second;
    }
    // Beaten, not dead: the fight ends at 1 health, as a player duel does.
    if (std::shared_ptr<FusionCompanionDuel> duel = CompanionDuelOf(owner))
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        duel->rules.Beaten(owner);
    }
    return health ? health - 1 : 0;
}

void FusionMgr::ChallengeCompanionDuel(Player* player, ObjectGuid target)
{
    FusionCompanionTell chat(player);
    Player* other = target.IsPlayer() && player->IsInWorld() ? player->GetMap()->GetPlayer(target) : nullptr;
    if (!other || other == player || !other->HasFusion())
    {
        chat.SendSysMessage("Target a player with a companion out to challenge their companion.");
        return;
    }
    Pet* mine = CompanionPet(player);
    Pet* theirs = CompanionPet(other);
    if (!mine)
    {
        chat.SendSysMessage("Summon your companion first.");
        return;
    }
    if (!theirs)
    {
        chat.PSendSysMessage("%s has no companion out.", other->GetName());
        return;
    }
    if (player->IsInCombat() || other->IsInCombat() || mine->IsInCombat() || theirs->IsInCombat())
    {
        chat.SendSysMessage("Not while either of you is in combat.");
        return;
    }
    if (!player->IsWithinDistInMap(other, Companions::CompanionDuel::kChallengeRange))
    {
        chat.PSendSysMessage("%s is too far away (%u yards at most).", other->GetName(),
            uint32(Companions::CompanionDuel::kChallengeRange));
        return;
    }
    uint64 const a = player->GetObjectGuid().GetRawValue();
    uint64 const b = other->GetObjectGuid().GetRawValue();
    if (CompanionDuelOf(a) || CompanionDuelOf(b))
    {
        chat.SendSysMessage("One of you is already in a companion duel.");
        return;
    }
    uint32 const now = WorldTimer::getMSTime();
    {
        // One challenge a minute between the same two players: no pestering someone with them.
        std::lock_guard<std::mutex> lock(m_companionLock);
        auto it = m_companionChallengeMs.find(PairKey(a, b));
        if (it != m_companionChallengeMs.end() && now - it->second < Companions::kRechallengeMs)
        {
            chat.PSendSysMessage("You challenged %s a moment ago; wait %u s.", other->GetName(),
                (Companions::kRechallengeMs - (now - it->second) + 999) / 1000);
            return;
        }
        m_companionChallengeMs[PairKey(a, b)] = now;
    }
    auto duel = std::make_shared<FusionCompanionDuel>();
    if (!duel->rules.Challenge(a, b, now))
        return;
    duel->owner = {a, b};
    duel->mapId = player->GetMapId();
    duel->told = Companions::DuelPhase::Challenged;
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        m_companionDuels.push_back(duel);
    }
    Companions::DuelUpdate u;
    u.phase = Companions::DuelPhase::Challenged;
    u.opponent = b;
    SendChannel(player, Companions::kChannelCompanion, Companions::EncodeDuel(u));
    u.opponent = a;
    SendChannel(other, Companions::kChannelCompanion, Companions::EncodeDuel(u));
    chat.PSendSysMessage("|cffffd200You challenge %s's companion to a duel.|r", other->GetName());
    FusionCompanionTell(other).PSendSysMessage("|cffffd200%s challenges your companion to a duel.|r", player->GetName());
}

void FusionMgr::AnswerCompanionDuel(Player* player, bool accept)
{
    uint64 const me = player->GetObjectGuid().GetRawValue();
    std::shared_ptr<FusionCompanionDuel> duel = CompanionDuelOf(me);
    if (!duel)
    {
        FusionCompanionTell(player).SendSysMessage("No companion duel is waiting for you.");
        return;
    }
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        if (accept)
            accepted = duel->rules.Accept(me, WorldTimer::getMSTime());
        else
            duel->rules.Decline(me);
    }
    if (accept && !accepted)
    {
        FusionCompanionTell(player).SendSysMessage("There is no companion duel challenge for you to accept.");
        return;
    }
    if (!accepted)
        return;
    // Both companions at full health and at the lower of their two levels: the duel tests the
    // species, stage and role, not who levelled more.
    Map* map = player->GetMap();
    std::array<Player*, 2> owners{map->GetPlayer(ObjectGuid(duel->owner[0])), map->GetPlayer(ObjectGuid(duel->owner[1]))};
    std::array<Pet*, 2> pets{owners[0] ? CompanionPet(owners[0]) : nullptr, owners[1] ? CompanionPet(owners[1]) : nullptr};
    std::array<uint8, 2> levels{1, 1};
    std::array<Companions::CompanionSpecies const*, 2> species{};
    std::array<Companions::CompanionState, 2> states{};
    for (int i = 0; i < 2; ++i)
    {
        Item* item = pets[i] ? owners[i]->GetItemByGuid(owners[i]->GetFusion().companion.item) : nullptr;
        species[i] = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
        if (!species[i])
            continue;   // the next drive sees it gone and calls the duel off
        states[i] = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
        levels[i] = Companions::EffectiveLevel(states[i], uint8(owners[i]->GetLevel()));
    }
    uint8 const level = Companions::DuelLevels(levels[0], levels[1]).first;
    for (int i = 0; i < 2; ++i)
    {
        if (!species[i])
            continue;
        // Its health before the fight: it goes back no better than that (a duel is no free heal).
        duel->preHealth[i] = pets[i]->GetMaxHealth() ? float(pets[i]->GetHealth()) / float(pets[i]->GetMaxHealth()) : 1.f;
        BuildCompanion(pets[i], *species[i], states[i], level, true, false);
        owners[i]->GetFusion().companion.duelStats = true;
    }
    std::lock_guard<std::mutex> lock(m_companionLock);
    for (int i = 0; i < 2; ++i)
        duel->pet[i] = pets[i] ? pets[i]->GetObjectGuid().GetRawValue() : 0;
    if (pets[0] && pets[1])
        duel->center = (PosOf(pets[0]) + PosOf(pets[1])) * 0.5f;
}

void FusionMgr::ForfeitCompanionDuel(Player* player)
{
    uint64 const me = player->GetObjectGuid().GetRawValue();
    std::shared_ptr<FusionCompanionDuel> duel = CompanionDuelOf(me);
    if (!duel)
    {
        FusionCompanionTell(player).SendSysMessage("You are in no companion duel.");
        return;
    }
    std::lock_guard<std::mutex> lock(m_companionLock);
    if (duel->rules.Phase() == Companions::DuelPhase::Challenged)
        duel->rules.Decline(me);    // taking a challenge back, or turning it down
    else
        duel->rules.Forfeit(me);
}

void FusionMgr::DriveCompanionDuel(Player* player, std::shared_ptr<FusionCompanionDuel> const& duel, uint32 now)
{
    // Both owners' updates drive it; whichever comes first in a quarter second does the work.
    // An owner gone to another map is judged by the other owner's update (who finds it gone:
    // it fled); only when both have left does this one call it, as its own side fleeing.
    if (player->GetMapId() != duel->mapId)
    {
        uint64 const me = player->GetObjectGuid().GetRawValue();
        uint64 const them = duel->owner[0] == me ? duel->owner[1] : duel->owner[0];
        Player* other = ObjectAccessor::FindPlayer(ObjectGuid(them));
        if (other && other->IsInWorld() && other->GetMapId() == duel->mapId)
            return;
        std::lock_guard<std::mutex> lock(m_companionLock);
        if (!duel->over)
            duel->rules.Fled(me);
    }
    Map* map = player->GetMap();
    std::array<Player*, 2> owners{};
    std::array<Pet*, 2> pets{};
    for (int i = 0; i < 2; ++i)
    {
        owners[i] = map->GetPlayer(ObjectGuid(duel->owner[i]));
        pets[i] = owners[i] ? CompanionPet(owners[i]) : nullptr;
    }
    bool startFight = false, finished = false, tell = false;
    Companions::DuelPhase phase = Companions::DuelPhase::Idle;
    Companions::DuelResult result;
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        if (duel->over || now - duel->lastDriveMs < 250)
            return;
        duel->lastDriveMs = now;
        Companions::DuelPhase const before = duel->rules.Phase();
        std::array<float, 2> share{0.f, 0.f};
        for (int i = 0; i < 2; ++i)
        {
            // Dismissed, dead, away, swapped for another companion or carried out of the arena:
            // that side fled.
            Pet* p = pets[i];
            bool gone = !owners[i] || !owners[i]->IsAlive() || !p || !p->IsAlive();
            if (!gone && duel->pet[i] && p->GetObjectGuid().GetRawValue() != duel->pet[i])
                gone = true;
            if (!gone && before == Companions::DuelPhase::Fighting &&
                !Companions::CompanionDuel::InArena(duel->center, PosOf(p)))
                gone = true;
            if (gone)
                duel->rules.Fled(duel->owner[i]);
            else
                share[i] = p->GetMaxHealth() ? float(p->GetHealth()) / float(p->GetMaxHealth()) : 0.f;
        }
        duel->rules.Update(now, share[0], share[1]);
        phase = duel->rules.Phase();
        result = duel->rules.Result();
        if (phase == Companions::DuelPhase::Fighting && !duel->fighting)
        {
            duel->fighting = true;
            startFight = true;
        }
        if (phase == Companions::DuelPhase::Finished)
        {
            duel->over = true;
            finished = true;
            m_companionDuels.erase(std::remove(m_companionDuels.begin(), m_companionDuels.end(), duel), m_companionDuels.end());
        }
        if (phase != duel->told)
        {
            duel->told = phase;
            tell = true;
        }
    }

    if (startFight && pets[0] && pets[1])
    {
        {
            std::lock_guard<std::mutex> lock(m_duelFoesLock);
            m_duelFoes[duel->pet[0]] = {duel->pet[1], duel->owner[0]};
            m_duelFoes[duel->pet[1]] = {duel->pet[0], duel->owner[1]};
            m_duelFoesCount = uint32(m_duelFoes.size());
        }
        for (int i = 0; i < 2; ++i)
            if (pets[i]->AI())
                pets[i]->AI()->AttackStart(pets[1 - i]);
    }
    if (tell)
    {
        for (int i = 0; i < 2; ++i)
        {
            if (!owners[i])
                continue;
            Companions::DuelUpdate u;
            u.phase = phase;
            u.end = result.end;
            u.winner = result.winner < 0 ? int8(-1) : int8(result.winner == i ? 0 : 1);
            u.opponent = duel->owner[1 - i];
            SendChannel(owners[i], Companions::kChannelCompanion, Companions::EncodeDuel(u));
            SendCompanionBar(owners[i], true);
        }
    }
    if (!finished)
        return;

    // The end: the two companions stop, their own stats come back (health stays as the fight
    // left it), and the winner earns its experience.
    {
        std::lock_guard<std::mutex> lock(m_duelFoesLock);
        m_duelFoes.erase(duel->pet[0]);
        m_duelFoes.erase(duel->pet[1]);
        m_duelFoesCount = uint32(m_duelFoes.size());
    }
    std::array<uint8, 2> levels{1, 1};
    for (int i = 0; i < 2; ++i)
    {
        if (!owners[i])
            continue;
        FusionCompanionOut& out = owners[i]->GetFusion().companion;
        Item* item = pets[i] ? owners[i]->GetItemByGuid(out.item) : nullptr;
        Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
        if (pets[i] && pets[i]->IsAlive())
        {
            pets[i]->AttackStop();
            pets[i]->CombatStop(true);
        }
        if (sp)
        {
            Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
            levels[i] = Companions::EffectiveLevel(s, uint8(owners[i]->GetLevel()));
            if (out.duelStats && pets[i]->IsAlive())
            {
                BuildCompanion(pets[i], *sp, s, levels[i], false);
                // As hurt as the fight left it, never better than it went in.
                float const share = pets[i]->GetMaxHealth() ? float(pets[i]->GetHealth()) / float(pets[i]->GetMaxHealth()) : 1.f;
                float const keep = std::min(share, duel->preHealth[i]);
                pets[i]->SetHealth(std::max<uint32>(1, uint32(keep * float(pets[i]->GetMaxHealth()) + 0.5f)));
            }
        }
        out.duelStats = false;
        out.builtLevel = uint8(owners[i]->GetLevel());
    }
    static char const* const kEnds[] = {"", "beaten", "forfeited", "fled", "an owner stepped in", "time ran out",
        "declined", "not answered", "called off"};
    char const* why = uint8(result.end) < 9 ? kEnds[uint8(result.end)] : "";
    for (int i = 0; i < 2; ++i)
    {
        if (!owners[i])
            continue;
        FusionCompanionTell chat(owners[i]);
        if (result.winner < 0)
            chat.PSendSysMessage("|cffffd200Companion duel over: no winner (%s).|r", why);
        else if (result.winner == i)
            chat.PSendSysMessage("|cff40ff40Your companion won the duel (%s).|r", why);
        else
            chat.PSendSysMessage("|cffff4040Your companion lost the duel (%s).|r", why);
    }
    int const w = result.winner;
    if (w >= 0 && owners[w])
    {
        // Only a fight won pays: a forfeit, a flight or a meddling owner pays nothing.
        uint32 xp = Companions::DuelPaysXp(result.end) ? Companions::DuelWinXp(levels[w], levels[1 - w]) : 0;
        bool capped = false;
        if (xp)
        {
            // At most a few paid wins a day for the winner's account, every character of it
            // together (Companions::PayDuelWin); kept across restarts.
            uint32 const day = uint32(time(nullptr) / 86400);
            uint32 const account = owners[w]->GetSession()->GetAccountId();
            bool known;
            {
                std::lock_guard<std::mutex> lock(m_companionLock);
                known = m_companionDuelXp.count(account) != 0;
            }
            Companions::DuelWinsToday saved;
            if (!known)
                if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
                        "SELECT `day`, `wins` FROM `account_fusion_companion_duel` WHERE `account` = %u", account))
                    saved = {r->Fetch()[0].GetUInt32(), r->Fetch()[1].GetUInt32()};
            Companions::DuelWinsToday counted;
            {
                std::lock_guard<std::mutex> lock(m_companionLock);
                auto const [it, fresh] = m_companionDuelXp.try_emplace(account, saved);
                (void)fresh;
                capped = !Companions::PayDuelWin(it->second, day);
                counted = it->second;
            }
            if (capped)
                xp = 0;
            else
                CharacterDatabase.PExecute("REPLACE INTO `account_fusion_companion_duel` (`account`, `day`, `wins`) VALUES (%u, %u, %u)",
                    account, counted.day, counted.wins);
        }
        if (capped)
            FusionCompanionTell(owners[w]).PSendSysMessage("No experience: your companions have won duels for it %u times today.",
                Companions::kDuelWinsPerDay);
        GiveCompanionXp(owners[w], xp);
    }
    for (int i = 0; i < 2; ++i)
        if (owners[i])
            SendCompanionBar(owners[i], true);
}

// ── Requests from the client ────────────────────────────────────────────────────────────────

void FusionMgr::HandleCompanionChannel(Player* player, std::vector<uint8> const& body)
{
    if (!player->IsInWorld())
        return;
    // The Companions window's own requests (FusionCompanionIndex.cpp).
    Companions::Index::Request w;
    if (Companions::Index::DecodeRequest(body, w))
    {
        HandleCompanionWindow(player, w);
        return;
    }
    Companions::Request r;
    if (!Companions::DecodeRequest(body, r))
        return;
    switch (r.op)
    {
        case Companions::kOpSummon:
            if (Item* item = player->GetItemByGuid(ObjectGuid(r.guid)))
                UseCompanionItem(player, item);
            break;
        case Companions::kOpDismiss:
            if (CompanionDuelOf(player->GetObjectGuid().GetRawValue()))
                ForfeitCompanionDuel(player);
            DismissCompanion(player);
            break;
        case Companions::kOpEvolve:
            EvolveCompanion(player, r.guid);
            break;
        case Companions::kOpDuelChallenge:
            // 0: the player's own target (the Companions window's "Duel target").
            ChallengeCompanionDuel(player, r.guid ? ObjectGuid(r.guid) : player->GetSelectionGuid());
            break;
        case Companions::kOpDuelAnswer:
            AnswerCompanionDuel(player, r.accept);
            break;
        case Companions::kOpDuelForfeit:
            ForfeitCompanionDuel(player);
            break;
        case Companions::kOpRide:
            RideCompanion(player);
            break;
        case Companions::kOpPick:
            PickCompanionAbility(player, r.guid, r.slot, r.option);
            break;
        case Companions::kOpAutocast:
        {
            if (r.slot == Companions::PetBarSlot::Mounted)
                break;      // the mount button never casts by itself
            FusionCompanionOut& c = player->GetFusion().companion;
            c.autocast[r.slot] = r.accept;
            if (Pet* pet = CompanionPet(player))
                if (CharmInfo* ci = pet->GetCharmInfo())
                    if (UnitActionBarEntry const* e = ci->GetActionBarEntry(r.slot))
                        if (e->IsActionBarForSpell() && e->GetAction())
                        {
                            ci->SetSpellAutocast(e->GetAction(), r.accept);
                            player->PetSpellInitialize();
                        }
            SendCompanionMoves(player);
            break;
        }
        case Companions::kOpBarPress:
        {
            // The pet bar aimed by the crosshair (fusion/CompanionMoves.h, PetBarSlot).
            namespace Slot = Companions::PetBarSlot;
            Fusion::Vec3 const* point = (r.flags & Companions::PRESS_POINT) ? &r.point : nullptr;
            switch (r.slot)
            {
                case Slot::Command:
                    CompanionCommand(player, r.guid, point, (r.flags & Companions::PRESS_DOUBLE) || (!r.guid && !point));
                    break;
                case Slot::Stance:
                case Slot::Hold:
                    CompanionStance(player, r.slot);
                    break;
                case Slot::Follow:
                    CompanionCommand(player, 0, nullptr, true);
                    break;
                case Slot::Stay:
                    if (Pet* pet = CompanionPet(player))
                        pet->HandlePetCommand(COMMAND_STAY, nullptr);
                    break;
                default:
                    CastCompanionAbility(player, r.slot, r.guid, point, true);
                    break;
            }
            break;
        }
        default:
            break;
    }
}

// ── Commands ────────────────────────────────────────────────────────────────────────────────

Item* FusionMgr::GiveCompanion(Player* player, uint16 species, uint8 level, uint8 stage)
{
    Companions::CompanionSpecies const* sp = Companions::FindSpecies(m_companionSpecies, species);
    if (!sp || !sObjectMgr.GetItemPrototype(sp->item))
        return nullptr;
    ItemPosCountVec dest;
    InventoryResult const msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, sp->item, 1);
    if (msg != EQUIP_ERR_OK)
    {
        player->SendEquipError(msg, nullptr, nullptr, 0, sp->item);
        return nullptr;
    }
    Item* item = player->StoreNewItem(dest, sp->item, true, Item::GenerateItemRandomPropertyId(sp->item));
    if (!item)
        return nullptr;
    player->SendNewItem(item, 1, false, true);
    // The new item rolled its traits when it was made (StampCompanionItem): keep them, set the
    // level and stage asked for.
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    s.species = sp->id;
    s.xp = 0;
    s.level = std::max<uint8>(1, std::min<uint8>(level, Companions::kMaxLevel));
    s.stage = std::min<uint8>(stage, uint8(sp->stageCount - 1));
    SaveCompanionState(item->GetGUIDLow(), s);
    StampCompanionItem(item);
    return item;
}

// .fusion pet [<species> [level] [stage]]: an admin's companion item for testing; with no
// argument, the species list.
bool ChatHandler::HandleFusionPetCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player) || !player->HasFusion())
    {
        SendSysMessage("Fusion: only an admin may make companions.");
        SetSentErrorMessage(true);
        return false;
    }
    std::string const line = args ? args : "";
    std::istringstream words(line);
    std::string first;
    words >> first;
    if (first == "breed")
    {
        std::string mode;
        words >> mode;
        sFusionMgr.BreedFirstPair(player, mode == "free");
        return true;
    }
    if (first == "pick")
    {
        uint32 st = 0, option = 0;
        words >> st >> option;
        FusionCompanionOut const& c = player->GetFusion().companion;
        if (c.item.IsEmpty() || st < 1 || st > 3 || option < 1 || option > 2)
        {
            SendSysMessage("Fusion: .fusion pet pick <stage 1-3> <1|2> (the companion out).");
            return true;
        }
        std::vector<uint8> body = Fusion::Companions::EncodeRequest({Fusion::Companions::kOpPick, c.item.GetRawValue(),
            false, uint8(st - 1), uint8(option)});
        sFusionMgr.HandleCompanionChannel(player, body);
        return true;
    }
    std::istringstream in(line);
    uint32 species = 0, level = 1, stage = 0;
    if (!(in >> species))
    {
        std::string list;
        for (Fusion::Companions::CompanionSpecies const& sp : sFusionMgr.CompanionSpeciesList())
            list += (list.empty() ? "" : ", ") + std::to_string(sp.id) + " " + sp.name;
        PSendSysMessage("Fusion: .fusion pet <species> [level] [stage] [trait,trait,...] [twinN] [genN] | "
            ".fusion pet breed [free] | .fusion pet pick <stage> <1|2>. Species: %s.", list.c_str());
        return true;
    }
    in >> level >> stage;
    std::string rest;
    std::getline(in, rest);
    uint8 twin = 0, gen = 0;
    bool anyTraits = false;
    uint32 const traits = FusionCompanionTraitsFromText(rest, twin, gen, anyTraits);
    Item* item = sFusionMgr.GiveCompanion(player, uint16(species), uint8(std::min<uint32>(level, 255)),
        uint8(stage ? stage - 1 : 0), anyTraits ? traits : 0xFFFFFFFF, twin, gen);
    if (!item)
    {
        PSendSysMessage("Fusion: no companion species %u (or its item is missing, or your bags are full).", species);
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("Fusion: a level %u companion of species %u is in your bags.", std::max<uint32>(1, std::min<uint32>(level, 60)), species);
    return true;
}

// .fusion petduel [accept | decline | forfeit]: challenge your target's companion with yours,
// or answer a challenge.
bool ChatHandler::HandleFusionPetDuelCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !player->HasFusion())
        return false;
    std::string const arg = args ? std::string(args) : std::string();
    if (arg == "accept")
        sFusionMgr.AnswerCompanionDuel(player, true);
    else if (arg == "decline")
        sFusionMgr.AnswerCompanionDuel(player, false);
    else if (arg == "forfeit")
        sFusionMgr.ForfeitCompanionDuel(player);
    else
        sFusionMgr.ChallengeCompanionDuel(player, player->GetSelectionGuid());
    return true;
}
