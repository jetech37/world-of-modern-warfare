/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Weapon.h"
#include "fusion/Clock.h"
#include "fusion/Movement.h"

#include <limits>

using namespace Fusion;

TEST(a_stunned_or_rooted_player_can_neither_mantle_nor_lunge)
{
    ActorState free;
    CHECK(CanMoveAction(free, MoveAction::Mantle));
    CHECK(CanMoveAction(free, MoveAction::Lunge));
    for (bool ActorState::*cc : {&ActorState::stunned, &ActorState::rooted, &ActorState::confused,
             &ActorState::fleeing, &ActorState::inSpline, &ActorState::mounted, &ActorState::taxi,
             &ActorState::swimming})
    {
        ActorState s;
        s.*cc = true;
        CHECK(!CanMoveAction(s, MoveAction::Mantle));
        CHECK(!CanMoveAction(s, MoveAction::Lunge));
    }
    ActorState dead;
    dead.alive = false;
    CHECK(!CanMoveAction(dead, MoveAction::Lunge));
    ActorState pacified;
    pacified.pacified = true;           // no melee: no knife, but a climb is fine
    CHECK(!CanMoveAction(pacified, MoveAction::Lunge));
    CHECK(CanMoveAction(pacified, MoveAction::Mantle));
    ActorState air;
    air.airborne = true;                // a lunge from a jump flung the player
    CHECK(!CanMoveAction(air, MoveAction::Lunge));
    CHECK(CanMoveAction(air, MoveAction::Mantle));
}

TEST(the_lunge_is_from_a_standing_body_only)
{
    for (Stance st : {Stance::Crouch, Stance::Prone, Stance::Slide})
    {
        ActorState s;
        s.stance = st;
        CHECK(!CanMoveAction(s, MoveAction::Lunge));
        CHECK(CanMoveAction(s, MoveAction::Mantle));
    }
    ActorState standing;
    standing.stance = Stance::Stand;
    CHECK(CanMoveAction(standing, MoveAction::Lunge));
}

TEST(a_shield_or_knife_fires_no_rounds)
{
    for (WeaponClass c : {WeaponClass::RiotShield, WeaponClass::Melee})
        CHECK(!FiresRounds(c));
    for (WeaponClass c : {WeaponClass::AssaultRifle, WeaponClass::Shotgun, WeaponClass::Pistol})
        CHECK(FiresRounds(c));
}

TEST(a_stunned_player_cannot_fire)
{
    ActorState s;
    CHECK(CanFire(s));
    s.rooted = true;                    // rooted still shoots
    CHECK(CanFire(s));
    for (bool ActorState::*cc : {&ActorState::stunned, &ActorState::confused, &ActorState::fleeing})
    {
        ActorState t;
        t.*cc = true;
        CHECK(!CanFire(t));
    }
    ActorState dead;
    dead.alive = false;
    CHECK(!CanFire(dead));
}

TEST(no_mantle_from_high_in_the_air)
{
    MantleProbe p;
    p.feetZ = 0.f;
    p.groundAhead[2] = 1.2f;
    p.headroomClear = true;
    p.startHeight = 0.3f;               // just off the ground with the jump
    CHECK(MantleRise(p).has_value());
    p.startHeight = kMantleMaxStartHeight + 0.1f;   // a second press at the top of a jump
    CHECK(!MantleRise(p).has_value());
}

TEST(seconds_since_survives_the_wrap_and_never)
{
    CHECK(SecondsSince(5000, std::optional<uint32_t>(3000)) == 2.f);
    CHECK(SecondsSince(0x100u, std::optional<uint32_t>(0xFFFFFF00u)) > 0.5f);
    CHECK(SecondsSince(0x100u, std::optional<uint32_t>(0xFFFFFF00u)) < 0.52f);
    // A day-25 uptime against a player who never sprinted: never, not negative.
    CHECK(SecondsSince(0x90000000u, std::nullopt) == std::numeric_limits<float>::infinity());
}

TEST(a_locked_gun_does_not_resend_the_speed)
{
    // The server keeps a locked gun as none (0): the same locked gun next message is no change.
    SpeedInputs const was{Stance::Stand, 0, false};
    CHECK(!SpeedInputsChanged(was, {Stance::Stand, 0, false}));
    CHECK(SpeedInputsChanged(was, {Stance::Crouch, 0, false}));
    CHECK(SpeedInputsChanged(was, {Stance::Stand, 7, false}));
    CHECK(SpeedInputsChanged(was, {Stance::Stand, 0, true}));
}
