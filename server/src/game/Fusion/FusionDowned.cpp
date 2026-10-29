/*
 * World of Modern Warfare: the downed state (fusion/Downed.h) in the game. A hit that would kill a
 * player leaves them down at 1 health instead: they crawl (forced prone, slow), cannot attack,
 * cast or use anything but a revival kit, and bleed out after a time that grows with Stamina.
 * Hits on a downed player drain a small finisher pool, and the one that empties it kills. Last
 * Stand fires a pistol while down; Final Stand gets back up by itself once a life. Only the
 * resurrection spells and the revival kits (First Aid's Field Revival Kit, Engineering's Gnomish
 * Revival Coil) bring a downed player back up. Bleeding out or being finished is the vanilla
 * death: ghost, corpse run, spirit healer.
 *
 * The client is told on fusion channel 5 (Downed::kChannelDowned) and never predicts any of it.
 * Everyone else sees a downed player lying down: the state sets UNIT_DYNFLAG_DEAD on a living
 * body, as Feign Death does (Player::CheckMirrorTimerActivation keeps feign death's own timer
 * out of it), which also lets the client aim a resurrection spell at them.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"
#include "FusionClasses.h"

#include "Bag.h"
#include "Chat.h"
#include "Creature.h"
#include "Item.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "ThreatManager.h"
#include "Timer.h"
#include "World.h"
#include "WorldSession.h"

namespace Downed = Fusion::Downed;

namespace
{
    Fusion::Vec3 FeetOf(Unit const* u) { return {u->GetPositionX(), u->GetPositionY(), u->GetPositionZ()}; }

    // Players this near hear that someone went down or got up.
    constexpr float kTellRadius = 100.f;

    char const* ItemName(uint32 entry)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
        return proto && proto->Name1 ? proto->Name1 : "revival kit";
    }
}

bool FusionMgr::IsDowned(Unit const* unit) const
{
    Player const* p = unit ? unit->ToPlayer() : nullptr;
    return p && p->HasFusion() && p->GetFusionConst().downed.Active();
}

void FusionMgr::SendDowned(Player* player, std::vector<uint8_t> const& body) const
{
    if (player->GetSession())
        SendChannel(player, Downed::kChannelDowned, body);
}

// â”€â”€ Going down, getting up, dying â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

uint32 FusionMgr::DownedDamage(Unit* attacker, Player* victim, uint32 damage)
{
    if (!m_enabled || !victim->HasFusion() || !victim->IsAlive() || !victim->IsInWorld())
        return damage;
    FusionPlayerData& d = victim->GetFusion();
    // Not on a taxi (nothing to crawl on); a duel already ends at 1 health on its own.
    bool const mayGoDown = !victim->IsTaxiFlying();
    Downed::DamageFate const f = Downed::OnDamage(d.downed.Active(), mayGoDown, d.downed.finisherLeft,
        victim->GetHealth(), damage);
    switch (f.fate)
    {
        case Downed::Fate::GoDown:
            // A class option may hold them up a while (Spirit of Redemption): at 1 health.
            if (sFusionClasses.PreventsDown(victim))
                return f.damage;
            GoDown(victim, attacker);
            return f.damage;
        case Downed::Fate::Absorbed:
            d.downed.finisherLeft = f.poolLeft;
            return 0;
        case Downed::Fate::Finished:
            d.downed.finisherLeft = 0;
            if (d.downedCause == 0)
                d.downedCause = uint8(Downed::DeathCause::Finished);
            // The downed state decided on a death: a GM's god mode (GM.CheatGod gives it at login)
            // would hold the body at 1 health and leave it down for good. Lifted for this blow;
            // EndDowned gives it back.
            if (uint32 const god = victim->GetInvincibilityHpThreshold())
            {
                d.downedGodHp = god;
                victim->SetInvincibilityHpThreshold(0);
            }
            return f.damage;
        default:
            return f.damage;
    }
}

uint16 FusionMgr::LastStandPistolOf(Player const* player) const
{
    uint16 secondary = 0;
    Fusion::WeaponClass cls = Fusion::WeaponClass::AssaultRifle;
    if (Fusion::ClassLoadout const* c = player->GetFusionConst().Equipped())
        if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(c->secondary))
        {
            secondary = c->secondary;
            cls = w->weaponClass;
        }
    return Downed::LastStandPistol(secondary, cls);
}

void FusionMgr::GoDown(Player* player, Unit* by)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    bool const finalStand = DeathstreakReady(d, Fusion::PerkId::FinalStand) && !d.finalStandUsed;
    d.downed.GoDown(now, player->GetMaxHealth(), uint32(std::max(0.f, player->GetStat(STAT_STAMINA))),
        d.perks.lastStand, finalStand);
    // Last Stand Pro: longer to be revived.
    if (d.downed.armed)
        d.downed.Extend(Fusion::LastStandExtraMs(d.perks) + d.augments.lastStandMs);   // and Last Rites' glyph
    if (d.downed.finalStand)
        d.finalStandUsed = true;
    d.downedBy = by && by != player ? by->GetObjectGuid() : ObjectGuid();
    d.downedCause = 0;
    d.downedPistol = d.downed.armed ? LastStandPistolOf(player) : 0;
    d.nextDownedLookMs = now;

    // Whatever was going on stops: a cast, the swing, a mount, a kit at work on someone else.
    player->InterruptNonMeleeSpells(false);
    player->AttackStop();
    if (player->IsMounted())
        player->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    if (d.kitUse.Active())
        StopKit(player, Downed::KitState::Stopped);
    // Lying down to everyone (and a resurrection spell's target): see the file's comment.
    player->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
    d.stance = Fusion::Stance::Prone;
    d.sprinting = false;
    UpdateDownedSpeed(player);

    SendDownedState(player, now);
    TellDownedNear(player, true);
    // Reincarnation or a soulstone may stand them back up (FusionClassAbilities.cpp).
    sFusionClasses.OnWentDown(player);
    uint32 const secs = d.downed.totalMs / 1000;
    ChatHandler chat(player);
    if (d.downed.finalStand)
        chat.PSendSysMessage("Downed. Final Stand: hold on %u seconds and you get back up.", uint32(Downed::kFinalStandSeconds));
    else if (d.downed.armed)
        chat.PSendSysMessage("Downed. Last Stand: your pistol is up. %u seconds to be revived.", secs);
    else
        chat.PSendSysMessage("Downed: %u seconds to be revived.", secs);
}

void FusionMgr::SendDownedState(Player* player, uint32 now)
{
    FusionPlayerData const& d = player->GetFusionConst();
    Downed::DownedMsg m;
    if (d.downed.armed)
        m.flags |= Downed::DOWNED_ARMED;
    if (d.downed.finalStand)
        m.flags |= Downed::DOWNED_FINAL_STAND;
    if (FindKit(player))
        m.flags |= Downed::DOWNED_HAS_KIT;
    m.remainingMs = d.downed.RemainingMs(now);
    m.totalMs = d.downed.totalMs;
    m.pistol = d.downedPistol;
    SendDowned(player, Downed::EncodeDowned(m));
}

void FusionMgr::TellDownedNear(Player* player, bool downed)
{
    Map* map = player->GetMap();
    if (!map)
        return;
    std::vector<uint8_t> const body = Downed::EncodeNear(player->GetObjectGuid().GetRawValue(), downed);
    Fusion::Vec3 const at = FeetOf(player);
    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* p = it->getSource();
        if (!p || p == player || !p->IsInWorld() || !p->HasFusion() || !p->GetSession())
            continue;
        if ((FeetOf(p) - at).Length() <= kTellRadius)
            SendDowned(p, body);
    }
}

void FusionMgr::UpdateDownedSpeed(Player* player)
{
    player->UpdateSpeed(MOVE_RUN, false);
    player->UpdateSpeed(MOVE_RUN_BACK, false);
    player->UpdateSpeed(MOVE_WALK, false);
    player->UpdateSpeed(MOVE_SWIM, false);
    player->UpdateSpeed(MOVE_SWIM_BACK, false);
}

void FusionMgr::ReviveDowned(Player* player, Downed::UpReason reason, ObjectGuid by, float healthShare)
{
    FusionPlayerData& d = player->GetFusion();
    if (!d.downed.Active() || !player->IsAlive())
        return;
    d.downed.Clear();
    d.downedBy.Clear();
    d.downedPistol = 0;
    d.downedCause = 0;
    player->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
    uint32 const health = Downed::ReviveHealth(player->GetMaxHealth(), healthShare);
    player->SetHealth(health);
    UpdateDownedSpeed(player);

    Downed::UpMsg m;
    m.reason = reason;
    m.healthPct = uint8(std::min<uint32>(100, health * 100 / std::max<uint32>(1, player->GetMaxHealth())));
    m.by = by.GetRawValue();
    SendDowned(player, Downed::EncodeUp(m));
    TellDownedNear(player, false);
    CountStat(player, {Fusion::Stat::TimesRevived, Fusion::kAnyKey});

    ChatHandler chat(player);
    Player* reviver = !by.IsEmpty() && by != player->GetObjectGuid() ? ObjectAccessor::FindPlayer(by) : nullptr;
    if (reviver)
        chat.PSendSysMessage("%s brought you back up.", reviver->GetName());
    else if (reason == Downed::UpReason::FinalStand)
        chat.SendSysMessage("Final Stand: you are back on your feet.");
    else
        chat.SendSysMessage("You are back on your feet.");
}

void FusionMgr::EndDowned(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    if (d.kitUse.Active())
        StopKit(player, Downed::KitState::Stopped);
    if (!d.downed.Active())
        return;
    Downed::DeathCause const cause = d.downedCause ? Downed::DeathCause(d.downedCause) : Downed::DeathCause::Finished;
    d.downed.Clear();
    d.downedBy.Clear();
    d.downedPistol = 0;
    d.downedCause = 0;
    player->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
    if (d.downedGodHp)
    {
        player->SetInvincibilityHpThreshold(d.downedGodHp);
        d.downedGodHp = 0;
    }
    UpdateDownedSpeed(player);
    SendDowned(player, Downed::EncodeDied(cause));
    TellDownedNear(player, false);
}

void FusionMgr::BleedOut(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    d.downed.finisherLeft = 0;
    d.downedCause = uint8(Downed::DeathCause::BledOut);
    // Whoever put them down gets the kill, when still about.
    Unit* killer = player;
    if (!d.downedBy.IsEmpty())
        if (Unit* by = ObjectAccessor::GetUnit(*player, d.downedBy))
            if (by->IsInWorld() && by->IsAlive() && by->GetMap() == player->GetMap())
                killer = by;
    killer->DealDamage(player, std::max<uint32>(1, player->GetHealth()), nullptr, DIRECT_DAMAGE,
        SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
    // Anything that kept the body alive (a GM's invincibility) still ends the state.
    if (player->IsAlive() && d.downed.Active())
        player->DealDamage(player, std::max<uint32>(1, player->GetHealth()), nullptr, DIRECT_DAMAGE,
            SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
}

void FusionMgr::UpdateDowned(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    if (d.kitUse.Active())
        UpdateKitUse(player, now);
    if (!d.downed.Active())
        return;
    // Dead some other way (the void, a GM): the state goes with it.
    if (!player->IsAlive())
    {
        EndDowned(player);
        return;
    }
    // Held at 1 health: healing and regeneration do not bring a downed player up.
    if (player->GetHealth() > 1)
        player->SetHealth(1);
    if (player->GetVictim())
        player->AttackStop();
    if (player->IsNonMeleeSpellCasted(false))
        player->InterruptNonMeleeSpells(false);
    if (player->IsMounted())
        player->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    switch (d.downed.Update(now))
    {
        case Downed::Tick::GetUp:
            ReviveDowned(player, Downed::UpReason::FinalStand, player->GetObjectGuid(), Downed::kFinalStandHealthShare);
            return;
        case Downed::Tick::BledOut:
            BleedOut(player);
            return;
        default:
            break;
    }
    if (int32(now - d.nextDownedLookMs) >= 0)
    {
        d.nextDownedLookMs = now + 500;
        TurnCreaturesAway(player, now);
    }
}

void FusionMgr::TurnCreaturesAway(Player* player, uint32 now)
{
    FusionPlayerData const& d = player->GetFusionConst();
    // A copy: dropping threat can change who attacks whom.
    std::vector<Unit*> const attackers(player->GetAttackers().begin(), player->GetAttackers().end());
    for (Unit* u : attackers)
    {
        Creature* c = u ? u->ToCreature() : nullptr;
        if (!c || !c->IsAlive() || c->IsPet() || !c->CanHaveThreatList())
            continue;
        bool other = false;
        for (HostileReference* ref : c->GetThreatManager().getThreatList())
        {
            Unit* t = ref ? ref->getTarget() : nullptr;
            if (t && t != player && t->IsAlive() && !IsDowned(t))
            {
                other = true;
                break;
            }
        }
        // To nothing: the downed player's threat drops to nothing, so anyone else comes first.
        if (Downed::CreatureLeaves(d.downed.sinceMs, now, other))
            c->GetThreatManager().modifyThreatPercent(player, -100);
    }
}

// â”€â”€ Resurrection spells â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

bool FusionMgr::ReviveBySpell(WorldObject* caster, Unit* target)
{
    if (!m_enabled || !IsDowned(target))
        return false;
    Player* p = const_cast<Player*>(target->ToPlayer());
    ReviveDowned(p, Downed::UpReason::Spell, caster ? caster->GetObjectGuid() : ObjectGuid(), Downed::kSpellReviveShare);
    return true;
}

// â”€â”€ Revival kits â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

Item* FusionMgr::FindKit(Player* player) const
{
    // The coil first (it is quicker), then the kit.
    for (uint32 entry : {Downed::kRevivalCoil, Downed::kFieldRevivalKit})
    {
        for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                if (item->GetEntry() == entry)
                    return item;
        for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
            if (Bag* bag = static_cast<Bag*>(player->GetItemByPos(INVENTORY_SLOT_BAG_0, i)))
                if (Item* item = bag->GetItemByEntry(entry))
                    return item;
    }
    return nullptr;
}

bool FusionMgr::UseItemDowned(Player* player, Item* item)
{
    if (!m_enabled || !player->HasFusion() || !item)
        return false;
    uint32 const entry = item->GetEntry();
    if (Downed::IsDownedItem(entry))
    {
        StartKit(player, item, player->GetSelectionGuid());
        return true;
    }
    // A downed player uses nothing else.
    if (player->GetFusionConst().downed.Active())
    {
        player->SendEquipError(EQUIP_ERR_YOU_ARE_DEAD, item, nullptr);
        return true;
    }
    return false;
}

void FusionMgr::HandleDownedChannel(Player* player, std::vector<uint8> const& body)
{
    Downed::UseRequest r;
    if (!player->IsInWorld() || !Downed::DecodeUse(body, r))
        return;
    Item* item = r.item ? player->GetItemByGuid(ObjectGuid(r.item)) : FindKit(player);
    if (!item || !Downed::IsDownedItem(item->GetEntry()))
    {
        ChatHandler(player).SendSysMessage("You carry no revival kit.");
        Downed::KitMsg m;
        m.state = Downed::KitState::Refused;
        SendDowned(player, Downed::EncodeKit(m));
        return;
    }
    StartKit(player, item, r.target ? ObjectGuid(r.target) : player->GetSelectionGuid());
}

void FusionMgr::StartKit(Player* player, Item* item, ObjectGuid targetGuid)
{
    FusionPlayerData& d = player->GetFusion();
    ChatHandler chat(player);
    auto refuse = [&](char const* why)
    {
        chat.SendSysMessage(why);
        Downed::KitMsg m;
        m.state = Downed::KitState::Refused;
        SendDowned(player, Downed::EncodeKit(m));
    };
    if (!player->IsAlive())
        return;
    uint32 const entry = item->GetEntry();
    // A manual makes a kit (its recipe's skill and reagents).
    if (uint32 const product = Downed::ManualProduct(entry))
    {
        if (d.downed.Active())
            return refuse("Not while you are down.");
        CraftFromManual(player, product);
        return;
    }
    Downed::Kit const* kit = Downed::KitInfo(entry);
    if (!kit)
        return;
    if (d.kitUse.Active())
        return refuse("You are already using a revival kit.");
    // The target: a downed player asked for (or selected), else yourself.
    Player* target = player;
    if (!targetGuid.IsEmpty() && targetGuid != player->GetObjectGuid())
        if (Player* t = ObjectAccessor::FindPlayer(targetGuid))
            if (t->IsInWorld() && t->GetMap() == player->GetMap() && IsDowned(t))
                target = t;
    Downed::UseContext c;
    c.userDowned = d.downed.Active();
    c.onSelf = target == player;
    c.targetDowned = IsDowned(target);
    c.friendly = !c.onSelf && !player->IsHostileTo(target) && !target->IsHostileTo(player);
    c.distance = c.onSelf ? 0.f : player->GetDistance(target);
    switch (Downed::CanUseKit(*kit, c))
    {
        case Downed::UseCheck::Ok:
            break;
        case Downed::UseCheck::TargetNotDowned:
            return refuse("Nobody here is down: target a downed ally, or use it on yourself while down.");
        case Downed::UseCheck::TooFar:
            return refuse("Too far away: get next to them.");
        case Downed::UseCheck::NotFriendly:
            return refuse("You can only revive an ally.");
        case Downed::UseCheck::UserDowned:
            return refuse("While down you can only revive yourself.");
    }
    uint32 const now = WorldTimer::getMSTime();
    uint32 const ms = Downed::UseMs(*kit, c.onSelf);
    d.kitUse.Start(item->GetObjectGuid().GetRawValue(), c.onSelf ? 0 : target->GetObjectGuid().GetRawValue(),
        now, ms, FeetOf(player));
    Downed::KitMsg m;
    m.state = Downed::KitState::Using;
    m.selfUse = c.onSelf;
    m.other = c.onSelf ? 0 : target->GetObjectGuid().GetRawValue();
    m.durationMs = ms;
    SendDowned(player, Downed::EncodeKit(m));
    if (!c.onSelf)
    {
        target->GetFusion().kitWorker = player->GetObjectGuid();
        Downed::KitMsg t = m;
        t.other = player->GetObjectGuid().GetRawValue();
        SendDowned(target, Downed::EncodeKit(t));
        chat.PSendSysMessage("Using %s on %s: hold still for %.1f seconds.", ItemName(entry), target->GetName(), ms / 1000.f);
        ChatHandler(target).PSendSysMessage("%s is reviving you.", player->GetName());
    }
    else
        chat.PSendSysMessage("Using %s on yourself: hold still for %.1f seconds.", ItemName(entry), ms / 1000.f);
}

void FusionMgr::StopKit(Player* player, Downed::KitState state)
{
    FusionPlayerData& d = player->GetFusion();
    if (!d.kitUse.Active())
        return;
    uint64 const target = d.kitUse.target;
    d.kitUse.Clear();
    Downed::KitMsg m;
    m.state = state;
    m.other = target;
    m.selfUse = target == 0;
    SendDowned(player, Downed::EncodeKit(m));
    if (target)
        if (Player* t = ObjectAccessor::FindPlayer(ObjectGuid(target)))
        {
            if (t->HasFusion() && t->GetFusionConst().kitWorker == player->GetObjectGuid())
                t->GetFusion().kitWorker.Clear();
            Downed::KitMsg tm = m;
            tm.selfUse = false;
            tm.other = player->GetObjectGuid().GetRawValue();
            SendDowned(t, Downed::EncodeKit(tm));
        }
}

void FusionMgr::UpdateKitUse(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    Downed::KitUse const u = d.kitUse;
    bool const onSelf = u.target == 0;
    Player* target = onSelf ? player : ObjectAccessor::FindPlayer(ObjectGuid(u.target));
    ChatHandler chat(player);
    if (!player->IsAlive())
    {
        StopKit(player, Downed::KitState::Stopped);
        return;
    }
    if (!target || !target->IsInWorld() || target->GetMap() != player->GetMap() || !IsDowned(target))
    {
        StopKit(player, Downed::KitState::Stopped);
        return;
    }
    if (!onSelf && player->GetDistance(target) > Downed::kKitReach + 1.f)
    {
        chat.SendSysMessage("Too far away: the revive stopped.");
        StopKit(player, Downed::KitState::Stopped);
        return;
    }
    switch (u.Progress(now, FeetOf(player)))
    {
        case Downed::KitProgress::Moved:
            chat.SendSysMessage("You moved: the revive stopped.");
            StopKit(player, Downed::KitState::Stopped);
            return;
        case Downed::KitProgress::Working:
            return;
        case Downed::KitProgress::Done:
            break;
    }
    Item* item = player->GetItemByGuid(ObjectGuid(u.item));
    Downed::Kit const* kit = item ? Downed::KitInfo(item->GetEntry()) : nullptr;
    if (!kit)
    {
        StopKit(player, Downed::KitState::Stopped);
        return;
    }
    // The kit is used up; then the patient is up.
    uint32 one = 1;
    player->DestroyItemCount(item, one, true);
    StopKit(player, Downed::KitState::Done);
    ReviveDowned(target, Downed::UpReason::Kit, player->GetObjectGuid(), kit->healthShare);
}

void FusionMgr::CraftFromManual(Player* player, uint32 product)
{
    for (size_t i = 0; i < m_crafts.size(); ++i)
        if (m_crafts[i].recipe.product == product)
        {
            CraftPart(player, i);
            return;
        }
    ChatHandler(player).SendSysMessage("This recipe is not known to the world yet (fusion_recipe).");
}

// â”€â”€ Game master tests â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void FusionMgr::ForceDown(Player* player)
{
    if (!player->IsAlive() || IsDowned(player))
        return;
    player->DealDamage(player, player->GetHealth(), nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
}

void FusionMgr::ForceUp(Player* player, Player* by)
{
    ReviveDowned(player, Downed::UpReason::GameMaster, by ? by->GetObjectGuid() : ObjectGuid(), Downed::kSpellReviveShare);
}

void FusionMgr::ForceBleedOut(Player* player)
{
    if (IsDowned(player))
        BleedOut(player);
}

bool FusionMgr::ReviveDownedBy(Player* player, ObjectGuid by, float healthShare)
{
    if (!m_enabled || !IsDowned(player))
        return false;
    ReviveDowned(player, Downed::UpReason::Spell, by, healthShare);
    return true;
}

void FusionMgr::ExtendDowned(Player* player, uint32 ms)
{
    if (!m_enabled || !IsDowned(player))
        return;
    player->GetFusion().downed.Extend(ms);
    SendDownedState(player, WorldTimer::getMSTime());
}

void FusionMgr::GoDownNow(Player* player)
{
    if (!m_enabled || !player->IsAlive() || IsDowned(player) || !player->HasFusion())
        return;
    player->SetHealth(1);
    GoDown(player, nullptr);
}

// .fusion down [up | bleed]: go down now (or the selected player), get them up, or bleed out now.
bool ChatHandler::HandleFusionDownCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player) || !player->HasFusion())
    {
        SendSysMessage("Fusion: only an admin may do that.");
        SetSentErrorMessage(true);
        return false;
    }
    Player* target = GetSelectedPlayer();
    if (!target || !target->HasFusion())
        target = player;
    std::string const arg = args ? std::string(args) : std::string();
    if (arg == "up")
    {
        if (!sFusionMgr.IsDowned(target))
        {
            PSendSysMessage("Fusion: %s is not down.", target->GetName());
            return true;
        }
        sFusionMgr.ForceUp(target, player);
    }
    else if (arg == "bleed")
        sFusionMgr.ForceBleedOut(target);
    else
        sFusionMgr.ForceDown(target);
    return true;
}
