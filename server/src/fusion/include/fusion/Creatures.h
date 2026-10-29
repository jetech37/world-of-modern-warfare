/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_CREATURES_H
#define FUSION_CREATURES_H

#include "fusion/Common.h"
#include "fusion/Gunfights.h"

namespace Fusion
{
    // The fusion AI's behaviour profiles (design doc, section 19), picked by creature type and a
    // few traits, with a small override table on top for the odd ones.
    enum class Profile : uint8_t
    {
        Soldier,        // armed humanoids: shoot, take cover, flank, fall back
        Caster,         // telegraphed snipers and support
        Rusher,         // beasts: sprint, zigzag, pounce, surround
        Horde,          // undead: slow, many, tough
        Juggernaut,     // brutes: charges and slams, beaten through weak points
        Flyer,          // dive attacks
        Machine,        // shielded; EMP and FMJ counter
        Passive,        // critters and totems: no fusion behaviour
    };

    struct CreatureTraits
    {
        CreatureType type = CreatureType::Humanoid;
        bool caster = false;            // the template uses mana and casts
        bool rangedWeapon = false;      // carries a bow, gun or crossbow
        bool flying = false;
        bool large = false;             // model bounds well above a player's
    };

    Profile ClassifyCreature(CreatureTraits const& t);

    // How much faster an aggroed creature runs at a player (kGunfight.chaseSpeed) until it is in
    // its reach: `meleeReach` for those that fight hand to hand, kGunfight.casterRange for a
    // caster; critters and totems keep their pace. `fastNow`: it runs fast now (Gunfights.h).
    float ChaseSpeedMultiplier(Profile p, float distanceToTarget, float meleeReach, bool fastNow);

    // The anti-exploit rules (design doc, section 19).
    struct ExploitRules
    {
        float unreachableAfter = 3.f;       // seconds a target must be out of reach
        float unreachableDamageTaken = 0.5f;// damage the creature takes while it can't reach you
        float searchDuration = 8.f;         // seconds searching the last known position
        float leashMultiplier = 1.5f;       // longer leashes than 1.12's
    };

    // Tracks whether a creature's target is out of reach long enough to switch to a ranged answer.
    class ReachTracker
    {
    public:
        explicit ReachTracker(ExploitRules r = {}) : m_rules(r) {}

        // Advance by dt with whether the creature can path to its target this tick.
        void Update(float dt, bool canReach);
        // The creature should use its ranged answer and take reduced damage.
        bool Unreachable() const { return m_out >= m_rules.unreachableAfter; }
        float DamageTakenMultiplier() const { return Unreachable() ? m_rules.unreachableDamageTaken : 1.f; }

    private:
        ExploitRules m_rules;
        float m_out = 0.f;
    };

    // Gunfire carries (Gunfights.h GunfireHearing): a wall between muffles it to kWallMuffle.
    constexpr float kWallMuffle = kGunfight.wallMuffle;
    // A blast is heard this far.
    constexpr float kBlastHearing = kGunfight.blastHearing;
    // A creature shot calls its own this close to it in.
    constexpr float kCallForHelpRadius = kGunfight.callForHelpRadius;
    // Creatures chase this many times further before giving up (guns reach 100 yards and more).
    constexpr float kLeashScale = kGunfight.leashScale;
}

#endif
