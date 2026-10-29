/*
 * Azeroth Warfare: tests for the CoD scale (Scale.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Scale.h"

using namespace Fusion;
using namespace Fusion::Scale;

TEST(player_health_is_100_at_1_and_300_at_60_plus_gear_stamina)
{
    CHECK_NEAR(PlayerMaxHealth(1, 0.f), 100.f, 1e-4);
    CHECK_NEAR(PlayerMaxHealth(60, 0.f), 300.f, 1e-4);
    CHECK_NEAR(PlayerMaxHealth(30, 0.f), 100.f + 200.f * 29.f / 59.f, 1e-3);
    // Stamina from gear counts one for one: a tank's 300 to 700 puts them at 600 to 1,000.
    CHECK_NEAR(PlayerMaxHealth(60, 300.f), 600.f, 1e-4);
    CHECK_NEAR(PlayerMaxHealth(60, 700.f), 1000.f, 1e-4);
    CHECK_NEAR(PlayerMaxHealth(60, -20.f), 300.f, 1e-4);     // a debuff never goes under
}

TEST(a_tank_takes_six_to_ten_sniper_shots)
{
    // A Barrett's 98 (MW2's body shot) from a fresh level 60 NPC: 98 x 1.5 = 147 a hit.
    CreatureFacts npc;
    npc.level = 60;
    float const hit = 98.f * CreatureGunMultiplier(npc);
    float const tank = PlayerMaxHealth(60, 600.f);
    float const cut = PlayerArmorCut(6000.f, 60);
    float const shots = tank / (hit * (1.f - cut));
    CHECK(shots >= 6.f && shots <= 10.f);
    // A fresh 60 takes two or three.
    CHECK(PlayerMaxHealth(60, 0.f) / hit < 3.f);
}

TEST(armor_cuts_at_most_thirty_percent)
{
    CHECK_NEAR(PlayerArmorCut(0.f, 60), 0.f, 1e-6);
    CHECK_NEAR(PlayerArmorCut(300.f, 10), 300.f / (300.f + 400.f + 850.f), 1e-5);
    CHECK_NEAR(PlayerArmorCut(100000.f, 60), kMaxArmorCut, 1e-6);
}

TEST(player_damage_is_1x_at_1_3x_fresh_at_60_and_up_to_10x_geared)
{
    CHECK_NEAR(LevelMultiplier(1), 1.f, 1e-6);
    CHECK_NEAR(LevelMultiplier(60), 3.f, 1e-6);
    CHECK_NEAR(ExpectedPower(60), 760.f, 1e-4);
    // Below the expected power never cuts damage; far above it stops at 3.33.
    CHECK_NEAR(PlayerDamageMultiplier(60, 300.f, 0.f, 0.f), 3.f, 1e-5);
    CHECK_NEAR(PlayerDamageMultiplier(60, 760.f, 0.f, 0.f), 3.f, 1e-5);
    CHECK_NEAR(PlayerDamageMultiplier(60, 0.f, 1520.f, 0.f), 6.f, 1e-4);
    CHECK_NEAR(PlayerDamageMultiplier(60, 5000.f, 0.f, 0.f), 3.f * kMaxGearMultiplier, 1e-4);
    // A caster's spell power counts three times.
    CHECK_NEAR(AttackPower(100.f, 50.f, 600.f), 1800.f, 1e-4);
    CHECK_NEAR(PlayerDamageMultiplier(1, 0.f, 0.f, 0.f), 1.f, 1e-6);
}

TEST(creatures_sit_on_the_cod_scale_by_level_rank_and_place)
{
    CreatureFacts f;
    f.level = 1;
    CHECK_NEAR(CreatureMaxHealth(f), 100.f, 1e-4);
    f.level = 60;
    CHECK_NEAR(CreatureMaxHealth(f), 250.f, 1e-4);
    f.rank = CreatureRank::Rare;
    CHECK_NEAR(CreatureMaxHealth(f), 375.f, 1e-3);
    f.rank = CreatureRank::Elite;
    CHECK_NEAR(CreatureMaxHealth(f), 750.f, 1e-3);
    f.rank = CreatureRank::RareElite;
    CHECK_NEAR(CreatureMaxHealth(f), 1000.f, 1e-3);
    f.rank = CreatureRank::WorldBoss;
    CHECK_NEAR(CreatureMaxHealth(f), 7500.f, 1e-2);
    // Dungeons and raids are harder.
    f.rank = CreatureRank::Elite;
    f.place = Place::Dungeon;
    CHECK_NEAR(CreatureMaxHealth(f), 750.f * 1.25f, 1e-2);
    f.boss = true;
    CHECK_NEAR(CreatureMaxHealth(f), 2500.f * 1.25f, 1e-2);   // x10 instead of the elite's x3
    f.place = Place::Raid;
    CHECK_NEAR(CreatureMaxHealth(f), 7500.f * 1.5f, 1e-2);    // x30
}

TEST(npc_damage_follows_level_and_rank_not_the_gap)
{
    CreatureFacts low;
    low.level = 1;
    CHECK_NEAR(CreatureGunMultiplier(low), 1.f, 1e-6);
    CreatureFacts top;
    top.level = 60;
    CHECK_NEAR(CreatureGunMultiplier(top), 1.5f, 1e-6);
    top.rank = CreatureRank::Elite;
    CHECK_NEAR(CreatureGunMultiplier(top), 2.25f, 1e-6);
    top.boss = true;
    CHECK_NEAR(CreatureGunMultiplier(top), 4.5f, 1e-6);
    // A level 1's M4 round still takes a tenth of a fresh level 60.
    CHECK(30.f * CreatureGunMultiplier(low) / PlayerMaxHealth(60, 0.f) >= 0.099f);
}

TEST(creature_melee_and_spells_convert_from_wow)
{
    CreatureFacts f;
    f.level = 60;
    CHECK_NEAR(CreatureMeleeBase(f), 35.f, 1e-4);
    CHECK_NEAR(CreatureSpellBase(f), 52.5f, 1e-4);
    // An average hit is the base; a weak one no less than 30% of it, a crushing one at most 3x.
    CHECK_NEAR(WowToCod(35.f, 500.f, 500.f), 35.f, 1e-4);
    CHECK_NEAR(WowToCod(35.f, 10.f, 500.f), 35.f * 0.3f, 1e-4);
    CHECK_NEAR(WowToCod(35.f, 5000.f, 500.f), 105.f, 1e-4);
    CHECK_NEAR(WowToCod(35.f, 0.f, 500.f), 0.f, 1e-6);
    CHECK_NEAR(ExpectedWowSpell(60, false), 620.f, 1e-4);
    CHECK_NEAR(ExpectedWowSpell(60, true), 124.f, 1e-4);
    f.rank = CreatureRank::Elite;
    CHECK_NEAR(CreatureMeleeBase(f), 52.5f, 1e-4);
}

TEST(wow_amounts_scale_with_the_pool)
{
    CHECK_NEAR(PoolRatio(300.f, 3000.f), 0.1f, 1e-6);
    CHECK_NEAR(PoolRatio(300.f, 0.f), 1.f, 1e-6);
}
