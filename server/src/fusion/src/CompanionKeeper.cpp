/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/CompanionKeeper.h"

#include <algorithm>
#include <cstring>

namespace Fusion::Companions::Keeper
{
    // ── Prices ──────────────────────────────────────────────────────────────────────────────

    uint32_t PurchasePrice(CompanionSpecies const& sp, uint8_t stage, uint8_t level)
    {
        uint32_t price = kBuyBase + uint32_t(level) * (uint32_t(stage) + 1) * kBuyPerLevelStage;
        if (stage < sp.stageCount && stage < kMaxStages && sp.stages[stage].rideable)
            price = std::max(price, kMountFloor);
        return price;
    }

    uint32_t ChangePrice(CompanionSpecies const& sp, CompanionState const& from, uint8_t stage, uint8_t level)
    {
        uint32_t const was = PurchasePrice(sp, from.stage, from.level);
        uint32_t const now = PurchasePrice(sp, stage, level);
        return kServiceFee + (now > was ? now - was : 0);
    }

    uint32_t PickPrice(CompanionState const& s, uint8_t stage)
    {
        return stage < kMaxStages && s.picks[stage] ? kRepickFee : 0;
    }

    // ── Limits ──────────────────────────────────────────────────────────────────────────────

    uint8_t LevelCap(Customer const& who)
    {
        return who.gm ? kMaxLevel : std::max<uint8_t>(1, std::min(who.level, kMaxLevel));
    }

    uint8_t MinLevelForStage(CompanionSpecies const& sp, uint8_t stage)
    {
        if (stage == 0 || stage > kMaxStages - 1)
            return 1;
        return std::max<uint8_t>(1, sp.evolutions[stage - 1].level);
    }

    std::vector<uint8_t> StagesFor(CompanionSpecies const& sp, Customer const& who)
    {
        std::vector<uint8_t> out;
        uint8_t const cap = LevelCap(who);
        for (uint8_t st = 0; st < sp.stageCount && st < kMaxStages; ++st)
            if (who.gm || MinLevelForStage(sp, st) <= cap)
                out.push_back(st);
        return out;
    }

    std::vector<uint8_t> LevelPresets(CompanionSpecies const& sp, uint8_t stage, Customer const& who)
    {
        std::vector<uint8_t> out;
        if (stage >= sp.stageCount)
            return out;
        uint8_t const lo = who.gm ? 1 : MinLevelForStage(sp, stage);
        uint8_t const hi = LevelCap(who);
        if (lo > hi)
            return out;
        auto add = [&](uint8_t l) {
            if (l >= lo && l <= hi && std::find(out.begin(), out.end(), l) == out.end())
                out.push_back(l);
        };
        add(1);
        for (uint8_t l = 10; l <= kMaxLevel; l += 10)
            add(l);
        add(lo);
        add(MinLevelForStage(sp, stage));
        add(hi);
        std::sort(out.begin(), out.end());
        return out;
    }

    namespace
    {
        // Level and stage checks shared by buying and changing. `keep` is a level the companion
        // already has (a traded one may stay above its owner); 0 for a purchase.
        Refusal CheckStageLevel(CompanionSpecies const& sp, uint8_t stage, uint8_t level, Customer const& who,
            uint8_t keep)
        {
            if (stage >= sp.stageCount || stage >= kMaxStages)
                return Refusal::BadStage;
            if (level < 1 || level > kMaxLevel)
                return Refusal::BadLevel;
            if (who.gm)
                return Refusal::None;
            if (level > LevelCap(who) && level != keep)
                return Refusal::AboveOwner;
            if (level < MinLevelForStage(sp, stage))
                return Refusal::StageNeedsLevel;
            return Refusal::None;
        }
    }

    Refusal CanBuy(CompanionSpecies const& sp, uint8_t stage, uint8_t level, Customer const& who)
    {
        if (!who.gm && !kPlayersBuy)
            return Refusal::GmOnly;
        Refusal const r = CheckStageLevel(sp, stage, level, who, 0);
        if (r != Refusal::None)
            return r;
        if (!who.gm && who.money < PurchasePrice(sp, stage, level))
            return Refusal::NoMoney;
        return Refusal::None;
    }

    Refusal CanChange(CompanionSpecies const& sp, CompanionState const& s, uint8_t stage, uint8_t level,
        Customer const& who)
    {
        if (!who.gm && !kPlayersBuy)
            return Refusal::GmOnly;
        Refusal const r = CheckStageLevel(sp, stage, level, who, s.level);
        if (r != Refusal::None)
            return r;
        if (stage == s.stage && level == s.level)
            return Refusal::Unchanged;
        if (!who.gm && who.money < ChangePrice(sp, s, stage, level))
            return Refusal::NoMoney;
        return Refusal::None;
    }

    void ApplyChange(CompanionSpecies const& sp, CompanionState& s, uint8_t stage, uint8_t level)
    {
        if (level != s.level)
        {
            s.level = std::max<uint8_t>(1, std::min(level, kMaxLevel));
            s.xp = 0;
        }
        stage = std::min<uint8_t>(stage, uint8_t(std::max<uint8_t>(sp.stageCount, 1) - 1));
        if (stage != s.stage)
        {
            s.stage = stage;
            s.evolveKills = 0;
            for (uint8_t st = uint8_t(stage + 1); st < kMaxStages; ++st)
                s.picks[st] = 0;
        }
    }

    Refusal CanPick(CompanionSpecies const& sp, CompanionState const& s, uint8_t stage, uint8_t option,
        Customer const& who)
    {
        if (stage >= kMaxStages || stage >= sp.stageCount)
            return Refusal::BadStage;
        if (stage > s.stage)
            return Refusal::NotReached;
        if (option < 1 || option > 2 || !MovesetData(MovesetOf(sp)).stages[stage][option - 1].Exists())
            return Refusal::NoAbility;
        if (s.picks[stage] == option)
            return Refusal::Unchanged;
        if (!who.gm && who.money < PickPrice(s, stage))
            return Refusal::NoMoney;
        return Refusal::None;
    }

    bool ApplyPick(CompanionSpecies const& sp, CompanionState& s, uint8_t stage, uint8_t option)
    {
        Customer admin;
        admin.gm = true;
        Refusal const r = CanPick(sp, s, stage, option, admin);
        if (r == Refusal::Unchanged)
            return true;
        if (r != Refusal::None)
            return false;
        s.picks[stage] = option;
        return true;
    }

    Refusal CanToggleTrait(CompanionSpecies const& sp, CompanionState const& s, Trait t, Customer const& who)
    {
        if (!who.gm && !kPlayersEditTraits)
            return Refusal::GmOnly;
        if (t == Trait::None || uint8_t(t) > kTraitCount)
            return Refusal::Ineligible;
        if (HasTrait(s.traits, t))
            return Refusal::None;
        if (!TraitEligible(t, sp))
            return Refusal::Ineligible;
        std::vector<Trait> const have = TraitList(s.traits);
        if (have.size() >= kMaxTraits)
            return Refusal::TooMany;
        for (Trait o : have)
            if (TraitsConflict(o, t))
                return Refusal::Conflict;
        if (!ValidTraits(s.traits | TraitBit(t), sp))
            return Refusal::Conflict;
        return Refusal::None;
    }

    void ApplyToggleTrait(CompanionSpecies const& sp, CompanionState& s, Trait t)
    {
        bool const had = HasTrait(s.traits, t);
        s.traits ^= TraitBit(t);
        s.rarity = uint8_t(RarityOf(s.traits));
        if (t == Trait::TwinSouled)
            s.twinStage = had ? 0 : uint8_t(std::min<uint8_t>(uint8_t(s.stage + 1), std::max<uint8_t>(sp.stageCount, 1)));
    }

    std::vector<Trait> AddableTraits(CompanionSpecies const& sp, CompanionState const& s)
    {
        Customer admin;
        admin.gm = true;
        std::vector<Trait> out;
        for (uint8_t i = 1; i <= kTraitCount; ++i)
            if (!HasTrait(s.traits, Trait(i)) && CanToggleTrait(sp, s, Trait(i), admin) == Refusal::None)
                out.push_back(Trait(i));
        return out;
    }

    Refusal CanSetTwinStage(CompanionSpecies const& sp, CompanionState const& s, uint8_t twinStage,
        Customer const& who)
    {
        if (!who.gm && !kPlayersEditTraits)
            return Refusal::GmOnly;
        if (!HasTrait(s.traits, Trait::TwinSouled))
            return Refusal::NotTwin;
        if (twinStage < 1 || twinStage > sp.stageCount || twinStage > kMaxStages)
            return Refusal::BadStage;
        if (twinStage == s.twinStage)
            return Refusal::Unchanged;
        return Refusal::None;
    }

    // ── Browsing ────────────────────────────────────────────────────────────────────────────

    char const* FamilyName(Family f)
    {
        switch (f)
        {
            case Family::Critter: return "Critter";
            case Family::Dragon: return "Dragon";
            case Family::FaerieDragon: return "Faerie Dragon";
            case Family::Wolf: return "Wolf";
            case Family::Cat: return "Cat";
            case Family::Bear: return "Bear";
            case Family::Spider: return "Spider";
            case Family::Raptor: return "Raptor";
            case Family::Crocolisk: return "Crocolisk";
            case Family::Gorilla: return "Gorilla";
            case Family::Owl: return "Owl";
            case Family::Bat: return "Bat";
            case Family::Tallstrider: return "Tallstrider";
            case Family::Scorpid: return "Scorpid";
            case Family::FireElemental: return "Fire Elemental";
            case Family::WaterElemental: return "Water Elemental";
            case Family::EarthElemental: return "Earth Elemental";
            case Family::AirElemental: return "Air Elemental";
            case Family::Murloc: return "Murloc";
            case Family::Infernal: return "Infernal";
            case Family::Ooze: return "Ooze";
            case Family::Mechanical: return "Mechanical";
            case Family::Kodo: return "Kodo";
            case Family::Horse: return "Horse";
            case Family::Mechanostrider: return "Mechanostrider";
            case Family::SkeletalHorse: return "Skeletal Horse";
            case Family::Ram: return "Ram";
            case Family::Hyena: return "Hyena";
            case Family::Boar: return "Boar";
            case Family::Crab: return "Crab";
            case Family::Turtle: return "Turtle";
            case Family::WindSerpent: return "Wind Serpent";
            case Family::CarrionBird: return "Carrion Bird";
            case Family::Basilisk: return "Basilisk";
            case Family::Threshadon: return "Threshadon";
            case Family::Hydra: return "Hydra";
            case Family::BogBeast: return "Bog Beast";
            case Family::Imp: return "Imp";
            case Family::Felhound: return "Felhound";
            case Family::Felguard: return "Felguard";
            case Family::Skeleton: return "Skeleton";
            case Family::Ghoul: return "Ghoul";
            case Family::Banshee: return "Banshee";
            case Family::Gnoll: return "Gnoll";
            case Family::Kobold: return "Kobold";
            case Family::Trogg: return "Trogg";
            case Family::Ogre: return "Ogre";
            case Family::Centaur: return "Centaur";
            case Family::Quilboar: return "Quilboar";
            case Family::Furbolg: return "Furbolg";
            case Family::Harpy: return "Harpy";
            case Family::Troll: return "Troll";
            case Family::Naga: return "Naga";
            case Family::Makrura: return "Makrura";
            case Family::Moonkin: return "Moonkin";
            case Family::Worgen: return "Worgen";
            case Family::PoisonElemental: return "Poison Elemental";
            case Family::Darkhound: return "Darkhound";
            case Family::CryptFiend: return "Crypt Fiend";
            case Family::Gryphon: return "Gryphon";
            case Family::WindRider: return "Wind Rider";
            case Family::Hippogryph: return "Hippogryph";
            case Family::Dragonhawk: return "Dragonhawk";
        }
        return "Other";
    }

    char const* RoleName(Role r)
    {
        switch (r)
        {
            case Role::Melee: return "melee";
            case Role::Ranged: return "ranged";
            case Role::Tank: return "tank";
        }
        return "melee";
    }

    std::vector<Family> Families(std::vector<CompanionSpecies> const& all)
    {
        std::vector<Family> out;
        for (CompanionSpecies const& sp : all)
            if (std::find(out.begin(), out.end(), sp.family) == out.end())
                out.push_back(sp.family);
        std::sort(out.begin(), out.end(), [](Family a, Family b) { return std::strcmp(FamilyName(a), FamilyName(b)) < 0; });
        return out;
    }

    std::vector<uint16_t> SpeciesOf(std::vector<CompanionSpecies> const& all, Family f)
    {
        std::vector<uint16_t> out;
        for (CompanionSpecies const& sp : all)
            if (sp.family == f)
                out.push_back(sp.id);
        std::sort(out.begin(), out.end());
        return out;
    }

    PageSpan Page(size_t count, uint32_t page)
    {
        PageSpan p;
        size_t const pages = std::max<size_t>(1, (count + kPerPage - 1) / kPerPage);
        p.page = uint32_t(std::min<size_t>(page, pages - 1));
        p.first = size_t(p.page) * kPerPage;
        p.end = std::min(count, p.first + kPerPage);
        p.prev = p.page > 0;
        p.next = p.end < count;
        return p;
    }
}
