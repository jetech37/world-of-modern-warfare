/*
 * World of Modern Warfare: spells cast from the hands (fusion/Spellcasting.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Spellcasting.h"

#include <limits>

using namespace Fusion;
using namespace Fusion::Spellcasting;

namespace
{
    SpellFacts Facts(uint32_t first, int32_t castMs, bool positive = false)
    {
        SpellFacts f;
        f.firstRank = first;
        f.castMs = castMs;
        f.positive = positive;
        return f;
    }
}

TEST(a_hand_holds_spells_with_a_reach_and_the_area_spells)
{
    // Fireball, Flash Heal: a reach and a school: held.
    CHECK(InHand(Facts(133, 2000)));
    CHECK(InHand(Facts(2061, 1000, true)));
    // A shout or a buff on oneself, a physical move, Heroic Strike: not held.
    SpellFacts self = Facts(6673, 0);
    self.selfRange = true;
    CHECK(!InHand(self));
    SpellFacts move = Facts(100, 0);
    move.physical = true;
    CHECK(!InHand(move));
    SpellFacts swing = Facts(78, 0);
    swing.physical = true;
    swing.nextSwing = true;
    CHECK(!InHand(swing));
    // Frost Nova and Consecration were cast on oneself; a hand aims them now.
    SpellFacts nova = Facts(122, 0);
    nova.selfRange = true;
    CHECK(InHand(nova) && ShapeOf(nova) == Shape::Area);
    SpellFacts consecration = Facts(26573, 0);
    consecration.selfRange = true;
    CHECK(InHand(consecration) && ShapeOf(consecration) == Shape::Area);
    SpellFacts passive = Facts(133, 0);
    passive.passive = true;
    CHECK(!InHand(passive));
}

TEST(a_release_lands_by_the_spells_kind)
{
    CHECK(ShapeOf(Facts(133, 2000)) == Shape::Bolt);         // Fireball
    CHECK(ShapeOf(Facts(2061, 1000, true)) == Shape::Heal);  // Flash Heal
    CHECK(ShapeOf(Facts(8921, 0)) == Shape::Splash);         // Moonfire: the ground at the crosshair
    AreaRow const* moon = AreaOf(8921);
    CHECK(moon && moon->zoneMs == 6000 && moon->zoneTickMs == 1000);
    CHECK(AreaOf(133) == nullptr);
    // One ground spell per caster class at most.
    CHECK(AreaRows().size() >= 4);
}

TEST(light_spells_charge_quick_and_heavy_or_area_ones_longer)
{
    CHECK(ChargeMs(Facts(5176, 0)) == kChargeInstantMs);            // an instant
    CHECK(ChargeMs(Facts(116, 1500)) == kChargeQuickMs);            // Frostbolt as fitted, 1.5 s
    CHECK(ChargeMs(Facts(5176, 2000)) == kChargeMediumMs);          // Wrath, 2 s
    CHECK(ChargeMs(Facts(133, 2000)) == kChargeMediumMs);           // Fireball as fitted, 2 s
    CHECK(ChargeMs(Facts(686, 3000)) == kChargeHeavyMs);            // Shadow Bolt, 3 s
    SpellFacts missiles = Facts(5143, 0);
    missiles.channeled = true;
    CHECK(ChargeMs(missiles) == kChargeChannelMs);
    CHECK(ChargeMs(Facts(8921, 0)) == kChargeAreaMs);               // Moonfire on the ground
    // The light ones sit within 0.3-0.6 s, the rest longer.
    CHECK(kChargeInstantMs >= 300 && kChargeMediumMs <= 600);
    CHECK(kChargeHeavyMs > kChargeMediumMs && kChargeAreaMs > kChargeMediumMs);
}

TEST(the_server_holds_a_hand_to_one_cast_a_charge)
{
    // The first cast goes; the next once the charge (less the slack) has passed.
    CHECK(ChargeServed(0, false, 1000, 600));
    CHECK(!ChargeServed(1000, true, 1300, 600));
    CHECK(ChargeServed(1000, true, 1000 + 600 - kChargeSlackMs, 600));
    CHECK(ChargeServed(1000, true, 1700, 600));
    // Across the world clock's wrap.
    CHECK(ChargeServed(0xFFFFFF00u, true, 0x00000300u, 600));
    CHECK(!ChargeServed(0xFFFFFF00u, true, 0x00000010u, 600));
}

TEST(a_release_must_land_within_reach)
{
    Vec3 const me{0.f, 0.f, 0.f};
    CHECK(InReach(me, {30.f, 0.f, 0.f}, 30.f));
    CHECK(InReach(me, {31.5f, 0.f, 0.f}, 30.f));            // the slack
    CHECK(!InReach(me, {33.f, 0.f, 0.f}, 30.f));
    CHECK(!InReach(me, {20.f, 20.f, 20.f}, 30.f));          // in three dimensions
}

TEST(a_bolt_at_the_ground_finds_the_enemy_standing_there)
{
    std::vector<Candidate> units = {
        {1, {10.f, 0.f, 0.f}, 0.5f},
        {2, {14.f, 0.f, 0.f}, 1.5f},
    };
    // At its feet, and on its chest.
    CHECK(BoltVictim({10.2f, 0.f, 0.f}, units) == 1);
    CHECK(BoltVictim({10.f, 0.f, 1.5f}, units) == 1);
    // The nearer edge wins: a big one's.
    CHECK(BoltVictim({12.6f, 0.f, 0.f}, units) == 2);
    // Nothing there: nothing hit.
    CHECK(BoltVictim({30.f, 0.f, 0.f}, units) == 0);
    CHECK(BoltVictim({10.f, 0.f, 9.f}, units) == 0);        // high above it
    CHECK(BoltVictim({0.f, 0.f, 0.f}, {}) == 0);
}

TEST(a_heal_lands_on_the_crosshairs_friend_or_oneself)
{
    CHECK(!HealsSelf(true, false));
    CHECK(HealsSelf(false, false));
    CHECK(HealsSelf(true, true));
}

TEST(hand_casts_round_trip_the_wire)
{
    HandCast c;
    c.spell = 25306;
    c.hand = kHandBoth;
    c.flags = CAST_POINT | CAST_SELF;
    c.target = 0xF130000000001234ull;
    c.point = {-8913.5f, 554.25f, 93.75f};
    std::vector<uint8_t> const b = EncodeHandCast(c);
    CHECK(b.size() == kHandCastBytes && b[0] == kOpHandCast);
    HandCast d;
    CHECK(DecodeHandCast(b, d));
    CHECK(d.spell == c.spell && d.hand == c.hand && d.flags == c.flags && d.target == c.target);
    CHECK(d.point.x == c.point.x && d.point.y == c.point.y && d.point.z == c.point.z);
    // Short, long, a third hand, a point that is no number: refused.
    std::vector<uint8_t> shorter(b.begin(), b.end() - 1);
    CHECK(!DecodeHandCast(shorter, d));
    std::vector<uint8_t> longer = b;
    longer.push_back(0);
    CHECK(!DecodeHandCast(longer, d));
    c.hand = 3;
    CHECK(!DecodeHandCast(EncodeHandCast(c), d));
    c.hand = 0;
    c.point.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!DecodeHandCast(EncodeHandCast(c), d));
}

TEST(hand_spells_go_in_pieces_under_the_channel_size)
{
    std::vector<HandSpell> spells;
    for (uint32_t i = 0; i < 300; ++i)
        spells.push_back({1000 + i, uint16_t(300 + i), i % 2 ? Shape::Splash : Shape::Bolt, uint8_t(i % 16)});
    std::vector<std::vector<uint8_t>> const bodies = EncodeHandSpells(spells, 1000);
    CHECK(bodies.size() >= 3);
    std::vector<HandSpell> all;
    for (std::vector<uint8_t> const& b : bodies)
    {
        CHECK(b.size() <= 1000);
        std::vector<HandSpell> part;
        CHECK(DecodeHandSpells(b, part));
        all.insert(all.end(), part.begin(), part.end());
    }
    CHECK(all.size() == spells.size());
    CHECK(all[7].spell == 1007 && all[7].chargeMs == 307 && all[7].shape == Shape::Splash && all[7].radius == 7);
}

TEST(cast_results_and_zones_round_trip_the_wire)
{
    uint8_t hand = 0;
    CastError e = CastError::None;
    uint32_t spell = 0;
    CHECK(DecodeCastResult(EncodeCastResult(1, CastError::NoSight, 9835), hand, e, spell));
    CHECK(hand == 1 && e == CastError::NoSight && spell == 9835);
    Zone z{9835, {1.f, 2.f, 3.f}, 12.f, 6000};
    Zone d;
    CHECK(DecodeZone(EncodeZone(z), d));
    CHECK(d.spell == 9835 && d.at.y == 2.f && d.radius == 12.f && d.ms == 6000);
    CHECK(!DecodeZone(EncodeCastResult(0, CastError::None, 1), d));
}

// ── Free-aimed bolts ──

namespace
{
    SpellFacts Ranged(uint32_t first, int32_t castMs, float range)
    {
        SpellFacts f = Facts(first, castMs);
        f.rangeYards = range;
        return f;
    }

    Body Standing(uint64_t guid, Vec3 feet)
    {
        Body b;
        b.guid = guid;
        b.pose.feet = feet;
        return b;
    }
}

TEST(hostile_spells_with_reach_fly_free_and_touches_stay)
{
    // Fireball, Shadow Bolt, Polymorph: thirty-odd yards, free-aimed.
    CHECK(ShapeOf(Ranged(133, 2000, 35.f)) == Shape::Missile);
    CHECK(ShapeOf(Ranged(686, 3000, 30.f)) == Shape::Missile);
    // A touch (melee reach) keeps landing on the crosshair's enemy.
    CHECK(ShapeOf(Ranged(20271, 0, 5.f)) == Shape::Bolt);
    // A beam channel holds on its unit; a missile channel (Arcane Missiles) flies.
    SpellFacts drain = Ranged(689, 0, 30.f);
    drain.channeled = true;
    CHECK(ShapeOf(drain) == Shape::Bolt);
    SpellFacts missiles = Ranged(5143, 0, 30.f);
    missiles.channeled = true;
    missiles.missileChannel = true;
    CHECK(ShapeOf(missiles) == Shape::Missile);
    CHECK(ChargeMs(missiles) == kChargeChannelMs);
    // Blizzard, Flamestrike: the spell's own ground area at the point, charged as an area.
    SpellFacts blizzard = Ranged(10, 0, 30.f);
    blizzard.channeled = true;
    blizzard.groundTarget = true;
    CHECK(ShapeOf(blizzard) == Shape::Ground);
    CHECK(ChargeMs(blizzard) == kChargeAreaMs);
    // Heals stay heals, whatever their reach; the area rows stay theirs.
    SpellFacts heal = Ranged(2061, 1500, 40.f);
    heal.positive = true;
    CHECK(ShapeOf(heal) == Shape::Heal);
    CHECK(ShapeOf(Ranged(8921, 0, 30.f)) == Shape::Splash);
}

TEST(a_bolt_leaves_the_eye_and_flies_its_reach)
{
    Vec3 const eye{100.f, 50.f, 11.6f};
    // The client's eye stands when near the server's; far off (or no number), the server's.
    Vec3 const near{100.5f, 50.f, 11.6f};
    Vec3 o = BoltOrigin(near, eye, 3.f);
    CHECK(o.x == near.x);
    o = BoltOrigin({140.f, 50.f, 11.6f}, eye, 3.f);
    CHECK(o.x == eye.x && o.y == eye.y && o.z == eye.z);
    o = BoltOrigin({std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}, eye, 3.f);
    CHECK(o.x == eye.x);
    // The aim is made a unit vector; nothing or no number is no aim.
    Vec3 d;
    CHECK(AimDirection({0.f, 3.f, 4.f}, d));
    CHECK_NEAR(d.Length(), 1.f, 1e-5);
    CHECK_NEAR(d.z, 0.8f, 1e-5);
    CHECK(!AimDirection({0.f, 0.f, 0.f}, d));
    CHECK(!AimDirection({std::numeric_limits<float>::infinity(), 0.f, 0.f}, d));
    // Reach and travel: Fireball's 35 yards at 24 yards a second, a bit under 1.5 s.
    CHECK_NEAR(BoltReach(BoltTraits{}), kBoltSafetyYards, 1e-5);
    CHECK(FlightMs(36.f, 24.f) == 1500);
    CHECK(FlightMs(36.f, 0.f) == 0);                  // an instant strikes at once
    CHECK(FlightMs(10000.f, 1.f) == kMaxFlightMs);    // never longer than the cap
    // A step covers speed x time, never past what is left; an instant covers all of it.
    CHECK_NEAR(StepYards(24.f, 100, 30.f), 2.4f, 1e-5);
    CHECK_NEAR(StepYards(24.f, 2000, 30.f), 30.f, 1e-5);
    CHECK_NEAR(StepYards(0.f, 50, 37.f), 37.f, 1e-5);
    CHECK(StepYards(24.f, 100, 0.f) == 0.f);
    // Arcane Missiles: one missile a tick.
    CHECK(VolleySize(5000, 1000) == 5);
    CHECK(VolleySize(3000, 1000) == 3);
    CHECK(VolleySize(0, 1000) == 1);
    CHECK(VolleySize(60000, 100) == kMaxVolley);
}

TEST(a_bolt_strikes_the_first_body_on_its_path)
{
    std::vector<Body> bodies = {Standing(1, {20.f, 0.f, 0.f}), Standing(2, {10.f, 0.f, 0.f}),
        Standing(3, {15.f, 3.f, 0.f})};
    Vec3 const from{0.f, 0.f, 1.2f};
    Vec3 const east{1.f, 0.f, 0.f};
    // The nearer of two in line.
    BodyStrike s = FirstBody(from, east, 40.f, kBoltRadius, bodies);
    CHECK(s.index == 1);
    CHECK_NEAR(s.distance, 10.f - 0.45f - kBoltRadius, 0.05);
    // Short of it: nothing this step.
    CHECK(FirstBody(from, east, 9.f, kBoltRadius, bodies).index < 0);
    // Aimed at the one off the line: struck; passing well wide of it: not.
    Vec3 const toThird = (Vec3{15.f, 3.f, 1.2f} - from).Normalized();
    s = FirstBody(from, toThird, 40.f, kBoltRadius, bodies);
    CHECK(s.index == 2);
    Vec3 const wide = (Vec3{15.f, 4.5f, 1.2f} - from).Normalized();
    CHECK(FirstBody(from, wide, 40.f, kBoltRadius, bodies).index < 0);
    // Over the head: a miss.
    Vec3 const high = (Vec3{10.f, 0.f, 3.f} - from).Normalized();
    CHECK(FirstBody(from, high, 12.f, kBoltRadius, {bodies[1]}).index < 0);
    // A unit that stepped onto the bolt is struck where the step starts.
    s = FirstBody({10.f, 0.1f, 1.f}, east, 2.f, kBoltRadius, bodies);
    CHECK(s.index == 1 && s.distance == 0.f);
    // A target that walked off the line between steps is missed: it can be dodged.
    std::vector<Body> moved = {Standing(2, {10.f, 2.f, 0.f})};
    CHECK(FirstBody(from, east, 40.f, kBoltRadius, moved).index < 0);
}

TEST(an_instant_bolt_forgives_the_crosshair_unit_by_a_little)
{
    Body const b = Standing(7, {20.f, 0.f, 0.f});
    Vec3 const from{0.f, 0.f, 1.2f};
    float at = 0.f;
    // Passing 1.2 yards to its side (0.75 off its edge): it counts.
    Vec3 const past = (Vec3{20.f, 1.2f, 1.2f} - from).Normalized();
    CHECK(ClaimHolds(from, past, 40.f, b, at));
    CHECK(at > 17.f && at < 21.f);
    // Past the wall the server found first: no.
    CHECK(!ClaimHolds(from, past, 15.f, b, at));
    // Two yards wide: no.
    Vec3 const wide = (Vec3{20.f, 2.2f, 1.2f} - from).Normalized();
    CHECK(!ClaimHolds(from, wide, 40.f, b, at));
}

TEST(the_nearest_of_body_wall_and_piece_is_struck)
{
    BodyStrike none;
    BodyStrike unit;
    unit.index = 0;
    unit.distance = 12.f;
    // Nothing at all.
    CHECK(FirstStrike(none, -1.f, 0, 0.f).kind == StrikeKind::None);
    // The world in front of the unit.
    Strike s = FirstStrike(unit, 8.f, 0, 0.f);
    CHECK(s.kind == StrikeKind::World && s.distance == 8.f);
    // The unit in front of the world, and one flush against the wall.
    CHECK(FirstStrike(unit, 20.f, 0, 0.f).kind == StrikeKind::Unit);
    s = FirstStrike(unit, 11.9f, 0, 0.f);
    CHECK(s.kind == StrikeKind::Unit && s.body == 0 && s.distance == 12.f);
    // A built piece nearer than the wall takes it; behind the unit, the unit does.
    s = FirstStrike(none, 20.f, 42, 6.f);
    CHECK(s.kind == StrikeKind::Piece && s.piece == 42 && s.distance == 6.f);
    s = FirstStrike(unit, -1.f, 42, 15.f);
    CHECK(s.kind == StrikeKind::Unit && s.piece == 0);
    CHECK(FirstStrike(none, 3.f, 42, 6.f).kind == StrikeKind::World);
}

TEST(an_impacts_splash_takes_the_bodies_around_the_point)
{
    std::vector<Body> bodies = {Standing(1, {0.f, 0.f, 0.f}), Standing(2, {2.5f, 0.f, 0.f}),
        Standing(3, {9.f, 0.f, 0.f}), Standing(4, {0.f, 0.f, 6.5f})};
    // A blast at a knee within 2 yards: the first two; the third and the one on the ledge above
    // are out of it.
    std::vector<size_t> const hit = InBlast({0.5f, 0.f, 0.5f}, 2.f, bodies);
    CHECK(hit.size() == 2 && hit[0] == 0 && hit[1] == 1);
    CHECK(InBlast({0.f, 0.f, 0.f}, 0.f, bodies).size() == 1);        // touching only
    CHECK(InBlast({50.f, 0.f, 0.f}, 8.f, bodies).empty());
}

TEST(aimed_releases_and_bolt_events_round_trip_the_wire)
{
    HandAim a;
    a.cast.spell = 133;
    a.cast.hand = kHandRightButton;
    a.cast.flags = CAST_POINT;
    a.cast.target = 0;
    a.cast.point = {10.f, 20.f, 30.f};
    a.origin = {1.f, 2.f, 3.5f};
    a.dir = {0.f, 0.6f, 0.8f};
    std::vector<uint8_t> const b = EncodeHandAim(a);
    CHECK(b.size() == kHandAimBytes && b[0] == kOpHandAim);
    HandAim d;
    CHECK(DecodeHandAim(b, d));
    CHECK(d.cast.spell == 133 && d.cast.hand == kHandRightButton && d.cast.flags == CAST_POINT);
    CHECK(d.cast.point.z == 30.f && d.origin.z == 3.5f && d.dir.y == 0.6f && d.dir.z == 0.8f);
    // An op 12 body is no aimed release, nor is a short one, nor one aimed at no number.
    HandCast plain;
    CHECK(!DecodeHandAim(EncodeHandCast(a.cast), d));
    CHECK(!DecodeHandAim(std::vector<uint8_t>(b.begin(), b.end() - 1), d));
    a.dir.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!DecodeHandAim(EncodeHandAim(a), d));
    // And op 12 still reads op 12 only.
    CHECK(!DecodeHandCast(b, plain));

    BoltEvent launch;
    launch.kind = BoltEventKind::Launch;
    launch.id = 77;
    launch.spell = 25306;
    launch.caster = 0x42ull;
    launch.from = {1.f, 2.f, 3.f};
    launch.to = {31.f, 2.f, 3.f};
    launch.speed = 24.f;
    launch.flags = BOLT_VOLLEY;
    std::vector<uint8_t> const lb = EncodeBoltEvent(launch);
    CHECK(lb.size() == kBoltLaunchBytes && lb[0] == kOpBolt && lb[1] == 1);
    BoltEvent e;
    CHECK(DecodeBoltEvent(lb, e));
    CHECK(e.kind == BoltEventKind::Launch && e.id == 77 && e.spell == 25306 && e.caster == 0x42);
    CHECK(e.from.y == 2.f && e.to.x == 31.f && e.speed == 24.f && e.flags == BOLT_VOLLEY);

    BoltEvent impact;
    impact.kind = BoltEventKind::Impact;
    impact.id = 77;
    impact.spell = 25306;
    impact.caster = 0x42;
    impact.to = {20.f, 2.f, 3.f};
    impact.unit = 0xF130000000001234ull;
    impact.flags = BOLT_HIT_PIECE;
    std::vector<uint8_t> const ib = EncodeBoltEvent(impact);
    CHECK(ib.size() == kBoltImpactBytes && ib[1] == 2);
    CHECK(DecodeBoltEvent(ib, e));
    CHECK(e.kind == BoltEventKind::Impact && e.unit == impact.unit && e.to.x == 20.f && e.flags == BOLT_HIT_PIECE);
    // A kind that is neither, a short body: refused.
    std::vector<uint8_t> bad = ib;
    bad[1] = 3;
    CHECK(!DecodeBoltEvent(bad, e));
    CHECK(!DecodeBoltEvent(std::vector<uint8_t>(ib.begin(), ib.end() - 1), e));
}
