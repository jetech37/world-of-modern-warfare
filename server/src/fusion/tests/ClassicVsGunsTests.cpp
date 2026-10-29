/*
 * World of Modern Warfare: guns against classic combat. The balance model's numbers
 * (fusion/Balance.h), the limited-ammunition rules (Ammo.h), and the deepened classic combat:
 * melee combos, flanking, the raised shield, full charges and bullet wards.
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Ammo.h"
#include "fusion/Balance.h"
#include "fusion/ClassEffects.h"
#include "fusion/Kits.h"
#include "fusion/Melee.h"
#include "fusion/Spellcasting.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace Fusion;

namespace
{
    WeaponTable const& Seed()
    {
        static WeaponTable const t = [] {
            std::ifstream in(FUSION_SEED_WEAPONS);
            std::stringstream text;
            text << in.rdbuf();
            WeaponTable w;
            std::string err;
            w.LoadJson(text.str(), err);
            return w;
        }();
        return t;
    }

    Balance::Rotation const* RotationOf(char const* cls, char const* archetype)
    {
        for (Balance::Rotation const& r : Balance::Rotations())
            if (r.cls == cls && r.archetype == archetype)
                return &r;
        return nullptr;
    }
}

// ── Limited ammunition ──────────────────────────────────────────────────────────────────────

TEST(limited_ammo_respawn_tops_up_half_the_reserve)
{
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 120;
    GunAmmo g;
    g.Reset(w);
    g.Set(3, 10);
    g.RespawnResupply();
    CHECK(g.Mag() == 30 && g.Reserve() == 60);
    // Never lowered.
    g.Set(30, 100);
    g.RespawnResupply();
    CHECK(g.Reserve() == 100);
    AmmoRules legacy;
    legacy.limited = false;
    g.Set(0, 0);
    g.RespawnResupply(legacy);
    CHECK(g.Mag() == 30 && g.Reserve() == 120);
}

TEST(limited_ammo_restock_is_free_at_a_station)
{
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 120;
    GunAmmo g;
    g.Reset(w);
    g.Set(10, 50);
    CHECK(g.MissingRounds() == 90);
    // The magazine first, then the reserve, never past full.
    CHECK(g.Restock(25) == 25);
    CHECK(g.Mag() == 30 && g.Reserve() == 55);
    CHECK(g.Restock(1000) == 65);
    CHECK(g.MissingRounds() == 0);
    // A full resupply (a kit station's, free) leaves nothing missing.
    g.Set(0, 0);
    g.FullResupply();
    CHECK(g.Mag() == 30 && g.Reserve() == 120 && g.MissingRounds() == 0);
}

TEST(limited_ammo_scavenges_a_share_of_a_magazine)
{
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 120;
    GunAmmo g;
    g.Reset(w);
    g.Set(30, 0);
    CHECK(g.ScavengeMags(CurrentAmmoRules().gunnerScavengeMags) == 15);
    CHECK(g.ScavengeMags(0.01f) == 1);   // at least a round
    CHECK(g.ScavengeMags(0.f) == 0);
    g.Set(30, 118);
    CHECK(g.ScavengeMags(1.f) == 2);     // up to the max
    CHECK(g.ScavengeMags(1.f) == 0);
}

TEST(limited_ammo_field_floor_is_one_magazine_and_configurable)
{
    WeaponProfile w;
    w.magSize = 20;
    w.maxReserve = 80;
    GunAmmo g;
    g.Reset(w);
    CHECK(g.FieldTarget() == 20);
    AmmoRules r;
    r.fieldFloorMags = 2.f;
    CHECK(g.FieldTarget(r) == 40);
    r.fieldFloorMags = 10.f;
    CHECK(g.FieldTarget(r) == 80);      // never past the max
    // The setting is the process's.
    AmmoRules const before = CurrentAmmoRules();
    AmmoRules none = before;
    none.fieldFloorMags = 0.f;
    SetAmmoRules(none);
    CHECK(g.FieldTarget() == 0);
    SetAmmoRules(before);
    CHECK(g.FieldTarget() == 20);
}

TEST(limited_ammo_field_trickle_is_a_breather_and_a_boss_restocks)
{
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 120;
    GunAmmo g;
    g.Reset(w);
    g.Set(0, 0);
    // Half a round a second after ten quiet seconds: a magazine back in a minute.
    for (int i = 0; i < 20; ++i)
        g.FieldRearm(20.f, 1.f);
    CHECK(g.Reserve() == 10);
    for (int i = 0; i < 200; ++i)
        g.FieldRearm(20.f, 1.f);
    CHECK(g.Reserve() == 30);
    // A boss's armory: everything topped up to the share, never lowered.
    g.TopUp(CurrentAmmoRules().bossRestockShare);
    CHECK(g.Mag() == 30 && g.Reserve() == 120);
    g.Set(30, 100);
    g.TopUp(0.5f);
    CHECK(g.Mag() == 30 && g.Reserve() == 100);
    g.Set(0, 0);
    g.TopUp(0.5f);
    CHECK(g.Mag() == 15 && g.Reserve() == 60);
    CHECK_NEAR(CurrentAmmoRules().eliteScavengeScale, 3.0, 1e-6);
}

TEST(scavenger_takes_twice_a_bodys_rounds)
{
    AmmoRules const& r = CurrentAmmoRules();
    CHECK_NEAR(BodyScavengeMags(r.gunnerScavengeMags, false), 0.5, 1e-6);
    CHECK_NEAR(BodyScavengeMags(r.gunnerScavengeMags, true), 1.0, 1e-6);
    CHECK_NEAR(BodyScavengeMags(r.playerScavengeMags, true), 2.0, 1e-6);
    CHECK_NEAR(BodyScavengeMags(-1.f, true), 0.0, 1e-6);
    AmmoRules off = r;
    off.scavengerBodyScale = 0.5f;      // never less than without the perk
    CHECK_NEAR(BodyScavengeMags(1.f, true, off), 1.0, 1e-6);
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 120;
    GunAmmo g;
    g.Reset(w);
    g.Set(30, 0);
    CHECK(g.ScavengeMags(BodyScavengeMags(r.gunnerScavengeMags, true)) == 30);
}

// ── Melee: combos, flanks, the shield ───────────────────────────────────────────────────────

TEST(melee_combo_builds_to_a_finisher_in_rhythm)
{
    Melee::Combo c;
    uint16_t const swing = 600;
    CHECK_NEAR(Melee::ComboHit(c, 1000, swing), 1.0, 1e-6);
    CHECK_NEAR(Melee::ComboHit(c, 1600, swing), Melee::kComboSecond, 1e-6);
    CHECK_NEAR(Melee::ComboHit(c, 2300, swing), Melee::kComboFinisher, 1e-6);
    // After the finisher it starts over.
    CHECK_NEAR(Melee::ComboHit(c, 2900, swing), 1.0, 1e-6);
    // Too slow (past the swing and the window): broken.
    CHECK_NEAR(Melee::ComboHit(c, 2900 + swing + Melee::kComboWindowMs + 1, swing), 1.0, 1e-6);
    // A whiff breaks it too.
    Melee::ComboHit(c, 5000, swing);
    Melee::ComboBreak(c);
    CHECK_NEAR(Melee::ComboHit(c, 5600, swing), 1.0, 1e-6);
    // Over a chain the blows average 1.2.
    CHECK_NEAR((1.f + Melee::kComboSecond + Melee::kComboFinisher) / 3.f, 1.2, 1e-5);
}

TEST(melee_flank_and_behind_hit_harder)
{
    CHECK_NEAR(Melee::PositionalMultiplier(0.f), 1.0, 1e-6);
    CHECK_NEAR(Melee::PositionalMultiplier(1.5f), Melee::kFlankMultiplier, 1e-6);
    CHECK_NEAR(Melee::PositionalMultiplier(3.1f), Melee::kBehindMultiplier, 1e-6);
    CHECK_NEAR(Melee::PositionalMultiplier(-3.1f), Melee::kBehindMultiplier, 1e-6);
}

TEST(raised_shield_blocks_and_parries_in_time)
{
    Melee::Guard g;
    CHECK(Melee::GuardBlocks(g, true, 0) == Melee::Block::None);
    Melee::GuardUpdate(g, true, 1000);
    CHECK(Melee::GuardBlocks(g, true, 1200) == Melee::Block::Perfect);
    CHECK(Melee::GuardBlocks(g, false, 1200) == Melee::Block::None);    // from behind
    CHECK(Melee::GuardBlocks(g, true, 1000 + Melee::kPerfectBlockMs + 1) == Melee::Block::Partial);
    CHECK_NEAR(Melee::BlockMultiplier(Melee::Block::Perfect, true), 0.0, 1e-6);
    CHECK_NEAR(Melee::BlockMultiplier(Melee::Block::Partial, true), 0.5, 1e-6);
    CHECK_NEAR(Melee::BlockMultiplier(Melee::Block::Partial, false), 0.25, 1e-6);
    CHECK_NEAR(Melee::BlockMultiplier(Melee::Block::None, false), 1.0, 1e-6);
    // Flicking it: a raise soon after the last parries nothing.
    Melee::GuardUpdate(g, false, 1500);
    Melee::GuardUpdate(g, true, 1600);
    CHECK(Melee::GuardBlocks(g, true, 1650) == Melee::Block::Partial);
    Melee::GuardUpdate(g, false, 2000);
    Melee::GuardUpdate(g, true, 1600 + Melee::kPerfectBlockRearmMs);
    CHECK(Melee::GuardBlocks(g, true, 1700 + Melee::kPerfectBlockRearmMs) == Melee::Block::Perfect);
    Melee::GuardUpdate(g, false, 5000);
    CHECK(Melee::GuardBlocks(g, true, 5001) == Melee::Block::None);
}

// ── Spells: full charges, wards ─────────────────────────────────────────────────────────────

TEST(full_charge_is_held_much_longer_and_hits_much_harder)
{
    namespace SC = Spellcasting;
    CHECK(SC::FullChargeMs(450) == 1238);     // 2.75 times the charge
    // Served only once the long hold has passed since the hand's last cast.
    CHECK(SC::FullChargeServed(0, false, 0, 450));
    CHECK(!SC::FullChargeServed(1000, true, 1000 + 900, 450));
    CHECK(SC::FullChargeServed(1000, true, 1000 + 1238 - SC::kChargeSlackMs, 450));
    // Much harder than a tap; a full charge alone outhits a dual tap.
    CHECK(SC::kFullChargePower >= 2.f);
    CHECK(SC::ReleasePower(false, true) > SC::ReleasePower(true, false));
    CHECK(!SC::FullChargeServed(1000, true, 99999, 0));     // a spray: nothing to hold
    CHECK_NEAR(SC::ReleasePower(false, false), 1.0, 1e-6);
    CHECK_NEAR(SC::ReleasePower(false, true), SC::kFullChargePower, 1e-6);
    CHECK_NEAR(SC::ReleasePower(true, false), SC::kDualPower, 1e-6);
    CHECK_NEAR(SC::ReleasePower(true, true), SC::kMaxReleasePower, 1e-6);
}

TEST(wards_soak_bullets_half_again_better)
{
    float ward = 60.f;
    // A 30-point round takes 20 of the ward.
    CHECK_NEAR(Classes::WardAbsorb(ward, 30.f, true), 30.0, 1e-4);
    CHECK_NEAR(ward, 40.0, 1e-4);
    // A spell one for one.
    CHECK_NEAR(Classes::WardAbsorb(ward, 30.f, false), 30.0, 1e-4);
    CHECK_NEAR(ward, 10.0, 1e-4);
    // The last 10 stop 15 of a round, the rest gets through.
    CHECK_NEAR(Classes::WardAbsorb(ward, 40.f, true), 15.0, 1e-4);
    CHECK_NEAR(ward, 0.0, 1e-4);
    CHECK_NEAR(Classes::WardAbsorb(ward, 40.f, true), 0.0, 1e-6);
}

TEST(ranged_spells_carry_the_power_melee_reach_does_not)
{
    CHECK_NEAR(Classes::RangedSpellScale(30.f), Classes::kRangedSpellPower, 1e-6);
    CHECK_NEAR(Classes::RangedSpellScale(Classes::kMeleeSpellReach), 1.0, 1e-6);  // a strike's reach
    CHECK_NEAR(Classes::RangedSpellScale(0.f), 1.0, 1e-6);                         // round the caster
    CHECK_NEAR(Classes::RangedSpellScale(40.f, 1.f), 1.0, 1e-6);                   // the knob off
}

// ── The balance model ───────────────────────────────────────────────────────────────────────

TEST(balance_model_gun_numbers)
{
    WeaponProfile const* famas = Seed().Find(std::string("famas_mp"));
    CHECK(famas != nullptr);
    if (!famas)
        return;
    Balance::GunCase g;
    g.weapon = *famas;
    g.range = 25.f;
    g.stoppingPower = true;
    // Level 60 against a normal creature (250, x2 by the danger table: 500): 40 x 1.04 (head
    // share) x 3 x 1.4 = 174.7 a hit, 3 hits at 70%.
    CHECK_NEAR(Balance::TargetPool(60, false), 500.0, 0.5);
    Balance::GunResult const pve = Balance::Gun(g, 60, false);
    CHECK_NEAR(pve.hitPct, 34.9, 0.2);
    CHECK_NEAR(pve.roundsPerKill, 3.f / 0.7f, 1e-3);
    CHECK(pve.load == 150);
    CHECK_NEAR(pve.killsPerLoad, 35.0, 0.1);
    // Against an equal player (300, PvP 0.4, armor 15%): 19.8% a hit, 6 hits at 45%.
    Balance::GunResult const pvp = Balance::Gun(g, 60, true);
    CHECK_NEAR(pvp.hitPct, 19.8, 0.2);
    CHECK_NEAR(pvp.roundsPerKill, 6.f / 0.45f, 1e-2);
    CHECK(pvp.ttk < 1.f);
    // The level barely matters: the pools and the guns scale together.
    Balance::GunResult const pvp10 = Balance::Gun(g, 10, true);
    CHECK_NEAR(pvp10.hitPct, pvp.hitPct, 0.5);
    // Extended magazines: more kills a magazine, the same load per kill.
    g.magScale = 1.5f;
    CHECK(Balance::Gun(g, 60, false).killsPerMag > pve.killsPerMag);
}

TEST(balance_model_classic_numbers)
{
    CHECK(Balance::Rotations().size() == 27);
    Balance::Rotation const* wizard = RotationOf("Mage", "Wizard");
    Balance::Rotation const* berserker = RotationOf("Warrior", "Berserker");
    CHECK(wizard && berserker);
    if (!wizard || !berserker)
        return;
    // Fireball at 18 %EH and its burn every 2 s, more than a quarter of a pool in 5 s.
    Balance::ClassResult const w = Balance::Classic(*wizard, 60, true, false);
    CHECK(w.sustainedPct > 7.f && w.sustainedPct < 20.f);
    CHECK(w.burst5 > 25.f);
    // The deepening helps: full charges for the wizard, combos for the berserker.
    CHECK(Balance::Classic(*wizard, 60, true, true).sustainedPct > w.sustainedPct * 1.15f);
    Balance::ClassResult const b = Balance::Classic(*berserker, 60, false, false);
    CHECK(Balance::Classic(*berserker, 60, false, true).sustainedPct > b.sustainedPct * 1.08f);
    // Every rotation kills, if slowly; none out-damages a rifle's burst.
    WeaponProfile const* famas = Seed().Find(std::string("famas_mp"));
    if (!famas)
        return;
    Balance::GunCase g;
    g.weapon = *famas;
    g.stoppingPower = true;
    for (uint32_t level : {10u, 30u, 60u})
        for (bool pvp : {false, true})
        {
            float const gun = Balance::Gun(g, level, pvp).burstPct;
            for (Balance::Rotation const& r : Balance::Rotations())
            {
                Balance::ClassResult const c = Balance::Classic(r, level, pvp, true);
                CHECK(c.ttk < 600.f);
                CHECK(c.sustainedPct < gun);
            }
        }
}

TEST(balance_model_engagements_and_the_report)
{
    Balance::GunResult gun;
    gun.ttk = 0.5f;
    gun.roundsPerKill = 10.f;
    gun.load = 45;
    Balance::ClassResult classic;
    classic.ttk = 8.f;
    Balance::Engagement const e = Balance::Fight(gun, classic, 10);
    CHECK(e.gunKills == 4);
    CHECK_NEAR(e.gunOnly, 5.0, 1e-4);
    CHECK_NEAR(e.mixed, 4 * 0.5 + 6 * 8.0, 1e-4);
    CHECK_NEAR(e.classicOnly, 80.0, 1e-4);
    std::string const report = Balance::Report(Seed());
    CHECK(report.find("Level 60") != std::string::npos);
    CHECK(report.find("Mage Wizard") != std::string::npos);
    // FUSION_BALANCE_REPORT=<path>: write the tables BALANCE_GUNS_VS_CLASSES.md quotes.
    if (char const* path = std::getenv("FUSION_BALANCE_REPORT"))
    {
        std::ofstream out(path, std::ios::binary);
        out << report;
    }
}
