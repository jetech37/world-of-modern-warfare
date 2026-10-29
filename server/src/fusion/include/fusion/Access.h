/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_ACCESS_H
#define FUSION_ACCESS_H

#include "fusion/Damage.h"
#include "fusion/Weapon.h"

#include <cstdint>

namespace Fusion
{
    // The one unlock gate every grant goes through (design spec, section 1.2): a game master passes
    // everything; anyone else unlocks by level, MW2's rank scaled to 60 (UnlockLevel).

    // The level unlocks are judged at: a game master counts as level 60.
    uint32_t EffectiveLevel(uint32_t level, bool gm);

    bool CanUseWeapon(uint32_t level, bool gm, WeaponProfile const& w);
    bool CanUseAmmo(uint32_t level, bool gm, AmmoType t);
}

#endif
