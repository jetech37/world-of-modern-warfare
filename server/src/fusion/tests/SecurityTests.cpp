/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * Regression tests for the exploit review of 2026-10 (design/SECURITY_REVIEW_2026-10.md): what a
 * modified client sends is held to what the player could do.
 */

#include "Test.h"
#include "fusion/Bounties.h"
#include "fusion/Gathering.h"
#include "fusion/HitValidation.h"
#include "fusion/Locks.h"
#include "fusion/Packets.h"
#include "fusion/Prestige.h"
#include "fusion/Throttle.h"
#include "fusion/Achievements.h"
#include "fusion/Build.h"
#include "fusion/Classes.h"
#include "fusion/Construction.h"
#include "fusion/Homestead.h"
#include "fusion/Match.h"
#include "fusion/Portals.h"
#include "fusion/Clock.h"
#include "fusion/Companions.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

using namespace Fusion;

TEST(security_a_shot_starts_at_the_shooters_eye)
{
    Vec3 const eye{100.f, 200.f, 30.f};
    // Near the eye (the client's camera and muzzle): kept as sent.
    Vec3 const near{101.f, 201.f, 30.5f};
    Vec3 const kept = ShotOrigin(near, eye, kOriginTolerance);
    CHECK(kept.x == near.x && kept.y == near.y && kept.z == near.z);
    // Beside a target far away, or off the map (the server's height lookups assert on it): the eye.
    for (Vec3 const far : {Vec3{140.f, 200.f, 30.f}, Vec3{20000.f, 0.f, 0.f}, Vec3{100.f, 200.f, 34.f}})
    {
        Vec3 const o = ShotOrigin(far, eye, kOriginTolerance);
        CHECK(o.x == eye.x && o.y == eye.y && o.z == eye.z);
    }
    // Not a number, nor infinite.
    float const nan = std::numeric_limits<float>::quiet_NaN();
    float const inf = std::numeric_limits<float>::infinity();
    for (Vec3 const bad : {Vec3{nan, 200.f, 30.f}, Vec3{100.f, inf, 30.f}, Vec3{100.f, 200.f, -inf}})
    {
        Vec3 const o = ShotOrigin(bad, eye, kOriginTolerance);
        CHECK(o.x == eye.x && o.y == eye.y && o.z == eye.z);
    }
    // A rider has more slack, as the hit check gives it.
    Vec3 const riderClaim{104.f, 200.f, 30.f};
    CHECK(ShotOrigin(riderClaim, eye, 5.f).x == 104.f);
    CHECK(ShotOrigin(riderClaim, eye, 3.f).x == eye.x);
}

TEST(security_only_a_gun_in_hand_fires)
{
    uint16_t const primary = 10, secondary = 20, pistol = 30;
    CHECK(GunInHand(primary, primary, secondary, false, 0));
    CHECK(GunInHand(secondary, primary, secondary, false, 0));
    // Another unlocked gun: not in hand, so no fresh magazine of its own to fire.
    CHECK(!GunInHand(99, primary, secondary, false, 0));
    // Picked up off the ground, or Last Stand's pistol while down.
    CHECK(GunInHand(99, primary, secondary, true, 0));
    CHECK(GunInHand(pistol, primary, secondary, false, pistol));
    CHECK(!GunInHand(pistol, primary, secondary, false, 0));
    CHECK(!GunInHand(0, 0, 0, false, 0));
}

TEST(security_shots_saved_up_dont_all_go_off_at_once)
{
    uint32_t const now = 50000;
    CHECK(ShotFresh(now, now));
    CHECK(ShotFresh(now - 900, now));
    CHECK(ShotFresh(now - PoseHistory::kWindowMs, now));
    CHECK(!ShotFresh(now - PoseHistory::kWindowMs - 1, now));
    CHECK(!ShotFresh(now - 10000, now));
    // Across the clock's wrap.
    CHECK(ShotFresh(0xFFFFFF00u, 0x100u));
}

TEST(security_changing_tools_keeps_the_swing_wait)
{
    using namespace Fusion::Gathering;
    uint32_t const pick = uint32_t(Ability::MiningPick);
    uint32_t const knife = uint32_t(Ability::SkinningKnife);
    ToolDef const* p = ToolInfo(pick);
    CHECK(p && p->strikeMs > 0);
    if (!p)
        return;
    Progress progress;
    uint32_t now = 10000;
    CHECK(Strike(progress, pick, 7, 5, now) == Step::Counted);
    // Readying another tool and back between strikes once cleared the wait: every strike counted.
    ToolChanged(progress);
    CHECK(progress.done == 0);
    CHECK(Strike(progress, knife, 7, 5, now + 10) == Step::TooSoon);
    ToolChanged(progress);
    CHECK(Strike(progress, pick, 7, 5, now + 20) == Step::TooSoon);
    // After the swing, a strike counts again.
    CHECK(Strike(progress, pick, 7, 5, now + p->strikeMs) == Step::Counted);
}

TEST(security_costly_asks_are_throttled_and_gunplay_is_not)
{
    using Throttle::Costly;
    // The Construction window, reinforcing, the achievements book, prestige, the class page.
    CHECK(Costly(Packets::kChannelWorld, {Homestead::kOpAsk}));
    CHECK(Costly(Packets::kChannelWorld, Homestead::EncodeAct(Homestead::Action::None, 0)));
    CHECK(Costly(Packets::kChannelWorld, {Construction::kOpReinforce, 1, 0, 0, 0}));
    CHECK(Costly(Packets::kChannelAchievements, {1, 1}));
    CHECK(Costly(Prestige::kChannelPrestige, {Prestige::kVersion, 1}));
    CHECK(Costly(Classes::kChannelClasses, {Classes::kOpAskState}));
    CHECK(Costly(Bounty::kChannelBounty, Bounty::EncodeAsk()));
    CHECK(Costly(Bounty::kChannelBounty, Bounty::EncodeTurnIn(95000)));
    // Locks, portals' menus, doors and taking pieces down.
    CHECK(Costly(Packets::kChannelBuild, {Locks::kOpSetLock}));
    CHECK(Costly(Packets::kChannelBuild, {Portals::kOpAsk}));
    CHECK(Costly(Packets::kChannelBuild, {uint8_t(Build::ClientOp::Toggle)}));
    CHECK(Costly(Packets::kChannelBuild, {uint8_t(Build::ClientOp::Remove)}));
    // Not: the scoreboard (Tab held), placing (its own pace), a code tried (its own limiter),
    // spell aim, anything empty.
    CHECK(!Costly(Prestige::kChannelPrestige, {Prestige::kVersion, Match::kOpScoreboard}));
    CHECK(!Costly(Packets::kChannelBuild, {uint8_t(Build::ClientOp::Place)}));
    CHECK(!Costly(Packets::kChannelBuild, {Locks::kOpTryCode}));
    CHECK(!Costly(Classes::kChannelClasses, {uint8_t(Classes::kOpAskState + 1)}));
    CHECK(!Costly(Packets::kChannelWorld, {}));

    // The allowance: a burst, then five a second.
    RateLimit limit{Throttle::kCostlyBurst, Throttle::kCostlyRefillMs};
    uint32_t allowed = 0;
    for (uint32_t i = 0; i < 1000; ++i)
        allowed += limit.Allow(1000) ? 1 : 0;
    CHECK(allowed == Throttle::kCostlyBurst);
    allowed = 0;
    for (uint32_t ms = 1000; ms < 11000; ms += 10)
        allowed += limit.Allow(ms) ? 1 : 0;
    CHECK(allowed >= 45 && allowed <= 51);
}

TEST(security_explosives_are_held_closer_to_their_fire_rate)
{
    // #20: a sustained 1.25x for bullets (jitter's room), 1.11x for anything with a splash.
    WeaponProfile rifle;
    rifle.fireTime = 0.075f;
    CHECK_NEAR(FireRateSlack(rifle), 0.8f, 1e-6);
    WeaponProfile rpg;
    rpg.weaponClass = WeaponClass::Launcher;
    rpg.explosionRadius = 4.f;
    CHECK_NEAR(FireRateSlack(rpg), 0.9f, 1e-6);
    WeaponProfile gl = rpg;
    gl.slot = WeaponSlot::Alternate;
    CHECK_NEAR(FireRateSlack(gl), 0.9f, 1e-6);
}

TEST(security_pieces_built_free_pay_nothing_back)
{
    // #14: refund, salvage and plot packing come only from what was paid.
    using namespace Fusion::Construction;
    Build::Piece wall;
    wall.type = Build::PieceType::WoodWall;
    wall.health = Build::MaxHealth(wall);
    CHECK(wall.paid == Build::kPaidAll);                    // pieces from before: paid in full
    CHECK(!Refund(wall).empty());
    CHECK(!Salvage(wall.type, 0, wall.paid).empty());
    // Built free: nothing at all, not even the one main material salvage always gives.
    wall.paid = uint8_t(Build::kPaidAll & ~Build::kPaidPiece);
    CHECK(Refund(wall).empty());
    CHECK(Salvage(wall.type, 0, wall.paid).empty());
    // Paid for, then a tier put on free: the piece's share comes back, never the tier's.
    wall.paid = uint8_t(Build::kPaidAll & ~Build::PaidTier(2));
    wall.reinforcement = 2;
    wall.health = Build::MaxHealth(wall);
    auto has = [](std::vector<Material> const& list, uint32_t item) {
        for (Material const& m : list)
            if (m.item == item && m.count > 0)
                return true;
        return false;
    };
    std::vector<Material> const back = Refund(wall);
    CHECK(has(back, kRoughLumber) && has(back, kIronBracket));
    CHECK(!has(back, kReinforcedPlate));
    std::vector<Material> const broken = Salvage(wall.type, 2, wall.paid);
    CHECK(has(broken, kRoughLumber) && has(broken, kIronBracket) && !has(broken, kReinforcedPlate));
    // The old calls are the paid-in-full ones.
    CHECK(ReinforcementSpent(2).size() == ReinforcementSpent(2, Build::kPaidAll).size());
    CHECK(ReinforcementSpent(2, 0).empty());
}

TEST(security_no_companion_goods_from_gray_kills_or_summons)
{
    // #21: nothing from a creature gray to its killer or from a summon or pet; critters, always
    // gray, drop their own small companions at their full rate (0.5%).
    using namespace Fusion::Companions;
    auto const& all = StarterSpecies();
    DropVictim broodling{7047, CreatureType::Dragonkin, 0, CreatureRank::Normal};
    CHECK(DropsCompanionGoods(broodling));
    CHECK(RollDrop(all, broodling, 0.f, 0.5f) == 1);
    DropVictim gray = broodling;
    gray.grayToKiller = true;
    CHECK(!DropsCompanionGoods(gray));
    CHECK(RollDrop(all, gray, 0.f, 0.5f) == 0);
    DropVictim summon = broodling;
    summon.summoned = true;
    CHECK(RollDrop(all, summon, 0.f, 0.5f) == 0);
    // Catalysts: the same.
    DropVictim dragon{999990, CreatureType::Dragonkin, 0, CreatureRank::RareElite};
    CHECK(CatalystDropChance(dragon, 50) > 0.f);
    dragon.grayToKiller = true;
    CHECK(CatalystDropChance(dragon, 50) == 0.f);
    dragon.grayToKiller = false;
    dragon.summoned = true;
    CHECK(CatalystDropChance(dragon, 50) == 0.f);
    // A rat is always gray; it still drops the alley cat, at the critters' full 0.5%.
    DropVictim rat{2110, CreatureType::Critter, 0, CreatureRank::Normal, true, false};
    CHECK(DropsCompanionGoods(rat));
    CHECK(RollDrop(all, rat, 0.0049f, 0.f) == 26);
    CHECK(RollDrop(all, rat, 0.0051f, 0.f) == 0);
    CHECK(CatalystDropChance(rat, 60) == 0.f);      // no critter catalyst exists (CatalystFor)
    // A summoned critter (a pet rat): nothing.
    rat.summoned = true;
    CHECK(RollDrop(all, rat, 0.f, 0.f) == 0);
}

TEST(security_the_first_prestige_milestone_pays_once_an_account)
{
    // #22: the Triple Threat (100g, no prestige needed) was claimed again on every alt.
    using namespace Fusion::Prestige;
    Standing now;
    now.level = 60;
    now.gunsMaxed = true;
    now.skills[202] = 300;
    State first;
    CHECK(MilestonesDue(first, now).size() == 1);
    CHECK(ClaimMilestone(first, 0));
    CHECK((first.accountMilestones & kAccountMilestones) == kAccountMilestones);
    // An alt of the same account: the account's claim is loaded with it.
    State alt;
    alt.accountMilestones = first.accountMilestones;
    CHECK(MilestonesDue(alt, now).empty());
    CHECK(!ClaimMilestone(alt, 0));
    CHECK(alt.tokens == 0 && alt.milestones == 0);
    // The later milestones are each character's own (they need its prestige ranks).
    alt.level = alt.guns = 1;
    alt.professions[202] = 1;
    std::vector<uint8_t> const due = MilestonesDue(alt, now);
    CHECK(due.size() == 1 && due[0] == 1);
    CHECK(ClaimMilestone(alt, 1));
    CHECK((alt.accountMilestones & ~kAccountMilestones) == 0);
}

TEST(security_handed_out_goods_sell_for_nothing)
{
    // #23: every token-shop item is priced at 0 by the world SQL, and salvaged materials are
    // written with no price.
    std::string const dir = FUSION_VECTORS_DIR "/../../../sql/custom/azeroth_warfare/";
    std::stringstream goods;
    goods << std::ifstream(dir + "26_handed_out_goods.sql").rdbuf();
    std::string const sql = goods.str();
    CHECK(sql.find("SET `sell_price` = 0 WHERE `entry` IN (") != std::string::npos);
    for (Prestige::Good const& g : Prestige::Shop())
        if (g.kind == Prestige::GoodKind::Item)
        {
            bool const listed = sql.find("  " + std::to_string(g.item)) != std::string::npos;
            CHECK(listed);
            if (!listed)
                std::printf("    token-shop item %u sells: add it to 26_handed_out_goods.sql\n", g.item);
        }
    std::stringstream salvage;
    salvage << std::ifstream(dir + "24_construction_rules.sql").rdbuf();
    std::string const rules = salvage.str();
    CHECK(!rules.empty());
    CHECK(rules.find("t.`sell_price`") == std::string::npos);
}

TEST(security_companion_duel_experience_is_capped_per_account)
{
    // #30: the cap counted per pair of characters, so alts multiplied it.
    using namespace Fusion::Companions;
    DuelWinsToday account;
    uint32_t const day = 20000;
    for (uint32_t i = 0; i < kDuelWinsPerDay; ++i)
        CHECK(PayDuelWin(account, day));
    // Whoever the next foe is, and whichever of the account's characters wins: nothing.
    CHECK(!PayDuelWin(account, day));
    CHECK(account.wins == kDuelWinsPerDay);
    // The next day starts over.
    CHECK(PayDuelWin(account, day + 1));
    CHECK(account.day == day + 1 && account.wins == 1);
}
