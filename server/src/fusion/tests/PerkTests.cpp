/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Ammo.h"
#include "fusion/Perks.h"

using namespace Fusion;

TEST(perks_of_a_class)
{
    ClassLoadout l;
    l.perk1 = PerkId::Marathon;
    l.perk2 = PerkId::StoppingPower;
    l.perk3 = PerkId::Ninja;
    PerkFlags p = PerksOf(&l, false);
    CHECK(p.marathon && !p.marathonPro);
    CHECK(p.stoppingPower && p.ninja);
    CHECK(!p.lightweight && !p.commando && !p.hardline && !p.scavenger);
    p = PerksOf(&l, true);          // a GM has every Pro
    CHECK(p.marathon && p.marathonPro);
    l.perk2 = PerkId::Hardline;
    l.perk3 = PerkId::Commando;
    p = PerksOf(&l, false);
    CHECK(p.hardline && p.commando && !p.stoppingPower);
    CHECK(PerksOf(nullptr, true).marathon == false);  // no class: no perks
}

TEST(pro_perks_do_more)
{
    ClassLoadout l;
    l.perk1 = PerkId::Marathon;
    l.perk2 = PerkId::DangerClose;
    l.perk3 = PerkId::Hardline;
    PerkFlags regular = PerksOf(&l, false);
    PerkFlags pro = PerksOf(&l, true);
    CHECK(!regular.pro && pro.pro);
    // Danger Close: explosives either way; air support only Pro.
    CHECK_NEAR(ExplosiveDamageMultiplier(regular), 1.25f, 1e-6);
    CHECK_NEAR(ExplosiveDamageMultiplier(regular, true), 1.f, 1e-6);
    CHECK_NEAR(ExplosiveDamageMultiplier(pro, true), 1.25f, 1e-6);
    CHECK_NEAR(ExplosiveDamageMultiplier(PerkFlags{}, false), 1.f, 1e-6);
    // Hardline Pro: a deathstreak a death sooner.
    CHECK(DeathstreakDeaths(regular, 4) == 4);
    CHECK(DeathstreakDeaths(pro, 4) == 3);
    CHECK(DeathstreakDeaths(pro, 1) == 1);
    // Commando Pro: no falling damage.
    l.perk3 = PerkId::Commando;
    CHECK(!NoFallDamage(PerksOf(&l, false)));
    CHECK(NoFallDamage(PerksOf(&l, true)));
}

TEST(stopping_power_is_bullets_only)
{
    PerkFlags p;
    CHECK(BulletDamageMultiplier(p, AmmoType::Basic) == 1.f);
    p.stoppingPower = true;
    CHECK(BulletDamageMultiplier(p, AmmoType::Basic) == 1.4f);
    CHECK(BulletDamageMultiplier(p, AmmoType::Explosive) == 1.f);   // not explosives
}

TEST(scavenger_refills_a_magazine)
{
    WeaponProfile m4;
    m4.magSize = 30;
    m4.maxReserve = 180;
    GunAmmo a;
    a.Reset(m4);
    uint32_t const full = a.Reserve();
    for (int i = 0; i < 40; ++i)
        a.Fire();
    a.Reload();
    uint32_t const before = a.Reserve();
    CHECK(before < full);
    a.Scavenge();
    CHECK(a.Reserve() == std::min<uint32_t>(before + 30, 180));
    for (int i = 0; i < 10; ++i)
        a.Scavenge();
    CHECK(a.Reserve() == 180);      // never past the max
}

TEST(ninja_shortens_detection)
{
    PerkFlags p;
    CHECK(DetectionPerkMultiplier(p) == 1.f);
    p.ninja = true;
    CHECK(DetectionPerkMultiplier(p) == 0.7f);
}

TEST(the_last_perks_and_the_deathstreaks)
{
    ClassLoadout l;
    l.perk1 = PerkId::OneManArmy;
    l.perk2 = PerkId::DangerClose;
    l.perk3 = PerkId::LastStand;
    l.deathstreak = PerkId::Painkiller;
    PerkFlags const p = PerksOf(&l, false);
    CHECK(p.oneManArmy && p.dangerClose && p.lastStand && p.deathstreak == PerkId::Painkiller);
    // Danger Close: explosives x1.25.
    CHECK_NEAR(ExplosiveDamageMultiplier(p), 1.25f, 1e-6);
    CHECK_NEAR(ExplosiveDamageMultiplier(PerkFlags{}), 1.f, 1e-6);
    // One Man Army: a class changes even in combat.
    CHECK(!EquipWaitsForCombat(true, true));
    CHECK(EquipWaitsForCombat(true, false));
    CHECK(!EquipWaitsForCombat(false, false));
}

TEST(a_deathstreak_counts_deaths_since_the_last_kill)
{
    DeathStreak s;
    s.OnDeath();
    s.OnDeath();
    CHECK(!s.Reached(3));
    s.OnDeath();
    CHECK(s.Reached(3));           // Painkiller's three
    s.OnKill();
    CHECK(!s.Reached(1));          // a kill ends it
}

TEST(painkiller_takes_two_thirds_off)
{
    CHECK_NEAR(DamageTakenMultiplier(true), 1.f / 3.f, 1e-6);
    CHECK_NEAR(DamageTakenMultiplier(false), 1.f, 1e-6);
}

TEST(last_stand_holds_a_fatal_hit)
{
    // A fatal hit with Last Stand (or Final Stand) ready leaves the player down at 1 health.
    CHECK(HoldsFatalHit(100, 100, true));
    CHECK(HoldsFatalHit(100, 250, true));
    CHECK(!HoldsFatalHit(100, 99, true));      // not fatal
    CHECK(!HoldsFatalHit(100, 100, false));    // none ready
    CHECK_NEAR(kLastStandSeconds, 10.f, 1e-6);
    CHECK_NEAR(kMartyrdomDelay, 2.5f, 1e-6);
}

TEST(every_perk_does_something_in_the_classic_world)
{
    PerkFlags none;
    CHECK(GoldLootMultiplier(none) == 1.f && CastTimeMultiplier(none) == 1.f);
    CHECK(FallDamageMultiplier(none) == 1.f && KillXpPerkMultiplier(none) == 1.f);
    CHECK(!BlocksCallForHelp(none) && StealthConcealYards(none) == 0.f && StealthDetectYards(none) == 0.f);
    PerkFlags p;
    p.scavenger = p.sleightOfHand = p.scrambler = p.stoppingPower = true;
    p.lightweight = p.hardline = p.coldBlooded = p.ninja = p.sitRep = p.marathon = true;
    CHECK(GoldLootMultiplier(p) > 1.f);
    CHECK(CastTimeMultiplier(p) < 1.f);          // quicker hands
    CHECK(CreatureCastAtMultiplier(p) > 1.f);    // jammed casters
    CHECK(MeleePerkMultiplier(p) > 1.f);
    CHECK(FallDamageMultiplier(p) == 0.5f);
    p.commando = p.pro = true;
    CHECK(FallDamageMultiplier(p) == 0.f);       // Commando Pro: no fall damage at all
    CHECK(KillXpPerkMultiplier(p) > 1.f && BlocksCallForHelp(p));
    CHECK(StealthConcealYards(p) > 0.f && StealthDetectYards(p) > 0.f);
    CHECK(OutOfCombatSpeedMultiplier(p, false) > 1.f && OutOfCombatSpeedMultiplier(p, true) == 1.f);
    // Every perk the catalog has puts something in play.
    ClassLoadout l;
    for (uint16_t id : {PerkId::Scrambler, PerkId::SitRep, PerkId::ColdBlooded})
    {
        l.perk1 = id;
        PerkFlags const f = PerksOf(&l, false);
        CHECK(f.scrambler || f.sitRep || f.coldBlooded);
    }
}

TEST(stacked_perks_add_up_as_the_design_says)
{
    ClassLoadout l;
    l.perk1 = l.perk2 = PerkId::Marathon;       // additive
    PerkFlags p = PerksOf(&l, false);
    CHECK(p.Count(PerkId::Marathon) == 2 && p.marathon);
    CHECK_NEAR(OutOfCombatSpeedMultiplier(p, false), 1.10f, 1e-5);
    CHECK_NEAR(OutOfCombatSpeedMultiplier(p, true), 1.f, 1e-6);
    l.perk1 = l.perk2 = l.perk3 = PerkId::Scavenger;   // additive: a quarter a copy
    CHECK_NEAR(GoldLootMultiplier(PerksOf(&l, false)), 1.75f, 1e-5);
    l.perk1 = l.perk2 = l.perk3 = PerkId::SleightOfHand;   // diminishing: 15, 22.5, 26.25%
    CHECK_NEAR(CastTimeMultiplier(PerksOf(&l, false)), 1.f - 0.2625f, 1e-5);
    l.perk3 = 0;
    CHECK_NEAR(CastTimeMultiplier(PerksOf(&l, false)), 1.f - 0.225f, 1e-5);
    l.perk1 = l.perk2 = l.perk3 = PerkId::Scrambler;       // diminishing
    CHECK_NEAR(CreatureCastAtMultiplier(PerksOf(&l, false)), 1.35f, 1e-5);
    l.perk1 = l.perk2 = PerkId::SitRep;                    // additive yards
    l.perk3 = PerkId::Ninja;
    p = PerksOf(&l, false);
    CHECK_NEAR(StealthDetectYards(p), 10.f, 1e-5);
    CHECK_NEAR(StealthConcealYards(p), 3.f, 1e-5);
    // A flag set by hand counts as one copy.
    PerkFlags hand;
    hand.scavenger = true;
    CHECK_NEAR(GoldLootMultiplier(hand), 1.25f, 1e-5);
    CHECK(Copies(hand, PerkId::Scavenger, hand.scavenger) == 1);
}

TEST(each_slot_chooses_its_perks_pro)
{
    ClassLoadout l;
    l.perk1 = PerkId::Marathon;
    l.perk2 = PerkId::Commando;
    l.perk3 = PerkId::LastStand;
    l.perkPro = 0x02;                           // Commando's Pro only
    PerkFlags p = PerksOf(&l, false);
    CHECK(NoFallDamage(p));
    CHECK(!p.marathonPro && !p.IsPro(PerkId::Marathon));
    CHECK_NEAR(OutOfCombatSpeedMultiplier(p, false), 1.05f, 1e-5);
    CHECK(LastStandExtraMs(p) == 0);
    l.perkPro = 0x05;                           // Marathon's and Last Stand's
    p = PerksOf(&l, false);
    CHECK(!NoFallDamage(p) && p.marathonPro);
    CHECK_NEAR(OutOfCombatSpeedMultiplier(p, false), 1.10f, 1e-5);
    CHECK(LastStandExtraMs(p) == 10000);
    // The Pros the design gives the rest.
    l = ClassLoadout{};
    l.perk1 = PerkId::OneManArmy;
    l.perk2 = PerkId::StoppingPower;
    l.perk3 = PerkId::ColdBlooded;
    p = PerksOf(&l, false);
    CHECK(FieldEditorCooldown(p, 3600) == 3600);
    CHECK_NEAR(MeleePerkMultiplier(p), 1.2f, 1e-6);
    CHECK_NEAR(DetectionPerkMultiplier(p), 1.f, 1e-6);
    l.perkPro = 0x07;
    p = PerksOf(&l, false);
    CHECK(FieldEditorCooldown(p, 3600) == 1800);
    CHECK_NEAR(MeleePerkMultiplier(p), 1.35f, 1e-6);
    CHECK_NEAR(DetectionPerkMultiplier(p), kColdBloodedProDetection, 1e-6);
    // A GM's Pro switch plays every Pro without the bits.
    l.perkPro = 0;
    CHECK(FieldEditorCooldown(PerksOf(&l, true), 3600) == 1800);
}

TEST(field_quartermaster_rearms_in_the_field_on_its_cooldown)
{
    PerkFlags none;
    CHECK(FieldRearmCooldownMs(none) == 0);
    CHECK(!FieldRearmReady(none, 1000, 0));
    PerkFlags oma;
    oma.oneManArmy = true;
    CHECK(FieldRearmCooldownMs(oma) == kFieldRearmCooldownMs);
    CHECK(FieldRearmReady(oma, 1000, 0));                    // never used
    uint32_t const ready = 1000 + FieldRearmCooldownMs(oma);
    CHECK(!FieldRearmReady(oma, 1000 + 60000, ready));
    CHECK(FieldRearmReady(oma, ready, ready));
    CHECK(FieldRearmReady(oma, 0xFFFFFF00u + 0x200u, 0xFFFFFF00u + 0x100u)); // across the wrap
    oma.pro = true;
    CHECK(FieldRearmCooldownMs(oma) == kFieldRearmCooldownMs / 2);
}
