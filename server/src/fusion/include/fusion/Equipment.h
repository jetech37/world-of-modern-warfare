/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_EQUIPMENT_H
#define FUSION_EQUIPMENT_H

#include "fusion/Common.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Fusion
{
    // MW2's equipment: the lethals (frag, semtex, throwing knife, claymore, C4) and the
    // tacticals (flash, stun, smoke), thrown from the eye or planted at the feet.
    enum class EquipmentKind : uint8_t
    {
        None,
        Frag,
        Semtex,
        ThrowingKnife,
        Claymore,
        C4,
        Flash,
        Stun,
        Smoke,
    };

    EquipmentKind KindOf(std::string const& weaponName);

    // World channel op: a tactical caught this player, [41, 1 flash | 2 stun | 3 smoke], for the
    // client's whiteout and daze.
    constexpr uint8_t kOpTacticalHit = 41;

    // Thrown (an arc from the eye) rather than planted (a claymore at the feet).
    bool IsThrown(EquipmentKind k);

    // How long after it lands it goes off (frag from the throw: MW2 cooks nothing here), < 0 for
    // none: C4 waits for its trigger, a claymore for its tripwire, a knife strikes on contact.
    float FuseSeconds(EquipmentKind k);

    // A throw: yards a second off the hand, and the longest flight followed.
    constexpr float kThrowSpeed = 22.f;
    constexpr float kMaxFlightSeconds = 4.f;

    struct Throw
    {
        Vec3 at;                // where it stopped (or where it was when the flight ran out)
        float seconds = 0.f;    // how long it flew
        bool landed = false;    // it hit something
    };

    // The arc of a throw in WoW's gravity, stepped and tested against the world: `hit(a, b)`
    // gives where a segment first meets something solid.
    Throw ThrowArc(Vec3 origin, Vec3 dir, float speed,
        std::function<std::optional<Vec3>(Vec3, Vec3)> const& hit);

    // A claymore's tripwire: a body within kClaymoreRange in front of it, inside its cone.
    constexpr float kClaymoreRange = 3.f;
    constexpr float kClaymoreHalfAngle = 50.f;  // degrees
    // `range`: the tripwire's reach (Intellect's equipment strength lengthens it, Stats.h).
    bool ClaymoreTripped(Vec3 at, float facing, Vec3 body, float range = kClaymoreRange);

    // A tactical's reach (yards) and how long it holds what it catches (seconds).
    constexpr float kFlashRadius = 10.f, kFlashSeconds = 3.f;
    constexpr float kStunRadius = 8.f, kStunSeconds = 4.f;
    constexpr float kSmokeRadius = 7.f, kSmokeSeconds = 6.f;
}

#endif
