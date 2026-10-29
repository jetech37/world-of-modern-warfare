/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * The vanilla 1.12 pets as battle companions (Companions::VanillaPets, 31_vanilla_companions.sql):
 * every retired pet item maps to a species' own item, so it starts as that line's first form; the
 * world SQL replaces every source and is safe to run again; the characters swap is idempotent.
 */

#include "Test.h"
#include "fusion/CompanionMoves.h"
#include "fusion/Companions.h"

#include <fstream>
#include <map>
#include <set>
#include <string>

using namespace Fusion;
using namespace Fusion::Companions;

namespace
{
    std::string ReadSql(char const* file)
    {
        std::ifstream in(std::string(FUSION_VECTORS_DIR "/../../../sql/custom/azeroth_warfare/") + file);
        CHECK(in.good());
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
}

TEST(vanilla_pets_each_become_a_stage_one_companion)
{
    auto const& pets = VanillaPets();
    CHECK(pets.size() == 61);
    std::set<uint32_t> olds, news;
    for (VanillaPet const& p : pets)
    {
        CHECK(olds.insert(p.oldItem).second);           // each pet item once
        CHECK(news.insert(p.item).second);              // each to its own line
        CHECK(!IsCompanionItem(p.oldItem));             // the old entries are no companion items
        CHECK(IsCompanionItem(p.item));
        CHECK(VanillaPetReplacement(p.oldItem) == p.item);
        CompanionSpecies const* sp = FindByItem(StarterSpecies(), p.item);
        CHECK(sp && sp->id == p.species);
        if (!sp)
            continue;
        // The species' own item: a fresh copy is its first form, level 1.
        CompanionState s;
        CHECK(FreshState(StarterSpecies(), StarterItems(), p.item, s) == sp);
        CHECK(s.stage == 0 && s.level == 1 && s.species == p.species);
        CHECK(sp->stageCount == 3);
        CHECK(MovesetOf(*sp) != Moveset::None);
    }
    CHECK(VanillaPetReplacement(90026) == 0 && VanillaPetReplacement(0) == 0);
    // A few by hand: a pet that already was a line's first form, and new lines.
    CHECK(VanillaPetReplacement(10822) == SpeciesItem(1));     // Dark Whelpling: Black Whelpling
    CHECK(VanillaPetReplacement(8488) == SpeciesItem(26));     // Silver Tabby: Snow Tiger Kitten
    CHECK(VanillaPetReplacement(4401) == SpeciesItem(28));     // Mechanical Squirrel
    CHECK(VanillaPetReplacement(8485) == SpeciesItem(165));    // Bombay
    CHECK(VanillaPetReplacement(23007) == SpeciesItem(210));   // Mr. Wiggles
}

TEST(vanilla_pet_lines_start_as_the_pet_itself)
{
    // Species 165-214: the first form is the vanilla pet's own creature, and its drop source.
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        if (sp.id < 165 || sp.id > 214)
            continue;
        CHECK(sp.item == SpeciesItem(sp.id));
        CHECK(sp.stages[0].display != 0);
        CHECK(sp.drops.entries.size() == 1 && sp.drops.entries[0] == sp.stages[0].creature);
        CHECK(sp.stageCount == 1 || sp.evolutions[0].level == 20);
        CHECK(!sp.stages[sp.stageCount - 1].rideable);
    }
}

TEST(vanilla_pet_swap_is_idempotent_and_covers_every_copy)
{
    std::vector<std::string> const sql = VanillaPetSwapSql();
    CHECK(sql.size() == VanillaPets().size() * 4);
    std::set<std::string> tables;
    for (std::string const& s : sql)
    {
        // Only "old -> new" rewrites: a second run matches no row (no new item is an old one).
        CHECK(s.rfind("UPDATE `", 0) == 0 && s.find(" WHERE `item_id` = ") != std::string::npos);
        uint32_t const from = uint32_t(std::stoul(s.substr(s.rfind("= ") + 2)));
        CHECK(VanillaPetReplacement(from) != 0);
        CHECK(VanillaPetReplacement(VanillaPetReplacement(from)) == 0);
        tables.insert(s.substr(8, s.find('`', 8) - 8));
    }
    CHECK(tables == std::set<std::string>({"item_instance", "character_inventory", "mail_items", "auction"}));
}

TEST(vanilla_pet_world_sql_replaces_every_source)
{
    std::string const sql = ReadSql("31_vanilla_companions.sql");
    for (VanillaPet const& p : VanillaPets())
    {
        std::string const o = std::to_string(p.oldItem), n = std::to_string(p.item);
        for (char const* t : {"npc_vendor", "creature_loot_template", "gameobject_loot_template", "item_loot_template",
                 "reference_loot_template", "mail_loot_template"})
            CHECK(sql.find(std::string("UPDATE IGNORE `") + t + "` SET `item` = " + n + " WHERE `item` = " + o + ";")
                != std::string::npos);
        CHECK(sql.find("SET `RewItemId1` = " + n + " WHERE `RewItemId1` = " + o + ";") != std::string::npos);
        CHECK(sql.find("SET `RewChoiceItemId6` = " + n + " WHERE `RewChoiceItemId6` = " + o + ";") != std::string::npos);
        CHECK(sql.find("SET `effectItemType1` = " + n + " WHERE `effectItemType1` = " + o + ";") != std::string::npos);
    }
    // Safe to run again: the rows it writes are deleted first, and nothing else is inserted.
    CHECK(sql.find("DELETE FROM `fusion_companion_species` WHERE `id` BETWEEN 165 AND 214;")
        < sql.find("INSERT INTO `fusion_companion_species`"));
    CHECK(sql.find("DELETE FROM `item_template` WHERE `entry` BETWEEN 91165 AND 91214;")
        < sql.find("INSERT INTO `item_template`"));
    size_t inserts = 0;
    for (size_t at = 0; (at = sql.find("INSERT", at)) != std::string::npos; ++at)
        ++inserts;
    CHECK(inserts == 2);
    CHECK(sql.find("DROP TABLE") == std::string::npos);
}
