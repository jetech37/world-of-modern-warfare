/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 *
 * The whole companion catalogue, line by line: every species' stages, mounts, abilities, basic
 * attack, traits, items and index entry hold together (design/COMPANION_CATALOG.md).
 */

#include "Test.h"
#include "fusion/CompanionIndex.h"
#include "fusion/CompanionKeeper.h"
#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>

using namespace Fusion;
using namespace Fusion::Companions;

namespace
{
    // 1.12's mount spells the catalogue rides on: 60% and 100%.
    std::set<uint32_t> const kMountSpells = {580, 458, 6777, 8394, 10793, 10799, 10969, 17462, 18990};
    std::set<uint32_t> const kSwiftMountSpells = {23250, 23229, 23239, 23221, 23219, 23241, 23223, 17465, 23249};

    bool IsMountFamily(Family f)
    {
        return f == Family::Kodo || f == Family::Horse || f == Family::Mechanostrider || f == Family::SkeletalHorse ||
            f == Family::Ram;
    }

    std::string Named(uint32_t id) { return "#" + std::to_string(id); }

    // A deterministic roller for the trait rolls.
    Roller Lcg(uint32_t seed)
    {
        auto state = std::make_shared<uint32_t>(seed ? seed : 1);
        return [state]() {
            *state = *state * 1664525u + 1013904223u;
            return float(*state >> 8) / float(1u << 24);
        };
    }

    // Whom an ability kind may be aimed at.
    bool AimFits(Ability const& a)
    {
        using K = AbilityKind;
        using T = AbilityAim;
        switch (a.kind)
        {
            case K::Heal: return a.aim == T::Owner || a.aim == T::Self || a.aim == T::Around;
            case K::Buff: return a.aim == T::Owner || a.aim == T::Self || a.aim == T::Around;
            case K::Defensive: return a.aim == T::Self;
            case K::Taunt: return a.aim == T::Enemy || a.aim == T::Around;
            case K::GapCloser: return a.aim == T::Enemy || a.aim == T::Self;
            case K::Dispel: return a.aim == T::Enemy || a.aim == T::Owner;
            case K::Mounted: return false;
            default: return a.aim != T::Owner && a.aim != T::Self ? true : a.kind == K::AoE;
        }
    }
}

TEST(companion_catalog_every_stage_has_a_creature_and_a_sane_size)
{
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        CHECK(sp.stageCount >= 1 && sp.stageCount <= kMaxStages);
        CHECK(!sp.name.empty());
        for (uint8_t st = 0; st < kMaxStages; ++st)
        {
            if (st >= sp.stageCount)
            {
                CHECK(sp.stages[st].creature == 0 && !sp.stages[st].rideable);
                continue;
            }
            CHECK(sp.stages[st].creature != 0);
            CHECK(sp.stages[st].scale >= 0.15f && sp.stages[st].scale <= 2.5f);
            // With the size traits on top it still stays drawable.
            CompanionState s;
            s.species = sp.id;
            s.stage = st;
            s.traits = TraitBit(Trait::Giant);
            CHECK(sp.stages[st].scale * ModsOf(s.traits).scale <= 3.f);
            CHECK(LookCreature(StarterSpecies(), sp, s) == sp.stages[st].creature);
        }
        // Every stage creature leads back to its species in the index's creature map.
        Index::CreatureMap const map = Index::MapCreatures(StarterSpecies());
        for (uint8_t st = 0; st < sp.stageCount; ++st)
        {
            auto it = map.find(sp.stages[st].creature);
            CHECK(it != map.end());
            if (it != map.end())
                CHECK(std::find(it->second.begin(), it->second.end(), sp.id) != it->second.end());
        }
    }
}

TEST(companion_catalog_mounts_ride_their_last_stage_with_vanilla_spells)
{
    size_t rideable = 0;
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        int ridden = -1;
        for (uint8_t st = 0; st < sp.stageCount; ++st)
            if (sp.stages[st].rideable)
            {
                CHECK(ridden < 0);      // one mount stage at most
                ridden = st;
            }
        if (IsMountFamily(sp.family))
            CHECK(ridden >= 0);
        if (ridden < 0)
        {
            CompanionState s;
            s.species = sp.id;
            s.stage = uint8_t(sp.stageCount - 1);
            CHECK(!IsRideable(sp, s) && !MountedMove(sp, s).Exists());
            continue;
        }
        ++rideable;
        StageInfo const& stage = sp.stages[ridden];
        // Always the final form: a grown companion, never a pup carrying its owner.
        CHECK(ridden + 1 == sp.stageCount);
        // Ridden, it is drawn as a mount display at that display's own size: walking beside its
        // owner it must be the same size, so the mount stage is never rescaled.
        CHECK_NEAR(stage.scale, 1.f, 1e-4);
        CHECK(kMountSpells.count(stage.mountSpell) == 1);
        CHECK(kSwiftMountSpells.count(stage.swiftMountSpell) == 1);
        // A line on another family's spells (the wolf's) must show its own display.
        bool const ownSpells = stage.mountSpell != 580 || sp.family == Family::Wolf;
        if (!ownSpells)
            CHECK(stage.rideDisplay != 0);
        CompanionState s;
        s.species = sp.id;
        s.stage = uint8_t(ridden);
        CHECK(IsRideable(sp, s));
        CHECK(MountedMove(sp, s).Exists() && MountedMove(sp, s).kind == AbilityKind::Mounted);
        // The ridden display: its own as it walked, else the stage's own (the look table), else
        // the ride display, else the creature's.
        CHECK(Index::MountDisplay(sp, s, 1234, 99) == 1234);
        CHECK(Index::MountDisplay(sp, s, 0, 99) ==
            (stage.display ? stage.display : stage.rideDisplay ? stage.rideDisplay : 99u));
        // The rider's speed is vanilla's, and the swift spell at 100%.
        CHECK(RideSpell(stage, 60) == stage.mountSpell);
        CHECK(RideSpell(stage, 100) == stage.swiftMountSpell);
        // Earlier stages are no mount.
        for (uint8_t st = 0; st < ridden; ++st)
        {
            CompanionState pup = s;
            pup.stage = st;
            CHECK(!IsRideable(sp, pup) && Index::MountDisplay(sp, pup, 1234, 99) == 0);
        }
    }
    CHECK(rideable >= 9 + 20);
}

TEST(companion_catalog_tireless_carries_its_rider_lower)
{
    CompanionSpecies const* wolf = FindSpecies(StarterSpecies(), 6);
    CHECK(wolf != nullptr);
    if (!wolf)
        return;
    CompanionState s;
    s.species = 6;
    s.stage = 2;
    s.level = 40;
    RideContext ctx;
    ctx.ownerLevel = 40;
    ctx.ridingSkill = kRideSkill;
    ctx.summoned = true;
    ctx.healthShare = 0.25f;
    CHECK(CanRide(*wolf, s, ctx) == RideBlock::Worn);
    s.traits = TraitBit(Trait::Tireless);
    CHECK(CanRide(*wolf, s, ctx) == RideBlock::None);
    ctx.healthShare = 0.1f;
    CHECK(CanRide(*wolf, s, ctx) == RideBlock::Worn);
}

TEST(companion_catalog_every_reached_stage_offers_two_real_abilities)
{
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        MovesetInfo const& m = MovesetData(MovesetOf(sp));
        CHECK(m.id != Moveset::None);
        for (uint8_t st = 0; st < sp.stageCount; ++st)
        {
            std::set<std::string> names;
            for (Ability const& a : m.stages[st])
            {
                CHECK(a.Exists());
                CHECK(*a.name != 0);
                names.insert(a.name);
                CHECK(a.kind != AbilityKind::Mounted);
                CHECK(AimFits(a));
                CHECK(a.cooldown > 0);
                // A heal heals something; numbers within the role budgets' reach.
                if (a.kind == AbilityKind::Heal)
                    CHECK(a.pct > 0 || a.dotPct > 0);
                CHECK(a.pct <= 250 && a.dotPct <= 200);
                // A stun never longer than 3 s on players; the catalog's are within 4 s.
                if (a.kind == AbilityKind::Stun || a.kind == AbilityKind::AoEStun)
                    CHECK(a.duration <= 10);
                // Something sets it off by itself, unless it is a mount's.
                AutocastContext c;
                c.hasTarget = true;
                c.targetHealth = 0.3f;
                c.targetCasting = true;
                c.targetInMeleeWithOwner = true;
                c.targetApproachingOwner = true;
                c.targetDistance = 20.f;
                c.ownerHitByOther = true;
                c.allyUnderHalfAttacked = true;
                c.ownerHealth = 0.3f;
                c.companionHealth = 0.3f;
                c.alliesUnder70 = 3;
                c.enemiesNear = 3;
                c.enemiesInMeleeWithUs = 3;
                c.attackersOnCompanion = 3;
                c.ownerInCombat = true;
                c.allyDispellable = true;
                c.enemyDispellable = true;
                bool fires = ShouldAutocast(a, c);
                if (!fires)
                {
                    c.targetApproachingOwner = false;      // gap closers wait for a far target
                    fires = ShouldAutocast(a, c);
                }
                CHECK(fires);
            }
            CHECK(names.size() == 2);   // two different choices
            // Picking either fills the stage's slot with it.
            for (uint8_t option = 1; option <= 2; ++option)
            {
                CompanionState s;
                s.species = sp.id;
                s.stage = st;
                for (uint8_t k = 0; k < st; ++k)
                    s.picks[k] = 1;
                CHECK(PendingPick(sp, s) == st);
                CHECK(Pick(sp, s, st, option));
                CHECK(AbilityInSlot(sp, s, uint8_t(PetBarSlot::Ability1 + st)).spell == m.stages[st][option - 1].spell);
            }
        }
        // Stages a line does not have offer nothing.
        for (uint8_t st = sp.stageCount; st < kMaxStages; ++st)
        {
            CompanionState s;
            s.species = sp.id;
            s.stage = uint8_t(sp.stageCount - 1);
            CHECK(!AbilityInSlot(sp, s, uint8_t(PetBarSlot::Ability1 + st)).Exists());
        }
    }
}

TEST(companion_catalog_casters_shoot_from_range_and_the_rest_swing)
{
    std::set<uint16_t> meleeCasters;
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        uint32_t const bolt = RangedBasicSpell(sp);
        if (sp.role != Role::Ranged)
        {
            CHECK(bolt == 0);
            continue;
        }
        if (!bolt)
        {
            meleeCasters.insert(sp.id);
            continue;
        }
        CHECK(IsRangedBasic(bolt));
        CHECK(bolt == MovesetData(MovesetOf(sp)).basicSpell);
    }
    // The ranged lines whose catalog basic is a claw or a strike: the clockwork squirrel, rocs,
    // quilboar, harpies, moonkin.
    CHECK(meleeCasters == std::set<uint16_t>({28, 125, 157, 159, 163}));
    // The whelps breathe Firebolt, spider tanks shoot, trolls throw; a Gunner shoots.
    CHECK(RangedBasicSpell(*FindSpecies(StarterSpecies(), 1)) == 3110);
    CHECK(RangedBasicSpell(*FindSpecies(StarterSpecies(), 151)) == 6660);
    CHECK(RangedBasicSpell(*FindSpecies(StarterSpecies(), 160)) == 10277);
    CHECK(RangedBasicSpell(*FindSpecies(StarterSpecies(), 6)) == 0);
    CHECK(IsRangedBasic(kGunnerShotSpell));
    CHECK(kRangedFireRange > kRangedReach);
    // A Gunner shoots whatever its line's basic was; a Gunner bit on a line that cannot roll it
    // changes nothing.
    CompanionSpecies const& gnoll = *FindSpecies(StarterSpecies(), 152);
    CompanionSpecies const& wolf = *FindSpecies(StarterSpecies(), 6);
    CompanionState s;
    CHECK(RangedAttackSpell(gnoll, s) == 0);
    s.traits = TraitBit(Trait::Gunner);
    CHECK(RangedAttackSpell(gnoll, s) == kGunnerShotSpell);
    CHECK(RangedAttackSpell(*FindSpecies(StarterSpecies(), 160), s) == kGunnerShotSpell);
    CHECK(RangedAttackSpell(wolf, s) == 0);
    s.traits = 0;
    CHECK(RangedAttackSpell(*FindSpecies(StarterSpecies(), 1), s) == 3110);
}

TEST(companion_catalog_evolutions_climb_with_the_family_catalyst)
{
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        uint8_t last = 0;
        for (uint8_t e = 0; e + 1 < sp.stageCount; ++e)
        {
            EvolutionRule const& r = sp.evolutions[e];
            CHECK(r.Exists());
            CHECK(r.level > last && r.level <= kMaxLevel);
            last = r.level;
            if (r.item)
            {
                CHECK(IsCatalystItem(r.item));
                CHECK(r.itemCount >= 1);
            }
            else
                CHECK(r.itemCount == 0);
            if (r.condition == Condition::KillType)
                CHECK(r.conditionCount > 0 && r.conditionType != CreatureType::None);
            else
                CHECK(r.conditionCount == 0);
            // The keeper's floor for the stage is the rule's level.
            CHECK(Keeper::MinLevelForStage(sp, uint8_t(e + 1)) == r.level);
        }
        CHECK(Keeper::MinLevelForStage(sp, 0) == 1);
        // A three-stage line breeds, so it names its family reagent.
        if (sp.stageCount >= 3)
            CHECK(BreedReagent(sp) != 0);
    }
}

TEST(companion_catalog_trait_rolls_are_always_valid)
{
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        size_t eligible = 0;
        for (uint8_t t = 1; t <= kTraitCount; ++t)
            if (TraitEligible(Trait(t), sp))
                ++eligible;
        CHECK(eligible >= 15);
        CHECK(TraitEligible(Trait::Gunner, sp) == IsHumanoidFamily(sp.family));
        CHECK(TraitEligible(Trait::Tireless, sp) == IsRideableLine(sp));
        Roller roll = Lcg(sp.id * 7919u);
        for (uint8_t r = 0; r <= uint8_t(Rarity::Legendary); ++r)
            for (int i = 0; i < 20; ++i)
            {
                uint8_t twin = 0;
                uint32_t const mask = RollTraits(sp, Rarity(r), roll, twin);
                CHECK(ValidTraits(mask, sp));
                CHECK(RarityOf(mask) == Rarity(r));
                if (HasTrait(mask, Trait::TwinSouled))
                    CHECK(twin >= 1 && twin <= sp.stageCount);
                else
                    CHECK(twin == 0);
            }
    }
}

TEST(companion_catalog_stats_stay_under_a_hunter_pet_everywhere)
{
    uint32_t const strongest = TraitBit(Trait::Primal) | TraitBit(Trait::Giant) | TraitBit(Trait::Ironhide) |
        TraitBit(Trait::Ferocious) | TraitBit(Trait::Swift);
    for (CompanionSpecies const& sp : StarterSpecies())
        for (uint8_t st = 0; st < sp.stageCount; ++st)
            for (uint8_t level = 1; level <= kMaxLevel; level = uint8_t(level + 7))
                for (uint32_t traits : {0u, strongest})
                {
                    CompanionState s;
                    s.species = sp.id;
                    s.stage = st;
                    s.level = level;
                    s.traits = traits;
                    CompanionStats const c = StatsWithTraits(sp, s, level);
                    CompanionStats const pet = HunterPetBaseline(level);
                    CHECK(c.health > 0 && c.health < pet.health);
                    CHECK(c.armor < pet.armor);
                    CHECK(c.Dps() > 0.f && c.Dps() < pet.Dps());
                    CHECK(c.damageMin <= c.damageMax && c.attackMs >= 1000);
                }
}

TEST(companion_catalog_items_seeds_and_index_entries)
{
    Index::SeenMap seen;
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        CHECK(IsCompanionItem(sp.item) && FindByItem(StarterSpecies(), sp.item) == &sp);
        CompanionState fresh;
        CHECK(FreshState(StarterSpecies(), StarterItems(), sp.item, fresh) == &sp);
        CHECK(fresh.species == sp.id && fresh.stage == 0 && fresh.level == 1);
        // Every stage at its lowest and highest level reads back off the item.
        for (uint8_t st = 0; st < sp.stageCount; ++st)
            for (uint8_t level : {uint8_t(1), uint8_t(37), kMaxLevel})
            {
                CompanionState s;
                s.species = sp.id;
                s.stage = st;
                s.level = level;
                CompanionState back;
                CHECK(FromItemSeed(ItemSeed(s), back));
                CHECK(back.species == sp.id && back.stage == st && back.level == level);
            }
        // A game master's entry: every stage named, reached and with its two choices, the mount
        // marked and its move named.
        Index::Entry const e = Index::MakeEntry(sp, seen, true, Named, Named, Named);
        CHECK(e.seen != Index::Seen::Unknown);
        CHECK(e.name == sp.name && e.item == sp.item && e.stages.size() == sp.stageCount);
        CHECK(!e.trait.empty());
        CHECK(e.mounted.empty() == !IsRideableLine(sp));
        for (uint8_t st = 0; st < e.stages.size(); ++st)
        {
            CHECK(e.stages[st].creature == sp.stages[st].creature);
            CHECK(!e.stages[st].name.empty() && !e.stages[st].reach.empty());
            CHECK(!e.stages[st].abilities[0].empty() && !e.stages[st].abilities[1].empty());
            CHECK(e.stages[st].rideable == sp.stages[st].rideable);
            if (st > 0)
                CHECK(e.stages[st].reach.rfind("Level " + std::to_string(sp.evolutions[st - 1].level), 0) == 0);
        }
        CHECK(e.eligible != 0);
    }
    // The mounts' own items start ridden, at a stage that is a mount.
    for (ItemStart const& i : StarterItems())
    {
        CompanionState s;
        CompanionSpecies const* sp = FreshState(StarterSpecies(), StarterItems(), i.item, s);
        CHECK(sp && sp->id == i.species);
        if (!sp)
            continue;
        CHECK(s.stage == i.stage && s.level == i.level && s.stage < sp->stageCount);
        CHECK(IsRideable(*sp, s));
    }
}

TEST(companion_catalog_hybrids_wear_the_other_line_where_they_share_a_model)
{
    auto const& all = StarterSpecies();
    CompanionSpecies const& nightsaber = *FindSpecies(all, 7);
    CompanionSpecies const& wolf = *FindSpecies(all, 6);
    CompanionState s;
    s.species = 7;
    s.stage = 1;
    s.hybrid = 8;   // a Frostsaber's coloring: they share every stage
    CHECK(LookCreature(all, nightsaber, s) == FindSpecies(all, 8)->stages[1].creature);
    // Wolf and coyote share the middle stage only.
    CompanionState w;
    w.species = 6;
    w.hybrid = 111;
    w.stage = 0;
    CHECK(LookCreature(all, wolf, w) == wolf.stages[0].creature);
    w.stage = 1;
    CHECK(LookCreature(all, wolf, w) == FindSpecies(all, 111)->stages[1].creature);
    // No coloring, or one it cannot wear: its own.
    w.hybrid = 0;
    CHECK(LookCreature(all, wolf, w) == wolf.stages[1].creature);
    w.hybrid = 1;
    CHECK(LookCreature(all, wolf, w) == wolf.stages[1].creature);
    w.stage = 5;
    CHECK(LookCreature(all, wolf, w) == 0);
}

TEST(companion_catalog_item_texts_say_what_the_companion_is)
{
    // Every companion item's tooltip: a rideable line says it grows into (or is) a mount, and no
    // text sends a player to the companion bar, which is gone (the pet bar rides it).
    std::map<uint32_t, std::string> description;
    for (char const* file : {"02_companions.sql", "08_companion_lines.sql", "20_companion_fixes.sql", "31_vanilla_companions.sql",
             "35_flying_companions.sql"})
    {
        std::ifstream in(std::string(FUSION_VECTORS_DIR "/../../../sql/custom/azeroth_warfare/") + file);
        CHECK(in.good());
        std::string line;
        while (std::getline(in, line))
        {
            CHECK(line.find("companion bar") == std::string::npos || line.rfind("--", 0) == 0);
            if (line.rfind("(90", 0) != 0 && line.rfind("(91", 0) != 0)
                continue;
            // (entry, patch, class, subclass, 'name', 'description', ...
            // A quote doubled inside a string ('Lil'' Smoky') is no end of it.
            auto close = [&line](size_t open) {
                size_t at = open == std::string::npos ? open : open + 1;
                while ((at = line.find('\'', at)) != std::string::npos && at + 1 < line.size() && line[at + 1] == '\'')
                    at += 2;
                return at;
            };
            size_t const q1 = line.find('\''), q2 = close(q1);
            size_t const q3 = q2 == std::string::npos ? q2 : line.find('\'', q2 + 1), q4 = close(q3);
            if (q4 == std::string::npos)
                continue;
            std::string text = line.substr(q3 + 1, q4 - q3 - 1);
            for (size_t at = 0; (at = text.find("''", at)) != std::string::npos; ++at)
                text.erase(at, 1);
            description[uint32_t(std::stoul(line.substr(1)))] = text;
        }
    }
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        auto it = description.find(sp.item);
        CHECK(it != description.end());
        if (it == description.end())
            continue;
        CHECK(it->second.find(sp.name) != std::string::npos);
        bool const saysMount = it->second.find("rideable") != std::string::npos;
        CHECK(saysMount == IsRideableLine(sp));
    }
    for (ItemStart const& i : StarterItems())
    {
        auto it = description.find(i.item);
        CHECK(it != description.end() && it->second.find("mount") != std::string::npos);
    }
}

TEST(companion_catalog_capped_means_a_full_bar_at_the_owners_level)
{
    CompanionState s;
    s.level = 20;
    s.xp = XpToNext(20);
    CHECK(IsCapped(s, 20));
    CHECK(IsCapped(s, 15));     // a traded one above its owner, bar full
    CHECK(!IsCapped(s, 21));
    s.xp = XpToNext(20) - 1;
    CHECK(!IsCapped(s, 20));
    s.level = kMaxLevel;
    s.xp = 0;
    CHECK(!IsCapped(s, kMaxLevel));
}
