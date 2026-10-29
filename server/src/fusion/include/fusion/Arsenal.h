/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_ARSENAL_H
#define FUSION_ARSENAL_H

#include <cstddef>
#include <cstdint>
#include <string>

// The arsenal's names inside the world (design doc section 5, stage 1): every weapon, attachment,
// camo, perk, deathstreak, killstreak and piece of equipment has a name of its own and a line or
// two of origin, keyed by MW2's internal names. The data lives in one file the client reads,
// client/assets/arsenal_names.json; the table here (ArsenalTable.cpp) is generated from it by
// server/contrib/fusion/make_arsenal_names.py, and ArsenalTests holds the two together.
namespace Fusion::Arsenal
{
    enum class Kind : uint8_t
    {
        Weapon,         // by MW2 weapon name: "m4_mp", "frag_grenade_mp", "uav_mp"
        Attachment,     // by attachment id: "acog"
        Camo,           // by model suffix: "_woodland"
        Perk,           // by MW2 specialty: "specialty_marathon" (perks and deathstreaks)
        Killstreak,     // by MW2 streak name: "uav", "helicopter_minigun"
        Count,
    };

    struct Lore
    {
        char const* key;
        char const* name;
        char const* origin;
    };

    // The generated table, in the file's order.
    Lore const* Table(Kind kind, size_t& count);

    // An entry by key, null when the table has none.
    Lore const* Find(Kind kind, std::string const& key);

    // MW2's internal name for a perk or deathstreak id (Fusion::PerkId) and for a killstreak
    // (Fusion::StreakId); "" for an unknown id.
    char const* PerkKey(uint16_t perkId);
    char const* StreakKey(uint32_t streakId);

    // A name, "" when the table has none (the caller keeps its own).
    std::string WeaponName(std::string const& weaponName);
    std::string AttachmentName(std::string const& id);
    std::string CamoName(std::string const& suffix);
    std::string PerkName(uint16_t perkId);
    std::string StreakName(uint32_t streakId);
}

#endif
