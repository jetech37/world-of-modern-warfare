/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Defense.h"
#include "fusion/Killstreak.h"

#include <cmath>

using namespace Fusion;

namespace
{
    std::vector<StreakReward> Ladder()
    {
        return {{1, 3, false}, {2, 5, true}, {3, 7, true}};
    }
}

TEST(streak_values_in_tenths)
{
    CHECK(StreakTenths(KillKind::Player) == 10);        // an enemy player: one kill
    CHECK(StreakTenths(KillKind::Creature) == 1);       // a mob worth XP: a tenth
    CHECK(StreakTenths(KillKind::GrayCreature) == 0);   // gray and critters: nothing
    CHECK(StreakTenths(KillKind::EliteCreature, 1) == 10);  // an elite: 1 / N each
    CHECK(StreakTenths(KillKind::EliteCreature, 2) == 5);
    CHECK(StreakTenths(KillKind::EliteCreature, 3) == 3);   // 0.33, to the nearest tenth
    CHECK(StreakTenths(KillKind::EliteCreature, 4) == 3);   // 0.25 rounds up
    CHECK(StreakTenths(KillKind::EliteCreature, 40) == 1);  // a raid still earns a tenth
    CHECK(StreakTenths(KillKind::EliteCreature, 0) == 10);  // no damager list: the killer's
}

TEST(streak_rewards_at_their_kill_counts)
{
    Streak s;
    s.SetLoadout(Ladder(), false);
    CHECK(s.AddKill(KillKind::Player).empty());
    CHECK(s.AddKill(KillKind::GrayCreature).empty());
    CHECK(s.Tenths() == 10);
    CHECK(s.AddKill(KillKind::Player).empty());
    for (int i = 0; i < 9; ++i)
        CHECK(s.AddKill(KillKind::Creature).empty());   // 2.9
    auto r = s.AddKill(KillKind::Creature);             // 3.0: the first reward
    CHECK(r.size() == 1 && r[0].id == 1);
    CHECK(s.Count() == 3);
    s.Reset();
    CHECK(s.Tenths() == 0);
    CHECK(s.Earned().size() == 1);      // earned rewards outlive the death
    CHECK(s.Use(1));
    CHECK(!s.Use(1));
}

TEST(a_split_elite_can_cross_a_threshold)
{
    Streak s;
    s.SetLoadout(Ladder(), false);
    s.AddKill(KillKind::Player);
    s.AddKill(KillKind::Player);
    for (int i = 0; i < 8; ++i)
        s.AddKill(KillKind::Creature);                  // 2.8
    auto r = s.AddKill(KillKind::EliteCreature, 2);     // +0.5 = 3.3
    CHECK(r.size() == 1 && r[0].id == 1);
    CHECK(s.Tenths() == 33);
}

TEST(hardline_takes_one_kill_off)
{
    Streak s;
    s.SetLoadout(Ladder(), true);
    s.AddKill(KillKind::Player);
    auto r = s.AddKill(KillKind::Player);
    CHECK(r.size() == 1 && r[0].id == 1);
}

TEST(damage_credit_lists_each_damager_once)
{
    DamageCredit c;
    c.Add(100, 1, 0);
    c.Add(100, 2, 10);
    c.Add(100, 1, 20);
    c.Add(200, 3, 20);
    auto d = c.Take(100);
    CHECK(d.size() == 2);
    CHECK(c.Take(100).empty());         // taken: forgotten
    CHECK(c.Tracked() == 1);
}

TEST(damage_credit_forgets_a_left_alone_victim)
{
    DamageCredit c;
    c.Add(100, 1, 0);
    c.Add(200, 2, DamageCredit::kForgetMs - 1000);
    c.Expire(DamageCredit::kForgetMs + 1);   // 100 untouched past the limit, 200 still fresh
    CHECK(c.Take(100).empty());
    CHECK(c.Take(200).size() == 1);
}

TEST(damage_credit_survives_the_clock_wrapping)
{
    DamageCredit c;
    c.Add(100, 1, 0xFFFFFF00u);
    c.Expire(0x100u);                   // 512 ms later, across the wrap
    CHECK(c.Take(100).size() == 1);
}

TEST(the_default_ladder_is_the_hud_ladder)
{
    auto l = DefaultStreakLadder();
    CHECK(l.size() == 8);
    CHECK(l[0].id == StreakId::Uav && l[0].kills == 3 && !l[0].aerial);
    // MW2's sentry at 5, on the ground (construction no longer builds one).
    CHECK(l[1].id == StreakId::SentryGun && l[1].kills == 5 && !l[1].aerial);
    CHECK(l[2].id == StreakId::PredatorMissile && l[2].kills == 5 && l[2].aerial);
    CHECK(l[3].id == StreakId::HarrierStrike && l[3].kills == 7);
    // The lore streaks' gunship and bomber, at MW2's 7 and 9.
    CHECK(l[4].id == StreakId::AttackHelicopter && l[4].kills == 7 && l[4].aerial);
    CHECK(l[5].id == StreakId::StealthBomber && l[5].kills == 9 && l[5].aerial);
    CHECK(l[6].id == StreakId::ChopperGunner && l[6].kills == 11);
    CHECK(l[7].id == StreakId::TacticalNuke && l[7].kills == 25 && !l[7].aerial);
    for (size_t i = 1; i < l.size(); ++i)
        CHECK(l[i - 1].kills <= l[i].kills);
}

TEST(five_kills_earn_the_sentry_and_the_missile_together)
{
    Streak s;
    s.SetLoadout(DefaultStreakLadder(), false);
    for (int i = 0; i < 4; ++i)
        s.AddKill(KillKind::Player);
    auto r = s.AddKill(KillKind::Player);
    CHECK(r.size() == 2 && r[0].id == StreakId::SentryGun && r[1].id == StreakId::PredatorMissile);
    CHECK(s.Use(StreakId::SentryGun));
    CHECK(!s.Use(StreakId::SentryGun));
}

TEST(the_streak_sentry_stands_in_front_and_lasts_a_minute_and_a_half)
{
    CHECK(Defense::kStreakSentryMs == 90000u);
    Vec3 const at = Defense::StreakSentrySpot({10.f, 20.f, 5.f}, 0.f);
    CHECK(std::fabs(at.x - (10.f + Defense::kStreakSentryAhead)) < 1e-4f && std::fabs(at.y - 20.f) < 1e-4f && at.z == 5.f);
    Vec3 const side = Defense::StreakSentrySpot({0.f, 0.f, 0.f}, 1.5707963f);
    CHECK(std::fabs(side.x) < 1e-3f && std::fabs(side.y - Defense::kStreakSentryAhead) < 1e-3f);
}

TEST(a_reward_is_paid_once_a_life)
{
    // Hardline on: the UAV at 2. Hardline off mid-streak (a class swap): crossing 3 must not pay it again.
    Streak s;
    s.SetLoadout(Ladder(), true);
    s.AddKill(KillKind::Player);
    CHECK(s.AddKill(KillKind::Player).size() == 1);
    s.SetLoadout(Ladder(), false);
    CHECK(s.AddKill(KillKind::Player).empty());
    s.Reset();                          // a new life pays again
    s.AddKill(KillKind::Player);
    s.AddKill(KillKind::Player);
    CHECK(s.AddKill(KillKind::Player).size() == 1);
}

TEST(player_kills_are_worth_less_the_lower_the_victim)
{
    CHECK(PlayerKillTenths(30, 30) == 10);  // an equal: a whole kill
    CHECK(PlayerKillTenths(30, 45) == 10);  // a higher level: still at most one
    CHECK(PlayerKillTenths(30, 29) == 9);   // a tenth less per level below
    CHECK(PlayerKillTenths(30, 25) == 5);
    CHECK(PlayerKillTenths(30, 21) == 1);   // nine below: the floor
    CHECK(PlayerKillTenths(60, 1) == 1);    // never nothing
    Streak s;
    s.SetLoadout({{1, 3, false}}, false);
    CHECK(s.AddTenths(0).empty() && s.Tenths() == 0);   // a creature kill adds nothing
    s.AddTenths(PlayerKillTenths(40, 40));
    s.AddTenths(PlayerKillTenths(40, 40));
    CHECK(s.AddTenths(PlayerKillTenths(40, 35)).empty());  // 2.5 kills: not yet 3
    CHECK(s.AddTenths(PlayerKillTenths(40, 35)).size() == 1);  // 3.0
}
