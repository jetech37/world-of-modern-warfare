/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_SHIPS_H
#define FUSION_SHIPS_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Player ships (design/PLAYER_SHIPS.md): a sailing ship and an airship a player commissions at a
// Shipwright, pilots from the helm and keeps. Everything here is pure: the classes' numbers, the
// helm's kinematics against the world the game samples, upkeep, wear, repair and salvage, and
// the wire of fusion channel 14. The game glue is game/Fusion/FusionShips.cpp.
namespace Fusion::Ships
{
    enum class Class : uint8_t { None = 0, Sloop = 1, Airship = 2 };
    enum class State : uint8_t { Building = 0, Afloat = 1, Wrecked = 2 };

    constexpr uint32_t kGold = 10000;   // copper

    struct Material
    {
        uint32_t item;
        uint32_t count;
        char const* name;
    };

    struct ClassInfo
    {
        Class cls;
        char const* name;
        uint32_t goEntry;           // gameobject_template (36_player_ships.sql), type 11
        uint32_t deedItem;          // item_template
        uint32_t deedPrice;         // copper, at the Shipwright
        uint32_t buildCopper;       // paid when the keel is laid
        uint32_t buildMinutes;
        std::vector<Material> materials;
        float topSpeed;             // yd/s, Full Ahead
        float reverseSpeed;         // yd/s, Full Astern
        float accel;                // yd/s^2
        float turnRate;             // rad/s
        float climbRate;            // yd/s (0: keeps to the water)
        uint32_t maxHull;
        uint32_t upkeepPerDay;      // copper
        uint32_t repairPerPct;      // copper per 1% of hull
        uint32_t summonFee;         // copper
        float deckZ;                // the deck's height over the ship's origin (the bell lands here)
        float draft;                // a sailing ship's least water depth
    };

    ClassInfo const* Info(Class c);
    ClassInfo const* InfoByEntry(uint32_t goEntry);
    ClassInfo const* InfoByDeed(uint32_t item);
    // A class as a GM types it: sloop|sail|ship|1, airship|zeppelin|air|2 (any case); else null.
    ClassInfo const* InfoByName(std::string const& name);

    // `.ship test`: where a test ship is put, beside the GM (to their right) rather than on them,
    // so they can walk aboard. kTestBesideYards clears a sloop's beam with room to spare.
    constexpr float kTestBesideYards = 14.f;
    void BesideSpot(float x, float y, float o, float& outX, float& outY);
    std::vector<ClassInfo> const& Classes();

    // ── The helm ─────────────────────────────────────────────────────────────────────────────
    // The engine telegraph: Full Astern, Stop, Slow, Half, Full Ahead.
    constexpr int8_t kNotchMin = -1;
    constexpr int8_t kNotchMax = 3;
    char const* NotchName(int8_t notch);
    // The share of top speed (or of reverse speed, astern) a notch asks for.
    float NotchShare(int8_t notch);

    enum HelmKeys : uint8_t
    {
        KEY_AHEAD = 0x01,   // W: one notch up (on the press)
        KEY_ASTERN = 0x02,  // S: one notch down (on the press)
        KEY_PORT = 0x04,    // A: turn left while held
        KEY_STARBOARD = 0x08, // D: turn right while held
        KEY_CLIMB = 0x10,   // Space
        KEY_DIVE = 0x20,    // X
        KEY_ALL = 0x3F,
    };

    // The pilot's held keys, with the telegraph's edge on W and S.
    struct Helm
    {
        uint8_t keys = 0;
        int8_t notch = 0;
        // New key bits from the client: a W or S press moves the notch one step.
        void Press(uint8_t newKeys);
        void Release() { keys = 0; }
    };

    // The ship's motion state.
    struct Motion
    {
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;
        float speed = 0.f;          // yd/s along the heading (negative astern)
        float climb = 0.f;          // yd/s
        float yawRate = 0.f;        // rad/s
    };

    // What the game read of the world at the step's target point.
    struct Probe
    {
        bool inBounds = true;       // inside the map's edges
        bool water = false;         // liquid under the point
        float waterZ = 0.f;
        float groundZ = -1.0e6f;    // the terrain or a WMO under the point
        float ceilingZ = 1.0e6f;    // a ward's ceiling (Wards::kNoZ: none)
        bool unfinished = false;    // in "unfinished" airspace: push back out
    };

    constexpr float kAirClearance = 10.f;   // an airship's least height over the ground
    constexpr float kAirMaxAboveGround = 450.f;
    constexpr float kWorldCeiling = 1200.f;
    constexpr float kAnchorSpeed = 2.f;     // anchoring needs the ship slower than this
    constexpr float kScrapeSpeed = 4.f;     // grounding faster than this scrapes the hull
    constexpr float kListingShare = 0.25f;  // under this share of hull: half speed

    enum StepFlags : uint8_t
    {
        STEP_BLOCKED = 0x01,        // the move was refused: shore, shoal, terrain, edge
        STEP_SCRAPED = 0x02,        // ... at speed: hull lost (StepResult::hullLoss)
        STEP_CEILING = 0x04,        // held under a ward's (or the world's) ceiling
    };

    struct StepResult
    {
        uint8_t flags = 0;
        uint32_t hullLoss = 0;
    };

    // The speed limit right now: the class's top speed, halved when neglected or listing.
    float SpeedCap(ClassInfo const& info, bool neglected, uint32_t hull);

    // One step of the helm's kinematics. `helm` may be null (no pilot: the ship coasts to a stop).
    // `probe(x, y, z)` reads the world at the target point; the step tries the full move, and is
    // refused (the ship stops, its heading kept) when the target is not sailable or flyable.
    template <class ProbeFn>
    StepResult Step(ClassInfo const& info, Motion& m, Helm const* helm, bool anchored, float capSpeed,
        float dt, ProbeFn&& probe);

    // The kinematics' parts, exposed for the tests.
    float ApproachSpeed(float speed, float target, float accel, float dt);
    bool Sailable(ClassInfo const& info, Probe const& p, float& zOut);
    bool Flyable(Probe const& p, float wantZ, float& zOut, uint8_t& flags);

    // ── Upkeep, wear, repair and salvage ─────────────────────────────────────────────────────
    constexpr uint32_t kDaySec = 86400;
    constexpr uint32_t kUpkeepBlockDays = 7;    // paid in blocks, and included with a commission
    constexpr uint32_t kImpoundDays = 14;       // overdue this long: impounded (wrecked)
    constexpr float kNeglectWearPerDay = 0.04f; // of max hull, per day overdue
    constexpr uint32_t kBellCooldownSec = 30 * 60;
    constexpr float kDockRadius = 120.f;        // a ship this close to a Shipwright is docked
    constexpr uint32_t kSalvagePct = 30;        // of the build gold

    uint32_t UpkeepBlockCost(ClassInfo const& info);
    bool Neglected(uint32_t now, uint32_t upkeepUntil);
    uint32_t DaysOverdue(uint32_t now, uint32_t upkeepUntil);
    bool Impounded(uint32_t now, uint32_t upkeepUntil);
    // The hull lost to neglect between two sweeps (per whole day overdue newly passed).
    uint32_t NeglectWear(ClassInfo const& info, uint32_t upkeepUntil, uint32_t lastSweep, uint32_t now);
    uint32_t RepairCost(ClassInfo const& info, uint32_t hull);
    uint32_t SalvageCost(ClassInfo const& info);
    // Hull lost scraping ground at `speed` yd/s.
    uint32_t ScrapeLoss(ClassInfo const& info, float speed);
    uint8_t HullPct(ClassInfo const& info, uint32_t hull);

    // ── Ownership ────────────────────────────────────────────────────────────────────────────
    enum class Refusal : uint8_t
    {
        None = 0, NotOwner, NotAboard, Wrecked, Building, Anchored, TooFast, Piloted, Passengers,
        NotDocked, Cooldown, AlreadyOwns, NoDeed, NoGold, NoMaterials, NotReady, Neglected, Repaired,
        Dead, Downed, Mounted, Dueling,
    };
    char const* RefusalText(Refusal r);

    // Commissioning: the deed, the gold and every material in the bags, and no ship of the class.
    struct Purse
    {
        uint32_t copper = 0;
        bool hasDeed = false;
        bool ownsClass = false;
        std::vector<std::pair<uint32_t, uint32_t>> items;  // entry, count in the bags
    };
    Refusal CanCommission(ClassInfo const& info, Purse const& p);
    Refusal CanTakeHelm(bool owner, bool aboard, State s, bool piloted);
    // The pilot's own state: a helm needs someone alive, up (not downed), aboard, on foot (not
    // on a mount or a companion's back) and out of a companion duel. Checked when taking the
    // helm and every ship update while holding it (the helm is let go the moment one fails).
    struct HelmBody
    {
        bool alive = true;
        bool aboard = true;
        bool downed = false;
        bool mounted = false;
        bool dueling = false;
    };
    Refusal HelmBodyRefusal(HelmBody const& b);
    // Whether the pilot casts or shoots: never at the helm (the hands are on the wheel; the
    // client keeps the gun down there too). Triggered and passive spells are not casts.
    inline bool CastsAtHelm(bool atHelm, bool triggered) { return !atHelm || triggered; }
    Refusal CanAnchor(float speed);
    Refusal CanRing(bool owner, State s, uint32_t now, uint32_t bellAt);
    Refusal CanSummon(bool owner, State s, bool piloted, bool passengers);
    Refusal CanRepair(bool owner, State s, bool docked, uint32_t hull, uint32_t maxHull);

    // A ship's default name: "<owner>'s Sloop".
    std::string DefaultName(std::string const& owner, ClassInfo const& info);

    // ── Fusion channel 14 ────────────────────────────────────────────────────────────────────
    constexpr uint8_t kChannel = 14;
    // client to server
    constexpr uint8_t kOpTakeHelm = 1;      // [1]
    constexpr uint8_t kOpLeaveHelm = 2;     // [2]
    constexpr uint8_t kOpKeys = 3;          // [3, keys u8]
    constexpr uint8_t kOpAnchor = 4;        // [4]: drop it, or weigh it
    constexpr uint8_t kOpBell = 5;          // [5]: the Ship's Bell
    // server to client
    constexpr uint8_t kOpPose = 20;         // see Pose
    constexpr uint8_t kOpGone = 21;         // [21, guid u64]: the ship left the world
    constexpr uint8_t kOpHelm = 22;         // [22, guid u64]: you are at this ship's helm (0: none)

    enum PoseFlags : uint8_t
    {
        POSE_ANCHORED = 0x01,
        POSE_PILOTED = 0x02,
        POSE_MINE = 0x04,       // the receiver owns it
        POSE_NEGLECTED = 0x08,
        POSE_LISTING = 0x10,
    };

    struct Pose
    {
        uint64_t guid = 0;
        uint8_t cls = 0;
        uint8_t flags = 0;
        int8_t notch = 0;
        uint8_t hullPct = 100;
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;
        float vx = 0.f, vy = 0.f, vz = 0.f;
        float yawRate = 0.f;
        uint16_t bellSec = 0;   // seconds until the bell can ring again (to the owner)
        std::string name;
    };
    std::vector<uint8_t> EncodePose(Pose const& p);
    bool DecodePose(std::vector<uint8_t> const& b, Pose& out);
    std::vector<uint8_t> EncodeGone(uint64_t guid);
    std::vector<uint8_t> EncodeHelm(uint64_t guid);
    // Client ops: the op, and the keys for kOpKeys. False for anything malformed.
    bool DecodeClient(std::vector<uint8_t> const& b, uint8_t& op, uint8_t& keys);
    std::vector<uint8_t> EncodeKeys(uint8_t keys);

    // How often a pose goes out.
    constexpr uint32_t kPoseMovingMs = 200;
    constexpr uint32_t kPoseRestMs = 2000;
}

// ── Step, a template so the game can hand it a lambda ──────────────────────────────────────────
#include <algorithm>
#include <cmath>

namespace Fusion::Ships
{
    template <class ProbeFn>
    StepResult Step(ClassInfo const& info, Motion& m, Helm const* helm, bool anchored, float capSpeed,
        float dt, ProbeFn&& probe)
    {
        StepResult r;
        if (dt <= 0.f)
            return r;
        if (anchored)
        {
            m.speed = m.climb = m.yawRate = 0.f;
            return r;
        }
        int8_t const notch = helm ? helm->notch : 0;
        uint8_t const keys = helm ? helm->keys : 0;
        float target = notch >= 0 ? NotchShare(notch) * capSpeed
                                  : -NotchShare(notch) * std::min(info.reverseSpeed, capSpeed);
        m.speed = ApproachSpeed(m.speed, target, info.accel, dt);

        float turn = 0.f;
        if (keys & KEY_PORT)
            turn += 1.f;
        if (keys & KEY_STARBOARD)
            turn -= 1.f;
        // A ship turns by its way through the water (or air): half the rate when barely moving.
        float const way = std::min(1.f, 0.5f + std::fabs(m.speed) / std::max(1.f, info.topSpeed));
        m.yawRate = turn * info.turnRate * way;
        m.o = std::fmod(m.o + m.yawRate * dt, 6.2831853f);
        if (m.o < 0.f)
            m.o += 6.2831853f;

        float climb = 0.f;
        if (info.climbRate > 0.f)
        {
            if (keys & KEY_CLIMB)
                climb += info.climbRate;
            if (keys & KEY_DIVE)
                climb -= info.climbRate;
        }
        m.climb = climb;

        float const nx = m.x + std::cos(m.o) * m.speed * dt;
        float const ny = m.y + std::sin(m.o) * m.speed * dt;
        float const wantZ = m.z + climb * dt;
        Probe const p = probe(nx, ny, wantZ);
        float z = wantZ;
        bool ok = p.inBounds;
        if (ok)
            ok = info.climbRate > 0.f ? Flyable(p, wantZ, z, r.flags) : Sailable(info, p, z);
        if (!ok)
        {
            r.flags |= STEP_BLOCKED;
            if (std::fabs(m.speed) > kScrapeSpeed)
            {
                r.flags |= STEP_SCRAPED;
                r.hullLoss = ScrapeLoss(info, std::fabs(m.speed));
            }
            m.speed = 0.f;
            // An airship held where it is still answers its altitude keys, within its limits.
            if (info.climbRate > 0.f && p.inBounds)
            {
                Probe const here = probe(m.x, m.y, wantZ);
                float hz = wantZ;
                uint8_t f = 0;
                if (Flyable(here, wantZ, hz, f))
                    m.z = hz;
            }
            return r;
        }
        m.x = nx;
        m.y = ny;
        m.climb = (z - m.z) / dt;
        m.z = z;
        return r;
    }
}

#endif
