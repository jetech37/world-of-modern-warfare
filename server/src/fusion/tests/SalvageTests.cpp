/*
 * Azeroth Warfare: tests for salvage, the fresh-materials rule and the portal's recipe
 * (Construction.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Construction.h"

#include <set>

using namespace Fusion;
using namespace Fusion::Construction;
using Fusion::Build::PieceType;

namespace
{
    uint32_t CountOf(std::vector<Material> const& list, uint32_t item)
    {
        uint32_t n = 0;
        for (Material const& m : list)
            if (m.item == item)
                n += m.count;
        return n;
    }
}

TEST(every_material_a_piece_takes_has_a_salvaged_item_of_its_own)
{
    std::set<uint32_t> fresh, salvaged;
    for (SalvagePair const& p : SalvageTable())
    {
        CHECK(p.salvaged >= kSalvagedFirst && p.salvaged <= kSalvagedLast);
        CHECK(fresh.insert(p.fresh).second);            // each once
        CHECK(salvaged.insert(p.salvaged).second);
        CHECK(SalvagedOf(p.fresh) == p.salvaged && FreshOf(p.salvaged) == p.fresh);
        CHECK(FreshOf(p.fresh) == 0 && SalvagedOf(p.salvaged) == 0);
    }
    for (Recipe const& r : Recipes())
        for (Material const& m : r.materials)
            CHECK(SalvagedOf(m.item) != 0);
    for (uint8_t t = 1; t <= Build::kMaxReinforcement; ++t)
        for (Material const& m : ReinforceInfo(t)->materials)
            CHECK(SalvagedOf(m.item) != 0);
    CHECK(SalvagedOf(kWoodcuttingAxe) == 0 && FreshOf(kSalvagedLast + 1) == 0);
}

TEST(what_comes_back_out_of_a_piece_comes_back_salvaged)
{
    // A door taken down: half its materials, each its salvaged item.
    std::vector<Material> const back = AsSalvaged(Refund(PieceType::WoodDoor, 1.f));
    CHECK(CountOf(back, SalvagedOf(kSturdyLumber)) == 7);
    CHECK(CountOf(back, SalvagedOf(kBronzeHinge)) == 1);
    CHECK(CountOf(back, SalvagedOf(kCopperNails)) == 4);
    CHECK(CountOf(back, kSturdyLumber) == 0);
    for (Material const& m : back)
        CHECK(FreshOf(m.item) != 0);
    // Summed by item, and the empty ones dropped.
    std::vector<Material> const summed = AsSalvaged({{kRoughLumber, 2}, {kRoughLumber, 3}, {kIronBar, 0}});
    CHECK(summed.size() == 1 && summed[0].item == SalvagedOf(kRoughLumber) && summed[0].count == 5);
    // Something with no salvaged item stays itself.
    CHECK(AsSalvaged({{kWoodcuttingAxe, 1}})[0].item == kWoodcuttingAxe);
}

TEST(salvaged_goods_build_like_fresh_ones)
{
    Recipe const* wall = RecipeFor(PieceType::WoodWall);    // 16 Rough Lumber, 4 Copper Nails
    Inventory bags{{SalvagedOf(kRoughLumber), 16}, {SalvagedOf(kCopperNails), 4}};
    CHECK(CanAffordBuild(bags, wall->materials));
    CHECK(!CanAfford(bags, wall->materials));               // as the trades see it: nothing
    CHECK(CheckPlace(PieceType::WoodWall, Tier::Apprentice, 30, bags, false) == PlaceCheck::Ok);
    // Half of each kind does too.
    Inventory mixed{{kRoughLumber, 8}, {SalvagedOf(kRoughLumber), 8}, {kCopperNails, 2}, {SalvagedOf(kCopperNails), 2}};
    CHECK(CanAffordBuild(mixed, wall->materials));
    // Short: what's missing counts both kinds.
    Inventory shy{{kRoughLumber, 7}, {SalvagedOf(kRoughLumber), 8}, {kCopperNails, 4}};
    std::vector<Material> const missing = MissingForBuild(shy, wall->materials);
    CHECK(missing.size() == 1 && missing[0].item == kRoughLumber && missing[0].count == 1);
    CHECK(CheckPlace(PieceType::WoodWall, Tier::Apprentice, 30, shy, false) == PlaceCheck::NoMaterials);
    // A reinforcement takes salvage too.
    Inventory braces{{SalvagedOf(kIronBracket), 4}, {kIronBar, 6}};
    CHECK(CheckReinforce(PieceType::WoodWall, 0, Tier::Journeyman, 150, braces, false) == ReinforceCheck::Ok);
    // The bags to count: each item and its salvaged one, once.
    std::vector<uint32_t> const items = ItemsToCount(wall->materials);
    CHECK(items.size() == 4);
}

TEST(a_piece_paid_with_salvage_teaches_nothing_and_fresh_goes_first_when_it_could)
{
    Recipe const* wall = RecipeFor(PieceType::WoodWall);
    Inventory bags{{kRoughLumber, 16}, {SalvagedOf(kRoughLumber), 16}, {kCopperNails, 4}, {SalvagedOf(kCopperNails), 4}};
    // A piece that could teach takes fresh first: it teaches.
    CHECK(CouldTeach(PieceType::WoodWall, 30, Tier::Apprentice));
    Payment fresh = PlanPayment(wall->materials, bags, true);
    CHECK(fresh.ok && !fresh.usedSalvage);
    CHECK(CountOf(fresh.take, kRoughLumber) == 16 && CountOf(fresh.take, SalvagedOf(kRoughLumber)) == 0);
    // One past that teaches nothing anyway (gray) spends the salvage first.
    CHECK(!CouldTeach(PieceType::WoodWall, 120, Tier::Journeyman));
    Payment salvage = PlanPayment(wall->materials, bags, false);
    CHECK(salvage.ok && salvage.usedSalvage);
    CHECK(CountOf(salvage.take, SalvagedOf(kRoughLumber)) == 16 && CountOf(salvage.take, kRoughLumber) == 0);
    // Not enough fresh: the rest from salvage, and it teaches nothing.
    Inventory few{{kRoughLumber, 10}, {SalvagedOf(kRoughLumber), 16}, {kCopperNails, 4}};
    Payment mixed = PlanPayment(wall->materials, few, true);
    CHECK(mixed.ok && mixed.usedSalvage);
    CHECK(CountOf(mixed.take, kRoughLumber) == 10 && CountOf(mixed.take, SalvagedOf(kRoughLumber)) == 6);
    CHECK(CountOf(mixed.take, kCopperNails) == 4);
    // Too little of both: no payment.
    CHECK(!PlanPayment(wall->materials, {{kRoughLumber, 8}, {SalvagedOf(kRoughLumber), 7}, {kCopperNails, 4}}, true).ok);
    // The skill-up: a paid placement teaches at orange; with salvage (paid = false) it doesn't.
    CHECK(PlacementSkillUp(PieceType::WoodWall, 30, Tier::Apprentice, !fresh.usedSalvage, 0.f) == 31);
    CHECK(PlacementSkillUp(PieceType::WoodWall, 30, Tier::Apprentice, !mixed.usedSalvage, 0.f) == 30);
    // At the tier's cap nothing teaches.
    CHECK(!CouldTeach(PieceType::WoodFloor, 75, Tier::Apprentice));
    CHECK(!CouldTeach(PieceType::WoodFloor, 10, Tier::None));
}

TEST(taking_down_and_putting_up_again_can_never_level_construction)
{
    // Every piece: what comes back out of it, by any road, is salvaged only, so a second build
    // from it has usedSalvage and teaches nothing.
    for (Recipe const& r : Recipes())
    {
        std::vector<Material> back = AsSalvaged(Refund(r.piece, 1.f, Reinforceable(r.piece) ? 3 : 0));
        std::vector<Material> const broken = AsSalvaged(Salvage(r.piece, 0));
        back.insert(back.end(), broken.begin(), broken.end());
        for (Material const& m : back)
            CHECK(FreshOf(m.item) != 0);
        Inventory bags;
        for (Material const& m : back)
            bags[m.item] += m.count;
        Payment const p = PlanPayment(r.materials, bags, true);
        CHECK(!p.ok || p.usedSalvage);
    }
}

TEST(the_portal_is_an_artisans_dear_piece)
{
    Recipe const* portal = RecipeFor(PieceType(Build::kPortalProp));
    CHECK(portal != nullptr);
    if (!portal)
        return;
    CHECK(portal->skill.required >= 275);
    CHECK(CountOf(portal->materials, kArcaneCrystal) >= 1);
    CHECK(CountOf(portal->materials, kLargeBrilliantShard) >= 1);
    CHECK(CountOf(portal->materials, kFortifiedPlate) >= 1);
    CHECK(Build::IsPortal(PieceType(Build::kPortalProp)) && Build::IsProp(PieceType(Build::kPortalProp)));
    CHECK(!Build::IsStructure(PieceType(Build::kPortalProp)));
    CHECK(Build::Def(PieceType(Build::kPortalProp)).name == "Portal");
    CHECK(Build::Def(PieceType(Build::kPortalProp)).health >= Build::Def(PieceType::StoneWall).health);
    CHECK(!Reinforceable(PieceType(Build::kPortalProp)));
}
