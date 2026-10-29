/*
 * World of Modern Warfare: the rules behind the class options' custom effects (design/CLASS_CATALOG.md,
 * sections 1-9), apart from the game so they can be tested: which states make their holder immune
 * to what, the short control durations against players, damage over time, charges, Camouflage's
 * stand-still rule, the rescues from going down, totems, constructs and traps, Spellweave.
 * GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_CLASS_EFFECTS_H
#define FUSION_CLASS_EFFECTS_H

#include "fusion/Common.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Fusion::Classes
{
    // ── Immunities ──
    enum ImmunityFlags : uint8_t
    {
        IMMUNE_STUN = 0x01,
        IMMUNE_FEAR = 0x02,
        IMMUNE_KNOCKBACK = 0x04,
        IMMUNE_SLOW = 0x08,
        IMMUNE_ROOT = 0x10,
    };
    // What an option's running state makes its holder immune to (0: nothing). Blood Frenzy (141)
    // is the Berserker's Frenzy with Blood Frenzy in the kit; Worgen Form (1161) its momentum.
    uint8_t ImmunitiesOf(uint16_t option);

    // ── Control effects held shorter than their vanilla spell ──
    struct DurationCap
    {
        uint16_t option = 0;
        uint32_t npcMs = 0;
        uint32_t playerMs = 0;
    };
    // The cap of an option's control effect on its target (null: none, the spell's own).
    DurationCap const* DurationCapOf(uint16_t option);
    uint32_t CappedMs(DurationCap const& c, bool player);

    // ── Geometry ──
    // Whether `p` is within `radius` of the segment a-b on the ground (z ignored): a charge's path.
    bool NearSegment(Vec3 a, Vec3 b, Vec3 p, float radius);

    // ── Charges (Shadowstep's second with Slippery) ──
    struct ChargePool
    {
        uint8_t value = 0;
        uint8_t max = 0;
        uint32_t regenMs = 0;       // one back after this long
        uint32_t nextMs = 0;        // the next back then (0: full)
        // Spend one (true), starting the regeneration clock when it was full.
        bool Use(uint32_t nowMs);
        // Charges back with time.
        void Update(uint32_t nowMs);
    };

    // ── Camouflage: stand still to fade, firing or sprinting breaks it ──
    constexpr uint32_t kCamouflageStillMs = 2000;
    constexpr uint32_t kCamouflageCooldownMs = 10000;
    struct Camouflage
    {
        bool on = false;            // toggled on: fading when still
        bool hidden = false;        // faded into stealth
        uint32_t stillSinceMs = 0;
        uint32_t readyMs = 0;       // may be toggled on again then (10 s after it broke)
        enum class Step : uint8_t { None, Fade, Break };
        // Toggle on (true) or off; refused while the cooldown after a break runs.
        bool Toggle(uint32_t nowMs);
        // One update: whether the hunter moved (running; walking keeps it), fired or sprints.
        Step Update(uint32_t nowMs, bool running, bool fired, bool sprinting);
    };

    // ── Rescues from going down ──
    enum class Rescue : uint8_t
    {
        None,
        Spirit,             // Spirit of Redemption: 12 s a spirit of light, then down
        Reincarnation,      // the shaman's own: back up at 40%
        Soulstone,          // a warlock's stone: back up at 40%
    };
    constexpr uint32_t kSpiritMs = 12000;
    constexpr uint32_t kRescueDelayMs = 2000;           // down this long before standing back up
    constexpr float kReincarnationShare = 0.4f;
    constexpr float kSoulstoneShare = 0.4f;
    constexpr uint32_t kReincarnationCooldownMs = 20 * 60 * 1000;
    constexpr uint32_t kSoulstoneMs = 15 * 60 * 1000;
    // What saves a player about to go down: the spirit keeps them up (once its cooldown allows);
    // otherwise, once down, Reincarnation before a soulstone.
    Rescue RescueOnDown(bool spiritReady, bool reincarnationReady, bool soulstone);

    // ── Totems (Totemist: Totemic Web, Ancestral Circle) ──
    constexpr float kTotemLinkYards = 20.f;
    // A totem's effect multiplier: +50% linked to another within 20 yd, doubled in the circle.
    float TotemMultiplier(bool linked, bool circle);
    uint8_t TotemLimit(bool circle);

    // ── Constructs (Artificer) ──
    uint8_t ConstructLimit(bool grandArray);

    // ── Traps (Trapper: Prepared Ground, Killing Field) ──
    uint8_t TrapLimit(bool killingField);
    float TrapChainYards(bool killingField);

    // ── Spellweave (Wizard) ──
    // One Wizard spell of `school`: a switch from the last spell's school pays off (its damage
    // times kWeaveBonus); a repeat does not. No counter to keep up: the switch is the weave.
    constexpr float kWeaveBonus = 1.3f;
    float WeaveBonus(uint32_t& lastSchool, uint32_t school);

    // ── What the removed resources paid for (GAME_DESIGN.md section 2, "Class resources") ──
    // Berserker's Frenzy (was 5 Fury): a kill, a Whirlwind among kFrenzyCrowd enemies, or a
    // Bloodthirst hit by chance; Rampage holds it.
    constexpr float kFrenzyChance = 0.2f;
    constexpr uint32_t kFrenzyMs = 6000;
    constexpr size_t kFrenzyCrowd = 3;
    // Lightwarden's Zenith (was 100 Solar Power): each Lightwarden heal's chance, in percent (what
    // it gave of the 100 before): Flash of Light 10, Holy Light 20, Holy Shock 15; 0 for others.
    uint8_t ZenithChance(uint16_t option);
    // Starcaller's Eclipse (was 3 Lunar and 3 Solar): Moonfire, Faerie Fire and Lunar Strike's
    // chance, and a melee blow's (`option` 0, `blow`).
    float EclipseChance(uint16_t option, bool blow);
    // Arcane Edge (was a full Bladecharge): a Flame Blade blow's chance to make the next Wizard
    // damage spell instant and double, for kArcaneEdgeMs.
    constexpr float kArcaneEdgeChance = 0.2f;
    constexpr uint32_t kArcaneEdgeMs = 10000;

    // ── Damage over time (Witchfire's burn, Envenom's poison, Bear Trap's bleed) ──
    struct Dot
    {
        uint64_t target = 0;
        uint16_t code = 0;          // what put it there (an option id)
        uint32_t untilMs = 0;
        uint32_t nextMs = 0;        // the next tick
        float pctPerTick = 0.f;     // %EH a tick, a stack
        uint8_t stacks = 1;
        uint8_t maxStacks = 1;
    };
    constexpr uint32_t kDotTickMs = 1000;
    // Put one on (or refresh it: one more stack up to its cap, the duration from now).
    void AddDot(std::vector<Dot>& dots, uint64_t target, uint16_t code, uint32_t nowMs, uint32_t durationMs,
        float pctPerTick, uint8_t maxStacks);
    // The ticks due now (target, %EH), the run out ones dropped.
    std::vector<std::pair<uint64_t, float>> DueTicks(std::vector<Dot>& dots, uint32_t nowMs);
    uint8_t StacksOn(std::vector<Dot> const& dots, uint64_t target, uint16_t code);

    // ── The rogue (CLASS_CATALOG.md section 4) ──
    // Assassin, the Opening: leaving stealth starts kOpeningMs in which the Assassin's blades and
    // abilities hit twice as hard; the openers (Ambush, Cheap Shot, Garrote) need stealth or the
    // opening, so a fight that drags on leaves the Assassin without them.
    constexpr uint32_t kOpeningMs = 4000;
    constexpr float kOpeningMultiplier = 2.f;
    bool IsOpener(uint16_t option);
    enum class OpenerVerdict : uint8_t
    {
        Ok,
        NeedsStealth,       // out of stealth and the opening
        NeedsBehind,        // Ambush faces the target's front (Distract lifts this)
    };
    // `flanking`: a Distract of the rogue's is on (its 8 s).
    OpenerVerdict CanOpen(uint16_t option, bool stealthed, bool opening, bool behind, bool flanking);
    // Cheap Shot's stun and Garrote's silence: a second longer from behind (or flanking).
    uint32_t CheapShotMs(bool player, bool behind);
    uint32_t GarroteSilenceMs(bool behind);
    constexpr uint32_t kDistractFlankMs = 8000;
    // Preparation readies these options (the core's Stealth, Sprint and Vanish and the Assassin's
    // own, Preparation itself left out).
    std::vector<uint16_t> PreparationReadies();

    // Trickster, the Poison Satchel: one coat at a time, pressed again it switches to the next.
    enum class Poison : uint8_t { None, Crippling, MindNumbing, Wound };
    Poison NextPoison(Poison p);
    char const* PoisonName(Poison p);
    // The vanilla debuff a blow under the coat puts on (the rank the level allows): Crippling
    // 3409/11201, Mind-numbing 5760/8692/11398, Wound 13218/13222/13223/13224.
    uint32_t PoisonDebuff(Poison p, uint8_t level);
    // Slip: every Trickster ability makes its rogue untargetable this long.
    constexpr uint32_t kSlipMs = 500;

    // Trickster, Light Fingers: lifting a purse (pickpocketing). The game asks with what it sees.
    struct PocketTarget
    {
        bool creature = false;      // players have no pockets to lift (no stealing from players)
        bool alive = false;
        bool hostile = false;       // not friendly to the rogue
        bool hasPocketLoot = false; // a pickpocket loot table
        bool humanoid = false;      // humanoids carry coin at least
        bool alreadyPicked = false; // lifted once (vmangos lootForPickPocketed)
        float distance = 0.f;
        bool inCombat = false;
        bool behind = false;
        bool stealthed = false;
        bool lightFingers = false;  // lifted with the option, not the Utility's Pick Pocket
    };
    enum class PocketVerdict : uint8_t
    {
        Ok,
        NoTarget,       // nothing, a corpse, a friend, or a player
        NoPockets,      // not a humanoid, no pickpocket loot
        Picked,         // lifted already
        TooFar,
        NeedsStealth,   // Pick Pocket without Light Fingers: stealth, out of combat, behind
    };
    constexpr float kPocketReach = 5.f;
    PocketVerdict CanLift(PocketTarget const& t);

    // Lockpicking. Pick Lock (Utility) opens the vanilla locks (lockboxes, chests, doors) by the
    // Lockpicking skill; Light Fingers adds kLightFingersLockBonus and makes it instant. A
    // homestead's doors and portals are player property under their owner's lock (Locks.h): no
    // pick opens them and none shortens a code lock's wait, so a rogue is no brute-force tool
    // against a 4-digit code.
    enum class LockKind : uint8_t { VanillaChest, VanillaLockbox, VanillaDoor, HomesteadDoor, HomesteadPortal };
    constexpr uint16_t kLightFingersLockBonus = 50;
    bool PickOpens(LockKind kind, uint16_t lockSkill, uint16_t lockpicking, bool lightFingers);
    // Pick Lock's cast time: vanilla's (5 s), none with Light Fingers.
    uint32_t PickLockCastMs(uint32_t vanillaMs, bool lightFingers);

    // Duelist: Riposte's guard, Lunge's dash, Blade Flurry, Relentless.
    constexpr uint32_t kRiposteMs = 1500;
    constexpr float kRipostePct = 12.f;         // %EH struck back at a parried blow
    constexpr float kLungeRange = 30.f;
    constexpr float kLungeMinRange = 3.f;
    constexpr float kLungePct = 12.f;           // %EH, the blow at the end of the dash
    constexpr float kSinisterSecondPct = 6.f;   // %EH, Sinister Strike's off-hand blow
    constexpr float kBladeFlurryShare = 0.5f;
    constexpr float kRelentlessBulletCut = 0.85f;
    constexpr uint16_t kRelentlessFrom = 5;     // Momentum from which Relentless cuts bullets
    // Whether a hit on a guarded Duelist is parried: a melee blow (not a bullet) from in front.
    bool Parries(bool guarding, bool melee, bool fromFront);

    // ── Small numbers ──
    constexpr float kPrayerBonus = 1.2f;               // Prayer of Healing (was +10% a Devotion)
    constexpr float kStoneskinBulletCut = 0.85f;        // Stoneskin (was 5% an Earth charge held)
    constexpr float kDemonicPactCut = 0.85f;            // Demonic Pact (was paid from Fel Fury)
    constexpr float kGroundSmashPct = 15.f;             // %EH (was 5% an Earth charge)
    constexpr float kBladeBlinkPct = 20.f;              // %EH (was 10% and 5% a Bladecharge)
    constexpr uint32_t kFrostNovaExtraMs = 2000;        // Spellsword's Frost Nova (was 1 s a Bladecharge)
    constexpr float kBarrierHealthShare = 0.5f;         // Barrier (was 0.4, x2 at full Foresight)
    constexpr uint32_t kVampiricMs = 20000;             // Vampiric Embrace (was 15 s and 5 s an orb)
    constexpr float kSacrificeShare = 0.5f;             // Blessing of Sacrifice moves half of it
    constexpr uint32_t kMirrorSwaps = 3;                // Hall of Mirrors
    constexpr uint8_t kOverwatchPrey = 3;               // Mark Prey's prey under Overwatch
    constexpr uint32_t kRallyingCryBleedMs = 10000;     // extra bleed-out time

    // ── Wards against guns (design/BALANCE_GUNS_VS_CLASSES.md) ──
    // A class absorb (Mana Shield, Holy Shield, Rallying Cry's, a bear's momentum...) is made to
    // stop rounds: each point of it takes kWardBulletEfficiency points of a bullet's damage, so a
    // ward of 25% HP soaks a magazine's worth more than its face. Other damage one for one.
    constexpr float kWardBulletEfficiency = 1.5f;
    // A ward with `left` points meets `incoming` damage: what it takes off (spending `left`).
    float WardAbsorb(float& left, float incoming, bool bullet, float efficiency = kWardBulletEfficiency);

    // ── Ranged spells against guns (design/BALANCE_GUNS_VS_CLASSES.md) ──
    // The catalog's %EH numbers were set against a magazine of 100-150 %EH; a rifle's is nearer 600
    // (PvP) and its rounds kill a normal creature in a third of a second. A spell cast from range
    // (reach past kMeleeSpellReach: bolts, shocks, damage over time, ground areas) deals
    // `power` times its damage (Fusion.Classes.RangedSpellPower). Melee-range strikes and areas
    // round the caster keep theirs: the blade already trades with a gun up close.
    constexpr float kMeleeSpellReach = 8.f;
    constexpr float kRangedSpellPower = 2.f;
    float RangedSpellScale(float spellRangeYards, float power = kRangedSpellPower);
}

#endif
