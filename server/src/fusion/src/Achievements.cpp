/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Achievements.h"
#include "fusion/CompanionBattles.h"
#include "fusion/Arsenal.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <utility>

namespace Fusion
{
    // ── Counters ────────────────────────────────────────────────────────────────────────────

    uint8_t AttachmentCode(std::string const& attachment)
    {
        // MW2's fourteen, in catalog.json's order. Never renumber: the codes are in stored
        // counter keys and achievement ids.
        static char const* const kFixed[] = {"reflex", "silencer", "acog", "gl", "fmj", "shotgun", "grip",
            "rof", "akimbo", "tactical", "eotech", "thermal", "heartbeat", "xmags"};
        if (attachment.empty())
            return 0;
        for (size_t i = 0; i < sizeof(kFixed) / sizeof(kFixed[0]); ++i)
            if (attachment == kFixed[i])
                return uint8_t(i + 1);
        // FNV-1a, folded into 128-255 so it never meets a fixed code.
        uint32_t h = 2166136261u;
        for (char c : attachment)
            h = (h ^ uint8_t(c)) * 16777619u;
        return uint8_t(128 + h % 128);
    }

    bool IsMaximum(Stat s)
    {
        return s == Stat::Level || s == Stat::BestPlayerStreak || s == Stat::BestMultiKill || s == Stat::ProfessionSkill
            || s == Stat::ExaltedFactions || s == Stat::FactionRank || s == Stat::HolidaysCelebrated
            || s == Stat::FullyGlyphedKits;
    }

    char const* StatName(Stat s)
    {
        switch (s)
        {
            case Stat::None: return "none";
            case Stat::PlayerKills: return "player_kills";
            case Stat::CreatureKills: return "creature_kills";
            case Stat::EliteKills: return "elite_kills";
            case Stat::RareKills: return "rare_kills";
            case Stat::BossKills: return "boss_kills";
            case Stat::CreatureTypeKills: return "creature_type_kills";
            case Stat::ClassKills: return "class_kills";
            case Stat::ClassTypeKills: return "class_type_kills";
            case Stat::Headshots: return "headshots";
            case Stat::ClassHeadshots: return "class_headshots";
            case Stat::GunKills: return "gun_kills";
            case Stat::GunHeadshots: return "gun_headshots";
            case Stat::AttachmentKills: return "attachment_kills";
            case Stat::AttachedKills: return "attached_kills";
            case Stat::PerkKills: return "perk_kills";
            case Stat::EquipmentKills: return "equipment_kills";
            case Stat::ExplosiveKills: return "explosive_kills";
            case Stat::StreakKills: return "streak_kills";
            case Stat::MeleeKills: return "melee_kills";
            case Stat::LongRangeKills: return "long_range_kills";
            case Stat::CloseRangeKills: return "close_range_kills";
            case Stat::CrouchedKills: return "crouched_kills";
            case Stat::ProneKills: return "prone_kills";
            case Stat::MultiKills: return "multi_kills";
            case Stat::BestMultiKill: return "best_multi_kill";
            case Stat::RevengeKills: return "revenge_kills";
            case Stat::Deaths: return "deaths";
            case Stat::PlayerDeaths: return "player_deaths";
            case Stat::Level: return "level";
            case Stat::Achievement: return "achievement";
            case Stat::AchievementPoints: return "achievement_points";
            case Stat::ProfessionSkill: return "profession_skill";
            case Stat::ItemsCrafted: return "items_crafted";
            case Stat::GlyphsInscribed: return "glyphs_inscribed";
            case Stat::GemsSocketed: return "gems_socketed";
            case Stat::HerbsMilled: return "herbs_milled";
            case Stat::OreProspected: return "ore_prospected";
            case Stat::SongsPlayed: return "songs_played";
            case Stat::PerfectSongs: return "perfect_songs";
            case Stat::ExaltedFactions: return "exalted_factions";
            case Stat::FactionRank: return "faction_rank";
            case Stat::BattlegroundsWon: return "battlegrounds_won";
            case Stat::BattlegroundsPlayed: return "battlegrounds_played";
            case Stat::DungeonBossKills: return "dungeon_boss_kills";
            case Stat::HolidayQuests: return "holiday_quests";
            case Stat::HolidaysCelebrated: return "holidays_celebrated";
            case Stat::CompanionsCaught: return "companions_caught";
            case Stat::WandererKills: return "wanderer_kills";
            case Stat::WanderersHired: return "wanderers_hired";
            case Stat::WandererPurchases: return "wanderer_purchases";
            case Stat::FullyGlyphedKits: return "fully_glyphed_kits";
            case Stat::ZoneQuests: return "zone_quests";
            case Stat::SkyLaunches: return "sky_launches";
            case Stat::ShipsCommissioned: return "ships_commissioned";
            case Stat::ZonesExplored: return "zones_explored";
            case Stat::QuestsCompleted: return "quests_completed";
            case Stat::DistanceSprinted: return "distance_sprinted";
            case Stat::CorpsesLooted: return "corpses_looted";
            case Stat::Reloads: return "reloads";
            case Stat::ClassSwaps: return "class_swaps";
            case Stat::RevivesGiven: return "revives_given";
            case Stat::TimesRevived: return "times_revived";
            case Stat::StreaksEarned: return "streaks_earned";
            case Stat::StreaksCalled: return "streaks_called";
            case Stat::BestPlayerStreak: return "best_player_streak";
            case Stat::TacticalVictims: return "tactical_victims";
            case Stat::BuildingsBuilt: return "buildings_built";
            case Stat::CompanionsEvolved: return "companions_evolved";
            case Stat::StreaksDowned: return "streaks_downed";
            case Stat::BountiesCompleted: return "bounties_completed";
            case Stat::CompanionBattlesWon: return "companion_battles_won";
            case Stat::TamersDefeated: return "tamers_defeated";
            case Stat::Count: break;
        }
        return "unknown";
    }

    uint32_t AccountStats::Get(StatKey k) const
    {
        auto it = m_values.find(k);
        return it == m_values.end() ? 0 : it->second;
    }

    void AccountStats::Add(StatKey k, uint32_t n)
    {
        if (!n)
            return;
        uint32_t& v = m_values[k];
        v = n > std::numeric_limits<uint32_t>::max() - v ? std::numeric_limits<uint32_t>::max() : v + n;
        m_dirty.insert(k);
    }

    void AccountStats::Raise(StatKey k, uint32_t v)
    {
        uint32_t& cur = m_values[k];
        if (v > cur)
        {
            cur = v;
            m_dirty.insert(k);
        }
    }

    void AccountStats::Record(StatKey k, uint32_t v)
    {
        if (IsMaximum(k.stat))
            Raise(k, v);
        else
            Add(k, v);
    }

    std::vector<StatKey> AccountStats::TakeDirty()
    {
        std::vector<StatKey> out(m_dirty.begin(), m_dirty.end());
        m_dirty.clear();
        return out;
    }

    std::vector<StatKey> KillStats(KillEvent const& e)
    {
        std::vector<StatKey> out;
        if (e.gray)
            return out;
        auto add = [&](Stat s, uint32_t key = kAnyKey) { out.push_back({s, key}); };

        // A player is a humanoid to every typed counter: "kill 25 humanoids" takes either.
        CreatureType const type = e.victimIsPlayer ? CreatureType::Humanoid : e.creatureType;
        if (e.victimIsPlayer)
        {
            add(Stat::PlayerKills);
            if (e.revenge)
                add(Stat::RevengeKills);
        }
        else
        {
            add(Stat::CreatureKills);
            CreatureRank const r = e.rank;
            if (r == CreatureRank::Elite || r == CreatureRank::RareElite || r == CreatureRank::WorldBoss
                || r == CreatureRank::DungeonBoss)
                add(Stat::EliteKills);
            if (r == CreatureRank::Rare || r == CreatureRank::RareElite)
                add(Stat::RareKills);
            if (r == CreatureRank::WorldBoss || r == CreatureRank::DungeonBoss)
                add(Stat::BossKills);
        }
        if (type != CreatureType::None)
            add(Stat::CreatureTypeKills, TypeKey(type));

        // A gun's kill: not a lethal's or a reward's, which carry their own counters.
        bool const gun = e.weaponId && !e.streakId && e.equipment == EquipmentKind::None;
        if (gun)
        {
            add(Stat::ClassKills, ClassKey(e.weaponClass));
            if (type != CreatureType::None)
                add(Stat::ClassTypeKills, ClassTypeKey(e.weaponClass, type));
            add(Stat::GunKills, e.weaponId);
            bool attached = false;
            for (std::string const& a : e.attachments)
            {
                if (a.empty())
                    continue;
                add(Stat::AttachmentKills, GunAttachmentKey(e.weaponId, a));
                attached = true;
            }
            if (attached)
                add(Stat::AttachedKills);
            if (e.headshot)
            {
                add(Stat::ClassHeadshots, ClassKey(e.weaponClass));
                add(Stat::GunHeadshots, e.weaponId);
            }
        }
        if (e.headshot)
            add(Stat::Headshots);
        // Each perk once, however many slots hold it (a stacked perk counts one kill).
        for (size_t i = 0; i < e.perks.size(); ++i)
            if (e.perks[i] && std::find(e.perks.begin(), e.perks.begin() + long(i), e.perks[i]) == e.perks.begin() + long(i))
                add(Stat::PerkKills, e.perks[i]);
        if (e.equipment != EquipmentKind::None)
            add(Stat::EquipmentKills, EquipmentKey(e.equipment));
        bool const blast = e.equipment == EquipmentKind::Frag || e.equipment == EquipmentKind::Semtex
            || e.equipment == EquipmentKind::Claymore || e.equipment == EquipmentKind::C4;
        if (e.explosive || blast || (gun && e.weaponClass == WeaponClass::Launcher))
            add(Stat::ExplosiveKills);
        if (e.streakId)
            add(Stat::StreakKills, e.streakId);
        if (e.melee || (gun && (e.weaponClass == WeaponClass::Melee || e.weaponClass == WeaponClass::RiotShield)))
            add(Stat::MeleeKills);
        if (e.distance >= kLongRangeKill)
        {
            add(Stat::LongRangeKills);
            if (gun)
                add(Stat::LongRangeKills, ClassKey(e.weaponClass));
        }
        if (e.distance <= kCloseRangeKill)
        {
            add(Stat::CloseRangeKills);
            if (type != CreatureType::None)
                add(Stat::CloseRangeKills, TypeKey(type));
        }
        if (e.stance == Stance::Crouch || e.stance == Stance::Slide)
            add(Stat::CrouchedKills);
        else if (e.stance == Stance::Prone)
            add(Stat::ProneKills);
        if (e.burst >= 2)
            add(Stat::MultiKills);
        return out;
    }

    void RecordKill(AccountStats& stats, KillEvent const& e)
    {
        for (StatKey const& k : KillStats(e))
            stats.Add(k);
        if (!e.gray && e.burst >= 2)
            stats.Raise({Stat::BestMultiKill, kAnyKey}, e.burst);
    }

    // ── Unlocks ─────────────────────────────────────────────────────────────────────────────

    UnlockSet UnlockSet::Starter(Catalog const& catalog)
    {
        UnlockSet s;
        if (WeaponProfile const* usp = catalog.Weapons().Find(std::string(kStarterGun)))
            s.Grant(Unlock::Gun(usp->id));
        return s;
    }

    UnlockSet UnlockSet::Everything()
    {
        UnlockSet s;
        s.m_all = true;
        return s;
    }

    bool UnlockSet::Has(Unlock const& u) const
    {
        return m_all || m_have.count(u) > 0;
    }

    bool UnlockSet::CanUseWeapon(WeaponProfile const& w) const
    {
        switch (w.slot)
        {
            case WeaponSlot::Alternate:
                return IsGunUnlocked(w.alternateOf) && IsAttachmentUnlocked(w.alternateOf, w.alternateAttachment);
            case WeaponSlot::Lethal:
                return IsLethalUnlocked(w.id);
            case WeaponSlot::Tactical:
                return IsTacticalUnlocked(w.id);
            case WeaponSlot::Killstreak:
                return true;
            default:
                return IsGunUnlocked(w.id);
        }
    }

    LoadoutError ValidateLoadout(Catalog const& catalog, ClassLoadout const& l, UnlockSet const& unlocks, bool pro)
    {
        // Every rule but the lock first, judged as a GM so only the shape of the class counts.
        LoadoutError const shape = ValidateLoadout(catalog, l, 60u, true, pro);
        if (shape != LoadoutError::None)
            return shape;
        bool const open = unlocks.IsGunUnlocked(l.primary)
            && unlocks.IsAttachmentUnlocked(l.primary, l.primaryAtt1)
            && unlocks.IsAttachmentUnlocked(l.primary, l.primaryAtt2)
            && unlocks.IsCamoUnlocked(l.primary, l.primaryCamo)
            && unlocks.IsGunUnlocked(l.secondary)
            && unlocks.IsAttachmentUnlocked(l.secondary, l.secondaryAtt)
            && unlocks.IsAttachmentUnlocked(l.secondary, l.secondaryAtt2)
            && unlocks.IsLethalUnlocked(l.lethal)
            && unlocks.IsTacticalUnlocked(l.tactical)
            && unlocks.IsPerkUnlocked(l.perk1)
            && unlocks.IsPerkUnlocked(l.perk2)
            && unlocks.IsPerkUnlocked(l.perk3)
            && unlocks.IsDeathstreakUnlocked(l.deathstreak)
            && unlocks.IsKillstreakUnlocked(l.streak1)
            && unlocks.IsKillstreakUnlocked(l.streak2)
            && unlocks.IsKillstreakUnlocked(l.streak3)
            && unlocks.IsAttachmentUnlocked(l.primary, l.primaryAtt3)
            && unlocks.IsAttachmentUnlocked(l.primary, l.primaryAtt4);
        if (!open)
            return LoadoutError::Locked;
        // Each slot's Pro: earned (a GM's Pro switch plays them all without the bits).
        uint16_t const perks[3] = {l.perk1, l.perk2, l.perk3};
        for (int i = 0; i < 3; ++i)
            if ((l.perkPro & (1u << i)) && !unlocks.IsPerkProUnlocked(perks[i]))
                return LoadoutError::ProLocked;
        return LoadoutError::None;
    }

    std::vector<StreakReward> UnlockedStreaks(std::vector<StreakReward> const& ladder, UnlockSet const& unlocks)
    {
        std::vector<StreakReward> out;
        for (StreakReward const& r : ladder)
            if (unlocks.IsKillstreakUnlocked(r.id))
                out.push_back(r);
        return out;
    }

    // ── Achievements ────────────────────────────────────────────────────────────────────────

    bool AchievementBook::Add(Achievement a, std::string& error)
    {
        if (a.id == 0)
        {
            error = "achievement \"" + a.name + "\" has id 0";
            return false;
        }
        if (m_index.count(a.id))
        {
            error = "achievement id " + std::to_string(a.id) + " (\"" + a.name + "\") is used twice";
            return false;
        }
        m_index[a.id] = m_all.size();
        m_all.push_back(std::move(a));
        return true;
    }

    Achievement const* AchievementBook::Find(uint32_t id) const
    {
        auto it = m_index.find(id);
        return it == m_index.end() ? nullptr : &m_all[it->second];
    }

    uint16_t DefaultPoints(AchievementCategory c)
    {
        switch (c)
        {
            case AchievementCategory::Weapons:
            case AchievementCategory::Perks:
            case AchievementCategory::Killstreaks:
                return 10;
            case AchievementCategory::Attachments:
            case AchievementCategory::Camos:
            case AchievementCategory::Deathstreaks:
            case AchievementCategory::Lethals:
            case AchievementCategory::Tacticals:
                return 5;
            case AchievementCategory::Feats: return 25;
            case AchievementCategory::FeatsOfStrength: return 0;
            default: return 10;
        }
    }

    uint32_t PointsOf(AchievementBook const& book, std::set<uint32_t> const& earned)
    {
        uint32_t sum = 0;
        for (uint32_t id : earned)
            if (Achievement const* a = book.Find(id))
                sum += a->points;
        return sum;
    }

    uint32_t RequirementValue(AchievementBook const& book, AccountStats const& stats, std::set<uint32_t> const& earned,
        StatKey const& key)
    {
        switch (key.stat)
        {
            case Stat::Achievement: return earned.count(key.key) ? 1u : 0u;
            case Stat::AchievementPoints: return PointsOf(book, earned);
            default: return stats.Get(key);
        }
    }

    std::vector<uint32_t> Evaluate(AchievementBook const& book, AccountStats const& stats,
        std::set<uint32_t> const& alreadyEarned)
    {
        std::vector<uint32_t> out;
        std::set<uint32_t> earned = alreadyEarned;
        // Again while anything new was earned: a meta-achievement (or a points tier) may follow.
        for (bool more = true; more;)
        {
            more = false;
            for (Achievement const& a : book.All())
            {
                if (a.requirements.empty() || earned.count(a.id))
                    continue;
                bool const met = std::all_of(a.requirements.begin(), a.requirements.end(),
                    [&](Requirement const& r) { return RequirementValue(book, stats, earned, r.key) >= r.needed; });
                if (met)
                {
                    out.push_back(a.id);
                    earned.insert(a.id);
                    more = true;
                }
            }
        }
        return out;
    }

    UnlockSet UnlocksFor(Catalog const& catalog, AchievementBook const& book, std::set<uint32_t> const& earned,
        bool isGm)
    {
        if (isGm)
            return UnlockSet::Everything();
        UnlockSet s = UnlockSet::Starter(catalog);
        for (uint32_t id : earned)
            if (Achievement const* a = book.Find(id))
                for (Unlock const& u : a->rewards)
                    s.Grant(u);
        return s;
    }

    float AchievementProgress::Fraction() const
    {
        if (earned)
            return 1.f;
        if (requirements.empty())
            return 0.f;
        float sum = 0.f;
        for (RequirementProgress const& r : requirements)
            sum += r.needed ? float(r.current) / float(r.needed) : 1.f;
        return sum / float(requirements.size());
    }

    bool AchievementProgress::Started() const
    {
        return earned || std::any_of(requirements.begin(), requirements.end(),
            [](RequirementProgress const& r) { return r.current > 0; });
    }

    AchievementProgress ProgressOf(Achievement const& a, AccountStats const& stats, bool earned)
    {
        AchievementProgress p;
        p.id = a.id;
        p.earned = earned;
        for (Requirement const& r : a.requirements)
            p.requirements.push_back({r.key, std::min(stats.Get(r.key), r.needed), r.needed});
        return p;
    }

    AchievementProgress ProgressOf(AchievementBook const& book, Achievement const& a, AccountStats const& stats,
        std::set<uint32_t> const& earned)
    {
        AchievementProgress p;
        p.id = a.id;
        p.earned = earned.count(a.id) > 0;
        for (Requirement const& r : a.requirements)
            p.requirements.push_back({r.key, std::min(RequirementValue(book, stats, earned, r.key), r.needed), r.needed});
        return p;
    }

    // ── Names ───────────────────────────────────────────────────────────────────────────────

    namespace
    {
        std::string Stem(std::string const& weaponName)
        {
            std::string s = weaponName;
            if (s.size() > 3 && s.compare(s.size() - 3, 3, "_mp") == 0)
                s.resize(s.size() - 3);
            return s;
        }

        char const* ClassPlural(WeaponClass c)
        {
            switch (c)
            {
                case WeaponClass::AssaultRifle: return "assault rifles";
                case WeaponClass::Smg: return "SMGs";
                case WeaponClass::Lmg: return "LMGs";
                case WeaponClass::Shotgun: return "shotguns";
                case WeaponClass::Sniper: return "sniper rifles";
                case WeaponClass::Pistol: return "pistols";
                case WeaponClass::MachinePistol: return "machine pistols";
                case WeaponClass::Launcher: return "launchers";
                case WeaponClass::RiotShield: return "riot shields";
                case WeaponClass::Melee: return "melee weapons";
                case WeaponClass::Lethal: return "lethal equipment";
                case WeaponClass::Tactical: return "tactical equipment";
                case WeaponClass::Killstreak: return "killstreak rewards";
            }
            return "guns";
        }

        char const* TypePlural(CreatureType t)
        {
            switch (t)
            {
                case CreatureType::Beast: return "beasts";
                case CreatureType::Dragonkin: return "dragonkin";
                case CreatureType::Demon: return "demons";
                case CreatureType::Elemental: return "elementals";
                case CreatureType::Giant: return "giants";
                case CreatureType::Undead: return "undead";
                case CreatureType::Humanoid: return "humanoids";
                case CreatureType::Critter: return "critters";
                case CreatureType::Mechanical: return "mechanicals";
                case CreatureType::Totem: return "totems";
                default: return "creatures";
            }
        }

        // Equipment by its catalog weapon's lore name (fusion/Arsenal.h), else MW2's.
        std::string EquipmentLoreName(char const* weaponName, char const* mw2Name)
        {
            std::string const lore = Arsenal::WeaponName(weaponName);
            return lore.empty() ? mw2Name : lore;
        }

        std::string EquipmentName(EquipmentKind k)
        {
            switch (k)
            {
                case EquipmentKind::Frag: return EquipmentLoreName("frag_grenade_mp", "Frag");
                case EquipmentKind::Semtex: return EquipmentLoreName("semtex_mp", "Semtex");
                case EquipmentKind::ThrowingKnife: return EquipmentLoreName("throwingknife_mp", "Throwing Knife");
                case EquipmentKind::Claymore: return EquipmentLoreName("claymore_mp", "Claymore");
                case EquipmentKind::C4: return EquipmentLoreName("c4_mp", "C4");
                case EquipmentKind::Flash: return EquipmentLoreName("flash_grenade_mp", "Flash Grenade");
                case EquipmentKind::Stun: return EquipmentLoreName("concussion_grenade_mp", "Stun Grenade");
                case EquipmentKind::Smoke: return EquipmentLoreName("smoke_grenade_mp", "Smoke Grenade");
                case EquipmentKind::None: break;
            }
            return "equipment";
        }

        std::string GunNameById(Catalog const& catalog, uint32_t id)
        {
            WeaponProfile const* w = catalog.Weapons().Find(uint16_t(id));
            return w ? GunDisplayName(w->name) : "gun " + std::to_string(id);
        }

        std::string AttachmentNameByCode(Catalog const& catalog, uint8_t code)
        {
            for (Attachment const& a : catalog.Attachments())
                if (AttachmentCode(a.id) == code)
                    return AttachmentDisplayName(a);
            return "attachment";
        }
    }

    std::string AttachmentDisplayName(Attachment const& a)
    {
        std::string const lore = Arsenal::AttachmentName(a.id);
        return lore.empty() ? a.name : lore;
    }

    std::string CamoDisplayName(Camo const& c)
    {
        std::string const lore = Arsenal::CamoName(c.suffix);
        return lore.empty() ? c.name : lore;
    }

    std::string PerkDisplayName(Perk const& p)
    {
        std::string const lore = Arsenal::PerkName(p.id);
        return lore.empty() ? p.name : lore;
    }

    std::string GunDisplayName(std::string const& weaponName)
    {
        // The arsenal's own names first (fusion/Arsenal.h); MW2's for anything it lacks.
        std::string const lore = Arsenal::WeaponName(weaponName);
        if (!lore.empty())
            return lore;
        static std::map<std::string, char const*> const kNames = {
            {"famas", "FAMAS"}, {"m4", "M4A1"}, {"scar", "SCAR-H"}, {"tavor", "TAR-21"}, {"fal", "FAL"},
            {"m16", "M16A4"}, {"masada", "ACR"}, {"fn2000", "F2000"}, {"ak47", "AK-47"},
            {"ump45", "UMP45"}, {"mp5k", "MP5K"}, {"kriss", "Vector"}, {"p90", "P90"}, {"uzi", "Mini-Uzi"},
            {"sa80", "L86 LSW"}, {"rpd", "RPD"}, {"mg4", "MG4"}, {"aug", "AUG HBAR"}, {"m240", "M240"},
            {"cheytac", "Intervention"}, {"barrett", "Barrett .50cal"}, {"wa2000", "WA2000"}, {"m21", "M21 EBR"},
            {"spas12", "SPAS-12"}, {"aa12", "AA-12"}, {"striker", "Striker"}, {"ranger", "Ranger"},
            {"m1014", "M1014"}, {"model1887", "Model 1887"},
            {"usp", "USP .45"}, {"coltanaconda", ".44 Magnum"}, {"beretta", "M9"}, {"deserteagle", "Desert Eagle"},
            {"pp2000", "PP2000"}, {"glock", "G18"}, {"beretta393", "M93 Raffica"}, {"tmp", "TMP"},
            {"at4", "AT4-HS"}, {"m79", "Thumper"}, {"stinger", "Stinger"}, {"javelin", "Javelin"},
            {"rpg", "RPG-7"}, {"riotshield", "Riot Shield"},
            {"frag_grenade", "Frag"}, {"semtex", "Semtex"}, {"throwingknife", "Throwing Knife"},
            {"claymore", "Claymore"}, {"c4", "C4"}, {"flash_grenade", "Flash Grenade"},
            {"concussion_grenade", "Stun Grenade"}, {"smoke_grenade", "Smoke Grenade"},
        };
        std::string const stem = Stem(weaponName);
        auto it = kNames.find(stem);
        if (it != kNames.end())
            return it->second;
        std::string out = stem;
        for (char& c : out)
            c = c == '_' ? ' ' : char(std::toupper(uint8_t(c)));
        return out;
    }

    std::string StreakName(uint32_t streakId)
    {
        std::string const lore = Arsenal::StreakName(streakId);
        if (!lore.empty())
            return lore;
        static char const* const kNames[] = {"UAV", "Care Package", "Counter-UAV", "Sentry Gun",
            "Predator Missile", "Precision Airstrike", "Harrier Strike", "Attack Helicopter",
            "Emergency Airdrop", "Pave Low", "Stealth Bomber", "Chopper Gunner", "AC-130", "EMP",
            "Tactical Nuke"};
        if (streakId >= 1 && streakId <= sizeof(kNames) / sizeof(kNames[0]))
            return kNames[streakId - 1];
        return "killstreak " + std::to_string(streakId);
    }

    namespace TitleId
    {
        constexpr uint32_t Explorer = 1, Veteran = 2, Dutiful = 3, Architect = 4, Beastmaster = 5,
            Headhunter = 6, Kingslayer = 7, Untouchable = 8, Relentless = 9, BountyHunter = 10, Manhunter = 11,
            Tamer = 12, EasternTamer = 13, WesternTamer = 14, GrandMasterTamer = 15, Scribe = 16, Gemcutter = 17,
            Bard = 18, MasterBuilder = 19, Seeker = 20, Exalted = 21, Loremaster = 22, DungeonMaster = 23,
            Battlemaster = 24, Wayfarer = 25, Patron = 26, Insane = 27, Arsenal = 28, Jack = 29;
    }

    std::string TitleName(uint32_t titleId)
    {
        static char const* const kNames[] = {"the Explorer", "Veteran", "the Dutiful", "the Architect",
            "Beastmaster", "Headhunter", "Kingslayer", "the Untouchable", "the Relentless", "the Bounty Hunter",
            "Manhunter", "the Tamer", "Tamer of the East", "Tamer of the West", "Grand Master Tamer", "the Scribe",
            "the Gemcutter", "the Bard", "Master Builder", "the Seeker", "the Exalted", "the Loremaster",
            "Dungeon Master", "Battlemaster", "the Wayfarer", "Patron of the Wanderers", "the Insane",
            "the Walking Arsenal", "Jack of All Trades"};
        if (titleId >= 1 && titleId <= sizeof(kNames) / sizeof(kNames[0]))
            return kNames[titleId - 1];
        return "";
    }

    namespace
    {
        // Names the Wrath-style descriptions use: the professions' skill lines, factions, bosses
        // and zones the default book names (an unknown id by number).
        std::string ProfessionName(uint32_t line)
        {
            switch (line)
            {
                case 171: return "Alchemy";
                case 164: return "Blacksmithing";
                case 333: return "Enchanting";
                case 202: return "Engineering";
                case 182: return "Herbalism";
                case 165: return "Leatherworking";
                case 186: return "Mining";
                case 393: return "Skinning";
                case 197: return "Tailoring";
                case 185: return "Cooking";
                case 129: return "First Aid";
                case 356: return "Fishing";
                case 9001: return "Construction";
                case 9002: return "Minstrelsy";
                case 9003: return "Inscription";
                case 9004: return "Jewelcrafting";
                default: return "profession " + std::to_string(line);
            }
        }

        std::string RankName(uint32_t rank)
        {
            static char const* const kRanks[] = {"Hated", "Hostile", "Unfriendly", "Neutral", "Friendly", "Honored",
                "Revered", "Exalted"};
            return rank < 8 ? kRanks[rank] : "Exalted";
        }

        std::string FactionName(uint32_t faction)
        {
            switch (faction)
            {
                case 21: return "Booty Bay";
                case 59: return "the Thorium Brotherhood";
                case 270: return "the Zandalar Tribe";
                case 369: return "Gadgetzan";
                case 529: return "the Argent Dawn";
                case 576: return "Timbermaw Hold";
                case 609: return "the Cenarion Circle";
                case 749: return "the Hydraxian Waterlords";
                case 910: return "the Brood of Nozdormu";
                default: return "faction " + std::to_string(faction);
            }
        }

        std::string ZoneName(uint32_t zone)
        {
            switch (zone)
            {
                case 1: return "Dun Morogh";
                case 12: return "Elwynn Forest";
                case 14: return "Durotar";
                case 17: return "the Barrens";
                case 33: return "Stranglethorn Vale";
                case 40: return "Westfall";
                case 85: return "Tirisfal Glades";
                case 139: return "the Eastern Plaguelands";
                case 141: return "Teldrassil";
                case 215: return "Mulgore";
                case 440: return "Tanaris";
                case 618: return "Winterspring";
                default: return "zone " + std::to_string(zone);
            }
        }
    }

    std::vector<DungeonBoss> const& DungeonBosses()
    {
        static std::vector<DungeonBoss> const all = {
            {639, "Edwin VanCleef", "the Deadmines", false}, {3654, "Mutanus the Devourer", "the Wailing Caverns", false},
            {4275, "Archmage Arugal", "Shadowfang Keep", false}, {1716, "Bazil Thredd", "the Stockade", false},
            {4829, "Aku'mai", "Blackfathom Deeps", false}, {7800, "Mekgineer Thermaplugg", "Gnomeregan", false},
            {4421, "Charlga Razorflank", "Razorfen Kraul", false},
            {3977, "High Inquisitor Whitemane", "the Scarlet Monastery", false},
            {7358, "Amnennar the Coldbringer", "Razorfen Downs", false}, {2748, "Archaedas", "Uldaman", false},
            {7267, "Chief Ukorz Sandscalp", "Zul'Farrak", false}, {12201, "Princess Theradras", "Maraudon", false},
            {5709, "the Shade of Eranikus", "the Sunken Temple", false},
            {9019, "Emperor Dagran Thaurissan", "Blackrock Depths", false},
            {9568, "Overlord Wyrmthalak", "Lower Blackrock Spire", false},
            {10363, "General Drakkisath", "Upper Blackrock Spire", false},
            {1853, "Darkmaster Gandling", "Scholomance", false}, {10440, "Baron Rivendare", "Stratholme", false},
            {11486, "Prince Tortheldrin", "Dire Maul", false},
            {11502, "Ragnaros", "the Molten Core", true}, {10184, "Onyxia", "Onyxia's Lair", true},
            {11583, "Nefarian", "Blackwing Lair", true}, {14834, "Hakkar", "Zul'Gurub", true},
            {15339, "Ossirian the Unscarred", "the Ruins of Ahn'Qiraj", true},
            {15727, "C'Thun", "the Temple of Ahn'Qiraj", true}, {15990, "Kel'Thuzad", "Naxxramas", true},
        };
        return all;
    }

    bool IsDungeonBoss(uint32_t entry)
    {
        for (DungeonBoss const& b : DungeonBosses())
            if (b.entry == entry)
                return true;
        return false;
    }

    namespace
    {
        std::string BossName(uint32_t entry)
        {
            for (DungeonBoss const& b : DungeonBosses())
                if (b.entry == entry)
                    return std::string(b.name) + " in " + b.place;
            return "boss " + std::to_string(entry);
        }
    }

    std::string DescribeRequirement(Requirement const& r, Catalog const& catalog)
    {
        std::string const n = std::to_string(r.needed);
        uint32_t const k = r.key.key;
        auto cls = [&](uint32_t key) { return ClassPlural(WeaponClass(key ? key - 1 : 0)); };
        auto type = [&](uint32_t key) { return TypePlural(CreatureType(key)); };
        switch (r.key.stat)
        {
            case Stat::PlayerKills: return "Kill " + n + " enemy players";
            case Stat::CreatureKills: return "Kill " + n + " creatures worth experience";
            case Stat::EliteKills: return "Kill " + n + " elite creatures";
            case Stat::RareKills: return "Kill " + n + " rare creatures";
            case Stat::BossKills: return "Kill " + n + " dungeon or world bosses";
            case Stat::CreatureTypeKills: return "Kill " + n + " " + type(k);
            case Stat::ClassKills: return "Get " + n + " kills with " + cls(k);
            case Stat::ClassTypeKills:
                return "Kill " + n + " " + type(k & 0xFF) + " with " + cls(k >> 8);
            case Stat::Headshots: return "Get " + n + " headshot kills";
            case Stat::ClassHeadshots: return "Get " + n + " headshot kills with " + cls(k);
            case Stat::GunKills: return "Get " + n + " kills with the " + GunNameById(catalog, k);
            case Stat::GunHeadshots: return "Get " + n + " headshot kills with the " + GunNameById(catalog, k);
            case Stat::AttachmentKills:
                return "Get " + n + " kills with the " + GunNameById(catalog, k >> 8) + " carrying a "
                    + AttachmentNameByCode(catalog, uint8_t(k & 0xFF));
            case Stat::AttachedKills: return "Get " + n + " kills with a gun carrying an attachment";
            case Stat::PerkKills:
            {
                Perk const* p = catalog.FindPerk(uint16_t(k));
                return "Get " + n + " kills with " + (p ? PerkDisplayName(*p) : "a perk") + " in your class";
            }
            case Stat::EquipmentKills: return "Get " + n + " kills with the " + EquipmentName(EquipmentKind(k));
            case Stat::ExplosiveKills: return "Get " + n + " kills with explosives";
            case Stat::StreakKills: return "Get " + n + " kills with the " + StreakName(k);
            case Stat::MeleeKills: return "Get " + n + " melee kills";
            case Stat::LongRangeKills:
                return "Get " + n + " kills" + (k ? std::string(" with ") + cls(k) : std::string()) + " from 40 yards or farther";
            case Stat::CloseRangeKills:
                return k ? "Kill " + n + " " + type(k) + " within 8 yards" : "Get " + n + " kills within 8 yards";
            case Stat::CrouchedKills: return "Get " + n + " kills while crouched";
            case Stat::ProneKills: return "Get " + n + " kills while prone";
            case Stat::MultiKills: return "Get " + n + " kills as part of multi-kills";
            case Stat::BestMultiKill: return "Kill " + n + " enemies in one burst";
            case Stat::RevengeKills: return "Get " + n + " revenge kills";
            case Stat::Deaths: return "Die " + n + " times";
            case Stat::PlayerDeaths: return "Be killed by enemy players " + n + " times";
            case Stat::Level: return "Reach level " + n;
            case Stat::ZonesExplored: return "Explore " + n + " zones";
            case Stat::QuestsCompleted: return "Complete " + n + " quests";
            case Stat::DistanceSprinted: return "Sprint " + n + " yards";
            case Stat::CorpsesLooted: return "Loot " + n + " corpses";
            case Stat::Reloads: return "Reload " + n + " times";
            case Stat::ClassSwaps: return "Equip a class " + n + " times";
            case Stat::RevivesGiven: return "Revive " + n + " downed allies";
            case Stat::TimesRevived: return "Be revived " + n + " times";
            case Stat::StreaksEarned:
                return k ? "Earn the " + StreakName(k) + " " + n + " times" : "Earn " + n + " killstreak rewards";
            case Stat::StreaksCalled:
                return k ? "Call in the " + StreakName(k) + " " + n + " times" : "Call in " + n + " killstreak rewards";
            case Stat::BestPlayerStreak: return "Kill " + n + " enemy players in one life";
            case Stat::TacticalVictims: return "Catch " + n + " enemies with the " + EquipmentName(EquipmentKind(k));
            case Stat::BuildingsBuilt: return "Build " + n + " building pieces";
            case Stat::CompanionsEvolved: return "Evolve " + n + " companions";
            case Stat::StreaksDowned:
                return k ? "Shoot down " + n + " enemy " + StreakName(k) + "s" : "Shoot down " + n + " enemy killstreaks";
            case Stat::BountiesCompleted: return "Turn in " + n + " bounties from the bounty boards";
            case Stat::CompanionBattlesWon: return "Win " + n + " companion battles";
            case Stat::TamersDefeated:
            {
                if (!k)
                    return "Defeat companion tamers " + n + " times";
                CompanionBattles::Tamer const* t = CompanionBattles::FindTamer(CompanionBattles::Tamers(), uint16_t(k));
                return t ? "Defeat " + t->name + " in a companion battle" : "Defeat a companion tamer";
            }
            case Stat::Achievement: return "Earn the achievement " + std::to_string(k);
            case Stat::AchievementPoints: return "Earn " + n + " achievement points";
            case Stat::ProfessionSkill: return "Reach " + n + " skill in " + ProfessionName(k);
            case Stat::ItemsCrafted:
                return k ? "Make " + n + " things with " + ProfessionName(k) : "Make " + n + " things at a trade window";
            case Stat::GlyphsInscribed: return "Inscribe " + n + " glyphs into your kits";
            case Stat::GemsSocketed: return "Set " + n + " gems into your kits' guns";
            case Stat::HerbsMilled: return "Mill herbs " + n + " times";
            case Stat::OreProspected: return "Prospect ore " + n + " times";
            case Stat::SongsPlayed: return "Play " + n + " songs that land";
            case Stat::PerfectSongs: return "Play " + n + " songs near perfectly";
            case Stat::ExaltedFactions: return "Be Exalted with " + n + " factions";
            case Stat::FactionRank: return "Reach " + RankName(r.needed) + " with " + FactionName(k);
            case Stat::BattlegroundsWon: return "Win " + n + " battlegrounds";
            case Stat::BattlegroundsPlayed: return "Play " + n + " battlegrounds to their end";
            case Stat::DungeonBossKills: return "Defeat " + BossName(k);
            case Stat::HolidayQuests: return "Complete " + n + " world event quests";
            case Stat::HolidaysCelebrated: return "Take part in " + n + " different world events";
            case Stat::CompanionsCaught: return "Catch " + n + " companions";
            case Stat::WandererKills: return "Defeat " + n + " wanderers";
            case Stat::WanderersHired: return "Hire wanderers " + n + " times";
            case Stat::WandererPurchases: return "Buy " + n + " wares from wanderers";
            case Stat::FullyGlyphedKits: return "Fill all six glyph slots of " + n + " kits";
            case Stat::ZoneQuests: return "Complete " + n + " quests in " + ZoneName(k);
            case Stat::SkyLaunches: return "Launch or Surge " + n + " times while skyriding";
            case Stat::ShipsCommissioned: return "Commission " + n + " ships";
            case Stat::None:
            case Stat::Count: break;
        }
        return "Do something " + n + " times";
    }

    // ── The default book ────────────────────────────────────────────────────────────────────

    namespace
    {
        Requirement R(Stat s, uint32_t needed, uint32_t key = kAnyKey)
        {
            return {{s, key}, needed};
        }
        Requirement ClassR(Stat s, WeaponClass c, uint32_t needed) { return R(s, needed, ClassKey(c)); }
        Requirement TypeR(CreatureType t, uint32_t needed) { return R(Stat::CreatureTypeKills, needed, TypeKey(t)); }

        // A hand-written path: the achievement's name and what it asks.
        struct Path
        {
            char const* item;   // a weapon's stem ("m4"), or a perk or streak id as text
            char const* name;
            std::vector<Requirement> requirements;
        };

        using WC = WeaponClass;
        using CT = CreatureType;

        // The guns, each from what suits it in Azeroth. Secondaries come early off the starter
        // pistol; the primaries' first of each class comes from a matching corner of the world
        // (humanoids in Westfall for the SMG, beasts up close for the shotgun, elites for the LMG,
        // the long shot for the sniper, Gnomeregan's machines for the launcher), and the rest of
        // a class from using it.
        std::vector<Path> GunPaths()
        {
            return {
                // Pistols and machine pistols, off the USP.
                {"pp2000", "Spray and Pray", {ClassR(Stat::ClassKills, WC::Pistol, 60)}},
                {"coltanaconda", "Six Shooter", {ClassR(Stat::ClassHeadshots, WC::Pistol, 30)}},
                {"beretta", "Sidearm Specialist", {ClassR(Stat::ClassKills, WC::Pistol, 150)}},
                {"deserteagle", "Hand Cannon", {ClassR(Stat::ClassHeadshots, WC::Pistol, 75), R(Stat::Level, 40)}},
                {"glock", "Full Auto", {ClassR(Stat::ClassKills, WC::MachinePistol, 75)}},
                {"beretta393", "Three-Round Burst", {ClassR(Stat::ClassKills, WC::MachinePistol, 150)}},
                {"tmp", "Silent Partner", {ClassR(Stat::ClassHeadshots, WC::MachinePistol, 40)}},
                // SMGs: the first from clearing Defias with a pistol.
                {"ump45", "Westfall Cleanup", {R(Stat::ClassTypeKills, 25, ClassTypeKey(WC::Pistol, CT::Humanoid))}},
                {"mp5k", "Close Quarters", {ClassR(Stat::ClassKills, WC::Smg, 75)}},
                {"p90", "Bullet Hose", {R(Stat::ClassTypeKills, 150, ClassTypeKey(WC::Smg, CT::Humanoid))}},
                {"kriss", "Vector Calculus", {ClassR(Stat::ClassKills, WC::Smg, 225)}},
                {"uzi", "Gnomeregan Special", {ClassR(Stat::ClassKills, WC::Smg, 350), TypeR(CT::Mechanical, 25)}},
                // Assault rifles: the first from soldiering through the early zones.
                {"famas", "Militia Recruit", {R(Stat::Level, 10), R(Stat::CreatureKills, 100)}},
                {"m4", "Rifleman", {ClassR(Stat::ClassKills, WC::AssaultRifle, 50)}},
                {"scar", "Battle Rifle", {ClassR(Stat::ClassKills, WC::AssaultRifle, 125)}},
                {"tavor", "Bullpup", {ClassR(Stat::ClassKills, WC::AssaultRifle, 200)}},
                {"fal", "Marksman", {ClassR(Stat::ClassHeadshots, WC::AssaultRifle, 50)}},
                {"m16", "Burst Discipline", {ClassR(Stat::ClassKills, WC::AssaultRifle, 350)}},
                {"masada", "Adaptive Combat", {ClassR(Stat::ClassKills, WC::AssaultRifle, 500), R(Stat::EliteKills, 50)}},
                {"fn2000", "Space Age", {ClassR(Stat::ClassKills, WC::AssaultRifle, 700)}},
                {"ak47", "Old Reliable", {ClassR(Stat::ClassKills, WC::AssaultRifle, 1000), R(Stat::Level, 50)}},
                // Sniper rifles: the first from the long shot, with anything.
                {"cheytac", "Eagle Eye", {R(Stat::LongRangeKills, 50)}},
                {"barrett", "Fifty Cal", {ClassR(Stat::ClassKills, WC::Sniper, 75)}},
                {"wa2000", "One Shot", {ClassR(Stat::ClassHeadshots, WC::Sniper, 50)}},
                {"m21", "Overwatch", {ClassR(Stat::LongRangeKills, WC::Sniper, 150)}},
                // Shotguns: the first from beasts up close.
                {"spas12", "Beast Mode", {R(Stat::CloseRangeKills, 25, TypeKey(CT::Beast))}},
                {"aa12", "Auto Shotty", {ClassR(Stat::ClassKills, WC::Shotgun, 75)}},
                {"ranger", "Stranglethorn Ranger", {R(Stat::CloseRangeKills, 100, TypeKey(CT::Beast)), R(Stat::Level, 30)}},
                {"striker", "Crowd Control", {ClassR(Stat::ClassKills, WC::Shotgun, 150), R(Stat::MultiKills, 25)}},
                {"m1014", "Slug Thrower", {ClassR(Stat::ClassKills, WC::Shotgun, 250)}},
                {"model1887", "Lever Action", {ClassR(Stat::ClassKills, WC::Shotgun, 400), R(Stat::Level, 50)}},
                // LMGs: the first from elites, the rest from dungeons.
                {"sa80", "Elite Hunter", {R(Stat::EliteKills, 25)}},
                {"rpd", "Suppressive Fire", {ClassR(Stat::ClassKills, WC::Lmg, 100)}},
                {"mg4", "Heavy Gunner", {ClassR(Stat::ClassKills, WC::Lmg, 200)}},
                {"aug", "Dungeon Delver", {R(Stat::EliteKills, 150), R(Stat::BossKills, 5)}},
                {"m240", "Raid Ready", {ClassR(Stat::ClassKills, WC::Lmg, 400), R(Stat::BossKills, 15)}},
                // Launchers: machines, the wide world, dragons, and blowing things up.
                {"at4", "Scrap Metal", {TypeR(CT::Mechanical, 25)}},
                {"m79", "Wanderer", {R(Stat::ZonesExplored, 20)}},
                {"stinger", "Dragon Slayer", {TypeR(CT::Dragonkin, 15)}},
                {"javelin", "Heavy Ordnance", {R(Stat::ExplosiveKills, 50)}},
                {"rpg", "Demolitions Expert", {ClassR(Stat::ClassKills, WC::Launcher, 100), R(Stat::BossKills, 10)}},
                // The riot shield, from fighting up close.
                {"riotshield", "Shield Wall", {R(Stat::MeleeKills, 40)}},
            };
        }

        // The lethals and tacticals, by weapon stem.
        std::vector<Path> EquipmentPaths()
        {
            return {
                {"frag_grenade", "Pull the Pin", {R(Stat::CreatureKills, 100)}},
                {"semtex", "Sticky Situation", {R(Stat::EquipmentKills, 15, EquipmentKey(EquipmentKind::Frag))}},
                {"throwingknife", "Knife Thrower", {R(Stat::MeleeKills, 25)}},
                {"claymore", "Tripwire", {R(Stat::ExplosiveKills, 30), R(Stat::CrouchedKills, 20)}},
                {"c4", "Demolition", {TypeR(CT::Mechanical, 50), R(Stat::EquipmentKills, 10, EquipmentKey(EquipmentKind::Claymore))}},
                {"flash_grenade", "Blinding Light", {R(Stat::Level, 8)}},
                {"concussion_grenade", "Concussed", {R(Stat::TacticalVictims, 25, EquipmentKey(EquipmentKind::Flash))}},
                {"smoke_grenade", "Smoke Screen", {R(Stat::ZonesExplored, 10)}},
            };
        }

        // The perks and deathstreaks, by perk id, each from what the perk is about.
        std::map<uint16_t, Path> PerkPaths()
        {
            std::map<uint16_t, Path> m;
            auto add = [&](uint16_t id, char const* name, std::vector<Requirement> reqs) {
                m[id] = Path{"", name, std::move(reqs)};
            };
            add(PerkId::Marathon, "Long Road to Booty Bay", {R(Stat::DistanceSprinted, 15000)});
            add(PerkId::SleightOfHand, "Quick Hands", {R(Stat::Reloads, 300)});
            add(PerkId::Scavenger, "Picked Clean", {R(Stat::CorpsesLooted, 150)});
            add(PerkId::Bling, "Accessorized", {R(Stat::AttachedKills, 200)});
            add(PerkId::OneManArmy, "Quick Change", {R(Stat::ClassSwaps, 30), R(Stat::Level, 30)});
            add(PerkId::StoppingPower, "Hard Hitter", {R(Stat::CreatureKills, 150)});
            add(PerkId::Lightweight, "Light on Your Feet", {R(Stat::ZonesExplored, 15)});
            add(PerkId::Hardline, "On a Roll", {R(Stat::StreaksEarned, 10)});
            add(PerkId::ColdBlooded, "Cold Blooded", {TypeR(CT::Undead, 150)});
            add(PerkId::DangerClose, "Danger Close", {R(Stat::ExplosiveKills, 40)});
            add(PerkId::Commando, "Knife Fight", {R(Stat::MeleeKills, 20)});
            add(PerkId::SteadyAim, "From the Hip", {ClassR(Stat::ClassKills, WC::Smg, 60)});
            add(PerkId::Scrambler, "Jammer", {TypeR(CT::Mechanical, 30)});
            add(PerkId::Ninja, "Silent Steps", {R(Stat::CrouchedKills, 50)});
            add(PerkId::SitRep, "Situational Awareness", {TypeR(CT::Elemental, 30)});
            add(PerkId::LastStand, "Not Today", {R(Stat::TimesRevived, 10)});
            // Deathstreaks: from dying, which levelling with friends does plenty of.
            add(PerkId::Painkiller, "Walk It Off", {R(Stat::Deaths, 15)});
            add(PerkId::Copycat, "Copycat", {R(Stat::PlayerDeaths, 15)});
            add(PerkId::Martyrdom, "Parting Gift", {R(Stat::Deaths, 40), R(Stat::EquipmentKills, 10, EquipmentKey(EquipmentKind::Frag))});
            add(PerkId::FinalStand, "Back on Your Feet", {R(Stat::Deaths, 75), R(Stat::TimesRevived, 5)});
            return m;
        }

        // Killstreaks: player kills only, as the rewards are PvP's. The bigger ones also want a
        // streak of players in one life, half (or so) of MW2's.
        struct StreakPath
        {
            uint32_t id;
            char const* name;
            uint32_t playerKills;
            uint32_t bestStreak;    // 0 for none
        };

        std::vector<StreakPath> StreakPaths()
        {
            return {
                {StreakId::Uav, "Eyes in the Sky", 5, 0},
                {StreakId::CarePackage, "Special Delivery", 15, 0},
                {StreakId::CounterUav, "Blackout", 20, 0},
                {StreakId::SentryGun, "Automated Defense", 25, 0},
                {StreakId::PredatorMissile, "Guided Missile", 30, 0},
                {StreakId::PrecisionAirstrike, "Danger Danger", 40, 3},
                {StreakId::HarrierStrike, "Air Superiority", 50, 4},
                {StreakId::AttackHelicopter, "Gunship", 60, 4},
                {StreakId::EmergencyAirdrop, "Supply Drop", 75, 5},
                {StreakId::PaveLow, "Heavy Air", 90, 5},
                {StreakId::StealthBomber, "Carpet Bomb", 100, 6},
                {StreakId::ChopperGunner, "Door Gunner", 125, 7},
                {StreakId::Ac130, "Angel of Death", 150, 7},
                {StreakId::Emp, "Lights Out", 200, 9},
                {StreakId::TacticalNuke, "Game Over", 300, 12},
            };
        }

        // Attachments by kills with the gun, MW2's way, on a gentler ladder.
        constexpr uint32_t kAttachmentLadder[] = {10, 20, 35, 50, 70, 90, 115, 140, 170, 200};
        // Camos: the first six by headshots with the gun, the rest by kills with it.
        constexpr uint32_t kCamoHeadshots[] = {5, 10, 20, 30, 45, 60};
        constexpr uint32_t kCamoKills[] = {250, 350};

        uint32_t LadderAt(uint32_t const* ladder, size_t size, size_t i, uint32_t step)
        {
            return i < size ? ladder[i] : ladder[size - 1] + uint32_t(i - size + 1) * step;
        }

        std::string Describe(std::vector<Requirement> const& reqs, Catalog const& catalog)
        {
            std::string out;
            for (size_t i = 0; i < reqs.size(); ++i)
            {
                std::string part = DescribeRequirement(reqs[i], catalog);
                if (i > 0)
                {
                    part[0] = char(std::tolower(uint8_t(part[0])));
                    out += i + 1 == reqs.size() ? " and " : ", ";
                }
                out += part;
            }
            return out + ".";
        }

        bool IsGun(WeaponProfile const& w)
        {
            return w.slot == WeaponSlot::Primary || w.slot == WeaponSlot::Secondary;
        }
    }

    namespace
    {
        // Wrath's categories on our systems (design/GAME_DESIGN.md section 14, "Achievements"):
        // general milestones, quests, exploration, PvP, dungeons and raids, professions (the layer's
        // four too), reputation, world events, guns and kits, companions, the open world's
        // Wanderers and bounties, and Feats of Strength; points on each, meta-achievements that ask
        // for others, and titles. Ids are AchievementId(category, n), never renumbered.
        void AddWrathStyle(AchievementBook& book, Catalog const& catalog)
        {
            using C = AchievementCategory;
            std::string error;
            auto add = [&](C cat, uint32_t n, std::string name, std::vector<Requirement> reqs, uint16_t points,
                           std::vector<Unlock> rewards = {}, bool hidden = false, std::string description = {}) {
                Achievement a;
                a.id = AchievementId(cat, n);
                a.category = cat;
                a.name = std::move(name);
                a.description = description.empty() ? Describe(reqs, catalog) : std::move(description);
                a.requirements = std::move(reqs);
                a.rewards = std::move(rewards);
                a.hidden = hidden;
                a.points = cat == C::FeatsOfStrength ? 0 : points;
                book.Add(std::move(a), error);
                return AchievementId(cat, n);
            };
            auto meta = [](std::vector<uint32_t> const& ids) {
                std::vector<Requirement> out;
                for (uint32_t id : ids)
                    out.push_back(R(Stat::Achievement, 1, id));
                return out;
            };
            auto names = [&](std::vector<uint32_t> const& ids) {
                std::string out;
                for (size_t i = 0; i < ids.size(); ++i)
                {
                    Achievement const* a = book.Find(ids[i]);
                    out += (i ? (i + 1 == ids.size() ? " and " : ", ") : "") + (a ? a->name : std::string("?"));
                }
                return "Earn " + out + ".";
            };
            auto title = [](uint32_t t) { return std::vector<Unlock>{Unlock::Title(t)}; };

            // ── General ──
            uint32_t n = 1;
            for (uint32_t level : {10u, 20u, 30u, 40u, 50u})
                add(C::General, n++, "Level " + std::to_string(level), {R(Stat::Level, level)}, 10);
            n = 10;
            for (uint32_t pts : {100u, 250u, 500u, 1000u, 2000u})
            {
                std::vector<Unlock> r = pts == 500 ? title(TitleId::Seeker) : std::vector<Unlock>{};
                add(C::General, n++, std::to_string(pts) + " Achievement Points", {R(Stat::AchievementPoints, pts)},
                    pts >= 1000 ? 25 : 10, r);
            }
            add(C::General, 20, "Off the Beaten Path", {R(Stat::DistanceSprinted, 50000)}, 10);
            add(C::General, 21, "The Long Haul", {R(Stat::DistanceSprinted, 250000)}, 25);
            add(C::General, 22, "Fortune Seeker", {R(Stat::CorpsesLooted, 1000)}, 10);
            add(C::General, 23, "Down but Not Out", {R(Stat::TimesRevived, 25)}, 10);
            add(C::General, 24, "Field Medic", {R(Stat::RevivesGiven, 10)}, 10);
            add(C::General, 25, "Combat Medic", {R(Stat::RevivesGiven, 50)}, 25);

            // ── Quests ──
            n = 1;
            for (uint32_t q : {1u, 50u, 100u, 500u, 1000u})
                add(C::Quests, n++, q == 1 ? "First Errand" : std::to_string(q) + " Quests", {R(Stat::QuestsCompleted, q)},
                    q >= 500 ? 25 : 10);
            struct ZoneQ
            {
                uint32_t zone;
                char const* name;
                uint32_t quests;
            };
            std::vector<ZoneQ> const zones = {
                {12, "Elwynn Errands", 15}, {1, "Dun Morogh Duties", 15}, {141, "Teldrassil Tasks", 15},
                {14, "Durotar Dealings", 15}, {85, "Tirisfal Troubles", 15}, {215, "Mulgore Matters", 15},
                {40, "Westfall Wanted", 20}, {17, "Barrens Business", 30}, {33, "Stranglethorn Stories", 30},
                {440, "Tanaris Tales", 20}, {618, "Winterspring Work", 15}, {139, "Plaguelands Patrols", 15},
            };
            std::vector<uint32_t> lore;
            n = 20;
            for (ZoneQ const& z : zones)
            {
                uint32_t const id = add(C::Quests, n++, z.name, {R(Stat::ZoneQuests, z.quests, z.zone)}, 10);
                if (z.zone == 33 || z.zone == 440 || z.zone == 618 || z.zone == 139)
                    lore.push_back(id);
            }
            {
                std::vector<Requirement> reqs = meta(lore);
                reqs.push_back(R(Stat::QuestsCompleted, 750));
                add(C::Quests, 40, "Loremaster of Azeroth", std::move(reqs), 50, title(TitleId::Loremaster), false,
                    names(lore).substr(0, names(lore).size() - 1) + ", and complete 750 quests.");
            }

            // ── Exploration ──
            n = 1;
            for (uint32_t z : {10u, 25u, 75u, 150u, 300u})
                add(C::Exploration, n++, z == 300 ? "The Wayfarer" : "Explore " + std::to_string(z) + " Places",
                    {R(Stat::ZonesExplored, z)}, z >= 150 ? 25 : 10,
                    z == 300 ? title(TitleId::Wayfarer) : std::vector<Unlock>{});

            // ── PvP ──
            add(C::PvP, 1, "First Blood", {R(Stat::PlayerKills, 1)}, 10);
            add(C::PvP, 2, "Fifty Fallen Foes", {R(Stat::PlayerKills, 50)}, 10);
            add(C::PvP, 3, "Quarter Thousand", {R(Stat::PlayerKills, 250)}, 10);
            add(C::PvP, 4, "One Thousand Kills", {R(Stat::PlayerKills, 1000)}, 25);
            add(C::PvP, 5, "Killing Spree", {R(Stat::BestPlayerStreak, 5)}, 10);
            add(C::PvP, 6, "Rampage", {R(Stat::BestPlayerStreak, 10)}, 25);
            add(C::PvP, 7, "Payback", {R(Stat::RevengeKills, 10)}, 10);
            add(C::PvP, 8, "Air Defense", {R(Stat::StreaksDowned, 10)}, 10);
            add(C::PvP, 10, "Battleground Veteran", {R(Stat::BattlegroundsPlayed, 25)}, 10);
            add(C::PvP, 11, "First Victory", {R(Stat::BattlegroundsWon, 1)}, 10);
            add(C::PvP, 12, "Ten Victories", {R(Stat::BattlegroundsWon, 10)}, 10);
            add(C::PvP, 13, "Fifty Victories", {R(Stat::BattlegroundsWon, 50)}, 25);
            uint32_t const wsg = add(C::PvP, 20, "Capture the Flag", {R(Stat::BattlegroundsWon, 10, 489)}, 10, {}, false,
                "Win Warsong Gulch (Capture the Flag) 10 times.");
            uint32_t const ab = add(C::PvP, 21, "Domination", {R(Stat::BattlegroundsWon, 10, 529)}, 10, {}, false,
                "Win Arathi Basin (Domination) 10 times.");
            uint32_t const av = add(C::PvP, 22, "Ground War", {R(Stat::BattlegroundsWon, 5, 30)}, 10, {}, false,
                "Win Alterac Valley (Ground War) 5 times.");
            add(C::PvP, 30, "Battlemaster", meta({wsg, ab, av}), 50, title(TitleId::Battlemaster), false, names({wsg, ab, av}));

            // ── Dungeons and raids ──
            std::vector<uint32_t> dungeons, raids;
            n = 1;
            for (DungeonBoss const& b : DungeonBosses())
            {
                std::string place = b.place;
                place[0] = char(std::toupper(uint8_t(place[0])));
                uint32_t const id = add(C::Dungeons, n++, place, {R(Stat::DungeonBossKills, 1, b.entry)}, b.raid ? 25 : 10);
                (b.raid ? raids : dungeons).push_back(id);
            }
            add(C::Dungeons, 50, "Fifty Bosses", {R(Stat::BossKills, 50)}, 10);
            add(C::Dungeons, 51, "Two Hundred Bosses", {R(Stat::BossKills, 200)}, 25);
            add(C::Dungeons, 60, "Dungeon Master", meta(dungeons), 50, title(TitleId::DungeonMaster), false,
                "Defeat the last boss of every dungeon of Azeroth.");
            add(C::Dungeons, 61, "Raider of Azeroth", meta(raids), 50, {}, false, "Defeat the last boss of every raid of Azeroth.");

            // ── Professions ──
            struct Prof
            {
                uint32_t line;
                char const* name;
            };
            std::vector<Prof> const profs = {
                {171, "Alchemy"}, {164, "Blacksmithing"}, {333, "Enchanting"}, {202, "Engineering"}, {182, "Herbalism"},
                {165, "Leatherworking"}, {186, "Mining"}, {393, "Skinning"}, {197, "Tailoring"}, {185, "Cooking"},
                {129, "First Aid"}, {356, "Fishing"}, {9001, "Construction"}, {9002, "Minstrelsy"},
                {9003, "Inscription"}, {9004, "Jewelcrafting"},
            };
            std::map<uint32_t, uint32_t> artisan;
            n = 1;
            for (Prof const& p : profs)
            {
                add(C::Professions, n++, std::string("Journeyman ") + p.name, {R(Stat::ProfessionSkill, 150, p.line)}, 10);
                std::vector<Unlock> r;
                switch (p.line)
                {
                    case 9001: r = title(TitleId::MasterBuilder); break;
                    case 9002: r = title(TitleId::Bard); break;
                    case 9003: r = title(TitleId::Scribe); break;
                    case 9004: r = title(TitleId::Gemcutter); break;
                    default: break;
                }
                artisan[p.line] = add(C::Professions, n++, std::string("Artisan ") + p.name,
                    {R(Stat::ProfessionSkill, 300, p.line)}, 10, r);
            }
            n = 40;
            for (uint32_t c : {10u, 100u, 1000u})
                add(C::Professions, n++, "Made " + std::to_string(c) + " Things", {R(Stat::ItemsCrafted, c)}, c >= 1000 ? 25 : 10);
            add(C::Professions, 45, "The Mill Turns", {R(Stat::HerbsMilled, 1)}, 10);
            add(C::Professions, 46, "Pigment Pusher", {R(Stat::HerbsMilled, 100)}, 10);
            add(C::Professions, 47, "Rock Breaker", {R(Stat::OreProspected, 1)}, 10);
            add(C::Professions, 48, "Gem Hunter", {R(Stat::OreProspected, 100)}, 10);
            add(C::Professions, 49, "First Performance", {R(Stat::SongsPlayed, 1)}, 10);
            add(C::Professions, 50, "Fifty Songs", {R(Stat::SongsPlayed, 50)}, 10);
            add(C::Professions, 51, "Crowd Favourite", {R(Stat::SongsPlayed, 250)}, 25);
            add(C::Professions, 52, "Perfect Pitch", {R(Stat::PerfectSongs, 1)}, 10);
            add(C::Professions, 53, "Virtuoso", {R(Stat::PerfectSongs, 25)}, 25);
            add(C::Professions, 54, "Groundbreaking", {R(Stat::BuildingsBuilt, 1)}, 10);
            add(C::Professions, 55, "Raising the Rafters", {R(Stat::BuildingsBuilt, 25)}, 10);
            add(C::Professions, 56, "City Planner", {R(Stat::BuildingsBuilt, 500)}, 25);
            {
                std::vector<uint32_t> const jack = {artisan[9001], artisan[9002], artisan[9003], artisan[9004],
                    artisan[185], artisan[129], artisan[356]};
                add(C::Professions, 60, "Jack of All Trades", meta(jack), 50, title(TitleId::Jack), false,
                    "Reach 300 in Construction, Minstrelsy, Inscription, Jewelcrafting, Cooking, First Aid and Fishing.");
            }

            // ── Reputation ──
            n = 1;
            for (uint32_t e : {1u, 5u, 10u, 20u})
                add(C::Reputation, n++, e == 1 ? "Exalted" : std::to_string(e) + " Exalted Reputations",
                    {R(Stat::ExaltedFactions, e)}, e >= 10 ? 25 : 10, e == 20 ? title(TitleId::Exalted) : std::vector<Unlock>{});
            struct Fac
            {
                uint32_t faction;
                char const* name;
            };
            std::vector<Fac> const facs = {
                {576, "Friend of the Furbolg"}, {529, "Champion of the Dawn"}, {609, "Guardian of Cenarius"},
                {59, "Brotherhood Friend"}, {270, "Hakkar's Bane"}, {749, "Hydraxian Ally"}, {910, "Timekeeper"},
                {21, "Booty Bay Regular"},
            };
            std::map<uint32_t, uint32_t> facId;
            n = 10;
            for (Fac const& f : facs)
                facId[f.faction] = add(C::Reputation, n++, f.name, {R(Stat::FactionRank, 7, f.faction)}, 10);

            // ── World events ──
            add(C::WorldEvents, 1, "Festive Spirit", {R(Stat::HolidayQuests, 1)}, 10);
            add(C::WorldEvents, 2, "Holiday Helper", {R(Stat::HolidayQuests, 10)}, 10);
            add(C::WorldEvents, 3, "Year-Round Reveler", {R(Stat::HolidayQuests, 50)}, 25);
            add(C::WorldEvents, 4, "Two Holidays", {R(Stat::HolidaysCelebrated, 2)}, 10);
            add(C::WorldEvents, 5, "Four Holidays", {R(Stat::HolidaysCelebrated, 4)}, 10);
            add(C::WorldEvents, 6, "Every Season", {R(Stat::HolidaysCelebrated, 6)}, 25);

            // ── Guns and kits ──
            add(C::Combat, 1, "A Thousand Creatures", {R(Stat::CreatureKills, 1000)}, 10);
            add(C::Combat, 2, "Ten Thousand Creatures", {R(Stat::CreatureKills, 10000)}, 25);
            add(C::Combat, 3, "Elite Slayer", {R(Stat::EliteKills, 100)}, 10);
            add(C::Combat, 4, "Elite Exterminator", {R(Stat::EliteKills, 500)}, 25);
            add(C::Combat, 5, "Rare Find", {R(Stat::RareKills, 10)}, 10);
            add(C::Combat, 6, "Collector of the Rare", {R(Stat::RareKills, 50)}, 25);
            add(C::Combat, 7, "Hundred Headshots", {R(Stat::Headshots, 100)}, 10);
            add(C::Combat, 8, "Long Shot", {R(Stat::LongRangeKills, 100)}, 10);
            add(C::Combat, 9, "Point Blank", {R(Stat::CloseRangeKills, 100)}, 10);
            add(C::Combat, 10, "Multi-Killer", {R(Stat::MultiKills, 50)}, 10);
            add(C::Combat, 11, "Hand to Hand", {R(Stat::MeleeKills, 100)}, 10);
            add(C::Combat, 12, "Blast Radius", {R(Stat::ExplosiveKills, 250)}, 10);
            add(C::Combat, 13, "Streaker", {R(Stat::StreaksEarned, 25)}, 10);
            add(C::Combat, 14, "Call It In", {R(Stat::StreaksCalled, 10)}, 10);
            add(C::Combat, 15, "Air Support on Call", {R(Stat::StreaksCalled, 100)}, 25);
            add(C::Combat, 16, "Accessorized for Life", {R(Stat::AttachedKills, 1000)}, 10);
            add(C::Combat, 17, "Reload Habit", {R(Stat::Reloads, 5000)}, 10);
            add(C::Combat, 18, "Kit Collector", {R(Stat::ClassSwaps, 50)}, 10);
            add(C::Combat, 20, "Inscribed", {R(Stat::GlyphsInscribed, 1)}, 10);
            add(C::Combat, 21, "Glyph Library", {R(Stat::GlyphsInscribed, 10)}, 10);
            add(C::Combat, 22, "Fully Glyphed", {R(Stat::FullyGlyphedKits, 1)}, 10);
            add(C::Combat, 23, "Five Kits, Fully Glyphed", {R(Stat::FullyGlyphedKits, 5)}, 25);
            add(C::Combat, 24, "Socketed", {R(Stat::GemsSocketed, 1)}, 10);
            add(C::Combat, 25, "Bejeweled Arsenal", {R(Stat::GemsSocketed, 10)}, 10);
            add(C::Combat, 30, "The Walking Arsenal",
                {R(Stat::Headshots, 1000), R(Stat::LongRangeKills, 250), R(Stat::CloseRangeKills, 250),
                    R(Stat::ExplosiveKills, 250), R(Stat::MeleeKills, 100)},
                50, title(TitleId::Arsenal));

            // ── Companions ──
            n = 1;
            for (uint32_t c : {1u, 10u, 25u, 50u})
                add(C::Companions, n++, c == 1 ? "Caught One" : std::to_string(c) + " Companions Caught",
                    {R(Stat::CompanionsCaught, c)}, c >= 25 ? 25 : 10);
            add(C::Companions, 10, "Growing Up", {R(Stat::CompanionsEvolved, 1)}, 10);
            add(C::Companions, 11, "Proud Parent", {R(Stat::CompanionsEvolved, 10)}, 25);
            add(C::Companions, 12, "First Battle Won", {R(Stat::CompanionBattlesWon, 1)}, 10);
            add(C::Companions, 13, "Ten Battles Won", {R(Stat::CompanionBattlesWon, 10)}, 10);
            add(C::Companions, 14, "Hundred Battles Won", {R(Stat::CompanionBattlesWon, 100)}, 25);
            add(C::Companions, 15, "Tamer Tamer", {R(Stat::TamersDefeated, 5)}, 10);
            add(C::Companions, 16, "Tamer Trouncer", {R(Stat::TamersDefeated, 20)}, 25);

            // ── The open world: Wanderers and bounties ──
            add(C::World, 1, "Posted Notice", {R(Stat::BountiesCompleted, 1)}, 10);
            add(C::World, 2, "Regular at the Board", {R(Stat::BountiesCompleted, 25)}, 10);
            add(C::World, 10, "Not So Friendly", {R(Stat::WandererKills, 1)}, 10);
            add(C::World, 11, "Wanderer's Bane", {R(Stat::WandererKills, 25)}, 10);
            add(C::World, 12, "Hired Help", {R(Stat::WanderersHired, 1)}, 10);
            add(C::World, 13, "Good Employer", {R(Stat::WanderersHired, 10)}, 10);
            add(C::World, 14, "Window Shopping", {R(Stat::WandererPurchases, 5)}, 10);
            add(C::World, 15, "Patron of the Wanderers", {R(Stat::WanderersHired, 25), R(Stat::WandererPurchases, 50)}, 25,
                title(TitleId::Patron));
            // Skyriding and ships.
            add(C::World, 20, "Liftoff", {R(Stat::SkyLaunches, 1)}, 10);
            add(C::World, 21, "Sky Lord", {R(Stat::SkyLaunches, 500)}, 10);
            add(C::World, 30, "Keel Laid", {R(Stat::ShipsCommissioned, 1)}, 10);
            add(C::World, 31, "Fleet Admiral", {R(Stat::ShipsCommissioned, 2)}, 25);

            // ── Feats of Strength: no points ──
            add(C::FeatsOfStrength, 1, "Game Over, Man", {R(Stat::StreaksCalled, 1, StreakId::TacticalNuke)}, 0, {}, true);
            add(C::FeatsOfStrength, 2, "One-Soldier Army", {R(Stat::BestMultiKill, 6)}, 0, {}, true);
            add(C::FeatsOfStrength, 3, "Insane in the Membrane",
                meta({facId[59], facId[576], facId[270], facId[910], facId[21]}), 0, title(TitleId::Insane), true,
                "Be Exalted with the Thorium Brotherhood, Timbermaw Hold, the Zandalar Tribe, the Brood of "
                "Nozdormu and Booty Bay.");
            add(C::FeatsOfStrength, 4, "Sky Is Falling", {R(Stat::StreaksDowned, 100)}, 0, {}, true);
        }
    }

    AchievementBook DefaultAchievementBook(Catalog const& catalog)
    {
        AchievementBook book;
        std::string error;  // the ids are built unique; a clash would drop the duplicate
        auto add = [&](uint32_t id, AchievementCategory cat, std::string name, std::vector<Requirement> reqs,
                       std::vector<Unlock> rewards, bool hidden = false) {
            Achievement a;
            a.id = id;
            a.category = cat;
            a.name = std::move(name);
            a.description = Describe(reqs, catalog);
            a.requirements = std::move(reqs);
            a.rewards = std::move(rewards);
            a.hidden = hidden;
            a.points = DefaultPoints(cat);
            book.Add(std::move(a), error);
        };

        std::vector<Path> const guns = GunPaths();
        std::vector<Path> const equipment = EquipmentPaths();
        auto findPath = [](std::vector<Path> const& paths, std::string const& stem) -> Path const* {
            for (Path const& p : paths)
                if (stem == p.item)
                    return &p;
            return nullptr;
        };

        // Guns, then each gun's attachments and camos.
        std::map<WeaponClass, uint32_t> unknownInClass;
        for (WeaponProfile const& w : catalog.Weapons().All())
        {
            if (!IsGun(w))
                continue;
            std::string const gunName = GunDisplayName(w.name);
            if (w.name != kStarterGun)
            {
                Path const* p = findPath(guns, Stem(w.name));
                if (p)
                    add(AchievementId(AchievementCategory::Weapons, w.id), AchievementCategory::Weapons, p->name,
                        p->requirements, {Unlock::Gun(w.id)});
                else
                {
                    // A gun the book was not written for: kills with its class, more for each.
                    uint32_t const n = 100 + 100 * unknownInClass[w.weaponClass]++;
                    add(AchievementId(AchievementCategory::Weapons, w.id), AchievementCategory::Weapons,
                        gunName, {ClassR(Stat::ClassKills, w.weaponClass, n)}, {Unlock::Gun(w.id)});
                }
            }

            // Attachments in the order MW2 unlocks them.
            std::vector<std::string> atts = w.attachments;
            std::stable_sort(atts.begin(), atts.end(), [&](std::string const& a, std::string const& b) {
                Attachment const* x = catalog.FindAttachment(a);
                Attachment const* y = catalog.FindAttachment(b);
                return (x ? x->mw2UnlockRank : 0) < (y ? y->mw2UnlockRank : 0);
            });
            for (size_t i = 0; i < atts.size(); ++i)
            {
                Attachment const* a = catalog.FindAttachment(atts[i]);
                uint32_t const n = LadderAt(kAttachmentLadder, std::size(kAttachmentLadder), i, 40);
                add(AchievementId(AchievementCategory::Attachments, w.id, AttachmentCode(atts[i])),
                    AchievementCategory::Attachments, gunName + ": " + (a ? AttachmentDisplayName(*a) : atts[i]),
                    {R(Stat::GunKills, n, w.id)}, {Unlock::Attachment(w.id, atts[i])});
            }

            if (w.camos)
            {
                for (size_t i = 0; i < catalog.Camos().size(); ++i)
                {
                    Camo const& c = catalog.Camos()[i];
                    Requirement const r = i < std::size(kCamoHeadshots)
                        ? R(Stat::GunHeadshots, kCamoHeadshots[i], w.id)
                        : R(Stat::GunKills,
                            LadderAt(kCamoKills, std::size(kCamoKills), i - std::size(kCamoHeadshots), 100), w.id);
                    add(AchievementId(AchievementCategory::Camos, w.id, c.id), AchievementCategory::Camos,
                        gunName + ": " + CamoDisplayName(c), {r}, {Unlock::Camo(w.id, c.id)});
                }
            }
        }

        // Equipment.
        for (WeaponProfile const& w : catalog.Weapons().All())
        {
            bool const lethal = w.slot == WeaponSlot::Lethal;
            if (!lethal && w.slot != WeaponSlot::Tactical)
                continue;
            AchievementCategory const cat = lethal ? AchievementCategory::Lethals : AchievementCategory::Tacticals;
            Unlock const u = lethal ? Unlock::Lethal(w.id) : Unlock::Tactical(w.id);
            if (Path const* p = findPath(equipment, Stem(w.name)))
                add(AchievementId(cat, w.id), cat, p->name, p->requirements, {u});
            else
                add(AchievementId(cat, w.id), cat, GunDisplayName(w.name),
                    {lethal ? R(Stat::ExplosiveKills, 50) : R(Stat::Level, 20)}, {u});
        }

        // Perks and deathstreaks.
        std::map<uint16_t, Path> const perks = PerkPaths();
        for (Perk const& p : catalog.Perks())
        {
            bool const deathstreak = p.tier == 0;
            AchievementCategory const cat = deathstreak ? AchievementCategory::Deathstreaks : AchievementCategory::Perks;
            Unlock const u = deathstreak ? Unlock::Deathstreak(p.id) : Unlock::Perk(p.id);
            auto it = perks.find(p.id);
            if (it != perks.end())
                add(AchievementId(cat, p.id), cat, it->second.name, it->second.requirements, {u});
            else
                add(AchievementId(cat, p.id), cat, PerkDisplayName(p),
                    {deathstreak ? R(Stat::Deaths, 25) : R(Stat::CreatureKills, 100u * p.tier)}, {u});
            // Its Pro: kills with it in the kit (MW2's Pro challenges), after the perk itself.
            if (!deathstreak && p.HasPro())
                add(AchievementId(cat, p.id, kProAchievementSub), cat, PerkDisplayName(p) + " Pro",
                    {R(Stat::PerkKills, p.proKills, p.id)}, {Unlock::PerkPro(p.id)});
        }

        // Killstreaks.
        for (StreakPath const& s : StreakPaths())
        {
            std::vector<Requirement> reqs = {R(Stat::PlayerKills, s.playerKills)};
            if (s.bestStreak)
                reqs.push_back(R(Stat::BestPlayerStreak, s.bestStreak));
            add(AchievementId(AchievementCategory::Killstreaks, s.id), AchievementCategory::Killstreaks, s.name,
                reqs, {Unlock::Killstreak(s.id)});
        }

        // Feats: titles for the long haul, a few hidden for finding.
        auto feat = [&](uint32_t title, char const* name, std::vector<Requirement> reqs, bool hidden = false) {
            add(AchievementId(AchievementCategory::Feats, title), AchievementCategory::Feats, name, std::move(reqs),
                {Unlock::Title(title)}, hidden);
        };
        feat(TitleId::Explorer, "Explorer of Azeroth", {R(Stat::ZonesExplored, 40)});
        feat(TitleId::Veteran, "Level 60", {R(Stat::Level, 60)});
        feat(TitleId::Dutiful, "Loremaster's Errand", {R(Stat::QuestsCompleted, 250)});
        feat(TitleId::Architect, "Homesteader", {R(Stat::BuildingsBuilt, 100)});
        feat(TitleId::Beastmaster, "Pack Leader", {R(Stat::CompanionsEvolved, 5)});
        feat(TitleId::Headhunter, "Headhunter", {R(Stat::Headshots, 500)});
        feat(TitleId::Kingslayer, "Kingslayer", {R(Stat::BossKills, 25)});
        feat(TitleId::Untouchable, "Untouchable", {R(Stat::BestPlayerStreak, 15)}, true);
        feat(TitleId::Relentless, "Relentless", {R(Stat::Deaths, 200), R(Stat::RevengeKills, 25)}, true);

        // The bounty boards (Bounties.h): a title at 10, a camo on every gun at 50, a title and
        // another camo at 100. The camo achievements' own ids sit past the titles' (item 100 on).
        auto everyGun = [&](char const* suffix) {
            std::vector<Unlock> camos;
            uint8_t camo = 0;
            for (Camo const& c : catalog.Camos())
                if (c.suffix == suffix)
                    camo = c.id;
            if (!camo)
                return camos;
            for (WeaponProfile const& w : catalog.Weapons().All())
                if (IsGun(w) && w.camos)
                    camos.push_back(Unlock::Camo(w.id, camo));
            return camos;
        };
        feat(TitleId::BountyHunter, "Bounty Hunter", {R(Stat::BountiesCompleted, 10)});
        add(AchievementId(AchievementCategory::Feats, kBountyAchievementItem), AchievementCategory::Feats,
            "Big Game Hunter", {R(Stat::BountiesCompleted, 50)}, everyGun("_red_tiger"));
        {
            std::vector<Unlock> rewards = everyGun("_blue_tiger");
            rewards.insert(rewards.begin(), Unlock::Title(TitleId::Manhunter));
            add(AchievementId(AchievementCategory::Feats, TitleId::Manhunter), AchievementCategory::Feats,
                "Manhunter", {R(Stat::BountiesCompleted, 100)}, std::move(rewards));
        }

        // Companion battles (CompanionBattles.h, design/COMPANION_BATTLES.md): wins, every tamer
        // anyone may battle on each continent (Blizzard's "Taming Eastern Kingdoms" / "Taming
        // Kalimdor"), and both grand masters.
        feat(TitleId::Tamer, "Companion Brawler", {R(Stat::CompanionBattlesWon, 25)});
        {
            std::vector<Requirement> east, west;
            for (CompanionBattles::Tamer const& t : CompanionBattles::Tamers())
            {
                if (t.side != 0 || t.grand)
                    continue;
                // Kalimdor's zones (AreaTable ids) against the Eastern Kingdoms'.
                bool const kalimdor = t.zone == 400 || t.zone == 405 || t.zone == 15 || t.zone == 357
                    || t.zone == 361 || t.zone == 493 || t.zone == 618;
                (kalimdor ? west : east).push_back(R(Stat::TamersDefeated, 1, t.id));
            }
            feat(TitleId::EasternTamer, "Taming Eastern Kingdoms", std::move(east));
            feat(TitleId::WesternTamer, "Taming Kalimdor", std::move(west));
        }
        {
            std::vector<Requirement> grand;
            for (CompanionBattles::Tamer const& t : CompanionBattles::Tamers())
                if (t.grand)
                    grand.push_back(R(Stat::TamersDefeated, 1, t.id));
            feat(TitleId::GrandMasterTamer, "Grand Master Tamer", std::move(grand));
        }
        AddWrathStyle(book, catalog);
        return book;
    }

    // ── The achievements window ─────────────────────────────────────────────────────────────

    namespace AchievementWire
    {
        namespace
        {
            struct Writer
            {
                std::vector<uint8_t> out;

                void U8(uint8_t v) { out.push_back(v); }
                void U16(uint16_t v)
                {
                    for (int i = 0; i < 2; ++i)
                        out.push_back(uint8_t(v >> (8 * i)));
                }
                void U32(uint32_t v)
                {
                    for (int i = 0; i < 4; ++i)
                        out.push_back(uint8_t(v >> (8 * i)));
                }
                void Str8(std::string const& s)
                {
                    size_t const n = std::min<size_t>(s.size(), 255);
                    U8(uint8_t(n));
                    out.insert(out.end(), s.begin(), s.begin() + long(n));
                }
                void Str16(std::string const& s)
                {
                    size_t const n = std::min<size_t>(s.size(), 65535);
                    U16(uint16_t(n));
                    out.insert(out.end(), s.begin(), s.begin() + long(n));
                }
                void Bytes(std::vector<uint8_t> const& b) { out.insert(out.end(), b.begin(), b.end()); }
            };

            struct Reader
            {
                std::vector<uint8_t> const& in;
                size_t pos = 0;
                bool ok = true;

                bool Need(size_t n)
                {
                    if (!ok || in.size() - pos < n)
                        ok = false;
                    return ok;
                }
                uint8_t U8() { return Need(1) ? in[pos++] : 0; }
                uint16_t U16()
                {
                    if (!Need(2))
                        return 0;
                    uint16_t v = uint16_t(in[pos] | in[pos + 1] << 8);
                    pos += 2;
                    return v;
                }
                uint32_t U32()
                {
                    if (!Need(4))
                        return 0;
                    uint32_t v = 0;
                    for (int i = 0; i < 4; ++i)
                        v |= uint32_t(in[pos + size_t(i)]) << (8 * i);
                    pos += 4;
                    return v;
                }
                std::string Str(size_t n)
                {
                    if (!Need(n))
                        return {};
                    std::string s(in.begin() + long(pos), in.begin() + long(pos + n));
                    pos += n;
                    return s;
                }
                std::string Str8() { return Str(U8()); }
                std::string Str16() { return Str(U16()); }
                bool Done() const { return ok && pos == in.size(); }
            };

            constexpr uint8_t kFlagEarned = 0x01;
            constexpr uint8_t kFlagHidden = 0x02;
            constexpr size_t kPageHeader = 8;   // version, op, page, pages, count

            std::vector<uint8_t> EncodeView(View const& v)
            {
                Writer w;
                w.U32(v.id);
                w.U8(uint8_t(v.category));
                w.U8(uint8_t((v.earned ? kFlagEarned : 0) | (v.hidden ? kFlagHidden : 0)));
                w.U16(v.points);
                w.Str8(v.name);
                w.Str16(v.description);
                size_t const reqs = std::min<size_t>(v.requirements.size(), 255);
                w.U8(uint8_t(reqs));
                for (size_t i = 0; i < reqs; ++i)
                {
                    RequirementProgress const& r = v.requirements[i];
                    w.U16(uint16_t(r.key.stat));
                    w.U32(r.key.key);
                    w.U32(r.current);
                    w.U32(r.needed);
                }
                size_t const rewards = std::min<size_t>(v.rewards.size(), 255);
                w.U8(uint8_t(rewards));
                for (size_t i = 0; i < rewards; ++i)
                {
                    Unlock const& u = v.rewards[i];
                    w.U8(uint8_t(u.kind));
                    w.U32(u.id);
                    w.U8(u.camo);
                    w.Str8(u.attachment);
                }
                return w.out;
            }

            View DecodeView(Reader& r)
            {
                View v;
                v.id = r.U32();
                v.category = AchievementCategory(r.U8());
                uint8_t const flags = r.U8();
                v.earned = flags & kFlagEarned;
                v.hidden = flags & kFlagHidden;
                v.points = r.U16();
                v.name = r.Str8();
                v.description = r.Str16();
                uint8_t const reqs = r.U8();
                for (uint8_t i = 0; i < reqs && r.ok; ++i)
                {
                    RequirementProgress p;
                    p.key.stat = Stat(r.U16());
                    p.key.key = r.U32();
                    p.current = r.U32();
                    p.needed = r.U32();
                    v.requirements.push_back(p);
                }
                uint8_t const rewards = r.U8();
                for (uint8_t i = 0; i < rewards && r.ok; ++i)
                {
                    Unlock u;
                    u.kind = UnlockKind(r.U8());
                    u.id = r.U32();
                    u.camo = r.U8();
                    u.attachment = r.Str8();
                    v.rewards.push_back(u);
                }
                return v;
            }

            bool Header(Reader& r, Op op)
            {
                return r.U8() == kVersion && r.U8() == uint8_t(op) && r.ok;
            }
        }

        bool View::operator==(View const& o) const
        {
            auto req = [](RequirementProgress const& a, RequirementProgress const& b) {
                return a.key == b.key && a.current == b.current && a.needed == b.needed;
            };
            return id == o.id && category == o.category && earned == o.earned && hidden == o.hidden
                && points == o.points && name == o.name && description == o.description && rewards == o.rewards
                && std::equal(requirements.begin(), requirements.end(), o.requirements.begin(), o.requirements.end(), req);
        }

        std::vector<View> Views(AchievementBook const& book, AccountStats const& stats, std::set<uint32_t> const& earned)
        {
            std::vector<View> out;
            out.reserve(book.Size());
            for (Achievement const& a : book.All())
            {
                AchievementProgress const p = ProgressOf(book, a, stats, earned);
                View v;
                v.id = a.id;
                v.category = a.category;
                v.earned = p.earned;
                v.hidden = a.hidden;
                v.points = a.points;
                if (a.hidden && !p.Started())
                    v.name = "???";
                else
                {
                    v.name = a.name;
                    v.description = a.description;
                    v.requirements = p.requirements;
                    v.rewards = a.rewards;
                }
                out.push_back(std::move(v));
            }
            return out;
        }

        std::vector<ProgressEntry> Touched(AchievementBook const& book, AccountStats const& stats,
            std::set<uint32_t> const& earned, std::vector<StatKey> const& changed)
        {
            std::set<StatKey> const moved(changed.begin(), changed.end());
            std::vector<ProgressEntry> out;
            for (Achievement const& a : book.All())
            {
                if (earned.count(a.id))
                    continue;
                bool const touched = std::any_of(a.requirements.begin(), a.requirements.end(),
                    [&](Requirement const& r) { return moved.count(r.key) > 0; });
                if (!touched)
                    continue;
                ProgressEntry e;
                e.id = a.id;
                for (Requirement const& r : a.requirements)
                    e.current.push_back(std::min(RequirementValue(book, stats, earned, r.key), r.needed));
                out.push_back(std::move(e));
            }
            return out;
        }

        std::vector<StatKey> ChangedByEarning(std::vector<uint32_t> const& fresh)
        {
            std::vector<StatKey> out;
            if (fresh.empty())
                return out;
            for (uint32_t id : fresh)
                out.push_back({Stat::Achievement, id});
            out.push_back({Stat::AchievementPoints, kAnyKey});
            return out;
        }

        std::vector<std::vector<uint8_t>> EncodePages(std::vector<View> const& views, size_t maxBody)
        {
            // Group the encoded rows greedily; a row too big for any page goes alone.
            std::vector<std::vector<std::vector<uint8_t>>> groups(1);
            size_t used = kPageHeader;
            for (View const& v : views)
            {
                std::vector<uint8_t> row = EncodeView(v);
                if (!groups.back().empty() && (used + row.size() > maxBody || groups.back().size() == 0xFFFF))
                {
                    groups.emplace_back();
                    used = kPageHeader;
                }
                used += row.size();
                groups.back().push_back(std::move(row));
            }
            std::vector<std::vector<uint8_t>> pages;
            for (size_t i = 0; i < groups.size(); ++i)
            {
                Writer w;
                w.U8(kVersion);
                w.U8(uint8_t(Op::Page));
                w.U16(uint16_t(i));
                w.U16(uint16_t(groups.size()));
                w.U16(uint16_t(groups[i].size()));
                for (std::vector<uint8_t> const& row : groups[i])
                    w.Bytes(row);
                pages.push_back(std::move(w.out));
            }
            return pages;
        }

        std::vector<uint8_t> EncodeRequest()
        {
            return {kVersion, uint8_t(Op::Request)};
        }

        std::vector<uint8_t> EncodeEarned(std::vector<uint32_t> const& ids)
        {
            Writer w;
            w.U8(kVersion);
            w.U8(uint8_t(Op::Earned));
            size_t const n = std::min<size_t>(ids.size(), 0xFFFF);
            w.U16(uint16_t(n));
            for (size_t i = 0; i < n; ++i)
                w.U32(ids[i]);
            return w.out;
        }

        std::vector<uint8_t> EncodeProgress(std::vector<ProgressEntry> const& entries)
        {
            Writer w;
            w.U8(kVersion);
            w.U8(uint8_t(Op::Progress));
            size_t const n = std::min<size_t>(entries.size(), 0xFFFF);
            w.U16(uint16_t(n));
            for (size_t i = 0; i < n; ++i)
            {
                w.U32(entries[i].id);
                size_t const c = std::min<size_t>(entries[i].current.size(), 255);
                w.U8(uint8_t(c));
                for (size_t k = 0; k < c; ++k)
                    w.U32(entries[i].current[k]);
            }
            return w.out;
        }

        bool PeekOp(std::vector<uint8_t> const& body, Op& out)
        {
            if (body.size() < 2 || body[0] != kVersion)
                return false;
            out = Op(body[1]);
            return true;
        }

        bool DecodePage(std::vector<uint8_t> const& body, Page& out)
        {
            Reader r{body};
            if (!Header(r, Op::Page))
                return false;
            Page p;
            p.page = r.U16();
            p.pages = r.U16();
            uint16_t const n = r.U16();
            for (uint16_t i = 0; i < n && r.ok; ++i)
                p.views.push_back(DecodeView(r));
            if (!r.Done() || p.page >= p.pages)
                return false;
            out = std::move(p);
            return true;
        }

        bool DecodeEarned(std::vector<uint8_t> const& body, std::vector<uint32_t>& out)
        {
            Reader r{body};
            if (!Header(r, Op::Earned))
                return false;
            std::vector<uint32_t> ids;
            uint16_t const n = r.U16();
            for (uint16_t i = 0; i < n && r.ok; ++i)
                ids.push_back(r.U32());
            if (!r.Done())
                return false;
            out = std::move(ids);
            return true;
        }

        bool DecodeProgress(std::vector<uint8_t> const& body, std::vector<ProgressEntry>& out)
        {
            Reader r{body};
            if (!Header(r, Op::Progress))
                return false;
            std::vector<ProgressEntry> entries;
            uint16_t const n = r.U16();
            for (uint16_t i = 0; i < n && r.ok; ++i)
            {
                ProgressEntry e;
                e.id = r.U32();
                uint8_t const c = r.U8();
                for (uint8_t k = 0; k < c && r.ok; ++k)
                    e.current.push_back(r.U32());
                entries.push_back(std::move(e));
            }
            if (!r.Done())
                return false;
            out = std::move(entries);
            return true;
        }

        std::vector<uint8_t> EncodeRecord(AccountStats const& stats)
        {
            Writer w;
            w.U8(kVersion);
            w.U8(uint8_t(Op::Record));
            w.U8(uint8_t(std::size(kRecordStats)));
            for (Stat s : kRecordStats)
            {
                w.U16(uint16_t(s));
                w.U32(stats.Get(s));
            }
            return w.out;
        }

        bool DecodeRecord(std::vector<uint8_t> const& body, std::vector<std::pair<Stat, uint32_t>>& out)
        {
            Reader r{body};
            if (!Header(r, Op::Record))
                return false;
            std::vector<std::pair<Stat, uint32_t>> rows;
            uint8_t const n = r.U8();
            for (uint8_t i = 0; i < n && r.ok; ++i)
            {
                Stat const s = Stat(r.U16());
                rows.emplace_back(s, r.U32());
            }
            if (!r.Done())
                return false;
            out = std::move(rows);
            return true;
        }
    }
}
