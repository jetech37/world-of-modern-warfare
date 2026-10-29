/*
 * World of Modern Warfare: player ships (design/PLAYER_SHIPS.md). GPL-2.0-or-later.
 *
 * A Shipwright (creature 91810, script npc_fusion_shipwright, 36_player_ships.sql) sells the
 * deeds and commissions, launches, repairs, salvages and summons a player's ship; its rules are
 * fusion/Ships.h. A ship in the world is a PlayerShip: an ElevatorTransport (gameobject type 11)
 * with no animation and its own Update, in its map's transport set, so the map sends it to every
 * player on the map and vmangos's own movement code boards the players who walk onto its deck
 * (MovementHandler: Map::GetTransport by guid, GenericTransport::AddPassenger). Each map tick it
 * steps the helm's kinematics and carries its passengers (GenericTransport::UpdatePosition), and
 * streams its pose on fusion channel 14 (a 1.12 client computes a transport's pose from its path,
 * and a ship has none).
 *
 * The ships are kept in the characters database's `aw_ship` (made here at startup); every afloat
 * ship is put back where it was left, anchored, when the server starts.
 */

#ifndef AW_FUSION_SHIPS_H
#define AW_FUSION_SHIPS_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Transport.h"
#include "fusion/ShipCrew.h"
#include "fusion/Ships.h"

#include <array>
#include <atomic>
#include <unordered_map>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class Creature;
class Player;

// A ship in the world.
class PlayerShip : public ElevatorTransport
{
public:
    PlayerShip(uint32 shipId, Fusion::Ships::ClassInfo const& info);

    void Update(uint32 update_diff, uint32 time_diff) override;

    uint32 ShipId() const { return m_shipId; }
    Fusion::Ships::ClassInfo const& Info() const { return m_info; }

    // The helm (the pilot's map thread, which is the ship's: a pilot must be aboard).
    ObjectGuid Pilot() const { return m_pilot; }
    void TakeHelm(Player* pilot);
    void LeaveHelm(bool tell = true);
    void SetKeys(uint8 keys) { m_helm.Press(keys); }
    bool Anchored() const { return m_anchored; }
    void SetAnchored(bool on) { m_anchored = on; m_poseMs = 0; m_saveMs = 0; }
    float Speed() const { return m_motion.speed; }
    bool HasPassengers() { std::lock_guard<std::mutex> g(m_passengerMutex); return !m_passengers.empty(); }
    bool IsAboard(Unit const* u) const { return u && u->GetTransport() == this; }
    // The motion state from the object's pose (after Create).
    void Place() { m_motion.x = GetPositionX(); m_motion.y = GetPositionY(); m_motion.z = GetPositionZ(); m_motion.o = GetOrientation(); }

    // Any thread.
    std::atomic<uint32> hull;
    std::atomic<uint32> upkeepUntil;
    std::atomic<uint32> bellAt;
    std::atomic<bool> wrecked{false};       // hull gone: the world thread takes it out
    std::atomic<uint8> rights{Fusion::Ships::kDefaultRights};
    std::atomic<uint32> ownerGuild{0};
    std::atomic<uint32> lastHitAt{0};       // unix seconds
    ObjectGuid owner;
    std::string name;

    // The deck guns (the gunner's map thread, the ship's).
    uint8 GunOf(ObjectGuid crewman) const;
    // A free mounting nearest the player (Fusion::Ships::kNoMount: none in reach).
    uint8 FreeMountNear(Player const* p) const;
    bool ManGun(Player* p, uint8 mount);
    void LeaveGun(Player* p, bool tell = true);
    void LeaveAllGuns();
    // The ship's pose, for the hull's hit tests.
    float X() const { return m_motion.x; }
    float Y() const { return m_motion.y; }
    float Z() const { return m_motion.z; }
    float O() const { return m_motion.o; }

    // Send the pose to one player now (a map entry, a helm change).
    void SendPoseTo(Player* p);
    void Damage(uint32 amount);

private:
    void Broadcast();
    Fusion::Ships::Probe ProbeAt(float x, float y, float z) const;

    uint32 m_shipId;
    Fusion::Ships::ClassInfo const& m_info;
    Fusion::Ships::Motion m_motion;
    Fusion::Ships::Helm m_helm;
    ObjectGuid m_pilot;
    std::array<ObjectGuid, 4> m_gunners;
    bool m_anchored = true;
    int32 m_poseMs = 0;
    int32 m_saveMs = 0;
    int32 m_restMs = 0;
    bool m_moved = false;
};

class ShipMgr
{
public:
    static ShipMgr& Instance();

    static constexpr uint32 kShipwrightEntry = 91810;

    struct Record
    {
        uint32 id = 0;
        uint32 owner = 0;               // character guid low
        Fusion::Ships::Class cls = Fusion::Ships::Class::None;
        std::string name;
        Fusion::Ships::State state = Fusion::Ships::State::Building;
        uint32 map = 0;
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;
        uint32 hull = 0;
        bool anchored = true;
        uint32 readyAt = 0;             // unix seconds: the build is done
        uint32 upkeepUntil = 0;
        uint32 created = 0;
        uint32 bellAt = 0;
        uint32 lastSweep = 0;
        uint8 rights = Fusion::Ships::kDefaultRights;
        uint32 ownerGuild = 0;          // the owner's guild when last seen
        uint32 lastRespawnAt = 0;
        // Where its riders come ashore when it is gone at their login: the dock it last left,
        // on the planks (safeMap 0xFFFFFFFF: none).
        uint32 safeMap = 0xFFFFFFFF;
        float safeX = 0.f, safeY = 0.f, safeZ = 0.f;
        std::vector<Fusion::Ships::Stack> cargo;
        ObjectGuid go;                  // the PlayerShip in the world, if spawned
    };

    // Who a player is to a ship.
    Fusion::Ships::Standing StandingOf(Player const* p, PlayerShip const& ship) const;

    // Hull damage from the fusion's weapons (FusionMgr::HandleShots, Explode): the shooter's
    // map thread. A round is tested against each ship's hull but the shooter's own.
    void RoundAt(Player* shooter, Fusion::Vec3 origin, Fusion::Vec3 dir, float range, float damage);
    void BlastAt(Player* owner, Fusion::Vec3 at, float radius, float damage);

    // A player's spirit released: back aboard a ship they crew, if one may take them
    // (Player::RepopAtGraveyard). True when handled.
    bool RespawnAboard(Player* player);

    // Logout safety: a rider is saved with their deck offset (Player::SaveToDB), and put back at
    // login on the deck if the ship is still there, else at its dock on solid ground, else at
    // their hearth (Player::LoadFromDB, before the map is entered).
    void SaveRider(Player* player);
    // True when the rider must go to their hearth (no ship and no dock on their map).
    bool PlaceRiderAtLogin(Player* player);

    // Startup, after the world is loaded: the table, the rows.
    void Load();
    // The world thread, between map updates: spawns, removals, summons, the upkeep sweep.
    void Update();
    bool IsEnabled() const { return m_enabled; }

    // Channel 14 (FusionMgr::HandleChannel), the player's map thread.
    void HandleChannel(Player* player, std::vector<uint8> const& body);
    // The Shipwright's gossip.
    bool GossipHello(Player* player, Creature* creature);
    bool GossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action);

    // The ship's map thread: its live state into the record, and saved when asked.
    void Publish(PlayerShip const& ship, bool save);
    // A ship on this map, by its id; nullptr when not spawned there.
    PlayerShip* Find(Map* map, uint32 shipId) const;
    PlayerShip* ShipOf(Player const* player) const;     // the ship the player rides, if any
    // WorldSession::LogoutPlayer: the helm and any deck gun the player holds are let go.
    void CrewLogout(Player* player);

    // GM (.ship).
    bool Command(Player* gm, std::string const& args);

private:
    struct Pending
    {
        enum Kind { Spawn, Despawn, Move, Erase } kind;
        uint32 id;
        uint32 map;
        float x, y, z, o;
    };

    bool Owns(uint32 owner, Fusion::Ships::Class c, Record* out = nullptr) const;
    std::vector<Record> RecordsOf(uint32 owner) const;
    void Save(Record const& r) const;
    void Spawn(Record& r);
    void Despawn(Record& r);
    void Queue(Pending p);
    bool LaunchPoint(Creature const* at, Fusion::Ships::ClassInfo const& info, float& x, float& y, float& z, float& o) const;
    bool LaunchHere(Player const* at, Fusion::Ships::ClassInfo const& info, float& x, float& y, float& z, float& o) const;
    bool Docked(Record const& r, Creature const* at) const;
    void Commission(Player* player, Creature* creature, Fusion::Ships::ClassInfo const& info);
    void Ring(Player* player, PlayerShip* aboard);
    void Sweep(uint32 now);
    void HandleCrew(Player* player, PlayerShip* ship, Fusion::Ships::CrewAsk const& ask);
    void SendCargo(Player* player, Record const& r);
    void SaveCargo(Record const& r) const;
    void SetSafe(Record& r, uint32 map, float x, float y, float z);
    Record* RecordOf(PlayerShip const& ship);

    struct Rider
    {
        uint32 ship = 0;
        uint32 map = 0;
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;     // the deck offset
        float wx = 0.f, wy = 0.f, wz = 0.f;            // the world position at the save
    };
    std::unordered_map<uint32, Rider> m_riders;

    bool m_enabled = true;
    mutable std::recursive_mutex m_lock;   // recursive: a spawn's first Update publishes
    std::map<uint32, Record> m_ships;
    std::vector<Pending> m_pending;
    uint32 m_nextId = 1;
    uint32 m_sweepAt = 0;
};

#define sShipMgr ShipMgr::Instance()

#endif
