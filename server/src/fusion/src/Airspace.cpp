/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Airspace.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fusion { namespace Wards
{
    std::vector<Zone> const& Zones()
    {
        // Measured (city_holes CITY_PLACES): each city circle is centred on its city WMO's placed
        // group boxes and reaches past them; pads are TaxiNodes.dbc flight masters, the Dalaran
        // portal arrival (34_dalaran.sql) and the Lordaeron launch point. AreaTable ids are 1.12's
        // plus the community's (Stormwind Harbor 4411, Ironforge Airfields 4020). Still estimated:
        // the Teldrassil canopy and Blackrock Mountain circles and every ceiling height.
        static std::vector<Zone> const zones{
            // ── Cities: a ceiling over the roofs, flight masters as pads ──
            // Stormwind.wmo's groups x -9201..-8150, y 168..1438 (the harbour included).
            {1, "Stormwind", Kind::City, 0, -8676.f, 803.f, 720.f, {1519, 4411}, 30.f, kNoZ,
                {{"the gryphon tower", -8840.6f, 489.7f, 109.6f, 15.f},
                 {"the harbour flight master", -8476.2f, 1348.7f, 5.2f, 20.f}}},
            // Ironforge.wmo's groups x -5111..-4560, y -1451..-797, and the mountain round them.
            {2, "Ironforge", Kind::City, 0, -4836.f, -1124.f, 550.f, {1537, 809}, 25.f, kNoZ,
                {{"the gryphon master", -4821.8f, -1155.4f, 502.2f, 15.f}}},
            // Ogrimmar.wmo's groups x 1250..2376, y -5049..-3771.
            {3, "Orgrimmar", Kind::City, 1, 1813.f, -4410.f, 700.f, {1637}, 30.f, kNoZ,
                {{"the wind rider tower", 1677.6f, -4315.7f, 61.2f, 15.f},
                 {"the zeppelin tower", 1320.1f, -4649.2f, 21.6f, 20.f}}},
            // Undercity.wmo's groups x 1237..1878, y 10..776; the ruins of Lordaeron (153) over it.
            {4, "Undercity", Kind::City, 0, 1558.f, 393.f, 420.f, {1497, 153}, 25.f, kNoZ,
                {{"the ruins' courtyard", 1630.f, 238.f, 61.7f, 20.f}}},
            // The mesas' WMOs x -1450..-923, y -376..359 (the great totem tops out at 257).
            {5, "Thunder Bluff", Kind::City, 1, -1187.f, -8.f, 420.f, {1638}, 40.f, 260.f,
                {{"the wind rider rise", -1197.2f, 29.7f, 176.9f, 15.f}}},
            // Darnassis.wmo's groups x 9529..10240, y 2155..2662.
            {6, "Darnassus", Kind::City, 1, 9885.f, 2408.f, 450.f, {1657}, 25.f, kNoZ, {}},
            // ND_Dalaran's placed box x -58..612, y -14..701 (area 279; the spires reach about
            // 150 over the streets at 51-71, 34_dalaran.sql arrival at 71).
            {7, "Dalaran", Kind::City, 0, 277.f, 343.f, 380.f, {279}, 30.f, 240.f,
                {{"the portal arrival", 297.12f, 293.69f, 71.f, 20.f}}},
            // ── Unfinished places: a fixed invisible ceiling ──
            // Hyjal (stock 616 and the community's): no higher than the homesteads' ceiling.
            {20, "Mount Hyjal", Kind::Unfinished, 1, 5100.f, -2700.f, 1550.f, {616}, 0.f, 1700.f,
                {{"the homesteads' gate", 5385.f, -3710.f, 1594.2f, 15.f},
                 {"Nordanaar", 5497.7f, -3690.7f, 1594.2f, 15.f},
                 {"the ledge over Felwood", 5234.f, -1220.f, 1366.8f, 15.f}}},
            // Teldrassil's top round Darnassus: under the canopy (estimated).
            {21, "the Teldrassil canopy", Kind::Unfinished, 1, 9900.f, 1200.f, 1400.f, {}, 0.f, 1420.f, {}},
            // Blackrock Mountain's back, over Searing Gorge and the Burning Steppes' rim (estimated).
            {22, "Blackrock Mountain", Kind::Unfinished, 0, -7600.f, -1100.f, 600.f, {25, 254, 1445}, 0.f, 330.f, {}},
            // The ridge round the Ironforge Airfields (TaxiNodes 93 at z 509).
            {23, "the Ironforge airport ridge", Kind::Unfinished, 0, -4492.7f, -1581.f, 450.f, {4020}, 0.f, 600.f,
                {{"the airfield flight master", -4492.7f, -1581.f, 509.f, 20.f}}},
        };
        return zones;
    }

    bool Contains(Zone const& z, uint32_t map, float x, float y, uint32_t area, uint32_t zoneId)
    {
        if (z.map != map)
            return false;
        float const d2 = (x - z.x) * (x - z.x) + (y - z.y) * (y - z.y);
        if (d2 <= z.radius * z.radius)
            return true;
        float const reach = z.radius * kAreaReach;
        if (d2 > reach * reach)
            return false;
        for (uint32_t a : z.areas)
            if (a != 0 && (a == area || a == zoneId))
                return true;
        return false;
    }

    float CeilingOf(Zone const& z, float x, float y, float groundZ, bool* pad)
    {
        float c = z.maxZ;
        if (z.aboveGround > 0.f)
            c = std::min(c, groundZ + z.aboveGround);
        bool onPad = false;
        for (Pad const& p : z.pads)
            if ((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) <= p.radius * p.radius)
            {
                c = std::max(c, p.z + kPadHeadroom);
                onPad = true;
            }
        if (pad)
            *pad = onPad;
        return c;
    }

    bool Find(std::vector<Zone> const& zones, uint32_t map, float x, float y, float groundZ,
        uint32_t area, uint32_t zoneId, Here& out)
    {
        out = Here{};
        for (Zone const& z : zones)
        {
            if (!Contains(z, map, x, y, area, zoneId))
                continue;
            bool pad = false;
            float const c = CeilingOf(z, x, y, groundZ, &pad);
            if (!out.zone || c < out.ceilingZ)
            {
                out.zone = &z;
                out.ceilingZ = c;
                out.pad = pad;
            }
        }
        return out.zone != nullptr;
    }

    float AllowedUp(float z, float ceilingZ)
    {
        float const left = ceilingZ - z;
        if (left >= 0.f)
            return left * kEaseGain;
        return -std::min(kPushMax, kPushMin - left * kPushGain);
    }

    bool Hold(Ward& w, uint16_t zone, float ceilingZ, float z, uint32_t nowMs, bool& enteredOver)
    {
        enteredOver = false;
        if (zone == 0)
        {
            w = Ward{};
            return true;
        }
        float const over = z - ceilingZ;
        if (w.zone != zone)
        {
            // Into the airspace: allowed to be as far over as the rider came in, and coming down.
            w = Ward{};
            w.zone = zone;
            w.allowance = std::max(0.f, over);
            w.lastCeiling = ceilingZ;
            w.sampleMs = nowMs;
            w.pushed = over > 0.f;
            enteredOver = w.pushed;
            return true;
        }
        float const dt = std::max(0.f, float(int32_t(nowMs - w.sampleMs)) / 1000.f);
        // A ceiling that drops (lower ground, off a pad) is not the rider's doing: it adds.
        w.allowance = std::max(0.f, w.allowance - kHoldRate * dt) + std::max(0.f, w.lastCeiling - ceilingZ);
        w.lastCeiling = ceilingZ;
        w.sampleMs = nowMs;
        bool const nowOver = over > 0.f;
        enteredOver = nowOver && !w.pushed;
        w.pushed = nowOver;
        bool const bad = over > w.allowance + kHoldSlack;
        w.strikes = bad ? uint8_t(std::min<int>(w.strikes + 1, 255)) : uint8_t(0);
        return w.strikes < kHoldStrikes;
    }

    std::vector<uint8_t> EncodeWard(uint8_t flags, float ceilingZ)
    {
        std::vector<uint8_t> o{kOpWard, flags};
        uint32_t u;
        std::memcpy(&u, &ceilingZ, 4);
        for (int i = 0; i < 4; ++i)
            o.push_back(uint8_t(u >> (8 * i)));
        return o;
    }

    char const* PushMessage(Kind k)
    {
        return k == Kind::City ? "The city's wards force you down." : "The winds here will carry you no higher.";
    }
}}
