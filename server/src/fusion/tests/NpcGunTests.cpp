/*
 * Azeroth Warfare: tests for NPCs with guns (NpcGuns.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Gunfights.h"
#include "fusion/NpcGuns.h"
#include "fusion/Scale.h"

#include <string>

using namespace Fusion;

namespace
{
    GunFacts Humanoid(char const* name, uint32_t entry)
    {
        GunFacts f;
        f.type = CreatureType::Humanoid;
        f.name = name;
        f.entry = entry;
        return f;
    }
}

TEST(who_carries_a_gun)
{
    // A humanoid that already shoots (a bow, a gun, a thrown weapon) always swaps to a gun.
    GunFacts archer = Humanoid("Defias Pathstalker", 7);
    archer.rangedWeapon = true;
    CHECK(ClassifyGunner(archer) == GunRole::Gunner);
    // A labourer stays at melee: the Deadmines' miners, a peon, a slave.
    CHECK(ClassifyGunner(Humanoid("Defias Miner", 1)) == GunRole::MeleeTrash);
    CHECK(ClassifyGunner(Humanoid("Lazy Peon", 2)) == GunRole::MeleeTrash);
    CHECK(ClassifyGunner(Humanoid("Gnoll Brute Laborer", 3)) == GunRole::MeleeTrash);
    // A caster waits for the spell system; a beast has no hands for one.
    GunFacts mage = Humanoid("Defias Conjurer", 4);
    mage.caster = true;
    CHECK(ClassifyGunner(mage) == GunRole::None);
    GunFacts wolf = Humanoid("Young Wolf", 5);
    wolf.type = CreatureType::Beast;
    CHECK(ClassifyGunner(wolf) == GunRole::None);
    // Other humanoid fighters: a share of each kind carries guns, decided by its entry, so a kind
    // always fights the same way; the gunner camps (Defias and the like) most of them, the wild
    // humanoids (murlocs, kobolds, gnolls) a few.
    int gunners = 0, wild = 0;
    for (uint32_t e = 1000; e < 1200; ++e)
    {
        if (ClassifyGunner(Humanoid("Defias Thug", e)) == GunRole::Gunner)
            ++gunners;
        if (ClassifyGunner(Humanoid("Murloc Warrior", e)) == GunRole::Gunner)
            ++wild;
    }
    CHECK(gunners > 200 * 0.7 && gunners < 200 * 0.95);
    CHECK(wild > 200 * 0.1 && wild < 200 * 0.3);
    CHECK(ClassifyGunner(Humanoid("Defias Thug", 1234)) == ClassifyGunner(Humanoid("Defias Thug", 1234)));
}

TEST(gunner_camps_go_by_faction_or_name)
{
    // By faction template: the Defias (17), the Scarlet Crusade (67), the Syndicate (87).
    CHECK(CampOf(17, "Some Bandit") && std::string(CampOf(17, "Some Bandit")->name) == "Defias Brotherhood");
    CHECK(CampOf(67, "Crimson Guardsman") != nullptr);
    CHECK(CampOf(87, "Syndicate Footpad") != nullptr);
    // A shared template (16, the world's monsters) by name.
    CHECK(CampOf(16, "Wastewander Bandit") != nullptr);
    CHECK(CampOf(16, "Twilight Geomancer") != nullptr);
    CHECK(CampOf(16, "Cave Yeti") == nullptr);
    // Not a camp: murlocs, kobolds.
    CHECK(CampOf(18, "Murloc Warrior") == nullptr);
    CHECK(CampOf(26, "Kobold Tunneler") == nullptr);
    // A camp's faction makes a fighter of any name a likely gunner.
    int gunners = 0;
    for (uint32_t e = 2000; e < 2200; ++e)
    {
        GunFacts f = Humanoid("Thug", e);
        f.faction = 54;     // Dark Iron
        if (ClassifyGunner(f) == GunRole::Gunner)
            ++gunners;
    }
    CHECK(gunners > 200 * 0.7);
}

TEST(bosses_keep_their_own_ways)
{
    // A boss is never armed (its script stays as it is), even one that carries a bow.
    GunFacts boss = Humanoid("Edwin VanCleef", 639);
    boss.faction = 17;
    boss.boss = true;
    CHECK(ClassifyGunner(boss) == GunRole::None);
    boss.rangedWeapon = true;
    CHECK(ClassifyGunner(boss) == GunRole::None);
}

TEST(npc_guns_go_by_level_and_rank)
{
    auto classOf = [](std::string const& gun) {
        if (gun.find("usp") == 0 || gun.find("beretta") == 0 || gun.find("deserteagle") == 0) return WeaponClass::Pistol;
        if (gun.find("pp2000") == 0 || gun.find("glock") == 0 || gun.find("tmp") == 0) return WeaponClass::MachinePistol;
        if (gun.find("mp5k") == 0 || gun.find("ump45") == 0 || gun.find("kriss") == 0 || gun.find("uzi") == 0 || gun.find("p90") == 0) return WeaponClass::Smg;
        if (gun.find("m240") == 0 || gun.find("rpd") == 0 || gun.find("mg4") == 0 || gun.find("sa80") == 0) return WeaponClass::Lmg;
        if (gun.find("barrett") == 0 || gun.find("m21") == 0 || gun.find("wa2000") == 0 || gun.find("cheytac") == 0) return WeaponClass::Sniper;
        if (gun.find("spas12") == 0 || gun.find("m1014") == 0 || gun.find("aa12") == 0 || gun.find("striker") == 0) return WeaponClass::Shotgun;
        return WeaponClass::AssaultRifle;
    };
    for (uint32_t e = 1; e < 60; ++e)
    {
        // Low levels: pistols, SMGs and the odd shotgun; never a rifle, an LMG or a sniper.
        WeaponClass const low = classOf(NpcGunFor(e, 8, CreatureRank::Normal));
        CHECK(low == WeaponClass::Pistol || low == WeaponClass::MachinePistol || low == WeaponClass::Smg || low == WeaponClass::Shotgun);
        // High-level elites: rifles and LMGs.
        WeaponClass const elite = classOf(NpcGunFor(e, 55, CreatureRank::Elite));
        CHECK(elite == WeaponClass::Lmg || elite == WeaponClass::AssaultRifle);
        // A rare elite past the first levels: a sniper.
        CHECK(classOf(NpcGunFor(e, 35, CreatureRank::RareElite)) == WeaponClass::Sniper);
        // Normal ones at 45 and up: rifles mostly, an LMG now and then, never a sniper.
        WeaponClass const high = classOf(NpcGunFor(e, 50, CreatureRank::Normal));
        CHECK(high != WeaponClass::Sniper && high != WeaponClass::Pistol);
    }
    CHECK(NpcGunFor(77, 30, CreatureRank::Elite) == NpcGunFor(77, 30, CreatureRank::Elite));
}

TEST(each_level_has_its_guns)
{
    CHECK(!NpcGunFor(10, 5).empty());
    CHECK(NpcGunItemFor(5) == 2508u);       // Old Blunderbuss
    CHECK(NpcGunItemFor(25) == 3024u);      // BKP 2700 "Enforcer"
    CHECK(NpcGunItemFor(60) == 16004u);     // Dark Iron Rifle
    // A kind keeps its gun.
    CHECK(NpcGunFor(77, 30) == NpcGunFor(77, 30));
}

TEST(npc_aim_falls_off_with_range_and_cover)
{
    auto chance = [](float distance, Stance stance, Gait gait) {
        NpcAimInput in;
        in.distance = distance;
        in.heldMs = 5000;
        in.gait = gait;
        return NpcConeHitChance(NpcAimCone(in), distance, NpcTargetBody(stance, false), 0);
    };
    float const close = chance(5.f, Stance::Stand, Gait::Still);
    float const distant = chance(40.f, Stance::Stand, Gait::Still);
    CHECK(close > distant && close <= kGunfight.maxHitChance && distant > 0.1f && distant < 0.3f);
    CHECK(chance(20.f, Stance::Prone, Gait::Still) < chance(20.f, Stance::Stand, Gait::Still));
    CHECK(chance(20.f, Stance::Stand, Gait::Sprint) < chance(20.f, Stance::Stand, Gait::Still));
}

TEST(npc_rounds_hurt_by_level)
{
    // An M4 round (30) from an NPC: the creature's multiplier, no level gap.
    Scale::CreatureFacts npc;
    npc.level = 20;
    uint32_t const even = NpcHitDamage(30.f, Scale::CreatureGunMultiplier(npc), 0.f);
    CHECK(even >= 34 && even <= 36);
    Scale::CreatureFacts elite = npc;
    elite.rank = CreatureRank::Elite;
    CHECK(NpcHitDamage(30.f, Scale::CreatureGunMultiplier(elite), 0.f) > even);   // an elite hits harder
    Scale::CreatureFacts higher = npc;
    higher.level = 30;
    CHECK(NpcHitDamage(30.f, Scale::CreatureGunMultiplier(higher), 0.f) > even); // a higher level too
    // A level 5 thug against a level 60 still hurts: the same round, whoever it hits.
    Scale::CreatureFacts thug;
    thug.level = 5;
    CHECK(NpcHitDamage(30.f, Scale::CreatureGunMultiplier(thug), 0.f) >= 30);
    // Armor takes its cut.
    CHECK(NpcHitDamage(30.f, 1.f, 0.3f) == 21u);
    CHECK(NpcHitDamage(0.1f, 1.f, 0.f) >= 1u);
}

TEST(npcs_fire_in_bursts)
{
    NpcTrigger t;
    uint32_t now = 0;
    int shots = 0;
    for (; now < 3000; now += 50)
        if (t.Fire(now, 3, 100, 1200))
            ++shots;
    // Three-round bursts 1.2 s apart: about two bursts in three seconds.
    CHECK(shots >= 6 && shots <= 9);
}

TEST(npcs_fight_like_wow_creatures_over_time)
{
    auto settled = [](float distance, int32_t gap) {
        NpcAimInput in;
        in.distance = distance;
        in.heldMs = 5000;
        return NpcConeHitChance(NpcAimCone(in), distance, NpcTargetBody(Stance::Stand, false), gap);
    };
    // A low NPC misses a far higher player most of the time.
    CHECK(settled(10.f, -55) < 0.2f);
    CHECK(settled(25.f, 3) > settled(25.f, 0));
    // One equal-level gunner (three-round bursts, 1.2 s apart, at 15 yards, its aim settled) takes
    // a level 20 player's health in a few seconds: dangerous at medium range, not instant.
    float const shotsPerSec = 3.f / 1.4f;
    Scale::CreatureFacts npc;
    npc.level = 20;
    float const perSec = shotsPerSec * settled(15.f, 0)
        * float(NpcHitDamage(30.f, Scale::CreatureGunMultiplier(npc), 0.f)) / Scale::PlayerMaxHealth(20, 0.f);
    std::printf("  (an equal gunner at 15 yd takes %.0f%% of a level 20's health a second)\n", perSec * 100.f);
    CHECK(perSec > 0.15f && perSec < 0.5f);
}
