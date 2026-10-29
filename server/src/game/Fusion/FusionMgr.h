/*
 * Azeroth Warfare: where the fusion rules (src/fusion) meet the game. The rules are plain data
 * in, data out; this manager feeds them the game's state at its decision points: shots, the
 * client's stance and gait, creature aggro, and per-player ammo, streaks and the FFA flag.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef MANGOS_FUSION_MGR_H
#define MANGOS_FUSION_MGR_H

#include "Common.h"
#include "FusionCrafts.h"
#include "FusionMinstrelsy.h"
#include "fusion/Throttle.h"
#include "fusion/Achievements.h"
#include "fusion/ActionSystems.h"
#include "fusion/Ammo.h"
#include "fusion/Bounce.h"
#include "fusion/Build.h"
#include "fusion/CompanionBattles.h"
#include "fusion/CompanionIndex.h"
#include "fusion/CompanionRest.h"
#include "fusion/Companions.h"
#include "fusion/Defense.h"
#include "fusion/Downed.h"
#include "fusion/NpcGuns.h"
#include "fusion/Catalog.h"
#include "fusion/Loadout.h"
#include "fusion/Perks.h"
#include "fusion/Pickups.h"
#include "fusion/Rounds.h"
#include "fusion/Clock.h"
#include "fusion/Construction.h"
#include "fusion/Damage.h"
#include "fusion/Danger.h"
#include "fusion/FieldGear.h"
#include "fusion/Furniture.h"
#include "fusion/Gathering.h"
#include "fusion/Maintenance.h"
#include "fusion/Gunfights.h"
#include "fusion/HitValidation.h"
#include "fusion/Homestead.h"
#include "fusion/Killstreak.h"
#include "fusion/Kits.h"
#include "fusion/Locks.h"
#include "fusion/Launch.h"
#include "fusion/Match.h"
#include "fusion/Melee.h"
#include "fusion/Movement.h"
#include "fusion/Packets.h"
#include "fusion/Plots.h"
#include "fusion/Portals.h"
#include "fusion/Prestige.h"
#include "fusion/Pvp.h"
#include "fusion/Reload.h"
#include "fusion/Riding.h"
#include "fusion/Scale.h"
#include "fusion/SkyStreaks.h"
#include "fusion/Spellcasting.h"
#include "fusion/Stats.h"
#include "fusion/Weapon.h"

#include <array>
#include <atomic>
#include <set>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

class Creature;
class Item;
class Pet;
class GameObject;
class Player;
class Unit;
class QueryResult;
class Spell;
class SpellEntry;
struct Loot;
struct FusionCompanionDuel;
struct FusionCompanionBattle;
struct FusionClassState;

// A battle companion's line for its owner's UI error frame (FusionCompanionIndex.cpp), with the
// chat handler's two calls, so a line leaves the chat window by its handler's type alone. Colour
// codes pick the kind and are dropped: none or red a refusal, |cffffd200 news, any other a success.
struct FusionCompanionTell
{
    explicit FusionCompanionTell(Player* p) : player(p) {}
    void SendSysMessage(char const* text) const;
    void PSendSysMessage(char const* format, ...) const;
    Player* player;
};

// The battle companion a player has out (FusionCompanions.cpp, fusion/Companions.h). Its level
// and experience live with its item (FusionMgr keeps them by item guid); this is only what the
// summon itself needs.
struct FusionCompanionOut
{
    ObjectGuid pet;                     // the guardian; empty when none is out
    ObjectGuid item;                    // the item it was summoned from
    uint32 mapId = 0;                   // where it was summoned: a teleport takes it along
    uint8 builtLevel = 0;               // the owner's level its stats were last built for
    bool duelStats = false;             // built at a companion duel's levels
    uint32 nextLookMs = 0;
    // A mount companion being ridden: the guardian is gone and its owner sits on the vanilla
    // mount spell's aura; getting off (or being knocked off) puts it back on foot.
    bool riding = false;
    uint32 rideSpell = 0;
    float rideHealth = 1.f;             // its health share when mounted, restored on foot
    uint32 rideDisplay = 0;             // the display it is ridden with (Index::MountDisplay)
    uint32 rideSinceMs = 0;             // the mount spell was cast then (its aura may lag a tick)
    uint32 rideCastMs = 0;              // its cast time: triggered, it is in no spell slot
    bool rideLeaving = false;           // its owner asked to get off (the aura may still land)
    // Mounting (Companions::MountCastMs): a cast its owner stands still for while it runs to the
    // spot in front of them; the ride begins when it ends. 0: not mounting.
    uint32 mountUntilMs = 0;
    float mountFromX = 0.f, mountFromY = 0.f, mountFromZ = 0.f;     // where its owner stood
    // What the client's bar last showed, to send it again only when something changed.
    uint8 sentFlags = 0xFF;
    uint8 sentBlock = 0xFF;
    uint8 sentHealth = 0xFF;
    // Put away by the game for a while (a taxi, another mount, a map change): it comes back
    // when that is over.
    bool suspended = false;
    uint32 nextOldPetLookMs = 0;
    // The pet bar (fusion/CompanionMoves.h): each slot's cooldown and autocast.
    std::array<uint32, 10> readyAtMs{};
    std::array<bool, 10> autocast{{true, true, true, true, true, true, false, true, true, true}};
    uint64 ownerHitBy = 0;          // the last enemy to hit the owner, and when (taunts)
    uint32 ownerHitMs = 0;
    uint32 nextShotMs = 0;          // a Gunner's next shot
    uint32 nextProcMs = 0;          // its traits' on-hit effects rest until then
    uint32 packmuleMs = 0;          // a Packmule's next magazine
    bool berserk = false;           // Berserk is on
    uint32 trackingSpell = 0;       // a Tracker's tracking, cast on the owner
    // Abilities whose aura durations follow the catalog, not the vanilla spell: clamped on the
    // next look once their aura is on.
    struct Clamp
    {
        uint64 target = 0;
        uint32 spell = 0;
        int32 durationMs = 0;
        uint32 untilMs = 0;
    };
    std::vector<Clamp> clamps;
    bool lineageSent = false;       // the bags' lineage lines went to the client

    // What outlives one summon (DismissCompanion keeps it): the pet bar's autocasts and stance,
    // and of the companion last out its cooldowns. Its wounds and whether it fainted are kept
    // per item by FusionMgr (Companions::Recovery, CompanionRest.h), saved, so sending it back
    // and calling it again is no free heal, and a fainted one stays fainted through a relog.
    uint8 react = 1;                    // ReactStates, REACT_DEFENSIVE (Assist)
    // How each other companion went back (its cooldowns), so swapping between two is no
    // cooldown reset; lastItem is the one out last (its cooldowns are readyAtMs).
    Fusion::Companions::RestBook rests;
    uint32 indexAskedMs = 0;            // the Companions window's last full index (paced)
    ObjectGuid lastItem;
    float seenHealth = 1.f;             // its health share at the last look
    // Back at its owner's side once it can be: after the owner's death or a logout (the item
    // was out, characters.character_fusion_companion_out).
    ObjectGuid returnItem;
    // The bags' fainted companions are looked at now and then: one whose wait is over says so.
    uint32 nextFaintLookMs = 0;
    // A fainted companion's item used with a Reviving Draught in the bags: used again soon, the
    // draught goes to it.
    ObjectGuid draughtAskItem;
    uint32 draughtAskMs = 0;
    // The owed ability choice was announced for this item and stage (once, not every summon).
    ObjectGuid pickToldItem;
    int8 pickToldStage = -1;
    bool rideMounted = false;           // the mount spell's aura landed (else: not here)
    bool petDied = false;               // it was killed (a corpse that despawns at once included)
};

// What dealt the fusion hit being applied now (FusionMgr::Hit sets it for the call), so a kill
// it causes is counted with its gun, headshot, melee or equipment (fusion/Achievements.h).
struct FusionHitSource
{
    uint16 weaponId = 0;
    bool headshot = false;
    bool melee = false;
    bool explosive = false;
    Fusion::EquipmentKind equipment = Fusion::EquipmentKind::None;
};

// What the fusion keeps for each player; owned by the Player (Player::GetFusion).
// Something of a player's in the world waiting to go off: a thrown grenade, a planted claymore or
// C4, a called-in strike.
struct FusionCharge
{
    // NpcFrag: an NPC gunner's grenade, kept on the player it was thrown at (FusionMgr::NpcExplode).
    enum class Kind : uint8 { Blast, Claymore, C4, Flash, Stun, Smoke, Hover, Gunner, Nuke, NpcFrag };
    Kind kind = Kind::Blast;
    uint16 weaponId = 0;
    Fusion::Vec3 pos;
    float facing = 0.f;
    uint32 atMs = 0;                // goes off (Blast, tacticals, Nuke), starts (Hover, Gunner)
    uint32 untilMs = 0;             // Hover, Gunner: how long they keep firing
    uint32 nextMs = 0;              // Hover, Gunner, Claymore: the next look
    float radius = 0.f, inner = 0.f, outer = 0.f;
    uint32 mapId = 0;
    uint64 thrower = 0;             // NpcFrag: the creature that threw it
};

// What an NPC gunner remembers of its fight (fusion/NpcGuns.h): made the first time it fights a
// player, kept on the creature (Creature::m_fusionGunner).
struct FusionGunnerState
{
    Fusion::GunnerMind mind;
    Fusion::HurtWindow hurt;
    Fusion::SightTrack sight;
    Fusion::NpcAimTrack aim;        // how long it has held its aim on its target (Gunfights.h)
    uint32 shotAtMs = 0;            // a player's round last passed near it (server clock)
    bool hasThrown = false;
    uint32 thrownMs = 0;            // its last grenade
    uint32 grenadeLiveUntilMs = 0;  // its grenade is in the air (or on its fuse) until
    uint32 grenadeThinkMs = 0;
};

// A lore killstreak's craft out (fusion/SkyStreaks.h, FusionSkyStreaks.cpp): a creature the
// server flies or sets down, and what it is doing.
struct FusionSkyCraft
{
    Fusion::Sky::Craft craft = Fusion::Sky::Craft::Rider;
    Fusion::Team team = Fusion::Team::Alliance;     // the owner's, for its looks and sounds
    ObjectGuid guid;
    uint32 mapId = 0;
    uint32 startMs = 0;
    uint32 untilMs = 0;                 // its work is done (then it flies away, or is taken down)
    uint32 nextShotMs = 0;
    uint32 nextPathMs = 0;              // when its ring's next stretch is flown
    uint32 pathEndMs = 0;               // where on the ring (ms) that stretch ends
    // A ring (the gunships, the hornet after its pass); a gunship flies in to it first, until
    // orbitStartMs.
    bool orbiting = false;
    Fusion::Sky::Orbit orbit;
    uint32 orbitStartMs = 0;
    // A bombing pass (the bomber, the hornet first): where each bomb lands, on the ground.
    bool bombing = false;
    Fusion::Sky::BombRun run;
    uint32 runStartMs = 0;
    uint32 bombsDropped = 0;
    std::vector<Fusion::Vec3> marks;
    std::vector<std::pair<uint32, Fusion::Vec3>> falling;  // bombs on their way down: when, where
    // Flying away when done, gone at goneMs.
    bool leaving = false;
    uint32 goneMs = 0;
    // Its gun: whom it is on, how many rounds it has put at them, when it looks again.
    uint64 target = 0;
    uint32 retargetMs = 0;
    uint32 roundsOnTarget = 0;
    // A turret: where it stands, its arc's middle, the way it is shown pointing, when it may turn
    // again, when it is set up, when its barrel has spun up on its target.
    Fusion::Vec3 at;
    float facing = 0.f;
    float shownYaw = 0.f;
    uint32 turnMs = 0;
    uint32 activeMs = 0;
    uint32 engageMs = 0;
    // Shot at: by whom last, and when; shot down.
    uint64 hitBy = 0;
    uint32 hitByMs = 0;
    bool downed = false;
    // The owner's HUD (world op 39): the health share last sent, the next refresh; its kills.
    uint8 sentHealth = 0xFF;
    uint32 nextNewsMs = 0;
    uint32 kills = 0;
    // Its sounds: the players its engine loop was pushed to, the next wing beat, its arrival cry.
    std::vector<uint64> heard;
    uint32 nextHearMs = 0;
    uint32 nextBeatMs = 0;
    bool cried = false;
};

// The Goblin Rocket the player is steering (world ops 37 and 38).
struct FusionRocket
{
    bool active = false;
    uint32 ticket = 0;
    ObjectGuid vehicle;
    uint32 mapId = 0;
    uint32 startMs = 0;
    Fusion::Vec3 start;
    Fusion::Vec3 last;                  // the last report the server took
    uint32 lastMs = 0;
    uint32 movedMs = 0;                 // the creature's last move (for the others' view)
    // Its flight reports each cost a test against the world: the client's ten a second, with
    // slack for a hitch, and no more (an impact or a bail always counts).
    Fusion::RateLimit reports{6, 60};
};

// The bounty boards (FusionBounties.cpp): a bounty this character took (aw_bounty_char).
struct FusionBountyRow
{
    uint32 quest = 0;
    uint32 day = 0;                     // the day it was posted
    uint32 key = 0;                     // what it asked (Bounty::Bounty::key)
    uint32 doneDay = 0;                 // the day it was turned in; 0 while it is not
};

struct FusionBountyData
{
    bool loaded = false;                // the rows read from the database (at the first update)
    std::vector<FusionBountyRow> rows;
    ObjectGuid board;                   // the board last used
    uint32 checkedGeneration = 0;       // the postings the quest log was last checked against
    uint32 checkMs = 0;
};

struct FusionPlayerData
{
    Fusion::Stance stance = Fusion::Stance::Stand;
    Fusion::Gait gait = Fusion::Gait::Run;
    uint8 stateFlags = 0;
    uint16 heldWeaponId = 0;
    Fusion::AmmoType loadedAmmo = Fusion::AmmoType::Basic;

    std::unordered_map<uint16, Fusion::GunAmmo> ammo;  // per gun, made on first use
    uint32 lastShotMs = 0;                              // server clock
    bool hasLastShot = false;
    // Each gun's reload pace (fusion/Reload.h): a reload is held to a share of its honest time,
    // counted from the gun's last round. Keyed by gun id.
    std::unordered_map<uint16, Fusion::ReloadPace> reloadPace;
    Fusion::RecentLeast reloadFactor;                   // the quickest reload factor of late

    Fusion::SprintStamina stamina;
    bool sprinting = false;                             // asked to sprint and has the stamina
    std::optional<uint32> sprintEndedMs;                // server clock; the sprint-out delay runs from here

    Fusion::PerkFlags perks;                            // the class's movement perks
    uint32 lastMoveActionMs = 0;                        // the last mantle or lunge, to rate-limit them
    uint32 lastLungeMs = 0;                             // the last lunge
    uint32 lastSwingMs = 0;                             // the last melee swing (or lunge)
    uint64 lastStruckGuid = 0;                          // the last EventKind::Struck sent (NoteStruck)
    uint32 lastStruckMs = 0;
    Fusion::Melee::Combo combo;                         // the swings' chain (fusion/Melee.h)
    Fusion::Melee::Guard guard;                         // the shield raised (the Classic layer's right button)
    uint32 meleeEntry = 0xFFFFFFFF;                     // the main hand the client was last told of
    uint32 offHandEntry = 0xFFFFFFFF;                   // and the off hand
    // The action system the client is in (fusion/ActionSystems.h): Utility's hands are bare.
    Fusion::ActionSystems::System actionSystem = Fusion::ActionSystems::System::Modern;
    // Its hands' pose and acts relayed to those near (ActionSystems.h): ~12 a second each.
    Fusion::RateLimit poseLimit{12, 80};
    Fusion::RateLimit actLimit{8, 100};
    // Utility's tool in the hands (fusion/Gathering.h; 0 bare hands), the strikes on what it
    // works now, and the Repair Hammer's credit on the piece it mends (fusion/Maintenance.h).
    uint32 utilityTool = 0;
    Fusion::Gathering::Progress toolProgress;
    // The tools mask Utility's bar was last told (ActionSystems.h op 9; 0xFFFF: not yet), and
    // when it was last looked at.
    uint16 utilityToolsSent = 0xFFFF;
    uint32 utilityToolsCheckMs = 0;
    Fusion::Maintenance::Credit repairCredit;
    int8 proOverride = -1;                              // .fusion pro: -1 as the account (GM: Pro), 0 off, 1 on
    uint32 lastAlertMs = 0;                             // gunfire's last alert (throttled)
    // Creatures this player's gunfire may still draw (Gunfights.h), and those that heard it
    // behind a wall and come after a beat.
    Fusion::HearingBudget hearing;
    struct HeardAlert
    {
        uint64 creature = 0;
        uint32 dueMs = 0;
        float radius = 0.f;
    };
    std::vector<HeardAlert> heardAlerts;
    // Building: the pieces this client has been sent (on knownMap), the next streaming look.
    std::unordered_set<uint32> knownPieces;
    uint32 knownMap = 0xFFFFFFFF;
    uint32 nextBuildMs = 0;
    uint32 nextNpcGunsMs = 0;                           // the next list of NPC gunners near
    uint32 lastPlaceMs = 0;
    uint8 trappedLooks = 0;                             // building looks running this body was inside a piece
    bool sentBuildStatus = false;
    // Sentry guns of this player's within reach, with their triggers.
    struct Sentry
    {
        uint32 piece = 0;
        Fusion::Vec3 muzzle;
        float yaw = 0.f;                                // the way it was set down facing
        Fusion::NpcTrigger trigger;
        uint64 target = 0;
        bool streak = false;                            // the killstreak's: shoots enemy players too
    };
    std::vector<Sentry> sentries;
    // Raids on this player's base.
    Fusion::Defense::RaidClock raid;
    std::vector<ObjectGuid> raiders;
    Fusion::Vec3 raidBase;
    uint32 raidSize = 0;
    // Deathstreaks.
    Fusion::DeathStreak deathStreak;
    bool wasAlive = true;                               // to see a respawn
    uint32 painkillerUntilMs = 0;
    // The downed state (FusionDowned.cpp, fusion/Downed.h): down at 1 health instead of dead;
    // Last Stand fights back with a pistol, Final Stand gets back up (once a life).
    Fusion::Downed::State downed;
    ObjectGuid downedBy;                                // who put them down (credited if they bleed out)
    uint16 downedPistol = 0;                            // Last Stand's pistol, 0 when unarmed
    uint8 downedCause = 0;                              // Downed::DeathCause of a death while down
    // A GM's god mode (Player::SetCheatGod: an invincibility threshold) lifted for the killing
    // blow the downed state decided on, given back when the state ends; 0 none.
    uint32 downedGodHp = 0;
    uint32 nextDownedLookMs = 0;                        // creatures turning away
    bool finalStandUsed = false;
    Fusion::Downed::KitUse kitUse;                      // a revival kit at work (by this player)
    ObjectGuid kitWorker;                               // another player working a kit on this one
    uint32 martyrdomAtMs = 0;                           // the dropped grenade goes off then
    Fusion::Vec3 martyrdomPos;
    uint32 martyrdomMap = 0;
    std::vector<FusionCharge> charges;                  // equipment and strikes in the world

    Fusion::FfaFlag ffa;
    Fusion::Streak streak;
    Fusion::PoseHistory poses;                          // for lag-compensated hits on this player
    float outOfCombatSeconds = 0.f;

    // The client's shot clock against the server's, following drift (fusion/Clock.h).
    Fusion::ClockSync clock;

    // The gun's ammunition; an akimbo pair's left gun keeps its own, under its id | kLeftGun.
    static constexpr uint16 kLeftGun = 0x8000;
    Fusion::GunAmmo& AmmoFor(Fusion::WeaponProfile const& w, bool left = false);

    // Create-a-Class (MW2 design spec 1.4, 2.2): the five saved classes, the one in hand and one
    // waiting to be equipped (Fusion::kNoClass for none).
    std::array<std::optional<Fusion::ClassLoadout>, Fusion::kClassSlots> classes;
    uint8 equippedSlot = Fusion::kNoClass;
    uint8 pendingSlot = Fusion::kNoClass;
    bool sentClasses = false;                           // the login reply has gone out
    // Why the class equipped at logout could not be equipped at login (told after the reply).
    Fusion::LoadoutError loginRefusal = Fusion::LoadoutError::None;
    uint8 loginRefusedSlot = Fusion::kNoClass;
    // Saves write the database: a few at once, then one a second.
    Fusion::RateLimit loadoutLimit{5, 1000};
    // The class in hand as it was when equipped: editing the saved class waits for an equip.
    std::optional<Fusion::ClassLoadout> applied;
    // The field editor (fusion/Kits.h): its window and cooldown, unix seconds.
    Fusion::FieldEditor fieldEditor;
    uint32 fieldRearmReadyMs = 0;                       // Field Quartermaster's field rearm (Perks.h)

    // Achievements and unlocks (fusion/Achievements.h): the account's counters and earned
    // achievements, loaded at login, and the unlocks they grant.
    Fusion::AccountStats stats;
    std::set<uint32> earned;
    Fusion::UnlockSet unlocks;
    bool achievementsLoaded = false;
    uint32 playerKillRun = 0;                           // enemy players killed this life
    uint64 lastKiller = 0;                              // the player who last killed this one
    uint32 burstStartMs = 0, burstKills = 0;            // kills close together (multi-kills)
    float sprintYards = 0.f;                            // sprinted, not yet counted
    FusionCompanionOut companion;                       // the battle companion out, if any
    // Summoning, putting away and riding a companion: a few at once, then one every two seconds.
    // Each summon takes a fresh pet guid (never reused while the server runs; their end stops
    // the server) and writes the database.
    Fusion::RateLimit companionSummons{3, 2000};
    // The channel messages that cost real work (fusion/Throttle.h): one allowance for them all.
    Fusion::RateLimit costlyAsks{Fusion::Throttle::kCostlyBurst, Fusion::Throttle::kCostlyRefillMs};
    // The Bestiary (FusionCompanionIndex.cpp): the species seen or caught, loaded at login,
    // and the stable master the Companions window was opened at (its full view while near).
    Fusion::Companions::Index::SeenMap companionSeen;
    bool companionSeenLoaded = false;
    ObjectGuid companionStable;
    FusionBountyData bounty;            // the bounty boards (FusionBounties.cpp)
    FusionMinstrelData minstrel;        // Minstrelsy (FusionMinstrelsy.cpp)
    FusionCraftsData crafts;            // Inscription, Jewelcrafting, the kits' glyphs and gems (FusionCrafts.cpp)
    // The kit carried's glyphs for the perks' hooks (cast time, melee, detection, falls, loot...);
    // their handling and the gun in hand's gems go into `combat` (FusionCrafts.cpp ApplyHandling).
    Fusion::Crafts::Mods augments;
    std::shared_ptr<FusionClassState> classFramework;   // the class framework's (FusionClasses.h)
    Fusion::ClassLoadout const* Equipped() const { return applied ? &*applied : nullptr; }

    // Construction (FusionConstruction.cpp, src/fusion/docs/CONSTRUCTION.md): the profession's
    // tier and skill, loaded at login; whether the client has had its recipes and skill; the plot
    // it was last told it stands on (and the plots' generation then, so a sale or a permit sends
    // it again), and the last plot it walked on (the steward sells that one); where the homestead
    // courier took it from.
    Fusion::Construction::Tier constructionTier = Fusion::Construction::Tier::None;
    uint16 constructionSkill = 0;
    // The kits made at the trade window and not yet placed (Construction.h Kits).
    Fusion::Construction::Kits constructionKits;
    bool sentConstruction = false;
    uint32 plotHere = 0xFFFFFFFF;
    uint32 plotGeneration = 0;
    uint32 plotVisited = 0;
    // Furniture: resting at a homestead's bed (set by it), and the next check.
    bool furnitureResting = false;
    uint32 furnitureNextMs = 0;
    bool courierSaved = false;
    uint32 courierMap = 0;
    float courierX = 0.f, courierY = 0.f, courierZ = 0.f, courierO = 0.f;
    // Code locks (fusion/Locks.h) this player gave the right code at: piece id -> the lock's hash
    // then, so a changed code asks again. Kept for the session.
    std::unordered_map<uint32, std::string> codesKnown;
    // The Construction window (FusionConstruction.cpp, fusion/Homestead.h): the homestead NPC
    // that opened it (its buttons work while the player stays by it), and the claimed plots near
    // last sent for the world's markers (with where and at which plots' generation).
    ObjectGuid homesteadNpc;
    // The player the Plot Steward's permit lines were written for (their target then): a press
    // permits that one, whoever is targeted by the time it comes.
    uint32 homesteadPermitTarget = 0;
    uint8 homesteadRole = 0;
    std::vector<uint8_t> nearPlotsSent;
    uint32 nearPlotsMap = 0xFFFFFFFF;
    float nearPlotsX = 0.f, nearPlotsY = 0.f;
    uint32 nearPlotsGeneration = 0;

    // Riding (FusionRiding.cpp, fusion/Riding.h): a mount's glide, an updraft's burst of flight
    // and the glide down after it, timed by the server (its clock); the next look at the ground.
    Fusion::Riding::Flight flight;
    uint32 nextGroundLookMs = 0;
    bool wasJumping = false;                            // last update's jump flag, to see one begin
    bool flightCheat = false;                           // the burst's flight is ours to take back
    bool skyGauge = false;                              // the client was last told it rides a flying line
    // Warded airspace (fusion/Airspace.h): what the client was last told (op 73), and when.
    uint8 wardFlags = 0;
    float wardCeiling = 0.f;
    uint32 wardToldMs = 0;
    uint8 wardKind = 0;                                 // the airspace's Wards::Kind (0 none)

    // What the character's stats do with guns (FusionStats.cpp, fusion/Stats.h): worked out
    // twice a second, sent to the client on channel 6 when they change. The last hit taken
    // (server clock, 0 none) starts MW2's regeneration delay; the fraction of a point carried.
    Fusion::Stats::Derived combat;
    bool combatSent = false;
    uint32 nextCombatMs = 0;
    uint32 lastHurtMs = 0;
    float regenCarry = 0.f;
    // Item procs from gun hits: the pull last rolled (one roll a pull) and each proc's cooldown.
    uint32 procShotMs = 0;
    bool procShotRolled = false;
    std::unordered_map<uint32, uint32> procReadyMs;
    // Weapon pickups and enchanted rounds (FusionArmory.cpp, fusion/Pickups.h, fusion/Rounds.h):
    // the guns picked up this life (never saved), the last list of guns near that went out, the
    // enchanted rounds in a gun, the burns its incendiary rounds set, and the round recipes
    // learned (read from the characters DB the first time an enchanting trainer is opened).
    Fusion::Pickups::Hands pickups;
    // A ship's deck gun manned (FusionShips.cpp): its gun is held as a pickup, and the pickups
    // from before come back when the crewman leaves it.
    uint16 mountedGun = 0;
    Fusion::Pickups::Hands mountedSaved;
    uint32 droppedSig = 0;
    uint32 nextDroppedMs = 0;
    Fusion::Rounds::Loaded rounds;
    std::vector<Fusion::Rounds::Burn> burns;
    std::set<uint32> roundRecipes;
    bool roundRecipesLoaded = false;

    // Prestige, the professions' field gear and MW2 matches (FusionPrestige.cpp; fusion/Prestige.h,
    // FieldGear.h, Match.h): the prestige state (loaded with the achievements) and its pending
    // two-step question; the field recipes learned; what worn gear adds (a bandolier's reserve, a
    // ghillie's aggro multiplier); the stims taken; the arena match and the match HUD.
    Fusion::Prestige::State prestige;
    bool prestigeLoaded = false;
    Fusion::Prestige::Pending prestigeAsk;
    uint32 nextPrestigeMs = 0;
    uint32 nextMilestoneMs = 0;
    size_t gunsMaxedFor = size_t(-1);                   // earned.size() gunsMaxed was worked out for
    bool gunsMaxed = false;
    std::set<uint32> tradeRecipes;
    float reserveBonus = 0.f;
    float ghillie = 1.f;
    Fusion::FieldGear::Stims stims;
    bool inArena = false;
    uint32 arenaRespawnMs = 0;
    uint32 arenaMatchSeen = 0;
    uint32 nextMatchHudMs = 0;
    bool matchHudShown = false;

    // The lore killstreaks' craft out, and the rocket in flight (FusionSkyStreaks.cpp); the
    // wrecks of shot-down craft on their way to the ground (when, where: a burst there); who last
    // hurt this player and when (a craft turns on them first).
    std::vector<FusionSkyCraft> sky;
    FusionRocket rocket;
    uint32 nextRocketTicket = 1;
    std::vector<std::pair<uint32, Fusion::Vec3>> wrecks;
    uint64 lastHurtBy = 0;
    uint32 lastHurtByMs = 0;

    FusionPlayerData() { streak.SetLoadout(Fusion::DefaultStreakLadder(), false); }
};

class FusionMgr
{
public:
    static FusionMgr& Instance();

    // Read the config (Fusion.*) and the weapon table (<DataDir>/fusion/weapons.json).
    void Load();
    bool IsEnabled() const { return m_enabled; }

    Fusion::WeaponTable const& Weapons() const { return m_catalog.Weapons(); }
    Fusion::Catalog const& Catalog() const { return m_catalog; }
    Fusion::ParTable const& Par() const { return m_par; }

    // The packet handlers.
    void HandleShots(Player* shooter, Fusion::Packets::ShotBatch const& batch);
    void HandleState(Player* player, Fusion::Packets::State const& state);

    // Every player update: pose history, the FFA countdown and the field rearm.
    void UpdatePlayer(Player* player, uint32 diffMs);

    // The share of a creature's WoW aggro radius a player's stance and motion leave (design doc
    // section 15); 1 for anything but a fusion player.
    float AggroRadiusMultiplier(Creature const* creature, Unit const* target) const;

    // The share of the run speed the player's stance, sprint, aim and gun leave (design doc,
    // section 15); 1 when mounted or not a fusion player. Unit::UpdateSpeed applies it.
    float MoveSpeedMultiplier(Player const* player) const;

    // How much faster a creature runs at its player target, by its fusion profile and the
    // distance (design doc, section 19); 1 for pets and non-player targets.
    float CreatureChaseMultiplier(Creature const* creature) const;

    // Unit::DealDamage: a player (or their pet) damaged a unit; elites remember who, to split
    // their streak value (MW2 design spec 4.1).
    // `direct`: a blow (not damage over time), for the client's target-on-hit (NoteStruck).
    void OnDamage(Unit* attacker, Unit* victim, bool direct = false, bool fatal = false);
    // Unit::Kill: any death, by gunfire or anything else. Credits the streak: an enemy player 1,
    // a creature worth XP 0.1, an elite 1/N to each of its N damagers.
    void OnUnitKilled(Unit* killer, Unit* victim);
    void OnPlayerDeath(Player* victim);
    // The damage a fusion player takes, after its perks (called by Unit::DealDamage).
    uint32 AdjustDamageTaken(Unit* attacker, Player* victim, uint32 damage);
    // A blast at `at` owned by `owner`: every hostile body within `radius` in sight of it takes
    // MW2's splash, `inner` at the centre to `outer` at the edge (Danger Close counts).
    void Explode(Player* owner, Fusion::Vec3 at, float radius, float inner, float outer, uint16 weaponId);
    // Equipment and killstreaks through the shot message (the shot's weapon is one of them).
    void UseEquipment(Player* player, Fusion::WeaponProfile const& w, Fusion::Packets::Shot const& shot);
    void CallIn(Player* player, Fusion::WeaponProfile const& w, Fusion::Packets::Shot const& shot);
    // The lore killstreaks (FusionSkyStreaks.cpp): call one in (false: the world refused it and
    // the reward is kept), fly and fire what is out, the rocket's reports, a GM's grant.
    bool CallSkyStreak(Player* player, uint32 streakId, Fusion::Vec3 origin, Fusion::Vec3 dir);
    void UpdateSkyStreaks(Player* player, uint32 now);
    void HandleRocketReport(Player* player, std::vector<uint8> const& body);
    void EndRocket(Player* player, bool tellClient);
    void DismissSkyCraft(Player* player);
    // Hand a reward over and tell the client it is ready (a GM's `.streak`, a refused call-in).
    bool GrantStreak(Player* player, uint32 streakId);
    // The craft as a creature (the caller's faction, owned by the caller so every attack check
    // takes the caller's PvP rules; the rocket unselectable and immune), its targets near
    // `around` (fusion/SkyStreaks.h Target, with the units by guid), and one round of its gun
    // (true: it killed).
    Creature* SummonCraft(Player* player, Fusion::Sky::Craft craft, Fusion::Vec3 at, float facing, uint32 lifeMs);
    std::vector<Fusion::Sky::Target> SkyTargets(Player* player, Creature* craft, FusionSkyCraft const& k,
        Fusion::Vec3 around, float radius, std::unordered_map<uint64, Unit*>& byGuid) const;
    bool SkyRound(Player* player, Creature* craft, Unit* target, Fusion::Vec3 gun, Fusion::WeaponProfile const& w,
        float hitChance);
    // A craft shot at (who, for its targets) and shot down (the burst, the wreck, the news to the
    // owner and the shooter, the shooter's stat); a kill a craft made, for the owner's feed.
    void OnCraftHurt(Unit* attacker, Creature* craft);
    void OnCraftKilled(Unit* killer, Creature* craft);
    void NoteCraftKill(Player* owner, uint32 streakId, Unit* victim);
    // The owner's HUD line for a craft (op 39), its sounds for the players near, and its flight
    // away when its work is done.
    void SendCraftUp(Player* owner, Creature* c, FusionSkyCraft& k, uint32 now);
    void CraftSounds(Map* map, Creature* c, FusionSkyCraft& k, uint32 now);
    void LeaveCraft(Map* map, Creature* c, FusionSkyCraft& k, uint32 now);
    void UpdateCharges(Player* player, uint32 now);
    void ApplyTactical(Player* owner, FusionCharge const& c);
    void ThrowKnife(Player* player, Fusion::WeaponProfile const& w, Fusion::Vec3 origin, Fusion::Vec3 dir);
    // Where a ray from the eye first meets the world (or its end).
    Fusion::Vec3 AimPoint(Player* player, Fusion::Vec3 origin, Fusion::Vec3 dir, float reach) const;
    // The nearest hostile body to `around` within `radius` the sky over `from` can see.
    Unit* NearestTarget(Player* owner, Fusion::Vec3 around, float radius, Fusion::Vec3 from) const;
    // Noise: hostile creatures within `radius` of `at` come for the player (gunfire, a blast):
    // those in sight at once, those behind a wall within half of it after a beat (they
    // investigate first), a few a shot and a few a while (Gunfights.h WhoHearsTheShot).
    void Alert(Player* player, Fusion::Vec3 at, float radius);
    // Those that heard it behind a wall, their beat over: they come if they still can.
    void UpdateHeardAlerts(Player* player, uint32 now);
    // Whether a creature can be drawn by noise or a call to `player` (alive, hostile, out of a
    // fight, not evading, not passive, its own master).
    bool CanBeDrawn(Creature* c, Player* player) const;
    // A creature shot calls its own (its faction) within kGunfight.callForHelpRadius in sight of
    // it, the nearest few, at most once every callForHelpEveryMs.
    void RallyAllies(Creature* c, Unit* attacker);
    // Where a creature in a fight stands, kept a second (a round is checked where it was seen).
    void RecordCreaturePose(Creature* c, uint32 now);
    // An NPC with a gun: classifies it once (NpcGuns.h), arms its model, keeps it off its target
    // and fires bursts at a player in sight.
    void UpdateCreatureGun(Creature* creature, uint32 diffMs);
    // A player's round passed near a creature (it ducks for cover).
    static void ShotNear(Unit* unit, uint32 now);
    // One round of a wanderer's gun (FusionWanderers.cpp) at any unit: the NPC gunners' aim cone,
    // a player hit as by an NPC gunner of its level, a creature as by a player of its level (the
    // level's gun multiplier on the creature's CoD pool), cover taking it, the tracer told to
    // everyone near (op 32) and the hit dealt. True if it hit.
    bool WandererRound(Creature* shooter, Unit* target, Fusion::WeaponProfile const& w, uint32 aimHeldMs);
    // A melee creature whose way to its target a built piece blocks stops and breaks it down.
    void UpdateCreatureSiege(Creature* creature, uint32 diffMs);

    // ── Building (fusion/Build.h) ──
    void HandleChannel(Player* player, uint8 channel, std::vector<uint8> const& body);
    void SendChannel(Player* player, uint8 channel, std::vector<uint8_t> const& body) const;
    // The nearest built piece a ray meets on the map (`t` its distance), 0 none.
    // The nearest piece the ray meets within maxDistance (0: none); `ignoreOwner` skips one
    // builder's own pieces.
    uint32 PieceOnRay(uint32 mapId, Fusion::Vec3 origin, Fusion::Vec3 dir, float maxDistance, float& t,
        uint32 ignoreOwner = 0) const;
    // A creature blocked by a built piece looks for a way around it: a point to one side that it
    // can reach and see its victim from with no piece between. True: it is walking there.
    bool TryDetour(Creature* c, Fusion::Vec3 from, Fusion::Vec3 to, uint32 piece);
    static constexpr float kDetourStep = 6.f;       // yards to the side
    static constexpr uint8 kDetourTries = 2;        // per piece, then it breaks through
    // Damage a piece (CoD damage; a blast's scales with the material); it may break and take
    // what it held up with it. `by` is who did it: a piece on a plot only breaks for those the
    // plot lets (Plots::CanDamage).
    void DamagePiece(Map* map, uint32 id, float damage, bool blast, Fusion::Plots::Actor const& by);
    // A blast's damage to the pieces about it.
    void BlastPieces(Map* map, Fusion::Vec3 at, Fusion::WeaponProfile const& blast, Fusion::Plots::Actor const& by);

    // ── A hand spell's bolt in flight (FusionSpellcasting.cpp, fusion/Spellcasting.h) ──
    // Where the player's eye is: their rounds and bolts leave from it.
    Fusion::Vec3 EyeOf(Player const* player) const;
    // Where a segment a -> b first meets the world: a built piece (its id in `piece`, else 0), a
    // building, a solid doodad, a closed door or the terrain; its distance from a, or < 0 for none.
    // It is the walking body's collision: the vmaps hold only the models' collision hulls (a bush or
    // a canopy has none, so a bolt flies through it as a player walks through it) and the WMO faces
    // that collide; the game objects are those with a collision model.
    float BoltMeetsWorld(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b, uint32& piece) const;
    // Whether a splash or a spray at `a` reaches a body at `b`: nothing a bolt would strike between.
    bool BoltSees(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b) const;
    // The bodies a player's bolt may strike within `reach` of `around`: living units it may
    // attack (PvP's rules too) with their hitboxes; `units` by the same index.
    void BoltBodies(Player* caster, Fusion::Vec3 around, float reach, std::vector<Fusion::Spellcasting::Body>& bodies,
        std::vector<Unit*>& units) const;
    // A spell's direct damage to built pieces at an impact: the piece struck (0 none) and those
    // within `radius` of `at`.
    void SpellStrikesPieces(Player* owner, SpellEntry const* info, Fusion::Vec3 at, uint32 piece, float radius);
    // A channel body to each fusion player within `radius` of `at` but `skip`.
    void TellNearOn(Map* map, uint8 channel, Fusion::Vec3 at, std::vector<uint8_t> const& body, Player const* skip,
        float radius = Fusion::Launch::kTellRadius);
    // The player's bedroll: back to life there instead of a graveyard (true if so).
    bool RespawnAtBedroll(Player* player);
    // Whether the player's deathstreak is `id` and its deaths are reached.
    bool DeathstreakReady(FusionPlayerData const& d, uint16 id) const;

    // Create-a-Class: the saved classes at login; a request to save and equip; the reply.
    void LoadClasses(Player* player, std::unique_ptr<QueryResult> classes, std::unique_ptr<QueryResult> fusion);
    void HandleLoadout(Player* player, Fusion::Packets::LoadoutRequest const& request);
    void SendLoadout(Player* player, Fusion::Packets::LoadoutStatus status,
        Fusion::LoadoutError error = Fusion::LoadoutError::None, uint8 errorSlot = Fusion::kNoClass) const;

    void SendEvents(Player* player, Fusion::Packets::Events const& events) const;

    // A game master (security >= Fusion.Admin.MinSecurity) passes every unlock gate.
    bool IsAdmin(Player const* player) const;

    // A unit's perks if it is a fusion player (its class's), else none: the classic world's perk
    // effects ask this (loot, casting, falls, experience, stealth, calls for help).
    Fusion::PerkFlags const* PerksOf(Unit const* unit) const;
    // A fusion player's glyph effects (FusionCrafts.cpp), else none.
    Fusion::Crafts::Mods const* AugmentsOf(Unit const* unit) const;

    // Achievements (FusionAchievements.cpp): load at login; count events; earn, save, announce.
    void LoadAchievements(Player* player);
    void CountStat(Player* player, Fusion::StatKey key, uint32 n = 1);
    void RecordStat(Player* player, Fusion::StatKey key, uint32 value);
    void CountKill(Player* killer, Unit* victim);
    void CountDeath(Player* victim, Player* killer);
    // A profession's skill (stock lines only; the layer's record their own), and something made
    // at a stock trade (Player::UpdateCraftSkill).
    void NoteSkill(Player* player, uint16 skillLine, uint16 value);
    void CountCraft(Player* player, uint32 skillLine);
    // A world event's quest done (Player::RewardQuest): the holidays celebrated with it.
    void CountHolidayQuest(Player* player, uint16 eventId);
    // The standing with a faction changed (ReputationMgr): its rank, and how many are Exalted.
    void NoteReputation(Player* player, uint32 factionId, uint8 rank, uint32 exalted);
    void EvaluateAchievements(Player* player);
    void SendAchievementPages(Player* player);
    void SendUnlocks(Player* player);
    void HandleAchievementChannel(Player* player, std::vector<uint8> const& body);
    void GrantAchievement(Player* player, uint32 id);
    void ResetAchievements(Player* player);
    Fusion::AchievementBook const& Book() const { return m_book; }
    // The hit being applied now (set around FusionMgr::Hit).
    static FusionHitSource& CurrentHit();

    // ── Battle companions (FusionCompanions.cpp, fusion/Companions.h, docs/COMPANIONS.md) ──
    // The species (world DB) and every companion item's saved progress (characters DB).
    void LoadCompanions();
    // Swaps the retired vanilla pet items for their companion items (characters DB, at startup).
    static void SwapRetiredPetItems();
    std::vector<Fusion::Companions::CompanionSpecies> const& CompanionSpeciesList() const { return m_companionSpecies; }
    // A companion item used from the bags (WorldSession::HandleUseItemOpcode, or the companion
    // channel's summon): summon it, or dismiss it if it is the one out.
    // Write a companion item's species, stage and level into its suffix factor
    // (Companions::ItemSeed), which its tooltip reads in bags, trades, mail and the auction house.
    void StampCompanionItem(Item* item);
    void UseCompanionItem(Player* player, Item* item);
    void HandleCompanionChannel(Player* player, std::vector<uint8> const& body);
    // Every player update: follow the companion out (gone, dead, item gone, owner levelled,
    // a new place for the evolve button) and drive its duel.
    void UpdateCompanion(Player* player, uint32 now);
    // FusionMgr::OnDamage and OnUnitKilled: a companion's part in a kill (its xp), the owner's
    // fight it joins, and the companion-duel interference rule.
    void OnCompanionDamage(Unit* attacker, Unit* victim);
    void OnCompanionKill(Unit* killer, Unit* victim);
    // Creature::GenerateLootForBody: the rare companion item and evolution catalyst rolls.
    void ScavengeCompanionGold(Creature* creature, Loot& loot);
    void AddCompanionLoot(Creature* creature, Loot& loot);
    // Unit::DealDamage: a hit that would kill a companion in a companion duel leaves it at 1
    // health and loses the duel instead.
    uint32 CompanionDuelDamage(Unit* victim, uint32 damage);
    // WorldObject::GetReactionTo / IsValidAttackTarget: two companions fighting a companion duel
    // are enemies, and only to each other.
    bool CompanionsDuelling(Unit const* a, Unit const* b) const;
    // Item::DeleteAllFromDB: a destroyed companion item takes its progress with it.
    void ForgetCompanion(uint32 itemGuidLow);
    // `.fusion pet` (admins): a companion item of a species at a level (and stage) in the bags.
    Item* GiveCompanion(Player* player, uint16 species, uint8 level, uint8 stage);
    // `.fusion petduel` and the companion bar: challenge, answer, give up.
    void ChallengeCompanionDuel(Player* player, ObjectGuid target);
    void AnswerCompanionDuel(Player* player, bool accept);
    void ForfeitCompanionDuel(Player* player);
    // WorldSession::HandlePetAction / HandlePetSpellAutocastOpcode: a companion's pet bar is
    // WOMW's (fusion/CompanionMoves.h). True when it was handled.
    bool CompanionPetAction(Player* player, Pet* pet, uint8 type, uint32 action, ObjectGuid target);
    bool CompanionAutocast(Player* player, Pet* pet, uint32 spell, bool on);
    // ── The Companions window (FusionCompanionIndex.cpp, fusion/CompanionIndex.h) ──
    // Login: the species this character has seen or caught.
    void LoadCompanionIndex(Player* player);
    // WorldSession::HandleSetSelectionOpcode: targeting a creature of a companion species marks
    // it seen.
    void OnCompanionTarget(Player* player, Unit* target);
    // A stable master spoken to (gossip hello or the stable list): the Companions window opens
    // with its full view instead of the stock stable or gossip. True when handled.
    bool CompanionStableHello(Player* player, Creature* creature);
    // A companion ride spell's mount aura no ride accounts for (Companions::StrayRideAura).
    void TakeOffStrayRideAuras(Player* player);
    // Aura::HandleAuraMounted: the display a player riding their companion shows (0: the
    // spell's own).
    uint32 CompanionMountDisplay(Player* player, uint32 spellId) const;
    // Fainting (fusion/CompanionRest.h): WorldSession::LogoutPlayer, before the pet goes: the
    // companion out goes back into its item as hurt as it is (fainted, if it was beaten), saved.
    void CompanionLogout(Player* player);
    // A Reviving Draught used from the bags (WorldSession::HandleUseItemOpcode, or the companion
    // channel): it revives the companion that fainted last.
    void UseReviveDraughtItem(Player* player, Item* draught);
    // A stable master's gossip: revive and heal the companions, buy a Reviving Draught, or the
    // Companions window. True when it was one of these.
    bool CompanionStableSelect(Player* player, Creature* creature, uint32 sender, uint32 action);
    // `.fusion pet`: a companion with chosen traits and generation as well.
    Item* GiveCompanion(Player* player, uint16 species, uint8 level, uint8 stage, uint32 traits,
        uint8 twinStage, uint8 generation);
    // `.fusion petbreed`: breed the first two eligible adults in the bags, no stable master
    // needed (admins test); with `free` no reagents, fee or rest either.
    void BreedFirstPair(Player* player, bool free);

    // ── Companion duels (FusionCompanionBattles.cpp, fusion/CompanionBattles.h,
    // design/COMPANION_BATTLES.md): turn-based card duels after Slay the Spire, each companion
    // playing its own deck, against tamers, wanderers and players, on fusion channel 13. ──
    // Startup: the tamers (world table fusion_companion_tamer) and the daily table.
    void LoadCompanionBattles();
    void HandleCompanionBattleChannel(Player* player, std::vector<uint8> const& body);
    // Every player update: the battle's clock, its NPC's choices, its end.
    void UpdateCompanionBattle(Player* player, uint32 now);
    // A tamer's or a Companion Battle Trainer's gossip (script npc_fusion_companion_tamer).
    bool CompanionTamerHello(Player* player, Creature* creature);
    bool CompanionTamerSelect(Player* player, Creature* creature, uint32 sender, uint32 action);
    // A wanderer who battles companions, asked (WandererMgr::GossipSelect).
    bool StartWandererBattle(Player* player, Creature* c, std::string const& name, uint8 level, uint32 seed);
    // A player challenges another (channel op 10, `.fusion petbattle`).
    void ChallengeCompanionBattle(Player* player, ObjectGuid target);
    void AnswerCompanionBattle(Player* player, bool accept);
    bool InCompanionBattle(Player const* player) const;
    // WorldSession::LogoutPlayer: a battle in progress is forfeited.
    void CompanionBattleLogout(Player* player);
    // A companion's duel deck: the deck window (op 4) for an item (0: the companion out), and
    // a deck saved from it (op 15), checked against what the companion knows.
    void SendCompanionDeck(Player* player, uint64 itemGuid, std::string const& note = "");
    void SaveCompanionDeck(Player* player, uint64 itemGuid, std::vector<uint16> const& cards);

    // Pro perks: a GM's by default; `.fusion pro on|off` (admins) sets them either way until
    // logout. `mode` -1 back to the default, 0 off, 1 on. Tells the client (world op 21).
    bool ProPerks(Player const* player) const;
    void SetProPerks(Player* player, int8 mode);
    // `.fusion clear [radius]`: take down every built piece within `radius` yards of the
    // player (a GM clearing a test site). Returns how many went.
    uint32 ClearPiecesNear(Player* player, float radius);

    // ── The CoD scale (fusion/Scale.h) ──
    // A creature's pool by level, rank and place (Creature::InitStatsForLevel).
    uint32 CreatureMaxHealth(Creature const* creature) const;
    // A player's outgoing damage multiplier M (level and gear).
    float DamageMultiplier(Player const* player) const;
    // What a WoW amount (a heal, regeneration, a player's spell) is worth on the unit's pool: its
    // CoD pool over the one WoW would give it; 1 for pets and anything unscaled.
    float HealScale(Unit const* unit) const;
    // Unit::DealDamage: WoW damage to the CoD scale. A creature's melee and spells on a player
    // by its level and rank; anything else by the victim's pool. The fusion's own hits pass.
    uint32 ConvertDamage(Unit* attacker, Unit* victim, uint32 damage, uint32 damageType, bool spell, uint32 schoolMask) const;
    // Deal fusion damage (already on the CoD scale).
    void Hit(Unit* from, Unit* to, uint32 damage, FusionHitSource const& source = {});

    // ── What stats do with guns (FusionStats.cpp, fusion/Stats.h) ──
    // The rules' input from the character as it stands (gear, buffs, level, perks).
    Fusion::Stats::Input StatsInputOf(Player const* player) const;
    // Every player update: work the table out again (twice a second) and tell the client when it
    // changed; MW2's regeneration once the delay since the last hit taken has passed.
    void UpdateCombatStats(Player* player, uint32 now, uint32 diffMs);
    // OnDamage: a fusion player took damage (the regeneration waits again).
    void NoteHurt(Unit* victim);
    // A gun's (and equipment's) damage multiplier: the level's, never gear's.
    float GunMultiplier(Player const* player) const;
    // The share of a hit `target` armor takes, after the round's FMJ and a special round's
    // armor ignore (AmmoAgainst).
    float BulletCutOn(Player const* target, Fusion::HitInput const& in, float armorIgnore) const;
    // Whether a body shot of the shooter's counts as a weak-spot hit on the target.
    bool WeakSpotRoll(Player const* shooter, Unit const* target) const;
    // Item procs from a gun hit, once a pull (after the hit is dealt).
    void GunProcs(Player* shooter, Unit* target, Fusion::WeaponProfile const& w, uint32 shotTimeMs);
    // Intellect's equipment strength: 0 to 0.4, on flash and stun duration, smoke and Claymore.
    float EquipmentStrength(Player const* player) const;
    // A flash or stun's aura on `u`, lengthened by the owner's equipment strength.
    void StrengthenTactical(Player* owner, Unit* u, uint32 spell) const;
    // `.fusion stats`.
    std::string CombatStatsReport(Player* player);
    // ── A more dangerous world and open-world construction (FusionWorld.cpp) ──
    // The danger table's multipliers on a creature's hits (fusion/Danger.h): its melee and spells,
    // or (`gun`) its gun's rounds and grenades.
    float CreatureDangerDamage(Creature const* creature, bool gun) const;
    // What the gunner rules know of a creature: its type, faction, name, boss or not.
    Fusion::GunFacts GunFactsOf(Creature const* creature) const;
    // Spell::_handle_immediate_phase: a player's (or their pet's) damage spell hits the built
    // pieces about where it lands, and the piece between caster and target.
    void SpellHitsPieces(Spell* spell);
    // Damage a piece by kind (fusion/Build.h DamageTaken); the other DamagePiece is this with
    // Bullet or Blast. Whoever breaks a piece salvages part of it.
    void DamagePiece(Map* map, uint32 id, float damage, Fusion::Build::DamageKind kind, Fusion::Plots::Actor const& by);
    // A piece broken by a player: a share of its materials to them, or shared out in their group.
    void SalvagePiece(Map* map, Fusion::Build::Piece const& piece, Fusion::Plots::Actor const& by);
    // The owner puts a piece up a reinforcement tier (world channel op 50).
    void ReinforcePiece(Player* player, uint32 id);
    // The Sentry Gun killstreak: a sentry piece in front of the player for 90 s.
    void DeployStreakSentry(Player* player);
    // Take away the killstreak sentries on the map whose time is up.
    void ExpireStreakSentries(Map* map, uint32 now);
    bool IsStreakSentry(uint32 pieceId) const;
    // ── The downed state (FusionDowned.cpp, fusion/Downed.h) ──
    // Unit::DealDamage: a hit on a player that would kill goes down instead; a downed player's
    // hits drain the finisher pool. Returns the damage the game should still deal.
    uint32 DownedDamage(Unit* attacker, Player* victim, uint32 damage);
    bool IsDowned(Unit const* unit) const;
    // Every player update: the bleed-out, Final Stand, creatures turning away, a kit at work.
    void UpdateDowned(Player* player, uint32 now);
    // A resurrection spell (or Goblin Jumper Cables) landing on a downed player brings them up;
    // false when the target is not down (the spell carries on as vanilla's).
    bool ReviveBySpell(WorldObject* caster, Unit* target);
    // WorldSession::HandleUseItemOpcode: a revival kit or manual (true: handled), and nothing
    // else for a downed player.
    bool UseItemDowned(Player* player, Item* item);
    void HandleDownedChannel(Player* player, std::vector<uint8> const& body);
    // The action system the client switched to (fusion/ActionSystems.h): Utility puts the
    // weapons away, its swings bare-handed.
    void SetActionSystem(Player* player, Fusion::ActionSystems::System system);
    // A pose or hand act the client told (ActionSystems.h ops 5, 6), relayed to those near.
    void RelayHands(Player* player, std::vector<uint8> const& body);

    // ── Utility's tools (FusionTools.cpp; fusion/Gathering.h, Maintenance.h) ──
    // The tool the client readied in Utility (0 bare hands): the hands show it (the melee profile).
    void SetUtilityTool(Player* player, uint32 tool);
    // A strike of the readied tool at its target: counted, and the last one gathers (WoW's own
    // gathering spell, cast for the player), casts or reels the line, opens the smith's trade at
    // an anvil, or mends a built piece a step.
    void ToolStrike(Player* player, Fusion::ActionSystems::StrikeRequest const& r);
    // The item entry of the readied tool the player carries (in the bags or in hand), 0 for none.
    uint32 ToolItemOf(Player const* player, uint32 tool) const;
    // What the player brings to a tool: its item carried, its trade learned, a GM's pass.
    Fusion::Gathering::Holder ToolHolder(Player const* player, Fusion::Gathering::ToolDef const& tool) const;
    // Utility's bar: the tools the player has and is trained for (Gathering.h OnUtilityBar), told
    // the client (op 9) at login and within a second of a change (a tool looted, sold or
    // destroyed, a trade learned or unlearned).
    void UpdateUtilityTools(Player* player, uint32 now);
    // Decay (fusion/Maintenance.h): bring a map's pieces up to now, collapse what has nothing left,
    // and tell those who see them. Once an hour a map, by a player's update there.
    void DecayPieces(Map* map);
    // The time a piece's plot is paid to (0 in the open world, or on an unowned plot).
    int64 KeptUntil(Fusion::Build::Piece const& piece);
    // GM: the pieces within 60 yards of the player age `days`, then this map's decay is worked out.
    uint32 AgePiecesNear(Player* player, int32 days);
    // The main- and off-hand entries the client's melee profile names now (0 with the weapons away).
    std::pair<uint32, uint32> MeleeEntries(Player const* player) const;
    // GM commands: go down now, get up, bleed out now.
    void ForceDown(Player* player);
    void ForceUp(Player* player, Player* by);
    void ForceBleedOut(Player* player);
    // The class options (FusionClassAbilities.cpp): a downed player stands up at `healthShare`
    // of their health, brought up by `by` (a revive, Reincarnation, a soulstone); false when not
    // down. More bleed-out time (Rallying Cry).
    bool ReviveDownedBy(Player* player, ObjectGuid by, float healthShare);
    void ExtendDowned(Player* player, uint32 ms);
    // A player goes down now, as a fatal blow would put them (Spirit of Redemption's end).
    void GoDownNow(Player* player);

private:
    void GoDown(Player* player, Unit* by);
    void ReviveDowned(Player* player, Fusion::Downed::UpReason reason, ObjectGuid by, float healthShare);
    // A death while down (bled out or finished): the state goes, the client is told.
    void EndDowned(Player* player);
    void BleedOut(Player* player);
    void UpdateDownedSpeed(Player* player);
    void TurnCreaturesAway(Player* player, uint32 now);
    void SendDownedState(Player* player, uint32 now);
    void TellDownedNear(Player* player, bool downed);
    void StartKit(Player* player, Item* item, ObjectGuid target);
    void UpdateKitUse(Player* player, uint32 now);
    void StopKit(Player* player, Fusion::Downed::KitState state);
    void CraftFromManual(Player* player, uint32 product);
    Item* FindKit(Player* player) const;
    uint16 LastStandPistolOf(Player const* player) const;
    void SendDowned(Player* player, std::vector<uint8_t> const& body) const;

private:
    // MW2 movement requests (design spec 5.4-5.5): climb the ledge ahead, or lunge with the knife
    // at the enemy in front. Each checks and moves the player itself; a refused one does nothing.
    void TryMantle(Player* player);
    void TryLunge(Player* player);

    // Melee by the main hand (fusion/Melee.h): its profile (with the weapon's DPS, item and look),
    // a hit's CoD-scale damage on a target, a plain swing, and telling the client the profile.
    Fusion::Melee::Profile MeleeOf(Player const* player, float* dps = nullptr, bool* armed = nullptr,
        uint32* entry = nullptr, uint32* display = nullptr) const;
    uint32 MeleeDamage(Player* player, Unit* target) const;
    void Strike(Player* player, Fusion::Packets::Shot const& shot);
    void SendMeleeProfile(Player* player);
    // Whether the player may strike (or shoot) `u` under the PvP rules.
    bool PvpAllows(Player* player, Unit* u) const;
    // A round of `shooter`'s passes through `u`: its own or its group's (Fusion::ShotPassesThrough).
    bool ShotPassesThrough(Player* shooter, Unit* u) const;
    // Land a melee hit and tell both sides.
    void MeleeHit(Player* player, Unit* target, uint32 damage);
    // Tell the player it struck `target` with WoW's damage (EventKind::Struck), debounced.
    void NoteStruck(Player* player, Unit* target, bool fatal);

    // The player's body for the movement and fire gates (stun, root, crowd control, mount...).
    static Fusion::ActorState ActorOf(Player const* player);
    // Resync the client's magazine and reserve for a gun (after a rearm or a gun change).
    void SendAmmo(Player* player, Fusion::WeaponProfile const& w);
    // The reload factor (Sleight of Hand, ranged attack power) the client may be reloading with:
    // the quickest of the last few seconds (fusion/Reload.h).
    float ReloadFactorOf(Player* player, uint32 now);
    // Fill what of `w`'s asked-for reloads is due (fusion/Reload.h); true when anything landed.
    bool SettleReload(Player* player, Fusion::WeaponProfile const& w, uint32 now);

    // Equip a saved class now, or wait for out of combat or the respawn.
    void RequestEquip(Player* player, uint8 slot);
    void ApplyEquip(Player* player, uint8 slot);
    // Put the equipped class's perks in play (MW2 design spec 3.3).
    void ApplyPerks(Player* player);
    // A gun as the player holds it: with the applied class's attachments when the class carries
    // it (MW2's numbers for the combination), else the plain gun; nullopt for an unknown id.
    std::optional<Fusion::WeaponProfile> EffectiveWeapon(Player const* player, uint16 id) const;

    // Add a kill's streak value to a player's streak and tell them of any reward it earned.
    // A kill credited to `player`: `tenths` toward the streak (0 for a creature, which still
    // counts for Scavenger and ends a deathstreak).
    void AwardStreak(Player* player, uint32 tenths);
public:
    // A player talks to a creature: the fusion's own NPCs answer here (true: handled).
    bool OnGossipHello(Player* player, Creature* creature);
    // A trainer's or innkeeper's prepared gossip gains "Edit your kits" (a kit station, Kits.h);
    // talking to one also tops up the ammunition (WOMW design, section 9).
    void AddKitGossip(Player* player, Creature* creature);
    // A kit station's "Rearm": ammunition and equipment refilled, special rounds bought when the
    // character can pay (fusion/Kits.h).
    void RearmAtStation(Player* player);
    // A trainer the client asked for its list straight away (no gossip flag): a short menu of
    // training and the kits instead, so "Edit your kits" is there too (true: sent).
    bool TrainerKitGossip(Player* player, Creature* creature);
    // "Edit your kits" (or the short menu's training) chosen (true: handled): the gossip closes
    // and the kit window opens to edit.
    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action);
    // Whether a kit station (a living, friendly trainer or innkeeper) stands within reach.
    bool AtKitStation(Player* player) const;
    // What the player may do with kits where they stand.
    Fusion::KitAccess KitAccessNow(Player* player) const;
    // The field editor asked for from the kit window (build channel op 31): opens the window to
    // edit anywhere for a short while, unless cooling down.
    void UseFieldEditor(Player* player);
    // The old Field Quartermaster's Ledger used from the bags (op 30): the field editor.
    void UseLedger(Player* player, ObjectGuid item);
    // The kit window's status line (world op 42): where kits change, the field editor's clock.
    void SendKitStatus(Player* player);
private:
    // Every gun's magazine and reserve full, and the equipment back (a trainer or innkeeper, a new life).
    void RefillAmmo(Player* player);
    // Limited ammunition (fusion/Ammo.h): equipment back (free), a fresh life's resupply, rounds
    // bought at a station (returns the result line), rounds taken from a body.
    void RefillEquipment(Player* player);
    void RespawnAmmo(Player* player);
    void ScavengeRounds(Player* player, float mags);
    // Tell the player their streak's progress (tenths), for the HUD ladder.
    void SendStreakProgress(Player* player) const;

    // The health a hit takes from the target, through MW2's damage and the game's health scale.
    float HealthDamage(Player* shooter, Unit* target, Fusion::HitInput const& in) const;

    bool m_enabled = false;
    // Building.
    void LoadPieces();
    void UpdateBuild(Player* player, uint32 now);
    void UpdateSentries(Player* player, uint32 now);
    void UpdateRaid(Player* player, uint32 now, uint32 piecesNear, Fusion::Vec3 base);
    void SpawnRaid(Player* player);
    void SendWorld(Player* player, std::vector<uint8_t> const& body) const;
    // Hostile humanoid kinds by level, for raids (made once).
    std::vector<uint32> const& RaidKinds(uint32 level);
    std::unordered_map<uint32, std::vector<uint32>> m_raidKinds;
    bool m_raidKindsMade = false;
    std::mutex m_raidKindsLock;
    void BuildPlace(Player* player, Fusion::Build::PlaceRequest const& request);
    void BuildRemove(Player* player, uint32 id);
    void SendBuildStatus(Player* player);
    // Remove pieces (and what falls with them), tell the players near.
    // Takes a piece out of the world and whatever it alone held up; returns how many fell with it.
    uint32 RemovePieces(Map* map, uint32 id, Fusion::Build::RemoveCause cause);
    // Send each player near `at` on the map a body made for them.
    void Broadcast(Map* map, Fusion::Vec3 at, std::function<std::vector<uint8_t>(Player*)> const& body);
    // The same on the world channel, to fusion players within Fusion::Launch::kTellRadius.
    void TellNear(Map* map, Fusion::Vec3 at, std::function<std::vector<uint8_t>(Player*)> const& body,
        float radius = Fusion::Launch::kTellRadius);
    // The NPC gunners near a player and their guns (op 33).
    void SendNpcGuns(Player* player);
    // MW2-style soldiering (NpcGuns.h): carry out a gunner's act, test candidate spots, find its
    // squad, lob a frag, and set an NPC frag off.
    void GunnerDo(Creature* c, Player* target, FusionGunnerState& g, Fusion::GunnerAct act, float holdRange, uint32 now);
    std::vector<Fusion::GunnerSpot> TestSpots(Creature* c, Player* target, std::vector<Fusion::Vec3> const& spots) const;
    std::vector<Creature*> SquadOf(Creature* c) const;
    void GunnerMoveTo(Creature* c, Fusion::Vec3 spot);
    void GunnerGrenade(Creature* c, Player* target, FusionGunnerState& g, uint32 now);
    void NpcExplode(Player* holder, FusionCharge const& charge);
    // Where a segment a -> b first meets a built piece, a building or the terrain (its distance
    // from a), or none.
    std::optional<float> WorldOnSegment(Map const* map, Fusion::Vec3 a, Fusion::Vec3 b) const;
    // A frag's bouncing flight against the same world (Bounce.h): the ground's normal from the
    // heights, a wall's from probes along WorldOnSegment.
    Fusion::Bounce::Path FlyFrag(Map const* map, Fusion::Vec3 from, Fusion::Vec3 velocity, float gravity, float fuseS) const;
    // Whether a built piece stands between two points.
    bool PieceBetween(uint32 mapId, Fusion::Vec3 a, Fusion::Vec3 b, uint32 ignoreOwner = 0) const;
    // The pieces of a map near a point (copies), and the ground there.
    std::vector<Fusion::Build::Piece> PiecesNear(uint32 mapId, Fusion::Vec3 at, float radius) const;
    mutable std::mutex m_buildLock;
    std::unordered_map<uint32, std::vector<Fusion::Build::Piece>> m_pieces;   // by map
    uint32 m_nextPieceId = 1;
    bool m_buildFree = false;
    uint32 m_adminMinSecurity = SEC_GAMEMASTER;
    bool m_pvpLowLevelProtection = true;
    Fusion::Coefficients m_coefficients;
    Fusion::Catalog m_catalog;
    Fusion::AchievementBook m_book;
    Fusion::ParTable m_par;
    // Elites' damagers. Map updates run on several threads, so it is locked.
    Fusion::DamageCredit m_credit;
    std::mutex m_creditLock;
    uint32 m_creditExpiredMs = 0;

    // Battle companions (FusionCompanions.cpp).
    Fusion::Companions::CompanionState CompanionStateOf(uint32 itemGuidLow, uint32 itemEntry) const;
    void SaveCompanionState(uint32 itemGuidLow, Fusion::Companions::CompanionState const& state);
    Fusion::Companions::CompanionSpecies const* CompanionSpeciesOfItem(uint32 itemEntry) const;
    Pet* CompanionPet(Player* player) const;
    Player* CompanionOwner(Unit* unit) const;
    bool SummonCompanion(Player* player, Item* item);
    // Back into its item: `cooldownMs` it was beaten (no summoning until then), `tell` the
    // client's bar, `comeBack` it returns once its owner is up again (the owner died).
    // `beaten`: it was killed (or put away dead): it faints.
    void DismissCompanion(Player* player, bool beaten = false, bool tell = true, bool comeBack = false);
    // Fainting and reviving (FusionCompanions.cpp, fusion/CompanionRest.h): each companion item's
    // wounds and faint, by item guid, saved in characters.character_fusion_companion_rest.
    Fusion::Companions::Recovery CompanionRecoveryOf(uint32 itemGuidLow) const;
    void SaveCompanionRecovery(uint32 itemGuidLow, Fusion::Companions::Recovery const& r);
    // The wait before this item's companion comes round on its own (Loyal: a third).
    uint32 CompanionFaintWait(Item* item) const;
    // Its record with an over wait settled (and saved); `tell` says so to its owner.
    Fusion::Companions::Recovery SettleCompanion(Player* player, Item* item, bool tell);
    // Seconds until it comes round; 0 when it has not fainted.
    uint32 CompanionFaintLeft(Player* player, Item* item);
    // It fainted now: saved, and its owner told how to revive it.
    void FaintCompanion(Player* player, ObjectGuid itemGuid);
    // It went back into its item at this health share (saved when hurt).
    void PutCompanionAway(ObjectGuid itemGuid, float share);
    std::string CompanionName(Item* item) const;
    // Every few seconds: a fainted companion in the bags whose wait is over says so.
    void LookAtFaintedCompanions(Player* player, uint32 now);
    // At a stable master: every fainted companion in the bags revived (the fee: level x
    // Companions.StableReviveCostPerLevel each), every hurt one healed, free.
    void ReviveAtStable(Player* player);
    // A Reviving Draught from the bags for this companion (0: the one that fainted last).
    void UseReviveDraught(Player* player, uint64 companionItem);
    void BuyReviveDraught(Player* player);
    // The stable master's gossip, when a companion needs it (else the window opens at once).
    bool CompanionStableGossip(Player* player, Creature* creature);
    struct CompanionCare
    {
        uint32 fainted = 0;
        uint32 hurt = 0;
        uint32 cost = 0;
    };
    CompanionCare CompanionCareOf(Player* player);
    // The display the companion wears: its stage's own (the look table), else the same template
    // model at every summon (the item picks it); a hybrid's other line where they share one.
    uint32 CompanionDisplay(Fusion::Companions::CompanionSpecies const& sp, Fusion::Companions::CompanionState const& s,
        uint32 itemGuidLow) const;
    // characters.character_fusion_companion_out: the companion out (empty: none), so a logout
    // brings it back at login.
    void SaveCompanionOut(Player* player, ObjectGuid item);
    // Every look with no companion out: one to bring back (a login, its owner up again) and
    // the end of a defeated one's rest.
    void UpdateCompanionReturn(Player* player, uint32 now);
    void BuildCompanion(Pet* pet, Fusion::Companions::CompanionSpecies const& sp,
        Fusion::Companions::CompanionState const& state, uint8 level, bool fullHealth, bool bond = true);
    Fusion::Companions::EvolveContext CompanionContext(Player* player, Fusion::Companions::CompanionSpecies const& sp,
        Fusion::Companions::CompanionState const& state) const;
    void SendCompanionBar(Player* player, bool force = false);
    void EvolveCompanion(Player* player, uint64 itemGuid);
    // Mount the companion out (a rideable stage), or get off it.
    void RideCompanion(Player* player);
    // The mount cast (Companions::MountCastMs): watched every update, it ends in the saddle or
    // breaks; `why` is told on a break (empty: quietly).
    void UpdateMountCast(Player* player, uint32 now);
    void BreakMountCast(Player* player, char const* why);
    void FinishRide(Player* player);
    void Dismount(Player* player);
    Fusion::Companions::RideContext RideContextOf(Player* player) const;
    void CreditCompanionKill(Player* owner, Creature* victim);
    // The pet slot and the pet bar.
    void DismissOldPet(Player* player);
    // Its object scale: the stage's and its traits' on top of `display`'s own size, as vanilla
    // scales any creature (Companions::LookScale).
    float CompanionScale(Fusion::Companions::CompanionSpecies const& sp, Fusion::Companions::CompanionState const& s,
        uint32 display) const;
    void SetupCompanionBar(Player* player, Pet* pet, Fusion::Companions::CompanionSpecies const& sp,
        Fusion::Companions::CompanionState const& s);
    void SendCompanionMoves(Player* player);
    void SendCompanionLineage(Player* player);
    void UpdateCompanionCombat(Player* player, uint32 now);
    bool CastCompanionAbility(Player* player, uint8 slot, uint64 targetGuid, Fusion::Vec3 const* point, bool manual);
    void CompanionCommand(Player* player, uint64 targetGuid, Fusion::Vec3 const* point, bool follow);
    void CompanionStance(Player* player, uint8 slot);
    void PickCompanionAbility(Player* player, uint64 itemGuid, uint8 stage, uint8 option);
    void OnCompanionHit(Player* owner, Pet* pet, Unit* victim, uint32 damage);
    struct CompanionBreedPair
    {
        Item* a = nullptr;
        Item* b = nullptr;
    };
    std::vector<CompanionBreedPair> CompanionBreedPairs(Player* player) const;
    bool BreedCompanions(Player* player, Item* a, Item* b, bool free);
    void RollFreshCompanion(Item* item);
    std::unordered_map<uint32, uint32> m_companionPhoenixMs;   // item guid -> Phoenix-blooded ready again
    void GiveCompanionXp(Player* owner, uint32 xp);
    std::shared_ptr<FusionCompanionDuel> CompanionDuelOf(uint64 owner) const;
    // A duel holding this owner's companion: counting down or fighting (Companions::DuelHolds).
    // A challenge that only waits for its answer holds nothing.
    bool CompanionDuelHolds(uint64 owner) const;
    void DriveCompanionDuel(Player* player, std::shared_ptr<FusionCompanionDuel> const& duel, uint32 now);
    std::vector<Fusion::Companions::CompanionSpecies> m_companionSpecies;
    std::vector<Fusion::Companions::ItemStart> m_companionItems;   // mount items' starting points
    std::unordered_map<uint32, Fusion::Companions::CompanionState> m_companionStates;   // by item guid
    std::unordered_map<uint32, Fusion::Companions::Recovery> m_companionRecovery;       // by item guid (m_companionLock)
    Fusion::Companions::FaintSettings m_faint;     // mangosd.conf's Companions.*
    Fusion::DamageCredit m_companionCredit;                 // victims -> owners whose companion hit them
    uint32 m_companionCreditExpiredMs = 0;
    std::vector<std::shared_ptr<FusionCompanionDuel>> m_companionDuels;
    // Paid duel wins today by winner's account (Companions::PayDuelWin), loaded from
    // `account_fusion_companion_duel` on an account's first win this run (m_companionLock).
    std::unordered_map<uint32, Fusion::Companions::DuelWinsToday> m_companionDuelXp;
    std::unordered_map<uint64, uint32> m_companionChallengeMs;     // pair of owners -> last challenge (m_companionLock)
    mutable std::mutex m_companionLock;
    // Companions fighting a duel: pet -> (its foe, its owner). Read on every reaction check, so it
    // has its own lock and a count that keeps the common case (no duel) lock-free.
    std::unordered_map<uint64, std::pair<uint64, uint64>> m_duelFoes;
    mutable std::mutex m_duelFoesLock;
    std::atomic<uint32> m_duelFoesCount{0};
    // Companion battles (FusionCompanionBattles.cpp): by battle id, and each owner's battle.
    std::vector<Fusion::CompanionBattles::Tamer> m_tamers;
    std::unordered_map<uint32, std::shared_ptr<FusionCompanionBattle>> m_battles;
    std::unordered_map<uint64, uint32> m_battleOf;                  // player guid -> battle id
    std::unordered_map<uint64, std::pair<uint64, uint32>> m_battleAsks; // challenged -> (challenger, when)
    std::unordered_map<uint32, Fusion::Companions::DuelWinsToday> m_battleXp;  // PvP paid wins by account
    uint32 m_nextBattle = 1;
    mutable std::mutex m_battleLock;
    std::shared_ptr<FusionCompanionBattle> BattleOf(Player const* player) const;
    std::vector<Fusion::CompanionBattles::BattlePet> BattleTeamOf(Player* player, std::string& why, uint8 cap = 0);
    bool CanBattle(Player* player, std::string& why);
    bool StartCompanionBattle(Player* a, Player* b, Creature* npc, std::vector<Fusion::CompanionBattles::BattlePet> npcTeam,
        std::string const& npcName, uint16 tamer, uint8 xpFactor, bool daily);
    void HoldForBattle(Player* player, bool hold);
    void SendBattle(std::shared_ptr<FusionCompanionBattle> const& b, uint32 now);
    void EndCompanionBattle(std::shared_ptr<FusionCompanionBattle> const& b);
    void TellBattle(Player* player, std::string const& text);
    std::vector<Fusion::CompanionBattles::BattlePet> NpcTeam(std::vector<Fusion::CompanionBattles::TamerPet> const& pets) const;
    // Each companion item's saved deck and the cards it learned (character_fusion_companion_deck).
    struct CompanionDeckRow
    {
        std::vector<uint16> deck;
        std::vector<uint16> learned;
    };
    std::unordered_map<uint32, CompanionDeckRow> m_companionDecks;  // by item guid, under m_companionLock
    CompanionDeckRow CompanionDeckOf(uint32 itemGuidLow) const;
    void SaveCompanionDeckRow(uint32 itemGuidLow, CompanionDeckRow const& row);
    std::vector<Fusion::CompanionBattles::LibraryCard> CompanionLibrary(Item* item, Fusion::Companions::CompanionSpecies const& sp,
        Fusion::Companions::CompanionState const& s) const;
    Item* DeckItem(Player* player, uint64 itemGuid) const;
    // The Companions window (FusionCompanionIndex.cpp).
    std::unordered_map<uint32, std::vector<uint16>> m_companionCreatures;  // creature entry -> species
    void RecordCompanionSeen(Player* player, uint16 species, Fusion::Companions::Index::Seen how);
    bool AtCompanionStable(Player* player) const;
    void SendCompanionIndex(Player* player);
    void SendCompanionCarried(Player* player);
    void HandleCompanionWindow(Player* player, Fusion::Companions::Index::Request const& r);
    void CompanionGmEdit(Player* player, Fusion::Companions::Index::Request const& r);
    void RepickCompanionAbility(Player* player, uint64 itemGuid, uint8 stage, uint8 option);
    // A changed companion saved, its item re-stamped, and (when it is out) brought out again in
    // its new shape (`resummon`) or with its bar rebuilt.
    void CommitCompanion(Player* player, Item* item, Fusion::Companions::CompanionState const& s, bool resummon);
public:
    // A line in the player's UI error frame (the client's), instead of the chat window.
    void CompanionNotice(Player* player, Fusion::Companions::Index::NoticeKind kind, std::string const& text) const;
private:
    // ── Weapon pickups and enchanted rounds (FusionArmory.cpp) ──
public:
    // Startup: the characters table of learned round recipes.
    void LoadArmory();
    // A unit died (Unit::Kill, before OnPlayerDeath): an NPC gunner or a fusion player drops the
    // gun in hand where it fell.
    void DropGunOf(Unit* victim);
    // Lay a gun on the ground near `at` (it falls to the ground there); its id, 0 if none.
    uint32 DropGun(Map* map, Fusion::Vec3 at, float yaw, Fusion::Pickups::Gun const& gun, uint32 mag, uint32 reserve);
    // The world channel's pickup ops (60-69).
    void HandleArmoryChannel(Player* player, std::vector<uint8> const& body);
    // A player died: the pickups and the enchanted rounds go, the kit's guns come back.
    void ArmoryOnDeath(Player* player);
    // The pickups go (death, a kit swap): the kit's own guns are back; tell the client.
    void ClearPickups(Player* player);
    // A ship's deck gun (FusionShips.cpp): hand its gun over as a pickup, full; and take it back.
    bool MountGun(Player* player, uint16 gunId);
    void UnmountGun(Player* player);
    // Whether a gun the player holds is a pickup (which skips the unlock check).
    static bool PickupAllows(FusionPlayerData const& d, uint16 gunId);
    // Every player update: the guns near, the burns, a timed load running out.
    void UpdateArmory(Player* player, uint32 now);
    // An enchanted rounds item used from the bags (WorldSession::HandleUseItemOpcode).
    void UseRoundItem(Player* player, Item* item);
    // Load rounds into the gun in hand (the item, or `.fusion rounds`); false with no gun.
    bool LoadRounds(Player* player, Fusion::Rounds::Round round, uint8 grade);
    // A gun's hit with a round: the damage's scale (its trade-off, armor-piercing's bypass), and
    // what the round does after the hit lands (a burn, a slow, mana burned).
    float RoundDamageScale(Unit* target, Fusion::Rounds::Round round) const;
    void RoundHit(Player* shooter, Unit* target, Fusion::Rounds::Round round, uint32 damage, uint16 weaponId);
    // A pull fired the loaded rounds' last round, or the first of a fresh magazine: tell the HUD.
    void SendRounds(Player* player);
    // Enchanting trainers teach and make the rounds through a gossip line of their own.
    void AddEnchantingGossip(Player* player, Creature* creature);
    bool EnchantingGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action);
private:
    void SendHeld(Player* player);
    void TakeGun(Player* player, uint32 id);
    void EnchantingMenu(Player* player, Creature* creature);
    void LoadRoundRecipes(Player* player);
    mutable std::mutex m_droppedLock;
    Fusion::Pickups::Field m_dropped;

    // ── Construction, woodcutting and plots (FusionConstruction.cpp; src/fusion/docs/CONSTRUCTION.md) ──
public:
    // A part the fusion crafts (the world table `fusion_recipe`): the rules' recipe, its name for
    // the trainer's menu and whether it asks for an anvil.
    struct Craft
    {
        Fusion::Construction::CraftRecipe recipe;
        std::string name;
        bool needsAnvil = false;
    };

    // Startup: the characters tables, the plots and permits, the part recipes, and a first upkeep
    // sweep (no map is loaded yet, so lapsed pieces are taken straight from the table).
    void LoadConstruction();
    // Login: the character's Construction tier and skill.
    void LoadConstructionSkill(Player* player);
    // Every build update (once a second): the recipes and skill once, an upkeep reminder, the plot
    // under the player when it changes, and the hourly upkeep sweep.
    void UpdateConstruction(Player* player, uint32 now);
    // A body inside a piece (Build.h Traps) is moved to the nearest spot clear of them.
    void FreeFromPieces(Player* player);
    // -- Furniture (FusionFurniture.cpp; fusion/Furniture.h) --
    // Its tables (the plots' stashes, the trophies) made and read, at start.
    void LoadFurniture();
    // F on a piece of furniture: sit, lie down, read, open the stash, mount a trophy...
    void UseFurniture(Player* player, uint32 pieceId);
    // The stash worked: an item put in from the bags, or taken out.
    void HandleStash(Player* player, Fusion::Furniture::StashAct const& act);
    // A few times a minute: resting at a homestead's bed, and the stand-ins (mailbox, dummy,
    // trophy likeness) kept up near the player.
    void UpdateFurniture(Player* player, uint32 now);
    // Whether a crafting station of the spell focus stands within reach (Spell::CheckCast).
    bool FurnitureFocusNear(Player* player, uint32 focus);
    // How much stronger the player's song lands: on a bard's stage kStageBoost, else 1.
    float StageBoost(Player* player);
    // A packed plot's stash to its builder's reclaim.
    void PackStash(uint32 plotId, std::vector<Fusion::Build::Piece> const& pieces);
    // Set the player's tier and skill, save them and tell the client; `announce` says a skill-up.
    void SetConstruction(Player* player, Fusion::Construction::Tier tier, uint16 skill, bool announce);
    // Who the player is to the plot rules.
    Fusion::Plots::Actor PlotActor(Player const* player) const;
    Fusion::Plots::Grid const& PlotGrid() const { return m_plotGrid; }
    // The sites and districts: the world DB's `fusion_plot_site`, `_site_stop` and `_district`
    // (sql 12) when present and sound, else Plots::Default().
    void LoadPlotGrid();
    // A ghost (or a living player under the world) in a homestead site rises at its nearest
    // courier stop, where a spirit healer stands (Player::RepopAtGraveyard; true if so).
    bool RespawnInSite(Player* player);

    // The build code's checks (FusionMgr.cpp calls these at its decision points).
    // A piece about to be placed: the plot rules (false: Refusal::Protected).
    bool PlotAllowsBuild(Player* player, Fusion::Build::Piece const& piece);
    // A piece placed takes a kit made at the trade window (none when built free): the refusal,
    // or none (paid). `usedSalvage` is kept for the callers and is always false now.
    std::optional<Fusion::Build::Refusal> PayForPiece(Player* player, Fusion::Build::PieceType type, bool free, bool& usedSalvage);
    // A kit made: the skill-up a piece paid for with fresh materials may give.
    void PlacedSkillUp(Player* player, Fusion::Build::PieceType type, bool paidFresh);
    // Construction's trade window: `count` kits of a piece made (Create / Create All), each paid
    // and each rolling its skill-up as WoW's crafting does; the kits saved and sent.
    void CraftKits(Player* player, Fusion::Build::PieceType type, uint16 count);
    // One piece's kits written and the client told.
    void SaveKit(Player* player, Fusion::Build::PieceType type);
    // Whether the player may take a piece down (the plot rules, else only its builder or a GM).
    bool MayRemovePiece(Player* player, Fusion::Build::Piece const& piece);
    // A piece taken down: half its materials back by what is left of it, salvaged (to the stash
    // when the bags are full).
    void RefundPiece(Player* player, Fusion::Build::Piece const& piece);
    // Whether the plot the door stands on counts the player of its household (its owner, those
    // they permit, a GM: Plots::CanUseDoor Allowed).
    bool PlotAllowsDoor(Player const* player, Fusion::Build::Piece const& door);

    // ── Locks and portals (FusionLocks.cpp; fusion/Locks.h, Portals.h) ──
    // Whether the player may open a door or use a portal now. The caller holds m_buildLock (the
    // lock's secret is read under it; this takes only the plots' lock).
    Fusion::Locks::Verdict LockVerdict(Player const* player, Fusion::Build::Piece const& piece);
    // The build channel's lock and portal ops (4-7).
    void HandleLockChannel(Player* player, std::vector<uint8> const& body);
    // A refused door or portal: the keypad when it wants a code, else the reason.
    void SendLockRefusal(Player* player, Fusion::Build::Piece const& piece, Fusion::Locks::Verdict v);
    // The hearthstone's cooldown left (ms), which portals and the courier's way back share; and
    // starting it (on the server and the client's hearthstone).
    uint32 HearthCooldownLeft(Player* player) const;
    void StartHearthCooldown(Player* player);
private:
    void SetPieceLock(Player* player, Fusion::Locks::SetLock const& set);
    void TryLockCode(Player* player, uint32 pieceId, std::string const& code);
    void OpenPortalMenu(Player* player, uint32 pieceId);
    void UsePortal(Player* player, uint32 pieceId, uint8 destination);
    // A piece of the player's map by id (a copy), within `reach` of them; false when none.
    bool LockablePieceNear(Player* player, uint32 pieceId, float reach, Fusion::Build::Piece& out);
    // Toggle a door the player may open (and send it to those near).
    void SwingDoor(Player* player, uint32 pieceId);
    // Furniture (FusionFurniture.cpp).
    struct FurnitureProxy
    {
        uint32 map = 0;
        ObjectGuid guid;
        bool creature = false;
        Fusion::Vec3 at;
    };
    bool FurniturePieceNear(Player* player, uint32 pieceId, float reach, Fusion::Build::Piece& out);
    Fusion::Plots::Access PlotAccessAt(Player const* player, Fusion::Build::Piece const& p, uint32& plotId);
    void SendFurniturePage(Player* player, Fusion::Build::Piece const& p, std::string const& title, std::vector<std::string> const& lines);
    void SendStash(Player* player, Fusion::Build::Piece const& p);
    uint16 StashSlotsOf(uint32 mapId, uint32 plotId);
    bool StorableItem(Item const* it) const;
    void SaveStash(uint32 plotId, std::vector<Fusion::Furniture::StashEntry> const& stash);
    void DropFurnitureProxy(Map* map, uint32 pieceId);
    std::mutex m_furnitureLock;     // the three below; taken after m_buildLock and m_plotLock, never before
    std::unordered_map<uint32, FurnitureProxy> m_furnitureProxies;                          // by piece id
    std::unordered_map<uint32, std::vector<Fusion::Furniture::StashEntry>> m_stashes;      // by plot id
    std::unordered_map<uint32, std::pair<uint32, float>> m_trophies;                       // piece id -> display, scale
    std::unordered_map<uint32, Fusion::Locks::Secret> m_lockSecrets;    // code locks by piece (m_buildLock)
    std::unordered_map<uint32, int64> m_decayNext;                      // a map's next decay sweep (m_buildLock)
    void RepairStrike(Player* player, Fusion::ActionSystems::StrikeRequest const& r);
    std::mutex m_codeLock;                                              // m_codeLimiter
    Fusion::Locks::Limiter m_codeLimiter;
public:
    // Whether `by` may hurt the piece (the caller holds m_buildLock; this takes only the plots').
    bool PlotAllowsDamage(Fusion::Plots::Actor const& by, Fusion::Build::Piece const& piece);
    // The same by the piece's id (takes m_buildLock: never call it holding that lock).
    bool PieceDamageableBy(uint32 mapId, uint32 pieceId, Fusion::Plots::Actor const& by);

    // The scripts (scripts/custom/custom_fusion.cpp): the trainer, steward and courier open the
    // Construction window instead of a gossip menu; a lumber node used.
    bool ConstructionGossipHello(Player* player, Creature* creature);
    bool ConstructionGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action);
    bool UseLumberNode(Player* player, GameObject* go);
    void OpenedLumberNode(Player* player, GameObject* go);

    // -- Riding (FusionRiding.cpp, fusion/Riding.h) --
    // Startup: the launch points (world DB `fusion_launch_point`, else Riding::DefaultLaunchPoints).
    void LoadRiding();
    // Every player update: the glide, the burst of flight and the glide down after it, and the
    // site's floor (a player fallen through the world is put back at its nearest stop).
    void UpdateRiding(Player* player, uint32 now);
    void TellWard(Player* player, uint32 now, uint8 flags, float ceilingZ);
    // FusionMgr::Hit: a mounted player hit by gunfire may be knocked off.
    void KnockOffMount(Unit* to, uint32 damage, FusionHitSource const& source);
    // How far above a standing eye a rider's sits, and how far off a rider's shot may start.
    float RiderEyeRise(Player const* player) const;
    float ShotOriginTolerance(Player const* player) const;
    // For `.fusion riding`: the flight and the nearest launch point.
    std::string RidingReport(Player* player);
    // Skyriding (Skyriding.h): whether the mount is a flying companion line, and a Launch
    // or Surge asked for (the mounted move, or world op 72); false if refused.
    bool RidesSkyrider(Player* player) const;
    bool SkyMove(Player* player, Fusion::Skyriding::Move m);
    Fusion::Riding::Sense RidingSense(Player* player, uint32 now, bool update);
    void ApplyFlight(Player* player, Fusion::Riding::Phase before, Fusion::Riding::Sense const& s, uint8 skyFlags);
    void SendSky(Player* player, uint8 skyFlags);

    // Clear every lapsed plot and pack what stood on it into its builders' stashes. `map` is the
    // map whose update the caller is in (a lapsed plot on it has its pieces go from the world as
    // well), or null at startup. A plot on another map still loaded waits for a player there;
    // one on a map not loaded is packed straight from the table. Returns the plots lapsed.
    uint32 SweepPlots(Map* map);
    // For `.fusion construction`.
    std::string ConstructionReport(Player* player);
    Fusion::Plots::Registry PlotsCopy() const;
    // Move an account's plot's paid time (testing upkeep); false when it has none.
    bool SetPlotPaidUntil(uint32 account, int64 until);

private:
    void SendPlotHere(Player* player, uint32 plotId);
    // Pack and take away every piece on a plot gone back on sale, and drop its rows.
    void PackPlot(Map* map, uint32 plotId);
    // A plot's own rows (the plot and its permits), deleted at once.
    void ForgetPlotRows(uint32 plotId);
    bool PlotBeingPacked(uint32 plotId) const;
    // The Construction window (fusion/Homestead.h): what one of its buttons did, for its status
    // line (never chat).
    struct Said
    {
        bool ok = false;
        std::string text;
    };
    // Send the window: `open` opens it on the client (from the HUD or an NPC), else it refreshes;
    // the status line says `said` when there is anything to say.
    void SendHomestead(Player* player, Fusion::Homestead::Open open, Said const& said);
    // GCC cannot default a nested struct with member initialisers here, so an overload.
    void SendHomestead(Player* player, Fusion::Homestead::Open open) { SendHomestead(player, open, Said()); }
    // The window's messages (world ops 52 and 53).
    void HandleHomestead(Player* player, std::vector<uint8> const& body);
    // The NPC that opened the window, when the player still stands by it (else None).
    Fusion::Homestead::Npc HomesteadNpcNear(Player* player, Creature** creature = nullptr);
    // The claimed plots near the player for the markers, when they changed.
    void SendNearPlots(Player* player, bool force);
    // How many pieces stand on a plot.
    uint32 PiecesOnPlot(uint32 plotId);
    std::vector<Fusion::Homestead::Service> TrainerServices(Player* player);
    Said Train(Player* player);
    Said CraftPart(Player* player, size_t index);
    Said ClaimPlot(Player* player, Fusion::Homestead::Action how, uint32 arg, Creature* steward);
    Said PayPlot(Player* player, uint32 weeks);
    Said SetPermit(Player* player, uint32 guid, Fusion::Plots::PermitLevel level);
    Said AbandonPlot(Player* player);
    Said CollectReclaim(Player* player);
    Said TeleportToPlot(Player* player);
    Said Travel(Player* player, uint32 arg);
    // Remember where a player leaving the open world for a site was, for the courier's way back.
    void RememberWayBack(Player* player);
    // Add materials to a character's stash (`character_fusion_reclaim`).
    static void StashReclaim(uint32 guid, uint32 item, uint32 count);

    mutable std::mutex m_plotLock;              // m_plots; taken after m_buildLock, never before
    Fusion::Plots::Registry m_plots;
    std::atomic<uint32> m_plotGeneration{1};    // bumped on every sale, payment, permit and lapse
    std::atomic<int64_t> m_nextPlotSweep{0};
    Fusion::Plots::Grid m_plotGrid = Fusion::Plots::Default();     // set at startup only
    std::vector<uint32> m_plotsToPack;          // lapsed plots whose map's update must pack them (m_plotLock)
    std::vector<uint32> m_plotsPacking;         // of those, the ones a sweep is packing now (m_plotLock)
    std::atomic<uint32> m_plotsPending{0};
    std::vector<Craft> m_crafts;
    std::vector<Fusion::Riding::LaunchPoint> m_launchPoints = Fusion::Riding::DefaultLaunchPoints();  // set at startup only
    std::vector<uint8_t> m_recipesPacket;       // op 16, the same for everyone
    std::vector<uint8_t> m_reinforcementsPacket;    // world channel op 51
    // The danger table (Fusion.Danger.* in the config), and the killstreak sentries standing
    // (piece id -> map, the server time they go; under m_buildLock).
    Fusion::Danger::Config m_danger;
    std::unordered_map<uint32, std::pair<uint32, uint32>> m_streakSentries;

    // ── Prestige, field gear and MW2 matches (FusionPrestige.cpp; fusion/Prestige.h, FieldGear.h,
    //    Match.h; src/fusion/docs/PRESTIGE.md) ──
public:
    // Startup: the characters tables and the field recipes (world `fusion_trade_recipe`).
    void LoadPrestigeTables();
    // Login (from LoadAchievements, before the unlocks): the character's prestige, its account's
    // gun rank and the field recipes it learned.
    void LoadPrestige(Player* player);
    // The account's unlocks: its achievements' (UnlocksFor) and what prestige put on every gun.
    Fusion::UnlockSet UnlocksOf(Player const* player) const;
    // The prestige channel (8): the Prestige tab, the two-step confirm, titles, tracers, scoreboard.
    void HandlePrestigeChannel(Player* player, std::vector<uint8> const& body);
    // Every player update: worn field gear, milestones, the arena and the match HUD.
    void UpdatePrestige(Player* player, uint32 now);
    // Player::GiveXP: a kill's experience for a prestiged character on its way back up.
    uint32 PrestigeKillXp(Player const* player, uint32 xp) const;
    // The chat handler: a worn title before what the player says.
    void TitleChat(Player const* player, std::string& message, uint32 type) const;
    // WorldSession::HandleUseItemOpcode: an Engineering charge or an Alchemy stim (true: used).
    bool UseFieldItem(Player* player, Item* item);
    // FusionStats: the stims in effect on the handling; FusionMgr: a ghillie's aggro multiplier.
    void ApplyFieldStims(FusionPlayerData const& d, Fusion::Stats::Derived& combat, uint32 now) const;
    // The profession trainers' field recipes: added to a trainer's prepared gossip, and chosen.
    void AddProfessionGossip(Player* player, Creature* creature);
    bool FieldGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action);
    // A battleground's score (BattleGroundWS/AB/AV::UpdatePlayerScore): MW2's points.
    void OnBattlegroundScore(uint32 bgInstance, uint32 mapId, Player* player, uint32 type, uint32 value);
    // OnUnitKilled: a player killed a player (the arena's Team Deathmatch).
    void MatchKill(Player* killer, Player* victim);
    // `.fusion prestige`, `.fusion tdm` (admins).
    std::string PrestigeReport(Player* player);
    void ForcePrestigeReady(Player* player, Fusion::Prestige::Track track, uint16 key);
    void GivePrestigeTokens(Player* player, uint32 tokens);
    std::string ArenaReport();
    void EndArenaMatch();

private:
    struct TradeRecipe
    {
        Fusion::Construction::CraftRecipe recipe;
        std::string name;
        uint32 cost = 0;                        // the trainer's fee to learn it, copper
    };
    Fusion::Prestige::Standing StandingOf(Player* player);
    void SavePrestige(Player* player);
    void SendPrestigeState(Player* player);
    void DoPrestige(Player* player, Fusion::Prestige::Track track, uint16 key);
    void PayMilestone(Player* player, Fusion::Prestige::Milestone const& m);
    void SendLook(Player* player);
    void UpdateWornFieldGear(Player* player);
    void UpdateArena(Player* player, uint32 now);
    void SendMatchHud(Player* player, uint32 now);
    void SendScoreboard(Player* player);
    void TradeMenu(Player* player, Creature* creature, uint16 line);
    void LearnTrade(Player* player, size_t index);
    void CraftTrade(Player* player, size_t index);
    void BuyGood(Player* player, uint8 goodId);
    std::vector<TradeRecipe> m_tradeRecipes;
    mutable std::mutex m_matchLock;             // the boards and the arena match
    struct BgBoard
    {
        Fusion::Match::Mode mode = Fusion::Match::Mode::None;
        Fusion::Match::Board board;
        uint32 lastMs = 0;
    };
    std::unordered_map<uint32, BgBoard> m_bgBoards;     // by battleground instance id
    Fusion::Match::Deathmatch m_arena;
    uint32 m_arenaMatchNo = 1;                  // bumped when a match ends (players tell it once)
    std::string m_arenaEndText;
};

#define sFusionMgr FusionMgr::Instance()

#endif
