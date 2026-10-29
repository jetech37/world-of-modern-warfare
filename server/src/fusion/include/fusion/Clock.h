/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_CLOCK_H
#define FUSION_CLOCK_H

#include <cstdint>
#include <optional>

namespace Fusion
{
    // Seconds from `thenMs` to `nowMs` on the server's wrapping millisecond clock; infinity when
    // it never happened. Correct across the clock's wrap and past 2^31 ms of uptime.
    float SecondsSince(uint32_t nowMs, std::optional<uint32_t> thenMs);

    // The client stamps its shots with its own millisecond clock. Its offset to the server's is
    // the smallest (server - client) seen recently: the sample with the least latency in it.
    // Recently, not ever: the two clocks drift (6% was measured live), and an all-time minimum
    // falls further behind each second until every shot reads as too old to rewind.
    class ClockSync
    {
    public:
        explicit ClockSync(uint32_t windowMs = 2000) : m_window(windowMs) {}

        // The server time a client stamp stands for, learning from this sample.
        uint32_t ToServerTime(uint32_t clientMs, uint32_t nowMs);

        int32_t Offset() const { return m_offset; }

    private:
        uint32_t m_window;
        bool m_has = false;
        bool m_hasPrevious = false;
        uint32_t m_windowStart = 0;
        int32_t m_currentMin = 0;
        int32_t m_previousMin = 0;
        int32_t m_offset = 0;
    };

    // A token bucket on the wrapping millisecond clock:  requests at once, then one back
    // every . For client requests that cost the server (database writes).
    class RateLimit
    {
    public:
        RateLimit(uint32_t burst, uint32_t refillMs) : m_burst(burst), m_refill(refillMs), m_tokens(burst) {}

        // Spend one request at : false when the bucket is empty.
        bool Allow(uint32_t nowMs);

    private:
        uint32_t m_burst;
        uint32_t m_refill;
        uint32_t m_tokens;
        bool m_started = false;
        uint32_t m_last = 0;
    };
}

#endif
