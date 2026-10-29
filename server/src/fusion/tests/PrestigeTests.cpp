/*
 * Azeroth Warfare: tests for prestige (Prestige.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Prestige.h"

#include <fstream>
#include <sstream>

using namespace Fusion;
using namespace Fusion::Prestige;

namespace
{
    Catalog const& PrestigeCatalog()
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

    AchievementBook SmallBook()
    {
        AchievementBook book;
        std::string error;
        auto add = [&](uint32_t id, AchievementCategory c) {
            Achievement a;
            a.id = id;
            a.category = c;
            a.name = "a" + std::to_string(id);
            a.requirements = {{{Stat::PlayerKills, kAnyKey}, id}};
            book.Add(a, error);
        };
        add(1, AchievementCategory::Weapons);
        add(2, AchievementCategory::Attachments);
        add(3, AchievementCategory::Camos);
        add(4, AchievementCategory::Feats);
        add(5, AchievementCategory::Perks);
        return book;
    }

    Standing Maxed()
    {
        Standing s;
        s.level = 60;
        s.gunsMaxed = true;
        s.skills[202] = 300;
        return s;
    }
}

TEST(prestige_guns_maxed_asks_only_weapons_and_attachments)
{
    AchievementBook const book = SmallBook();
    CHECK(!GunsMaxed(book, {1}));
    CHECK(GunsMaxed(book, {1, 2}));
    CHECK(GunsMaxed(book, {1, 2, 4}));
    CHECK(!GunsMaxed(AchievementBook(), {}));   // an empty book is never maxed
}

TEST(prestige_gun_reset_keeps_feats_only)
{
    AchievementBook const book = SmallBook();
    std::set<uint32_t> const kept = KeptAfterGunPrestige(book, {1, 2, 3, 4, 5, 999});
    CHECK(kept.count(4) == 1);
    CHECK(kept.count(999) == 1);                // unknown to the book: left alone
    CHECK(kept.count(1) == 0 && kept.count(2) == 0 && kept.count(3) == 0 && kept.count(5) == 0);
}

TEST(prestige_eligibility_by_track)
{
    State s;
    Standing now;
    now.level = 59;
    now.skills[202] = 299;
    CHECK(CanPrestige(s, now, Track::Level, 0) == Refusal::NotMaxed);
    CHECK(CanPrestige(s, now, Track::Guns, 0) == Refusal::NotMaxed);
    CHECK(CanPrestige(s, now, Track::Profession, 202) == Refusal::NotMaxed);
    CHECK(CanPrestige(s, now, Track::Profession, 171) == Refusal::NotMaxed);   // not known
    CHECK(CanPrestige(s, now, Track::Profession, 9001) == Refusal::NotAProfession);
    now = Maxed();
    CHECK(CanPrestige(s, now, Track::Level, 0) == Refusal::None);
    CHECK(CanPrestige(s, now, Track::Guns, 0) == Refusal::None);
    CHECK(CanPrestige(s, now, Track::Profession, 202) == Refusal::None);
    s.level = kMaxRank;
    CHECK(CanPrestige(s, now, Track::Level, 0) == Refusal::MaxRank);
}

TEST(prestige_apply_ranks_up_and_pays)
{
    State s;
    Reward r = Apply(s, Track::Level, 0);
    CHECK(s.level == 1 && r.tokens == 5 && s.tokens == 5);
    CHECK(TitleText(r.title) == "Private");
    r = Apply(s, Track::Level, 0);
    CHECK(s.level == 2 && r.tracer == Tracer::Red && s.HasTracer(Tracer::Red));
    r = Apply(s, Track::Guns, 0);
    CHECK(s.guns == 1 && r.camo == 6);
    r = Apply(s, Track::Profession, 202);
    CHECK(s.ProfessionRank(202) == 1 && r.tokens == 3);
    CHECK(TitleText(r.title) == "Seasoned Engineer");
    CHECK(s.tokens == 5 + 5 + 5 + 3);
    for (int i = 0; i < 20; ++i)
        Apply(s, Track::Guns, 0);
    CHECK(s.guns == kMaxRank);
}

TEST(prestige_titles_held_and_worn)
{
    State s;
    CHECK(TitlesHeld(s).empty());
    CHECK(HoldsTitle(s, 0));
    s.level = 3;
    s.guns = 1;
    s.professions[171] = 6;
    std::vector<uint16_t> const held = TitlesHeld(s);
    CHECK(HoldsTitle(s, 3) && HoldsTitle(s, 1) && !HoldsTitle(s, 4));
    CHECK(HoldsTitle(s, 11) && !HoldsTitle(s, 12));
    CHECK(Titled("Thrall", 3) == "Sergeant Thrall");
    CHECK(Titled("Thrall", 16) == "Thrall the Gunsmith");
    // Alchemy is index 2: rank 6 is "Master Alchemist", and the lower grades show once each.
    uint16_t const master = uint16_t(100 + 2 * 10 + 5);
    CHECK(TitleText(master) == "Master Alchemist");
    CHECK(Titled("Jaina", master) == "Jaina, Master Alchemist");
    CHECK(std::find(held.begin(), held.end(), master) != held.end());
    CHECK(HoldsTitle(s, uint16_t(100 + 2 * 10 + 0)));    // rank 1's, a grade below
    CHECK(!HoldsTitle(s, uint16_t(100 + 2 * 10 + 6)));   // rank 7 not reached
    CHECK(Titled("Thrall", 999) == "Thrall");
}

TEST(prestige_camos_go_on_every_camo_gun)
{
    Catalog const& catalog = PrestigeCatalog();
    State s;
    s.guns = 3;
    std::vector<uint8_t> const camos = PrestigeCamos(s);
    CHECK(camos.size() == 2 && camos[0] == 6 && camos[1] == 7);
    UnlockSet u = UnlockSet::Starter(catalog);
    GrantPrestigeUnlocks(catalog, s, u);
    WeaponProfile const* m4 = catalog.Weapons().Find(std::string("m4_mp"));
    CHECK(m4 != nullptr);
    if (m4)
    {
        CHECK(u.IsCamoUnlocked(m4->id, 6) && u.IsCamoUnlocked(m4->id, 7));
        CHECK(!u.IsCamoUnlocked(m4->id, 8));
        CHECK(!u.IsGunUnlocked(m4->id));        // the camo waits for the gun
    }
    // A camo bought in the shop counts too.
    CHECK(Buy(s, 30) == BuyResult::NotEnoughTokens);
    s.tokens = 10;
    CHECK(Buy(s, 30) == BuyResult::Ok && s.tokens == 0);
    CHECK(PrestigeCamos(s).size() == 3);
    s.tokens = 50;
    CHECK(Buy(s, 30) == BuyResult::AlreadyOwned && s.tokens == 50);
}

TEST(prestige_milestones_need_everything_maxed_together)
{
    State s;
    Standing now = Maxed();
    std::vector<uint8_t> due = MilestonesDue(s, now);
    CHECK(due.size() == 1 && due[0] == 0);      // the first, with no prestige at all
    now.gunsMaxed = false;
    CHECK(MilestonesDue(s, now).empty());
    now = Maxed();
    now.skills[202] = 299;
    CHECK(MilestonesDue(s, now).empty());
    now = Maxed();
    CHECK(ClaimMilestone(s, 0) && !ClaimMilestone(s, 0));
    CHECK(s.tokens == 10);
    s.level = 1;
    s.guns = 1;
    CHECK(MilestonesDue(s, now).empty());       // no profession prestiged yet
    s.professions[202] = 1;
    due = MilestonesDue(s, now);
    CHECK(due.size() == 1 && due[0] == 1);
    CHECK(ClaimMilestone(s, 1) && s.HasTracer(Tracer::White));
    CHECK(HoldsTitle(s, 201) && TitleText(201) == "the Unbroken");
    s.level = s.guns = 5;
    s.professions[202] = 5;
    CHECK(MilestonesDue(s, now).size() == 2);
}

TEST(prestige_kill_xp_helps_the_second_climb)
{
    CHECK_NEAR(KillXpMultiplier(0, 10), 1.0, 1e-6);
    CHECK_NEAR(KillXpMultiplier(1, 10), 1.5, 1e-6);
    CHECK_NEAR(KillXpMultiplier(10, 10), 3.0, 1e-6);
    CHECK_NEAR(KillXpMultiplier(4, 60), 1.0, 1e-6);
}

TEST(prestige_confirm_is_two_steps_and_expires)
{
    Pending p;
    Track t;
    uint16_t key;
    CHECK(!Confirm(p, 5, 0, t, key));
    Ask(p, Track::Profession, 202, 77, 1000);
    CHECK(!Confirm(p, 78, 1000, t, key));       // the wrong nonce
    CHECK(Confirm(p, 77, 1000 + kConfirmMs, t, key) && t == Track::Profession && key == 202);
    CHECK(!Confirm(p, 77, 1000, t, key));       // once only
    Ask(p, Track::Level, 0, 9, 1000);
    CHECK(!Confirm(p, 9, 1001 + kConfirmMs, t, key));
    CHECK(!WhatItCosts(Track::Guns, 0).empty());
}

TEST(prestige_wire_round_trips_client_messages)
{
    ClientMessage m;
    m.op = Op::Ask;
    m.track = Track::Profession;
    m.key = 171;
    ClientMessage back;
    CHECK(DecodeClient(EncodeClient(m), back) && back.op == Op::Ask && back.track == Track::Profession && back.key == 171);
    m = ClientMessage();
    m.op = Op::Confirm;
    m.nonce = 0xDEADBEEF;
    CHECK(DecodeClient(EncodeClient(m), back) && back.nonce == 0xDEADBEEF);
    m = ClientMessage();
    m.op = Op::SetTracer;
    m.tracer = Tracer::Gold;
    CHECK(DecodeClient(EncodeClient(m), back) && back.tracer == Tracer::Gold);
    CHECK(!DecodeClient({kVersion, uint8_t(Op::Ask), 9, 0, 0}, back));   // no track 9
    CHECK(!DecodeClient({kVersion, uint8_t(Op::Confirm), 1}, back));     // short
    CHECK(!DecodeClient({2, 1}, back));                                  // another version
}

TEST(the_token_shop_is_on_the_prestige_tab_not_an_npc)
{
    // Buying is a tab click (no Prestige Quartermaster to stand by).
    ClientMessage m;
    m.op = Op::Buy;
    m.good = 20;
    ClientMessage back;
    CHECK(DecodeClient(EncodeClient(m), back) && back.op == Op::Buy && back.good == 20);
    CHECK(!DecodeClient({kVersion, uint8_t(Op::Buy)}, back));            // no good named
    // The list: every good, its price, and whether this state owns it.
    State s;
    s.cosmetics |= 1u << uint8_t(Tracer::Red);
    std::vector<uint8_t> const b = EncodeShop(s);
    CHECK(b.size() > 3 && b[0] == kVersion && b[1] == uint8_t(Op::ShopList) && b[2] == Shop().size());
    // The first good: id, price u32, owned, name.
    Good const& first = Shop()[0];
    CHECK(b[3] == first.id && b[4] == first.price && b[8] == 0 && b[9] == std::string(first.name).size());
    Good const* red = FindGood(20);
    CHECK(red && Owns(s, *red));
    CHECK(BuyText(BuyResult::AlreadyOwned, *red, 3).find("already own") != std::string::npos);
    CHECK(BuyText(BuyResult::NotEnoughTokens, *red, 1).find("you have 1") != std::string::npos);
}

TEST(prestige_state_encodes_what_the_tab_shows)
{
    View v;
    v.state.level = 2;
    v.state.tokens = 300;
    v.state.title = 2;
    v.standing = Maxed();
    std::vector<uint8_t> const b = EncodeState(v);
    CHECK(b.size() > 30);
    CHECK(b[0] == kVersion && b[1] == uint8_t(Op::StateMsg));
    CHECK(b[2] == 2 && b[3] == 0);
    CHECK(b[4] == 44 && b[5] == 1);             // 300 tokens, little-endian
    // level 60, maxed, not busy, then both tracks ready.
    size_t const at = 2 + 1 + 1 + 4 + 4 + 4 + 2 + 1;
    CHECK(b[at] == 60 && b[at + 1] == 1 && b[at + 2] == 0 && b[at + 3] == 0 && b[at + 4] == 0);
    v.busy = true;
    std::vector<uint8_t> const busy = EncodeState(v);
    CHECK(busy[at + 3] == uint8_t(Refusal::Busy));
}
