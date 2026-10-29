/*
 * World of Modern Warfare: spells cast from the hands (fusion/Spellcasting.h). The client charges a
 * spell in a hand and sends the release with the crosshair's unit, point and ray (channel 7, op
 * 41); this casts it at once, instant and on the move, past the global cooldown, after its own
 * checks (the hand's charge, sight to a point). A hostile spell with reach flies as a bolt with no
 * range (its fizzle distance or a safety cap), straight or homing, and splashes the enemies around
 * where it strikes; a touch needs a unit, a heal falls back on its caster, an area lands at the
 * point and a splash spell hits every enemy around it and leaves a zone. A spray pours while its
 * button is held (op 43).
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionClasses.h"
#include "FusionMgr.h"
#include "CellImpl.h"
#include "Database/DBCStores.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "fusion/Scale.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <memory>

using namespace Fusion::Classes;
namespace SC = Fusion::Spellcasting;

namespace
{
    // A release's own power over its hits while it strikes (FusionClassState::releasePower): the
    // hand's cast, its bolt's impact or its zone's tick, never another hand's.
    struct ReleaseScope
    {
        FusionClassState& s;
        uint32 chain;
        float power;
        ReleaseScope(FusionClassState& state, uint32 c, float p) : s(state), chain(state.releaseChain), power(state.releasePower)
        {
            s.releaseChain = c;
            s.releasePower = p;
        }
        ~ReleaseScope()
        {
            s.releaseChain = chain;
            s.releasePower = power;
        }
        ReleaseScope(ReleaseScope const&) = delete;
        ReleaseScope& operator=(ReleaseScope const&) = delete;
    };

    uint32 ChainOf(uint32 spell)
    {
        uint32 const first = sSpellMgr.GetFirstSpellInChain(spell);
        return first ? first : spell;
    }
}

namespace
{
    uint32 Now() { return WorldTimer::getMSTime(); }

    constexpr float kDegToRad = 3.14159265f / 180.f;

    // The splash rules number auras and effects as the game does.
    static_assert(SPELL_AURA_MOD_CONFUSE == 5 && SPELL_AURA_MOD_STUN == 12 && SPELL_AURA_MOD_ROOT == 26
        && SPELL_AURA_MOD_DECREASE_SPEED == 33 && SPELL_AURA_PERIODIC_DAMAGE == 3 && SPELL_AURA_TRANSFORM == 56,
        "fusion/Spellcasting.h KindOfAura's numbering");
    static_assert(SPELL_EFFECT_INTERRUPT_CAST == 68 && SPELL_EFFECT_KNOCK_BACK == 98,
        "fusion/Spellcasting.h ControlEffect's numbering");

    // Whether a spell harms: direct damage, a leech, or a damage over time.
    bool Harms(SpellEntry const* e)
    {
        if (!e)
            return false;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            uint32 const eff = e->Effect[i];
            if (eff == SPELL_EFFECT_SCHOOL_DAMAGE || eff == SPELL_EFFECT_HEALTH_LEECH
                || eff == SPELL_EFFECT_WEAPON_DAMAGE || eff == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL
                || eff == SPELL_EFFECT_NORMALIZED_WEAPON_DMG || eff == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE)
                return true;
            if (eff == SPELL_EFFECT_APPLY_AURA
                && SC::KindOfAura(e->EffectApplyAuraName[i]) == SC::AuraKind::DamageOverTime)
                return true;
        }
        return false;
    }

    // The spell's longest aura, for how long a splashed share holds.
    uint32 LongestAuraMs(SpellEntry const* e)
    {
        int32 const d = e ? e->GetDuration() : 0;
        return d > 0 ? uint32(d) : 0;
    }

    Fusion::Vec3 Pos(WorldObject const* o) { return {o->GetPositionX(), o->GetPositionY(), o->GetPositionZ()}; }

    bool Hostile(Player* p, Unit* u) { return u && u->IsAlive() && p->IsValidAttackTarget(u); }

    // Living enemies of `p` within `range` of `at` that `at` can see.
    std::vector<Unit*> EnemiesAround(Player* p, Fusion::Vec3 at, float range)
    {
        std::list<Unit*> units;
        float const reach = range + (Pos(p) - at).Length();
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(p, p, reach);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(p, searcher, reach);
        std::vector<Unit*> out;
        for (Unit* u : units)
            if (Hostile(p, u) && (Pos(u) - at).Length() <= range + u->GetObjectBoundingRadius()
                && u->IsWithinLOS(at.x, at.y, at.z + 1.f))
                out.push_back(u);
        return out;
    }

    // The area-spell refit: its effects at the destination the hand sends; with none (a cast off
    // the bar) the destination is the caster, as it always was.
    void AimAtPoint(SpellEntry* e)
    {
        auto enemyArea = [](uint32 t)
        {
            return t == TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC || t == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC
                || t == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC || t == TARGET_ENUM_UNITS_ENEMY_WITHIN_CASTER_RANGE;
        };
        auto friendArea = [](uint32 t)
        {
            return t == TARGET_ENUM_UNITS_FRIEND_AOE_AT_SRC_LOC || t == TARGET_ENUM_UNITS_FRIEND_AOE_AT_DEST_LOC
                || t == TARGET_ENUM_UNITS_FRIEND_AOE_AT_DYNOBJ_LOC;
        };
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (!e->Effect[i])
                continue;
            uint32 const a = e->EffectImplicitTargetA[i], b = e->EffectImplicitTargetB[i];
            if (e->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA)
            {
                // The ground itself (Consecration): its dynamic object at the destination.
                e->EffectImplicitTargetA[i] = TARGET_LOCATION_CASTER_DEST;
                e->EffectImplicitTargetB[i] = TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC;
            }
            else if (enemyArea(a) || enemyArea(b))
            {
                // Frost Nova: every enemy around the destination.
                e->EffectImplicitTargetA[i] = TARGET_LOCATION_CASTER_DEST;
                e->EffectImplicitTargetB[i] = TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC;
            }
            else if (friendArea(a) || friendArea(b))
            {
                e->EffectImplicitTargetA[i] = TARGET_LOCATION_CASTER_DEST;
                e->EffectImplicitTargetB[i] = TARGET_ENUM_UNITS_FRIEND_AOE_AT_DEST_LOC;
            }
        }
        // A point needs a reach: thirty yards where the spell had none.
        if (e->rangeIndex == SPELL_RANGE_IDX_SELF_ONLY)
            e->rangeIndex = 4;
    }
}

namespace
{
    // The rank of a chain a level casts: the highest whose level allows it, else the first.
    SpellEntry const* RankForLevel(uint32 firstRank, uint32 level)
    {
        SpellEntry const* best = sSpellMgr.GetSpellEntry(firstRank);
        if (!best)
            return nullptr;
        struct Worker
        {
            uint32 level;
            SpellEntry const*& best;
            void operator()(uint32 id) const
            {
                if (SpellEntry const* e = sSpellMgr.GetSpellEntry(id))
                    if (e->spellLevel <= level && e->spellLevel >= best->spellLevel)
                        best = e;
            }
        } worker{level, best};
        sSpellMgr.doForHighRanks(firstRank, worker);
        return best;
    }

    // A spell's direct-damage effect, or -1.
    int DamageEffectOf(SpellEntry const* e)
    {
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (e->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                return i;
        return -1;
    }

    // An effect's average value cast at `level`, as CalculateSpellEffectValue rolls it: its base,
    // its points by level (within the rank's levels), and the middle of its dice.
    float AverageEffectValue(SpellEntry const* e, int i, uint32 level)
    {
        int32 l = int32(level);
        if (e->maxLevel > 0 && l > int32(e->maxLevel))
            l = int32(e->maxLevel);
        else if (l < int32(e->baseLevel))
            l = int32(e->baseLevel);
        l -= int32(e->spellLevel);
        float value = float(e->EffectBasePoints[i]) + float(l) * e->EffectRealPointsPerLevel[i];
        int32 const dice = int32(e->EffectBaseDice[i]);
        int32 const sides = int32(float(e->EffectDieSides[i]) + float(l) * e->EffectDicePerLevel[i]);
        value += sides <= 1 ? float(dice) : 0.5f * float(dice + sides);
        return value;
    }
}

SC::SpellFacts FusionClasses::HandFacts(SpellEntry const* e) const
{
    SC::SpellFacts f;
    f.firstRank = sSpellMgr.GetFirstSpellInChain(e->Id);
    if (!f.firstRank)
        f.firstRank = e->Id;
    SpellCastTimesEntry const* t = sSpellCastTimesStore.LookupEntry(e->CastingTimeIndex);
    f.castMs = t ? t->CastTime : 0;
    f.channeled = e->IsChanneledSpell();
    f.positive = e->IsPositiveSpell();
    f.selfRange = e->rangeIndex == SPELL_RANGE_IDX_SELF_ONLY;
    f.physical = e->School == SPELL_SCHOOL_NORMAL;
    f.nextSwing = (e->Attributes & (SPELL_ATTR_ON_NEXT_SWING_NO_DAMAGE | SPELL_ATTR_ON_NEXT_SWING)) != 0;
    f.passive = e->IsPassiveSpell();
    f.rangeYards = Spells::GetSpellMaxRange(sSpellRangeStore.LookupEntry(e->rangeIndex));
    // A point on the ground chosen by the caster: Blizzard, Flamestrike, Rain of Fire, Hurricane.
    f.groundTarget = (e->Targets & TARGET_FLAG_DEST_LOCATION) != 0;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (e->Effect[i] && e->EffectImplicitTargetA[i] == TARGET_LOCATION_CASTER_DEST)
            f.groundTarget = true;
    f.missileChannel = f.channeled && BoltFactsOf(e).strikes != e->Id;
    return f;
}

FusionClasses::HandBoltFacts FusionClasses::BoltFactsOf(SpellEntry const* e) const
{
    HandBoltFacts f;
    f.strikes = e->Id;
    f.speed = e->speed;
    // Arcane Missiles: a channel whose ticks fire a missile spell; a release fires its missiles.
    if (e->IsChanneledSpell())
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (e->Effect[i] == SPELL_EFFECT_APPLY_AURA && e->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_TRIGGER_SPELL)
                if (SpellEntry const* m = sSpellMgr.GetSpellEntry(e->EffectTriggerSpell[i]); m && m->speed > 0.f)
                {
                    f.strikes = m->Id;
                    f.speed = m->speed;
                    f.volley = SC::VolleySize(e->GetDuration(), e->EffectAmplitude[i]);
                    break;
                }
    // Its own area about where it lands (enemies around the destination): that is its splash.
    SpellEntry const* hit = sSpellMgr.GetSpellEntry(f.strikes);
    if (hit)
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (hit->Effect[i] && (hit->EffectImplicitTargetA[i] == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC
                || hit->EffectImplicitTargetB[i] == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC))
                f.ownArea = std::max(f.ownArea, Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(hit->EffectRadiusIndex[i])));
    // Its fizzle, its splash and whether it homes: the spell's row, else the defaults
    // (fusion/Spellcasting.h TraitsOf), by the released spell.
    SC::BoltFacts b;
    b.firstRank = sSpellMgr.GetFirstSpellInChain(e->Id) ? sSpellMgr.GetFirstSpellInChain(e->Id) : e->Id;
    b.speed = f.speed;
    b.rangeYards = Spells::GetSpellMaxRange(sSpellRangeStore.LookupEntry(e->rangeIndex));
    b.ownArea = f.ownArea;
    b.damages = Harms(hit);
    f.traits = SC::TraitsOf(b);
    return f;
}

SC::BoltTraits FusionClasses::TraitsFor(FusionClassState& s, SpellEntry const* info, HandBoltFacts const& facts,
    uint32 now) const
{
    SC::BoltTraits t = facts.traits;
    if (s.homingGrants.empty())
        return t;
    uint32 const chain = sSpellMgr.GetFirstSpellInChain(info->Id) ? sSpellMgr.GetFirstSpellInChain(info->Id) : info->Id;
    s.homingGrants.erase(std::remove_if(s.homingGrants.begin(), s.homingGrants.end(),
        [now](FusionHomingGrant const& g) { return g.untilMs && int32(now - g.untilMs) >= 0; }), s.homingGrants.end());
    for (FusionHomingGrant const& g : s.homingGrants)
        if (!g.chain || g.chain == chain)
            t = SC::WithHoming(t, g.homing);
    return t;
}

void FusionClasses::GrantHoming(Player* player, uint32 chain, SC::Homing homing, uint32 ms)
{
    if (!player)
        return;
    FusionClassState& s = StateOf(player);
    RevokeHoming(player, chain);
    s.homingGrants.push_back({chain, homing, ms ? Now() + ms : 0});
}

void FusionClasses::RevokeHoming(Player* player, uint32 chain)
{
    if (!player)
        return;
    FusionClassState& s = StateOf(player);
    s.homingGrants.erase(std::remove_if(s.homingGrants.begin(), s.homingGrants.end(),
        [chain](FusionHomingGrant const& g) { return g.chain == chain; }), s.homingGrants.end());
}

bool FusionClasses::GrantsSwingLunge(Player const* /*player*/) const
{
    // Nothing grants it yet: the warrior's Charge (its own spell) is the dash. A Charge perk or
    // talent that should also dash on the swing returns true here (and the melee profile is sent
    // again, FusionMgr::SendMeleeProfile, so the client asks for it).
    return false;
}

void FusionClasses::FitHandSpells()
{
    m_handSpells.clear();
    m_handBolts.clear();
    m_handSpellsOf.clear();
    uint32 fitted = 0;
    for (uint8 cls = 1; cls <= 11; ++cls)
    {
        if (!IsPlayableClass(cls))
            continue;
        // Every rank of the class's options, its basic spell and its Utility spells.
        std::set<uint32> ranks;
        if (auto c = m_spellOption.find(cls); c != m_spellOption.end())
            for (auto const& [spell, option] : c->second)
                ranks.insert(spell);
        std::vector<uint32> heads = UtilityOf(cls);
        if (uint32 const basic = BasicSpellOf(cls))
            heads.push_back(basic);
        for (uint32 head : heads)
            for (Rank const& r : Chain(head))
                ranks.insert(r.spell);
        std::vector<SC::HandSpell>& list = m_handSpellsOf[cls];
        for (uint32 id : ranks)
        {
            SpellEntry* e = const_cast<SpellEntry*>(sSpellMgr.GetSpellEntry(id));
            if (!e)
                continue;
            SC::SpellFacts const f = HandFacts(e);
            if (!SC::InHand(f))
                continue;
            SC::HandSpell h;
            h.spell = id;
            h.chargeMs = SC::ChargeMs(f);
            h.shape = SC::ShapeOf(f);
            if (SC::AreaRow const* a = SC::AreaOf(f.firstRank))
            {
                h.radius = uint8(std::min(255.f, a->radius + 0.5f));
                if (a->shape == SC::Shape::Area)
                    AimAtPoint(e);
            }
            else if (h.shape == SC::Shape::Ground)
            {
                // The marker's ring: the spell's own area.
                float r = 0.f;
                for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (e->Effect[i])
                        r = std::max(r, Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(e->EffectRadiusIndex[i])));
                h.radius = uint8(std::min(255.f, r + 0.5f));
            }
            else if (h.shape == SC::Shape::Missile)
            {
                HandBoltFacts const b = BoltFactsOf(e);
                m_handBolts[id] = b;
                h.radius = uint8(std::min(255.f, b.traits.splashYards + 0.5f));
            }
            else if (h.shape == SC::Shape::Spray)
            {
                // The client draws the cone this long.
                if (SC::SprayRow const* row = SC::SprayOf(f.firstRank))
                    h.radius = uint8(std::min(255.f, row->rangeYards + 0.5f));
            }
            // Cast on the move, a channel held while walking.
            e->InterruptFlags &= ~uint32(SPELL_INTERRUPT_FLAG_MOVEMENT);
            e->ChannelInterruptFlags &= ~uint32(AURA_INTERRUPT_MOVING_CANCELS);
            m_handSpells[id] = h;
            list.push_back(h);
            ++fitted;
        }
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: %u class spell ranks fitted to the hands.", fitted);
}

void FusionClasses::SendHandSpells(Player* player)
{
    auto it = m_handSpellsOf.find(player->GetClass());
    if (it == m_handSpellsOf.end() || it->second.empty())
        return;
    for (std::vector<uint8> const& body : SC::EncodeHandSpells(it->second, Fusion::Packets::kMaxChannelBody))
        sFusionMgr.SendChannel(player, kChannelClasses, body);
}

SpellCastResult FusionClasses::CastFromHand(Player* player, FusionClassState& s, SpellEntry const* info, Unit* target,
    Fusion::Vec3 const* point)
{
    SpellCastTargets targets;
    if (target)
        targets.setUnitTarget(target);
    if (point)
        targets.setDestination(point->x, point->y, point->z);
    // The charge stands for the global cooldown: the other hand may cast straight after.
    player->ResetGCD(info);
    Spell* spell = new Spell(player, info, false);
    s.handCasting = info->Id;
    SpellCastResult const r = spell->prepare(std::move(targets));
    s.handCasting = 0;
    // Cast now rather than at the next spell update: a second hand released this same tick would
    // otherwise find this one still preparing and be refused.
    if (r == SPELL_CAST_OK && spell->getState() == SPELL_STATE_PREPARING && spell->GetCastTime() == 0)
        spell->cast();
    return r;
}

namespace
{
    struct Traced
    {
        SC::Strike strike;
        Unit* unit = nullptr;
    };

    // What a step from `from` along `dir` for `len` yards meets first.
    Traced TraceBolt(Player* player, Fusion::Vec3 from, Fusion::Vec3 dir, float len, uint64 claim)
    {
        Traced t;
        if (len <= 0.f)
            return t;
        uint32 piece = 0;
        float const world = sFusionMgr.BoltMeetsWorld(player->GetMap(), from, from + dir * len, piece);
        std::vector<SC::Body> bodies;
        std::vector<Unit*> units;
        sFusionMgr.BoltBodies(player, from + dir * (len * 0.5f), len * 0.5f + 3.f, bodies, units);
        SC::BodyStrike body = SC::FirstBody(from, dir, len, SC::kBoltRadius, bodies);
        // An instant bolt forgives the crosshair's unit a little (it was under it on the screen).
        if (claim && body.index < 0)
            for (size_t i = 0; i < bodies.size(); ++i)
            {
                float at = 0.f;
                float const upTo = world >= 0.f ? world + SC::kUnitBeforeWall : len;
                if (bodies[i].guid == claim && SC::ClaimHolds(from, dir, upTo, bodies[i], at))
                {
                    body.index = int(i);
                    body.distance = at;
                }
            }
        t.strike = piece ? SC::FirstStrike(body, -1.f, piece, world) : SC::FirstStrike(body, world, 0, 0.f);
        if (t.strike.kind == SC::StrikeKind::Unit)
            t.unit = units[size_t(t.strike.body)];
        return t;
    }

    // The spell lands on `unit` as the bolt's impact: the game's whole pipeline, no second flight.
    // `splash`: a share of it on an enemy the bolt did not hit (0: the one it hit, whole).
    void Arrive(Player* player, SpellEntry const* info, Unit* unit, Fusion::Vec3 at, bool area, float splash = 0.f)
    {
        SpellCastTargets targets;
        targets.setUnitTarget(unit);
        // Its own area about the destination centres on the impact.
        if (area)
            targets.setDestination(at.x, at.y, at.z);
        Spell* spell = new Spell(player, info, true);
        spell->m_fusionArrived = true;
        spell->m_fusionSplash = splash;
        spell->prepare(std::move(targets));
    }

    // A unit's chest, where a homing bolt steers.
    Fusion::Vec3 ChestOf(Unit const* u)
    {
        return Pos(u) + Fusion::Vec3{0.f, 0.f, std::max(1.f, u->GetCollisionHeight()) * 0.55f};
    }
}

void FusionClasses::HandCast(Player* player, std::vector<uint8> const& body)
{
    SC::HandCast c;
    // Op 41 carries the ray the release was aimed along; op 12 (an older client) only the
    // crosshair's unit and point.
    bool aimed = false;
    Fusion::Vec3 aimFrom, aimDir;
    if (!body.empty() && body[0] == SC::kOpHandAim)
    {
        SC::HandAim a;
        if (!SC::DecodeHandAim(body, a))
            return;
        c = a.cast;
        aimFrom = a.origin;
        aimDir = a.dir;
        aimed = true;
    }
    else if (!SC::DecodeHandCast(body, c))
        return;
    uint32 const asked = c.spell;
    auto refuse = [&](SC::CastError e)
    {
        sFusionMgr.SendChannel(player, kChannelClasses, SC::EncodeCastResult(c.hand, e, asked));
    };
    // A hand readied before a level-up holds the old rank: it casts the ability's single entry.
    if (!player->HasActiveSpell(c.spell))
        if (uint32 const entry = SingleEntryOf(player, c.spell))
            c.spell = entry;
    auto held = m_handSpells.find(c.spell);
    SpellEntry const* info = sSpellMgr.GetSpellEntry(c.spell);
    if (held == m_handSpells.end() || !info || !player->HasActiveSpell(c.spell) || !player->IsInWorld())
        return refuse(SC::CastError::NotHeld);
    if (!player->IsAlive())
        return refuse(SC::CastError::Refused);
    SC::HandSpell const& h = held->second;
    FusionClassState& s = StateOf(player);
    uint32 const now = Now();
    bool const dual = c.hand == SC::kHandBoth;
    // Each hand casts once a charge: a dual cast needs both charged.
    for (uint8 hand = 0; hand < 2; ++hand)
        if ((dual || hand == c.hand) && !SC::ChargeServed(s.handCastMs[hand], s.handCast[hand], now, h.chargeMs))
            return refuse(SC::CastError::Charging);
    // A full charge (fusion/Spellcasting.h): the client says it was held, the hands' time agrees.
    bool full = (c.flags & SC::CAST_FULL) != 0 && h.chargeMs > 0;
    for (uint8 hand = 0; hand < 2 && full; ++hand)
        if ((dual || hand == c.hand) && !SC::FullChargeServed(s.handCastMs[hand], s.handCast[hand], now, h.chargeMs))
            full = false;

    Map* map = player->GetMap();
    Unit* crosshair = c.target ? map->GetUnit(ObjectGuid(c.target)) : nullptr;
    if (crosshair && !crosshair->IsAlive())
        crosshair = nullptr;
    bool const hasPoint = (c.flags & SC::CAST_POINT) != 0;
    float range = Spells::GetSpellMaxRange(sSpellRangeStore.LookupEntry(info->rangeIndex));
    if (range < 1.f)
        range = SC::kAreaReach;
    Fusion::Vec3 const me = Pos(player);
    auto pointOk = [&]() -> SC::CastError
    {
        if (!hasPoint)
            return SC::CastError::NoPoint;
        if (!SC::InReach(me, c.point, range))
            return SC::CastError::OutOfRange;
        if (!player->IsWithinLOS(c.point.x, c.point.y, c.point.z + 1.f))
            return SC::CastError::NoSight;
        return SC::CastError::None;
    };

    // A dual cast's power and width ride the spell's own hits from here (a single cast clears an
    // older dual one's, never stronger than designed).
    uint32 const first = sSpellMgr.GetFirstSpellInChain(info->Id) ? sSpellMgr.GetFirstSpellInChain(info->Id) : info->Id;
    SC::AreaRow const* area = SC::AreaOf(first);
    s.dualWindows.erase(std::remove_if(s.dualWindows.begin(), s.dualWindows.end(),
        [&](std::pair<uint32, uint32> const& w) { return w.first == first || int32(now - w.second) >= 0; }),
        s.dualWindows.end());
    s.fullWindows.erase(std::remove_if(s.fullWindows.begin(), s.fullWindows.end(),
        [&](std::pair<uint32, uint32> const& w) { return w.first == first || int32(now - w.second) >= 0; }),
        s.fullWindows.end());
    size_t const windowsBefore = s.dualWindows.size();
    size_t const fullBefore = s.fullWindows.size();
    if (dual || full)
    {
        uint32 lasts = SC::kDualWindowMs;
        if (area)
            lasts = std::max(lasts, area->zoneMs);
        if (int32 const d = info->GetDuration(); d > 0)
            lasts = std::max(lasts, uint32(d));
        // A bolt's power holds for its flight; a missile channel's for its missiles' spell.
        if (h.shape == SC::Shape::Missile)
            if (auto b = m_handBolts.find(info->Id); b != m_handBolts.end())
            {
                lasts = std::max(lasts, SC::FlightMs(SC::BoltReach(b->second.traits), b->second.speed)
                    + uint32(b->second.volley) * SC::kVolleyGapMs + 500);
                if (b->second.strikes != info->Id)
                {
                    uint32 const missile = sSpellMgr.GetFirstSpellInChain(b->second.strikes)
                        ? sSpellMgr.GetFirstSpellInChain(b->second.strikes) : b->second.strikes;
                    if (dual)
                        s.dualWindows.emplace_back(missile, now + lasts);
                    if (full)
                        s.fullWindows.emplace_back(missile, now + lasts);
                }
            }
        if (dual)
            s.dualWindows.emplace_back(first, now + lasts);
        if (full)
            s.fullWindows.emplace_back(first, now + lasts);
    }
    auto dropDual = [&]()
    {
        s.dualWindows.resize(windowsBefore);
        s.fullWindows.resize(fullBefore);
    };

    // This release's own power rides its own hits (and its bolts' and zones'), never the other
    // hand's: two hands charge and strike apart.
    ReleaseScope const scope(s, first, SC::ReleasePower(dual, full));
    SpellCastResult r = SPELL_CAST_OK;
    switch (h.shape)
    {
        case SC::Shape::Heal:
        {
            Unit* on = player;
            if (crosshair && !SC::HealsSelf(player->IsValidHelpfulTarget(crosshair), (c.flags & SC::CAST_SELF) != 0))
                on = crosshair;
            r = CastFromHand(player, s, info, on, nullptr);
            break;
        }
        case SC::Shape::Bolt:
        {
            Unit* on = Hostile(player, crosshair) ? crosshair : nullptr;
            // A beam or a touch with nothing under the crosshair: the first enemy along its ray.
            if (!on && aimed)
            {
                Fusion::Vec3 dir;
                if (SC::AimDirection(aimDir, dir))
                {
                    Fusion::Vec3 const from = SC::BoltOrigin(aimFrom, sFusionMgr.EyeOf(player), sFusionMgr.ShotOriginTolerance(player));
                    on = TraceBolt(player, from, dir, std::max(0.f, range) + SC::kRangeSlack, c.target).unit;
                }
            }
            // At the ground: the enemy standing at the point.
            if (!on && hasPoint && SC::InReach(me, c.point, range + SC::kBoltSnap))
            {
                std::vector<SC::Candidate> nearby;
                for (Unit* u : EnemiesAround(player, c.point, SC::kBoltSnap + 5.f))
                    nearby.push_back({u->GetObjectGuid().GetRawValue(), Pos(u), u->GetObjectBoundingRadius()});
                if (uint64 const g = SC::BoltVictim(c.point, nearby))
                    on = map->GetUnit(ObjectGuid(g));
            }
            if (!on)
            {
                dropDual();
                return refuse(SC::CastError::NoTarget);
            }
            if (!SC::InReach(me, Pos(on), range + on->GetObjectBoundingRadius()))
            {
                dropDual();
                return refuse(SC::CastError::OutOfRange);
            }
            if (!player->IsWithinLOSInMap(on))
            {
                dropDual();
                return refuse(SC::CastError::NoSight);
            }
            r = CastFromHand(player, s, info, on, nullptr);
            break;
        }
        case SC::Shape::Missile:
        {
            // Free-aimed: from the eye along the aim, at whatever is there (fusion/Spellcasting.h).
            Fusion::Vec3 const eye = sFusionMgr.EyeOf(player);
            Fusion::Vec3 origin = eye;
            Fusion::Vec3 dir;
            bool ok = false;
            if (aimed)
            {
                origin = SC::BoltOrigin(aimFrom, eye, sFusionMgr.ShotOriginTolerance(player));
                ok = SC::AimDirection(aimDir, dir);
            }
            else if (crosshair)
                ok = SC::AimDirection(Pos(crosshair) + Fusion::Vec3{0.f, 0.f, 1.f} - eye, dir);
            else if (hasPoint)
                ok = SC::AimDirection(c.point - eye, dir);
            if (!ok)
            {
                dropDual();
                return refuse(SC::CastError::NoPoint);
            }
            if (SC::CastError const e = ReleaseBolt(player, s, info, c, origin, dir); e != SC::CastError::None)
            {
                dropDual();
                return refuse(e);
            }
            break;
        }
        case SC::Shape::Spray:
            // A spray is held (op 43), never released.
            dropDual();
            return refuse(SC::CastError::NotHeld);
        case SC::Shape::Ground:
        {
            if (SC::CastError const e = pointOk(); e != SC::CastError::None)
            {
                dropDual();
                return refuse(e);
            }
            // Down onto the ground under the point (the crosshair on a unit's chest).
            Fusion::Vec3 at = c.point;
            float const ground = map->GetHeight(at.x, at.y, at.z + 1.f);
            if (ground > INVALID_HEIGHT && at.z - ground < 4.f)
                at.z = ground;
            r = CastFromHand(player, s, info, nullptr, &at);
            break;
        }
        case SC::Shape::Area:
        {
            if (SC::CastError const e = pointOk(); e != SC::CastError::None)
            {
                dropDual();
                return refuse(e);
            }
            r = CastFromHand(player, s, info, nullptr, &c.point);
            break;
        }
        case SC::Shape::Splash:
        {
            if (SC::CastError const e = pointOk(); e != SC::CastError::None)
            {
                dropDual();
                return refuse(e);
            }
            if (!player->IsSpellReady(info))
            {
                dropDual();
                return refuse(SC::CastError::Refused);
            }
            float const radius = (area ? area->radius : 8.f) * (dual ? SC::kDualRadius : 1.f);
            std::vector<Unit*> hit = EnemiesAround(player, c.point, radius);
            // The one nearest the point (in the spell's own reach) takes the real cast: its
            // cooldown, the option's custom part, the resources. The rest take the same spell,
            // triggered.
            std::sort(hit.begin(), hit.end(), [&](Unit* a, Unit* b)
                { return (Pos(a) - c.point).Length() < (Pos(b) - c.point).Length(); });
            auto primary = std::find_if(hit.begin(), hit.end(),
                [&](Unit* u) { return player->IsWithinDistInMap(u, range); });
            if (primary != hit.end())
                r = CastFromHand(player, s, info, *primary, nullptr);
            else
            {
                // Nothing in reach yet: the spell is spent all the same (its zone waits for them),
                // paid as a cast is (its power, cooldown and the global cooldown): those near
                // the point take it below, and a spell that cost nothing would be free splash.
                if (SC::CastError const e = PayRelease(player, info); e != SC::CastError::None)
                {
                    dropDual();
                    return refuse(e);
                }
            }
            if (r == SPELL_CAST_OK)
                for (Unit* u : hit)
                    if (primary == hit.end() || u != *primary)
                        player->CastSpell(u, info, true);
            if (r == SPELL_CAST_OK && area && area->zoneMs)
            {
                FusionHandZone z;
                z.power = s.releasePower > 0.f ? s.releasePower : 1.f;
                z.spell = info->Id;
                z.mapId = player->GetMapId();
                z.at = c.point;
                z.radius = radius;
                z.untilMs = now + area->zoneMs;
                z.tickMs = std::max<uint32>(250, area->zoneTickMs);
                z.nextTickMs = now + z.tickMs;
                s.handZones.push_back(z);
                SC::Zone wire{info->Id, c.point, radius, uint16(std::min<uint32>(area->zoneMs, 65535))};
                sFusionMgr.SendChannel(player, kChannelClasses, SC::EncodeZone(wire));
            }
            break;
        }
    }
    if (r != SPELL_CAST_OK)
    {
        dropDual();
        return refuse(SC::CastError::Refused);
    }
    for (uint8 hand = 0; hand < 2; ++hand)
        if (dual || hand == c.hand)
        {
            s.handCastMs[hand] = now;
            s.handCast[hand] = true;
        }
}

void FusionClasses::TickHandZones(Player* player, FusionClassState& s, uint32 now)
{
    if (s.handZones.empty())
        return;
    ObjectGuid const me = player->GetObjectGuid();
    for (auto it = s.handZones.begin(); it != s.handZones.end();)
    {
        FusionHandZone& z = *it;
        if (int32(now - z.untilMs) >= 0 || z.mapId != player->GetMapId())
        {
            it = s.handZones.erase(it);
            continue;
        }
        if (int32(now - z.nextTickMs) >= 0)
        {
            z.nextTickMs = now + z.tickMs;
            if (SpellEntry const* info = sSpellMgr.GetSpellEntry(z.spell))
            {
                ReleaseScope const scope(s, ChainOf(z.spell), z.power);
                for (Unit* u : EnemiesAround(player, z.at, z.radius))
                    if (!u->GetSpellAuraHolder(z.spell, me))
                        player->CastSpell(u, info, true);
            }
        }
        ++it;
    }
}

float FusionClasses::DualPower(FusionClassState const& s, SpellEntry const* spell, uint32 now) const
{
    if (!spell)
        return 1.f;
    uint32 const first = ChainOf(spell->Id);
    // A hand's own release striking now: its power, whatever another hand's window says.
    if (s.releasePower > 0.f && s.releaseChain == first)
        return s.releasePower;
    if (s.dualWindows.empty() && s.fullWindows.empty())
        return 1.f;
    auto in = [&](std::vector<std::pair<uint32, uint32>> const& windows)
    {
        for (auto const& [chain, until] : windows)
            if (chain == first && int32(until - now) > 0)
                return true;
        return false;
    };
    // Both hands at once, a full charge, or both (fusion/Spellcasting.h ReleasePower).
    return SC::ReleasePower(in(s.dualWindows), in(s.fullWindows));
}

float FusionClasses::RadiusMultiplier(Spell const* spell) const
{
    if (!m_enabled || !spell)
        return 1.f;
    SpellCaster const* caster = spell->GetCaster();
    Player const* p = caster ? caster->ToPlayer() : nullptr;
    FusionClassState const* s = p ? StateOf(p) : nullptr;
    if (!s || s->dualWindows.empty())
        return 1.f;
    // The dual cast's power and its width go together (a full charge is stronger, not wider).
    uint32 const id = spell->m_spellInfo->Id;
    uint32 const first = sSpellMgr.GetFirstSpellInChain(id) ? sSpellMgr.GetFirstSpellInChain(id) : id;
    uint32 const now = Now();
    for (auto const& [chain, until] : s->dualWindows)
        if (chain == first && int32(until - now) > 0)
            return SC::kDualRadius;
    return 1.f;
}

// ── Free-aimed bolts (fusion/Spellcasting.h, Shape::Missile) ──
//
// A release pays for the spell at once, as a cast would, and leaves a bolt (a missile channel:
// one per tick, a quarter second apart) at the caster's eye. Each update flies it on by its speed
// and tests the step against the world (vmaps, terrain, built pieces) and the hitboxes of the
// enemies about it, as they stand now: a target that steps aside is missed, one that steps in is
// struck. What it strikes takes the spell through the game's own pipeline (a triggered cast with
// no second flight: damage, auras, procs, threat, the combat log, the kill); the world shows the
// impact and the pieces take its damage. Everyone near is told of the flight and the impact (op
// 42) to draw the spell's own missile and impact.

SC::CastError FusionClasses::PayRelease(Player* player, SpellEntry const* info)
{
    if (!player->IsSpellReady(info))
        return SC::CastError::Refused;
    std::unique_ptr<Spell> probe(new Spell(player, info, false));
    // Silenced, stunned, pacified; an option's resource short.
    if (probe->CheckCasterAuras() != SPELL_CAST_OK || CheckCast(probe.get()) != SPELL_CAST_OK)
        return SC::CastError::Refused;
    uint32 const cost = Spell::CalculatePowerCost(info, player, probe.get());
    if (cost && !player->HasCheatOption(PLAYER_CHEAT_NO_POWER))
    {
        bool short_ = false;
        if (info->powerType == POWER_HEALTH)
            short_ = player->GetHealth() <= cost;
        else if (info->powerType < MAX_POWERS)
            short_ = player->GetPower(Powers(info->powerType)) < cost;
        if (short_)
        {
            player->RestoreSpellMods(probe.get());
            return SC::CastError::Refused;
        }
        if (info->powerType == POWER_HEALTH)
            player->ModifyHealth(-int32(cost));
        else if (info->powerType < MAX_POWERS)
        {
            player->ModifyPower(Powers(info->powerType), -int32(cost));
            if (info->powerType == POWER_MANA && !info->HasAttribute(SPELL_ATTR_EX2_DONT_BLOCK_MANA_REGEN))
                player->SetLastManaUse(info->Id);
        }
    }
    // The charges a cast spends (Clearcasting's free spell) go with this one.
    player->RemoveSpellMods(probe.get());
    player->AddCooldown(info);
    if (uint32 const cd = std::max(info->RecoveryTime, info->CategoryRecoveryTime))
        player->SendSpellCooldown(info->Id, Milliseconds(cd), player->GetObjectGuid());
    // The charge stands for the global cooldown (CastFromHand), and a cast gives a hider away.
    player->ResetGCD(info);
    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_ACTION_CANCELS | AURA_INTERRUPT_ACTION_CANCELS_LATE, info->Id);
    return SC::CastError::None;
}

SC::CastError FusionClasses::ReleaseBolt(Player* player, FusionClassState& s, SpellEntry const* info, SC::HandCast const& c,
    Fusion::Vec3 origin, Fusion::Vec3 dir)
{
    auto known = m_handBolts.find(info->Id);
    HandBoltFacts const facts = known != m_handBolts.end() ? known->second : BoltFactsOf(info);
    if (s.handBolts.size() + facts.volley > SC::kMaxBoltsInFlight)
        return SC::CastError::Refused;
    if (SC::CastError const e = PayRelease(player, info); e != SC::CastError::None)
        return e;
    uint32 const now = Now();
    bool const dual = c.hand == SC::kHandBoth;
    // No range: its fizzle distance or the safety cap; straight unless a row or a grant homes it.
    SC::BoltTraits const traits = TraitsFor(s, info, facts, now);
    for (uint8 i = 0; i < facts.volley; ++i)
    {
        FusionHandBolt b;
        b.id = ++m_nextBoltId;
        if (!b.id)
            b.id = ++m_nextBoltId;
        b.spell = info->Id;
        b.strikes = facts.strikes;
        b.mapId = player->GetMapId();
        b.at = origin;
        b.dir = dir;
        b.speed = facts.speed;
        b.left = SC::BoltReach(traits);
        b.fizzles = traits.fizzleYards > 0.f;
        b.splash = traits.splashYards * (dual ? SC::kDualRadius : 1.f);
        b.ownArea = traits.ownArea;
        // An instant ray strikes at once: nothing to steer.
        b.homing = facts.speed > 0.f ? traits.homing : SC::Homing{};
        b.startMs = now + uint32(i) * SC::kVolleyGapMs;
        b.lastMs = b.startMs;
        b.volley = i > 0;
        // Only an instant bolt is forgiven the crosshair's unit; a flying one can be dodged.
        b.claim = facts.speed <= 0.f ? c.target : 0;
        b.power = s.releasePower > 0.f ? s.releasePower : SC::ReleasePower(dual, false);
        s.handBolts.push_back(b);
    }
    // The first leaves (or strikes) now.
    TickHandBolts(player, s, now);
    return SC::CastError::None;
}

void FusionClasses::TickHandBolts(Player* player, FusionClassState& s, uint32 now)
{
    if (s.handBolts.empty())
        return;
    // Flown from a copy: an impact runs the game's spell code, which may come back here.
    std::vector<FusionHandBolt> flying;
    flying.swap(s.handBolts);
    std::vector<FusionHandBolt> still;
    for (FusionHandBolt& b : flying)
        if (!StepBolt(player, b, now))
            still.push_back(b);
    still.insert(still.end(), s.handBolts.begin(), s.handBolts.end());
    s.handBolts.swap(still);
}

bool FusionClasses::StepBolt(Player* player, FusionHandBolt& b, uint32 now)
{
    if (!player->IsInWorld() || b.mapId != player->GetMapId())
        return true;
    if (int32(now - b.startMs) < 0)
        return false;
    Map* map = player->GetMap();
    // Its course to those near: from where it is to where it ends as things stand. A homing bolt's
    // goes to its caster too (it drew the bolt straight at the release), and again as it turns.
    auto tellCourse = [&](uint8 flags)
    {
        Traced const ahead = TraceBolt(player, b.at, b.dir, b.left, 0);
        SC::BoltEvent e;
        e.kind = SC::BoltEventKind::Launch;
        e.id = b.id;
        e.spell = b.strikes;
        e.caster = player->GetObjectGuid().GetRawValue();
        e.from = b.at;
        e.to = b.at + b.dir * (ahead.strike.kind != SC::StrikeKind::None ? ahead.strike.distance : b.left);
        e.speed = b.speed;
        e.flags = flags;
        bool const toCaster = flags & (SC::BOLT_VOLLEY | SC::BOLT_HOMING);
        sFusionMgr.TellNearOn(map, kChannelClasses, b.at, SC::EncodeBoltEvent(e), toCaster ? nullptr : player);
        b.toldMs = now;
    };
    bool const volley = b.strikes != b.spell;
    if (!b.launched)
    {
        b.launched = true;
        b.lastMs = now;
        // A volley's later missile leaves from where the caster's eye is now.
        if (b.volley)
            b.at = sFusionMgr.EyeOf(player);
        // A homing bolt picks its target now: the selected (Tab) one roughly ahead, else the enemy
        // nearest the crosshair within its cone (fusion/Spellcasting.h HomingTarget).
        if (b.homing.On())
        {
            std::vector<SC::Body> bodies;
            std::vector<Unit*> units;
            float const half = SC::kHomingAcquireYards * 0.5f;
            sFusionMgr.BoltBodies(player, b.at + b.dir * half, half + 3.f, bodies, units);
            int const i = SC::HomingTarget(b.at, b.dir, b.homing.coneDeg, bodies,
                player->GetSelectionGuid().GetRawValue());
            if (i >= 0)
                b.homeOn = bodies[size_t(i)].guid;
        }
        if (b.speed > 0.f)
            tellCourse(uint8((volley ? SC::BOLT_VOLLEY : 0) | (b.homeOn ? SC::BOLT_HOMING : 0)));
    }
    uint32 const dt = now - b.lastMs;
    b.lastMs = now;
    // Homing: turn toward the target's chest, at most its turn rate for this step's time.
    if (b.homeOn)
    {
        Unit* t = map->GetUnit(ObjectGuid(b.homeOn));
        if (t && t->IsAlive() && t->IsInWorld())
            b.dir = SC::SteerToward(b.dir, ChestOf(t) - b.at, b.homing.turnDegPerSec * kDegToRad * float(dt) / 1000.f);
        else
        {
            // Its target gone: on straight from here, and those near are told so.
            b.homeOn = 0;
            if (b.speed > 0.f)
                tellCourse(uint8((volley ? SC::BOLT_VOLLEY : 0) | SC::BOLT_HOMING));
        }
    }
    float const step = SC::StepYards(b.speed, dt, b.left);
    if (step > 0.f)
    {
        Traced const t = TraceBolt(player, b.at, b.dir, step, b.claim);
        if (t.strike.kind != SC::StrikeKind::None)
        {
            BoltStrikes(player, b, b.at + b.dir * t.strike.distance, t.unit,
                t.strike.kind == SC::StrikeKind::Piece ? t.strike.piece : 0, false);
            return true;
        }
        b.at = b.at + b.dir * step;
        b.left -= step;
    }
    // Its fizzle distance flown (or the safety cap, or too long aloft): it fades where it is.
    if (b.left <= 0.01f || int32(now - b.startMs) > int32(SC::kMaxFlightMs))
    {
        BoltStrikes(player, b, b.at, nullptr, 0, true);
        return true;
    }
    if (b.homeOn && b.speed > 0.f && now - b.toldMs >= SC::kSteerTellMs)
        tellCourse(uint8((volley ? SC::BOLT_VOLLEY : 0) | SC::BOLT_HOMING));
    return false;
}

namespace
{
    // The splash's share for a splashed enemy, for the damage hook: as long as the spell's own
    // auras run (its damage over time is cut too), a moment for a direct hit's.
    void NoteShare(FusionClassState& s, Unit* u, uint32 chain, float scale, uint32 untilMs)
    {
        uint64 const g = u->GetObjectGuid().GetRawValue();
        s.splashShares.erase(std::remove_if(s.splashShares.begin(), s.splashShares.end(),
            [&](FusionSplashShare const& x) { return x.victim == g && x.chain == chain; }), s.splashShares.end());
        if (scale < 1.f)
            s.splashShares.push_back({g, chain, scale, untilMs});
    }
}

void FusionClasses::BoltStrikes(Player* player, FusionHandBolt const& b, Fusion::Vec3 at, Unit* unit, uint32 piece, bool spent)
{
    SpellEntry const* info = sSpellMgr.GetSpellEntry(b.strikes);
    if (!spent && info)
    {
        Map* map = player->GetMap();
        FusionClassState& s = StateOf(player);
        uint32 const now = Now();
        uint32 const chain = sSpellMgr.GetFirstSpellInChain(info->Id) ? sSpellMgr.GetFirstSpellInChain(info->Id) : info->Id;
        bool const direct = unit && unit->IsAlive();
        // The bolt's own release power, whichever hand charged it.
        ReleaseScope const scope(s, chain, b.power);
        s.splashShares.erase(std::remove_if(s.splashShares.begin(), s.splashShares.end(),
            [now](FusionSplashShare const& x) { return int32(now - x.untilMs) >= 0; }), s.splashShares.end());
        if (b.ownArea)
        {
            // The spell's own area about the impact does the splash, as the spell has it.
            if (direct)
                Arrive(player, info, unit, at, true);
            else
            {
                // At the world: centred on the impact, on the enemy nearest it that it can see.
                std::vector<SC::Body> bodies;
                std::vector<Unit*> units;
                sFusionMgr.BoltBodies(player, at, b.splash + 2.f, bodies, units);
                Fusion::Vec3 const from = at - b.dir * 0.3f;
                Unit* nearest = nullptr;
                float best = 0.f;
                for (size_t i : SC::InBlast(at, b.splash, bodies))
                {
                    float const d = (bodies[i].pose.feet - at).Length();
                    if ((!nearest || d < best) && sFusionMgr.BoltSees(map, from, SC::BodyCentre(bodies[i])))
                    {
                        nearest = units[i];
                        best = d;
                    }
                }
                if (nearest)
                    Arrive(player, info, nearest, at, true);
            }
        }
        else
        {
            // The one it hit takes the spell whole.
            if (direct)
            {
                NoteShare(s, unit, chain, 1.f, 0);
                Arrive(player, info, unit, at, false);
            }
            // Those around, a share by how near (fusion/Spellcasting.h Splash): enemies only, in
            // sight of the impact (a little back along the flight, off the surface it struck).
            if (b.splash > 0.f)
            {
                std::vector<SC::Body> bodies;
                std::vector<Unit*> units;
                sFusionMgr.BoltBodies(player, at, b.splash + 2.f, bodies, units);
                int hitIndex = -1;
                for (size_t i = 0; i < units.size(); ++i)
                    if (direct && units[i] == unit)
                        hitIndex = int(i);
                Fusion::Vec3 const from = at - b.dir * 0.3f;
                auto sees = [&](Fusion::Vec3, Fusion::Vec3 chest) { return sFusionMgr.BoltSees(map, from, chest); };
                uint32 const lasts = std::max<uint32>(1500, LongestAuraMs(info) + 1000);
                // A slow or a debuff without damage over time lasts its share; one with it keeps its
                // length and deals its share (the damage hook).
                bool dot = false, timed = false;
                for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (info->Effect[i] == SPELL_EFFECT_APPLY_AURA)
                    {
                        SC::AuraKind const k = SC::KindOfAura(info->EffectApplyAuraName[i]);
                        dot = dot || k == SC::AuraKind::DamageOverTime;
                        timed = timed || k == SC::AuraKind::Slow || k == SC::AuraKind::Other;
                    }
                for (SC::SplashHit const& h : SC::Splash(at, b.splash, bodies, hitIndex, sees))
                {
                    Unit* u = units[h.body];
                    if (!u || !u->IsAlive())
                        continue;
                    NoteShare(s, u, chain, h.scale, now + lasts);
                    if (timed && !dot && LongestAuraMs(info))
                    {
                        FusionAuraFix fix;
                        fix.target = u->GetObjectGuid().GetRawValue();
                        fix.spell = chain;
                        fix.ms = SC::SplashAuraMs(LongestAuraMs(info), h.scale);
                        fix.untilMs = now + 3000;
                        s.auraFixes.push_back(fix);
                    }
                    Arrive(player, info, u, at, false, h.scale);
                }
            }
        }
        // The built pieces take its damage: the one struck, and those in its splash.
        sFusionMgr.SpellStrikesPieces(player, info, at, piece, b.splash);
    }
    SC::BoltEvent e;
    e.kind = SC::BoltEventKind::Impact;
    e.id = b.id;
    e.spell = b.strikes;
    e.caster = player->GetObjectGuid().GetRawValue();
    e.to = at;
    e.unit = unit && !spent ? unit->GetObjectGuid().GetRawValue() : 0;
    // Spent at its fizzle distance: a fizzle (its puff); at the safety cap it just fades.
    bool const fizzled = spent && b.fizzles && b.left <= 0.01f;
    e.flags = uint8((piece ? SC::BOLT_HIT_PIECE : 0) | (spent ? SC::BOLT_SPENT : 0) | (fizzled ? SC::BOLT_FIZZLED : 0));
    sFusionMgr.TellNearOn(player->GetMap(), kChannelClasses, at, SC::EncodeBoltEvent(e), nullptr);
}

float FusionClasses::SplashShareOf(FusionClassState const& s, Unit const* victim, SpellEntry const* spell, uint32 now) const
{
    if (s.splashShares.empty() || !victim || !spell)
        return 1.f;
    uint64 const g = victim->GetObjectGuid().GetRawValue();
    uint32 const chain = sSpellMgr.GetFirstSpellInChain(spell->Id) ? sSpellMgr.GetFirstSpellInChain(spell->Id) : spell->Id;
    for (FusionSplashShare const& x : s.splashShares)
        if (x.victim == g && x.chain == chain && int32(x.untilMs - now) > 0)
            return x.scale;
    return 1.f;
}

bool FusionClasses::SplashSkipsEffect(Spell const* spell, uint8 effIndex) const
{
    if (!spell || effIndex >= MAX_EFFECT_INDEX)
        return false;
    SpellEntry const* e = spell->m_spellInfo;
    uint32 const eff = e->Effect[effIndex];
    if (SC::ControlEffect(eff))
        return true;
    return (eff == SPELL_EFFECT_APPLY_AURA || eff == SPELL_EFFECT_PERSISTENT_AREA_AURA)
        && SC::KindOfAura(e->EffectApplyAuraName[effIndex]) == SC::AuraKind::HardControl;
}

// ── Sprays (fusion/Spellcasting.h, Shape::Spray) ──
//
// The client sends op 43 when a hand's button goes down on a spray, again every 100 ms with the
// aim while it is held, and once more when it comes up. Each update the server ticks every spray
// pouring: the mana, then each enemy in the cone it can see takes the tick's class damage (and a
// frost spray's chill). Out of mana, silenced, stunned, dead, or not heard of for 600 ms: it stops,
// and the client's hand fizzles. Everyone near is told of its course (op 44) to draw it.

void FusionClasses::TellSpray(Player* player, FusionHandSpray const& sp, uint8 hand, bool on)
{
    SC::SprayEvent e;
    e.caster = player->GetObjectGuid().GetRawValue();
    e.spell = sp.spell;
    e.hand = hand;
    e.on = on;
    e.from = sp.from;
    e.dir = sp.dir;
    uint32 const first = sSpellMgr.GetFirstSpellInChain(sp.spell) ? sSpellMgr.GetFirstSpellInChain(sp.spell) : sp.spell;
    if (SC::SprayRow const* row = SC::SprayOf(first))
    {
        e.range = row->rangeYards;
        e.halfAngleDeg = row->halfAngleDeg;
    }
    sFusionMgr.TellNearOn(player->GetMap(), kChannelClasses, sp.from, SC::EncodeSprayEvent(e), player);
}

void FusionClasses::StopSpray(Player* player, FusionClassState& s, uint8 hand, bool tellClient)
{
    FusionHandSpray& sp = s.handSprays[hand];
    if (!sp.spell)
        return;
    if (player->IsInWorld())
        TellSpray(player, sp, hand, false);
    if (tellClient)
        sFusionMgr.SendChannel(player, kChannelClasses, SC::EncodeCastResult(hand, SC::CastError::Refused, sp.spell));
    sp = FusionHandSpray{};
}

void FusionClasses::HandSpray(Player* player, std::vector<uint8> const& body)
{
    SC::SprayAim a;
    if (!SC::DecodeSprayAim(body, a))
        return;
    FusionClassState& s = StateOf(player);
    FusionHandSpray& sp = s.handSprays[a.hand];
    if (!a.on)
    {
        StopSpray(player, s, a.hand, false);
        return;
    }
    uint32 const asked = a.spell;
    auto refuse = [&](SC::CastError e)
    {
        StopSpray(player, s, a.hand, false);
        sFusionMgr.SendChannel(player, kChannelClasses, SC::EncodeCastResult(a.hand, e, asked));
    };
    if (!player->HasActiveSpell(a.spell))
        if (uint32 const entry = SingleEntryOf(player, a.spell))
            a.spell = entry;
    auto held = m_handSpells.find(a.spell);
    SpellEntry const* info = sSpellMgr.GetSpellEntry(a.spell);
    if (held == m_handSpells.end() || held->second.shape != SC::Shape::Spray || !info || !player->HasActiveSpell(a.spell)
        || !player->IsInWorld())
        return refuse(SC::CastError::NotHeld);
    if (!player->IsAlive())
        return refuse(SC::CastError::Refused);
    Fusion::Vec3 dir;
    if (!SC::AimDirection(a.dir, dir))
        return refuse(SC::CastError::NoPoint);
    uint32 const now = Now();
    bool const starting = sp.spell != a.spell;
    if (starting)
    {
        // Silenced, stunned, pacified: no spray.
        std::unique_ptr<Spell> probe(new Spell(player, info, false));
        if (probe->CheckCasterAuras() != SPELL_CAST_OK)
            return refuse(SC::CastError::Refused);
        sp = FusionHandSpray{};
        sp.spell = a.spell;
        sp.nextTickMs = now;
        // A spray gives a hider away, as a cast does.
        player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_ACTION_CANCELS | AURA_INTERRUPT_ACTION_CANCELS_LATE, info->Id);
    }
    sp.from = SC::BoltOrigin(a.origin, sFusionMgr.EyeOf(player), sFusionMgr.ShotOriginTolerance(player));
    sp.dir = dir;
    sp.heardMs = now;
    if (starting || now - sp.toldMs >= SC::kSprayTellMs)
    {
        sp.toldMs = now;
        TellSpray(player, sp, a.hand, true);
    }
    // The first tick pours at once.
    if (starting)
        TickHandSprays(player, s, now);
}

void FusionClasses::TickHandSprays(Player* player, FusionClassState& s, uint32 now)
{
    for (uint8 hand = 0; hand < 2; ++hand)
    {
        FusionHandSpray& sp = s.handSprays[hand];
        if (!sp.spell)
            continue;
        SpellEntry const* info = sSpellMgr.GetSpellEntry(sp.spell);
        uint32 const first = info && sSpellMgr.GetFirstSpellInChain(info->Id) ? sSpellMgr.GetFirstSpellInChain(info->Id) : sp.spell;
        SC::SprayRow const* row = SC::SprayOf(first);
        // Let go without a word (a lost message, the hands lowered, a disconnect): it stops.
        if (!info || !row || !player->IsInWorld() || !player->IsAlive() || int32(now - sp.heardMs) > int32(SC::kSprayAimTimeoutMs))
        {
            StopSpray(player, s, hand, false);
            continue;
        }
        while (int32(now - sp.nextTickMs) >= 0 && sp.spell)
        {
            sp.nextTickMs += row->tickMs;
            // Behind by more than a tick (a stall): one tick now, not a burst.
            if (int32(now - sp.nextTickMs) > int32(row->tickMs))
                sp.nextTickMs = now + row->tickMs;
            std::unique_ptr<Spell> probe(new Spell(player, info, false));
            if (probe->CheckCasterAuras() != SPELL_CAST_OK)
            {
                StopSpray(player, s, hand, true);
                break;
            }
            // The tick's mana, as a cast pays it (and the five-second rule's wait).
            if (player->GetPowerType() == POWER_MANA && !player->HasCheatOption(PLAYER_CHEAT_NO_POWER))
            {
                uint32 const cost = SC::SprayTickMana(player->GetMaxPower(POWER_MANA), *row);
                if (player->GetPower(POWER_MANA) < cost)
                {
                    StopSpray(player, s, hand, true);
                    break;
                }
                player->ModifyPower(POWER_MANA, -int32(cost));
                player->SetLastManaUse(info->Id);
            }
            // Every enemy in the cone the hand can see.
            std::vector<SC::Body> bodies;
            std::vector<Unit*> units;
            sFusionMgr.BoltBodies(player, sp.from + sp.dir * (row->rangeYards * 0.5f), row->rangeYards * 0.5f + 3.f,
                bodies, units);
            Map* map = player->GetMap();
            auto sees = [&](Fusion::Vec3 a, Fusion::Vec3 b) { return sFusionMgr.BoltSees(map, a, b); };
            // The bolt it is measured against: the rank this level casts, as fast as it charges.
            SpellEntry const* bolt = RankForLevel(row->boltChain, player->GetLevel());
            int const boltEffect = bolt ? DamageEffectOf(bolt) : -1;
            uint16 const boltCharge = bolt ? SC::ChargeMs(HandFacts(bolt)) : 0;
            float const boltBase = boltEffect < 0 ? 0.f
                : AverageEffectValue(bolt, boltEffect, player->GetLevel()) * SC::BelowRankScale(player->GetLevel(), bolt->spellLevel);
            SpellSchoolMask const schoolMask = info->GetSpellSchoolMask();
            for (size_t i : SC::SprayCatches(sp.from, sp.dir, *row, bodies, sees))
            {
                Unit* u = units[i];
                if (!u || !u->IsAlive())
                    continue;
                // The bolt's hit with the caster's spell power and talents, as a cast at this
                // victim would have it; the tick its share; then the victim's own modifiers and a
                // crit of the spray's.
                float const boltHit = boltEffect < 0 ? 0.f
                    : player->SpellDamageBonusDone(u, bolt, SpellEffectIndex(boltEffect), boltBase, SPELL_DIRECT_DAMAGE);
                float tick = SC::SprayTickDamage(*row, boltHit, boltCharge);
                tick = u->SpellDamageBonusTaken(player, info, EFFECT_INDEX_0, tick, SPELL_DIRECT_DAMAGE);
                SpellNonMeleeDamage dmg(player, u, info->Id, SpellSchools(info->School));
                uint32 amount = std::max<uint32>(1, rand_ditheru(tick));
                if (player->IsSpellCrit(u, info, schoolMask))
                {
                    dmg.hitTypeFlags |= SPELL_HIT_TYPE_CRIT;
                    amount = player->SpellCriticalDamageBonus(info, amount, u);
                }
                dmg.damage = amount;
                // A spell hit like any other: resisted and absorbed in part, in the combat log,
                // onto the CoD-scale pool (FusionMgr::ConvertDamage) with the class effects.
                u->CalculateAbsorbResistBlock(player, &dmg, info);
                player->DealDamageMods(u, dmg.damage, &dmg.absorb);
                player->SendSpellNonMeleeDamageLog(&dmg);
                player->DealSpellDamage(&dmg, true);
                if (row->chillMs && u->IsAlive())
                    if (SpellAuraHolder* h = u->AddAura(12484, 0, player))      // Chilled: 30% slower
                    {
                        h->SetAuraMaxDuration(int32(row->chillMs));
                        u->RefreshAura(12484, int32(row->chillMs));
                    }
            }
        }
    }
}
