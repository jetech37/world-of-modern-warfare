/*
 * World of Modern Warfare: Utility's tools (fusion/Gathering.h) and the upkeep of built pieces
 * (fusion/Maintenance.h) in the game. The client readies a tool in Utility (channel 10 op 2) and
 * strikes with it (op 3); the server counts the strikes on what the tool works and, on the last,
 * runs WoW's own gathering for the player: the open-lock spell at an ore vein or a herb, the
 * skinning spell at a corpse, the fishing spell (and the bobber's use to reel in), the lumber
 * node's use, or the Repair Hammer's step of repair on a built piece. Each strike is answered
 * (op 4) so the client can show the progress. Decay brings each map's pieces up to date once an
 * hour (FusionConstruction.cpp's player update calls DecayPieces).
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "Bag.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <cinttypes>
#include <ctime>
#include <sstream>

namespace
{
    using namespace Fusion::Gathering;
    namespace AS = Fusion::ActionSystems;
    namespace Maint = Fusion::Maintenance;

    int64 Now() { return int64(time(nullptr)); }

    // Every item the player carries: equipped, in the backpack and in the bags.
    template <typename F>
    void ForEachCarried(Player const* player, F&& f)
    {
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (f(item))
                    return;
        for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
            if (Bag const* bag = dynamic_cast<Bag const*>(player->GetItemByPos(INVENTORY_SLOT_BAG_0, b)))
                for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                    if (Item const* item = bag->GetItemByPos(uint8(j)))
                        if (f(item))
                            return;
    }

    // A known spell of the player's that has an effect `effect` (and, for an open-lock spell, its
    // lock type as the effect's misc value; for fishing, the fishing spot as its target).
    uint32 KnownSpellWith(Player const* player, uint32 effect, int32 misc, bool fishing)
    {
        uint32 found = 0;
        for (auto const& kv : player->GetSpellMap())
        {
            if (kv.second.state == PLAYERSPELL_REMOVED || kv.second.disabled)
                continue;
            SpellEntry const* e = sSpellMgr.GetSpellEntry(kv.first);
            if (!e)
                continue;
            for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (e->Effect[i] != effect)
                    continue;
                if (fishing && e->EffectImplicitTargetA[i] != TARGET_LOCATION_CASTER_FISHING_SPOT)
                    continue;
                if (!fishing && misc >= 0 && e->EffectMiscValue[i] != misc)
                    continue;
                // The highest id of those that fit: a later rank.
                found = std::max(found, kv.first);
            }
        }
        return found;
    }

    // A gameobject's lock asking a gathering skill: the lock type (Herbalism, Mining) and the skill
    // it asks; false when it asks none.
    bool GatheringLock(GameObject const* go, uint32& lockType, uint16& required)
    {
        LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
        if (!lock)
            return false;
        for (int j = 0; j < MAX_LOCK_CASE; ++j)
            if (lock->Type[j] == LOCK_KEY_SKILL && (lock->Index[j] == LOCKTYPE_MINING || lock->Index[j] == LOCKTYPE_HERBALISM))
            {
                lockType = lock->Index[j];
                required = uint16(lock->Skill[j]);
                return true;
            }
        return false;
    }

    // What a tool's outcome says when it is a refusal (the server's own notification line).
    char const* RefusalText(Outcome o)
    {
        switch (o)
        {
            case Outcome::NoTarget: return "Nothing there to work.";
            case Outcome::TooFar: return "Too far away.";
            case Outcome::WrongTarget: return "That tool doesn't work that.";
            case Outcome::NotDamaged: return "It needs no repair.";
            case Outcome::RecentlyDamaged: return "It was hit moments ago: it can't be mended under fire.";
            case Outcome::NotYours: return "You may not work on that.";
            default: return nullptr;
        }
    }
}

uint32 FusionMgr::ToolItemOf(Player const* player, uint32 tool) const
{
    ToolDef const* t = ToolInfo(tool);
    if (!t)
        return 0;
    uint32 found = 0;
    if (t->ability == Ability::FishingPole)
    {
        // Any pole: the one in hand first, else the first in the bags.
        ForEachCarried(player, [&](Item const* item) {
            ItemPrototype const* proto = item->GetProto();
            if (proto && proto->Class == kWeaponClass && proto->SubClass == kFishingPoleSubclass)
            {
                found = proto->ItemId;
                return true;
            }
            return false;
        });
        return found;
    }
    if (!t->item)
        return 0;
    return player->HasItemCount(t->item, 1) ? t->item : 0;
}

Holder FusionMgr::ToolHolder(Player const* player, ToolDef const& tool) const
{
    FusionPlayerData const& d = player->GetFusionConst();
    Holder h;
    h.gm = IsAdmin(player);
    h.hasItem = ToolItemOf(player, uint32(tool.ability)) != 0;
    if (tool.skill == kSkillConstruction)
        h.learned = d.constructionTier != Fusion::Construction::Tier::None;
    else if (tool.skill == kSkillMinstrelsy)
        h.learned = sMinstrelMgr.Knows(player, uint32(tool.ability));     // and the instrument's skill
    else
        h.learned = !tool.skill || player->HasSkill(tool.skill);
    return h;
}

void FusionMgr::UpdateUtilityTools(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    if (d.utilityToolsSent != 0xFFFF && now - d.utilityToolsCheckMs < 1000)
        return;
    d.utilityToolsCheckMs = now;
    uint16 const mask = UtilityBarTools([&](ToolDef const& t) { return ToolHolder(player, t); });
    if (mask == d.utilityToolsSent)
        return;
    d.utilityToolsSent = mask;
    SendChannel(player, AS::kChannel, AS::EncodeTools(mask));
}

void FusionMgr::SetUtilityTool(Player* player, uint32 tool)
{
    FusionPlayerData& d = player->GetFusion();
    if (d.utilityTool == tool)
        return;
    d.utilityTool = tool;
    Fusion::Gathering::ToolChanged(d.toolProgress);
    SendMeleeProfile(player);
}

void FusionMgr::ToolStrike(Player* player, AS::StrikeRequest const& r)
{
    FusionPlayerData& d = player->GetFusion();
    ToolDef const* tool = ToolInfo(r.tool);
    if (!tool || !player->IsAlive() || d.actionSystem != AS::System::Utility)
        return;
    // The client readied this tool (op 2 may have been lost: the hands follow the strike).
    if (d.utilityTool != r.tool)
        SetUtilityTool(player, r.tool);
    auto answer = [&](Outcome o, uint8 done = 0, uint8 needed = 0) {
        if (char const* why = RefusalText(o))
            player->GetSession()->SendNotification("%s", why);
        SendChannel(player, AS::kChannel, AS::EncodeStruck(r.tool, uint8(o), done, needed));
    };
    bool const gm = IsAdmin(player);
    Holder const h = ToolHolder(player, *tool);
    Outcome const ok = CheckTool(*tool, h);
    if (ok == Outcome::NoTool)
    {
        player->GetSession()->SendNotification("Requires %s", tool->ability == Ability::FishingPole ? "a fishing pole" : tool->name);
        return answer(ok);
    }
    if (ok == Outcome::NotLearned)
    {
        char const* trade = tool->skill == kSkillMining ? "Mining" : tool->skill == kSkillHerbalism ? "Herbalism"
            : tool->skill == kSkillSkinning ? "Skinning" : tool->skill == kSkillFishing ? "Fishing"
            : tool->skill == kSkillBlacksmithing ? "Blacksmithing" : tool->skill == kSkillMinstrelsy ? "Minstrelsy"
            : "Construction";
        player->GetSession()->SendNotification("Requires %s", trade);
        return answer(ok);
    }
    if (tool->work == Work::Piece)
        return RepairStrike(player, r);
    // An instrument: the strike's target is the song (FusionMinstrelsy.cpp).
    if (tool->work == Work::Song)
        return answer(Outcome(sMinstrelMgr.Perform(player, r.tool, uint16(std::min<uint64>(r.target, 0xFFFF)))), 1, 1);

    uint32 const nowMs = WorldTimer::getMSTime();
    Map* map = player->GetMap();

    // ── Fishing: the line out, or the bobber in ──
    if (tool->work == Work::Water)
    {
        Step const step = Fusion::Gathering::Strike(d.toolProgress, r.tool, 0, 1, nowMs);
        if (step == Step::TooSoon)
            return answer(Outcome::TooSoon);
        if (GameObject* bobber = map->GetGameObject(player->GetChannelObjectGuid()))
            if (bobber->GetGoType() == GAMEOBJECT_TYPE_FISHINGNODE && bobber->GetOwnerGuid() == player->GetObjectGuid())
            {
                // WoW's own use of the bobber: the catch when it bobs, "got away" when it doesn't.
                bobber->Use(player);
                return answer(Outcome::Reeled, 1, 1);
            }
        uint32 const spell = KnownSpellWith(player, SPELL_EFFECT_TRANS_DOOR, -1, true);
        if (!spell)
        {
            player->GetSession()->SendNotification("Requires Fishing");
            return answer(Outcome::NotLearned);
        }
        // Cast for the player (the pole is the hands' tool, in the bags or equipped).
        return answer(player->CastSpell(player, spell, true) == SPELL_CAST_OK ? Outcome::Cast : Outcome::Failed, 1, 1);
    }

    // ── A corpse to skin ──
    if (tool->work == Work::Corpse)
    {
        ObjectGuid const guid(r.target);
        Creature* c = guid.IsCreatureOrPet() ? map->GetAnyTypeCreature(guid) : nullptr;
        if (!c)
            return answer(Outcome::NoTarget);
        if (c->IsAlive() || !c->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))
            return answer(Outcome::WrongTarget);
        if (!player->IsWithinDistInMap(c, kReach + c->GetObjectBoundingRadius()))
            return answer(Outcome::TooFar);
        uint16 const skill = player->GetSkillValue(SKILL_SKINNING);
        uint16 const required = SkinningRequired(c->GetLevel(), skill);
        if (skill < required && !gm)
        {
            player->GetSession()->SendNotification("Requires Skinning %u", uint32(required));
            return answer(Outcome::Failed);
        }
        uint8 const needed = StrikesFor(r.tool, required, skill);
        Step const step = Fusion::Gathering::Strike(d.toolProgress, r.tool, r.target, needed, nowMs);
        if (step == Step::TooSoon)
            return answer(Outcome::TooSoon);
        if (step == Step::Counted)
            return answer(Outcome::Struck, d.toolProgress.done, needed);
        uint32 const spell = KnownSpellWith(player, SPELL_EFFECT_SKINNING, -1, false);
        if (!spell)
            return answer(Outcome::NotLearned);
        // Vanilla's skinning, its checks and loot and skill-up (Spell::CheckCast, EffectSkinning).
        return answer(player->CastSpell(c, spell, true) == SPELL_CAST_OK ? Outcome::Gathered : Outcome::Failed, needed, needed);
    }

    // ── A gameobject: a vein, a herb, a lumber tree, an anvil ──
    ObjectGuid const guid(r.target);
    GameObject* go = guid.IsGameObject() ? map->GetGameObject(guid) : nullptr;
    if (!go || !go->isSpawned())
        return answer(Outcome::NoTarget);
    if (!go->IsAtInteractDistance(player, uint32(kReach)))
        return answer(Outcome::TooFar);
    GameObjectInfo const* info = go->GetGOInfo();
    Work work = Work::None;
    uint32 lockType = 0;
    uint16 required = 0;
    uint16 skill = 0;
    Fusion::Construction::NodeDef const* lumber = Fusion::Construction::NodeFor(go->GetEntry());
    if (lumber)
    {
        work = Work::Lumber;
        required = lumber->skill.required;
        skill = d.constructionSkill;
    }
    else if (GatheringLock(go, lockType, required))
    {
        work = lockType == LOCKTYPE_MINING ? Work::Vein : Work::Herb;
        skill = player->GetSkillValue(lockType == LOCKTYPE_MINING ? SKILL_MINING : SKILL_HERBALISM);
    }
    else if (info->type == GAMEOBJECT_TYPE_SPELL_FOCUS && (info->spellFocus.focusId == 1 || info->spellFocus.focusId == 3))
        work = Work::Anvil;     // an anvil (focus 1) or a forge (3)
    if (!Fits(r.tool, work))
        return answer(Outcome::WrongTarget);
    if (work == Work::Anvil)
    {
        Step const step = Fusion::Gathering::Strike(d.toolProgress, r.tool, r.target, 1, nowMs);
        if (step == Step::TooSoon)
            return answer(Outcome::TooSoon);
        // The client opens Blacksmithing's own window; its recipes keep their own anvil checks.
        return answer(Outcome::Opened, 1, 1);
    }
    if (skill < required && !gm)
    {
        player->GetSession()->SendNotification("Requires %s %u", work == Work::Vein ? "Mining" : work == Work::Herb ? "Herbalism"
            : "Construction", uint32(required));
        return answer(Outcome::Failed);
    }
    uint8 const needed = StrikesFor(r.tool, required, skill);
    Step const step = Fusion::Gathering::Strike(d.toolProgress, r.tool, r.target, needed, nowMs);
    if (step == Step::TooSoon)
        return answer(Outcome::TooSoon);
    if (step == Step::Counted)
        return answer(Outcome::Struck, d.toolProgress.done, needed);
    if (work == Work::Lumber)
    {
        // The lumber node's own use: the axe, the skill, its loot and its skill-up.
        UseLumberNode(player, go);
        return answer(Outcome::Gathered, needed, needed);
    }
    uint32 const spell = KnownSpellWith(player, SPELL_EFFECT_OPEN_LOCK, int32(lockType), false);
    if (!spell)
        return answer(Outcome::NotLearned);
    // WoW's own gathering, cast for the player: the lock's skill check, the loot window, the
    // node's depletion and one skill-up a node (Spell::EffectOpenLock).
    return answer(player->CastSpell(go, spell, true) == SPELL_CAST_OK ? Outcome::Gathered : Outcome::Failed, needed, needed);
}

int64 FusionMgr::KeptUntil(Fusion::Build::Piece const& piece)
{
    uint32 const plot = Fusion::Plots::PlotOf(PlotGrid(), piece);
    if (!plot)
        return 0;
    std::lock_guard<std::mutex> lock(m_plotLock);
    Fusion::Plots::Plot const* p = m_plots.Find(plot);
    return p && p->Owned() ? p->paidUntil : 0;
}

void FusionMgr::RepairStrike(Player* player, AS::StrikeRequest const& r)
{
    using Fusion::Build::Piece;
    FusionPlayerData& d = player->GetFusion();
    auto answer = [&](Outcome o, uint8 done = 0, uint8 needed = 0) {
        if (char const* why = RefusalText(o))
            player->GetSession()->SendNotification("%s", why);
        SendChannel(player, AS::kChannel, AS::EncodeStruck(r.tool, uint8(o), done, needed));
    };
    Map* map = player->GetMap();
    uint32 const id = uint32(r.target);
    Piece found;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(map->GetId());
        if (it == m_pieces.end())
            return answer(Outcome::NoTarget);
        auto pit = std::find_if(it->second.begin(), it->second.end(), [id](Piece const& p) { return p.id == id; });
        if (pit == it->second.end())
            return answer(Outcome::NoTarget);
        found = *pit;
    }
    Fusion::Vec3 const eye(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() + 1.6f);
    if (Fusion::Build::Distance(found, eye) > kReach + 1.f)
        return answer(Outcome::TooFar);
    // The pieces one may take down (the builder, a plot's household, a GM), and the builder's
    // party: a friend may mend a wall with their own goods.
    bool allowed = MayRemovePiece(player, found);
    if (!allowed && found.owner)
        if (Group* g = player->GetGroup())
            allowed = g->IsMember(ObjectGuid(HIGHGUID_PLAYER, found.owner));
    if (!allowed)
        return answer(Outcome::NotYours);
    uint32 const nowMs = WorldTimer::getMSTime();
    if (Fusion::Gathering::Strike(d.toolProgress, r.tool, r.target, 1, nowMs) == Step::TooSoon)
        return answer(Outcome::TooSoon);
    // Up to date first: what time has taken since the last sweep is mended too.
    int64 const now = Now();
    int64 const kept = KeptUntil(found);
    Maint::Decayed const decayed = Maint::Decay(found.health, Fusion::Build::MaxHealth(found), found.decayedAt, now, kept);
    found.health = decayed.health;
    found.decayedAt = decayed.decayedAt;
    // The bags' counts of what the repair could ask: each material and its salvaged item.
    std::vector<Fusion::Construction::Material> asks;
    for (Maint::Owed const& o : Maint::RepairBasis(found.type, found.reinforcement))
        asks.push_back({o.item, uint32(o.count) + 1});
    Fusion::Construction::Inventory bags;
    for (uint32 item : Fusion::Construction::ItemsToCount(asks))
        bags[item] = player->GetItemCount(item);
    bool const free = m_buildFree || IsAdmin(player);
    Maint::RepairStep const step = Maint::PlanRepair(found, d.repairCredit, bags, free, now, found.damagedAt);
    switch (step.check)
    {
        case Maint::RepairCheck::Ok:
            break;
        case Maint::RepairCheck::NotRepairable:
            return answer(Outcome::WrongTarget);
        case Maint::RepairCheck::NotDamaged:
            return answer(Outcome::NotDamaged);
        case Maint::RepairCheck::RecentlyDamaged:
            return answer(Outcome::RecentlyDamaged);
        case Maint::RepairCheck::NoMaterials:
        {
            std::ostringstream o;
            for (size_t i = 0; i < step.cost.size(); ++i)
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(step.cost[i].item);
                o << (i ? ", " : "") << step.cost[i].count << " " << (proto ? proto->Name1 : "?");
            }
            player->GetSession()->SendNotification("Repairing it takes %s (fresh or salvaged).", o.str().c_str());
            return answer(Outcome::NoMaterials);
        }
    }
    for (Fusion::Construction::Material const& m : step.payment.take)
        player->DestroyItemCount(m.item, m.count, true);
    d.repairCredit = step.credit;
    Piece mended;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto& list = m_pieces[map->GetId()];
        auto pit = std::find_if(list.begin(), list.end(), [id](Piece const& p) { return p.id == id; });
        if (pit == list.end())
            return answer(Outcome::NoTarget);
        float const max = Fusion::Build::MaxHealth(*pit);
        pit->decayedAt = found.decayedAt;
        pit->health = std::min(max, found.health + step.gain);
        mended = *pit;
    }
    CharacterDatabase.PExecute("UPDATE `fusion_build_piece` SET `health` = %f, `decayed_at` = %" PRId64 " WHERE `id` = %u",
        mended.health, int64_t(mended.decayedAt), id);
    uint32 const mapId = map->GetId();
    Broadcast(map, mended.pos, [&](Player* viewer) {
        FusionPlayerData const& vd = viewer->GetFusionConst();
        if (vd.knownMap != mapId || !vd.knownPieces.count(id))
            return std::vector<uint8_t>();
        return Fusion::Build::EncodeHealth({mended});
    });
    // No skill-up: repairs teach no Construction (fusion/Maintenance.h).
    float const max = std::max(1.f, Fusion::Build::MaxHealth(mended));
    answer(Outcome::Repaired, uint8(std::min(100.f, mended.health * 100.f / max + 0.5f)), 100);
}

uint32 FusionMgr::AgePiecesNear(Player* player, int32 days)
{
    uint32 n = 0;
    Fusion::Vec3 const at(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(player->GetMapId());
        if (it != m_pieces.end())
            for (Fusion::Build::Piece& p : it->second)
                if ((p.pos - at).Length() <= 60.f)
                {
                    p.decayedAt -= int64(days) * Maint::kDay;
                    ++n;
                }
    }
    DecayPieces(player->GetMap());
    return n;
}

void FusionMgr::DecayPieces(Map* map)
{
    using Fusion::Build::Piece;
    if (!map)
        return;
    int64 const now = Now();
    uint32 const mapId = map->GetId();
    // The plots' paid times first (m_plotLock is never taken inside m_buildLock's hold here).
    std::unordered_map<uint32, int64> paid;
    {
        std::lock_guard<std::mutex> lock(m_plotLock);
        for (auto const& kv : m_plots.plots)
            if (kv.second.Owned())
                paid[kv.first] = kv.second.paidUntil;
    }
    std::vector<uint32> fallen;
    std::unordered_map<uint32, Piece> changed;
    std::vector<uint32> reflagged;      // a plot's upkeep began or ran out: the piece's flags change
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(mapId);
        if (it == m_pieces.end())
            return;
        for (Piece& p : it->second)
        {
            uint32 const plot = Fusion::Plots::PlotOf(PlotGrid(), p);
            auto pp = plot ? paid.find(plot) : paid.end();
            int64 const kept = pp == paid.end() ? 0 : pp->second;
            bool const wasKept = p.kept;
            p.kept = kept > now;
            Maint::Decayed const dd = Maint::Decay(p.health, Fusion::Build::MaxHealth(p), p.decayedAt, now, kept);
            p.decayedAt = dd.decayedAt;
            if (dd.collapsed)
                fallen.push_back(p.id);
            else if (dd.changed || wasKept != p.kept)
            {
                p.health = dd.health;
                changed[p.id] = p;
                if (wasKept != p.kept)
                    reflagged.push_back(p.id);
            }
        }
    }
    // The in-memory health only: the table keeps each piece's health with the time it was true at,
    // and decay since then is worked out again from it after a restart, so nothing is counted twice.
    for (uint32 id : fallen)
    {
        changed.erase(id);
        RemovePieces(map, id, Fusion::Build::RemoveCause::Destroyed);
    }
    if (!fallen.empty())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u built pieces on map %u fell to decay.", uint32(fallen.size()), mapId);
    if (changed.empty())
        return;
    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
    {
        Player* viewer = i->getSource();
        if (!viewer || !viewer->IsInWorld() || !viewer->HasFusion() || !viewer->GetSession())
            continue;
        FusionPlayerData const& vd = viewer->GetFusionConst();
        if (vd.knownMap != mapId)
            continue;
        std::vector<Piece> health, flags;
        for (uint32 id : vd.knownPieces)
        {
            auto c = changed.find(id);
            if (c == changed.end())
                continue;
            if (std::find(reflagged.begin(), reflagged.end(), id) != reflagged.end())
                flags.push_back(c->second);
            else
                health.push_back(c->second);
        }
        // A few hundred at a time, well within a message.
        for (size_t at = 0; at < health.size(); at += 400)
        {
            std::vector<Piece> part(health.begin() + at, health.begin() + std::min(health.size(), at + 400));
            SendChannel(viewer, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeHealth(part));
        }
        for (size_t at = 0; at < flags.size(); at += 100)
        {
            std::vector<Piece> part(flags.begin() + at, flags.begin() + std::min(flags.size(), at + 100));
            SendChannel(viewer, Fusion::Packets::kChannelBuild, Fusion::Build::EncodePieces(part, viewer->GetGUIDLow()));
        }
    }
}
