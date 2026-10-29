/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/SkyStreaks.h"

#include "fusion/Arsenal.h"
#include "fusion/Killstreak.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace Fusion
{
namespace Sky
{
    namespace
    {
        void PutU16(std::vector<uint8_t>& o, uint16_t v)
        {
            o.push_back(uint8_t(v));
            o.push_back(uint8_t(v >> 8));
        }
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
        void PutF(std::vector<uint8_t>& o, float f)
        {
            uint32_t v;
            std::memcpy(&v, &f, 4);
            PutU32(o, v);
        }
        uint16_t GetU16(std::vector<uint8_t> const& b, size_t i)
        {
            return uint16_t(b[i] | b[i + 1] << 8);
        }
        uint64_t GetU64(std::vector<uint8_t> const& b, size_t i)
        {
            uint64_t v = 0;
            for (int k = 7; k >= 0; --k)
                v = v << 8 | b[i + size_t(k)];
            return v;
        }
        void PutStr8(std::vector<uint8_t>& o, std::string const& s)
        {
            size_t const n = std::min(s.size(), kNewsNameMax);
            o.push_back(uint8_t(n));
            o.insert(o.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
        }
        uint32_t GetU32(std::vector<uint8_t> const& b, size_t i)
        {
            return uint32_t(b[i]) | uint32_t(b[i + 1]) << 8 | uint32_t(b[i + 2]) << 16 | uint32_t(b[i + 3]) << 24;
        }
        float GetF(std::vector<uint8_t> const& b, size_t i)
        {
            uint32_t const v = GetU32(b, i);
            float f;
            std::memcpy(&f, &v, 4);
            return f;
        }
        float Flat(Vec3 a, Vec3 b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }
        // An angle folded into [-pi, pi).
        float Wrap(float a)
        {
            a = std::fmod(a + kPi, 2.f * kPi);
            if (a < 0.f)
                a += 2.f * kPi;
            return a - kPi;
        }
    }

    Look LookOf(Craft craft, Team team)
    {
        bool const horde = team == Team::Horde;
        switch (craft)
        {
            case Craft::Rocket:
                return {kEntryRocket, Display::kGoblinRocketCar, 0, 1.f};
            case Craft::Rider:
                return horde ? Look{kEntryRider, Display::kOrcRider, Display::kWindRiderMount, 1.6f}
                             : Look{kEntryRider, Display::kDwarfRider, Display::kGryphonMount, 1.6f};
            case Craft::Gyrocopter:
                return {kEntryGyrocopter, Display::kFlyingMachine, 0, 2.f};
            case Craft::Hornet:
                return {kEntryHornet, Display::kFlyingMachine, 0, 1.4f};
            case Craft::Bomber:
                // No zeppelin is a creature in 1.12 (the zeppelins are transports): the Horde's
                // bomber is a big Kor'kron wind rider, the Alliance's a big dwarven flying machine.
                return horde ? Look{kEntryBomber, Display::kOrcRider, Display::kWindRiderMount, 2.4f}
                             : Look{kEntryBomber, Display::kFlyingMachine, 0, 3.f};
            case Craft::Turret:
                return {kEntryTurret, Display::kCannon, 0, 0.7f};
        }
        return {};
    }

    uint32_t StreakOf(Craft craft)
    {
        switch (craft)
        {
            case Craft::Rocket: return StreakId::PredatorMissile;
            case Craft::Rider: return StreakId::AttackHelicopter;
            case Craft::Gyrocopter: return StreakId::ChopperGunner;
            case Craft::Hornet: return StreakId::HarrierStrike;
            case Craft::Bomber: return StreakId::StealthBomber;
            case Craft::Turret: return StreakId::SentryGun;
        }
        return 0;
    }

    std::optional<Craft> CraftOfEntry(uint32_t entry)
    {
        switch (entry)
        {
            case kEntryRocket: return Craft::Rocket;
            case kEntryRider: return Craft::Rider;
            case kEntryGyrocopter: return Craft::Gyrocopter;
            case kEntryBomber: return Craft::Bomber;
            case kEntryTurret: return Craft::Turret;
            case kEntryHornet: return Craft::Hornet;
            default: return std::nullopt;
        }
    }

    // ── Standing up to fire ──

    float CraftToughness(Craft craft)
    {
        // In fresh players' health: a gunship takes most of a magazine of steady fire or a
        // launcher's rocket or two (kCraftBlastScale), the heavy one half again as much; the
        // hornet is lighter; the turret takes a few bursts or one grenade well placed.
        switch (craft)
        {
            case Craft::Rocket: return 0.f;
            case Craft::Rider: return 8.f;
            case Craft::Gyrocopter: return 12.f;
            case Craft::Hornet: return 6.f;
            case Craft::Bomber: return 10.f;
            case Craft::Turret: return 5.f;
        }
        return 0.f;
    }

    uint32_t CraftMaxHealth(Craft craft, float playerHealth)
    {
        float const t = CraftToughness(craft);
        if (t <= 0.f)
            return 0;
        return std::max<uint32_t>(1, uint32_t(t * std::max(1.f, playerHealth) + 0.5f));
    }

    bool CraftShootable(Craft craft)
    {
        return CraftToughness(craft) > 0.f;
    }

    uint8_t HealthPercent(uint32_t health, uint32_t maxHealth)
    {
        if (maxHealth == 0 || health == 0)
            return 0;
        uint64_t const pct = uint64_t(health) * 100u / maxHealth;
        return uint8_t(std::clamp<uint64_t>(pct, 1, 100));
    }

    Hitbox CraftHitbox(Craft craft, float scale)
    {
        scale = std::max(0.1f, scale);
        switch (craft)
        {
            case Craft::Rider: return {1.5f * scale, 2.4f * scale};
            case Craft::Gyrocopter:
            case Craft::Hornet: return {1.3f * scale, 2.f * scale};
            case Craft::Bomber: return {1.5f * scale, 2.4f * scale};
            case Craft::Turret: return {std::max(0.7f, 1.1f * scale), std::max(1.2f, 2.f * scale)};
            case Craft::Rocket: return {0.6f, 0.8f};
        }
        return {};
    }

    CraftSounds SoundsOf(Craft craft, Team team)
    {
        bool const horde = team == Team::Horde;
        CraftSounds s;
        switch (craft)
        {
            case Craft::Rider:
                s.beat = Sound::kWingFlapHuge;
                s.beatMs = 1300;
                s.arrive = horde ? Sound::kWyvernAggro : Sound::kGryphonAggro;
                s.destroyed = Sound::kBombBotDeath;
                break;
            case Craft::Gyrocopter:
            case Craft::Hornet:
                s.loop = Sound::kFlyingMachineLoop;
                s.destroyed = Sound::kBombBotDeath;
                break;
            case Craft::Bomber:
                if (horde)
                {
                    s.beat = Sound::kWingFlapHuge;
                    s.beatMs = 1100;
                    s.arrive = Sound::kWyvernAggro;
                }
                else
                    s.loop = Sound::kFlyingMachineLoop;
                s.destroyed = Sound::kBombBotDeath;
                break;
            case Craft::Turret:
                s.destroyed = Sound::kCannonDeath;
                break;
            case Craft::Rocket:
                break;
        }
        return s;
    }

    std::optional<Craft> CraftOf(uint32_t streakId)
    {
        switch (streakId)
        {
            case StreakId::PredatorMissile: return Craft::Rocket;
            case StreakId::AttackHelicopter: return Craft::Rider;
            case StreakId::ChopperGunner: return Craft::Gyrocopter;
            case StreakId::HarrierStrike: return Craft::Hornet;
            case StreakId::StealthBomber: return Craft::Bomber;
            case StreakId::SentryGun: return Craft::Turret;
            default: return std::nullopt;
        }
    }

    // ── Orbits ──

    namespace
    {
        float AngleAt(Orbit const& o, uint32_t ms)
        {
            float const omega = o.radius > 0.1f ? o.speed / o.radius : 0.f;
            return o.startAngle + omega * float(ms) / 1000.f;
        }
    }

    Vec3 OrbitAt(Orbit const& o, uint32_t ms)
    {
        float const a = AngleAt(o, ms);
        return {o.centre.x + o.radius * std::cos(a), o.centre.y + o.radius * std::sin(a), o.centre.z + o.height};
    }

    float OrbitFacing(Orbit const& o, uint32_t ms)
    {
        return Wrap(AngleAt(o, ms) + kPi / 2.f);
    }

    std::vector<Vec3> OrbitPath(Orbit const& o, uint32_t ms, uint32_t spanMs, uint32_t stepMs)
    {
        std::vector<Vec3> out;
        stepMs = std::max<uint32_t>(stepMs, 50);
        for (uint32_t t = 0; t < spanMs; t += stepMs)
            out.push_back(OrbitAt(o, ms + t));
        out.push_back(OrbitAt(o, ms + spanMs));
        return out;
    }

    float StartAngle(float facing, uint32_t sequence)
    {
        // Helicopters are spaced by the golden angle.
        return Wrap(facing + std::fmod(float(sequence) * 2.39996323f, 2.f * kPi));
    }

    Vec3 ArriveFrom(Orbit const& o)
    {
        Vec3 const out{std::cos(o.startAngle), std::sin(o.startAngle), 0.f};
        return o.centre + out * kArriveFrom + Vec3{0.f, 0.f, o.height + kArriveRise};
    }

    Vec3 LeaveTo(Vec3 at, float facing)
    {
        return at + Vec3{std::cos(facing), std::sin(facing), 0.f} * kLeaveTo + Vec3{0.f, 0.f, kLeaveRise};
    }

    uint32_t FlightMs(Vec3 from, Vec3 to, float speed)
    {
        return uint32_t((to - from).Length() / std::max(1.f, speed) * 1000.f + 0.5f);
    }

    // ── Targets ──

    float TargetScore(Target const& t, float distance, uint64_t current)
    {
        // The weights are wider than any reach (75 yards), so each rule outranks distance; the
        // current target's hold is a near tie's worth.
        return distance - (t.threat ? 400.f : 0.f) - (t.atCraft ? 300.f : 0.f) - (t.player ? 150.f : 0.f)
            - (t.inCombat ? 80.f : 0.f) - (current && t.guid == current ? 12.f : 0.f);
    }

    namespace
    {
        // The best candidate in sight: ranked first, then tested for sight in that order, a few
        // at the most.
        std::optional<uint64_t> BestInSight(Vec3 from, std::vector<std::pair<float, Target const*>>& ranked,
            Sight const& sight)
        {
            std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) {
                return a.first < b.first || (a.first == b.first && a.second->guid < b.second->guid);
            });
            uint32_t tests = 0;
            for (auto const& r : ranked)
            {
                if (!sight)
                    return r.second->guid;
                if (tests++ >= kMaxSightTests)
                    break;
                if (sight(from, r.second->chest))
                    return r.second->guid;
            }
            return std::nullopt;
        }
    }

    std::optional<uint64_t> AirTarget(Vec3 gun, Vec3 centre, float patrol, float reach,
        std::vector<Target> const& targets, Sight const& sight, uint64_t current)
    {
        std::vector<std::pair<float, Target const*>> ranked;
        for (Target const& t : targets)
        {
            if (!t.alive || !t.hostile || Flat(t.chest, centre) > patrol)
                continue;
            float const d = (t.chest - gun).Length();
            if (d > reach)
                continue;
            ranked.push_back({TargetScore(t, d, current), &t});
        }
        return BestInSight(gun, ranked, sight);
    }

    float WalkInChance(float chance, uint32_t roundsOnTarget)
    {
        if (roundsOnTarget >= kWalkInRounds)
            return chance;
        return chance * (0.4f + 0.6f * float(roundsOnTarget) / float(kWalkInRounds));
    }

    // ── The turret ──

    bool TurretCovers(Vec3 at, float facing, Vec3 to)
    {
        if ((to - at).Length() > kTurretRange || Flat(to, at) < 0.05f)
            return false;
        float const yaw = std::atan2(to.y - at.y, to.x - at.x);
        return std::fabs(Wrap(yaw - facing)) <= kTurretHalfArc;
    }

    std::optional<uint64_t> TurretTarget(Vec3 muzzle, float facing, std::vector<Target> const& targets,
        Sight const& sight, uint64_t current)
    {
        std::vector<std::pair<float, Target const*>> ranked;
        for (Target const& t : targets)
        {
            if (!t.alive || !t.hostile || !TurretCovers(muzzle, facing, t.chest))
                continue;
            ranked.push_back({TargetScore(t, (t.chest - muzzle).Length(), current), &t});
        }
        return BestInSight(muzzle, ranked, sight);
    }

    float TurretAimYaw(Vec3 at, Vec3 to)
    {
        return std::atan2(to.y - at.y, to.x - at.x);
    }

    float TurretIdleYaw(float facing, uint32_t ms)
    {
        float const phase = 2.f * kPi * float(ms % kTurretSweepMs) / float(kTurretSweepMs);
        return Wrap(facing + kTurretHalfArc * kTurretSweep * std::sin(phase));
    }

    bool TurretShouldTurn(float shown, float want)
    {
        return std::fabs(Wrap(want - shown)) >= kTurretTurnStep;
    }

    Placement CheckPlacement(Spot const& s)
    {
        if (s.standing >= kMaxTurrets)
            return Placement::TooMany;
        if (Flat(s.feet, s.spot) > kPlaceReach + 0.25f)
            return Placement::TooFar;
        if (!s.ground || !std::isfinite(*s.ground) || std::fabs(*s.ground - s.spot.z) > kPlaceGroundSlack)
            return Placement::NoGround;
        if (std::fabs(*s.ground - s.feet.z) > kPlaceRise)
            return Placement::TooSteep;
        for (int i = 0; i < 4; ++i)
            if (!s.corner[i] || std::fabs(s.corners[i] - *s.ground) > kPlaceStep)
                return Placement::TooSteep;
        if (!s.sight)
            return Placement::NoSight;
        return Placement::Ok;
    }

    char const* PlacementText(Placement p)
    {
        switch (p)
        {
            case Placement::Ok: return "Turret set down.";
            case Placement::TooFar: return "Too far: set the turret down within 6 yards.";
            case Placement::NoGround: return "The turret needs solid ground there.";
            case Placement::TooSteep: return "Too steep for the turret.";
            case Placement::NoSight: return "You can't reach that spot.";
            case Placement::TooMany: return "You already have two turrets out.";
        }
        return "";
    }

    // ── Bombing runs ──

    BombRun MakeBombRun(Vec3 centre, float facing, uint32_t bombs, float length, float height, float speed, float lead)
    {
        BombRun r;
        r.centre = centre;
        r.dir = {std::cos(facing), std::sin(facing), 0.f};
        r.bombs = std::max<uint32_t>(1, bombs);
        r.length = std::max(0.f, length);
        r.height = height;
        r.speed = std::max(1.f, speed);
        r.lead = lead;
        return r;
    }

    float BombOffset(BombRun const& r, uint32_t i)
    {
        if (r.bombs <= 1)
            return 0.f;
        i = std::min(i, r.bombs - 1);
        return -r.length / 2.f + r.length * float(i) / float(r.bombs - 1);
    }

    Vec3 BombPoint(BombRun const& r, uint32_t i)
    {
        return r.centre + r.dir * BombOffset(r, i);
    }

    Vec3 BomberAt(BombRun const& r, uint32_t ms)
    {
        float const along = -r.lead - r.length / 2.f + r.speed * float(ms) / 1000.f;
        return r.centre + r.dir * along + Vec3{0.f, 0.f, r.height};
    }

    uint32_t ReleaseMs(BombRun const& r, uint32_t i)
    {
        float const along = r.lead + r.length / 2.f + BombOffset(r, i);
        return uint32_t(along / r.speed * 1000.f + 0.5f);
    }

    uint32_t RunMs(BombRun const& r)
    {
        return uint32_t((2.f * r.lead + r.length) / r.speed * 1000.f + 0.5f);
    }

    Vec3 RunStart(BombRun const& r) { return BomberAt(r, 0); }
    Vec3 RunEnd(BombRun const& r) { return BomberAt(r, RunMs(r)); }

    uint32_t LandMs(BombRun const& r, uint32_t i)
    {
        return ReleaseMs(r, i) + kBombFallMs;
    }

    // ── The craft's news ──

    std::vector<uint8_t> EncodeCraftUp(uint32_t streak, uint64_t guid, uint16_t seconds, uint8_t healthPercent)
    {
        std::vector<uint8_t> o{kOpCraft, uint8_t(News::Up), uint8_t(streak)};
        PutU64(o, guid);
        PutU16(o, seconds);
        o.push_back(std::min<uint8_t>(healthPercent, 100));
        return o;
    }

    std::vector<uint8_t> EncodeCraftGone(uint64_t guid, GoneWhy why)
    {
        std::vector<uint8_t> o{kOpCraft, uint8_t(News::Gone)};
        PutU64(o, guid);
        o.push_back(uint8_t(why));
        return o;
    }

    std::vector<uint8_t> EncodeCraftKill(uint32_t streak, std::string const& victim)
    {
        std::vector<uint8_t> o{kOpCraft, uint8_t(News::Kill), uint8_t(streak)};
        PutStr8(o, victim);
        return o;
    }

    std::vector<uint8_t> EncodeCraftDowned(uint32_t streak, bool yours, std::string const& other)
    {
        std::vector<uint8_t> o{kOpCraft, uint8_t(News::Downed), uint8_t(streak), uint8_t(yours ? 1 : 0)};
        PutStr8(o, other);
        return o;
    }

    std::vector<uint8_t> EncodeStrikeLine(uint8_t flags, uint16_t firstMs, uint16_t lastMs, std::vector<Vec3> const& points)
    {
        std::vector<uint8_t> o{kOpCraft, uint8_t(News::StrikeLine), flags};
        PutU16(o, firstMs);
        PutU16(o, lastMs);
        size_t const n = std::min<size_t>(points.size(), kMaxLinePoints);
        o.push_back(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            PutF(o, points[i].x);
            PutF(o, points[i].y);
            PutF(o, points[i].z);
        }
        return o;
    }

    std::optional<NewsMsg> DecodeNews(std::vector<uint8_t> const& b)
    {
        if (b.size() < 2 || b[0] != kOpCraft)
            return std::nullopt;
        NewsMsg m;
        auto str8 = [&](size_t at) -> bool {
            if (b.size() < at + 1 || b.size() != at + 1 + b[at])
                return false;
            m.name.assign(b.begin() + std::ptrdiff_t(at + 1), b.end());
            return true;
        };
        switch (News(b[1]))
        {
            case News::Up:
                if (b.size() != 14)
                    return std::nullopt;
                m.kind = News::Up;
                m.streak = b[2];
                m.guid = GetU64(b, 3);
                m.seconds = GetU16(b, 11);
                m.health = b[13];
                return m;
            case News::Gone:
                if (b.size() != 11 || b[10] > uint8_t(GoneWhy::Dismissed))
                    return std::nullopt;
                m.kind = News::Gone;
                m.guid = GetU64(b, 2);
                m.why = GoneWhy(b[10]);
                return m;
            case News::Kill:
                m.kind = News::Kill;
                if (b.size() < 3)
                    return std::nullopt;
                m.streak = b[2];
                if (!str8(3))
                    return std::nullopt;
                return m;
            case News::Downed:
                m.kind = News::Downed;
                if (b.size() < 4)
                    return std::nullopt;
                m.streak = b[2];
                m.yours = b[3] != 0;
                if (!str8(4))
                    return std::nullopt;
                return m;
            case News::StrikeLine:
            {
                if (b.size() < 8)
                    return std::nullopt;
                m.kind = News::StrikeLine;
                m.flags = b[2];
                m.firstMs = GetU16(b, 3);
                m.lastMs = GetU16(b, 5);
                size_t const n = b[7];
                if (n > kMaxLinePoints || b.size() != 8 + n * 12)
                    return std::nullopt;
                for (size_t i = 0; i < n; ++i)
                {
                    Vec3 const p{GetF(b, 8 + i * 12), GetF(b, 12 + i * 12), GetF(b, 16 + i * 12)};
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                        return std::nullopt;
                    m.points.push_back(p);
                }
                return m;
            }
        }
        return std::nullopt;
    }

    // ── The Goblin Rocket ──

    RocketPose RocketStart(Vec3 target, float facing)
    {
        RocketPose p;
        Vec3 const back{std::cos(facing), std::sin(facing), 0.f};
        p.pos = target - back * kRocketBack + Vec3{0.f, 0.f, kRocketHeight};
        p.yaw = Wrap(facing);
        p.pitch = Clamp(std::atan2(-kRocketHeight, kRocketBack), kRocketPitchMin, kRocketPitchMax);
        return p;
    }

    Vec3 RocketForward(RocketPose const& p)
    {
        float const c = std::cos(p.pitch);
        return {c * std::cos(p.yaw), c * std::sin(p.yaw), std::sin(p.pitch)};
    }

    float RocketSpeed(RocketPose const& p)
    {
        return p.boost ? kRocketBoost : kRocketSpeed;
    }

    RocketPose SteerRocket(RocketPose p, float mouseX, float mouseY, float dt)
    {
        dt = std::max(0.f, dt);
        float const most = kRocketMaxTurnRate * dt;
        // Right turns right (WoW's yaw runs counter-clockwise), down noses down.
        p.yaw = Wrap(p.yaw - Clamp(mouseX * kRocketTurn, -most, most));
        p.pitch = Clamp(p.pitch - Clamp(mouseY * kRocketTurn, -most, most), kRocketPitchMin, kRocketPitchMax);
        p.pos = p.pos + RocketForward(p) * (RocketSpeed(p) * dt);
        return p;
    }

    bool RocketStepOk(Vec3 from, Vec3 to, uint32_t dtMs)
    {
        // Two reports can arrive together: a quarter second at the least, half again the boost.
        float const secs = float(std::max<uint32_t>(dtMs, 250)) / 1000.f;
        return (to - from).Length() <= kRocketBoost * secs * 1.5f + 5.f;
    }

    bool RocketReachOk(Vec3 start, Vec3 at, uint32_t elapsedMs)
    {
        return (at - start).Length() <= kRocketBoost * float(elapsedMs) / 1000.f * 1.2f + 10.f;
    }

    std::vector<uint8_t> EncodeRocket(bool start, uint32_t ticket, uint64_t vehicle, RocketPose const& pose, uint16_t lifeMs)
    {
        std::vector<uint8_t> o{kOpRocket, uint8_t(start ? 1 : 0)};
        PutU32(o, ticket);
        PutU64(o, vehicle);
        PutF(o, pose.pos.x);
        PutF(o, pose.pos.y);
        PutF(o, pose.pos.z);
        PutF(o, pose.yaw);
        PutF(o, pose.pitch);
        PutU16(o, lifeMs);
        return o;
    }

    std::optional<RocketReportMsg> DecodeRocketReport(std::vector<uint8_t> const& b)
    {
        if (b.size() != 18 || b[0] != kOpRocketReport || b[5] > uint8_t(RocketReport::Bail))
            return std::nullopt;
        RocketReportMsg m;
        m.ticket = GetU32(b, 1);
        m.kind = RocketReport(b[5]);
        m.at = {GetF(b, 6), GetF(b, 10), GetF(b, 14)};
        if (!std::isfinite(m.at.x) || !std::isfinite(m.at.y) || !std::isfinite(m.at.z))
            return std::nullopt;
        return m;
    }

    // ── `.streak` ──

    namespace
    {
        // Lower case, letters and digits only ("Goblin Rocket" and "goblin_rocket" alike).
        std::string Squash(std::string const& s)
        {
            std::string out;
            for (char ch : s)
                if (std::isalnum(static_cast<unsigned char>(ch)))
                    out.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
            return out;
        }
    }

    std::optional<uint32_t> StreakByName(std::string const& name, std::vector<uint32_t> const& allowed)
    {
        std::string const n = Squash(name);
        if (n.empty())
            return std::nullopt;
        auto ok = [&](uint32_t id) -> std::optional<uint32_t> {
            if (std::find(allowed.begin(), allowed.end(), id) != allowed.end())
                return id;
            return std::nullopt;
        };
        if (std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return n.size() <= 3 ? ok(uint32_t(std::stoul(n))) : std::nullopt;
        struct Word
        {
            char const* word;
            uint32_t id;
        };
        static Word const kWords[] = {
            {"uav", StreakId::Uav}, {"gyro", StreakId::Uav}, {"spygyro", StreakId::Uav},
            {"sentry", StreakId::SentryGun}, {"turret", StreakId::SentryGun},
            {"predator", StreakId::PredatorMissile}, {"rocket", StreakId::PredatorMissile},
            {"missile", StreakId::PredatorMissile},
            {"harrier", StreakId::HarrierStrike}, {"hornets", StreakId::HarrierStrike},
            {"hornet", StreakId::HarrierStrike},
            {"helicopter", StreakId::AttackHelicopter}, {"heli", StreakId::AttackHelicopter},
            {"gunship", StreakId::AttackHelicopter}, {"rider", StreakId::AttackHelicopter},
            {"gryphon", StreakId::AttackHelicopter}, {"windrider", StreakId::AttackHelicopter},
            {"stealth", StreakId::StealthBomber}, {"bomber", StreakId::StealthBomber},
            {"airstrike", StreakId::StealthBomber}, {"zeppelin", StreakId::StealthBomber},
            {"chopper", StreakId::ChopperGunner}, {"choppergunner", StreakId::ChopperGunner},
            {"gyrocopter", StreakId::ChopperGunner},
            {"nuke", StreakId::TacticalNuke},
        };
        for (Word const& w : kWords)
            if (n == w.word)
                return ok(w.id);
        for (uint32_t id = 1; id <= StreakId::TacticalNuke; ++id)
            if (n == Squash(Arsenal::StreakKey(id)) || n == Squash(Arsenal::StreakName(id)))
                return ok(id);
        return std::nullopt;
    }
}
}
