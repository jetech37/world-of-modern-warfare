/*
 * World of Modern Warfare: every druid form, stock and new, against the form rules.
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Classes.h"
#include "fusion/DruidForms.h"

#include <set>

using namespace Fusion;
using namespace Fusion::DruidForms;

namespace
{
    // SpellShapeshiftForm.dbc rows 1.12 ships empty (no name, no flags, no bar): free for new forms.
    std::set<uint8_t> const kEmptyRows = {9, 10, 11, 12, 13, 20, 21, 22, 23, 24, 25, 26, 27, 29};

    // Spell attribute bits a form spell must not carry, or a leave path breaks.
    constexpr uint32_t kAttrPassive = 0x00000040;
    constexpr uint32_t kAttrDoNotDisplay = 0x00000080;        // no buff, no spellbook
    constexpr uint32_t kAttrDebuff = 0x04000000;
    constexpr uint32_t kAttrNoAuraCancel = 0x80000000;        // the buff cannot be right-clicked off
    constexpr uint32_t kAttrExNoAuraIcon = 0x10000000;        // no buff icon (Mar'li Transform's)
    constexpr uint32_t kAttrNotShapeshift = 0x00010000;
}

TEST(druid_forms_table_holds_every_form_of_the_catalog)
{
    // Every form option, Travel Form (core) and Aquatic Form (Utility). Tree of Life is no form:
    // the druid keeps casting in it, in a treant's look (FusionClassAbilities.cpp).
    std::set<uint32_t> want;
    for (Classes::Option const* o : Classes::OptionsOf(Classes::kDruid))
        if (o->kind == Classes::Kind::Form)
            want.insert(o->spells[0]);
    want.insert(783);
    want.insert(1066);
    for (uint32_t spell : want)
        CHECK(BySpell(spell) != nullptr);
    CHECK(BySpell(775) == nullptr);
    // Stock: cat, bear, dire bear, travel, aquatic, moonkin; new: stag, crow, spider, worgen.
    CHECK(Forms().size() == 10);
}

TEST(druid_forms_have_their_own_ids_spells_and_pages)
{
    std::set<uint8_t> forms;
    std::set<uint32_t> spells;
    std::set<uint8_t> pages;
    for (Form const& f : Forms())
    {
        CHECK(forms.insert(f.form).second);
        CHECK(spells.insert(f.spell).second);
        CHECK(ByForm(f.form) == &f);
        CHECK(BySpell(f.spell) == &f);
        // A page of its own (bear and dire bear are one form family, 1.12's page 9 for both).
        CHECK(f.bonusBar >= 1 && f.bonusBar <= kMaxBonusBar);
        if (f.form != kDireBear)
            CHECK(pages.insert(f.bonusBar).second);
        // The page's twelve slots fit the action button array, and an action slot is one byte.
        CHECK(FirstSlotOfBar(f.bonusBar) + kSlotsPerPage <= kActionButtons);
        if (f.stock)
            CHECK(!kEmptyRows.count(f.form));
        else
            CHECK(kEmptyRows.count(f.form) == 1);
    }
    CHECK(kActionButtons <= 256);
    CHECK(kActionButtons > 120);
    // The stock pages stay the stock ones.
    CHECK(ByForm(kCat)->bonusBar == 1);
    CHECK(ByForm(kBear)->bonusBar == 3);
    CHECK(ByForm(kDireBear)->bonusBar == 3);
    CHECK(FirstSlotOfBar(1) == 72);
    CHECK(FirstSlotOfBar(3) == 96);
}

TEST(druid_forms_show_their_own_limbs_and_hold_no_gun)
{
    for (Form const& f : Forms())
    {
        CHECK(!f.guns);
        CHECK(!f.heldWeapon);
        CHECK(!f.humanoidBody);
        CHECK(f.limbs != Limbs::None);
        CHECK(BlocksGuns(f.form));
        // Only the worgen keeps hands, and shows them at rest; the animals only in action.
        CHECK((f.limbs == Limbs::ClawedHands) == (f.form == kWorgen));
        CHECK(f.limbsAlways == (f.form == kWorgen));
        CHECK(f.display != 0);
        if (!f.stock)
            CHECK(f.creature != 0);
    }
    CHECK(!BlocksGuns(0));
    CHECK(!BlocksGuns(17));     // Battle Stance
    CHECK(!BlocksGuns(30));     // Stealth
}

TEST(druid_forms_are_left_every_stock_way)
{
    for (Form const& f : Forms())
    {
        CHECK((f.leave & kStockLeave) == kStockLeave);
        // 1.12 druid forms refuse a caster spell rather than drop (Can't do that while
        // shapeshifted); none leaves on one.
        CHECK(!(f.leave & LEAVE_NON_FORM_SPELL));
        CHECK(((f.leave & LEAVE_TIMED) != 0) == (f.durationMs != 0));
    }
    CHECK(ByForm(kTree) == nullptr);
}

TEST(druid_fitted_form_spells_are_clean_shapeshifts)
{
    int fitted = 0;
    for (Form const& f : Forms())
    {
        if (!f.fit)
            continue;
        ++fitted;
        // The client is told to cast it on oneself (op 7's ON_SELF): Arugal's Curse wanted a target.
        CHECK(IsFitted(f.spell));
        FittedSpell const s = FitFormSpell(f);
        CHECK(IsCleanFormSpell(s, f.form));
        CHECK(!(s.attributes & (kAttrPassive | kAttrDoNotDisplay | kAttrDebuff | kAttrNoAuraCancel)));
        CHECK(!(s.attributesEx & kAttrExNoAuraIcon));
        // Not while shapeshifted, as stock forms: another form cancels the current one first.
        CHECK(s.attributes & kAttrNotShapeshift);
        CHECK(s.activeIconId != 0);
        CHECK(s.durationIndex == (f.durationMs ? kDuration30s : kDurationInfinite));
    }
    CHECK(fitted == 4);     // the four new forms
    CHECK(!IsFitted(768) && !IsFitted(775) && !IsFitted(0));
    // The rewrite takes out what trapped the old donors.
    FittedSpell bad = FitFormSpell(*ByForm(kWorgen));
    bad.effects[1] = {kEffectApplyAura, kAuraModCharm, 0, 39, 6};
    CHECK(!IsCleanFormSpell(bad, kWorgen));
    bad = FitFormSpell(*ByForm(kStag));
    bad.effects[1] = {kEffectApplyAura, kAuraModStun, 0, -1, kTargetCaster};    // Reindeer's stun
    CHECK(!IsCleanFormSpell(bad, kStag));
    bad = FitFormSpell(*ByForm(kSpider));
    bad.effects[0] = {kEffectApplyAura, kAuraTransform, 14967, 0, kTargetCaster};
    CHECK(!IsCleanFormSpell(bad, kSpider));
    // Speed and the glide come with the form, so they go with it.
    FittedSpell const stag = FitFormSpell(*ByForm(kStag));
    CHECK(stag.effects[1].aura == kAuraModIncreaseSpeed && stag.effects[1].basePoints + 1 == 50);
    CHECK(FitFormSpell(*ByForm(kCrow)).effects[1].aura == kAuraFeatherFall);
}

TEST(druid_form_bars_carry_the_forms_abilities)
{
    auto cat = BarLayout(*ByForm(kCat));
    CHECK(cat[0] == 9913 && cat[1] == 9827 && cat[2] == 9904);     // Prowl, Pounce, Rake
    CHECK(cat[3] == 22812);                                         // Barkskin
    CHECK(cat[4] == 0);
    auto bear = BarLayout(*ByForm(kDireBear));
    CHECK(bear[0] == 9881 && bear[1] == 8983 && bear[2] == 6795);
    CHECK(BarLayout(*ByForm(kBear)) == bear);
    CHECK(BarLayout(*ByForm(kStag))[0] == 16979);
    CHECK(BarLayout(*ByForm(kSpider))[0] == 12252);
    CHECK(BarLayout(*ByForm(kWorgen))[0] == 16979);
    // Travel Form is a core ability: Barkskin only.
    CHECK(BarLayout(*ByForm(kTravel))[0] == 22812);
    for (Form const& f : Forms())
    {
        std::vector<uint32_t> const a = Abilities(f);
        auto const bar = BarLayout(f);
        for (size_t i = 0; i < a.size(); ++i)
            CHECK(bar[i] == a[i]);
        CHECK(bar[a.size()] == 22812);
    }
}

TEST(druid_forms_wire_round_trips)
{
    std::vector<WireForm> const forms = WireForms();
    CHECK(forms.size() == Forms().size());
    std::vector<uint8_t> const body = EncodeForms(forms);
    CHECK(body[0] == kOpForms);
    std::vector<WireForm> back;
    CHECK(DecodeForms(body, back));
    CHECK(back.size() == forms.size());
    for (size_t i = 0; i < back.size() && i < forms.size(); ++i)
    {
        CHECK(back[i].spell == forms[i].spell);
        CHECK(back[i].form == forms[i].form);
        CHECK(back[i].bonusBar == forms[i].bonusBar);
        CHECK(back[i].stanceBarOrder == forms[i].stanceBarOrder);
        CHECK(back[i].formFlags == kFormFlagsShapeshift);
        CHECK(back[i].creatureType == forms[i].creatureType);
        CHECK(back[i].name == forms[i].name);
    }
    std::vector<uint8_t> cut(body.begin(), body.end() - 1);
    CHECK(!DecodeForms(cut, back));
}
