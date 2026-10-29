/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PICKUPS_H
#define FUSION_PICKUPS_H

#include "fusion/Common.h"
#include "fusion/Weapon.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Weapon pickups, as in MW2 (design doc section 6): a dead NPC gunner or a dead player drops the
// gun in hand; look at it and hold F to take it. It replaces the gun in its slot (a primary or a
// secondary, by the gun's class) for this life, with the ammunition it had (an NPC's: a magazine
// and a couple of spares). It is never saved: death, a kit swap or logging out clears it and the
// kit's own guns come back. A pickup ignores achievement unlocks. The server owns the guns on the
// ground and decides every pickup; the client asks and is told what it holds.
namespace Fusion::Pickups
{
    // A gun lies this long, then it is gone.
    constexpr uint32_t kDespawnMs = 75000;
    // How near the player's feet must be to the gun (yards; the client asks from 2.5 or so).
    constexpr float kReach = 4.f;
    // How long F is held to take it (the client's), and how far the eye looks for one.
    constexpr uint32_t kHoldMs = 600;
    constexpr float kLookReach = 3.5f;
    // At most this many lie on one map (an instance counts apart); the oldest goes first.
    constexpr size_t kMaxPerMap = 48;
    // Players are told of the guns within kTellRadius, every kTellEveryMs when that changed.
    constexpr float kTellRadius = 60.f;
    constexpr uint32_t kTellEveryMs = 500;
    constexpr uint8_t kMaxListed = 48;
    // An NPC's gun comes with a magazine and this many more.
    constexpr uint32_t kNpcSpareMagazines = 2;

    enum class Slot : uint8_t { Primary, Secondary };

    // The slot a gun takes, false for anything that is not a primary or secondary (equipment,
    // killstreaks, underbarrel weapons).
    bool SlotOf(WeaponProfile const& w, Slot& out);

    // A gun as it is held: the catalog gun, its attachments ("" none) and camo.
    struct Gun
    {
        uint16_t id = 0;
        std::string att1, att2;
        uint8_t camo = 0;

        bool operator==(Gun const& o) const { return id == o.id && att1 == o.att1 && att2 == o.att2 && camo == o.camo; }
    };

    struct Dropped
    {
        uint32_t id = 0;
        uint32_t mapId = 0;
        uint32_t instanceId = 0;
        Vec3 pos;                   // where it lies (raw WoW coordinates, on the ground)
        float yaw = 0.f;            // the way its barrel points
        Gun gun;
        uint32_t mag = 0;
        uint32_t reserve = 0;
        uint32_t expiresMs = 0;
    };

    // An NPC's gun: a full magazine and kNpcSpareMagazines more, no more than the gun carries.
    void NpcAmmo(WeaponProfile const& w, uint32_t& mag, uint32_t& reserve);
    // Whether a gun with this ammunition is worth dropping (an empty one is not).
    bool WorthDropping(uint32_t mag, uint32_t reserve);

    // The guns on the ground, every map. Not locked: the glue holds one lock around it.
    class Field
    {
    public:
        // Lay a gun down for kDespawnMs; its id. Past kMaxPerMap the oldest on its map goes.
        uint32_t Add(Dropped g, uint32_t nowMs);
        // Drop the ones whose time is up; how many went.
        size_t Expire(uint32_t nowMs);

        enum class Take : uint8_t { Ok, Gone, TooFar };
        // Take a gun for a player standing at `feet` on the map: it must still lie there and be
        // within kReach. Ok: `out` holds it and it is off the ground.
        Take TakeGun(uint32_t id, uint32_t mapId, uint32_t instanceId, Vec3 feet, uint32_t nowMs, Dropped& out);

        // The guns within `radius` of `at` on the map, nearest first, at most `max`.
        std::vector<Dropped> Near(uint32_t mapId, uint32_t instanceId, Vec3 at, float radius, size_t max) const;
        Dropped const* Find(uint32_t id) const;
        size_t Size() const { return m_guns.size(); }

    private:
        std::vector<Dropped> m_guns;
        uint32_t m_nextId = 1;
    };

    // A player's pickups for this life, by slot.
    struct Hands
    {
        std::array<std::optional<Gun>, 2> slots;

        Gun const* For(Slot s) const;
        // The pickup that is this catalog gun, if any (a pickup's gun skips the unlock check).
        Gun const* Holding(uint16_t gunId) const;
        bool Any() const;
        void Clear();
    };

    // ── The wire: SMSG/CMSG_FUSION_CHANNEL, kChannelWorld, ops 60-63 ──
    // op 60 (server): [60, count u8, count x (id u32, gun u16, x y z f32, yaw f32)]: the guns
    //       lying near the player (at most kMaxListed), sent when that changes; count 0 clears.
    // op 61 (client): [61, id u32]: take this gun (F held on it).
    // op 62 (server): [62, count u8, count x (slot u8, gun u16, camo u8, len u8, att1, len u8,
    //       att2)]: what the player holds from pickups now, in place of the kit's; count 0: the
    //       kit's own again.
    // op 63 (server): [63, reason u8 (Field::Take)]: a pickup refused.
    constexpr uint8_t kOpGuns = 60;
    constexpr uint8_t kOpTake = 61;
    constexpr uint8_t kOpHeld = 62;
    constexpr uint8_t kOpRefused = 63;

    std::vector<uint8_t> EncodeGuns(std::vector<Dropped> const& guns);
    bool DecodeGuns(std::vector<uint8_t> const& body, std::vector<Dropped>& out);
    std::vector<uint8_t> EncodeTake(uint32_t id);
    bool DecodeTake(std::vector<uint8_t> const& body, uint32_t& id);
    std::vector<uint8_t> EncodeHeld(Hands const& h);
    bool DecodeHeld(std::vector<uint8_t> const& body, Hands& out);
    std::vector<uint8_t> EncodeRefused(Field::Take why);

    // A fingerprint of a list (its ids, in any order), to send it again only when it changed.
    uint32_t ListSignature(std::vector<Dropped> const& guns);
}

#endif
