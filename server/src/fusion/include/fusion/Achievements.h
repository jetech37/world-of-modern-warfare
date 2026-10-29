/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_ACHIEVEMENTS_H
#define FUSION_ACHIEVEMENTS_H

#include "fusion/Catalog.h"
#include "fusion/Equipment.h"
#include "fusion/Killstreak.h"
#include "fusion/Loadout.h"
#include "fusion/Movement.h"
#include "fusion/Packets.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// Unlocks by achievement (replacing unlocks by level): a new account starts with the USP .45 and
// nothing else, and earns every gun, attachment, camo, perk, deathstreak, piece of equipment and
// killstreak by doing things in the world. The server counts what an account does (AccountStats),
// the book says what each achievement asks for and what it unlocks (AchievementBook), and the
// account's unlocks are derived from the achievements it has earned (UnlocksFor), never stored on
// their own, so retuning the book retunes every account. Integration notes: docs/ACHIEVEMENTS.md.
namespace Fusion
{
    // ── Counters ────────────────────────────────────────────────────────────────────────────

    // What the server counts per account. The values are stored in the database, so they never
    // change meaning: add new stats at the end. Each says what its key is; "any" is kAnyKey.
    enum class Stat : uint16_t
    {
        None = 0,
        PlayerKills = 1,            // any: enemy players killed (killstreaks unlock only from these)
        CreatureKills = 2,          // any: creatures killed that were worth XP
        EliteKills = 3,             // any: elites, rare elites and bosses
        RareKills = 4,              // any: rares and rare elites
        BossKills = 5,              // any: dungeon bosses and world bosses
        CreatureTypeKills = 6,      // TypeKey: kills of a creature type (players count as humanoids)
        ClassKills = 7,             // ClassKey: kills with a gun of that class
        ClassTypeKills = 8,         // ClassTypeKey: kills of a creature type with a gun class
        Headshots = 9,              // any: kills by a shot to the head
        ClassHeadshots = 10,        // ClassKey
        GunKills = 11,              // gun id
        GunHeadshots = 12,          // gun id
        AttachmentKills = 13,       // GunAttachmentKey: kills with that gun carrying that attachment
        AttachedKills = 14,         // any: kills with a gun carrying any attachment
        PerkKills = 15,             // perk id: kills with that perk in the class
        EquipmentKills = 16,        // EquipmentKey: kills with a lethal
        ExplosiveKills = 17,        // any: lethals that explode, launchers and underbarrel grenades
        StreakKills = 18,           // streak id: kills by a called-in killstreak reward
        MeleeKills = 19,            // any: the knife and the riot shield
        LongRangeKills = 20,        // any, or ClassKey: kills at kLongRangeKill or farther
        CloseRangeKills = 21,       // any, or TypeKey: kills at kCloseRangeKill or nearer
        CrouchedKills = 22,         // any: the killer crouched (or sliding)
        ProneKills = 23,            // any
        MultiKills = 24,            // any: kills that were the second or later of a burst
        BestMultiKill = 25,         // any, a maximum: the most kills in one burst
        RevengeKills = 26,          // any: killing the player who last killed you
        Deaths = 27,                // any
        PlayerDeaths = 28,          // any: deaths to enemy players
        Level = 29,                 // any, a maximum: the highest level a character reached
        ZonesExplored = 30,         // any: zones (AreaTable entries with an explore flag) discovered
        QuestsCompleted = 31,       // any
        DistanceSprinted = 32,      // any: yards
        CorpsesLooted = 33,         // any
        Reloads = 34,               // any
        ClassSwaps = 35,            // any: Create-a-Class classes equipped
        RevivesGiven = 36,          // any: downed allies revived
        TimesRevived = 37,          // any: getting back up after going down
        StreaksEarned = 38,         // any, or streak id: killstreak rewards earned
        StreaksCalled = 39,         // any, or streak id: killstreak rewards called in
        BestPlayerStreak = 40,      // any, a maximum: enemy players killed in one life
        TacticalVictims = 41,       // EquipmentKey: enemies caught by a flash, stun or smoke
        BuildingsBuilt = 42,        // any: building pieces put down (Build.h)
        CompanionsEvolved = 43,     // any
        StreaksDowned = 44,         // any, or streak id: enemy killstreak craft shot down
        BountiesCompleted = 45,     // any: bounty board bounties turned in (Bounties.h)
        CompanionBattlesWon = 46,   // any: companion battles won (CompanionBattles.h), tamers and players
        TamersDefeated = 47,        // any, or a tamer's id: companion tamers beaten (CompanionBattles::Tamers)
        // ── Wrath-style criteria (design/GAME_DESIGN.md section 14, "Achievements") ──
        Achievement = 48,           // achievement id: 1 once earned (meta-achievements); never stored
        AchievementPoints = 49,     // any: the points of every achievement earned; never stored
        ProfessionSkill = 50,       // skill line (SkillLine.dbc, or the layer's 9001-9004), a maximum
        ItemsCrafted = 51,          // any, or a skill line: things made at a trade window
        GlyphsInscribed = 52,       // any: glyphs inscribed into kits (Crafts.h)
        GemsSocketed = 53,          // any: gems set into kits' guns
        HerbsMilled = 54,           // any: millings (five herbs each)
        OreProspected = 55,         // any: prospectings (five ore each)
        SongsPlayed = 56,           // any: Minstrelsy songs that landed
        PerfectSongs = 57,          // any: songs played at a quality of 0.95 or better
        ExaltedFactions = 58,       // any, a maximum: factions at Exalted
        FactionRank = 59,           // faction id, a maximum: the standing's rank (7 Exalted)
        BattlegroundsWon = 60,      // any, or a map id
        BattlegroundsPlayed = 61,   // any: battlegrounds played to their end
        DungeonBossKills = 62,      // creature entry: a dungeon or raid boss killed
        HolidayQuests = 63,         // any, or a game event id: quests of a world event done while it ran
        HolidaysCelebrated = 64,    // any, a maximum: world events with a quest done
        CompanionsCaught = 65,      // any: companions caught
        WandererKills = 66,         // any: wanderers killed
        WanderersHired = 67,        // any: wanderers hired
        WandererPurchases = 68,     // any: wares bought from wanderers
        FullyGlyphedKits = 69,      // any, a maximum: kits with all six glyph slots inscribed
        ZoneQuests = 70,            // zone id (the quest's ZoneOrSort): quests completed there
        SkyLaunches = 71,           // any: skyriding Launches (Ctrl+Space) and Surges
        ShipsCommissioned = 72,     // any: ships whose keel was laid at a Shipwright (Ships.h)
        Count,
    };

    constexpr uint32_t kAnyKey = 0;

    // Keys for keyed stats. A class or type key is never 0, so "any" can share the stat.
    inline uint32_t ClassKey(WeaponClass c) { return uint32_t(c) + 1; }
    inline uint32_t TypeKey(CreatureType t) { return uint32_t(t); }
    inline uint32_t ClassTypeKey(WeaponClass c, CreatureType t) { return ClassKey(c) << 8 | TypeKey(t); }
    inline uint32_t EquipmentKey(EquipmentKind k) { return uint32_t(k); }

    // An attachment's number (1-255): MW2's fourteen have fixed ones (the order of catalog.json)
    // so stored counters and achievement ids never move; any other id gets a hash in 128-255.
    uint8_t AttachmentCode(std::string const& attachment);
    inline uint32_t GunAttachmentKey(uint16_t gun, std::string const& attachment)
    {
        return uint32_t(gun) << 8 | AttachmentCode(attachment);
    }

    // A stat kept as a maximum (Level, BestPlayerStreak, BestMultiKill) rather than a running count.
    bool IsMaximum(Stat s);

    // The stat's name, for logs and GM commands ("player_kills").
    char const* StatName(Stat s);

    struct StatKey
    {
        Stat stat = Stat::None;
        uint32_t key = kAnyKey;

        bool operator<(StatKey const& o) const { return std::tie(stat, key) < std::tie(o.stat, o.key); }
        bool operator==(StatKey const& o) const { return stat == o.stat && key == o.key; }
    };

    // One account's counters: a row per (stat, key) in account_fusion_stats. Every change is
    // remembered until TakeDirty, so the server writes only the rows that moved.
    class AccountStats
    {
    public:
        uint32_t Get(StatKey k) const;
        uint32_t Get(Stat s, uint32_t key = kAnyKey) const { return Get(StatKey{s, key}); }

        // Count `n` more (saturating, so a counter never wraps back to zero).
        void Add(StatKey k, uint32_t n = 1);
        // Keep the larger of the stored value and `v`.
        void Raise(StatKey k, uint32_t v);
        // Add or Raise, as the stat is kept.
        void Record(StatKey k, uint32_t v);
        // Load a stored row, without marking it changed.
        void Load(StatKey k, uint32_t v) { m_values[k] = v; }

        std::map<StatKey, uint32_t> const& All() const { return m_values; }
        std::vector<StatKey> TakeDirty();

    private:
        std::map<StatKey, uint32_t> m_values;
        std::set<StatKey> m_dirty;
    };

    // Ranges that make a kill long or close (yards): past a vanilla caster's 40, inside melee
    // reach and a bit.
    constexpr float kLongRangeKill = 40.f;
    constexpr float kCloseRangeKill = 8.f;

    // Everything about a kill the counters care about, filled by the server where it credits the
    // kill. A gray creature or a critter counts nothing, as it is worth nothing to a streak.
    struct KillEvent
    {
        bool victimIsPlayer = false;
        CreatureType creatureType = CreatureType::None;     // ignored for a player (a humanoid)
        CreatureRank rank = CreatureRank::Normal;
        bool gray = false;

        uint16_t weaponId = 0;                              // the gun, 0 for none
        WeaponClass weaponClass = WeaponClass::Pistol;      // the gun's class (unused without one)
        std::vector<std::string> attachments;               // the gun's, in the class
        std::vector<uint16_t> perks;                        // the class's perks, deathstreak too
        EquipmentKind equipment = EquipmentKind::None;      // a lethal that killed
        uint32_t streakId = 0;                              // a killstreak reward that killed

        bool headshot = false;
        bool melee = false;                                 // the knife, a lunge, the riot shield
        bool explosive = false;                             // a launcher or underbarrel grenade
        float distance = 0.f;                               // yards from killer to victim
        Stance stance = Stance::Stand;
        bool revenge = false;
        uint32_t burst = 1;                                 // kills in this burst, this one included
    };

    // The counters a kill moves (each by one; BestMultiKill is raised to `burst` separately).
    std::vector<StatKey> KillStats(KillEvent const& e);
    void RecordKill(AccountStats& stats, KillEvent const& e);

    // ── Unlocks ─────────────────────────────────────────────────────────────────────────────

    enum class UnlockKind : uint8_t
    {
        Gun = 1,            // id: catalog gun id
        Attachment = 2,     // id: gun id, attachment: MW2 id (MW2 unlocks attachments per gun)
        Camo = 3,           // id: gun id, camo: catalog camo id (per gun, too)
        Perk = 4,           // id: perk id
        Deathstreak = 5,    // id: deathstreak (perk) id
        Lethal = 6,         // id: catalog equipment id
        Tactical = 7,       // id: catalog equipment id
        Killstreak = 8,     // id: StreakId
        Title = 9,          // id: TitleName's
        PerkPro = 10,       // id: perk id; its Pro (MW2's Pro challenge done)
    };

    struct Unlock
    {
        UnlockKind kind = UnlockKind::Gun;
        uint32_t id = 0;
        std::string attachment;
        uint8_t camo = 0;

        static Unlock Gun(uint16_t gun) { return {UnlockKind::Gun, gun, {}, 0}; }
        static Unlock Attachment(uint16_t gun, std::string const& att) { return {UnlockKind::Attachment, gun, att, 0}; }
        static Unlock Camo(uint16_t gun, uint8_t camo) { return {UnlockKind::Camo, gun, {}, camo}; }
        static Unlock Perk(uint16_t perk) { return {UnlockKind::Perk, perk, {}, 0}; }
        static Unlock Deathstreak(uint16_t id) { return {UnlockKind::Deathstreak, id, {}, 0}; }
        static Unlock Lethal(uint16_t id) { return {UnlockKind::Lethal, id, {}, 0}; }
        static Unlock Tactical(uint16_t id) { return {UnlockKind::Tactical, id, {}, 0}; }
        static Unlock Killstreak(uint32_t id) { return {UnlockKind::Killstreak, id, {}, 0}; }
        static Unlock Title(uint32_t id) { return {UnlockKind::Title, id, {}, 0}; }
        static Unlock PerkPro(uint16_t perk) { return {UnlockKind::PerkPro, perk, {}, 0}; }

        auto Tie() const { return std::tie(kind, id, attachment, camo); }
        bool operator<(Unlock const& o) const { return Tie() < o.Tie(); }
        bool operator==(Unlock const& o) const { return Tie() == o.Tie(); }
    };

    // The pistol every account starts with (MW2's USP .45).
    constexpr char const* kStarterGun = "usp_mp";

    // What an account may use. "None" (id 0, no attachment, camo 0) is always allowed; a game
    // master has everything.
    class UnlockSet
    {
    public:
        // The starter kit: the USP .45 alone.
        static UnlockSet Starter(Catalog const& catalog);
        static UnlockSet Everything();

        void Grant(Unlock const& u) { m_have.insert(u); }
        bool Has(Unlock const& u) const;
        bool IsEverything() const { return m_all; }

        bool IsGunUnlocked(uint16_t gun) const { return !gun || Has(Unlock::Gun(gun)); }
        bool IsAttachmentUnlocked(uint16_t gun, std::string const& att) const
        {
            return att.empty() || Has(Unlock::Attachment(gun, att));
        }
        bool IsCamoUnlocked(uint16_t gun, uint8_t camo) const { return !camo || Has(Unlock::Camo(gun, camo)); }
        bool IsPerkUnlocked(uint16_t perk) const { return !perk || Has(Unlock::Perk(perk)); }
        bool IsPerkProUnlocked(uint16_t perk) const { return perk && Has(Unlock::PerkPro(perk)); }
        bool IsDeathstreakUnlocked(uint16_t id) const { return !id || Has(Unlock::Deathstreak(id)); }
        bool IsLethalUnlocked(uint16_t id) const { return !id || Has(Unlock::Lethal(id)); }
        bool IsTacticalUnlocked(uint16_t id) const { return !id || Has(Unlock::Tactical(id)); }
        bool IsKillstreakUnlocked(uint32_t id) const { return !id || Has(Unlock::Killstreak(id)); }
        bool HasTitle(uint32_t id) const { return Has(Unlock::Title(id)); }

        // Whether any catalog weapon may be fired: an underbarrel weapon with its gun and
        // attachment, equipment by its slot, a killstreak's weapon always (the reward is gated
        // when it is chosen).
        bool CanUseWeapon(WeaponProfile const& w) const;

        // What is held, for a GM listing (empty for Everything).
        std::set<Unlock> const& Held() const { return m_have; }

    private:
        bool m_all = false;
        std::set<Unlock> m_have;
    };

    // Create-a-Class against the account's unlocks rather than the player's level: the class
    // must pass every other rule of ValidateLoadout, then each gun, attachment, camo, perk,
    // deathstreak, equipment and killstreak in it must be unlocked (else Locked), and each perk
    // slot's Pro bit the account's earned Pro (else ProLocked).
    LoadoutError ValidateLoadout(Catalog const& catalog, ClassLoadout const& l, UnlockSet const& unlocks,
        bool pro = false);

    // A killstreak ladder with only the unlocked rewards (the HUD's default ladder before a class
    // picks three).
    std::vector<StreakReward> UnlockedStreaks(std::vector<StreakReward> const& ladder, UnlockSet const& unlocks);

    // ── Achievements ────────────────────────────────────────────────────────────────────────

    class AchievementBook;

    // A Pro's achievement id under its perk's (AchievementId's sub byte).
    constexpr uint32_t kProAchievementSub = 1;
    // The bounty boards' camo feat ("Big Game Hunter"): its item under Feats, past the titles'.
    constexpr uint32_t kBountyAchievementItem = 100;

    // The achievements window's tabs; also the top byte of a default book's ids.
    enum class AchievementCategory : uint8_t
    {
        Weapons = 1,
        Attachments = 2,
        Camos = 3,
        Perks = 4,
        Deathstreaks = 5,
        Lethals = 6,
        Tacticals = 7,
        Killstreaks = 8,
        Feats = 9,          // titles and milestones
        // Wrath's categories (design section 14), past the window's own tabs (10 Prestige, 11 the
        // record).
        General = 20,
        Quests = 21,
        Exploration = 22,
        PvP = 23,
        Dungeons = 24,          // dungeons and raids
        Professions = 25,
        Reputation = 26,
        WorldEvents = 27,
        Combat = 28,            // guns and kits
        Companions = 29,
        World = 30,             // Wanderers, bounties, the open world's dangers
        FeatsOfStrength = 31,   // no points: things done once, often hidden
    };

    // Whether a category is one of the arsenal's unlock tabs (1-9) rather than Wrath's.
    inline bool IsUnlockCategory(AchievementCategory c) { return uint8_t(c) >= 1 && uint8_t(c) <= 9; }
    // The points an achievement of the category is worth unless it says otherwise.
    uint16_t DefaultPoints(AchievementCategory c);

    // A default book's achievement id: stable across catalog changes, as they are stored.
    constexpr uint32_t AchievementId(AchievementCategory c, uint32_t item, uint32_t sub = 0)
    {
        return uint32_t(c) << 24 | (item & 0xFFFF) << 8 | (sub & 0xFF);
    }

    struct Requirement
    {
        StatKey key;
        uint32_t needed = 1;
    };

    // An achievement: earned once every requirement is met (an achievement with none is only
    // ever awarded by hand, by a GM or a script), it unlocks its rewards for the account.
    struct Achievement
    {
        uint32_t id = 0;
        AchievementCategory category = AchievementCategory::Feats;
        std::string name;
        std::string description;
        std::vector<Requirement> requirements;
        std::vector<Unlock> rewards;
        // Shown as "???" until it has some progress or is earned: some things are for finding.
        bool hidden = false;
        // Wrath's achievement points (Feats of Strength have none).
        uint16_t points = 0;
    };

    // A requirement's count: the stat's, or for a meta-achievement's Stat::Achievement 1 once
    // that achievement is earned, and Stat::AchievementPoints the points earned.
    uint32_t RequirementValue(AchievementBook const& book, AccountStats const& stats,
        std::set<uint32_t> const& earned, StatKey const& key);
    // The points of every earned achievement in the book.
    uint32_t PointsOf(AchievementBook const& book, std::set<uint32_t> const& earned);
    // Whether a stat is worked out from the earned achievements rather than counted.
    inline bool IsDerived(Stat s) { return s == Stat::Achievement || s == Stat::AchievementPoints; }

    class AchievementBook
    {
    public:
        // False (and nothing added) on an id of 0 or one already in the book.
        bool Add(Achievement a, std::string& error);
        Achievement const* Find(uint32_t id) const;
        std::vector<Achievement> const& All() const { return m_all; }
        size_t Size() const { return m_all.size(); }

    private:
        std::vector<Achievement> m_all;
        std::map<uint32_t, size_t> m_index;
    };

    // The achievements the stats have earned that are not in `alreadyEarned`, in book order
    // (a meta-achievement after those it asks for, as each newly earned one counts at once).
    // The server stores them and announces them; asking again returns nothing new.
    std::vector<uint32_t> Evaluate(AchievementBook const& book, AccountStats const& stats,
        std::set<uint32_t> const& alreadyEarned);

    // An account's unlocks: the starter kit and every earned achievement's rewards (a GM: all).
    UnlockSet UnlocksFor(Catalog const& catalog, AchievementBook const& book, std::set<uint32_t> const& earned,
        bool isGm);

    struct RequirementProgress
    {
        StatKey key;
        uint32_t current = 0;   // held to `needed`
        uint32_t needed = 0;
    };

    struct AchievementProgress
    {
        uint32_t id = 0;
        bool earned = false;
        std::vector<RequirementProgress> requirements;
        // 0..1, every requirement weighted alike; 1 once earned.
        float Fraction() const;
        bool Started() const;
    };

    AchievementProgress ProgressOf(Achievement const& a, AccountStats const& stats, bool earned);
    // With the meta-achievements' and points' counts from the earned set.
    AchievementProgress ProgressOf(AchievementBook const& book, Achievement const& a, AccountStats const& stats,
        std::set<uint32_t> const& earned);

    // A requirement in words, as the default book's descriptions are written ("Kill 25 humanoids
    // with pistols"); the client may show it beside the progress bar.
    std::string DescribeRequirement(Requirement const& r, Catalog const& catalog);

    // The display names the default book writes with: the arsenal's own (fusion/Arsenal.h:
    // "Stormwind Guard Carbine", "Thorium Grenade", "Gnomish Spy Gyro"), else MW2's.
    std::string GunDisplayName(std::string const& weaponName);
    std::string StreakName(uint32_t streakId);
    std::string AttachmentDisplayName(Attachment const& a);
    std::string CamoDisplayName(Camo const& c);
    std::string PerkDisplayName(Perk const& p);
    // The titles a Title unlock grants ("the Explorer"), "" for an unknown id.
    std::string TitleName(uint32_t titleId);

    // The last boss of every dungeon and raid the default book names (Stat::DungeonBossKills keys).
    struct DungeonBoss
    {
        uint32_t entry;     // creature_template
        char const* name;
        char const* place;
        bool raid;
    };
    std::vector<DungeonBoss> const& DungeonBosses();
    bool IsDungeonBoss(uint32_t entry);

    // The built-in book: every gun (but the starter), attachment (per gun), camo (per gun), perk
    // and its Pro (catalog.json's pro_kills: kills with the perk in the kit, as MW2's Pro
    // challenges), deathstreak, lethal, tactical and killstreak in the catalog, on paths that fit vanilla
    // Azeroth, paced for about 60 hours of leveling with friends. Guns the book has no hand-written
    // path for (a newer catalog's) unlock by kills with their class.
    AchievementBook DefaultAchievementBook(Catalog const& catalog);

    // ── The achievements window (SMSG/CMSG_FUSION_CHANNEL, channel kChannelAchievements) ────
    namespace Packets
    {
        constexpr uint8_t kChannelAchievements = 3;
    }

    namespace AchievementWire
    {
        // 2: each row carries its points (Wrath's categories and meta-achievements).
        constexpr uint8_t kVersion = 2;

        // The body's second byte (after the version).
        enum class Op : uint8_t
        {
            Request = 1,    // client: send me the list (the window opened)
            Page = 2,       // server: a page of the list
            Earned = 3,     // server: these were just earned (a toast)
            Progress = 4,   // server: requirement counts moved
            // 5 is the unlock set (the game glue's EncodeUnlocks).
            Record = 6,     // server: the account's record for the Journal (kills, deaths, best streak)
        };

        // One row of the window. A hidden achievement with no progress goes as "???" with no
        // requirements or rewards, so the client cannot spoil it.
        struct View
        {
            uint32_t id = 0;
            AchievementCategory category = AchievementCategory::Feats;
            bool earned = false;
            bool hidden = false;
            uint16_t points = 0;
            std::string name;
            std::string description;
            std::vector<RequirementProgress> requirements;
            std::vector<Unlock> rewards;

            bool operator==(View const& o) const;
        };

        struct Page
        {
            uint16_t page = 0;
            uint16_t pages = 1;
            std::vector<View> views;
        };

        // A changed achievement's current counts, in its requirements' order.
        struct ProgressEntry
        {
            uint32_t id = 0;
            std::vector<uint32_t> current;
        };

        std::vector<View> Views(AchievementBook const& book, AccountStats const& stats,
            std::set<uint32_t> const& earned);

        // The unearned achievements that ask for any of `changed` (AccountStats::TakeDirty), with
        // their counts now: what a Progress message carries after the server counts something.
        // A newly earned achievement moves its metas and the points: pass {Achievement, id} and
        // {AchievementPoints} among `changed` (ChangedByEarning).
        std::vector<ProgressEntry> Touched(AchievementBook const& book, AccountStats const& stats,
            std::set<uint32_t> const& earned, std::vector<StatKey> const& changed);

        // The derived keys a newly earned set moves.
        std::vector<StatKey> ChangedByEarning(std::vector<uint32_t> const& fresh);

        // The list as channel bodies of at most `maxBody` bytes each (at least one, even empty).
        std::vector<std::vector<uint8_t>> EncodePages(std::vector<View> const& views,
            size_t maxBody = Packets::kMaxChannelBody);
        std::vector<uint8_t> EncodeRequest();
        std::vector<uint8_t> EncodeEarned(std::vector<uint32_t> const& ids);
        std::vector<uint8_t> EncodeProgress(std::vector<ProgressEntry> const& entries);

        // The stats the Journal's record shows, in the order a Record message carries them.
        constexpr Stat kRecordStats[] = {Stat::PlayerKills, Stat::CreatureKills, Stat::Deaths,
            Stat::PlayerDeaths, Stat::BestPlayerStreak, Stat::Headshots};

        // A Record: a count, then each of kRecordStats as (stat u16, value u32), its "any" key.
        std::vector<uint8_t> EncodeRecord(AccountStats const& stats);
        bool DecodeRecord(std::vector<uint8_t> const& body, std::vector<std::pair<Stat, uint32_t>>& out);

        // The op of a body, false on a wrong version or an empty body.
        bool PeekOp(std::vector<uint8_t> const& body, Op& out);
        bool DecodePage(std::vector<uint8_t> const& body, Page& out);
        bool DecodeEarned(std::vector<uint8_t> const& body, std::vector<uint32_t>& out);
        bool DecodeProgress(std::vector<uint8_t> const& body, std::vector<ProgressEntry>& out);
    }
}

#endif
