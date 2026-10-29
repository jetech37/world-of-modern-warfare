/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_FURNITURE_H
#define FUSION_FURNITURE_H

#include "fusion/Build.h"
#include "fusion/Common.h"

#include <cstdint>
#include <string>
#include <vector>

// Furniture: Construction's decor layer, after retail WoW's housing (decor pieces placed one by
// one, aligned to the surface they go on, small things parented onto tables, a grid and 15-degree
// turns to keep things neat) and the fan projects that brought it to older clients. Each piece is
// one of WoW 1.12's own doodads (a chair, a bed, a chest, a bookshelf, an anvil...), and many of
// them do something:
//
//   Seat        F sits you down on it (facing out of it).
//   Bed         F lies you down; a bed on your own homestead (or one you hold a permit on) makes
//               you rest there, as an inn does: rested experience builds while you are near it.
//   Stash       F opens the plot's shared stash: one store per plot, its room the sum of the
//               stash pieces on the plot; the owner and anyone with a permit put in and take out.
//   Book        F reads it: a lore book or a guide to the game's systems.
//   Station     A crafting station: an anvil, a forge, a cooking fire, a loom or an alchemy table
//               counts as WoW's own (its spell focus) for the professions' recipes within reach.
//   Mailbox     A working mailbox (the server stands a real one in it): right-click it as any.
//   Dummy       A training dummy (the server stands a dummy in it): hit it and it reports your
//               damage and damage per second when you stop.
//   Trophy      A trophy plinth: F with a companion out mounts its likeness on it.
//   Stage       A bard's stage: songs played standing on it land a quarter stronger.
//   Surface     Tables, desks and shelves: small pieces stand on their tops.
//   Decor       Banners, rugs, lights, plants and the like: for the look (variants for colours).
//
// A furniture piece is a piece like any other (Build.h): a type of its own on the wire and in the
// pieces table (ids from kBase), saved, streamed, decayed and refunded as pieces are. There is no
// cap on furniture; small decor is cheap to draw and the client fades it out beyond kDrawFar.
namespace Fusion { namespace Furniture
{
    constexpr uint8_t kBase = Build::kFurnitureBase;

    enum class Use : uint8_t { Decor, Seat, Bed, Surface, Stash, Book, Station, Mailbox, Dummy, Trophy, Stage };
    // What a piece stands on: the ground or a floor; also a table's top (small things); a wall.
    enum class Mount : uint8_t { Floor, Surface, Wall };
    enum class Station : uint8_t { None, Anvil, Forge, Cooking, Loom, Alchemy };

    struct Def
    {
        uint8_t id;
        char const* name;
        Use use;
        Mount mount;
        Station station;
        float top;              // a surface's height above its origin (tables, shelves), else 0
        Vec3 center;            // its box, from its model's bounds (the client's prop_bounds)
        Vec3 half;
        uint16_t stashSlots;    // a stash piece's room
        uint8_t book;           // a book piece's text (Books()), 0 none
        uint32_t priceCopper;   // sold as a kit by the Plot Steward (0: not sold)
        uint8_t dropWeight;     // found as a kit when cutting lumber (0: never)
    };
    std::vector<Def> const& All();
    inline bool IsFurniture(Build::PieceType t) { return uint8_t(t) >= kBase; }
    Def const* Find(Build::PieceType t);
    Use UseOf(Build::PieceType t);          // Decor for a piece that is no furniture
    // What the Plot Steward sells (priceCopper), in catalogue order.
    std::vector<Def const*> Sold();
    // A lumber node cut finds a kit kDropChance of the time: which (by dropWeight), `roll` and
    // `pick` in [0, 1); None when it finds nothing.
    constexpr float kDropChance = 0.04f;
    Build::PieceType DropFor(float roll, float pick);

    // ── Placement ──
    // Finer than building: positions on a quarter-yard grid, turns in 15-degree steps (the
    // client offers both and may go free; the server takes any position and yaw it is sent).
    constexpr float kGrid = 0.25f;
    constexpr float kTurnStep = 0.2617994f;     // 15 degrees
    float SnapYaw(float yaw);
    float SnapGrid(float v);
    // Whether a piece stands as its mount asks: Floor on the ground or a floor (Build::Footed),
    // Surface there or on a surface piece's top (a table), Wall against a built wall, a beam or a
    // structure (touching it, kWallSlack). Wall pieces need no ground under them.
    constexpr float kWallSlack = 0.08f;
    bool OnSurface(Build::Piece const& p, std::vector<Build::Piece> const& pieces);
    bool OnWall(Build::Piece const& p, std::vector<Build::Piece> const& pieces);
    bool Footed(Build::Piece const& p, std::vector<Build::Piece> const& pieces, Build::Ground const& ground);
    // Two of the same small piece may stand this close (a row of candles); other pieces keep
    // Build::Overlaps' half yard.
    constexpr float kSameSpacing = 0.1f;

    // ── Using a piece (F) ──
    constexpr float kUseReach = 4.f;
    // The vanilla spell focus a station stands for (SpellFocusObject.dbc): 1 Anvil, 2 Loom,
    // 3 Forge, 4 Cooking Fire, 663 Alchemy Lab (the flasks' and transmutes' lab).
    constexpr uint32_t kAlchemyLabFocus = 663;
    uint32_t SpellFocus(Station s);
    // Whether a station piece answering `focus` stands within `radius` of `at` (vanilla allows
    // 10 yards, its rows ask 7 at most).
    constexpr float kStationReach = 8.f;
    bool StationNear(std::vector<Build::Piece> const& pieces, Vec3 at, uint32_t focus, float radius = kStationReach);

    // Seats and beds: where the sitter goes (the piece's middle, at a seat's height: its top, or
    // kSeatHeight for a chair whose back stands taller, kBedHeight for a bed), and which way they
    // face (the piece's yaw).
    constexpr float kSeatHeight = 0.55f;
    constexpr float kBedHeight = 0.8f;
    Vec3 SeatSpot(Build::Piece const& p);

    // Resting: a player within kRestRadius of a bed they may rest at (their own plot's, or one
    // they hold a permit on: `mayRest`) is resting, as in an inn.
    constexpr float kRestRadius = 10.f;
    bool RestsNear(std::vector<Build::Piece> const& pieces, Vec3 feet, std::function<bool(Build::Piece const&)> const& mayRest);

    // Stages: a performer standing on (or within kStageReach of) a stage plays kStageBoost
    // stronger.
    constexpr float kStageReach = 2.5f;
    constexpr float kStageBoost = 1.25f;
    bool OnStage(std::vector<Build::Piece> const& pieces, Vec3 feet);

    // ── The stash: one per plot ──
    struct StashEntry
    {
        uint32_t item = 0;
        uint32_t count = 0;
    };
    // The room of a plot's stash: the sum of its stash pieces' slots, at most kMaxStashSlots.
    constexpr uint16_t kMaxStashSlots = 240;
    uint16_t StashSlots(std::vector<Build::Piece> const& onPlot);
    // What may go in: a plain item (no enchantment, no random property, not soulbound, not a
    // quest item, not a bag with things in it). The stash keeps entries and counts only.
    struct ItemFacts
    {
        bool soulbound = false;
        bool enchanted = false;
        bool randomProperty = false;
        bool quest = false;
        bool filledBag = false;
        bool conjured = false;
    };
    bool Storable(ItemFacts const& f);
    enum class StashResult : uint8_t { Ok, NoRoom, NotStorable, NotThere, NotAllowed };
    // `count` of `item` put in, merged onto its stacks (`maxStack` each) and then new slots; all
    // or nothing.
    StashResult Deposit(std::vector<StashEntry>& stash, uint16_t slots, uint32_t item, uint32_t count, uint32_t maxStack);
    // `count` (at most the slot's) taken from slot `index`; the slot goes when emptied.
    StashResult Withdraw(std::vector<StashEntry>& stash, size_t index, uint32_t count, StashEntry& out);

    // ── The training dummy's meter ──
    // Every hit on a dummy by one player: the damage and the time; idle kDummyIdleMs after the
    // last hit, the round is over and is reported.
    constexpr uint32_t kDummyIdleMs = 5000;
    struct DpsMeter
    {
        uint64_t firstMs = 0;
        uint64_t lastMs = 0;
        uint64_t total = 0;
        uint32_t hits = 0;
        void Hit(uint64_t nowMs, uint32_t damage);
        bool Idle(uint64_t nowMs) const { return hits && nowMs - lastMs >= kDummyIdleMs; }
        // Damage per second over the round: from the first hit to the last, at least a second.
        float Dps() const;
        float Seconds() const;
        void Reset() { *this = DpsMeter{}; }
    };
    std::string DpsReport(DpsMeter const& m);

    // ── Books ──
    struct Book
    {
        uint8_t id;
        char const* title;
        std::vector<char const*> pages;
    };
    std::vector<Book> const& Books();
    Book const* BookOf(uint8_t id);

    // ── The wire: on the build channel ──
    //   client [8, piece u32]: use it (F).
    //   client [9, piece u32, action u8 (1 put in, 2 take out), a u8, b u8, count u16]: the stash
    //          (put in: bag a, slot b; take out: slot a | b << 8).
    //   server [21, piece u32, kind u8 (Use), title str8, n u8, n x line str16]: a page to show
    //          (a book's page, a dummy's report, a trophy's or a stash's word).
    //   server [22, piece u32, slots u16, n u16, n x (item u32, count u16),
    //          m u8, m x (bag u8, slot u8, item u32, count u16)]: the stash and what the bags hold.
    constexpr uint8_t kOpUse = 8;
    constexpr uint8_t kOpStash = 9;
    constexpr uint8_t kOpPage = 21;
    constexpr uint8_t kOpStashView = 22;
    constexpr uint8_t kStashPut = 1;
    constexpr uint8_t kStashTake = 2;
    bool DecodeUse(std::vector<uint8_t> const& body, uint32_t& piece);
    std::vector<uint8_t> EncodeUse(uint32_t piece);
    struct StashAct
    {
        uint32_t piece = 0;
        uint8_t action = 0;
        uint8_t a = 0;
        uint8_t b = 0;
        uint16_t count = 0;
    };
    bool DecodeStash(std::vector<uint8_t> const& body, StashAct& out);
    std::vector<uint8_t> EncodeStash(StashAct const& a);
    std::vector<uint8_t> EncodePage(uint32_t piece, Use kind, std::string const& title, std::vector<std::string> const& lines);
    struct BagItem
    {
        uint8_t bag = 0;
        uint8_t slot = 0;
        uint32_t item = 0;
        uint16_t count = 0;
    };
    std::vector<uint8_t> EncodeStashView(uint32_t piece, uint16_t slots, std::vector<StashEntry> const& stash, std::vector<BagItem> const& bags);
}}

#endif
