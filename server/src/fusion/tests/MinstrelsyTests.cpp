/*
 * World of Modern Warfare: Minstrelsy (fusion/Minstrelsy.h): the charts, the judging, the potency,
 * the server's plausibility checks, and the profession (songs, skill-ups, effects, tips, the
 * Utility bar, the wire). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/ActionSystems.h"
#include "fusion/Gathering.h"
#include "fusion/Minstrelsy.h"
#include "fusion/Throttle.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <set>

using namespace Fusion;
using namespace Fusion::Minstrelsy;

namespace
{
    // A playing of the chart: each note struck `offset(i)` ms from its time, held notes let go
    // `release` ms from their end (or not struck at all where `skip(i)`); in time order.
    std::vector<Press> Play(Chart const& c, std::function<int32_t(size_t)> offset,
        std::function<bool(size_t)> skip = nullptr, int32_t release = 0)
    {
        std::vector<Press> out;
        for (size_t i = 0; i < c.notes.size(); ++i)
        {
            if (skip && skip(i))
                continue;
            Note const& n = c.notes[i];
            int64_t const at = int64_t(n.ms) + offset(i);
            out.push_back({uint32_t(std::max<int64_t>(0, at)), n.lane, true});
            int64_t const up = n.holdMs ? int64_t(n.ms) + n.holdMs + release : at + 60;
            out.push_back({uint32_t(std::max<int64_t>(at + 1, up)), n.lane, false});
        }
        std::stable_sort(out.begin(), out.end(), [](Press const& a, Press const& b) { return a.ms < b.ms; });
        return out;
    }

    // A person's timing: within a few tens of milliseconds, never twice the same.
    int32_t Jitter(size_t i, int32_t spread)
    {
        uint32_t x = uint32_t(i) * 2654435761u + 12345u;
        x ^= x >> 13;
        x *= 0x5bd1e995u;
        x ^= x >> 15;
        return int32_t(x % uint32_t(2 * spread + 1)) - spread;
    }

    InstrumentDef const& D(Instrument i) { return *Def(i); }

    std::vector<Instrument> All()
    {
        return {Instrument::HandDrum, Instrument::Lute, Instrument::Flute, Instrument::Harp, Instrument::Bagpipes,
            Instrument::ThunderDrums};
    }

    // Feed a whole playing in batches of half a second, as the client sends it.
    Reject Feed(Fusion::Minstrelsy::Stream& s, Chart const& c, std::vector<Press> const& presses, uint32_t lagMs = 80)
    {
        size_t at = 0;
        for (uint32_t now = 500; at < presses.size(); now += 500)
        {
            std::vector<Press> batch;
            while (at < presses.size() && presses[at].ms + lagMs <= now && batch.size() < kMaxBatch)
                batch.push_back(presses[at++]);
            if (Reject r = Accept(s, c, batch, now); r != Reject::None)
                return r;
            if (now > c.lengthMs + 10000)
                break;
        }
        return Reject::None;
    }
}

TEST(minstrel_instruments_are_six_utility_tools)
{
    namespace AS = Fusion::ActionSystems;
    namespace G = Fusion::Gathering;
    CHECK(Instruments().size() == 6);
    uint16_t lastSkill = 0;
    for (uint8_t tier = 1; tier <= 6; ++tier)
    {
        InstrumentDef const& d = Instruments()[tier - 1];
        CHECK(uint8_t(d.id) == tier);
        CHECK(Def(d.id) == &d && DefOfTool(d.tool) == &d && DefOfItem(d.item) == &d);
        CHECK(AS::IsTool(d.tool) && AS::IsInstrument(d.tool));
        CHECK(AS::AbilityOn(d.tool, AS::System::Utility) && !AS::AbilityOn(d.tool, AS::System::Classic));
        G::ToolDef const* t = G::ToolInfo(d.tool);
        CHECK(t && t->item == d.item && t->skill == kSkillMinstrelsy && t->work == G::Work::Song);
        CHECK(std::string(t->name) == d.name);
        CHECK(d.skill > lastSkill || tier == 1);
        lastSkill = d.skill;
        CHECK(d.lanes >= 2 && d.lanes <= kMaxLanes);
        CHECK(d.perfectMs < d.goodMs);
        CHECK(d.floor == 0.5f && d.ceiling > 1.f && d.ceiling <= 1.5f);
        CHECK(d.grid == 1 || d.grid == 2 || d.grid == 4);
        CHECK(d.holdMin <= d.holdMax);
    }
    // The owner's requirements: 1, 50, 100, 150, 225, 275.
    CHECK(Instruments()[0].skill == 1 && Instruments()[1].skill == 50 && Instruments()[2].skill == 100);
    CHECK(Instruments()[3].skill == 150 && Instruments()[4].skill == 225 && Instruments()[5].skill == 275);
    CHECK(Def(Instrument::None) == nullptr && DefOfTool(10) == nullptr && DefOfItem(0) == nullptr);
    CHECK(G::kSkillMinstrelsy == kSkillMinstrelsy && kSkillMinstrelsy != G::kSkillConstruction);
    // The tools mask still fits its u16 (op 9), the instruments after the Repair Hammer.
    CHECK(AS::ToolBit(uint32_t(AS::Ability::ThunderDrums)) == (1u << 12) && AS::kAllTools == 0x1FFF);
}

TEST(minstrel_charts_are_the_same_for_a_seed_and_differ_between_seeds)
{
    for (Instrument i : All())
    {
        Chart const a = MakeChart(i, 12345);
        Chart const b = MakeChart(i, 12345);
        CHECK(a.notes.size() == b.notes.size() && ChartHash(a) == ChartHash(b));
        std::set<uint32_t> hashes;
        for (uint32_t seed = 1; seed <= 20; ++seed)
            hashes.insert(ChartHash(MakeChart(i, seed)));
        CHECK(hashes.size() >= 19);
    }
    // The client's generator (client/src/bard/rules.rs) makes these very charts: its tests hold
    // the same hashes.
    CHECK(ChartHash(MakeChart(Instrument::HandDrum, 12345)) == 4073638693u);
    CHECK(ChartHash(MakeChart(Instrument::Lute, 12345)) == 1187863032u);
    CHECK(ChartHash(MakeChart(Instrument::Flute, 12345)) == 2148115026u);
    CHECK(ChartHash(MakeChart(Instrument::Harp, 12345)) == 1699884994u);
    CHECK(ChartHash(MakeChart(Instrument::Bagpipes, 12345)) == 2300773996u);
    CHECK(ChartHash(MakeChart(Instrument::ThunderDrums, 12345)) == 3222150876u);
    CHECK(ChartHash(MakeChart(Instrument::ThunderDrums, 0xDEADBEEF)) == 3175862811u);
    CHECK(MakeChart(Instrument::None, 1).notes.empty());
    // The seed 12345 charts' sizes, as the client's tests hold them too.
    CHECK(MakeChart(Instrument::HandDrum, 12345).notes.size() == 16);
    CHECK(MakeChart(Instrument::Harp, 12345).notes.size() == 37);
    CHECK(MakeChart(Instrument::ThunderDrums, 12345).notes.size() == 44);
}

TEST(minstrel_charts_are_playable)
{
    for (Instrument i : All())
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            InstrumentDef const& d = D(i);
            Chart const c = MakeChart(i, seed * 7919);
            CHECK(c.countInMs == TickMs(d.bpm, 16) && c.beatMs == 60000u / d.bpm);
            CHECK(c.lengthMs == TickMs(d.bpm, 16u * (d.bars + 1)));
            CHECK(!c.notes.empty());
            std::array<uint32_t, kMaxLanes> laneFree{};
            uint32_t last = 0;
            std::set<uint32_t> times;
            bool chords = false;
            bool holds = false;
            for (Note const& n : c.notes)
            {
                CHECK(n.lane < d.lanes);
                CHECK(n.ms >= c.countInMs && n.ms + n.holdMs <= c.lengthMs);
                CHECK(n.ms >= last);
                // A lane never has two notes at once (a hold keeps its lane to its end).
                CHECK(n.ms >= laneFree[n.lane]);
                laneFree[n.lane] = n.ms + n.holdMs + 1;
                chords |= !times.insert(n.ms).second;
                holds |= n.holdMs > 0;
                last = n.ms;
            }
            // Every bar starts on a note.
            for (uint32_t bar = 1; bar <= d.bars; ++bar)
                CHECK(times.count(TickMs(d.bpm, 16 * bar)) == 1);
            if (d.chord == 0)
                CHECK(!chords);
            if (d.hold == 0)
                CHECK(!holds);
        }
    // Over a few songs, the instruments with chords and holds do have them.
    for (Instrument i : {Instrument::Lute, Instrument::Harp, Instrument::ThunderDrums})
    {
        bool chord = false;
        for (uint32_t seed = 1; seed <= 10 && !chord; ++seed)
        {
            Chart const c = MakeChart(i, seed);
            for (size_t k = 1; k < c.notes.size(); ++k)
                chord |= c.notes[k].ms == c.notes[k - 1].ms;
        }
        CHECK(chord);
    }
    for (Instrument i : {Instrument::Flute, Instrument::Harp, Instrument::Bagpipes})
    {
        bool hold = false;
        for (uint32_t seed = 1; seed <= 10 && !hold; ++seed)
            for (Note const& n : MakeChart(i, seed).notes)
                hold |= n.holdMs > 0;
        CHECK(hold);
    }
}

TEST(minstrel_instruments_grow_harder_and_reach_higher)
{
    float lastDifficulty = 0.f;
    InstrumentDef const* prev = nullptr;
    for (Instrument i : All())
    {
        float sum = 0.f;
        for (uint32_t seed = 1; seed <= 30; ++seed)
            sum += Difficulty(MakeChart(i, seed));
        float const avg = sum / 30.f;
        CHECK(avg > lastDifficulty * 1.1f);
        lastDifficulty = avg;
        InstrumentDef const& d = D(i);
        if (prev)
        {
            CHECK(d.lanes >= prev->lanes);
            CHECK(d.bpm >= prev->bpm || d.grid < prev->grid);
            CHECK(d.perfectMs < prev->perfectMs && d.goodMs < prev->goodMs);
            CHECK(d.ceiling > prev->ceiling);
            CHECK(d.scrollMs < prev->scrollMs);
        }
        prev = &d;
    }
    // The Hand Drum is two lanes of quarter notes; the capstone all six keys in sixteenths.
    CHECK(D(Instrument::HandDrum).lanes == 2 && D(Instrument::HandDrum).grid == 4);
    CHECK(D(Instrument::ThunderDrums).lanes == 6 && D(Instrument::ThunderDrums).grid == 1);
}

TEST(minstrel_timing_windows_judge_perfect_good_and_miss)
{
    InstrumentDef const& d = D(Instrument::Lute);    // 50 and 120 ms
    CHECK(Judge(0, d) == Judgement::Perfect);
    CHECK(Judge(50, d) == Judgement::Perfect && Judge(-50, d) == Judgement::Perfect);
    CHECK(Judge(51, d) == Judgement::Good && Judge(-51, d) == Judgement::Good);
    CHECK(Judge(120, d) == Judgement::Good && Judge(-120, d) == Judgement::Good);
    CHECK(Judge(121, d) == Judgement::Miss && Judge(-121, d) == Judgement::Miss);
    // The drum forgives more than the capstone.
    CHECK(Judge(55, D(Instrument::HandDrum)) == Judgement::Perfect);
    CHECK(Judge(55, D(Instrument::ThunderDrums)) == Judgement::Good);
    CHECK(Judge(100, D(Instrument::ThunderDrums)) == Judgement::Miss);
    // The combo multiplier: x1 for the first 8, then x2, x3, x4 at most.
    CHECK(Multiplier(0) == 1 && Multiplier(7) == 1 && Multiplier(8) == 2 && Multiplier(16) == 3);
    CHECK(Multiplier(24) == 4 && Multiplier(500) == 4);
}

TEST(minstrel_a_perfect_performance_reaches_the_ceiling)
{
    for (Instrument i : All())
    {
        Chart const c = MakeChart(i, 99);
        InstrumentDef const& d = D(i);
        Tally const t = Score(c, Play(c, [](size_t k) { return Jitter(k, 12); }), kSongOver);
        CHECK(t.perfect == c.notes.size() && t.good == 0 && t.miss == 0 && t.strays == 0);
        CHECK(t.kept == t.holds && t.dropped == 0);
        CHECK(t.judged == t.notes + t.holds);
        CHECK(t.maxCombo == t.judged && t.score == t.maxScore);
        CHECK_NEAR(Quality(t), 1.0, 1e-6);
        CHECK(!Robotic(t));
        CHECK_NEAR(Potency(d, t), d.ceiling, 1e-5);
    }
}

TEST(minstrel_the_potency_follows_the_accuracy_and_the_combo)
{
    InstrumentDef const& d = D(Instrument::Harp);
    Chart const c = MakeChart(Instrument::Harp, 4242);
    auto potencyOf = [&](std::function<int32_t(size_t)> off, std::function<bool(size_t)> skip = nullptr) {
        return Potency(d, Score(c, Play(c, off, skip), kSongOver));
    };
    float const perfect = potencyOf([](size_t k) { return Jitter(k, 15); });
    // All struck, all just Good (60 ms late on a 40/95 window).
    Tally const goodT = Score(c, Play(c, [](size_t) { return 60; }), kSongOver);
    CHECK(goodT.good == c.notes.size() && goodT.perfect == 0);
    float const good = Potency(d, goodT);
    // Every fourth note missed: the combo never builds.
    float const gappy = potencyOf([](size_t k) { return Jitter(k, 15); }, [](size_t k) { return k % 4 == 3; });
    // The same number missed, all at the end: the combo had built.
    size_t const quarter = c.notes.size() / 4;
    float const late = potencyOf([](size_t k) { return Jitter(k, 15); },
        [&](size_t k) { return k >= c.notes.size() - quarter; });
    // Nothing played: flat.
    Tally const none = Score(c, {}, kSongOver);
    CHECK(none.miss == c.notes.size() && none.judged == none.notes + none.holds);
    CHECK(Potency(d, none) == 0.f && Quality(none) == 0.f);
    CHECK(perfect > good && good > gappy && gappy > d.floor);
    CHECK(late > gappy);
    CHECK(good > 1.f);
    // From 50% at poor to the ceiling at perfect; never past it.
    for (float q : {0.2f, 0.5f, 0.99f})
    {
        Tally t;
        t.judged = 100;
        t.perfect = uint16_t(q * 100);
        t.miss = uint16_t(100 - t.perfect);
        t.maxScore = 300;
        t.score = uint32_t(300 * q);
        CHECK_NEAR(Quality(t), q, 0.02);
        float const p = Potency(d, t);
        CHECK(p >= d.floor && p <= d.ceiling);
    }
    CHECK(LivePotency(d, Tally{}) == d.floor);
}

TEST(minstrel_strays_holds_and_misses_count)
{
    // A stray press breaks the combo and costs accuracy.
    Chart const c = MakeChart(Instrument::Lute, 777);
    InstrumentDef const& d = D(Instrument::Lute);
    std::vector<Press> clean = Play(c, [](size_t k) { return Jitter(k, 10); });
    std::vector<Press> noisy = clean;
    for (size_t k = 1; k < c.notes.size(); k += 3)
    {
        // A press half way between two notes on a lane with no note near.
        uint32_t const mid = (c.notes[k - 1].ms + c.notes[k].ms) / 2;
        // A lane with no note near.
        int lane = -1;
        for (uint8_t l = 0; l < d.lanes && lane < 0; ++l)
        {
            bool near = false;
            for (Note const& n : c.notes)
                if (n.lane == l && (n.ms > mid ? n.ms - mid : mid - n.ms) <= d.goodMs)
                    near = true;
            if (!near)
                lane = l;
        }
        if (lane >= 0)
        {
            noisy.push_back({mid, uint8_t(lane), true});
            noisy.push_back({mid + 30, uint8_t(lane), false});
        }
    }
    std::stable_sort(noisy.begin(), noisy.end(), [](Press const& a, Press const& b) { return a.ms < b.ms; });
    Tally const a = Score(c, clean, kSongOver);
    Tally const b = Score(c, noisy, kSongOver);
    CHECK(b.strays > 0 && a.strays == 0);
    CHECK(b.perfect == a.perfect && b.maxCombo < a.maxCombo);
    CHECK(Quality(b) < Quality(a));

    // Held notes: let go half way through, or held to the end.
    Chart const f = MakeChart(Instrument::Flute, 31337);
    Tally const kept = Score(f, Play(f, [](size_t k) { return Jitter(k, 10); }), kSongOver);
    CHECK(kept.holds > 0 && kept.kept == kept.holds);
    std::vector<Press> early;
    for (size_t k = 0; k < f.notes.size(); ++k)
    {
        Note const& n = f.notes[k];
        early.push_back({n.ms, n.lane, true});
        uint32_t const up = n.holdMs ? n.ms + n.holdMs / 2 : n.ms + 50;
        early.push_back({up, n.lane, false});
    }
    std::stable_sort(early.begin(), early.end(), [](Press const& a, Press const& b) { return a.ms < b.ms; });
    Tally const dropped = Score(f, early, kSongOver);
    CHECK(dropped.dropped == dropped.holds && dropped.kept == 0);
    CHECK(Quality(dropped) < Quality(kept));
    // A held note never let go is kept when the song ends.
    std::vector<Press> downs;
    for (Note const& n : f.notes)
        if (n.holdMs)
        {
            downs.push_back({n.ms, n.lane, true});
            break;
        }
    Tally const held = Score(f, downs, kSongOver);
    CHECK(held.kept == 1 && held.perfect == 1);

    // The live tally judges only what has come due.
    Chart const h = MakeChart(Instrument::HandDrum, 5);
    std::vector<Press> half = Play(h, [](size_t) { return 0; });
    uint32_t const mid = h.notes[h.notes.size() / 2].ms;
    Tally const live = Score(h, half, mid);
    CHECK(live.judged > 0 && live.judged < h.notes.size());
    CHECK(live.miss == 0);
    float const lp = LivePotency(D(Instrument::HandDrum), live);
    CHECK(lp > 1.f && lp <= D(Instrument::HandDrum).ceiling);
}

TEST(minstrel_too_even_a_performance_is_capped)
{
    InstrumentDef const& d = D(Instrument::Bagpipes);
    Chart const c = MakeChart(Instrument::Bagpipes, 2024);
    // Every note exactly on time: no person plays like that.
    Tally const bot = Score(c, Play(c, [](size_t) { return 0; }), kSongOver);
    CHECK(Robotic(bot) && Spread(bot) < 1.f);
    CHECK_NEAR(Potency(d, bot), d.floor + (d.ceiling - d.floor) * kRoboticQuality, 1e-5);
    // ... nor a fixed late offset (the spread is about the mean).
    CHECK(Robotic(Score(c, Play(c, [](size_t) { return 20; }), kSongOver)));
    // A person's spread passes, and reaches higher.
    Tally const person = Score(c, Play(c, [](size_t k) { return Jitter(k, 25); }), kSongOver);
    CHECK(!Robotic(person) && Spread(person) > 10.f);
    CHECK(Potency(d, person) > Potency(d, bot));
}

TEST(minstrel_the_server_takes_an_honest_stream)
{
    for (Instrument i : All())
    {
        Chart const c = MakeChart(i, 8080);
        std::vector<Press> const presses = Play(c, [](size_t k) { return Jitter(k, 20); });
        Fusion::Minstrelsy::Stream s;
        CHECK(Feed(s, c, presses) == Reject::None);
        CHECK(s.presses.size() == presses.size() && s.ignored == 0);
        Tally const t = Score(c, s.presses, kSongOver);
        CHECK(t.perfect + t.good == c.notes.size());
        CHECK(Potency(D(i), t) > 1.f);
    }
}

TEST(minstrel_the_server_rejects_impossible_streams)
{
    Chart const c = MakeChart(Instrument::Harp, 1);
    std::vector<Press> const presses = Play(c, [](size_t k) { return Jitter(k, 20); });
    // The whole song sent at its start: presses from the future.
    {
        Fusion::Minstrelsy::Stream s;
        std::vector<Press> first(presses.begin(), presses.begin() + 64);
        CHECK(Accept(s, c, first, 200) == Reject::Future);
        // Rejected stays rejected.
        CHECK(Accept(s, c, {}, 100000) == Reject::Future);
        Tally const t = Score(c, s.presses, kSongOver);
        CHECK(Potency(*Def(c.instrument), t) == 0.f);
    }
    // Out of time order.
    {
        Fusion::Minstrelsy::Stream s;
        std::vector<Press> back = {{3000, 0, true}, {2000, 1, true}};
        CHECK(Accept(s, c, back, 5000) == Reject::Order);
    }
    // A lane the harp does not have.
    {
        Fusion::Minstrelsy::Stream s;
        CHECK(Accept(s, c, {{3000, 5, true}}, 5000) == Reject::Lane);
    }
    // A batch bigger than any client sends.
    {
        Fusion::Minstrelsy::Stream s;
        std::vector<Press> big;
        for (uint32_t k = 0; k < kMaxBatch + 1; ++k)
            big.push_back({3000 + k * 100, uint8_t(k % 5), (k % 2) == 0});
        CHECK(Accept(s, c, big, 100000) == Reject::Flood);
    }
    // Mashing every key all song long: more presses than any playing needs.
    {
        Fusion::Minstrelsy::Stream s;
        std::vector<Press> mash;
        for (uint32_t ms = c.countInMs; ms < c.lengthMs; ms += 45)
            for (uint8_t lane = 0; lane < 5; ++lane)
            {
                mash.push_back({ms, lane, true});
                mash.push_back({ms + 20, lane, false});
            }
        std::stable_sort(mash.begin(), mash.end(), [](Press const& a, Press const& b) { return a.ms < b.ms; });
        CHECK(Feed(s, c, mash, 0) == Reject::Flood);
    }
}

TEST(minstrel_the_server_ignores_what_means_nothing_and_mashing_scores_low)
{
    Chart const c = MakeChart(Instrument::Lute, 3);
    // A key down twice, a release of a key not down, a tap too soon: ignored, not rejected.
    {
        Fusion::Minstrelsy::Stream s;
        std::vector<Press> odd = {{2000, 0, true}, {2010, 0, true}, {2020, 1, false}, {2030, 0, false},
            {2035, 0, true}, {2400, 0, false}};
        CHECK(Accept(s, c, odd, 3000) == Reject::None);
        CHECK(s.ignored == 4 && s.presses.size() == 2);
    }
    // Presses after the song are dropped.
    {
        Fusion::Minstrelsy::Stream s;
        CHECK(Accept(s, c, {{c.lengthMs + kEndGraceMs + 10, 0, true}}, c.lengthMs + 5000) == Reject::None);
        CHECK(s.presses.empty() && s.ignored == 1);
    }
    // Tapping every lane on every beat (within the press budget): the strays sink it.
    {
        InstrumentDef const& d = D(Instrument::Lute);
        std::vector<Press> beats;
        for (uint32_t tick = 16; tick < 16u * (d.bars + 1); tick += 4)
            for (uint8_t lane = 0; lane < d.lanes; ++lane)
            {
                uint32_t const ms = TickMs(d.bpm, tick);
                beats.push_back({ms, lane, true});
                beats.push_back({ms + 30, lane, false});
            }
        std::stable_sort(beats.begin(), beats.end(), [](Press const& a, Press const& b) { return a.ms < b.ms; });
        Fusion::Minstrelsy::Stream s;
        Reject const r = Feed(s, c, beats, 0);
        Tally const honest = Score(c, Play(c, [](size_t k) { return Jitter(k, 20); }), kSongOver);
        if (r == Reject::None)
            CHECK(Potency(d, Score(c, s.presses, kSongOver)) < Potency(d, honest) - 0.2f);
    }
}

TEST(minstrel_messages_round_trip_on_the_tools_channel_and_the_notes_are_throttled)
{
    namespace AS = Fusion::ActionSystems;
    Start const st{77, 7, Instrument::Flute, 0xCAFEBABE};
    Start sb;
    std::vector<uint8_t> const sbytes = EncodeStart(st);
    CHECK(sbytes.size() == 12 && sbytes[0] == 51);
    CHECK(DecodeStart(sbytes, sb));
    CHECK(sb.perf == 77 && sb.song == 7 && sb.instrument == Instrument::Flute && sb.seed == 0xCAFEBABE);
    Hits h{9, {{1200, 3, true}, {1500, 3, false}, {1501, 0, true}}};
    std::vector<uint8_t> const hb = EncodeHits(h);
    CHECK(hb.size() == 1 + 4 + 1 + 3 * 5 && hb[0] == kOpSongHits);
    Hits back;
    CHECK(DecodeHits(hb, back) && back.perf == 9 && back.presses.size() == 3);
    CHECK(back.presses[1].ms == 1500 && back.presses[1].lane == 3 && !back.presses[1].down && back.presses[2].down);
    std::vector<uint8_t> shortHits = hb;
    shortHits.pop_back();
    CHECK(!DecodeHits(shortHits, back));
    uint32_t perf = 0;
    CHECK(DecodeStop(EncodeStop(41), perf) && perf == 41);
    Result const r{5, 12, RESULT_ROBOTIC | RESULT_SKILLUP, 30, 4, 2, 3, 1, 6, 25, 1234, 876};
    Result rb;
    std::vector<uint8_t> const rbytes = EncodeResult(r);
    CHECK(rbytes.size() == 26);
    CHECK(DecodeResult(rbytes, rb));
    CHECK(rb.perf == 5 && rb.song == 12 && rb.flags == (RESULT_ROBOTIC | RESULT_SKILLUP) && rb.perfect == 30 && rb.strays == 6);
    CHECK(rb.maxCombo == 25 && rb.potencyPm == 1234 && rb.qualityPm == 876);
    // The ops are free on the action systems' channel (1-9 are its own).
    std::set<uint8_t> ops;
    for (uint8_t op : {kOpSongHits, kOpSongStart, kOpSongStop, kOpSongResult, kOpBuffs, kOpSkill, kOpHeard, kOpTip,
             kOpTipped, kOpAskWindow, kOpAct, kOpWindow})
    {
        CHECK(op >= 50 && op <= 62);
        CHECK(ops.insert(op).second);
    }
    CHECK(!AS::DecodeSystem(EncodeStop(1)) && !AS::DecodeTool(EncodeStop(1)) && !AS::DecodeStrike(hb));
    // A strike of an instrument names its song as its target.
    AS::StrikeRequest const strike{uint32_t(AS::Ability::Harp), 12};
    std::optional<AS::StrikeRequest> const sr = AS::DecodeStrike(AS::EncodeStrike(strike));
    CHECK(sr && sr->tool == uint32_t(AS::Ability::Harp) && sr->target == 12);
    // The notes are one of the costly asks (the shared allowance), as are a tip and the window;
    // the stop is not, nor anything of the class channel any more.
    CHECK(Throttle::Costly(AS::kChannel, EncodeHits(h)));
    CHECK(Throttle::Costly(AS::kChannel, EncodeTip(1, 100)));
    CHECK(Throttle::Costly(AS::kChannel, std::vector<uint8_t>{kOpAskWindow}));
    CHECK(Throttle::Costly(AS::kChannel, EncodeAct(Action::Train, 0)));
    CHECK(!Throttle::Costly(AS::kChannel, EncodeStop(1)));
    CHECK(!Throttle::Costly(AS::kChannel, AS::EncodeTool(17)));
    // The rest of the wire.
    uint64_t who = 0;
    uint32_t copper = 0;
    CHECK(DecodeTip(EncodeTip(0x0102030405060708ull, 2500), who, copper) && who == 0x0102030405060708ull && copper == 2500);
    std::vector<uint8_t> const heard = EncodeHeard({42, 4, 1100, "Lyra"});
    CHECK(heard.size() == 1 + 8 + 2 + 2 + 1 + 4 && heard[0] == kOpHeard);
    std::vector<uint8_t> const skill = EncodeSkillOf(Tier::Expert, 201);
    CHECK(skill == std::vector<uint8_t>({kOpSkill, 3, 201, 0, 225, 0}));
    Window w;
    w.open = true;
    w.trainer = true;
    w.tier = Tier::Journeyman;
    w.skill = 99;
    w.message = "Hello";
    w.services.push_back({Action::Train, 0, 2500, false, 5, "Train Expert Minstrelsy", "Requires 125"});
    w.services.push_back({Action::Buy, 2, 1500, true, 0, "Buy a Lute", ""});
    Window wb;
    CHECK(DecodeWindow(EncodeWindow(w), wb));
    CHECK(wb.open && wb.trainer && wb.tier == Tier::Journeyman && wb.skill == 99 && wb.message == "Hello");
    CHECK(wb.services.size() == 2 && wb.services[0].label == "Train Expert Minstrelsy" && !wb.services[0].enabled);
    CHECK(wb.services[1].action == Action::Buy && wb.services[1].arg == 2 && wb.services[1].cost == 1500);
    Action a = Action::None;
    uint32_t arg = 0;
    CHECK(DecodeAct(EncodeAct(Action::Buy, 3), a, arg) && a == Action::Buy && arg == 3);
    CHECK(!DecodeAct(EncodeAct(Action(9), 3), a, arg));
}

TEST(minstrel_the_utility_bar_has_room_for_the_instruments)
{
    namespace AS = Fusion::ActionSystems;
    // Three pages: the sixth page and the last six of bonus bar 12; Modern keeps three.
    CHECK(AS::SystemOfButton(60) == AS::System::Utility && AS::SystemOfButton(71) == AS::System::Utility);
    CHECK(AS::SystemOfButton(210) == AS::System::Utility && AS::SystemOfButton(215) == AS::System::Utility);
    CHECK(AS::SystemOfButton(192) == AS::System::Modern && AS::SystemOfButton(209) == AS::System::Modern);
    uint32_t const lute = uint32_t(AS::Ability::Lute);
    CHECK(AS::ButtonAllows(212, lute, AS::kFusionButtonType) && AS::ButtonAllows(61, lute, AS::kFusionButtonType));
    CHECK(!AS::ButtonAllows(200, lute, AS::kFusionButtonType) && !AS::ButtonAllows(1, lute, AS::kFusionButtonType));
    CHECK(!AS::ButtonAllows(212, uint32_t(AS::Ability::Primary), AS::kFusionButtonType));
}

TEST(minstrel_an_instrument_shows_on_utility_only_when_owned_and_trained)
{
    namespace AS = Fusion::ActionSystems;
    namespace G = Fusion::Gathering;
    G::ToolDef const& harp = *G::ToolInfo(uint32_t(AS::Ability::Harp));
    // The game's holder: the item carried, and "learned" = Minstrelsy known with the instrument's skill.
    CHECK(!G::OnUtilityBar(harp, {false, true, false}));
    CHECK(!G::OnUtilityBar(harp, {true, false, false}));
    CHECK(!G::OnUtilityBar(harp, {false, false, true}));
    CHECK(G::OnUtilityBar(harp, {true, true, false}));
    CHECK(G::CheckTool(harp, {true, false, false}) == G::Outcome::NotLearned);
    CHECK(G::CheckTool(harp, {false, true, false}) == G::Outcome::NoTool);
    uint16_t const mask = G::UtilityBarTools([&](G::ToolDef const& t) {
        return G::Holder{t.ability == AS::Ability::Harp || t.ability == AS::Ability::HandDrum, t.skill == kSkillMinstrelsy, false};
    });
    CHECK(mask == (AS::ToolBit(uint32_t(AS::Ability::Harp)) | AS::ToolBit(uint32_t(AS::Ability::HandDrum))));
    CHECK(G::Fits(uint32_t(AS::Ability::Harp), G::Work::Song) && !G::Fits(uint32_t(AS::Ability::Harp), G::Work::Vein));
}

TEST(minstrel_trainers_teach_four_tiers_to_300)
{
    CHECK(SkillCap(Tier::Apprentice) == 75 && SkillCap(Tier::Journeyman) == 150 && SkillCap(Tier::Expert) == 225 && SkillCap(Tier::Artisan) == 300);
    CHECK(CanTrainTier(Tier::None, Tier::Apprentice, 0, 5, 100) == TrainResult::Ok);
    CHECK(CanTrainTier(Tier::None, Tier::Apprentice, 0, 4, 100) == TrainResult::LevelTooLow);
    CHECK(CanTrainTier(Tier::None, Tier::Apprentice, 0, 5, 99) == TrainResult::NotEnoughMoney);
    CHECK(CanTrainTier(Tier::None, Tier::Journeyman, 0, 60, 100000) == TrainResult::NotNext);
    CHECK(CanTrainTier(Tier::Apprentice, Tier::Journeyman, 49, 60, 100000) == TrainResult::SkillTooLow);
    CHECK(CanTrainTier(Tier::Apprentice, Tier::Journeyman, 50, 60, 100000) == TrainResult::Ok);
    CHECK(CanTrainTier(Tier::Artisan, Tier::Artisan, 300, 60, 100000) == TrainResult::AlreadyKnown);
    CHECK(CanTrainTier(Tier::Expert, Tier::Artisan, 0, 1, 0, true) == TrainResult::Ok);
    // A tier's cap is reachable before the next asks for it.
    for (Tier t : {Tier::Apprentice, Tier::Journeyman, Tier::Expert})
        CHECK(TierOf(Fusion::Construction::Next(t)).skillToLearn <= SkillCap(t));
}

TEST(minstrel_every_instrument_has_its_songs_and_the_bands_cover_1_to_300)
{
    std::set<uint16_t> ids;
    for (InstrumentDef const& d : Instruments())
    {
        std::vector<SongDef const*> const songs = SongsOf(d.id);
        CHECK(songs.size() >= 2);
        for (SongDef const* s : songs)
        {
            CHECK(ids.insert(s->id).second && Song(s->id) == s);
            CHECK(s->required >= d.skill && s->required <= 300);
            CHECK(s->durationMs > 0 && s->base > 0.f);
            // The combat songs come at 225 and later, never stronger than their caps' mild 5%.
            if (CombatSong(s->effect))
                CHECK(s->required >= 225);
            CHECK(Magnitude(*s, 1.5f) <= CapOf(s->effect) + 1e-6f);
        }
    }
    CHECK(Song(0) == nullptr && Song(999) == nullptr);
    CHECK(CapOf(Effect::Ward) <= 0.05f && CapOf(Effect::Rally) <= 0.05f);
    // From 1 to 299 there is always a song that still teaches (not gray) to an instrument the
    // skill can play.
    for (uint16_t skill = 1; skill < 300; ++skill)
    {
        bool teaches = false;
        for (SongDef const& s : Songs())
            if (skill >= std::max(s.required, Def(s.instrument)->skill)
                && Fusion::Construction::ColorOf(skill, SkillOf(s)) != Color::Gray)
                teaches = true;
        CHECK(teaches);
    }
}

TEST(minstrel_skill_rises_faster_the_better_a_song_is_played_and_only_in_its_band)
{
    SongDef const& road = *Song(3);     // Ballad of the Road, 50: orange to 75, yellow to 100, green to 125
    CHECK(road.required == 50);
    // Orange, played well: always; played poorly: rarely; flat: never.
    CHECK_NEAR(SkillUpChance(road, 60, 1.f), 1.0, 1e-6);
    CHECK(SkillUpChance(road, 60, 0.5f) < SkillUpChance(road, 60, 0.7f));
    CHECK(SkillUpChance(road, 60, 0.7f) < SkillUpChance(road, 60, 0.9f) + 1e-6f);
    CHECK(SkillUpChance(road, 60, 0.1f) == 0.f);
    // Gray (125 and over) never; green less than yellow.
    CHECK(SkillUpChance(road, 125, 1.f) == 0.f && SkillUpChance(road, 200, 1.f) == 0.f);
    CHECK(SkillUpChance(road, 110, 1.f) < SkillUpChance(road, 80, 1.f));
    // A near-perfect song while orange or yellow: two points; a good one: one.
    CHECK(SkillGain(road, 60, 150, 0.95f, false, 0.f) == 62);
    CHECK(SkillGain(road, 60, 150, 0.8f, false, 0.f) == 61);
    CHECK(SkillGain(road, 110, 150, 0.95f, false, 0.f) == 111);   // green: one
    // Never past the tier's cap; a robotic song never two points and teaches as a good one.
    CHECK(SkillGain(road, 74, 75, 1.f, false, 0.f) == 75 && SkillGain(road, 75, 75, 1.f, false, 0.f) == 75);
    CHECK(SkillGain(road, 60, 150, 1.f, true, 0.f) == 61);
    CHECK(SkillGain(road, 60, 150, 1.f, true, 0.7f) == 60);
    CHECK(SkillGain(road, 60, 150, 1.f, false, 0.99f) == 62);
    // The curve: a person playing Ballad of the Road at 0.8 from 50 levels a few points a song,
    // and stalls where it turns gray (125).
    uint16_t skill = 50;
    uint32_t seed = 7;
    for (int song = 0; song < 400; ++song)
    {
        seed = seed * 1664525u + 1013904223u;
        skill = SkillGain(road, skill, 150, 0.8f, false, float(seed >> 8) / float(1u << 24));
    }
    CHECK(skill >= 115 && skill <= 125);
}

TEST(minstrel_songs_are_played_where_they_belong_and_combat_cuts_the_rest)
{
    InstrumentDef const& drum = *Def(Instrument::HandDrum);
    InstrumentDef const& harp = *Def(Instrument::Harp);
    SongDef const& cadence = *Song(1);
    SongDef const& hearth = *Song(2);
    SongDef const& ward = *Song(12);
    SongDef const& fire = *Song(10);
    Place out;
    CHECK(CanPerform(cadence, drum, Tier::Apprentice, 1, out) == Refusal::Ok);
    CHECK(CanPerform(cadence, drum, Tier::None, 1, out) == Refusal::NotLearned);
    CHECK(CanPerform(cadence, harp, Tier::Apprentice, 300, out) == Refusal::WrongInstrument);
    CHECK(CanPerform(hearth, drum, Tier::Apprentice, 19, out) == Refusal::SkillTooLow);
    CHECK(CanPerform(hearth, drum, Tier::Apprentice, 20, out) == Refusal::NotAtInn);
    CHECK(CanPerform(fire, harp, Tier::Expert, 200, out) == Refusal::NotAtFire);
    Place inn;
    inn.resting = true;
    CHECK(CanPerform(hearth, drum, Tier::Apprentice, 20, inn) == Refusal::Ok);
    Place fight;
    fight.inCombat = true;
    CHECK(CanPerform(cadence, drum, Tier::Apprentice, 50, fight) == Refusal::InCombat);
    CHECK(CanPerform(ward, harp, Tier::Expert, 224, fight) == Refusal::SkillTooLow);
    CHECK(CanPerform(ward, harp, Tier::Expert, 225, fight) == Refusal::Ok);
    fight.coolingDown = true;
    CHECK(CanPerform(ward, harp, Tier::Expert, 225, fight) == Refusal::Cooldown);
    // The harp's own requirement holds for its every song.
    CHECK(CanPerform(fire, harp, Tier::Journeyman, 149, Place{false, true, false, false}) == Refusal::SkillTooLow);
    CHECK(CombatInterrupts(cadence) && CombatInterrupts(hearth) && !CombatInterrupts(ward));
}

TEST(minstrel_effects_scale_with_the_performance_and_stay_mild)
{
    SongDef const& pipes = *Song(13);     // March of the Clans: travel
    SongDef const& ward = *Song(12);
    SongDef const& fire = *Song(10);
    SongDef const& mend = *Song(16);
    CHECK(Magnitude(pipes, 0.f) == 0.f);
    CHECK(Magnitude(pipes, 0.5f) < Magnitude(pipes, 1.f) && Magnitude(pipes, 1.f) < Magnitude(pipes, 1.45f));
    CHECK_NEAR(Magnitude(pipes, 1.45f), 0.145, 1e-5);
    CHECK_NEAR(Magnitude(ward, 1.35f), 0.0405, 1e-5);
    std::vector<Buff> buffs;
    uint32_t const now = 1000;
    ApplyBuff(buffs, pipes, Magnitude(pipes, 1.2f), now);
    ApplyBuff(buffs, ward, Magnitude(ward, 1.f), now);
    // Travel out of combat only; the ward any time, a little.
    CHECK_NEAR(SpeedFactor(buffs, now + 10, false), 1.12, 1e-5);
    CHECK_NEAR(SpeedFactor(buffs, now + 10, true), 1.0, 1e-5);
    CHECK_NEAR(DamageTakenFactor(buffs, now + 10), 0.97, 1e-5);
    // A weaker travel song does not replace a stronger one that lasts longer...
    ApplyBuff(buffs, *Song(1), Magnitude(*Song(1), 1.f), now + 20);
    CHECK(buffs.size() == 2 && SpeedFactor(buffs, now + 30, false) > 1.11f);
    // ... and the ward ends after its 20 s.
    CHECK_NEAR(DamageTakenFactor(buffs, now + 20001), 1.0, 1e-6);
    // A fire song regenerates out of combat and lengthens the travel song by half, once.
    uint32_t const before = buffs[0].untilMs;
    ApplyBuff(buffs, fire, Magnitude(fire, 1.f), now + 100);
    CHECK(buffs[0].untilMs == before + pipes.durationMs / 2 && buffs[0].extended);
    ApplyBuff(buffs, fire, Magnitude(fire, 1.f), now + 200);
    CHECK(buffs[0].untilMs == before + pipes.durationMs / 2);
    CHECK_NEAR(RegenPerSecond(buffs, now + 300, false), 0.35, 1e-5);
    CHECK(RegenPerSecond(buffs, now + 300, true) == 0.f);
    ApplyBuff(buffs, mend, Magnitude(mend, 1.f), now + 300);
    CHECK_NEAR(RegenPerSecond(buffs, now + 400, true), 0.3, 1e-5);
    CHECK(HasEffect(buffs, Effect::Mend, now + 400) && !HasEffect(buffs, Effect::Rested, now + 400));
    // The listener's buffs on the wire, the time left with them.
    std::vector<Buff> back;
    CHECK(DecodeBuffs(EncodeBuffs(buffs, now + 400), back, 5000));
    CHECK(back.size() == buffs.size() && back[0].song == 13 && back[0].untilMs == 5000 + (buffs[0].untilMs - (now + 400)));
    CHECK_NEAR(back[0].value, buffs[0].value, 1e-4);
    Expire(buffs, now + 50 * 60000);
    CHECK(buffs.empty());
}

TEST(minstrel_tips_are_capped_and_never_between_ones_own_characters)
{
    TipAsk a;
    a.tipperAccount = 1;
    a.performerAccount = 2;
    a.copper = 500;
    a.money = 100000;
    a.heard = true;
    CHECK(CheckTip(a) == TipCheck::Ok);
    TipAsk b = a;
    b.performerAccount = 1;
    CHECK(CheckTip(b) == TipCheck::SameAccount);
    b = a;
    b.heard = false;
    CHECK(CheckTip(b) == TipCheck::NotHeard);
    b = a;
    b.tipped = true;
    CHECK(CheckTip(b) == TipCheck::AlreadyTipped);
    b = a;
    b.copper = kMaxTip + 1;
    CHECK(CheckTip(b) == TipCheck::TooMuch);
    b = a;
    b.givenToday = kTipGiveDaily - 499;
    CHECK(CheckTip(b) == TipCheck::GiveCap);
    b = a;
    b.gotToday = kTipGetDaily - 100;
    CHECK(CheckTip(b) == TipCheck::GetCap);
    b = a;
    b.money = 499;
    CHECK(CheckTip(b) == TipCheck::NoMoney);
    b = a;
    b.copper = 0;
    CHECK(CheckTip(b) == TipCheck::Zero);
    // The crowd: by the playing and the listeners, nothing for a gray song or a poor one, and
    // never past the day's caps.
    CHECK(CrowdDailyCap(60) == 3000 && CrowdDailyCap(1) == 50);
    CHECK(CrowdTip(60, 1.f, 5, Color::Orange, 0, 0) == 200);
    CHECK(CrowdTip(60, 1.f, 9, Color::Orange, 0, 0) == 200);
    CHECK(CrowdTip(60, 1.f, 1, Color::Orange, 0, 0) == 40);
    CHECK(CrowdTip(60, 0.75f, 5, Color::Yellow, 0, 0) == 150);
    CHECK(CrowdTip(60, 0.4f, 5, Color::Orange, 0, 0) == 0);
    CHECK(CrowdTip(60, 1.f, 5, Color::Gray, 0, 0) == 0);
    CHECK(CrowdTip(60, 1.f, 0, Color::Orange, 0, 0) == 0);
    CHECK(CrowdTip(60, 1.f, 5, Color::Orange, 2950, 0) == 50);
    CHECK(CrowdTip(60, 1.f, 5, Color::Orange, 3000, 0) == 0);
    CHECK(CrowdTip(60, 1.f, 5, Color::Orange, 0, kCrowdAccountDaily - 10) == 10);
    // A day of songs at the cap: the most the crowd can give a character.
    uint32_t total = 0;
    for (int i = 0; i < 1000; ++i)
        total += CrowdTip(60, 1.f, 5, Color::Orange, total, total);
    CHECK(total == CrowdDailyCap(60));
    CHECK(DayOf(86400 * 3 + 5) == 3);
}

TEST(minstrel_every_instrument_is_made_by_minstrels)
{
    CHECK(Recipes().size() == 6);
    for (InstrumentDef const& d : Instruments())
    {
        Recipe const* r = nullptr;
        for (Recipe const& x : Recipes())
            if (x.instrument == d.id)
                r = &x;
        CHECK(r && RecipeOf(r->id) == r);
        // Gated at the instrument's own requirement, of other trades' goods.
        CHECK(r->required == d.skill && !r->reagents.empty());
        for (Fusion::Construction::Material const& m : r->reagents)
            CHECK(m.count > 0 && m.item != d.item);
        CHECK((r->score != 0) == (d.id == Instrument::ThunderDrums));
    }
    Recipe const& harp = *RecipeOf(4);
    CHECK(CanLearn(harp, Tier::Journeyman, 150, false, false, 4000) == LearnCheck::Ok);
    CHECK(CanLearn(harp, Tier::Journeyman, 149, false, false, 4000) == LearnCheck::SkillTooLow);
    CHECK(CanLearn(harp, Tier::None, 300, false, false, 4000) == LearnCheck::NotLearned);
    CHECK(CanLearn(harp, Tier::Journeyman, 150, true, false, 4000) == LearnCheck::Known);
    CHECK(CanLearn(harp, Tier::Journeyman, 150, false, false, 3999) == LearnCheck::NotEnoughMoney);
    Recipe const& drums = *RecipeOf(6);
    CHECK(CanLearn(drums, Tier::Artisan, 280, false, false, 100000) == LearnCheck::NoScore);
    CHECK(CanLearn(drums, Tier::Artisan, 280, false, true, 100000) == LearnCheck::Ok);
    Fusion::Construction::Inventory bags{{91003, 8}, {3860, 2}, {4291, 2}};
    CHECK(CanMake(harp, true, 150, bags) == MakeCheck::Ok);
    CHECK(CanMake(harp, false, 150, bags) == MakeCheck::Unknown);
    CHECK(CanMake(harp, true, 140, bags) == MakeCheck::SkillTooLow);
    bags[4291] = 1;
    CHECK(CanMake(harp, true, 150, bags) == MakeCheck::NoReagents);
    // Making teaches in the usual bands: orange always, gray never, never past the cap.
    CHECK(MakeSkillUp(harp, 160, 225, 0.99f) == 161);
    CHECK(MakeSkillUp(harp, 225, 300, 0.f) == 225);
    CHECK(MakeSkillUp(harp, 150, 150, 0.f) == 150);
    Action a = Action::None;
    uint32_t arg = 0;
    CHECK(DecodeAct(EncodeAct(Action::Make, 4), a, arg) && a == Action::Make && arg == 4);
}

TEST(minstrel_anti_cheat_holds_for_the_profession_too)
{
    // A bot's perfect timing: potency capped, skill as a good song, no double point.
    Chart const c = MakeChart(Instrument::Bagpipes, 2024);
    Tally const bot = Score(c, Play(c, [](size_t) { return 0; }), kSongOver);
    CHECK(Robotic(bot));
    SongDef const& road = *Song(3);
    CHECK(SkillGain(road, 60, 150, Quality(bot), Robotic(bot), 0.f) == 61);
    // A rejected stream lands nothing.
    Fusion::Minstrelsy::Stream s;
    std::vector<Press> const presses = Play(c, [](size_t k) { return Jitter(k, 20); });
    std::vector<Press> first(presses.begin(), presses.begin() + std::min<size_t>(presses.size(), 20));
    CHECK(Accept(s, c, first, 100) == Reject::Future);
    CHECK(Potency(*Def(c.instrument), Score(c, s.presses, kSongOver)) == 0.f);
}

// ── The trade window's book and Create All (professions hub) ──

TEST(the_book_lists_every_recipe_with_its_colours_and_round_trips)
{
    std::vector<uint16_t> known{Recipes().front().id};
    std::vector<BookEntry> book = Book(known);
    CHECK(book.size() == Recipes().size());
    for (size_t i = 0; i < book.size(); ++i)
    {
        CHECK(book[i].id == Recipes()[i].id);
        CHECK(book[i].known == (i == 0));
        CHECK(book[i].item == Def(Recipes()[i].instrument)->item);
        CHECK(book[i].skill.required == SkillOf(Recipes()[i]).required);
        CHECK(!book[i].reagents.empty());
    }
    std::vector<BookEntry> back;
    CHECK(DecodeBook(EncodeBook(book), back));
    CHECK(back.size() == book.size());
    for (size_t i = 0; i < back.size(); ++i)
    {
        CHECK(back[i].id == book[i].id && back[i].known == book[i].known && back[i].item == book[i].item);
        CHECK(back[i].skill.gray == book[i].skill.gray && back[i].reagents.size() == book[i].reagents.size());
    }
    std::vector<uint8_t> b = EncodeBook(book);
    b.pop_back();
    CHECK(!DecodeBook(b, back));
}

TEST(make_carries_a_count_held_to_the_batch)
{
    CHECK(MakeRecipeOf(MakeArg(3, 5)) == 3 && MakeCountOf(MakeArg(3, 5)) == 5);
    // An old client's plain recipe id makes one.
    CHECK(MakeRecipeOf(3) == 3 && MakeCountOf(3) == 1);
    CHECK(MakeCountOf(MakeArg(3, 60000)) == kMaxMakeBatch);
}
