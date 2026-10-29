/*
 * Azeroth Warfare: tests for Utility's tools (Gathering.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Gathering.h"

using namespace Fusion::Gathering;

TEST(gathering_every_tool_has_its_trade_and_item)
{
    using A = Ability;
    for (uint32_t t = Fusion::ActionSystems::kFirstTool; t <= Fusion::ActionSystems::kLastTool; ++t)
        CHECK(ToolInfo(t) != nullptr);
    CHECK(!ToolInfo(uint32_t(A::BareHands)));
    CHECK(ToolInfo(uint32_t(A::MiningPick))->item == 2901 && ToolInfo(uint32_t(A::MiningPick))->skill == 186);
    CHECK(ToolInfo(uint32_t(A::SkinningKnife))->item == 7005 && ToolInfo(uint32_t(A::SkinningKnife))->skill == 393);
    CHECK(ToolInfo(uint32_t(A::Herbalism))->item == 0 && ToolInfo(uint32_t(A::Herbalism))->skill == 182);
    CHECK(ToolInfo(uint32_t(A::BlacksmithHammer))->item == 5956);
    CHECK(ToolInfo(uint32_t(A::WoodcuttingAxe))->item == kWoodcuttingAxe);
    CHECK(ToolInfo(uint32_t(A::RepairHammer))->item == kRepairHammer);
    // Each works its own thing alone.
    CHECK(Fits(uint32_t(A::MiningPick), Work::Vein));
    CHECK(!Fits(uint32_t(A::MiningPick), Work::Herb));
    CHECK(Fits(uint32_t(A::Herbalism), Work::Herb));
    CHECK(Fits(uint32_t(A::SkinningKnife), Work::Corpse));
    CHECK(!Fits(uint32_t(A::SkinningKnife), Work::Vein));
    CHECK(Fits(uint32_t(A::FishingPole), Work::Water));
    CHECK(Fits(uint32_t(A::WoodcuttingAxe), Work::Lumber));
    CHECK(!Fits(uint32_t(A::WoodcuttingAxe), Work::Vein));
    CHECK(Fits(uint32_t(A::BlacksmithHammer), Work::Anvil));
    CHECK(Fits(uint32_t(A::RepairHammer), Work::Piece));
    CHECK(!Fits(uint32_t(A::RepairHammer), Work::None));
    CHECK(!Fits(uint32_t(A::BareHands), Work::Herb));
}

TEST(gathering_tools_ask_their_item_and_trade)
{
    ToolDef const& pick = *ToolInfo(uint32_t(Ability::MiningPick));
    CHECK(CheckTool(pick, {true, true, false}) == Outcome::Struck);
    CHECK(CheckTool(pick, {false, true, false}) == Outcome::NoTool);
    CHECK(CheckTool(pick, {true, false, false}) == Outcome::NotLearned);
    CHECK(CheckTool(pick, {false, false, true}) == Outcome::Struck);     // a game master
    // Herbalism needs no tool, only the trade.
    ToolDef const& herbs = *ToolInfo(uint32_t(Ability::Herbalism));
    CHECK(CheckTool(herbs, {false, true, false}) == Outcome::Struck);
    CHECK(CheckTool(herbs, {false, false, false}) == Outcome::NotLearned);
    // Any pole: the holder says whether one is there.
    ToolDef const& pole = *ToolInfo(uint32_t(Ability::FishingPole));
    CHECK(CheckTool(pole, {false, true, false}) == Outcome::NoTool);
    CHECK(CheckTool(pole, {true, true, false}) == Outcome::Struck);
    // The Repair Hammer asks the hammer and no trade.
    ToolDef const& hammer = *ToolInfo(uint32_t(Ability::RepairHammer));
    CHECK(CheckTool(hammer, {true, false, false}) == Outcome::Struck);
    CHECK(CheckTool(hammer, {false, false, false}) == Outcome::NoTool);
}

TEST(utility_bar_shows_only_the_tools_had_and_trained)
{
    namespace AS = Fusion::ActionSystems;
    using A = Ability;
    // Nothing carried, nothing learned: no tool (bare hands are the client's, always there).
    CHECK(UtilityBarTools([](ToolDef const&) { return Holder{}; }) == 0);
    // A game master's pass does not put tools on the bar.
    CHECK(UtilityBarTools([](ToolDef const&) { return Holder{false, false, true}; }) == 0);
    // Everything carried and learned: every tool.
    CHECK(UtilityBarTools([](ToolDef const&) { return Holder{true, true, false}; }) == AS::kAllTools);
    // A pick in the bags without Mining, Mining without a pick: neither shows.
    auto only = [](A tool, Holder h) {
        return UtilityBarTools([&](ToolDef const& t) { return t.ability == tool ? h : Holder{}; });
    };
    CHECK(only(A::MiningPick, {true, false, false}) == 0);
    CHECK(only(A::MiningPick, {false, true, false}) == 0);
    CHECK(only(A::MiningPick, {true, true, false}) == AS::ToolBit(uint32_t(A::MiningPick)));
    // Herbalism: the trade alone. The Repair Hammer: owning it alone.
    CHECK(only(A::Herbalism, {false, true, false}) == AS::ToolBit(uint32_t(A::Herbalism)));
    CHECK(only(A::RepairHammer, {true, false, false}) == AS::ToolBit(uint32_t(A::RepairHammer)));
    CHECK(only(A::RepairHammer, {false, true, false}) == 0);
    // The op: [9, mask u16 LE], nothing past the tools.
    uint16_t const mask = AS::ToolBit(uint32_t(A::MiningPick)) | AS::ToolBit(uint32_t(A::RepairHammer));
    std::vector<uint8_t> const body = AS::EncodeTools(mask);
    CHECK(body == (std::vector<uint8_t>{AS::kOpTools, 0x41, 0x00}));
    CHECK(AS::DecodeTools(body) == mask);
    CHECK(!AS::DecodeTools({AS::kOpTools, 0x00, 0x20}));
    CHECK(!AS::DecodeTools({AS::kOpTools, 0x01}));
    CHECK(AS::ToolBit(uint32_t(A::BareHands)) == 0);
}

TEST(gathering_swings_by_skill)
{
    uint32_t const pick = uint32_t(Ability::MiningPick);
    // A copper vein (Mining 1): orange to a new miner, 4 swings; 3 once yellow; 2 once far below.
    CHECK(StrikesFor(pick, 1, 1) == 4);
    CHECK(StrikesFor(pick, 1, 25) == 4);
    CHECK(StrikesFor(pick, 1, 26) == 3);
    CHECK(StrikesFor(pick, 1, 75) == 3);
    CHECK(StrikesFor(pick, 1, 76) == 2);
    CHECK(StrikesFor(pick, 230, 300) == 3);
    // Never more than 4 nor fewer than 2 for a vein or a tree.
    for (uint16_t s = 0; s <= 300; s += 5)
    {
        uint8_t const n = StrikesFor(pick, 100, s);
        CHECK(n >= 2 && n <= 4);
        CHECK(StrikesFor(uint32_t(Ability::WoodcuttingAxe), 100, s) == n);
        uint8_t const h = StrikesFor(uint32_t(Ability::Herbalism), 100, s);
        CHECK(h >= 1 && h <= 3);
    }
    CHECK(StrikesFor(uint32_t(Ability::SkinningKnife), 0, 300) == 1);
    CHECK(StrikesFor(uint32_t(Ability::FishingPole), 0, 1) == 1);
    CHECK(StrikesFor(uint32_t(Ability::RepairHammer), 0, 0) == 1);
    // Vanilla's skinning requirement.
    CHECK(SkinningRequired(5, 1) == 0);
    CHECK(SkinningRequired(20, 50) == 100);
    CHECK(SkinningRequired(40, 150) == 200);
    CHECK(SkinningRequired(60, 300) == 300);
}

TEST(gathering_strikes_count_on_one_target)
{
    uint32_t const pick = uint32_t(Ability::MiningPick);
    Progress p;
    uint32_t t = 10000;
    CHECK(Strike(p, pick, 7, 3, t) == Step::Counted);
    CHECK(p.done == 1);
    // Too fast: not counted.
    CHECK(Strike(p, pick, 7, 3, t + MinGapMs(pick) - 1) == Step::TooSoon);
    CHECK(p.done == 1);
    t += 900;
    CHECK(Strike(p, pick, 7, 3, t) == Step::Counted);
    t += 900;
    CHECK(Strike(p, pick, 7, 3, t) == Step::Complete);
    CHECK(p.done == 0);
    // Another target starts over.
    t += 900;
    CHECK(Strike(p, pick, 7, 3, t) == Step::Counted);
    t += 900;
    CHECK(Strike(p, pick, 8, 3, t) == Step::Counted);
    CHECK(p.done == 1 && p.target == 8);
    // A long pause starts over too.
    t += kStrikeWindowMs + 1;
    CHECK(Strike(p, pick, 8, 3, t) == Step::Counted);
    CHECK(p.done == 1);
    // One swing needed: done at once.
    Progress q;
    CHECK(Strike(q, uint32_t(Ability::FishingPole), 0, 1, 500) == Step::Complete);
    // The timer wrapping (WoW's ms clock) is no stall.
    Progress w;
    CHECK(Strike(w, pick, 1, 4, 0xFFFFFF00u) == Step::Counted);
    CHECK(Strike(w, pick, 1, 4, 0xFFFFFF00u + 1000u) == Step::Counted);
    CHECK(w.done == 2);
}
