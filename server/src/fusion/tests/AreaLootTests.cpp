/*
 * Azeroth Warfare: tests for area looting's window (AreaLoot.h). GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/AreaLoot.h"

using namespace Fusion::AreaLoot;

namespace
{
    Row R(uint64_t source, uint8_t slot, uint32_t item) { return Row{source, slot, item}; }
}

TEST(area_loot_numbers_the_opened_corpse_first_then_the_others)
{
    std::vector<Row> rows = {R(10, 0, 100), R(10, 3, 101), R(20, 1, 200), R(30, 0, 300)};
    std::vector<Entry> e = Pack(rows);
    CHECK(e.size() == 4);
    CHECK(e[0].source == 10 && e[0].sourceSlot == 0 && e[0].windowSlot == 0 && e[0].itemId == 100);
    CHECK(e[1].source == 10 && e[1].sourceSlot == 3 && e[1].windowSlot == 1);
    CHECK(e[2].source == 20 && e[2].sourceSlot == 1 && e[2].windowSlot == 2 && e[2].itemId == 200);
    CHECK(e[3].source == 30 && e[3].windowSlot == 3);
}

TEST(area_loot_leaves_what_does_not_fit_on_its_corpse)
{
    std::vector<Row> rows;
    for (uint8_t i = 0; i < 10; ++i)
        rows.push_back(R(1, i, 1000 + i));
    for (uint8_t i = 0; i < 10; ++i)
        rows.push_back(R(2, i, 2000 + i));
    std::vector<Entry> e = Pack(rows);
    CHECK(e.size() == kMaxWindowSlots);
    CHECK(e.back().windowSlot == kMaxWindowSlots - 1);
    CHECK(e.back().source == 2 && e.back().sourceSlot == 5);
    // A smaller window asked is honoured, a larger one is not.
    CHECK(Pack(rows, 4).size() == 4);
    CHECK(Pack(rows, 200).size() == kMaxWindowSlots);
}

TEST(area_loot_places_a_corpse_slot_once)
{
    std::vector<Row> rows = {R(1, 2, 5), R(1, 2, 5), R(1, 4, 6)};
    std::vector<Entry> e = Pack(rows);
    CHECK(e.size() == 2);
    CHECK(e[1].sourceSlot == 4 && e[1].windowSlot == 1);
    // The same slot number on another corpse is another row.
    CHECK(Pack({R(1, 0, 5), R(2, 0, 5)}).size() == 2);
}

TEST(area_loot_gold_adds_up_and_never_wraps)
{
    CHECK(SumGold({}) == 0);
    CHECK(SumGold({10, 20, 30}) == 60);
    CHECK(SumGold({0xFFFFFFF0u, 0x100u}) == 0xFFFFFFFFu);
}

TEST(area_loot_radius_is_held_to_its_range)
{
    CHECK(ClampRadius(30.f) == 30.f);
    CHECK(ClampRadius(-5.f) == 0.f);
    CHECK(ClampRadius(0.f) == 0.f);
    CHECK(ClampRadius(500.f) == kMaxRadius);
}

TEST(area_loot_session_maps_both_ways_and_forgets_a_corpse)
{
    Session s;
    CHECK(!s.Active());
    s.primary = 10;
    s.sources = {10, 20};
    s.entries = Pack({R(10, 0, 100), R(20, 2, 200), R(20, 5, 201)});
    CHECK(s.Active());
    CHECK(s.HasSource(20) && !s.HasSource(30) && !s.HasSource(0));

    Entry const* e = s.ByWindowSlot(1);
    CHECK(e && e->source == 20 && e->sourceSlot == 2);
    e = s.BySource(20, 5);
    CHECK(e && e->windowSlot == 2);
    CHECK(s.BySource(20, 3) == nullptr);
    CHECK(s.ByWindowSlot(7) == nullptr);

    // A taken row goes; the other window slots keep their numbers.
    s.RemoveWindowSlot(1);
    CHECK(s.ByWindowSlot(1) == nullptr);
    CHECK(s.ByWindowSlot(2) && s.ByWindowSlot(2)->sourceSlot == 5);

    // The primary cannot be dropped; another corpse goes with its rows.
    s.DropSource(10);
    CHECK(s.HasSource(10));
    s.DropSource(20);
    CHECK(!s.HasSource(20) && s.entries.size() == 1);

    s.takingMoney = true;
    s.Clear();
    CHECK(!s.Active() && s.sources.empty() && s.entries.empty() && !s.takingMoney);
}

TEST(area_loot_source_map_lists_every_corpse_and_each_rows_corpse)
{
    Session s;
    s.primary = 10;
    s.sources = {10, 20, 30};       // 30 adds only coin
    s.entries = Pack({R(10, 0, 100), R(20, 2, 200), R(20, 5, 201)});
    SourceMap m = MapOf(s, [](uint64_t guid) { return uint32_t(guid * 100); });
    CHECK(m.lootGuid == 10);
    CHECK(m.sources.size() == 3);
    CHECK(m.sources[0].guid == 10 && m.sources[0].entry == 1000);
    CHECK(m.sources[2].guid == 30 && m.sources[2].entry == 3000);
    CHECK(m.rows.size() == 3);
    CHECK(m.rows[0] == std::make_pair(uint8_t(0), uint8_t(0)));
    CHECK(m.rows[1] == std::make_pair(uint8_t(1), uint8_t(1)));
    CHECK(m.rows[2] == std::make_pair(uint8_t(2), uint8_t(1)));

    // A corpse that leaves takes its rows out of the map; the others keep their window slots.
    s.RemoveWindowSlot(0);
    s.DropSource(30);
    m = MapOf(s, nullptr);
    CHECK(m.sources.size() == 2 && m.sources[1].entry == 0);
    CHECK(m.rows.size() == 2 && m.rows[0].first == 1 && m.rows[1].first == 2);
}
