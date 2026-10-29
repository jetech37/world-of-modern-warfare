/*
 * Azeroth Warfare: tests for the professions' field gear (FieldGear.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/FieldGear.h"

using namespace Fusion;
using namespace Fusion::FieldGear;

namespace
{
    WeaponProfile Equip(char const* name, uint16_t mag)
    {
        WeaponProfile w;
        w.id = 4001;
        w.name = name;
        w.magSize = mag;
        w.maxReserve = 0;
        return w;
    }
}

TEST(field_gear_items_are_in_w8s_range)
{
    for (Item const& i : All())
    {
        CHECK(i.entry >= 93200 && i.entry <= 93399);
        CHECK(Find(i.entry) == &i);
    }
    CHECK(Find(93199) == nullptr && Find(90001) == nullptr && Find(93399) == nullptr);
    CHECK(IsLethal(EquipmentKind::C4) && !IsLethal(EquipmentKind::Smoke));
}

TEST(field_gear_adds_a_charge_of_the_kits_own_kind)
{
    WeaponProfile const frag = Equip("frag_grenade_mp", 1);
    GunAmmo ammo;
    ammo.Reset(frag);
    CHECK(ammo.Mag() == 1);
    Item const& spareFrag = *Find(93200);
    Item const& spareFlash = *Find(93205);
    CHECK(AddCharge(spareFlash, &frag, ammo) == ChargeResult::NoSuchEquipment);
    CHECK(AddCharge(spareFrag, nullptr, ammo) == ChargeResult::NoSuchEquipment);
    CHECK(AddCharge(spareFrag, &frag, ammo) == ChargeResult::Added && ammo.Mag() == 2);
    CHECK(AddCharge(spareFrag, &frag, ammo) == ChargeResult::Added && ammo.Mag() == 3);
    CHECK(AddCharge(spareFrag, &frag, ammo) == ChargeResult::Full && ammo.Mag() == 3);
    // Thrown down to none, the spares still count.
    CHECK(ammo.Fire() && ammo.Fire() && ammo.Fire() && !ammo.Fire());
    CHECK(AddCharge(spareFrag, &frag, ammo) == ChargeResult::Added && ammo.Mag() == 1);
    // A flash kit holds two of its own and two more.
    WeaponProfile const flash = Equip("flash_grenade_mp", 2);
    GunAmmo f;
    f.Reset(flash);
    CHECK(AddCharge(spareFlash, &flash, f) == ChargeResult::Added);
    CHECK(AddCharge(spareFlash, &flash, f) == ChargeResult::Added);
    CHECK(AddCharge(spareFlash, &flash, f) == ChargeResult::Full && f.Mag() == 4);
}

TEST(field_gear_bandolier_carries_more_reserve_not_a_bigger_mag)
{
    WeaponProfile m4;
    m4.id = 2;
    m4.name = "m4_mp";
    m4.magSize = 30;
    m4.maxReserve = 120;
    GunAmmo a;
    a.Reset(m4);
    CHECK(a.Reserve() == 120);
    float const bonus = ReserveBonus({93220, 93222, 12345});
    CHECK_NEAR(bonus, 0.75, 1e-6);
    a.SetReserveBonus(bonus, true);
    CHECK(a.MaxReserve() == 210 && a.Reserve() == 210 && a.Mag() == 30);
    // Taking it off brings the reserve down to the gun's own.
    a.SetReserveBonus(0.f, false);
    CHECK(a.MaxReserve() == 120 && a.Reserve() == 120);
    // Put back on mid-life: room for more, filled by rearms, not at once.
    a.SetReserveBonus(0.5f, false);
    CHECK(a.MaxReserve() == 180 && a.Reserve() == 120);
    a.FullResupply();
    CHECK(a.Reserve() == 180);
    CHECK_NEAR(ReserveBonus({}), 0.0, 1e-6);
}

TEST(field_gear_ghillie_shrinks_the_aggro_radius)
{
    CHECK_NEAR(GhillieMultiplier({}), 1.0, 1e-6);
    CHECK_NEAR(GhillieMultiplier({93240}), 0.85, 1e-6);
    CHECK_NEAR(GhillieMultiplier({93240, 93242}), 0.65, 1e-6);
    CHECK(IsGhillie(93243) && !IsGhillie(93220));
}

TEST(field_gear_stims_change_handling_while_they_last)
{
    Stims s;
    Stats::Derived d;
    ApplyStims(d, s, 1000);
    CHECK_NEAR(d[Stats::Out::Sway], 1.0, 1e-6);
    s.Take(StimKind::Focus, 1000);
    CHECK(s.Active(StimKind::Focus, 1000 + 59999) && !s.Active(StimKind::Focus, 1000 + 60000));
    ApplyStims(d, s, 2000);
    CHECK_NEAR(d[Stats::Out::Sway], kFocusSway, 1e-6);
    CHECK_NEAR(d[Stats::Out::Recoil], kFocusRecoil, 1e-6);
    Stats::Derived r;
    s.Take(StimKind::SecondWind, 1000);
    ApplyStims(r, s, 2000);
    CHECK_NEAR(r.regenDelay, kSecondWindDelay, 1e-6);
    CHECK_NEAR(r.regenRate, Stats::kRegenRate * kSecondWindRate, 1e-6);
    Stats::Derived late;
    ApplyStims(late, s, 1000 + 70000);
    CHECK_NEAR(late.regenRate, Stats::kRegenRate, 1e-6);
    CHECK(!s.Active(StimKind::Adrenaline, 0));
    s.Take(StimKind::Adrenaline, 5);
    CHECK(s.Active(StimKind::Adrenaline, 6));
}
