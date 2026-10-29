/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PVP_H
#define FUSION_PVP_H

#include "fusion/Common.h"

namespace Fusion
{
    // A player as the PvP rules see them.
    struct PvpPlayer
    {
        uint64_t guid = 0;
        Team team = Team::Alliance;
        uint32_t level = 1;
        uint32_t groupId = 0;           // 0 = not in a group
        bool ffaFlag = false;           // the personal free-for-all flag (effective state)
        bool inSanctuary = false;       // capitals and inns
    };

    // Low-level protection: no damage to players this many levels below unless they attacked first.
    constexpr uint32_t kLowLevelProtection = 10;

    // Whether attacker may damage victim (design doc, section 10): the factions fight; two
    // players of one faction fight only when both carry the free-for-all flag; never inside a
    // group; never in a sanctuary; never far below your level unless the victim started the
    // fight (victimStartedIt).
    bool CanAttack(PvpPlayer const& attacker, PvpPlayer const& victim, bool victimStartedIt = false);

    // Whether two bodies collide (design doc, section 17): enemies collide so they can't stack;
    // a faction and a group pass through each other, so no one can block a doorway; nothing
    // collides in a sanctuary.
    bool BodiesCollide(PvpPlayer const& a, PvpPlayer const& b);

    // The personal free-for-all flag: on at once; off only after 5 minutes out of combat, so
    // nobody escapes a fight by toggling (design doc, D51b).
    class FfaFlag
    {
    public:
        static constexpr float kOffDelaySeconds = 300.f;

        void RequestOn() { m_on = true; m_pendingOff = false; m_timer = 0.f; }
        void RequestOff() { if (m_on) { m_pendingOff = true; m_timer = 0.f; } }
        // Combat restarts the countdown.
        void Update(float dt, bool inCombat);
        bool On() const { return m_on; }
        bool PendingOff() const { return m_pendingOff; }
        float SecondsUntilOff() const { return m_pendingOff ? kOffDelaySeconds - m_timer : 0.f; }

    private:
        bool m_on = false;
        bool m_pendingOff = false;
        float m_timer = 0.f;
    };
}

#endif
