/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Downed.h"

using namespace Fusion;
using namespace Fusion::Downed;

TEST(downed_bleed_out_grows_with_stamina)
{
    // 20 s, and a second for every 25 Stamina, held to a minute.
    CHECK_NEAR(BleedOutSeconds(0), 20.f, 1e-6);
    CHECK_NEAR(BleedOutSeconds(24), 20.f, 1e-6);
    CHECK_NEAR(BleedOutSeconds(25), 21.f, 1e-6);
    CHECK_NEAR(BleedOutSeconds(250), 30.f, 1e-6);
    CHECK_NEAR(BleedOutSeconds(100000), kBleedMaxSeconds, 1e-6);
    CHECK_NEAR(kBleedBaseSeconds, 20.f, 1e-6);
    CHECK_NEAR(kStaminaPerSecond, 25.f, 1e-6);
}

TEST(downed_a_fatal_hit_goes_down_instead)
{
    // Up: only a hit that would kill goes down, and it leaves 1 health.
    DamageFate f = OnDamage(false, true, 0, 100, 99);
    CHECK(f.fate == Fate::Normal && f.damage == 99);
    f = OnDamage(false, true, 0, 100, 100);
    CHECK(f.fate == Fate::GoDown && f.damage == 99);
    f = OnDamage(false, true, 0, 100, 5000);
    CHECK(f.fate == Fate::GoDown && f.damage == 99);
    // Already at 1 health: down with no damage at all.
    f = OnDamage(false, true, 0, 1, 30);
    CHECK(f.fate == Fate::GoDown && f.damage == 0);
    // Where going down is not allowed (a duel, the fusion off), the hit is the game's.
    f = OnDamage(false, false, 0, 100, 100);
    CHECK(f.fate == Fate::Normal && f.damage == 100);
}

TEST(downed_damage_fills_the_finisher_pool_then_kills)
{
    // Down: hits take nothing from the 1 health but drain the finisher pool; the hit that
    // empties it kills (all the health that is left).
    DamageFate f = OnDamage(true, true, 50, 1, 20);
    CHECK(f.fate == Fate::Absorbed && f.damage == 0 && f.poolLeft == 30);
    f = OnDamage(true, true, 30, 1, 29);
    CHECK(f.fate == Fate::Absorbed && f.poolLeft == 1);
    f = OnDamage(true, true, 1, 1, 1);
    CHECK(f.fate == Fate::Finished && f.damage == 1 && f.poolLeft == 0);
    f = OnDamage(true, true, 0, 1, 1);
    CHECK(f.fate == Fate::Finished);
    // Even where going down is not allowed, a downed player keeps the pool.
    f = OnDamage(true, false, 10, 1, 5);
    CHECK(f.fate == Fate::Absorbed && f.poolLeft == 5);
    // The pool is a tenth of the health, never under 1.
    CHECK(FinisherPool(1000) == 100);
    CHECK(FinisherPool(5) == 1);
    CHECK(FinisherPool(0) == 1);
}

TEST(downed_state_runs_out_or_gets_back_up)
{
    State s;
    CHECK(!s.Active());
    s.GoDown(1000, 2000, 200, false, false);
    CHECK(s.Active() && !s.armed && !s.finalStand);
    CHECK(s.untilMs == 1000 + 28000);   // 20 + 200/25 s
    CHECK(s.finisherLeft == 200);
    CHECK(s.RemainingMs(1000) == 28000);
    CHECK(s.RemainingMs(15000) == 14000);
    CHECK(s.Update(28999) == Tick::None);
    CHECK(s.Update(29000) == Tick::BledOut);
    CHECK(s.RemainingMs(40000) == 0);
    // Across the clock's wrap.
    State w;
    w.GoDown(0xFFFFF000u, 1000, 0, false, false);
    CHECK(w.untilMs == 0xFFFFF000u + 20000u);
    CHECK(w.Update(0xFFFFFFF0u) == Tick::None);
    CHECK(w.Update(0xFFFFF000u + 20000u) == Tick::BledOut);
    CHECK(w.RemainingMs(0xFFFFF000u + 10000u) == 10000);

    // Final Stand gets up after kFinalStandSeconds, well before the bleed-out.
    State fs;
    fs.GoDown(0, 1000, 0, false, true);
    CHECK(fs.finalStand && fs.armed);           // Final Stand fights back too
    CHECK(fs.Update(9999) == Tick::None);
    CHECK(fs.Update(10000) == Tick::GetUp);
    // Last Stand fights back but still bleeds out.
    State ls;
    ls.GoDown(0, 1000, 0, true, false);
    CHECK(ls.armed && !ls.finalStand);
    CHECK(ls.Update(10000) == Tick::None);
    ls.Clear();
    CHECK(!ls.Active() && ls.Update(999999) == Tick::None);
}

TEST(downed_only_last_stand_fires_and_only_a_pistol)
{
    CHECK(!MayFire(false, WeaponClass::Pistol));
    CHECK(MayFire(true, WeaponClass::Pistol));
    CHECK(!MayFire(true, WeaponClass::AssaultRifle));
    CHECK(!MayFire(true, WeaponClass::MachinePistol));
    CHECK(!MayFire(true, WeaponClass::Lethal));
    CHECK(!MayFire(true, WeaponClass::Killstreak));
    CHECK(!MayFire(true, WeaponClass::Melee));
    // The pistol: the class's secondary when it is one, else the USP.
    CHECK(LastStandPistol(33, WeaponClass::Pistol) == 33);
    CHECK(LastStandPistol(34, WeaponClass::MachinePistol) == kDefaultPistol);
    CHECK(LastStandPistol(0, WeaponClass::Pistol) == kDefaultPistol);
    CHECK(kDefaultPistol == 30);
}

TEST(downed_revive_health)
{
    CHECK(ReviveHealth(1000, kSpellReviveShare) == 300);
    CHECK(ReviveHealth(1000, KitInfo(kFieldRevivalKit)->healthShare) == 250);
    CHECK(ReviveHealth(3, 0.1f) == 1);          // never under 1
    CHECK(ReviveHealth(0, 0.5f) == 1);
    CHECK_NEAR(kFinalStandHealthShare, 0.5f, 1e-6);
}

TEST(downed_revival_kits)
{
    Kit const* kit = KitInfo(kFieldRevivalKit);
    Kit const* coil = KitInfo(kRevivalCoil);
    CHECK(kit && coil && !KitInfo(7148) && !KitInfo(0));
    CHECK(IsKitItem(kFieldRevivalKit) && IsKitItem(kRevivalCoil) && !IsKitItem(kFieldMedicHandbook));
    CHECK(kit->skillLine == 129 && coil->skillLine == 202);
    // Slower on yourself.
    CHECK(kit->selfMs > kit->otherMs && coil->selfMs > coil->otherMs);
    CHECK(kit->selfMs >= 6000 && kit->selfMs <= 8000);
    CHECK(coil->selfMs >= 6000 && coil->selfMs <= 8000);
    CHECK(UseMs(*kit, true) == kit->selfMs && UseMs(*kit, false) == kit->otherMs);
    // The crafting manuals and what they make.
    CHECK(ManualProduct(kFieldMedicHandbook) == kFieldRevivalKit);
    CHECK(ManualProduct(kRevivalSchematic) == kRevivalCoil);
    CHECK(ManualProduct(kFieldRevivalKit) == 0);
    CHECK(IsDownedItem(kFieldMedicHandbook) && IsDownedItem(kRevivalCoil) && !IsDownedItem(93050));
}

TEST(downed_who_may_use_a_kit_on_whom)
{
    Kit const& kit = *KitInfo(kFieldRevivalKit);
    UseContext c;
    // Yourself while down.
    c.userDowned = true;
    c.onSelf = true;
    CHECK(CanUseKit(kit, c) == UseCheck::Ok);
    // Up, on yourself: nothing to revive.
    c.userDowned = false;
    CHECK(CanUseKit(kit, c) == UseCheck::TargetNotDowned);
    // On a downed ally in reach.
    c.onSelf = false;
    c.targetDowned = true;
    c.distance = 3.f;
    c.friendly = true;
    CHECK(CanUseKit(kit, c) == UseCheck::Ok);
    c.distance = kKitReach + 0.5f;
    CHECK(CanUseKit(kit, c) == UseCheck::TooFar);
    c.distance = 3.f;
    c.friendly = false;
    CHECK(CanUseKit(kit, c) == UseCheck::NotFriendly);
    c.friendly = true;
    c.targetDowned = false;
    CHECK(CanUseKit(kit, c) == UseCheck::TargetNotDowned);
    // A downed player may only work on themselves.
    c.targetDowned = true;
    c.userDowned = true;
    CHECK(CanUseKit(kit, c) == UseCheck::UserDowned);
    // No skill to use one: the skill is in making it.
    c.userDowned = false;
    CHECK(CanUseKit(kit, c) == UseCheck::Ok);
    CHECK(kit.skillToMake == 150 && KitInfo(kRevivalCoil)->skillToMake == 175);
}

TEST(downed_a_kit_use_breaks_when_the_user_moves)
{
    KitUse u;
    CHECK(!u.Active());
    u.Start(5, 77, 1000, 3000, Vec3{0.f, 0.f, 0.f});
    CHECK(u.Active() && u.item == 5 && u.target == 77);
    CHECK(u.Progress(1000, Vec3{0.f, 0.f, 0.f}) == KitProgress::Working);
    CHECK(u.Progress(2000, Vec3{0.5f, 0.5f, 0.f}) == KitProgress::Working);   // a shuffle
    CHECK(u.Progress(3999, Vec3{0.f, 0.f, 0.f}) == KitProgress::Working);
    CHECK(u.Progress(4000, Vec3{0.f, 0.f, 0.f}) == KitProgress::Done);
    KitUse m;
    m.Start(5, 0, 0, 7000, Vec3{0.f, 0.f, 0.f});
    CHECK(m.Progress(100, Vec3{kKitMoveTolerance + 0.1f, 0.f, 0.f}) == KitProgress::Moved);
    m.Clear();
    CHECK(!m.Active());
}

TEST(downed_creatures_lose_interest)
{
    // A creature on a downed player turns to another target after a moment; with none it
    // keeps at it.
    CHECK(!CreatureLeaves(1000, 0, true));
    CHECK(!CreatureLeaves(1000, 1000 + uint32_t(kCreatureInterestSeconds * 1000.f) - 1, true));
    CHECK(CreatureLeaves(1000, 1000 + uint32_t(kCreatureInterestSeconds * 1000.f), true));
    CHECK(!CreatureLeaves(1000, 99999, false));
}

TEST(downed_wire_round_trips)
{
    DownedMsg d;
    d.flags = DOWNED_ARMED | DOWNED_HAS_KIT;
    d.remainingMs = 27500;
    d.totalMs = 28000;
    d.pistol = 30;
    std::vector<uint8_t> b = EncodeDowned(d);
    CHECK(b == std::vector<uint8_t>({kOpDowned, 0x05, 0x6C, 0x6B, 0, 0, 0x60, 0x6D, 0, 0, 30, 0}));
    DownedMsg back;
    CHECK(DecodeDowned(b, back));
    CHECK(back.flags == d.flags && back.remainingMs == 27500 && back.totalMs == 28000 && back.pistol == 30);
    CHECK(!DecodeDowned({kOpDowned, 1, 2}, back));

    UpMsg u;
    u.reason = UpReason::Kit;
    u.healthPct = 25;
    u.by = 0x0102;
    b = EncodeUp(u);
    CHECK(b == std::vector<uint8_t>({kOpUp, 2, 25, 0x02, 0x01, 0, 0, 0, 0, 0, 0}));
    UpMsg uback;
    CHECK(DecodeUp(b, uback) && uback.reason == UpReason::Kit && uback.healthPct == 25 && uback.by == 0x0102);

    b = EncodeDied(DeathCause::Finished);
    CHECK(b == std::vector<uint8_t>({kOpDied, 2}));
    DeathCause cause;
    CHECK(DecodeDied(b, cause) && cause == DeathCause::Finished);

    KitMsg k;
    k.state = KitState::Using;
    k.selfUse = false;
    k.other = 9;
    k.durationMs = 3000;
    b = EncodeKit(k);
    CHECK(b == std::vector<uint8_t>({kOpKit, 1, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0xB8, 0x0B, 0, 0}));
    KitMsg kback;
    CHECK(DecodeKit(b, kback) && kback.state == KitState::Using && kback.other == 9 && kback.durationMs == 3000);

    b = EncodeNear(0xAB, true);
    CHECK(b == std::vector<uint8_t>({kOpNear, 0xAB, 0, 0, 0, 0, 0, 0, 0, 1}));
    uint64_t who = 0;
    bool down = false;
    CHECK(DecodeNear(b, who, down) && who == 0xAB && down);

    // Client to server: use a kit (or a manual) on a target, 0 for the server's pick.
    UseRequest r;
    r.item = 0x11;
    r.target = 0x22;
    b = EncodeUse(r);
    CHECK(b == std::vector<uint8_t>({kOpUse, 0x11, 0, 0, 0, 0, 0, 0, 0, 0x22, 0, 0, 0, 0, 0, 0, 0}));
    UseRequest rback;
    CHECK(DecodeUse(b, rback) && rback.item == 0x11 && rback.target == 0x22);
    CHECK(!DecodeUse({kOpUse, 1}, rback));
    CHECK(!DecodeUse({kOpDowned}, rback));
    CHECK(kChannelDowned == 5);
}
