/*
 * World of Modern Warfare: a battle companion's pet bar, abilities, traits and breeding
 * (fusion/CompanionMoves.h, fusion/CompanionTraits.h). The companion is the player's controlled
 * pet (FusionCompanions.cpp summons it into the pet slot); its ten-button pet bar is laid out by
 * Companions::PetBarSlot and every press is WOMW's: the client aims Command and the abilities at
 * the crosshair (channel 4, kOpBarPress), the stock bar's presses still work against the current
 * target, and autocast follows a condition per ability type instead of vanilla's "whenever it is
 * off cooldown". Abilities are the vanilla spells cast triggered from the companion, their damage
 * set from its own basic hit. A rideable form's pet bar holds its mount spell (slot 7): pressing it
 * rides the companion. Traits roll when a companion item is first made, and two adults of one family
 * breed at a stable master, from the Companions window (FusionCompanionIndex.cpp). Its lines go to
 * the owner's UI error frame (FusionCompanionTell), not the chat. See src/fusion/docs/COMPANIONS.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"
#include "FusionLootSource.h"

#include "fusion/CompanionIndex.h"
#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"

#include "Bag.h"
#include "Chat.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Group.h"
#include "Item.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"

#include <ctime>
#include <sstream>

namespace Companions = Fusion::Companions;
using Companions::PetBarSlot::Count;

namespace
{
    // How far the crosshair may send it, and how far an aimed ability reaches.
    constexpr float kCommandRange = 60.f;
    constexpr float kAbilityRange = 40.f;

    Companions::Roller CompanionRoll()
    {
        return []() { return float(rand_norm()); };
    }

    // Each companion item in the bags (not the bank).
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

    // An ability's base points for each of its spell's effects: damage from the companion's
    // basic hit, healing from the healed one's health; effects that do neither keep the spell's.
    std::array<optional<int32>, MAX_EFFECT_INDEX> AbilityPoints(SpellEntry const* info, Companions::Ability const& a,
        Companions::CompanionStats const& st, uint32 healedMaxHealth)
    {
        std::array<optional<int32>, MAX_EFFECT_INDEX> bp;
        uint32 const direct = Companions::AbilityDirect(a, st, healedMaxHealth);
        uint32 const overTime = Companions::AbilityOverTime(a, st, healedMaxHealth);
        float const hit = Companions::BasicHit(st);
        int32 const duration = info->GetDuration();
        for (uint8 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            switch (info->Effect[i])
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_HEALTH_LEECH:
                case SPELL_EFFECT_HEAL:
                    if (direct)
                        bp[i] = int32(direct) - 1;
                    break;
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                    // On top of its swing: the share above one basic hit.
                    if (a.pct)
                        bp[i] = std::max<int32>(0, int32(direct) - int32(hit)) - 1;
                    break;
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                    if (a.pct)
                        bp[i] = int32(a.pct) - 1;
                    break;
                case SPELL_EFFECT_APPLY_AURA:
                case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                {
                    uint32 const aura = info->EffectApplyAuraName[i];
                    bool const dot = aura == SPELL_AURA_PERIODIC_DAMAGE || aura == SPELL_AURA_PERIODIC_LEECH;
                    bool const hot = aura == SPELL_AURA_PERIODIC_HEAL;
                    if ((dot || hot) && overTime && info->EffectAmplitude[i] && duration > 0)
                    {
                        int32 const span = a.duration ? int32(a.duration) * 1000 : duration;
                        int32 const ticks = std::max<int32>(1, span / int32(info->EffectAmplitude[i]));
                        bp[i] = std::max<int32>(1, int32(overTime) / ticks) - 1;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        return bp;
    }

    std::string ItemName(uint32 entry)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
        return proto ? proto->Name1 : "reagents";
    }

    bool IsStunLike(Companions::AbilityKind k)
    {
        return k == Companions::AbilityKind::Stun || k == Companions::AbilityKind::AoEStun;
    }

    std::string Lower(std::string s)
    {
        std::string out;
        for (char c : s)
            if (std::isalnum(uint8(c)))
                out += char(std::tolower(uint8(c)));
        return out;
    }
}

// ── Looks ───────────────────────────────────────────────────────────────────────────────────

float FusionMgr::CompanionScale(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s,
    uint32 display) const
{
    // The display's own size (CreatureDisplayInfo x CreatureModelData), which vanilla folds into
    // every creature's OBJECT_FIELD_SCALE_X, then the stage's and the traits' on top: a riding
    // wolf at stage scale 1 is the 1.2 it is ridden at.
    return Companions::LookScale(sp, s, display ? Unit::GetScaleForDisplayId(display) : 1.f);
}

// ── Trait rolls on a fresh item ─────────────────────────────────────────────────────────────

void FusionMgr::RollFreshCompanion(Item* item)
{
    Companions::CompanionState s;
    Companions::CompanionSpecies const* sp = Companions::FreshState(m_companionSpecies, m_companionItems, item->GetEntry(), s);
    if (!sp)
        return;
    // The rarity follows what dropped it: the corpse being stored from (an area-loot window's
    // other corpses, a group roll and the master looter's say which), else the creature its
    // looter is looting now.
    Fusion::CreatureRank rank = Fusion::CreatureRank::Normal;
    bool critter = false;
    if (Player* owner = ObjectAccessor::FindPlayer(item->GetOwnerGuid()))
    {
        ObjectGuid const source = FusionLootSource::Current().IsEmpty() ? owner->GetLootGuid() : FusionLootSource::Current();
        if (owner->IsInWorld() && source.IsCreature())
            if (Creature* c = owner->GetMap()->GetCreature(source))
            {
                CreatureInfo const* ci = c->GetCreatureInfo();
                uint32 const r = ci ? ci->rank : 0;
                rank = r <= 4 ? Fusion::CreatureRank(r) : Fusion::CreatureRank::Normal;
                if (c->GetMap()->IsDungeon() && c->GetScriptName().rfind("boss_", 0) == 0)
                    rank = Fusion::CreatureRank::DungeonBoss;
                critter = c->GetCreatureType() == CREATURE_TYPE_CRITTER;
            }
    }
    Companions::RollDrop(*sp, s, rank, critter, CompanionRoll());
    SaveCompanionState(item->GetGUIDLow(), s);
    // Caught: its index entry opens for its finder.
    if (Player* owner = ObjectAccessor::FindPlayer(item->GetOwnerGuid()))
        if (owner->IsInWorld())
            RecordCompanionSeen(owner, sp->id, Companions::Index::Seen::Captured);
}

// ── The pet bar ─────────────────────────────────────────────────────────────────────────────

void FusionMgr::SetupCompanionBar(Player* player, Pet* pet, Companions::CompanionSpecies const& sp,
    Companions::CompanionState const& s)
{
    CharmInfo* ci = pet->GetCharmInfo();
    if (!ci)
        return;
    FusionCompanionOut const& c = player->GetFusion().companion;
    namespace Slot = Companions::PetBarSlot;
    ReactStates const react = pet->GetReactState();
    ci->SetActionBar(Slot::Command, COMMAND_ATTACK, ACT_COMMAND);
    ci->SetActionBar(Slot::Stance, react == REACT_AGGRESSIVE ? REACT_AGGRESSIVE : REACT_DEFENSIVE, ACT_REACTION);
    ci->SetActionBar(Slot::Hold, REACT_PASSIVE, ACT_REACTION);
    for (uint8 slot : {Slot::Ability1, Slot::Ability2, Slot::Ability3, Slot::Twin})
    {
        Companions::Ability const a = Companions::AbilityInSlot(sp, s, slot);
        if (a.Exists() && sSpellMgr.GetSpellEntry(a.spell))
            ci->SetActionBar(slot, a.spell, c.autocast[slot] ? ACT_ENABLED : ACT_DISABLED);
        else
            ci->SetActionBar(slot, 0, ACT_DISABLED);
    }
    // A rideable form: its mount spell, pressed to ride it (CastCompanionAbility).
    {
        uint32 const mount = Companions::IsRideable(sp, s) ? sp.stages[s.stage].mountSpell : 0;
        ci->SetActionBar(Slot::Mounted, mount && sSpellMgr.GetSpellEntry(mount) ? mount : 0, ACT_DISABLED);
    }
    ci->SetActionBar(Slot::Follow, COMMAND_FOLLOW, ACT_COMMAND);
    ci->SetActionBar(Slot::Stay, COMMAND_STAY, ACT_COMMAND);
    player->PetSpellInitialize();
}

void FusionMgr::SendCompanionMoves(Player* player)
{
    if (!player->HasFusion())
        return;
    FusionCompanionOut const& c = player->GetFusion().companion;
    Companions::Moves m;
    Item* item = (!c.pet.IsEmpty() || c.riding) ? player->GetItemByGuid(c.item) : nullptr;
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (sp)
    {
        Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
        Companions::MovesetInfo const& info = Companions::MovesetData(Companions::MovesetOf(*sp));
        m.item = c.item.GetRawValue();
        m.species = sp->id;
        m.pending = int8(Companions::PendingPick(*sp, s));
        m.twinStage = s.twinStage;
        m.traits = s.traits;
        m.picks = s.picks;
        for (uint8 i = 0; i < Count; ++i)
            if (c.autocast[i])
                m.autocast |= uint16(1u << i);
        for (uint8 st = 0; st < Companions::kMaxStages; ++st)
            for (uint8 o = 0; o < 2; ++o)
            {
                m.spells[st][o] = info.stages[st][o].spell;
                m.names[st][o] = info.stages[st][o].name;
            }
        Companions::Ability const mounted = Companions::MountedMove(*sp, s);
        m.mountedSpell = mounted.spell;
        m.mountedName = mounted.name;
    }
    SendChannel(player, Companions::kChannelCompanion, Companions::EncodeMoves(m));
}

void FusionMgr::SendCompanionLineage(Player* player)
{
    ForEachBagItem(player, [&](Item* item) {
        if (!Companions::IsCompanionItem(item->GetEntry()))
            return;
        Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
        if (s.species && s.generation)
            SendChannel(player, Companions::kChannelCompanion, Companions::EncodeLineage(Companions::ItemSeed(s), s));
    });
}

bool FusionMgr::CompanionPetAction(Player* player, Pet* pet, uint8 type, uint32 action, ObjectGuid target)
{
    if (!pet || !pet->IsFusionCompanion() || !player->HasFusion())
        return false;
    namespace Slot = Companions::PetBarSlot;
    uint64 const guid = target.GetRawValue();
    switch (type)
    {
        case ACT_COMMAND:
            if (action == COMMAND_ATTACK)
                CompanionCommand(player, guid, nullptr, guid == 0);
            else if (action == COMMAND_FOLLOW)
                CompanionCommand(player, 0, nullptr, true);
            else if (action == COMMAND_DISMISS)
                DismissCompanion(player);
            else
                return false;   // stay: vanilla's
            return true;
        case ACT_REACTION:
            CompanionStance(player, action == REACT_PASSIVE ? Slot::Hold : Slot::Stance);
            return true;
        default:
        {
            // A spell button: whichever slot holds it.
            CharmInfo* ci = pet->GetCharmInfo();
            for (uint8 slot = 0; ci && slot < Count; ++slot)
                if (UnitActionBarEntry const* e = ci->GetActionBarEntry(slot))
                    if (e->IsActionBarForSpell() && e->GetAction() == action)
                    {
                        CastCompanionAbility(player, slot, guid, nullptr, true);
                        return true;
                    }
            return true;
        }
    }
}

bool FusionMgr::CompanionAutocast(Player* player, Pet* pet, uint32 spell, bool on)
{
    if (!pet || !pet->IsFusionCompanion() || !player->HasFusion())
        return false;
    FusionCompanionOut& c = player->GetFusion().companion;
    CharmInfo* ci = pet->GetCharmInfo();
    for (uint8 slot = 0; ci && slot < Count; ++slot)
        if (UnitActionBarEntry const* e = ci->GetActionBarEntry(slot))
            if (slot != Companions::PetBarSlot::Mounted && e->IsActionBarForSpell() && e->GetAction() == spell)
            {
                c.autocast[slot] = on;
                ci->SetSpellAutocast(spell, on);
            }
    SendCompanionMoves(player);
    return true;
}

void FusionMgr::CompanionCommand(Player* player, uint64 targetGuid, Fusion::Vec3 const* point, bool follow)
{
    Pet* pet = CompanionPet(player);
    if (!pet || !pet->IsAlive())
        return;
    if (player->GetFusion().companion.duelStats)
        return;     // its duel is its own
    if (follow)
    {
        pet->HandlePetCommand(COMMAND_FOLLOW, nullptr);
        return;
    }
    if (targetGuid)
    {
        Unit* u = pet->GetMap()->GetUnit(ObjectGuid(targetGuid));
        if (u && u->IsAlive() && pet->IsValidAttackTarget(u) && pet->IsWithinDistInMap(u, kCommandRange)
            && (pet->IsWithinLOSInMap(u) || player->IsWithinLOSInMap(u)))
        {
            pet->HandlePetCommand(COMMAND_ATTACK, u);
            return;
        }
        if (!point)
        {
            FusionCompanionTell(player).SendSysMessage("Your companion can't attack that.");
            return;
        }
    }
    if (point)
    {
        float const dx = point->x - player->GetPositionX(), dy = point->y - player->GetPositionY();
        if (dx * dx + dy * dy > kCommandRange * kCommandRange)
        {
            FusionCompanionTell(player).SendSysMessage("Too far for your companion.");
            return;
        }
        if (std::fabs(point->z - player->GetPositionZ()) > kCommandRange / 2.f || !player->IsWithinLOS(point->x, point->y, point->z + 1.f))
        {
            FusionCompanionTell(player).SendSysMessage("You can't see that spot.");
            return;
        }
        // Move there and hold: vanilla's Stay, at the crosshair's spot. It walks there as PetAI
        // walks back to a stay spot (the move's id its guid), so it knows when it has arrived and
        // comes back to that spot, not where it was told, after a fight.
        pet->HandlePetCommand(COMMAND_STAY, nullptr);
        if (CharmInfo* ci = pet->GetCharmInfo())
        {
            pet->GetMotionMaster()->Clear(false);
            pet->GetMotionMaster()->MovePoint(pet->GetGUIDLow(), point->x, point->y, point->z, MOVE_PATHFINDING);
            ci->SaveStayPosition();
            ci->SetIsAtStay(false);
            ci->SetIsReturning(true);
        }
        CompanionNotice(player, Companions::Index::NoticeKind::Info,
            "Companion: holding that spot (double-tap Command to call it back).");
        return;
    }
    pet->HandlePetCommand(COMMAND_FOLLOW, nullptr);
}

void FusionMgr::CompanionStance(Player* player, uint8 slot)
{
    Pet* pet = CompanionPet(player);
    CharmInfo* ci = pet ? pet->GetCharmInfo() : nullptr;
    if (!ci)
        return;
    ReactStates const now = pet->GetReactState();
    if (slot == Companions::PetBarSlot::Hold)
    {
        if (now == REACT_PASSIVE)
        {
            pet->SetReactState(REACT_DEFENSIVE);
            pet->HandlePetCommand(COMMAND_FOLLOW, nullptr);
            CompanionNotice(player, Companions::Index::NoticeKind::Info, "Companion: assist (it fights what you fight).");
        }
        else
        {
            pet->InterruptNonMeleeSpells(false);
            pet->AttackStop();
            pet->SetReactState(REACT_PASSIVE);
            pet->HandlePetCommand(COMMAND_STAY, nullptr);
            CompanionNotice(player, Companions::Index::NoticeKind::Info, "Companion: hold (it stays put and fights only on command).");
        }
    }
    else
    {
        // Assist (vanilla Defensive) <-> Aggressive; from Hold back to Assist.
        ReactStates const next = now == REACT_DEFENSIVE ? REACT_AGGRESSIVE : REACT_DEFENSIVE;
        pet->SetReactState(next);
        if (now == REACT_PASSIVE)
            pet->HandlePetCommand(COMMAND_FOLLOW, nullptr);
        CompanionNotice(player, Companions::Index::NoticeKind::Info, next == REACT_AGGRESSIVE ? "Companion: aggressive (it attacks what comes near)." : "Companion: assist (it fights what you fight).");
    }
    ci->SetActionBar(Companions::PetBarSlot::Stance,
        pet->GetReactState() == REACT_AGGRESSIVE ? REACT_AGGRESSIVE : REACT_DEFENSIVE, ACT_REACTION);
    player->PetSpellInitialize();
    // Kept for its next summon (FusionCompanionOut::react).
    player->GetFusion().companion.react = uint8(pet->GetReactState());
}

bool FusionMgr::CastCompanionAbility(Player* player, uint8 slot, uint64 targetGuid, Fusion::Vec3 const* point, bool manual)
{
    if (slot >= Count || !player->HasFusion())
        return false;
    FusionCompanionOut& c = player->GetFusion().companion;
    FusionCompanionTell chat(player);
    Pet* pet = CompanionPet(player);
    bool const mounted = slot == Companions::PetBarSlot::Mounted;
    // On foot, the mount button rides it.
    if (mounted && !c.riding && pet)
    {
        if (manual)
            RideCompanion(player);
        return manual;
    }
    if (mounted ? !c.riding : !pet)
        return false;
    Item* item = player->GetItemByGuid(c.item);
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
        return false;
    Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    Companions::Ability const a = mounted ? Companions::MountedMove(*sp, s) : Companions::AbilityInSlot(*sp, s, slot);
    SpellEntry const* info = a.Exists() ? sSpellMgr.GetSpellEntry(a.spell) : nullptr;
    if (!info)
    {
        if (manual)
            chat.SendSysMessage(Companions::PendingPick(*sp, s) >= 0
                ? "Choose your companion's ability first (the Companions window)." : "No ability in that slot yet.");
        return false;
    }
    uint32 const now = WorldTimer::getMSTime();
    if (int32(c.readyAtMs[slot] - now) > 0)
    {
        if (manual)
            chat.PSendSysMessage("%s is not ready (%u s).", a.name, (c.readyAtMs[slot] - now + 999) / 1000);
        return false;
    }
    // A flying mount's Launch: a vigor charge spent on a throw straight up, taking off
    // into a skyride or climbing in one (FusionRiding, Riding.h, Skyriding.h).
    if (mounted && Companions::IsSkySurge(a))
    {
        if (!SkyMove(player, Fusion::Skyriding::Move::Ascent))
        {
            if (manual)
                chat.SendSysMessage("Your mount can't climb from here (no vigor, in water, or not yet).");
            return false;
        }
        uint32 const cd = std::max<uint32>(1000, Companions::AbilityCooldownMs(a, false));
        c.readyAtMs[slot] = now + cd;
        SendChannel(player, Companions::kChannelCompanion, Companions::EncodeCooldown(slot, cd));
        return true;
    }
    Unit* caster = mounted ? static_cast<Unit*>(player) : static_cast<Unit*>(pet);
    if (!caster->IsAlive() || (!mounted && pet->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL)))
        return false;

    // Whom: the crosshair's unit, else what it fights, else the owner's target.
    Unit* target = nullptr;
    Map* map = caster->GetMap();
    using Aim = Companions::AbilityAim;
    switch (a.aim)
    {
        case Aim::Enemy:
        {
            // In sight too: no stun or blast through a wall or a floor.
            auto ok = [&](Unit* u) {
                return u && u->IsAlive() && caster->IsValidAttackTarget(u) && caster->IsWithinDistInMap(u, kAbilityRange)
                    && caster->IsWithinLOSInMap(u);
            };
            if (targetGuid)
                if (Unit* u = map->GetUnit(ObjectGuid(targetGuid)); ok(u))
                    target = u;
            if (!target && ok(caster->GetVictim()))
                target = caster->GetVictim();
            if (!target && !mounted && ok(pet->GetVictim()))
                target = pet->GetVictim();
            if (!target && ok(player->GetVictim()))
                target = player->GetVictim();
            if (!target)
                if (Unit* sel = map->GetUnit(player->GetSelectionGuid()); ok(sel))
                    target = sel;
            if (!target)
            {
                if (manual)
                    chat.SendSysMessage("Aim at an enemy.");
                return false;
            }
            break;
        }
        case Aim::Owner:
            target = player;
            break;
        default:
            target = caster;
            break;
    }

    uint8 const level = Companions::EffectiveLevel(s, uint8(player->GetLevel()));
    Companions::CompanionStats const st = Companions::StatsWithTraits(*sp, s, level);
    if (a.aim == Aim::Ground)
    {
        Fusion::Vec3 at = point ? *point
            : (caster->GetVictim() ? Fusion::Vec3{caster->GetVictim()->GetPositionX(), caster->GetVictim()->GetPositionY(),
                                                   caster->GetVictim()->GetPositionZ()}
                                   : Fusion::Vec3{caster->GetPositionX(), caster->GetPositionY(), caster->GetPositionZ()});
        float const dx = at.x - caster->GetPositionX(), dy = at.y - caster->GetPositionY();
        if (dx * dx + dy * dy > kAbilityRange * kAbilityRange)
        {
            if (manual)
                chat.SendSysMessage("Too far.");
            return false;
        }
        // The client's point: in sight, and not far above or below (a floor away).
        if (std::fabs(at.z - caster->GetPositionZ()) > kAbilityRange / 2.f || !caster->IsWithinLOS(at.x, at.y, at.z + 1.f))
        {
            if (manual)
                chat.SendSysMessage("Your companion can't see that spot.");
            return false;
        }
        caster->CastSpell(at.x, at.y, at.z, info, true);
    }
    else
    {
        uint32 const healed = a.aim == Aim::Owner ? player->GetMaxHealth() : caster->GetMaxHealth();
        std::array<optional<int32>, MAX_EFFECT_INDEX> const bp = AbilityPoints(info, a, st, healed);
        caster->CastCustomSpell(target, info, bp[0], bp[1], bp[2], true);
        // The catalog's duration for its crowd control and auras; a stun on a player never
        // passes 3 s.
        if (a.duration && target != caster)
        {
            int32 ms = int32(a.duration) * 1000;
            if (target->IsPlayer() && IsStunLike(a.kind))
                ms = std::min(ms, 3000);
            c.clamps.push_back({target->GetObjectGuid().GetRawValue(), a.spell, ms, now + 3000});
        }
    }
    uint32 const cd = std::max<uint32>(1000, Companions::AbilityCooldownMs(a, Companions::HasTrait(s.traits, Companions::Trait::Primal)));
    c.readyAtMs[slot] = now + cd;
    if (!mounted)
        player->SendSpellCooldown(a.spell, Milliseconds(cd), pet->GetObjectGuid());
    SendChannel(player, Companions::kChannelCompanion, Companions::EncodeCooldown(slot, cd));
    return true;
}

// ── The half-second look in a fight ─────────────────────────────────────────────────────────

void FusionMgr::UpdateCompanionCombat(Player* player, uint32 now)
{
    FusionCompanionOut& c = player->GetFusion().companion;
    if (!c.lineageSent)
    {
        c.lineageSent = true;
        SendCompanionLineage(player);
    }
    // Durations to the catalog's, once the auras are on.
    if (!c.clamps.empty())
    {
        std::vector<FusionCompanionOut::Clamp> keep;
        for (FusionCompanionOut::Clamp const& k : c.clamps)
        {
            Unit* u = player->IsInWorld() ? player->GetMap()->GetUnit(ObjectGuid(k.target)) : nullptr;
            SpellAuraHolder* h = u ? u->GetSpellAuraHolder(k.spell) : nullptr;
            if (h)
            {
                if (h->GetAuraDuration() > k.durationMs)
                {
                    h->SetAuraMaxDuration(k.durationMs);
                    h->SetAuraDuration(k.durationMs);
                    h->UpdateAuraDuration();
                }
            }
            else if (int32(k.untilMs - now) > 0)
                keep.push_back(k);
        }
        c.clamps.swap(keep);
    }
    Pet* pet = CompanionPet(player);
    if (!pet || !pet->IsAlive())
        return;
    Item* item = player->GetItemByGuid(c.item);
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
        return;
    Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    using Companions::Trait;
    bool const fighting = pet->IsInCombat() || player->IsInCombat();
    float const share = pet->GetMaxHealth() ? float(pet->GetHealth()) / float(pet->GetMaxHealth()) : 1.f;

    // Berserk: below 35% it rages until healed above half.
    if (Companions::HasTrait(s.traits, Trait::Berserk) && !c.duelStats)
    {
        if (!c.berserk && fighting && share < 0.35f)
        {
            c.berserk = true;
            pet->CastSpell(pet, 8599, true);
        }
        else if (c.berserk && share > 0.5f)
            c.berserk = false;
    }
    // Packmule: a magazine for its owner's gun every 30 s in a fight, 10 s out of one
    // (Quartermaster: 20 s in a fight).
    if (Companions::HasTrait(s.traits, Trait::Packmule) && !c.duelStats && int32(now - c.packmuleMs) >= 0)
    {
        bool const qm = Companions::HasCombo(s.traits, Companions::Combo::Quartermaster);
        c.packmuleMs = now + (fighting ? (qm ? 20000 : 30000) : 10000);
        FusionPlayerData& d = player->GetFusion();
        auto it = d.ammo.find(d.heldWeaponId);
        if (it != d.ammo.end())
        {
            it->second.Scavenge();
            if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(d.heldWeaponId))
                SendAmmo(player, *w);
        }
    }
    Unit* victim = pet->GetVictim();
    if (victim && !victim->IsAlive())
        victim = nullptr;

    // Its basic attack from range: a caster's bolt, a Gunner's gun (SummonCompanion keeps it from
    // swinging and at kRangedReach). It fires whenever it attacks something, before either side
    // is in combat: the first bolt is what starts the fight. In a companion duel too, at the
    // duel's level (the pet's own, BuildCompanion's).
    if (uint32 const bolt = Companions::RangedAttackSpell(*sp, s); bolt && victim && int32(now - c.nextShotMs) >= 0 &&
        !pet->IsNonMeleeSpellCasted(false) && !pet->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL) &&
        pet->IsWithinDistInMap(victim, Companions::kRangedFireRange) && pet->IsWithinLOSInMap(victim))
        if (SpellEntry const* info = sSpellMgr.GetSpellEntry(bolt))
        {
            Companions::CompanionStats const st = Companions::StatsWithTraits(*sp, s, uint8(pet->GetLevel()));
            c.nextShotMs = now + st.attackMs;
            // One basic hit: a bolt's school damage set to it, a Shoot's or a Throw's from the
            // ranged weapon damage BuildCompanion set to it.
            Companions::Ability basic;
            basic.spell = bolt;
            basic.pct = 100;
            std::array<optional<int32>, MAX_EFFECT_INDEX> const bp = AbilityPoints(info, basic, st, 0);
            if (!pet->HasInArc(victim))
                pet->SetFacingToObject(victim);
            pet->CastCustomSpell(victim, info, bp[0], bp[1], bp[2], true);
        }

    // Hold: it does nothing by itself (a commanded attack still fires its basic attack above);
    // a duel is fought with the basic attack alone.
    if (c.duelStats || !fighting || pet->GetReactState() == REACT_PASSIVE)
        return;

    // One ability a look: the first whose moment it is.
    Companions::AutocastContext ctx;
    ctx.ownerHealth = player->GetMaxHealth() ? float(player->GetHealth()) / float(player->GetMaxHealth()) : 1.f;
    ctx.companionHealth = share;
    ctx.ownerInCombat = player->IsInCombat();
    ctx.dungeonOtherPacksNear = player->GetMap()->IsDungeon();
    ctx.ownerHitByOther = c.ownerHitBy && now - c.ownerHitMs < 2000;
    ctx.allyDispellable = player->HasAuraType(SPELL_AURA_MOD_STUN) || player->HasAuraType(SPELL_AURA_MOD_ROOT) ||
        player->HasAuraType(SPELL_AURA_MOD_FEAR) || player->HasAuraType(SPELL_AURA_MOD_CONFUSE);
    for (Unit* u : pet->GetAttackers())
        if (u->IsAlive())
            ++ctx.attackersOnCompanion;
    // The hurt around it: its owner, itself, and the owner's party within 30 yards (an area
    // heal waits for two of them under 70%, a taunt answers a party member under half being hit).
    auto hurt = [](Unit const* u, float under) {
        return u->IsAlive() && u->GetMaxHealth() && float(u->GetHealth()) < float(u->GetMaxHealth()) * under;
    };
    if (hurt(player, 0.7f))
        ++ctx.alliesUnder70;
    if (hurt(pet, 0.7f))
        ++ctx.alliesUnder70;
    if (Group* group = player->GetGroup())
        for (GroupReference* r = group->GetFirstMember(); r; r = r->next())
        {
            Player* m = r->getSource();
            if (!m || m == player || !m->IsInWorld() || m->GetMap() != player->GetMap() || !pet->IsWithinDistInMap(m, 30.f))
                continue;
            if (hurt(m, 0.7f))
                ++ctx.alliesUnder70;
            if (hurt(m, 0.5f) && !m->GetAttackers().empty())
                ctx.allyUnderHalfAttacked = true;
        }
    std::set<Unit*> foes;
    for (Unit* u : player->GetAttackers())
        foes.insert(u);
    for (Unit* u : pet->GetAttackers())
        foes.insert(u);
    if (victim)
        foes.insert(victim);
    for (Unit* u : foes)
    {
        if (!u || !u->IsAlive())
            continue;
        if (pet->IsWithinDistInMap(u, 8.f))
        {
            ++ctx.enemiesNear;
            if (u->HasBreakableByDamageCrowdControlAura())
                ctx.wouldBreakCC = true;
        }
        if ((u->GetVictim() == player && u->IsWithinDistInMap(player, 5.f)) || (u->GetVictim() == pet && u->IsWithinDistInMap(pet, 5.f)))
            ++ctx.enemiesInMeleeWithUs;
    }
    if (victim)
    {
        ctx.hasTarget = true;
        ctx.targetDistance = pet->GetDistance(victim);
        ctx.targetHealth = victim->GetMaxHealth() ? float(victim->GetHealth()) / float(victim->GetMaxHealth()) : 1.f;
        if (Creature* vc = victim->ToCreature())
            ctx.targetIsBoss = vc->IsWorldBoss() || (vc->GetMap()->IsDungeon() && vc->IsElite());
        ctx.targetCasting = victim->IsNonMeleeSpellCasted(false);
        ctx.targetAttacksCompanion = victim->GetVictim() == pet;
        ctx.targetInMeleeWithOwner = victim->GetVictim() == player && victim->IsWithinDistInMap(player, 5.f);
        ctx.targetApproachingOwner = victim->GetVictim() == player && victim->IsWithinDistInMap(player, 15.f) &&
            !victim->IsWithinDistInMap(player, 5.f) && victim->IsMoving();
        ctx.targetFleeing = victim->HasUnitState(UNIT_STATE_FLEEING);
        // A magic buff on it a Devour Magic would eat.
        for (auto const& [id, holder] : victim->GetSpellAuraHolderMap())
            if (holder && holder->IsPositive() && holder->GetSpellProto()->Dispel == DISPEL_MAGIC)
            {
                ctx.enemyDispellable = true;
                break;
            }
    }
    namespace Slot = Companions::PetBarSlot;
    for (uint8 slot : {Slot::Ability1, Slot::Ability2, Slot::Ability3, Slot::Twin})
    {
        if (!c.autocast[slot] || int32(c.readyAtMs[slot] - now) > 0)
            continue;
        Companions::Ability const a = Companions::AbilityInSlot(*sp, s, slot);
        if (!a.Exists())
            continue;
        Companions::AutocastContext t = ctx;
        Unit* recipient = a.aim == Companions::AbilityAim::Owner ? static_cast<Unit*>(player) : static_cast<Unit*>(pet);
        if (victim)
            if (SpellAuraHolder* h = victim->GetSpellAuraHolder(a.spell, pet->GetObjectGuid()))
            {
                t.targetHasIt = true;
                t.targetStacks = uint8(h->GetStackAmount());
                t.maxStacks = uint8(std::max<uint32>(1, sSpellMgr.GetSpellEntry(a.spell) ? sSpellMgr.GetSpellEntry(a.spell)->StackAmount : 1));
                t.targetItExpiresS = float(h->GetAuraDuration()) / 1000.f;
            }
        t.hotFromIt = recipient->HasAura(a.spell);
        t.ownerHasBuff = recipient->HasAura(a.spell);
        // A dispel goes where its aim says: an enemy's buff, or its owner's trouble.
        if (a.kind == Companions::AbilityKind::Dispel)
        {
            if (a.aim == Companions::AbilityAim::Enemy)
                t.allyDispellable = false;
            else
                t.enemyDispellable = false;
        }
        if (Companions::ShouldAutocast(a, t) && CastCompanionAbility(player, slot, victim ? victim->GetObjectGuid().GetRawValue() : 0, nullptr, false))
            break;
    }
}

// ── Traits on its hits ──────────────────────────────────────────────────────────────────────

void FusionMgr::OnCompanionHit(Player* owner, Pet* pet, Unit* victim, uint32 /*damage*/)
{
    FusionCompanionOut& c = owner->GetFusion().companion;
    uint32 const now = WorldTimer::getMSTime();
    if (c.duelStats || int32(now - c.nextProcMs) < 0 || !victim->IsAlive())
        return;
    Item* item = owner->GetItemByGuid(c.item);
    Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
        return;
    Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    if (!s.traits)
        return;
    using Companions::Trait;
    Companions::CompanionStats const st = Companions::StatsWithTraits(*sp, s, Companions::EffectiveLevel(s, uint8(owner->GetLevel())));
    float const hit = Companions::BasicHit(st);
    bool procced = false;
    auto cast = [&](uint32 spell, float bpShare) {
        if (sSpellMgr.GetSpellEntry(spell))
        {
            pet->CastCustomSpell(victim, spell, std::max<int32>(1, int32(hit * bpShare)), {}, {}, true);
            procced = true;
        }
    };
    if (Companions::HasTrait(s.traits, Trait::Venomous) && rand_norm() < 0.20 && !victim->HasAura(744))
    {
        // 30% BH over 12 s, one stack (Poison's four ticks).
        cast(744, 0.3f / 4.f);
        if (Companions::HasCombo(s.traits, Companions::Combo::Viper))
            pet->CastSpell(victim, 9672, true);     // and a 30% slow
    }
    if (Companions::HasTrait(s.traits, Trait::FireTouched) && rand_norm() < 0.25)
        cast(3110, 0.4f);   // a quarter of hits for 40%: about +10% as fire
    if (Companions::HasTrait(s.traits, Trait::FrostTouched) && rand_norm() < 0.10)
        cast(9672, 0.2f);
    if (Companions::HasTrait(s.traits, Trait::StormTouched) &&
        rand_norm() < (Companions::HasCombo(s.traits, Companions::Combo::Stormcaller) ? 0.12 : 0.08))
        cast(12058, 0.5f);
    // Shadow-touched and Bloodthirsty heal it from its hits.
    float heal = 0.f;
    if (Companions::HasTrait(s.traits, Trait::ShadowTouched))
        heal += 0.04f * hit;
    if (Companions::HasTrait(s.traits, Trait::Bloodthirsty))
        heal += 0.03f * hit;
    if (heal >= 1.f && pet->GetHealth() < pet->GetMaxHealth())
        pet->SetHealth(std::min(pet->GetMaxHealth(), pet->GetHealth() + uint32(heal)));
    if (procced)
        c.nextProcMs = now + 1500;
}

// ── Breeding and retraining at a stable master ──────────────────────────────────────────────

std::vector<FusionMgr::CompanionBreedPair> FusionMgr::CompanionBreedPairs(Player* player) const
{
    std::vector<Item*> adults;
    ForEachBagItem(player, [&](Item* item) {
        if (!Companions::IsCompanionItem(item->GetEntry()))
            return;
        Companions::CompanionSpecies const* sp = CompanionSpeciesOfItem(item->GetEntry());
        Companions::CompanionState const s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
        if (sp && sp->stageCount >= 3 && s.stage + 1 >= sp->stageCount)
            adults.push_back(item);
    });
    std::vector<CompanionBreedPair> pairs;
    for (size_t i = 0; i < adults.size(); ++i)
        for (size_t j = i + 1; j < adults.size(); ++j)
        {
            Companions::CompanionSpecies const* a = CompanionSpeciesOfItem(adults[i]->GetEntry());
            Companions::CompanionSpecies const* b = CompanionSpeciesOfItem(adults[j]->GetEntry());
            if (a && b && Companions::BreedGroup(a->family) == Companions::BreedGroup(b->family))
                pairs.push_back({adults[i], adults[j]});
        }
    return pairs;
}

bool FusionMgr::BreedCompanions(Player* player, Item* ia, Item* ib, bool free)
{
    FusionCompanionTell chat(player);
    Companions::CompanionSpecies const* spA = CompanionSpeciesOfItem(ia->GetEntry());
    Companions::CompanionSpecies const* spB = CompanionSpeciesOfItem(ib->GetEntry());
    if (!spA || !spB)
        return false;
    Companions::CompanionState a = CompanionStateOf(ia->GetGUIDLow(), ia->GetEntry());
    Companions::CompanionState b = CompanionStateOf(ib->GetGUIDLow(), ib->GetEntry());
    a.species = spA->id;
    b.species = spB->id;
    FusionCompanionOut const& c = player->GetFusion().companion;
    uint32 const reagentA = Companions::BreedReagent(*spA), reagentB = Companions::BreedReagent(*spB);
    Companions::BreedContext ctx;
    ctx.now = uint32(time(nullptr));
    bool const out = !c.pet.IsEmpty() || c.riding;
    ctx.aBusy = out && c.item == ia->GetObjectGuid();
    ctx.bBusy = out && c.item == ib->GetObjectGuid();
    if (reagentA == reagentB)
        ctx.reagentsA = ctx.reagentsB = player->GetItemCount(reagentA) / 2;
    else
    {
        ctx.reagentsA = player->GetItemCount(reagentA);
        ctx.reagentsB = player->GetItemCount(reagentB);
    }
    ctx.money = player->GetMoney();
    uint32 const fee = Companions::BreedFee(a.level, b.level);
    if (free)
    {
        ctx.reagentsA = ctx.reagentsB = Companions::kBreedReagentsPerParent;
        ctx.money = fee;
        a.breedReadyAt = b.breedReadyAt = 0;
    }
    using Companions::BreedBlock;
    switch (Companions::CanBreed(*spA, a, *spB, b, ia == ib, ctx))
    {
        case BreedBlock::None: break;
        case BreedBlock::SameCompanion: chat.SendSysMessage("A companion cannot breed with itself."); return false;
        case BreedBlock::NotAdult: chat.SendSysMessage("Both companions must be fully grown."); return false;
        case BreedBlock::OtherFamily: chat.SendSysMessage("Only companions of one family breed together."); return false;
        case BreedBlock::Exhausted: chat.PSendSysMessage("A companion breeds at most %u times.", uint32(Companions::kBreedMax)); return false;
        case BreedBlock::Resting:
        {
            uint32 const until = std::max(a.breedReadyAt, b.breedReadyAt);
            chat.PSendSysMessage("Still resting from its last litter (%u h).", (until - ctx.now + 3599) / 3600);
            return false;
        }
        case BreedBlock::Busy: chat.SendSysMessage("Put your companion away first."); return false;
        case BreedBlock::NoReagents:
            chat.PSendSysMessage("You need %u %s and %u %s.", uint32(Companions::kBreedReagentsPerParent),
                ItemName(reagentA).c_str(), uint32(Companions::kBreedReagentsPerParent), ItemName(reagentB).c_str());
            return false;
        case BreedBlock::NoMoney:
            chat.PSendSysMessage("The stable master asks %ug %us.", fee / 10000, fee % 10000 / 100);
            return false;
    }
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, spA->item, 1) != EQUIP_ERR_OK)
    {
        player->SendEquipError(EQUIP_ERR_INVENTORY_FULL, nullptr, nullptr, spA->item);
        return false;
    }
    // The young one first: nothing is paid for a litter that never came.
    Item* pup = player->StoreNewItem(dest, spA->item, true);
    if (!pup)
    {
        chat.SendSysMessage("The stable master could not hand you the young one; nothing was taken.");
        return false;
    }
    if (!free)
    {
        player->DestroyItemCount(reagentA, Companions::kBreedReagentsPerParent, true);
        player->DestroyItemCount(reagentB, Companions::kBreedReagentsPerParent, true);
        player->ModifyMoney(-int32(fee));
    }
    Companions::CompanionState const kid = Companions::BreedOffspring(*spA, a, *spB, b, CompanionRoll());
    Companions::MarkBred(a, ctx.now);
    Companions::MarkBred(b, ctx.now);
    SaveCompanionState(ia->GetGUIDLow(), a);
    SaveCompanionState(ib->GetGUIDLow(), b);
    StampCompanionItem(ia);
    StampCompanionItem(ib);
    SaveCompanionState(pup->GetGUIDLow(), kid);
    StampCompanionItem(pup);
    player->SendNewItem(pup, 1, true, false);
    SendCompanionLineage(player);
    RecordCompanionSeen(player, spA->id, Companions::Index::Seen::Captured);
    std::string const traits = Companions::TraitLine(kid.traits);
    chat.PSendSysMessage("|cffffd200Your %s and %s had a young one: %s%s%s.|r", spA->name.c_str(), spB->name.c_str(),
        spA->name.c_str(), traits.empty() ? "" : " - ", traits.c_str());
    return true;
}

void FusionMgr::BreedFirstPair(Player* player, bool free)
{
    std::vector<CompanionBreedPair> const pairs = CompanionBreedPairs(player);
    if (pairs.empty())
    {
        FusionCompanionTell(player).SendSysMessage("Fusion: no two fully grown companions of one family in your bags.");
        return;
    }
    BreedCompanions(player, pairs.front().a, pairs.front().b, free);
}

void FusionMgr::PickCompanionAbility(Player* player, uint64 itemGuid, uint8 stage, uint8 option)
{
    Item* item = player->GetItemByGuid(ObjectGuid(itemGuid));
    Companions::CompanionSpecies const* sp = item && Companions::IsCompanionItem(item->GetEntry())
        ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
        return;
    if (!Player::IsInventoryPos(item->GetPos()) || item->IsInTrade())
        return CompanionNotice(player, Companions::Index::NoticeKind::Error,
            item->IsInTrade() ? "Not while it is in a trade window." : "That companion must be in your bags.");
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    s.species = sp->id;
    if (!Companions::Pick(*sp, s, stage, option))
    {
        // Already chosen (a stable master changes it), or a stage it has not reached.
        CompanionNotice(player, Companions::Index::NoticeKind::Error, stage < Companions::kMaxStages && s.picks[stage]
            ? "That stage's ability is chosen; a stable master can change it."
            : "Your companion has not reached that stage.");
        SendCompanionMoves(player);
        SendCompanionCarried(player);
        return;
    }
    SaveCompanionState(item->GetGUIDLow(), s);
    Companions::Ability const a = Companions::MovesetData(Companions::MovesetOf(*sp)).stages[stage][option - 1];
    FusionCompanionTell(player).PSendSysMessage("|cff71d5ffYour %s learned %s.|r", sp->name.c_str(), a.name);
    SendCompanionCarried(player);
    FusionCompanionOut const& c = player->GetFusion().companion;
    if (c.item == item->GetObjectGuid())
        if (Pet* pet = CompanionPet(player))
            SetupCompanionBar(player, pet, *sp, s);
    SendCompanionMoves(player);
}

Item* FusionMgr::GiveCompanion(Player* player, uint16 species, uint8 level, uint8 stage, uint32 traits,
    uint8 twinStage, uint8 generation)
{
    Item* item = GiveCompanion(player, species, level, stage);
    if (!item)
        return nullptr;
    Companions::CompanionState s = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    if (Companions::CompanionSpecies const* sp = Companions::FindSpecies(m_companionSpecies, species))
    {
        if (traits != 0xFFFFFFFF)
        {
            s.traits = traits;
            s.twinStage = Companions::HasTrait(traits, Companions::Trait::TwinSouled)
                ? uint8(std::max<uint8>(1, std::min<uint8>(twinStage ? twinStage : 1, sp->stageCount))) : 0;
            s.rarity = uint8(Companions::RarityOf(traits));
        }
        if (generation)
        {
            s.generation = generation;
            s.parentA = s.parentB = sp->id;
        }
    }
    SaveCompanionState(item->GetGUIDLow(), s);
    StampCompanionItem(item);
    return item;
}

// ── Commands ────────────────────────────────────────────────────────────────────────────────

// The `.fusion pet` extras (ChatHandler::HandleFusionPetCommand): a trait list by name, and the
// breed and pick subcommands. `text` is the rest of the line after the species, level and stage.
uint32 FusionCompanionTraitsFromText(std::string const& text, uint8& twin, uint8& gen, bool& any)
{
    uint32 mask = 0;
    any = false;
    std::istringstream in(text);
    std::string word;
    while (in >> word)
    {
        std::string const w = Lower(word);
        if (w.rfind("twin", 0) == 0 && w.size() > 4 && std::isdigit(uint8(w[4])))
        {
            twin = uint8(w[4] - '0');
            continue;
        }
        if (w.rfind("gen", 0) == 0 && w.size() > 3 && std::isdigit(uint8(w[3])))
        {
            gen = uint8(std::strtoul(w.c_str() + 3, nullptr, 10));
            continue;
        }
        if (w == "common" || w == "none")
        {
            any = true;
            continue;
        }
        std::stringstream parts(word);
        std::string part;
        while (std::getline(parts, part, ','))
            for (uint8 i = 1; i <= Companions::kTraitCount; ++i)
                if (Lower(Companions::TraitData(Companions::Trait(i)).name) == Lower(part))
                {
                    mask |= Companions::TraitBit(Companions::Trait(i));
                    any = true;
                }
    }
    return mask;
}
