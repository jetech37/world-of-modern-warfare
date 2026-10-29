/*
 * Azeroth Warfare: tests for the action systems' bars and op (ActionSystems.h). GPL-2.0-or-later,
 * see Common.h.
 */

#include "Test.h"
#include "fusion/ActionSystems.h"
#include "fusion/DruidForms.h"

using namespace Fusion::ActionSystems;

namespace
{
    constexpr uint8_t kSpell = 0x00;
    constexpr uint8_t kMacro = 0x40;
    constexpr uint8_t kItem = 0x80;
}

TEST(action_system_bars_are_six_slot_pages_apart_from_the_form_pages)
{
    for (System s : {System::Modern, System::Classic, System::Utility})
    {
        Range const r = RangeOf(s);
        CHECK((r.last - r.first + 1) % kSlotsPerPage == 0);
        CHECK(r.last < kButtons);
        CHECK(SystemOfButton(r.first) == s);
        CHECK(SystemOfButton(r.last) == s);
    }
    // The kit's first page (FusionClasses::LayOutBar, buttons 0..11) is Classic's.
    CHECK(SystemOfButton(0) == System::Classic);
    CHECK(SystemOfButton(11) == System::Classic);
    // Every druid form's page is no system's; Modern's are past the last form's.
    namespace DF = Fusion::DruidForms;
    for (DF::Form const& f : DF::Forms())
    {
        uint16_t const first = DF::FirstSlotOfBar(f.bonusBar);
        CHECK(!SystemOfButton(first));
        CHECK(!SystemOfButton(uint16_t(first + DF::kSlotsPerPage - 1)));
    }
    CHECK(DF::kActionButtons + 12 == kButtons && kCompanionBar.first == DF::kActionButtons && kCompanionBar.last + 1 == kButtons);
    CHECK(RangeOf(System::Modern).first == DF::FirstSlotOfBar(11));
}

TEST(action_system_abilities_go_only_on_their_own_bars)
{
    uint32_t const gun = uint32_t(Ability::Primary);
    uint32_t const fists = uint32_t(Ability::BareHands);
    CHECK(ButtonAllows(192, gun, kFusionButtonType));
    CHECK(!ButtonAllows(0, gun, kFusionButtonType));
    CHECK(!ButtonAllows(60, gun, kFusionButtonType));
    CHECK(!ButtonAllows(80, gun, kFusionButtonType));
    CHECK(ButtonAllows(60, fists, kFusionButtonType));
    CHECK(ButtonAllows(5, fists, kFusionButtonType));
    CHECK(!ButtonAllows(200, fists, kFusionButtonType));
    CHECK(!ButtonAllows(192, 100, kFusionButtonType));
    // WoW's own entries: Classic's bar and the form pages only.
    CHECK(ButtonAllows(0, 133, kSpell));
    CHECK(ButtonAllows(59, 6948, kItem));
    CHECK(ButtonAllows(80, 133, kSpell));
    CHECK(!ButtonAllows(192, 133, kSpell));
    CHECK(!ButtonAllows(70, 1, kMacro));
    CHECK(!ButtonAllows(216, 133, kSpell));
}

TEST(action_system_op_is_the_clients)
{
    // The client's bytes (client/src/action_systems.rs, OP_SYSTEM).
    CHECK(EncodeSystem(System::Utility) == (std::vector<uint8_t>{1, 2}));
    CHECK(DecodeSystem({1, 0}) == System::Modern);
    CHECK(DecodeSystem({1, 1}) == System::Classic);
    CHECK(DecodeSystem({1, 2}) == System::Utility);
    CHECK(!DecodeSystem({1, 3}));
    CHECK(!DecodeSystem({2, 0}));
    CHECK(!DecodeSystem({1}));
    CHECK(Unarmed(System::Utility) && !Unarmed(System::Classic) && !Unarmed(System::Modern));
}

TEST(action_system_pose_and_acts_relay_byte_for_byte)
{
    // Ops 5-8 (after the tools' 2-4). The client's bytes (client/src/body_state.rs, encode_pose): Classic, ADS and a spell in the
    // left hand, crouched, 20 degrees down, gun 0x0102, a sword and a shield, no tool.
    std::vector<uint8_t> const body{5, 1, kPoseAds | kPoseCastLeft, 1, uint8_t(int8_t(-20)), 0x02, 0x01, 7, 6, 0};
    std::optional<Pose> const p = DecodePose(body);
    CHECK(p.has_value());
    CHECK(p->system == 1 && p->stance == 1 && p->pitch == -20 && p->gun == 0x0102);
    CHECK(p->flags == (kPoseAds | kPoseCastLeft) && p->mainSubclass == 7 && p->offSubclass == 6);
    CHECK(EncodePose(*p) == body);
    std::vector<uint8_t> const relay = EncodePoseOf(0x0102030405060708ull, *p);
    CHECK(relay.size() == 18);
    CHECK(relay[0] == kOpPoseOf && relay[1] == 0x08 && relay[8] == 0x01);
    CHECK(std::vector<uint8_t>(relay.begin() + 9, relay.end()) == std::vector<uint8_t>(body.begin() + 1, body.end()));
    // Malformed: short, a fourth system, a fifth stance, past straight up.
    CHECK(!DecodePose({5, 1, 0}));
    CHECK(!DecodePose({5, 3, 0, 0, 0, 0, 0, 255, 255, 0}));
    CHECK(!DecodePose({5, 0, 0, 4, 0, 0, 0, 255, 255, 0}));
    CHECK(!DecodePose({5, 0, 0, 0, 91, 0, 0, 255, 255, 0}));

    std::optional<Act> const a = DecodeAct({6, 2, 1});
    CHECK(a && a->kind == 2 && a->arg == 1);
    CHECK(!DecodeAct({6, 0, 0}));
    CHECK(!DecodeAct({6, kActLast + 1, 0}));
    CHECK(!DecodeAct({6, 1}));
    CHECK(EncodeActOf(5, *a) == (std::vector<uint8_t>{8, 5, 0, 0, 0, 0, 0, 0, 0, 2, 1}));
}

TEST(action_system_tools_go_on_utility_alone)
{
    for (uint32_t t = kFirstTool; t <= kLastTool; ++t)
    {
        CHECK(IsTool(t) && KnownAbility(t));
        CHECK(AbilityOn(t, System::Utility));
        CHECK(!AbilityOn(t, System::Classic));
        CHECK(!AbilityOn(t, System::Modern));
        CHECK(ButtonAllows(60, t, kFusionButtonType));
        CHECK(ButtonAllows(71, t, kFusionButtonType));
        CHECK(!ButtonAllows(0, t, kFusionButtonType));
        CHECK(!ButtonAllows(59, t, kFusionButtonType));
        CHECK(!ButtonAllows(192, t, kFusionButtonType));
        CHECK(!ButtonAllows(80, t, kFusionButtonType));
    }
    CHECK(!IsTool(uint32_t(Ability::BareHands)));
    CHECK(!IsTool(kLastTool + 1));
    CHECK(!KnownAbility(kLastAbility + 1));
    // Bare hands stay Classic's and Utility's.
    CHECK(AbilityOn(uint32_t(Ability::BareHands), System::Classic));
    CHECK(AbilityOn(uint32_t(Ability::BareHands), System::Utility));
}

TEST(action_system_tool_ops_round_trip)
{
    CHECK(EncodeTool(uint32_t(Ability::MiningPick)) == (std::vector<uint8_t>{2, 10}));
    CHECK(DecodeTool({2, 10}) == uint32_t(Ability::MiningPick));
    CHECK(DecodeTool({2, 0}) == 0u);
    CHECK(!DecodeTool({2, 9}));
    CHECK(!DecodeTool({2, 23}));
    CHECK(!DecodeTool({2}));
    StrikeRequest r;
    r.tool = uint32_t(Ability::RepairHammer);
    r.target = 0x0102030405060708ull;
    std::vector<uint8_t> const b = EncodeStrike(r);
    CHECK(b.size() == 10 && b[0] == kOpStrike && b[1] == 16 && b[2] == 0x08 && b[9] == 0x01);
    std::optional<StrikeRequest> const back = DecodeStrike(b);
    CHECK(back && back->tool == r.tool && back->target == r.target);
    CHECK(!DecodeStrike({3, 9, 0, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(!DecodeStrike({3, 10, 0}));
    CHECK(EncodeStruck(10, 1, 2, 3) == (std::vector<uint8_t>{4, 10, 1, 2, 3}));
}

TEST(build_structure_goes_on_utility_alone_and_is_no_tool)
{
    using namespace Fusion::ActionSystems;
    uint32_t const b = uint32_t(Ability::BuildStructure);
    CHECK(KnownAbility(b));
    CHECK(AbilityOn(b, System::Utility));
    CHECK(!AbilityOn(b, System::Modern) && !AbilityOn(b, System::Classic));
    CHECK(!IsTool(b) && ToolBit(b) == 0);
    CHECK(!KnownAbility(kLastAbility + 1));
    CHECK(ButtonAllows(60, b, kFusionButtonType));
    CHECK(!ButtonAllows(0, b, kFusionButtonType));
}

TEST(companion_slots_go_on_the_companion_bar_alone)
{
    using namespace Fusion::ActionSystems;
    uint32_t const first = uint32_t(Ability::CompanionSlot1);
    uint32_t const last = uint32_t(Ability::CompanionSlot10);
    CHECK(last - first == 9 && KnownAbility(first) && KnownAbility(last));
    for (uint32_t id = first; id <= last; ++id)
    {
        CHECK(IsCompanionSlot(id));
        CHECK(!AbilityOn(id, System::Modern) && !AbilityOn(id, System::Classic) && !AbilityOn(id, System::Utility));
        CHECK(ButtonAllows(216, id, kFusionButtonType) && ButtonAllows(227, id, kFusionButtonType));
        CHECK(!ButtonAllows(180, id, kFusionButtonType) && !ButtonAllows(0, id, kFusionButtonType));
        CHECK(!ButtonAllows(228, id, kFusionButtonType));
        CHECK(!ButtonAllows(60, id, kFusionButtonType));
    }
    // Nothing else goes on the Companion bar: no gun, no spell.
    CHECK(!ButtonAllows(220, uint32_t(Ability::Primary), kFusionButtonType));
    CHECK(!ButtonAllows(220, 133, 0));
    CHECK(!IsCompanionSlot(uint32_t(Ability::BuildStructure)));
    // The bar is no system of the hands.
    CHECK(!SystemOfButton(220));
}
