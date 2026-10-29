/*
 * World of Modern Warfare: Minstrelsy (fusion/Minstrelsy.h). GPL-2.0-or-later, see Common.h.
 *
 * The chart generator and the judge are mirrored line for line by the client (client/src/bard/
 * rules.rs): integer arithmetic and the same random stream, so a seed makes the same chart on
 * both sides (MinstrelsyTests checks the client's hashes).
 */

#include "fusion/Minstrelsy.h"

#include "fusion/ActionSystems.h"

#include <algorithm>
#include <cmath>

namespace Fusion::Minstrelsy
{
    namespace
    {
        using AB = ActionSystems::Ability;
        constexpr uint32_t T(AB a) { return uint32_t(a); }

        // SplitMix64, its upper half: the client's `Rng` is the same.
        struct Rng
        {
            uint64_t s;
            uint32_t Next()
            {
                s += 0x9E3779B97F4A7C15ull;
                uint64_t z = s;
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                z ^= z >> 31;
                return uint32_t(z >> 32);
            }
            uint32_t Below(uint32_t n) { return n ? Next() % n : 0; }
        };

        void PutU16(std::vector<uint8_t>& b, uint16_t v)
        {
            b.push_back(uint8_t(v));
            b.push_back(uint8_t(v >> 8));
        }
        void PutU32(std::vector<uint8_t>& b, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b.push_back(uint8_t(v >> (8 * i)));
        }
        void PutU64(std::vector<uint8_t>& b, uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                b.push_back(uint8_t(v >> (8 * i)));
        }
        void PutStr(std::vector<uint8_t>& b, std::string const& s)
        {
            size_t const n = std::min<size_t>(s.size(), 255);
            b.push_back(uint8_t(n));
            b.insert(b.end(), s.begin(), s.begin() + n);
        }
        struct Reader
        {
            std::vector<uint8_t> const& b;
            size_t at = 0;
            bool ok = true;
            uint8_t U8()
            {
                if (at + 1 > b.size())
                {
                    ok = false;
                    return 0;
                }
                return b[at++];
            }
            uint16_t U16()
            {
                uint16_t v = U8();
                v = uint16_t(v | (uint16_t(U8()) << 8));
                return v;
            }
            uint32_t U32()
            {
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(U8()) << (8 * i);
                return v;
            }
            uint64_t U64()
            {
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= uint64_t(U8()) << (8 * i);
                return v;
            }
            std::string Str()
            {
                uint8_t const n = U8();
                if (!ok || at + n > b.size())
                {
                    ok = false;
                    return {};
                }
                std::string s(b.begin() + at, b.begin() + at + n);
                at += n;
                return s;
            }
            bool Done() const { return ok && at == b.size(); }
        };
    }

    std::array<InstrumentDef, kInstruments> const& Instruments()
    {
        // id, name, skill, item, tool, lanes, bpm, bars, grid, density, accent, chord, maxChord,
        // hold, holdMin, holdMax, run, perfect, good, scroll, floor, ceiling.
        static std::array<InstrumentDef, kInstruments> const rows = {{
            {Instrument::HandDrum, "Hand Drum", 1, 94101, T(AB::HandDrum),
                2, 88, 4, 4, 200, 56, 0, 1, 0, 0, 0, 0, 60, 140, 1800, 0.5f, 1.1f},
            {Instrument::Lute, "Lute", 50, 94102, T(AB::Lute),
                3, 96, 4, 2, 110, 110, 40, 2, 0, 0, 0, 40, 50, 120, 1600, 0.5f, 1.2f},
            {Instrument::Flute, "Flute", 100, 94103, T(AB::Flute),
                4, 100, 4, 2, 115, 105, 0, 1, 70, 2, 6, 60, 45, 110, 1500, 0.5f, 1.3f},
            {Instrument::Harp, "Harp", 150, 94104, T(AB::Harp),
                5, 108, 4, 2, 150, 100, 60, 3, 40, 2, 4, 90, 40, 95, 1350, 0.5f, 1.35f},
            {Instrument::Bagpipes, "Bagpipes", 225, 94105, T(AB::Bagpipes),
                6, 120, 4, 1, 90, 120, 30, 2, 50, 4, 8, 140, 35, 85, 1200, 0.5f, 1.45f},
            {Instrument::ThunderDrums, "Thunder Drums", 275, 94106, T(AB::ThunderDrums),
                6, 132, 4, 1, 120, 110, 90, 3, 20, 2, 4, 100, 30, 75, 1100, 0.5f, 1.5f},
        }};
        return rows;
    }

    InstrumentDef const* Def(Instrument i)
    {
        for (InstrumentDef const& d : Instruments())
            if (d.id == i)
                return &d;
        return nullptr;
    }

    InstrumentDef const* DefOfTool(uint32_t tool)
    {
        for (InstrumentDef const& d : Instruments())
            if (tool && d.tool == tool)
                return &d;
        return nullptr;
    }

    InstrumentDef const* DefOfItem(uint32_t item)
    {
        for (InstrumentDef const& d : Instruments())
            if (item && d.item == item)
                return &d;
        return nullptr;
    }

    uint32_t TickMs(uint16_t bpm, uint32_t tick)
    {
        return bpm ? uint32_t(uint64_t(tick) * 15000u / bpm) : 0;
    }

    Chart MakeChart(Instrument i, uint32_t seed)
    {
        Chart c;
        c.instrument = i;
        c.seed = seed;
        InstrumentDef const* d = Def(i);
        if (!d)
            return c;
        c.countInMs = TickMs(d->bpm, 16);
        c.beatMs = TickMs(d->bpm, 4);
        c.lengthMs = TickMs(d->bpm, 16u * (uint32_t(d->bars) + 1));
        Rng rng{(uint64_t(seed) << 8) ^ uint64_t(uint8_t(i)) ^ 0xA0761D6478BD642Full};
        uint32_t const grid = d->grid;
        uint32_t const steps = uint32_t(d->bars) * (16u / grid);
        struct Raw
        {
            uint32_t tick;
            uint32_t holdTicks;
            uint8_t lane;
        };
        std::vector<Raw> raw;
        std::array<uint32_t, kMaxLanes> busy{};
        int prev = -1;
        int dir = 1;
        auto freeLanes = [&](uint32_t tick) {
            std::vector<uint8_t> f;
            for (uint8_t l = 0; l < d->lanes; ++l)
                if (busy[l] <= tick)
                    f.push_back(l);
            return f;
        };
        for (uint32_t step = 0; step < steps; ++step)
        {
            uint32_t const tick = step * grid;
            bool const downbeat = tick % 16 == 0;
            bool const onBeat = tick % 4 == 0;
            uint32_t const chance = uint32_t(d->density) + (onBeat ? d->accent : 0u);
            uint32_t const roll = rng.Below(256);
            if (!downbeat && roll >= chance)
                continue;
            std::vector<uint8_t> const free = freeLanes(tick);
            if (free.empty())
                continue;
            int lane = -1;
            if (prev >= 0 && d->run > 0 && rng.Below(256) < d->run)
                for (int attempt = 0; attempt < 2 && lane < 0; ++attempt)
                {
                    int const cand = prev + dir;
                    if (cand >= 0 && cand < d->lanes && busy[cand] <= tick)
                        lane = cand;
                    else
                        dir = -dir;
                }
            if (lane < 0)
                lane = free[rng.Below(uint32_t(free.size()))];
            uint32_t holdSteps = 0;
            if (d->hold > 0 && rng.Below(256) < d->hold)
            {
                uint32_t const want = d->holdMin + rng.Below(uint32_t(d->holdMax - d->holdMin) + 1);
                holdSteps = std::min(want, steps - step - 1);
            }
            raw.push_back({tick, holdSteps * grid, uint8_t(lane)});
            busy[lane] = tick + (holdSteps + 1) * grid;
            if (d->maxChord > 1 && d->chord > 0 && rng.Below(256) < d->chord)
            {
                uint32_t const extra = 1 + rng.Below(uint32_t(d->maxChord) - 1);
                for (uint32_t e = 0; e < extra; ++e)
                {
                    std::vector<uint8_t> const more = freeLanes(tick);
                    if (more.empty())
                        break;
                    uint8_t const l2 = more[rng.Below(uint32_t(more.size()))];
                    raw.push_back({tick, 0, l2});
                    busy[l2] = tick + grid;
                }
            }
            prev = lane;
        }
        std::stable_sort(raw.begin(), raw.end(),
            [](Raw const& a, Raw const& b) { return a.tick != b.tick ? a.tick < b.tick : a.lane < b.lane; });
        c.notes.reserve(raw.size());
        for (Raw const& r : raw)
        {
            Note n;
            n.ms = TickMs(d->bpm, 16 + r.tick);
            n.holdMs = r.holdTicks ? TickMs(d->bpm, 16 + r.tick + r.holdTicks) - n.ms : 0;
            n.lane = r.lane;
            c.notes.push_back(n);
        }
        return c;
    }

    uint32_t ChartHash(Chart const& c)
    {
        uint32_t h = 2166136261u;
        auto eat = [&](uint8_t b) {
            h ^= b;
            h *= 16777619u;
        };
        for (Note const& n : c.notes)
        {
            for (int i = 0; i < 4; ++i)
                eat(uint8_t(n.ms >> (8 * i)));
            for (int i = 0; i < 4; ++i)
                eat(uint8_t(n.holdMs >> (8 * i)));
            eat(n.lane);
        }
        return h;
    }

    float Difficulty(Chart const& c)
    {
        InstrumentDef const* d = Def(c.instrument);
        if (!d || c.lengthMs <= c.countInMs)
            return 0.f;
        float holds = 0.f;
        for (Note const& n : c.notes)
            if (n.holdMs)
                holds += 1.f;
        float const seconds = float(c.lengthMs - c.countInMs) / 1000.f;
        float const rate = (float(c.notes.size()) + 0.5f * holds) / seconds;
        return rate * (1.f + 0.15f * float(d->lanes - 2)) * (120.f / float(d->goodMs));
    }

    Judgement Judge(int32_t deltaMs, InstrumentDef const& d)
    {
        uint32_t const off = uint32_t(deltaMs < 0 ? -int64_t(deltaMs) : int64_t(deltaMs));
        if (off <= d.perfectMs)
            return Judgement::Perfect;
        if (off <= d.goodMs)
            return Judgement::Good;
        return Judgement::Miss;
    }

    uint8_t Multiplier(uint32_t combo)
    {
        return uint8_t(1 + std::min<uint32_t>(combo / 8, 3));
    }

    Tally Score(Chart const& c, std::vector<Press> const& presses, uint32_t upToMs)
    {
        Tally t;
        InstrumentDef const* d = Def(c.instrument);
        if (!d)
            return t;
        size_t const n = c.notes.size();
        t.notes = uint16_t(n);
        for (Note const& note : c.notes)
            if (note.holdMs)
                ++t.holds;
        // Heads: 0 pending, 1 struck, 2 missed. Holds' ends: 0 pending, 1 judged.
        std::vector<uint8_t> head(n, 0);
        std::vector<uint8_t> tail(n, 0);
        std::array<int, kMaxLanes> active;
        active.fill(-1);
        uint32_t const good = d->goodMs;
        size_t next = 0;    // the first note whose head may still be pending

        auto judged = [&](bool success, uint8_t points) {
            t.maxScore += uint32_t(kPointsPerfect) * Multiplier(t.judged);
            ++t.judged;
            if (success)
            {
                t.score += uint32_t(points) * Multiplier(t.combo);
                ++t.combo;
                t.maxCombo = std::max(t.maxCombo, t.combo);
            }
            else
                t.combo = 0;
        };
        auto endOf = [&](size_t j) { return c.notes[j].ms + c.notes[j].holdMs; };
        // Everything that happens on its own before `now`: a head's window closing unstruck (a
        // miss, and a held note's end lost with it), a held note held past its end (kept); in
        // time order, a head before an end at the same moment.
        auto advance = [&](uint32_t now) {
            for (;;)
            {
                while (next < n && head[next] != 0)
                    ++next;
                uint64_t const headAt = next < n ? uint64_t(c.notes[next].ms) + good : UINT64_MAX;
                int keepLane = -1;
                uint64_t keepAt = UINT64_MAX;
                for (uint8_t l = 0; l < d->lanes; ++l)
                    if (active[l] >= 0 && uint64_t(endOf(size_t(active[l]))) + good < keepAt)
                    {
                        keepAt = uint64_t(endOf(size_t(active[l]))) + good;
                        keepLane = l;
                    }
                uint64_t const at = std::min(headAt, keepAt);
                if (at == UINT64_MAX || at >= uint64_t(now))
                    return;
                if (headAt <= keepAt)
                {
                    head[next] = 2;
                    ++t.miss;
                    judged(false, 0);
                    if (c.notes[next].holdMs)
                    {
                        tail[next] = 1;
                        ++t.dropped;
                        judged(false, 0);
                    }
                }
                else
                {
                    size_t const j = size_t(active[keepLane]);
                    tail[j] = 1;
                    ++t.kept;
                    judged(true, kPointsHold);
                    active[keepLane] = -1;
                }
            }
        };

        for (Press const& p : presses)
        {
            if (p.lane >= d->lanes)
                continue;
            if (p.ms >= upToMs)
                break;
            advance(p.ms);
            if (p.down)
            {
                if (active[p.lane] >= 0)
                    continue;
                int found = -1;
                for (size_t j = next; j < n && c.notes[j].ms <= uint64_t(p.ms) + good; ++j)
                {
                    Note const& note = c.notes[j];
                    if (head[j] != 0 || note.lane != p.lane)
                        continue;
                    int64_t const delta = int64_t(p.ms) - int64_t(note.ms);
                    if (delta < -int64_t(good) || delta > int64_t(good))
                        continue;
                    found = int(j);
                    break;
                }
                if (found < 0)
                {
                    ++t.strays;
                    t.combo = 0;
                    continue;
                }
                Note const& note = c.notes[size_t(found)];
                int32_t const delta = int32_t(int64_t(p.ms) - int64_t(note.ms));
                head[size_t(found)] = 1;
                ++t.hits;
                t.sumDelta += delta;
                t.sumDeltaSq += int64_t(delta) * delta;
                if (Judge(delta, *d) == Judgement::Perfect)
                {
                    ++t.perfect;
                    judged(true, kPointsPerfect);
                }
                else
                {
                    ++t.good;
                    judged(true, kPointsGood);
                }
                if (note.holdMs)
                    active[p.lane] = found;
            }
            else
            {
                int const j = active[p.lane];
                if (j < 0)
                    continue;
                active[p.lane] = -1;
                tail[size_t(j)] = 1;
                if (uint64_t(p.ms) + good >= endOf(size_t(j)))
                {
                    ++t.kept;
                    judged(true, kPointsHold);
                }
                else
                {
                    ++t.dropped;
                    judged(false, 0);
                }
            }
        }
        advance(upToMs);
        if (upToMs == kSongOver)
            // The song is over: a held note still down has been held to its end.
            for (uint8_t l = 0; l < d->lanes; ++l)
                if (active[l] >= 0)
                {
                    tail[size_t(active[l])] = 1;
                    ++t.kept;
                    judged(true, kPointsHold);
                    active[l] = -1;
                }
        return t;
    }

    float Quality(Tally const& t)
    {
        float const units = float(t.judged) + 0.5f * float(t.strays);
        if (units <= 0.f)
            return 0.f;
        float const acc = (float(t.perfect) + float(t.good) * (2.f / 3.f) + float(t.kept)) / units;
        float const sc = t.maxScore ? float(t.score) / float(t.maxScore) : 0.f;
        return std::clamp(0.6f * acc + 0.4f * sc, 0.f, 1.f);
    }

    float Spread(Tally const& t)
    {
        if (t.hits < 2)
            return 1000.f;
        double const mean = double(t.sumDelta) / t.hits;
        double const var = double(t.sumDeltaSq) / t.hits - mean * mean;
        return float(std::sqrt(std::max(0.0, var)));
    }

    bool Robotic(Tally const& t)
    {
        return t.hits >= kRoboticHits && Spread(t) < kRoboticSpreadMs;
    }

    float Potency(InstrumentDef const& d, Tally const& t)
    {
        float q = Quality(t);
        if (q < kFailQuality)
            return 0.f;
        if (Robotic(t))
            q = std::min(q, kRoboticQuality);
        return d.floor + (d.ceiling - d.floor) * q;
    }

    float LivePotency(InstrumentDef const& d, Tally const& t)
    {
        if (!t.judged)
            return d.floor;
        return d.floor + (d.ceiling - d.floor) * Quality(t);
    }

    uint32_t MaxDowns(Chart const& c)
    {
        return uint32_t(c.notes.size()) * 2 + 24;
    }

    Reject Accept(Stream& s, Chart const& c, std::vector<Press> const& batch, uint32_t elapsedMs)
    {
        if (s.rejected != Reject::None)
            return s.rejected;
        InstrumentDef const* d = Def(c.instrument);
        auto reject = [&](Reject r) {
            s.rejected = r;
            return r;
        };
        if (!d)
            return reject(Reject::Lane);
        if (batch.size() > kMaxBatch)
            return reject(Reject::Flood);
        for (Press const& p : batch)
        {
            if (p.lane >= d->lanes)
                return reject(Reject::Lane);
            if (p.ms < s.lastMs)
                return reject(Reject::Order);
            if (uint64_t(p.ms) > uint64_t(elapsedMs) + kFutureSlackMs)
                return reject(Reject::Future);
            s.lastMs = p.ms;
            if (uint64_t(p.ms) > uint64_t(c.lengthMs) + kEndGraceMs)
            {
                ++s.ignored;
                continue;
            }
            if (p.down)
            {
                if (s.held[p.lane]
                    || (s.everDown[p.lane] && p.ms - s.lastDown[p.lane] < kMinTapGapMs))
                {
                    ++s.ignored;
                    continue;
                }
                if (++s.downs > MaxDowns(c))
                    return reject(Reject::Flood);
                s.held[p.lane] = true;
                s.everDown[p.lane] = true;
                s.lastDown[p.lane] = p.ms;
            }
            else
            {
                if (!s.held[p.lane])
                {
                    ++s.ignored;
                    continue;
                }
                s.held[p.lane] = false;
            }
            s.presses.push_back(p);
        }
        return Reject::None;
    }

    std::vector<uint8_t> EncodeStart(Start const& s)
    {
        std::vector<uint8_t> b{kOpSongStart};
        PutU32(b, s.perf);
        PutU16(b, s.song);
        b.push_back(uint8_t(s.instrument));
        PutU32(b, s.seed);
        return b;
    }

    bool DecodeStart(std::vector<uint8_t> const& body, Start& out)
    {
        Reader r{body};
        if (r.U8() != kOpSongStart)
            return false;
        out.perf = r.U32();
        out.song = r.U16();
        out.instrument = Instrument(r.U8());
        out.seed = r.U32();
        return r.Done();
    }

    std::vector<uint8_t> EncodeHits(Hits const& h)
    {
        std::vector<uint8_t> b{kOpSongHits};
        PutU32(b, h.perf);
        size_t const n = std::min<size_t>(h.presses.size(), 255);
        b.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            PutU32(b, h.presses[i].ms);
            b.push_back(uint8_t((h.presses[i].lane & 0x7F) | (h.presses[i].down ? kLaneDown : 0)));
        }
        return b;
    }

    bool DecodeHits(std::vector<uint8_t> const& body, Hits& out)
    {
        Reader r{body};
        if (r.U8() != kOpSongHits)
            return false;
        out.perf = r.U32();
        uint8_t const n = r.U8();
        out.presses.clear();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            Press p;
            p.ms = r.U32();
            uint8_t const l = r.U8();
            p.lane = uint8_t(l & 0x7F);
            p.down = (l & kLaneDown) != 0;
            out.presses.push_back(p);
        }
        return r.Done();
    }

    std::vector<uint8_t> EncodeStop(uint32_t perf)
    {
        std::vector<uint8_t> b{kOpSongStop};
        PutU32(b, perf);
        return b;
    }

    bool DecodeStop(std::vector<uint8_t> const& body, uint32_t& perf)
    {
        Reader r{body};
        if (r.U8() != kOpSongStop)
            return false;
        perf = r.U32();
        return r.Done();
    }

    std::vector<uint8_t> EncodeResult(Result const& x)
    {
        std::vector<uint8_t> b{kOpSongResult};
        PutU32(b, x.perf);
        PutU16(b, x.song);
        b.push_back(x.flags);
        for (uint16_t v : {x.perfect, x.good, x.miss, x.kept, x.dropped, x.strays, x.maxCombo, x.potencyPm, x.qualityPm})
            PutU16(b, v);
        return b;
    }

    bool DecodeResult(std::vector<uint8_t> const& body, Result& out)
    {
        Reader r{body};
        if (r.U8() != kOpSongResult)
            return false;
        out.perf = r.U32();
        out.song = r.U16();
        out.flags = r.U8();
        out.perfect = r.U16();
        out.good = r.U16();
        out.miss = r.U16();
        out.kept = r.U16();
        out.dropped = r.U16();
        out.strays = r.U16();
        out.maxCombo = r.U16();
        out.potencyPm = r.U16();
        out.qualityPm = r.U16();
        return r.Done();
    }

    // ── The profession ──

    namespace
    {
        TierDef const kTiers[] = {
            {Tier::None, "", 0, 0, 0, 0},
            {Tier::Apprentice, "Apprentice", 75, 0, 5, 100},
            {Tier::Journeyman, "Journeyman", 150, 50, 10, 500},
            {Tier::Expert, "Expert", 225, 125, 20, 2500},
            {Tier::Artisan, "Artisan", 300, 200, 35, 10000},
        };

        bool Live(uint32_t untilMs, uint32_t nowMs) { return int32_t(untilMs - nowMs) > 0; }
    }

    TierDef const& TierOf(Tier t)
    {
        size_t const i = size_t(t);
        return kTiers[i < sizeof(kTiers) / sizeof(kTiers[0]) ? i : 0];
    }

    uint16_t SkillCap(Tier t) { return TierOf(t).cap; }

    TrainResult CanTrainTier(Tier have, Tier want, uint16_t skill, uint8_t level, uint32_t money, bool gm)
    {
        if (want == Tier::None || uint8_t(have) >= uint8_t(want))
            return TrainResult::AlreadyKnown;
        if (want != Construction::Next(have))
            return TrainResult::NotNext;
        if (gm)
            return TrainResult::Ok;
        TierDef const& d = TierOf(want);
        if (skill < d.skillToLearn)
            return TrainResult::SkillTooLow;
        if (level < d.levelToLearn)
            return TrainResult::LevelTooLow;
        if (money < d.costCopper)
            return TrainResult::NotEnoughMoney;
        return TrainResult::Ok;
    }

    std::vector<Recipe> const& Recipes()
    {
        // Rough Lumber 91001, Sturdy Lumber 91002, Hardwood Plank 91003, Ironwood Plank 91004 (a
        // builder's); Light Leather 2318, Thick Leather 4304, Rugged Leather 8170, Rugged Hide 8171
        // (a skinner's); Coarse Thread 2320, Silken Thread 4291, Heavy Silken Thread 8343 (a
        // tailor's); Silver Bar 2842, Mithril Bar 3860, Thorium Bar 12359 (a miner's).
        static std::vector<Recipe> const rows = {
            {1, Instrument::HandDrum, 1, 50, 0, {{2318, 2}, {91001, 1}}},
            {2, Instrument::Lute, 50, 500, 0, {{91001, 6}, {2320, 3}}},
            {3, Instrument::Flute, 100, 1500, 0, {{91002, 4}, {2842, 1}}},
            {4, Instrument::Harp, 150, 4000, 0, {{91003, 8}, {3860, 2}, {4291, 2}}},
            {5, Instrument::Bagpipes, 225, 8000, 0, {{4304, 10}, {8343, 2}, {91003, 2}}},
            {6, Instrument::ThunderDrums, 275, 20000, kThunderDrumsScore, {{8170, 12}, {8171, 2}, {91004, 4}, {12359, 2}}},
        };
        return rows;
    }

    Recipe const* RecipeOf(uint16_t id)
    {
        for (Recipe const& r : Recipes())
            if (r.id == id)
                return &r;
        return nullptr;
    }

    Thresholds SkillOf(Recipe const& r) { return Construction::Spaced(r.required); }

    LearnCheck CanLearn(Recipe const& r, Tier tier, uint16_t skill, bool known, bool hasScore, uint32_t money, bool gm)
    {
        if (known)
            return LearnCheck::Known;
        if (gm)
            return LearnCheck::Ok;
        if (tier == Tier::None)
            return LearnCheck::NotLearned;
        if (skill < r.required)
            return LearnCheck::SkillTooLow;
        if (r.score && !hasScore)
            return LearnCheck::NoScore;
        if (money < r.learnCost)
            return LearnCheck::NotEnoughMoney;
        return LearnCheck::Ok;
    }

    MakeCheck CanMake(Recipe const& r, bool known, uint16_t skill, Construction::Inventory const& bags, bool gm)
    {
        if (gm)
            return MakeCheck::Ok;
        if (!known)
            return MakeCheck::Unknown;
        if (skill < r.required)
            return MakeCheck::SkillTooLow;
        return Construction::CanAfford(bags, r.reagents) ? MakeCheck::Ok : MakeCheck::NoReagents;
    }

    uint16_t MakeSkillUp(Recipe const& r, uint16_t skill, uint16_t cap, float roll)
    {
        return Construction::ApplySkillUp(skill, cap, SkillOf(r), roll);
    }

    bool CombatSong(Effect e)
    {
        return e == Effect::Ward || e == Effect::Rally || e == Effect::Mend;
    }

    std::vector<SongDef> const& Songs()
    {
        constexpr uint32_t kMin = 60000;
        // Each instrument's songs, by the skill they ask. Travel and Rally: a share of the run
        // speed; Rested: a share of a level's experience; Ward: a share of the damage; Campfire
        // and Mend: percent of the maximum health and mana a second.
        static std::vector<SongDef> const rows = {
            {1, Instrument::HandDrum, "Marching Cadence", Effect::Travel, 1, 0.06f, 10 * kMin},
            {2, Instrument::HandDrum, "Hearthside Rhythm", Effect::Rested, 20, 0.01f, kRestedCooldownMs},
            {3, Instrument::Lute, "Ballad of the Road", Effect::Travel, 50, 0.07f, 15 * kMin},
            {4, Instrument::Lute, "Tavern Ballad", Effect::Rested, 70, 0.015f, kRestedCooldownMs},
            {5, Instrument::Lute, "Fireside Air", Effect::Campfire, 90, 0.25f, 10 * kMin},
            {6, Instrument::Flute, "Wanderer's Air", Effect::Travel, 100, 0.08f, 15 * kMin},
            {7, Instrument::Flute, "Lullaby of the Hearth", Effect::Rested, 125, 0.02f, kRestedCooldownMs},
            {8, Instrument::Flute, "Ember Song", Effect::Campfire, 140, 0.3f, 15 * kMin},
            {9, Instrument::Harp, "Hymn of Rest", Effect::Rested, 150, 0.025f, kRestedCooldownMs},
            {10, Instrument::Harp, "Hymn of the Hearth", Effect::Campfire, 175, 0.35f, 15 * kMin},
            {11, Instrument::Harp, "Starlight Nocturne", Effect::Travel, 200, 0.09f, 20 * kMin},
            {12, Instrument::Harp, "Hymn of Warding", Effect::Ward, 225, 0.03f, 20000},
            {13, Instrument::Bagpipes, "March of the Clans", Effect::Travel, 225, 0.1f, 20 * kMin},
            {14, Instrument::Bagpipes, "Skirl of the Clans", Effect::Rally, 250, 0.03f, 20000},
            {15, Instrument::ThunderDrums, "Grand Revel", Effect::Rested, 275, 0.03f, kRestedCooldownMs},
            {16, Instrument::ThunderDrums, "Heartbeat of the Earth", Effect::Mend, 285, 0.3f, 20000},
        };
        return rows;
    }

    SongDef const* Song(uint16_t id)
    {
        for (SongDef const& s : Songs())
            if (s.id == id)
                return &s;
        return nullptr;
    }

    std::vector<SongDef const*> SongsOf(Instrument i)
    {
        std::vector<SongDef const*> out;
        for (SongDef const& s : Songs())
            if (s.instrument == i)
                out.push_back(&s);
        return out;
    }

    Thresholds SkillOf(SongDef const& s) { return Construction::Spaced(s.required); }

    float CapOf(Effect e)
    {
        switch (e)
        {
            case Effect::Travel: return 0.15f;
            case Effect::Rested: return 0.05f;
            case Effect::Campfire: return 0.6f;
            case Effect::Ward: return 0.05f;
            case Effect::Rally: return 0.05f;
            case Effect::Mend: return 0.4f;
        }
        return 0.f;
    }

    float Magnitude(SongDef const& s, float potency)
    {
        if (potency <= 0.f)
            return 0.f;
        return std::min(s.base * potency, CapOf(s.effect));
    }

    Refusal CanPerform(SongDef const& s, InstrumentDef const& i, Tier tier, uint16_t skill, Place const& p, bool gm)
    {
        if (s.instrument != i.id)
            return Refusal::WrongInstrument;
        if (!gm && tier == Tier::None)
            return Refusal::NotLearned;
        if (!gm && skill < std::max(i.skill, s.required))
            return Refusal::SkillTooLow;
        if (CombatSong(s.effect))
            return p.coolingDown ? Refusal::Cooldown : Refusal::Ok;
        if (p.inCombat)
            return Refusal::InCombat;
        if (s.effect == Effect::Rested && !p.resting && !gm)
            return Refusal::NotAtInn;
        if (s.effect == Effect::Campfire && !p.fire && !gm)
            return Refusal::NotAtFire;
        return Refusal::Ok;
    }

    float SkillUpChance(SongDef const& s, uint16_t skill, float quality)
    {
        if (quality < kFailQuality)
            return 0.f;
        float const played = std::clamp((quality - kFailQuality) / 0.6f, 0.f, 1.f);
        return Construction::SkillUpChance(skill, SkillOf(s)) * played;
    }

    uint16_t SkillGain(SongDef const& s, uint16_t skill, uint16_t cap, float quality, bool robotic, float roll)
    {
        if (skill >= cap)
            return skill;
        float const q = robotic ? std::min(quality, kRoboticQuality) : quality;
        if (roll >= SkillUpChance(s, skill, q))
            return skill;
        Color const c = Construction::ColorOf(skill, SkillOf(s));
        uint16_t const points = (!robotic && q >= 0.9f && (c == Color::Orange || c == Color::Yellow)) ? 2 : 1;
        return uint16_t(std::min<uint32_t>(cap, uint32_t(skill) + points));
    }

    void Expire(std::vector<Buff>& buffs, uint32_t nowMs)
    {
        buffs.erase(std::remove_if(buffs.begin(), buffs.end(), [&](Buff const& b) { return !Live(b.untilMs, nowMs); }),
            buffs.end());
    }

    void ApplyBuff(std::vector<Buff>& buffs, SongDef const& s, float value, uint32_t nowMs)
    {
        Expire(buffs, nowMs);
        if (value <= 0.f)
            return;
        if (s.effect == Effect::Campfire)
            for (Buff& b : buffs)
            {
                SongDef const* o = Song(b.song);
                if (!o || b.extended || o->effect == Effect::Rested || o->effect == Effect::Campfire)
                    continue;
                b.untilMs += uint32_t(float(o->durationMs) * kCampfireExtend);
                b.extended = true;
            }
        uint32_t const until = nowMs + s.durationMs;
        for (Buff& b : buffs)
        {
            SongDef const* o = Song(b.song);
            if (!o || o->effect != s.effect)
                continue;
            if (value >= b.value || int32_t(b.untilMs - until) < 0)
                b = Buff{s.id, value, until, false};
            return;
        }
        buffs.push_back(Buff{s.id, value, until, false});
    }

    namespace
    {
        float ValueOf(std::vector<Buff> const& buffs, Effect e, uint32_t nowMs)
        {
            for (Buff const& b : buffs)
                if (SongDef const* s = Song(b.song); s && s->effect == e && Live(b.untilMs, nowMs))
                    return b.value;
            return 0.f;
        }
    }

    bool HasEffect(std::vector<Buff> const& buffs, Effect e, uint32_t nowMs)
    {
        for (Buff const& b : buffs)
            if (SongDef const* s = Song(b.song); s && s->effect == e && Live(b.untilMs, nowMs))
                return true;
        return false;
    }

    float SpeedFactor(std::vector<Buff> const& buffs, uint32_t nowMs, bool inCombat)
    {
        return 1.f + (inCombat ? 0.f : ValueOf(buffs, Effect::Travel, nowMs)) + ValueOf(buffs, Effect::Rally, nowMs);
    }

    float DamageTakenFactor(std::vector<Buff> const& buffs, uint32_t nowMs)
    {
        return 1.f - ValueOf(buffs, Effect::Ward, nowMs);
    }

    float RegenPerSecond(std::vector<Buff> const& buffs, uint32_t nowMs, bool inCombat)
    {
        return (inCombat ? 0.f : ValueOf(buffs, Effect::Campfire, nowMs)) + ValueOf(buffs, Effect::Mend, nowMs);
    }

    std::vector<uint8_t> EncodeBuffs(std::vector<Buff> const& buffs, uint32_t nowMs)
    {
        std::vector<uint8_t> b{kOpBuffs};
        std::vector<Buff const*> live;
        for (Buff const& x : buffs)
            if (Live(x.untilMs, nowMs) && live.size() < 16)
                live.push_back(&x);
        b.push_back(uint8_t(live.size()));
        for (Buff const* x : live)
        {
            PutU16(b, x->song);
            PutU16(b, uint16_t(std::clamp(std::lround(x->value * 10000.f), 0l, 65535l)));
            PutU32(b, x->untilMs - nowMs);
        }
        return b;
    }

    bool DecodeBuffs(std::vector<uint8_t> const& body, std::vector<Buff>& out, uint32_t nowMs)
    {
        Reader r{body};
        if (r.U8() != kOpBuffs)
            return false;
        uint8_t const n = r.U8();
        out.clear();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            Buff x;
            x.song = r.U16();
            x.value = float(r.U16()) / 10000.f;
            x.untilMs = nowMs + r.U32();
            out.push_back(x);
        }
        return r.Done();
    }

    TipCheck CheckTip(TipAsk const& a)
    {
        if (a.copper == 0)
            return TipCheck::Zero;
        if (a.tipperAccount == a.performerAccount)
            return TipCheck::SameAccount;
        if (!a.heard)
            return TipCheck::NotHeard;
        if (a.tipped)
            return TipCheck::AlreadyTipped;
        if (a.copper > kMaxTip)
            return TipCheck::TooMuch;
        if (uint64_t(a.givenToday) + a.copper > kTipGiveDaily)
            return TipCheck::GiveCap;
        if (uint64_t(a.gotToday) + a.copper > kTipGetDaily)
            return TipCheck::GetCap;
        if (a.money < a.copper)
            return TipCheck::NoMoney;
        return TipCheck::Ok;
    }

    uint32_t CrowdDailyCap(uint8_t level)
    {
        return 50u * std::max<uint32_t>(1, level);
    }

    uint32_t CrowdTip(uint8_t level, float quality, uint32_t listeners, Color songColor, uint32_t crowdToday, uint32_t accountCrowdToday)
    {
        if (quality < 0.5f || songColor == Color::Gray || listeners == 0)
            return 0;
        uint32_t const cap = CrowdDailyCap(level);
        float const share = float(std::min(listeners, kCrowdListeners)) / float(kCrowdListeners);
        uint32_t const amount = uint32_t(float(cap) / 15.f * std::clamp(quality, 0.f, 1.f) * share);
        uint32_t const leftChar = crowdToday < cap ? cap - crowdToday : 0;
        uint32_t const leftAccount = accountCrowdToday < kCrowdAccountDaily ? kCrowdAccountDaily - accountCrowdToday : 0;
        return std::min({amount, leftChar, leftAccount});
    }

    std::vector<uint8_t> EncodeSkillOf(Tier t, uint16_t skill)
    {
        std::vector<uint8_t> b{kOpSkill, uint8_t(t)};
        PutU16(b, skill);
        PutU16(b, SkillCap(t));
        return b;
    }

    std::vector<uint8_t> EncodeHeard(Heard const& h)
    {
        std::vector<uint8_t> b{kOpHeard};
        PutU64(b, h.performer);
        PutU16(b, h.song);
        PutU16(b, h.potencyPm);
        PutStr(b, h.name);
        return b;
    }

    std::vector<uint8_t> EncodeTip(uint64_t performer, uint32_t copper)
    {
        std::vector<uint8_t> b{kOpTip};
        PutU64(b, performer);
        PutU32(b, copper);
        return b;
    }

    bool DecodeTip(std::vector<uint8_t> const& body, uint64_t& performer, uint32_t& copper)
    {
        Reader r{body};
        if (r.U8() != kOpTip)
            return false;
        performer = r.U64();
        copper = r.U32();
        return r.Done();
    }

    std::vector<uint8_t> EncodeTipped(bool ok, uint32_t copper, std::string const& text)
    {
        std::vector<uint8_t> b{kOpTipped, uint8_t(ok ? 1 : 0)};
        PutU32(b, copper);
        PutStr(b, text);
        return b;
    }

    std::vector<uint8_t> EncodeWindow(Window const& w)
    {
        std::vector<uint8_t> b{kOpWindow, uint8_t(w.open), uint8_t(w.trainer), uint8_t(w.tier)};
        PutU16(b, w.skill);
        PutU16(b, SkillCap(w.tier));
        b.push_back(w.tone);
        PutStr(b, w.message);
        size_t const n = std::min(w.services.size(), kMaxServices);
        b.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Service const& s = w.services[i];
            b.push_back(uint8_t(s.action));
            PutU32(b, s.arg);
            PutU32(b, s.cost);
            b.push_back(uint8_t(s.enabled));
            b.push_back(s.hue);
            PutStr(b, s.label);
            PutStr(b, s.note);
        }
        return b;
    }

    bool DecodeWindow(std::vector<uint8_t> const& body, Window& out)
    {
        Reader r{body};
        if (r.U8() != kOpWindow)
            return false;
        out = Window{};
        out.open = r.U8() != 0;
        out.trainer = r.U8() != 0;
        uint8_t const tier = r.U8();
        if (tier > uint8_t(Tier::Artisan))
            return false;
        out.tier = Tier(tier);
        out.skill = r.U16();
        r.U16();
        out.tone = r.U8();
        out.message = r.Str();
        uint8_t const n = r.U8();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            Service s;
            s.action = Action(r.U8());
            s.arg = r.U32();
            s.cost = r.U32();
            s.enabled = r.U8() != 0;
            s.hue = r.U8();
            s.label = r.Str();
            s.note = r.Str();
            out.services.push_back(std::move(s));
        }
        return r.Done();
    }

    std::vector<uint8_t> EncodeAct(Action a, uint32_t arg)
    {
        std::vector<uint8_t> b{kOpAct, uint8_t(a)};
        PutU32(b, arg);
        return b;
    }

    bool DecodeAct(std::vector<uint8_t> const& body, Action& a, uint32_t& arg)
    {
        Reader r{body};
        if (r.U8() != kOpAct)
            return false;
        uint8_t const v = r.U8();
        arg = r.U32();
        if (!r.Done() || v < uint8_t(Action::Train) || v > uint8_t(Action::Make))
            return false;
        a = Action(v);
        return true;
    }

    uint16_t MakeRecipeOf(uint32_t arg) { return uint16_t(arg & 0xFFFF); }
    uint16_t MakeCountOf(uint32_t arg) { return std::clamp<uint16_t>(uint16_t(arg >> 16), 1, kMaxMakeBatch); }
    uint32_t MakeArg(uint16_t recipe, uint16_t count) { return uint32_t(recipe) | uint32_t(count) << 16; }

    std::vector<BookEntry> Book(std::vector<uint16_t> const& known)
    {
        std::vector<BookEntry> out;
        for (Recipe const& r : Recipes())
        {
            BookEntry e;
            e.id = r.id;
            e.instrument = r.instrument;
            e.skill = SkillOf(r);
            e.known = std::find(known.begin(), known.end(), r.id) != known.end();
            InstrumentDef const* d = Def(r.instrument);
            e.item = d ? d->item : 0;
            e.reagents = r.reagents;
            out.push_back(std::move(e));
        }
        return out;
    }

    std::vector<uint8_t> EncodeBook(std::vector<BookEntry> const& book)
    {
        std::vector<uint8_t> b{kOpBook, uint8_t(std::min<size_t>(book.size(), 255))};
        for (size_t i = 0; i < book.size() && i < 255; ++i)
        {
            BookEntry const& e = book[i];
            PutU16(b, e.id);
            b.push_back(uint8_t(e.instrument));
            PutU16(b, e.skill.required);
            PutU16(b, e.skill.yellow);
            PutU16(b, e.skill.green);
            PutU16(b, e.skill.gray);
            b.push_back(uint8_t(e.known));
            PutU32(b, e.item);
            size_t const n = std::min<size_t>(e.reagents.size(), 255);
            b.push_back(uint8_t(n));
            for (size_t k = 0; k < n; ++k)
            {
                PutU32(b, e.reagents[k].item);
                PutU16(b, uint16_t(std::min<uint32_t>(e.reagents[k].count, 0xFFFF)));
            }
        }
        return b;
    }

    bool DecodeBook(std::vector<uint8_t> const& body, std::vector<BookEntry>& out)
    {
        Reader r{body};
        out.clear();
        if (r.U8() != kOpBook)
            return false;
        uint8_t const n = r.U8();
        for (uint8_t i = 0; i < n && r.ok; ++i)
        {
            BookEntry e;
            e.id = r.U16();
            e.instrument = Instrument(r.U8());
            e.skill.required = r.U16();
            e.skill.yellow = r.U16();
            e.skill.green = r.U16();
            e.skill.gray = r.U16();
            e.known = r.U8() != 0;
            e.item = r.U32();
            uint8_t const k = r.U8();
            for (uint8_t j = 0; j < k && r.ok; ++j)
            {
                uint32_t const item = r.U32();
                e.reagents.push_back({item, r.U16()});
            }
            out.push_back(std::move(e));
        }
        return r.ok && r.at == body.size();
    }
}
