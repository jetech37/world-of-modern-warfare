/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_AIRSPACE_H
#define FUSION_AIRSPACE_H

#include <cstdint>
#include <vector>

// Warded airspace (design/FOREVER_REVIEW.md "Flying over cities"): the classic cities and the
// worst unfinished places break when flown over (open roofs, back faces, terrain holes, the
// unbuilt backs of mountains), so a skyride may not climb above a ceiling there. Over a city the
// ceiling sits a little above the local ground (the roofs); over an unfinished place it is a
// fixed height (an invisible ceiling). Above it the mount is pushed down, softly but faster the
// further over it is, so a rider entering a city high up is brought down and cannot hover over
// it; under it the rider flies as anywhere. A few landing pads (flight masters, city gates) raise
// the ceiling over them so a take-off or a landing there runs its course.
//
// The server finds the airspace (its circle, or its AreaTable ids near it) and the ceiling (the
// map's height under the rider), tells the client (world op 73), and holds the reported flight
// to it with `Hold`: the rider must come down at least kHoldRate, from wherever they first were
// over it, else the skyride ends in the glide down. The client pushes harder (`AllowedUp`,
// `client/src/riding.rs` mirrors it to the float), so an honest flight is never caught.
namespace Fusion { namespace Wards
{
    enum class Kind : uint8_t { City = 1, Unfinished = 2 };

    // No fixed ceiling / no ground ceiling.
    constexpr float kNoZ = 1.0e6f;

    struct Pad
    {
        char const* name;
        float x, y, z;
        float radius;
    };

    struct Zone
    {
        uint16_t id;
        char const* name;
        Kind kind;
        uint32_t map;
        float x, y, radius;             // the circle (world x, y; yards)
        std::vector<uint32_t> areas;    // AreaTable zone or subzone ids that count too, near it
        float aboveGround;              // the ceiling over the local ground (0: none)
        float maxZ;                     // a fixed ceiling (kNoZ: none)
        std::vector<Pad> pads;
    };

    // ── The rules (yards, seconds) ──
    // An AreaTable id counts within this many radii of the circle (it keeps a match local).
    constexpr float kAreaReach = 1.5f;
    // Over a pad the ceiling is at least this far above it: a Launch (about 34) fits.
    constexpr float kPadHeadroom = 45.f;
    // The client's push (AllowedUp): under the ceiling the climb eases off, kEaseGain per yard
    // left; over it the mount sinks at kPushMin plus kPushGain per yard over, up to kPushMax.
    constexpr float kEaseGain = 2.f;
    constexpr float kPushMin = 4.f;
    constexpr float kPushGain = 0.5f;
    constexpr float kPushMax = 20.f;
    // The server's hold: the allowance over the ceiling shrinks at kHoldRate (under kPushMin, so
    // the client's push always wins), plus kHoldSlack for the network and a stale ceiling; two
    // bad looks running end the flight.
    constexpr float kHoldRate = 3.f;
    constexpr float kHoldSlack = 10.f;
    constexpr uint8_t kHoldStrikes = 2;
    // The client is told the ceiling at most this often, or when it moves this much.
    constexpr uint32_t kTellMs = 200;
    constexpr float kTellMove = 1.f;

    // The table: the cities, then the unfinished places. VERIFY the circles in game.
    std::vector<Zone> const& Zones();

    // The ceiling over (x, y) on `map` (with `groundZ` under the rider and their area and zone
    // ids), the lowest of every airspace there: false outside every one.
    struct Here
    {
        Zone const* zone = nullptr;     // the airspace whose ceiling it is
        float ceilingZ = kNoZ;
        bool pad = false;               // over a landing pad
    };
    bool Find(std::vector<Zone> const& zones, uint32_t map, float x, float y, float groundZ,
        uint32_t area, uint32_t zoneId, Here& out);
    // One airspace's ceiling at a spot (pads included).
    float CeilingOf(Zone const& z, float x, float y, float groundZ, bool* pad = nullptr);
    bool Contains(Zone const& z, uint32_t map, float x, float y, uint32_t area, uint32_t zoneId);

    // The fastest climb the client allows at height z under (or over) the ceiling: a cap on the
    // skyride's upward velocity, negative over the ceiling.
    float AllowedUp(float z, float ceilingZ);

    // The server's hold on a skyride in warded airspace.
    struct Ward
    {
        uint16_t zone = 0;              // the airspace being held to (0: none)
        float allowance = 0.f;          // how far over the ceiling the rider may still be
        float lastCeiling = 0.f;
        uint32_t sampleMs = 0;
        uint8_t strikes = 0;
        bool pushed = false;            // was over the ceiling at the last look
    };
    // A look at the rider: `zone` 0 when out of every airspace. False once the rider stays over
    // the ceiling longer than the push allows (end the flight). `enteredOver` is set when the
    // rider has just come over the ceiling (the UI message).
    bool Hold(Ward& w, uint16_t zone, float ceilingZ, float z, uint32_t nowMs, bool& enteredOver);

    // ── The wire: world channel, op 73 (server to client) ──
    // [73, flags u8, ceiling f32]: flags WARD_IN (in warded airspace: the ceiling holds),
    // WARD_UNFINISHED (an unfinished place, not a city), WARD_PAD (over a landing pad).
    constexpr uint8_t kOpWard = 73;
    enum WardFlags : uint8_t
    {
        WARD_IN = 0x01,
        WARD_UNFINISHED = 0x02,
        WARD_PAD = 0x04,
    };
    std::vector<uint8_t> EncodeWard(uint8_t flags, float ceilingZ);
    // The message on being pushed down.
    char const* PushMessage(Kind k);
}}

#endif
