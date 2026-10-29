/*
 * Azeroth Warfare: tests for how companions rest in their items (CompanionRest.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/CompanionRest.h"
#include "fusion/CompanionTraits.h"

#include <fstream>
#include <string>

using namespace Fusion::Companions;

TEST(companion_rest_heals_over_a_minute)
{
    CHECK_NEAR(RestedHealth(0.25f, 0), 0.25f, 1e-5f);
    CHECK_NEAR(RestedHealth(0.25f, kRestHealMs / 2), 0.75f, 1e-5f);
    CHECK_NEAR(RestedHealth(0.25f, kRestHealMs * 5), 1.f, 1e-5f);
    CHECK_NEAR(RestedHealth(-1.f, 0), 0.f, 1e-5f);
    CHECK_NEAR(RestedHealth(0.5f, 10, 0), 1.f, 1e-5f);
}

TEST(companion_swapped_back_keeps_its_wounds_and_cooldowns)
{
    RestBook book;
    // A put away hurt at t=1000 with its slot 3 ready at t=31000; B out meanwhile.
    Rest a;
    a.health = 0.2f;
    a.awayMs = 1000;
    a.readyAtMs[3] = 31000;
    book.Put(11, a);
    // Called again 6 s later: 10% healed, the cooldown still running.
    Rest back = book.Take(11, 7000);
    CHECK_NEAR(back.health, 0.3f, 1e-4f);
    CHECK(back.readyAtMs[3] == 31000);
    // One never put away comes whole, with no cooldowns.
    Rest fresh = book.Take(12, 7000);
    CHECK_NEAR(fresh.health, 1.f, 1e-5f);
    CHECK(fresh.readyAtMs[3] == 0);
    // Put again replaces; forgetting drops it.
    a.health = 0.9f;
    book.Put(11, a);
    CHECK(book.Size() == 1);
    CHECK_NEAR(book.Take(11, 1000).health, 0.9f, 1e-4f);
    book.Forget(11);
    CHECK(book.Size() == 0);
    // The clock wrapping round still counts the rest.
    Rest w;
    w.health = 0.f;
    w.awayMs = 0xFFFFFFFFu - 999;
    book.Put(5, w);
    CHECK_NEAR(book.Take(5, kRestHealMs / 2 - 1000).health, 0.5f, 1e-3f);
}

TEST(companion_rest_book_keeps_the_latest_few)
{
    RestBook book;
    for (uint64_t i = 1; i <= RestBook::kMax + 4; ++i)
    {
        Rest r;
        r.health = 0.f;
        r.awayMs = 0;
        book.Put(i, r);
    }
    CHECK(book.Size() == RestBook::kMax);
    // The oldest went first: whole again.
    CHECK_NEAR(book.Take(1, 0).health, 1.f, 1e-5f);
    CHECK_NEAR(book.Take(RestBook::kMax + 4, 0).health, 0.f, 1e-5f);
}

// ── Fainting (CompanionRest.h's Recovery) ───────────────────────────────────────────────────────

TEST(companion_faints_and_stays_fainted_until_the_wait_is_over)
{
    uint32_t const wait = 3600;
    Recovery r;
    CHECK(IsWhole(r) && !IsFainted(r, 1000, wait) && FaintSecondsLeft(r, 1000, wait) == 0);
    Faint(r, 1000);
    CHECK(!IsWhole(r) && IsFainted(r, 1000, wait));
    CHECK(FaintSecondsLeft(r, 1000, wait) == 3600);
    CHECK(FaintSecondsLeft(r, 1000 + 520, wait) == 3080);
    CHECK(HealthAt(r, 1000 + 520, wait) == 0.f);
    // Settling early does nothing.
    CHECK(!Settle(r, 1000 + 3599, wait) && IsFainted(r, 1000 + 3599, wait));
    // A clock that went back keeps the whole wait.
    CHECK(FaintSecondsLeft(r, 10, wait) == 3600);
    // The hour is over: it comes round whole, once.
    CHECK(!IsFainted(r, 1000 + 3600, wait));
    CHECK_NEAR(HealthAt(r, 1000 + 3600, wait), 1.f, 1e-6f);
    CHECK(Settle(r, 1000 + 4000, wait));
    CHECK(IsWhole(r) && r.awayAt == 1000 + 3600);
    CHECK(!Settle(r, 1000 + 5000, wait));
    // Faint at the epoch's first second still counts as fainted.
    Recovery z;
    Faint(z, 0);
    CHECK(z.faintedAt != 0 && IsFainted(z, 0, wait));
}

TEST(companion_dismissed_or_swapped_while_fainted_stays_fainted)
{
    uint32_t const wait = 3600;
    Recovery r;
    Faint(r, 5000);
    // Put away again (Dismiss, its item, a logout, its owner dying with it): still fainted, the
    // wait not restarted, and no health from the putting away.
    PutAway(r, 1.f, 5100);
    CHECK(IsFainted(r, 5100, wait) && r.faintedAt == 5000 && r.health == 0.f);
    CHECK(FaintSecondsLeft(r, 5100, wait) == 3500);
}

TEST(companion_wounds_are_kept_through_a_swap_and_heal_at_rest)
{
    uint32_t const wait = 3600;
    Recovery r;
    PutAway(r, 0.2f, 100);
    CHECK(!IsWhole(r));
    // Called again 6 s later: 10% healed (a minute heals it all), not whole.
    CHECK_NEAR(HealthAt(r, 106, wait), 0.3f, 1e-4f);
    CHECK_NEAR(HealthAt(r, 160, wait), 1.f, 1e-6f);
    // Long away: whole, and no overflow.
    CHECK_NEAR(HealthAt(r, 100 + 4000000, wait), 1.f, 1e-6f);
    // A clock that went back heals nothing.
    CHECK_NEAR(HealthAt(r, 50, wait), 0.2f, 1e-4f);
    PutAway(r, 1.f, 200);
    CHECK(IsWhole(r));
}

TEST(companion_revived_by_each_path)
{
    uint32_t const wait = 3600;
    // A stable master: whole, now.
    Recovery stable;
    Faint(stable, 1000);
    Revive(stable, 1200, 1.f);
    CHECK(!IsFainted(stable, 1200, wait) && IsWhole(stable));
    CHECK_NEAR(HealthAt(stable, 1200, wait), 1.f, 1e-6f);
    // A draught: up at half health, healing from there.
    Recovery draught;
    Faint(draught, 1000);
    Revive(draught, 1200, kDraughtReviveHealth);
    CHECK(!IsFainted(draught, 1200, wait) && !IsWhole(draught));
    CHECK_NEAR(HealthAt(draught, 1200, wait), 0.5f, 1e-6f);
    CHECK_NEAR(HealthAt(draught, 1215, wait), 0.75f, 1e-4f);
    // The wait (Loyal: a third of it, CompanionTraits.h's FaintRecoverSeconds).
    Recovery waited;
    Faint(waited, 1000);
    CHECK(Settle(waited, 1000 + wait, wait) && IsWhole(waited));
    Recovery loyal;
    Faint(loyal, 1000);
    uint32_t const loyalWait = FaintRecoverSeconds(TraitBit(Trait::Loyal), wait);
    CHECK(loyalWait == 1200);
    CHECK(!Settle(loyal, 1000 + 1199, loyalWait) && Settle(loyal, 1000 + 1200, loyalWait));
    // A stable master's fee: by level.
    CHECK(StableReviveCost(60, 10) == 600 && StableReviveCost(0, 10) == 10 && StableReviveCost(20, 0) == 0);
}

TEST(companion_recovery_survives_the_database)
{
    Recovery r;
    Faint(r, 1700000000u);
    Recovery back = FromRow(ToRow(r));
    CHECK(back.faintedAt == r.faintedAt && back.awayAt == r.awayAt && back.health == 0.f);
    CHECK(IsFainted(back, 1700000000u + 60, 3600));
    Recovery hurt;
    PutAway(hurt, 0.437f, 1700000123u);
    RecoveryRow const row = ToRow(hurt);
    CHECK(row.healthPermille == 437 && row.faintedAt == 0 && row.awayAt == 1700000123u);
    CHECK_NEAR(FromRow(row).health, 0.437f, 1e-6f);
    // A row out of range is read as whole, never above.
    RecoveryRow bad;
    bad.healthPermille = 5000;
    CHECK_NEAR(FromRow(bad).health, 1.f, 1e-6f);
    CHECK(ToRow(Recovery{}).healthPermille == 1000);
}

TEST(companion_faint_settings_and_wait_text)
{
    FaintSettings const d = FaintSettingsFrom(int32_t(kFaintRecoverMinutes), int32_t(kStableReviveCostPerLevel));
    CHECK(d.recoverSeconds == 3600 && d.costPerLevel == 10);
    CHECK(FaintSettingsFrom(0, -5).recoverSeconds == 60 && FaintSettingsFrom(0, -5).costPerLevel == 0);
    CHECK(FaintSettingsFrom(1000000, 1000000).recoverSeconds == 7 * 24 * 3600);
    CHECK(FaintSettingsFrom(90, 25).recoverSeconds == 5400 && FaintSettingsFrom(90, 25).costPerLevel == 25);
    CHECK(WaitText(3120) == "52 min");
    CHECK(WaitText(3061) == "52 min");
    CHECK(WaitText(3600) == "1 h");
    CHECK(WaitText(3900) == "1 h 5 min");
    CHECK(WaitText(40) == "40 s" && WaitText(0) == "1 s");
}

TEST(companion_reviving_draught_matches_its_world_sql)
{
    // The item the server hands out and takes (kReviveDraughtItem) is the one the SQL makes, at
    // the price a stable master asks (kReviveDraughtPrice), and the general goods vendors sell it.
    std::ifstream in(FUSION_VECTORS_DIR "/../../../sql/custom/azeroth_warfare/24_companion_revive.sql");
    CHECK(in.good());
    std::string line;
    bool item = false;
    bool sold = false;
    while (std::getline(in, line))
    {
        if (line.rfind("(" + std::to_string(kReviveDraughtItem) + ",", 0) == 0)
        {
            item = line.find("'Reviving Draught'") != std::string::npos
                && line.find(", 1, " + std::to_string(kReviveDraughtPrice) + ", ") != std::string::npos;
        }
        if (line.find("SELECT DISTINCT") != std::string::npos && line.find(std::to_string(kReviveDraughtItem)) != std::string::npos)
            sold = true;
    }
    CHECK(item);
    CHECK(sold);
}
