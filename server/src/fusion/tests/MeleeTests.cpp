/*
 * Azeroth Warfare: tests for melee by the weapon in hand (Melee.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Melee.h"

using namespace Fusion;
using namespace Fusion::Melee;

TEST(each_weapon_kind_swings_at_its_own_pace)
{
    struct Row { uint8_t sub; uint16_t ms; float dmg; bool two; };
    Row const rows[] = {
        {Dagger, 450, 35.f, false}, {Fist, 400, 30.f, false}, {Sword, 600, 60.f, false},
        {Axe, 700, 70.f, false}, {Mace, 800, 85.f, false}, {Staff, 900, 75.f, true},
        {Polearm, 1050, 110.f, true}, {Sword2, 1000, 120.f, true}, {Axe2, 1100, 135.f, true},
        {Mace2, 1250, 150.f, true}, {kNoWeapon, 500, 25.f, false},
    };
    for (Row const& r : rows)
    {
        Profile const p = ProfileFor(r.sub);
        CHECK(p.subclass == r.sub);
        CHECK(p.swingMs == r.ms);
        CHECK_NEAR(p.baseDamage, r.dmg, 1e-6);
        CHECK(p.twoHanded == r.two);
        CHECK(p.lungeMs == uint16_t(float(r.ms) * 1.4f + 0.5f));
    }
    // By weight: maces slowest and hardest, then axes, then swords.
    CHECK(ProfileFor(Mace).swingMs > ProfileFor(Axe).swingMs && ProfileFor(Axe).swingMs > ProfileFor(Sword).swingMs);
    CHECK(ProfileFor(Mace).baseDamage > ProfileFor(Axe).baseDamage && ProfileFor(Axe).baseDamage > ProfileFor(Sword).baseDamage);
    // Odd kinds swing as their nearest.
    CHECK(ProfileFor(Spear).swingMs == 1050 && ProfileFor(Spear).subclass == Spear);
    CHECK(ProfileFor(Wand).swingMs == 500);
}

TEST(the_weapon_and_the_wielder_set_the_damage)
{
    // A level 60 weapon of the expected 57 DPS: the base, times M.
    CHECK_NEAR(WeaponFactor(57.f, 60, true), 1.f, 1e-5);
    CHECK_NEAR(WeaponFactor(10.f, 60, true), 0.6f, 1e-6);
    CHECK_NEAR(WeaponFactor(500.f, 60, true), 2.f, 1e-6);
    CHECK_NEAR(WeaponFactor(0.f, 60, false), 1.f, 1e-6);
    CHECK_NEAR(Damage(ProfileFor(Mace2), 57.f, 60, true, 3.f), 450.f, 1e-2);
    CHECK_NEAR(Damage(ProfileFor(Dagger), 3.9f, 1, true, 1.f), 35.f, 1e-3);
}

TEST(swings_never_come_faster_than_the_weapon)
{
    CHECK(Ready(450, 450));
    CHECK(Ready(410, 450));         // the network's jitter
    CHECK(!Ready(300, 450));
    CHECK(!Ready(1000, 1250));
}

TEST(the_melee_profile_message_carries_both_hands)
{
    std::vector<uint8_t> const b = EncodeProfile(ProfileFor(Mace2), 0x01020304u, 0x0A0B0C0Du,
        {OffHandKind::Shield, kNoWeapon, 0x11223344u});
    CHECK(b.size() == 21);
    CHECK(b[15] == 2 && b[16] == 255);
    CHECK(b[17] == 0x44 && b[18] == 0x33 && b[19] == 0x22 && b[20] == 0x11);
    CHECK(b[0] == 20 && b[1] == Mace2 && b[2] == 1);
    CHECK(b[3] == (1250 & 0xFF) && b[4] == (1250 >> 8));
    CHECK(b[5] == (1750 & 0xFF) && b[6] == (1750 >> 8));
    CHECK(b[7] == 0x04 && b[8] == 0x03 && b[9] == 0x02 && b[10] == 0x01);
    CHECK(b[11] == 0x0D && b[12] == 0x0C && b[13] == 0x0B && b[14] == 0x0A);
    std::vector<uint8_t> const none = EncodeProfile(ProfileFor(kNoWeapon), 0, 0);
    CHECK(none.size() == 21 && none[1] == 255 && none[2] == 0 && none[15] == 0);
}
