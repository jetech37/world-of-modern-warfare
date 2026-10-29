/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Furniture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Fusion { namespace Furniture
{
    std::vector<Def> const& All()
    {
        // FURNITURE_TABLE_BEGIN (the client's pieces.rs FURNITURE holds the same, with models)
        static std::vector<Def> const all = {
            {200, "Wooden chair", Use::Seat, Mount::Floor, Station::None, 0.00f, {-0.12f, -0.03f, 0.26f}, {0.33f, 0.32f, 0.27f}, 0, 0, 0, 0},
            {201, "Cushioned chair", Use::Seat, Mount::Floor, Station::None, 0.00f, {-0.31f, -0.00f, 1.04f}, {0.46f, 0.80f, 1.04f}, 0, 0, 0, 0},
            {202, "Inn bench", Use::Seat, Mount::Floor, Station::None, 0.00f, {-0.01f, 0.00f, 0.39f}, {0.40f, 2.10f, 0.39f}, 0, 0, 0, 0},
            {203, "Stormwind bench", Use::Seat, Mount::Floor, Station::None, 0.00f, {-0.47f, 0.00f, 0.22f}, {0.53f, 1.50f, 0.28f}, 0, 0, 0, 0},
            {204, "Dwarven chair", Use::Seat, Mount::Floor, Station::None, 0.00f, {-0.14f, 0.00f, 1.53f}, {0.55f, 1.04f, 1.56f}, 0, 0, 0, 0},
            {205, "Log bench", Use::Seat, Mount::Floor, Station::None, 0.00f, {-0.03f, 0.03f, 0.13f}, {0.70f, 3.69f, 0.37f}, 0, 0, 0, 3},
            {206, "High-backed chair", Use::Seat, Mount::Floor, Station::None, 0.00f, {-0.56f, 0.00f, 0.85f}, {0.69f, 0.59f, 0.86f}, 0, 0, 0, 0},
            {207, "Inn bed", Use::Bed, Mount::Floor, Station::None, 0.00f, {0.05f, 0.00f, 0.67f}, {2.39f, 1.69f, 0.67f}, 0, 0, 0, 0},
            {208, "Canopy bed", Use::Bed, Mount::Floor, Station::None, 0.00f, {0.00f, 0.00f, 2.15f}, {2.96f, 3.10f, 2.15f}, 0, 0, 0, 0},
            {209, "Bunk bed", Use::Bed, Mount::Floor, Station::None, 0.00f, {0.07f, 0.03f, 1.72f}, {2.63f, 1.39f, 1.72f}, 0, 0, 0, 0},
            {210, "Dwarven bed", Use::Bed, Mount::Floor, Station::None, 0.00f, {0.01f, 0.35f, 1.13f}, {2.20f, 3.51f, 1.15f}, 0, 0, 0, 0},
            {211, "Inn table", Use::Surface, Mount::Floor, Station::None, 1.04f, {-0.00f, 0.00f, 0.52f}, {1.43f, 1.99f, 0.52f}, 0, 0, 0, 0},
            {212, "Small table", Use::Surface, Mount::Floor, Station::None, 1.14f, {-0.02f, 0.01f, 0.57f}, {1.06f, 1.08f, 0.57f}, 0, 0, 0, 0},
            {213, "Long table", Use::Surface, Mount::Floor, Station::None, 0.98f, {0.05f, -0.00f, 0.44f}, {1.35f, 1.42f, 0.54f}, 0, 0, 0, 0},
            {214, "Writing desk", Use::Surface, Mount::Floor, Station::None, 1.74f, {-0.44f, -0.00f, 0.86f}, {0.98f, 1.59f, 0.88f}, 0, 0, 0, 0},
            {215, "Chest", Use::Stash, Mount::Floor, Station::None, 0.00f, {-0.00f, -0.00f, 0.32f}, {0.39f, 0.59f, 0.31f}, 16, 0, 0, 0},
            {216, "Footlocker", Use::Stash, Mount::Floor, Station::None, 0.00f, {-0.00f, -0.00f, 0.23f}, {0.30f, 0.69f, 0.24f}, 8, 0, 0, 2},
            {217, "Wardrobe", Use::Stash, Mount::Floor, Station::None, 0.00f, {0.34f, 0.01f, 1.71f}, {0.66f, 1.05f, 1.71f}, 24, 0, 0, 0},
            {218, "Bookshelf", Use::Book, Mount::Floor, Station::None, 0.00f, {0.12f, -0.00f, 2.03f}, {0.44f, 1.40f, 2.04f}, 0, 2, 0, 0},
            {219, "Abbey bookshelf", Use::Book, Mount::Floor, Station::None, 0.00f, {-0.09f, -0.01f, 2.54f}, {1.78f, 0.77f, 2.53f}, 0, 3, 0, 0},
            {220, "Book stack", Use::Book, Mount::Surface, Station::None, 0.00f, {-0.14f, 0.05f, 0.18f}, {0.40f, 0.29f, 0.18f}, 0, 1, 0, 3},
            {221, "Lectern", Use::Book, Mount::Floor, Station::None, 0.00f, {0.04f, 0.00f, 0.74f}, {0.39f, 0.41f, 0.74f}, 0, 4, 0, 0},
            {222, "Smith's anvil", Use::Station, Mount::Floor, Station::Anvil, 0.00f, {0.05f, 0.00f, 0.48f}, {0.73f, 0.36f, 0.48f}, 0, 0, 0, 0},
            {223, "Forge", Use::Station, Mount::Floor, Station::Forge, 0.00f, {0.18f, 0.06f, 0.33f}, {1.83f, 1.70f, 0.41f}, 0, 0, 0, 0},
            {224, "Cooking fire", Use::Station, Mount::Floor, Station::Cooking, 0.00f, {0.00f, 0.00f, 0.65f}, {1.49f, 1.49f, 0.64f}, 0, 0, 0, 0},
            {225, "Loom", Use::Station, Mount::Floor, Station::Loom, 0.00f, {-0.01f, 0.01f, 1.15f}, {0.48f, 1.08f, 1.15f}, 0, 0, 0, 0},
            {226, "Alchemy table", Use::Station, Mount::Floor, Station::Alchemy, 0.00f, {0.01f, 0.06f, 1.23f}, {0.97f, 1.68f, 1.23f}, 0, 0, 0, 0},
            {227, "Mailbox", Use::Mailbox, Mount::Floor, Station::None, 0.00f, {0.00f, 0.01f, 1.54f}, {0.52f, 0.71f, 1.54f}, 0, 0, 50000, 0},
            // The dummy is only its base: the creature standing in it takes the shots (FusionFurniture.cpp).
            {228, "Training dummy", Use::Dummy, Mount::Floor, Station::None, 0.00f, {0.12f, -0.11f, 0.05f}, {0.29f, 0.89f, 0.05f}, 0, 0, 20000, 0},
            {229, "Trophy plinth", Use::Trophy, Mount::Floor, Station::None, 0.00f, {-0.08f, 0.01f, 0.51f}, {1.11f, 2.03f, 0.68f}, 0, 0, 0, 0},
            {230, "Bard's stage", Use::Stage, Mount::Floor, Station::None, 0.00f, {-1.00f, 0.12f, 0.24f}, {4.70f, 3.85f, 0.26f}, 0, 0, 0, 0},
            {231, "Stormwind banner", Use::Decor, Mount::Floor, Station::None, 0.00f, {-0.02f, -0.03f, 2.95f}, {0.19f, 2.43f, 3.50f}, 0, 0, 5000, 0},
            {232, "Horde banner", Use::Decor, Mount::Floor, Station::None, 0.00f, {-0.01f, -0.00f, 2.82f}, {0.33f, 1.59f, 2.77f}, 0, 0, 5000, 0},
            {233, "Clan banner", Use::Decor, Mount::Floor, Station::None, 0.00f, {-0.16f, 0.00f, 3.07f}, {0.21f, 1.19f, 3.26f}, 0, 0, 0, 0},
            {234, "Stormwind rug", Use::Decor, Mount::Floor, Station::None, 0.00f, {0.08f, 0.02f, 0.01f}, {1.55f, 1.40f, 0.05f}, 0, 0, 0, 0},
            {235, "Red rug", Use::Decor, Mount::Floor, Station::None, 0.00f, {0.11f, -0.01f, 0.01f}, {5.00f, 2.39f, 0.05f}, 0, 0, 3000, 0},
            {236, "Bearskin rug", Use::Decor, Mount::Floor, Station::None, 0.00f, {0.14f, 0.00f, 0.43f}, {2.79f, 2.89f, 0.45f}, 0, 0, 0, 2},
            {237, "Tauren rug", Use::Decor, Mount::Floor, Station::None, 0.00f, {-0.00f, 0.02f, 0.03f}, {1.34f, 0.90f, 0.05f}, 0, 0, 0, 0},
            {238, "Candle", Use::Decor, Mount::Surface, Station::None, 0.00f, {0.00f, -0.00f, 0.27f}, {0.12f, 0.12f, 0.27f}, 0, 0, 0, 4},
            {239, "Candelabra", Use::Decor, Mount::Surface, Station::None, 0.00f, {0.00f, 0.00f, 0.28f}, {0.18f, 0.43f, 0.28f}, 0, 0, 0, 0},
            {240, "Brazier", Use::Decor, Mount::Floor, Station::None, 0.00f, {-0.02f, -0.06f, 0.37f}, {1.11f, 1.16f, 0.36f}, 0, 0, 0, 0},
            {241, "Wall candelabra", Use::Decor, Mount::Wall, Station::None, 0.00f, {0.00f, 0.00f, 1.59f}, {0.29f, 0.84f, 1.59f}, 0, 0, 0, 0},
            {242, "Hanging lantern", Use::Decor, Mount::Wall, Station::None, 0.00f, {0.00f, 0.00f, 0.92f}, {0.36f, 0.36f, 1.19f}, 0, 0, 0, 0},
            {243, "Flowers", Use::Decor, Mount::Surface, Station::None, 0.00f, {0.02f, -0.06f, 0.13f}, {0.23f, 0.41f, 0.24f}, 0, 0, 0, 4},
            {244, "Planter", Use::Decor, Mount::Floor, Station::None, 0.00f, {0.00f, 0.00f, 0.56f}, {2.63f, 2.66f, 1.22f}, 0, 0, 0, 0},
            {245, "Beer keg", Use::Decor, Mount::Surface, Station::None, 0.00f, {-0.15f, 0.00f, 0.65f}, {0.68f, 0.74f, 0.65f}, 0, 0, 0, 0},
            {246, "Mug", Use::Decor, Mount::Surface, Station::None, 0.00f, {-0.05f, -0.02f, 0.13f}, {0.12f, 0.16f, 0.14f}, 0, 0, 0, 4},
            {247, "Weapon rack", Use::Decor, Mount::Floor, Station::None, 0.00f, {0.05f, 0.18f, 1.33f}, {0.65f, 0.53f, 1.45f}, 0, 0, 0, 0},
            {248, "Wall shield", Use::Decor, Mount::Wall, Station::None, 0.00f, {0.05f, 0.00f, -0.00f}, {0.05f, 0.36f, 0.53f}, 0, 0, 0, 0},
            {249, "Mounted boar head", Use::Decor, Mount::Wall, Station::None, 0.00f, {0.42f, 0.00f, 0.32f}, {0.48f, 0.48f, 0.62f}, 0, 0, 0, 2},
            {250, "Stuffed bear", Use::Decor, Mount::Floor, Station::None, 0.00f, {0.12f, -0.07f, 1.90f}, {1.09f, 1.12f, 1.91f}, 0, 0, 0, 0},
            {251, "Painting", Use::Decor, Mount::Wall, Station::None, 0.00f, {-0.01f, -0.00f, -0.05f}, {0.11f, 2.36f, 1.22f}, 0, 0, 4000, 0},
            {252, "Globe", Use::Decor, Mount::Surface, Station::None, 0.00f, {0.01f, -0.05f, 0.83f}, {0.39f, 0.46f, 0.84f}, 0, 0, 0, 0},
            {253, "Armor stand", Use::Decor, Mount::Floor, Station::None, 0.00f, {0.00f, -0.00f, 0.89f}, {0.45f, 0.43f, 0.89f}, 0, 0, 0, 0},
            {254, "Potbelly stove", Use::Station, Mount::Floor, Station::Cooking, 0.00f, {-0.32f, 0.04f, 1.85f}, {0.72f, 0.54f, 1.89f}, 0, 0, 0, 0},
        };
        // FURNITURE_TABLE_END
        return all;
    }

    Def const* Find(Build::PieceType t)
    {
        uint8_t const v = uint8_t(t);
        if (v < kBase)
            return nullptr;
        std::vector<Def> const& a = All();
        size_t const i = size_t(v - kBase);
        return i < a.size() ? &a[i] : nullptr;
    }

    Use UseOf(Build::PieceType t)
    {
        Def const* d = Find(t);
        return d ? d->use : Use::Decor;
    }

    std::vector<Def const*> Sold()
    {
        std::vector<Def const*> out;
        for (Def const& d : All())
            if (d.priceCopper)
                out.push_back(&d);
        return out;
    }

    Build::PieceType DropFor(float roll, float pick)
    {
        if (roll >= kDropChance)
            return Build::PieceType::None;
        uint32_t total = 0;
        for (Def const& d : All())
            total += d.dropWeight;
        if (!total)
            return Build::PieceType::None;
        float at = pick * float(total);
        for (Def const& d : All())
        {
            if (!d.dropWeight)
                continue;
            if (at < float(d.dropWeight))
                return Build::PieceType(d.id);
            at -= float(d.dropWeight);
        }
        for (auto it = All().rbegin(); it != All().rend(); ++it)
            if (it->dropWeight)
                return Build::PieceType(it->id);
        return Build::PieceType::None;
    }

    float SnapYaw(float yaw)
    {
        float const steps = std::round(yaw / kTurnStep);
        float y = steps * kTurnStep;
        float const tau = 6.2831853f;
        y = std::fmod(y, tau);
        if (y < 0.f)
            y += tau;
        return y;
    }

    float SnapGrid(float v)
    {
        return std::round(v / kGrid) * kGrid;
    }

    namespace
    {
        float Lowest(Build::Piece const& p)
        {
            float low = 1e30f;
            for (Build::Box const& b : Build::BoxesOf(p))
            {
                Build::WorldBox const w = Build::Place(p, b);
                float const reach = std::fabs(w.axis[0].z) * w.half.x + std::fabs(w.axis[1].z) * w.half.y + std::fabs(w.axis[2].z) * w.half.z;
                low = std::min(low, w.center.z - reach);
            }
            return low;
        }

        // A wall to hang a piece on: anything built that is not furniture, a floor's underside
        // excepted only by its own geometry (touching is touching).
        bool Hangs(Build::PieceType t)
        {
            return !IsFurniture(t);
        }
    }

    bool OnSurface(Build::Piece const& p, std::vector<Build::Piece> const& pieces)
    {
        float const low = Lowest(p);
        Vec3 const c = Build::Center(p);
        for (Build::Piece const& q : pieces)
        {
            if (q.id == p.id || (q.pos - c).Length() > 6.f)
                continue;
            Def const* d = Find(q.type);
            if (!d || d->top <= 0.f)
                continue;
            std::optional<float> const top = Build::TopUnder(q, c.x, c.y, low + Build::kFloorSlack);
            if (top && *top >= low - Build::kFloorSlack)
                return true;
        }
        return false;
    }

    bool OnWall(Build::Piece const& p, std::vector<Build::Piece> const& pieces)
    {
        Vec3 const c = Build::Center(p);
        for (Build::Piece const& q : pieces)
            if (q.id != p.id && Hangs(q.type) && (Build::Center(q) - c).Length() < 30.f && Build::Touch(p, q, kWallSlack))
                return true;
        return false;
    }

    bool Footed(Build::Piece const& p, std::vector<Build::Piece> const& pieces, Build::Ground const& ground)
    {
        Def const* d = Find(p.type);
        if (!d)
            return Build::Footed(p, pieces, ground);
        switch (d->mount)
        {
            case Mount::Wall:
                return OnWall(p, pieces);
            case Mount::Surface:
                return Build::Footed(p, pieces, ground) || OnSurface(p, pieces);
            default:
            {
                // A floor piece stands on the ground or a built floor, never on another piece of
                // furniture's top (a table holds small things, not chairs).
                std::vector<Build::Piece> floors;
                floors.reserve(pieces.size());
                for (Build::Piece const& q : pieces)
                    if (!Find(q.type))
                        floors.push_back(q);
                return Build::Footed(p, floors, ground);
            }
        }
    }

    uint32_t SpellFocus(Station s)
    {
        switch (s)
        {
            case Station::Anvil: return 1;
            case Station::Loom: return 2;
            case Station::Forge: return 3;
            case Station::Cooking: return 4;
            case Station::Alchemy: return kAlchemyLabFocus;
            default: return 0;
        }
    }

    bool StationNear(std::vector<Build::Piece> const& pieces, Vec3 at, uint32_t focus, float radius)
    {
        if (!focus)
            return false;
        for (Build::Piece const& p : pieces)
        {
            Def const* d = Find(p.type);
            if (!d || d->use != Use::Station || SpellFocus(d->station) != focus || p.health <= 0.f)
                continue;
            if (Build::Distance(p, at) <= radius)
                return true;
        }
        return false;
    }

    Vec3 SeatSpot(Build::Piece const& p)
    {
        Vec3 c = Build::Center(p);
        Def const* d = Find(p.type);
        float h = 0.f;
        if (d)
        {
            float const boxTop = d->center.z + d->half.z;
            h = d->top > 0.f ? d->top : std::min(boxTop, d->use == Use::Bed ? kBedHeight : kSeatHeight);
        }
        c.z = p.pos.z + h;
        return c;
    }

    bool RestsNear(std::vector<Build::Piece> const& pieces, Vec3 feet, std::function<bool(Build::Piece const&)> const& mayRest)
    {
        for (Build::Piece const& p : pieces)
            if (UseOf(p.type) == Use::Bed && p.health > 0.f && Build::Distance(p, feet) <= kRestRadius && (!mayRest || mayRest(p)))
                return true;
        return false;
    }

    bool OnStage(std::vector<Build::Piece> const& pieces, Vec3 feet)
    {
        for (Build::Piece const& p : pieces)
            if (UseOf(p.type) == Use::Stage && p.health > 0.f && Build::Distance(p, feet) <= kStageReach)
                return true;
        return false;
    }

    uint16_t StashSlots(std::vector<Build::Piece> const& onPlot)
    {
        uint32_t n = 0;
        for (Build::Piece const& p : onPlot)
            if (Def const* d = Find(p.type))
                if (d->use == Use::Stash && p.health > 0.f)
                    n += d->stashSlots;
        return uint16_t(std::min<uint32_t>(n, kMaxStashSlots));
    }

    bool Storable(ItemFacts const& f)
    {
        return !f.soulbound && !f.enchanted && !f.randomProperty && !f.quest && !f.filledBag && !f.conjured;
    }

    StashResult Deposit(std::vector<StashEntry>& stash, uint16_t slots, uint32_t item, uint32_t count, uint32_t maxStack)
    {
        if (!item || !count)
            return StashResult::NotThere;
        if (maxStack == 0)
            maxStack = 1;
        // The room on its stacks, then whole new slots.
        uint64_t room = 0;
        for (StashEntry const& e : stash)
            if (e.item == item && e.count < maxStack)
                room += maxStack - e.count;
        size_t const freeSlots = stash.size() < slots ? slots - stash.size() : 0;
        room += uint64_t(freeSlots) * maxStack;
        if (room < count)
            return StashResult::NoRoom;
        uint32_t left = count;
        for (StashEntry& e : stash)
            if (left && e.item == item && e.count < maxStack)
            {
                uint32_t const put = std::min(left, maxStack - e.count);
                e.count += put;
                left -= put;
            }
        while (left)
        {
            uint32_t const put = std::min(left, maxStack);
            stash.push_back({item, put});
            left -= put;
        }
        return StashResult::Ok;
    }

    StashResult Withdraw(std::vector<StashEntry>& stash, size_t index, uint32_t count, StashEntry& out)
    {
        if (index >= stash.size() || !count)
            return StashResult::NotThere;
        StashEntry& e = stash[index];
        uint32_t const take = std::min(count, e.count);
        out = {e.item, take};
        e.count -= take;
        if (!e.count)
            stash.erase(stash.begin() + std::ptrdiff_t(index));
        return StashResult::Ok;
    }

    void DpsMeter::Hit(uint64_t nowMs, uint32_t damage)
    {
        if (!hits)
            firstMs = nowMs;
        lastMs = nowMs;
        total += damage;
        ++hits;
    }

    float DpsMeter::Seconds() const
    {
        return hits ? std::max(1.f, float(lastMs - firstMs) / 1000.f) : 0.f;
    }

    float DpsMeter::Dps() const
    {
        return hits ? float(total) / Seconds() : 0.f;
    }

    std::string DpsReport(DpsMeter const& m)
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%llu damage in %.1f s over %u hits: %.1f damage per second",
            static_cast<unsigned long long>(m.total), m.Seconds(), m.hits, m.Dps());
        return buf;
    }

    std::vector<Book> const& Books()
    {
        static std::vector<Book> const books = {
            {1, "A Builder's Primer", {
                "Construction is a trade like any other. Train it at a Construction Trainer, cut lumber with a "
                "Woodcutting Axe, and make your pieces as kits at the trade window (B, then Construction).",
                "Place a kit with Build Structure on Utility's bar. [ and ] pick the piece, the wheel sets how far "
                "out it goes, R turns it. Furniture turns in 15-degree steps and sits on a quarter-yard grid.",
                "Small things (candles, books, mugs) stand on tables; banners, shields and trophies hang on walls. "
                "Take a piece down for half its materials back.",
            }},
            {2, "Hearth and Home", {
                "A plot bought from a Plot Steward is yours to keep while its upkeep is paid. Nothing you build "
                "there decays, and nobody else may build on it without your permit.",
                "A bed on your own plot lets you rest there as in an inn. A chest, a footlocker or a wardrobe adds "
                "room to the plot's shared stash, which you and your permit holders all reach.",
                "An anvil, a forge, a cooking fire, a loom or an alchemy table works for your professions as the "
                "ones in the cities do. A mailbox works as any mailbox.",
            }},
            {3, "The Fall of the Second War", {
                "When the Horde broke at Blackrock Spire, the orcs that survived were herded into internment camps. "
                "The Alliance that beat them did not long outlast its victory.",
                "Gilneas and Stromgarde left it, Quel'Thalas turned its back, and Lordaeron was left to pay for the "
                "camps alone. Peace, it turned out, was more expensive than war.",
            }},
            {4, "On the Use of Firearms", {
                "Every soldier carries a kit of guns, equipment and perks. Aim down the sights to steady a shot; "
                "a shot to the head does the most harm.",
                "Test your kit on a training dummy: strike it and, when you stop, it tells you your damage and your "
                "damage per second.",
            }},
            {5, "Songs of the Road", {
                "A minstrel's song lands best before a crowd. Play standing on a bard's stage and the song carries "
                "a quarter further into those who hear it.",
                "Practice, keep time, and never play the same note twice the same way: listeners know a machine.",
            }},
            {6, "Beasts of Azeroth", {
                "A companion tamed is a friend for life. Bring yours to a trophy plinth and use it: the plinth keeps "
                "its likeness, for your guests to admire.",
            }},
        };
        return books;
    }

    Book const* BookOf(uint8_t id)
    {
        for (Book const& b : Books())
            if (b.id == id)
                return &b;
        return nullptr;
    }

    namespace
    {
        void Put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
        void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
        uint16_t Get16(uint8_t const* b) { return uint16_t(b[0] | b[1] << 8); }
        uint32_t Get32(uint8_t const* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }
    }

    bool DecodeUse(std::vector<uint8_t> const& body, uint32_t& piece)
    {
        if (body.size() != 5 || body[0] != kOpUse)
            return false;
        piece = Get32(&body[1]);
        return true;
    }

    std::vector<uint8_t> EncodeUse(uint32_t piece)
    {
        std::vector<uint8_t> o{kOpUse};
        Put32(o, piece);
        return o;
    }

    bool DecodeStash(std::vector<uint8_t> const& body, StashAct& out)
    {
        if (body.size() != 10 || body[0] != kOpStash)
            return false;
        out.piece = Get32(&body[1]);
        out.action = body[5];
        out.a = body[6];
        out.b = body[7];
        out.count = Get16(&body[8]);
        return out.action == kStashPut || out.action == kStashTake;
    }

    std::vector<uint8_t> EncodeStash(StashAct const& a)
    {
        std::vector<uint8_t> o{kOpStash};
        Put32(o, a.piece);
        o.push_back(a.action);
        o.push_back(a.a);
        o.push_back(a.b);
        Put16(o, a.count);
        return o;
    }

    std::vector<uint8_t> EncodePage(uint32_t piece, Use kind, std::string const& title, std::vector<std::string> const& lines)
    {
        std::vector<uint8_t> o{kOpPage};
        Put32(o, piece);
        o.push_back(uint8_t(kind));
        std::string const t = title.substr(0, 255);
        o.push_back(uint8_t(t.size()));
        o.insert(o.end(), t.begin(), t.end());
        size_t const n = std::min<size_t>(lines.size(), 32);
        o.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            std::string const l = lines[i].substr(0, 2000);
            Put16(o, uint16_t(l.size()));
            o.insert(o.end(), l.begin(), l.end());
        }
        return o;
    }

    std::vector<uint8_t> EncodeStashView(uint32_t piece, uint16_t slots, std::vector<StashEntry> const& stash, std::vector<BagItem> const& bags)
    {
        std::vector<uint8_t> o{kOpStashView};
        Put32(o, piece);
        Put16(o, slots);
        size_t const n = std::min<size_t>(stash.size(), 0xFFFF);
        Put16(o, uint16_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Put32(o, stash[i].item);
            Put16(o, uint16_t(std::min<uint32_t>(stash[i].count, 0xFFFF)));
        }
        size_t const m = std::min<size_t>(bags.size(), 255);
        o.push_back(uint8_t(m));
        for (size_t i = 0; i < m; ++i)
        {
            o.push_back(bags[i].bag);
            o.push_back(bags[i].slot);
            Put32(o, bags[i].item);
            Put16(o, bags[i].count);
        }
        return o;
    }
}}
