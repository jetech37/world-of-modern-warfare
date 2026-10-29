/*
 * Azeroth Warfare: tests for Construction (Construction.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Construction.h"

using namespace Fusion;
using namespace Fusion::Construction;
using Fusion::Build::PieceType;

TEST(every_known_piece_has_a_recipe_with_materials)
{
    size_t known = 0;
    for (uint32_t t = 0; t < 256; ++t)
    {
        // The sentry gun is a known piece but no longer built (a killstreak now).
        if (!Build::Known(uint8_t(t)) || PieceType(t) == PieceType::SentryGun)
        {
            CHECK(RecipeFor(PieceType(t)) == nullptr);
            continue;
        }
        ++known;
        Recipe const* r = RecipeFor(PieceType(t));
        CHECK(r != nullptr);
        if (!r)
            continue;
        CHECK(r->piece == PieceType(t));
        CHECK(!r->materials.empty());
        for (Material const& m : r->materials)
            CHECK(m.item != 0 && m.count > 0);
        // Each colour comes after the last, and orange starts within Artisan.
        CHECK(r->skill.required >= 1 && r->skill.required <= 300);
        CHECK(r->skill.required < r->skill.yellow && r->skill.yellow < r->skill.green && r->skill.green < r->skill.gray);
    }
    CHECK(Recipes().size() == known);
    // In type order.
    for (size_t i = 1; i < Recipes().size(); ++i)
        CHECK(uint8_t(Recipes()[i - 1].piece) < uint8_t(Recipes()[i].piece));
}

TEST(wood_comes_early_stone_mid_and_the_big_props_late)
{
    CHECK(RecipeFor(PieceType::WoodFloor)->skill.required == 1);
    CHECK(RecipeFor(PieceType::WoodWall)->skill.required < 75);
    CHECK(RecipeFor(PieceType::StoneWall)->skill.required >= 150);
    CHECK(RecipeFor(PieceType::StoneFloor)->skill.required >= 75);
    CHECK(RecipeFor(PieceType::WoodDoor)->skill.required > RecipeFor(PieceType::WoodDoorway)->skill.required);
    CHECK(RecipeFor(PieceType(126))->skill.required >= 275);     // the well
    // There's something orange or yellow to make all the way to 300.
    for (uint16_t s = 1; s < 300; ++s)
    {
        bool teaches = false;
        for (Recipe const& r : Recipes())
            if (Color c = ColorOf(s, r.skill); c == Color::Orange || c == Color::Yellow)
                teaches = true;
        CHECK(teaches);
    }
}

TEST(tiers_cap_the_skill_and_train_in_order)
{
    CHECK(Cap(Tier::None) == 0);
    CHECK(Cap(Tier::Apprentice) == 75);
    CHECK(Cap(Tier::Journeyman) == 150);
    CHECK(Cap(Tier::Expert) == 225);
    CHECK(Cap(Tier::Artisan) == 300);
    CHECK(Next(Tier::None) == Tier::Apprentice && Next(Tier::Artisan) == Tier::None);

    CHECK(CanTrain(Tier::None, Tier::Apprentice, 0, 5, 1000) == TrainResult::Ok);
    CHECK(CanTrain(Tier::None, Tier::Apprentice, 0, 4, 1000) == TrainResult::LevelTooLow);
    CHECK(CanTrain(Tier::None, Tier::Apprentice, 0, 5, 999) == TrainResult::NotEnoughMoney);
    CHECK(CanTrain(Tier::None, Tier::Journeyman, 0, 60, 1000000) == TrainResult::NotNext);
    CHECK(CanTrain(Tier::Apprentice, Tier::Apprentice, 10, 60, 1000000) == TrainResult::AlreadyKnown);
    CHECK(CanTrain(Tier::Apprentice, Tier::Journeyman, 49, 60, 1000000) == TrainResult::SkillTooLow);
    CHECK(CanTrain(Tier::Apprentice, Tier::Journeyman, 50, 10, 5000) == TrainResult::Ok);
    CHECK(CanTrain(Tier::Journeyman, Tier::Expert, 125, 19, 1000000) == TrainResult::LevelTooLow);
    CHECK(CanTrain(Tier::Expert, Tier::Artisan, 200, 35, 100000) == TrainResult::Ok);
    CHECK(CanTrain(Tier::Artisan, Tier::Artisan, 300, 60, 0) == TrainResult::AlreadyKnown);
    // A game master skips the asks, not the order.
    CHECK(CanTrain(Tier::Apprentice, Tier::Journeyman, 1, 1, 0, true) == TrainResult::Ok);
    CHECK(CanTrain(Tier::None, Tier::Artisan, 1, 1, 0, true) == TrainResult::NotNext);
}

TEST(skill_up_odds_follow_vanillas_colours)
{
    Thresholds const t{100, 125, 150, 175};
    CHECK(ColorOf(99, t) == Color::Red);
    CHECK(ColorOf(100, t) == Color::Orange);
    CHECK(ColorOf(124, t) == Color::Orange);
    CHECK(ColorOf(125, t) == Color::Yellow);
    CHECK(ColorOf(150, t) == Color::Green);
    CHECK(ColorOf(175, t) == Color::Gray);

    CHECK_NEAR(SkillUpChance(99, t), 0.f, 1e-6);
    CHECK_NEAR(SkillUpChance(110, t), 1.f, 1e-6);
    CHECK_NEAR(SkillUpChance(125, t), 1.f, 1e-6);       // yellow's first point: (175-125)/(175-125)
    CHECK_NEAR(SkillUpChance(150, t), 0.5f, 1e-6);      // green's first: half
    CHECK_NEAR(SkillUpChance(170, t), 0.1f, 1e-6);
    CHECK_NEAR(SkillUpChance(175, t), 0.f, 1e-6);
    CHECK_NEAR(SkillUpChance(250, t), 0.f, 1e-6);
    // Never rising as the skill does.
    for (uint16_t s = 100; s < 200; ++s)
        CHECK(SkillUpChance(uint16_t(s + 1), t) <= SkillUpChance(s, t));

    CHECK(ApplySkillUp(110, 150, t, 0.99f) == 111);     // orange: sure
    CHECK(ApplySkillUp(150, 225, t, 0.49f) == 151);
    CHECK(ApplySkillUp(150, 225, t, 0.51f) == 150);
    CHECK(ApplySkillUp(180, 225, t, 0.f) == 180);       // gray: never
    CHECK(ApplySkillUp(110, 110, t, 0.f) == 110);       // at the tier's cap
    CHECK(Spaced(40).yellow == 65 && Spaced(40).green == 90 && Spaced(40).gray == 115);
}

TEST(affording_a_piece_counts_every_material)
{
    Recipe const& door = *RecipeFor(PieceType::WoodDoor);
    Inventory bags;
    CHECK(!CanAfford(bags, door.materials));
    CHECK(Missing(bags, door.materials).size() == door.materials.size());
    for (Material const& m : door.materials)
        bags[m.item] = m.count;
    CHECK(CanAfford(bags, door.materials));
    bags[kBronzeHinge] = 1;
    std::vector<Material> const short1 = Missing(bags, door.materials);
    CHECK(short1.size() == 1 && short1[0].item == kBronzeHinge && short1[0].count == 1);
    // The same item twice asks for both.
    std::vector<Material> const twice = {{kRoughLumber, 3}, {kRoughLumber, 2}};
    CHECK(!CanAfford({{kRoughLumber, 4}}, twice));
    CHECK(CanAfford({{kRoughLumber, 5}}, twice));
}

TEST(placing_checks_learning_skill_and_materials_and_gms_build_free)
{
    Inventory const none;
    Inventory wood{{kRoughLumber, 100}, {kCopperNails, 100}};
    CHECK(CheckPlace(PieceType::WoodFloor, Tier::None, 0, wood, false) == PlaceCheck::NotLearned);
    CHECK(CheckPlace(PieceType::WoodFloor, Tier::Apprentice, 1, wood, false) == PlaceCheck::Ok);
    CHECK(CheckPlace(PieceType::WoodFloor, Tier::Apprentice, 1, none, false) == PlaceCheck::NoMaterials);
    CHECK(CheckPlace(PieceType::WoodWall, Tier::Apprentice, 19, wood, false) == PlaceCheck::SkillTooLow);
    CHECK(CheckPlace(PieceType::StoneWall, Tier::Apprentice, 1, none, false) == PlaceCheck::SkillTooLow);
    CHECK(CheckPlace(PieceType(99), Tier::Artisan, 300, wood, false) == PlaceCheck::Unknown);
    // A game master (or a free realm) needs neither skill nor materials.
    CHECK(CheckPlace(PieceType::StoneWall, Tier::None, 0, none, true) == PlaceCheck::Ok);
    CHECK(CheckPlace(PieceType(99), Tier::None, 0, none, true) == PlaceCheck::Unknown);
}

TEST(only_paid_pieces_teach)
{
    // The wood floor: orange to 25.
    CHECK(PlacementSkillUp(PieceType::WoodFloor, 10, Tier::Apprentice, true, 0.99f) == 11);
    CHECK(PlacementSkillUp(PieceType::WoodFloor, 10, Tier::Apprentice, false, 0.f) == 10);
    CHECK(PlacementSkillUp(PieceType::WoodFloor, 10, Tier::None, true, 0.f) == 10);
    CHECK(PlacementSkillUp(PieceType::WoodFloor, 75, Tier::Apprentice, true, 0.f) == 75);    // gray, and capped
    CHECK(PlacementSkillUp(PieceType::WoodRoof, 75, Tier::Apprentice, true, 0.f) == 75);     // capped
    CHECK(PlacementSkillUp(PieceType::WoodRoof, 75, Tier::Journeyman, true, 0.f) == 76);
}

TEST(taking_down_gives_back_half_and_never_more)
{
    // The door: 14 sturdy lumber, 2 hinges, 8 nails.
    std::vector<Material> const whole = Refund(PieceType::WoodDoor, 1.f);
    CHECK(whole.size() == 3);
    for (Material const& m : whole)
    {
        if (m.item == kSturdyLumber) CHECK(m.count == 7);
        if (m.item == kBronzeHinge) CHECK(m.count == 1);
        if (m.item == kCopperNails) CHECK(m.count == 4);
    }
    // Half broken: a quarter, rounded down; two hinges at a quarter share come back as nothing.
    std::vector<Material> const hurt = Refund(PieceType::WoodDoor, 0.5f);
    CHECK(hurt.size() == 2 && hurt[0].item == kSturdyLumber && hurt[0].count == 3);
    CHECK(Refund(PieceType::WoodDoor, 0.f).empty());
    CHECK(Refund(PieceType::Torch, 1.f).empty());
    // For every piece, at any health, never more than went in.
    for (Recipe const& r : Recipes())
        for (float h : {1.f, 0.7f, 0.3f, 2.f})
            for (Material const& m : Refund(r.piece, h))
            {
                uint32_t in = 0;
                for (Material const& k : r.materials)
                    if (k.item == m.item)
                        in += k.count;
                CHECK(m.count * 2 <= in);
            }
}

TEST(lumber_nodes_gate_on_axe_skill_and_tier)
{
    CHECK(Nodes().size() == 4);
    NodeDef const* soft = NodeFor(191001);
    NodeDef const* iron = NodeFor(191004);
    CHECK(soft && soft->node == Node::Softwood && soft->lumber == kRoughLumber);
    CHECK(iron && iron->node == Node::Ironwood && iron->lumber == kIronwoodPlank);
    CHECK(NodeFor(1731) == nullptr);     // a copper vein
    CHECK(NodeDefOf(Node::Hardwood)->gameobject == 191003);
    // Each tier's node opens at the skill the last tier's cap allows.
    CHECK(NodeDefOf(Node::SturdyTimber)->skill.required <= Cap(Tier::Apprentice));
    CHECK(NodeDefOf(Node::Hardwood)->skill.required <= Cap(Tier::Journeyman));
    CHECK(NodeDefOf(Node::Ironwood)->skill.required <= Cap(Tier::Expert));

    CHECK(CanGather(191001, Tier::Apprentice, 1, true) == GatherCheck::Ok);
    CHECK(CanGather(191001, Tier::Apprentice, 1, false) == GatherCheck::NoAxe);
    CHECK(CanGather(191001, Tier::None, 0, true) == GatherCheck::NotLearned);
    CHECK(CanGather(191002, Tier::Apprentice, 74, true) == GatherCheck::SkillTooLow);
    CHECK(CanGather(191002, Tier::Apprentice, 75, true) == GatherCheck::Ok);
    CHECK(CanGather(191004, Tier::Expert, 224, true) == GatherCheck::SkillTooLow);
    CHECK(CanGather(191004, Tier::Expert, 225, true) == GatherCheck::Ok);
    CHECK(CanGather(12345, Tier::Artisan, 300, true) == GatherCheck::NotANode);
    CHECK(CanGather(191004, Tier::None, 0, false, true) == GatherCheck::Ok);     // a game master

    for (NodeDef const& n : Nodes())
    {
        CHECK(n.minYield >= 1 && n.minYield <= n.maxYield);
        CHECK(NodeYield(n, 0.f) == n.minYield);
        CHECK(NodeYield(n, 0.9999f) == n.maxYield);
        CHECK(NodeYield(n, 1.f) == n.maxYield);
        CHECK(n.respawnMinSec > 0 && n.respawnMinSec <= n.respawnMaxSec);
    }
    // Rarer wood is slower to come back.
    CHECK(NodeDefOf(Node::Ironwood)->respawnMinSec > NodeDefOf(Node::Softwood)->respawnMinSec);
    // Cutting teaches like mining: softwood is orange early and gray at 100.
    CHECK(GatherSkillUp(*soft, 10, Tier::Apprentice, 0.99f) == 11);
    CHECK(GatherSkillUp(*soft, 75, Tier::Apprentice, 0.f) == 75);           // the cap
    CHECK(GatherSkillUp(*soft, 100, Tier::Journeyman, 0.f) == 100);         // gray
    CHECK(GatherSkillUp(*soft, 10, Tier::None, 0.f) == 10);
}

TEST(parts_craft_on_skill_and_reagents)
{
    CraftRecipe nails;
    nails.id = 1;
    nails.skillLine = kSkillBlacksmithing;
    nails.skill = {1, 30, 45, 60};
    nails.product = kCopperNails;
    nails.productCount = 5;
    nails.reagents = {{kCopperBar, 1}};
    CHECK(CanCraft(nails, 0, {{kCopperBar, 1}}) == CraftCheck::SkillTooLow);
    CHECK(CanCraft(nails, 1, {}) == CraftCheck::NoReagents);
    CHECK(CanCraft(nails, 1, {{kCopperBar, 1}}) == CraftCheck::Ok);
    CHECK(CanCraft(nails, 0, {{kCopperBar, 1}}, true) == CraftCheck::Ok);
    CHECK(CanCraft(nails, 0, {}, true) == CraftCheck::NoReagents);    // even a GM needs the bar
}

TEST(the_build_menu_wire_round_trips)
{
    std::vector<uint8_t> const bytes = EncodeRecipes(Recipes());
    CHECK(bytes[0] == uint8_t(ServerOp::Recipes));
    CHECK(bytes.size() < 8192);      // the channel's limit
    std::vector<Recipe> back;
    CHECK(DecodeRecipes(bytes, back));
    CHECK(back.size() == Recipes().size());
    for (size_t i = 0; i < back.size() && i < Recipes().size(); ++i)
    {
        Recipe const& a = Recipes()[i];
        Recipe const& b = back[i];
        CHECK(a.piece == b.piece && a.skill.required == b.skill.required && a.skill.gray == b.skill.gray);
        CHECK(a.materials.size() == b.materials.size());
        for (size_t k = 0; k < a.materials.size() && k < b.materials.size(); ++k)
            CHECK(a.materials[k].item == b.materials[k].item && a.materials[k].count == b.materials[k].count);
    }
    // One recipe exactly: op, count, type, four thresholds, its two materials.
    std::vector<uint8_t> const one = EncodeRecipes({*RecipeFor(PieceType::WoodFloor)});
    CHECK(one.size() == 1 + 2 + 1 + 8 + 1 + 2 * 6);
    CHECK(one[3] == uint8_t(PieceType::WoodFloor) && one[4] == 1 && one[5] == 0);
    CHECK(one[12] == 2 && one[13] == (kRoughLumber & 0xFF) && one[17] == 12);
    // Cut short or with bytes over: refused.
    std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 1);
    CHECK(!DecodeRecipes(cut, back));
    std::vector<uint8_t> over = bytes;
    over.push_back(0);
    CHECK(!DecodeRecipes(over, back));

    std::vector<uint8_t> const skill = EncodeSkill(Tier::Journeyman, 120);
    CHECK(skill.size() == 6);
    CHECK(skill[0] == uint8_t(ServerOp::Skill) && skill[1] == uint8_t(Tier::Journeyman));
    CHECK(skill[2] == 120 && skill[3] == 0 && skill[4] == 150 && skill[5] == 0);
}

// ── Kits: Construction crafts pieces ahead, as WoW's trades do (professions hub) ──

TEST(kits_are_made_taken_and_capped)
{
    Kits k;
    CHECK(KitsOf(k, PieceType::WoodWall) == 0);
    CHECK(!TakeKit(k, PieceType::WoodWall));
    CHECK(AddKits(k, PieceType::WoodWall, 3) == 3);
    CHECK(KitsOf(k, PieceType::WoodWall) == 3);
    CHECK(TakeKit(k, PieceType::WoodWall));
    CHECK(KitsOf(k, PieceType::WoodWall) == 2);
    CHECK(TakeKit(k, PieceType::WoodWall) && TakeKit(k, PieceType::WoodWall));
    CHECK(k.empty());
    // Never past the cap; an unknown piece takes none.
    CHECK(AddKits(k, PieceType::WoodFloor, kMaxKits + 10) == kMaxKits);
    CHECK(AddKits(k, PieceType::WoodFloor, 1) == 0);
    CHECK(AddKits(k, PieceType::None, 1) == 0);
    CHECK(KitsOf(k, PieceType::None) == 0);
}

TEST(a_kit_asks_what_building_asked)
{
    Recipe const* r = RecipeFor(PieceType::WoodWall);
    CHECK(r != nullptr);
    if (!r)
        return;
    Inventory none;
    Inventory enough;
    for (Material const& m : r->materials)
        enough[m.item] = m.count;
    CHECK(CheckKit(PieceType::WoodWall, Tier::None, 0, enough, 0, false) == KitCheck::NotLearned);
    CHECK(CheckKit(PieceType::WoodWall, Tier::Apprentice, uint16_t(r->skill.required - 1), enough, 0, false) == (r->skill.required > 1 ? KitCheck::SkillTooLow : KitCheck::Ok));
    CHECK(CheckKit(PieceType::WoodWall, Tier::Apprentice, r->skill.required, none, 0, false) == KitCheck::NoMaterials);
    CHECK(CheckKit(PieceType::WoodWall, Tier::Apprentice, r->skill.required, enough, 0, false) == KitCheck::Ok);
    CHECK(CheckKit(PieceType::WoodWall, Tier::Apprentice, r->skill.required, enough, kMaxKits, false) == KitCheck::Full);
    // A game master needs nothing; the sentry gun is no piece to make.
    CHECK(CheckKit(PieceType::WoodWall, Tier::None, 0, none, 0, true) == KitCheck::Ok);
    CHECK(CheckKit(PieceType::SentryGun, Tier::Artisan, 300, enough, 0, true) == KitCheck::Unknown);
    CHECK(CraftBatch(0) == 1 && CraftBatch(7) == 7 && CraftBatch(60000) == kMaxCraftBatch);
}

TEST(the_kit_wire_round_trips_and_refuses_junk)
{
    Kits k;
    AddKits(k, PieceType::WoodWall, 4);
    AddKits(k, PieceType::WoodFloor, 2);
    Kits back;
    CHECK(DecodeKits(EncodeKits(k), back));
    CHECK(back == k);
    std::vector<uint8_t> b = EncodeKits(k);
    b.push_back(0);
    CHECK(!DecodeKits(b, back));
    PieceType t = PieceType::None;
    uint16_t n = 0;
    CHECK(DecodeCraftKit(EncodeCraftKit(PieceType::WoodWall, 5), t, n));
    CHECK(t == PieceType::WoodWall && n == 5);
    CHECK(!DecodeCraftKit(EncodeCraftKit(PieceType::None, 1), t, n));
    CHECK(!DecodeCraftKit({kOpCraftKit, uint8_t(PieceType::WoodWall)}, t, n));
    CHECK(kChannelProfessions == 12);
}
