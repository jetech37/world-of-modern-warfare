/*
 * Azeroth Warfare: the fusion rules (Modern Warfare 2 combat on a 1.12 world).
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef FUSION_COMMON_H
#define FUSION_COMMON_H

#include <algorithm>
#include <cmath>
#include <cstdint>

// The fusion rules depend on nothing in the server: every rule is plain data in, plain data out,
// so each one is unit tested on its own (src/fusion/tests) and the game calls in at its decision
// points. Distances are yards, times seconds unless a name says otherwise.
namespace Fusion
{
    struct Vec3
    {
        float x = 0.f, y = 0.f, z = 0.f;

        Vec3() = default;
        constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

        Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
        float Dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
        float Length() const { return std::sqrt(Dot(*this)); }
        Vec3 Normalized() const
        {
            float l = Length();
            return l > 1e-6f ? *this * (1.f / l) : Vec3{};
        }
    };

    inline float Clamp(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }
    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    // WoW's three factions as the rules see them; creatures carry their own hostility.
    enum class Team : uint8_t
    {
        Alliance,
        Horde,
        Neutral,
    };

    // 1.12 creature ranks (creature_template.rank).
    enum class CreatureRank : uint8_t
    {
        Normal = 0,
        Elite = 1,
        RareElite = 2,
        WorldBoss = 3,
        Rare = 4,
        // Not a 1.12 rank: a dungeon boss is flagged by the instance scripts.
        DungeonBoss = 5,
    };

    // 1.12 creature types (CreatureType.dbc).
    enum class CreatureType : uint8_t
    {
        None = 0,
        Beast = 1,
        Dragonkin = 2,
        Demon = 3,
        Elemental = 4,
        Giant = 5,
        Undead = 6,
        Humanoid = 7,
        Critter = 8,
        Mechanical = 9,
        NotSpecified = 10,
        Totem = 11,
    };

    // A player's heaviest armor type, for special-ammo matchups.
    enum class ArmorClass : uint8_t
    {
        Cloth,
        Leather,
        Mail,
        Plate,
    };
}

#endif
