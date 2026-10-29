/*
 * World of Modern Warfare: weapon pickups (fusion/Pickups.h) and Enchanting's specialized rounds
 * (fusion/Rounds.h) in the game. A dead NPC gunner or player drops the gun in hand; the server
 * keeps the guns on the ground, tells players near of them (world channel op 60), and on a
 * player's request (op 61, F held on one) checks the reach and puts it in the slot of its class
 * for this life (op 62 tells the client). Enchanted rounds are items an enchanter learns and makes
 * at an enchanting trainer; used, they load the gun in hand, and the gun's hits carry the round's
 * effect (HandleShots calls RoundDamageScale and RoundHit).
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "WorldSession.h"
#include "fusion/Arsenal.h"

#include <sstream>

namespace Pickups = Fusion::Pickups;
namespace Rounds = Fusion::Rounds;

namespace
{
    Fusion::Vec3 Feet(Unit const* u) { return {u->GetPositionX(), u->GetPositionY(), u->GetPositionZ()}; }

    // Vanilla's Chilled (Improved Blizzard, rank 1): 30% slower for 1.5 seconds, frost school.
    constexpr uint32 kChilledSpell = 12484;

    // The enchanting trainer's gossip line and its actions.
    constexpr uint32 kRoundsSender = 0x5257;    // "RW"
    constexpr uint32 kRoundsMenu = 1;
    constexpr uint32 kLearnBase = 100;
    constexpr uint32 kMakeBase = 200;

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

    std::string ItemName(uint32 item)
    {
        if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item))
            if (proto->Name1)
                return proto->Name1;
        return "item " + std::to_string(item);
    }

    std::string GunName(Fusion::Catalog const& catalog, uint16 id)
    {
        Fusion::WeaponProfile const* w = catalog.Weapons().Find(id);
        return w ? Fusion::GunDisplayName(w->name) : "gun";
    }

    Rounds::TargetFacts FactsOf(Unit const* u)
    {
        Rounds::TargetFacts t;
        t.isPlayer = u->IsPlayer();
        t.type = Fusion::CreatureType(u->GetCreatureType());
        if (Creature const* c = u->ToCreature())
        {
            t.slowImmune = c->IsWorldBoss();
            CreatureInfo const* ci = c->GetCreatureInfo();
            t.fireElemental = ci && (ci->school_immune_mask & SPELL_SCHOOL_MASK_FIRE);
        }
        t.maxMana = u->GetPowerType() == POWER_MANA ? u->GetMaxPower(POWER_MANA) : 0;
        return t;
    }
}

void FusionMgr::LoadArmory()
{
    // The characters table (src/fusion/include/fusion/Rounds.h), made here as no migration file may be.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_round_recipe` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`recipe` int(10) unsigned NOT NULL COMMENT 'Rounds::Recipes id',"
        "PRIMARY KEY (`guid`, `recipe`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
}

// ── Pickups ─────────────────────────────────────────────────────────────────────────────────

bool FusionMgr::PickupAllows(FusionPlayerData const& d, uint16 gunId)
{
    return d.pickups.Holding(gunId) != nullptr;
}

uint32 FusionMgr::DropGun(Map* map, Fusion::Vec3 at, float yaw, Pickups::Gun const& gun, uint32 mag, uint32 reserve)
{
    Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(gun.id);
    Pickups::Slot slot;
    if (!map || !w || !Pickups::SlotOf(*w, slot) || !Pickups::WorthDropping(mag, reserve))
        return 0;
    Pickups::Dropped d;
    d.mapId = map->GetId();
    d.instanceId = map->GetInstanceId();
    // On the ground below where it fell (a body dies standing; the gun drops to its feet).
    float const ground = map->GetHeight(at.x, at.y, at.z + 2.f);
    d.pos = {at.x, at.y, ground > INVALID_HEIGHT ? ground + 0.05f : at.z};
    d.yaw = yaw;
    d.gun = gun;
    d.mag = mag;
    d.reserve = reserve;
    uint32 const now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(m_droppedLock);
    m_dropped.Expire(now);
    return m_dropped.Add(std::move(d), now);
}

void FusionMgr::DropGunOf(Unit* victim)
{
    if (!m_enabled || !victim || !victim->IsInWorld())
        return;
    // A little to the side of the body, turned at random, as a dropped gun lies.
    float const yaw = frand(0.f, 2.f * float(M_PI));
    Fusion::Vec3 const at = Feet(victim) + Fusion::Vec3{std::cos(yaw) * 0.6f, std::sin(yaw) * 0.6f, 0.f};
    if (Creature* c = victim->ToCreature())
    {
        if (c->IsPet() || c->m_fusionGunRole != uint8(Fusion::GunRole::Gunner) || !c->m_fusionGunId)
            return;
        Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(c->m_fusionGunId);
        if (!w)
            return;
        uint32 mag = 0, reserve = 0;
        Pickups::NpcAmmo(*w, mag, reserve);
        DropGun(c->GetMap(), at, yaw, {w->id, "", "", 0}, mag, reserve);
        return;
    }
    Player* p = victim->ToPlayer();
    if (!p || !p->HasFusion())
        return;
    FusionPlayerData& d = p->GetFusion();
    Fusion::WeaponProfile const* base = m_catalog.Weapons().Find(d.heldWeaponId);
    Pickups::Slot slot;
    if (!base || !Pickups::SlotOf(*base, slot))
        return;
    // The gun as it was held: a pickup's attachments, else the kit's.
    Pickups::Gun gun{base->id, "", "", 0};
    if (Pickups::Gun const* held = d.pickups.Holding(base->id))
        gun = *held;
    else if (Fusion::ClassLoadout const* c = d.Equipped())
    {
        if (c->primary == base->id)
            gun = {base->id, c->primaryAtt1, c->primaryAtt2, c->primaryCamo};
        else if (c->secondary == base->id)
            gun = {base->id, c->secondaryAtt, c->secondaryAtt2, 0};
    }
    std::optional<Fusion::WeaponProfile> const w = EffectiveWeapon(p, base->id);
    if (!w)
        return;
    Fusion::GunAmmo& ammo = d.AmmoFor(*w);
    DropGun(p->GetMap(), at, yaw, gun, ammo.Mag(), ammo.Reserve());
}

void FusionMgr::ArmoryOnDeath(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    if (d.rounds.round != Rounds::Round::None)
    {
        d.rounds = {};
        SendRounds(player);
    }
    if (d.pickups.Any())
        ClearPickups(player);
}

void FusionMgr::ClearPickups(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    if (!d.pickups.Any())
        return;
    // The picked-up guns' rounds go with them; the kit's guns start afresh.
    for (auto const& g : d.pickups.slots)
        if (g)
        {
            d.ammo.erase(g->id);
            d.ammo.erase(uint16(g->id | FusionPlayerData::kLeftGun));
        }
    if (Fusion::ClassLoadout const* c = d.Equipped())
        for (uint16 const gun : {c->primary, c->secondary})
        {
            d.ammo.erase(gun);
            d.ammo.erase(uint16(gun | FusionPlayerData::kLeftGun));
        }
    d.pickups.Clear();
    SendHeld(player);
}

void FusionMgr::SendHeld(Player* player)
{
    if (player->GetSession())
        SendChannel(player, Fusion::Packets::kChannelWorld, Pickups::EncodeHeld(player->GetFusionConst().pickups));
}

void FusionMgr::HandleArmoryChannel(Player* player, std::vector<uint8> const& body)
{
    uint32 id = 0;
    if (Pickups::DecodeTake(body, id))
        TakeGun(player, id);
}

void FusionMgr::TakeGun(Player* player, uint32 id)
{
    if (!m_enabled || !player->IsInWorld() || !player->IsAlive() || !player->HasFusion())
        return;
    FusionPlayerData& d = player->GetFusion();
    Pickups::Dropped got;
    Pickups::Field::Take result;
    {
        std::lock_guard<std::mutex> lock(m_droppedLock);
        // Reach from the feet, the way the gun lies on the ground.
        result = m_dropped.TakeGun(id, player->GetMapId(), player->GetInstanceId(), Feet(player),
            WorldTimer::getMSTime(), got);
    }
    if (result != Pickups::Field::Take::Ok)
    {
        SendChannel(player, Fusion::Packets::kChannelWorld, Pickups::EncodeRefused(result));
        return;
    }
    Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(got.gun.id);
    Pickups::Slot slot;
    if (!w || !Pickups::SlotOf(*w, slot))
        return;
    // MW2's swap: the gun in that slot drops where the player stands, with its rounds.
    uint16 const before = d.pickups.For(slot) ? d.pickups.For(slot)->id
        : d.Equipped() ? (slot == Pickups::Slot::Primary ? d.Equipped()->primary : d.Equipped()->secondary) : 0;
    if (Fusion::WeaponProfile const* old = before ? m_catalog.Weapons().Find(before) : nullptr)
    {
        Pickups::Gun oldGun{old->id, "", "", 0};
        if (Pickups::Gun const* held = d.pickups.For(slot))
            oldGun = *held;
        else if (Fusion::ClassLoadout const* c = d.Equipped())
            oldGun = slot == Pickups::Slot::Primary ? Pickups::Gun{old->id, c->primaryAtt1, c->primaryAtt2, c->primaryCamo}
                : Pickups::Gun{old->id, c->secondaryAtt, c->secondaryAtt2, 0};
        if (std::optional<Fusion::WeaponProfile> const ow = EffectiveWeapon(player, old->id))
        {
            Fusion::GunAmmo& ammo = d.AmmoFor(*ow);
            DropGun(player->GetMap(), Feet(player), player->GetOrientation() + float(M_PI) / 2.f, oldGun, ammo.Mag(), ammo.Reserve());
        }
        d.ammo.erase(old->id);
        d.ammo.erase(uint16(old->id | FusionPlayerData::kLeftGun));
    }
    d.pickups.slots[size_t(slot)] = got.gun;
    // Its own rounds: what it had on the ground, held to its magazine and reserve.
    if (std::optional<Fusion::WeaponProfile> const nw = EffectiveWeapon(player, got.gun.id))
    {
        d.ammo.erase(got.gun.id);
        d.ammo.erase(uint16(got.gun.id | FusionPlayerData::kLeftGun));
        d.AmmoFor(*nw).Set(got.mag, got.reserve);
        SendAmmo(player, *nw);
    }
    SendHeld(player);
    d.droppedSig = 0;       // the list near changed: send it again
    ChatHandler(player).PSendSysMessage("Picked up the %s.", GunName(m_catalog, got.gun.id).c_str());
}

void FusionMgr::UpdateArmory(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    // The guns near, when that changed.
    if (int32(now - d.nextDroppedMs) >= 0 && player->IsInWorld() && player->GetSession())
    {
        d.nextDroppedMs = now + Pickups::kTellEveryMs;
        std::vector<Pickups::Dropped> nearby;
        {
            std::lock_guard<std::mutex> lock(m_droppedLock);
            m_dropped.Expire(now);
            nearby = m_dropped.Near(player->GetMapId(), player->GetInstanceId(), Feet(player), Pickups::kTellRadius,
                Pickups::kMaxListed);
        }
        uint32 const sig = Pickups::ListSignature(nearby);
        if (sig != d.droppedSig)
        {
            d.droppedSig = sig;
            SendChannel(player, Fusion::Packets::kChannelWorld, Pickups::EncodeGuns(nearby));
        }
    }
    // A timed load runs out.
    if (d.rounds.round != Rounds::Round::None && !d.rounds.Active(now))
    {
        d.rounds = {};
        SendRounds(player);
    }
    // The incendiary burns this player set.
    if (!d.burns.empty() && player->IsInWorld())
    {
        for (Rounds::BurnTick const& t : Rounds::TickBurns(d.burns, now))
        {
            Unit* target = player->GetMap()->GetUnit(ObjectGuid(t.target));
            if (!target || !target->IsAlive() || !target->IsInWorld() || !player->IsValidAttackTarget(target))
                continue;
            FusionHitSource source;
            source.weaponId = t.weapon;
            Hit(player, target, std::max<uint32>(1, uint32(t.damage + 0.5f)), source);
        }
    }
}

// ── Enchanted rounds ────────────────────────────────────────────────────────────────────────

bool FusionMgr::LoadRounds(Player* player, Rounds::Round round, uint8 grade)
{
    FusionPlayerData& d = player->GetFusion();
    Fusion::WeaponProfile const* base = m_catalog.Weapons().Find(d.heldWeaponId);
    Pickups::Slot slot;
    if (!base || !Pickups::SlotOf(*base, slot) || !Fusion::FiresRounds(base->weaponClass)
        || base->weaponClass == Fusion::WeaponClass::Launcher)
        return false;
    std::optional<Fusion::WeaponProfile> const w = EffectiveWeapon(player, base->id);
    if (!w)
        return false;
    uint32 const now = WorldTimer::getMSTime();
    d.rounds = Rounds::Load(round, grade, w->id, w->magSize, now);
    if (!d.rounds.Active(now))
        return false;
    SendRounds(player);
    Rounds::RoundDef const& def = Rounds::Def(round);
    if (d.rounds.timed)
        ChatHandler(player).PSendSysMessage("%s loaded into the %s for %u seconds.", def.name,
            GunName(m_catalog, w->id).c_str(), d.rounds.Left(now) / 1000);
    else
        ChatHandler(player).PSendSysMessage("%s loaded into the %s for %u magazines.", def.name,
            GunName(m_catalog, w->id).c_str(), d.rounds.roundsLeft / std::max<uint32>(1, w->magSize));
    return true;
}

void FusionMgr::UseRoundItem(Player* player, Item* item)
{
    if (!m_enabled || !player->HasFusion() || !item)
        return;
    Rounds::RoundItem const it = Rounds::ItemRound(item->GetEntry());
    if (it.round == Rounds::Round::None)
        return;
    if (!player->IsAlive())
    {
        player->GetSession()->SendNotification("You can't do that while dead.");
        return;
    }
    if (!LoadRounds(player, it.round, it.grade))
    {
        player->GetSession()->SendNotification("Hold a gun to load rounds into it.");
        return;
    }
    uint32 count = 1;
    player->DestroyItemCount(item, count, true);
}

void FusionMgr::SendRounds(Player* player)
{
    if (player->GetSession())
        SendChannel(player, Fusion::Packets::kChannelWorld,
            Rounds::EncodeLoaded(player->GetFusionConst().rounds, WorldTimer::getMSTime()));
}

float FusionMgr::RoundDamageScale(Unit* target, Rounds::Round round) const
{
    if (round == Rounds::Round::None)
        return 1.f;
    Rounds::HitEffect const e = Rounds::OnHit(round, FactsOf(target));
    float scale = e.damageMultiplier;
    // Armor-piercing gives back part of the bullet armor cut HealthDamage took (players' armor;
    // creatures take none yet).
    if (e.armorBypass > 0.f)
        if (Player const* tp = target->ToPlayer())
            scale *= Rounds::ArmorBypassScale(Fusion::Scale::PlayerArmorCut(float(tp->GetArmor()), tp->GetLevel()), e.armorBypass);
    return scale;
}

void FusionMgr::RoundHit(Player* shooter, Unit* target, Rounds::Round round, uint32 damage, uint16 weaponId)
{
    if (round == Rounds::Round::None || !target->IsAlive())
        return;
    Rounds::HitEffect const e = Rounds::OnHit(round, FactsOf(target));
    uint32 const now = WorldTimer::getMSTime();
    if (e.burnShare > 0.f)
        Rounds::AddBurn(shooter->GetFusion().burns, target->GetObjectGuid().GetRawValue(), weaponId,
            float(damage) * e.burnShare, e.burnMs, now);
    if (e.slow)
        shooter->CastSpell(target, kChilledSpell, true);
    if (e.manaBurnShare > 0.f && target->GetMaxHealth())
    {
        uint32 const burned = Rounds::ManaBurned(float(damage) / float(target->GetMaxHealth()),
            target->GetMaxPower(POWER_MANA), e.manaBurnShare);
        if (burned)
            target->ModifyPower(POWER_MANA, -int32(burned));
    }
}

// ── The enchanting trainer ──────────────────────────────────────────────────────────────────

void FusionMgr::LoadRoundRecipes(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    if (d.roundRecipesLoaded)
        return;
    d.roundRecipesLoaded = true;
    d.roundRecipes.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `recipe` FROM `character_fusion_round_recipe` WHERE `guid` = %u", player->GetGUIDLow()))
    {
        do
            d.roundRecipes.insert(r->Fetch()[0].GetUInt32());
        while (r->NextRow());
    }
}

void FusionMgr::AddEnchantingGossip(Player* player, Creature* creature)
{
    if (!m_enabled || !player->HasFusion() || !creature)
        return;
    CreatureInfo const* ci = creature->GetCreatureInfo();
    if (!ci || !Rounds::IsEnchantingTrainer(uint8(ci->trainer_type), ci->subname))
        return;
    if (!player->HasSkill(Rounds::kSkillEnchanting) && !IsAdmin(player))
        return;
    player->PlayerTalkClass->GetGossipMenu().AddMenuItem(GOSSIP_ICON_TRAINER, "Specialized rounds for guns",
        kRoundsSender, kRoundsMenu, "", false);
}

void FusionMgr::EnchantingMenu(Player* player, Creature* creature)
{
    LoadRoundRecipes(player);
    FusionPlayerData const& d = player->GetFusionConst();
    bool const gm = IsAdmin(player);
    uint16 const skill = player->HasSkill(Rounds::kSkillEnchanting) ? player->GetSkillValue(Rounds::kSkillEnchanting) : 0;
    PlayerMenu* menu = player->PlayerTalkClass;
    menu->ClearMenus();
    std::vector<Rounds::Recipe> const& all = Rounds::Recipes();
    for (size_t i = 0; i < all.size(); ++i)
    {
        Rounds::Recipe const& r = all[i];
        std::ostringstream o;
        if (d.roundRecipes.count(r.craft.id) || gm)
        {
            o << "Make " << ItemName(r.craft.product) << " x" << r.craft.productCount << " (";
            for (size_t k = 0; k < r.craft.reagents.size(); ++k)
                o << (k ? ", " : "") << r.craft.reagents[k].count << " " << ItemName(r.craft.reagents[k].item);
            o << ")";
            menu->GetGossipMenu().AddMenuItem(GOSSIP_ICON_INTERACT_1, o.str(), kRoundsSender, uint32(kMakeBase + i), "", false);
        }
        else if (skill >= r.craft.skill.required)
        {
            o << "Learn " << ItemName(r.craft.product) << " (" << Money(r.trainCopper) << ")";
            menu->GetGossipMenu().AddMenuItem(GOSSIP_ICON_TRAINER, o.str(), kRoundsSender, uint32(kLearnBase + i), "", false);
        }
        else
        {
            // What comes later, to aim for.
            o << ItemName(r.craft.product) << ": Enchanting " << r.craft.skill.required;
            menu->GetGossipMenu().AddMenuItem(GOSSIP_ICON_CHAT, o.str(), kRoundsSender, kRoundsMenu, "", false);
        }
    }
    menu->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, creature->GetObjectGuid());
}

bool FusionMgr::EnchantingGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (sender != kRoundsSender || !m_enabled || !player->HasFusion())
        return false;
    LoadRoundRecipes(player);
    FusionPlayerData& d = player->GetFusion();
    bool const gm = IsAdmin(player);
    bool const has = player->HasSkill(Rounds::kSkillEnchanting);
    uint16 const skill = has ? player->GetSkillValue(Rounds::kSkillEnchanting) : 0;
    std::vector<Rounds::Recipe> const& all = Rounds::Recipes();
    ChatHandler chat(player);
    if (action >= kLearnBase && action < kLearnBase + all.size())
    {
        Rounds::Recipe const& r = all[action - kLearnBase];
        std::string const name = ItemName(r.craft.product);
        switch (Rounds::CanLearn(r, d.roundRecipes.count(r.craft.id) > 0, has, skill, player->GetMoney(), gm))
        {
            case Rounds::LearnCheck::Ok:
                if (!gm)
                    player->ModifyMoney(-int32(r.trainCopper));
                d.roundRecipes.insert(r.craft.id);
                CharacterDatabase.PExecute("REPLACE INTO `character_fusion_round_recipe` (`guid`, `recipe`) VALUES (%u, %u)",
                    player->GetGUIDLow(), r.craft.id);
                chat.PSendSysMessage("You have learned to make %s.", name.c_str());
                break;
            case Rounds::LearnCheck::SkillTooLow:
                chat.PSendSysMessage("%s needs %u Enchanting.", name.c_str(), uint32(r.craft.skill.required));
                break;
            case Rounds::LearnCheck::NotEnoughMoney:
                chat.PSendSysMessage("Learning %s costs %s.", name.c_str(), Money(r.trainCopper).c_str());
                break;
            case Rounds::LearnCheck::NoEnchanting:
                chat.SendSysMessage("Only an enchanter can learn this.");
                break;
            default:
                break;
        }
    }
    else if (action >= kMakeBase && action < kMakeBase + all.size())
    {
        Rounds::Recipe const& r = all[action - kMakeBase];
        std::string const name = ItemName(r.craft.product);
        if (!gm && !d.roundRecipes.count(r.craft.id))
            return true;
        Fusion::Construction::Inventory bags;
        for (Fusion::Construction::Material const& m : r.craft.reagents)
            bags[m.item] = player->GetItemCount(m.item);
        switch (Fusion::Construction::CanCraft(r.craft, skill, bags, gm))
        {
            case Fusion::Construction::CraftCheck::Ok:
            {
                ItemPosCountVec dest;
                if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, r.craft.product, r.craft.productCount) != EQUIP_ERR_OK)
                {
                    chat.SendSysMessage("Your bags are full.");
                    break;
                }
                if (!gm)
                    for (Fusion::Construction::Material const& m : r.craft.reagents)
                        player->DestroyItemCount(m.item, m.count, true);
                if (Item* made = player->StoreNewItem(dest, r.craft.product, true))
                    player->SendNewItem(made, r.craft.productCount, false, true);
                // A point, as vanilla's own crafting gives it.
                if (has && !gm)
                {
                    uint16 const pure = player->GetSkillValuePure(Rounds::kSkillEnchanting);
                    if (Fusion::Construction::ApplySkillUp(pure, player->GetSkillMaxPure(Rounds::kSkillEnchanting),
                            r.craft.skill, rand_norm_f()) > pure)
                        player->UpdateSkill(Rounds::kSkillEnchanting, 1);
                }
                break;
            }
            case Fusion::Construction::CraftCheck::SkillTooLow:
                chat.PSendSysMessage("%s needs %u Enchanting.", name.c_str(), uint32(r.craft.skill.required));
                break;
            default:
            {
                std::ostringstream o;
                std::vector<Fusion::Construction::Material> const missing = Fusion::Construction::Missing(bags, r.craft.reagents);
                for (size_t k = 0; k < missing.size(); ++k)
                    o << (k ? ", " : "") << missing[k].count << " " << ItemName(missing[k].item);
                chat.PSendSysMessage("You still need %s.", o.str().c_str());
                break;
            }
        }
    }
    // Back to the list, to make another.
    EnchantingMenu(player, creature);
    return true;
}

// ── Test commands ───────────────────────────────────────────────────────────────────────────

// .fusion dropgun <catalog id | weapon name> [magazine] [reserve]: lay a gun at your feet (admins).
bool ChatHandler::HandleFusionDropGunCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player) || !player->HasFusion())
    {
        SendSysMessage("Fusion: only an admin may drop guns.");
        SetSentErrorMessage(true);
        return false;
    }
    std::istringstream in(args ? args : "");
    std::string which;
    if (!(in >> which))
    {
        SendSysMessage("Fusion: .fusion dropgun <catalog id | weapon name, e.g. barrett_mp> [magazine] [reserve]");
        return true;
    }
    Fusion::WeaponProfile const* w = std::all_of(which.begin(), which.end(), ::isdigit)
        ? sFusionMgr.Weapons().Find(uint16(std::stoul(which)))
        : sFusionMgr.Weapons().Find(which.find("_mp") == std::string::npos ? which + "_mp" : which);
    if (!w)
    {
        PSendSysMessage("Fusion: no gun %s in the catalog.", which.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    uint32 mag = 0, reserve = 0;
    Fusion::Pickups::NpcAmmo(*w, mag, reserve);
    in >> mag >> reserve;
    // Two yards ahead, where the crosshair falls.
    float const o = player->GetOrientation();
    Fusion::Vec3 const at{player->GetPositionX() + 2.f * std::cos(o), player->GetPositionY() + 2.f * std::sin(o), player->GetPositionZ()};
    uint32 const id = sFusionMgr.DropGun(player->GetMap(), at, o + float(M_PI) / 2.f, {w->id, "", "", 0}, mag, reserve);
    if (!id)
    {
        PSendSysMessage("Fusion: the %s can't be dropped (not a primary or secondary, or no rounds).", w->name.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("Fusion: dropped the %s (%s, %u + %u rounds), gone in %u seconds.",
        Fusion::GunDisplayName(w->name).c_str(), w->name.c_str(), mag, reserve, Fusion::Pickups::kDespawnMs / 1000);
    return true;
}

// .fusion rounds <1-5> [grade 1-2]: load enchanted rounds into the gun in hand (admins). 1
// incendiary, 2 frost, 3 armor-piercing, 4 arcane, 5 holy; 0 unloads.
bool ChatHandler::HandleFusionRoundsCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player) || !player->HasFusion())
    {
        SendSysMessage("Fusion: only an admin may load rounds by command; enchanters make them.");
        SetSentErrorMessage(true);
        return false;
    }
    std::istringstream in(args ? args : "");
    uint32 round = 0, grade = 1;
    if (!(in >> round) || round >= uint32(Fusion::Rounds::Round::Count))
    {
        SendSysMessage("Fusion: .fusion rounds <0-5> [grade 1-2]: 1 incendiary, 2 frost, 3 armor-piercing, 4 arcane, 5 holy, 0 unload. Items 93050-93059 too.");
        return true;
    }
    in >> grade;
    if (round == 0)
    {
        player->GetFusion().rounds = {};
        sFusionMgr.SendRounds(player);
        SendSysMessage("Fusion: rounds unloaded.");
        return true;
    }
    if (!sFusionMgr.LoadRounds(player, Fusion::Rounds::Round(round), uint8(std::clamp<uint32>(grade, 1, 2))))
    {
        SendSysMessage("Fusion: hold a gun first.");
        SetSentErrorMessage(true);
        return false;
    }
    return true;
}

// ── A ship's deck gun (FusionShips.cpp) ────────────────────────────────────────────────────────

bool FusionMgr::MountGun(Player* player, uint16 gunId)
{
    if (!m_enabled || !player->HasFusion())
        return false;
    FusionPlayerData& d = player->GetFusion();
    Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(gunId);
    Pickups::Slot slot;
    if (!w || !Pickups::SlotOf(*w, slot))
        return false;
    if (!d.mountedGun)
        d.mountedSaved = d.pickups;
    d.mountedGun = gunId;
    d.pickups.slots[size_t(slot)] = Pickups::Gun{gunId, "", "", 0};
    d.ammo.erase(gunId);
    if (std::optional<Fusion::WeaponProfile> const nw = EffectiveWeapon(player, gunId))
    {
        d.AmmoFor(*nw).Set(nw->magSize, nw->maxReserve);
        SendAmmo(player, *nw);
    }
    SendHeld(player);
    return true;
}

void FusionMgr::UnmountGun(Player* player)
{
    if (!player->HasFusion())
        return;
    FusionPlayerData& d = player->GetFusion();
    if (!d.mountedGun)
        return;
    d.ammo.erase(d.mountedGun);
    // Dead, the pickups from before are gone with the rest (ArmoryOnDeath).
    if (player->IsAlive())
        d.pickups = d.mountedSaved;
    else
        d.pickups.Clear();
    d.mountedGun = 0;
    d.mountedSaved.Clear();
    SendHeld(player);
}
