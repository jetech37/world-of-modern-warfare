/*
 * World of Modern Warfare: area looting (fusion/AreaLoot.h) in the game. See AreaLoot.h.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "AreaLoot.h"

#include "CellImpl.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "World.h"
#include "Fusion/FusionMgr.h"
#include "WorldSession.h"
#include "fusion/AreaLoot.h"
#include "fusion/Packets.h"

#include <algorithm>
#include <list>
#include <vector>

namespace
{
    using Fusion::AreaLoot::Entry;
    using Fusion::AreaLoot::Row;
    using Fusion::AreaLoot::Session;
    typedef WorldPackets::Loot::LootResponse::LootSlotItem SlotItem;

    float Radius()
    {
        return Fusion::AreaLoot::ClampRadius(sWorld.getConfig(CONFIG_FLOAT_AREA_LOOT_RADIUS));
    }

    // The other corpses a window may gather: dead, sparkling for this player, in reach and in sight.
    class CorpseCheck
    {
    public:
        CorpseCheck(Player* player, Creature const* primary, float radius)
            : m_player(player), m_primary(primary), m_radius(radius) {}

        bool operator()(Creature* creature)
        {
            if (creature == m_primary || creature->IsAlive() || !creature->IsInWorld())
                return false;
            if (!creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
                return false;
            if (creature->lootForSkin || creature->lootForPickPocketed)
                return false;
            if (!m_player->IsWithinDistInMap(creature, m_radius))
                return false;
            // AI, tapping, present at the death, round robin, anything left: the sparkle's rule.
            if (!m_player->IsAllowedToLoot(creature))
                return false;
            return m_player->IsWithinLOSInMap(creature);
        }

    private:
        Player* m_player;
        Creature const* m_primary;
        float m_radius;
    };

    // The corpse behind a window row, if the player may still take from it; else nullptr and why.
    Creature* SourceFor(Player* player, uint64 raw, PermissionTypes& permission, LootError& error)
    {
        Session const& s = player->GetAreaLoot();
        bool const isPrimary = raw == s.primary;
        error = LOOT_ERROR_DIDNT_KILL;
        if (!player->IsInWorld())
            return nullptr;

        Creature* creature = player->GetMap()->GetCreature(ObjectGuid(raw));
        if (!creature || !creature->IsInWorld() || creature->IsAlive())
            return nullptr;
        if (creature->lootForSkin || creature->lootForPickPocketed)
            return nullptr;
        if (creature->AI() && !creature->AI()->CanBeLooted())
            return nullptr;

        if (isPrimary)
        {
            // As a lone corpse's take asks.
            if (!creature->IsWithinDistInMap(player, player->GetMaxLootDistance(creature), true, SizeFactor::None))
            {
                error = LOOT_ERROR_TOO_FAR;
                return nullptr;
            }
        }
        else
        {
            if (!player->IsWithinDistInMap(creature, Radius() + Fusion::AreaLoot::kTakeSlack))
            {
                error = LOOT_ERROR_TOO_FAR;
                return nullptr;
            }
            if (!player->IsAllowedToLoot(creature))
                return nullptr;
        }

        Player* recipient = creature->GetLootRecipient();
        if (!recipient)
            return nullptr;
        permission = player->GetCorpseLootPermission(creature, recipient);
        if (permission == NONE_PERMISSION || permission == MASTER_PERMISSION)
            return nullptr;
        return creature;
    }

    // The player stops looting a corpse of the window; an emptied one stops sparkling.
    void LetGo(Player* player, uint64 raw)
    {
        if (!player->IsInWorld())
            return;
        Creature* creature = player->GetMap()->GetCreature(ObjectGuid(raw));
        if (!creature)
            return;
        if (creature->IsAlive())
            creature->loot.RemoveLooter(player->GetObjectGuid());
        else
            player->GetSession()->ReleaseCreatureLoot(creature);
    }

    uint32 EntryOf(uint64_t raw)
    {
        return ObjectGuid(raw).GetEntry();
    }

    // The window's source map (fusion/AreaLoot.h, op 36), so the client can section the window
    // by corpse; a client that does not know it shows the flat window.
    void SendSources(Player* player)
    {
        Session const& s = player->GetAreaLoot();
        if (!s.Active())
            return;
        sFusionMgr.SendChannel(player, Fusion::Packets::kChannelWorld,
            Fusion::AreaLoot::Encode(Fusion::AreaLoot::MapOf(s, &EntryOf)));
    }

    // A corpse (not the opened one) leaves the window: its rows go from the client's window too.
    void DropFromWindow(Player* player, uint64 raw)
    {
        Session& s = player->GetAreaLoot();
        if (!s.HasSource(raw) || raw == s.primary)
            return;
        std::vector<uint8> slots;
        for (Entry const& e : s.entries)
            if (e.source == raw)
                slots.push_back(e.windowSlot);
        s.DropSource(raw);
        for (uint8 slot : slots)
            player->SendNotifyLootItemRemoved(slot);
        LetGo(player, raw);
        SendSources(player);
    }

    void RemoveRow(Player* player, uint8 windowSlot)
    {
        player->GetAreaLoot().RemoveWindowSlot(windowSlot);
        player->SendNotifyLootItemRemoved(windowSlot);
    }
}

bool AreaLooting::WriteWindow(Player* player, Creature* primary, PermissionTypes permission, WorldPackets::Loot::LootResponse& packet)
{
    Session& s = player->GetAreaLoot();
    s.Clear();

    if (!sWorld.getConfig(CONFIG_BOOL_AREA_LOOT_ENABLE))
        return false;
    float const radius = Radius();
    if (radius <= 0.f)
        return false;
    if (!player->IsInWorld() || player->IsInCombat())
        return false;
    if (permission == NONE_PERMISSION || permission == MASTER_PERMISSION)
        return false;
    if (primary->lootForSkin)
        return false;

    std::list<Creature*> found;
    CorpseCheck check(player, primary, radius);
    MaNGOS::CreatureListSearcher<CorpseCheck> searcher(found, check);
    Cell::VisitGridObjects(player, searcher, radius);
    if (found.empty())
        return false;

    // Nearest first, so a full window takes what lies closest.
    std::vector<std::pair<float, Creature*>> byDistance;
    byDistance.reserve(found.size());
    for (Creature* creature : found)
        byDistance.emplace_back(player->GetDistance(creature, SizeFactor::None), creature);
    std::sort(byDistance.begin(), byDistance.end(), [](std::pair<float, Creature*> const& a, std::pair<float, Creature*> const& b)
    {
        if (a.first != b.first)
            return a.first < b.first;
        return a.second->GetObjectGuid().GetRawValue() < b.second->GetObjectGuid().GetRawValue();
    });

    // The opened corpse's rows first, as its own window shows them.
    std::vector<Row> rows;
    std::vector<SlotItem> rowItems;
    uint64 const primaryRaw = primary->GetObjectGuid().GetRawValue();
    {
        WorldPackets::Loot::LootResponse view;
        LootView(primary->loot, player, permission).WriteLoot(view);
        for (SlotItem const& item : view.items)
        {
            rows.push_back(Row{primaryRaw, item.idx, item.itemId});
            rowItems.push_back(item);
        }
    }

    // Then each other corpse's rows this player may take now, as opening it would start them.
    std::vector<Creature*> candidates;
    for (auto const& pair : byDistance)
    {
        Creature* creature = pair.second;
        Player* recipient = creature->GetLootRecipient();
        if (!recipient)
            continue;
        PermissionTypes const p = player->GetCorpseLootPermission(creature, recipient);
        if (p == NONE_PERMISSION || p == MASTER_PERMISSION)
            continue;

        player->PrepareCreatureBodyLoot(creature, recipient);
        if (creature->loot.isLooted())
            continue;

        WorldPackets::Loot::LootResponse view;
        LootView(creature->loot, player, p).WriteLoot(view);
        bool any = false;
        uint64 const raw = creature->GetObjectGuid().GetRawValue();
        for (SlotItem const& item : view.items)
        {
            if (item.slotType != LOOT_SLOT_TYPE_ALLOW_LOOT)
                continue;
            rows.push_back(Row{raw, item.idx, item.itemId});
            rowItems.push_back(item);
            any = true;
        }
        if (any || creature->loot.gold)
            candidates.push_back(creature);
    }

    std::vector<Entry> entries = Fusion::AreaLoot::Pack(rows);

    // A corpse joins the window with a row in it or with coin; the rest keep their loot.
    std::vector<Creature*> extras;
    for (Creature* creature : candidates)
    {
        uint64 const raw = creature->GetObjectGuid().GetRawValue();
        bool const shown = std::any_of(entries.begin(), entries.end(), [raw](Entry const& e) { return e.source == raw; });
        if (shown || creature->loot.gold)
            extras.push_back(creature);
    }
    if (extras.empty())
        return false;

    std::vector<uint32> golds;
    golds.push_back(primary->loot.gold);
    for (Creature* creature : extras)
        golds.push_back(creature->loot.gold);
    packet.gold = Fusion::AreaLoot::SumGold(golds);

    for (Entry const& e : entries)
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].source != e.source || rows[i].sourceSlot != e.sourceSlot)
                continue;
            SlotItem item = rowItems[i];
            item.idx = e.windowSlot;
            packet.items.push_back(item);
            break;
        }
    }

    s.primary = primaryRaw;
    s.sources.push_back(primaryRaw);
    for (Creature* creature : extras)
    {
        s.sources.push_back(creature->GetObjectGuid().GetRawValue());
        creature->loot.loot_type = LOOT_CORPSE;
        creature->loot.AddLooter(player->GetObjectGuid());
    }
    s.entries = entries;

    // Before the window itself (Player::SendLoot sends it next), so the client lays it out at once.
    SendSources(player);

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "AreaLoot: %s opens %s with %u other corpse(s), %u row(s), %u copper",
        player->GetGuidStr().c_str(), primary->GetGuidStr().c_str(), uint32(extras.size()), uint32(entries.size()), packet.gold);
    return true;
}

void AreaLooting::TakeItem(Player* player, uint8 windowSlot)
{
    Session& s = player->GetAreaLoot();
    Entry const* entry = s.ByWindowSlot(windowSlot);
    if (!entry)
    {
        player->SendEquipError(EQUIP_ERR_ALREADY_LOOTED, nullptr, nullptr);
        return;
    }
    uint64 const raw = entry->source;
    uint8 const sourceSlot = entry->sourceSlot;
    uint32 const itemId = entry->itemId;
    bool const isPrimary = raw == s.primary;
    ObjectGuid const primaryGuid(s.primary);

    PermissionTypes permission = NONE_PERMISSION;
    LootError error = LOOT_ERROR_DIDNT_KILL;
    Creature* creature = SourceFor(player, raw, permission, error);
    if (!creature)
    {
        if (isPrimary)
            player->SendLootError(primaryGuid, error);
        else
        {
            // Gone, risen, out of reach or no longer this player's: it leaves the window.
            DropFromWindow(player, raw);
            player->SendEquipError(EQUIP_ERR_ALREADY_LOOTED, nullptr, nullptr);
        }
        return;
    }

    // The row must still be what the window showed, and the player's to take now.
    WorldPackets::Loot::LootResponse view;
    LootView(creature->loot, player, permission).WriteLoot(view);
    auto row = std::find_if(view.items.begin(), view.items.end(), [sourceSlot](SlotItem const& item) { return item.idx == sourceSlot; });
    if (row == view.items.end() || row->itemId != itemId)
    {
        RemoveRow(player, windowSlot);
        player->SendEquipError(EQUIP_ERR_ALREADY_LOOTED, nullptr, nullptr);
        return;
    }
    if (row->slotType != LOOT_SLOT_TYPE_ALLOW_LOOT)
    {
        player->SendLootError(primaryGuid, LOOT_ERROR_DIDNT_KILL);
        return;
    }

    // Its removal notice reaches this window through the Session, as its own slot.
    if (!player->GetSession()->StoreLootItemFrom(&creature->loot, creature->GetObjectGuid(), sourceSlot, nullptr))
        return;

    // An emptied corpse of the window is done now: it stops sparkling and may be skinned.
    if (!isPrimary && creature->loot.isLooted())
    {
        s.DropSource(raw);
        player->GetSession()->ReleaseCreatureLoot(creature);
        SendSources(player);
    }
}

void AreaLooting::TakeMoney(Player* player)
{
    Session& s = player->GetAreaLoot();
    s.takingMoney = true;
    size_t const sourcesBefore = s.sources.size();

    std::vector<uint64> const sources = s.sources;
    for (uint64 raw : sources)
    {
        bool const isPrimary = raw == s.primary;
        PermissionTypes permission = NONE_PERMISSION;
        LootError error = LOOT_ERROR_DIDNT_KILL;
        Creature* creature = SourceFor(player, raw, permission, error);
        if (!creature)
        {
            if (!isPrimary)
                DropFromWindow(player, raw);
            continue;
        }
        if (!creature->loot.gold)
            continue;

        player->GetSession()->LootMoneyFrom(&creature->loot, true, nullptr);

        if (!isPrimary && creature->loot.isLooted())
        {
            s.DropSource(raw);
            player->GetSession()->ReleaseCreatureLoot(creature);
        }
    }

    s.takingMoney = false;
    if (s.sources.size() != sourcesBefore)
        SendSources(player);
    player->SendNotifyLootMoneyRemoved();
}

void AreaLooting::ReleaseSource(Player* player, ObjectGuid source)
{
    DropFromWindow(player, source.GetRawValue());
}

void AreaLooting::EndSession(Player* player)
{
    Session& s = player->GetAreaLoot();
    if (!s.Active())
        return;

    std::vector<uint64> const sources = s.sources;
    uint64 const primary = s.primary;
    s.Clear();
    for (uint64 raw : sources)
        if (raw != primary)
            LetGo(player, raw);
}
