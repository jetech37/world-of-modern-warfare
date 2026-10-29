/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Rounds.h"

#include <algorithm>
#include <cmath>

namespace Fusion::Rounds
{
    RoundDef const& Def(Round r)
    {
        static RoundDef const kDefs[] = {
            {Round::None, "", "", 0, 0, 1.f, 1.f, 0.f, 0, 0.f, 0, 0.f, 0.f, 1.f, 0xFFFFFF},
            // Burns 30% of the hit again over three seconds; the round itself is softer.
            {Round::Incendiary, "Incendiary Rounds", "INCENDIARY", 3, 0, 0.92f, 1.f, 0.30f, 3000, 0.f, 0, 0.f, 0.f, 1.f,
                0xFF7A1F},
            // A short slow on every hit (vanilla's Chilled, frost: resistances count); the gun
            // cycles an eighth slower.
            {Round::Frost, "Frost Rounds", "FROST", 3, 0, 1.f, 1.12f, 0.f, 0, 0.30f, 1500, 0.f, 0.f, 1.f, 0x7FD4FF},
            // Half the bullet armor cut ignored; a tenth less against everything.
            {Round::ArmorPiercing, "Armor-Piercing Rounds", "ARMOR-PIERCING", 3, 0, 0.90f, 1.f, 0.f, 0, 0.f, 0, 0.50f, 0.f,
                1.f, 0xC8D0E0},
            // Burns mana as it takes health; timed, not by magazines.
            {Round::Arcane, "Arcane Rounds", "ARCANE", 0, 45, 0.92f, 1.f, 0.f, 0, 0.f, 0, 0.f, 0.60f, 1.f, 0xB04CFF},
            // 15% more against the undead and demons, a tenth less against anything else.
            {Round::Holy, "Holy Rounds", "HOLY", 3, 0, 0.90f, 1.f, 0.f, 0, 0.f, 0, 0.f, 0.f, 1.15f, 0xFFE680},
        };
        static_assert(sizeof(kDefs) / sizeof(kDefs[0]) == size_t(Round::Count), "one definition per round");
        size_t const i = size_t(r);
        return i < size_t(Round::Count) ? kDefs[i] : kDefs[0];
    }

    RoundItem ItemRound(uint32_t entry)
    {
        if (entry < kFirstItem || entry > kLastItem)
            return {};
        uint32_t const k = entry - kFirstItem;
        return {Round(1 + k / 2), uint8_t(1 + k % 2)};
    }

    uint32_t ItemFor(Round r, uint8_t grade)
    {
        if (r == Round::None || r >= Round::Count || grade < 1 || grade > 2)
            return 0;
        return kFirstItem + (uint32_t(r) - 1) * 2 + (grade - 1);
    }

    std::string ItemName(Round r, uint8_t grade)
    {
        if (!ItemFor(r, grade))
            return "";
        return std::string(grade == 2 ? "Greater " : "Lesser ") + Def(r).name;
    }

    bool Loaded::Active(uint32_t nowMs) const
    {
        if (round == Round::None)
            return false;
        return timed ? int32_t(untilMs - nowMs) > 0 : roundsLeft > 0;
    }

    Round Loaded::Fire(uint16_t gunId, uint32_t nowMs)
    {
        if (!Active(nowMs))
        {
            round = Round::None;
            return Round::None;
        }
        if (gunId != gun)
            return Round::None;
        Round const out = round;
        if (!timed && --roundsLeft == 0)
            round = Round::None;
        return out;
    }

    uint32_t Loaded::Left(uint32_t nowMs) const
    {
        if (!Active(nowMs))
            return 0;
        return timed ? untilMs - nowMs : roundsLeft;
    }

    Loaded Load(Round r, uint8_t grade, uint16_t gun, uint32_t magSize, uint32_t nowMs)
    {
        Loaded l;
        RoundDef const& d = Def(r);
        if (r == Round::None || r >= Round::Count || !gun)
            return l;
        uint32_t const times = grade >= 2 ? 2 : 1;
        l.round = r;
        l.gun = gun;
        if (d.seconds)
        {
            l.timed = true;
            l.untilMs = nowMs + d.seconds * 1000 * times;
        }
        else
            l.roundsLeft = std::max<uint32_t>(1, magSize) * d.magazines * times;
        return l;
    }

    HitEffect OnHit(Round r, TargetFacts const& t)
    {
        HitEffect e;
        RoundDef const& d = Def(r);
        e.damageMultiplier = d.damageMultiplier;
        switch (r)
        {
            case Round::Incendiary:
                if (!t.fireElemental)
                {
                    e.burnShare = d.burnShare;
                    e.burnMs = d.burnMs;
                }
                break;
            case Round::Frost:
                e.slow = !t.slowImmune;
                break;
            case Round::ArmorPiercing:
                e.armorBypass = d.armorBypass;
                break;
            case Round::Arcane:
                e.manaBurnShare = t.maxMana ? d.manaBurnShare : 0.f;
                break;
            case Round::Holy:
                if (t.type == CreatureType::Undead || t.type == CreatureType::Demon)
                    e.damageMultiplier = d.holyMultiplier;
                break;
            default:
                break;
        }
        return e;
    }

    float ArmorBypassScale(float armorCut, float bypass)
    {
        float const cut = std::clamp(armorCut, 0.f, 0.95f);
        if (cut <= 0.f)
            return 1.f;
        float const kept = cut * (1.f - std::clamp(bypass, 0.f, 1.f));
        return (1.f - kept) / (1.f - cut);
    }

    uint32_t ManaBurned(float healthShare, uint32_t maxMana, float share)
    {
        float const v = std::clamp(healthShare, 0.f, 1.f) * float(maxMana) * std::max(0.f, share);
        return uint32_t(std::lround(v));
    }

    void AddBurn(std::vector<Burn>& burns, uint64_t target, uint16_t weapon, float total, uint32_t durationMs, uint32_t nowMs)
    {
        if (total <= 0.f || durationMs == 0)
            return;
        uint8_t const ticks = uint8_t(std::clamp<uint32_t>(durationMs / kBurnTickMs, 1, 255));
        for (Burn& b : burns)
            if (b.target == target)
            {
                b.left = std::max(b.left, total);
                b.ticks = ticks;
                b.weapon = weapon;
                return;     // its next tick stays where it was
            }
        burns.push_back({target, weapon, total, ticks, nowMs + kBurnTickMs});
    }

    std::vector<BurnTick> TickBurns(std::vector<Burn>& burns, uint32_t nowMs)
    {
        std::vector<BurnTick> out;
        for (Burn& b : burns)
        {
            while (b.ticks && int32_t(nowMs - b.nextMs) >= 0)
            {
                float const dmg = b.left / float(b.ticks);
                b.left -= dmg;
                --b.ticks;
                b.nextMs += kBurnTickMs;
                out.push_back({b.target, b.weapon, dmg});
            }
        }
        burns.erase(std::remove_if(burns.begin(), burns.end(), [](Burn const& b) { return b.ticks == 0; }), burns.end());
        return out;
    }

    namespace
    {
        // Vanilla's enchanting materials.
        constexpr uint32_t StrangeDust = 10940, SoulDust = 11083, VisionDust = 11137, DreamDust = 11176,
            IllusionDust = 16204;
        constexpr uint32_t LesserMagicEssence = 10938, LesserAstralEssence = 10998, LesserMysticEssence = 11134,
            LesserNetherEssence = 11174, GreaterNetherEssence = 11175, GreaterEternalEssence = 16203;
        constexpr uint32_t SmallGlowingShard = 11138, LargeGlowingShard = 11139, SmallRadiantShard = 11177,
            LargeRadiantShard = 11178, SmallBrilliantShard = 14343;

        Recipe Make(uint32_t id, Round r, uint8_t grade, uint16_t required, std::vector<Construction::Material> reagents,
            uint32_t copper)
        {
            Recipe out;
            out.craft.id = id;
            out.craft.skillLine = kSkillEnchanting;
            // Enchanting's own spacing for its middle recipes: yellow 20 on, green 35, gray 50.
            out.craft.skill = {required, uint16_t(required + 20), uint16_t(required + 35), uint16_t(required + 50)};
            out.craft.product = ItemFor(r, grade);
            out.craft.productCount = 2;
            out.craft.reagents = std::move(reagents);
            out.trainCopper = copper;
            return out;
        }
    }

    std::vector<Recipe> const& Recipes()
    {
        static std::vector<Recipe> const kRecipes = {
            Make(62600, Round::Incendiary, 1, 60, {{StrangeDust, 2}, {LesserMagicEssence, 1}}, 2000),
            Make(62601, Round::Frost, 1, 110, {{SoulDust, 2}, {LesserAstralEssence, 1}}, 5000),
            Make(62602, Round::ArmorPiercing, 1, 150, {{VisionDust, 2}, {SmallGlowingShard, 1}}, 8000),
            Make(62603, Round::Arcane, 1, 180, {{VisionDust, 2}, {LesserMysticEssence, 1}}, 12000),
            Make(62604, Round::Holy, 1, 210, {{DreamDust, 2}, {LesserNetherEssence, 1}, {LargeGlowingShard, 1}}, 18000),
            Make(62605, Round::Incendiary, 2, 225, {{DreamDust, 3}, {GreaterNetherEssence, 1}}, 25000),
            Make(62606, Round::Frost, 2, 235, {{DreamDust, 3}, {SmallRadiantShard, 1}}, 30000),
            Make(62607, Round::ArmorPiercing, 2, 250, {{IllusionDust, 2}, {LargeRadiantShard, 1}}, 35000),
            Make(62608, Round::Arcane, 2, 265, {{IllusionDust, 2}, {GreaterEternalEssence, 1}}, 40000),
            Make(62609, Round::Holy, 2, 285, {{IllusionDust, 3}, {GreaterEternalEssence, 1}, {SmallBrilliantShard, 1}}, 50000),
        };
        return kRecipes;
    }

    Recipe const* FindRecipe(uint32_t id)
    {
        for (Recipe const& r : Recipes())
            if (r.craft.id == id)
                return &r;
        return nullptr;
    }

    bool IsEnchantingTrainer(uint8_t trainerType, std::string const& subName)
    {
        // TRAINER_TYPE_TRADESKILLS.
        return trainerType == 2 && subName.find("Enchant") != std::string::npos;
    }

    LearnCheck CanLearn(Recipe const& r, bool known, bool hasEnchanting, uint16_t skill, uint32_t money, bool gm)
    {
        if (known)
            return LearnCheck::Known;
        if (gm)
            return LearnCheck::Ok;
        if (!hasEnchanting)
            return LearnCheck::NoEnchanting;
        if (skill < r.craft.skill.required)
            return LearnCheck::SkillTooLow;
        if (money < r.trainCopper)
            return LearnCheck::NotEnoughMoney;
        return LearnCheck::Ok;
    }

    std::vector<uint8_t> EncodeLoaded(Loaded const& l, uint32_t nowMs)
    {
        bool const on = l.Active(nowMs);
        uint32_t const left = on ? l.Left(nowMs) : 0;
        uint16_t const gun = on ? l.gun : 0;
        return {kOpLoaded, uint8_t(on ? l.round : Round::None), uint8_t(gun), uint8_t(gun >> 8), uint8_t(on && l.timed),
            uint8_t(left), uint8_t(left >> 8), uint8_t(left >> 16), uint8_t(left >> 24)};
    }

    bool DecodeLoaded(std::vector<uint8_t> const& b, Loaded& out)
    {
        if (b.size() != 9 || b[0] != kOpLoaded || b[1] >= uint8_t(Round::Count))
            return false;
        out = {};
        out.round = Round(b[1]);
        out.gun = uint16_t(b[2] | (b[3] << 8));
        out.timed = b[4] != 0;
        uint32_t const left = uint32_t(b[5]) | (uint32_t(b[6]) << 8) | (uint32_t(b[7]) << 16) | (uint32_t(b[8]) << 24);
        // A timed load decodes as milliseconds from now.
        (out.timed ? out.untilMs : out.roundsLeft) = left;
        return true;
    }
}
