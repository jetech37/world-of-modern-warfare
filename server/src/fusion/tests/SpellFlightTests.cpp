/*
 * World of Modern Warfare: hand spells in flight and on impact (fusion/Spellcasting.h): no range
 * but a safety cap or a fizzle distance, splash with the direct hit doing the most, sight for the
 * splash, hard control only on a direct hit, homing at a limited turn rate, and the held sprays.
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Spellcasting.h"

#include <limits>

using namespace Fusion;
using namespace Fusion::Spellcasting;

namespace
{
    Body Upright(uint64_t guid, Vec3 feet)
    {
        Body b;
        b.guid = guid;
        b.pose.feet = feet;
        return b;
    }

    SpellFacts Spell(uint32_t first, int32_t castMs, float range)
    {
        SpellFacts f;
        f.firstRank = first;
        f.castMs = castMs;
        f.rangeYards = range;
        return f;
    }

    BoltFacts Flying(uint32_t first, float speed, float range, bool damages = true)
    {
        BoltFacts f;
        f.firstRank = first;
        f.speed = speed;
        f.rangeYards = range;
        f.damages = damages;
        return f;
    }

    // Flies a bolt as the server does, step by step (`dtMs` apart), against bodies standing
    // still: the index struck, or -1 when it flew its reach out. `homeOn` steers it at its turn
    // rate (-1: straight).
    int FlyBolt(Vec3 from, Vec3 dir, float speed, BoltTraits const& t, std::vector<Body> const& bodies,
        int homeOn, uint32_t dtMs = 50)
    {
        float left = BoltReach(t);
        Vec3 at = from;
        for (int guard = 0; guard < 100000 && left > 0.01f; ++guard)
        {
            if (homeOn >= 0 && t.homing.On())
                dir = SteerToward(dir, BodyCentre(bodies[size_t(homeOn)]) - at,
                    t.homing.turnDegPerSec * 3.14159265f / 180.f * float(dtMs) / 1000.f);
            float const step = StepYards(speed, dtMs, left);
            BodyStrike const s = FirstBody(at, dir, step, kBoltRadius, bodies);
            if (s.index >= 0)
                return s.index;
            at = at + dir * step;
            left -= step;
        }
        return -1;
    }

    bool Clear(Vec3, Vec3) { return true; }
}

TEST(a_bolt_has_no_range_only_a_safety_cap_or_its_fizzle_distance)
{
    // Fireball (35 yards in WoW, 24 yards a second): no range now; it flies to the safety cap.
    BoltTraits const fireball = TraitsOf(Flying(133, 24.f, 35.f));
    CHECK(fireball.fizzleYards == 0.f);
    CHECK_NEAR(BoltReach(fireball), kBoltSafetyYards, 1e-5);
    CHECK(!fireball.homing.On());
    // A target 120 yards off, far past the old 37-yard reach, is struck.
    std::vector<Body> const far = {Upright(9, {120.f, 0.f, 0.f})};
    CHECK(FlyBolt({0.f, 0.f, 1.2f}, {1.f, 0.f, 0.f}, 24.f, fireball, far, -1, 100) == 0);
    // Nothing in the way: it ends at the cap, inside the flight-time cap.
    CHECK(FlyBolt({0.f, 0.f, 1.2f}, {1.f, 0.f, 0.f}, 24.f, fireball, {}, -1, 100) < 0);
    CHECK(FlightMs(kBoltSafetyYards, 20.f) == 10000 && FlightMs(kBoltSafetyYards, 20.f) <= kMaxFlightMs);
    // An instant ray (Mind Blast, 30 yards): a hit-scan fizzles at twice its spell's reach.
    BoltTraits const blast = TraitsOf(Flying(8092, 0.f, 30.f));
    CHECK_NEAR(blast.fizzleYards, 60.f, 1e-5);
    CHECK_NEAR(BoltReach(blast), 60.f, 1e-5);
    std::vector<Body> const past = {Upright(9, {70.f, 0.f, 0.f})};
    CHECK(FlyBolt({0.f, 0.f, 1.2f}, {1.f, 0.f, 0.f}, 0.f, blast, past, -1) < 0);     // fizzled first
    std::vector<Body> const within = {Upright(9, {55.f, 0.f, 0.f})};
    CHECK(FlyBolt({0.f, 0.f, 1.2f}, {1.f, 0.f, 0.f}, 0.f, blast, within, -1) == 0);
    // Rows: Arcane Missiles fade at 45 yards, the shocks at 30 (under the instant default of 40).
    CHECK_NEAR(TraitsOf(Flying(5143, 36.f, 30.f)).fizzleYards, 45.f, 1e-5);
    CHECK_NEAR(TraitsOf(Flying(8056, 0.f, 20.f)).fizzleYards, 30.f, 1e-5);
    CHECK_NEAR(TraitsOf(Flying(8042, 0.f, 20.f)).fizzleYards, 30.f, 1e-5);
    // Never past the safety cap.
    CHECK_NEAR(TraitsOf(Flying(1, 0.f, 500.f)).fizzleYards, kBoltSafetyYards, 1e-5);
}

TEST(a_damaging_bolt_splashes_and_one_without_harm_strikes_only_what_it_hits)
{
    CHECK_NEAR(TraitsOf(Flying(133, 24.f, 35.f)).splashYards, kDefaultSplashYards, 1e-5);
    // Polymorph does no harm: no splash, only what it hits is a sheep.
    CHECK(TraitsOf(Flying(118, 20.f, 30.f, false)).splashYards == 0.f);
    // Curse of Doom harms, but it is one victim's doom: no splash, by its row.
    CHECK(TraitsOf(Flying(603, 0.f, 30.f)).splashYards == 0.f);
    // Arcane Missiles: a small splash each.
    CHECK_NEAR(TraitsOf(Flying(5143, 36.f, 30.f)).splashYards, 1.5f, 1e-5);
    // A spell with its own area about the destination keeps it, whole, as its splash.
    BoltFacts own = Flying(1, 20.f, 30.f);
    own.ownArea = 5.f;
    BoltTraits const t = TraitsOf(own);
    CHECK(t.ownArea && t.splashYards == 5.f);
}

TEST(splash_hurts_without_a_direct_hit_and_the_direct_hit_hurts_most)
{
    // The share: whole for the one hit; 75% at the blast's centre down to 50% at its edge.
    CHECK(SplashScale(0.f, 3.f, true) == 1.f);
    CHECK_NEAR(SplashScale(0.f, 3.f, false), kSplashCentre, 1e-5);
    CHECK_NEAR(SplashScale(1.5f, 3.f, false), 0.625f, 1e-5);
    CHECK_NEAR(SplashScale(3.f, 3.f, false), kSplashEdge, 1e-5);
    CHECK(SplashScale(3.1f, 3.f, false) == 0.f);
    CHECK(SplashScale(0.f, 0.f, false) == 0.f);
    for (float d = 0.f; d < 2.9f; d += 0.1f)
        CHECK(SplashScale(d, 3.f, false) >= SplashScale(d + 0.1f, 3.f, false));
    CHECK(SplashScale(0.f, 3.f, false) < SplashScale(0.f, 3.f, true));

    // A bolt into the wall beside three enemies: the two near take a share, the far one nothing;
    // with the first hit directly, it is not splashed again.
    std::vector<Body> const bodies = {Upright(1, {0.f, 0.f, 0.f}), Upright(2, {1.5f, 0.f, 0.f}),
        Upright(3, {6.f, 0.f, 0.f})};
    std::vector<SplashHit> hits = Splash({0.f, -0.6f, 1.f}, 2.5f, bodies, -1, Clear);
    CHECK(hits.size() == 2 && hits[0].body == 0 && hits[1].body == 1);
    CHECK(hits.size() == 2 && hits[0].scale > hits[1].scale);         // nearer, more
    CHECK(hits.size() == 2 && hits[0].scale <= kSplashCentre && hits[1].scale >= kSplashEdge);
    hits = Splash({0.f, -0.6f, 1.f}, 2.5f, bodies, 0, Clear);
    CHECK(hits.size() == 1 && hits[0].body == 1);
    // No splash radius: nothing.
    CHECK(Splash({0.f, 0.f, 1.f}, 0.f, bodies, -1, Clear).empty());
}

TEST(splash_needs_sight_from_the_impact)
{
    // Two enemies at the blast, one behind a wall (the sight test says so): only the other.
    std::vector<Body> const bodies = {Upright(1, {1.f, 0.f, 0.f}), Upright(2, {-1.f, 0.f, 0.f})};
    auto wallAtNegativeX = [](Vec3 from, Vec3 to) { return !(from.x > -0.5f && to.x < -0.5f); };
    std::vector<SplashHit> const hits = Splash({0.f, 0.f, 1.f}, 3.f, bodies, -1, wallAtNegativeX);
    CHECK(hits.size() == 1 && hits[0].body == 0);
    // The sight is drawn to the chest.
    Vec3 const chest = BodyCentre(bodies[0]);
    CHECK(chest.z > 0.5f && chest.z < 1.6f && chest.x == 1.f);
}

TEST(a_splash_drops_hard_control_and_shortens_slows)
{
    // Polymorph's confuse and transform, a stun, a fear, a root, a silence, a charm: only a direct
    // hit.
    for (uint32_t a : {5u, 56u, 12u, 7u, 26u, 27u, 6u, 60u})
        CHECK(KindOfAura(a) == AuraKind::HardControl);
    CHECK(KindOfAura(33) == AuraKind::Slow);                // Frostbolt's chill
    CHECK(KindOfAura(3) == AuraKind::DamageOverTime);       // Corruption, a burn
    CHECK(KindOfAura(53) == AuraKind::DamageOverTime);
    CHECK(KindOfAura(22) == AuraKind::Other);               // a resistance debuff
    // An interrupt (Earth Shock) and a knock-back are control too.
    CHECK(ControlEffect(68) && ControlEffect(98));
    CHECK(!ControlEffect(2) && !ControlEffect(6));
    // A splashed slow lasts its share, never under half a second nor over the whole.
    CHECK(SplashAuraMs(9000, 0.5f) == 4500);
    CHECK(SplashAuraMs(9000, 1.5f) == 9000);
    CHECK(SplashAuraMs(600, 0.5f) == kSplashMinAuraMs);
}

TEST(a_homing_bolt_picks_the_selected_target_or_the_one_nearest_the_crosshair)
{
    Vec3 const from{0.f, 0.f, 1.6f};
    Vec3 const east{1.f, 0.f, 0.f};
    // One a little off the crosshair, one well off it, one behind.
    std::vector<Body> const bodies = {Upright(1, {20.f, 2.f, 0.f}), Upright(2, {20.f, 12.f, 0.f}),
        Upright(3, {-20.f, 0.f, 0.f})};
    CHECK(HomingTarget(from, east, 20.f, bodies, 0) == 0);
    // Tab on the one well off it (31 degrees): within the selection's 45, it is the bolt's.
    CHECK(HomingTarget(from, east, 20.f, bodies, 2) == 1);
    // Tab on the one behind: not ahead, so the crosshair's pick.
    CHECK(HomingTarget(from, east, 20.f, bodies, 3) == 0);
    // Nothing in the cone: straight.
    CHECK(HomingTarget(from, {0.f, -1.f, 0.f}, 20.f, bodies, 0) < 0);
    // Out of acquiring reach.
    std::vector<Body> const far = {Upright(1, {kHomingAcquireYards + 5.f, 0.f, 0.f})};
    CHECK(HomingTarget(from, east, 20.f, far, 0) < 0);
}

TEST(a_homing_bolt_turns_at_a_limited_rate_and_can_still_miss)
{
    Vec3 const east{1.f, 0.f, 0.f};
    // One step's turn is clamped; a small one is exact.
    Vec3 d = SteerToward(east, {0.f, 1.f, 0.f}, 0.1f);
    CHECK_NEAR(std::acos(d.x), 0.1f, 1e-3);
    CHECK_NEAR(d.Length(), 1.f, 1e-5);
    d = SteerToward(east, {1.f, 0.05f, 0.f}, 0.5f);
    CHECK_NEAR(d.y, 0.05f / std::sqrt(1.0025f), 1e-4);
    // Straight behind: it turns, by the limit.
    d = SteerToward(east, {-1.f, 0.f, 0.f}, 0.2f);
    CHECK_NEAR(std::acos(Clamp(d.x, -1.f, 1.f)), 0.2f, 1e-3);
    // No turn rate: no turn.
    d = SteerToward(east, {0.f, 1.f, 0.f}, 0.f);
    CHECK(d.x == 1.f);

    // Arcane Missiles (36 yards a second, 270 degrees a second: a 7.6-yard turning circle).
    BoltTraits const missiles = TraitsOf(Flying(5143, 36.f, 30.f));
    CHECK(missiles.homing.On());
    Vec3 const from{0.f, 0.f, 1.2f};
    // 20 yards off and 3 aside: straight it misses, homing it strikes.
    std::vector<Body> const aside = {Upright(1, {20.f, 3.f, 0.f})};
    BoltTraits straight = missiles;
    straight.homing = {};
    CHECK(FlyBolt(from, east, 36.f, straight, aside, -1) < 0);
    CHECK(FlyBolt(from, east, 36.f, missiles, aside, 0) == 0);
    // 3 yards off and 3 aside: inside its turning circle, it cannot come round in time.
    std::vector<Body> const close = {Upright(1, {3.f, 3.f, 0.f})};
    CHECK(FlyBolt(from, east, 36.f, missiles, close, 0) < 0);
    // A grant (a talent, a perk) gives a straight bolt a modest turn; a row's stronger one stays.
    BoltTraits const granted = WithHoming(TraitsOf(Flying(133, 24.f, 35.f)), kGrantedHoming);
    CHECK(granted.homing.On() && granted.homing.turnDegPerSec == kGrantedHoming.turnDegPerSec);
    CHECK(WithHoming(missiles, kGrantedHoming).homing.turnDegPerSec == 270.f);
    std::vector<Body> const fireballAside = {Upright(1, {25.f, 2.f, 0.f})};
    CHECK(FlyBolt(from, east, 24.f, granted, fireballAside, 0) == 0);
    CHECK(FlyBolt(from, east, 24.f, TraitsOf(Flying(133, 24.f, 35.f)), fireballAside, 0) < 0);
}

TEST(a_spray_pours_a_cone_that_ticks_damage_and_mana)
{
    // Scorch and Cone of Cold are sprays: held in a hand (Cone of Cold was cast on oneself), no
    // charge.
    SpellFacts const scorch = Spell(2948, 1500, 30.f);
    CHECK(InHand(scorch) && ShapeOf(scorch) == Shape::Spray && ChargeMs(scorch) == 0);
    SpellFacts cone = Spell(120, 0, 0.f);
    cone.selfRange = true;
    CHECK(InHand(cone) && ShapeOf(cone) == Shape::Spray);
    SprayRow const* fire = SprayOf(2948);
    SprayRow const* frost = SprayOf(120);
    CHECK(fire && frost && !SprayOf(133));
    if (!fire || !frost)
        return;
    CHECK(frost->chillMs > 0 && fire->chillMs == 0);
    CHECK(frost->halfAngleDeg > fire->halfAngleDeg && frost->rangeYards < fire->rangeYards);

    Vec3 const eye{0.f, 0.f, 1.6f};
    Vec3 const east{1.f, 0.f, 0.f};
    // In front within reach: caught; past the reach, well aside or behind: not.
    CHECK(InSpray(eye, east, *fire, Upright(1, {5.f, 0.f, 0.f})));
    CHECK(InSpray(eye, east, *fire, Upright(1, {9.f, 1.5f, 0.f})));
    CHECK(!InSpray(eye, east, *fire, Upright(1, {11.f, 0.f, 0.f})));
    CHECK(!InSpray(eye, east, *fire, Upright(1, {4.f, 4.f, 0.f})));     // 45 degrees off
    CHECK(!InSpray(eye, east, *fire, Upright(1, {-5.f, 0.f, 0.f})));
    // The frost spray is wider: 30 degrees off is in it, not in the fire's.
    Body const wide = Upright(1, {5.f, 2.9f, 0.f});
    CHECK(InSpray(eye, east, *frost, wide) && !InSpray(eye, east, *fire, wide));
    // One standing on the caster is in it whatever the aim.
    CHECK(InSpray(eye, {0.f, 1.f, 0.f}, *fire, Upright(1, {0.2f, 0.f, 0.f})));
    // A tick catches everyone in it that it can see.
    std::vector<Body> const bodies = {Upright(1, {4.f, 0.f, 0.f}), Upright(2, {7.f, 1.f, 0.f}),
        Upright(3, {7.f, -8.f, 0.f})};
    std::vector<size_t> caught = SprayCatches(eye, east, *fire, bodies, Clear);
    CHECK(caught.size() == 2 && caught[0] == 0 && caught[1] == 1);
    auto behindWall = [](Vec3, Vec3 to) { return to.x < 5.f; };
    caught = SprayCatches(eye, east, *fire, bodies, behindWall);
    CHECK(caught.size() == 1 && caught[0] == 0);
    // Mana a tick: its share of the pool, at least 1; about sixteen seconds from full.
    CHECK(SprayTickMana(4000, *fire) == 60);
    CHECK(SprayTickMana(10, *fire) == 1);
    float const secondsFromFull = 100.f / fire->manaPctPerTick * float(fire->tickMs) / 1000.f;
    CHECK(secondsFromFull > 12.f && secondsFromFull < 20.f);
}

TEST(a_held_spray_trades_with_casting_its_bolt_over_the_same_time)
{
    SprayRow const* fire = SprayOf(2948);
    SprayRow const* frost = SprayOf(120);
    CHECK(fire && frost);
    if (!fire || !frost)
        return;
    // Each is measured against its school's bolt: Scorch against Fireball, Cone of Cold against
    // Frostbolt, at about the same damage a second for the fire stream and less for the chill.
    CHECK(fire->boltChain == 133 && frost->boltChain == 116);
    CHECK(fire->boltShare >= 0.9f && fire->boltShare <= 1.1f);
    CHECK(frost->boltShare >= 0.6f && frost->boltShare <= 0.8f);
    // A level 60 Fireball (rank 12, 596-760: 678 on average) charges in 0.8 s: 847.5 a second,
    // 211.9 a quarter-second tick. Held for a second (four ticks) the stream deals its share of it.
    float const fireball = 678.f;
    float const tick = SprayTickDamage(*fire, fireball, kChargeHeavyMs);
    CHECK_NEAR(tick, fire->boltShare * 678.f * 250.f / 800.f, 1e-3);
    float const boltPerSecond = fireball * 1000.f / float(kChargeHeavyMs);
    float const sprayPerSecond = tick * 1000.f / float(fire->tickMs);
    CHECK(sprayPerSecond >= 0.9f * boltPerSecond && sprayPerSecond <= 1.1f * boltPerSecond);
    // Frostbolt rank 11 (535 on average), the same charge: Cone of Cold's 60-80% of it.
    float const frostbolt = 535.f;
    float const chill = SprayTickDamage(*frost, frostbolt, kChargeHeavyMs) * 1000.f / float(frost->tickMs);
    float const frostPerSecond = frostbolt * 1000.f / float(kChargeHeavyMs);
    CHECK(chill >= 0.6f * frostPerSecond && chill <= 0.8f * frostPerSecond);
    // Spell power and talents ride the bolt's hit: 300 more on the Fireball (its 1.0 coefficient)
    // is 300/678 more a tick, linearly; a quicker low rank (0.45 s charge) is a faster stream.
    CHECK_NEAR(SprayTickDamage(*fire, fireball + 300.f, kChargeHeavyMs) / tick, (678.f + 300.f) / 678.f, 1e-4);
    CHECK_NEAR(SprayTickDamage(*fire, 18.f, kChargeQuickMs), fire->boltShare * 18.f * 250.f / 450.f, 1e-4);
    // Nothing to measure against: nothing.
    CHECK(SprayTickDamage(*fire, 0.f, kChargeHeavyMs) == 0.f && SprayTickDamage(*fire, fireball, 0) == 0.f);
    // A rank known below its level hits as the level allows, never below a quarter.
    CHECK(BelowRankScale(60, 60) == 1.f && BelowRankScale(70, 60) == 1.f);
    CHECK_NEAR(BelowRankScale(2, 4), 0.5f, 1e-6);
    CHECK(BelowRankScale(1, 40) == 0.25f && BelowRankScale(5, 0) == 1.f);
}

TEST(sprays_and_the_new_bolt_flags_round_trip_the_wire)
{
    SprayAim a;
    a.spell = 10161;
    a.hand = kHandRightButton;
    a.on = true;
    a.origin = {1.f, 2.f, 3.f};
    a.dir = {0.f, 1.f, 0.f};
    std::vector<uint8_t> const b = EncodeSprayAim(a);
    CHECK(b.size() == kSprayAimBytes && b[0] == kOpSprayAim);
    SprayAim d;
    CHECK(DecodeSprayAim(b, d));
    CHECK(d.spell == 10161 && d.hand == kHandRightButton && d.on && d.origin.z == 3.f && d.dir.y == 1.f);
    a.on = false;
    CHECK(DecodeSprayAim(EncodeSprayAim(a), d) && !d.on);
    // Both hands at once is no spray's; a short body or no number is refused.
    a.hand = kHandBoth;
    CHECK(!DecodeSprayAim(EncodeSprayAim(a), d));
    CHECK(!DecodeSprayAim(std::vector<uint8_t>(b.begin(), b.end() - 1), d));
    a.hand = 0;
    a.dir.x = std::numeric_limits<float>::infinity();
    CHECK(!DecodeSprayAim(EncodeSprayAim(a), d));

    SprayEvent e;
    e.caster = 0x77ull;
    e.spell = 2948;
    e.hand = 1;
    e.on = true;
    e.from = {5.f, 6.f, 7.f};
    e.dir = {1.f, 0.f, 0.f};
    e.range = 10.f;
    e.halfAngleDeg = 20.f;
    std::vector<uint8_t> const eb = EncodeSprayEvent(e);
    CHECK(eb.size() == kSprayEventBytes && eb[0] == kOpSpray);
    SprayEvent f;
    CHECK(DecodeSprayEvent(eb, f));
    CHECK(f.caster == 0x77 && f.spell == 2948 && f.hand == 1 && f.on && f.from.y == 6.f && f.range == 10.f
        && f.halfAngleDeg == 20.f);
    CHECK(!DecodeSprayEvent(b, f));
    // Free ops on channel 7: 1-13 are the class page and the hands, 41-42 the bolts.
    CHECK(kOpSprayAim == 43 && kOpSpray == 44);

    // The fizzle and the homing course ride op 42's flags.
    BoltEvent fizzle;
    fizzle.kind = BoltEventKind::Impact;
    fizzle.id = 5;
    fizzle.flags = BOLT_SPENT | BOLT_FIZZLED;
    BoltEvent g;
    CHECK(DecodeBoltEvent(EncodeBoltEvent(fizzle), g) && g.flags == (BOLT_SPENT | BOLT_FIZZLED));
    BoltEvent course;
    course.kind = BoltEventKind::Launch;
    course.speed = 36.f;
    course.flags = BOLT_HOMING | BOLT_VOLLEY;
    CHECK(DecodeBoltEvent(EncodeBoltEvent(course), g) && g.flags == (BOLT_HOMING | BOLT_VOLLEY));
}
