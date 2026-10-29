/*
 * World of Modern Warfare: Minstrelsy, music as a secondary profession (design/GAME_DESIGN.md
 * section 12, "Minstrelsy"). GPL-2.0-or-later, see Common.h.
 *
 * Every class may learn it from a Minstrel trainer (like Fishing or Construction it takes no
 * primary profession slot), skill 1 to 300 in four tiers, kept in the fusion's own skill table
 * (key kSkillMinstrelsy, beside Construction's 9001: the client can't learn a SkillLine.dbc row).
 * The six instruments are Utility tools (ActionSystems.h, Gathering.h): on Utility's bar only
 * while carried and while the skill reaches the instrument's requirement. With one readied, the
 * fire button plays the song picked (pressing the instrument's key again picks the next): a
 * short rhythm song whose notes fall down a highway over Utility's bar buttons, played on the
 * slot keys while the minstrel keeps moving.
 *
 * The engine (kept from the Bard's instruments): each performance's chart is made from a seed
 * the server picks (MakeChart, mirrored by client/src/minstrel/rules.rs); the client reports only
 * its presses and releases; the server takes them only as plausible (Accept), judges them (Score)
 * and turns them into a quality and a potency (the instrument's floor to its ceiling). A stream no
 * honest client sends is rejected whole; one timed too evenly for a person (Robotic) keeps a good
 * performance's strength and never the mastery bonus.
 *
 * The profession (below the engine): the songs, what each does (mostly out of combat: travel
 * speed, rested experience at an inn, regeneration by a campfire; from skill 225 three mild
 * combat songs), the skill-ups (a song's colour band, faster the better it is played), and the
 * tips (from other players, capped and never between one account's characters; from the crowd
 * at an inn, capped a day). The game (game/Fusion/FusionMinstrelsy.cpp) runs it.
 */

#ifndef FUSION_MINSTRELSY_H
#define FUSION_MINSTRELSY_H

#include "fusion/Common.h"
#include "fusion/Construction.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Fusion::Minstrelsy
{
    // The instruments, by tier (their wire value).
    enum class Instrument : uint8_t
    {
        None = 0,
        HandDrum = 1,       // tier 1: 2 lanes, quarter notes, wide windows
        Lute = 2,           // tier 2: 3 lanes, eighths, two-note chords
        Flute = 3,          // tier 3: 4 lanes, held notes (breaths)
        Harp = 4,           // tier 4: 5 lanes, chords of three, runs, holds
        Bagpipes = 5,       // tier 5: 6 lanes, sixteenth runs over long drones
        ThunderDrums = 6,   // tier 6 (capstone): 6 lanes, fast, dense chords
    };
    constexpr uint8_t kInstruments = 6;
    constexpr uint8_t kMaxLanes = 6;

    struct InstrumentDef
    {
        Instrument id = Instrument::None;
        char const* name = "";
        uint16_t skill = 1;             // the Minstrelsy it asks (to play it, and to show on the bar)
        uint32_t item = 0;              // its item (world DB, 30_minstrelsy.sql)
        uint32_t tool = 0;              // its Utility ability (ActionSystems::Ability)
        uint8_t lanes = 2;
        uint16_t bpm = 90;
        uint8_t bars = 4;               // the song's bars after the one-bar count-in
        uint8_t grid = 4;               // sixteenths a step: 4 quarters, 2 eighths, 1 sixteenths
        uint8_t density = 0;            // a step's chance of a note, of 256
        uint8_t accent = 0;             // added on the beat
        uint8_t chord = 0;              // a note's chance of being a chord, of 256
        uint8_t maxChord = 1;           // lanes a chord may take
        uint8_t hold = 0;               // a note's chance of being held, of 256
        uint8_t holdMin = 0;            // a hold's length in steps
        uint8_t holdMax = 0;
        uint8_t run = 0;                // the chance the next note steps on from the last (a run)
        uint16_t perfectMs = 50;        // the timing windows, either side
        uint16_t goodMs = 120;
        uint16_t scrollMs = 1500;       // how long a note takes down the highway (the client's)
        float floor = 0.5f;             // the potency of a poor performance
        float ceiling = 1.5f;           // ... of a perfect one
    };
    // The six, in tier order.
    std::array<InstrumentDef, kInstruments> const& Instruments();
    InstrumentDef const* Def(Instrument i);
    // The instrument of a Utility tool ability, or of an item (null: none).
    InstrumentDef const* DefOfTool(uint32_t tool);
    InstrumentDef const* DefOfItem(uint32_t item);

    // ── The chart ──
    struct Note
    {
        uint32_t ms = 0;            // on the song's clock (0: the count-in's first beat)
        uint32_t holdMs = 0;        // 0: a tap
        uint8_t lane = 0;
    };
    struct Chart
    {
        Instrument instrument = Instrument::None;
        uint32_t seed = 0;
        uint32_t countInMs = 0;     // the first bar: the metronome alone
        uint32_t beatMs = 0;        // a beat (rounded down; the notes are timed exactly)
        uint32_t lengthMs = 0;      // the last bar's end
        std::vector<Note> notes;    // by time, then lane
    };
    // A sixteenth's place on the song's clock: count-in included (tick 0 is the count-in's start).
    uint32_t TickMs(uint16_t bpm, uint32_t tick);
    // The chart of a song: the same instrument and seed make the same chart, here and on the client.
    Chart MakeChart(Instrument i, uint32_t seed);
    // FNV-1a over the notes (ms, holdMs, lane), for comparing two implementations.
    uint32_t ChartHash(Chart const& c);
    // How hard a chart is to play: notes and held notes a second, weighted by lanes, chords and the
    // windows' narrowness. Grows with the tier.
    float Difficulty(Chart const& c);

    // ── Judging ──
    enum class Judgement : uint8_t
    {
        Perfect,
        Good,
        Miss,
    };
    // A press `deltaMs` from its note.
    Judgement Judge(int32_t deltaMs, InstrumentDef const& d);
    // The combo's multiplier before a hit: x1, then x2 from 8 in a row, x3 from 16, x4 from 24.
    uint8_t Multiplier(uint32_t combo);
    constexpr uint8_t kPointsPerfect = 3;
    constexpr uint8_t kPointsGood = 2;
    constexpr uint8_t kPointsHold = 3;      // a held note kept to its end

    // A key going down (a press) or up (a release) on the song's clock.
    struct Press
    {
        uint32_t ms = 0;
        uint8_t lane = 0;
        bool down = true;
    };

    struct Tally
    {
        uint16_t notes = 0;         // the chart's notes
        uint16_t holds = 0;         // ... and held notes (each a second thing to judge: its end)
        uint16_t perfect = 0;
        uint16_t good = 0;
        uint16_t miss = 0;
        uint16_t kept = 0;          // held notes kept to their end
        uint16_t dropped = 0;       // ... let go early (or never struck)
        uint16_t strays = 0;        // presses that met no note (they break the combo)
        uint16_t combo = 0;
        uint16_t maxCombo = 0;
        uint32_t score = 0;
        uint32_t maxScore = 0;      // the score of what was judged, all perfect
        uint16_t judged = 0;        // notes and ends judged so far
        // The hits' timing (press - note), for Robotic.
        uint16_t hits = 0;
        int64_t sumDelta = 0;
        int64_t sumDeltaSq = 0;
    };
    // Judge `presses` (in time order) against the chart, up to `upToMs` on the song's clock: a note
    // whose window has closed by then and was not struck is a miss; a hold still held past its end
    // is kept. kSongOver judges everything.
    constexpr uint32_t kSongOver = 0xFFFFFFFFu;
    Tally Score(Chart const& c, std::vector<Press> const& presses, uint32_t upToMs);
    // What a performance was worth, 0..1: 60% the accuracy (Perfect 1, Good 2/3, a kept hold 1,
    // half a note lost for each stray press), 40% the score against an all-perfect one (the combo).
    float Quality(Tally const& t);
    // Below this the song falls flat: nothing happens.
    constexpr float kFailQuality = 0.2f;
    // The hits' spread (ms), for Robotic: a person's is tens of milliseconds.
    float Spread(Tally const& t);
    constexpr uint16_t kRoboticHits = 16;
    constexpr float kRoboticSpreadMs = 3.f;
    // Timed too evenly for a person (at least kRoboticHits hits, spread under kRoboticSpreadMs).
    bool Robotic(Tally const& t);
    // Robotic performances keep at most this quality (a good one's, no mastery).
    constexpr float kRoboticQuality = 0.6f;
    // The performance's potency: the instrument's floor to its ceiling by the quality (Robotic
    // capped). 0 when the song falls flat.
    float Potency(InstrumentDef const& d, Tally const& t);
    // The potency as the meter shows it while the song plays: the floor until anything is judged.
    float LivePotency(InstrumentDef const& d, Tally const& t);

    // ── Plausibility (the server's) ──
    enum class Reject : uint8_t
    {
        None,
        Lane,           // a lane the instrument does not have
        Order,          // out of time order
        Future,         // from the song's future, by the server's clock
        Flood,          // more presses than any playing of the chart needs, or a batch too big
    };
    constexpr uint32_t kFutureSlackMs = 300;    // the server's clock may run that much behind
    constexpr uint32_t kMinTapGapMs = 40;       // one key twice faster than this: one press
    constexpr uint32_t kEndGraceMs = 1500;      // the song's last presses may come this late
    constexpr size_t kMaxBatch = 64;
    // The presses one song may take: twice its notes and a margin.
    uint32_t MaxDowns(Chart const& c);
    struct Stream
    {
        std::vector<Press> presses;     // as accepted
        std::array<bool, kMaxLanes> held{};
        std::array<uint32_t, kMaxLanes> lastDown{};
        std::array<bool, kMaxLanes> everDown{};
        uint32_t lastMs = 0;
        uint32_t downs = 0;
        uint32_t ignored = 0;           // repeats, releases of keys not down, too-fast taps
        Reject rejected = Reject::None;
    };
    // Take a batch of presses at `elapsedMs` on the server's clock since the song began. Presses
    // that mean nothing (a key down twice, a release of a key not down, a tap too soon after the
    // last on its key, after the song) are ignored; a stream that no client could honestly send is
    // rejected whole (and stays rejected).
    Reject Accept(Stream& s, Chart const& c, std::vector<Press> const& batch, uint32_t elapsedMs);

    // ── The wire: the action systems' channel 10 (ActionSystems::kChannel), ops 50-62 ──
    // A song begins with a Utility strike (op 3) of a readied instrument whose target is the song
    // picked; the server answers it (op 4, Gathering::Outcome) and starts the performance (op 51).
    constexpr uint8_t kOpSongHits = 50;     // client: [50, perf u32, n u8, n x (ms u32, lane u8 | 0x80 down)]
    constexpr uint8_t kOpSongStart = 51;    // server: [51, perf u32, song u16, instrument u8, seed u32]
    constexpr uint8_t kOpSongStop = 52;     // client: [52, perf u32]: played out, or stopped early
    constexpr uint8_t kOpSongResult = 53;   // server: see Result
    constexpr uint8_t kOpBuffs = 55;        // server: the songs on the listener, see EncodeBuffs
    constexpr uint8_t kOpSkill = 56;        // server: [56, tier u8, skill u16, cap u16]
    constexpr uint8_t kOpHeard = 57;        // server: [57, performer u64, song u16, potency u16 pm, name str8]
    constexpr uint8_t kOpTip = 58;          // client: [58, performer u64, copper u32]
    constexpr uint8_t kOpTipped = 59;       // server: [59, ok u8, copper u32, text str8]
    constexpr uint8_t kOpAskWindow = 60;    // client: [60]: the Minstrelsy window from the HUD
    constexpr uint8_t kOpAct = 61;          // client: [61, action u8, arg u32] at a trainer
    constexpr uint8_t kOpWindow = 62;       // server: see EncodeWindow
    constexpr uint8_t kLaneDown = 0x80;

    struct Start
    {
        uint32_t perf = 0;
        uint16_t song = 0;
        Instrument instrument = Instrument::None;
        uint32_t seed = 0;
    };
    std::vector<uint8_t> EncodeStart(Start const& s);
    bool DecodeStart(std::vector<uint8_t> const& body, Start& out);

    struct Hits
    {
        uint32_t perf = 0;
        std::vector<Press> presses;
    };
    std::vector<uint8_t> EncodeHits(Hits const& h);
    bool DecodeHits(std::vector<uint8_t> const& body, Hits& out);
    std::vector<uint8_t> EncodeStop(uint32_t perf);
    bool DecodeStop(std::vector<uint8_t> const& body, uint32_t& perf);

    enum ResultFlags : uint8_t
    {
        RESULT_FLAT = 0x01,         // fell flat: no effect
        RESULT_REJECTED = 0x02,     // the presses were impossible
        RESULT_ROBOTIC = 0x04,      // timed too evenly: capped
        RESULT_CUT = 0x08,          // stopped before its end (a stun, combat, death, the hands changed)
        RESULT_SKILLUP = 0x10,      // the skill rose
    };
    // op 53: [53, perf u32, song u16, flags u8, perfect u16, good u16, miss u16, kept u16,
    // dropped u16, strays u16, maxCombo u16, potency u16 (per mille), quality u16 (per mille)]
    struct Result
    {
        uint32_t perf = 0;
        uint16_t song = 0;
        uint8_t flags = 0;
        uint16_t perfect = 0;
        uint16_t good = 0;
        uint16_t miss = 0;
        uint16_t kept = 0;
        uint16_t dropped = 0;
        uint16_t strays = 0;
        uint16_t maxCombo = 0;
        uint16_t potencyPm = 0;
        uint16_t qualityPm = 0;
    };
    std::vector<uint8_t> EncodeResult(Result const& r);
    bool DecodeResult(std::vector<uint8_t> const& body, Result& out);

    // ── The profession ──
    // The key of Minstrelsy in `character_fusion_skill` (Construction's table): no SkillLine.dbc id.
    constexpr uint16_t kSkillMinstrelsy = 9002;
    // The Minstrel trainers (30_minstrelsy.sql).
    constexpr uint32_t kTrainerEntry = 94100;
    using Construction::Tier;
    using Construction::Thresholds;
    using Construction::Color;
    using Construction::TrainResult;
    struct TierDef
    {
        Tier tier;
        char const* name;
        uint16_t cap;
        uint16_t skillToLearn;
        uint8_t levelToLearn;
        uint32_t costCopper;
    };
    TierDef const& TierOf(Tier t);
    uint16_t SkillCap(Tier t);
    // One tier at a time, in order; a game master skips the skill, level and money.
    TrainResult CanTrainTier(Tier have, Tier want, uint16_t skill, uint8_t level, uint32_t money, bool gm = false);

    // What a song does.
    enum class Effect : uint8_t
    {
        Travel = 1,     // the party runs faster, out of combat only
        Rested = 2,     // at an inn or in a city: everyone listening gains rested experience
        Campfire = 3,   // by a fire: the party regenerates out of combat; their other songs last longer
        Ward = 4,       // combat (225+): the party takes a little less damage
        Rally = 5,      // combat: the party moves a little faster, in combat too
        Mend = 6,       // combat: the party regenerates a little, in combat too
    };
    bool CombatSong(Effect e);
    struct SongDef
    {
        uint16_t id = 0;
        Instrument instrument = Instrument::None;
        char const* name = "";
        Effect effect = Effect::Travel;
        uint16_t required = 1;          // the skill it asks; its colours are Construction::Spaced's
        float base = 0.f;               // its magnitude at potency 1 (see Magnitude)
        uint32_t durationMs = 0;        // how long it lasts on a listener
    };
    std::vector<SongDef> const& Songs();
    SongDef const* Song(uint16_t id);
    std::vector<SongDef const*> SongsOf(Instrument i);
    Thresholds SkillOf(SongDef const& s);

    // A song's magnitude at a potency, capped per effect: Travel and Rally a share of run speed,
    // Rested a share of a level's experience, Ward a share of the damage taken, Campfire and Mend
    // a share of the maximum health (and mana) a second.
    float Magnitude(SongDef const& s, float potency);
    float CapOf(Effect e);
    constexpr float kCampfireExtend = 0.5f;                     // a fire song lengthens the others by half their length
    constexpr uint32_t kRestedCooldownMs = 30u * 60u * 1000u;   // one song's rest a listener per half hour
    constexpr uint32_t kCombatCooldownMs = 30000;               // a combat song again after this
    constexpr float kPartyRange = 30.f;
    constexpr float kRoomRange = 20.f;                          // an inn's listeners
    constexpr float kFireRange = 8.f;

    // Where a song may be started.
    struct Place
    {
        bool resting = false;           // an inn or a city (PLAYER_FLAGS_RESTING)
        bool fire = false;              // a campfire or cooking fire within kFireRange
        bool inCombat = false;
        bool coolingDown = false;       // this combat song played within kCombatCooldownMs
    };
    enum class Refusal : uint8_t { Ok, NotLearned, SkillTooLow, WrongInstrument, InCombat, NotAtInn, NotAtFire, Cooldown };
    Refusal CanPerform(SongDef const& s, InstrumentDef const& i, Tier tier, uint16_t skill, Place const& p, bool gm = false);
    // Whether entering combat ends the song: every song but the combat ones.
    inline bool CombatInterrupts(SongDef const& s) { return !CombatSong(s.effect); }

    // The skill after a performance: the song's colour gives vanilla's chance (orange always, gray
    // never), times how well it was played (nothing under kFailQuality, full from 0.8); a song
    // played at 0.9 or better while orange or yellow gives two points. A robotic performance
    // teaches as a quality of at most kRoboticQuality (and never two points).
    float SkillUpChance(SongDef const& s, uint16_t skill, float quality);
    uint16_t SkillGain(SongDef const& s, uint16_t skill, uint16_t cap, float quality, bool robotic, float roll);

    // ── The songs on a listener ──
    struct Buff
    {
        uint16_t song = 0;
        float value = 0.f;
        uint32_t untilMs = 0;
        bool extended = false;          // a fire song has lengthened it already
    };
    // One buff an effect: a new song replaces one of its effect when at least as strong, or when
    // the old one would end sooner; a fire song lengthens the others (once each). A Rested buff
    // only marks the rest given (its cooldown).
    void ApplyBuff(std::vector<Buff>& buffs, SongDef const& s, float value, uint32_t nowMs);
    void Expire(std::vector<Buff>& buffs, uint32_t nowMs);
    bool HasEffect(std::vector<Buff> const& buffs, Effect e, uint32_t nowMs);
    float SpeedFactor(std::vector<Buff> const& buffs, uint32_t nowMs, bool inCombat);
    float DamageTakenFactor(std::vector<Buff> const& buffs, uint32_t nowMs);
    // Health (and mana) a second, a share of the maximum.
    float RegenPerSecond(std::vector<Buff> const& buffs, uint32_t nowMs, bool inCombat);
    // [55, n u8, n x (song u16, value u16 per ten thousand, ms left u32)]
    std::vector<uint8_t> EncodeBuffs(std::vector<Buff> const& buffs, uint32_t nowMs);
    bool DecodeBuffs(std::vector<uint8_t> const& body, std::vector<Buff>& out, uint32_t nowMs);

    // ── Tips ──
    constexpr uint32_t kMaxTip = 10000;             // one tip, copper (1 gold)
    constexpr uint32_t kTipGiveDaily = 50000;       // a character gives at most 5 gold a day
    constexpr uint32_t kTipGetDaily = 100000;       // a minstrel takes at most 10 gold a day from players
    constexpr uint32_t kHeardWindowMs = 60000;      // a song may be tipped this long after it lands
    struct TipAsk
    {
        uint32_t tipperAccount = 0;
        uint32_t performerAccount = 0;
        uint32_t copper = 0;
        uint32_t money = 0;             // the tipper's
        uint32_t givenToday = 0;        // by the tipper
        uint32_t gotToday = 0;          // by the minstrel, from players
        bool heard = false;             // a song of the minstrel's heard within kHeardWindowMs
        bool tipped = false;            // that song tipped already by this tipper
    };
    enum class TipCheck : uint8_t { Ok, Zero, SameAccount, NotHeard, AlreadyTipped, TooMuch, GiveCap, GetCap, NoMoney };
    TipCheck CheckTip(TipAsk const& a);
    // The crowd's tip (the friendly people and wanderers listening at an inn or in a city): none
    // under quality 0.5, for a gray song or with nobody listening; a fifteenth of the day's cap for
    // a perfect song before five listeners; never past the character's or the account's day cap.
    uint32_t CrowdDailyCap(uint8_t level);
    constexpr uint32_t kCrowdAccountDaily = 6000;   // 60 silver a day for all of an account's characters
    constexpr uint32_t kCrowdListeners = 5;
    uint32_t CrowdTip(uint8_t level, float quality, uint32_t listeners, Color songColor, uint32_t crowdToday, uint32_t accountCrowdToday);
    // The day a tip counts on (unix seconds).
    inline uint32_t DayOf(int64_t unixSeconds) { return uint32_t(unixSeconds / 86400); }

    std::vector<uint8_t> EncodeSkillOf(Tier t, uint16_t skill);
    struct Heard
    {
        uint64_t performer = 0;
        uint16_t song = 0;
        uint16_t potencyPm = 0;
        std::string name;
    };
    std::vector<uint8_t> EncodeHeard(Heard const& h);
    std::vector<uint8_t> EncodeTip(uint64_t performer, uint32_t copper);
    bool DecodeTip(std::vector<uint8_t> const& body, uint64_t& performer, uint32_t& copper);
    std::vector<uint8_t> EncodeTipped(bool ok, uint32_t copper, std::string const& text);

    // ── The Minstrelsy window (a trainer's, or the HUD's limited view) ──
    // ── Making instruments: Minstrelsy's own recipes ──
    // Every instrument is made by minstrels, of other trades' goods (lumber, leather, bars,
    // thread). A recipe is learned from a Minstrel Trainer once the skill reaches it (the Thunder
    // Drums' only with its score, a rare drop, handed over); making one teaches by vanilla's
    // colours (Construction::Spaced from the requirement). Anywhere: no tools or fire asked.
    constexpr uint32_t kThunderDrumsScore = 94110;   // the score the Thunder Drums are learned from
    struct Recipe
    {
        uint16_t id = 0;
        Instrument instrument = Instrument::None;
        uint16_t required = 1;
        uint32_t learnCost = 0;         // copper, at the trainer
        uint32_t score = 0;             // an item the trainer takes to teach it (0: none)
        std::vector<Construction::Material> reagents;
    };
    std::vector<Recipe> const& Recipes();
    Recipe const* RecipeOf(uint16_t id);
    Thresholds SkillOf(Recipe const& r);
    enum class LearnCheck : uint8_t { Ok, NotLearned, Known, SkillTooLow, NoScore, NotEnoughMoney };
    LearnCheck CanLearn(Recipe const& r, Tier tier, uint16_t skill, bool known, bool hasScore, uint32_t money, bool gm = false);
    enum class MakeCheck : uint8_t { Ok, Unknown, SkillTooLow, NoReagents };
    MakeCheck CanMake(Recipe const& r, bool known, uint16_t skill, Construction::Inventory const& bags, bool gm = false);
    // The skill after making one (`roll` in [0, 1)), never past `cap`.
    uint16_t MakeSkillUp(Recipe const& r, uint16_t skill, uint16_t cap, float roll);

    enum class Action : uint8_t { None = 0, Train = 1, Buy = 2, Learn = 3, Make = 4 };
    struct Service
    {
        Action action = Action::None;
        uint32_t arg = 0;
        uint32_t cost = 0;
        bool enabled = true;
        uint8_t hue = 0;                // Homestead::Hue
        std::string label;
        std::string note;
    };
    // [62, open u8, trainer u8, tier u8, skill u16, cap u16, tone u8, message str8, n u8,
    //  n x (action u8, arg u32, cost u32, enabled u8, hue u8, label str8, note str8)]
    struct Window
    {
        bool open = false;              // a trainer was spoken to: open it
        bool trainer = false;           // its buttons work
        Tier tier = Tier::None;
        uint16_t skill = 0;
        uint8_t tone = 0;               // Homestead::Tone
        std::string message;
        std::vector<Service> services;
    };
    constexpr size_t kMaxServices = 24;
    std::vector<uint8_t> EncodeWindow(Window const& w);
    bool DecodeWindow(std::vector<uint8_t> const& body, Window& out);
    std::vector<uint8_t> EncodeAct(Action a, uint32_t arg);
    bool DecodeAct(std::vector<uint8_t> const& body, Action& a, uint32_t& arg);
    // Make's arg: the recipe id in its low 16 bits, how many in its high (0 one: Create; more:
    // Create All), held to 1..kMaxMakeBatch.
    constexpr uint16_t kMaxMakeBatch = 20;
    uint16_t MakeRecipeOf(uint32_t arg);
    uint16_t MakeCountOf(uint32_t arg);
    uint32_t MakeArg(uint16_t recipe, uint16_t count);

    // ── The trade window's book (the client mirrors WoW's TradeSkillFrame with it) ──
    // [63, n u8, n x (id u16, instrument u8, required u16, yellow u16, green u16, gray u16,
    //  known u8, item u32, reagents u8, reagents x (item u32, count u16))]: every recipe, known
    // or not, sent with the window and at login.
    constexpr uint8_t kOpBook = 63;
    struct BookEntry
    {
        uint16_t id = 0;
        Instrument instrument = Instrument::None;
        Thresholds skill;
        bool known = false;
        uint32_t item = 0;
        std::vector<Construction::Material> reagents;
    };
    std::vector<BookEntry> Book(std::vector<uint16_t> const& known);
    std::vector<uint8_t> EncodeBook(std::vector<BookEntry> const& book);
    bool DecodeBook(std::vector<uint8_t> const& body, std::vector<BookEntry>& out);
}

#endif
