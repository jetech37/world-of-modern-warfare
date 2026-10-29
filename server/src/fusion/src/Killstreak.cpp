/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Killstreak.h"

#include <algorithm>
#include <cmath>

namespace Fusion
{
    uint32_t StreakTenths(KillKind k, uint32_t damagers)
    {
        switch (k)
        {
            case KillKind::Player:        return 10;
            case KillKind::Creature:      return 1;
            case KillKind::EliteCreature:
                if (damagers <= 1)
                    return 10;
                return std::max<uint32_t>(1, (10 + damagers / 2) / damagers);
            case KillKind::GrayCreature:  return 0;
        }
        return 0;
    }

    std::vector<StreakReward> DefaultStreakLadder()
    {
        return {
            {StreakId::Uav, 3, false},
            {StreakId::SentryGun, 5, false},
            {StreakId::PredatorMissile, 5, true},
            {StreakId::HarrierStrike, 7, true},
            {StreakId::AttackHelicopter, 7, true},
            {StreakId::StealthBomber, 9, true},
            {StreakId::ChopperGunner, 11, true},
            {StreakId::TacticalNuke, 25, false},
        };
    }

    void Streak::SetLoadout(std::vector<StreakReward> rewards, bool hardline)
    {
        m_rewards = std::move(rewards);
        m_hardline = hardline;
    }

    uint32_t PlayerKillTenths(uint32_t killerLevel, uint32_t victimLevel)
    {
        if (victimLevel >= killerLevel)
            return 10;
        uint32_t const below = killerLevel - victimLevel;
        return below >= 9 ? 1 : 10 - below;
    }

    std::vector<StreakReward> Streak::AddKill(KillKind k, uint32_t damagers)
    {
        return AddTenths(StreakTenths(k, damagers));
    }

    std::vector<StreakReward> Streak::AddTenths(uint32_t value)
    {
        std::vector<StreakReward> earned;
        if (value == 0)
            return earned;
        m_tenths += value;
        for (auto const& r : m_rewards)
        {
            uint32_t need = r.kills;
            if (m_hardline && need > 1)
                --need;
            need *= 10;
            // Reached (a fraction can step past a threshold) and not paid this life.
            bool const paid = std::find(m_paid.begin(), m_paid.end(), r.id) != m_paid.end();
            if (!paid && m_tenths >= need)
            {
                earned.push_back(r);
                m_earned.push_back(r);
                m_paid.push_back(r.id);
            }
        }
        return earned;
    }

    bool Streak::Use(uint32_t rewardId)
    {
        for (auto it = m_earned.begin(); it != m_earned.end(); ++it)
        {
            if (it->id == rewardId)
            {
                m_earned.erase(it);
                return true;
            }
        }
        return false;
    }

    void DamageCredit::Add(uint64_t victim, uint64_t attacker, uint32_t nowMs)
    {
        Entry& e = m_victims[victim];
        e.lastMs = nowMs;
        if (std::find(e.attackers.begin(), e.attackers.end(), attacker) == e.attackers.end())
            e.attackers.push_back(attacker);
    }

    std::vector<uint64_t> DamageCredit::Take(uint64_t victim)
    {
        auto it = m_victims.find(victim);
        if (it == m_victims.end())
            return {};
        std::vector<uint64_t> out = std::move(it->second.attackers);
        m_victims.erase(it);
        return out;
    }

    void DamageCredit::Expire(uint32_t nowMs)
    {
        for (auto it = m_victims.begin(); it != m_victims.end();)
        {
            // Unsigned difference: correct across the millisecond clock's wrap.
            if (nowMs - it->second.lastMs > kForgetMs)
                it = m_victims.erase(it);
            else
                ++it;
        }
    }

    Airspace::Cell Airspace::CellAt(uint32_t map, float x, float y)
    {
        return Cell{int32_t(std::floor(x / kCellSize)), int32_t(std::floor(y / kCellSize)), map};
    }

    std::optional<uint64_t> Airspace::TryLaunch(uint32_t map, float x, float y)
    {
        Cell c = CellAt(map, x, y);
        auto& list = m_active[c];
        if (list.size() >= kMaxPerCell)
            return std::nullopt;
        uint64_t ticket = m_next++;
        list.push_back(ticket);
        m_tickets[ticket] = c;
        return ticket;
    }

    void Airspace::Release(uint64_t ticket)
    {
        auto t = m_tickets.find(ticket);
        if (t == m_tickets.end())
            return;
        auto& list = m_active[t->second];
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            if (*it == ticket)
            {
                list.erase(it);
                break;
            }
        }
        m_tickets.erase(t);
    }

    uint32_t Airspace::ActiveIn(uint32_t map, float x, float y) const
    {
        auto it = m_active.find(CellAt(map, x, y));
        return it == m_active.end() ? 0 : uint32_t(it->second.size());
    }
}
