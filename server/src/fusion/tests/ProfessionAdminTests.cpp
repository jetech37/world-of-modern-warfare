/*
 * Azeroth Warfare: the fusion rules' tests. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/ProfessionAdmin.h"

#include <set>

using namespace Fusion::ProfessionAdmin;

TEST(profession_admin_names_every_profession)
{
    CHECK(All().size() == 16);
    std::set<uint16_t> lines;
    std::set<uint32_t> spells;
    for (Profession const& p : All())
    {
        CHECK(Find(p.name) == &p);
        if (p.kind != Kind::Stock)
            continue;
        CHECK(p.skillLine != 0);
        lines.insert(p.skillLine);
        for (uint32_t s : p.rankSpells)
        {
            CHECK(s != 0);
            spells.insert(s);
        }
    }
    CHECK(lines.size() == 12);
    CHECK(spells.size() == 48);
    CHECK(Find("first aid") && Find("FirstAid") == Find("first-aid"));
    CHECK(Find("alch") && std::string(Find("alch")->name) == "Alchemy");
    CHECK(std::string(Find("lw")->name) == "Leatherworking");
    CHECK(std::string(Find("bs")->name) == "Blacksmithing");
    CHECK(Find("construction")->kind == Kind::Construction);
    CHECK(Find("Minstrelsy")->kind == Kind::Minstrelsy);
    CHECK(Find("insc")->kind == Kind::Inscription && Find("Jewelcrafting")->kind == Kind::Jewelcrafting);
    CHECK(Find("jc")->kind == Kind::Jewelcrafting && Find("jewel")->kind == Kind::Jewelcrafting);
    CHECK(Find("min") == nullptr);          // Mining or Minstrelsy: ambiguous
    CHECK(Find("xy") == nullptr);
    CHECK(Find("") == nullptr);
    CHECK(Find("Mining")->skillLine == 186);
}

TEST(profession_admin_ranks_follow_the_skill)
{
    CHECK(RanksFor(0) == 1 && RanksFor(1) == 1 && RanksFor(75) == 1);
    CHECK(RanksFor(76) == 2 && RanksFor(150) == 2);
    CHECK(RanksFor(151) == 3 && RanksFor(225) == 3);
    CHECK(RanksFor(226) == 4 && RanksFor(300) == 4);
    CHECK(RankCap(1) == 75 && RankCap(4) == 300 && RankCap(9) == 300 && RankCap(0) == 75);
}

TEST(profession_admin_parses_its_command_line)
{
    Command c = Parse("learn all");
    CHECK(c.ok && c.verb == Verb::Learn && c.all && c.skill == 300);
    c = Parse("learn first aid 150");
    CHECK(c.ok && c.profession && std::string(c.profession->name) == "First Aid" && c.skill == 150);
    c = Parse("learn construction 999");
    CHECK(c.ok && c.skill == 300);              // clamped
    c = Parse("set mining 0");
    CHECK(c.ok && c.skill == 1);                // clamped up
    c = Parse("set mining");
    CHECK(!c.ok && !c.error.empty());
    c = Parse("set all 100");
    CHECK(!c.ok);
    c = Parse("recipes minstrelsy");
    CHECK(c.ok && c.verb == Verb::Recipes && c.profession->kind == Kind::Minstrelsy);
    c = Parse("recipes all");
    CHECK(c.ok && c.all);
    c = Parse("recipes tailoring 5");
    CHECK(!c.ok);
    c = Parse("learn underwater basket weaving");
    CHECK(!c.ok && c.error.find("No profession") != std::string::npos);
    c = Parse("");
    CHECK(c.ok && c.verb == Verb::List);
    c = Parse("list");
    CHECK(c.ok && c.verb == Verb::List);
    c = Parse("dance");
    CHECK(!c.ok);
}
