/*
 * Azeroth Warfare: tests for the danger scaling (Danger.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Danger.h"

using namespace Fusion;
using namespace Fusion::Danger;

namespace
{
    Scale::CreatureFacts Facts(uint32_t level, CreatureRank rank, bool boss = false, Scale::Place place = Scale::Place::World)
    {
        Scale::CreatureFacts f;
        f.level = level;
        f.rank = rank;
        f.boss = boss;
        f.place = place;
        return f;
    }
}

TEST(danger_kinds_follow_rank_and_place)
{
    CHECK(KindOf(Facts(30, CreatureRank::Normal)) == Kind::Normal);
    CHECK(KindOf(Facts(30, CreatureRank::Rare)) == Kind::Rare);
    CHECK(KindOf(Facts(30, CreatureRank::Elite)) == Kind::Elite);
    CHECK(KindOf(Facts(30, CreatureRank::RareElite)) == Kind::RareElite);
    CHECK(KindOf(Facts(63, CreatureRank::WorldBoss, true)) == Kind::WorldBoss);
    CHECK(KindOf(Facts(20, CreatureRank::Elite, true, Scale::Place::Dungeon)) == Kind::DungeonBoss);
    CHECK(KindOf(Facts(63, CreatureRank::WorldBoss, true, Scale::Place::Raid)) == Kind::RaidBoss);
    CHECK(KindOf(Facts(60, CreatureRank::Elite, true, Scale::Place::Raid)) == Kind::RaidBoss);
    CHECK(KindOf(Facts(20, CreatureRank::DungeonBoss)) == Kind::DungeonBoss);
}

TEST(danger_raises_health_by_kind_at_full_ease)
{
    CHECK_NEAR(HealthMultiplier(Facts(30, CreatureRank::Normal), 0), 2.f, 1e-4);
    CHECK_NEAR(HealthMultiplier(Facts(30, CreatureRank::Elite), 0), 3.5f, 1e-4);
    CHECK_NEAR(HealthMultiplier(Facts(30, CreatureRank::RareElite), 0), 4.5f, 1e-4);
    // Trash in a dungeon has its place's share on top; a boss's kind already carries it.
    CHECK_NEAR(HealthMultiplier(Facts(30, CreatureRank::Elite, false, Scale::Place::Dungeon), 36), 5.25f, 1e-4);
    CHECK_NEAR(HealthMultiplier(Facts(30, CreatureRank::Elite, true, Scale::Place::Dungeon), 36), 32.f, 1e-4);
    CHECK_NEAR(HealthMultiplier(Facts(60, CreatureRank::Elite, false, Scale::Place::Raid), 409), 10.5f, 1e-4);
    CHECK_NEAR(HealthMultiplier(Facts(63, CreatureRank::WorldBoss, true, Scale::Place::Raid), 409), 80.f, 1e-4);
}

TEST(danger_eases_in_over_the_first_levels)
{
    // Half the extra at level 1, all of it from 15.
    CHECK_NEAR(HealthMultiplier(Facts(1, CreatureRank::Normal), 0), 1.5f, 1e-4);
    CHECK_NEAR(HealthMultiplier(Facts(15, CreatureRank::Normal), 0), 2.f, 1e-4);
    float const mid = HealthMultiplier(Facts(8, CreatureRank::Normal), 0);
    CHECK(mid > 1.5f && mid < 2.f);
    CHECK_NEAR(DamageMultiplier(Facts(1, CreatureRank::Normal), 0), 1.2f, 1e-4);
}

TEST(danger_damage_hits_harder_and_guns_less_so)
{
    for (CreatureRank r : {CreatureRank::Normal, CreatureRank::Rare, CreatureRank::Elite, CreatureRank::RareElite})
    {
        Scale::CreatureFacts const f = Facts(40, r);
        float const melee = DamageMultiplier(f, 0);
        float const gun = GunDamageMultiplier(f, 0);
        CHECK(melee >= 1.3f && melee <= 1.6f);
        CHECK(gun >= 1.f && gun <= melee);
    }
    // Raid trash hits a little harder than the world's.
    CHECK(DamageMultiplier(Facts(60, CreatureRank::Elite, false, Scale::Place::Raid), 409)
        > DamageMultiplier(Facts(60, CreatureRank::Elite), 0));
}

TEST(danger_twenty_man_raids_are_half_a_forty)
{
    Scale::CreatureFacts const boss = Facts(63, CreatureRank::WorldBoss, true, Scale::Place::Raid);
    // The map's share is of the extra over 1, so the knobs still turn it all off.
    CHECK_NEAR(HealthMultiplier(boss, 309) - 1.f, 0.5f * (HealthMultiplier(boss, 409) - 1.f), 1e-3);
    CHECK_NEAR(HealthMultiplier(boss, 509) - 1.f, 0.5f * (HealthMultiplier(boss, 409) - 1.f), 1e-3);
}

TEST(danger_knobs_scale_the_extra_and_zero_turns_it_off)
{
    Config c = Defaults();
    c.healthKnob = 0.f;
    c.damageKnob = 0.f;
    Scale::CreatureFacts const f = Facts(40, CreatureRank::Elite);
    CHECK_NEAR(HealthMultiplier(f, 0, c), 1.f, 1e-5);
    CHECK_NEAR(DamageMultiplier(f, 0, c), 1.f, 1e-5);
    CHECK_NEAR(GunDamageMultiplier(f, 0, c), 1.f, 1e-5);
    c.healthKnob = 2.f;
    CHECK_NEAR(HealthMultiplier(f, 0, c), 6.f, 1e-4);
}

TEST(danger_health_sits_on_the_cod_scale)
{
    Scale::CreatureFacts const f = Facts(60, CreatureRank::Normal);
    CHECK_NEAR(CreatureMaxHealth(f, 0), 250.f * 2.f, 1e-2);
}

TEST(danger_time_to_kill_lands_in_the_planned_windows)
{
    // A level 60 with an M4: 30 a round x M 3 (level only), about half of 400 a second sustained.
    float const sustained = 400.f * 3.f * 0.5f;
    // A normal creature: more than half a second, less than two.
    float const normal = CreatureMaxHealth(Facts(60, CreatureRank::Normal), 0) / sustained;
    CHECK(normal > 0.5f && normal < 2.f);
    // An elite: a few seconds alone.
    float const elite = CreatureMaxHealth(Facts(60, CreatureRank::Elite), 0) / sustained;
    CHECK(elite > 2.f && elite < 8.f);
    // A dungeon boss against five: half a minute to a minute.
    float const boss = CreatureMaxHealth(Facts(60, CreatureRank::Elite, true, Scale::Place::Dungeon), 329) / (5.f * sustained);
    CHECK(boss > 20.f && boss < 60.f);
    // A raid boss against twenty: about a minute.
    float const raid = CreatureMaxHealth(Facts(63, CreatureRank::WorldBoss, true, Scale::Place::Raid), 409) / (20.f * sustained);
    CHECK(raid > 40.f && raid < 120.f);
}
