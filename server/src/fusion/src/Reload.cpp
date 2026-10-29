/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Reload.h"
#include "fusion/Stats.h"

#include <algorithm>

namespace Fusion
{
    float ReloadFactor(bool sleightOfHand, float handlingReload)
    {
        // The client holds the handling it is sent to (0.1, 1]; the server never sends less than
        // the output's limit.
        float const handling = Clamp(handlingReload, Stats::OutputLimit(Stats::Out::ReloadTime), 1.f);
        return (sleightOfHand ? 0.5f : 1.f) * handling;
    }

    float QuickestReloadSeconds(WeaponProfile const& w, float factor)
    {
        float secs = w.reloadTime > 0.f ? w.reloadTime : 0.f;
        if (w.reloadEmptyTime > 0.f)
            secs = secs > 0.f ? std::min(secs, w.reloadEmptyTime) : w.reloadEmptyTime;
        return secs * std::max(0.f, factor);
    }

    float QuickestShellSeconds(WeaponProfile const& w, float factor)
    {
        return std::max(0.f, w.reloadTime) * std::max(0.f, factor);
    }

    void ReloadPace::Fired(uint32_t nowMs)
    {
        m_fired = true;
        m_lastRoundMs = nowMs;
        m_shellsSinceRound = 0;
    }

    ReloadPace::Due ReloadPace::Take(uint32_t nowMs, float fullSecs, float shellSecs)
    {
        Due due;
        // Seconds since the last round (wrap-safe; a clock that went back counts as none).
        int32_t const sinceMs = m_fired ? int32_t(nowMs - m_lastRoundMs) : 0;
        float const since = sinceMs <= 0 ? 0.f : float(sinceMs) / 1000.f;
        if (m_pendingFull && (!m_fired || since >= kReloadSlack * fullSecs))
        {
            due.full = true;
            m_pendingFull = false;
        }
        while (m_pendingShells > 0
            && (!m_fired || since >= kReloadSlack * shellSecs * float(m_shellsSinceRound + 1)))
        {
            --m_pendingShells;
            ++m_shellsSinceRound;
            ++due.shells;
        }
        return due;
    }

    float RecentLeast::Update(float value, uint32_t nowMs, uint32_t memoryMs)
    {
        // A new least, or the old one forgotten: start over from this one.
        if (!m_set || value <= m_least || int32_t(nowMs - m_untilMs) >= 0)
        {
            m_set = true;
            m_least = value;
            m_untilMs = nowMs + memoryMs;
        }
        return std::min(m_least, value);
    }
}
