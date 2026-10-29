/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_BOUNTIES_H
#define FUSION_BOUNTIES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Bounty boards: a notice board in the towns and outposts of the open-world zones posts a few
// bounties for its zone each day (server time), made from what really lives there: kill a number
// of a creature that spawns in the zone, collect an item its creatures drop, clear a camp of
// mixed creatures, or hunt a named rare or elite for a better purse. The rules here are plain
// data: which creatures and items may be a bounty (the same "ordinary creature" rules as the
// quest tuning, 22_quest_tuning.sql), the day's pick for a zone (deterministic: the same day,
// zone and pool always post the same bounties), the rewards by level, the daily cap and the
// board's wire. The game (FusionBounties.cpp) gathers the pools from the world database at
// startup and runs each day's bounties as quests in a reserved id range, so they sit in the quest
// log and the quest tracker like any other quest.
//
// Assignments are generated from the live world data: a daily board rather than a fixed campaign.
namespace Fusion::Bounty
{
    enum class Kind : uint8_t
    {
        Kill = 1,       // kill N of one creature
        Collect = 2,    // bring N of an item the zone's creatures drop
        Hunt = 3,       // kill one named rare or elite
        Camp = 4,       // clear a camp: N of each of two or three creatures that live together
    };
    char const* KindName(Kind k);

    // ── Days ────────────────────────────────────────────────────────────────────────────────

    // Days from 1970-01-01 to a civil date (the proleptic Gregorian calendar; month 1-12).
    int64_t DaysFromCivil(int64_t year, unsigned month, unsigned day);

    // The server's local clock, broken down.
    struct LocalTime
    {
        int64_t year = 1970;
        unsigned month = 1, day = 1;
        unsigned hour = 0, minute = 0, second = 0;
    };

    // The bounty day a moment falls in: days count from 1970, and a day begins at `resetHour`
    // local time (before it, it is still the day before).
    uint32_t DayOf(LocalTime const& t, uint8_t resetHour);
    // Seconds until the next day begins.
    uint32_t SecondsToReset(LocalTime const& t, uint8_t resetHour);

    // A bounty runs through the day it was posted and the next, so one taken late in the day
    // still has a full day in it; after that it is withdrawn from the quest log.
    inline bool IsExpired(uint32_t postedDay, uint32_t today) { return today > postedDay + 1; }

    // ── Quest ids ───────────────────────────────────────────────────────────────────────────

    // Each day's bounties are quests in one of three blocks of a reserved range, by the day:
    // today's block, yesterday's (still running) and a third, refilled when its day comes round,
    // by when its bounties have expired everywhere. Ids are fixed per (block, zone, index), so the
    // client's per-session quest cache never shows a stale bounty under a reused id within a
    // session shorter than two days.
    constexpr uint32_t kQuestBase = 95000;
    constexpr uint32_t kBlocks = 3;
    constexpr uint32_t kBlockSize = 1000;
    constexpr uint32_t kPerZone = 5;                    // bounties a board posts each day
    constexpr uint32_t kMaxZones = kBlockSize / kPerZone;
    constexpr uint32_t kFirstQuest = kQuestBase;
    constexpr uint32_t kLastQuest = kQuestBase + kBlocks * kBlockSize - 1;

    inline uint32_t BlockOf(uint32_t day) { return day % kBlocks; }
    // 0 when the zone slot or index is out of range.
    uint32_t QuestId(uint32_t day, uint32_t zoneSlot, uint32_t index);
    inline bool IsBountyQuest(uint32_t questId) { return questId >= kFirstQuest && questId <= kLastQuest; }

    struct QuestSlot
    {
        uint32_t block = 0;
        uint32_t zoneSlot = 0;
        uint32_t index = 0;
    };
    bool SlotOf(uint32_t questId, QuestSlot& out);

    // ── What may be a bounty ────────────────────────────────────────────────────────────────

    // A zone's level band (the board's row): targets are taken a little around it.
    struct ZoneBand
    {
        uint8_t minLevel = 1;
        uint8_t maxLevel = 60;
    };

    // 1.12's creature ranks and the types a bounty never asks for.
    constexpr uint8_t kRankNormal = 0, kRankElite = 1, kRankRareElite = 2, kRankBoss = 3, kRankRare = 4;
    constexpr uint8_t kTypeCritter = 8, kTypeNotSpecified = 10, kTypeTotem = 11;

    // A creature entry as the world database has it, over all its open-world spawns.
    struct CreatureFacts
    {
        uint32_t entry = 0;
        uint8_t minLevel = 1, maxLevel = 1;
        uint8_t rank = kRankNormal;         // the highest rank over its patch versions
        uint8_t type = 0;
        bool townsfolk = false;             // npc_flags, a guard, an invisible trigger or a civilian
        bool hostile = true;                // its faction is hostile to players or neutral to all
        bool unattackable = false;          // not selectable or not attackable
        uint32_t openWorldSpawns = 0;       // Eastern Kingdoms and Kalimdor, game events excluded
        uint32_t instanceSpawns = 0;
        uint32_t longestRespawnSecs = 0;    // its slowest open-world spawn
    };

    inline uint8_t LevelOf(CreatureFacts const& c) { return uint8_t((c.minLevel + c.maxLevel + 1) / 2); }

    // Spawns a kill target needs in the zone (and a camp member near its camp): enough that a
    // bounty is never a wait for one creature to come back.
    constexpr uint32_t kMinZoneSpawns = 6;
    // A rare or named elite comes back within this, so one killed today is up again today.
    constexpr uint32_t kHuntRespawnMax = 3 * 3600;
    // A named elite: an elite with no more than this many spawns in the world.
    constexpr uint32_t kNamedEliteSpawns = 2;
    // Targets a little outside the zone's band still count (a zone's edges overlap).
    constexpr uint8_t kBandSlackBelow = 1, kBandSlackAbove = 2;

    bool InBand(uint8_t level, ZoneBand band);
    // An ordinary creature of the zone (22_quest_tuning.sql's "ordinary": rank 0, at least four
    // open-world spawns, none in an instance, no townsfolk; ours: not a critter or totem, hostile
    // or neutral, attackable, in the band, kMinZoneSpawns in the zone, not our own content).
    bool IsKillTarget(CreatureFacts const& c, uint32_t spawnsInZone, ZoneBand band);
    // A rare (or rare elite) or a named elite of the zone, quick enough to come back.
    bool IsHuntTarget(CreatureFacts const& c, uint32_t spawnsInZone, ZoneBand band);

    // An item as the world database has it.
    struct ItemFacts
    {
        uint32_t entry = 0;
        uint8_t itemClass = 0, subClass = 0, quality = 0, bonding = 0;
        uint32_t stackable = 1;
        uint32_t maxCount = 0;
        bool startsQuest = false;
    };
    constexpr uint8_t kItemClassTradeGoods = 7, kItemClassMisc = 15;
    // A drop worth collecting: a stackable trade good or plain misc item (hides, meat, cloth,
    // fangs), poor or common, not bound, no limit on how many one carries, starting no quest.
    bool IsCollectItem(ItemFacts const& i);
    // The least drop chance (0-1) a collect bounty is made from: never a grind for a rare drop.
    constexpr float kMinDropChance = 0.15f;

    // ── Pools: a zone's validated targets ───────────────────────────────────────────────────

    struct KillCandidate
    {
        uint32_t entry = 0;
        std::string name;
        uint8_t level = 1;
        uint32_t spawns = 0;        // in the zone
        uint32_t family = 0;        // its faction template: creatures of a camp share one
    };

    struct CollectCandidate
    {
        uint32_t item = 0;
        std::string itemName;
        uint32_t dropper = 0;       // the zone's commonest creature that drops it
        std::string dropperName;
        uint8_t level = 1;          // the dropper's
        float chance = 0.f;         // per kill, 0-1
    };

    struct HuntCandidate
    {
        uint32_t entry = 0;
        std::string name;
        uint8_t level = 1;
        bool elite = false;
    };

    struct Camp
    {
        std::vector<KillCandidate> members;     // two or three, sorted by entry
        uint8_t level = 1;                      // the highest member's
        uint32_t area = 0;                      // where it stands (an AreaTable id)
        std::string place;                      // the area's name, filled by the game
    };

    // One spawn of a kill candidate, for finding camps.
    struct SpawnPoint
    {
        uint32_t entry = 0;
        float x = 0.f, y = 0.f;
        uint32_t area = 0;
    };

    // Camps: around a candidate's spawns, the other candidates of its family and a level close to
    // it with at least kCampMinEach spawns within kCampRadius; two or three members (the ones
    // with most spawns there), one camp per member set. Sorted by level, then members.
    constexpr float kCampRadius = 50.f;
    constexpr uint32_t kCampMinEach = 3;
    constexpr uint8_t kCampLevelSpread = 3;
    std::vector<Camp> FindCamps(std::vector<KillCandidate> const& kills, std::vector<SpawnPoint> const& spawns);

    struct ZonePool
    {
        uint32_t zone = 0;
        std::string zoneName;
        ZoneBand band;
        std::vector<KillCandidate> kills;
        std::vector<CollectCandidate> collects;
        std::vector<HuntCandidate> hunts;
        std::vector<Camp> camps;

        bool Empty() const { return kills.empty() && collects.empty() && hunts.empty() && camps.empty(); }
    };

    // ── Rewards ─────────────────────────────────────────────────────────────────────────────

    struct Reward
    {
        uint32_t xp = 0;
        uint32_t money = 0;             // copper
        uint32_t moneyMaxLevel = 0;     // copper instead of the experience at the level cap
        int32_t reputation = 0;         // with the board's faction, when it has one
    };

    // A bounty's purse by its level: about a vanilla kill quest's experience at that level
    // (60L + 0.6L^2), coin growing with the square of the level, more for a camp (x1.25), a hunt
    // (x1.5) and an elite hunt (x2); reputation 75, 100 a camp, 150 a hunt.
    Reward RewardFor(Kind kind, uint8_t level, bool elite);

    // The lowest level that may take a bounty of `level`: a hunt asks for more.
    uint8_t MinLevelFor(Kind kind, uint8_t level);

    // ── The day's bounties ──────────────────────────────────────────────────────────────────

    // Objective counts: the quest log keeps each in 6 bits (22_quest_tuning.sql), so 63 at most.
    constexpr uint8_t kMaxObjectiveCount = 63;

    struct Objective
    {
        uint32_t creature = 0;
        uint8_t count = 0;
        std::string name;
    };

    struct Bounty
    {
        uint32_t questId = 0;
        Kind kind = Kind::Kill;
        uint32_t zone = 0;
        uint8_t level = 1;
        uint8_t minLevel = 1;
        bool elite = false;
        std::vector<Objective> kills;       // at most 4 (the quest's creature objectives)
        uint32_t item = 0;
        uint8_t itemCount = 0;
        std::string itemName;
        Reward reward;
        std::string title;                  // "Bounty: Defias Pillager"
        std::string objective;              // one line for the board: "Kill 12 Defias Pillagers"
        std::string objectives;             // the quest log's objectives text
        std::string details;                // the quest log's description
        uint32_t key = 0;                   // what it asks, hashed: a held bounty is the posted one
    };

    // The day's bounties for a zone: up to kPerZone. With enough candidates, a kill from each
    // third of the zone's level range (low, middle, high), a collect, and a hunt or a camp; with
    // fewer, whatever the pool holds. `salt` re-rolls a day (a GM's refresh); 0 normally.
    std::vector<Bounty> Post(ZonePool const& pool, uint32_t zoneSlot, uint32_t day, uint32_t salt = 0);

    // "Wolf" -> "Wolves", "Defias Pillager" -> "Defias Pillagers": the English plural the
    // objectives read with.
    std::string Plural(std::string const& name);

    // ── Taking and turning in ───────────────────────────────────────────────────────────────

    // Bounties a character may turn in a day.
    constexpr uint32_t kDailyCap = 5;
    // How near a board a player must stand to take or turn in (yards).
    constexpr float kBoardReach = 10.f;

    enum class Refusal : uint8_t
    {
        None = 0,
        NotPosted = 1,      // not on this board today
        OtherZone = 2,      // a bounty of another zone
        Level = 3,          // below its level
        Held = 4,           // already in the quest log
        DoneToday = 5,      // already turned in today
        Cap = 6,            // the day's bounties are used up
        LogFull = 7,
        Expired = 8,
        NotComplete = 9,
        TooFar = 10,        // walked away from the board
        WrongTeam = 11,     // the other faction's board
        NotHeld = 12,
    };
    // What the board says for a refusal.
    char const* RefusalText(Refusal r);

    struct AcceptCheck
    {
        bool nearBoard = true;
        bool teamAllowed = true;
        bool posted = true;             // on this board's list today
        uint8_t playerLevel = 1;
        uint8_t minLevel = 1;
        bool inLog = false;
        bool doneToday = false;         // this bounty
        uint32_t doneCount = 0;         // bounties turned in today
        uint32_t heldCount = 0;         // bounties in the quest log now
        bool logFull = false;
    };
    // Taking one: held and done today together stay within the cap, so every bounty taken can be
    // turned in today.
    Refusal CanAccept(AcceptCheck const& c);

    struct TurnInCheck
    {
        bool nearBoard = true;
        bool teamAllowed = true;
        bool held = true;
        bool sameZone = true;
        bool expired = false;
        bool complete = true;
        uint32_t doneCount = 0;
    };
    Refusal CanTurnIn(TurnInCheck const& c);

    // ── The board's window: SMSG/CMSG_FUSION_CHANNEL 11, little-endian ──────────────────────
    // Channels 1-10 are taken (Packets.h build 1 and world 2, achievements 3, companions 4,
    // downed 5, stats 6, classes 7, prestige 8, feedback 9, action systems 10).
    // Client to server: [version, op, ...]
    //   op 1 ask:     the board again (the open board, or nothing)
    //   op 2 accept:  quest u32
    //   op 3 turn in: quest u32
    // Server to client: [version, op, ...]
    //   op 1 board:   open u8, zone u32, town str8, zone name str8, done u8, cap u8, level u8,
    //                 seconds to reset u32, rows u8, then per row: quest u32, kind u8, state u8,
    //                 level u8, min level u8, elite u8, xp u32, money u32, reputation u16,
    //                 faction str8, title str8, objective str16, details str16, progress u8,
    //                 then per objective: have u8, need u8, label str8
    //   op 2 result:  ok u8, text str8
    constexpr uint8_t kChannelBounty = 11;
    constexpr uint8_t kVersion = 1;
    constexpr uint8_t kOpAsk = 1;
    constexpr uint8_t kOpAccept = 2;
    constexpr uint8_t kOpTurnIn = 3;
    constexpr uint8_t kOpBoard = 1;
    constexpr uint8_t kOpResult = 2;

    // A row's state for this player.
    enum class RowState : uint8_t
    {
        Available = 0,
        Accepted = 1,       // in the log, not done yet
        Ready = 2,          // complete: turn it in
        Done = 3,           // turned in today
        Locked = 4,         // below its level
        Elsewhere = 5,      // held, another zone's: turned in at that zone's boards
        Full = 6,           // the day's cap is reached
    };

    struct Progress
    {
        uint8_t have = 0;
        uint8_t need = 0;
        std::string label;
    };

    struct Row
    {
        uint32_t questId = 0;
        Kind kind = Kind::Kill;
        RowState state = RowState::Available;
        uint8_t level = 1;
        uint8_t minLevel = 1;
        bool elite = false;
        uint32_t xp = 0;
        uint32_t money = 0;
        uint16_t reputation = 0;
        std::string repName;            // "" for none
        std::string title;
        std::string objective;
        std::string details;
        std::vector<Progress> progress;  // the objectives' counts (empty for a bounty not held)
    };

    struct Board
    {
        bool open = false;              // open the window (a board was used), else refresh it
        uint32_t zone = 0;
        std::string town;
        std::string zoneName;
        uint8_t done = 0;
        uint8_t cap = kDailyCap;
        uint8_t level = 1;              // the player's, for the rows' difficulty colours
        uint32_t secondsToReset = 0;
        std::vector<Row> rows;
    };

    std::vector<uint8_t> EncodeBoard(Board const& b);
    bool DecodeBoard(std::vector<uint8_t> const& body, Board& out);
    std::vector<uint8_t> EncodeResult(bool ok, std::string const& text);
    bool DecodeResult(std::vector<uint8_t> const& body, bool& ok, std::string& text);
    std::vector<uint8_t> EncodeAsk();
    std::vector<uint8_t> EncodeAccept(uint32_t questId);
    std::vector<uint8_t> EncodeTurnIn(uint32_t questId);
    // A client message: its op and quest (0 for an ask); false for a wrong version or size.
    bool DecodeRequest(std::vector<uint8_t> const& body, uint8_t& op, uint32_t& questId);
}

#endif
