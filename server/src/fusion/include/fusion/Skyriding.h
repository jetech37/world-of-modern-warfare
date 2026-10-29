/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_SKYRIDING_H
#define FUSION_SKYRIDING_H

#include <cstdint>
#include <vector>

// Skyriding (COMPANIONS.md "Flying mounts", design/GAME_DESIGN.md section 11): the flying
// companion lines (species 215-223) fly as Dragonflight's dragonriding does, by momentum, never
// free. There is no climb on demand and no hover: the mount always flies forward along the
// rider's pitch, a dive trades height for speed, a pull-up trades speed for height, drag and a
// constant sink bleed energy away, so range comes from height. Launch (a charge) throws
// the mount up, from the ground (a take-off) or the air; Surge (a charge) adds speed.
// Charges ("vigor") refill over time, faster on the ground. Landing ends the flight.
//
// The client flies the physics (`client/src/riding.rs` mirrors `Advance` to the float, tested
// on both sides against the same numbers); the server grants the flight (GM flight, the client's
// levitating swim, at kMaxSpeed) and holds it to the physics' energy law with `Envelope`: the
// rider's energy g·z + v²/2 can only fall, by at least the sink and half the drag, except by a
// spent charge. Any flight that climbs or keeps speed beyond that (a fly hack, a speed hack) is
// ended on the spot.
namespace Fusion { namespace Skyriding
{
    // ── The physics (yards, seconds, radians; pitch up positive) ──
    constexpr float kGravity = 16.f;            // along the flight path: the dive's pull
    constexpr float kDrag = 0.06f;              // speed lost per second, per yard/second
    constexpr float kSink = 1.5f;               // always down this fast: no hovering, no holding level forever
    constexpr float kStallSpeed = 10.f;         // under this the mount sinks harder...
    constexpr float kStallSink = 0.8f;          // ...by this per yard/second short of it
    constexpr float kMaxSpeed = 50.f;           // the fastest dive (a running mount is 14)
    constexpr float kMinPitch = -1.3963f;       // -80 degrees
    constexpr float kMaxPitch = 1.2217f;        // +70 degrees

    // Launch: straight up at kAscentRate for kAscentSeconds (about 34 yards), leaving at
    // kAscentExitSpeed at least. Surge: kSurgeSpeed more along the path.
    constexpr float kAscentSeconds = 1.2f;
    constexpr float kAscentRate = 28.f;
    constexpr float kAscentExitSpeed = 20.f;
    constexpr float kSurgeSpeed = 16.f;

    // The mount in flight: its speed along the path, and the Ascent still to run.
    struct Body
    {
        float speed = 0.f;
        float ascentLeft = 0.f;
    };
    // This step's velocity: along the rider's facing (level), and up.
    struct Motion
    {
        float forward = 0.f;
        float up = 0.f;
    };
    float ClampPitch(float pitch);
    // One step: the speed after it and the velocity to move at. Deterministic: the same steps give
    // the same numbers on both sides.
    Motion Advance(Body& b, float pitch, float dt);
    void Ascend(Body& b);
    void Surge(Body& b);
    // Energy per unit mass, with z the height: what Advance only ever lowers.
    float Energy(float z, float speed);

    // ── Vigor: the charges Ascent and Surge spend ──
    constexpr uint8_t kMaxCharges = 3;
    constexpr float kChargeAirSeconds = 20.f;   // one charge back, in flight
    constexpr float kChargeGroundSeconds = 5.f; // one charge back, on the ground
    constexpr float kMoveGapSeconds = 1.f;      // between two moves, whatever the charges
    struct Vigor
    {
        uint8_t charges = kMaxCharges;
        float progress = 0.f;                   // toward the next charge, 0..1
        uint32_t lastMs = 0;                    // last refill (0: never)
        uint32_t readyMs = 0;                   // the next move may go then
    };
    // Refill up to now: in flight at the air rate, otherwise the ground rate.
    void Recharge(Vigor& v, uint32_t nowMs, bool flying);
    // Spend a charge, if one is there and the gap has passed.
    bool Spend(Vigor& v, uint32_t nowMs);

    enum class Move : uint8_t { Ascent = 1, Surge = 2 };

    // ── The server's check ──
    // How far the reported flight may run ahead of the law before it is ended: a height and a
    // speed of slack (network and the client's step), and two bad looks running.
    constexpr float kSlackHeight = 8.f;
    constexpr float kSlackSpeed = 6.f;
    constexpr uint8_t kStrikes = 2;
    constexpr uint32_t kSampleMs = 400;         // the speed is measured over at least this
    constexpr float kMaxFlightSeconds = 300.f;  // a hard stop, whatever the energy
    struct Envelope
    {
        float budget = 0.f;                     // the most energy the rider can have now
        uint32_t sampleMs = 0;
        float x = 0.f, y = 0.f, z = 0.f;        // the last sample
        float speed = 0.f, lastSpeed = 0.f;     // measured, the last two samples
        uint8_t strikes = 0;
    };
    void Begin(Envelope& e, uint32_t nowMs, float x, float y, float z, float speed);
    // The most energy a move can add at the rider's speed.
    float Gain(Move m, float speed);
    void Grant(Envelope& e, Move m);
    // A position report: false once the flight has broken the law (end it).
    bool Check(Envelope& e, uint32_t nowMs, float x, float y, float z);

    // ── The wire: world channel ──
    // Server to client, op 71: [71, flags u8, charges u8, max u8, progress f32, speed f32] on every
    // change (the flight begins or ends, a move, a charge back). Client to server, op 72:
    // [72, move u8]: Launch (1) or Surge (2).
    constexpr uint8_t kOpSky = 71;
    constexpr uint8_t kOpSkyMove = 72;
    enum SkyFlags : uint8_t
    {
        SKY_FLYING = 0x01,      // skyriding now
        SKY_ASCENT = 0x02,      // an Ascent began with this message
        SKY_SURGE = 0x04,       // a Surge began with this message
        SKY_RIDER = 0x08,       // riding a flying line (the gauge shows)
    };
    std::vector<uint8_t> EncodeSky(uint8_t flags, Vigor const& v, float speed);
    bool DecodeSkyMove(std::vector<uint8_t> const& body, Move& out);
}}

#endif
