/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_COMPANION_MOVES_H
#define FUSION_COMPANION_MOVES_H

#include "fusion/Companions.h"

#include <array>
#include <vector>

// A companion's moveset: its basic attack, its family trait, one ability per stage (the owner
// picks one of two at each evolution, and at stage 1 on the first summon), and a mounted move
// for a rideable final form. Data from design/COMPANION_CATALOG.md section 2.
//
// The numbers are WOMW's, the vanilla spell is the vehicle: the server casts the spell from the
// companion (triggered) for its icon, animation and aura, and sets its damage from the
// companion's own basic hit (BH, the average of Stats' damageMin and damageMax), so abilities
// scale with role, stage and level and stay under the hunter-pet yardstick.
//
// The pet bar (the game's own, SMSG_PET_SPELLS) is laid out by PetBarSlots; the autocast is
// WOMW's own, a condition per ability type (ShouldAutocast) instead of vanilla's "whenever it is
// off cooldown".
namespace Fusion::Companions
{
    // What an ability does, for the autocast conditions and the server's handling.
    enum class AbilityKind : uint8_t
    {
        Damage = 0,     // single target
        Burst = 1,      // single target, waits for a target under 50% or a boss
        AoE = 2,        // damages enemies around it, in a cone, or at a point
        Taunt = 3,
        Heal = 4,       // `pct` is a share of the healed unit's max health
        Root = 5,       // roots and snares
        Interrupt = 6,
        Stun = 7,
        AoEStun = 8,
        Debuff = 9,
        Buff = 10,      // the owner and the party
        Defensive = 11, // on itself
        Dispel = 12,
        GapCloser = 13,
        Fear = 14,
        Mounted = 15,   // cast while ridden, never autocast
    };

    // Whom it is cast on: an enemy (the crosshair's), itself, its owner, a ground point (the
    // crosshair's), or around itself / in a cone in front of it (no target needed).
    enum class AbilityAim : uint8_t
    {
        Enemy = 0,
        Self = 1,
        Owner = 2,
        Ground = 3,
        Around = 4,
    };

    struct Ability
    {
        uint32_t spell = 0;         // 0: no ability
        char const* name = "";
        AbilityKind kind = AbilityKind::Damage;
        AbilityAim aim = AbilityAim::Enemy;
        uint16_t pct = 0;           // direct damage, % of BH (heals: % of max health)
        uint16_t dotPct = 0;        // damage (or healing) over time, % of BH, the whole duration
        uint8_t cooldown = 0;       // seconds
        uint8_t duration = 0;       // its effect's seconds (CC, buff, DoT); 0 = the spell's own

        bool Exists() const { return spell != 0; }
    };

    enum class Moveset : uint8_t
    {
        None = 0,
        Critter, Spider, Bear, Cat, Wolf, Raptor, Scorpid, Crocolisk, Turtle, Gorilla, Boar, Crab,
        WindSerpent, CarrionBird, Roc, Hyena, Owl, Bat, Tallstrider, Kodo, Basilisk, Threshadon, Hydra,
        Dragonkin, FaerieDragon, FireElemental, WaterElemental, EarthElemental, AirElemental, BogBeast,
        PoisonElemental, Ooze, Infernal, Imp, Felhound, Felguard, Darkhound, Skeleton, Ghoul, Banshee,
        CryptFiend, HarvestGolem, Shredder, SpiderTank, Mechanostrider, Horse, SkeletalHorse, Ram,
        Gnoll, Kobold, Trogg, Ogre, Centaur, Quilboar, Furbolg, Harpy, Troll, Naga, Makrura, Moonkin,
        Worgen, Murloc,
        // The flying mounts' lines (species 215-223): their mounted move is kSkySurge.
        Gryphon, WindRider, Hippogryph, Dragonhawk, Drake, Phoenix,
        Count
    };

    struct MovesetInfo
    {
        Moveset id = Moveset::None;
        char const* name = "";
        uint32_t basicSpell = 0;    // its basic attack's look (0: a plain melee swing)
        char const* trait = "";     // the family trait, always on
        std::array<std::array<Ability, 2>, kMaxStages> stages{};
        Ability mounted;            // a rideable final form's move
    };

    MovesetInfo const& MovesetData(Moveset m);
    // The moveset of a species: by family, with the few lines that differ from their family
    // (rocs, golems, shredders, spider tanks, crypt fiends) by id.
    Moveset MovesetOf(CompanionSpecies const& sp);

    // ── Ability picks ───────────────────────────────────────────────────────────────────────
    // CompanionState::picks[stage]: 0 not picked yet, 1 the first option, 2 the second.
    // The stage whose pick is owed (the companion reached it and has not chosen), or -1.
    int PendingPick(CompanionSpecies const& sp, CompanionState const& s);
    // Choose for that stage; false if the stage is not reached, already chosen (use Repick at a
    // stable master), or the option has no ability.
    bool Pick(CompanionSpecies const& sp, CompanionState& s, uint8_t stage, uint8_t option);
    // The abilities it has now, stage order: the picked option of each reached stage (both for
    // a Twin-souled stage, see CompanionTraits.h). Skips unpicked stages.
    std::vector<Ability> ActiveAbilities(CompanionSpecies const& sp, CompanionState const& s);
    // The mounted move, when its current stage is ridden.
    Ability MountedMove(CompanionSpecies const& sp, CompanionState const& s);

    // ── Launch: the flying mounts' mounted move ─────────────────────────────────────
    // The flying lines skyride (Skyriding.h, Riding.h's Phase::Skyride): momentum flight, never
    // free. Their mounted move is Launch, a throw straight up that spends a vigor charge
    // (Skyriding::Vigor, the real limit): off the ground it takes off into a skyride, in one it
    // climbs again. The slot's own cooldown is only the gap between presses. The spell is only
    // its icon (Slow Fall's feather); nothing is cast.
    constexpr uint32_t kSkySurgeSpell = 130;
    constexpr uint8_t kSkySurgeCooldown = 1;   // seconds
    bool IsSkySurge(Ability const& a);

    // ── The basic attack ────────────────────────────────────────────────────────────────────
    // A caster's basic attack is a bolt from range (design/COMPANION_CATALOG.md section 2: the
    // whelps' and imps' Firebolt, the elementals' bolts, Lightning Breath, Acid and Toxic Spit,
    // Shadow Bolt, Shoot, Throw): the moveset's basic spell, cast at its attack speed for one
    // basic hit, from kRangedReach. 0 for a companion that swings in melee: every melee and tank
    // role, and the ranged lines whose catalog basic is a claw or a strike (rocs, harpies,
    // quilboar, moonkin, the clockwork squirrel).
    uint32_t RangedBasicSpell(CompanionSpecies const& sp);
    // Whether a spell is one of the ranged basic attacks.
    bool IsRangedBasic(uint32_t spell);
    // A Gunner's basic attack (CompanionTraits.h): the NPC gunners' shot, from kRangedReach.
    constexpr uint32_t kGunnerShotSpell = 6660;
    // The basic attack a companion fires from range now: a Gunner's gun (its trait swaps the
    // basic attack, design section 3), else its line's bolt; 0 when it swings in melee.
    uint32_t RangedAttackSpell(CompanionSpecies const& sp, CompanionState const& s);
    // How far a ranged basic attack reaches: it chases to kRangedReach and fires within this.
    constexpr float kRangedFireRange = 30.f;

    // ── Numbers ─────────────────────────────────────────────────────────────────────────────
    // The basic hit: the average of its basic attack's damage range.
    inline float BasicHit(CompanionStats const& st) { return (st.damageMin + st.damageMax) * 0.5f; }
    // An ability's direct and over-time amounts for a companion with `stats` (healing: of
    // `healedMaxHealth`).
    uint32_t AbilityDirect(Ability const& a, CompanionStats const& st, uint32_t healedMaxHealth = 0);
    uint32_t AbilityOverTime(Ability const& a, CompanionStats const& st, uint32_t healedMaxHealth = 0);
    // Its cooldown in ms, shortened by Primal (CompanionTraits.h) when `primal`.
    uint32_t AbilityCooldownMs(Ability const& a, bool primal);

    // ── Autocast ────────────────────────────────────────────────────────────────────────────
    // What the companion sees every half second, filled by the server.
    struct AutocastContext
    {
        bool hasTarget = false;             // an enemy it is fighting (its victim)
        float targetDistance = 0.f;
        float targetHealth = 1.f;           // 0..1
        bool targetIsBoss = false;
        bool targetCasting = false;         // a cast or channel of 1 s or longer
        bool targetAttacksCompanion = false;
        bool targetInMeleeWithOwner = false;
        bool targetApproachingOwner = false;    // within 15 yd and closing (a charging melee)
        bool targetFleeing = false;
        bool targetHasIt = false;           // its debuff / root is already on the target
        uint8_t targetStacks = 0;
        uint8_t maxStacks = 1;
        float targetItExpiresS = 99.f;      // seconds left on its debuff
        bool ownerHitByOther = false;       // an enemy not attacking the companion hit the owner in 2 s
        bool allyUnderHalfAttacked = false;
        float ownerHealth = 1.f;
        float companionHealth = 1.f;
        bool hotFromIt = false;             // its heal-over-time is already on the hurt one
        uint8_t alliesUnder70 = 0;
        uint8_t enemiesNear = 0;            // in its reach (melee AoE 8 yd)
        uint8_t enemiesInMeleeWithUs = 0;   // in melee with the owner or the companion
        uint8_t attackersOnCompanion = 0;
        bool wouldBreakCC = false;          // an AoE would hit a crowd-controlled enemy
        bool ownerInCombat = false;
        bool ownerFiring = false;           // the owner started shooting this tick
        bool ownerHasBuff = false;
        bool allyDispellable = false;
        bool enemyDispellable = false;
        bool dungeonOtherPacksNear = false;
        bool riding = false;
    };
    // Whether to cast `a` now (it is off cooldown and set to autocast).
    bool ShouldAutocast(Ability const& a, AutocastContext const& c);

    // ── The pet bar ─────────────────────────────────────────────────────────────────────────
    // Zero-based slots of the ten-button bar (Ctrl+1..Ctrl+0).
    namespace PetBarSlot
    {
        constexpr uint8_t Command = 0;      // context: attack / move and hold / follow
        constexpr uint8_t Stance = 1;       // Assist (vanilla Defensive) <-> Aggressive
        constexpr uint8_t Hold = 2;         // vanilla Passive + stay
        constexpr uint8_t Ability1 = 3;     // stage 1, 2, 3 abilities
        constexpr uint8_t Ability2 = 4;
        constexpr uint8_t Ability3 = 5;
        constexpr uint8_t Mounted = 6;      // the mounted move, while ridden
        constexpr uint8_t Twin = 7;         // a Twin-souled stage's second ability
        constexpr uint8_t Follow = 8;
        constexpr uint8_t Stay = 9;
        constexpr uint8_t Count = 10;
    }
    // The ability in an ability slot (Ability1..3, Twin), or none.
    Ability AbilityInSlot(CompanionSpecies const& sp, CompanionState const& s, uint8_t slot);
}

#endif
