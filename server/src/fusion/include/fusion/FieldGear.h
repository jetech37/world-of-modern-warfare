/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_FIELD_GEAR_H
#define FUSION_FIELD_GEAR_H

#include "fusion/Ammo.h"
#include "fusion/Equipment.h"
#include "fusion/Stats.h"

#include <cstdint>
#include <vector>

// The professions' first pass (design doc section 12): professions never make guns stronger;
// they make what a fighter carries. Items 93200-93399 (W8's range):
//
// - Engineering, field gear: one more charge of the kit's own lethal or tactical, beyond the kit's
//   free one, for this life (a frag, semtex, claymore, C4, flash, stun or smoke). It only fits the
//   kit's own kind, as MW2 carries one kind of each.
// - Leatherworking, bandoliers: an ammo pouch worn in a bag slot carries more spare ammunition
//   (the reserve), never a bigger magazine.
// - Tailoring, ghillie mantles: a cloak that shrinks the radius creatures notice you in, and keeps
//   you off enemy radar (a UAV's sweep) unless you are fighting them.
// - Alchemy, combat stims: a short, steadier aim (Focus), a regeneration burst (Second Wind) or a
//   sprint that does not tire (Adrenaline).
namespace Fusion::FieldGear
{
    enum class Kind : uint8_t
    {
        None = 0,
        Charge = 1,         // Engineering: `equipment`
        Bandolier = 2,      // Leatherworking: `amount` more reserve (a share)
        Ghillie = 3,        // Tailoring: `amount` the aggro radius multiplier
        Stim = 4,           // Alchemy: `stim`
    };

    enum class StimKind : uint8_t
    {
        None = 0,
        Focus = 1,          // steadier aim
        SecondWind = 2,     // regeneration burst
        Adrenaline = 3,     // sprint without tiring
        Count,
    };

    struct Item
    {
        uint32_t entry = 0;
        Kind kind = Kind::None;
        EquipmentKind equipment = EquipmentKind::None;
        StimKind stim = StimKind::None;
        float amount = 0.f;
    };

    // The item, or null when it is not field gear.
    Item const* Find(uint32_t entry);
    std::vector<Item> const& All();

    // ── Engineering ──
    // The most charges a life may carry over the kit's own (its magazine).
    constexpr uint32_t kMaxExtraCharges = 2;
    enum class ChargeResult : uint8_t
    {
        Added,
        NoSuchEquipment,    // the kit carries no lethal / tactical of the item's kind
        Full,               // already the kit's charges and kMaxExtraCharges more
    };
    // Add the item's charge to the kit's equipment ammo (`kit` the kit's lethal or tactical
    // profile, null for none).
    ChargeResult AddCharge(Item const& item, WeaponProfile const* kit, GunAmmo& ammo);
    // Whether the item's equipment is a lethal (else a tactical).
    bool IsLethal(EquipmentKind k);

    // ── Leatherworking and Tailoring: what worn items add up to ──
    // The reserve bonus of the bandoliers among `worn` (the best one counts).
    float ReserveBonus(std::vector<uint32_t> const& worn);
    // The aggro radius multiplier of the ghillies among `worn` (the best one), 1 without.
    float GhillieMultiplier(std::vector<uint32_t> const& worn);
    bool IsGhillie(uint32_t entry);

    // ── Alchemy ──
    uint32_t StimMs(StimKind k);
    // When each stim wears off (server clock, 0 none).
    struct Stims
    {
        uint32_t untilMs[size_t(StimKind::Count)] = {};
        void Take(StimKind k, uint32_t nowMs) { untilMs[size_t(k)] = nowMs + StimMs(k); }
        bool Active(StimKind k, uint32_t nowMs) const
        {
            uint32_t const u = untilMs[size_t(k)];
            return u && int32_t(nowMs - u) < 0;
        }
    };
    // What the stims in effect do to the stats' handling (after Stats::Compute).
    void ApplyStims(Stats::Derived& d, Stims const& s, uint32_t nowMs);
    // A Focus stim's multipliers.
    constexpr float kFocusSway = 0.6f, kFocusRecoil = 0.85f, kFocusFlinch = 0.7f, kFocusHipSpread = 0.9f;
    // A Second Wind's regeneration: it starts after this delay and runs this many times faster.
    constexpr float kSecondWindDelay = 1.f, kSecondWindRate = 2.5f;
}

#endif
