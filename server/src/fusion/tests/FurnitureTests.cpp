/*
 * Azeroth Warfare: tests for furniture (Furniture.h): the catalogue, placement on floors, tables
 * and walls, the uses' rules, the stash, the dummy's meter and the wire.
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Construction.h"
#include "fusion/Furniture.h"
#include "fusion/Homestead.h"
#include "fusion/Packets.h"
#include "fusion/Throttle.h"

#include <cmath>
#include <set>
#include <string>

using namespace Fusion;
using namespace Fusion::Build;
namespace F = Fusion::Furniture;

namespace
{
    PieceType ByName(char const* name)
    {
        for (F::Def const& d : F::All())
            if (std::string(d.name) == name)
                return PieceType(d.id);
        return PieceType::None;
    }

    PieceType FirstOf(F::Use u)
    {
        for (F::Def const& d : F::All())
            if (d.use == u)
                return PieceType(d.id);
        return PieceType::None;
    }

    Piece At(PieceType t, Vec3 pos, uint32_t id = 1, float yaw = 0.f)
    {
        Piece p;
        p.id = id;
        p.type = t;
        p.map = 1;
        p.pos = pos;
        p.yaw = yaw;
        p.health = Def(t).health;
        return p;
    }

    Ground Flat(float z) { return [z](float, float, float) { return z; }; }
}

TEST(the_catalogue_is_known_contiguous_and_mirrors_the_build_rules)
{
    CHECK(F::All().size() >= 30);
    uint8_t expect = F::kBase;
    std::set<std::string> names;
    for (F::Def const& d : F::All())
    {
        CHECK(d.id == expect++);
        PieceType const t = PieceType(d.id);
        CHECK(Known(d.id) && IsProp(t) && !IsStructure(t) && F::IsFurniture(t));
        CHECK(names.insert(d.name).second);
        CHECK(Def(t).name == d.name);
        CHECK(Def(t).groundOnly == (d.mount != F::Mount::Wall));
        CHECK(d.half.x > 0.f && d.half.y > 0.f && d.half.z > 0.f);
        // Every piece is made at the trade window, as the rest of Construction's.
        CHECK(Construction::RecipeFor(t) != nullptr && Construction::Buildable(t));
        CHECK(!Construction::Reinforceable(t));
        if (d.use == F::Use::Stash)
            CHECK(d.stashSlots > 0);
        if (d.use == F::Use::Book)
            CHECK(F::BookOf(d.book) != nullptr);
        if (d.use == F::Use::Station)
            CHECK(F::SpellFocus(d.station) != 0);
        if (d.use == F::Use::Surface)
            CHECK(d.top > 0.f);
    }
    CHECK(F::kBase + F::All().size() <= 256);
    if (F::kBase + F::All().size() < 256)
        CHECK(!Known(uint8_t(F::kBase + F::All().size())));
    // The structures stop short of furniture.
    CHECK(Structures().size() + kStructureBase <= F::kBase);
}

TEST(every_use_has_a_piece_and_every_station_kind_is_there)
{
    for (F::Use u : {F::Use::Decor, F::Use::Seat, F::Use::Bed, F::Use::Surface, F::Use::Stash, F::Use::Book, F::Use::Station,
             F::Use::Mailbox, F::Use::Dummy, F::Use::Trophy, F::Use::Stage})
        CHECK(FirstOf(u) != PieceType::None);
    std::set<uint32_t> focuses;
    for (F::Def const& d : F::All())
        if (d.use == F::Use::Station)
            focuses.insert(F::SpellFocus(d.station));
    // Anvil 1, loom 2, forge 3, cooking fire 4, and the alchemy lab.
    CHECK(focuses.count(1) && focuses.count(2) && focuses.count(3) && focuses.count(4) && focuses.count(F::kAlchemyLabFocus));
    CHECK(F::UseOf(PieceType::WoodWall) == F::Use::Decor && F::Find(PieceType::WoodWall) == nullptr);
}

TEST(some_furnishings_are_sold_and_some_found_and_nothing_else_changes_hands)
{
    CHECK(!F::Sold().empty());
    for (F::Def const* d : F::Sold())
        CHECK(d->priceCopper >= 100);
    // A cut finds nothing most of the time, and only droppable pieces otherwise.
    CHECK(F::DropFor(F::kDropChance, 0.5f) == PieceType::None);
    CHECK(F::DropFor(0.99f, 0.f) == PieceType::None);
    std::set<uint8_t> found;
    for (int i = 0; i < 100; ++i)
    {
        PieceType const t = F::DropFor(0.f, float(i) / 100.f);
        CHECK(t != PieceType::None && F::Find(t) && F::Find(t)->dropWeight > 0);
        found.insert(uint8_t(t));
    }
    CHECK(found.size() >= 3);
    CHECK(Homestead::NeededFor(Homestead::Action::BuyDecor) == Homestead::Npc::Steward);
}

TEST(fine_placement_snaps_to_fifteen_degrees_and_a_quarter_yard)
{
    CHECK(std::fabs(F::SnapYaw(0.1f)) < 1e-4f);
    CHECK(std::fabs(F::SnapYaw(0.2f) - F::kTurnStep) < 1e-4f);
    CHECK(std::fabs(F::SnapYaw(-F::kTurnStep) - (6.2831853f - F::kTurnStep)) < 1e-3f);
    CHECK(std::fabs(F::SnapGrid(1.13f) - 1.25f) < 1e-5f);
    CHECK(std::fabs(F::SnapGrid(-0.12f) - 0.f) < 1e-5f);
}

TEST(a_chair_stands_on_the_ground_or_a_floor_but_not_in_the_air)
{
    PieceType const chair = FirstOf(F::Use::Seat);
    std::vector<Piece> none;
    CHECK(F::Footed(At(chair, {0, 0, 100.f}), none, Flat(100.f)));
    CHECK(!F::Footed(At(chair, {0, 0, 103.f}), none, Flat(100.f)));
    // On a wood floor's top (0.3 up).
    std::vector<Piece> floor = {At(PieceType::WoodFloor, {0, 0, 110.f}, 7)};
    CHECK(F::Footed(At(chair, {0.5f, 0.2f, 110.3f}), floor, Flat(100.f)));
}

TEST(small_things_stand_on_tables_but_a_chair_does_not)
{
    PieceType table = PieceType::None;
    for (F::Def const& d : F::All())
        if (d.use == F::Use::Surface && d.mount == F::Mount::Floor)
        {
            table = PieceType(d.id);
            break;
        }
    CHECK(table != PieceType::None);
    float const top = F::Find(table)->top;
    std::vector<Piece> pieces = {At(table, {0, 0, 100.f}, 7)};
    PieceType small = PieceType::None;
    for (F::Def const& d : F::All())
        if (d.mount == F::Mount::Surface)
        {
            small = PieceType(d.id);
            break;
        }
    CHECK(small != PieceType::None);
    Vec3 const c = Center(pieces[0]);
    Piece const onTop = At(small, {c.x, c.y, 100.f + top}, 8);
    CHECK(F::OnSurface(onTop, pieces));
    CHECK(F::Footed(onTop, pieces, Flat(90.f)));
    // A chair (a floor piece) isn't put on a table.
    CHECK(!F::Footed(At(FirstOf(F::Use::Seat), {c.x, c.y, 100.f + top}, 9), pieces, Flat(90.f)));
    // Nor is anything that floats above the table.
    CHECK(!F::Footed(At(small, {c.x, c.y, 100.f + top + 1.f}, 10), pieces, Flat(90.f)));
    // A row of the same small thing may stand close together; the table holds it up.
    std::vector<Piece> row = pieces;
    row.push_back(onTop);
    Piece next = onTop;
    next.id = 11;
    next.pos.x += 0.2f;
    CHECK(!Overlaps({row[1]}, next));
    row.push_back(next);
    row[0].health = Def(table).health;
    Supports const s = Solve(row, Flat(100.f));
    CHECK(Stands(small, s.at(11)));
}

TEST(wall_pieces_hang_on_a_wall_and_nowhere_else)
{
    PieceType wallPiece = PieceType::None;
    for (F::Def const& d : F::All())
        if (d.mount == F::Mount::Wall)
        {
            wallPiece = PieceType(d.id);
            break;
        }
    CHECK(wallPiece != PieceType::None);
    F::Def const& d = *F::Find(wallPiece);
    // A wood wall at x = 0 (its box 0.1 thick along x, 3 wide, 3 tall).
    std::vector<Piece> walls = {At(PieceType::WoodWall, {0, 0, 100.f}, 7)};
    WorldBox const wb = Place(walls[0], Def(PieceType::WoodWall).boxes[0]);
    float const face = wb.center.x + wb.half.x;
    // Hung with its thin side flat on the wall's face, its box's back against it.
    bool const thinX = d.half.x <= d.half.y;
    float const yaw = thinX ? 0.f : 1.5707963f;
    float const back = thinX ? d.half.x - d.center.x : d.half.y + d.center.y;
    Piece hung = At(wallPiece, {face + back + 0.01f, 0.f, 101.f}, 8, yaw);
    CHECK(F::OnWall(hung, walls));
    CHECK(F::Footed(hung, walls, Flat(0.f)));
    std::vector<Piece> with = walls;
    with.push_back(hung);
    CHECK(Stands(wallPiece, Solve(with, Flat(100.f)).at(8)));
    // Away from the wall it hangs on nothing.
    Piece away = hung;
    away.pos.x += 2.f;
    CHECK(!F::Footed(away, walls, Flat(0.f)));
    // Another piece of furniture is no wall: the same piece hung on a wardrobe's side hangs on nothing.
    PieceType const wardrobe = ByName("Wardrobe");
    CHECK(wardrobe != PieceType::None);
    std::vector<Piece> furniture = {At(wardrobe, {0, 0, 100.f}, 9)};
    WorldBox const fb = Place(furniture[0], Def(wardrobe).boxes[0]);
    Piece onFurniture = hung;
    onFurniture.pos.x = fb.center.x + fb.half.x + back + 0.01f;
    CHECK(!F::OnWall(onFurniture, furniture));
}

TEST(furniture_never_holds_up_the_building)
{
    // A wall leaning on a chest falls when its floor goes.
    std::vector<Piece> pieces = {At(FirstOf(F::Use::Stash), {0, 0, 100.f}, 1), At(PieceType::WoodWall, {0.f, 0.f, 103.f}, 2)};
    Supports const s = Solve(pieces, Flat(100.f));
    CHECK(s.at(1) >= 0.99f);
    CHECK(s.at(2) < Rules(Material::Wood).minSupport || !Touch(pieces[0], pieces[1]));
}

TEST(stations_answer_their_spell_focus_within_reach)
{
    PieceType anvil = PieceType::None;
    for (F::Def const& d : F::All())
        if (d.station == F::Station::Anvil)
            anvil = PieceType(d.id);
    std::vector<Piece> pieces = {At(anvil, {0, 0, 0}, 1)};
    CHECK(F::StationNear(pieces, {2, 0, 0}, 1));
    CHECK(!F::StationNear(pieces, {2, 0, 0}, 4));         // no cooking fire
    CHECK(!F::StationNear(pieces, {F::kStationReach + 3.f, 0, 0}, 1));
    pieces[0].health = 0.f;
    CHECK(!F::StationNear(pieces, {2, 0, 0}, 1));
    CHECK(!F::StationNear(pieces, {2, 0, 0}, 0));
}

TEST(a_bed_rests_only_where_the_plot_allows_and_a_stage_boosts_songs)
{
    std::vector<Piece> pieces = {At(FirstOf(F::Use::Bed), {0, 0, 0}, 1), At(FirstOf(F::Use::Stage), {50, 0, 0}, 2)};
    CHECK(F::RestsNear(pieces, {3, 0, 0}, [](Piece const&) { return true; }));
    CHECK(!F::RestsNear(pieces, {3, 0, 0}, [](Piece const&) { return false; }));
    CHECK(!F::RestsNear(pieces, {F::kRestRadius + 5.f, 0, 0}, [](Piece const&) { return true; }));
    CHECK(F::OnStage(pieces, Center(pieces[1])));
    CHECK(!F::OnStage(pieces, {0, 0, 0}));
    CHECK(F::kStageBoost > 1.f);
    // A seat puts its sitter on top of it, facing its way.
    Vec3 const seat = F::SeatSpot(At(FirstOf(F::Use::Seat), {5, 5, 10}, 3));
    CHECK(seat.z > 10.f && seat.z < 12.f);
}

TEST(the_stash_merges_stacks_keeps_to_its_room_and_takes_out_exactly)
{
    std::vector<Piece> plot = {At(FirstOf(F::Use::Stash), {0, 0, 0}, 1), At(FirstOf(F::Use::Stash), {3, 0, 0}, 2),
        At(FirstOf(F::Use::Seat), {6, 0, 0}, 3)};
    uint16_t const slots = F::StashSlots(plot);
    CHECK(slots == 2 * F::Find(plot[0].type)->stashSlots);
    plot[1].health = 0.f;
    CHECK(F::StashSlots(plot) == F::Find(plot[0].type)->stashSlots);
    std::vector<Piece> many(100, plot[0]);
    CHECK(F::StashSlots(many) == F::kMaxStashSlots);

    std::vector<F::StashEntry> stash;
    CHECK(F::Deposit(stash, 2, 2589, 15, 20) == F::StashResult::Ok);
    CHECK(F::Deposit(stash, 2, 2589, 10, 20) == F::StashResult::Ok);       // 20 + 5
    CHECK(stash.size() == 2 && stash[0].count == 20 && stash[1].count == 5);
    CHECK(F::Deposit(stash, 2, 2589, 16, 20) == F::StashResult::NoRoom);   // only 15 left
    CHECK(stash[1].count == 5);                                             // all or nothing
    CHECK(F::Deposit(stash, 2, 4470, 1, 20) == F::StashResult::NoRoom);    // no slot for another item
    CHECK(F::Deposit(stash, 2, 2589, 15, 20) == F::StashResult::Ok);
    F::StashEntry out;
    CHECK(F::Withdraw(stash, 1, 30, out) == F::StashResult::Ok && out.item == 2589 && out.count == 20);
    CHECK(stash.size() == 1);
    CHECK(F::Withdraw(stash, 5, 1, out) == F::StashResult::NotThere);
    CHECK(F::Withdraw(stash, 0, 0, out) == F::StashResult::NotThere);

    F::ItemFacts plain;
    CHECK(F::Storable(plain));
    for (int i = 0; i < 6; ++i)
    {
        F::ItemFacts f;
        bool* flags[] = {&f.soulbound, &f.enchanted, &f.randomProperty, &f.quest, &f.filledBag, &f.conjured};
        *flags[i] = true;
        CHECK(!F::Storable(f));
    }
}

TEST(the_dummy_meter_reports_damage_per_second_and_goes_idle)
{
    F::DpsMeter m;
    CHECK(!m.Idle(100000) && m.Dps() == 0.f);
    m.Hit(1000, 100);
    m.Hit(3000, 200);
    m.Hit(5000, 300);
    CHECK(m.total == 600 && m.hits == 3);
    CHECK(std::fabs(m.Seconds() - 4.f) < 1e-4f);
    CHECK(std::fabs(m.Dps() - 150.f) < 1e-3f);
    CHECK(!m.Idle(5000 + F::kDummyIdleMs - 1));
    CHECK(m.Idle(5000 + F::kDummyIdleMs));
    CHECK(F::DpsReport(m).find("150.0 damage per second") != std::string::npos);
    // One hit: a second at least.
    F::DpsMeter one;
    one.Hit(0, 50);
    CHECK(one.Dps() == 50.f);
}

TEST(furniture_ops_round_trip_and_are_paced)
{
    uint32_t id = 0;
    CHECK(F::DecodeUse(F::EncodeUse(0xA1B2C3D4), id) && id == 0xA1B2C3D4);
    CHECK(!F::DecodeUse({F::kOpUse, 1, 2}, id));
    F::StashAct a{77, F::kStashPut, 255, 23, 12}, b;
    CHECK(F::DecodeStash(F::EncodeStash(a), b) && b.piece == 77 && b.action == F::kStashPut && b.a == 255 && b.b == 23 && b.count == 12);
    F::StashAct bad = a;
    bad.action = 7;
    CHECK(!F::DecodeStash(F::EncodeStash(bad), b));
    std::vector<uint8_t> const page = F::EncodePage(5, F::Use::Book, "Title", {"one", "two"});
    CHECK(page[0] == F::kOpPage && page[5] == uint8_t(F::Use::Book) && page[6] == 5);
    std::vector<uint8_t> const view = F::EncodeStashView(5, 16, {{2589, 20}}, {{255, 23, 4470, 3}});
    CHECK(view.size() == 1 + 4 + 2 + 2 + 6 + 1 + 8);
    CHECK(Throttle::Costly(Packets::kChannelBuild, F::EncodeUse(1)));
    CHECK(Throttle::Costly(Packets::kChannelBuild, F::EncodeStash(a)));
    // No clash with the build channel's other ops.
    for (uint8_t op : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(5), uint8_t(6), uint8_t(7)})
        CHECK(op != F::kOpUse && op != F::kOpStash);
    for (uint8_t op : {uint8_t(10), uint8_t(11), uint8_t(12), uint8_t(13), uint8_t(14), uint8_t(15), uint8_t(16), uint8_t(17), uint8_t(18),
             uint8_t(19), uint8_t(20)})
        CHECK(op != F::kOpPage && op != F::kOpStashView);
}

TEST(books_have_pages)
{
    CHECK(F::Books().size() >= 4);
    for (F::Book const& b : F::Books())
        CHECK(!b.pages.empty() && std::string(b.title).size() > 3);
}

TEST(furniture_is_saved_as_a_piece_round_trips_on_the_wire)
{
    PieceType const chest = FirstOf(F::Use::Stash);
    std::vector<Piece> pieces = {At(chest, {1, 2, 3}, 42, 0.5f)};
    std::vector<uint8_t> const body = EncodePieces(pieces, 0);
    // count u16, then id u32 and the type.
    CHECK(body.size() > 7 && body[0] == uint8_t(ServerOp::Pieces) && body[7] == uint8_t(chest));
    PlaceRequest r;
    std::vector<uint8_t> place = {uint8_t(ClientOp::Place), uint8_t(chest)};
    for (int i = 0; i < 16; ++i)
        place.push_back(0);
    CHECK(DecodePlace(place, r) && r.type == chest);
}
