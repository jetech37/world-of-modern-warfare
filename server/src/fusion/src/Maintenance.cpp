/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Maintenance.h"

#include <algorithm>
#include <cmath>

namespace Fusion { namespace Maintenance
{
    int64_t DecayingSeconds(int64_t from, int64_t to, int64_t keptUntil)
    {
        int64_t const start = std::max(from, keptUntil);
        return to > start ? to - start : 0;
    }

    float DecayLoss(float maxHealth, int64_t from, int64_t to, int64_t keptUntil)
    {
        int64_t const secs = DecayingSeconds(from, to, keptUntil);
        // In doubles: a gap of years is no trouble, and an hour's loss isn't rounded away.
        return float(double(maxHealth) * double(kDecayPerDay) * double(secs) / double(kDay));
    }

    Decayed Decay(float health, float maxHealth, int64_t decayedAt, int64_t now, int64_t keptUntil)
    {
        Decayed d;
        d.health = health;
        d.decayedAt = decayedAt;
        if (decayedAt <= 0)
        {
            d.decayedAt = now;
            return d;
        }
        if (now <= decayedAt)
            return d;
        float const loss = DecayLoss(maxHealth, decayedAt, now, keptUntil);
        d.decayedAt = now;
        if (loss > 0.f)
        {
            d.health = health - loss;
            d.changed = true;
        }
        d.collapsed = d.health <= 0.f;
        if (d.collapsed)
            d.health = 0.f;
        return d;
    }

    float DaysLeft(float health, float maxHealth)
    {
        if (maxHealth <= 0.f)
            return 0.f;
        return std::max(0.f, health) / (maxHealth * kDecayPerDay);
    }

    bool Repairable(Build::PieceType t)
    {
        return Construction::RecipeFor(t) != nullptr;
    }

    std::vector<Owed> RepairBasis(Build::PieceType t, uint8_t reinforcement)
    {
        std::vector<Owed> out;
        Construction::Recipe const* r = Construction::RecipeFor(t);
        if (!r)
            return out;
        auto add = [&](Material const& m) {
            for (Owed& o : out)
                if (o.item == m.item)
                {
                    o.count += float(m.count) * kRepairShare;
                    return;
                }
            out.push_back({m.item, float(m.count) * kRepairShare});
        };
        for (Material const& m : r->materials)
            add(m);
        if (reinforcement > 0 && Construction::Reinforceable(t))
            for (Material const& m : Construction::ReinforcementSpent(reinforcement))
                add(m);
        return out;
    }

    RepairStep PlanRepair(Build::Piece const& p, Credit const& credit, Inventory const& bags, bool free, int64_t now,
        int64_t damagedAt)
    {
        RepairStep s;
        s.credit = credit;
        if (!Repairable(p.type))
            return s;
        float const maxHealth = Build::MaxHealth(p);
        // Within half a point of whole is whole (the wire shows health per mille).
        if (maxHealth <= 0.f || p.health >= maxHealth - 0.5f)
        {
            s.check = RepairCheck::NotDamaged;
            return s;
        }
        if (damagedAt > 0 && now >= damagedAt && now - damagedAt < kRepairAfterDamage)
        {
            s.check = RepairCheck::RecentlyDamaged;
            return s;
        }
        s.gain = std::min(kRepairTick * maxHealth, maxHealth - std::max(0.f, p.health));
        if (free)
        {
            s.check = RepairCheck::Ok;
            s.payment.ok = true;
            return s;
        }
        float const fraction = s.gain / maxHealth;
        Credit next;
        next.piece = p.id;
        if (credit.piece == p.id)
            next.prepaid = credit.prepaid;
        for (Owed const& o : RepairBasis(p.type, p.reinforcement))
        {
            float const want = o.count * fraction;
            float& prepaid = next.prepaid[o.item];
            float const need = want - prepaid;
            if (need <= 1e-4f)
            {
                prepaid = -need;
                continue;
            }
            uint32_t const take = uint32_t(std::ceil(need - 1e-4f));
            prepaid = float(take) - need;
            if (prepaid < 0.f)
                prepaid = 0.f;
            s.cost.push_back({o.item, take});
        }
        s.payment = Construction::PlanPayment(s.cost, bags, false);
        if (!s.payment.ok)
        {
            s.check = RepairCheck::NoMaterials;
            return s;
        }
        s.check = RepairCheck::Ok;
        s.credit = next;
        return s;
    }
}}
