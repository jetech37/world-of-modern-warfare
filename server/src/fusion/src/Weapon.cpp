/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Weapon.h"

#include <algorithm>
#include "fusion/Json.h"

namespace Fusion
{
    WeaponProfile const* WeaponTable::Find(uint16_t id) const
    {
        for (auto const& w : m_weapons)
            if (w.id == id)
                return &w;
        return nullptr;
    }

    WeaponProfile const* WeaponTable::Find(std::string const& name) const
    {
        for (auto const& w : m_weapons)
            if (w.name == name)
                return &w;
        return nullptr;
    }

    WeaponSlot DefaultSlot(WeaponClass c)
    {
        switch (c)
        {
            case WeaponClass::Pistol:
            case WeaponClass::MachinePistol:
            case WeaponClass::Shotgun:
            case WeaponClass::Launcher:
                return WeaponSlot::Secondary;
            case WeaponClass::Lethal:
                return WeaponSlot::Lethal;
            case WeaponClass::Tactical:
                return WeaponSlot::Tactical;
            case WeaponClass::Killstreak:
                return WeaponSlot::Killstreak;
            default:
                return WeaponSlot::Primary;
        }
    }

    namespace
    {
        bool ParseClass(std::string const& s, WeaponClass& out)
        {
            static struct { char const* name; WeaponClass c; } const table[] = {
                {"assault_rifle", WeaponClass::AssaultRifle},
                {"smg", WeaponClass::Smg},
                {"lmg", WeaponClass::Lmg},
                {"shotgun", WeaponClass::Shotgun},
                {"sniper", WeaponClass::Sniper},
                {"pistol", WeaponClass::Pistol},
                {"machine_pistol", WeaponClass::MachinePistol},
                {"launcher", WeaponClass::Launcher},
                {"riot_shield", WeaponClass::RiotShield},
                {"melee", WeaponClass::Melee},
                {"lethal", WeaponClass::Lethal},
                {"tactical", WeaponClass::Tactical},
                {"killstreak", WeaponClass::Killstreak},
            };
            for (auto const& row : table)
            {
                if (s == row.name)
                {
                    out = row.c;
                    return true;
                }
            }
            return false;
        }
    }

    // weapons.json: {"version": 1, "weapons": [{"id": 1, "name": "m4_mp", "class": "assault_rifle",
    // "damage": 30, "min_damage": 20, "max_damage_range": 42.3, "min_damage_range": 83.3,
    // "pellets": 1, "fire_time": 0.075, "mag_size": 30, "max_reserve": 120, "reload_time": 2.2,
    // "reload_empty_time": 2.8, "move_speed_scale": 0.95, "ads_move_speed_scale": 0.5,
    // "location_multipliers": {"head": 1.4, "neck": 1, ...}, "unlock_rank": 1, "starter": true}]}
    bool WeaponTable::LoadJson(std::string const& text, std::string& error)
    {
        Json::Value root;
        if (!Json::Parse(text, root, error))
            return false;
        Json::Value const* list = root.Get("weapons");
        if (!list || !list->IsArray())
        {
            error = "weapons.json has no \"weapons\" array";
            return false;
        }
        std::vector<WeaponProfile> loaded;
        for (Json::Value const& w : list->array)
        {
            if (!w.IsObject())
            {
                error = "a weapon entry is not an object";
                return false;
            }
            WeaponProfile p;
            p.id = uint16_t(w.NumberOr("id", 0));
            p.name = w.StringOr("name", "");
            if (p.id == 0 || p.name.empty())
            {
                error = "a weapon entry lacks an id or name";
                return false;
            }
            if (!ParseClass(w.StringOr("class", "assault_rifle"), p.weaponClass))
            {
                error = "weapon " + p.name + " has an unknown class";
                return false;
            }
            p.damage = float(w.NumberOr("damage", p.damage));
            p.minDamage = float(w.NumberOr("min_damage", p.minDamage));
            p.maxDamageRange = float(w.NumberOr("max_damage_range", p.maxDamageRange));
            p.minDamageRange = float(w.NumberOr("min_damage_range", p.minDamageRange));
            p.pellets = uint8_t(std::max(1.0, w.NumberOr("pellets", 1)));
            p.fireTime = float(w.NumberOr("fire_time", p.fireTime));
            p.hipSpreadMin = float(w.NumberOr("hip_spread_min", 0.0));
            p.hipSpreadMax = float(w.NumberOr("hip_spread_max", 0.0));
            p.adsSpread = float(w.NumberOr("ads_spread", 0.0));
            p.magSize = uint16_t(w.NumberOr("mag_size", p.magSize));
            p.maxReserve = uint16_t(w.NumberOr("max_reserve", p.maxReserve));
            p.reloadTime = float(w.NumberOr("reload_time", p.reloadTime));
            p.reloadEmptyTime = float(w.NumberOr("reload_empty_time", p.reloadEmptyTime));
            p.moveSpeedScale = float(w.NumberOr("move_speed_scale", p.moveSpeedScale));
            p.adsMoveSpeedScale = float(w.NumberOr("ads_move_speed_scale", p.adsMoveSpeedScale));
            p.mw2UnlockRank = uint8_t(w.NumberOr("unlock_rank", 1));
            p.starter = w.BoolOr("starter", false);
            std::string const slot = w.StringOr("slot", "");
            p.slot = slot == "primary" ? WeaponSlot::Primary
                : slot == "secondary" ? WeaponSlot::Secondary
                : slot == "alternate" ? WeaponSlot::Alternate
                : slot == "lethal" ? WeaponSlot::Lethal
                : slot == "tactical" ? WeaponSlot::Tactical
                : slot == "killstreak" ? WeaponSlot::Killstreak : DefaultSlot(p.weaponClass);
            p.alternateOf = uint16_t(w.NumberOr("alternate_of", 0));
            p.alternateAttachment = w.StringOr("attachment", "");
            p.explosionRadius = float(w.NumberOr("explosion_radius", 0.0));
            p.explosionInner = float(w.NumberOr("explosion_inner", 0.0));
            p.explosionOuter = float(w.NumberOr("explosion_outer", 0.0));
            if (Json::Value const* a = w.Get("attachments"); a && a->IsArray())
                for (Json::Value const& id : a->array)
                    p.attachments.push_back(id.string);
            if (Json::Value const* v = w.Get("variants"); v && v->IsObject())
                for (auto const& kv : v->object)
                    p.variants[kv.first] = kv.second.string;
            p.camos = w.BoolOr("camos", false);
            if (Json::Value const* vs = w.Get("variant_stats"); vs && vs->IsObject())
                for (auto const& kv : vs->object)
                    for (auto const& field : kv.second.object)
                        p.variantStats[kv.first][field.first] = field.second.number;
            if (Json::Value const* m = w.Get("location_multipliers"))
            {
                static char const* const keys[] = {"head", "neck", "torso_upper", "torso_lower", "arm", "leg"};
                for (size_t k = 0; k < size_t(HitLocation::Count); ++k)
                    p.locationMultiplier[k] = float(m->NumberOr(keys[k], p.locationMultiplier[k]));
            }
            if (p.fireTime <= 0.f || p.magSize == 0)
            {
                error = "weapon " + p.name + " has no fire time or magazine";
                return false;
            }
            loaded.push_back(std::move(p));
        }
        m_weapons = std::move(loaded);
        return true;
    }

    bool AlternateAllowed(WeaponProfile const& w, uint16_t primary, std::vector<std::string> const& attachments)
    {
        if (w.slot != WeaponSlot::Alternate)
            return true;
        return w.alternateOf == primary
            && std::find(attachments.begin(), attachments.end(), w.alternateAttachment) != attachments.end();
    }
}
