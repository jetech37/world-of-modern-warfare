/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Arsenal.h"

#include "fusion/Catalog.h"
#include "fusion/Killstreak.h"

namespace Fusion::Arsenal
{
    Lore const* Find(Kind kind, std::string const& key)
    {
        size_t n = 0;
        Lore const* table = Table(kind, n);
        for (size_t i = 0; i < n; ++i)
            if (key == table[i].key)
                return &table[i];
        return nullptr;
    }

    char const* PerkKey(uint16_t perkId)
    {
        switch (perkId)
        {
            case PerkId::Marathon: return "specialty_marathon";
            case PerkId::SleightOfHand: return "specialty_fastreload";
            case PerkId::Scavenger: return "specialty_scavenger";
            case PerkId::Bling: return "specialty_bling";
            case PerkId::OneManArmy: return "specialty_onemanarmy";
            case PerkId::StoppingPower: return "specialty_bulletdamage";
            case PerkId::Lightweight: return "specialty_lightweight";
            case PerkId::Hardline: return "specialty_hardline";
            case PerkId::ColdBlooded: return "specialty_coldblooded";
            case PerkId::DangerClose: return "specialty_dangerclose";
            case PerkId::Commando: return "specialty_commando";
            case PerkId::SteadyAim: return "specialty_steadyaim";
            case PerkId::Scrambler: return "specialty_localjammer";
            case PerkId::Ninja: return "specialty_quieter";
            case PerkId::SitRep: return "specialty_bombsquad";
            case PerkId::LastStand: return "specialty_pistoldeath";
            case PerkId::Copycat: return "specialty_copycat";
            case PerkId::Painkiller: return "specialty_painkiller";
            case PerkId::Martyrdom: return "specialty_grenadepulldeath";
            case PerkId::FinalStand: return "specialty_finalstand";
            default: return "";
        }
    }

    char const* StreakKey(uint32_t streakId)
    {
        // MW2's own streak names, in StreakId order.
        static char const* const kKeys[] = {"uav", "airdrop", "counter_uav", "sentry", "predator_missile",
            "precision_airstrike", "harrier_airstrike", "helicopter", "airdrop_mega", "helicopter_flares",
            "stealth_airstrike", "helicopter_minigun", "ac130", "emp", "nuke"};
        static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == StreakId::TacticalNuke, "one key per streak");
        if (streakId >= 1 && streakId <= sizeof(kKeys) / sizeof(kKeys[0]))
            return kKeys[streakId - 1];
        return "";
    }

    namespace
    {
        std::string NameOf(Kind kind, std::string const& key)
        {
            Lore const* l = Find(kind, key);
            return l ? l->name : "";
        }
    }

    std::string WeaponName(std::string const& weaponName)
    {
        // The catalog's names end in "_mp"; a stem alone ("m4") is the same gun.
        if (Lore const* l = Find(Kind::Weapon, weaponName))
            return l->name;
        auto endsWith = [&](char const* tail) {
            std::string const t = tail;
            return weaponName.size() >= t.size() && weaponName.compare(weaponName.size() - t.size(), t.size(), t) == 0;
        };
        // An underbarrel weapon (gl_m4_mp, m4_shotgun_attach_mp) goes by its attachment's name.
        if (weaponName.rfind("gl_", 0) == 0)
            return AttachmentName("gl");
        if (endsWith("_shotgun_attach_mp"))
            return AttachmentName("shotgun");
        if (!endsWith("_mp"))
            return NameOf(Kind::Weapon, weaponName + "_mp");
        return "";
    }

    std::string AttachmentName(std::string const& id) { return NameOf(Kind::Attachment, id); }
    std::string CamoName(std::string const& suffix) { return NameOf(Kind::Camo, suffix); }
    std::string PerkName(uint16_t perkId) { return NameOf(Kind::Perk, PerkKey(perkId)); }
    std::string StreakName(uint32_t streakId) { return NameOf(Kind::Killstreak, StreakKey(streakId)); }
}
