/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_COMPANION_TRAITS_H
#define FUSION_COMPANION_TRAITS_H

#include "fusion/Companions.h"

#include <functional>
#include <string>
#include <vector>

// Companion traits, rarity, named combinations and breeding (design/COMPANION_CATALOG.md
// sections 3 and 4). Traits are rolled when a companion item drops (and when one is bred),
// stored with the companion (CompanionState::traits, a bitmask) and shown on the item's tooltip
// through its suffix factor (ItemSeed). They are multiplicative with the role's stats and never
// lift a companion to a hunter pet of its level (StatsWithTraits clamps under the baseline).
namespace Fusion::Companions
{
    enum class Trait : uint8_t
    {
        None = 0,
        // Combat
        Ironhide = 1, Swift = 2, Ferocious = 3, Bloodthirsty = 4, Berserk = 5, Venomous = 6, ThickSkulled = 7,
        // Elemental
        FireTouched = 8, FrostTouched = 9, ShadowTouched = 10, StormTouched = 11, Spellward = 12,
        // Utility
        Loyal = 13, Tracker = 14, Scavenger = 15, Forager = 16, Packmule = 17,
        // Body and looks
        Giant = 18, Runt = 19, Albino = 20, Shadowcoat = 21, Gilded = 22,
        // Mount (rideable lines)
        Surefooted = 23, Skybound = 24, Tireless = 25,
        // Humanoid lines
        Gunner = 26,
        // Legendary
        PhoenixBlooded = 27, Alpha = 28, TwinSouled = 29, Primal = 30,
    };
    constexpr uint8_t kTraitCount = 30;
    constexpr uint8_t kMaxTraits = 3;

    enum class TraitKind : uint8_t
    {
        Combat = 0,
        Elemental = 1,
        Utility = 2,
        Looks = 3,
        Mount = 4,
        Gunner = 5,
        Legendary = 6,
    };

    enum class Rarity : uint8_t
    {
        Common = 0,
        Uncommon = 1,
        Rare = 2,
        Epic = 3,
        Legendary = 4,
    };

    struct TraitInfo
    {
        Trait id = Trait::None;
        char const* name = "";
        TraitKind kind = TraitKind::Combat;
        uint8_t weight = 1;         // inside its kind
    };
    TraitInfo const& TraitData(Trait t);
    char const* RarityName(Rarity r);

    inline uint32_t TraitBit(Trait t) { return t == Trait::None ? 0u : 1u << (uint8_t(t) - 1); }
    inline bool HasTrait(uint32_t mask, Trait t) { return (mask & TraitBit(t)) != 0; }
    // The traits in a mask, by id.
    std::vector<Trait> TraitList(uint32_t mask);
    // Rarity from the traits: 0 Common, 1 Uncommon, 2 Rare, 3 Epic, a legendary one Legendary.
    Rarity RarityOf(uint32_t mask);

    // A family's creature type (for the trait restrictions and catalysts).
    CreatureType FamilyType(Family f);
    bool IsRideableLine(CompanionSpecies const& sp);
    // Whether a species may have the trait at all (3a's "eligible" column and 3c's rules).
    bool TraitEligible(Trait t, CompanionSpecies const& sp);
    // Whether two traits exclude each other (one body size, one coloring, one element).
    bool TraitsConflict(Trait a, Trait b);
    // Whether a mask is a valid set for the species: eligible, no conflicts, at most three, at
    // most one legendary.
    bool ValidTraits(uint32_t mask, CompanionSpecies const& sp);

    // ── Rolling ─────────────────────────────────────────────────────────────────────────────
    // Uniform rolls in [0, 1).
    using Roller = std::function<float()>;
    // A drop's rarity by its victim (3b): critters and normals 60/27/10/2.7/0.3, elites and
    // rares 35/33/22/8.5/1.5, rare elites and bosses 15/30/33/17/5.
    Rarity RollRarity(CreatureRank rank, bool critter, float roll);
    // Traits for a rarity: each slot draws a kind (3b's weights, mount and gunner only where
    // eligible), then a trait in it; no duplicates, exclusions drawn again. A Legendary gets two
    // and one legendary trait. `twinStage` gets the Twin-souled stage (1-3) when it is drawn.
    uint32_t RollTraits(CompanionSpecies const& sp, Rarity r, Roller const& roll, uint8_t& twinStage);
    // A fresh drop's whole roll into `s` (traits, rarity, twin stage).
    void RollDrop(CompanionSpecies const& sp, CompanionState& s, CreatureRank rank, bool critter, Roller const& roll);

    // ── Named combinations (3d) ─────────────────────────────────────────────────────────────
    enum class Combo : uint8_t
    {
        None = 0, Viper, Juggernaut, Bloodfrenzy, Wildfire, Glacier, Stormcaller, Nightstalker, Voidward,
        Colossus, Warhorse, Pathfinder, GoldenHoard, Quartermaster, Ashborn,
    };
    struct ComboInfo
    {
        Combo id = Combo::None;
        char const* name = "";
        Trait a = Trait::None, b = Trait::None;
    };
    std::vector<ComboInfo> const& Combos();
    std::vector<Combo> CombosOf(uint32_t mask);
    inline bool HasCombo(uint32_t mask, Combo c)
    {
        for (Combo x : CombosOf(mask))
            if (x == c)
                return true;
        return false;
    }

    // ── Effects on stats ────────────────────────────────────────────────────────────────────
    struct TraitMods
    {
        float health = 1.f;
        float damage = 1.f;
        float armor = 1.f;
        float attackSpeed = 1.f;    // attacks per second, multiplied
        float runSpeed = 1.f;
        float scale = 1.f;
        float critPct = 0.f;
        float dodgePct = 0.f;
    };
    TraitMods ModsOf(uint32_t mask);
    // Stats with the traits on top: Primal counts one stage higher (1.2 at a final stage), the
    // mods multiply, and each stat stays under the hunter pet of its level.
    CompanionStats StatsWithTraits(CompanionSpecies const& sp, CompanionState const& s, uint8_t level);
    // A corpse's coin with the looter's companion's Scavenger (+10%, Golden Hoard +25%) on it.
    uint32_t ScavengedGold(uint32_t gold, uint32_t mask);
    // How long a fainted companion takes to come round on its own (CompanionRest.h): the realm's
    // wait (`baseSeconds`, Companions.FaintRecoverMinutes), Loyal a third of it (at least 1 s).
    uint32_t FaintRecoverSeconds(uint32_t mask, uint32_t baseSeconds);
    // Mounted speed with Tireless (+10) and Warhorse (+15 in total).
    uint8_t MountSpeedWithTraits(uint8_t speed, uint32_t mask);
    // The health share below which it carries nobody: 35%, Tireless 15%.
    float RideMinHealth(uint32_t mask);

    // ── Tooltip ─────────────────────────────────────────────────────────────────────────────
    // "Rare: Swift, Venomous" (empty for Common), and the combinations' line ("Viper: Venomous +
    // Swift"), and the lineage line ("Gen 2, of Wolf Pup and Timber Wolf Pup, bred 1 of 3").
    std::string TraitLine(uint32_t mask);
    std::string ComboLine(uint32_t mask);

    // ── Breeding (section 4) ────────────────────────────────────────────────────────────────
    constexpr uint8_t kBreedMax = 3;
    constexpr uint32_t kBreedRestSeconds = 72 * 3600;
    constexpr uint8_t kBreedReagentsPerParent = 5;
    constexpr float kInheritChance = 0.35f;
    constexpr float kInheritLegendaryChance = 0.03f;
    constexpr float kMutationChance = 0.05f;
    constexpr float kLegendaryMutationChance = 0.003f;
    constexpr float kHybridChance = 0.08f;

    // Families that breed together: one family, except the elementals (one group) and the
    // demons (one group).
    uint8_t BreedGroup(Family f);
    // The family reagent each parent's owner brings (the low band for lines that evolve below
    // 45, the high band otherwise).
    uint32_t BreedReagent(CompanionSpecies const& sp);
    // The fee in copper: 10 silver times the parents' average level.
    uint32_t BreedFee(uint8_t levelA, uint8_t levelB);

    enum class BreedBlock : uint8_t
    {
        None = 0,
        SameCompanion = 1,
        NotAdult = 2,       // both must be at their final stage of a three-stage line
        OtherFamily = 3,
        Exhausted = 4,      // bred kBreedMax times
        Resting = 5,        // inside its 72 h
        Busy = 6,           // summoned, ridden or duelling
        NoReagents = 7,
        NoMoney = 8,
    };
    struct BreedContext
    {
        uint32_t now = 0;           // unix seconds
        bool aBusy = false, bBusy = false;
        uint32_t reagentsA = 0;     // the reagent count each parent needs, held
        uint32_t reagentsB = 0;
        uint32_t money = 0;
    };
    BreedBlock CanBreed(CompanionSpecies const& spA, CompanionState const& a, CompanionSpecies const& spB,
        CompanionState const& b, bool sameItem, BreedContext const& ctx);
    // The offspring: the first parent's species, level 1, stage 0; each parent trait passes at
    // 35% (legendary 3%), exclusions keep one at random, at most three; a 5% mutation (0.3% a
    // legendary one if both parents are Epic or better); rarity from its traits; generation one
    // above the older parent; 8% a hybrid coloring when the parents' species differ.
    CompanionState BreedOffspring(CompanionSpecies const& spA, CompanionState const& a,
        CompanionSpecies const& spB, CompanionState const& b, Roller const& roll);
    // After a breeding: one more on each parent's count, and its rest.
    void MarkBred(CompanionState& parent, uint32_t now);

    // Hybrid colorings (4b): the stages (bit 0 = stage 1) at which `species` may wear `other`'s
    // display; 0 for a pair with no shared model.
    uint8_t HybridStages(uint16_t species, uint16_t other);
    // The creature whose model the companion wears now: its stage's own, or for a hybrid its other
    // parent species' creature of that stage where HybridStages lets it (the stats, the name and
    // the moveset stay its own species'). 0 for a stage it does not have.
    uint32_t LookCreature(std::vector<CompanionSpecies> const& all, CompanionSpecies const& sp, CompanionState const& s);
    // The display it wears now, by the same rule: the look table's display of its stage (or of a
    // hybrid's other line at that stage). 0 when that stage names none: the creature's own then.
    uint32_t LookDisplay(std::vector<CompanionSpecies> const& all, CompanionSpecies const& sp, CompanionState const& s);
    // Its object scale (OBJECT_FIELD_SCALE_X): the stage's scale and its traits' (Colossus...) on
    // top of the display's own size, `displayScale` (CreatureDisplayInfo's scale times its model's,
    // what vanilla gives any creature; 0 or less counts as 1). A mount line's last form at scale 1
    // is exactly the size it is ridden at.
    float LookScale(CompanionSpecies const& sp, CompanionState const& s, float displayScale);
}

#endif
