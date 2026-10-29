/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_DANGER_H
#define FUSION_DANGER_H

#include "fusion/Common.h"
#include "fusion/Scale.h"

#include <cstdint>
#include <vector>

// A more dangerous world (design doc, section 10, and its note on dungeons and raids): guns kill
// far faster than vanilla's swords and spells, so creatures get more health and hit harder, on top
// of the CoD scale (Scale.h), without a quest or a boss script being touched. One table, by kind of
// creature (its rank, and for a boss where it stands), eased in over the first levels and, inside
// an instance, scaled by the instance (a twenty-man raid is half a forty-man's).
//
// The reasoning behind the defaults, a level 60 with an M4 (30 a round, 400 a second; guns hit
// creatures by level only, M 3 at 60, gear adds nothing; about half of that sustained, over
// reloads and misses: ~600 a second):
// - A normal creature on the CoD scale has 250 health: 0.4 s, three rounds. At x2 it takes six
//   or so, under a second of fire: still a gun's kill, no longer a sneeze.
// - An elite (x3 on the scale) at x3.5 is 2,625: four or five seconds alone, a magazine's worth.
// - A dungeon boss (3,125) at x32 is 100,000: some 35 s for a level 60 five, 40 s for a level
//   20 five (Deadmines): long enough for its mechanics to play out.
// - A raid boss (11,600 at 63) at x80 is 0.93 million: under 80 s for twenty, 2.5 min for ten.
// - Creature melee and spells (35 a swing at 60 on the scale, against 300 to 600 health) hit 1.4
//   to 1.6 times as hard; NPC guns, already lethal in a camp, only up to 1.25 times.
namespace Fusion { namespace Danger
{
    // The kinds the table is kept by: the template's rank, and a boss by where it stands.
    enum class Kind : uint8_t { Normal, Rare, Elite, RareElite, DungeonBoss, RaidBoss, WorldBoss, Count };

    struct Multipliers
    {
        float health = 1.f;
        float damage = 1.f;         // its melee and spells on a player
        float gunDamage = 1.f;      // an NPC gunner's rounds and grenades
    };

    // An instance of its own size: a share of the extra over 1 the kind gives (all of it, trash
    // and bosses alike).
    struct MapOverride
    {
        uint32_t map = 0;
        float health = 1.f;
        float damage = 1.f;
    };

    struct Config
    {
        Multipliers kinds[size_t(Kind::Count)] = {
            {2.0f, 1.4f, 1.0f},     // Normal
            {2.5f, 1.4f, 1.1f},     // Rare
            {3.5f, 1.5f, 1.15f},    // Elite
            {4.5f, 1.6f, 1.2f},     // RareElite
            {32.f, 1.5f, 1.25f},    // DungeonBoss
            {80.f, 1.5f, 1.25f},    // RaidBoss
            {80.f, 1.5f, 1.25f},    // WorldBoss
        };
        // A boss's own kind carries its place; the rest of an instance (its trash) gets these.
        Multipliers dungeonTrash{1.5f, 1.1f, 1.f};
        Multipliers raidTrash{3.f, 1.2f, 1.f};
        // The first levels are eased: the extra over 1 is easeStart of itself at level 1, rising
        // to all of it at easeFullLevel.
        float easeStart = 0.5f;
        uint32_t easeFullLevel = 15;
        // The realm's knobs (Fusion.Danger.Health, Fusion.Danger.Damage): how much of the extra
        // over 1 is kept (0 turns the scaling off, 2 doubles it).
        float healthKnob = 1.f;
        float damageKnob = 1.f;
        std::vector<MapOverride> maps = {
            {309, 0.5f, 1.f},       // Zul'Gurub: twenty players
            {509, 0.5f, 1.f},       // Ruins of Ahn'Qiraj: twenty players
            {229, 1.5f, 1.f},       // Blackrock Spire: ten in 1.12, against a dungeon's five
            {533, 1.25f, 1.1f},     // Naxxramas: the last raid
        };
    };

    Config const& Defaults();

    Kind KindOf(Scale::CreatureFacts const& f);

    // The full multipliers for a creature on map `map`: the kind's (and its instance's trash
    // share), eased by level, the knobs and the map's override applied.
    Multipliers For(Scale::CreatureFacts const& f, uint32_t map, Config const& c = Defaults());
    float HealthMultiplier(Scale::CreatureFacts const& f, uint32_t map, Config const& c = Defaults());
    float DamageMultiplier(Scale::CreatureFacts const& f, uint32_t map, Config const& c = Defaults());
    float GunDamageMultiplier(Scale::CreatureFacts const& f, uint32_t map, Config const& c = Defaults());

    // The CoD scale's health with the danger on it, for a creature on map `map`.
    float CreatureMaxHealth(Scale::CreatureFacts const& f, uint32_t map, Config const& c = Defaults());
}}

#endif
