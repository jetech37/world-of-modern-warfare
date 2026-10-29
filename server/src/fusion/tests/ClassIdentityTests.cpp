/*
 * World of Modern Warfare: the class identity pass (fusion/ClassTuning.h) and the duel model
 * (fusion/Balance.h Duel). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Balance.h"
#include "fusion/ClassTuning.h"
#include "fusion/Classes.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

using namespace Fusion;
namespace CT = Fusion::ClassTuning;
using Fusion::Classes::OptionId;

namespace
{
    WeaponTable const& Seed()
    {
        static WeaponTable const t = [] {
            std::ifstream in(FUSION_SEED_WEAPONS);
            std::stringstream text;
            text << in.rdbuf();
            WeaponTable w;
            std::string err;
            w.LoadJson(text.str(), err);
            return w;
        }();
        return t;
    }

    Balance::Rotation const* RotationOf(char const* cls, char const* archetype)
    {
        for (Balance::Rotation const& r : Balance::Rotations())
            if (r.cls == cls && r.archetype == archetype)
                return &r;
        return nullptr;
    }

    Balance::DuelResult DuelOf(char const* cls, char const* archetype, bool tuned)
    {
        Balance::Assumptions a;
        a.tuned = tuned;
        return Balance::Duel(*RotationOf(cls, archetype), Balance::RepresentativeGuns(Seed(), 60).front(), 60, a);
    }
}

TEST(class_tuning_rows_name_real_options_once)
{
    std::set<uint16_t> seen;
    for (CT::Row const& r : CT::Rows())
    {
        CHECK(Classes::FindOption(r.option) != nullptr);
        CHECK(seen.insert(r.option).second);
        CHECK(r.damage > 0.25f && r.damage <= 3.f);
    }
    CHECK(CT::DamageScale(OptionId(Classes::kWarrior, 1, 0)) == 1.f);     // unlisted
    // Homing is a kit feature, never the default: only the rows that say so.
    CHECK(CT::GrantsHoming(OptionId(Classes::kMage, 3, 0)));               // Arcane Missiles
    CHECK(!CT::GrantsHoming(OptionId(Classes::kMage, 1, 0)));              // Fireball
}

TEST(class_tuning_lead_archetype_counts_picks_and_breaks_ties_by_tier)
{
    using namespace Classes;
    Picks p{};
    CHECK(CT::LeadArchetype(kPriest, p) == -1);
    p[0] = OptionId(kPriest, 1, 1);      // Cleric
    p[1] = OptionId(kPriest, 2, 0);      // Shadow
    CHECK(CT::LeadArchetype(kPriest, p) == 0);        // a tie: the higher tier's
    p[2] = OptionId(kPriest, 3, 1);
    CHECK(CT::LeadArchetype(kPriest, p) == 1);        // two Cleric picks
    // Another class's ids count for nothing.
    Picks q{};
    q[0] = OptionId(kMage, 1, 0);
    CHECK(CT::LeadArchetype(kPriest, q) == -1);
}

TEST(class_tuning_healers_get_atonement_and_a_stronger_basic_spell)
{
    using namespace Classes;
    CHECK(CT::IsHealerArchetype(kPriest, 1) && CT::IsHealerArchetype(kPriest, 2) && !CT::IsHealerArchetype(kPriest, 0));
    CHECK(CT::IsHealerArchetype(kDruid, 1) && CT::IsHealerArchetype(kShaman, 2) && CT::IsHealerArchetype(kPaladin, 2));
    CHECK(!CT::IsHealerArchetype(kWarrior, 0) && !CT::IsHealerArchetype(kMage, 2));
    CHECK(CT::BasicSpellScale(kPriest, 1) > 1.f);
    CHECK(CT::BasicSpellScale(kPriest, -1) == 1.f);
    CHECK(CT::BasicSpellScale(kMage, 0) == 1.f);      // the Wizard's Fireball is tuned as its option
    CHECK_NEAR(CT::AtonementHeal(10.f, true), 10.f * CT::kAtonementShare, 1e-5);
    CHECK(CT::AtonementHeal(10.f, false) == 0.f);
    CHECK(CT::AtonementHeal(-1.f, true) == 0.f);
}

TEST(class_tuning_shatter_gap_guard_and_drain_pressure)
{
    using namespace Classes;
    CHECK(CT::ShatterMultiplier(true, true) == CT::kShatterBonus);
    CHECK(CT::ShatterMultiplier(true, false) == 1.f && CT::ShatterMultiplier(false, true) == 1.f);
    CHECK(CT::IsGapCloser(OptionId(kWarrior, 0, 0)) && CT::IsGapCloser(OptionId(kWarrior, 2, 0)));
    CHECK(CT::IsGapCloser(OptionId(kRogue, 2, 2)) && CT::IsGapCloser(OptionId(kMage, 2, 1)));
    CHECK(!CT::IsGapCloser(OptionId(kWarrior, 0, 1)));     // Bloodrage
    CHECK(CT::DrainHealMultiplier(0) == 1.f);
    CHECK_NEAR(CT::DrainHealMultiplier(2), 1.f - 2.f * CT::kDrainHealCutPerAffliction, 1e-5);
    CHECK_NEAR(CT::DrainHealMultiplier(9), 1.f - CT::kDrainHealCutMax, 1e-5);
}

TEST(duel_model_fills_every_rotation_from_the_catalog)
{
    for (Balance::Rotation const& r : Balance::Rotations())
    {
        CHECK(r.clsId != 0);
        CHECK(r.archetypeIndex >= 0);
        for (Balance::Move const& m : r.moves)
            CHECK(m.option != 0 || m.basic || m.name.find('(') != std::string::npos || m.name == "Exorcism");
    }
    // The model's moves read the tuning rows by option.
    Balance::Rotation const* w = RotationOf("Warlock", "Afflicter");
    CHECK(w && w->moves.front().option == OptionId(Classes::kWarlock, 1, 0));
}

TEST(duel_model_is_deterministic_and_the_report_prints)
{
    Balance::DuelResult const a = DuelOf("Warrior", "Berserker", true);
    Balance::DuelResult const b = DuelOf("Warrior", "Berserker", true);
    CHECK(a.classWins == b.classWins && a.seconds == b.seconds && a.classLeft == b.classLeft);
    std::string const report = Balance::DuelReport(Seed(), 60);
    CHECK(report.find("Rogue Assassin") != std::string::npos);
    if (char const* path = std::getenv("FUSION_DUEL_REPORT"))
    {
        std::ofstream out(path, std::ios::binary);
        out << report;
        for (uint32_t level : {10u, 30u})
            out << Balance::DuelReport(Seed(), level);
    }
}


TEST(identity_pass_every_archetype_beats_an_equal_gunner_up_close_without_a_gun)
{
    // 25 yd, level 60, an AK-47 with Stopping Power: each kit's own play wins.
    for (Balance::Rotation const& r : Balance::Rotations())
    {
        Balance::DuelResult const d = DuelOf(r.cls.c_str(), r.archetype.c_str(), true);
        if (!d.classWins)
            std::printf("  %s %s loses the duel\n", r.cls.c_str(), r.archetype.c_str());
        CHECK(d.classWins);
    }
    // The ones that lost before the pass, and why they win now.
    for (auto const& [cls, arch] : std::vector<std::pair<char const*, char const*>>{{"Hunter", "Trapper"},
             {"Hunter", "Slayer"}, {"Priest", "Cleric"}, {"Shaman", "Earthbreaker"}, {"Shaman", "Totemist"},
             {"Warlock", "Afflicter"}, {"Druid", "Grovekeeper"}, {"Druid", "Starcaller"}})
    {
        CHECK(!DuelOf(cls, arch, false).classWins);
        CHECK(DuelOf(cls, arch, true).classWins);
    }
}

TEST(identity_pass_guns_stay_a_strong_option)
{
    // From 40 yd a rifle still beats the kits that must walk in without a long gap-closer.
    Balance::Assumptions a;
    a.tuned = true;
    a.duelGap = 40.f;
    Balance::GunCase const rifle = Balance::RepresentativeGuns(Seed(), 60).front();
    int gunWins = 0;
    for (Balance::Rotation const& r : Balance::Rotations())
        if (!Balance::Duel(r, rifle, 60, a).classWins)
            ++gunWins;
    CHECK(gunWins >= 3);
    // And a rifle kills a normal creature many times faster than any class (the ammunition is
    // what it pays for that).
    Balance::Assumptions pve;
    pve.tuned = true;
    float const gun = Balance::Gun(Balance::RepresentativeGuns(Seed(), 60).front(), 60, false, pve).ttk;
    for (Balance::Rotation const& r : Balance::Rotations())
        CHECK(Balance::Classic(r, 60, false, true, pve).ttk > gun);
}

TEST(identity_pass_casters_quest_without_a_gun)
{
    // Every archetype clears a normal creature of its level in at most ~5 s on its own (before the
    // pass healers needed 11 s, the Afflicter and Starcaller 10).
    Balance::Assumptions pve;
    pve.tuned = true;
    for (uint32_t level : {30u, 60u})
        for (Balance::Rotation const& r : Balance::Rotations())
        {
            float const ttk = Balance::Classic(r, level, false, true, pve).ttk;
            if (ttk > 5.5f)
                std::printf("  %s %s needs %.1f s at %u\n", r.cls.c_str(), r.archetype.c_str(), ttk, level);
            CHECK(ttk <= 5.5f);
        }
}

TEST(identity_pass_the_mechanics_show_in_the_model)
{
    Balance::Assumptions a;
    a.tuned = true;
    Balance::GunCase const rifle = Balance::RepresentativeGuns(Seed(), 60).front();
    // Atonement: a Cleric ends a duel with more health than the same kit without it.
    Balance::Rotation cleric = *RotationOf("Priest", "Cleric");
    Balance::DuelResult const with = Balance::Duel(cleric, rifle, 60, a);
    cleric.clsId = Classes::kMage;      // no healer lead: no Atonement (and Smite unscaled)
    Balance::DuelResult const without = Balance::Duel(cleric, rifle, 60, a);
    CHECK(with.classWins && (!without.classWins || with.classLeft > without.classLeft));
    // Charging through fire: at 40 yd the Duelist only wins with the guard on its Lunge.
    Balance::Rotation duelist = *RotationOf("Rogue", "Duelist");
    a.duelGap = 40.f;
    CHECK(Balance::Duel(duelist, rifle, 60, a).classWins);
    duelist.duel.closerGuard = false;
    CHECK(!Balance::Duel(duelist, rifle, 60, a).classWins);
}
