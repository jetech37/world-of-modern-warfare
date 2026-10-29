/*
 * World of Modern Warfare: the druid's shapeshift forms. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/DruidForms.h"

#include "fusion/Classes.h"

namespace Fusion::DruidForms
{
    namespace
    {
        // One row: the form, its spell, its name, its bar page and its front limbs. The rest is
        // set by the row's modifiers. (The client's crate::forms reads these lines back in a
        // test: keep each `F(...)` on one line.)
        Form F(uint8_t form, uint32_t spell, char const* name, uint8_t bonusBar, Limbs limbs)
        {
            Form f;
            f.form = form;
            f.spell = spell;
            f.name = name;
            f.bonusBar = bonusBar;
            f.limbs = limbs;
            return f;
        }

        Form Stock(Form f, uint32_t display, int8_t order, uint16_t option)
        {
            f.stock = true;
            f.display = display;
            f.stanceBarOrder = order;
            f.option = option;
            return f;
        }

        Form New(Form f, uint32_t creature, uint32_t display, int8_t order, uint16_t option)
        {
            f.fit = true;
            f.creature = creature;
            f.display = display;
            f.stanceBarOrder = order;
            f.option = option;
            return f;
        }

        using Classes::OptionId;
        using Classes::kDruid;

        std::vector<Form> MakeForms()
        {
            std::vector<Form> rows;
            rows.push_back(Stock(F(kCat, 768, "Cat Form", 1, Limbs::Paws), 892, 2, OptionId(kDruid, 1, 0)));
            rows.push_back(Stock(F(kBear, 5487, "Bear Form", 3, Limbs::Paws), 2281, 0, OptionId(kDruid, 2, 0)));
            rows.push_back(Stock(F(kDireBear, 9634, "Dire Bear Form", 3, Limbs::Paws), 2281, 0, OptionId(kDruid, 2, 0)));
            rows.push_back(Stock(F(kTravel, 783, "Travel Form", 5, Limbs::Paws), 632, 3, OptionId(kDruid, 0, 2)));
            rows.push_back(Stock(F(kAquatic, 1066, "Aquatic Form", 6, Limbs::Flippers), 2428, 1, 0));
            rows.push_back(Stock(F(kMoonkin, 24858, "Moonkin Form", 4, Limbs::Claws), 15374, 4, 0));
            // Tree of Life is no form: the druid keeps casting in it, in a treant's look (its refit
            // and FusionClassAbilities.cpp), so it has no page and no shapeshift.
            {
                Form f = New(F(kStag, 21936, "Stag Form", 7, Limbs::Hooves), 8759, 8013, 5, OptionId(kDruid, 3, 0));
                f.speedPct = 50;
                rows.push_back(f);
            }
            {
                Form f = New(F(kCrow, 8153, "Crow Form", 8, Limbs::Talons), 5918, 4566, 6, OptionId(kDruid, 4, 0));
                f.slowFall = true;
                rows.push_back(f);
            }
            rows.push_back(New(F(kSpider, 24084, "Spider Form", 9, Limbs::Legs), 14967, 15226, 7, OptionId(kDruid, 5, 0)));
            {
                Form f = New(F(kWorgen, 7621, "Worgen Form", 10, Limbs::ClawedHands), 3857, 202, 8, OptionId(kDruid, 6, 0));
                f.limbsAlways = true;
                f.creatureType = 7;
                rows.push_back(f);
            }
            return rows;
        }
    }

    std::vector<Form> const& Forms()
    {
        static std::vector<Form> const rows = MakeForms();
        return rows;
    }

    Form const* ByForm(uint8_t form)
    {
        if (!form)
            return nullptr;
        for (Form const& f : Forms())
            if (f.form == form)
                return &f;
        return nullptr;
    }

    Form const* BySpell(uint32_t spell)
    {
        if (!spell)
            return nullptr;
        for (Form const& f : Forms())
            if (f.spell == spell)
                return &f;
        return nullptr;
    }

    bool IsFitted(uint32_t spell)
    {
        Form const* f = BySpell(spell);
        return f && f->fit;
    }

    bool BlocksGuns(uint8_t form)
    {
        Form const* f = ByForm(form);
        return f && !f->guns;
    }

    std::vector<uint32_t> Abilities(Form const& f)
    {
        std::vector<uint32_t> out;
        Classes::Option const* o = f.option ? Classes::FindOption(f.option) : nullptr;
        // A core form (Travel) has no abilities of its own.
        if (!o || o->tier == Classes::kCoreTier)
            return out;
        for (size_t i = 1; i < o->spells.size(); ++i)
            if (o->spells[i])
                out.push_back(o->spells[i]);
        return out;
    }

    std::array<uint32_t, kSlotsPerPage> BarLayout(Form const& f)
    {
        std::array<uint32_t, kSlotsPerPage> bar{};
        size_t slot = 0;
        for (uint32_t s : Abilities(f))
            if (slot < bar.size())
                bar[slot++] = s;
        // Barkskin works in every form ("not affected by forms").
        std::array<Classes::Option const*, 3> const core = Classes::CoreOf(kDruid);
        if (core[0] && slot < bar.size())
            bar[slot++] = core[0]->spells[0];
        return bar;
    }

    FittedSpell FitFormSpell(Form const& f)
    {
        FittedSpell s;
        s.effects[0] = {kEffectApplyAura, kAuraModShapeshift, int32_t(f.form), -1, kTargetCaster};
        size_t i = 1;
        if (f.speedPct > 0)
            s.effects[i++] = {kEffectApplyAura, kAuraModIncreaseSpeed, 0, f.speedPct - 1, kTargetCaster};
        if (f.slowFall)
            s.effects[i++] = {kEffectApplyAura, kAuraFeatherFall, 0, -1, kTargetCaster};
        s.attributes = kFormAttributes;
        s.attributesEx = kFormAttributesEx;
        s.durationIndex = f.durationMs == 30000 ? kDuration30s : kDurationInfinite;
        // Going under water ends a land form, as Travel Form's 0x80 does; the crow too.
        s.auraInterruptFlags = (f.form == kStag || f.form == kCrow) ? 0x80u : 0u;
        s.activeIconId = kActiveIconForm;
        return s;
    }

    bool IsCleanFormSpell(FittedSpell const& s, uint8_t form)
    {
        int shapeshifts = 0;
        for (FittedEffect const& e : s.effects)
        {
            if (!e.effect)
                continue;
            if (e.target != kTargetCaster)
                return false;
            if (e.effect != kEffectApplyAura)
                return false;
            switch (e.aura)
            {
                case kAuraModShapeshift:
                    if (e.misc != form)
                        return false;
                    ++shapeshifts;
                    break;
                case kAuraModStun:
                case kAuraTransform:
                case kAuraModCharm:
                    return false;
                default:
                    break;
            }
        }
        return shapeshifts == 1 && s.dispel == 0;
    }

    std::vector<WireForm> WireForms()
    {
        std::vector<WireForm> out;
        for (Form const& f : Forms())
        {
            WireForm w;
            w.spell = f.spell;
            w.form = f.form;
            w.bonusBar = f.bonusBar;
            w.stanceBarOrder = f.stanceBarOrder;
            w.formFlags = kFormFlagsShapeshift;
            w.creatureType = f.creatureType;
            w.name = f.name;
            out.push_back(w);
        }
        return out;
    }

    std::vector<uint8_t> EncodeForms(std::vector<WireForm> const& forms)
    {
        std::vector<uint8_t> b;
        b.push_back(kOpForms);
        size_t const n = std::min<size_t>(forms.size(), 255);
        b.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            WireForm const& w = forms[i];
            for (int k = 0; k < 4; ++k)
                b.push_back(uint8_t(w.spell >> (8 * k)));
            b.push_back(w.form);
            b.push_back(w.bonusBar);
            b.push_back(uint8_t(w.stanceBarOrder));
            b.push_back(w.formFlags);
            b.push_back(uint8_t(w.creatureType));
            size_t const len = std::min<size_t>(w.name.size(), 255);
            b.push_back(uint8_t(len));
            b.insert(b.end(), w.name.begin(), w.name.begin() + len);
        }
        return b;
    }

    bool DecodeForms(std::vector<uint8_t> const& body, std::vector<WireForm>& out)
    {
        out.clear();
        if (body.size() < 2 || body[0] != kOpForms)
            return false;
        size_t at = 2;
        for (uint8_t i = 0; i < body[1]; ++i)
        {
            if (at + 10 > body.size())
                return false;
            WireForm w;
            w.spell = uint32_t(body[at]) | uint32_t(body[at + 1]) << 8 | uint32_t(body[at + 2]) << 16
                | uint32_t(body[at + 3]) << 24;
            w.form = body[at + 4];
            w.bonusBar = body[at + 5];
            w.stanceBarOrder = int8_t(body[at + 6]);
            w.formFlags = body[at + 7];
            w.creatureType = int8_t(body[at + 8]);
            size_t const len = body[at + 9];
            at += 10;
            if (at + len > body.size())
                return false;
            w.name.assign(body.begin() + at, body.begin() + at + len);
            at += len;
            out.push_back(w);
        }
        return at == body.size();
    }
}
