/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * The channel messages that cost the server real work each time (a window rebuilt whole, a
 * database query or write, a scan of the map, a broadcast to everyone near): a client may send
 * them a few at once, then a handful a second, from one shared allowance (FusionPlayerData's
 * costlyAsks). Without it a modified client could send hundreds a server tick (the anti-flood
 * check allows 300) and stall its map and the character database. Gunfire, movement, spell aim
 * and the like are not among them: they have their own pace checks.
 */

#ifndef FUSION_THROTTLE_H
#define FUSION_THROTTLE_H

#include <cstdint>
#include <vector>

namespace Fusion::Throttle
{
    // The allowance: this many at once, then one more every kCostlyRefillMs (five a second).
    constexpr uint32_t kCostlyBurst = 10;
    constexpr uint32_t kCostlyRefillMs = 200;

    // Whether a client's channel message is one of the costly ones.
    bool Costly(uint8_t channel, std::vector<uint8_t> const& body);
}

#endif
