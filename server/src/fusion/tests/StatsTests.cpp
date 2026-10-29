/*
 * Azeroth Warfare: tests for what stats do with guns (Stats.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Packets.h"
#include "fusion/Scale.h"
#include "fusion/Stats.h"

using namespace Fusion;
using namespace Fusion::Stats;

namespace
{
    // A level's character with `share` budgets of every stat from gear (and totals to match).
    Input Geared(uint32_t level, float share)
    {
        Input in;
        in.level = level;
        float const b = StatBudget(level) * share;
        in.strengthBonus = in.agilityBonus = in.staminaBonus = in.intellectBonus = in.spiritBonus = b;
        in.strength = in.agility = in.stamina = in.intellect = in.spirit = 20.f + 1.5f * float(level) + b;
        in.rangedPowerBonus = RangedPowerBudget(level) * share;
        in.hitPct = kHitBudgetPct * share;
        in.defenseBonus = DefenseBudget(level) * share;
        in.armor = 0.f;
        return in;
    }

    bool IsMultiplier(Out o)
    {
        return o < Out::HeavyGunRelief;
    }
}

TEST(stats_naked_is_mw2s_own_handling)
{
    Input in;
    in.level = 60;
    Derived const d = Compute(in);
    for (size_t i = 0; i < size_t(Out::Count); ++i)
        CHECK_NEAR(d.value[i], IsMultiplier(Out(i)) ? 1.f : 0.f, 1e-6);
    CHECK_NEAR(d.regenDelay, kRegenDelay, 1e-6);
    CHECK_NEAR(d.regenRate, kRegenRate, 1e-6);
    CHECK_NEAR(d.weakSpotChance, 0.f, 1e-6);
    CHECK(d.thresholds == 0);
    CHECK(!d.instantAdsAfterSprint);
}

TEST(stats_budget_grows_with_level)
{
    CHECK_NEAR(StatBudget(1), 10.f, 1e-4);
    CHECK_NEAR(StatBudget(10), 17.f, 1e-4);
    CHECK_NEAR(StatBudget(60), 312.f, 1e-3);
    CHECK(StatBudget(40) > StatBudget(20));
    CHECK_NEAR(RangedPowerBudget(60), 156.f, 1e-3);
    CHECK_NEAR(DefenseBudget(60), 120.f, 1e-4);
}

TEST(stats_the_same_share_of_gear_means_the_same_at_10_and_60)
{
    for (float share : {0.25f, 1.f, 2.f})
    {
        // Totals out of the way: thresholds are round numbers, the same at any level.
        Input lowIn = Geared(10, share), highIn = Geared(60, share);
        for (Input* in : {&lowIn, &highIn})
            in->strength = in->agility = in->stamina = in->intellect = in->spirit = 0.f;
        Derived const low = Compute(lowIn);
        Derived const high = Compute(highIn);
        for (Out o : {Out::AdsTime, Out::SwapTime, Out::SprintOutTime, Out::Recoil, Out::Sway, Out::ReloadTime,
                 Out::HeavyGunRelief, Out::EquipmentStrength, Out::RegenRate, Out::Bloom})
            CHECK_NEAR(low[o], high[o], 1e-4);
        CHECK_NEAR(low.regenDelay, high.regenDelay, 1e-4);
    }
}

TEST(stats_a_rating_gives_two_thirds_of_the_cap_when_well_geared)
{
    CHECK_NEAR(Share(0.3f, 0.f), 0.f, 1e-6);
    CHECK_NEAR(Share(0.3f, 1.f), 0.2f, 1e-5);
    CHECK_NEAR(Share(0.3f, 0.5f), 0.15f, 1e-5);
    // Diminishing, and never past the cap even at an absurd rating.
    CHECK(Share(0.3f, 2.f) - Share(0.3f, 1.f) < Share(0.3f, 1.f) - Share(0.3f, 0.f));
    CHECK(Share(0.3f, 1000.f) < 0.3f);
    CHECK_NEAR(Share(0.3f, -5.f), 0.f, 1e-6);
    // A rating is held to kMaxRating.
    Input in;
    in.level = 60;
    in.agilityBonus = 1e6f;
    CHECK_NEAR(Rating(in, Source::Agility), kMaxRating, 1e-6);
    in.agilityBonus = -50.f;    // a debuff below the base: no rating, no penalty
    CHECK_NEAR(Rating(in, Source::Agility), 0.f, 1e-6);
}

TEST(stats_each_stat_moves_its_own_outputs)
{
    Input base;
    base.level = 60;
    Derived const naked = Compute(base);

    Input agi = base;
    agi.agilityBonus = StatBudget(60);
    Derived const a = Compute(agi);
    CHECK(a[Out::AdsTime] < 1.f && a[Out::SwapTime] < 1.f && a[Out::SprintOutTime] < 1.f && a[Out::HipSpread] < 1.f);
    CHECK_NEAR(a[Out::Recoil], 1.f, 1e-6);
    CHECK_NEAR(a[Out::ReloadTime], 1.f, 1e-6);

    Input str = base;
    str.strengthBonus = StatBudget(60);
    Derived const s = Compute(str);
    CHECK(s[Out::Recoil] < 1.f);
    CHECK(s[Out::HeavyGunRelief] > 0.f);
    CHECK_NEAR(s[Out::AdsTime], 1.f, 1e-6);

    Input in = base;
    in.intellectBonus = StatBudget(60);
    CHECK(Compute(in)[Out::EquipmentStrength] > 0.f);

    Input spi = base;
    spi.spiritBonus = StatBudget(60);
    Derived const sp = Compute(spi);
    CHECK(sp.regenDelay < naked.regenDelay);
    CHECK(sp.regenRate > naked.regenRate);

    Input rap = base;
    rap.rangedPowerBonus = RangedPowerBudget(60);
    Derived const r = Compute(rap);
    CHECK(r[Out::Sway] < 1.f && r[Out::ReloadTime] < 1.f);
    CHECK_NEAR(r[Out::AdsTime], 1.f, 1e-6);

    Input hit = base;
    hit.hitPct = 6.f;
    Derived const h = Compute(hit);
    CHECK(h[Out::HipSpread] < 1.f && h[Out::Bloom] < 1.f);

    Input def = base;
    def.defenseBonus = 60.f;
    Derived const df = Compute(def);
    CHECK(df[Out::Flinch] < 1.f && df[Out::WeakSpotTaken] > 0.f);

    Input arm = base;
    arm.armor = 6000.f;
    Derived const ar = Compute(arm);
    CHECK(ar[Out::Flinch] < 1.f);
    CHECK_NEAR(ar.bulletCut, Scale::kMaxArmorCut, 1e-6);

    Input crit = base;
    crit.critPct = 10.f;
    CHECK_NEAR(Compute(crit).weakSpotChance, 0.10f, 1e-6);
}

TEST(stats_every_output_holds_its_cap)
{
    // Everything absurdly high, with the Pros and every threshold.
    Input in = Geared(60, 100.f);
    in.armor = 1e6f;
    in.critPct = 100.f;
    in.perks.pro = in.perks.sleightOfHand = in.perks.lightweight = true;
    Derived const d = Compute(in);
    for (size_t i = 0; i < size_t(Out::Count); ++i)
    {
        Out const o = Out(i);
        if (IsMultiplier(o))
        {
            CHECK(d[o] >= OutputLimit(o) - 1e-6f);
            CHECK(d[o] <= 1.f);
        }
        else
        {
            CHECK(d[o] <= OutputLimit(o) + 1e-6f);
            CHECK(d[o] >= 0.f);
        }
    }
    // The pinned caps.
    CHECK_NEAR(OutputLimit(Out::AdsTime), 0.55f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::SwapTime), 0.6f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::SprintOutTime), 0.4f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::Recoil), 0.65f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::HipSpread), 0.8f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::Bloom), 0.7f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::Sway), 0.5f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::ReloadTime), 0.8f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::Flinch), 0.4f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::HeavyGunRelief), 0.5f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::EquipmentStrength), 0.4f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::RegenRate), 1.f, 1e-6);
    CHECK_NEAR(OutputLimit(Out::WeakSpotTaken), 0.6f, 1e-6);
    CHECK(d.regenDelay >= kRegenMinDelay - 1e-6f);
    CHECK(d.regenDelay < 2.5f);
    CHECK(d.regenRate <= kRegenRate * 2.f + 1e-6f);
    CHECK_NEAR(d.weakSpotChance, kWeakSpotCap, 1e-6);
    CHECK_NEAR(d.bulletCut, Scale::kMaxArmorCut, 1e-6);
}

TEST(stats_no_output_raises_gun_damage)
{
    // A gun's multiplier is the level's alone, whatever the gear.
    for (uint32_t level : {1u, 10u, 30u, 60u})
        CHECK_NEAR(GunDamageMultiplier(level), Scale::LevelMultiplier(level), 1e-6);
    CHECK_NEAR(GunDamageMultiplier(60), 3.f, 1e-5);
    // Armor only ever takes damage away; FMJ and special rounds only take less away.
    for (float cut : {0.f, 0.1f, 0.3f, 0.9f})
        for (bool fmj : {false, true})
            for (float ignore : {0.f, 0.5f, 1.f})
            {
                float const c = BulletCut(cut, fmj, ignore);
                CHECK(c >= 0.f && c <= Scale::kMaxArmorCut);
            }
    CHECK_NEAR(BulletCut(0.3f, false, 0.f), 0.3f, 1e-6);
    CHECK_NEAR(BulletCut(0.3f, true, 0.f), 0.15f, 1e-6);
    CHECK_NEAR(BulletCut(0.3f, true, 0.5f), 0.075f, 1e-6);
    CHECK_NEAR(BulletCut(0.9f, false, 0.f), Scale::kMaxArmorCut, 1e-6);
    // The weak-spot chance is a share of body shots, never above the cap.
    CHECK_NEAR(WeakSpotChance(0.5f, 0.f), kWeakSpotCap, 1e-6);
    CHECK_NEAR(WeakSpotChance(0.2f, 0.5f), 0.1f, 1e-6);
    CHECK_NEAR(WeakSpotChance(-1.f, 0.f), 0.f, 1e-6);
    CHECK_NEAR(WeakSpotChance(0.2f, 5.f), 0.f, 1e-6);
    // No conversion feeds a damage output: there is none in the table.
    for (Conversion const& c : kConversions)
        CHECK(c.out < Out::Count);
}

TEST(stats_full_plate_cuts_about_thirty_percent)
{
    // Scale's cut, capped: a geared plate wearer at 60 hits the cap, cloth stays low.
    Input plate;
    plate.level = 60;
    plate.armor = 5500.f;
    CHECK_NEAR(Compute(plate).bulletCut, 0.30f, 1e-6);
    Input cloth;
    cloth.level = 60;
    cloth.armor = 800.f;
    float const c = Compute(cloth).bulletCut;
    CHECK(c > 0.08f && c < 0.16f);
}

TEST(stats_stamina_has_diminishing_returns)
{
    float const knee = 1.5f * StatBudget(60);
    CHECK_NEAR(EffectiveStamina(60, 0.f), 0.f, 1e-6);
    CHECK_NEAR(EffectiveStamina(60, 100.f), 100.f, 1e-4);
    CHECK_NEAR(EffectiveStamina(60, knee), knee, 1e-3);
    CHECK_NEAR(EffectiveStamina(60, knee + 100.f), knee + 50.f, 1e-3);
    CHECK_NEAR(EffectiveStamina(60, -20.f), -20.f, 1e-6);    // a debuff counts in full
    CHECK(EffectiveStamina(60, 900.f) > EffectiveStamina(60, 800.f));
}

TEST(stats_strength_eases_only_heavy_guns)
{
    CHECK_NEAR(HeavyGunMoveScale(WeaponClass::Lmg, 0.8f, 0.5f), 0.9f, 1e-6);
    CHECK_NEAR(HeavyGunMoveScale(WeaponClass::Sniper, 0.9f, 0.f), 0.9f, 1e-6);
    CHECK_NEAR(HeavyGunMoveScale(WeaponClass::AssaultRifle, 0.95f, 0.5f), 0.95f, 1e-6);
    // Relief never makes a gun quicker than empty hands, nor slower than its own.
    CHECK_NEAR(HeavyGunMoveScale(WeaponClass::Lmg, 0.8f, 5.f), 0.9f, 1e-6);
    CHECK_NEAR(HeavyGunMoveScale(WeaponClass::Lmg, 1.1f, 0.5f), 1.1f, 1e-6);
}

TEST(stats_sprint_out_follows_agility)
{
    CHECK(!SprintOutReady(0.2f, false, 1.f));
    CHECK(SprintOutReady(0.25f, false, 1.f));
    CHECK(SprintOutReady(0.16f, false, 0.6f));
    CHECK(!SprintOutReady(1.f, true, 0.6f));
    CHECK(SprintOutReady(0.06f, false, 0.6f, 0.1f));
    // A multiplier outside the limits is held to them.
    CHECK(!SprintOutReady(0.05f, false, 0.f));
    CHECK(SprintOutReady(0.3f, false, 3.f));
}

TEST(stats_regeneration_waits_then_runs)
{
    Derived d;
    CHECK_NEAR(RegenAmount(d, 300.f, 4.9f, 0.1f), 0.f, 1e-6);
    CHECK_NEAR(RegenAmount(d, 300.f, 6.f, 1.f), 300.f * kRegenRate, 1e-4);
    // Straddling the delay: only the part after it.
    CHECK_NEAR(RegenAmount(d, 300.f, 5.5f, 1.f), 300.f * kRegenRate * 0.5f, 1e-4);
    CHECK_NEAR(RegenAmount(d, 300.f, 9.f, 0.f), 0.f, 1e-6);
    CHECK_NEAR(RegenAmount(d, 0.f, 9.f, 1.f), 0.f, 1e-6);
    // Spirit: sooner and faster.
    Input in;
    in.level = 60;
    in.spiritBonus = StatBudget(60);
    Derived const s = Compute(in);
    CHECK(RegenAmount(s, 300.f, 4.5f, 0.5f) > 0.f);
    CHECK(RegenAmount(s, 300.f, 9.f, 1.f) > RegenAmount(d, 300.f, 9.f, 1.f));
}

TEST(stats_thresholds_unlock_their_bonuses)
{
    Input in;
    in.level = 60;
    in.agility = 149.f;
    Derived const below = Compute(in);
    CHECK(!below.instantAdsAfterSprint);
    in.agility = 150.f;
    Derived const at = Compute(in);
    CHECK(at.instantAdsAfterSprint);
    CHECK(at.thresholds & 1u);

    Input sp;
    sp.level = 60;
    sp.spirit = 200.f;
    Derived const s = Compute(sp);
    CHECK_NEAR(s.regenDelay, kRegenDelay - 1.f, 1e-5);

    // Every threshold has a text, and the bitmask fits.
    CHECK(kThresholds.size() <= 16);
    for (Threshold const& t : kThresholds)
        CHECK(t.text && *t.text && t.at > 0.f);
}

TEST(stats_the_pros_quicken_handling)
{
    Input in;
    in.level = 60;
    in.perks.sleightOfHand = true;
    CHECK_NEAR(Compute(in)[Out::AdsTime], 1.f, 1e-6);    // Sleight of Hand itself is the reload's
    in.perks.pro = true;
    CHECK_NEAR(Compute(in)[Out::AdsTime], kSleightOfHandProAds, 1e-6);
    Input lw;
    lw.level = 60;
    lw.perks.lightweight = lw.perks.pro = true;
    CHECK_NEAR(Compute(lw)[Out::SprintOutTime], kLightweightProSprintOut, 1e-6);
}

TEST(stats_gun_procs_scale_down_for_fire_rate)
{
    // A 1 PPM proc on an M4 (0.075 s): a minute of full fire procs about half a time.
    float const ppm = GunProcChance(0.f, 1.f, 0.075f);
    CHECK_NEAR(ppm, 1.f * 0.075f / 60.f * kGunProcRate, 1e-7);
    CHECK_NEAR(ppm * (60.f / 0.075f), kGunProcRate, 1e-4);
    // A flat 10% chance on hit: as often as a 2 s swing would, at half the rate.
    float const flat = GunProcChance(10.f, 0.f, 0.1f);
    CHECK_NEAR(flat, 0.10f * 0.1f / kProcReferenceSwing * kGunProcRate, 1e-7);
    // A slow gun never procs more than the flat chance per pull.
    CHECK(GunProcChance(10.f, 0.f, 10.f) <= 0.10f + 1e-6f);
    CHECK(GunProcChance(0.f, 60.f, 10.f) <= 1.f);
    CHECK_NEAR(GunProcChance(0.f, 0.f, 0.1f), 0.f, 1e-7);
    CHECK_NEAR(GunProcChance(10.f, 0.f, -1.f), 0.f, 1e-7);
}

TEST(stats_handling_round_trips_on_channel_6)
{
    Input in = Geared(60, 1.f);
    in.armor = 3000.f;
    in.critPct = 8.f;
    in.agility = 160.f;
    Derived const d = Compute(in);
    std::vector<uint8_t> const bytes = EncodeHandling(d);
    CHECK(bytes.size() == 2 + 2 * (size_t(Out::Count) + 4) + 2 + 1);
    CHECK(bytes[0] == kOpHandling && bytes[1] == kHandlingVersion);
    Derived back;
    CHECK(DecodeHandling(bytes, back));
    CHECK(SameOnWire(d, back));
    for (size_t i = 0; i < size_t(Out::Count); ++i)
        CHECK_NEAR(back.value[i], d.value[i], 0.0006);
    CHECK_NEAR(back.regenDelay, d.regenDelay, 0.0006);
    CHECK(back.instantAdsAfterSprint);
    CHECK(back.thresholds == d.thresholds);
    // Short, wrong op or wrong version: refused.
    std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 1);
    CHECK(!DecodeHandling(cut, back));
    std::vector<uint8_t> bad = bytes;
    bad[1] = 9;
    CHECK(!DecodeHandling(bad, back));
    bad = bytes;
    bad[0] = 2;
    CHECK(!DecodeHandling(bad, back));
}

TEST(stats_handling_bytes_match_the_client)
{
    // The client's `handling_decodes_the_servers_bytes` (client/src/handling.rs) decodes these
    // same bytes: every multiplier 1, every share 0, the delay 5 s, the rate 0.125, then weak
    // spot 0.1, cut 0.2, thresholds 0b101, flags instant ADS.
    Derived d;
    d.weakSpotChance = 0.1f;
    d.bulletCut = 0.2f;
    d.thresholds = 0x5;
    d.instantAdsAfterSprint = true;
    d.value[size_t(Out::AdsTime)] = 0.75f;
    d.value[size_t(Out::EquipmentStrength)] = 0.25f;
    std::string const hex = Packets::ToHex(EncodeHandling(d));
    CHECK(hex == "0101ee02e803e803e803e803e803e803e803e8030000fa0000000000000088137d006400c800050001");
    if (hex != "0101ee02e803e803e803e803e803e803e803e8030000fa0000000000000088137d006400c800050001")
        std::printf("  bytes: %s\n", hex.c_str());
}
