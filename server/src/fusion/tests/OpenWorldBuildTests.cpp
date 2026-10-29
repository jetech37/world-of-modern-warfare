/*
 * Azeroth Warfare: tests for open-world construction (design doc, section 12): pieces guns and
 * spells break, salvage, reinforcement, and the sentry gone to the killstreaks. GPL-2.0-or-later,
 * see Common.h.
 */

#include "Test.h"
#include "fusion/Build.h"
#include "fusion/Construction.h"

using namespace Fusion;
using namespace Fusion::Construction;
using Fusion::Build::PieceType;

namespace
{
    Build::Piece Make(PieceType t, uint8_t reinforcement = 0)
    {
        Build::Piece p;
        p.id = 1;
        p.type = t;
        p.reinforcement = reinforcement;
        p.health = Build::MaxHealth(t, reinforcement);
        return p;
    }

    uint32_t CountOf(std::vector<Material> const& list, uint32_t item)
    {
        uint32_t n = 0;
        for (Material const& m : list)
            if (m.item == item)
                n += m.count;
        return n;
    }

    // Rounds of `damage` a piece takes to break, as bullets.
    float Rounds(Build::Piece const& p, float damage)
    {
        return p.health / Build::DamageTaken(p, damage, Build::DamageKind::Bullet);
    }
}

TEST(the_sentry_is_no_longer_built)
{
    // A killstreak now (Killstreak.h): no recipe, nobody places one, not even a game master.
    CHECK(!Buildable(PieceType::SentryGun));
    CHECK(RecipeFor(PieceType::SentryGun) == nullptr);
    CHECK(CheckPlace(PieceType::SentryGun, Tier::Artisan, 300, {}, false) == PlaceCheck::Unknown);
    CHECK(CheckPlace(PieceType::SentryGun, Tier::None, 0, {}, true) == PlaceCheck::Unknown);
    // Still a known piece: the killstreak's sentry goes over the wire as one.
    CHECK(Build::Known(uint8_t(PieceType::SentryGun)));
    for (Recipe const& r : Recipes())
        CHECK(r.piece != PieceType::SentryGun);
}

TEST(building_is_semi_expensive)
{
    // A wall is a few trees' worth of lumber and the smith's nails, not a handful.
    CHECK(CountOf(RecipeFor(PieceType::WoodWall)->materials, kRoughLumber) >= 8);
    CHECK(CountOf(RecipeFor(PieceType::WoodWall)->materials, kCopperNails) >= 2);
    CHECK(CountOf(RecipeFor(PieceType::WoodFloor)->materials, kRoughLumber) >= 6);
    CHECK(CountOf(RecipeFor(PieceType::StoneWall)->materials, kSolidStone) >= 10);
    CHECK(CountOf(RecipeFor(PieceType::StoneWall)->materials, kIronBracket) >= 3);
    CHECK(CountOf(RecipeFor(PieceType::Sandbags)->materials, kLinenCloth) >= 6);
    // The camp's small things stay cheap.
    CHECK(CountOf(RecipeFor(PieceType::Torch)->materials, kRoughLumber) <= 1);
}

TEST(guns_break_open_world_walls_in_a_few_magazines)
{
    // An M4's round is 30 CoD, a magazine 30 rounds (900).
    float const mag = 30.f;
    CHECK(Rounds(Make(PieceType::WoodWall), 30.f) <= mag);
    float const stone = Rounds(Make(PieceType::StoneWall), 30.f) / mag;
    CHECK(stone >= 1.5f && stone <= 3.f);
    // FMJ (x1.5 on pieces) cuts it.
    CHECK(Rounds(Make(PieceType::StoneWall), 45.f) < Rounds(Make(PieceType::StoneWall), 30.f));
    // Every piece breaks to less than four magazines.
    for (uint8_t t = 1; t < uint8_t(PieceType::Count); ++t)
        CHECK(Rounds(Make(PieceType(t)), 30.f) <= 4.f * mag);
}

TEST(spells_break_walls_in_a_couple_of_casts)
{
    // A spell as strong as a creature of the caster's level is expected to cast (Scale.h).
    float const typical = Build::SpellPieceDamage(20.f + 10.f * 40.f, 40, false);
    CHECK_NEAR(typical, Build::kSpellPieceDamage, 1e-3);
    Build::Piece const wood = Make(PieceType::WoodWall);
    Build::Piece const stone = Make(PieceType::StoneWall);
    float const woodCasts = wood.health / Build::DamageTaken(wood, typical, Build::DamageKind::Spell);
    float const stoneCasts = stone.health / Build::DamageTaken(stone, typical, Build::DamageKind::Spell);
    CHECK(woodCasts <= 2.f);
    CHECK(stoneCasts > 2.f && stoneCasts <= 5.f);
    // A weak spell does a third, a huge one at most three times; a tick a fifth of a hit.
    CHECK_NEAR(Build::SpellPieceDamage(1.f, 40, false), 0.3f * Build::kSpellPieceDamage, 1e-3);
    CHECK_NEAR(Build::SpellPieceDamage(1e6f, 40, false), 3.f * Build::kSpellPieceDamage, 1e-3);
    CHECK_NEAR(Build::SpellPieceDamage((20.f + 10.f * 40.f) / 5.f, 40, true), Build::kSpellPieceDamage / 5.f, 1e-3);
    CHECK(Build::SpellPieceDamage(0.f, 40, false) == 0.f);
}

TEST(reinforcement_multiplies_health_and_turns_bullets)
{
    CHECK(Build::kMaxReinforcement == 3);
    float last = Build::MaxHealth(PieceType::StoneWall, 0);
    float lastCut = 0.f;
    for (uint8_t tier = 1; tier <= Build::kMaxReinforcement; ++tier)
    {
        float const h = Build::MaxHealth(PieceType::StoneWall, tier);
        CHECK(h > last * 1.5f);
        last = h;
        float const cut = Build::ReinforcedBulletReduction(tier);
        CHECK(cut > lastCut && cut < 1.f);
        lastCut = cut;
    }
    // The top tier holds against a dozen magazines and more.
    CHECK(Rounds(Make(PieceType::StoneWall, 3), 30.f) / 30.f >= 12.f);
    // Blasts and spells are not turned (explosives are the answer to a fortified wall).
    Build::Piece const fort = Make(PieceType::StoneWall, 3);
    CHECK_NEAR(Build::DamageTaken(fort, 100.f, Build::DamageKind::Spell), 100.f, 1e-4);
    CHECK_NEAR(Build::DamageTaken(fort, 100.f, Build::DamageKind::Blast),
        100.f * Build::Rules(Build::Material::Stone).blastScale, 1e-4);
    CHECK(Build::DamageTaken(fort, 100.f, Build::DamageKind::Bullet) < 40.f);
    // Out of range tiers are held to the top.
    CHECK_NEAR(Build::MaxHealth(PieceType::StoneWall, 9), Build::MaxHealth(PieceType::StoneWall, 3), 1e-3);
}

TEST(reinforcing_asks_skill_and_very_expensive_materials)
{
    CHECK(Reinforceable(PieceType::StoneWall));
    CHECK(Reinforceable(PieceType::WoodDoor));
    CHECK(!Reinforceable(PieceType::Torch));
    CHECK(!Reinforceable(PieceType::Bedroll));
    CHECK(!Reinforceable(PieceType::SentryGun));
    CHECK(!Reinforceable(PieceType(100)));      // WoW's props
    CHECK(ReinforceInfo(0) == nullptr && ReinforceInfo(4) == nullptr);
    uint16_t lastSkill = 0;
    for (uint8_t t = 1; t <= 3; ++t)
    {
        ReinforceTier const* r = ReinforceInfo(t);
        CHECK(r != nullptr);
        if (!r)
            continue;
        CHECK(r->skill > lastSkill);
        lastSkill = r->skill;
        CHECK(!r->materials.empty());
    }
    // Each tier costs more than the stone wall it goes on.
    Inventory rich{{kIronBracket, 100}, {kIronBar, 100}, {kReinforcedPlate, 100}, {kMithrilBar, 100},
        {kFortifiedPlate, 100}, {kThoriumBar, 100}, {kDenseStone, 100}};
    CHECK(CheckReinforce(PieceType::StoneWall, 0, Tier::Artisan, 300, rich, false) == ReinforceCheck::Ok);
    CHECK(CheckReinforce(PieceType::StoneWall, 3, Tier::Artisan, 300, rich, false) == ReinforceCheck::MaxTier);
    CHECK(CheckReinforce(PieceType::Torch, 0, Tier::Artisan, 300, rich, false) == ReinforceCheck::NotReinforceable);
    CHECK(CheckReinforce(PieceType::StoneWall, 0, Tier::None, 0, rich, false) == ReinforceCheck::NotLearned);
    CHECK(CheckReinforce(PieceType::StoneWall, 2, Tier::Expert, 225, rich, false) == ReinforceCheck::SkillTooLow);
    CHECK(CheckReinforce(PieceType::StoneWall, 0, Tier::Artisan, 300, {}, false) == ReinforceCheck::NoMaterials);
    CHECK(CheckReinforce(PieceType::StoneWall, 0, Tier::None, 0, {}, true) == ReinforceCheck::Ok);
    CHECK(CheckReinforce(PieceType::StoneWall, 3, Tier::None, 0, {}, true) == ReinforceCheck::MaxTier);
    // What a piece reinforced to a tier has had spent on it: every tier up to it.
    CHECK(ReinforcementSpent(0).empty());
    CHECK(CountOf(ReinforcementSpent(2), kIronBracket) == CountOf(ReinforceInfo(1)->materials, kIronBracket)
        + CountOf(ReinforceInfo(2)->materials, kIronBracket));
}

TEST(destroying_a_piece_salvages_part_of_it)
{
    // The stone wall: 40% of each material, rounded down.
    Recipe const* wall = RecipeFor(PieceType::StoneWall);
    std::vector<Material> const s = Salvage(PieceType::StoneWall, 0);
    for (Material const& m : wall->materials)
        CHECK(CountOf(s, m.item) == uint32_t(float(m.count) * kSalvageShare + 1e-4f));
    CHECK(kSalvageShare >= 0.3f && kSalvageShare <= 0.5f);
    // At least one of the main material when the piece cost any: a torch's lumber.
    CHECK(CountOf(Salvage(PieceType::Torch, 0), kRoughLumber) == 1);
    // A reinforced piece gives back a share of its reinforcement too.
    std::vector<Material> const fort = Salvage(PieceType::StoneWall, 3);
    CHECK(CountOf(fort, kFortifiedPlate) == uint32_t(float(CountOf(ReinforcementSpent(3), kFortifiedPlate)) * kSalvageShare + 1e-4f));
    CHECK(CountOf(fort, kSolidStone) == CountOf(s, kSolidStone));
    // Never more than went in, for every piece and tier.
    for (Recipe const& r : Recipes())
        for (uint8_t tier = 0; tier <= 3; ++tier)
        {
            std::vector<Material> spent = r.materials;
            if (Reinforceable(r.piece))
                for (Material const& m : ReinforcementSpent(tier))
                    spent.push_back(m);
            for (Material const& m : Salvage(r.piece, Reinforceable(r.piece) ? tier : 0))
                CHECK(m.count <= CountOf(spent, m.item));
        }
    // Nothing for a piece without a recipe.
    CHECK(Salvage(PieceType::SentryGun, 0).empty());
}

TEST(a_group_shares_the_salvage_item_by_item)
{
    std::vector<Material> const loot = {{kSolidStone, 5}, {kIronBracket, 1}};
    auto const one = SplitSalvage(loot, 1, 0);
    CHECK(one.size() == 1 && CountOf(one[0], kSolidStone) == 5 && CountOf(one[0], kIronBracket) == 1);
    auto const three = SplitSalvage(loot, 3, 1);
    CHECK(three.size() == 3);
    uint32_t stone = 0, brackets = 0;
    for (auto const& part : three)
    {
        stone += CountOf(part, kSolidStone);
        brackets += CountOf(part, kIronBracket);
        CHECK(CountOf(part, kSolidStone) >= 1 && CountOf(part, kSolidStone) <= 2);
    }
    CHECK(stone == 5 && brackets == 1);
    // The deal starts at `start`: the second member gets the first stone, the third the fifth, and
    // the bracket comes round to the first.
    CHECK(CountOf(three[1], kSolidStone) == 2 && CountOf(three[0], kIronBracket) == 1);
    CHECK(SplitSalvage(loot, 0, 0).empty());
}

TEST(taking_down_a_reinforced_piece_returns_part_of_its_reinforcement)
{
    std::vector<Material> const back = Refund(PieceType::StoneWall, 1.f, 2);
    CHECK(CountOf(back, kReinforcedPlate) == uint32_t(float(CountOf(ReinforcementSpent(2), kReinforcedPlate)) * kRefundShare));
    CHECK(CountOf(back, kSolidStone) == CountOf(Refund(PieceType::StoneWall, 1.f), kSolidStone));
}

TEST(the_wire_carries_a_pieces_reinforcement)
{
    Build::Piece p = Make(PieceType::StoneWall, 2);
    p.owner = 5;
    p.health = Build::MaxHealth(PieceType::StoneWall, 2) * 0.5f;
    std::vector<uint8_t> const bytes = Build::EncodePieces({p}, 5);
    // op, count, id, type, xyz yaw, health per mille, support, flags.
    size_t const at = 1 + 2 + 4 + 1 + 16;
    uint16_t const perMille = uint16_t(bytes[at] | bytes[at + 1] << 8);
    CHECK(perMille == 500);
    uint8_t const flags = bytes[at + 3];
    CHECK((flags & Build::kPieceMine) != 0);
    CHECK(((flags >> Build::kPieceReinforceShift) & 3) == 2);
}

TEST(the_reinforcement_table_round_trips)
{
    std::vector<uint8_t> const bytes = EncodeReinforcements();
    CHECK(bytes.size() > 2 && bytes[0] == kOpReinforcements);
    uint8_t request[] = {kOpReinforce, 7, 0, 0, 0};
    uint32_t id = 0;
    CHECK(DecodeReinforce(std::vector<uint8_t>(request, request + 5), id) && id == 7);
    CHECK(!DecodeReinforce(std::vector<uint8_t>(request, request + 4), id));
}
