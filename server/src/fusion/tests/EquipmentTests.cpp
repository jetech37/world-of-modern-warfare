/*
 * Azeroth Warfare: tests for thrown and planted equipment (Equipment.h). GPL-2.0-or-later.
 */

#include "Test.h"
#include "fusion/Equipment.h"
#include "fusion/Catalog.h"
#include "fusion/Loadout.h"

#include <cmath>
#include <optional>

using namespace Fusion;

namespace
{
    // Flat ground at z = 0: a segment crossing it hits where it crosses.
    std::optional<Vec3> Ground(Vec3 a, Vec3 b)
    {
        if (a.z >= 0.f && b.z < 0.f)
        {
            float const t = a.z / (a.z - b.z);
            return a + (b - a) * t;
        }
        return std::nullopt;
    }
}

TEST(a_grenade_arcs_to_the_ground)
{
    // Thrown level from 1.5 yd up at the throw speed: it lands ahead, on the ground.
    Throw const t = ThrowArc({0.f, 0.f, 1.5f}, {1.f, 0.f, 0.f}, kThrowSpeed, Ground);
    CHECK(t.landed);
    CHECK_NEAR(t.at.z, 0.f, 1e-3);
    CHECK(t.at.x > 5.f && t.at.x < kThrowSpeed * 2.f);
    CHECK(t.seconds > 0.f);
    // Thrown up, it goes further than thrown level.
    Throw const lob = ThrowArc({0.f, 0.f, 1.5f}, Vec3{1.f, 0.f, 1.f}.Normalized(), kThrowSpeed, Ground);
    CHECK(lob.at.x > t.at.x);
    // Into a wall a yard ahead: it stops there.
    auto wall = [](Vec3 a, Vec3 b) -> std::optional<Vec3> {
        if (a.x < 1.f && b.x >= 1.f)
            return a + (b - a) * ((1.f - a.x) / (b.x - a.x));
        return Ground(a, b);
    };
    Throw const hit = ThrowArc({0.f, 0.f, 1.5f}, {1.f, 0.f, 0.f}, kThrowSpeed, wall);
    CHECK(hit.landed && std::fabs(hit.at.x - 1.f) < 0.05f);
}

TEST(a_throw_into_nothing_ends_at_its_reach)
{
    auto none = [](Vec3, Vec3) -> std::optional<Vec3> { return std::nullopt; };
    Throw const t = ThrowArc({0.f, 0.f, 1.5f}, {1.f, 0.f, 0.f}, kThrowSpeed, none);
    CHECK(!t.landed);
    CHECK(t.seconds <= kMaxFlightSeconds + 1e-3f);
}

TEST(each_kind_keeps_its_own_fuse)
{
    CHECK_NEAR(FuseSeconds(EquipmentKind::Frag), 3.5f, 1e-6);
    CHECK_NEAR(FuseSeconds(EquipmentKind::Semtex), 2.5f, 1e-6);
    CHECK(FuseSeconds(EquipmentKind::C4) < 0.f);          // on the trigger
    CHECK(FuseSeconds(EquipmentKind::Claymore) < 0.f);    // on a tripwire
    CHECK(IsThrown(EquipmentKind::Frag) && IsThrown(EquipmentKind::Flash) && !IsThrown(EquipmentKind::Claymore));
}

TEST(a_claymore_trips_on_what_walks_in_front)
{
    Vec3 const at{0.f, 0.f, 0.f};
    float const facing = 0.f;                               // along +x
    CHECK(ClaymoreTripped(at, facing, {2.f, 0.f, 0.f}));
    CHECK(ClaymoreTripped(at, facing, {2.f, 1.f, 0.f}));   // in the cone
    CHECK(!ClaymoreTripped(at, facing, {-2.f, 0.f, 0.f})); // behind
    CHECK(!ClaymoreTripped(at, facing, {0.5f, 2.5f, 0.f})); // to the side
    CHECK(!ClaymoreTripped(at, facing, {5.f, 0.f, 0.f}));  // too far
}

TEST(equipment_kinds_come_from_their_names)
{
    CHECK(KindOf("frag_grenade_mp") == EquipmentKind::Frag);
    CHECK(KindOf("semtex_mp") == EquipmentKind::Semtex);
    CHECK(KindOf("c4_mp") == EquipmentKind::C4);
    CHECK(KindOf("claymore_mp") == EquipmentKind::Claymore);
    CHECK(KindOf("throwingknife_mp") == EquipmentKind::ThrowingKnife);
    CHECK(KindOf("flash_grenade_mp") == EquipmentKind::Flash);
    CHECK(KindOf("concussion_grenade_mp") == EquipmentKind::Stun);
    CHECK(KindOf("smoke_grenade_mp") == EquipmentKind::Smoke);
    CHECK(KindOf("m4_mp") == EquipmentKind::None);
}

TEST(a_class_carries_its_equipment)
{
    char const* text = R"({"version": 2, "weapons": [
        {"id": 2, "name": "m4_mp", "class": "assault_rifle"},
        {"id": 4001, "name": "frag_grenade_mp", "class": "lethal", "slot": "lethal", "mag_size": 1,
         "explosion_radius": 7.11, "explosion_inner": 150, "explosion_outer": 55},
        {"id": 4004, "name": "claymore_mp", "class": "lethal", "slot": "lethal", "unlock_rank": 31},
        {"id": 4101, "name": "flash_grenade_mp", "class": "tactical", "slot": "tactical", "mag_size": 2},
        {"id": 3005, "name": "remotemissile_projectile_mp", "class": "killstreak", "slot": "killstreak"}],
        "attachments": []})";
    Catalog c;
    std::string error;
    CHECK(c.LoadJson(text, error));
    WeaponProfile const* frag = c.Weapons().Find(uint16_t(4001));
    CHECK(frag && frag->weaponClass == WeaponClass::Lethal && frag->slot == WeaponSlot::Lethal);
    CHECK(frag && !FiresRounds(frag->weaponClass));
    WeaponProfile const* missile = c.Weapons().Find(uint16_t(3005));
    CHECK(missile && missile->slot == WeaponSlot::Killstreak && !FiresRounds(missile->weaponClass));

    ClassLoadout l;
    l.lethal = 4001;
    l.tactical = 4101;
    CHECK(ValidateLoadout(c, l, 1, false) == LoadoutError::None);
    l.lethal = 4101;                    // a tactical in the lethal slot
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::WrongSlot);
    l.lethal = 2;                       // a gun
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::WrongSlot);
    l.lethal = 999;
    CHECK(ValidateLoadout(c, l, 60, true) == LoadoutError::UnknownWeapon);
    l.lethal = 4004;                    // the claymore unlocks later
    CHECK(ValidateLoadout(c, l, 1, false) == LoadoutError::Locked);
    l.lethal = 0;                       // none is fine
    l.tactical = 0;
    CHECK(ValidateLoadout(c, l, 1, false) == LoadoutError::None);
}
