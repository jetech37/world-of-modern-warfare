/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/FieldGear.h"

#include <algorithm>

namespace Fusion::FieldGear
{
    std::vector<Item> const& All()
    {
        // Entries as sql/custom/azeroth_warfare/11_prestige_professions.sql makes them.
        static std::vector<Item> const items = {
            // Engineering: one more of the kit's own equipment.
            {93200, Kind::Charge, EquipmentKind::Frag, StimKind::None, 0.f},
            {93201, Kind::Charge, EquipmentKind::Semtex, StimKind::None, 0.f},
            {93202, Kind::Charge, EquipmentKind::Claymore, StimKind::None, 0.f},
            {93203, Kind::Charge, EquipmentKind::C4, StimKind::None, 0.f},
            {93204, Kind::Charge, EquipmentKind::ThrowingKnife, StimKind::None, 0.f},
            {93205, Kind::Charge, EquipmentKind::Flash, StimKind::None, 0.f},
            {93206, Kind::Charge, EquipmentKind::Stun, StimKind::None, 0.f},
            {93207, Kind::Charge, EquipmentKind::Smoke, StimKind::None, 0.f},
            // Leatherworking: bandoliers (ammo pouches), a quarter of the reserve more a step.
            {93220, Kind::Bandolier, EquipmentKind::None, StimKind::None, 0.25f},
            {93221, Kind::Bandolier, EquipmentKind::None, StimKind::None, 0.5f},
            {93222, Kind::Bandolier, EquipmentKind::None, StimKind::None, 0.75f},
            {93223, Kind::Bandolier, EquipmentKind::None, StimKind::None, 1.0f},     // the prestige shop's
            // Tailoring: ghillie mantles (cloaks), the aggro radius they leave.
            {93240, Kind::Ghillie, EquipmentKind::None, StimKind::None, 0.85f},
            {93241, Kind::Ghillie, EquipmentKind::None, StimKind::None, 0.75f},
            {93242, Kind::Ghillie, EquipmentKind::None, StimKind::None, 0.65f},
            {93243, Kind::Ghillie, EquipmentKind::None, StimKind::None, 0.6f},       // the prestige shop's
            // Alchemy: combat stims.
            {93260, Kind::Stim, EquipmentKind::None, StimKind::Focus, 0.f},
            {93261, Kind::Stim, EquipmentKind::None, StimKind::SecondWind, 0.f},
            {93262, Kind::Stim, EquipmentKind::None, StimKind::Adrenaline, 0.f},
        };
        return items;
    }

    Item const* Find(uint32_t entry)
    {
        if (entry < 93200 || entry > 93399)
            return nullptr;
        for (Item const& i : All())
            if (i.entry == entry)
                return &i;
        return nullptr;
    }

    bool IsLethal(EquipmentKind k)
    {
        switch (k)
        {
            case EquipmentKind::Frag:
            case EquipmentKind::Semtex:
            case EquipmentKind::ThrowingKnife:
            case EquipmentKind::Claymore:
            case EquipmentKind::C4:
                return true;
            default:
                return false;
        }
    }

    ChargeResult AddCharge(Item const& item, WeaponProfile const* kit, GunAmmo& ammo)
    {
        if (item.kind != Kind::Charge || !kit || KindOf(kit->name) != item.equipment)
            return ChargeResult::NoSuchEquipment;
        uint32_t const own = std::max<uint32_t>(1, kit->magSize);
        return ammo.AddCharges(1, own + kMaxExtraCharges) ? ChargeResult::Added : ChargeResult::Full;
    }

    float ReserveBonus(std::vector<uint32_t> const& worn)
    {
        float best = 0.f;
        for (uint32_t e : worn)
            if (Item const* i = Find(e); i && i->kind == Kind::Bandolier)
                best = std::max(best, i->amount);
        return best;
    }

    float GhillieMultiplier(std::vector<uint32_t> const& worn)
    {
        float best = 1.f;
        for (uint32_t e : worn)
            if (Item const* i = Find(e); i && i->kind == Kind::Ghillie)
                best = std::min(best, i->amount);
        return best;
    }

    bool IsGhillie(uint32_t entry)
    {
        Item const* i = Find(entry);
        return i && i->kind == Kind::Ghillie;
    }

    uint32_t StimMs(StimKind k)
    {
        switch (k)
        {
            case StimKind::Focus: return 60000;
            case StimKind::SecondWind: return 12000;
            case StimKind::Adrenaline: return 20000;
            default: return 0;
        }
    }

    void ApplyStims(Stats::Derived& d, Stims const& s, uint32_t nowMs)
    {
        using Stats::Out;
        if (s.Active(StimKind::Focus, nowMs))
        {
            d.value[size_t(Out::Sway)] *= kFocusSway;
            d.value[size_t(Out::Recoil)] *= kFocusRecoil;
            d.value[size_t(Out::Flinch)] *= kFocusFlinch;
            d.value[size_t(Out::HipSpread)] *= kFocusHipSpread;
        }
        if (s.Active(StimKind::SecondWind, nowMs))
        {
            d.regenDelay = std::min(d.regenDelay, kSecondWindDelay);
            d.regenRate *= kSecondWindRate;
        }
    }
}
