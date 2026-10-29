/*
 * Azeroth Warfare: the fusion rules' tests. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"

#include "fusion/Damage.h"
#include "fusion/Scale.h"

using namespace Fusion;

namespace
{
    // An M4A1-like profile; the real numbers come from the MW2 install's export.
    WeaponProfile M4()
    {
        WeaponProfile w;
        w.id = 1;
        w.name = "m4_mp";
        w.damage = 30.f;
        w.minDamage = 20.f;
        w.maxDamageRange = 42.f;
        w.minDamageRange = 83.f;
        w.fireTime = 0.075f;
        w.locationMultiplier[size_t(HitLocation::Head)] = 1.4f;
        return w;
    }

    AttackerStats AtPar(uint32_t level, ParTable const& par)
    {
        AttackerStats a;
        a.level = level;
        a.attackPower = par.AttackPower(level);
        a.rangedItemLevel = par.ItemLevel(level);
        return a;
    }
}

TEST(falloff_follows_mw2)
{
    WeaponProfile w = M4();
    CHECK_NEAR(CodDamageAtRange(w, 10.f), 30.f, 1e-4);
    CHECK_NEAR(CodDamageAtRange(w, 42.f), 30.f, 1e-4);
    // MW2's ranges, stretched by the class's share for WoW's longer fights (Gunfights.h).
    float const k = FalloffRangeScale(w.weaponClass);
    CHECK_NEAR(CodDamageAtRange(w, 62.5f * k), 25.f, 1e-3);
    CHECK_NEAR(CodDamageAtRange(w, 200.f), 20.f, 1e-4);
}

TEST(unlock_levels_scale_rank_70_to_level_60)
{
    CHECK(UnlockLevel(1) == 1);
    CHECK(UnlockLevel(35) == 30);
    CHECK(UnlockLevel(70) == 60);
    CHECK(UnlockLevel(4) == 4);
}

TEST(par_character_has_100_cod_health)
{
    ParTable par;
    for (uint32_t level : {1u, 10u, 30u, 60u})
    {
        PlayerDefense d{level, par.MaxHealth(level), par.Armor(level)};
        CHECK_NEAR(CodHealth(d, par), 100.f, 0.01);
    }
}

TEST(cod_health_follows_stamina_and_armor_and_clamps)
{
    ParTable par;
    PlayerDefense cloth{60, par.MaxHealth(60) * 0.95f, 300.f};
    PlayerDefense tank{60, par.MaxHealth(60) * 1.15f, 5000.f};
    float c = CodHealth(cloth, par);
    float t = CodHealth(tank, par);
    CHECK(c < 100.f && c >= 75.f);
    CHECK(t > 120.f && t <= 150.f);
    PlayerDefense god{60, 100000.f, 20000.f};
    CHECK_NEAR(CodHealth(god, par), 150.f, 1e-4);
    PlayerDefense paper{60, 10.f, 0.f};
    CHECK_NEAR(CodHealth(paper, par), 75.f, 1e-4);
}

TEST(normal_creature_dies_to_three_or_four_m4_body_shots)
{
    // On the CoD scale a creature's pool and a player's multiplier grow together: a normal
    // creature of the player's level takes three or four M4 body shots at every level.
    WeaponProfile w = M4();
    HitInput in;
    in.weapon = &w;
    in.distance = 20.f;
    TargetTraits wolf;
    wolf.creatureType = CreatureType::Beast;
    for (uint32_t level : {1u, 20u, 40u, 60u})
    {
        float const per = CodHitDamage(in, wolf, CombatContext::PvE, Scale::LevelMultiplier(level));
        Scale::CreatureFacts f;
        f.level = level;
        float const hp = Scale::CreatureMaxHealth(f);
        CHECK(per * 2.f < hp);
        CHECK(per * 4.f >= hp);
        // An elite takes three times as many.
        f.rank = CreatureRank::Elite;
        CHECK_NEAR(Scale::CreatureMaxHealth(f), hp * 3.f, 1e-2);
    }
}

TEST(pvp_time_to_kill_between_equals_is_0_6_to_1_0_seconds)
{
    WeaponProfile w = M4();
    HitInput in;
    in.weapon = &w;
    in.distance = 20.f;
    TargetTraits player;
    player.isPlayer = true;
    // Fresh characters of a level: the pool and the multiplier keep MW2's 100 between them.
    for (uint32_t level : {1u, 30u, 60u})
    {
        float const per = CodHitDamage(in, player, CombatContext::PvP, Scale::LevelMultiplier(level));
        int hits = int(std::ceil(Scale::PlayerMaxHealth(level, 0.f) / per - 1e-3f));
        float ttk = float(hits - 1) * w.fireTime;
        CHECK(ttk >= 0.6f && ttk <= 1.0f);
    }
}

TEST(old_pvp_time_to_kill_between_equals_is_0_6_to_1_0_seconds)
{
    ParTable par;
    WeaponProfile w = M4();
    HitInput in;
    in.weapon = &w;
    in.distance = 20.f;
    in.attacker = AtPar(40, par);
    TargetTraits player;
    player.isPlayer = true;
    float per = CodDamage(in, 40, player, CombatContext::PvP, par);
    int hits = int(std::ceil(100.f / per));
    float ttk = float(hits - 1) * w.fireTime;
    CHECK(ttk >= 0.6f && ttk <= 1.0f);
}

TEST(cod_hit_damage_crits_and_ammo)
{
    WeaponProfile w = M4();
    HitInput in;
    in.weapon = &w;
    TargetTraits t;
    float const body = CodHitDamage(in, t, CombatContext::PvE, 2.f);
    CHECK_NEAR(body, 60.f, 1e-4);
    in.critRoll = true;
    CHECK_NEAR(CodHitDamage(in, t, CombatContext::PvE, 2.f), 84.f, 1e-3);
    in.critRoll = false;
    in.ammo = AmmoType::Explosive;
    CHECK_NEAR(CodHitDamage(in, t, CombatContext::PvE, 2.f), 51.f, 1e-3);
    in.weapon = nullptr;
    CHECK_NEAR(CodHitDamage(in, t, CombatContext::PvE, 2.f), 0.f, 1e-6);
}

TEST(headshots_and_crits_use_the_head_multiplier)
{
    ParTable par;
    WeaponProfile w = M4();
    HitInput in;
    in.weapon = &w;
    in.attacker = AtPar(30, par);
    TargetTraits t;
    float body = CodDamage(in, 30, t, CombatContext::PvE, par);
    in.location = HitLocation::Head;
    float head = CodDamage(in, 30, t, CombatContext::PvE, par);
    CHECK_NEAR(head / body, 1.4f, 1e-4);
    in.location = HitLocation::Leg;
    in.critRoll = true;
    CHECK_NEAR(CodDamage(in, 30, t, CombatContext::PvE, par), head, 1e-4);
}

TEST(level_gap_moves_damage_both_ways_and_clamps)
{
    CHECK_NEAR(LevelFactor(30, 30), 1.f, 1e-6);
    CHECK_NEAR(LevelFactor(35, 30), 1.4f, 1e-5);
    CHECK_NEAR(LevelFactor(25, 30), 0.6f, 1e-5);
    CHECK_NEAR(LevelFactor(60, 1), 1.6f, 1e-6);
    CHECK_NEAR(LevelFactor(1, 60), 0.4f, 1e-6);
}

TEST(gun_power_is_par_at_par_and_capped_by_the_gear_ceiling)
{
    ParTable par;
    CHECK_NEAR(GunPowerMultiplier(AtPar(50, par), par), 1.f, 1e-5);
    AttackerStats geared = AtPar(60, par);
    geared.attackPower *= 3.f;
    geared.rangedItemLevel *= 2.f;
    CHECK_NEAR(GunPowerMultiplier(geared, par), 1.25f, 1e-5);
    AttackerStats naked;
    naked.level = 60;
    CHECK(GunPowerMultiplier(naked, par) >= 0.75f);
    CHECK(GunPowerMultiplier(naked, par) < 1.f);
}

TEST(empty_ranged_slot_falls_back_to_main_hand)
{
    ParTable par;
    AttackerStats a = AtPar(40, par);
    a.rangedItemLevel = 0.f;
    a.mainHandItemLevel = par.ItemLevel(40) * 1.2f;
    AttackerStats b = AtPar(40, par);
    b.rangedItemLevel = par.ItemLevel(40) * 1.2f;
    CHECK_NEAR(GunPowerMultiplier(a, par), GunPowerMultiplier(b, par), 1e-5);
}

TEST(special_ammo_matchups)
{
    TargetTraits cloth;
    cloth.isPlayer = true;
    cloth.armor = ArmorClass::Cloth;
    TargetTraits plate = cloth;
    plate.armor = ArmorClass::Plate;
    CHECK_NEAR(AmmoAgainst(AmmoType::HollowPoint, cloth).damageMultiplier, 1.2f, 1e-6);
    CHECK_NEAR(AmmoAgainst(AmmoType::HollowPoint, plate).damageMultiplier, 0.8f, 1e-6);
    CHECK(AmmoAgainst(AmmoType::ArmorPiercing, plate).armorIgnore > 0.f);
    CHECK(AmmoAgainst(AmmoType::ArmorPiercing, cloth).armorIgnore == 0.f);

    TargetTraits fire;
    fire.creatureType = CreatureType::Elemental;
    fire.fireElemental = true;
    CHECK(AmmoAgainst(AmmoType::Incendiary, fire).burnFraction == 0.f);
    TargetTraits ghoul;
    ghoul.creatureType = CreatureType::Undead;
    CHECK_NEAR(AmmoAgainst(AmmoType::Blessed, ghoul).damageMultiplier, 1.3f, 1e-6);
    CHECK(AmmoAgainst(AmmoType::Incendiary, ghoul).burnFraction > 0.2f);

    TargetTraits boss;
    boss.slowImmune = true;
    CHECK(AmmoAgainst(AmmoType::IceThreaded, boss).slowPercent == 0.f);
    TargetTraits shielded;
    shielded.hasAbsorbShield = true;
    CHECK(AmmoAgainst(AmmoType::Arcane, shielded).shieldBreakMultiplier > 1.f);
    CHECK(AmmoAgainst(AmmoType::Explosive, cloth).splashRadius > 0.f);
}

TEST(ammo_unlocks_by_level)
{
    CHECK(AmmoUnlockLevel(AmmoType::Incendiary) == 10);
    CHECK(AmmoUnlockLevel(AmmoType::Explosive) == 20);
    CHECK(AmmoUnlockLevel(AmmoType::Arcane) == 30);
}

TEST(player_damage_converts_through_cod_health)
{
    // 50 CoD damage takes half of a 100-CoD-health player's health, a third of a 150's.
    CHECK_NEAR(ToPlayerHealth(50.f, 100.f, 3000.f), 1500.f, 1e-3);
    CHECK_NEAR(ToPlayerHealth(50.f, 150.f, 3000.f), 1000.f, 1e-3);
}

TEST(spell_must_beat_the_gunfire_it_costs)
{
    // 300 CoD DPS, a 2.5 s cast: worth at least (2.5 + 0.4) x 300 x 1.3.
    CHECK_NEAR(MinSpellValue(300.f, 2.5f), 1131.f, 0.01);
    CHECK_NEAR(MinSpellValue(300.f, 0.f, 0.4f, 1.f), 120.f, 1e-3);
}
