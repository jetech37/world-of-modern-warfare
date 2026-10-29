/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_DOWNED_H
#define FUSION_DOWNED_H

#include "fusion/Common.h"
#include "fusion/Weapon.h"

#include <cstdint>
#include <vector>

// The downed state (design doc section 8). A hit that would kill a player leaves them down at 1
// health instead: they crawl, cannot fight (unless Last Stand: their pistol), cannot cast or use
// anything but a revival kit, and bleed out after a time that grows with Stamina. Hits on a
// downed player drain a small finisher pool; the hit that empties it kills. Bleeding out or being
// finished is a real death (ghost, corpse run). Only resurrection spells and the revival kits
// (First Aid and Engineering) bring a downed player back up; there is no hold-F revive.
namespace Fusion::Downed
{
    // ── Timings and shares ──────────────────────────────────────────────────────────────────
    // The bleed-out: 20 s, a second more for every 25 Stamina, held to a minute.
    constexpr float kBleedBaseSeconds = 20.f;
    constexpr float kStaminaPerSecond = 25.f;
    constexpr float kBleedMaxSeconds = 60.f;
    float BleedOutSeconds(uint32_t stamina);

    // Damage a downed player soaks before the hit that finishes them: a tenth of their health.
    constexpr float kFinisherShare = 0.10f;
    uint32_t FinisherPool(uint32_t maxHealth);

    // Crawling: MW2's prone speed over the run, whatever is in hand.
    constexpr float kCrawlSpeedScale = 0.15f;

    // Health on getting up: a resurrection spell, Final Stand's own (MW2's deathstreak).
    constexpr float kSpellReviveShare = 0.30f;
    constexpr float kFinalStandHealthShare = 0.5f;
    constexpr float kFinalStandSeconds = 10.f;
    uint32_t ReviveHealth(uint32_t maxHealth, float share);

    // A creature on a downed player turns to another target after this long (so the downed can
    // crawl away); with no other target it keeps at it.
    constexpr float kCreatureInterestSeconds = 2.f;
    bool CreatureLeaves(uint32_t downedSinceMs, uint32_t nowMs, bool hasOtherTarget);

    // ── What happens to a hit ───────────────────────────────────────────────────────────────
    enum class Fate : uint8_t
    {
        Normal,     // not fatal (or going down is not allowed): the game's own damage
        GoDown,     // fatal: down at 1 health instead
        Absorbed,   // on a downed player: into the finisher pool, the health untouched
        Finished,   // on a downed player: the pool is empty, a real death
    };
    struct DamageFate
    {
        Fate fate = Fate::Normal;
        uint32_t damage = 0;        // what the game should still deal
        uint32_t poolLeft = 0;      // a downed player's finisher pool after the hit
    };
    // `downed` the victim is down already, with `pool` left; `mayGoDown` the victim may go down
    // (a fusion player, not in a duel). `health` its current health.
    DamageFate OnDamage(bool downed, bool mayGoDown, uint32_t pool, uint32_t health, uint32_t damage);

    // ── A player's downed state ─────────────────────────────────────────────────────────────
    enum class Tick : uint8_t { None, BledOut, GetUp };

    struct State
    {
        bool active = false;
        uint32_t sinceMs = 0;           // server clock
        uint32_t untilMs = 0;           // bleeds out then
        uint32_t totalMs = 0;
        uint32_t finisherLeft = 0;
        bool armed = false;             // Last Stand (or Final Stand): the pistol fires
        bool finalStand = false;        // gets back up by itself after kFinalStandSeconds

        bool Active() const { return active; }
        void GoDown(uint32_t nowMs, uint32_t maxHealth, uint32_t stamina, bool lastStand, bool finalStandReady);
        Tick Update(uint32_t nowMs) const;
        uint32_t RemainingMs(uint32_t nowMs) const;
        // More time to be revived (a warlord's Rallying Cry): the bleed-out moves `ms` later.
        // Nothing for a player not down; Final Stand's own clock is not the bleed-out's.
        void Extend(uint32_t ms);
        void Clear() { *this = State{}; }
    };

    // ── Last Stand ──────────────────────────────────────────────────────────────────────────
    // The USP, every class's starting secondary.
    constexpr uint16_t kDefaultPistol = 30;
    // Whether a downed player may fire this gun: only with Last Stand, and only a pistol.
    bool MayFire(bool armed, WeaponClass c);
    // The pistol a downed player fights with: the class's secondary if it is a pistol, else the
    // USP.
    uint16_t LastStandPistol(uint16_t secondary, WeaponClass secondaryClass);

    // ── Revival kits (item entries 93020-93049) ─────────────────────────────────────────────
    constexpr uint32_t kFieldRevivalKit = 93020;     // First Aid
    constexpr uint32_t kRevivalCoil = 93021;         // Engineering
    constexpr uint32_t kFieldMedicHandbook = 93022;  // makes a Field Revival Kit
    constexpr uint32_t kRevivalSchematic = 93023;    // makes a Gnomish Revival Coil
    constexpr uint32_t kFirstItem = 93020;
    constexpr uint32_t kLastItem = 93049;

    struct Kit
    {
        uint32_t item = 0;
        uint16_t skillLine = 0;         // the profession that makes it
        uint16_t skillToMake = 0;
        uint32_t otherMs = 0;           // on a downed ally
        uint32_t selfMs = 0;            // on yourself while down
        float healthShare = 0.f;
    };
    Kit const* KitInfo(uint32_t itemEntry);
    bool IsKitItem(uint32_t itemEntry);
    // A manual's product (0 for anything else): using the manual makes one.
    uint32_t ManualProduct(uint32_t itemEntry);
    // Any of the package's items, which the client routes to the downed channel.
    inline bool IsDownedItem(uint32_t itemEntry) { return KitInfo(itemEntry) || ManualProduct(itemEntry); }
    inline uint32_t UseMs(Kit const& k, bool onSelf) { return onSelf ? k.selfMs : k.otherMs; }

    // Reach to work on a downed ally (yards), and how far the user may shuffle while working.
    constexpr float kKitReach = 5.f;
    constexpr float kKitMoveTolerance = 1.5f;

    enum class UseCheck : uint8_t { Ok, TargetNotDowned, TooFar, NotFriendly, UserDowned };
    struct UseContext
    {
        bool userDowned = false;
        bool onSelf = false;
        bool targetDowned = false;
        bool friendly = false;
        float distance = 0.f;
    };
    UseCheck CanUseKit(Kit const& kit, UseContext const& c);

    // A kit being used: the user keeps still until it is done.
    enum class KitProgress : uint8_t { Working, Done, Moved };
    struct KitUse
    {
        uint64_t item = 0;              // the item's guid
        uint64_t target = 0;            // 0 on yourself
        uint32_t startMs = 0;
        uint32_t durationMs = 0;
        Vec3 at;
        bool active = false;

        bool Active() const { return active; }
        void Start(uint64_t itemGuid, uint64_t targetGuid, uint32_t nowMs, uint32_t ms, Vec3 userAt);
        KitProgress Progress(uint32_t nowMs, Vec3 userAt) const;
        void Clear() { *this = KitUse{}; }
    };

    // ── The wire: SMSG/CMSG_FUSION_CHANNEL on kChannelDowned, little-endian, first byte the op ──
    constexpr uint8_t kChannelDowned = 5;
    // Server to client.
    constexpr uint8_t kOpDowned = 1;    // you are down
    constexpr uint8_t kOpUp = 2;        // you are back up
    constexpr uint8_t kOpDied = 3;      // you bled out or were finished (a real death)
    constexpr uint8_t kOpKit = 4;       // a kit being used by you or on you
    constexpr uint8_t kOpNear = 5;      // a player near went down or got up
    // Client to server.
    constexpr uint8_t kOpUse = 10;      // use a kit (or a manual)

    enum DownedFlags : uint8_t
    {
        DOWNED_ARMED = 0x01,            // Last Stand: the pistol is up
        DOWNED_FINAL_STAND = 0x02,      // gets back up by itself
        DOWNED_HAS_KIT = 0x04,          // carries a revival kit
    };
    // op 1, 12 bytes: [1, flags u8, remaining ms u32, total ms u32, pistol u16]
    struct DownedMsg
    {
        uint8_t flags = 0;
        uint32_t remainingMs = 0;
        uint32_t totalMs = 0;
        uint16_t pistol = 0;            // the gun to raise when armed, else 0
    };
    std::vector<uint8_t> EncodeDowned(DownedMsg const& m);
    bool DecodeDowned(std::vector<uint8_t> const& body, DownedMsg& out);

    // op 2, 11 bytes: [2, reason u8, health % u8, by guid u64]
    enum class UpReason : uint8_t { Spell = 1, Kit = 2, FinalStand = 3, GameMaster = 4 };
    struct UpMsg
    {
        UpReason reason = UpReason::Spell;
        uint8_t healthPct = 0;
        uint64_t by = 0;                // who brought them up (themselves for a kit on self)
    };
    std::vector<uint8_t> EncodeUp(UpMsg const& m);
    bool DecodeUp(std::vector<uint8_t> const& body, UpMsg& out);

    // op 3, 2 bytes: [3, cause u8]
    enum class DeathCause : uint8_t { BledOut = 1, Finished = 2 };
    std::vector<uint8_t> EncodeDied(DeathCause c);
    bool DecodeDied(std::vector<uint8_t> const& body, DeathCause& out);

    // op 4, 15 bytes: [4, state u8, self use u8, other guid u64, duration ms u32]. To the user:
    // `other` is the target (0 on yourself); to the target: the one working on them.
    enum class KitState : uint8_t { Stopped = 0, Using = 1, Done = 2, Refused = 3 };
    struct KitMsg
    {
        KitState state = KitState::Stopped;
        bool selfUse = false;
        uint64_t other = 0;
        uint32_t durationMs = 0;
    };
    std::vector<uint8_t> EncodeKit(KitMsg const& m);
    bool DecodeKit(std::vector<uint8_t> const& body, KitMsg& out);

    // op 5, 10 bytes: [5, player guid u64, downed u8]
    std::vector<uint8_t> EncodeNear(uint64_t player, bool downed);
    bool DecodeNear(std::vector<uint8_t> const& body, uint64_t& player, bool& downed);

    // op 10, 17 bytes: [10, item guid u64, target guid u64]. Item 0: any kit in the bags; target
    // 0: the server's pick (a downed player selected and in reach, else yourself).
    struct UseRequest
    {
        uint64_t item = 0;
        uint64_t target = 0;
    };
    std::vector<uint8_t> EncodeUse(UseRequest const& r);
    bool DecodeUse(std::vector<uint8_t> const& body, UseRequest& out);
}

#endif
