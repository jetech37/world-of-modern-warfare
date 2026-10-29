/*
 * World of Modern Warfare: the class options' custom parts (design/CLASS_CATALOG.md, routes B and
 * C, and the route A tweaks a spell's data can't carry) and the shared systems they stand on:
 * archetype resources, bullet-blocking shapes, smoke, spotting, temporary minions and timed
 * effects. Numbers are the catalog's; %EH is a share of an equal-level player's pool
 * (Fusion::Scale::PlayerMaxHealth).
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "FusionClasses.h"
#include "FusionMgr.h"
#include "fusion/ClassTuning.h"
#include "fusion/DruidForms.h"
#include "CellImpl.h"
#include "Database/DBCStores.h"
#include "Chat.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Totem.h"
#include "GameObject.h"
#include "Item.h"
#include "LootMgr.h"
#include "HostileRefManager.h"
#include "Utilities/Random.h"
#include "World.h"
#include "fusion/Scale.h"

#include <algorithm>
#include <cmath>
#include <list>

using namespace Fusion::Classes;

namespace
{
    constexpr float kPi = 3.14159265f;

    // Shared effect kinds (option ids are below 1200; these sit apart).
    constexpr uint16 kFxFrenzy = 2001;          // Berserker's Frenzy: faster, fewer bullets hurt
    constexpr uint16 kFxZenith = 2002;          // Lightwarden's Zenith: heals stronger
    constexpr uint16 kFxEclipse = 2003;         // Starcaller's Eclipse
    constexpr uint16 kFxOpening = 2004;         // Assassin: just out of stealth
    constexpr uint16 kFxRegen = 2005;           // value: % of max health per second
    constexpr uint16 kFxAbsorb = 2006;          // value: damage of any kind it takes
    constexpr uint16 kFxBulletCut = 2007;       // value: bullet damage multiplier
    constexpr uint16 kFxSpeed = 2008;           // value: run speed multiplier
    constexpr uint16 kFxDamageCut = 2009;       // value: every damage multiplier
    constexpr uint16 kFxAbsorbBullet = 2010;    // value: bullet damage it takes (Holy Shield)
    constexpr uint16 kFxAbsorbSpell = 2011;     // value: non-bullet damage it takes (Hex Ward)
    constexpr uint16 kFxOtherCut = 2012;        // value: non-bullet damage multiplier
    constexpr uint16 kFxFamiliar = 2013;        // source: the familiar soaking 30%
    constexpr uint16 kFxDrawFire = 2014;        // source: the warrior who taunted
    constexpr uint16 kFxDemoralized = 2015;     // value: damage dealt multiplier
    constexpr uint16 kFxBulletMiss = 2016;      // value: the chance a round misses
    constexpr uint16 kFxLanding = 2017;         // Fel Leap: the landing's blast when it ends
    constexpr uint16 kFxScale = 2018;           // a grown form: its scale is put back when it ends
    constexpr uint16 kFxRemoveAura = 2019;      // value: an aura (spell id) taken off when it ends
    constexpr uint16 kFxDemorph = 2020;         // a borrowed look (Tree of Life): the own one back when it ends
    constexpr uint16 kFxEndCharm = 2021;        // source: a charmed unit; value: the charm, broken when it ends
    constexpr uint16 kFxSpirit = 2022;          // Spirit of Redemption: unharmed, then down
    constexpr uint16 kFxUntargetable = 2023;    // Smoke Bomb's second out of reach
    constexpr uint16 kFxWeave = 2024;           // value: the next spell's damage multiplier
    constexpr uint16 kFxCombustion = 2025;      // value: spells left to crit and weave
    constexpr uint16 kFxSacrifice = 2026;       // source: the paladin taking half
    constexpr uint16 kFxVampiric = 2027;        // Vampiric Embrace on the priest
    constexpr uint16 kFxDevotion = 2028;        // value: Prayer of Healing's multiplier, cast now
    constexpr uint16 kFxMomentumCrit = 2029;    // Cat Form's momentum: the next blow doubles
    constexpr uint16 kFxHexWard = 2030;         // the next control effect is shrugged off
    constexpr uint16 kFxBonusHealth = 2031;     // more max health (Last Stand's aura), taken off at its end
    constexpr uint16 kFxHellfire = 2032;        // Hellfire burning round the warlock
    constexpr uint16 kFxNoSprint = 2033;        // may not sprint
    constexpr uint16 kFxMirrorEnd = 2034;       // Hall of Mirrors' cooldown starts when it ends
    constexpr uint16 kFxArcaneEdge = 2035;      // Arcane Edge: the next Wizard damage spell instant and double
    constexpr uint16 kFxFlank = 2036;           // Distract: the Assassin's openers count as from behind
    constexpr uint16 kFxRiposte = 2037;         // Riposte: blows from in front are parried
    constexpr uint16 kFxLungeBlow = 2038;       // source: Lunge's target, struck when the dash lands

    constexpr uint32 kTreantDisplay = 864;      // Treant (creature 1964)
    constexpr uint32 kSpiritDisplay = 16031;    // the spirit of redemption's look

    // Generic auras used for their effect, with the duration set here (spell_template rows).
    constexpr uint32 kAuraStun = 56;            // Stun
    constexpr uint32 kAuraDisarm = 676;         // Disarm: no gun, no weapon (BlocksFire)
    constexpr uint32 kAuraSilence = 23207;      // Silence
    constexpr uint32 kAuraDazed = 13496;        // Dazed: half speed
    constexpr uint32 kAuraInvisible = 7870;     // Lesser Invisibility
    constexpr uint32 kAuraSlowFall = 130;       // Slow Fall
    constexpr uint32 kAuraChill = 12484;        // Chilled: 30% slower (its amount set where it is cast)
    constexpr uint32 kAuraBonusHealth = 12976;  // Last Stand's: max health up by its amount
    constexpr uint32 kAuraSoulstone = 20765;    // Soulstone Resurrection: the stone's look
    constexpr uint32 kAuraFrostTrap = 13810;    // Frost Trap Aura: the Tar Trap's ground
    constexpr uint32 kAuraCamouflage = 5858;    // Dryad Camouflage: a stealth
    constexpr uint32 kAuraLockpicking = 9133;   // Lockpicking: + skill (Light Fingers' bonus)

    // Spells the custom parts recognise by their first rank.
    constexpr uint32 kSpellFrostNova = 122;
    constexpr uint32 kSpellPrayerOfHealing = 596;
    constexpr uint32 kSpellBash = 5211;
    constexpr uint32 kSpellPounce = 9005;
    constexpr uint32 kSpellMaul = 6807;
    constexpr uint32 kSpellFeralCharge = 16979;
    constexpr uint32 kBearTrapObject = 111148;  // Placing Bear Trap's trap
    constexpr uint32 kArcaneStim = 93450;       // Conjure Supplies' item (21_class_options.sql)
    constexpr uint32 kArcaneStimsMax = 4;

    // Creatures the minions are made of.
    constexpr uint32 kSkeleton = 11197;         // Mindless Skeleton
    constexpr uint32 kGhoul = 16141;            // Ghoul Berserker
    constexpr uint32 kAbomination = 16428;      // Unstoppable Abomination
    constexpr uint32 kStalker = 15214;          // Invisible Stalker: a decoy takes the player's look
    constexpr uint32 kWisp = 7570;              // Elven Wisp

    uint32 Now() { return WorldTimer::getMSTime(); }

    Fusion::Vec3 Pos(WorldObject const* o) { return {o->GetPositionX(), o->GetPositionY(), o->GetPositionZ()}; }

    float EqualHealth(uint32 level) { return Fusion::Scale::PlayerMaxHealth(level, 0.f); }

    uint32 Eh(Player* p, float pct)
    {
        return std::max<uint32>(1, uint32(EqualHealth(p->GetLevel()) * pct / 100.f + 0.5f));
    }

    bool Hostile(Player* p, Unit* u) { return u && u->IsAlive() && p->IsValidAttackTarget(u); }

    // Class damage on the CoD scale (a spell's own damage is converted by FusionMgr instead).
    void Strike(Player* p, Unit* t, float pctEH)
    {
        if (Hostile(p, t))
            sFusionMgr.Hit(p, t, Eh(p, pctEH));
    }

    void HealPct(Player* p, Unit* t, float pct, SpellEntry const* e)
    {
        if (t && t->IsAlive() && t->GetHealth() < t->GetMaxHealth())
            p->DealHeal(t, std::max<uint32>(1, uint32(t->GetMaxHealth() * pct / 100.f)), e);
    }

    // An aura for exactly `ms` (the generic auras above, whatever their own duration).
    void TimedAura(Unit* t, uint32 spell, uint32 ms, Unit* caster)
    {
        if (!t || !t->IsAlive())
            return;
        if (SpellAuraHolder* h = t->AddAura(spell, 0, caster))
        {
            h->SetAuraMaxDuration(int32(ms));
            t->RefreshAura(spell, int32(ms));
        }
    }

    std::vector<Unit*> Enemies(Player* p, WorldObject const* around, float range)
    {
        std::list<Unit*> units;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(around, p, range);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(around, searcher, range);
        std::vector<Unit*> out;
        for (Unit* u : units)
            if (Hostile(p, u))
                out.push_back(u);
        return out;
    }

    std::vector<Unit*> EnemiesAt(Player* p, Fusion::Vec3 at, float range)
    {
        std::vector<Unit*> out;
        float const reach = range + (Pos(p) - at).Length();
        for (Unit* u : Enemies(p, p, reach))
            if ((Pos(u) - at).Length() <= range)
                out.push_back(u);
        return out;
    }

    // The player and the members of its group in reach (players only: the effects live on them).
    std::vector<Player*> Party(Player* p, float range, Fusion::Vec3 at)
    {
        std::vector<Player*> out;
        auto take = [&](Player* m)
        {
            if (m && m->IsInWorld() && m->IsAlive() && m->GetMap() == p->GetMap() && m->HasFusion()
                && (Pos(m) - at).Length() <= range)
                out.push_back(m);
        };
        if (Group* g = p->GetGroup())
        {
            for (GroupReference* r = g->GetFirstMember(); r; r = r->next())
                take(r->getSource());
        }
        else
            take(p);
        return out;
    }

    std::vector<Player*> Party(Player* p, float range) { return Party(p, range, Pos(p)); }

    Unit* Selected(Player* p)
    {
        ObjectGuid const g = p->GetSelectionGuid();
        return g.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*p, g);
    }

    // A friendly player selected within reach, else the caster.
    Player* AllyTarget(Player* p, float range)
    {
        if (Unit* u = Selected(p))
            if (Player* a = u->ToPlayer(); a && a->IsAlive() && a->HasFusion() && p->IsFriendlyTo(a)
                && p->IsWithinDistInMap(a, range))
                return a;
        return p;
    }

    // The hostile unit the caster aims at: the spell's target, else its selection, in reach.
    Unit* EnemyTarget(Player* p, Spell* spell, float range)
    {
        Unit* t = spell ? spell->m_targets.getUnitTarget() : nullptr;
        if (!Hostile(p, t))
            t = Selected(p);
        return Hostile(p, t) && p->IsWithinDistInMap(t, range) ? t : nullptr;
    }

    // A point `dist` yards ahead of the unit at ground height.
    Fusion::Vec3 Ahead(Unit* u, float dist)
    {
        float const o = u->GetOrientation();
        float x = u->GetPositionX() + std::cos(o) * dist;
        float y = u->GetPositionY() + std::sin(o) * dist;
        float z = u->GetPositionZ();
        u->UpdateAllowedPositionZ(x, y, z);
        return {x, y, z};
    }

    // Leap toward a point (a knockback the player's client flies): horizontal and vertical speeds
    // for a jump of `dist` yards.
    void LeapTo(Player* p, float angle, float dist, float vertical = 8.f)
    {
        float const g = 19.29f;
        float const flight = 2.f * vertical / g;
        p->KnockBack(angle, std::max(1.f, dist / flight), vertical);
    }

    // Move a unit to a point at once (a pull): a player is knocked there, a creature placed.
    void PullTo(Unit* u, Fusion::Vec3 to, float facing)
    {
        if (Player* t = u->ToPlayer())
        {
            float const dist = std::sqrt((to.x - t->GetPositionX()) * (to.x - t->GetPositionX())
                + (to.y - t->GetPositionY()) * (to.y - t->GetPositionY()));
            LeapTo(t, t->GetAngle(to.x, to.y), dist, 6.f);
        }
        else
            u->NearTeleportTo(to.x, to.y, to.z, facing);
    }

    bool InKitOf(Player* p, FusionClassState const& s, uint16 id) { return InKit(p->GetClass(), s.picks, id); }

    bool PicksArchetype(FusionClassState const& s, uint8 cls, uint8 archetype)
    {
        for (uint16 id : s.picks)
            if (Option const* o = FindOption(id); o && o->cls == cls && o->archetype == archetype)
                return true;
        return false;
    }

    // The dead creature nearest the player within reach (a corpse to raise or burst).
    struct DeadCreatureCheck
    {
        WorldObject const* from;
        float range;
        bool operator()(Creature* c) const
        {
            return c && !c->IsAlive() && !c->IsPet() && c->IsInWorld() && from->IsWithinDistInMap(c, range);
        }
    };

    Creature* NearestCorpse(Player* p, float range)
    {
        std::list<Creature*> found;
        DeadCreatureCheck check{p, range};
        MaNGOS::CreatureListSearcher<DeadCreatureCheck> searcher(found, check);
        Cell::VisitGridObjects(p, searcher, range);
        Creature* best = nullptr;
        for (Creature* c : found)
            if (!best || p->GetDistance(c) < p->GetDistance(best))
                best = c;
        return best;
    }

    // Whether a player's class effects keep it from being thrown about.
    bool KnockImmune(Unit const* u)
    {
        Player const* p = u ? u->ToPlayer() : nullptr;
        if (!p || !p->HasFusion())
            return false;
        auto const& cf = p->GetFusionConst().classFramework;
        return cf && (cf->immunities & IMMUNE_KNOCKBACK);
    }

    // Throw a unit `dist` yards along `angle` (a knockback): a player flies it, a creature jumps.
    void Shove(Unit* u, float angle, float dist, float vertical = 5.f)
    {
        if (!u || !u->IsAlive() || KnockImmune(u))
            return;
        if (Player* p = u->ToPlayer())
        {
            LeapTo(p, angle, dist, vertical);
            return;
        }
        Creature* c = u->ToCreature();
        if (!c || c->IsWorldBoss() || c->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
            return;
        float x = u->GetPositionX() + std::cos(angle) * dist;
        float y = u->GetPositionY() + std::sin(angle) * dist;
        float z = u->GetPositionZ();
        u->UpdateAllowedPositionZ(x, y, z);
        c->GetMotionMaster()->MoveJump(x, y, z, std::max(8.f, dist * 2.f), vertical);
    }

    // More max health for `ms` (Unbreakable, Avatar of Stone, Demon Form, Worgen Form): Last
    // Stand's own aura, at the share given.
    void BonusHealth(Player* p, float share, uint32 ms)
    {
        p->RemoveAurasDueToSpell(kAuraBonusHealth);
        p->CastCustomSpell(p, kAuraBonusHealth, int32(p->GetMaxHealth() * share), {}, {}, true);
        if (SpellAuraHolder* h = p->GetSpellAuraHolder(kAuraBonusHealth))
        {
            h->SetAuraMaxDuration(int32(ms));
            p->RefreshAura(kAuraBonusHealth, int32(ms));
        }
    }

    // Slowed by `pct` for `ms` (Chilled at that amount).
    void Slow(Unit* t, Player* by, float pct, uint32 ms)
    {
        if (!t || !t->IsAlive())
            return;
        if (pct <= 30.f)
        {
            TimedAura(t, kAuraChill, ms, by);
            return;
        }
        by->CastCustomSpell(t, kAuraChill, int32(-pct), {}, {}, true);
        if (SpellAuraHolder* h = t->GetSpellAuraHolder(kAuraChill))
        {
            h->SetAuraMaxDuration(int32(ms));
            t->RefreshAura(kAuraChill, int32(ms));
        }
    }

    // The party member in reach with the least health (a share), other than `except`.
    Player* LowestAlly(Player* p, float range, Fusion::Vec3 at, Unit const* except)
    {
        Player* best = nullptr;
        float bestShare = 2.f;
        for (Player* m : Party(p, range, at))
        {
            if (m == except)
                continue;
            float const share = float(m->GetHealth()) / float(std::max<uint32>(1, m->GetMaxHealth()));
            if (share < bestShare)
            {
                best = m;
                bestShare = share;
            }
        }
        return best;
    }

    // The enemies nearest `around` (not `except`), closest first, at most `n`.
    std::vector<Unit*> NearestEnemies(Player* p, Unit* around, float range, size_t n, Unit const* except)
    {
        std::vector<Unit*> e = Enemies(p, around, range);
        e.erase(std::remove(e.begin(), e.end(), except), e.end());
        std::sort(e.begin(), e.end(), [around](Unit* a, Unit* b) { return around->GetDistance(a) < around->GetDistance(b); });
        if (e.size() > n)
            e.resize(n);
        return e;
    }

    // The control effects a Hex Ward shrugs off.
    constexpr AuraType kControlAuras[] = {SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE,
        SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_SILENCE, SPELL_AURA_MOD_CHARM, SPELL_AURA_MOD_POSSESS};

    // A melee blow's custom procs run outside the blow (they deal damage of their own).
    thread_local bool t_inProcs = false;
    // Blessing of Sacrifice moves damage to a paladin, who may be under one too.
    thread_local bool t_inSacrifice = false;
}

// ── Resources ──

Meter* FusionClasses::MeterOf(FusionClassState& s, Resource r)
{
    for (Meter& m : s.meters)
        if (m.resource == r)
            return &m;
    return nullptr;
}

void FusionClasses::Gain(Player* player, FusionClassState& s, Resource r, uint16 n)
{
    Meter* m = MeterOf(s, r);
    if (!m || !n)
        return;
    uint32 const now = Now();
    // Avenging Crusade doubles Holy Power gains.
    if (r == Resource::HolyPower && HasEffect(s, OptionId(kPaladin, 6, 0), now))
        n = uint16(n * 2);
    if (!m->Add(n))
        return;
    s.metersDirty = true;
    (void)now;
}

void FusionClasses::Frenzy(Player* player, FusionClassState& s, uint32 ms)
{
    // The Berserker's Frenzy (it was 5 Fury): faster, fewer bullets hurt; it refunds Intercept.
    uint32 const now = Now();
    bool const fresh = !HasEffect(s, kFxFrenzy, now);
    AddEffect(player, s, kFxFrenzy, ms);
    if (!fresh)
        return;
    if (InKitOf(player, s, OptionId(kWarrior, 2, 0)))
        if (SpellEntry const* e = sSpellMgr.GetSpellEntry(RankFor(20617, uint8(std::min<uint32>(player->GetLevel(), 255)))))
            player->RemoveSpellCooldown(e, true);
    ChatHandler(player).SendSysMessage("|cffff4020Frenzy!|r");
}

void FusionClasses::Zenith(Player* player, FusionClassState& s)
{
    // The Lightwarden's sun at Zenith (it was 100 Solar Power): heals +50% for a while.
    if (HasEffect(s, kFxZenith, Now()))
        return;
    AddEffect(player, s, kFxZenith, uint32(kZenithSeconds * 1000.f));
    ChatHandler(player).SendSysMessage("|cffffe060The sun is at Zenith.|r");
}

bool FusionClasses::HasArcaneEdge(FusionClassState const& s) const
{
    return HasEffect(s, kFxArcaneEdge, Now());
}

void FusionClasses::Eclipse(Player* player, FusionClassState& s)
{
    // The Starcaller's Eclipse (it was 3 Lunar and 3 Solar).
    if (HasEffect(s, kFxEclipse, Now()))
        return;
    AddEffect(player, s, kFxEclipse, InKitOf(player, s, OptionId(kDruid, 5, 2)) ? 15000 : 10000);
    ChatHandler(player).SendSysMessage("|cff80a0ffEclipse.|r");
}

void FusionClasses::GainOnCast(Player* player, FusionClassState& s, Option const& o, Spell* spell)
{
    uint32 const now = Now();
    switch (o.id)
    {
        case OptionId(kWarrior, 1, 0):                                                  // Bloodthirst
            if (frand(0.f, 1.f) < kFrenzyChance)
                Frenzy(player, s, kFrenzyMs);
            break;
        case OptionId(kWarrior, 3, 0):                                                  // Whirlwind
            if (Enemies(player, player, 8.f).size() >= kFrenzyCrowd)
                Frenzy(player, s, kFrenzyMs);
            break;
        case OptionId(kPaladin, 1, 0): Gain(player, s, Resource::HolyPower, 1); break;    // Crusader Strike
        case OptionId(kPaladin, 5, 0): Gain(player, s, Resource::HolyPower, 2); break;    // Repentance
        case OptionId(kShaman, 1, 0):                                                    // Lightning Bolt
        case OptionId(kShaman, 3, 0): Gain(player, s, Resource::Static, 1); break;       // Frost Shock
        case OptionId(kShaman, 2, 0): Gain(player, s, Resource::Static, 2); break;       // Chain Lightning
        default:
            break;
    }
    // Lightwarden: a heal may bring the sun to Zenith.
    if (uint8 const chance = ZenithChance(o.id); chance && urand(0, 99) < chance)
        Zenith(player, s);
    // Starcaller: Moonfire, Faerie Fire and Lunar Strike may bring Eclipse.
    if (float const chance = EclipseChance(o.id, false); chance > 0.f && frand(0.f, 1.f) < chance)
        Eclipse(player, s);
    // Wizard: Spellweave pays a switch of school from the last spell. Combustion makes the next
    // three crit and count as a switch.
    if (o.cls == kMage && o.archetype == 0 && o.tier != kCoreTier)
    {
        uint32 const school = spell->m_spellInfo->School;
        float payoff = WeaveBonus(s.lastSchool, school);
        if (FusionClassEffect* c = EffectOf(s, kFxCombustion, now); c && c->value >= 1.f)
        {
            payoff = 1.5f * kWeaveBonus;       // a crit (x1.5) of a switch
            c->value -= 1.f;
            if (c->value < 1.f)
                c->untilMs = now;
        }
        // Arcane Edge: the spell it was waiting for is instant (CastTime) and deals double.
        if (FusionClassEffect* edge = EffectOf(s, kFxArcaneEdge, now))
        {
            payoff *= 2.f;
            edge->untilMs = now;
        }
        if (payoff > 1.f)
            AddEffect(player, s, kFxWeave, 4000, 0, payoff);
    }
    // Grand Array: constructs placed during it have no cooldown.
    if (o.cls == kMage && o.archetype == 2 && o.tier != kCoreTier && o.tier != 6 && HasEffect(s, OptionId(kMage, 6, 2), now))
        player->RemoveSpellCooldown(spell->m_spellInfo, true);
    // Solar Ascension: every Lightwarden heal also heals the two weakest allies within 15 yd of
    // its target for half.
    if (o.cls == kPaladin && o.archetype == 2 && HasEffect(s, OptionId(kPaladin, 6, 2), now))
    {
        float pct = 0.f;
        switch (o.id)
        {
            case OptionId(kPaladin, 1, 2): pct = 10.f; break;      // Flash of Light, 20%
            case OptionId(kPaladin, 2, 2): pct = 22.5f; break;     // Holy Light, 45%
            case OptionId(kPaladin, 3, 2): pct = 10.f; break;      // Holy Shock, 20%
            case OptionId(kPaladin, 5, 2): pct = 50.f; break;      // Lay on Hands, full
            default: break;
        }
        Unit* t = spell->m_targets.getUnitTarget();
        if (pct > 0.f && t && !player->IsValidAttackTarget(t))
        {
            Unit* first = t;
            for (int i = 0; i < 2; ++i)
                if (Player* m = LowestAlly(player, 15.f, Pos(t), first))
                {
                    HealPct(player, m, pct, spell->m_spellInfo);
                    first = m;      // the next is another one
                }
        }
    }
    // Tree of Life: every HoT also lands on the weakest ally within 20 yd.
    if (o.cls == kDruid && o.archetype == 1 && (o.tier == 1 || o.tier == 2) && HasEffect(s, OptionId(kDruid, 6, 1), now))
        if (Player* m = LowestAlly(player, 20.f, Pos(player), spell->m_targets.getUnitTarget()))
            player->CastSpell(m, spell->m_spellInfo->Id, true);
    // Afflictions are remembered for what their target's death does (Soul Siphon, Doom,
    // Devouring Plague and Shadowform's spread).
    bool const affliction = (o.cls == kWarlock && o.archetype == 0 && o.kind != Kind::Passive)
        || o.id == OptionId(kPriest, 1, 0) || o.id == OptionId(kPriest, 4, 0);
    if (affliction)
        if (Unit* t = spell->m_targets.getUnitTarget(); t && t != player)
        {
            uint64 const g = t->GetObjectGuid().GetRawValue();
            uint32 const id = spell->m_spellInfo->Id;
            s.afflictions.erase(std::remove_if(s.afflictions.begin(), s.afflictions.end(),
                [&](FusionAffliction const& a) { return int32(a.untilMs - now) <= 0 || (a.target == g && a.spell == id); }),
                s.afflictions.end());
            s.afflictions.push_back({g, id, now + 60000});
            // Shadowform: every DoT also slows 30%.
            if ((o.id == OptionId(kPriest, 1, 0) || o.id == OptionId(kPriest, 4, 0))
                && player->GetShapeshiftForm() == FORM_SHADOW)
                Slow(t, player, 30.f, o.id == OptionId(kPriest, 1, 0) ? 18000 : 24000);
        }
    // Trickster, Slip: every Trickster ability makes the rogue untargetable for a moment.
    if (o.cls == kRogue && o.archetype == 1 && o.tier != kCoreTier)
    {
        player->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        AddEffect(player, s, kFxUntargetable, kSlipMs);
    }
    // Adrenaline Rush: Lunge has no cooldown.
    if (o.id == OptionId(kRogue, 2, 2) && HasEffect(s, OptionId(kRogue, 6, 2), now))
        player->RemoveSpellCooldown(spell->m_spellInfo, true);
    // Eclipse: a Starcaller's spells come back at once.
    if (o.cls == kDruid && o.archetype == 2 && HasEffect(s, kFxEclipse, now))
        player->RemoveSpellCooldown(spell->m_spellInfo, true);
    // Rampage: Charge and Intercept have no cooldown.
    if (HasEffect(s, OptionId(kWarrior, 6, 0), now)
        && (o.id == OptionId(kWarrior, 0, 0) || o.id == OptionId(kWarrior, 2, 0)))
        player->RemoveSpellCooldown(spell->m_spellInfo, true);
    // Arcane Avatar: Blink and Blade Blink have no cooldown; Demon Form: Fel Leap neither.
    if ((HasEffect(s, OptionId(kMage, 6, 1), now) && (o.id == OptionId(kMage, 0, 0) || o.id == OptionId(kMage, 2, 1)))
        || (HasEffect(s, OptionId(kWarlock, 6, 1), now) && o.id == OptionId(kWarlock, 2, 1)))
        player->RemoveSpellCooldown(spell->m_spellInfo, true);
}

// ── Effects ──

FusionClassEffect* FusionClasses::EffectOf(FusionClassState& s, uint16 code, uint32 now, uint64 source)
{
    for (FusionClassEffect& e : s.effects)
        if (e.code == code && (!source || e.source == source) && int32(e.untilMs - now) > 0)
            return &e;
    return nullptr;
}

bool FusionClasses::HasEffect(FusionClassState const& s, uint16 code, uint32 now) const
{
    for (FusionClassEffect const& e : s.effects)
        if (e.code == code && int32(e.untilMs - now) > 0)
            return true;
    return false;
}

void FusionClasses::AddEffect(Player* player, FusionClassState& s, uint16 code, uint32 ms, uint64 source, float value)
{
    (void)player;
    uint32 const until = Now() + ms;
    for (FusionClassEffect& e : s.effects)
        if (e.code == code && e.source == source)
        {
            e.untilMs = std::max(e.untilMs, until);
            e.value = value;
            return;
        }
    s.effects.push_back({code, until, source, value});
}

void FusionClasses::EndEffect(Player* player, FusionClassState& s, FusionClassEffect const& e)
{
    switch (e.code)
    {
        case OptionId(kRogue, 6, 0):        // Deathmark: half of what was dealt, again
        {
            Unit* t = ObjectAccessor::GetUnit(*player, ObjectGuid(s.deathmark));
            if (t && t->IsAlive() && s.deathmarkDamage > 0.f && player->IsValidAttackTarget(t))
                sFusionMgr.Hit(player, t, std::max<uint32>(1, uint32(s.deathmarkDamage * 0.5f)));
            s.deathmark = 0;
            s.deathmarkDamage = 0.f;
            break;
        }
        case kFxAbsorbBullet:               // Holy Shield broke or ran out: Aegis consecrates the spot
        {
            FusionClassZone z;
            z.code = OptionId(kPaladin, 1, 1);
            z.mapId = player->GetMapId();
            z.at = Pos(player);
            z.radius = 8.f;
            z.startMs = Now();
            z.untilMs = z.startMs + 4000;
            s.zones.push_back(z);
            break;
        }
        case kFxLanding:                    // Fel Leap lands
            for (Unit* u : Enemies(player, player, 6.f))
            {
                Strike(player, u, 10.f);
                TimedAura(u, kAuraStun, 1000, player);
            }
            break;
        case kFxScale:
            player->SetObjectScale(e.value > 0.f ? e.value : 1.f);
            break;
        case kFxRemoveAura:
            player->RemoveAurasDueToSpell(uint32(e.value));
            break;
        case kFxDemorph:
            player->DeMorph();
            break;
        case kFxEndCharm:                   // Mind Control holds a player 5 s, a creature 30 s
            if (Unit* t = ObjectAccessor::GetUnit(*player, ObjectGuid(e.source)))
                t->RemoveAurasDueToSpell(uint32(e.value));
            break;
        case kFxSpirit:                     // Spirit of Redemption ends: down
            ChatHandler(player).SendSysMessage("|cffffffffThe spirit fades.|r");
            sFusionMgr.GoDownNow(player);
            break;
        case kFxUntargetable:
            player->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            break;
        case kFxLungeBlow:                  // Lunge lands: the blow, if the dash got there
            if (Unit* t = ObjectAccessor::GetUnit(*player, ObjectGuid(e.source)); Hostile(player, t)
                && player->IsWithinDistInMap(t, 6.f))
            {
                Strike(player, t, kLungePct);
                if (Meter* m = MeterOf(s, Resource::Momentum); m && m->Add(1))
                    s.metersDirty = true;
                s.lastBlowMs = Now();
            }
            break;
        case kFxBonusHealth:
            player->RemoveAurasDueToSpell(kAuraBonusHealth);
            break;
        case kFxMirrorEnd:                  // Hall of Mirrors' swaps are over: its cooldown starts
            if (SpellEntry const* info = sSpellMgr.GetSpellEntry(uint32(e.value)))
            {
                uint32 const ms = SpecOf(OptionId(kRogue, 6, 1)) ? SpecOf(OptionId(kRogue, 6, 1))->cooldownMs : 120000;
                player->AddCooldown(info, nullptr, false, ms);
                s.cooldownSync.push_back({info->Id, ms});
            }
            s.mirrorSwaps = 0;
            break;
        case OptionId(kWarlock, 6, 0):      // Curse of Doom falls: 60 %EH, unless it was purged
            if (Unit* t = ObjectAccessor::GetUnit(*player, ObjectGuid(e.source)); t && t->IsAlive())
                if (SpellAuraHolder* curse = t->GetSpellAuraHolder(uint32(e.value), player->GetObjectGuid()))
                {
                    (void)curse;
                    Strike(player, t, 60.f);
                    if (t->IsAlive())
                        t->RemoveAurasDueToSpell(uint32(e.value));     // vanilla's own 60 s hit never comes
                }
            break;
        case OptionId(kPriest, 6, 1):       // Divine Hymn ends: downed allies inside stand up at 50%
            for (Player* m : Party(player, 40.f))
                if (sFusionMgr.IsDowned(m))
                    sFusionMgr.ReviveDownedBy(m, player->GetObjectGuid(), 0.5f);
            break;
        default:
            break;
    }
}

void FusionClasses::TickEffects(Player* player, FusionClassState& s, uint32 now)
{
    float const dt = 0.25f;
    // Ended effects first (an absorb used up ends too).
    std::vector<FusionClassEffect> ended;
    for (auto it = s.effects.begin(); it != s.effects.end();)
    {
        bool const spent = (it->code == kFxAbsorb || it->code == kFxAbsorbBullet || it->code == kFxAbsorbSpell)
            && it->value <= 0.f;
        if (int32(it->untilMs - now) <= 0 || spent)
        {
            ended.push_back(*it);
            it = s.effects.erase(it);
        }
        else
            ++it;
    }
    for (FusionClassEffect const& e : ended)
        EndEffect(player, s, e);
    UpdateImmunities(player, s, now);
    TickAuraFixes(player, s, now);
    TickDots(player, s, now);
    TickRescue(player, s, now);
    if (!player->IsAlive())
    {
        return;
    }
    TickCamouflage(player, s, now);
    s.steps.Update(now);
    // Hex Ward: the next control effect is shrugged off, and the ward with it.
    if (FusionClassEffect* ward = EffectOf(s, kFxHexWard, now))
    {
        std::vector<uint32> holds;      // hostile holds only (an Ice Block is a stun of one's own)
        for (AuraType a : kControlAuras)
            for (Aura* aura : player->GetAurasByType(a))
                if (aura && !aura->IsPositive())
                    holds.push_back(aura->GetId());
        if (!holds.empty())
        {
            for (uint32 id : holds)
                player->RemoveAurasDueToSpell(id);
            ward->untilMs = now;
            ChatHandler(player).SendSysMessage("|cffc080ffThe Hex Ward breaks the hold.|r");
        }
    }
    // Healing over time.
    for (FusionClassEffect const& e : s.effects)
        if (e.code == kFxRegen)
            HealPct(player, player, e.value * dt, nullptr);

    bool const second = (now / 1000) != ((now - 250) / 1000);   // once a second, near enough
    uint8 const cls = player->GetClass();

    // Assassin, the Opening: leaving stealth starts it.
    bool const stealthed = player->HasAuraType(SPELL_AURA_MOD_STEALTH);
    if (s.wasStealthed && !stealthed && cls == kRogue && PicksArchetype(s, kRogue, 0))
        AddEffect(player, s, kFxOpening, kOpeningMs);
    s.wasStealthed = stealthed;
    // Duelist: the blows Riposte parried are struck back.
    if (!s.ripostes.empty())
    {
        std::vector<uint64> back;
        back.swap(s.ripostes);
        for (uint64 g : back)
            if (Unit* u = ObjectAccessor::GetUnit(*player, ObjectGuid(g)); Hostile(player, u)
                && player->IsWithinDistInMap(u, 6.f))
            {
                Strike(player, u, kRipostePct);
                if (Meter* m = MeterOf(s, Resource::Momentum); m && m->Add(2))
                    s.metersDirty = true;
                s.lastBlowMs = now;
            }
    }

    if (!second)
        return;
    // Overwatch: every enemy in front within 100 yd is spotted.
    if (HasEffect(s, OptionId(kHunter, 6, 0), now))
    {
        std::vector<Unit*> seen;
        for (Unit* u : Enemies(player, player, 100.f))
            if (InArc(Pos(player), player->GetOrientation(), kPi / 2.f, Pos(u))
                && !SightBlocked(player->GetMapId(), Pos(player), Pos(u)))
                seen.push_back(u);
        Spot(player, seen, 1500);
    }
    // Starfall: two enemies within 30 yd struck every second.
    if (HasEffect(s, OptionId(kDruid, 6, 2), now))
    {
        std::vector<Unit*> e = Enemies(player, player, 30.f);
        for (size_t i = 0; i < e.size() && i < 2; ++i)
            Strike(player, e[urand(0, uint32(e.size() - 1))], 6.f);
    }
    // Divine Hymn: allies within 40 yd heal 8% a second.
    if (HasEffect(s, OptionId(kPriest, 6, 1), now))
        for (Player* m : Party(player, 40.f))
            HealPct(player, m, 8.f, nullptr);
    // Hellfire: 3 %EH a second to enemies within 10 yd, 1% of the warlock's own health, Fel Fury.
    if (HasEffect(s, kFxHellfire, now))
    {
        for (Unit* u : Enemies(player, player, 10.f))
            Strike(player, u, 3.f);
        uint32 const cost = std::max<uint32>(1, player->GetMaxHealth() / 100);
        if (player->GetHealth() > cost + 1)
            player->ModifyHealth(-int32(cost));
    }
    // Worgen Form: +30% max health while in it.
    if (cls == kDruid)
    {
        bool const worgen = player->GetShapeshiftForm() == ShapeshiftForm(Fusion::DruidForms::kWorgen);
        bool const has = player->HasAura(kAuraBonusHealth);
        if (worgen && !has)
            BonusHealth(player, 0.3f, 3600000);
        else if (!worgen && has)
            player->RemoveAurasDueToSpell(kAuraBonusHealth);
        // Crow Form: the enemies below within 40 yd are spotted for the party.
        if (player->GetShapeshiftForm() == ShapeshiftForm(Fusion::DruidForms::kCrow))
        {
            std::vector<Unit*> below;
            for (Unit* u : Enemies(player, player, 40.f))
                if (u->GetPositionZ() < player->GetPositionZ() - 3.f && player->IsWithinLOSInMap(u))
                    below.push_back(u);
            Spot(player, below, 1500);
        }
    }
    // Grand Array: every 3 s an absorb of 8% for allies within 8 yd.
    if (HasEffect(s, OptionId(kMage, 6, 2), now) && (now / 1000) % 3 == 0)
        for (Player* m : Party(player, 8.f))
            AddEffect(m, StateOf(m), kFxAbsorb, 3000, player->GetObjectGuid().GetRawValue(), m->GetMaxHealth() * 0.08f);
    // Duelist: Momentum ebbs a point a second once the blows stop (Adrenaline Rush holds it full).
    if (Meter* m = MeterOf(s, Resource::Momentum))
    {
        if (HasEffect(s, OptionId(kRogue, 6, 2), now))
        {
            if (m->value != m->max)
            {
                m->value = m->max;
                s.metersDirty = true;
            }
        }
        else if (m->value && now - s.lastBlowMs > kMomentumHoldMs)
        {
            m->value = uint16(m->value - 1);
            s.metersDirty = true;
        }
    }
    // Trickster, Light Fingers: Lockpicking counts 50 higher while it is in the kit.
    if (cls == kRogue)
    {
        bool const fingers = InKit(cls, s.picks, OptionId(kRogue, 4, 1));
        bool const has = player->HasAura(kAuraLockpicking);
        if (fingers && !has)
        {
            player->CastCustomSpell(player, kAuraLockpicking, int32(kLightFingersLockBonus), {}, {}, true);
            if (SpellAuraHolder* h = player->GetSpellAuraHolder(kAuraLockpicking))
            {
                h->SetAuraMaxDuration(-1);
                h->SetAuraDuration(-1);
            }
        }
        else if (!fingers && has)
            player->RemoveAurasDueToSpell(kAuraLockpicking);
    }
    // Holy Power and Static ebb out of combat (Life Force keeps).
    if (!player->IsInCombat())
        for (Meter& m : s.meters)
            if (m.value && (m.resource == Resource::HolyPower || m.resource == Resource::Static))
            {
                m.value = uint16(m.value - 1);
                s.metersDirty = true;
            }
}

// ── Immunities, fitted auras, damage over time, rescues, Camouflage ──

void FusionClasses::UpdateImmunities(Player* player, FusionClassState& s, uint32 now)
{
    uint8 want = 0;
    for (FusionClassEffect const& e : s.effects)
    {
        if (int32(e.untilMs - now) <= 0)
            continue;
        if (e.code == kFxFrenzy)
        {
            if (InKit(player->GetClass(), s.picks, OptionId(kWarrior, 4, 0)))
                want |= ImmunitiesOf(OptionId(kWarrior, 4, 0));     // Blood Frenzy's Frenzy
        }
        else if (e.code < 2000)
            want |= ImmunitiesOf(e.code);
    }
    if (want == s.immunities)
        return;
    // A key of its own, so the class's immunities come and go without touching a spell's.
    constexpr uint32 kKey = 0x7FFF0001;
    struct Grant
    {
        uint8 flag;
        uint32 op;
        uint32 type;
    };
    static Grant const grants[] = {
        {IMMUNE_STUN, IMMUNITY_MECHANIC, MECHANIC_STUN},
        {IMMUNE_STUN, IMMUNITY_MECHANIC, MECHANIC_KNOCKOUT},
        {IMMUNE_FEAR, IMMUNITY_MECHANIC, MECHANIC_FEAR},
        {IMMUNE_FEAR, IMMUNITY_MECHANIC, MECHANIC_HORROR},
        {IMMUNE_SLOW, IMMUNITY_MECHANIC, MECHANIC_SNARE},
        {IMMUNE_SLOW, IMMUNITY_STATE, SPELL_AURA_MOD_DECREASE_SPEED},
        {IMMUNE_ROOT, IMMUNITY_MECHANIC, MECHANIC_ROOT},
        {IMMUNE_KNOCKBACK, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK},
    };
    for (Grant const& g : grants)
    {
        bool const had = s.immunities & g.flag;
        bool const wants = want & g.flag;
        if (had != wants)
            player->ApplySpellImmune(kKey, g.op, g.type, wants);
    }
    // What already holds them goes.
    uint8 const gained = uint8(want & ~s.immunities);
    uint32 mechanics = 0;
    if (gained & IMMUNE_STUN)
        mechanics |= (1 << (MECHANIC_STUN - 1)) | (1 << (MECHANIC_KNOCKOUT - 1));
    if (gained & IMMUNE_FEAR)
        mechanics |= (1 << (MECHANIC_FEAR - 1)) | (1 << (MECHANIC_HORROR - 1));
    if (gained & IMMUNE_SLOW)
        mechanics |= 1 << (MECHANIC_SNARE - 1);
    if (gained & IMMUNE_ROOT)
        mechanics |= 1 << (MECHANIC_ROOT - 1);
    if (mechanics)
        player->RemoveAurasAtMechanicImmunity(mechanics, 0, true);
    if (gained & IMMUNE_SLOW)
    {
        std::vector<uint32> slows;
        for (Aura* a : player->GetAurasByType(SPELL_AURA_MOD_DECREASE_SPEED))
            if (a && !a->IsPositive())
                slows.push_back(a->GetId());
        for (uint32 id : slows)
            player->RemoveAurasDueToSpell(id);
    }
    s.immunities = want;
}

void FusionClasses::TickAuraFixes(Player* player, FusionClassState& s, uint32 now)
{
    if (s.auraFixes.empty())
        return;
    ObjectGuid const me = player->GetObjectGuid();
    // The holder of the fix's spell this player put on `t`; fitted at once.
    auto fit = [&](Unit* t, FusionAuraFix const& f) -> bool
    {
        for (auto const& [id, holder] : t->GetSpellAuraHolderMap())
        {
            if (!holder || holder->GetCasterGuid() != me || sSpellMgr.GetFirstSpellInChain(id) != f.spell)
                continue;
            int32 ms;
            if (f.extend)
                ms = holder->GetAuraDuration() + int32(f.ms);
            else
                ms = int32(t->IsPlayer() || !f.npcMs ? f.ms : f.npcMs);
            if (f.extend)
                holder->SetAuraMaxDuration(std::max(holder->GetAuraMaxDuration(), ms));
            else
                holder->SetAuraMaxDuration(ms);
            holder->SetAuraDuration(ms);
            holder->UpdateAuraDuration();
            return true;
        }
        return false;
    };
    for (auto it = s.auraFixes.begin(); it != s.auraFixes.end();)
    {
        bool done = false;
        if (it->target)
        {
            if (Unit* t = ObjectAccessor::GetUnit(*player, ObjectGuid(it->target)))
                done = fit(t, *it);
        }
        else
            // Frost Nova reaches 10 yd round the mage, or round the crosshair's point 30 yd off.
            for (Unit* u : Enemies(player, player, 45.f))
                done = fit(u, *it) || done;
        if (done || int32(now - it->untilMs) >= 0)
            it = s.auraFixes.erase(it);
        else
            ++it;
    }
}

void FusionClasses::TickDots(Player* player, FusionClassState& s, uint32 now)
{
    if (s.dots.empty())
        return;
    for (auto const& [target, pct] : DueTicks(s.dots, now))
        if (player->IsAlive())
            if (Unit* t = ObjectAccessor::GetUnit(*player, ObjectGuid(target)))
                Strike(player, t, pct);
}

void FusionClasses::TickRescue(Player* player, FusionClassState& s, uint32 now)
{
    if (!s.rescueAtMs || int32(now - s.rescueAtMs) < 0)
        return;
    Rescue const r = Rescue(s.rescue);
    s.rescueAtMs = 0;
    s.rescue = 0;
    if (!sFusionMgr.IsDowned(player))
        return;
    if (r == Rescue::Reincarnation)
    {
        sFusionMgr.ReviveDownedBy(player, player->GetObjectGuid(), kReincarnationShare);
        ChatHandler(player).SendSysMessage("|cff60c0ffReincarnation: you stand back up.|r");
    }
    else if (r == Rescue::Soulstone)
    {
        ObjectGuid const by(s.soulstoneBy);
        s.soulstoneBy = 0;
        s.soulstoneUntilMs = 0;
        player->RemoveAurasDueToSpell(kAuraSoulstone);
        sFusionMgr.ReviveDownedBy(player, by, kSoulstoneShare);
        ChatHandler(player).SendSysMessage("|cffb060ffThe soulstone stands you back up.|r");
    }
}

void FusionClasses::TickCamouflage(Player* player, FusionClassState& s, uint32 now)
{
    if (!s.camo.on)
        return;
    FusionPlayerData const& d = player->GetFusionConst();
    bool const running = player->IsMoving() && !player->m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE);
    bool const fired = s.lastFiredMs && int32(now - s.lastFiredMs) < 500;
    switch (s.camo.Update(now, running, fired, d.sprinting))
    {
        case Camouflage::Step::Fade:
            TimedAura(player, kAuraCamouflage, 3600000, player);
            ChatHandler(player).SendSysMessage("|cff80c080You fade into the surroundings.|r");
            break;
        case Camouflage::Step::Break:
            player->RemoveAurasDueToSpell(kAuraCamouflage);
            ChatHandler(player).SendSysMessage("|cffc08080Camouflage broken.|r");
            break;
        default:
            // Its stealth went some other way (damage, a cast): it fades again once still.
            if (s.camo.hidden && !player->HasAura(kAuraCamouflage))
            {
                s.camo.hidden = false;
                s.camo.stillSinceMs = now;
            }
            break;
    }
}

std::vector<GameObject*> FusionClasses::TrapsOf(Player* player) const
{
    std::vector<GameObject*> out;
    for (GameObject* go : player->GetSpellGameObjects())
    {
        if (!go || !go->isSpawned() || go->GetGOInfo()->type != GAMEOBJECT_TYPE_TRAP)
            continue;
        if (Option const* o = OptionOfSpell(kHunter, go->GetSpellId());
            o && (o->id == OptionId(kHunter, 1, 1) || o->id == OptionId(kHunter, 3, 1) || o->id == OptionId(kHunter, 4, 1)
                || o->id == OptionId(kHunter, 6, 1)))
            out.push_back(go);
    }
    return out;
}

void FusionClasses::MarkUnit(Unit* u, uint16 code, uint32 ms, float value)
{
    std::lock_guard<std::mutex> lock(m_worldLock);
    uint32 const now = Now();
    // Old marks go first (the map stays small).
    for (auto it = m_unitMarks.begin(); it != m_unitMarks.end();)
    {
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(), [now](UnitMarkEntry const& m) { return int32(m.untilMs - now) <= 0; }),
            list.end());
        it = list.empty() ? m_unitMarks.erase(it) : std::next(it);
    }
    std::vector<UnitMarkEntry>& list = m_unitMarks[u->GetObjectGuid().GetRawValue()];
    for (UnitMarkEntry& m : list)
        if (m.code == code)
        {
            m.untilMs = now + ms;
            m.value = value;
            return;
        }
    list.push_back({code, now + ms, value});
}

float FusionClasses::UnitMark(Unit const* u, uint16 code) const
{
    std::lock_guard<std::mutex> lock(m_worldLock);
    auto it = m_unitMarks.find(u->GetObjectGuid().GetRawValue());
    if (it == m_unitMarks.end())
        return 0.f;
    uint32 const now = Now();
    for (UnitMarkEntry const& m : it->second)
        if (m.code == code && int32(m.untilMs - now) > 0)
            return m.value;
    return 0.f;
}

void FusionClasses::AddConstruct(Player* player, FusionClassState& s, uint16 code, uint64 key)
{
    uint32 const now = Now();
    // The ones gone by themselves leave the count.
    s.constructs.erase(std::remove_if(s.constructs.begin(), s.constructs.end(), [&](std::pair<uint16, uint64> const& c)
        {
            if (c.first == OptionId(kMage, 5, 2))       // a familiar: while it lives
            {
                Creature* f = player->GetMap()->GetCreature(ObjectGuid(c.second));
                return !f || !f->IsAlive();
            }
            return int32(uint32(c.second) - now) <= 0;  // a wall or a zone: its end time
        }), s.constructs.end());
    s.constructs.push_back({code, key});
    size_t const limit = ConstructLimit(HasEffect(s, OptionId(kMage, 6, 2), now));
    while (s.constructs.size() > limit)
    {
        std::pair<uint16, uint64> const old = s.constructs.front();
        s.constructs.erase(s.constructs.begin());
        switch (old.first)
        {
            case OptionId(kMage, 1, 2):         // a Warding Rune's screen
            {
                std::lock_guard<std::mutex> lock(m_worldLock);
                for (Blocker& b : m_blockers[player->GetMapId()])
                    if (b.owner == player->GetObjectGuid().GetRawValue() && b.untilMs == uint32(old.second))
                        b.untilMs = now;
                break;
            }
            case OptionId(kMage, 5, 2):         // a familiar
                if (Creature* f = player->GetMap()->GetCreature(ObjectGuid(old.second)))
                    f->DespawnOrUnsummon();
                break;
            default:                            // a glyph, a pair of pads
                for (FusionClassZone& z : s.zones)
                    if (z.code == old.first && z.untilMs == uint32(old.second))
                        z.untilMs = now;
                break;
        }
    }
}

// ── The game's questions ──

bool FusionClasses::BlocksFire(Player const* player) const
{
    if (!m_enabled)
        return false;
    // Disarmed (a warrior's Disarm), pacified (Blessing of Protection), or another's to steer.
    return player->HasAuraType(SPELL_AURA_MOD_DISARM) || player->HasAuraType(SPELL_AURA_MOD_PACIFY)
        || !player->GetCharmerGuid().IsEmpty();
}

void FusionClasses::OnFired(Player* player)
{
    if (!m_enabled || !player->HasFusion() || !player->GetFusion().classFramework)
        return;
    FusionClassState& s = StateOf(player);
    s.lastFiredMs = Now();
}

float FusionClasses::SpreadMultiplier(Player const* player) const
{
    if (!m_enabled)
        return 1.f;
    FusionClassState const* s = StateOf(player);
    return s && HasEffect(*s, kFxDemoralized, Now()) ? 1.5f : 1.f;
}

bool FusionClasses::BlocksSprint(Player const* player) const
{
    if (!m_enabled)
        return false;
    // The Tar Trap's ground holds anyone in it.
    if (player->HasAura(kAuraFrostTrap))
        return true;
    FusionClassState const* s = StateOf(player);
    return s && HasEffect(*s, kFxNoSprint, Now());
}

bool FusionClasses::PreventsDown(Player* player)
{
    if (!m_enabled || !player->HasFusion())
        return false;
    FusionClassState& s = StateOf(player);
    uint32 const now = Now();
    if (HasEffect(s, kFxSpirit, now))
        return true;        // already a spirit: nothing harms it
    bool const spirit = player->GetClass() == kPriest && InKit(kPriest, s.picks, OptionId(kPriest, 5, 1))
        && int32(now - s.spiritReadyMs) >= 0;
    if (RescueOnDown(spirit, false, false) != Rescue::Spirit)
        return false;
    s.spiritReadyMs = now + 120000;
    AddEffect(player, s, kFxSpirit, kSpiritMs);
    player->SetDisplayId(kSpiritDisplay);
    AddEffect(player, s, kFxDemorph, kSpiritMs);
    ChatHandler(player).SendSysMessage("|cffffffffSpirit of Redemption: 12 seconds of light before you fall.|r");
    return true;
}

void FusionClasses::OnWentDown(Player* player)
{
    if (!m_enabled || !player->HasFusion())
        return;
    FusionClassState& s = StateOf(player);
    uint32 const now = Now();
    bool const reincarnation = player->GetClass() == kShaman && int32(now - s.reincarnationReadyMs) >= 0;
    bool const stone = s.soulstoneBy && int32(s.soulstoneUntilMs - now) > 0;
    Rescue const r = RescueOnDown(false, reincarnation, stone);
    if (r == Rescue::None)
        return;
    if (r == Rescue::Reincarnation)
        s.reincarnationReadyMs = now + kReincarnationCooldownMs;
    s.rescue = uint8(r);
    s.rescueAtMs = now + kRescueDelayMs;
}

bool FusionClasses::KeepsOldTrap(Unit* caster, GameObject* old)
{
    Player* p = caster ? caster->ToPlayer() : nullptr;
    if (!m_enabled || !p || p->GetClass() != kHunter || !old || !p->HasFusion())
        return false;
    // The Trapper's traps stand side by side (the count is kept when the next is laid).
    std::vector<GameObject*> const traps = TrapsOf(p);
    return std::find(traps.begin(), traps.end(), old) != traps.end();
}

void FusionClasses::OnTrapFired(GameObject* trap, Unit* owner, Unit* target)
{
    static thread_local bool chaining = false;
    Player* p = owner ? owner->ToPlayer() : nullptr;
    if (!m_enabled || !p || !trap || !target || p->GetClass() != kHunter || !p->HasFusion())
        return;
    FusionClassState& s = StateOf(p);
    uint32 const now = Now();
    // A bear trap also bleeds (3 %EH a second, 6 s) and holds the sprint 10 s.
    if (trap->GetEntry() == kBearTrapObject || (OptionOfSpell(kHunter, trap->GetSpellId())
        && OptionOfSpell(kHunter, trap->GetSpellId())->id == OptionId(kHunter, 4, 1)))
    {
        AddDot(s.dots, target->GetObjectGuid().GetRawValue(), OptionId(kHunter, 4, 1), now, 6000,
            3.f * Fusion::ClassTuning::DamageScale(OptionId(kHunter, 4, 1)), 1);
        if (Player* t = target->ToPlayer(); t && t->HasFusion())
            AddEffect(t, StateOf(t), kFxNoSprint, 10000, p->GetObjectGuid().GetRawValue());
    }
    // The snap (fusion/ClassTuning.h): a trap's jaws stun a player a moment whatever the trap,
    // the Trapper's way to stop a gun (a freeze alone breaks on the first hit).
    if (target->IsPlayer() && !chaining)
        TimedAura(target, kAuraStun, Fusion::ClassTuning::kTrapSnapMs, p);
    // Prepared Ground: every other trap near it goes off on the enemy nearest it.
    if (chaining)
        return;
    chaining = true;
    float const reach = TrapChainYards(HasEffect(s, OptionId(kHunter, 6, 1), now));
    for (GameObject* other : TrapsOf(p))
    {
        if (other == trap || other->GetDistance(trap) > reach)
            continue;
        GameObjectInfo const* info = other->GetGOInfo();
        Unit* victim = target;
        float best = 8.f;
        for (Unit* u : Enemies(p, other, 8.f))
            if (other->GetDistance(u) < best)
            {
                best = other->GetDistance(u);
                victim = u;
            }
        p->CastSpell(victim, info->trap.spellId, true, nullptr, nullptr, other->GetObjectGuid());
        other->AddUse();
        OnTrapFired(other, p, victim);
        if (info->GetCharges() && other->GetUseCount() >= info->GetCharges())
            other->SetLootState(GO_JUST_DEACTIVATED);
    }
    chaining = false;
}

bool FusionClasses::GroundingCatch(Unit* thrower, uint32 mapId, Fusion::Vec3& at)
{
    if (!m_enabled || !thrower)
        return false;
    uint32 const now = Now();
    std::lock_guard<std::mutex> lock(m_worldLock);
    auto it = m_grounders.find(mapId);
    if (it == m_grounders.end())
        return false;
    std::vector<Grounder>& list = it->second;
    list.erase(std::remove_if(list.begin(), list.end(), [now](Grounder const& g) { return int32(g.untilMs - now) <= 0; }),
        list.end());
    for (Grounder& g : list)
    {
        if (g.used || (g.at - at).Length() > 10.f)
            continue;
        Unit* owner = ObjectAccessor::GetUnit(*thrower, ObjectGuid(g.owner));
        if (!owner || !owner->IsHostileTo(thrower))
            continue;
        g.used = true;
        at = g.at;
        return true;
    }
    return false;
}

bool FusionClasses::UseClassItem(Player* player, Item* item)
{
    if (!m_enabled || !item || item->GetEntry() != kArcaneStim || !player->HasFusion())
        return false;
    if (!player->IsAlive())
        return true;
    // A mage's Arcane Stim: 25% of one's health back and 20% faster for 4 s.
    FusionClassState& s = StateOf(player);
    HealPct(player, player, 25.f, nullptr);
    AddEffect(player, s, kFxSpeed, 4000, uint64(kArcaneStim), 1.2f);
    player->DestroyItemCount(kArcaneStim, 1, true);
    return true;
}

// ── Zones ──

void FusionClasses::TickZones(Player* player, FusionClassState& s, uint32 now)
{
    for (auto it = s.zones.begin(); it != s.zones.end();)
    {
        FusionClassZone& z = *it;
        if (int32(z.untilMs - now) <= 0 || z.mapId != player->GetMapId())
        {
            // A totem's zone ends: the totem goes with it (it would stand its vanilla time).
            TotemSlot slot = TotemSlot(TOTEM_SLOT_NONE);
            uint32 spellOf = 0;
            switch (z.code)
            {
                case OptionId(kShaman, 1, 2): slot = TOTEM_SLOT_WATER; spellOf = 10463; break;
                case OptionId(kShaman, 2, 2): slot = TOTEM_SLOT_EARTH; spellOf = 2484; break;
                case OptionId(kShaman, 5, 2): slot = TOTEM_SLOT_AIR; spellOf = 15112; break;
                case OptionId(kShaman, 3, 2): slot = TOTEM_SLOT_AIR; spellOf = 8177; break;
                case OptionId(kShaman, 6, 2): slot = TOTEM_SLOT_WATER; spellOf = 17359; break;
                default: break;
            }
            if (spellOf && z.variant != 0xFF)
                if (Totem* t = player->GetTotem(slot))
                    if (sSpellMgr.GetFirstSpellInChain(t->GetUInt32Value(UNIT_CREATED_BY_SPELL))
                        == sSpellMgr.GetFirstSpellInChain(spellOf))
                        t->UnSummon();
            it = s.zones.erase(it);
            continue;
        }
        ++it;
        if (int32(now - z.startMs) < 0)
            continue;
        uint32 const step = z.code == OptionId(kMage, 3, 2) ? 250 : 1000;
        if (z.nextTickMs && int32(now - z.nextTickMs) < 0)
            continue;
        bool const first = z.nextTickMs == 0;
        z.nextTickMs = now + step;
        uint64 const src = player->GetObjectGuid().GetRawValue() ^ (uint64(z.code) << 48) ^ z.variant;
        switch (z.code)
        {
            case OptionId(kWarrior, 1, 2):      // a banner: Rally, Ward or Pursuit
                for (Player* m : Party(player, z.radius, z.at))
                {
                    FusionClassState& ms = StateOf(m);
                    if (z.variant == 1)
                        AddEffect(m, ms, kFxBulletCut, 1500, src, 0.85f);
                    else if (z.variant == 2)
                        AddEffect(m, ms, kFxSpeed, 1500, src, 1.15f);
                    else
                        AddEffect(m, ms, kFxRegen, 1500, src, 1.5f);
                }
                break;
            case OptionId(kPaladin, 1, 1):      // Aegis: a short Consecration
            case OptionId(kPaladin, 2, 1):      // Consecration: enemies inside slowed 30%
                for (Unit* u : EnemiesAt(player, z.at, z.radius))
                {
                    if (z.code == OptionId(kPaladin, 1, 1))
                        Strike(player, u, 2.f);
                    Slow(u, player, 30.f, 1500);
                }
                if (InKitOf(player, s, OptionId(kPaladin, 5, 1)))     // Sacred Ground
                    for (Player* m : Party(player, z.radius, z.at))
                        AddEffect(m, StateOf(m), kFxBulletCut, 1500, src, 0.8f);
                break;
            case OptionId(kHunter, 2, 0):       // Flare: stealth and Camouflage show, all spotted
            {
                std::vector<Unit*> lit = EnemiesAt(player, z.at, z.radius);
                for (Unit* u : lit)
                    if (u->HasAuraType(SPELL_AURA_MOD_STEALTH))
                        u->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
                if (first)
                    Spot(player, lit, 2000);
                else
                    Spot(player, lit, 1500);
                break;
            }
            case OptionId(kPriest, 4, 1):       // Lightwell: an ally below 35% near it is healed
                for (Player* m : Party(player, z.radius, z.at))
                {
                    if (!z.charges)
                        break;
                    if (m->GetHealth() * 100 < m->GetMaxHealth() * 35 && !EffectOf(StateOf(m), kFxRegen, now, src))
                    {
                        AddEffect(m, StateOf(m), kFxRegen, 6000, src, 5.f);     // 30% over 6 s
                        --z.charges;
                    }
                }
                if (!z.charges)
                    z.untilMs = now;
                break;
            case OptionId(kShaman, 1, 2):       // Healing Stream Totem: 2% a second
            case OptionId(kShaman, 2, 2):       // Earthbind Totem: no sprint
            case OptionId(kShaman, 5, 2):       // Windwall Totem: 30% fewer bullets hurt
            {
                // Totemic Web: +50% linked to another within 20 yd; the Ancestral Circle doubles it.
                bool linked = false, circle = false;
                for (FusionClassZone const& o : s.zones)
                {
                    if (int32(o.untilMs - now) <= 0)
                        continue;
                    if (o.code == OptionId(kShaman, 6, 2))
                        circle = true;
                    else if (&o != &z && (o.code == OptionId(kShaman, 1, 2) || o.code == OptionId(kShaman, 2, 2)
                        || o.code == OptionId(kShaman, 5, 2)) && (o.at - z.at).Length() <= kTotemLinkYards)
                        linked = true;
                }
                float const mult = TotemMultiplier(linked, circle);
                if (z.code == OptionId(kShaman, 1, 2))
                    for (Player* m : Party(player, z.radius, z.at))
                        AddEffect(m, StateOf(m), kFxRegen, 1500, src, 2.f * mult);
                else if (z.code == OptionId(kShaman, 5, 2))
                    for (Player* m : Party(player, z.radius, z.at))
                        AddEffect(m, StateOf(m), kFxBulletCut, 1500, src, std::max(0.2f, 1.f - 0.3f * mult));
                else
                    for (Unit* u : EnemiesAt(player, z.at, z.radius))
                        if (Player* e = u->ToPlayer(); e && e->HasFusion())
                            AddEffect(e, StateOf(e), kFxNoSprint, 1500, src);
                break;
            }
            case OptionId(kShaman, 5, 0):       // Overcharge's static field: 40% slower
                for (Unit* u : EnemiesAt(player, z.at, z.radius))
                    Slow(u, player, 40.f, 1500);
                break;
            case OptionId(kPaladin, 6, 1):      // Bastion of Light: enemies inside are slowed
                for (Unit* u : EnemiesAt(player, z.at, z.radius))
                    TimedAura(u, kAuraDazed, 1500, player);
                break;
            case OptionId(kPriest, 6, 2):       // Sanctum: allies inside take half of the rest
                for (Player* m : Party(player, z.radius, z.at))
                    AddEffect(m, StateOf(m), kFxOtherCut, 1500, src, 0.5f);
                break;
            case OptionId(kShaman, 6, 0):       // Earthquake
            {
                bool const knock = (now / 1000) % 3 == 0;
                for (Unit* u : EnemiesAt(player, z.at, z.radius))
                {
                    Strike(player, u, 2.f);
                    if (knock)
                        TimedAura(u, kAuraStun, 1000, player);
                }
                break;
            }
            case OptionId(kShaman, 6, 2):       // Ancestral Circle (it also doubles the totems above)
                for (Player* m : Party(player, z.radius, z.at))
                    AddEffect(m, StateOf(m), kFxRegen, 1500, src, 2.f);
                break;
            case OptionId(kMage, 2, 2):         // Healing Glyph
                for (Player* m : Party(player, z.radius, z.at))
                    AddEffect(m, StateOf(m), kFxRegen, 1500, src, 2.f);
                break;
            case OptionId(kMage, 3, 2):         // Blink Pads: a step on one, out at the other
            {
                FusionClassZone const* other = nullptr;
                for (FusionClassZone const& o : s.zones)
                    if (o.code == z.code && o.variant != z.variant)
                        other = &o;
                if (!other)
                    break;
                for (Player* m : Party(player, 1.5f, z.at))
                {
                    uint64 const g = m->GetObjectGuid().GetRawValue();
                    bool waiting = false;
                    for (auto const& w : z.waits)
                        if (w.first == g && int32(w.second - now) > 0)
                            waiting = true;
                    if (waiting)
                        continue;
                    const_cast<FusionClassZone*>(other)->waits.push_back({g, now + 2000});
                    z.waits.push_back({g, now + 2000});
                    m->NearTeleportTo(other->at.x, other->at.y, other->at.z + 0.5f, m->GetOrientation());
                }
                break;
            }
            case OptionId(kMage, 6, 0):         // Meteor: lands, then the ground burns
                if (first)
                {
                    std::vector<Unit*> hit = EnemiesAt(player, z.at, z.radius);
                    float const share = hit.empty() ? 0.f : 60.f / float(hit.size());
                    for (Unit* u : hit)
                        Strike(player, u, std::max(share, 10.f));
                }
                else
                    for (Unit* u : EnemiesAt(player, z.at, z.radius))
                        Strike(player, u, 3.f);
                break;
            default:
                break;
        }
    }
}

// ── Minions ──

Creature* FusionClasses::RaiseMinion(Player* player, FusionClassState& s, uint16 code, uint32 entry, Fusion::Vec3 at,
    uint32 ms, float healthShare)
{
    Creature* c = player->SummonCreature(entry, at.x, at.y, at.z, player->GetOrientation(),
        TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, ms);
    if (!c)
        return nullptr;
    c->SetFactionTemplateId(player->GetFactionTemplateId());
    c->SetLevel(player->GetLevel());
    uint32 const health = std::max<uint32>(1, uint32(player->GetMaxHealth() * healthShare));
    c->SetMaxHealth(health);
    c->SetHealth(health);
    c->SetOwnerGuid(player->GetObjectGuid());
    c->GetMotionMaster()->MoveFollow(player, 2.f, kPi / 2.f + frand(-1.f, 1.f));
    s.minions.push_back({c->GetObjectGuid().GetRawValue(), code, at});
    return c;
}

void FusionClasses::TickMinions(Player* player, FusionClassState& s, uint32 now)
{
    if (s.minions.empty())
        return;
    bool const second = (now / 1000) != ((now - 250) / 1000);
    Map* map = player->GetMap();
    Unit* target = player->GetVictim();
    if (!Hostile(player, target))
        target = Selected(player);
    for (auto it = s.minions.begin(); it != s.minions.end();)
    {
        Creature* c = map->GetCreature(ObjectGuid(it->guid));
        if (!c || !c->IsAlive())
        {
            // A minion fell: Life Force, and Death's Harvest's blast where it fell.
            bool const necro = it->code >= OptionId(kWarlock, 1, 2) && it->code <= OptionId(kWarlock, 6, 2);
            if (necro)
            {
                Gain(player, s, Resource::LifeForce, 10);
                if (InKitOf(player, s, OptionId(kWarlock, 5, 2)))
                    for (Unit* u : EnemiesAt(player, it->last, 6.f))
                        Strike(player, u, 10.f);
            }
            if (c)
                c->DespawnOrUnsummon();
            it = s.minions.erase(it);
            continue;
        }
        it->last = Pos(c);
        // Fighters join the warlock's fight (decoys and familiars do not).
        bool const fighter = it->code == OptionId(kWarlock, 1, 2) || it->code == OptionId(kWarlock, 4, 2)
            || it->code == OptionId(kWarlock, 6, 2);
        if (fighter && Hostile(player, target) && c->GetVictim() != target && c->AI())
            c->AI()->AttackStart(target);
        bool const ghoul = it->code == OptionId(kWarlock, 4, 2);
        if (fighter && Hostile(player, target) && int32(now - it->nextActMs) >= 0
            && !c->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
        {
            float const dist = c->GetDistance(target);
            if (ghoul && dist > 5.f && dist <= 20.f)
            {
                // A ghoul leaps 20 yd onto the warlock's target.
                c->GetMotionMaster()->MoveJump(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                    25.f, 5.f);
                it->nextActMs = now + 8000;
            }
            else if (it->code == OptionId(kWarlock, 6, 2) && dist > 6.f && dist <= 30.f && c->IsWithinLOSInMap(target))
            {
                // The abomination's Meat Hook: the enemy is dragged to it, stunned a second.
                float const toward = c->GetAngle(target);
                Fusion::Vec3 at{c->GetPositionX() + std::cos(toward) * 3.f, c->GetPositionY() + std::sin(toward) * 3.f,
                    c->GetPositionZ()};
                c->UpdateAllowedPositionZ(at.x, at.y, at.z);
                PullTo(target, at, toward + kPi);
                TimedAura(target, kAuraStun, 1000, player);
                it->nextActMs = now + 12000;
            }
        }
        // A ghoul's claws: 5 %EH a second at arm's length.
        if (ghoul && second && Hostile(player, target) && c->IsWithinDistInMap(target, 5.f))
            Strike(player, target, 5.f);
        ++it;
    }
}

// ── The world's shapes ──

void FusionClasses::AddBlocker(uint32 mapId, Blocker const& b)
{
    std::lock_guard<std::mutex> lock(m_worldLock);
    std::vector<Blocker>& list = m_blockers[mapId];
    uint32 const now = Now();
    list.erase(std::remove_if(list.begin(), list.end(), [now](Blocker const& x) { return int32(x.untilMs - now) <= 0; }),
        list.end());
    list.push_back(b);
}

void FusionClasses::AddSmoke(Player* owner, Fusion::Vec3 at, float radius, uint32 ms)
{
    uint32 const now = Now();
    {
        std::lock_guard<std::mutex> lock(m_worldLock);
        std::vector<Smoke>& list = m_smokes[owner->GetMapId()];
        list.erase(std::remove_if(list.begin(), list.end(), [now](Smoke const& x) { return int32(x.untilMs - now) <= 0; }),
            list.end());
        list.push_back({at, radius, now + ms});
    }
    // Everyone near sees the cloud (the client draws it), and spotting outlines inside it end.
    std::vector<uint8> const body = EncodeSmoke({at, radius, 0}, uint16(std::min<uint32>(ms, 65535)));
    Map::PlayerList const& players = owner->GetMap()->GetPlayers();
    for (auto itr = players.begin(); itr != players.end(); ++itr)
        if (Player* p = itr->getSource(); p && p->HasFusion() && (Pos(p) - at).Length() <= 150.f)
            sFusionMgr.SendChannel(p, kChannelClasses, body);
}

bool FusionClasses::SightBlocked(uint32 mapId, Fusion::Vec3 a, Fusion::Vec3 b) const
{
    if (!m_enabled)
        return false;
    std::lock_guard<std::mutex> lock(m_worldLock);
    auto it = m_smokes.find(mapId);
    if (it == m_smokes.end())
        return false;
    uint32 const now = Now();
    for (Smoke const& s : it->second)
        if (int32(s.untilMs - now) > 0 && Fusion::Classes::SightBlocked(s, a, b))
            return true;
    return false;
}

void FusionClasses::Spot(Player* owner, std::vector<Unit*> const& units, uint32 ms)
{
    std::vector<Fusion::Classes::Spot> spots;
    for (Unit* u : units)
        if (u && !SightBlocked(owner->GetMapId(), Pos(owner), Pos(u)))
            spots.push_back({u->GetObjectGuid().GetRawValue(), uint16(std::min<uint32>(ms, 65535))});
    if (spots.empty())
        return;
    std::vector<uint8> const body = EncodeSpotted(spots);
    for (Player* m : Party(owner, 200.f))
        sFusionMgr.SendChannel(m, kChannelClasses, body);
}

// ── The damage hooks ──

bool FusionClasses::StopsRound(Unit* from, Unit* to, uint32 damage)
{
    if (!m_enabled || !from || !to)
        return false;
    uint32 const now = Now();
    Fusion::Vec3 const shooter = Pos(from) + Fusion::Vec3{0.f, 0.f, 1.6f};
    Fusion::Vec3 const victim = Pos(to) + Fusion::Vec3{0.f, 0.f, 1.2f};
    // The victim's own: a raised Bulwark, a dodge.
    if (Player* v = to->ToPlayer(); v && v->HasFusion() && v->GetFusion().classFramework)
    {
        FusionClassState& s = StateOf(v);
        if (HasEffect(s, OptionId(kWarrior, 1, 1), now)
            && InArc(Pos(v), v->GetOrientation(), 50.f * kPi / 180.f, Pos(from)))
        {
            v->ModifyPower(POWER_RAGE, 50);     // 5 rage a blocked round
            return true;
        }
        for (FusionClassEffect const& e : s.effects)
            if (e.code == kFxBulletMiss && int32(e.untilMs - now) > 0 && frand(0.f, 1.f) < e.value)
                return true;
        if (HasEffect(s, kFxEclipse, now) && InKit(kDruid, s.picks, OptionId(kDruid, 5, 2)) && frand(0.f, 1.f) < 0.3f)
            return true;
    }
    // The world's walls and domes, raised by the victim's side.
    Player* wornOwner = nullptr;
    bool broke = false;
    {
        std::lock_guard<std::mutex> lock(m_worldLock);
        auto it = m_blockers.find(to->GetMapId());
        if (it == m_blockers.end())
            return false;
        bool stopped = false;
        for (Blocker& b : it->second)
        {
            if (int32(b.untilMs - now) <= 0 || b.broken)
                continue;
            Unit* owner = ObjectAccessor::GetUnit(*to, ObjectGuid(b.owner));
            if (owner && (owner == from || owner->IsFriendlyTo(from)))
                continue;
            Blocker shape = b;
            if (b.followsOwner)
            {
                if (!owner)
                    continue;
                shape.center = Pos(owner);
            }
            if (!Blocks(shape, shooter, victim))
                continue;
            // A Barrier or a Warding Rune wears down by what it stops, and breaks.
            broke = Wear(b, float(damage));
            wornOwner = owner ? owner->ToPlayer() : nullptr;
            stopped = true;
            break;
        }
        if (!stopped)
            return false;
    }
    // Told outside the lock (it sends).
    if (wornOwner && broke)
        ChatHandler(wornOwner).SendSysMessage("|cffffe080Your screen breaks.|r");
    return true;
}

uint32 FusionClasses::AdjustDamage(Unit* attacker, Unit* victim, uint32 damage, SpellEntry const* spell)
{
    if (!m_enabled || !damage || !attacker || !victim || attacker == victim)
        return damage;
    FusionHitSource const& hit = FusionMgr::CurrentHit();
    bool const bullet = hit.weaponId && !hit.melee && !hit.explosive;
    if (Player* a = attacker->ToPlayer(); a && a->HasFusion() && a->GetFusion().classFramework)
    {
        // A basic spell known below its first rank's level (Exorcism's is 20, every paladin has
        // it from 1) hits as hard as the level allows, not as its rank's.
        if (spell && spell->spellLevel > a->GetLevel())
            if (uint32 const basic = BasicSpellOf(a->GetClass());
                basic && sSpellMgr.GetFirstSpellInChain(spell->Id) == sSpellMgr.GetFirstSpellInChain(basic))
                damage = std::max<uint32>(1, uint32(float(damage) * std::max(0.25f, float(a->GetLevel()) / float(spell->spellLevel))));
        damage = AdjustDealt(a, victim, StateOf(a), damage, bullet, spell);
        // A dual cast from both hands hits harder (FusionSpellcasting.cpp).
        if (float const dual = DualPower(StateOf(a), spell, Now()); dual > 1.f)
            damage = uint32(float(damage) * dual + 0.5f);
        // A spell from range trades with guns (fusion/ClassEffects.h RangedSpellScale), and with
        // a creature harder again (fusion/ClassTuning.h kCreatureSpellPower).
        if (spell && !bullet)
            if (float k = Fusion::Classes::RangedSpellScale(
                    Spells::GetSpellMaxRange(sSpellRangeStore.LookupEntry(spell->rangeIndex)), m_rangedSpellPower); k != 1.f)
            {
                if (!victim->IsPlayer() && !victim->GetCharmerOrOwnerGuid().IsPlayer())
                    k *= Fusion::ClassTuning::kCreatureSpellPower;
                damage = std::max<uint32>(1, uint32(float(damage) * k + 0.5f));
            }
        // A bolt's splash on an enemy it did not hit: its share, by how near (FusionSpellcasting.cpp).
        if (float const share = SplashShareOf(StateOf(a), victim, spell, Now()); share < 1.f)
            damage = std::max<uint32>(1, uint32(float(damage) * share + 0.5f));
        // The class identity pass (fusion/ClassTuning.h): the option's and the basic spell's own
        // scale, the Wizard's Shatter, a healer's Atonement.
        if (spell && !bullet)
            damage = IdentityDamage(a, victim, damage, spell);
    }
    // A creature demoralized by a warlord deals less (a player's own mark is its state's).
    else if (attacker->IsCreature())
    {
        // The hunter's bond: a hunter's companion bites harder on its Mark Prey (fusion/ClassTuning.h).
        if (Player* owner = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
            owner && owner != attacker && Fusion::ClassTuning::HunterBond(owner->GetClass()) && owner->HasFusion()
            && owner->GetFusion().classFramework)
        {
            std::vector<uint64> const& prey = StateOf(owner).prey;
            if (std::find(prey.begin(), prey.end(), victim->GetObjectGuid().GetRawValue()) != prey.end())
                damage = std::max<uint32>(1, uint32(float(damage) * Fusion::ClassTuning::kBondPreyBonus + 0.5f));
        }
        if (float const cut = UnitMark(attacker, OptionId(kWarrior, 3, 2)); cut > 0.f)
            damage = std::max<uint32>(1, uint32(float(damage) * cut + 0.5f));
    }
    // A blow: a swing (a player's, FusionMgr::Strike, or a creature's melee), no spell, no blast.
    bool const blow = !bullet && !hit.explosive && (hit.melee || (!spell && !hit.weaponId));
    if (Player* v = victim->ToPlayer(); v && v->HasFusion() && v->GetFusion().classFramework && damage)
        damage = AdjustTaken(attacker, v, StateOf(v), damage, bullet, blow);
    return damage;
}

uint32 FusionClasses::IdentityDamage(Player* attacker, Unit* victim, uint32 damage, SpellEntry const* spell)
{
    namespace CT = Fusion::ClassTuning;
    FusionClassState& s = StateOf(attacker);
    uint8 const cls = attacker->GetClass();
    int const lead = CT::LeadArchetype(cls, s.picks);
    float k = 1.f;
    if (Option const* o = DamageOptionOf(cls, spell->Id))
        k *= CT::DamageScale(o->id);
    uint32 const basic = BasicSpellOf(cls);
    bool const basicSpell = basic && sSpellMgr.GetFirstSpellInChain(spell->Id) == sSpellMgr.GetFirstSpellInChain(basic);
    if (basicSpell)
        k *= CT::BasicSpellScale(cls, lead);
    // Shatter: a Wizard's spells into a chilled, rooted or stunned target.
    if (cls == kMage && lead == 0)
        k *= CT::ShatterMultiplier(true, victim->HasAuraType(SPELL_AURA_MOD_ROOT)
            || victim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) || victim->HasUnitState(UNIT_STATE_STUNNED));
    if (k != 1.f)
        damage = std::max<uint32>(1, uint32(float(damage) * k + 0.5f));
    // Atonement: a healer's basic spell heals the healer for a share of what it dealt (as a share
    // of the victim's pool, so a CoD-scale pool and a WoW one count alike).
    if (basicSpell && CT::IsHealerArchetype(cls, lead) && victim->GetMaxHealth() && !sFusionMgr.IsDowned(attacker))
        if (float const back = CT::AtonementHeal(100.f * float(std::min(damage, victim->GetHealth())) / float(victim->GetMaxHealth()), true);
            back > 0.f)
            HealPct(attacker, attacker, back, spell);
    return damage;
}

uint32 FusionClasses::AdjustDealt(Player* attacker, Unit* victim, FusionClassState& s, uint32 damage, bool bullet,
    SpellEntry const* spell)
{
    uint32 const now = Now();
    float mult = 1.f;
    uint32 add = 0;
    bool const melee = !bullet && attacker->IsWithinDistInMap(victim, 6.f);
    // A blow of one's own: the hands' swing or a melee ability (not a class effect's damage, a
    // tick or a spell that happens to land close).
    bool const blow = melee && (FusionMgr::CurrentHit().melee || (spell && spell->DmgClass == SPELL_DAMAGE_CLASS_MELEE));
    if (!bullet && spell)
    {
        // Spellweave's payoff, Combustion, Arcane Edge: the spell they were set for.
        if (FusionClassEffect* w = EffectOf(s, kFxWeave, now))
        {
            mult *= w->value;
            w->untilMs = now;
        }
    }
    if (blow)
    {
        // Death Wish: +30% in melee (vanilla's own +20% of physical, and the rest).
        if (HasEffect(s, OptionId(kWarrior, 5, 0), now))
            mult *= 1.3f / 1.2f;
        // Cat Form's momentum: the next blow crits.
        if (FusionClassEffect* c = EffectOf(s, kFxMomentumCrit, now))
        {
            mult *= 2.f;
            c->untilMs = now;
        }
        // Duelist: each point of Momentum adds to the blow, and the blow adds a point.
        if (Meter* m = MeterOf(s, Resource::Momentum))
        {
            mult *= MomentumMultiplier(m->value);
            if (m->Add(1))
                s.metersDirty = true;
            s.lastBlowMs = now;
        }
        // Its procs (poisons, burns, waves, smashes, cleaves) land after the blow.
        if (!t_inProcs)
            s.pendingMelee.push_back({victim->GetObjectGuid().GetRawValue(), damage});
    }
    if (!bullet)
    {
        if (HasEffect(s, kFxOpening, now))
            mult *= 2.f;
        // Bane: the marked kind takes more (Witchfire: every kind).
        bool const witchfire = HasEffect(s, OptionId(kHunter, 6, 2), now);
        if (FusionClassEffect* bane = EffectOf(s, OptionId(kHunter, 1, 2), now))
        {
            bool const casterMark = bane->value < 0.f;
            bool const caster = victim->IsPlayer() && victim->GetPowerType() == POWER_MANA;
            if (witchfire || (casterMark ? caster : victim->GetCreatureType() == uint32(bane->value)))
                mult *= 1.3f;
        }
        else if (witchfire)
            mult *= 1.3f;
        if (witchfire && victim->GetHealth() < victim->GetMaxHealth() / 4)
            return victim->GetHealth();
        if (melee)
        {
            if (HasEffect(s, OptionId(kShaman, 1, 1), now))         // Stone Fists
                add += Eh(attacker, 8.f);
            if (HasEffect(s, OptionId(kMage, 1, 1), now))           // Flame Blade
            {
                add += Eh(attacker, 12.f);
                // Arcane Edge: a blade's blow may ready the next spell.
                if (InKit(kMage, s.picks, OptionId(kMage, 5, 1)) && frand(0.f, 1.f) < kArcaneEdgeChance)
                    AddEffect(attacker, s, kFxArcaneEdge, kArcaneEdgeMs);
            }
            // Starcaller: a blow may bring Eclipse.
            if (PicksArchetype(s, kDruid, 2) && frand(0.f, 1.f) < EclipseChance(0, true))
                Eclipse(attacker, s);
            if (attacker->GetShapeshiftForm() == ShapeshiftForm(Fusion::DruidForms::kWorgen))   // Worgen: lifesteal
                HealPct(attacker, attacker, 100.f * 0.3f * float(damage) / std::max<uint32>(1, attacker->GetMaxHealth()), nullptr);
        }
    }
    if (FusionClassEffect* d = EffectOf(s, kFxDemoralized, now))
        mult *= d->value;
    if (FusionClassEffect* mark = EffectOf(s, kFxDrawFire, now))
        if (mark->source != victim->GetObjectGuid().GetRawValue())
            mult *= 0.7f;
    uint32 const out = uint32(float(damage) * mult + 0.5f) + add;
    if (s.deathmark && s.deathmark == victim->GetObjectGuid().GetRawValue())
        s.deathmarkDamage += float(out);
    // Vampiric Embrace: a quarter of the priest's shadow damage heals the party.
    if (!bullet && spell && spell->School == SPELL_SCHOOL_SHADOW && out && HasEffect(s, kFxVampiric, now))
        for (Player* m : Party(attacker, 40.f))
            if (m->GetHealth() < m->GetMaxHealth())
                attacker->DealHeal(m, std::max<uint32>(1, out / 4), spell);
    return out;
}

void FusionClasses::RunMeleeProcs(Player* player, FusionClassState& s)
{
    std::vector<std::pair<uint64, uint32>> blows;
    blows.swap(s.pendingMelee);
    if (!player->IsAlive())
        return;
    for (auto const& [guid, damage] : blows)
        if (Unit* v = ObjectAccessor::GetUnit(*player, ObjectGuid(guid)))
            MeleeProcs(player, v, s, damage);
}

void FusionClasses::MeleeProcs(Player* attacker, Unit* victim, FusionClassState& s, uint32 damage)
{
    uint32 const now = Now();
    t_inProcs = true;
    uint64 const vg = victim->GetObjectGuid().GetRawValue();
    bool const paced = int32(now - s.nextProcMs) >= 0;     // the heavy procs, at most so often
    bool proc = false;
    // Witchfire: the blade burns, 3 %EH a second for 4 s.
    if (HasEffect(s, OptionId(kHunter, 6, 2), now))
        AddDot(s.dots, vg, OptionId(kHunter, 6, 2), now, 4000, 3.f, 1);
    // Deadly Poison: a poison stack (up to 3), 1.5 %EH a second each, 12 s.
    if (HasEffect(s, OptionId(kRogue, 1, 1), now))
        AddDot(s.dots, vg, OptionId(kRogue, 1, 1), now, 12000, 1.5f, 3);
    // The Poison Satchel's coat: its vanilla debuff on every blow.
    if (HasEffect(s, OptionId(kRogue, 3, 1), now) && victim->IsAlive())
        if (uint32 const debuff = PoisonDebuff(Poison(s.poison), uint8(std::min<uint32>(attacker->GetLevel(), 255))))
            attacker->CastSpell(victim, debuff, true);
    // Blade Flurry: the blow also strikes the nearest other enemy within 8 yd, for half.
    if (HasEffect(s, OptionId(kRogue, 4, 2), now) && damage)
        for (Unit* u : NearestEnemies(attacker, victim, 8.f, 1, victim))
            sFusionMgr.Hit(attacker, u, std::max<uint32>(1, uint32(float(damage) * kBladeFlurryShare)));
    // Mana Shield: each blow restores 5% of the pool (up to its 25%).
    if (FusionClassEffect* shield = EffectOf(s, kFxAbsorb, now, attacker->GetObjectGuid().GetRawValue() ^ OptionId(kMage, 4, 1)))
        shield->value = std::min(shield->value + attacker->GetMaxHealth() * 0.05f, attacker->GetMaxHealth() * 0.25f);
    // Stone Fists: the blow throws the target back 3 yd.
    if (HasEffect(s, OptionId(kShaman, 1, 1), now) && victim->IsAlive())
        Shove(victim, attacker->GetAngle(victim), 3.f, 3.f);
    if (paced && victim->IsAlive())
    {
        // Arcane Avatar: an arcane wave 15 yd ahead, 10 %EH to every enemy in it.
        if (HasEffect(s, OptionId(kMage, 6, 1), now))
        {
            for (Unit* u : Enemies(attacker, attacker, 15.f))
                if (InArc(Pos(attacker), attacker->GetOrientation(), 20.f * kPi / 180.f, Pos(u)))
                    Strike(attacker, u, 10.f);
            proc = true;
        }
        // Avatar of Stone: every blow is a Ground Smash of one charge.
        if (HasEffect(s, OptionId(kShaman, 6, 1), now))
        {
            for (Unit* u : Enemies(attacker, victim, 10.f))
            {
                Strike(attacker, u, 5.f);
                TimedAura(u, kAuraStun, 2000, attacker);
            }
            proc = true;
        }
        // Demon Form: the claws cleave 15 %EH in front.
        if (HasEffect(s, OptionId(kWarlock, 6, 1), now))
        {
            for (Unit* u : Enemies(attacker, attacker, 6.f))
                if (InArc(Pos(attacker), attacker->GetOrientation(), kPi / 3.f, Pos(u)))
                    Strike(attacker, u, 15.f);
            proc = true;
        }
    }
    if (proc)
        s.nextProcMs = now + 1000;
    t_inProcs = false;
}

uint32 FusionClasses::AdjustTaken(Unit* attacker, Player* victim, FusionClassState& s, uint32 damage, bool bullet, bool blow)
{
    uint32 const now = Now();
    // A spirit of redemption cannot be harmed.
    if (HasEffect(s, kFxSpirit, now))
        return 0;
    float mult = 1.f;
    // Shadowform: 30% less of everything.
    if (victim->GetShapeshiftForm() == FORM_SHADOW)
        mult *= 0.7f;
    // A spirit wolf is hard to hit (fusion/ClassTuning.h): the shaman's way in.
    if (bullet && victim->GetShapeshiftForm() == FORM_GHOSTWOLF)
        mult *= Fusion::ClassTuning::kGhostWolfBulletCut;
    for (FusionClassEffect const& e : s.effects)
    {
        if (int32(e.untilMs - now) <= 0)
            continue;
        switch (e.code)
        {
            case kFxDamageCut: mult *= e.value; break;
            case kFxBulletCut: if (bullet) mult *= e.value; break;
            case kFxOtherCut: if (!bullet) mult *= e.value; break;
            case kFxFrenzy: if (bullet) mult *= 0.75f; break;
            case OptionId(kWarrior, 5, 0): if (bullet) mult *= 0.8f; break;     // Death Wish
            case OptionId(kPaladin, 6, 0): if (bullet) mult *= 0.8f; break;     // Avenging Crusade
            case OptionId(kWarlock, 1, 1): if (bullet) mult *= 0.8f; break;     // Fel Hide
            case OptionId(kMage, 6, 1): mult *= 0.75f; break;                   // Arcane Avatar
            case OptionId(kShaman, 6, 1): mult *= 0.7f; break;                  // Avatar of Stone
            case OptionId(kDruid, 6, 1): mult *= 0.8f; break;                   // Tree of Life
            default: break;
        }
    }
    // Riposte: a blow from in front is parried and struck back (not a bullet).
    if (attacker && !bullet && HasEffect(s, kFxRiposte, now)
        && Parries(true, victim->IsWithinDistInMap(attacker, 6.f), InArc(Pos(victim), victim->GetOrientation(), kPi / 2.f, Pos(attacker))))
    {
        s.ripostes.push_back(attacker->GetObjectGuid().GetRawValue());
        return 0;
    }
    // The shield raised (fusion/Melee.h Guard): bullets and blows from in front; raised just in
    // time, a parry that stops it all.
    if (attacker && attacker != victim && (bullet || blow))
    {
        Fusion::Melee::Block const b = Fusion::Melee::GuardBlocks(victim->GetFusion().guard,
            InArc(Pos(victim), victim->GetOrientation(), Fusion::Melee::kGuardHalfAngle, Pos(attacker)), now);
        if (b == Fusion::Melee::Block::Perfect)
            return 0;
        mult *= Fusion::Melee::BlockMultiplier(b, bullet);
    }
    // Relentless: at 5 Momentum or more, fewer bullets hurt.
    if (bullet && InKit(victim->GetClass(), s.picks, OptionId(kRogue, 5, 2)))
        if (Meter* m = MeterOf(s, Resource::Momentum); m && m->value >= kRelentlessFrom)
            mult *= kRelentlessBulletCut;
    // Stoneskin: fewer bullets hurt.
    if (bullet && InKit(victim->GetClass(), s.picks, OptionId(kShaman, 5, 1)))
        mult *= kStoneskinBulletCut;
    // Demonic Pact: less of everything hurts.
    if (InKit(victim->GetClass(), s.picks, OptionId(kWarlock, 5, 1)))
        mult *= kDemonicPactCut;
    float left = float(damage) * mult;
    // Blessing of Sacrifice: half of it goes to the paladin (whose own shields may take it).
    if (!t_inSacrifice && attacker && left > 0.f)
        if (FusionClassEffect* sac = EffectOf(s, kFxSacrifice, now))
            if (Player* pal = ObjectAccessor::FindPlayer(ObjectGuid(sac->source)); pal && pal != victim && pal->IsAlive()
                && pal->GetMap() == victim->GetMap() && pal->IsWithinDistInMap(victim, 40.f) && !sFusionMgr.IsDowned(pal))
                if (uint32 const part = uint32(left * kSacrificeShare))
                {
                    left -= float(part);
                    t_inSacrifice = true;
                    sFusionMgr.Hit(attacker, pal, part);
                    t_inSacrifice = false;
                }
    // Absorbs: the bullet-only, the spell-only, then any.
    for (FusionClassEffect& e : s.effects)
    {
        if (int32(e.untilMs - now) <= 0 || left <= 0.f)
            continue;
        bool const takes = e.code == kFxAbsorb || (e.code == kFxAbsorbBullet && bullet)
            || (e.code == kFxAbsorbSpell && !bullet);
        if (!takes)
            continue;
        // A ward soaks bullets half again better than its face (fusion/ClassEffects.h).
        left -= Fusion::Classes::WardAbsorb(e.value, left, bullet);
    }
    // An arcane familiar takes 30% of it.
    if (FusionClassEffect* f = EffectOf(s, kFxFamiliar, now))
        if (Creature* wisp = victim->GetMap()->GetCreature(ObjectGuid(f->source)); wisp && wisp->IsAlive())
        {
            uint32 const part = uint32(left * 0.3f);
            if (part >= wisp->GetHealth())
                wisp->DespawnOrUnsummon();
            else
                wisp->SetHealth(wisp->GetHealth() - part);
            left -= float(part);
        }
    uint32 out = uint32(std::max(0.f, left) + 0.5f);
    // Premonition: an Oracle near keeps an ally from going below 15% (once a minute each).
    uint32 const health = victim->GetHealth();
    bool const dropsLow = out && (out >= health || health - out < victim->GetMaxHealth() * 15 / 100);
    if (dropsLow)
        if (int32(now - s.premonitionReadyMs) >= 0)
            for (Player* m : Party(victim, 30.f))
            {
                FusionClassState* ms = m->GetFusion().classFramework.get();
                if (!ms || !InKit(m->GetClass(), ms->picks, OptionId(kPriest, 4, 2)))
                    continue;
                uint32 const floor = victim->GetMaxHealth() * 15 / 100;
                out = victim->GetHealth() > floor ? std::min(out, victim->GetHealth() - floor) : 0;
                AddEffect(victim, s, kFxAbsorb, 8000, m->GetObjectGuid().GetRawValue(), victim->GetMaxHealth() * 0.2f);
                s.premonitionReadyMs = now + 60000;
                break;
            }
    return out;
}

void FusionClasses::OnKill(Unit* killer, Unit* victim)
{
    if (!m_enabled || !killer || !victim)
        return;
    Player* player = killer->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player || !player->HasFusion() || !player->GetFusion().classFramework)
        return;
    FusionClassState& s = StateOf(player);
    uint32 const now = Now();
    uint8 const cls = player->GetClass();
    bool const melee = player->IsWithinDistInMap(victim, 6.f);
    uint64 const vg = victim->GetObjectGuid().GetRawValue();
    // Mark Prey: killing the prey resets every cooldown (Overwatch: each prey adds 5 s to it).
    if (auto p = std::find(s.prey.begin(), s.prey.end(), vg); p != s.prey.end())
    {
        s.prey.erase(p);
        player->RemoveAllCooldowns();
        if (FusionClassEffect* ow = EffectOf(s, OptionId(kHunter, 6, 0), now))
            ow->untilMs += 5000;
        ChatHandler(player).SendSysMessage("|cffffd200Prey down: your cooldowns are reset.|r");
    }
    // The afflictions the victim carried, and what its death does with them.
    std::vector<uint32> carried;
    for (FusionAffliction const& a : s.afflictions)
        if (a.target == vg && int32(a.untilMs - now) > 0)
            carried.push_back(a.spell);
    s.afflictions.erase(std::remove_if(s.afflictions.begin(), s.afflictions.end(),
        [&](FusionAffliction const& a) { return a.target == vg || int32(a.untilMs - now) <= 0; }), s.afflictions.end());
    if (!carried.empty())
    {
        auto isOf = [](uint32 spell, uint32 option) { return sSpellMgr.GetFirstSpellInChain(spell) == sSpellMgr.GetFirstSpellInChain(option); };
        bool const doomed = std::any_of(carried.begin(), carried.end(), [&](uint32 sp) { return isOf(sp, 603); });
        std::vector<uint32> spread;     // what goes on, and to how many
        size_t targets = 0;
        float reach = 0.f;
        if (cls == kWarlock && doomed)
        {
            // Curse of Doom: every affliction to all enemies within 15 yd.
            spread = carried;
            targets = 50;
            reach = 15.f;
        }
        else if (cls == kWarlock && InKit(cls, s.picks, OptionId(kWarlock, 5, 0)))
        {
            // Soul Siphon: every affliction to the two nearest within 15 yd.
            spread = carried;
            targets = 2;
            reach = 15.f;
        }
        else if (cls == kPriest)
        {
            // Devouring Plague jumps to an enemy within 10 yd; Shadowform spreads every DoT to two
            // and gives an orb.
            bool const shadow = player->GetShapeshiftForm() == FORM_SHADOW;
            for (uint32 sp : carried)
                if (shadow || isOf(sp, 19280))
                    spread.push_back(sp);
            targets = shadow ? 2 : 1;
            reach = 10.f;
        }
        // Doom itself does not go on (a new one is cast); a curse goes once.
        spread.erase(std::remove_if(spread.begin(), spread.end(), [&](uint32 sp) { return isOf(sp, 603); }), spread.end());
        if (!spread.empty())
            for (Unit* u : NearestEnemies(player, victim, reach, targets, victim))
                for (uint32 sp : spread)
                {
                    player->CastSpell(u, sp, true);
                    s.afflictions.push_back({u->GetObjectGuid().GetRawValue(), sp, now + 60000});
                }
    }
    if (s.deathmark && s.deathmark == victim->GetObjectGuid().GetRawValue())
    {
        s.deathmark = 0;
        TimedAura(player, kAuraInvisible, 3000, player);
        player->RemoveAllCooldowns();
    }
    switch (cls)
    {
        case kWarrior:
            // A Berserker's kill sends it into a Frenzy; with Blood Frenzy it also heals 10% and
            // readies Intercept.
            if (PicksArchetype(s, kWarrior, 0))
                Frenzy(player, s, kFrenzyMs);
            if (InKit(cls, s.picks, OptionId(kWarrior, 4, 0)))
            {
                HealPct(player, player, 10.f, nullptr);
                if (SpellEntry const* e = sSpellMgr.GetSpellEntry(RankFor(20617, uint8(player->GetLevel()))))
                    player->RemoveSpellCooldown(e, true);
            }
            if (melee && HasEffect(s, OptionId(kWarrior, 6, 0), now))
                HealPct(player, player, 30.f, nullptr);
            break;
        case kPaladin:
            if (melee && InKit(cls, s.picks, OptionId(kPaladin, 3, 0)))
                Gain(player, s, Resource::HolyPower, 2);
            break;
        case kHunter:
            if (HasEffect(s, OptionId(kHunter, 6, 2), now))
            {
                // Witchfire: a kill readies the Throwing Axes again.
                if (SpellEntry const* e = sSpellMgr.GetSpellEntry(RankFor(16075, uint8(player->GetLevel()))))
                    player->RemoveSpellCooldown(e, true);
                HealPct(player, player, 15.f, nullptr);
            }
            break;
        case kMage:
            // Arcane Edge: a kill in melee readies the next spell.
            if (melee && InKit(cls, s.picks, OptionId(kMage, 5, 1)))
                AddEffect(player, s, kFxArcaneEdge, kArcaneEdgeMs);
            break;
        case kWarlock:
        {
            Gain(player, s, Resource::LifeForce, 20);
            // A killing blow gives a soul shard (up to 5).
            if (player->GetItemCount(6265) < 5)
                player->AddItem(6265, 1);
            if (InKit(cls, s.picks, OptionId(kWarlock, 5, 0)))
                HealPct(player, player, 10.f, nullptr);
            break;
        }
        default:
            break;
    }
}

float FusionClasses::HealMultiplier(SpellCaster const* healer, SpellEntry const* spell) const
{
    if (!m_enabled || !healer || !healer->IsPlayer())
        return 1.f;
    FusionClassState const* s = StateOf(static_cast<Player const*>(healer));
    if (!s)
        return 1.f;
    uint32 const now = Now();
    float m = 1.f;
    if (HasEffect(*s, kFxZenith, now))
        m *= 1.f + kZenithHealBonus;
    m *= DualPower(*s, spell, now);     // a dual cast from both hands
    if (HasEffect(*s, OptionId(kDruid, 6, 1), now))      // Tree of Life
        m *= 1.5f;
    if (HasEffect(*s, kFxEclipse, now) && InKit(kDruid, s->picks, OptionId(kDruid, 5, 2)))
        m *= 1.1f;
    // Prayer of Healing's own bonus, cast now.
    if (spell && sSpellMgr.GetFirstSpellInChain(spell->Id) == kSpellPrayerOfHealing)
        for (FusionClassEffect const& e : s->effects)
            if (e.code == kFxDevotion && int32(e.untilMs - now) > 0)
                m *= e.value;
    return m;
}

float FusionClasses::HealTakenMultiplier(Unit const* target) const
{
    if (!m_enabled || !target)
        return 1.f;
    // Each different Afflicter affliction on the target (any warlock's) cuts its heals.
    std::set<uint16> kinds;
    for (auto const& [id, holder] : target->GetSpellAuraHolderMap())
    {
        SpellEntry const* e = holder ? holder->GetSpellProto() : nullptr;
        if (!e || e->SpellFamilyName != SPELLFAMILY_WARLOCK || holder->IsPositive())
            continue;
        if (Option const* o = OptionOfSpell(kWarlock, id);
            o && o->cls == kWarlock && o->archetype == 0)
            kinds.insert(o->id);
    }
    return kinds.empty() ? 1.f : Fusion::ClassTuning::DrainHealMultiplier(uint32(kinds.size()));
}

float FusionClasses::SpeedMultiplier(Player const* player) const
{
    if (!m_enabled)
        return 1.f;
    FusionClassState const* s = StateOf(player);
    if (!s)
        return 1.f;
    uint32 const now = Now();
    float m = 1.f;
    for (FusionClassEffect const& e : s->effects)
    {
        if (int32(e.untilMs - now) <= 0)
            continue;
        switch (e.code)
        {
            case kFxSpeed: m *= e.value; break;
            case kFxFrenzy: m *= 1.3f; break;
            case OptionId(kWarrior, 1, 1): m *= 0.6f; break;    // Bulwark raised
            case OptionId(kHunter, 0, 0): m *= 1.15f; break;    // Mark Prey: on the hunt
            default: break;
        }
    }
    // Zealotry: 4% a Holy Power held.
    if (InKit(player->GetClass(), s->picks, OptionId(kPaladin, 3, 0)))
        for (Meter const& mt : s->meters)
            if (mt.resource == Resource::HolyPower)
                m *= 1.f + 0.04f * mt.value;
    return m;
}

// ── The rogue's helpers ──

bool FusionClasses::HasOpening(FusionClassState const& s, uint32 now) const { return HasEffect(s, kFxOpening, now); }

bool FusionClasses::HasFlank(FusionClassState const& s, uint32 now) const { return HasEffect(s, kFxFlank, now); }

void FusionClasses::OpenNow(Player* player, FusionClassState& s)
{
    if (PicksArchetype(s, kRogue, 0))
        AddEffect(player, s, kFxOpening, kOpeningMs);
}

void FusionClasses::Restealth(Player* player)
{
    // As Vanish does: attacks and casts on the rogue stop, the threat goes, then Stealth.
    player->InterruptSpellsCastedOnMe(true);
    player->InterruptAttacksOnMe(0.0f, false);
    player->CombatStop();
    HostileReference* ref = player->GetHostileRefManager().getFirst();
    while (ref)
    {
        HostileReference* next = ref->next();
        ref->removeReference();
        delete ref;
        ref = next;
    }
    if (uint32 const stealth = RankFor(1787, uint8(std::min<uint32>(player->GetLevel(), 255))))
    {
        player->RemoveSpellCooldown(sSpellMgr.GetSpellEntry(stealth), true);
        player->CastSpell(player, stealth, true);
    }
}

void FusionClasses::LiftPurse(Player* player, Unit* target)
{
    // Light Fingers: the creature's own pickpocket loot (its table, and the coin vanilla adds),
    // straight into the bags; once a creature, as vanilla's Pick Pocket.
    Creature* c = target ? target->ToCreature() : nullptr;
    PocketTarget t;
    t.creature = c != nullptr;
    t.alive = target && target->IsAlive();
    t.hostile = target && !player->IsFriendlyTo(target);
    t.humanoid = target && target->GetCreatureType() == CREATURE_TYPE_HUMANOID;
    t.hasPocketLoot = c && c->GetCreatureInfo()->pickpocket_loot_id;
    t.alreadyPicked = c && c->lootForPickPocketed;
    t.distance = target ? player->GetDistance(target) : 0.f;
    t.inCombat = player->IsInCombat();
    t.lightFingers = true;
    switch (CanLift(t))
    {
        case PocketVerdict::Ok: break;
        case PocketVerdict::Picked:
            ChatHandler(player).SendSysMessage("|cffff6060Its pockets are empty.|r");
            return;
        case PocketVerdict::TooFar:
            ChatHandler(player).SendSysMessage("|cffff6060Too far to reach its purse.|r");
            return;
        default:
            ChatHandler(player).SendSysMessage("|cffff6060Nothing to lift there.|r");
            return;
    }
    Loot loot(c);
    if (uint32 const id = c->GetCreatureInfo()->pickpocket_loot_id)
        loot.FillLoot(id, LootTemplates_Pickpocketing, player, true);
    uint32 const a = urand(0, c->GetLevel() / 2);
    uint32 const b = urand(0, player->GetLevel() / 2);
    uint32 const coin = uint32(10 * (a + b) * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));
    c->lootForPickPocketed = true;
    player->AutoStoreLoot(loot, false);
    if (coin)
    {
        player->ModifyMoney(int32(coin));
        player->SendLootMoneyNotify(coin);
    }
    ChatHandler(player).SendSysMessage("|cff80ff80You lift its purse.|r");
}

// ── The options ──

void FusionClasses::CastOption(Player* player, FusionClassState& s, Option const& o, Spell* spell)
{
    uint32 const now = Now();
    uint64 const me = player->GetObjectGuid().GetRawValue();
    SpellEntry const* info = spell->m_spellInfo;
    // Charging through fire (fusion/ClassTuning.h): a gap-closer halves the bullets for a moment.
    if (Fusion::ClassTuning::IsGapCloser(o.id))
        AddEffect(player, s, kFxBulletCut, Fusion::ClassTuning::kGapGuardMs, me ^ 0x6A9, Fusion::ClassTuning::kGapGuardBulletCut);
    switch (o.id)
    {
        // ── Warrior ──
        case OptionId(kWarrior, 0, 1):          // Bloodrage: 15% back over 6 s
            AddEffect(player, s, kFxRegen, 6000, me ^ o.id, 2.5f);
            break;
        case OptionId(kWarrior, 0, 2):          // Disarm: 4 s on a creature, 3 s on a player (no gun either)
        case OptionId(kPaladin, 0, 1):          // Hammer of Justice: 5 s / 3 s
            if (Unit* t = spell->m_targets.getUnitTarget(); t && t != player)
                if (DurationCap const* c = DurationCapOf(o.id))
                    s.auraFixes.push_back({t->GetObjectGuid().GetRawValue(), sSpellMgr.GetFirstSpellInChain(info->Id),
                        c->playerMs, c->npcMs, false, now + 2000});
            break;
        case OptionId(kWarrior, 1, 0):          // Bloodthirst heals 10%
            HealPct(player, player, 10.f, info);
            break;
        case OptionId(kWarrior, 1, 1):          // Bulwark: the frontal arc for 6 s
            AddEffect(player, s, o.id, 6000);
            break;
        case OptionId(kWarrior, 1, 2):          // Battle Standard (War Banners: three kinds)
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            z.at = Pos(player);
            z.radius = 15.f;
            z.startMs = now;
            z.untilMs = now + 30000;
            if (InKitOf(player, s, OptionId(kWarrior, 5, 2)))
            {
                z.variant = s.bannerKind;
                s.bannerKind = uint8((s.bannerKind + 1) % 3);
                // At most three banners.
                uint32 banners = 0;
                for (auto it = s.zones.rbegin(); it != s.zones.rend(); ++it)
                    if (it->code == o.id && ++banners >= 3)
                        it->untilMs = now;
                static char const* const kinds[] = {"Rally", "Ward", "Pursuit"};
                ChatHandler(player).PSendSysMessage("Banner of %s planted.", kinds[z.variant]);
            }
            else
                for (FusionClassZone& old : s.zones)
                    if (old.code == o.id)
                        old.untilMs = now;      // one at a time
            s.zones.push_back(z);
            break;
        }
        case OptionId(kWarrior, 2, 1):          // Chain Hook: yanked to the shield, stunned 1 s
            if (Unit* t = EnemyTarget(player, spell, 30.f))
            {
                PullTo(t, Ahead(player, 2.f), player->GetOrientation() + kPi);
                TimedAura(t, kAuraStun, 1000, player);
            }
            break;
        case OptionId(kWarrior, 2, 2):          // Rallying Cry: 15% now, 10% absorb for 8 s
            for (Player* m : Party(player, 30.f))
            {
                // A downed ally gets 10 s more to be revived instead (held at 1 health anyway).
                if (sFusionMgr.IsDowned(m))
                {
                    sFusionMgr.ExtendDowned(m, kRallyingCryBleedMs);
                    continue;
                }
                HealPct(player, m, 15.f, info);
                AddEffect(m, StateOf(m), kFxAbsorb, 8000, me ^ o.id, m->GetMaxHealth() * 0.10f);
            }
            break;
        case OptionId(kWarrior, 3, 1):          // Draw Fire: players near are marked
            for (Unit* u : Enemies(player, player, 25.f))
                if (Player* e = u->ToPlayer(); e && e->HasFusion())
                    AddEffect(e, StateOf(e), kFxDrawFire, 4000, me, 0.f);
            break;
        case OptionId(kWarrior, 3, 2):          // Demoralizing Shout: enemies near deal 20% less, spread +50%
            for (Unit* u : Enemies(player, player, 20.f))
            {
                if (Player* e = u->ToPlayer(); e && e->HasFusion())
                    AddEffect(e, StateOf(e), kFxDemoralized, 10000, me, 0.8f);
                else if (u->IsCreature())
                    MarkUnit(u, o.id, 10000, 0.8f);
            }
            break;
        case OptionId(kWarrior, 5, 1):          // Shield Charge: a path swept behind a raised shield
        {
            Unit* t = EnemyTarget(player, spell, 30.f);
            Fusion::Vec3 const from = Pos(player);
            Fusion::Vec3 const to = t ? Pos(t) : Ahead(player, 20.f);
            for (Unit* u : Enemies(player, player, 30.f))
                if (NearSegment(from, to, Pos(u), 2.5f))
                {
                    Strike(player, u, 8.f);
                    TimedAura(u, kAuraStun, 2000, player);
                }
            AddEffect(player, s, OptionId(kWarrior, 1, 1), 1500);     // Bulwark up for the charge
            break;
        }
        case OptionId(kWarrior, 5, 0):          // Death Wish: 20% fewer bullets hurt, 15 s
            AddEffect(player, s, o.id, 15000);
            break;
        case OptionId(kWarrior, 6, 0):          // Rampage: grown, Fury held full, 20 s
            AddEffect(player, s, o.id, 20000);
            if (!HasEffect(s, kFxScale, now))
            {
                AddEffect(player, s, kFxScale, 20000, 0, player->GetObjectScale());
                player->SetObjectScale(player->GetObjectScale() * 1.3f);
            }
            Frenzy(player, s, 20000);       // its Frenzy held for its 20 s
            break;
        case OptionId(kWarrior, 6, 1):          // Unbreakable: a dome around the warrior, 12 s
        {
            Blocker b;
            b.shape = BlockShape::Dome;
            b.center = Pos(player);
            b.radius = 6.f;
            b.owner = me;
            b.untilMs = now + 12000;
            b.followsOwner = true;
            AddBlocker(player->GetMapId(), b);
            // +50% max health (Last Stand's own +30% replaced).
            BonusHealth(player, 0.5f, 12000);
            AddEffect(player, s, kFxBonusHealth, 12000);
            break;
        }
        case OptionId(kWarrior, 6, 2):          // Warlord's Command: 3%/s and no stun or fear for 20 s
            for (Player* m : Party(player, 40.f))
            {
                // Downed allies within 15 yd stand up at 30% as it starts.
                if (sFusionMgr.IsDowned(m))
                {
                    if (player->IsWithinDistInMap(m, 15.f))
                        sFusionMgr.ReviveDownedBy(m, player->GetObjectGuid(), 0.3f);
                    else
                        continue;
                }
                AddEffect(m, StateOf(m), kFxRegen, 20000, me ^ o.id, 3.f);
                AddEffect(m, StateOf(m), o.id, 20000, me);
            }
            break;

        // ── Paladin ──
        case OptionId(kPaladin, 0, 0):          // Divine Protection: half damage, 8 s
            AddEffect(player, s, kFxDamageCut, 8000, me ^ o.id, 0.5f);
            break;
        case OptionId(kPaladin, 1, 1):          // Holy Shield: an absorb of bullets on an ally
        {
            Player* a = AllyTarget(player, 30.f);
            AddEffect(a, StateOf(a), kFxAbsorbBullet, 10000, me, player->GetMaxHealth() * 0.2f);
            break;
        }
        case OptionId(kPaladin, 2, 0):          // Judgement: 8 %EH a Holy Power; at 3 a stun
        {
            Unit* t = EnemyTarget(player, spell, 20.f);
            Meter* hp = MeterOf(s, Resource::HolyPower);
            uint16 const power = hp ? hp->value : 0;
            if (t)
            {
                Strike(player, t, 8.f * std::max<uint16>(1, power));
                if (power >= 3)
                    TimedAura(t, kAuraStun, 1500, player);
            }
            if (hp && !HasEffect(s, OptionId(kPaladin, 6, 0), now))
                hp->value = 0, s.metersDirty = true;
            break;
        }
        case OptionId(kPaladin, 2, 1):          // Consecration: enemies inside slowed, Sacred Ground's ward
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            // Where it burns: the hand's point (FusionSpellcasting.cpp), else at the paladin's feet.
            z.at = spell->m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION
                ? Fusion::Vec3{spell->m_targets.m_destX, spell->m_targets.m_destY, spell->m_targets.m_destZ}
                : Pos(player);
            z.radius = 8.f;
            z.startMs = now;
            z.untilMs = now + 8000;
            s.zones.push_back(z);
            break;
        }
        case OptionId(kPaladin, 3, 1):          // Blessing of Protection: 6 s (it pacifies: no shot)
            if (Unit* t = spell->m_targets.getUnitTarget())
                s.auraFixes.push_back({t->GetObjectGuid().GetRawValue(), sSpellMgr.GetFirstSpellInChain(info->Id),
                    6000, 6000, false, now + 2000});
            break;
        case OptionId(kPaladin, 4, 1):          // Blessing of Sacrifice: half of what an ally takes, 12 s
        {
            Unit* t = spell->m_targets.getUnitTarget();
            Player* a = t && t->IsPlayer() && t != player ? t->ToPlayer() : AllyTarget(player, 30.f);
            if (a && a != player && a->HasFusion())
            {
                AddEffect(a, StateOf(a), kFxSacrifice, 12000, me);
                ChatHandler(a).PSendSysMessage("|cffffe080%s takes half of your wounds.|r", player->GetName());
            }
            break;
        }
        case OptionId(kPaladin, 4, 0):          // Divine Storm: a finisher around the paladin
        {
            Meter* hp = MeterOf(s, Resource::HolyPower);
            uint16 const power = std::max<uint16>(1, hp ? hp->value : 0);
            for (Unit* u : Enemies(player, player, 8.f))
                Strike(player, u, 10.f * power);
            for (Player* m : Party(player, 8.f))
                HealPct(player, m, 5.f * power, info);
            if (hp && !HasEffect(s, OptionId(kPaladin, 6, 0), now))
                hp->value = 0, s.metersDirty = true;
            break;
        }
        case OptionId(kPaladin, 4, 2):          // Rekindle: in combat it is the battle rez, 10 min
            if (InKitOf(player, s, o.id) && player->IsInCombat())
            {
                uint32 const ms = SpecOf(o.id) ? SpecOf(o.id)->cooldownMs : 600000;
                player->AddCooldown(info, nullptr, false, ms);
                if (!s.cooldownSync.empty() && s.cooldownSync.back().first == info->Id)
                    s.cooldownSync.back().second = ms;
            }
            break;
        case OptionId(kPaladin, 5, 2):          // Lay on Hands: a downed ally stands up (then is healed full)
            if (Unit* t = spell->m_targets.getUnitTarget())
                sFusionMgr.ReviveBySpell(player, t);
            break;
        case OptionId(kPaladin, 6, 0):          // Avenging Crusade, 20 s
            AddEffect(player, s, o.id, 20000);
            break;
        case OptionId(kPaladin, 6, 1):          // Bastion of Light: an 8 yd dome, 8 s
        {
            Blocker b;
            b.shape = BlockShape::Dome;
            b.center = Pos(player);
            b.radius = 8.f;
            b.owner = me;
            b.untilMs = now + 8000;
            AddBlocker(player->GetMapId(), b);
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            z.at = b.center;
            z.radius = 8.f;
            z.startMs = now;
            z.untilMs = b.untilMs;
            s.zones.push_back(z);
            break;
        }
        case OptionId(kPaladin, 6, 2):          // Solar Ascension: Zenith held for 20 s
            AddEffect(player, s, o.id, 20000);
            AddEffect(player, s, kFxZenith, 20000);
            break;

        // ── Hunter ──
        case OptionId(kHunter, 0, 0):           // Mark Prey: seen through walls by the party
            if (Unit* t = EnemyTarget(player, spell, 100.f))
            {
                // One prey; three under Overwatch (the oldest goes).
                uint64 const g = t->GetObjectGuid().GetRawValue();
                size_t const most = HasEffect(s, OptionId(kHunter, 6, 0), now) ? kOverwatchPrey : 1;
                s.prey.erase(std::remove(s.prey.begin(), s.prey.end(), g), s.prey.end());
                s.prey.push_back(g);
                while (s.prey.size() > most)
                    s.prey.erase(s.prey.begin());
                AddEffect(player, s, o.id, 30000);
                Spot(player, {t}, 30000);
            }
            break;
        case OptionId(kHunter, 0, 1):           // Feign Death: 6 s at most; the enemies aiming see a kill
        {
            s.auraFixes.push_back({me, info->Id, 6000, 6000, false, now + 2000});
            Fusion::Packets::Events ev;
            ev.events.push_back({Fusion::Packets::EventKind::Kill, 0, me, 0, 0});
            for (Unit* u : Enemies(player, player, 100.f))
                if (Player* e = u->ToPlayer(); e && e->HasFusion()
                    && (e->GetSelectionGuid() == player->GetObjectGuid() || e->GetVictim() == player))
                    sFusionMgr.SendEvents(e, ev);
            break;
        }
        case OptionId(kHunter, 0, 2):           // Disengage: 15 yd back, 30% less damage 2 s
            LeapTo(player, player->GetOrientation() + kPi, 15.f, 7.f);
            AddEffect(player, s, kFxDamageCut, 2000, me ^ o.id, 0.7f);
            break;
        case OptionId(kHunter, 1, 0):           // Field Glass: enemies in the glass spotted 8 s
        {
            std::vector<Unit*> seen;
            for (Unit* u : Enemies(player, player, 80.f))
                if (InArc(Pos(player), player->GetOrientation(), 15.f * kPi / 180.f, Pos(u))
                    && player->IsWithinLOSInMap(u))
                    seen.push_back(u);
            Spot(player, seen, 8000);
            break;
        }
        case OptionId(kHunter, 1, 2):           // Bane: the target's kind (or casters) for 60 s
            if (Unit* t = EnemyTarget(player, spell, 40.f))
            {
                bool const caster = t->IsPlayer() && t->GetPowerType() == POWER_MANA;
                AddEffect(player, s, o.id, 60000, 0, caster ? -1.f : float(t->GetCreatureType()));
            }
            break;
        case OptionId(kHunter, 2, 0):           // Flare: fired at the crosshair's enemy or 30 yd ahead; 15 yd lit, 20 s
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            Unit* t = Selected(player);
            z.at = Hostile(player, t) && player->IsWithinDistInMap(t, 40.f) ? Pos(t) : Ahead(player, 30.f);
            z.radius = 15.f;
            z.startMs = now;
            z.untilMs = now + 20000;
            s.zones.push_back(z);
            break;
        }
        case OptionId(kHunter, 2, 1):           // Net Launcher: no sprint for 8 s
            if (Unit* t = EnemyTarget(player, spell, 30.f))
                if (Player* e = t->ToPlayer(); e && e->HasFusion())
                    AddEffect(e, StateOf(e), kFxNoSprint, 8000, me);
            break;
        case OptionId(kHunter, 2, 2):           // Throwing Axes: 8 %EH and a slow
            if (Unit* t = EnemyTarget(player, spell, 30.f))
            {
                Strike(player, t, 8.f * Fusion::ClassTuning::DamageScale(o.id));
                TimedAura(t, kAuraDazed, 3000, player);
                // Witchfire: the axes burn too.
                if (HasEffect(s, OptionId(kHunter, 6, 2), now))
                    AddDot(s.dots, t->GetObjectGuid().GetRawValue(), OptionId(kHunter, 6, 2), now, 4000, 3.f, 1);
            }
            break;
        case OptionId(kHunter, 3, 0):           // Camouflage: on, it fades in once still 2 s; again, off
        {
            bool const wasOn = s.camo.on;
            if (!s.camo.Toggle(now))
            {
                ChatHandler(player).PSendSysMessage("|cffff6060Camouflage is ready again in %u s.|r",
                    uint32((s.camo.readyMs - now + 999) / 1000));
                break;
            }
            if (wasOn)
            {
                player->RemoveAurasDueToSpell(kAuraCamouflage);
                ChatHandler(player).SendSysMessage("Camouflage off.");
            }
            else
                ChatHandler(player).SendSysMessage("Camouflage: stand still to fade in.");
            break;
        }
        case OptionId(kHunter, 1, 1):           // Freezing Trap
        case OptionId(kHunter, 3, 1):           // Tar Trap
        case OptionId(kHunter, 4, 1):           // Bear Trap
        case OptionId(kHunter, 6, 1):           // Killing Field
        {
            // Prepared Ground: four traps at once (eight in the Killing Field); the oldest goes.
            bool const field = o.id == OptionId(kHunter, 6, 1) || HasEffect(s, OptionId(kHunter, 6, 1), now);
            size_t const limit = TrapLimit(field);
            size_t const adding = o.id == OptionId(kHunter, 6, 1) ? 5 : 1;    // the field: its own and four
            std::vector<GameObject*> traps = TrapsOf(player);
            for (size_t i = 0; i < traps.size() && traps.size() - i + adding > limit; ++i)
                traps[i]->SetLootState(GO_JUST_DEACTIVATED);
            if (o.id != OptionId(kHunter, 6, 1))
                break;
            // Killing Field: four armed traps in a ring (Freezing, Tar, Bear and Net, as known;
            // Freezing for the rest), and for 60 s the limit is 8 and the chain reaches 12 yd.
            AddEffect(player, s, o.id, 60000);
            uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
            uint32 const freezing = RankFor(14311, level);
            uint32 const ring[4] = {
                freezing,
                s.learned.count(OptionId(kHunter, 3, 1)) ? 13809u : freezing,
                s.learned.count(OptionId(kHunter, 4, 1)) ? 9437u : freezing,
                freezing,
            };
            for (int i = 0; i < 4; ++i)
            {
                float const a = player->GetOrientation() + float(i) * kPi / 2.f;
                float x = player->GetPositionX() + std::cos(a) * 5.f;
                float y = player->GetPositionY() + std::sin(a) * 5.f;
                float z = player->GetPositionZ();
                player->UpdateAllowedPositionZ(x, y, z);
                player->CastSpell(x, y, z, ring[i], true);
            }
            break;
        }
        case OptionId(kHunter, 3, 2):           // Purge: two magic buffs off, silence 1 s
            if (Unit* t = EnemyTarget(player, spell, 30.f))
            {
                std::vector<uint32> buffs;
                for (auto const& [id, holder] : t->GetSpellAuraHolderMap())
                    if (holder && holder->IsPositive() && !holder->IsPassive()
                        && holder->GetSpellProto()->Dispel == DISPEL_MAGIC
                        && std::find(buffs.begin(), buffs.end(), id) == buffs.end())
                        buffs.push_back(id);
                for (size_t i = 0; i < buffs.size() && i < 2; ++i)
                    t->RemoveAurasDueToSpell(buffs[i]);
                TimedAura(t, kAuraSilence, 1000, player);
            }
            break;
        case OptionId(kHunter, 4, 0):           // Grappling Hook: reel to the target or a rooftop ahead
        {
            Fusion::Vec3 to;
            if (Unit* t = Selected(player); t && player->IsWithinDistInMap(t, 35.f) && player->IsWithinLOSInMap(t))
                to = Pos(t);
            else
            {
                float const o2 = player->GetOrientation();
                float x = player->GetPositionX() + std::cos(o2) * 30.f;
                float y = player->GetPositionY() + std::sin(o2) * 30.f;
                float z = player->GetMap()->GetHeight(x, y, player->GetPositionZ() + 20.f);
                if (z < player->GetPositionZ() - 30.f)
                    z = player->GetPositionZ();
                to = {x, y, z};
            }
            float const dist = std::sqrt((to.x - player->GetPositionX()) * (to.x - player->GetPositionX())
                + (to.y - player->GetPositionY()) * (to.y - player->GetPositionY()));
            float const rise = std::max(0.f, to.z - player->GetPositionZ()) + 3.f;
            LeapTo(player, player->GetAngle(to.x, to.y), dist, std::sqrt(2.f * 19.29f * rise));
            break;
        }
        case OptionId(kHunter, 4, 2):           // Scatter: also silenced 4 s
            if (Unit* t = EnemyTarget(player, spell, 20.f))
                TimedAura(t, kAuraSilence, 4000, player);
            break;
        case OptionId(kHunter, 5, 0):           // Smoke Pellet
            AddSmoke(player, Pos(player), 8.f, 10000);
            break;
        case OptionId(kHunter, 5, 2):           // Hex Ward: a spell absorb and the next hold shrugged off
            for (Player* m : Party(player, 10.f))
            {
                AddEffect(m, StateOf(m), kFxAbsorbSpell, 8000, me, m->GetMaxHealth() * 0.3f);
                AddEffect(m, StateOf(m), kFxHexWard, 8000, me);
            }
            break;
        case OptionId(kHunter, 6, 0):           // Overwatch, 20 s
            AddEffect(player, s, o.id, 20000);
            break;
        case OptionId(kHunter, 6, 2):           // Witchfire, 30 s
            AddEffect(player, s, o.id, 30000);
            break;

        // ── Rogue ──
        // Assassin. Ambush is vanilla's blow (CheckCast holds the opener's gate); it sunders a
        // player's gun for a moment (fusion/ClassTuning.h kSunderDisarmMs).
        case OptionId(kRogue, 1, 0):
            if (Unit* t = EnemyTarget(player, spell, 6.f); t && t->IsPlayer())
                TimedAura(t, kAuraDisarm, Fusion::ClassTuning::kSunderDisarmMs, player);
            break;
        case OptionId(kRogue, 2, 0):            // Cheap Shot: 4 s / 2.5 s, a second more from behind
            if (Unit* t = EnemyTarget(player, spell, 6.f))
            {
                bool const behind = player->IsBehindTarget(t, false) || HasEffect(s, kFxFlank, now);
                s.auraFixes.push_back({t->GetObjectGuid().GetRawValue(), sSpellMgr.GetFirstSpellInChain(info->Id),
                    CheapShotMs(true, behind), CheapShotMs(false, behind), false, now + 2000});
            }
            break;
        case OptionId(kRogue, 3, 0):            // Garrote: also silenced 3 s (4 s from behind)
            if (Unit* t = EnemyTarget(player, spell, 6.f))
                TimedAura(t, kAuraSilence, GarroteSilenceMs(player->IsBehindTarget(t, false) || HasEffect(s, kFxFlank, now)),
                    player);
            break;
        case OptionId(kRogue, 4, 0):            // Distract: a point 20 yd ahead; creatures near it turn to it
        {
            Fusion::Vec3 const at = Ahead(player, 20.f);
            for (Unit* u : EnemiesAt(player, at, 10.f))
                if (Creature* c = u->ToCreature(); c && !c->IsInCombat() && !c->IsWorldBoss())
                {
                    c->SetFacingTo(c->GetAngle(at.x, at.y));
                    c->GetMotionMaster()->MoveDistract(10000);
                }
            AddEffect(player, s, kFxFlank, kDistractFlankMs);
            break;
        }
        case OptionId(kRogue, 5, 0):            // Preparation: the core and the Assassin ready again
        {
            uint8 const level = uint8(std::min<uint32>(player->GetLevel(), 255));
            for (uint16 id : PreparationReadies())
                if (Option const* r = FindOption(id); r && r->spells[0])
                    if (SpellEntry const* e = sSpellMgr.GetSpellEntry(RankFor(r->spells[0], level)))
                        player->RemoveSpellCooldown(e, true);
            ChatHandler(player).SendSysMessage("|cffc0c0ffPrepared: your openers and shadows are ready.|r");
            break;
        }
        // Trickster.
        case OptionId(kRogue, 1, 1):            // Deadly Poison: the blades coated, 60 s
            AddEffect(player, s, o.id, 60000);
            break;
        case OptionId(kRogue, 2, 1):            // Smoke Bomb: the smoke, and back into the shadows in it
            AddSmoke(player, Pos(player), 8.f, 8000);
            player->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            AddEffect(player, s, kFxUntargetable, 1000);
            Restealth(player);
            break;
        case OptionId(kRogue, 3, 1):            // Poison Satchel: a coat, pressed again the next one
        {
            Poison const p = HasEffect(s, o.id, now) ? NextPoison(Poison(s.poison)) : Poison::Crippling;
            s.poison = uint8(p);
            AddEffect(player, s, o.id, 60000, 0, float(s.poison));
            ChatHandler(player).PSendSysMessage("|cff80ff80%s on your blades.|r", PoisonName(p));
            break;
        }
        case OptionId(kRogue, 4, 1):            // Light Fingers: a purse lifted, in combat or out
            LiftPurse(player, Selected(player));
            break;
        case OptionId(kRogue, 5, 1):            // Decoy: a running copy; the rogue unseen 3 s
        {
            Fusion::Vec3 const ahead = Ahead(player, 25.f);
            if (Creature* c = RaiseMinion(player, s, o.id, kStalker, Pos(player), 6000, 0.3f))
            {
                c->SetDisplayId(player->GetDisplayId());
                c->GetMotionMaster()->MovePoint(1, ahead.x, ahead.y, ahead.z);
            }
            TimedAura(player, kAuraInvisible, 3000, player);
            break;
        }
        // Duelist.
        case OptionId(kRogue, 1, 2):            // Sinister Strike: the off hand's blow after the main's
            if (Unit* t = EnemyTarget(player, spell, 6.f))
            {
                Strike(player, t, kSinisterSecondPct);
                if (Meter* m = MeterOf(s, Resource::Momentum); m && m->Add(1))
                    s.metersDirty = true;
                s.lastBlowMs = now;
            }
            break;
        case OptionId(kRogue, 2, 2):            // Lunge: a dash at the enemy in the crosshair, then a blow
            if (Unit* t = EnemyTarget(player, spell, kLungeRange))
            {
                float const dist = player->GetDistance(t);
                if (dist > kLungeMinRange)
                {
                    player->SetLaunched(true);
                    LeapTo(player, player->GetAngle(t), dist - 1.5f, 4.f);
                }
                AddEffect(player, s, kFxLungeBlow, dist > kLungeMinRange ? 450 : 50, t->GetObjectGuid().GetRawValue());
            }
            break;
        case OptionId(kRogue, 3, 2):            // Riposte: the guard up 1.5 s
            AddEffect(player, s, kFxRiposte, kRiposteMs);
            break;
        case OptionId(kRogue, 4, 2):            // Blade Flurry, 12 s
            AddEffect(player, s, o.id, 12000);
            break;
        case OptionId(kRogue, 6, 2):            // Adrenaline Rush: Momentum full and held, 15 s
            AddEffect(player, s, o.id, 15000);
            if (Meter* m = MeterOf(s, Resource::Momentum))
            {
                m->value = m->max;
                s.metersDirty = true;
            }
            s.lastBlowMs = now;
            break;
        case OptionId(kRogue, 6, 0):            // Deathmark: 10 s
            if (Unit* t = EnemyTarget(player, spell, 10.f))
            {
                s.deathmark = t->GetObjectGuid().GetRawValue();
                s.deathmarkDamage = 0.f;
                AddEffect(player, s, o.id, 10000);
            }
            break;
        case OptionId(kRogue, 6, 1):            // Hall of Mirrors: three copies, 12 s; again, a swap
        {
            if (HasEffect(s, o.id, now))
            {
                // A swap with the copy in the crosshair (or the first standing): three in all.
                Creature* mirror = nullptr;
                Unit* sel = Selected(player);
                for (FusionClassMinion const& m : s.minions)
                    if (m.code == o.id)
                        if (Creature* c = player->GetMap()->GetCreature(ObjectGuid(m.guid)); c && c->IsAlive())
                            if (!mirror || c == sel)
                                mirror = c;
                if (mirror && s.mirrorSwaps)
                {
                    Fusion::Vec3 const here = Pos(player);
                    float const facing = player->GetOrientation();
                    player->NearTeleportTo(mirror->GetPositionX(), mirror->GetPositionY(), mirror->GetPositionZ(),
                        mirror->GetOrientation());
                    mirror->NearTeleportTo(here.x, here.y, here.z, facing);
                    --s.mirrorSwaps;
                }
                if (s.mirrorSwaps)
                    player->RemoveSpellCooldown(info, true);
                break;
            }
            for (int i = 0; i < 3; ++i)
            {
                float const a = player->GetOrientation() + (float(i) - 1.f) * 1.2f;
                Fusion::Vec3 at{player->GetPositionX() + std::cos(a) * 3.f, player->GetPositionY() + std::sin(a) * 3.f,
                    player->GetPositionZ()};
                if (Creature* c = RaiseMinion(player, s, o.id, kStalker, at, 12000, 0.3f))
                    c->SetDisplayId(player->GetDisplayId());
            }
            AddEffect(player, s, o.id, 12000);
            // The button swaps while it lasts; its cooldown starts when it ends.
            s.mirrorSwaps = uint8(kMirrorSwaps);
            AddEffect(player, s, kFxMirrorEnd, 12000, 0, float(info->Id));
            player->RemoveSpellCooldown(info, true);
            break;
        }

        // ── Priest ──
        case OptionId(kPriest, 0, 1):           // Desperate Prayer: a downed priest stands up
            sFusionMgr.ReviveBySpell(player, player);
            break;
        case OptionId(kPriest, 1, 2):           // Barrier: a wall of light ahead, 8 s
        case OptionId(kMage, 1, 2):             // Warding Rune: a smaller screen, 10 s
        {
            bool const barrier = o.id == OptionId(kPriest, 1, 2);
            Blocker b;
            b.shape = BlockShape::Wall;
            b.center = Ahead(player, barrier ? 6.f : 4.f);
            b.facing = player->GetOrientation();
            b.radius = barrier ? 3.f : 2.f;
            b.height = 3.f;
            b.owner = me;
            b.untilMs = now + (barrier ? 8000 : 10000);
            b.health = player->GetMaxHealth() * (barrier ? kBarrierHealthShare : 0.4f);
            AddBlocker(player->GetMapId(), b);
            if (!barrier)
                AddConstruct(player, s, o.id, b.untilMs);
            break;
        }
        case OptionId(kPriest, 2, 2):           // Pain Suppression on an ally, 8 s
        {
            Player* a = AllyTarget(player, 40.f);
            AddEffect(a, StateOf(a), kFxDamageCut, 8000, me ^ o.id, 0.6f);
            break;
        }
        case OptionId(kPriest, 3, 0):           // Vampiric Embrace on the priest
            AddEffect(player, s, kFxVampiric, kVampiricMs);
            break;
        case OptionId(kPriest, 3, 1):           // Prayer of Healing: the Cleric's heals stronger
            AddEffect(player, s, kFxDevotion, 3000, 0, kPrayerBonus);
            break;
        case OptionId(kPriest, 4, 1):           // Lightwell: heals by itself an ally below 35% near it, 5 times, 60 s
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            z.at = Ahead(player, 3.f);
            z.radius = 5.f;
            z.startMs = now;
            z.untilMs = now + 60000;
            z.charges = 5;
            s.zones.push_back(z);
            break;
        }
        case OptionId(kPriest, 5, 0):           // Mind Control: a player is held 5 s, a creature 30 s
            if (Unit* t = spell->m_targets.getUnitTarget(); t && t != player)
                AddEffect(player, s, kFxEndCharm, t->IsPlayer() ? 5000 : 30000, t->GetObjectGuid().GetRawValue(),
                    float(info->Id));
            break;
        case OptionId(kPriest, 6, 0):           // Shadowform: 25 s
            AddEffect(player, s, o.id, 25000);
            AddEffect(player, s, kFxRemoveAura, 25000, info->Id, float(info->Id));
            break;
        case OptionId(kPriest, 6, 1):           // Divine Hymn, 8 s
            AddEffect(player, s, o.id, 8000);
            break;
        case OptionId(kPriest, 6, 2):           // Sanctum: a 10 yd dome, 10 s
        {
            Blocker b;
            b.shape = BlockShape::Dome;
            b.center = Pos(player);
            b.radius = 10.f;
            b.owner = me;
            b.untilMs = now + 10000;
            AddBlocker(player->GetMapId(), b);
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            z.at = b.center;
            z.radius = 10.f;
            z.startMs = now;
            z.untilMs = b.untilMs;
            s.zones.push_back(z);
            break;
        }

        // ── Shaman ──
        case OptionId(kShaman, 1, 0):           // Lightning Bolt: at full Static, an Overload
            if (Meter* st = MeterOf(s, Resource::Static); st && st->Full())
            {
                Unit* t = EnemyTarget(player, spell, 40.f);
                uint32 chains = InKitOf(player, s, OptionId(kShaman, 5, 0)) ? 4 : 3;
                if (HasEffect(s, OptionId(kShaman, 4, 0), now))
                    chains = 5;
                if (t)
                {
                    std::vector<Unit*> around = Enemies(player, t, 10.f);
                    uint32 n = 0;
                    for (Unit* u : around)
                    {
                        if (n++ >= chains)
                            break;
                        if (u != t)
                            Strike(player, u, 15.f);
                        TimedAura(u, kAuraStun, 1000, player);
                    }
                    // Overcharge: a static field where it struck, 5 yd, 4 s, 40% slower.
                    if (InKitOf(player, s, OptionId(kShaman, 5, 0)))
                    {
                        FusionClassZone z;
                        z.code = OptionId(kShaman, 5, 0);
                        z.mapId = player->GetMapId();
                        z.at = Pos(t);
                        z.radius = 5.f;
                        z.startMs = now;
                        z.untilMs = now + 4000;
                        s.zones.push_back(z);
                    }
                }
                st->value = 0;
                s.metersDirty = true;
                ChatHandler(player).SendSysMessage("|cff80c0ffOverload!|r");
            }
            break;
        case OptionId(kShaman, 1, 1):           // Stone Fists, 60 s
            AddEffect(player, s, o.id, 60000);
            break;
        case OptionId(kShaman, 1, 2):           // Healing Stream Totem: 2% a second, 20 yd, 30 s
        case OptionId(kShaman, 2, 2):           // Earthbind Totem: no sprint within 10 yd, 30 s
        case OptionId(kShaman, 5, 2):           // Windwall Totem: 30% fewer bullets hurt within 8 yd, 20 s
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            z.at = Pos(player);
            z.radius = o.id == OptionId(kShaman, 1, 2) ? 20.f : o.id == OptionId(kShaman, 2, 2) ? 10.f : 8.f;
            z.startMs = now;
            z.fullMs = o.id == OptionId(kShaman, 5, 2) ? 20000 : 30000;
            z.untilMs = now + z.fullMs;
            // One of each at a time (the totem's own element slot): the old zone goes, its totem
            // already replaced by the new one.
            for (FusionClassZone& old : s.zones)
                if (old.code == o.id)
                {
                    old.untilMs = now;
                    old.fullMs = 0;
                    old.variant = 0xFF;
                }
            s.zones.push_back(z);
            break;
        }
        case OptionId(kShaman, 3, 2):           // Grounding Totem: 15 s; also catches the next enemy grenade within 10 yd
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            z.at = Pos(player);
            z.radius = 10.f;
            z.startMs = now;
            z.fullMs = 15000;
            z.untilMs = now + z.fullMs;
            for (FusionClassZone& old : s.zones)
                if (old.code == o.id)
                {
                    old.untilMs = now;
                    old.fullMs = 0;
                    old.variant = 0xFF;
                }
            s.zones.push_back(z);
            std::lock_guard<std::mutex> lock(m_worldLock);
            std::vector<Grounder>& list = m_grounders[player->GetMapId()];
            for (Grounder& g : list)
                if (g.owner == me)
                    g.untilMs = now;        // the old totem's
            list.push_back({me, z.at, z.untilMs, false});
            break;
        }
        case OptionId(kShaman, 3, 1):           // Earth Shock: class abilities locked 3 s
            if (Unit* t = EnemyTarget(player, spell, 20.f))
                TimedAura(t, kAuraSilence, 3000, player);
            break;
        case OptionId(kShaman, 4, 2):           // Chain Heal: a totem it passes near starts its time over
            if (Unit* t = spell->m_targets.getUnitTarget())
                for (FusionClassZone& z : s.zones)
                    if (z.fullMs && (z.at - Pos(t)).Length() <= 20.f)
                    {
                        z.untilMs = now + z.fullMs;
                        ChatHandler(player).SendSysMessage("Chain Heal renews a totem.");
                    }
            break;
        case OptionId(kShaman, 4, 0):           // Elemental Mastery: a wider Overload, 15 s
            AddEffect(player, s, o.id, 15000);
            break;
        case OptionId(kShaman, 4, 1):           // Ground Smash: a stunning slam around
            for (Unit* u : Enemies(player, player, 10.f))
            {
                Strike(player, u, kGroundSmashPct);
                TimedAura(u, kAuraStun, 2000, player);
            }
            break;
        case OptionId(kShaman, 6, 0):           // Earthquake: at the target, or ahead
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            Unit* t = EnemyTarget(player, spell, 40.f);
            z.at = t ? Pos(t) : Ahead(player, 15.f);
            z.radius = 15.f;
            z.startMs = now;
            z.untilMs = now + 10000;
            s.zones.push_back(z);
            break;
        }
        case OptionId(kShaman, 6, 1):           // Avatar of Stone: grown, +50% health, 30% less damage, 20 s
            AddEffect(player, s, o.id, 20000);
            AddEffect(player, s, kFxRemoveAura, 20000, info->Id, float(info->Id));
            if (!HasEffect(s, kFxScale, now))
            {
                AddEffect(player, s, kFxScale, 20000, 0, player->GetObjectScale());
                player->SetObjectScale(player->GetObjectScale() * 1.3f);
            }
            BonusHealth(player, 0.5f, 20000);
            AddEffect(player, s, kFxBonusHealth, 20000);
            break;
        case OptionId(kShaman, 6, 2):           // Ancestral Circle, 15 s: totems doubled, the downed stand
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            z.at = Pos(player);
            z.radius = 20.f;
            z.startMs = now;
            z.untilMs = now + 15000;
            s.zones.push_back(z);
            for (Player* m : Party(player, 20.f))
                if (sFusionMgr.IsDowned(m))
                    sFusionMgr.ReviveDownedBy(m, player->GetObjectGuid(), 0.3f);
            break;
        }

        // ── Mage ──
        case OptionId(kMage, 1, 1):             // Flame Blade, 60 s
            AddEffect(player, s, o.id, 60000);
            break;
        case OptionId(kMage, 2, 1):             // Blade Blink: to the enemy, a strike
            if (Unit* t = EnemyTarget(player, spell, 20.f))
            {
                float x, y, z;
                t->GetNearPoint(player, x, y, z, player->GetObjectBoundingRadius(), 1.f, t->GetAngle(player));
                player->NearTeleportTo(x, y, z, player->GetAngle(t));
                Strike(player, t, kBladeBlinkPct);
            }
            break;
        case OptionId(kMage, 2, 2):             // Healing Glyph, 15 s
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            z.at = Pos(player);
            z.radius = 6.f;
            z.startMs = now;
            z.untilMs = now + 15000;
            s.zones.push_back(z);
            AddConstruct(player, s, o.id, z.untilMs);
            break;
        }
        case OptionId(kMage, 3, 1):             // Frost Nova: the Spellsword's holds longer
            s.auraFixes.push_back({0, kSpellFrostNova, kFrostNovaExtraMs, 0, true, now + 1500});
            break;
        case OptionId(kMage, 4, 0):             // Blast Wave: thrown back 8 yd
            for (Unit* u : Enemies(player, player, 10.f))
                Shove(u, player->GetAngle(u), 8.f, 6.f);
            break;
        case OptionId(kMage, 4, 1):             // Mana Shield: 25% of the mage's health, 15 s
            AddEffect(player, s, kFxAbsorb, 15000, me ^ o.id, player->GetMaxHealth() * 0.25f);
            break;
        case OptionId(kMage, 4, 2):             // Conjure Supplies: Arcane Stims, up to 4 carried
        {
            uint32 const have = player->GetItemCount(kArcaneStim);
            if (have < kArcaneStimsMax && player->AddItem(kArcaneStim, kArcaneStimsMax - have))
                ChatHandler(player).PSendSysMessage("|cff80c0ffArcane Stims conjured: %u. Trade them to the squad.|r",
                    kArcaneStimsMax);
            else if (have >= kArcaneStimsMax)
                ChatHandler(player).SendSysMessage("You already carry four Arcane Stims.");
            break;
        }
        case OptionId(kMage, 5, 0):             // Combustion: the next three spells crit and weave
            AddEffect(player, s, kFxCombustion, 60000, 0, 3.f);
            ChatHandler(player).SendSysMessage("|cffff8040Combustion: your next three spells.|r");
            break;
        case OptionId(kMage, 3, 2):             // Blink Pads: here and at the target (or 20 yd on)
        {
            for (FusionClassZone& old : s.zones)
                if (old.code == o.id)
                    old.untilMs = now;
            FusionClassZone a;
            a.code = o.id;
            a.mapId = player->GetMapId();
            a.at = Pos(player);
            a.radius = 1.5f;
            a.startMs = now;
            a.untilMs = now + 45000;
            FusionClassZone b = a;
            b.variant = 1;
            Unit* t = Selected(player);
            b.at = t && player->IsWithinDistInMap(t, 40.f) ? Pos(t) : Ahead(player, 20.f);
            // The caster stands on the first: it does not go at once.
            a.waits.push_back({me, now + 2000});
            s.zones.push_back(a);
            s.zones.push_back(b);
            AddConstruct(player, s, o.id, a.untilMs);
            break;
        }
        case OptionId(kMage, 5, 2):             // Arcane Familiar: bound to an ally, 30 s
        {
            Player* a = AllyTarget(player, 30.f);
            if (Creature* c = RaiseMinion(player, s, o.id, kWisp, Pos(a), 30000, 0.5f))
            {
                c->GetMotionMaster()->MoveFollow(a, 1.5f, kPi);
                AddEffect(a, StateOf(a), kFxFamiliar, 30000, c->GetObjectGuid().GetRawValue());
                AddConstruct(player, s, o.id, c->GetObjectGuid().GetRawValue());
            }
            break;
        }
        case OptionId(kMage, 6, 0):             // Meteor: falls 1.5 s later at the target or ahead
        {
            FusionClassZone z;
            z.code = o.id;
            z.mapId = player->GetMapId();
            Unit* t = EnemyTarget(player, spell, 40.f);
            z.at = t ? Pos(t) : Ahead(player, 20.f);
            z.radius = 8.f;
            z.startMs = now + 1500;
            z.untilMs = z.startMs + 6500;
            s.zones.push_back(z);
            break;
        }
        case OptionId(kMage, 6, 1):             // Arcane Avatar, 20 s
            AddEffect(player, s, o.id, 20000);
            break;
        case OptionId(kMage, 6, 2):             // Grand Array, 30 s
            AddEffect(player, s, o.id, 30000);
            break;

        // ── Warlock ──
        case OptionId(kWarlock, 0, 1):          // Life Tap: also a soul shard
            if (player->GetItemCount(6265) < 5)
                player->AddItem(6265, 1);
            break;
        case OptionId(kWarlock, 0, 2):          // Soulstone: on the crosshair's ally (or oneself), 15 min
        {
            Player* a = AllyTarget(player, 30.f);
            // One stone a warlock: the last one goes.
            if (s.soulstoneOn && s.soulstoneOn != a->GetObjectGuid().GetRawValue())
                if (Player* old = ObjectAccessor::FindPlayer(ObjectGuid(s.soulstoneOn)); old && old->HasFusion())
                {
                    FusionClassState& os = StateOf(old);
                    if (os.soulstoneBy == me)
                    {
                        os.soulstoneBy = 0;
                        old->RemoveAurasDueToSpell(kAuraSoulstone);
                    }
                }
            FusionClassState& as = StateOf(a);
            as.soulstoneBy = me;
            as.soulstoneUntilMs = now + kSoulstoneMs;
            s.soulstoneOn = a->GetObjectGuid().GetRawValue();
            TimedAura(a, kAuraSoulstone, kSoulstoneMs, player);
            ChatHandler(a).PSendSysMessage("|cffb060ff%s's soulstone: if you go down, you stand back up.|r", player->GetName());
            break;
        }
        case OptionId(kWarlock, 1, 1):          // Fel Hide, 20 s
            AddEffect(player, s, o.id, 20000);
            break;
        case OptionId(kWarlock, 2, 0):          // Curse of Exhaustion: no sprint 12 s
            if (Unit* t = EnemyTarget(player, spell, 30.f))
                if (Player* e = t->ToPlayer(); e && e->HasFusion())
                    AddEffect(e, StateOf(e), kFxNoSprint, 12000, me);
            break;
        case OptionId(kWarlock, 3, 1):          // Hellfire: 10 s, on the move
            AddEffect(player, s, kFxHellfire, 10000);
            break;
        case OptionId(kWarlock, 6, 0):          // Curse of Doom: 60 %EH after 10 s, unless purged
            if (Unit* t = spell->m_targets.getUnitTarget(); t && t != player)
                AddEffect(player, s, o.id, 10000, t->GetObjectGuid().GetRawValue(), float(info->Id));
            break;
        case OptionId(kWarlock, 1, 2):          // Raise Skeleton: a corpse rises, 60 s
        case OptionId(kWarlock, 4, 2):          // Raise Ghoul
        {
            bool const ghoul = o.id == OptionId(kWarlock, 4, 2);
            Creature* corpse = NearestCorpse(player, 30.f);
            if (!corpse)
            {
                ChatHandler(player).SendSysMessage("|cffff6060No corpse within 30 yards.|r");
                break;
            }
            uint32 count = 0;
            for (FusionClassMinion const& m : s.minions)
                if (m.code == OptionId(kWarlock, 1, 2) || m.code == OptionId(kWarlock, 4, 2))
                    ++count;
            if (count >= (ghoul ? 5u : 3u))
            {
                ChatHandler(player).SendSysMessage("|cffff6060You have as many minions as you can hold.|r");
                break;
            }
            if (Meter* lf = MeterOf(s, Resource::LifeForce))
                lf->Spend(ghoul ? 30 : 20), s.metersDirty = true;
            Fusion::Vec3 const at = Pos(corpse);
            corpse->RemoveCorpse();
            RaiseMinion(player, s, o.id, ghoul ? kGhoul : kSkeleton, at, 60000, ghoul ? 0.4f : 0.25f);
            break;
        }
        case OptionId(kWarlock, 2, 1):          // Fel Leap: 25 yd on, a blast where it lands
            LeapTo(player, player->GetOrientation(), 25.f, 9.f);
            AddEffect(player, s, kFxLanding, 950);
            break;
        case OptionId(kWarlock, 2, 2):          // Corpse Explosion: a corpse or a minion bursts
        {
            Fusion::Vec3 at;
            bool found = false;
            if (Creature* corpse = NearestCorpse(player, 30.f))
            {
                at = Pos(corpse);
                corpse->RemoveCorpse();
                found = true;
            }
            else
                for (FusionClassMinion& m : s.minions)
                    if (Creature* c = player->GetMap()->GetCreature(ObjectGuid(m.guid)); c && c->IsAlive())
                    {
                        at = Pos(c);
                        c->DespawnOrUnsummon();
                        found = true;
                        break;
                    }
            if (!found)
            {
                ChatHandler(player).SendSysMessage("|cffff6060Nothing dead within 30 yards to burst.|r");
                break;
            }
            if (Meter* lf = MeterOf(s, Resource::LifeForce))
                lf->Spend(15), s.metersDirty = true;
            for (Unit* u : EnemiesAt(player, at, 8.f))
                Strike(player, u, 20.f);
            break;
        }
        case OptionId(kWarlock, 3, 2):          // Bone Armor: 25% + 5% a minion, 20 s
        {
            if (Meter* lf = MeterOf(s, Resource::LifeForce))
                lf->Spend(20), s.metersDirty = true;
            float const share = 0.25f + 0.05f * float(s.minions.size());
            AddEffect(player, s, kFxAbsorb, 20000, me ^ o.id, player->GetMaxHealth() * share);
            break;
        }
        case OptionId(kWarlock, 6, 1):          // Demon Form: 30 s, +50% health, claws
            AddEffect(player, s, o.id, 30000);
            AddEffect(player, s, kFxRemoveAura, 30000, info->Id, float(info->Id));
            BonusHealth(player, 0.5f, 30000);
            AddEffect(player, s, kFxBonusHealth, 30000);
            break;
        case OptionId(kWarlock, 6, 2):          // Abomination, 45 s
            if (Meter* lf = MeterOf(s, Resource::LifeForce))
                lf->Spend(80), s.metersDirty = true;
            RaiseMinion(player, s, o.id, kAbomination, Ahead(player, 3.f), 45000, 1.0f);
            break;

        // ── Druid ──
        case OptionId(kDruid, 1, 0):            // Cat: Wild Momentum, +30% speed 6 s and the next blow crits
            if (info->Id == 768)
            {
                AddEffect(player, s, kFxSpeed, 6000, me ^ o.id, 1.3f);
                AddEffect(player, s, kFxMomentumCrit, 6000);
            }
            else if (sSpellMgr.GetFirstSpellInChain(info->Id) == kSpellPounce)     // Pounce: a 3 s stun
                if (Unit* t = spell->m_targets.getUnitTarget(); t && t != player)
                    s.auraFixes.push_back({t->GetObjectGuid().GetRawValue(), kSpellPounce, 3000, 3000, false, now + 2000});
            break;
        case OptionId(kDruid, 2, 0):            // Bear: momentum, an absorb of 15% for 6 s
        {
            uint32 const first = sSpellMgr.GetFirstSpellInChain(info->Id);
            if (first == sSpellMgr.GetFirstSpellInChain(9634) || info->Id == 5487 || info->Id == 9634)
                AddEffect(player, s, kFxAbsorb, 6000, me ^ o.id, player->GetMaxHealth() * 0.15f);
            else if (first == kSpellMaul)                   // Maul: 12 %EH at once
            {
                if (Unit* t = EnemyTarget(player, spell, 6.f))
                    Strike(player, t, 12.f);
            }
            else if (first == kSpellBash)                   // Bash: 4 s, 3 s on a player
                if (Unit* t = spell->m_targets.getUnitTarget(); t && t != player)
                    s.auraFixes.push_back({t->GetObjectGuid().GetRawValue(), kSpellBash, 3000, 4000, false, now + 2000});
            break;
        }
        case OptionId(kDruid, 2, 2):            // Lunar Strike: a moonlit blow, 12 %EH and 5 yd back
            if (Unit* t = EnemyTarget(player, spell, 6.f))
            {
                Strike(player, t, 12.f);
                Shove(t, player->GetAngle(t), 5.f, 4.f);
            }
            break;
        case OptionId(kDruid, 3, 0):            // Stag Form (its own shapeshift, DruidForms): momentum
            if (info->Id == 21936)
                AddEffect(player, s, kFxSpeed, 4000, me ^ o.id, 1.1f);
            // The charge is the worgen's Rending Leap too: 10 %EH where it lands.
            else if (info->Id == kSpellFeralCharge
                && player->GetShapeshiftForm() == ShapeshiftForm(Fusion::DruidForms::kWorgen))
                if (Unit* t = EnemyTarget(player, spell, 30.f))
                    Strike(player, t, 10.f);
            break;
        case OptionId(kDruid, 6, 0):            // Worgen Form: momentum, 6 s immune to stun and fear
            if (info->Id == 7621)
                AddEffect(player, s, o.id, 6000);
            break;
        case OptionId(kDruid, 3, 2):            // Faerie Fire: the target outlined for 40 s
            if (Unit* t = EnemyTarget(player, spell, 40.f))
                Spot(player, {t}, 40000);
            break;
        // Crow Form glides by its own slow fall, which leaves with the form (DruidForms), and the
        // Worgen's lifesteal holds while the form does (AdjustDealt): neither needs a timer here.
        case OptionId(kDruid, 6, 1):            // Tree of Life: 30 s in a treant's look
            // Tree Form itself is a shapeshift with a stun (an unused vanilla spell): replaced,
            // so the druid keeps casting; only the look is borrowed.
            AddEffect(player, s, o.id, 30000);
            player->SetDisplayId(kTreantDisplay);
            AddEffect(player, s, kFxDemorph, 30000);
            break;
        case OptionId(kDruid, 6, 2):            // Starfall: 10 s, and Eclipse at once
            AddEffect(player, s, o.id, 10000);
            AddEffect(player, s, kFxEclipse, InKitOf(player, s, OptionId(kDruid, 5, 2)) ? 15000 : 10000);
            break;
        default:
            break;
    }
}
