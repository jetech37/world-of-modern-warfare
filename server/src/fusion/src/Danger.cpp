/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Danger.h"

namespace Fusion { namespace Danger
{
    namespace
    {
        // The share of the extra the level keeps: easeStart at level 1, all of it at easeFullLevel.
        float Ease(uint32_t level, Config const& c)
        {
            if (c.easeFullLevel <= 1)
                return 1.f;
            float const t = Clamp(float(int32_t(std::max<uint32_t>(1, level)) - 1) / float(c.easeFullLevel - 1), 0.f, 1.f);
            return Lerp(c.easeStart, 1.f, t);
        }

        MapOverride const* OverrideOf(uint32_t map, Config const& c)
        {
            for (MapOverride const& o : c.maps)
                if (o.map == map)
                    return &o;
            return nullptr;
        }

        // 1 plus the extra of `m`, eased, knobbed and shared by the map.
        float Apply(float m, float ease, float knob, float share)
        {
            return std::max(0.f, 1.f + (m - 1.f) * ease * std::max(0.f, knob) * std::max(0.f, share));
        }
    }

    Config const& Defaults()
    {
        static Config const c;
        return c;
    }

    Kind KindOf(Scale::CreatureFacts const& f)
    {
        bool const boss = f.boss || f.rank == CreatureRank::WorldBoss || f.rank == CreatureRank::DungeonBoss;
        if (boss)
        {
            if (f.place == Scale::Place::Raid)
                return Kind::RaidBoss;
            if (f.place == Scale::Place::Dungeon || f.rank == CreatureRank::DungeonBoss)
                return Kind::DungeonBoss;
            return Kind::WorldBoss;
        }
        switch (f.rank)
        {
            case CreatureRank::Rare:        return Kind::Rare;
            case CreatureRank::Elite:       return Kind::Elite;
            case CreatureRank::RareElite:   return Kind::RareElite;
            default:                        return Kind::Normal;
        }
    }

    Multipliers For(Scale::CreatureFacts const& f, uint32_t map, Config const& c)
    {
        Kind const kind = KindOf(f);
        Multipliers m = c.kinds[size_t(kind)];
        bool const boss = kind == Kind::DungeonBoss || kind == Kind::RaidBoss || kind == Kind::WorldBoss;
        if (!boss && f.place != Scale::Place::World)
        {
            Multipliers const& t = f.place == Scale::Place::Raid ? c.raidTrash : c.dungeonTrash;
            m.health *= t.health;
            m.damage *= t.damage;
            m.gunDamage *= t.gunDamage;
        }
        float const ease = Ease(f.level, c);
        MapOverride const* o = OverrideOf(map, c);
        Multipliers out;
        out.health = Apply(m.health, ease, c.healthKnob, o ? o->health : 1.f);
        out.damage = Apply(m.damage, ease, c.damageKnob, o ? o->damage : 1.f);
        out.gunDamage = Apply(m.gunDamage, ease, c.damageKnob, o ? o->damage : 1.f);
        return out;
    }

    float HealthMultiplier(Scale::CreatureFacts const& f, uint32_t map, Config const& c) { return For(f, map, c).health; }
    float DamageMultiplier(Scale::CreatureFacts const& f, uint32_t map, Config const& c) { return For(f, map, c).damage; }
    float GunDamageMultiplier(Scale::CreatureFacts const& f, uint32_t map, Config const& c) { return For(f, map, c).gunDamage; }

    float CreatureMaxHealth(Scale::CreatureFacts const& f, uint32_t map, Config const& c)
    {
        return Scale::CreatureMaxHealth(f) * HealthMultiplier(f, map, c);
    }
}}
