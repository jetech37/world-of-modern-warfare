/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_COMPANION_REST_H
#define FUSION_COMPANION_REST_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// How each companion went back into its item, and whether it fainted.
//
//  - Wounds: a companion put away hurt comes back as hurt as it went, healed for the time it
//    rested (whole again in a minute), whichever other companion was out in between, so swapping
//    between two companions is no free heal. Its pet bar's cooldowns are kept the same way
//    (RestBook, in memory: they are short).
//  - Fainting (the monster-tamer rule): a companion beaten to 0 health FAINTS. It stays fainted,
//    on its own (the owner's other companions are not touched), until a stable master revives it
//    (whole, for a small fee), a Reviving Draught does (at half health), or the long wait is over
//    (an hour by default, Companions.FaintRecoverMinutes; Loyal a third of it), after which it
//    comes round whole. A fainted companion cannot be summoned or ridden.
//
// Both live in a Recovery per companion item, kept by the server in
// characters.character_fusion_companion_rest (by item guid, made at startup): they outlive a
// logout and a restart and follow the item through a trade. Times are unix seconds, so the wait
// runs on while its owner is offline.
namespace Fusion::Companions
{
    // Put away hurt, a companion heals back to full in this long.
    constexpr uint32_t kRestHealMs = 60 * 1000;

    // ── Fainting ────────────────────────────────────────────────────────────────────────────
    // The defaults of mangosd.conf's Companions.FaintRecoverMinutes and
    // Companions.StableReviveCostPerLevel.
    constexpr uint32_t kFaintRecoverMinutes = 60;
    constexpr uint32_t kStableReviveCostPerLevel = 10;     // copper per level of each fainted one
    // The Reviving Draught (sql/custom/azeroth_warfare/24_companion_revive.sql): one revives one
    // fainted companion at half health. Sold by general goods vendors and stable masters.
    constexpr uint32_t kReviveDraughtItem = 93470;
    constexpr uint32_t kReviveDraughtPrice = 1500;         // copper (15 silver), the item's buy price
    constexpr float kDraughtReviveHealth = 0.5f;

    // The realm's settings, from mangosd.conf: the wait clamped to 1 minute .. 7 days, the fee
    // per level to 0 .. 10 gold.
    struct FaintSettings
    {
        uint32_t recoverSeconds = kFaintRecoverMinutes * 60;
        uint32_t costPerLevel = kStableReviveCostPerLevel;
    };
    FaintSettings FaintSettingsFrom(int32_t recoverMinutes, int32_t costPerLevel);

    struct Recovery
    {
        float health = 1.f;         // its health share when it went back, 0..1 (its wounds)
        uint32_t awayAt = 0;        // when it went back (unix seconds)
        uint32_t faintedAt = 0;     // when it fainted (unix seconds); 0: it has not
    };

    // Nothing to remember: whole and not fainted (the server keeps no row for it).
    bool IsWhole(Recovery const& r);
    // Seconds until it comes round on its own; 0 when it is not fainted (or the wait is over).
    // A clock that went back keeps the full wait, never an early recovery.
    uint32_t FaintSecondsLeft(Recovery const& r, uint32_t now, uint32_t recoverSeconds);
    bool IsFainted(Recovery const& r, uint32_t now, uint32_t recoverSeconds);
    // It fainted at `now`: health 0, its wait starts.
    void Faint(Recovery& r, uint32_t now);
    // Revived at `now` with this health share (a stable master 1, a draught kDraughtReviveHealth).
    void Revive(Recovery& r, uint32_t now, float health);
    // Its long wait is over: it comes round whole. True when it just did (to say so once);
    // false when it was not fainted or is still waiting.
    bool Settle(Recovery& r, uint32_t now, uint32_t recoverSeconds);
    // It went back into its item (put away, a swap, a logout) at this health share. A fainted
    // one stays fainted.
    void PutAway(Recovery& r, float health, uint32_t now);
    // Its health share when called at `now`: as it went, healed for the time since (whole in
    // `healMs`); 0 while fainted.
    float HealthAt(Recovery const& r, uint32_t now, uint32_t recoverSeconds, uint32_t healMs = kRestHealMs);
    // A stable master's fee to revive a fainted companion of this level.
    uint32_t StableReviveCost(uint8_t level, uint32_t costPerLevel);
    // "52 min", "1 h 5 min", "40 s": how long a wait still is.
    std::string WaitText(uint32_t seconds);

    // The row as the characters database keeps it: health in thousandths.
    struct RecoveryRow
    {
        uint16_t healthPermille = 1000;
        uint32_t awayAt = 0;
        uint32_t faintedAt = 0;
    };
    RecoveryRow ToRow(Recovery const& r);
    Recovery FromRow(RecoveryRow const& row);

    // ── Wounds and cooldowns while swapping (in memory) ─────────────────────────────────────
    struct Rest
    {
        float health = 1.f;                     // its health share when it went back, 0..1
        uint32_t awayMs = 0;                    // when (the server's millisecond clock)
        std::array<uint32_t, 10> readyAtMs{};   // each pet bar slot's cooldown end
    };

    // A share of health after resting `restedMs`: back to full in `healMs`.
    float RestedHealth(float health, uint32_t restedMs, uint32_t healMs = kRestHealMs);

    class RestBook
    {
    public:
        // The most companions remembered; the longest-rested is forgotten first (by then it has
        // healed and its cooldowns have run out anyway).
        static constexpr size_t kMax = 16;

        // Remember how this item's companion went back (replacing what was there).
        void Put(uint64_t item, Rest const& r);
        // How it comes back at `nowMs`: healed for the time since, with the cooldowns it still has.
        // One never put away comes back whole with no cooldowns.
        Rest Take(uint64_t item, uint32_t nowMs) const;
        void Forget(uint64_t item);
        size_t Size() const { return m_entries.size(); }

    private:
        std::vector<std::pair<uint64_t, Rest>> m_entries;   // oldest first
    };
}

#endif
