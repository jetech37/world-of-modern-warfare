/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_COMPANION_KEEPER_H
#define FUSION_COMPANION_KEEPER_H

#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"

#include <cstddef>
#include <vector>

// The Companion Keeper's rules: prices, caps and checks for making and changing companions. The
// keeper NPC is gone (sql/custom/azeroth_warfare/15_companion_index.sql takes its spawns out); its
// tools are a game master's, from the Companions window (CompanionIndex.h,
// game/Fusion/FusionCompanionIndex.cpp).
//
// Who may do what:
//  - Players never buy a companion, nor change its level or stage (kPlayersBuy): companions drop
//    and are bred. CanBuy and CanChange refuse them (GmOnly); the prices stay as the yardstick.
//  - Anyone chooses or changes the ability of a stage it has reached: the first choice is free,
//    a change costs kRepickFee (a stable master's retraining, per stage).
//  - Traits and a Twin-souled stage are an admin's tools only (kPlayersEditTraits).
//  - An admin does all of it free, at any level and stage (60 at most), whatever their own level.
namespace Fusion::Companions::Keeper
{
    constexpr uint32_t kFirstEntry = 92700;
    constexpr uint32_t kLastEntry = 92799;
    inline bool IsKeeper(uint32_t entry) { return entry >= kFirstEntry && entry <= kLastEntry; }

    // ── Prices, copper ──────────────────────────────────────────────────────────────────────
    constexpr uint32_t kBuyBase = 10000;            // 1 gold
    constexpr uint32_t kBuyPerLevelStage = 2000;    // 20 silver a level for each stage (1-3)
    // A rideable form never sells for less than the vendors' companion mounts (80 gold).
    constexpr uint32_t kMountFloor = 800000;
    constexpr uint32_t kServiceFee = 5000;          // 50 silver, on any level or stage change
    constexpr uint32_t kRepickFee = 5000;           // 50 silver to change a stage's ability
    constexpr bool kPlayersEditTraits = false;
    constexpr bool kPlayersBuy = false;

    // 1g + level x stage x 20s (stage counted 1-3), and kMountFloor for a rideable stage. Level 1
    // stage 1 is 1g 20s, level 60 stage 3 is 37g.
    uint32_t PurchasePrice(CompanionSpecies const& sp, uint8_t stage, uint8_t level);
    // Taking `from` to `stage` at `level`: what buying the new one costs above the old, plus
    // kServiceFee. Lowering costs only the fee.
    uint32_t ChangePrice(CompanionSpecies const& sp, CompanionState const& from, uint8_t stage, uint8_t level);
    // Choosing a stage's ability: free when it has none yet, kRepickFee to change it.
    uint32_t PickPrice(CompanionState const& s, uint8_t stage);

    // ── Limits ──────────────────────────────────────────────────────────────────────────────
    // Who is at the keeper: their level, whether they are an admin, and their money.
    struct Customer
    {
        uint8_t level = 1;
        bool gm = false;
        uint32_t money = 0;
    };

    // The highest level they may buy or set: their own (an admin's: kMaxLevel).
    uint8_t LevelCap(Customer const& who);
    // The level a stage needs (the evolution rule's that reaches it); 1 for the first stage.
    uint8_t MinLevelForStage(CompanionSpecies const& sp, uint8_t stage);
    // The stages (0-based) a customer may pick for the species: an admin every stage, a player
    // those whose level they could reach (MinLevelForStage <= their cap).
    std::vector<uint8_t> StagesFor(CompanionSpecies const& sp, Customer const& who);
    // The level choices offered at a stage: 1, 10, 20 ... 60 that fit (a player: from the stage's
    // level up to their own), plus the stage's own level and the cap when they are not on the list.
    std::vector<uint8_t> LevelPresets(CompanionSpecies const& sp, uint8_t stage, Customer const& who);

    // Why the keeper says no, in the order checked; None = it may.
    enum class Refusal : uint8_t
    {
        None = 0,
        BadStage = 1,       // past the species' last stage
        BadLevel = 2,       // 0 or above 60
        AboveOwner = 3,     // above the player's own level
        StageNeedsLevel = 4,    // below the level its stage needs
        Unchanged = 5,      // already so
        GmOnly = 6,
        NotReached = 7,     // an ability of a stage it has not reached
        NoAbility = 8,      // that option is empty
        Ineligible = 9,     // a trait its species can't have
        Conflict = 10,      // a trait that excludes one it has
        TooMany = 11,       // three traits already
        NotTwin = 12,       // a Twin-souled stage on a companion without the trait
        NoMoney = 13,
    };

    Refusal CanBuy(CompanionSpecies const& sp, uint8_t stage, uint8_t level, Customer const& who);
    // A carried companion to `stage` at `level` (either may be its current one).
    Refusal CanChange(CompanionSpecies const& sp, CompanionState const& s, uint8_t stage, uint8_t level,
        Customer const& who);
    // A new level or stage: experience and the kill count start over; a lower stage forgets the
    // ability choices of the stages above it (they are asked again when it gets there).
    void ApplyChange(CompanionSpecies const& sp, CompanionState& s, uint8_t stage, uint8_t level);

    Refusal CanPick(CompanionSpecies const& sp, CompanionState const& s, uint8_t stage, uint8_t option,
        Customer const& who);
    // Set (or change) the stage's ability choice; false when CanPick would refuse it.
    bool ApplyPick(CompanionSpecies const& sp, CompanionState& s, uint8_t stage, uint8_t option);

    // Adding or removing one trait (admins only): removing always may; adding must leave
    // a valid set (ValidTraits: eligible, no exclusions, at most three, one legendary).
    Refusal CanToggleTrait(CompanionSpecies const& sp, CompanionState const& s, Trait t, Customer const& who);
    // Toggle it, and the rarity follows; adding Twin-souled doubles its current stage, removing it
    // clears the doubled stage.
    void ApplyToggleTrait(CompanionSpecies const& sp, CompanionState& s, Trait t);
    // Every trait the companion has none of that it could take now, by id.
    std::vector<Trait> AddableTraits(CompanionSpecies const& sp, CompanionState const& s);
    // A Twin-souled companion's doubled stage (1-3, admins only).
    Refusal CanSetTwinStage(CompanionSpecies const& sp, CompanionState const& s, uint8_t twinStage,
        Customer const& who);

    // ── Browsing ────────────────────────────────────────────────────────────────────────────
    char const* FamilyName(Family f);
    char const* RoleName(Role r);
    // The families with species to sell, by name; a family's species, by id.
    std::vector<Family> Families(std::vector<CompanionSpecies> const& all);
    std::vector<uint16_t> SpeciesOf(std::vector<CompanionSpecies> const& all, Family f);

    // A 1.12 gossip menu shows about 15 options: a page holds kPerPage entries and leaves room
    // for previous, next and back.
    constexpr uint8_t kPerPage = 12;
    struct PageSpan
    {
        uint32_t page = 0;      // clamped to the last page
        size_t first = 0;       // entries [first, end)
        size_t end = 0;
        bool prev = false;
        bool next = false;
    };
    PageSpan Page(size_t count, uint32_t page);

    // A gossip option's action: [op u8][a u16][b u8], high to low; the option's sender carries
    // a species, a family or an item guid.
    inline uint32_t Action(uint8_t op, uint16_t a = 0, uint8_t b = 0)
    {
        return uint32_t(op) << 24 | uint32_t(a) << 8 | b;
    }
    inline uint8_t ActionOp(uint32_t action) { return uint8_t(action >> 24); }
    inline uint16_t ActionA(uint32_t action) { return uint16_t(action >> 8); }
    inline uint8_t ActionB(uint32_t action) { return uint8_t(action); }
}

#endif
