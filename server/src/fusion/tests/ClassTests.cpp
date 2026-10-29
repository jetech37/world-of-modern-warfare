/*
 * World of Modern Warfare: the class framework's rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Classes.h"

#include <map>
#include <set>

using namespace Fusion;
using namespace Fusion::Classes;

namespace
{
    std::array<uint8_t, 9> const kClasses = {kWarrior, kPaladin, kHunter, kRogue, kPriest, kShaman, kMage, kWarlock, kDruid};

    std::set<uint16_t> AllOf(uint8_t cls)
    {
        std::set<uint16_t> s;
        for (Option const* o : OptionsOf(cls))
            s.insert(o->id);
        return s;
    }
}

TEST(class_catalog_has_three_core_and_eighteen_options_per_class)
{
    CHECK(Catalog().size() == 9 * 21);
    for (uint8_t cls : kClasses)
    {
        CHECK(IsPlayableClass(cls));
        CHECK(OptionsOf(cls).size() == 18);
        for (Option const* o : CoreOf(cls))
            CHECK(o && o->tier == kCoreTier && o->archetype == kCoreArchetype && o->spells[0]);
        for (uint8_t t = 1; t <= kTiers; ++t)
        {
            auto tier = TierOptions(cls, t);
            for (uint8_t a = 0; a < kArchetypes; ++a)
            {
                CHECK(tier[a] && tier[a]->tier == t && tier[a]->archetype == a && tier[a]->cls == cls);
                CHECK(tier[a] && tier[a]->spells[0] != 0);
                // Capstones are tier 6 only.
                CHECK(tier[a] && tier[a]->capstone == (t == kTiers));
            }
        }
    }
    CHECK(!IsPlayableClass(6));     // no death knight in 1.12
    CHECK(!IsPlayableClass(10));
}

TEST(class_option_ids_read_as_class_tier_archetype)
{
    CHECK(OptionId(kWarrior, 1, 0) == 111);
    CHECK(OptionId(kDruid, 6, 2) == 1163);
    CHECK(OptionId(kMage, 0, 0) == 801);
    Option const* o = FindOption(111);
    CHECK(o && std::string(o->name) == "Bloodthirst" && o->spells[0] == 23894);
    o = FindOption(1163);
    CHECK(o && std::string(o->name) == "Starfall");
    CHECK(FindOption(0) == nullptr);
    CHECK(FindOption(171) == nullptr);   // tier 7
    std::set<uint16_t> ids;
    for (Option const& x : Catalog())
        CHECK(ids.insert(x.id).second);
}

TEST(class_options_never_share_a_spell_inside_a_class)
{
    // One spell per button: a press must name one option. Forms may share their abilities (the
    // stag's and the worgen's charge are one spell, told apart by the form).
    for (uint8_t cls : kClasses)
    {
        std::map<uint32_t, uint16_t> owner;
        for (Option const& o : Catalog())
        {
            // A custom passive's spell is only its icon on the talent page.
            if (o.cls != cls || !LearnsSpells(o))
                continue;
            uint32_t const s = o.spells[0];
            auto it = owner.find(s);
            CHECK(it == owner.end());
            owner[s] = o.id;
        }
        // Utility spells are not options' spells either.
        for (uint32_t u : UtilityOf(cls))
            CHECK(owner.find(u) == owner.end() || FindOption(owner[u])->name == std::string("Rekindle"));
    }
}

TEST(class_trainer_prices_rise_by_tier)
{
    CHECK(TierLevel(0) == 1);
    CHECK(TierLevel(1) == 10 && TierLevel(6) == 60);
    CHECK(TierPrice(1) == 10000 && TierPrice(2) == 50000 && TierPrice(3) == 120000);
    CHECK(TierPrice(4) == 220000 && TierPrice(5) == 350000 && TierPrice(6) == 600000);
    CHECK(PureBuildPrice() == 1350000);     // 135 gold, as the catalog says
    CHECK(TierPrice(0) == 0);
}

TEST(class_options_are_bought_once_at_their_level)
{
    std::set<uint16_t> learned;
    CHECK(CanBuy(kMage, 10, 10000, learned, 811) == BuyError::None);
    CHECK(CanBuy(kMage, 9, 10000, learned, 811) == BuyError::TooLow);
    CHECK(CanBuy(kMage, 10, 9999, learned, 811) == BuyError::NoMoney);
    CHECK(CanBuy(kWarrior, 10, 10000, learned, 811) == BuyError::WrongClass);
    CHECK(CanBuy(kMage, 60, 1000000, learned, 801) == BuyError::Core);
    CHECK(CanBuy(kMage, 60, 1000000, learned, 999) == BuyError::UnknownOption);
    learned.insert(811);
    CHECK(CanBuy(kMage, 10, 10000, learned, 811) == BuyError::AlreadyLearned);
    CHECK(CanBuy(kMage, 59, 600000, learned, 861) == BuyError::TooLow);
    CHECK(CanBuy(kMage, 60, 600000, learned, 861) == BuyError::None);
}

TEST(class_picks_need_the_option_learned_its_tier_and_its_level)
{
    std::set<uint16_t> learned = {111, 122, 133};
    Picks p{111, 122, 133, 0, 0, 0};
    uint8_t bad = 0;
    CHECK(ValidatePicks(kWarrior, 30, learned, p, &bad) == PickError::None);
    CHECK(ValidatePicks(kWarrior, 29, learned, p, &bad) == PickError::TierLocked && bad == 3);
    p[3] = 141;
    CHECK(ValidatePicks(kWarrior, 40, learned, p, &bad) == PickError::NotLearned && bad == 4);
    CHECK(ValidatePicks(kWarrior, 40, learned, p, &bad, true) == PickError::None);    // a GM
    p[3] = 0;
    p[0] = 122;     // a tier 2 option in tier 1
    CHECK(ValidatePicks(kWarrior, 60, learned, p, &bad) == PickError::WrongTier && bad == 1);
    p[0] = 811;
    CHECK(ValidatePicks(kWarrior, 60, learned, p, &bad, true) == PickError::WrongClass);
    p[0] = 101;     // a core ability is no pick
    CHECK(ValidatePicks(kWarrior, 60, learned, p, &bad, true) == PickError::WrongTier);
    p[0] = 5000;
    CHECK(ValidatePicks(kWarrior, 60, learned, p, &bad) == PickError::UnknownOption);
    // Sanitized drops only the bad picks.
    Picks s = Sanitized(kWarrior, 25, learned, Picks{111, 122, 133, 141, 0, 0});
    CHECK(s[0] == 111 && s[1] == 122 && s[2] == 0 && s[3] == 0);
}

TEST(class_a_bought_option_fills_an_empty_tier)
{
    Picks p{};
    CHECK(AutoSlot(p, *FindOption(812)));
    CHECK(p[0] == 812);
    CHECK(!AutoSlot(p, *FindOption(811)));  // tier 1 is taken: the kit is edited at a trainer or innkeeper
    CHECK(p[0] == 812);
    CHECK(!AutoSlot(p, *FindOption(801)));  // a core ability
}

TEST(class_kit_is_core_then_picks)
{
    Picks p{1111, 0, 1133, 0, 0, 1162};
    auto kit = KitOptions(kDruid, p);
    CHECK(kit.size() == 6);
    CHECK(kit[0]->id == 1101 && kit[2]->id == 1103 && kit[3]->id == 1111 && kit[5]->id == 1162);
    CHECK(InKit(kDruid, p, 1133));
    CHECK(!InKit(kDruid, p, 1121));
    CHECK(InKit(kDruid, p, 1102));          // core, always
    // A pick of another class or the wrong tier counts for nothing.
    Picks wrong{811, 1111, 0, 0, 0, 0};
    CHECK(KitOptions(kDruid, wrong).size() == 3);
}

TEST(class_ranks_follow_the_level)
{
    std::vector<Rank> fireball = {{133, 1}, {143, 6}, {145, 12}, {3140, 18}, {8400, 24}, {25306, 60}};
    CHECK(RankForLevel(fireball, 1) == 133);
    CHECK(RankForLevel(fireball, 11) == 143);
    CHECK(RankForLevel(fireball, 59) == 8400);
    CHECK(RankForLevel(fireball, 60) == 25306);
    // Bought at 10 on a chain that starts later (a level 20 talent): its first rank.
    CHECK(RankForLevel({{23881, 40}, {23894, 60}}, 10) == 23881);
    CHECK(RankForLevel({}, 60) == 0);
}

TEST(class_bar_is_core_picks_then_utility)
{
    // Warrior: three core, Bloodthirst, Blood Frenzy (passive: no button).
    auto bar = BarLayout(kWarrior, Picks{111, 0, 0, 141, 0, 0});
    CHECK(bar[0] == 11578 && bar[1] == 2687 && bar[2] == 676);
    CHECK(bar[3] == 23894 && bar[4] == 0);
    // Shaman: Reincarnation is a passive core ability: slot 2 stays empty, picks still start at 3.
    auto sham = BarLayout(kShaman, Picks{711, 0, 0, 0, 0, 0});
    CHECK(sham[0] == 2645 && sham[1] == 8012 && sham[2] == 0);
    CHECK(sham[3] == 15208);
    CHECK(sham[4] == 20777);    // then Utility, Ancestral Spirit first
    // A mage's many Utility spells stop at slot 11, the sprays (Scorch, Cone of Cold) first.
    auto mage = BarLayout(kMage, Picks{811, 821, 831, 841, 851, 861});
    CHECK(mage[8] == 24340 && mage[9] == 10207 && mage[10] == 10161 && mage[11] == 28612);
}

TEST(class_old_spellbook_goes_but_not_the_rest)
{
    SpellFacts f;
    f.classSkillLine = true;
    CHECK(!KeepSpell(f));       // an old class spell
    f.kit = true;
    CHECK(KeepSpell(f));        // ... unless the kit has it
    f = {};
    f.talent = true;
    CHECK(!KeepSpell(f));
    f = {};
    f.classSkillLine = true;
    f.revive = true;
    CHECK(KeepSpell(f));        // the resurrections the downed state needs
    f = {};
    CHECK(KeepSpell(f));        // anything outside the class lines
    f.racial = true;
    CHECK(KeepSpell(f));
    f = {};
    f.proficiency = true;
    f.classSkillLine = true;
    CHECK(KeepSpell(f));
    f = {};
    f.profession = true;
    CHECK(KeepSpell(f));
}

// A character that spent stock talent points before the kits (or got a talent rank any other
// way) loses those ranks at its first kit apply after login, which refunds the points; only a
// talent spell that is the kit's own option stays.
TEST(class_stock_talents_go_at_login)
{
    SpellFacts f;
    f.talent = true;                    // Improved Heroic Strike, Deflection...
    CHECK(!KeepSpell(f));
    f.classSkillLine = true;            // a talent on a class line
    CHECK(!KeepSpell(f));
    f.kit = true;                       // Bloodthirst, picked in the kit in hand
    CHECK(KeepSpell(f));
    // Every talent-spell option is learned by the kit, so the apply can keep it.
    for (Option const& o : Catalog())
        if (o.kind != Kind::Passive)
            CHECK(LearnsSpells(o));
}

TEST(class_learns_spells_but_custom_passives)
{
    CHECK(LearnsSpells(*FindOption(111)));      // Bloodthirst
    CHECK(!LearnsSpells(*FindOption(141)));     // Blood Frenzy: a custom passive
    CHECK(LearnsSpells(*FindOption(552)));      // Spirit of Redemption: a vanilla passive
    CHECK(LearnsSpells(*FindOption(703)));      // Reincarnation
    CHECK(LearnsSpells(*FindOption(1111)));     // Cat Form
    CHECK(FindOption(1111)->spells[3] == 9904); // with Rake
}

TEST(class_option_by_spell_finds_core_and_tiers)
{
    CHECK(OptionBySpell(kMage, 25306)->id == 811);
    CHECK(OptionBySpell(kMage, 1953)->id == 801);
    CHECK(OptionBySpell(kMage, 14514)->id == 822);
    CHECK(OptionBySpell(kDruid, 9904)->id == 1111);    // a form's ability
    CHECK(OptionBySpell(kWarrior, 25306) == nullptr);
    CHECK(OptionBySpell(kMage, 0) == nullptr);
}

TEST(class_resources_follow_the_archetypes_picked)
{
    Picks p{711, 0, 0, 0, 0, 0};
    auto r = KitResources(kShaman, p);
    CHECK(r.size() == 1 && r[0] == Resource::Static);
    CHECK(ResourceMax(Resource::Static, kShaman, p) == 5);
    p[4] = 751;     // Overcharge
    CHECK(ResourceMax(Resource::Static, kShaman, p) == 8);
    CHECK(KitResources(kShaman, Picks{712, 0, 0, 0, 0, 0}).empty());
    CHECK(KitResources(kWarrior, Picks{}).empty());
    Picks necro{913, 0, 0, 0, 953, 0};
    CHECK(ResourceMax(Resource::LifeForce, kWarlock, necro) == 150);
    CHECK(std::string(ResourceName(Resource::HolyPower)) == "Holy Power");
}

TEST(class_meters_cap_and_spend)
{
    Meter m{Resource::HolyPower, 0, 3};
    CHECK(m.Add(2) == 2 && m.value == 2);
    CHECK(m.Add(5) == 1 && m.value == 3 && m.Full());
    CHECK(!m.Spend(4) && m.value == 3);
    CHECK(m.Spend(2) && m.value == 1);
    CHECK(m.SpendAll() == 1 && m.value == 0);
}

TEST(class_dome_stops_rounds_from_outside_only)
{
    Blocker d;
    d.shape = BlockShape::Dome;
    d.center = {0.f, 0.f, 0.f};
    d.radius = 8.f;
    float at = -1.f;
    CHECK(Blocks(d, {20.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, &at));
    CHECK(at > 0.5f && at < 0.7f);      // stopped at the shell, 12 of 20 yards in
    CHECK(!Blocks(d, {2.f, 0.f, 1.f}, {20.f, 0.f, 1.f}));   // fired out from inside
    CHECK(!Blocks(d, {20.f, 20.f, 1.f}, {30.f, 20.f, 1.f})); // nowhere near
    CHECK(Blocks(d, {20.f, 0.f, 1.f}, {-20.f, 0.f, 1.f}));  // passing through
}

TEST(class_wall_stops_rounds_crossing_its_front)
{
    Blocker w;
    w.shape = BlockShape::Wall;
    w.center = {0.f, 0.f, 0.f};
    w.facing = 0.f;         // its front looks down +x
    w.radius = 3.f;         // 6 yards wide
    w.height = 3.f;
    CHECK(Blocks(w, {10.f, 0.f, 1.5f}, {-10.f, 0.f, 1.5f}));     // from the front
    CHECK(!Blocks(w, {-10.f, 0.f, 1.5f}, {10.f, 0.f, 1.5f}));    // allies behind fire out
    CHECK(!Blocks(w, {10.f, 5.f, 1.5f}, {-10.f, 5.f, 1.5f}));    // past its end
    CHECK(!Blocks(w, {10.f, 0.f, 6.f}, {-10.f, 0.f, 6.f}));      // over it
    CHECK(!Blocks(w, {10.f, 0.f, 1.5f}, {2.f, 0.f, 1.5f}));      // stops short of it
}

TEST(class_arc_blocks_the_front_cone)
{
    float const half = 50.f * 3.14159265f / 180.f;
    CHECK(InArc({0.f, 0.f, 0.f}, 0.f, half, {10.f, 2.f, 0.f}));
    CHECK(!InArc({0.f, 0.f, 0.f}, 0.f, half, {-10.f, 0.f, 0.f}));
    CHECK(!InArc({0.f, 0.f, 0.f}, 0.f, half, {1.f, 10.f, 0.f}));    // 84 degrees off
    Blocker a;
    a.shape = BlockShape::Arc;
    a.halfAngle = half;
    a.facing = 3.14159265f;     // looking down -x
    CHECK(Blocks(a, {-10.f, 0.f, 0.f}, {0.f, 0.f, 0.f}));
    CHECK(!Blocks(a, {10.f, 0.f, 0.f}, {0.f, 0.f, 0.f}));
}

TEST(class_smoke_blocks_sight_through_it)
{
    Smoke s{{0.f, 0.f, 0.f}, 8.f, 0};
    CHECK(SightBlocked(s, {-20.f, 0.f, 1.f}, {20.f, 0.f, 1.f}));
    CHECK(SightBlocked(s, {-20.f, 0.f, 1.f}, {1.f, 0.f, 1.f}));     // the target inside
    CHECK(!SightBlocked(s, {-20.f, 20.f, 1.f}, {20.f, 20.f, 1.f}));
}

TEST(class_wire_round_trips)
{
    State st;
    st.cls = kMage;
    st.level = 42;
    st.kit = 2;
    st.flags = STATE_CAN_EDIT;
    st.picks = {811, 822, 0, 841, 0, 0};
    st.learned = {811, 822, 841, 812};
    std::vector<uint8_t> b = EncodeState(st);
    CHECK(b[0] == kOpState && b.size() == 5 + 12 + 1 + 8);
    State back;
    CHECK(DecodeState(b, back));
    CHECK(back.cls == kMage && back.level == 42 && back.kit == 2 && back.flags == STATE_CAN_EDIT);
    CHECK(back.picks == st.picks && back.learned == st.learned);
    b.pop_back();
    CHECK(!DecodeState(b, back));

    std::vector<Meter> ms = {{Resource::Static, 3, 5}, {Resource::LifeForce, 120, 150}};
    std::vector<uint8_t> mb = EncodeMeters(ms);
    CHECK(mb.size() == 2 + 2 * 5);
    std::vector<Meter> mback;
    CHECK(DecodeMeters(mb, mback) && mback.size() == 2 && mback[1].value == 120 && mback[1].max == 150);

    CHECK(EncodePickResult(PickError::NotLearned, 3) == std::vector<uint8_t>({kOpPickResult, 4, 3}));
    CHECK(EncodeSpotted({{0x0102, 500}}).size() == 2 + 10);
    CHECK(EncodeSmoke({{1.f, 2.f, 3.f}, 8.f, 0}, 10000).size() == 1 + 16 + 2);
}

TEST(class_names_are_the_catalogs)
{
    CHECK(std::string(ArchetypeName(kRogue, 2)) == "Duelist");
    CHECK(std::string(ArchetypeName(kRogue, kCoreArchetype)) == "Core");
    CHECK(std::string(ClassName(kShaman)) == "Shaman");
    CHECK(AllOf(kPriest).count(563));
}

TEST(class_page_carries_the_catalog_to_the_client)
{
    Page p = MakePage(kWarrior);
    CHECK(p.entries.size() == 21);
    CHECK(p.entries[0].id == 101 && p.entries[0].tier == 0 && p.entries[0].name == "Charge");
    CHECK(p.entries[3].id == 111 && p.entries[3].spell == 23894);
    CHECK(p.archetypes[1] == "Vanguard");
    std::vector<uint8_t> b = EncodePage(p);
    CHECK(b[0] == kOpPage && b.size() < 8192);
    Page back;
    CHECK(DecodePage(b, back));
    CHECK(back.cls == kWarrior && back.entries.size() == 21 && back.archetypes == p.archetypes);
    CHECK(back.entries[12].name == p.entries[12].name && back.entries[12].kind == Kind::Passive);
    CHECK(back.entries[20].capstone && back.entries[20].spell == 22888);
    b.pop_back();
    CHECK(!DecodePage(b, back));
}

TEST(class_spell_tweaks_go_in_pieces)
{
    std::vector<SpellTweak> tweaks;
    for (uint32_t i = 0; i < 700; ++i)
        tweaks.push_back({11578 + i, 0x00050010u, 1024u, 0u, 15000u, 0u, 1, 2, TWEAK_NO_COST | TWEAK_ON_SELF});
    std::vector<std::vector<uint8_t>> bodies = EncodeSpellTweaks(tweaks, 8192);
    CHECK(bodies.size() == 3);
    size_t total = 0;
    for (auto const& b : bodies)
    {
        CHECK(b.size() <= 8192 && b[0] == kOpSpellTweaks);
        std::vector<SpellTweak> back;
        CHECK(DecodeSpellTweaks(b, back));
        total += back.size();
        CHECK(back[0].recoveryMs == 15000 && back[0].castIndex == 1 && back[0].rangeIndex == 2);
        CHECK(back[0].flags == (TWEAK_NO_COST | TWEAK_ON_SELF) && back[0].attributesEx == 1024);
    }
    CHECK(total == 700);
    CHECK(bodies[0].size() == 3 + 282 * kSpellTweakBytes);
    CHECK(EncodeSpellTweaks({}, 8192).empty());
}
