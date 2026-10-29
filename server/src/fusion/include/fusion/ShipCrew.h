/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_SHIP_CREW_H
#define FUSION_SHIP_CREW_H

#include "fusion/Common.h"
#include "fusion/Ships.h"

#include <optional>
#include <string>
#include <vector>

// Player ships' second slice (design/PLAYER_SHIPS.md): the crew's rights, the deck guns, hull
// damage from weapons, the cargo hold, the respawn aboard, and the liquids a keel floats on.
namespace Fusion::Ships
{
    // ── Liquids ──────────────────────────────────────────────────────────────────────────────
    // The map's liquid type bits (vmangos GridMapDefines.h): a keel floats on water and the
    // ocean, never magma or slime.
    constexpr uint8_t kLiquidMagma = 0x01;
    constexpr uint8_t kLiquidOcean = 0x02;
    constexpr uint8_t kLiquidSlime = 0x04;
    constexpr uint8_t kLiquidWater = 0x08;
    bool SailableLiquid(uint8_t typeFlags);

    // ── Crew rights ──────────────────────────────────────────────────────────────────────────
    enum Rights : uint8_t
    {
        RIGHT_PARTY_HELM = 0x01,
        RIGHT_GUILD_HELM = 0x02,
        RIGHT_PARTY_GUNS = 0x04,
        RIGHT_GUILD_GUNS = 0x08,
        RIGHT_ANYONE_GUNS = 0x10,
        RIGHT_PARTY_RESPAWN = 0x20,
        RIGHT_GUILD_RESPAWN = 0x40,
        RIGHT_ALL = 0x7F,
    };
    constexpr uint8_t kDefaultRights = RIGHT_PARTY_GUNS | RIGHT_GUILD_GUNS | RIGHT_PARTY_RESPAWN | RIGHT_GUILD_RESPAWN;

    enum class Duty : uint8_t { Helm, Guns, Respawn };

    // Who someone is to the ship: its owner, in the owner's party, in the owner's guild.
    struct Standing
    {
        bool owner = false;
        bool party = false;
        bool guild = false;
    };
    bool May(Standing s, uint8_t rights, Duty duty);
    char const* RightName(uint8_t bit);

    // ── Deck guns ────────────────────────────────────────────────────────────────────────────
    // A mounting on the deck, in the ship's frame (x forward, y left, z up from its origin), and
    // the arsenal gun a crewman fires from it (weapons.json: 19 the M240, 42 the RPG). Manning
    // one hands the gun over as a pickup, so it fires through the fusion's own shots and their
    // validation, and the crewman is rooted there.
    struct Mount
    {
        float x, y, z, o;
        uint16_t gun;
        char const* name;
    };
    std::vector<Mount> const& MountsOf(Class c);
    constexpr uint8_t kNoMount = 0xFF;
    // Where a mounting is in the world, for a ship at (sx, sy, sz, so).
    Vec3 MountWorld(Mount const& m, float sx, float sy, float sz, float so);
    // A crewman stays at a mounting while within this of it (a deck shifts under the feet).
    constexpr float kMountReach = 4.f;

    // ── The hull as a target ─────────────────────────────────────────────────────────────────
    // A box about the ship's origin in its frame: half its length and beam, and its depth
    // under and height over the origin (VERIFY each against the models in game).
    struct HullShape
    {
        float halfLength, halfBeam, below, above;
    };
    HullShape const& HullOf(Class c);
    // The distance along a ray (unit dir) to the hull, if it hits within maxDist.
    std::optional<float> RayVsHull(Vec3 origin, Vec3 dir, float maxDist, HullShape const& h,
        float sx, float sy, float sz, float so);
    // How much of a blast reaches the hull: 1 at the hull, falling to 0 at the radius.
    float BlastShare(Vec3 center, float radius, HullShape const& h, float sx, float sy, float sz, float so);

    // Weapons hurt a hull less than flesh, explosives more than rounds.
    constexpr float kRoundHullScale = 1.f;
    constexpr float kBlastHullScale = 4.f;
    uint32_t HullDamage(float weaponDamage, bool blast, float share = 1.f);
    // Hit in the last this many seconds: under fire (no respawn aboard, no anchoring away).
    constexpr uint32_t kUnderFireSec = 60;

    // ── The cargo hold ───────────────────────────────────────────────────────────────────────
    // Slots of one stack each; only plain goods (no enchant, no random property, no charges,
    // no soulbinding: what the game checks) go in, and come out as new items.
    struct Stack
    {
        uint32_t entry = 0;
        uint32_t count = 0;
    };
    uint8_t CargoSlots(Class c);
    // Stow up to `count` of an item into the hold (topping up part stacks first, then free
    // slots): how many went in.
    uint32_t Stow(std::vector<Stack>& hold, uint8_t slots, uint32_t entry, uint32_t count, uint32_t maxStack);
    // Take slot `index` out whole; false when there is none.
    bool Take(std::vector<Stack>& hold, uint8_t index, Stack& out);

    // ── The respawn aboard ───────────────────────────────────────────────────────────────────
    constexpr uint32_t kRespawnCooldownSec = 10 * 60;
    constexpr float kRespawnReach = 2000.f;     // a ship farther than this from the corpse: no
    enum class RespawnRefusal : uint8_t { None, NotCrew, Wrecked, NotAnchored, UnderFire, Cooldown, TooFar };
    RespawnRefusal CanRespawnAboard(bool crew, State s, bool anchored, uint32_t now, uint32_t lastHitAt,
        uint32_t lastRespawnAt, float distance);

    // ── Fusion channel 14, the second slice's ops ────────────────────────────────────────────
    // client
    constexpr uint8_t kOpManGun = 6;        // [6, mount u8]
    constexpr uint8_t kOpLeaveGun = 7;      // [7]
    constexpr uint8_t kOpCargo = 8;         // [8]: the hold and the bags' plain goods
    constexpr uint8_t kOpStow = 9;          // [9, entry u32, count u32]
    constexpr uint8_t kOpTake = 10;         // [10, slot u8]
    // server
    constexpr uint8_t kOpGun = 23;          // [23, ship guid u64, mount u8 (kNoMount: none)]
    constexpr uint8_t kOpCargoList = 24;    // see EncodeCargo
    constexpr uint8_t kOpMounts = 25;       // [25, ship guid u64, n u8, n x (manned u8)]

    // The second slice's pose flags (Ships.h PoseFlags carries the first five).
    constexpr uint8_t POSE_CREW = 0x20;     // the receiver may man the guns
    constexpr uint8_t POSE_HELMSMAN = 0x40; // the receiver may take the helm

    struct CargoLine
    {
        uint32_t entry = 0;
        uint32_t count = 0;
        std::string name;
    };
    std::vector<uint8_t> EncodeCargo(uint8_t slots, std::vector<CargoLine> const& hold, std::vector<CargoLine> const& bags);
    bool DecodeCargo(std::vector<uint8_t> const& b, uint8_t& slots, std::vector<CargoLine>& hold, std::vector<CargoLine>& bags);
    std::vector<uint8_t> EncodeGun(uint64_t ship, uint8_t mount);
    std::vector<uint8_t> EncodeMounts(uint64_t ship, std::vector<bool> const& manned);

    // The second slice's client ops: op and its arguments. False for anything malformed.
    struct CrewAsk
    {
        uint8_t op = 0;
        uint8_t index = 0;
        uint32_t entry = 0;
        uint32_t count = 0;
    };
    bool DecodeCrew(std::vector<uint8_t> const& b, CrewAsk& out);
}

#endif
