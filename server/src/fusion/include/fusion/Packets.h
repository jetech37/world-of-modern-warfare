/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PACKETS_H
#define FUSION_PACKETS_H

#include "fusion/Common.h"
#include "fusion/Loadout.h"
#include "fusion/Movement.h"

#include <string>
#include <vector>

// The fusion's own world packets, beside 1.12's (whose table ends at 827). The client side lives
// in the benilla fork (benilla-protocol, fusion.rs); both sides are tested against the same byte
// vectors (src/fusion/vectors/packets.txt), so they can't drift apart. Little-endian throughout.
namespace Fusion::Packets
{
    constexpr uint16_t CMSG_FUSION_SHOTS = 828;
    constexpr uint16_t SMSG_FUSION_EVENTS = 829;
    constexpr uint16_t CMSG_FUSION_STATE = 830;
    constexpr uint16_t CMSG_FUSION_LOADOUT = 831;
    constexpr uint16_t SMSG_FUSION_LOADOUT = 832;
    // A general channel both ways: a channel byte and an opaque body (building, world events).
    constexpr uint16_t CMSG_FUSION_CHANNEL = 833;
    constexpr uint16_t SMSG_FUSION_CHANNEL = 834;
    constexpr uint16_t kNumMsgTypes = 835;
    constexpr uint8_t kChannelBuild = 1;
    constexpr uint8_t kChannelWorld = 2;
    constexpr size_t kMaxChannelBody = 8192;

    constexpr uint8_t kVersion = 1;
    // CMSG_FUSION_SHOTS has its own: version 2 added each pull's seed and spread.
    constexpr uint8_t kShotsVersion = 2;
    // One batch carries at most this many shots (the client sends 20 batches a second).
    constexpr uint8_t kMaxShotsPerBatch = 16;
    constexpr uint8_t kMaxEventsPerPacket = 32;

    // ── CMSG_FUSION_SHOTS ───────────────────────────────────────────────────────────────────
    enum ShotFlags : uint8_t
    {
        SHOT_ADS = 0x01,
        SHOT_CLAIMS_HEAD = 0x02,
        SHOT_AIRBORNE = 0x04,
        SHOT_DETONATE = 0x08,       // with C4's id: set off the C4 already out
        SHOT_LEFT = 0x10,           // the round came from the left gun of an akimbo pair
    };

    struct Shot
    {
        uint32_t timeMs = 0;        // the shooter's view time on the server clock
        uint16_t weaponId = 0;
        uint8_t flags = 0;
        Stance stance = Stance::Stand;
        uint64_t targetGuid = 0;    // the unit the client's ray hit, 0 = none
        Vec3 origin;
        Vec3 direction;
        uint32_t seed = 0;          // the pull's pellet seed (Ballistics.h PelletDirection)
        float spreadDeg = 0.f;      // the cone the client scattered in; held to the gun's least
    };

    struct ShotBatch
    {
        std::vector<Shot> shots;
    };

    // ── CMSG_FUSION_STATE ───────────────────────────────────────────────────────────────────
    enum StateFlags : uint8_t
    {
        STATE_AIMING = 0x01,
        STATE_FFA_ON = 0x02,    // request the free-for-all flag on
        STATE_FFA_OFF = 0x04,   // request it off (takes 5 minutes out of combat)
        STATE_RELOADING = 0x08,
        STATE_REARM = 0x10,     // buy special rounds; only while resting (a city or an inn)
        STATE_MANTLE = 0x20,    // climb the ledge ahead, if there is one
        STATE_LUNGE = 0x40,     // knife lunge at the enemy in front, if one is in range
        STATE_RELOAD_ONE = 0x80, // one round into the held gun (a shotgun's shell), not a full reload
    };

    struct State
    {
        Stance stance = Stance::Stand;
        Gait gait = Gait::Run;
        uint8_t flags = 0;
        uint16_t heldWeaponId = 0;
        uint8_t loadedAmmo = 0;     // AmmoType
    };

    // ── SMSG_FUSION_EVENTS ──────────────────────────────────────────────────────────────────
    enum class EventKind : uint8_t
    {
        Hit = 0,            // you hit guid for amount
        Kill = 1,           // you killed guid
        Hurt = 2,           // guid hit you for amount
        StreakEarned = 3,   // extra = reward id
        Ammo = 4,           // amount = magazine, extra = reserve (a resync)
        Revived = 5,
        Struck = 6,         // you struck guid with WoW's damage (a spell, a technique): target it
    };

    // Whether a Struck for `guid` at `nowMs` is due after the last one sent (`lastGuid` at
    // `lastMs`): another unit at once, the same one again after half a second.
    constexpr uint32_t kStruckRepeatMs = 500;
    inline bool StruckDue(uint64_t lastGuid, uint32_t lastMs, uint64_t guid, uint32_t nowMs)
    {
        return guid != lastGuid || lastMs == 0 || nowMs - lastMs >= kStruckRepeatMs;
    }

    enum EventFlags : uint8_t
    {
        EVENT_HEADSHOT = 0x01,
        EVENT_FATAL = 0x02,
        EVENT_ASSIST = 0x04,
        // On StreakEarned: not a reward but the streak's progress, `amount` in tenths of a kill.
        EVENT_PROGRESS = 0x08,
        // On Ammo: the left gun's magazine of an akimbo pair.
        EVENT_LEFT = 0x10,
    };

    struct Event
    {
        EventKind kind = EventKind::Hit;
        uint8_t flags = 0;
        uint64_t guid = 0;
        uint32_t amount = 0;
        uint16_t extra = 0;
    };

    struct Events
    {
        std::vector<Event> events;
    };

    // Create-a-Class (MW2 design spec 1.3): the classes to save and the one to equip, and the
    // server's answer. These carry their own version byte; the others keep kVersion.
    // 2: the secondary's second attachment (Bling Pro); 3: each kit's six talent picks (u16 each,
    // after the streaks); 4: the primary's third attachment (str) and the perk slots' Pro bits
    // (u8), after the talent picks; 5: the primary's fourth attachment (str), after the Pro bits.
    constexpr uint8_t kLoadoutVersion = 5;

    struct LoadoutRequest
    {
        uint8_t equipSlot = kNoClass;                               // 0-4, or kNoClass
        std::vector<std::pair<uint8_t, ClassLoadout>> classes;      // slots to save, at most five
    };

    enum class LoadoutStatus : uint8_t
    {
        Saved,      // saved; nothing to equip
        Pending,    // saved; the equip waits until out of combat or alive
        Applied,    // the equipped class is in hand (also sent at login)
        Rejected,   // nothing saved: `error` in `errorSlot`
    };

    struct LoadoutReply
    {
        LoadoutStatus status = LoadoutStatus::Saved;
        LoadoutError error = LoadoutError::None;
        uint8_t errorSlot = kNoClass;
        uint8_t equippedSlot = kNoClass;
        std::vector<std::pair<uint8_t, ClassLoadout>> classes;      // the saved classes
    };

    // Encode a message's body (the opcode travels in the world packet header).
    std::vector<uint8_t> Encode(ShotBatch const& m);
    std::vector<uint8_t> Encode(State const& m);
    std::vector<uint8_t> Encode(Events const& m);

    // Decode a body; false on a wrong version, a bad count or a short buffer.
    bool Decode(std::vector<uint8_t> const& body, ShotBatch& out);
    bool Decode(std::vector<uint8_t> const& body, State& out);
    bool Decode(std::vector<uint8_t> const& body, Events& out);

    std::vector<uint8_t> Encode(LoadoutRequest const& m);
    std::vector<uint8_t> Encode(LoadoutReply const& m);
    bool Decode(std::vector<uint8_t> const& body, LoadoutRequest& out);
    bool Decode(std::vector<uint8_t> const& body, LoadoutReply& out);

    // ── CMSG/SMSG_FUSION_CHANNEL: version u8, channel u8, length u16, body ──
    struct ChannelMessage
    {
        uint8_t channel = 0;
        std::vector<uint8_t> body;
    };
    std::vector<uint8_t> Encode(ChannelMessage const& m);
    bool Decode(std::vector<uint8_t> const& body, ChannelMessage& out);

    std::string ToHex(std::vector<uint8_t> const& bytes);
    std::vector<uint8_t> FromHex(std::string const& hex);
}

#endif
