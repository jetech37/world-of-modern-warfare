/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * The flying mounts as companion lines (35_flying_companions.sql, species 215-223): three forms, a
 * pet-sized first one, the last a Wrath riding model of the patch layer (displays 40019+) ridden on
 * the ground as the display it walks with, and Launch as the mounted move.
 */

#include "Test.h"
#include "fusion/CompanionKeeper.h"
#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"
#include "fusion/Companions.h"
#include "fusion/Riding.h"

#include <cstring>
#include <set>

using namespace Fusion;
using namespace Fusion::Companions;

namespace
{
    // The patch layer's display ids (client/src/wotlk/import.rs DISPLAY_BASE, its reserved range).
    constexpr uint32_t kPatchFirst = 40001;
    constexpr uint32_t kPatchLast = 41000;
    constexpr uint32_t kFlyingFirst = 40019;     // the ones this pass added
    constexpr uint32_t kFlyingLast = 40037;
}

TEST(flying_lines_are_three_forms_ending_on_a_ridden_patch_display)
{
    std::set<uint32_t> rideDisplays;
    std::set<Family> families;
    int lines = 0;
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        if (sp.id < 215 || sp.id > 223)
            continue;
        ++lines;
        families.insert(sp.family);
        CHECK(sp.stageCount == 3);
        CHECK(sp.item == SpeciesItem(sp.id));
        CHECK(!IsHumanoidFamily(sp.family));
        // Only the last form is ridden, on the wolf's spells, as the display it walks with.
        CHECK(!sp.stages[0].rideable && !sp.stages[1].rideable);
        StageInfo const& last = sp.stages[2];
        CHECK(last.rideable && last.mountSpell == 580 && last.swiftMountSpell == 23250);
        CHECK(last.rideDisplay == last.display);
        CHECK(last.display >= kFlyingFirst && last.display <= kFlyingLast);
        CHECK(last.scale == 1.f);       // ridden at the model's own size
        CHECK(rideDisplays.insert(last.display).second);
        // Every form has a display: 1.12's, or the patch layer's reserved range.
        for (uint8_t i = 0; i < 3; ++i)
        {
            CHECK(sp.stages[i].display != 0);
            CHECK(sp.stages[i].display < 30000 || (sp.stages[i].display >= kPatchFirst && sp.stages[i].display < kPatchLast));
            CHECK(sp.stages[i].scale >= 0.15f && sp.stages[i].scale <= 2.5f);
        }
        // Stage 1 -> 2 at 20 anywhere, 2 -> 3 at 40 (where riding starts) with a catalyst.
        CHECK(sp.evolutions[0].level == 20 && sp.evolutions[0].areas.empty() && !sp.evolutions[0].item);
        CHECK(sp.evolutions[1].level == 40 && IsCatalystItem(sp.evolutions[1].item) && !sp.evolutions[1].areas.empty());
        CHECK(!sp.drops.entries.empty() && sp.drops.type == CreatureType::None);
    }
    CHECK(lines == 9);
    CHECK(families.count(Family::Gryphon) && families.count(Family::WindRider) && families.count(Family::Hippogryph)
        && families.count(Family::Dragonhawk) && families.count(Family::Dragon) && families.count(Family::FireElemental));
    CHECK(std::strcmp(Keeper::FamilyName(Family::WindRider), "Wind Rider") == 0);
    CHECK(BreedReagent(*FindSpecies(StarterSpecies(), 215)) != 0);
    CHECK(BreedGroup(Family::Gryphon) != BreedGroup(Family::Hippogryph));
}

TEST(flying_lines_take_off_with_skyward_ascent_when_ridden)
{
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        if (sp.id < 215 || sp.id > 223)
            continue;
        CompanionState s;
        s.species = sp.id;
        s.stage = 2;
        Ability const a = MountedMove(sp, s);
        CHECK(IsSkySurge(a));
        CHECK(a.kind == AbilityKind::Mounted && a.cooldown == kSkySurgeCooldown);
        s.stage = 1;
        CHECK(!MountedMove(sp, s).Exists());
    }
    // The ground lines keep their own mounted moves.
    CompanionSpecies const* wolf = FindSpecies(StarterSpecies(), 6);
    CompanionState w;
    w.species = 6;
    w.stage = 2;
    CHECK(wolf && !IsSkySurge(MountedMove(*wolf, w)));
    // The Ascent's climb clears the height a skyride starts at from a fall, and its cooldown is
    // only the press gap: the charges hold it back.
    CHECK(Skyriding::kAscentRate * Skyriding::kAscentSeconds > Riding::kGlideMinHeight);
    CHECK(kSkySurgeCooldown <= Skyriding::kMoveGapSeconds);
}
