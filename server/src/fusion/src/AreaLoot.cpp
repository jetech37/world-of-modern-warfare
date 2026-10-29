/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/AreaLoot.h"

#include <algorithm>
#include <limits>

namespace Fusion::AreaLoot
{
    std::vector<Entry> Pack(std::vector<Row> const& rows, uint8_t maxSlots)
    {
        if (maxSlots > kMaxWindowSlots)
            maxSlots = kMaxWindowSlots;

        std::vector<Entry> out;
        out.reserve(maxSlots);
        for (Row const& r : rows)
        {
            if (out.size() >= maxSlots)
                break;
            bool repeat = std::any_of(out.begin(), out.end(), [&r](Entry const& e)
                { return e.source == r.source && e.sourceSlot == r.sourceSlot; });
            if (repeat)
                continue;
            Entry e;
            e.source = r.source;
            e.sourceSlot = r.sourceSlot;
            e.windowSlot = uint8_t(out.size());
            e.itemId = r.itemId;
            out.push_back(e);
        }
        return out;
    }

    uint32_t SumGold(std::vector<uint32_t> const& golds)
    {
        uint64_t sum = 0;
        for (uint32_t g : golds)
            sum += g;
        uint64_t const cap = std::numeric_limits<uint32_t>::max();
        return uint32_t(sum > cap ? cap : sum);
    }

    float ClampRadius(float radius)
    {
        if (!(radius > 0.f))        // also NaN
            return 0.f;
        return radius > kMaxRadius ? kMaxRadius : radius;
    }

    void Session::Clear()
    {
        primary = 0;
        sources.clear();
        entries.clear();
        takingMoney = false;
    }

    bool Session::HasSource(uint64_t source) const
    {
        return source && std::find(sources.begin(), sources.end(), source) != sources.end();
    }

    Entry const* Session::ByWindowSlot(uint8_t windowSlot) const
    {
        for (Entry const& e : entries)
            if (e.windowSlot == windowSlot)
                return &e;
        return nullptr;
    }

    Entry const* Session::BySource(uint64_t source, uint8_t sourceSlot) const
    {
        for (Entry const& e : entries)
            if (e.source == source && e.sourceSlot == sourceSlot)
                return &e;
        return nullptr;
    }

    void Session::RemoveWindowSlot(uint8_t windowSlot)
    {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [windowSlot](Entry const& e) { return e.windowSlot == windowSlot; }), entries.end());
    }

    void Session::DropSource(uint64_t source)
    {
        if (!source || source == primary)
            return;
        sources.erase(std::remove(sources.begin(), sources.end(), source), sources.end());
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [source](Entry const& e) { return e.source == source; }), entries.end());
    }

    SourceMap MapOf(Session const& s, uint32_t (*entryOf)(uint64_t))
    {
        SourceMap m;
        m.lootGuid = s.primary;
        for (uint64_t guid : s.sources)
        {
            if (m.sources.size() >= 255)
                break;
            m.sources.push_back(Source{guid, entryOf ? entryOf(guid) : 0});
        }
        for (Entry const& e : s.entries)
        {
            for (size_t i = 0; i < m.sources.size(); ++i)
            {
                if (m.sources[i].guid != e.source)
                    continue;
                m.rows.emplace_back(e.windowSlot, uint8_t(i));
                break;
            }
        }
        return m;
    }

    namespace
    {
        void Put(std::vector<uint8_t>& out, uint64_t v, size_t bytes)
        {
            for (size_t i = 0; i < bytes; ++i)
                out.push_back(uint8_t(v >> (8 * i)));
        }

        bool Get(std::vector<uint8_t> const& in, size_t& at, size_t bytes, uint64_t& v)
        {
            if (at > in.size() || in.size() - at < bytes)
                return false;
            v = 0;
            for (size_t i = 0; i < bytes; ++i)
                v |= uint64_t(in[at + i]) << (8 * i);
            at += bytes;
            return true;
        }
    }

    std::vector<uint8_t> Encode(SourceMap const& m)
    {
        size_t const n = m.sources.size() > 255 ? 255 : m.sources.size();
        std::vector<std::pair<uint8_t, uint8_t>> rows;
        for (auto const& r : m.rows)
            if (r.second < n && rows.size() < 255)
                rows.push_back(r);

        std::vector<uint8_t> out;
        out.reserve(11 + 12 * n + 2 * rows.size());
        out.push_back(kOpSources);
        Put(out, m.lootGuid, 8);
        out.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Put(out, m.sources[i].guid, 8);
            Put(out, m.sources[i].entry, 4);
        }
        out.push_back(uint8_t(rows.size()));
        for (auto const& r : rows)
        {
            out.push_back(r.first);
            out.push_back(r.second);
        }
        return out;
    }

    bool Decode(std::vector<uint8_t> const& body, SourceMap& out)
    {
        out = SourceMap();
        size_t at = 0;
        uint64_t v = 0;
        if (!Get(body, at, 1, v) || v != kOpSources)
            return false;
        if (!Get(body, at, 8, v))
            return false;
        out.lootGuid = v;
        if (!Get(body, at, 1, v))
            return false;
        size_t const n = size_t(v);
        for (size_t i = 0; i < n; ++i)
        {
            Source src;
            if (!Get(body, at, 8, v))
                return false;
            src.guid = v;
            if (!Get(body, at, 4, v))
                return false;
            src.entry = uint32_t(v);
            out.sources.push_back(src);
        }
        if (!Get(body, at, 1, v))
            return false;
        size_t const m = size_t(v);
        for (size_t i = 0; i < m; ++i)
        {
            uint64_t slot = 0, index = 0;
            if (!Get(body, at, 1, slot) || !Get(body, at, 1, index) || index >= n)
                return false;
            out.rows.emplace_back(uint8_t(slot), uint8_t(index));
        }
        return at == body.size();
    }
}
