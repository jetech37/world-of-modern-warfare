/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Access.h"

namespace Fusion
{
    uint32_t EffectiveLevel(uint32_t level, bool gm)
    {
        return gm ? 60u : level;
    }

    bool CanUseWeapon(uint32_t level, bool gm, WeaponProfile const& w)
    {
        return w.starter || EffectiveLevel(level, gm) >= UnlockLevel(w.mw2UnlockRank);
    }

    bool CanUseAmmo(uint32_t level, bool gm, AmmoType t)
    {
        return EffectiveLevel(level, gm) >= AmmoUnlockLevel(t);
    }
}
