/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PERKS_H
#define FUSION_PERKS_H

#include "fusion/Damage.h"
#include "fusion/Loadout.h"
#include "fusion/Movement.h"

namespace Fusion
{
    // The perks a kit puts in play (nullptr: no kit, no perks): how many slots hold each
    // (stacking, WOMW design section 3), and which play their Pro (the kit's Pro bits, checked
    // against the account's earned Pros when it was saved); every one its Pro when `pro` (a GM
    // by default; `.fusion pro on|off` sets it either way).
    PerkFlags PerksOf(ClassLoadout const* l, bool pro);

    // The copies of a perk in play, 0 to kMaxPerkStack: its slots, or one where only its flag is
    // set (`flag`, the PerkFlags field).
    uint8_t Copies(PerkFlags const& p, uint16_t id, bool flag);
    // A diminishing stack's total share: `first` for one copy, each next copy half the last's.
    float DiminishingShare(float first, uint8_t copies);

    // Cold-Blooded Pro: creatures notice the player at this share of the distance as well.
    constexpr float kColdBloodedProDetection = 0.85f;

    // Stopping Power: bullet damage x1.4 (not explosive rounds, not the knife).
    float BulletDamageMultiplier(PerkFlags const& p, AmmoType ammo);

    // Ninja: creatures notice the player at x0.7 the distance (Cold-Blooded Pro: x0.85 more).
    float DetectionPerkMultiplier(PerkFlags const& p);

    // Danger Close: explosive damage x1.25; Danger Close Pro adds air support (killstreaks).
    float ExplosiveDamageMultiplier(PerkFlags const& p, bool airSupport = false);

    // The deaths a deathstreak needs: Hardline Pro needs one fewer.
    uint32_t DeathstreakDeaths(PerkFlags const& p, uint32_t needed);

    // Commando Pro: no falling damage.
    bool NoFallDamage(PerkFlags const& p);

    // One Man Army: a class changes even in combat (others wait for out of combat).
    bool EquipWaitsForCombat(bool inCombat, bool oneManArmy);

    // The deaths since the player's last kill: MW2's deathstreaks (Copycat 4, Painkiller 3,
    // Martyrdom 4, Final Stand 4) start once it reaches theirs, and a kill ends it.
    struct DeathStreak
    {
        uint32_t deaths = 0;
        void OnDeath() { ++deaths; }
        void OnKill() { deaths = 0; }
        bool Reached(uint32_t needed) const { return needed > 0 && deaths >= needed; }
    };

    // ── WOMW: every perk also does something in the classic world ─────────────────────────
    // Scavenger: a quarter more gold from a creature's body you loot, per copy (additive); the
    // Pro a quarter more.
    float GoldLootMultiplier(PerkFlags const& p);
    // Sleight of Hand: spells cast 15% quicker (the hands are quicker); copies diminish.
    float CastTimeMultiplier(PerkFlags const& p);
    // Scrambler: a creature's spell at the player casts a fifth slower (its aim jammed); copies
    // diminish; the Pro a tenth more.
    float CreatureCastAtMultiplier(PerkFlags const& p);
    // Stopping Power: the weapon in hand (the hands' melee, crate::hands) hits a fifth harder
    // (the Pro: 35%). It never stacks: guns are the shared baseline (design section 1).
    float MeleePerkMultiplier(PerkFlags const& p);
    // Lightweight: falls hurt half as much; Commando Pro: not at all.
    float FallDamageMultiplier(PerkFlags const& p);
    // Hardline: a tenth more experience from kills.
    float KillXpPerkMultiplier(PerkFlags const& p);
    // Cold-Blooded: creatures don't call their friends against the player.
    bool BlocksCallForHelp(PerkFlags const& p);
    // Ninja: stealth is harder to see through (yards off a detector's reach; copies diminish, the
    // Pro 2 more); SitRep: the player sees stealth from farther (5 yards a copy, the Pro 3 more).
    float StealthConcealYards(PerkFlags const& p);
    float StealthDetectYards(PerkFlags const& p);
    // Marathon: out of combat the player runs a twentieth faster a copy (crossing Azeroth on
    // foot); the Pro a twentieth more.
    float OutOfCombatSpeedMultiplier(PerkFlags const& p, bool inCombat);

    // One Man Army Pro: the field editor (Kits.h) is ready again in half the time.
    uint32_t FieldEditorCooldown(PerkFlags const& p, uint32_t normal);

    // Field Quartermaster (One Man Army): the Rearm ability works anywhere, not only resting in
    // an inn or a city, once every kFieldRearmCooldownMs (the Pro half that): every gun's
    // magazine and reserve and the equipment, free, as a kit station's. Without the perk: never.
    constexpr uint32_t kFieldRearmCooldownMs = 10u * 60u * 1000u;
    uint32_t FieldRearmCooldownMs(PerkFlags const& p);
    // Whether the field rearm is ready at `nowMs` (`readyAtMs` 0: never used).
    bool FieldRearmReady(PerkFlags const& p, uint32_t nowMs, uint32_t readyAtMs);

    // Last Stand Pro: the bleed-out comes this much later (more time to be revived).
    constexpr float kLastStandProSeconds = 10.f;
    uint32_t LastStandExtraMs(PerkFlags const& p);

    // Painkiller: a respawn's first seconds take a third of the damage (MW2's triple health).
    constexpr float kPainkillerSeconds = 10.f;
    float DamageTakenMultiplier(bool painkillerActive);

    // Last Stand (and the Final Stand deathstreak): a fatal hit leaves the player down at 1
    // health for kLastStandSeconds, then Last Stand dies and Final Stand gets back up.
    constexpr float kLastStandSeconds = 10.f;
    bool HoldsFatalHit(uint32_t health, uint32_t damage, bool ready);

    // Martyrdom: a grenade drops where the player fell and goes off this long after (MW2's
    // frag: 256 in, 150 to 55).
    constexpr float kMartyrdomDelay = 2.5f;
    constexpr float kMartyrdomRadius = 7.11f;
    constexpr float kMartyrdomInner = 150.f;
    constexpr float kMartyrdomOuter = 55.f;
}

#endif
