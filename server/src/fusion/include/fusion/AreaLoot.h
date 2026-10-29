/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_AREALOOT_H
#define FUSION_AREALOOT_H

#include <cstdint>
#include <utility>
#include <vector>

// Area looting, as retail does it: a player out of combat who opens one corpse sees, in the one
// window, what every nearby corpse they may loot holds. The window shows at most 16 rows; what does
// not fit stays on its corpse for the next open. In combat a corpse loots alone.
//
// The game keeps a Session per player while such a window is open. It holds only guids and slots:
// each window row says which corpse it came from and that corpse's own slot, so a take is re-checked
// against the corpse itself and the corpse's loot is never copied.
namespace Fusion::AreaLoot
{
    // The client's loot window holds this many rows.
    constexpr uint8_t kMaxWindowSlots = 16;
    // The search radius (yards) by default and at most (Loot.AreaLoot.Radius).
    constexpr float kDefaultRadius = 30.f;
    constexpr float kMaxRadius = 100.f;
    // A take from another corpse is refused once the player is farther than radius + this.
    constexpr float kTakeSlack = 5.f;

    // A row a corpse offers this player, in the corpse's window order.
    struct Row
    {
        uint64_t source = 0;        // the corpse's guid
        uint8_t sourceSlot = 0;     // its own loot slot
        uint32_t itemId = 0;
    };

    // A row of the combined window.
    struct Entry
    {
        uint64_t source = 0;
        uint8_t sourceSlot = 0;
        uint8_t windowSlot = 0;
        uint32_t itemId = 0;
    };

    // Numbers the rows 0, 1, ... in the order given (the opened corpse's first), skipping a repeat of
    // a (source, slot) already placed, up to maxSlots; the rest stay on their corpses.
    std::vector<Entry> Pack(std::vector<Row> const& rows, uint8_t maxSlots = kMaxWindowSlots);

    // The corpses' coin together, held at the most a uint32 can say.
    uint32_t SumGold(std::vector<uint32_t> const& golds);

    // The radius the config asks, held to [0, kMaxRadius]; 0 turns area looting off.
    float ClampRadius(float radius);

    // A player's open area-loot window.
    struct Session
    {
        uint64_t primary = 0;               // the corpse the player opened: the window's guid
        std::vector<uint64_t> sources;      // every corpse in the window, the primary first
        std::vector<Entry> entries;
        bool takingMoney = false;           // set while this player takes the coin

        bool Active() const { return primary != 0; }
        void Clear();

        bool HasSource(uint64_t source) const;
        Entry const* ByWindowSlot(uint8_t windowSlot) const;
        Entry const* BySource(uint64_t source, uint8_t sourceSlot) const;
        // The row is gone (taken, or the corpse said so).
        void RemoveWindowSlot(uint8_t windowSlot);
        // The corpse leaves the window with its rows; the primary leaves only with Clear.
        void DropSource(uint64_t source);
    };

    // ── The window's source map, for a client that sections the window by corpse ──
    // SMSG_FUSION_CHANNEL (kChannelWorld) op 36, little-endian, sent beside each combined window
    // and again whenever a corpse leaves it:
    //   [36, loot guid u64, n u8, n x (corpse guid u64, creature entry u32),
    //    m u8, m x (window slot u8, source index u8)]
    // The sources come in window order (the opened corpse first, then nearest first), a corpse
    // that adds only coin included; a row names its corpse by index into them. Window slots never
    // renumber while a window is open, so a client keeps each row's place across takes. A client
    // that knows no op 36 shows the flat window.
    constexpr uint8_t kOpSources = 36;

    struct Source
    {
        uint64_t guid = 0;
        uint32_t entry = 0;         // the creature's template, for a client that never saw it
    };

    struct SourceMap
    {
        uint64_t lootGuid = 0;      // the window's guid: the opened corpse
        std::vector<Source> sources;
        std::vector<std::pair<uint8_t, uint8_t>> rows;  // (window slot, index into sources)
    };

    // The map of an open Session; `entryOf` gives a corpse's creature entry (nullptr: 0).
    SourceMap MapOf(Session const& s, uint32_t (*entryOf)(uint64_t));

    std::vector<uint8_t> Encode(SourceMap const& m);
    // False on another op, a row naming no source, or a short or overlong buffer.
    bool Decode(std::vector<uint8_t> const& body, SourceMap& out);
}

#endif
