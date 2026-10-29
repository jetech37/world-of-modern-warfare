/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Achievements.h"
#include "fusion/Arsenal.h"
#include "fusion/Catalog.h"
#include "fusion/Json.h"
#include "fusion/Killstreak.h"

#include <fstream>
#include <set>
#include <sstream>

using namespace Fusion;

namespace
{
    std::string ReadAll(std::string const& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    // The client's data file, which the generated table must match.
    Json::Value const& NamesFile()
    {
        static Json::Value const v = [] {
            Json::Value out;
            std::string error;
            Json::Parse(ReadAll(FUSION_VECTORS_DIR "/../../../../client/assets/arsenal_names.json"), out, error);
            return out;
        }();
        return v;
    }

    // The shared catalog, raw, for what the server's Catalog does not keep (the perks' icons).
    Json::Value const& CatalogFile()
    {
        static Json::Value const v = [] {
            Json::Value out;
            std::string error;
            Json::Parse(ReadAll(FUSION_VECTORS_DIR "/../../../contrib/fusion/catalog.json"), out, error);
            return out;
        }();
        return v;
    }

    Catalog const& SeedCatalog()
    {
        static Catalog const c = [] {
            Catalog out;
            std::string error;
            out.LoadJson(ReadAll(FUSION_VECTORS_DIR "/../../../contrib/fusion/catalog.json"), error);
            return out;
        }();
        return c;
    }

    char const* SectionOf(Arsenal::Kind k)
    {
        switch (k)
        {
            case Arsenal::Kind::Weapon: return "weapons";
            case Arsenal::Kind::Attachment: return "attachments";
            case Arsenal::Kind::Camo: return "camos";
            case Arsenal::Kind::Perk: return "perks";
            case Arsenal::Kind::Killstreak: return "killstreaks";
            default: return "";
        }
    }
}

TEST(arsenal_table_matches_the_client_names_file)
{
    Json::Value const& file = NamesFile();
    CHECK(file.IsObject());
    for (uint8_t k = 0; k < uint8_t(Arsenal::Kind::Count); ++k)
    {
        Arsenal::Kind const kind = Arsenal::Kind(k);
        Json::Value const* section = file.Get(SectionOf(kind));
        CHECK(section && section->IsObject());
        if (!section)
            continue;
        size_t n = 0;
        Arsenal::Lore const* table = Arsenal::Table(kind, n);
        // Every entry both ways, same name and origin: else run make_arsenal_names.py.
        CHECK(n == section->object.size());
        for (size_t i = 0; i < n; ++i)
        {
            Json::Value const* e = section->Get(table[i].key);
            CHECK(e && e->StringOr("name", "") == table[i].name && e->StringOr("origin", "") == table[i].origin);
        }
    }
}

TEST(every_catalog_item_has_a_lore_name_and_origin)
{
    Catalog const& c = SeedCatalog();
    CHECK(c.Weapons().Size() > 40);
    for (WeaponProfile const& w : c.Weapons().All())
    {
        std::string const name = Arsenal::WeaponName(w.name);
        CHECK(!name.empty());
        if (name.empty())
            std::printf("    no lore name for %s\n", w.name.c_str());
    }
    for (Attachment const& a : c.Attachments())
        CHECK(!Arsenal::AttachmentName(a.id).empty());
    for (Camo const& m : c.Camos())
        CHECK(!Arsenal::CamoName(m.suffix).empty());
    for (Perk const& p : c.Perks())
        CHECK(!Arsenal::PerkName(p.id).empty());
    for (uint32_t s = StreakId::Uav; s <= StreakId::TacticalNuke; ++s)
        CHECK(!Arsenal::StreakName(s).empty());
    // Every entry carries an origin of a line or two, and no two weapons share a name unless
    // they are the same thing (a killstreak's weapon and the streak).
    std::set<std::string> names;
    size_t n = 0;
    Arsenal::Lore const* t = Arsenal::Table(Arsenal::Kind::Weapon, n);
    for (size_t i = 0; i < n; ++i)
    {
        std::string const origin = t[i].origin;
        CHECK(origin.size() > 20 && origin.size() < 240);
        CHECK(names.insert(t[i].name).second);
    }
}

TEST(perk_keys_are_the_catalogs_specialties)
{
    Json::Value const& cat = CatalogFile();
    size_t seen = 0;
    for (char const* section : {"perks", "deathstreaks"})
    {
        Json::Value const* list = cat.Get(section);
        CHECK(list && list->IsArray());
        if (!list)
            continue;
        for (Json::Value const& p : list->array)
        {
            uint16_t const id = uint16_t(p.NumberOr("id", 0));
            CHECK(p.StringOr("icon", "") == Arsenal::PerkKey(id));
            ++seen;
        }
    }
    CHECK(seen == 20);
}

TEST(arsenal_names_keep_the_requested_examples)
{
    CHECK(Arsenal::WeaponName("spas12_mp") == "Bilgewater Scattercannon");
    CHECK(Arsenal::WeaponName("barrett_mp") == "Thorium Siege Rifle");
    CHECK(Arsenal::WeaponName("cheytac_mp") == "Mountaineer's Longshot");
    CHECK(Arsenal::WeaponName("masada_mp") == "Ironforge Battle Rifle");
    CHECK(Arsenal::WeaponName("ump45") == "Gnomeregan Pulse Carbine");     // a stem alone
    CHECK(Arsenal::WeaponName("rpg_mp") == "Steamwheedle Rocket Tube");
    CHECK(Arsenal::WeaponName("frag_grenade_mp") == "Thorium Grenade");
    CHECK(Arsenal::WeaponName("c4_mp") == "Seaforium Charge");
    CHECK(Arsenal::WeaponName("flash_grenade_mp") == "Arcane Flash Bomb");
    CHECK(Arsenal::WeaponName("gl_m4_mp") == Arsenal::AttachmentName("gl"));   // underbarrel
    CHECK(Arsenal::WeaponName("m4_shotgun_attach_mp") == Arsenal::AttachmentName("shotgun"));
    CHECK(Arsenal::WeaponName("no_such_gun_mp").empty());
    CHECK(Arsenal::StreakName(StreakId::Uav) == "Gnomish Spy Gyro");
    CHECK(Arsenal::StreakName(StreakId::CarePackage) == "Goblin Cargo Drop");
    CHECK(Arsenal::StreakName(StreakId::PredatorMissile) == "Goblin Rocket");
    CHECK(Arsenal::StreakName(StreakId::ChopperGunner) == "Gyrocopter Gunship");
    CHECK(Arsenal::StreakName(StreakId::Ac130) == "Zeppelin Gunship");
    CHECK(Arsenal::StreakName(StreakId::SentryGun) == "Dwarven Sentry Turret");
    CHECK(Arsenal::StreakName(StreakId::TacticalNuke) == "Gnomeregan Radiation Bomb");
    CHECK(Arsenal::StreakName(0).empty() && Arsenal::StreakName(16).empty());
    // The real gun stays out of the name: its hint sits in the origin.
    Arsenal::Lore const* spas = Arsenal::Find(Arsenal::Kind::Weapon, "spas12_mp");
    CHECK(spas && std::string(spas->name).find("SPAS") == std::string::npos
        && std::string(spas->origin).find("SP-12") != std::string::npos);
}

TEST(server_texts_use_the_arsenal_names)
{
    CHECK(GunDisplayName("m4_mp") == "Stormwind Guard Carbine");
    CHECK(GunDisplayName("frag_grenade_mp") == "Thorium Grenade");
    CHECK(StreakName(StreakId::Uav) == "Gnomish Spy Gyro");
    Catalog const& c = SeedCatalog();
    Requirement r;
    r.key = {Stat::PerkKills, PerkId::StoppingPower};
    r.needed = 5;
    CHECK(DescribeRequirement(r, c) == "Get 5 kills with Thorium-Core Loads in your class");
}
