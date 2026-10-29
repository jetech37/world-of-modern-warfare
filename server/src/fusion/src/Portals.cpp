/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Portals.h"

#include <algorithm>

namespace Fusion { namespace Portals
{
    std::vector<Destination> const& Capitals()
    {
        // The mages' teleports (Teleport: Stormwind 3561 ...); the spots are each city's heart,
        // where a GM's .tele goes, for a world DB without spell_target_position rows.
        static std::vector<Destination> const capitals = {
            {1, "Stormwind", Team::Alliance, 3561, 0, -8833.38f, 628.628f, 94.0066f, 1.0654f},
            {2, "Ironforge", Team::Alliance, 3562, 0, -4918.88f, -940.406f, 501.564f, 5.4235f},
            {3, "Darnassus", Team::Alliance, 3565, 1, 9949.56f, 2284.21f, 1341.40f, 1.5959f},
            {4, "Orgrimmar", Team::Horde, 3567, 1, 1629.36f, -4373.39f, 31.2564f, 3.5484f},
            {5, "Undercity", Team::Horde, 3563, 0, 1584.07f, 241.987f, -52.1534f, 0.0496f},
            {6, "Thunder Bluff", Team::Horde, 3566, 1, -1277.37f, 124.804f, 131.287f, 5.2227f},
        };
        return capitals;
    }

    Destination const* CapitalById(uint8_t id)
    {
        for (Destination const& d : Capitals())
            if (d.id == id)
                return &d;
        return nullptr;
    }

    std::vector<Choice> ChoicesFor(Team team, bool hasHomestead)
    {
        std::vector<Choice> out;
        for (Destination const& d : Capitals())
            if (d.team == team)
                out.push_back({d.id, d.name});
        if (hasHomestead)
            out.push_back({kHomestead, "My homestead"});
        return out;
    }

    bool Allowed(Team team, uint8_t id, bool hasHomestead)
    {
        if (id == kHomestead)
            return hasHomestead;
        Destination const* d = CapitalById(id);
        return d && d->team == team;
    }

    TravelCheck CanTravel(Traveller const& t)
    {
        if (!t.alive)
            return TravelCheck::Dead;
        if (t.inCombat)
            return TravelCheck::InCombat;
        if (t.travelling)
            return TravelCheck::Travelling;
        if (t.cooldownLeftMs > 0)
            return TravelCheck::CoolingDown;
        return TravelCheck::Ok;
    }

    std::string WaitText(uint32_t ms)
    {
        if (ms < 60 * 1000)
            return "under a minute";
        uint32_t const minutes = (ms + 59 * 1000) / (60 * 1000);
        return std::to_string(minutes) + " min";
    }

    bool MayPlace(uint32_t ownedByBuilder, bool onPlot, uint32_t onThatPlot, bool gm)
    {
        if (gm)
            return true;
        if (ownedByBuilder >= kMaxPerPlayer)
            return false;
        return !onPlot || onThatPlot < kMaxPerPlot;
    }

    namespace
    {
        void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
        uint32_t Get32(uint8_t const* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }
    }

    bool DecodeAsk(std::vector<uint8_t> const& body, uint32_t& piece)
    {
        if (body.size() != 5 || body[0] != kOpAsk)
            return false;
        piece = Get32(&body[1]);
        return true;
    }

    std::vector<uint8_t> EncodeAsk(uint32_t piece)
    {
        std::vector<uint8_t> o{kOpAsk};
        Put32(o, piece);
        return o;
    }

    bool DecodeTravel(std::vector<uint8_t> const& body, uint32_t& piece, uint8_t& destination)
    {
        if (body.size() != 6 || body[0] != kOpTravel)
            return false;
        piece = Get32(&body[1]);
        destination = body[5];
        return true;
    }

    std::vector<uint8_t> EncodeTravel(uint32_t piece, uint8_t destination)
    {
        std::vector<uint8_t> o{kOpTravel};
        Put32(o, piece);
        o.push_back(destination);
        return o;
    }

    std::vector<uint8_t> EncodeMenu(Menu const& m)
    {
        std::vector<uint8_t> o{kOpMenu};
        Put32(o, m.piece);
        Put32(o, m.cooldownSeconds);
        size_t const n = std::min<size_t>(m.choices.size(), 255);
        o.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Choice const& c = m.choices[i];
            o.push_back(c.id);
            size_t const len = std::min<size_t>(c.name.size(), 255);
            o.push_back(uint8_t(len));
            o.insert(o.end(), c.name.begin(), c.name.begin() + std::ptrdiff_t(len));
        }
        return o;
    }

    bool DecodeMenu(std::vector<uint8_t> const& body, Menu& out)
    {
        out = Menu();
        if (body.size() < 10 || body[0] != kOpMenu)
            return false;
        out.piece = Get32(&body[1]);
        out.cooldownSeconds = Get32(&body[5]);
        size_t const n = body[9];
        size_t at = 10;
        for (size_t i = 0; i < n; ++i)
        {
            if (at + 2 > body.size())
                return false;
            Choice c;
            c.id = body[at];
            size_t const len = body[at + 1];
            at += 2;
            if (at + len > body.size())
                return false;
            c.name.assign(body.begin() + std::ptrdiff_t(at), body.begin() + std::ptrdiff_t(at + len));
            at += len;
            out.choices.push_back(std::move(c));
        }
        return at == body.size();
    }
}}
