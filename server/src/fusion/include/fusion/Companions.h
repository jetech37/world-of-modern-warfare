/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_COMPANIONS_H
#define FUSION_COMPANIONS_H

#include "fusion/Common.h"

#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

// Battle companions: a creature that fights at a player's side, summoned from an item.
//
// There is no capturing. Every companion comes from a companion ITEM, a rare drop (critters
// included), and the item is always Bind on Equip, so it can be traded, mailed and auctioned.
// The companion's level, experience and evolution stage belong to the item INSTANCE (keyed by its
// item guid), not to the character: selling a level 40 drake sells the drake at 40. Keeping many
// companions costs bag and bank space, which is the point.
//
// A companion is the pet of every class: it takes the pet slot (hunters' and warlocks' own pets
// are gone), is always weaker than a hunter pet of its level (HunterPetBaseline), and can never
// fight above its owner's level. Only one companion is out at a time.
//
// See docs/COMPANIONS.md for how the game server and the client use these rules.
namespace Fusion::Companions
{
    constexpr uint8_t kMaxLevel = 60;
    constexpr uint8_t kMaxStages = 3;

    // Item entries reserved for the system in the world database (sql/custom/azeroth_warfare/
    // 02_companions.sql, 08_companion_lines.sql): one companion item per species (90000 + id
    // for the starter set, 91000 + id from species 101), then the evolution catalysts.
    constexpr uint32_t kItemFirst = 90001;
    constexpr uint32_t kItemLast = 90099;
    constexpr uint32_t kLineItemFirst = 91100;
    constexpr uint32_t kLineItemLast = 91999;
    constexpr uint32_t kCatalystFirst = 90101;
    constexpr uint32_t kCatalystLast = 90199;
    inline bool IsCompanionItem(uint32_t entry)
    {
        return (entry >= kItemFirst && entry <= kItemLast) || (entry >= kLineItemFirst && entry <= kLineItemLast);
    }
    // A species' own item entry by the convention above.
    inline uint32_t SpeciesItem(uint16_t id) { return id < 100 ? 90000u + id : 91000u + id; }
    inline bool IsCatalystItem(uint32_t entry) { return entry >= kCatalystFirst && entry <= kCatalystLast; }

    // The evolution catalysts, each dropped by its creature type (CatalystFor).
    namespace Catalyst
    {
        constexpr uint32_t DraconicEssence = 90101;     // dragonkin
        constexpr uint32_t PrimalBeastEssence = 90102;  // beasts
        constexpr uint32_t ElementalCore = 90103;       // elementals
        constexpr uint32_t FelEmber = 90104;            // demons
        constexpr uint32_t NecroticResidue = 90105;     // undead
        constexpr uint32_t ArcaneGearwork = 90106;      // mechanicals
        constexpr uint32_t TribalFetish = 90107;        // humanoids
    }

    // What a companion fights like. The role sets its stat curve: a melee companion is the
    // all-rounder, a ranged one (whelps breathing fire, elementals casting bolts) hits from
    // kRangedReach and is fragile, a tank is hard to kill and hits softly.
    enum class Role : uint8_t
    {
        Melee = 0,
        Ranged = 1,
        Tank = 2,
    };

    // The creature family, for the client's bar icon and for flavor; stats come from the role.
    enum class Family : uint8_t
    {
        Critter = 0,
        Dragon = 1,
        FaerieDragon = 2,
        Wolf = 3,
        Cat = 4,
        Bear = 5,
        Spider = 6,
        Raptor = 7,
        Crocolisk = 8,
        Gorilla = 9,
        Owl = 10,
        Bat = 11,
        Tallstrider = 12,
        Scorpid = 13,
        FireElemental = 14,
        WaterElemental = 15,
        EarthElemental = 16,
        AirElemental = 17,
        Murloc = 18,
        Infernal = 19,
        Ooze = 20,
        Mechanical = 21,
        // The mount families (their last stage is ridden, see StageInfo::rideable).
        Kodo = 22,
        Horse = 23,
        Mechanostrider = 24,
        SkeletalHorse = 25,
        Ram = 26,
        // The new lines (design/COMPANION_CATALOG.md, species 101-164).
        Hyena = 27,
        Boar = 28,
        Crab = 29,
        Turtle = 30,
        WindSerpent = 31,
        CarrionBird = 32,
        Basilisk = 33,
        Threshadon = 34,
        Hydra = 35,
        BogBeast = 36,
        Imp = 37,
        Felhound = 38,
        Felguard = 39,
        Skeleton = 40,
        Ghoul = 41,
        Banshee = 42,
        Gnoll = 43,
        Kobold = 44,
        Trogg = 45,
        Ogre = 46,
        Centaur = 47,
        Quilboar = 48,
        Furbolg = 49,
        Harpy = 50,
        Troll = 51,
        Naga = 52,
        Makrura = 53,
        Moonkin = 54,
        Worgen = 55,
        PoisonElemental = 56,
        Darkhound = 57,
        CryptFiend = 58,
        // The flying mounts' lines (35_flying_companions.sql, species 215-223): they skyride
        // (Skyriding.h), taking off with Launch (CompanionMoves.h, kSkySurge).
        Gryphon = 59,
        WindRider = 60,
        Hippogryph = 61,
        Dragonhawk = 62,
    };
    // The last family id (the tables that list every family run to it).
    constexpr Family kLastFamily = Family::Dragonhawk;
    // Whether a family's companions are humanoids (they can roll Gunner, never ride): the
    // murlocs and the humanoid lines.
    bool IsHumanoidFamily(Family f);

    // ── Evolution ───────────────────────────────────────────────────────────────────────────
    // Many companions start as a baby (whelpling, pup, cub, spark) and grow into a larger form of
    // the same creature (whelp, drake). Evolving is a goal, not a timer: the companion must reach
    // a level, and a rule may also want a catalyst item, a place that fits the creature (a black
    // whelp grows up among its flight in Blackrock or the Wyrmbog, a fire spark at Fire Plume
    // Ridge), a time of day, or a deed done while it was out (kills of a creature type).
    enum class Condition : uint8_t
    {
        None = 0,
        Night = 1,      // server game time 20:00 to 05:59
        Day = 2,        // 06:00 to 19:59
        KillType = 3,   // `conditionCount` kills of `conditionType` creatures while summoned
    };

    struct EvolutionRule
    {
        uint8_t level = 0;              // 0: there is no rule (the stage is final)
        uint32_t item = 0;              // a catalyst consumed by evolving, 0 = none
        uint8_t itemCount = 0;
        std::vector<uint32_t> areas;    // zone or area ids, any one of them; empty = anywhere
        Condition condition = Condition::None;
        CreatureType conditionType = CreatureType::None;
        uint16_t conditionCount = 0;

        bool Exists() const { return level > 0; }
    };

    // Where and how the owner is when asking to evolve; the server fills it in.
    struct EvolveContext
    {
        uint8_t ownerLevel = 1;
        bool summoned = false;      // the companion must be out: it evolves in front of you
        bool inCombat = false;      // the owner or the companion
        bool inDuel = false;
        uint32_t zoneId = 0;
        uint32_t areaId = 0;
        uint32_t itemsHeld = 0;     // how many of the rule's catalyst the owner carries
        uint8_t hour = 12;          // server game time, 0..23
    };

    // Why a companion can't evolve now, in the order they're checked; None = it can. The value
    // goes to the client (the evolve button's tooltip).
    enum class EvolveBlock : uint8_t
    {
        None = 0,
        FinalStage = 1,
        NotSummoned = 2,
        InCombat = 3,
        InDuel = 4,
        LevelTooLow = 5,
        WrongPlace = 6,
        WrongTime = 7,
        NeedKills = 8,
        MissingItem = 9,
    };

    // ── Species ─────────────────────────────────────────────────────────────────────────────
    struct StageInfo
    {
        uint32_t creature = 0;      // creature_template entry summoned at this stage (its model)
        float scale = 1.f;          // drawn at this scale: a drake at 0.6 still fits a companion
        // A mount family's last stage is a mount: it walks and fights at its owner's side like
        // any companion, and its owner can ride it. Riding casts a vanilla mount spell, so the
        // model (the spell's aura creature, also this stage's `creature`), the speed and every
        // vanilla mount rule come with it: `mountSpell` the 60% one, `swiftMountSpell` the 100%.
        bool rideable = false;
        uint32_t mountSpell = 0;
        uint32_t swiftMountSpell = 0;
        // A line with no vanilla mount of its own rides on family-neutral mount spells (the
        // wolf's) and the server shows this display instead of the spell's
        // (UNIT_FIELD_MOUNTDISPLAYID); 0 = the spell's own display.
        uint32_t rideDisplay = 0;
        // The creature display this stage wears (the look table, 29_companion_looks.sql): each
        // form of a line its own model or skin, a mount line's last form a mount model. 0 = its
        // creature's own display. `scale` is on top of the display's own (CreatureDisplayInfo's),
        // as vanilla scales a creature (ObjectScale).
        uint32_t display = 0;
    };

    // Which creatures drop a species' item: listed entries first (a black whelp from black
    // dragonkin), else a creature of `type` whose pet family is `petFamily` (0: any of the type).
    struct DropSource
    {
        std::vector<uint32_t> entries;
        CreatureType type = CreatureType::None;    // None: entries only
        uint8_t petFamily = 0;                     // creature_template.pet_family
    };

    struct CompanionSpecies
    {
        uint16_t id = 0;
        std::string name;
        uint32_t item = 0;                          // the item that summons it
        Family family = Family::Critter;
        Role role = Role::Melee;
        uint8_t stageCount = 1;
        std::array<StageInfo, kMaxStages> stages{};
        // evolutions[i] takes stage i to stage i + 1.
        std::array<EvolutionRule, kMaxStages - 1> evolutions{};
        DropSource drops;
    };

    // The starter set, mirrored row for row by the world database's fusion_companion_species
    // (sql/custom/azeroth_warfare/02_companions.sql; a test holds the two together). The server
    // loads the table, so species can be added in SQL alone; this copy is for tests and tools.
    std::vector<CompanionSpecies> const& StarterSpecies();
    CompanionSpecies const* FindSpecies(std::vector<CompanionSpecies> const& all, uint16_t id);
    CompanionSpecies const* FindByItem(std::vector<CompanionSpecies> const& all, uint32_t itemEntry);

    // ── One companion: the state of one item instance ───────────────────────────────────────
    // Stored per item guid (characters.character_fusion_companion). A fresh drop is level 1,
    // stage 0, no xp.
    struct CompanionState
    {
        uint16_t species = 0;
        uint8_t stage = 0;
        uint8_t level = 1;
        uint32_t xp = 0;            // toward the next level
        uint16_t evolveKills = 0;   // kills counted toward the next evolution's KillType condition
        // Rolled when the item dropped (CompanionTraits.h): a bitmask of Trait ids (bit id - 1),
        // its rarity, and the stage a Twin-souled trait doubles (0 none, 1-3).
        uint32_t traits = 0;
        uint8_t rarity = 0;
        uint8_t twinStage = 0;
        // The owner's ability choice per stage (CompanionMoves.h): 0 not yet, 1 or 2.
        std::array<uint8_t, kMaxStages> picks{};
        // Breeding (CompanionTraits.h): times bred, when it may breed again (unix seconds), its
        // generation (0 wild), its parents' species, and the species whose coloring it wears.
        uint8_t breedCount = 0;
        uint32_t breedReadyAt = 0;
        uint8_t generation = 0;
        uint16_t parentA = 0;
        uint16_t parentB = 0;
        uint16_t hybrid = 0;
    };

    // Items that start a species further along than its own item does: the mounts' companion
    // items (sold where the vendors sold vanilla mounts) start at the rideable stage, level 40.
    // Mirrored by the world database's fusion_companion_item (02_companions.sql).
    struct ItemStart
    {
        uint32_t item = 0;
        uint16_t species = 0;
        uint8_t stage = 0;
        uint8_t level = 1;
    };
    std::vector<ItemStart> const& StarterItems();
    // An item instance with no saved progress yet: its species, and the state it starts in
    // (`out`). Its species' own item starts at stage 0, level 1; an ItemStart where it says.
    // nullptr for an item that is no companion.
    CompanionSpecies const* FreshState(std::vector<CompanionSpecies> const& all, std::vector<ItemStart> const& items,
        uint32_t itemEntry, CompanionState& out);

    // The vanilla 1.12 pet items, retired: each is replaced by the companion item of a species whose
    // first form is that pet (31_vanilla_companions.sql turns every world source into the new item).
    struct VanillaPet
    {
        uint32_t oldItem = 0;       // the vanilla pet item (8485 Cat Carrier (Bombay))
        uint32_t item = 0;          // its companion item, the species' own (stage 1, level 1)
        uint16_t species = 0;
    };
    std::vector<VanillaPet> const& VanillaPets();
    // The companion item that replaces a vanilla pet item, 0 for any other item.
    uint32_t VanillaPetReplacement(uint32_t oldItem);
    // The characters database statements that swap every copy of a retired pet item (bags, bank,
    // mail, auction house) for its companion item; run at startup, so a second run changes nothing.
    std::vector<std::string> VanillaPetSwapSql();

    // The level it fights at: never above its owner's. A level 40 companion bought by a level 20
    // character fights at 20 but keeps its 40 on the item.
    inline uint8_t EffectiveLevel(CompanionState const& s, uint8_t ownerLevel)
    {
        return std::max<uint8_t>(1, std::min(s.level, ownerLevel));
    }

    // ── Experience ──────────────────────────────────────────────────────────────────────────
    // A player's 1.12 experience to the next level: ((8 L + diff(L)) * (45 + 5 L)), rounded to 100.
    uint32_t PlayerXpToNext(uint8_t level);
    // A companion needs kXpFactor of that (0 at 60), and it gets a kill's full experience rather
    // than a share of it, so one out from level 1 levels about two thirds faster than its owner
    // and catches up with an owner who found it late, then waits at the owner's level.
    constexpr float kXpFactor = 0.6f;
    uint32_t XpToNext(uint8_t level);

    // 1.12's gray level: a creature at or below it is worth nothing (critters always are).
    uint8_t GrayLevel(uint8_t level);
    // 1.12's zero-difference value, how fast a lower creature's experience falls off.
    uint8_t ZeroDifference(uint8_t level);
    // A kill's experience to a companion of `companionLevel` (pass its effective level): 1.12's
    // creature experience, (5 L + 45) at the same level, +5% a level above (to +4 levels), falling
    // to nothing at the gray level, doubled for an elite. A companion earns it for a kill it took
    // part in (it damaged the victim, or was out and in combat with it when it died).
    uint32_t KillXp(uint8_t companionLevel, uint8_t victimLevel, bool victimElite);

    struct XpGain
    {
        uint8_t levels = 0;     // levels gained
        bool capped = false;    // held back by the owner's level, the bar full and waiting
        uint32_t wasted = 0;    // experience past a full bar (or any at 60), dropped
    };
    // Add experience, levelling up to the owner's level and no further. At the owner's level the
    // companion banks experience up to a full bar (xp == XpToNext(level)) and drops the rest; when
    // the owner levels, GainXp(s, 0, newLevel) cashes the full bar in at once. A companion already
    // above its owner (it was traded) never loses a level, it just stops gaining them.
    XpGain GainXp(CompanionState& s, uint32_t amount, uint8_t ownerLevel);
    // Held back by its owner: at or above the owner's level with a full bar, so a kill earns it
    // nothing until the owner levels.
    bool IsCapped(CompanionState const& s, uint8_t ownerLevel);

    // ── Stats ───────────────────────────────────────────────────────────────────────────────
    struct CompanionStats
    {
        uint32_t health = 0;
        float damageMin = 0.f;
        float damageMax = 0.f;
        uint32_t armor = 0;
        uint32_t attackMs = 2000;
        float reach = 0.f;      // yards it attacks from: melee 0 (the server's melee range)

        float Dps() const { return (damageMin + damageMax) * 0.5f * 1000.f / float(attackMs); }
    };
    constexpr float kRangedReach = 20.f;

    // Following its owner as a hunter's pet does: behind and to the side (this angle from the
    // owner's facing), at a distance that grows with its size so a big one never stands in its
    // owner, pathing around what is in the way (the follow movement). Left farther behind than
    // kCatchUpYards out of a fight (a fall, a ledge, a door it could not path through), it
    // catches up at once, set down at its follow spot.
    constexpr float kFollowAngle = 2.5132741f;      // 0.8 pi: behind, to the left
    constexpr float kCatchUpYards = 35.f;
    inline float FollowDistance(float boundingRadius) { return 2.5f + 1.5f * boundingRadius; }
    inline bool ShouldCatchUp(float distance, bool fighting) { return !fighting && distance > kCatchUpYards; }
    // Where it walks when the navmesh walk from its owner to its follow spot can't be traced
    // (no mmaps loaded, as the packaged server runs, or no poly under the spot): vanilla falls
    // back to the owner's own position, which stood every pet in its owner. A companion keeps
    // its follow spot (the path finder still routes it), never the owner's position.
    enum class FollowFallback : uint8_t { FollowSpot, OwnerPosition };
    inline FollowFallback FollowFallbackOf(bool companion) { return companion ? FollowFallback::FollowSpot : FollowFallback::OwnerPosition; }
    // The point it follows to: `distance` from the owner, `angle` off the owner's facing.
    inline void FollowSpot(float ownerX, float ownerY, float ownerFacing, float distance, float angle, float& x, float& y)
    {
        x = ownerX + distance * std::cos(ownerFacing + angle);
        y = ownerY + distance * std::sin(ownerFacing + angle);
    }

    // The yardstick a companion stays under: a 1.12 hunter pet of `level` (happy, untalented,
    // average gear on its master): health 40 + 55 L, 1.5 + 0.8 L damage a second at a 2.0 attack,
    // 50 L armor; 3340 health, 49.5 dps and 3000 armor at 60.
    CompanionStats HunterPetBaseline(uint8_t level);

    // A role's share of the baseline at the adult stage, and each stage's multiplier on it. The
    // biggest share (a tank's armor, 0.8) times the biggest stage (an elder, 1.12) is 0.9: an
    // evolved companion comes close to a hunter pet in its best stat and never reaches it.
    struct RoleShare
    {
        float health, dps, armor;
        uint32_t attackMs;
    };
    RoleShare RoleShareOf(Role r);
    float StageMultiplier(uint8_t stage, uint8_t stageCount);

    // A companion's stats at `level` (pass its effective level) and stage.
    CompanionStats Stats(Role role, uint8_t stage, uint8_t stageCount, uint8_t level);
    inline CompanionStats Stats(CompanionSpecies const& sp, CompanionState const& s, uint8_t ownerLevel)
    {
        return Stats(sp.role, s.stage, sp.stageCount, EffectiveLevel(s, ownerLevel));
    }

    // ── Evolution checks ────────────────────────────────────────────────────────────────────
    // Whether night (Condition::Night) holds at `hour`.
    inline bool IsNight(uint8_t hour) { return hour >= 20 || hour < 6; }
    // The rule that takes the companion past its stage, or nullptr at its final stage.
    EvolutionRule const* NextRule(CompanionSpecies const& sp, CompanionState const& s);
    // Whether the rule lets the companion evolve now. Its level counts as its effective level:
    // a traded level 45 whelp in a level 30 character's bags waits for the character.
    EvolveBlock CanEvolve(EvolutionRule const& rule, CompanionState const& s, EvolveContext const& ctx);
    EvolveBlock CanEvolve(CompanionSpecies const& sp, CompanionState const& s, EvolveContext const& ctx);
    // Take the next stage (the caller consumed the rule's catalyst). Level and experience stay;
    // the kill count starts over for the next rule.
    void Evolve(CompanionState& s);
    // A kill made while the companion was out: counts toward its next rule's KillType condition.
    // Returns whether it counted.
    bool RecordKill(CompanionSpecies const& sp, CompanionState& s, CreatureType victimType);

    // ── Riding ──────────────────────────────────────────────────────────────────────────────
    // Every mount is a companion's last form. Riding follows vanilla's rules unchanged: level 40
    // and 75 riding skill for a 60% mount, level 60 and 150 for a 100% one (the swift spell). A
    // companion worn down below kRideMinHealth carries nobody until it has healed; a fainted one
    // (CompanionRest.h) can't be summoned, so it carries nobody until it is revived.
    constexpr uint8_t kRideLevel = 40;
    constexpr uint8_t kSwiftRideLevel = 60;
    constexpr uint16_t kRideSkill = 75;
    constexpr uint16_t kSwiftRideSkill = 150;
    constexpr float kRideMinHealth = 0.35f;

    // The mounted speed bonus (percent) a rider of `ownerLevel` with `ridingSkill` gets: 0, 60
    // or 100.
    uint8_t MountSpeed(uint8_t ownerLevel, uint16_t ridingSkill);
    // Whether the companion's current stage can be ridden at all.
    bool IsRideable(CompanionSpecies const& sp, CompanionState const& s);
    // The vanilla mount spell for a speed: the swift one at 100 when the stage has one.
    uint32_t RideSpell(StageInfo const& stage, uint8_t speed);

    struct RideContext
    {
        uint8_t ownerLevel = 1;
        uint16_t ridingSkill = 0;   // the rider's best riding skill
        bool summoned = false;      // it must be out to be mounted
        bool inCombat = false;      // the owner or the companion (vanilla: no mounting in combat)
        bool inDuel = false;        // a companion duel
        float healthShare = 1.f;    // the companion's health, 0..1
        bool mountAllowed = true;   // the map takes mounts and the rider is outdoors (no dungeons)
    };

    // Why a companion cannot be ridden now, in the order checked; None = it can.
    enum class RideBlock : uint8_t
    {
        None = 0,
        NotRideable = 1,
        NotSummoned = 2,
        InCombat = 3,
        InDuel = 4,
        LevelTooLow = 5,
        NoSkill = 6,
        Worn = 7,
        NotHere = 8,        // a dungeon, a raid or indoors: no mounts there
    };
    RideBlock CanRide(CompanionSpecies const& sp, CompanionState const& s, RideContext const& ctx);

    // The ride's half-second look. Riding casts the vanilla mount spell triggered: it keeps its
    // cast time (3 s), but a triggered spell with a cast time is not in its caster's spell slot,
    // so nothing but the clock says it is still being cast. Giving up on it early (the old 1.5 s
    // wait) put the companion back on foot before the aura landed, and the aura then landed on a
    // rider no longer riding: the spell's own mount showed (the Timber Wolf for every line that
    // rides on the wolf's spells, a tiger among them).
    enum class RideLook : uint8_t
    {
        Mounted = 0,    // in the saddle
        Waiting = 1,    // the mount spell is still being cast: hold
        Off = 2,        // off it (by choice, or knocked off): back on foot; remove the aura if on
        NeverTook = 3,  // the cast's time passed and no aura came (indoors, interrupted)
    };
    // Past the spell's cast time, how long its aura may still lag (a server tick or two).
    constexpr uint32_t kRideCastGraceMs = 1500;
    // `hasAura`: the mount spell's aura is on now; `mounted`: it has been on since the ride began;
    // `sinceMs`: since the ride began; `castMs`: the mount spell's cast time; `leaving`: its
    // owner asked to get off.
    RideLook LookAtRide(bool hasAura, bool mounted, uint32_t sinceMs, uint32_t castMs, bool leaving);

    // ── Mounting: a cast with a bar ──
    // Riding a companion is a cast its owner stands still for, a cast bar on the client (op 7):
    // meanwhile the companion runs to a spot just in front of its owner, and the mount completes as
    // it arrives (its aura put on at once, no mount spell's own cast, sound or name). Moving,
    // a fight, the companion gone or the ride key again breaks it.
    constexpr float kMountSpotYards = 1.5f;         // in front of the owner, past the companion's own size
    constexpr uint32_t kMountCastMinMs = 1200;      // never quicker than this, however near
    constexpr uint32_t kMountCastMaxMs = 3500;      // a companion far off runs faster to make it
    constexpr uint32_t kMountCastSettleMs = 300;    // it stops and turns before the owner climbs on
    constexpr float kMountCastMoveYards = 0.6f;     // the owner moving more than this breaks the cast
    // The cast's length for a companion `distance` yards from its spot, running `runSpeed` yards a
    // second; and the speed it runs at to arrive in that time (at least its own run).
    uint32_t MountCastMs(float distance, float runSpeed);
    float MountRunSpeed(float distance, float runSpeed, uint32_t castMs);
    // The spot in front of an owner at `owner` facing `facing` (radians, WoW's: x along cos, y
    // along sin) for a companion of `petRadius` yards.
    Vec3 MountSpot(Vec3 owner, float facing, float petRadius);
    // Whether a mount cast begun with its owner at `from` still holds with them at `now`.
    bool MountCastHolds(Vec3 from, Vec3 now);
    // Whether a spell is one some species is ridden with (a rideable stage's mount spells).
    bool IsCompanionRideSpell(std::vector<CompanionSpecies> const& all, uint32_t spell);
    // A mount aura of a companion ride spell that no companion ride accounts for: not the ride
    // in progress, not cast from an item (vanilla's own reins) and not a spell its rider knows.
    // It shows the spell's own mount (a wolf for the lines that ride on the wolf's spells): one
    // saved at a logout in the saddle and loaded at the next login, or one landing after the
    // ride was given up. The server takes it off.
    bool StrayRideAura(bool companionSpell, bool riding, uint32_t rideSpell, uint32_t auraSpell, bool fromItem,
        bool knownSpell);

    // ── Drops ───────────────────────────────────────────────────────────────────────────────
    // A companion item should be an event: critter 0.5%, normal 0.2%, rare 2%, elite 1%, rare
    // elite 5%, world or dungeon boss 3%. Critters are the most generous because they are the
    // least work and the cheapest companions; a rare elite is a hunt worth the trip. A totem, or a
    // creature with no type, never drops one. (The security review's 0.2% for critters was taken
    // back: the owner wants critters unrestricted, 2026-10, #21.)
    float DropChance(CreatureType type, CreatureRank rank, bool isCritter);

    struct DropVictim
    {
        uint32_t entry = 0;
        CreatureType type = CreatureType::None;
        uint8_t petFamily = 0;
        CreatureRank rank = CreatureRank::Normal;
        bool grayToKiller = false;      // too low to give its killer experience
        bool summoned = false;          // a summon, a pet, a guardian or a totem: made, not found
    };
    // Whether a kill may drop companion goods (a companion item or a catalyst) at all: never a
    // summon or a pet (a warlock's imp, a hunter's own beast: made, so killed again and again), and
    // never a creature gray to its killer (a level 60 sweeping a starting zone at three times the
    // respawn). Critters are always gray, but are the only home of a few small companions (the
    // alley cat, the wild rabbit): the gray rule spares them, and they drop as any other creature
    // (security review 2026-10, #21; no critter type has a catalyst, CatalystFor).
    bool DropsCompanionGoods(DropVictim const& v);
    // The species a creature can drop: listed entries win, then its type and pet family, then its
    // type alone. Empty if none.
    std::vector<uint16_t> DroppableSpecies(std::vector<CompanionSpecies> const& all, DropVictim const& v);
    // Roll for a companion item on a kill: `chanceRoll` and `pickRoll` uniform in [0, 1). Returns
    // the species (its item is CompanionSpecies::item), 0 for no drop.
    // Nothing when DropsCompanionGoods says no.
    uint16_t RollDrop(std::vector<CompanionSpecies> const& all, DropVictim const& v, float chanceRoll, float pickRoll);

    // The catalyst a creature type drops (0 for none), and the chance: only from creatures level
    // 25 and up (evolutions that want one start at 30), normal 0.4%, rare 2%, elite 1.5%, rare
    // elite 8%, boss 10%.
    uint32_t CatalystFor(CreatureType type);
    float CatalystDropChance(CreatureType type, CreatureRank rank, uint8_t victimLevel);
    // The same for a kill, 0 when it drops no companion goods (DropsCompanionGoods).
    float CatalystDropChance(DropVictim const& v, uint8_t victimLevel);

    // ── Companion duels ─────────────────────────────────────────────────────────────────────
    // Two players set their companions on each other while they stay out of it. Real time, not
    // turn based: the companions fight with their normal AI and stats. The rules:
    //  - Both companions must be out, both owners out of combat and within kChallengeRange.
    //  - The challenged player has kChallengeTimeoutMs to accept; then a kCountdownMs countdown,
    //    both companions healed to full, then the fight, at most kMaxFightMs.
    //  - Levels are normalized to the lower of the two effective levels (DuelLevels): a duel tests
    //    species, stage and role, not who levelled more. Stats return when it ends.
    //  - A companion brought to 1 health is beaten (the server stops the killing blow, as in a
    //    player duel); its owner loses. Fleeing (dismissed, or carried past kArenaRadius from
    //    where the fight began) loses; so does forfeiting, and so does an owner who joins in
    //    (attacks either companion or the other owner). At the time limit the higher health share
    //    wins, within 1% a draw. Anything before the fight starts cancels it with no result.
    //  - Nothing is lost. The winner earns DuelWinXp, nothing against a gray opponent.
    enum class DuelPhase : uint8_t
    {
        Idle = 0,
        Challenged = 1,
        Countdown = 2,
        Fighting = 3,
        Finished = 4,
    };

    enum class DuelEnd : uint8_t
    {
        None = 0,
        Beaten = 1,
        Forfeit = 2,
        Fled = 3,
        Interfered = 4,
        Timeout = 5,
        Declined = 6,
        Expired = 7,        // the challenge was not answered
        Cancelled = 8,      // called off before the fight began
    };

    struct DuelResult
    {
        DuelEnd end = DuelEnd::None;
        int8_t winner = -1;     // 0 the challenger, 1 the challenged, -1 nobody
    };

    std::pair<uint8_t, uint8_t> DuelLevels(uint8_t challengerLevel, uint8_t challengedLevel);
    // 5% of a level's experience, none against an opponent at or below the winner's gray level.
    uint32_t DuelWinXp(uint8_t winnerLevel, uint8_t loserLevel);
    // Whether a finished duel earns its winner experience: only a fight won (beaten, or ahead
    // when time ran out), never a forfeit, a flight or an owner stepping in, so two friends can't
    // farm wins by giving up at once.
    bool DuelPaysXp(DuelEnd end);
    // Paid duel wins a day, counted per winner's account (its characters together), so alts don't
    // multiply the cap (security review 2026-10, #30; it was per pair of characters).
    constexpr uint32_t kDuelWinsPerDay = 5;
    struct DuelWinsToday
    {
        uint32_t day = 0;               // unix days
        uint32_t wins = 0;              // paid wins that day
    };
    // Whether a win on `day` pays, counting it when it does; a new day starts the count over.
    bool PayDuelWin(DuelWinsToday& w, uint32_t day, uint32_t cap = kDuelWinsPerDay);
    // Whether a duel in this phase holds its companions (no summoning, swapping, evolving or
    // riding): the countdown and the fight. A challenge only waiting for an answer holds nothing,
    // so nobody can be kept from their companion by repeated challenges.
    bool DuelHolds(DuelPhase phase);
    // A player may challenge the same player again this long after their last challenge.
    constexpr uint32_t kRechallengeMs = 60 * 1000;

    class CompanionDuel
    {
    public:
        static constexpr float kChallengeRange = 30.f;
        static constexpr float kArenaRadius = 40.f;
        static constexpr uint32_t kChallengeTimeoutMs = 30000;
        static constexpr uint32_t kCountdownMs = 3000;
        static constexpr uint32_t kMaxFightMs = 180000;

        // Owners' guids. False unless idle and the challenge is between two different players.
        bool Challenge(uint64_t challenger, uint64_t challenged, uint32_t nowMs);
        // The challenged player says yes: the countdown starts.
        bool Accept(uint64_t who, uint32_t nowMs);
        bool Decline(uint64_t who);
        // Advance time: an unanswered challenge expires, the countdown ends in the fight, a
        // fight past the limit ends on health shares (each 0..1).
        void Update(uint32_t nowMs, float healthShareChallenger = 1.f, float healthShareChallenged = 1.f);
        // `who` is an owner's guid; a side not in the duel is ignored.
        void Beaten(uint64_t who);      // that owner's companion is at 1 health
        void Forfeit(uint64_t who);
        void Fled(uint64_t who);        // dismissed, died to something else, or left the arena
        void Interfered(uint64_t who);  // that owner attacked a companion or the other owner

        DuelPhase Phase() const { return m_phase; }
        DuelResult Result() const { return m_result; }
        bool Involves(uint64_t who) const { return m_phase != DuelPhase::Idle && Side(who) >= 0; }
        uint64_t Challenger() const { return m_owner[0]; }
        uint64_t Challenged() const { return m_owner[1]; }
        uint32_t CountdownLeftMs(uint32_t nowMs) const;
        // Whether a companion at `pos` is still inside the arena around `center`.
        static bool InArena(Vec3 center, Vec3 pos) { return (pos - center).Length() <= kArenaRadius; }

    private:
        int Side(uint64_t who) const;
        void Lose(uint64_t who, DuelEnd why);
        void Finish(DuelEnd why, int8_t winner);

        DuelPhase m_phase = DuelPhase::Idle;
        DuelResult m_result;
        std::array<uint64_t, 2> m_owner{};
        uint32_t m_sinceMs = 0;     // when the current phase began
    };

    // ── What the client sees: SMSG_FUSION_CHANNEL / CMSG_FUSION_CHANNEL bodies on their own channel,
    // kChannelCompanion, little-endian. First byte the op.
    constexpr uint8_t kChannelCompanion = 4;     // 3 is the achievements window (Achievements.h)
    // Server to client.
    constexpr uint8_t kOpBar = 1;           // the companion bar: one companion's state
    constexpr uint8_t kOpEvolved = 2;       // a companion evolved (play the effect)
    constexpr uint8_t kOpDuel = 3;          // a companion duel's phase or result
    constexpr uint8_t kOpMoves = 4;         // the companion out's moveset, choices and autocasts
    constexpr uint8_t kOpCooldown = 5;      // a pet bar slot went on cooldown
    constexpr uint8_t kOpLineage = 6;       // a bred companion item's lineage (by its seed)
    // [7, phase u8, cast ms u32]: the mount cast's bar (MountCastPhase).
    constexpr uint8_t kOpMountCast = 7;
    enum class MountCastPhase : uint8_t
    {
        Start = 0,      // the bar starts, `ms` long
        Done = 1,       // in the saddle
        Broken = 2,     // moved, a fight, the companion gone, or called off
    };
    // Client to server.
    constexpr uint8_t kOpSummon = 10;       // [10, item guid u64]: summon (or dismiss, if out)
    constexpr uint8_t kOpDismiss = 11;      // [11]
    constexpr uint8_t kOpEvolve = 12;       // [12, item guid u64]
    constexpr uint8_t kOpDuelChallenge = 13;    // [13, target player guid u64]
    constexpr uint8_t kOpDuelAnswer = 14;   // [14, accept u8]
    constexpr uint8_t kOpDuelForfeit = 15;  // [15]
    constexpr uint8_t kOpRide = 16;         // [16]: mount the companion out, or get off it
    constexpr uint8_t kOpPick = 17;         // [17, item guid u64, stage u8, option u8 (1-2)]
    // [18, slot u8 (0-9), target guid u64, flags u8, x f32, y f32, z f32]: a pet bar press aimed
    // by the crosshair: the unit under it (0 none) and, with PRESS_POINT, the ground point.
    constexpr uint8_t kOpBarPress = 18;
    constexpr uint8_t kOpAutocast = 19;     // [19, slot u8, on u8]
    enum PressFlags : uint8_t
    {
        PRESS_POINT = 0x01,     // x, y, z is the ground under the crosshair
        PRESS_DOUBLE = 0x02,    // a double tap (Command: follow)
    };

    enum BarFlags : uint8_t
    {
        BAR_SUMMONED = 0x01,
        BAR_CAN_EVOLVE = 0x02,
        BAR_CAPPED = 0x04,      // held at the owner's level with a full bar
        BAR_IN_DUEL = 0x08,
        BAR_FINAL_STAGE = 0x10,
        BAR_RIDEABLE = 0x20,    // its stage is a mount
        BAR_CAN_RIDE = 0x40,    // ... and its owner may ride it now (CanRide)
        BAR_RIDING = 0x80,      // its owner is riding it (it is not out on foot)
    };
    // The riding bits of a bar (MakeBar leaves them to the server, which knows the rider).
    uint8_t RideFlags(CompanionSpecies const& sp, CompanionState const& s, RideContext const& ctx, bool riding);

    // op 1, 27 bytes: [1, item guid u64, species u16, stage u8, level u8, effective level u8,
    //                  xp u32, xp to next u32, flags u8, evolve block u8, next evolve level u8,
    //                  catalysts needed u8, health % u8]. Sent on summon, on every xp or level
    //                  change, on evolving, and on entering a new area (the evolve button may
    //                  light up); item guid 0 with flags 0 means no companion is out.
    struct Bar
    {
        uint64_t item = 0;
        uint16_t species = 0;
        uint8_t stage = 0;
        uint8_t level = 1;
        uint8_t effectiveLevel = 1;
        uint32_t xp = 0;
        uint32_t xpToNext = 0;
        uint8_t flags = 0;
        EvolveBlock block = EvolveBlock::None;
        uint8_t nextEvolveLevel = 0;    // 0 at the final stage
        uint8_t catalystCount = 0;      // catalysts the next evolution consumes
        uint8_t healthPct = 100;
    };
    // Fill a bar from the rules (flags other than SUMMONED / IN_DUEL derive from the state).
    Bar MakeBar(uint64_t item, CompanionSpecies const& sp, CompanionState const& s, EvolveContext const& ctx,
        uint8_t healthPct);

    // op 2, 12 bytes: [2, item guid u64, species u16, stage u8]
    // op 3, 12 bytes: [3, phase u8, end u8, winner i8, opponent guid u64]
    struct DuelUpdate
    {
        DuelPhase phase = DuelPhase::Idle;
        DuelEnd end = DuelEnd::None;
        int8_t winner = -1;     // 0 = you, 1 = the opponent, -1 nobody (from the reader's side)
        uint64_t opponent = 0;
    };

    // A companion item's suffix factor (ITEM_FIELD_PROPERTY_SEED, which companion items, never
    // random-suffix items, leave unused). The field reaches the client in the item's own update
    // fields and in the auction, trade and mail lists, so a buyer sees the companion's level,
    // stage, rarity and traits on its tooltip. The server stamps it on every load and change; it
    // is not saved (the companion table is the record). Bits, low to high:
    //   0-5 level (1-60), 6-7 stage (0-2), 8-15 species (1-255), 16-20, 21-25, 26-30 up to three
    //   trait ids (CompanionTraits.h, 1-30, lowest first; 0 none), 31 bred (it has a lineage).
    uint32_t ItemSeed(CompanionState const& s);
    // A seed back into species, stage, level, traits and the bred bit (generation 1 when set);
    // false for anything not a companion's.
    bool FromItemSeed(uint32_t seed, CompanionState& out);

    std::vector<uint8_t> EncodeBar(Bar const& b);
    bool DecodeBar(std::vector<uint8_t> const& body, Bar& out);
    std::vector<uint8_t> EncodeEvolved(uint64_t item, uint16_t species, uint8_t stage);
    std::vector<uint8_t> EncodeDuel(DuelUpdate const& d);
    bool DecodeDuel(std::vector<uint8_t> const& body, DuelUpdate& out);

    // op 4: [4, item guid u64, species u16, pending stage i8 (-1 none), twin stage u8,
    //        traits u32, autocast mask u16 (bit per slot), picks 3 x u8,
    //        options 3 x 2 x (spell u32, name: length u8 + bytes), mounted (spell u32, name)].
    //        Sent on summon, evolution, a pick and an autocast change; item 0 when none is out.
    struct Moves
    {
        uint64_t item = 0;
        uint16_t species = 0;
        int8_t pending = -1;
        uint8_t twinStage = 0;
        uint32_t traits = 0;
        uint16_t autocast = 0;
        std::array<uint8_t, kMaxStages> picks{};
        std::array<std::array<uint32_t, 2>, kMaxStages> spells{};
        std::array<std::array<std::string, 2>, kMaxStages> names{};
        uint32_t mountedSpell = 0;
        std::string mountedName;
    };
    std::vector<uint8_t> EncodeMoves(Moves const& m);
    bool DecodeMoves(std::vector<uint8_t> const& body, Moves& out);
    // op 5, 6 bytes: [5, slot u8, cooldown ms u32].
    std::vector<uint8_t> EncodeCooldown(uint8_t slot, uint32_t ms);
    // op 6, 13 bytes: [6, seed u32, generation u8, parent a u16, parent b u16, bred u8, hybrid u16].
    std::vector<uint8_t> EncodeLineage(uint32_t seed, CompanionState const& s);
    std::vector<uint8_t> EncodeMountCast(MountCastPhase phase, uint32_t ms);

    // A client request, decoded: op plus its arguments.
    struct Request
    {
        uint8_t op = 0;
        uint64_t guid = 0;          // an item or player guid, or a press's target
        bool accept = false;        // duel answer, autocast on
        uint8_t slot = 0;           // press, autocast; pick: the stage
        uint8_t option = 0;         // pick
        uint8_t flags = 0;          // press (PressFlags)
        Vec3 point;                 // press with PRESS_POINT
    };
    std::vector<uint8_t> EncodeRequest(Request const& r);
    // False for an unknown op or a body of the wrong length.
    bool DecodeRequest(std::vector<uint8_t> const& body, Request& out);
}

#endif
