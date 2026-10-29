/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PLOTS_H
#define FUSION_PLOTS_H

#include "fusion/Build.h"
#include "fusion/Common.h"
#include "fusion/Construction.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Plots: land a player buys to build on in peace. The open world stays as it was (anyone builds,
// raids come for a base, bullets and blasts break what stands); the plots are the other half, a
// homestead nobody else may touch. They lie in districts outside the classic world's play bounds
// (the Hyjal summit on Kalimdor, which 1.12 shipped with terrain and never opened; Azshara Crater,
// map 37, cut as a battleground and left empty; the flat placeholder plateau north of the Eastern
// Plaguelands where Quel'Thalas would be), reached by a courier NPC, laid out as grids of square
// plots with fixed ids. design/WORLD_BUILDING.md has the measured survey behind every number.
//
// A plot is bought with gold from a steward and kept with a small weekly upkeep, paid ahead. A
// plot whose upkeep runs out is overdue for a grace of two weeks (still safe, but nothing new may
// go up), then lapses: it goes back on sale and what stood on it is packed (what taking each piece
// down would give, salvaged) into each piece's builder's reclaim stash. One plot to an account (alts share it: an
// account's characters are one player).
//
// On a plot only its owner (any character of the owning account), those they permit and game
// masters may build, take down, break or open; creatures, sentries and raids never touch it, and
// no raid is ever called anywhere in a district.
namespace Fusion { namespace Plots
{
    enum class Size : uint8_t { Small = 1, Standard = 2, Large = 3 };

    // A district: a grid of `cols` by `rows` square plots of side `cell` from (originX, originY)
    // (its lowest x and y corner) on `map`. Plot ids run from `firstId`, row by row along x. It
    // lies in a site (below): the place, its gate and its safety bounds.
    struct District
    {
        Size size = Size::Standard;
        uint32_t map = 1;
        float originX = 0.f;
        float originY = 0.f;
        float cell = 40.f;
        uint16_t cols = 1;
        uint16_t rows = 1;
        uint32_t firstId = 1;
        uint32_t priceCopper = 0;
        uint32_t upkeepCopper = 0;      // a week's
        uint32_t deedItem = 0;          // the receipt the steward hands over
        uint8_t site = 0;               // Site::id; 0: none (a test grid)

        uint32_t Count() const { return uint32_t(cols) * rows; }
    };

    struct Rect
    {
        float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
        bool Contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
        Rect Inset(float by) const { return {x0 + by, y0 + by, x1 - by, y1 - by}; }
        bool Holds(Rect const& o) const { return o.x0 >= x0 && o.y0 >= y0 && o.x1 <= x1 && o.y1 <= y1; }
        bool Overlaps(Rect const& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
    };

    // A spot a courier sets a player down on.
    struct Spot
    {
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;
    };

    // A courier's stop inside a site besides its gate (Hyjal's crater floor, 900 yards and 140
    // down from the gate).
    struct Stop
    {
        std::string name;               // "the crater floor"
        Spot at;
    };

    // A site: one place that holds districts. Its gate is where the courier sets players down and
    // the steward, trainer, supplier and a spirit healer stand. `bounds` follows the rim of the
    // site's ground: whoever is inside it below `floorZ` has fallen off or through the world and
    // is put back at the nearest stop; a flight inside it never climbs above `ceilingZ` and ends
    // on leaving it. A ghost released inside it rises at the nearest stop.
    struct Site
    {
        uint8_t id = 0;
        std::string name;               // "Azshara Crater"
        std::string where;              // "in Azshara Crater": after "homestead 301 "
        uint32_t map = 1;
        Spot gate;
        std::vector<Stop> stops;
        Rect bounds;
        float floorZ = 0.f;
        float ceilingZ = 0.f;
    };

    // The closest of a site's gate and stops to (x, y, z).
    Spot NearestStop(Site const& s, float x, float y, float z);

    // A builder keeps this far in from a plot's edge, so neighbours' walls never touch.
    constexpr float kBuildMargin = 1.f;
    // No raid is called within this far of a district (raiders spawn kRaidSpawnDistance out).
    constexpr float kRaidBuffer = 80.f;

    class Grid
    {
    public:
        Grid() = default;
        explicit Grid(std::vector<District> districts, std::vector<Site> sites = {})
            : m_districts(std::move(districts)), m_sites(std::move(sites)) {}

        std::vector<District> const& Districts() const { return m_districts; }
        std::vector<Site> const& Sites() const { return m_sites; }
        Site const* SiteById(uint8_t id) const;
        Site const* SiteOf(uint32_t plotId) const;
        // The site whose bounds hold (x, y) on `map`, grown by `margin`; null when none.
        Site const* SiteAt(uint32_t map, float x, float y, float margin = 0.f) const;
        // Where to put back a player at (x, y, z) who has fallen below the floor of the site
        // they're over; nothing when they haven't.
        std::optional<Spot> FellThrough(uint32_t map, float x, float y, float z) const;
        // What is wrong with the layout (overlapping districts or ids, a district outside its
        // site or with none, a gate or stop on a plot, a site without bounds): empty when sound.
        std::vector<std::string> Problems() const;
        // The plot under (x, y) on `map`, 0 when none. Each plot holds its low edges, not its high
        // ones, so a point on a line between two plots is in exactly one.
        uint32_t PlotAt(uint32_t map, float x, float y) const;
        District const* DistrictOf(uint32_t plotId) const;
        std::optional<Rect> Bounds(uint32_t plotId) const;
        Rect DistrictBounds(District const& d) const;
        // Whether (x, y) is in a district, or within `margin` of one.
        bool InDistrict(uint32_t map, float x, float y, float margin = 0.f) const;
        uint32_t Count() const;

    private:
        std::vector<District> m_districts;
        std::vector<Site> m_sites;
    };

    // The realm's sites and districts, as measured in design/WORLD_BUILDING.md (§4):
    // - Site 1, the Hyjal summit (map 1): 32 small plots of 24 yards on the east terrace, 24 of 40
    //   in the gate pocket, 8 large of 64 on the crater floor (ids 1-32, 101-124, 201-208).
    // - Site 2, Azshara Crater (map 37): 32 small on the central floor, 24 on the north arm, 8
    //   large in the south canyon (ids 301-332, 401-424, 501-508).
    // - Site 3, the Thalassian plateau (map 0, z 129): 64 small, 96 standard, 16 large (ids
    //   601-664, 701-796, 801-816).
    // The world DB's `fusion_plot_site` and `fusion_plot_district` (sql 12) may replace it: an
    // admin moves an origin there without a rebuild. Plot ids must never be renumbered.
    Grid const& Default();

    // ── Ownership ──
    enum class PermitLevel : uint8_t { None = 0, Guest = 1, Builder = 2 };
    struct Permit
    {
        uint32_t guid = 0;              // a character
        PermitLevel level = PermitLevel::Guest;
    };
    // Guests open doors; builders also build and take down what they built.
    constexpr size_t kMaxPermits = 20;

    // Unix seconds.
    constexpr int64_t kWeek = 7 * 24 * 3600;
    // Bought with the first weeks paid; paid ahead at most this far; overdue this long, then lapsed.
    constexpr uint32_t kIncludedWeeks = 1;
    constexpr uint32_t kMaxPrepaidWeeks = 12;
    constexpr uint32_t kGraceWeeks = 2;
    // One plot to an account.
    constexpr uint32_t kMaxPlotsPerAccount = 1;

    struct Plot
    {
        uint32_t id = 0;
        uint32_t ownerAccount = 0;      // 0: for sale
        uint32_t ownerGuid = 0;         // the character who bought it
        int64_t boughtAt = 0;
        int64_t paidUntil = 0;
        std::vector<Permit> permits;

        bool Owned() const { return ownerAccount != 0; }
    };

    enum class Standing : uint8_t { ForSale, Paid, Overdue, Lapsed };
    Standing StandingOf(Plot const& p, int64_t now);

    // Every plot that has an owner (or had: a lapsed one waits for Lapse to clear it). Ids absent
    // are for sale.
    struct Registry
    {
        std::unordered_map<uint32_t, Plot> plots;

        Plot const* Find(uint32_t id) const;
        Plot* Find(uint32_t id);
        uint32_t CountOwnedBy(uint32_t account) const;
        Plot const* OwnedBy(uint32_t account) const;
        // Clear every plot past its grace: their ids, for the caller to pack their pieces (Pack)
        // and drop the rows. Each goes back on sale.
        std::vector<uint32_t> Lapse(int64_t now);
    };

    // Grant or change a permit (false: the list is full, or it's the owner); revoke one.
    bool Grant(Plot& p, uint32_t guid, PermitLevel level);
    bool Revoke(Plot& p, uint32_t guid);
    PermitLevel PermitOf(Plot const& p, uint32_t guid);

    // ── Buying and upkeep ──
    struct Buyer
    {
        uint32_t guid = 0;
        uint32_t account = 0;
        uint32_t money = 0;             // copper
    };
    enum class BuyResult : uint8_t { Ok, NoSuchPlot, Taken, LimitReached, NotEnoughMoney };
    BuyResult CanBuy(Grid const& g, Registry const& r, uint32_t plotId, Buyer const& b);
    // Buy: on Ok the plot is the buyer's with kIncludedWeeks paid, and `charge` is the price.
    BuyResult Buy(Grid const& g, Registry& r, uint32_t plotId, Buyer const& b, int64_t now, uint32_t& charge);

    enum class PayResult : uint8_t { Ok, NotOwner, NotEnoughMoney, TooFarAhead, Lapsed, NoWeeks };
    // Pay `weeks` of upkeep. Overdue weeks are owed first: the weeks run on from where the paid
    // time ended, not from now, so letting a plot slip never saves anything. `account` must own it.
    PayResult PayUpkeep(Grid const& g, Plot& p, uint32_t account, uint32_t weeks, uint32_t money, int64_t now, uint32_t& charge);

    // Giving a plot up: it goes back on sale at once (pack its pieces as for a lapse); no refund.
    bool Abandon(Registry& r, uint32_t plotId, uint32_t account);

    // ── Pieces on plots ──
    // The plot a piece belongs to: the one under its middle (0: none).
    uint32_t PlotOf(Grid const& g, Build::Piece const& p);
    // Whether every corner of a piece's boxes lies inside the plot, kBuildMargin in from its edge.
    bool Fits(Grid const& g, uint32_t plotId, Build::Piece const& p);

    // A lapsed (or released) plot's pieces packed up: what taking each down would give back
    // (Construction::Refund, by its health and with its reinforcement's share), as salvaged
    // items (Construction::AsSalvaged), to each builder, summed by item.
    struct Reclaim
    {
        uint32_t guid = 0;
        uint32_t item = 0;
        uint32_t count = 0;
    };
    std::vector<Reclaim> Pack(std::vector<Build::Piece> const& pieces);

    // ── Protection ──
    // Who acts: a player (their character and account), or anything that isn't one (a creature,
    // a raider, a sentry gun, a blast with no player behind it).
    struct Actor
    {
        enum class Kind : uint8_t { Player, Creature, World };
        Kind kind = Kind::Player;
        uint32_t guid = 0;
        uint32_t account = 0;
        bool gm = false;

        static Actor Player(uint32_t guid, uint32_t account, bool gm = false) { return {Kind::Player, guid, account, gm}; }
        static Actor Creature() { return {Kind::Creature, 0, 0, false}; }
        static Actor World() { return {Kind::World, 0, 0, false}; }
    };

    enum class Access : uint8_t { None, Guest, Builder, Owner, Gm };
    // What an actor may do on a plot (Owner: any character of the owning account). A plot for sale
    // or lapsed has no owner.
    Access AccessOf(Actor const& a, Plot const* p, int64_t now);

    // The answer the build code acts on: OpenWorld when the spot is outside every district (the
    // existing rules decide), else whether the plot rules allow it.
    enum class Verdict : uint8_t { OpenWorld, Allowed, Denied };

    // Putting a piece down. In a district only inside one plot (Fits), and only by its owner or a
    // builder while it's paid (overdue: nothing new goes up); a game master anywhere.
    Verdict CanBuild(Grid const& g, Registry const& r, Actor const& a, Build::Piece const& piece, int64_t now);
    // Taking one down: the owner anything on the plot, a builder what they built.
    Verdict CanRemove(Grid const& g, Registry const& r, Actor const& a, Build::Piece const& piece, int64_t now);
    // Hurting one (a bullet, a blast, a brawler's blow): the owner anything, a builder only what
    // they built, never a creature, raid or sentry. A piece on a plot is otherwise unbreakable.
    Verdict CanDamage(Grid const& g, Registry const& r, Actor const& attacker, Build::Piece const& piece, int64_t now);
    // Whether the salvage of a piece broken on a plot goes back to its builder instead of the
    // breaker: the plot's owner (or a game master) shooting down a permitted builder's piece would
    // otherwise keep 40% of it, more than taking it down (which already sends the builder its
    // refund) gives anyone. False out in the world, for the builder's own pieces, and for a piece
    // with no builder (security review 2026-10, #25).
    bool SalvageToBuilder(Grid const& g, Registry const& r, Actor const& breaker, Build::Piece const& piece, int64_t now);
    // Opening or shutting a door: the owner and anyone they permit.
    Verdict CanUseDoor(Grid const& g, Registry const& r, Actor const& a, Build::Piece const& door, int64_t now);
    // Whether a raid may be called on a base at (x, y): never in or near a district, nor anywhere in
    // a site.
    bool RaidsAllowedAt(Grid const& g, uint32_t map, float x, float y);

    // ── The wire: on the build channel, after Construction's ops ──
    // Plot = 18: plot id u32 (0: none here), size u8, standing u8, access u8, x0 y0 x1 y1 f32,
    // paid until u32 (unix seconds), permits u8. Sent when a player enters or leaves a plot.
    constexpr uint8_t kOpPlot = 18;
    std::vector<uint8_t> EncodePlot(Grid const& g, Registry const& r, uint32_t plotId, Actor const& viewer, int64_t now);
}}

#endif
