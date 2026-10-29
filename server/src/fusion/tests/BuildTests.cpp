/*
 * Azeroth Warfare: tests for building (Build.h). GPL-2.0-or-later, see Common.h.
 */

#define _USE_MATH_DEFINES // M_PI on MSVC, before anything includes <cmath>
#include "Test.h"
#include "fusion/Build.h"

#include <cmath>

using namespace Fusion;
using namespace Fusion::Build;

namespace
{
    Piece At(uint32_t id, PieceType t, Vec3 p, float yaw = 0.f)
    {
        Piece x;
        x.id = id;
        x.type = t;
        x.pos = p;
        x.yaw = yaw;
        x.health = Def(t).health;
        return x;
    }
}

TEST(every_piece_has_boxes_and_a_material)
{
    for (uint8_t t = 1; t < uint8_t(PieceType::Count); ++t)
    {
        PieceDef const& d = Def(PieceType(t));
        CHECK(!d.boxes.empty());
        CHECK(d.health > 0.f);
        CHECK(!d.name.empty());
        CHECK(d.cost.count > 0);
    }
    CHECK(Known(1) && !Known(0) && !Known(uint8_t(PieceType::Count)));
}

TEST(a_ray_hits_a_wall_and_misses_beside_it)
{
    // A wall at the origin facing +x: 3 yards wide along y, 3 tall, thin along x.
    Piece wall = At(1, PieceType::WoodWall, {0, 0, 0});
    float t = 0.f;
    CHECK(RayHits(wall, {-10, 0, 1.5f}, {1, 0, 0}, 50.f, t));
    CHECK_NEAR(t, 10.f - Def(PieceType::WoodWall).boxes[0].half.x, 1e-3);
    CHECK(!RayHits(wall, {-10, 5, 1.5f}, {1, 0, 0}, 50.f, t));
    CHECK(!RayHits(wall, {-10, 0, 1.5f}, {1, 0, 0}, 5.f, t));        // too short
    // Turned 90 degrees, it now runs along x: a ray along x passes beside it.
    Piece turned = At(2, PieceType::WoodWall, {0, 0, 0}, float(M_PI) / 2.f);
    CHECK(!RayHits(turned, {-10, 3, 1.5f}, {1, 0, 0}, 50.f, t));
    CHECK(RayHits(turned, {0, -10, 1.5f}, {0, 1, 0}, 50.f, t));
}

TEST(a_doorway_lets_a_ray_through)
{
    Piece door = At(1, PieceType::WoodDoorway, {0, 0, 0});
    float t = 0.f;
    CHECK(!RayHits(door, {-10, 0, 1.f}, {1, 0, 0}, 50.f, t));       // through the opening
    CHECK(RayHits(door, {-10, 1.3f, 1.f}, {1, 0, 0}, 50.f, t));      // the jamb
    CHECK(RayHits(door, {-10, 0, 2.8f}, {1, 0, 0}, 50.f, t));        // the lintel
}

TEST(pieces_on_the_ground_hold_and_support_falls_off_up_and_out)
{
    // A ground floor, a wall on it, a wall on that, and a floor cantilevered out from the top.
    auto ground = [](float, float, float) { return 0.f; };
    std::vector<Piece> ps = {
        At(1, PieceType::WoodFloor, {0, 0, 0}),
        At(2, PieceType::WoodWall, {1.5f, 0, 0.3f}),
        At(3, PieceType::WoodWall, {1.5f, 0, 3.3f}),
        At(4, PieceType::WoodFloor, {3.f, 0, 6.f}),
        At(5, PieceType::WoodFloor, {6.f, 0, 6.f}),
    };
    Supports s = Solve(ps, ground);
    CHECK_NEAR(s.at(1), 1.f, 1e-6);
    CHECK(s.at(2) < 1.f && s.at(2) > 0.8f);
    CHECK(s.at(3) < s.at(2));
    CHECK(s.at(4) < s.at(3));
    CHECK(s.at(5) < s.at(4));
    // A floor in the air touching nothing has none.
    ps.push_back(At(6, PieceType::WoodFloor, {30.f, 0, 10.f}));
    s = Solve(ps, ground);
    CHECK_NEAR(s.at(6), 0.f, 1e-6);
    CHECK(!Stands(PieceType::WoodFloor, s.at(6)));
    CHECK(Stands(PieceType::WoodFloor, s.at(1)));
}

TEST(stone_holds_tall_but_not_far)
{
    auto ground = [](float, float, float) { return 0.f; };
    std::vector<Piece> tower;
    for (uint32_t i = 0; i < 8; ++i)
        tower.push_back(At(i + 1, PieceType::StoneWall, {0, 0, 3.f * float(i)}));
    Supports s = Solve(tower, ground);
    CHECK(Stands(PieceType::StoneWall, s.at(8)));               // eight storeys of stone
    std::vector<Piece> wood;
    for (uint32_t i = 0; i < 12; ++i)
        wood.push_back(At(i + 1, PieceType::WoodWall, {0, 0, 3.f * float(i)}));
    s = Solve(wood, ground);
    CHECK(!Stands(PieceType::WoodWall, s.at(12)));              // twelve of wood is too many
    // Stone reaches out poorly: a stone floor hung off a stone wall's side, then another.
    std::vector<Piece> ledge = {
        At(1, PieceType::StoneWall, {0, 0, 0}),
        At(2, PieceType::StoneFloor, {1.5f, 0, 2.7f}),
        At(3, PieceType::StoneFloor, {4.5f, 0, 2.7f}),
    };
    s = Solve(ledge, ground);
    CHECK(Stands(PieceType::StoneFloor, s.at(2)));
    CHECK(!Stands(PieceType::StoneFloor, s.at(3)));
}

TEST(collapse_takes_what_no_longer_stands)
{
    auto ground = [](float, float, float) { return 0.f; };
    std::vector<Piece> ps = {
        At(1, PieceType::WoodWall, {0, 0, 0}),
        At(2, PieceType::WoodWall, {0, 0, 3.f}),
        At(3, PieceType::WoodWall, {0, 0, 6.f}),
        At(9, PieceType::WoodWall, {20, 0, 0}),
    };
    // Shoot out the bottom wall: the two above fall, the one apart stays.
    std::vector<uint32_t> fallen = Collapse(ps, 1, ground);
    CHECK(fallen.size() == 2);
    CHECK(ps.size() == 1 && ps[0].id == 9);
}

TEST(collapse_near_leaves_what_stands_outside_its_window_alone)
{
    auto ground = [](float, float, float) { return 0.f; };
    // Far off (60 yards), a wall whose own footing lay outside the gathered window: judged from
    // the window alone it seems to float. Taking a piece down here must not take it.
    Piece far = At(2, PieceType::WoodWall, {60.f, 0, 3.f});
    far.support = 0.9f;
    std::vector<Piece> ps = {
        At(9, PieceType::WoodWall, {0, 0, 0}),
        At(10, PieceType::WoodWall, {0, 0, 3.f}),
        far,
    };
    std::vector<uint32_t> fallen = CollapseNear(ps, 9, ground, {0, 0, 0}, 30.f);
    CHECK(fallen.size() == 1 && fallen[0] == 10);
    CHECK(ps.size() == 1 && ps[0].id == 2);
    CHECK_NEAR(ps[0].support, 0.9f, 1e-5f);     // its support untouched
    // Within the fall radius the old rules hold.
    std::vector<Piece> near = {
        At(1, PieceType::WoodWall, {0, 0, 0}),
        At(2, PieceType::WoodWall, {0, 0, 3.f}),
        At(3, PieceType::WoodWall, {20, 0, 0}),
    };
    CHECK(CollapseNear(near, 1, ground, {0, 0, 0}).size() == 1 && near.size() == 1 && near[0].id == 3);
}

TEST(a_new_piece_must_not_sit_inside_another)
{
    std::vector<Piece> ps = {At(1, PieceType::WoodWall, {0, 0, 0})};
    CHECK(Overlaps(ps, At(0, PieceType::WoodWall, {0.05f, 0, 0})));
    CHECK(!Overlaps(ps, At(0, PieceType::WoodWall, {0, 0, 3.f})));          // stacked on top
    CHECK(!Overlaps(ps, At(0, PieceType::WoodWall, {0, 3.f, 0})));          // beside it
    CHECK(!Overlaps(ps, At(0, PieceType::WoodFloor, {1.5f, 0, 0})));        // a floor at its foot
}

TEST(snapping_lines_pieces_up)
{
    // Aiming just past a floor's +x edge with another floor: it lands edge to edge.
    std::vector<Piece> ps = {At(1, PieceType::WoodFloor, {0, 0, 0})};
    Placement p;
    CHECK(Snap(ps, PieceType::WoodFloor, {3.4f, 0.2f, 0.3f}, 0.f, 3.f, p));
    CHECK_NEAR(p.pos.x, 3.f, 1e-3);
    CHECK_NEAR(p.pos.y, 0.f, 1e-3);
    CHECK_NEAR(p.pos.z, 0.f, 1e-3);
    // A wall aimed at the floor's +y edge stands on it, turned to run along x.
    CHECK(Snap(ps, PieceType::WoodWall, {0.2f, 1.6f, 0.5f}, 0.f, 3.f, p));
    CHECK_NEAR(p.pos.z, Def(PieceType::WoodFloor).top, 1e-3);
    CHECK(std::fabs(p.pos.y - 1.5f) < 1e-3);
    // Nothing near: no snap.
    CHECK(!Snap(ps, PieceType::WoodFloor, {40.f, 0, 0}, 0.f, 3.f, p));
}

TEST(the_wire_round_trips)
{
    std::vector<Piece> ps = {At(7, PieceType::Sandbags, {1.5f, -2.f, 30.25f}, 1.f)};
    ps[0].health = 123.f;
    ps[0].owner = 42;
    std::vector<uint8_t> bytes = EncodePieces(ps, 42);
    // op, count, then id, type, xyz, yaw, health per mille, support, flags.
    CHECK(bytes.size() == 1 + 2 + (4 + 1 + 16 + 2 + 1 + 1));
    CHECK(bytes[0] == uint8_t(ServerOp::Pieces));
    PlaceRequest r;
    std::vector<uint8_t> place = {uint8_t(ClientOp::Place), uint8_t(PieceType::WoodWall)};
    auto put = [&](float f) { uint8_t b[4]; std::memcpy(b, &f, 4); place.insert(place.end(), b, b + 4); };
    put(1.f); put(2.f); put(3.f); put(0.5f);
    CHECK(DecodePlace(place, r));
    CHECK(r.type == PieceType::WoodWall && r.pos.y == 2.f && r.yaw == 0.5f);
    place[1] = 255;     // past the last furniture piece
    CHECK(!DecodePlace(place, r));
    place.pop_back();
    place[1] = uint8_t(PieceType::WoodWall);
    CHECK(!DecodePlace(place, r));
}

TEST(props_are_pieces_too)
{
    // WoW's props (from 100): known, boxed by their models' bounds, on the ground.
    CHECK(Known(kPropBase) && Known(uint8_t(kPropBase + Props().size() - 1)));
    CHECK(!Known(uint8_t(kPropBase + Props().size())) && !Known(99));
    PieceDef const& tent = Def(PieceType(kPropBase + 18));      // the large tent
    CHECK(tent.groundOnly && tent.boxes.size() == 1 && tent.boxes[0].half.x > 5.f);
    CHECK(Def(PieceType(kPropBase + 14)).material == Material::Stone);  // the stone fence
}

TEST(pieces_that_only_touch_do_not_overlap)
{
    // A wall standing on a floor, along its edge: they meet along a face, nothing inside.
    std::vector<Piece> ps = {At(1, PieceType::WoodFloor, {0, 0, 0})};
    CHECK(!Overlaps(ps, At(0, PieceType::WoodWall, {1.5f, 0, 0.3f})));
    CHECK(!Overlaps(ps, At(0, PieceType::WoodWall, {0, 1.5f, 0.3f}, 1.5707963f)));
    // Two floors side by side share an edge.
    CHECK(!Overlaps(ps, At(0, PieceType::WoodFloor, {3.f, 0, 0})));
    CHECK(!Overlaps(ps, At(0, PieceType::WoodFloor, {0, 3.f, 0})));
    CHECK(!Overlaps(ps, At(0, PieceType::StoneFloor, {3.f, 3.f, 0})));
    // A roof on a wall's top, two walls meeting at a corner.
    std::vector<Piece> walls = {At(1, PieceType::WoodWall, {0, 0, 0})};
    CHECK(!Overlaps(walls, At(0, PieceType::WoodRoof, {0, 0, 3.f})));
    CHECK(!Overlaps(walls, At(0, PieceType::WoodWall, {1.5f, 1.5f, 0}, 1.5707963f)));
    // But a floor sunk into another still does.
    CHECK(Overlaps(ps, At(0, PieceType::WoodFloor, {1.f, 0, 0.05f})));
}

TEST(the_clients_blueprints_fit_together)
{
    // The client's blueprints (client/src/build/pieces.rs), turned and moved as a builder would:
    // no piece of one sits inside another, so each goes down in turn.
    struct Part { PieceType t; Vec3 at; float yaw; };
    float const q = 1.5707963f;
    std::vector<std::vector<Part>> const blueprints = {
        {{PieceType::WoodFloor, {0, 0, 0}, 0}, {PieceType::WoodWall, {1.5f, 0, 0.3f}, 0},
         {PieceType::WoodWall, {-1.5f, 0, 0.3f}, 0}, {PieceType::WoodWall, {0, 1.5f, 0.3f}, q},
         {PieceType::WoodDoorway, {0, -1.5f, 0.3f}, q}, {PieceType::WoodFloor, {0, 0, 3.f}, 0},
         {PieceType::WoodHalfWall, {1.5f, 0, 3.3f}, 0}, {PieceType::WoodHalfWall, {0, 1.5f, 3.3f}, q},
         {PieceType::WoodHalfWall, {0, -1.5f, 3.3f}, q}, {PieceType::WoodStairs, {-4.5f, 0, 0}, 0},
         {PieceType::Torch, {1.2f, 1.2f, 3.3f}, 0}},
        {{PieceType::WoodFloor, {0, -1.5f, 0}, 0}, {PieceType::WoodFloor, {0, 1.5f, 0}, 0},
         {PieceType::WoodWall, {1.5f, -1.5f, 0.3f}, 0}, {PieceType::WoodWall, {1.5f, 1.5f, 0.3f}, 0},
         {PieceType::WoodWindow, {-1.5f, -1.5f, 0.3f}, 0}, {PieceType::WoodDoorway, {-1.5f, 1.5f, 0.3f}, 0},
         {PieceType::WoodWall, {0, -3.f, 0.3f}, q}, {PieceType::WoodWindow, {0, 3.f, 0.3f}, q},
         {PieceType::WoodFloor, {0, -1.5f, 3.f}, 0}, {PieceType::WoodFloor, {0, 1.5f, 3.f}, 0},
         {PieceType::Torch, {1.1f, 2.6f, 0.3f}, 0}},
        {{PieceType::Sandbags, {3.f, 0, 0}, 0}, {PieceType::Sandbags, {-3.f, 0, 0}, 0},
         {PieceType::Sandbags, {0, 3.f, 0}, q}, {PieceType::Sandbags, {0, -3.f, 0}, q}},
    };
    float const yaw = 0.7f, c = std::cos(yaw), s = std::sin(yaw);
    Vec3 const origin{100.f, 50.f, 10.f};
    auto ground = [](float, float, float) { return 10.f; };
    for (auto const& bp : blueprints)
    {
        std::vector<Piece> built;
        for (Part const& part : bp)
        {
            Piece p = At(uint32_t(built.size() + 1), part.t,
                origin + Vec3{part.at.x * c - part.at.y * s, part.at.x * s + part.at.y * c, part.at.z}, yaw + part.yaw);
            CHECK(!Overlaps(built, p));
            built.push_back(p);
            Supports const sup = Solve(built, ground);
            CHECK(Stands(p.type, sup.at(p.id)));
        }
    }
}

TEST(reach_is_to_the_nearest_point_of_a_piece)
{
    // A wall 3 yards wide, standing at x = 0: from 10 yards out its face is 9.85 away, from
    // beside its end the end.
    Piece const wall = At(1, PieceType::WoodWall, {0, 0, 0});
    CHECK_NEAR(Distance(wall, {10.f, 0, 1.5f}), 9.85f, 0.01f);
    CHECK_NEAR(Distance(wall, {0, 5.f, 1.5f}), 3.5f, 0.01f);
    CHECK_NEAR(Distance(wall, {0.1f, 0.5f, 1.f}), 0.f, 0.01f);      // inside it
    // A long blueprint piece: its centre may be past reach while its near end is not.
    Piece const roof = At(2, PieceType::WoodRoof, {0, 0, 3.f});
    CHECK(Distance(roof, {-1.f, 0, 3.f}) < (Center(roof) - Vec3{-1.f, 0, 3.f}).Length());
}

TEST(top_under_a_point_is_the_box_surface_below_it)
{
    // Sandbags: 0.9 yd high, 0.8 deep along their x, 3 long along their y.
    Piece const bags = At(1, PieceType::Sandbags, {0, 0, 0});
    CHECK_NEAR(TopUnder(bags, 0.f, 0.f, 5.f).value_or(-1.f), 0.9f, 1e-4);
    CHECK_NEAR(TopUnder(bags, 0.3f, 1.4f, 5.f).value_or(-1.f), 0.9f, 1e-4);
    CHECK(!TopUnder(bags, 0.6f, 0.f, 5.f).has_value());     // beside them
    CHECK(!TopUnder(bags, 0.f, 0.f, 0.5f).has_value());     // from inside: no surface under
    // Turned a quarter: now long along world x.
    Piece const turned = At(2, PieceType::Sandbags, {0, 0, 0}, float(M_PI) / 2.f);
    CHECK(TopUnder(turned, 1.2f, 0.f, 5.f).has_value());
    CHECK(!TopUnder(turned, 0.f, 1.2f, 5.f).has_value());
    // Stairs (a pitched box): the top climbs along them.
    Piece const stairs = At(3, PieceType::WoodStairs, {0, 0, 0});
    float const low = TopUnder(stairs, 0.5f, 0.f, 5.f).value_or(-1.f);
    float const mid = TopUnder(stairs, 1.5f, 0.f, 5.f).value_or(-1.f);
    CHECK_NEAR(mid, 1.5f + 0.08f * std::sqrt(2.f), 0.01f);
    CHECK_NEAR(mid - low, 1.f, 0.01f);
}

TEST(top_under_among_pieces_is_the_highest_below_the_search)
{
    std::vector<Piece> pieces{At(1, PieceType::WoodFloor, {0, 0, 0}),
        At(2, PieceType::Sandbags, {0, 0, 0.3f}),
        At(3, PieceType::WoodWall, {5.f, 0, 0})};
    CHECK_NEAR(TopUnder(pieces, 0.f, 0.f, 3.f).value_or(-1.f), 1.2f, 1e-4);   // the bags on the floor
    CHECK_NEAR(TopUnder(pieces, 1.f, 1.f, 3.f).value_or(-1.f), 0.3f, 1e-4);   // the floor beside them
    CHECK_NEAR(TopUnder(pieces, 0.f, 0.f, 1.f).value_or(-1.f), 0.3f, 1e-4);   // from inside the bags
    CHECK(!TopUnder(pieces, 5.f, 0.f, 2.f).has_value());    // a wall taller than the search
    CHECK(!TopUnder(pieces, 9.f, 9.f, 3.f).has_value());
}

TEST(ground_pieces_may_stand_on_a_built_floor)
{
    PieceType const barrel = PieceType(kPropBase + 2);
    CHECK(Def(barrel).groundOnly);
    Ground const deep = [](float, float, float) { return -100.f; };     // the terrain far below
    std::vector<Piece> const built = {At(1, PieceType::WoodFloor, {0, 0, 5.f}), At(2, PieceType::StoneFloor, {10.f, 0, 5.f}),
        At(3, PieceType::WoodWall, {20.f, 0, 5.f})};
    // On a wood floor's top (0.3 up) and a stone floor's (0.4 up), within a little of it.
    CHECK(Footed(At(9, barrel, {0, 0, 5.3f}), built, deep));
    CHECK(Footed(At(9, barrel, {0.5f, -0.5f, 5.4f}), built, deep));
    CHECK(Footed(At(9, barrel, {10.f, 0, 5.4f}), built, deep));
    // Floating over the floor, or beside it.
    CHECK(!Footed(At(9, barrel, {0, 0, 6.f}), built, deep));
    CHECK(!Footed(At(9, barrel, {0, 3.f, 5.3f}), built, deep));
    // A wall's top is no floor.
    CHECK(!Footed(At(9, barrel, {20.f, 0, 8.f}), built, deep));
    // Sandbags too, and the terrain still counts.
    CHECK(Footed(At(9, PieceType::Sandbags, {0, 0, 5.3f}), built, deep));
    Ground const flat = [](float, float, float) { return 0.f; };
    CHECK(Footed(At(9, barrel, {40.f, 0, 0.f}), built, flat));
    CHECK(!Footed(At(9, barrel, {40.f, 0, 0.f}), built, deep));
}

TEST(nothing_is_built_on_top_of_a_service_npc)
{
    Piece wall = At(1, PieceType::WoodWall, {0, 0, 0});
    // A flight master (npc flags set) 3 yards off: too near. A plain creature there: fine.
    CHECK(CrowdsNpc(wall, {0, 3.f, 0}, 0x8));
    CHECK(!CrowdsNpc(wall, {0, 3.f, 0}, 0));
    // Far enough off: fine.
    CHECK(!CrowdsNpc(wall, {0, kNpcClearance + 3.f, 0}, 0x8));
}
