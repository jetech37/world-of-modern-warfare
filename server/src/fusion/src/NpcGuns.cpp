/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/NpcGuns.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace Fusion
{
    namespace
    {
        // A stable spread over entries (the same kind decides the same way every time).
        uint32_t Mix(uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }

        void Put16(std::vector<uint8_t>& o, uint16_t v)
        {
            o.push_back(uint8_t(v));
            o.push_back(uint8_t(v >> 8));
        }

        void Put32(std::vector<uint8_t>& o, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }

        void Put64(std::vector<uint8_t>& o, uint64_t v)
        {
            Put32(o, uint32_t(v));
            Put32(o, uint32_t(v >> 32));
        }

        void PutF(std::vector<uint8_t>& o, float f)
        {
            uint32_t v;
            std::memcpy(&v, &f, 4);
            Put32(o, v);
        }

        Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

        bool Labourer(std::string name)
        {
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            for (char const* w : {"miner", "peon", "slave", "peasant", "laborer", "labourer", "worker",
                     "digger", "servant", "prisoner", "drudge", "excavator", "porter"})
                if (name.find(w) != std::string::npos)
                    return true;
            return false;
        }
    }

    std::vector<GunnerCamp> const& GunnerCamps()
    {
        // Faction templates from the world DB (creature_template.faction), with about how many
        // spawns each camp holds in 1.12's world; names where a camp shares a template.
        static std::vector<GunnerCamp> const camps = {
            {"Defias Brotherhood", {17, 27}, {"Defias"}, 85},                       // ~1,300 spawns
            {"Scarlet Crusade", {67, 89}, {"Scarlet", "Crimson"}, 75},              // ~1,650
            {"Dark Iron Dwarves", {54, 63}, {"Dark Iron", "Anvilrage", "Shadowforge"}, 85},  // ~1,500
            {"Syndicate", {87}, {"Syndicate"}, 85},                                 // ~590
            {"Venture Company", {47}, {"Venture Co"}, 90},                          // ~1,180 (its miners stay melee)
            {"Pirates", {119, 230}, {"Bloodsail", "Southsea"}, 85},                 // ~630
            {"Kurzen's Rebels", {46}, {"Kurzen"}, 90},                              // ~230
            {"Bael'dun Expedition", {122}, {"Bael'dun"}, 90},                       // ~220
            {"Wastewander Bandits", {}, {"Wastewander"}, 85},                       // ~190
            {"Twilight's Hammer", {128}, {"Twilight"}, 60},                         // ~540
            {"Theramore Deserters", {}, {"Theramore Deserter"}, 85},                // ~36
            {"Blackrock Clan", {40}, {"Blackrock"}, 50},                            // ~720
            {"Dragonmaw Clan", {62}, {"Dragonmaw"}, 50},                            // ~270
            {"Burning Blade", {554}, {"Burning Blade", "Searing Blade"}, 40},       // ~400
        };
        return camps;
    }

    GunnerCamp const* CampOf(uint32_t faction, std::string const& name)
    {
        for (GunnerCamp const& c : GunnerCamps())
        {
            if (faction && std::find(c.factions.begin(), c.factions.end(), faction) != c.factions.end())
                return &c;
            for (char const* part : c.names)
                if (name.find(part) != std::string::npos)
                    return &c;
        }
        return nullptr;
    }

    GunRole ClassifyGunner(GunFacts const& f)
    {
        if (f.type != CreatureType::Humanoid || f.boss)
            return GunRole::None;
        if (f.rangedWeapon)
            return GunRole::Gunner;
        if (f.caster)
            return GunRole::None;
        if (Labourer(f.name))
            return GunRole::MeleeTrash;
        GunnerCamp const* camp = CampOf(f.faction, f.name);
        uint32_t const share = camp ? camp->sharePercent : kWildGunnerSharePercent;
        return Mix(f.entry) % 100 < share ? GunRole::Gunner : GunRole::MeleeTrash;
    }

    std::string NpcGunFor(uint32_t entry, uint32_t level, CreatureRank rank)
    {
        static std::vector<char const*> const low = {"usp_mp", "beretta_mp", "pp2000_mp", "mp5k_mp", "ump45_mp", "spas12_mp"};
        static std::vector<char const*> const mid = {"ump45_mp", "mp5k_mp", "kriss_mp", "m4_mp", "famas_mp", "m1014_mp", "fal_mp"};
        static std::vector<char const*> const high = {"ak47_mp", "scar_mp", "tavor_mp", "fn2000_mp", "m16_mp", "fal_mp"};
        static std::vector<char const*> const rifles = {"m4_mp", "famas_mp", "ak47_mp", "scar_mp", "fal_mp"};
        static std::vector<char const*> const heavy = {"m240_mp", "rpd_mp", "mg4_mp", "sa80_mp", "ak47_mp", "scar_mp"};
        static std::vector<char const*> const lmgs = {"m240_mp", "rpd_mp", "mg4_mp"};
        static std::vector<char const*> const snipers = {"m21_mp", "wa2000_mp", "barrett_mp", "cheytac_mp"};
        static std::vector<char const*> const lightSnipers = {"m21_mp", "wa2000_mp"};
        uint32_t const pick = Mix(entry * 2654435761u + 7u);
        auto from = [pick](std::vector<char const*> const& list) { return std::string(list[pick % list.size()]); };
        if (rank == CreatureRank::RareElite && level >= 20)
            return level < 40 ? from(lightSnipers) : from(snipers);
        // An elite is two steps up the ladder, a rare one.
        bool const elite = rank == CreatureRank::Elite || rank == CreatureRank::RareElite;
        uint32_t const lvl = level + (elite ? 20 : rank == CreatureRank::Rare ? 10 : 0);
        if (lvl < 20)
            return from(low);
        if (lvl < 40)
            return from(mid);
        if (elite)
            return level >= 40 ? from(heavy) : from(rifles);
        // One normal kind in six past 45 hauls an LMG.
        if (lvl >= 45 && (pick >> 8) % 6 == 0)
            return from(lmgs);
        return from(high);
    }

    std::string NpcGunFor(uint32_t entry, uint32_t level)
    {
        return NpcGunFor(entry, level, CreatureRank::Normal);
    }

    uint32_t NpcGunItemFor(uint32_t level)
    {
        if (level < 14) return 2508;    // Old Blunderbuss
        if (level < 21) return 3023;    // Large Bore Blunderbuss
        if (level < 31) return 3024;    // BKP 2700 "Enforcer"
        if (level < 43) return 3025;    // BKP 42 "Ultra"
        if (level < 50) return 4089;    // Ricochet Blunderbuss
        return 16004;                   // Dark Iron Rifle
    }

    uint32_t NpcHitDamage(float codDamage, float creatureMultiplier, float armorCut)
    {
        float const damage = codDamage * creatureMultiplier * (1.f - Clamp(armorCut, 0.f, 1.f));
        return std::max<uint32_t>(1, uint32_t(damage + 0.5f));
    }

    bool NpcTrigger::Fire(uint32_t nowMs, uint32_t burst, uint32_t gapMs, uint32_t pauseMs)
    {
        if (!started)
        {
            started = true;
            nextMs = nowMs;
            burstLeft = burst;
        }
        if (int32_t(nowMs - nextMs) < 0)
            return false;
        if (burstLeft == 0)
            burstLeft = burst;
        --burstLeft;
        nextMs = nowMs + (burstLeft == 0 ? pauseMs : gapMs);
        return true;
    }

    float GunnerHoldRange(WeaponClass c)
    {
        switch (c)
        {
            case WeaponClass::Shotgun:
            case WeaponClass::Smg:
            case WeaponClass::MachinePistol:
            case WeaponClass::Pistol:
                return 10.f;
            case WeaponClass::Sniper:
                return 35.f;
            default:
                return 20.f;
        }
    }

    bool GunnerShouldAdvance(float distance, bool lineOfSight, float holdRange)
    {
        return !lineOfSight || distance > holdRange || distance <= kGunnerKnifeRange;
    }

    std::vector<uint8_t> EncodeNpcFire(uint64_t npc, uint16_t gun, bool hit, Vec3 to)
    {
        std::vector<uint8_t> o{kOpNpcFire};
        Put64(o, npc);
        Put16(o, gun);
        o.push_back(hit ? 1 : 0);
        PutF(o, to.x);
        PutF(o, to.y);
        PutF(o, to.z);
        return o;
    }

    std::vector<uint8_t> EncodeNpcGuns(std::vector<NpcGunEntry> const& gunners)
    {
        size_t const n = std::min<size_t>(gunners.size(), kMaxNpcGunsListed);
        std::vector<uint8_t> o{kOpNpcGuns, uint8_t(n)};
        for (size_t i = 0; i < n; ++i)
        {
            Put64(o, gunners[i].guid);
            Put16(o, gunners[i].gun);
        }
        return o;
    }

    std::vector<uint8_t> EncodePlayerRound(uint64_t shooter, uint16_t gun, bool hit, Vec3 from, Vec3 to)
    {
        std::vector<uint8_t> o{kOpPlayerRound};
        Put64(o, shooter);
        Put16(o, gun);
        o.push_back(hit ? 1 : 0);
        for (Vec3 const v : {from, to})
        {
            PutF(o, v.x);
            PutF(o, v.y);
            PutF(o, v.z);
        }
        return o;
    }

    Vec3 NpcMissPoint(Vec3 muzzle, Vec3 chest, float angle01, float radius01)
    {
        return NpcMissPointAt(muzzle, chest, angle01, 1.f + Clamp(radius01, 0.f, 1.f));
    }

    Vec3 NpcMissPointAt(Vec3 muzzle, Vec3 chest, float angle01, float offYards)
    {
        Vec3 along = chest - muzzle;
        along = along.Length() > 1e-4f ? along.Normalized() : Vec3{1.f, 0.f, 0.f};
        // Two directions square to the line of fire: beside it, and over it.
        Vec3 side = Cross(along, {0.f, 0.f, 1.f});
        side = side.Length() > 1e-4f ? side.Normalized() : Vec3{0.f, 1.f, 0.f};
        Vec3 const over = Cross(side, along).Normalized();
        float const turn = Clamp(angle01, 0.f, 1.f) * 6.2831853f;
        return chest + (side * std::cos(turn) + over * std::sin(turn)) * std::max(0.f, offYards);
    }

    namespace
    {
        constexpr float kPi = 3.14159265f;

        // The turn from b to a, in [-pi, pi].
        float AngleDiff(float a, float b)
        {
            float d = std::fmod(a - b, 2.f * kPi);
            if (d > kPi)
                d -= 2.f * kPi;
            else if (d < -kPi)
                d += 2.f * kPi;
            return d;
        }

        float Bearing(Vec3 from, Vec3 to)
        {
            return std::atan2(to.y - from.y, to.x - from.x);
        }

        float Flat(Vec3 a, Vec3 b)
        {
            return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
        }

        Vec3 Around(Vec3 centre, float radius, float angle)
        {
            return {centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle), centre.z};
        }

        uint32_t TravelMs(float yards)
        {
            return uint32_t(std::max(0.f, yards) / kGunnerRunSpeed * 1000.f);
        }

        uint32_t Between(uint32_t lo, uint32_t hi, uint32_t roll)
        {
            return lo + roll % (hi - lo + 1);
        }
    }

    GunnerAct GunnerThink(GunnerMind& m, GunnerSense const& s, uint32_t nowMs)
    {
        // Closing in, it stops the moment it has the target in sight within its hold range: a
        // think a second lets a charge (Gunfights.h chaseSpeed) carry it into the target's face.
        bool const arrived = m.mode == GunnerMode::Chase && s.sight && s.distance <= s.holdRange
            && s.distance > kGunnerKnifeRange;
        if (!arrived && int32_t(nowMs - m.thinkMs) < 0)
            return GunnerAct::None;
        m.thinkMs = nowMs + kGunnerThinkMs;
        // At arm's length the knife, which its WoW chase brings to hand.
        if (s.distance <= kGunnerKnifeRange)
            return m.mode == GunnerMode::Chase ? GunnerAct::None : GunnerAct::Chase;
        bool const pressed = s.hurt || s.shotAt;
        bool const due = int32_t(nowMs - m.untilMs) >= 0;
        switch (m.mode)
        {
            case GunnerMode::Cover:
                // Hidden, it sees nothing, and that is the point; a target gone far is chased.
                if (s.distance > kGunnerReach)
                    return GunnerAct::Chase;
                return due ? GunnerAct::Peek : GunnerAct::None;
            case GunnerMode::Peek:
                if (!due)
                    return GunnerAct::None;
                if (pressed && m.peeks < kMaxPeeks)
                    return GunnerAct::ReturnToCover;
                // Done hiding: out to fight in the open a while.
                m.hasCover = false;
                m.peeks = 0;
                m.coverReadyMs = nowMs + kCoverCooldownMs;
                return GunnerAct::Strafe;
            default:
                break;
        }
        if (!s.sight || s.distance > s.holdRange)
            return m.mode == GunnerMode::Chase ? GunnerAct::None : GunnerAct::Chase;
        if (pressed && int32_t(nowMs - m.coverReadyMs) >= 0)
            return GunnerAct::TakeCover;
        if (m.mode == GunnerMode::Chase || due)
            return GunnerAct::Strafe;
        return GunnerAct::None;
    }

    void GunnerMoved(GunnerMind& m, GunnerAct act, bool found, Vec3 spot, float travel, uint32_t nowMs, uint32_t roll)
    {
        // Out in the open again (no cover, no peek): it strafes now and looks for cover later.
        auto open = [&]() {
            m.mode = GunnerMode::Fight;
            m.hasCover = false;
            m.peeks = 0;
            m.untilMs = nowMs;
            m.coverReadyMs = nowMs + kCoverCooldownMs;
        };
        switch (act)
        {
            case GunnerAct::Chase:
                m.mode = GunnerMode::Chase;
                m.hasCover = false;
                m.hasAnchor = false;
                m.peeks = 0;
                break;
            case GunnerAct::Strafe:
                m.mode = GunnerMode::Fight;
                // No spot this time: look again at the next think.
                m.untilMs = nowMs + (found ? TravelMs(travel) + Between(kStrafeMinMs, kStrafeMaxMs, roll) : 0);
                break;
            case GunnerAct::TakeCover:
                if (!found)
                    return open();
                m.mode = GunnerMode::Cover;
                m.hasCover = true;
                m.cover = spot;
                m.peeks = 0;
                m.untilMs = nowMs + TravelMs(travel) + Between(kCoverMinMs, kCoverMaxMs, roll);
                break;
            case GunnerAct::Peek:
                if (!found)
                    return open();
                m.mode = GunnerMode::Peek;
                ++m.peeks;
                m.untilMs = nowMs + TravelMs(travel) + kPeekMs;
                break;
            case GunnerAct::ReturnToCover:
                if (!found)
                    return open();
                m.mode = GunnerMode::Cover;
                m.untilMs = nowMs + TravelMs(travel) + Between(kCoverMinMs, kCoverMaxMs, roll);
                break;
            default:
                break;
        }
    }

    float GunnerBearingOffset(uint64_t guid)
    {
        uint32_t const h = Mix(uint32_t(guid) ^ Mix(uint32_t(guid >> 32) + 0x9e3779b9u));
        float degrees;
        if (h % 3 == 0)
            degrees = float(45 + (h >> 4) % 46) * ((h >> 12) & 1 ? 1.f : -1.f);    // a flanker
        else
            degrees = float(int32_t((h >> 4) % 61) - 30);
        return degrees * kPi / 180.f;
    }

    std::vector<Vec3> FiringSpots(Vec3 self, Vec3 target, float holdRange, float preferred)
    {
        float const d = Flat(self, target);
        float const b0 = d > 0.1f ? Bearing(target, self) : preferred;
        float const r = Clamp(d, kGunnerKnifeRange + 4.f, std::max(holdRange, kGunnerKnifeRange + 4.f));
        Vec3 const centre{target.x, target.y, self.z};
        std::vector<Vec3> out;
        // A few yards either side, at its distance.
        for (float lateral : {3.f, 4.5f, 6.f})
        {
            out.push_back(Around(centre, r, b0 + lateral / r));
            out.push_back(Around(centre, r, b0 - lateral / r));
        }
        // A step round toward its preferred bearing, a little nearer and a little farther.
        float const toward = AngleDiff(preferred, b0) >= 0.f ? 1.f : -1.f;
        out.push_back(Around(centre, r * 0.85f, b0 + toward * 5.f / r));
        out.push_back(Around(centre, std::min(r * 1.15f, std::max(holdRange, r)), b0 + toward * 4.f / r));
        return out;
    }

    std::vector<Vec3> CoverSpots(Vec3 self, uint32_t roll)
    {
        float const turn = float(roll % 360) * kPi / 180.f;
        std::vector<Vec3> out;
        for (int k = 0; k < 4; ++k)
            out.push_back(Around(self, 5.f, turn + float(k) * kPi / 2.f));
        for (int k = 0; k < 6; ++k)
            out.push_back(Around(self, 10.f, turn + kPi / 6.f + float(k) * kPi / 3.f));
        return out;
    }

    std::vector<Vec3> PeekSpots(Vec3 cover)
    {
        std::vector<Vec3> out;
        for (int k = 0; k < 4; ++k)
            out.push_back(Around(cover, 2.5f, float(k) * kPi / 2.f));
        for (int k = 0; k < 4; ++k)
            out.push_back(Around(cover, 4.f, kPi / 4.f + float(k) * kPi / 2.f));
        return out;
    }

    int ChooseFiringSpot(std::vector<GunnerSpot> const& spots, Vec3 self, Vec3 target, float preferred,
        std::vector<Vec3> const& squad, uint32_t roll)
    {
        (void)self;
        int best = -1;
        float bestScore = 0.f;
        for (size_t i = 0; i < spots.size(); ++i)
        {
            GunnerSpot const& s = spots[i];
            if (!s.reachable || !s.inSight)
                continue;
            float const b = Bearing(target, s.pos);
            // Yards of arc from its preferred bearing.
            float score = std::fabs(AngleDiff(b, preferred)) * std::max(1.f, Flat(s.pos, target));
            // Apart from its squad: not on a squadmate's spot, nor on its line to the target.
            for (Vec3 const& q : squad)
            {
                if (Flat(q, s.pos) < 4.f)
                    score += 8.f;
                if (std::fabs(AngleDiff(Bearing(target, q), b)) < 12.f * kPi / 180.f)
                    score += 3.f;
            }
            score += float((roll >> (i % 16)) & 3u) * 0.5f;
            if (best < 0 || score < bestScore)
            {
                best = int(i);
                bestScore = score;
            }
        }
        return best;
    }

    int ChooseCoverSpot(std::vector<GunnerSpot> const& spots, Vec3 self, Vec3 target)
    {
        float const now = Flat(self, target);
        int best = -1;
        float bestScore = 0.f;
        for (size_t i = 0; i < spots.size(); ++i)
        {
            GunnerSpot const& s = spots[i];
            float const there = Flat(s.pos, target);
            if (!s.reachable || s.inSight || there < kGunnerKnifeRange + 4.f || there > kGunnerReach)
                continue;
            // The nearest, but loath to run at the target.
            float const score = Flat(s.pos, self) + 0.5f * std::max(0.f, now - there);
            if (best < 0 || score < bestScore)
            {
                best = int(i);
                bestScore = score;
            }
        }
        return best;
    }

    int ChoosePeekSpot(std::vector<GunnerSpot> const& spots, Vec3 cover)
    {
        int best = -1;
        float bestDistance = 0.f;
        for (size_t i = 0; i < spots.size(); ++i)
        {
            GunnerSpot const& s = spots[i];
            if (!s.reachable || !s.inSight)
                continue;
            float const d = Flat(s.pos, cover);
            if (best < 0 || d < bestDistance)
            {
                best = int(i);
                bestDistance = d;
            }
        }
        return best;
    }

    bool HurtWindow::Update(float healthPct, uint32_t nowMs)
    {
        if (!started || int32_t(nowMs - markMs) > int32_t(kHurtWindowMs) || healthPct >= markPct)
        {
            started = true;
            markPct = healthPct;
            markMs = nowMs;
            return false;
        }
        return markPct - healthPct >= kHurtShare;
    }

    void SightTrack::Update(uint64_t targetGuid, bool sight, bool fast, Vec3 targetFeet, uint32_t nowMs)
    {
        if (targetGuid != target)
        {
            target = targetGuid;
            seeing = false;
            everSeen = false;
            sinceMs = nowMs;
        }
        if (sight)
        {
            lastSeen = targetFeet;
            everSeen = true;
            if (!seeing || fast)
            {
                seeing = true;
                sinceMs = nowMs;
            }
        }
        else if (seeing)
        {
            seeing = false;
            sinceMs = nowMs;
        }
    }

    uint32_t SightTrack::HiddenMs(uint32_t nowMs) const
    {
        return seeing || !everSeen ? 0 : nowMs - sinceMs;
    }

    bool NpcShouldThrowGrenade(uint32_t hiddenMs, float distance, bool cooledDown, bool squadThrowing, uint32_t roll)
    {
        return hiddenMs >= kNpcGrenadeHiddenMs && distance >= kNpcGrenadeMin && distance <= kNpcGrenadeMax
            && cooledDown && !squadThrowing && roll % 100 < kNpcGrenadeChancePercent;
    }

    float NpcLobSeconds(float distance)
    {
        return Clamp(0.6f + distance / 18.f, 0.8f, 2.2f);
    }
}
