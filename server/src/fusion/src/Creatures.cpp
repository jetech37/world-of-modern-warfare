/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Creatures.h"

namespace Fusion
{
    Profile ClassifyCreature(CreatureTraits const& t)
    {
        switch (t.type)
        {
            case CreatureType::Critter:
            case CreatureType::Totem:
                return Profile::Passive;
            case CreatureType::Mechanical:
                return Profile::Machine;
            case CreatureType::Undead:
                return t.caster ? Profile::Caster : Profile::Horde;
            case CreatureType::Beast:
                return t.flying ? Profile::Flyer : (t.large ? Profile::Juggernaut : Profile::Rusher);
            case CreatureType::Giant:
            case CreatureType::Elemental:
                return t.caster ? Profile::Caster : Profile::Juggernaut;
            case CreatureType::Dragonkin:
                return t.flying ? Profile::Flyer : Profile::Juggernaut;
            case CreatureType::Demon:
                return t.caster ? Profile::Caster : (t.large ? Profile::Juggernaut : Profile::Rusher);
            case CreatureType::Humanoid:
            case CreatureType::NotSpecified:
            case CreatureType::None:
                if (t.caster)
                    return Profile::Caster;
                if (t.large)
                    return Profile::Juggernaut;
                return Profile::Soldier;
        }
        return Profile::Soldier;
    }

    float ChaseSpeedMultiplier(Profile p, float distance, float meleeReach, bool fastNow)
    {
        switch (p)
        {
            case Profile::Passive:
                return 1.f;
            case Profile::Caster:
                return ChaseSpeed(distance, kGunfight.casterRange, fastNow);
            default:
                return ChaseSpeed(distance, meleeReach, fastNow);
        }
    }

    void ReachTracker::Update(float dt, bool canReach)
    {
        if (canReach)
            m_out = 0.f;
        else
            m_out += dt;
    }
}
