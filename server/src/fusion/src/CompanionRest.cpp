/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/CompanionRest.h"

#include <algorithm>
#include <cmath>

namespace Fusion::Companions
{
    namespace
    {
        float Share(float h) { return std::isfinite(h) ? std::max(0.f, std::min(1.f, h)) : 1.f; }
    }

    FaintSettings FaintSettingsFrom(int32_t recoverMinutes, int32_t costPerLevel)
    {
        FaintSettings f;
        f.recoverSeconds = uint32_t(std::max(1, std::min(recoverMinutes, 7 * 24 * 60))) * 60u;
        f.costPerLevel = uint32_t(std::max(0, std::min(costPerLevel, 100000)));
        return f;
    }

    bool IsWhole(Recovery const& r)
    {
        return !r.faintedAt && Share(r.health) >= 1.f;
    }

    uint32_t FaintSecondsLeft(Recovery const& r, uint32_t now, uint32_t recoverSeconds)
    {
        if (!r.faintedAt)
            return 0;
        uint32_t const elapsed = now >= r.faintedAt ? now - r.faintedAt : 0;
        return elapsed >= recoverSeconds ? 0 : recoverSeconds - elapsed;
    }

    bool IsFainted(Recovery const& r, uint32_t now, uint32_t recoverSeconds)
    {
        return FaintSecondsLeft(r, now, recoverSeconds) > 0;
    }

    void Faint(Recovery& r, uint32_t now)
    {
        r.health = 0.f;
        r.awayAt = now;
        r.faintedAt = now ? now : 1;
    }

    void Revive(Recovery& r, uint32_t now, float health)
    {
        r.faintedAt = 0;
        r.health = Share(health);
        r.awayAt = now;
    }

    bool Settle(Recovery& r, uint32_t now, uint32_t recoverSeconds)
    {
        if (!r.faintedAt || IsFainted(r, now, recoverSeconds))
            return false;
        // Whole from the moment the wait ended.
        uint32_t const end = r.faintedAt + recoverSeconds;
        Revive(r, end >= r.faintedAt ? end : now, 1.f);
        return true;
    }

    void PutAway(Recovery& r, float health, uint32_t now)
    {
        if (r.faintedAt)
            return;
        r.health = Share(health);
        r.awayAt = now;
    }

    float HealthAt(Recovery const& r, uint32_t now, uint32_t recoverSeconds, uint32_t healMs)
    {
        if (IsFainted(r, now, recoverSeconds))
            return 0.f;
        if (r.faintedAt)
            return 1.f;     // its wait is over: whole
        uint32_t const rested = now >= r.awayAt ? now - r.awayAt : 0;
        // In milliseconds, capped well before a uint32 wraps (a day is whole anyway).
        uint32_t const restedMs = std::min<uint32_t>(rested, 86400u) * 1000u;
        return RestedHealth(r.health, restedMs, healMs);
    }

    uint32_t StableReviveCost(uint8_t level, uint32_t costPerLevel)
    {
        return uint32_t(std::max<uint8_t>(level, 1)) * costPerLevel;
    }

    std::string WaitText(uint32_t seconds)
    {
        if (seconds < 60)
            return std::to_string(std::max<uint32_t>(seconds, 1)) + " s";
        uint32_t const minutes = (seconds + 59) / 60;
        if (minutes < 60)
            return std::to_string(minutes) + " min";
        uint32_t const hours = minutes / 60;
        uint32_t const rest = minutes % 60;
        return std::to_string(hours) + " h" + (rest ? " " + std::to_string(rest) + " min" : std::string());
    }

    RecoveryRow ToRow(Recovery const& r)
    {
        RecoveryRow row;
        row.healthPermille = uint16_t(std::lround(Share(r.health) * 1000.f));
        row.awayAt = r.awayAt;
        row.faintedAt = r.faintedAt;
        return row;
    }

    Recovery FromRow(RecoveryRow const& row)
    {
        Recovery r;
        r.health = float(std::min<uint16_t>(row.healthPermille, 1000)) / 1000.f;
        r.awayAt = row.awayAt;
        r.faintedAt = row.faintedAt;
        return r;
    }

    float RestedHealth(float health, uint32_t restedMs, uint32_t healMs)
    {
        float const h = std::max(0.f, std::min(1.f, health));
        if (!healMs)
            return 1.f;
        return std::min(1.f, h + float(restedMs) / float(healMs));
    }

    void RestBook::Put(uint64_t item, Rest const& r)
    {
        Forget(item);
        m_entries.emplace_back(item, r);
        if (m_entries.size() > kMax)
            m_entries.erase(m_entries.begin(), m_entries.begin() + (m_entries.size() - kMax));
    }

    Rest RestBook::Take(uint64_t item, uint32_t nowMs) const
    {
        for (auto const& [id, r] : m_entries)
            if (id == item)
            {
                Rest out = r;
                out.health = RestedHealth(r.health, nowMs - r.awayMs);
                return out;
            }
        return Rest{};
    }

    void RestBook::Forget(uint64_t item)
    {
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
            [item](std::pair<uint64_t, Rest> const& e) { return e.first == item; }), m_entries.end());
    }
}
