/*
 * Azeroth Warfare: where the fusion rules meet the game. GPL-2.0-or-later, see FusionMgr.h.
 */

#include "FusionMgr.h"
#include "fusion/Riding.h"
#include "fusion/DruidForms.h"
#include "FusionClasses.h"
#include "FusionFeedback.h"
#include "FusionBounties.h"
#include "FusionShips.h"
#include "FusionWanderers.h"

#include <algorithm>

#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include "Util.h"
#include "Formulas.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "MoveSpline.h"
#include "VMapFactory.h"
#include "SpellMgr.h"
#include "fusion/Access.h"
#include "fusion/Ballistics.h"
#include "fusion/Equipment.h"
#include "fusion/Creatures.h"
#include "fusion/Detection.h"
#include "fusion/NpcGuns.h"

#include <cinttypes>
#include <fstream>
#include <sstream>

namespace
{
    Fusion::Vec3 V(float x, float y, float z) { return {x, y, z}; }

    // What lets a melee press lunge (fusion/Movement.h SwingLunges): the class framework's grant.
    Fusion::LungeGrant LungeGrantOf(Player const* player)
    {
        Fusion::LungeGrant g;
        g.charge = sFusionClasses.GrantsSwingLunge(player);
        return g;
    }

    // The eye above the feet, by stance: the shot's origin must sit near it.
    float EyeHeight(Fusion::Stance s) { return 1.6f * Fusion::StanceHeightScale(s); }
    // The id of a gunner's own runs to a spot (MovePoint), apart from any script's.
    constexpr uint32 kGunnerPointId = 0xF05E;

    Fusion::ArmorClass ArmorFor(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
                return Fusion::ArmorClass::Plate;
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
                return Fusion::ArmorClass::Mail;
            case CLASS_ROGUE:
            case CLASS_DRUID:
                return Fusion::ArmorClass::Leather;
            default:
                return Fusion::ArmorClass::Cloth;
        }
    }

    Fusion::PvpPlayer PvpOf(Player const* p, FusionPlayerData const* d)
    {
        Fusion::PvpPlayer out;
        out.guid = p->GetObjectGuid().GetRawValue();
        out.team = p->GetTeam() == HORDE ? Fusion::Team::Horde : Fusion::Team::Alliance;
        out.level = p->GetLevel();
        out.groupId = p->GetGroup() ? p->GetGroup()->GetId() : 0;
        out.ffaFlag = d ? d->ffa.On() : p->IsFFAPvP();
        out.inSanctuary = false;    // the game's own sanctuary checks run in IsValidAttackTarget
        return out;
    }

    Fusion::AttackerStats StatsOf(Player const* p)
    {
        Fusion::AttackerStats a;
        a.level = p->GetLevel();
        a.attackPower = std::max(p->GetTotalAttackPowerValue(BASE_ATTACK), p->GetTotalAttackPowerValue(RANGED_ATTACK));
        if (Item const* ranged = p->GetWeaponForAttack(RANGED_ATTACK))
            a.rangedItemLevel = float(ranged->GetProto()->ItemLevel);
        if (Item const* main = p->GetWeaponForAttack(BASE_ATTACK))
            a.mainHandItemLevel = float(main->GetProto()->ItemLevel);
        a.critChance = p->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE) / 100.f;
        return a;
    }

    // A creature as the CoD scale sees it. A boss: a world boss's rank, the boss flag, or an
    // instance's elite run by a boss script.
    Fusion::Scale::CreatureFacts FactsOf(Creature const* c)
    {
        Fusion::Scale::CreatureFacts f;
        f.level = c->GetLevel();
        CreatureInfo const* ci = c->GetCreatureInfo();
        uint32 const rank = ci ? ci->rank : 0;
        f.rank = rank <= 4 ? Fusion::CreatureRank(rank) : Fusion::CreatureRank::Normal;
        Map const* map = c->FindMap();
        f.place = map && map->IsRaid() ? Fusion::Scale::Place::Raid
            : map && map->IsDungeon() ? Fusion::Scale::Place::Dungeon : Fusion::Scale::Place::World;
        f.boss = rank == CREATURE_ELITE_WORLDBOSS || (ci && (ci->static_flags1 & CREATURE_STATIC_FLAG_RAID_BOSS_MOB));
        if (!f.boss && f.place != Fusion::Scale::Place::World && (rank == CREATURE_ELITE_ELITE || rank == CREATURE_ELITE_RAREELITE))
            f.boss = c->GetScriptName().rfind("boss_", 0) == 0;
        return f;
    }

    // Whether an ammunition entry is a gun's rounds (bought) or equipment's charges (free).
    bool IsGunAmmo(Fusion::WeaponTable const& t, uint16 id)
    {
        Fusion::WeaponProfile const* w = t.Find(id);
        return !w || Fusion::FiresRounds(w->weaponClass);
    }

    // Set while the fusion deals its own (CoD-scale) damage: Unit::DealDamage leaves it as is.
    thread_local bool t_fusionHit = false;

    // A fractional amount to whole points, the fraction by chance.
    uint32 Dither(float v)
    {
        if (v <= 0.f)
            return 0;
        uint32 const whole = uint32(v);
        return whole + (frand(0.f, 1.f) < v - float(whole) ? 1 : 0);
    }

    Fusion::TargetTraits TraitsOf(Unit const* u)
    {
        Fusion::TargetTraits t;
        t.isPlayer = u->IsPlayer();
        t.creatureType = Fusion::CreatureType(u->GetCreatureType());
        if (Player const* p = u->ToPlayer())
            t.armor = ArmorFor(p->GetClass());
        if (Creature const* c = u->ToCreature())
            t.slowImmune = c->IsWorldBoss();
        return t;
    }
}

Fusion::GunAmmo& FusionPlayerData::AmmoFor(Fusion::WeaponProfile const& w, bool left)
{
    uint16 const key = left && w.akimbo ? uint16(w.id | kLeftGun) : w.id;
    auto it = ammo.find(key);
    if (it == ammo.end())
    {
        it = ammo.emplace(key, Fusion::GunAmmo{}).first;
        it->second.Reset(w);
        it->second.SetReserveBonus(reserveBonus, true);     // a bandolier (FusionPrestige.cpp)
    }
    return it->second;
}

FusionMgr& FusionMgr::Instance()
{
    static FusionMgr instance;
    return instance;
}

void FusionMgr::Load()
{
    m_enabled = sConfig.GetBoolDefault("Fusion.Enable", false);
    m_pvpLowLevelProtection = sConfig.GetBoolDefault("Fusion.PvP.LowLevelProtection", true);
    m_coefficients.pve = sConfig.GetFloatDefault("Fusion.Damage.PvE", 1.0f);
    m_coefficients.pvp = sConfig.GetFloatDefault("Fusion.Damage.PvP", 0.4f);
    m_adminMinSecurity = sConfig.GetIntDefault("Fusion.Admin.MinSecurity", SEC_GAMEMASTER);
    m_buildFree = sConfig.GetBoolDefault("Fusion.Build.Free", false);
    // A more dangerous world (fusion/Danger.h): how much of the table's extra health and damage
    // the realm keeps (0 off, 1 the table, 2 double).
    m_danger = Fusion::Danger::Defaults();
    m_danger.healthKnob = sConfig.GetFloatDefault("Fusion.Danger.Health", 1.0f);
    m_danger.damageKnob = sConfig.GetFloatDefault("Fusion.Danger.Damage", 1.0f);
    // Limited ammunition (fusion/Ammo.h, design/BALANCE_GUNS_VS_CLASSES.md).
    {
        Fusion::AmmoRules r;
        r.limited = sConfig.GetBoolDefault("Fusion.Ammo.Limited", true);
        r.fieldFloorMags = sConfig.GetFloatDefault("Fusion.Ammo.FieldFloorMags", r.fieldFloorMags);
        r.respawnReserveShare = sConfig.GetFloatDefault("Fusion.Ammo.RespawnReserveShare", r.respawnReserveShare);
        r.gunnerScavengeMags = sConfig.GetFloatDefault("Fusion.Ammo.GunnerScavengeMags", r.gunnerScavengeMags);
        r.playerScavengeMags = sConfig.GetFloatDefault("Fusion.Ammo.PlayerScavengeMags", r.playerScavengeMags);
        r.fieldRoundsPerSecond = sConfig.GetFloatDefault("Fusion.Ammo.FieldRoundsPerSecond", r.fieldRoundsPerSecond);
        r.eliteScavengeScale = sConfig.GetFloatDefault("Fusion.Ammo.EliteScavengeScale", r.eliteScavengeScale);
        r.bossRestockShare = sConfig.GetFloatDefault("Fusion.Ammo.BossRestockShare", r.bossRestockShare);
        r.scavengerBodyScale = sConfig.GetFloatDefault("Fusion.Ammo.ScavengerBodyScale", r.scavengerBodyScale);
        Fusion::SetAmmoRules(r);
    }
    // Fainted companions (fusion/CompanionRest.h): how long one waits to come round on its own,
    // and a stable master's fee per level to revive one.
    m_faint = Fusion::Companions::FaintSettingsFrom(
        sConfig.GetIntDefault("Companions.FaintRecoverMinutes", int32(Fusion::Companions::kFaintRecoverMinutes)),
        sConfig.GetIntDefault("Companions.StableReviveCostPerLevel", int32(Fusion::Companions::kStableReviveCostPerLevel)));
    if (!m_enabled)
        return;
    LoadPieces();
    LoadCompanions();
    LoadCompanionBattles();
    LoadConstruction();
    LoadArmory();
    LoadPrestigeTables();
    LoadRiding();
    sFusionClasses.Load();

    // The catalog (version 2), else the version 1 weapon table.
    std::vector<std::string> paths;
    std::string const catalogFile = sConfig.GetStringDefault("Fusion.CatalogFile", "");
    paths.push_back(catalogFile.empty() ? sWorld.GetDataPath() + "fusion/catalog.json" : catalogFile);
    std::string const weaponsFile = sConfig.GetStringDefault("Fusion.WeaponsFile", "");
    paths.push_back(weaponsFile.empty() ? sWorld.GetDataPath() + "fusion/weapons.json" : weaponsFile);
    for (std::string const& path : paths)
    {
        std::ifstream in(path);
        if (!in)
            continue;
        std::stringstream text;
        text << in.rdbuf();
        std::string error;
        Fusion::Catalog catalog;
        if (!catalog.LoadJson(text.str(), error))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: %s: %s", path.c_str(), error.c_str());
            return;
        }
        m_catalog = std::move(catalog);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: enabled, %u weapons, %u attachments, %u camos from %s.",
            uint32(m_catalog.Weapons().Size()), uint32(m_catalog.Attachments().size()), uint32(m_catalog.Camos().size()), path.c_str());
        // Achievements: the book from the catalog, the account tables (characters DB).
        m_book = Fusion::DefaultAchievementBook(m_catalog);
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `account_fusion_stats` ("
            "`account` int(11) unsigned NOT NULL, `stat` smallint(5) unsigned NOT NULL, "
            "`key` int(11) unsigned NOT NULL DEFAULT '0', `value` int(11) unsigned NOT NULL DEFAULT '0', "
            "PRIMARY KEY (`account`, `stat`, `key`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `account_fusion_achievement` ("
            "`account` int(11) unsigned NOT NULL, `achievement` int(11) unsigned NOT NULL, "
            "`earned_at` bigint(20) unsigned NOT NULL DEFAULT '0', `character` int(11) unsigned NOT NULL DEFAULT '0', "
            "PRIMARY KEY (`account`, `achievement`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u achievements.", uint32(m_book.All().size()));
        return;
    }
    sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: no catalog at %s nor weapon table at %s; guns are disabled.", paths[0].c_str(), paths[1].c_str());
}

void FusionMgr::SendEvents(Player* player, Fusion::Packets::Events const& events) const
{
    if (events.events.empty())
        return;
    std::vector<uint8_t> body = Fusion::Packets::Encode(events);
    WorldPacket data(SMSG_FUSION_EVENTS, body.size());
    data.append(body.data(), body.size());
    player->GetSession()->SendPacket(&data);
}

bool FusionMgr::IsAdmin(Player const* player) const
{
    return player->GetSession() && uint32(player->GetSession()->GetSecurity()) >= m_adminMinSecurity;
}

bool FusionMgr::ProPerks(Player const* player) const
{
    int8 const mode = player->HasFusion() ? player->GetFusionConst().proOverride : int8(-1);
    return mode < 0 ? IsAdmin(player) : mode > 0;
}

void FusionMgr::SetProPerks(Player* player, int8 mode)
{
    player->GetFusion().proOverride = mode;
    ApplyPerks(player);
    SendWorld(player, {21, uint8_t(ProPerks(player) ? 1 : 0)});
}

uint32 FusionMgr::ClearPiecesNear(Player* player, float radius)
{
    Map* map = player->GetMap();
    Fusion::Vec3 const me = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    uint32 cleared = 0;
    for (Fusion::Build::Piece const& p : PiecesNear(map->GetId(), me, radius))
    {
        RemovePieces(map, p.id, Fusion::Build::RemoveCause::TakenDown);
        ++cleared;
    }
    return cleared;
}

bool ChatHandler::HandleFusionClearCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player))
    {
        SendSysMessage("Fusion: only an admin may clear built pieces.");
        SetSentErrorMessage(true);
        return false;
    }
    float radius = 20.f;
    if (args && *args)
        radius = std::clamp(float(std::atof(args)), 1.f, 200.f);
    uint32 const n = sFusionMgr.ClearPiecesNear(player, radius);
    PSendSysMessage("Fusion: took down %u built pieces within %.0f yards.", n, radius);
    return true;
}

bool ChatHandler::HandleFusionProCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player))
    {
        SendSysMessage("Fusion: only an admin may switch Pro perks.");
        SetSentErrorMessage(true);
        return false;
    }
    std::string const arg = args ? std::string(args) : std::string();
    if (arg == "on" || arg == "off" || arg == "default")
        sFusionMgr.SetProPerks(player, arg == "on" ? 1 : arg == "off" ? 0 : -1);
    else if (!arg.empty())
    {
        SendSysMessage("Usage: .fusion pro [on|off|default]");
        SetSentErrorMessage(true);
        return false;
    }
    int8 const mode = player->GetFusion().proOverride;
    PSendSysMessage("Fusion: Pro perks are %s (%s) until logout.", sFusionMgr.ProPerks(player) ? "ON" : "OFF",
        mode < 0 ? "the account's default: a GM has them" : "set by .fusion pro");
    return true;
}

uint32 FusionMgr::CreatureMaxHealth(Creature const* creature) const
{
    // The CoD scale, with the danger table's health on it (fusion/Danger.h).
    return std::max<uint32>(1, uint32(Fusion::Danger::CreatureMaxHealth(FactsOf(creature), creature->GetMapId(), m_danger) + 0.5f));
}

float FusionMgr::CreatureDangerDamage(Creature const* creature, bool gun) const
{
    Fusion::Scale::CreatureFacts const f = FactsOf(creature);
    return gun ? Fusion::Danger::GunDamageMultiplier(f, creature->GetMapId(), m_danger)
        : Fusion::Danger::DamageMultiplier(f, creature->GetMapId(), m_danger);
}

Fusion::GunFacts FusionMgr::GunFactsOf(Creature const* c) const
{
    Fusion::GunFacts f;
    f.type = Fusion::CreatureType(c->GetCreatureType());
    f.caster = c->GetMaxPower(POWER_MANA) > 0;
    f.rangedWeapon = c->GetVirtualItemDisplayId(RANGED_ATTACK) != 0 && c->GetVirtualItemClass(RANGED_ATTACK) == ITEM_CLASS_WEAPON;
    f.name = c->GetName();
    f.entry = c->GetEntry();
    CreatureInfo const* ci = c->GetCreatureInfo();
    f.faction = ci ? ci->faction : 0;
    // A boss keeps its script's ways: a world boss, the boss flag, an instance's boss script.
    f.boss = FactsOf(c).boss;
    return f;
}

float FusionMgr::DamageMultiplier(Player const* player) const
{
    int32 spell = 0;
    for (int i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        spell = std::max(spell, player->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + i));
    return Fusion::Scale::PlayerDamageMultiplier(player->GetLevel(), player->GetTotalAttackPowerValue(BASE_ATTACK),
        player->GetTotalAttackPowerValue(RANGED_ATTACK), float(spell));
}

float FusionMgr::HealScale(Unit const* unit) const
{
    if (!m_enabled)
        return 1.f;
    if (unit->IsPlayer())
        return Fusion::Scale::PoolRatio(float(unit->GetMaxHealth()), m_par.MaxHealth(unit->GetLevel()));
    if (Creature const* c = unit->ToCreature(); c && c->m_fusionWowMaxHealth)
        return Fusion::Scale::PoolRatio(float(c->GetMaxHealth()), float(c->m_fusionWowMaxHealth));
    return 1.f;
}

uint32 FusionMgr::ConvertDamage(Unit* attacker, Unit* victim, uint32 damage, uint32 damageType, bool spell, uint32 schoolMask) const
{
    if (!m_enabled || t_fusionHit || !damage)
        return damage;
    Player* const victimPlayer = victim->ToPlayer();
    Creature* const attackerCreature = attacker->ToCreature();
    if (victimPlayer && attackerCreature && !attacker->GetCharmerOrOwnerGuid().IsPlayer())
    {
        // A creature on a player: its own scale, by level and rank, whatever its WoW numbers.
        Fusion::Scale::CreatureFacts const f = FactsOf(attackerCreature);
        float const armor = float(victimPlayer->GetArmor());
        float cod;
        if (!spell && damageType == DIRECT_DAMAGE)
        {
            // A melee swing against its own average, before WoW's armor (the scale's cut follows).
            float wow = float(damage);
            if (schoolMask & SPELL_SCHOOL_MASK_NORMAL)
                wow /= std::max(0.25f, 1.f - Fusion::ArmorReduction(armor, attacker->GetLevel()));
            float const expected = 0.5f * (attacker->GetFloatValue(UNIT_FIELD_MINDAMAGE) + attacker->GetFloatValue(UNIT_FIELD_MAXDAMAGE));
            cod = Fusion::Scale::WowToCod(Fusion::Scale::CreatureMeleeBase(f), wow, expected);
        }
        else
            cod = Fusion::Scale::WowToCod(Fusion::Scale::CreatureSpellBase(f), float(damage),
                Fusion::Scale::ExpectedWowSpell(attacker->GetLevel(), damageType == DOT));
        cod *= CreatureDangerDamage(attackerCreature, false);
        cod *= 1.f - Fusion::Scale::PlayerArmorCut(armor, victimPlayer->GetLevel());
        return std::max<uint32>(1, Dither(cod));
    }
    // Anyone else's WoW damage keeps its share of the victim's pool.
    float const ratio = HealScale(victim);
    if (ratio == 1.f)
        return damage;
    return std::max<uint32>(1, Dither(float(damage) * ratio));
}

void FusionMgr::Hit(Unit* from, Unit* to, uint32 damage, FusionHitSource const& source)
{
    bool const was = t_fusionHit;
    FusionHitSource const wasSource = CurrentHit();
    // World of Modern Warfare: a class shield in the way stops a round (FusionClasses.cpp).
    if (source.weaponId && !source.melee && !source.explosive && sFusionClasses.StopsRound(from, to, damage))
        return;
    t_fusionHit = true;
    CurrentHit() = source;
    from->DealDamage(to, damage, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
    // Gunfire, a blade or a blast may throw a rider off (fusion/Riding.h).
    if (to->IsInWorld() && to->IsAlive() && !source.melee)
        KnockOffMount(to, damage, source);
    t_fusionHit = was;
    CurrentHit() = wasSource;
}

float FusionMgr::MoveSpeedMultiplier(Player const* player) const
{
    if (!m_enabled || !player->HasFusion() || player->IsMounted())
        return 1.f;
    FusionPlayerData const& d = player->GetFusionConst();
    // Down: a crawl, whatever is in hand.
    if (d.downed.Active())
        return Fusion::Downed::kCrawlSpeedScale;
    Fusion::MovementState m;
    m.stance = d.stance;
    // Walking and backpedalling are WoW's own speeds already; only sprint is the fusion's.
    m.gait = d.sprinting ? Fusion::Gait::Sprint : Fusion::Gait::Run;
    m.aiming = d.stateFlags & Fusion::Packets::STATE_AIMING;
    m.lightweight = d.perks.lightweight;
    // Strength lifts part of a heavy gun's weight (fusion/Stats.h).
    Fusion::WeaponProfile const* held = m_catalog.Weapons().Find(d.heldWeaponId);
    std::optional<Fusion::WeaponProfile> eased;
    if (held && d.combat[Fusion::Stats::Out::HeavyGunRelief] > 0.f)
    {
        eased = *held;
        eased->moveSpeedScale = Fusion::Stats::HeavyGunMoveScale(held->weaponClass, held->moveSpeedScale,
            d.combat[Fusion::Stats::Out::HeavyGunRelief]);
        held = &*eased;
    }
    return Fusion::SpeedMultiplier(m, held)
        * Fusion::OutOfCombatSpeedMultiplier(d.perks, player->IsInCombat())
        * (player->IsInCombat() ? 1.f : d.augments.outOfCombatSpeed)    // the Long Road's glyph
        * sFusionClasses.SpeedMultiplier(player)
        * sMinstrelMgr.SpeedFactor(player);       // a minstrel's travel or rally song
}

void FusionMgr::HandleState(Player* player, Fusion::Packets::State const& state)
{
    FusionPlayerData& d = player->GetFusion();
    // Down: prone, whatever the client asks; no sprint, mantle or lunge (FusionDowned.cpp).
    if (d.downed.Active() && (state.stance != Fusion::Stance::Prone || state.gait == Fusion::Gait::Sprint
        || (state.flags & (Fusion::Packets::STATE_MANTLE | Fusion::Packets::STATE_LUNGE))))
    {
        Fusion::Packets::State crawl = state;
        crawl.stance = Fusion::Stance::Prone;
        if (crawl.gait == Fusion::Gait::Sprint)
            crawl.gait = Fusion::Gait::Run;
        crawl.flags &= ~uint8(Fusion::Packets::STATE_MANTLE | Fusion::Packets::STATE_LUNGE);
        HandleState(player, crawl);
        return;
    }
    // Only a gun the character may use; a locked one would grant its move speed.
    Fusion::WeaponProfile const* held = m_catalog.Weapons().Find(state.heldWeaponId);
    // A picked-up gun is the player's for this life, unlocked or not.
    uint16 const usable = held && (player->GetFusion().unlocks.CanUseWeapon(*held) || PickupAllows(d, held->id))
        ? state.heldWeaponId : 0;
    // Compared as the server keeps them: a locked gun is none on both sides.
    bool const speedChanged = Fusion::SpeedInputsChanged(
        {d.stance, d.heldWeaponId, bool(d.stateFlags & Fusion::Packets::STATE_AIMING)},
        {state.stance, usable, bool(state.flags & Fusion::Packets::STATE_AIMING)});
    d.stance = state.stance;
    d.gait = state.gait;
    d.stateFlags = state.flags;
    // The shield raised (fusion/Melee.h Guard): the Classic layer's right button, a shield in the
    // off hand.
    {
        bool shield = false;
        if (d.actionSystem == Fusion::ActionSystems::System::Classic)
            if (Item const* off = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                if (ItemPrototype const* proto = off->GetProto())
                    shield = proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD;
        Fusion::Melee::GuardUpdate(d.guard, shield && (state.flags & Fusion::Packets::STATE_AIMING), WorldTimer::getMSTime());
    }
    uint16 const wasHeld = d.heldWeaponId;
    d.heldWeaponId = usable;
    // A new gun in hand: tell the client its magazine and reserve, which it otherwise keeps from
    // the last gun and may think dry.
    if (d.heldWeaponId != wasHeld && d.heldWeaponId != 0)
        SendAmmo(player, *held);
    if (state.loadedAmmo < uint8(Fusion::AmmoType::Count))
    {
        auto const type = Fusion::AmmoType(state.loadedAmmo);
        // Only rounds the character has unlocked.
        if (Fusion::CanUseAmmo(player->GetLevel(), IsAdmin(player), type))
        {
            d.loadedAmmo = type;
            if (std::optional<Fusion::WeaponProfile> const w = EffectiveWeapon(player, state.heldWeaponId))
                d.AmmoFor(*w).LoadSpecial(type);
        }
    }

    if (speedChanged)
    {
        // Not forced: vmangos' forced update writes the server's rate only and never tells the
        // client, which kept moving at its old speed.
        player->UpdateSpeed(MOVE_RUN, false);
        player->UpdateSpeed(MOVE_RUN_BACK, false);
    }

    if (state.flags & Fusion::Packets::STATE_FFA_ON)
        d.ffa.RequestOn();
    else if (state.flags & Fusion::Packets::STATE_FFA_OFF)
        d.ffa.RequestOff();
    if (d.ffa.On() != player->IsFFAPvP())
        player->SetFFAPvP(d.ffa.On());

    // Rearm (the Modern bar's): resting in a capital or an inn, a kit station's rearm, free (the
    // special rounds topped up for the quartermaster's fee when it can be paid). Anywhere else
    // only with Field Quartermaster (One Man Army), on its cooldown (fusion/Perks.h).
    if (state.flags & Fusion::Packets::STATE_REARM)
    {
        uint32 const nowMs = WorldTimer::getMSTime();
        if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
            RearmAtStation(player);
        else if (Fusion::FieldRearmReady(d.perks, nowMs, d.fieldRearmReadyMs))
        {
            // The Quartermaster's glyph has it ready sooner.
            uint32 const cooldown = uint32(Fusion::FieldRearmCooldownMs(d.perks) * d.augments.fieldRearm);
            d.fieldRearmReadyMs = std::max<uint32>(1, nowMs + cooldown);
            RefillAmmo(player);
            ChatHandler(player).PSendSysMessage("Field Quartermaster: rearmed in the field. Ready again in %u minutes.",
                cooldown / 60000);
        }
        else if (d.perks.oneManArmy)
            ChatHandler(player).PSendSysMessage("Field Quartermaster: ready again in %u seconds.",
                uint32(int32(d.fieldRearmReadyMs - nowMs)) / 1000 + 1);
    }

    // MW2 movement requests: one mantle or lunge per half second, and never mounted, on a taxi,
    // swimming or dead.
    // Both move by knock-back, which a stun, root or running spline skips, so those (and crowd
    // control) refuse them outright rather than letting the strike land without the dash.
    uint8 moves = state.flags & (Fusion::Packets::STATE_MANTLE | Fusion::Packets::STATE_LUNGE);
    // A melee press lunges only with a grant (a warrior's Charge perk): without one a lunge request
    // (an older client's swing) is dropped, and the swing is just a swing.
    if ((moves & Fusion::Packets::STATE_LUNGE) && !(moves & Fusion::Packets::STATE_MANTLE)
        && !Fusion::SwingLunges(LungeGrantOf(player)))
        moves = 0;
    if (moves && m_enabled)
    {
        Fusion::MoveAction const action = (moves & Fusion::Packets::STATE_MANTLE) ? Fusion::MoveAction::Mantle : Fusion::MoveAction::Lunge;
        uint32 const now = WorldTimer::getMSTime();
        // The lunge also waits out its recovery, 1.4 swings of the main hand (back-to-back lunges
        // stacked their dashes), and a swing's.
        Fusion::Melee::Profile const melee = MeleeOf(player);
        bool const lungeReady = action != Fusion::MoveAction::Lunge
            || ((!d.lastLungeMs || Fusion::Melee::Ready(now - d.lastLungeMs, melee.lungeMs))
                && (!d.lastSwingMs || Fusion::Melee::Ready(now - d.lastSwingMs, melee.swingMs)));
        if (Fusion::CanMoveAction(ActorOf(player), action) && now - d.lastMoveActionMs >= 500 && lungeReady)
        {
            d.lastMoveActionMs = now;
            if (action == Fusion::MoveAction::Lunge)
                d.lastLungeMs = now;
            if (action == Fusion::MoveAction::Mantle)
                TryMantle(player);
            else
                TryLunge(player);
        }
    }

    // A reload fills the magazine (both, for an akimbo pair); a shotgun loading shell by shell
    // asks for one round at a time, so a reload cut short leaves the magazine part full. The
    // client says when its reload finished; one said sooner than the gun could reload after its
    // last round waits for its time (fusion/Reload.h, SettleReload).
    if (state.flags & (Fusion::Packets::STATE_RELOADING | Fusion::Packets::STATE_RELOAD_ONE))
    {
        if (std::optional<Fusion::WeaponProfile> const w = EffectiveWeapon(player, state.heldWeaponId))
        {
            Fusion::ReloadPace& pace = d.reloadPace[w->id];
            if (state.flags & Fusion::Packets::STATE_RELOADING)
                pace.AskFull();
            else
                pace.AskShell();
            SettleReload(player, *w, WorldTimer::getMSTime());
        }
    }
}

float FusionMgr::ReloadFactorOf(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    return d.reloadFactor.Update(Fusion::ReloadFactor(d.perks.sleightOfHand, d.combat[Fusion::Stats::Out::ReloadTime]), now);
}

bool FusionMgr::SettleReload(Player* player, Fusion::WeaponProfile const& w, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    auto it = d.reloadPace.find(w.id);
    if (it == d.reloadPace.end() || !it->second.Pending())
        return false;
    float const factor = ReloadFactorOf(player, now);
    Fusion::ReloadPace::Due const due = it->second.Take(now, Fusion::QuickestReloadSeconds(w, factor),
        Fusion::QuickestShellSeconds(w, factor));
    if (!due.Any())
        return false;
    if (due.full)
    {
        d.AmmoFor(w).Reload();
        if (w.akimbo)
            d.AmmoFor(w, true).Reload();
    }
    for (uint32 i = 0; i < due.shells; ++i)
        if (!d.AmmoFor(w).ReloadOne() && w.akimbo)
            d.AmmoFor(w, true).ReloadOne();     // the right is full: the left's shell
    // The client is told only of the gun in hand (the ammo event names no gun).
    if (w.id == d.heldWeaponId)
        SendAmmo(player, w);
    return true;
}

void FusionMgr::UpdatePlayer(Player* player, uint32 diffMs)
{
    FusionPlayerData& d = player->GetFusion();
    float const dt = diffMs / 1000.f;
    uint32 const now = WorldTimer::getMSTime();

    Fusion::BodyPose pose;
    pose.feet = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    pose.orientation = player->GetOrientation();
    pose.stance = d.stance;
    d.poses.Record(now, pose);
    UpdateCompanion(player, now);
    UpdateCompanionBattle(player, now);
    UpdateCombatStats(player, now, diffMs);
    // The quickest reload factor of late, kept in mind every update (a reload begun with Sleight
    // of Hand may end after it is gone), and a reload asked for too soon landing when its time
    // comes (fusion/Reload.h).
    ReloadFactorOf(player, now);
    for (auto const& [gunId, pace] : d.reloadPace)
        if (pace.Pending())
            if (std::optional<Fusion::WeaponProfile> const w = EffectiveWeapon(player, gunId))
                SettleReload(player, *w, now);
    UpdatePrestige(player, now);
    UpdateRiding(player, now);
    sFusionClasses.Update(player, diffMs);

    // A respawn: Painkiller's seconds, a fresh Final Stand.
    bool const alive = player->IsAlive();
    if (alive && !d.wasAlive)
    {
        d.finalStandUsed = false;
        // A fresh life, fresh equipment, and full magazines; the reserves topped up to a share
        // under limited ammunition (fusion/Ammo.h), all of them under the old model. The kit in
        // hand stays.
        if (Fusion::ClassLoadout const* c = d.Equipped())
        {
            d.ammo.erase(c->lethal);
            d.ammo.erase(c->tactical);
        }
        RespawnAmmo(player);
        if (DeathstreakReady(d, Fusion::PerkId::Painkiller))
        {
            d.painkillerUntilMs = now + uint32(Fusion::kPainkillerSeconds * 1000.f);
            ChatHandler(player).SendSysMessage("Painkiller: 10 seconds of a third of the damage.");
        }
    }
    d.wasAlive = alive;
    // Down: the bleed-out, Final Stand, a revival kit at work (FusionDowned.cpp).
    UpdateDowned(player, now);
    if (!d.charges.empty())
        UpdateCharges(player, now);
    if (!d.sky.empty() || d.rocket.active)
        UpdateSkyStreaks(player, now);
    // Martyrdom's grenade.
    if (d.martyrdomAtMs && int32(now - d.martyrdomAtMs) >= 0)
    {
        d.martyrdomAtMs = 0;
        if (player->GetMapId() == d.martyrdomMap)
            Explode(player, d.martyrdomPos, Fusion::kMartyrdomRadius, Fusion::kMartyrdomInner, Fusion::kMartyrdomOuter, 0);
    }

    // Create-a-Class: the saved classes once after login, and a waiting equip once out of
    // combat and alive.
    if (!d.sentClasses)
    {
        d.sentClasses = true;
        SendUnlocks(player);
        SendLoadout(player, d.Equipped() ? Fusion::Packets::LoadoutStatus::Applied : Fusion::Packets::LoadoutStatus::Saved);
        if (d.loginRefusal != Fusion::LoadoutError::None)
            SendLoadout(player, Fusion::Packets::LoadoutStatus::Rejected, d.loginRefusal, d.loginRefusedSlot);
        SendMeleeProfile(player);
        SendWorld(player, {21, uint8_t(ProPerks(player) ? 1 : 0)});
    }
    // Utility's bar: the tools the player has and is trained for (FusionTools.cpp).
    UpdateUtilityTools(player, now);
    // The main hand changed (equipped, taken off, broken): tell the client its melee profile.
    {
        auto const [entry, offEntry] = MeleeEntries(player);
        if (entry != d.meleeEntry || offEntry != d.offHandEntry)
            SendMeleeProfile(player);
    }
    if (d.pendingSlot != Fusion::kNoClass && Fusion::EquipRule(player->IsInCombat(), player->IsAlive()) == Fusion::EquipDecision::ApplyNow)
        ApplyEquip(player, d.pendingSlot);

    if (!player->IsAlive() && d.streak.Tenths() > 0)
    {
        d.streak.Reset();
        SendStreakProgress(player);
    }

    // Sprinting counts toward its achievements (Marathon), whole yards at a time.
    if (d.sprinting && player->IsMoving())
    {
        d.sprintYards += player->GetSpeed(MOVE_RUN) * dt;
        if (d.sprintYards >= 50.f)
        {
            uint32 const yards = uint32(d.sprintYards);
            d.sprintYards -= float(yards);
            CountStat(player, {Fusion::Stat::DistanceSprinted, Fusion::kAnyKey}, yards);
        }
    }

    // Sprint lasts as long as the stamina does.
    // A class effect may forbid it (Earthbind, a net, tar, Curse of Exhaustion).
    bool const wantsSprint = d.gait == Fusion::Gait::Sprint && d.stance == Fusion::Stance::Stand
        && !sFusionClasses.BlocksSprint(player);
    // An Adrenaline stim runs as Marathon does while it lasts (fusion/FieldGear.h).
    bool const tireless = d.perks.marathon || d.stims.Active(Fusion::FieldGear::StimKind::Adrenaline, now);
    bool const sprinting = d.stamina.Update(dt, wantsSprint, tireless) && wantsSprint;
    if (sprinting != d.sprinting)
    {
        if (!sprinting)
            d.sprintEndedMs = now;
        d.sprinting = sprinting;
        player->UpdateSpeed(MOVE_RUN, false);
    }   // death ends the streak; earned rewards stay until used

    bool const inCombat = player->IsInCombat();
    // Marathon's out-of-combat pace changes with combat.
    if (d.perks.marathon && inCombat != (d.outOfCombatSeconds == 0.f))
        player->UpdateSpeed(MOVE_RUN, false);
    d.outOfCombatSeconds = inCombat ? 0.f : d.outOfCombatSeconds + dt;

    bool const wasOn = d.ffa.On();
    d.ffa.Update(dt, inCombat);
    if (wasOn && !d.ffa.On())
        player->SetFFAPvP(false);

    if (!inCombat)
        for (auto& kv : d.ammo)
            kv.second.FieldRearm(d.outOfCombatSeconds, dt);

    if (int32(now - d.nextBuildMs) >= 0)
    {
        d.nextBuildMs = now + 1000;
        UpdateBuild(player, now);
    }
    if (m_enabled && int32(now - d.nextNpcGunsMs) >= 0)
    {
        d.nextNpcGunsMs = now + Fusion::kNpcGunsEveryMs;
        SendNpcGuns(player);
    }
    if (m_enabled)
        UpdateArmory(player, now);
    if (!d.sentries.empty())
        UpdateSentries(player, now);
    if (!d.heardAlerts.empty())
        UpdateHeardAlerts(player, now);
}

float FusionMgr::AggroRadiusMultiplier(Creature const* creature, Unit const* target) const
{
    if (!m_enabled)
        return 1.f;
    Player const* player = target->ToPlayer();
    if (!player || !player->HasFusion())
        return 1.f;
    FusionPlayerData const& d = player->GetFusionConst();
    // A downed player draws nothing new (fusion/Downed.h): they can crawl away.
    if (d.downed.Active())
        return 0.f;

    Fusion::Observer o;
    o.level = creature->GetLevel();
    o.type = Fusion::CreatureType(creature->GetCreatureType());
    o.position = V(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());
    o.facing = V(std::cos(creature->GetOrientation()), std::sin(creature->GetOrientation()), 0.f);

    Fusion::Intruder p;
    p.level = player->GetLevel();
    p.position = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    p.stance = d.stance;
    p.gait = d.gait;

    // Smoke hides the player (FusionClasses.cpp).
    if (sFusionClasses.SightBlocked(creature->GetMapId(), o.position, p.position))
        return 0.f;
    float const base = Fusion::WowAggroRadius(o.level, p.level);
    // Ninja, and a ghillie mantle (fusion/FieldGear.h).
    float const ninja = Fusion::DetectionPerkMultiplier(d.perks) * d.ghillie * d.augments.detection;
    return base > 0.f ? Fusion::DetectionRadius(o, p) / base * ninja : ninja;
}

float FusionMgr::CreatureChaseMultiplier(Creature const* creature) const
{
    if (!m_enabled || creature->IsPet())
        return 1.f;
    Unit const* victim = creature->GetVictim();
    // At its own pace on its way home, held, or under a script's or a flight's moves.
    if (!victim || !victim->IsCharmerOrOwnerPlayerOrPlayerItself() || creature->IsInEvadeMode()
        || creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        return 1.f;
    bool const fastNow = creature->GetFusionChase() > 1.05f;
    float const distance = creature->GetDistance(victim);
    // A gunner rushes in only while it closes in to shoot (its WoW chase, Gunfights.h): to its
    // hold range in sight of the target, or to anywhere it can see it from.
    if (creature->m_fusionGunRole == uint8(Fusion::GunRole::Gunner))
    {
        Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(creature->m_fusionGunId);
        if (!w || (creature->m_fusionGunner && creature->m_fusionGunner->mind.mode != Fusion::GunnerMode::Chase))
            return 1.f;
        float const hold = Fusion::GunnerHoldRange(w->weaponClass);
        bool const sight = creature->m_fusionGunner && creature->m_fusionGunner->sight.seeing;
        return Fusion::ChaseSpeed(distance, sight ? hold : 0.f, fastNow);
    }
    Fusion::CreatureTraits t;
    t.type = Fusion::CreatureType(creature->GetCreatureType());
    t.caster = creature->GetMaxPower(POWER_MANA) > 0;
    t.flying = creature->CanFly();
    t.large = creature->GetObjectBoundingRadius() > 1.2f;
    // Its reach to the target, edge to edge (GetDistance's terms): a melee swing's.
    float const reach = std::max(0.f, creature->GetCombatReachToTarget(victim, false, 0.f)
        - creature->GetObjectBoundingRadius() - victim->GetObjectBoundingRadius());
    return Fusion::ChaseSpeedMultiplier(Fusion::ClassifyCreature(t), distance, reach, fastNow);
}

void FusionMgr::OnDamage(Unit* attacker, Unit* victim, bool direct, bool fatal)
{
    OnCompanionDamage(attacker, victim);
    // A player's own blow with WoW's damage (a class spell, a strike, a technique): the client
    // makes the unit its target (its target-on-hit), as the fusion's Hit events do for rounds
    // and swings. Damage over time and a companion's are not the player's action.
    if (direct && !t_fusionHit && attacker && attacker != victim)
        if (Player* p = attacker->ToPlayer(); p && p->HasFusion())
            NoteStruck(p, victim, fatal);
    NoteHurt(victim);
    // Who last hurt a player (a killstreak craft of theirs turns on them first), and who is
    // shooting at a craft.
    if (Player* vp = victim->ToPlayer(); vp && vp->HasFusion() && attacker && attacker != victim)
    {
        FusionPlayerData& vd = vp->GetFusion();
        vd.lastHurtBy = attacker->GetObjectGuid().GetRawValue();
        vd.lastHurtByMs = WorldTimer::getMSTime();
    }
    Creature* c = victim->ToCreature();
    if (c && Fusion::Sky::CraftOfEntry(c->GetEntry()))
    {
        OnCraftHurt(attacker, c);
        return;
    }
    if (!c || !c->IsElite())
        return;
    Player* player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)
        return;
    uint32 const now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(m_creditLock);
    if (now - m_creditExpiredMs > 10000)
    {
        m_creditExpiredMs = now;
        m_credit.Expire(now);
    }
    m_credit.Add(c->GetObjectGuid().GetRawValue(), player->GetObjectGuid().GetRawValue(), now);
}

void FusionMgr::OnUnitKilled(Unit* killer, Unit* victim)
{
    // A wanderer's kill, or a wanderer killed (FusionWanderers.cpp).
    if (Creature* wc = killer ? killer->ToCreature() : nullptr; wc && wc->m_wanderer)
        sWandererMgr.OnKill(wc, victim);
    if (Creature* wv = victim->ToCreature(); wv && wv->m_wanderer)
        sWandererMgr.OnDeath(wv, killer);
    // A player who killed what was on a wanderer: remembered as a help.
    sWandererMgr.OnHelpKill(killer, victim);
    // A killstreak's craft shot down: its own burst and news, no kill of a creature.
    if (Creature* craft = victim->ToCreature(); craft && Fusion::Sky::CraftOfEntry(craft->GetEntry()))
    {
        OnCraftKilled(killer, craft);
        return;
    }
    OnCompanionKill(killer, victim);
    DropGunOf(victim);      // an NPC gunner's or a player's gun falls where they did
    sFusionClasses.OnKill(killer, victim);
    std::vector<uint64> damagers;
    if (victim->IsCreature())
    {
        std::lock_guard<std::mutex> lock(m_creditLock);
        damagers = m_credit.Take(victim->GetObjectGuid().GetRawValue());
    }
    if (Player* dead = victim->ToPlayer(); dead && dead->HasFusion())
    {
        OnPlayerDeath(dead);
        Player* by = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        CountDeath(dead, by);
        MatchKill(by, dead);
    }
    Player* player = killer->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (player && player != victim && player->HasFusion())
        CountKill(player, victim);
    if (!player || !player->HasFusion() || player == victim)
        return;

    // Killstreaks are PvP only (WOMW): an enemy player, worth less the lower their level.
    if (Player* dead = victim->ToPlayer())
    {
        // The fallen's rounds (limited ammunition, fusion/Ammo.h).
        ScavengeRounds(player, Fusion::BodyScavengeMags(Fusion::CurrentAmmoRules().playerScavengeMags, player->GetFusion().perks.scavenger));
        AwardStreak(player, Fusion::PlayerKillTenths(player->GetLevel(), dead->GetLevel()));
        return;
    }
    Creature* c = victim->ToCreature();
    if (!c || c->IsPet())
        return;
    if (c->GetCreatureType() == CREATURE_TYPE_CRITTER ||
        MaNGOS::XP::GetColorCode(player->GetLevel(), c->GetLevel()) == MaNGOS::XP::GRAY)
    {
        return;     // worth nothing
    }
    // A gunner's body carries rounds (limited ammunition, fusion/Ammo.h): the gun in hand takes a share.
    if (c->m_fusionGunner)
        ScavengeRounds(player, Fusion::BodyScavengeMags(Fusion::CurrentAmmoRules().gunnerScavengeMags
            * (c->IsElite() ? Fusion::CurrentAmmoRules().eliteScavengeScale : 1.f), player->GetFusion().perks.scavenger));
    // A boss in an instance leaves its armory to everyone who fought it.
    if (Fusion::CurrentAmmoRules().limited && FactsOf(c).boss && c->GetMap()->IsDungeon())
    {
        std::vector<uint64> fought = damagers;
        if (std::find(fought.begin(), fought.end(), player->GetObjectGuid().GetRawValue()) == fought.end())
            fought.push_back(player->GetObjectGuid().GetRawValue());
        for (uint64 guid : fought)
            if (Player* p = player->GetMap()->GetPlayer(ObjectGuid(guid)); p && p->HasFusion())
            {
                FusionPlayerData& pd = p->GetFusion();
                for (auto& kv : pd.ammo)
                    if (IsGunAmmo(m_catalog.Weapons(), kv.first))
                        kv.second.TopUp(Fusion::CurrentAmmoRules().bossRestockShare);
                if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(pd.heldWeaponId))
                    SendAmmo(p, *w);
                ChatHandler(p).SendSysMessage("The fallen boss's armory: your guns are restocked.");
            }
    }
    // A creature worth XP earns no streak, but it is a kill: Scavenger and the deathstreak see it
    // (an elite's, for each damager still on this map; the killer counts even if unlisted).
    if (std::find(damagers.begin(), damagers.end(), player->GetObjectGuid().GetRawValue()) == damagers.end())
        damagers.push_back(player->GetObjectGuid().GetRawValue());
    if (!c->IsElite())
        damagers = {player->GetObjectGuid().GetRawValue()};
    for (uint64 guid : damagers)
        if (Player* p = player->GetMap()->GetPlayer(ObjectGuid(guid)))
            if (p->HasFusion())
                AwardStreak(p, 0);
}

void FusionMgr::AwardStreak(Player* player, uint32 tenths)
{
    FusionPlayerData& d = player->GetFusion();
    d.deathStreak.OnKill();
    Fusion::Streak& streak = d.streak;
    std::vector<Fusion::StreakReward> earned = streak.AddTenths(tenths);
    // Scavenger: every kill worth XP brings a magazine back for every gun.
    if (d.perks.scavenger)
    {
        for (auto& kv : d.ammo)
            kv.second.Scavenge();
        if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(d.heldWeaponId))
            SendAmmo(player, *w);
    }
    if (tenths == 0)
        return;     // no streak change to show
    for (auto const& r : earned)
    {
        d.stats.Add({Fusion::Stat::StreaksEarned, Fusion::kAnyKey});
        d.stats.Add({Fusion::Stat::StreaksEarned, r.id});
    }
    if (!earned.empty())
        EvaluateAchievements(player);
    Fusion::Packets::Events ev;
    ev.events.push_back({Fusion::Packets::EventKind::StreakEarned, Fusion::Packets::EVENT_PROGRESS, 0, streak.Tenths(), 0});
    for (auto const& r : earned)
        ev.events.push_back({Fusion::Packets::EventKind::StreakEarned, 0, 0, r.kills, uint16(r.id)});
    SendEvents(player, ev);
}

void FusionMgr::LoadClasses(Player* player, std::unique_ptr<QueryResult> classes, std::unique_ptr<QueryResult> fusion)
{
    FusionPlayerData& d = player->GetFusion();
    LoadAchievements(player);
    LoadCompanionIndex(player);
    LoadConstructionSkill(player);
    if (classes)
    {
        do
        {
            Field* f = classes->Fetch();
            uint8 const slot = f[0].GetUInt8();
            if (slot >= Fusion::kClassSlots)
                continue;
            Fusion::ClassLoadout l;
            l.name = f[1].GetCppString();
            l.primary = f[2].GetUInt16();
            l.primaryAtt1 = f[3].GetCppString();
            l.primaryAtt2 = f[4].GetCppString();
            l.primaryCamo = f[5].GetUInt8();
            l.secondary = f[6].GetUInt16();
            l.secondaryAtt = f[7].GetCppString();
            l.lethal = f[8].GetUInt16();
            l.tactical = f[9].GetUInt16();
            l.perk1 = f[10].GetUInt16();
            l.perk2 = f[11].GetUInt16();
            l.perk3 = f[12].GetUInt16();
            l.deathstreak = f[13].GetUInt16();
            l.streak1 = f[14].GetUInt16();
            l.streak2 = f[15].GetUInt16();
            l.streak3 = f[16].GetUInt16();
            l.secondaryAtt2 = f[17].GetCppString();
            for (size_t t = 0; t < Fusion::kKitTalentTiers; ++t)
                l.talentPicks[t] = f[18 + t].GetUInt16();
            l.primaryAtt3 = f[24].GetCppString();
            l.perkPro = f[25].GetUInt8();
            l.primaryAtt4 = f[26].GetCppString();
            d.classes[slot] = std::move(l);
        } while (classes->NextRow());
    }
    if (fusion)
    {
        // The field editor's cooldown runs on across logouts.
        d.fieldEditor.readyAt = fusion->Fetch()[1].GetUInt64();
        uint8 const slot = fusion->Fetch()[0].GetUInt8();
        // A class saved under rules that have since changed (a gun removed from the catalog)
        // is not put in hand.
        if (slot < Fusion::kClassSlots && d.classes[slot])
        {
            Fusion::LoadoutError const e = Fusion::ValidateLoadout(m_catalog, *d.classes[slot], d.unlocks, ProPerks(player));
            if (e == Fusion::LoadoutError::None)
            {
                d.equippedSlot = slot;
                d.applied = d.classes[slot];
                ApplyPerks(player);
            }
            else
            {
                // Say why after the login reply (a GM's class, say, once no longer a GM).
                d.loginRefusal = e;
                d.loginRefusedSlot = slot;
            }
        }
    }
}

Fusion::PerkFlags const* FusionMgr::PerksOf(Unit const* unit) const
{
    if (!m_enabled || !unit)
        return nullptr;
    Player const* p = unit->ToPlayer();
    return p && p->HasFusion() ? &p->GetFusionConst().perks : nullptr;
}

Fusion::Crafts::Mods const* FusionMgr::AugmentsOf(Unit const* unit) const
{
    if (!m_enabled || !unit)
        return nullptr;
    Player const* p = unit->ToPlayer();
    return p && p->HasFusion() ? &p->GetFusionConst().augments : nullptr;
}

namespace
{
    // The kit stations' gossip options: their own sender ('KT'), apart from vanilla's menus (0)
    // and the fusion's others.
    constexpr uint32 kKitSender = 0x4B54;
    constexpr uint32 kGossipKits = 1;
    constexpr uint32 kGossipTrain = 2;
    constexpr uint32 kGossipRearm = 3;
}

void FusionMgr::RefillAmmo(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    if (std::optional<Fusion::WeaponProfile> const w = EffectiveWeapon(player, d.heldWeaponId))
        d.AmmoFor(*w);
    for (auto& kv : d.ammo)
        kv.second.FullResupply();
    // The client counts itself dry until told otherwise.
    if (player->IsInWorld())
        if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(d.heldWeaponId))
            SendAmmo(player, *w);
}

void FusionMgr::RefillEquipment(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    for (auto& kv : d.ammo)
        if (!IsGunAmmo(m_catalog.Weapons(), kv.first))
            kv.second.FullResupply();
    if (player->IsInWorld())
        if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(d.heldWeaponId))
            SendAmmo(player, *w);
}

void FusionMgr::RespawnAmmo(Player* player)
{
    if (!Fusion::CurrentAmmoRules().limited)
        return RefillAmmo(player);
    FusionPlayerData& d = player->GetFusion();
    if (std::optional<Fusion::WeaponProfile> const w = EffectiveWeapon(player, d.heldWeaponId))
        d.AmmoFor(*w);
    for (auto& kv : d.ammo)
    {
        if (IsGunAmmo(m_catalog.Weapons(), kv.first))
            kv.second.RespawnResupply();
        else
            kv.second.FullResupply();
    }
    if (player->IsInWorld())
        if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(d.heldWeaponId))
            SendAmmo(player, *w);
}

void FusionMgr::ScavengeRounds(Player* player, float mags)
{
    if (!Fusion::CurrentAmmoRules().limited || mags <= 0.f)
        return;
    FusionPlayerData& d = player->GetFusion();
    auto it = d.ammo.find(d.heldWeaponId);
    if (it == d.ammo.end() || !IsGunAmmo(m_catalog.Weapons(), it->first))
        return;
    if (it->second.ScavengeMags(mags))
        if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(d.heldWeaponId))
            SendAmmo(player, *w);
}

bool FusionMgr::OnGossipHello(Player* player, Creature* creature)
{
    // A stable master: the Companions window, not the stock stable or gossip.
    return CompanionStableHello(player, creature);
}

void FusionMgr::AddKitGossip(Player* player, Creature* creature)
{
    if (!IsEnabled() || !player->HasFusion() || !creature
        || !Fusion::IsKitStation(creature->GetUInt32Value(UNIT_NPC_FLAGS)) || creature->IsHostileTo(player))
        return;
    // Talking to one refills every magazine, reserve and piece of equipment, free (WOMW design,
    // section 9): under limited ammunition the trip back is the cost, not the purse.
    RefillAmmo(player);
    player->PlayerTalkClass->GetGossipMenu().AddMenuItem(GOSSIP_ICON_BATTLE, "Edit your kits", kKitSender,
        kGossipKits, "", false);
    // Rearm, as the old rearm key did in an inn or a city (fusion/Kits.h): the special rounds.
    uint32 const specialFee = Fusion::QuartermasterFee(player->GetLevel());
    player->PlayerTalkClass->GetGossipMenu().AddMenuItem(GOSSIP_ICON_VENDOR, Fusion::RearmGossipText(specialFee),
        kKitSender, kGossipRearm, "", false);
}

void FusionMgr::RearmAtStation(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const fee = Fusion::QuartermasterFee(player->GetLevel());
    Fusion::StationRearm const r = Fusion::PlanStationRearm(player->GetMoney(), fee);
    if (r == Fusion::StationRearm::Rounds)
    {
        player->ModifyMoney(-int32(fee));
        if (std::optional<Fusion::WeaponProfile> const w = EffectiveWeapon(player, d.heldWeaponId))
            d.AmmoFor(*w);
        for (auto& kv : d.ammo)
            kv.second.QuartermasterRearm(Fusion::EffectiveLevel(player->GetLevel(), IsAdmin(player)));
    }
    // Every magazine, reserve and piece of equipment back (and the client told), free.
    RefillAmmo(player);
    ChatHandler(player).SendSysMessage(Fusion::RearmResultText(r, fee).c_str());
}

bool FusionMgr::TrainerKitGossip(Player* player, Creature* creature)
{
    if (!IsEnabled() || !player->HasFusion() || !creature || creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP)
        || !Fusion::IsKitStation(creature->GetUInt32Value(UNIT_NPC_FLAGS)) || creature->IsHostileTo(player))
        return false;
    PlayerMenu* menu = player->PlayerTalkClass;
    menu->ClearMenus();
    if (creature->IsTrainerOf(player, false))
        menu->GetGossipMenu().AddMenuItem(GOSSIP_ICON_TRAINER, "Train me.", kKitSender, kGossipTrain, "", false);
    AddKitGossip(player, creature);
    menu->SendGossipMenu(player->GetGossipTextId(creature), creature->GetObjectGuid());
    return true;
}

bool FusionMgr::OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    // A stable master's revive, draught and window options (FusionCompanionRevive.cpp).
    if (CompanionStableSelect(player, creature, sender, action))
        return true;
    if (sender != kKitSender || !IsEnabled() || !player->HasFusion())
        return false;
    player->PlayerTalkClass->CloseGossip();
    if (!Fusion::IsKitStation(creature->GetUInt32Value(UNIT_NPC_FLAGS)) || creature->IsHostileTo(player))
        return true;
    if (action == kGossipTrain)
    {
        player->GetSession()->SendTrainerList(creature->GetObjectGuid());
        return true;
    }
    if (action == kGossipRearm)
    {
        RearmAtStation(player);
        return true;
    }
    SendChannel(player, Fusion::Packets::kChannelWorld, Fusion::OpenKitEditor(Fusion::KitOpener::Station));
    SendKitStatus(player);
    return true;
}

bool FusionMgr::AtKitStation(Player* player) const
{
    std::list<Creature*> around;
    MaNGOS::AnyUnitInObjectRangeCheck check(player, Fusion::kKitStationReach);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(around, check);
    Cell::VisitGridObjects(player, searcher, Fusion::kKitStationReach);
    return std::any_of(around.begin(), around.end(), [player](Creature const* c) {
        return Fusion::KitStationAllows(c->GetUInt32Value(UNIT_NPC_FLAGS), c->IsAlive(), c->IsHostileTo(player),
            player->GetDistance(c));
    });
}

Fusion::KitAccess FusionMgr::KitAccessNow(Player* player) const
{
    uint64 const now = uint64(sWorld.GetGameTime());
    return Fusion::KitAccessOf(IsAdmin(player), AtKitStation(player),
        Fusion::FieldEditorOpen(player->GetFusionConst().fieldEditor, now));
}

void FusionMgr::SendKitStatus(Player* player)
{
    if (!m_enabled || !player->HasFusion())
        return;
    uint64 const now = uint64(sWorld.GetGameTime());
    SendChannel(player, Fusion::Packets::kChannelWorld,
        Fusion::EncodeKitStatus(Fusion::KitStatusOf(KitAccessNow(player), player->GetFusionConst().fieldEditor, now)));
}

void FusionMgr::UseFieldEditor(Player* player)
{
    if (!m_enabled || !player->HasFusion())
        return;
    FusionPlayerData& d = player->GetFusion();
    uint64 const now = uint64(sWorld.GetGameTime());
    // Dead, or cooling down: the window stays as it is and its status line says why.
    if (player->IsAlive() && Fusion::UseFieldEditor(d.fieldEditor, now) == Fusion::FieldEditorUse::Opened)
        SendChannel(player, Fusion::Packets::kChannelWorld, Fusion::OpenKitEditor(Fusion::KitOpener::Field,
            uint16(Fusion::FieldEditorWindowLeft(d.fieldEditor, now))));
    SendKitStatus(player);
}

void FusionMgr::UseLedger(Player* player, ObjectGuid itemGuid)
{
    Item* item = player->GetItemByGuid(itemGuid);
    if (!m_enabled || !player->HasFusion() || !item || item->GetEntry() != Fusion::kLedgerItem)
        return;
    // From the bags only: not the bank, not a trade window.
    if (!Player::IsInventoryPos(item->GetPos()) || item->IsInTrade())
    {
        player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, item, nullptr);
        return;
    }
    if (!player->IsAlive())
    {
        player->SendEquipError(EQUIP_ERR_CANT_DO_RIGHT_NOW, item, nullptr);
        return;
    }
    // On cooldown the window still opens, to look, and says when it is ready.
    uint64 const now = uint64(sWorld.GetGameTime());
    if (Fusion::FieldEditorSecondsLeft(player->GetFusionConst().fieldEditor, now))
    {
        SendChannel(player, Fusion::Packets::kChannelWorld, Fusion::OpenKitEditor(Fusion::KitOpener::Hud));
        SendKitStatus(player);
        return;
    }
    UseFieldEditor(player);
}

void FusionMgr::HandleLoadout(Player* player, Fusion::Packets::LoadoutRequest const& sent)
{
    // Names are saved as clean UTF-8 (the client may send anything).
    Fusion::Packets::LoadoutRequest request = sent;
    for (auto& entry : request.classes)
        entry.second.name = Fusion::CleanClassName(entry.second.name);
    FusionPlayerData& d = player->GetFusion();
    if (!d.loadoutLimit.Allow(WorldTimer::getMSTime()))
    {
        SendLoadout(player, Fusion::Packets::LoadoutStatus::Rejected, Fusion::LoadoutError::TooFast, request.equipSlot);
        return;
    }
    // Kits are edited and switched only near a trainer or innkeeper, through the field editor or
    // by a GM; anywhere else they are only viewed (WOMW design, section 3).
    uint64 const now = uint64(sWorld.GetGameTime());
    Fusion::KitAccess access = Fusion::KitAccess::View;
    if (Fusion::KitRequestChanges(!request.classes.empty(), request.equipSlot, d.equippedSlot))
    {
        access = KitAccessNow(player);
        if (!Fusion::CanChangeKits(access))
        {
            SendLoadout(player, Fusion::Packets::LoadoutStatus::Rejected, Fusion::LoadoutError::NotAtKitStation,
                request.classes.empty() ? request.equipSlot : request.classes.begin()->first);
            SendKitStatus(player);
            return;
        }
    }
    for (auto const& [slot, l] : request.classes)
    {
        Fusion::LoadoutError e = Fusion::ValidateLoadout(m_catalog, l, d.unlocks, ProPerks(player));
        // The kit's talent picks: options the character bought, of their tiers, at its level.
        if (e == Fusion::LoadoutError::None
            && sFusionClasses.ValidatePicks(player, l.talentPicks) != Fusion::Classes::PickError::None)
            e = Fusion::LoadoutError::UnknownTalent;
        if (e != Fusion::LoadoutError::None)
        {
            SendLoadout(player, Fusion::Packets::LoadoutStatus::Rejected, e, slot);
            return;
        }
    }
    if (request.equipSlot != Fusion::kNoClass)
    {
        bool const saved = request.equipSlot < Fusion::kClassSlots && d.classes[request.equipSlot];
        bool const inRequest = std::any_of(request.classes.begin(), request.classes.end(),
            [&](auto const& c) { return c.first == request.equipSlot; });
        if (!saved && !inRequest)
        {
            SendLoadout(player, Fusion::Packets::LoadoutStatus::Rejected, Fusion::LoadoutError::UnknownWeapon, request.equipSlot);
            return;
        }
        // A class saved earlier is checked again: the player may have lost GM, or the catalog
        // changed since.
        if (!inRequest)
        {
            Fusion::LoadoutError const e = Fusion::ValidateLoadout(m_catalog, *d.classes[request.equipSlot],
                d.unlocks, ProPerks(player));
            if (e != Fusion::LoadoutError::None)
            {
                SendLoadout(player, Fusion::Packets::LoadoutStatus::Rejected, e, request.equipSlot);
                return;
            }
        }
    }

    static SqlStatementID saveClass;
    uint32 const guid = player->GetGUIDLow();
    for (auto const& [slot, l] : request.classes)
    {
        d.classes[slot] = l;
        SqlStatement stmt = CharacterDatabase.CreateStatement(saveClass,
            "REPLACE INTO `character_fusion_class` (`guid`, `slot`, `name`, `primary`, `primary_att1`, `primary_att2`, `primary_camo`, "
            "`secondary`, `secondary_att`, `lethal`, `tactical`, `perk1`, `perk2`, `perk3`, `deathstreak`, `streak1`, `streak2`, `streak3`, "
            "`secondary_att2`, `talent1`, `talent2`, `talent3`, `talent4`, `talent5`, `talent6`, `primary_att3`, `perk_pro`, `primary_att4`) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.addUInt32(guid);
        stmt.addUInt8(slot);
        stmt.addString(l.name);
        stmt.addUInt16(l.primary);
        stmt.addString(l.primaryAtt1);
        stmt.addString(l.primaryAtt2);
        stmt.addUInt8(l.primaryCamo);
        stmt.addUInt16(l.secondary);
        stmt.addString(l.secondaryAtt);
        for (uint16 v : {l.lethal, l.tactical, l.perk1, l.perk2, l.perk3, l.deathstreak, l.streak1, l.streak2, l.streak3})
            stmt.addUInt16(v);
        stmt.addString(l.secondaryAtt2);
        for (uint16 v : l.talentPicks)
            stmt.addUInt16(v);
        stmt.addString(l.primaryAtt3);
        stmt.addUInt8(l.perkPro);
        stmt.addString(l.primaryAtt4);
        stmt.Execute();
    }

    // A change made through the field editor starts its cooldown, saved with the character (the
    // column keeps the ledger's old name); the window's status line shows it.
    // One Man Army Pro has it ready again in half the time.
    if (access == Fusion::KitAccess::Field && Fusion::SpendFieldEditor(d.fieldEditor, now,
            Fusion::FieldEditorCooldown(d.perks, Fusion::kFieldEditorCooldownSeconds)
                - std::min<uint32>(d.augments.fieldEditorSeconds, Fusion::kFieldEditorCooldownSeconds / 2)))
    {
        static SqlStatementID saveFieldEditor;
        SqlStatement stmt = CharacterDatabase.CreateStatement(saveFieldEditor,
            "INSERT INTO `character_fusion` (`guid`, `ledger_ready_at`) VALUES (?, ?) "
            "ON DUPLICATE KEY UPDATE `ledger_ready_at` = VALUES(`ledger_ready_at`)");
        stmt.addUInt32(guid);
        stmt.addUInt64(d.fieldEditor.readyAt);
        stmt.Execute();
        SendKitStatus(player);
    }

    RequestEquip(player, request.equipSlot);
}

void FusionMgr::RequestEquip(Player* player, uint8 slot)
{
    FusionPlayerData& d = player->GetFusion();
    bool const changed = slot < Fusion::kClassSlots && d.classes[slot]
        && (slot != d.equippedSlot || !d.applied || !(*d.applied == *d.classes[slot]));
    Fusion::EquipState s{d.equippedSlot, d.pendingSlot};
    // One Man Army changes class even in combat.
    bool const waits = Fusion::EquipWaitsForCombat(player->IsInCombat(), d.perks.oneManArmy);
    Fusion::EquipOutcome const outcome = Fusion::PlanEquip(s, slot, changed, waits, player->IsAlive());
    d.pendingSlot = s.pending;
    switch (outcome)
    {
        case Fusion::EquipOutcome::Applied:
            if (changed)
                ApplyEquip(player, slot);
            else
                SendLoadout(player, Fusion::Packets::LoadoutStatus::Applied);
            break;
        case Fusion::EquipOutcome::Pending:
            SendLoadout(player, Fusion::Packets::LoadoutStatus::Pending);
            break;
        case Fusion::EquipOutcome::Saved:
            SendLoadout(player, Fusion::Packets::LoadoutStatus::Saved);
            break;
    }
}

void FusionMgr::ApplyEquip(Player* player, uint8 slot)
{
    FusionPlayerData& d = player->GetFusion();
    d.pendingSlot = Fusion::kNoClass;
    d.equippedSlot = slot;
    d.applied = d.classes[slot];
    CountStat(player, {Fusion::Stat::ClassSwaps, Fusion::kAnyKey});
    ClearPickups(player);   // a kit swap brings the kit's own guns back
    // A class's guns take its attachments' magazines: their ammo starts afresh.
    if (d.applied)
    {
        for (uint16 const gun : {d.applied->primary, d.applied->secondary})
        {
            d.ammo.erase(gun);
            d.ammo.erase(uint16(gun | FusionPlayerData::kLeftGun));
        }
    }
    ApplyPerks(player);
    static SqlStatementID saveEquipped;
    // Not REPLACE: the row also keeps the field editor's cooldown.
    SqlStatement stmt = CharacterDatabase.CreateStatement(saveEquipped,
        "INSERT INTO `character_fusion` (`guid`, `equipped_slot`) VALUES (?, ?) "
        "ON DUPLICATE KEY UPDATE `equipped_slot` = VALUES(`equipped_slot`)");
    stmt.PExecute(player->GetGUIDLow(), slot);
    SendLoadout(player, Fusion::Packets::LoadoutStatus::Applied);
}

std::optional<Fusion::WeaponProfile> FusionMgr::EffectiveWeapon(Player const* player, uint16 id) const
{
    Fusion::WeaponProfile const* gun = m_catalog.Weapons().Find(id);
    if (!gun)
        return std::nullopt;
    if (gun->slot == Fusion::WeaponSlot::Killstreak)
        return *gun;                    // Streak::Use checks it was earned
    if (gun->slot == Fusion::WeaponSlot::Lethal || gun->slot == Fusion::WeaponSlot::Tactical)
    {
        Fusion::ClassLoadout const* c = player->HasFusion() ? player->GetFusionConst().Equipped() : nullptr;
        if (!c || (c->lethal != id && c->tactical != id))
            return std::nullopt;
        return *gun;
    }
    if (gun->slot == Fusion::WeaponSlot::Alternate)
    {
        // An underbarrel weapon: only the equipped class's primary carrying it raises it (or a
        // picked-up primary carrying it).
        if (Fusion::Pickups::Gun const* p = player->HasFusion() ? player->GetFusionConst().pickups.For(Fusion::Pickups::Slot::Primary) : nullptr)
        {
            std::vector<std::string> atts;
            for (std::string const* a : {&p->att1, &p->att2})
                if (!a->empty())
                    atts.push_back(*a);
            return Fusion::AlternateAllowed(*gun, p->id, atts) ? std::optional<Fusion::WeaponProfile>(*gun) : std::nullopt;
        }
        Fusion::ClassLoadout const* c = player->HasFusion() ? player->GetFusionConst().Equipped() : nullptr;
        if (!c)
            return std::nullopt;
        std::vector<std::string> const atts = Fusion::PrimaryAttachments(*c);
        if (!Fusion::AlternateAllowed(*gun, c->primary, atts))
            return std::nullopt;
        return *gun;
    }
    if (!player->HasFusion())
        return *gun;
    // A picked-up gun with the attachments it was dropped with (Pickups.h).
    if (Fusion::Pickups::Gun const* p = player->GetFusionConst().pickups.Holding(id))
    {
        std::vector<std::string> atts;
        for (std::string const* a : {&p->att1, &p->att2})
            if (!a->empty())
                atts.push_back(*a);
        return m_catalog.Effective(*gun, std::move(atts));
    }
    Fusion::ClassLoadout const* c = player->GetFusionConst().Equipped();
    if (!c)
        return *gun;
    std::vector<std::string> atts;
    if (c->primary == id)
        atts = Fusion::PrimaryAttachments(*c);
    else if (c->secondary == id)
        atts = Fusion::SecondaryAttachments(*c);
    return m_catalog.Effective(*gun, std::move(atts));
}

void FusionMgr::ApplyPerks(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    d.perks = Fusion::PerksOf(d.Equipped(), ProPerks(player));
    // The kit's killstreaks (all of the ladder when it chose none), only those the account has
    // unlocked (fusion/Kits.h).
    d.streak.SetLoadout(Fusion::KitStreaks(d.Equipped(), Fusion::DefaultStreakLadder(), d.unlocks), d.perks.hardline);
    if (player->IsInWorld())
    {
        // Lightweight's speed.
        player->UpdateSpeed(MOVE_RUN, false);
        player->UpdateSpeed(MOVE_RUN_BACK, false);
    }
}

void FusionMgr::SendLoadout(Player* player, Fusion::Packets::LoadoutStatus status, Fusion::LoadoutError error, uint8 errorSlot) const
{
    FusionPlayerData const& d = player->GetFusion();
    Fusion::Packets::LoadoutReply reply;
    reply.status = status;
    reply.error = error;
    reply.errorSlot = errorSlot;
    reply.equippedSlot = d.equippedSlot;
    for (uint8 slot = 0; slot < Fusion::kClassSlots; ++slot)
        if (d.classes[slot])
            reply.classes.push_back({slot, *d.classes[slot]});
    std::vector<uint8_t> const body = Fusion::Packets::Encode(reply);
    WorldPacket data(SMSG_FUSION_LOADOUT, body.size());
    data.append(body.data(), body.size());
    player->GetSession()->SendPacket(&data);
}

Fusion::ActorState FusionMgr::ActorOf(Player const* player)
{
    Fusion::ActorState s;
    s.alive = player->IsAlive();
    s.mounted = player->IsMounted();
    s.taxi = player->IsTaxiFlying();
    s.swimming = player->IsInWater();
    s.stunned = player->HasUnitState(UNIT_STATE_STUNNED);
    s.rooted = player->HasUnitState(UNIT_STATE_ROOT);
    s.confused = player->HasUnitState(UNIT_STATE_CONFUSED);
    s.fleeing = player->HasUnitState(UNIT_STATE_FLEEING);
    s.pacified = player->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED);
    s.inSpline = !player->movespline->Finalized();
    s.airborne = player->m_movementInfo.HasMovementFlag(MovementFlags(MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR));
    if (player->HasFusion())
        s.stance = player->GetFusionConst().stance;
    return s;
}

void FusionMgr::SendAmmo(Player* player, Fusion::WeaponProfile const& base)
{
    Fusion::WeaponProfile const w = EffectiveWeapon(player, base.id).value_or(base);
    Fusion::Packets::Events ev;
    // An akimbo pair: the right gun's, then the left's (flagged).
    for (bool const left : {false, true})
    {
        if (left && !w.akimbo)
            break;
        Fusion::GunAmmo& ammo = player->GetFusion().AmmoFor(w, left);
        ev.events.push_back({Fusion::Packets::EventKind::Ammo, uint8(left ? Fusion::Packets::EVENT_LEFT : 0), 0, ammo.Mag(),
            uint16(std::min<uint32>(ammo.Reserve(), 0xFFFF))});
    }
    SendEvents(player, ev);
}

void FusionMgr::SendStreakProgress(Player* player) const
{
    Fusion::Packets::Events ev;
    ev.events.push_back({Fusion::Packets::EventKind::StreakEarned, Fusion::Packets::EVENT_PROGRESS, 0, player->GetFusion().streak.Tenths(), 0});
    SendEvents(player, ev);
}

float FusionMgr::HealthDamage(Player* shooter, Unit* target, Fusion::HitInput const& in) const
{
    Player* targetPlayer = target->GetCharmerOrOwnerPlayerOrPlayerItself();
    Fusion::CombatContext const context = targetPlayer && targetPlayer != shooter ? Fusion::CombatContext::PvP : Fusion::CombatContext::PvE;
    // The pools are the CoD scale: the hit is MW2's, times the shooter's M.
    // Gear never raises a gun's damage (design doc section 7): the level's multiplier only.
    float cod = Fusion::CodHitDamage(in, TraitsOf(target), context, GunMultiplier(shooter), m_coefficients);
    if (Player* tp = target->ToPlayer())
        return cod * (1.f - BulletCutOn(tp, in, Fusion::AmmoAgainst(in.ammo, TraitsOf(tp)).armorIgnore));
    // A pet or a totem keeps WoW's pool: hit it as a fresh player of its level.
    if (Creature* c = target->ToCreature(); c && !c->m_fusionWowMaxHealth)
        cod *= Fusion::Scale::PoolRatio(float(c->GetMaxHealth()), Fusion::Scale::PlayerMaxHealth(c->GetLevel(), 0.f));
    return cod;
}

void FusionMgr::TryMantle(Player* player)
{
    Map const* map = player->GetMap();
    float const x = player->GetPositionX();
    float const y = player->GetPositionY();
    float const z = player->GetPositionZ();
    float const o = player->GetOrientation();

    // Built pieces aren't in the height maps: their box tops are ground too, and their boxes walls.
    std::vector<Fusion::Build::Piece> const pieces = PiecesNear(map->GetId(), V(x, y, z), 8.f);
    auto groundAt = [&](float px, float py, float from)
    {
        float g = map->GetHeight(px, py, from);
        if (std::optional<float> const top = Fusion::Build::TopUnder(pieces, px, py, from); top && *top > g)
            g = *top;
        return g;
    };
    auto clear = [&](float ax, float ay, float az, float bx, float by, float bz)
    {
        if (!map->isInLineOfSight(ax, ay, az, bx, by, bz, true, false))
            return false;
        for (Fusion::Build::Piece const& piece : pieces)
            if (Fusion::Build::SegmentBlocked(piece, V(ax, ay, az), V(bx, by, bz)))
                return false;
        return true;
    };

    // Measured from the ground under the player, not the body: the request comes with the jump
    // that Space starts, so the body is already rising.
    Fusion::MantleProbe p;
    p.feetZ = groundAt(x, y, z + 0.5f);
    if (p.feetZ <= INVALID_HEIGHT)
        return;
    float top = p.feetZ;
    float topReach = Fusion::kMantleProbeReach[0];
    for (int i = 0; i < Fusion::kMantleColumns; ++i)
    {
        float const r = Fusion::kMantleProbeReach[i];
        float const g = groundAt(x + std::cos(o) * r, y + std::sin(o) * r, p.feetZ + Fusion::kMantleMaxRise + 0.5f);
        p.groundAhead[i] = g > INVALID_HEIGHT ? g : p.feetZ;
        if (p.groundAhead[i] > top)
        {
            top = p.groundAhead[i];
            topReach = r;
        }
    }
    // Room to stand on the ledge, and nothing between the chest and the ledge at its height.
    float const fx = x + std::cos(o) * topReach;
    float const fy = y + std::sin(o) * topReach;
    p.headroomClear = clear(fx, fy, top + 0.2f, fx, fy, top + Fusion::kMantleHeadroom)
        && clear(x, y, top + 0.5f, fx, fy, top + 0.5f);
    // An ordinary jump is fine (the request comes with one); a long fall is not.
    if (player->m_movementInfo.HasMovementFlag(MovementFlags(MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR)))
        p.airSeconds = player->m_movementInfo.GetFallTime() / 1000.f;
    p.startHeight = z - p.feetZ;

    std::optional<float> const rise = Fusion::MantleRise(p);
    if (!rise)
        return;
    Fusion::Impulse const imp = Fusion::MantleImpulse(*rise);
    player->SetLaunched(true);  // the anticheat expects a knock-back, as KnockBackFrom marks it
    player->KnockBack(o, imp.horizontal, imp.vertical);
    // The climber's client plays the vault (the gun dips, the hands plant): a knock-back alone
    // reads as a second hop. World op 22: [22, rise f32 (yards)].
    std::vector<uint8_t> body{22};
    uint8_t raw[4];
    float const r = *rise;
    std::memcpy(raw, &r, 4);
    body.insert(body.end(), raw, raw + 4);
    SendWorld(player, body);
}

void FusionMgr::TryLunge(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    float const range = Fusion::LungeRange(d.perks);
    Fusion::Vec3 const from = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());

    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(player, searcher, range);

    Unit* target = nullptr;
    float best = range + 1.f;
    for (Unit* u : units)
    {
        if (u == player || !u->IsAlive() || !player->IsValidAttackTarget(u) || !player->IsWithinLOSInMap(u))
            continue;
        if (!Fusion::InLungeCone(from, player->GetOrientation(), V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ())))
            continue;
        if (!PvpAllows(player, u))
            continue;
        float const dist = player->GetDistance(u);
        if (dist < best)
        {
            best = dist;
            target = u;
        }
    }
    if (!target)
        return;

    Fusion::Impulse const dash = Fusion::LungeImpulse(best);
    player->SetLaunched(true);
    player->KnockBack(player->GetAngle(target), dash.horizontal, dash.vertical);

    // The strike: the main hand's (fusion/Melee.h), on players and creatures alike.
    MeleeHit(player, target, MeleeDamage(player, target));
}

bool FusionMgr::PvpAllows(Player* player, Unit* u) const
{
    Player* tp = u->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!tp || tp == player)
        return true;
    Fusion::PvpPlayer const a = PvpOf(player, player->HasFusion() ? &player->GetFusionConst() : nullptr);
    Fusion::PvpPlayer const b = PvpOf(tp, tp->HasFusion() ? &tp->GetFusionConst() : nullptr);
    return Fusion::CanAttack(a, b, tp->GetVictim() == player || !m_pvpLowLevelProtection);
}

std::pair<uint32, uint32> FusionMgr::MeleeEntries(Player const* player) const
{
    if (player->HasFusion() && Fusion::ActionSystems::Unarmed(player->GetFusionConst().actionSystem))
        return {0, 0};
    Item const* main = player->GetWeaponForAttack(BASE_ATTACK, true, true);
    uint32 const entry = main && main->GetProto()->Class == ITEM_CLASS_WEAPON ? main->GetProto()->ItemId : 0;
    Item const* off = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    return {entry, off ? off->GetEntry() : 0};
}

// What the player's hands do, for everyone near to see on its body (ActionSystems.h ops 5-8): a
// pose or an act the client sent, checked and passed on with its guid, at a bounded rate.
void FusionMgr::RelayHands(Player* player, std::vector<uint8> const& body)
{
    namespace AS = Fusion::ActionSystems;
    FusionPlayerData& d = player->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    std::vector<uint8_t> out;
    if (std::optional<AS::Pose> const pose = AS::DecodePose(body))
    {
        if (!d.poseLimit.Allow(now))
            return;
        out = AS::EncodePoseOf(player->GetObjectGuid().GetRawValue(), *pose);
    }
    else if (std::optional<AS::Act> const act = AS::DecodeAct(body))
    {
        if (!d.actLimit.Allow(now))
            return;
        out = AS::EncodeActOf(player->GetObjectGuid().GetRawValue(), *act);
    }
    else
        return;
    TellNearOn(player->GetMap(), AS::kChannel, V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()),
        out, player, AS::kTellRadius);
}

void FusionMgr::SetActionSystem(Player* player, Fusion::ActionSystems::System system)
{
    FusionPlayerData& d = player->GetFusion();
    if (d.actionSystem == system)
        return;
    bool const wasUnarmed = Fusion::ActionSystems::Unarmed(d.actionSystem);
    d.actionSystem = system;
    bool const unarmed = Fusion::ActionSystems::Unarmed(system);
    if (unarmed == wasUnarmed)
        return;
    // Weapons away as WoW sheathes them, for everyone near to see; drawn again on the way out.
    player->SetSheath(unarmed ? SHEATH_STATE_UNARMED : SHEATH_STATE_MELEE);
    // WoW's own swing (the auto-attack the hands start) bare-handed too, as a disarm makes it
    // (Aura::HandleAuraModDisarm), unless a real disarm already has, or a form swings its own.
    if (!player->IsNoWeaponShapeShift() && !player->HasAuraType(SPELL_AURA_MOD_DISARM)
        && unarmed != player->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISARMED))
    {
        if (!unarmed)
        {
            player->ApplyModFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISARMED, false);
            player->SetRegularAttackTime();
        }
        if (Item* weapon = player->GetWeaponForAttack(BASE_ATTACK, true, true))
            player->_ApplyWeaponDependentAuraMods(weapon, BASE_ATTACK, !unarmed);
        if (unarmed)
        {
            player->ApplyModFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISARMED, true);
            player->SetAttackTime(BASE_ATTACK, BASE_ATTACK_TIME);
        }
        player->UpdateDamagePhysical(BASE_ATTACK);
    }
    SendMeleeProfile(player);
}

Fusion::Melee::Profile FusionMgr::MeleeOf(Player const* player, float* dps, bool* armed, uint32* entry, uint32* display) const
{
    Item const* item = player->GetWeaponForAttack(BASE_ATTACK, true, true);
    ItemPrototype const* proto = item ? item->GetProto() : nullptr;
    if (proto && proto->Class != ITEM_CLASS_WEAPON)
        proto = nullptr;
    // Utility (fusion/ActionSystems.h): the weapons are put away, the fists swing.
    if (player->HasFusion() && Fusion::ActionSystems::Unarmed(player->GetFusionConst().actionSystem))
        proto = nullptr;
    if (armed)
        *armed = proto != nullptr;
    if (entry)
        *entry = proto ? proto->ItemId : 0;
    if (display)
        *display = proto ? proto->DisplayInfoID : 0;
    if (dps)
    {
        float sum = 0.f;
        if (proto)
            for (auto const& dmg : proto->Damage)
                sum += 0.5f * (dmg.DamageMin + dmg.DamageMax);
        *dps = proto && proto->Delay ? sum / (float(proto->Delay) / 1000.f) : 0.f;
    }
    return Fusion::Melee::ProfileFor(proto ? uint8(std::min<uint32>(proto->SubClass, 254)) : Fusion::Melee::kNoWeapon);
}

uint32 FusionMgr::MeleeDamage(Player* player, Unit* target) const
{
    float dps = 0.f;
    bool armed = false;
    Fusion::Melee::Profile const p = MeleeOf(player, &dps, &armed);
    float damage = Fusion::Melee::Damage(p, dps, player->GetLevel(), armed, DamageMultiplier(player))
        * Fusion::MeleePerkMultiplier(player->GetFusionConst().perks)
        * player->GetFusionConst().augments.melee;     // the Bladesworn's glyph
    Player* targetPlayer = target->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (targetPlayer && targetPlayer != player)
        damage *= m_coefficients.pvp;
    else
        damage *= m_coefficients.pve;
    if (Player* tp = target->ToPlayer())
        damage *= 1.f - Fusion::Scale::PlayerArmorCut(float(tp->GetArmor()), tp->GetLevel());
    else if (Creature* c = target->ToCreature(); c && !c->m_fusionWowMaxHealth)
        damage *= Fusion::Scale::PoolRatio(float(c->GetMaxHealth()), Fusion::Scale::PlayerMaxHealth(c->GetLevel(), 0.f));
    return std::max<uint32>(1, uint32(damage + 0.5f));
}

void FusionMgr::NoteStruck(Player* player, Unit* target, bool fatal)
{
    FusionPlayerData& d = player->GetFusion();
    uint64 const guid = target->GetObjectGuid().GetRawValue();
    uint32 const now = WorldTimer::getMSTime();
    // A spray's ticks and a splash's targets: one word per unit per half second.
    if (!Fusion::Packets::StruckDue(d.lastStruckGuid, d.lastStruckMs, guid, now))
        return;
    d.lastStruckGuid = guid;
    d.lastStruckMs = now;
    // In a packet of its own, so a client that does not know the kind drops only this.
    Fusion::Packets::Events ev;
    // OnDamage runs before the blow is dealt: a killing blow's unit is still alive here, so
    // `fatal` (the damage reaches its health) marks it, and the client never targets the corpse.
    uint8 const flags = (fatal || !target->IsAlive()) ? Fusion::Packets::EVENT_FATAL : 0;
    ev.events.push_back({Fusion::Packets::EventKind::Struck, flags, guid, 0, 0});
    SendEvents(player, ev);
}

void FusionMgr::MeleeHit(Player* player, Unit* target, uint32 damage)
{
    FusionPlayerData& d = player->GetFusion();
    uint64 const guid = target->GetObjectGuid().GetRawValue();
    FusionHitSource melee;
    melee.melee = true;
    Hit(player, target, damage, melee);
    if (target->IsAlive())
        if (Creature* c = target->ToCreature())
            RallyAllies(c, player);
    bool const fatal = !target->IsAlive();

    Fusion::Packets::Events ev;
    uint8 const flags = fatal ? Fusion::Packets::EVENT_FATAL : 0;
    // The swing's own id: the client tells a blow from a gun's round by it (its target-on-hit).
    ev.events.push_back({Fusion::Packets::EventKind::Hit, flags, guid, damage, Fusion::Melee::kSwingWeaponId});
    if (fatal)
        ev.events.push_back({Fusion::Packets::EventKind::Kill, flags, guid, 0, Fusion::Melee::kSwingWeaponId});
    SendEvents(player, ev);
    if (Player* tp = target->ToPlayer())
    {
        Fusion::Packets::Events hurt;
        hurt.events.push_back({Fusion::Packets::EventKind::Hurt, flags, player->GetObjectGuid().GetRawValue(), damage, d.heldWeaponId});
        SendEvents(tp, hurt);
    }
}

void FusionMgr::Strike(Player* player, Fusion::Packets::Shot const& shot)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    Fusion::Melee::Profile const p = MeleeOf(player);
    // Never quicker than the weapon swings, a lunge counting as a swing.
    if (d.lastSwingMs && !Fusion::Melee::Ready(now - d.lastSwingMs, p.swingMs))
        return;
    if (d.lastLungeMs && !Fusion::Melee::Ready(now - d.lastLungeMs, p.swingMs))
        return;
    d.lastSwingMs = now;

    // The nearest enemy in reach of the hand, in the cone the player aims.
    Fusion::Vec3 aim = V(shot.direction.x, shot.direction.y, 0.f).Normalized();
    if (aim.Length() < 0.5f)
        aim = V(std::cos(player->GetOrientation()), std::sin(player->GetOrientation()), 0.f);
    float const facing = std::atan2(aim.y, aim.x);
    float const search = Fusion::Melee::kReach + player->GetObjectBoundingRadius() + 6.f;
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, search);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(player, searcher, search);
    Unit* target = nullptr;
    float best = 1e9f;
    for (Unit* u : units)
    {
        if (u == player || !u->IsAlive() || !player->IsValidAttackTarget(u))
            continue;
        // Edge to edge: a big body is reached sooner.
        float const gap = player->GetDistance(u);
        if (gap > Fusion::Melee::kReach)
            continue;
        float off = std::remainder(player->GetAngle(u) - facing, 2.f * float(M_PI));
        if (std::fabs(off) > Fusion::Melee::kConeHalf && gap > 0.5f)
            continue;
        if (!PvpAllows(player, u) || !player->IsWithinLOSInMap(u))
            continue;
        if (gap < best)
        {
            best = gap;
            target = u;
        }
    }
    if (!target)
    {
        // A swing that met nothing breaks the chain (fusion/Melee.h).
        Fusion::Melee::ComboBreak(d.combo);
        return;
    }
    // A rhythm of blows builds to a finisher; round the side or the back hits harder.
    float mult = Fusion::Melee::ComboHit(d.combo, now, p.swingMs);
    float const off = std::remainder(target->GetAngle(player) - target->GetOrientation(), 2.f * float(M_PI));
    mult *= Fusion::Melee::PositionalMultiplier(off);
    // A readied next-swing technique (Heroic Strike, Cleave, Maul) rides this swing, as WoW's
    // swing carries it (Unit::AttackerStateUpdate): it lands instead of the plain blow. The hands
    // never start WoW's auto-attack, so nothing else would ever consume it.
    if (Spell* queued = player->GetCurrentSpell(CURRENT_MELEE_SPELL))
    {
        queued->m_targets.setUnitTarget(target);
        queued->cast();
        if (queued->isSuccessCast())
            return;
    }
    MeleeHit(player, target, std::max<uint32>(1, uint32(float(MeleeDamage(player, target)) * mult + 0.5f)));
}

void FusionMgr::SendMeleeProfile(Player* player)
{
    uint32 entry = 0, display = 0;
    Fusion::Melee::Profile p = MeleeOf(player, nullptr, nullptr, &entry, &display);
    // Utility's tool in the hands (fusion/Gathering.h): its model, held as its kind of weapon,
    // swung at the tool's own pace. It deals no blows: the fists stay WoW's swing.
    FusionPlayerData const& fd = player->GetFusionConst();
    if (fd.actionSystem == Fusion::ActionSystems::System::Utility && fd.utilityTool)
        if (Fusion::Gathering::ToolDef const* tool = Fusion::Gathering::ToolInfo(fd.utilityTool))
        {
            uint32 const toolEntry = ToolItemOf(player, fd.utilityTool);
            ItemPrototype const* proto = toolEntry ? sObjectMgr.GetItemPrototype(toolEntry) : nullptr;
            if (proto)
            {
                p = Fusion::Melee::ProfileFor(uint8(std::min<uint32>(proto->SubClass, 254)));
                entry = proto->ItemId;
                display = proto->DisplayInfoID;
            }
            p.swingMs = uint16(tool->strikeMs);
        }
    // No lunge without a grant (fusion/Movement.h SwingLunges): the client then never asks for one.
    if (!Fusion::SwingLunges(LungeGrantOf(player)))
        p.lungeMs = 0;
    // The off hand: a second weapon, a shield or a held item, for the client's hands (none with
    // the weapons put away).
    Fusion::Melee::OffHand off;
    Item const* offItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    if (player->GetFusionConst().actionSystem == Fusion::ActionSystems::System::Utility)
        offItem = nullptr;
    if (ItemPrototype const* proto = offItem ? offItem->GetProto() : nullptr)
    {
        off.displayId = proto->DisplayInfoID;
        if (proto->Class == ITEM_CLASS_WEAPON)
        {
            off.kind = Fusion::Melee::OffHandKind::Weapon;
            off.subclass = uint8(std::min<uint32>(proto->SubClass, 254));
        }
        else if (proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
            off.kind = Fusion::Melee::OffHandKind::Shield;
        else
            off.kind = Fusion::Melee::OffHandKind::Held;
    }
    player->GetFusion().meleeEntry = entry;
    player->GetFusion().offHandEntry = offItem ? offItem->GetEntry() : 0;
    SendWorld(player, Fusion::Melee::EncodeProfile(p, entry, display, off));
}

void FusionMgr::OnPlayerDeath(Player* victim)
{
    if (!victim->HasFusion())
        return;
    FusionPlayerData& d = victim->GetFusion();
    if (d.streak.Tenths() > 0)
    {
        d.streak.Reset();
        SendStreakProgress(victim);
    }
    d.deathStreak.OnDeath();
    EndDowned(victim);
    // Martyrdom: a grenade drops where the player fell.
    if (DeathstreakReady(d, Fusion::PerkId::Martyrdom))
    {
        d.martyrdomAtMs = WorldTimer::getMSTime() + uint32(Fusion::kMartyrdomDelay * 1000.f);
        d.martyrdomPos = V(victim->GetPositionX(), victim->GetPositionY(), victim->GetPositionZ() + 0.3f);
        d.martyrdomMap = victim->GetMapId();
        ChatHandler(victim).SendSysMessage("Martyrdom: grenade dropped.");
    }
    ArmoryOnDeath(victim);  // pickups and enchanted rounds end with the life
}

Fusion::Vec3 FusionMgr::AimPoint(Player* player, Fusion::Vec3 origin, Fusion::Vec3 dir, float reach) const
{
    Map const* map = player->GetMap();
    Fusion::Vec3 end = origin + dir * reach;
    float wx, wy, wz;
    if (VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(map->GetId(), origin.x, origin.y, origin.z,
            end.x, end.y, end.z, wx, wy, wz, -0.2f))
        end = V(wx, wy, wz);
    // The ground: the first step whose point is under the terrain.
    float const length = (end - origin).Length();
    for (float t = 2.f; t < length; t += 2.f)
    {
        Fusion::Vec3 const p = origin + dir * t;
        float const ground = map->GetHeight(p.x, p.y, p.z + 2.f);
        if (ground > INVALID_HEIGHT && p.z < ground)
            return V(p.x, p.y, ground + 0.2f);
    }
    return end;
}

Unit* FusionMgr::NearestTarget(Player* owner, Fusion::Vec3 around, float radius, Fusion::Vec3 from) const
{
    float const reach = radius + (V(owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ()) - around).Length();
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(owner, owner, reach);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(owner, searcher, reach);
    Map const* map = owner->GetMap();
    Unit* best = nullptr;
    float bestD = radius;
    for (Unit* u : units)
    {
        if (u == owner || !u->IsAlive() || !owner->IsValidAttackTarget(u))
            continue;
        Fusion::Vec3 const body = V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + 1.f);
        float const dist = (body - around).Length();
        if (dist > bestD || !map->isInLineOfSight(from.x, from.y, from.z, body.x, body.y, body.z, true, true))
            continue;
        best = u;
        bestD = dist;
    }
    return best;
}

void FusionMgr::UseEquipment(Player* player, Fusion::WeaponProfile const& w, Fusion::Packets::Shot const& shot)
{
    FusionPlayerData& d = player->GetFusion();
    Fusion::EquipmentKind const kind = Fusion::KindOf(w.name);
    uint32 const now = WorldTimer::getMSTime();
    // C4's trigger: what is out goes off.
    if (kind == Fusion::EquipmentKind::C4 && (shot.flags & Fusion::Packets::SHOT_DETONATE))
    {
        for (FusionCharge& c : d.charges)
            if (c.kind == FusionCharge::Kind::C4)
            {
                c.kind = FusionCharge::Kind::Blast;
                c.atMs = now;
            }
        return;
    }
    if (kind == Fusion::EquipmentKind::None)
        return;
    // No throw from a skyride in the air (fusion/Riding.h ThrowsFromSaddle); the charge is kept.
    if (player->IsMounted())
    {
        float const ground = player->GetMap()->GetHeight(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
        float const above = ground > INVALID_HEIGHT ? player->GetPositionZ() - ground : 0.f;
        if (!Fusion::Riding::ThrowsFromSaddle(RidesSkyrider(player), above))
            return;
    }
    if (!d.AmmoFor(w).Fire())
        return;
    Fusion::Vec3 const origin = V(shot.origin.x, shot.origin.y, shot.origin.z);
    Fusion::Vec3 const dir = V(shot.direction.x, shot.direction.y, shot.direction.z).Normalized();
    if (dir.Length() < 0.5f)
        return;
    if (kind == Fusion::EquipmentKind::ThrowingKnife)
    {
        ThrowKnife(player, w, origin, dir);
        return;
    }
    FusionCharge c;
    c.weaponId = w.id;
    c.radius = w.explosionRadius;
    c.inner = w.explosionInner;
    c.outer = w.explosionOuter;
    c.mapId = player->GetMapId();
    if (kind == Fusion::EquipmentKind::Claymore)
    {
        // Planted a yard ahead of the feet, facing the way the player faces.
        float const o = player->GetOrientation();
        c.kind = FusionCharge::Kind::Claymore;
        c.facing = o;
        c.pos = V(player->GetPositionX() + std::cos(o), player->GetPositionY() + std::sin(o), player->GetPositionZ() + 0.2f);
        d.charges.push_back(c);
        return;
    }
    Map const* map = player->GetMap();
    if (kind == Fusion::EquipmentKind::Frag)
    {
        // A frag bounces and rolls (Bounce.h) and goes off where it is when its fuse runs out:
        // the fuse less what the thrower cooked it in the hand (the shot's seed carries the cook
        // in ms). Everyone near, the thrower too, is told its path (op 35) to draw it.
        float const fuse = Fusion::Bounce::CookedFuse(Fusion::FuseSeconds(kind), shot.seed);
        Fusion::Bounce::Path const path = FlyFrag(map, origin, dir * Fusion::kThrowSpeed, Fusion::kWowGravity, fuse);
        c.kind = FusionCharge::Kind::Blast;
        c.pos = path.end + Fusion::Vec3{0.f, 0.f, 0.1f};
        c.atMs = now + path.fuseMs;
        d.charges.push_back(c);
        std::vector<uint8_t> const seen = Fusion::Bounce::EncodePath(player->GetObjectGuid().GetRawValue(), w.id,
            Fusion::kWowGravity, path);
        TellNear(player->GetMap(), origin, [&](Player*) { return seen; });
        return;
    }
    // Thrown: the arc to where it lands.
    Fusion::Throw const t = Fusion::ThrowArc(origin, dir, Fusion::kThrowSpeed, [map](Fusion::Vec3 a, Fusion::Vec3 b) -> std::optional<Fusion::Vec3> {
        float wx, wy, wz;
        std::optional<Fusion::Vec3> wall;
        if (VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(map->GetId(), a.x, a.y, a.z, b.x, b.y, b.z, wx, wy, wz, -0.1f))
            wall = V(wx, wy, wz);
        float const ground = map->GetHeight(b.x, b.y, b.z + 3.f);
        if (ground > INVALID_HEIGHT && b.z <= ground)
        {
            Fusion::Vec3 const g = V(b.x, b.y, ground + 0.1f);
            if (!wall || (g - a).Length() < (*wall - a).Length())
                return g;
        }
        return wall;
    });
    c.pos = t.at;
    float const fuse = Fusion::FuseSeconds(kind);
    switch (kind)
    {
        case Fusion::EquipmentKind::Semtex:
            c.kind = FusionCharge::Kind::Blast;      // sticks, then its fuse
            c.atMs = now + uint32((t.seconds + fuse) * 1000.f);
            break;
        case Fusion::EquipmentKind::C4:
            c.kind = FusionCharge::Kind::C4;
            break;
        case Fusion::EquipmentKind::Flash:
            c.kind = FusionCharge::Kind::Flash;
            c.atMs = now + uint32((t.seconds + fuse) * 1000.f);
            break;
        case Fusion::EquipmentKind::Stun:
            c.kind = FusionCharge::Kind::Stun;
            c.atMs = now + uint32((t.seconds + fuse) * 1000.f);
            break;
        case Fusion::EquipmentKind::Smoke:
            c.kind = FusionCharge::Kind::Smoke;
            c.atMs = now + uint32((t.seconds + fuse) * 1000.f);
            break;
        default:
            return;
    }
    d.charges.push_back(c);
}

void FusionMgr::ThrowKnife(Player* player, Fusion::WeaponProfile const& w, Fusion::Vec3 origin, Fusion::Vec3 dir)
{
    // A straight flight to the nearest body within reach of the line, in sight.
    float const reach = 25.f;
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, reach);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(player, searcher, reach);
    Map const* map = player->GetMap();
    Unit* target = nullptr;
    float best = reach;
    for (Unit* u : units)
    {
        if (u == player || !u->IsAlive() || !player->IsValidAttackTarget(u))
            continue;
        Fusion::Vec3 const body = V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + 1.f);
        float const along = (body - origin).Dot(dir);
        if (along <= 0.f || along > best)
            continue;
        float const miss = (body - (origin + dir * along)).Length();
        if (miss > std::max(0.8f, u->GetObjectBoundingRadius() + 0.3f))
            continue;
        if (!map->isInLineOfSight(origin.x, origin.y, origin.z, body.x, body.y, body.z, true, true))
            continue;
        target = u;
        best = along;
    }
    if (!target)
        return;
    // As the knife: a player dies; a creature takes three body shots of the gun in hand.
    uint32 damage;
    if (Player* tp = target->ToPlayer())
        damage = tp->GetHealth();
    else
    {
        FusionPlayerData& d = player->GetFusion();
        Fusion::WeaponProfile const* gun = m_catalog.Weapons().Find(d.heldWeaponId);
        if (!gun || !Fusion::FiresRounds(gun->weaponClass))
            gun = m_catalog.Weapons().Find(std::string("m4_mp"));
        if (!gun)
            return;
        Fusion::HitInput in;
        in.weapon = gun;
        in.distance = 1.f;
        in.location = Fusion::HitLocation::TorsoUpper;
        in.ammo = Fusion::AmmoType::Basic;
        in.attacker = StatsOf(player);
        damage = std::max<uint32>(1, uint32(Fusion::LungeCreatureDamage(HealthDamage(player, target, in)) + 0.5f));
    }
    uint64 const guid = target->GetObjectGuid().GetRawValue();
    FusionHitSource knife;
    knife.weaponId = w.id;
    knife.equipment = Fusion::EquipmentKind::ThrowingKnife;
    Hit(player, target, damage, knife);
    bool const fatal = !target->IsAlive();
    Fusion::Packets::Events ev;
    uint8 const flags = fatal ? Fusion::Packets::EVENT_FATAL : 0;
    ev.events.push_back({Fusion::Packets::EventKind::Hit, flags, guid, damage, w.id});
    if (fatal)
        ev.events.push_back({Fusion::Packets::EventKind::Kill, flags, guid, 0, w.id});
    SendEvents(player, ev);
}

void FusionMgr::ApplyTactical(Player* owner, FusionCharge const& c)
{
    // A flash stuns, a stun grenade dazes, smoke blinds: each a spell of the world's own.
    uint32 const spell = c.kind == FusionCharge::Kind::Flash ? 56u        // Stun (3 s)
        : c.kind == FusionCharge::Kind::Stun ? 1604u                       // Dazed
        : 21060u;                                                          // Blind
    // Intellect's equipment strength: a bigger smoke, a longer flash or stun (fusion/Stats.h).
    float const radius = c.kind == FusionCharge::Kind::Smoke ? Fusion::kSmokeRadius * (1.f + EquipmentStrength(owner))
        : std::max(c.radius, c.kind == FusionCharge::Kind::Flash ? Fusion::kFlashRadius : Fusion::kStunRadius);
    float const reach = radius + (V(owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ()) - c.pos).Length();
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(owner, owner, reach);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(owner, searcher, reach);
    Map const* map = owner->GetMap();
    for (Unit* u : units)
    {
        if (u == owner || !u->IsAlive() || !owner->IsValidAttackTarget(u))
            continue;
        Fusion::Vec3 const body = V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + 1.f);
        if ((body - c.pos).Length() > radius
            || !map->isInLineOfSight(c.pos.x, c.pos.y, c.pos.z + 0.5f, body.x, body.y, body.z, true, true))
            continue;
        owner->CastSpell(u, spell, true);
        if (c.kind != FusionCharge::Kind::Smoke)
            StrengthenTactical(owner, u, spell);
        // A player caught sees it too: the flash whites out the view, the stun grenade dazes it
        // (world op 41, [41, 1 flash | 2 stun | 3 smoke]).
        if (Player* p = u->ToPlayer(); p && p->HasFusion())
            SendWorld(p, {Fusion::kOpTacticalHit, uint8_t(c.kind == FusionCharge::Kind::Flash ? 1
                : c.kind == FusionCharge::Kind::Stun ? 2 : 3)});
    }
}

void FusionMgr::CallIn(Player* player, Fusion::WeaponProfile const& w, Fusion::Packets::Shot const& shot)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const id = w.id - 3000u;
    if (!d.streak.Use(id))
        return;                         // not earned
    uint32 const now = WorldTimer::getMSTime();
    Fusion::Vec3 const origin = V(shot.origin.x, shot.origin.y, shot.origin.z);
    Fusion::Vec3 const dir = V(shot.direction.x, shot.direction.y, shot.direction.z).Normalized();
    // The lore killstreaks fly and stand as creatures (FusionSkyStreaks.cpp). One the world
    // refuses (no ground for the turret, a rocket already up) is handed back.
    if (Fusion::Sky::CraftOf(id))
    {
        if (!CallSkyStreak(player, id, origin, dir))
        {
            GrantStreak(player, id);
            return;
        }
        // Wanderers near look up (FusionWandererLives.cpp).
        sWandererMgr.NoteWorldEvent(player->GetMapId(), Fusion::Vec3(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()), Fusion::Wanderers::NewsKind::Aircraft, player->GetName());
    }
    d.stats.Add({Fusion::Stat::StreaksCalled, Fusion::kAnyKey});
    d.stats.Add({Fusion::Stat::StreaksCalled, id});
    EvaluateAchievements(player);
    if (id == Fusion::StreakId::TacticalNuke)
    {
        FusionCharge c;
        c.weaponId = w.id;
        c.mapId = player->GetMapId();
        c.kind = FusionCharge::Kind::Nuke;
        c.atMs = now + 10000;
        d.charges.push_back(c);
        ChatHandler(player).SendSysMessage("Tactical nuke incoming.");
    }
    // The UAV is the client's sweep.
}

void FusionMgr::UpdateCharges(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    auto due = [now](uint32 at) { return at && int32(now - at) >= 0; };
    Fusion::Vec3 const me = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    std::vector<FusionCharge> keep;
    // Copied: a blast can kill the player and change what is out.
    std::vector<FusionCharge> charges;
    charges.swap(d.charges);
    for (FusionCharge& c : charges)
    {
        if (c.mapId != player->GetMapId())
            continue;                   // left behind
        switch (c.kind)
        {
            case FusionCharge::Kind::Blast:
                if (!due(c.atMs))
                {
                    keep.push_back(c);
                    break;
                }
                // An enemy shaman's Grounding Totem catches it (FusionClassAbilities.cpp).
                sFusionClasses.GroundingCatch(player, c.mapId, c.pos);
                Explode(player, c.pos, c.radius, c.inner, c.outer, c.weaponId);
                break;
            case FusionCharge::Kind::NpcFrag:
                if (!due(c.atMs))
                    keep.push_back(c);
                else
                {
                    if (Unit* thrower = ObjectAccessor::GetUnit(*player, ObjectGuid(c.thrower)))
                        sFusionClasses.GroundingCatch(thrower, c.mapId, c.pos);
                    NpcExplode(player, c);
                }
                break;
            case FusionCharge::Kind::C4:
                keep.push_back(c);      // until its trigger
                break;
            case FusionCharge::Kind::Claymore:
            {
                if (c.nextMs && int32(now - c.nextMs) < 0)
                {
                    keep.push_back(c);
                    break;
                }
                c.nextMs = now + 200;
                // Intellect's equipment strength reaches the tripwire and the blast farther.
                float const strength = 1.f + EquipmentStrength(player);
                Unit* body = NearestTarget(player, c.pos, Fusion::kClaymoreRange * strength, c.pos + Fusion::Vec3{0.f, 0.f, 0.5f});
                if (body && Fusion::ClaymoreTripped(c.pos, c.facing, V(body->GetPositionX(), body->GetPositionY(), body->GetPositionZ()),
                        Fusion::kClaymoreRange * strength))
                    Explode(player, c.pos, c.radius * strength, c.inner, c.outer, c.weaponId);
                else
                    keep.push_back(c);
                break;
            }
            case FusionCharge::Kind::Flash:
            case FusionCharge::Kind::Stun:
            case FusionCharge::Kind::Smoke:
                if (!due(c.atMs))
                    keep.push_back(c);
                else
                {
                    sFusionClasses.GroundingCatch(player, c.mapId, c.pos);
                    ApplyTactical(player, c);
                }
                break;
            case FusionCharge::Kind::Hover:
            case FusionCharge::Kind::Gunner:
            {
                if (due(c.untilMs))
                    break;              // its time is up
                if (!due(c.nextMs))
                {
                    keep.push_back(c);
                    break;
                }
                // The harrier over its spot, the gunner over the player: fire on the nearest
                // enemy the sky can see.
                bool const gunner = c.kind == FusionCharge::Kind::Gunner;
                Fusion::Vec3 const over = gunner ? me : c.pos;
                c.nextMs = now + (gunner ? 300 : 1000);
                if (Unit* t = NearestTarget(player, over, gunner ? 50.f : 30.f, over + Fusion::Vec3{0.f, 0.f, 25.f}))
                {
                    Fusion::Vec3 const at = V(t->GetPositionX(), t->GetPositionY(), t->GetPositionZ() + 0.5f);
                    if (gunner)
                        Explode(player, at, c.radius, c.inner, c.outer, c.weaponId);
                    else
                        Explode(player, at, 1.5f, 60.f, 40.f, c.weaponId);
                }
                keep.push_back(c);
                break;
            }
            case FusionCharge::Kind::Nuke:
            {
                if (!due(c.atMs))
                {
                    keep.push_back(c);
                    break;
                }
                std::list<Unit*> units;
                MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, 150.f);
                MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
                Cell::VisitAllObjects(player, searcher, 150.f);
                Fusion::Packets::Events ev;
                for (Unit* u : units)
                {
                    if (u == player || !u->IsAlive() || !player->IsValidAttackTarget(u))
                        continue;
                    if (Creature* cr = u->ToCreature(); cr && cr->IsWorldBoss())
                        continue;       // a raid boss shrugs it off
                    uint64 const guid = u->GetObjectGuid().GetRawValue();
                    uint32 const damage = u->GetHealth();
                    Hit(player, u, damage);
                    ev.events.push_back({Fusion::Packets::EventKind::Kill, Fusion::Packets::EVENT_FATAL, guid, 0, c.weaponId});
                    if (ev.events.size() + 1 > Fusion::Packets::kMaxEventsPerPacket)
                    {
                        SendEvents(player, ev);
                        ev.events.clear();
                    }
                }
                SendEvents(player, ev);
                break;
            }
        }
    }
    // Anything added while these went off (none today) stays too.
    for (FusionCharge& c : d.charges)
        keep.push_back(c);
    d.charges.swap(keep);
}

void FusionMgr::RecordCreaturePose(Creature* c, uint32 now)
{
    // Only in a fight (the rest stand still or stroll), and a killstreak's craft (always on the
    // move, and shot at from afar): a sample every 50 ms, a second's worth.
    if (!c->IsInCombat() && !Fusion::Sky::CraftOfEntry(c->GetEntry()))
    {
        c->m_fusionPoses.reset();
        return;
    }
    if (c->m_fusionPoses && int32(now - c->m_fusionPoseMs) < 50)
        return;
    if (!c->m_fusionPoses)
        c->m_fusionPoses = std::make_shared<Fusion::PoseHistory>();
    c->m_fusionPoseMs = now;
    Fusion::BodyPose pose;
    pose.feet = V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ());
    pose.orientation = c->GetOrientation();
    c->m_fusionPoses->Record(now, pose);
}

void FusionMgr::UpdateCreatureGun(Creature* c, uint32 diffMs)
{
    c->m_fusionGunClockMs += diffMs;
    if (!c->IsPet())
        RecordCreaturePose(c, WorldTimer::getMSTime());
    // A wanderer carries its gun its own way (FusionWanderers.cpp, WandererRound below).
    if (c->m_wanderer)
        return;
    if (c->m_fusionGunRole == 0xFF)
    {
        // Classify once: pets, totems, guards of a world boss and the like keep their ways; the
        // gunner camps (by faction) mostly carry guns, the wild humanoids rarely, bosses never.
        Fusion::GunFacts const f = GunFactsOf(c);
        Fusion::GunRole role = c->IsPet() || c->IsTotem() || c->IsWorldBoss() || c->IsCivilian()
            ? Fusion::GunRole::None : Fusion::ClassifyGunner(f);
        uint16 gunId = 0;
        if (role == Fusion::GunRole::Gunner)
        {
            CreatureInfo const* ci = c->GetCreatureInfo();
            Fusion::CreatureRank const rank = ci && ci->rank <= 4 ? Fusion::CreatureRank(ci->rank) : Fusion::CreatureRank::Normal;
            if (Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(Fusion::NpcGunFor(c->GetEntry(), c->GetLevel(), rank)))
                gunId = w->id;
            else
                role = Fusion::GunRole::MeleeTrash;
        }
        c->m_fusionGunRole = uint8(role);
        c->m_fusionGunId = gunId;
        if (role == Fusion::GunRole::Gunner)
            c->SetVirtualItem(RANGED_ATTACK, Fusion::NpcGunItemFor(c->GetLevel()));
    }
    if (c->m_fusionGunRole != uint8(Fusion::GunRole::Gunner))
        return;

    Unit* victim = c->GetVictim();
    Player* target = victim ? victim->ToPlayer() : nullptr;
    MotionMaster* mm = c->GetMotionMaster();
    // Stunned, feared, disarmed (a warrior's Disarm knocks its gun aside) or casting a spell of
    // its own: it sits this tick out, its soldiering kept (tearing it down here put a gunner that
    // cast its WoW Shoot back on its melee chase).
    if (c->IsInCombat() && target && target->IsAlive() && !c->IsInEvadeMode() && c->GetCharmerOrOwnerGuid().IsEmpty()
        && (c->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL) || c->IsNonMeleeSpellCasted(false)
            || c->HasAuraType(SPELL_AURA_MOD_DISARM)))
        return;
    if (!c->IsInCombat() || !target || !target->IsAlive() || c->IsInEvadeMode() || !c->GetCharmerOrOwnerGuid().IsEmpty())
    {
        // No player to shoot (out of the fight, or on a pet), or not its own master: its own
        // footwork back, and the soldiering starts over next fight.
        if (c->m_fusionStandingOff)
        {
            c->m_fusionStandingOff = false;
            if (mm->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
                mm->MovementExpired(false);
            if (c->AI())
                c->AI()->SetCombatMovement(true);
        }
        if (c->m_fusionGunner)
            c->m_fusionGunner->mind = Fusion::GunnerMind();
        if (!c->IsInCombat() && c->GetSheath() == SHEATH_STATE_RANGED)
            c->SetSheath(SHEATH_STATE_MELEE);
        return;
    }

    // A gunner fights like an MW2 soldier (NpcGuns.h): it closes in (its WoW chase) until it sees
    // its target within its gun's hold range, then keeps moving from firing spot to firing spot,
    // shooting on the move; under fire it takes cover and peeks out to shoot. At arm's length it
    // puts the gun away and fights hand to hand (its melee, on the CoD scale).
    Fusion::WeaponProfile const* w = m_catalog.Weapons().Find(c->m_fusionGunId);
    if (!w)
        return;
    if (!c->m_fusionGunner)
        c->m_fusionGunner = std::make_shared<FusionGunnerState>();
    FusionGunnerState& g = *c->m_fusionGunner;
    uint32 const now = WorldTimer::getMSTime();
    FusionPlayerData const& d = target->GetFusion();
    float const dist = c->GetDistance(target);
    bool const sight = dist <= Fusion::kGunnerReach && c->IsWithinLOSInMap(target);
    Fusion::Vec3 const feet = V(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
    g.sight.Update(target->GetObjectGuid().GetRawValue(), sight, d.sprinting, feet, now);
    g.aim.Update(target->GetObjectGuid().GetRawValue(), sight, now);
    bool const hurt = g.hurt.Update(c->GetHealthPercent(), now);
    // Its own moves only over its chase, standing or a run to a spot (not a script's path, its
    // way home or a flight).
    // Standing off, nothing puts it back on its melee chase but its own think: its WoW AI turns
    // combat movement on again (a ranged spell's cast failing, an AttackStart from a call for
    // help), and that chase runs it into the target's face.
    if (c->AI() && g.mind.mode != Fusion::GunnerMode::Chase && mm->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
    {
        c->m_fusionStandingOff = true;
        mm->MovementExpired(false);
        mm->MoveIdle();
        c->AI()->SetCombatMovement(false);
        c->StopMoving();
    }
    MovementGeneratorType const moving = mm->GetCurrentMovementGeneratorType();
    if (c->AI() && (moving == CHASE_MOTION_TYPE || moving == IDLE_MOTION_TYPE || moving == POINT_MOTION_TYPE))
    {
        Fusion::GunnerSense s;
        s.distance = dist;
        s.sight = sight;
        s.holdRange = Fusion::GunnerHoldRange(w->weaponClass);
        s.hurt = hurt;
        s.shotAt = g.shotAtMs && now - g.shotAtMs <= Fusion::kShotAtWindowMs;
        Fusion::GunnerAct const act = Fusion::GunnerThink(g.mind, s, now);
        if (act != Fusion::GunnerAct::None)
            GunnerDo(c, target, g, act, s.holdRange, now);
        GunnerGrenade(c, target, g, now);
    }
    // Standing off, it keeps its face to the target.
    if (c->m_fusionStandingOff && sight && !c->HasInArc(target, M_PI_F / 2.f) && c->movespline->Finalized())
        c->SetFacingToObject(target);
    if (dist <= Fusion::kGunnerKnifeRange || c->CanReachWithMeleeAutoAttack(target))
    {
        if (c->GetSheath() != SHEATH_STATE_MELEE)
            c->SetSheath(SHEATH_STATE_MELEE);
        return;
    }
    if (c->GetSheath() != SHEATH_STATE_RANGED)
    {
        // (A respawn reloads the template's equipment: arm it again.)
        c->SetVirtualItem(RANGED_ATTACK, Fusion::NpcGunItemFor(c->GetLevel()));
        c->SetSheath(SHEATH_STATE_RANGED);
    }
    if (!sight)
        return;

    Fusion::Vec3 const muzzle = V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ() + 1.4f);
    Fusion::Vec3 const chest = V(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ() + 1.2f);
    float coverT = 0.f;
    uint32 const cover = PieceOnRay(c->GetMapId(), muzzle, (chest - muzzle).Normalized(), dist, coverT);
    uint32 burst = 3, gap = std::max<uint32>(60, uint32(w->fireTime * 1000.f)), pause = 1200;
    if (w->weaponClass == Fusion::WeaponClass::Shotgun || w->weaponClass == Fusion::WeaponClass::Pistol)
        burst = 1;
    else if (w->weaponClass == Fusion::WeaponClass::Sniper)
        burst = 1, pause = 2500;
    else if (w->weaponClass == Fusion::WeaponClass::Lmg)
        burst = 5;
    Fusion::NpcTrigger trigger{c->m_fusionGunBurstLeft, c->m_fusionGunNextMs, c->m_fusionGunStarted};
    bool const fires = trigger.Fire(c->m_fusionGunClockMs, burst, gap, pause);
    c->m_fusionGunBurstLeft = trigger.burstLeft;
    c->m_fusionGunNextMs = trigger.nextMs;
    c->m_fusionGunStarted = trigger.started;
    if (!fires)
        return;

    // Turning in place stops a run: only a gunner standing still turns to shoot.
    if (!c->HasInArc(target, M_PI_F / 2.f) && c->movespline->Finalized())
        c->SetFacingToObject(target);
    uint32 damage = 0;
    bool hit = false;
    // Its aim cone (Gunfights.h): wide when it first sees the target, settling as it holds its
    // aim; wider with range, the target's movement and partial cover (the legs hidden from the
    // muzzle); tighter for an elite or a rare.
    Fusion::Vec3 const hips = V(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ() + 0.5f);
    bool const partial = d.stance == Fusion::Stance::Stand
        && (!c->GetMap()->isInLineOfSight(muzzle.x, muzzle.y, muzzle.z, hips.x, hips.y, hips.z, true, true)
            || PieceBetween(c->GetMapId(), muzzle, hips));
    Fusion::NpcAimInput aim;
    aim.distance = dist;
    aim.heldMs = g.aim.HeldMs(now);
    aim.gait = d.gait;
    aim.sprinting = d.sprinting;
    aim.partialCover = partial;
    if (CreatureInfo const* ci = c->GetCreatureInfo(); ci && ci->rank <= 4)
        aim.rank = Fusion::CreatureRank(ci->rank);
    float const cone = Fusion::NpcAimCone(aim);
    float const body = Fusion::NpcTargetBody(d.stance, partial);
    float const chance = Fusion::NpcConeHitChance(cone, dist, body, int32(c->GetLevel()) - int32(target->GetLevel()));
    if (cover)
        DamagePiece(c->GetMap(), cover, w->damage * 0.5f, false, Fusion::Plots::Actor::Creature());    // the wall takes it
    else if (frand(0.f, 1.f) < chance)
    {
        hit = true;
        damage = Fusion::NpcHitDamage(Fusion::CodDamageAtRange(*w, dist), Fusion::Scale::CreatureGunMultiplier(FactsOf(c)) * CreatureDangerDamage(c, true),
            Fusion::Scale::PlayerArmorCut(float(target->GetArmor()), target->GetLevel()));
    }
    // Everyone near sees the round: into the chest, into the cover, or past the target as near
    // as its cone put it.
    Fusion::Vec3 const to = hit ? chest
        : cover ? muzzle + (chest - muzzle).Normalized() * coverT
        : Fusion::NpcMissPointAt(muzzle, chest, frand(0.f, 1.f), Fusion::NpcMissOffset(cone, dist, body, frand(0.f, 1.f)));
    std::vector<uint8_t> const round = Fusion::EncodeNpcFire(c->GetObjectGuid().GetRawValue(), w->id, hit, to);
    TellNear(c->GetMap(), muzzle, [&](Player*) { return round; }, Fusion::kNpcTellRadius);
    if (damage)
        Hit(c, target, damage);
    // The player hears the shot and sees its tracer; a miss carries no damage.
    Fusion::Packets::Events hurtEvents;
    uint8 const flags = target->IsAlive() ? 0 : Fusion::Packets::EVENT_FATAL;
    hurtEvents.events.push_back({Fusion::Packets::EventKind::Hurt, flags, c->GetObjectGuid().GetRawValue(), damage, w->id});
    SendEvents(target, hurtEvents);
}

bool FusionMgr::WandererRound(Creature* c, Unit* target, Fusion::WeaponProfile const& w, uint32 aimHeldMs)
{
    float const dist = c->GetDistance(target);
    Fusion::Vec3 const muzzle = V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ() + 1.4f);
    Fusion::Vec3 const chest = V(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ() + 1.2f);
    Player* const tp = target->ToPlayer();
    FusionPlayerData const* d = tp && tp->HasFusion() ? &tp->GetFusionConst() : nullptr;
    // The aim cone NPC gunners use (Gunfights.h): wide on a fresh target, settling as the aim is
    // held, wider with range and the target's movement; a wanderer is an ordinary soldier.
    Fusion::NpcAimInput aim;
    aim.distance = dist;
    aim.heldMs = aimHeldMs;
    aim.gait = d ? d->gait : target->IsMoving() ? Fusion::Gait::Run : Fusion::Gait::Still;
    aim.sprinting = d && d->sprinting;
    Fusion::Stance const stance = d ? d->stance : Fusion::Stance::Stand;
    float const cone = Fusion::NpcAimCone(aim);
    float const body = Fusion::NpcTargetBody(stance, false);
    float const chance = Fusion::NpcConeHitChance(cone, dist, body, int32(c->GetLevel()) - int32(target->GetLevel()));
    float coverT = 0.f;
    uint32 const cover = PieceOnRay(c->GetMapId(), muzzle, (chest - muzzle).Normalized(), dist, coverT);
    bool hit = false;
    uint32 damage = 0;
    if (cover)
        DamagePiece(c->GetMap(), cover, w.damage * 0.5f, false, Fusion::Plots::Actor::Creature());
    else if (frand(0.f, 1.f) < chance)
    {
        hit = true;
        if (tp)
            // A player takes it as from an NPC gunner of the wanderer's level.
            damage = Fusion::NpcHitDamage(Fusion::CodDamageAtRange(w, dist), Fusion::Scale::CreatureGunMultiplier(FactsOf(c)),
                Fusion::Scale::PlayerArmorCut(float(tp->GetArmor()), tp->GetLevel()));
        else
        {
            // A creature as from a player of the wanderer's level: the gun's CoD hit, the level's
            // multiplier (never gear's), on the creature's CoD pool.
            Fusion::HitInput in;
            in.weapon = &w;
            in.distance = dist;
            in.location = Fusion::HitLocation::TorsoUpper;
            in.ammo = Fusion::AmmoType::Basic;
            float cod = Fusion::CodHitDamage(in, TraitsOf(target), Fusion::CombatContext::PvE,
                Fusion::Stats::GunDamageMultiplier(c->GetLevel()), m_coefficients);
            if (Creature* tc = target->ToCreature(); tc && !tc->m_fusionWowMaxHealth)
                cod *= Fusion::Scale::PoolRatio(float(tc->GetMaxHealth()), Fusion::Scale::PlayerMaxHealth(tc->GetLevel(), 0.f));
            damage = std::max<uint32>(1, uint32(cod + 0.5f));
        }
    }
    Fusion::Vec3 const to = hit ? chest
        : cover ? muzzle + (chest - muzzle).Normalized() * coverT
        : Fusion::NpcMissPointAt(muzzle, chest, frand(0.f, 1.f), Fusion::NpcMissOffset(cone, dist, body, frand(0.f, 1.f)));
    std::vector<uint8_t> const round = Fusion::EncodeNpcFire(c->GetObjectGuid().GetRawValue(), w.id, hit, to);
    TellNear(c->GetMap(), muzzle, [&](Player*) { return round; }, Fusion::kNpcTellRadius);
    if (!hit)
    {
        ShotNear(target, WorldTimer::getMSTime());
        return false;
    }
    FusionHitSource source;
    source.weaponId = w.id;
    Hit(c, target, damage, source);
    if (tp && tp->HasFusion())
    {
        Fusion::Packets::Events hurt;
        uint8 const flags = tp->IsAlive() ? 0 : Fusion::Packets::EVENT_FATAL;
        hurt.events.push_back({Fusion::Packets::EventKind::Hurt, flags, c->GetObjectGuid().GetRawValue(), damage, w.id});
        SendEvents(tp, hurt);
    }
    return true;
}

void FusionMgr::ShotNear(Unit* unit, uint32 now)
{
    if (Creature* c = unit ? unit->ToCreature() : nullptr)
        if (c->m_fusionGunner)
            c->m_fusionGunner->shotAtMs = now;
}

bool FusionMgr::PieceBetween(uint32 mapId, Fusion::Vec3 a, Fusion::Vec3 b, uint32 ignoreOwner) const
{
    float const len = (b - a).Length();
    float t = 0.f;
    return len > 0.1f && PieceOnRay(mapId, a, (b - a) * (1.f / len), len, t, ignoreOwner) != 0;
}

std::optional<float> FusionMgr::WorldOnSegment(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b) const
{
    float const len = (b - a).Length();
    if (len < 1e-4f)
        return std::nullopt;
    Fusion::Vec3 const dir = (b - a) * (1.f / len);
    float best = len;
    float pieceT = 0.f;
    if (PieceOnRay(map->GetId(), a, dir, best, pieceT))
        best = std::min(best, pieceT);
    float wx, wy, wz;
    if (VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(map->GetId(), a.x, a.y, a.z, b.x, b.y, b.z, wx, wy, wz, 0.f))
        best = std::min(best, (V(wx, wy, wz) - a).Length());
    // The terrain (the vmaps hold buildings, not the ground): where the segment ends under it,
    // the crossing is found from the heights at both ends.
    float const gb = map->GetHeight(b.x, b.y, b.z + 2.f);
    if (gb > INVALID_HEIGHT && b.z < gb)
    {
        float const ga = map->GetHeight(a.x, a.y, a.z + 2.f);
        float const above = ga > INVALID_HEIGHT ? a.z - ga : 0.f;
        float const f = above > 0.f ? above / (above + (gb - b.z)) : 0.f;
        best = std::min(best, f * len);
    }
    if (best >= len)
        return std::nullopt;
    return best;
}

Fusion::Vec3 FusionMgr::EyeOf(Player const* player) const
{
    Fusion::Stance const stance = player->HasFusion() ? player->GetFusionConst().stance : Fusion::Stance::Stand;
    return V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() + EyeHeight(stance) + RiderEyeRise(player));
}

float FusionMgr::BoltMeetsWorld(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b, uint32& piece) const
{
    piece = 0;
    float const len = (b - a).Length();
    if (!map || len < 1e-4f)
        return -1.f;
    Fusion::Vec3 const dir = (b - a) * (1.f / len);
    float best = len;
    float pieceT = 0.f;
    if (uint32 const id = PieceOnRay(map->GetId(), a, dir, best, pieceT); id && pieceT < best)
    {
        best = pieceT;
        piece = id;
    }
    float wx, wy, wz;
    if (VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(map->GetId(), a.x, a.y, a.z, b.x, b.y, b.z, wx, wy, wz, 0.f))
    {
        float const d = (V(wx, wy, wz) - a).Length();
        if (d < best)
        {
            best = d;
            piece = 0;
        }
    }
    // A closed door, a gate, a game object with a collision model: the body bumps it, so does a bolt.
    Movement::Vector3 hit;
    if (map->GetDynamicObjectHitPos(Movement::Vector3(a.x, a.y, a.z), Movement::Vector3(b.x, b.y, b.z), hit, 0.f))
    {
        float const d = (V(hit.x, hit.y, hit.z) - a).Length();
        if (d < best)
        {
            best = d;
            piece = 0;
        }
    }
    // The terrain (the vmaps hold buildings, not the ground): sampled every two yards, the
    // crossing found between the last sample above it and the first below.
    float prevT = 0.f;
    float prevAbove = 0.f;
    {
        float const g = map->GetHeight(a.x, a.y, a.z + 2.f);
        prevAbove = g > INVALID_HEIGHT ? a.z - g : 1.f;
    }
    for (float t = std::min(2.f, len);; t = std::min(t + 2.f, len))
    {
        Fusion::Vec3 const p = a + dir * t;
        float const g = map->GetHeight(p.x, p.y, p.z + 2.f);
        float const above = g > INVALID_HEIGHT ? p.z - g : 1.f;
        if (above < 0.f)
        {
            float const f = prevAbove > 0.f ? prevAbove / (prevAbove - above) : 0.f;
            float const d = prevT + (t - prevT) * f;
            if (d < best)
            {
                best = d;
                piece = 0;
            }
            break;
        }
        prevT = t;
        prevAbove = above;
        if (t >= len || t >= best)
            break;
    }
    return best < len ? best : -1.f;
}

bool FusionMgr::BoltSees(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b) const
{
    uint32 piece = 0;
    return map && BoltMeetsWorld(map, a, b, piece) < 0.f;
}

bool FusionMgr::ShotPassesThrough(Player* shooter, Unit* u) const
{
    Player* controller = u->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!controller)
        return false;
    bool const grouped = controller != shooter && shooter->IsInSameRaidWith(controller);
    return Fusion::ShotPassesThrough(shooter->GetObjectGuid().GetRawValue(),
        controller->GetObjectGuid().GetRawValue(), grouped);
}

void FusionMgr::BoltBodies(Player* caster, Fusion::Vec3 around, float reach, std::vector<Fusion::Spellcasting::Body>& bodies,
    std::vector<Unit*>& units) const
{
    bodies.clear();
    units.clear();
    float const search = reach + (V(caster->GetPositionX(), caster->GetPositionY(), caster->GetPositionZ()) - around).Length() + 5.f;
    std::list<Unit*> found;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(caster, caster, search);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(found, check);
    Cell::VisitAllObjects(caster, searcher, search);
    for (Unit* u : found)
    {
        if (u == caster || !u->IsAlive() || ShotPassesThrough(caster, u) || !caster->IsValidAttackTarget(u) || !PvpAllows(caster, u))
            continue;
        Fusion::Vec3 const feet = V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ());
        if ((feet - around).Length() > reach + 10.f)
            continue;
        Fusion::Spellcasting::Body b;
        b.guid = u->GetObjectGuid().GetRawValue();
        b.pose.feet = feet;
        b.pose.orientation = u->GetOrientation();
        if (Player const* tp = u->ToPlayer(); tp && tp->HasFusion())
            b.pose.stance = tp->GetFusionConst().stance;
        else if (Creature const* c = u->ToCreature())
        {
            // A creature's own bounds, as a round finds it (HandleShots).
            float const lift = Fusion::BodyLift(c->GetCreatureInfo()->pet_family, c->CanFly(), c->IsHovering() || c->IsLevitating());
            b.shape.height = std::max(1.0f, u->GetCollisionHeight()) + lift;
            b.shape.radius = std::max(0.45f, u->GetObjectBoundingRadius());
            b.shape.headRadius = std::max(0.16f, b.shape.radius * 0.35f);
        }
        bodies.push_back(b);
        units.push_back(u);
    }
}

void FusionMgr::SpellStrikesPieces(Player* owner, SpellEntry const* info, Fusion::Vec3 at, uint32 piece, float radius)
{
    using namespace Fusion::Build;
    Map* map = owner ? owner->GetMap() : nullptr;
    if (!m_enabled || !info || !map || map->Instanceable())
        return;
    // Its direct damage, as SpellHitsPieces counts it.
    float damage = 0.f;
    for (uint8 i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (info->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
            damage += SpellPieceDamage(owner->CalculateSpellEffectValue(nullptr, info, SpellEffectIndex(i)), owner->GetLevel(), false);
    if (damage <= 0.f)
        return;
    Fusion::Plots::Actor const by = PlotActor(owner);
    if (piece)
        DamagePiece(map, piece, damage, DamageKind::Spell, by);
    if (radius <= 0.f)
        return;
    for (Piece const& p : PiecesNear(map->GetId(), at, radius + 6.f))
        if (p.id != piece && Distance(p, at) <= radius)
            DamagePiece(map, p.id, damage, DamageKind::Spell, by);
}

void FusionMgr::TellNearOn(Map* map, uint8 channel, Fusion::Vec3 at, std::vector<uint8_t> const& body, Player const* skip,
    float radius)
{
    if (!map || body.empty())
        return;
    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* p = it->getSource();
        if (!p || p == skip || !p->IsInWorld() || !p->HasFusion() || !p->GetSession())
            continue;
        if ((V(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ()) - at).Length() > radius)
            continue;
        SendChannel(p, channel, body);
    }
}

Fusion::Bounce::Path FusionMgr::FlyFrag(Map const* map, Fusion::Vec3 from, Fusion::Vec3 velocity, float gravity, float fuseS) const
{
    Fusion::Bounce::Probe const probe = [this, map](Fusion::Vec3 a, Fusion::Vec3 b) { return WorldOnSegment(map, a, b); };
    Fusion::Bounce::Height const height = [map](float x, float y, float z) -> std::optional<float> {
        float const h = map->GetHeight(x, y, z + 2.f);
        if (h <= INVALID_HEIGHT)
            return std::nullopt;
        return h;
    };
    return Fusion::Bounce::Fly(from, velocity, gravity, fuseS,
        [&](Fusion::Vec3 a, Fusion::Vec3 b) -> std::optional<Fusion::Bounce::Contact> {
            std::optional<float> const t = probe(a, b);
            if (!t)
                return std::nullopt;
            Fusion::Bounce::Contact c;
            c.fraction = *t / std::max(1e-4f, (b - a).Length());
            c.normal = Fusion::Bounce::SurfaceNormal(a, b, a + (b - a) * c.fraction, height, probe);
            return c;
        });
}

std::vector<Fusion::GunnerSpot> FusionMgr::TestSpots(Creature* c, Player* target, std::vector<Fusion::Vec3> const& spots) const
{
    // Each spot: the ground there, a clear run to it from the gunner (no wall, no built piece,
    // no drop or climb past a few yards), and whether the target's eye sees a chest there.
    Map const* map = c->GetMap();
    uint32 const mapId = map->GetId();
    Fusion::Vec3 const self = V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ());
    Fusion::Vec3 const eye = V(target->GetPositionX(), target->GetPositionY(),
        target->GetPositionZ() + EyeHeight(target->GetFusionConst().stance));
    Fusion::Vec3 const knee{0.f, 0.f, 0.8f};
    Fusion::Vec3 const chest{0.f, 0.f, 1.2f};
    std::vector<Fusion::GunnerSpot> out;
    for (Fusion::Vec3 const& p : spots)
    {
        Fusion::GunnerSpot s;
        float const ground = map->GetHeight(p.x, p.y, self.z + 4.f);
        if (ground <= INVALID_HEIGHT || std::fabs(ground - self.z) > 4.f)
        {
            out.push_back(s);
            continue;
        }
        s.pos = V(p.x, p.y, ground);
        s.reachable = map->isInLineOfSight(self.x, self.y, self.z + knee.z, s.pos.x, s.pos.y, s.pos.z + knee.z, true, true)
            && !PieceBetween(mapId, self + knee, s.pos + knee);
        if (s.reachable)
        {
            Fusion::Vec3 const at = s.pos + chest;
            s.inSight = map->isInLineOfSight(eye.x, eye.y, eye.z, at.x, at.y, at.z, true, true) && !PieceBetween(mapId, eye, at);
        }
        out.push_back(s);
    }
    return out;
}

std::vector<Creature*> FusionMgr::SquadOf(Creature* c) const
{
    std::list<Creature*> creatures;
    MaNGOS::AnyUnitInObjectRangeCheck check(c, Fusion::kSquadRadius);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
    Cell::VisitGridObjects(c, searcher, Fusion::kSquadRadius);
    std::vector<Creature*> squad;
    for (Creature* o : creatures)
        if (o != c && o->IsAlive() && o->IsInCombat() && o->m_fusionGunRole == uint8(Fusion::GunRole::Gunner))
            squad.push_back(o);
    return squad;
}

void FusionMgr::GunnerMoveTo(Creature* c, Fusion::Vec3 spot)
{
    // Its own run to a spot: the chase stands aside (combat movement off) until it closes in again.
    if (!c->m_fusionStandingOff)
    {
        c->m_fusionStandingOff = true;
        c->AI()->SetCombatMovement(false);
    }
    c->GetMotionMaster()->MovePoint(kGunnerPointId, spot.x, spot.y, spot.z, MOVE_PATHFINDING | MOVE_RUN_MODE);
}

void FusionMgr::GunnerDo(Creature* c, Player* target, FusionGunnerState& g, Fusion::GunnerAct act, float holdRange, uint32 now)
{
    using Fusion::GunnerAct;
    Fusion::Vec3 const self = V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ());
    Fusion::Vec3 const feet = V(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
    Fusion::GunnerMind& m = g.mind;
    switch (act)
    {
        case GunnerAct::Chase:
        {
            // Back to its WoW chase (out of sight, out of range, or at arm's length).
            Fusion::GunnerMoved(m, act, true, {}, 0.f, now, 0);
            if (c->m_fusionStandingOff)
            {
                c->m_fusionStandingOff = false;
                MotionMaster* mm = c->GetMotionMaster();
                if (mm->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
                    mm->MovementExpired(false);
                c->AI()->SetCombatMovement(true);
            }
            break;
        }
        case GunnerAct::Strafe:
        {
            // Its preferred bearing about the target, set as it comes into the fight: squadmates
            // spread round it, a third of them flanking.
            if (!m.hasAnchor)
            {
                m.hasAnchor = true;
                m.anchor = std::atan2(self.y - feet.y, self.x - feet.x) + Fusion::GunnerBearingOffset(c->GetObjectGuid().GetRawValue());
            }
            std::vector<Fusion::GunnerSpot> const spots = TestSpots(c, target, Fusion::FiringSpots(self, feet, holdRange, m.anchor));
            std::vector<Fusion::Vec3> squad;
            for (Creature* o : SquadOf(c))
                squad.push_back(V(o->GetPositionX(), o->GetPositionY(), o->GetPositionZ()));
            int const i = Fusion::ChooseFiringSpot(spots, self, feet, m.anchor, squad, urand(0, 0xFFFF));
            if (i >= 0)
                GunnerMoveTo(c, spots[size_t(i)].pos);
            else if (!c->m_fusionStandingOff)
            {
                // Nowhere better: it holds where it is, in sight and range.
                c->m_fusionStandingOff = true;
                c->AI()->SetCombatMovement(false);
                c->StopMoving();
            }
            Fusion::GunnerMoved(m, act, i >= 0, i >= 0 ? spots[size_t(i)].pos : self,
                i >= 0 ? (spots[size_t(i)].pos - self).Length() : 0.f, now, urand(0, 0xFFFF));
            break;
        }
        case GunnerAct::TakeCover:
        {
            std::vector<Fusion::GunnerSpot> const spots = TestSpots(c, target, Fusion::CoverSpots(self, urand(0, 359)));
            int const i = Fusion::ChooseCoverSpot(spots, self, feet);
            if (i < 0)
            {
                // No cover near: it strafes instead.
                Fusion::GunnerMoved(m, act, false, {}, 0.f, now, 0);
                GunnerDo(c, target, g, GunnerAct::Strafe, holdRange, now);
                break;
            }
            GunnerMoveTo(c, spots[size_t(i)].pos);
            Fusion::GunnerMoved(m, act, true, spots[size_t(i)].pos, (spots[size_t(i)].pos - self).Length(), now, urand(0, 0xFFFF));
            break;
        }
        case GunnerAct::Peek:
        {
            std::vector<Fusion::GunnerSpot> const spots = TestSpots(c, target, Fusion::PeekSpots(m.cover));
            int const i = Fusion::ChoosePeekSpot(spots, m.cover);
            if (i < 0)
            {
                Fusion::GunnerMoved(m, act, false, {}, 0.f, now, 0);
                GunnerDo(c, target, g, GunnerAct::Strafe, holdRange, now);
                break;
            }
            GunnerMoveTo(c, spots[size_t(i)].pos);
            Fusion::GunnerMoved(m, act, true, spots[size_t(i)].pos, (spots[size_t(i)].pos - self).Length(), now, 0);
            break;
        }
        case GunnerAct::ReturnToCover:
            GunnerMoveTo(c, m.cover);
            Fusion::GunnerMoved(m, act, true, m.cover, (m.cover - self).Length(), now, urand(0, 0xFFFF));
            break;
        default:
            break;
    }
}

void FusionMgr::GunnerGrenade(Creature* c, Player* target, FusionGunnerState& g, uint32 now)
{
    // At most a look a second; the cheap tests first, its squad's grenades last.
    if (int32(now - g.grenadeThinkMs) < 0)
        return;
    g.grenadeThinkMs = now + Fusion::kGunnerThinkMs;
    Fusion::Vec3 const self = V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ());
    Fusion::Vec3 const aim = g.sight.lastSeen;
    float const flat = std::sqrt((aim.x - self.x) * (aim.x - self.x) + (aim.y - self.y) * (aim.y - self.y));
    bool const cooled = !g.hasThrown || now - g.thrownMs >= Fusion::kNpcGrenadeCooldownMs;
    if (!Fusion::NpcShouldThrowGrenade(g.sight.HiddenMs(now), flat, cooled, false, urand(0, 99)))
        return;
    for (Creature* o : SquadOf(c))
        if (o->m_fusionGunner && o->m_fusionGunner->hasThrown && int32(now - o->m_fusionGunner->grenadeLiveUntilMs) < 0)
            return;                     // one of its group's is in the air
    Fusion::WeaponProfile const* frag = m_catalog.Weapons().Find(std::string("frag_grenade_mp"));
    if (!frag)
        return;

    // Lobbed to land where it last saw the target, then bouncing and rolling as a player's frag
    // does (Bounce.h, the client draws it from op 35 alone), and set off on the frag's fuse
    // wherever it is then.
    Map* map = c->GetMap();
    Fusion::Vec3 const from = self + Fusion::Vec3{0.f, 0.f, 1.4f};
    Fusion::Vec3 const to = aim + Fusion::Vec3{0.f, 0.f, 0.1f};
    float const seconds = Fusion::NpcLobSeconds(flat);
    Fusion::Vec3 const v = Fusion::Launch::LobVelocity(from, to, Fusion::Launch::kGrenadeGravity, seconds);
    Fusion::Bounce::Path const path = FlyFrag(map, from, v, Fusion::Launch::kGrenadeGravity,
        Fusion::FuseSeconds(Fusion::EquipmentKind::Frag));
    FusionCharge charge;
    charge.kind = FusionCharge::Kind::NpcFrag;
    charge.weaponId = frag->id;
    charge.pos = path.end + Fusion::Vec3{0.f, 0.f, 0.1f};
    charge.radius = frag->explosionRadius;
    charge.inner = frag->explosionInner;
    charge.outer = frag->explosionOuter;
    charge.mapId = map->GetId();
    charge.atMs = now + path.fuseMs;
    charge.thrower = c->GetObjectGuid().GetRawValue();
    target->GetFusion().charges.push_back(charge);
    g.hasThrown = true;
    g.thrownMs = now;
    g.grenadeLiveUntilMs = charge.atMs;
    if (c->movespline->Finalized())
        c->SetFacingTo(std::atan2(aim.y - self.y, aim.x - self.x));
    std::vector<uint8_t> const seen = Fusion::Bounce::EncodePath(charge.thrower, frag->id, Fusion::Launch::kGrenadeGravity, path);
    TellNear(map, from, [&](Player*) { return seen; });
}

void FusionMgr::NpcExplode(Player* holder, FusionCharge const& charge)
{
    Map* map = holder->GetMap();
    if (map->GetId() != charge.mapId)
        return;
    Fusion::WeaponProfile blast;
    blast.id = charge.weaponId;
    blast.explosionRadius = charge.radius;
    blast.explosionInner = charge.inner;
    blast.explosionOuter = charge.outer;
    BlastPieces(map, charge.pos, blast, Fusion::Plots::Actor::Creature());
    std::vector<uint8_t> const seen = Fusion::Launch::EncodeExplosion(charge.pos, charge.radius, charge.weaponId);
    TellNear(map, charge.pos, [&](Player*) { return seen; });
    // Its thrower gone (dead, despawned), it hurts no one: the damage needs a dealer.
    Creature* thrower = map->GetCreature(ObjectGuid(charge.thrower));
    if (!thrower || !thrower->IsAlive())
        return;
    // Players and their pets in sight of the blast take its splash on the creature's gun scale.
    Fusion::Vec3 const self = V(thrower->GetPositionX(), thrower->GetPositionY(), thrower->GetPositionZ());
    float const reach = charge.radius + (self - charge.pos).Length();
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(thrower, thrower, reach);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(thrower, searcher, reach);
    float const scale = Fusion::Scale::CreatureGunMultiplier(FactsOf(thrower)) * CreatureDangerDamage(thrower, true);
    for (Unit* u : units)
    {
        if (!u->IsAlive() || !thrower->IsValidAttackTarget(u) || !u->GetCharmerOrOwnerPlayerOrPlayerItself())
            continue;
        Fusion::Vec3 const centre = V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + 1.f);
        float const splash = Fusion::SplashDamage(blast, (centre - charge.pos).Length());
        if (splash <= 0.f || !map->isInLineOfSight(charge.pos.x, charge.pos.y, charge.pos.z, centre.x, centre.y, centre.z, true, true))
            continue;
        Player* p = u->ToPlayer();
        float const cut = p ? Fusion::Scale::PlayerArmorCut(float(p->GetArmor()), p->GetLevel()) : 0.f;
        uint32 const damage = Fusion::NpcHitDamage(splash, scale, cut);
        Hit(thrower, u, damage);
        if (p)
        {
            Fusion::Packets::Events hurt;
            uint8 const flags = p->IsAlive() ? 0 : Fusion::Packets::EVENT_FATAL;
            hurt.events.push_back({Fusion::Packets::EventKind::Hurt, flags, charge.thrower, damage, charge.weaponId});
            SendEvents(p, hurt);
        }
    }
}

void FusionMgr::SendNpcGuns(Player* player)
{
    if (!player->IsInWorld() || !player->GetSession())
        return;
    std::list<Creature*> creatures;
    MaNGOS::AnyUnitInObjectRangeCheck check(player, Fusion::kNpcTellRadius);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
    Cell::VisitGridObjects(player, searcher, Fusion::kNpcTellRadius);
    // The nearest first, should there be more than the message holds.
    std::vector<std::pair<float, Fusion::NpcGunEntry>> seen;
    for (Creature* c : creatures)
        if (c->IsInWorld() && c->IsAlive() && c->m_fusionGunRole == uint8(Fusion::GunRole::Gunner) && c->m_fusionGunId)
            seen.push_back({player->GetDistance(c), {c->GetObjectGuid().GetRawValue(), c->m_fusionGunId}});
    std::sort(seen.begin(), seen.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<Fusion::NpcGunEntry> gunners;
    for (auto const& n : seen)
        gunners.push_back(n.second);
    SendChannel(player, Fusion::Packets::kChannelWorld, Fusion::EncodeNpcGuns(gunners));
}

// ── Building ────────────────────────────────────────────────────────────────────────────────

namespace
{
    Fusion::Build::Ground GroundOf(Map const* map)
    {
        return [map](float x, float y, float z) { return map->GetHeight(x, y, z, true, 30.f); };
    }

    // Maps where building is allowed: the open world, not dungeons, raids or battlegrounds.
    bool CanBuildOn(Map const* map)
    {
        return map && !map->Instanceable();
    }

    // Whether a piece standing as `p` would shut a player inside it: the builder or anyone near
    // (Build.h Traps).
    bool TrapsAPlayer(Map* map, Fusion::Build::Piece const& p)
    {
        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* q = it->getSource();
            if (!q || !q->IsInWorld() || q->GetTransport())
                continue;
            Fusion::Vec3 const feet = V(q->GetPositionX(), q->GetPositionY(), q->GetPositionZ());
            if ((feet - p.pos).Length() <= 60.f && Fusion::Build::Traps(p, feet))
                return true;
        }
        return false;
    }
}

void FusionMgr::LoadPieces()
{
    // Bling Pro's second attachment on the secondary (loadout version 2): a column the class
    // table gained after it was made, added here as no migration file may be.
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `character_fusion_class` ADD COLUMN IF NOT EXISTS "
        "`secondary_att2` varchar(16) NOT NULL DEFAULT '' AFTER `secondary_att`");
    // Kits (loadout version 3): each kit's six talent picks, one option id per tier (0 none;
    // package W6 fills them), and the field editor's cooldown (unix seconds; the column keeps the
    // old ledger's name).
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `character_fusion_class` "
        "ADD COLUMN IF NOT EXISTS `talent1` smallint(5) unsigned NOT NULL DEFAULT 0, "
        "ADD COLUMN IF NOT EXISTS `talent2` smallint(5) unsigned NOT NULL DEFAULT 0, "
        "ADD COLUMN IF NOT EXISTS `talent3` smallint(5) unsigned NOT NULL DEFAULT 0, "
        "ADD COLUMN IF NOT EXISTS `talent4` smallint(5) unsigned NOT NULL DEFAULT 0, "
        "ADD COLUMN IF NOT EXISTS `talent5` smallint(5) unsigned NOT NULL DEFAULT 0, "
        "ADD COLUMN IF NOT EXISTS `talent6` smallint(5) unsigned NOT NULL DEFAULT 0");
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `character_fusion` ADD COLUMN IF NOT EXISTS "
        "`ledger_ready_at` bigint(20) unsigned NOT NULL DEFAULT 0");
    // Kits (loadout version 4): the primary's third attachment (two Tinker's Rails) and which
    // perk slots play their Pro (bits 0-2). Existing kits keep '' and 0: nothing changes for them.
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `character_fusion_class` "
        "ADD COLUMN IF NOT EXISTS `primary_att3` varchar(16) NOT NULL DEFAULT '', "
        "ADD COLUMN IF NOT EXISTS `perk_pro` tinyint(3) unsigned NOT NULL DEFAULT 0");
    // Kits (loadout version 5): the primary's fourth attachment (Tinker's Rails three times).
    // Existing kits keep '': nothing changes for them.
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `character_fusion_class` "
        "ADD COLUMN IF NOT EXISTS `primary_att4` varchar(16) NOT NULL DEFAULT ''");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `fusion_build_piece` ("
        "`id` int(10) unsigned NOT NULL,"
        "`map` smallint(5) unsigned NOT NULL,"
        "`type` tinyint(3) unsigned NOT NULL,"
        "`x` float NOT NULL, `y` float NOT NULL, `z` float NOT NULL, `yaw` float NOT NULL,"
        "`health` float NOT NULL,"
        "`owner` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "PRIMARY KEY (`id`), KEY `map` (`map`), KEY `owner` (`owner`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8");
    // A piece's reinforcement tier (fusion/Build.h), added after the table was made.
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `fusion_build_piece` ADD COLUMN IF NOT EXISTS "
        "`reinforcement` tinyint(3) unsigned NOT NULL DEFAULT '0' AFTER `owner`");
    // A door's or portal's lock (fusion/Locks.h), added after the table was made: every door
    // from before takes the default, Owner and party (2). A code lock keeps a salted, stretched
    // SHA-256 of its code, never the code.
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `fusion_build_piece` "
        "ADD COLUMN IF NOT EXISTS `lock_mode` tinyint(3) unsigned NOT NULL DEFAULT '2' COMMENT 'Locks::Mode' AFTER `reinforcement`, "
        "ADD COLUMN IF NOT EXISTS `lock_salt` varchar(32) NOT NULL DEFAULT '' AFTER `lock_mode`, "
        "ADD COLUMN IF NOT EXISTS `lock_hash` varchar(64) NOT NULL DEFAULT '' AFTER `lock_salt`");
    // Decay (fusion/Maintenance.h): the time a piece's health was last brought up to date. Added
    // after the table was made; every piece from before starts its clock at this start (0 means
    // "from before"), so none loses anything for the time before decay existed. Nothing else is
    // touched: safe on live data and on every restart.
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `fusion_build_piece` ADD COLUMN IF NOT EXISTS "
        "`decayed_at` bigint(20) NOT NULL DEFAULT '0' COMMENT 'unix time health is true at' AFTER `lock_hash`");
    // What was paid for a piece (Build::Piece::paid: bit 0 the piece, bit t reinforcement tier t),
    // added after the table was made: every piece from before counts as paid in full (15), as it
    // paid out before. A piece built or reinforced free from now on pays that part nothing back.
    CharacterDatabase.DirectExecute(
        "ALTER TABLE `fusion_build_piece` ADD COLUMN IF NOT EXISTS "
        "`paid` tinyint(3) unsigned NOT NULL DEFAULT '15' COMMENT 'Build::kPaid bits' AFTER `decayed_at`");
    int64 const loadedAt = int64(time(nullptr));
    CharacterDatabase.DirectPExecute("UPDATE `fusion_build_piece` SET `decayed_at` = %" PRId64 " WHERE `decayed_at` = 0", int64_t(loadedAt));
    std::lock_guard<std::mutex> lock(m_buildLock);
    m_pieces.clear();
    m_streakSentries.clear();
    m_lockSecrets.clear();
    uint32 count = 0, sentries = 0;
    if (std::unique_ptr<QueryResult> result = CharacterDatabase.Query("SELECT `id`, `map`, `type`, `x`, `y`, `z`, `yaw`, `health`, `owner`, `reinforcement`, "
            "`lock_mode`, `lock_salt`, `lock_hash`, `decayed_at`, `paid` FROM `fusion_build_piece`"))
    {
        do
        {
            Field* f = result->Fetch();
            Fusion::Build::Piece p;
            p.id = f[0].GetUInt32();
            p.map = f[1].GetUInt32();
            if (!Fusion::Build::Known(f[2].GetUInt8()))
                continue;
            p.type = Fusion::Build::PieceType(f[2].GetUInt8());
            p.pos = V(f[3].GetFloat(), f[4].GetFloat(), f[5].GetFloat());
            p.yaw = f[6].GetFloat();
            p.health = f[7].GetFloat();
            p.owner = f[8].GetUInt32();
            p.reinforcement = std::min<uint8>(f[9].GetUInt8(), Fusion::Build::kMaxReinforcement);
            p.lock = f[10].GetUInt8();
            p.decayedAt = f[13].GetInt64();
            p.paid = uint8(f[14].GetUInt8() & Fusion::Build::kPaidAll);
            if (p.decayedAt <= 0)
                p.decayedAt = loadedAt;
            // A code lock without its secret (or a mode past the last) falls back to the default.
            if (!Fusion::Locks::ValidMode(p.lock))
                p.lock = Fusion::Build::kDefaultLock;
            if (p.lock == uint8(Fusion::Locks::Mode::Password))
            {
                Fusion::Locks::Secret const secret{f[11].GetCppString(), f[12].GetCppString()};
                if (secret.Set())
                    m_lockSecrets[p.id] = secret;
                else
                    p.lock = Fusion::Build::kDefaultLock;
            }
            m_nextPieceId = std::max(m_nextPieceId, p.id + 1);
            // A sentry gun built before it became a killstreak: taken down, its three parts to
            // its builder's stash at the Plot Steward.
            if (p.type == Fusion::Build::PieceType::SentryGun)
            {
                for (uint32 part : {Fusion::Construction::kSentryBarrel, Fusion::Construction::kSentryTargeting, Fusion::Construction::kSentryTripod})
                    StashReclaim(p.owner, part, 1);
                CharacterDatabase.PExecute("DELETE FROM `fusion_build_piece` WHERE `id` = %u", p.id);
                ++sentries;
                continue;
            }
            m_pieces[p.map].push_back(p);
            ++count;
        } while (result->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u built pieces%s.", count, m_buildFree ? " (building is free)" : "");
    if (sentries)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u built sentry guns taken down (a killstreak now); their parts are in their builders' stashes.", sentries);
}

void FusionMgr::SendChannel(Player* player, uint8 channel, std::vector<uint8_t> const& body) const
{
    Fusion::Packets::ChannelMessage m;
    m.channel = channel;
    m.body = body;
    std::vector<uint8_t> const bytes = Fusion::Packets::Encode(m);
    WorldPacket data(SMSG_FUSION_CHANNEL, bytes.size());
    data.append(bytes.data(), bytes.size());
    player->GetSession()->SendPacket(&data);
}

void FusionMgr::HandleChannel(Player* player, uint8 channel, std::vector<uint8> const& body)
{
    // The costly asks (a window rebuilt, a query, a write, a broadcast) at a person's pace.
    if (Fusion::Throttle::Costly(channel, body) && player->HasFusion()
        && !player->GetFusion().costlyAsks.Allow(WorldTimer::getMSTime()))
        return;
    if (channel == Fusion::Packets::kChannelWorld && body.size() == 1 && body[0] == 9 && IsAdmin(player))
    {
        FusionPlayerData& d = player->GetFusion();
        d.raid.phase = Fusion::Defense::RaidPhase::Quiet;
        d.raid.armed = true;
        d.raid.nextMs = WorldTimer::getMSTime();
        ChatHandler(player).SendSysMessage("Fusion: a raid is called on your base.");
        return;
    }
    // An admin calls the raid off: the raiders leave.
    if (channel == Fusion::Packets::kChannelWorld && body.size() == 1 && body[0] == 10 && IsAdmin(player))
    {
        FusionPlayerData& d = player->GetFusion();
        for (ObjectGuid const& g : d.raiders)
            if (Creature* c = player->GetMap()->GetCreature(g))
                c->DespawnOrUnsummon();
        d.raiders.clear();
        d.raid.phase = Fusion::Defense::RaidPhase::Fighting;
        d.raid.nextMs = WorldTimer::getMSTime();
        ChatHandler(player).SendSysMessage("Fusion: the raid is called off.");
        return;
    }
    // The Construction window (FusionConstruction.cpp): world ops 52 and 53.
    if (channel == Fusion::Packets::kChannelWorld && !body.empty()
        && (body[0] == Fusion::Homestead::kOpAsk || body[0] == Fusion::Homestead::kOpAct))
    {
        if (player->IsInWorld())
            HandleHomestead(player, body);
        return;
    }
    if (channel == Fusion::Packets::kChannelWorld && !body.empty() && body[0] == Fusion::Construction::kOpReinforce)
    {
        uint32 id = 0;
        if (player->IsInWorld() && Fusion::Construction::DecodeReinforce(body, id))
            ReinforcePiece(player, id);
        return;
    }
    // The professions' trade windows (FusionConstruction.cpp): Construction's kits made;
    // Inscription's and Jewelcrafting's ops 20-32 and the kits' glyphs and gems (FusionCrafts.cpp).
    if (channel == Fusion::Construction::kChannelProfessions)
    {
        if (sCraftsMgr.HandleChannel(player, body))
            return;
        Fusion::Build::PieceType type = Fusion::Build::PieceType::None;
        uint16 count = 0;
        if (player->IsInWorld() && player->HasFusion() && m_enabled && Fusion::Construction::DecodeCraftKit(body, type, count))
            CraftKits(player, type, count);
        return;
    }
    if (channel == Fusion::Packets::kChannelAchievements)
    {
        HandleAchievementChannel(player, body);
        return;
    }
    // The Goblin Rocket's reports (FusionSkyStreaks.cpp): world op 38.
    if (channel == Fusion::Packets::kChannelWorld && !body.empty() && body[0] == Fusion::Sky::kOpRocketReport)
    {
        if (player->IsInWorld())
            HandleRocketReport(player, body);
        return;
    }
    // Bug reports and suggestions (FusionFeedback.cpp): a channel of their own.
    if (channel == Fusion::Feedback::kChannelFeedback)
    {
        sFeedbackMgr.HandleChannel(player, body);
        return;
    }
    // Skyriding's Ascent and Surge (FusionRiding.cpp, fusion/Skyriding.h): world op 72.
    if (channel == Fusion::Packets::kChannelWorld && !body.empty() && body[0] == Fusion::Skyriding::kOpSkyMove)
    {
        Fusion::Skyriding::Move m;
        if (player->IsInWorld() && Fusion::Skyriding::DecodeSkyMove(body, m))
            SkyMove(player, m);
        return;
    }
    // Weapon pickups (FusionArmory.cpp): world ops 60-69.
    if (channel == Fusion::Packets::kChannelWorld && !body.empty() && body[0] >= 60 && body[0] < 70)
    {
        HandleArmoryChannel(player, body);
        return;
    }
    // Companion battles (FusionCompanionBattles.cpp): a channel of their own.
    if (channel == Fusion::CompanionBattles::kChannel)
    {
        HandleCompanionBattleChannel(player, body);
        return;
    }
    if (channel == Fusion::Companions::kChannelCompanion)
    {
        HandleCompanionChannel(player, body);
        return;
    }
    if (channel == Fusion::Downed::kChannelDowned)
    {
        HandleDownedChannel(player, body);
        return;
    }
    if (channel == Fusion::Prestige::kChannelPrestige)
    {
        HandlePrestigeChannel(player, body);
        return;
    }
    if (channel == Fusion::Classes::kChannelClasses)
    {
        sFusionClasses.HandleChannel(player, body);
        return;
    }
    // Player ships (FusionShips.cpp): a channel of their own.
    if (channel == Fusion::Ships::kChannel)
    {
        sShipMgr.HandleChannel(player, body);
        return;
    }
    // The bounty boards (FusionBounties.cpp): a channel of their own.
    if (channel == Fusion::Bounty::kChannelBounty)
    {
        sBountyMgr.HandleChannel(player, body);
        return;
    }
    if (channel == Fusion::ActionSystems::kChannel)
    {
        if (!player->IsInWorld() || !player->HasFusion())
            return;
        // Minstrelsy's ops 50-62 (FusionMinstrelsy.cpp): a song's notes and end, tips, its window.
        if (sMinstrelMgr.HandleChannel(player, body))
            return;
        if (std::optional<Fusion::ActionSystems::System> s = Fusion::ActionSystems::DecodeSystem(body))
            SetActionSystem(player, *s);
        else if (std::optional<uint32_t> tool = Fusion::ActionSystems::DecodeTool(body))
            SetUtilityTool(player, *tool);
        else if (std::optional<Fusion::ActionSystems::StrikeRequest> strike = Fusion::ActionSystems::DecodeStrike(body))
            ToolStrike(player, *strike);
        else
            RelayHands(player, body);
        return;
    }
    if (channel != Fusion::Packets::kChannelBuild || body.empty() || !player->IsInWorld())
        return;
    // The kits' ops on this channel: the old ledger used from the bags, the field editor asked
    // for from the kit window, and the window's status.
    if (body[0] == Fusion::kOpUseLedger)
    {
        uint64_t guid = 0;
        if (Fusion::DecodeUseLedger(body, guid))
            UseLedger(player, ObjectGuid(guid));
        return;
    }
    if (body[0] == Fusion::kOpFieldEdit && body.size() == 1)
    {
        UseFieldEditor(player);
        return;
    }
    if (body[0] == Fusion::kOpAskKitStatus && body.size() == 1)
    {
        SendKitStatus(player);
        return;
    }
    // Locks and portals (FusionLocks.cpp): ops 4-7.
    if (body[0] == Fusion::Locks::kOpSetLock || body[0] == Fusion::Locks::kOpTryCode
        || body[0] == Fusion::Portals::kOpAsk || body[0] == Fusion::Portals::kOpTravel)
    {
        HandleLockChannel(player, body);
        return;
    }
    // Furniture (FusionFurniture.cpp): F on a piece, and the stash.
    if (body[0] == Fusion::Furniture::kOpUse)
    {
        uint32 id = 0;
        if (Fusion::Furniture::DecodeUse(body, id))
            UseFurniture(player, id);
        return;
    }
    if (body[0] == Fusion::Furniture::kOpStash)
    {
        Fusion::Furniture::StashAct act;
        if (Fusion::Furniture::DecodeStash(body, act))
            HandleStash(player, act);
        return;
    }
    switch (Fusion::Build::ClientOp(body[0]))
    {
        case Fusion::Build::ClientOp::Place:
        {
            Fusion::Build::PlaceRequest r;
            if (Fusion::Build::DecodePlace(body, r))
                BuildPlace(player, r);
            else
                SendChannel(player, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeRefused(Fusion::Build::Refusal::Unknown));
            break;
        }
        case Fusion::Build::ClientOp::Remove:
        {
            uint32 id = 0;
            if (Fusion::Build::DecodeRemove(body, id))
                BuildRemove(player, id);
            break;
        }
        case Fusion::Build::ClientOp::Toggle:
        {
            // Its lock has its say (FusionLocks.cpp).
            uint32 id = 0;
            if (Fusion::Build::DecodeToggle(body, id))
                SwingDoor(player, id);
            break;
        }
        default:
            break;
    }
}

std::vector<Fusion::Build::Piece> FusionMgr::PiecesNear(uint32 mapId, Fusion::Vec3 at, float radius) const
{
    std::vector<Fusion::Build::Piece> out;
    std::lock_guard<std::mutex> lock(m_buildLock);
    auto it = m_pieces.find(mapId);
    if (it == m_pieces.end())
        return out;
    for (Fusion::Build::Piece const& p : it->second)
        if ((p.pos - at).Length() <= radius)
            out.push_back(p);
    return out;
}

void FusionMgr::Broadcast(Map* map, Fusion::Vec3 at, std::function<std::vector<uint8_t>(Player*)> const& body)
{
    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* p = it->getSource();
        if (!p || !p->IsInWorld() || !p->HasFusion())
            continue;
        if ((V(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ()) - at).Length() > Fusion::Build::kStreamFar)
            continue;
        std::vector<uint8_t> const b = body(p);
        if (!b.empty())
            SendChannel(p, Fusion::Packets::kChannelBuild, b);
    }
}

void FusionMgr::TellNear(Map* map, Fusion::Vec3 at, std::function<std::vector<uint8_t>(Player*)> const& body, float radius)
{
    if (!map)
        return;
    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* p = it->getSource();
        if (!p || !p->IsInWorld() || !p->HasFusion() || !p->GetSession())
            continue;
        if ((V(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ()) - at).Length() > radius)
            continue;
        std::vector<uint8_t> const b = body(p);
        if (!b.empty())
            SendChannel(p, Fusion::Packets::kChannelWorld, b);
    }
}

void FusionMgr::SendBuildStatus(Player* player)
{
    uint32 owned = 0;
    uint32 const me = player->GetGUIDLow();
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        for (auto const& kv : m_pieces)
            for (Fusion::Build::Piece const& p : kv.second)
                if (p.owner == me)
                    ++owned;
    }
    bool const free = m_buildFree || IsAdmin(player);
    // No cap on pieces (their cost and upkeep are the limit): max 0.
    SendChannel(player, Fusion::Packets::kChannelBuild, Fusion::Build::EncodeStatus(owned, 0, free));
}

void FusionMgr::BuildPlace(Player* player, Fusion::Build::PlaceRequest const& r)
{
    using namespace Fusion::Build;
    auto refuse = [&](Refusal why) { SendChannel(player, Fusion::Packets::kChannelBuild, EncodeRefused(why)); };
    FusionPlayerData& d = player->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    Map* map = player->GetMap();
    if (!CanBuildOn(map) || !player->IsAlive() || int32(now - d.lastPlaceMs) < 150)
        return refuse(Refusal::Unknown);
    // Every try counts against the pace, refused or not: a flood of placements that fail late
    // (no materials, no support) costs the server no more than the ones that succeed.
    d.lastPlaceMs = now;
    if (player->IsInCombat() && !IsAdmin(player))
        return refuse(Refusal::InCombat);
    // Not from a flight path or a boat: a builder passing over a city would wall in its roads.
    if ((player->IsTaxiFlying() || player->GetTransport()) && !IsAdmin(player))
        return refuse(Refusal::Unknown);
    Fusion::Vec3 const me = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() + 1.f);
    Piece p;
    p.type = r.type;
    p.pos = r.pos;
    p.yaw = r.yaw;
    p.map = map->GetId();
    p.owner = player->GetGUIDLow();
    p.health = Def(r.type).health;
    // Its decay starts now (fusion/Maintenance.h); a paid plot keeps it.
    p.decayedAt = int64(time(nullptr));
    // To its nearest point: a blueprint's long pieces (a roof, stairs) reach past their middles.
    if (Distance(p, me) > kPlaceReach + 3.f)
        return refuse(Refusal::TooFar);
    // The builder must see where it goes: not through a wall, a hill or someone's base (a bedroll
    // laid inside a sealed house from outside it). Traced a little short of its middle, so what
    // it snaps against never hides it; the builder's own pieces never hide it either, so a
    // blueprint's far wall still goes up behind its near one.
    {
        Fusion::Vec3 const c = Center(p);
        Fusion::Vec3 const to = c - me;
        float const len = to.Length();
        if (len > kSightSlack + 0.5f)
        {
            Fusion::Vec3 const end = me + to * ((len - kSightSlack) / len);
            if (!map->isInLineOfSight(me.x, me.y, me.z, end.x, end.y, end.z, true, true) || PieceBetween(p.map, me, end, p.owner))
                return refuse(Refusal::OutOfSight);
        }
    }
    // Nobody walls in a vendor, a flight master, a trainer or a Plot Steward.
    {
        float const look = kNpcClearance + 30.f;
        std::list<Creature*> around;
        MaNGOS::AnyUnitInObjectRangeCheck check(player, look);
        MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(around, check);
        Cell::VisitGridObjects(player, searcher, look);
        for (Creature const* c : around)
            if (c->IsAlive() && !c->IsPet() && CrowdsNpc(p, V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ()),
                    c->GetUInt32Value(UNIT_NPC_FLAGS)) && !IsAdmin(player))
                return refuse(Refusal::CrowdsNpc);
    }
    std::vector<Piece> nearby = PiecesNear(p.map, p.pos, 60.f);
    if (Overlaps(nearby, p))
        return refuse(Refusal::Overlaps);
    // Nobody is shut inside it: not the builder, not anyone standing where it would go.
    if (TrapsAPlayer(map, p))
        return refuse(Refusal::Traps);
    Fusion::Build::Ground const ground = GroundOf(map);
    // Ground pieces stand on the terrain or on a built floor; furniture as its mount asks (on a
    // table's top, hung on a wall: fusion/Furniture.h).
    if (IsFurniturePiece(r.type) ? !Fusion::Furniture::Footed(p, nearby, ground) : Def(r.type).groundOnly && !Footed(p, nearby, ground))
        return refuse(Refusal::Unsupported);
    if (!PlotAllowsBuild(player, p))
        return refuse(Refusal::Protected);
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        // Portals (fusion/Portals.h): two to a player, one to a plot.
        if (IsPortal(p.type))
        {
            uint32 const plot = Fusion::Plots::PlotOf(PlotGrid(), p);
            uint32 mine = 0, onPlot = 0;
            for (auto const& kv : m_pieces)
                for (Piece const& q : kv.second)
                    if (IsPortal(q.type))
                    {
                        if (q.owner == p.owner)
                            ++mine;
                        if (plot && kv.first == p.map && Fusion::Plots::PlotOf(PlotGrid(), q) == plot)
                            ++onPlot;
                    }
            if (!Fusion::Portals::MayPlace(mine, plot != 0, onPlot, IsAdmin(player)))
                return refuse(Refusal::PortalLimit);
        }
    }
    p.kept = KeptUntil(p) > p.decayedAt;
    p.id = 0xFFFFFFFF;
    nearby.push_back(p);
    Supports const s = Solve(nearby, ground);
    p.support = s.at(p.id);
    if (!Stands(p.type, p.support))
        return refuse(Refusal::Unsupported);
    // Construction: the skill, and the recipe's materials (FusionConstruction.cpp).
    bool const free = m_buildFree || IsAdmin(player);
    bool usedSalvage = false;
    if (std::optional<Refusal> const why = PayForPiece(player, p.type, free, usedSalvage))
        return refuse(*why);
    // Built free, it gives nothing of itself back (security review 2026-10, #14); its tiers say
    // their own when reinforced.
    p.paid = free ? uint8(Fusion::Build::kPaidAll & ~Fusion::Build::kPaidPiece) : Fusion::Build::kPaidAll;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        p.id = m_nextPieceId++;
        m_pieces[p.map].push_back(p);
    }
    if (BaseOf(p.type) == PieceType::Bedroll)
    {
        std::vector<std::pair<uint32, uint32>> old;     // (map, id)
        {
            std::lock_guard<std::mutex> lock(m_buildLock);
            for (auto const& kv : m_pieces)
                for (Piece const& q : kv.second)
                    if (q.owner == p.owner && BaseOf(q.type) == PieceType::Bedroll && q.id != p.id)
                        old.push_back({kv.first, q.id});
        }
        for (auto const& [oldMap, oldId] : old)
        {
            if (oldMap == map->GetId())
                RemovePieces(map, oldId, RemoveCause::TakenDown);
            else
            {
                std::lock_guard<std::mutex> lock(m_buildLock);
                auto& list = m_pieces[oldMap];
                list.erase(std::remove_if(list.begin(), list.end(), [oldId = oldId](Piece const& q) { return q.id == oldId; }), list.end());
                CharacterDatabase.PExecute("DELETE FROM `fusion_build_piece` WHERE `id` = %u", oldId);
            }
        }
        ChatHandler(player).SendSysMessage("Bedroll laid: you will come back to life here.");
    }
    CountStat(player, {Fusion::Stat::BuildingsBuilt, Fusion::kAnyKey});
    // Placing teaches nothing: the kit's making did (CraftKits).
    (void)usedSalvage;
    // Wanderers near take note of a builder (FusionWandererLives.cpp).
    sWandererMgr.NoteWorldEvent(player->GetMapId(), p.pos, Fusion::Wanderers::NewsKind::Building, player->GetName());
    CharacterDatabase.PExecute("INSERT INTO `fusion_build_piece` (`id`, `map`, `type`, `x`, `y`, `z`, `yaw`, `health`, `owner`, `lock_mode`, `decayed_at`, `paid`) "
        "VALUES (%u, %u, %u, %f, %f, %f, %f, %f, %u, %u, %" PRId64 ", %u)", p.id, p.map, uint32(p.type), p.pos.x, p.pos.y, p.pos.z, p.yaw, p.health, p.owner,
        uint32(p.lock), int64_t(p.decayedAt), uint32(p.paid));
    Piece const placed = p;
    Broadcast(map, p.pos, [this, placed](Player* viewer) {
        FusionPlayerData& vd = viewer->GetFusion();
        if (vd.knownMap != placed.map)
            return std::vector<uint8_t>();
        vd.knownPieces.insert(placed.id);
        return EncodePieces({placed}, viewer->GetGUIDLow());
    });
    SendBuildStatus(player);
}

void FusionMgr::BuildRemove(Player* player, uint32 id)
{
    using namespace Fusion::Build;
    Map* map = player->GetMap();
    Piece found;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(map->GetId());
        if (it == m_pieces.end())
            return;
        auto pit = std::find_if(it->second.begin(), it->second.end(), [id](Piece const& p) { return p.id == id; });
        if (pit == it->second.end())
            return;
        found = *pit;
    }
    // As placing: alive, and not mid-fight (no pulling down a wall about to break, to deny its
    // attacker the salvage).
    if (!player->IsAlive())
        return SendChannel(player, Fusion::Packets::kChannelBuild, EncodeRefused(Refusal::Unknown));
    if (player->IsInCombat() && !IsAdmin(player))
        return SendChannel(player, Fusion::Packets::kChannelBuild, EncodeRefused(Refusal::InCombat));
    if (!MayRemovePiece(player, found))
        return SendChannel(player, Fusion::Packets::kChannelBuild, EncodeRefused(Refusal::NotYours));
    if ((Center(found) - V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ())).Length() > kReach + 5.f)
        return SendChannel(player, Fusion::Packets::kChannelBuild, EncodeRefused(Refusal::TooFar));
    // Half the materials come back, scaled by what is left of it: to whoever built it, and only of
    // what was paid for it (a piece built free gives nothing back, Build::Piece::paid).
    RefundPiece(player, found);
    if (uint32 const fell = RemovePieces(map, id, RemoveCause::TakenDown))
        ChatHandler(player).PSendSysMessage("%u %s it held up came down with it.", fell, fell == 1 ? "piece" : "pieces");
    SendBuildStatus(player);
}

uint32 FusionMgr::RemovePieces(Map* map, uint32 id, Fusion::Build::RemoveCause cause)
{
    using namespace Fusion::Build;
    Fusion::Vec3 at;
    std::vector<Piece> nearby;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto& list = m_pieces[map->GetId()];
        auto pit = std::find_if(list.begin(), list.end(), [id](Piece const& p) { return p.id == id; });
        if (pit == list.end())
            return 0;
        at = pit->pos;
    }
    // A wide window, but only what stands near enough may fall: a piece at the window's edge
    // holding up on something just outside it is never taken for floating (Build::CollapseNear).
    nearby = PiecesNear(map->GetId(), at, kCollapseGather);
    std::vector<uint32> const fallen = CollapseNear(nearby, id, GroundOf(map), at);
    std::unordered_map<uint32, float> supports;
    for (Piece const& p : nearby)
        supports[p.id] = p.support;
    std::vector<Piece> changed;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto& list = m_pieces[map->GetId()];
        list.erase(std::remove_if(list.begin(), list.end(), [&](Piece const& p) {
            return p.id == id || std::find(fallen.begin(), fallen.end(), p.id) != fallen.end();
        }), list.end());
        m_streakSentries.erase(id);
        m_lockSecrets.erase(id);
        for (uint32 f : fallen)
        {
            m_streakSentries.erase(f);
            m_lockSecrets.erase(f);
        }
        for (Piece& p : list)
        {
            auto s = supports.find(p.id);
            if (s != supports.end() && std::fabs(s->second - p.support) > 0.02f)
            {
                p.support = s->second;
                changed.push_back(p);
            }
        }
    }
    CharacterDatabase.PExecute("DELETE FROM `fusion_build_piece` WHERE `id` = %u", id);
    for (uint32 f : fallen)
        CharacterDatabase.PExecute("DELETE FROM `fusion_build_piece` WHERE `id` = %u", f);
    uint32 const mapId = map->GetId();
    Broadcast(map, at, [&](Player* viewer) {
        FusionPlayerData& vd = viewer->GetFusion();
        if (vd.knownMap != mapId)
            return std::vector<uint8_t>();
        vd.knownPieces.erase(id);
        for (uint32 f : fallen)
            vd.knownPieces.erase(f);
        std::vector<uint8_t> b = EncodeRemoved({id}, cause);
        if (!fallen.empty())
        {
            std::vector<uint8_t> const more = EncodeRemoved(fallen, RemoveCause::Collapsed);
            SendChannel(viewer, Fusion::Packets::kChannelBuild, more);
        }
        if (!changed.empty())
            SendChannel(viewer, Fusion::Packets::kChannelBuild, EncodeHealth(changed));
        return b;
    });
    return uint32(fallen.size());
}

uint32 FusionMgr::PieceOnRay(uint32 mapId, Fusion::Vec3 origin, Fusion::Vec3 dir, float maxDistance, float& t,
    uint32 ignoreOwner) const
{
    std::lock_guard<std::mutex> lock(m_buildLock);
    auto it = m_pieces.find(mapId);
    if (it == m_pieces.end())
        return 0;
    uint32 best = 0;
    float bestT = maxDistance;
    for (Fusion::Build::Piece const& p : it->second)
    {
        if (ignoreOwner && p.owner == ignoreOwner)
            continue;
        // Far off the ray's span: skip without the box test.
        Fusion::Vec3 const rel = p.pos - origin;
        float const along = rel.Dot(dir);
        if (along < -6.f || along > maxDistance + 6.f || (rel - dir * along).Length() > 6.f)
            continue;
        float tp = 0.f;
        if (Fusion::Build::RayHits(p, origin, dir, bestT, tp) && tp < bestT)
        {
            bestT = tp;
            best = p.id;
        }
    }
    if (best)
        t = bestT;
    return best;
}

void FusionMgr::DamagePiece(Map* map, uint32 id, float damage, bool blast, Fusion::Plots::Actor const& by)
{
    DamagePiece(map, id, damage, blast ? Fusion::Build::DamageKind::Blast : Fusion::Build::DamageKind::Bullet, by);
}

void FusionMgr::DamagePiece(Map* map, uint32 id, float damage, Fusion::Build::DamageKind kind, Fusion::Plots::Actor const& by)
{
    using namespace Fusion::Build;
    if (!map || damage <= 0.f)
        return;
    Piece hurt;
    bool broken = false;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto& list = m_pieces[map->GetId()];
        auto pit = std::find_if(list.begin(), list.end(), [id](Piece const& p) { return p.id == id; });
        if (pit == list.end() || !PlotAllowsDamage(by, *pit))
            return;
        // Out in the world nothing protects it (a bought plot does, above); its reinforcement
        // turns part of a bullet or a blow (fusion/Build.h).
        pit->health -= DamageTaken(*pit, damage, kind);
        // No repair under fire (fusion/Maintenance.h kRepairAfterDamage).
        pit->damagedAt = int64(time(nullptr));
        broken = pit->health <= 0.f;
        hurt = *pit;
    }
    if (broken)
    {
        RemovePieces(map, id, RemoveCause::Destroyed);
        // Whoever broke it salvages part of it.
        SalvagePiece(map, hurt, by);
        return;
    }
    // With the time its health is true at, so decay is never counted twice (fusion/Maintenance.h).
    CharacterDatabase.PExecute("UPDATE `fusion_build_piece` SET `health` = %f, `decayed_at` = %" PRId64 " WHERE `id` = %u", hurt.health,
        int64_t(hurt.decayedAt), id);
    uint32 const mapId = map->GetId();
    Broadcast(map, hurt.pos, [&](Player* viewer) {
        FusionPlayerData const& vd = viewer->GetFusionConst();
        if (vd.knownMap != mapId || !vd.knownPieces.count(id))
            return std::vector<uint8_t>();
        return EncodeHealth({hurt});
    });
}

void FusionMgr::BlastPieces(Map* map, Fusion::Vec3 at, Fusion::WeaponProfile const& blast, Fusion::Plots::Actor const& by)
{
    if (!map || blast.explosionRadius <= 0.f)
        return;
    std::vector<Fusion::Build::Piece> const nearby = PiecesNear(map->GetId(), at, blast.explosionRadius + 6.f);
    for (Fusion::Build::Piece const& p : nearby)
    {
        // The nearest of its boxes' centres stands for the piece.
        float dist = 1e9f;
        for (Fusion::Build::Box const& b : Fusion::Build::Def(p.type).boxes)
            dist = std::min(dist, (Fusion::Build::Place(p, b).center - at).Length() - std::min(b.half.y, b.half.z));
        float const splash = Fusion::SplashDamage(blast, std::max(0.f, dist));
        if (splash > 0.f)
            DamagePiece(map, p.id, splash, true, by);
    }
}

void FusionMgr::UpdateBuild(Player* player, uint32 now)
{
    using namespace Fusion::Build;
    FusionPlayerData& d = player->GetFusion();
    if (!d.sentBuildStatus)
    {
        d.sentBuildStatus = true;
        SendBuildStatus(player);
    }
    UpdateConstruction(player, now);
    UpdateFurniture(player, now);
    ExpireStreakSentries(player->GetMap(), WorldTimer::getMSTime());
    uint32 const mapId = player->GetMapId();
    if (d.knownMap != mapId)
    {
        d.knownMap = mapId;
        d.knownPieces.clear();
    }
    Fusion::Vec3 const me = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    std::vector<Piece> send;
    bool warm = false;
    uint32 const myGuid = player->GetGUIDLow();
    std::vector<FusionPlayerData::Sentry> sentries;
    uint32 baseCount = 0;
    Fusion::Vec3 baseSum;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(mapId);
        if (it != m_pieces.end())
            for (Piece const& p : it->second)
            {
                float const dist = (p.pos - me).Length();
                if (p.owner == myGuid)
                {
                    // A homestead's pieces are no base for a raid (Plots.h).
                    if (dist <= Fusion::Defense::kBaseRadius && !Fusion::Plots::PlotOf(PlotGrid(), p))
                    {
                        ++baseCount;
                        baseSum = baseSum + p.pos;
                    }
                    if (p.type == PieceType::SentryGun && dist <= Fusion::Defense::kSentryOwnerRange)
                    {
                        FusionPlayerData::Sentry s;
                        s.piece = p.id;
                        s.muzzle = ToWorld(p, {0.f, 0.f, 1.f});
                        for (FusionPlayerData::Sentry const& old : d.sentries)
                            if (old.piece == p.id)
                                s = old;
                        s.yaw = p.yaw;
                        s.streak = m_streakSentries.count(p.id) != 0;
                        sentries.push_back(s);
                    }
                }
                if (BaseOf(p.type) == PieceType::Campfire && dist < 6.f)
                    warm = true;
                if (dist <= kStreamNear && !d.knownPieces.count(p.id))
                    send.push_back(p);
                else if (dist > kStreamFar)
                    d.knownPieces.erase(p.id);
            }
    }
    // A batch the channel holds (25 bytes a piece).
    constexpr size_t kBatch = 300;
    for (size_t i = 0; i < send.size(); i += kBatch)
    {
        std::vector<Piece> part(send.begin() + std::ptrdiff_t(i), send.begin() + std::ptrdiff_t(std::min(send.size(), i + kBatch)));
        for (Piece const& p : part)
            d.knownPieces.insert(p.id);
        SendChannel(player, Fusion::Packets::kChannelBuild, EncodePieces(part, player->GetGUIDLow()));
    }
    d.sentries = std::move(sentries);
    UpdateRaid(player, now, baseCount, baseCount ? baseSum * (1.f / float(baseCount)) : me);
    FreeFromPieces(player);

    // By a campfire, out of the fight, a player mends: 4% a second.
    if (warm && player->IsAlive() && !player->IsInCombat() && player->GetHealth() < player->GetMaxHealth())
        player->SetHealth(std::min(player->GetMaxHealth(), player->GetHealth() + std::max<uint32>(1, player->GetMaxHealth() / 25)));
    (void)now;
}

void FusionMgr::FreeFromPieces(Player* player)
{
    using namespace Fusion::Build;
    FusionPlayerData& d = player->GetFusion();
    Map* map = player->GetMap();
    if (!map || !player->IsInWorld() || player->GetTransport() || player->IsTaxiFlying() || player->IsBeingTeleported())
    {
        d.trappedLooks = 0;
        return;
    }
    // Placing refuses a piece that would shut anyone in (BuildPlace, a door's toggle); a body
    // found inside one anyway (a plot's pieces coming back, a position the server and the client
    // disagree on) is moved to the nearest spot clear of them. Two looks running (a second
    // apart), so a body brushing a wall as it moves is never taken for a caught one.
    Fusion::Vec3 const feet = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    std::vector<Piece> const nearby = PiecesNear(map->GetId(), feet, 40.f);
    if (!Traps(nearby, feet))
    {
        d.trappedLooks = 0;
        return;
    }
    if (++d.trappedLooks < 2)
        return;
    d.trappedLooks = 0;
    std::optional<Fusion::Vec3> const spot = FreeSpot(nearby, feet, GroundOf(map));
    if (!spot)
        return;
    player->NearTeleportTo(spot->x, spot->y, spot->z + 0.05f, player->GetOrientation());
    ChatHandler(player).SendSysMessage("You were caught inside a building: moved clear of it.");
}

void FusionMgr::SendWorld(Player* player, std::vector<uint8_t> const& body) const
{
    SendChannel(player, Fusion::Packets::kChannelWorld, body);
}

bool FusionMgr::RespawnAtBedroll(Player* player)
{
    using namespace Fusion::Build;
    if (!m_enabled || player->IsAlive() || player->GetTransport() || player->InBattleGround())
        return false;
    Piece bed;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(m_buildLock);
        auto it = m_pieces.find(player->GetMapId());
        if (it != m_pieces.end())
            for (Piece const& p : it->second)
                if (p.owner == player->GetGUIDLow() && BaseOf(p.type) == PieceType::Bedroll)
                {
                    bed = p;
                    found = true;
                }
    }
    if (!found)
        return false;
    player->ResurrectPlayer(0.5f);
    player->SpawnCorpseBones();
    player->TeleportTo(player->GetMapId(), bed.pos.x, bed.pos.y, bed.pos.z + 0.4f, bed.yaw);
    ChatHandler(player).SendSysMessage("You wake at your bedroll.");
    return true;
}

void FusionMgr::UpdateSentries(Player* player, uint32 now)
{
    using namespace Fusion::Defense;
    if (!player->IsAlive())
        return;
    FusionPlayerData& d = player->GetFusion();
    Map* map = player->GetMap();
    // The sentry's round: an M240's.
    static Fusion::WeaponProfile const sentryGun = [] {
        Fusion::WeaponProfile w;
        w.id = 3999;
        w.name = "sentry_gun";
        w.weaponClass = Fusion::WeaponClass::Lmg;
        w.damage = kSentryDamage;
        w.minDamage = kSentryDamage * 0.8f;
        w.maxDamageRange = 40.f;
        w.minDamageRange = 80.f;
        return w;
    }();
    Fusion::Vec3 const me = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    for (FusionPlayerData::Sentry& s : d.sentries)
    {
        if (!s.trigger.Fire(now, kSentryBurst, kSentryGapMs, kSentryPauseMs))
            continue;
        // A new burst: choose the target.
        Unit* target = nullptr;
        if (s.target && s.trigger.burstLeft + 1 < kSentryBurst)
            target = map->GetUnit(ObjectGuid(s.target));
        // A burst's target that slipped out of the arc is let go.
        if (target && !SentryCovers(s.muzzle, s.yaw, V(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ() + 1.f)))
            target = nullptr;
        if (!target || !target->IsAlive())
        {
            std::list<Unit*> units;
            float const reach = kSentryRange + (s.muzzle - me).Length();
            MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, reach);
            MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
            Cell::VisitAllObjects(player, searcher, reach);
            std::vector<Candidate> cs;
            std::unordered_map<uint64, Unit*> byGuid;
            for (Unit* u : units)
            {
                Creature* c = u->ToCreature();
                // The killstreak's sentry (MW2's) also turns on the enemy players its owner may
                // fight; a built one only ever shot creatures.
                bool const enemyPlayer = s.streak && u->IsPlayer() && u != player && player->IsValidAttackTarget(u) && PvpAllows(player, u);
                if (!enemyPlayer && (!c || c->IsPet() || c->IsCivilian() || c->IsTotem()))
                    continue;
                Candidate cand;
                cand.guid = u->GetObjectGuid().GetRawValue();
                cand.position = V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + 1.f);
                cand.alive = u->IsAlive();
                cand.hostile = enemyPlayer || (c && c->IsHostileTo(player) && player->IsValidAttackTarget(u));
                cand.inCombat = u->IsInCombat();
                cs.push_back(cand);
                byGuid[cand.guid] = u;
            }
            Fusion::Vec3 const from = s.muzzle;
            uint32 const mapId = map->GetId();
            auto sight = [&](Fusion::Vec3 to) {
                if (!map->isInLineOfSight(from.x, from.y, from.z + 0.2f, to.x, to.y, to.z, true, true))
                    return false;
                Fusion::Vec3 const dir = (to - from).Normalized();
                float t = 0.f;
                // Its own box is behind the muzzle; anything else on the way blocks.
                return !PieceOnRay(mapId, from + dir * 0.6f, dir, (to - from).Length() - 0.6f, t);
            };
            std::optional<uint64_t> const pick = SentryTarget(from, s.yaw, cs, sight);
            target = pick ? byGuid[*pick] : nullptr;
            s.target = pick ? *pick : 0;
        }
        if (!target)
        {
            s.trigger.burstLeft = 0;    // nothing to shoot: wait a pause
            continue;
        }
        Fusion::HitInput in;
        in.weapon = &sentryGun;
        in.distance = (V(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ()) - s.muzzle).Length();
        in.location = Fusion::HitLocation::TorsoUpper;
        in.ammo = Fusion::AmmoType::Basic;
        in.attacker = StatsOf(player);
        bool const hit = roll_chance_f(80.f);
        uint32 damage = 0;
        if (hit)
        {
            damage = std::max<uint32>(1, uint32(HealthDamage(player, target, in) + 0.5f));
            uint64 const guid = target->GetObjectGuid().GetRawValue();
            Hit(player, target, damage);
            Fusion::Packets::Events ev;
            uint8 const flags = target->IsAlive() ? 0 : Fusion::Packets::EVENT_FATAL;
            ev.events.push_back({Fusion::Packets::EventKind::Hit, flags, guid, damage, sentryGun.id});
            if (!target->IsAlive())
                ev.events.push_back({Fusion::Packets::EventKind::Kill, flags, guid, 0, sentryGun.id});
            SendEvents(player, ev);
        }
        uint32 const piece = s.piece;
        uint64 const targetGuid = target->GetObjectGuid().GetRawValue();
        Broadcast(map, s.muzzle, [&](Player* viewer) {
            if (viewer->GetFusionConst().knownMap != map->GetId())
                return std::vector<uint8_t>();
            return Fusion::Build::EncodeSentryFire(piece, targetGuid, hit);
        });
    }
}

std::vector<uint32> const& FusionMgr::RaidKinds(uint32 level)
{
    std::lock_guard<std::mutex> lock(m_raidKindsLock);
    if (!m_raidKindsMade)
    {
        m_raidKindsMade = true;
        for (auto const& kv : sObjectMgr.GetCreatureInfoMap())
        {
            CreatureInfo const* ci = kv.second.get();
            if (!ci || ci->type != CREATURE_TYPE_HUMANOID || ci->rank != 0 || ci->npc_flags != 0 || !ci->subname.empty()
                || !ci->loot_id || ci->level_max > 62 || ci->level_min < 1 || ci->script_id != 0
                || (ci->unit_class != 1 && ci->unit_class != 4))   // fighters (warriors, rogues): casters wait for the spell system
                continue;
            FactionTemplateEntry const* ft = sObjectMgr.GetFactionTemplateEntry(ci->faction);
            if (!ft || !ft->IsHostileToPlayers())
                continue;
            FactionEntry const* raw = sObjectMgr.GetFactionEntry(ft->faction);
            if (raw && raw->reputationListID >= 0)
                continue;
            std::string const& n = ci->name;
            if (n.find('[') != std::string::npos || n.find("Trigger") != std::string::npos || n.find("Spirit") != std::string::npos
                || n.find("Image") != std::string::npos || n.find("Invisible") != std::string::npos || n.find("Test") != std::string::npos
                || n.find("Captain") != std::string::npos || n.find("Lieutenant") != std::string::npos)
                continue;
            for (uint32 l = ci->level_min; l <= ci->level_max; ++l)
                m_raidKinds[l].push_back(ci->entry);
        }
        uint32 total = 0;
        for (auto const& kv : m_raidKinds)
            total += uint32(kv.second.size());
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u raider kinds by level.", total);
    }
    static std::vector<uint32> const none;
    for (int32 spread = 0; spread <= 4; ++spread)
        for (int32 l : {int32(level) - spread, int32(level) + spread})
        {
            auto it = m_raidKinds.find(uint32(std::max(1, l)));
            if (it != m_raidKinds.end() && !it->second.empty())
                return it->second;
        }
    return none;
}

void FusionMgr::SpawnRaid(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    std::vector<uint32> const& kinds = RaidKinds(player->GetLevel());
    if (kinds.empty())
        return;
    Map* map = player->GetMap();
    // From one side, spread a little: a war party, not a ring.
    float const from = frand(0.f, 2.f * float(M_PI));
    // One or two kinds, as a band of the same people would be.
    uint32 const kindA = kinds[urand(0, uint32(kinds.size()) - 1)];
    uint32 const kindB = kinds[urand(0, uint32(kinds.size()) - 1)];
    d.raiders.clear();
    for (uint32 i = 0; i < d.raidSize; ++i)
    {
        float const a = from + frand(-0.35f, 0.35f);
        float const r = Fusion::Defense::kRaidSpawnDistance + frand(-6.f, 6.f);
        float x = d.raidBase.x + std::cos(a) * r;
        float y = d.raidBase.y + std::sin(a) * r;
        float z = map->GetHeight(x, y, d.raidBase.z + 30.f, true, 80.f);
        if (z < INVALID_HEIGHT + 1.f)
            continue;
        Creature* c = player->SummonCreature(i % 3 == 2 ? kindB : kindA, x, y, z + 0.5f, a + float(M_PI),
            TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, Fusion::Defense::kRaidLengthMs + 60000);
        if (!c)
            continue;
        c->SetRespawnDelay(0);
        if (c->AI())
            c->AI()->AttackStart(player);
        d.raiders.push_back(c->GetObjectGuid());
    }
    std::vector<uint8_t> body{2, uint8_t(d.raiders.size())};
    if (CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(kindA))
        body.insert(body.end(), ci->name.begin(), ci->name.begin() + std::min<size_t>(ci->name.size(), 40));
    SendWorld(player, body);
}

void FusionMgr::UpdateRaid(Player* player, uint32 now, uint32 piecesNear, Fusion::Vec3 base)
{
    using namespace Fusion::Defense;
    FusionPlayerData& d = player->GetFusion();
    Map* map = player->GetMap();
    uint32 alive = 0;
    for (ObjectGuid const& g : d.raiders)
        if (Creature* c = map->GetCreature(g); c && c->IsAlive())
            ++alive;
    AreaEntry const* zone = AreaEntry::GetById(player->GetZoneId());
    bool const city = zone && (zone->Flags & AREA_FLAG_CAPITAL);
    bool const hasBase = BaseQualifies(piecesNear, player->GetLevel()) && player->IsAlive() && !city
        && !player->IsTaxiFlying() && !player->InBattleGround() && !map->Instanceable() && player->HasFusion()
        && Fusion::Plots::RaidsAllowedAt(PlotGrid(), map->GetId(), player->GetPositionX(), player->GetPositionY());
    if (d.raid.phase == RaidPhase::Quiet && hasBase)
        d.raidBase = base;
    switch (Advance(d.raid, now, hasBase || d.raid.phase != RaidPhase::Quiet, alive, frand(0.f, 1.f)))
    {
        case RaidStep::Warn:
            d.raidSize = RaidSize(piecesNear);
            SendWorld(player, {1, uint8_t(kRaidWarningMs / 1000), uint8_t(d.raidSize)});
            break;
        case RaidStep::Spawn:
            SpawnRaid(player);
            break;
        case RaidStep::End:
        {
            bool const repelled = alive == 0 && !d.raiders.empty();
            for (ObjectGuid const& g : d.raiders)
                if (Creature* c = map->GetCreature(g); c && c->IsAlive())
                    c->DespawnOrUnsummon();
            if (repelled)
                player->GiveXP(uint32(d.raiders.size()) * (40u + player->GetLevel() * 8u), nullptr);
            d.raiders.clear();
            SendWorld(player, {3, uint8_t(repelled ? 1 : 0)});
            break;
        }
        default:
            break;
    }
}

void FusionMgr::UpdateCreatureSiege(Creature* c, uint32 diffMs)
{
    if (c->m_fusionGunRole == uint8(Fusion::GunRole::Gunner) || c->IsPet() || !c->IsInCombat())
    {
        c->m_fusionSiegePiece = 0;
        return;
    }
    c->m_fusionSiegeClockMs += diffMs;
    Unit* victim = c->GetVictim();
    if (!victim || !victim->IsCharmerOrOwnerPlayerOrPlayerItself() || c->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
        return;
    Fusion::Vec3 const from = V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ() + 1.f);
    Fusion::Vec3 const to = V(victim->GetPositionX(), victim->GetPositionY(), victim->GetPositionZ() + 1.f);
    float const dist = (to - from).Length();
    float t = 0.f;
    uint32 const piece = dist > 0.5f ? PieceOnRay(c->GetMapId(), from, (to - from) * (1.f / dist), dist, t) : 0;
    // Walking around a wall it found a way past: let it get there.
    if (int32(c->m_fusionSiegeClockMs - c->m_fusionDetourUntilMs) < 0)
    {
        if (!piece)
        {
            c->m_fusionDetourUntilMs = c->m_fusionSiegeClockMs;
            c->GetMotionMaster()->MoveChase(victim);
        }
        return;
    }
    // Walking into a wall: first a way around it (twice at most), else stop at it and break it
    // down; the way clear, back to the chase. A piece on a plot it may not break (Plots.h) is no
    // siege: it keeps chasing.
    if (piece && t < 2.5f + c->GetSpeed(MOVE_RUN) * 0.3f && c->m_fusionSiegePiece != piece
        && TryDetour(c, from, to, piece))
        return;
    if (piece && t < 2.5f + c->GetSpeed(MOVE_RUN) * 0.3f && PieceDamageableBy(c->GetMapId(), piece, Fusion::Plots::Actor::Creature()))
    {
        if (c->m_fusionSiegePiece != piece)
        {
            c->m_fusionSiegePiece = piece;
            c->StopMoving();
            c->GetMotionMaster()->MoveIdle();
            c->m_fusionSiegeNextMs = c->m_fusionSiegeClockMs + 600;
        }
        if (int32(c->m_fusionSiegeClockMs - c->m_fusionSiegeNextMs) >= 0)
        {
            c->m_fusionSiegeNextMs = c->m_fusionSiegeClockMs + 2000;
            c->SetFacingTo(std::atan2(to.y - from.y, to.x - from.x));
            c->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK1H);
            float const blow = (15.f + 3.f * float(c->GetLevel())) * (c->IsElite() ? 3.f : 1.f);
            DamagePiece(c->GetMap(), piece, blow, false, Fusion::Plots::Actor::Creature());
        }
    }
    else if (c->m_fusionSiegePiece)
    {
        c->m_fusionSiegePiece = 0;
        c->GetMotionMaster()->MoveChase(victim);
    }
}

bool FusionMgr::TryDetour(Creature* c, Fusion::Vec3 from, Fusion::Vec3 to, uint32 piece)
{
    if (c->m_fusionDetourPiece != piece)
    {
        c->m_fusionDetourPiece = piece;
        c->m_fusionDetourTries = 0;
    }
    if (c->m_fusionDetourTries >= kDetourTries)
        return false;
    ++c->m_fusionDetourTries;
    Map* map = c->GetMap();
    float const heading = std::atan2(to.y - from.y, to.x - from.x);
    // Either side, a little off the line first, then square to it.
    for (float turn : {0.87f, -0.87f, 1.57f, -1.57f})
    {
        float const a = heading + turn;
        Fusion::Vec3 side = V(from.x + std::cos(a) * kDetourStep, from.y + std::sin(a) * kDetourStep, from.z);
        float const ground = map->GetHeight(side.x, side.y, side.z + 2.f);
        if (ground <= INVALID_HEIGHT || std::fabs(ground + 1.f - side.z) > 3.f)
            continue;           // a cliff or a drop
        side.z = ground + 1.f;
        auto clear = [&](Fusion::Vec3 a, Fusion::Vec3 b) {
            Fusion::Vec3 const d = b - a;
            float const len = d.Length();
            float hit = 0.f;
            return len < 0.1f || (!PieceOnRay(c->GetMapId(), a, d * (1.f / len), len, hit)
                && map->isInLineOfSight(a.x, a.y, a.z, b.x, b.y, b.z, true, true));
        };
        if (!clear(from, side) || !clear(side, to))
            continue;
        c->StopMoving();
        c->GetMotionMaster()->MovePoint(0, side.x, side.y, side.z - 1.f, MOVE_PATHFINDING | MOVE_RUN_MODE);
        float const secs = kDetourStep / std::max(1.f, c->GetSpeed(MOVE_RUN)) + 0.5f;
        c->m_fusionDetourUntilMs = c->m_fusionSiegeClockMs + uint32(secs * 1000.f);
        return true;
    }
    return false;
}

bool FusionMgr::CanBeDrawn(Creature* c, Player* player) const
{
    // Only the hostile come: a neutral creature minds its own business. Pets, charmed and
    // scripted-passive creatures keep their ways; one on its way home lets its leash run out.
    return c && c->IsAlive() && !c->IsInCombat() && !c->IsPet() && c->GetCharmerOrOwnerGuid().IsEmpty()
        && !c->IsInEvadeMode() && c->AI() && c->CanInitiateAttack() && c->IsHostileTo(player)
        && player->IsValidAttackTarget(c);
}

void FusionMgr::Alert(Player* player, Fusion::Vec3 at, float radius)
{
    if (!player->IsAlive() || radius <= 0.f || !player->HasFusion())
        return;
    FusionPlayerData& d = player->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    uint32 const budget = d.hearing.Left(now);
    if (budget == 0)
        return;
    float const reach = radius + (V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()) - at).Length();
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, reach);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(player, searcher, reach);
    Map const* map = player->GetMap();
    std::vector<Fusion::Listener> listeners;
    for (Unit* u : units)
    {
        Creature* c = u->ToCreature();
        if (!CanBeDrawn(c, player))
            continue;
        // One already on its way after an earlier shot is not counted twice.
        uint64 const guid = c->GetObjectGuid().GetRawValue();
        if (std::any_of(d.heardAlerts.begin(), d.heardAlerts.end(), [guid](FusionPlayerData::HeardAlert const& a) { return a.creature == guid; }))
            continue;
        Fusion::Vec3 const ear = V(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ() + 1.5f);
        float const distance = (ear - at).Length();
        if (distance > radius)
            continue;
        Fusion::Listener l;
        l.guid = guid;
        l.distance = distance;
        l.lineOfSight = map->isInLineOfSight(at.x, at.y, at.z, ear.x, ear.y, ear.z, true, true);
        listeners.push_back(l);
    }
    std::vector<Fusion::HeardBy> const heard = Fusion::WhoHearsTheShot(std::move(listeners), radius, budget, urand(0, 0xFFFFFFFF));
    if (heard.empty())
        return;
    d.hearing.Spend(uint32(heard.size()), now);
    for (Fusion::HeardBy const& h : heard)
    {
        if (h.delayMs)
        {
            // Behind a wall: it turns to the noise and comes after a beat.
            d.heardAlerts.push_back({h.guid, now + h.delayMs, radius});
            if (Creature* c = player->GetMap()->GetCreature(ObjectGuid(h.guid)); c && c->movespline->Finalized())
                c->SetFacingToObject(player);
            continue;
        }
        if (Creature* c = player->GetMap()->GetCreature(ObjectGuid(h.guid)))
            c->AI()->AttackStart(player);
    }
}

void FusionMgr::UpdateHeardAlerts(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    for (size_t i = 0; i < d.heardAlerts.size();)
    {
        FusionPlayerData::HeardAlert const a = d.heardAlerts[i];
        if (int32(now - a.dueMs) < 0)
        {
            ++i;
            continue;
        }
        d.heardAlerts[i] = d.heardAlerts.back();
        d.heardAlerts.pop_back();
        if (!player->IsAlive() || d.downed.Active())
            continue;
        // It comes if it still can, and the shooter has not gone far off meanwhile.
        Creature* c = player->GetMap()->GetCreature(ObjectGuid(a.creature));
        if (!CanBeDrawn(c, player) || !c->IsWithinDistInMap(player, a.radius * 1.25f))
            continue;
        c->AI()->AttackStart(player);
    }
}

void FusionMgr::RallyAllies(Creature* c, Unit* attacker)
{
    if (!c || !attacker || !c->IsAlive() || c->IsPet() || !c->GetCharmerOrOwnerGuid().IsEmpty() || c->IsInEvadeMode())
        return;
    // Cold-Blooded draws no help (as WoW's own call for help).
    if (Fusion::PerkFlags const* perks = PerksOf(attacker); perks && Fusion::BlocksCallForHelp(*perks))
        return;
    uint32 const now = WorldTimer::getMSTime();
    if (c->m_fusionRallied && now - c->m_fusionRallyMs < Fusion::kGunfight.callForHelpEveryMs)
        return;
    c->m_fusionRallied = true;
    c->m_fusionRallyMs = now;
    float const radius = Fusion::kGunfight.callForHelpRadius;
    std::list<Unit*> units;
    MaNGOS::AnySameFactionUnitInObjectRangeCheck check(c, radius);
    MaNGOS::UnitListSearcher<MaNGOS::AnySameFactionUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitGridObjects(c, searcher, radius);
    std::vector<std::pair<float, Creature*>> allies;
    for (Unit* u : units)
    {
        Creature* a = u->ToCreature();
        // Its own, out of a fight, free to act, against an enemy of theirs, in sight of the call.
        if (!a || a == c || !a->IsAlive() || a->IsInCombat() || a->IsPet() || !a->GetCharmerOrOwnerGuid().IsEmpty()
            || a->IsInEvadeMode() || !a->AI() || a->HasReactState(REACT_PASSIVE)
            || a->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_PENDING_STUNNED)
            || a->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_SELECTABLE)
            || a->IsFriendlyTo(attacker) || !attacker->IsTargetableBy(a) || !c->IsWithinLOSInMap(a))
            continue;
        allies.push_back({c->GetDistance(a), a});
    }
    std::sort(allies.begin(), allies.end(), [](auto const& x, auto const& y) { return x.first < y.first; });
    size_t const n = std::min<size_t>(allies.size(), Fusion::kGunfight.callForHelpMax);
    for (size_t i = 0; i < n; ++i)
        allies[i].second->AI()->AttackStart(attacker);
}

bool FusionMgr::DeathstreakReady(FusionPlayerData const& d, uint16 id) const
{
    if (d.perks.deathstreak != id)
        return false;
    Fusion::Perk const* p = m_catalog.FindPerk(id);
    return p && d.deathStreak.Reached(Fusion::DeathstreakDeaths(d.perks, p->deaths));
}

uint32 FusionMgr::AdjustDamageTaken(Unit* /*attacker*/, Player* victim, uint32 damage)
{
    if (!m_enabled || !victim->HasFusion() || !victim->IsAlive())
        return damage;
    FusionPlayerData& d = victim->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    // Painkiller: a respawn's first seconds take a third.
    bool const painkiller = d.painkillerUntilMs && int32(d.painkillerUntilMs - now) > 0;
    damage = std::max<uint32>(1, uint32(float(damage) * Fusion::DamageTakenMultiplier(painkiller)));
    // A minstrel's Hymn of Warding (FusionMinstrelsy.cpp).
    damage = std::max<uint32>(1, uint32(float(damage) * sMinstrelMgr.DamageTakenFactor(victim) + 0.5f));
    // Last Stand and Final Stand are the downed state's now (DownedDamage, FusionDowned.cpp).
    return damage;
}

void FusionMgr::Explode(Player* owner, Fusion::Vec3 at, float radius, float inner, float outer, uint16 weaponId)
{
    FusionPlayerData& d = owner->GetFusion();
    Alert(owner, at, Fusion::kBlastHearing);
    Fusion::WeaponProfile blast;
    blast.id = weaponId;
    blast.explosionRadius = radius;
    blast.explosionInner = inner;
    blast.explosionOuter = outer;
    BlastPieces(owner->GetMap(), at, blast, PlotActor(owner));
    // And any ship's hull near it (FusionShips.cpp).
    sShipMgr.BlastAt(owner, at, radius, inner);
    // Everyone near sees it go off.
    std::vector<uint8_t> const seen = Fusion::Launch::EncodeExplosion(at, radius, weaponId);
    TellNear(owner->GetMap(), at, [&](Player*) { return seen; });
    Map const* map = owner->GetMap();
    float const reach = radius + (V(owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ()) - at).Length();
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(owner, owner, reach);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(owner, searcher, reach);
    Fusion::Packets::Events ev;
    uint32 const now = WorldTimer::getMSTime();
    for (Unit* u : units)
    {
        if (u == owner || !u->IsAlive() || !owner->IsValidAttackTarget(u) || !PvpAllows(owner, u))
            continue;
        Fusion::Vec3 const centre = V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + 1.f);
        if ((centre - at).Length() <= radius + 4.f)
            ShotNear(u, now);           // a gunner near a blast ducks for cover
        float splash = Fusion::SplashDamage(blast, (centre - at).Length());
        if (splash <= 0.f || !map->isInLineOfSight(at.x, at.y, at.z, centre.x, centre.y, centre.z, true, true))
            continue;
        // A killstreak's craft: the launchers and grenades are the answer to it.
        if (Creature const* uc = u->ToCreature(); uc && Fusion::Sky::CraftOfEntry(uc->GetEntry()))
            splash *= Fusion::Sky::kCraftBlastScale;
        Fusion::WeaponProfile hit = blast;
        // Danger Close; its Pro adds the killstreaks' blasts (air support).
        hit.damage = hit.minDamage = splash * Fusion::ExplosiveDamageMultiplier(d.perks, weaponId >= 3000 && weaponId < 4000);
        Fusion::HitInput in;
        in.weapon = &hit;
        in.distance = 0.f;
        in.location = Fusion::HitLocation::TorsoUpper;
        in.ammo = Fusion::AmmoType::Explosive;
        in.attacker = StatsOf(owner);
        uint32 const damage = std::max<uint32>(1, uint32(HealthDamage(owner, u, in) + 0.5f));
        uint64 const guid = u->GetObjectGuid().GetRawValue();
        FusionHitSource source;
        source.weaponId = weaponId;
        source.explosive = true;
        Hit(owner, u, damage, source);
        uint8 const flags = u->IsAlive() ? 0 : Fusion::Packets::EVENT_FATAL;
        ev.events.push_back({Fusion::Packets::EventKind::Hit, flags, guid, damage, weaponId});
        if (!u->IsAlive())
        {
            ev.events.push_back({Fusion::Packets::EventKind::Kill, flags, guid, 0, weaponId});
            // A killstreak's kill (a bomb, a shell, a rocket): the owner's feed names it.
            if (weaponId > 3000 && weaponId < 3000 + 64)
                NoteCraftKill(owner, uint32(weaponId - 3000), u);
        }
        if (ev.events.size() + 2 > Fusion::Packets::kMaxEventsPerPacket)
        {
            SendEvents(owner, ev);
            ev.events.clear();
        }
    }
    SendEvents(owner, ev);
}

void FusionMgr::HandleShots(Player* shooter, Fusion::Packets::ShotBatch const& batch)
{
    if (!m_enabled || !Fusion::CanFire(ActorOf(shooter)))
        return;
    // Disarmed, under Blessing of Protection, mind controlled: no shot, no swing.
    if (sFusionClasses.BlocksFire(shooter))
        return;
    FusionPlayerData& d = shooter->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    Fusion::Packets::Events shooterEvents;

    for (Fusion::Packets::Shot const& sent : batch.shots)
    {
        // The shot starts where the server has the shooter's eye, give or take the latency's
        // tolerance: never beside a target behind a wall, nor off the map (whose height and sight
        // lookups assert on it). Everything below takes this origin.
        Fusion::Packets::Shot shot = sent;
        shot.origin = Fusion::ShotOrigin(sent.origin, EyeOf(shooter), ShotOriginTolerance(shooter));
        // Down: only Last Stand's pistol fires (fusion/Downed.h); no swing, no equipment.
        if (d.downed.Active())
        {
            Fusion::WeaponProfile const* gun = m_catalog.Weapons().Find(shot.weaponId);
            if (!gun || !Fusion::Downed::MayFire(d.downed.armed, gun->weaponClass))
                continue;
        }
        // A plain melee swing of the main hand.
        if (shot.weaponId == Fusion::Melee::kSwingWeaponId)
        {
            Strike(shooter, shot);
            continue;
        }
        // A druid form has no hands for a gun, a grenade or a killstreak's radio
        // (fusion/DruidForms.h): only its swipe above.
        if (Fusion::DruidForms::BlocksGuns(uint8(shooter->GetShapeshiftForm())))
            continue;
        std::optional<Fusion::WeaponProfile> const held = EffectiveWeapon(shooter, shot.weaponId);
        if (!held)
            continue;
        Fusion::WeaponProfile const* w = &*held;
        if (w->slot == Fusion::WeaponSlot::Lethal || w->slot == Fusion::WeaponSlot::Tactical)
        {
            UseEquipment(shooter, *w, shot);
            continue;
        }
        if (w->slot == Fusion::WeaponSlot::Killstreak)
        {
            CallIn(shooter, *w, shot);
            continue;
        }
        if (!Fusion::FiresRounds(w->weaponClass))
            continue;                   // a shield or a knife strikes, it fires nothing
        if (!d.unlocks.CanUseWeapon(*w) && !PickupAllows(d, w->id))
            continue;
        // Only a gun in hand fires: the class's, a picked-up one or Last Stand's pistol, not any
        // unlocked gun (each kept a full magazine of its own: endless ammunition).
        // (An underbarrel weapon was checked against its gun by EffectiveWeapon.)
        if (Fusion::ClassLoadout const* c = d.Equipped();
            c && w->slot != Fusion::WeaponSlot::Alternate && !Fusion::GunInHand(w->id, c->primary, c->secondary, PickupAllows(d, w->id), d.downed.Active() ? d.downedPistol : 0))
            continue;
        // No fire while sprinting or in MW2's sprint-out; 0.1 s of slack for the update tick that
        // records the sprint's end after the client stopped.
        if (!Fusion::Stats::SprintOutReady(Fusion::SecondsSince(now, d.sprintEndedMs), d.sprinting,
                d.combat[Fusion::Stats::Out::SprintOutTime], 0.1f))
            continue;

        // One shot is one pull of the trigger: one round, every pellet.
        uint32 const shotTime = d.clock.ToServerTime(shot.timeMs, now);
        // Shots saved up past the lag window would all go off at once (a launcher's magazine in
        // one packet): only recent ones count.
        if (!Fusion::ShotFresh(shotTime, now))
            continue;
        // An akimbo pair's left gun fires from its own magazine.
        Fusion::GunAmmo& ammo = d.AmmoFor(*w, (shot.flags & Fusion::Packets::SHOT_LEFT) != 0);
        Fusion::AmmoType const round = ammo.NextRound();
        // Enchanted rounds in this gun (Rounds.h): frost's cycle is slower.
        float const cycle = d.rounds.Active(now) && d.rounds.gun == w->id
            ? Fusion::Rounds::Def(d.rounds.round).fireTimeMultiplier : 1.f;
        if (d.hasLastShot && int32(shotTime - d.lastShotMs) < int32(w->fireTime * cycle * 1000.f * Fusion::FireRateSlack(*w)))
            continue;   // faster than the gun fires (explosives are held closer, HitValidation.h)
        if (!ammo.Fire())
            continue;
        Fusion::Rounds::Round const enchanted = d.rounds.Fire(w->id, now);
        // The load's last round, or the first of a fresh magazine of it: the HUD's count.
        if (enchanted != Fusion::Rounds::Round::None
            && (!d.rounds.Active(now) || (!d.rounds.timed && d.rounds.roundsLeft % std::max<uint32>(1, w->magSize) == 0)))
            SendRounds(shooter);
        d.lastShotMs = shotTime;
        d.hasLastShot = true;
        // A reload counts from the gun's last round (fusion/Reload.h), on the server's clock as
        // the reload's word is: both arrive with the same lag.
        d.reloadPace[w->id].Fired(now);
        // A shot gives a camouflaged hunter away (FusionClassAbilities.cpp).
        sFusionClasses.OnFired(shooter);

        // The shot is heard: hostile creatures near enough come for the shooter (a few a shot).
        if (int32(now - d.lastAlertMs) >= int32(Fusion::kGunfight.hearingEveryMs))
        {
            d.lastAlertMs = now;
            Alert(shooter, V(shooter->GetPositionX(), shooter->GetPositionY(), shooter->GetPositionZ() + 1.5f),
                Fusion::GunfireHearing(w->silenced, d.perks.coldBlooded));
        }

        // The pellets scatter in the cone the client drew them in, held to the gun's least.
        bool const ads = shot.flags & Fusion::Packets::SHOT_ADS;
        // A demoralized shooter's rounds scatter wider than the client drew them.
        float const spread = Fusion::ClampSpread(*w, ads, shot.spreadDeg) * sFusionClasses.SpreadMultiplier(shooter);
        float const range = Fusion::PelletRange(*w);
        Fusion::Vec3 const origin = V(shot.origin.x, shot.origin.y, shot.origin.z);
        Fusion::Vec3 const aim = V(shot.direction.x, shot.direction.y, shot.direction.z).Normalized();
        if (aim.Length() < 0.5f)
            continue;

        // Every body that could stand on a pellet's ray: the server, not the client, decides who
        // is hit, so a crowd takes the pellets wherever they land.
        // A lobbed grenade can come down a little beyond its weapon's range.
        float const reach = (Fusion::Launch::IsGrenadeLauncher(*w) ? std::max(range, 150.f) : range) + 5.f;
        std::list<Unit*> units;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(shooter, shooter, reach);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(shooter, searcher, reach);
        struct Body
        {
            Unit* unit;
            Fusion::PoseHistory single;
            Fusion::PoseHistory const* history;
            Fusion::HitboxShape shape;
            // A creature in a fight: where it stood the last second (RecordCreaturePose).
            Fusion::PoseHistory const* rewind = nullptr;
        };
        std::vector<Body> bodies;
        for (Unit* u : units)
        {
            if (u == shooter || !u->IsAlive() || ShotPassesThrough(shooter, u) || !shooter->IsValidAttackTarget(u))
                continue;
            if (Player* tp = u->GetCharmerOrOwnerPlayerOrPlayerItself(); tp && tp != shooter)
            {
                Fusion::PvpPlayer const a = PvpOf(shooter, &d);
                Fusion::PvpPlayer const b = PvpOf(tp, tp->HasFusion() ? &tp->GetFusionConst() : nullptr);
                bool const victimStarted = tp->GetVictim() == shooter || !m_pvpLowLevelProtection;
                if (!Fusion::CanAttack(a, b, victimStarted))
                    continue;
            }
            Body b{u, {}, nullptr, {}};
            if (Player* tp = u->ToPlayer(); tp && tp->HasFusion())
                b.history = &tp->GetFusionConst().poses;   // rewound to where the shooter saw it
            else
            {
                Fusion::BodyPose pose;
                pose.feet = V(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ());
                pose.orientation = u->GetOrientation();
                b.single.Record(shotTime, pose);            // creatures: their current place
                if (!u->IsPlayer())
                {
                    Creature const* c = u->ToCreature();
                    float const lift = c ? Fusion::BodyLift(c->GetCreatureInfo()->pet_family, c->CanFly(),
                        c->IsHovering() || c->IsLevitating()) : 0.f;
                    b.shape.height = std::max(1.0f, u->GetCollisionHeight()) + lift;
                    b.shape.radius = std::max(0.45f, u->GetObjectBoundingRadius());
                    b.shape.headRadius = std::max(0.16f, b.shape.radius * 0.35f);
                    // A killstreak's craft: the body of a gryphon and its rider, a flying machine,
                    // a cannon, drawn larger than the model (Sky::CraftHitbox); no head to shoot.
                    if (std::optional<Fusion::Sky::Craft> const craft = c ? Fusion::Sky::CraftOfEntry(c->GetEntry()) : std::nullopt)
                    {
                        Fusion::Sky::Hitbox const box = Fusion::Sky::CraftHitbox(*craft, c->GetObjectScale());
                        b.shape.height = box.height;
                        b.shape.radius = box.radius;
                        b.shape.headRadius = 0.01f;
                    }
                    if (c && c->m_fusionPoses && c->m_fusionPoses->Size() > 1)
                        b.rewind = c->m_fusionPoses.get();
                }
            }
            bodies.push_back(std::move(b));
        }
        for (Body& b : bodies)
            if (!b.history)
                b.history = &b.single;

        Fusion::ShotContext ctx;
        ctx.weapon = w;
        ctx.shooterPosition = V(shooter->GetPositionX(), shooter->GetPositionY(),
            shooter->GetPositionZ() + EyeHeight(d.stance) + RiderEyeRise(shooter));
        ctx.originTolerance = ShotOriginTolerance(shooter);
        ctx.serverTimeMs = now;
        Map const* map = shooter->GetMap();
        auto clear = [map](Fusion::Vec3 a, Fusion::Vec3 b) {
            return map->isInLineOfSight(a.x, a.y, a.z, b.x, b.y, b.z, true, true);
        };
        // A wall's thickness along a shot: from the first hit going in to the first coming back.
        auto wallThickness = [map](Fusion::Vec3 a, Fusion::Vec3 b) {
            VMAP::IVMapManager* vmaps = VMAP::VMapFactory::createOrGetVMapManager();
            float ix, iy, iz, ox, oy, oz;
            if (!vmaps->getObjectHitPos(map->GetId(), a.x, a.y, a.z, b.x, b.y, b.z, ix, iy, iz, 0.f)
                || !vmaps->getObjectHitPos(map->GetId(), b.x, b.y, b.z, a.x, a.y, a.z, ox, oy, oz, 0.f))
                return 1e9f;
            return (V(ix, iy, iz) - V(ox, oy, oz)).Length();
        };
        ctx.lineOfSight = [clear, wallThickness, w](Fusion::Vec3 a, Fusion::Vec3 b) {
            return clear(a, b) || Fusion::ThroughWall(*w, wallThickness(a, b));
        };

        // One hit: MW2's damage for the weapon at the range and spot, as health, with the events.
        auto deal = [&](Unit* target, Fusion::WeaponProfile const* weapon, float distance, Fusion::HitLocation location, float scale)
        {
            // A killstreak's craft has no head or limbs to pick out: every round is a body shot.
            if (Creature const* tc = target->ToCreature(); tc && Fusion::Sky::CraftOfEntry(tc->GetEntry()))
                location = Fusion::HitLocation::TorsoUpper;
            Fusion::HitInput in;
            in.weapon = weapon;
            in.distance = distance;
            in.location = location;
            in.ammo = round;
            in.attacker = StatsOf(shooter);
            in.critRoll = location != Fusion::HitLocation::Head && WeakSpotRoll(shooter, target);
            float const health = HealthDamage(shooter, target, in) * Fusion::BulletDamageMultiplier(d.perks, in.ammo) * scale
                * RoundDamageScale(target, enchanted);

            uint32 const damage = std::max<uint32>(1, uint32(health + 0.5f));
            bool const head = location == Fusion::HitLocation::Head || in.critRoll;
            uint64 const targetGuid = target->GetObjectGuid().GetRawValue();
            FusionHitSource source;
            source.weaponId = weapon->id;
            source.headshot = head;
            source.explosive = round == Fusion::AmmoType::Explosive;
            Hit(shooter, target, damage, source);
            GunProcs(shooter, target, *weapon, shotTime);
            RoundHit(shooter, target, enchanted, damage, weapon->id);
            if (target->IsAlive())
                if (Creature* c = target->ToCreature())
                    RallyAllies(c, shooter);
            bool const fatal = !target->IsAlive();

            uint8 flags = (head ? Fusion::Packets::EVENT_HEADSHOT : 0) | (fatal ? Fusion::Packets::EVENT_FATAL : 0);
            shooterEvents.events.push_back({Fusion::Packets::EventKind::Hit, flags, targetGuid, damage, w->id});
            if (fatal)
                shooterEvents.events.push_back({Fusion::Packets::EventKind::Kill, flags, targetGuid, 0, w->id});
            if (Player* tp = target->ToPlayer())
            {
                Fusion::Packets::Events hurt;
                hurt.events.push_back({Fusion::Packets::EventKind::Hurt, flags, shooter->GetObjectGuid().GetRawValue(), damage, w->id});
                SendEvents(tp, hurt);
            }
            if (shooterEvents.events.size() + 2 > Fusion::Packets::kMaxEventsPerPacket)
            {
                SendEvents(shooter, shooterEvents);
                shooterEvents.events.clear();
            }
        };

        // A launcher's round bursts where it lands; everything within the blast's radius in sight
        // of it is hurt.
        if (Fusion::IsExplosive(*w))
        {
            Fusion::ShotClaim claim;
            claim.shooterTimeMs = shotTime;
            claim.origin = origin;
            claim.direction = Fusion::PelletDirection(aim, shot.seed, 0, spread);
            Fusion::Vec3 at;
            uint16 flight;
            if (Fusion::Launch::IsGrenadeLauncher(*w))
            {
                // A 40 mm grenade is lobbed: it falls under gravity, so one fired level drops
                // into the ground ahead rather than bursting in mid air. Each small step of the
                // arc is tested against the bodies, built pieces, buildings and terrain, and the
                // nearest contact on that step ends the flight.
                auto contact = [&](Fusion::Vec3 a, Fusion::Vec3 b, float& fraction) {
                    float const len = (b - a).Length();
                    if (len < 1e-4f)
                        return false;
                    Fusion::Vec3 const dir = (b - a) * (1.f / len);
                    float best = len;
                    for (Body const& body : bodies)
                    {
                        std::optional<Fusion::BodyPose> const pose = body.history->At(shotTime);
                        if (!pose)
                            continue;
                        if (auto hit = Fusion::RayVsBody(a, dir, *pose, body.shape); hit && hit->distance < best)
                            best = hit->distance;
                    }
                    if (std::optional<float> const world = WorldOnSegment(map, a, b))
                        best = std::min(best, *world);
                    if (best >= len)
                        return false;
                    fraction = best / len;
                    return true;
                };
                Fusion::Launch::ArcEnd const end = Fusion::Launch::FlyArc(origin, claim.direction * Fusion::Launch::kGrenadeSpeed,
                    Fusion::Launch::kGrenadeGravity, Fusion::Launch::kGrenadeStepS, Fusion::Launch::kGrenadeMaxFlightS, 0.2f, contact);
                at = end.at;
                flight = end.flightMs;
            }
            else
            {
                // A rocket flies straight along the aim to the first body on its path, else the
                // first wall, else its reach.
                float impact = std::min(range, Fusion::kMaxShotRange);
                for (Body const& b : bodies)
                {
                    ctx.targetShape = b.shape;
                    Fusion::ShotResult const r = Fusion::ValidateShot(claim, ctx, *b.history);
                    if (r.verdict == Fusion::ShotVerdict::Hit)
                        impact = std::min(impact, r.distance);
                }
                float pieceT = 0.f;
                if (PieceOnRay(map->GetId(), origin, claim.direction, impact, pieceT))
                    impact = std::min(impact, pieceT);
                Fusion::Vec3 const farthest = origin + claim.direction * impact;
                float wx, wy, wz;
                if (VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(map->GetId(), origin.x, origin.y, origin.z,
                        farthest.x, farthest.y, farthest.z, wx, wy, wz, 0.f))
                    impact = std::min(impact, (V(wx, wy, wz) - origin).Length());
                // The ground (the vmaps hold buildings, not the terrain): a round that missed a body
                // went on through the ground and burst out of reach of anything, so it seemed to do
                // nothing. It stops at the first step under the terrain.
                for (float t = 1.f; t < impact; t += 1.f)
                {
                    Fusion::Vec3 const p = origin + claim.direction * t;
                    float const ground = map->GetHeight(p.x, p.y, p.z + 2.f);
                    if (ground > INVALID_HEIGHT && p.z < ground)
                    {
                        impact = t;
                        break;
                    }
                }
                // A little short of the surface, so the blast's sight lines start in the open; it
                // goes off when the round gets there (UpdateCharges, Explode).
                at = origin + claim.direction * std::max(0.f, impact - 0.2f);
                flight = Fusion::Launch::FlightMs(impact, Fusion::Launch::ProjectileSpeed(*w));
            }
            FusionCharge c;
            c.kind = FusionCharge::Kind::Blast;
            c.weaponId = w->id;
            c.pos = at;
            c.radius = w->explosionRadius;
            c.inner = w->explosionInner;
            c.outer = w->explosionOuter;
            c.mapId = map->GetId();
            c.atMs = now + flight;
            d.charges.push_back(c);
            std::vector<uint8_t> const launch = Fusion::Launch::EncodeLaunch(shooter->GetObjectGuid().GetRawValue(), w->id, origin, at, flight);
            TellNear(shooter->GetMap(), origin, [&](Player*) { return launch; });
            continue;
        }

        uint8 const pellets = std::max<uint8>(1, w->pellets);
        for (uint8 i = 0; i < pellets; ++i)
        {
            // This pellet's ray, and the nearest body on it within the pellet's reach.
            Fusion::ShotClaim claim;
            claim.shooterTimeMs = shotTime;
            claim.origin = origin;
            claim.direction = Fusion::PelletDirection(aim, shot.seed, i, spread);
            std::vector<std::pair<Unit*, Fusion::ShotResult>> hits;
            float pieceT = 0.f;
            uint32 const piece = PieceOnRay(map->GetId(), origin, claim.direction, range, pieceT);
            for (size_t k = 0; k < bodies.size(); ++k)
            {
                Body const& b = bodies[k];
                if (!b.unit->IsAlive())
                    continue;
                // A round passing within a few yards of a gunner's chest: it is under fire.
                if (Creature* bc = b.unit->ToCreature(); bc && bc->m_fusionGunner)
                {
                    Fusion::Vec3 const rel = V(bc->GetPositionX(), bc->GetPositionY(), bc->GetPositionZ() + 1.2f) - origin;
                    float const along = rel.Dot(claim.direction);
                    if (along > 0.f && along < range && (rel - claim.direction * along).Length() < 3.f)
                        ShotNear(bc, now);
                }
                ctx.targetShape = b.shape;
                // The head claim is the client's for the unit under its crosshair.
                claim.claimsHead = (shot.flags & Fusion::Packets::SHOT_CLAIMS_HEAD)
                    && b.unit->GetObjectGuid().GetRawValue() == shot.targetGuid;
                Fusion::ShotResult r = Fusion::ValidateShot(claim, ctx, *b.history);
                // A moving creature: the client drew it a little behind where the server has it,
                // so a round that missed it here is tried where it stood at the shot's time and a
                // beat before (kGunfight.creatureRewindMs).
                if (r.verdict == Fusion::ShotVerdict::Miss && b.rewind)
                    for (uint32 back : {0u, Fusion::kGunfight.creatureRewindMs})
                    {
                        Fusion::ShotClaim earlier = claim;
                        earlier.shooterTimeMs = shotTime - back;
                        Fusion::ShotResult const then = Fusion::ValidateShot(earlier, ctx, *b.rewind);
                        if (then.verdict == Fusion::ShotVerdict::Hit)
                        {
                            r = then;
                            break;
                        }
                    }
                if (r.verdict != Fusion::ShotVerdict::Hit || r.distance > range || (piece && r.distance > pieceT))
                    continue;
                hits.push_back({b.unit, r});
            }
            if (piece && hits.empty())
                DamagePiece(shooter->GetMap(), piece, w->damage * (w->fmj ? 1.5f : 1.f), false, PlotActor(shooter));
            // A round that found no body may find a ship's hull (FusionShips.cpp).
            if (hits.empty() && !piece)
                sShipMgr.RoundAt(shooter, origin, claim.direction, range, w->damage * (w->fmj ? 1.5f : 1.f));
            // Nearest first: the first body takes the round, FMJ carries it into the next.
            std::sort(hits.begin(), hits.end(), [](auto const& a, auto const& b) { return a.second.distance < b.second.distance; });
            // Everyone else near sees the round fly (op 34, its first pellet): to the body it hit,
            // else the wall or ground in its way, else as far as it carries.
            if (i == 0)
            {
                float reachT = std::min(range, Fusion::kMaxShotRange);
                if (!hits.empty())
                    reachT = std::min(reachT, hits.front().second.distance);
                else if (std::optional<float> const world = WorldOnSegment(map, origin, origin + claim.direction * reachT))
                    reachT = *world;
                std::vector<uint8_t> const seen = Fusion::EncodePlayerRound(shooter->GetObjectGuid().GetRawValue(), w->id,
                    !hits.empty(), origin, origin + claim.direction * reachT);
                TellNear(shooter->GetMap(), origin, [&](Player* p) { return p == shooter ? std::vector<uint8_t>() : seen; },
                    Fusion::kNpcTellRadius);
            }
            int const pierce = std::min<int>(Fusion::BodiesPierced(*w), int(hits.size()));
            for (int h = 0; h < pierce; ++h)
            {
                Unit* target = hits[size_t(h)].first;
                Fusion::ShotResult const& bestResult = hits[size_t(h)].second;
                if (!target->IsAlive())
                    continue;
                // Through a wall (FMJ only), less of the round arrives.
                Fusion::Vec3 const hitPoint = origin + claim.direction * bestResult.distance;
                float const scale = Fusion::PierceScale(h) * (clear(origin, hitPoint) ? 1.f : Fusion::kFmjWallScale);

                deal(target, w, bestResult.distance, bestResult.location, scale);
            }
        }
    }
    SendEvents(shooter, shooterEvents);
}
