/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * The server's hold on reloads (fusion/Reload.h; security review 2026-10, #13).
 */

#include "Test.h"
#include "fusion/Catalog.h"
#include "fusion/Reload.h"
#include "fusion/Stats.h"

using namespace Fusion;

namespace
{
    WeaponProfile Gun(float reload, float empty)
    {
        WeaponProfile w;
        w.id = 2;
        w.reloadTime = reload;
        w.reloadEmptyTime = empty;
        return w;
    }
}

TEST(reload_factor_is_the_clients)
{
    // The client's: reload_scale (Sleight of Hand halves it) times the handling it was sent.
    CHECK_NEAR(ReloadFactor(false, 1.f), 1.f, 1e-6);
    CHECK_NEAR(ReloadFactor(true, 1.f), 0.5f, 1e-6);
    CHECK_NEAR(ReloadFactor(true, 0.9f), 0.45f, 1e-6);
    // Held to what the server can send: never below the output's limit, never above 1.
    float const least = Stats::OutputLimit(Stats::Out::ReloadTime);
    CHECK_NEAR(ReloadFactor(false, 0.f), least, 1e-6);
    CHECK_NEAR(ReloadFactor(false, 3.f), 1.f, 1e-6);
}

TEST(reload_quickest_times)
{
    // The shorter of the two (the magazine counts may differ by a round).
    CHECK_NEAR(QuickestReloadSeconds(Gun(2.2f, 2.8f), 1.f), 2.2f, 1e-5);
    CHECK_NEAR(QuickestReloadSeconds(Gun(3.f, 2.5f), 0.5f), 1.25f, 1e-5);
    // A launcher's underbarrel with no empty time; a gun with no times at all: no limit.
    CHECK_NEAR(QuickestReloadSeconds(Gun(2.6f, 0.f), 1.f), 2.6f, 1e-5);
    CHECK(QuickestReloadSeconds(Gun(0.f, 0.f), 1.f) == 0.f);
    CHECK_NEAR(QuickestShellSeconds(Gun(0.667f, 0.667f), 0.5f), 0.3335f, 1e-5);
}

TEST(reload_asked_right_after_the_last_round_waits)
{
    ReloadPace pace;
    pace.Fired(10000);
    pace.AskFull();
    // 2.2 s reload: nothing lands before 0.7 of it (1540 ms) after the last round...
    CHECK(!pace.Take(10001, 2.2f, 0.f).Any());
    CHECK(!pace.Take(11539, 2.2f, 0.f).Any());
    CHECK(pace.Pending());
    // ...then the magazine fills, once.
    ReloadPace::Due const due = pace.Take(11540, 2.2f, 0.f);
    CHECK(due.full && due.shells == 0);
    CHECK(!pace.Pending());
    CHECK(!pace.Take(20000, 2.2f, 0.f).Any());
}

TEST(reload_honest_client_is_never_held)
{
    // An honest reload (its full time after the last round, Sleight of Hand or not) lands at once.
    for (float factor : {1.f, 0.5f, 0.4f})
    {
        WeaponProfile const w = Gun(2.03f, 2.36f);
        float const secs = QuickestReloadSeconds(w, factor);
        ReloadPace pace;
        pace.Fired(5000);
        pace.AskFull();
        CHECK(pace.Take(5000 + uint32_t(secs * 1000.f), secs, 0.f).full);
        // A shot held up on the way by a quarter of the reload: still on time.
        ReloadPace late;
        late.Fired(5000 + uint32_t(secs * 250.f));
        late.AskFull();
        CHECK(late.Take(5000 + uint32_t(secs * 1000.f), secs, 0.f).full);
    }
}

TEST(reload_firing_on_waits_again)
{
    ReloadPace pace;
    pace.Fired(1000);
    pace.AskFull();
    pace.Fired(2000);       // fired on through it: counts from here
    CHECK(!pace.Take(2000 + 1539, 2.2f, 0.f).Any());
    CHECK(pace.Take(2000 + 1540, 2.2f, 0.f).full);
}

TEST(reload_before_any_shot_is_due)
{
    ReloadPace pace;
    pace.AskFull();
    CHECK(pace.Take(123, 2.2f, 0.f).full);
    pace.AskShell();
    pace.AskShell();
    CHECK(pace.Take(124, 2.2f, 0.5f).shells == 2);
}

TEST(reload_shells_land_one_slot_at_a_time)
{
    // A shell-by-shell reload: the k-th shell after the last round needs k x 0.7 of a shell's time.
    ReloadPace pace;
    pace.Fired(0);
    for (int i = 0; i < 4; ++i)
        pace.AskShell();
    // 0.5 s a shell: 350, 700, 1050, 1400 ms.
    CHECK(pace.Take(349, 0.f, 0.5f).shells == 0);
    CHECK(pace.Take(350, 0.f, 0.5f).shells == 1);
    CHECK(pace.Take(1049, 0.f, 0.5f).shells == 1);
    CHECK(pace.Take(5000, 0.f, 0.5f).shells == 2);
    CHECK(!pace.Pending());
    // Asking for all of them at once from a modified client: the same pace.
    ReloadPace burst;
    burst.Fired(0);
    for (int i = 0; i < 8; ++i)
        burst.AskShell();
    CHECK(burst.Take(10, 0.f, 0.5f).shells == 0);
    CHECK(burst.Take(700, 0.f, 0.5f).shells == 2);
    // A shot resets the count: the next shell waits a whole slot.
    burst.Fired(800);
    CHECK(burst.Take(1100, 0.f, 0.5f).shells == 0);
    CHECK(burst.Take(1150, 0.f, 0.5f).shells == 1);
}

TEST(reload_clock_wrap_and_backwards)
{
    ReloadPace pace;
    pace.Fired(0xFFFFFF00u);
    pace.AskFull();
    CHECK(!pace.Take(0x100u, 2.f, 0.f).Any());               // 512 ms across the wrap
    CHECK(pace.Take(0xFFFFFF00u + 1400u, 2.f, 0.f).full);    // 1.4 s across it
    ReloadPace back;
    back.Fired(5000);
    back.AskFull();
    CHECK(!back.Take(4000, 1.f, 0.f).Any());                 // a clock that went back: none passed
}

TEST(reload_quicker_factor_is_remembered_a_while)
{
    RecentLeast least;
    CHECK_NEAR(least.Update(0.5f, 1000), 0.5f, 1e-6);       // Sleight of Hand
    // Gone (a class change): a reload begun with it still counts it for a while.
    CHECK_NEAR(least.Update(1.f, 2000), 0.5f, 1e-6);
    CHECK_NEAR(least.Update(1.f, 1000 + kReloadFactorMemoryMs - 1), 0.5f, 1e-6);
    CHECK_NEAR(least.Update(1.f, 1000 + kReloadFactorMemoryMs), 1.f, 1e-6);
    // A quicker one counts at once.
    CHECK_NEAR(least.Update(0.4f, 20000), 0.4f, 1e-6);
}

TEST(reload_catalog_takes_the_quickest_definition_held)
{
    // Akimbo pistols reload quicker than one (MW2's own definitions): the server must never hold
    // a reload longer than the client's definition.
    char const* const json = R"({"version": 2,
        "weapons": [{"id": 30, "name": "usp_mp", "class": "pistol", "fire_time": 0.15, "mag_size": 12,
            "reload_time": 1.625, "reload_empty_time": 1.917,
            "attachments": ["akimbo", "silencer", "xmags"],
            "variants": {"akimbo": "usp_akimbo_mp", "silencer": "usp_silencer_mp",
                         "akimbo+silencer": "usp_akimbo_silencer_mp", "xmags": "usp_xmags_mp",
                         "akimbo+xmags": "usp_akimbo_xmags_mp", "silencer+xmags": "usp_silencer_xmags_mp"},
            "variant_stats": {"akimbo": {"reload_time": 1.3, "reload_empty_time": 2.5},
                              "akimbo+silencer": {"reload_time": 1.2},
                              "xmags": {"mag_size": 18, "reload_time": 1.8}}}],
        "attachments": [{"id": "akimbo"}, {"id": "silencer"}, {"id": "xmags"}]})";
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(json, error));
    WeaponProfile const& usp = *c.Weapons().Find(30);
    // Its own: the base's.
    CHECK_NEAR(c.Effective(usp, {}).reloadTime, 1.625f, 1e-5);
    CHECK_NEAR(c.Effective(usp, {"silencer"}).reloadTime, 1.625f, 1e-5);
    // A combination's own, never longer than the base's.
    CHECK_NEAR(c.Effective(usp, {"akimbo"}).reloadTime, 1.3f, 1e-5);
    CHECK_NEAR(c.Effective(usp, {"akimbo"}).reloadEmptyTime, 1.917f, 1e-5);
    CHECK_NEAR(c.Effective(usp, {"xmags"}).reloadTime, 1.625f, 1e-5);
    // Three attachments: the quickest of every definition they could be held as.
    CHECK_NEAR(c.Effective(usp, {"akimbo", "silencer", "xmags"}).reloadTime, 1.2f, 1e-5);
    // Another gun's combination never counts.
    CHECK_NEAR(c.Effective(usp, {"silencer", "xmags"}).reloadTime, 1.625f, 1e-5);
}
