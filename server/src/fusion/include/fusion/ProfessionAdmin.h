/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PROFESSION_ADMIN_H
#define FUSION_PROFESSION_ADMIN_H

#include "fusion/Common.h"

#include <array>
#include <string>
#include <vector>

// ── The game masters' profession tools (.profession, vps/ADMIN.md) ──
// Testing a trade means levelling it first; these name every profession a character can have
// (vanilla's twelve, plus World of Modern Warfare's Construction and Minstrelsy, which live in
// the fusion's own skill table rather than SkillLine.dbc), the trainer spells that teach each
// rank, and the rank a skill needs.
namespace Fusion { namespace ProfessionAdmin
{
    enum class Kind : uint8_t
    {
        Stock,          // a SkillLine.dbc profession: its rank spells and recipes are WoW's
        Construction,   // the fusion's (FusionConstruction.cpp): recipes come with the tier
        Minstrelsy,     // the fusion's (FusionMinstrelsy.cpp): instrument recipes learned
        Inscription,    // the fusion's (FusionCrafts.cpp): recipes learned
        Jewelcrafting,  // the fusion's (FusionCrafts.cpp): recipes learned
    };

    struct Profession
    {
        char const* name;
        Kind kind;
        uint16_t skillLine;             // SkillLine.dbc id (0 for the fusion's own)
        // The spells Apprentice, Journeyman, Expert and Artisan teach (0 for the fusion's own).
        std::array<uint32_t, 4> rankSpells;
    };

    // Every profession, vanilla's in SkillLine order, then Construction, Minstrelsy, Inscription
    // and Jewelcrafting.
    std::vector<Profession> const& All();
    // A profession by name, any case, or by a prefix of at least three letters ("alch",
    // "first aid", "firstaid", "lw" for Leatherworking, "bs" for Blacksmithing); null if none.
    Profession const* Find(std::string const& name);

    // The skill's ceiling: 300, vanilla's max.
    constexpr uint16_t kMaxSkill = 300;
    // How many ranks a skill needs: 1 (Apprentice, to 75) up to 4 (Artisan, to 300); 1 for 0.
    uint8_t RanksFor(uint16_t skill);
    // The rank's cap (75, 150, 225, 300).
    uint16_t RankCap(uint8_t ranks);

    // A command line: "learn <name|all> [skill]", "set <name> <value>", "recipes <name|all>",
    // "list". `ok` false (with `error`) for anything malformed; the skill is clamped to 1..300.
    enum class Verb : uint8_t { List, Learn, Set, Recipes };
    struct Command
    {
        bool ok = false;
        Verb verb = Verb::List;
        bool all = false;
        Profession const* profession = nullptr;
        uint16_t skill = kMaxSkill;
        std::string error;
    };
    Command Parse(std::string const& line);
    // The usage line.
    char const* Usage();
}}

#endif
