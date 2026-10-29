/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/ProfessionAdmin.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Fusion { namespace ProfessionAdmin
{
    std::vector<Profession> const& All()
    {
        static std::vector<Profession> const all = {
            {"Alchemy", Kind::Stock, 171, {2259, 3101, 3464, 11611}},
            {"Blacksmithing", Kind::Stock, 164, {2018, 3100, 3538, 9785}},
            {"Enchanting", Kind::Stock, 333, {7411, 7412, 7413, 13920}},
            {"Engineering", Kind::Stock, 202, {4036, 4037, 4038, 12656}},
            {"Herbalism", Kind::Stock, 182, {2366, 2368, 3570, 11993}},
            {"Leatherworking", Kind::Stock, 165, {2108, 3104, 3811, 10662}},
            {"Mining", Kind::Stock, 186, {2575, 2576, 3564, 10248}},
            {"Skinning", Kind::Stock, 393, {8613, 8617, 8618, 10768}},
            {"Tailoring", Kind::Stock, 197, {3908, 3909, 3910, 12180}},
            {"Cooking", Kind::Stock, 185, {2550, 3102, 3413, 18260}},
            {"First Aid", Kind::Stock, 129, {3273, 3274, 7924, 10846}},
            {"Fishing", Kind::Stock, 356, {7620, 7731, 7732, 18248}},
            {"Construction", Kind::Construction, 0, {0, 0, 0, 0}},
            {"Minstrelsy", Kind::Minstrelsy, 0, {0, 0, 0, 0}},
            {"Inscription", Kind::Inscription, 0, {0, 0, 0, 0}},
            {"Jewelcrafting", Kind::Jewelcrafting, 0, {0, 0, 0, 0}},
        };
        return all;
    }

    namespace
    {
        // Lower case, spaces and dashes out: "First Aid" and "first-aid" are "firstaid".
        std::string Key(std::string const& s)
        {
            std::string k;
            for (char c : s)
                if (std::isalnum(static_cast<unsigned char>(c)))
                    k += char(std::tolower(static_cast<unsigned char>(c)));
            return k;
        }
    }

    Profession const* Find(std::string const& name)
    {
        std::string const k = Key(name);
        if (k.empty())
            return nullptr;
        static struct { char const* alias; char const* name; } const aliases[] = {
            {"bs", "Blacksmithing"}, {"lw", "Leatherworking"}, {"ench", "Enchanting"}, {"eng", "Engineering"},
            {"herb", "Herbalism"}, {"fa", "First Aid"}, {"aid", "First Aid"}, {"bandage", "First Aid"},
            {"build", "Construction"}, {"minstrel", "Minstrelsy"}, {"music", "Minstrelsy"},
            {"insc", "Inscription"}, {"scribe", "Inscription"}, {"jc", "Jewelcrafting"}, {"jewel", "Jewelcrafting"},
        };
        for (auto const& a : aliases)
            if (k == a.alias)
                return Find(a.name);
        Profession const* prefixed = nullptr;
        for (Profession const& p : All())
        {
            std::string const pk = Key(p.name);
            if (pk == k)
                return &p;
            if (k.size() >= 3 && pk.compare(0, k.size(), k) == 0)
            {
                if (prefixed)
                    return nullptr;     // ambiguous
                prefixed = &p;
            }
        }
        return prefixed;
    }

    uint8_t RanksFor(uint16_t skill)
    {
        if (skill <= 75)
            return 1;
        if (skill <= 150)
            return 2;
        if (skill <= 225)
            return 3;
        return 4;
    }

    uint16_t RankCap(uint8_t ranks)
    {
        return uint16_t(75u * std::min<uint8_t>(std::max<uint8_t>(ranks, 1), 4));
    }

    char const* Usage()
    {
        return "Usage: .profession list | learn <name|all> [skill 1-300] | set <name> <skill 1-300> | recipes <name|all>";
    }

    Command Parse(std::string const& line)
    {
        Command c;
        std::istringstream in(line);
        std::string verb;
        in >> verb;
        verb = Key(verb);
        std::vector<std::string> words;
        for (std::string w; in >> w;)
            words.push_back(w);
        // A trailing number is the skill; everything before it the name ("first aid 150").
        long number = -1;
        if (!words.empty())
        {
            std::string const& last = words.back();
            if (!last.empty() && std::all_of(last.begin(), last.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }))
            {
                number = last.size() > 4 ? 100000 : std::stol(last);
                words.pop_back();
            }
        }
        std::string name;
        for (std::string const& w : words)
            name += (name.empty() ? "" : " ") + w;

        if (verb.empty() || verb == "list")
        {
            c.ok = name.empty() && number < 0;
            c.verb = Verb::List;
            if (!c.ok)
                c.error = Usage();
            return c;
        }
        if (verb == "learn")
            c.verb = Verb::Learn;
        else if (verb == "set")
            c.verb = Verb::Set;
        else if (verb == "recipes" || verb == "recipe")
            c.verb = Verb::Recipes;
        else
        {
            c.error = Usage();
            return c;
        }
        if (name.empty())
        {
            c.error = Usage();
            return c;
        }
        c.all = Key(name) == "all";
        if (c.all && c.verb == Verb::Set)
        {
            c.error = "Set takes one profession: .profession set <name> <skill>";
            return c;
        }
        if (!c.all)
        {
            c.profession = Find(name);
            if (!c.profession)
            {
                c.error = "No profession called \"" + name + "\".";
                return c;
            }
        }
        if (c.verb == Verb::Set && number < 0)
        {
            c.error = "Set needs a skill: .profession set <name> <skill 1-300>";
            return c;
        }
        if (c.verb == Verb::Recipes && number >= 0)
        {
            c.error = Usage();
            return c;
        }
        if (number >= 0)
            c.skill = uint16_t(std::min<long>(std::max<long>(number, 1), kMaxSkill));
        c.ok = true;
        return c;
    }
}}
