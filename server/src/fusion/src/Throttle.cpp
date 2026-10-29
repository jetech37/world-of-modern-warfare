/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Throttle.h"
#include "fusion/Crafts.h"

#include "fusion/Achievements.h"
#include "fusion/ActionSystems.h"
#include "fusion/Bounties.h"
#include "fusion/Build.h"
#include "fusion/Classes.h"
#include "fusion/CompanionBattles.h"
#include "fusion/Construction.h"
#include "fusion/Furniture.h"
#include "fusion/Homestead.h"
#include "fusion/Locks.h"
#include "fusion/Match.h"
#include "fusion/Minstrelsy.h"
#include "fusion/Packets.h"
#include "fusion/Portals.h"
#include "fusion/Prestige.h"

namespace Fusion::Throttle
{
    bool Costly(uint8_t channel, std::vector<uint8_t> const& body)
    {
        if (body.empty())
            return false;
        uint8_t const op = body[0];
        if (channel == Packets::kChannelWorld)
            // The Construction window (asked, or an act refused with the window sent again) and
            // reinforcing a piece.
            return op == Homestead::kOpAsk || op == Homestead::kOpAct || op == Construction::kOpReinforce;
        if (channel == Construction::kChannelProfessions)
            // Kits made (bags searched, items taken, a write); Inscription's and Jewelcrafting's
            // windows, books, makes and the kits' glyphs and gems (Crafts.h, ops 20-31).
            return op == Construction::kOpCraftKit || (op >= Crafts::kOpAsk && op <= Crafts::kOpAugment);
        if (channel == Packets::kChannelAchievements)
            return true;                    // the whole book, every page
        if (channel == Prestige::kChannelPrestige)
            // All but the scoreboard (Tab held), which the match ops share the channel with.
            return !(body.size() >= 2 && body[0] == Prestige::kVersion && body[1] == Match::kOpScoreboard);
        if (channel == Bounty::kChannelBounty)
            return true;                    // the board rebuilt, a quest taken or turned in, a write
        if (channel == CompanionBattles::kChannel)
            return true;                    // a challenge, an answer, a battle's choice or its state
        if (channel == Classes::kChannelClasses)
            return op == Classes::kOpAskState;
        if (channel == ActionSystems::kChannel)
            // A minstrel's notes (each batch judged against the song's chart: a client sends a few
            // a second while it plays), a tip (a write) and the Minstrelsy window.
            return op == Minstrelsy::kOpSongHits || op == Minstrelsy::kOpTip || op == Minstrelsy::kOpAskWindow
                || op == Minstrelsy::kOpAct;
        if (channel == Packets::kChannelBuild)
            // A lock set (a stretched hash, a write, a broadcast), a portal's menu, a door swung
            // (a scan of the map's players, a broadcast) or a piece taken down (the support
            // solver over the pieces near).
            return op == Locks::kOpSetLock || op == Portals::kOpAsk
                || op == uint8_t(Build::ClientOp::Toggle) || op == uint8_t(Build::ClientOp::Remove)
                // A piece of furniture used (a page, a seat, the stash read) or the stash worked (items moved, a write).
                || op == Furniture::kOpUse || op == Furniture::kOpStash;
        return false;
    }
}
