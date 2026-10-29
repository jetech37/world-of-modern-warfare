/*
 * Azeroth Warfare: tests for the bounty boards (Bounties.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Bounties.h"

#include <set>
#include <string>

using namespace Fusion::Bounty;

namespace
{
    CreatureFacts Ordinary(uint32_t entry, uint8_t level)
    {
        CreatureFacts c;
        c.entry = entry;
        c.minLevel = level;
        c.maxLevel = level;
        c.type = 7;                 // humanoid
        c.openWorldSpawns = 20;
        c.longestRespawnSecs = 300;
        return c;
    }

    ZonePool Westfall()
    {
        ZonePool p;
        p.zone = 40;
        p.zoneName = "Westfall";
        p.band = {9, 18};
        uint8_t level = 9;
        for (uint32_t e = 100; e < 109; ++e, ++level)
            p.kills.push_back({e, "Creature " + std::to_string(e), level, 12, 17});
        p.collects.push_back({2000, "Coyote Meat", 101, "Coyote", 10, 0.5f});
        p.collects.push_back({2001, "Linen Cloth", 102, "Defias Looter", 11, 0.3f});
        p.hunts.push_back({500, "Foe Reaper 4000", 20, true});
        Camp c;
        c.members = {p.kills[2], p.kills[3]};
        c.level = 12;
        c.place = "Jangolode Mine";
        p.camps.push_back(c);
        return p;
    }

    bool InPool(ZonePool const& p, uint32_t creature)
    {
        for (auto const& k : p.kills)
            if (k.entry == creature)
                return true;
        for (auto const& h : p.hunts)
            if (h.entry == creature)
                return true;
        return false;
    }
}

TEST(bounty_days_count_from_1970_and_turn_at_the_reset_hour)
{
    CHECK(DaysFromCivil(1970, 1, 1) == 0);
    CHECK(DaysFromCivil(1970, 1, 2) == 1);
    CHECK(DaysFromCivil(2000, 3, 1) == 11017);
    CHECK(DaysFromCivil(2024, 3, 1) - DaysFromCivil(2024, 2, 28) == 2);     // a leap year
    CHECK(DaysFromCivil(2026, 1, 1) - DaysFromCivil(2025, 12, 31) == 1);

    LocalTime t{2026, 10, 20, 5, 59, 59};
    LocalTime u{2026, 10, 20, 6, 0, 0};
    uint32_t const d = uint32_t(DaysFromCivil(2026, 10, 20));
    // Before six it is still yesterday's board; at six the new day starts.
    CHECK(DayOf(t, 6) == d - 1);
    CHECK(DayOf(u, 6) == d);
    CHECK(SecondsToReset(t, 6) == 1);
    CHECK(SecondsToReset(u, 6) == 86400);
    // A midnight reset.
    CHECK(DayOf(LocalTime{2026, 10, 20, 0, 0, 0}, 0) == d);
    CHECK(SecondsToReset(LocalTime{2026, 10, 20, 23, 0, 0}, 0) == 3600);
}

TEST(bounty_runs_through_its_day_and_the_next)
{
    CHECK(!IsExpired(100, 100));
    CHECK(!IsExpired(100, 101));
    CHECK(IsExpired(100, 102));
}

TEST(bounty_quest_ids_rotate_through_three_blocks)
{
    // Today's, yesterday's and the next day's bounties never share an id.
    std::set<uint32_t> ids;
    for (uint32_t day = 1000; day < 1003; ++day)
        for (uint32_t zone = 0; zone < kMaxZones; ++zone)
            for (uint32_t i = 0; i < kPerZone; ++i)
            {
                uint32_t const id = QuestId(day, zone, i);
                CHECK(IsBountyQuest(id));
                CHECK(ids.insert(id).second);
                QuestSlot s;
                CHECK(SlotOf(id, s) && s.block == BlockOf(day) && s.zoneSlot == zone && s.index == i);
            }
    CHECK(ids.size() == kBlocks * kBlockSize);
    // The fourth day reuses the first day's ids: by then they have expired.
    CHECK(QuestId(1003, 7, 2) == QuestId(1000, 7, 2));
    CHECK(IsExpired(1000, 1003));
    CHECK(QuestId(1, kMaxZones, 0) == 0 && QuestId(1, 0, kPerZone) == 0);
    QuestSlot s;
    CHECK(!SlotOf(kFirstQuest - 1, s) && !SlotOf(kLastQuest + 1, s));
    // Clear of vanilla's quests (the highest is under 10000) and of our content's 90000s.
    CHECK(kFirstQuest > 90000 && kLastQuest < 100000);
}

TEST(bounty_kill_targets_are_ordinary_creatures_of_the_zone)
{
    ZoneBand const band{9, 18};
    CreatureFacts c = Ordinary(100, 12);
    CHECK(IsKillTarget(c, 10, band));
    CHECK(!IsKillTarget(c, kMinZoneSpawns - 1, band));      // too few here
    CreatureFacts elite = c;
    elite.rank = kRankElite;
    CHECK(!IsKillTarget(elite, 10, band));
    CreatureFacts town = c;
    town.townsfolk = true;
    CHECK(!IsKillTarget(town, 10, band));
    CreatureFacts critter = c;
    critter.type = kTypeCritter;
    CHECK(!IsKillTarget(critter, 10, band));
    CreatureFacts dungeon = c;
    dungeon.instanceSpawns = 1;
    CHECK(!IsKillTarget(dungeon, 10, band));
    CreatureFacts friendly = c;
    friendly.hostile = false;
    CHECK(!IsKillTarget(friendly, 10, band));
    CreatureFacts untouchable = c;
    untouchable.unattackable = true;
    CHECK(!IsKillTarget(untouchable, 10, band));
    CreatureFacts few = c;
    few.openWorldSpawns = 3;
    CHECK(!IsKillTarget(few, 10, band));
    CreatureFacts ours = c;
    ours.entry = 91001;
    CHECK(!IsKillTarget(ours, 10, band));
    // The band, with a little slack each way.
    CHECK(IsKillTarget(Ordinary(1, 8), 10, band));
    CHECK(!IsKillTarget(Ordinary(1, 7), 10, band));
    CHECK(IsKillTarget(Ordinary(1, 20), 10, band));
    CHECK(!IsKillTarget(Ordinary(1, 21), 10, band));
}

TEST(bounty_hunts_are_rares_and_named_elites_that_come_back)
{
    ZoneBand const band{18, 30};
    CreatureFacts rare = Ordinary(200, 25);
    rare.rank = kRankRare;
    rare.openWorldSpawns = 1;
    rare.longestRespawnSecs = 2 * 3600;
    CHECK(IsHuntTarget(rare, 1, band));
    CHECK(!IsHuntTarget(rare, 0, band));                    // not in this zone
    CreatureFacts slow = rare;
    slow.longestRespawnSecs = 10 * 3600;
    CHECK(!IsHuntTarget(slow, 1, band));
    CreatureFacts named = rare;
    named.rank = kRankElite;
    CHECK(IsHuntTarget(named, 1, band));
    CreatureFacts camp = named;
    camp.openWorldSpawns = 12;                              // an elite camp, not a name
    CHECK(!IsHuntTarget(camp, 12, band));
    CreatureFacts boss = rare;
    boss.rank = kRankBoss;
    CHECK(!IsHuntTarget(boss, 1, band));
    CHECK(!IsHuntTarget(Ordinary(1, 25), 10, band));        // a normal creature is a kill bounty
}

TEST(bounty_collects_are_plain_stackable_drops)
{
    ItemFacts meat;
    meat.entry = 2673;
    meat.itemClass = kItemClassTradeGoods;
    meat.quality = 1;
    meat.stackable = 10;
    CHECK(IsCollectItem(meat));
    ItemFacts fang = meat;
    fang.itemClass = kItemClassMisc;
    fang.quality = 0;
    CHECK(IsCollectItem(fang));
    ItemFacts bound = meat;
    bound.bonding = 4;                  // a quest item
    CHECK(!IsCollectItem(bound));
    ItemFacts unique = meat;
    unique.maxCount = 1;
    CHECK(!IsCollectItem(unique));
    ItemFacts single = meat;
    single.stackable = 1;
    CHECK(!IsCollectItem(single));
    ItemFacts green = meat;
    green.quality = 2;
    CHECK(!IsCollectItem(green));
    ItemFacts starter = meat;
    starter.startsQuest = true;
    CHECK(!IsCollectItem(starter));
    ItemFacts weapon = meat;
    weapon.itemClass = 2;
    CHECK(!IsCollectItem(weapon));
}

TEST(bounty_camps_are_creatures_of_one_family_standing_together)
{
    std::vector<KillCandidate> kills = {
        {1, "Defias Pillager", 14, 8, 17},
        {2, "Defias Looter", 15, 8, 17},
        {3, "Coyote", 12, 8, 31},          // another family, at the same spot
        {4, "Defias Smuggler", 15, 8, 17}, // the same family, far away
    };
    std::vector<SpawnPoint> spawns;
    for (int i = 0; i < 4; ++i)
    {
        spawns.push_back({1, 10.f * i, 0.f, 99});
        spawns.push_back({2, 10.f * i, 5.f, 99});
        spawns.push_back({3, 10.f * i, 2.f, 99});
        spawns.push_back({4, 1000.f + 10.f * i, 0.f, 98});
    }
    std::vector<Camp> camps = FindCamps(kills, spawns);
    CHECK(camps.size() == 1);
    if (camps.size() == 1)
    {
        CHECK(camps[0].members.size() == 2);
        CHECK(camps[0].members[0].entry == 1 && camps[0].members[1].entry == 2);
        CHECK(camps[0].level == 15);
        CHECK(camps[0].area == 99);
    }
    // Too far apart: no camp.
    std::vector<SpawnPoint> apart;
    for (int i = 0; i < 4; ++i)
    {
        apart.push_back({1, 10.f * i, 0.f, 99});
        apart.push_back({2, 500.f + 10.f * i, 0.f, 99});
    }
    CHECK(FindCamps(kills, apart).empty());
}

TEST(bounty_posting_is_the_same_for_the_same_day)
{
    ZonePool const p = Westfall();
    std::vector<Bounty> a = Post(p, 3, 20000);
    std::vector<Bounty> b = Post(p, 3, 20000);
    CHECK(a.size() == kPerZone && a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        CHECK(a[i].questId == b[i].questId && a[i].key == b[i].key && a[i].title == b[i].title);
        CHECK(a[i].questId == QuestId(20000, 3, uint32_t(i)));
    }
    // Other days and a GM's re-roll post other bounties (over a week, something changes).
    bool differs = false, rerolled = false;
    for (uint32_t day = 20001; day < 20008; ++day)
    {
        std::vector<Bounty> c = Post(p, 3, day);
        for (size_t i = 0; i < c.size() && i < a.size(); ++i)
            differs = differs || c[i].key != a[i].key;
    }
    for (uint32_t salt = 1; salt < 8; ++salt)
    {
        std::vector<Bounty> c = Post(p, 3, 20000, salt);
        for (size_t i = 0; i < c.size() && i < a.size(); ++i)
            rerolled = rerolled || c[i].key != a[i].key;
    }
    CHECK(differs);
    CHECK(rerolled);
}

TEST(bounty_posting_spreads_over_the_zones_levels_and_kinds)
{
    ZonePool const p = Westfall();
    for (uint32_t day = 30000; day < 30020; ++day)
    {
        std::vector<Bounty> v = Post(p, 0, day);
        CHECK(v.size() == kPerZone);
        if (v.size() < kPerZone)
            continue;
        // A kill from each third: low (9-11), middle (12-14), high (15-17).
        CHECK(v[0].kind == Kind::Kill && v[0].level <= 11);
        CHECK(v[1].kind == Kind::Kill && v[1].level >= 12 && v[1].level <= 14);
        CHECK(v[2].kind == Kind::Kill && v[2].level >= 15);
        CHECK(v[3].kind == Kind::Collect);
        CHECK(v[4].kind == Kind::Hunt || v[4].kind == Kind::Camp);
        std::set<uint32_t> keys;
        for (Bounty const& b : v)
        {
            CHECK(keys.insert(b.key).second);
            CHECK(b.zone == 40);
            CHECK(!b.title.empty() && !b.objectives.empty() && !b.details.empty() && !b.objective.empty());
            CHECK(b.minLevel <= b.level && b.minLevel >= 1);
            CHECK(b.reward.xp > 0 && b.reward.money > 0);
            for (Objective const& o : b.kills)
            {
                CHECK(InPool(p, o.creature));
                CHECK(o.count >= 1 && o.count <= kMaxObjectiveCount);
            }
            CHECK(b.kills.size() <= 4);
            if (b.kind == Kind::Collect)
                CHECK(b.item != 0 && b.itemCount >= 3 && b.itemCount <= 10 && b.kills.empty());
            if (b.kind == Kind::Hunt)
                CHECK(b.kills.size() == 1 && b.kills[0].count == 1 && b.elite);
            if (b.kind == Kind::Camp)
                CHECK(b.kills.size() == 2 && b.title.find("Jangolode Mine") != std::string::npos);
        }
    }
}

TEST(bounty_posting_makes_do_with_a_small_pool)
{
    ZonePool p;
    p.zone = 8;
    p.zoneName = "Swamp of Sorrows";
    p.kills.push_back({1, "Marsh Murloc", 36, 10, 18});
    std::vector<Bounty> v = Post(p, 1, 5);
    CHECK(v.size() == 1 && v[0].kind == Kind::Kill);
    p.collects.push_back({7, "Murloc Fin", 1, "Marsh Murloc", 36, 0.4f});
    p.collects.push_back({8, "Murloc Eye", 1, "Marsh Murloc", 36, 0.3f});
    v = Post(p, 1, 5);
    CHECK(v.size() == 3);
    ZonePool empty;
    CHECK(Post(empty, 1, 5).empty());
}

TEST(bounty_rewards_grow_with_level_and_difficulty)
{
    Reward const k10 = RewardFor(Kind::Kill, 10, false);
    Reward const k30 = RewardFor(Kind::Kill, 30, false);
    Reward const k60 = RewardFor(Kind::Kill, 60, false);
    CHECK(k10.xp < k30.xp && k30.xp < k60.xp);
    CHECK(k10.money < k30.money && k30.money < k60.money);
    // About a vanilla kill quest's experience: ~660 at 10, ~2340 at 30.
    CHECK(k10.xp >= 600 && k10.xp <= 700);
    CHECK(k30.xp >= 2200 && k30.xp <= 2500);
    CHECK(RewardFor(Kind::Collect, 30, false).xp == k30.xp);
    Reward const camp = RewardFor(Kind::Camp, 30, false);
    Reward const hunt = RewardFor(Kind::Hunt, 30, false);
    Reward const elite = RewardFor(Kind::Hunt, 30, true);
    CHECK(k30.xp < camp.xp && camp.xp < hunt.xp && hunt.xp < elite.xp);
    CHECK(k30.money < camp.money && camp.money < hunt.money && hunt.money < elite.money);
    CHECK(k30.reputation < camp.reputation && camp.reputation < hunt.reputation);
    CHECK(k60.moneyMaxLevel == k60.xp * 6);
    // Levels out of range are held to 1-60.
    CHECK(RewardFor(Kind::Kill, 0, false).xp == RewardFor(Kind::Kill, 1, false).xp);
    CHECK(RewardFor(Kind::Kill, 90, false).xp == k60.xp);
    CHECK(MinLevelFor(Kind::Kill, 12) == 8 && MinLevelFor(Kind::Hunt, 12) == 10 && MinLevelFor(Kind::Kill, 2) == 1);
}

TEST(bounty_daily_cap_counts_what_is_held_and_done)
{
    AcceptCheck c;
    c.playerLevel = 20;
    c.minLevel = 16;
    CHECK(CanAccept(c) == Refusal::None);
    AcceptCheck low = c;
    low.playerLevel = 15;
    CHECK(CanAccept(low) == Refusal::Level);
    AcceptCheck held = c;
    held.inLog = true;
    CHECK(CanAccept(held) == Refusal::Held);
    AcceptCheck again = c;
    again.doneToday = true;
    CHECK(CanAccept(again) == Refusal::DoneToday);
    AcceptCheck four = c;
    four.doneCount = 2;
    four.heldCount = 2;
    CHECK(CanAccept(four) == Refusal::None);
    AcceptCheck five = four;
    five.heldCount = 3;
    CHECK(CanAccept(five) == Refusal::Cap);
    AcceptCheck full = c;
    full.logFull = true;
    CHECK(CanAccept(full) == Refusal::LogFull);
    AcceptCheck far = c;
    far.nearBoard = false;
    CHECK(CanAccept(far) == Refusal::TooFar);
    AcceptCheck horde = c;
    horde.teamAllowed = false;
    CHECK(CanAccept(horde) == Refusal::WrongTeam);
    AcceptCheck gone = c;
    gone.posted = false;
    CHECK(CanAccept(gone) == Refusal::NotPosted);

    TurnInCheck t;
    CHECK(CanTurnIn(t) == Refusal::None);
    TurnInCheck unfinished = t;
    unfinished.complete = false;
    CHECK(CanTurnIn(unfinished) == Refusal::NotComplete);
    TurnInCheck elsewhere = t;
    elsewhere.sameZone = false;
    CHECK(CanTurnIn(elsewhere) == Refusal::OtherZone);
    TurnInCheck old = t;
    old.expired = true;
    CHECK(CanTurnIn(old) == Refusal::Expired);
    TurnInCheck capped = t;
    capped.doneCount = kDailyCap;
    CHECK(CanTurnIn(capped) == Refusal::Cap);
    TurnInCheck stranger = t;
    stranger.held = false;
    CHECK(CanTurnIn(stranger) == Refusal::NotHeld);
    for (uint8_t r = 1; r <= 12; ++r)
        CHECK(std::string(RefusalText(Refusal(r))).size() > 5);
}

TEST(bounty_board_message_round_trips)
{
    Board b;
    b.open = true;
    b.zone = 40;
    b.town = "Sentinel Hill";
    b.zoneName = "Westfall";
    b.done = 2;
    b.level = 14;
    b.secondsToReset = 3600;
    for (Bounty const& x : Post(Westfall(), 2, 777))
    {
        Row r;
        r.questId = x.questId;
        r.kind = x.kind;
        r.state = RowState::Accepted;
        r.level = x.level;
        r.minLevel = x.minLevel;
        r.elite = x.elite;
        r.xp = x.reward.xp;
        r.money = x.reward.money;
        r.reputation = uint16_t(x.reward.reputation);
        r.repName = "Stormwind";
        r.title = x.title;
        r.objective = x.objective;
        r.details = x.details;
        r.progress.push_back({3, 12, "Creature slain"});
        b.rows.push_back(r);
    }
    std::vector<uint8_t> const body = EncodeBoard(b);
    CHECK(body.size() < 8192);
    Board back;
    CHECK(DecodeBoard(body, back));
    CHECK(back.open && back.zone == 40 && back.town == "Sentinel Hill" && back.zoneName == "Westfall");
    CHECK(back.done == 2 && back.cap == kDailyCap && back.level == 14 && back.secondsToReset == 3600);
    CHECK(back.rows.size() == b.rows.size());
    for (size_t i = 0; i < back.rows.size() && i < b.rows.size(); ++i)
    {
        CHECK(back.rows[i].questId == b.rows[i].questId && back.rows[i].kind == b.rows[i].kind);
        CHECK(back.rows[i].title == b.rows[i].title && back.rows[i].objective == b.rows[i].objective);
        CHECK(back.rows[i].xp == b.rows[i].xp && back.rows[i].repName == "Stormwind");
        CHECK(back.rows[i].progress.size() == 1 && back.rows[i].progress[0].need == 12);
    }
    std::vector<uint8_t> cut(body.begin(), body.end() - 1);
    CHECK(!DecodeBoard(cut, back));

    bool ok = false;
    std::string text;
    CHECK(DecodeResult(EncodeResult(true, "Bounty taken."), ok, text) && ok && text == "Bounty taken.");

    uint8_t op = 0;
    uint32_t quest = 0;
    CHECK(DecodeRequest(EncodeAsk(), op, quest) && op == kOpAsk && quest == 0);
    CHECK(DecodeRequest(EncodeAccept(95012), op, quest) && op == kOpAccept && quest == 95012);
    CHECK(DecodeRequest(EncodeTurnIn(96001), op, quest) && op == kOpTurnIn && quest == 96001);
    CHECK(!DecodeRequest({kVersion, 9}, op, quest));
    CHECK(!DecodeRequest({2, kOpAsk}, op, quest));
    CHECK(!DecodeRequest({kVersion, kOpAccept, 1, 2}, op, quest));
}

TEST(bounty_objectives_read_in_plural)
{
    CHECK(Plural("Defias Pillager") == "Defias Pillagers");
    CHECK(Plural("Young Wolf") == "Young Wolves");
    CHECK(Plural("Dark Iron Dwarf") == "Dark Iron Dwarves");
    CHECK(Plural("Harpy") == "Harpies");
    CHECK(Plural("Monkey") == "Monkeys");
    CHECK(Plural("Lynx") == "Lynxes");
    CHECK(Plural("Defias Henchman") == "Defias Henchmen");
    CHECK(Plural("Dragonmaw Shaman") == "Dragonmaw Shamans");
    CHECK(Plural("Kobold Vermin") == "Kobold Vermin");
    CHECK(Plural("Servant of Arugal") == "Servants of Arugal");
    CHECK(Plural("Defias Thief") == "Defias Thieves");
    CHECK(Plural("Chief") == "Chiefs");
}

TEST(bounty_targets_and_postings_keep_to_the_zones_levels)
{
    // A zone's creatures from level 1 to 60: only those in the band (with its slack) are taken,
    // and every day's postings stay among them.
    ZoneBand const band{30, 40};
    ZonePool p;
    p.zone = 33;
    p.zoneName = "Stranglethorn Vale";
    p.band = band;
    for (uint8_t level = 1; level <= 60; ++level)
    {
        CreatureFacts c = Ordinary(1000u + level, level);
        if (IsKillTarget(c, 10, band))
            p.kills.push_back({c.entry, "Creature " + std::to_string(level), LevelOf(c), 10, 1});
    }
    CHECK(p.kills.size() == size_t(40 + kBandSlackAbove - (30 - kBandSlackBelow) + 1));
    for (uint32_t day = 0; day < 60; ++day)
        for (Bounty const& b : Post(p, 14, day))
        {
            CHECK(InBand(b.level, band));
            CHECK(b.minLevel + 4 >= b.level);
            CHECK(IsBountyQuest(b.questId));
        }
    // Two zones on the same day post their own bounties under their own ids.
    ZonePool q = p;
    q.zone = 8;
    std::vector<Bounty> const a = Post(p, 14, 500), c = Post(q, 15, 500);
    CHECK(a.size() == kPerZone && c.size() == kPerZone);
    for (size_t i = 0; i < a.size() && i < c.size(); ++i)
        CHECK(a[i].questId != c[i].questId && a[i].zone != c[i].zone);
}
