/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * The look table (Companions.cpp's kLooks, 29_companion_looks.sql): every form of a line wears
 * its own display, grows, and a mount line's last form is the mount it is ridden as. The display
 * ids themselves are checked against the install by the client (companion_ui's tests).
 */

#include "Test.h"
#include "fusion/CompanionIndex.h"
#include "fusion/CompanionTraits.h"

#include <set>

using namespace Fusion;
using namespace Fusion::Companions;

namespace
{
    CompanionSpecies const& Look(uint16_t id)
    {
        CompanionSpecies const* s = FindSpecies(StarterSpecies(), id);
        static CompanionSpecies none;
        return s ? *s : none;
    }

    CompanionState At(uint16_t species, uint8_t stage, uint32_t traits = 0)
    {
        CompanionState s;
        s.species = species;
        s.stage = stage;
        s.traits = traits;
        return s;
    }
}

TEST(companion_looks_every_stage_wears_its_own_display)
{
    size_t stages = 0;
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        std::set<uint32_t> seen;
        for (uint8_t st = 0; st < kMaxStages; ++st)
        {
            StageInfo const& stage = sp.stages[st];
            if (st >= sp.stageCount)
            {
                CHECK(stage.display == 0);
                continue;
            }
            ++stages;
            // A display named for every form, never the same one twice in a line.
            CHECK(stage.display != 0);
            CHECK(seen.insert(stage.display).second);
            CHECK(LookDisplay(StarterSpecies(), sp, At(sp.id, st)) == stage.display);
            // A line on the wolf's spells rides as its last form looks.
            if (stage.rideable && stage.rideDisplay)
                CHECK(stage.rideDisplay == stage.display);
        }
        // Past its stages: no look at all.
        CHECK(LookDisplay(StarterSpecies(), sp, At(sp.id, sp.stageCount)) == 0);
    }
    CHECK(stages > 250);
}

TEST(companion_looks_mount_lines_end_on_a_mount_model)
{
    // The owner's rule: a mountable line's last form is a riding model where 1.12 has one, so the
    // summoned companion and the mount are the same creature.
    struct Final
    {
        uint16_t species;
        uint32_t display;   // the riding display
    };
    static Final const kFinals[] = {
        {6, 247},       // riding wolf (brown)
        {7, 6448},      // riding nightsaber
        {8, 6080},      // riding frostsaber
        {12, 6473},     // riding raptor (violet)
        {29, 11641},    // riding kodo
        {30, 2404},     // riding horse
        {31, 6569},     // mechanostrider
        {32, 10670},    // skeletal horse
        {33, 2736},     // riding ram
        {106, 6443},    // riding tiger (red)
        {107, 6442},    // riding sabercat (brown)
        {108, 9714},    // riding sabercat (gold)
        {109, 2326},    // riding worg (reddish brown)
        {110, 14336},   // war wolf (white)
        {111, 2328},    // riding wolf (dark brown)
        {112, 6472},    // riding raptor (turquoise)
        {113, 4806},    // riding raptor (emerald)
        {114, 6469},    // riding raptor (tomato)
        {130, 12241},   // riding kodo (white)
    };
    for (Final const& f : kFinals)
    {
        CompanionSpecies const& sp = Look(f.species);
        CHECK(sp.stageCount >= 2);
        if (sp.stageCount < 2)
            continue;
        StageInfo const& last = sp.stages[sp.stageCount - 1];
        CHECK(last.rideable);
        CHECK(last.display == f.display);
        // Walking it is the size it is ridden at: the mount shows its display at its own size.
        CHECK_NEAR(last.scale, 1.f, 1e-4);
        // Ridden, the companion shows its own last form, whichever spell carries it.
        CompanionState s = At(sp.id, uint8_t(sp.stageCount - 1));
        CHECK(Index::MountDisplay(sp, s, 0, 99) == f.display);
    }
}

TEST(companion_looks_scale_rides_on_the_displays_own_size)
{
    CompanionSpecies const& wolf = Look(6);
    // The stage's scale on top of the display's (a riding wolf's 1.2 is the size it is ridden at).
    CHECK_NEAR(LookScale(wolf, At(6, 2), 1.2f), 1.2f, 1e-4);
    CHECK_NEAR(LookScale(wolf, At(6, 0), 0.4f), wolf.stages[0].scale * 0.4f, 1e-4);
    // An unknown display size counts as 1.
    CHECK_NEAR(LookScale(wolf, At(6, 1), 0.f), wolf.stages[1].scale, 1e-4);
    CHECK_NEAR(LookScale(wolf, At(6, 1), -2.f), wolf.stages[1].scale, 1e-4);
    // The size traits on top.
    CHECK_NEAR(LookScale(wolf, At(6, 1, TraitBit(Trait::Giant)), 1.f), wolf.stages[1].scale * 1.2f, 1e-4);
    CHECK_NEAR(LookScale(wolf, At(6, 1, TraitBit(Trait::Runt)), 1.f), wolf.stages[1].scale * 0.8f, 1e-4);
    // A stage it does not have: the display's own size.
    CHECK_NEAR(LookScale(wolf, At(6, 5), 1.3f), 1.3f, 1e-4);
}

TEST(companion_looks_follow_the_table_stage_by_stage)
{
    // A few lines spelled out: whelp -> drake -> dragon; cub -> sabercat -> riding sabercat; a
    // raptor hatchling, a raptor, a riding raptor.
    CompanionSpecies const& black = Look(1);
    CHECK(black.stages[0].display == 6288 && black.stages[1].display == 9586 && black.stages[2].display == 8574);
    CompanionSpecies const& night = Look(7);
    CHECK(night.stages[0].display == 11454 && night.stages[1].display == 9954 && night.stages[2].display == 6448);
    CompanionSpecies const& sun = Look(112);
    CHECK(sun.stages[0].display == 40002 && sun.stages[1].display == 1747 && sun.stages[2].display == 6472);
    // Its stage picks its look: evolving moves it on.
    CHECK(LookDisplay(StarterSpecies(), night, At(7, 0)) == 11454);
    CHECK(LookDisplay(StarterSpecies(), night, At(7, 1)) == 9954);
    CHECK(LookDisplay(StarterSpecies(), night, At(7, 2)) == 6448);
    // Consecutive forms never wear the same display (their models and skins, and their sizes,
    // are checked against the install by the client's tests).
    for (CompanionSpecies const& sp : StarterSpecies())
        for (uint8_t st = 1; st < sp.stageCount; ++st)
            CHECK(sp.stages[st].display != sp.stages[st - 1].display);
}

TEST(companion_looks_hybrids_wear_the_other_lines_display)
{
    auto const& all = StarterSpecies();
    CompanionSpecies const& nightsaber = Look(7);
    CompanionState s = At(7, 1);
    s.hybrid = 8;   // a Frostsaber's coloring: they share every stage
    CHECK(LookDisplay(all, nightsaber, s) == Look(8).stages[1].display);
    CHECK(LookCreature(all, nightsaber, s) == Look(8).stages[1].creature);
    // Ridden, it is the coloring it walked with (the server passes LookDisplay's).
    s.stage = 2;
    uint32_t const walked = LookDisplay(all, nightsaber, s);
    CHECK(walked == Look(8).stages[2].display);
    CHECK(Index::MountDisplay(nightsaber, s, walked, 99) == walked);
    // Wolf and coyote share the middle stage only.
    CompanionSpecies const& wolf = Look(6);
    CompanionState w = At(6, 0);
    w.hybrid = 111;
    CHECK(LookDisplay(all, wolf, w) == wolf.stages[0].display);
    w.stage = 1;
    CHECK(LookDisplay(all, wolf, w) == Look(111).stages[1].display);
}

TEST(companion_looks_go_to_the_window)
{
    // The window's models (op 34): each stage's own display, and the ride display its last.
    CompanionSpecies const& sp = Look(109);
    Index::Looks const l = Index::MakeLooks(sp, [](uint32_t) { return 12345u; }, [](uint32_t) { return std::string("x"); });
    CHECK(l.stages.size() == 3);
    for (size_t st = 0; st < l.stages.size() && st < 3; ++st)
        CHECK(l.stages[st].display == sp.stages[st].display);
    CHECK(l.rideDisplay == sp.stages[2].display);
    // A stage with no display of its own falls back to its creature's.
    CompanionSpecies bare = sp;
    bare.stages[0].display = 0;
    CHECK(Index::MakeLooks(bare, [](uint32_t) { return 12345u; }, [](uint32_t) { return std::string(); })
              .stages[0].display == 12345u);
}
