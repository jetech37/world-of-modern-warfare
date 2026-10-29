/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_HOMESTEAD_H
#define FUSION_HOMESTEAD_H

#include "fusion/Common.h"
#include "fusion/Plots.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The Construction window (the client's Panel::Construction): construction and the homestead in
// one classic-looking window instead of gossip menus and chat lines. Opened from the HUD it shows
// the player's Construction, their plot and the build catalogue; opened by a homestead NPC (the
// Plot Steward, the plot coordinator; the courier; the Construction trainer) it also lists what
// that NPC does, each a button with its price: claim a plot, release it, go to it, pay upkeep,
// permits, travel, training and the smiths' parts. Every result is said in the window, not in
// chat. The server builds the list (it knows the prices, what is free and what the player may
// do); the client draws it and sends back the button pressed.
//
// The plots near the player also go to the client, so it can mark them in the world: the
// player's own plot clearly (banners at the corners, posts along the edges, a faint line on the
// ground), others' claimed plots with plain corner posts.
namespace Fusion { namespace Homestead
{
    // ── The wire: the world channel (Packets kChannelWorld), Construction's ops 50-59 ──
    // Client: [52]: the window as it stands (opened from the HUD).
    // Client: [53, action u8, arg u32]: a button pressed.
    // Server: [54, ...]: the window (EncodeWindow).
    // Server: [55, count u8, each: plot id u32, x0 y0 x1 y1 f32, relation u8]: the claimed plots
    //         near the player (sent when they change; empty when there are none).
    constexpr uint8_t kOpAsk = 52;
    constexpr uint8_t kOpAct = 53;
    constexpr uint8_t kOpWindow = 54;
    constexpr uint8_t kOpNearPlots = 55;

    // Who opened the window: no one (the HUD), or one of the homestead NPCs.
    enum class Npc : uint8_t { None = 0, Steward = 1, Courier = 2, Trainer = 3 };
    // What the client does with a window message: show it if open, open it with HUD access, or
    // open it with an NPC's access (the client writes OpenPanel{Construction, Npc}).
    enum class Open : uint8_t { Refresh = 0, Hud = 1, Npc = 2 };
    // How the status line reads: nothing, a plain note, done (green), or an error (red).
    enum class Tone : uint8_t { None = 0, Info = 1, Done = 2, Error = 3 };

    enum class Action : uint8_t
    {
        None = 0,
        Claim = 1,          // arg: the district's index (its first free plot)
        ClaimVisited = 2,   // arg: the plot id (the one the player last walked on)
        Release = 3,
        Teleport = 4,       // to the player's own plot
        Pay = 5,            // arg: weeks (0: as far ahead as allowed)
        Collect = 6,        // what a packed plot left the player
        Train = 7,          // the next Construction tier
        Craft = 8,          // arg: the part's index
        Travel = 9,         // arg: kTravelBack, kTravelMine, or site index * kStopsPerSite + stop
        Permit = 10,        // arg: the level (0 revoke) for the player's target
        Revoke = 11,        // arg: the character
        BuyDecor = 12,      // arg: a furniture piece's type: a kit of it (Furniture.h priceCopper)
    };
    constexpr uint32_t kTravelBack = 0xFFFFFFFFu;
    constexpr uint32_t kTravelMine = 0xFFFFFFFEu;
    constexpr uint32_t kStopsPerSite = 16;

    // The NPC an action needs (Npc::None: none, the HUD may do it).
    Npc NeededFor(Action a);
    // Whether `a` may be done with the window opened by `at`.
    bool AllowedAt(Action a, Npc at);

    // A button: its action and argument, its price in copper (0: free), whether it may be pressed
    // now (else `note` says why), whether it asks first, the line's colour (a recipe's, Hue), and
    // two lines of text.
    enum class Hue : uint8_t { Normal = 0, Orange = 1, Yellow = 2, Green = 3, Gray = 4, Red = 5 };
    struct Service
    {
        Action action = Action::None;
        uint32_t arg = 0;
        uint32_t cost = 0;
        bool enabled = true;
        bool confirm = false;
        Hue hue = Hue::Normal;
        std::string label;
        std::string note;
    };

    // The player's plot as the window shows it.
    struct PlotView
    {
        uint32_t id = 0;                // 0: none
        uint8_t size = 0;               // Plots::Size
        uint8_t standing = 0;           // Plots::Standing
        uint16_t map = 0;
        Plots::Rect bounds;
        uint32_t paidUntil = 0;         // unix seconds
        uint32_t upkeep = 0;            // copper a week
        uint8_t permits = 0;
        uint16_t pieces = 0;            // pieces standing on it
        std::string place;              // "in Azshara Crater"
    };

    struct Window
    {
        Open open = Open::Refresh;
        Npc npc = Npc::None;
        std::string npcName;
        Tone tone = Tone::None;
        std::string message;
        PlotView plot;
        uint32_t reclaim = 0;           // items packed and waiting at a steward
        std::vector<Service> services;
    };

    // [54, open u8, npc u8, npc name str8, tone u8, message str8,
    //  plot id u32, size u8, standing u8, map u16, x0 y0 x1 y1 f32, paid until u32, upkeep u32,
    //  permits u8, pieces u16, place str8, reclaim u32,
    //  services u8, each: action u8, arg u32, cost u32, flags u8 (1 enabled, 2 confirm), hue u8,
    //  label str8, note str8]. str8: a length byte and that many bytes (cut at 255). At most
    // kMaxServices services; the body stays under the channel's limit.
    constexpr size_t kMaxServices = 48;
    std::vector<uint8_t> EncodeWindow(Window const& w);
    bool DecodeWindow(std::vector<uint8_t> const& body, Window& out);

    std::vector<uint8_t> EncodeAct(Action a, uint32_t arg);
    bool DecodeAct(std::vector<uint8_t> const& body, Action& a, uint32_t& arg);

    // "1g 20s", "50s", "0c": as the steward's prices read.
    std::string MoneyText(uint32_t copper);

    // ── Claiming: one plot to a player ──
    // The first plot of district `index` nobody holds and not in `busy` (plots given up whose
    // pieces are still being packed) (0: none, or no such district).
    uint32_t FirstFree(Plots::Grid const& g, Plots::Registry const& r, size_t index, std::vector<uint32_t> const& busy = {});
    // Claim the first free plot of district `index` (only of site `site` when it is not 0).
    // The one-plot limit is checked before anything else: someone who holds a plot hears so,
    // never that the district is full. On Ok `plotId` is the plot and `charge` its price.
    Plots::BuyResult ClaimInDistrict(Plots::Grid const& g, Plots::Registry& r, size_t index, uint8_t site,
        Plots::Buyer const& b, int64_t now, uint32_t& charge, uint32_t& plotId, std::vector<uint32_t> const& busy = {});
    // A plot by id (the one last walked on), the limit first as well.
    Plots::BuyResult ClaimPlot(Plots::Grid const& g, Plots::Registry& r, uint32_t plotId, Plots::Buyer const& b,
        int64_t now, uint32_t& charge);
    // Plots read from the database beyond one to an account (rows left from before the limit,
    // or written by hand): every one but the earliest bought of each account, by id. The caller
    // packs them as abandoned, so an account never holds two.
    std::vector<uint32_t> Excess(Plots::Registry const& r);

    // ── Going there ──
    // Where the plot coordinator sets a player down on their plot: the middle of the plot's
    // edge nearest its site's gate, inside the edge by half the build margin (no piece stands
    // there, so nobody lands in a wall), facing into the plot. z is the gate's; the server finds
    // the ground.
    constexpr float kArrivalInset = Plots::kBuildMargin * 0.5f;
    Plots::Spot Arrival(Plots::Grid const& g, uint32_t plotId);

    // ── What each NPC offers ──
    struct StewardAsk
    {
        uint32_t account = 0;
        uint32_t guid = 0;
        uint32_t money = 0;             // copper
        bool gm = false;
        uint8_t site = 0;               // the steward's site; 0 sells every site's plots
        uint32_t visited = 0;           // the plot the player last walked on
        uint32_t reclaim = 0;           // items packed and waiting
        uint32_t target = 0;            // the player's target, another player (0: none)
        std::string targetName;
        std::function<std::string(uint32_t)> nameOf;   // a character's name, for the permits
    };
    // A player without a plot: a Claim a district of the steward's site (with its price, its
    // upkeep and how many are free; greyed when none are or the gold is short), and the plot last
    // walked on if it's for sale. A player with one: go to it, pay upkeep (a week, four, as far as
    // allowed), permits for the target and revoking each, and release it (asked first). Collect
    // when something is packed and waiting.
    std::vector<Service> StewardServices(Plots::Grid const& g, Plots::Registry const& r, StewardAsk const& ask, int64_t now);

    struct CourierAsk
    {
        uint32_t account = 0;
        uint32_t map = 0;
        float x = 0.f, y = 0.f;         // the courier
        bool canGoBack = false;         // the player came from outside every site
        // The hearthstone's cooldown left (Portals.h): "Take me back" shares it, and waits on it.
        uint32_t backCooldownMs = 0;
    };
    // In a site: back where the player came from, and the site's other stops (not the one the
    // courier stands at). Anywhere: the player's own homestead and every other site's gate.
    constexpr float kHomesteadsNear = 300.f;
    constexpr float kStopHere = 60.f;
    std::vector<Service> CourierServices(Plots::Grid const& g, Plots::Registry const& r, CourierAsk const& ask);
    // A Travel argument's destination: the site and the spot; false for kTravelBack and
    // kTravelMine (the caller knows those) or a stop that isn't there.
    bool TravelStop(Plots::Grid const& g, uint32_t arg, Plots::Site const*& site, Plots::Spot& at);

    // ── Marking plots in the world ──
    enum class Relation : uint8_t { Mine = 1, Permitted = 2, Claimed = 3 };
    struct NearPlot
    {
        uint32_t id = 0;
        Plots::Rect bounds;
        Relation relation = Relation::Claimed;
    };
    // Claimed plots (not lapsed) on `map` whose middle is within `radius` of (x, y), nearest
    // first, at most kMaxNear; the viewer's own always among them when it's that near.
    constexpr float kNearRadius = 220.f;
    constexpr size_t kMaxNear = 64;
    std::vector<NearPlot> PlotsNear(Plots::Grid const& g, Plots::Registry const& r, Plots::Actor const& viewer,
        uint32_t map, float x, float y, float radius, int64_t now);
    std::vector<uint8_t> EncodeNearPlots(std::vector<NearPlot> const& plots);
}}

#endif
