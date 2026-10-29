/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_GUNFIGHTS_H
#define FUSION_GUNFIGHTS_H

#include "fusion/Common.h"
#include "fusion/Movement.h"

#include <cstdint>
#include <vector>

namespace Fusion
{
    // ── Gunfights: the tuning table ─────────────────────────────────────────────────────────
    // Every number that decides how a gunfight feels, in one place: how far a player's rounds
    // carry and how their damage falls off, the hip and aimed cones, an NPC gunner's aim, how far
    // gunfire is heard and how many it draws, the call for help, and the chase. The rules below
    // and the game glue read them from kGunfight; tune here. (The client mirrors the three player
    // numbers it draws with: client/src/aim.rs MAX_RANGE, client/src/spread.rs HIP_SCALE and
    // ADS_CAP_DEG.)
    struct GunfightTuning
    {
        // ── Player guns ──
        // The farthest a round carries and the server checks a hit (yards); past what a 1.12
        // client draws units at, so nothing in sight is out of reach. The crosshair's reach too.
        float shotRange = 150.f;
        // The hip cone the client draws, as a share of MW2's own (its WeaponDef's): MW2 scatters
        // its rounds toward the middle of the cone, ours spread evenly over it, so the same
        // degrees felt wilder; this brings the middle of the spread back to MW2's.
        float hipSpreadScale = 0.7f;
        // The aimed cone of a rifle, LMG or sniper at most (degrees): down the sights is on.
        float adsCapDeg = 0.1f;
        // MW2's damage falloff ranges stretched for WoW's longer fights (a camp is pulled from 40
        // yards and more, not round an MW2 corner): full damage out to maxDamageRange times this,
        // the least from minDamageRange times this, linear between, and the least past it (no cap
        // short of shotRange).
        float falloffRifle = 1.5f;      // assault rifles
        float falloffLmg = 1.5f;
        float falloffSmg = 1.3f;        // SMGs, machine pistols and pistols
        float falloffSniper = 1.f;      // already full damage to 111 yards
        float falloffShotgun = 1.f;     // a shotgun's pellets stop at kShotgunReach of its least
        // A moving creature is tested where it was this long before the shot as well as where the
        // shot's time puts it: the client draws creatures a little behind the server (yards are
        // lost to latency at a charge's speed).
        uint32_t creatureRewindMs = 150;

        // ── NPC gunners' aim (NpcAimCone) ──
        // The cone (half-angle, degrees) when it first has the target in sight, narrowing to its
        // settled cone over settleMs of holding its aim.
        float acquireConeDeg = 9.f;
        float settledConeDeg = 3.f;
        uint32_t settleMs = 1500;
        // A sight break shorter than this keeps half the settling; a longer one starts it over.
        uint32_t sightGraceMs = 600;
        // Past this many yards the cone widens by coneDegPerYard each yard.
        float coneWidensFrom = 15.f;
        float coneDegPerYard = 0.03f;
        // The target's movement widens it.
        float walkCone = 1.15f;
        float runCone = 1.35f;
        float sprintCone = 1.8f;
        float backpedalCone = 1.2f;
        // Only the target's upper body in sight (partial cover): a wider cone, a smaller body.
        float partialCoverCone = 1.5f;
        float partialCoverBody = 0.6f;
        // Rank: elites and rares aim tighter.
        float eliteCone = 0.75f;
        float rareCone = 0.85f;
        float rareEliteCone = 0.65f;
        float bossCone = 0.6f;
        // The body a round must land within (yards from the chest's line), by the target's stance.
        float standBody = 0.55f;
        float crouchBody = 0.45f;
        float proneBody = 0.3f;
        // The hit chance is held between these; the level gap moves it levelGapPerLevel a level.
        float minHitChance = 0.02f;
        float maxHitChance = 0.75f;
        float levelGapPerLevel = 0.015f;
        // A miss passes at least this far from the body, so its tracer reads as a near miss.
        float missClearance = 0.35f;

        // ── Gunfire heard (WhoHearsTheShot) ──
        float hearingOpen = 60.f;       // an unsuppressed shot, yards
        float hearingSilenced = 15.f;   // a suppressor
        float coldBloodedHearing = 0.5f;
        float blastHearing = 60.f;      // an explosion
        // A creature with no line of sight to the shooter hears this share of it, and comes after
        // a beat (it investigates first) of hearingDelayMinMs..MaxMs.
        float wallMuffle = 0.5f;
        uint32_t hearingDelayMinMs = 800;
        uint32_t hearingDelayMaxMs = 1800;
        // At most this many creatures drawn by one shot, and by one shooter in hearingWindowMs; a
        // shooter's shots are weighed at most once every hearingEveryMs.
        uint32_t hearingPerShot = 3;
        uint32_t hearingPerWindow = 8;
        uint32_t hearingWindowMs = 10000;
        uint32_t hearingEveryMs = 500;

        // ── A creature shot calls its own (RallyAllies) ──
        float callForHelpRadius = 20.f;
        uint32_t callForHelpMax = 4;    // allies a call brings
        uint32_t callForHelpEveryMs = 3000;

        // ── The chase (ChaseSpeedMultiplier) ──
        // An aggroed creature runs this much faster toward a player until it is within its reach
        // (melee, or casterRange for a caster) plus chaseSlowBand, and at its own pace from there.
        float chaseSpeed = 1.6f;
        float chaseSlowBand = 2.f;      // yards past its reach where it eases off
        float chaseFastBand = 4.f;      // ... and speeds up again past its reach plus this
        float casterRange = 30.f;

        // A creature fighting a player hand to hand stands at least this far from the player's
        // centre (yards, plus its own bounding radius): WoW's chase runs it to a moving target's
        // 1.5 yards, inside a first-person camera. Melee reaches 5.
        float meleeKeepOff = 2.5f;

        // Creatures chase this many times further before giving up (guns reach 100 yards and more).
        float leashScale = 2.f;
    };

    constexpr GunfightTuning kGunfight{};

    // ── Player guns ──
    // The share of a weapon class's MW2 falloff ranges it fights at (kGunfight).
    float FalloffRangeScale(uint8_t weaponClass);

    // ── NPC gunners' aim ──
    // An aim cone that opens wide when the gunner first sees its target and closes as it holds
    // its aim on it; it widens with range, the target's movement and cover; elites aim tighter.
    struct NpcAimInput
    {
        float distance = 0.f;
        uint32_t heldMs = 0;            // its aim held on the target (NpcAimTrack::HeldMs)
        Gait gait = Gait::Still;
        bool sprinting = false;
        bool partialCover = false;      // the target's lower body is hidden
        CreatureRank rank = CreatureRank::Normal;
    };
    float NpcAimCone(NpcAimInput const& in);
    // The body a round must land within, by the target's stance and cover (yards).
    float NpcTargetBody(Stance stance, bool partialCover);
    // The chance a round in the cone lands on a body `body` yards wide at `distance`: MW2
    // scatters rounds toward the middle of the cone (the radius uniform, not the area), so it is
    // body / (distance * tan(cone)), held to the table's bounds and moved by the level gap (the
    // NPC's level less the target's).
    float NpcConeHitChance(float coneDeg, float distance, float body, int32_t levelGap);
    // How far beside the chest a missed round passes (yards): somewhere in the cone outside the
    // body, by `roll01` in [0, 1].
    float NpcMissOffset(float coneDeg, float distance, float body, float roll01);

    // How long a gunner has held its aim on its target. Sight lost resets it (a short break keeps
    // half), as does a new target.
    struct NpcAimTrack
    {
        uint64_t target = 0;
        bool holding = false;
        uint32_t sinceMs = 0;           // holding: aim held since; else lost since
        uint32_t keptMs = 0;            // settling carried over a short break
        void Update(uint64_t targetGuid, bool sight, uint32_t nowMs);
        uint32_t HeldMs(uint32_t nowMs) const;
    };

    // ── Gunfire heard ──
    // How far a shot is heard: kGunfight's open or suppressed range, Cold-Blooded's share.
    float GunfireHearing(bool silenced, bool coldBlooded);

    struct Listener
    {
        uint64_t guid = 0;
        float distance = 0.f;           // yards from the shot
        bool lineOfSight = false;       // it sees the shooter
    };
    struct HeardBy
    {
        uint64_t guid = 0;
        uint32_t delayMs = 0;           // 0: it comes at once
    };
    // Which listeners a shot heard `radius` yards draws: those in sight of the shooter within the
    // radius at once, the others within its muffled share after a beat; nearest first, at most
    // kGunfight.hearingPerShot and `budget`. `roll` spreads the beats.
    std::vector<HeardBy> WhoHearsTheShot(std::vector<Listener> listeners, float radius, uint32_t budget, uint32_t roll);

    // A shooter's rolling allowance of creatures drawn by gunfire (kGunfight.hearingPerWindow in
    // hearingWindowMs).
    struct HearingBudget
    {
        uint32_t windowStartMs = 0;
        uint32_t used = 0;
        bool started = false;
        uint32_t Left(uint32_t nowMs);
        void Spend(uint32_t n, uint32_t nowMs);
    };

    // ── The chase ──
    // How much faster an aggroed creature runs at a player `distance` yards off, given the reach
    // it fights from (melee, or kGunfight.casterRange for a caster) and whether it is running fast
    // now (the bands keep it from flickering at the edge).
    float ChaseSpeed(float distance, float reach, bool fastNow);
}

#endif
