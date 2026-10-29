/*
 * Azeroth Warfare: tests for the unlock gate (Access.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Access.h"
#include "fusion/Damage.h"
#include "fusion/Weapon.h"

#include <fstream>
#include <sstream>

using namespace Fusion;

TEST(gm_uses_every_gun)
{
    std::ifstream in(FUSION_SEED_WEAPONS);
    std::stringstream text;
    text << in.rdbuf();
    WeaponTable t;
    std::string err;
    CHECK(t.LoadJson(text.str(), err));
    CHECK(t.Size() > 0);
    for (WeaponProfile const& w : t.All())
        CHECK(CanUseWeapon(1, true, w));
}

TEST(level_gates_non_starters)
{
    WeaponProfile late;
    late.starter = false;
    late.mw2UnlockRank = 70;
    CHECK(!CanUseWeapon(59, false, late));
    CHECK(CanUseWeapon(60, false, late));

    WeaponProfile starter;
    starter.starter = true;
    starter.mw2UnlockRank = 70;
    CHECK(CanUseWeapon(1, false, starter));
}

TEST(gm_uses_every_round)
{
    CHECK(CanUseAmmo(1, true, AmmoType::Blessed));
    CHECK(!CanUseAmmo(29, false, AmmoType::Blessed));
    CHECK(CanUseAmmo(30, false, AmmoType::Blessed));
    CHECK(EffectiveLevel(5, true) == 60);
    CHECK(EffectiveLevel(5, false) == 5);
}
