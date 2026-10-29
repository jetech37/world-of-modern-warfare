/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Detection.h"

namespace Fusion
{
    float WowAggroRadius(uint32_t creatureLevel, uint32_t playerLevel)
    {
        float r = 20.f + float(int(creatureLevel) - int(playerLevel));
        return Clamp(r, 5.f, 45.f);
    }

    float DetectionRadius(Observer const& o, Intruder const& p)
    {
        float stance = 1.f;
        switch (p.stance)
        {
            case Stance::Stand:  stance = 1.f; break;
            case Stance::Crouch:
            case Stance::Slide:  stance = 0.6f; break;
            case Stance::Prone:  stance = 0.3f; break;
        }

        float motion = 1.f;
        switch (p.gait)
        {
            case Gait::Still:     motion = 0.7f; break;
            case Gait::Walk:      motion = 0.85f; break;
            case Gait::Backpedal:
            case Gait::Run:       motion = 1.f; break;
            case Gait::Sprint:    motion = 1.3f; break;
        }

        // Behind the creature it notices half as far; beasts smell, so only a little less.
        Vec3 toPlayer = (p.position - o.position).Normalized();
        bool inFront = toPlayer.Dot(o.facing.Normalized()) >= 0.f;
        float facing = 1.f;
        if (!inFront)
            facing = o.type == CreatureType::Beast ? 0.8f : 0.5f;

        float perks = p.ninja ? 0.7f : 1.f;
        return WowAggroRadius(o.level, p.level) * stance * motion * facing * perks;
    }

    bool AlertMeter::Update(float dt, bool insideRadius)
    {
        if (insideRadius)
            m_level = std::min(1.f, m_level + dt / kFillSeconds);
        else
            m_level = std::max(0.f, m_level - dt / kDrainSeconds);
        if (m_level >= 1.f && !m_fired)
        {
            m_fired = true;
            return true;
        }
        if (m_level <= 0.f)
            m_fired = false;
        return false;
    }
}
