/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * The lore killstreaks (WOMW design, section 5): MW2's air support and sentry flown and manned by
 * the world's own engineering, drawn with models the 1.12 data already has.
 *
 *  - Predator missile  -> the Goblin Rocket: the player steers it down from the sky camera.
 *  - Attack helicopter -> the Sky Rider Gunship: a gryphon rider (Alliance) or a wind rider
 *    (Horde) circles the call-in point and shoots the exposed enemies.
 *  - Chopper gunner    -> the Gyrocopter Gunship: the heavier tier, a gnomish flying machine on
 *    the same ring with an explosive cannon.
 *  - Harrier strike    -> Gnomish Sky Hornets: a flying machine's pass of three bombs, then it
 *    hovers over the mark firing rockets.
 *  - Stealth bomber    -> the Siege Bomber: a dwarven bomber (Alliance) or a wind rider bomber
 *    (Horde) flies over the mark along the caller's facing, a line of nine bombs under it.
 *  - Sentry gun        -> the Dwarven Sentry Turret: set down on ground within reach, it turns on
 *    and shoots the hostiles in its arc.
 *
 * This is the pure part (where a craft flies, whom it shoots, where the bombs fall, how the rocket
 * steers, where a turret may stand, the rocket's wire, the GM's `.streak` names); the game side
 * (game/Fusion/FusionSkyStreaks.cpp) summons the craft as creatures and does the shooting.
 */

#ifndef FUSION_SKY_STREAKS_H
#define FUSION_SKY_STREAKS_H

#include "fusion/Common.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Fusion
{
namespace Sky
{
    constexpr float kPi = 3.14159265f;

    // ── The craft and their looks ──

    enum class Craft : uint8_t
    {
        Rocket,         // the Goblin Rocket (predator missile)
        Rider,          // the Sky Rider Gunship (attack helicopter)
        Gyrocopter,     // the Gyrocopter Gunship (chopper gunner)
        Hornet,         // a Gnomish Sky Hornet (harrier strike)
        Bomber,         // the Siege Bomber (stealth bomber)
        Turret,         // the Dwarven Sentry Turret (sentry gun)
    };

    // The creature_template entries the craft are summoned as
    // (sql/custom/azeroth_warfare/23_lore_streaks.sql: unselectable, immune, passive).
    constexpr uint32_t kEntryRocket = 93500;
    constexpr uint32_t kEntryRider = 93501;
    constexpr uint32_t kEntryGyrocopter = 93502;
    constexpr uint32_t kEntryBomber = 93503;
    constexpr uint32_t kEntryTurret = 93504;
    constexpr uint32_t kEntryHornet = 93505;

    // 1.12 creature displays (CreatureDisplayInfo.dbc).
    namespace Display
    {
        constexpr uint32_t kGoblinRocketCar = 10318;    // Creature\GoblinRocketCar
        constexpr uint32_t kFlyingMachine = 8011;       // Creature\FlyingMachineCreature (a gyrocopter)
        constexpr uint32_t kCannon = 3127;              // Creature\Object\Cannon
        constexpr uint32_t kDwarfRider = 13471;         // Wing Commander Slidore, a dwarf
        constexpr uint32_t kOrcRider = 13473;           // Wing Commander Guse, an orc
        constexpr uint32_t kGryphonMount = 6852;        // the flight masters' riding gryphon
        constexpr uint32_t kWindRiderMount = 6851;      // the flight masters' wind rider
    }

    struct Look
    {
        uint32_t entry = 0;
        uint32_t display = 0;
        uint32_t mount = 0;     // a mount under the display (a rider), 0 for none
        float scale = 1.f;
    };

    // How a craft looks for the caller's side (Neutral takes the Alliance's). Seen from 50 yards
    // up and out, a gunship at the model's own size was a speck: the flyers are drawn larger.
    Look LookOf(Craft craft, Team team);
    // The craft a streak flies, if any (StreakId), and back.
    std::optional<Craft> CraftOf(uint32_t streakId);
    uint32_t StreakOf(Craft craft);
    // The craft a creature entry is (93500-93505), if any.
    std::optional<Craft> CraftOfEntry(uint32_t entry);

    // ── Standing up to fire: health, the body shots see, the noise ──

    // How many fresh players' health (at the owner's level) a craft has; 0 for one that can't be
    // shot down (the rocket: seconds long and steered, as MW2's predator).
    float CraftToughness(Craft craft);
    // Its health on the CoD scale for an owner whose own full health is `playerHealth`.
    uint32_t CraftMaxHealth(Craft craft, float playerHealth);
    bool CraftShootable(Craft craft);
    // A blast (a launcher's rocket, a grenade, a killstreak's bomb) hurts a craft this much more
    // than it would a body: MW2's launchers are the answer to air support.
    constexpr float kCraftBlastScale = 2.f;
    // Health as a share for the HUD (0-100; at least 1 while any is left).
    uint8_t HealthPercent(uint32_t health, uint32_t maxHealth);

    struct Hitbox
    {
        float radius = 0.5f;
        float height = 1.f;
    };
    // The body the server's shot test gives a craft drawn at `scale` (a creature's bounding radius
    // is a person's; a gryphon and its rider are a good deal more).
    Hitbox CraftHitbox(Craft craft, float scale);

    // WoW's own sounds (SoundEntries.dbc) the server plays on the craft: an engine loop pushed to
    // each player once the craft is streamed to them, a wing beat on a beat, a cry on arrival, a
    // burst when it is destroyed.
    namespace Sound
    {
        constexpr uint32_t kFlyingMachineLoop = 7654;   // "FlyingMachieneLoop": a flying machine's engine
        constexpr uint32_t kWingFlapHuge = 5059;        // big wings, heard to 200 yards
        constexpr uint32_t kGryphonAggro = 1378;
        constexpr uint32_t kWyvernAggro = 761;
        constexpr uint32_t kBombBotDeath = 7517;        // a gnomish machine bursting
        constexpr uint32_t kCannonDeath = 3641;
    }
    struct CraftSounds
    {
        uint32_t loop = 0;      // looped on the craft (the client stops it when the craft goes)
        uint32_t beat = 0;      // played every beatMs
        uint32_t beatMs = 0;
        uint32_t arrive = 0;
        uint32_t destroyed = 0;
    };
    CraftSounds SoundsOf(Craft craft, Team team);
    // The loop is pushed to a player within kLoopHear of the craft, and again only after they
    // were farther than kLoopForget (the client stops a loop past its cutoff).
    constexpr float kLoopHear = 90.f;      // inside the engine loop's own 100 yard cutoff
    constexpr float kLoopForget = 104.f;

    // ── Orbits: the gunships' rings ──

    struct Orbit
    {
        Vec3 centre;            // the ground under the ring's middle
        float radius = 30.f;
        float height = 40.f;    // above the centre
        float startAngle = 0.f; // radians, WoW's (counter-clockwise from +x)
        float speed = 12.f;     // yards a second along the ring
    };

    // Where on the ring the craft is `ms` after it started.
    Vec3 OrbitAt(Orbit const& o, uint32_t ms);
    // The way it faces there (along the ring, counter-clockwise).
    float OrbitFacing(Orbit const& o, uint32_t ms);
    // The ring from `ms` for `spanMs`, a point every `stepMs` (both ends in): the path the
    // creature flies next.
    std::vector<Vec3> OrbitPath(Orbit const& o, uint32_t ms, uint32_t spanMs, uint32_t stepMs);
    // A second caller's ring starts elsewhere on the circle (the golden angle apart), so two
    // gunships over one spot do not fly through each other.
    float StartAngle(float facing, uint32_t sequence);

    // Comings and goings: a gunship flies in from the horizon to its ring (out along the ring's
    // start, higher), and when its time is up flies on along its heading, climbing, until it is
    // out of sight; the bombers fly on past their line the same way. No craft pops in or out.
    constexpr float kArriveFrom = 160.f;
    constexpr float kArriveRise = 20.f;
    constexpr float kArriveSpeed = 32.f;
    constexpr float kLeaveTo = 170.f;
    constexpr float kLeaveRise = 45.f;
    constexpr float kLeaveSpeed = 36.f;
    Vec3 ArriveFrom(Orbit const& o);
    Vec3 LeaveTo(Vec3 at, float facing);
    // How long a straight flight takes.
    uint32_t FlightMs(Vec3 from, Vec3 to, float speed);

    // The attack helicopter's ring (30 yd out, 40 up, a minute).
    constexpr float kRiderRadius = 30.f;
    constexpr float kRiderHeight = 40.f;
    constexpr float kRiderSpeed = 12.f;
    constexpr uint32_t kRiderMs = 60000;
    constexpr uint32_t kRiderShotMs = 300;          // a round every 0.3 s
    constexpr float kRiderDamage = 22.f;            // CoD damage a round, an LMG's
    // The chopper gunner's: lower and tighter, slower and heavier.
    constexpr float kGyroRadius = 25.f;
    constexpr float kGyroHeight = 32.f;
    constexpr float kGyroSpeed = 10.f;
    constexpr uint32_t kGyroMs = 60000;
    constexpr uint32_t kGyroShotMs = 700;           // a 20 mm explosive round
    // The harrier's hover over its mark.
    constexpr float kHornetRadius = 15.f;
    constexpr float kHornetHeight = 26.f;
    constexpr float kHornetSpeed = 9.f;
    constexpr uint32_t kHornetMs = 30000;
    constexpr uint32_t kHornetShotMs = 1100;        // a rocket
    // Who a gunship shoots at: on the ground within this of the ring's middle, and within this
    // of the gun.
    constexpr float kPatrolReach = 55.f;
    constexpr float kGunReach = 75.f;
    // How often a craft looks for a new target; between looks it keeps the one it has while that
    // one stays alive, in reach and in the open (the search is the costly part).
    constexpr uint32_t kRetargetMs = 500;

    // ── Targets ──

    struct Target
    {
        uint64_t guid = 0;
        Vec3 chest;
        bool alive = true;
        bool hostile = true;
        bool inCombat = false;
        bool threat = false;        // fighting the craft's owner (its victim, or hurt them lately)
        bool atCraft = false;       // shooting at this craft
        bool player = false;        // an enemy player (a killstreak is for them first)
    };
    using Sight = std::function<bool(Vec3 from, Vec3 to)>;

    // A target's rank, lower first: whoever is on the owner, then whoever is on the craft, then
    // players, then anyone already fighting, then the nearest. The one already in the sights
    // keeps it over a near tie, so the gun doesn't flick between two.
    float TargetScore(Target const& t, float distance, uint64_t current);
    // The most candidates a pick tests for sight (best first): each test is a ray through the
    // world, and a crowd under a gunship is many.
    constexpr uint32_t kMaxSightTests = 6;

    // Whom a gun in the air at `gun` shoots: a living hostile on the ground within `patrol`
    // (flat) of `centre` and `reach` of the gun, in sight of it (exposed), best by TargetScore.
    std::optional<uint64_t> AirTarget(Vec3 gun, Vec3 centre, float patrol, float reach,
        std::vector<Target> const& targets, Sight const& sight, uint64_t current = 0);

    // A gun walks its fire onto a new target: its first rounds land less often.
    constexpr uint32_t kWalkInRounds = 3;
    float WalkInChance(float chance, uint32_t roundsOnTarget);

    // ── The turret ──

    constexpr float kTurretRange = 40.f;
    constexpr float kTurretHalfArc = 70.f * kPi / 180.f;    // 140 degrees in all
    constexpr float kTurretMuzzle = 1.1f;           // the gun's height over its feet
    constexpr float kTurretDamage = 20.f;           // an M240's round (Defense's sentry)
    constexpr uint32_t kTurretMs = 90000;           // MW2's sentry: a minute and a half
    constexpr uint32_t kTurretShotMs = 150;
    // It sets itself up before it fires, spins its barrel up on each new target, and sweeps its
    // arc while it has none, so it is plainly on and turning.
    constexpr uint32_t kTurretDeployMs = 1500;
    constexpr uint32_t kTurretSpinUpMs = 450;
    constexpr uint32_t kTurretSweepMs = 4000;
    constexpr float kTurretSweep = 0.6f;                    // of the half arc
    constexpr float kTurretTurnStep = 4.f * kPi / 180.f;    // smaller turns are not sent
    constexpr uint32_t kMaxTurrets = 2;             // a player's standing at once
    // Placement: within this of the feet (flat), the ground under it found within this of the
    // spot, no more than this between the spot and a corner half a yard out, and no more than
    // this above or below the feet.
    constexpr float kPlaceReach = 6.f;
    constexpr float kPlaceGroundSlack = 1.f;
    constexpr float kPlaceStep = 0.6f;
    constexpr float kPlaceRise = 2.5f;

    // Whether a turret at `at` set down facing `facing` can turn on `to`: in range, and within
    // the arc either side of its facing.
    bool TurretCovers(Vec3 at, float facing, Vec3 to);
    // Whom it shoots: the best (TargetScore) living hostile it covers and sees.
    std::optional<uint64_t> TurretTarget(Vec3 muzzle, float facing, std::vector<Target> const& targets,
        Sight const& sight, uint64_t current = 0);
    // Where the turret points: at its target, or `ms` into its idle sweep either side of its
    // facing.
    float TurretAimYaw(Vec3 at, Vec3 to);
    float TurretIdleYaw(float facing, uint32_t ms);
    // Whether the yaw shown is far enough from the one wanted to turn it.
    bool TurretShouldTurn(float shown, float want);

    enum class Placement : uint8_t
    {
        Ok,
        TooFar,
        NoGround,
        TooSteep,
        NoSight,
        TooMany,
    };

    struct Spot
    {
        Vec3 feet;                  // the player's
        Vec3 spot;                  // where they pointed
        std::optional<float> ground;    // the ground's height at the spot
        float corners[4] = {0.f, 0.f, 0.f, 0.f};   // the ground half a yard out each way
        bool corner[4] = {true, true, true, true};  // found
        bool sight = true;          // the player sees the spot
        uint32_t standing = 0;      // the player's turrets already out
    };

    Placement CheckPlacement(Spot const& s);
    char const* PlacementText(Placement p);

    // ── Bombing runs ──

    struct BombRun
    {
        Vec3 centre;                // the mark (on the ground)
        Vec3 dir;                   // flat, unit: the way it flies
        float height = 45.f;        // above the mark (the game raises it over hills on the way)
        float speed = 55.f;         // yards a second
        float lead = 140.f;         // how far before the mark it starts, and past the line it ends
        uint32_t bombs = 9;
        float length = 64.f;        // the line the bombs cover, centred on the mark
    };

    constexpr uint32_t kBombs = 9;
    constexpr float kBombLine = 64.f;               // 8 yards apart
    constexpr float kBomberSpeed = 55.f;
    constexpr float kBomberHeight = 45.f;
    constexpr float kBomberLead = 140.f;
    constexpr uint32_t kBombFallMs = 1200;          // from the release to the blast
    constexpr float kBomberReach = 200.f;           // how far off the caller may mark
    // The harrier's pass: three bombs over 12 yards, lower and slower.
    constexpr uint32_t kHornetBombs = 3;
    constexpr float kHornetLine = 12.f;
    constexpr float kHornetPassHeight = 26.f;
    constexpr float kHornetPassSpeed = 35.f;
    constexpr float kHornetLead = 80.f;             // 3.5 s from the call to the first blast

    BombRun MakeBombRun(Vec3 centre, float facing, uint32_t bombs = kBombs, float length = kBombLine,
        float height = kBomberHeight, float speed = kBomberSpeed, float lead = kBomberLead);
    // Bomb i's offset along the line from the mark (-length/2 .. length/2).
    float BombOffset(BombRun const& r, uint32_t i);
    // Where bomb i comes down (at the mark's height: the game drops it to the ground there).
    Vec3 BombPoint(BombRun const& r, uint32_t i);
    // Where the craft is `ms` into the run, and when it is over bomb i (its release).
    Vec3 BomberAt(BombRun const& r, uint32_t ms);
    uint32_t ReleaseMs(BombRun const& r, uint32_t i);
    // The whole run: from the start to `lead` past the line's far end.
    uint32_t RunMs(BombRun const& r);
    Vec3 RunStart(BombRun const& r);
    Vec3 RunEnd(BombRun const& r);
    // When bomb i lands (from the run's start): its release and its fall.
    uint32_t LandMs(BombRun const& r, uint32_t i);

    // A falling bomb (and a shot-down craft's wreck) on the projectile op (31): the client draws
    // it dropping, nose down, rather than as a rocket.
    constexpr uint16_t kBombVisual = 3099;

    // ── The craft's news (world op 39) ──
    //
    // To the owner: a craft is up, how long it has left and its health (sent again as these
    // change), gone (and why), a kill it made, it was shot down (or you shot one down). To
    // everyone near a bombing run: where the bombs will fall, before they do.
    //   [39, 1, streak u8, guid u64, seconds u16, health % u8]
    //   [39, 2, guid u64, why u8]
    //   [39, 3, streak u8, name str8]                     (str8: length u8, then the bytes)
    //   [39, 4, streak u8, yours u8, name str8]           yours: it was the reader's craft
    //   [39, 5, flags u8, first ms u16, last ms u16, n u8, (x f32, y f32, z f32) * n]
    constexpr uint8_t kOpCraft = 39;
    enum class News : uint8_t
    {
        Up = 1,
        Gone = 2,
        Kill = 3,
        Downed = 4,
        StrikeLine = 5,
    };
    enum class GoneWhy : uint8_t
    {
        Done = 0,
        ShotDown = 1,
        Dismissed = 2,
    };
    constexpr uint8_t kLineMine = 1;        // the reader called it
    constexpr uint8_t kLineHostile = 2;     // an enemy did
    constexpr size_t kNewsNameMax = 48;
    constexpr uint8_t kMaxLinePoints = 16;

    std::vector<uint8_t> EncodeCraftUp(uint32_t streak, uint64_t guid, uint16_t seconds, uint8_t healthPercent);
    std::vector<uint8_t> EncodeCraftGone(uint64_t guid, GoneWhy why);
    std::vector<uint8_t> EncodeCraftKill(uint32_t streak, std::string const& victim);
    std::vector<uint8_t> EncodeCraftDowned(uint32_t streak, bool yours, std::string const& other);
    std::vector<uint8_t> EncodeStrikeLine(uint8_t flags, uint16_t firstMs, uint16_t lastMs,
        std::vector<Vec3> const& points);

    struct NewsMsg
    {
        News kind = News::Up;
        uint32_t streak = 0;
        uint64_t guid = 0;
        uint16_t seconds = 0;
        uint8_t health = 0;
        GoneWhy why = GoneWhy::Done;
        bool yours = false;
        std::string name;
        uint8_t flags = 0;
        uint16_t firstMs = 0;
        uint16_t lastMs = 0;
        std::vector<Vec3> points;
    };
    std::optional<NewsMsg> DecodeNews(std::vector<uint8_t> const& b);

    // ── The Goblin Rocket ──

    // World ops (the world channel): the server starts and ends the player's flight, the client
    // reports where the rocket is and where it struck.
    //   [37, phase u8 (1 start, 0 end), ticket u32, vehicle guid u64, x f32, y f32, z f32,
    //    yaw f32, pitch f32, life ms u16]     server -> client (36 bytes)
    //   [38, ticket u32, kind u8 (RocketReport), x f32, y f32, z f32]      client -> server (18)
    constexpr uint8_t kOpRocket = 37;
    constexpr uint8_t kOpRocketReport = 38;

    constexpr float kRocketHeight = 90.f;           // over the mark at the start
    constexpr float kRocketBack = 35.f;             // and this far back, toward the caller
    constexpr float kRocketSpeed = 18.f;            // yards a second
    constexpr float kRocketBoost = 55.f;            // after the click (for the rest of the flight)
    constexpr float kRocketPitchMin = -88.f * kPi / 180.f;  // straight down, nearly
    constexpr float kRocketPitchMax = -12.f * kPi / 180.f;  // it never climbs
    constexpr float kRocketTurn = 0.0022f;          // radians a mouse count
    constexpr float kRocketMaxTurnRate = 2.2f;      // radians a second, at most
    constexpr uint32_t kRocketLifeMs = 22000;       // then it is lost (no blast)
    constexpr uint32_t kRocketGraceMs = 3000;       // the server waits this past it
    constexpr uint32_t kRocketReportMs = 100;
    constexpr float kRocketHitReach = 1.6f;         // a body this near the rocket is struck
    // The blast: the catalog's remotemissile_projectile_mp (12 yd, 600 at the centre, 300 at the
    // edge), so Danger Close and the ragdolls' throw take it as any other.

    enum class RocketReport : uint8_t
    {
        Fly = 0,
        Impact = 1,
        Bail = 2,       // Escape: no blast
    };

    struct RocketPose
    {
        Vec3 pos;
        float yaw = 0.f;
        float pitch = -1.f;
        bool boost = false;
    };

    // The rocket's start over the mark `target` for a caller facing `facing`: up and back toward
    // the caller, nose on the mark.
    RocketPose RocketStart(Vec3 target, float facing);
    Vec3 RocketForward(RocketPose const& p);
    float RocketSpeed(RocketPose const& p);
    // Steer by the mouse (counts this frame: right and down positive) and fly `dt` seconds.
    RocketPose SteerRocket(RocketPose p, float mouseX, float mouseY, float dt);
    // Whether a report from `from` to `to` `dtMs` later could be the rocket's (the server's check:
    // boosted the whole time, with slack for the network).
    bool RocketStepOk(Vec3 from, Vec3 to, uint32_t dtMs);
    // Whether `at` could be reached from the start `elapsedMs` into the flight.
    bool RocketReachOk(Vec3 start, Vec3 at, uint32_t elapsedMs);

    std::vector<uint8_t> EncodeRocket(bool start, uint32_t ticket, uint64_t vehicle, RocketPose const& pose,
        uint16_t lifeMs);
    struct RocketReportMsg
    {
        uint32_t ticket = 0;
        RocketReport kind = RocketReport::Fly;
        Vec3 at;
    };
    std::optional<RocketReportMsg> DecodeRocketReport(std::vector<uint8_t> const& b);

    // ── The GM's `.streak` ──

    // A streak by its id ("5"), MW2's key ("predator_missile"), its name in the world ("Goblin
    // Rocket", any case, spaces or underscores) or a short word ("rocket", "predator", "turret",
    // "sentry", "gunship", "helicopter", "gyrocopter", "chopper", "hornets", "harrier", "bomber",
    // "stealth", "uav", "nuke"). Only ids in `allowed` (the ladder the client knows) answer.
    std::optional<uint32_t> StreakByName(std::string const& name, std::vector<uint32_t> const& allowed);
}
}

#endif
