/*
 * World of Modern Warfare: achievements and unlocks (fusion/Achievements.h) in the game. The
 * account's counters and earned achievements load at login; the game's events count into them
 * (kills, deaths, levels, quests, zones, sprinting...); whatever they newly earn is saved,
 * announced and unlocks gear for Create-a-Class at once. See src/fusion/docs/ACHIEVEMENTS.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Formulas.h"
#include "Player.h"
#include "Timer.h"
#include "World.h"
#include "WorldSession.h"
#include "fusion/Equipment.h"

#include <ctime>

namespace
{
    // The unlocks message on the achievements channel (op 5, the client's Create-a-Class greys
    // what is locked): version · 5 · everything u8 · count u16 · count × (kind u8 · id u32 · camo u8
    // · attachment len u8 + bytes).
    constexpr uint8_t kOpUnlocks = 5;

    void PutU16(std::vector<uint8_t>& b, uint16_t v)
    {
        b.push_back(uint8_t(v));
        b.push_back(uint8_t(v >> 8));
    }

    void PutU32(std::vector<uint8_t>& b, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    }

    std::vector<uint8_t> EncodeUnlocks(Fusion::UnlockSet const& u)
    {
        std::vector<uint8_t> b{Fusion::AchievementWire::kVersion, kOpUnlocks, uint8_t(u.IsEverything() ? 1 : 0)};
        PutU16(b, uint16_t(u.Held().size()));
        for (Fusion::Unlock const& x : u.Held())
        {
            b.push_back(uint8_t(x.kind));
            PutU32(b, x.id);
            b.push_back(x.camo);
            b.push_back(uint8_t(std::min<size_t>(x.attachment.size(), 255)));
            b.insert(b.end(), x.attachment.begin(), x.attachment.begin() + std::min<size_t>(x.attachment.size(), 255));
        }
        return b;
    }

    // What a reward is called in a chat line.
    std::string RewardText(Fusion::Catalog const& catalog, Fusion::Unlock const& u)
    {
        auto gunName = [&](uint32_t id) {
            Fusion::WeaponProfile const* w = catalog.Weapons().Find(uint16_t(id));
            return w ? Fusion::GunDisplayName(w->name) : std::string("a gun");
        };
        auto perkName = [&](uint32_t id) {
            Fusion::Perk const* p = catalog.FindPerk(uint16_t(id));
            return p ? Fusion::PerkDisplayName(*p) : std::string("a perk");
        };
        switch (u.kind)
        {
            case Fusion::UnlockKind::Gun:
            case Fusion::UnlockKind::Lethal:
            case Fusion::UnlockKind::Tactical:
                return gunName(u.id);
            case Fusion::UnlockKind::Attachment:
            {
                Fusion::Attachment const* a = catalog.FindAttachment(u.attachment);
                return (a ? Fusion::AttachmentDisplayName(*a) : u.attachment) + " for the " + gunName(u.id);
            }
            case Fusion::UnlockKind::Camo:
            {
                Fusion::Camo const* c = catalog.FindCamo(u.camo);
                return (c ? Fusion::CamoDisplayName(*c) : std::string("a camo")) + " camo for the " + gunName(u.id);
            }
            case Fusion::UnlockKind::Perk:
            case Fusion::UnlockKind::Deathstreak:
                return perkName(u.id);
            case Fusion::UnlockKind::Killstreak:
                return Fusion::StreakName(u.id);
            case Fusion::UnlockKind::Title:
                return "the title " + Fusion::TitleName(u.id);
            case Fusion::UnlockKind::PerkPro:
                return perkName(u.id) + " Pro";
        }
        return "something";
    }

    // 1.12's creature type and rank as the counters key them.
    Fusion::CreatureType TypeOf(Creature const* c)
    {
        uint32 const t = c->GetCreatureType();
        return t <= 10 ? Fusion::CreatureType(t) : Fusion::CreatureType::NotSpecified;
    }

    Fusion::CreatureRank RankOf(Creature const* c)
    {
        CreatureInfo const* ci = c->GetCreatureInfo();
        uint32 const rank = ci ? ci->rank : 0;
        Fusion::CreatureRank r = rank <= 4 ? Fusion::CreatureRank(rank) : Fusion::CreatureRank::Normal;
        Map const* map = c->FindMap();
        if (map && map->IsDungeon() && (rank == CREATURE_ELITE_ELITE || rank == CREATURE_ELITE_RAREELITE)
            && c->GetScriptName().rfind("boss_", 0) == 0)
            r = Fusion::CreatureRank::DungeonBoss;
        return r;
    }
}

FusionHitSource& FusionMgr::CurrentHit()
{
    static thread_local FusionHitSource source;
    return source;
}

void FusionMgr::LoadAchievements(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const account = player->GetSession()->GetAccountId();
    LoadPrestige(player);   // its camos join the unlocks (FusionPrestige.cpp)
    d.stats = Fusion::AccountStats();
    d.earned.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `stat`, `key`, `value` FROM `account_fusion_stats` WHERE `account` = %u", account))
    {
        do
        {
            Field* f = r->Fetch();
            d.stats.Load({Fusion::Stat(f[0].GetUInt16()), f[1].GetUInt32()}, f[2].GetUInt32());
        } while (r->NextRow());
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `achievement` FROM `account_fusion_achievement` WHERE `account` = %u", account))
    {
        do
            d.earned.insert(r->Fetch()[0].GetUInt32());
        while (r->NextRow());
    }
    d.stats.TakeDirty();
    // The level this character reached counts at once (an account made before achievements).
    d.stats.Record({Fusion::Stat::Level, Fusion::kAnyKey}, player->GetLevel());
    // And its professions' skills (characters from before these achievements).
    for (uint16 line : {171, 164, 333, 202, 182, 165, 186, 393, 197, 185, 129, 356})
        if (player->HasSkill(line))
            d.stats.Record({Fusion::Stat::ProfessionSkill, line}, player->GetSkillValueBase(line));
    if (d.constructionTier != Fusion::Construction::Tier::None)
        d.stats.Record({Fusion::Stat::ProfessionSkill, Fusion::Construction::kSkillConstruction}, d.constructionSkill);
    d.unlocks = UnlocksOf(player);
    d.achievementsLoaded = true;
}

void FusionMgr::CountStat(Player* player, Fusion::StatKey key, uint32 n)
{
    if (!m_enabled || !player->HasFusion() || !player->GetFusion().achievementsLoaded)
        return;
    player->GetFusion().stats.Add(key, n);
    EvaluateAchievements(player);
}

void FusionMgr::RecordStat(Player* player, Fusion::StatKey key, uint32 value)
{
    if (!m_enabled || !player->HasFusion() || !player->GetFusion().achievementsLoaded)
        return;
    player->GetFusion().stats.Record(key, value);
    EvaluateAchievements(player);
}

void FusionMgr::CountKill(Player* killer, Unit* victim)
{
    if (!m_enabled || !killer->HasFusion())
        return;
    FusionPlayerData& d = killer->GetFusion();
    if (!d.achievementsLoaded)
        return;
    FusionHitSource const& hit = CurrentHit();
    Fusion::KillEvent e;
    if (Player* p = victim->ToPlayer())
    {
        e.victimIsPlayer = true;
        e.revenge = d.lastKiller == p->GetObjectGuid().GetRawValue();
        if (e.revenge)
            d.lastKiller = 0;
        ++d.playerKillRun;
        d.stats.Raise({Fusion::Stat::BestPlayerStreak, Fusion::kAnyKey}, d.playerKillRun);
    }
    else if (Creature* c = victim->ToCreature())
    {
        e.creatureType = TypeOf(c);
        e.rank = RankOf(c);
        // The achievements' bosses (any weapon, any means) and the wanderers.
        if (Fusion::IsDungeonBoss(c->GetEntry()))
            d.stats.Add({Fusion::Stat::DungeonBossKills, c->GetEntry()});
        if (c->m_wanderer)
            d.stats.Add({Fusion::Stat::WandererKills, Fusion::kAnyKey});
        e.gray = c->GetCreatureType() == CREATURE_TYPE_CRITTER
            || MaNGOS::XP::GetColorCode(killer->GetLevel(), c->GetLevel()) == MaNGOS::XP::GRAY;
    }
    else
        return;
    // The weapon: the fusion hit that killed, else the gun in hand for a kill by other means
    // (a WoW spell or swing counts as no gun).
    e.weaponId = hit.weaponId;
    if (Fusion::WeaponProfile const* w = e.weaponId ? m_catalog.Weapons().Find(e.weaponId) : nullptr)
    {
        e.weaponClass = w->weaponClass;
        if (w->slot == Fusion::WeaponSlot::Lethal)
            e.equipment = Fusion::KindOf(w->name);
    }
    if (hit.equipment != Fusion::EquipmentKind::None)
        e.equipment = hit.equipment;
    if (e.weaponId >= 3000 && e.weaponId < 4000)
    {
        e.streakId = e.weaponId - 3000u;   // a killstreak reward's blast or guns
        e.explosive = true;
    }
    e.headshot = hit.headshot;
    e.melee = hit.melee;
    e.explosive = e.explosive || hit.explosive;
    if (Fusion::ClassLoadout const* l = d.Equipped())
    {
        auto add = [&](std::string const& a) { if (!a.empty()) e.attachments.push_back(a); };
        if (e.weaponId && e.weaponId == l->primary)
        {
            add(l->primaryAtt1);
            add(l->primaryAtt2);
            add(l->primaryAtt3);
            add(l->primaryAtt4);
        }
        else if (e.weaponId && e.weaponId == l->secondary)
        {
            add(l->secondaryAtt);
            add(l->secondaryAtt2);
        }
        for (uint16 p : {l->perk1, l->perk2, l->perk3, l->deathstreak})
            if (p)
                e.perks.push_back(p);
    }
    e.distance = killer->GetDistance(victim);
    e.stance = d.stance;
    uint32 const now = WorldTimer::getMSTime();
    d.burstKills = WorldTimer::getMSTimeDiff(d.burstStartMs, now) <= 2000 ? d.burstKills + 1 : 1;
    if (d.burstKills == 1)
        d.burstStartMs = now;
    e.burst = d.burstKills;
    Fusion::RecordKill(d.stats, e);
    EvaluateAchievements(killer);
}

namespace
{
    // The stock professions the achievements follow (SkillLine.dbc).
    bool IsProfessionLine(uint32 line)
    {
        switch (line)
        {
            case 171: case 164: case 333: case 202: case 182: case 165: case 186: case 393: case 197:
            case 185: case 129: case 356:
                return true;
            default:
                return false;
        }
    }
}

void FusionMgr::NoteSkill(Player* player, uint16 skillLine, uint16 value)
{
    if (IsProfessionLine(skillLine))
        RecordStat(player, {Fusion::Stat::ProfessionSkill, skillLine}, value);
}

void FusionMgr::CountCraft(Player* player, uint32 skillLine)
{
    if (!IsProfessionLine(skillLine) || !m_enabled || !player->HasFusion() || !player->GetFusion().achievementsLoaded)
        return;
    FusionPlayerData& d = player->GetFusion();
    d.stats.Add({Fusion::Stat::ItemsCrafted, Fusion::kAnyKey});
    d.stats.Add({Fusion::Stat::ItemsCrafted, skillLine});
    EvaluateAchievements(player);
}

void FusionMgr::CountHolidayQuest(Player* player, uint16 eventId)
{
    if (!m_enabled || !player->HasFusion() || !player->GetFusion().achievementsLoaded)
        return;
    FusionPlayerData& d = player->GetFusion();
    d.stats.Add({Fusion::Stat::HolidayQuests, Fusion::kAnyKey});
    d.stats.Add({Fusion::Stat::HolidayQuests, eventId});
    uint32 events = 0;
    for (auto const& [k, v] : d.stats.All())
        if (k.stat == Fusion::Stat::HolidayQuests && k.key != Fusion::kAnyKey && v > 0)
            ++events;
    d.stats.Raise({Fusion::Stat::HolidaysCelebrated, Fusion::kAnyKey}, events);
    EvaluateAchievements(player);
}

void FusionMgr::NoteReputation(Player* player, uint32 factionId, uint8 rank, uint32 exalted)
{
    if (!m_enabled || !player->HasFusion() || !player->GetFusion().achievementsLoaded)
        return;
    FusionPlayerData& d = player->GetFusion();
    d.stats.Raise({Fusion::Stat::FactionRank, factionId}, rank);
    d.stats.Raise({Fusion::Stat::ExaltedFactions, Fusion::kAnyKey}, exalted);
    EvaluateAchievements(player);
}

void FusionMgr::CountDeath(Player* victim, Player* killer)
{
    if (!m_enabled || !victim->HasFusion())
        return;
    FusionPlayerData& d = victim->GetFusion();
    d.playerKillRun = 0;
    if (!d.achievementsLoaded)
        return;
    d.stats.Add({Fusion::Stat::Deaths, Fusion::kAnyKey});
    if (killer && killer != victim)
    {
        d.stats.Add({Fusion::Stat::PlayerDeaths, Fusion::kAnyKey});
        d.lastKiller = killer->GetObjectGuid().GetRawValue();
    }
    EvaluateAchievements(victim);
}

void FusionMgr::EvaluateAchievements(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const account = player->GetSession()->GetAccountId();
    std::vector<Fusion::StatKey> const changed = d.stats.TakeDirty();
    if (changed.empty())
        return;
    // The counters as they stand (a handful of rows a kill).
    for (Fusion::StatKey const& k : changed)
        CharacterDatabase.PExecute(
            "INSERT INTO `account_fusion_stats` (`account`, `stat`, `key`, `value`) VALUES (%u, %u, %u, %u) "
            "ON DUPLICATE KEY UPDATE `value` = VALUES(`value`)",
            account, uint32(k.stat), k.key, d.stats.Get(k));
    std::vector<uint32_t> const fresh = Fusion::Evaluate(m_book, d.stats, d.earned);
    for (uint32_t id : fresh)
    {
        d.earned.insert(id);
        CharacterDatabase.PExecute(
            "INSERT IGNORE INTO `account_fusion_achievement` (`account`, `achievement`, `earned_at`, `character`) "
            "VALUES (%u, %u, %u, %u)",
            account, id, uint32(time(nullptr)), player->GetGUIDLow());
        Fusion::Achievement const* a = m_book.Find(id);
        if (!a)
            continue;
        std::string line = "|cffffd200Achievement earned:|r " + a->name;
        for (size_t i = 0; i < a->rewards.size(); ++i)
            line += (i == 0 ? " — unlocks " : ", ") + RewardText(m_catalog, a->rewards[i]);
        ChatHandler(player).SendSysMessage(line.c_str());
    }
    if (!fresh.empty())
    {
        d.unlocks = UnlocksOf(player);
        SendChannel(player, Fusion::Packets::kChannelAchievements, Fusion::AchievementWire::EncodeEarned(fresh));
        SendUnlocks(player);
        // A new killstreak reward joins the ladder at once.
        d.streak.SetLoadout(Fusion::KitStreaks(d.Equipped(), Fusion::DefaultStreakLadder(), d.unlocks), d.perks.hardline);
    }
    // A newly earned achievement moves its meta-achievements and the points tiers too.
    std::vector<Fusion::StatKey> touched = changed;
    for (Fusion::StatKey const& k : Fusion::AchievementWire::ChangedByEarning(fresh))
        touched.push_back(k);
    std::vector<Fusion::AchievementWire::ProgressEntry> const progress =
        Fusion::AchievementWire::Touched(m_book, d.stats, d.earned, touched);
    if (!progress.empty())
        SendChannel(player, Fusion::Packets::kChannelAchievements, Fusion::AchievementWire::EncodeProgress(progress));
}

void FusionMgr::SendAchievementPages(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    for (std::vector<uint8_t> const& body :
         Fusion::AchievementWire::EncodePages(Fusion::AchievementWire::Views(m_book, d.stats, d.earned)))
        SendChannel(player, Fusion::Packets::kChannelAchievements, body);
}

void FusionMgr::SendUnlocks(Player* player)
{
    SendChannel(player, Fusion::Packets::kChannelAchievements, EncodeUnlocks(player->GetFusion().unlocks));
}

void FusionMgr::HandleAchievementChannel(Player* player, std::vector<uint8> const& body)
{
    Fusion::AchievementWire::Op op;
    if (!player->GetFusion().achievementsLoaded || !Fusion::AchievementWire::PeekOp(body, op))
        return;
    if (op == Fusion::AchievementWire::Op::Request)
    {
        SendAchievementPages(player);
        SendUnlocks(player);
        // The Journal's record: the account's kills, deaths and best streak.
        SendChannel(player, Fusion::Packets::kChannelAchievements,
            Fusion::AchievementWire::EncodeRecord(player->GetFusion().stats));
    }
}

void FusionMgr::GrantAchievement(Player* player, uint32 id)
{
    FusionPlayerData& d = player->GetFusion();
    if (!m_book.Find(id) || d.earned.count(id))
        return;
    d.earned.insert(id);
    CharacterDatabase.PExecute(
        "INSERT IGNORE INTO `account_fusion_achievement` (`account`, `achievement`, `earned_at`, `character`) "
        "VALUES (%u, %u, %u, %u)",
        player->GetSession()->GetAccountId(), id, uint32(time(nullptr)), player->GetGUIDLow());
    d.unlocks = UnlocksOf(player);
    SendChannel(player, Fusion::Packets::kChannelAchievements, Fusion::AchievementWire::EncodeEarned({id}));
    SendUnlocks(player);
}

void FusionMgr::ResetAchievements(Player* player)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const account = player->GetSession()->GetAccountId();
    CharacterDatabase.PExecute("DELETE FROM `account_fusion_stats` WHERE `account` = %u", account);
    CharacterDatabase.PExecute("DELETE FROM `account_fusion_achievement` WHERE `account` = %u", account);
    d.stats = Fusion::AccountStats();
    d.earned.clear();
    d.unlocks = UnlocksOf(player);
    SendUnlocks(player);
}

// .fusion ach [grant <id> | reset | count]: an admin's achievement tools for testing. With no
// argument it reports the account's progress.
bool ChatHandler::HandleFusionAchCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player) || !player->HasFusion())
    {
        SendSysMessage("Fusion: only an admin may change achievements.");
        SetSentErrorMessage(true);
        return false;
    }
    std::string const arg = args ? std::string(args) : std::string();
    FusionPlayerData& d = player->GetFusion();
    if (arg.rfind("grant ", 0) == 0)
    {
        uint32 const id = uint32(std::strtoul(arg.c_str() + 6, nullptr, 10));
        if (!sFusionMgr.Book().Find(id))
        {
            PSendSysMessage("Fusion: no achievement %u.", id);
            return false;
        }
        sFusionMgr.GrantAchievement(player, id);
        PSendSysMessage("Fusion: granted %u.", id);
        return true;
    }
    if (arg == "reset")
    {
        sFusionMgr.ResetAchievements(player);
        SendSysMessage("Fusion: this account's achievements and counters are cleared.");
        return true;
    }
    PSendSysMessage("Fusion: %u of %u achievements earned; %u counters.", uint32(d.earned.size()),
        uint32(sFusionMgr.Book().All().size()), uint32(d.stats.All().size()));
    return true;
}
