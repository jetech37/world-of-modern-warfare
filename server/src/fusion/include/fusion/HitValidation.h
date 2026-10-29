/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_HIT_VALIDATION_H
#define FUSION_HIT_VALIDATION_H

#include "fusion/Common.h"
#include "fusion/Gunfights.h"
#include "fusion/Movement.h"
#include "fusion/Weapon.h"

#include <deque>
#include <functional>
#include <optional>

namespace Fusion
{
    // ── The standard hitbox ─────────────────────────────────────────────────────────────────
    // Every player is the same height (races are scaled to it), so one capsule with a head zone
    // serves all; stances shorten it (design doc, sections 9 and 15). Creatures pass their own
    // height and radius from their model bounds.
    struct HitboxShape
    {
        float height = 2.0f;            // yards standing; feet at the unit's position
        float radius = 0.45f;
        float headRadius = 0.16f;
        float headOffset = 0.18f;       // the head's centre, this far below the top
    };

    struct BodyPose
    {
        Vec3 feet;                      // the unit's position (feet)
        float orientation = 0.f;        // radians, unused by the capsule but kept for creatures
        Stance stance = Stance::Stand;
    };

    struct RayHit
    {
        float distance = 0.f;
        HitLocation location = HitLocation::TorsoUpper;
    };

    // The nearest hit of a ray on the capsule, with the location from where it struck: the head
    // sphere first, then height bands down the body. Server-side ground truth (the referee).
    std::optional<RayHit> RayVsBody(Vec3 origin, Vec3 dir, BodyPose const& pose, HitboxShape const& shape);

    // Distance from a ray to the head's centre: how close a claimed headshot came to the
    // server's head zone.
    float RayHeadMiss(Vec3 origin, Vec3 dir, BodyPose const& pose, HitboxShape const& shape);

    // ── Lag compensation ────────────────────────────────────────────────────────────────────
    // About one second of a unit's poses, so a shot is checked against where the target was on
    // the shooter's screen.
    class PoseHistory
    {
    public:
        static constexpr uint32_t kWindowMs = 1000;

        void Record(uint32_t timeMs, BodyPose const& pose);
        // The pose at a past time, interpolated; nullopt outside the window.
        std::optional<BodyPose> At(uint32_t timeMs) const;
        size_t Size() const { return m_samples.size(); }

    private:
        struct Sample
        {
            uint32_t time;
            BodyPose pose;
        };
        std::deque<Sample> m_samples;
    };

    // ── Validating one shot ─────────────────────────────────────────────────────────────────
    struct ShotClaim
    {
        uint32_t shooterTimeMs = 0;     // the shooter's view time (server clock, after sync)
        Vec3 origin;
        Vec3 direction;
        bool claimsHead = false;        // the client's bone said head
    };

    struct ShotContext
    {
        WeaponProfile const* weapon = nullptr;
        Vec3 shooterPosition;           // where the server has the shooter
        HitboxShape targetShape;
        uint32_t serverTimeMs = 0;
        uint32_t lastShotTimeMs = 0;    // the shooter's previous accepted shot
        bool hasLastShot = false;
        bool magazineHasRound = true;
        // How far the shot may start from shooterPosition (more for a rider: Riding.h).
        float originTolerance = 3.f;
        // Line of sight between two points (the server's world collision).
        std::function<bool(Vec3, Vec3)> lineOfSight;
    };

    enum class ShotVerdict : uint8_t
    {
        Hit,
        Miss,
        RejectedFireRate,
        RejectedEmpty,
        RejectedOrigin,     // the shot didn't start near the shooter
        RejectedTooOld,     // outside the lag-compensation window
        RejectedRange,
        RejectedNoSight,
    };

    struct ShotResult
    {
        ShotVerdict verdict = ShotVerdict::Miss;
        HitLocation location = HitLocation::TorsoUpper;
        float distance = 0.f;
        bool headAccepted = false;
    };

    // The tolerance (yards) a claimed headshot may miss the server's head zone by: the client
    // hit the real animated mesh, the server only knows the standing zone.
    constexpr float kHeadClaimTolerance = 0.25f;
    // How far the shot's origin may sit from the shooter's eye (latency and the stance's camera).
    constexpr float kOriginTolerance = 3.f;
    // The longest any weapon reaches (kGunfight.shotRange): past it a round is gone, short of it
    // a hit lands and the falloff (CodDamageAtRange) decides the damage.
    constexpr float kMaxShotRange = kGunfight.shotRange;

    ShotResult ValidateShot(ShotClaim const& shot, ShotContext const& ctx, PoseHistory const& target);

    // Whether a round passes through a body instead of stopping on it: the body is on the shooter's
    // side. `controller` is the player controlling the body (itself for a player, the owner or
    // charmer of a pet, a companion, a mount, a totem), 0 for none; `controllerGrouped` whether that
    // player is in the shooter's party or raid.
    inline bool ShotPassesThrough(uint64_t shooter, uint64_t controller, bool controllerGrouped)
    {
        return controller != 0 && (controller == shooter || controllerGrouped);
    }

    // The point a client's shot leaves from, as the server acts on it: the client's own (it sees
    // its muzzle and camera) when finite and within `tolerance` of the server's eye, else the eye.
    // Every use of a shot's origin (blasts, thrown equipment, knives, building pieces, tracers)
    // takes this one, so a client can't start a rocket beside its target or outside the map.
    Vec3 ShotOrigin(Vec3 claimed, Vec3 eye, float tolerance);

    // Whether a primary or secondary gun is in the shooter's hands: the equipped class's own, a
    // picked-up gun, or Last Stand's pistol. Not any gun the account has unlocked (each with a
    // fresh magazine of its own).
    bool GunInHand(uint16_t gunId, uint16_t classPrimary, uint16_t classSecondary, bool pickedUp,
        uint16_t lastStandPistol);

    // Whether a shot the client stamped at `shotTimeMs` (server clock) is recent enough to be
    // acted on: within the lag-compensation window. Older ones were saved up to go off at once.
    bool ShotFresh(uint32_t shotTimeMs, uint32_t nowMs);

    // The share of a weapon's fire time two of its shots must at least be apart. 0.8 leaves room
    // for a connection's jitter (shots bunched on the way), at the cost of a sustained 1.25x rate
    // for a client that fires early on purpose. An explosive (a launcher, an underbarrel grenade:
    // anything with a splash) is held to 0.9 (1.11x): a rocket or grenade a fifth sooner is worth
    // far more than a bullet, and their slow cycles leave jitter a small share (security review
    // 2026-10, #20).
    constexpr float kFireRateSlack = 0.8f;
    constexpr float kExplosiveFireRateSlack = 0.9f;
    float FireRateSlack(WeaponProfile const& w);
}

#endif
