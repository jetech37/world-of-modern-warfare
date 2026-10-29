/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/CompanionTraits.h"

#include <map>

using namespace Fusion;
using namespace Fusion::Companions;

namespace
{
    CompanionSpecies const& Sp(uint16_t id)
    {
        static CompanionSpecies none;
        CompanionSpecies const* s = FindSpecies(StarterSpecies(), id);
        return s ? *s : none;
    }

    // A deterministic [0, 1) sequence.
    struct Lcg
    {
        uint32_t state;
        float operator()()
        {
            state = state * 1664525u + 1013904223u;
            return float(state >> 8) / float(1u << 24);
        }
    };
}

TEST(companion_trait_table_and_rarity)
{
    CHECK(TraitData(Trait::Swift).kind == TraitKind::Combat);
    CHECK(std::string(TraitData(Trait::PhoenixBlooded).name) == "Phoenix-blooded");
    for (uint8_t i = 1; i <= kTraitCount; ++i)
        CHECK(TraitData(Trait(i)).id == Trait(i));
    CHECK(RarityOf(0) == Rarity::Common);
    CHECK(RarityOf(TraitBit(Trait::Swift)) == Rarity::Uncommon);
    CHECK(RarityOf(TraitBit(Trait::Swift) | TraitBit(Trait::Venomous)) == Rarity::Rare);
    CHECK(RarityOf(TraitBit(Trait::Swift) | TraitBit(Trait::Venomous) | TraitBit(Trait::Loyal)) == Rarity::Epic);
    CHECK(RarityOf(TraitBit(Trait::Swift) | TraitBit(Trait::Alpha)) == Rarity::Legendary);
    CHECK(TraitLine(TraitBit(Trait::Swift) | TraitBit(Trait::Venomous)) == "Rare: Swift, Venomous");
    CHECK(ComboLine(TraitBit(Trait::Swift) | TraitBit(Trait::Venomous)) == "Viper: Venomous + Swift");
    CHECK(TraitLine(0).empty());

    // 3b's rarity table, at its edges.
    CHECK(RollRarity(CreatureRank::Normal, false, 0.59f) == Rarity::Common);
    CHECK(RollRarity(CreatureRank::Normal, false, 0.6f) == Rarity::Uncommon);
    CHECK(RollRarity(CreatureRank::Normal, false, 0.998f) == Rarity::Legendary);
    CHECK(RollRarity(CreatureRank::Elite, false, 0.34f) == Rarity::Common);
    CHECK(RollRarity(CreatureRank::RareElite, false, 0.14f) == Rarity::Common);
    CHECK(RollRarity(CreatureRank::DungeonBoss, false, 0.96f) == Rarity::Legendary);
    CHECK(RollRarity(CreatureRank::RareElite, true, 0.5f) == Rarity::Common);   // a critter rolls low
}

TEST(companion_trait_restrictions_and_exclusions)
{
    CompanionSpecies const& wolf = Sp(6), &ember = Sp(19), &water = Sp(20), &squirrel = Sp(28);
    CompanionSpecies const& gnoll = Sp(152), &skeleton = Sp(145), &ooze = Sp(25), &panther = Sp(9);
    CHECK(TraitEligible(Trait::Venomous, wolf) && !TraitEligible(Trait::Venomous, squirrel));
    CHECK(!TraitEligible(Trait::FrostTouched, ember) && !TraitEligible(Trait::FireTouched, water));
    CHECK(!TraitEligible(Trait::Bloodthirsty, ember) && !TraitEligible(Trait::Bloodthirsty, ooze));
    CHECK(!TraitEligible(Trait::ShadowTouched, squirrel) && !TraitEligible(Trait::Albino, squirrel));
    CHECK(!TraitEligible(Trait::Albino, skeleton) && TraitEligible(Trait::Shadowcoat, skeleton));
    CHECK(TraitEligible(Trait::Gunner, gnoll) && !TraitEligible(Trait::Gunner, wolf));
    CHECK(TraitEligible(Trait::Gunner, Sp(23)));          // the murloc too
    CHECK(TraitEligible(Trait::Tireless, wolf) && !TraitEligible(Trait::Tireless, panther));
    CHECK(TraitsConflict(Trait::Giant, Trait::Runt));
    CHECK(TraitsConflict(Trait::Albino, Trait::Gilded));
    CHECK(TraitsConflict(Trait::FireTouched, Trait::StormTouched));
    CHECK(!TraitsConflict(Trait::FireTouched, Trait::Spellward));
    CHECK(!ValidTraits(TraitBit(Trait::Giant) | TraitBit(Trait::Runt), wolf));
    CHECK(ValidTraits(TraitBit(Trait::Giant) | TraitBit(Trait::Ironhide), wolf));
}

TEST(companion_rolls_follow_rarity_counts_and_rules)
{
    Lcg rng{7};
    auto roll = [&rng]() { return rng(); };
    std::map<Rarity, int> counts;
    for (CompanionSpecies const& sp : StarterSpecies())
        for (int r = 0; r <= int(Rarity::Legendary); ++r)
            for (int i = 0; i < 40; ++i)
            {
                uint8_t twin = 0;
                uint32_t const mask = RollTraits(sp, Rarity(r), roll, twin);
                CHECK(ValidTraits(mask, sp));
                CHECK(RarityOf(mask) == Rarity(r));
                if (HasTrait(mask, Trait::TwinSouled))
                    CHECK(twin >= 1 && twin <= sp.stageCount);
                else
                    CHECK(twin == 0);
                // Fire elementals and imps never draw another element.
                if (sp.family == Family::FireElemental || sp.family == Family::Imp)
                    CHECK(!HasTrait(mask, Trait::StormTouched) && !HasTrait(mask, Trait::ShadowTouched));
            }
    // A drop's whole roll lands in the state.
    CompanionState s;
    RollDrop(Sp(6), s, CreatureRank::RareElite, false, roll);
    CHECK(s.rarity == uint8_t(RarityOf(s.traits)));
    (void)counts;
}

TEST(companion_traits_never_reach_a_hunter_pet)
{
    // Every combination of up to three traits, on every species, stage and some levels.
    std::vector<uint32_t> masks{0};
    for (uint8_t a = 1; a <= kTraitCount; ++a)
    {
        masks.push_back(TraitBit(Trait(a)));
        for (uint8_t b = a + 1; b <= kTraitCount; ++b)
        {
            masks.push_back(TraitBit(Trait(a)) | TraitBit(Trait(b)));
            for (uint8_t c = b + 1; c <= kTraitCount; ++c)
                masks.push_back(TraitBit(Trait(a)) | TraitBit(Trait(b)) | TraitBit(Trait(c)));
        }
    }
    int checked = 0;
    for (uint16_t id : {6, 10, 19, 145, 155})
    {
        CompanionSpecies const& sp = Sp(id);
        for (uint32_t m : masks)
            for (uint8_t stage = 0; stage < sp.stageCount; ++stage)
                for (uint8_t level : {1, 20, 40, 60})
                {
                    CompanionState s;
                    s.stage = stage;
                    s.traits = m;
                    CompanionStats const st = StatsWithTraits(sp, s, level);
                    CompanionStats const pet = HunterPetBaseline(level);
                    CHECK(st.health < pet.health && st.armor < pet.armor && st.Dps() < pet.Dps());
                    ++checked;
                }
    }
    CHECK(checked > 100000);
    // Traits do what they say where there is headroom.
    CompanionState plain, swift;
    plain.stage = swift.stage = 1;
    swift.traits = TraitBit(Trait::Swift);
    CHECK(StatsWithTraits(Sp(6), swift, 30).attackMs < StatsWithTraits(Sp(6), plain, 30).attackMs);
    CompanionState primal = plain;
    primal.traits = TraitBit(Trait::Primal);
    CHECK(StatsWithTraits(Sp(6), primal, 30).health > StatsWithTraits(Sp(6), plain, 30).health);
    CHECK(ModsOf(TraitBit(Trait::Giant)).scale > 1.f && ModsOf(TraitBit(Trait::Runt)).scale < 1.f);
    CHECK(FaintRecoverSeconds(TraitBit(Trait::Loyal), 3600) == 1200 && FaintRecoverSeconds(0, 3600) == 3600);
    CHECK(FaintRecoverSeconds(TraitBit(Trait::Loyal), 2) == 1 && FaintRecoverSeconds(0, 0) == 1);
    CHECK(MountSpeedWithTraits(60, TraitBit(Trait::Tireless)) == 70);
    CHECK(MountSpeedWithTraits(100, TraitBit(Trait::Tireless) | TraitBit(Trait::Surefooted)) == 115);
    CHECK(CombosOf(TraitBit(Trait::Giant) | TraitBit(Trait::Ironhide)).size() == 1);
    CHECK(HasCombo(TraitBit(Trait::PhoenixBlooded) | TraitBit(Trait::FireTouched), Combo::Ashborn));
}

TEST(companion_breeding_rules_and_offspring)
{
    CompanionSpecies const& wolf = Sp(6), &coyote = Sp(111), &bear = Sp(10);
    CompanionState a, b;
    a.species = 6;
    b.species = 111;
    a.stage = b.stage = 2;
    a.level = 50;
    b.level = 40;
    BreedContext ctx;
    ctx.now = 1000000;
    ctx.reagentsA = ctx.reagentsB = 5;
    ctx.money = BreedFee(50, 40);
    CHECK(BreedFee(50, 40) == 45000);           // 10 silver times the average level
    CHECK(CanBreed(wolf, a, coyote, b, false, ctx) == BreedBlock::None);
    CHECK(CanBreed(wolf, a, wolf, a, true, ctx) == BreedBlock::SameCompanion);
    CompanionState young = a;
    young.stage = 1;
    CHECK(CanBreed(wolf, young, coyote, b, false, ctx) == BreedBlock::NotAdult);
    CompanionState cub = b;
    cub.species = 10;
    CHECK(CanBreed(wolf, a, bear, cub, false, ctx) == BreedBlock::OtherFamily);
    CompanionState tired = a;
    tired.breedCount = kBreedMax;
    CHECK(CanBreed(wolf, tired, coyote, b, false, ctx) == BreedBlock::Exhausted);
    CompanionState rest = a;
    MarkBred(rest, ctx.now);
    CHECK(rest.breedCount == 1 && rest.breedReadyAt == ctx.now + kBreedRestSeconds);
    CHECK(CanBreed(wolf, rest, coyote, b, false, ctx) == BreedBlock::Resting);
    BreedContext poor = ctx;
    poor.reagentsB = 4;
    CHECK(CanBreed(wolf, a, coyote, b, false, poor) == BreedBlock::NoReagents);
    poor = ctx;
    poor.money = ctx.money - 1;
    CHECK(CanBreed(wolf, a, coyote, b, false, poor) == BreedBlock::NoMoney);
    // Elementals and demons breed inside their group.
    CHECK(BreedGroup(Family::FireElemental) == BreedGroup(Family::WaterElemental));
    CHECK(BreedGroup(Family::Felhound) == BreedGroup(Family::Darkhound));
    CHECK(BreedGroup(Family::Wolf) != BreedGroup(Family::Cat));
    CHECK(BreedReagent(wolf) != 0 && BreedReagent(Sp(152)) != 0);

    // Offspring: the first parent's species, a fresh level 1, its traits from the parents.
    a.traits = TraitBit(Trait::Swift) | TraitBit(Trait::Venomous) | TraitBit(Trait::Loyal);
    b.traits = TraitBit(Trait::Ironhide) | TraitBit(Trait::Runt);
    b.generation = 2;
    Lcg rng{11};
    auto roll = [&rng]() { return rng(); };
    int traits = 0, hybrids = 0;
    int const n = 4000;
    for (int i = 0; i < n; ++i)
    {
        CompanionState kid = BreedOffspring(wolf, a, coyote, b, roll);
        CHECK(kid.species == 6 && kid.level == 1 && kid.stage == 0 && kid.generation == 3);
        CHECK(kid.parentA == 6 && kid.parentB == 111);
        CHECK(ValidTraits(kid.traits, wolf));
        CHECK(kid.rarity == uint8_t(RarityOf(kid.traits)));
        traits += int(TraitList(kid.traits).size());
        hybrids += kid.hybrid ? 1 : 0;
    }
    // Five parent traits at 35% each, plus the odd mutation: about 1.8 a pup.
    CHECK(traits > n * 16 / 10 && traits < n * 20 / 10);
    // Wolf Pup and Timber Wolf Pup share the Wolf model at stage 2: an 8% hybrid coloring.
    CHECK(HybridStages(6, 111) == 0b010);
    CHECK(hybrids > n * 5 / 100 && hybrids < n * 11 / 100);
    CHECK(HybridStages(6, 10) == 0);
}

TEST(companion_scavenger_finds_more_coin)
{
    uint32_t const scavenger = TraitBit(Trait::Scavenger);
    CHECK(ScavengedGold(1000, 0) == 1000);
    CHECK(ScavengedGold(1000, scavenger) == 1100);
    CHECK(ScavengedGold(1000, scavenger | TraitBit(Trait::Gilded)) == 1250);   // Golden Hoard
    CHECK(ScavengedGold(0, scavenger) == 0);
}
