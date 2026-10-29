/*
 * World of Modern Warfare: battle companions faint (fusion/CompanionRest.h). A companion beaten to
 * 0 health faints and stays fainted, on its own, until a stable master revives it (a small fee by
 * level, wounds healed too), a Reviving Draught does (at half health), or the long wait is over
 * (Companions.FaintRecoverMinutes, an hour by default; Loyal a third). Each companion item's
 * wounds and faint are kept by item guid in characters.character_fusion_companion_rest (made at
 * startup), so a logout, a restart or a trade changes nothing. See src/fusion/docs/COMPANIONS.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "fusion/CompanionIndex.h"
#include "fusion/CompanionRest.h"
#include "fusion/CompanionTraits.h"

#include "Bag.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "Timer.h"
#include "WorldSession.h"

#include <ctime>

namespace Companions = Fusion::Companions;
namespace Index = Fusion::Companions::Index;

namespace
{
    // The stable master's gossip options.
    constexpr uint32 kStableSender = 0x5354;    // "ST"
    constexpr uint32 kStableRevive = 1;
    constexpr uint32 kStableBuy = 2;
    constexpr uint32 kStableWindow = 3;

    uint32 UnixNow() { return uint32(time(nullptr)); }

    // Each item in the bags (not the bank).
    template <class F>
    void ForEachBagItem(Player* player, F f)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                f(item);
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            if (Bag* b = dynamic_cast<Bag*>(player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
                for (uint32 slot = 0; slot < b->GetBagSize(); ++slot)
                    if (Item* item = b->GetItemByPos(uint8(slot)))
                        f(item);
    }

    std::string Money(uint32 copper)
    {
        std::string o;
        if (copper >= 10000)
            o += std::to_string(copper / 10000) + "g";
        if (copper % 10000 >= 100)
            o += (o.empty() ? "" : " ") + std::to_string((copper % 10000) / 100) + "s";
        if (copper % 100 || copper == 0)
            o += (o.empty() ? "" : " ") + std::to_string(copper % 100) + "c";
        return o;
    }

    // "Wolf Pup", "Wolf Pup and Raptor", "Wolf Pup, Raptor and Kodo Calf".
    std::string NameList(std::vector<std::string> const& names)
    {
        std::string o;
        for (size_t i = 0; i < names.size(); ++i)
            o += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
        return o;
    }

    // A line in the chat window (the companion's other lines go to the UI error frame).
    void ChatLine(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player).SendSysMessage(text.c_str());
    }
}

// ── The records ─────────────────────────────────────────────────────────────────────────────

Companions::Recovery FusionMgr::CompanionRecoveryOf(uint32 itemGuidLow) const
{
    std::lock_guard<std::mutex> lock(m_companionLock);
    auto it = m_companionRecovery.find(itemGuidLow);
    return it != m_companionRecovery.end() ? it->second : Companions::Recovery{};
}

void FusionMgr::SaveCompanionRecovery(uint32 itemGuidLow, Companions::Recovery const& r)
{
    if (!itemGuidLow)
        return;
    bool const whole = Companions::IsWhole(r);
    {
        std::lock_guard<std::mutex> lock(m_companionLock);
        auto it = m_companionRecovery.find(itemGuidLow);
        if (whole)
        {
            // Whole and never hurt: nothing kept, nothing written.
            if (it == m_companionRecovery.end())
                return;
            m_companionRecovery.erase(it);
        }
        else
            m_companionRecovery[itemGuidLow] = r;
    }
    if (whole)
    {
        CharacterDatabase.PExecute("DELETE FROM `character_fusion_companion_rest` WHERE `item_guid` = %u", itemGuidLow);
        return;
    }
    Companions::RecoveryRow const row = Companions::ToRow(r);
    CharacterDatabase.PExecute(
        "REPLACE INTO `character_fusion_companion_rest` (`item_guid`, `health`, `away_at`, `fainted_at`) VALUES (%u, %u, %u, %u)",
        itemGuidLow, uint32(row.healthPermille), row.awayAt, row.faintedAt);
}

uint32 FusionMgr::CompanionFaintWait(Item* item) const
{
    uint32 const traits = item ? CompanionStateOf(item->GetGUIDLow(), item->GetEntry()).traits : 0;
    return Companions::FaintRecoverSeconds(traits, m_faint.recoverSeconds);
}

std::string FusionMgr::CompanionName(Item* item) const
{
    if (item)
        if (Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry()))
            return sp->name;
    return "companion";
}

Companions::Recovery FusionMgr::SettleCompanion(Player* player, Item* item, bool tell)
{
    Companions::Recovery r = CompanionRecoveryOf(item->GetGUIDLow());
    if (!Companions::Settle(r, UnixNow(), CompanionFaintWait(item)))
        return r;
    SaveCompanionRecovery(item->GetGUIDLow(), r);
    if (tell && player)
    {
        std::string const name = CompanionName(item);
        ChatLine(player, "|cff71d5ffYour " + name + " has come round: it can be summoned again.|r");
        CompanionNotice(player, Index::NoticeKind::Good, "Your " + name + " has come round.");
    }
    return r;
}

uint32 FusionMgr::CompanionFaintLeft(Player* player, Item* item)
{
    Companions::Recovery const r = SettleCompanion(player, item, true);
    return Companions::FaintSecondsLeft(r, UnixNow(), CompanionFaintWait(item));
}

void FusionMgr::FaintCompanion(Player* player, ObjectGuid itemGuid)
{
    Item* item = player ? player->GetItemByGuid(itemGuid) : nullptr;
    uint32 const guidLow = item ? item->GetGUIDLow() : itemGuid.GetCounter();
    if (!guidLow)
        return;
    uint32 const now = UnixNow();
    Companions::Recovery r = CompanionRecoveryOf(guidLow);
    uint32 const wait = CompanionFaintWait(item);
    // Already down (its kill was seen, then its look put it away): one faint, one wait.
    if (Companions::IsFainted(r, now, wait))
        return;
    Companions::Faint(r, now);
    SaveCompanionRecovery(guidLow, r);
    if (!player)
        return;
    std::string const name = CompanionName(item);
    ChatLine(player, "|cffff6060Your " + name + " has fainted!|r It comes round on its own in " + Companions::WaitText(wait)
        + ", or revive it sooner at a stable master or with a Reviving Draught.");
    CompanionNotice(player, Index::NoticeKind::Error, "Your " + name + " has fainted.");
}

void FusionMgr::PutCompanionAway(ObjectGuid itemGuid, float share)
{
    uint32 const guidLow = itemGuid.GetCounter();
    if (!guidLow)
        return;
    Companions::Recovery r = CompanionRecoveryOf(guidLow);
    Companions::PutAway(r, share, UnixNow());
    SaveCompanionRecovery(guidLow, r);
}

void FusionMgr::CompanionLogout(Player* player)
{
    if (!m_enabled || !player || !player->HasFusion())
        return;
    FusionCompanionOut& c = player->GetFusion().companion;
    if (c.item.IsEmpty() || (c.pet.IsEmpty() && !c.riding && !c.suspended))
        return;
    Pet* pet = CompanionPet(player);
    // Killed and logged out before its look saw it: it fainted all the same.
    if (!c.riding && (c.petDied || (pet && !pet->IsAlive())))
    {
        FaintCompanion(player, c.item);
        SaveCompanionOut(player, ObjectGuid());
        return;
    }
    // Off the saddle first: a mount aura saved now would be loaded at the next login with no
    // ride to show the companion, as its spell's own mount (a wolf for a tiger).
    if (c.riding && c.rideSpell && player->HasAura(c.rideSpell))
        player->RemoveAurasDueToSpell(c.rideSpell);
    // As hurt as it is; the out row stays, so it is back at the next login, no healthier.
    float share = c.riding ? c.rideHealth : c.seenHealth;
    if (pet && pet->IsAlive() && pet->GetMaxHealth())
        share = float(pet->GetHealth()) / float(pet->GetMaxHealth());
    PutCompanionAway(c.item, share);
}

void FusionMgr::LookAtFaintedCompanions(Player* player, uint32 /*now*/)
{
    bool any = false;
    ForEachBagItem(player, [&](Item* item) {
        if (!Companions::IsCompanionItem(item->GetEntry()))
            return;
        Companions::Recovery const before = CompanionRecoveryOf(item->GetGUIDLow());
        if (!before.faintedAt)
            return;
        Companions::Recovery const after = SettleCompanion(player, item, true);
        any = any || (before.faintedAt && !after.faintedAt);
    });
    if (any)
        SendCompanionCarried(player);
}

// ── Reviving ────────────────────────────────────────────────────────────────────────────────

FusionMgr::CompanionCare FusionMgr::CompanionCareOf(Player* player)
{
    CompanionCare care;
    uint32 const now = UnixNow();
    FusionCompanionOut const& c = player->GetFusion().companion;
    ForEachBagItem(player, [&](Item* item) {
        if (!Companions::IsCompanionItem(item->GetEntry()) || !CompanionSpeciesOfItem(item->GetEntry()))
            return;
        Companions::Recovery const r = SettleCompanion(player, item, true);
        uint32 const wait = CompanionFaintWait(item);
        bool const out = c.item == item->GetObjectGuid() && (!c.pet.IsEmpty() || c.riding);
        if (Companions::IsFainted(r, now, wait))
        {
            ++care.fainted;
            care.cost += Companions::StableReviveCost(CompanionStateOf(item->GetGUIDLow(), item->GetEntry()).level,
                m_faint.costPerLevel);
            return;
        }
        float health = Companions::HealthAt(r, now, wait);
        if (out)
        {
            Pet* pet = CompanionPet(player);
            health = c.riding ? c.rideHealth
                : pet && pet->GetMaxHealth() ? float(pet->GetHealth()) / float(pet->GetMaxHealth()) : 1.f;
        }
        if (health < 0.995f)
            ++care.hurt;
    });
    return care;
}

void FusionMgr::ReviveAtStable(Player* player)
{
    if (!player->HasFusion())
        return;
    bool const gm = IsAdmin(player);
    if (!AtCompanionStable(player) && !gm)
    {
        CompanionNotice(player, Index::NoticeKind::Error, "A stable master revives companions: talk to one.");
        return;
    }
    CompanionCare const care = CompanionCareOf(player);
    if (!care.fainted && !care.hurt)
    {
        CompanionNotice(player, Index::NoticeKind::Info, "Your companions are all well.");
        return;
    }
    uint32 const cost = gm ? 0 : care.cost;
    if (cost && player->GetMoney() < cost)
    {
        std::string const text = "Reviving your companions costs " + Money(cost) + ".";
        CompanionNotice(player, Index::NoticeKind::Error, text);
        ChatLine(player, text);
        return;
    }
    if (cost)
        player->ModifyMoney(-int32(cost));
    uint32 const now = UnixNow();
    std::vector<std::string> revived;
    FusionCompanionOut& c = player->GetFusion().companion;
    ForEachBagItem(player, [&](Item* item) {
        if (!Companions::IsCompanionItem(item->GetEntry()) || !CompanionSpeciesOfItem(item->GetEntry()))
            return;
        Companions::Recovery r = CompanionRecoveryOf(item->GetGUIDLow());
        if (Companions::IsFainted(r, now, CompanionFaintWait(item)))
            revived.push_back(CompanionName(item));
        Companions::Revive(r, now, 1.f);
        SaveCompanionRecovery(item->GetGUIDLow(), r);
    });
    // The one out: whole as well.
    if (Pet* pet = CompanionPet(player))
        if (pet->IsAlive())
        {
            pet->SetHealth(pet->GetMaxHealth());
            c.seenHealth = 1.f;
        }
    if (c.riding)
        c.rideHealth = 1.f;
    std::string text = revived.empty() ? "The stable master tends to your companions: every wound is healed."
        : "The stable master tends to your companions: " + NameList(revived) + (revived.size() == 1 ? " is" : " are")
            + " back on " + (revived.size() == 1 ? "its" : "their") + " feet, and every wound is healed";
    if (!revived.empty())
        text += cost ? " (" + Money(cost) + ")." : ".";
    ChatLine(player, "|cff71d5ff" + text + "|r");
    CompanionNotice(player, Index::NoticeKind::Good, revived.empty() ? "Your companions are healed." : "Your companions are revived.");
    SendCompanionCarried(player);
    SendCompanionBar(player, true);
}

void FusionMgr::UseReviveDraught(Player* player, uint64 companionItem)
{
    if (!player->HasFusion())
        return;
    if (!player->GetItemCount(Companions::kReviveDraughtItem))
    {
        CompanionNotice(player, Index::NoticeKind::Error,
            "You have no Reviving Draught: general goods vendors and stable masters sell them.");
        return;
    }
    uint32 const now = UnixNow();
    Item* target = nullptr;
    if (companionItem)
    {
        Item* item = player->GetItemByGuid(ObjectGuid(companionItem));
        if (!item || !Companions::IsCompanionItem(item->GetEntry()) || !Player::IsInventoryPos(item->GetPos()))
        {
            CompanionNotice(player, Index::NoticeKind::Error, "That companion must be in your bags.");
            return;
        }
        if (!CompanionFaintLeft(player, item))
        {
            CompanionNotice(player, Index::NoticeKind::Error, "Your " + CompanionName(item) + " has not fainted.");
            return;
        }
        target = item;
    }
    else
    {
        // The one that fainted last.
        uint32 latest = 0;
        ForEachBagItem(player, [&](Item* item) {
            if (!Companions::IsCompanionItem(item->GetEntry()))
                return;
            Companions::Recovery const r = SettleCompanion(player, item, true);
            if (Companions::IsFainted(r, now, CompanionFaintWait(item)) && (!target || r.faintedAt >= latest))
            {
                target = item;
                latest = r.faintedAt;
            }
        });
        if (!target)
        {
            CompanionNotice(player, Index::NoticeKind::Info, "None of your companions has fainted.");
            return;
        }
    }
    player->DestroyItemCount(Companions::kReviveDraughtItem, 1, true);
    Companions::Recovery r = CompanionRecoveryOf(target->GetGUIDLow());
    Companions::Revive(r, now, Companions::kDraughtReviveHealth);
    SaveCompanionRecovery(target->GetGUIDLow(), r);
    FusionCompanionOut& c = player->GetFusion().companion;
    c.draughtAskItem.Clear();
    std::string const name = CompanionName(target);
    ChatLine(player, "|cff71d5ffThe Reviving Draught brings your " + name + " round: it is back on its feet at half health.|r");
    CompanionNotice(player, Index::NoticeKind::Good, "Your " + name + " is revived.");
    SendCompanionCarried(player);
}

void FusionMgr::UseReviveDraughtItem(Player* player, Item* draught)
{
    if (!m_enabled || !player->HasFusion() || !draught)
        return;
    if (!Player::IsInventoryPos(draught->GetPos()) || draught->IsInTrade())
    {
        player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, draught, nullptr);
        return;
    }
    UseReviveDraught(player, 0);
}

void FusionMgr::BuyReviveDraught(Player* player)
{
    bool const gm = IsAdmin(player);
    if (!AtCompanionStable(player) && !gm)
    {
        CompanionNotice(player, Index::NoticeKind::Error, "Stable masters sell Reviving Draughts: talk to one.");
        return;
    }
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(Companions::kReviveDraughtItem);
    if (!proto)
    {
        CompanionNotice(player, Index::NoticeKind::Error, "The stable master has no Reviving Draughts today.");
        return;
    }
    uint32 const price = gm ? 0 : (proto->BuyPrice ? proto->BuyPrice : Companions::kReviveDraughtPrice);
    if (player->GetMoney() < price)
    {
        CompanionNotice(player, Index::NoticeKind::Error, "A Reviving Draught costs " + Money(price) + ".");
        return;
    }
    ItemPosCountVec dest;
    InventoryResult const msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, Companions::kReviveDraughtItem, 1);
    if (msg != EQUIP_ERR_OK)
    {
        player->SendEquipError(msg, nullptr, nullptr, 0, Companions::kReviveDraughtItem);
        return;
    }
    Item* item = player->StoreNewItem(dest, Companions::kReviveDraughtItem, true);
    if (!item)
        return;
    if (price)
        player->ModifyMoney(-int32(price));
    player->SendNewItem(item, 1, true, false);
    SendCompanionCarried(player);
}

// ── The stable master ───────────────────────────────────────────────────────────────────────

bool FusionMgr::CompanionStableGossip(Player* player, Creature* creature)
{
    CompanionCare const care = CompanionCareOf(player);
    if (!care.fainted && !care.hurt)
        return false;
    PlayerMenu* menu = player->PlayerTalkClass;
    menu->ClearMenus();
    std::string revive = care.fainted ? "Revive and heal my companions" : "Heal my companions";
    if (care.fainted && care.cost && !IsAdmin(player))
        revive += " (" + Money(care.cost) + ")";
    menu->GetGossipMenu().AddMenuItem(GOSSIP_ICON_INTERACT_1, revive, kStableSender, kStableRevive, "", false);
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(Companions::kReviveDraughtItem);
    if (proto)
        menu->GetGossipMenu().AddMenuItem(GOSSIP_ICON_VENDOR, "Buy a Reviving Draught ("
            + Money(proto->BuyPrice ? proto->BuyPrice : Companions::kReviveDraughtPrice) + ")", kStableSender, kStableBuy, "", false);
    menu->GetGossipMenu().AddMenuItem(GOSSIP_ICON_CHAT, "Show me my companions", kStableSender, kStableWindow, "", false);
    menu->SendGossipMenu(player->GetGossipTextId(creature), creature->GetObjectGuid());
    return true;
}

bool FusionMgr::CompanionStableSelect(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (sender != kStableSender || !m_enabled || !player->HasFusion())
        return false;
    player->PlayerTalkClass->CloseGossip();
    if (!creature || !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_STABLEMASTER))
        return true;
    player->GetFusion().companionStable = creature->GetObjectGuid();
    switch (action)
    {
        case kStableRevive:
            ReviveAtStable(player);
            break;
        case kStableBuy:
            BuyReviveDraught(player);
            // Still someone to revive: the options again.
            CompanionStableGossip(player, creature);
            break;
        default:
            SendChannel(player, Companions::kChannelCompanion, Index::EncodeOpen(Index::Access::Npc));
            break;
    }
    return true;
}
