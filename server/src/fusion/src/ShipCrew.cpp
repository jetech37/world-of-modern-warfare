/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/ShipCrew.h"

#include <algorithm>
#include <cmath>

namespace Fusion::Ships
{
    namespace
    {
        void PutU32(std::vector<uint8_t>& o, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }
        void PutU64(std::vector<uint8_t>& o, uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }
        uint32_t U32(std::vector<uint8_t> const& b, size_t at)
        {
            return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
        }
        void PutLines(std::vector<uint8_t>& o, std::vector<CargoLine> const& v)
        {
            size_t const n = std::min<size_t>(v.size(), 64);
            o.push_back(uint8_t(n));
            for (size_t i = 0; i < n; ++i)
            {
                PutU32(o, v[i].entry);
                PutU32(o, v[i].count);
                size_t const len = std::min<size_t>(v[i].name.size(), 64);
                o.push_back(uint8_t(len));
                o.insert(o.end(), v[i].name.begin(), v[i].name.begin() + len);
            }
        }
        bool GetLines(std::vector<uint8_t> const& b, size_t& at, std::vector<CargoLine>& out)
        {
            if (at >= b.size())
                return false;
            size_t const n = b[at++];
            out.clear();
            for (size_t i = 0; i < n; ++i)
            {
                if (at + 9 > b.size())
                    return false;
                CargoLine l;
                l.entry = U32(b, at);
                l.count = U32(b, at + 4);
                size_t const len = b[at + 8];
                at += 9;
                if (at + len > b.size())
                    return false;
                l.name.assign(b.begin() + at, b.begin() + at + len);
                at += len;
                out.push_back(std::move(l));
            }
            return true;
        }
        // A point in the world into the ship's frame.
        Vec3 ToLocal(Vec3 p, float sx, float sy, float sz, float so)
        {
            float const dx = p.x - sx, dy = p.y - sy;
            float const c = std::cos(so), s = std::sin(so);
            return {dx * c + dy * s, -dx * s + dy * c, p.z - sz};
        }
    }

    bool SailableLiquid(uint8_t typeFlags)
    {
        return (typeFlags & (kLiquidWater | kLiquidOcean)) && !(typeFlags & (kLiquidMagma | kLiquidSlime));
    }

    bool May(Standing s, uint8_t rights, Duty duty)
    {
        if (s.owner)
            return true;
        switch (duty)
        {
            case Duty::Helm:
                return (s.party && (rights & RIGHT_PARTY_HELM)) || (s.guild && (rights & RIGHT_GUILD_HELM));
            case Duty::Guns:
                return (rights & RIGHT_ANYONE_GUNS) || (s.party && (rights & RIGHT_PARTY_GUNS))
                    || (s.guild && (rights & RIGHT_GUILD_GUNS));
            case Duty::Respawn:
                return (s.party && (rights & RIGHT_PARTY_RESPAWN)) || (s.guild && (rights & RIGHT_GUILD_RESPAWN));
        }
        return false;
    }

    char const* RightName(uint8_t bit)
    {
        switch (bit)
        {
            case RIGHT_PARTY_HELM: return "your party may take the helm";
            case RIGHT_GUILD_HELM: return "your guild may take the helm";
            case RIGHT_PARTY_GUNS: return "your party may man the guns";
            case RIGHT_GUILD_GUNS: return "your guild may man the guns";
            case RIGHT_ANYONE_GUNS: return "anyone aboard may man the guns";
            case RIGHT_PARTY_RESPAWN: return "your party may respawn aboard";
            case RIGHT_GUILD_RESPAWN: return "your guild may respawn aboard";
            default: return "";
        }
    }

    std::vector<Mount> const& MountsOf(Class c)
    {
        // VERIFY every mounting's place on the models in game.
        static std::vector<Mount> const sloop{
            {4.f, 6.f, 6.5f, 1.5708f, 19, "Port gun"},
            {4.f, -6.f, 6.5f, -1.5708f, 19, "Starboard gun"},
            {22.f, 0.f, 8.f, 0.f, 19, "Bow gun"},
        };
        static std::vector<Mount> const airship{
            {8.f, 5.f, -13.f, 1.5708f, 19, "Port gun"},
            {8.f, -5.f, -13.f, -1.5708f, 19, "Starboard gun"},
            {-12.f, 0.f, -13.f, 3.1416f, 42, "Rocket rack"},
        };
        static std::vector<Mount> const none;
        return c == Class::Sloop ? sloop : c == Class::Airship ? airship : none;
    }

    Vec3 MountWorld(Mount const& m, float sx, float sy, float sz, float so)
    {
        float const c = std::cos(so), s = std::sin(so);
        return {sx + m.x * c - m.y * s, sy + m.x * s + m.y * c, sz + m.z};
    }

    HullShape const& HullOf(Class c)
    {
        // VERIFY against the models: Transport_Ship is about 60 yd long, the zeppelin about 70.
        static HullShape const sloop{30.f, 8.f, 4.f, 14.f};
        static HullShape const airship{34.f, 10.f, 18.f, 14.f};
        return c == Class::Airship ? airship : sloop;
    }

    std::optional<float> RayVsHull(Vec3 origin, Vec3 dir, float maxDist, HullShape const& h,
        float sx, float sy, float sz, float so)
    {
        // The slab test in the ship's frame.
        Vec3 const o = ToLocal(origin, sx, sy, sz, so);
        float const c = std::cos(so), s = std::sin(so);
        Vec3 const d{dir.x * c + dir.y * s, -dir.x * s + dir.y * c, dir.z};
        float lo[3] = {-h.halfLength, -h.halfBeam, -h.below};
        float hi[3] = {h.halfLength, h.halfBeam, h.above};
        float oo[3] = {o.x, o.y, o.z};
        float dd[3] = {d.x, d.y, d.z};
        float tmin = 0.f, tmax = maxDist;
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(dd[i]) < 1e-6f)
            {
                if (oo[i] < lo[i] || oo[i] > hi[i])
                    return std::nullopt;
                continue;
            }
            float t1 = (lo[i] - oo[i]) / dd[i], t2 = (hi[i] - oo[i]) / dd[i];
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return std::nullopt;
        }
        return tmin;
    }

    float BlastShare(Vec3 center, float radius, HullShape const& h, float sx, float sy, float sz, float so)
    {
        if (radius <= 0.f)
            return 0.f;
        Vec3 const p = ToLocal(center, sx, sy, sz, so);
        float const dx = std::max(0.f, std::fabs(p.x) - h.halfLength);
        float const dy = std::max(0.f, std::fabs(p.y) - h.halfBeam);
        float const dz = p.z < -h.below ? -h.below - p.z : p.z > h.above ? p.z - h.above : 0.f;
        float const dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        return std::max(0.f, 1.f - dist / radius);
    }

    uint32_t HullDamage(float weaponDamage, bool blast, float share)
    {
        float const v = std::max(0.f, weaponDamage) * (blast ? kBlastHullScale : kRoundHullScale) * std::clamp(share, 0.f, 1.f);
        return uint32_t(v + 0.5f);
    }

    uint8_t CargoSlots(Class c) { return c == Class::Airship ? 24 : c == Class::Sloop ? 16 : 0; }

    uint32_t Stow(std::vector<Stack>& hold, uint8_t slots, uint32_t entry, uint32_t count, uint32_t maxStack)
    {
        if (!entry || !count)
            return 0;
        maxStack = std::max<uint32_t>(1, maxStack);
        uint32_t left = count;
        for (Stack& s : hold)
            if (s.entry == entry && s.count < maxStack && left)
            {
                uint32_t const add = std::min(left, maxStack - s.count);
                s.count += add;
                left -= add;
            }
        while (left && hold.size() < slots)
        {
            uint32_t const add = std::min(left, maxStack);
            hold.push_back({entry, add});
            left -= add;
        }
        return count - left;
    }

    bool Take(std::vector<Stack>& hold, uint8_t index, Stack& out)
    {
        if (index >= hold.size())
            return false;
        out = hold[index];
        hold.erase(hold.begin() + index);
        return true;
    }

    RespawnRefusal CanRespawnAboard(bool crew, State s, bool anchored, uint32_t now, uint32_t lastHitAt,
        uint32_t lastRespawnAt, float distance)
    {
        if (!crew)
            return RespawnRefusal::NotCrew;
        if (s != State::Afloat)
            return RespawnRefusal::Wrecked;
        if (!anchored)
            return RespawnRefusal::NotAnchored;
        if (lastHitAt && now < lastHitAt + kUnderFireSec)
            return RespawnRefusal::UnderFire;
        if (lastRespawnAt && now < lastRespawnAt + kRespawnCooldownSec)
            return RespawnRefusal::Cooldown;
        if (distance > kRespawnReach)
            return RespawnRefusal::TooFar;
        return RespawnRefusal::None;
    }

    std::vector<uint8_t> EncodeCargo(uint8_t slots, std::vector<CargoLine> const& hold, std::vector<CargoLine> const& bags)
    {
        std::vector<uint8_t> o{kOpCargoList, slots};
        PutLines(o, hold);
        PutLines(o, bags);
        return o;
    }

    bool DecodeCargo(std::vector<uint8_t> const& b, uint8_t& slots, std::vector<CargoLine>& hold, std::vector<CargoLine>& bags)
    {
        if (b.size() < 4 || b[0] != kOpCargoList)
            return false;
        slots = b[1];
        size_t at = 2;
        return GetLines(b, at, hold) && GetLines(b, at, bags) && at == b.size();
    }

    std::vector<uint8_t> EncodeGun(uint64_t ship, uint8_t mount)
    {
        std::vector<uint8_t> o{kOpGun};
        PutU64(o, ship);
        o.push_back(mount);
        return o;
    }

    std::vector<uint8_t> EncodeMounts(uint64_t ship, std::vector<bool> const& manned)
    {
        std::vector<uint8_t> o{kOpMounts};
        PutU64(o, ship);
        o.push_back(uint8_t(manned.size()));
        for (bool m : manned)
            o.push_back(m ? 1 : 0);
        return o;
    }

    bool DecodeCrew(std::vector<uint8_t> const& b, CrewAsk& out)
    {
        out = CrewAsk();
        if (b.empty())
            return false;
        out.op = b[0];
        switch (out.op)
        {
            case kOpManGun:
            case kOpTake:
                if (b.size() != 2)
                    return false;
                out.index = b[1];
                return true;
            case kOpLeaveGun:
            case kOpCargo:
                return b.size() == 1;
            case kOpStow:
                if (b.size() != 9)
                    return false;
                out.entry = U32(b, 1);
                out.count = U32(b, 5);
                return out.entry && out.count;
            default:
                return false;
        }
    }
}
