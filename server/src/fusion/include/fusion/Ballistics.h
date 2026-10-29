/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_BALLISTICS_H
#define FUSION_BALLISTICS_H

#include "fusion/Common.h"
#include "fusion/HitValidation.h"
#include "fusion/Weapon.h"

#include <cstdint>
#include <vector>

namespace Fusion
{
    // A pull's pellets, as MW2 scatters them (Bullet_Endpos): each pellet's direction is a point
    // uniform in the disc of radius tan(spread) one unit along the aim, from the pull's seed.
    // The client draws the same seed, so both see the same pellets (benilla-protocol's
    // pellet_hash / pellet_direction are this, bit for bit in the hash).
    uint32_t PelletHash(uint32_t seed, uint32_t index);
    Vec3 PelletDirection(Vec3 dir, uint32_t seed, uint32_t index, float spreadDeg);

    // No cone is wider than this (degrees): past it a claim is noise.
    constexpr float kMaxSpreadDeg = 30.f;

    // The least cone the server accepts for a shot: the gun's aimed spread (10% slack; a rifle,
    // LMG or sniper's held to kGunfight.adsCapDeg), or at the hip its standing least as the client
    // draws it (kGunfight.hipSpreadScale) lowered as far as prone (half) and Steady Aim (0.65)
    // take it.
    float LeastSpread(WeaponProfile const& w, bool ads);
    // The client's claimed cone, held between that least and kMaxSpreadDeg (a NaN claims none).
    float ClampSpread(WeaponProfile const& w, bool ads, float claimedDeg);
    // The aimed cone a gun is drawn with: its own, held to kGunfight.adsCapDeg for a rifle, LMG
    // or sniper.
    float AimedSpread(WeaponProfile const& w);

    // How far a pellet carries: a shotgun's to kShotgunReach times its least-damage range (at the
    // least damage past it; MW2's own cut-off at that range was too short at WoW's scale), the
    // range stretched as its falloff is (FalloffRangeScale); a bullet to the server's shot range.
    constexpr float kShotgunReach = 2.f;
    float PelletRange(WeaponProfile const& w);

    // A body a pellet's ray hit, and how far along.
    struct PelletCandidate
    {
        uint64_t guid = 0;
        float distance = 0.f;
    };
    // The nearest (its index), -1 for none: the first body in the way takes the pellet.
    int NearestCandidate(std::vector<PelletCandidate> const& c);

    // How far above its server position a creature's body is drawn, yards: a flyer, a hoverer,
    // or a family that walks the ground on the server while its model flies over it (carrion
    // birds, bats, owls, wind serpents). Its hitbox grows by this upward, so a shot at the model
    // lands.
    constexpr float kFlyerLift = 2.5f;
    float BodyLift(uint32_t family, bool canFly, bool hovering);

    // FMJ (MW2's deeper penetration): a round goes through the first body into the next at
    // kFmjBodyScale of its damage, and through a wall up to kFmjWallDepth yards thick at
    // kFmjWallScale. Other rounds stop at the first body and at any wall.
    constexpr float kFmjBodyScale = 0.75f;
    constexpr float kFmjWallDepth = 1.f;
    constexpr float kFmjWallScale = 0.5f;
    int BodiesPierced(WeaponProfile const& w);

    // A splash weapon (a launcher, an underbarrel grenade): its round bursts where it lands and
    // hurts everything within its radius, the inner damage at the centre fading to the outer at
    // the edge (MW2's explosionInnerDamage / OuterDamage).
    bool IsExplosive(WeaponProfile const& w);
    float SplashDamage(WeaponProfile const& w, float distance);
    // The damage share of the `index`th body on a round's path (0 = the first).
    float PierceScale(int index);
    // Whether a round goes through a wall `thickness` yards thick.
    bool ThroughWall(WeaponProfile const& w, float thickness);
}

#endif
