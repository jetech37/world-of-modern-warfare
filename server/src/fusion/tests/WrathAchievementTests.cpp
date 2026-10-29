/*
 * World of Modern Warfare: the Wrath-style achievements (fusion/Achievements.h): categories,
 * points, meta-achievements, the new criteria and their wire. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Achievements.h"
#include "fusion/Crafts.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>

using namespace Fusion;

namespace
{
    Achievement Make(uint32_t id, std::vector<Requirement> reqs, uint16_t points, AchievementCategory c = AchievementCategory::General)
    {
        Achievement a;
        a.id = id;
        a.category = c;
        a.name = "A" + std::to_string(id);
        a.description = "D.";
        a.requirements = std::move(reqs);
        a.points = points;
        return a;
    }

    Requirement Req(Stat s, uint32_t needed, uint32_t key = kAnyKey) { return {{s, key}, needed}; }

    // The real catalog (contrib/fusion/catalog.json), as AchievementTests reads it.
    Catalog const& RealCatalog()
    {
        static Catalog const catalog = [] {
            std::string path = FUSION_SEED_WEAPONS;
            path.replace(path.rfind("weapons.json"), 12, "catalog.json");
            std::ifstream in(path);
            std::stringstream ss;
            ss << in.rdbuf();
            Catalog c;
            std::string error;
            c.LoadJson(ss.str(), error);
            return c;
        }();
        return catalog;
    }
}

TEST(a_meta_achievement_follows_the_ones_it_asks_for_in_one_pass)
{
    AchievementBook book;
    std::string e;
    // The meta first in the book: it must still be earned in the same evaluation.
    CHECK(book.Add(Make(3, {Req(Stat::Achievement, 1, 1), Req(Stat::Achievement, 1, 2)}, 50), e));
    CHECK(book.Add(Make(1, {Req(Stat::QuestsCompleted, 5)}, 10), e));
    CHECK(book.Add(Make(2, {Req(Stat::ZonesExplored, 3)}, 10), e));
    CHECK(book.Add(Make(4, {Req(Stat::AchievementPoints, 70)}, 0), e));
    AccountStats s;
    s.Add({Stat::QuestsCompleted, kAnyKey}, 5);
    std::vector<uint32_t> got = Evaluate(book, s, {});
    CHECK(got == std::vector<uint32_t>({1}));
    s.Add({Stat::ZonesExplored, kAnyKey}, 3);
    got = Evaluate(book, s, {1});
    // 2, then the meta (3), then the points tier (10 + 10 + 50 = 70).
    CHECK(got == std::vector<uint32_t>({2, 3, 4}));
    CHECK(PointsOf(book, {1, 2, 3, 4}) == 70);
    CHECK(Evaluate(book, s, {1, 2, 3, 4}).empty());
}

TEST(meta_and_points_progress_reads_from_the_earned_set)
{
    AchievementBook book;
    std::string e;
    book.Add(Make(1, {Req(Stat::QuestsCompleted, 1)}, 10), e);
    book.Add(Make(2, {Req(Stat::QuestsCompleted, 2)}, 10), e);
    book.Add(Make(9, {Req(Stat::Achievement, 1, 1), Req(Stat::Achievement, 1, 2), Req(Stat::AchievementPoints, 25)}, 25), e);
    AccountStats s;
    std::set<uint32_t> const earned = {1};
    AchievementProgress const p = ProgressOf(book, *book.Find(9), s, earned);
    CHECK(!p.earned && p.requirements.size() == 3);
    CHECK(p.requirements[0].current == 1 && p.requirements[1].current == 0 && p.requirements[2].current == 10);
    // Earning 2 touches the meta: ChangedByEarning names it and the points.
    std::vector<StatKey> const changed = AchievementWire::ChangedByEarning({2});
    CHECK(changed.size() == 2 && changed[0] == (StatKey{Stat::Achievement, 2}));
    std::vector<AchievementWire::ProgressEntry> const t = AchievementWire::Touched(book, s, {1, 2}, changed);
    CHECK(t.size() == 1 && t[0].id == 9 && t[0].current == std::vector<uint32_t>({1, 1, 20}));
    CHECK(AchievementWire::ChangedByEarning({}).empty());
    // The derived stats are never counted.
    CHECK(IsDerived(Stat::Achievement) && IsDerived(Stat::AchievementPoints) && !IsDerived(Stat::QuestsCompleted));
}

TEST(points_go_on_the_wire_with_version_two)
{
    AchievementBook book;
    std::string e;
    book.Add(Make(AchievementId(AchievementCategory::Dungeons, 1), {Req(Stat::DungeonBossKills, 1, 639)}, 10,
        AchievementCategory::Dungeons), e);
    std::vector<AchievementWire::View> const v = AchievementWire::Views(book, AccountStats(), {});
    CHECK(v.size() == 1 && v[0].points == 10 && v[0].category == AchievementCategory::Dungeons);
    std::vector<std::vector<uint8_t>> const pages = AchievementWire::EncodePages(v);
    CHECK(pages.size() == 1 && pages[0][0] == 2 && AchievementWire::kVersion == 2);
    AchievementWire::Page p;
    CHECK(AchievementWire::DecodePage(pages[0], p) && p.views.size() == 1 && p.views[0] == v[0]);
    CHECK(p.views[0].points == 10);
}

TEST(the_default_book_has_every_wrath_category_with_points_and_titles)
{
    AchievementBook const book = DefaultAchievementBook(RealCatalog());
    std::set<AchievementCategory> seen;
    std::set<uint32_t> titles;
    for (Achievement const& a : book.All())
    {
        seen.insert(a.category);
        CHECK(uint32_t(a.category) == a.id >> 24);
        if (a.category == AchievementCategory::FeatsOfStrength)
            CHECK(a.points == 0);
        else if (!IsUnlockCategory(a.category))
            CHECK(a.points >= 10);
        for (Unlock const& u : a.rewards)
            if (u.kind == UnlockKind::Title)
            {
                CHECK(!TitleName(u.id).empty());
                CHECK(titles.insert(u.id).second);  // each title from one achievement
            }
        for (Requirement const& r : a.requirements)
        {
            // A meta asks only for achievements in the book.
            if (r.key.stat == Stat::Achievement)
                CHECK(book.Find(r.key.key) != nullptr && r.key.key != a.id);
            CHECK(DescribeRequirement(r, RealCatalog()).find("Do something") == std::string::npos);
        }
        CHECK(a.description.size() > 5 && a.description.back() == '.');
    }
    for (AchievementCategory c : {AchievementCategory::General, AchievementCategory::Quests, AchievementCategory::Exploration,
             AchievementCategory::PvP, AchievementCategory::Dungeons, AchievementCategory::Professions,
             AchievementCategory::Reputation, AchievementCategory::WorldEvents, AchievementCategory::Combat,
             AchievementCategory::Companions, AchievementCategory::World, AchievementCategory::FeatsOfStrength})
        CHECK(seen.count(c));
    CHECK(TitleName(29) == "Jack of All Trades" && TitleName(16) == "the Scribe");

    // Every last boss has its achievement, and Dungeon Master asks for each dungeon's.
    for (DungeonBoss const& b : DungeonBosses())
    {
        bool found = false;
        for (Achievement const& a : book.All())
            for (Requirement const& r : a.requirements)
                found = found || (r.key.stat == Stat::DungeonBossKills && r.key.key == b.entry);
        CHECK(found);
    }
    Achievement const* dm = book.Find(AchievementId(AchievementCategory::Dungeons, 60));
    CHECK(dm && dm->requirements.size() == 19 && dm->rewards.size() == 1);
    CHECK(IsDungeonBoss(639) && !IsDungeonBoss(1));

    // The layer's professions: Artisan of each gives its title.
    auto artisan = [&](uint32_t line) -> Achievement const* {
        for (Achievement const& a : book.All())
            if (a.requirements.size() == 1 && a.requirements[0].key == (StatKey{Stat::ProfessionSkill, line})
                && a.requirements[0].needed == 300)
                return &a;
        return nullptr;
    };
    for (uint32_t line : {9001u, 9002u, 9003u, 9004u})
    {
        Achievement const* a = artisan(line);
        CHECK(a && a->rewards.size() == 1 && a->rewards[0].kind == UnlockKind::Title);
    }
    CHECK(artisan(171) && artisan(171)->rewards.empty());
    Achievement const* insc = artisan(Crafts::kSkillInscription);
    CHECK(insc && insc->name == "Artisan Inscription" && insc->description == "Reach 300 skill in Inscription.");
}

TEST(every_wrath_achievement_is_earned_by_its_counters_and_its_metas_follow)
{
    AchievementBook const book = DefaultAchievementBook(RealCatalog());
    AccountStats s;
    for (Achievement const& a : book.All())
        for (Requirement const& r : a.requirements)
            if (!IsDerived(r.key.stat))
                s.Raise(r.key, r.needed);
    std::vector<uint32_t> const got = Evaluate(book, s, {});
    // Everything, metas and points tiers included (the camos alone pass 2000 points).
    std::set<uint32_t> const all(got.begin(), got.end());
    for (Achievement const& a : book.All())
        CHECK(all.count(a.id));
    CHECK(got.size() == book.Size());
    // The Loremaster comes after its zones, in the order it was earned.
    uint32_t const lore = AchievementId(AchievementCategory::Quests, 40);
    uint32_t const stv = AchievementId(AchievementCategory::Quests, 28);
    CHECK(std::find(got.begin(), got.end(), lore) > std::find(got.begin(), got.end(), stv));
}

TEST(the_new_criteria_are_kept_as_maximums_where_they_are_levels)
{
    AccountStats s;
    s.Record({Stat::ProfessionSkill, 9003}, 120);
    s.Record({Stat::ProfessionSkill, 9003}, 80);
    CHECK(s.Get(Stat::ProfessionSkill, 9003) == 120);
    s.Record({Stat::ExaltedFactions, kAnyKey}, 3);
    s.Record({Stat::ExaltedFactions, kAnyKey}, 2);
    CHECK(s.Get(Stat::ExaltedFactions) == 3);
    s.Record({Stat::ItemsCrafted, kAnyKey}, 2);
    s.Record({Stat::ItemsCrafted, kAnyKey}, 3);
    CHECK(s.Get(Stat::ItemsCrafted) == 5);
    CHECK(std::string(StatName(Stat::ZoneQuests)) == "zone_quests");
    CHECK(std::string(StatName(Stat::FullyGlyphedKits)) == "fully_glyphed_kits");
    CHECK(DescribeRequirement({{Stat::FactionRank, 576}, 7}, RealCatalog()) == "Reach Exalted with Timbermaw Hold");
    CHECK(DescribeRequirement({{Stat::DungeonBossKills, 639}, 1}, RealCatalog()) == "Defeat Edwin VanCleef in the Deadmines");
}

TEST(skyriding_and_ships_count_toward_achievements)
{
    AccountStats s;
    s.Record({Stat::SkyLaunches, kAnyKey}, 1);
    s.Record({Stat::SkyLaunches, kAnyKey}, 1);
    CHECK(s.Get(Stat::SkyLaunches) == 2);
    CHECK(!IsMaximum(Stat::ShipsCommissioned));
    CHECK(std::string(StatName(Stat::SkyLaunches)) == "sky_launches");
    CHECK(std::string(StatName(Stat::ShipsCommissioned)) == "ships_commissioned");
    CHECK(DescribeRequirement({{Stat::ShipsCommissioned, kAnyKey}, 1}, RealCatalog()) == "Commission 1 ships");
}
