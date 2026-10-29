/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Match.h"
#include "fusion/Prestige.h"

#include <algorithm>

namespace Fusion::Match
{
    namespace
    {
        void PutU16(std::vector<uint8_t>& b, uint32_t v)
        {
            v = std::min<uint32_t>(v, 0xFFFF);
            b.push_back(uint8_t(v));
            b.push_back(uint8_t(v >> 8));
        }
        void PutU32(std::vector<uint8_t>& b, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b.push_back(uint8_t(v >> (8 * i)));
        }

        struct In
        {
            std::vector<uint8_t> const& b;
            size_t at = 0;
            bool ok = true;
            uint8_t U8()
            {
                if (at + 1 > b.size()) { ok = false; return 0; }
                return b[at++];
            }
            uint16_t U16()
            {
                uint16_t const lo = U8();
                return uint16_t(lo | uint16_t(U8()) << 8);
            }
            uint32_t U32()
            {
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(U8()) << (8 * i);
                return v;
            }
            std::string Str8()
            {
                size_t const n = U8();
                if (!ok || at + n > b.size()) { ok = false; return {}; }
                std::string s(b.begin() + at, b.begin() + at + n);
                at += n;
                return s;
            }
        };
    }

    char const* ModeName(Mode m)
    {
        switch (m)
        {
            case Mode::CaptureTheFlag: return "Capture the Flag";
            case Mode::Domination: return "Domination";
            case Mode::GroundWar: return "Ground War";
            case Mode::TeamDeathmatch: return "Team Deathmatch";
            default: return "";
        }
    }

    Mode ModeOfMap(uint32_t mapId)
    {
        switch (mapId)
        {
            case 489: return Mode::CaptureTheFlag;
            case 529: return Mode::Domination;
            case 30: return Mode::GroundWar;
            default: return Mode::None;
        }
    }

    uint32_t Points(Mode m, Event e)
    {
        switch (e)
        {
            case Event::Kill: return 100;
            case Event::Death: return 0;
            case Event::FlagCapture: return m == Mode::CaptureTheFlag ? 500 : 0;
            case Event::FlagReturn: return m == Mode::CaptureTheFlag ? 100 : 0;
            case Event::BaseCapture: return 200;
            case Event::BaseDefend: return 100;
            case Event::Objective: return 100;
        }
        return 0;
    }

    bool EventOfScoreType(uint32_t scoreType, Event& out)
    {
        switch (scoreType)
        {
            case 1: out = Event::Kill; return true;             // SCORE_KILLING_BLOWS
            case 2: out = Event::Death; return true;            // SCORE_DEATHS
            case 7: out = Event::FlagCapture; return true;      // SCORE_FLAG_CAPTURES
            case 8: out = Event::FlagReturn; return true;       // SCORE_FLAG_RETURNS
            case 9: case 11: case 13: out = Event::BaseCapture; return true;    // bases, graveyards, towers assaulted
            case 10: case 12: case 14: out = Event::BaseDefend; return true;    // and defended
            case 15: out = Event::Objective; return true;       // SCORE_SECONDARY_OBJECTIVES
            default: return false;                              // honorable kills, honor
        }
    }

    void Board::Join(uint64_t guid, std::string const& name, uint8_t team)
    {
        Row& r = m_rows[guid];
        r.guid = guid;
        r.name = name;
        r.team = team;
    }

    uint32_t Board::Record(Mode mode, uint64_t guid, Event e)
    {
        auto it = m_rows.find(guid);
        if (it == m_rows.end())
            return 0;
        Row& r = it->second;
        uint32_t const points = Points(mode, e);
        r.score += points;
        switch (e)
        {
            case Event::Kill: ++r.kills; break;
            case Event::Death: ++r.deaths; break;
            default: ++r.objectives; break;
        }
        return points;
    }

    Row const* Board::Find(uint64_t guid) const
    {
        auto it = m_rows.find(guid);
        return it == m_rows.end() ? nullptr : &it->second;
    }

    std::vector<Row> Board::Sorted() const
    {
        std::vector<Row> rows;
        for (auto const& kv : m_rows)
            rows.push_back(kv.second);
        std::stable_sort(rows.begin(), rows.end(), [](Row const& a, Row const& b) {
            if (a.score != b.score)
                return a.score > b.score;
            if (a.kills != b.kills)
                return a.kills > b.kills;
            return a.deaths < b.deaths;
        });
        return rows;
    }

    uint32_t Board::TeamScore(uint8_t team) const
    {
        uint32_t s = 0;
        for (auto const& kv : m_rows)
            if (kv.second.team == team)
                s += kv.second.score;
        return s;
    }

    bool InArena(uint32_t mapId, Vec3 p)
    {
        if (mapId != kArenaMap || p.z > kArenaTop || p.z < kArenaCentre.z - 6.f)
            return false;
        float const dx = p.x - kArenaCentre.x, dy = p.y - kArenaCentre.y;
        return dx * dx + dy * dy <= kArenaRadius * kArenaRadius;
    }

    Vec3 ArenaSpawn(uint32_t i)
    {
        float const a = float(i % 8) * 6.2831853f / 8.f;
        float const r = 26.f;
        return {kArenaCentre.x + r * std::cos(a), kArenaCentre.y + r * std::sin(a), kArenaCentre.z};
    }

    bool Deathmatch::OnKill(uint64_t killer, uint8_t killerTeam, uint64_t victim, uint8_t victimTeam, uint32_t nowMs)
    {
        if (m_phase == Phase::Intermission)
            return false;
        if (m_phase == Phase::Waiting)
        {
            m_phase = Phase::Running;
            m_startedMs = nowMs;
        }
        m_board.Record(Mode::TeamDeathmatch, victim, Event::Death);
        if (killer == victim || killerTeam >= 2 || killerTeam == victimTeam)
            return false;
        m_board.Record(Mode::TeamDeathmatch, killer, Event::Kill);
        ++m_teamKills[killerTeam];
        if (m_teamKills[killerTeam] >= m_rules.scoreLimit)
        {
            End(nowMs);
            return true;
        }
        return false;
    }

    bool Deathmatch::Update(uint32_t nowMs)
    {
        if (m_phase == Phase::Running && nowMs - m_startedMs >= m_rules.timeLimitMs)
        {
            End(nowMs);
            return true;
        }
        if (m_phase == Phase::Intermission && nowMs - m_endedMs >= m_rules.intermissionMs)
        {
            // A fresh match: the scores go, the players stay on the board.
            Board fresh;
            for (Row const& r : m_board.Sorted())
                fresh.Join(r.guid, r.name, r.team);
            m_board = fresh;
            m_teamKills[0] = m_teamKills[1] = 0;
            m_phase = Phase::Waiting;
        }
        return false;
    }

    void Deathmatch::End(uint32_t nowMs)
    {
        m_phase = Phase::Intermission;
        m_endedMs = nowMs;
        m_winner = m_teamKills[0] > m_teamKills[1] ? kAlliance : m_teamKills[1] > m_teamKills[0] ? kHorde : kNoTeam;
    }

    uint32_t Deathmatch::MsLeft(uint32_t nowMs) const
    {
        switch (m_phase)
        {
            case Phase::Running:
            {
                uint32_t const gone = nowMs - m_startedMs;
                return gone >= m_rules.timeLimitMs ? 0 : m_rules.timeLimitMs - gone;
            }
            case Phase::Intermission:
            {
                uint32_t const gone = nowMs - m_endedMs;
                return gone >= m_rules.intermissionMs ? 0 : m_rules.intermissionMs - gone;
            }
            default:
                return 0;
        }
    }

    std::vector<uint8_t> EncodeHud(Hud const& h)
    {
        std::vector<uint8_t> b{Prestige::kVersion, kOpHud, uint8_t(h.mode), h.team, h.phase};
        PutU32(b, h.limit);
        PutU32(b, h.msLeft);
        PutU32(b, h.teamScore[0]);
        PutU32(b, h.teamScore[1]);
        PutU32(b, h.score);
        PutU16(b, h.kills);
        PutU16(b, h.deaths);
        return b;
    }

    bool DecodeHud(std::vector<uint8_t> const& body, Hud& out)
    {
        In in{body};
        if (in.U8() != Prestige::kVersion || in.U8() != kOpHud)
            return false;
        out.mode = Mode(in.U8());
        out.team = in.U8();
        out.phase = in.U8();
        out.limit = in.U32();
        out.msLeft = in.U32();
        out.teamScore[0] = in.U32();
        out.teamScore[1] = in.U32();
        out.score = in.U32();
        out.kills = in.U16();
        out.deaths = in.U16();
        return in.ok;
    }

    std::vector<uint8_t> EncodeScoreboard(Mode mode, std::vector<Row> const& rows)
    {
        size_t const n = std::min<size_t>(rows.size(), 40);
        std::vector<uint8_t> b{Prestige::kVersion, kOpScoreboard, uint8_t(mode), uint8_t(n)};
        for (size_t i = 0; i < n; ++i)
        {
            Row const& r = rows[i];
            b.push_back(r.team);
            PutU32(b, r.score);
            PutU16(b, r.kills);
            PutU16(b, r.deaths);
            PutU16(b, r.objectives);
            size_t const len = std::min<size_t>(r.name.size(), 48);
            b.push_back(uint8_t(len));
            b.insert(b.end(), r.name.begin(), r.name.begin() + len);
        }
        return b;
    }

    bool DecodeScoreboard(std::vector<uint8_t> const& body, Mode& mode, std::vector<Row>& rows)
    {
        In in{body};
        if (in.U8() != Prestige::kVersion || in.U8() != kOpScoreboard)
            return false;
        mode = Mode(in.U8());
        uint8_t const n = in.U8();
        rows.clear();
        for (uint8_t i = 0; i < n && in.ok; ++i)
        {
            Row r;
            r.team = in.U8();
            r.score = in.U32();
            r.kills = in.U16();
            r.deaths = in.U16();
            r.objectives = in.U16();
            r.name = in.Str8();
            rows.push_back(r);
        }
        return in.ok;
    }

    std::vector<uint8_t> EncodeMatchEnd(uint8_t winner, std::string const& text)
    {
        std::vector<uint8_t> b{Prestige::kVersion, kOpMatchEnd, winner};
        PutU16(b, uint32_t(text.size()));
        b.insert(b.end(), text.begin(), text.begin() + std::min<size_t>(text.size(), 0xFFFF));
        return b;
    }
}
