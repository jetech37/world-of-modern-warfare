/*
 * World of Modern Warfare: a numeric model of guns against the classes' classic combat (spells and
 * melee), for design/BALANCE_GUNS_VS_CLASSES.md. It reads the same rules the game does (the
 * weapon profiles and falloff, the CoD-scale pools of Scale.h, melee swing profiles, the hands'
 * charge times, the limited-ammunition rules of Ammo.h) and the class catalog's %EH numbers, and
 * answers: how fast each kills at levels 10, 30 and 60, how much it sustains, and how long a
 * load of ammunition lasts. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_BALANCE_H
#define FUSION_BALANCE_H

#include "fusion/Common.h"
#include "fusion/Weapon.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Fusion::Balance
{
    // What the model assumes of the player and the fight; every number is a knob.
    struct Assumptions
    {
        float pveAccuracy = 0.7f;       // share of rounds that hit a creature (big, slow, at 15-30 yd)
        float pvpAccuracy = 0.45f;      // ... an equal player who moves and strafes
        float headShare = 0.1f;         // of the hits, head shots (MW2's 1.4)
        float pvpArmorCut = 0.15f;      // a mid-geared target's cut of bullets (Scale::kMaxArmorCut 0.3)
        float spellAccuracy = 0.85f;    // free-aimed bolts (a near miss still splashes)
        float meleeAccuracy = 0.9f;
        float gear = 1.f;               // G, the gear multiplier on melee and class damage (1 fresh)
        float coefPve = 1.f;            // Fusion.Damage.PvE / PvP
        float coefPvp = 0.4f;
        float seconds = 60.f;           // the sustained window
        // This pass's ranged-spell power (Fusion.Classes.RangedSpellPower, Classes.h), in the
        // deepened columns only.
        float rangedSpellPower = 2.f;
        // The pool to measure against instead of the level's (0: the level's), e.g. an elite's
        // share in a group.
        float poolOverride = 0.f;
        // The class identity pass (fusion/ClassTuning.h): each option's and basic spell's damage
        // scale, Shatter, Atonement, Charging through fire, Ambush's sundering. Off: as before it.
        bool tuned = false;
        // The duel's opening distance for a kit that closes (0: its own, 25 yd from the gun).
        float duelGap = 0.f;
    };

    // The pool a hit is measured against: an equal-level player (PvP) or a normal creature of the
    // level in the open world (PvE), on the CoD scale with the danger table on it (Danger.h).
    float TargetPool(uint32_t level, bool pvp);
    // An equal-level player's pool: what %EH is a share of.
    float EqualHealth(uint32_t level);

    // ── Guns ──
    struct GunCase
    {
        std::string label;
        WeaponProfile weapon;
        float range = 20.f;             // yards
        bool stoppingPower = false;     // the perk: 1.4x
        float magScale = 1.f;           // extended magazines: 1.5
    };
    struct GunResult
    {
        float hitPct = 0.f;             // one round (all pellets that hit), % of the pool
        float roundsPerKill = 0.f;      // fired, misses included
        float ttk = 0.f;                // seconds, from the first round
        float burstPct = 0.f;           // % of the pool a second while the trigger is down
        float sustainedPct = 0.f;       // ... with reloads
        float killsPerMag = 0.f;
        float killsPerLoad = 0.f;       // magazine and reserve
        float triggerSecondsPerLoad = 0.f;  // firing and reloading through it all
        uint32_t load = 0;              // rounds carried
    };
    GunResult Gun(GunCase const& g, uint32_t level, bool pvp, Assumptions const& a = {});
    // The same against any pool (an elite, a dungeon's trash).
    GunResult GunAgainst(GunCase const& g, uint32_t level, float pool, bool pvp, Assumptions const& a = {});

    // ── Classic combat ──
    // One ability (or the basic spell) as the catalog gives it: %EH, its cooldown and how long it
    // keeps the hands (the hands' charge, a channel, a blow).
    struct Move
    {
        std::string name;
        uint32_t level = 1;             // learned at (a tier option: its tier)
        float pct = 0.f;                // %EH on landing
        float dotPerSec = 0.f;          // %EH a second after it, for dotSec (refreshes, one target)
        float dotSec = 0.f;
        float cdSec = 0.f;
        float busySec = 0.3f;           // the charge (0.3 instant, 0.45 quick, 0.6 medium, 0.8 heavy) or a channel
        bool melee = false;             // a blow (melee accuracy, Bane) or a spell (spell accuracy)
        uint32_t rankLevel = 0;         // a basic spell first learned at this level hits less below it
        bool unscaled = false;          // not a ranged WoW spell (a custom strike, a self area): no ranged-spell power
        uint16_t option = 0;            // its kit option (ClassTuning::DamageScale), 0 none
        bool basic = false;             // the class's basic spell (ClassTuning::BasicSpellScale)
        bool chills = false;            // slows its target (Frostbolt): a Wizard's Shatter after it
        bool ranged = false;            // usable from 20 yd or more: it lands while a melee kit still closes
    };

    // How an archetype fights a gunner face to face (the duel model): what it has to close, how it
    // stops the gun, what it soaks and heals. The audit of each class's kit as it plays.
    struct Control
    {
        uint32_t level = 1;             // learned at
        float seconds = 0.f;            // the gunner cannot fire this long (a stun, fear, disarm, sheep; players' durations)
        float cdSec = 0.f;
        bool breaksOnDamage = false;    // a fear or a sheep: counts for half while the class keeps hitting
        bool ranged = true;             // usable from the class's range (false: in melee reach only)
        bool tunedOnly = false;         // added by the identity pass (Ambush's sundering)
        float castSec = 0.f;            // its cast (Polymorph, Fear: 1.5 s): the gun keeps firing meanwhile
    };
    // Diminishing returns: each control within kControlDrSec of the last lasts half as long as
    // the one before it (vanilla's PvP rule, folded into one family).
    constexpr float kControlDrSec = 18.f;
    // A gap-closer's flight (Charge, Intercept, a blink is quicker but lands a blow after it).
    constexpr float kCloserTravelSec = 0.4f;
    struct DuelKit
    {
        float gap = 0.f;                // yards to close before it deals damage (a caster: 0)
        float runSpeed = 7.f;           // yards a second while closing
        float closerYards = 0.f;        // a gap-closer's reach (Charge, Intercept, Lunge, Blade Blink, Fel Leap)
        uint32_t closerLevel = 1;
        float closerStun = 0.f;         // the gunner cannot fire this long after it lands
        bool closerGuard = false;       // Charging through fire (ClassTuning kGapGuardBulletCut)
        bool stealth = false;           // opens from stealth at arm's length, unseen
        std::vector<Control> control;
        float bulletCut = 1.f;          // bullets taken, its defensives' typical uptime folded in
        float wardPct = 0.f;            // an absorb, % of own HP (bullets soak 1.5x), recast on its cooldown
        float wardCdSec = 0.f;
        uint32_t wardLevel = 1;
        float healPerSec = 0.f;         // % of own HP a second its heals give back while it fights
        float drainShare = 0.f;         // share of its damage it heals (Drain Life, Bloodthirst, Worgen)
        float closingBulletCut = 1.f;   // bullets taken while it closes, the pass's (Ghost Wolf)
        float petPctPerSec = 0.f;       // its battle companion's damage, % of the pool a second (the hunter's bond scales it)
    };
    struct Rotation
    {
        std::string cls;
        std::string archetype;
        std::vector<Move> moves;
        // Plain swings between the abilities (Melee::ProfileFor): a melee archetype's weapon, or
        // none (a caster with its hands full of spells).
        bool swings = false;
        uint8_t weapon = 0;             // ItemSubclassWeapon
        float swingBonusPct = 0.f;      // %EH added to every swing (Flame Blade, Stone Fists)
        uint32_t swingBonusLevel = 0;
        // The archetype's signature, as a damage multiplier from a level (Spellweave, Bane,
        // Momentum, the Opening for its first seconds).
        float signature = 1.f;
        uint32_t signatureLevel = 0;
        float signatureSeconds = 0.f;   // 0: always; else only for this long from the start (the Opening)
        std::string answer;             // its answer to guns, in a few words
        uint8_t clsId = 0;              // Classes::kWarrior..., filled in from `cls`
        int8_t archetypeIndex = -1;     // 0..2, filled in from `archetype`
        bool shatter = false;           // the Wizard: spells into a chilled or held target (ClassTuning)
        DuelKit duel;
    };
    // Every archetype's core single-target rotation (design/CLASS_CATALOG.md).
    std::vector<Rotation> const& Rotations();

    struct ClassResult
    {
        float sustainedPct = 0.f;       // % of the pool a second over Assumptions::seconds
        float ttk = 0.f;                // seconds to kill one target from full
        float burst5 = 0.f;             // % of the pool dealt in the first 5 s
    };
    // `deep`: with this pass's deepening: melee combos, full charges where the cooldown leaves the
    // time and nothing better is due (casters; a swinging class taps), the ranged-spell power.
    ClassResult Classic(Rotation const& r, uint32_t level, bool pvp, bool deep, Assumptions const& a = {},
        std::vector<float>* curve = nullptr);
    // `curve`: when given, the share of the pool dealt so far (%) at every kStep from 0.
    constexpr float kStep = 0.05f;

    // ── A duel: the archetype against an equal-level gunner (Assumptions::tuned: after the
    // identity pass) ──
    // The gunner holds `gun` and fires from the first moment it can (a 0.3 s reaction; 0.6 s
    // against a class opening from stealth) whenever it is not stunned, feared, sheeped or
    // disarmed; the class closes its kit's gap (a melee kit; a caster fights from where it is),
    // spends its control on cooldown, soaks with its ward and heals as its kit says. Both have an
    // equal player's pool; downed states and movement other than the closing are left out.
    struct DuelResult
    {
        bool classWins = false;
        float seconds = 0.f;            // when one of them went down (60: neither)
        float classLeft = 0.f;          // % of its pool the class has at the end
        float gunnerLeft = 0.f;
        float classTtk = 0.f;           // seconds the class's damage needs on a gunner who never shoots back
    };
    DuelResult Duel(Rotation const& r, GunCase const& gun, uint32_t level, Assumptions const& a = {});
    // The duel table (before and after the pass) as Markdown.
    std::string DuelReport(WeaponTable const& table, uint32_t level = 60);

    // ── An engagement: targets one after another ──
    // A gun until the load runs dry (the limited model's field floor refills a magazine between
    // fights), then classic combat; against classic combat only and an unlimited gun.
    struct Engagement
    {
        uint32_t targets = 0;
        float gunOnly = 0.f;            // seconds, as if ammunition were endless
        float mixed = 0.f;              // seconds: the gun while it has rounds, then the class
        float classicOnly = 0.f;
        uint32_t gunKills = 0;          // of them, by the gun
    };
    Engagement Fight(GunResult const& gun, ClassResult const& classic, uint32_t targets);

    // The representative guns of each class at a level (the best-known unlocked by then, a starter
    // at 10), with typical attachments; `table` is the weapons.json roster.
    std::vector<GunCase> RepresentativeGuns(WeaponTable const& table, uint32_t level);

    // The whole report as Markdown tables (what BALANCE_GUNS_VS_CLASSES.md quotes).
    std::string Report(WeaponTable const& table, Assumptions const& a = {});
}

#endif
