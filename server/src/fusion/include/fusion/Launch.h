/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_LAUNCH_H
#define FUSION_LAUNCH_H

#include "fusion/Common.h"
#include "fusion/Weapon.h"

#include <cstdint>
#include <functional>
#include <vector>

// Launchers' rounds fly: the server finds where one lands as it leaves the tube and sets it off
// when it gets there, and tells the players about so their clients can draw the round in flight
// and every blast.
namespace Fusion { namespace Launch
{
    // Yards a second: a rocket (RPG-7, AT4, Javelin, Stinger) 40, a 40 mm grenade (the M79 and
    // every underbarrel launcher) 45, MW2's launcher reaching across a map (some 135 yards at 45°).
    constexpr float kRocketSpeed = 40.f;
    constexpr float kGrenadeSpeed = 45.f;
    bool IsGrenadeLauncher(WeaponProfile const& w);
    float ProjectileSpeed(WeaponProfile const& w);
    // Flight time in ms (at least one, at most 65535).
    uint16_t FlightMs(float distance, float speed);

    // A rocket flies straight along the aim. A 40 mm grenade is lobbed: it leaves the muzzle at
    // aim * kGrenadeSpeed and falls at kGrenadeGravity (yd/s^2, down along z), so a grenade fired
    // level drops into the ground ahead instead of bursting in mid air. The server steps the arc
    // kGrenadeStepS at a time and the round bursts at its first contact, or wherever it is after
    // kGrenadeMaxFlightS.
    constexpr float kGrenadeGravity = 15.f;
    constexpr float kGrenadeStepS = 0.02f;
    constexpr float kGrenadeMaxFlightS = 6.f;
    // p(t) = origin + velocity t + (0, 0, -gravity) t^2 / 2.
    Vec3 ArcAt(Vec3 origin, Vec3 velocity, float gravity, float t);
    // Tests the segment a -> b; on a contact sets fraction (0..1 along it) and returns true.
    using SegmentContact = std::function<bool(Vec3 a, Vec3 b, float& fraction)>;
    struct ArcEnd
    {
        Vec3 at;                // the burst point, exactly ArcAt(flightMs / 1000)
        uint16_t flightMs = 1;
        bool contact = false;   // false: it ran out of time in the air
    };
    // Flies the arc until contact or maxSeconds. The burst is backOff yards (along the arc) short of
    // the contact, so the blast's sight lines start in the open, and its time is whole milliseconds
    // so the client's rebuilt arc (op 31 below) passes through exactly the same points.
    ArcEnd FlyArc(Vec3 origin, Vec3 velocity, float gravity, float step, float maxSeconds, float backOff,
        SegmentContact const& contact);
    // The velocity that carries a round from `from` to `to` in `seconds` under `gravity` (a lob).
    Vec3 LobVelocity(Vec3 from, Vec3 to, float gravity, float seconds);

    // Players this near hear of launches and blasts.
    constexpr float kTellRadius = 200.f;

    // SMSG_FUSION_CHANNEL (kChannelWorld) bodies, little-endian:
    // op 30, 19 bytes: [30, x y z f32, radius f32 (yards), weapon u16]
    // op 31, 37 bytes: [31, shooter guid u64, weapon u16, from x y z f32, to x y z f32, flight_ms u16]
    //   A rocket flies the straight line from -> to in flight_ms. A grenade (IsGrenadeLauncher) flies
    //   the arc p(t) = from + v0 t + g t^2 / 2 with g = (0, 0, -kGrenadeGravity) and
    //   v0 = (to - from - g T^2 / 2) / T, T = flight_ms / 1000: from is the muzzle, to the burst
    //   point and flight_ms the simulated time, so the client needs nothing else to draw it.
    constexpr uint8_t kOpExplosion = 30;
    constexpr uint8_t kOpLaunch = 31;
    std::vector<uint8_t> EncodeExplosion(Vec3 at, float radius, uint16_t weapon);
    std::vector<uint8_t> EncodeLaunch(uint64_t shooter, uint16_t weapon, Vec3 from, Vec3 to, uint16_t flightMs);
}}

#endif
