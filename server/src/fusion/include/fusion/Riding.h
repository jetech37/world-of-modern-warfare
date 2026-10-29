/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_RIDING_H
#define FUSION_RIDING_H

#include "fusion/Airspace.h"
#include "fusion/Common.h"
#include "fusion/Plots.h"
#include "fusion/Skyriding.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Riding (design/GAME_DESIGN.md section 11): a mount is a fighting position, not a way out of the
// fight. A rider fires guns and throws equipment from the saddle (no sprint: the mount is the
// speed), and gunfire can knock them off as vanilla's daze does. A mount also flies, briefly: a
// mounted jump from height glides (falling slowly, keeping its speed), and at a launch point (a
// tower, an updraft, a flight master's perch, a district's gate) a mounted jump takes off for a
// short burst of flight under a hard ceiling, then glides down. The flying companion lines skyride
// instead (Skyriding.h): momentum flight that only goes as far as height and charges carry it.
// The server keeps every timer; the client only moves as it is told (feather fall for a glide,
// GM-style flight for a burst, GM-style flight steered by the momentum physics for a skyride).
namespace Fusion { namespace Riding
{
    // ── Shooting from the saddle ──
    // A rider's eye sits this much above a standing one.
    constexpr float kMountedEyeRise = 1.2f;
    // How far a mounted shot's origin may sit from the server's eye: the mount's speed puts the
    // client further ahead of the server than a runner (HitValidation's kOriginTolerance is 3).
    constexpr float kMountedOriginTolerance = 5.f;
    float OriginTolerance(bool mounted);

    // ── Knocked off ──
    // A hit on a mounted player: the chance it throws them off. Per hit, scaling with its share of
    // their maximum health (kKnockOffPerShare: a tenth of it is 15%), nothing under
    // kKnockOffMinShare (a pellet or a graze); a sniper's round or a blast always does.
    struct RiderHit
    {
        float damage = 0.f;
        float maxHealth = 1.f;
        bool sniper = false;
        bool explosive = false;
    };
    constexpr float kKnockOffPerShare = 1.5f;
    constexpr float kKnockOffMinShare = 0.04f;
    float KnockOffChance(RiderHit const& h);
    // `roll` is uniform in [0, 1).
    bool KnockedOff(RiderHit const& h, float roll);

    // ── Flight ──
    // A launch point: a mounted jump within `radius` of (x, y, z) (and kLaunchSlackZ of its
    // height) takes off for `burstSeconds`. The flight ends early on rising above `ceilingZ`,
    // going `range` yards from the point, or leaving the site it began in.
    struct LaunchPoint
    {
        uint16_t id = 0;
        std::string name;
        uint32_t map = 0;
        float x = 0.f, y = 0.f, z = 0.f;
        float radius = 10.f;
        float burstSeconds = 15.f;
        float ceilingZ = 0.f;
        float range = 300.f;
    };
    constexpr float kLaunchSlackZ = 6.f;
    // After a flight, the next take-off waits this long.
    constexpr float kLaunchCooldownSeconds = 30.f;
    // The longest burst any point may give (a table row asking more is held to it).
    constexpr float kMaxBurstSeconds = 20.f;

    // The capitals' perches, the Dun Morogh airfield, Hyjal's gate and its Felwood ledge, the
    // Azshara Crater hub, the Thalassian plateau's gate, Light's Hope Chapel. The world DB's
    // `fusion_launch_point` (sql 12) may replace them.
    std::vector<LaunchPoint> const& DefaultLaunchPoints();
    LaunchPoint const* LaunchAt(std::vector<LaunchPoint> const& points, uint32_t map, float x, float y, float z);
    // What is wrong with a table of points (a ceiling under its point, no radius, a burst too
    // long, ids shared): empty when sound.
    std::vector<std::string> Problems(std::vector<LaunchPoint> const& points);

    // A mounted fall glides once the ground is this far below.
    constexpr float kGlideMinHeight = 8.f;
    // A glide lasts at most this long; the glide down after a flight, at most kDescentSeconds.
    constexpr float kGlideSeconds = 8.f;
    constexpr float kDescentSeconds = 60.f;
    // Down: on the ground (not falling, and within this of it).
    constexpr float kLandedHeight = 2.5f;
    // The glide down lasts at least this long (the client's first falling report may lag the end
    // of the flight).
    constexpr float kMinDescentSeconds = 1.f;

    // Skyride: a flying companion line's momentum flight (Skyriding.h), from a fall off height, a
    // launch point's updraft (a free Launch) or a Launch; ground mounts never.
    enum class Phase : uint8_t { None = 0, Glide = 1, Burst = 2, Descent = 3, Skyride = 4 };
    // A skyride's landing doesn't count until this long after an Ascent began it (the client is
    // still on the ground when the take-off is granted).
    constexpr float kTakeOffGraceSeconds = Skyriding::kAscentSeconds + 0.8f;

    // A player's flight (server clock, ms).
    struct Flight
    {
        Phase phase = Phase::None;
        uint32_t sinceMs = 0;
        uint32_t untilMs = 0;               // the phase ends by then at the latest
        uint16_t point = 0;                 // Burst: the launch point
        uint32_t map = 0;
        float originX = 0.f, originY = 0.f; // Burst: where it took off
        float ceilingZ = 0.f;
        float range = 0.f;
        bool bounded = false;               // Burst: begun in a site, it ends on leaving it
        Plots::Rect bounds;
        uint32_t nextLaunchMs = 0;          // the cooldown
        bool glideSpent = false;            // a glide gave out on this fall: none till the ground
        // Skyride: the energy law the reported flight is held to, the charges (kept in every
        // phase), when a landing first counts, and whether the law ended the last one.
        Skyriding::Envelope sky;
        Skyriding::Vigor vigor;
        uint32_t landAfterMs = 0;
        bool broke = false;
        // Warded airspace (Airspace.h): the hold on the ceiling, whether it ended the last
        // skyride, and whether this update the rider came over a ceiling (the UI message).
        Wards::Ward ward;
        bool wardBroke = false;
        bool wardPushed = false;
    };

    // What the server sees of the player this update.
    struct Sense
    {
        uint32_t nowMs = 0;
        bool alive = true;
        bool mounted = false;
        bool airborne = false;              // jumping or falling
        bool jumped = false;                // a jump began since the last update
        bool exempt = false;                // a game master's own flight, a taxi, a transport
        uint32_t map = 0;
        float x = 0.f, y = 0.f, z = 0.f;
        float groundZ = -100000.f;          // the ground below (far below: none found)
        LaunchPoint const* launch = nullptr;
        Plots::Site const* site = nullptr;  // the site the player is in, if any
        bool skyrider = false;              // riding a flying companion line (species 215-223)
        bool inWater = false;
        float speed = 0.f;                  // the mount's run speed: a skyride starts at it
        uint16_t wardZone = 0;              // the warded airspace here (Airspace.h), 0 none
        float wardCeilingZ = Wards::kNoZ;
    };

    // Move the flight on: the phase after this update. The game acts on the change (leaving a
    // burst stops the flight, a glide or descent starts or stops feather fall).
    void Step(Flight& f, Sense const& s);
    // A Launch or a Surge asked for: true if it goes (a charge is spent). An Ascent
    // off the ground or out of a glide takes off into a skyride; a Surge needs one under way.
    bool SkyMove(Flight& f, Sense const& s, Skyriding::Move m);
    // Seconds left in the phase (0 none).
    float SecondsLeft(Flight const& f, uint32_t nowMs);

    // ── The wire: world channel, op 70 (server to client) ──
    // Flight = 70: phase u8, seconds left f32, ceiling f32. Sent on every change of phase.
    constexpr uint8_t kOpFlight = 70;
    std::vector<uint8_t> EncodeFlight(Flight const& f, uint32_t nowMs);

    // Grenades (and the other thrown equipment) from the saddle: yes on the ground, a ground
    // mount or a flyer standing; never while skyriding in the air (the hands are on the reins
    // at speed, and a throw from altitude would be a free bomb run). The client greys the slot
    // out the same way (crate::weapon).
    constexpr float kAirborneYards = 3.f;
    inline bool ThrowsFromSaddle(bool skyrider, float heightAboveGround)
    {
        return !skyrider || heightAboveGround < kAirborneYards;
    }
}}

#endif
