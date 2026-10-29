/*
 * Azeroth Warfare: tests for the building styles (Build.h Styles) and the second catalogue of
 * structures (ids 156 on). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Construction.h"
#include "fusion/Packets.h"
#include "fusion/Plots.h"

#include <string>

using namespace Fusion;
using namespace Fusion::Build;

namespace
{
    PieceType StructureNamed(char const* name)
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

    uint32_t CountOf(std::vector<Construction::Material> const& m, uint32_t item)
    {
        uint32_t n = 0;
        for (Construction::Material const& x : m)
            if (x.item == item)
                n += x.count;
        return n;
    }
}

TEST(every_built_piece_but_the_sentry_has_four_styles_that_are_it_to_the_rules)
{
    CHECK(StyledPieces().size() == kStyledSlots);
    // The styled range sits between the built pieces and the props.
    CHECK(kStyleBase >= uint8_t(PieceType::Count));
    CHECK(kStyleBase + (kStyleCount - 1) * kStyledSlots <= kPropBase);
    for (PieceType base : StyledPieces())
    {
        CHECK(base != PieceType::SentryGun);
        CHECK(Styled(base, 0) == base && StyleOf(base) == 0 && BaseOf(base) == base);
        Construction::Recipe const* plain = Construction::RecipeFor(base);
        CHECK(plain != nullptr);
        for (uint8_t style = 1; style < kStyleCount; ++style)
        {
            PieceType const t = Styled(base, style);
            CHECK(t != PieceType::None && t != base);
            CHECK(Known(uint8_t(t)) && !IsProp(t));
            CHECK(BaseOf(t) == base && StyleOf(t) == style);
            // Not styled twice.
            CHECK(Styled(t, style) == PieceType::None);
            // The same piece to the rules: boxes, health, snaps, material.
            PieceDef const& d = Def(t);
            PieceDef const& b = Def(base);
            CHECK(d.boxes.size() == b.boxes.size() && d.snaps.size() == b.snaps.size());
            CHECK(d.health == b.health && d.material == b.material && d.groundOnly == b.groundOnly);
            CHECK(Construction::Reinforceable(t) == Construction::Reinforceable(base));
            // Its recipe: the plain one at the same skill, and a little of the culture's goods.
            Construction::Recipe const* r = Construction::RecipeFor(t);
            CHECK(r != nullptr);
            if (!r || !plain)
                continue;
            CHECK(r->piece == t);
            CHECK(r->skill.required == plain->skill.required);
            for (Construction::Material const& m : plain->materials)
                CHECK(CountOf(r->materials, m.item) >= m.count);
            uint32_t extra = 0, spent = 0;
            for (Construction::Material const& m : r->materials)
                spent += m.count;
            for (Construction::Material const& m : plain->materials)
                extra += m.count;
            CHECK(spent > extra && spent - extra <= 2);
        }
    }
    // The sentry gun has no style, nor does a prop or a structure; an unknown style is none.
    CHECK(Styled(PieceType::SentryGun, 1) == PieceType::None);
    CHECK(Styled(PieceType(kPropBase), 1) == PieceType::None);
    CHECK(Styled(PieceType::WoodWall, kStyleCount) == PieceType::None);
    CHECK(StyleOf(PieceType(kPropBase)) == 0 && StyleOf(PieceType(kStructureBase)) == 0);
    CHECK(!Known(uint8_t(kStyleBase + (kStyleCount - 1) * kStyledSlots)));
    CHECK(std::string(StyleName(3)) == "Orcish" && std::string(StyleName(kStyleCount)).empty());
}

TEST(each_style_asks_its_own_cultures_goods)
{
    namespace C = Construction;
    PieceType const wall = PieceType::WoodWall;
    CHECK(CountOf(C::RecipeFor(Styled(wall, 1))->materials, C::kCoarseStone) == 2);   // Stormwind
    CHECK(CountOf(C::RecipeFor(Styled(wall, 2))->materials, C::kCopperBar) == 2);     // Ironforge
    CHECK(CountOf(C::RecipeFor(Styled(wall, 3))->materials, C::kLightLeather) == 2);  // Orcish
    CHECK(CountOf(C::RecipeFor(Styled(wall, 4))->materials, C::kWoolCloth) == 2);     // Night elf
    // A small thing asks one.
    CHECK(CountOf(C::RecipeFor(Styled(PieceType::Torch, 4))->materials, C::kWoolCloth) == 1);
    // Styles don't make building or taking down pay: half of a whole styled wall comes back.
    std::vector<C::Material> const back = C::Refund(Styled(wall, 3), 1.f);
    CHECK(CountOf(back, C::kLightLeather) == 1);
}

TEST(a_styled_door_still_opens_and_a_styled_bedroll_is_still_a_bedroll)
{
    Piece door = At(Styled(PieceType::WoodDoor, 4), 0.f, 0.f);
    std::vector<Box> const shut = BoxesOf(door);
    door.open = true;
    std::vector<Box> const open = BoxesOf(door);
    CHECK(shut.size() == 4 && open.size() == 4);
    CHECK(open[3].center.x > shut[3].center.x + 0.5f);
    CHECK(BaseOf(Styled(PieceType::Bedroll, 3)) == PieceType::Bedroll);
    CHECK(BaseOf(Styled(PieceType::Campfire, 2)) == PieceType::Campfire);
}

TEST(the_recipes_with_every_style_still_fit_one_message)
{
    std::vector<uint8_t> const body = Construction::EncodeRecipes(Construction::Recipes());
    CHECK(body.size() + 1 <= Packets::kMaxChannelBody);
    std::vector<Construction::Recipe> back;
    CHECK(Construction::DecodeRecipes(body, back) && back.size() == Construction::Recipes().size());
}

TEST(the_second_catalogue_has_a_town_a_farm_and_a_camp)
{
    for (char const* name : {"Inn", "Chapel", "Windmill", "Farmhouse", "Blacksmith", "Lumber yard", "Lighthouse",
             "Gate arch", "City wall", "Dock", "Gazebo", "Orcish longhouse", "Moonwell shrine"})
    {
        PieceType const t = StructureNamed(name);
        CHECK(t != PieceType::None);
        if (t == PieceType::None)
            continue;
        CHECK(uint8_t(t) >= 156 && IsStructure(t) && Known(uint8_t(t)));
        Construction::Recipe const* r = Construction::RecipeFor(t);
        CHECK(r && r->materials.size() >= 3 && r->skill.required >= 100);
        CHECK(Def(t).boxes.size() >= 8 && Def(t).boxes.size() <= 64);
    }
    // The big town buildings are Artisan work.
    CHECK(Construction::RecipeFor(StructureNamed("Inn"))->skill.required >= 250);
    CHECK(Construction::RecipeFor(StructureNamed("Lighthouse"))->skill.required >= 250);
    CHECK(Def(StructureNamed("City wall")).material == Material::Stone);
    CHECK(Def(StructureNamed("Dock")).material == Material::Wood);
}

TEST(the_city_wall_carries_a_walk_and_the_gate_arch_lets_you_through)
{
    // The wall's top is a walk at four and a half yards: a body coming down on it lands there.
    Piece const wall = At(StructureNamed("City wall"), 0.f, 0.f);
    std::optional<float> const top = TopUnder(wall, 0.f, -3.f, 110.f);
    CHECK(top && *top > 104.f && *top < 105.f);
    // Its steps climb the inside: low near the bottom, high near the top.
    std::optional<float> const low = TopUnder(wall, -2.f, -0.5f, 110.f);
    std::optional<float> const high = TopUnder(wall, -2.f, 7.5f, 110.f);
    CHECK(low && high && *low < 101.f && *high > 104.f);
    // A shot through the wall stops.
    float t = 0.f;
    CHECK(RayHits(wall, {-20.f, -3.f, 102.f}, {1.f, 0.f, 0.f}, 40.f, t));
    // The gate arch: a way through its middle, its piers either side.
    Piece const arch = At(StructureNamed("Gate arch"), 0.f, 0.f);
    CHECK(!RayHits(arch, {-20.f, 0.f, 101.5f}, {1.f, 0.f, 0.f}, 40.f, t));
    CHECK(RayHits(arch, {-20.f, 7.f, 101.5f}, {1.f, 0.f, 0.f}, 40.f, t));
}

TEST(the_second_catalogue_fits_the_plots_by_size)
{
    Plots::Grid const g({
        {Plots::Size::Small, 1, 1000.f, 2000.f, 24.f, 1, 1, 1, 1, 1, 0},
        {Plots::Size::Standard, 1, 3000.f, 2000.f, 40.f, 1, 1, 51, 1, 1, 0},
        {Plots::Size::Large, 1, 2000.f, 2000.f, 64.f, 1, 1, 101, 1, 1, 0},
    });
    // A gazebo and the moonwell go on a small plot; the inn needs a large one.
    CHECK(Plots::Fits(g, 1, At(StructureNamed("Gazebo"), 1012.f, 2012.f)));
    CHECK(Plots::Fits(g, 1, At(StructureNamed("Moonwell shrine"), 1012.f, 2012.f)));
    CHECK(!Plots::Fits(g, 1, At(StructureNamed("Inn"), 1012.f, 2012.f)));
    CHECK(Plots::Fits(g, 101, At(StructureNamed("Inn"), 2032.f, 2032.f)));
    // The farmhouse and its yard take a standard plot's width.
    CHECK(Plots::Fits(g, 51, At(StructureNamed("Farmhouse"), 3020.f, 2020.f)));
}
