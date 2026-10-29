/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Packets.h"

#include <algorithm>

#include <cstring>

namespace Fusion::Packets
{
    namespace
    {
        struct Writer
        {
            std::vector<uint8_t> out;

            void U8(uint8_t v) { out.push_back(v); }
            void U16(uint16_t v)
            {
                for (int i = 0; i < 2; ++i)
                    out.push_back(uint8_t(v >> (8 * i)));
            }
            void U32(uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    out.push_back(uint8_t(v >> (8 * i)));
            }
            void U64(uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                    out.push_back(uint8_t(v >> (8 * i)));
            }
            void F32(float f)
            {
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                U32(bits);
            }
            void Str(std::string const& s)
            {
                size_t const n = std::min<size_t>(s.size(), 255);
                U8(uint8_t(n));
                out.insert(out.end(), s.begin(), s.begin() + n);
            }
            void V3(Vec3 v)
            {
                F32(v.x);
                F32(v.y);
                F32(v.z);
            }
        };

        struct Reader
        {
            std::vector<uint8_t> const& in;
            size_t pos = 0;
            bool ok = true;

            bool Need(size_t n)
            {
                if (pos + n > in.size())
                    ok = false;
                return ok;
            }
            uint8_t U8()
            {
                if (!Need(1))
                    return 0;
                return in[pos++];
            }
            uint16_t U16()
            {
                if (!Need(2))
                    return 0;
                uint16_t v = uint16_t(in[pos] | (in[pos + 1] << 8));
                pos += 2;
                return v;
            }
            uint32_t U32()
            {
                if (!Need(4))
                    return 0;
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(in[pos + i]) << (8 * i);
                pos += 4;
                return v;
            }
            uint64_t U64()
            {
                if (!Need(8))
                    return 0;
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= uint64_t(in[pos + i]) << (8 * i);
                pos += 8;
                return v;
            }
            float F32()
            {
                uint32_t bits = U32();
                float f;
                std::memcpy(&f, &bits, 4);
                return f;
            }
            Vec3 V3()
            {
                float x = F32();
                float y = F32();
                float z = F32();
                return {x, y, z};
            }
            std::string Str(size_t max)
            {
                size_t const n = U8();
                if (n > max)
                    ok = false;
                if (!ok || !Need(n))
                    return {};
                std::string s(in.begin() + pos, in.begin() + pos + n);
                pos += n;
                return s;
            }
            bool Done() const { return ok && pos == in.size(); }
        };

        void WriteClasses(Writer& w, std::vector<std::pair<uint8_t, ClassLoadout>> const& classes)
        {
            size_t const n = std::min(classes.size(), kClassSlots);
            w.U8(uint8_t(n));
            for (size_t i = 0; i < n; ++i)
            {
                ClassLoadout const& l = classes[i].second;
                w.U8(classes[i].first);
                w.Str(l.name);
                w.U16(l.primary);
                w.Str(l.primaryAtt1);
                w.Str(l.primaryAtt2);
                w.U8(l.primaryCamo);
                w.U16(l.secondary);
                w.Str(l.secondaryAtt);
                w.Str(l.secondaryAtt2);
                for (uint16_t v : {l.lethal, l.tactical, l.perk1, l.perk2, l.perk3, l.deathstreak, l.streak1, l.streak2, l.streak3})
                    w.U16(v);
                for (uint16_t v : l.talentPicks)
                    w.U16(v);
                w.Str(l.primaryAtt3);
                w.U8(l.perkPro);
                w.Str(l.primaryAtt4);
            }
        }

        bool ReadClasses(Reader& r, std::vector<std::pair<uint8_t, ClassLoadout>>& classes)
        {
            uint8_t const n = r.U8();
            if (n > kClassSlots)
                return false;
            classes.clear();
            for (uint8_t i = 0; i < n && r.ok; ++i)
            {
                uint8_t const slot = r.U8();
                if (slot >= kClassSlots)
                    return false;
                ClassLoadout l;
                l.name = r.Str(kClassNameMax);
                l.primary = r.U16();
                l.primaryAtt1 = r.Str(16);
                l.primaryAtt2 = r.Str(16);
                l.primaryCamo = r.U8();
                l.secondary = r.U16();
                l.secondaryAtt = r.Str(16);
                l.secondaryAtt2 = r.Str(16);
                for (uint16_t* v : {&l.lethal, &l.tactical, &l.perk1, &l.perk2, &l.perk3, &l.deathstreak, &l.streak1, &l.streak2, &l.streak3})
                    *v = r.U16();
                for (uint16_t& v : l.talentPicks)
                    v = r.U16();
                l.primaryAtt3 = r.Str(16);
                l.perkPro = r.U8();
                l.primaryAtt4 = r.Str(16);
                classes.push_back({slot, std::move(l)});
            }
            return r.ok;
        }

        bool FiniteVec(Vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    }

    std::vector<uint8_t> Encode(ShotBatch const& m)
    {
        Writer w;
        w.U8(kShotsVersion);
        size_t n = std::min<size_t>(m.shots.size(), kMaxShotsPerBatch);
        w.U8(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Shot const& s = m.shots[i];
            w.U32(s.timeMs);
            w.U16(s.weaponId);
            w.U8(s.flags);
            w.U8(uint8_t(s.stance));
            w.U64(s.targetGuid);
            w.V3(s.origin);
            w.V3(s.direction);
            w.U32(s.seed);
            w.F32(s.spreadDeg);
        }
        return w.out;
    }

    std::vector<uint8_t> Encode(State const& m)
    {
        Writer w;
        w.U8(kVersion);
        w.U8(uint8_t(m.stance));
        w.U8(uint8_t(m.gait));
        w.U8(m.flags);
        w.U16(m.heldWeaponId);
        w.U8(m.loadedAmmo);
        return w.out;
    }

    std::vector<uint8_t> Encode(Events const& m)
    {
        Writer w;
        w.U8(kVersion);
        size_t n = std::min<size_t>(m.events.size(), kMaxEventsPerPacket);
        w.U8(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Event const& e = m.events[i];
            w.U8(uint8_t(e.kind));
            w.U8(e.flags);
            w.U64(e.guid);
            w.U32(e.amount);
            w.U16(e.extra);
        }
        return w.out;
    }

    bool Decode(std::vector<uint8_t> const& body, ShotBatch& out)
    {
        Reader r{body};
        out.shots.clear();
        if (r.U8() != kShotsVersion)
            return false;
        uint8_t n = r.U8();
        if (!r.ok || n > kMaxShotsPerBatch)
            return false;
        for (uint8_t i = 0; i < n; ++i)
        {
            Shot s;
            s.timeMs = r.U32();
            s.weaponId = r.U16();
            s.flags = r.U8();
            uint8_t stance = r.U8();
            s.targetGuid = r.U64();
            s.origin = r.V3();
            s.direction = r.V3();
            s.seed = r.U32();
            s.spreadDeg = r.F32();
            if (!r.ok || stance > uint8_t(Stance::Slide) || !FiniteVec(s.origin) || !FiniteVec(s.direction))
                return false;
            s.stance = Stance(stance);
            out.shots.push_back(s);
        }
        return r.Done();
    }

    bool Decode(std::vector<uint8_t> const& body, State& out)
    {
        Reader r{body};
        if (r.U8() != kVersion)
            return false;
        uint8_t stance = r.U8();
        uint8_t gait = r.U8();
        out.flags = r.U8();
        out.heldWeaponId = r.U16();
        out.loadedAmmo = r.U8();
        if (!r.Done() || stance > uint8_t(Stance::Slide) || gait > uint8_t(Gait::Backpedal))
            return false;
        out.stance = Stance(stance);
        out.gait = Gait(gait);
        return true;
    }

    bool Decode(std::vector<uint8_t> const& body, Events& out)
    {
        Reader r{body};
        out.events.clear();
        if (r.U8() != kVersion)
            return false;
        uint8_t n = r.U8();
        if (!r.ok || n > kMaxEventsPerPacket)
            return false;
        for (uint8_t i = 0; i < n; ++i)
        {
            Event e;
            uint8_t kind = r.U8();
            e.flags = r.U8();
            e.guid = r.U64();
            e.amount = r.U32();
            e.extra = r.U16();
            if (!r.ok || kind > uint8_t(EventKind::Struck))
                return false;
            e.kind = EventKind(kind);
            out.events.push_back(e);
        }
        return r.Done();
    }

    std::string ToHex(std::vector<uint8_t> const& bytes)
    {
        static char const digits[] = "0123456789abcdef";
        std::string s;
        s.reserve(bytes.size() * 2);
        for (uint8_t b : bytes)
        {
            s.push_back(digits[b >> 4]);
            s.push_back(digits[b & 15]);
        }
        return s;
    }

    std::vector<uint8_t> FromHex(std::string const& hex)
    {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::vector<uint8_t> out;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            int hi = nib(hex[i]), lo = nib(hex[i + 1]);
            if (hi < 0 || lo < 0)
                return {};
            out.push_back(uint8_t(hi << 4 | lo));
        }
        return out;
    }

    std::vector<uint8_t> Encode(LoadoutRequest const& m)
    {
        Writer w;
        w.U8(kLoadoutVersion);
        w.U8(m.equipSlot);
        WriteClasses(w, m.classes);
        return w.out;
    }

    std::vector<uint8_t> Encode(LoadoutReply const& m)
    {
        Writer w;
        w.U8(kLoadoutVersion);
        w.U8(uint8_t(m.status));
        w.U8(uint8_t(m.error));
        w.U8(m.errorSlot);
        w.U8(m.equippedSlot);
        WriteClasses(w, m.classes);
        return w.out;
    }

    bool Decode(std::vector<uint8_t> const& body, LoadoutRequest& out)
    {
        Reader r{body};
        if (r.U8() != kLoadoutVersion)
            return false;
        out.equipSlot = r.U8();
        if (out.equipSlot >= kClassSlots && out.equipSlot != kNoClass)
            return false;
        return ReadClasses(r, out.classes) && r.Done();
    }

    bool Decode(std::vector<uint8_t> const& body, LoadoutReply& out)
    {
        Reader r{body};
        if (r.U8() != kLoadoutVersion)
            return false;
        uint8_t const status = r.U8();
        uint8_t const error = r.U8();
        if (status > uint8_t(LoadoutStatus::Rejected) || error > uint8_t(LoadoutError::ProLocked))
            return false;
        out.status = LoadoutStatus(status);
        out.error = LoadoutError(error);
        out.errorSlot = r.U8();
        out.equippedSlot = r.U8();
        return ReadClasses(r, out.classes) && r.Done();
    }

    std::vector<uint8_t> Encode(ChannelMessage const& m)
    {
        size_t const len = std::min(m.body.size(), kMaxChannelBody);
        std::vector<uint8_t> out{kVersion, m.channel, uint8_t(len), uint8_t(len >> 8)};
        out.insert(out.end(), m.body.begin(), m.body.begin() + std::ptrdiff_t(len));
        return out;
    }

    bool Decode(std::vector<uint8_t> const& body, ChannelMessage& out)
    {
        if (body.size() < 4 || body[0] != kVersion)
            return false;
        size_t const len = size_t(body[2]) | size_t(body[3]) << 8;
        if (len > kMaxChannelBody || body.size() != 4 + len)
            return false;
        out.channel = body[1];
        out.body.assign(body.begin() + 4, body.end());
        return true;
    }
}

