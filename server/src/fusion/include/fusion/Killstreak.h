/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_KILLSTREAK_H
#define FUSION_KILLSTREAK_H

#include "fusion/Common.h"

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Fusion
{
    // What a kill was worth toward a streak (MW2 design spec 4.1).
    enum class KillKind : uint8_t
    {
        Player,
        Creature,       // a normal creature worth XP
        EliteCreature,  // elite, rare elite or boss: shared by everyone who damaged it
        GrayCreature,   // gray to the killer, or a critter: worth nothing
    };

    // A kill's worth in tenths of a streak kill (MW2 design spec 4.1): an enemy player 10, a
    // creature worth XP 1, an elite 10 / N (to the nearest tenth, at least 1) to each of the N
    // players who damaged it. WOMW plays killstreaks as PvP only: the server credits players
    // through PlayerKillTenths and creatures earn nothing toward a streak.
    uint32_t StreakTenths(KillKind k, uint32_t damagers = 1);

    // An enemy player's worth to a streak, by level: a whole kill for a victim at or above the
    // killer's level, a tenth less for each level below, never under a tenth (so a high level
    // farming low ones gains little, and nobody's kill is worth nothing).
    uint32_t PlayerKillTenths(uint32_t killerLevel, uint32_t victimLevel);

    struct StreakReward
    {
        uint32_t id = 0;
        uint32_t kills = 0;     // the streak that earns it
        bool aerial = false;    // counts against the airspace limit
    };

    // MW2's killstreak rewards, by id (MW2 design spec 4.2 order); the id goes to the client in
    // the StreakEarned event.
    namespace StreakId
    {
        constexpr uint32_t Uav = 1, CarePackage = 2, CounterUav = 3, SentryGun = 4,
            PredatorMissile = 5, PrecisionAirstrike = 6, HarrierStrike = 7, AttackHelicopter = 8,
            EmergencyAirdrop = 9, PaveLow = 10, StealthBomber = 11, ChopperGunner = 12,
            Ac130 = 13, Emp = 14, TacticalNuke = 15;
    }

    // The ladder every player has until Create-a-Class picks three: the client HUD's.
    std::vector<StreakReward> DefaultStreakLadder();

    // A player's streak: kills since the last death, earned rewards waiting to be called in.
    class Streak
    {
    public:
        // The player's chosen rewards (three, from the Armory), and whether Hardline is on.
        void SetLoadout(std::vector<StreakReward> rewards, bool hardline);

        // Count a kill (for an elite, among `damagers` players); returns the rewards it earned.
        std::vector<StreakReward> AddKill(KillKind k, uint32_t damagers = 1);
        // Count a kill worth `tenths` (PlayerKillTenths); returns the rewards it earned.
        std::vector<StreakReward> AddTenths(uint32_t tenths);
        // Death, or logging out, ends the streak; earned-but-unused rewards are kept until used.
        void Reset()
        {
            m_tenths = 0;
            m_paid.clear();
        }
        // The streak in tenths of a kill, and in whole kills.
        uint32_t Tenths() const { return m_tenths; }
        uint32_t Count() const { return m_tenths / 10; }
        std::vector<StreakReward> const& Earned() const { return m_earned; }
        // Spend an earned reward; false if it isn't held.
        bool Use(uint32_t rewardId);
        // Hand a reward over without the kills (a GM's `.streak`, a call-in the world refused).
        void Grant(StreakReward const& r) { m_earned.push_back(r); }

    private:
        std::vector<StreakReward> m_rewards;
        std::vector<StreakReward> m_earned;
        uint32_t m_tenths = 0;
        bool m_hardline = false;
        // The rewards this life has paid: a ladder change mid-streak (Hardline swapped) must
        // not pay one twice.
        std::vector<uint32_t> m_paid;
    };

    // Who damaged each victim, for splitting an elite's streak value: every player who dealt it
    // damage, once each. A victim left alone for kForgetMs (it reset or evaded) is forgotten.
    class DamageCredit
    {
    public:
        static constexpr uint32_t kForgetMs = 5 * 60 * 1000;

        void Add(uint64_t victim, uint64_t attacker, uint32_t nowMs);
        // The victim's damagers, forgetting it.
        std::vector<uint64_t> Take(uint64_t victim);
        void Expire(uint32_t nowMs);
        size_t Tracked() const { return m_victims.size(); }

    private:
        struct Entry
        {
            std::vector<uint64_t> attackers;
            uint32_t lastMs = 0;
        };
        std::unordered_map<uint64_t, Entry> m_victims;
    };

    // The airspace limit: at most a few aerial streaks over any 200-yard area at once, as MW2
    // allows one helicopter at a time; extras wait ("Airspace too crowded").
    class Airspace
    {
    public:
        static constexpr float kCellSize = 200.f;
        static constexpr uint32_t kMaxPerCell = 2;

        struct Cell
        {
            int32_t x = 0, y = 0;
            uint32_t map = 0;
            bool operator<(Cell const& o) const
            {
                return map != o.map ? map < o.map : (x != o.x ? x < o.x : y < o.y);
            }
        };

        static Cell CellAt(uint32_t map, float x, float y);

        // Try to launch; on success returns a ticket to release when the streak ends.
        std::optional<uint64_t> TryLaunch(uint32_t map, float x, float y);
        void Release(uint64_t ticket);
        uint32_t ActiveIn(uint32_t map, float x, float y) const;

    private:
        std::map<Cell, std::vector<uint64_t>> m_active;
        std::map<uint64_t, Cell> m_tickets;
        uint64_t m_next = 1;
    };
}

#endif
