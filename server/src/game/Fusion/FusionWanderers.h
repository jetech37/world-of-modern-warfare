/*
 * World of Modern Warfare: the wanderers. GPL-2.0-or-later.
 *
 * A third kind of character between players and NPCs (design/WANDERERS.md): named people of the
 * playable races who travel the roads between towns, fight the local monsters with MW2 guns on
 * the fusion's own gun rules, loot, rest at inns, greet the players they pass, call for help when
 * losing, and take bounties from the boards. The rules are fusion/Wanderers.h; this file puts
 * them in the world.
 *
 * Each wanderer is a creature (never a Player session), and one only while a player is near: the
 * roster lives here, each with its trip along a road network made at startup from the region's
 * towns (sql/custom/azeroth_warfare/28_wanderers.sql, its innkeepers and flight masters, and its
 * bounty boards). Out of every player's sight it walks its trip on paper, a few each tick; when a
 * player comes within Wanderers.SpawnRadius it is summoned where its trip has got to, and when
 * none is within Wanderers.DespawnRadius it is sent out again, never mid-fight. Its creature
 * template is made here, one per wanderer (entries 93700 + id), so each has its own name; its
 * look is a dressed NPC model of its race and gender from the game's own data.
 *
 * The client needs nothing new: the gun in its hands is the NPC gunners' (world op 33, m_fusionGunId),
 * each round the NPC gunners' (op 32), and its third-person body is posed from the pose relay
 * other players' bodies use (action-systems op 7, sent here for it).
 *
 * Kept in the characters database (made here at startup): `aw_wanderer` (who each one is and
 * what it is doing) and `aw_wanderer_bond` (how well it knows each player). mangosd.conf:
 * Wanderers.* (off by default). GMs: `.wanderer list|info|spawn|despawn`.
 *
 * Threads: Load, Update and the GM commands run on the world thread with every map at rest
 * (spawning, sending out, saving); UpdateCreature, OnKill and OnDeath on the creature's map
 * thread, touching only that wanderer (and, for a call for help, wanderers on the same map).
 */

#ifndef AW_FUSION_WANDERERS_H
#define AW_FUSION_WANDERERS_H

#include "Common.h"
#include "ObjectGuid.h"
#include "fusion/WandererLives.h"
#include "fusion/Wanderers.h"

#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class Map;
class Player;
class Unit;

struct FusionWandererBond
{
    int32 points = 0;
    uint32 greets = 0;
    uint32 lastMet = 0;             // unix seconds
    // Phase 2: what it remembers of them.
    uint32 helped = 0;              // they killed what was on it
    uint32 harmed = 0;              // they attacked or killed it
    uint32 traded = 0;              // they bought from it, or hired it
    bool dirty = false;
};

// One wanderer: who it is and what it is doing (rec, mind), and its creature while it is in
// the world. A creature's m_wanderer points here.
struct FusionWanderer : std::enable_shared_from_this<FusionWanderer>
{
    Fusion::Wanderers::Record rec;
    Fusion::Wanderers::Mind mind;
    uint32 region = 0;
    uint32 entry = 0;               // its creature template
    uint16 gun = 0;                 // its MW2 gun (catalog id)
    float healthPct = 100.f;        // kept while it is out of the world
    bool dirty = false;             // to be saved

    // In the world.
    ObjectGuid guid;
    uint32 mapId = 0;
    bool spawned = false;
    uint32 diedAtMs = 0;
    uint32 hiddenUntilMs = 0;       // a GM sent it out: not back in before this
    uint32 nextThinkMs = 0, nextLookMs = 0, nextPoseMs = 0, nextHealMs = 0;
    uint16 lastPose = 0xFFFF;

    // Its walk: the steps to its trip's next point.
    std::vector<Fusion::Vec3> steps;
    size_t step = 0;
    uint8 stuck = 0;
    bool moving = false;
    uint32 stepAtMs = 0;            // when the step's move was given (its path may take a moment)

    // What it saw last look.
    uint64 attacker = 0, bestTarget = 0, playerNear = 0;
    uint64 nearWanderer = 0;        // the nearest other wanderer within kChatRange (its creature)
    float nearestPlayer = 1e9f;
    // Its last kill.
    uint64 killed = 0;
    Fusion::Vec3 corpseAt;
    uint8 killedLevel = 1;
    // A friend's call for help: its attacker.
    uint64 helpFor = 0;

    // Its gun.
    uint32 burstLeft = 0, nextShotMs = 0;
    bool triggerStarted = false;
    uint64 aimTarget = 0;
    uint32 aimSinceMs = 0;
    uint32 nextMoveMs = 0;

    // Unseen.
    uint32 lastAdvanceMs = 0;
    uint32 unseenUntilMs = 0;       // resting (or hunting) where it is until then
    std::vector<int> recentIdx;     // its recent places as node indexes of its region

    std::unordered_map<uint32, FusionWandererBond> bonds;  // by player guid (low)

    // Phase 2: its life (fusion/WandererLives.h; FusionWandererLives.cpp).
    Fusion::Wanderers::Life life;
    bool activityOn = false;        // doing life.doing where it planned (not yet credited)
    uint32 doingStartMs = 0, doingMs = 0;
    uint32 nextLineMs = 0;          // its own gap between remarks
    uint32 needsMs = 0;             // its needs' clock
    uint32 talkUntilMs = 0;         // a player is talking to it: it stands
    uint64 talkTo = 0;
    uint32 chatWith = 0;            // a wanderer it stopped to talk to (id)
    uint32 chatUntilMs = 0, replyAtMs = 0;
    uint32 lastChatMs = 0;
    std::string replyLine;          // its answer, said at replyAtMs
    uint64 hiredBy = 0;             // a player it walks with
    uint32 hiredUntilMs = 0;
    bool following = false;
};

class WandererMgr
{
public:
    static WandererMgr& Instance();

    // Startup, after the world database, the fusion rules and the bounty boards.
    void Load();
    bool IsEnabled() const { return m_enabled; }

    // The world thread, between map updates: who is in the world, the unseen walking, saving.
    void Update(uint32 diff);
    // Creature::Update, for a wanderer's creature (its map thread).
    void UpdateCreature(Creature* c, uint32 diff);
    // FusionMgr::OnUnitKilled: a wanderer's kill, and a wanderer's death.
    void OnKill(Creature* c, Unit* victim);
    void OnDeath(Creature* c, Unit* killer);

    // GM commands (FusionWanderers.cpp, ChatHandler::HandleWandererCommand).
    std::vector<std::string> List(Player const* viewer) const;
    std::vector<std::string> Info(uint32 id, Player const* gm) const;
    // The wanderer of the GM's own region (or `id`) brought to the GM; false with a reason.
    bool Summon(Player* gm, uint32 id, std::string& why);
    // Sent out of the world (`id`, or 0 for all), kept out for a few minutes.
    uint32 Dismiss(uint32 id);
    // `.wanderer goto <id>`: the GM taken to it.
    bool Goto(Player* gm, uint32 id, std::string& why);
    // The wanderer a creature is (0 none).
    static uint32 IdOf(Creature const* c);

    // Phase 2 (FusionWandererLives.cpp). Talking to one (NPCHandler): its gossip menu; true when
    // the creature is a wanderer and it was handled.
    bool GossipHello(Player* player, Creature* c);
    bool GossipSelect(Player* player, Creature* c, uint32 sender, uint32 action);
    // Its stall (ItemHandler): its pack as a vendor's list, and a purchase from it.
    bool SendWares(Player* player, Creature* c);
    // A companion battle against it (FusionCompanionBattles.cpp): it stands, facing `player`, `ms` more.
    void HoldForBattle(Creature* c, uint64 player, uint32 ms);
    bool BuyFrom(Player* player, ObjectGuid vendor, uint32 item, uint8 count);
    // A player killed something that was fighting a wanderer (FusionMgr::OnUnitKilled).
    void OnHelpKill(Unit* killer, Unit* victim);
    // Something wanderers near notice: a killstreak's craft overhead, a player building. Any
    // thread; taken up on the world thread.
    void NoteWorldEvent(uint32 mapId, Fusion::Vec3 at, Fusion::Wanderers::NewsKind kind, std::string const& who);

private:
    struct Region
    {
        uint32 map = 0;
        Fusion::Wanderers::Side side = Fusion::Wanderers::Side::Alliance;
        std::vector<Fusion::Wanderers::ZoneBand> zones;
        std::vector<std::string> zoneNames;
        Fusion::Wanderers::RoadNet net;
        std::vector<int> boards;            // node indexes
        std::map<int, uint32> boardZone;    // board node index -> its zone
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
        std::string name;
        // Phase 2: each node's place name (its AreaTable area), its workplaces by kind, what a
        // workplace yields (the world's loot tables), what a market's vendors sell, how many
        // vendors a market has, and the rare creatures that live there.
        std::vector<std::string> placeName;
        std::vector<int> byKind[size_t(Fusion::Wanderers::NodeKind::Count)];
        std::map<int, Fusion::Wanderers::YieldTable> yields;
        std::map<int, std::vector<std::pair<uint32, Fusion::Wanderers::Price>>> goods;
        std::map<int, uint32> marketSize;
        struct Rare
        {
            std::string name;
            Fusion::Vec3 pos;
        };
        std::vector<Rare> rares;
    };
    struct WorldEvent
    {
        uint32 map = 0;
        Fusion::Vec3 at;
        Fusion::Wanderers::NewsKind kind = Fusion::Wanderers::NewsKind::None;
        std::string who;
    };

    void LoadZones();
    void LoadNodes();
    void IndexSpawns();
    void BuildLooks();
    void LoadRoster();
    void LoadBonds();
    FusionWanderer* MakeWanderer(uint32 id, uint32 region, Fusion::Wanderers::ZoneBand const& band);
    void MakeTemplate(FusionWanderer& w);
    uint32 RaceFaction(uint8 race) const;
    uint32 PickDisplay(uint8 race, uint8& gender, uint32 seed) const;
    Fusion::Wanderers::ZoneBand BandOf(FusionWanderer const& w) const;

    // The world thread.
    void SpawnIn(FusionWanderer& w, Fusion::Vec3 const* at = nullptr);
    void SpawnOut(FusionWanderer& w);
    void AdvanceUnseen(FusionWanderer& w, uint32 now);
    void Save(FusionWanderer& w);
    void SaveAll(bool all);

    // Either thread (only the wanderer's own state).
    void PlanNext(FusionWanderer& w, Fusion::Vec3 from, uint32 roll);
    void ArriveAt(FusionWanderer& w);
    bool BoardHasBounty(FusionWanderer const& w) const;
    bool TakeErrand(FusionWanderer& w, uint32 now, uint32 roll);
    void TurnInErrand(FusionWanderer& w, uint32 roll);
    void CreditKill(FusionWanderer& w, uint32 entry, uint8 level);
    std::optional<Fusion::Vec3> GroundOf(Region const& r, uint32 entry, Fusion::Vec3 around) const;

    // The map thread.
    void Look(Creature* c, FusionWanderer& w, uint32 now);
    void Do(Creature* c, FusionWanderer& w, Fusion::Wanderers::Act act, uint32 now);
    void Walk(Creature* c, FusionWanderer& w, uint32 now);
    void Fight(Creature* c, FusionWanderer& w, uint32 now);
    void SendPose(Creature* c, FusionWanderer& w, uint32 now, bool force);
    void StepTo(Creature* c, Fusion::Vec3 p, bool run);
    void Say(Creature* c, FusionWanderer const& w, std::string const& line, Unit const* to = nullptr, bool yell = false) const;

    FusionWanderer* ById(uint32 id) const;

    // Phase 2, its life (FusionWandererLives.cpp).
    struct Spot
    {
        Fusion::Wanderers::NodeKind kind;
        Fusion::Vec3 pos;
        std::string name;
        std::vector<uint32> lootIds;     // gameobject or skinning loot ids found there
        uint32 vendors = 0;
        std::vector<uint32> vendorEntries;
    };
    void DiscoverPlaces(Region& g, std::vector<Fusion::Wanderers::Node>& list, uint32 regionIndex, std::map<uint32, Spot>& spots);
    void IndexPlaces(Region& g);
    void LoadYields(Region& g, std::map<uint32, Spot> const& spots);
    void AssignLife(FusionWanderer& w, bool fresh);
    bool FixHaunts(FusionWanderer& w);
    void Reborn(FusionWanderer& w, std::set<std::string>& names);
    Fusion::Wanderers::Haunts HauntsOf(FusionWanderer const& w) const;
    Fusion::Wanderers::DayContext DayFor(FusionWanderer const& w) const;
    // Its next plan from `from` (the life's PlanNext): where (a node index, -1 none) and what it
    // will do there; a bounty plan sets the goal to TakeBounty.
    int PlanLife(FusionWanderer& w, Fusion::Vec3 from, uint32 roll);
    // Arrived: start the planned activity (ms); done (or broken off): what it came to.
    uint32 StartActivity(FusionWanderer& w, uint32 now, Creature* c);
    void EndActivity(FusionWanderer& w, uint32 now, Creature* c);
    void ActivityPose(Creature* c, FusionWanderer const& w) const;
    Fusion::Wanderers::Talk TalkOf(FusionWanderer const& w, std::string const& to, Creature const* c) const;
    std::string NodeName(FusionWanderer const& w, uint32 nodeId) const;
    std::string PlaceNear(Region const& r, Fusion::Vec3 p) const;
    Fusion::Wanderers::Price PriceOf(uint32 item) const;
    void News(FusionWanderer& w, Fusion::Wanderers::NewsKind kind, std::string const& what, std::string const& where);
    bool Speak(Creature* c, FusionWanderer& w, std::string const& line, Unit const* to = nullptr, bool yell = false, bool force = false);
    bool LifeInWorld(Creature* c, FusionWanderer& w, uint32 now);
    void MeetOnPaper(uint32 now);
    void TakeEvents();
    void TickLives(uint32 now);
    void EndHire(Creature* c, FusionWanderer& w, bool sayBye);
    std::vector<std::string> LifeInfo(FusionWanderer const& w) const;

    bool m_enabled = false;
    uint32 m_perZone = 3;
    uint32 m_maxSpawned = 24;
    uint32 m_perPlayer = 8;
    float m_spawnRadius = 200.f;
    float m_despawnRadius = 300.f;
    uint32 m_unseenPerTick = 25;
    uint32 m_saveEveryMs = 60000;
    bool m_chat = true;
    bool m_bounties = true;
    std::vector<uint32> m_zoneFilter;
    // Phase 2 knobs.
    bool m_lives = true;
    bool m_trade = true;
    bool m_hire = true;
    uint32 m_lifeDays = 60;
    uint32 m_linesPerMinute = 20;
    uint32 m_lineGapMs = 40000;
    uint32 m_spotsPerZone = 3;
    std::map<uint32, Fusion::Wanderers::ChatBudget> m_budget;  // by map (made at Load: no inserts after)
    std::mutex m_eventLock;
    std::vector<WorldEvent> m_events;
    uint32 m_lifeTickMs = 0;
    uint32 m_meetMs = 0;

    std::vector<Region> m_regions;
    std::vector<std::shared_ptr<FusionWanderer>> m_roster;
    // Dressed NPC models by race and gender: [race][gender].
    std::map<uint8, std::vector<uint32>> m_looks[2];
    // Where each creature of the regions spawns (for a bounty's hunting ground).
    std::unordered_map<uint32, std::vector<Fusion::Vec3>> m_spawnsOf;
    // Authored roads (node id pairs), for the networks.
    std::vector<std::pair<uint32, uint32>> m_roads;

    uint32 m_tickMs = 0;
    uint32 m_saveMs = 0;
    size_t m_unseenCursor = 0;
};

#define sWandererMgr WandererMgr::Instance()

#endif
