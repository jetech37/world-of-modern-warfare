/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_MOVEMENT_H
#define FUSION_MOVEMENT_H

#include "fusion/Common.h"
#include "fusion/Weapon.h"

#include <optional>

namespace Fusion
{
    // CoD stances (design doc, section 15). The server tracks them and grants the speeds below,
    // so the client never moves faster than it was granted.
    enum class Stance : uint8_t
    {
        Stand,
        Crouch,
        Prone,
        Slide,      // a short burst out of a sprint, at crouch height
    };

    enum class Gait : uint8_t
    {
        Still,
        Walk,
        Run,
        Sprint,
        Backpedal,
    };

    struct MovementState
    {
        Stance stance = Stance::Stand;
        Gait gait = Gait::Run;
        bool aiming = false;            // aiming down sights
        bool marathon = false;          // the perk: no sprint limit
        bool lightweight = false;       // the perk: +7%
    };

    // The share of standing height a stance leaves of the hitbox and the camera.
    float StanceHeightScale(Stance s);

    // The speed multiplier over the character's base run speed. Sprint can't be combined with
    // aiming or a crouch; the weapon's own MW2 weight applies to every gait.
    float SpeedMultiplier(MovementState const& m, WeaponProfile const* held);

    // Sprint stamina: seconds of sprint available, and how fast it comes back.
    struct SprintStamina
    {
        float max = 4.f;
        float current = 4.f;
        float regenPerSecond = 1.f;

        // Advance by dt; returns whether the character may keep sprinting.
        bool Update(float dt, bool sprinting, bool marathon);
    };

    // Seconds after a sprint before the gun can be aimed or fired (MW2's sprint-out time).
    constexpr float kSprintOutTime = 0.25f;
    // Spread multiplier while airborne (applied by the client's bloom; the server has no spread).
    constexpr float kAirborneSpreadMultiplier = 2.f;

    // The movement perks the class carries (filled by the loadout in a later phase).
    // The perks in play (MW2 design spec 3.3), from the equipped class (Fusion::PerksOf).
    struct PerkFlags
    {
        bool marathon = false;
        bool marathonPro = false;
        bool pro = false;               // every perk in play is its Pro (MW2's upgrade)
        bool lightweight = false;
        bool commando = false;
        bool sleightOfHand = false;
        bool scavenger = false;
        bool stoppingPower = false;
        bool hardline = false;
        bool ninja = false;
        bool steadyAim = false;
        bool coldBlooded = false;
        bool oneManArmy = false;
        bool dangerClose = false;
        bool lastStand = false;
        bool scrambler = false;
        bool sitRep = false;
        uint16_t deathstreak = 0;       // the class's deathstreak (PerkId), 0 for none
        // Per perk id (below 32): the kit's slots holding it (stacking, Catalog.h PerkStack), and
        // whether it plays its Pro (a slot's Pro bit, or `pro` for all).
        uint8_t stacks[32] = {};
        uint32_t proBits = 0;

        uint8_t Count(uint16_t id) const { return id < 32 ? stacks[id] : 0; }
        // Callers check the perk itself first: this says only whether it plays as its Pro.
        bool IsPro(uint16_t id) const { return pro || (id < 32 && (proBits >> id & 1u)); }
    };

    // Whether the gun may fire: not while sprinting, nor until kSprintOutTime after.
    bool SprintOutReady(float secondsSinceSprint, bool sprinting);

    // Mantling (design spec, section 5.4): a ledge rising 0.5-1.8 yd within 1.6 yd ahead, with
    // 2 yd of headroom over it, climbed with a knock-back arc in WoW's gravity. The top is above
    // what WoW's own jump clears (about 1.6 yd), so a mantle reaches what a hop can't. Built
    // pieces are ledges too (Build::TopUnder), as the server's height maps don't hold them.
    constexpr float kMantleMinRise = 0.5f;
    constexpr float kMantleMaxRise = 1.8f;
    constexpr float kWowGravity = 19.2911f;     // yards per second squared
    constexpr float kWowJumpSpeed = 7.95797334f;    // WoW's jump: up at this, about 1.64 yd high
    // The request comes with the jump Space starts, so the server may read the body anywhere on
    // its way up: up to this high over the ground (above the jump's top, for the slack of the
    // height maps), and up to this long in the air (its rise, and a quarter second of lag).
    // Longer is a fall, not a jump.
    constexpr float kMantleMaxStartHeight = 1.8f;
    constexpr float kMantleMaxAirSeconds = kWowJumpSpeed / kWowGravity + 0.25f;
    constexpr float kMantleMinForward = 7.f;    // yards per second: never slower than a run
    constexpr int kMantleColumns = 4;
    constexpr float kMantleProbeReach[kMantleColumns] = {0.4f, 0.8f, 1.2f, 1.6f};
    constexpr float kMantleReach = 1.6f;        // the farthest column
    constexpr float kMantleHeadroom = 2.f;
    constexpr float kMantleSeconds = 0.6f;

    struct MantleProbe
    {
        float feetZ = 0.f;
        float groundAhead[kMantleColumns] = {};     // at kMantleProbeReach ahead
        bool headroomClear = false;
        float airSeconds = 0.f;                     // since the feet left the ground
        float startHeight = 0.f;                    // the body over the ground under it
    };

    // What the player's body is doing, for the gates on MW2's movement actions and fire.
    struct ActorState
    {
        bool alive = true;
        bool mounted = false, taxi = false, swimming = false;
        bool stunned = false, rooted = false, confused = false, fleeing = false;
        bool pacified = false;
        bool inSpline = false;      // a server-driven move (a knock-back, a charge) still running
        bool airborne = false;      // jumping or falling
        Stance stance = Stance::Stand;
    };

    // Sprint over a run, and Lightweight over everything. Above MW2's 1.5 and 1.07: WoW's run
    // (7 yd/s) is already quicker than MW2's, and MW2's margins were too small to feel here.
    constexpr float kSprintSpeedScale = 1.65f;
    constexpr float kLightweightSpeedScale = 1.12f;

    enum class MoveAction : uint8_t { Mantle, Lunge };

    // What the server's move speed depends on from the client's state: the stance, the gun in
    // hand as the server keeps it (0 for none or a locked one) and aiming. A change re-sends it.
    struct SpeedInputs
    {
        Stance stance = Stance::Stand;
        uint16_t heldWeaponId = 0;
        bool aiming = false;
    };
    inline bool SpeedInputsChanged(SpeedInputs const& a, SpeedInputs const& b)
    {
        return a.stance != b.stance || a.heldWeaponId != b.heldWeaponId || a.aiming != b.aiming;
    }

    // A mantle or lunge moves the player by knock-back, which a stun, root or running spline
    // would silently skip, so they are refused outright; crowd control and pacify also stop the
    // knife. The lunge is from a standing body only (not crouched, prone or sliding).
    bool CanMoveAction(ActorState const& s, MoveAction a);
    // Gunfire: not dead, stunned, confused or fleeing (rooted still shoots).
    bool CanFire(ActorState const& s);

    // The ledge's rise over the feet, or none when there is no ledge to climb.
    std::optional<float> MantleRise(MantleProbe const& p);

    struct Impulse
    {
        float horizontal = 0.f;     // yards per second along the facing
        float vertical = 0.f;       // yards per second up
    };

    // The knock-back that carries the body 0.3 yd over a ledge of this rise and onto it.
    Impulse MantleImpulse(float rise);

    // The knife lunge (design spec, section 5.5): MW2's range, a 60° cone in front, a quick dash.
    float LungeRange(PerkFlags const& perks);
    bool InLungeCone(Vec3 from, float facing, Vec3 to);
    Impulse LungeImpulse(float distance);
    // A creature takes three body shots' worth; a one-hit kill would trivialise PvE.
    float LungeCreatureDamage(float bodyShotHealth);

    // Whether a melee press may lunge (WOMW: a swing is a swing; only the warrior's Charge carries
    // the body forward). The knife lunge stays as a grant: nothing gives it today, and a Charge
    // perk or talent meant to dash on the swing sets `charge` (FusionClasses::GrantsSwingLunge).
    // Without it the server drops a lunge request and sends the client a lunge time of 0.
    struct LungeGrant
    {
        bool charge = false;
    };
    bool SwingLunges(LungeGrant const& grant);
}

#endif
