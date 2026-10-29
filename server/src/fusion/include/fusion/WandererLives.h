/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_WANDERER_LIVES_H
#define FUSION_WANDERER_LIVES_H

#include "fusion/Wanderers.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Wanderers, Phase 2 (design/WANDERERS.md section 13): their lives. Each wanderer has a trade
// (a fisherman, a miner, a herbalist, a trapper, a peddler, a courier, a sellsword, a minstrel or a
// pilgrim) with a home, a workplace and a market; a day shaped by the clock (sleep at night, work in
// the day, an inn in the evening) and by a cycle of days (a market trip every few work days, a day
// off now and then); a personality (brave or cowardly, chatty or reserved, greedy or generous,
// curious, lawful or roguish, sociable) with moods and needs (fatigue, hunger); friends and rivals
// among the other wanderers; news of its own days to talk about; and a pack whose goods really
// change: gathered from the world's own loot tables at its workplace, sold to a market's vendors at
// their own prices, and offered to players at a stall that is its pack.
//
// Everything here is plain data and pure functions, so it is tested (tests/WandererLifeTests.cpp);
// the game (game/Fusion/FusionWanderers.cpp, FusionWandererLives.cpp) feeds it the world.
namespace Fusion::Wanderers
{
    // ── Trades ──────────────────────────────────────────────────────────────────────────────

    enum class Occupation : uint8_t
    {
        None = 0,       // a Phase 1 roamer (no trade): roads, bounties
        Fisherman = 1,
        Miner = 2,
        Herbalist = 3,
        Trapper = 4,
        Peddler = 5,
        Courier = 6,
        Sellsword = 7,
        Minstrel = 8,
        Pilgrim = 9,
        Count = 10,
    };

    // A thing it may gather or carry: an item entry, how likely (relative weight), how many.
    struct Ware
    {
        uint32_t item = 0;
        uint16_t weight = 1;
        uint8_t minCount = 1, maxCount = 1;
    };

    struct OccupationDef
    {
        Occupation id = Occupation::None;
        char const* name = "";          // "Fisherman"
        char const* trade = "";         // "fishing"
        NodeKind work = NodeKind::Town; // where it works
        bool gathers = false;           // its work yields goods (from the spot's loot table)
        uint32_t workMinMs = 0, workMaxMs = 0;  // one session of work
        uint8_t sessionsPerMarket = 3;  // gatherers: work sessions before a market trip
        uint8_t brave = 50;             // its trade's courage (a sellsword's high, a minstrel's low)
        uint8_t bountyPercent = 0;      // the share of its days' plans that send it to a board
        uint16_t share = 10;            // how often a new wanderer takes it up
        std::vector<Ware> fallback;     // its goods when the world has no loot table for the spot
        std::vector<uint32_t> rares;    // the rare finds of its trade
    };
    OccupationDef const& OccupationInfo(Occupation o);
    char const* OccupationName(Occupation o);
    // A new wanderer's trade, by the seed and the shares, among those whose workplace the region
    // has (`hasKind(kind)`); None when it has none of them.
    Occupation OccupationFor(uint32_t seed, std::function<bool(NodeKind)> const& hasKind);
    // Its title under its name: "Fisherman of Lakeshire" (just the trade with no home).
    std::string Title(Occupation o, std::string const& home);

    // ── Personality ─────────────────────────────────────────────────────────────────────────

    // Each from 0 to 100; the name is the high end (brave 90 is very brave, 10 a coward).
    struct Traits
    {
        uint8_t brave = 50;
        uint8_t chatty = 50;
        uint8_t greedy = 50;
        uint8_t curious = 50;
        uint8_t lawful = 50;
        uint8_t sociable = 50;
    };
    // By the seed, its trade leaning its courage.
    Traits TraitsFor(uint32_t seed, Occupation o);
    // "brave, chatty, generous, curious, lawful, sociable": the traits that stand out (above 65 or
    // below 35), at most four; "even-tempered" when none does.
    std::string DescribeTraits(Traits const& t);

    // Its needs, 0 to 100: mood (100 cheerful), fatigue and hunger (100 spent, starving).
    struct Needs
    {
        uint8_t mood = 60;
        uint8_t fatigue = 20;
        uint8_t hunger = 20;
    };

    // What it is doing with its day.
    enum class Activity : uint8_t
    {
        None = 0,
        Sleep = 1,      // at night, at home or an inn
        Eat = 2,        // a meal at an inn
        Rest = 3,       // tired: a sit at an inn
        Work = 4,       // its trade at its workplace
        Sell = 5,       // at a market: to the vendors, and a stall for players
        Stock = 6,      // a peddler buys goods to carry
        Deliver = 7,    // a courier's parcel to its door
        Perform = 8,    // a minstrel plays an inn
        Socialize = 9,  // an evening at an inn with company
        Sightsee = 10,  // a detour to a landmark (a pilgrim's work)
        Visit = 11,     // to where a friend is
        Bounty = 12,    // to a board for a bounty (the Phase 1 hunt)
        Count = 13,
    };
    char const* ActivityName(Activity a);
    // A verb phrase for talk: "fishing", "selling my catch", "sleeping".
    char const* ActivityPhrase(Activity a, Occupation o);

    // Its needs over `ms` of doing `a`: fatigue rises on the road and at work and falls asleep or
    // at rest, hunger rises and a meal ends it, mood follows both (and drops with a fight lost).
    void TickNeeds(Needs& n, Activity a, uint32_t ms);
    // Its mood nudged by something that happened (a sale, a chat, a fight, a death), held 0-100.
    void Nudge(Needs& n, int delta);

    // ── Decisions by nature ─────────────────────────────────────────────────────────────────

    // The health it runs below when attacked: a coward early, the brave never (0).
    float FleeBelowPct(Traits const& t);
    // Whether it starts fights with what it meets (the brave and the roguish do; the peaceable
    // only fight back, and a sellsword always hunts).
    bool StartsFights(Traits const& t, Occupation o);
    // A curious one takes a detour to a sight on its way now and then.
    bool TakesDetour(Traits const& t, uint32_t roll);
    // Two wanderers meeting on the road stop to talk: the sociable, friends, and the chatty
    // likelier; rivals sometimes too (to trade barbs).
    bool StopsToChat(Traits const& a, Traits const& b, int affinity, uint32_t roll);
    constexpr uint32_t kChatMs = 9000;          // a chat on the road
    constexpr float kChatRange = 9.f;
    // It agrees to walk with a player a while: not a coward, not one who hates them; the greedy
    // for more coin.
    bool AcceptsHire(Traits const& t, Occupation o, int32_t bondPoints);
    // What it asks to walk with a player for kHireMs (copper).
    uint32_t HirePrice(uint8_t level, Traits const& t, Occupation o, uint8_t bondTier);
    constexpr uint32_t kHireMs = 10 * 60 * 1000;

    // ── The pack ────────────────────────────────────────────────────────────────────────────

    struct Stock
    {
        uint32_t item = 0;
        uint16_t count = 0;
        bool rare = false;
    };
    constexpr size_t kMaxStock = 12;            // stacks in its pack
    constexpr uint16_t kMaxStack = 40;          // of each
    // Adds what fits; the number that did not.
    uint16_t AddStock(std::vector<Stock>& pack, uint32_t item, uint16_t count, bool rare = false);
    // Takes up to `count`; the number taken.
    uint16_t TakeStock(std::vector<Stock>& pack, uint32_t item, uint16_t count);
    uint32_t CountStock(std::vector<Stock> const& pack, uint32_t item);
    uint32_t PackItems(std::vector<Stock> const& pack);

    // A loot table's row, as the world's own (fishing_loot_template, gameobject_loot_template,
    // skinning_loot_template): its chance in percent (0 in a group: an equal share of what the
    // group's other rows leave), its group (0 none), its counts.
    struct LootRow
    {
        uint32_t item = 0;
        float chance = 0.f;
        uint8_t group = 0;
        uint8_t minCount = 1, maxCount = 1;
    };
    // What a workplace yields: its loot rows (empty: the trade's fallback wares), and the trade's
    // rare finds at `rarePermille` a gather.
    struct YieldTable
    {
        std::vector<LootRow> rows;
        std::vector<Ware> fallback;
        std::vector<uint32_t> rares;
        uint32_t rarePermille = 12;
    };
    YieldTable YieldFor(Occupation o, std::vector<LootRow> rows);
    // One gather, vanilla's way: each ungrouped row by its chance, one row of each group; or one
    // fallback ware by weight. A rare find on top by its odds. The same roll gives the same goods.
    std::vector<Stock> RollGather(YieldTable const& t, uint32_t roll);
    // A gather every kGatherMs of work.
    constexpr uint32_t kGatherMs = 60 * 1000;
    // A session of `ms` of work: its gathers (at least one), all into one list (stacked).
    std::vector<Stock> WorkYield(YieldTable const& t, uint32_t ms, uint32_t seed);

    // An item's prices (item_template): what a vendor pays for one, and what it asks.
    struct Price
    {
        uint32_t sell = 0;
        uint32_t buy = 0;
    };
    // What it asks a player for one (copper): its trade's goods at about twice a vendor's buying
    // price, more the greedier, a friend less, a rare find dearer. Never at or under what a vendor
    // pays (no buying from it to sell on at a profit), never nothing.
    uint32_t StallPrice(Price p, Traits const& t, bool rare, uint8_t bondTier);
    // At a market: it sells to the vendors (their price) all but what it keeps for its stall, a
    // few of each (more the greedier) and every rare find. The copper it got and how many it sold.
    struct Sale
    {
        uint32_t copper = 0;
        uint32_t items = 0;
    };
    Sale SellAtMarket(std::vector<Stock>& pack, std::function<Price(uint32_t)> const& priceOf, Traits const& t);
    uint16_t StallKeeps(Traits const& t);
    // A peddler stocks up from a market's vendors (`goods`: what they sell, and its prices): up to
    // four goods it can carry, a few of each, at a vendor's price, from no more than half its
    // purse (the greedy keep more back). It spends from `money`; what it bought.
    std::vector<Stock> StockUp(std::vector<std::pair<uint32_t, Price>> const& goods, uint32_t& money, Traits const& t,
        std::vector<Stock>& pack, uint32_t roll);
    // A meal or a bed at an inn (copper), by level.
    uint32_t InnCost(uint8_t level, Activity a);
    // A minstrel's tips for a performance, a courier's pay for a parcel (copper).
    uint32_t Tips(uint8_t level, uint32_t listeners, uint32_t roll);
    uint32_t CourierPay(uint8_t level, float yards);

    // ── Its days ────────────────────────────────────────────────────────────────────────────

    // The day number (days since 1970, local).
    uint32_t DayOf(int64_t unixLocal);
    uint8_t HourOf(int64_t unixLocal);
    // A day off: one in every five to eight, by the seed.
    bool IsDayOff(uint32_t seed, uint32_t day);
    // The time of day for talk: "night", "morning", "afternoon", "evening".
    char const* TimeOfDay(uint8_t hour);

    // A courier's parcel: from a place to a place, for a sender.
    struct Parcel
    {
        uint32_t from = 0, to = 0;      // node ids (0: none carried)
        uint32_t pay = 0;
        std::string sender;             // "the Goldshire smith"
        bool Carried() const { return to != 0; }
    };

    // What it knows of its day when it plans.
    struct DayContext
    {
        Occupation occupation = Occupation::None;
        Traits traits;
        Needs needs;
        uint8_t hour = 12;
        uint32_t day = 0;
        uint32_t seed = 0;
        uint8_t sessions = 0;           // work sessions since its last market
        uint32_t packItems = 0;
        bool parcel = false;            // a courier carrying one
        bool lastWasDetour = false;
        bool friendKnown = false;       // a friend's whereabouts (Haunts::friendAt)
    };
    // What it means to do next (before where).
    Activity Scheduled(DayContext const& c, uint32_t roll);

    // Where its life happens, as node indexes of its region's network (-1 none).
    struct Haunts
    {
        int home = -1;                  // where it sleeps (an inn or a town of its home)
        int work = -1;                  // its workplace
        int market = -1;                // where it sells
        int parcelTo = -1;              // a courier's parcel's door
        int friendAt = -1;              // the settlement a friend is at
        std::vector<int> works;         // the other workplaces of its kind (it varies)
        std::vector<int> markets;
        std::vector<int> inns;
        std::vector<int> landmarks;
        std::vector<int> boards;
        std::vector<int> settlements;
    };
    // A plan: what to do and where.
    struct Plan
    {
        Activity activity = Activity::None;
        int node = -1;
        bool detour = false;
    };
    // The next plan from node `at`: the scheduled activity at its place (an inn nearby for a meal,
    // its own workplace mostly but now and then another, the market, a landmark not seen lately, a
    // friend), a curious one's detour first now and then. Never node -1 when the network has places.
    Plan PlanNext(DayContext const& c, Haunts const& h, RoadNet const& net, int at, std::vector<int> const& recent,
        uint32_t roll);
    // How long it does it (ms): a night's sleep until six (at least a few minutes), work by its
    // trade, a meal quick, an evening long for the sociable.
    uint32_t ActivityMs(Activity a, OccupationDef const& d, Traits const& t, uint8_t hour, uint32_t roll);

    // ── Friends and rivals ──────────────────────────────────────────────────────────────────

    struct Tie
    {
        uint32_t other = 0;             // the other wanderer's id
        int8_t affinity = 0;            // -100 a sworn rival, 100 a dear friend
        uint16_t met = 0;               // times they met
    };
    constexpr int8_t kFriendAt = 30, kRivalAt = -30;
    constexpr size_t kMaxTies = 6;
    char const* TieName(int8_t affinity);   // "friend", "rival", "acquaintance"
    // Its first ties among its region's others (`others`: their ids, not its own): one to three,
    // mostly friendly, now and then a rival.
    std::vector<Tie> FirstTies(uint32_t id, std::vector<uint32_t> const& others, uint32_t seed);
    // They met: alike natures warm, a lawful one and a roguish one cool; a chat warms. Adds the
    // tie if new (dropping the weakest past kMaxTies).
    void Meet(std::vector<Tie>& ties, uint32_t other, Traits const& mine, Traits const& theirs, uint32_t roll);
    Tie const* TieWith(std::vector<Tie> const& ties, uint32_t other);
    // Its best friend and worst rival (0 none).
    uint32_t BestFriend(std::vector<Tie> const& ties);
    uint32_t WorstRival(std::vector<Tie> const& ties);

    // ── News: what happened to it lately ────────────────────────────────────────────────────

    enum class NewsKind : uint8_t
    {
        None = 0,
        Catch = 1,      // a rare find (what: the item's name)
        Sold = 2,       // a good market day (what: the copper as text)
        Fight = 3,      // it killed something (what: its name)
        Fled = 4,       // it ran from something
        Died = 5,       // it was killed (and came back)
        Bounty = 6,     // a bounty done (what: its title)
        Aircraft = 7,   // a killstreak's craft overhead
        Building = 8,   // a player building nearby
        Friend = 9,     // it met a friend (what: the friend's name)
        Gone = 10,      // a friend moved away (what: the friend's name)
        Helped = 11,    // a player helped it (what: the player's name)
        Count = 12,
    };
    struct News
    {
        NewsKind kind = NewsKind::None;
        std::string what;
        std::string where;              // a place name ("Lakeshire")
        uint32_t day = 0;
    };
    constexpr size_t kMaxNews = 4;
    // The newest first; older news of the same kind is replaced.
    void AddNews(std::vector<News>& news, News n);

    // ── Talk ────────────────────────────────────────────────────────────────────────────────

    // What a line may speak of. Empty fields fall back to plain words.
    struct Talk
    {
        std::string name;               // its own
        std::string player;             // the one it talks to (or another wanderer)
        std::string home, work, market, here, heading;      // place names
        std::string friendName, rivalName;
        std::string weather;            // "rain", "snow", "storm" or empty (fine)
        Occupation occupation = Occupation::None;
        Activity doing = Activity::None;
        Traits traits;
        Needs needs;
        uint8_t hour = 12;
        uint8_t bondTier = 0;
        News news;                      // its latest (kind None: nothing new)
    };
    // A remark to someone it meets (a player passing, a wanderer it stops with): weather and time,
    // its trade, its mood, its news.
    std::string ChatLine(Talk const& t, uint32_t seed);
    // The other's answer in a chat between two wanderers.
    std::string ReplyLine(Talk const& t, uint32_t seed);
    // "Tell me about yourself."
    std::string AboutLine(Talk const& t, std::string const& backstory, uint32_t seed);
    // "Where are you headed?"
    std::string HeadingLine(Talk const& t, uint32_t seed);
    // Its news as a line ("Caught a Mightfish near Lakeshire this morning!"); empty for none.
    std::string NewsLine(Talk const& t, uint32_t seed);
    // "Heard any rumours?": a bounty posted (`bounty`) or a rare creature seen near a place.
    std::string RumourLine(std::string const& what, std::string const& where, bool bounty, uint32_t seed);
    std::string NoRumourLine(uint32_t seed);
    // A world event in sight: an aircraft overhead, a player building.
    std::string EventLine(NewsKind k, Talk const& t, uint32_t seed);
    // Running from a fight: help asked of a player near by name (empty: of anyone).
    std::string FleeLine(Talk const& t, uint32_t seed);
    // A player killed what was on it.
    std::string ThanksLine(Talk const& t, uint32_t seed);
    // At its work, its rest, a sale, a performance (for a player near).
    std::string ActivityLine(Talk const& t, uint32_t seed);
    // A hire agreed, refused, and ended.
    std::string HireLine(Talk const& t, bool accepted, uint32_t seed);
    // Made at its creation: where it is from and how it came to its trade.
    std::string Backstory(Occupation o, uint8_t race, std::string const& home, Traits const& t, uint32_t seed);
    // A line fit to say: not empty, at most 255 characters, no unfilled {token}.
    bool LineIsClean(std::string const& line);

    // Lines a map may hear: at most `perMinute` a minute (a wanderer's own gap is its business).
    struct ChatBudget
    {
        uint32_t windowMs = 0;
        uint32_t used = 0;
    };
    bool MaySpeak(ChatBudget& b, uint32_t nowMs, uint32_t perMinute);

    // ── Its whole life, kept ────────────────────────────────────────────────────────────────

    struct Life
    {
        Occupation occupation = Occupation::None;
        Traits traits;
        Needs needs;
        uint32_t home = 0, work = 0, market = 0;    // node ids
        std::string homeName;                       // the title's place ("Lakeshire")
        std::vector<Stock> pack;
        Activity doing = Activity::None;            // what it is doing now (paper and world)
        uint32_t doingNode = 0;                     // where (node id)
        uint8_t sessions = 0;
        bool lastWasDetour = false;
        Parcel parcel;
        std::vector<News> news;
        std::vector<Tie> ties;
        uint32_t born = 0;                          // its first day
        uint32_t retireDay = 0;                     // the day it moves away
        uint32_t generation = 0;                    // which person holds this id (a new one each move)
        uint32_t earned = 0;                        // copper from its trade, all told
        std::string backstory;
    };
    // A new life for wanderer `id` on day `today`: trade, nature, a backstory (home filled by the
    // game), a span of `lifeDays` (+/- a third, by the seed) before it moves away; 0 never.
    Life NewLife(uint32_t seed, Occupation o, uint8_t race, std::string const& homeName, uint32_t today, uint32_t lifeDays);
    bool Retires(Life const& l, uint32_t today);
    // "L1|..." text; names and lines lose '|', ';', ':' and ',' (stripped). Decode leaves `l`
    // untouched for text it cannot read.
    std::string EncodeLife(Life const& l);
    bool DecodeLife(std::string const& text, Life& l);
}

#endif
