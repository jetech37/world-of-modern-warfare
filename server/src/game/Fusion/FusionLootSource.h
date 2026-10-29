/*
 * World of Modern Warfare: which corpse (or chest) a loot item is being stored from, while it is
 * stored. A companion item rolls its traits when it is made, at the rarity of the creature that
 * dropped it (FusionMgr::RollFreshCompanion); the item itself does not know its dropper, and the
 * looter's open loot window is not it when the item comes from another corpse of the area-loot
 * window, a group roll or the master looter. Set around those stores (LootHandler.cpp,
 * Group.cpp); nested scopes restore the outer one.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef FUSION_LOOT_SOURCE_H
#define FUSION_LOOT_SOURCE_H

#include "ObjectGuid.h"

class FusionLootSource
{
public:
    explicit FusionLootSource(ObjectGuid source) : m_previous(Slot()) { Slot() = source; }
    ~FusionLootSource() { Slot() = m_previous; }
    FusionLootSource(FusionLootSource const&) = delete;
    FusionLootSource& operator=(FusionLootSource const&) = delete;

    // The source being stored from on this thread now (empty when none).
    static ObjectGuid Current() { return Slot(); }

private:
    static ObjectGuid& Slot()
    {
        static thread_local ObjectGuid current;
        return current;
    }

    ObjectGuid m_previous;
};

#endif
