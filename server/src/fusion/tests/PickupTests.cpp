/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Ammo.h"
#include "fusion/Packets.h"
#include "fusion/Pickups.h"

using namespace Fusion;
using namespace Fusion::Pickups;

namespace
{
    WeaponProfile Profile(uint16_t id, WeaponClass c, uint16_t mag, uint16_t reserve)
    {
        WeaponProfile w;
        w.id = id;
        w.weaponClass = c;
        w.slot = DefaultSlot(c);
        w.magSize = mag;
        w.maxReserve = reserve;
        return w;
    }

    Dropped At(uint16_t gun, Vec3 pos, uint32_t map = 0, uint32_t instance = 0)
    {
        Dropped d;
        d.gun.id = gun;
        d.pos = pos;
        d.mapId = map;
        d.instanceId = instance;
        d.mag = 30;
        d.reserve = 60;
        return d;
    }
}

TEST(a_pickup_takes_the_slot_of_its_class)
{
    Slot s = Slot::Secondary;
    CHECK(SlotOf(Profile(2, WeaponClass::AssaultRifle, 30, 180), s) && s == Slot::Primary);
    CHECK(SlotOf(Profile(21, WeaponClass::Sniper, 10, 60), s) && s == Slot::Primary);
    CHECK(SlotOf(Profile(24, WeaponClass::Shotgun, 8, 64), s) && s == Slot::Secondary);
    CHECK(SlotOf(Profile(30, WeaponClass::Pistol, 12, 72), s) && s == Slot::Secondary);
    CHECK(SlotOf(Profile(42, WeaponClass::Launcher, 1, 2), s) && s == Slot::Secondary);
    // Equipment and killstreaks are never picked up.
    CHECK(!SlotOf(Profile(4001, WeaponClass::Lethal, 1, 0), s));
    CHECK(!SlotOf(Profile(3001, WeaponClass::Killstreak, 1, 0), s));
    WeaponProfile alt = Profile(1002, WeaponClass::Launcher, 1, 1);
    alt.slot = WeaponSlot::Alternate;
    CHECK(!SlotOf(alt, s));
}

TEST(an_npc_gun_comes_with_a_magazine_and_spares)
{
    uint32_t mag = 0, reserve = 0;
    NpcAmmo(Profile(2, WeaponClass::AssaultRifle, 30, 180), mag, reserve);
    CHECK(mag == 30 && reserve == 60);
    // Never more than the gun carries: a rocket tube's one spare.
    NpcAmmo(Profile(42, WeaponClass::Launcher, 1, 1), mag, reserve);
    CHECK(mag == 1 && reserve == 1);
    CHECK(WorthDropping(0, 5) && WorthDropping(3, 0) && !WorthDropping(0, 0));
}

TEST(a_picked_up_gun_keeps_the_rounds_it_had)
{
    GunAmmo a;
    a.Reset(Profile(2, WeaponClass::AssaultRifle, 30, 180));
    a.Set(12, 40);
    CHECK(a.Mag() == 12 && a.Reserve() == 40);
    // Never past what the gun holds.
    a.Set(99, 999);
    CHECK(a.Mag() == 30 && a.Reserve() == 180);
}

TEST(a_dropped_gun_lies_until_its_time_is_up)
{
    Field f;
    uint32_t const id = f.Add(At(2, {10.f, 10.f, 0.f}), 1000);
    CHECK(id != 0 && f.Size() == 1);
    CHECK(f.Find(id) && f.Find(id)->expiresMs == 1000 + kDespawnMs);
    CHECK(f.Expire(1000 + kDespawnMs - 1) == 0);
    CHECK(f.Expire(1000 + kDespawnMs) == 1 && f.Size() == 0);
    // Between 60 and 90 seconds, as MW2's.
    CHECK(kDespawnMs >= 60000 && kDespawnMs <= 90000);
}

TEST(a_pickup_needs_the_gun_within_reach_on_the_same_map)
{
    Field f;
    uint32_t const id = f.Add(At(2, {10.f, 10.f, 0.f}, 1, 7), 0);
    Dropped got;
    CHECK(f.TakeGun(id, 0, 7, {10.f, 10.f, 0.f}, 10, got) == Field::Take::Gone);     // another map
    CHECK(f.TakeGun(id, 1, 8, {10.f, 10.f, 0.f}, 10, got) == Field::Take::Gone);     // another instance
    CHECK(f.TakeGun(id, 1, 7, {10.f, 20.f, 0.f}, 10, got) == Field::Take::TooFar);
    CHECK(f.Size() == 1);
    CHECK(f.TakeGun(id, 1, 7, {12.f, 11.f, 0.5f}, 10, got) == Field::Take::Ok);
    CHECK(got.id == id && got.gun.id == 2 && got.mag == 30 && got.reserve == 60);
    // Taken: nobody else gets it.
    CHECK(f.TakeGun(id, 1, 7, {10.f, 10.f, 0.f}, 10, got) == Field::Take::Gone);
    // An expired gun is gone too.
    uint32_t const old = f.Add(At(2, {0.f, 0.f, 0.f}), 0);
    CHECK(f.TakeGun(old, 0, 0, {0.f, 0.f, 0.f}, kDespawnMs + 5, got) == Field::Take::Gone);
}

TEST(a_crowded_map_drops_its_oldest_gun)
{
    Field f;
    uint32_t first = 0;
    for (size_t i = 0; i < kMaxPerMap; ++i)
    {
        uint32_t const id = f.Add(At(2, {float(i), 0.f, 0.f}), uint32_t(i * 10));
        if (!first)
            first = id;
    }
    f.Add(At(30, {0.f, 0.f, 0.f}, 1), 5000);     // another map does not count
    CHECK(f.Size() == kMaxPerMap + 1);
    f.Add(At(2, {99.f, 0.f, 0.f}), 6000);
    CHECK(f.Size() == kMaxPerMap + 1 && !f.Find(first));
}

TEST(guns_near_a_player_come_nearest_first)
{
    Field f;
    uint32_t const far = f.Add(At(2, {30.f, 0.f, 0.f}), 0);
    uint32_t const near = f.Add(At(30, {5.f, 0.f, 0.f}), 0);
    f.Add(At(2, {500.f, 0.f, 0.f}), 0);
    f.Add(At(2, {1.f, 0.f, 0.f}, 1), 0);           // another map
    std::vector<Dropped> const seen = f.Near(0, 0, {0.f, 0.f, 0.f}, kTellRadius, kMaxListed);
    CHECK(seen.size() == 2 && seen[0].id == near && seen[1].id == far);
    CHECK(f.Near(0, 0, {0.f, 0.f, 0.f}, kTellRadius, 1).size() == 1);
    // The signature ignores the order, not the content.
    std::vector<Dropped> swapped = {seen[1], seen[0]};
    CHECK(ListSignature(seen) == ListSignature(swapped));
    CHECK(ListSignature(seen) != ListSignature({seen[0]}));
    CHECK(ListSignature({}) != ListSignature({seen[0]}));
}

TEST(hands_hold_a_pickup_per_slot_and_clear_together)
{
    Hands h;
    CHECK(!h.Any() && !h.For(Slot::Primary));
    h.slots[size_t(Slot::Primary)] = Pickups::Gun{21, "acog", "", 2};
    CHECK(h.Any() && h.For(Slot::Primary) && h.For(Slot::Primary)->id == 21);
    CHECK(h.Holding(21) && !h.Holding(30));
    h.slots[size_t(Slot::Secondary)] = Pickups::Gun{30, "", "", 0};
    CHECK(h.Holding(30));
    h.Clear();
    CHECK(!h.Any() && !h.Holding(21));
}

TEST(pickup_messages_encode_and_decode)
{
    Dropped d = At(21, {-8949.5f, -132.25f, 83.5f});
    d.id = 0x01020304;
    d.yaw = 1.5f;
    std::vector<uint8_t> const list = EncodeGuns({d});
    // Frozen: the client (crate::pickups) decodes these exact bytes.
    CHECK(Packets::ToHex(list) == "3c0104030201150000d60bc6004004c30000a7420000c03f");
    std::vector<Dropped> back;
    CHECK(DecodeGuns(list, back) && back.size() == 1 && back[0].id == d.id && back[0].gun.id == 21
        && back[0].pos.x == d.pos.x && back[0].yaw == 1.5f);
    CHECK(DecodeGuns({kOpGuns, 0}, back) && back.empty());
    CHECK(!DecodeGuns({kOpGuns, 1, 0}, back));

    uint32_t id = 0;
    CHECK(Packets::ToHex(EncodeTake(0x01020304)) == "3d04030201");
    CHECK(DecodeTake(EncodeTake(77), id) && id == 77);
    CHECK(!DecodeTake({kOpTake, 1, 2}, id));

    Hands h;
    h.slots[0] = Pickups::Gun{2, "acog", "gl", 1};
    std::vector<uint8_t> const held = EncodeHeld(h);
    CHECK(Packets::ToHex(held) == "3e01000200010461636f6702676c");
    Hands h2;
    CHECK(DecodeHeld(held, h2) && h2.For(Slot::Primary) && *h2.For(Slot::Primary) == *h.For(Slot::Primary)
        && !h2.For(Slot::Secondary));
    CHECK(DecodeHeld({kOpHeld, 0}, h2) && !h2.Any());
    CHECK(Packets::ToHex(EncodeRefused(Field::Take::TooFar)) == "3f02");
}
