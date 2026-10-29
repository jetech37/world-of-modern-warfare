/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Progression.h"

namespace Fusion
{
    uint32_t ScaleKillXp(uint32_t vanillaXp, bool rested, XpRates const& r)
    {
        float xp = float(vanillaXp) * r.creatureKill * (rested ? r.restedMultiplier : 1.f);
        // Never round a real kill down to nothing.
        return vanillaXp > 0 ? std::max<uint32_t>(1, uint32_t(std::lround(xp))) : 0;
    }

    uint32_t ScaleQuestXp(uint32_t vanillaXp, bool rested, XpRates const& r)
    {
        float xp = float(vanillaXp) * r.quest * (rested ? r.restedMultiplier : 1.f);
        return uint32_t(std::lround(xp));
    }

    void HealthRegen::OnDamaged(bool wounding)
    {
        m_sinceDamage = 0.f;
        if (wounding)
            m_wound = m_rules.woundDuration;
    }

    float HealthRegen::Update(float dt, float currentFraction)
    {
        m_sinceDamage += dt;
        m_wound = std::max(0.f, m_wound - dt);
        if (!Regenerating() || currentFraction >= 1.f)
            return 0.f;
        float gain = dt / m_rules.regenFullTime;
        return std::min(gain, 1.f - currentFraction);
    }
}
