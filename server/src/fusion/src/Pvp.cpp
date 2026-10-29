/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Pvp.h"

namespace Fusion
{
    namespace
    {
        bool SameGroup(PvpPlayer const& a, PvpPlayer const& b)
        {
            return a.groupId != 0 && a.groupId == b.groupId;
        }
    }

    bool CanAttack(PvpPlayer const& attacker, PvpPlayer const& victim, bool victimStartedIt)
    {
        if (attacker.guid == victim.guid)
            return false;
        if (attacker.inSanctuary || victim.inSanctuary)
            return false;
        if (SameGroup(attacker, victim))
            return false;

        bool enemies = attacker.team != victim.team;
        if (!enemies && !(attacker.ffaFlag && victim.ffaFlag))
            return false;

        if (attacker.level >= victim.level + kLowLevelProtection && !victimStartedIt)
            return false;
        return true;
    }

    bool BodiesCollide(PvpPlayer const& a, PvpPlayer const& b)
    {
        if (a.guid == b.guid)
            return false;
        if (a.inSanctuary || b.inSanctuary)
            return false;
        if (SameGroup(a, b))
            return false;
        if (a.team == b.team && !(a.ffaFlag && b.ffaFlag))
            return false;
        return true;
    }

    void FfaFlag::Update(float dt, bool inCombat)
    {
        if (!m_pendingOff)
            return;
        if (inCombat)
        {
            m_timer = 0.f;
            return;
        }
        m_timer += dt;
        if (m_timer >= kOffDelaySeconds)
        {
            m_on = false;
            m_pendingOff = false;
            m_timer = 0.f;
        }
    }
}
