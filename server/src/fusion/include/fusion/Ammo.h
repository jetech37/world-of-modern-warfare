/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_AMMO_H
#define FUSION_AMMO_H

#include "fusion/Common.h"
#include "fusion/Damage.h"
#include "fusion/Weapon.h"

#include <array>

namespace Fusion
{
    // ── Limited ammunition (design doc, section 9; design/BALANCE_GUNS_VS_CLASSES.md) ──
    // Guns win the first seconds of a fight; ammunition is what they spend to do it. Under the
    // limited model a character carries MW2's magazine and reserve and gets rounds back by going
    // back to a kit station (a trainer or an innkeeper: free, the trip is the cost), by
    // scavenging them from the fallen (gunners and players; Scavenger doubles a body's), from the
    // Scavenger perk's magazine a kill, a Packmule, ammo pickups, Field Quartermaster's rearm in
    // the field on its cooldown (Perks.h), or as a trickle back up to one magazine when out of
    // combat. Class abilities cost no ammunition, so a long fight, a dungeon or a camp-after-camp
    // grind leans on them. Every number is a server setting (Fusion.Ammo.*, FusionMgr::Load);
    // `limited` off restores the old model (a field trickle to half, a full load at respawn).
    struct AmmoRules
    {
        bool limited = true;
        // Out of combat (after GunAmmo::kFieldRearmDelay) the reserve trickles back to this many
        // magazines: never stranded with an empty gun, never a full load for free.
        float fieldFloorMags = 1.f;
        // ... at this many rounds a second: a breather, not a refill (30 rounds in a minute).
        float fieldRoundsPerSecond = 0.5f;
        // The old model's field kit: half the max reserve.
        float legacyFieldShare = 0.5f;
        // A fresh life (a respawn): every magazine full and the reserve topped up to this share
        // of its max (never lowered). The old model: 1, a full load.
        float respawnReserveShare = 0.5f;
        // A gunner NPC killed (it carried a gun): the gun in hand takes this many magazines from
        // the body; an enemy player, this many. Scavenger adds its own magazine for every gun.
        float gunnerScavengeMags = 0.5f;
        float playerScavengeMags = 1.f;
        // An elite gunner carries this many times a normal one's.
        float eliteScavengeScale = 3.f;
        // Scavenger (Gravedigger's Bandolier) takes this many times a body's rounds.
        float scavengerBodyScale = 2.f;
        // A boss killed in an instance leaves its armory: everyone who fought it has every gun
        // topped up to this share of a full load (a dungeon's trash is the classes' to carry).
        float bossRestockShare = 1.f;
    };
    AmmoRules const& CurrentAmmoRules();
    void SetAmmoRules(AmmoRules const& r);

    // The magazines a body gives the gun in hand (`mags`, the gunner's or player's share), with
    // the Scavenger perk or without.
    float BodyScavengeMags(float mags, bool scavenger, AmmoRules const& r = CurrentAmmoRules());

    // One gun's ammunition (design doc, section 21): MW2's magazine and reserve of basic rounds,
    // which are free, plus a carried reserve of the special type loaded into it. An akimbo pair
    // keeps one for each hand, each with one gun's magazine.
    class GunAmmo
    {
    public:
        // How many magazines of a special type the character can carry for each gun.
        static constexpr uint32_t kSpecialMagazinesCarried = 4;
        // The share of the reserve the field rearm restores, and how fast.
        static constexpr float kFieldKitShare = 0.5f;
        static constexpr float kFieldRearmDelay = 10.f;     // seconds out of combat
        static constexpr float kFieldRearmPerSecond = 0.1f; // share of max reserve per second

        void Reset(WeaponProfile const& w);
        // A picked-up gun's rounds (Pickups.h): what it had on the ground, held to its magazine
        // and reserve. After Reset.
        void Set(uint32_t mag, uint32_t reserve);

        // Load a special type (from the Armory); basic rounds when it runs out.
        void LoadSpecial(AmmoType t) { m_loaded = t; }
        AmmoType Loaded() const { return m_loaded; }
        // What the next round is: the loaded special while any is carried, else basic.
        AmmoType NextRound() const;

        // Fire one round; false when the magazine is empty.
        bool Fire();
        // Reload from the reserve; returns rounds moved.
        uint32_t Reload();
        // One round from the reserve (a shotgun loading shell by shell); 0 when full or dry.
        uint32_t ReloadOne();

        // Out-of-combat trickle back up to the field kit: one magazine of reserve under the limited
        // model, half the max reserve under the old one (AmmoRules).
        void FieldRearm(float secondsOutOfCombat, float dt, AmmoRules const& r = CurrentAmmoRules());
        // What the field trickle refills the reserve to.
        uint32_t FieldTarget(AmmoRules const& r = CurrentAmmoRules()) const;
        // A fresh life: magazines full, the reserve topped up to AmmoRules::respawnReserveShare
        // (all of it under the old model).
        void RespawnResupply(AmmoRules const& r = CurrentAmmoRules());
        // The magazine and reserve topped up to `share` of their max (never lowered).
        void TopUp(float share);
        // The basic rounds missing from a full load (magazine and reserve).
        uint32_t MissingRounds() const;
        // Rounds put back: the magazine first, then the reserve; returns how many went in.
        uint32_t Restock(uint32_t rounds);
        // Rounds taken from a body: `mags` magazines' worth (at least one round) into the reserve,
        // up to its max; returns how many went in.
        uint32_t ScavengeMags(float mags);
        // A full kit: respawn or a camp.
        void FullResupply();

        uint32_t Mag() const { return m_mag; }
        uint32_t Reserve() const { return m_reserve; }
        uint32_t Special(AmmoType t) const { return m_special[size_t(t)]; }
        uint32_t SpecialCap() const { return m_magSize * kSpecialMagazinesCarried; }
        // Top every special type the character has unlocked back up to the cap (a quartermaster).
        void QuartermasterRearm(uint32_t level);
        void AddSpecial(AmmoType t, uint32_t rounds);
        // Scavenger: a magazine's worth back into the reserve, up to the max.
        void Scavenge();

        // Field gear (FieldGear.h): `n` more rounds (an equipment's charges) in the magazine past
        // its size, up to `cap` in all; returns how many went in.
        uint32_t AddCharges(uint32_t n, uint32_t cap);
        // A bandolier (FieldGear.h): the reserve holds the gun's max times 1 + `share`. A smaller
        // max takes the reserve down with it; `fill` tops it up to the new max (a fresh life).
        void SetReserveBonus(float share, bool fill);
        uint32_t MaxReserve() const { return m_maxReserve; }

    private:
        uint16_t m_magSize = 0;
        uint16_t m_maxReserve = 0;
        uint16_t m_baseReserve = 0;     // the gun's own max, before a bandolier
        uint32_t m_mag = 0;
        uint32_t m_reserve = 0;
        float m_trickle = 0.f;
        AmmoType m_loaded = AmmoType::Basic;
        std::array<uint32_t, size_t(AmmoType::Count)> m_special{};
    };

    // The quartermaster's fee for a full special rearm, in copper: about a quest reward or two at
    // the character's level, so it stays a small, one-stop cost (design doc, section 21).
    uint32_t QuartermasterFee(uint32_t level);
}

#endif
