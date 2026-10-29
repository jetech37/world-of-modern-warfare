/*
 * World of Modern Warfare: player ships (FusionShips.h, design/PLAYER_SHIPS.md). GPL-2.0-or-later.
 */

#include "FusionShips.h"
#include "FusionMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "GridDefines.h"
#include "GridMap.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "fusion/Airspace.h"
#include "vmap/IVMapManager.h"
#include "Bag.h"
#include "Group.h"
#include "Item.h"
#include "ObjectMgr.h"

#include <set>

#include <cmath>
#include <ctime>
#include <sstream>

namespace S = Fusion::Ships;

// Map.cpp's specializations for an elevator, which a PlayerShip is.
template<> void Map::Add(ElevatorTransport*);
template<> void Map::Remove(ElevatorTransport*, bool);

namespace
{
    uint32 Now() { return uint32(time(nullptr)); }

    // The water a keel floats on at (x, y): water or the ocean, never magma or slime
    // (Fusion::Ships::SailableLiquid); VMAP_INVALID_HEIGHT_VALUE for none. `ground` is the
    // bottom under it.
    float SailWater(TerrainInfo const* t, float x, float y, float z, float* ground)
    {
        float const bottom = t->GetHeightStatic(x, y, z, true, DEFAULT_WATER_SEARCH);
        if (ground)
            *ground = bottom;
        GridMapLiquidData liquid;
        if (!t->getLiquidStatus(x, y, bottom, MAP_ALL_LIQUIDS, &liquid))
            return VMAP_INVALID_HEIGHT_VALUE;
        if (!Fusion::Ships::SailableLiquid(uint8(liquid.type_flags)))
            return VMAP_INVALID_HEIGHT_VALUE;
        return liquid.level;
    }

    // How far the pose stream reaches: past the visibility range, short of a whole continent.
    constexpr float kPoseReach = 1500.f;
    constexpr int32 kSaveMovingMs = 30000;
    constexpr int32 kAutoAnchorMs = 5000;   // unpiloted and at rest this long: the anchor drops

    std::string Money(uint32 copper)
    {
        uint32 const g = copper / 10000, s = (copper / 100) % 100, c = copper % 100;
        std::ostringstream o;
        if (g)
            o << g << "g";
        if (s)
            o << (g ? " " : "") << s << "s";
        if (c || (!g && !s))
            o << (g || s ? " " : "") << c << "c";
        return o.str();
    }

    std::string Duration(uint32 sec)
    {
        std::ostringstream o;
        if (sec >= 3600)
            o << sec / 3600 << "h " << (sec / 60) % 60 << "m";
        else
            o << (sec + 59) / 60 << "m";
        return o.str();
    }

    void Tell(Player* p, char const* text)
    {
        if (p && *text)
            ChatHandler(p).PSendSysMessage("|cff88ccff%s|r", text);
    }

    // The gossip actions: what * 16 + class.
    enum GossipWhat : uint32
    {
        kGossipSender = 0x5110,
        kRightsSender = 0x5111,
        kCommission = 1, kLaunch = 2, kSummon = 3, kRepair = 4, kUpkeep = 5, kSalvage = 6, kDeeds = 7, kExplain = 8,
    };
}

// ── PlayerShip ─────────────────────────────────────────────────────────────────────────────────

PlayerShip::PlayerShip(uint32 shipId, S::ClassInfo const& info)
    : hull(info.maxHull), upkeepUntil(0), bellAt(0), m_shipId(shipId), m_info(info)
{
}

S::Probe PlayerShip::ProbeAt(float x, float y, float z) const
{
    S::Probe p;
    p.inBounds = std::fabs(x) < MAP_HALFSIZE - 200.f && std::fabs(y) < MAP_HALFSIZE - 200.f;
    if (!p.inBounds)
        return p;
    TerrainInfo const* t = GetMap()->GetTerrain();
    float ground = VMAP_INVALID_HEIGHT_VALUE;
    float const water = SailWater(t, x, y, z + 5.f, &ground);
    if (water > VMAP_INVALID_HEIGHT_VALUE + 1.f)
    {
        p.water = true;
        p.waterZ = water;
    }
    if (m_info.climbRate > 0.f)
        ground = t->GetHeightStatic(x, y, z, true, 2000.f);
    p.groundZ = ground > VMAP_INVALID_HEIGHT_VALUE + 1.f ? ground : -1.0e6f;
    if (m_info.climbRate > 0.f)
    {
        uint32 zone = 0, area = 0;
        t->GetZoneAndAreaId(zone, area, x, y, z);
        Fusion::Wards::Here here;
        if (Fusion::Wards::Find(Fusion::Wards::Zones(), GetMapId(), x, y, p.groundZ, area, zone, here))
        {
            p.ceilingZ = here.ceilingZ;
            p.unfinished = here.zone && here.zone->kind == Fusion::Wards::Kind::Unfinished;
        }
    }
    return p;
}

// The pilot's state for the helm (fusion/Ships.h HelmBody).
static S::HelmBody HelmBodyOf(PlayerShip const& ship, Player* p)
{
    S::HelmBody b;
    b.alive = p && p->IsAlive();
    b.aboard = ship.IsAboard(p);
    b.downed = p && sFusionMgr.IsDowned(p);
    b.mounted = p && (p->IsMounted() || p->GetFusion().companion.riding);
    b.dueling = p && sFusionMgr.InCompanionBattle(p);
    return b;
}

void PlayerShip::TakeHelm(Player* pilot)
{
    m_pilot = pilot->GetObjectGuid();
    m_helm = S::Helm();
    pilot->SetRooted(true);
    sFusionMgr.SendChannel(pilot, S::kChannel, S::EncodeHelm(GetObjectGuid().GetRawValue()));
    m_poseMs = 0;
}

void PlayerShip::LeaveHelm(bool tell)
{
    if (m_pilot.IsEmpty())
        return;
    if (Player* p = GetMap()->GetPlayer(m_pilot))
    {
        p->SetRooted(false);
        if (tell)
            sFusionMgr.SendChannel(p, S::kChannel, S::EncodeHelm(0));
    }
    m_pilot.Clear();
    m_helm.Release();
    m_helm.notch = 0;
    m_poseMs = 0;
    m_saveMs = 0;
}

void PlayerShip::Damage(uint32 amount)
{
    uint32 h = hull.load();
    uint32 const left = amount >= h ? 0 : h - amount;
    hull.store(left);
    if (amount)
        lastHitAt.store(Now());
    if (!left && !wrecked.exchange(true))
    {
        char const* how = m_info.climbRate > 0.f ? "crashes" : "sinks";
        std::lock_guard<std::mutex> g(m_passengerMutex);
        for (Unit* u : m_passengers)
            if (Player* p = u->ToPlayer())
                ChatHandler(p).PSendSysMessage("|cffff4040%s %s!|r", name.c_str(), how);
    }
}

void PlayerShip::Update(uint32 update_diff, uint32 /*time_diff*/)
{
    if (!IsInWorld() || wrecked.load())
        return;
    float const dt = std::min(0.5f, float(update_diff) / 1000.f);
    // The pilot: still here, alive and aboard, or the helm is free again.
    Player* pilot = m_pilot.IsEmpty() ? nullptr : GetMap()->GetPlayer(m_pilot);
    // Downed, dead, mounted (a companion's back too), in a duel or ashore: the helm is let go.
    if (!m_pilot.IsEmpty() && (!pilot || S::HelmBodyRefusal(HelmBodyOf(*this, pilot)) != S::Refusal::None))
    {
        LeaveHelm(pilot != nullptr);
        pilot = nullptr;
    }
    // The gunners: still here, alive, aboard and at their mounting, or the gun is free again.
    for (size_t i = 0; i < m_gunners.size(); ++i)
    {
        if (m_gunners[i].IsEmpty())
            continue;
        Player* g = GetMap()->GetPlayer(m_gunners[i]);
        bool keep = g && g->IsAlive() && IsAboard(g) && i < S::MountsOf(m_info.cls).size();
        if (keep)
        {
            Fusion::Vec3 const at = S::MountWorld(S::MountsOf(m_info.cls)[i], m_motion.x, m_motion.y, m_motion.z, m_motion.o);
            keep = std::hypot(g->GetPositionX() - at.x, g->GetPositionY() - at.y) <= S::kMountReach * 2.f;
        }
        if (!keep)
        {
            if (g)
                LeaveGun(g, true);
            else
                m_gunners[i].Clear();
        }
    }
    // The owner aboard or near: their guild, for the guild's rights.
    if (Player* o = GetMap()->GetPlayer(owner))
        ownerGuild.store(o->GetGuildId());
    bool const neglected = S::Neglected(Now(), upkeepUntil.load());
    float const cap = S::SpeedCap(m_info, neglected, hull.load());
    S::Motion before = m_motion;
    S::StepResult r = S::Step(m_info, m_motion, pilot ? &m_helm : nullptr, m_anchored, cap, dt,
        [this](float x, float y, float z) { return ProbeAt(x, y, z); });
    if (r.hullLoss)
    {
        Damage(r.hullLoss);
        if (pilot)
            ChatHandler(pilot).PSendSysMessage("|cffff8040The hull scrapes! (%u%% hull)|r", S::HullPct(m_info, hull.load()));
    }
    bool const moved = m_motion.x != before.x || m_motion.y != before.y || m_motion.z != before.z || m_motion.o != before.o;
    if (moved)
    {
        UpdatePosition(m_motion.x, m_motion.y, m_motion.z, m_motion.o);
        m_moved = true;
        m_restMs = 0;
    }
    else if (!pilot && !m_anchored)
    {
        // Unpiloted and at rest: the anchor drops by itself.
        m_restMs += int32(update_diff);
        if (m_restMs >= kAutoAnchorMs)
        {
            m_anchored = true;
            m_saveMs = 0;
        }
    }
    m_poseMs -= int32(update_diff);
    if (m_poseMs <= 0)
    {
        bool const still = !moved && m_motion.speed == 0.f && m_motion.yawRate == 0.f;
        m_poseMs = int32(still ? S::kPoseRestMs : S::kPoseMovingMs);
        Broadcast();
        sShipMgr.Publish(*this, false);
    }
    m_saveMs -= int32(update_diff);
    if (m_saveMs <= 0)
    {
        m_saveMs = kSaveMovingMs;
        if (m_moved || m_anchored)
        {
            sShipMgr.Publish(*this, true);
            m_moved = false;
        }
    }
}

void PlayerShip::SendPoseTo(Player* p)
{
    S::Pose pose;
    pose.guid = GetObjectGuid().GetRawValue();
    pose.cls = uint8(m_info.cls);
    uint32 const now = Now();
    uint32 const h = hull.load();
    pose.flags = uint8((m_anchored ? S::POSE_ANCHORED : 0) | (m_pilot.IsEmpty() ? 0 : S::POSE_PILOTED)
        | (p->GetObjectGuid() == owner ? S::POSE_MINE : 0)
        | (S::Neglected(now, upkeepUntil.load()) ? S::POSE_NEGLECTED : 0)
        | (float(h) < S::kListingShare * float(m_info.maxHull) ? S::POSE_LISTING : 0));
    S::Standing const st = sShipMgr.StandingOf(p, *this);
    uint8 const r = rights.load();
    if (S::May(st, r, S::Duty::Guns))
        pose.flags |= S::POSE_CREW;
    if (S::May(st, r, S::Duty::Helm))
        pose.flags |= S::POSE_HELMSMAN;
    pose.notch = m_helm.notch;
    pose.hullPct = S::HullPct(m_info, h);
    pose.x = m_motion.x;
    pose.y = m_motion.y;
    pose.z = m_motion.z;
    pose.o = m_motion.o;
    pose.vx = std::cos(m_motion.o) * m_motion.speed;
    pose.vy = std::sin(m_motion.o) * m_motion.speed;
    pose.vz = m_motion.climb;
    pose.yawRate = m_motion.yawRate;
    uint32 const bell = bellAt.load();
    if (p->GetObjectGuid() == owner && bell && now < bell + S::kBellCooldownSec)
        pose.bellSec = uint16(bell + S::kBellCooldownSec - now);
    pose.name = name;
    sFusionMgr.SendChannel(p, S::kChannel, S::EncodePose(pose));
}

void PlayerShip::Broadcast()
{
    for (auto const& ref : GetMap()->GetPlayers())
    {
        Player* p = ref.getSource();
        if (!p || !p->IsInWorld() || !p->GetSession())
            continue;
        if (p->GetDistance2d(this) > kPoseReach && p->GetObjectGuid() != owner)
            continue;
        SendPoseTo(p);
    }
}

// ── ShipMgr ────────────────────────────────────────────────────────────────────────────────────

ShipMgr& ShipMgr::Instance()
{
    static ShipMgr instance;
    return instance;
}

void ShipMgr::Load()
{
    m_enabled = true;
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `aw_ship` ("
        "`id` int(10) unsigned NOT NULL,"
        "`owner` int(10) unsigned NOT NULL,"
        "`class` tinyint(3) unsigned NOT NULL,"
        "`name` varchar(64) NOT NULL DEFAULT '',"
        "`state` tinyint(3) unsigned NOT NULL DEFAULT '0',"
        "`map` int(10) unsigned NOT NULL DEFAULT '0',"
        "`x` float NOT NULL DEFAULT '0',"
        "`y` float NOT NULL DEFAULT '0',"
        "`z` float NOT NULL DEFAULT '0',"
        "`o` float NOT NULL DEFAULT '0',"
        "`hull` int(10) unsigned NOT NULL DEFAULT '0',"
        "`anchored` tinyint(3) unsigned NOT NULL DEFAULT '1',"
        "`ready_at` int(10) unsigned NOT NULL DEFAULT '0',"
        "`upkeep_until` int(10) unsigned NOT NULL DEFAULT '0',"
        "`created` int(10) unsigned NOT NULL DEFAULT '0',"
        "`bell_at` int(10) unsigned NOT NULL DEFAULT '0',"
        "`last_sweep` int(10) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`id`), KEY `owner` (`owner`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW player ships (design/PLAYER_SHIPS.md)'");
    // The second slice's columns, on a table the first made (MariaDB's IF NOT EXISTS).
    for (char const* col : {
            "`rights` tinyint(3) unsigned NOT NULL DEFAULT '108'",
            "`owner_guild` int(10) unsigned NOT NULL DEFAULT '0'",
            "`last_respawn` int(10) unsigned NOT NULL DEFAULT '0'",
            "`safe_map` int(10) unsigned NOT NULL DEFAULT '4294967295'",
            "`safe_x` float NOT NULL DEFAULT '0'",
            "`safe_y` float NOT NULL DEFAULT '0'",
            "`safe_z` float NOT NULL DEFAULT '0'"})
        CharacterDatabase.DirectPExecute("ALTER TABLE `aw_ship` ADD COLUMN IF NOT EXISTS %s", col);
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `aw_ship_cargo` ("
        "`ship` int(10) unsigned NOT NULL,"
        "`slot` tinyint(3) unsigned NOT NULL,"
        "`entry` int(10) unsigned NOT NULL,"
        "`count` int(10) unsigned NOT NULL,"
        "PRIMARY KEY (`ship`, `slot`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW player ships: the cargo holds'");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `aw_ship_rider` ("
        "`guid` int(10) unsigned NOT NULL,"
        "`ship` int(10) unsigned NOT NULL,"
        "`map` int(10) unsigned NOT NULL,"
        "`x` float NOT NULL, `y` float NOT NULL, `z` float NOT NULL, `o` float NOT NULL,"
        "`wx` float NOT NULL, `wy` float NOT NULL, `wz` float NOT NULL,"
        "PRIMARY KEY (`guid`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW player ships: who logged out aboard'");
    std::lock_guard<std::recursive_mutex> g(m_lock);
    m_ships.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query(
            "SELECT `id`, `owner`, `class`, `name`, `state`, `map`, `x`, `y`, `z`, `o`, `hull`, `anchored`, "
            "`ready_at`, `upkeep_until`, `created`, `bell_at`, `last_sweep`, `rights`, `owner_guild`, `last_respawn`, "
            "`safe_map`, `safe_x`, `safe_y`, `safe_z` FROM `aw_ship`"))
    {
        do
        {
            Field* f = r->Fetch();
            Record rec;
            rec.id = f[0].GetUInt32();
            rec.owner = f[1].GetUInt32();
            rec.cls = S::Class(f[2].GetUInt8());
            rec.name = f[3].GetCppString();
            rec.state = S::State(f[4].GetUInt8());
            rec.map = f[5].GetUInt32();
            rec.x = f[6].GetFloat();
            rec.y = f[7].GetFloat();
            rec.z = f[8].GetFloat();
            rec.o = f[9].GetFloat();
            rec.hull = f[10].GetUInt32();
            rec.anchored = true;    // every ship comes back anchored
            rec.readyAt = f[12].GetUInt32();
            rec.upkeepUntil = f[13].GetUInt32();
            rec.created = f[14].GetUInt32();
            rec.bellAt = f[15].GetUInt32();
            rec.lastSweep = f[16].GetUInt32();
            rec.rights = f[17].GetUInt8();
            rec.ownerGuild = f[18].GetUInt32();
            rec.lastRespawnAt = f[19].GetUInt32();
            rec.safeMap = f[20].GetUInt32();
            rec.safeX = f[21].GetFloat();
            rec.safeY = f[22].GetFloat();
            rec.safeZ = f[23].GetFloat();
            if (!S::Info(rec.cls))
                continue;
            m_nextId = std::max(m_nextId, rec.id + 1);
            if (rec.state == S::State::Afloat)
                m_pending.push_back({Pending::Spawn, rec.id, rec.map, rec.x, rec.y, rec.z, rec.o});
            m_ships[rec.id] = rec;
        } while (r->NextRow());
    }
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query("SELECT `ship`, `entry`, `count` FROM `aw_ship_cargo` ORDER BY `ship`, `slot`"))
    {
        do
        {
            Field* f = r->Fetch();
            auto it = m_ships.find(f[0].GetUInt32());
            if (it != m_ships.end())
                it->second.cargo.push_back({f[1].GetUInt32(), f[2].GetUInt32()});
        } while (r->NextRow());
    }
    m_riders.clear();
    if (std::unique_ptr<QueryResult> r = CharacterDatabase.Query(
            "SELECT `guid`, `ship`, `map`, `x`, `y`, `z`, `o`, `wx`, `wy`, `wz` FROM `aw_ship_rider`"))
    {
        do
        {
            Field* f = r->Fetch();
            Rider rd;
            rd.ship = f[1].GetUInt32();
            rd.map = f[2].GetUInt32();
            rd.x = f[3].GetFloat();
            rd.y = f[4].GetFloat();
            rd.z = f[5].GetFloat();
            rd.o = f[6].GetFloat();
            rd.wx = f[7].GetFloat();
            rd.wy = f[8].GetFloat();
            rd.wz = f[9].GetFloat();
            m_riders[f[0].GetUInt32()] = rd;
        } while (r->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Player ships: %u loaded, %u to launch", uint32(m_ships.size()), uint32(m_pending.size()));
}

void ShipMgr::Save(Record const& r) const
{
    std::string name = r.name;
    CharacterDatabase.escape_string(name);
    CharacterDatabase.PExecute(
        "REPLACE INTO `aw_ship` (`id`, `owner`, `class`, `name`, `state`, `map`, `x`, `y`, `z`, `o`, `hull`, `anchored`, "
        "`ready_at`, `upkeep_until`, `created`, `bell_at`, `last_sweep`, `rights`, `owner_guild`, `last_respawn`, "
        "`safe_map`, `safe_x`, `safe_y`, `safe_z`) VALUES "
        "(%u, %u, %u, '%s', %u, %u, %f, %f, %f, %f, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %f, %f, %f)",
        r.id, r.owner, uint32(r.cls), name.c_str(), uint32(r.state), r.map, r.x, r.y, r.z, r.o, r.hull,
        r.anchored ? 1u : 0u, r.readyAt, r.upkeepUntil, r.created, r.bellAt, r.lastSweep, uint32(r.rights), r.ownerGuild,
        r.lastRespawnAt, r.safeMap, r.safeX, r.safeY, r.safeZ);
}

void ShipMgr::Queue(Pending p)
{
    m_pending.push_back(p);     // under m_lock
}

void ShipMgr::Publish(PlayerShip const& ship, bool save)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    auto it = m_ships.find(ship.ShipId());
    if (it == m_ships.end())
        return;
    Record& r = it->second;
    r.map = ship.GetMapId();
    r.x = ship.GetPositionX();
    r.y = ship.GetPositionY();
    r.z = ship.GetPositionZ();
    r.o = ship.GetOrientation();
    r.hull = ship.hull.load();
    r.anchored = ship.Anchored();
    r.bellAt = ship.bellAt.load();
    if (save)
        Save(r);
}

PlayerShip* ShipMgr::Find(Map* map, uint32 shipId) const
{
    if (!map)
        return nullptr;
    ObjectGuid go;
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        auto it = m_ships.find(shipId);
        if (it == m_ships.end() || it->second.map != map->GetId())
            return nullptr;
        go = it->second.go;
    }
    if (go.IsEmpty())
        return nullptr;
    GenericTransport* t = map->GetTransport(go);
    return t && S::InfoByEntry(t->GetEntry()) ? static_cast<PlayerShip*>(t) : nullptr;
}

PlayerShip* ShipMgr::ShipOf(Player const* player) const
{
    GenericTransport* t = player ? player->GetTransport() : nullptr;
    return t && S::InfoByEntry(t->GetEntry()) && t->GetGoType() == GAMEOBJECT_TYPE_TRANSPORT
        ? static_cast<PlayerShip*>(t) : nullptr;
}

void ShipMgr::Spawn(Record& r)
{
    S::ClassInfo const* info = S::Info(r.cls);
    Map* map = info ? sMapMgr.FindMap(r.map) : nullptr;
    if (!map || !MaNGOS::IsValidMapCoord(r.x, r.y, r.z, r.o))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Player ships: ship %u can't be launched on map %u (no such open map, or a bad pose)", r.id, r.map);
        return;
    }
    if (!r.go.IsEmpty())
        Despawn(r);
    PlayerShip* ship = new PlayerShip(r.id, *info);
    uint32 const low = map->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT);
    if (!ship->Create(low, info->goEntry, map, r.x, r.y, r.z, r.o, 0.f, 0.f, std::sin(r.o / 2.f), std::cos(r.o / 2.f)))
    {
        delete ship;
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Player ships: gameobject %u is missing (36_player_ships.sql)", info->goEntry);
        return;
    }
    ship->hull.store(r.hull);
    ship->upkeepUntil.store(r.upkeepUntil);
    ship->bellAt.store(r.bellAt);
    ship->owner = ObjectGuid(HIGHGUID_PLAYER, r.owner);
    ship->name = r.name;
    ship->rights.store(r.rights);
    ship->ownerGuild.store(r.ownerGuild);
    ship->SetAnchored(r.anchored);
    ship->Place();
    r.go = ship->GetObjectGuid();
    map->Add<ElevatorTransport>(ship);
}

void ShipMgr::Despawn(Record& r)
{
    if (r.go.IsEmpty())
        return;
    if (Map* map = sMapMgr.FindMap(r.map))
        if (GenericTransport* t = map->GetTransport(r.go))
            if (S::InfoByEntry(t->GetEntry()))
            {
                PlayerShip* ship = static_cast<PlayerShip*>(t);
                ship->LeaveHelm(true);
                ship->LeaveAllGuns();
                for (auto const& ref : map->GetPlayers())
                    if (Player* p = ref.getSource())
                        sFusionMgr.SendChannel(p, S::kChannel, S::EncodeGone(r.go.GetRawValue()));
                map->Remove<ElevatorTransport>(ship, true);
            }
    r.go.Clear();
}

void ShipMgr::Update()
{
    uint32 const now = Now();
    std::lock_guard<std::recursive_mutex> g(m_lock);
    // Wrecks: out of the world, the row kept.
    for (auto& [id, r] : m_ships)
    {
        if (r.go.IsEmpty())
            continue;
        Map* map = sMapMgr.FindMap(r.map);
        GenericTransport* t = map ? map->GetTransport(r.go) : nullptr;
        if (!t || !S::InfoByEntry(t->GetEntry()))
            continue;
        PlayerShip* ship = static_cast<PlayerShip*>(t);
        if (ship->wrecked.load())
        {
            r.hull = 0;
            r.state = S::State::Wrecked;
            r.x = ship->GetPositionX();
            r.y = ship->GetPositionY();
            r.z = ship->GetPositionZ();
            Despawn(r);
            Save(r);
        }
    }
    std::vector<Pending> todo;
    todo.swap(m_pending);
    for (Pending const& p : todo)
    {
        auto it = m_ships.find(p.id);
        if (it == m_ships.end())
            continue;
        Record& r = it->second;
        switch (p.kind)
        {
            case Pending::Despawn:
                Despawn(r);
                break;
            case Pending::Erase:
                Despawn(r);
                CharacterDatabase.PExecute("DELETE FROM `aw_ship` WHERE `id` = %u", r.id);
                m_ships.erase(it);
                break;
            case Pending::Move:
                Despawn(r);
                r.map = p.map;
                r.x = p.x;
                r.y = p.y;
                r.z = p.z;
                r.o = p.o;
                r.anchored = true;
                Save(r);
                [[fallthrough]];
            case Pending::Spawn:
                if (r.state == S::State::Afloat)
                    Spawn(r);
                break;
        }
    }
    if (now >= m_sweepAt)
    {
        m_sweepAt = now + 3600;
        Sweep(now);
    }
}

// The hourly upkeep sweep (m_lock held): neglect wears the hull; impounded ships are wrecked.
void ShipMgr::Sweep(uint32 now)
{
    for (auto& [id, r] : m_ships)
    {
        S::ClassInfo const* info = S::Info(r.cls);
        if (!info || r.state != S::State::Afloat)
            continue;
        uint32 const from = std::max(r.lastSweep, r.upkeepUntil);
        uint32 wear = S::NeglectWear(*info, r.upkeepUntil, from, now);
        r.lastSweep = now;
        PlayerShip* ship = nullptr;
        if (!r.go.IsEmpty())
            if (Map* map = sMapMgr.FindMap(r.map))
                if (GenericTransport* t = map->GetTransport(r.go))
                    ship = static_cast<PlayerShip*>(t);
        if (S::Impounded(now, r.upkeepUntil))
            wear = r.hull;
        if (ship)
        {
            if (wear)
                ship->Damage(wear);
            r.hull = ship->hull.load();
        }
        else
            r.hull = wear >= r.hull ? 0 : r.hull - wear;
        if (!r.hull && !ship)
            r.state = S::State::Wrecked;
        Save(r);
    }
}

bool ShipMgr::Owns(uint32 owner, S::Class c, Record* out) const
{
    for (auto const& [id, r] : m_ships)
        if (r.owner == owner && r.cls == c)
        {
            if (out)
                *out = r;
            return true;
        }
    return false;
}

std::vector<ShipMgr::Record> ShipMgr::RecordsOf(uint32 owner) const
{
    std::vector<Record> v;
    std::lock_guard<std::recursive_mutex> g(m_lock);
    for (auto const& [id, r] : m_ships)
        if (r.owner == owner)
            v.push_back(r);
    return v;
}

bool ShipMgr::Docked(Record const& r, Creature const* at) const
{
    return at && r.map == at->GetMapId()
        && std::hypot(r.x - at->GetPositionX(), r.y - at->GetPositionY()) <= S::kDockRadius;
}

// Where a Shipwright launches a ship: a sailing ship on deep water near the dock (searched in
// rings outward, each spot checked for the ship's length around it); an airship over the dock.
bool ShipMgr::LaunchPoint(Creature const* at, S::ClassInfo const& info, float& x, float& y, float& z, float& o) const
{
    TerrainInfo const* t = at->GetMap()->GetTerrain();
    float const cx = at->GetPositionX(), cy = at->GetPositionY(), cz = at->GetPositionZ();
    if (info.climbRate > 0.f)
    {
        x = cx;
        y = cy;
        float const ground = t->GetHeightStatic(cx, cy, cz + 5.f, true, 200.f);
        z = std::max(cz, ground) + 40.f;
        o = at->GetOrientation();
        return true;
    }
    for (float r = 30.f; r <= 150.f; r += 10.f)
        for (int i = 0; i < 24; ++i)
        {
            float const a = at->GetOrientation() + float(i) * 6.2831853f / 24.f;
            float const px = cx + std::cos(a) * r, py = cy + std::sin(a) * r;
            bool ok = true;
            float level = 0.f;
            for (auto [dx, dy] : {std::pair<float, float>{0, 0}, {20, 0}, {-20, 0}, {0, 20}, {0, -20}})
            {
                float ground = VMAP_INVALID_HEIGHT_VALUE;
                float const w = SailWater(t, px + dx, py + dy, cz + 5.f, &ground);
                if (w <= VMAP_INVALID_HEIGHT_VALUE + 1.f || (ground > VMAP_INVALID_HEIGHT_VALUE + 1.f && w - ground < info.draft + 2.f))
                {
                    ok = false;
                    break;
                }
                if (dx == 0 && dy == 0)
                    level = w;
            }
            if (!ok)
                continue;
            x = px;
            y = py;
            z = level;
            o = a;  // bow out, away from the dock
            return true;
        }
    return false;
}

// A GM's ship at their feet: on the water they stand in or over, or in the air around them.
bool ShipMgr::LaunchHere(Player const* at, S::ClassInfo const& info, float& x, float& y, float& z, float& o) const
{
    x = at->GetPositionX();
    y = at->GetPositionY();
    o = at->GetOrientation();
    if (info.climbRate > 0.f)
    {
        z = at->GetPositionZ() + 30.f;
        return true;
    }
    float ground = VMAP_INVALID_HEIGHT_VALUE;
    float const w = SailWater(at->GetMap()->GetTerrain(), x, y, at->GetPositionZ() + 5.f, &ground);
    if (w <= VMAP_INVALID_HEIGHT_VALUE + 1.f)
        return false;
    z = w;
    return true;
}

void ShipMgr::Commission(Player* player, Creature* creature, S::ClassInfo const& info)
{
    S::Purse purse;
    purse.copper = player->GetMoney();
    purse.hasDeed = player->GetItemCount(info.deedItem) > 0;
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        purse.ownsClass = Owns(player->GetGUIDLow(), info.cls);
    }
    for (S::Material const& m : info.materials)
        purse.items.push_back({m.item, player->GetItemCount(m.item)});
    S::Refusal const why = S::CanCommission(info, purse);
    if (why != S::Refusal::None)
    {
        Tell(player, S::RefusalText(why));
        if (why == S::Refusal::NoMaterials)
            for (S::Material const& m : info.materials)
            {
                uint32 const have = player->GetItemCount(m.item);
                if (have < m.count)
                    ChatHandler(player).PSendSysMessage("  %s: %u of %u", m.name, have, m.count);
            }
        return;
    }
    player->DestroyItemCount(info.deedItem, 1, true);
    for (S::Material const& m : info.materials)
        player->DestroyItemCount(m.item, m.count, true);
    player->ModifyMoney(-int32(info.buildCopper));
    uint32 const now = Now();
    Record r;
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        r.id = m_nextId++;
        r.owner = player->GetGUIDLow();
        r.cls = info.cls;
        r.name = S::DefaultName(player->GetName(), info);
        r.state = S::State::Building;
        r.map = creature->GetMapId();
        r.hull = info.maxHull;
        r.readyAt = now + info.buildMinutes * 60;
        r.upkeepUntil = now + S::kUpkeepBlockDays * S::kDaySec;
        r.created = now;
        r.lastSweep = now;
        m_ships[r.id] = r;
        Save(r);
    }
    sFusionMgr.CountStat(player, {Fusion::Stat::ShipsCommissioned, Fusion::kAnyKey});
    ChatHandler(player).PSendSysMessage("|cff88ccffThe keel of %s is laid. Come back to a Shipwright in %s to launch it.|r",
        r.name.c_str(), Duration(info.buildMinutes * 60).c_str());
}

bool ShipMgr::GossipHello(Player* player, Creature* creature)
{
    if (!player || !creature)
        return false;
    PlayerMenu* menu = player->PlayerTalkClass;
    menu->ClearMenus();
    GossipMenu& g = menu->GetGossipMenu();
    uint32 const now = Now();
    std::vector<Record> const mine = RecordsOf(player->GetGUIDLow());
    for (S::ClassInfo const& info : S::Classes())
    {
        uint32 const c = uint32(info.cls);
        Record const* r = nullptr;
        for (Record const& x : mine)
            if (x.cls == info.cls)
                r = &x;
        if (!r)
        {
            std::ostringstream o;
            o << "Commission a " << info.name << " (" << Money(info.buildCopper) << ", the deed and materials)";
            g.AddMenuItem(GOSSIP_ICON_MONEY_BAG, o.str(), kGossipSender, kCommission * 16 + c, "", false);
            continue;
        }
        if (r->state == S::State::Building)
        {
            if (now >= r->readyAt)
                g.AddMenuItem(GOSSIP_ICON_TAXI, "Launch " + r->name + ".", kGossipSender, kLaunch * 16 + c, "", false);
            else
                g.AddMenuItem(GOSSIP_ICON_CHAT, r->name + " is being built: ready in " + Duration(r->readyAt - now) + ".",
                    kGossipSender, kExplain * 16 + c, "", false);
            continue;
        }
        if (r->state == S::State::Wrecked)
        {
            g.AddMenuItem(GOSSIP_ICON_MONEY_BAG, "Salvage " + r->name + " and rebuild it here (" + Money(S::SalvageCost(info)) + ")",
                kGossipSender, kSalvage * 16 + c, "", false);
            continue;
        }
        if (!Docked(*r, creature))
            g.AddMenuItem(GOSSIP_ICON_TAXI, "Bring " + r->name + " to this dock (" + Money(info.summonFee) + ")",
                kGossipSender, kSummon * 16 + c, "", false);
        if (uint32 cost = S::RepairCost(info, r->hull))
            g.AddMenuItem(GOSSIP_ICON_INTERACT_1, "Repair " + r->name + ", " + std::to_string(S::HullPct(info, r->hull)) + "% hull ("
                + Money(cost) + ")", kGossipSender, kRepair * 16 + c, "", false);
        std::string const due = now > r->upkeepUntil ? "overdue!"
            : "paid for " + std::to_string((r->upkeepUntil - now) / S::kDaySec) + " more days";
        g.AddMenuItem(GOSSIP_ICON_MONEY_BAG, "Pay 7 days' upkeep for " + r->name + " (" + Money(S::UpkeepBlockCost(info)) + "; " + due + ")",
            kGossipSender, kUpkeep * 16 + c, "", false);
        // The crew's rights, one toggle each.
        for (uint8 bit = 1; bit <= S::RIGHT_GUILD_RESPAWN; bit <<= 1)
        {
            bool const on = r->rights & bit;
            g.AddMenuItem(GOSSIP_ICON_INTERACT_1, std::string(on ? "[x] " : "[  ] ") + r->name + ": " + S::RightName(bit),
                kRightsSender, c * 256 + bit, "", false);
        }
    }
    g.AddMenuItem(GOSSIP_ICON_VENDOR, "Show me the ship deeds.", kGossipSender, kDeeds * 16, "", false);
    g.AddMenuItem(GOSSIP_ICON_CHAT, "How do ships work?", kGossipSender, kExplain * 16, "", false);
    menu->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, creature->GetObjectGuid());
    return true;
}

bool ShipMgr::GossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (!player || !creature || (sender != kGossipSender && sender != kRightsSender))
        return false;
    if (sender == kRightsSender)
    {
        uint8 const bit = uint8(action % 256);
        S::Class const cls = S::Class(action / 256);
        {
            std::lock_guard<std::recursive_mutex> g(m_lock);
            for (auto& [id, x] : m_ships)
                if (x.owner == player->GetGUIDLow() && x.cls == cls && (bit & S::RIGHT_ALL))
                {
                    x.rights ^= bit;
                    x.ownerGuild = player->GetGuildId();
                    if (!x.go.IsEmpty())
                        if (Map* map = sMapMgr.FindMap(x.map))
                            if (GenericTransport* t = map->GetTransport(x.go))
                            {
                                static_cast<PlayerShip*>(t)->rights.store(x.rights);
                                static_cast<PlayerShip*>(t)->ownerGuild.store(x.ownerGuild);
                            }
                    Save(x);
                    ChatHandler(player).PSendSysMessage("|cff88ccff%s: %s %s.|r", x.name.c_str(), S::RightName(bit),
                        (x.rights & bit) ? "(on)" : "(off)");
                }
        }
        // Back to the menu, to set the next.
        return GossipHello(player, creature);
    }
    player->PlayerTalkClass->CloseGossip();
    uint32 const what = action / 16;
    S::ClassInfo const* info = S::Info(S::Class(action % 16));
    uint32 const now = Now();
    if (what == kDeeds)
    {
        player->GetSession()->SendListInventory(creature->GetObjectGuid());
        return true;
    }
    if (what == kExplain || !info)
    {
        Tell(player, "A ship takes a deed (sold here), gold and rare materials, then hours to build. Board it, "
            "take the helm from the Ship frame: W and S set the telegraph, A and D steer, Space and X climb and dive "
            "(airships). Anyone may ride on deck. Pay upkeep here weekly, repair it here when it's moored, and ring the "
            "Ship's Bell to return aboard from anywhere.");
        for (S::ClassInfo const& i : S::Classes())
        {
            std::ostringstream o;
            o << i.name << ": deed " << Money(i.deedPrice) << ", " << Money(i.buildCopper) << ",";
            for (S::Material const& m : i.materials)
                o << " " << m.count << " " << m.name << ",";
            o << " " << i.buildMinutes / 60 << "h to build; upkeep " << Money(i.upkeepPerDay) << " a day.";
            Tell(player, o.str().c_str());
        }
        return true;
    }
    if (what == kCommission)
    {
        Commission(player, creature, *info);
        return true;
    }
    std::lock_guard<std::recursive_mutex> g(m_lock);
    Record* r = nullptr;
    for (auto& [id, x] : m_ships)
        if (x.owner == player->GetGUIDLow() && x.cls == info->cls)
            r = &x;
    if (!r)
        return true;
    auto pay = [&](uint32 copper) {
        if (player->GetMoney() < copper)
        {
            Tell(player, S::RefusalText(S::Refusal::NoGold));
            return false;
        }
        player->ModifyMoney(-int32(copper));
        return true;
    };
    auto launch = [&]() {
        float x, y, z, o;
        if (!LaunchPoint(creature, *info, x, y, z, o))
        {
            Tell(player, "There is no deep water near this dock for a ship.");
            return false;
        }
        r->state = S::State::Afloat;
        SetSafe(*r, creature->GetMapId(), creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());
        Queue({Pending::Move, r->id, creature->GetMapId(), x, y, z, o});
        return true;
    };
    switch (what)
    {
        case kLaunch:
            if (r->state != S::State::Building || now < r->readyAt)
                Tell(player, S::RefusalText(S::Refusal::NotReady));
            else if (launch())
                ChatHandler(player).PSendSysMessage("|cff88ccff%s is launched! It's moored by the dock, anchored.|r", r->name.c_str());
            break;
        case kSalvage:
            if (r->state != S::State::Wrecked)
                break;
            if (!pay(S::SalvageCost(*info)))
                break;
            r->hull = info->maxHull;
            if (launch())
                ChatHandler(player).PSendSysMessage("|cff88ccff%s is raised and rebuilt.|r", r->name.c_str());
            else
            {
                r->state = S::State::Wrecked;
                player->ModifyMoney(int32(S::SalvageCost(*info)));
            }
            break;
        case kSummon:
        {
            PlayerShip* ship = nullptr;
            if (Map* map = sMapMgr.FindMap(r->map))
                if (GenericTransport* t = r->go.IsEmpty() ? nullptr : map->GetTransport(r->go))
                    ship = static_cast<PlayerShip*>(t);
            // The aboard and pilot checks read the other map's ship: the worst a race does is a
            // refusal or a passenger left on the ship's way (the despawn takes them off).
            S::Refusal why = S::CanSummon(true, r->state, ship && !ship->Pilot().IsEmpty(), ship && ship->HasPassengers());
            if (why != S::Refusal::None)
            {
                Tell(player, S::RefusalText(why));
                break;
            }
            if (!pay(info->summonFee))
                break;
            if (!launch())
                player->ModifyMoney(int32(info->summonFee));
            else
                ChatHandler(player).PSendSysMessage("|cff88ccff%s is brought round to this dock.|r", r->name.c_str());
            break;
        }
        case kRepair:
        {
            S::Refusal why = S::CanRepair(true, r->state, Docked(*r, creature), r->hull, info->maxHull);
            if (why != S::Refusal::None)
            {
                Tell(player, S::RefusalText(why));
                break;
            }
            if (!pay(S::RepairCost(*info, r->hull)))
                break;
            r->hull = info->maxHull;
            if (PlayerShip* ship = r->go.IsEmpty() || r->map != creature->GetMapId() ? nullptr
                    : static_cast<PlayerShip*>(creature->GetMap()->GetTransport(r->go)))
                ship->hull.store(info->maxHull);
            Save(*r);
            ChatHandler(player).PSendSysMessage("|cff88ccff%s is repaired.|r", r->name.c_str());
            break;
        }
        case kUpkeep:
        {
            if (!pay(S::UpkeepBlockCost(*info)))
                break;
            r->upkeepUntil = std::max(r->upkeepUntil, now) + S::kUpkeepBlockDays * S::kDaySec;
            r->lastSweep = now;
            if (!r->go.IsEmpty())
                if (Map* map = sMapMgr.FindMap(r->map))
                    if (GenericTransport* t = map->GetTransport(r->go))
                        static_cast<PlayerShip*>(t)->upkeepUntil.store(r->upkeepUntil);
            Save(*r);
            ChatHandler(player).PSendSysMessage("|cff88ccffUpkeep for %s is paid for %u days.|r", r->name.c_str(),
                (r->upkeepUntil - now) / S::kDaySec);
            break;
        }
        default:
            break;
    }
    return true;
}

void ShipMgr::Ring(Player* player, PlayerShip* aboard)
{
    uint32 const now = Now();
    Record r;
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        Record* found = nullptr;
        for (auto& [id, x] : m_ships)
            if (x.owner == player->GetGUIDLow() && x.state == S::State::Afloat && !x.go.IsEmpty())
                found = &x;
        if (!found)
        {
            Tell(player, "You have no ship afloat.");
            return;
        }
        S::Refusal why = S::CanRing(true, found->state, now, found->bellAt);
        if (why != S::Refusal::None)
        {
            ChatHandler(player).PSendSysMessage("|cff88ccff%s (%s)|r", S::RefusalText(why),
                Duration(found->bellAt + S::kBellCooldownSec - now).c_str());
            return;
        }
        if (aboard && aboard->ShipId() == found->id)
        {
            Tell(player, "You are already aboard.");
            return;
        }
        found->bellAt = now;
        r = *found;
        Save(r);
    }
    S::ClassInfo const* info = S::Info(r.cls);
    if (Map* map = sMapMgr.FindMap(r.map))
        if (GenericTransport* t = map->GetTransport(r.go))
            static_cast<PlayerShip*>(t)->bellAt.store(now);
    // Onto the deck: a little over it, so the body falls onto the planks and the client boards.
    player->TeleportTo(r.map, r.x, r.y, r.z + info->deckZ + 1.5f, r.o);
    ChatHandler(player).PSendSysMessage("|cff88ccffThe Ship's Bell rings you back aboard %s.|r", r.name.c_str());
}

void ShipMgr::HandleChannel(Player* player, std::vector<uint8> const& body)
{
    uint8 op = 0, keys = 0;
    if (!player || !player->IsInWorld())
        return;
    PlayerShip* ship = ShipOf(player);
    if (!S::DecodeClient(body, op, keys))
    {
        S::CrewAsk ask;
        if (S::DecodeCrew(body, ask))
            HandleCrew(player, ship, ask);
        return;
    }
    // The helm and the anchor: the owner, or a helmsman by the owner's rights.
    bool const helmsman = ship && S::May(StandingOf(player, *ship), ship->rights.load(), S::Duty::Helm);
    switch (op)
    {
        case S::kOpTakeHelm:
        {
            S::Refusal why = S::CanTakeHelm(helmsman, ship != nullptr, S::State::Afloat, ship && !ship->Pilot().IsEmpty());
            if (!ship)
                why = S::Refusal::NotAboard;
            else if (why == S::Refusal::None)
                why = S::HelmBodyRefusal(HelmBodyOf(*ship, player));
            if (why != S::Refusal::None)
            {
                Tell(player, S::RefusalText(why));
                return;
            }
            if (ship->GunOf(player->GetObjectGuid()) != S::kNoMount)
                ship->LeaveGun(player, true);
            ship->TakeHelm(player);
            if (ship->Anchored())
                Tell(player, "You take the helm. The anchor is down: weigh it to sail.");
            else
                Tell(player, "You take the helm.");
            return;
        }
        case S::kOpLeaveHelm:
            if (ship && ship->Pilot() == player->GetObjectGuid())
                ship->LeaveHelm(true);
            else
                sFusionMgr.SendChannel(player, S::kChannel, S::EncodeHelm(0));
            return;
        case S::kOpKeys:
            if (ship && ship->Pilot() == player->GetObjectGuid())
                ship->SetKeys(keys);
            return;
        case S::kOpAnchor:
        {
            if (!helmsman)
            {
                Tell(player, S::RefusalText(ship ? S::Refusal::NotOwner : S::Refusal::NotAboard));
                return;
            }
            if (!ship->Anchored())
            {
                S::Refusal why = S::CanAnchor(ship->Speed());
                if (why != S::Refusal::None)
                {
                    Tell(player, S::RefusalText(why));
                    return;
                }
                ship->SetAnchored(true);
                Tell(player, "The anchor drops.");
            }
            else
            {
                ship->SetAnchored(false);
                Tell(player, "Anchor's aweigh.");
            }
            ship->SendPoseTo(player);
            return;
        }
        case S::kOpBell:
            Ring(player, ship);
            return;
        default:
            return;
    }
}

bool ShipMgr::Command(Player* gm, std::string const& args)
{
    std::istringstream in(args);
    std::string what;
    in >> what;
    ChatHandler chat(gm);
    Player* target = gm;
    if (gm->GetSelectionGuid().IsPlayer())
        if (Player* sel = gm->GetMap()->GetPlayer(gm->GetSelectionGuid()))
            target = sel;
    auto classArg = [&]() -> S::ClassInfo const* {
        std::string c;
        in >> c;
        return S::InfoByName(c);
    };
    uint32 const now = Now();
    if (what == "test")
    {
        // A ship for the GM's own testing: free (no deed, gold, materials or build time), afloat
        // and anchored beside them. An existing one of the class is restored and moved here.
        S::ClassInfo const* info = classArg();
        if (!info)
        {
            chat.SendSysMessage(".ship test airship|sail: a free ship of that class for you, afloat beside you, ready to board.");
            return true;
        }
        float x, y, z;
        float const o = gm->GetOrientation();
        S::BesideSpot(gm->GetPositionX(), gm->GetPositionY(), o, x, y);
        if (info->climbRate > 0.f)
            z = gm->GetPositionZ() + 2.f;   // hovering at deck height beside the GM
        else
        {
            float ground = VMAP_INVALID_HEIGHT_VALUE;
            z = SailWater(gm->GetMap()->GetTerrain(), x, y, gm->GetPositionZ() + 5.f, &ground);
            if (z <= VMAP_INVALID_HEIGHT_VALUE + 1.f)
            {
                chat.SendSysMessage("A sloop needs water beside you: stand on a dock or a shore facing along it, with the sea to your right.");
                return true;
            }
        }
        std::lock_guard<std::recursive_mutex> g(m_lock);
        Record existing;
        if (Owns(gm->GetGUIDLow(), info->cls, &existing))
        {
            Record& r = m_ships[existing.id];
            r.state = S::State::Afloat;
            r.hull = info->maxHull;
            r.readyAt = std::min(r.readyAt, now);
            r.upkeepUntil = std::max(r.upkeepUntil, now + S::kUpkeepBlockDays * S::kDaySec);
            if (!gm->IsInWater())
                SetSafe(r, gm->GetMapId(), gm->GetPositionX(), gm->GetPositionY(), gm->GetPositionZ());
            Save(r);
            Queue({Pending::Move, r.id, gm->GetMapId(), x, y, z, o});
            chat.PSendSysMessage("%s is restored and brought beside you.", r.name.c_str());
            return true;
        }
        Record r;
        r.id = m_nextId++;
        r.owner = gm->GetGUIDLow();
        r.cls = info->cls;
        r.name = S::DefaultName(gm->GetName(), *info);
        r.state = S::State::Afloat;
        r.map = gm->GetMapId();
        r.x = x;
        r.y = y;
        r.z = z;
        r.o = o;
        r.hull = info->maxHull;
        r.readyAt = now;
        r.upkeepUntil = now + S::kUpkeepBlockDays * S::kDaySec;
        r.created = r.lastSweep = now;
        if (!gm->IsInWater())
            SetSafe(r, gm->GetMapId(), gm->GetPositionX(), gm->GetPositionY(), gm->GetPositionZ());
        m_ships[r.id] = r;
        Save(r);
        Queue({Pending::Spawn, r.id, r.map, x, y, z, o});
        chat.PSendSysMessage("%s (ship %u) is afloat beside you, anchored. Board it and take the helm.", r.name.c_str(), r.id);
        return true;
    }
    if (what == "give")
    {
        S::ClassInfo const* info = classArg();
        if (!info)
        {
            chat.SendSysMessage(".ship give sloop|airship: a ship of that class for the selected player (or you), afloat at your feet "
                "(a sloop needs you on or over water).");
            return true;
        }
        float x, y, z, o;
        if (!LaunchHere(gm, *info, x, y, z, o))
        {
            chat.SendSysMessage("A sloop needs water here: stand on a dock over the sea, or swim.");
            return true;
        }
        std::lock_guard<std::recursive_mutex> g(m_lock);
        Record existing;
        if (Owns(target->GetGUIDLow(), info->cls, &existing))
        {
            Record& r = m_ships[existing.id];
            r.state = S::State::Afloat;
            r.hull = info->maxHull;
            r.upkeepUntil = std::max(r.upkeepUntil, now + S::kUpkeepBlockDays * S::kDaySec);
            if (!gm->IsInWater())
                SetSafe(r, gm->GetMapId(), gm->GetPositionX(), gm->GetPositionY(), gm->GetPositionZ());
            Queue({Pending::Move, r.id, gm->GetMapId(), x, y, z, o});
            chat.PSendSysMessage("%s is restored and brought here.", r.name.c_str());
            return true;
        }
        Record r;
        r.id = m_nextId++;
        r.owner = target->GetGUIDLow();
        r.cls = info->cls;
        r.name = S::DefaultName(target->GetName(), *info);
        r.state = S::State::Afloat;
        r.map = gm->GetMapId();
        r.x = x;
        r.y = y;
        r.z = z;
        r.o = o;
        r.hull = info->maxHull;
        r.upkeepUntil = now + S::kUpkeepBlockDays * S::kDaySec;
        r.created = r.lastSweep = now;
        if (!gm->IsInWater())
            SetSafe(r, gm->GetMapId(), gm->GetPositionX(), gm->GetPositionY(), gm->GetPositionZ());
        m_ships[r.id] = r;
        Save(r);
        Queue({Pending::Spawn, r.id, r.map, x, y, z, o});
        chat.PSendSysMessage("%s (ship %u) launches here, anchored.", r.name.c_str(), r.id);
        return true;
    }
    if (what == "list")
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        for (auto const& [id, r] : m_ships)
        {
            S::ClassInfo const* info = S::Info(r.cls);
            char const* state = r.state == S::State::Afloat ? "afloat" : r.state == S::State::Wrecked ? "wrecked" : "building";
            chat.PSendSysMessage("%u: %s (owner %u) %s, map %u (%.0f, %.0f, %.0f), hull %u%%, upkeep %s%s", id, r.name.c_str(),
                r.owner, state, r.map, r.x, r.y, r.z, info ? S::HullPct(*info, r.hull) : 0,
                now > r.upkeepUntil ? "OVERDUE " : "", Duration(now > r.upkeepUntil ? now - r.upkeepUntil : r.upkeepUntil - now).c_str());
        }
        if (m_ships.empty())
            chat.SendSysMessage("No ships.");
        return true;
    }
    // The rest act on the target's ship of a class (or their only one).
    auto pick = [&](S::ClassInfo const* info) -> Record* {
        Record* found = nullptr;
        for (auto& [id, r] : m_ships)
            if (r.owner == target->GetGUIDLow() && (!info || r.cls == info->cls))
                found = &r;
        return found;
    };
    auto liveShip = [&](Record const& r) -> PlayerShip* {
        if (r.go.IsEmpty())
            return nullptr;
        Map* map = sMapMgr.FindMap(r.map);
        GenericTransport* t = map ? map->GetTransport(r.go) : nullptr;
        return t ? static_cast<PlayerShip*>(t) : nullptr;
    };
    if (what == "hull")
    {
        uint32 pct = 100;
        in >> pct;
        std::lock_guard<std::recursive_mutex> g(m_lock);
        Record* r = pick(nullptr);
        if (!r)
        {
            chat.SendSysMessage("No ship.");
            return true;
        }
        S::ClassInfo const* info = S::Info(r->cls);
        uint32 const hull = info->maxHull * std::min(pct, 100u) / 100;
        if (PlayerShip* s = liveShip(*r))
        {
            if (hull < s->hull.load())
                s->Damage(s->hull.load() - hull);
            else
                s->hull.store(hull);
        }
        r->hull = hull;
        Save(*r);
        chat.PSendSysMessage("%s: hull %u%%.", r->name.c_str(), std::min(pct, 100u));
        return true;
    }
    if (what == "upkeep")
    {
        int32 days = 0;
        in >> days;
        std::lock_guard<std::recursive_mutex> g(m_lock);
        Record* r = pick(nullptr);
        if (!r)
        {
            chat.SendSysMessage("No ship.");
            return true;
        }
        r->upkeepUntil = uint32(int64(now) + int64(days) * S::kDaySec);
        r->lastSweep = std::min(r->lastSweep, r->upkeepUntil);
        if (PlayerShip* s = liveShip(*r))
            s->upkeepUntil.store(r->upkeepUntil);
        Save(*r);
        m_sweepAt = 0;  // sweep now
        chat.PSendSysMessage("%s: upkeep paid until %d days from now (negative: overdue).", r->name.c_str(), days);
        return true;
    }
    if (what == "finish")
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        for (auto& [id, r] : m_ships)
            if (r.owner == target->GetGUIDLow() && r.state == S::State::Building)
            {
                r.readyAt = now;
                Save(r);
                chat.PSendSysMessage("%s is finished: launch it at a Shipwright.", r.name.c_str());
            }
        return true;
    }
    if (what == "summon")
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        Record* r = pick(classArg());
        S::ClassInfo const* info = r ? S::Info(r->cls) : nullptr;
        float x, y, z, o;
        if (!r || !LaunchHere(gm, *info, x, y, z, o))
        {
            chat.SendSysMessage(r ? "A sloop needs water here." : "No ship.");
            return true;
        }
        r->state = S::State::Afloat;
        Queue({Pending::Move, r->id, gm->GetMapId(), x, y, z, o});
        chat.PSendSysMessage("%s is brought here.", r->name.c_str());
        return true;
    }
    if (what == "bell")
    {
        {
            std::lock_guard<std::recursive_mutex> g(m_lock);
            if (Record* r = pick(nullptr))
                r->bellAt = 0;
        }
        Ring(gm, ShipOf(gm));
        return true;
    }
    if (what == "delete")
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        Record* r = pick(classArg());
        if (!r)
        {
            chat.SendSysMessage("No ship.");
            return true;
        }
        chat.PSendSysMessage("%s is deleted.", r->name.c_str());
        Queue({Pending::Erase, r->id, 0, 0.f, 0.f, 0.f, 0.f});
        return true;
    }
    chat.SendSysMessage(".ship test airship|sail | give sloop|airship | list | hull <pct> | upkeep <days> | finish | summon [class] | bell | delete [class]. "
        "Acts on the selected player's ships, or yours.");
    return true;
}

bool ChatHandler::HandleShipCommand(char* args)
{
    Player* gm = GetSession() ? GetSession()->GetPlayer() : nullptr;
    if (!gm)
        return false;
    return sShipMgr.Command(gm, args ? std::string(args) : std::string());
}

// ── The second slice: crew, deck guns, hull damage, cargo, respawn aboard, logout safety ──────

uint8 PlayerShip::GunOf(ObjectGuid crewman) const
{
    for (size_t i = 0; i < m_gunners.size(); ++i)
        if (m_gunners[i] == crewman)
            return uint8(i);
    return S::kNoMount;
}

uint8 PlayerShip::FreeMountNear(Player const* p) const
{
    std::vector<S::Mount> const& mounts = S::MountsOf(m_info.cls);
    uint8 best = S::kNoMount;
    float bestD = S::kMountReach * 2.f;
    for (size_t i = 0; i < mounts.size() && i < m_gunners.size(); ++i)
    {
        if (!m_gunners[i].IsEmpty())
            continue;
        Fusion::Vec3 const at = S::MountWorld(mounts[i], m_motion.x, m_motion.y, m_motion.z, m_motion.o);
        float const d = std::hypot(p->GetPositionX() - at.x, p->GetPositionY() - at.y);
        if (d <= bestD)
        {
            bestD = d;
            best = uint8(i);
        }
    }
    return best;
}

bool PlayerShip::ManGun(Player* p, uint8 mount)
{
    std::vector<S::Mount> const& mounts = S::MountsOf(m_info.cls);
    if (mount >= mounts.size() || mount >= m_gunners.size() || !m_gunners[mount].IsEmpty())
        return false;
    if (!sFusionMgr.MountGun(p, mounts[mount].gun))
        return false;
    m_gunners[mount] = p->GetObjectGuid();
    p->SetRooted(true);
    sFusionMgr.SendChannel(p, S::kChannel, S::EncodeGun(GetObjectGuid().GetRawValue(), mount));
    ChatHandler(p).PSendSysMessage("|cff88ccffYou man the %s. Fire as with any gun; Leave the Gun (or Escape) to step away.|r",
        mounts[mount].name);
    return true;
}

void PlayerShip::LeaveGun(Player* p, bool tell)
{
    uint8 const i = GunOf(p->GetObjectGuid());
    if (i == S::kNoMount)
        return;
    m_gunners[i].Clear();
    sFusionMgr.UnmountGun(p);
    if (p->IsAlive())
        p->SetRooted(false);
    if (tell)
        sFusionMgr.SendChannel(p, S::kChannel, S::EncodeGun(GetObjectGuid().GetRawValue(), S::kNoMount));
}

void PlayerShip::LeaveAllGuns()
{
    for (ObjectGuid& g : m_gunners)
        if (!g.IsEmpty())
        {
            if (Player* p = GetMap()->GetPlayer(g))
                LeaveGun(p, true);
            g.Clear();
        }
}

S::Standing ShipMgr::StandingOf(Player const* p, PlayerShip const& ship) const
{
    S::Standing s;
    if (!p)
        return s;
    s.owner = p->GetObjectGuid() == ship.owner;
    if (Group const* grp = const_cast<Player*>(p)->GetGroup())
        s.party = grp->IsMember(ship.owner);
    uint32 const guild = ship.ownerGuild.load();
    s.guild = guild && p->GetGuildId() == guild;
    return s;
}

ShipMgr::Record* ShipMgr::RecordOf(PlayerShip const& ship)
{
    auto it = m_ships.find(ship.ShipId());
    return it == m_ships.end() ? nullptr : &it->second;
}

void ShipMgr::SetSafe(Record& r, uint32 map, float x, float y, float z)
{
    r.safeMap = map;
    r.safeX = x;
    r.safeY = y;
    r.safeZ = z;
}

void ShipMgr::RoundAt(Player* shooter, Fusion::Vec3 origin, Fusion::Vec3 dir, float range, float damage)
{
    if (!shooter || !shooter->IsInWorld())
        return;
    Map* map = shooter->GetMap();
    PlayerShip* best = nullptr;
    float bestT = range;
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        for (auto const& [id, r] : m_ships)
        {
            if (r.go.IsEmpty() || r.map != map->GetId())
                continue;
            GenericTransport* t = map->GetTransport(r.go);
            if (!t || t == shooter->GetTransport())
                continue;   // not the deck the shooter stands on
            PlayerShip* ship = static_cast<PlayerShip*>(t);
            if (S::May(StandingOf(shooter, *ship), ship->rights.load(), S::Duty::Guns))
                continue;   // not a ship the shooter crews
            if (std::optional<float> hit = S::RayVsHull(origin, dir, bestT, S::HullOf(ship->Info().cls),
                    ship->X(), ship->Y(), ship->Z(), ship->O()))
            {
                bestT = *hit;
                best = ship;
            }
        }
    }
    if (best)
        best->Damage(S::HullDamage(damage, false));
}

void ShipMgr::BlastAt(Player* owner, Fusion::Vec3 at, float radius, float damage)
{
    if (!owner || !owner->IsInWorld())
        return;
    Map* map = owner->GetMap();
    std::vector<std::pair<PlayerShip*, float>> hurt;
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        for (auto const& [id, r] : m_ships)
        {
            if (r.go.IsEmpty() || r.map != map->GetId())
                continue;
            GenericTransport* t = map->GetTransport(r.go);
            if (!t)
                continue;
            PlayerShip* ship = static_cast<PlayerShip*>(t);
            if (t == owner->GetTransport() || S::May(StandingOf(owner, *ship), ship->rights.load(), S::Duty::Guns))
                continue;
            float const share = S::BlastShare(at, radius, S::HullOf(ship->Info().cls), ship->X(), ship->Y(), ship->Z(), ship->O());
            if (share > 0.f)
                hurt.push_back({ship, share});
        }
    }
    for (auto const& [ship, share] : hurt)
        ship->Damage(S::HullDamage(damage, true, share));
}

bool ShipMgr::RespawnAboard(Player* player)
{
    if (!player || player->IsAlive() || player->InBattleGround())
        return false;
    uint32 const now = Now();
    Map* map = player->GetMap();
    PlayerShip* best = nullptr;
    float bestD = S::kRespawnReach;
    {
        std::lock_guard<std::recursive_mutex> g(m_lock);
        for (auto const& [id, r] : m_ships)
        {
            if (r.go.IsEmpty() || r.map != map->GetId())
                continue;
            GenericTransport* t = map->GetTransport(r.go);
            if (!t)
                continue;
            PlayerShip* ship = static_cast<PlayerShip*>(t);
            bool const crew = S::May(StandingOf(player, *ship), ship->rights.load(), S::Duty::Respawn);
            float const d = player->GetDistance2d(ship);
            if (S::CanRespawnAboard(crew, r.state, ship->Anchored(), now, ship->lastHitAt.load(), r.lastRespawnAt, d)
                    == S::RespawnRefusal::None && d <= bestD)
            {
                bestD = d;
                best = ship;
            }
        }
        if (!best)
            return false;
        if (Record* r = RecordOf(*best))
        {
            r->lastRespawnAt = now;
            Save(*r);
        }
    }
    if (GenericTransport* t = player->GetTransport())
        t->RemovePassenger(player);
    player->ResurrectPlayer(0.5f);
    player->SpawnCorpseBones();
    player->TeleportTo(best->GetMapId(), best->X(), best->Y(), best->Z() + best->Info().deckZ + 1.5f, best->O());
    ChatHandler(player).PSendSysMessage("|cff88ccffYou come to aboard %s.|r", best->name.c_str());
    return true;
}

void ShipMgr::SaveRider(Player* player)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUIDLow();
    PlayerShip* ship = ShipOf(player);
    std::lock_guard<std::recursive_mutex> g(m_lock);
    if (!ship)
    {
        if (m_riders.erase(guid))
            CharacterDatabase.PExecute("DELETE FROM `aw_ship_rider` WHERE `guid` = %u", guid);
        return;
    }
    Rider rd;
    rd.ship = ship->ShipId();
    rd.map = ship->GetMapId();
    rd.x = player->GetTransOffsetX();
    rd.y = player->GetTransOffsetY();
    rd.z = player->GetTransOffsetZ();
    rd.o = player->GetTransOffsetO();
    rd.wx = player->GetPositionX();
    rd.wy = player->GetPositionY();
    rd.wz = player->GetPositionZ();
    m_riders[guid] = rd;
    CharacterDatabase.PExecute("REPLACE INTO `aw_ship_rider` (`guid`, `ship`, `map`, `x`, `y`, `z`, `o`, `wx`, `wy`, `wz`) "
        "VALUES (%u, %u, %u, %f, %f, %f, %f, %f, %f, %f)", guid, rd.ship, rd.map, rd.x, rd.y, rd.z, rd.o, rd.wx, rd.wy, rd.wz);
}

bool ShipMgr::PlaceRiderAtLogin(Player* player)
{
    uint32 const guid = player->GetGUIDLow();
    std::lock_guard<std::recursive_mutex> g(m_lock);
    auto it = m_riders.find(guid);
    if (it == m_riders.end())
        return false;
    Rider const rd = it->second;
    m_riders.erase(it);
    CharacterDatabase.PExecute("DELETE FROM `aw_ship_rider` WHERE `guid` = %u", guid);
    if (rd.map != player->GetMapId())
        return false;       // moved since (a GM, a teleport): leave them where they are
    auto ship = m_ships.find(rd.ship);
    if (ship != m_ships.end())
    {
        Record const& r = ship->second;
        // Still afloat where the rider can be put back: on the deck, at their place on it.
        if (r.state == S::State::Afloat && !r.go.IsEmpty() && r.map == rd.map
            && std::fabs(rd.x) < 250.f && std::fabs(rd.y) < 250.f && std::fabs(rd.z) < 100.f)
        {
            float x = rd.x, y = rd.y, z = rd.z, o = rd.o;
            GenericTransport::CalculatePassengerPosition(x, y, z, &o, r.x, r.y, r.z, r.o);
            player->Relocate(x, y, z + 0.5f, o);
            return false;
        }
        // Gone: ashore at its last dock, on the planks.
        if (r.safeMap == player->GetMapId())
        {
            player->Relocate(r.safeX, r.safeY, r.safeZ + 0.2f, player->GetOrientation());
            return false;
        }
    }
    // No ship and no dock on this map: never the sea or the sky, so the hearth.
    return true;
}

void ShipMgr::SaveCargo(Record const& r) const
{
    CharacterDatabase.PExecute("DELETE FROM `aw_ship_cargo` WHERE `ship` = %u", r.id);
    for (size_t i = 0; i < r.cargo.size(); ++i)
        CharacterDatabase.PExecute("INSERT INTO `aw_ship_cargo` (`ship`, `slot`, `entry`, `count`) VALUES (%u, %u, %u, %u)",
            r.id, uint32(i), r.cargo[i].entry, r.cargo[i].count);
}

namespace
{
    // Plain goods only go in a hold, and come out as new items: nothing a fresh item would lose
    // (an enchant, a random property, charges, durability) and nothing bound.
    bool PlainGoods(Item const* it)
    {
        ItemPrototype const* proto = it ? it->GetProto() : nullptr;
        if (!proto || it->IsSoulBound() || it->IsBag() || it->GetItemRandomPropertyId() != 0 || proto->MaxDurability > 0
            || proto->Bonding == BIND_WHEN_PICKED_UP || proto->Bonding == BIND_QUEST_ITEM || proto->Class == ITEM_CLASS_QUEST)
            return false;
        for (uint32 e = 0; e < MAX_ENCHANTMENT_SLOT; ++e)
            if (it->GetEnchantmentId(EnchantmentSlot(e)))
                return false;
        for (auto const& sp : proto->Spells)
            if (sp.SpellCharges != 0)
                return false;
        return true;
    }

    // The bags' plain goods by entry; an entry with any copy that is not plain is left out.
    void BagGoods(Player* player, std::map<uint32, uint32>& plain, std::set<uint32>& mixed)
    {
        auto look = [&](Item* it) {
            if (!it)
                return;
            if (PlainGoods(it))
                plain[it->GetEntry()] += it->GetCount();
            else
                mixed.insert(it->GetEntry());
        };
        for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
            look(player->GetItemByPos(INVENTORY_SLOT_BAG_0, i));
        for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
            if (Bag* bag = dynamic_cast<Bag*>(player->GetItemByPos(INVENTORY_SLOT_BAG_0, b)))
                for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                    look(bag->GetItemByPos(uint8(j)));
        for (uint32 e : mixed)
            plain.erase(e);
    }

    std::string ItemName(uint32 entry)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
        return proto && proto->Name1 ? proto->Name1 : "";
    }
}

void ShipMgr::SendCargo(Player* player, Record const& r)
{
    std::vector<S::CargoLine> hold, bags;
    for (S::Stack const& s : r.cargo)
        hold.push_back({s.entry, s.count, ItemName(s.entry)});
    std::map<uint32, uint32> plain;
    std::set<uint32> mixed;
    BagGoods(player, plain, mixed);
    for (auto const& [entry, count] : plain)
        bags.push_back({entry, count, ItemName(entry)});
    sFusionMgr.SendChannel(player, S::kChannel, S::EncodeCargo(S::CargoSlots(r.cls), hold, bags));
}

void ShipMgr::HandleCrew(Player* player, PlayerShip* ship, S::CrewAsk const& ask)
{
    if (!ship)
    {
        Tell(player, S::RefusalText(S::Refusal::NotAboard));
        return;
    }
    S::Standing const st = StandingOf(player, *ship);
    switch (ask.op)
    {
        case S::kOpManGun:
        {
            if (!S::May(st, ship->rights.load(), S::Duty::Guns))
            {
                Tell(player, "The owner hasn't given you the guns.");
                return;
            }
            if (ship->Pilot() == player->GetObjectGuid())
            {
                Tell(player, "Leave the helm first.");
                return;
            }
            if (ship->GunOf(player->GetObjectGuid()) != S::kNoMount)
                return;
            uint8 const mount = ask.index == S::kNoMount ? ship->FreeMountNear(player) : ask.index;
            if (mount == S::kNoMount || !ship->ManGun(player, mount))
                Tell(player, "No free gun within reach: stand by one of the ship's guns.");
            return;
        }
        case S::kOpLeaveGun:
            ship->LeaveGun(player, true);
            return;
        default:
            break;
    }
    // The hold: the owner, aboard.
    if (!st.owner)
    {
        Tell(player, "Only the owner opens the hold.");
        return;
    }
    std::lock_guard<std::recursive_mutex> g(m_lock);
    Record* r = RecordOf(*ship);
    if (!r)
        return;
    if (ask.op == S::kOpStow)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(ask.entry);
        std::map<uint32, uint32> plain;
        std::set<uint32> mixed;
        BagGoods(player, plain, mixed);
        auto have = plain.find(ask.entry);
        if (!proto || have == plain.end())
            Tell(player, mixed.count(ask.entry) ? "Only plain goods go in the hold (nothing enchanted, bound or worn)."
                                                : "You have none of that.");
        else
        {
            uint32 const want = std::min(ask.count, have->second);
            std::vector<S::Stack> hold = r->cargo;
            uint32 const stowed = S::Stow(hold, S::CargoSlots(r->cls), ask.entry, want, proto->GetMaxStackSize());
            if (!stowed)
                Tell(player, "The hold is full.");
            else
            {
                // Out of the bags, then into the hold: never both.
                player->DestroyItemCount(ask.entry, stowed, true);
                r->cargo = std::move(hold);
                SaveCargo(*r);
            }
        }
    }
    else if (ask.op == S::kOpTake && ask.index < r->cargo.size())
    {
        S::Stack const s = r->cargo[ask.index];
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, s.entry, s.count) != EQUIP_ERR_OK)
            Tell(player, "Your bags are full.");
        else
        {
            S::Stack out;
            S::Take(r->cargo, ask.index, out);
            SaveCargo(*r);
            if (Item* it = player->StoreNewItem(dest, s.entry, true))
                player->SendNewItem(it, s.count, true, false);
        }
    }
    SendCargo(player, *r);
}

void ShipMgr::CrewLogout(Player* player)
{
    if (!m_enabled || !player)
        return;
    PlayerShip* ship = ShipOf(player);
    if (!ship)
        return;
    if (ship->Pilot() == player->GetObjectGuid())
        ship->LeaveHelm(false);
    if (ship->GunOf(player->GetObjectGuid()) != Fusion::Ships::kNoMount)
        ship->LeaveGun(player, false);
    player->SetRooted(false);
}
