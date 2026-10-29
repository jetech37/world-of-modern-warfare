/*
 * World of Modern Warfare: Inscription and Jewelcrafting in the game (FusionCrafts.h,
 * fusion/Crafts.h).
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionCrafts.h"
#include "FusionMgr.h"

#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Item.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <sstream>

namespace
{
    namespace C = Fusion::Crafts;
    using C::Trade;
    using C::Tier;

    constexpr float kTrainerReach = 12.f;
    constexpr uint8 kChannel = Fusion::Construction::kChannelProfessions;

    FusionCraftsData& D(Player* p) { return p->GetFusion().crafts; }
    FusionCraftsData const& D(Player const* p) { return p->GetFusionConst().crafts; }

    std::string Money(uint32 copper) { return Fusion::Homestead::MoneyText(copper); }

    Fusion::Homestead::Hue HueOf(Fusion::Construction::Color c)
    {
        using Fusion::Construction::Color;
        using Fusion::Homestead::Hue;
        switch (c)
        {
            case Color::Orange: return Hue::Orange;
            case Color::Yellow: return Hue::Yellow;
            case Color::Green: return Hue::Green;
            case Color::Gray: return Hue::Gray;
            default: return Hue::Red;
        }
    }

    std::string ItemName(uint32 entry)
    {
        ItemPrototype const* p = sObjectMgr.GetItemPrototype(entry);
        return p ? p->Name1 : "?";
    }

    Fusion::Construction::Inventory BagsFor(Player* player, C::Recipe const& r)
    {
        Fusion::Construction::Inventory bags;
        for (Fusion::Construction::Material const& m : r.reagents)
            bags[m.item] = player->GetItemCount(m.item);
        if (r.tool)
            bags[r.tool] = player->GetItemCount(r.tool);
        return bags;
    }

    bool ValidTrade(Trade t) { return t == Trade::Inscription || t == Trade::Jewelcrafting; }

    char const* GlyphRefusal(C::GlyphCheck c)
    {
        switch (c)
        {
            case C::GlyphCheck::NotAGlyph: return "That is not a glyph.";
            case C::GlyphCheck::WrongKind: return "That glyph goes in the other kind of slot.";
            case C::GlyphCheck::SlotLocked: return "That glyph slot opens at a higher level.";
            case C::GlyphCheck::WrongClass: return "That glyph is for another class.";
            case C::GlyphCheck::AlreadyInKit: return "This kit holds that glyph already.";
            case C::GlyphCheck::BadSlot: return "There is no such glyph slot.";
            default: return "";
        }
    }

    char const* GemRefusal(C::GemCheck c)
    {
        switch (c)
        {
            case C::GemCheck::NotAGem: return "That is not a cut gem.";
            case C::GemCheck::NoGun: return "The kit has no gun there.";
            case C::GemCheck::NoSocket: return "That gun has no such socket (the third asks Jewelcrafting 275 of your own).";
            case C::GemCheck::MetaOnly: return "The meta socket takes a meta gem.";
            case C::GemCheck::NotMeta: return "A meta gem goes in the primary's meta socket.";
            case C::GemCheck::SameKind: return "That gun has a gem tuning the same thing already.";
            case C::GemCheck::NeedsScope: return "A sway gem asks a scope (a sniper, the ACOG or the Thermal).";
            default: return "";
        }
    }
}

CraftsMgr& CraftsMgr::Instance()
{
    static CraftsMgr mgr;
    return mgr;
}

void CraftsMgr::Load()
{
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_skill` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`skill` smallint(5) unsigned NOT NULL COMMENT '9001 Construction, 9002 Minstrelsy, 9003 Inscription, 9004 Jewelcrafting',"
        "`tier` tinyint(3) unsigned NOT NULL DEFAULT '0',"
        "`value` smallint(5) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`guid`, `skill`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_craft_recipe` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`trade` tinyint(3) unsigned NOT NULL COMMENT '1 Inscription, 2 Jewelcrafting',"
        "`recipe` smallint(5) unsigned NOT NULL COMMENT 'Crafts::Recipes id',"
        "PRIMARY KEY (`guid`, `trade`, `recipe`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_kit_glyph` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`kit` tinyint(3) unsigned NOT NULL COMMENT 'the kit slot, 0-4',"
        "`kind` tinyint(3) unsigned NOT NULL COMMENT '0 major, 1 minor',"
        "`slot` tinyint(3) unsigned NOT NULL COMMENT '0-2',"
        "`item` int(10) unsigned NOT NULL COMMENT 'the glyph item',"
        "PRIMARY KEY (`guid`, `kit`, `kind`, `slot`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_kit_gem` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`kit` tinyint(3) unsigned NOT NULL COMMENT 'the kit slot, 0-4',"
        "`gun` tinyint(3) unsigned NOT NULL COMMENT '0 primary, 1 secondary',"
        "`socket` tinyint(3) unsigned NOT NULL COMMENT '0-2 plain, 3 meta',"
        "`item` int(10) unsigned NOT NULL COMMENT 'the cut gem item',"
        "PRIMARY KEY (`guid`, `kit`, `gun`, `socket`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: Inscription and Jewelcrafting: %u recipes, %u glyphs, %u gems.",
        uint32(C::Recipes().size()), uint32(C::Glyphs().size()), uint32(C::Gems().size()));
}

void CraftsMgr::LoadPlayer(Player* player)
{
    FusionCraftsData& d = D(player);
    d = FusionCraftsData{};
    d.loaded = true;
    uint32 const guid = player->GetGUIDLow();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `skill`, `tier`, `value` FROM `character_fusion_skill` WHERE `guid` = %u AND `skill` IN (%u, %u)",
            guid, uint32(C::kSkillInscription), uint32(C::kSkillJewelcrafting)))
    {
        do
        {
            Field* f = r->Fetch();
            Trade const t = C::TradeOfSkill(f[0].GetUInt16());
            uint8 const tier = f[1].GetUInt8();
            if (!ValidTrade(t) || tier > uint8(Tier::Artisan))
                continue;
            d.Of(t).tier = Tier(tier);
            d.Of(t).skill = std::min<uint16>(f[2].GetUInt16(), C::SkillCap(Tier(tier)));
        } while (r->NextRow());
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `trade`, `recipe` FROM `character_fusion_craft_recipe` WHERE `guid` = %u", guid))
    {
        do
        {
            Field* f = r->Fetch();
            Trade const t = Trade(f[0].GetUInt8());
            if (ValidTrade(t))
                d.Of(t).recipes.push_back(f[1].GetUInt16());
        } while (r->NextRow());
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `kit`, `kind`, `slot`, `item` FROM `character_fusion_kit_glyph` WHERE `guid` = %u", guid))
    {
        do
        {
            Field* f = r->Fetch();
            uint8 const kit = f[0].GetUInt8(), kind = f[1].GetUInt8(), slot = f[2].GetUInt8();
            if (kit >= Fusion::kClassSlots || kind > 1 || slot >= C::kGlyphSlots)
                continue;
            (kind == 0 ? d.glyphs[kit].major : d.glyphs[kit].minor)[slot] = f[3].GetUInt32();
        } while (r->NextRow());
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `kit`, `gun`, `socket`, `item` FROM `character_fusion_kit_gem` WHERE `guid` = %u", guid))
    {
        do
        {
            Field* f = r->Fetch();
            uint8 const kit = f[0].GetUInt8(), gun = f[1].GetUInt8(), socket = f[2].GetUInt8();
            if (kit >= Fusion::kClassSlots || gun > 1 || socket >= C::kSockets)
                continue;
            d.gems[kit].Of(C::GunSlot(gun))[socket] = f[3].GetUInt32();
        } while (r->NextRow());
    }
}

void CraftsMgr::TakeSlots(Player* player)
{
    FusionCraftsData& d = D(player);
    if (d.slotsTaken)
        return;
    d.slotsTaken = true;
    // Vanilla counts its own professions' slots as their spells load; the layer's two take theirs.
    uint32 known = 0;
    for (FusionCraftTrade const& t : d.trades)
        known += t.tier != Tier::None ? 1 : 0;
    uint32 const free = player->GetFreePrimaryProfessionPoints();
    player->SetFreePrimaryProfessions(uint16(free > known ? free - known : 0));
}

void CraftsMgr::UpdatePlayer(Player* player, uint32 /*diffMs*/)
{
    if (!sFusionMgr.IsEnabled() || !player->HasFusion() || !player->IsInWorld())
        return;
    FusionCraftsData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    TakeSlots(player);
    if (!d.sent)
    {
        d.sent = true;
        for (Trade t : {Trade::Inscription, Trade::Jewelcrafting})
        {
            SendSkill(player, t);
            SendBook(player, t);
            if (d.Of(t).tier != Tier::None)
                sFusionMgr.RecordStat(player, {Fusion::Stat::ProfessionSkill, C::SkillKey(t)}, d.Of(t).skill);
        }
    }
    // A gun drawn: its gems at once, not at the next half second. Another kit carried: the
    // client's Glyphs & Gems window follows it.
    FusionPlayerData& f = player->GetFusion();
    uint8 const kit = f.Equipped() ? f.equippedSlot : uint8(255);
    if (kit != d.lastKit)
    {
        d.lastKit = kit;
        f.nextCombatMs = WorldTimer::getMSTime();
        SendAugments(player, false);
    }
    if (f.heldWeaponId != d.lastHeld)
    {
        d.lastHeld = f.heldWeaponId;
        f.nextCombatMs = WorldTimer::getMSTime();
    }
}

uint16 CraftsMgr::SkillOf(Player const* player, Trade t) const
{
    if (!player->HasFusion() || !ValidTrade(t))
        return 0;
    FusionCraftTrade const& s = D(player).Of(t);
    return s.tier == Tier::None ? 0 : s.skill;
}

void CraftsMgr::SetSkill(Player* player, Trade t, Tier tier, uint16 skill, bool announce)
{
    FusionCraftTrade& s = D(player).Of(t);
    skill = std::min<uint16>(skill, C::SkillCap(tier));
    bool const up = announce && skill > s.skill;
    s.tier = tier;
    s.skill = skill;
    CharacterDatabase.PExecute("REPLACE INTO `character_fusion_skill` (`guid`, `skill`, `tier`, `value`) VALUES (%u, %u, %u, %u)",
        player->GetGUIDLow(), uint32(C::SkillKey(t)), uint32(tier), uint32(skill));
    SendSkill(player, t);
    sFusionMgr.RecordStat(player, {Fusion::Stat::ProfessionSkill, C::SkillKey(t)}, skill);
    if (up)
        ChatHandler(player).PSendSysMessage("Your skill in %s has increased to %u.", C::TradeName(t), uint32(skill));
}

void CraftsMgr::SendSkill(Player* player, Trade t)
{
    FusionCraftTrade const& s = D(player).Of(t);
    sFusionMgr.SendChannel(player, kChannel, C::EncodeSkill(t, s.tier, s.skill));
}

void CraftsMgr::SendBook(Player* player, Trade t)
{
    sFusionMgr.SendChannel(player, kChannel, C::EncodeBook(t, C::Book(t, D(player).Of(t).recipes)));
}

bool CraftsMgr::GossipHello(Player* player, Creature* creature)
{
    if (!sFusionMgr.IsEnabled() || !player->HasFusion())
        return false;
    Trade const t = C::TradeOfTrainer(creature->GetEntry());
    if (!ValidTrade(t))
        return false;
    FusionCraftsData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    d.trainer = creature->GetObjectGuid();
    d.trainerTrade = t;
    player->PlayerTalkClass->ClearMenus();
    player->PlayerTalkClass->CloseGossip();
    SendWindow(player, t, true);
    return true;
}

Creature* CraftsMgr::TrainerNear(Player* player, Trade t) const
{
    FusionCraftsData const& d = D(player);
    if (d.trainer.IsEmpty() || d.trainerTrade != t || !player->GetMap())
        return nullptr;
    Creature* c = player->GetMap()->GetCreature(d.trainer);
    if (!c || !c->IsAlive() || C::TradeOfTrainer(c->GetEntry()) != t || !player->IsWithinDistInMap(c, kTrainerReach))
        return nullptr;
    return c;
}

void CraftsMgr::SendWindow(Player* player, Trade t, bool open, Said const& said)
{
    FusionCraftsData const& d = D(player);
    FusionCraftTrade const& s = d.Of(t);
    bool const gm = sFusionMgr.IsAdmin(player);
    C::Window w;
    w.trade = t;
    w.open = open;
    w.tier = s.tier;
    w.skill = s.skill;
    if (!said.text.empty())
    {
        w.tone = uint8(said.ok ? Fusion::Homestead::Tone::Done : Fusion::Homestead::Tone::Error);
        w.message = said.text;
    }
    w.trainer = TrainerNear(player, t) != nullptr;
    std::string const name = C::TradeName(t);
    if (w.trainer)
    {
        Tier const next = Fusion::Construction::Next(s.tier);
        if (next != Tier::None)
        {
            C::TierDef const& td = C::TierOf(next);
            C::Service sv;
            sv.action = C::Action::Train;
            sv.cost = td.costCopper;
            sv.label = std::string("Train ") + td.name + " " + name;
            std::ostringstream note;
            note << "Skill up to " << td.cap << "  -  level " << uint32(td.levelToLearn);
            if (td.skillToLearn)
                note << ", " << name << " " << td.skillToLearn;
            if (s.tier == Tier::None)
                note << "; takes a primary profession slot";
            sv.note = note.str();
            switch (C::CanTrainTier(s.tier, next, s.skill, uint8(player->GetLevel()), player->GetMoney(),
                player->GetFreePrimaryProfessionPoints(), gm))
            {
                case C::TrainCheck::Ok: break;
                case C::TrainCheck::NoFreeSlot: sv.enabled = false; sv.note = "You know two primary professions already"; break;
                case C::TrainCheck::SkillTooLow: sv.enabled = false; sv.note = "Requires " + name + " " + std::to_string(td.skillToLearn); break;
                case C::TrainCheck::LevelTooLow: sv.enabled = false; sv.note = "Requires level " + std::to_string(td.levelToLearn); break;
                case C::TrainCheck::NotEnoughMoney: sv.enabled = false; sv.note = "You need " + Money(td.costCopper); break;
                default: sv.enabled = false; break;
            }
            w.services.push_back(std::move(sv));
        }
        for (C::Ware const& ware : C::Wares(t))
        {
            C::Service sv;
            sv.action = C::Action::Buy;
            sv.arg = ware.item;
            sv.cost = ware.price;
            sv.label = "Buy: " + ItemName(ware.item);
            sv.note = "Goods of the trade";
            if (!gm && player->GetMoney() < ware.price)
            {
                sv.enabled = false;
                sv.note = "You need " + Money(ware.price);
            }
            w.services.push_back(std::move(sv));
        }
        // The recipes to learn, the nearest first, up to the window's room.
        std::vector<C::Recipe const*> learnable;
        for (C::Recipe const* r : C::RecipesOf(t))
            if (!C::Known(*r, s.recipes))
                learnable.push_back(r);
        std::stable_sort(learnable.begin(), learnable.end(),
            [](C::Recipe const* a, C::Recipe const* b) { return a->required < b->required; });
        size_t const room = C::kMaxServices - 1 - w.services.size();
        for (C::Recipe const* r : learnable)
        {
            if (w.services.size() >= room)
                break;
            C::Service sv;
            sv.action = C::Action::Learn;
            sv.arg = r->id;
            sv.cost = r->learnCost;
            sv.label = std::string("Learn: ") + r->name;
            sv.note = name + " " + std::to_string(r->required);
            sv.hue = uint8(s.tier == Tier::None || s.skill < r->required ? Fusion::Homestead::Hue::Red
                : HueOf(Fusion::Construction::ColorOf(s.skill, C::SkillOf(*r))));
            switch (C::CanLearn(*r, t, s.tier, s.skill, false, player->GetMoney(), gm))
            {
                case C::LearnCheck::Ok: break;
                case C::LearnCheck::NotLearned: sv.enabled = false; sv.note = "Learn " + name + " first"; break;
                case C::LearnCheck::NotEnoughMoney: sv.enabled = false; sv.note = "You need " + Money(r->learnCost); break;
                default: sv.enabled = false; sv.note = "Requires " + name + " " + std::to_string(r->required); break;
            }
            w.services.push_back(std::move(sv));
        }
        if (s.tier != Tier::None)
        {
            C::Service sv;
            sv.action = C::Action::Unlearn;
            sv.label = "Unlearn " + name;
            sv.note = "Forget the skill and its recipes; the profession slot comes back";
            w.services.push_back(std::move(sv));
        }
    }
    sFusionMgr.SendChannel(player, kChannel, C::EncodeWindow(w));
    SendBook(player, t);
}

CraftsMgr::Said CraftsMgr::Act(Player* player, Trade t, C::Action a, uint32 arg)
{
    FusionCraftsData& d = D(player);
    FusionCraftTrade& s = d.Of(t);
    bool const gm = sFusionMgr.IsAdmin(player);
    std::string const name = C::TradeName(t);
    if (a == C::Action::Make)
    {
        uint16 const id = C::MakeRecipeOf(arg);
        uint16 const count = C::MakeCountOf(arg);
        Said said;
        uint16 made = 0;
        for (uint16 i = 0; i < count; ++i)
        {
            Said const one = Make(player, t, id);
            if (!one.ok)
            {
                if (!made)
                    said = one;
                break;
            }
            said = one;
            ++made;
        }
        if (made > 1)
            if (C::Recipe const* r = C::RecipeOf(id))
                said.text = std::string(r->name) + ": done " + std::to_string(made) + " times.";
        return said;
    }
    if (!TrainerNear(player, t))
        return {false, "Speak with " + std::string(t == Trade::Inscription ? "an Inscription" : "a Jewelcrafting")
            + " Trainer for that."};
    if (a == C::Action::Train)
    {
        Tier const have = s.tier;
        Tier const want = Fusion::Construction::Next(have);
        C::TierDef const& td = C::TierOf(want);
        switch (C::CanTrainTier(have, want, s.skill, uint8(player->GetLevel()), player->GetMoney(),
            player->GetFreePrimaryProfessionPoints(), gm))
        {
            case C::TrainCheck::Ok: break;
            case C::TrainCheck::NoFreeSlot: return {false, "You already know two primary professions."};
            case C::TrainCheck::SkillTooLow: return {false, "You need " + std::to_string(td.skillToLearn) + " " + name + " for that."};
            case C::TrainCheck::LevelTooLow: return {false, "You must be level " + std::to_string(td.levelToLearn) + "."};
            case C::TrainCheck::NotEnoughMoney: return {false, std::string(td.name) + " " + name + " costs " + Money(td.costCopper) + "."};
            default: return {false, "There is nothing more I can teach you."};
        }
        if (!gm)
            player->ModifyMoney(-int32(td.costCopper));
        if (have == Tier::None)
        {
            uint32 const free = player->GetFreePrimaryProfessionPoints();
            player->SetFreePrimaryProfessions(uint16(free ? free - 1 : 0));
        }
        SetSkill(player, t, want, have == Tier::None ? Fusion::Construction::kStartSkill : s.skill, false);
        SendBook(player, t);
        return {true, std::string("You have learned ") + td.name + " " + name + ": your skill may now reach "
            + std::to_string(td.cap) + "."};
    }
    if (a == C::Action::Learn)
    {
        C::Recipe const* r = C::RecipeOf(uint16(arg));
        if (!r || r->trade != t)
            return {false, "There is no such recipe."};
        bool const known = C::Known(*r, s.recipes);
        switch (C::CanLearn(*r, t, s.tier, s.skill, known, player->GetMoney(), gm))
        {
            case C::LearnCheck::Ok: break;
            case C::LearnCheck::Known: return {false, "You know that already."};
            case C::LearnCheck::NotLearned: return {false, "Learn " + name + " first."};
            case C::LearnCheck::NotEnoughMoney: return {false, "That costs " + Money(r->learnCost) + "."};
            default: return {false, "You need " + std::to_string(r->required) + " " + name + " for that."};
        }
        if (!gm)
            player->ModifyMoney(-int32(r->learnCost));
        s.recipes.push_back(r->id);
        CharacterDatabase.PExecute("REPLACE INTO `character_fusion_craft_recipe` (`guid`, `trade`, `recipe`) VALUES (%u, %u, %u)",
            player->GetGUIDLow(), uint32(t), uint32(r->id));
        return {true, std::string("You can now make ") + r->name + "."};
    }
    if (a == C::Action::Buy)
    {
        C::Ware const* ware = nullptr;
        for (C::Ware const& w : C::Wares(t))
            if (w.item == arg)
                ware = &w;
        if (!ware || !sObjectMgr.GetItemPrototype(ware->item))
            return {false, "I don't sell that."};
        if (!gm && player->GetMoney() < ware->price)
            return {false, ItemName(ware->item) + " costs " + Money(ware->price) + "."};
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, ware->item, ware->count) != EQUIP_ERR_OK)
            return {false, "Your bags are full."};
        if (!gm)
            player->ModifyMoney(-int32(ware->price));
        if (Item* item = player->StoreNewItem(dest, ware->item, true))
            player->SendNewItem(item, ware->count, true, false);
        return {true, "You buy " + ItemName(ware->item) + "."};
    }
    if (a == C::Action::Unlearn)
    {
        if (s.tier == Tier::None)
            return {false, "You don't know " + name + "."};
        s = FusionCraftTrade{};
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_skill` WHERE `guid` = %u AND `skill` = %u",
            player->GetGUIDLow(), uint32(C::SkillKey(t)));
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_craft_recipe` WHERE `guid` = %u AND `trade` = %u",
            player->GetGUIDLow(), uint32(t));
        player->SetFreePrimaryProfessions(uint16(player->GetFreePrimaryProfessionPoints() + 1));
        SendSkill(player, t);
        SendBook(player, t);
        return {true, "You have unlearned " + name + "."};
    }
    return {false, ""};
}

CraftsMgr::Said CraftsMgr::Make(Player* player, Trade t, uint16 id)
{
    C::Recipe const* r = C::RecipeOf(id);
    if (!r || r->trade != t)
        return {false, "There is no such recipe."};
    FusionCraftTrade const& s = D(player).Of(t);
    bool const gm = sFusionMgr.IsAdmin(player);
    std::string const name = C::TradeName(t);
    switch (C::CanMake(*r, s.tier, C::Known(*r, s.recipes), s.skill, BagsFor(player, *r), gm))
    {
        case C::MakeCheck::Ok: break;
        case C::MakeCheck::NotLearned: return {false, "Learn " + name + " from its trainer first."};
        case C::MakeCheck::Unknown: return {false, std::string("Learn ") + r->name + " from a trainer first."};
        case C::MakeCheck::SkillTooLow: return {false, "You need " + std::to_string(r->required) + " " + name + " for that."};
        case C::MakeCheck::NoTool: return {false, "You need a " + ItemName(r->tool) + " in your bags."};
        default: return {false, std::string("You lack the reagents for ") + r->name + "."};
    }
    // What comes out: the product, or the draws of milling and prospecting.
    std::vector<C::Draw> out;
    if (r->kind == C::Kind::Make)
        out.push_back({r->product, r->productCount});
    else
    {
        std::vector<uint8_t> rolls(C::RollBytes(*r));
        for (uint8_t& b : rolls)
            b = uint8_t(urand(0, 255));
        out = C::Roll(*r, rolls);
    }
    std::vector<ItemPosCountVec> dests(out.size());
    for (size_t i = 0; i < out.size(); ++i)
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dests[i], out[i].item, out[i].count) != EQUIP_ERR_OK)
            return {false, "Your bags are full."};
    if (!gm)
        for (Fusion::Construction::Material const& m : r->reagents)
            player->DestroyItemCount(m.item, m.count, true);
    std::string got;
    for (size_t i = 0; i < out.size(); ++i)
    {
        // The bags may have shifted with the reagents gone: place each afresh.
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, out[i].item, out[i].count) != EQUIP_ERR_OK)
            continue;
        if (Item* item = player->StoreNewItem(dest, out[i].item, true))
            player->SendNewItem(item, out[i].count, false, true);
        got += (got.empty() ? "" : ", ") + std::to_string(out[i].count) + " " + ItemName(out[i].item);
    }
    // A point, by vanilla's colours (a game master's free goods teach nothing).
    if (!gm && s.tier != Tier::None)
        if (uint16 const next = C::MakeSkillUp(*r, s.skill, C::SkillCap(s.tier), rand_norm_f()); next != s.skill)
            SetSkill(player, t, s.tier, next, true);
    sFusionMgr.CountStat(player, {Fusion::Stat::ItemsCrafted, Fusion::kAnyKey});
    sFusionMgr.CountStat(player, {Fusion::Stat::ItemsCrafted, C::SkillKey(t)});
    if (r->kind == C::Kind::Mill)
        sFusionMgr.CountStat(player, {Fusion::Stat::HerbsMilled, Fusion::kAnyKey});
    else if (r->kind == C::Kind::Prospect)
        sFusionMgr.CountStat(player, {Fusion::Stat::OreProspected, Fusion::kAnyKey});
    return {true, std::string(r->name) + ": " + got + "."};
}

bool CraftsMgr::HandleChannel(Player* player, std::vector<uint8> const& body)
{
    if (body.empty() || body[0] < C::kOpAsk || body[0] > C::kOpAugments)
        return false;
    if (!sFusionMgr.IsEnabled() || !player->HasFusion() || !player->IsInWorld())
        return true;
    FusionCraftsData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    switch (body[0])
    {
        case C::kOpAsk:
        {
            if (body.size() == 2 && ValidTrade(Trade(body[1])))
            {
                Trade const t = Trade(body[1]);
                SendSkill(player, t);
                SendBook(player, t);
            }
            return true;
        }
        case C::kOpAct:
        {
            Trade t;
            C::Action a;
            uint32 arg;
            if (!C::DecodeAct(body, t, a, arg))
                return true;
            Said const said = Act(player, t, a, arg);
            // The trainer's window answers in its status line; a Make from the trade window in
            // the chat as WoW's own trades do.
            if (a == C::Action::Make)
            {
                if (!said.text.empty())
                    ChatHandler(player).SendSysMessage(((said.ok ? "" : "|cffff2020") + said.text).c_str());
                SendBook(player, t);
            }
            else
                SendWindow(player, t, false, said);
            return true;
        }
        case C::kOpAskAugments:
            SendAugments(player, true);
            return true;
        case C::kOpAugment:
        {
            C::AugmentAsk ask;
            if (C::DecodeAugment(body, ask))
                SendAugments(player, false, Augment(player, ask));
            return true;
        }
        default:
            return true;
    }
}

// ── The kits' augments ────────────────────────────────────────────────────────────────────────

C::GunInfo CraftsMgr::GunOf(Player const* player, C::GunSlot g) const
{
    C::GunInfo info;
    Fusion::ClassLoadout const* kit = player->GetFusionConst().Equipped();
    if (!kit)
        return info;
    uint16 const id = g == C::GunSlot::Primary ? kit->primary : kit->secondary;
    Fusion::WeaponProfile const* w = id ? sFusionMgr.Catalog().Weapons().Find(id) : nullptr;
    if (!w)
        return info;
    info.present = true;
    info.weaponClass = w->weaponClass;
    info.scoped = C::Scoped(w->weaponClass,
        g == C::GunSlot::Primary ? Fusion::PrimaryAttachments(*kit) : Fusion::SecondaryAttachments(*kit));
    return info;
}

void CraftsMgr::ApplyHandling(Player* player, Fusion::Stats::Derived& combat)
{
    if (!player->HasFusion())
        return;
    FusionPlayerData& f = player->GetFusion();
    FusionCraftsData const& d = f.crafts;
    C::Mods mods;
    Fusion::ClassLoadout const* kit = f.Equipped();
    if (d.loaded && kit && f.equippedSlot < Fusion::kClassSlots)
    {
        mods = C::GlyphMods(d.glyphs[f.equippedSlot], uint8(player->GetLevel()), player->GetClass());
        // The gun in hand's gems: only the kit's own guns (not one picked up, nor a streak's).
        if (f.heldWeaponId && !f.pickups.Holding(f.heldWeaponId))
        {
            uint16 const jc = SkillOf(player, Trade::Jewelcrafting);
            if (f.heldWeaponId == kit->primary)
                C::Combine(mods, C::GemMods(d.gems[f.equippedSlot], C::GunSlot::Primary, GunOf(player, C::GunSlot::Primary), jc));
            else if (f.heldWeaponId == kit->secondary)
                C::Combine(mods, C::GemMods(d.gems[f.equippedSlot], C::GunSlot::Secondary, GunOf(player, C::GunSlot::Secondary), jc));
        }
    }
    C::ApplyHandling(combat, mods);
    // The other effects for the perks' hooks; the handling is in `combat` already.
    mods.handling = {};
    if (mods != f.augments)
    {
        bool const speed = mods.outOfCombatSpeed != f.augments.outOfCombatSpeed;
        f.augments = mods;
        if (speed && player->IsInWorld())
        {
            player->UpdateSpeed(MOVE_RUN, false);
            player->UpdateSpeed(MOVE_RUN_BACK, false);
        }
    }
}

void CraftsMgr::SendAugments(Player* player, bool open, Said const& said)
{
    FusionPlayerData const& f = player->GetFusionConst();
    FusionCraftsData const& d = f.crafts;
    C::Augments a;
    a.open = open;
    a.level = uint8(player->GetLevel());
    a.jewelcrafting = SkillOf(player, Trade::Jewelcrafting);
    if (f.Equipped() && f.equippedSlot < Fusion::kClassSlots)
    {
        a.kit = f.equippedSlot;
        a.glyphs = d.glyphs[f.equippedSlot];
        a.gems = d.gems[f.equippedSlot];
        C::GunInfo const p = GunOf(player, C::GunSlot::Primary), s = GunOf(player, C::GunSlot::Secondary);
        a.flags = uint8((p.present ? C::kAugPrimary : 0) | (s.present ? C::kAugSecondary : 0)
            | (p.scoped ? C::kAugPrimaryScoped : 0) | (s.scoped ? C::kAugSecondaryScoped : 0)
            | (C::MetaAwake(a.gems.primary) ? C::kAugPrimaryMeta : 0) | (C::MetaAwake(a.gems.secondary) ? C::kAugSecondaryMeta : 0));
    }
    a.canEdit = Fusion::CanChangeKits(sFusionMgr.KitAccessNow(player));
    if (!said.text.empty())
    {
        a.tone = uint8(said.ok ? Fusion::Homestead::Tone::Done : Fusion::Homestead::Tone::Error);
        a.message = said.text;
    }
    sFusionMgr.SendChannel(player, kChannel, C::EncodeAugments(a));
}

CraftsMgr::Said CraftsMgr::Augment(Player* player, C::AugmentAsk const& ask)
{
    FusionPlayerData& f = player->GetFusion();
    FusionCraftsData& d = f.crafts;
    if (!f.Equipped() || f.equippedSlot >= Fusion::kClassSlots)
        return {false, "Carry a kit first."};
    if (!Fusion::CanChangeKits(sFusionMgr.KitAccessNow(player)))
        return {false, "Glyphs and gems go in where kits change: at a trainer or an innkeeper, or with the field editor."};
    uint8 const kit = f.equippedSlot;
    bool const gm = sFusionMgr.IsAdmin(player);
    if (ask.item && !gm && !player->HasItemCount(ask.item, 1))
        return {false, "You have no " + ItemName(ask.item) + "."};
    uint32 const guid = player->GetGUIDLow();
    if (ask.what == 0)
    {
        C::GlyphKind const kind = C::GlyphKind(ask.kind);
        uint32& slot = (kind == C::GlyphKind::Major ? d.glyphs[kit].major : d.glyphs[kit].minor)[ask.slot];
        if (!ask.item)
        {
            if (!slot)
                return {false, "That slot is empty."};
            slot = 0;
            CharacterDatabase.PExecute("DELETE FROM `character_fusion_kit_glyph` WHERE `guid` = %u AND `kit` = %u AND `kind` = %u AND `slot` = %u",
                guid, uint32(kit), uint32(ask.kind), uint32(ask.slot));
            f.nextCombatMs = WorldTimer::getMSTime();
            return {true, "The glyph is scraped away."};
        }
        C::GlyphCheck const c = C::CanInscribe(d.glyphs[kit], kind, ask.slot, ask.item, uint8(player->GetLevel()), player->GetClass());
        if (c != C::GlyphCheck::Ok)
            return {false, GlyphRefusal(c)};
        if (!gm)
            player->DestroyItemCount(ask.item, 1, true);
        slot = ask.item;
        CharacterDatabase.PExecute("REPLACE INTO `character_fusion_kit_glyph` (`guid`, `kit`, `kind`, `slot`, `item`) VALUES (%u, %u, %u, %u, %u)",
            guid, uint32(kit), uint32(ask.kind), uint32(ask.slot), ask.item);
        f.nextCombatMs = WorldTimer::getMSTime();
        sFusionMgr.CountStat(player, {Fusion::Stat::GlyphsInscribed, Fusion::kAnyKey});
        uint32 full = 0;
        for (C::KitGlyphs const& g : d.glyphs)
            full += std::all_of(g.major.begin(), g.major.end(), [](uint32 x) { return x != 0; })
                && std::all_of(g.minor.begin(), g.minor.end(), [](uint32 x) { return x != 0; }) ? 1 : 0;
        sFusionMgr.RecordStat(player, {Fusion::Stat::FullyGlyphedKits, Fusion::kAnyKey}, full);
        return {true, ItemName(ask.item) + " is inscribed into the kit."};
    }
    C::GunSlot const gun = C::GunSlot(ask.kind);
    uint32& socket = d.gems[kit].Of(gun)[ask.slot];
    if (!ask.item)
    {
        if (!socket)
            return {false, "That socket is empty."};
        socket = 0;
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_kit_gem` WHERE `guid` = %u AND `kit` = %u AND `gun` = %u AND `socket` = %u",
            guid, uint32(kit), uint32(ask.kind), uint32(ask.slot));
        f.nextCombatMs = WorldTimer::getMSTime();
        return {true, "The gem is pried out and shatters."};
    }
    C::GemCheck const c = C::CanSocket(d.gems[kit], gun, ask.slot, ask.item, GunOf(player, gun), SkillOf(player, Trade::Jewelcrafting));
    if (c != C::GemCheck::Ok)
        return {false, GemRefusal(c)};
    if (!gm)
        player->DestroyItemCount(ask.item, 1, true);
    socket = ask.item;
    CharacterDatabase.PExecute("REPLACE INTO `character_fusion_kit_gem` (`guid`, `kit`, `gun`, `socket`, `item`) VALUES (%u, %u, %u, %u, %u)",
        guid, uint32(kit), uint32(ask.kind), uint32(ask.slot), ask.item);
    f.nextCombatMs = WorldTimer::getMSTime();
    sFusionMgr.CountStat(player, {Fusion::Stat::GemsSocketed, Fusion::kAnyKey});
    return {true, ItemName(ask.item) + " is set into the " + (gun == C::GunSlot::Primary ? "primary." : "secondary.")};
}

// ── The game masters' tools ───────────────────────────────────────────────────────────────────

uint16 CraftsMgr::GmSkillOf(Player* player, Trade t)
{
    if (!player->HasFusion() || !ValidTrade(t))
        return 0;
    if (!D(player).loaded)
        LoadPlayer(player);
    return SkillOf(player, t);
}

void CraftsMgr::GmSetSkill(Player* player, Trade t, uint16 skill)
{
    if (!player->HasFusion() || !ValidTrade(t))
        return;
    if (!D(player).loaded)
        LoadPlayer(player);
    skill = std::max<uint16>(1, std::min<uint16>(skill, C::SkillCap(Tier::Artisan)));
    SetSkill(player, t, C::TierHolding(skill), skill, false);
    SendBook(player, t);
}

uint32 CraftsMgr::GmLearnAllRecipes(Player* player, Trade t)
{
    if (!player->HasFusion() || !ValidTrade(t))
        return 0;
    FusionCraftsData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    uint32 added = 0;
    for (C::Recipe const* r : C::RecipesOf(t))
        if (!C::Known(*r, d.Of(t).recipes))
        {
            d.Of(t).recipes.push_back(r->id);
            CharacterDatabase.PExecute("REPLACE INTO `character_fusion_craft_recipe` (`guid`, `trade`, `recipe`) VALUES (%u, %u, %u)",
                player->GetGUIDLow(), uint32(t), uint32(r->id));
            ++added;
        }
    SendBook(player, t);
    return added;
}
