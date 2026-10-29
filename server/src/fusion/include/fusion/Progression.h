/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PROGRESSION_H
#define FUSION_PROGRESSION_H

#include "fusion/Common.h"

namespace Fusion
{
    // Leveling pace (design doc, section 17): about 60 hours to 60. Kills are several times faster
    // than vanilla, so creature XP is cut; quest XP stays, so quests carry leveling.
    struct XpRates
    {
        float creatureKill = 0.4f;
        float quest = 1.0f;
        float restedMultiplier = 2.0f;  // rested XP is Double XP
    };

    uint32_t ScaleKillXp(uint32_t vanillaXp, bool rested, XpRates const& r = {});
    uint32_t ScaleQuestXp(uint32_t vanillaXp, bool rested, XpRates const& r = {});

    // Attachments and Pro perks unlock by use, at MW2's requirement times the grind multiplier.
    constexpr uint32_t kGrindMultiplier = 10;
    inline uint32_t UseRequirement(uint32_t mw2Requirement) { return mw2Requirement * kGrindMultiplier; }

    // Health regen and the downed state (design doc, section 16).
    struct HealthRules
    {
        float regenDelay = 5.f;         // seconds without damage before regen starts
        float regenFullTime = 4.f;      // seconds from empty to full once it starts
        float woundDuration = 8.f;      // a boss or elite hit stops regen this long
        float downedDuration = 20.f;    // bleed-out time while downed in a group
        float reviveTime = 6.f;         // any ally
        float healerReviveTime = 2.f;   // Priests, Paladins, Shamans, Druids
    };

    class HealthRegen
    {
    public:
        explicit HealthRegen(HealthRules rules = {}) : m_rules(rules) {}

        void OnDamaged(bool wounding);
        // Advance by dt; returns the health fraction (0..1) to add this tick.
        float Update(float dt, float currentFraction);
        bool Regenerating() const { return m_sinceDamage >= m_rules.regenDelay && m_wound <= 0.f; }

    private:
        HealthRules m_rules;
        float m_sinceDamage = 1e9f;
        float m_wound = 0.f;
    };

    enum class ReviverKind : uint8_t
    {
        Anyone,
        Healer,
    };

    inline float ReviveTime(ReviverKind k, HealthRules const& r = {})
    {
        return k == ReviverKind::Healer ? r.healerReviveTime : r.reviveTime;
    }
}

#endif
