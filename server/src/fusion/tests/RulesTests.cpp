/*
 * Azeroth Warfare: the fusion rules' tests. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"

#include "fusion/Ammo.h"
#include "fusion/Creatures.h"
#include "fusion/Detection.h"
#include "fusion/Killstreak.h"
#include "fusion/Movement.h"
#include "fusion/Progression.h"
#include "fusion/Pvp.h"

#include <cmath>

using namespace Fusion;

// ── Detection ───────────────────────────────────────────────────────────────────────────────

TEST(wow_aggro_radius_by_level_gap)
{
    CHECK_NEAR(WowAggroRadius(10, 10), 20.f, 1e-6);
    CHECK_NEAR(WowAggroRadius(8, 10), 18.f, 1e-6);
    CHECK_NEAR(WowAggroRadius(1, 60), 5.f, 1e-6);
    CHECK_NEAR(WowAggroRadius(60, 1), 45.f, 1e-6);
}

TEST(level_10_crawls_past_level_8_murlocs)
{
    // The design doc's example: prone and moving, in front of the murloc: about 4.6 yards.
    Observer murloc{8, CreatureType::Humanoid, {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
    Intruder player{10, {4.f, 0.f, 0.f}, Stance::Prone, Gait::Walk, false};
    float r = DetectionRadius(murloc, player);
    CHECK_NEAR(r, 18.f * 0.3f * 0.85f, 1e-4);
    CHECK(r > 4.f && r < 5.f);
}

TEST(stance_motion_facing_and_ninja_shrink_detection)
{
    Observer o{20, CreatureType::Humanoid, {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
    Intruder front{20, {5.f, 0.f, 0.f}, Stance::Stand, Gait::Run, false};
    Intruder behind = front;
    behind.position = {-5.f, 0.f, 0.f};
    CHECK_NEAR(DetectionRadius(o, behind), DetectionRadius(o, front) * 0.5f, 1e-4);
    Intruder sprint = front;
    sprint.gait = Gait::Sprint;
    CHECK(DetectionRadius(o, sprint) > DetectionRadius(o, front));
    Intruder ninja = front;
    ninja.ninja = true;
    CHECK_NEAR(DetectionRadius(o, ninja), DetectionRadius(o, front) * 0.7f, 1e-4);
    // Beasts smell what's behind them.
    Observer wolf = o;
    wolf.type = CreatureType::Beast;
    CHECK_NEAR(DetectionRadius(wolf, behind), DetectionRadius(wolf, front) * 0.8f, 1e-4);
}

TEST(alert_meter_fills_over_a_second_and_drains)
{
    AlertMeter m;
    CHECK(!m.Update(0.5f, true));
    CHECK(m.Alerted());
    CHECK(m.Update(0.6f, true));        // crosses full: hostile now
    CHECK(!m.Update(0.1f, true));       // fires once
    m.Update(5.f, false);
    CHECK(!m.Alerted());
}

// ── PvP ─────────────────────────────────────────────────────────────────────────────────────

namespace
{
    PvpPlayer P(uint64_t guid, Team team, uint32_t level = 30)
    {
        PvpPlayer p;
        p.guid = guid;
        p.team = team;
        p.level = level;
        return p;
    }
}

TEST(factions_fight_and_a_faction_needs_both_flags)
{
    PvpPlayer a = P(1, Team::Alliance), h = P(2, Team::Horde), a2 = P(3, Team::Alliance);
    CHECK(CanAttack(a, h));
    CHECK(!CanAttack(a, a2));
    a.ffaFlag = true;
    CHECK(!CanAttack(a, a2));
    a2.ffaFlag = true;
    CHECK(CanAttack(a, a2));
    a.groupId = a2.groupId = 7;
    CHECK(!CanAttack(a, a2));
    CHECK(!CanAttack(a, a));
}

TEST(sanctuaries_and_low_level_protection)
{
    PvpPlayer a = P(1, Team::Alliance, 60), h = P(2, Team::Horde, 45);
    CHECK(!CanAttack(a, h));            // 15 levels below
    CHECK(CanAttack(a, h, true));       // unless they started it
    CHECK(CanAttack(h, a));             // up is always allowed
    h.level = 55;
    CHECK(CanAttack(a, h));
    h.inSanctuary = true;
    CHECK(!CanAttack(a, h));
}

TEST(collision_only_between_enemies)
{
    PvpPlayer a = P(1, Team::Alliance), h = P(2, Team::Horde), a2 = P(3, Team::Alliance);
    CHECK(BodiesCollide(a, h));
    CHECK(!BodiesCollide(a, a2));
    a.ffaFlag = a2.ffaFlag = true;
    CHECK(BodiesCollide(a, a2));
    a.groupId = a2.groupId = 4;
    CHECK(!BodiesCollide(a, a2));
    h.inSanctuary = true;
    CHECK(!BodiesCollide(a, h));
}

TEST(ffa_flag_turns_off_after_five_quiet_minutes)
{
    FfaFlag f;
    f.RequestOn();
    CHECK(f.On());
    f.RequestOff();
    f.Update(200.f, false);
    CHECK(f.On());
    f.Update(10.f, true);               // combat restarts the countdown
    f.Update(250.f, false);
    CHECK(f.On());
    f.Update(60.f, false);
    CHECK(!f.On());
}

// ── Movement ────────────────────────────────────────────────────────────────────────────────

TEST(speeds_follow_stance_gait_and_weapon_weight)
{
    WeaponProfile lmg;
    lmg.moveSpeedScale = 0.875f;
    lmg.adsMoveSpeedScale = 0.4f;
    MovementState m;
    m.gait = Gait::Sprint;
    CHECK_NEAR(SpeedMultiplier(m, &lmg), kSprintSpeedScale * 0.875f, 1e-5);
    CHECK_NEAR(kSprintSpeedScale, 1.65f, 1e-6); // over MW2's 1.5: WoW's run is already quick
    m.aiming = true;                    // no sprinting while aiming
    CHECK_NEAR(SpeedMultiplier(m, &lmg), 0.875f * 0.4f, 1e-5);
    MovementState prone;
    prone.stance = Stance::Prone;
    CHECK_NEAR(SpeedMultiplier(prone, nullptr), 0.15f, 1e-6); // MW2 player_proneSpeedScale
    MovementState back;
    back.gait = Gait::Backpedal;
    CHECK_NEAR(SpeedMultiplier(back, nullptr), 0.85f, 1e-6);
    MovementState light;
    light.lightweight = true;
    CHECK_NEAR(SpeedMultiplier(light, nullptr), kLightweightSpeedScale, 1e-6);
    CHECK_NEAR(kLightweightSpeedScale, 1.12f, 1e-6);
    MovementState lightSprint;
    lightSprint.lightweight = true;
    lightSprint.gait = Gait::Sprint;    // Lightweight speeds the sprint too
    CHECK_NEAR(SpeedMultiplier(lightSprint, nullptr), 1.65f * 1.12f, 1e-5);
    CHECK_NEAR(StanceHeightScale(Stance::Crouch), 0.65f, 1e-6);
}

TEST(sprint_stamina_and_marathon)
{
    SprintStamina s;
    CHECK(s.Update(3.f, true, false));
    CHECK(!s.Update(2.f, true, false));
    s.Update(10.f, false, false);
    CHECK_NEAR(s.current, s.max, 1e-6);
    SprintStamina m;
    CHECK(m.Update(100.f, true, true));
}

// ── Killstreaks ─────────────────────────────────────────────────────────────────────────────

namespace
{
    std::vector<StreakReward> Ladder()
    {
        return {{1, 3, false}, {2, 5, true}, {3, 7, true}};
    }
}

TEST(airspace_allows_two_per_area)
{
    Airspace a;
    auto t1 = a.TryLaunch(0, 10.f, 10.f);
    auto t2 = a.TryLaunch(0, 150.f, 50.f);
    CHECK(t1 && t2);
    CHECK(!a.TryLaunch(0, 100.f, 100.f));
    CHECK(a.TryLaunch(0, 450.f, 10.f).has_value());     // the next area over
    CHECK(a.TryLaunch(1, 10.f, 10.f).has_value());      // another map
    a.Release(*t1);
    CHECK(a.ActiveIn(0, 10.f, 10.f) == 1);
    CHECK(a.TryLaunch(0, 20.f, 20.f).has_value());
}

// ── Ammo ────────────────────────────────────────────────────────────────────────────────────

TEST(magazine_reserve_and_specials)
{
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 90;
    GunAmmo g;
    g.Reset(w);
    for (int i = 0; i < 30; ++i)
        CHECK(g.Fire());
    CHECK(!g.Fire());
    CHECK(g.Reload() == 30);
    CHECK(g.Reserve() == 60);

    g.LoadSpecial(AmmoType::Incendiary);
    CHECK(g.NextRound() == AmmoType::Basic);       // none carried yet
    g.QuartermasterRearm(9);
    CHECK(g.Special(AmmoType::Incendiary) == 0);   // not unlocked below 10
    g.QuartermasterRearm(10);
    CHECK(g.Special(AmmoType::Incendiary) == 120); // four magazines
    CHECK(g.Special(AmmoType::Explosive) == 0);    // level 20
    CHECK(g.NextRound() == AmmoType::Incendiary);
    g.Fire();
    CHECK(g.Special(AmmoType::Incendiary) == 119);
}

TEST(a_shotgun_loads_shell_by_shell)
{
    WeaponProfile w;
    w.magSize = 6;
    w.maxReserve = 2;
    GunAmmo g;
    g.Reset(w);
    CHECK(g.ReloadOne() == 0);                      // full
    g.Fire();
    g.Fire();
    g.Fire();
    CHECK(g.ReloadOne() == 1 && g.Mag() == 4 && g.Reserve() == 1);
    CHECK(g.ReloadOne() == 1 && g.Mag() == 5 && g.Reserve() == 0);
    CHECK(g.ReloadOne() == 0 && g.Mag() == 5);      // the reserve is dry: a partial magazine
}

TEST(each_akimbo_hand_has_its_own_magazine)
{
    // The akimbo profile counts both hands' rounds; each hand's GunAmmo holds one gun's.
    WeaponProfile w;
    w.magSize = 24;
    w.maxReserve = 36;
    w.akimbo = true;
    GunAmmo right, left;
    right.Reset(w);
    left.Reset(w);
    CHECK(right.Mag() == 12 && left.Mag() == 12);
    for (int i = 0; i < 12; ++i)
        CHECK(left.Fire());
    CHECK(!left.Fire() && right.Mag() == 12);
    CHECK(left.Reload() == 12 && left.Mag() == 12);
}

TEST(field_rearm_trickles_to_one_magazine_after_ten_quiet_seconds)
{
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 100;
    GunAmmo g;
    g.Reset(w);
    for (int i = 0; i < 4; ++i)
    {
        while (g.Fire()) {}
        g.Reload();
    }
    CHECK(g.Reserve() == 0);
    g.FieldRearm(5.f, 1.f);
    CHECK(g.Reserve() == 0);
    for (int i = 0; i < 100; ++i)
        g.FieldRearm(20.f, 1.f);
    CHECK(g.Reserve() == 30);       // limited ammunition: one magazine, not a free load
    AmmoRules legacy;
    legacy.limited = false;
    for (int i = 0; i < 100; ++i)
        g.FieldRearm(20.f, 1.f, legacy);
    CHECK(g.Reserve() == 50);       // the old model: half the reserve
    g.FullResupply();
    CHECK(g.Reserve() == 100 && g.Mag() == 30);
}

TEST(quartermaster_fee_is_small)
{
    CHECK(QuartermasterFee(10) == 300);     // 3 silver
    CHECK(QuartermasterFee(60) == 4800);    // 48 silver
}

// ── Progression and health ──────────────────────────────────────────────────────────────────

TEST(xp_rates)
{
    CHECK(ScaleKillXp(100, false) == 40);
    CHECK(ScaleKillXp(100, true) == 80);
    CHECK(ScaleKillXp(1, false) == 1);
    CHECK(ScaleQuestXp(1000, false) == 1000);
    CHECK(UseRequirement(25) == 250);
}

TEST(regen_waits_and_wounds_stop_it)
{
    HealthRegen r;
    r.OnDamaged(false);
    CHECK(r.Update(4.f, 0.5f) == 0.f);
    CHECK(r.Update(1.5f, 0.5f) > 0.f);
    r.OnDamaged(true);
    CHECK(r.Update(6.f, 0.5f) == 0.f);      // wounded: past the delay but no regen
    CHECK(r.Update(3.f, 0.5f) > 0.f);
    CHECK_NEAR(ReviveTime(ReviverKind::Healer), 2.f, 1e-6);
    CHECK_NEAR(ReviveTime(ReviverKind::Anyone), 6.f, 1e-6);
}

// ── Creatures ───────────────────────────────────────────────────────────────────────────────

TEST(creature_profiles)
{
    CHECK(ClassifyCreature({CreatureType::Humanoid, false, true, false, false}) == Profile::Soldier);
    CHECK(ClassifyCreature({CreatureType::Humanoid, true, false, false, false}) == Profile::Caster);
    CHECK(ClassifyCreature({CreatureType::Beast}) == Profile::Rusher);
    CHECK(ClassifyCreature({CreatureType::Beast, false, false, true, false}) == Profile::Flyer);
    CHECK(ClassifyCreature({CreatureType::Undead}) == Profile::Horde);
    CHECK(ClassifyCreature({CreatureType::Giant}) == Profile::Juggernaut);
    CHECK(ClassifyCreature({CreatureType::Mechanical}) == Profile::Machine);
    CHECK(ClassifyCreature({CreatureType::Critter}) == Profile::Passive);
}

TEST(rushers_close_faster_from_far)
{
    // Every profile that fights hand to hand runs at x1.6 until it is in its reach (here 5 yards)
    // and at its own pace there; the bands keep it from flickering at the edge.
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 40.f, 5.f, false), kGunfight.chaseSpeed, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 80.f, 5.f, true), 1.6f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 5.f, 5.f, true), 1.f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 3.f, 5.f, false), 1.f, 1e-6);
    // Fast, it eases off within 2 yards of its reach; slowed, it speeds up past 4.
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 7.5f, 5.f, true), 1.6f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 6.5f, 5.f, true), 1.f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 8.f, 5.f, false), 1.f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 9.5f, 5.f, false), 1.6f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Soldier, 40.f, 5.f, false), 1.6f, 1e-6);   // melee soldiers close in too
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Horde, 40.f, 5.f, false), 1.6f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Juggernaut, 0.f, 5.f, true), 1.f, 1e-6);
}

TEST(unreachable_targets_trigger_the_ranged_answer)
{
    ReachTracker t;
    t.Update(2.f, false);
    CHECK(!t.Unreachable());
    t.Update(1.5f, false);
    CHECK(t.Unreachable());
    CHECK_NEAR(t.DamageTakenMultiplier(), 0.5f, 1e-6);
    t.Update(0.1f, true);
    CHECK(!t.Unreachable());
}

// ── MW2 movement: sprint-out, mantle, knife lunge ───────────────────────────────────────────────

TEST(sprint_out_delay)
{
    CHECK(!SprintOutReady(0.1f, false));
    CHECK(SprintOutReady(0.25f, false));
    CHECK(!SprintOutReady(5.f, true));
}

TEST(mantle_rise_window)
{
    MantleProbe p;
    p.feetZ = 0.f;
    p.headroomClear = true;

    p.groundAhead[0] = 0.f; p.groundAhead[1] = 1.f; p.groundAhead[2] = 1.f;
    auto rise = MantleRise(p);
    CHECK(rise.has_value());
    CHECK_NEAR(rise.value_or(0.f), 1.f, 1e-4);

    p.groundAhead[0] = p.groundAhead[1] = p.groundAhead[2] = 0.3f;
    CHECK(!MantleRise(p).has_value());      // too low: a step, not a ledge

    p.groundAhead[0] = 0.f; p.groundAhead[1] = 2.f; p.groundAhead[2] = 2.f;
    CHECK(!MantleRise(p).has_value());      // too high: a wall

    // Higher than WoW's own jump clears (about 1.6 yd): what makes a mantle more than a hop.
    p.groundAhead[0] = 0.f; p.groundAhead[1] = 1.7f; p.groundAhead[2] = 1.7f;
    CHECK(MantleRise(p).has_value());
    p.groundAhead[1] = p.groundAhead[2] = kMantleMaxRise;
    CHECK(MantleRise(p).has_value());
}

TEST(mantle_ledge_just_past_arm_length_counts)
{
    // Four columns out to 1.6 yd: a crate a step beyond 1.2 yd is still a ledge.
    CHECK(kMantleColumns == 4);
    CHECK_NEAR(kMantleProbeReach[kMantleColumns - 1], 1.6f, 1e-4);
    CHECK_NEAR(kMantleReach, kMantleProbeReach[kMantleColumns - 1], 1e-4);
    MantleProbe p;
    p.headroomClear = true;
    p.groundAhead[kMantleColumns - 1] = 1.2f;
    CHECK(MantleRise(p).has_value());
}

TEST(mantle_refused_falling_or_ceiling)
{
    MantleProbe p;
    p.feetZ = 0.f;
    p.groundAhead[0] = 0.f; p.groundAhead[1] = 1.f; p.groundAhead[2] = 1.f;
    p.headroomClear = true;
    p.airSeconds = 1.5f;                    // a fall, long past a jump's rise
    CHECK(!MantleRise(p).has_value());
    p.airSeconds = 0.f;
    p.headroomClear = false;
    CHECK(!MantleRise(p).has_value());
}

TEST(mantle_accepts_the_whole_rise_of_a_jump)
{
    // Space starts WoW's jump with the request, so the server may read the body anywhere on the
    // way up: every point of the rise is accepted, by height and by time in the air.
    float const apex = kWowJumpSpeed * kWowJumpSpeed / (2.f * kWowGravity);
    CHECK(apex > 1.5f && apex < 1.7f);
    float const rise = kWowJumpSpeed / kWowGravity;
    MantleProbe p;
    p.headroomClear = true;
    p.groundAhead[1] = 1.2f;
    for (float t = 0.f; t <= rise + 1e-4f; t += rise / 8.f)
    {
        p.startHeight = kWowJumpSpeed * t - kWowGravity * t * t / 2.f;
        p.airSeconds = t;
        CHECK(MantleRise(p).has_value());
    }
}

TEST(mantle_impulse_clears_ledge)
{
    for (float rise : {0.6f, 1.f, 1.7f, kMantleMaxRise})
    {
        Impulse const imp = MantleImpulse(rise);
        float const apex = imp.vertical * imp.vertical / (2.f * kWowGravity);
        CHECK(apex >= rise + 0.4f - 1e-4f);
        // Never slower forward than a run: the climb carries the body onto the ledge.
        CHECK(imp.horizontal >= kMantleMinForward);
        // The body slides up the ledge's face (WoW keeps a launch's speed against a wall) and,
        // from a face as far as the farthest column, is still over the ledge, half a yard in,
        // when it comes back down to the ledge's height.
        float const g = kWowGravity, v = imp.vertical;
        float const down = (v + std::sqrt(v * v - 2.f * g * rise)) / g;
        CHECK(down * imp.horizontal >= kMantleReach + 0.5f);
    }
}

TEST(lunge_range_and_cone)
{
    PerkFlags none;
    PerkFlags commando;
    commando.commando = true;
    // Five times MW2's reach, by request: 20 yards, 40 with Commando.
    CHECK_NEAR(LungeRange(none), 20.f, 1e-6);
    CHECK_NEAR(LungeRange(commando), 40.f, 1e-6);
    CHECK(InLungeCone(Vec3(0, 0, 0), 0.f, Vec3(18, 2, 0)));
    CHECK(InLungeCone(Vec3(0, 0, 0), 0.f, Vec3(3, 0, 0)));
    CHECK(!InLungeCone(Vec3(0, 0, 0), 0.f, Vec3(0, 3, 0)));
}

TEST(a_melee_swing_never_lunges_without_a_charge_grant)
{
    // The lunge bug: every standing swing at an enemy within 20 yards dashed the player at it.
    // A swing is a swing now; only a grant (a warrior's Charge perk) makes it dash.
    LungeGrant none;
    CHECK(!SwingLunges(none));
    LungeGrant charge;
    charge.charge = true;
    CHECK(SwingLunges(charge));
}

TEST(lunge_creature_damage)
{
    CHECK_NEAR(LungeCreatureDamage(100.f), 300.f, 1e-4);
}

TEST(lunge_dash_covers_the_distance)
{
    // A knock-back carries the body only while it is airborne: the arc's airtime times the
    // horizontal speed must cover the distance to the target.
    for (float d : {2.f, 4.f, 20.f, 40.f})
    {
        Impulse const dash = LungeImpulse(d);
        float const airtime = 2.f * dash.vertical / kWowGravity;
        CHECK_NEAR(dash.horizontal * airtime, d, 0.05);
        CHECK(dash.horizontal <= 40.f);
        CHECK(dash.vertical * dash.vertical / (2.f * kWowGravity) <= 2.5f);   // a dash, not a leap
    }
}

TEST(airborne_spread_is_double)
{
    CHECK_NEAR(kAirborneSpreadMultiplier, 2.f, 1e-6);
}

TEST(gunfire_is_heard)
{
    // An open shot carries 60 yards, a suppressed one 15 (MW2: suppressed fire stays off the
    // radar); Cold-Blooded halves either; a wall between halves it again.
    CHECK_NEAR(GunfireHearing(false, false), 60.f, 1e-6);
    CHECK_NEAR(GunfireHearing(true, false), 15.f, 1e-6);
    CHECK_NEAR(GunfireHearing(false, true), 30.f, 1e-6);
    CHECK_NEAR(kWallMuffle, 0.5f, 1e-6);
}

TEST(melee_soldiers_close_the_gap)
{
    // A humanoid with only its fists (a miner) runs at a gunman; a caster until it is in casting
    // range (30 yards), then at its pace; a critter never.
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Soldier, 5.f, 5.f, true), 1.f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Soldier, 40.f, 5.f, false), 1.6f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Caster, 50.f, 5.f, false), 1.6f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Caster, 25.f, 5.f, true), 1.f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Passive, 40.f, 5.f, false), 1.f, 1e-6);
}

TEST(creatures_chase_further)
{
    CHECK_NEAR(kLeashScale, 2.f, 1e-6);
}
