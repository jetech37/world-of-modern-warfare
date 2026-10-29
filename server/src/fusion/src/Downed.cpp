/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Downed.h"

namespace Fusion::Downed
{
    float BleedOutSeconds(uint32_t stamina)
    {
        float const extra = std::floor(float(stamina) / kStaminaPerSecond);
        return std::min(kBleedMaxSeconds, kBleedBaseSeconds + extra);
    }

    uint32_t FinisherPool(uint32_t maxHealth)
    {
        return std::max<uint32_t>(1, uint32_t(float(maxHealth) * kFinisherShare));
    }

    uint32_t ReviveHealth(uint32_t maxHealth, float share)
    {
        return std::max<uint32_t>(1, uint32_t(float(maxHealth) * share + 0.5f));
    }

    bool CreatureLeaves(uint32_t downedSinceMs, uint32_t nowMs, bool hasOtherTarget)
    {
        if (!hasOtherTarget)
            return false;
        return int32_t(nowMs - downedSinceMs) >= int32_t(kCreatureInterestSeconds * 1000.f);
    }

    DamageFate OnDamage(bool downed, bool mayGoDown, uint32_t pool, uint32_t health, uint32_t damage)
    {
        DamageFate f;
        if (downed)
        {
            // The finisher pool soaks the hit; the one that empties it kills outright.
            if (damage < pool)
            {
                f.fate = Fate::Absorbed;
                f.damage = 0;
                f.poolLeft = pool - damage;
            }
            else
            {
                f.fate = Fate::Finished;
                f.damage = std::max<uint32_t>(health, 1);
                f.poolLeft = 0;
            }
            return f;
        }
        f.damage = damage;
        if (mayGoDown && damage >= health)
        {
            f.fate = Fate::GoDown;
            f.damage = health > 0 ? health - 1 : 0;
        }
        return f;
    }

    void State::GoDown(uint32_t nowMs, uint32_t maxHealth, uint32_t stamina, bool lastStand, bool finalStandReady)
    {
        active = true;
        sinceMs = nowMs;
        totalMs = uint32_t(BleedOutSeconds(stamina) * 1000.f);
        untilMs = nowMs + totalMs;
        finisherLeft = FinisherPool(maxHealth);
        finalStand = finalStandReady && !lastStand;
        armed = lastStand || finalStand;
    }

    Tick State::Update(uint32_t nowMs) const
    {
        if (!active)
            return Tick::None;
        if (finalStand && int32_t(nowMs - sinceMs) >= int32_t(kFinalStandSeconds * 1000.f))
            return Tick::GetUp;
        if (int32_t(nowMs - untilMs) >= 0)
            return Tick::BledOut;
        return Tick::None;
    }

    uint32_t State::RemainingMs(uint32_t nowMs) const
    {
        if (!active)
            return 0;
        int32_t const left = int32_t(untilMs - nowMs);
        return left > 0 ? uint32_t(left) : 0;
    }

    void State::Extend(uint32_t ms)
    {
        if (!active)
            return;
        untilMs += ms;
        totalMs += ms;
    }

    bool MayFire(bool armed, WeaponClass c)
    {
        return armed && c == WeaponClass::Pistol;
    }

    uint16_t LastStandPistol(uint16_t secondary, WeaponClass secondaryClass)
    {
        return secondary && secondaryClass == WeaponClass::Pistol ? secondary : kDefaultPistol;
    }

    namespace
    {
        // First Aid makes the kit (150, with Heavy Silk Bandages in reach), Engineering the coil
        // (175, after Goblin Jumper Cables' 165). The coil works a little quicker and brings a
        // little more health; the kit is cheaper to make.
        Kit const kKits[] = {
            {kFieldRevivalKit, 129, 150, 3000, 7000, 0.25f},
            {kRevivalCoil, 202, 175, 2500, 6000, 0.30f},
        };
    }

    Kit const* KitInfo(uint32_t itemEntry)
    {
        for (Kit const& k : kKits)
            if (k.item == itemEntry)
                return &k;
        return nullptr;
    }

    bool IsKitItem(uint32_t itemEntry)
    {
        return KitInfo(itemEntry) != nullptr;
    }

    uint32_t ManualProduct(uint32_t itemEntry)
    {
        switch (itemEntry)
        {
            case kFieldMedicHandbook: return kFieldRevivalKit;
            case kRevivalSchematic: return kRevivalCoil;
            default: return 0;
        }
    }

    UseCheck CanUseKit(Kit const& /*kit*/, UseContext const& c)
    {
        if (c.onSelf)
            return c.userDowned ? UseCheck::Ok : UseCheck::TargetNotDowned;
        // A downed player works only on themselves.
        if (c.userDowned)
            return UseCheck::UserDowned;
        if (!c.targetDowned)
            return UseCheck::TargetNotDowned;
        if (!c.friendly)
            return UseCheck::NotFriendly;
        if (c.distance > kKitReach)
            return UseCheck::TooFar;
        return UseCheck::Ok;
    }

    void KitUse::Start(uint64_t itemGuid, uint64_t targetGuid, uint32_t nowMs, uint32_t ms, Vec3 userAt)
    {
        item = itemGuid;
        target = targetGuid;
        startMs = nowMs;
        durationMs = ms;
        at = userAt;
        active = true;
    }

    KitProgress KitUse::Progress(uint32_t nowMs, Vec3 userAt) const
    {
        Vec3 const moved{userAt.x - at.x, userAt.y - at.y, 0.f};
        if (moved.Length() > kKitMoveTolerance)
            return KitProgress::Moved;
        if (int32_t(nowMs - startMs) >= int32_t(durationMs))
            return KitProgress::Done;
        return KitProgress::Working;
    }

    // ── The wire ────────────────────────────────────────────────────────────────────────────
    namespace
    {
        constexpr size_t kDownedBytes = 12;
        constexpr size_t kUpBytes = 11;
        constexpr size_t kDiedBytes = 2;
        constexpr size_t kKitBytes = 15;
        constexpr size_t kNearBytes = 10;
        constexpr size_t kUseBytes = 17;

        void Put8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
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
            for (int i = 0; i < 8; ++i)
                o.push_back(uint8_t(v >> (8 * i)));
        }

        struct Reader
        {
            std::vector<uint8_t> const& b;
            size_t at;
            uint8_t U8() { return b[at++]; }
            uint16_t U16()
            {
                uint16_t v = uint16_t(b[at] | (b[at + 1] << 8));
                at += 2;
                return v;
            }
            uint32_t U32()
            {
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(b[at + i]) << (8 * i);
                at += 4;
                return v;
            }
            uint64_t U64()
            {
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= uint64_t(b[at + i]) << (8 * i);
                at += 8;
                return v;
            }
        };
    }

    std::vector<uint8_t> EncodeDowned(DownedMsg const& m)
    {
        std::vector<uint8_t> o;
        o.reserve(kDownedBytes);
        Put8(o, kOpDowned);
        Put8(o, m.flags);
        Put32(o, m.remainingMs);
        Put32(o, m.totalMs);
        Put16(o, m.pistol);
        return o;
    }

    bool DecodeDowned(std::vector<uint8_t> const& body, DownedMsg& out)
    {
        if (body.size() != kDownedBytes || body[0] != kOpDowned)
            return false;
        Reader r{body, 1};
        out.flags = r.U8();
        out.remainingMs = r.U32();
        out.totalMs = r.U32();
        out.pistol = r.U16();
        return true;
    }

    std::vector<uint8_t> EncodeUp(UpMsg const& m)
    {
        std::vector<uint8_t> o;
        o.reserve(kUpBytes);
        Put8(o, kOpUp);
        Put8(o, uint8_t(m.reason));
        Put8(o, m.healthPct);
        Put64(o, m.by);
        return o;
    }

    bool DecodeUp(std::vector<uint8_t> const& body, UpMsg& out)
    {
        if (body.size() != kUpBytes || body[0] != kOpUp)
            return false;
        Reader r{body, 1};
        out.reason = UpReason(r.U8());
        out.healthPct = r.U8();
        out.by = r.U64();
        return true;
    }

    std::vector<uint8_t> EncodeDied(DeathCause c)
    {
        return {kOpDied, uint8_t(c)};
    }

    bool DecodeDied(std::vector<uint8_t> const& body, DeathCause& out)
    {
        if (body.size() != kDiedBytes || body[0] != kOpDied)
            return false;
        out = DeathCause(body[1]);
        return true;
    }

    std::vector<uint8_t> EncodeKit(KitMsg const& m)
    {
        std::vector<uint8_t> o;
        o.reserve(kKitBytes);
        Put8(o, kOpKit);
        Put8(o, uint8_t(m.state));
        Put8(o, m.selfUse ? 1 : 0);
        Put64(o, m.other);
        Put32(o, m.durationMs);
        return o;
    }

    bool DecodeKit(std::vector<uint8_t> const& body, KitMsg& out)
    {
        if (body.size() != kKitBytes || body[0] != kOpKit)
            return false;
        Reader r{body, 1};
        out.state = KitState(r.U8());
        out.selfUse = r.U8() != 0;
        out.other = r.U64();
        out.durationMs = r.U32();
        return true;
    }

    std::vector<uint8_t> EncodeNear(uint64_t player, bool downed)
    {
        std::vector<uint8_t> o;
        o.reserve(kNearBytes);
        Put8(o, kOpNear);
        Put64(o, player);
        Put8(o, downed ? 1 : 0);
        return o;
    }

    bool DecodeNear(std::vector<uint8_t> const& body, uint64_t& player, bool& downed)
    {
        if (body.size() != kNearBytes || body[0] != kOpNear)
            return false;
        Reader r{body, 1};
        player = r.U64();
        downed = r.U8() != 0;
        return true;
    }

    std::vector<uint8_t> EncodeUse(UseRequest const& q)
    {
        std::vector<uint8_t> o;
        o.reserve(kUseBytes);
        Put8(o, kOpUse);
        Put64(o, q.item);
        Put64(o, q.target);
        return o;
    }

    bool DecodeUse(std::vector<uint8_t> const& body, UseRequest& out)
    {
        if (body.size() != kUseBytes || body[0] != kOpUse)
            return false;
        Reader r{body, 1};
        out.item = r.U64();
        out.target = r.U64();
        return true;
    }
}
