/*
 * World of Modern Warfare: spells cast from the hands (fusion/Spellcasting.h).
 * GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Spellcasting.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Fusion::Spellcasting
{
    std::vector<AreaRow> const& AreaRows()
    {
        // One per caster class where a ground spell is natural; the first rank of each chain.
        static std::vector<AreaRow> const rows = {
            {8921, Shape::Splash, 8.f, 6000, 1000},     // Moonfire: a moonlit patch, 6 s
            {122, Shape::Area, 10.f, 0, 0},             // Frost Nova: the shockwave lands at the crosshair
            {26573, Shape::Area, 8.f, 0, 0},            // Consecration: the holy ground at the crosshair
            {18223, Shape::Splash, 8.f, 0, 0},          // Curse of Exhaustion: every enemy around the point
        };
        return rows;
    }

    AreaRow const* AreaOf(uint32_t firstRank)
    {
        for (AreaRow const& r : AreaRows())
            if (r.firstRank == firstRank)
                return &r;
        return nullptr;
    }

    std::vector<SprayRow> const& SprayRows()
    {
        // The first rank of each chain; the damage rides the bolt's rank at the caster's level.
        static std::vector<SprayRow> const rows = {
            // Scorch: the mage's fire stream, a narrow 10-yard jet, as strong a second as Fireballs
            // thrown as fast as the hand charges them (to each enemy in it), 1.5% of the mana a
            // tick: about 16 s from a full pool.
            {2948, 10.f, 20.f, 250, 133, 1.0f, 1.5f, 0},
            // Cone of Cold: the frost spray, wider and shorter, 70% of a Frostbolt stream's damage
            // a second, but it chills (30% slower while in it and 1.5 s after).
            {120, 9.f, 30.f, 250, 116, 0.7f, 1.5f, 1500},
        };
        return rows;
    }

    SprayRow const* SprayOf(uint32_t firstRank)
    {
        for (SprayRow const& r : SprayRows())
            if (r.firstRank == firstRank)
                return &r;
        return nullptr;
    }

    std::vector<BoltRow> const& BoltRows()
    {
        // By the released spell's first rank. Everything unlisted takes TraitsOf's defaults.
        static std::vector<BoltRow> const rows = {
            // Arcane Missiles: magic darts that seek (the selected target, else the one nearest the
            // crosshair) and fade out after 45 yards; each splashes a little.
            {5143, 45.f, 1.5f, {270.f, 20.f}},
            // The shocks are short, sharp spells: they fizzle at 30 yards.
            {8042, 30.f, -1.f, {}},             // Earth Shock
            {8056, 30.f, -1.f, {}},             // Frost Shock
            // Curse of Doom is one victim's doom, never a crowd's.
            {603, 0.f, 0.f, {}},
        };
        return rows;
    }

    BoltRow const* BoltRowOf(uint32_t firstRank)
    {
        for (BoltRow const& r : BoltRows())
            if (r.firstRank == firstRank)
                return &r;
        return nullptr;
    }

    BoltTraits TraitsOf(BoltFacts const& f)
    {
        BoltTraits t;
        if (f.speed <= 0.f && f.rangeYards > 0.f)
            t.fizzleYards = f.rangeYards * kInstantFizzleScale;
        if (f.ownArea > 0.f)
        {
            t.splashYards = f.ownArea;
            t.ownArea = true;
        }
        else if (f.damages)
            t.splashYards = kDefaultSplashYards;
        if (BoltRow const* r = BoltRowOf(f.firstRank))
        {
            if (r->fizzleYards > 0.f)
                t.fizzleYards = r->fizzleYards;
            else if (r->fizzleYards < 0.f)
                t.fizzleYards = 0.f;
            if (r->splashYards >= 0.f && !t.ownArea)
                t.splashYards = r->splashYards;
            t.homing = r->homing;
        }
        t.fizzleYards = std::min(t.fizzleYards, kBoltSafetyYards);
        return t;
    }

    BoltTraits WithHoming(BoltTraits t, Homing h)
    {
        t.homing.turnDegPerSec = std::max(t.homing.turnDegPerSec, h.turnDegPerSec);
        t.homing.coneDeg = std::max(t.homing.coneDeg, h.coneDeg);
        return t;
    }

    bool InHand(SpellFacts const& f)
    {
        if (f.passive || f.nextSwing)
            return false;
        if (AreaOf(f.firstRank) || SprayOf(f.firstRank))
            return true;
        return !f.selfRange && !f.physical;
    }

    Shape ShapeOf(SpellFacts const& f)
    {
        if (AreaRow const* a = AreaOf(f.firstRank))
            return a->shape;
        if (SprayOf(f.firstRank))
            return Shape::Spray;
        if (f.positive)
            return Shape::Heal;
        if (f.groundTarget)
            return Shape::Ground;
        // Past melee a hostile spell flies from the hand; a channel only when its ticks are
        // missiles (a beam such as Drain Life holds on the unit it was cast at).
        if (f.rangeYards > kMeleeReach && (!f.channeled || f.missileChannel))
            return Shape::Missile;
        return Shape::Bolt;
    }

    uint16_t ChargeMs(SpellFacts const& f)
    {
        // A spray pours from the press: nothing to charge.
        if (SprayOf(f.firstRank))
            return 0;
        uint16_t ms;
        if (f.channeled)
            ms = kChargeChannelMs;
        else if (f.castMs <= 0)
            ms = kChargeInstantMs;
        else if (f.castMs <= 1500)
            ms = kChargeQuickMs;
        else if (f.castMs <= 2500)
            ms = kChargeMediumMs;
        else
            ms = kChargeHeavyMs;
        Shape const s = ShapeOf(f);
        if (s == Shape::Area || s == Shape::Splash || s == Shape::Ground)
            ms = std::max(ms, kChargeAreaMs);
        return ms;
    }

    uint16_t FullChargeMs(uint16_t chargeMs)
    {
        return uint16_t(std::min(65535.f, float(chargeMs) * kFullChargeFactor + 0.5f));
    }

    bool FullChargeServed(uint32_t lastCastMs, bool castBefore, uint32_t nowMs, uint16_t chargeMs)
    {
        // A spray has no charge to hold.
        return chargeMs > 0 && ChargeServed(lastCastMs, castBefore, nowMs, FullChargeMs(chargeMs));
    }

    float ReleasePower(bool dual, bool full)
    {
        float p = (dual ? kDualPower : 1.f) * (full ? kFullChargePower : 1.f);
        return std::min(p, kMaxReleasePower);
    }

    bool ChargeServed(uint32_t lastCastMs, bool castBefore, uint32_t nowMs, uint16_t chargeMs)
    {
        if (!castBefore)
            return true;
        uint32_t const need = chargeMs > kChargeSlackMs ? uint32_t(chargeMs - kChargeSlackMs) : 0u;
        // The world clock wraps: the difference as signed.
        return int32_t(nowMs - lastCastMs) >= int32_t(need);
    }

    bool InReach(Vec3 from, Vec3 point, float range)
    {
        return (point - from).Length() <= range + kRangeSlack;
    }

    uint64_t BoltVictim(Vec3 point, std::vector<Candidate> const& units)
    {
        uint64_t best = 0;
        float bestGap = std::numeric_limits<float>::max();
        for (Candidate const& c : units)
        {
            // The gap from the point to the unit's edge, measured along the ground and up its body
            // (a point on its chest is on it).
            float const dx = point.x - c.at.x, dy = point.y - c.at.y;
            float const flat = std::sqrt(dx * dx + dy * dy);
            float const above = point.z - c.at.z;
            float const up = above < -1.f ? -1.f - above : above > 3.f ? above - 3.f : 0.f;
            float const gap = std::max(0.f, std::sqrt(flat * flat + up * up) - c.radius);
            if (gap <= kBoltSnap && gap < bestGap)
            {
                best = c.guid;
                bestGap = gap;
            }
        }
        return best;
    }

    bool HealsSelf(bool crosshairFriend, bool selfKey)
    {
        return selfKey || !crosshairFriend;
    }

    namespace
    {
        bool FiniteV(Vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // A body's upright axis (the capsule's centre line, feet to crown less the radius at each
        // end) and its radius widened by `width`.
        void Axis(Body const& b, float width, Vec3& lo, Vec3& hi, float& r)
        {
            float const height = std::max(0.2f, b.shape.height * StanceHeightScale(b.pose.stance));
            float const bottom = std::min(b.shape.radius, height * 0.5f);
            r = b.shape.radius + width;
            lo = b.pose.feet + Vec3{0.f, 0.f, bottom};
            hi = b.pose.feet + Vec3{0.f, 0.f, std::max(bottom, height - bottom)};
        }

        float PointToSegment(Vec3 p, Vec3 a, Vec3 b)
        {
            Vec3 const ab = b - a;
            float const len2 = ab.Dot(ab);
            float const t = len2 > 1e-8f ? Clamp((p - a).Dot(ab) / len2, 0.f, 1.f) : 0.f;
            return (p - (a + ab * t)).Length();
        }

        HitboxShape Widened(HitboxShape s, float width)
        {
            s.radius += width;
            s.headRadius += width;
            return s;
        }
    }

    Vec3 BoltOrigin(Vec3 claimed, Vec3 eye, float tolerance)
    {
        if (!FiniteV(claimed) || (claimed - eye).Length() > tolerance)
            return eye;
        return claimed;
    }

    bool AimDirection(Vec3 claimed, Vec3& out)
    {
        if (!FiniteV(claimed) || claimed.Length() < 1e-3f)
            return false;
        out = claimed.Normalized();
        return true;
    }

    float BoltReach(BoltTraits const& t)
    {
        return t.fizzleYards > 0.f ? std::min(t.fizzleYards, kBoltSafetyYards) : kBoltSafetyYards;
    }

    uint32_t FlightMs(float yards, float speed)
    {
        if (speed <= 0.f || yards <= 0.f)
            return 0;
        return std::min<uint32_t>(kMaxFlightMs, uint32_t(yards / speed * 1000.f + 0.5f));
    }

    float StepYards(float speed, uint32_t dtMs, float left)
    {
        if (left <= 0.f)
            return 0.f;
        if (speed <= 0.f)
            return left;
        return std::min(left, speed * float(dtMs) / 1000.f);
    }

    uint8_t VolleySize(int32_t durationMs, uint32_t amplitudeMs)
    {
        if (durationMs <= 0 || amplitudeMs == 0)
            return 1;
        return uint8_t(std::clamp<int32_t>(durationMs / int32_t(amplitudeMs), 1, kMaxVolley));
    }

    BodyStrike FirstBody(Vec3 from, Vec3 dir, float length, float width, std::vector<Body> const& bodies)
    {
        BodyStrike best;
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            Body const& b = bodies[i];
            float d = -1.f;
            Vec3 lo, hi;
            float r = 0.f;
            Axis(b, width, lo, hi, r);
            // Already inside it (it stepped onto the bolt): struck where the step starts.
            if (b.pose.stance != Stance::Prone && PointToSegment(from, lo, hi) <= r)
                d = 0.f;
            else if (std::optional<RayHit> const h = RayVsBody(from, dir, b.pose, Widened(b.shape, width)))
                d = h->distance;
            if (d < 0.f || d > length)
                continue;
            if (best.index < 0 || d < best.distance)
            {
                best.index = int(i);
                best.distance = d;
            }
        }
        return best;
    }

    bool ClaimHolds(Vec3 from, Vec3 dir, float upTo, Body const& claimed, float& at)
    {
        std::optional<RayHit> const h = RayVsBody(from, dir, claimed.pose, Widened(claimed.shape, kClaimSlack));
        if (!h || h->distance > upTo)
            return false;
        at = h->distance;
        return true;
    }

    Strike FirstStrike(BodyStrike unit, float world, uint32_t piece, float pieceAt)
    {
        Strike s;
        float best = std::numeric_limits<float>::max();
        if (world >= 0.f)
        {
            s.kind = StrikeKind::World;
            s.distance = best = world;
        }
        if (piece && pieceAt >= 0.f && pieceAt < best)
        {
            s.kind = StrikeKind::Piece;
            s.piece = piece;
            s.distance = best = pieceAt;
        }
        if (unit.index >= 0 && (s.kind == StrikeKind::None || unit.distance <= best + kUnitBeforeWall))
        {
            s.kind = StrikeKind::Unit;
            s.body = unit.index;
            s.piece = 0;
            s.distance = unit.distance;
        }
        return s;
    }

    std::vector<size_t> InBlast(Vec3 at, float radius, std::vector<Body> const& bodies)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            Vec3 lo, hi;
            float r = 0.f;
            Axis(bodies[i], 0.f, lo, hi, r);
            if (PointToSegment(at, lo, hi) - r <= radius)
                out.push_back(i);
        }
        return out;
    }

    float SplashScale(float distance, float radius, bool direct)
    {
        if (direct)
            return 1.f;
        if (radius <= 0.f || distance > radius)
            return 0.f;
        return Lerp(kSplashCentre, kSplashEdge, Clamp(distance / radius, 0.f, 1.f));
    }

    Vec3 BodyCentre(Body const& b)
    {
        Vec3 lo, hi;
        float r = 0.f;
        Axis(b, 0.f, lo, hi, r);
        return (lo + hi) * 0.5f;
    }

    std::vector<SplashHit> Splash(Vec3 at, float radius, std::vector<Body> const& bodies, int direct,
        std::function<bool(Vec3, Vec3)> const& sees)
    {
        std::vector<SplashHit> out;
        if (radius <= 0.f)
            return out;
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            if (int(i) == direct)
                continue;
            Vec3 lo, hi;
            float r = 0.f;
            Axis(bodies[i], 0.f, lo, hi, r);
            float const d = std::max(0.f, PointToSegment(at, lo, hi) - r);
            if (d > radius)
                continue;
            if (sees && !sees(at, BodyCentre(bodies[i])))
                continue;
            out.push_back({i, SplashScale(d, radius, false)});
        }
        return out;
    }

    AuraKind KindOfAura(uint32_t auraType)
    {
        switch (auraType)
        {
            case 2:     // SPELL_AURA_MOD_POSSESS
            case 5:     // SPELL_AURA_MOD_CONFUSE (Polymorph)
            case 6:     // SPELL_AURA_MOD_CHARM
            case 7:     // SPELL_AURA_MOD_FEAR
            case 12:    // SPELL_AURA_MOD_STUN
            case 25:    // SPELL_AURA_MOD_PACIFY
            case 26:    // SPELL_AURA_MOD_ROOT
            case 27:    // SPELL_AURA_MOD_SILENCE
            case 56:    // SPELL_AURA_TRANSFORM
            case 60:    // SPELL_AURA_MOD_PACIFY_SILENCE
            case 128:   // SPELL_AURA_MOD_POSSESS_PET
                return AuraKind::HardControl;
            case 33:    // SPELL_AURA_MOD_DECREASE_SPEED
                return AuraKind::Slow;
            case 3:     // SPELL_AURA_PERIODIC_DAMAGE
            case 53:    // SPELL_AURA_PERIODIC_LEECH
                return AuraKind::DamageOverTime;
            default:
                return AuraKind::Other;
        }
    }

    bool ControlEffect(uint32_t effect)
    {
        return effect == 68         // SPELL_EFFECT_INTERRUPT_CAST
            || effect == 98;        // SPELL_EFFECT_KNOCK_BACK
    }

    uint32_t SplashAuraMs(uint32_t fullMs, float scale)
    {
        uint32_t const ms = uint32_t(float(fullMs) * Clamp(scale, 0.f, 1.f) + 0.5f);
        return std::min(fullMs, std::max(kSplashMinAuraMs, ms));
    }

    namespace
    {
        constexpr float kDegToRad = 3.14159265f / 180.f;

        Vec3 Cross(Vec3 a, Vec3 b)
        {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }

        // The angle between two unit vectors, radians.
        float AngleBetween(Vec3 a, Vec3 b)
        {
            return std::acos(Clamp(a.Dot(b), -1.f, 1.f));
        }
    }

    int HomingTarget(Vec3 from, Vec3 dir, float coneDeg, std::vector<Body> const& bodies, uint64_t selected)
    {
        int best = -1;
        float bestAngle = std::numeric_limits<float>::max();
        float const cone = coneDeg * kDegToRad;
        float const selectedCone = std::max(coneDeg, kSelectedConeDeg) * kDegToRad;
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            Vec3 const v = BodyCentre(bodies[i]) - from;
            float const d = v.Length();
            if (d < 0.5f || d > kHomingAcquireYards)
                continue;
            float const angle = AngleBetween(v * (1.f / d), dir);
            // The selected target is the bolt's whenever it is roughly ahead.
            if (selected && bodies[i].guid == selected && angle <= selectedCone)
                return int(i);
            if (angle <= cone && angle < bestAngle)
            {
                best = int(i);
                bestAngle = angle;
            }
        }
        return best;
    }

    Vec3 SteerToward(Vec3 dir, Vec3 want, float maxRadians)
    {
        Vec3 const w = want.Normalized();
        if (w.Length() < 0.5f || maxRadians <= 0.f)
            return dir;
        float const angle = AngleBetween(dir, w);
        if (angle <= maxRadians)
            return w;
        float const s = std::sin(angle);
        if (s < 1e-4f)
        {
            // Straight behind: turn about any axis across the flight.
            Vec3 side = Cross(dir, Vec3{0.f, 0.f, 1.f});
            if (side.Length() < 1e-3f)
                side = Cross(dir, Vec3{1.f, 0.f, 0.f});
            side = side.Normalized();
            return (dir * std::cos(maxRadians) + side * std::sin(maxRadians)).Normalized();
        }
        // Along the great circle from `dir` to `w`, `maxRadians` of the way.
        return (dir * (std::sin(angle - maxRadians) / s) + w * (std::sin(maxRadians) / s)).Normalized();
    }

    bool InSpray(Vec3 eye, Vec3 dir, SprayRow const& row, Body const& b)
    {
        Vec3 lo, hi;
        float r = 0.f;
        Axis(b, 0.f, lo, hi, r);
        float const half = row.halfAngleDeg * kDegToRad;
        for (Vec3 p : {lo, (lo + hi) * 0.5f, hi})
        {
            Vec3 const v = p - eye;
            float const d = v.Length();
            if (d <= r)
                return true;            // the hand is in it
            if (d - r > row.rangeYards)
                continue;
            if (AngleBetween(v * (1.f / d), dir) <= half + std::atan(r / d))
                return true;
        }
        return false;
    }

    std::vector<size_t> SprayCatches(Vec3 eye, Vec3 dir, SprayRow const& row, std::vector<Body> const& bodies,
        std::function<bool(Vec3, Vec3)> const& sees)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < bodies.size(); ++i)
            if (InSpray(eye, dir, row, bodies[i]) && (!sees || sees(eye, BodyCentre(bodies[i]))))
                out.push_back(i);
        return out;
    }

    float SprayTickDamage(SprayRow const& row, float boltHit, uint16_t boltChargeMs)
    {
        if (boltChargeMs == 0 || boltHit <= 0.f || row.boltShare <= 0.f)
            return 0.f;
        return row.boltShare * boltHit * float(row.tickMs) / float(boltChargeMs);
    }

    float BelowRankScale(uint32_t level, uint32_t rankLevel)
    {
        if (rankLevel == 0 || level >= rankLevel)
            return 1.f;
        return std::max(0.25f, float(level) / float(rankLevel));
    }

    uint32_t SprayTickMana(uint32_t maxMana, SprayRow const& row)
    {
        return std::max<uint32_t>(1, uint32_t(float(maxMana) * row.manaPctPerTick / 100.f + 0.5f));
    }

    namespace
    {
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
        void PutF(std::vector<uint8_t>& o, float f)
        {
            uint32_t v = 0;
            std::memcpy(&v, &f, 4);
            Put32(o, v);
        }

        struct Reader
        {
            std::vector<uint8_t> const& b;
            size_t at = 0;
            bool ok = true;
            uint64_t Bytes(size_t n)
            {
                if (at + n > b.size())
                {
                    ok = false;
                    return 0;
                }
                uint64_t v = 0;
                for (size_t i = 0; i < n; ++i)
                    v |= uint64_t(b[at + i]) << (8 * i);
                at += n;
                return v;
            }
            uint8_t U8() { return uint8_t(Bytes(1)); }
            uint16_t U16() { return uint16_t(Bytes(2)); }
            uint32_t U32() { return uint32_t(Bytes(4)); }
            uint64_t U64() { return Bytes(8); }
            float F()
            {
                uint32_t const v = U32();
                float f = 0.f;
                std::memcpy(&f, &v, 4);
                return f;
            }
            bool Done() const { return ok && at == b.size(); }
        };

        bool Finite(Vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    }

    std::vector<uint8_t> EncodeHandCast(HandCast const& c)
    {
        std::vector<uint8_t> o;
        o.reserve(kHandCastBytes);
        Put8(o, kOpHandCast);
        Put32(o, c.spell);
        Put8(o, c.hand);
        Put8(o, c.flags);
        Put64(o, c.target);
        PutF(o, c.point.x);
        PutF(o, c.point.y);
        PutF(o, c.point.z);
        return o;
    }

    bool DecodeHandCast(std::vector<uint8_t> const& body, HandCast& out)
    {
        Reader r{body};
        if (r.U8() != kOpHandCast)
            return false;
        out.spell = r.U32();
        out.hand = r.U8();
        out.flags = r.U8();
        out.target = r.U64();
        out.point.x = r.F();
        out.point.y = r.F();
        out.point.z = r.F();
        return r.Done() && out.hand <= kHandBoth && Finite(out.point);
    }

    std::vector<uint8_t> EncodeHandAim(HandAim const& a)
    {
        std::vector<uint8_t> o = EncodeHandCast(a.cast);
        o[0] = kOpHandAim;
        for (Vec3 v : {a.origin, a.dir})
        {
            PutF(o, v.x);
            PutF(o, v.y);
            PutF(o, v.z);
        }
        return o;
    }

    bool DecodeHandAim(std::vector<uint8_t> const& body, HandAim& out)
    {
        if (body.size() != kHandAimBytes || body[0] != kOpHandAim)
            return false;
        // The head is op 12's release, byte for byte.
        std::vector<uint8_t> head(body.begin(), body.begin() + kHandCastBytes);
        head[0] = kOpHandCast;
        if (!DecodeHandCast(head, out.cast))
            return false;
        Reader r{body, kHandCastBytes};
        out.origin.x = r.F();
        out.origin.y = r.F();
        out.origin.z = r.F();
        out.dir.x = r.F();
        out.dir.y = r.F();
        out.dir.z = r.F();
        return r.Done() && Finite(out.origin) && Finite(out.dir);
    }

    std::vector<uint8_t> EncodeBoltEvent(BoltEvent const& e)
    {
        std::vector<uint8_t> o;
        o.reserve(kBoltLaunchBytes);
        Put8(o, kOpBolt);
        Put8(o, uint8_t(e.kind));
        Put32(o, e.id);
        Put32(o, e.spell);
        Put64(o, e.caster);
        if (e.kind == BoltEventKind::Launch)
        {
            for (Vec3 v : {e.from, e.to})
            {
                PutF(o, v.x);
                PutF(o, v.y);
                PutF(o, v.z);
            }
            PutF(o, e.speed);
        }
        else
        {
            PutF(o, e.to.x);
            PutF(o, e.to.y);
            PutF(o, e.to.z);
            Put64(o, e.unit);
        }
        Put8(o, e.flags);
        return o;
    }

    bool DecodeBoltEvent(std::vector<uint8_t> const& body, BoltEvent& out)
    {
        Reader r{body};
        if (r.U8() != kOpBolt)
            return false;
        uint8_t const kind = r.U8();
        if (kind != uint8_t(BoltEventKind::Launch) && kind != uint8_t(BoltEventKind::Impact))
            return false;
        out = BoltEvent{};
        out.kind = BoltEventKind(kind);
        out.id = r.U32();
        out.spell = r.U32();
        out.caster = r.U64();
        if (out.kind == BoltEventKind::Launch)
        {
            out.from.x = r.F();
            out.from.y = r.F();
            out.from.z = r.F();
            out.to.x = r.F();
            out.to.y = r.F();
            out.to.z = r.F();
            out.speed = r.F();
        }
        else
        {
            out.to.x = r.F();
            out.to.y = r.F();
            out.to.z = r.F();
            out.unit = r.U64();
        }
        out.flags = r.U8();
        return r.Done() && Finite(out.from) && Finite(out.to) && std::isfinite(out.speed);
    }

    std::vector<std::vector<uint8_t>> EncodeHandSpells(std::vector<HandSpell> const& spells, size_t maxBody)
    {
        std::vector<std::vector<uint8_t>> bodies;
        size_t const perBody = std::max<size_t>(1, (maxBody - 3) / kHandSpellBytes);
        for (size_t first = 0; first < spells.size(); first += perBody)
        {
            size_t const n = std::min(perBody, spells.size() - first);
            std::vector<uint8_t> o;
            Put8(o, kOpHandSpells);
            Put16(o, uint16_t(n));
            for (size_t i = first; i < first + n; ++i)
            {
                Put32(o, spells[i].spell);
                Put16(o, spells[i].chargeMs);
                Put8(o, uint8_t(spells[i].shape));
                Put8(o, spells[i].radius);
            }
            bodies.push_back(std::move(o));
        }
        return bodies;
    }

    bool DecodeHandSpells(std::vector<uint8_t> const& body, std::vector<HandSpell>& out)
    {
        Reader r{body};
        if (r.U8() != kOpHandSpells)
            return false;
        uint16_t const n = r.U16();
        out.clear();
        for (uint16_t i = 0; i < n && r.ok; ++i)
        {
            HandSpell h;
            h.spell = r.U32();
            h.chargeMs = r.U16();
            h.shape = Shape(r.U8());
            h.radius = r.U8();
            out.push_back(h);
        }
        return r.Done();
    }

    std::vector<uint8_t> EncodeCastResult(uint8_t hand, CastError e, uint32_t spell)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpCastResult);
        Put8(o, hand);
        Put8(o, uint8_t(e));
        Put32(o, spell);
        return o;
    }

    bool DecodeCastResult(std::vector<uint8_t> const& body, uint8_t& hand, CastError& e, uint32_t& spell)
    {
        Reader r{body};
        if (r.U8() != kOpCastResult)
            return false;
        hand = r.U8();
        e = CastError(r.U8());
        spell = r.U32();
        return r.Done();
    }

    std::vector<uint8_t> EncodeZone(Zone const& z)
    {
        std::vector<uint8_t> o;
        Put8(o, kOpZone);
        Put32(o, z.spell);
        PutF(o, z.at.x);
        PutF(o, z.at.y);
        PutF(o, z.at.z);
        PutF(o, z.radius);
        Put16(o, z.ms);
        return o;
    }

    bool DecodeZone(std::vector<uint8_t> const& body, Zone& out)
    {
        Reader r{body};
        if (r.U8() != kOpZone)
            return false;
        out.spell = r.U32();
        out.at.x = r.F();
        out.at.y = r.F();
        out.at.z = r.F();
        out.radius = r.F();
        out.ms = r.U16();
        return r.Done();
    }

    std::vector<uint8_t> EncodeSprayAim(SprayAim const& a)
    {
        std::vector<uint8_t> o;
        o.reserve(kSprayAimBytes);
        Put8(o, kOpSprayAim);
        Put32(o, a.spell);
        Put8(o, a.hand);
        Put8(o, a.on ? 1 : 0);
        for (Vec3 v : {a.origin, a.dir})
        {
            PutF(o, v.x);
            PutF(o, v.y);
            PutF(o, v.z);
        }
        return o;
    }

    bool DecodeSprayAim(std::vector<uint8_t> const& body, SprayAim& out)
    {
        Reader r{body};
        if (r.U8() != kOpSprayAim)
            return false;
        out.spell = r.U32();
        out.hand = r.U8();
        out.on = r.U8() != 0;
        out.origin.x = r.F();
        out.origin.y = r.F();
        out.origin.z = r.F();
        out.dir.x = r.F();
        out.dir.y = r.F();
        out.dir.z = r.F();
        // One hand at a time: a spray is never both hands' (each hand pours its own).
        return r.Done() && out.hand < kHandBoth && Finite(out.origin) && Finite(out.dir);
    }

    std::vector<uint8_t> EncodeSprayEvent(SprayEvent const& e)
    {
        std::vector<uint8_t> o;
        o.reserve(kSprayEventBytes);
        Put8(o, kOpSpray);
        Put64(o, e.caster);
        Put32(o, e.spell);
        Put8(o, e.hand);
        Put8(o, e.on ? 1 : 0);
        for (Vec3 v : {e.from, e.dir})
        {
            PutF(o, v.x);
            PutF(o, v.y);
            PutF(o, v.z);
        }
        PutF(o, e.range);
        PutF(o, e.halfAngleDeg);
        return o;
    }

    bool DecodeSprayEvent(std::vector<uint8_t> const& body, SprayEvent& out)
    {
        Reader r{body};
        if (r.U8() != kOpSpray)
            return false;
        out.caster = r.U64();
        out.spell = r.U32();
        out.hand = r.U8();
        out.on = r.U8() != 0;
        out.from.x = r.F();
        out.from.y = r.F();
        out.from.z = r.F();
        out.dir.x = r.F();
        out.dir.y = r.F();
        out.dir.z = r.F();
        out.range = r.F();
        out.halfAngleDeg = r.F();
        return r.Done() && Finite(out.from) && Finite(out.dir) && std::isfinite(out.range)
            && std::isfinite(out.halfAngleDeg);
    }
}
