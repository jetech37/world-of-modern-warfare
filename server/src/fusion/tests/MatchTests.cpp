/*
 * Azeroth Warfare: tests for MW2 modes in battlegrounds and the arena (Match.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Match.h"

using namespace Fusion;
using namespace Fusion::Match;

TEST(match_modes_by_map)
{
    CHECK(ModeOfMap(489) == Mode::CaptureTheFlag);
    CHECK(ModeOfMap(529) == Mode::Domination);
    CHECK(ModeOfMap(30) == Mode::GroundWar);
    CHECK(ModeOfMap(0) == Mode::None);
    CHECK(std::string(ModeName(Mode::TeamDeathmatch)) == "Team Deathmatch");
}

TEST(match_points_and_score_types)
{
    CHECK(Points(Mode::CaptureTheFlag, Event::Kill) == 100);
    CHECK(Points(Mode::CaptureTheFlag, Event::FlagCapture) == 500);
    CHECK(Points(Mode::Domination, Event::FlagCapture) == 0);
    CHECK(Points(Mode::Domination, Event::BaseCapture) == 200);
    CHECK(Points(Mode::GroundWar, Event::Death) == 0);
    Event e;
    CHECK(EventOfScoreType(1, e) && e == Event::Kill);
    CHECK(EventOfScoreType(7, e) && e == Event::FlagCapture);
    CHECK(EventOfScoreType(10, e) && e == Event::BaseDefend);
    CHECK(EventOfScoreType(13, e) && e == Event::BaseCapture);
    CHECK(!EventOfScoreType(3, e) && !EventOfScoreType(4, e));   // honor is not MW2's
}

TEST(match_board_sorts_best_first)
{
    Board b;
    b.Join(1, "Anduin", kAlliance);
    b.Join(2, "Garrosh", kHorde);
    b.Join(3, "Jaina", kAlliance);
    CHECK(b.Record(Mode::CaptureTheFlag, 2, Event::Kill) == 100);
    CHECK(b.Record(Mode::CaptureTheFlag, 1, Event::FlagCapture) == 500);
    CHECK(b.Record(Mode::CaptureTheFlag, 3, Event::Kill) == 100);
    b.Record(Mode::CaptureTheFlag, 3, Event::Death);
    CHECK(b.Record(Mode::CaptureTheFlag, 99, Event::Kill) == 0);  // not on the board
    std::vector<Row> const rows = b.Sorted();
    CHECK(rows.size() == 3);
    CHECK(rows[0].guid == 1 && rows[0].objectives == 1);
    CHECK(rows[1].guid == 2 && rows[2].guid == 3);              // the same score: fewer deaths first
    CHECK(b.TeamScore(kAlliance) == 600 && b.TeamScore(kHorde) == 100);
}

TEST(match_arena_bounds_and_spawns)
{
    CHECK(InArena(0, kArenaCentre));
    CHECK(!InArena(1, kArenaCentre));
    CHECK(!InArena(0, {kArenaCentre.x + 50.f, kArenaCentre.y, kArenaCentre.z}));
    CHECK(!InArena(0, {kArenaCentre.x, kArenaCentre.y, kArenaTop + 1.f}));
    for (uint32_t i = 0; i < 16; ++i)
        CHECK(InArena(0, ArenaSpawn(i)));
    CHECK((ArenaSpawn(0) - ArenaSpawn(4)).Length() > 40.f);
}

TEST(match_deathmatch_runs_ends_and_restarts)
{
    DeathmatchRules rules;
    rules.scoreLimit = 3;
    rules.timeLimitMs = 60000;
    rules.intermissionMs = 1000;
    Deathmatch m(rules);
    m.Join(1, "Anduin", kAlliance);
    m.Join(2, "Garrosh", kHorde);
    m.Join(3, "Varian", kAlliance);
    CHECK(m.CurrentPhase() == Deathmatch::Phase::Waiting && m.MsLeft(0) == 0);
    CHECK(!m.OnKill(1, kAlliance, 2, kHorde, 100));
    CHECK(m.CurrentPhase() == Deathmatch::Phase::Running && m.MsLeft(100) == 60000);
    CHECK(!m.OnKill(1, kAlliance, 3, kAlliance, 200));          // a teammate: no point
    CHECK(m.TeamKills(kAlliance) == 1);
    CHECK(m.Scores().Find(3)->deaths == 1 && m.Scores().Find(1)->kills == 1);
    CHECK(!m.OnKill(2, kHorde, 2, kHorde, 300));                // a fall: a death only
    CHECK(!m.OnKill(3, kAlliance, 2, kHorde, 400));
    CHECK(m.OnKill(1, kAlliance, 2, kHorde, 500));              // the third: the end
    CHECK(m.CurrentPhase() == Deathmatch::Phase::Intermission && m.Winner() == kAlliance);
    CHECK(!m.OnKill(2, kHorde, 1, kAlliance, 600));             // nothing counts now
    CHECK(m.TeamKills(kHorde) == 0);
    CHECK(!m.Update(1400));
    CHECK(m.CurrentPhase() == Deathmatch::Phase::Intermission);
    m.Update(1500);
    CHECK(m.CurrentPhase() == Deathmatch::Phase::Waiting);
    CHECK(m.TeamKills(kAlliance) == 0 && m.Scores().Size() == 3 && m.Scores().Find(1)->kills == 0);
    // Time runs out: the side ahead wins, level is a draw.
    m.OnKill(2, kHorde, 1, kAlliance, 2000);
    CHECK(m.Update(2000 + rules.timeLimitMs) && m.Winner() == kHorde);
    Deathmatch draw(rules);
    draw.OnKill(1, kAlliance, 1, kAlliance, 0);
    CHECK(draw.Update(rules.timeLimitMs) && draw.Winner() == kNoTeam);
}

TEST(match_wire_round_trips)
{
    Hud h;
    h.mode = Mode::TeamDeathmatch;
    h.team = kHorde;
    h.phase = 1;
    h.limit = 30;
    h.msLeft = 123456;
    h.teamScore[0] = 7;
    h.teamScore[1] = 9;
    h.score = 900;
    h.kills = 9;
    h.deaths = 2;
    Hud back;
    CHECK(DecodeHud(EncodeHud(h), back));
    CHECK(back.mode == Mode::TeamDeathmatch && back.team == kHorde && back.msLeft == 123456);
    CHECK(back.teamScore[1] == 9 && back.score == 900 && back.deaths == 2);
    Board b;
    b.Join(5, "Sylvanas", kHorde);
    b.Record(Mode::Domination, 5, Event::BaseCapture);
    Mode mode;
    std::vector<Row> rows;
    CHECK(DecodeScoreboard(EncodeScoreboard(Mode::Domination, b.Sorted()), mode, rows));
    CHECK(mode == Mode::Domination && rows.size() == 1 && rows[0].name == "Sylvanas" && rows[0].score == 200);
    std::vector<uint8_t> cut = EncodeScoreboard(Mode::Domination, b.Sorted());
    cut.pop_back();
    CHECK(!DecodeScoreboard(cut, mode, rows));
}
