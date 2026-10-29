/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Stats.h"
#include "fusion/Scale.h"
#include "fusion/Catalog.h"

namespace Fusion { namespace Stats
{
    namespace
    {
        bool IsMultiplier(Out o)
        {
            return o < Out::HeavyGunRelief;
        }
    }

    // The table (design doc, section 7). Each line: a source, what it moves, and the most it
    // alone can give (two thirds of that for a well-geared character). Stamina has no line: it
    // is health (EffectiveStamina) and, later, the downed bleed-out.
    std::vector<Conversion> const kConversions = {
        // Strength: recoil control, less of a heavy gun's weight.
        {Source::Strength, Out::Recoil, 0.30f},
        {Source::Strength, Out::HeavyGunRelief, 0.50f},
        // Agility: quicker hands, a little hip-fire accuracy.
        {Source::Agility, Out::AdsTime, 0.30f},
        {Source::Agility, Out::SwapTime, 0.30f},
        {Source::Agility, Out::SprintOutTime, 0.40f},
        {Source::Agility, Out::HipSpread, 0.08f},
        // Intellect: equipment strength.
        {Source::Intellect, Out::EquipmentStrength, 0.30f},
        // Spirit: regeneration sooner and faster.
        {Source::Spirit, Out::RegenDelay, 0.40f},
        {Source::Spirit, Out::RegenRate, 0.80f},
        // Ranged attack power: handling, never damage.
        {Source::RangedPower, Out::Sway, 0.40f},
        {Source::RangedPower, Out::ReloadTime, 0.15f},
        // Hit: a tighter cone, less bloom.
        {Source::Hit, Out::HipSpread, 0.12f},
        {Source::Hit, Out::Bloom, 0.25f},
        // Defense: less flinch, fewer weak-spot hits taken.
        {Source::Defense, Out::Flinch, 0.30f},
        {Source::Defense, Out::WeakSpotTaken, 0.50f},
        // Armor: less flinch (its bullet cut is Scale's, below).
        {Source::Armor, Out::Flinch, 0.30f},
    };

    std::vector<Threshold> const kThresholds = {
        {StatKind::Agility, 150.f, Bonus::InstantAdsAfterSprint, 0.f, "150 Agility: the first aim after a sprint is instant"},
        {StatKind::Spirit, 200.f, Bonus::RegenEarlier, 1.f, "200 Spirit: health starts coming back a second sooner"},
        {StatKind::Strength, 150.f, Bonus::SteadyFirstShot, 0.f, "150 Strength: the first round of a burst does not kick"},
        {StatKind::Agility, 250.f, Bonus::QuickerSwap, 0.10f, "250 Agility: swaps another 10% quicker"},
        {StatKind::Intellect, 200.f, Bonus::StrongerEquipment, 0.10f, "200 Intellect: equipment 10% stronger"},
        {StatKind::Stamina, 250.f, Bonus::Unflinching, 0.15f, "250 Stamina: 15% less flinch"},
        {StatKind::Defense, 25.f, Bonus::Hardened, 0.25f, "25 Defense over the cap: a quarter fewer weak-spot hits taken"},
        {StatKind::Spirit, 300.f, Bonus::RegenFaster, 0.20f, "300 Spirit: health comes back 20% faster"},
    };

    float OutputLimit(Out o)
    {
        switch (o)
        {
            case Out::AdsTime: return 0.55f;
            case Out::SwapTime: return 0.6f;
            case Out::SprintOutTime: return 0.4f;
            case Out::Recoil: return 0.65f;
            case Out::HipSpread: return 0.8f;
            case Out::Bloom: return 0.7f;
            case Out::Sway: return 0.5f;
            case Out::ReloadTime: return 0.8f;
            case Out::Flinch: return 0.4f;
            case Out::HeavyGunRelief: return 0.5f;
            case Out::EquipmentStrength: return 0.4f;
            case Out::RegenDelay: return 0.6f;
            case Out::RegenRate: return 1.f;
            case Out::WeakSpotTaken: return 0.6f;
            default: return 1.f;
        }
    }

    float StatBudget(uint32_t level)
    {
        float const l = float(std::max<uint32_t>(1, level));
        return std::max(10.f, 0.07f * l * l + l);
    }

    float RangedPowerBudget(uint32_t level)
    {
        return 0.5f * StatBudget(level);
    }

    float DefenseBudget(uint32_t level)
    {
        return std::max(10.f, 2.f * float(level));
    }

    float Rating(Input const& in, Source s)
    {
        float r = 0.f;
        float const budget = StatBudget(in.level);
        switch (s)
        {
            case Source::Strength: r = in.strengthBonus / budget; break;
            case Source::Agility: r = in.agilityBonus / budget; break;
            case Source::Stamina: r = in.staminaBonus / budget; break;
            case Source::Intellect: r = in.intellectBonus / budget; break;
            case Source::Spirit: r = in.spiritBonus / budget; break;
            case Source::RangedPower: r = in.rangedPowerBonus / RangedPowerBudget(in.level); break;
            case Source::Hit: r = in.hitPct / kHitBudgetPct; break;
            case Source::Defense: r = in.defenseBonus / DefenseBudget(in.level); break;
            case Source::Armor: r = Scale::PlayerArmorCut(in.armor, in.level) / Scale::kMaxArmorCut; break;
            default: break;
        }
        if (!(r > 0.f))     // also a NaN
            return 0.f;
        return std::min(r, kMaxRating);
    }

    float Share(float cap, float rating)
    {
        if (!(rating > 0.f) || !(cap > 0.f))
            return 0.f;
        return cap * rating / (rating + 0.5f);
    }

    Derived::Derived()
    {
        for (size_t i = 0; i < value.size(); ++i)
            value[i] = IsMultiplier(Out(i)) ? 1.f : 0.f;
    }

    namespace
    {
        float TotalOf(Input const& in, StatKind k)
        {
            switch (k)
            {
                case StatKind::Strength: return in.strength;
                case StatKind::Agility: return in.agility;
                case StatKind::Stamina: return in.stamina;
                case StatKind::Intellect: return in.intellect;
                case StatKind::Spirit: return in.spirit;
                case StatKind::Defense: return in.defenseBonus;
            }
            return 0.f;
        }
    }

    Derived Compute(Input const& in)
    {
        Derived d;
        // Every source's share, summed per output.
        std::array<float, size_t(Out::Count)> share{};
        for (Conversion const& c : kConversions)
            share[size_t(c.out)] += Share(c.cap, Rating(in, c.source));

        // Thresholds.
        float regenEarlier = 0.f;
        for (size_t i = 0; i < kThresholds.size(); ++i)
        {
            Threshold const& t = kThresholds[i];
            if (TotalOf(in, t.stat) < t.at)
                continue;
            d.thresholds |= uint16_t(1u << i);
            switch (t.bonus)
            {
                case Bonus::InstantAdsAfterSprint: d.instantAdsAfterSprint = true; break;
                case Bonus::SteadyFirstShot: d.steadyFirstShot = true; break;
                case Bonus::RegenEarlier: regenEarlier += t.amount; break;
                case Bonus::RegenFaster: share[size_t(Out::RegenRate)] += t.amount; break;
                case Bonus::QuickerSwap: share[size_t(Out::SwapTime)] += t.amount; break;
                case Bonus::StrongerEquipment: share[size_t(Out::EquipmentStrength)] += t.amount; break;
                case Bonus::Unflinching: share[size_t(Out::Flinch)] += t.amount; break;
                case Bonus::Hardened: share[size_t(Out::WeakSpotTaken)] += t.amount; break;
            }
        }

        for (size_t i = 0; i < size_t(Out::Count); ++i)
        {
            Out const o = Out(i);
            if (IsMultiplier(o))
            {
                float m = 1.f - share[i];
                // MW2's Pros: Sleight of Hand Pro aims faster, Lightweight Pro sprints out faster.
                if (o == Out::AdsTime && in.perks.sleightOfHand && in.perks.IsPro(PerkId::SleightOfHand))
                    m *= kSleightOfHandProAds;
                if (o == Out::SprintOutTime && in.perks.lightweight && in.perks.IsPro(PerkId::Lightweight))
                    m *= kLightweightProSprintOut;
                d.value[i] = Clamp(m, OutputLimit(o), 1.f);
            }
            else
                d.value[i] = Clamp(share[i], 0.f, OutputLimit(o));
        }

        d.regenDelay = std::max(kRegenMinDelay, kRegenDelay * (1.f - d[Out::RegenDelay]) - regenEarlier);
        d.regenRate = kRegenRate * (1.f + d[Out::RegenRate]);
        d.weakSpotChance = Clamp(in.critPct / 100.f, 0.f, kWeakSpotCap);
        d.bulletCut = Scale::PlayerArmorCut(in.armor, in.level);
        return d;
    }

    float GunDamageMultiplier(uint32_t level)
    {
        return Scale::LevelMultiplier(level);
    }

    float BulletCut(float armorCut, bool fmj, float armorIgnore)
    {
        float cut = Clamp(armorCut, 0.f, Scale::kMaxArmorCut);
        if (fmj)
            cut *= 1.f - kFmjArmorPierce;
        return cut * (1.f - Clamp(armorIgnore, 0.f, 1.f));
    }

    float WeakSpotChance(float shooterChance, float targetWeakSpotTaken)
    {
        return Clamp(shooterChance, 0.f, kWeakSpotCap) * (1.f - Clamp(targetWeakSpotTaken, 0.f, 1.f));
    }

    float EffectiveStamina(uint32_t level, float staminaBonus)
    {
        float const knee = 1.5f * StatBudget(level);
        if (staminaBonus <= knee)
            return staminaBonus;
        return knee + 0.5f * (staminaBonus - knee);
    }

    float HeavyGunMoveScale(WeaponClass c, float moveSpeedScale, float relief)
    {
        if ((c != WeaponClass::Lmg && c != WeaponClass::Sniper) || moveSpeedScale >= 1.f)
            return moveSpeedScale;
        float const r = Clamp(relief, 0.f, OutputLimit(Out::HeavyGunRelief));
        return 1.f - (1.f - moveSpeedScale) * (1.f - r);
    }

    bool SprintOutReady(float secondsSinceSprint, bool sprinting, float sprintOutMultiplier, float slack)
    {
        float const m = Clamp(sprintOutMultiplier, OutputLimit(Out::SprintOutTime), 1.f);
        return !sprinting && secondsSinceSprint + slack >= kSprintOutTime * m;
    }

    float RegenAmount(Derived const& d, float maxHealth, float secondsSinceHurt, float dt)
    {
        if (!(maxHealth > 0.f) || !(dt > 0.f))
            return 0.f;
        float const after = std::min(dt, secondsSinceHurt - d.regenDelay);
        if (!(after > 0.f))
            return 0.f;
        return maxHealth * d.regenRate * after;
    }

    float GunProcChance(float chancePct, float ppm, float fireTime)
    {
        if (!(fireTime > 0.f))
            return 0.f;
        float p = ppm > 0.f ? ppm * fireTime / 60.f : chancePct / 100.f * fireTime / kProcReferenceSwing;
        p *= kGunProcRate;
        if (chancePct > 0.f && chancePct <= 100.f)
            p = std::min(p, chancePct / 100.f);
        return Clamp(p, 0.f, 1.f);
    }

    namespace
    {
        void PutU16(std::vector<uint8_t>& o, uint16_t v)
        {
            o.push_back(uint8_t(v & 0xFF));
            o.push_back(uint8_t(v >> 8));
        }

        uint16_t Thousandths(float v)
        {
            float const t = std::round(v * 1000.f);
            return uint16_t(Clamp(t, 0.f, 65535.f));
        }

        constexpr size_t kHandlingBytes = 2 + 2 * (size_t(Out::Count) + 4) + 2 + 1;
    }

    std::vector<uint8_t> EncodeHandling(Derived const& d)
    {
        std::vector<uint8_t> o;
        o.reserve(kHandlingBytes);
        o.push_back(kOpHandling);
        o.push_back(kHandlingVersion);
        for (float v : d.value)
            PutU16(o, Thousandths(v));
        PutU16(o, Thousandths(d.regenDelay));   // seconds in thousandths: milliseconds
        PutU16(o, Thousandths(d.regenRate));
        PutU16(o, Thousandths(d.weakSpotChance));
        PutU16(o, Thousandths(d.bulletCut));
        PutU16(o, d.thresholds);
        o.push_back(uint8_t((d.instantAdsAfterSprint ? kFlagInstantAds : 0) | (d.steadyFirstShot ? kFlagSteadyFirstShot : 0)));
        return o;
    }

    bool DecodeHandling(std::vector<uint8_t> const& body, Derived& out)
    {
        if (body.size() != kHandlingBytes || body[0] != kOpHandling || body[1] != kHandlingVersion)
            return false;
        size_t at = 2;
        auto u16 = [&]() {
            uint16_t const v = uint16_t(body[at] | (body[at + 1] << 8));
            at += 2;
            return v;
        };
        Derived d;
        for (float& v : d.value)
            v = float(u16()) / 1000.f;
        d.regenDelay = float(u16()) / 1000.f;
        d.regenRate = float(u16()) / 1000.f;
        d.weakSpotChance = float(u16()) / 1000.f;
        d.bulletCut = float(u16()) / 1000.f;
        d.thresholds = u16();
        uint8_t const flags = body[at];
        d.instantAdsAfterSprint = flags & kFlagInstantAds;
        d.steadyFirstShot = flags & kFlagSteadyFirstShot;
        out = d;
        return true;
    }

    bool SameOnWire(Derived const& a, Derived const& b)
    {
        return EncodeHandling(a) == EncodeHandling(b);
    }
}}
