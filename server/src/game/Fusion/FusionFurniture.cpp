/*
 * World of Modern Warfare: furniture's uses (fusion/Furniture.h; src/fusion/docs/CONSTRUCTION.md,
 * "Furniture"). Seats and beds, the plot's shared stash, books, crafting stations as WoW's spell
 * focuses, the mailbox and training dummy the server stands in their pieces, trophy plinths and
 * the bard's stage.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "Bag.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GameObject.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "WorldSession.h"

#include "fusion/Furniture.h"
#include "fusion/Plots.h"

#include <algorithm>
#include <sstream>

namespace
{
    namespace F = Fusion::Furniture;
    namespace Plots = Fusion::Plots;
    using Fusion::Build::Piece;

    Fusion::Vec3 FeetOf(Player const* p) { return {p->GetPositionX(), p->GetPositionY(), p->GetPositionZ()}; }
    int64 Now() { return int64(time(nullptr)); }

    // The server's stand-ins for the pieces that are WoW objects at heart (sql 33_furniture.sql).
    constexpr uint32 kDummyEntry = 94250;           // creature_template: the training dummy
    constexpr uint32 kTrophyEntry = 94251;          // creature_template: a trophy's likeness
    constexpr uint32 kMailboxEntry = 194200;        // gameobject_template: a mailbox (type 19)
    // Stand-ins are kept up for pieces this near a player, checked this often.
    constexpr float kProxyNear = 120.f;
    constexpr uint32 kProxyCheckMs = 3000;
    // A trophy's likeness is drawn this much smaller than the companion.
    constexpr float kTrophyScale = 0.45f;
}

void FusionMgr::LoadFurniture()
{
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `fusion_plot_stash` ("
        "`plot` int(10) unsigned NOT NULL,"
        "`slot` smallint(5) unsigned NOT NULL,"
        "`item` int(10) unsigned NOT NULL,"
        "`count` int(10) unsigned NOT NULL,"
        "PRIMARY KEY (`plot`, `slot`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Furniture: a plot''s shared stash (fusion/Furniture.h)'");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `fusion_furniture_trophy` ("
        "`piece` int(10) unsigned NOT NULL,"
        "`display` int(10) unsigned NOT NULL,"
        "`scale` float NOT NULL DEFAULT '1',"
        "PRIMARY KEY (`piece`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Furniture: the likeness on a trophy plinth'");
    // A trophy whose plinth is gone is forgotten.
    CharacterDatabase.DirectExecute("DELETE t FROM `fusion_furniture_trophy` t LEFT JOIN `fusion_build_piece` p ON p.`id` = t.`piece` WHERE p.`id` IS NULL");
    std::lock_guard<std::mutex> lock(m_furnitureLock);
    m_stashes.clear();
    m_trophies.clear();
    uint32 stashed = 0;
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query("SELECT `plot`, `item`, `count` FROM `fusion_plot_stash` ORDER BY `plot`, `slot`"))
    {
        do
        {
            Field* f = r->Fetch();
            if (f[2].GetUInt32() && sObjectMgr.GetItemPrototype(f[1].GetUInt32()))
            {
                m_stashes[f[0].GetUInt32()].push_back({f[1].GetUInt32(), f[2].GetUInt32()});
                ++stashed;
            }
        } while (r->NextRow());
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query("SELECT `piece`, `display`, `scale` FROM `fusion_furniture_trophy`"))
    {
        do
        {
            Field* f = r->Fetch();
            m_trophies[f[0].GetUInt32()] = {f[1].GetUInt32(), f[2].GetFloat()};
        } while (r->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: furniture: %u kinds, %u stashed stacks on %u plots, %u trophies.",
        uint32(F::All().size()), stashed, uint32(m_stashes.size()), uint32(m_trophies.size()));
}

bool FusionMgr::FurniturePieceNear(Player* player, uint32 pieceId, float reach, Piece& out)
{
    Map* map = player->GetMap();
    if (!map)
        return false;
    std::lock_guard<std::mutex> lock(m_buildLock);
    auto it = m_pieces.find(map->GetId());
    if (it == m_pieces.end())
        return false;
    auto pit = std::find_if(it->second.begin(), it->second.end(), [pieceId](Piece const& p) { return p.id == pieceId; });
    if (pit == it->second.end() || !F::Find(pit->type))
        return false;
    out = *pit;
    return Fusion::Build::Distance(*pit, FeetOf(player) + Fusion::Vec3{0.f, 0.f, 1.f}) <= reach;
}

Plots::Access FusionMgr::PlotAccessAt(Player const* player, Piece const& p, uint32& plotId)
{
    plotId = Plots::PlotOf(PlotGrid(), p);
    std::lock_guard<std::mutex> lock(m_plotLock);
    return Plots::AccessOf(PlotActor(player), plotId ? m_plots.Find(plotId) : nullptr, Now());
}

void FusionMgr::SendFurniturePage(Player* player, Piece const& p, std::string const& title, std::vector<std::string> const& lines)
{
    SendChannel(player, Fusion::Packets::kChannelBuild, F::EncodePage(p.id, F::UseOf(p.type), title, lines));
}

void FusionMgr::UseFurniture(Player* player, uint32 pieceId)
{
    Piece p;
    if (!player->IsAlive() || !FurniturePieceNear(player, pieceId, F::kUseReach + 1.f, p))
        return;
    F::Def const* d = F::Find(p.type);
    if (!d)
        return;
    ChatHandler chat(player);
    switch (d->use)
    {
        case F::Use::Seat:
        case F::Use::Bed:
        {
            if (player->IsInCombat())
            {
                chat.SendSysMessage("Not in a fight.");
                return;
            }
            Fusion::Vec3 const at = F::SeatSpot(p);
            // Facing out of the seat (its front is its +x).
            player->NearTeleportTo(at.x, at.y, at.z + 0.05f, p.yaw);
            player->SetStandState(d->use == F::Use::Bed ? UNIT_STAND_STATE_SLEEP : UNIT_STAND_STATE_SIT_MEDIUM_CHAIR);
            if (d->use == F::Use::Bed)
            {
                uint32 plot = 0;
                Plots::Access const a = PlotAccessAt(player, p, plot);
                chat.SendSysMessage(plot && a >= Plots::Access::Guest
                    ? "You lie down. At your homestead you rest here as at an inn."
                    : "You lie down. (A bed rests you only on your own homestead or one you hold a permit on.)");
            }
            return;
        }
        case F::Use::Stash:
            SendStash(player, p);
            return;
        case F::Use::Book:
        {
            F::Book const* b = F::BookOf(d->book);
            if (!b)
                return;
            std::vector<std::string> lines(b->pages.begin(), b->pages.end());
            SendFurniturePage(player, p, b->title, lines);
            return;
        }
        case F::Use::Station:
        {
            static char const* const says[] = {"", "An anvil: smiths' recipes that ask for one work within reach.",
                "A forge: smelting and the smiths' forged recipes work within reach.",
                "A cooking fire: cooking recipes work within reach.",
                "A loom: tailoring recipes that ask for one work within reach.",
                "An alchemy table: alchemy recipes that ask for a lab work within reach."};
            size_t const i = size_t(d->station);
            chat.SendSysMessage(i < sizeof(says) / sizeof(says[0]) ? says[i] : "");
            return;
        }
        case F::Use::Mailbox:
            chat.SendSysMessage("A mailbox: right-click it to read and send your mail.");
            return;
        case F::Use::Dummy:
            chat.SendSysMessage("A training dummy: strike it with anything. When you stop, it tells you your damage per second.");
            return;
        case F::Use::Stage:
            chat.SendSysMessage("A bard's stage: songs played standing on it land a quarter stronger.");
            return;
        case F::Use::Trophy:
        {
            uint32 plot = 0;
            bool const mine = p.owner == player->GetGUIDLow() || PlotAccessAt(player, p, plot) >= Plots::Access::Owner;
            if (!mine)
            {
                chat.SendSysMessage("A trophy plinth: only its owner mounts a likeness on it.");
                return;
            }
            Pet* pet = player->GetPet();
            if (!pet || !pet->IsAlive())
            {
                chat.SendSysMessage("Call a companion out first: the plinth takes the likeness of the companion beside you.");
                return;
            }
            uint32 const display = pet->GetNativeDisplayId();
            float const scale = std::max(0.2f, pet->GetObjectScale() * kTrophyScale);
            {
                std::lock_guard<std::mutex> lock(m_furnitureLock);
                m_trophies[p.id] = {display, scale};
            }
            CharacterDatabase.PExecute("REPLACE INTO `fusion_furniture_trophy` (`piece`, `display`, `scale`) VALUES (%u, %u, %f)", p.id, display, scale);
            // The old likeness goes; the next check stands the new one.
            DropFurnitureProxy(player->GetMap(), p.id);
            chat.PSendSysMessage("The plinth now shows %s.", pet->GetName());
            return;
        }
        default:
            return;
    }
}

uint16 FusionMgr::StashSlotsOf(uint32 mapId, uint32 plotId)
{
    std::vector<Piece> onPlot;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(mapId);
        if (it != m_pieces.end())
            for (Piece const& q : it->second)
                if (F::UseOf(q.type) == F::Use::Stash && Plots::PlotOf(PlotGrid(), q) == plotId)
                    onPlot.push_back(q);
    }
    return F::StashSlots(onPlot);
}

void FusionMgr::SendStash(Player* player, Piece const& p)
{
    uint32 plot = 0;
    Plots::Access const a = PlotAccessAt(player, p, plot);
    if (!plot)
    {
        SendFurniturePage(player, p, "Stash", {"A stash works on a plot: it keeps the plot's shared store. Out in the world it is only a chest."});
        return;
    }
    if (a < Plots::Access::Guest)
    {
        SendFurniturePage(player, p, "Stash", {"This plot's stash is its household's: its owner and those they permit."});
        return;
    }
    uint16 const slots = StashSlotsOf(p.map, plot);
    std::vector<F::StashEntry> stash;
    {
        std::lock_guard<std::mutex> lock(m_furnitureLock);
        auto it = m_stashes.find(plot);
        if (it != m_stashes.end())
            stash = it->second;
    }
    // What the bags hold that may go in (the backpack and the four bags).
    std::vector<F::BagItem> bags;
    auto consider = [&](uint8 bag, uint8 slot) {
        Item* it = player->GetItemByPos(bag, slot);
        if (it && StorableItem(it))
            bags.push_back({bag, slot, it->GetEntry(), uint16(std::min<uint32>(it->GetCount(), 0xFFFF))});
    };
    for (uint8 s = INVENTORY_SLOT_ITEM_START; s < INVENTORY_SLOT_ITEM_END; ++s)
        consider(INVENTORY_SLOT_BAG_0, s);
    for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
        if (Item* bagItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, b))
            if (bagItem->IsBag())
                for (uint32 s = 0; s < static_cast<Bag*>(bagItem)->GetBagSize(); ++s)
                    consider(b, uint8(s));
    SendChannel(player, Fusion::Packets::kChannelBuild, F::EncodeStashView(p.id, slots, stash, bags));
}

bool FusionMgr::StorableItem(Item const* it) const
{
    if (!it)
        return false;
    ItemPrototype const* proto = it->GetProto();
    if (!proto)
        return false;
    F::ItemFacts f;
    f.soulbound = it->IsSoulBound();
    for (uint32 e = 0; e < MAX_ENCHANTMENT_SLOT; ++e)
        if (it->GetEnchantmentId(EnchantmentSlot(e)))
            f.enchanted = true;
    f.randomProperty = it->GetItemRandomPropertyId() != 0;
    f.quest = proto->Class == ITEM_CLASS_QUEST || proto->StartQuest != 0;
    f.filledBag = it->IsBag() && !static_cast<Bag const*>(it)->IsEmpty();
    f.conjured = (proto->Flags & ITEM_FLAG_CONJURED) != 0;
    return F::Storable(f);
}

void FusionMgr::SaveStash(uint32 plotId, std::vector<F::StashEntry> const& stash)
{
    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `fusion_plot_stash` WHERE `plot` = %u", plotId);
    for (size_t i = 0; i < stash.size(); ++i)
        CharacterDatabase.PExecute("INSERT INTO `fusion_plot_stash` (`plot`, `slot`, `item`, `count`) VALUES (%u, %u, %u, %u)",
            plotId, uint32(i), stash[i].item, stash[i].count);
    CharacterDatabase.CommitTransaction();
}

void FusionMgr::HandleStash(Player* player, F::StashAct const& act)
{
    Piece p;
    if (!player->IsAlive() || !FurniturePieceNear(player, act.piece, F::kUseReach + 1.f, p) || F::UseOf(p.type) != F::Use::Stash)
        return;
    uint32 plot = 0;
    if (PlotAccessAt(player, p, plot) < Plots::Access::Guest || !plot)
        return;
    ChatHandler chat(player);
    if (act.action == F::kStashPut)
    {
        Item* it = player->GetItemByPos(act.a, act.b);
        bool const bagSlot = (act.a == INVENTORY_SLOT_BAG_0 && act.b >= INVENTORY_SLOT_ITEM_START && act.b < INVENTORY_SLOT_ITEM_END)
            || (act.a >= INVENTORY_SLOT_BAG_START && act.a < INVENTORY_SLOT_BAG_END);
        if (!it || !bagSlot || !StorableItem(it) || it->IsInTrade())
        {
            chat.SendSysMessage("That can't go in the stash (bound, enchanted, a quest item or a bag with things in it).");
            return SendStash(player, p);
        }
        uint32 const count = std::min<uint32>(act.count ? act.count : it->GetCount(), it->GetCount());
        uint32 const entry = it->GetEntry();
        uint16 const slots = StashSlotsOf(p.map, plot);
        F::StashResult r;
        std::vector<F::StashEntry> saved;
        {
            std::lock_guard<std::mutex> lock(m_furnitureLock);
            std::vector<F::StashEntry>& stash = m_stashes[plot];
            r = F::Deposit(stash, slots, entry, count, std::max<uint32>(1, it->GetProto()->GetMaxStackSize()));
            if (r == F::StashResult::Ok)
            {
                // Out of the bags under the same lock: nobody takes it out before it has left them.
                uint32 left = count;
                player->DestroyItemCount(it, left, true);
                saved = stash;
            }
        }
        if (r != F::StashResult::Ok)
            chat.SendSysMessage("The stash is full: another chest, footlocker or wardrobe on the plot adds room.");
        else
            SaveStash(plot, saved);
        return SendStash(player, p);
    }
    // Taking out: what fits in the bags.
    size_t const index = size_t(act.a) | size_t(act.b) << 8;
    F::StashEntry want;
    {
        std::lock_guard<std::mutex> lock(m_furnitureLock);
        std::vector<F::StashEntry>& stash = m_stashes[plot];
        if (index >= stash.size())
            return SendStash(player, p);
        want = stash[index];
    }
    uint32 count = std::min<uint32>(act.count ? act.count : want.count, want.count);
    ItemPosCountVec dest;
    uint32 noSpace = 0;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, want.item, count, &noSpace) != EQUIP_ERR_OK)
    {
        count = noSpace < count ? count - noSpace : 0;
        dest.clear();
        if (!count || player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, want.item, count) != EQUIP_ERR_OK)
        {
            chat.SendSysMessage("Your bags are full.");
            return SendStash(player, p);
        }
    }
    F::StashEntry took;
    std::vector<F::StashEntry> saved;
    {
        std::lock_guard<std::mutex> lock(m_furnitureLock);
        std::vector<F::StashEntry>& stash = m_stashes[plot];
        // Still the same stack (someone else may have taken from it meanwhile).
        if (index >= stash.size() || stash[index].item != want.item || F::Withdraw(stash, index, count, took) != F::StashResult::Ok)
            return SendStash(player, p);
        saved = stash;
    }
    SaveStash(plot, saved);
    if (took.count != count)
    {
        dest.clear();
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, took.item, took.count) != EQUIP_ERR_OK)
            return SendStash(player, p);
    }
    player->StoreNewItem(dest, took.item, true, 0);
    SendStash(player, p);
}

void FusionMgr::PackStash(uint32 plotId, std::vector<Piece> const& pieces)
{
    // The stash of a plot packed up goes to the builder of its stash pieces (else of any piece),
    // at the Plot Steward as everything else packed.
    uint32 to = 0;
    for (Piece const& p : pieces)
        if (F::UseOf(p.type) == F::Use::Stash)
        {
            to = p.owner;
            break;
        }
    if (!to && !pieces.empty())
        to = pieces.front().owner;
    if (!to)
        return;
    std::vector<F::StashEntry> stash;
    {
        std::lock_guard<std::mutex> lock(m_furnitureLock);
        auto it = m_stashes.find(plotId);
        if (it == m_stashes.end())
            return;
        stash = std::move(it->second);
        m_stashes.erase(it);
    }
    CharacterDatabase.DirectPExecute("DELETE FROM `fusion_plot_stash` WHERE `plot` = %u", plotId);
    for (F::StashEntry const& e : stash)
        StashReclaim(to, e.item, e.count);
}

bool FusionMgr::FurnitureFocusNear(Player* player, uint32 focus)
{
    if (!focus || !player || !player->GetMap())
        return false;
    std::vector<Piece> const around = PiecesNear(player->GetMapId(), FeetOf(player), F::kStationReach + 6.f);
    return F::StationNear(around, FeetOf(player), focus);
}

float FusionMgr::StageBoost(Player* player)
{
    if (!player || !player->GetMap())
        return 1.f;
    std::vector<Piece> const around = PiecesNear(player->GetMapId(), FeetOf(player), F::kStageReach + 12.f);
    return F::OnStage(around, FeetOf(player)) ? F::kStageBoost : 1.f;
}

void FusionMgr::DropFurnitureProxy(Map* map, uint32 pieceId)
{
    FurnitureProxy px;
    {
        std::lock_guard<std::mutex> lock(m_furnitureLock);
        auto it = m_furnitureProxies.find(pieceId);
        if (it == m_furnitureProxies.end())
            return;
        px = it->second;
        m_furnitureProxies.erase(it);
    }
    if (!map || map->GetId() != px.map)
        return;
    if (px.creature)
    {
        if (Creature* c = map->GetCreature(px.guid))
            c->DespawnOrUnsummon();
    }
    else if (GameObject* go = map->GetGameObject(px.guid))
        go->Delete();
}

void FusionMgr::UpdateFurniture(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    if (int32(now - d.furnitureNextMs) < 0)
        return;
    d.furnitureNextMs = now + kProxyCheckMs;
    Map* map = player->GetMap();
    if (!map || map->Instanceable())
        return;
    Fusion::Vec3 const feet = FeetOf(player);
    std::vector<Piece> const around = PiecesNear(map->GetId(), feet, kProxyNear);

    // Resting at a bed of one's homestead (or one the player holds a permit on), as at an inn.
    bool const rests = F::RestsNear(around, feet, [&](Piece const& bed) {
        uint32 plot = 0;
        return PlotAccessAt(player, bed, plot) >= Plots::Access::Guest && plot != 0;
    });
    if (rests && !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
    {
        player->SetRestType(REST_TYPE_IN_CITY);
        d.furnitureResting = true;
    }
    else if (!rests && d.furnitureResting)
    {
        d.furnitureResting = false;
        if (player->GetRestType() == REST_TYPE_IN_CITY)
            player->SetRestType(REST_TYPE_NO);
        // A capital the player is in still rests them.
        player->UpdateZone(player->GetZoneId(), player->GetAreaId());
    }

    // The stand-ins: a mailbox, a dummy, a trophy's likeness for each such piece near; those
    // whose piece is gone go.
    std::vector<uint32> gone;
    std::vector<Piece> want;
    {
        std::lock_guard<std::mutex> lock(m_furnitureLock);
        for (auto const& kv : m_furnitureProxies)
        {
            if (kv.second.map != map->GetId())
                continue;
            bool const still = std::any_of(around.begin(), around.end(), [&](Piece const& q) { return q.id == kv.first; });
            Fusion::Vec3 const at = kv.second.at;
            if (!still && (at - feet).Length() < kProxyNear - 20.f)
                gone.push_back(kv.first);
        }
        for (Piece const& q : around)
        {
            F::Use const u = F::UseOf(q.type);
            if (u != F::Use::Mailbox && u != F::Use::Dummy && !(u == F::Use::Trophy && m_trophies.count(q.id)))
                continue;
            auto it = m_furnitureProxies.find(q.id);
            if (it == m_furnitureProxies.end())
                want.push_back(q);
            else
            {
                bool const alive = it->second.creature ? map->GetCreature(it->second.guid) != nullptr : map->GetGameObject(it->second.guid) != nullptr;
                if (!alive)
                {
                    m_furnitureProxies.erase(it);
                    want.push_back(q);
                }
            }
        }
    }
    for (uint32 id : gone)
        DropFurnitureProxy(map, id);
    for (Piece const& q : want)
    {
        F::Use const u = F::UseOf(q.type);
        FurnitureProxy px;
        px.map = map->GetId();
        px.at = q.pos;
        if (u == F::Use::Mailbox)
        {
            if (GameObject* go = player->SummonGameObject(kMailboxEntry, q.pos.x, q.pos.y, q.pos.z, q.yaw, 0.f, 0.f, 0.f, 0.f, 0, false))
            {
                px.guid = go->GetObjectGuid();
                px.creature = false;
            }
        }
        else
        {
            uint32 display = 0;
            float scale = 1.f;
            if (u == F::Use::Trophy)
            {
                std::lock_guard<std::mutex> lock(m_furnitureLock);
                auto t = m_trophies.find(q.id);
                if (t == m_trophies.end())
                    continue;
                display = t->second.first;
                scale = t->second.second;
            }
            // A likeness stands on its plinth's top; a dummy on the ground where its piece is.
            F::Def const* def = F::Find(q.type);
            Fusion::Vec3 at = q.pos;
            if (u == F::Use::Trophy && def)
            {
                at = Fusion::Build::Center(q);
                at.z = q.pos.z + (def->top > 0.f ? def->top : def->center.z + def->half.z);
            }
            if (Creature* c = player->SummonCreature(u == F::Use::Dummy ? kDummyEntry : kTrophyEntry, at.x, at.y, at.z, q.yaw, TEMPSUMMON_MANUAL_DESPAWN, 0))
            {
                c->SetRooted(true);
                c->SetWanderDistance(0.f);
                if (u == F::Use::Trophy)
                {
                    // A likeness: nobody's to fight or to pick.
                    c->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                    c->SetFactionTemplateId(35);
                }
                if (display)
                {
                    c->SetDisplayId(display);
                    c->SetNativeDisplayId(display);
                    c->SetObjectScale(scale);
                }
                px.guid = c->GetObjectGuid();
                px.creature = true;
            }
        }
        if (!px.guid.IsEmpty())
        {
            std::lock_guard<std::mutex> lock(m_furnitureLock);
            m_furnitureProxies[q.id] = px;
        }
    }
}
