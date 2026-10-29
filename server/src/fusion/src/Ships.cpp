/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Ships.h"

#include <cmath>
#include <cstring>

namespace Fusion::Ships
{
    namespace
    {
        constexpr float kDeg = 3.14159265f / 180.f;

        std::vector<ClassInfo> MakeClasses()
        {
            std::vector<ClassInfo> v;
            // The Sloop: the stock Transport_Ship. Materials from four trades' high end.
            v.push_back({Class::Sloop, "Sloop", 191800, 93800, 100 * kGold, 400 * kGold, 120,
                {{3860, 20, "Mithril Bar"}, {12359, 10, "Thorium Bar"}, {4339, 40, "Bolt of Mageweave"},
                    {4304, 20, "Thick Leather"}},
                14.f, 3.f, 1.5f, 18.f * kDeg, 0.f, 10000, 5 * kGold, 1 * kGold, 10 * kGold,
                6.f /* VERIFY in game */, 3.f});
            // The Airship: the stock Transport_Zeppelin. Engineering's and the endgame's goods.
            v.push_back({Class::Airship, "Airship", 191801, 93801, 300 * kGold, 1200 * kGold, 360,
                {{12359, 20, "Thorium Bar"}, {12360, 6, "Arcanite Bar"}, {4389, 4, "Gyrochronatom"},
                    {12655, 2, "Enchanted Thorium Bar"}, {14048, 40, "Bolt of Runecloth"},
                    {8170, 20, "Rugged Leather"}},
                20.f, 4.f, 2.f, 26.f * kDeg, 6.f, 8000, 15 * kGold, 3 * kGold, 40 * kGold,
                -14.f /* VERIFY in game: the gondola deck under the envelope */, 0.f});
            return v;
        }

        void PutU16(std::vector<uint8_t>& o, uint16_t v)
        {
            o.push_back(uint8_t(v));
            o.push_back(uint8_t(v >> 8));
        }
        void PutU64(std::vector<uint8_t>& o, uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }
        void PutF32(std::vector<uint8_t>& o, float f)
        {
            uint32_t u;
            std::memcpy(&u, &f, 4);
            for (int i = 0; i < 4; ++i)
                o.push_back(uint8_t(u >> (8 * i)));
        }
        uint16_t U16(std::vector<uint8_t> const& b, size_t at) { return uint16_t(b[at] | (b[at + 1] << 8)); }
        uint64_t U64(std::vector<uint8_t> const& b, size_t at)
        {
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i)
                v |= uint64_t(b[at + i]) << (8 * i);
            return v;
        }
        float F32(std::vector<uint8_t> const& b, size_t at)
        {
            uint32_t u = uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
            float f;
            std::memcpy(&f, &u, 4);
            return f;
        }
        constexpr size_t kPoseFixed = 1 + 8 + 4 + 8 * 4 + 2 + 1;   // through the name's length byte
    }

    std::vector<ClassInfo> const& Classes()
    {
        static std::vector<ClassInfo> const v = MakeClasses();
        return v;
    }

    ClassInfo const* Info(Class c)
    {
        for (ClassInfo const& i : Classes())
            if (i.cls == c)
                return &i;
        return nullptr;
    }

    ClassInfo const* InfoByEntry(uint32_t goEntry)
    {
        for (ClassInfo const& i : Classes())
            if (i.goEntry == goEntry)
                return &i;
        return nullptr;
    }

    ClassInfo const* InfoByDeed(uint32_t item)
    {
        for (ClassInfo const& i : Classes())
            if (i.deedItem == item)
                return &i;
        return nullptr;
    }

    ClassInfo const* InfoByName(std::string const& name)
    {
        std::string n;
        for (char ch : name)
            n += char(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        if (n == "sloop" || n == "sail" || n == "ship" || n == "1")
            return Info(Class::Sloop);
        if (n == "airship" || n == "zeppelin" || n == "air" || n == "2")
            return Info(Class::Airship);
        return nullptr;
    }

    void BesideSpot(float x, float y, float o, float& outX, float& outY)
    {
        // WoW's orientation turns counter-clockwise from +x, so the right hand is o - pi/2.
        float const right = o - 1.5707963f;
        outX = x + std::cos(right) * kTestBesideYards;
        outY = y + std::sin(right) * kTestBesideYards;
    }

    char const* NotchName(int8_t notch)
    {
        switch (notch)
        {
            case -1: return "Full Astern";
            case 0: return "All Stop";
            case 1: return "Slow Ahead";
            case 2: return "Half Ahead";
            case 3: return "Full Ahead";
            default: return "?";
        }
    }

    float NotchShare(int8_t notch)
    {
        switch (notch)
        {
            case -1: return 1.f;
            case 1: return 0.3f;
            case 2: return 0.6f;
            case 3: return 1.f;
            default: return 0.f;
        }
    }

    void Helm::Press(uint8_t newKeys)
    {
        newKeys &= KEY_ALL;
        uint8_t const pressed = uint8_t(newKeys & ~keys);
        if ((pressed & KEY_AHEAD) && !(newKeys & KEY_ASTERN) && notch < kNotchMax)
            ++notch;
        if ((pressed & KEY_ASTERN) && !(newKeys & KEY_AHEAD) && notch > kNotchMin)
            --notch;
        keys = newKeys;
    }

    float SpeedCap(ClassInfo const& info, bool neglected, uint32_t hull)
    {
        float cap = info.topSpeed;
        if (neglected)
            cap *= 0.5f;
        if (float(hull) < kListingShare * float(info.maxHull))
            cap *= 0.5f;
        return cap;
    }

    float ApproachSpeed(float speed, float target, float accel, float dt)
    {
        // Braking (toward zero, or reversing) bites twice as hard as speeding up.
        bool const braking = (speed > 0.f && target < speed) || (speed < 0.f && target > speed);
        float const step = accel * dt * (braking ? 2.f : 1.f);
        if (speed < target)
            return std::min(target, speed + step);
        return std::max(target, speed - step);
    }

    bool Sailable(ClassInfo const& info, Probe const& p, float& zOut)
    {
        if (!p.water)
            return false;
        if (p.waterZ - p.groundZ < info.draft)
            return false;   // a shoal or the shore
        zOut = p.waterZ;
        return true;
    }

    bool Flyable(Probe const& p, float wantZ, float& zOut, uint8_t& flags)
    {
        if (p.unfinished)
            return false;
        float floor = p.groundZ + kAirClearance;
        if (p.water && p.waterZ + kAirClearance > floor)
            floor = p.waterZ + kAirClearance;
        float ceiling = std::min({p.ceilingZ, kWorldCeiling, p.groundZ + kAirMaxAboveGround});
        float z = wantZ;
        if (z > ceiling)
        {
            z = ceiling;
            flags |= STEP_CEILING;
        }
        // Terrain rising faster than the ship can climb, or a ceiling under the ground's
        // clearance (a ward over a hill): refused.
        if (z < floor)
        {
            if (floor - z > 1.5f || floor > ceiling)
                return false;
            z = floor;
        }
        zOut = z;
        return true;
    }

    uint32_t UpkeepBlockCost(ClassInfo const& info) { return info.upkeepPerDay * kUpkeepBlockDays; }

    bool Neglected(uint32_t now, uint32_t upkeepUntil) { return now > upkeepUntil; }

    uint32_t DaysOverdue(uint32_t now, uint32_t upkeepUntil)
    {
        return now > upkeepUntil ? (now - upkeepUntil) / kDaySec : 0;
    }

    bool Impounded(uint32_t now, uint32_t upkeepUntil) { return DaysOverdue(now, upkeepUntil) >= kImpoundDays; }

    uint32_t NeglectWear(ClassInfo const& info, uint32_t upkeepUntil, uint32_t lastSweep, uint32_t now)
    {
        uint32_t const before = DaysOverdue(lastSweep, upkeepUntil);
        uint32_t const after = DaysOverdue(now, upkeepUntil);
        if (after <= before)
            return 0;
        return uint32_t(float(after - before) * kNeglectWearPerDay * float(info.maxHull));
    }

    uint32_t RepairCost(ClassInfo const& info, uint32_t hull)
    {
        if (hull >= info.maxHull)
            return 0;
        uint32_t const missing = info.maxHull - hull;
        // Per whole or part percent missing.
        uint32_t const pct = (missing * 100 + info.maxHull - 1) / info.maxHull;
        return pct * info.repairPerPct;
    }

    uint32_t SalvageCost(ClassInfo const& info) { return info.buildCopper / 100 * kSalvagePct; }

    uint32_t ScrapeLoss(ClassInfo const& info, float speed)
    {
        // 0.25% of the hull per yd/s over the scrape speed: a full-speed grounding costs about 2.5%.
        float const over = std::max(0.f, speed - kScrapeSpeed);
        return uint32_t(over * 0.0025f * float(info.maxHull));
    }

    uint8_t HullPct(ClassInfo const& info, uint32_t hull)
    {
        if (!info.maxHull)
            return 0;
        uint32_t const pct = (std::min(hull, info.maxHull) * 100 + info.maxHull - 1) / info.maxHull;
        return uint8_t(pct);
    }

    char const* RefusalText(Refusal r)
    {
        switch (r)
        {
            case Refusal::None: return "";
            case Refusal::NotOwner: return "That ship isn't yours.";
            case Refusal::NotAboard: return "You must be aboard your ship.";
            case Refusal::Wrecked: return "Your ship is a wreck. A Shipwright can salvage it.";
            case Refusal::Building: return "Your ship is still being built.";
            case Refusal::Anchored: return "The anchor is down.";
            case Refusal::TooFast: return "The ship is moving too fast to drop anchor.";
            case Refusal::Piloted: return "Someone is at the helm.";
            case Refusal::Passengers: return "There are people aboard.";
            case Refusal::NotDocked: return "Your ship must be moored at this dock.";
            case Refusal::Cooldown: return "The Ship's Bell isn't ready yet.";
            case Refusal::AlreadyOwns: return "You already own a ship of that kind.";
            case Refusal::NoDeed: return "You need the deed for it.";
            case Refusal::NoGold: return "You don't have enough money.";
            case Refusal::NoMaterials: return "You don't have all the materials.";
            case Refusal::NotReady: return "Your ship isn't finished yet.";
            case Refusal::Neglected: return "Your ship's upkeep is overdue.";
            case Refusal::Repaired: return "Your ship needs no repairs.";
            case Refusal::Dead: return "You can't steer while dead.";
            case Refusal::Downed: return "You can't steer while downed.";
            case Refusal::Mounted: return "Dismount to take the helm.";
            case Refusal::Dueling: return "Not during a companion duel.";
        }
        return "";
    }

    Refusal CanCommission(ClassInfo const& info, Purse const& p)
    {
        if (p.ownsClass)
            return Refusal::AlreadyOwns;
        if (!p.hasDeed)
            return Refusal::NoDeed;
        if (p.copper < info.buildCopper)
            return Refusal::NoGold;
        for (Material const& m : info.materials)
        {
            uint32_t have = 0;
            for (auto const& [entry, count] : p.items)
                if (entry == m.item)
                    have += count;
            if (have < m.count)
                return Refusal::NoMaterials;
        }
        return Refusal::None;
    }

    Refusal CanTakeHelm(bool owner, bool aboard, State s, bool piloted)
    {
        if (!owner)
            return Refusal::NotOwner;
        if (s == State::Wrecked)
            return Refusal::Wrecked;
        if (s == State::Building)
            return Refusal::Building;
        if (!aboard)
            return Refusal::NotAboard;
        if (piloted)
            return Refusal::Piloted;
        return Refusal::None;
    }

    Refusal HelmBodyRefusal(HelmBody const& b)
    {
        if (!b.alive)
            return Refusal::Dead;
        if (!b.aboard)
            return Refusal::NotAboard;
        if (b.downed)
            return Refusal::Downed;
        if (b.mounted)
            return Refusal::Mounted;
        if (b.dueling)
            return Refusal::Dueling;
        return Refusal::None;
    }

    Refusal CanAnchor(float speed) { return std::fabs(speed) > kAnchorSpeed ? Refusal::TooFast : Refusal::None; }

    Refusal CanRing(bool owner, State s, uint32_t now, uint32_t bellAt)
    {
        if (!owner)
            return Refusal::NotOwner;
        if (s == State::Wrecked)
            return Refusal::Wrecked;
        if (s == State::Building)
            return Refusal::Building;
        if (bellAt && now < bellAt + kBellCooldownSec)
            return Refusal::Cooldown;
        return Refusal::None;
    }

    Refusal CanSummon(bool owner, State s, bool piloted, bool passengers)
    {
        if (!owner)
            return Refusal::NotOwner;
        if (s == State::Wrecked)
            return Refusal::Wrecked;
        if (s == State::Building)
            return Refusal::Building;
        if (piloted)
            return Refusal::Piloted;
        if (passengers)
            return Refusal::Passengers;
        return Refusal::None;
    }

    Refusal CanRepair(bool owner, State s, bool docked, uint32_t hull, uint32_t maxHull)
    {
        if (!owner)
            return Refusal::NotOwner;
        if (s == State::Wrecked)
            return Refusal::Wrecked;
        if (s == State::Building)
            return Refusal::Building;
        if (hull >= maxHull)
            return Refusal::Repaired;
        if (!docked)
            return Refusal::NotDocked;
        return Refusal::None;
    }

    std::string DefaultName(std::string const& owner, ClassInfo const& info)
    {
        return owner + "'s " + info.name;
    }

    std::vector<uint8_t> EncodePose(Pose const& p)
    {
        std::vector<uint8_t> o;
        size_t const n = std::min<size_t>(p.name.size(), 48);
        o.reserve(kPoseFixed + n);
        o.push_back(kOpPose);
        PutU64(o, p.guid);
        o.push_back(p.cls);
        o.push_back(p.flags);
        o.push_back(uint8_t(p.notch));
        o.push_back(p.hullPct);
        for (float f : {p.x, p.y, p.z, p.o, p.vx, p.vy, p.vz, p.yawRate})
            PutF32(o, f);
        PutU16(o, p.bellSec);
        o.push_back(uint8_t(n));
        o.insert(o.end(), p.name.begin(), p.name.begin() + n);
        return o;
    }

    bool DecodePose(std::vector<uint8_t> const& b, Pose& out)
    {
        if (b.size() < kPoseFixed || b[0] != kOpPose)
            return false;
        size_t const n = b[kPoseFixed - 1];
        if (b.size() != kPoseFixed + n)
            return false;
        out.guid = U64(b, 1);
        out.cls = b[9];
        out.flags = b[10];
        out.notch = int8_t(b[11]);
        out.hullPct = b[12];
        float* f[] = {&out.x, &out.y, &out.z, &out.o, &out.vx, &out.vy, &out.vz, &out.yawRate};
        for (size_t i = 0; i < 8; ++i)
            *f[i] = F32(b, 13 + 4 * i);
        out.bellSec = U16(b, 45);
        out.name.assign(b.begin() + kPoseFixed, b.end());
        return true;
    }

    std::vector<uint8_t> EncodeGone(uint64_t guid)
    {
        std::vector<uint8_t> o{kOpGone};
        PutU64(o, guid);
        return o;
    }

    std::vector<uint8_t> EncodeHelm(uint64_t guid)
    {
        std::vector<uint8_t> o{kOpHelm};
        PutU64(o, guid);
        return o;
    }

    bool DecodeClient(std::vector<uint8_t> const& b, uint8_t& op, uint8_t& keys)
    {
        keys = 0;
        if (b.empty())
            return false;
        op = b[0];
        switch (op)
        {
            case kOpTakeHelm:
            case kOpLeaveHelm:
            case kOpAnchor:
            case kOpBell:
                return b.size() == 1;
            case kOpKeys:
                if (b.size() != 2 || (b[1] & ~KEY_ALL))
                    return false;
                keys = b[1];
                return true;
            default:
                return false;
        }
    }

    std::vector<uint8_t> EncodeKeys(uint8_t keys) { return {kOpKeys, keys}; }
}
