/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_WEAPON_H
#define FUSION_WEAPON_H

#include "fusion/Common.h"
#include <map>

#include <string>
#include <vector>

namespace Fusion
{
    // MW2's weapon classes, which set movement speed, the class bonuses and which perks apply.
    enum class WeaponClass : uint8_t
    {
        AssaultRifle,
        Smg,
        Lmg,
        Shotgun,
        Sniper,
        Pistol,
        MachinePistol,
        Launcher,
        RiotShield,
        Melee,
        Lethal,         // equipment: a grenade, a knife to throw, a claymore, C4
        Tactical,       // equipment: a flash, stun or smoke grenade
        Killstreak,     // a reward called in with the shot message
    };

    // Whether the class fires rounds: a riot shield and a knife only strike.
    inline bool FiresRounds(WeaponClass c)
    {
        return c != WeaponClass::RiotShield && c != WeaponClass::Melee && c != WeaponClass::Lethal
            && c != WeaponClass::Tactical && c != WeaponClass::Killstreak;
    }

    // Where a hit landed; each weapon carries MW2's own multiplier for each.
    enum class HitLocation : uint8_t
    {
        Head,
        Neck,
        TorsoUpper,
        TorsoLower,
        Arm,
        Leg,
        Count,
    };

    // Which Create-a-Class slot a gun fills: MW2's secondaries are the pistols, machine pistols,
    // shotguns and launchers.
    enum class WeaponSlot : uint8_t
    {
        Primary,
        Secondary,
        Alternate,      // an underbarrel weapon (gl_m4_mp): raised from its gun, never picked
        Lethal,         // a class's lethal equipment
        Tactical,       // a class's tactical equipment
        Killstreak,     // a reward, never a class's pick
    };

    WeaponSlot DefaultSlot(WeaponClass c);

    // Whether a class whose primary is `primary` with `attachments` may fire an underbarrel
    // weapon: it must be that gun's, carried by one of them. True for any other weapon.
    bool AlternateAllowed(struct WeaponProfile const& w, uint16_t primary, std::vector<std::string> const& attachments);

    // One weapon's behaviour as MW2 defines it, exported from the player's MW2 install
    // (weapons.json). Ranges are converted from MW2 inches to yards at export.
    struct WeaponProfile
    {
        uint16_t id = 0;
        std::string name;               // "m4_mp"
        WeaponClass weaponClass = WeaponClass::AssaultRifle;

        float damage = 30.f;            // CoD damage at or inside maxDamageRange
        float minDamage = 20.f;         // CoD damage at or beyond minDamageRange
        float maxDamageRange = 42.f;    // yards
        float minDamageRange = 83.f;    // yards
        uint8_t pellets = 1;            // shotguns fire several

        float fireTime = 0.08f;         // seconds between shots
        uint16_t magSize = 30;
        uint16_t maxReserve = 120;
        float reloadTime = 2.2f;
        float reloadEmptyTime = 2.8f;
        float moveSpeedScale = 0.95f;   // MW2's own, 1.0 = unencumbered
        float adsMoveSpeedScale = 0.5f;
        // MW2's spread cones (degrees): the standing hip spread's least and most, and aimed.
        float hipSpreadMin = 0.f;
        float hipSpreadMax = 0.f;
        float adsSpread = 0.f;
        bool fmj = false;               // the class gun carries FMJ rounds (Ballistics.h)
        bool silenced = false;          // the class gun carries a silencer (Creatures.h hearing)
        bool akimbo = false;            // a gun in each hand: magSize counts both hands' rounds
        // A splash (yards, CoD damage at the centre and at the edge); radius 0 for none.
        float explosionRadius = 0.f;
        float explosionInner = 0.f;
        float explosionOuter = 0.f;
        // An underbarrel weapon's gun and the attachment that carries it (WeaponSlot::Alternate).
        uint16_t alternateOf = 0;
        std::string alternateAttachment;

        float locationMultiplier[size_t(HitLocation::Count)] = {1.4f, 1.f, 1.f, 1.f, 1.f, 1.f};

        uint8_t mw2UnlockRank = 1;      // MW2's own unlock rank, 1 = default
        bool starter = false;           // one per class at level 1

        // Create-a-Class (catalog version 2; empty in version 1).
        WeaponSlot slot = WeaponSlot::Primary;
        std::vector<std::string> attachments;           // allowed ids, MW2's order
        std::map<std::string, std::string> variants;    // "acog+silencer" -> "m4_acog_silencer_mp"
        // What each combination's definition changes, field name to value (catalog units):
        // "silencer" -> {"max_damage_range": 13.89, ...}.
        std::map<std::string, std::map<std::string, double>> variantStats;
        bool camos = false;

        float LocationMultiplier(HitLocation loc) const { return locationMultiplier[size_t(loc)]; }
    };

    // The share of its MW2 falloff ranges a weapon class fights at (Gunfights.h, kGunfight):
    // WoW's fights are longer than MW2's maps.
    float FalloffRangeScale(WeaponClass c);

    // MW2's damage falloff: full damage inside maxDamageRange, minDamage beyond minDamageRange,
    // linear between; both ranges stretched by the class's FalloffRangeScale. No cap: past the
    // least-damage range a round still does its least.
    inline float CodDamageAtRange(WeaponProfile const& w, float distance)
    {
        float const k = FalloffRangeScale(w.weaponClass);
        float const fullTo = w.maxDamageRange * k;
        float const leastFrom = w.minDamageRange * k;
        if (distance <= fullTo)
            return w.damage;
        if (distance >= leastFrom || leastFrom <= fullTo)
            return w.minDamage;
        float t = (distance - fullTo) / (leastFrom - fullTo);
        return Lerp(w.damage, w.minDamage, t);
    }

    // The level a weapon unlocks at: MW2's rank scaled from MW2's cap (70) to WoW's (60).
    inline uint8_t UnlockLevel(uint8_t mw2Rank)
    {
        int level = int(std::ceil(float(mw2Rank) * 60.f / 70.f));
        return uint8_t(std::max(1, std::min(60, level)));
    }

    // The weapon roster: the exported profiles, looked up by id or name.
    class WeaponTable
    {
    public:
        void Add(WeaponProfile profile) { m_weapons.push_back(std::move(profile)); }
        WeaponProfile const* Find(uint16_t id) const;
        WeaponProfile const* Find(std::string const& name) const;
        std::vector<WeaponProfile> const& All() const { return m_weapons; }
        size_t Size() const { return m_weapons.size(); }

        // Parse weapons.json as the export tool writes it. Returns false and fills error on a
        // malformed file; unknown keys are ignored so the format can grow.
        bool LoadJson(std::string const& text, std::string& error);

    private:
        std::vector<WeaponProfile> m_weapons;
    };
}

#endif
