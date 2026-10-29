/*
 * World of Modern Warfare: the bounty boards. GPL-2.0-or-later.
 *
 * A Bounty Board (gameobject 191200, script go_fusion_bounty_board) stands in the towns and
 * outposts of the open-world zones (sql/custom/azeroth_warfare/27_bounty_boards.sql: the zones in
 * `aw_bounty_zone`, the boards in `aw_bounty_board`). Each zone's boards post up to five bounties
 * a day, made by fusion/Bounties.h from what really lives in the zone: at startup the server
 * reads every open-world creature spawn (game-event spawns left out), finds its zone from the
 * map files and keeps, per zone, the creatures, drops, rares and camps a bounty may ask for.
 *
 * The bounties are quests the server makes itself (ObjectMgr::AddGeneratedQuest) in the reserved
 * ids 95000-97999, three blocks of a thousand that take turns by the day: today's, yesterday's
 * (still running) and the one refilled at the next reset. So a bounty sits in the quest log and
 * the quest tracker, counts its kills and items as any quest does, and is turned in with the
 * board's own window (fusion channel 11): XP and coin by level, reputation with the board's
 * faction, and the "Bounty Hunter" achievements (Stat::BountiesCompleted).
 *
 * Each character may finish Bounty::kDailyCap a day; what it took is kept in the characters
 * database's `aw_bounty_char` (made here at startup), the day's re-roll in `aw_bounty_day`.
 * mangosd.conf: Bounty.Enable (default 1), Bounty.ResetHour (local server time, default 6).
 */

#ifndef AW_FUSION_BOUNTIES_H
#define AW_FUSION_BOUNTIES_H

#include "Common.h"
#include "ObjectGuid.h"
#include "fusion/Bounties.h"

#include <atomic>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class GameObject;
class Player;
class Quest;
struct FusionBountyRow;

class BountyMgr
{
public:
    static BountyMgr& Instance();

    // The board's gameobject entry (27_bounty_boards.sql).
    static constexpr uint32 kBoardEntry = 191200;

    // Startup, after the world database and the quests are loaded: the zones and boards, the
    // pools, the characters tables, and the quests of today, yesterday and the day before.
    void Load();
    bool IsEnabled() const { return m_enabled; }

    // The world thread, between map updates (World::Update): the day's turn and a GM's re-roll.
    void Update();

    // A board used (its script's GOHello): its window. True when handled.
    bool UseBoard(Player* player, GameObject* go);
    // Channel 11 (FusionMgr::HandleChannel), the player's map thread.
    void HandleChannel(Player* player, std::vector<uint8> const& body);
    // Player::Update: the rows loaded at the first update, and a bounty that has expired or was
    // withdrawn taken out of the quest log.
    void UpdatePlayer(Player* player, uint32 diffMs);

    // GM: `.bounty refresh` re-rolls today's bounties at the next world tick.
    void AskRefresh() { m_refreshAsked = true; }
    // GM: `.bounty list`: the day and the zone's bounties as lines.
    std::vector<std::string> Describe(uint32 zone) const;

    // Today's bounties of a zone that ask for kills (kill, hunt, camp), for the wanderers who
    // take them (FusionWanderers.cpp). Read from any map thread, as the board's window is.
    struct KillPosting
    {
        uint32 questId = 0;
        uint8 level = 1;
        bool elite = false;
        std::vector<std::pair<uint32, uint8>> kills;    // creature entry, count
        std::string title;
    };
    std::vector<KillPosting> KillPostings(uint32 zone) const;

private:
    struct ZoneRow
    {
        uint32 zone = 0;
        uint32 slot = 0;
        Fusion::Bounty::ZoneBand band;
        std::string name;
    };
    struct BoardRow
    {
        uint32 zone = 0;
        std::string town;
        uint8 team = 0;             // 0 any, 1 Alliance, 2 Horde
        uint32 repFaction = 0;
    };
    struct Posted
    {
        Fusion::Bounty::Bounty bounty;
        uint32 day = 0;
    };

    void LoadZones();
    void BuildPools();
    void MakeQuests();
    void PostDay(uint32 day);
    void Fill(Quest* q, Posted const* p) const;
    uint32 SaltOf(uint32 day) const;

    // The day and the seconds to its end, by the server's clock (the world thread).
    void Tick();

    BoardRow const* BoardOf(GameObject const* go) const;
    bool TeamAllows(BoardRow const& b, Player const* player) const;
    Posted const* PostedOf(uint32 questId) const;
    FusionBountyRow* RowOf(Player* player, uint32 questId, Posted const& p) const;
    uint32 DoneToday(Player* player) const;
    uint32 HeldCount(Player* player) const;
    GameObject* NearBoard(Player* player) const;

    void LoadRows(Player* player);
    void CheckLog(Player* player);
    void SendBoard(Player* player, GameObject* go, bool open);
    void SendResult(Player* player, bool ok, std::string const& text);
    void Accept(Player* player, uint32 questId);
    void TurnIn(Player* player, uint32 questId);

    bool m_enabled = true;
    uint8 m_resetHour = 6;

    std::map<uint32, ZoneRow> m_zones;                          // by zone (AreaTable id)
    std::unordered_map<uint32, BoardRow> m_boards;              // by the board's spawn guid
    std::map<uint32, Fusion::Bounty::ZonePool> m_pools;         // by zone
    std::unordered_map<uint32, Posted> m_posted;                // by quest id
    std::unordered_map<uint32, Quest*> m_quests;                // by quest id, every block's
    std::map<uint32, uint32> m_salts;                           // a GM's re-rolls, by day

    // Written by the world thread only; read from every map thread.
    std::atomic<uint32> m_today{0};
    std::atomic<uint32> m_generation{1};                        // bumped whenever a posting changes
    std::atomic<int64> m_nextReset{0};                          // unix seconds
    std::atomic<bool> m_refreshAsked{false};
};

#define sBountyMgr BountyMgr::Instance()

#endif
