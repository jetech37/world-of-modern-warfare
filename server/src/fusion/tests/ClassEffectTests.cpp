/*
 * World of Modern Warfare: the class options' custom effects (fusion/ClassEffects.h) and the
 * options the polish pass of 2026-10-12 made whole (design/CLASS_CATALOG.md section 15).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/ClassEffects.h"
#include "fusion/Classes.h"
#include "fusion/Downed.h"

#include <set>
#include <string>

using namespace Fusion;
using namespace Fusion::Classes;

namespace
{
    uint8_t ImmunitiesOfId(uint16_t id) { return ImmunitiesOf(id); }
}

TEST(class_states_make_their_holder_immune_as_the_catalog_says)
{
    // Rampage: stun, fear, knockback; Warlord's Command: stun and fear for the party.
    CHECK(ImmunitiesOfId(161) == (IMMUNE_STUN | IMMUNE_FEAR | IMMUNE_KNOCKBACK));
    CHECK(ImmunitiesOfId(163) == (IMMUNE_STUN | IMMUNE_FEAR));
    // Blood Frenzy's Frenzy and Avenging Crusade: slows; Death Wish: fear.
    CHECK(ImmunitiesOfId(141) == IMMUNE_SLOW);
    CHECK(ImmunitiesOfId(261) == IMMUNE_SLOW);
    CHECK(ImmunitiesOfId(151) == IMMUNE_FEAR);
    // Avatar of Stone: knockback and slows; Demon Form: fear and slows; the worgen's momentum.
    CHECK(ImmunitiesOfId(762) == (IMMUNE_KNOCKBACK | IMMUNE_SLOW));
    CHECK(ImmunitiesOfId(962) == (IMMUNE_FEAR | IMMUNE_SLOW));
    CHECK(ImmunitiesOfId(1161) == (IMMUNE_STUN | IMMUNE_FEAR));
    // Nothing else grants any.
    CHECK(ImmunitiesOfId(111) == 0 && ImmunitiesOfId(0) == 0 && ImmunitiesOfId(2001) == 0);
}

TEST(class_control_effects_are_shorter_on_players)
{
    DurationCap const* disarm = DurationCapOf(103);
    CHECK(disarm && CappedMs(*disarm, false) == 4000 && CappedMs(*disarm, true) == 3000);
    DurationCap const* hammer = DurationCapOf(202);
    CHECK(hammer && CappedMs(*hammer, false) == 5000 && CappedMs(*hammer, true) == 3000);
    CHECK(DurationCapOf(101) == nullptr);
    // The audit's duration is the creature's.
    CHECK(SpecOf(103)->durationMs == 4000 && SpecOf(202)->durationMs == 5000);
}

TEST(class_shield_charge_sweeps_its_path)
{
    Vec3 const a{0.f, 0.f, 0.f}, b{20.f, 0.f, 0.f};
    CHECK(NearSegment(a, b, {10.f, 2.f, 0.f}, 2.5f));
    CHECK(NearSegment(a, b, {20.f, 0.f, 5.f}, 2.5f));     // height does not matter
    CHECK(!NearSegment(a, b, {10.f, 3.f, 0.f}, 2.5f));
    CHECK(!NearSegment(a, b, {24.f, 0.f, 0.f}, 2.5f));    // past its end
    CHECK(!NearSegment(a, b, {-3.f, 0.f, 0.f}, 2.5f));    // behind its start
    CHECK(NearSegment(a, a, {1.f, 1.f, 0.f}, 2.f));       // no length: a circle
}

TEST(class_shadowstep_keeps_a_second_charge)
{
    ChargePool p;
    p.value = 2;
    p.max = 2;
    p.regenMs = 20000;
    CHECK(p.Use(1000) && p.value == 1 && p.nextMs == 21000);
    CHECK(p.Use(2000) && p.value == 0);
    CHECK(!p.Use(3000));
    p.Update(21000);
    CHECK(p.value == 1 && p.nextMs == 41000);
    p.Update(41000);
    CHECK(p.value == 2 && p.nextMs == 0);
    // A long wait does not overfill.
    CHECK(p.Use(50000));
    p.Update(500000);
    CHECK(p.value == 2);
}

TEST(class_camouflage_fades_when_still_and_breaks_on_a_shot)
{
    Camouflage c;
    CHECK(c.Update(0, false, false, false) == Camouflage::Step::None);     // off
    CHECK(c.Toggle(1000) && c.on && !c.hidden);
    CHECK(c.Update(2000, false, false, false) == Camouflage::Step::None);
    CHECK(c.Update(2500, true, false, false) == Camouflage::Step::None);   // running: the clock restarts
    CHECK(c.Update(4000, false, false, false) == Camouflage::Step::None);
    CHECK(c.Update(4500, false, false, false) == Camouflage::Step::Fade);
    CHECK(c.hidden);
    // Walking keeps it.
    CHECK(c.Update(5000, true, false, false) == Camouflage::Step::None && c.hidden);
    // A shot breaks it, and it waits 10 s.
    CHECK(c.Update(6000, false, true, false) == Camouflage::Step::Break);
    CHECK(!c.on && !c.hidden);
    CHECK(!c.Toggle(10000));
    CHECK(c.Toggle(16000) && c.on);
    // A sprint breaks it too; a press turns it off at once.
    CHECK(c.Update(18100, false, false, false) == Camouflage::Step::Fade);
    CHECK(c.Update(18200, false, false, true) == Camouflage::Step::Break);
    CHECK(c.Toggle(28200));
    CHECK(c.Toggle(28300) && !c.on);
}

TEST(class_rescues_from_going_down_come_in_order)
{
    CHECK(RescueOnDown(true, true, true) == Rescue::Spirit);
    CHECK(RescueOnDown(false, true, true) == Rescue::Reincarnation);
    CHECK(RescueOnDown(false, false, true) == Rescue::Soulstone);
    CHECK(RescueOnDown(false, false, false) == Rescue::None);
    CHECK(kSpiritMs == 12000);
    CHECK_NEAR(kReincarnationShare, 0.4f, 1e-6f);
    CHECK_NEAR(kSoulstoneShare, 0.4f, 1e-6f);
    CHECK(kReincarnationCooldownMs == 20u * 60u * 1000u);
}

TEST(class_rallying_cry_gives_a_downed_ally_more_time)
{
    Downed::State d;
    d.Extend(10000);
    CHECK(!d.Active() && d.untilMs == 0);       // nobody down: nothing
    d.GoDown(1000, 1000, 0, false, false);
    uint32_t const until = d.untilMs, total = d.totalMs;
    d.Extend(kRallyingCryBleedMs);
    CHECK(d.untilMs == until + 10000 && d.totalMs == total + 10000);
    CHECK(d.Update(until + 5000) == Downed::Tick::None);
    CHECK(d.Update(until + 10000) == Downed::Tick::BledOut);
}

TEST(class_totems_traps_and_constructs_have_their_limits)
{
    CHECK_NEAR(TotemMultiplier(false, false), 1.f, 1e-6f);
    CHECK_NEAR(TotemMultiplier(true, false), 1.5f, 1e-6f);
    CHECK_NEAR(TotemMultiplier(false, true), 2.f, 1e-6f);
    CHECK_NEAR(TotemMultiplier(true, true), 3.f, 1e-6f);
    CHECK(TotemLimit(false) == 3 && TotemLimit(true) == 5);
    CHECK(TrapLimit(false) == 4 && TrapLimit(true) == 8);
    CHECK_NEAR(TrapChainYards(false), 6.f, 1e-6f);
    CHECK_NEAR(TrapChainYards(true), 12.f, 1e-6f);
    CHECK(ConstructLimit(false) == 3 && ConstructLimit(true) == 6);
}

TEST(class_spellweave_pays_off_on_a_switch_of_school)
{
    uint32_t last = 0;
    CHECK_NEAR(WeaveBonus(last, 4), 1.f, 1e-6f);            // the first spell
    CHECK_NEAR(WeaveBonus(last, 16), kWeaveBonus, 1e-6f);   // frost after fire
    CHECK_NEAR(WeaveBonus(last, 16), 1.f, 1e-6f);           // a repeat
    CHECK_NEAR(WeaveBonus(last, 4), kWeaveBonus, 1e-6f);
}

TEST(class_damage_over_time_stacks_and_ticks)
{
    std::vector<Dot> dots;
    AddDot(dots, 7, 421, 0, 12000, 1.5f, 3);
    AddDot(dots, 7, 421, 500, 12000, 1.5f, 3);
    AddDot(dots, 7, 421, 600, 12000, 1.5f, 3);
    AddDot(dots, 7, 421, 700, 12000, 1.5f, 3);      // capped at 3
    CHECK(dots.size() == 1 && StacksOn(dots, 7, 421) == 3);
    auto ticks = DueTicks(dots, 1000);
    CHECK(ticks.size() == 1 && ticks[0].first == 7);
    CHECK_NEAR(ticks[0].second, 4.5f, 1e-5f);       // 1.5 %EH a stack
    // Witchfire's burn on another: 4 s, 3 %EH, four ticks then gone.
    AddDot(dots, 9, 363, 1000, 4000, 3.f, 1);
    size_t burns = 0;
    for (uint32_t t = 2000; t <= 6000; t += 1000)
        for (auto const& [target, pct] : DueTicks(dots, t))
            if (target == 9)
            {
                ++burns;
                CHECK_NEAR(pct, 3.f, 1e-5f);
            }
    CHECK(burns == 4);
    CHECK(StacksOn(dots, 9, 363) == 0);
    CHECK(StacksOn(dots, 7, 421) == 3);             // the poison runs on (refreshed at 700)
    DueTicks(dots, 13000);
    CHECK(dots.empty());
}

TEST(class_barrier_wears_down_and_breaks)
{
    Blocker wall;
    wall.shape = BlockShape::Wall;
    wall.center = {0.f, 0.f, 0.f};
    wall.facing = 0.f;
    wall.radius = 3.f;
    wall.height = 3.f;
    wall.untilMs = 10000;
    wall.health = 100.f;
    Vec3 const shooter{10.f, 0.f, 1.5f}, victim{-5.f, 0.f, 1.2f};
    CHECK(Blocks(wall, shooter, victim));
    CHECK(!Wear(wall, 60.f) && !wall.broken);
    CHECK(Blocks(wall, shooter, victim));
    CHECK(Wear(wall, 60.f) && wall.broken);
    CHECK(!Blocks(wall, shooter, victim));          // gone
    CHECK(!Wear(wall, 60.f));                       // breaks once
    // A dome with no health never breaks.
    Blocker dome;
    dome.radius = 6.f;
    dome.untilMs = 10000;
    CHECK(!Wear(dome, 1e9f) && !dome.broken);
}

TEST(class_small_numbers_are_the_catalogs)
{
    CHECK_NEAR(kPrayerBonus, 1.2f, 1e-6f);
    CHECK_NEAR(kSacrificeShare, 0.5f, 1e-6f);
    CHECK(kMirrorSwaps == 3 && kOverwatchPrey == 3);
}

TEST(class_refits_replace_only_what_the_custom_part_does)
{
    CHECK(ReplaceMask(REFIT_REPLACE) == 0x7);
    CHECK(ReplaceMask(REFIT_REPLACE_FIRST) == 0x1);
    CHECK(ReplaceMask(REFIT_REPLACE_THIRD) == 0x4);
    CHECK(ReplaceMask(REFIT_REPLACE_FIRST | REFIT_REPLACE_SECOND) == 0x3);
    CHECK(ReplaceMask(REFIT_SELF | REFIT_IN_COMBAT) == 0);
    // Death Wish keeps its damage and fear immunity, loses its armor cut; Shadowform keeps its
    // form and shadow damage, loses vanilla's 15% of physical (30% of all is the custom part's).
    CHECK(ReplaceMask(RefitOf(12328)->flags) == 0x4);
    CHECK(ReplaceMask(RefitOf(15473)->flags) == 0x4);
    // Hammer of Justice reaches 20 yd; Lunar Strike is a melee blow.
    CHECK(RefitOf(10308)->rangeIndex == 3);
    CHECK(RefitOf(25298)->rangeIndex == 2 && (RefitOf(25298)->flags & REFIT_REPLACE));
    // Hellfire is no channel; Unbreakable's +30% gives way to +50%; Soulstone makes no item.
    CHECK((RefitOf(11684)->flags & REFIT_NOT_CHANNEL) && (RefitOf(11684)->flags & REFIT_REPLACE));
    CHECK(RefitOf(12975)->flags & REFIT_REPLACE);
    CHECK((RefitOf(20757)->flags & REFIT_REPLACE) && (RefitOf(20757)->flags & REFIT_KEEP_REAGENT));
    // The cat's and the bear's abilities keep their form and cost nothing; Maul strikes at once.
    for (uint32_t s : {9913u, 9827u, 9904u, 9881u, 8983u, 6795u})
    {
        Refit const* r = RefitOf(s);
        CHECK(r && (r->flags & REFIT_KEEP_STANCE));
    }
    for (uint32_t s : {9881u, 8983u, 6795u})
        CHECK(RefitOf(s)->flags & REFIT_FREE);
    CHECK(RefitOf(9881)->flags & REFIT_NOT_NEXT_SWING);
    CHECK(RefitOf(8983)->cooldownMs == 45000);
}

TEST(class_polish_pass_options_are_working)
{
    // The options this pass finished (CLASS_CATALOG.md section 15).
    std::set<uint16_t> const fixed = {103, 121, 123, 302, 733, 843, 133, 141, 151, 152, 161, 162, 163, 202, 222, 232, 242, 261,
        263, 312, 321, 331, 332, 342, 353, 361, 362, 363, 421, 422, 462, 513, 531, 532, 541, 542, 551, 552,
        561, 562, 703, 712, 713, 723, 732, 743, 751, 753, 762, 763, 813, 832, 841, 842, 851, 852, 862, 863, 903,
        921, 932, 943, 951, 961, 962, 963, 1111, 1121, 1123, 1162};
    // 452 (Slippery) was one; its slot is the Decoy since the rogue rework (2026-10-21).
    CHECK(fixed.size() == 70);
    for (uint16_t id : fixed)
    {
        Spec const* s = SpecOf(id);
        CHECK(s && s->status == Status::Working);
    }
    // What is left partial says what it lacks.
    size_t partial = 0;
    for (Spec const& s : Specs())
        if (s.status == Status::Partial)
        {
            ++partial;
            CHECK(!fixed.count(s.id));
            CHECK(std::string(s.note).size() > 0);
        }
    CHECK(partial == 10);
    // The capstones: every tier 6 option of the warrior, paladin, rogue, priest, shaman, mage,
    // warlock and hunter works now.
    for (uint8_t cls : {kWarrior, kPaladin, kHunter, kRogue, kPriest, kShaman, kMage, kWarlock})
        for (Option const* o : TierOptions(cls, 6))
            CHECK(o && SpecOf(o->id)->status == Status::Working);
    // Timed now, not a toggle: Shadowform 25 s, Combustion until its three spells (60 s at most).
    CHECK(SpecOf(561)->ends == Ends::Timed && SpecOf(561)->durationMs == 25000);
    CHECK(SpecOf(851)->ends == Ends::Timed && SpecOf(851)->durationMs == 60000);
}
