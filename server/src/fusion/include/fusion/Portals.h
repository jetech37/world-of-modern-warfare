/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PORTALS_H
#define FUSION_PORTALS_H

#include "fusion/Build.h"
#include "fusion/Common.h"

#include <cstdint>
#include <string>
#include <vector>

// Portals and the hearthstone's cooldown. A portal (Build.h kPortalProp) is an Artisan builder's
// piece: a mage's portal that opens on the user's own faction's capitals (Stormwind, Ironforge
// and Darnassus; Orgrimmar, Undercity and Thunder Bluff), never the enemy's, and on the user's
// own homestead when they hold one. It is expensive (Construction.cpp), a player keeps at most
// kMaxPerPlayer and a plot holds at most kMaxPerPlot. Who may step through is its lock's say, as
// a door's (Locks.h).
//
// Travel of this kind shares one clock: the hearthstone's cooldown (spell 8690 cast from item
// 6948, an hour). Using a portal or the homestead courier's "Take me back" starts it, as using the
// hearthstone does, and none of them works while it runs. The server keeps the cooldown in the
// player's own spell cooldowns, so it outlives a logout like the hearthstone's.
namespace Fusion { namespace Portals
{
    constexpr uint32_t kHearthSpell = 8690;
    constexpr uint32_t kHearthItem = 6948;
    constexpr uint32_t kHearthCooldownMs = 3600 * 1000;

    constexpr uint32_t kMaxPerPlayer = 2;
    constexpr uint32_t kMaxPerPlot = 1;
    // How near a player must stand to use one (to its nearest point).
    constexpr float kUseReach = 5.f;

    // Fusion::Team (Common.h): a Neutral user sees no capital.

    // A place a portal opens on. A capital's spot is where the mages' own teleport lands
    // (spell_target_position of `spell`, read by the server; the spot here when that's missing).
    struct Destination
    {
        uint8_t id = 0;
        char const* name = "";
        Team team = Team::Alliance;
        uint32_t spell = 0;         // the mage teleport whose landing it shares
        uint32_t map = 0;
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;
    };
    // Every capital, by id 1-6.
    std::vector<Destination> const& Capitals();
    Destination const* CapitalById(uint8_t id);
    // The homestead: the user's own plot (its arrival spot is the server's to work out).
    constexpr uint8_t kHomestead = 100;

    // What a user of `team` sees in the portal's list: their capitals, and their homestead when
    // they hold one. (id, name) in the order shown.
    struct Choice
    {
        uint8_t id = 0;
        std::string name;
    };
    std::vector<Choice> ChoicesFor(Team team, bool hasHomestead);
    // Whether a user may go to `id` through a portal: one of their faction's capitals, or their
    // own homestead if they have one. Never an enemy capital.
    bool Allowed(Team team, uint8_t id, bool hasHomestead);

    // Whether a player may travel now (a portal, the courier's way back): the shared cooldown,
    // and the usual: alive, out of a fight, not on a flight or a boat.
    enum class TravelCheck : uint8_t { Ok, CoolingDown, Dead, InCombat, Travelling };
    struct Traveller
    {
        bool alive = true;
        bool inCombat = false;
        bool travelling = false;
        uint32_t cooldownLeftMs = 0;    // the hearthstone's
    };
    TravelCheck CanTravel(Traveller const& t);
    // "58 min", "3 min", "under a minute".
    std::string WaitText(uint32_t ms);

    // Whether another portal may go up: the builder's own count, and the plot's when it is on one.
    // A game master is not held to them.
    bool MayPlace(uint32_t ownedByBuilder, bool onPlot, uint32_t onThatPlot, bool gm);

    // ── The wire: the build channel (Packets kChannelBuild) ──
    // Client: [6, piece u32]: open a portal's list (F on it).
    //         [7, piece u32, destination u8]: step through.
    // Server: [20, piece u32, cooldown left u32 s, count u8, each: id u8, name length u8, name]:
    //          the list for this player.
    constexpr uint8_t kOpAsk = 6;
    constexpr uint8_t kOpTravel = 7;
    constexpr uint8_t kOpMenu = 20;
    bool DecodeAsk(std::vector<uint8_t> const& body, uint32_t& piece);
    std::vector<uint8_t> EncodeAsk(uint32_t piece);
    bool DecodeTravel(std::vector<uint8_t> const& body, uint32_t& piece, uint8_t& destination);
    std::vector<uint8_t> EncodeTravel(uint32_t piece, uint8_t destination);
    struct Menu
    {
        uint32_t piece = 0;
        uint32_t cooldownSeconds = 0;
        std::vector<Choice> choices;
    };
    std::vector<uint8_t> EncodeMenu(Menu const& m);
    bool DecodeMenu(std::vector<uint8_t> const& body, Menu& out);
}}

#endif
