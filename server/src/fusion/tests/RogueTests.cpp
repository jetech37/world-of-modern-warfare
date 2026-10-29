/*
 * World of Modern Warfare: the rogue's three archetypes since the Bard became the Duelist
 * (2026-10-21, design/CLASS_CATALOG.md section 4): the option table, the Assassin's openers, the
 * Trickster's poisons, pockets and locks, the Duelist's Momentum and guard, and moving a Bard's
 * character onto the Duelist. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/ClassEffects.h"
#include "fusion/Classes.h"

#include <algorithm>
#include <set>
#include <string>

using namespace Fusion;
using namespace Fusion::Classes;

TEST(rogue_archetypes_are_assassin_trickster_duelist)
{
    CHECK(std::string(ArchetypeName(kRogue, 0)) == "Assassin");
    CHECK(std::string(ArchetypeName(kRogue, 1)) == "Trickster");
    CHECK(std::string(ArchetypeName(kRogue, 2)) == "Duelist");
    for (uint8_t a = 0; a < kArchetypes; ++a)
        CHECK(std::string(ArchetypeName(kRogue, a)) != "Bard");
    // Six tiers of three, ids class/tier/archetype, the core apart, a capstone at 60 only.
    std::vector<Option const*> const all = OptionsOf(kRogue);
    CHECK(all.size() == 18u);
    for (uint8_t tier = 1; tier <= kTiers; ++tier)
    {
        std::array<Option const*, kArchetypes> const row = TierOptions(kRogue, tier);
        for (uint8_t a = 0; a < kArchetypes; ++a)
        {
            CHECK(row[a] != nullptr);
            CHECK(row[a]->id == OptionId(kRogue, tier, a));
            CHECK(row[a]->capstone == (tier == 6));
            CHECK(Available(row[a]->id));
        }
    }
    std::array<Option const*, 3> const core = CoreOf(kRogue);
    CHECK(core[0]->id == 401 && std::string(core[0]->name) == "Stealth");
    CHECK(core[1]->id == 402 && core[2]->id == 403);
    // The new table, by name.
    char const* const names[6][3] = {
        {"Ambush", "Deadly Poison", "Sinister Strike"},
        {"Cheap Shot", "Smoke Bomb", "Lunge"},
        {"Garrote", "Poison Satchel", "Riposte"},
        {"Distract", "Light Fingers", "Blade Flurry"},
        {"Preparation", "Decoy", "Relentless"},
        {"Deathmark", "Hall of Mirrors", "Adrenaline Rush"},
    };
    for (uint8_t tier = 1; tier <= kTiers; ++tier)
        for (uint8_t a = 0; a < kArchetypes; ++a)
            CHECK(std::string(FindOption(OptionId(kRogue, tier, a))->name) == names[tier - 1][a]);
    CHECK(FindOption(OptionId(kRogue, 5, 2))->kind == Kind::Passive);
}

TEST(rogue_options_no_longer_name_an_instrument)
{
    // The Bard's donors (Hand Drum, Lute, Flute, Harp, Bagpipes, Thunder Drums) are retired.
    for (uint32_t s : {13159u, 19779u, 17512u, 10293u, 18946u, 26188u})
    {
        CHECK(OptionBySpell(kRogue, s) == nullptr);
        std::vector<uint32_t> const& r = RetiredSpells(kRogue);
        CHECK(std::find(r.begin(), r.end(), s) != r.end());
    }
    // No retired spell is any option's, and Utility is untouched.
    for (uint32_t s : RetiredSpells(kRogue))
        CHECK(OptionBySpell(kRogue, s) == nullptr);
    CHECK(UtilityOf(kRogue) == (std::vector<uint32_t>{921, 1804, 1842}));
    // Every option's spell is refitted, and none is the Utility's.
    for (Option const* o : OptionsOf(kRogue))
    {
        CHECK(RefitOf(o->spells[0]) != nullptr || !LearnsSpells(*o));
        for (uint32_t u : UtilityOf(kRogue))
            CHECK(o->spells[0] != u);
    }
    CHECK(RefitOf(11269)->flags & REFIT_ANY_SIDE);     // Ambush: the opener's gate is ours
}

TEST(assassin_openers_need_stealth_or_the_opening)
{
    uint16_t const ambush = OptionId(kRogue, 1, 0), cheap = OptionId(kRogue, 2, 0), garrote = OptionId(kRogue, 3, 0);
    CHECK(IsOpener(ambush) && IsOpener(cheap) && IsOpener(garrote));
    CHECK(!IsOpener(OptionId(kRogue, 4, 0)) && !IsOpener(OptionId(kRogue, 6, 0)) && !IsOpener(OptionId(kRogue, 1, 2)));
    // Out of the shadows and the opening: none of them (the long fight is not the Assassin's).
    for (uint16_t o : {ambush, cheap, garrote})
        CHECK(CanOpen(o, false, false, true, false) == OpenerVerdict::NeedsStealth);
    // From stealth or inside the opening they go; Ambush only from behind or with Distract.
    CHECK(CanOpen(ambush, true, false, true, false) == OpenerVerdict::Ok);
    CHECK(CanOpen(ambush, false, true, true, false) == OpenerVerdict::Ok);
    CHECK(CanOpen(ambush, true, false, false, false) == OpenerVerdict::NeedsBehind);
    CHECK(CanOpen(ambush, true, false, false, true) == OpenerVerdict::Ok);
    CHECK(CanOpen(cheap, true, false, false, false) == OpenerVerdict::Ok);
    CHECK(CanOpen(garrote, false, true, false, false) == OpenerVerdict::Ok);
    // Anything else is no opener's business.
    CHECK(CanOpen(OptionId(kRogue, 1, 2), false, false, false, false) == OpenerVerdict::Ok);
    // Positioning pays a second.
    CHECK(CheapShotMs(false, false) == 4000 && CheapShotMs(true, false) == 2500);
    CHECK(CheapShotMs(false, true) == 5000 && CheapShotMs(true, true) == 3500);
    CHECK(GarroteSilenceMs(false) == 3000 && GarroteSilenceMs(true) == 4000);
    CHECK(kOpeningMs == 4000 && kOpeningMultiplier == 2.f);
}

TEST(preparation_readies_the_core_and_the_assassin)
{
    std::vector<uint16_t> const r = PreparationReadies();
    std::set<uint16_t> const s(r.begin(), r.end());
    CHECK(s.count(401) && s.count(402) && s.count(403));
    CHECK(s.count(411) && s.count(421) && s.count(431) && s.count(441) && s.count(461));
    CHECK(!s.count(451));      // not itself
    CHECK(!s.count(412) && !s.count(413));
}

TEST(poison_satchel_switches_through_three_poisons)
{
    CHECK(NextPoison(Poison::None) == Poison::Crippling);
    CHECK(NextPoison(Poison::Crippling) == Poison::MindNumbing);
    CHECK(NextPoison(Poison::MindNumbing) == Poison::Wound);
    CHECK(NextPoison(Poison::Wound) == Poison::Crippling);
    CHECK(std::string(PoisonName(Poison::Wound)) == "Wound Poison");
    // The vanilla debuff by level: the first rank below its own level, the best one allowed above.
    CHECK(PoisonDebuff(Poison::Crippling, 10) == 3409);
    CHECK(PoisonDebuff(Poison::Crippling, 49) == 3409 && PoisonDebuff(Poison::Crippling, 50) == 11201);
    CHECK(PoisonDebuff(Poison::MindNumbing, 40) == 8692 && PoisonDebuff(Poison::MindNumbing, 60) == 11398);
    CHECK(PoisonDebuff(Poison::Wound, 30) == 13218 && PoisonDebuff(Poison::Wound, 60) == 13224);
    CHECK(PoisonDebuff(Poison::None, 60) == 0);
}

TEST(light_fingers_lifts_purses_only_from_creatures)
{
    PocketTarget t;
    t.creature = t.alive = t.hostile = t.humanoid = t.hasPocketLoot = true;
    t.distance = 3.f;
    t.inCombat = true;
    t.lightFingers = true;
    CHECK(CanLift(t) == PocketVerdict::Ok);             // in combat, from the front, unstealthed
    PocketTarget u = t;
    u.alreadyPicked = true;
    CHECK(CanLift(u) == PocketVerdict::Picked);         // once a creature
    u = t;
    u.distance = 6.f;
    CHECK(CanLift(u) == PocketVerdict::TooFar);
    u = t;
    u.creature = false;                                 // a player: never stolen from
    CHECK(CanLift(u) == PocketVerdict::NoTarget);
    u = t;
    u.alive = false;
    CHECK(CanLift(u) == PocketVerdict::NoTarget);
    u = t;
    u.hostile = false;
    CHECK(CanLift(u) == PocketVerdict::NoTarget);
    u = t;
    u.humanoid = u.hasPocketLoot = false;               // a beast has no pockets
    CHECK(CanLift(u) == PocketVerdict::NoPockets);
    // The Utility's Pick Pocket keeps vanilla's rule: stealthed, out of combat, behind.
    u = t;
    u.lightFingers = false;
    CHECK(CanLift(u) == PocketVerdict::NeedsStealth);
    u.inCombat = false;
    u.stealthed = true;
    u.behind = true;
    CHECK(CanLift(u) == PocketVerdict::Ok);
}

TEST(lockpicks_open_vanilla_locks_never_a_homestead)
{
    CHECK(PickOpens(LockKind::VanillaLockbox, 175, 175, false));
    CHECK(!PickOpens(LockKind::VanillaLockbox, 225, 175, false));
    CHECK(PickOpens(LockKind::VanillaLockbox, 225, 175, true));     // Light Fingers' +50
    CHECK(!PickOpens(LockKind::VanillaChest, 226, 175, true));
    CHECK(PickOpens(LockKind::VanillaDoor, 1, 1, false));
    // A player's door or portal: whatever the skill.
    CHECK(!PickOpens(LockKind::HomesteadDoor, 0, 300, true));
    CHECK(!PickOpens(LockKind::HomesteadPortal, 0, 300, true));
    CHECK(PickLockCastMs(5000, false) == 5000 && PickLockCastMs(5000, true) == 0);
}

TEST(duelist_momentum_builds_and_ebbs)
{
    CHECK(ArchetypeResources(kRogue, 2) == std::vector<Resource>{Resource::Momentum});
    CHECK(ArchetypeResources(kRogue, 0).empty() && ArchetypeResources(kRogue, 1).empty());
    CHECK(std::string(ResourceName(Resource::Momentum)) == "Momentum");
    std::vector<Resource> const all = AllResources();
    CHECK(std::find(all.begin(), all.end(), Resource::Momentum) != all.end());
    // A blend with one Duelist pick carries the meter; a pure Assassin none.
    CHECK(KitResources(kRogue, Picks{411, 421, 431, 441, 451, 461}).empty());
    CHECK(KitResources(kRogue, Picks{411, 0, 433, 0, 0, 0}) == std::vector<Resource>{Resource::Momentum});
    CHECK_NEAR(MomentumMultiplier(0), 1.f, 1e-6f);
    CHECK_NEAR(MomentumMultiplier(5), 1.25f, 1e-6f);
    CHECK_NEAR(MomentumMultiplier(8), 1.4f, 1e-6f);
    CHECK_NEAR(MomentumMultiplier(200), 1.4f, 1e-6f);
    CHECK(MomentumAfter(5, 0) == 5 && MomentumAfter(5, 2000) == 5);
    CHECK(MomentumAfter(5, 2001) == 4 && MomentumAfter(5, 3001) == 3);
    CHECK(MomentumAfter(5, 60000) == 0 && MomentumAfter(0, 5000) == 0);
    Meter m{Resource::Momentum, 0, kMomentumMax};
    for (int i = 0; i < 9; ++i)
        m.Add(1);
    CHECK(m.value == kMomentumMax && m.Full());
    // No Duelist option spends it: it is a buff that pays by itself.
    for (uint8_t tier = 1; tier <= kTiers; ++tier)
        CHECK(CastCost(OptionId(kRogue, tier, 2)).first == Resource::None);
}

TEST(duelist_riposte_parries_blows_not_bullets)
{
    CHECK(Parries(true, true, true));
    CHECK(!Parries(true, false, true));     // a bullet is no blow to parry
    CHECK(!Parries(true, true, false));     // from behind
    CHECK(!Parries(false, true, true));
    CHECK(kRiposteMs == 1500 && kLungeRange > kLungeMinRange);
}

TEST(bard_characters_move_onto_the_duelist)
{
    // The ids stay: a Bard kit's picks and the options bought are the Duelist's, tier by tier.
    Picks const bard{413, 423, 433, 443, 453, 463};
    std::set<uint16_t> const learned(bard.begin(), bard.end());
    CHECK(ValidatePicks(kRogue, 60, learned, bard) == PickError::None);
    CHECK(Sanitized(kRogue, 60, learned, bard) == bard);
    for (uint16_t id : bard)
        CHECK(FindOption(id)->archetype == 2 && std::string(ArchetypeName(kRogue, 2)) == "Duelist");
    // A blend keeps its Assassin and Trickster picks too.
    Picks const blend{411, 423, 432, 0, 453, 0};
    std::set<uint16_t> const bought(blend.begin(), blend.end());
    CHECK(Sanitized(kRogue, 50, bought, blend) == blend);

    // Bar buttons: each instrument (any rank: `topOf` folds it onto its chain) gets the Duelist
    // option of its tier when known, else it is cleared; other buttons are left alone.
    auto topOf = [](uint32_t s) { return s == 13158u ? 13159u : s; };   // a lower "rank"
    std::set<uint32_t> const knows = {11294, 22120, 14251, 13877, 13750, 11269, 1725, 25351};
    auto known = [&](uint32_t s) { return knows.count(s) != 0; };
    std::vector<std::pair<uint8_t, uint32_t>> const bar = {
        {3, 13159}, {4, 19779}, {5, 17512}, {6, 10293}, {7, 18946}, {8, 26188},
        {9, 1787}, {14, 13158}, {20, 25300}, {21, 8643}, {22, 21655}, {23, 2094}, {24, 700}};
    std::vector<std::pair<uint8_t, uint32_t>> const moves = MigrateRetiredButtons(kRogue, bar, topOf, known);
    std::vector<std::pair<uint8_t, uint32_t>> const want = {
        {3, 11294}, {4, 22120}, {5, 14251}, {6, 13877}, {7, 0}, {8, 13750},
        {14, 11294}, {20, 11269}, {21, 1725}, {22, 25351}, {23, 0}, {24, 0}};
    CHECK(moves == want);       // Blind's Satchel is not known here: cleared
    // Run again on the moved bar: nothing more to do (safe at every login).
    std::vector<std::pair<uint8_t, uint32_t>> after;
    for (auto const& b : bar)
    {
        auto m = std::find_if(moves.begin(), moves.end(), [&](auto const& x) { return x.first == b.first; });
        if (m == moves.end())
            after.push_back(b);
        else if (m->second)
            after.push_back(*m);
    }
    CHECK(MigrateRetiredButtons(kRogue, after, topOf, known).empty());
    // Another class's bar is not the rogue's business.
    CHECK(MigrateRetiredButtons(kMage, bar, topOf, known).empty());
    CHECK(RetiredReplacement(kMage, 13159) == 0);
}
