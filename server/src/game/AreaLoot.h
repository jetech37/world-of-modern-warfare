/*
 * World of Modern Warfare: area looting (fusion/AreaLoot.h) in the game.
 *
 * Out of combat, a player who opens a creature corpse's loot sees in the one window the loot of
 * every nearby corpse they may loot (Loot.AreaLoot.Enable, Loot.AreaLoot.Radius). In combat, or
 * with master looting, a corpse loots alone. The window's rows and the Session that maps them back
 * to their corpses are Fusion::AreaLoot's; this is the game side: building the window, and taking
 * an item, the coin, or letting the corpses go. Each take re-checks the corpse it comes from (still
 * there, dead, near, this player's to loot, the same item in the same slot) before anything moves.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef MANGOS_AREALOOT_H
#define MANGOS_AREALOOT_H

#include "Common.h"
#include "LootMgr.h"
#include "ObjectGuid.h"

class Creature;
class Player;

namespace WorldPackets
{
    namespace Loot
    {
        class LootResponse;
    }
}

namespace AreaLooting
{
    // Player::SendLoot, for a CMSG_LOOT on a creature's body loot (primary), the player's permission
    // on it already known and its rolls started. When area looting applies and another corpse adds
    // something, writes the combined window into packet, opens the player's Session and returns
    // true; otherwise false and the caller writes the corpse's own window.
    bool WriteWindow(Player* player, Creature* primary, PermissionTypes permission, WorldPackets::Loot::LootResponse& packet);

    // CMSG_AUTOSTORE_LOOT_ITEM on an area-loot window.
    void TakeItem(Player* player, uint8 windowSlot);

    // CMSG_LOOT_MONEY on an area-loot window: every corpse's coin that is still the player's.
    void TakeMoney(Player* player);

    // One corpse of the window (not the opened one) is done with, e.g. a won roll emptied it: it
    // leaves the window, whose other rows stay.
    void ReleaseSource(Player* player, ObjectGuid source);

    // The window closes: every corpse but the opened one is let go (the client never knew them),
    // and the Session ends. The opened corpse is the caller's (DoLootRelease).
    void EndSession(Player* player);
}

#endif
