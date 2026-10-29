/*
 * World of Modern Warfare: spells cast from the hands, the two-hand way (design/GAME_DESIGN.md,
 * "Spellcasting"). GPL-2.0-or-later, see Common.h.
 *
 * With the hands up a spell is readied into a hand; holding that hand's mouse button charges it,
 * and releasing it fully charged casts it at the crosshair: no target is selected first, the cast
 * is instant and works on the move. The client times the charge; the server casts at once and
 * holds each hand to one cast per charge, so the charge can't be skipped. These rules decide which
 * spells a hand holds, how long each charges, where a release lands and the wire of it all; the
 * game (game/Fusion/FusionSpellcasting.cpp) casts.
 */

#ifndef FUSION_SPELLCASTING_H
#define FUSION_SPELLCASTING_H

#include "fusion/Common.h"
#include "fusion/HitValidation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace Fusion::Spellcasting
{
    // What a release does with the crosshair.
    enum class Shape : uint8_t
    {
        Bolt = 0,   // at the crosshair's enemy, or the enemy standing at the crosshair's point
        Heal = 1,   // a helpful spell: the crosshair's friend, else (or with the self key) oneself
        Area = 2,   // the spell's own area lands at the crosshair's point (Frost Nova, Consecration)
        Splash = 3, // the spell on every enemy around the point, and a zone that keeps putting it on
                    // whoever walks in (Moonfire)
        Missile = 4,// free-aimed: flies from the hand along the crosshair's ray and strikes whatever
                    // it meets first (a unit, a wall, the ground, a built piece); straight unless it
                    // homes (BoltTraits), splashing the enemies around where it strikes
        Ground = 5, // the spell's own ground area (Blizzard, Flamestrike) at the crosshair's point
        Spray = 6,  // held: a short cone in front (a fire stream, a frost spray) for as long as the
                    // button is down, ticking damage and mana (SprayRow)
    };

    // A spell whose release lands on the ground, by its chain's first rank.
    struct AreaRow
    {
        uint32_t firstRank = 0;
        Shape shape = Shape::Area;
        float radius = 0.f;         // yards (an Area spell's own radius, for the marker)
        uint32_t zoneMs = 0;        // Splash: how long the zone stays (0: none)
        uint32_t zoneTickMs = 0;    // Splash: how often it puts the spell on newcomers
    };
    std::vector<AreaRow> const& AreaRows();
    AreaRow const* AreaOf(uint32_t firstRank);

    // What the game reads off a spell entry to place it.
    struct SpellFacts
    {
        uint32_t firstRank = 0;
        int32_t castMs = 0;         // its cast time as the kit fits it (a hand casts it instantly)
        bool channeled = false;
        bool positive = false;      // a helpful spell
        bool selfRange = false;     // SpellRange.dbc row 1: on oneself (a buff, a shout)
        bool physical = false;      // school 0: a move, not a spell
        bool nextSwing = false;     // Heroic Strike's kind: rides the next swing
        bool passive = false;
        float rangeYards = 0.f;     // its reach (SpellRange.dbc max)
        bool groundTarget = false;  // it wants a point on the ground (Targets & DEST_LOCATION)
        bool missileChannel = false;// a channel whose ticks fire missiles (Arcane Missiles)
    };
    // Whether a hand holds it (the client's `hands::readied_as` InHand, plus the area spells a
    // hand now aims: Frost Nova and Consecration were cast on oneself).
    bool InHand(SpellFacts const& f);
    Shape ShapeOf(SpellFacts const& f);

    // ── The charge ──
    constexpr uint16_t kChargeInstantMs = 300;  // an instant spell (Moonfire's bolt, a shock)
    constexpr uint16_t kChargeQuickMs = 450;    // up to 1.5 s (Frostbolt, Flash Heal, Lightning Bolt)
    constexpr uint16_t kChargeMediumMs = 600;   // up to 2.5 s (Fireball, Wrath, Smite)
    constexpr uint16_t kChargeHeavyMs = 800;    // longer (Shadow Bolt)
    constexpr uint16_t kChargeChannelMs = 500;  // a channel (Arcane Missiles, Drain Life)
    constexpr uint16_t kChargeAreaMs = 900;     // anything landing on the ground
    uint16_t ChargeMs(SpellFacts const& f);
    // The server lets a hand cast again once its charge has passed since its last cast, less a
    // little for the network's jitter.
    constexpr uint16_t kChargeSlackMs = 150;
    bool ChargeServed(uint32_t lastCastMs, bool castBefore, uint32_t nowMs, uint16_t chargeMs);

    // ── Charge levels: a tap or a full charge (design/BALANCE_GUNS_VS_CLASSES.md) ──
    // Let go once charged: a tap, the spell as designed. Held on to kFullChargeFactor times its
    // charge: a full charge, kFullChargePower as strong (damage and healing): a long hold for a
    // much harder hit. The client flags it (CAST_FULL) and the server holds the hand to the time
    // (FullChargeServed: since the hand's last cast). Each hand's release carries its own power
    // (FusionHandBolt::power, FusionClassState::releasePower), never the other hand's. A dual cast
    // of two full charges is both, never past kMaxReleasePower.
    constexpr float kFullChargeFactor = 2.75f;
    constexpr float kFullChargePower = 2.25f;
    constexpr float kMaxReleasePower = 2.8f;
    uint16_t FullChargeMs(uint16_t chargeMs);
    bool FullChargeServed(uint32_t lastCastMs, bool castBefore, uint32_t nowMs, uint16_t chargeMs);
    // A release's power: dual, full, both (capped), or 1.
    float ReleasePower(bool dual, bool full);

    // Both hands holding the same spell, released together: one stronger cast.
    constexpr float kDualPower = 1.5f;      // damage and healing
    constexpr float kDualRadius = 1.5f;     // an area's radius
    // How long a dual cast's power holds for the spell's own hits (its travel; an area's lasting
    // ground is the spell's duration, given by the game).
    constexpr uint32_t kDualWindowMs = 1500;

    // ── Where it lands ──
    constexpr float kRangeSlack = 2.f;      // yards past the spell's reach the server lets pass
    constexpr float kAreaReach = 30.f;      // an area spell whose own range was "self"
    bool InReach(Vec3 from, Vec3 point, float range);
    // A bolt released at the ground lands on the enemy nearest the point, within this many yards
    // of its edge.
    constexpr float kBoltSnap = 2.5f;
    struct Candidate
    {
        uint64_t guid = 0;
        Vec3 at;                    // its feet
        float radius = 0.f;         // its bounding radius
    };
    uint64_t BoltVictim(Vec3 point, std::vector<Candidate> const& units);
    // Who a heal lands on: the crosshair's friend, else oneself; the self key always oneself.
    bool HealsSelf(bool crosshairFriend, bool selfKey);

    // ── Free-aimed bolts (Shape::Missile) ──
    // A hostile spell reaching past melee is a bolt: it leaves the caster's eye along the aim at
    // its Spell.dbc speed (0: it strikes at once, along the ray) and hits the first body, wall,
    // piece or ground on its path. It has no range: it flies until it strikes something, or until
    // its fizzle distance if it has one (BoltTraits), or the technical safety cap. The server flies
    // it and decides.
    constexpr float kMeleeReach = 6.f;          // within this a hostile spell stays a touch (Bolt)
    constexpr float kBoltRadius = 0.3f;         // a bolt's own width: grazing a body hits it
    constexpr float kClaimSlack = 1.f;          // an instant bolt: the client's crosshair unit
                                                // counts if the ray passes this close to it
    constexpr float kUnitBeforeWall = 0.25f;    // a body flush against a wall is hit first
    // Purely technical caps, not ranges: no bolt flies farther or longer (the world past 200 yd
    // is not loaded around the caster anyway).
    constexpr float kBoltSafetyYards = 200.f;
    constexpr uint32_t kMaxFlightMs = 12000;
    constexpr size_t kMaxBoltsInFlight = 16;    // per caster
    constexpr uint32_t kVolleyGapMs = 250;      // a missile channel's missiles leave this far apart
    constexpr uint8_t kMaxVolley = 6;

    // Where a bolt leaves: the client's eye if it is within `tolerance` of the server's, else the
    // server's (a moved or lying origin can't shoot from elsewhere).
    Vec3 BoltOrigin(Vec3 claimed, Vec3 eye, float tolerance);
    // The aim as a unit vector; false for no direction (zero, not finite).
    bool AimDirection(Vec3 claimed, Vec3& out);

    // ── What a bolt does in flight and on impact (data, per spell; talents and perks add) ──
    // Homing: the bolt curves toward its target at a limited turn rate, so a target that moves
    // across it fast enough, or close enough, still makes it miss. 0 degrees a second: straight.
    struct Homing
    {
        float turnDegPerSec = 0.f;
        float coneDeg = 0.f;        // the crosshair's cone it picks an unselected target in
        bool On() const { return turnDegPerSec > 0.f; }
    };
    // What talents, perks and kit options grant a class's bolts (FusionClasses::GrantHoming):
    // a modest turn, so the bolt still has to be aimed roughly.
    constexpr Homing kGrantedHoming = {120.f, 12.f};
    // A spell's row, by its chain's first rank (the spell released: Arcane Missiles' channel, not
    // its missile). Unlisted spells take the defaults below.
    struct BoltRow
    {
        uint32_t firstRank = 0;
        float fizzleYards = 0.f;    // > 0: it fizzles out this far from the hand; 0 the default;
                                    // < 0: never (an instant ray's default lifted)
        float splashYards = -1.f;   // < 0 the default; 0 no splash
        Homing homing;
    };
    std::vector<BoltRow> const& BoltRows();
    BoltRow const* BoltRowOf(uint32_t firstRank);
    // What the server reads off the spell for the defaults.
    struct BoltFacts
    {
        uint32_t firstRank = 0;
        float speed = 0.f;          // Spell.dbc speed: 0 an instant ray
        float rangeYards = 0.f;     // its SpellRange.dbc reach (only an instant ray's fizzle uses it)
        float ownArea = 0.f;        // its own enemy area about the destination (Spell.dbc radius)
        bool damages = false;       // direct damage, a leech or a damage over time
    };
    struct BoltTraits
    {
        float fizzleYards = 0.f;    // 0: none (it flies to the safety cap)
        float splashYards = 0.f;    // 0: none
        bool ownArea = false;       // the spell's own area does the splash (full strength, its own)
        Homing homing;
    };
    // The defaults: a flying bolt never fizzles; an instant ray (a hit-scan, Mind Blast) fizzles at
    // kInstantFizzleScale times its spell's reach, so it is not a 200-yard sniper rifle; a
    // damaging bolt splashes kDefaultSplashYards around where it strikes (its own area instead when
    // it has one: Arcane Explosion-style areas keep their full strength); a spell with no damage
    // (Polymorph, Fear, a curse with no harm) strikes only what it hits. Nothing homes.
    constexpr float kInstantFizzleScale = 2.f;
    constexpr float kDefaultSplashYards = 2.5f;
    BoltTraits TraitsOf(BoltFacts const& f);
    // A grant on top: the stronger turn and the wider cone win.
    BoltTraits WithHoming(BoltTraits t, Homing h);
    // How far a bolt flies: its fizzle distance, else the safety cap.
    float BoltReach(BoltTraits const& t);
    // Its flight time over `yards` (0 at speed 0, capped at kMaxFlightMs).
    uint32_t FlightMs(float yards, float speed);
    // The yards a bolt covers in `dtMs`, never past what it has `left` (all of it at speed 0).
    float StepYards(float speed, uint32_t dtMs, float left);
    // How many missiles a missile channel fires: its ticks, 1..kMaxVolley.
    uint8_t VolleySize(int32_t durationMs, uint32_t amplitudeMs);

    // A body a bolt can strike.
    struct Body
    {
        uint64_t guid = 0;
        BodyPose pose;
        HitboxShape shape;
    };
    // The nearest body a segment from `from` along unit `dir` for `length` yards strikes, widened
    // by `width` (a start inside a body strikes it at 0): its index into `bodies` and the distance.
    struct BodyStrike
    {
        int index = -1;
        float distance = 0.f;
    };
    BodyStrike FirstBody(Vec3 from, Vec3 dir, float length, float width, std::vector<Body> const& bodies);
    // An instant bolt's grace: whether the ray passes within kClaimSlack (plus the body) of the
    // claimed body before `upTo`; its distance in `at`.
    bool ClaimHolds(Vec3 from, Vec3 dir, float upTo, Body const& claimed, float& at);

    enum class StrikeKind : uint8_t
    {
        None = 0,
        Unit,
        World,      // a building, a rock, the terrain
        Piece,      // a built piece (fusion/Build.h)
    };
    struct Strike
    {
        StrikeKind kind = StrikeKind::None;
        float distance = 0.f;
        int body = -1;              // Unit: the index in the bodies
        uint32_t piece = 0;         // Piece: its id
    };
    // The first thing a step meets: the nearest body (`unit`), the world (`world`, its distance or
    // < 0 for none) or a piece (`piece` id, 0 none, at `pieceAt`). A body flush in front of a wall
    // (within kUnitBeforeWall) is hit first.
    Strike FirstStrike(BodyStrike unit, float world, uint32_t piece, float pieceAt);

    // The bodies within `radius` of a point (an impact's splash): the distance from the point to
    // each body's upright capsule.
    std::vector<size_t> InBlast(Vec3 at, float radius, std::vector<Body> const& bodies);

    // ── Splash: a bolt need not hit to hurt ──
    // Where a bolt strikes (a body or the world), the enemies around take the spell too, weaker the
    // farther from the point. The one it hit takes it whole; a near miss at the centre of the blast
    // kSplashCentre of it, falling to kSplashEdge at the radius: aim pays, a near miss still
    // counts, and a crowd hit at its feet does not take full damage each. Only enemies the
    // impact can see take it (walls, built pieces and the terrain are cover).
    constexpr float kSplashCentre = 0.75f;
    constexpr float kSplashEdge = 0.5f;
    // The share of the spell a body takes at `distance` yards from the impact (to its capsule): 1
    // for the body hit, kSplashCentre..kSplashEdge within `radius`, 0 beyond.
    float SplashScale(float distance, float radius, bool direct);
    struct SplashHit
    {
        size_t body = 0;
        float scale = 0.f;
    };
    // The bodies a blast at `at` takes, but `direct` (the body the bolt hit, -1 none), each with its
    // share; `sees(from, to)` is the line of sight from the impact to a body's chest.
    std::vector<SplashHit> Splash(Vec3 at, float radius, std::vector<Body> const& bodies, int direct,
        std::function<bool(Vec3, Vec3)> const& sees);
    // A body's chest, where sight to it is drawn and a homing bolt steers.
    Vec3 BodyCentre(Body const& b);

    // What a splashed enemy takes of a spell's auras, by aura type (SpellAuraDefines.h numbering).
    // Hard control (Polymorph, a stun, a fear, a root, a silence, a charm) lands only on a direct
    // hit: a near miss never turns a crowd into sheep. Damage over time splashes at the splash's
    // share of its damage; slows and other debuffs at the share of their duration.
    enum class AuraKind : uint8_t
    {
        Other = 0,
        HardControl,
        Slow,
        DamageOverTime,
    };
    AuraKind KindOfAura(uint32_t auraType);
    // Spell effects (SpellDefines.h numbering) a splash drops as control: an interrupt, a knock-back.
    bool ControlEffect(uint32_t effect);
    // A splashed slow or debuff's length: its share, never under kSplashMinAuraMs.
    constexpr uint32_t kSplashMinAuraMs = 500;
    uint32_t SplashAuraMs(uint32_t fullMs, float scale);

    // ── Homing ──
    // Who a homing bolt goes for, picked at its launch: the caster's selected (Tab) target if it
    // is within kSelectedConeDeg of the aim and kHomingAcquireYards, else the enemy nearest the
    // crosshair's ray within the bolt's cone; -1 for none (it flies straight).
    constexpr float kSelectedConeDeg = 45.f;
    constexpr float kHomingAcquireYards = 80.f;
    int HomingTarget(Vec3 from, Vec3 dir, float coneDeg, std::vector<Body> const& bodies, uint64_t selected);
    // `dir` (unit) turned toward `want` by at most `maxRadians`: a homing bolt's turn in one step.
    Vec3 SteerToward(Vec3 dir, Vec3 want, float maxRadians);
    // How often a homing bolt's course is told to those near (its caster too) as it turns.
    constexpr uint32_t kSteerTellMs = 150;

    // ── Sprays (Shape::Spray) ──
    // Held like a flamethrower: while the button is down the hand pours a short cone in front,
    // and every tick each enemy in the cone it can see takes the damage, the mana is paid, and a
    // frost spray chills. Let go, out of mana, silenced or stunned: it stops.
    //
    // The damage is measured against the class's own bolt, so a held spray trades with casting the
    // bolt over the same time: each tick deals `boltShare` of what the bolt deals over a tick when
    // cast as fast as its charge allows (one cast a charge from one hand). The bolt is the rank the
    // caster's level allows, its hit the average with the caster's spell power and talents as a
    // cast of it would have them; the tick is then a spell hit of the spray's own (its school, the
    // victim's resistances and taken modifiers, its crit), onto the CoD-scale pools as any spell.
    struct SprayRow
    {
        uint32_t firstRank = 0;
        float rangeYards = 0.f;
        float halfAngleDeg = 0.f;
        uint16_t tickMs = 250;
        uint32_t boltChain = 0;     // the bolt it is measured against (its first rank)
        float boltShare = 0.f;      // of the bolt's sustained damage a second, per enemy in the cone
        float manaPctPerTick = 0.f; // mana a tick, % of the caster's maximum
        uint32_t chillMs = 0;       // a frost spray's slow on whoever it touches (0: none)
    };
    std::vector<SprayRow> const& SprayRows();
    SprayRow const* SprayOf(uint32_t firstRank);
    // Whether a body is in the cone from `eye` along unit `dir`: some point of its upright axis
    // within the range and the angle (widened by the body's own radius).
    bool InSpray(Vec3 eye, Vec3 dir, SprayRow const& row, Body const& b);
    // The bodies a tick catches: in the cone and in sight of the eye.
    std::vector<size_t> SprayCatches(Vec3 eye, Vec3 dir, SprayRow const& row, std::vector<Body> const& bodies,
        std::function<bool(Vec3, Vec3)> const& sees);
    // A tick's damage before the victim (WoW scale): `row.boltShare` of the bolt's average hit
    // `boltHit` once per `boltChargeMs`, over the tick. 0 without a bolt or a charge.
    float SprayTickDamage(SprayRow const& row, float boltHit, uint16_t boltChargeMs);
    // A bolt rank known below its own level (a level 2 mage's Frostbolt) hits as hard as the level
    // allows: its share by level, at least a quarter; 1 at or past the rank's level.
    float BelowRankScale(uint32_t level, uint32_t rankLevel);
    // A tick's mana from the caster's maximum, at least 1.
    uint32_t SprayTickMana(uint32_t maxMana, SprayRow const& row);
    // The client re-sends the aim at kSprayAimEveryMs while the button is down; the server stops a
    // spray it has not heard of for kSprayAimTimeoutMs (a lost release), and tells those near of
    // its course at most every kSprayTellMs.
    constexpr uint32_t kSprayAimEveryMs = 100;
    constexpr uint32_t kSprayAimTimeoutMs = 600;
    constexpr uint32_t kSprayTellMs = 150;

    // ── The wire: channel 7 (Fusion::Classes::kChannelClasses), little-endian, first byte the op ──
    constexpr uint8_t kOpHandSpells = 9;    // server: the hand spells, see HandSpell
    constexpr uint8_t kOpCastResult = 10;   // server: [10, hand u8, error u8, spell u32]
    constexpr uint8_t kOpHandCast = 12;     // client: a release, see HandCast
    constexpr uint8_t kOpZone = 13;         // server: [13, spell u32, x f32, y f32, z f32, radius f32, ms u16]
    constexpr uint8_t kOpHandAim = 41;      // client: a release with its aim, see HandAim
    constexpr uint8_t kOpBolt = 42;         // server: a bolt launched or ended, see BoltEvent

    // The hands as the mouse fires them: the left button's hand, the right button's, both at once.
    constexpr uint8_t kHandLeftButton = 0;
    constexpr uint8_t kHandRightButton = 1;
    constexpr uint8_t kHandBoth = 2;

    enum HandCastFlags : uint8_t
    {
        CAST_POINT = 0x01,          // the crosshair is on the world: `point` is where
        CAST_SELF = 0x02,           // the self key held: a heal on oneself
        CAST_FULL = 0x04,           // held to a full charge (kFullChargeFactor)
    };
    // [12, spell u32, hand u8, flags u8, target u64, x f32, y f32, z f32]: the crosshair's unit (0
    // none) and point at the release.
    struct HandCast
    {
        uint32_t spell = 0;
        uint8_t hand = 0;
        uint8_t flags = 0;
        uint64_t target = 0;
        Vec3 point;
    };
    constexpr size_t kHandCastBytes = 27;
    std::vector<uint8_t> EncodeHandCast(HandCast const& c);
    bool DecodeHandCast(std::vector<uint8_t> const& body, HandCast& out);

    // [41, spell u32, hand u8, flags u8, target u64, point xyz f32, origin xyz f32, dir xyz f32]:
    // a release as op 12, with the ray it was aimed along (the camera's, in third person the
    // eye's), so a bolt flies where the crosshair points, at anything.
    struct HandAim
    {
        HandCast cast;
        Vec3 origin;
        Vec3 dir;
    };
    constexpr size_t kHandAimBytes = 51;
    std::vector<uint8_t> EncodeHandAim(HandAim const& a);
    bool DecodeHandAim(std::vector<uint8_t> const& body, HandAim& out);

    // [42, kind u8, id u32, spell u32, caster u64, ...]:
    //   Launch: from xyz f32, to xyz f32, speed f32, flags u8: a bolt left `from` and, as things
    //           stand, ends at `to` (others draw it; the caster drew its own at the release). A
    //           homing bolt's Launch goes to its caster too and comes again, from where the bolt
    //           is, as it turns: the bolt of that id flies on toward the new end.
    //   Impact: at xyz f32, unit u64 (0: the world), flags u8 (BoltImpactFlags): it ended there.
    enum class BoltEventKind : uint8_t
    {
        Launch = 1,
        Impact = 2,
    };
    enum BoltImpactFlags : uint8_t
    {
        BOLT_HIT_PIECE = 0x01,      // a built piece took it
        BOLT_SPENT = 0x02,          // it ended in the air and fades: no impact to show
        BOLT_VOLLEY = 0x04,         // Launch: a missile channel's missile (the caster draws these
                                    // from the server too; its own bolts it drew at the release)
        BOLT_FIZZLED = 0x08,        // Impact (with BOLT_SPENT): it fizzled out at its fizzle
                                    // distance: the fizzle's puff at `to`
        BOLT_HOMING = 0x10,         // Launch: a homing bolt's course (to its caster too)
    };
    struct BoltEvent
    {
        BoltEventKind kind = BoltEventKind::Launch;
        uint32_t id = 0;
        uint32_t spell = 0;
        uint64_t caster = 0;
        Vec3 from;                  // Launch
        Vec3 to;                    // Launch: the end foreseen; Impact: where it ended
        float speed = 0.f;          // Launch
        uint64_t unit = 0;          // Impact
        uint8_t flags = 0;
    };
    constexpr size_t kBoltLaunchBytes = 47;
    constexpr size_t kBoltImpactBytes = 39;
    std::vector<uint8_t> EncodeBoltEvent(BoltEvent const& e);
    bool DecodeBoltEvent(std::vector<uint8_t> const& body, BoltEvent& out);

    // [9, n u16, n x (spell u32, charge_ms u16, shape u8, radius u8)]: every rank a hand can hold,
    // how long it charges, how it lands and how wide (yards; 0 none).
    struct HandSpell
    {
        uint32_t spell = 0;
        uint16_t chargeMs = 0;
        Shape shape = Shape::Bolt;
        uint8_t radius = 0;
    };
    constexpr size_t kHandSpellBytes = 8;
    std::vector<std::vector<uint8_t>> EncodeHandSpells(std::vector<HandSpell> const& spells, size_t maxBody);
    bool DecodeHandSpells(std::vector<uint8_t> const& body, std::vector<HandSpell>& out);

    // Why a release cast nothing (the client's hand fizzles).
    enum class CastError : uint8_t
    {
        None = 0,
        NotHeld,        // not a spell a hand holds, or not known
        Charging,       // the hand released before its charge could have passed
        OutOfRange,
        NoSight,        // a wall between the caster and the point
        NoTarget,       // a bolt released at nothing
        NoPoint,        // an area released at the sky
        Refused,        // the spell's own checks (a cooldown, silenced, dead)
    };
    std::vector<uint8_t> EncodeCastResult(uint8_t hand, CastError e, uint32_t spell);
    bool DecodeCastResult(std::vector<uint8_t> const& body, uint8_t& hand, CastError& e, uint32_t& spell);

    // A lasting zone a release left (Moonfire's), for the client to draw.
    struct Zone
    {
        uint32_t spell = 0;
        Vec3 at;
        float radius = 0.f;
        uint16_t ms = 0;
    };
    std::vector<uint8_t> EncodeZone(Zone const& z);
    bool DecodeZone(std::vector<uint8_t> const& body, Zone& out);

    // [43, spell u32, hand u8, on u8, origin xyz f32, dir xyz f32]: a spray held (on 1: started or
    // its aim again, every kSprayAimEveryMs) or let go (on 0) by a hand (kHandLeftButton or
    // kHandRightButton), with the crosshair's ray as op 41's.
    struct SprayAim
    {
        uint32_t spell = 0;
        uint8_t hand = 0;
        bool on = false;
        Vec3 origin;
        Vec3 dir;
    };
    constexpr uint8_t kOpSprayAim = 43;     // client
    constexpr size_t kSprayAimBytes = 31;
    std::vector<uint8_t> EncodeSprayAim(SprayAim const& a);
    bool DecodeSprayAim(std::vector<uint8_t> const& body, SprayAim& out);

    // [44, caster u64, spell u32, hand u8, on u8, from xyz f32, dir xyz f32, range f32,
    // half_angle_deg f32]: a spray pouring (on 1, again as its aim moves) or stopped (on 0), for
    // everyone near to draw (its caster draws its own from the hand).
    struct SprayEvent
    {
        uint64_t caster = 0;
        uint32_t spell = 0;
        uint8_t hand = 0;
        bool on = false;
        Vec3 from;
        Vec3 dir;
        float range = 0.f;
        float halfAngleDeg = 0.f;
    };
    constexpr uint8_t kOpSpray = 44;        // server
    constexpr size_t kSprayEventBytes = 47;
    std::vector<uint8_t> EncodeSprayEvent(SprayEvent const& e);
    bool DecodeSprayEvent(std::vector<uint8_t> const& body, SprayEvent& out);
}

#endif
