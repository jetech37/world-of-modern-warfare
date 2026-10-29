/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_STATS_H
#define FUSION_STATS_H

#include "fusion/Common.h"
#include "fusion/Movement.h"
#include "fusion/Weapon.h"

#include <array>
#include <cstdint>
#include <vector>

// What a character's stats do with guns (design doc, section 7). Every vanilla item keeps its
// stats; what they do changes, and gear never raises raw gun damage: it changes handling,
// survival, recovery and equipment. Every conversion lives here, in one table (kConversions)
// and one list of thresholds (kThresholds), so archetypes can lean on them later by data alone.
//
// A stat is judged against a budget for the character's level, so the same share of a level's
// gear means the same handling at 10 and at 60. Each conversion gives cap x r / (r + 0.5) for a
// rating r (r = 1 is a well-geared character: two thirds of the cap), and every output is held
// to its own floor or ceiling after all its sources are summed.
namespace Fusion { namespace Stats
{
    // ── Inputs ──
    struct Input
    {
        uint32_t level = 1;
        // Totals as the character sheet shows them, and what gear, buffs and talents add over
        // the character's own (total - the race and class's base for the level).
        float strength = 0.f, agility = 0.f, stamina = 0.f, intellect = 0.f, spirit = 0.f;
        float strengthBonus = 0.f, agilityBonus = 0.f, staminaBonus = 0.f, intellectBonus = 0.f, spiritBonus = 0.f;
        float armor = 0.f;
        float hitPct = 0.f;             // +hit from gear and talents, percent
        float critPct = 0.f;            // ranged crit chance, percent (agility's share included)
        float defenseBonus = 0.f;       // defense skill over the level's cap (5 x level)
        float rangedPowerBonus = 0.f;   // ranged attack power from gear and buffs (not agility's)
        PerkFlags perks;
    };

    // ── Outputs ──
    // Handling multipliers (1 = MW2's own; lower is quicker or steadier). The client plays them;
    // the server holds the sprint-out to the same number.
    enum class Out : uint8_t
    {
        AdsTime,            // time to aim down sights
        SwapTime,           // weapon swap (drop and raise)
        SprintOutTime,      // from a sprint to the gun ready
        Recoil,             // camera kick per shot
        HipSpread,          // the hip-fire cone
        Bloom,              // how much each shot opens the cone
        Sway,               // a scope's sway
        ReloadTime,
        Flinch,             // the view kick when a round hits you
        // Not multipliers on time: shares and strengths, see Derived.
        HeavyGunRelief,     // share of an LMG's or sniper's move penalty taken away
        EquipmentStrength,  // flash and stun duration, smoke size, Claymore reach (1 + this)
        RegenDelay,         // share taken off the regeneration delay
        RegenRate,          // share added to the regeneration rate
        WeakSpotTaken,      // share of crits turned into weak-spot hits on you that are not
        Count,
    };

    enum class Source : uint8_t
    {
        Strength,
        Agility,
        Stamina,
        Intellect,
        Spirit,
        RangedPower,
        Hit,
        Defense,
        Armor,
        Count,
    };

    struct Conversion
    {
        Source source;
        Out out;
        float cap;          // the most this source alone gives
    };

    // The table (Stats.cpp has the numbers, the design doc section 7 the why).
    extern std::vector<Conversion> const kConversions;

    // Each output's limit once its sources are summed: the least a multiplier goes to, or the
    // most a share or strength goes to.
    float OutputLimit(Out o);

    // ── Thresholds: small bonuses at round numbers of a stat's total ──
    enum class Bonus : uint8_t
    {
        InstantAdsAfterSprint,  // the first ADS after a sprint is instant
        RegenEarlier,           // regeneration starts `amount` seconds sooner
        RegenFaster,            // + `amount` to the regeneration rate share
        QuickerSwap,            // - `amount` off the swap multiplier
        SteadyFirstShot,        // the first round of a burst kicks nothing
        StrongerEquipment,      // + `amount` equipment strength
        Unflinching,            // - `amount` off the flinch multiplier
        Hardened,               // + `amount` to the weak-spot share taken away
    };

    enum class StatKind : uint8_t { Strength, Agility, Stamina, Intellect, Spirit, Defense };

    struct Threshold
    {
        StatKind stat;
        float at;               // the total (Defense: the bonus over the level's cap)
        Bonus bonus;
        float amount;
        char const* text;
    };

    extern std::vector<Threshold> const kThresholds;

    // The level's budget for one stat from gear (a well-geared character's), at least 10:
    // 17 at 10, 48 at 20, 152 at 40, 312 at 60.
    float StatBudget(uint32_t level);
    // Ranged attack power from gear at the level: half the stat budget.
    float RangedPowerBudget(uint32_t level);
    // +Hit: 6% is a full rating at any level.
    constexpr float kHitBudgetPct = 6.f;
    // Defense over the cap: 2 x level.
    float DefenseBudget(uint32_t level);
    // A source's rating, 0 to kMaxRating (0 when it has nothing).
    constexpr float kMaxRating = 4.f;
    float Rating(Input const& in, Source s);
    // A conversion's share at a rating: cap x r / (r + 0.5).
    float Share(float cap, float rating);

    // ── Fixed numbers ──
    constexpr float kRegenDelay = 5.f;          // seconds after the last hit taken
    constexpr float kRegenMinDelay = 2.f;
    constexpr float kRegenRate = 0.125f;        // of the pool a second (full in 8 s)
    constexpr float kWeakSpotCap = 0.25f;       // the most of body shots that count as weak spots
    constexpr float kFmjArmorPierce = 0.5f;     // FMJ cuts through half the armor's cut
    constexpr float kFlinchDegrees = 1.2f;      // the client's kick for a hit taken
    // Perks that change handling (MW2's Pros): Sleight of Hand Pro aims faster, Lightweight Pro
    // comes out of a sprint faster.
    constexpr float kSleightOfHandProAds = 0.8f;
    constexpr float kLightweightProSprintOut = 0.7f;

    struct Derived
    {
        std::array<float, size_t(Out::Count)> value{};   // multipliers 1, shares 0 by default
        uint16_t thresholds = 0;                        // bit i: kThresholds[i] is met
        bool instantAdsAfterSprint = false;
        bool steadyFirstShot = false;
        float regenDelay = kRegenDelay;                 // seconds
        float regenRate = kRegenRate;                   // of the pool a second
        float weakSpotChance = 0.f;                     // the shooter's: body shot -> weak spot
        float bulletCut = 0.f;                          // the armor's cut of bullets (before FMJ)

        float operator[](Out o) const { return value[size_t(o)]; }
        Derived();
    };

    // The whole conversion for one character. Armor's cut needs the level (Scale.h).
    Derived Compute(Input const& in);

    // ── What the game applies ──
    // A gun's damage multiplier: the level's (Scale::LevelMultiplier), never gear's.
    float GunDamageMultiplier(uint32_t level);

    // The share of a bullet's damage the target's armor takes: armor's cut (at most Scale's 30%),
    // half of it gone to FMJ, and a special round's armor ignore on top.
    float BulletCut(float armorCut, bool fmj, float armorIgnore);

    // The chance a shooter's body shot counts as a weak-spot hit on a target.
    float WeakSpotChance(float shooterChance, float targetWeakSpotTaken);

    // Stamina from gear counts one for one up to 1.5 budgets, half beyond.
    float EffectiveStamina(uint32_t level, float staminaBonus);

    // An LMG's or sniper's move speed scale with Strength's relief (other guns unchanged).
    float HeavyGunMoveScale(WeaponClass c, float moveSpeedScale, float relief);

    // Whether the gun may fire after a sprint: Movement's rule at the character's own sprint-out
    // (`slack` the server's allowance for the client's clock).
    bool SprintOutReady(float secondsSinceSprint, bool sprinting, float sprintOutMultiplier, float slack = 0.f);

    // MW2's regeneration: nothing until the delay has passed since the last hit taken, then the
    // rate. Returns health to add over `dt` seconds on a pool of `maxHealth`.
    float RegenAmount(Derived const& d, float maxHealth, float secondsSinceHurt, float dt);

    // Item procs from gun hits: a "chance on hit" (percent) or a procs-per-minute rate, per pull
    // of a gun firing every `fireTime` seconds. A PPM proc procs ppm a minute at the gun's full
    // rate of fire, a flat chance as often as on a 2 s melee swing, both at kGunProcRate of that
    // and never more than the flat chance per pull.
    constexpr float kProcReferenceSwing = 2.f;
    constexpr float kGunProcRate = 0.5f;
    constexpr uint32_t kGunProcCooldownMs = 1000;    // one proc spell at most once a second
    float GunProcChance(float chancePct, float ppm, float fireTime);

    // ── Fusion channel 6: the client's handling ──
    constexpr uint8_t kChannelStats = 6;
    constexpr uint8_t kOpHandling = 1;      // server to client
    // [1, version 1, then u16 thousandths: every Out in order, regen delay (ms), regen rate,
    // weak-spot chance, bullet cut, then u16 thresholds, u8 flags (1 instant ADS after a sprint,
    // 2 steady first shot)]
    constexpr uint8_t kHandlingVersion = 1;
    constexpr uint8_t kFlagInstantAds = 0x01;
    constexpr uint8_t kFlagSteadyFirstShot = 0x02;
    std::vector<uint8_t> EncodeHandling(Derived const& d);
    bool DecodeHandling(std::vector<uint8_t> const& body, Derived& out);

    // The numbers sent, rounded as the wire rounds them (to tell whether they changed).
    bool SameOnWire(Derived const& a, Derived const& b);
}}

#endif
