/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Clock.h"

#include <limits>

#include <algorithm>

namespace Fusion
{
    float SecondsSince(uint32_t nowMs, std::optional<uint32_t> thenMs)
    {
        if (!thenMs)
            return std::numeric_limits<float>::infinity();
        return float(nowMs - *thenMs) / 1000.f;
    }

    uint32_t ClockSync::ToServerTime(uint32_t clientMs, uint32_t nowMs)
    {
        int32_t const sample = int32_t(nowMs - clientMs);
        if (!m_has || nowMs - m_windowStart >= m_window)
        {
            // A new window. The one before it still counts if it was the last one, so a window's
            // first samples are not judged by a single reading; an older one is stale.
            bool const contiguous = m_has && nowMs - m_windowStart < 2 * m_window;
            m_hasPrevious = contiguous;
            m_previousMin = m_currentMin;
            m_currentMin = sample;
            m_windowStart = nowMs;
            m_has = true;
        }
        else
            m_currentMin = std::min(m_currentMin, sample);

        m_offset = m_hasPrevious ? std::min(m_previousMin, m_currentMin) : m_currentMin;
        return clientMs + uint32_t(m_offset);
    }

    bool RateLimit::Allow(uint32_t nowMs)
    {
        if (!m_started)
        {
            m_started = true;
            m_last = nowMs;
        }
        uint32_t const elapsed = nowMs - m_last;
        if (m_refill > 0 && elapsed >= m_refill)
        {
            uint32_t const earned = elapsed / m_refill;
            m_tokens = earned >= m_burst - m_tokens ? m_burst : m_tokens + earned;
            m_last += earned * m_refill;
        }
        if (m_tokens == 0)
            return false;
        --m_tokens;
        return true;
    }
}
