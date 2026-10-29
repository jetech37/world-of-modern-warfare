/*
 * World of Modern Warfare: the class framework in the game (design/CLASS_CATALOG.md; the rules are
 * src/fusion's Fusion::Classes). The old class spellbook goes; a character's class spells are its
 * class's 3 core abilities, its Utility spells and the talent picks of the kit in hand
 * (ClassLoadout::talentPicks, Kits.h), learned at the rank its level allows. Options are bought
 * once at the class trainer (its own list replaces the vanilla ranks). The custom parts of the
 * options (routes B and C) and the shared systems (archetype resources, bullet-blocking shapes,
 * smoke, spotting, minions, forms) run here off a few hooks in the spell and damage code.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef MANGOS_FUSION_CLASSES_H
#define MANGOS_FUSION_CLASSES_H

#include "Common.h"
#include "SharedDefines.h"
#include "Spells/SpellDefines.h"
#include "fusion/Classes.h"
#include "fusion/ClassEffects.h"
#include "fusion/Spellcasting.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class GameObject;
class Item;
class Player;
class SpellCaster;
class Spell;
class Unit;
class SpellEntry;

// A timed effect on a player: an option's (its id) or a shared kind (FusionClasses.cpp kFx*).
struct FusionClassEffect
{
    uint16 code = 0;
    uint32 untilMs = 0;
    uint64 source = 0;              // who put it there, or what it is tied to (a minion, a mark)
    float value = 0.f;              // what it is worth: an absorb left, a multiplier, a rate
};

// A zone an option leaves in the world for a while (a banner, a glyph, a quake, a pad), ticked
// by its owner's update.
struct FusionClassZone
{
    uint16 code = 0;
    uint32 mapId = 0;
    Fusion::Vec3 at;
    float radius = 0.f;
    uint32 startMs = 0;             // it acts from then (a meteor's fall)
    uint32 untilMs = 0;
    uint32 nextTickMs = 0;
    uint32 fullMs = 0;              // its whole length (Chain Heal renews a totem's)
    uint8 variant = 0;              // a banner's kind, a pad's pair index
    uint8 charges = 0;              // uses left (a Lightwell's)
    std::vector<std::pair<uint64, uint32>> waits;   // per-unit cooldowns inside it (pads)
};

// A minion an option raised (skeletons, ghouls, the abomination, a decoy, a familiar).
struct FusionClassMinion
{
    uint64 guid = 0;
    uint16 code = 0;
    Fusion::Vec3 last;              // where it was last seen, for Death's Harvest's blast
    uint32 nextActMs = 0;           // its own move again then (a ghoul's leap, the Meat Hook)
};

// An aura an option put on a unit, fitted once it has landed (a cast's effects come after
// OnCast): held to `ms` (a control effect shorter on players), or `ms` longer (Frost Nova's
// Bladecharges).
struct FusionAuraFix
{
    uint64 target = 0;              // 0: every enemy near the caster holding it
    uint32 spell = 0;               // the first rank: any rank of the chain is fitted
    uint32 ms = 0;
    uint32 npcMs = 0;               // the cap on a creature (0: `ms` for both)
    bool extend = false;
    uint32 untilMs = 0;             // given up then (the cast missed)
};

// An affliction a caster put on a unit, remembered for what its death does (Soul Siphon, Doom,
// Devouring Plague, Shadowform).
struct FusionAffliction
{
    uint64 target = 0;
    uint32 spell = 0;               // the rank cast
    uint32 untilMs = 0;
};

// A Splash spell's lasting ground (Moonfire's moonlit patch, FusionSpellcasting.cpp): the spell
// put again on each enemy that comes in without it.
struct FusionHandZone
{
    uint32 spell = 0;               // the rank cast
    uint32 mapId = 0;
    Fusion::Vec3 at;
    float radius = 0.f;
    uint32 untilMs = 0;
    uint32 tickMs = 0;
    uint32 nextTickMs = 0;
    float power = 1.f;              // its release's power (dual, full charge), its own
};

// A hand spell's bolt in flight (FusionSpellcasting.cpp, fusion/Spellcasting.h): the server flies
// it step by step and it strikes the first unit, wall, piece or ground on its path.
struct FusionHandBolt
{
    uint32 id = 0;
    uint32 spell = 0;               // the rank released (paid for at the release)
    uint32 strikes = 0;             // what its impact casts: a missile channel's missile, else `spell`
    uint32 mapId = 0;
    Fusion::Vec3 at;                // where it is now
    Fusion::Vec3 dir;
    float speed = 0.f;              // yards a second; 0 strikes at once
    float left = 0.f;               // yards it may still fly (its fizzle distance or the safety cap)
    float splash = 0.f;             // the splash about where it strikes (yards; 0 none)
    bool ownArea = false;           // ... which is the spell's own area (full strength, its own)
    bool fizzles = false;           // running out of `left` is a fizzle, not the safety cap
    Fusion::Spellcasting::Homing homing;    // its turn (0: straight)
    uint64 homeOn = 0;              // the unit it curves toward, picked at its launch
    uint32 startMs = 0;             // it leaves then (a volley's later missiles wait their turn)
    uint32 lastMs = 0;              // its last step
    uint32 toldMs = 0;              // its course last told (a homing bolt's)
    bool launched = false;          // its flight told to those near
    bool volley = false;            // a later missile of a missile channel
    uint64 claim = 0;               // an instant bolt: the unit the client's crosshair was on
    float power = 1.f;              // its release's power (dual, full charge): this bolt's own,
                                    // never another hand's (fusion/Spellcasting.h ReleasePower)
};

// A hand pouring a spray (FusionSpellcasting.cpp, Shape::Spray), while its button is held.
struct FusionHandSpray
{
    uint32 spell = 0;               // the rank held; 0: not spraying
    Fusion::Vec3 from;              // the aim, as the client last sent it
    Fusion::Vec3 dir;
    uint32 heardMs = 0;             // the client's last word of it
    uint32 nextTickMs = 0;
    uint32 toldMs = 0;              // its course last told to those near
};

// A splashed enemy's share of a hand spell, for the damage hook (its hit and its damage over time):
// the splash's falloff (FusionSpellcasting.cpp).
struct FusionSplashShare
{
    uint64 victim = 0;
    uint32 chain = 0;               // the spell's first rank
    float scale = 1.f;
    uint32 untilMs = 0;
};

// Homing granted to a class's bolts by a talent, a perk or a kit option (FusionClasses::GrantHoming).
struct FusionHomingGrant
{
    uint32 chain = 0;               // a spell's first rank; 0: every bolt
    Fusion::Spellcasting::Homing homing;
    uint32 untilMs = 0;             // 0: until taken back
};

// What the class framework keeps for each player (owned by FusionPlayerData::classes).
struct FusionClassState
{
    bool loaded = false;            // the bought options are read
    bool applied = false;           // the spellbook matches the kit in hand
    std::set<uint16> learned;       // options bought at the trainer
    uint8 kit = 255;                // the slot of the kit in hand (FusionPlayerData::equippedSlot)
    Fusion::Classes::Picks picks{}; // its picks, as applied
    uint8 appliedLevel = 0;
    // Archetype resources (Holy Power, Static, Life Force; shown as buffs).
    std::vector<Fusion::Classes::Meter> meters;
    bool metersDirty = true;
    uint32 nextMeterMs = 0;
    // Spells whose cooldown the client should take from the server (sent the update after the
    // cast, after its SMSG_SPELL_GO), with the cooldown an option forced on it (0: the spell's).
    std::vector<std::pair<uint32, uint32>> cooldownSync;
    std::vector<FusionClassEffect> effects;
    std::vector<FusionClassZone> zones;
    std::vector<FusionClassMinion> minions;
    std::vector<Fusion::Classes::Dot> dots;         // burns, poisons and bleeds it put on others
    std::vector<FusionAuraFix> auraFixes;
    std::vector<FusionAffliction> afflictions;
    uint8 immunities = 0;           // ImmunityFlags applied now
    std::vector<uint64> prey;       // Mark Prey's targets (one; three under Overwatch)
    Fusion::Classes::ChargePool steps;      // a charge-limited ability's charges (none uses it now)
    Fusion::Classes::Camouflage camo;
    uint32 lastFiredMs = 0;         // its last round (Camouflage)
    // Rescues from going down.
    uint32 spiritReadyMs = 0;       // Spirit of Redemption again then
    uint32 reincarnationReadyMs = 0;
    uint64 soulstoneBy = 0;         // the warlock whose stone it carries
    uint32 soulstoneUntilMs = 0;
    uint32 rescueAtMs = 0;          // stands back up then (Reincarnation, a soulstone)
    uint8 rescue = 0;               // Fusion::Classes::Rescue
    uint8 mirrorSwaps = 0;          // Hall of Mirrors
    uint32 nextProcMs = 0;          // a melee proc's own pace (blade waves, claws, stone fists)
    // Melee blows landed (victim, damage), whose procs run at the next update, outside the blow.
    std::vector<std::pair<uint64, uint32>> pendingMelee;
    // The Artificer's constructs standing, oldest first: (option, its key: a minion's guid, or
    // the end time of a wall or a zone).
    std::vector<std::pair<uint16, uint64>> constructs;
    uint64 soulstoneOn = 0;         // a warlock: who carries its stone
    uint64 deathmark = 0;           // Deathmark's target
    float deathmarkDamage = 0.f;    // what the rogue dealt it during the mark
    uint32 nextTickMs = 0;
    uint32 lastSchool = 0;          // Wizard: the last spell's school, for Spellweave
    uint32 premonitionReadyMs = 0;  // an Oracle's Premonition may save this player again then
    // Rogue (CLASS_CATALOG.md section 4).
    uint8 poison = 0;               // Trickster: the Poison Satchel's coat (Fusion::Classes::Poison)
    uint32 lastBlowMs = 0;          // Duelist: the last melee blow (Momentum ebbs without one)
    std::vector<uint64> ripostes;   // Duelist: blows parried, struck back at the next update
    uint8 bannerKind = 0;           // War Banners: the next banner's kind
    bool wasStealthed = false;      // Assassin: leaving stealth starts the opening
    float speed = 1.f;              // the speed SpeedMultiplier gave last (UpdateSpeed on change)
    // Spells cast from the hands (FusionSpellcasting.cpp).
    std::array<uint32, 2> handCastMs{};     // each hand's last cast (left button's, right button's)
    std::array<bool, 2> handCast{};         // ... once it has cast
    uint32 handCasting = 0;                 // the spell a hand is casting this moment: instant
    std::vector<FusionHandZone> handZones;
    std::vector<FusionHandBolt> handBolts;
    std::array<FusionHandSpray, 2> handSprays{};    // each hand's spray (left button's, right's)
    std::vector<FusionSplashShare> splashShares;
    std::vector<FusionHomingGrant> homingGrants;
    // The release being struck this moment (a hand's cast, a bolt's impact, a zone's tick): its
    // own power for the chain's hits, over the windows below. 0: none.
    uint32 releaseChain = 0;
    float releasePower = 0.f;
    // Dual casts' power, by the chain's first rank: until then its later hits (damage over time)
    // are stronger. A hand's direct hits take their own release's power instead.
    std::vector<std::pair<uint32, uint32>> dualWindows;
    // Full charges' power the same way (fusion/Spellcasting.h CAST_FULL).
    std::vector<std::pair<uint32, uint32>> fullWindows;
};

class FusionClasses
{
public:
    static FusionClasses& Instance();

    // Startup (FusionMgr::Load): the characters table, and the class spells fitted to the kit
    // system (no reagents, no stances, the catalog's cooldowns, token costs).
    void Load();
    bool IsEnabled() const { return m_enabled; }

    // Every player update (FusionMgr::UpdatePlayer): load and apply at login, follow the level
    // and the kit in hand, run the effects and send the meters.
    void Update(Player* player, uint32 diffMs);

    // ── The kits (Kits.h; FusionMgr::HandleLoadout) ──
    // The options a character has bought.
    std::set<uint16> const& Learned(Player* player);
    // Whether a kit may hold these picks (the first refused tier in `badTier`).
    Fusion::Classes::PickError ValidatePicks(Player* player, Fusion::Classes::Picks const& picks, uint8* badTier = nullptr);
    // Set the picks of the kit in hand and save them (a GM's `.fusion talents`, a bought option
    // filling an empty tier). False when no kit is in hand.
    bool SetKitPicks(Player* player, Fusion::Classes::Picks const& picks);

    // ── The trainer (WorldSession::SendTrainerList / HandleTrainerBuySpellOpcode) ──
    // A class trainer of the player's class lists the class's options (true: sent).
    bool SendTrainerList(Player* player, Creature* trainer);
    // A purchase from it (true: handled, refused or not).
    bool BuyFromTrainer(Player* player, Creature* trainer, uint32 spellId);
    // Learn an option without paying (a GM's `.fusion option learn`).
    bool GrantOption(Player* player, uint16 id);
    void ForgetOptions(Player* player);

    // ── The client (fusion channel 7) ──
    void HandleChannel(Player* player, std::vector<uint8> const& body);
    void SendState(Player* player);
    void SendMeters(Player* player);
    void SendPage(Player* player);
    // The class spells as fitted at startup, for the client's own Spell.dbc copy (op 7).
    void SendSpellTweaks(Player* player);

    // ── Spells from the hands (FusionSpellcasting.cpp; fusion/Spellcasting.h) ──
    // The class's hand spells, their charge and how each lands (op 9).
    void SendHandSpells(Player* player);
    // A hand released a charged spell at the crosshair (op 12, or op 41 with its aim): cast at
    // once, or say why not.
    void HandCast(Player* player, std::vector<uint8> const& body);
    // A spray held, aimed again or let go (op 43).
    void HandSpray(Player* player, std::vector<uint8> const& body);
    // Spell::SetTargetMap, Spell::EffectPersistentAA: a dual cast's area is wider.
    float RadiusMultiplier(Spell const* spell) const;
    // Spell::HandleEffects: a splashed enemy (Spell::m_fusionSplash) takes no hard control
    // (fusion/Spellcasting.h AuraKind, ControlEffect).
    bool SplashSkipsEffect(Spell const* spell, uint8 effIndex) const;
    // ── Grants for the bolts (talents, perks, kit options; the kit rework fills these) ──
    // Homing for the player's bolts of `chain` (a spell's first rank; 0: every bolt), for `ms`
    // (0: until taken back with RevokeHoming). The turn and cone are the stronger of the spell's
    // own and the grant's (Fusion::Spellcasting::kGrantedHoming is the usual grant). For a perk:
    // GrantHoming(player, 0, SC::kGrantedHoming, 0) when it is equipped, RevokeHoming(player, 0)
    // when it is not; for a talent on one spell: the spell's first rank.
    void GrantHoming(Player* player, uint32 chain, Fusion::Spellcasting::Homing homing, uint32 ms);
    void RevokeHoming(Player* player, uint32 chain);
    // Whether a melee press may lunge (fusion/Movement.h SwingLunges): nothing grants it today; a
    // warrior's Charge perk is where it would.
    bool GrantsSwingLunge(Player const* player) const;

    // ── Spell hooks (Spell.cpp) ──
    // Extra refusals for a class option (a resource it needs, Rekindle in combat...).
    SpellCastResult CheckCast(Spell* spell);
    // A class spell's cast time with the kit (Rekindle: Redemption in 2 s).
    int32 CastTime(Spell* spell, int32 castTime);
    // Whether a downed player may cast this (Desperate Prayer stands its priest up).
    bool CastsWhileDowned(Spell const* spell) const;
    // Whether an option replaces the vanilla effect `effIndex` of the spell being cast.
    bool ReplacesEffect(Spell* spell, uint8 effIndex);
    // A class spell was cast: its option's custom part, resources, the cooldown sync.
    void OnCast(Spell* spell);
    // Talents are gone: the vanilla talent tree learns nothing (WorldSession::HandleLearnTalent).
    bool BlocksTalents() const { return m_enabled; }
    // A default race/class spell the kit system always takes away again (a class line's spell that
    // is no option, utility or basic spell): not learned back at login, so it is not "unlearned"
    // in chat at every login (Player::LearnDefaultSpells).
    bool SkipsDefaultSpell(uint8 cls, uint32 spellId) const;

    // ── Combat hooks ──
    // FusionMgr::Hit: a round of `damage` from `from` would hit `to`; a class shield in the way
    // (or a dodge) stops it (true), and a breakable one wears down.
    bool StopsRound(Unit* from, Unit* to, uint32 damage);
    // Unit::DealDamage: the damage after the class effects of the attacker and the victim.
    uint32 AdjustDamage(Unit* attacker, Unit* victim, uint32 damage, SpellEntry const* spell);
    // FusionMgr::OnUnitKilled.
    void OnKill(Unit* killer, Unit* victim);
    // Unit::SpellHealingBonusTaken: what the healer's class effects make of a heal of `spell`.
    float HealMultiplier(SpellCaster const* healer, SpellEntry const* spell) const;
    // Heals taken by `target`: less under the Afflicter's afflictions (ClassTuning DrainHealMultiplier).
    float HealTakenMultiplier(Unit const* target) const;
    // FusionMgr::MoveSpeedMultiplier: the share of the run speed the class effects leave.
    float SpeedMultiplier(Player const* player) const;
    // FusionMgr::HandleShots: no shot or swing (disarmed, Blessing of Protection, charmed).
    bool BlocksFire(Player const* player) const;
    // A round went off (Camouflage gives way).
    void OnFired(Player* player);
    // How much wider a player's rounds scatter (Demoralizing Shout).
    float SpreadMultiplier(Player const* player) const;
    // FusionMgr::UpdatePlayer: no sprint (Earthbind, a net, tar, a bear trap, Curse of Exhaustion).
    bool BlocksSprint(Player const* player) const;
    // ── Going down (FusionDowned.cpp) ──
    // A fatal blow on a player: true holds them up at 1 health (Spirit of Redemption).
    bool PreventsDown(Player* player);
    // They went down: Reincarnation or a soulstone stands them back up a little later.
    void OnWentDown(Player* player);
    // ── Hunter traps (Spell::EffectSummonObject, GameObject::Update) ──
    // A new trap in the same slot leaves the old one standing (the Trapper's limit is kept here).
    bool KeepsOldTrap(Unit* caster, GameObject* old);
    // One of a hunter's traps went off on `target`: the others near it go off too.
    void OnTrapFired(GameObject* trap, Unit* owner, Unit* target);
    // FusionMgr::UpdateCharges: a grenade or a flash of `thrower`'s going off at `at`; a
    // Grounding Totem of its enemies within 10 yd catches it (once): where it goes off instead.
    bool GroundingCatch(Unit* thrower, uint32 mapId, Fusion::Vec3& at);
    // WorldSession::HandleUseItemOpcode: a class item (a mage's Arcane Stim); true when used.
    bool UseClassItem(Player* player, Item* item);
    // FusionMgr::AggroRadiusMultiplier: whether smoke hides `b` from `a`.
    bool SightBlocked(uint32 mapId, Fusion::Vec3 a, Fusion::Vec3 b) const;

    // For `.fusion talents` and `.fusion option`.
    std::string Report(Player* player);

    // ── One entry per ability (fusion/Classes.h SingleEntry) ──
    // The ability a stale reference to `spellId` (a lower rank on an old bar, in a macro, in a
    // hand readied before a level-up) is cast as: the highest rank of its chain the player has
    // active; 0 if it is no class spell's rank or none is known.
    uint32 SingleEntryOf(Player const* player, uint32 spellId) const;

private:
    FusionClassState& StateOf(Player* player);
    FusionClassState const* StateOf(Player const* player) const;
    void LoadState(Player* player);
    void Apply(Player* player, bool layOutBar);
    void LayOutBar(Player* player);
    bool BarNeedsLayout(Player* player) const;
    // A retired class spell's buttons get their replacement (Classes::MigrateRetiredButtons): a
    // Bard's instruments become the Duelist's options. True when it moved any.
    bool MigrateRetiredBar(Player* player);
    // The ranks of a spell's chain, lowest first.
    std::vector<Fusion::Classes::Rank> Chain(uint32 spellId) const;
    // A chain's spell at the character's level.
    uint32 RankFor(uint32 spellId, uint8 level) const;
    // The option a cast spell is (any rank), of the caster's class; null if not one.
    Fusion::Classes::Option const* OptionOfSpell(uint8 cls, uint32 spellId) const;
    // The option whose damage a spell deals: its own, or the one whose spell set it off.
    Fusion::Classes::Option const* DamageOptionOf(uint8 cls, uint32 spellId) const;
    // The catalog's spell (an option's or a Utility spell) whose chain has `spellId`; 0 if none.
    uint32 CatalogSpellOf(uint8 cls, uint32 spellId) const;
    // The class spells fitted to the kit system at startup.
    void TweakSpells();
    // Every class spell's chain made one spellbook entry (SpellMgr's successors), after TweakSpells.
    void BuildSingleEntries();
    // A class's ranked chains (lowest rank first).
    std::vector<std::vector<Fusion::Classes::Rank>> ChainsOf(uint8 cls) const;
    // The druid forms' spells rewritten as shapeshifts (fusion/DruidForms.h).
    void FitDruidForms();
    // The forms the client's own DBC copy does not know, and every form's bar page (op 8).
    void SendForms(Player* player);
    // Each known form's own bar page laid out (its abilities, then Barkskin).
    void LayOutFormBars(Player* player, bool force);
    void SyncCooldowns(Player* player, FusionClassState& s);
    void RebuildMeters(Player* player, FusionClassState& s);
    // The hand spells of every class, found and fitted after TweakSpells: no movement interrupt,
    // an area spell aimed at a point.
    void FitHandSpells();
    // A spell as the hand rules read it (fusion/Spellcasting.h).
    Fusion::Spellcasting::SpellFacts HandFacts(SpellEntry const* e) const;
    // Cast from a hand: instant, past the global cooldown (the charge stands for it).
    SpellCastResult CastFromHand(Player* player, FusionClassState& s, SpellEntry const* info, Unit* target,
        Fusion::Vec3 const* point);
    void TickHandZones(Player* player, FusionClassState& s, uint32 now);
    // ── Free-aimed bolts (Shape::Missile) ──
    // What a bolt spell flies as, read off its entry at startup.
    struct HandBoltFacts
    {
        uint32 strikes = 0;         // the spell its impact casts
        float speed = 0.f;
        uint8 volley = 1;           // missiles a release fires
        float ownArea = 0.f;        // its own area at the impact (an enemy area at the destination)
        Fusion::Spellcasting::BoltTraits traits;    // fizzle, splash, homing (the spell's own)
    };
    HandBoltFacts BoltFactsOf(SpellEntry const* e) const;
    // A bolt's traits for this player: the spell's own and what is granted (GrantHoming).
    Fusion::Spellcasting::BoltTraits TraitsFor(FusionClassState& s, SpellEntry const* info,
        HandBoltFacts const& facts, uint32 now) const;
    // Every update while a spray pours: its ticks, a stale aim, the mana running out.
    void TickHandSprays(Player* player, FusionClassState& s, uint32 now);
    // A hand's spray stops (`tellClient`: the hand fizzles too, the server stopped it).
    void StopSpray(Player* player, FusionClassState& s, uint8 hand, bool tellClient);
    // Its course to those near (its caster draws its own).
    void TellSpray(Player* player, FusionHandSpray const& sp, uint8 hand, bool on);
    // A splashed enemy's share of a hand spell: the splash's falloff (1: none).
    float SplashShareOf(FusionClassState const& s, Unit const* victim, SpellEntry const* spell, uint32 now) const;
    // The release's price, paid now as a cast would (a bolt's impact lands later, triggered): its
    // checks (cooldown, silence, power, the option's resource), then power, cooldown, global
    // cooldown. Refused: nothing paid.
    Fusion::Spellcasting::CastError PayRelease(Player* player, SpellEntry const* info);
    // Release bolts along the aim; the hands are marked by the caller.
    Fusion::Spellcasting::CastError ReleaseBolt(Player* player, FusionClassState& s, SpellEntry const* info,
        Fusion::Spellcasting::HandCast const& c, Fusion::Vec3 origin, Fusion::Vec3 dir);
    // Fly every bolt one step; strike what it meets.
    void TickHandBolts(Player* player, FusionClassState& s, uint32 now);
    // One bolt's step (true: it is done).
    bool StepBolt(Player* player, FusionHandBolt& b, uint32 now);
    // A bolt struck: the spell on the unit it hit (or the world), the pieces, those near.
    void BoltStrikes(Player* player, FusionHandBolt const& b, Fusion::Vec3 at, Unit* unit, uint32 piece, bool spent);
    // A dual cast's power for a hit of `spell` (1 without one).
    float DualPower(FusionClassState const& s, SpellEntry const* spell, uint32 now) const;

    // FusionClassAbilities.cpp: the custom parts and the shared systems.
public:
    Fusion::Classes::Meter* MeterOf(FusionClassState& s, Fusion::Classes::Resource r);
    // Add to a resource; at its cap some fire (Fury's Frenzy, Solar Power's Zenith, Eclipse).
    void Gain(Player* player, FusionClassState& s, Fusion::Classes::Resource r, uint16 n);
    // What the removed resources paid for, now set off directly (GAME_DESIGN.md "Class resources").
    void Frenzy(Player* player, FusionClassState& s, uint32 ms);
    void Zenith(Player* player, FusionClassState& s);
    void Eclipse(Player* player, FusionClassState& s);
    // Arcane Edge is up: the next Wizard damage spell is instant.
    bool HasArcaneEdge(FusionClassState const& s) const;
    FusionClassEffect* EffectOf(FusionClassState& s, uint16 code, uint32 now, uint64 source = 0);
    bool HasEffect(FusionClassState const& s, uint16 code, uint32 now) const;
    void AddEffect(Player* player, FusionClassState& s, uint16 code, uint32 ms, uint64 source = 0, float value = 0.f);
    void AddBlocker(uint32 mapId, Fusion::Classes::Blocker const& b);
    void AddSmoke(Player* owner, Fusion::Vec3 at, float radius, uint32 ms);
    void Spot(Player* owner, std::vector<Unit*> const& units, uint32 ms);
private:
    // The rogue: back into stealth in combat as Vanish does (Smoke Bomb); Light Fingers' lift.
    void Restealth(Player* player);
    // The Assassin's opening and a Distract's flank (CheckCast's opener gate), and the opening
    // started at once (an opener cast from stealth).
    bool HasOpening(FusionClassState const& s, uint32 now) const;
    bool HasFlank(FusionClassState const& s, uint32 now) const;
    void OpenNow(Player* player, FusionClassState& s);
    void LiftPurse(Player* player, Unit* target);
    void CastOption(Player* player, FusionClassState& s, Fusion::Classes::Option const& o, Spell* spell);
    void GainOnCast(Player* player, FusionClassState& s, Fusion::Classes::Option const& o, Spell* spell);
    void TickEffects(Player* player, FusionClassState& s, uint32 now);
    void TickZones(Player* player, FusionClassState& s, uint32 now);
    void TickMinions(Player* player, FusionClassState& s, uint32 now);
    void EndEffect(Player* player, FusionClassState& s, FusionClassEffect const& e);
    uint32 AdjustTaken(Unit* attacker, Player* victim, FusionClassState& s, uint32 damage, bool bullet, bool blow = false);
    // The class identity pass on a class spell's hit (fusion/ClassTuning.h).
    uint32 IdentityDamage(Player* attacker, Unit* victim, uint32 damage, SpellEntry const* spell);
    uint32 AdjustDealt(Player* attacker, Unit* victim, FusionClassState& s, uint32 damage, bool bullet,
        SpellEntry const* spell);
    // A melee blow of the attacker's landed (hits, procs, poisons); `damage` as dealt.
    void MeleeProcs(Player* attacker, Unit* victim, FusionClassState& s, uint32 damage);
    // The blows queued since the last update.
    void RunMeleeProcs(Player* player, FusionClassState& s);
    // A new construct of the Artificer's: the oldest goes past the limit.
    void AddConstruct(Player* player, FusionClassState& s, uint16 code, uint64 key);
    Creature* RaiseMinion(Player* player, FusionClassState& s, uint16 code, uint32 entry, Fusion::Vec3 at,
        uint32 ms, float healthShare);
    // The immunities the player's running effects grant, applied and lifted as they change.
    void UpdateImmunities(Player* player, FusionClassState& s, uint32 now);
    void TickAuraFixes(Player* player, FusionClassState& s, uint32 now);
    void TickDots(Player* player, FusionClassState& s, uint32 now);
    void TickRescue(Player* player, FusionClassState& s, uint32 now);
    void TickCamouflage(Player* player, FusionClassState& s, uint32 now);
    // The hunter's traps standing now, oldest first.
    std::vector<GameObject*> TrapsOf(Player* player) const;
    // An enemy creature's mark from a class effect (Demoralizing Shout): its damage dealt.
    void MarkUnit(Unit* u, uint16 code, uint32 ms, float value);
    float UnitMark(Unit const* u, uint16 code) const;
    struct UnitMarkEntry
    {
        uint16 code = 0;
        uint32 untilMs = 0;
        float value = 0.f;
    };
    std::unordered_map<uint64, std::vector<UnitMarkEntry>> m_unitMarks;
    // Grounding Totems standing, by map (the world lock): each catches one grenade.
    struct Grounder
    {
        uint64 owner = 0;
        Fusion::Vec3 at;
        uint32 untilMs = 0;
        bool used = false;
    };
    std::unordered_map<uint32, std::vector<Grounder>> m_grounders;

    bool m_enabled = false;
    // Fusion.Classes.RangedSpellPower (fusion/ClassEffects.h RangedSpellScale).
    float m_rangedSpellPower = Fusion::Classes::kRangedSpellPower;
    // The spell ids of every option and their ranks, for the hot hooks: class -> spell -> option.
    std::unordered_map<uint32, std::unordered_map<uint32, uint16>> m_spellOption;
    // The spells an option's spells set off (EffectTriggerSpell), by class: their damage is the option's.
    std::unordered_map<uint32, std::unordered_map<uint32, uint16>> m_triggerOption;
    // Spells whose vanilla effects an option replaces (by effect index mask).
    std::unordered_map<uint32, uint8> m_replaced;
    // Every rank fitted at startup and how (the Tweak flags), for the client's copy.
    std::unordered_map<uint32, uint32> m_tweaked;
    // The class spells' ranked chains (lowest first), each one entry in the book: every rank's
    // chain, and each class's chains (its options', Utility's and basic spell's).
    std::vector<std::vector<Fusion::Classes::Rank>> m_chains;
    std::unordered_map<uint32, uint32> m_chainOfRank;
    std::unordered_map<uint8, std::vector<uint32>> m_classChains;
    // Every rank a hand can hold, and each class's (FitHandSpells).
    std::unordered_map<uint32, Fusion::Spellcasting::HandSpell> m_handSpells;
    std::unordered_map<uint8, std::vector<Fusion::Spellcasting::HandSpell>> m_handSpellsOf;
    // Every bolt rank's flight (FitHandSpells), and the bolts' ids (players update on many threads).
    std::unordered_map<uint32, HandBoltFacts> m_handBolts;
    std::atomic<uint32> m_nextBoltId{0};
    // The world's class objects (bullet blockers, smoke), by map. Players on one map update on
    // one thread, but a shot reads another player's shield: locked.
    mutable std::mutex m_worldLock;
    std::unordered_map<uint32, std::vector<Fusion::Classes::Blocker>> m_blockers;
    std::unordered_map<uint32, std::vector<Fusion::Classes::Smoke>> m_smokes;
};

#define sFusionClasses FusionClasses::Instance()

#endif
