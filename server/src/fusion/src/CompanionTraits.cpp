/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/CompanionTraits.h"

#include <array>

namespace Fusion::Companions
{
    namespace
    {
        std::array<TraitInfo, kTraitCount + 1> const kTraits = {{
            {Trait::None, "", TraitKind::Combat, 0},
            {Trait::Ironhide, "Ironhide", TraitKind::Combat, 1},
            {Trait::Swift, "Swift", TraitKind::Combat, 1},
            {Trait::Ferocious, "Ferocious", TraitKind::Combat, 1},
            {Trait::Bloodthirsty, "Bloodthirsty", TraitKind::Combat, 1},
            {Trait::Berserk, "Berserk", TraitKind::Combat, 1},
            {Trait::Venomous, "Venomous", TraitKind::Combat, 1},
            {Trait::ThickSkulled, "Thick-skulled", TraitKind::Combat, 1},
            {Trait::FireTouched, "Fire-touched", TraitKind::Elemental, 1},
            {Trait::FrostTouched, "Frost-touched", TraitKind::Elemental, 1},
            {Trait::ShadowTouched, "Shadow-touched", TraitKind::Elemental, 1},
            {Trait::StormTouched, "Storm-touched", TraitKind::Elemental, 1},
            {Trait::Spellward, "Spellward", TraitKind::Elemental, 1},
            {Trait::Loyal, "Loyal", TraitKind::Utility, 1},
            {Trait::Tracker, "Tracker", TraitKind::Utility, 1},
            {Trait::Scavenger, "Scavenger", TraitKind::Utility, 1},
            {Trait::Forager, "Forager", TraitKind::Utility, 1},
            {Trait::Packmule, "Packmule", TraitKind::Utility, 1},
            {Trait::Giant, "Giant", TraitKind::Looks, 3},
            {Trait::Runt, "Runt", TraitKind::Looks, 3},
            {Trait::Albino, "Albino", TraitKind::Looks, 2},
            {Trait::Shadowcoat, "Shadowcoat", TraitKind::Looks, 3},
            {Trait::Gilded, "Gilded", TraitKind::Looks, 1},
            {Trait::Surefooted, "Surefooted", TraitKind::Mount, 1},
            {Trait::Skybound, "Skybound", TraitKind::Mount, 1},
            {Trait::Tireless, "Tireless", TraitKind::Mount, 1},
            {Trait::Gunner, "Gunner", TraitKind::Gunner, 1},
            {Trait::PhoenixBlooded, "Phoenix-blooded", TraitKind::Legendary, 1},
            {Trait::Alpha, "Alpha", TraitKind::Legendary, 1},
            {Trait::TwinSouled, "Twin-souled", TraitKind::Legendary, 1},
            {Trait::Primal, "Primal", TraitKind::Legendary, 1},
        }};

        bool IsElement(Trait t)
        {
            return t == Trait::FireTouched || t == Trait::FrostTouched || t == Trait::ShadowTouched ||
                t == Trait::StormTouched;
        }
        bool IsSize(Trait t) { return t == Trait::Giant || t == Trait::Runt; }
        bool IsColoring(Trait t) { return t == Trait::Albino || t == Trait::Shadowcoat || t == Trait::Gilded; }

        // 3b's kind weights: Uncommon, Rare, Epic, Legendary (its non-legendary slots).
        uint8_t KindWeight(TraitKind k, Rarity r)
        {
            int const col = r == Rarity::Uncommon ? 0 : r == Rarity::Rare ? 1 : r == Rarity::Epic ? 2 : 3;
            static uint8_t const w[6][4] = {
                {40, 36, 32, 30},   // combat
                {15, 20, 24, 26},   // elemental
                {25, 20, 16, 14},   // utility
                {15, 14, 14, 14},   // looks
                {5, 6, 8, 8},       // mount
                {4, 8, 12, 12},     // gunner
            };
            return uint8_t(k) < 6 ? w[uint8_t(k)][col] : 0;
        }

        bool Conflicts(uint32_t mask, Trait t)
        {
            for (Trait o : TraitList(mask))
                if (o == t || TraitsConflict(o, t))
                    return true;
            return false;
        }

        // One trait of `kind` that fits beside `mask`, by the kind's inner weights; None if no
        // trait of the kind fits.
        Trait DrawInKind(CompanionSpecies const& sp, TraitKind kind, uint32_t mask, float roll)
        {
            // Fire elementals and imps always take Fire-touched when they draw an element.
            if (kind == TraitKind::Elemental && (sp.family == Family::FireElemental || sp.family == Family::Imp) &&
                !Conflicts(mask, Trait::FireTouched))
                return Trait::FireTouched;
            uint32_t total = 0;
            for (TraitInfo const& t : kTraits)
                if (t.id != Trait::None && t.kind == kind && TraitEligible(t.id, sp) && !Conflicts(mask, t.id))
                    total += t.weight;
            if (!total)
                return Trait::None;
            float at = Clamp(roll, 0.f, 0.99999f) * float(total);
            for (TraitInfo const& t : kTraits)
            {
                if (t.id == Trait::None || t.kind != kind || !TraitEligible(t.id, sp) || Conflicts(mask, t.id))
                    continue;
                if (at < float(t.weight))
                    return t.id;
                at -= float(t.weight);
            }
            return Trait::None;
        }

        // A non-legendary trait by the rarity's kind weights.
        Trait DrawOne(CompanionSpecies const& sp, Rarity r, uint32_t mask, Roller const& roll)
        {
            bool const ride = IsRideableLine(sp);
            bool const humanoid = IsHumanoidFamily(sp.family);
            for (int tries = 0; tries < 24; ++tries)
            {
                uint32_t total = 0;
                for (uint8_t k = 0; k < 6; ++k)
                {
                    TraitKind const kind = TraitKind(k);
                    if ((kind == TraitKind::Mount && !ride) || (kind == TraitKind::Gunner && !humanoid))
                        continue;
                    total += KindWeight(kind, r);
                }
                float at = Clamp(roll(), 0.f, 0.99999f) * float(total);
                TraitKind kind = TraitKind::Combat;
                for (uint8_t k = 0; k < 6; ++k)
                {
                    TraitKind const kk = TraitKind(k);
                    if ((kk == TraitKind::Mount && !ride) || (kk == TraitKind::Gunner && !humanoid))
                        continue;
                    if (at < float(KindWeight(kk, r)))
                    {
                        kind = kk;
                        break;
                    }
                    at -= float(KindWeight(kk, r));
                }
                Trait t = DrawInKind(sp, kind, mask, roll());
                if (t != Trait::None)
                    return t;
            }
            return Trait::None;
        }

        uint8_t RollTwinStage(CompanionSpecies const& sp, float roll)
        {
            uint8_t const stages = std::max<uint8_t>(1, sp.stageCount);
            if (stages >= 3)
                return roll < 1.f / 3.f ? 3 : roll < 2.f / 3.f ? 1 : 2;
            return uint8_t(1 + std::min<int>(stages - 1, int(roll * stages)));
        }

        Trait DrawLegendary(float roll)
        {
            static Trait const kLegendary[] = {Trait::PhoenixBlooded, Trait::Alpha, Trait::TwinSouled, Trait::Primal};
            return kLegendary[std::min(3, int(Clamp(roll, 0.f, 0.99999f) * 4.f))];
        }
    }

    TraitInfo const& TraitData(Trait t)
    {
        uint8_t const i = uint8_t(t);
        return kTraits[i <= kTraitCount ? i : 0];
    }

    char const* RarityName(Rarity r)
    {
        switch (r)
        {
            case Rarity::Uncommon: return "Uncommon";
            case Rarity::Rare: return "Rare";
            case Rarity::Epic: return "Epic";
            case Rarity::Legendary: return "Legendary";
            default: return "Common";
        }
    }

    std::vector<Trait> TraitList(uint32_t mask)
    {
        std::vector<Trait> out;
        for (uint8_t i = 1; i <= kTraitCount; ++i)
            if (mask & (1u << (i - 1)))
                out.push_back(Trait(i));
        return out;
    }

    Rarity RarityOf(uint32_t mask)
    {
        size_t n = 0;
        bool legendary = false;
        for (Trait t : TraitList(mask))
        {
            ++n;
            legendary = legendary || TraitData(t).kind == TraitKind::Legendary;
        }
        if (legendary)
            return Rarity::Legendary;
        return n >= 3 ? Rarity::Epic : n == 2 ? Rarity::Rare : n == 1 ? Rarity::Uncommon : Rarity::Common;
    }

    CreatureType FamilyType(Family f)
    {
        switch (f)
        {
            case Family::Critter: return CreatureType::Critter;
            case Family::Dragon:
            case Family::FaerieDragon: return CreatureType::Dragonkin;
            case Family::FireElemental:
            case Family::WaterElemental:
            case Family::EarthElemental:
            case Family::AirElemental:
            case Family::BogBeast:
            case Family::PoisonElemental: return CreatureType::Elemental;
            case Family::Infernal:
            case Family::Imp:
            case Family::Felhound:
            case Family::Felguard:
            case Family::Darkhound: return CreatureType::Demon;
            case Family::Ooze: return CreatureType::NotSpecified;
            case Family::Mechanical:
            case Family::Mechanostrider: return CreatureType::Mechanical;
            case Family::SkeletalHorse:
            case Family::Skeleton:
            case Family::Ghoul:
            case Family::Banshee:
            case Family::CryptFiend: return CreatureType::Undead;
            default: return IsHumanoidFamily(f) ? CreatureType::Humanoid : CreatureType::Beast;
        }
    }

    bool IsRideableLine(CompanionSpecies const& sp)
    {
        for (uint8_t i = 0; i < sp.stageCount; ++i)
            if (sp.stages[i].rideable)
                return true;
        return false;
    }

    bool TraitEligible(Trait t, CompanionSpecies const& sp)
    {
        CreatureType const type = FamilyType(sp.family);
        bool const mech = type == CreatureType::Mechanical;
        bool const elemental = type == CreatureType::Elemental;
        bool const ooze = sp.family == Family::Ooze;
        switch (t)
        {
            case Trait::None: return false;
            case Trait::Bloodthirsty: return !mech && !elemental && !ooze;
            case Trait::Venomous:
                return type == CreatureType::Beast || type == CreatureType::Demon || type == CreatureType::Undead ||
                    type == CreatureType::Dragonkin || type == CreatureType::Humanoid;
            case Trait::FireTouched: return sp.family != Family::WaterElemental;
            case Trait::FrostTouched:
                return sp.family != Family::FireElemental && sp.family != Family::Imp && sp.family != Family::Infernal;
            case Trait::ShadowTouched: return !mech;
            case Trait::Albino: return type == CreatureType::Beast || type == CreatureType::Dragonkin;
            case Trait::Shadowcoat:
                return type == CreatureType::Beast || type == CreatureType::Demon || type == CreatureType::Undead;
            case Trait::Surefooted:
            case Trait::Skybound:
            case Trait::Tireless: return IsRideableLine(sp);
            case Trait::Gunner: return IsHumanoidFamily(sp.family);
            default: return uint8_t(t) <= kTraitCount;
        }
    }

    bool TraitsConflict(Trait a, Trait b)
    {
        if (a == b)
            return false;
        return (IsSize(a) && IsSize(b)) || (IsColoring(a) && IsColoring(b)) || (IsElement(a) && IsElement(b)) ||
            (TraitData(a).kind == TraitKind::Legendary && TraitData(b).kind == TraitKind::Legendary);
    }

    bool ValidTraits(uint32_t mask, CompanionSpecies const& sp)
    {
        if (mask >> kTraitCount)
            return false;
        std::vector<Trait> const list = TraitList(mask);
        if (list.size() > kMaxTraits)
            return false;
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (!TraitEligible(list[i], sp))
                return false;
            for (size_t j = i + 1; j < list.size(); ++j)
                if (TraitsConflict(list[i], list[j]))
                    return false;
        }
        return true;
    }

    // ── Rolling ─────────────────────────────────────────────────────────────────────────────

    Rarity RollRarity(CreatureRank rank, bool critter, float roll)
    {
        // Cumulative Common, Uncommon, Rare, Epic (the rest Legendary).
        static float const kLow[4] = {0.60f, 0.87f, 0.97f, 0.997f};
        static float const kMid[4] = {0.35f, 0.68f, 0.90f, 0.985f};
        static float const kHigh[4] = {0.15f, 0.45f, 0.78f, 0.95f};
        float const* t = kLow;
        if (!critter)
        {
            if (rank == CreatureRank::Elite || rank == CreatureRank::Rare)
                t = kMid;
            else if (rank == CreatureRank::RareElite || rank == CreatureRank::WorldBoss || rank == CreatureRank::DungeonBoss)
                t = kHigh;
        }
        for (uint8_t i = 0; i < 4; ++i)
            if (roll < t[i])
                return Rarity(i);
        return Rarity::Legendary;
    }

    uint32_t RollTraits(CompanionSpecies const& sp, Rarity r, Roller const& roll, uint8_t& twinStage)
    {
        twinStage = 0;
        uint32_t mask = 0;
        int const plain = r == Rarity::Legendary ? 2 : int(r);
        for (int i = 0; i < plain; ++i)
            mask |= TraitBit(DrawOne(sp, r, mask, roll));
        if (r == Rarity::Legendary)
        {
            Trait const t = DrawLegendary(roll());
            mask |= TraitBit(t);
            if (t == Trait::TwinSouled)
                twinStage = RollTwinStage(sp, roll());
        }
        return mask;
    }

    void RollDrop(CompanionSpecies const& sp, CompanionState& s, CreatureRank rank, bool critter, Roller const& roll)
    {
        Rarity const r = RollRarity(rank, critter, roll());
        s.traits = RollTraits(sp, r, roll, s.twinStage);
        s.rarity = uint8_t(RarityOf(s.traits));
    }

    // ── Combinations ────────────────────────────────────────────────────────────────────────

    std::vector<ComboInfo> const& Combos()
    {
        static std::vector<ComboInfo> const all = {
            {Combo::Viper, "Viper", Trait::Venomous, Trait::Swift},
            {Combo::Juggernaut, "Juggernaut", Trait::Ironhide, Trait::ThickSkulled},
            {Combo::Bloodfrenzy, "Bloodfrenzy", Trait::Bloodthirsty, Trait::Berserk},
            {Combo::Wildfire, "Wildfire", Trait::FireTouched, Trait::Ferocious},
            {Combo::Glacier, "Glacier", Trait::FrostTouched, Trait::Ironhide},
            {Combo::Stormcaller, "Stormcaller", Trait::StormTouched, Trait::Swift},
            {Combo::Nightstalker, "Nightstalker", Trait::Shadowcoat, Trait::ShadowTouched},
            {Combo::Voidward, "Voidward", Trait::Spellward, Trait::ShadowTouched},
            {Combo::Colossus, "Colossus", Trait::Giant, Trait::Ironhide},
            {Combo::Warhorse, "Warhorse", Trait::Surefooted, Trait::Tireless},
            {Combo::Pathfinder, "Pathfinder", Trait::Tracker, Trait::Forager},
            {Combo::GoldenHoard, "Golden Hoard", Trait::Gilded, Trait::Scavenger},
            {Combo::Quartermaster, "Quartermaster", Trait::Gunner, Trait::Packmule},
            {Combo::Ashborn, "Ashborn", Trait::PhoenixBlooded, Trait::FireTouched},
        };
        return all;
    }

    std::vector<Combo> CombosOf(uint32_t mask)
    {
        std::vector<Combo> out;
        for (ComboInfo const& c : Combos())
            if (HasTrait(mask, c.a) && HasTrait(mask, c.b))
                out.push_back(c.id);
        return out;
    }

    // ── Stats ───────────────────────────────────────────────────────────────────────────────

    TraitMods ModsOf(uint32_t mask)
    {
        TraitMods m;
        if (HasTrait(mask, Trait::Ironhide))
        {
            m.armor *= 1.10f;
            m.health *= 1.05f;
        }
        if (HasTrait(mask, Trait::Swift))
        {
            m.runSpeed *= 1.15f;
            m.attackSpeed *= 1.08f;
        }
        if (HasTrait(mask, Trait::Ferocious))
        {
            m.damage *= 1.08f;
            m.critPct += 5.f;
        }
        if (HasTrait(mask, Trait::Giant))
        {
            m.scale *= 1.2f;
            m.health *= HasTrait(mask, Trait::Ironhide) ? 1.15f : 1.08f;   // Colossus
            m.runSpeed *= 0.95f;
        }
        if (HasTrait(mask, Trait::Runt))
        {
            m.scale *= 0.8f;
            m.dodgePct += 10.f;
            m.runSpeed *= 1.05f;
            m.health *= 0.95f;
        }
        return m;
    }

    CompanionStats StatsWithTraits(CompanionSpecies const& sp, CompanionState const& s, uint8_t level)
    {
        uint8_t const stage = std::min<uint8_t>(s.stage, uint8_t(std::max<uint8_t>(sp.stageCount, 1) - 1));
        CompanionStats st = Stats(sp.role, stage, sp.stageCount, level);
        if (HasTrait(s.traits, Trait::Primal))
        {
            // As one stage higher; at the final stage 1.2 in place of the elder's 1.12.
            float const now = StageMultiplier(stage, sp.stageCount);
            float const up = stage + 1 < sp.stageCount ? StageMultiplier(uint8_t(stage + 1), sp.stageCount)
                                                       : now * (1.2f / 1.12f);
            float const k = up / now;
            st.health = uint32_t(std::lround(float(st.health) * k));
            st.armor = uint32_t(std::lround(float(st.armor) * k));
            st.damageMin *= k;
            st.damageMax *= k;
        }
        TraitMods const m = ModsOf(s.traits);
        st.health = uint32_t(std::lround(float(st.health) * m.health));
        st.armor = uint32_t(std::lround(float(st.armor) * m.armor));
        st.damageMin *= m.damage;
        st.damageMax *= m.damage;
        st.attackMs = uint32_t(std::lround(float(st.attackMs) / m.attackSpeed));
        // Never a hunter pet: each stat at most 99% of the yardstick.
        CompanionStats const pet = HunterPetBaseline(level);
        st.health = std::min(st.health, uint32_t(float(pet.health) * 0.99f));
        st.armor = std::min(st.armor, uint32_t(float(pet.armor) * 0.99f));
        float const cap = pet.Dps() * 0.99f;
        if (st.Dps() > cap)
        {
            float const k = cap / st.Dps();
            st.damageMin *= k;
            st.damageMax *= k;
        }
        return st;
    }

    uint32_t ScavengedGold(uint32_t gold, uint32_t mask)
    {
        if (!gold || !HasTrait(mask, Trait::Scavenger))
            return gold;
        return uint32_t(float(gold) * (HasCombo(mask, Combo::GoldenHoard) ? 1.25f : 1.10f));
    }

    uint32_t FaintRecoverSeconds(uint32_t mask, uint32_t baseSeconds)
    {
        uint32_t const s = HasTrait(mask, Trait::Loyal) ? baseSeconds / 3 : baseSeconds;
        return s ? s : 1;
    }

    uint8_t MountSpeedWithTraits(uint8_t speed, uint32_t mask)
    {
        if (!speed)
            return 0;
        if (HasTrait(mask, Trait::Surefooted) && HasTrait(mask, Trait::Tireless))
            return uint8_t(speed + 15);
        return HasTrait(mask, Trait::Tireless) ? uint8_t(speed + 10) : speed;
    }

    float RideMinHealth(uint32_t mask) { return HasTrait(mask, Trait::Tireless) ? 0.15f : kRideMinHealth; }

    // ── Tooltip ─────────────────────────────────────────────────────────────────────────────

    std::string TraitLine(uint32_t mask)
    {
        std::vector<Trait> const list = TraitList(mask);
        if (list.empty())
            return std::string();
        std::string s = std::string(RarityName(RarityOf(mask))) + ": ";
        for (size_t i = 0; i < list.size(); ++i)
            s += (i ? ", " : "") + std::string(TraitData(list[i]).name);
        return s;
    }

    std::string ComboLine(uint32_t mask)
    {
        std::string s;
        for (ComboInfo const& c : Combos())
            if (HasTrait(mask, c.a) && HasTrait(mask, c.b))
                s += (s.empty() ? "" : "; ") + std::string(c.name) + ": " + TraitData(c.a).name + " + " + TraitData(c.b).name;
        return s;
    }

    // ── Breeding ────────────────────────────────────────────────────────────────────────────

    uint8_t BreedGroup(Family f)
    {
        switch (FamilyType(f))
        {
            case CreatureType::Elemental: return 200;
            case CreatureType::Demon: return 201;
            default: return uint8_t(f);
        }
    }

    uint32_t BreedReagent(CompanionSpecies const& sp)
    {
        bool const high = sp.evolutions[0].level >= 25 || (sp.evolutions[1].Exists() && sp.evolutions[1].level >= 45);
        auto band = [high](uint32_t low, uint32_t hi) { return high ? hi : low; };
        switch (sp.id)
        {
            case 1: return 7286;                // black whelp scale
            case 2: case 140: return 7287;      // red
            case 3: return 7392;                // green
            case 4: return band(7286, 7287);
            case 5: return 13463;               // faerie dragons: Dreamfoil
            case 24: case 141: return 14256;    // imps and infernals: Felcloth
            default: break;
        }
        switch (sp.family)
        {
            case Family::Spider:
            case Family::CryptFiend: return band(2251, 12205);
            case Family::Bear: return band(3173, 3730);
            case Family::Cat: return band(3731, 12202);
            case Family::Wolf: return band(2672, 12208);
            case Family::Raptor: return band(3685, 12184);
            case Family::Scorpid: return band(5466, 8154);
            case Family::Crocolisk: return band(2924, 3667);
            case Family::Turtle: return 3712;
            case Family::Gorilla: return band(4537, 4601);
            case Family::Boar: return band(769, 2677);
            case Family::Crab: return band(2674, 2675);
            case Family::WindSerpent:
            case Family::Gryphon:
            case Family::WindRider:
            case Family::Hippogryph:
            case Family::Dragonhawk: return band(4589, 15420);    // feathers
            case Family::CarrionBird: return band(729, 3404);
            case Family::Hyena: return 12037;
            case Family::Owl: return band(6889, 12207);
            case Family::Bat: return 5637;
            case Family::Tallstrider: return 5469;
            case Family::Kodo: return band(5467, 12204);
            case Family::Basilisk: return 7912;
            case Family::Threshadon: return 4603;
            case Family::Hydra: return 13754;
            case Family::Dragon: return 7286;
            case Family::FaerieDragon: return 13463;
            case Family::FireElemental: return band(7068, 7077);
            case Family::WaterElemental:
            case Family::PoisonElemental: return band(7070, 7079);
            case Family::EarthElemental: return band(7067, 7075);
            case Family::AirElemental: return band(7069, 7081);
            case Family::BogBeast: return band(8153, 12803);
            case Family::Ooze: return 7079;
            case Family::Infernal:
            case Family::Imp:
            case Family::Felhound:
            case Family::Felguard:
            case Family::Darkhound: return 14256;
            case Family::Skeleton:
            case Family::Ghoul:
            case Family::Banshee:
            case Family::SkeletalHorse: return band(7972, 12808);
            case Family::Mechanical:
            case Family::Mechanostrider: return sp.evolutions[1].Exists() && sp.evolutions[1].level >= 45 ? 15994 : high ? 4389 : 4359;
            case Family::Ram: return 3927;
            case Family::Horse: return 1645;
            case Family::Gnoll: return 12037;
            case Family::Kobold: return 2836;
            case Family::Trogg: return 2838;
            case Family::Ogre: return 3770;
            case Family::Centaur: return 4304;
            case Family::Quilboar: return 769;
            case Family::Furbolg: return 1645;
            case Family::Moonkin: return 8153;
            case Family::Harpy: return 4589;
            case Family::Troll: return 8151;
            case Family::Naga: return 7971;
            case Family::Makrura: return 7974;
            case Family::Worgen: return 12208;
            case Family::Murloc: return 1468;
            case Family::Critter: return 6889;  // Small Egg
            default: return 0;
        }
    }

    uint32_t BreedFee(uint8_t levelA, uint8_t levelB) { return 1000u * (uint32_t(levelA) + uint32_t(levelB)) / 2u; }

    BreedBlock CanBreed(CompanionSpecies const& spA, CompanionState const& a, CompanionSpecies const& spB,
        CompanionState const& b, bool sameItem, BreedContext const& ctx)
    {
        if (sameItem)
            return BreedBlock::SameCompanion;
        if (spA.stageCount < 3 || spB.stageCount < 3 || a.stage + 1 < spA.stageCount || b.stage + 1 < spB.stageCount)
            return BreedBlock::NotAdult;
        if (BreedGroup(spA.family) != BreedGroup(spB.family))
            return BreedBlock::OtherFamily;
        if (a.breedCount >= kBreedMax || b.breedCount >= kBreedMax)
            return BreedBlock::Exhausted;
        if (a.breedReadyAt > ctx.now || b.breedReadyAt > ctx.now)
            return BreedBlock::Resting;
        if (ctx.aBusy || ctx.bBusy)
            return BreedBlock::Busy;
        if (ctx.reagentsA < kBreedReagentsPerParent || ctx.reagentsB < kBreedReagentsPerParent)
            return BreedBlock::NoReagents;
        if (ctx.money < BreedFee(a.level, b.level))
            return BreedBlock::NoMoney;
        return BreedBlock::None;
    }

    CompanionState BreedOffspring(CompanionSpecies const& spA, CompanionState const& a,
        CompanionSpecies const& spB, CompanionState const& b, Roller const& roll)
    {
        CompanionState kid;
        kid.species = spA.id;
        kid.generation = uint8_t(std::min(250, std::max<int>(a.generation, b.generation) + 1));
        kid.parentA = spA.id;
        kid.parentB = spB.id;
        // Each parent trait passes on its own; duplicates count once.
        std::vector<Trait> passed;
        for (CompanionState const* p : {&a, &b})
            for (Trait t : TraitList(p->traits))
            {
                bool const legendary = TraitData(t).kind == TraitKind::Legendary;
                if (roll() < (legendary ? kInheritLegendaryChance : kInheritChance) && TraitEligible(t, spA) &&
                    std::find(passed.begin(), passed.end(), t) == passed.end())
                    passed.push_back(t);
            }
        // Exclusions keep one at random; a legendary first, then at most three in all.
        uint32_t mask = 0;
        while (!passed.empty())
        {
            size_t const i = std::min(passed.size() - 1, size_t(Clamp(roll(), 0.f, 0.99999f) * float(passed.size())));
            Trait const t = passed[i];
            passed.erase(passed.begin() + long(i));
            if (TraitList(mask).size() >= kMaxTraits || Conflicts(mask, t))
                continue;
            mask |= TraitBit(t);
        }
        if (TraitList(mask).size() < kMaxTraits && roll() < kMutationChance)
            mask |= TraitBit(DrawOne(spA, Rarity::Rare, mask, roll));
        bool const epicParents = RarityOf(a.traits) >= Rarity::Epic && RarityOf(b.traits) >= Rarity::Epic;
        if (epicParents && TraitList(mask).size() < kMaxTraits && roll() < kLegendaryMutationChance)
        {
            Trait const t = DrawLegendary(roll());
            if (!Conflicts(mask, t))
                mask |= TraitBit(t);
        }
        kid.traits = mask;
        if (HasTrait(mask, Trait::TwinSouled))
            kid.twinStage = HasTrait(a.traits, Trait::TwinSouled) && a.twinStage ? a.twinStage
                : HasTrait(b.traits, Trait::TwinSouled) && b.twinStage ? b.twinStage : RollTwinStage(spA, roll());
        kid.rarity = uint8_t(RarityOf(mask));
        if (spA.id != spB.id && HybridStages(spA.id, spB.id) && roll() < kHybridChance)
            kid.hybrid = spB.id;
        return kid;
    }

    void MarkBred(CompanionState& parent, uint32_t now)
    {
        parent.breedCount = uint8_t(std::min<int>(parent.breedCount + 1, kBreedMax));
        parent.breedReadyAt = now + kBreedRestSeconds;
    }

    uint8_t HybridStages(uint16_t species, uint16_t other)
    {
        struct Pair
        {
            std::vector<uint16_t> group;
            uint8_t stages;
        };
        // 4b's table: every pair inside a group shares a model at those stages.
        static std::vector<Pair> const kPairs = {
            {{11, 102}, 0b011},
            {{11, 101}, 0b100},
            {{101, 103}, 0b110},
            {{10, 104, 105}, 0b111},
            {{9, 106, 107}, 0b111},
            {{7, 8}, 0b111},
            {{109, 110}, 0b111},
            {{6, 111}, 0b010},
            {{12, 112, 113, 114}, 0b011},
            {{112, 113, 114}, 0b111},
            {{18, 115, 116}, 0b111},
            {{13, 117}, 0b011},
            {{118, 119}, 0b111},
            {{14, 120}, 0b111},
            {{124, 125}, 0b111},
            {{15, 127}, 0b011},
            {{16, 128}, 0b011},
            {{17, 129}, 0b111},
            {{29, 130}, 0b011},
            {{131, 132}, 0b111},
            {{138, 139}, 0b110},
            {{25, 139}, 0b101},
            {{1, 2, 3, 4, 140}, 0b011},
        };
        uint8_t stages = 0;
        for (Pair const& p : kPairs)
        {
            bool const a = std::find(p.group.begin(), p.group.end(), species) != p.group.end();
            bool const b = std::find(p.group.begin(), p.group.end(), other) != p.group.end();
            if (a && b && species != other)
                stages |= p.stages;
        }
        return stages;
    }

    uint32_t LookCreature(std::vector<CompanionSpecies> const& all, CompanionSpecies const& sp, CompanionState const& s)
    {
        if (s.stage >= sp.stageCount)
            return 0;
        if (s.hybrid && s.hybrid != sp.id && (HybridStages(sp.id, s.hybrid) & (1u << s.stage)))
            if (CompanionSpecies const* other = FindSpecies(all, s.hybrid))
                if (s.stage < other->stageCount && other->stages[s.stage].creature)
                    return other->stages[s.stage].creature;
        return sp.stages[s.stage].creature;
    }

    uint32_t LookDisplay(std::vector<CompanionSpecies> const& all, CompanionSpecies const& sp, CompanionState const& s)
    {
        if (s.stage >= sp.stageCount)
            return 0;
        // The same choice as LookCreature: a hybrid wears its other line's whole look.
        if (s.hybrid && s.hybrid != sp.id && (HybridStages(sp.id, s.hybrid) & (1u << s.stage)))
            if (CompanionSpecies const* other = FindSpecies(all, s.hybrid))
                if (s.stage < other->stageCount && other->stages[s.stage].creature)
                    return other->stages[s.stage].display;
        return sp.stages[s.stage].display;
    }

    float LookScale(CompanionSpecies const& sp, CompanionState const& s, float displayScale)
    {
        float const stage = s.stage < sp.stageCount ? sp.stages[s.stage].scale : 1.f;
        return stage * ModsOf(s.traits).scale * (displayScale > 0.f ? displayScale : 1.f);
    }
}
