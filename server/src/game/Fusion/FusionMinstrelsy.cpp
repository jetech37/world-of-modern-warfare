/*
 * World of Modern Warfare: Minstrelsy in the game (FusionMinstrelsy.h, fusion/Minstrelsy.h).
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMinstrelsy.h"
#include "FusionMgr.h"

#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "Util.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>

namespace
{
    namespace M = Fusion::Minstrelsy;
    namespace AS = Fusion::ActionSystems;
    using Fusion::Gathering::Outcome;

    constexpr uint32 kFireFocus = 4;                // a campfire or cooking fire (SpellFocusObject 4)
    constexpr float kTrainerReach = 12.f;
    constexpr float kCrowdRange = 15.f;
    constexpr float kTipRange = 40.f;

    uint32 NowMs() { return WorldTimer::getMSTime(); }
    int64 NowSecs() { return int64(time(nullptr)); }

    FusionMinstrelData& D(Player* p) { return p->GetFusion().minstrel; }
    FusionMinstrelData const& D(Player const* p) { return p->GetFusionConst().minstrel; }

    float Dist(Player const* a, WorldObject const* b) { return a->GetDistance(b); }

    bool Resting(Player const* p) { return p->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING); }

    bool NearFire(Player* p)
    {
        GameObject* found = nullptr;
        MaNGOS::GameObjectFocusCheck check(p, kFireFocus);
        MaNGOS::GameObjectSearcher<MaNGOS::GameObjectFocusCheck> searcher(found, check);
        Cell::VisitGridObjects(p, searcher, M::kFireRange);
        return found != nullptr && p->IsWithinDistInMap(found, M::kFireRange);
    }

    bool Held(Player const* p)
    {
        for (AuraType a : {SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_SILENCE, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE,
                 SPELL_AURA_MOD_PACIFY_SILENCE})
            if (p->HasAuraType(a))
                return true;
        return false;
    }

    // The fusion players on the performer's map within `range` (the performer too): the party only,
    // or everyone not hostile to it.
    std::vector<Player*> Listeners(Player* p, float range, bool partyOnly)
    {
        std::vector<Player*> out;
        Map* map = p->GetMap();
        if (!map)
            return out;
        Group* g = p->GetGroup();
        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
        {
            Player* m = i->getSource();
            if (!m || !m->IsInWorld() || !m->IsAlive() || !m->HasFusion() || Dist(p, m) > range)
                continue;
            if (m != p && partyOnly && !(g && g->IsMember(m->GetObjectGuid())))
                continue;
            if (m != p && !partyOnly && p->IsHostileTo(m))
                continue;
            out.push_back(m);
        }
        return out;
    }

    // The crowd at an inn: the friendly people near (not pets, not players) and the wanderers.
    uint32 CrowdNear(Player* p)
    {
        std::list<Unit*> units;
        MaNGOS::AnyFriendlyUnitInObjectRangeCheck check(p, kCrowdRange);
        MaNGOS::UnitListSearcher<MaNGOS::AnyFriendlyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(p, searcher, kCrowdRange);
        uint32 n = 0;
        for (Unit* u : units)
        {
            Creature* c = u->ToCreature();
            if (!c || c->IsPet() || c->IsInCombat())
                continue;
            if (c->m_wanderer || c->GetCreatureInfo()->type == CREATURE_TYPE_HUMANOID)
                ++n;
        }
        return n;
    }

    std::string Money(uint32 copper) { return Fusion::Homestead::MoneyText(copper); }

    Fusion::Homestead::Hue HueOf(M::Color c)
    {
        switch (c)
        {
            case M::Color::Orange: return Fusion::Homestead::Hue::Orange;
            case M::Color::Yellow: return Fusion::Homestead::Hue::Yellow;
            case M::Color::Green: return Fusion::Homestead::Hue::Green;
            case M::Color::Gray: return Fusion::Homestead::Hue::Gray;
            default: return Fusion::Homestead::Hue::Red;
        }
    }

    Outcome OutcomeOf(M::Refusal r)
    {
        switch (r)
        {
            case M::Refusal::Ok: return Outcome::Performing;
            case M::Refusal::NotLearned: return Outcome::NotLearned;
            case M::Refusal::SkillTooLow: return Outcome::SkillTooLow;
            case M::Refusal::WrongInstrument: return Outcome::NoSong;
            case M::Refusal::InCombat: return Outcome::InCombat;
            case M::Refusal::NotAtInn: return Outcome::NotAtInn;
            case M::Refusal::NotAtFire: return Outcome::NotAtFire;
            case M::Refusal::Cooldown: return Outcome::Cooldown;
        }
        return Outcome::Failed;
    }

    char const* RefusalText(M::Refusal r)
    {
        switch (r)
        {
            case M::Refusal::NotLearned: return "Requires Minstrelsy (a Minstrel Trainer teaches it).";
            case M::Refusal::SkillTooLow: return "Your Minstrelsy is too low for that song.";
            case M::Refusal::WrongInstrument: return "That instrument has no such song.";
            case M::Refusal::InCombat: return "Only a combat song can be played in combat.";
            case M::Refusal::NotAtInn: return "An inn song is played where travellers rest: an inn or a city.";
            case M::Refusal::NotAtFire: return "A fire song is played by a campfire.";
            case M::Refusal::Cooldown: return "That song was played moments ago.";
            default: return nullptr;
        }
    }

    char const* EffectText(M::Effect e)
    {
        switch (e)
        {
            case M::Effect::Travel: return "run faster";
            case M::Effect::Rested: return "are rested";
            case M::Effect::Campfire: return "regenerate by the fire";
            case M::Effect::Ward: return "take less damage";
            case M::Effect::Rally: return "move faster";
            case M::Effect::Mend: return "regenerate";
        }
        return "";
    }
}

MinstrelMgr& MinstrelMgr::Instance()
{
    static MinstrelMgr mgr;
    return mgr;
}

void MinstrelMgr::Load()
{
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_skill` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`skill` smallint(5) unsigned NOT NULL COMMENT '9001 Construction, 9002 Minstrelsy',"
        "`tier` tinyint(3) unsigned NOT NULL DEFAULT '0',"
        "`value` smallint(5) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`guid`, `skill`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_minstrel` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`day` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'unix days',"
        "`given` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'tips given that day, copper',"
        "`got` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'tips from players that day',"
        "`crowd` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'the crowd''s tips that day',"
        "PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `character_fusion_minstrel_recipe` ("
        "`guid` int(10) unsigned NOT NULL COMMENT 'character guid',"
        "`recipe` smallint(5) unsigned NOT NULL COMMENT 'Minstrelsy::Recipes id',"
        "PRIMARY KEY (`guid`, `recipe`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: Minstrelsy: %u instruments, %u songs.",
        uint32(M::Instruments().size()), uint32(M::Songs().size()));
}

void MinstrelMgr::LoadPlayer(Player* player)
{
    FusionMinstrelData& d = D(player);
    d = FusionMinstrelData{};
    d.loaded = true;
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `tier`, `value` FROM `character_fusion_skill` WHERE `guid` = %u AND `skill` = %u",
            player->GetGUIDLow(), uint32(M::kSkillMinstrelsy)))
    {
        Field* f = r->Fetch();
        uint8 const tier = f[0].GetUInt8();
        if (tier <= uint8(M::Tier::Artisan))
        {
            d.tier = M::Tier(tier);
            d.skill = std::min<uint16>(f[1].GetUInt16(), M::SkillCap(d.tier));
        }
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `day`, `given`, `got`, `crowd` FROM `character_fusion_minstrel` WHERE `guid` = %u", player->GetGUIDLow()))
    {
        Field* f = r->Fetch();
        d.day = f[0].GetUInt32();
        d.given = f[1].GetUInt32();
        d.got = f[2].GetUInt32();
        d.crowd = f[3].GetUInt32();
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
            "SELECT `recipe` FROM `character_fusion_minstrel_recipe` WHERE `guid` = %u", player->GetGUIDLow()))
    {
        do
            d.recipes.push_back(r->Fetch()[0].GetUInt16());
        while (r->NextRow());
    }
    RollDay(player);
    if (d.tier != M::Tier::None)
        sFusionMgr.RecordStat(player, {Fusion::Stat::ProfessionSkill, M::kSkillMinstrelsy}, d.skill);
}

void MinstrelMgr::RollDay(Player* player)
{
    FusionMinstrelData& d = D(player);
    uint32 const today = M::DayOf(NowSecs());
    if (d.day != today)
    {
        d.day = today;
        d.given = d.got = d.crowd = 0;
    }
}

void MinstrelMgr::SaveDay(Player* player)
{
    FusionMinstrelData const& d = D(player);
    CharacterDatabase.PExecute("REPLACE INTO `character_fusion_minstrel` (`guid`, `day`, `given`, `got`, `crowd`) VALUES (%u, %u, %u, %u, %u)",
        player->GetGUIDLow(), d.day, d.given, d.got, d.crowd);
}

void MinstrelMgr::SetSkill(Player* player, M::Tier tier, uint16 skill, bool announce)
{
    FusionMinstrelData& d = D(player);
    skill = std::min<uint16>(skill, M::SkillCap(tier));
    bool const up = announce && skill > d.skill;
    d.tier = tier;
    d.skill = skill;
    CharacterDatabase.PExecute("REPLACE INTO `character_fusion_skill` (`guid`, `skill`, `tier`, `value`) VALUES (%u, %u, %u, %u)",
        player->GetGUIDLow(), uint32(M::kSkillMinstrelsy), uint32(tier), uint32(skill));
    sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeSkillOf(tier, skill));
    sFusionMgr.RecordStat(player, {Fusion::Stat::ProfessionSkill, M::kSkillMinstrelsy}, skill);
    if (up)
        ChatHandler(player).PSendSysMessage("Your skill in Minstrelsy has increased to %u.", uint32(skill));
}

uint16 MinstrelMgr::GmSkillOf(Player* player)
{
    if (!player->HasFusion())
        return 0;
    FusionMinstrelData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    return d.tier == M::Tier::None ? 0 : d.skill;
}

void MinstrelMgr::GmSetSkill(Player* player, uint16 skill)
{
    if (!player->HasFusion())
        return;
    if (!D(player).loaded)
        LoadPlayer(player);
    skill = std::max<uint16>(1, std::min<uint16>(skill, M::SkillCap(M::Tier::Artisan)));
    M::Tier tier = M::Tier::Apprentice;
    while (M::SkillCap(tier) < skill && tier != M::Tier::Artisan)
        tier = Fusion::Construction::Next(tier);
    SetSkill(player, tier, skill, false);
}

uint32 MinstrelMgr::GmLearnAllRecipes(Player* player)
{
    if (!player->HasFusion())
        return 0;
    FusionMinstrelData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    uint32 added = 0;
    for (M::Recipe const& r : M::Recipes())
        if (std::find(d.recipes.begin(), d.recipes.end(), r.id) == d.recipes.end())
        {
            d.recipes.push_back(r.id);
            CharacterDatabase.PExecute("REPLACE INTO `character_fusion_minstrel_recipe` (`guid`, `recipe`) VALUES (%u, %u)",
                player->GetGUIDLow(), uint32(r.id));
            ++added;
        }
    sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeBook(M::Book(d.recipes)));
    return added;
}

bool MinstrelMgr::Knows(Player const* player, uint32 tool) const
{
    M::InstrumentDef const* i = M::DefOfTool(tool);
    if (!i || !player->HasFusion())
        return false;
    FusionMinstrelData const& d = D(player);
    return d.tier != M::Tier::None && d.skill >= i->skill;
}

float MinstrelMgr::SpeedFactor(Player const* player) const
{
    if (!player->HasFusion())
        return 1.f;
    return M::SpeedFactor(D(player).buffs, NowMs(), player->IsInCombat());
}

float MinstrelMgr::DamageTakenFactor(Player const* player) const
{
    if (!player->HasFusion())
        return 1.f;
    return M::DamageTakenFactor(D(player).buffs, NowMs());
}

// ── Every update ──────────────────────────────────────────────────────────────────────────────

void MinstrelMgr::UpdatePlayer(Player* player, uint32 /*diffMs*/)
{
    if (!sFusionMgr.IsEnabled() || !player->HasFusion() || !player->IsInWorld() || !player->GetSession())
        return;
    FusionMinstrelData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    if (!d.sent)
    {
        d.sent = true;
        sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeSkillOf(d.tier, d.skill));
        // The recipe book the trade window shows (the client's TradeSkillFrame).
        sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeBook(M::Book(d.recipes)));
    }
    uint32 const now = NowMs();
    if (d.playing.perf)
        TickPerformance(player, now);
    if (int32(now - d.nextTickMs) >= 0)
    {
        d.nextTickMs = now + 1000;
        TickBuffs(player, now);
    }
}

void MinstrelMgr::TickBuffs(Player* player, uint32 now)
{
    FusionMinstrelData& d = D(player);
    size_t const had = d.buffs.size();
    M::Expire(d.buffs, now);
    if (d.buffs.size() != had)
        d.buffsDirty = true;
    // Death ends what a song gave.
    if (!player->IsAlive() && !d.buffs.empty())
    {
        d.buffs.erase(std::remove_if(d.buffs.begin(), d.buffs.end(), [](M::Buff const& b) {
            M::SongDef const* s = M::Song(b.song);
            return !s || s->effect != M::Effect::Rested;
        }), d.buffs.end());
        d.buffsDirty = true;
    }
    bool const fight = player->IsInCombat();
    if (player->IsAlive())
        if (float const pct = M::RegenPerSecond(d.buffs, now, fight); pct > 0.f)
        {
            if (player->GetHealth() < player->GetMaxHealth())
                player->ModifyHealth(int32(std::max<uint32>(1, uint32(player->GetMaxHealth() * pct / 100.f))));
            if (player->GetPowerType() == POWER_MANA && player->GetPower(POWER_MANA) < player->GetMaxPower(POWER_MANA))
                player->ModifyPower(POWER_MANA, int32(std::max<uint32>(1, uint32(player->GetMaxPower(POWER_MANA) * pct / 100.f))));
        }
    float const speed = M::SpeedFactor(d.buffs, now, fight);
    if (std::fabs(speed - d.speed) > 1e-4f)
    {
        d.speed = speed;
        player->UpdateSpeed(MOVE_RUN, false);
        player->UpdateSpeed(MOVE_RUN_BACK, false);
    }
    d.heard.erase(std::remove_if(d.heard.begin(), d.heard.end(), [&](FusionMinstrelHeard const& h) {
        return now - h.atMs > M::kHeardWindowMs;
    }), d.heard.end());
    if (d.buffsDirty)
    {
        d.buffsDirty = false;
        sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeBuffs(d.buffs, now));
    }
}

// ── Performing ────────────────────────────────────────────────────────────────────────────────

uint8 MinstrelMgr::Perform(Player* player, uint32 tool, uint16 songId)
{
    FusionMinstrelData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    M::InstrumentDef const* inst = M::DefOfTool(tool);
    M::SongDef const* song = M::Song(songId);
    if (!inst || !song)
        return uint8(Outcome::NoSong);
    bool const gm = sFusionMgr.IsAdmin(player);
    uint32 const now = NowMs();
    M::Place place;
    place.resting = Resting(player);
    place.inCombat = player->IsInCombat();
    place.fire = song->effect == M::Effect::Campfire && NearFire(player);
    auto cd = d.combatReadyMs.find(song->id);
    place.coolingDown = cd != d.combatReadyMs.end() && int32(cd->second - now) > 0;
    M::Refusal const r = M::CanPerform(*song, *inst, d.tier, d.skill, place, gm);
    if (r != M::Refusal::Ok)
    {
        if (char const* why = RefusalText(r))
            player->GetSession()->SendNotification("%s", why);
        return uint8(OutcomeOf(r));
    }
    // Played again (or another song begun): the one playing ends as it stands.
    if (d.playing.perf)
        Finish(player, M::RESULT_CUT);
    FusionMinstrelPerformance p;
    p.perf = ++d.lastPerf;
    p.song = song->id;
    p.startMs = now;
    p.chart = M::MakeChart(inst->id, urand(1, 0x7FFFFFFF));
    M::Start start;
    start.perf = p.perf;
    start.song = song->id;
    start.instrument = inst->id;
    start.seed = p.chart.seed;
    d.playing = std::move(p);
    sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeStart(start));
    return uint8(Outcome::Performing);
}

void MinstrelMgr::TickPerformance(Player* player, uint32 now)
{
    FusionMinstrelData& d = D(player);
    FusionMinstrelPerformance const& p = d.playing;
    M::SongDef const* song = M::Song(p.song);
    M::InstrumentDef const* inst = M::Def(p.chart.instrument);
    FusionPlayerData const& fd = player->GetFusionConst();
    // Death, a stun, a silence, a fear; the hands no longer on the instrument; and, for any song
    // but a combat one, a fight: the song stops where it is (a root does not stop the hands).
    bool const cut = !song || !inst || !player->IsAlive() || Held(player)
        || fd.actionSystem != AS::System::Utility || fd.utilityTool != inst->tool || !player->HasItemCount(inst->item, 1)
        || (M::CombatInterrupts(*song) && player->IsInCombat());
    if (cut)
    {
        Finish(player, M::RESULT_CUT);
        return;
    }
    // The client never said it was over: the song ends a little after its last bar.
    if (now - p.startMs > p.chart.lengthMs + M::kEndGraceMs + 1000)
        Finish(player, 0);
}

bool MinstrelMgr::HandleChannel(Player* player, std::vector<uint8> const& body)
{
    if (body.empty())
        return false;
    uint8 const op = body[0];
    if (op != M::kOpSongHits && op != M::kOpSongStop && op != M::kOpTip && op != M::kOpAskWindow && op != M::kOpAct)
        return false;
    if (!sFusionMgr.IsEnabled() || !player->HasFusion() || !player->IsInWorld())
        return true;
    FusionMinstrelData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    if (op == M::kOpSongHits)
    {
        M::Hits h;
        if (!d.playing.perf || !M::DecodeHits(body, h) || h.perf != d.playing.perf)
            return true;
        FusionMinstrelPerformance& p = d.playing;
        if (M::Accept(p.stream, p.chart, h.presses, NowMs() - p.startMs) != M::Reject::None)
            Finish(player, M::RESULT_REJECTED);
        return true;
    }
    if (op == M::kOpSongStop)
    {
        uint32 perf = 0;
        if (!d.playing.perf || !M::DecodeStop(body, perf) || perf != d.playing.perf)
            return true;
        M::InstrumentDef const* inst = M::Def(d.playing.chart.instrument);
        uint32 const window = inst ? inst->goodMs : 0u;
        uint32 const length = d.playing.chart.lengthMs;
        uint32 const due = length > window ? length - window : 0u;
        uint32 const elapsed = NowMs() - d.playing.startMs;
        Finish(player, elapsed + M::kFutureSlackMs < due ? uint8(M::RESULT_CUT) : uint8(0));
        return true;
    }
    if (op == M::kOpTip)
    {
        uint64 performer = 0;
        uint32 copper = 0;
        if (M::DecodeTip(body, performer, copper))
            Tip(player, performer, copper);
        return true;
    }
    if (op == M::kOpAskWindow)
    {
        if (body.size() == 1)
        {
            d.trainer.Clear();
            SendWindow(player, false);
        }
        return true;
    }
    M::Action a = M::Action::None;
    uint32 arg = 0;
    if (M::DecodeAct(body, a, arg))
        SendWindow(player, false, Act(player, a, arg));
    return true;
}

void MinstrelMgr::Finish(Player* player, uint8 flags)
{
    FusionMinstrelData& d = D(player);
    FusionMinstrelPerformance p = std::move(d.playing);
    d.playing = FusionMinstrelPerformance{};
    M::SongDef const* song = M::Song(p.song);
    M::InstrumentDef const* inst = M::Def(p.chart.instrument);
    if (!p.perf || !song || !inst)
        return;
    M::Tally const t = M::Score(p.chart, p.stream.presses, M::kSongOver);
    float const quality = M::Quality(t);
    bool const robotic = M::Robotic(t);
    float potency = M::Potency(*inst, t);
    bool const rejected = p.stream.rejected != M::Reject::None;
    if (rejected)
    {
        flags |= M::RESULT_REJECTED;
        potency = 0.f;
    }
    // A song cut short (a stun, a fight for a peaceful song, the hands put to something else)
    // lands nothing and teaches nothing.
    if (flags & M::RESULT_CUT)
        potency = 0.f;
    if (robotic && quality > M::kRoboticQuality)
        flags |= M::RESULT_ROBOTIC;
    if (!player->IsAlive())
        potency = 0.f;
    // Played standing on a bard's stage (furniture, fusion/Furniture.h): it carries further.
    if (potency > 0.f)
        potency *= sFusionMgr.StageBoost(player);
    if (potency <= 0.f)
        flags |= M::RESULT_FLAT;
    // The skill: only a song that landed teaches.
    if (potency > 0.f && d.tier != M::Tier::None)
    {
        uint16 const next = M::SkillGain(*song, d.skill, M::SkillCap(d.tier), quality, robotic, rand_norm_f());
        if (next != d.skill)
        {
            flags |= M::RESULT_SKILLUP;
            SetSkill(player, d.tier, next, true);
        }
    }
    M::Result r;
    r.perf = p.perf;
    r.song = song->id;
    r.flags = flags;
    r.perfect = t.perfect;
    r.good = t.good;
    r.miss = t.miss;
    r.kept = t.kept;
    r.dropped = t.dropped;
    r.strays = t.strays;
    r.maxCombo = t.maxCombo;
    r.potencyPm = uint16(std::lround(potency * 1000.f));
    r.qualityPm = uint16(std::lround(quality * 1000.f));
    sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeResult(r));
    if (potency <= 0.f)
    {
        ChatHandler(player).PSendSysMessage("|cffff6060%s: %s.|r", song->name,
            rejected ? "the song falls apart" : (flags & M::RESULT_CUT) ? "the song is cut short" : "the song falls flat");
        return;
    }
    ChatHandler(player).PSendSysMessage("|cffffd100%s:|r %s at %u%% (%u perfect, %u good, %u missed, best combo %u).",
        inst->name, song->name, uint32(std::lround(potency * 100.f)), uint32(t.perfect), uint32(t.good),
        uint32(t.miss + t.dropped), uint32(t.maxCombo));
    if (M::CombatSong(song->effect))
        d.combatReadyMs[song->id] = NowMs() + M::kCombatCooldownMs;
    // The achievements: a song that landed, and one played near perfectly (never a robot's).
    sFusionMgr.CountStat(player, {Fusion::Stat::SongsPlayed, Fusion::kAnyKey});
    if (quality >= 0.95f && !robotic)
        sFusionMgr.CountStat(player, {Fusion::Stat::PerfectSongs, Fusion::kAnyKey});
    Land(player, *song, potency, robotic ? std::min(quality, M::kRoboticQuality) : quality);
}

void MinstrelMgr::Land(Player* player, M::SongDef const& song, float potency, float quality)
{
    uint32 const now = NowMs();
    float const value = M::Magnitude(song, potency);
    bool const room = song.effect == M::Effect::Rested;
    std::vector<Player*> const listeners = Listeners(player, room ? M::kRoomRange : M::kPartyRange, !room);
    uint32 helped = 0;
    for (Player* m : listeners)
    {
        FusionMinstrelData& md = D(m);
        if (!md.loaded)
            LoadPlayer(m);
        if (room)
        {
            // Rest: only where travellers rest, and once a listener each half hour.
            if (!Resting(m) || M::HasEffect(md.buffs, M::Effect::Rested, now))
                continue;
            if (uint32 const next = m->GetUInt32Value(PLAYER_NEXT_LEVEL_XP); next > 0 && m->GetLevel() < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
            {
                m->SetRestBonus(m->GetRestBonus() + float(next) * value);
                if (m != player)
                    ChatHandler(m).PSendSysMessage("|cffffd100%s's %s|r leaves you rested.", player->GetName(), song.name);
            }
        }
        M::ApplyBuff(md.buffs, song, value, now);
        md.buffsDirty = true;
        ++helped;
    }
    if (helped > 1)
        ChatHandler(player).PSendSysMessage("%u listeners %s.", helped, EffectText(song.effect));
    // Those near may tip: told who played what (any song, at an inn or not).
    uint32 const account = player->GetSession()->GetAccountId();
    M::Heard heard{player->GetObjectGuid().GetRawValue(), song.id, uint16(std::lround(potency * 1000.f)), player->GetName()};
    std::vector<uint8_t> const told = M::EncodeHeard(heard);
    for (Player* m : Listeners(player, M::kRoomRange, false))
    {
        if (m == player)
            continue;
        FusionMinstrelData& md = D(m);
        md.heard.push_back({heard.performer, account, D(player).lastPerf, now, false});
        if (md.heard.size() > 8)
            md.heard.erase(md.heard.begin());
        sFusionMgr.SendChannel(m, AS::kChannel, told);
    }
    // The crowd's coin, at an inn or in a city, capped a day for the character and the account.
    if (Resting(player))
    {
        FusionMinstrelData& d = D(player);
        RollDay(player);
        uint32 accountCrowd = 0;
        if (std::unique_ptr<QueryResult> r = CharacterDatabase.PQuery(
                "SELECT SUM(m.`crowd`) FROM `character_fusion_minstrel` m JOIN `characters` c ON c.`guid` = m.`guid` "
                "WHERE c.`account` = %u AND m.`day` = %u AND m.`guid` <> %u", account, d.day, player->GetGUIDLow()))
            accountCrowd = r->Fetch()[0].GetUInt32();
        M::Color const color = Fusion::Construction::ColorOf(d.skill, M::SkillOf(song));
        uint32 const coin = M::CrowdTip(uint8(player->GetLevel()), quality, CrowdNear(player), color, d.crowd, accountCrowd + d.crowd);
        if (coin > 0)
        {
            d.crowd += coin;
            player->ModifyMoney(int32(coin));
            SaveDay(player);
            ChatHandler(player).PSendSysMessage("|cffffd100The crowd tips you %s.|r", Money(coin).c_str());
        }
    }
}

void MinstrelMgr::WandererSong(Creature* performer)
{
    if (!sFusionMgr.IsEnabled() || !performer || !performer->GetMap())
        return;
    M::SongDef const* song = M::Song(2);        // Hearthside Rhythm
    if (!song)
        return;
    uint32 const now = NowMs();
    float const value = M::Magnitude(*song, 1.f);
    Map::PlayerList const& players = performer->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
    {
        Player* m = i->getSource();
        if (!m || !m->IsInWorld() || !m->IsAlive() || !m->HasFusion() || !Resting(m) || m->GetDistance(performer) > M::kRoomRange)
            continue;
        FusionMinstrelData& md = D(m);
        if (!md.loaded)
            LoadPlayer(m);
        if (M::HasEffect(md.buffs, M::Effect::Rested, now))
            continue;
        if (uint32 const next = m->GetUInt32Value(PLAYER_NEXT_LEVEL_XP); next > 0 && m->GetLevel() < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
            m->SetRestBonus(m->GetRestBonus() + float(next) * value);
        M::ApplyBuff(md.buffs, *song, value, now);
        md.buffsDirty = true;
        ChatHandler(m).PSendSysMessage("|cffffd100%s's %s|r leaves you rested.", performer->GetName(), song->name);
    }
}

// ── Tips ──────────────────────────────────────────────────────────────────────────────────────

void MinstrelMgr::Tip(Player* player, uint64 performerRaw, uint32 copper)
{
    auto answer = [&](bool ok, std::string const& text) {
        sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeTipped(ok, copper, text));
        if (!ok)
            player->GetSession()->SendNotification("%s", text.c_str());
    };
    Player* performer = player->GetMap() ? player->GetMap()->GetPlayer(ObjectGuid(performerRaw)) : nullptr;
    if (!performer || !performer->HasFusion() || performer == player || Dist(player, performer) > kTipRange)
        return answer(false, "The minstrel is not near.");
    FusionMinstrelData& d = D(player);
    FusionMinstrelData& pd = D(performer);
    if (!pd.loaded)
        LoadPlayer(performer);
    RollDay(player);
    RollDay(performer);
    uint32 const now = NowMs();
    FusionMinstrelHeard* heard = nullptr;
    for (FusionMinstrelHeard& h : d.heard)
        if (h.performer == performerRaw && now - h.atMs <= M::kHeardWindowMs)
            heard = &h;
    M::TipAsk a;
    a.tipperAccount = player->GetSession()->GetAccountId();
    a.performerAccount = performer->GetSession()->GetAccountId();
    a.copper = copper;
    a.money = player->GetMoney();
    a.givenToday = d.given;
    a.gotToday = pd.got;
    a.heard = heard != nullptr;
    a.tipped = heard && heard->tipped;
    switch (M::CheckTip(a))
    {
        case M::TipCheck::Ok: break;
        case M::TipCheck::Zero: return answer(false, "Nothing to give.");
        case M::TipCheck::SameAccount: return answer(false, "You can't tip your own characters.");
        case M::TipCheck::NotHeard: return answer(false, "Tip a song you heard, within a minute of it.");
        case M::TipCheck::AlreadyTipped: return answer(false, "You have tipped that song already.");
        case M::TipCheck::TooMuch: return answer(false, "A tip is at most " + Money(M::kMaxTip) + ".");
        case M::TipCheck::GiveCap: return answer(false, "You have given all the tips you may today (" + Money(M::kTipGiveDaily) + ").");
        case M::TipCheck::GetCap: return answer(false, std::string(performer->GetName()) + " has taken all the tips allowed today.");
        case M::TipCheck::NoMoney: return answer(false, "You don't have that much.");
    }
    heard->tipped = true;
    player->ModifyMoney(-int32(copper));
    performer->ModifyMoney(int32(copper));
    d.given += copper;
    pd.got += copper;
    SaveDay(player);
    SaveDay(performer);
    ChatHandler(performer).PSendSysMessage("|cffffd100%s tips you %s.|r", player->GetName(), Money(copper).c_str());
    answer(true, "You tip " + std::string(performer->GetName()) + " " + Money(copper) + ".");
}

// ── The trainer's window ──────────────────────────────────────────────────────────────────────

bool MinstrelMgr::GossipHello(Player* player, Creature* creature)
{
    if (!sFusionMgr.IsEnabled() || !player->HasFusion())
        return false;
    FusionMinstrelData& d = D(player);
    if (!d.loaded)
        LoadPlayer(player);
    d.trainer = creature->GetObjectGuid();
    player->PlayerTalkClass->ClearMenus();
    player->PlayerTalkClass->CloseGossip();
    SendWindow(player, true);
    return true;
}

Creature* MinstrelMgr::TrainerNear(Player* player) const
{
    FusionMinstrelData const& d = D(player);
    if (d.trainer.IsEmpty() || !player->GetMap())
        return nullptr;
    Creature* c = player->GetMap()->GetCreature(d.trainer);
    if (!c || !c->IsAlive() || c->GetEntry() != M::kTrainerEntry || !player->IsWithinDistInMap(c, kTrainerReach))
        return nullptr;
    return c;
}

void MinstrelMgr::SendWindow(Player* player, bool open, Said const& said)
{
    FusionMinstrelData const& d = D(player);
    M::Window w;
    w.open = open;
    w.tier = d.tier;
    w.skill = d.skill;
    if (!said.text.empty())
    {
        w.tone = uint8(said.ok ? Fusion::Homestead::Tone::Done : Fusion::Homestead::Tone::Error);
        w.message = said.text;
    }
    w.trainer = TrainerNear(player) != nullptr;
    if (w.trainer)
    {
        bool const gm = sFusionMgr.IsAdmin(player);
        M::Tier const next = Fusion::Construction::Next(d.tier);
        if (next != M::Tier::None)
        {
            M::TierDef const& t = M::TierOf(next);
            M::Service s;
            s.action = M::Action::Train;
            s.cost = t.costCopper;
            s.label = std::string("Train ") + t.name + " Minstrelsy";
            std::ostringstream note;
            note << "Skill up to " << t.cap << "  -  level " << uint32(t.levelToLearn);
            if (t.skillToLearn)
                note << ", Minstrelsy " << t.skillToLearn;
            s.note = note.str();
            switch (M::CanTrainTier(d.tier, next, d.skill, uint8(player->GetLevel()), player->GetMoney(), gm))
            {
                case M::TrainResult::Ok: break;
                case M::TrainResult::SkillTooLow: s.enabled = false; s.note = "Requires Minstrelsy " + std::to_string(t.skillToLearn); break;
                case M::TrainResult::LevelTooLow: s.enabled = false; s.note = "Requires level " + std::to_string(t.levelToLearn); break;
                case M::TrainResult::NotEnoughMoney: s.enabled = false; s.note = "You need " + Money(t.costCopper); break;
                default: s.enabled = false; break;
            }
            w.services.push_back(std::move(s));
        }
        // The trainer sells the two plain instruments; the rest are made or found.
        for (M::Instrument i : {M::Instrument::HandDrum})
        {
            M::InstrumentDef const& inst = *M::Def(i);
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(inst.item);
            if (!proto)
                continue;
            M::Service s;
            s.action = M::Action::Buy;
            s.arg = uint32(i);
            s.cost = proto->BuyPrice;
            s.label = std::string("Buy a ") + inst.name;
            s.note = inst.skill > 1 ? "Played from Minstrelsy " + std::to_string(inst.skill) : "Every minstrel's first";
            if (player->GetMoney() < proto->BuyPrice && !gm)
            {
                s.enabled = false;
                s.note = "You need " + Money(proto->BuyPrice);
            }
            w.services.push_back(std::move(s));
        }
    }
    // The instrument recipes: to learn (at a trainer), and to make (anywhere), in their colours.
    bool const gm = sFusionMgr.IsAdmin(player);
    for (M::Recipe const& r : M::Recipes())
    {
        M::InstrumentDef const& inst = *M::Def(r.instrument);
        bool const known = std::find(d.recipes.begin(), d.recipes.end(), r.id) != d.recipes.end();
        M::Service s;
        s.arg = r.id;
        s.hue = uint8(d.tier == M::Tier::None || d.skill < r.required ? Fusion::Homestead::Hue::Red
            : HueOf(Fusion::Construction::ColorOf(d.skill, M::SkillOf(r))));
        if (!known)
        {
            if (!w.trainer)
                continue;
            s.action = M::Action::Learn;
            s.cost = r.learnCost;
            s.label = std::string("Learn: ") + inst.name;
            s.note = "Minstrelsy " + std::to_string(r.required);
            switch (M::CanLearn(r, d.tier, d.skill, false, r.score && player->HasItemCount(r.score, 1), player->GetMoney(), gm))
            {
                case M::LearnCheck::Ok: break;
                case M::LearnCheck::NoScore: s.enabled = false; s.note = "Bring its score (a rare find)"; break;
                case M::LearnCheck::NotEnoughMoney: s.enabled = false; s.note = "You need " + Money(r.learnCost); break;
                default: s.enabled = false; s.note = "Requires Minstrelsy " + std::to_string(r.required); break;
            }
        }
        else
        {
            s.action = M::Action::Make;
            s.label = std::string("Make: ") + inst.name;
            std::ostringstream o;
            for (size_t k = 0; k < r.reagents.size(); ++k)
            {
                ItemPrototype const* p = sObjectMgr.GetItemPrototype(r.reagents[k].item);
                o << (k ? ", " : "") << r.reagents[k].count << " " << (p ? p->Name1 : "?");
            }
            s.note = o.str();
            Fusion::Construction::Inventory bags;
            for (Fusion::Construction::Material const& m : r.reagents)
                bags[m.item] = player->GetItemCount(m.item);
            s.enabled = M::CanMake(r, true, d.skill, bags, gm) == M::MakeCheck::Ok;
        }
        w.services.push_back(std::move(s));
    }
    sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeWindow(w));
    sFusionMgr.SendChannel(player, AS::kChannel, M::EncodeBook(M::Book(d.recipes)));
}

MinstrelMgr::Said MinstrelMgr::Act(Player* player, M::Action a, uint32 arg)
{
    if (a == M::Action::Make)
    {
        // Create (one) or Create All (the count), as WoW's trade window makes them: one at a time
        // until something runs out; the first refusal is the answer.
        uint16 const id = M::MakeRecipeOf(arg);
        uint16 const count = M::MakeCountOf(arg);
        Said said;
        uint16 made = 0;
        for (uint16 i = 0; i < count; ++i)
        {
            Said const one = Make(player, id);
            if (!one.ok)
            {
                if (!made)
                    said = one;
                break;
            }
            said = one;
            ++made;
        }
        if (made > 1)
            if (M::Recipe const* r = M::RecipeOf(id))
                said.text = "You make " + std::to_string(made) + " " + M::Def(r->instrument)->name + "s.";
        return said;
    }
    if (!TrainerNear(player))
        return {false, "Speak with a Minstrel Trainer for that."};
    FusionMinstrelData const& d = D(player);
    bool const gm = sFusionMgr.IsAdmin(player);
    if (a == M::Action::Train)
    {
        M::Tier const have = d.tier;
        M::Tier const want = Fusion::Construction::Next(have);
        M::TierDef const& t = M::TierOf(want);
        switch (M::CanTrainTier(have, want, d.skill, uint8(player->GetLevel()), player->GetMoney(), gm))
        {
            case M::TrainResult::Ok: break;
            case M::TrainResult::SkillTooLow: return {false, "You need " + std::to_string(t.skillToLearn) + " Minstrelsy for that."};
            case M::TrainResult::LevelTooLow: return {false, "You must be level " + std::to_string(t.levelToLearn) + "."};
            case M::TrainResult::NotEnoughMoney: return {false, std::string(t.name) + " Minstrelsy costs " + Money(t.costCopper) + "."};
            default: return {false, "There is nothing more I can teach you."};
        }
        if (!gm)
            player->ModifyMoney(-int32(t.costCopper));
        SetSkill(player, want, have == M::Tier::None ? 1 : d.skill, false);
        return {true, std::string("You have learned ") + t.name + " Minstrelsy: your skill may now reach " + std::to_string(t.cap) + "."};
    }
    if (a == M::Action::Learn)
    {
        M::Recipe const* r = M::RecipeOf(uint16(arg));
        if (!r)
            return {false, "There is no such recipe."};
        FusionMinstrelData& md = D(player);
        bool const known = std::find(md.recipes.begin(), md.recipes.end(), r->id) != md.recipes.end();
        bool const score = r->score && player->HasItemCount(r->score, 1);
        std::string const name = M::Def(r->instrument)->name;
        switch (M::CanLearn(*r, md.tier, md.skill, known, score, player->GetMoney(), gm))
        {
            case M::LearnCheck::Ok: break;
            case M::LearnCheck::Known: return {false, "You know that already."};
            case M::LearnCheck::NoScore: return {false, "The " + name + " are learned only from their score."};
            case M::LearnCheck::NotEnoughMoney: return {false, "That costs " + Money(r->learnCost) + "."};
            default: return {false, "You need " + std::to_string(r->required) + " Minstrelsy for that."};
        }
        if (!gm)
        {
            player->ModifyMoney(-int32(r->learnCost));
            if (r->score)
                player->DestroyItemCount(r->score, 1, true);
        }
        md.recipes.push_back(r->id);
        CharacterDatabase.PExecute("REPLACE INTO `character_fusion_minstrel_recipe` (`guid`, `recipe`) VALUES (%u, %u)",
            player->GetGUIDLow(), uint32(r->id));
        return {true, "You can now make the " + name + "."};
    }
    if (a == M::Action::Buy)
    {
        M::InstrumentDef const* inst = M::Def(M::Instrument(arg));
        if (!inst || inst->id != M::Instrument::HandDrum)
            return {false, "I don't sell that."};
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(inst->item);
        if (!proto)
            return {false, "I don't sell that."};
        if (!gm && player->GetMoney() < proto->BuyPrice)
            return {false, std::string("A ") + inst->name + " costs " + Money(proto->BuyPrice) + "."};
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, inst->item, 1) != EQUIP_ERR_OK)
            return {false, "Your bags are full."};
        if (!gm)
            player->ModifyMoney(-int32(proto->BuyPrice));
        if (Item* item = player->StoreNewItem(dest, inst->item, true))
            player->SendNewItem(item, 1, true, false);
        return {true, std::string("You buy a ") + inst->name + "."};
    }
    return {false, ""};
}

MinstrelMgr::Said MinstrelMgr::Make(Player* player, uint16 id)
{
    M::Recipe const* r = M::RecipeOf(id);
    if (!r)
        return {false, "There is no such recipe."};
    FusionMinstrelData const& d = D(player);
    bool const gm = sFusionMgr.IsAdmin(player);
    bool const known = std::find(d.recipes.begin(), d.recipes.end(), r->id) != d.recipes.end();
    M::InstrumentDef const& inst = *M::Def(r->instrument);
    Fusion::Construction::Inventory bags;
    for (Fusion::Construction::Material const& m : r->reagents)
        bags[m.item] = player->GetItemCount(m.item);
    switch (M::CanMake(*r, known, d.skill, bags, gm))
    {
        case M::MakeCheck::Ok: break;
        case M::MakeCheck::Unknown: return {false, std::string("Learn the ") + inst.name + " from a Minstrel Trainer first."};
        case M::MakeCheck::SkillTooLow: return {false, "You need " + std::to_string(r->required) + " Minstrelsy for that."};
        default: return {false, std::string("You lack the goods for the ") + inst.name + "."};
    }
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, inst.item, 1) != EQUIP_ERR_OK)
        return {false, "Your bags are full."};
    if (!gm)
        for (Fusion::Construction::Material const& m : r->reagents)
            player->DestroyItemCount(m.item, m.count, true);
    if (Item* item = player->StoreNewItem(dest, inst.item, true))
        player->SendNewItem(item, 1, false, true);
    // A point, by vanilla's colours (a game master's free goods teach nothing).
    if (!gm && d.tier != M::Tier::None)
        if (uint16 const next = M::MakeSkillUp(*r, d.skill, M::SkillCap(d.tier), rand_norm_f()); next != d.skill)
            SetSkill(player, d.tier, next, true);
    return {true, std::string("You make a ") + inst.name + "."};
}
