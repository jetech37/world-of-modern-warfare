/*
 * World of Modern Warfare: Inscription and Jewelcrafting (fusion/Crafts.h): the profession data,
 * training as a primary profession, recipes, milling and prospecting, the glyph and gem rules,
 * the handling they apply and the wire. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Crafts.h"
#include "fusion/Throttle.h"

#include <algorithm>
#include <set>

using namespace Fusion;
using namespace Fusion::Crafts;
using Stats::Out;

namespace
{
    Recipe const* Named(char const* name)
    {
        for (Recipe const& r : Recipes())
            if (std::string(r.name) == name)
                return &r;
        return nullptr;
    }

    GunInfo Gun(WeaponClass c = WeaponClass::AssaultRifle, bool scoped = false)
    {
        GunInfo g;
        g.present = true;
        g.weaponClass = c;
        g.scoped = scoped;
        return g;
    }
}

TEST(crafts_the_two_trades_have_their_keys_trainers_and_tiers)
{
    CHECK(SkillKey(Trade::Inscription) == 9003 && SkillKey(Trade::Jewelcrafting) == 9004);
    CHECK(TradeOfSkill(9003) == Trade::Inscription && TradeOfSkill(9002) == Trade::None);
    CHECK(TradeOfTrainer(kTrainerJewelcrafting) == Trade::Jewelcrafting && TradeOfTrainer(94100) == Trade::None);
    CHECK(std::string(TradeName(Trade::Inscription)) == "Inscription");
    CHECK(SkillCap(Tier::Apprentice) == 75 && SkillCap(Tier::Artisan) == 300);
    CHECK(TierHolding(1) == Tier::Apprentice && TierHolding(151) == Tier::Expert && TierHolding(300) == Tier::Artisan);
}

TEST(crafts_a_first_tier_takes_a_free_primary_slot)
{
    CHECK(CanTrainTier(Tier::None, Tier::Apprentice, 0, 5, 100, 1) == TrainCheck::Ok);
    CHECK(CanTrainTier(Tier::None, Tier::Apprentice, 0, 5, 100, 0) == TrainCheck::NoFreeSlot);
    // Later tiers don't ask a slot again.
    CHECK(CanTrainTier(Tier::Apprentice, Tier::Journeyman, 50, 10, 500, 0) == TrainCheck::Ok);
    CHECK(CanTrainTier(Tier::Apprentice, Tier::Journeyman, 49, 10, 500, 0) == TrainCheck::SkillTooLow);
    CHECK(CanTrainTier(Tier::Journeyman, Tier::Expert, 125, 19, 2500, 0) == TrainCheck::LevelTooLow);
    CHECK(CanTrainTier(Tier::Expert, Tier::Artisan, 200, 35, 9999, 0) == TrainCheck::NotEnoughMoney);
    CHECK(CanTrainTier(Tier::Apprentice, Tier::Expert, 300, 60, 99999, 1) == TrainCheck::NotNext);
    CHECK(CanTrainTier(Tier::Artisan, Tier::Artisan, 300, 60, 99999, 1) == TrainCheck::AlreadyKnown);
    CHECK(CanTrainTier(Tier::None, Tier::Apprentice, 0, 1, 0, 0, true) == TrainCheck::Ok);
}

TEST(crafts_every_recipe_is_well_formed)
{
    std::set<uint16_t> ids;
    std::set<uint32_t> products;
    size_t inscription = 0, jewelcrafting = 0;
    for (Recipe const& r : Recipes())
    {
        CHECK(ids.insert(r.id).second);
        CHECK(RecipeOf(r.id) == &r);
        CHECK(r.trade == Trade::Inscription || r.trade == Trade::Jewelcrafting);
        CHECK(r.required >= 1 && r.required <= 300);
        CHECK(!r.reagents.empty() && std::string(r.name).size() > 3 && std::string(r.group).size() > 3);
        for (Material const& m : r.reagents)
            CHECK(m.item && m.count);
        if (r.kind == Kind::Make)
        {
            CHECK(r.product != 0 && r.yields.empty() && r.productCount >= 1);
            CHECK(products.insert(r.product).second);   // one recipe makes each thing
        }
        else
        {
            CHECK(!r.yields.empty() && r.starter);
            CHECK(r.reagents.size() == 1 && r.reagents[0].count == 5);
            CHECK(r.trade == (r.kind == Kind::Mill ? Trade::Inscription : Trade::Jewelcrafting));
        }
        CHECK(r.starter ? r.learnCost == 0 : r.learnCost > 0);
        (r.trade == Trade::Inscription ? inscription : jewelcrafting)++;
    }
    CHECK(inscription >= 50 && jewelcrafting >= 30);
    CHECK(RecipeOf(0) == nullptr && RecipeOf(60000) == nullptr);
    // Every glyph and gem is made by its trade, and each is the trade's own item.
    for (GlyphDef const& g : Glyphs())
        CHECK(products.count(g.item) && g.item >= Item::kGlyphFirst && g.item <= Item::kGlyphLast);
    for (GemDef const& g : Gems())
        CHECK(products.count(g.item) && g.item >= Item::kGemFirst && g.item <= Item::kGemLast);
    // A starter recipe each, known with the profession.
    Recipe const* ink = Named("Moonglow Ink");
    CHECK(ink && ink->starter && Known(*ink, {}));
    Recipe const* midnight = Named("Midnight Ink");
    CHECK(midnight && !Known(*midnight, {}) && Known(*midnight, {midnight->id}));
    CHECK(RecipesOf(Trade::Jewelcrafting).front()->trade == Trade::Jewelcrafting);
    // Cuts ask the jeweler's kit (sold by the trainer), not used up.
    Recipe const* tiger = Named("Bold Tigerseye");
    CHECK(tiger && tiger->tool == Item::JewelersKit && tiger->starter);
    bool sold = false;
    for (Ware const& w : Wares(Trade::Jewelcrafting))
        sold = sold || w.item == Item::JewelersKit;
    CHECK(sold && !Wares(Trade::Inscription).empty());
}

TEST(crafts_learning_and_making_follow_the_skill)
{
    Recipe const& r = *Named("Lion's Ink");
    CHECK(CanLearn(r, Trade::Inscription, Tier::Journeyman, 100, false, 1000) == LearnCheck::Ok);
    CHECK(CanLearn(r, Trade::Jewelcrafting, Tier::Journeyman, 100, false, 1000) == LearnCheck::WrongTrade);
    CHECK(CanLearn(r, Trade::Inscription, Tier::None, 100, false, 1000) == LearnCheck::NotLearned);
    CHECK(CanLearn(r, Trade::Inscription, Tier::Journeyman, 99, false, 1000) == LearnCheck::SkillTooLow);
    CHECK(CanLearn(r, Trade::Inscription, Tier::Journeyman, 100, false, 999) == LearnCheck::NotEnoughMoney);
    CHECK(CanLearn(r, Trade::Inscription, Tier::Journeyman, 100, true, 1000) == LearnCheck::Known);

    Inventory bags;
    CHECK(CanMake(r, Tier::Journeyman, true, 100, bags) == MakeCheck::NoReagents);
    bags[Item::GoldenPigment] = 2;
    CHECK(CanMake(r, Tier::Journeyman, true, 100, bags) == MakeCheck::Ok);
    CHECK(CanMake(r, Tier::Journeyman, false, 100, bags) == MakeCheck::Unknown);
    CHECK(CanMake(r, Tier::Journeyman, true, 99, bags) == MakeCheck::SkillTooLow);
    CHECK(CanMake(r, Tier::None, true, 100, bags) == MakeCheck::NotLearned);
    CHECK(CanMake(r, Tier::None, false, 0, {}, true) == MakeCheck::Ok);
    Recipe const& cut = *Named("Bold Tigerseye");
    Inventory gems{{818, 1}};
    CHECK(CanMake(cut, Tier::Apprentice, true, 20, gems) == MakeCheck::NoTool);
    gems[Item::JewelersKit] = 1;
    CHECK(CanMake(cut, Tier::Apprentice, true, 20, gems) == MakeCheck::Ok);
    // Orange always teaches, gray never; never past the cap.
    CHECK(MakeSkillUp(r, 100, 150, 0.99f) == 101);
    CHECK(MakeSkillUp(r, 175, 300, 0.f) == 175);
    CHECK(MakeSkillUp(r, 150, 150, 0.f) == 150);
}

TEST(crafts_milling_and_prospecting_always_give_something)
{
    Recipe const& mill = *Named("Mill Peacebloom");
    CHECK(mill.kind == Kind::Mill && RollBytes(mill) == 2);
    std::vector<Draw> d = Roll(mill, {0, 0});
    CHECK(d.size() == 1 && d[0].item == Item::AlabasterPigment && d[0].count == 2);
    d = Roll(mill, {0, 1});
    CHECK(d[0].count == 3);
    // A miss on every draw still pays the first yield (Wrath's milling is never empty).
    d = Roll(mill, {255, 0});
    CHECK(d.size() == 1 && d[0].item == Item::AlabasterPigment);

    Recipe const& thorium = *Named("Prospect Thorium Ore");
    CHECK(thorium.kind == Kind::Prospect && thorium.required == 250);
    std::vector<uint8_t> all(RollBytes(thorium), 0);
    d = Roll(thorium, all);
    CHECK(d.size() == 2);   // never more than two gems
    std::vector<uint8_t> none(RollBytes(thorium), 255);
    d = Roll(thorium, none);
    CHECK(d.size() == 1 && d[0].item == 7910 && d[0].count == 1);
    for (Recipe const& r : Recipes())
        for (Yield const& y : r.yields)
            CHECK(y.item && y.min >= 1 && y.max >= y.min && y.chance > 0);
}

TEST(crafts_glyph_slots_open_with_level_and_hold_a_glyph_once)
{
    KitGlyphs kit;
    uint32_t const quick = 94330, steady = 94331, road = 94350;
    CHECK(CanInscribe(kit, GlyphKind::Major, 0, quick, 15, 1) == GlyphCheck::Ok);
    CHECK(CanInscribe(kit, GlyphKind::Major, 0, quick, 14, 1) == GlyphCheck::SlotLocked);
    CHECK(CanInscribe(kit, GlyphKind::Major, 1, quick, 29, 1) == GlyphCheck::SlotLocked);
    CHECK(CanInscribe(kit, GlyphKind::Major, 2, quick, 50, 1) == GlyphCheck::Ok);
    CHECK(CanInscribe(kit, GlyphKind::Minor, 0, quick, 60, 1) == GlyphCheck::WrongKind);
    CHECK(CanInscribe(kit, GlyphKind::Minor, 0, road, 20, 1) == GlyphCheck::Ok);
    CHECK(CanInscribe(kit, GlyphKind::Minor, 0, 818, 60, 1) == GlyphCheck::NotAGlyph);
    CHECK(CanInscribe(kit, GlyphKind::Minor, 3, road, 60, 1) == GlyphCheck::BadSlot);
    kit.major[0] = quick;
    CHECK(CanInscribe(kit, GlyphKind::Major, 1, quick, 60, 1) == GlyphCheck::AlreadyInKit);
    CHECK(CanInscribe(kit, GlyphKind::Major, 0, quick, 60, 1) == GlyphCheck::Ok);     // its own slot
    CHECK(CanInscribe(kit, GlyphKind::Major, 1, steady, 60, 1) == GlyphCheck::Ok);
    // Class glyphs ask the class: Quickened Words for casters (8 Mage), not warriors.
    CHECK(CanInscribe(kit, GlyphKind::Major, 1, 94340, 60, 8) == GlyphCheck::Ok);
    CHECK(CanInscribe(kit, GlyphKind::Major, 1, 94340, 60, 1) == GlyphCheck::WrongClass);
    CHECK(CanInscribe(kit, GlyphKind::Major, 1, 94342, 60, 3) == GlyphCheck::Ok);     // the Tracker, a hunter
    for (GlyphDef const& g : Glyphs())
        CHECK(std::string(g.name).rfind("Glyph of", 0) == 0 && std::string(g.text).size() > 10);
}

TEST(crafts_glyph_mods_count_open_slots_of_the_class_and_never_gun_damage)
{
    KitGlyphs kit;
    kit.major = {94330, 94340, 94336};      // Quick Draw, Quickened Words, Last Rites
    kit.minor = {94350, 94352, 0};          // Long Road, Gravedigger
    Mods const mage = GlyphMods(kit, 60, 8);
    CHECK_NEAR(mage.handling[size_t(Out::SwapTime)], -0.12f, 1e-6);
    CHECK_NEAR(mage.handling[size_t(Out::AdsTime)], 0.04f, 1e-6);
    CHECK_NEAR(mage.handling[size_t(Out::ReloadTime)], 0.05f, 1e-6);
    CHECK_NEAR(mage.castTime, 0.94f, 1e-6);
    CHECK(mage.lastStandMs == 5000);
    CHECK_NEAR(mage.outOfCombatSpeed, 1.03f, 1e-6);
    CHECK_NEAR(mage.goldLoot, 1.1f, 1e-6);
    // A warrior's Quickened Words sleeps; at level 29 only the first slots play.
    Mods const warrior = GlyphMods(kit, 60, 1);
    CHECK_NEAR(warrior.castTime, 1.f, 1e-6);
    CHECK_NEAR(warrior.handling[size_t(Out::ReloadTime)], 0.f, 1e-6);
    Mods const low = GlyphMods(kit, 29, 8);
    CHECK(low.lastStandMs == 0 && low.castTime == 1.f && low.goldLoot == 1.f);
    CHECK_NEAR(low.outOfCombatSpeed, 1.03f, 1e-6);
    CHECK(GlyphMods(KitGlyphs{}, 60, 8) == Mods{});
}

TEST(crafts_gems_fit_as_attachments_do)
{
    KitGems kit;
    GunInfo const rifle = Gun();
    uint32_t const tiger = 94450, ruby = 94459, malachite = 94451, moon = 94454, prism = 94464, relentless = 94465;
    CHECK(CanSocket(kit, GunSlot::Primary, 0, tiger, rifle, 0) == GemCheck::Ok);
    CHECK(CanSocket(kit, GunSlot::Primary, 0, 818, rifle, 0) == GemCheck::NotAGem);
    CHECK(CanSocket(kit, GunSlot::Primary, 0, tiger, GunInfo{}, 0) == GemCheck::NoGun);
    // The jeweler's own socket needs Jewelcrafting 275; the secondary has one socket.
    CHECK(CanSocket(kit, GunSlot::Primary, 2, tiger, rifle, 274) == GemCheck::NoSocket);
    CHECK(CanSocket(kit, GunSlot::Primary, 2, tiger, rifle, 275) == GemCheck::Ok);
    CHECK(CanSocket(kit, GunSlot::Secondary, 1, tiger, rifle, 300) == GemCheck::NoSocket);
    CHECK(CanSocket(kit, GunSlot::Secondary, 0, tiger, rifle, 0) == GemCheck::Ok);
    // The meta socket takes a meta gem only, and a meta gem goes nowhere else.
    CHECK(CanSocket(kit, GunSlot::Primary, kMetaSocket, tiger, rifle, 0) == GemCheck::MetaOnly);
    CHECK(CanSocket(kit, GunSlot::Primary, 0, relentless, rifle, 0) == GemCheck::NotMeta);
    CHECK(CanSocket(kit, GunSlot::Primary, kMetaSocket, relentless, rifle, 0) == GemCheck::Ok);
    CHECK(CanSocket(kit, GunSlot::Secondary, kMetaSocket, relentless, rifle, 0) == GemCheck::NoSocket);
    // One of each kind: two aiming gems never share a gun (the other gun may take one).
    kit.primary[0] = tiger;
    CHECK(CanSocket(kit, GunSlot::Primary, 1, ruby, rifle, 0) == GemCheck::SameKind);
    CHECK(CanSocket(kit, GunSlot::Primary, 0, ruby, rifle, 0) == GemCheck::Ok);     // replacing it
    CHECK(CanSocket(kit, GunSlot::Secondary, 0, ruby, rifle, 0) == GemCheck::Ok);
    CHECK(CanSocket(kit, GunSlot::Primary, 1, malachite, rifle, 0) == GemCheck::Ok);
    // A sway gem asks a scope.
    CHECK(CanSocket(kit, GunSlot::Primary, 1, moon, rifle, 0) == GemCheck::NeedsScope);
    CHECK(CanSocket(kit, GunSlot::Primary, 1, moon, Gun(WeaponClass::Sniper, true), 0) == GemCheck::Ok);
    CHECK(Scoped(WeaponClass::Sniper, {}) && Scoped(WeaponClass::Smg, {"acog"}) && Scoped(WeaponClass::Lmg, {"reflex", "thermal"}));
    CHECK(!Scoped(WeaponClass::AssaultRifle, {"reflex", "eotech"}));
    // Every gem: a colour, a handling effect (never damage), and a name.
    for (GemDef const& g : Gems())
    {
        CHECK(g.color != 0 && g.effects[0].out != Out::Count && std::string(g.text).size() > 5);
        CHECK(prism != g.item || g.color == GEM_PRISMATIC);
    }
}

TEST(crafts_the_meta_gem_wakes_with_two_colours)
{
    std::array<uint32_t, kSockets> s{};
    s[kMetaSocket] = 94465;
    CHECK(!MetaAwake(s));
    s[0] = 94450;                           // red
    CHECK(!MetaAwake(s));
    s[1] = 94459;                           // red again: one colour
    CHECK(!MetaAwake(s));
    s[1] = 94451;                           // blue
    CHECK(MetaAwake(s));
    s = {};
    s[0] = 94464;                           // prismatic: every colour
    CHECK(MetaAwake(s));

    KitGems kit;
    kit.primary = {94450, 94455, 0, 94465};  // red aim, yellow reload, the meta
    GunInfo const rifle = Gun();
    Mods m = GemMods(kit, GunSlot::Primary, rifle, 0);
    CHECK_NEAR(m.handling[size_t(Out::AdsTime)], -0.03f, 1e-6);
    CHECK_NEAR(m.handling[size_t(Out::ReloadTime)], -0.03f, 1e-6);
    CHECK_NEAR(m.handling[size_t(Out::RegenDelay)], 0.08f, 1e-6);
    // Asleep: only red left.
    kit.primary[1] = 0;
    m = GemMods(kit, GunSlot::Primary, rifle, 0);
    CHECK_NEAR(m.handling[size_t(Out::RegenDelay)], 0.f, 1e-6);
    // A sway gem without a scope sleeps; the jeweler's socket counts only with the skill.
    kit.primary = {94454, 0, 94455, 0};
    m = GemMods(kit, GunSlot::Primary, rifle, 0);
    CHECK(m.handling[size_t(Out::Sway)] == 0.f && m.handling[size_t(Out::ReloadTime)] == 0.f);
    m = GemMods(kit, GunSlot::Primary, Gun(WeaponClass::Sniper, true), 300);
    CHECK_NEAR(m.handling[size_t(Out::Sway)], -0.04f, 1e-6);
    CHECK_NEAR(m.handling[size_t(Out::ReloadTime)], -0.03f, 1e-6);
    // A doubled kind (saved before a rule changed) counts once.
    kit.primary = {94450, 94459, 0, 0};
    m = GemMods(kit, GunSlot::Primary, rifle, 0);
    CHECK_NEAR(m.handling[size_t(Out::AdsTime)], -0.03f, 1e-6);
    CHECK(GemMods(kit, GunSlot::Primary, GunInfo{}, 0) == Mods{});
}

TEST(crafts_handling_is_applied_within_the_limits)
{
    Stats::Derived d;
    Mods m;
    m.handling[size_t(Out::AdsTime)] = -0.10f;
    m.handling[size_t(Out::SwapTime)] = 0.05f;
    m.handling[size_t(Out::RegenDelay)] = 0.2f;
    m.handling[size_t(Out::EquipmentStrength)] = 0.9f;
    float const delay = d.regenDelay;
    ApplyHandling(d, m);
    CHECK_NEAR(d[Out::AdsTime], 0.9f, 1e-5);
    CHECK_NEAR(d[Out::SwapTime], 1.05f, 1e-5);
    CHECK_NEAR(d[Out::RegenDelay], 0.2f, 1e-5);
    CHECK_NEAR(d.regenDelay, delay - Stats::kRegenDelay * 0.2f, 1e-5);
    CHECK_NEAR(d[Out::EquipmentStrength], Stats::OutputLimit(Out::EquipmentStrength), 1e-5);
    // A multiplier never under its floor, nor a trade-off past 1.25.
    Stats::Derived e;
    e.value[size_t(Out::Recoil)] = 0.66f;
    Mods big;
    big.handling[size_t(Out::Recoil)] = -0.5f;
    big.handling[size_t(Out::Flinch)] = 2.f;
    ApplyHandling(e, big);
    CHECK_NEAR(e[Out::Recoil], Stats::OutputLimit(Out::Recoil), 1e-5);
    CHECK_NEAR(e[Out::Flinch], 1.25f, 1e-5);
    // Combining: multipliers multiply, shares add.
    Mods a, b;
    a.castTime = 0.9f;
    b.castTime = 0.9f;
    a.handling[0] = -0.1f;
    b.handling[0] = -0.05f;
    b.lastStandMs = 100;
    Combine(a, b);
    CHECK_NEAR(a.castTime, 0.81f, 1e-6);
    CHECK_NEAR(a.handling[0], -0.15f, 1e-6);
    CHECK(a.lastStandMs == 100);
}

TEST(crafts_wire_round_trips)
{
    Window w;
    w.trade = Trade::Jewelcrafting;
    w.open = true;
    w.trainer = true;
    w.tier = Tier::Journeyman;
    w.skill = 120;
    w.tone = 2;
    w.message = "You have learned it.";
    Service s;
    s.action = Action::Learn;
    s.arg = 7;
    s.cost = 300;
    s.label = "Learn: Bronze Setting";
    s.note = "Jewelcrafting 50";
    w.services.push_back(s);
    std::vector<uint8_t> b = EncodeWindow(w);
    CHECK(b[0] == kOpWindow && b[1] == 2);
    Window back;
    CHECK(DecodeWindow(b, back) && back.trade == Trade::Jewelcrafting && back.trainer && back.skill == 120);
    CHECK(back.services.size() == 1 && back.services[0].label == s.label && back.services[0].cost == 300);
    b.pop_back();
    CHECK(!DecodeWindow(b, back));

    Trade t;
    Action a;
    uint32_t arg;
    CHECK(DecodeAct(EncodeAct(Trade::Inscription, Action::Make, MakeArg(12, 5)), t, a, arg));
    CHECK(t == Trade::Inscription && a == Action::Make && MakeRecipeOf(arg) == 12 && MakeCountOf(arg) == 5);
    CHECK(MakeCountOf(MakeArg(1, 0)) == 1 && MakeCountOf(MakeArg(1, 999)) == kMaxMakeBatch);
    CHECK(!DecodeAct({kOpAct, 3, 1, 0, 0, 0, 0}, t, a, arg));      // no such trade
    CHECK(!DecodeAct({kOpAct, 1, 9, 0, 0, 0, 0}, t, a, arg));      // no such action

    std::vector<BookEntry> book = Book(Trade::Inscription, {});
    CHECK(!book.empty());
    std::vector<uint8_t> bb = EncodeBook(Trade::Inscription, book);
    std::vector<BookEntry> got;
    CHECK(DecodeBook(bb, t, got) && t == Trade::Inscription && got.size() == book.size());
    CHECK(got[0].name == book[0].name && got[0].reagents.size() == book[0].reagents.size());
    CHECK(got[0].kind == Kind::Mill && got[0].known && got[0].product == Item::AlabasterPigment);

    Tier tier;
    uint16_t skill, cap;
    CHECK(DecodeSkill(EncodeSkill(Trade::Jewelcrafting, Tier::Expert, 200), t, tier, skill, cap));
    CHECK(t == Trade::Jewelcrafting && tier == Tier::Expert && skill == 200 && cap == 225);

    AugmentAsk ask{1, 0, 3, 94465};
    AugmentAsk ab;
    CHECK(DecodeAugment(EncodeAugment(ask), ab) && ab.what == 1 && ab.slot == 3 && ab.item == 94465);
    CHECK(!DecodeAugment(EncodeAugment({0, 0, 3, 1}), ab));       // a glyph slot past the third

    Augments au;
    au.open = true;
    au.kit = 2;
    au.canEdit = true;
    au.level = 60;
    au.jewelcrafting = 280;
    au.glyphs.major = {94330, 0, 94336};
    au.gems.primary = {94450, 94455, 94460, 94465};
    au.flags = kAugPrimary | kAugPrimaryMeta;
    au.message = "Inscribed.";
    Augments aback;
    CHECK(DecodeAugments(EncodeAugments(au), aback));
    CHECK(aback.kit == 2 && aback.canEdit && aback.jewelcrafting == 280 && aback.glyphs == au.glyphs);
    CHECK(aback.gems == au.gems && aback.flags == au.flags && aback.message == au.message);
}

TEST(crafts_asks_are_paced_as_the_other_windows)
{
    uint8_t const ch = Construction::kChannelProfessions;
    for (uint8_t op : {kOpAsk, kOpAct, kOpAskAugments, kOpAugment})
        CHECK(Throttle::Costly(ch, {op, 1}));
    CHECK(Throttle::Costly(ch, {Construction::kOpCraftKit, 1, 1, 0}));
    CHECK(!Throttle::Costly(ch, {kOpAugments}));     // a server op, never asked
}
