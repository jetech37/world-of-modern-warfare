/*
 * Azeroth Warfare: the fusion rules' tests. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"

#include "fusion/Json.h"
#include "fusion/Weapon.h"

using namespace Fusion;

TEST(json_parses_the_basics)
{
    Json::Value v;
    std::string err;
    char const* text = R"({"a": 1.5, "b": [true, false, null], "c": "x\"y\\n", "d": {"e": -2e2}})";
    CHECK(Json::Parse(text, v, err));
    CHECK(v.IsObject());
    CHECK_NEAR(v.NumberOr("a", 0), 1.5, 1e-12);
    CHECK(v.Get("b") && v.Get("b")->IsArray() && v.Get("b")->array.size() == 3);
    CHECK(v.StringOr("c", "") == "x\"y\\n");
    CHECK_NEAR(v.Get("d")->NumberOr("e", 0), -200.0, 1e-9);
    CHECK(v.NumberOr("missing", 7) == 7);
}

TEST(json_rejects_garbage)
{
    Json::Value v;
    std::string err;
    CHECK(!Json::Parse("{\"a\": }", v, err));
    CHECK(!err.empty());
    CHECK(!Json::Parse("[1, 2", v, err));
    CHECK(!Json::Parse("{} trailing", v, err));
}

TEST(weapon_table_loads_json)
{
    std::string text = R"({"version": 1, "weapons": [
        {"id": 1, "name": "m4_mp", "class": "assault_rifle", "damage": 30, "min_damage": 20,
         "max_damage_range": 42.3, "min_damage_range": 83.3, "fire_time": 0.075, "mag_size": 30,
         "max_reserve": 120, "location_multipliers": {"head": 1.4}, "unlock_rank": 4, "starter": true},
        {"id": 2, "name": "spas12_mp", "class": "shotgun", "damage": 40, "min_damage": 10,
         "pellets": 8, "fire_time": 1.0, "unlock_rank": 1}
    ]})";
    WeaponTable t;
    std::string err;
    CHECK(t.LoadJson(text, err));
    CHECK(t.Size() == 2);
    WeaponProfile const* m4 = t.Find("m4_mp");
    CHECK(m4 && m4->id == 1 && m4->weaponClass == WeaponClass::AssaultRifle);
    CHECK(m4 && m4->starter);
    CHECK_NEAR(m4->locationMultiplier[size_t(HitLocation::Head)], 1.4f, 1e-6);
    WeaponProfile const* spas = t.Find(uint16_t(2));
    CHECK(spas && spas->pellets == 8 && spas->weaponClass == WeaponClass::Shotgun);
}

TEST(weapon_table_rejects_unknown_class)
{
    WeaponTable t;
    std::string err;
    char const* railgun = R"({"weapons": [{"id": 1, "name": "x", "class": "railgun"}]})";
    CHECK(!t.LoadJson(railgun, err));
}

#include <fstream>
#include <set>
#include <sstream>

TEST(seed_weapon_table_loads_with_one_starter_per_class)
{
    std::ifstream in(FUSION_SEED_WEAPONS);
    CHECK(in.good());
    std::stringstream text;
    text << in.rdbuf();
    WeaponTable t;
    std::string err;
    CHECK(t.LoadJson(text.str(), err));
    CHECK(t.Size() >= 40);
    std::set<int> classes, starters;
    std::set<uint16_t> ids;
    for (WeaponProfile const& w : t.All())
    {
        classes.insert(int(w.weaponClass));
        if (w.starter)
            starters.insert(int(w.weaponClass));
        CHECK(ids.insert(w.id).second);
        CHECK(UnlockLevel(w.mw2UnlockRank) <= 60);
        CHECK(w.fireTime > 0.f);
    }
    CHECK(classes == starters);
}
