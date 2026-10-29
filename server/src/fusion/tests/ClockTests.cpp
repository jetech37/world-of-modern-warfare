/*
 * Azeroth Warfare: tests for the client clock sync (Clock.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Clock.h"

#include <cstdint>

using namespace Fusion;

namespace
{
    // A shot fired at server time `fired` by a client whose clock reads `clientAt(fired)`,
    // arriving `latency` ms later: how old the sync thinks it is on arrival.
    template <class ClientAt>
    int32_t AgeOnArrival(ClockSync& sync, uint32_t fired, uint32_t latency, ClientAt clientAt)
    {
        uint32_t const now = fired + latency;
        return int32_t(now - sync.ToServerTime(clientAt(fired), now));
    }
}

TEST(clock_steady_latency)
{
    ClockSync sync;
    auto client = [](uint32_t s) { return s - 50000u; };
    int32_t worst = 0;
    for (uint32_t t = 100000; t < 160000; t += 100)
        worst = std::max(worst, AgeOnArrival(sync, t, 40 + (t / 100) % 7, client));
    CHECK(worst <= 50);
}

TEST(clock_follows_a_slow_client)
{
    // The client's clock runs 6% slow (measured live): the all-time minimum fell 7 s behind in two
    // minutes and every hit was refused as too old.
    ClockSync sync;
    auto client = [](uint32_t s) { return uint32_t(double(s) * 0.94); };
    int32_t worst = 0;
    for (uint32_t t = 100000; t < 220000; t += 100)
        worst = std::max(worst, AgeOnArrival(sync, t, 40, client));
    CHECK(worst < 400);
}

TEST(clock_follows_a_fast_client)
{
    ClockSync sync;
    auto client = [](uint32_t s) { return uint32_t(double(s) * 1.06); };
    int32_t worst = 0, best = 1 << 30;
    for (uint32_t t = 100000; t < 220000; t += 100)
    {
        int32_t const age = AgeOnArrival(sync, t, 40, client);
        worst = std::max(worst, age);
        best = std::min(best, age);
    }
    CHECK(worst < 400);
    CHECK(best > -400);     // never far in the future either
}

TEST(clock_forgets_after_a_pause)
{
    // A burst, a minute of silence while the clocks drift, another burst: the first shot after
    // the pause must not be judged by the stale offset.
    ClockSync sync;
    auto client = [](uint32_t s) { return uint32_t(double(s) * 0.94); };
    for (uint32_t t = 100000; t < 105000; t += 100)
        AgeOnArrival(sync, t, 40, client);
    CHECK(AgeOnArrival(sync, 165000, 40, client) < 400);
}

TEST(clock_ignores_one_late_packet)
{
    ClockSync sync;
    auto client = [](uint32_t s) { return s - 50000u; };
    for (uint32_t t = 100000; t < 101000; t += 100)
        AgeOnArrival(sync, t, 40, client);
    AgeOnArrival(sync, 101000, 900, client);    // a stall
    CHECK(AgeOnArrival(sync, 101100, 40, client) <= 45);
}

TEST(rate_limit_allows_a_burst_then_one_a_second)
{
    RateLimit limit(5, 1000);
    uint32_t const t = 4294967000u; // across the clock wrap
    for (int i = 0; i < 5; ++i)
        CHECK(limit.Allow(t));
    CHECK(!limit.Allow(t));             // the burst is spent
    CHECK(!limit.Allow(t + 999));
    CHECK(limit.Allow(t + 1000));       // one back per second
    CHECK(!limit.Allow(t + 1001));
    CHECK(limit.Allow(t + 60000));      // a long wait refills to the burst, no more
    for (int i = 0; i < 4; ++i)
        CHECK(limit.Allow(t + 60000));
    CHECK(!limit.Allow(t + 60000));
}
