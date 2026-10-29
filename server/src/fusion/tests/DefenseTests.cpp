/*
 * Azeroth Warfare: tests for sentries and raids (Defense.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Defense.h"
#include "fusion/Build.h"

using namespace Fusion;
using namespace Fusion::Defense;

TEST(a_sentry_shoots_the_nearest_hostile_in_sight)
{
    std::vector<Candidate> cs = {
        {1, {10, 0, 0}, true, true, false},
        {2, {5, 0, 0}, false, true, false},      // friendly: never
        {3, {20, 0, 0}, true, true, false},
        {4, {3, 0, 0}, true, false, false},      // dead
        {5, {50, 0, 0}, true, true, true},       // out of range
    };
    auto all = [](Vec3) { return true; };
    CHECK(SentryTarget({0, 0, 0}, 0.f, cs, all) == std::optional<uint64_t>(1));
    // A wall hides the nearest: the next one.
    auto wall = [](Vec3 p) { return p.x > 15.f; };
    CHECK(SentryTarget({0, 0, 0}, 0.f, cs, wall) == std::optional<uint64_t>(3));
    // One already fighting comes first.
    cs[2].inCombat = true;
    CHECK(SentryTarget({0, 0, 0}, 0.f, cs, all) == std::optional<uint64_t>(3));
    CHECK(!SentryTarget({0, 0, 0}, 0.f, {}, all));
}

TEST(a_sentry_fires_only_the_way_it_faces)
{
    // Facing +x: 80 degrees either side, 40 up or down.
    CHECK(SentryCovers({0, 0, 0}, 0.f, {10, 0, 0}));
    CHECK(SentryCovers({0, 0, 0}, 0.f, {10, 10, 0}));          // 45 degrees off
    CHECK(SentryCovers({0, 0, 0}, 0.f, {2, 10, 0}));           // about 79
    CHECK(!SentryCovers({0, 0, 0}, 0.f, {1, 10, 0}));          // about 84
    CHECK(!SentryCovers({0, 0, 0}, 0.f, {-10, 0, 0}));         // behind
    CHECK(SentryCovers({0, 0, 0}, 0.f, {10, 0, 8}));           // 39 degrees up
    CHECK(!SentryCovers({0, 0, 0}, 0.f, {10, 0, 9}));          // 42 up
    CHECK(!SentryCovers({0, 0, 0}, 0.f, {10, 0, -9}));         // 42 down
    // Facing the other way round the circle.
    CHECK(SentryCovers({0, 0, 0}, 3.1f, {-10, 1, 0}));
    CHECK(SentryCovers({0, 0, 0}, -3.1f, {-10, -1, 0}));
    CHECK(!SentryCovers({0, 0, 0}, 3.1f, {10, 0, 0}));
    // The nearest in its arc, not the nearest behind it.
    std::vector<Candidate> cs = {
        {1, {-3, 0, 0}, true, true, false},
        {2, {12, 2, 0}, true, true, false},
    };
    auto all = [](Vec3) { return true; };
    CHECK(SentryTarget({0, 0, 0}, 0.f, cs, all) == std::optional<uint64_t>(2));
    CHECK(SentryTarget({0, 0, 0}, 3.14159f, cs, all) == std::optional<uint64_t>(1));
    CHECK(!SentryTarget({0, 0, 0}, 1.5708f, cs, all));
}

TEST(a_base_draws_raids_after_a_while)
{
    CHECK(!BaseQualifies(5, 30));
    CHECK(BaseQualifies(8, 30));
    CHECK(!BaseQualifies(30, 3));                 // a new character is left in peace
    CHECK(RaidSize(8) == 3 && RaidSize(40) == 6 && RaidSize(500) == 8);
    CHECK(NextRaidDelayMs(0.f) == kRaidMinMs && NextRaidDelayMs(1.f) == kRaidMaxMs);

    RaidClock clock;
    uint32_t now = 1000;
    CHECK(Advance(clock, now, true, 0, 0.5f) == RaidStep::None);             // the wait begins
    CHECK(Advance(clock, now + kFirstRaidMs - 1, true, 0, 0.5f) == RaidStep::None);
    now += kFirstRaidMs;
    CHECK(Advance(clock, now, true, 0, 0.5f) == RaidStep::Warn);
    CHECK(Advance(clock, now + 1000, true, 0, 0.5f) == RaidStep::None);
    now += kRaidWarningMs;
    CHECK(Advance(clock, now, true, 0, 0.5f) == RaidStep::Spawn);
    CHECK(Advance(clock, now + 1000, true, 4, 0.5f) == RaidStep::None);       // still fighting
    CHECK(Advance(clock, now + 2000, true, 0, 0.5f) == RaidStep::End);        // all down
    CHECK(clock.phase == RaidPhase::Quiet);
    // Taking the base down stops the clock; building again starts the first wait over.
    CHECK(Advance(clock, now + 3000, false, 0, 0.5f) == RaidStep::None);
    CHECK(!clock.armed);
}

TEST(a_raid_gives_up_in_time)
{
    RaidClock clock;
    clock.phase = RaidPhase::Fighting;
    clock.nextMs = 5000;
    CHECK(Advance(clock, 4999, true, 3, 0.f) == RaidStep::None);
    CHECK(Advance(clock, 5000, true, 3, 0.f) == RaidStep::End);
}

TEST(sentries_and_bedrolls_are_pieces)
{
    using namespace Fusion::Build;
    CHECK(Def(PieceType::SentryGun).cost.item == kCopperBar);
    CHECK(Def(PieceType::Bedroll).groundOnly);
    CHECK(uint8_t(PieceType::Count) == 18);
    std::vector<uint8_t> const b = EncodeSentryFire(9, 0x1122334455667788ull, true);
    CHECK(b.size() == 1 + 4 + 8 + 1 && b[0] == uint8_t(ServerOp::SentryFire) && b[5] == 0x88 && b[12] == 0x11 && b[13] == 1);
}

TEST(a_door_opens_and_shuts)
{
    using namespace Fusion::Build;
    Piece door;
    door.id = 1;
    door.type = PieceType::WoodDoor;
    float t = 0.f;
    CHECK(RayHits(door, {-10, 0, 1.f}, {1, 0, 0}, 50.f, t));      // shut: the panel stops it
    door.open = true;
    CHECK(!RayHits(door, {-10, 0.2f, 1.f}, {1, 0, 0}, 50.f, t));  // open: through
    // Open, not ours (the high bits are its lock, Locks.h).
    CHECK((EncodePieces({door}, 99)[3 + 4 + 1 + 16 + 2 + 1] & 0x0F) == kPieceOpen);
    uint32_t id = 0;
    CHECK(DecodeToggle({uint8_t(ClientOp::Toggle), 9, 0, 0, 0}, id) && id == 9);
}
