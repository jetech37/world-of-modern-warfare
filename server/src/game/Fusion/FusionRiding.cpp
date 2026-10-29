/*
 * World of Modern Warfare: riding (fusion/Riding.h) in the game. A rider shoots from the saddle
 * and may be knocked off by gunfire; a mount glides on a fall from height, takes off at a launch
 * point for a short burst of flight under a hard ceiling, and glides down after. The glide is
 * vanilla's feather fall (the client already keeps its speed and only caps the fall) and the burst
 * is the GM flight the client already knows (levitating swimming), both sent and taken back by the
 * server on its own timers; fall damage is kept off for the glide. Also the homestead sites' floor:
 * whoever falls through the world over a site is put back at its nearest courier stop.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionMgr.h"

#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuraDefines.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "WorldSession.h"

#include <cmath>
#include <sstream>

namespace
{
    namespace Riding = Fusion::Riding;
    namespace Plots = Fusion::Plots;

    // How often the floor under a site is checked, and the ground under a mounted fall.
    constexpr uint32 kFloorLookMs = 500;
    // Below this the map had no height to give (Map::GetHeight's INVALID_HEIGHT).
    constexpr float kNoHeight = -50000.f;

    char const* PhaseName(Riding::Phase p)
    {
        switch (p)
        {
            case Riding::Phase::Glide: return "gliding";
            case Riding::Phase::Burst: return "flying";
            case Riding::Phase::Descent: return "gliding down";
            case Riding::Phase::Skyride: return "skyriding";
            default: return "on the ground";
        }
    }

    bool Gliding(Riding::Phase p) { return p == Riding::Phase::Glide || p == Riding::Phase::Descent; }
}

void FusionMgr::LoadRiding()
{
    m_launchPoints = Riding::DefaultLaunchPoints();
    std::unique_ptr<QueryResult> has = WorldDatabase.Query("SHOW TABLES LIKE 'fusion_launch_point'");
    if (!has)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: no fusion_launch_point table (sql 12): %u built-in launch points.", uint32(m_launchPoints.size()));
        return;
    }
    std::vector<Riding::LaunchPoint> points;
    if (std::unique_ptr<QueryResult> r = WorldDatabase.Query(
            "SELECT `id`, `name`, `map`, `x`, `y`, `z`, `radius`, `burst_seconds`, `ceiling_z`, `range` FROM `fusion_launch_point` ORDER BY `id`"))
    {
        do
        {
            Field* f = r->Fetch();
            Riding::LaunchPoint p;
            p.id = f[0].GetUInt16();
            p.name = f[1].GetCppString();
            p.map = f[2].GetUInt32();
            p.x = f[3].GetFloat();
            p.y = f[4].GetFloat();
            p.z = f[5].GetFloat();
            p.radius = f[6].GetFloat();
            p.burstSeconds = f[7].GetFloat();
            p.ceilingZ = f[8].GetFloat();
            p.range = f[9].GetFloat();
            points.push_back(std::move(p));
        } while (r->NextRow());
    }
    std::vector<std::string> const problems = Riding::Problems(points);
    for (std::string const& p : problems)
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: fusion_launch_point: %s.", p.c_str());
    if (points.empty() || !problems.empty())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: fusion_launch_point is empty or unsound: %u built-in launch points.", uint32(m_launchPoints.size()));
        return;
    }
    m_launchPoints = std::move(points);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u launch points.", uint32(m_launchPoints.size()));
}

float FusionMgr::RiderEyeRise(Player const* player) const
{
    return m_enabled && player->IsMounted() ? Riding::kMountedEyeRise : 0.f;
}

float FusionMgr::ShotOriginTolerance(Player const* player) const
{
    return Riding::OriginTolerance(m_enabled && player->IsMounted());
}

void FusionMgr::KnockOffMount(Unit* to, uint32 damage, FusionHitSource const& source)
{
    Player* rider = to ? to->ToPlayer() : nullptr;
    if (!m_enabled || !rider || !damage || !rider->IsAlive() || !rider->IsMounted())
        return;
    Fusion::WeaponProfile const* w = source.weaponId ? Weapons().Find(source.weaponId) : nullptr;
    Riding::RiderHit hit;
    hit.damage = float(damage);
    hit.maxHealth = float(rider->GetMaxHealth());
    hit.sniper = w && w->weaponClass == Fusion::WeaponClass::Sniper;
    hit.explosive = source.explosive;
    if (!Riding::KnockedOff(hit, rand_norm_f()))
        return;
    // As a daze would: every mount aura off (a companion mount comes back on foot by itself,
    // FusionCompanions.cpp).
    rider->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    rider->GetSession()->SendNotification("You were knocked off your mount!");
}

void FusionMgr::UpdateRiding(Player* player, uint32 now)
{
    FusionPlayerData& d = player->GetFusion();
    Map* map = player->GetMap();
    if (!map)
        return;
    float const x = player->GetPositionX(), y = player->GetPositionY(), z = player->GetPositionZ();
    bool const teleporting = player->IsBeingTeleported();

    // The site's floor: fallen off a rim or through the world, back to the nearest stop.
    if (int32(now - d.nextGroundLookMs) >= 0)
    {
        d.nextGroundLookMs = now + kFloorLookMs;
        if (!teleporting && !player->IsGameMaster() && !player->IsTaxiFlying() && !player->GetTransport())
            if (std::optional<Plots::Spot> const back = PlotGrid().FellThrough(player->GetMapId(), x, y, z))
            {
                player->TeleportTo(player->GetMapId(), back->x, back->y, back->z, back->o);
                player->GetSession()->SendNotification("You fell from the homesteads: the couriers pulled you back.");
                return;
            }
    }

    Riding::Sense const s = RidingSense(player, now, true);
    Riding::Phase const before = d.flight.phase;
    uint8 const charges = d.flight.vigor.charges;
    Riding::Step(d.flight, s);
    ApplyFlight(player, before, s, 0);
    // Over a ward's ceiling: the short message (the client is already pushing down).
    if (d.flight.wardPushed && d.wardKind)
        player->GetSession()->SendNotification(Fusion::Wards::PushMessage(Fusion::Wards::Kind(d.wardKind)));
    // A charge back, or the gauge's rider come or gone: tell the client.
    if (d.flight.vigor.charges != charges || s.skyrider != d.skyGauge)
        SendSky(player, 0);
    // No fall damage for a glide: the fall starts again from where the glide has got to.
    if (Gliding(d.flight.phase) || d.flight.phase == Riding::Phase::Burst || d.flight.phase == Riding::Phase::Skyride)
        player->SetFallInformation(0);
}

bool FusionMgr::RidesSkyrider(Player* player) const
{
    if (!m_enabled || !player->HasFusion() || !player->IsMounted())
        return false;
    FusionCompanionOut const& c = player->GetFusionConst().companion;
    if (!c.riding)
        return false;
    Item* item = player->GetItemByGuid(c.item);
    Fusion::Companions::CompanionSpecies const* sp = item ? CompanionSpeciesOfItem(item->GetEntry()) : nullptr;
    if (!sp)
        return false;
    Fusion::Companions::CompanionState const st = CompanionStateOf(item->GetGUIDLow(), item->GetEntry());
    return Fusion::Companions::IsSkySurge(Fusion::Companions::MountedMove(*sp, st));
}

Riding::Sense FusionMgr::RidingSense(Player* player, uint32 now, bool update)
{
    FusionPlayerData& d = player->GetFusion();
    Map* map = player->GetMap();
    float const x = player->GetPositionX(), y = player->GetPositionY(), z = player->GetPositionZ();
    uint32 const flags = player->m_movementInfo.moveFlags;
    bool const jumping = (flags & MOVEFLAG_JUMPING) != 0;
    Riding::Sense s;
    s.nowMs = now;
    s.alive = player->IsAlive();
    s.mounted = player->IsMounted();
    s.airborne = (flags & (MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR)) != 0;
    s.jumped = update && jumping && !d.wasJumping;
    if (update)
        d.wasJumping = jumping;
    // A game master's own flight, a taxi, a ship or a teleport: none of this.
    s.exempt = player->IsBeingTeleported() || player->IsTaxiFlying() || player->GetTransport()
        || (player->HasCheatOption(PLAYER_CHEAT_FLY) && !d.flightCheat);
    s.map = player->GetMapId();
    s.x = x;
    s.y = y;
    s.z = z;
    s.skyrider = RidesSkyrider(player);
    s.inWater = player->IsInWater();
    s.speed = player->GetSpeed(MOVE_RUN);
    Riding::Phase const before = d.flight.phase;
    // The ground only matters to a mounted fall, a glide, a skyride or the glide down.
    if (map && ((s.mounted && s.airborne) || Gliding(before) || before == Riding::Phase::Skyride || !update))
    {
        float const ground = map->GetHeight(x, y, z + 1.f);
        s.groundZ = ground < kNoHeight ? z - 1000.f : ground;
    }
    else
        s.groundZ = z;
    // Warded airspace over a city or an unfinished place: the ceiling a skyride is held under.
    d.wardKind = 0;
    if (before == Riding::Phase::Skyride)
    {
        Fusion::Wards::Here here;
        // No height to be had (a hole, no map): no ground ceiling, only a fixed one.
        float const ground = s.groundZ <= z - 999.f ? z : s.groundZ;
        if (Fusion::Wards::Find(Fusion::Wards::Zones(), s.map, x, y, ground, player->GetAreaId(), player->GetZoneId(), here))
        {
            s.wardZone = here.zone->id;
            s.wardCeilingZ = here.ceilingZ;
            d.wardKind = uint8(here.zone->kind);
            if (update)
                TellWard(player, now, uint8(Fusion::Wards::WARD_IN
                    | (here.zone->kind == Fusion::Wards::Kind::Unfinished ? Fusion::Wards::WARD_UNFINISHED : 0)
                    | (here.pad ? Fusion::Wards::WARD_PAD : 0)), here.ceilingZ);
        }
    }
    if (update && !s.wardZone)
        TellWard(player, now, 0, 0.f);
    if (s.jumped && s.mounted)
    {
        s.launch = Riding::LaunchAt(m_launchPoints, s.map, x, y, z);
        s.site = PlotGrid().SiteAt(s.map, x, y);
    }
    return s;
}

void FusionMgr::ApplyFlight(Player* player, Riding::Phase before, Riding::Sense const& s, uint8 skyFlags)
{
    FusionPlayerData& d = player->GetFusion();
    uint32 const now = s.nowMs;
    Riding::Phase const after = d.flight.phase;
    if (after == before)
    {
        if (skyFlags)
            SendSky(player, skyFlags);
        return;
    }
    // The burst's flight, and the skyride's: GM flight (the client's levitating swim). A burst
    // flies at the mount's speed; a skyride's speed is the physics', held to the energy law
    // (Skyriding::Envelope) under a swim speed of its fastest dive, which the anticheat checks.
    bool const flewBefore = before == Riding::Phase::Burst || before == Riding::Phase::Skyride;
    bool const fliesAfter = after == Riding::Phase::Burst || after == Riding::Phase::Skyride;
    if (fliesAfter)
    {
        if (!flewBefore)
        {
            d.flightCheat = true;
            player->SetCheatFly(true, false);
        }
        float const speed = after == Riding::Phase::Skyride ? Fusion::Skyriding::kMaxSpeed * 1.05f : player->GetSpeed(MOVE_RUN);
        player->SetSpeedRate(MOVE_SWIM, speed / baseMoveSpeed[MOVE_SWIM]);
        if (after == Riding::Phase::Burst)
            if (Riding::LaunchPoint const* p = s.launch)
                ChatHandler(player).PSendSysMessage("Updraft at %s: you can fly for %.0f seconds, up to %.0f yards.",
                    p->name.c_str(), Riding::SecondsLeft(d.flight, now), d.flight.ceilingZ - s.z);
    }
    else if (flewBefore)
    {
        if (d.flightCheat)
            player->SetCheatFly(false, false);
        d.flightCheat = false;
        player->UpdateSpeed(MOVE_SWIM, false);
        if (before == Riding::Phase::Skyride && d.flight.wardBroke)
            player->GetSession()->SendNotification(Fusion::Wards::PushMessage(
                d.wardKind ? Fusion::Wards::Kind(d.wardKind) : Fusion::Wards::Kind::City));
        else if (before == Riding::Phase::Skyride && d.flight.broke)
        {
            player->GetSession()->SendNotification("Your mount can't hold that flight: gliding down.");
            sLog.Out(LOG_ANTICHEAT, LOG_LVL_BASIC, "Fusion: the skyride of %s broke the energy law at (%.1f, %.1f, %.1f) on map %u.",
                player->GetName(), s.x, s.y, s.z, s.map);
        }
        else if (after == Riding::Phase::Descent)
            player->GetSession()->SendNotification(before == Riding::Phase::Skyride ? "Gliding down." : "The updraft gives out: gliding down.");
    }
    // The glide: feather fall, unless an aura already gives it.
    if (Gliding(after) != Gliding(before) && !player->HasAuraType(SPELL_AURA_FEATHER_FALL))
        player->SetFeatherFall(Gliding(after));
    SendChannel(player, Fusion::Packets::kChannelWorld, Riding::EncodeFlight(d.flight, now));
    if (after == Riding::Phase::Skyride || before == Riding::Phase::Skyride || skyFlags)
        SendSky(player, skyFlags);
}

void FusionMgr::TellWard(Player* player, uint32 now, uint8 flags, float ceilingZ)
{
    FusionPlayerData& d = player->GetFusion();
    bool const changed = flags != d.wardFlags;
    bool const moved = flags && std::fabs(ceilingZ - d.wardCeiling) >= Fusion::Wards::kTellMove
        && int32(now - d.wardToldMs) >= int32(Fusion::Wards::kTellMs);
    if (!changed && !moved)
        return;
    d.wardFlags = flags;
    d.wardCeiling = ceilingZ;
    d.wardToldMs = now;
    SendChannel(player, Fusion::Packets::kChannelWorld, Fusion::Wards::EncodeWard(flags, ceilingZ));
}

void FusionMgr::SendSky(Player* player, uint8 skyFlags)
{
    FusionPlayerData& d = player->GetFusion();
    d.skyGauge = RidesSkyrider(player);
    uint8 flags = skyFlags;
    if (d.flight.phase == Riding::Phase::Skyride)
        flags |= Fusion::Skyriding::SKY_FLYING;
    if (d.skyGauge)
        flags |= Fusion::Skyriding::SKY_RIDER;
    SendChannel(player, Fusion::Packets::kChannelWorld,
        Fusion::Skyriding::EncodeSky(flags, d.flight.vigor, std::min(player->GetSpeed(MOVE_RUN), Fusion::Skyriding::kMaxSpeed)));
}

bool FusionMgr::SkyMove(Player* player, Fusion::Skyriding::Move m)
{
    if (!m_enabled || !player->HasFusion() || !player->IsInWorld() || player->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
        return false;
    uint32 const now = WorldTimer::getMSTime();
    Riding::Sense const s = RidingSense(player, now, false);
    FusionPlayerData& d = player->GetFusion();
    Riding::Phase const before = d.flight.phase;
    if (!Riding::SkyMove(d.flight, s, m))
        return false;
    ApplyFlight(player, before, s, m == Fusion::Skyriding::Move::Ascent ? Fusion::Skyriding::SKY_ASCENT : Fusion::Skyriding::SKY_SURGE);
    player->SetFallInformation(0);
    CountStat(player, {Fusion::Stat::SkyLaunches, Fusion::kAnyKey});
    return true;
}

std::string FusionMgr::RidingReport(Player* player)
{
    FusionPlayerData const& d = player->GetFusionConst();
    uint32 const now = WorldTimer::getMSTime();
    std::ostringstream o;
    o.precision(1);
    o << std::fixed << "Fusion: riding: " << PhaseName(d.flight.phase);
    if (d.flight.phase != Riding::Phase::None)
        o << ", " << Riding::SecondsLeft(d.flight, now) << " s left";
    if (d.flight.phase == Riding::Phase::Burst)
        o << ", ceiling z " << d.flight.ceilingZ;
    if (d.flight.phase == Riding::Phase::Skyride && d.wardFlags)
        o << ", warded airspace " << d.flight.ward.zone << " (ceiling z " << d.wardCeiling << ", allowance "
          << d.flight.ward.allowance << ")";
    if (d.flight.phase == Riding::Phase::Skyride)
        o << ", energy budget " << d.flight.sky.budget << " (height " << d.flight.sky.budget / Fusion::Skyriding::kGravity
          << "), measured speed " << d.flight.sky.speed;
    o << ", vigor " << uint32(d.flight.vigor.charges) << "/" << uint32(Fusion::Skyriding::kMaxCharges);
    if (RidesSkyrider(player))
        o << " (a flying line: it skyrides)";
    if (int32(d.flight.nextLaunchMs - now) > 0)
        o << ", next take-off in " << float(d.flight.nextLaunchMs - now) / 1000.f << " s";
    o << (player->IsMounted() ? "; mounted." : "; on foot.");
    // The nearest launch point on this map.
    Riding::LaunchPoint const* nearest = nullptr;
    float best = 0.f;
    for (Riding::LaunchPoint const& p : m_launchPoints)
    {
        if (p.map != player->GetMapId())
            continue;
        float const dist = player->GetDistance(p.x, p.y, p.z);
        if (!nearest || dist < best)
        {
            nearest = &p;
            best = dist;
        }
    }
    if (nearest)
        o << " Nearest launch point: " << nearest->id << ", " << nearest->name << ", " << best << " yards (" << nearest->x << ", "
          << nearest->y << ", " << nearest->z << "; radius " << nearest->radius << ").";
    else
        o << " No launch point on this map.";
    return o.str();
}

bool ChatHandler::HandleFusionRidingCommand(char* args)
{
    WorldSession* session = GetSession();
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !sFusionMgr.IsEnabled() || !sFusionMgr.IsAdmin(player) || !player->HasFusion())
    {
        SendSysMessage("Fusion: only an admin may inspect riding.");
        SetSentErrorMessage(true);
        return false;
    }
    std::istringstream in(args ? args : "");
    std::string what;
    in >> what;
    if (what == "knockoff")
    {
        // Testing the knock-off: as a sniper's round would.
        FusionHitSource source;
        source.explosive = true;
        sFusionMgr.KnockOffMount(player, 1, source);
    }
    else if (!what.empty())
    {
        SendSysMessage("Usage: .fusion riding [knockoff]");
        SetSentErrorMessage(true);
        return false;
    }
    SendSysMessage(sFusionMgr.RidingReport(player).c_str());
    return true;
}
