/*
 * World of Modern Warfare: the class options' audit (design/CLASS_CATALOG.md section 15): every
 * core ability and option resolves to a spell the game fits to the kit system, has a status, a
 * valid cost and a way out of whatever state it starts. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Classes.h"

#include <algorithm>
#include <set>
#include <string>

using namespace Fusion;
using namespace Fusion::Classes;

namespace
{
    std::array<uint8_t, 9> const kAll = {kWarrior, kPaladin, kHunter, kRogue, kPriest, kShaman, kMage, kWarlock, kDruid};

    // A kit holding just this option (core abilities: none).
    Picks KitWith(Option const& o)
    {
        Picks p{};
        if (o.tier >= 1 && o.tier <= kTiers)
            p[o.tier - 1] = o.id;
        return p;
    }

    bool OnBar(std::array<uint32_t, 12> const& bar, uint32_t spell)
    {
        return std::find(bar.begin(), bar.end(), spell) != bar.end();
    }
}

TEST(class_audit_has_a_row_for_every_option)
{
    CHECK(Specs().size() == Catalog().size());
    std::set<uint16_t> seen;
    for (size_t i = 0; i < Catalog().size() && i < Specs().size(); ++i)
    {
        Option const& o = Catalog()[i];
        Spec const& s = Specs()[i];
        CHECK(s.id == o.id);                // in catalog order
        CHECK(seen.insert(s.id).second);
        CHECK(SpecOf(o.id) == &s);
        // Every option resolves to a spell, its name is the catalog's.
        CHECK(o.spells[0] != 0);
        CHECK(std::string(o.name).size() > 0);
        CHECK(s.note != nullptr);
    }
    CHECK(SpecOf(0) == nullptr && SpecOf(171) == nullptr);
    CHECK(std::string(StatusName(Status::Partial)) == "partial");
}

TEST(class_audit_says_what_is_missing)
{
    for (Spec const& s : Specs())
    {
        // Anything short of the catalog says what it lacks (or why it is unavailable).
        if (s.status != Status::Working)
            CHECK(std::string(s.note).size() > 0);
        // Notes are one tooltip line, not a paragraph.
        CHECK(std::string(s.note).size() < 120);
    }
}

TEST(class_audit_every_state_has_a_way_out)
{
    for (Option const& o : Catalog())
    {
        Spec const* s = SpecOf(o.id);
        if (!s)
            continue;
        // A passive has no button and nothing to end; a form is left by its button.
        CHECK((o.kind == Kind::Passive) == (s->ends == Ends::Passive));
        CHECK((o.kind == Kind::Form) == (s->ends == Ends::Form) || (o.id == OptionId(kDruid, 0, 2) && s->ends == Ends::Form));
        // What runs out says when; nothing else carries a duration.
        if (s->ends == Ends::Timed)
            CHECK(s->durationMs > 0 && s->durationMs <= 600000);
        else
            CHECK(s->durationMs == 0);
        // A toggle, a channel or a form is ended with its own button again: it must have one on
        // the kit's bar whenever it is in the kit.
        if (s->ends == Ends::Toggle || s->ends == Ends::Channel || s->ends == Ends::Form)
            CHECK(OnBar(BarLayout(o.cls, KitWith(o)), o.spells[0]));
        // Every button is limited somehow: a cooldown or a resource (toggles by a short one).
        if (s->ends != Ends::Passive && s->ends != Ends::Form)
            CHECK(s->cooldownMs > 0 || s->cost > 0);
        CHECK(s->cooldownMs <= 900000);
    }
}

TEST(class_audit_costs_are_the_archetypes)
{
    for (Option const& o : Catalog())
    {
        Spec const* s = SpecOf(o.id);
        if (!s)
            continue;
        if (s->resource == Resource::None)
        {
            CHECK(s->cost == 0);
        }
        else
        {
            // Spent from the option's own archetype's meter, never more than it can hold.
            std::vector<Resource> const own = ArchetypeResources(o.cls, o.archetype);
            CHECK(o.tier != kCoreTier);
            CHECK(std::find(own.begin(), own.end(), s->resource) != own.end());
            CHECK(s->cost > 0);
            CHECK(s->cost <= ResourceMax(s->resource, o.cls, KitWith(o)));
            // The kit with it has that meter.
            std::vector<Resource> const kit = KitResources(o.cls, KitWith(o));
            CHECK(std::find(kit.begin(), kit.end(), s->resource) != kit.end());
        }
        // Rage is the warrior's.
        if (s->rage)
            CHECK(o.cls == kWarrior && s->rage <= 100);
    }
}

TEST(class_audit_every_spell_is_fitted_to_the_kit_system)
{
    std::set<uint32_t> refits;
    for (Refit const& r : Refits())
    {
        CHECK(r.spell != 0);
        CHECK(refits.insert(r.spell).second);           // one refit per spell
        CHECK(RefitOf(r.spell) == &r);
        CHECK(r.cooldownMs >= -1 && r.castIndex >= -1 && r.castIndex != 0 && r.rangeIndex >= -1 && r.rangeIndex != 0);
        // A replaced donor may lose its channel; a vanilla channel keeps it.
        if (r.flags & REFIT_NOT_CHANNEL)
            CHECK((r.flags & REFIT_REPLACE) != 0);
        // A spell lands at the feet or on oneself, not both; one effect or all replaced.
        CHECK(!((r.flags & REFIT_AT_FEET) && (r.flags & REFIT_SELF)));
        CHECK(!((r.flags & REFIT_REPLACE) && (r.flags & (REFIT_REPLACE_FIRST | REFIT_REPLACE_SECOND | REFIT_REPLACE_THIRD))));
        // Rage stays the warrior's: only a form's own abilities (the bear's) are made free of it.
        if (r.flags & REFIT_FREE)
            CHECK((r.flags & REFIT_KEEP_STANCE) != 0);
    }
    CHECK(RefitOf(0) == nullptr);
    for (Option const& o : Catalog())
    {
        Spec const* s = SpecOf(o.id);
        // Every button's spell is fitted (the forms' own abilities are the forms' rework).
        if (!LearnsSpells(o) || o.kind == Kind::Passive)
            continue;
        if (o.kind == Kind::Form && o.cls == kDruid && o.tier != kCoreTier)
            continue;
        Refit const* r = RefitOf(o.spells[0]);
        CHECK(r != nullptr);
        // The refit's cooldown is the audit's (Rekindle keeps Redemption's and gets its own in
        // the kit).
        if (r && s && r->cooldownMs >= 0)
            CHECK(uint32_t(r->cooldownMs) == s->cooldownMs);
    }
}

TEST(class_casters_know_their_basic_spell_from_level_one)
{
    CHECK(BasicSpellOf(kPaladin) == 10314);     // Exorcism
    CHECK(BasicSpellOf(kPriest) == 10934);      // Smite
    CHECK(BasicSpellOf(kShaman) == 15208);      // Lightning Bolt
    CHECK(BasicSpellOf(kMage) == 25306);        // Fireball
    CHECK(BasicSpellOf(kWarlock) == 25307);     // Shadow Bolt
    CHECK(BasicSpellOf(kDruid) == 9912);        // Wrath
    CHECK(BasicSpellOf(kWarrior) == 0 && BasicSpellOf(kHunter) == 0 && BasicSpellOf(kRogue) == 0);
    for (uint8_t cls : kAll)
    {
        uint32_t const basic = BasicSpellOf(cls);
        auto const bar = BarLayout(cls, Picks{});
        if (!basic)
        {
            // No basic spell: slot 3 is the first pick's, or the Utility's.
            CHECK(bar[3] == (UtilityOf(cls).empty() ? 0u : UtilityOf(cls)[0]));
            continue;
        }
        // Fitted (no mana, its cooldown), on the bar after the core in every kit, no core ability.
        CHECK(RefitOf(basic) != nullptr);
        CHECK(bar[3] == basic);
        Option const* o = OptionBySpell(cls, basic);
        CHECK(!o || o->tier != kCoreTier);
        // A kit picking the option of the same spell has one button for it.
        if (o)
        {
            auto const both = BarLayout(cls, KitWith(*o));
            CHECK(std::count(both.begin(), both.end(), basic) == 1);
        }
    }
    // Picks still follow the basic spell: a mage's six picks, Fireball once.
    auto const mage = BarLayout(kMage, Picks{812, 821, 0, 0, 0, 0});
    CHECK(mage[3] == 25306 && mage[4] == 10199 && mage[5] == 25304);
    // A warrior's picks still start at 3.
    CHECK(BarLayout(kWarrior, Picks{111, 0, 0, 0, 0, 0})[3] == 23894);
}

TEST(class_unavailable_options_are_never_sold_or_picked)
{
    for (Spec const& s : Specs())
    {
        Option const* o = FindOption(s.id);
        if (!o || o->tier == kCoreTier)
            continue;
        std::set<uint16_t> learned = {s.id};
        Picks p{};
        p[o->tier - 1] = s.id;
        if (s.status == Status::Unavailable)
        {
            CHECK(!Available(s.id));
            CHECK(CanBuy(o->cls, 60, 10000000, {}, s.id) == BuyError::Unavailable);
            CHECK(ValidatePicks(o->cls, 60, learned, p) == PickError::Unavailable);
            CHECK(ValidatePicks(o->cls, 60, learned, p, nullptr, true) == PickError::None);    // a GM tests it
            CHECK(Sanitized(o->cls, 60, learned, p)[o->tier - 1] == 0);
        }
        else
        {
            CHECK(Available(s.id));
            CHECK(CanBuy(o->cls, 60, 10000000, {}, s.id) == BuyError::None);
            CHECK(ValidatePicks(o->cls, 60, learned, p) == PickError::None);
        }
    }
    CHECK(!Available(0) && !Available(5000));
    // The talent page names an option as the catalog does (an unavailable one says so).
    for (uint8_t cls : kAll)
        for (PageEntry const& e : MakePage(cls).entries)
        {
            Option const* o = FindOption(e.id);
            CHECK(o && (Available(e.id) ? e.name == o->name : e.name == std::string(o->name) + " (unavailable)"));
        }
}

TEST(class_retired_donors_are_no_class_spell)
{
    // The warrior's old Chain Hook spell goes from its spellbook; the hunter's hook keeps it.
    CHECK(RetiredSpells(kWarrior).size() == 1 && RetiredSpells(kWarrior)[0] == 13608);
    CHECK(OptionBySpell(kWarrior, 13608) == nullptr);
    CHECK(OptionBySpell(kWarrior, 28337) && OptionBySpell(kWarrior, 28337)->id == 122);
    CHECK(OptionBySpell(kHunter, 13608) && OptionBySpell(kHunter, 13608)->id == 341);
    CHECK(RetiredSpells(kHunter).empty());
    // Death's Harvest has its own icon, so the trainer tells it from Corpse Explosion.
    CHECK(FindOption(953)->spells[0] != FindOption(923)->spells[0]);
    for (uint8_t cls : kAll)
        for (uint32_t s : RetiredSpells(cls))
            CHECK(OptionBySpell(cls, s) == nullptr && BasicSpellOf(cls) != s);
}

TEST(class_options_have_distinct_spells_for_the_trainer)
{
    // The trainer sells by spell: no two options of a class may show the same one.
    for (uint8_t cls : kAll)
    {
        std::set<uint32_t> shown;
        for (Option const* o : OptionsOf(cls))
            CHECK(shown.insert(o->spells[0]).second);
    }
}
