/*
 * Azeroth Warfare: tests for the build catalogue's whole structures (Build.h Structures()).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Construction.h"
#include "fusion/Plots.h"

#include <string>

using namespace Fusion;
using namespace Fusion::Build;

namespace
{
    PieceType ByName(char const* name)
    {
        for (Structure const& s : Structures())
            if (std::string(s.name) == name)
                return PieceType(s.id);
        return PieceType::None;
    }

    Piece At(PieceType t, float x, float y, float yaw = 0.f)
    {
        Piece p;
        p.id = 1;
        p.type = t;
        p.map = 1;
        p.pos = {x, y, 100.f};
        p.yaw = yaw;
        p.health = Def(t).health;
        return p;
    }
}

TEST(the_catalogue_has_real_buildings_each_a_known_ground_piece_with_a_recipe)
{
    // A house, a watchtower, a barn and a stable, a smithy and a forge, a market stall, a
    // palisade and its gate, bridges, a well and a guard post.
    for (char const* name : {"Cottage", "Watchtower", "Barn", "Stable", "Smithy", "Forge", "Market stall", "Palisade",
             "Palisade gate", "Footbridge", "Stone bridge", "Village well", "Guard post"})
        CHECK(ByName(name) != PieceType::None);
    uint8_t expect = kStructureBase;
    for (Structure const& s : Structures())
    {
        // Ids run on from kStructureBase, clear of the props.
        CHECK(s.id == expect++);
        PieceType const t = PieceType(s.id);
        CHECK(Known(s.id) && IsProp(t) && IsStructure(t));
        PieceDef const& d = Def(t);
        CHECK(std::string(d.name) == s.name);
        CHECK(d.groundOnly && d.snaps.empty() && !d.boxes.empty() && d.boxes.size() <= 64);
        CHECK(d.material == (s.stone ? Material::Stone : Material::Wood));
        // Sturdier than any prop: a building takes some breaking in the open world.
        CHECK(d.health >= 3000.f);
        CHECK(!Construction::Reinforceable(t));
        Construction::Recipe const* r = Construction::RecipeFor(t);
        CHECK(r && r->materials.size() >= 2 && r->skill.required >= 30);
        // Every box stands on or over the ground's level (a foundation may sink a little), and
        // nothing is wider than a large plot.
        for (Box const& b : d.boxes)
        {
            CHECK(b.center.z - b.half.z > -3.f);
            CHECK(b.half.x > 0.f && b.half.y > 0.f && b.half.z > 0.f);
            CHECK(std::abs(b.center.x) + b.half.x < 31.f && std::abs(b.center.y) + b.half.y < 31.f);
        }
    }
    CHECK(!Known(uint8_t(kStructureBase + Structures().size())));
    CHECK(!Known(uint8_t(kStructureBase - 1)));
    // A building is a big recipe: more than any wall.
    CHECK(Construction::RecipeFor(ByName("Cottage"))->skill.required > Construction::RecipeFor(PieceType::WoodWall)->skill.required);
    CHECK(Construction::RecipeFor(ByName("Watchtower"))->skill.required >= 250);
}

TEST(a_structure_blocks_at_its_walls_and_leaves_room_under_its_roof)
{
    // The palisade gate: two walls with a way through between them.
    Piece const gate = At(ByName("Palisade gate"), 0.f, 0.f);
    Vec3 const dirX{1.f, 0.f, 0.f};
    float t = 0.f;
    CHECK(RayHits(gate, {-20.f, 7.1f, 101.5f}, dirX, 40.f, t));      // a wall
    CHECK(!RayHits(gate, {-20.f, 0.f, 101.5f}, dirX, 40.f, t));      // the gateway
    // The market stall's awning stands over the counter: something overhead, out of reach of a
    // standing player's head at its middle.
    PieceDef const& stall = Def(ByName("Market stall"));
    bool overhead = false;
    for (Box const& b : stall.boxes)
        if (b.center.z - b.half.z >= 2.5f)
            overhead = true;
    CHECK(overhead);
    // A cottage is shut: a shot at its wall stops.
    Piece const cottage = At(ByName("Cottage"), 0.f, 0.f);
    CHECK(RayHits(cottage, {-30.f, 0.f, 102.f}, dirX, 60.f, t) && t < 30.f);
}

TEST(a_structure_goes_on_a_plot_it_fits)
{
    Plots::Grid const g({
        {Plots::Size::Small, 1, 1000.f, 2000.f, 24.f, 1, 1, 1, 1, 1, 0},
        {Plots::Size::Large, 1, 2000.f, 2000.f, 64.f, 1, 1, 101, 1, 1, 0},
    });
    // A well in a small plot's middle fits; a footbridge (26 yards) doesn't, but fits a large one.
    CHECK(Plots::Fits(g, 1, At(ByName("Village well"), 1012.f, 2012.f)));
    CHECK(!Plots::Fits(g, 1, At(ByName("Footbridge"), 1012.f, 2012.f)));
    CHECK(Plots::Fits(g, 101, At(ByName("Footbridge"), 2032.f, 2032.f)));
    // Turned a quarter, still judged by every corner of every box.
    CHECK(Plots::Fits(g, 101, At(ByName("Barn"), 2032.f, 2032.f, 1.5707963f)));
}
