/*
 * Azeroth Warfare: tests for the rule that nobody is shut inside a piece (Build.h Traps,
 * FreeSpot, Refusal::Traps). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Build.h"

#include <string>

using namespace Fusion;
using namespace Fusion::Build;

namespace
{
    // The ground under these tests: flat at z 100.
    constexpr float kFloorZ = 100.f;

    Piece Put(PieceType t, float x, float y, float z, float yaw = 0.f)
    {
        Piece p;
        p.id = 1;
        p.type = t;
        p.map = 1;
        p.pos = {x, y, z};
        p.yaw = yaw;
        p.health = Def(t).health;
        return p;
    }

    PieceType Named(char const* name)
    {
        for (Structure const& s : Structures())
            if (std::string(s.name) == name)
                return PieceType(s.id);
        return PieceType::None;
    }

    Ground Flat()
    {
        return [](float, float, float) { return kFloorZ; };
    }
}

TEST(trap_wall_through_the_body)
{
    Vec3 const feet{0.f, 0.f, kFloorZ};
    // A wall's foot under the feet: the body is inside it.
    CHECK(Traps(Put(PieceType::WoodWall, 0.f, 0.f, kFloorZ), feet));
    // A styled wall is the same wall to the rules.
    CHECK(Traps(Put(Styled(PieceType::WoodWall, 2), 0.2f, 0.f, kFloorZ), feet));
    // A wall leant on (its face at the body's side) and one a yard off are no trap.
    CHECK(!Traps(Put(PieceType::WoodWall, kBodyRadius + 0.15f, 0.f, kFloorZ), feet));
    CHECK(!Traps(Put(PieceType::WoodWall, 1.f, 0.f, kFloorZ), feet));
    // A wall overhead (a storey up) is clear of a body under it.
    CHECK(!Traps(Put(PieceType::WoodWall, 0.f, 0.f, kFloorZ + 3.f), feet));
}

TEST(trap_floor_stood_on_is_free_one_at_the_knees_is_not)
{
    Vec3 const feet{0.f, 0.f, kFloorZ};
    // A floor's top 0.3 yd over its origin: one whose top is under the feet is stood on.
    CHECK(!Traps(Put(PieceType::WoodFloor, 0.f, 0.f, kFloorZ - 0.3f), feet));
    CHECK(Traps(Put(PieceType::WoodFloor, 0.f, 0.f, kFloorZ + 0.5f), feet));
    // A floor as a ceiling, over the head.
    CHECK(!Traps(Put(PieceType::WoodFloor, 0.f, 0.f, kFloorZ + kBodyHeight + 0.1f), feet));
}

TEST(trap_door_shut_on_a_body_in_its_doorway)
{
    Vec3 const feet{0.f, 0.f, kFloorZ};
    // A doorway's opening is no trap; its door shut is, and open again it is not.
    CHECK(!Traps(Put(PieceType::WoodDoorway, 0.f, 0.f, kFloorZ), feet));
    Piece door = Put(PieceType::WoodDoor, 0.f, 0.f, kFloorZ);
    CHECK(Traps(door, feet));
    door.open = true;
    CHECK(!Traps(door, feet));
}

TEST(trap_structure_walls_but_not_its_room)
{
    PieceType const cottage = Named("Cottage");
    CHECK(cottage != PieceType::None);
    Piece const c = Put(cottage, 0.f, 0.f, kFloorZ);
    // Its front wall's foot.
    CHECK(Traps(c, {-1.98f, -7.95f, kFloorZ}));
    // The room inside, under its roof, is clear; so is the yard well away.
    CHECK(!Traps(c, {-0.76f, -3.07f, kFloorZ}));
    CHECK(!Traps(c, {30.f, 30.f, kFloorZ}));
    // A list of pieces traps when any one does.
    std::vector<Piece> const list{Put(PieceType::WoodFloor, 40.f, 0.f, kFloorZ - 0.3f), c};
    CHECK(Traps(list, {-1.98f, -7.95f, kFloorZ}));
    CHECK(!Traps(list, {40.f, 0.f, kFloorZ}));
}

TEST(free_spot_nearest_clear_of_every_piece)
{
    Vec3 const feet{0.f, 0.f, kFloorZ};
    // Clear already: where it stands.
    std::vector<Piece> const none;
    std::optional<Vec3> const here = FreeSpot(none, feet, Flat());
    CHECK(here && (*here - feet).Length() < 1e-4f);

    // In a wall: out of either face, a little more than a body's radius off it, on the ground.
    std::vector<Piece> const wall{Put(PieceType::WoodWall, 0.f, 0.f, kFloorZ)};
    std::optional<Vec3> const out = FreeSpot(wall, feet, Flat());
    CHECK(out.has_value());
    if (out)
    {
        CHECK(!Traps(wall, *out));
        CHECK((*out - feet).Length() <= 1.01f);
        CHECK_NEAR(out->z, kFloorZ, 1e-3);
    }

    // A floor laid through the knees: up onto it.
    std::vector<Piece> const knees{Put(PieceType::WoodFloor, 0.f, 0.f, kFloorZ + 0.5f)};
    std::optional<Vec3> const up = FreeSpot(knees, feet, Flat());
    CHECK(up.has_value());
    if (up)
    {
        CHECK_NEAR(up->z, kFloorZ + 0.8f, 1e-3);
        CHECK_NEAR(up->x, 0.f, 1e-3);
        CHECK(!Traps(knees, *up));
    }

    // In a cottage's wall: out of it, and never into another of its boxes.
    std::vector<Piece> const cottage{Put(Named("Cottage"), 0.f, 0.f, kFloorZ)};
    Vec3 const inWall{-1.98f, -7.95f, kFloorZ};
    std::optional<Vec3> const fromHouse = FreeSpot(cottage, inWall, Flat());
    CHECK(fromHouse.has_value());
    if (fromHouse)
    {
        CHECK(!Traps(cottage, *fromHouse));
        CHECK((*fromHouse - inWall).Length() < 3.f);
    }

    // No ground given: the feet's height everywhere.
    std::optional<Vec3> const flat = FreeSpot(wall, feet, Ground());
    CHECK(flat && !Traps(wall, *flat) && std::fabs(flat->z - kFloorZ) < 1e-3f);
}

TEST(trap_refusal_on_the_wire)
{
    // The client names it by this number (wire.rs REFUSED_TRAPS).
    CHECK(uint8_t(Refusal::Traps) == 14);
    CHECK(uint8_t(Refusal::OutOfSight) == 15 && uint8_t(Refusal::CrowdsNpc) == 16);
    std::vector<uint8_t> const b = EncodeRefused(Refusal::Traps);
    CHECK(b.size() == 2 && b[0] == uint8_t(ServerOp::Refused) && b[1] == 14);
    // A piece may be put down farther out than it may be worked.
    CHECK(kPlaceReach >= 25.f && kPlaceReach > kReach);
}
