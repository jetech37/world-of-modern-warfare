/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/CompanionKeeper.h"

#include <algorithm>
#include <cstring>
#include <set>

using namespace Fusion;
using namespace Fusion::Companions;
using namespace Fusion::Companions::Keeper;

namespace
{
    CompanionSpecies const& KeeperSpecies(uint16_t id)
    {
        CompanionSpecies const* s = FindSpecies(StarterSpecies(), id);
        static CompanionSpecies none;
        return s ? *s : none;
    }

    Customer Player(uint8_t level, uint32_t money)
    {
        Customer c;
        c.level = level;
        c.money = money;
        return c;
    }

    Customer Admin(uint8_t level = 1)
    {
        Customer c;
        c.level = level;
        c.gm = true;
        return c;
    }

    bool Has(std::vector<uint8_t> const& v, uint8_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }
}

TEST(keeper_prices_scale_with_level_and_stage)
{
    CompanionSpecies const& whelp = KeeperSpecies(1);   // Black Whelpling, three stages, never ridden
    CHECK(PurchasePrice(whelp, 0, 1) == 10000 + 2000);
    CHECK(PurchasePrice(whelp, 0, 30) == 10000 + 30 * 2000);
    CHECK(PurchasePrice(whelp, 2, 60) == 10000 + 60 * 3 * 2000);     // 37 gold
    CHECK(PurchasePrice(whelp, 1, 40) > PurchasePrice(whelp, 0, 40));
    CHECK(PurchasePrice(whelp, 0, 41) > PurchasePrice(whelp, 0, 40));
    // A rideable form never undercuts the vendors' companion mounts.
    CompanionSpecies const& wolf = KeeperSpecies(6);    // Wolf Pup, ridden at its third stage
    CHECK(wolf.stages[2].rideable);
    CHECK(PurchasePrice(wolf, 2, 40) == kMountFloor);
    CHECK(PurchasePrice(wolf, 1, 40) < kMountFloor);
}

TEST(keeper_change_price_is_the_difference_plus_a_fee)
{
    CompanionSpecies const& whelp = KeeperSpecies(1);
    CompanionState s;
    s.species = 1;
    s.stage = 0;
    s.level = 20;
    CHECK(ChangePrice(whelp, s, 0, 30) == kServiceFee + PurchasePrice(whelp, 0, 30) - PurchasePrice(whelp, 0, 20));
    CHECK(ChangePrice(whelp, s, 1, 20) == kServiceFee + PurchasePrice(whelp, 1, 20) - PurchasePrice(whelp, 0, 20));
    // Down costs only the fee.
    CHECK(ChangePrice(whelp, s, 0, 10) == kServiceFee);
    // Buying low and raising it is never cheaper than buying it high.
    CHECK(PurchasePrice(whelp, 0, 20) + ChangePrice(whelp, s, 2, 60) >= PurchasePrice(whelp, 2, 60));
    // Ability choices: the first is free, a change is the retraining fee.
    CHECK(PickPrice(s, 0) == 0);
    s.picks[0] = 1;
    CHECK(PickPrice(s, 0) == kRepickFee);
}

TEST(keeper_levels_and_stages_offered)
{
    CompanionSpecies const& whelp = KeeperSpecies(1);   // evolves at 20 and 45
    CHECK(MinLevelForStage(whelp, 0) == 1);
    CHECK(MinLevelForStage(whelp, 1) == 20);
    CHECK(MinLevelForStage(whelp, 2) == 45);
    CHECK(LevelCap(Player(37, 0)) == 37);
    CHECK(LevelCap(Admin(1)) == kMaxLevel);

    // A level 37 player: stages 1 and 2, levels up to 37.
    std::vector<uint8_t> stages = StagesFor(whelp, Player(37, 0));
    CHECK(stages.size() == 2 && stages[0] == 0 && stages[1] == 1);
    std::vector<uint8_t> levels = LevelPresets(whelp, 0, Player(37, 0));
    CHECK(Has(levels, 1) && Has(levels, 10) && Has(levels, 30) && Has(levels, 37));
    CHECK(!Has(levels, 40) && !Has(levels, 60));
    CHECK(std::is_sorted(levels.begin(), levels.end()));
    levels = LevelPresets(whelp, 1, Player(37, 0));
    CHECK(levels.front() == 20 && levels.back() == 37 && !Has(levels, 10));
    CHECK(LevelPresets(whelp, 2, Player(37, 0)).empty());

    // An admin: every stage, every preset, at any level of their own.
    stages = StagesFor(whelp, Admin(1));
    CHECK(stages.size() == 3);
    levels = LevelPresets(whelp, 2, Admin(1));
    CHECK(Has(levels, 1) && Has(levels, 45) && Has(levels, 60));
    CHECK(levels.size() <= kPerPage);
}

TEST(keeper_buying_checks)
{
    CompanionSpecies const& whelp = KeeperSpecies(1);
    uint32_t const price = PurchasePrice(whelp, 1, 30);
    // Players never buy one (they drop and are bred), whatever they can pay.
    CHECK(!kPlayersBuy);
    CHECK(CanBuy(whelp, 1, 30, Player(40, price)) == Refusal::GmOnly);
    CHECK(CanBuy(whelp, 0, 1, Player(60, 10000000)) == Refusal::GmOnly);
    CHECK(CanBuy(whelp, 3, 30, Admin()) == Refusal::BadStage);
    CHECK(CanBuy(whelp, 0, 0, Admin()) == Refusal::BadLevel);
    CHECK(CanBuy(whelp, 0, 61, Admin()) == Refusal::BadLevel);
    // An admin buys anything free, above their own level and below a stage's.
    CHECK(CanBuy(whelp, 2, 60, Admin(1)) == Refusal::None);
    CHECK(CanBuy(whelp, 2, 5, Admin(1)) == Refusal::None);
}

TEST(keeper_changing_level_and_stage)
{
    CompanionSpecies const& whelp = KeeperSpecies(1);
    CompanionState s;
    s.species = 1;
    s.stage = 1;
    s.level = 30;
    s.xp = 1234;
    s.evolveKills = 7;
    s.picks = {1, 2, 0};
    CHECK(CanChange(whelp, s, 1, 30, Admin()) == Refusal::Unchanged);
    // A player changes neither: an admin's tool only.
    CHECK(CanChange(whelp, s, 1, 35, Player(35, 10000000)) == Refusal::GmOnly);
    CHECK(CanChange(whelp, s, 0, 10, Player(35, 10000000)) == Refusal::GmOnly);
    CHECK(CanChange(whelp, s, 2, 5, Admin()) == Refusal::None);
    CHECK(CanChange(whelp, s, 3, 30, Admin()) == Refusal::BadStage);

    CompanionState up = s;
    ApplyChange(whelp, up, 1, 45);
    CHECK(up.level == 45 && up.stage == 1 && up.xp == 0 && up.evolveKills == 7);
    ApplyChange(whelp, up, 2, 45);
    CHECK(up.stage == 2 && up.evolveKills == 0 && up.picks[1] == 2);
    CHECK(PendingPick(whelp, up) == 2);
    // Down a stage: the stages above forget their choices.
    ApplyChange(whelp, up, 0, 45);
    CHECK(up.stage == 0 && up.picks[0] == 1 && up.picks[1] == 0 && up.picks[2] == 0);
}

TEST(keeper_ability_choices)
{
    CompanionSpecies const& whelp = KeeperSpecies(1);
    CompanionState s;
    s.species = 1;
    s.stage = 1;
    s.level = 30;
    CHECK(CanPick(whelp, s, 0, 2, Player(30, 0)) == Refusal::None);         // the first is free
    CHECK(CanPick(whelp, s, 2, 1, Admin()) == Refusal::NotReached);
    CHECK(CanPick(whelp, s, 3, 1, Admin()) == Refusal::BadStage);
    CHECK(CanPick(whelp, s, 0, 3, Admin()) == Refusal::NoAbility);
    CHECK(ApplyPick(whelp, s, 0, 2));
    CHECK(s.picks[0] == 2);
    CHECK(CanPick(whelp, s, 0, 2, Admin()) == Refusal::Unchanged);
    CHECK(CanPick(whelp, s, 0, 1, Player(30, kRepickFee - 1)) == Refusal::NoMoney);
    CHECK(CanPick(whelp, s, 0, 1, Player(30, kRepickFee)) == Refusal::None);
    // Unlike Pick, the keeper changes a choice already made.
    CHECK(ApplyPick(whelp, s, 0, 1));
    CHECK(s.picks[0] == 1);
    CHECK(!ApplyPick(whelp, s, 2, 1));
}

TEST(keeper_trait_edits_follow_the_trait_rules)
{
    CompanionSpecies const& whelp = KeeperSpecies(1);
    CompanionSpecies const& wolf = KeeperSpecies(6);
    CompanionState s;
    s.species = 1;
    s.stage = 1;
    CHECK(CanToggleTrait(whelp, s, Trait::Swift, Player(60, 100000000)) == Refusal::GmOnly);
    CHECK(CanToggleTrait(whelp, s, Trait::Swift, Admin()) == Refusal::None);
    ApplyToggleTrait(whelp, s, Trait::Swift);
    CHECK(HasTrait(s.traits, Trait::Swift));
    CHECK(s.rarity == uint8_t(Rarity::Uncommon));
    // Exclusions: one body size.
    ApplyToggleTrait(whelp, s, Trait::Giant);
    CHECK(CanToggleTrait(whelp, s, Trait::Runt, Admin()) == Refusal::Conflict);
    // Mount traits only on a rideable line, Gunner only on humanoids.
    CHECK(CanToggleTrait(whelp, s, Trait::Surefooted, Admin()) == Refusal::Ineligible);
    CHECK(CanToggleTrait(whelp, s, Trait::Gunner, Admin()) == Refusal::Ineligible);
    CompanionState w;
    w.species = 6;
    CHECK(CanToggleTrait(wolf, w, Trait::Surefooted, Admin()) == Refusal::None);
    // Legendary traits are allowed; Twin-souled doubles the current stage.
    ApplyToggleTrait(whelp, s, Trait::TwinSouled);
    CHECK(TraitList(s.traits).size() == 3);
    CHECK(s.rarity == uint8_t(Rarity::Legendary));
    CHECK(s.twinStage == 2);
    // Three is the cap; removing always works.
    CHECK(CanToggleTrait(whelp, s, Trait::Ironhide, Admin()) == Refusal::TooMany);
    CHECK(AddableTraits(whelp, s).empty());
    CHECK(CanToggleTrait(whelp, s, Trait::TwinSouled, Admin()) == Refusal::None);
    ApplyToggleTrait(whelp, s, Trait::TwinSouled);
    CHECK(s.twinStage == 0 && !HasTrait(s.traits, Trait::TwinSouled));
    CHECK(s.rarity == uint8_t(Rarity::Rare));
    // With room again: no conflicts, no duplicates, only eligible ones on offer, and a second
    // legendary is not.
    ApplyToggleTrait(whelp, s, Trait::Alpha);
    ApplyToggleTrait(whelp, s, Trait::Swift);
    CHECK(TraitList(s.traits).size() == 2 && HasTrait(s.traits, Trait::Giant));
    std::vector<Trait> const add = AddableTraits(whelp, s);
    CHECK(!add.empty());
    for (Trait t : add)
    {
        CHECK(!HasTrait(s.traits, t));
        CHECK(t != Trait::Runt && t != Trait::Surefooted && t != Trait::Gunner);
        CHECK(TraitData(t).kind != TraitKind::Legendary);
        CHECK(ValidTraits(s.traits | TraitBit(t), whelp));
    }
    // A Twin-souled stage.
    CompanionState t;
    t.species = 1;
    t.stage = 2;
    CHECK(CanSetTwinStage(whelp, t, 1, Admin()) == Refusal::NotTwin);
    ApplyToggleTrait(whelp, t, Trait::TwinSouled);
    CHECK(t.twinStage == 3);
    CHECK(CanSetTwinStage(whelp, t, 1, Admin()) == Refusal::None);
    CHECK(CanSetTwinStage(whelp, t, 3, Admin()) == Refusal::Unchanged);
    CHECK(CanSetTwinStage(whelp, t, 4, Admin()) == Refusal::BadStage);
    CHECK(CanSetTwinStage(whelp, t, 1, Player(60, 100000000)) == Refusal::GmOnly);
}

TEST(keeper_browsing_and_pages)
{
    std::vector<CompanionSpecies> const& all = StarterSpecies();
    std::vector<Family> const fams = Families(all);
    std::set<Family> seen(fams.begin(), fams.end());
    CHECK(seen.size() == fams.size());
    for (CompanionSpecies const& sp : all)
        CHECK(seen.count(sp.family) == 1);
    for (size_t i = 1; i < fams.size(); ++i)
        CHECK(std::strcmp(FamilyName(fams[i - 1]), FamilyName(fams[i])) < 0);
    for (uint8_t f = 0; f <= uint8_t(kLastFamily); ++f)
        CHECK(std::strlen(FamilyName(Family(f))) > 0);
    std::vector<uint16_t> const dragons = SpeciesOf(all, Family::Dragon);
    CHECK(dragons.size() >= 4 && dragons.front() == 1);
    CHECK(std::is_sorted(dragons.begin(), dragons.end()));

    PageSpan p = Page(30, 0);
    CHECK(p.first == 0 && p.end == kPerPage && !p.prev && p.next);
    p = Page(30, 2);
    CHECK(p.first == 24 && p.end == 30 && p.prev && !p.next);
    p = Page(30, 9);        // past the end: the last page
    CHECK(p.page == 2 && p.first == 24);
    p = Page(0, 0);
    CHECK(p.first == 0 && p.end == 0 && !p.prev && !p.next);
    p = Page(kPerPage, 0);
    CHECK(p.end == kPerPage && !p.next);

    uint32_t const a = Action(7, 0x1234, 0x56);
    CHECK(ActionOp(a) == 7 && ActionA(a) == 0x1234 && ActionB(a) == 0x56);
    CHECK(IsKeeper(92700) && IsKeeper(92799) && !IsKeeper(92500));
}
