/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Achievements.h"

#include <fstream>
#include <sstream>

using namespace Fusion;

namespace
{
    // The real catalog (contrib/fusion/catalog.json, beside the seed weapons.json).
    Catalog const& RealCatalog()
    {
        static Catalog const catalog = [] {
            std::string path = FUSION_SEED_WEAPONS;
            path.replace(path.rfind("weapons.json"), 12, "catalog.json");
            std::ifstream in(path);
            std::stringstream ss;
            ss << in.rdbuf();
            Catalog c;
            std::string error;
            if (!c.LoadJson(ss.str(), error))
                std::printf("  catalog.json: %s\n", error.c_str());
            return c;
        }();
        return catalog;
    }

    uint16_t GunId(char const* name)
    {
        WeaponProfile const* w = RealCatalog().Weapons().Find(std::string(name));
        return w ? w->id : 0;
    }

    Achievement Make(uint32_t id, std::vector<Requirement> reqs, std::vector<Unlock> rewards = {})
    {
        Achievement a;
        a.id = id;
        a.name = "test " + std::to_string(id);
        a.requirements = std::move(reqs);
        a.rewards = std::move(rewards);
        return a;
    }

    bool Contains(std::vector<StatKey> const& v, Stat s, uint32_t key = kAnyKey)
    {
        return std::find(v.begin(), v.end(), StatKey{s, key}) != v.end();
    }

    // Stats that meet every requirement in the book.
    AccountStats Everything(AchievementBook const& book)
    {
        AccountStats s;
        for (Achievement const& a : book.All())
            for (Requirement const& r : a.requirements)
                s.Raise(r.key, r.needed);
        return s;
    }
}

TEST(achievement_real_catalog_loads)
{
    CHECK(RealCatalog().Weapons().Size() > 40);
    CHECK(GunId(kStarterGun) != 0);
}

TEST(starter_kit_is_the_pistol_alone)
{
    Catalog const& c = RealCatalog();
    UnlockSet const s = UnlockSet::Starter(c);
    uint16_t const usp = GunId("usp_mp");
    CHECK(s.IsGunUnlocked(usp));
    CHECK(s.Held().size() == 1);
    CHECK(!s.IsEverything());
    // "None" is always allowed.
    CHECK(s.IsGunUnlocked(0) && s.IsAttachmentUnlocked(usp, "") && s.IsCamoUnlocked(usp, 0));
    CHECK(s.IsPerkUnlocked(0) && s.IsLethalUnlocked(0) && s.IsKillstreakUnlocked(0));
    // Everything else is locked, the pistol's own attachments too.
    CHECK(!s.IsAttachmentUnlocked(usp, "silencer"));
    for (WeaponProfile const& w : c.Weapons().All())
        if (w.slot != WeaponSlot::Killstreak)
            CHECK(s.CanUseWeapon(w) == (w.id == usp));
    for (Perk const& p : c.Perks())
        CHECK(!s.IsPerkUnlocked(p.id) && !s.IsDeathstreakUnlocked(p.id));
    for (uint32_t id = StreakId::Uav; id <= StreakId::TacticalNuke; ++id)
        CHECK(!s.IsKillstreakUnlocked(id));
}

TEST(a_gm_has_everything)
{
    Catalog const& c = RealCatalog();
    AchievementBook const book = DefaultAchievementBook(c);
    UnlockSet const gm = UnlocksFor(c, book, {}, true);
    CHECK(gm.IsEverything());
    for (WeaponProfile const& w : c.Weapons().All())
    {
        CHECK(gm.CanUseWeapon(w));
        for (std::string const& a : w.attachments)
            CHECK(gm.IsAttachmentUnlocked(w.id, a));
    }
    CHECK(gm.IsCamoUnlocked(GunId("m4_mp"), 8));
    CHECK(gm.IsPerkUnlocked(PerkId::OneManArmy) && gm.IsDeathstreakUnlocked(PerkId::FinalStand));
    CHECK(gm.IsKillstreakUnlocked(StreakId::TacticalNuke) && gm.HasTitle(1));
}

TEST(attachment_codes_are_fixed_and_unknown_ones_hash_apart)
{
    CHECK(AttachmentCode("") == 0);
    CHECK(AttachmentCode("reflex") == 1);
    CHECK(AttachmentCode("xmags") == 14);
    CHECK(AttachmentCode("bayonet") >= 128);
    CHECK(AttachmentCode("bayonet") == AttachmentCode("bayonet"));
    CHECK(GunAttachmentKey(2, "acog") != GunAttachmentKey(3, "acog"));
    CHECK(GunAttachmentKey(2, "acog") == (2u << 8 | 3u));
}

TEST(account_stats_count_raise_and_track_changes)
{
    AccountStats s;
    s.Load({Stat::Deaths, kAnyKey}, 4);
    CHECK(s.Get(Stat::Deaths) == 4);
    CHECK(s.TakeDirty().empty());           // loading is not a change
    s.Add({Stat::Deaths, kAnyKey});
    s.Record({Stat::Level, kAnyKey}, 12);
    s.Record({Stat::Level, kAnyKey}, 9);    // a maximum: a lower level changes nothing
    CHECK(s.Get(Stat::Deaths) == 5 && s.Get(Stat::Level) == 12);
    std::vector<StatKey> d = s.TakeDirty();
    CHECK(d.size() == 2);
    CHECK(s.TakeDirty().empty());
    s.Raise({Stat::Level, kAnyKey}, 10);
    CHECK(s.TakeDirty().empty());           // not higher: not written
    s.Load({Stat::Reloads, kAnyKey}, 0xFFFFFFF0u);
    s.Add({Stat::Reloads, kAnyKey}, 100);
    CHECK(s.Get(Stat::Reloads) == 0xFFFFFFFFu);    // saturates, never wraps
    CHECK(IsMaximum(Stat::BestPlayerStreak) && !IsMaximum(Stat::PlayerKills));
    CHECK(std::string(StatName(Stat::GunKills)) == "gun_kills");
}

TEST(a_pistol_headshot_on_a_humanoid_counts_everything_it_is)
{
    KillEvent e;
    e.creatureType = CreatureType::Humanoid;
    e.weaponId = 30;
    e.weaponClass = WeaponClass::Pistol;
    e.attachments = {"silencer", ""};
    e.perks = {PerkId::StoppingPower};
    e.headshot = true;
    e.distance = 5.f;
    e.stance = Stance::Crouch;
    std::vector<StatKey> k = KillStats(e);
    CHECK(Contains(k, Stat::CreatureKills));
    CHECK(!Contains(k, Stat::PlayerKills));
    CHECK(Contains(k, Stat::CreatureTypeKills, TypeKey(CreatureType::Humanoid)));
    CHECK(Contains(k, Stat::ClassKills, ClassKey(WeaponClass::Pistol)));
    CHECK(Contains(k, Stat::ClassTypeKills, ClassTypeKey(WeaponClass::Pistol, CreatureType::Humanoid)));
    CHECK(Contains(k, Stat::GunKills, 30));
    CHECK(Contains(k, Stat::GunHeadshots, 30));
    CHECK(Contains(k, Stat::ClassHeadshots, ClassKey(WeaponClass::Pistol)));
    CHECK(Contains(k, Stat::Headshots));
    CHECK(Contains(k, Stat::AttachmentKills, GunAttachmentKey(30, "silencer")));
    CHECK(Contains(k, Stat::AttachedKills));
    CHECK(Contains(k, Stat::PerkKills, PerkId::StoppingPower));
    CHECK(Contains(k, Stat::CloseRangeKills));
    CHECK(Contains(k, Stat::CloseRangeKills, TypeKey(CreatureType::Humanoid)));
    CHECK(Contains(k, Stat::CrouchedKills));
    CHECK(!Contains(k, Stat::LongRangeKills) && !Contains(k, Stat::EliteKills) && !Contains(k, Stat::MultiKills));
    CHECK(!Contains(k, Stat::ExplosiveKills) && !Contains(k, Stat::MeleeKills));
}

TEST(kill_kinds_count_their_own_way)
{
    // Gray creatures and critters count nothing.
    KillEvent gray;
    gray.gray = true;
    gray.weaponId = 30;
    CHECK(KillStats(gray).empty());

    // A player is a humanoid, and a player kill, never a creature kill.
    KillEvent pvp;
    pvp.victimIsPlayer = true;
    pvp.creatureType = CreatureType::Beast;     // ignored for a player
    pvp.revenge = true;
    pvp.distance = 50.f;
    pvp.weaponId = 20;
    pvp.weaponClass = WeaponClass::Sniper;
    std::vector<StatKey> k = KillStats(pvp);
    CHECK(Contains(k, Stat::PlayerKills) && Contains(k, Stat::RevengeKills) && !Contains(k, Stat::CreatureKills));
    CHECK(Contains(k, Stat::CreatureTypeKills, TypeKey(CreatureType::Humanoid)));
    CHECK(!Contains(k, Stat::CreatureTypeKills, TypeKey(CreatureType::Beast)));
    CHECK(Contains(k, Stat::LongRangeKills) && Contains(k, Stat::LongRangeKills, ClassKey(WeaponClass::Sniper)));

    // A dungeon boss is an elite and a boss; a rare elite is an elite and a rare.
    KillEvent boss;
    boss.rank = CreatureRank::DungeonBoss;
    boss.distance = 20.f;
    k = KillStats(boss);
    CHECK(Contains(k, Stat::EliteKills) && Contains(k, Stat::BossKills) && !Contains(k, Stat::RareKills));
    boss.rank = CreatureRank::RareElite;
    k = KillStats(boss);
    CHECK(Contains(k, Stat::EliteKills) && Contains(k, Stat::RareKills) && !Contains(k, Stat::BossKills));

    // A frag is equipment and an explosive, not a gun kill even with a gun in hand.
    KillEvent frag;
    frag.equipment = EquipmentKind::Frag;
    frag.weaponId = 2;
    frag.weaponClass = WeaponClass::AssaultRifle;
    frag.distance = 20.f;
    frag.burst = 3;
    k = KillStats(frag);
    CHECK(Contains(k, Stat::EquipmentKills, EquipmentKey(EquipmentKind::Frag)) && Contains(k, Stat::ExplosiveKills));
    CHECK(!Contains(k, Stat::GunKills, 2) && !Contains(k, Stat::ClassKills, ClassKey(WeaponClass::AssaultRifle)));
    CHECK(Contains(k, Stat::MultiKills));
    AccountStats s;
    RecordKill(s, frag);
    CHECK(s.Get(Stat::BestMultiKill) == 3 && s.Get(Stat::MultiKills) == 1);

    // A riot shield bash is a melee kill; a killstreak's kill is the streak's.
    KillEvent shield;
    shield.weaponId = 43;
    shield.weaponClass = WeaponClass::RiotShield;
    shield.distance = 2.f;
    CHECK(Contains(KillStats(shield), Stat::MeleeKills));
    KillEvent heli;
    heli.victimIsPlayer = true;
    heli.streakId = StreakId::AttackHelicopter;
    heli.weaponId = 3012;
    heli.weaponClass = WeaponClass::Killstreak;
    heli.distance = 30.f;
    k = KillStats(heli);
    CHECK(Contains(k, Stat::StreakKills, StreakId::AttackHelicopter) && !Contains(k, Stat::GunKills, 3012));
}

TEST(evaluation_earns_each_achievement_exactly_once)
{
    AchievementBook book;
    std::string error;
    CHECK(book.Add(Make(1, {{{Stat::CreatureKills, kAnyKey}, 10}}, {Unlock::Gun(2)}), error));
    CHECK(book.Add(Make(2, {{{Stat::CreatureKills, kAnyKey}, 20}}, {Unlock::Gun(3)}), error));
    AccountStats s;
    std::set<uint32_t> earned;
    s.Add({Stat::CreatureKills, kAnyKey}, 9);
    CHECK(Evaluate(book, s, earned).empty());
    s.Add({Stat::CreatureKills, kAnyKey});
    std::vector<uint32_t> got = Evaluate(book, s, earned);
    CHECK(got.size() == 1 && got[0] == 1);
    earned.insert(got.begin(), got.end());
    CHECK(Evaluate(book, s, earned).empty());   // asking again: nothing new
    s.Add({Stat::CreatureKills, kAnyKey}, 100);
    got = Evaluate(book, s, earned);
    CHECK(got.size() == 1 && got[0] == 2);
    earned.insert(got.begin(), got.end());
    CHECK(Evaluate(book, s, earned).empty());
}

TEST(several_requirements_must_all_be_met)
{
    AchievementBook book;
    std::string error;
    CHECK(book.Add(Make(7, {{{Stat::Level, kAnyKey}, 10}, {{Stat::CreatureKills, kAnyKey}, 100}}), error));
    // An achievement without requirements is only ever awarded by hand.
    CHECK(book.Add(Make(8, {}), error));
    AccountStats s;
    s.Raise({Stat::Level, kAnyKey}, 60);
    CHECK(Evaluate(book, s, {}).empty());
    s.Add({Stat::CreatureKills, kAnyKey}, 99);
    CHECK(Evaluate(book, s, {}).empty());
    s.Add({Stat::CreatureKills, kAnyKey});
    std::vector<uint32_t> got = Evaluate(book, s, {});
    CHECK(got.size() == 1 && got[0] == 7);
}

TEST(the_book_refuses_a_bad_or_repeated_id)
{
    AchievementBook book;
    std::string error;
    CHECK(!book.Add(Make(0, {}), error) && !error.empty());
    CHECK(book.Add(Make(5, {}), error));
    CHECK(!book.Add(Make(5, {}), error));
    CHECK(book.Size() == 1 && book.Find(5) && !book.Find(6));
}

TEST(attachments_and_camos_unlock_per_gun)
{
    Catalog const& c = RealCatalog();
    AchievementBook const book = DefaultAchievementBook(c);
    uint16_t const m4 = GunId("m4_mp"), scar = GunId("scar_mp");
    Achievement const* m4Reflex = book.Find(AchievementId(AchievementCategory::Attachments, m4, AttachmentCode("reflex")));
    CHECK(m4Reflex != nullptr);
    if (!m4Reflex)
        return;
    // MW2's way: kills with that gun, the red dot first.
    CHECK(m4Reflex->requirements.size() == 1);
    CHECK(m4Reflex->requirements[0].key == (StatKey{Stat::GunKills, m4}));
    CHECK(m4Reflex->rewards.size() == 1 && m4Reflex->rewards[0] == Unlock::Attachment(m4, "reflex"));

    // Kills with the M4 unlock the M4's red dot, never the SCAR's.
    AccountStats s;
    KillEvent e;
    e.creatureType = CreatureType::Beast;
    e.weaponId = m4;
    e.weaponClass = WeaponClass::AssaultRifle;
    e.distance = 20.f;
    for (int i = 0; i < 10; ++i)
        RecordKill(s, e);
    std::vector<uint32_t> got = Evaluate(book, s, {});
    std::set<uint32_t> earned(got.begin(), got.end());
    UnlockSet const u = UnlocksFor(c, book, earned, false);
    CHECK(u.IsAttachmentUnlocked(m4, "reflex"));
    CHECK(!u.IsAttachmentUnlocked(scar, "reflex"));
    CHECK(!u.IsAttachmentUnlocked(m4, "silencer"));     // the next rung needs more

    // Camos: headshots with that gun.
    Achievement const* woodland = book.Find(AchievementId(AchievementCategory::Camos, m4, 1));
    CHECK(woodland && woodland->requirements[0].key == (StatKey{Stat::GunHeadshots, m4}));
    CHECK(woodland && woodland->rewards[0] == Unlock::Camo(m4, 1));
    // A gun without camos has none to earn.
    CHECK(!book.Find(AchievementId(AchievementCategory::Camos, GunId("usp_mp"), 1)));
}

TEST(an_underbarrel_weapon_comes_with_its_attachment)
{
    Catalog const& c = RealCatalog();
    WeaponProfile const* gl = c.Weapons().Find(std::string("gl_m4_mp"));
    CHECK(gl && gl->slot == WeaponSlot::Alternate);
    if (!gl)
        return;
    UnlockSet s = UnlockSet::Starter(c);
    s.Grant(Unlock::Gun(gl->alternateOf));
    CHECK(!s.CanUseWeapon(*gl));
    s.Grant(Unlock::Attachment(gl->alternateOf, gl->alternateAttachment));
    CHECK(s.CanUseWeapon(*gl));
}

TEST(every_catalog_unlockable_is_reachable_from_the_default_book)
{
    Catalog const& c = RealCatalog();
    AchievementBook const book = DefaultAchievementBook(c);
    std::set<uint32_t> all;
    for (Achievement const& a : book.All())
        all.insert(a.id);
    UnlockSet const u = UnlocksFor(c, book, all, false);
    CHECK(!u.IsEverything());

    size_t checked = 0;
    for (WeaponProfile const& w : c.Weapons().All())
    {
        if (w.slot == WeaponSlot::Killstreak)
            continue;
        CHECK(u.CanUseWeapon(w));
        if (!u.CanUseWeapon(w))
            std::printf("  unreachable weapon %s\n", w.name.c_str());
        ++checked;
        for (std::string const& a : w.attachments)
        {
            CHECK(u.IsAttachmentUnlocked(w.id, a));
            ++checked;
        }
        if (w.camos)
            for (Camo const& camo : c.Camos())
            {
                CHECK(u.IsCamoUnlocked(w.id, camo.id));
                ++checked;
            }
    }
    for (Perk const& p : c.Perks())
    {
        CHECK(p.tier == 0 ? u.IsDeathstreakUnlocked(p.id) : u.IsPerkUnlocked(p.id));
        ++checked;
    }
    for (uint32_t id = StreakId::Uav; id <= StreakId::TacticalNuke; ++id)
        CHECK(u.IsKillstreakUnlocked(id));
    CHECK(checked > 500);
    CHECK(c.Perks().size() == 20);
}

TEST(every_default_achievement_can_be_earned_and_reads_well)
{
    Catalog const& c = RealCatalog();
    AchievementBook const book = DefaultAchievementBook(c);
    CHECK(book.Size() > 550);     // about 600: most are per-gun attachments and camos
    // Every requirement is a counter the server keeps, and meeting them all earns them all.
    std::vector<uint32_t> got = Evaluate(book, Everything(book), {});
    CHECK(got.size() == book.Size());
    for (Achievement const& a : book.All())
    {
        // The arsenal's tabs unlock something each; Wrath's categories may give only points.
        CHECK(!a.requirements.empty() && !a.name.empty());
        CHECK(!IsUnlockCategory(a.category) || !a.rewards.empty());
        CHECK(uint32_t(a.category) == a.id >> 24);
        for (Requirement const& r : a.requirements)
            CHECK(r.needed > 0 && r.key.stat != Stat::None && r.key.stat < Stat::Count);
        CHECK(a.description.size() > 5 && a.description.back() == '.');
        CHECK(a.description.find("Do something") == std::string::npos);
        // Killstreaks are PvP's: only player kills (and player streaks) earn them.
        if (a.category == AchievementCategory::Killstreaks)
            for (Requirement const& r : a.requirements)
                CHECK(r.key.stat == Stat::PlayerKills || r.key.stat == Stat::BestPlayerStreak);
    }
    Achievement const* ump = book.Find(AchievementId(AchievementCategory::Weapons, GunId("ump45_mp")));
    CHECK(ump && ump->description == "Kill 25 humanoids with pistols.");
    Achievement const* deagle = book.Find(AchievementId(AchievementCategory::Weapons, GunId("deserteagle_mp")));
    CHECK(deagle && deagle->description == "Get 75 headshot kills with pistols and reach level 40.");
}

TEST(a_new_account_can_start_with_only_its_pistol)
{
    Catalog const& c = RealCatalog();
    AchievementBook const book = DefaultAchievementBook(c);
    AccountStats s;
    KillEvent e;
    e.creatureType = CreatureType::Humanoid;
    e.weaponId = GunId("usp_mp");
    e.weaponClass = WeaponClass::Pistol;
    e.distance = 15.f;
    for (int i = 0; i < 25; ++i)
        RecordKill(s, e);
    std::vector<uint32_t> got = Evaluate(book, s, {});
    std::set<uint32_t> earned(got.begin(), got.end());
    UnlockSet const u = UnlocksFor(c, book, earned, false);
    CHECK(u.IsGunUnlocked(GunId("ump45_mp")));          // Westfall Cleanup
    CHECK(u.IsAttachmentUnlocked(GunId("usp_mp"), "silencer"));
    CHECK(!u.IsGunUnlocked(GunId("m4_mp")));
}

TEST(create_a_class_checks_the_accounts_unlocks)
{
    Catalog const& c = RealCatalog();
    UnlockSet u = UnlockSet::Starter(c);
    uint16_t const usp = GunId("usp_mp"), m4 = GunId("m4_mp");
    ClassLoadout l;
    l.name = "Starter";
    l.secondary = usp;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::None);
    l.primary = m4;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::Locked);
    u.Grant(Unlock::Gun(m4));
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::None);
    l.primaryAtt1 = "acog";
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::Locked);
    u.Grant(Unlock::Attachment(m4, "acog"));
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::None);
    l.primaryCamo = 2;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::Locked);
    u.Grant(Unlock::Camo(m4, 2));
    l.perk1 = PerkId::Marathon;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::Locked);
    u.Grant(Unlock::Perk(PerkId::Marathon));
    l.lethal = GunId("frag_grenade_mp");
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::Locked);
    u.Grant(Unlock::Lethal(l.lethal));
    l.streak1 = uint16_t(StreakId::Uav);
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::Locked);
    u.Grant(Unlock::Killstreak(StreakId::Uav));
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::None);
    // The other rules still hold, and come first: a deathstreak in a perk slot is wrong, and a
    // perk that does not stack twice too many, not locked.
    l.perk2 = PerkId::Painkiller;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::WrongPerk);
    l.perk1 = l.perk2 = PerkId::StoppingPower;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::PerkDoesNotStack);
    l.perk1 = PerkId::Marathon;
    l.perk2 = 0;
    l.deathstreak = PerkId::Painkiller;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::Locked);
    // A GM passes everything.
    CHECK(ValidateLoadout(c, l, UnlockSet::Everything()) == LoadoutError::None);
}

TEST(the_streak_ladder_keeps_only_unlocked_rewards)
{
    UnlockSet u;
    u.Grant(Unlock::Killstreak(StreakId::Uav));
    std::vector<StreakReward> ladder = {{StreakId::Uav, 3, false}, {StreakId::PredatorMissile, 5, true}};
    std::vector<StreakReward> got = UnlockedStreaks(ladder, u);
    CHECK(got.size() == 1 && got[0].id == StreakId::Uav);
    CHECK(UnlockedStreaks(ladder, UnlockSet::Everything()).size() == 2);
}

TEST(progress_reports_current_and_needed)
{
    Achievement a = Make(9, {{{Stat::Level, kAnyKey}, 10}, {{Stat::CreatureKills, kAnyKey}, 100}});
    AccountStats s;
    AchievementProgress p = ProgressOf(a, s, false);
    CHECK(!p.Started());
    CHECK_NEAR(p.Fraction(), 0.f, 1e-6);
    s.Raise({Stat::Level, kAnyKey}, 60);
    s.Add({Stat::CreatureKills, kAnyKey}, 25);
    p = ProgressOf(a, s, false);
    CHECK(p.Started() && p.requirements.size() == 2);
    CHECK(p.requirements[0].current == 10 && p.requirements[0].needed == 10);     // held to needed
    CHECK(p.requirements[1].current == 25 && p.requirements[1].needed == 100);
    CHECK_NEAR(p.Fraction(), 0.625f, 1e-6);
    CHECK_NEAR(ProgressOf(a, s, true).Fraction(), 1.f, 1e-6);
}

TEST(a_hidden_achievement_is_masked_until_started)
{
    AchievementBook book;
    std::string error;
    Achievement a = Make(11, {{{Stat::BestPlayerStreak, kAnyKey}, 15}}, {Unlock::Title(8)});
    a.hidden = true;
    a.description = "Secret.";
    CHECK(book.Add(a, error));
    AccountStats s;
    std::vector<AchievementWire::View> v = AchievementWire::Views(book, s, {});
    CHECK(v.size() == 1 && v[0].hidden && v[0].name == "???" && v[0].description.empty());
    CHECK(v[0].requirements.empty() && v[0].rewards.empty());
    s.Raise({Stat::BestPlayerStreak, kAnyKey}, 3);
    v = AchievementWire::Views(book, s, {});
    CHECK(v[0].name == a.name && v[0].description == "Secret." && v[0].requirements[0].current == 3);
    CHECK(v[0].rewards.size() == 1);
}

TEST(touched_lists_only_unearned_achievements_on_changed_counters)
{
    AchievementBook book;
    std::string error;
    book.Add(Make(1, {{{Stat::Deaths, kAnyKey}, 10}}), error);
    book.Add(Make(2, {{{Stat::Deaths, kAnyKey}, 50}, {{Stat::Level, kAnyKey}, 5}}), error);
    book.Add(Make(3, {{{Stat::Reloads, kAnyKey}, 10}}), error);
    AccountStats s;
    s.Add({Stat::Deaths, kAnyKey}, 12);
    std::vector<AchievementWire::ProgressEntry> t =
        AchievementWire::Touched(book, s, {1}, s.TakeDirty());
    CHECK(t.size() == 1 && t[0].id == 2);
    CHECK(t.size() == 1 && t[0].current.size() == 2 && t[0].current[0] == 12 && t[0].current[1] == 0);
}

TEST(the_achievements_window_round_trips_in_pages)
{
    Catalog const& c = RealCatalog();
    AchievementBook const book = DefaultAchievementBook(c);
    AccountStats s;
    s.Add({Stat::CreatureKills, kAnyKey}, 120);
    s.Raise({Stat::Level, kAnyKey}, 12);
    std::set<uint32_t> earned;
    for (uint32_t id : Evaluate(book, s, {}))
        earned.insert(id);
    CHECK(!earned.empty());
    std::vector<AchievementWire::View> views = AchievementWire::Views(book, s, earned);
    std::vector<std::vector<uint8_t>> pages = AchievementWire::EncodePages(views);
    CHECK(pages.size() > 1);
    std::vector<AchievementWire::View> back;
    for (size_t i = 0; i < pages.size(); ++i)
    {
        CHECK(pages[i].size() <= Packets::kMaxChannelBody);
        AchievementWire::Op op;
        CHECK(AchievementWire::PeekOp(pages[i], op) && op == AchievementWire::Op::Page);
        AchievementWire::Page p;
        CHECK(AchievementWire::DecodePage(pages[i], p));
        CHECK(p.page == i && p.pages == pages.size());
        back.insert(back.end(), p.views.begin(), p.views.end());
    }
    CHECK(back.size() == views.size());
    bool same = back.size() == views.size();
    for (size_t i = 0; same && i < views.size(); ++i)
        same = back[i] == views[i];
    CHECK(same);

    // An empty list is still one (empty) page.
    std::vector<std::vector<uint8_t>> none = AchievementWire::EncodePages({});
    AchievementWire::Page p;
    CHECK(none.size() == 1 && AchievementWire::DecodePage(none[0], p) && p.views.empty() && p.pages == 1);

    // A cut-off or wrong-version page is refused.
    std::vector<uint8_t> cut = pages[0];
    cut.pop_back();
    CHECK(!AchievementWire::DecodePage(cut, p));
    std::vector<uint8_t> wrong = pages[0];
    wrong[0] = 99;
    CHECK(!AchievementWire::DecodePage(wrong, p));
    AchievementWire::Op op;
    CHECK(!AchievementWire::PeekOp(wrong, op));
}

TEST(earned_and_progress_messages_round_trip)
{
    std::vector<uint8_t> body = AchievementWire::EncodeEarned({0x01000200u, 7u});
    // Version 1, op 3, count 2, then the ids little-endian.
    CHECK(body.size() == 2 + 2 + 8);
    CHECK(body[0] == AchievementWire::kVersion && body[1] == 3 && body[2] == 2 && body[3] == 0);
    CHECK(body[4] == 0x00 && body[5] == 0x02 && body[6] == 0x00 && body[7] == 0x01);
    std::vector<uint32_t> ids;
    CHECK(AchievementWire::DecodeEarned(body, ids) && ids.size() == 2 && ids[0] == 0x01000200u && ids[1] == 7u);
    std::vector<AchievementWire::ProgressEntry> wrongOp;
    CHECK(!AchievementWire::DecodeProgress(body, wrongOp));     // an Earned body is not a Progress one

    std::vector<AchievementWire::ProgressEntry> in = {{5, {1, 2}}, {6, {}}};
    std::vector<AchievementWire::ProgressEntry> out;
    CHECK(AchievementWire::DecodeProgress(AchievementWire::EncodeProgress(in), out));
    CHECK(out.size() == 2 && out[0].id == 5 && out[0].current == std::vector<uint32_t>({1, 2}) && out[1].current.empty());

    std::vector<uint8_t> req = AchievementWire::EncodeRequest();
    AchievementWire::Op op;
    CHECK(req.size() == 2 && AchievementWire::PeekOp(req, op) && op == AchievementWire::Op::Request);
}

TEST(the_journal_record_carries_the_accounts_kills_and_deaths)
{
    AccountStats s;
    s.Add({Stat::PlayerKills, kAnyKey}, 12);
    s.Add({Stat::Deaths, kAnyKey}, 4);
    s.Raise({Stat::BestPlayerStreak, kAnyKey}, 7);
    s.Add({Stat::PlayerKills, 3}, 99);  // a keyed count is not the record's
    std::vector<uint8_t> body = AchievementWire::EncodeRecord(s);
    // Version 1, op 6, six stats, each (u16 stat, u32 value): what the client's Journal reads.
    CHECK(body.size() == 3 + 6 * 6);
    CHECK(body[0] == AchievementWire::kVersion && body[1] == 6 && body[2] == 6);
    CHECK(body[3] == 1 && body[4] == 0 && body[5] == 12 && body[6] == 0);   // player kills 12
    std::vector<std::pair<Stat, uint32_t>> rows;
    CHECK(AchievementWire::DecodeRecord(body, rows) && rows.size() == 6);
    CHECK(rows[0] == std::make_pair(Stat::PlayerKills, 12u));
    CHECK(rows[1] == std::make_pair(Stat::CreatureKills, 0u));
    CHECK(rows[2] == std::make_pair(Stat::Deaths, 4u));
    CHECK(rows[4] == std::make_pair(Stat::BestPlayerStreak, 7u));
    std::vector<std::pair<Stat, uint32_t>> wrong;
    CHECK(!AchievementWire::DecodeRecord(AchievementWire::EncodeRequest(), wrong));
}

TEST(every_perk_has_a_pro_earned_by_kills_with_it)
{
    Catalog const& c = RealCatalog();
    AchievementBook const book = DefaultAchievementBook(c);
    size_t pros = 0;
    for (Perk const& p : c.Perks())
    {
        Achievement const* a = book.Find(AchievementId(AchievementCategory::Perks, p.id, kProAchievementSub));
        if (p.tier == 0)
        {
            CHECK(!p.HasPro());
            continue;
        }
        CHECK(p.HasPro());
        CHECK(a != nullptr);
        if (!a)
            continue;
        ++pros;
        CHECK(a->rewards.size() == 1 && a->rewards[0] == Unlock::PerkPro(p.id));
        CHECK(a->requirements.size() == 1 && a->requirements[0].key == (StatKey{Stat::PerkKills, p.id}));
        CHECK(a->requirements[0].needed == p.proKills);
        CHECK(a->name.find(" Pro") != std::string::npos);
    }
    CHECK(pros == 16);
    // Tinker's Rails stacks as its own rule; Stopping Power never.
    CHECK(c.FindPerk(PerkId::Bling)->stack == PerkStack::Special && c.FindPerk(PerkId::Bling)->maxStack == 3);
    CHECK(c.FindPerk(PerkId::StoppingPower)->stack == PerkStack::None);
}

TEST(a_pro_is_chosen_only_once_earned)
{
    Catalog const& c = RealCatalog();
    AchievementBook const book = DefaultAchievementBook(c);
    uint16_t const bling = PerkId::Bling;
    ClassLoadout l;
    l.perk1 = bling;
    l.perkPro = 0x01;
    std::set<uint32_t> earned = {AchievementId(AchievementCategory::Perks, bling)};
    UnlockSet u = UnlocksFor(c, book, earned, false);
    CHECK(u.IsPerkUnlocked(bling) && !u.IsPerkProUnlocked(bling));
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::ProLocked);
    l.perkPro = 0;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::None);
    // Kills with Tinker's Rails in the kit earn its Pro.
    AccountStats stats;
    stats.Add({Stat::PerkKills, bling}, c.FindPerk(bling)->proKills);
    std::vector<uint32_t> const now = Evaluate(book, stats, earned);
    uint32_t const proId = AchievementId(AchievementCategory::Perks, bling, kProAchievementSub);
    CHECK(std::find(now.begin(), now.end(), proId) != now.end());
    earned.insert(proId);
    u = UnlocksFor(c, book, earned, false);
    CHECK(u.IsPerkProUnlocked(bling));
    l.perkPro = 0x01;
    CHECK(ValidateLoadout(c, l, u) == LoadoutError::None);
    CHECK(UnlockSet::Everything().IsPerkProUnlocked(bling));
    CHECK(!UnlockSet::Everything().IsPerkProUnlocked(0));
}

TEST(three_rails_carry_four_attachments_on_a_real_gun)
{
    // Tinker's Rails three times: four attachments of four kinds, every two of them a
    // combination the gun has (MW2's own definitions), each one unlocked.
    Catalog const& c = RealCatalog();
    ClassLoadout l;
    l.primary = GunId("famas_mp");
    l.primaryAtt1 = "reflex";
    l.primaryAtt2 = "silencer";
    l.primaryAtt3 = "gl";
    l.primaryAtt4 = "xmags";
    l.perk1 = l.perk2 = l.perk3 = PerkId::Bling;
    CHECK(l.primary != 0);
    CHECK(ValidateLoadout(c, l, UnlockSet::Everything()) == LoadoutError::None);
    // The gun shows two: the sight and the launcher.
    CHECK(PrimaryVariant(c, l) == c.VariantName(*c.Weapons().Find(l.primary), {"reflex", "gl"}));
    // A second sight never goes on, nor a fourth with two Rails.
    l.primaryAtt4 = "acog";
    CHECK(ValidateLoadout(c, l, UnlockSet::Everything()) == LoadoutError::IncompatibleAttachments);
    l.primaryAtt4 = "xmags";
    l.perk3 = 0;
    CHECK(ValidateLoadout(c, l, UnlockSet::Everything()) == LoadoutError::TwoAttachmentsNeedBling);
    // The real data never pairs two attachments of one kind.
    for (WeaponProfile const& w : c.Weapons().All())
        for (auto const& kv : w.variants)
        {
            size_t const plus = kv.first.find('+');
            if (plus != std::string::npos)
                CHECK(KindOfAttachment(kv.first.substr(0, plus)) != KindOfAttachment(kv.first.substr(plus + 1)));
        }
}

TEST(a_stacked_perk_counts_one_kill)
{
    KillEvent e;
    e.creatureType = CreatureType::Beast;
    e.perks = {PerkId::Bling, PerkId::Bling, PerkId::Marathon, PerkId::Bling};
    std::vector<StatKey> const k = KillStats(e);
    CHECK(std::count(k.begin(), k.end(), StatKey{Stat::PerkKills, PerkId::Bling}) == 1);
    CHECK(std::count(k.begin(), k.end(), StatKey{Stat::PerkKills, PerkId::Marathon}) == 1);
}
