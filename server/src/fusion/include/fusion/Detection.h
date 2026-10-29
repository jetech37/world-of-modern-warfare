/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_DETECTION_H
#define FUSION_DETECTION_H

#include "fusion/Common.h"
#include "fusion/Movement.h"

namespace Fusion
{
    // 1.12's level-based aggro radius: 20 yards at equal level, a yard more per level the
    // creature is above the player and less below, clamped to [5, 45].
    float WowAggroRadius(uint32_t creatureLevel, uint32_t playerLevel);

    struct Observer
    {
        uint32_t level = 1;
        CreatureType type = CreatureType::Humanoid;
        Vec3 position;
        Vec3 facing{1.f, 0.f, 0.f};     // unit vector the creature looks along
    };

    struct Intruder
    {
        uint32_t level = 1;
        Vec3 position;
        Stance stance = Stance::Stand;
        Gait gait = Gait::Run;
        bool ninja = false;             // the perk
    };

    // The radius inside which a creature notices a player, from WoW's level radius times the
    // player's stance, motion, the facing and perks (design doc, section 15).
    float DetectionRadius(Observer const& o, Intruder const& p);

    // Inside the radius a creature doesn't aggro at once: an alert meter fills over about a
    // second while it turns to look, and drains when the player leaves.
    class AlertMeter
    {
    public:
        static constexpr float kFillSeconds = 1.f;
        static constexpr float kDrainSeconds = 2.f;

        // Advance by dt; returns true on the update the creature becomes hostile.
        bool Update(float dt, bool insideRadius);
        float Level() const { return m_level; }
        bool Alerted() const { return m_level > 0.f; }
        void Reset() { m_level = 0.f; m_fired = false; }

    private:
        float m_level = 0.f;
        bool m_fired = false;
    };

    // Unsilenced gunfire and loud spells pull creatures within this radius regardless of the
    // detection radius (design doc, section 11).
    constexpr float kLoudNoiseRadius = 25.f;
}

#endif
