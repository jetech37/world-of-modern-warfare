/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_DEFENSE_H
#define FUSION_DEFENSE_H

#include "fusion/Common.h"

#include <cstdint>
#include <optional>
#include <vector>

// Holding what you build: sentry guns that shoot what comes for it, and raids, after Valheim's,
// that come for a base: a wave of the hostile humanoids of the player's level, gunners and
// brawlers both, who shoot down or break through what is in their way.
namespace Fusion { namespace Defense
{
    // ── Sentry guns ──
    constexpr float kSentryRange = 35.f;
    // A sentry works only while its builder is this near (it is theirs to man).
    constexpr float kSentryOwnerRange = 80.f;
    // MW2's sentry: an M240's round, in bursts.
    constexpr float kSentryDamage = 20.f;
    constexpr uint32_t kSentryBurst = 6;
    constexpr uint32_t kSentryGapMs = 110;
    constexpr uint32_t kSentryPauseMs = 700;

    // The Sentry Gun killstreak (Killstreak.h; construction no longer builds one): MW2's, set
    // down kStreakSentryAhead yards in front of the player, facing their way, for kStreakSentryMs.
    // It is a sentry piece like the old built one (it turns and fires the same), never saved, and
    // it shoots enemy players the player may fight as well as hostile creatures.
    constexpr uint32_t kStreakSentryMs = 90u * 1000u;
    constexpr float kStreakSentryAhead = 2.f;
    Vec3 StreakSentrySpot(Vec3 feet, float facing);

    struct Candidate
    {
        uint64_t guid = 0;
        Vec3 position;
        bool hostile = false;       // hostile to the builder and fair game
        bool alive = true;
        bool inCombat = false;      // already fighting (the builder or anyone)
    };

    // MW2's sentry turns only so far: within kSentryYawArc either side of the way it was set
    // down facing, and kSentryPitchArc up or down (radians).
    constexpr float kSentryYawArc = 80.f * 3.14159265f / 180.f;
    constexpr float kSentryPitchArc = 40.f * 3.14159265f / 180.f;

    // Whether a sentry at `at` facing `facing` (yaw, radians) can turn on `to`.
    bool SentryCovers(Vec3 at, float facing, Vec3 to);

    // Who a sentry at `at` facing `facing` shoots: the nearest living hostile in range, in its
    // arc and in sight, those already fighting first.
    template <typename Sight>
    std::optional<uint64_t> SentryTarget(Vec3 at, float facing, std::vector<Candidate> const& candidates, Sight sight)
    {
        std::optional<uint64_t> best;
        float bestScore = 1e30f;
        for (Candidate const& c : candidates)
        {
            if (!c.alive || !c.hostile)
                continue;
            float const d = (c.position - at).Length();
            if (d > kSentryRange || !SentryCovers(at, facing, c.position) || !sight(c.position))
                continue;
            float const score = d - (c.inCombat ? 1000.f : 0.f);
            if (score < bestScore)
            {
                bestScore = score;
                best = c.guid;
            }
        }
        return best;
    }

    // ── Raids ──
    // A base: this many of the player's pieces within kBaseRadius of them.
    constexpr uint32_t kBasePieces = 8;
    constexpr float kBaseRadius = 40.f;
    // The first raid comes no sooner than this after a base stands; then every 20 to 35 minutes.
    constexpr uint32_t kFirstRaidMs = 8u * 60u * 1000u;
    constexpr uint32_t kRaidMinMs = 20u * 60u * 1000u;
    constexpr uint32_t kRaidMaxMs = 35u * 60u * 1000u;
    // The warning before a wave, and how long a raid lasts before the rest give up.
    constexpr uint32_t kRaidWarningMs = 20u * 1000u;
    constexpr uint32_t kRaidLengthMs = 5u * 60u * 1000u;
    // Where the wave comes from: this far from the base.
    constexpr float kRaidSpawnDistance = 55.f;

    bool BaseQualifies(uint32_t piecesNear, uint32_t playerLevel);
    // How many come: more for a bigger base, 3 to 8.
    uint32_t RaidSize(uint32_t piecesNear);
    // The wait to the next raid, `roll` in [0, 1).
    uint32_t NextRaidDelayMs(float roll);

    enum class RaidPhase : uint8_t { Quiet, Warned, Fighting };

    // A player's raid clock.
    struct RaidClock
    {
        RaidPhase phase = RaidPhase::Quiet;
        uint32_t nextMs = 0;        // the next change (warning due, wave due, raid over)
        bool armed = false;         // a base stands and the first wait has begun
    };

    // What the clock asks for now.
    enum class RaidStep : uint8_t { None, Warn, Spawn, End };
    // Advance the clock: `hasBase` whether the player's base qualifies (and they may be raided
    // now: alive, out of a city, not flying), `waveLeft` the raiders still alive.
    RaidStep Advance(RaidClock& clock, uint32_t nowMs, bool hasBase, uint32_t waveLeft, float roll);
}}

#endif
