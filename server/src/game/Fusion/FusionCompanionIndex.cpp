/*
 * World of Modern Warfare: the Companions window (fusion/CompanionIndex.h) in the game. The window
 * is the client's (crate::companion_ui); this file feeds it and carries out what it asks:
 *
 *  - the Bestiary: every species, in full for those this character has seen (targeted a
 *    creature of it) or caught (carried one), the whole of it for a game master; the seen set is
 *    kept in characters.character_fusion_companion_seen, made here at startup;
 *  - the companions in the bags, with their level, stage, traits, abilities and breeding;
 *  - at a stable master (spoken to: the window opens instead of the stock stable or gossip),
 *    breeding two adults and changing a stage's ability;
 *  - a game master's tools, the old Companion Keeper's: any species into the bags at any stage and
 *    level, and any carried companion's level, stage, abilities and traits;
 *  - its lines for the UI error frame (op 32), which replace the companion's chat lines.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "fusion/CompanionIndex.h"
#include "fusion/CompanionKeeper.h"
#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"

#include "Bag.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Item.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "Timer.h"
#include "WorldSession.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace Companions = Fusion::Companions;
namespace Index = Fusion::Companions::Index;
namespace Keeper = Fusion::Companions::Keeper;

namespace
{
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

    std::string CreatureName(uint32 entry)
    {
        CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(entry);
        return ci ? std::string(ci->name) : std::string();
    }

    // A creature's model for the window's preview: its template's first display.
    uint32 CreatureDisplay(uint32 entry)
    {
        CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(entry);
        if (!ci)
            return 0;
        for (uint32 i = 0; i < MAX_DISPLAY_IDS_PER_CREATURE; ++i)
            if (ci->display_id[i])
                return ci->display_id[i];
        return 0;
    }

    std::string ItemName(uint32 entry)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
        return proto ? proto->Name1 : std::string();
    }

    std::string PlaceName(uint32 id)
    {
        AreaEntry const* a = AreaEntry::GetById(id);
        return a ? std::string(a->Name) : std::string();
    }

    // The creature a stage shows (its form's name), or "stage N".
    std::string FormName(Companions::CompanionSpecies const& sp, uint8 stage)
    {
        if (stage < sp.stageCount)
        {
            std::string const n = CreatureName(sp.stages[stage].creature);
            if (!n.empty())
                return n;
        }
        return "stage " + std::to_string(stage + 1);
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

    std::string RefusalText(Keeper::Refusal r, Companions::CompanionSpecies const& sp, uint8 stage, uint32 price)
    {
        using Keeper::Refusal;
        switch (r)
        {
            case Refusal::None: return "";
            case Refusal::BadStage: return sp.name + " has no such stage.";
            case Refusal::BadLevel: return "Companions are level 1 to 60.";
            case Refusal::AboveOwner: return "Not above your own level.";
            case Refusal::StageNeedsLevel:
                return "Its " + FormName(sp, stage) + " form needs level " + std::to_string(Keeper::MinLevelForStage(sp, stage)) + ".";
            case Refusal::Unchanged: return "It is already so.";
            case Refusal::GmOnly: return "Only a game master can do that.";
            case Refusal::NotReached: return "It has not reached that stage.";
            case Refusal::NoAbility: return "There is no such ability.";
            case Refusal::Ineligible: return sp.name + " cannot have that trait.";
            case Refusal::Conflict: return "That trait excludes one it has.";
            case Refusal::TooMany: return "It has three traits already.";
            case Refusal::NotTwin: return "It is not Twin-souled.";
            case Refusal::NoMoney: return "You need " + Money(price) + ".";
        }
        return "";
    }

    // A chat handler's text, its colour codes read for the kind and dropped.
    std::pair<Index::NoticeKind, std::string> Plain(char const* text)
    {
        std::string const s = text ? text : "";
        Index::NoticeKind kind = Index::NoticeKind::Error;
        std::string out;
        for (size_t i = 0; i < s.size();)
        {
            if (s[i] == '|' && i + 1 < s.size() && (s[i + 1] == 'c' || s[i + 1] == 'C') && i + 10 <= s.size())
            {
                std::string const color = s.substr(i + 2, 8);
                if (color == "ffffd200")
                    kind = Index::NoticeKind::Info;
                else if (color.rfind("ffff4", 0) == 0 || color.rfind("ffff2", 0) == 0)
                    kind = Index::NoticeKind::Error;
                else
                    kind = Index::NoticeKind::Good;
                i += 10;
                continue;
            }
            if (s[i] == '|' && i + 1 < s.size() && (s[i + 1] == 'r' || s[i + 1] == 'R'))
            {
                i += 2;
                continue;
            }
            out += s[i++];
        }
        return {kind, out};
    }
}

// ── Lines for the UI error frame ────────────────────────────────────────────────────────────

void FusionCompanionTell::SendSysMessage(char const* text) const
{
    if (!player)
        return;
    std::pair<Index::NoticeKind, std::string> const p = Plain(text);
    sFusionMgr.CompanionNotice(player, p.first, p.second);
}

void FusionCompanionTell::PSendSysMessage(char const* format, ...) const
{
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    SendSysMessage(buf);
}

void FusionMgr::CompanionNotice(Player* player, Index::NoticeKind kind, std::string const& text) const
{
    if (!player || !player->GetSession() || text.empty())
        return;
    SendChannel(player, Companions::kChannelCompanion, Index::EncodeNotice(kind, text));
}

// ── The seen set ────────────────────────────────────────────────────────────────────────────

void FusionMgr::LoadCompanionIndex(Player* player)
{
    if (!player->HasFusion())
        return;
    FusionPlayerData& d = player->GetFusion();
    d.companionSeen.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `species`, `captured` FROM `character_fusion_companion_seen` WHERE `guid` = %u",
            player->GetGUIDLow()))
    {
        do
        {
            Field* f = r->Fetch();
            Index::Record(d.companionSeen, f[0].GetUInt16(), f[1].GetUInt8() ? Index::Seen::Captured : Index::Seen::Seen);
        } while (r->NextRow());
    }
    d.companionSeenLoaded = true;
    // The companion out at logout comes back once the character is in the world
    // (FusionMgr::UpdateCompanionReturn).
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `item_guid` FROM `character_fusion_companion_out` WHERE `guid` = %u", player->GetGUIDLow()))
        d.companion.returnItem = ObjectGuid(HIGHGUID_ITEM, r->Fetch()[0].GetUInt32());
}

void FusionMgr::RecordCompanionSeen(Player* player, uint16 species, Index::Seen how)
{
    if (!player->HasFusion())
        return;
    FusionPlayerData& d = player->GetFusion();
    if (!d.companionSeenLoaded)
        return;
    Index::Seen const was = Index::Of(d.companionSeen, species);
    if (!Index::Record(d.companionSeen, species, how))
        return;
    CharacterDatabase.PExecute(
        "REPLACE INTO `character_fusion_companion_seen` (`guid`, `species`, `captured`) VALUES (%u, %u, %u)",
        player->GetGUIDLow(), uint32(species), how == Index::Seen::Captured ? 1u : 0u);
    // A species caught for the first time: the achievements count it.
    if (how == Index::Seen::Captured && was != Index::Seen::Captured)
        CountStat(player, {Fusion::Stat::CompanionsCaught, Fusion::kAnyKey});
    Companions::CompanionSpecies const* sp = Companions::FindSpecies(m_companionSpecies, species);
    if (!sp)
        return;
    bool const gm = IsAdmin(player);
    // The entry in full now (or marked caught).
    Index::Entry const e = Index::MakeEntry(*sp, d.companionSeen, gm, CreatureName, ItemName, PlaceName);
    SendChannel(player, Companions::kChannelCompanion, Index::EncodeIndex(gm ? Index::INDEX_GM : 0, {e}));
    if (was == Index::Seen::Unknown)
        SendChannel(player, Companions::kChannelCompanion,
            Index::EncodeLooks({Index::MakeLooks(*sp, CreatureDisplay, CreatureName)}));
    if (was == Index::Seen::Unknown && !gm)
        CompanionNotice(player, Index::NoticeKind::Info, "New Bestiary entry: " + sp->name + ".");
}

void FusionMgr::OnCompanionTarget(Player* player, Unit* target)
{
    if (!m_enabled || !player || !player->HasFusion() || !target || !target->IsCreature())
        return;
    auto it = m_companionCreatures.find(target->GetEntry());
    if (it == m_companionCreatures.end())
        return;
    // Someone's companion: its own species, whatever else shares its model.
    if (Player* owner = CompanionOwner(target))
        if (Item* item = owner->GetItemByGuid(owner->GetFusion().companion.item))
            if (Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry()))
            {
                RecordCompanionSeen(player, sp->id, Index::Seen::Seen);
                return;
            }
    for (uint16 species : it->second)
        RecordCompanionSeen(player, species, Index::Seen::Seen);
}

// ── The window ──────────────────────────────────────────────────────────────────────────────

bool FusionMgr::AtCompanionStable(Player* player) const
{
    if (!player->HasFusion())
        return false;
    ObjectGuid const guid = player->GetFusion().companionStable;
    return !guid.IsEmpty() && player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_STABLEMASTER) != nullptr;
}

bool FusionMgr::CompanionStableHello(Player* player, Creature* creature)
{
    if (!m_enabled || !player->HasFusion() || !creature || !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_STABLEMASTER))
        return false;
    player->PlayerTalkClass->CloseGossip();
    player->GetFusion().companionStable = creature->GetObjectGuid();
    // A companion fainted or hurt: the stable master's options first (revive and heal, a
    // Reviving Draught, the window; FusionCompanionRevive.cpp).
    if (CompanionStableGossip(player, creature))
        return true;
    // The client opens the window and asks for the index and the carried list.
    SendChannel(player, Companions::kChannelCompanion, Index::EncodeOpen(Index::Access::Npc));
    return true;
}

void FusionMgr::SendCompanionIndex(Player* player)
{
    if (!player->HasFusion())
        return;
    bool const gm = IsAdmin(player);
    FusionPlayerData& d = player->GetFusion();
    // What is in the bags is caught.
    ForEachBagItem(player, [&](Item* item) {
        if (Companions::IsCompanionItem(item->GetEntry()))
            if (Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry()))
                RecordCompanionSeen(player, sp->id, Index::Seen::Captured);
    });
    std::vector<Index::Entry> entries;
    entries.reserve(m_companionSpecies.size());
    for (Companions::CompanionSpecies const& sp : m_companionSpecies)
        entries.push_back(Index::MakeEntry(sp, d.companionSeen, gm, CreatureName, ItemName, PlaceName));
    for (std::vector<uint8_t> const& page : Index::IndexPages(entries, gm))
        SendChannel(player, Companions::kChannelCompanion, page);
    // The Bestiary's models, move icons and where each is found, for what the reader may see.
    std::vector<Index::Looks> looks;
    for (Companions::CompanionSpecies const& sp : m_companionSpecies)
        if (Index::Visible(d.companionSeen, sp.id, gm))
            looks.push_back(Index::MakeLooks(sp, CreatureDisplay, CreatureName));
    for (std::vector<uint8_t> const& page : Index::LooksPages(looks))
        SendChannel(player, Companions::kChannelCompanion, page);
    SendCompanionCarried(player);
}

void FusionMgr::SendCompanionCarried(Player* player)
{
    if (!player->HasFusion() || !player->GetSession())
        return;
    FusionCompanionOut const& c = player->GetFusion().companion;
    uint32 const now = uint32(time(nullptr));
    std::vector<Index::CarriedEntry> list;
    Index::CarriedCare care;
    ForEachBagItem(player, [&](Item* item) {
        if (!Companions::IsCompanionItem(item->GetEntry()))
            return;
        Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry());
        if (!sp)
            return;
        Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
        s.species = sp->id;
        s.stage = std::min<uint8>(s.stage, uint8(sp->stageCount - 1));
        bool const out = c.item == item->GetObjectGuid() && (!c.pet.IsEmpty() || c.riding);
        Companions::EvolveContext ctx = CompanionContext(player, *sp, s);
        ctx.summoned = ctx.summoned && out;
        Companions::EvolveBlock const block = Companions::CanEvolve(*sp, s, ctx);
        Companions::EvolutionRule const* rule = Companions::NextRule(*sp, s);
        Index::CarriedEntry e;
        e.item = item->GetObjectGuid().GetRawValue();
        e.species = sp->id;
        e.stage = s.stage;
        e.level = s.level;
        e.xp = s.xp;
        e.xpToNext = Companions::XpToNext(s.level);
        e.traits = s.traits;
        e.rarity = s.rarity;
        e.twinStage = s.twinStage;
        e.picks = s.picks;
        e.pending = int8(Companions::PendingPick(*sp, s));
        if (out)
            e.flags |= Index::CARRIED_OUT;
        if (out && c.riding)
            e.flags |= Index::CARRIED_RIDING;
        if (block == Companions::EvolveBlock::None)
            e.flags |= Index::CARRIED_CAN_EVOLVE;
        if (!rule)
            e.flags |= Index::CARRIED_FINAL;
        if (Companions::IsRideable(*sp, s))
            e.flags |= Index::CARRIED_RIDEABLE;
        if (Companions::IsCapped(s, uint8(player->GetLevel())))
            e.flags |= Index::CARRIED_CAPPED;
        // Fainted, or hurt (fusion/CompanionRest.h): the one out by its health now.
        Companions::Recovery const rec = SettleCompanion(player, item, true);
        uint32 const wait = CompanionFaintWait(item);
        e.faintSeconds = out ? 0 : Companions::FaintSecondsLeft(rec, now, wait);
        float health = Companions::HealthAt(rec, now, wait);
        if (out)
        {
            Pet* pet = CompanionPet(player);
            health = c.riding ? c.rideHealth
                : pet && pet->GetMaxHealth() ? float(pet->GetHealth()) / float(pet->GetMaxHealth()) : 1.f;
        }
        e.healthPct = uint8(std::min(100.f, std::max(0.f, health * 100.f + 0.5f)));
        if (e.faintSeconds)
        {
            e.flags |= Index::CARRIED_FAINTED;
            e.healthPct = 0;
            care.reviveCost += Companions::StableReviveCost(s.level, m_faint.costPerLevel);
            ++care.needsCare;
        }
        else if (health < 0.995f)
            ++care.needsCare;
        e.seed = Companions::ItemSeed(s);
        e.block = uint8(block);
        e.nextEvolveLevel = rule ? rule->level : 0;
        e.breedCount = s.breedCount;
        e.breedRestSeconds = s.breedReadyAt > now ? s.breedReadyAt - now : 0;
        e.generation = s.generation;
        list.push_back(e);
    });
    uint8 header = 0;
    if (IsAdmin(player))
        header |= Index::CARRIED_GM;
    if (AtCompanionStable(player))
        header |= Index::CARRIED_AT_STABLE;
    care.draughts = uint8(std::min<uint32>(player->GetItemCount(Companions::kReviveDraughtItem), 255));
    if (IsAdmin(player))
        care.reviveCost = 0;
    SendChannel(player, Companions::kChannelCompanion, Index::EncodeCarried(header, list, care));
}

void FusionMgr::CommitCompanion(Player* player, Item* item, Companions::CompanionState const& s, bool resummon)
{
    SaveCompanionState(item->GetGUIDLow(), s);
    StampCompanionItem(item);
    FusionCompanionOut const& c = player->GetFusion().companion;
    bool const out = c.item == item->GetObjectGuid() && (!c.pet.IsEmpty() || c.riding);
    if (out)
    {
        Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry());
        if (!resummon && sp)
        {
            if (Pet* pet = CompanionPet(player))
                SetupCompanionBar(player, pet, *sp, s);
            SendCompanionMoves(player);
        }
        else
        {
            DismissCompanion(player, false, false);
            if (!SummonCompanion(player, item))
            {
                SendCompanionBar(player, true);
                SendCompanionMoves(player);
            }
        }
    }
    SendCompanionCarried(player);
}

void FusionMgr::RepickCompanionAbility(Player* player, uint64 itemGuid, uint8 stage, uint8 option)
{
    Item* item = player->GetItemByGuid(ObjectGuid(itemGuid));
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp || !Player::IsInventoryPos(item->GetPos()))
        return CompanionNotice(player, Index::NoticeKind::Error, "That companion must be in your bags.");
    if (item->IsInTrade())
        return CompanionNotice(player, Index::NoticeKind::Error, "Not while it is in a trade window.");
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    s.species = sp->id;
    Keeper::Customer who;
    who.level = uint8(player->GetLevel());
    who.gm = IsAdmin(player);
    who.money = player->GetMoney();
    uint32 const price = Keeper::PickPrice(s, stage);
    Keeper::Refusal const r = Keeper::CanPick(*sp, s, stage, option, who);
    if (r != Keeper::Refusal::None || !Keeper::ApplyPick(*sp, s, stage, option))
    {
        CompanionNotice(player, Index::NoticeKind::Error, RefusalText(r, *sp, stage, price));
        return;
    }
    if (!who.gm && price)
        player->ModifyMoney(-int32(price));
    CommitCompanion(player, item, s, false);
    Companions::Ability const& a = Companions::MovesetData(Companions::MovesetOf(*sp)).stages[stage][option - 1];
    CompanionNotice(player, Index::NoticeKind::Good, "Your " + sp->name + " learned " + a.name + ".");
}

void FusionMgr::CompanionGmEdit(Player* player, Index::Request const& r)
{
    Item* item = player->GetItemByGuid(ObjectGuid(r.item));
    Companions::CompanionSpecies const* sp = item && Companions::IsCompanionItem(item->GetEntry())
        ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp || !Player::IsInventoryPos(item->GetPos()) || item->IsInTrade())
        return;
    Keeper::Customer who;
    who.level = uint8(player->GetLevel());
    who.gm = true;
    who.money = player->GetMoney();
    FusionCompanionOut const& c = player->GetFusion().companion;
    if (c.item == item->GetObjectGuid() && CompanionDuelOf(player->GetObjectGuid().GetRawValue()))
    {
        CompanionNotice(player, Index::NoticeKind::Error, "Not during a companion duel.");
        return;
    }
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    s.species = sp->id;
    s.stage = std::min<uint8>(s.stage, uint8(sp->stageCount - 1));
    Keeper::Refusal refusal = Keeper::Refusal::None;
    bool resummon = true;
    std::string done;
    switch (r.what)
    {
        case Index::Edit::Level:
        case Index::Edit::Stage:
        {
            uint8 const stage = r.what == Index::Edit::Stage ? r.a : s.stage;
            uint8 const level = r.what == Index::Edit::Level ? r.a : s.level;
            refusal = Keeper::CanChange(*sp, s, stage, level, who);
            if (refusal == Keeper::Refusal::None)
            {
                Keeper::ApplyChange(*sp, s, stage, level);
                done = "Your " + sp->name + " is now level " + std::to_string(s.level) + ", " + FormName(*sp, s.stage) + ".";
            }
            break;
        }
        case Index::Edit::Pick:
            refusal = Keeper::CanPick(*sp, s, r.a, r.b, who);
            if (refusal == Keeper::Refusal::None && Keeper::ApplyPick(*sp, s, r.a, r.b))
            {
                resummon = false;
                done = "Your " + sp->name + " learned "
                    + Companions::MovesetData(Companions::MovesetOf(*sp)).stages[r.a][r.b - 1].name + ".";
            }
            break;
        case Index::Edit::Trait:
        {
            if (r.a < 1 || r.a > Companions::kTraitCount)
                return;
            Companions::Trait const t = Companions::Trait(r.a);
            bool const removing = Companions::HasTrait(s.traits, t);
            refusal = Keeper::CanToggleTrait(*sp, s, t, who);
            if (refusal == Keeper::Refusal::None)
            {
                Keeper::ApplyToggleTrait(*sp, s, t);
                done = "Your " + sp->name + (removing ? " lost " : " gained ") + Companions::TraitData(t).name + ".";
            }
            break;
        }
        case Index::Edit::Twin:
            refusal = Keeper::CanSetTwinStage(*sp, s, r.a, who);
            if (refusal == Keeper::Refusal::None)
            {
                s.twinStage = r.a;
                done = "Twin-souled now doubles stage " + std::to_string(r.a) + ".";
            }
            break;
        case Index::Edit::ClearTraits:
            s.traits = 0;
            s.rarity = 0;
            s.twinStage = 0;
            done = "Your " + sp->name + " has no traits now.";
            break;
    }
    if (refusal != Keeper::Refusal::None || done.empty())
    {
        CompanionNotice(player, Index::NoticeKind::Error, RefusalText(refusal, *sp, r.a, 0));
        SendCompanionCarried(player);
        return;
    }
    CommitCompanion(player, item, s, resummon);
    CompanionNotice(player, Index::NoticeKind::Good, done);
}

void FusionMgr::HandleCompanionWindow(Player* player, Index::Request const& r)
{
    bool const gm = IsAdmin(player);
    Index::Access const at = AtCompanionStable(player) ? Index::Access::Npc : Index::Access::Hud;
    switch (r.op)
    {
        case Index::kOpIndexRequest:
        {
            // The whole index is many pages: once a second at most (the carried list is sent anyway).
            FusionCompanionOut& c = player->GetFusion().companion;
            uint32 const now = WorldTimer::getMSTime();
            if (c.indexAskedMs && now - c.indexAskedMs < 1000)
                return SendCompanionCarried(player);
            c.indexAskedMs = now ? now : 1;
            SendCompanionIndex(player);
            return;
        }
        case Index::kOpCarriedRequest:
            SendCompanionCarried(player);
            return;
        case Index::kOpGmGive:
        {
            if (!Index::Allowed(Index::Action::GmGive, at, gm))
                return CompanionNotice(player, Index::NoticeKind::Error, "Only a game master can do that.");
            Companions::CompanionSpecies const* sp = Companions::FindSpecies(m_companionSpecies, r.species);
            if (!sp)
                return;
            Keeper::Customer who;
            who.level = uint8(player->GetLevel());
            who.gm = true;
            Keeper::Refusal const refusal = Keeper::CanBuy(*sp, r.stage, r.level, who);
            if (refusal != Keeper::Refusal::None)
                return CompanionNotice(player, Index::NoticeKind::Error, RefusalText(refusal, *sp, r.stage, 0));
            if (!GiveCompanion(player, sp->id, r.level, r.stage))
                return CompanionNotice(player, Index::NoticeKind::Error, "Your bags are full (or its item is missing).");
            RecordCompanionSeen(player, sp->id, Index::Seen::Captured);
            CompanionNotice(player, Index::NoticeKind::Good, "A level " + std::to_string(r.level) + " " + sp->name + " ("
                + FormName(*sp, r.stage) + ") is in your bags.");
            SendCompanionCarried(player);
            return;
        }
        case Index::kOpGmEdit:
            if (!Index::Allowed(Index::Action::GmEdit, at, gm))
                return CompanionNotice(player, Index::NoticeKind::Error, "Only a game master can do that.");
            CompanionGmEdit(player, r);
            return;
        case Index::kOpBreed:
        {
            if (!Index::Allowed(Index::Action::Breed, at, gm))
                return CompanionNotice(player, Index::NoticeKind::Error, "Companions are bred at a stable master.");
            Item* a = player->GetItemByGuid(ObjectGuid(r.item));
            Item* b = player->GetItemByGuid(ObjectGuid(r.other));
            if (!a || !b || !Companions::IsCompanionItem(a->GetEntry()) || !Companions::IsCompanionItem(b->GetEntry())
                || !Player::IsInventoryPos(a->GetPos()) || !Player::IsInventoryPos(b->GetPos()))
                return CompanionNotice(player, Index::NoticeKind::Error, "Both companions must be in your bags.");
            if (a->IsInTrade() || b->IsInTrade())
                return CompanionNotice(player, Index::NoticeKind::Error, "Not while it is in a trade window.");
            if (CompanionFaintLeft(player, a) || CompanionFaintLeft(player, b))
                return CompanionNotice(player, Index::NoticeKind::Error, "A fainted companion can't breed: revive it first.");
            BreedCompanions(player, a, b, false);
            SendCompanionCarried(player);
            return;
        }
        case Index::kOpRepick:
            if (!Index::Allowed(Index::Action::Repick, at, gm))
                return CompanionNotice(player, Index::NoticeKind::Error, "A stable master changes a companion's abilities.");
            RepickCompanionAbility(player, r.item, r.a, r.b);
            return;
        // Reviving (FusionCompanionRevive.cpp).
        case Index::kOpStableRevive:
            if (!Index::Allowed(Index::Action::StableCare, at, gm))
                return CompanionNotice(player, Index::NoticeKind::Error, "A stable master revives companions: talk to one.");
            ReviveAtStable(player);
            return;
        case Index::kOpBuyDraught:
            if (!Index::Allowed(Index::Action::StableCare, at, gm))
                return CompanionNotice(player, Index::NoticeKind::Error, "Stable masters sell Reviving Draughts: talk to one.");
            BuyReviveDraught(player);
            return;
        case Index::kOpUseDraught:
            UseReviveDraught(player, r.item);
            return;
        default:
            return;
    }
}

// ── Riding ──────────────────────────────────────────────────────────────────────────────────

uint32 FusionMgr::CompanionMountDisplay(Player* player, uint32 spellId) const
{
    if (!m_enabled || !player || !player->HasFusion())
        return 0;
    FusionCompanionOut const& c = player->GetFusion().companion;
    return c.riding && c.rideSpell == spellId ? c.rideDisplay : 0;
}
