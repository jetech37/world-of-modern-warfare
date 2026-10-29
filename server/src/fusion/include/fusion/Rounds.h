/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_ROUNDS_H
#define FUSION_ROUNDS_H

#include "fusion/Common.h"
#include "fusion/Construction.h"
#include "fusion/Creatures.h"

#include <cstdint>
#include <string>
#include <vector>

// Enchanted rounds (design doc section 12, Enchanting): consumables an enchanter makes that load
// a special round into the gun in hand for a few magazines or a few seconds. Every round is an
// effect with a trade-off, never plain extra damage: incendiary burns but hits a little softer,
// frost slows but the gun cycles slower, armor-piercing ignores part of the bullet armor but
// does less to anything soft, arcane burns mana for a little damage, holy bites the undead and
// demons and does less to everything else. They sit beside the quartermaster's special ammo
// (Damage.h AmmoType), which they do not touch.
namespace Fusion::Rounds
{
    enum class Round : uint8_t
    {
        None,
        Incendiary,
        Frost,
        ArmorPiercing,
        Arcane,         // burns mana
        Holy,
        Count,
    };

    struct RoundDef
    {
        Round round = Round::None;
        char const* name = "";              // "Incendiary Rounds"
        char const* tag = "";               // the HUD's tag, "INCENDIARY"
        // A load lasts this many magazines of the gun it went into (a lesser item; a greater one
        // twice as many), or, when `seconds` is set, that long instead.
        uint8_t magazines = 0;
        uint32_t seconds = 0;
        float damageMultiplier = 1.f;       // on every hit (the trade-off)
        float fireTimeMultiplier = 1.f;     // above 1: the gun cycles slower
        float burnShare = 0.f;              // incendiary: this share of the hit again, burned over
        uint32_t burnMs = 0;                //   this long (fire elementals never burn)
        float slowPercent = 0.f;            // frost: a short slow (the glue's Chilled), not on
        uint32_t slowMs = 0;                //   targets immune to slows
        float armorBypass = 0.f;            // armor-piercing: share of the bullet armor cut ignored
        float manaBurnShare = 0.f;          // arcane: mana burned for health taken, see ManaBurned
        float holyMultiplier = 1.f;         // holy: the multiplier against undead and demons,
                                            //   in place of damageMultiplier
        uint32_t tracerRgb = 0xFFFFFF;      // the tracer's colour, 0xRRGGBB
    };

    RoundDef const& Def(Round r);

    // ── Items (item_template entries 93050-93059; world SQL 10_enchanted_rounds.sql) ──
    // Lesser and greater of each round in Round order: 93050 lesser incendiary, 93051 greater
    // incendiary, 93052 lesser frost and so on. A greater load lasts twice as long.
    constexpr uint32_t kFirstItem = 93050;
    constexpr uint32_t kLastItem = 93059;
    struct RoundItem
    {
        Round round = Round::None;
        uint8_t grade = 0;          // 1 lesser, 2 greater
    };
    RoundItem ItemRound(uint32_t entry);            // Round::None for any other item
    uint32_t ItemFor(Round r, uint8_t grade);       // 0 for none
    std::string ItemName(Round r, uint8_t grade);   // "Lesser Incendiary Rounds"

    // ── A load in a gun ──
    struct Loaded
    {
        Round round = Round::None;
        uint16_t gun = 0;           // the catalog gun it went into
        uint32_t roundsLeft = 0;    // by magazines: rounds still to fire
        uint32_t untilMs = 0;       // by time: when it ends (server clock)
        bool timed = false;

        bool Active(uint32_t nowMs) const;
        // The round a pull of `gun` fires: the load's while it lasts and the gun is the one it
        // went into, else None. A pull by magazines uses up one round; the load ends at none
        // left or at its time.
        Round Fire(uint16_t gunId, uint32_t nowMs);
        // What is left to show: rounds, or milliseconds for a timed load.
        uint32_t Left(uint32_t nowMs) const;
    };

    // A load of `r` at `grade` into a gun whose magazine holds `magSize`.
    Loaded Load(Round r, uint8_t grade, uint16_t gun, uint32_t magSize, uint32_t nowMs);

    // ── A hit ──
    struct TargetFacts
    {
        CreatureType type = CreatureType::Humanoid;
        bool isPlayer = false;
        bool fireElemental = false;     // never burns
        bool slowImmune = false;        // world bosses
        uint32_t maxMana = 0;
    };

    struct HitEffect
    {
        float damageMultiplier = 1.f;
        float burnShare = 0.f;          // of the hit's damage, burned over burnMs
        uint32_t burnMs = 0;
        bool slow = false;
        float armorBypass = 0.f;
        float manaBurnShare = 0.f;
    };
    HitEffect OnHit(Round r, TargetFacts const& t);

    // Armor-piercing against a bullet armor cut (Scale::PlayerArmorCut; package W3's bullet
    // armor when it lands): the factor that turns damage after the full cut into damage after
    // the cut less `bypass` of it. 1 when nothing is cut.
    float ArmorBypassScale(float armorCut, float bypass);

    // Arcane: the mana a hit burns, `healthShare` the share of the target's health it took:
    // that share of its mana times the round's share.
    uint32_t ManaBurned(float healthShare, uint32_t maxMana, float share);

    // Incendiary burns: one per target per shooter, ticking every kBurnTickMs. A new hit on a
    // burning target keeps the larger of what is left and the new burn, over a fresh duration.
    constexpr uint32_t kBurnTickMs = 1000;
    struct Burn
    {
        uint64_t target = 0;
        uint16_t weapon = 0;
        float left = 0.f;           // damage still to deal
        uint8_t ticks = 0;          // ticks still to come
        uint32_t nextMs = 0;
    };
    void AddBurn(std::vector<Burn>& burns, uint64_t target, uint16_t weapon, float total, uint32_t durationMs, uint32_t nowMs);
    struct BurnTick
    {
        uint64_t target = 0;
        uint16_t weapon = 0;
        float damage = 0.f;
    };
    // The ticks due by now; finished burns are dropped.
    std::vector<BurnTick> TickBurns(std::vector<Burn>& burns, uint32_t nowMs);

    // ── Recipes: Enchanting (skill line 333), learned from an enchanting trainer ──
    constexpr uint16_t kSkillEnchanting = 333;
    struct Recipe
    {
        Construction::CraftRecipe craft;    // id, skill, product (the round item), reagents
        uint32_t trainCopper = 0;           // the trainer's fee to learn it
    };
    // The recipes, by required skill. Ids 62600-62609 (the package's custom range).
    std::vector<Recipe> const& Recipes();
    Recipe const* FindRecipe(uint32_t id);

    // Whether a creature teaches them: a profession trainer (trainer type 2) whose title names
    // enchanting ("Expert Enchanter", "Enchanting Trainer").
    bool IsEnchantingTrainer(uint8_t trainerType, std::string const& subName);

    enum class LearnCheck : uint8_t { Ok, Known, NoEnchanting, SkillTooLow, NotEnoughMoney };
    LearnCheck CanLearn(Recipe const& r, bool known, bool hasEnchanting, uint16_t skill, uint32_t money, bool gm);

    // ── The wire: SMSG_FUSION_CHANNEL, kChannelWorld ──
    // op 64: [64, round u8, gun u16, timed u8, left u32]: the load in a gun now (round 0: none,
    // the load ended); `left` rounds, or milliseconds when timed.
    constexpr uint8_t kOpLoaded = 64;
    std::vector<uint8_t> EncodeLoaded(Loaded const& l, uint32_t nowMs);
    bool DecodeLoaded(std::vector<uint8_t> const& body, Loaded& out);
}

#endif
