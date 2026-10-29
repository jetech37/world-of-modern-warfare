/*
 * Azeroth Warfare: tests for NPC gunners' footwork and wire (NpcGuns.h). GPL-2.0-or-later, see
 * Common.h.
 */

#include "Test.h"
#include "fusion/Gunfights.h"
#include "fusion/NpcGuns.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Fusion;

TEST(a_gunner_holds_at_its_guns_range)
{
    // Close-in guns about 10 yards, rifles and LMGs about 20, snipers about 35.
    for (WeaponClass c : {WeaponClass::Shotgun, WeaponClass::Smg, WeaponClass::MachinePistol, WeaponClass::Pistol})
        CHECK_NEAR(GunnerHoldRange(c), 10.f, 1e-6);
    for (WeaponClass c : {WeaponClass::AssaultRifle, WeaponClass::Lmg})
        CHECK_NEAR(GunnerHoldRange(c), 20.f, 1e-6);
    CHECK_NEAR(GunnerHoldRange(WeaponClass::Sniper), 35.f, 1e-6);
    // Every range is past the knife and inside the gunner's reach.
    for (WeaponClass c : {WeaponClass::Launcher, WeaponClass::Melee, WeaponClass::Sniper})
        CHECK(GunnerHoldRange(c) > kGunnerKnifeRange && GunnerHoldRange(c) <= kGunnerReach);
}

TEST(a_gunner_advances_only_when_it_must)
{
    float const hold = 20.f;
    CHECK(!GunnerShouldAdvance(15.f, true, hold));      // in sight and range: stand and fire
    CHECK(!GunnerShouldAdvance(20.f, true, hold));
    CHECK(GunnerShouldAdvance(15.f, false, hold));      // lost sight: close in
    CHECK(GunnerShouldAdvance(25.f, true, hold));       // too far
    CHECK(GunnerShouldAdvance(2.f, true, hold));        // at arm's length: the knife
    CHECK(!GunnerShouldAdvance(4.f, true, hold));
}

namespace
{
    float FloatAt(std::vector<uint8_t> const& b, size_t at)
    {
        float f;
        std::memcpy(&f, &b[at], 4);
        return f;
    }
}

TEST(the_npc_fire_message_is_twenty_four_bytes)
{
    std::vector<uint8_t> const b = EncodeNpcFire(0x1122334455667788ull, 0x0203, true, {4.f, -5.f, 6.5f});
    CHECK(b.size() == 24);
    std::vector<uint8_t> const head = {32, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x03, 0x02, 1};
    CHECK(std::vector<uint8_t>(b.begin(), b.begin() + 12) == head);
    CHECK(FloatAt(b, 12) == 4.f && FloatAt(b, 16) == -5.f && FloatAt(b, 20) == 6.5f);
    // 4.0 as a little-endian float.
    CHECK(b[12] == 0x00 && b[13] == 0x00 && b[14] == 0x80 && b[15] == 0x40);
    CHECK(EncodeNpcFire(1, 2, false, {})[11] == 0);
}

TEST(the_npc_guns_message_lists_at_most_two_hundred)
{
    std::vector<uint8_t> const b = EncodeNpcGuns({{0x1122334455667788ull, 7}, {0x99ull, 0x1234}});
    std::vector<uint8_t> const want = {33, 2,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 7, 0,
        0x99, 0, 0, 0, 0, 0, 0, 0, 0x34, 0x12};
    CHECK(b == want);
    CHECK(EncodeNpcGuns({}) == std::vector<uint8_t>({33, 0}));
    std::vector<NpcGunEntry> many(250, NpcGunEntry{1, 2});
    std::vector<uint8_t> const capped = EncodeNpcGuns(many);
    CHECK(capped[1] == kMaxNpcGunsListed && capped.size() == 2 + 10 * size_t(kMaxNpcGunsListed));
}

TEST(a_players_round_is_thirty_six_bytes)
{
    std::vector<uint8_t> const b = EncodePlayerRound(0x1122334455667788ull, 0x0203, true, {1.f, 2.f, 3.f}, {4.f, -5.f, 6.5f});
    CHECK(b.size() == 36);
    std::vector<uint8_t> const head = {34, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x03, 0x02, 1};
    CHECK(std::vector<uint8_t>(b.begin(), b.begin() + 12) == head);
    CHECK(FloatAt(b, 12) == 1.f && FloatAt(b, 16) == 2.f && FloatAt(b, 20) == 3.f);
    CHECK(FloatAt(b, 24) == 4.f && FloatAt(b, 28) == -5.f && FloatAt(b, 32) == 6.5f);
    CHECK(EncodePlayerRound(1, 2, false, {}, {})[11] == 0);
    // Its op is free of the NPC ops beside it.
    CHECK(kOpPlayerRound != kOpNpcFire && kOpPlayerRound != kOpNpcGuns);
}

TEST(a_missed_round_lands_a_yard_or_two_beside_the_target)
{
    Vec3 const muzzle{0.f, 0.f, 1.4f}, chest{20.f, 0.f, 1.2f};
    for (float a : {0.f, 0.3f, 0.99f})
        for (float r : {0.f, 0.5f, 1.f})
        {
            Vec3 const miss = NpcMissPoint(muzzle, chest, a, r);
            float const off = (miss - chest).Length();
            CHECK(off >= 0.99f && off <= 2.01f);
            // Beside the line of fire, not along it.
            CHECK(std::fabs((miss - chest).Dot((chest - muzzle).Normalized())) < 0.01f);
        }
}

namespace
{
    GunnerSense Sense(float distance, bool sight, bool hurt = false, bool shotAt = false)
    {
        GunnerSense s;
        s.distance = distance;
        s.sight = sight;
        s.holdRange = 20.f;
        s.hurt = hurt;
        s.shotAt = shotAt;
        return s;
    }

    GunnerSense SenseAt(float distance, bool sight, float holdRange)
    {
        GunnerSense s = Sense(distance, sight);
        s.holdRange = holdRange;
        return s;
    }
}

TEST(a_gunner_in_range_keeps_moving_from_spot_to_spot)
{
    GunnerMind m;
    // In sight and range: straight to a firing spot, not standing still.
    CHECK(GunnerThink(m, Sense(15.f, true), 1000) == GunnerAct::Strafe);
    GunnerMoved(m, GunnerAct::Strafe, true, {1.f, 2.f, 0.f}, 4.f, 1000, 0);
    CHECK(m.mode == GunnerMode::Fight);
    CHECK(m.untilMs >= 1000 + kStrafeMinMs && m.untilMs <= 1000 + kStrafeMaxMs + 1000);
    // Once a think at most, and not before its spot's time is up.
    CHECK(GunnerThink(m, Sense(15.f, true), 1500) == GunnerAct::None);
    CHECK(GunnerThink(m, Sense(15.f, true), 2100) == GunnerAct::None);
    CHECK(GunnerThink(m, Sense(15.f, true), m.untilMs + 1) == GunnerAct::Strafe);
    // The hold's length varies with the roll, within 2 to 4 s.
    GunnerMind a, b;
    GunnerMoved(a, GunnerAct::Strafe, true, {}, 0.f, 0, 0);
    GunnerMoved(b, GunnerAct::Strafe, true, {}, 0.f, 0, 1999);
    CHECK(a.untilMs != b.untilMs);
    CHECK(a.untilMs >= kStrafeMinMs && b.untilMs <= kStrafeMaxMs);
}

TEST(a_gunner_out_of_sight_or_range_closes_in)
{
    GunnerMind m;
    m.mode = GunnerMode::Fight;
    CHECK(GunnerThink(m, Sense(15.f, false), 1000) == GunnerAct::Chase);
    GunnerMoved(m, GunnerAct::Chase, true, {}, 0.f, 1000, 0);
    CHECK(m.mode == GunnerMode::Chase);
    // Already chasing: nothing new.
    CHECK(GunnerThink(m, Sense(30.f, true), 2000) == GunnerAct::None);
    // At arm's length: the knife (its chase).
    GunnerMind k;
    k.mode = GunnerMode::Fight;
    CHECK(GunnerThink(k, Sense(2.f, true), 1000) == GunnerAct::Chase);
}

TEST(a_charging_gunner_stands_off_at_its_hold_range_between_thinks)
{
    // Live test: two rifle gunners aggroed 25 yards off ran into the player's face. A think a
    // second let a x1.6 charge carry them past the hold range. Now, chasing at the chase speed
    // in 100 ms ticks, a gunner stands off the tick it is within its hold range in sight.
    GunnerMind m;
    float const hold = GunnerHoldRange(WeaponClass::AssaultRifle);
    float distance = 25.f;
    float speed = 7.f;
    uint32_t now = 0;
    // Its first think in range of nothing: it closes in (its chase).
    CHECK(GunnerThink(m, SenseAt(distance, true, hold), now) == GunnerAct::None);
    GunnerAct act = GunnerAct::None;
    for (int tick = 0; tick < 50 && act == GunnerAct::None; ++tick)
    {
        now += 100;
        bool const fast = speed > 7.5f;
        speed = 7.f * ChaseSpeed(distance, hold, fast);
        distance -= speed * 0.1f;
        act = GunnerThink(m, SenseAt(distance, true, hold), now);
    }
    CHECK(act == GunnerAct::Strafe);
    CHECK(distance > hold - 1.2f && distance <= hold);
    // It eased off the rush before the hold range, too.
    CHECK_NEAR(ChaseSpeed(hold + 1.f, hold, true), 1.f, 1e-6);
    // Out of sight it keeps closing in: no stand-off on a target it cannot see.
    GunnerMind blind;
    CHECK(GunnerThink(blind, SenseAt(25.f, true, hold), 0) == GunnerAct::None);
    CHECK(GunnerThink(blind, SenseAt(15.f, false, hold), 100) == GunnerAct::None);
    // At arm's length it keeps its knife (the chase) until the target backs off.
    GunnerMind knife;
    CHECK(GunnerThink(knife, SenseAt(2.f, true, hold), 0) == GunnerAct::None);
    CHECK(GunnerThink(knife, SenseAt(2.5f, true, hold), 100) == GunnerAct::None);
    CHECK(GunnerThink(knife, SenseAt(8.f, true, hold), 200) == GunnerAct::Strafe);
}

TEST(a_hurt_or_shot_at_gunner_takes_cover_then_peeks)
{
    GunnerMind m;
    m.mode = GunnerMode::Fight;
    CHECK(GunnerThink(m, Sense(15.f, true, true), 1000) == GunnerAct::TakeCover);
    GunnerMoved(m, GunnerAct::TakeCover, true, {5.f, 0.f, 0.f}, 7.f, 1000, 0);
    CHECK(m.mode == GunnerMode::Cover && m.hasCover);
    // Hidden it sees nothing, and that is no reason to leave.
    CHECK(GunnerThink(m, Sense(15.f, false, true), 2100) == GunnerAct::None);
    // Its time in cover up (2 to 4 s after it gets there): a peek.
    CHECK(GunnerThink(m, Sense(15.f, false, true), m.untilMs + 1) == GunnerAct::Peek);
    uint32_t const t = m.untilMs + 1;
    GunnerMoved(m, GunnerAct::Peek, true, {6.f, 2.f, 0.f}, 2.f, t, 0);
    CHECK(m.mode == GunnerMode::Peek);
    CHECK(m.untilMs >= t + kPeekMs);
    // Still under fire after the burst: back to cover.
    CHECK(GunnerThink(m, Sense(15.f, true, false, true), m.untilMs + 1) == GunnerAct::ReturnToCover);
    GunnerMoved(m, GunnerAct::ReturnToCover, true, m.cover, 2.f, m.untilMs + 1, 0);
    CHECK(m.mode == GunnerMode::Cover);
    // No longer pressed after the next peek: out to fight in the open, and not straight back.
    uint32_t now = m.untilMs + 1;
    CHECK(GunnerThink(m, Sense(15.f, false), now) == GunnerAct::Peek);
    GunnerMoved(m, GunnerAct::Peek, true, {6.f, 2.f, 0.f}, 2.f, now, 0);
    now = m.untilMs + 1;
    CHECK(GunnerThink(m, Sense(15.f, true), now) == GunnerAct::Strafe);
    GunnerMoved(m, GunnerAct::Strafe, true, {}, 3.f, now, 0);
    CHECK(m.mode == GunnerMode::Fight);
    CHECK(GunnerThink(m, Sense(15.f, true, true), now + kGunnerThinkMs) != GunnerAct::TakeCover);
    CHECK(GunnerThink(m, Sense(15.f, true, true), now + kCoverCooldownMs + 1) == GunnerAct::TakeCover);
}

TEST(a_gunner_peeks_only_so_often)
{
    GunnerMind m;
    m.mode = GunnerMode::Fight;
    uint32_t now = 1000;
    CHECK(GunnerThink(m, Sense(15.f, true, true), now) == GunnerAct::TakeCover);
    GunnerMoved(m, GunnerAct::TakeCover, true, {5.f, 0.f, 0.f}, 3.f, now, 0);
    int peeks = 0;
    for (int i = 0; i < 20 && m.mode != GunnerMode::Fight; ++i)
    {
        now = std::max(now + kGunnerThinkMs, m.untilMs + 1);
        GunnerAct const a = GunnerThink(m, Sense(15.f, true, true), now);
        if (a == GunnerAct::Peek)
            ++peeks;
        GunnerMoved(m, a, true, m.cover, 2.f, now, 0);
    }
    CHECK(peeks == kMaxPeeks);
    CHECK(m.mode == GunnerMode::Fight);
}

TEST(no_cover_or_no_peek_leaves_it_fighting_in_the_open)
{
    GunnerMind m;
    m.mode = GunnerMode::Fight;
    CHECK(GunnerThink(m, Sense(15.f, true, true), 1000) == GunnerAct::TakeCover);
    GunnerMoved(m, GunnerAct::TakeCover, false, {}, 0.f, 1000, 0);
    CHECK(m.mode == GunnerMode::Fight && !m.hasCover);
    // Its strafe is what the glue does then; a while before it looks for cover again.
    CHECK(GunnerThink(m, Sense(15.f, true, true), 2000) != GunnerAct::TakeCover);

    GunnerMind p;
    p.mode = GunnerMode::Cover;
    p.hasCover = true;
    p.untilMs = 500;
    CHECK(GunnerThink(p, Sense(15.f, false), 1000) == GunnerAct::Peek);
    GunnerMoved(p, GunnerAct::Peek, false, {}, 0.f, 1000, 0);
    CHECK(p.mode == GunnerMode::Fight);
}

TEST(squadmates_spread_round_the_target)
{
    int flankers = 0, close = 0;
    float const deg = 3.14159265f / 180.f;
    for (uint64_t g = 1; g <= 300; ++g)
    {
        uint64_t const guid = 0xF130000000000000ull | (g * 7919u);
        float const o = GunnerBearingOffset(guid);
        CHECK(o == GunnerBearingOffset(guid));          // fixed per gunner
        float const a = std::fabs(o) / deg;
        if (a >= 45.f - 1e-3f && a <= 90.f + 1e-3f)
            ++flankers;
        else if (a <= 30.f + 1e-3f)
            ++close;
        else
            CHECK(false);
    }
    // About a third flank.
    CHECK(flankers > 60 && flankers < 140);
    CHECK(close + flankers == 300);
}

TEST(firing_spots_lie_beside_the_gunner_at_its_distance)
{
    Vec3 const target{0.f, 0.f, 0.f};
    Vec3 const self{15.f, 0.f, 0.f};
    std::vector<Vec3> const spots = FiringSpots(self, target, 20.f, 0.f);
    CHECK(spots.size() == 8);
    for (Vec3 const& p : spots)
    {
        float const step = (p - self).Length();
        CHECK(step >= 2.5f && step <= 7.f);
        float const r = std::sqrt(p.x * p.x + p.y * p.y);
        CHECK(r > kGunnerKnifeRange + 2.f && r <= 20.f + 1e-3f);
    }
    CHECK(CoverSpots(self, 0).size() == 10);
    for (Vec3 const& p : CoverSpots(self, 7))
        CHECK((p - self).Length() <= kCoverRadius);
    CHECK(PeekSpots(self).size() == 8);
}

TEST(a_firing_spot_is_in_sight_toward_its_bearing_and_apart_from_its_squad)
{
    Vec3 const target{0.f, 0.f, 0.f};
    Vec3 const self{15.f, 0.f, 0.f};
    float const quarter = 3.14159265f / 2.f;
    // Two spots either side; it prefers the side of its bearing (a flanker heading round to +y).
    std::vector<GunnerSpot> spots = {
        {{14.7f, -3.f, 0.f}, true, true},
        {{14.7f, 3.f, 0.f}, true, true},
        {{14.f, 5.f, 0.f}, true, false},     // the target cannot see it: no firing spot
        {{13.f, 7.f, 0.f}, false, true},     // unreachable
    };
    CHECK(ChooseFiringSpot(spots, self, target, quarter, {}, 0) == 1);
    CHECK(ChooseFiringSpot(spots, self, target, -quarter, {}, 0) == 0);
    // A squadmate standing on the +y spot pushes it to the other.
    CHECK(ChooseFiringSpot(spots, self, target, quarter, {{14.5f, 3.2f, 0.f}}, 0) == 0);
    // Nothing fit: none.
    std::vector<GunnerSpot> const blind = {{{14.f, 5.f, 0.f}, true, false}};
    CHECK(ChooseFiringSpot(blind, self, target, 0.f, {}, 0) == -1);
}

TEST(cover_is_the_nearest_reachable_spot_the_target_cannot_see)
{
    Vec3 const target{0.f, 0.f, 0.f};
    Vec3 const self{15.f, 0.f, 0.f};
    std::vector<GunnerSpot> const spots = {
        {{20.f, 0.f, 0.f}, true, true},      // in sight
        {{15.f, 9.f, 0.f}, true, false},     // hidden, 9 yards
        {{15.f, -5.f, 0.f}, true, false},    // hidden, 5 yards
        {{15.f, 3.f, 0.f}, false, false},    // hidden but unreachable
        {{2.f, 0.f, 0.f}, true, false},      // hidden, but at the target's feet
    };
    CHECK(ChooseCoverSpot(spots, self, target) == 2);
    std::vector<GunnerSpot> const none = {{{20.f, 0.f, 0.f}, true, true}};
    CHECK(ChooseCoverSpot(none, self, target) == -1);
    // A peek: the nearest reachable spot in sight of the target.
    std::vector<GunnerSpot> const peeks = {
        {{18.f, 0.f, 0.f}, true, true},
        {{16.f, 2.f, 0.f}, true, true},
        {{15.5f, 0.f, 0.f}, true, false},
    };
    CHECK(ChoosePeekSpot(peeks, self) == 1);
}

TEST(hurt_is_a_big_loss_of_health_lately)
{
    HurtWindow h;
    CHECK(!h.Update(100.f, 0));
    CHECK(!h.Update(80.f, 1000));
    CHECK(h.Update(65.f, 2000));            // 35 points in two seconds
    // Long after, the window has moved on.
    CHECK(!h.Update(65.f, 2000 + kHurtWindowMs + 1));
    // Slow losses never add up to hurt.
    HurtWindow slow;
    for (uint32_t t = 0; t <= 60000; t += 1000)
        CHECK(!slow.Update(100.f - float(t) / 1000.f, t));
}

TEST(a_gunner_remembers_where_it_last_saw_its_target)
{
    SightTrack s;
    s.Update(7, true, false, {1.f, 2.f, 3.f}, 1000);
    CHECK(s.HiddenMs(1000) == 0);
    // Out of sight, it counts the time hidden from where it last saw it.
    s.Update(7, false, false, {9.f, 9.f, 9.f}, 6000);
    CHECK(s.HiddenMs(8500) == 2500);
    CHECK_NEAR(s.lastSeen.x, 1.f, 1e-6);
    s.Update(7, true, false, {4.f, 4.f, 4.f}, 9000);
    CHECK(s.HiddenMs(9000) == 0);
    CHECK_NEAR(s.lastSeen.x, 4.f, 1e-6);
    // Never seen: no time hidden to count.
    SightTrack never;
    never.Update(9, false, false, {}, 1000);
    CHECK(never.HiddenMs(20000) == 0);
}

TEST(a_gunner_lobs_a_frag_at_a_target_long_behind_cover)
{
    // A roll under the chance throws (roll % 100 < 30).
    CHECK(NpcShouldThrowGrenade(6000, 15.f, true, false, 10));
    CHECK(!NpcShouldThrowGrenade(6000, 15.f, true, false, 50));       // not this think
    CHECK(!NpcShouldThrowGrenade(4000, 15.f, true, false, 10));       // not hidden long enough
    CHECK(!NpcShouldThrowGrenade(6000, 5.f, true, false, 10));        // too near
    CHECK(!NpcShouldThrowGrenade(6000, 30.f, true, false, 10));       // too far
    CHECK(!NpcShouldThrowGrenade(6000, 15.f, false, false, 10));      // its own cooldown
    CHECK(!NpcShouldThrowGrenade(6000, 15.f, true, true, 10));        // one of its group's in the air
    // A longer lob for a longer throw, within reason.
    CHECK(NpcLobSeconds(8.f) < NpcLobSeconds(25.f));
    CHECK(NpcLobSeconds(0.f) >= 0.8f && NpcLobSeconds(100.f) <= 2.5f);
}
