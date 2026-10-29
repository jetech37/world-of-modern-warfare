/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_MAINTENANCE_H
#define FUSION_MAINTENANCE_H

#include "fusion/Build.h"
#include "fusion/Construction.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

// Upkeep of built pieces (WOMW): a piece wears down with time and is kept up with a hammer.
//
// Decay. Out in the world every piece loses kDecayPerDay (2%) of its full health a day, so one
// left alone falls in 50 days; nothing about it changes but its health, and at 0 it collapses as a
// broken piece does. On a bought plot whose upkeep is paid (Plots.h), nothing decays: the upkeep
// is the maintenance. An overdue plot's pieces decay as the open world's do (and a lapsed plot is
// packed up anyway). Decay is worked out lazily: a piece keeps its health and the time it was true
// at (`decayedAt`), and what time has taken since is applied whenever the server looks (at a
// sweep, a hit, a repair), over any gap, the server's own downtime included.
//
// Repair. The Repair Hammer (Gathering.h) mends a damaged piece a step a strike: kRepairTick of
// its full health, paid with kRepairShare of what went into it (its recipe and its reinforcement)
// in proportion, fresh or salvaged goods alike (salvage first). Whole items are taken; what a
// strike takes over its share is credited to the next strike on the same piece. A piece hit
// within kRepairAfterDamage can't be mended (no hammering a wall back up under fire). Repairs
// teach no Construction: the skill comes from building with fresh materials and cutting lumber,
// and a repair would otherwise be a skill-up for damaging one's own wall and mending it.
namespace Fusion { namespace Maintenance
{
    using Construction::Inventory;
    using Construction::Material;

    constexpr int64_t kDay = 86400;
    constexpr float kDecayPerDay = 0.02f;
    // How often the server brings a map's pieces up to date (their health only moves 0.08% an
    // hour, so more often buys nothing).
    constexpr int64_t kDecaySweepSeconds = 3600;

    // The seconds of [from, to) a piece decays: none of it up to `keptUntil` (a plot's paid
    // upkeep; 0 or less in the open world), none when the clock went back.
    int64_t DecayingSeconds(int64_t from, int64_t to, int64_t keptUntil);
    // The health a piece of `maxHealth` loses over [from, to).
    float DecayLoss(float maxHealth, int64_t from, int64_t to, int64_t keptUntil);

    struct Decayed
    {
        float health = 0.f;
        int64_t decayedAt = 0;      // the time `health` is true at
        bool collapsed = false;     // nothing left: it falls
        bool changed = false;       // health moved
    };
    // A piece brought up to `now`. A piece from before decay (`decayedAt` 0) starts its clock now
    // and loses nothing; a clock set back takes nothing and keeps the old time.
    Decayed Decay(float health, float maxHealth, int64_t decayedAt, int64_t now, int64_t keptUntil);
    // Days left until a piece decaying in the open world collapses.
    float DaysLeft(float health, float maxHealth);

    // ── Repair ──
    constexpr float kRepairTick = 0.10f;
    constexpr float kRepairShare = 0.6f;
    constexpr int64_t kRepairAfterDamage = 30;
    // What mending a whole piece (0 to full) asks: kRepairShare of its recipe and of its
    // reinforcement, item by item (fractions kept: a strike pays a part of it).
    struct Owed
    {
        uint32_t item = 0;
        float count = 0.f;
    };
    std::vector<Owed> RepairBasis(Build::PieceType t, uint8_t reinforcement);
    bool Repairable(Build::PieceType t);

    // Paid ahead on one piece by the strikes before (whole items over a strike's share).
    struct Credit
    {
        uint32_t piece = 0;
        std::unordered_map<uint32_t, float> prepaid;
    };

    enum class RepairCheck : uint8_t { Ok, NotRepairable, NotDamaged, RecentlyDamaged, NoMaterials };
    struct RepairStep
    {
        RepairCheck check = RepairCheck::NotRepairable;
        float gain = 0.f;                   // health back
        std::vector<Material> cost;         // fresh items' counts to pay (salvage counts as fresh)
        Construction::Payment payment;      // the items to take from these bags
        Credit credit;                      // the credit after this strike
    };
    // One strike of the hammer on piece `p` at `now` (`damagedAt` when it was last hit, 0 never).
    // `free`: a game master or a free realm, who pay nothing. The credit is the striker's, kept
    // for one piece at a time (another piece starts it over).
    RepairStep PlanRepair(Build::Piece const& p, Credit const& credit, Inventory const& bags, bool free, int64_t now,
        int64_t damagedAt);
}}

#endif
