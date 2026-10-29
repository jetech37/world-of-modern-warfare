/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Pickups.h"

#include <algorithm>
#include <cstring>

namespace Fusion::Pickups
{
    bool SlotOf(WeaponProfile const& w, Slot& out)
    {
        switch (w.slot)
        {
            case WeaponSlot::Primary: out = Slot::Primary; return true;
            case WeaponSlot::Secondary: out = Slot::Secondary; return true;
            default: return false;
        }
    }

    void NpcAmmo(WeaponProfile const& w, uint32_t& mag, uint32_t& reserve)
    {
        mag = w.magSize;
        reserve = std::min<uint32_t>(uint32_t(w.magSize) * kNpcSpareMagazines, w.maxReserve);
    }

    bool WorthDropping(uint32_t mag, uint32_t reserve)
    {
        return mag + reserve > 0;
    }

    uint32_t Field::Add(Dropped g, uint32_t nowMs)
    {
        g.id = m_nextId++;
        if (!m_nextId)
            m_nextId = 1;
        g.expiresMs = nowMs + kDespawnMs;
        // The oldest on the map makes room.
        size_t onMap = 0;
        for (Dropped const& d : m_guns)
            if (d.mapId == g.mapId && d.instanceId == g.instanceId)
                ++onMap;
        if (onMap >= kMaxPerMap)
        {
            auto oldest = m_guns.end();
            for (auto it = m_guns.begin(); it != m_guns.end(); ++it)
                if (it->mapId == g.mapId && it->instanceId == g.instanceId
                    && (oldest == m_guns.end() || int32_t(it->expiresMs - oldest->expiresMs) < 0))
                    oldest = it;
            if (oldest != m_guns.end())
                m_guns.erase(oldest);
        }
        m_guns.push_back(std::move(g));
        return m_guns.back().id;
    }

    size_t Field::Expire(uint32_t nowMs)
    {
        size_t const before = m_guns.size();
        m_guns.erase(std::remove_if(m_guns.begin(), m_guns.end(),
            [nowMs](Dropped const& d) { return int32_t(nowMs - d.expiresMs) >= 0; }), m_guns.end());
        return before - m_guns.size();
    }

    Field::Take Field::TakeGun(uint32_t id, uint32_t mapId, uint32_t instanceId, Vec3 feet, uint32_t nowMs, Dropped& out)
    {
        Expire(nowMs);
        auto it = std::find_if(m_guns.begin(), m_guns.end(), [id](Dropped const& d) { return d.id == id; });
        if (it == m_guns.end() || it->mapId != mapId || it->instanceId != instanceId)
            return Take::Gone;
        if ((it->pos - feet).Length() > kReach)
            return Take::TooFar;
        out = std::move(*it);
        m_guns.erase(it);
        return Take::Ok;
    }

    std::vector<Dropped> Field::Near(uint32_t mapId, uint32_t instanceId, Vec3 at, float radius, size_t max) const
    {
        std::vector<std::pair<float, Dropped const*>> seen;
        for (Dropped const& d : m_guns)
        {
            if (d.mapId != mapId || d.instanceId != instanceId)
                continue;
            float const dist = (d.pos - at).Length();
            if (dist <= radius)
                seen.push_back({dist, &d});
        }
        std::stable_sort(seen.begin(), seen.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::vector<Dropped> out;
        for (size_t i = 0; i < seen.size() && i < max; ++i)
            out.push_back(*seen[i].second);
        return out;
    }

    Dropped const* Field::Find(uint32_t id) const
    {
        for (Dropped const& d : m_guns)
            if (d.id == id)
                return &d;
        return nullptr;
    }

    Gun const* Hands::For(Slot s) const
    {
        auto const& g = slots[size_t(s)];
        return g ? &*g : nullptr;
    }

    Gun const* Hands::Holding(uint16_t gunId) const
    {
        for (auto const& g : slots)
            if (g && g->id == gunId)
                return &*g;
        return nullptr;
    }

    bool Hands::Any() const
    {
        return slots[0].has_value() || slots[1].has_value();
    }

    void Hands::Clear()
    {
        slots[0].reset();
        slots[1].reset();
    }

    namespace
    {
        void PutU16(std::vector<uint8_t>& b, uint16_t v)
        {
            b.push_back(uint8_t(v));
            b.push_back(uint8_t(v >> 8));
        }
        void PutU32(std::vector<uint8_t>& b, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b.push_back(uint8_t(v >> (8 * i)));
        }
        void PutF32(std::vector<uint8_t>& b, float f)
        {
            uint32_t v;
            std::memcpy(&v, &f, 4);
            PutU32(b, v);
        }
        void PutStr(std::vector<uint8_t>& b, std::string const& s)
        {
            size_t const n = std::min<size_t>(s.size(), 255);
            b.push_back(uint8_t(n));
            b.insert(b.end(), s.begin(), s.begin() + n);
        }

        struct Reader
        {
            std::vector<uint8_t> const& b;
            size_t at = 0;
            bool ok = true;
            bool Need(size_t n)
            {
                ok = ok && at + n <= b.size();
                return ok;
            }
            uint8_t U8() { return Need(1) ? b[at++] : 0; }
            uint16_t U16()
            {
                if (!Need(2))
                    return 0;
                uint16_t v = uint16_t(b[at] | (b[at + 1] << 8));
                at += 2;
                return v;
            }
            uint32_t U32()
            {
                if (!Need(4))
                    return 0;
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(b[at + i]) << (8 * i);
                at += 4;
                return v;
            }
            float F32()
            {
                uint32_t const v = U32();
                float f;
                std::memcpy(&f, &v, 4);
                return f;
            }
            std::string Str()
            {
                size_t const n = U8();
                if (!Need(n))
                    return "";
                std::string s(b.begin() + at, b.begin() + at + n);
                at += n;
                return s;
            }
        };
    }

    std::vector<uint8_t> EncodeGuns(std::vector<Dropped> const& guns)
    {
        size_t const n = std::min<size_t>(guns.size(), kMaxListed);
        std::vector<uint8_t> b{kOpGuns, uint8_t(n)};
        for (size_t i = 0; i < n; ++i)
        {
            Dropped const& d = guns[i];
            PutU32(b, d.id);
            PutU16(b, d.gun.id);
            PutF32(b, d.pos.x);
            PutF32(b, d.pos.y);
            PutF32(b, d.pos.z);
            PutF32(b, d.yaw);
        }
        return b;
    }

    bool DecodeGuns(std::vector<uint8_t> const& body, std::vector<Dropped>& out)
    {
        if (body.size() < 2 || body[0] != kOpGuns || body.size() != 2 + size_t(body[1]) * 22)
            return false;
        Reader r{body, 2};
        out.clear();
        for (uint8_t i = 0; i < body[1]; ++i)
        {
            Dropped d;
            d.id = r.U32();
            d.gun.id = r.U16();
            d.pos.x = r.F32();
            d.pos.y = r.F32();
            d.pos.z = r.F32();
            d.yaw = r.F32();
            out.push_back(d);
        }
        return r.ok;
    }

    std::vector<uint8_t> EncodeTake(uint32_t id)
    {
        std::vector<uint8_t> b{kOpTake};
        PutU32(b, id);
        return b;
    }

    bool DecodeTake(std::vector<uint8_t> const& body, uint32_t& id)
    {
        if (body.size() != 5 || body[0] != kOpTake)
            return false;
        Reader r{body, 1};
        id = r.U32();
        return r.ok;
    }

    std::vector<uint8_t> EncodeHeld(Hands const& h)
    {
        std::vector<uint8_t> b{kOpHeld, 0};
        for (uint8_t s = 0; s < 2; ++s)
        {
            if (!h.slots[s])
                continue;
            ++b[1];
            b.push_back(s);
            PutU16(b, h.slots[s]->id);
            b.push_back(h.slots[s]->camo);
            PutStr(b, h.slots[s]->att1);
            PutStr(b, h.slots[s]->att2);
        }
        return b;
    }

    bool DecodeHeld(std::vector<uint8_t> const& body, Hands& out)
    {
        if (body.size() < 2 || body[0] != kOpHeld)
            return false;
        Reader r{body, 2};
        Hands h;
        for (uint8_t i = 0; i < body[1]; ++i)
        {
            uint8_t const s = r.U8();
            Gun g;
            g.id = r.U16();
            g.camo = r.U8();
            g.att1 = r.Str();
            g.att2 = r.Str();
            if (s > 1)
                return false;
            h.slots[s] = g;
        }
        if (!r.ok || r.at != body.size())
            return false;
        out = h;
        return true;
    }

    std::vector<uint8_t> EncodeRefused(Field::Take why)
    {
        return {kOpRefused, uint8_t(why)};
    }

    uint32_t ListSignature(std::vector<Dropped> const& guns)
    {
        // FNV-1a over the ids and guns, with the count.
        uint32_t h = 2166136261u ^ uint32_t(guns.size());
        auto mix = [&h](uint32_t v) {
            for (int i = 0; i < 4; ++i)
            {
                h ^= (v >> (8 * i)) & 0xFF;
                h *= 16777619u;
            }
        };
        // By id, not by distance: walking about reorders the list without changing it.
        std::vector<uint32_t> ids;
        for (Dropped const& d : guns)
            ids.push_back(d.id);
        std::sort(ids.begin(), ids.end());
        for (uint32_t id : ids)
            mix(id);
        return h;
    }
}
