/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_MATCH_H
#define FUSION_MATCH_H

#include "fusion/Common.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Battlegrounds as MW2 modes (design doc section 13): Warsong Gulch is Capture the Flag, Arathi
// Basin Domination, Alterac Valley Ground War. Each keeps vanilla's own rules and win condition;
// the fusion adds MW2's points (a kill, a capture, a defence), a match HUD and a scoreboard.
//
// Team Deathmatch is new, and is played in the Gurubashi Arena (Stranglethorn): vanilla already
// makes it a free-for-all PvP area, so no queue or instance is needed. Inside it the Alliance
// and the Horde score a point a kill against the other; the first team to the score limit, or the
// one ahead when the time runs out, wins; then a short intermission and a new match. A player who
// dies inside comes back in the arena after a few seconds (MW2's respawn), with no corpse run.
namespace Fusion::Match
{
    enum class Mode : uint8_t
    {
        None = 0,
        CaptureTheFlag = 1,     // Warsong Gulch (map 489)
        Domination = 2,         // Arathi Basin (map 529)
        GroundWar = 3,          // Alterac Valley (map 30)
        TeamDeathmatch = 4,     // the Gurubashi Arena (map 0)
    };
    char const* ModeName(Mode m);
    Mode ModeOfMap(uint32_t mapId);

    // What scores (vanilla's battleground score types map onto these).
    enum class Event : uint8_t
    {
        Kill = 1,
        Death = 2,
        FlagCapture = 3,
        FlagReturn = 4,
        BaseCapture = 5,        // a base, tower or graveyard assaulted
        BaseDefend = 6,         // and defended
        Objective = 7,          // anything else a mode counts
    };
    // MW2's points for an event in a mode.
    uint32_t Points(Mode m, Event e);
    // Vanilla's BattleGround score types (BattleGroundDefines.h ScoreType) as events; false for
    // those that are not one (honor).
    bool EventOfScoreType(uint32_t scoreType, Event& out);

    // Teams by vanilla's BattleGroundTeamIndex: 0 Alliance, 1 Horde.
    constexpr uint8_t kAlliance = 0, kHorde = 1, kNoTeam = 2;

    struct Row
    {
        uint64_t guid = 0;
        std::string name;
        uint8_t team = kNoTeam;
        uint32_t score = 0;
        uint32_t kills = 0;
        uint32_t deaths = 0;
        uint32_t objectives = 0;    // captures, returns, defences
    };

    // The players' points in one battleground or match.
    class Board
    {
    public:
        // Add a player (or rename / re-team one already in).
        void Join(uint64_t guid, std::string const& name, uint8_t team);
        // Count an event; returns the points it gave (0 for a player not on the board).
        uint32_t Record(Mode mode, uint64_t guid, Event e);
        Row const* Find(uint64_t guid) const;
        // Best first: score, then kills, then fewer deaths.
        std::vector<Row> Sorted() const;
        uint32_t TeamScore(uint8_t team) const;
        size_t Size() const { return m_rows.size(); }
        void Clear() { m_rows.clear(); }

    private:
        std::map<uint64_t, Row> m_rows;
    };

    // ── Team Deathmatch in the Gurubashi Arena ──
    constexpr uint32_t kArenaMap = 0;
    // The arena floor's middle (the treasure chest's spot) and its reach.
    constexpr Vec3 kArenaCentre{-13202.9f, 276.8f, 21.86f};
    constexpr float kArenaRadius = 40.f;
    constexpr float kArenaTop = 33.f;           // above this is the stands, not the floor
    bool InArena(uint32_t mapId, Vec3 p);
    // Where the dead come back: eight spots around the floor, `i` modulo 8.
    Vec3 ArenaSpawn(uint32_t i);

    struct DeathmatchRules
    {
        uint32_t scoreLimit = 30;           // kills by a team
        uint32_t timeLimitMs = 10 * 60 * 1000;
        uint32_t respawnMs = 5000;
        uint32_t intermissionMs = 15000;
    };

    class Deathmatch
    {
    public:
        enum class Phase : uint8_t { Waiting = 0, Running = 1, Intermission = 2 };

        explicit Deathmatch(DeathmatchRules rules = {}) : m_rules(rules) {}

        // A kill inside the arena: an enemy team's player killed scores for the killer's team
        // and the killer; the victim's death counts either way. Starts the clock with the first
        // kill. Returns true when this kill ended the match.
        bool OnKill(uint64_t killer, uint8_t killerTeam, uint64_t victim, uint8_t victimTeam, uint32_t nowMs);
        // The clock: a match past its time ends; an intermission past its time starts a fresh
        // board. Returns true on the update the match ended.
        bool Update(uint32_t nowMs);
        void Join(uint64_t guid, std::string const& name, uint8_t team) { m_board.Join(guid, name, team); }

        Phase CurrentPhase() const { return m_phase; }
        Board const& Scores() const { return m_board; }
        uint32_t TeamKills(uint8_t team) const { return team < 2 ? m_teamKills[team] : 0; }
        // The winner of the last match ended (kNoTeam for a draw), valid in the intermission.
        uint8_t Winner() const { return m_winner; }
        // Milliseconds left of the running match or of the intermission (0 waiting).
        uint32_t MsLeft(uint32_t nowMs) const;
        DeathmatchRules const& Rules() const { return m_rules; }

    private:
        void End(uint32_t nowMs);

        DeathmatchRules m_rules;
        Phase m_phase = Phase::Waiting;
        uint32_t m_startedMs = 0;
        uint32_t m_endedMs = 0;
        uint32_t m_teamKills[2] = {0, 0};
        uint8_t m_winner = kNoTeam;
        Board m_board;
    };

    // ── The wire, on the prestige channel (Prestige.h kChannelPrestige), ops from 20 ──
    constexpr uint8_t kOpHud = 20;          // server: the match HUD
    constexpr uint8_t kOpScoreboard = 21;   // server: the scoreboard; client: send it (Tab held)
    constexpr uint8_t kOpMatchEnd = 22;     // server: winner u8 · text str16

    struct Hud
    {
        Mode mode = Mode::None;
        uint8_t team = kNoTeam;             // the player's
        uint8_t phase = 0;                  // Deathmatch::Phase (1 for a battleground)
        uint32_t limit = 0;                 // the score limit (0 none shown)
        uint32_t msLeft = 0;
        uint32_t teamScore[2] = {0, 0};
        uint32_t score = 0, kills = 0, deaths = 0;
    };
    std::vector<uint8_t> EncodeHud(Hud const& h);
    bool DecodeHud(std::vector<uint8_t> const& body, Hud& out);
    // The rows, best first (at most 40, the rest left off).
    std::vector<uint8_t> EncodeScoreboard(Mode mode, std::vector<Row> const& rows);
    bool DecodeScoreboard(std::vector<uint8_t> const& body, Mode& mode, std::vector<Row>& rows);
    std::vector<uint8_t> EncodeMatchEnd(uint8_t winner, std::string const& text);
}

#endif
