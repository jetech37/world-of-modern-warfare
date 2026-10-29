/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_WANDERERS_H
#define FUSION_WANDERERS_H

#include "fusion/Common.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Wanderers (design/WANDERERS.md): a third kind of character between players and NPCs. Each one
// is a named person of a playable race who lives in the open world: it walks the roads between
// towns, shoots the monsters it meets with a real MW2 gun on the game's own gun rules, loots,
// rests at inns, greets the players it passes, calls for help when it is losing a fight, and now
// and then takes a bounty from a board, hunts it down and turns it in. It remembers its name,
// level, purse and the players it has met across restarts.
//
// The rules here are plain data: who a wanderer is (its race, its look's archetype, its name and
// gun, all from one seed), the road network between a region's towns and the routes along it,
// travel while nobody is near (a wanderer is a creature only near players), what it does next (a
// small state machine), which monster it shoots, how many are in the world at once and how much
// thinking a tick may spend, and the saved state's text. The game (game/Fusion/FusionWanderers.cpp)
// spawns, moves and fires them.
//
// They are real characters who quest and grind in squads, as server-side creatures instead of Player sessions (the design doc says why), fight on the
// fusion gun rules rather than WoW's auto-attack, and need nothing from the client but what it
// already draws for NPC gunners and other players' bodies.
namespace Fusion::Wanderers
{
    // ── Who a wanderer is ───────────────────────────────────────────────────────────────────

    enum class Side : uint8_t
    {
        Alliance = 1,
        Horde = 2,
    };

    // The playable races (ChrRaces ids).
    constexpr uint8_t kHuman = 1, kOrc = 2, kDwarf = 3, kNightElf = 4, kUndead = 5, kTauren = 6, kGnome = 7,
                      kTroll = 8;
    // The side a race fights for; none for a race no player can be.
    std::optional<Side> SideOfRace(uint8_t race);
    // A race of the side, by the seed: the side's four races in equal shares.
    uint8_t RaceFor(Side side, uint32_t seed);

    // How a wanderer fights: the kind of gun it carries and how far it stands off.
    enum class Archetype : uint8_t
    {
        Rifleman = 0,       // an assault rifle, the commonest
        Scout = 1,          // an SMG or a machine pistol, close
        Sharpshooter = 2,   // a marksman rifle, then a sniper, far
        Brawler = 3,        // a shotgun, very close
        Gunslinger = 4,     // a pistol (low levels), then a heavier sidearm
        Count = 5,
    };
    // The title over its name ("Wandering Rifleman").
    char const* ArchetypeTitle(Archetype a);
    // By the seed: riflemen 35%, scouts 20%, sharpshooters 15%, brawlers 15%, gunslingers 15%.
    Archetype ArchetypeFor(uint32_t seed);
    // The MW2 gun it carries (the catalog's name), by its archetype and level, fixed by the seed:
    // pistols and SMGs low, rifles and shotguns from 10, a sniper for a sharpshooter from 20.
    std::string GunFor(Archetype a, uint8_t level, uint32_t seed);
    // How far it likes to stand from its target (yards).
    float HoldRange(Archetype a);

    // A seed for wanderer `id` (and a re-roll's `salt`).
    uint32_t Seed(uint32_t id, uint32_t salt = 0);
    uint32_t Mix(uint32_t x);

    // A name of the race's own sound (our own syllables, never a lore name), 3 to 12 letters,
    // capitalised: the same race, gender and seed always give the same name.
    std::string MakeName(uint8_t race, uint8_t gender, uint32_t seed);
    // Whether a name would pass as a character's: 2 to 12 letters, a capital then lower case.
    bool ValidName(std::string const& name);

    // ── Levels ──────────────────────────────────────────────────────────────────────────────

    struct ZoneBand
    {
        uint32_t zone = 0;
        uint8_t minLevel = 1;
        uint8_t maxLevel = 10;
    };
    // A new wanderer's level: within its home zone's band, by the seed.
    uint8_t StartLevel(ZoneBand const& band, uint32_t seed);
    // It stops gaining levels this far past its zone's top (it is a local, not a hero).
    uint8_t LevelCap(ZoneBand const& band);
    // Experience for a kill, vanilla's shape: 45 + 5 x the victim's level, less for a grey kill
    // (5 or more levels under it: nothing), more for a harder one (+5% a level above, at most 4).
    uint32_t KillXp(uint8_t level, uint8_t victimLevel);
    // Experience from `level` to the next.
    uint32_t XpToLevel(uint8_t level);
    // Add experience; true when it gained a level. Past the cap it keeps none.
    bool GainXp(uint8_t& level, uint32_t& xp, uint32_t gained, uint8_t cap);
    // Coin a kill's corpse gives up (copper): about a vanilla creature's purse.
    uint32_t LootCopper(uint8_t victimLevel, uint32_t roll);

    // ── Roads ───────────────────────────────────────────────────────────────────────────────

    enum class NodeKind : uint8_t
    {
        Town = 0,       // a settlement: rest here a while
        Inn = 1,        // an innkeeper: rest here longest
        Board = 2,      // a bounty board: bounties are taken and turned in here
        Road = 3,       // a waypoint on a road: never a destination of its own
        // Phase 2 (WandererLives.h): where an occupation works, and where it sells.
        Fishing = 4,    // water to fish
        Mine = 5,       // ore veins
        Herbs = 6,      // herb meadows
        Wilds = 7,      // game to trap and skin
        Market = 8,     // vendors to sell to (a settlement too)
        Landmark = 9,   // a sight to see
        Count = 10,
    };
    // A settlement: a town, an inn, a board or a market (where a roamer may go, and rest).
    bool IsSettlement(NodeKind k);
    // A workplace: water, veins, herbs, wilds or a landmark (never a roamer's destination).
    bool IsWorkSpot(NodeKind k);
    char const* NodeKindName(NodeKind k);
    struct Node
    {
        uint32_t id = 0;
        uint32_t zone = 0;
        NodeKind kind = NodeKind::Town;
        Vec3 pos;
        std::string name;
    };
    struct Link
    {
        int to = -1;
        float length = 0.f;
    };
    struct RoadNet
    {
        std::vector<Node> nodes;
        std::vector<std::vector<Link>> links;       // by node index
        int IndexOf(uint32_t id) const;             // -1 when not in the network
        bool Linked(int a, int b) const;
        size_t LinkCount() const;                   // each road once
    };

    // Two nodes nearer than this are joined by a road of their own (besides the spanning tree).
    constexpr float kNeighbourLink = 350.f;
    // The network over a region's nodes: the authored roads (node id pairs; an unknown id is
    // skipped), a spanning tree of the shortest legs so every node is reachable from every other,
    // and each node's legs to the nodes within kNeighbourLink.
    RoadNet BuildRoads(std::vector<Node> nodes, std::vector<std::pair<uint32_t, uint32_t>> const& authored);
    // The shortest way from `from` to `to` (node indexes, both ends included); empty for none.
    std::vector<int> Route(RoadNet const& net, int from, int to);
    // The node nearest `p` (-1 for an empty network); `kinds` limits it to towns, inns and boards.
    int NearestNode(RoadNet const& net, Vec3 p, bool placesOnly = false);
    // Where to go next from node `at`: a town, inn or board (never a road node) other than `at`
    // and the last few visited (`recent`), the nearer ones likelier (a weight of 1 / (1 + the
    // route's length / 400)), its home zone's three times as likely. -1 when there is none.
    int ChooseDestination(RoadNet const& net, int at, std::vector<int> const& recent, uint32_t homeZone, uint32_t roll);
    // How many places it remembers, so it does not go back and forth between two.
    constexpr size_t kRecentPlaces = 3;
    void Remember(std::vector<int>& recent, int node);

    // A long leg is walked a step at a time (each step a short pathfinding move): the points
    // from `a` toward `b` every `step` yards, ending at `b` (`a` itself left out).
    std::vector<Vec3> Steps(Vec3 a, Vec3 b, float step);
    constexpr float kStepYards = 45.f;

    // A trip: a route's nodes (by id, so it survives the network's indexes changing at a
    // restart) and an optional end beyond the last node (a hunting ground, a corpse).
    struct Trip
    {
        std::vector<uint32_t> route;
        bool hasEnd = false;
        Vec3 end;
        uint32_t leg = 0;           // walking from point `leg` to `leg + 1`
        float along = 0.f;          // yards into that leg
        size_t Points() const { return route.size() + (hasEnd ? 1 : 0); }
        bool Done() const { return Points() == 0 || leg + 1 >= Points(); }
    };
    // Point `i` of a trip (its nodes, then its end); its first node for an unknown id.
    Vec3 TripPoint(RoadNet const& net, Trip const& t, size_t i);
    // Where on its trip it is now.
    Vec3 TripPos(RoadNet const& net, Trip const& t);
    // Walk `yards` along the trip, unseen (nobody near): true when it has arrived.
    bool Advance(RoadNet const& net, Trip& t, float yards);
    // A trip from wherever it stands (`from`, near node `fromNode`) to node `to`, and on to `end`.
    Trip PlanTrip(RoadNet const& net, int fromNode, int to, std::optional<Vec3> end = std::nullopt);
    // Walking pace on the road, unseen (yards a second): WoW's walk.
    constexpr float kWalkSpeed = 2.5f;

    // ── What it does ────────────────────────────────────────────────────────────────────────

    enum class State : uint8_t
    {
        Idle = 0,       // between things: it decides at once
        Travel = 1,     // walking a trip
        Fight = 2,
        Loot = 3,       // to its kill's corpse, then kneeling over it
        Rest = 4,       // sitting: after a hard fight, or at an inn
        Greet = 5,      // a wave and a word to a player passing
        Dead = 6,       // back at a town after a while
        Flee = 7,       // running from a fight it will not win (WandererLives.h FleeBelowPct)
    };
    char const* StateName(State s);

    enum class Goal : uint8_t
    {
        Roam = 0,           // town to town
        TakeBounty = 1,     // to a board to read what is posted
        Hunt = 2,           // after a bounty's creatures
        TurnIn = 3,         // back to the board with it done
    };
    char const* GoalName(Goal g);

    // A bounty it took: a kill bounty from the board (fusion/Bounties.h), up to four creatures.
    struct Errand
    {
        uint32_t questId = 0;
        uint32_t entries[4] = {0, 0, 0, 0};
        uint8_t needed = 0;
        uint8_t done = 0;
        uint32_t boardNode = 0;             // the board's node id
        bool hasGround = false;
        Vec3 ground;                        // where its creatures live
        uint32_t untilMs = 0;               // it gives up at this time (the mind's clock)
        std::string title;
        bool Wants(uint32_t entry) const;
        bool Complete() const { return needed && done >= needed; }
    };
    // A wanderer hunts at most this many of a bounty's creatures (a bounty asks a player for up
    // to a dozen: a wanderer has the rest of its life to get on with).
    constexpr uint8_t kMaxErrandKills = 6;
    // It gives up a bounty not finished in this long.
    constexpr uint32_t kErrandMs = 20 * 60 * 1000;
    // The share of arrivals that send it to look at a board, when one has a bounty for it.
    constexpr uint32_t kBountyChancePercent = 35;
    // It looks at a board again no sooner than this after it last did.
    constexpr uint32_t kBountyCooldownMs = 10 * 60 * 1000;

    // What it knows this think.
    struct Sense
    {
        uint32_t nowMs = 0;
        float healthPct = 100.f;
        uint64_t attacker = 0;          // the nearest unit fighting it, 0 none
        uint64_t bestTarget = 0;        // ChooseTarget's pick among what it sees, 0 none
        uint64_t killed = 0;            // a kill it just made (its corpse), 0 none
        uint64_t playerNear = 0;        // a friendly player within kGreetRange, 0 none
        bool arrived = false;           // its trip is done (or it has none)
        bool atBoard = false;           // within kBoardReach of its errand's board
        bool atPlace = false;           // arrived at a town or an inn
        NodeKind placeKind = NodeKind::Town;
        bool atCorpse = false;          // within reach of the corpse it is looting
        bool boardHasBounty = false;    // a board of its region has a kill bounty it may take
        uint32_t roll = 0;              // a random number for this think
        // Phase 2: its life (WandererLives.h).
        float fleeBelowPct = 0.f;       // it runs from an attacker below this health (0 never)
        uint32_t activityMs = 0;        // arrived where its plan sent it: do that this long (0 rest as Phase 1)
    };

    // What the game does about it.
    enum class Act : uint8_t
    {
        None = 0,
        Engage,         // turn on mind.target: gun up, face it, start the trigger
        CallForHelp,    // a yell; friends near join in on its attacker
        LootCorpse,     // walk to the corpse
        Kneel,          // kneel over it
        TakeLoot,       // stand up from it with its coin (mind.corpse is the one looted)
        Stand,          // stand up and get on with it
        Sit,            // sit down to rest
        Greet,          // face mind.greet, wave, say a line
        PlanTrip,       // plan the next trip by its goal (a place, a board, the hunting ground)
        TakeBounty,     // at the board: read it, take a bounty (or none) and go
        TurnIn,         // at the board: hand the bounty in
        Walk,           // keep walking the trip (it stopped)
        Flee,           // run for the nearest settlement, crying for help
        Work,           // arrived where its life's plan sent it: do it (mind.untilMs long)
    };
    char const* ActName(Act a);

    constexpr float kGreetRange = 12.f;
    constexpr uint32_t kGreetMs = 3000;                     // how long it stops for a greeting
    constexpr uint32_t kGreetAgainMs = 10 * 60 * 1000;      // the same player again
    constexpr size_t kGreetMemory = 16;
    constexpr float kRestBelowPct = 55.f;                   // it sits down after a fight below this
    constexpr uint32_t kRestMs = 15000;
    constexpr uint32_t kInnRestMinMs = 20000, kInnRestMaxMs = 60000;
    constexpr uint32_t kTownRestMinMs = 8000, kTownRestMaxMs = 20000;
    constexpr float kHelpBelowPct = 40.f;                   // it calls for help below this
    constexpr float kHelpRadius = 30.f;
    constexpr uint32_t kLootWalkMs = 8000;                  // gives up reaching a corpse after this
    constexpr uint32_t kKneelMs = 2500;
    constexpr float kBoardReach = 8.f;
    constexpr uint32_t kRespawnMs = 3 * 60 * 1000;          // back at a town after dying
    constexpr uint32_t kFleeMs = 8000;                      // how long it runs before it turns

    struct Mind
    {
        State state = State::Idle;
        Goal goal = Goal::Roam;
        uint32_t untilMs = 0;
        uint64_t target = 0;
        uint64_t corpse = 0;
        uint64_t greet = 0;
        bool kneeling = false;
        bool calledForHelp = false;
        uint32_t nextBountyMs = 0;
        bool fled = false;              // it ran from this fight already: cornered, it fights
        Errand errand;
        std::vector<std::pair<uint64_t, uint32_t>> greeted;     // player, when
    };
    // Whether it would greet `player` now (not within kGreetAgainMs of the last time).
    bool ShouldGreet(Mind const& m, uint64_t player, uint32_t nowMs);
    void NoteGreeted(Mind& m, uint64_t player, uint32_t nowMs);

    // The next act. A fight comes first (whoever attacks it, else what it chose to shoot); then
    // the loot of its kill; then rest when hurt; then a player passing to greet; then the trip
    // it is on (picked up again after anything that stopped it); then, arrived, its errand or a
    // rest and the next trip. The sense's roll picks a rest's length and whether it goes to a board.
    Act Think(Mind& m, Sense const& s);
    // It died: everything it was doing is dropped but its errand (a wanderer picks up where it
    // left off), and it comes back at kRespawnMs.
    void Died(Mind& m, uint32_t nowMs);
    // Back on its feet at a town.
    void Revived(Mind& m);
    // How long it rests at an arrival: an inn longer than a town, by `roll`.
    uint32_t RestMsAt(NodeKind kind, uint32_t roll);

    // ── What it shoots ──────────────────────────────────────────────────────────────────────

    struct Candidate
    {
        uint64_t guid = 0;
        float distance = 0.f;
        uint8_t level = 1;
        bool elite = false;
        bool player = false;
        bool attackingMe = false;
        bool tappedByOther = false;     // a player has tagged it: never steal a player's kill
        bool inSight = true;
        bool bountyTarget = false;
        float healthPct = 100.f;
    };
    struct TargetRules
    {
        uint8_t selfLevel = 1;
        float engageRange = 30.f;       // it starts fights this near
        float errandRange = 45.f;       // further for a bounty's creature
        uint8_t maxLevelAbove = 2;      // it leaves alone what is more than this above it
        bool hunting = false;           // on a bounty's hunt
        bool startsFights = true;       // a peaceable one only fights back (and a bounty's creatures)
    };
    // The one to shoot (an index into `c`), -1 none. It never starts a fight with a player, a
    // creature a player has tagged, an elite, one too far above it, one it cannot see or one out
    // of range; whoever attacks it, it fights back. Its attackers first, then a bounty's
    // creatures when hunting, then the nearest, the more wounded first, the lowest guid on a tie.
    int ChooseTarget(std::vector<Candidate> const& c, TargetRules const& r);

    // ── How many, and how much thinking ─────────────────────────────────────────────────────

    // The most wanderers a server keeps (their creature entries are 93700 + id, ids 1 to this).
    constexpr uint32_t kMaxRoster = 99;
    constexpr uint32_t kFirstEntry = 93700;
    // Each region's roster: `perZone` for each of its zones, held to what is left of kMaxRoster.
    uint32_t RosterFor(uint32_t perZone, uint32_t zones, uint32_t alreadyUsed);
    // How many may be in the world as creatures at once: `perPlayer` for each player in the
    // wanderers' zones (at least `perPlayer`), at most `maxSpawned`.
    uint32_t SpawnCap(uint32_t maxSpawned, uint32_t perPlayer, uint32_t players);
    // A wanderer comes into the world when a player is within `spawnRadius` of it and goes
    // back out of it when none is within `despawnRadius` (the gap stops it flickering), never in
    // the middle of a fight. Which ones are in the world (indexes), the nearest to a player first,
    // at most `cap`: those already in keep their place before newcomers.
    struct Presence
    {
        float nearestPlayer = 1e9f;     // yards to the nearest player on its map
        bool spawned = false;
        bool busy = false;              // fighting: it stays
        bool dead = false;              // waiting to come back: never spawned
    };
    std::vector<size_t> ChoosePresent(std::vector<Presence> const& w, uint32_t cap, float spawnRadius, float despawnRadius);
    // The unseen ones are walked a few at a time: the next `maxPerTick` of `count` from `cursor`
    // (round the end), and the cursor moved on.
    std::vector<size_t> TakeTurns(size_t count, size_t maxPerTick, size_t& cursor);
    // A creature wanderer thinks this often (ms), staggered by its id so they do not all think
    // on one tick, and looks about it (a grid search) every kLookMs.
    constexpr uint32_t kThinkMs = 400;
    constexpr uint32_t kLookMs = 1200;
    uint32_t FirstThinkMs(uint32_t id);

    // ── What it says ────────────────────────────────────────────────────────────────────────

    // How well it knows a player: 0 a stranger, 1 met before (3 points), 2 a friend (10).
    uint8_t BondTier(int32_t points);
    constexpr int32_t kGreetPoints = 1;
    // A greeting for `player` (its name), by how well it knows them; the same seed and count give
    // the same line.
    std::string GreetLine(uint32_t seed, uint8_t tier, std::string const& player, uint32_t count);
    std::string HelpLine(uint32_t seed);
    std::string BountyTakenLine(uint32_t seed, std::string const& title);
    std::string BountyDoneLine(uint32_t seed, std::string const& title);
    std::string RestLine(uint32_t seed, NodeKind at);

    // ── What it keeps ───────────────────────────────────────────────────────────────────────

    // A wanderer's row (the characters database, aw_wanderer): who it is in its own columns, and
    // what it is doing (EncodeState) as one line of text that survives a restart.
    struct Record
    {
        uint32_t id = 0;
        std::string name;
        uint8_t race = kHuman;
        uint8_t gender = 0;
        Archetype archetype = Archetype::Rifleman;
        Side side = Side::Alliance;
        uint32_t homeZone = 0;
        uint8_t level = 1;
        uint32_t xp = 0;
        uint32_t money = 0;             // copper
        uint32_t kills = 0, deaths = 0, bounties = 0;
        uint32_t seed = 0;
        uint32_t display = 0;           // its model (0: pick one again)
        // What it is doing.
        Trip trip;
        Goal goal = Goal::Roam;
        Errand errand;
        uint32_t lastNode = 0;          // the node id it last stood at
        std::vector<uint32_t> recent;   // the places it last visited (node ids)
        uint32_t respawnInMs = 0;       // dead: back this long after the restart
    };
    // "v1|..." fields: the trip, the goal, the errand, the last node, the recent places, the
    // respawn wait. Floats to a tenth of a yard. Names and titles have no '|' or ';' (stripped).
    std::string EncodeState(Record const& r);
    // Fills `r`'s state from the text; false (and `r`'s state untouched) for text it cannot read.
    bool DecodeState(std::string const& text, Record& r);
}

#endif
