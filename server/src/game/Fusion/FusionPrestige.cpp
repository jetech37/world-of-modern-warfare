/*
 * World of Modern Warfare: prestige, the professions' field gear and MW2's modes in battlegrounds
 * and the Gurubashi Arena, in the game (fusion/Prestige.h, FieldGear.h, Match.h; the why in
 * src/fusion/docs/PRESTIGE.md).
 *
 * - Prestige: the state loads with the achievements (it adds camos to the unlocks); the client's
 *   Prestige tab asks on channel 8, the server names what will be lost and a nonce, the client
 *   confirms with it; the reset is done here (level, the account's unlocks, a skill). Milestones
 *   are looked at every few seconds and paid once. The Prestige tab's token shop sells for tokens.
 * - Field gear: Engineering charges and Alchemy stims are used from the bags; a bandolier (in a
 *   bag slot) and a ghillie mantle (the cloak) are read off what the player wears. Their recipes
 *   are learned and made at the profession's trainers (vanilla's recipes are client spells we
 *   cannot add, so the fusion crafts these itself, as it does Construction's parts).
 * - Matches: MW2's points in Warsong Gulch, Arathi Basin and Alterac Valley, a HUD and a Tab
 *   scoreboard; Team Deathmatch in the Gurubashi Arena with respawns in the arena.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "BattleGround.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Util.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <sstream>

using Fusion::Prestige::Track;

namespace
{
    // Gossip from the fusion's profession menus: its own sender, apart from vanilla's menus and
    // Construction's (1).
    constexpr uint32 kFieldSender = 0x5738;
    enum FieldAction : uint32
    {
        kOpenTrade = 1,         // << 16 | skill line
        kLearn = 2,             // << 16 | recipe index
        kCraft = 3,             // << 16 | recipe index
        kBackToTrainer = 4,
    };
    uint32 Action(FieldAction a, uint32 arg) { return uint32(a) << 16 | (arg & 0xFFFF); }

    // The professions with field recipes, in the order their trainers are looked for.
    constexpr uint16 kTradeLines[] = {202, 165, 197, 171};

    constexpr uint32 kWornGearEveryMs = 1000;
    constexpr uint32 kMilestoneEveryMs = 5000;
    constexpr uint32 kMatchHudEveryMs = 1000;
    constexpr uint32 kBoardForgetMs = 3 * 3600 * 1000;

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

    std::string ItemName(uint32 entry)
    {
        ItemPrototype const* p = sObjectMgr.GetItemPrototype(entry);
        return p ? p->Name1 : "item " + std::to_string(entry);
    }

    // Put `count` of an item in the bags, else in the mail (a reward is never lost to full bags).
    void GiveOrMail(Player* player, uint32 entry, uint32 count, char const* subject)
    {
        if (!count || !sObjectMgr.GetItemPrototype(entry))
            return;
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, count) == EQUIP_ERR_OK)
        {
            if (Item* stored = player->StoreNewItem(dest, entry, true, Item::GenerateItemRandomPropertyId(entry)))
            {
                player->SendNewItem(stored, count, true, false);
                return;
            }
        }
        Item* item = Item::CreateItem(entry, count, player->GetObjectGuid());
        if (!item)
            return;
        CharacterDatabase.BeginTransaction(player->GetGUIDLow());
        item->SaveToDB();
        CharacterDatabase.CommitTransaction();
        MailDraft(subject, "Your bags were full.").AddItem(item)
            .SendMailTo(player, MailSender(player, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
        ChatHandler(player).PSendSysMessage("Your bags are full: %s was mailed to you.", ItemName(entry).c_str());
    }

    uint8 TeamOf(Player const* p)
    {
        return p->GetBGTeam() == ALLIANCE ? Fusion::Match::kAlliance : Fusion::Match::kHorde;
    }

    Fusion::Vec3 PosOf(Player const* p)
    {
        return {p->GetPositionX(), p->GetPositionY(), p->GetPositionZ()};
    }

    // The skill line a profession trainer teaches, of those with field recipes; 0 for none.
    uint16 TradeLineOf(Creature const* c)
    {
        if (!c->IsTrainer())
            return 0;
        CreatureInfo const* ci = c->GetCreatureInfo();
        if (!ci || ci->trainer_type != TRAINER_TYPE_TRADESKILLS)
            return 0;
        for (TrainerSpellData const* data : {c->GetTrainerSpells(), c->GetTrainerTemplateSpells()})
        {
            if (!data)
                continue;
            for (uint16 line : kTradeLines)
                for (auto const& kv : data->spellList)
                    if (kv.second.reqSkill == line)
                        return line;
        }
        return 0;
    }

    void AddItem(Player* player, uint8 icon, std::string const& text, uint32 action)
    {
        player->PlayerTalkClass->GetGossipMenu().AddMenuItem(icon, text.c_str(), kFieldSender, action, nullptr, false);
    }

    char const* ColorName(Fusion::Construction::Color c)
    {
        switch (c)
        {
            case Fusion::Construction::Color::Orange: return "orange";
            case Fusion::Construction::Color::Yellow: return "yellow";
            case Fusion::Construction::Color::Green: return "green";
            case Fusion::Construction::Color::Gray: return "gray";
            default: return "red";
        }
    }

    std::string Reagents(std::vector<Fusion::Construction::Material> const& list)
    {
        std::ostringstream o;
        for (size_t i = 0; i < list.size(); ++i)
            o << (i ? ", " : "") << list[i].count << " " << ItemName(list[i].item);
        return o.str();
    }
}

// ── Loading ─────────────────────────────────────────────────────────────────────────────────

void FusionMgr::LoadPrestigeTables()
{
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_prestige` ("
        "`guid` int(11) unsigned NOT NULL, `level_rank` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "`tokens` int(11) unsigned NOT NULL DEFAULT '0', `milestones` int(11) unsigned NOT NULL DEFAULT '0', "
        "`cosmetics` int(11) unsigned NOT NULL DEFAULT '0', `title` smallint(5) unsigned NOT NULL DEFAULT '0', "
        "`tracer` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_prestige_skill` ("
        "`guid` int(11) unsigned NOT NULL, `skill` smallint(5) unsigned NOT NULL, "
        "`rank` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "PRIMARY KEY (`guid`, `skill`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `account_fusion_prestige` ("
        "`account` int(11) unsigned NOT NULL, `gun_rank` tinyint(3) unsigned NOT NULL DEFAULT '0', "
        "PRIMARY KEY (`account`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    // The milestones claimed once an account (Prestige::kAccountMilestones), added after the table
    // was made; every character's claim from before counts for its account (safe on every start:
    // it only adds bits).
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `account_fusion_prestige` ADD COLUMN IF NOT EXISTS "
        "`milestones` int(11) unsigned NOT NULL DEFAULT '0' COMMENT 'Prestige::kAccountMilestones claimed'");
    CharacterDatabase.DirectPExecute(
        "UPDATE `account_fusion_prestige` a JOIN (SELECT c.`account`, BIT_OR(p.`milestones` & %u) AS m "
        "FROM `character_fusion_prestige` p JOIN `characters` c ON c.`guid` = p.`guid` GROUP BY c.`account`) x "
        "ON x.`account` = a.`account` SET a.`milestones` = a.`milestones` | x.m",
        Fusion::Prestige::kAccountMilestones);
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_trade_recipe` ("
        "`guid` int(11) unsigned NOT NULL, `recipe` int(11) unsigned NOT NULL, "
        "PRIMARY KEY (`guid`, `recipe`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");

    // The field recipes (world DB, 11_prestige_professions.sql); none without the table.
    m_tradeRecipes.clear();
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(
            "SELECT `id`, `name`, `skill_line`, `required`, `yellow`, `green`, `gray`, `product`, `product_count`, "
            "`reagent1`, `count1`, `reagent2`, `count2`, `reagent3`, `count3`, `reagent4`, `count4`, `cost` "
            "FROM `fusion_trade_recipe` ORDER BY `skill_line`, `required`, `id`"))
    {
        do
        {
            Field* f = r->Fetch();
            TradeRecipe t;
            t.recipe.id = f[0].GetUInt32();
            t.name = f[1].GetCppString();
            t.recipe.skillLine = f[2].GetUInt16();
            t.recipe.skill = {f[3].GetUInt16(), f[4].GetUInt16(), f[5].GetUInt16(), f[6].GetUInt16()};
            t.recipe.product = f[7].GetUInt32();
            t.recipe.productCount = std::max<uint32>(1, f[8].GetUInt32());
            for (int k = 0; k < 4; ++k)
                if (uint32 const item = f[9 + 2 * k].GetUInt32(), count = f[10 + 2 * k].GetUInt32(); item && count)
                    t.recipe.reagents.push_back({item, count});
            t.cost = f[17].GetUInt32();
            if (!sObjectMgr.GetItemPrototype(t.recipe.product))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: fusion_trade_recipe %u makes item %u, which does not exist.",
                    t.recipe.id, t.recipe.product);
                continue;
            }
            m_tradeRecipes.push_back(std::move(t));
        } while (r->NextRow() && m_tradeRecipes.size() < 0xFFFF);
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: prestige tables ready, %u field recipes.", uint32(m_tradeRecipes.size()));
}

void FusionMgr::LoadPrestige(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const guid = player->GetGUIDLow();
    d.prestige = Fusion::Prestige::State();
    d.tradeRecipes.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `level_rank`, `tokens`, `milestones`, `cosmetics`, `title`, `tracer` "
            "FROM `character_fusion_prestige` WHERE `guid` = %u", guid))
    {
        Field* f = r->Fetch();
        d.prestige.level = std::min<uint8>(f[0].GetUInt8(), Fusion::Prestige::kMaxRank);
        d.prestige.tokens = f[1].GetUInt32();
        d.prestige.milestones = f[2].GetUInt32();
        d.prestige.cosmetics = f[3].GetUInt32();
        d.prestige.title = f[4].GetUInt16();
        uint8 const tracer = f[5].GetUInt8();
        d.prestige.tracer = tracer < uint8(Fusion::Prestige::Tracer::Count) ? Fusion::Prestige::Tracer(tracer)
            : Fusion::Prestige::Tracer::Default;
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `skill`, `rank` FROM `character_fusion_prestige_skill` WHERE `guid` = %u", guid))
    {
        do
        {
            Field* f = r->Fetch();
            d.prestige.professions[f[0].GetUInt16()] = std::min<uint8>(f[1].GetUInt8(), Fusion::Prestige::kMaxRank);
        } while (r->NextRow());
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `gun_rank`, `milestones` FROM `account_fusion_prestige` WHERE `account` = %u", player->GetSession()->GetAccountId()))
    {
        d.prestige.guns = std::min<uint8>(r->Fetch()[0].GetUInt8(), Fusion::Prestige::kMaxRank);
        d.prestige.accountMilestones = r->Fetch()[1].GetUInt32() & Fusion::Prestige::kAccountMilestones;
    }
    // This character's own claim counts for its account too.
    d.prestige.accountMilestones |= d.prestige.milestones & Fusion::Prestige::kAccountMilestones;
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `recipe` FROM `character_fusion_trade_recipe` WHERE `guid` = %u", guid))
    {
        do
            d.tradeRecipes.insert(r->Fetch()[0].GetUInt32());
        while (r->NextRow());
    }
    // A title no longer held (a table edited by hand) is not worn.
    if (!Fusion::Prestige::HoldsTitle(d.prestige, d.prestige.title))
        d.prestige.title = 0;
    if (!d.prestige.HasTracer(d.prestige.tracer))
        d.prestige.tracer = Fusion::Prestige::Tracer::Default;
    d.gunsMaxedFor = size_t(-1);
    d.prestigeLoaded = true;
}

void FusionMgr::SavePrestige(Player* player)
{
    FusionPlayerData const& d = player->GetFusionConst();
    uint32 const guid = player->GetGUIDLow();
    CharacterDatabase.PExecute(
        "REPLACE INTO `character_fusion_prestige` (`guid`, `level_rank`, `tokens`, `milestones`, `cosmetics`, `title`, `tracer`) "
        "VALUES (%u, %u, %u, %u, %u, %u, %u)",
        guid, uint32(d.prestige.level), d.prestige.tokens, d.prestige.milestones, d.prestige.cosmetics,
        uint32(d.prestige.title), uint32(d.prestige.tracer));
    for (auto const& kv : d.prestige.professions)
        CharacterDatabase.PExecute(
            "REPLACE INTO `character_fusion_prestige_skill` (`guid`, `skill`, `rank`) VALUES (%u, %u, %u)",
            guid, uint32(kv.first), uint32(kv.second));
    // The account's milestones only ever gain bits: never lost to an older save.
    CharacterDatabase.PExecute("INSERT INTO `account_fusion_prestige` (`account`, `gun_rank`, `milestones`) VALUES (%u, %u, %u) "
        "ON DUPLICATE KEY UPDATE `gun_rank` = VALUES(`gun_rank`), `milestones` = `milestones` | VALUES(`milestones`)",
        player->GetSession()->GetAccountId(), uint32(d.prestige.guns), d.prestige.accountMilestones);
}

Fusion::UnlockSet FusionMgr::UnlocksOf(Player const* player) const
{
    FusionPlayerData const& d = player->GetFusionConst();
    Fusion::UnlockSet u = Fusion::UnlocksFor(m_catalog, m_book, d.earned, IsAdmin(player));
    Fusion::Prestige::GrantPrestigeUnlocks(m_catalog, d.prestige, u);
    return u;
}

Fusion::Prestige::Standing FusionMgr::StandingOf(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    if (d.gunsMaxedFor != d.earned.size())
    {
        d.gunsMaxed = Fusion::Prestige::GunsMaxed(m_book, d.earned);
        d.gunsMaxedFor = d.earned.size();
    }
    Fusion::Prestige::Standing s;
    s.level = player->GetLevel();
    s.gunsMaxed = d.gunsMaxed;
    for (uint16 line : Fusion::Prestige::Professions())
        if (player->HasSkill(line))
            s.skills[line] = player->GetSkillValuePure(line);
    return s;
}

// ── The Prestige tab (channel 8) ────────────────────────────────────────────────────────────

void FusionMgr::SendPrestigeState(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    Fusion::Prestige::View v;
    v.state = d.prestige;
    v.standing = StandingOf(player);
    v.busy = player->IsInCombat() || !player->IsAlive() || player->InBattleGround() || d.inArena;
    SendChannel(player, Fusion::Prestige::kChannelPrestige, Fusion::Prestige::EncodeState(v));
}

void FusionMgr::SendLook(Player* player)
{
    SendChannel(player, Fusion::Prestige::kChannelPrestige, Fusion::Prestige::EncodeLook(player->GetFusionConst().prestige.tracer));
}

void FusionMgr::HandlePrestigeChannel(Player* player, std::vector<uint8> const& body)
{
    FusionPlayerData& d = player->GetFusion();
    if (!d.prestigeLoaded)
        return;
    using namespace Fusion::Prestige;
    // The scoreboard (Tab held): the match ops share the channel.
    if (body.size() >= 2 && body[0] == kVersion && body[1] == Fusion::Match::kOpScoreboard)
    {
        SendScoreboard(player);
        return;
    }
    ClientMessage m;
    if (!DecodeClient(std::vector<uint8_t>(body.begin(), body.end()), m))
        return;
    uint32 const now = WorldTimer::getMSTime();
    auto refuse = [&](std::string const& why) {
        SendChannel(player, kChannelPrestige, EncodeResult(false, why));
    };
    bool const busy = player->IsInCombat() || !player->IsAlive() || player->InBattleGround() || d.inArena;
    switch (m.op)
    {
        case Op::Request:
            SendPrestigeState(player);
            SendLook(player);
            SendChannel(player, kChannelPrestige, EncodeShop(d.prestige));
            break;
        case Op::Buy:
            BuyGood(player, m.good);
            break;
        case Op::Ask:
        {
            Refusal const r = busy ? Refusal::Busy : CanPrestige(d.prestige, StandingOf(player), m.track, m.key);
            if (r != Refusal::None)
                return refuse(RefusalText(r));
            Ask(d.prestigeAsk, m.track, m.key, urand(1, 0x7FFFFFFF), now);
            SendChannel(player, kChannelPrestige, EncodePrompt(d.prestigeAsk.nonce, m.track, m.key, WhatItCosts(m.track, m.key)));
            break;
        }
        case Op::Confirm:
        {
            Track track;
            uint16_t key;
            if (!Confirm(d.prestigeAsk, m.nonce, now, track, key))
                return refuse(RefusalText(Refusal::NothingAsked));
            // Asked again at the moment it happens: the standing may have changed meanwhile.
            Refusal const r = busy ? Refusal::Busy : CanPrestige(d.prestige, StandingOf(player), track, key);
            if (r != Refusal::None)
                return refuse(RefusalText(r));
            DoPrestige(player, track, key);
            break;
        }
        case Op::Cancel:
            d.prestigeAsk.active = false;
            break;
        case Op::SetTitle:
            if (!HoldsTitle(d.prestige, m.title))
                return refuse("You do not hold that title.");
            d.prestige.title = m.title;
            SavePrestige(player);
            SendPrestigeState(player);
            break;
        case Op::SetTracer:
            if (!d.prestige.HasTracer(m.tracer))
                return refuse("You do not own that tracer colour.");
            d.prestige.tracer = m.tracer;
            SavePrestige(player);
            SendLook(player);
            SendPrestigeState(player);
            break;
        default:
            break;
    }
}

void FusionMgr::DoPrestige(Player* player, Track track, uint16 key)
{
    using namespace Fusion::Prestige;
    FusionPlayerData& d = player->GetFusion();
    ChatHandler chat(player);
    std::string what;
    switch (track)
    {
        case Track::Level:
        {
            // Gear the new level cannot wear goes to the bags (or the mail), then level 1.
            player->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
            for (uint32 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                if (Item* it = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    if (it->GetProto()->RequiredLevel > 1)
                        player->AutoUnequipItemFromSlot(slot);
            player->GiveLevel(1);
            player->InitTalentForLevel();
            player->SetUInt32Value(PLAYER_XP, 0);
            player->SetHealth(player->GetMaxHealth());
            what = "level";
            break;
        }
        case Track::Guns:
        {
            // The account's unlocks back to the starter kit: the Feats stay, the counters restart.
            uint32 const account = player->GetSession()->GetAccountId();
            std::set<uint32_t> const kept = KeptAfterGunPrestige(m_book, d.earned);
            CharacterDatabase.PExecute("DELETE FROM `account_fusion_stats` WHERE `account` = %u", account);
            CharacterDatabase.PExecute("DELETE FROM `account_fusion_achievement` WHERE `account` = %u", account);
            for (uint32 id : kept)
                CharacterDatabase.PExecute(
                    "INSERT IGNORE INTO `account_fusion_achievement` (`account`, `achievement`, `earned_at`, `character`) "
                    "VALUES (%u, %u, %u, %u)", account, id, uint32(time(nullptr)), player->GetGUIDLow());
            d.earned = kept;
            d.stats = Fusion::AccountStats();
            d.stats.Record({Fusion::Stat::Level, Fusion::kAnyKey}, player->GetLevel());
            d.gunsMaxedFor = size_t(-1);
            what = "guns";
            break;
        }
        case Track::Profession:
        {
            // The skill to 1, its trained rank (the max) and its recipes kept.
            player->SetSkill(key, kSkillAfterPrestige, player->GetSkillMaxPure(key));
            what = ProfessionName(key);
            break;
        }
    }
    Reward const r = Apply(d.prestige, track, key);
    uint8 const rank = RankOf(d.prestige, track, key);
    if (!d.prestige.title && r.title)
        d.prestige.title = r.title;
    SavePrestige(player);

    // The unlocks: a gun prestige's reset, a new camo on every gun.
    if (track == Track::Guns || r.camo)
    {
        d.unlocks = UnlocksOf(player);
        d.streak.SetLoadout(Fusion::KitStreaks(d.Equipped(), Fusion::DefaultStreakLadder(), d.unlocks), d.perks.hardline);
        SendUnlocks(player);
        if (track == Track::Guns)
        {
            SendAchievementPages(player);
            // The kit in hand may now hold locked parts: it is put down until edited.
            if (Fusion::ClassLoadout const* l = d.Equipped();
                l && Fusion::ValidateLoadout(m_catalog, *l, d.unlocks, ProPerks(player)) != Fusion::LoadoutError::None)
            {
                uint8 const slot = d.equippedSlot;
                d.applied.reset();
                d.equippedSlot = Fusion::kNoClass;
                ApplyPerks(player);
                SendLoadout(player, Fusion::Packets::LoadoutStatus::Saved);
                // The kit window says why (it holds gear no longer unlocked), not the chat.
                SendLoadout(player, Fusion::Packets::LoadoutStatus::Rejected, Fusion::LoadoutError::Locked, slot);
            }
        }
    }
    if (track == Track::Level)
        player->SaveToDB();

    std::ostringstream o;
    o << "Prestige " << uint32(rank) << " (" << what << "): " << r.tokens << " prestige tokens";
    if (r.title)
        o << ", the title \"" << TitleText(r.title) << "\"";
    if (r.tracer != Tracer::Default)
        o << ", " << TracerName(r.tracer) << " tracers";
    if (r.camo)
        if (Fusion::Camo const* c = m_catalog.FindCamo(r.camo))
            o << ", " << c->name << " camo on every gun";
    o << ".";
    chat.PSendSysMessage("|cffffd200%s|r", o.str().c_str());
    SendChannel(player, kChannelPrestige, EncodeResult(true, o.str()));
    SendPrestigeState(player);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %s prestiged %s to rank %u.", player->GetName(), what.c_str(), uint32(rank));
}

void FusionMgr::PayMilestone(Player* player, Fusion::Prestige::Milestone const& m)
{
    FusionPlayerData& d = player->GetFusion();
    if (!Fusion::Prestige::ClaimMilestone(d.prestige, m.id))
        return;
    uint32 const money = std::min<uint32>(m.goldCopper, MAX_MONEY_AMOUNT - std::min<uint32>(player->GetMoney(), MAX_MONEY_AMOUNT));
    player->ModifyMoney(int32(money));
    if (m.companionItem)
        GiveOrMail(player, m.companionItem, 1, "A prestige milestone");
    if (!d.prestige.title)
        d.prestige.title = m.title;
    SavePrestige(player);
    if (m.camo)
    {
        d.unlocks = UnlocksOf(player);
        SendUnlocks(player);
    }
    std::string const text = std::string("Prestige milestone: ") + m.name + "! " + Money(m.goldCopper) + ", "
        + std::to_string(m.tokens) + " tokens and more.";
    ChatHandler(player).PSendSysMessage("|cffff8000%s|r", text.c_str());
    SendChannel(player, Fusion::Prestige::kChannelPrestige, Fusion::Prestige::EncodeResult(true, text));
    SendPrestigeState(player);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %s reached the prestige milestone %s.", player->GetName(), m.name);
}

// ── Every update ────────────────────────────────────────────────────────────────────────────

void FusionMgr::UpdatePrestige(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    if (!d.prestigeLoaded)
        return;
    if (int32(now - d.nextPrestigeMs) >= 0)
    {
        bool const first = d.nextPrestigeMs == 0;
        d.nextPrestigeMs = now + kWornGearEveryMs;
        if (first)
            SendLook(player);
        UpdateWornFieldGear(player);
    }
    if (int32(now - d.nextMilestoneMs) >= 0)
    {
        d.nextMilestoneMs = now + kMilestoneEveryMs;
        if (player->IsAlive())
            for (uint8 id : Fusion::Prestige::MilestonesDue(d.prestige, StandingOf(player)))
                if (Fusion::Prestige::Milestone const* m = Fusion::Prestige::FindMilestone(id))
                    PayMilestone(player, *m);
    }
    UpdateArena(player, now);
    if (int32(now - d.nextMatchHudMs) >= 0)
        SendMatchHud(player, now);
}

void FusionMgr::UpdateWornFieldGear(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    std::vector<uint32_t> worn;
    if (Item const* cloak = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK))
        worn.push_back(cloak->GetEntry());
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        if (Item const* bag = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            worn.push_back(bag->GetEntry());
    d.ghillie = Fusion::FieldGear::GhillieMultiplier(worn);
    float const bonus = Fusion::FieldGear::ReserveBonus(worn);
    if (bonus != d.reserveBonus)
    {
        d.reserveBonus = bonus;
        for (auto& kv : d.ammo)
            kv.second.SetReserveBonus(bonus, false);
        if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(d.heldWeaponId))
            SendAmmo(player, *w);
    }
}

uint32 FusionMgr::PrestigeKillXp(Player const* player, uint32 xp) const
{
    if (!m_enabled || !player->HasFusion())
        return xp;
    FusionPlayerData const& d = player->GetFusionConst();
    return uint32(float(xp) * Fusion::Prestige::KillXpMultiplier(d.prestige.level, player->GetLevel()) + 0.5f);
}

void FusionMgr::TitleChat(Player const* player, std::string& message, uint32 type) const
{
    if (!m_enabled || !player->HasFusion())
        return;
    switch (type)
    {
        case CHAT_MSG_SAY: case CHAT_MSG_YELL: case CHAT_MSG_PARTY: case CHAT_MSG_GUILD: case CHAT_MSG_RAID:
        case CHAT_MSG_BATTLEGROUND: case CHAT_MSG_BATTLEGROUND_LEADER: case CHAT_MSG_CHANNEL:
            break;
        default:
            return;
    }
    uint16 const title = player->GetFusionConst().prestige.title;
    if (!title || message.empty() || message[0] == '.')
        return;
    std::string const t = Fusion::Prestige::TitleText(title);
    if (t.empty())
        return;
    message = "[" + t + "] " + message;
    if (message.size() > 255)
        message.resize(255);
}

// ── Field gear ──────────────────────────────────────────────────────────────────────────────

bool FusionMgr::UseFieldItem(Player* player, Item* item)
{
    using namespace Fusion::FieldGear;
    Fusion::FieldGear::Item const* gear = Fusion::FieldGear::Find(item->GetEntry());
    if (!gear || (gear->kind != Kind::Charge && gear->kind != Kind::Stim) || !player->HasFusion())
        return false;
    FusionPlayerData& d = player->GetFusion();
    ChatHandler chat(player);
    uint32 const entry = item->GetEntry();
    if (!player->IsAlive())
    {
        chat.SendSysMessage("You can't do that while dead.");
        return true;
    }
    if (gear->kind == Kind::Charge)
    {
        Fusion::ClassLoadout const* kit = d.Equipped();
        bool const lethal = IsLethal(gear->equipment);
        uint16 const id = kit ? (lethal ? kit->lethal : kit->tactical) : 0;
        Fusion::WeaponProfile const* w = id ? m_catalog.Weapons().Find(id) : nullptr;
        if (!w)
        {
            chat.SendSysMessage("Your kit does not carry that kind of equipment.");
            return true;
        }
        Fusion::GunAmmo& ammo = d.AmmoFor(*w);
        switch (AddCharge(*gear, w, ammo))
        {
            case ChargeResult::Added:
                player->DestroyItemCount(entry, 1, true);
                SendChannel(player, Fusion::Prestige::kChannelPrestige,
                    Fusion::Prestige::EncodeEquipment(lethal ? 0 : 1, uint8(std::min<uint32>(ammo.Mag(), 255))));
                chat.PSendSysMessage("%s: %u ready.", ItemName(entry).c_str(), ammo.Mag());
                break;
            case ChargeResult::Full:
                chat.PSendSysMessage("You already carry %u extra; that is all a kit holds.", kMaxExtraCharges);
                break;
            default:
                chat.SendSysMessage("Your kit does not carry that kind of equipment.");
                break;
        }
        return true;
    }
    uint32 const now = WorldTimer::getMSTime();
    d.stims.Take(gear->stim, now);
    player->DestroyItemCount(entry, 1, true);
    // The stats again now, so the client's handling takes the stim at once.
    d.nextCombatMs = now;
    SendChannel(player, Fusion::Prestige::kChannelPrestige, Fusion::Prestige::EncodeStim(uint8(gear->stim), StimMs(gear->stim)));
    chat.PSendSysMessage("%s takes hold for %u seconds.", ItemName(entry).c_str(), StimMs(gear->stim) / 1000);
    return true;
}

void FusionMgr::ApplyFieldStims(FusionPlayerData const& d, Fusion::Stats::Derived& combat, uint32 now) const
{
    Fusion::FieldGear::ApplyStims(combat, d.stims, now);
}

// ── The profession trainers' field recipes ──────────────────────────────────────────────────

void FusionMgr::AddProfessionGossip(Player* player, Creature* creature)
{
    if (!m_enabled || !player->HasFusion() || m_tradeRecipes.empty())
        return;
    uint16 const line = TradeLineOf(creature);
    if (!line)
        return;
    AddItem(player, GOSSIP_ICON_TRAINER, std::string("Field recipes (") + Fusion::Prestige::ProfessionName(line) + ")",
        Action(kOpenTrade, line));
}

void FusionMgr::TradeMenu(Player* player, Creature* creature, uint16 line)
{
    FusionPlayerData const& d = player->GetFusionConst();
    bool const gm = IsAdmin(player);
    uint16 const skill = player->HasSkill(line) ? player->GetSkillValue(line) : 0;
    ChatHandler(player).PSendSysMessage("%s field recipes. Your skill: %u.", Fusion::Prestige::ProfessionName(line), uint32(skill));
    for (size_t i = 0; i < m_tradeRecipes.size(); ++i)
    {
        TradeRecipe const& t = m_tradeRecipes[i];
        if (t.recipe.skillLine != line)
            continue;
        std::ostringstream o;
        if (!d.tradeRecipes.count(t.recipe.id) && !gm)
        {
            o << "Learn " << t.name << " (" << Money(t.cost) << "; skill " << t.recipe.skill.required << ")";
            AddItem(player, GOSSIP_ICON_TRAINER, o.str(), Action(kLearn, uint32(i)));
            continue;
        }
        o << "Make " << t.name;
        if (t.recipe.productCount > 1)
            o << " x" << t.recipe.productCount;
        o << " (" << Reagents(t.recipe.reagents) << ") ["
          << ColorName(Fusion::Construction::ColorOf(skill, t.recipe.skill)) << "]";
        AddItem(player, GOSSIP_ICON_INTERACT_1, o.str(), Action(kCraft, uint32(i)));
    }
    AddItem(player, GOSSIP_ICON_CHAT, "Back", Action(kBackToTrainer, line));
    player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, creature->GetObjectGuid());
}

void FusionMgr::LearnTrade(Player* player, size_t index)
{
    if (index >= m_tradeRecipes.size())
        return;
    TradeRecipe const& t = m_tradeRecipes[index];
    FusionPlayerData& d = player->GetFusion();
    ChatHandler chat(player);
    if (d.tradeRecipes.count(t.recipe.id))
        return;
    uint16 const skill = player->HasSkill(t.recipe.skillLine) ? player->GetSkillValuePure(t.recipe.skillLine) : 0;
    if (skill < t.recipe.skill.required)
    {
        chat.PSendSysMessage("%s needs %u %s.", t.name.c_str(), uint32(t.recipe.skill.required),
            Fusion::Prestige::ProfessionName(t.recipe.skillLine));
        return;
    }
    if (player->GetMoney() < t.cost)
    {
        chat.PSendSysMessage("Learning %s costs %s.", t.name.c_str(), Money(t.cost).c_str());
        return;
    }
    player->ModifyMoney(-int32(t.cost));
    d.tradeRecipes.insert(t.recipe.id);
    CharacterDatabase.PExecute("INSERT IGNORE INTO `character_fusion_trade_recipe` (`guid`, `recipe`) VALUES (%u, %u)",
        player->GetGUIDLow(), t.recipe.id);
    chat.PSendSysMessage("You have learned how to make %s.", t.name.c_str());
}

void FusionMgr::CraftTrade(Player* player, size_t index)
{
    using namespace Fusion::Construction;
    if (index >= m_tradeRecipes.size())
        return;
    TradeRecipe const& t = m_tradeRecipes[index];
    FusionPlayerData const& d = player->GetFusionConst();
    bool const gm = IsAdmin(player);
    ChatHandler chat(player);
    uint16 const line = t.recipe.skillLine;
    bool const known = player->HasSkill(line);
    if (!gm && !d.tradeRecipes.count(t.recipe.id))
        return;
    uint16 const skill = known ? player->GetSkillValue(line) : 0;
    Inventory bags;
    for (Material const& m : t.recipe.reagents)
        bags[m.item] = player->GetItemCount(m.item);
    switch (CanCraft(t.recipe, skill, bags, gm))
    {
        case CraftCheck::Ok:
            break;
        case CraftCheck::SkillTooLow:
            chat.PSendSysMessage("%s needs %u %s.", t.name.c_str(), uint32(t.recipe.skill.required),
                Fusion::Prestige::ProfessionName(line));
            return;
        default:
            chat.PSendSysMessage("You still need %s.", Reagents(Missing(bags, t.recipe.reagents)).c_str());
            return;
    }
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, t.recipe.product, t.recipe.productCount) != EQUIP_ERR_OK)
    {
        chat.SendSysMessage("Your bags are full.");
        return;
    }
    if (!gm)
        for (Material const& m : t.recipe.reagents)
            player->DestroyItemCount(m.item, m.count, true);
    if (Item* made = player->StoreNewItem(dest, t.recipe.product, true, Item::GenerateItemRandomPropertyId(t.recipe.product)))
        player->SendNewItem(made, t.recipe.productCount, false, true);
    if (gm || !known)
        return;
    uint16 const pure = player->GetSkillValuePure(line);
    if (ApplySkillUp(pure, player->GetSkillMaxPure(line), t.recipe.skill, rand_norm_f()) > pure)
        player->UpdateSkill(line, 1);
}

bool FusionMgr::FieldGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (!m_enabled || sender != kFieldSender || !player->HasFusion())
        return false;
    PlayerMenu* menu = player->PlayerTalkClass;
    uint32 const arg = action & 0xFFFF;
    switch (FieldAction(action >> 16))
    {
        case kOpenTrade:
            menu->ClearMenus();
            TradeMenu(player, creature, uint16(arg));
            return true;
        case kLearn:
        case kCraft:
        {
            if (arg >= m_tradeRecipes.size())
                return true;
            uint16 const line = m_tradeRecipes[arg].recipe.skillLine;
            if (TradeLineOf(creature) != line)
                return true;
            if (FieldAction(action >> 16) == kLearn)
                LearnTrade(player, arg);
            else
                CraftTrade(player, arg);
            menu->ClearMenus();
            TradeMenu(player, creature, line);
            return true;
        }
        case kBackToTrainer:
            menu->ClearMenus();
            player->PrepareGossipMenu(creature, creature->GetDefaultGossipMenuId());
            AddEnchantingGossip(player, creature);
            AddProfessionGossip(player, creature);
            AddKitGossip(player, creature);
            player->SendPreparedGossip(creature);
            return true;
        default:
            return false;
    }
}

// ── The token shop (the Prestige tab) ──────────────────────────────────────────────────────

void FusionMgr::BuyGood(Player* player, uint8 goodId)
{
    using namespace Fusion::Prestige;
    FusionPlayerData& d = player->GetFusion();
    // The answer goes to the tab's result line, never the chat.
    auto answer = [&](bool ok, std::string const& text) {
        SendChannel(player, kChannelPrestige, EncodeResult(ok, text));
    };
    Good const* g = FindGood(goodId);
    if (!g)
        return answer(false, "That is not for sale.");
    // Room first: tokens are never taken for an item that cannot be handed over.
    if (g->kind == GoodKind::Item)
    {
        ItemPosCountVec dest;
        if (!sObjectMgr.GetItemPrototype(g->item)
            || player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, g->item, g->count) != EQUIP_ERR_OK)
            return answer(false, "Make room in your bags first.");
    }
    Fusion::Prestige::BuyResult const r = Buy(d.prestige, goodId);
    if (r != Fusion::Prestige::BuyResult::Ok)
        return answer(false, BuyText(r, *g, d.prestige.tokens));
    if (g->kind == GoodKind::Item)
        GiveOrMail(player, g->item, g->count, "The token shop");
    if (g->kind == GoodKind::Camo)
    {
        d.unlocks = UnlocksOf(player);
        SendUnlocks(player);
    }
    SavePrestige(player);
    std::string text = BuyText(r, *g, d.prestige.tokens);
    if (g->kind == GoodKind::TracerColour)
        text += " Wear it under Tracer colour.";
    answer(true, text);
    SendPrestigeState(player);
    SendChannel(player, kChannelPrestige, EncodeShop(d.prestige));
}

// ── MW2 matches ─────────────────────────────────────────────────────────────────────────────

void FusionMgr::OnBattlegroundScore(uint32 bgInstance, uint32 mapId, Player* player, uint32 type, uint32 value)
{
    if (!m_enabled || !player || !player->HasFusion())
        return;
    Fusion::Match::Mode const mode = Fusion::Match::ModeOfMap(mapId);
    Fusion::Match::Event e;
    if (mode == Fusion::Match::Mode::None || !Fusion::Match::EventOfScoreType(type, e))
        return;
    uint32 const now = WorldTimer::getMSTime();
    uint32 points = 0;
    {
        std::lock_guard<std::mutex> lock(m_matchLock);
        BgBoard& b = m_bgBoards[bgInstance];
        b.mode = mode;
        b.lastMs = now;
        b.board.Join(player->GetObjectGuid().GetRawValue(), player->GetName(), TeamOf(player));
        for (uint32 i = 0; i < std::min<uint32>(value, 10); ++i)
            points += b.board.Record(mode, player->GetObjectGuid().GetRawValue(), e);
        // Battlegrounds long over are forgotten.
        for (auto it = m_bgBoards.begin(); it != m_bgBoards.end();)
            it = now - it->second.lastMs > kBoardForgetMs ? m_bgBoards.erase(it) : std::next(it);
    }
    if (points && e != Fusion::Match::Event::Kill)
        ChatHandler(player).PSendSysMessage("|cffffd200+%u|r", points);
}

void FusionMgr::MatchKill(Player* killer, Player* victim)
{
    if (!m_enabled || !killer || !victim || !victim->HasFusion() || !victim->GetFusionConst().inArena)
        return;
    if (!Fusion::Match::InArena(killer->GetMapId(), PosOf(killer)))
        killer = victim;    // a kill from outside the arena scores nothing
    uint32 const now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(m_matchLock);
    m_arena.Join(killer->GetObjectGuid().GetRawValue(), killer->GetName(), TeamOf(killer));
    m_arena.Join(victim->GetObjectGuid().GetRawValue(), victim->GetName(), TeamOf(victim));
    if (m_arena.OnKill(killer->GetObjectGuid().GetRawValue(), TeamOf(killer), victim->GetObjectGuid().GetRawValue(),
            TeamOf(victim), now))
    {
        ++m_arenaMatchNo;
        uint8 const w = m_arena.Winner();
        m_arenaEndText = w == Fusion::Match::kAlliance ? "The Alliance wins the Team Deathmatch!"
            : w == Fusion::Match::kHorde ? "The Horde wins the Team Deathmatch!" : "The Team Deathmatch is a draw.";
    }
}

void FusionMgr::UpdateArena(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    ChatHandler chat(player);
    if (player->IsAlive())
    {
        d.arenaRespawnMs = 0;
        bool const inside = Fusion::Match::InArena(player->GetMapId(), PosOf(player)) && !player->IsGameMaster();
        if (inside != d.inArena)
        {
            d.inArena = inside;
            if (inside)
            {
                std::lock_guard<std::mutex> lock(m_matchLock);
                m_arena.Join(player->GetObjectGuid().GetRawValue(), player->GetName(), TeamOf(player));
                d.arenaMatchSeen = m_arenaMatchNo;
                chat.PSendSysMessage("|cffffd200Team Deathmatch:|r first to %u kills against the other faction wins. "
                    "You come back in the arena when you die.", m_arena.Rules().scoreLimit);
            }
            else
                chat.SendSysMessage("You left the Team Deathmatch.");
        }
    }
    else if (d.inArena)
    {
        // MW2's respawn: back on the arena floor after a few seconds, no corpse run.
        if (!d.arenaRespawnMs)
            d.arenaRespawnMs = std::max<uint32>(1, now + m_arena.Rules().respawnMs);
        else if (int32(now - d.arenaRespawnMs) >= 0)
        {
            d.arenaRespawnMs = 0;
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
            Fusion::Vec3 const at = Fusion::Match::ArenaSpawn(urand(0, 7));
            float const facing = std::atan2(Fusion::Match::kArenaCentre.y - at.y, Fusion::Match::kArenaCentre.x - at.x);
            player->TeleportTo(Fusion::Match::kArenaMap, at.x, at.y, at.z + 0.5f, facing);
        }
    }
    uint32 matchNo;
    std::string endText;
    {
        std::lock_guard<std::mutex> lock(m_matchLock);
        if (m_arena.Update(now))
        {
            ++m_arenaMatchNo;
            uint8 const w = m_arena.Winner();
            m_arenaEndText = w == Fusion::Match::kAlliance ? "Time! The Alliance wins the Team Deathmatch."
                : w == Fusion::Match::kHorde ? "Time! The Horde wins the Team Deathmatch." : "Time! The Team Deathmatch is a draw.";
        }
        matchNo = m_arenaMatchNo;
        endText = m_arenaEndText;
    }
    if (d.inArena && d.arenaMatchSeen != matchNo)
    {
        d.arenaMatchSeen = matchNo;
        chat.PSendSysMessage("|cffffd200%s|r A new match starts shortly.", endText.c_str());
        SendChannel(player, Fusion::Prestige::kChannelPrestige, Fusion::Match::EncodeMatchEnd(m_arena.Winner(), endText));
    }
}

void FusionMgr::SendMatchHud(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    Fusion::Match::Hud h;
    uint64 const guid = player->GetObjectGuid().GetRawValue();
    BattleGround const* bg = player->InBattleGround() ? player->GetBattleGround() : nullptr;
    Fusion::Match::Mode const bgMode = bg ? Fusion::Match::ModeOfMap(bg->GetMapId()) : Fusion::Match::Mode::None;
    {
        std::lock_guard<std::mutex> lock(m_matchLock);
        Fusion::Match::Board const* board = nullptr;
        if (bgMode != Fusion::Match::Mode::None)
        {
            BgBoard& b = m_bgBoards[player->GetBattleGround()->GetInstanceID()];
            b.mode = bgMode;
            b.lastMs = now;
            b.board.Join(guid, player->GetName(), TeamOf(player));
            board = &b.board;
            h.mode = bgMode;
            h.phase = 1;
            h.teamScore[0] = b.board.TeamScore(Fusion::Match::kAlliance);
            h.teamScore[1] = b.board.TeamScore(Fusion::Match::kHorde);
        }
        else if (d.inArena)
        {
            board = &m_arena.Scores();
            h.mode = Fusion::Match::Mode::TeamDeathmatch;
            h.phase = uint8(m_arena.CurrentPhase());
            h.limit = m_arena.Rules().scoreLimit;
            h.msLeft = m_arena.MsLeft(now);
            h.teamScore[0] = m_arena.TeamKills(Fusion::Match::kAlliance);
            h.teamScore[1] = m_arena.TeamKills(Fusion::Match::kHorde);
        }
        if (board)
            if (Fusion::Match::Row const* r = board->Find(guid))
            {
                h.score = r->score;
                h.kills = r->kills;
                h.deaths = r->deaths;
            }
    }
    h.team = TeamOf(player);
    d.nextMatchHudMs = now + kMatchHudEveryMs;
    // Out of a match: one empty HUD to put it away, then nothing.
    if (h.mode == Fusion::Match::Mode::None && !d.matchHudShown)
        return;
    d.matchHudShown = h.mode != Fusion::Match::Mode::None;
    SendChannel(player, Fusion::Prestige::kChannelPrestige, Fusion::Match::EncodeHud(h));
}

void FusionMgr::SendScoreboard(Player* player)
{
    FusionPlayerData const& d = player->GetFusionConst();
    std::vector<Fusion::Match::Row> rows;
    Fusion::Match::Mode mode = Fusion::Match::Mode::None;
    {
        std::lock_guard<std::mutex> lock(m_matchLock);
        if (player->InBattleGround() && player->GetBattleGround())
        {
            auto it = m_bgBoards.find(player->GetBattleGround()->GetInstanceID());
            if (it != m_bgBoards.end())
            {
                mode = it->second.mode;
                rows = it->second.board.Sorted();
            }
        }
        else if (d.inArena)
        {
            mode = Fusion::Match::Mode::TeamDeathmatch;
            rows = m_arena.Scores().Sorted();
        }
    }
    SendChannel(player, Fusion::Prestige::kChannelPrestige, Fusion::Match::EncodeScoreboard(mode, rows));
}

// ── Admin tools ─────────────────────────────────────────────────────────────────────────────

std::string FusionMgr::PrestigeReport(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    Fusion::Prestige::Standing const s = StandingOf(player);
    std::ostringstream o;
    o << "Prestige: level " << uint32(d.prestige.level) << ", guns " << uint32(d.prestige.guns);
    for (auto const& kv : d.prestige.professions)
        o << ", " << Fusion::Prestige::ProfessionName(kv.first) << " " << uint32(kv.second);
    o << "; " << d.prestige.tokens << " tokens; milestones 0x" << std::hex << d.prestige.milestones << std::dec
      << "; title " << d.prestige.title << " (" << Fusion::Prestige::TitleText(d.prestige.title) << ")"
      << "; guns maxed " << (s.gunsMaxed ? "yes" : "no") << "; all maxed " << (Fusion::Prestige::AllMaxed(s) ? "yes" : "no")
      << "; field recipes " << d.tradeRecipes.size() << "; reserve +" << uint32(d.reserveBonus * 100.f)
      << "%, ghillie x" << d.ghillie;
    return o.str();
}

void FusionMgr::ForcePrestigeReady(Player* player, Track track, uint16 key)
{
    FusionPlayerData& d = player->GetFusion();
    switch (track)
    {
        case Track::Level:
            if (player->GetLevel() < Fusion::Prestige::kMaxLevel)
            {
                player->GiveLevel(Fusion::Prestige::kMaxLevel);
                player->InitTalentForLevel();
                player->SetUInt32Value(PLAYER_XP, 0);
            }
            break;
        case Track::Guns:
        {
            // Every Weapons and Attachments achievement earned, as play would.
            uint32 const account = player->GetSession()->GetAccountId();
            for (Fusion::Achievement const& a : m_book.All())
            {
                if (a.category != Fusion::AchievementCategory::Weapons && a.category != Fusion::AchievementCategory::Attachments)
                    continue;
                if (d.earned.insert(a.id).second)
                    CharacterDatabase.PExecute(
                        "INSERT IGNORE INTO `account_fusion_achievement` (`account`, `achievement`, `earned_at`, `character`) "
                        "VALUES (%u, %u, %u, %u)", account, a.id, uint32(time(nullptr)), player->GetGUIDLow());
            }
            d.unlocks = UnlocksOf(player);
            SendUnlocks(player);
            break;
        }
        case Track::Profession:
            if (player->HasSkill(key))
                player->SetSkill(key, Fusion::Prestige::kMaxSkill,
                    std::max<uint16>(Fusion::Prestige::kMaxSkill, player->GetSkillMaxPure(key)));
            break;
    }
    d.gunsMaxedFor = size_t(-1);
    SendPrestigeState(player);
}

void FusionMgr::GivePrestigeTokens(Player* player, uint32 tokens)
{
    player->GetFusion().prestige.tokens += tokens;
    SavePrestige(player);
    SendPrestigeState(player);
}

std::string FusionMgr::ArenaReport()
{
    std::lock_guard<std::mutex> lock(m_matchLock);
    uint32 const now = WorldTimer::getMSTime();
    std::ostringstream o;
    o << "Arena TDM: phase " << uint32(m_arena.CurrentPhase()) << ", Alliance " << m_arena.TeamKills(Fusion::Match::kAlliance)
      << " - Horde " << m_arena.TeamKills(Fusion::Match::kHorde) << " (to " << m_arena.Rules().scoreLimit << "), "
      << m_arena.MsLeft(now) / 1000 << " s left, " << m_arena.Scores().Size() << " players; match " << m_arenaMatchNo
      << "; " << m_bgBoards.size() << " battleground boards.";
    return o.str();
}

void FusionMgr::EndArenaMatch()
{
    std::lock_guard<std::mutex> lock(m_matchLock);
    // Its clock run out now: the next player update ends it as time would.
    m_arena = Fusion::Match::Deathmatch(m_arena.Rules());
    ++m_arenaMatchNo;
    m_arenaEndText = "A game master ended the Team Deathmatch.";
}

// .fusion prestige [ready level|guns|<skill line> | tokens <n> | clear | tdm [end]]: an admin's
// prestige tools for testing; with no argument, the character's prestige.
bool ChatHandler::HandleFusionPrestigeCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !player->HasFusion())
        return false;
    std::string const arg = args ? std::string(args) : std::string();
    if (arg.empty())
    {
        SendSysMessage(sFusionMgr.PrestigeReport(player).c_str());
        return true;
    }
    if (!sFusionMgr.IsAdmin(player))
    {
        SendSysMessage("Fusion: only an admin may change prestige.");
        SetSentErrorMessage(true);
        return false;
    }
    if (arg.rfind("ready ", 0) == 0)
    {
        std::string const what = arg.substr(6);
        if (what == "level")
            sFusionMgr.ForcePrestigeReady(player, Track::Level, 0);
        else if (what == "guns")
            sFusionMgr.ForcePrestigeReady(player, Track::Guns, 0);
        else
        {
            uint16 const line = uint16(std::strtoul(what.c_str(), nullptr, 10));
            if (!Fusion::Prestige::IsProfession(line) || !player->HasSkill(line))
            {
                PSendSysMessage("Fusion: %s is not a profession you know (a skill line: 202 Engineering, 171 Alchemy...).", what.c_str());
                return false;
            }
            sFusionMgr.ForcePrestigeReady(player, Track::Profession, line);
        }
        PSendSysMessage("Fusion: %s is ready to prestige (achievements window, Prestige tab).", what.c_str());
        return true;
    }
    if (arg.rfind("tokens ", 0) == 0)
    {
        uint32 const n = uint32(std::strtoul(arg.c_str() + 7, nullptr, 10));
        sFusionMgr.GivePrestigeTokens(player, n);
        PSendSysMessage("Fusion: +%u prestige tokens.", n);
        return true;
    }
    if (arg == "clear")
    {
        FusionPlayerData& d = player->GetFusion();
        d.prestige = Fusion::Prestige::State();
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_prestige` WHERE `guid` = %u", player->GetGUIDLow());
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_prestige_skill` WHERE `guid` = %u", player->GetGUIDLow());
        CharacterDatabase.PExecute("DELETE FROM `account_fusion_prestige` WHERE `account` = %u", session->GetAccountId());
        d.unlocks = sFusionMgr.UnlocksOf(player);
        sFusionMgr.SendUnlocks(player);
        SendSysMessage("Fusion: this character's prestige (and its account's gun rank) is cleared.");
        return true;
    }
    if (arg == "tdm")
    {
        SendSysMessage(sFusionMgr.ArenaReport().c_str());
        return true;
    }
    if (arg == "tdm end")
    {
        sFusionMgr.EndArenaMatch();
        SendSysMessage("Fusion: the arena's Team Deathmatch is reset.");
        return true;
    }
    SendSysMessage("Fusion: .fusion prestige [ready level|guns|<skill line> | tokens <n> | clear | tdm [end]]");
    return true;
}
