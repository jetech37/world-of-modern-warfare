/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Gathering.h"

namespace Fusion { namespace Gathering
{
    namespace
    {
        // A pick and an axe swing heavy, a knife quick, a hand pulls in between; the pole casts
        // slow. The client paces its swings by these (the server takes a quarter less).
        ToolDef const kTools[] = {
            {Ability::MiningPick, "Mining Pick", kSkillMining, kMiningPick, Work::Vein, 900},
            {Ability::SkinningKnife, "Skinning Knife", kSkillSkinning, kSkinningKnife, Work::Corpse, 600},
            {Ability::Herbalism, "Herbalism", kSkillHerbalism, 0, Work::Herb, 700},
            {Ability::FishingPole, "Fishing Pole", kSkillFishing, 0, Work::Water, 1200},
            {Ability::WoodcuttingAxe, "Woodcutting Axe", kSkillConstruction, kWoodcuttingAxe, Work::Lumber, 900},
            {Ability::BlacksmithHammer, "Blacksmith Hammer", kSkillBlacksmithing, kBlacksmithHammer, Work::Anvil, 800},
            {Ability::RepairHammer, "Repair Hammer", 0, kRepairHammer, Work::Piece, 900},
            // The instruments (Minstrelsy.h Instruments: the items 94101-94106).
            {Ability::HandDrum, "Hand Drum", kSkillMinstrelsy, 94101, Work::Song, 1000},
            {Ability::Lute, "Lute", kSkillMinstrelsy, 94102, Work::Song, 1000},
            {Ability::Flute, "Flute", kSkillMinstrelsy, 94103, Work::Song, 1000},
            {Ability::Harp, "Harp", kSkillMinstrelsy, 94104, Work::Song, 1000},
            {Ability::Bagpipes, "Bagpipes", kSkillMinstrelsy, 94105, Work::Song, 1000},
            {Ability::ThunderDrums, "Thunder Drums", kSkillMinstrelsy, 94106, Work::Song, 1000},
        };
    }

    ToolDef const* ToolInfo(uint32_t ability)
    {
        for (ToolDef const& t : kTools)
            if (uint32_t(t.ability) == ability)
                return &t;
        return nullptr;
    }

    bool Fits(uint32_t ability, Work w)
    {
        ToolDef const* t = ToolInfo(ability);
        return t && w != Work::None && t->work == w;
    }

    Outcome CheckTool(ToolDef const& t, Holder const& h)
    {
        if (h.gm)
            return Outcome::Struck;
        // The fishing pole: any pole, so the holder says whether one is there.
        bool const asksItem = t.item != 0 || t.ability == Ability::FishingPole;
        if (asksItem && !h.hasItem)
            return Outcome::NoTool;
        if (t.skill && !h.learned)
            return Outcome::NotLearned;
        return Outcome::Struck;
    }

    bool OnUtilityBar(ToolDef const& t, Holder const& h)
    {
        Holder real = h;
        real.gm = false;
        return CheckTool(t, real) == Outcome::Struck;
    }

    uint8_t StrikesFor(uint32_t ability, uint16_t required, uint16_t skill)
    {
        int const over = int(skill) - int(required);
        auto by = [over](uint8_t hard, uint8_t mid, uint8_t easy) {
            if (over < int(kHardMargin))
                return hard;
            return over < int(kEasyMargin) ? mid : easy;
        };
        switch (Ability(ability))
        {
            case Ability::MiningPick:
            case Ability::WoodcuttingAxe:
                return by(4, 3, 2);
            case Ability::Herbalism:
            case Ability::SkinningKnife:
                return by(3, 2, 1);
            default:
                return 1;
        }
    }

    uint16_t SkinningRequired(uint32_t level, uint16_t skill)
    {
        int const req = skill < 100 ? (int(level) - 10) * 10 : int(level) * 5;
        return uint16_t(req < 0 ? 0 : req > 0xFFFF ? 0xFFFF : req);
    }

    uint32_t MinGapMs(uint32_t ability)
    {
        ToolDef const* t = ToolInfo(ability);
        return t ? t->strikeMs * 3 / 4 : 0;
    }

    Step Strike(Progress& p, uint32_t tool, uint64_t target, uint8_t needed, uint32_t nowMs)
    {
        bool const same = p.done > 0 && p.tool == tool && p.target == target;
        uint32_t const since = nowMs - p.lastMs;
        // Any strike, here or at another target, waits out the tool's swing.
        if (p.lastMs != 0 && since < MinGapMs(tool))
            return Step::TooSoon;
        if (!same || since > kStrikeWindowMs)
        {
            p.tool = tool;
            p.target = target;
            p.done = 0;
        }
        p.lastMs = nowMs == 0 ? 1 : nowMs;
        ++p.done;
        if (p.done >= (needed ? needed : 1))
        {
            p.done = 0;
            p.target = 0;
            return Step::Complete;
        }
        return Step::Counted;
    }

    void ToolChanged(Progress& p)
    {
        uint32_t const last = p.lastMs;
        p = Progress{};
        p.lastMs = last;
    }
}}
