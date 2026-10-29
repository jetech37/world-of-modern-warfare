/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"

#include <set>

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
}

TEST(companion_every_species_has_a_moveset_with_two_choices_a_stage)
{
    for (CompanionSpecies const& sp : StarterSpecies())
    {
        Moveset const m = MovesetOf(sp);
        CHECK(m != Moveset::None);
        MovesetInfo const& info = MovesetData(m);
        CHECK(info.id == m);
        CHECK(*info.trait != 0);
        for (uint8_t st = 0; st < sp.stageCount; ++st)
        {
            // Critters have one stage and its pair; every stage a species reaches offers two.
            CHECK(info.stages[st][0].Exists() && info.stages[st][1].Exists());
            for (Ability const& a : info.stages[st])
            {
                CHECK(a.kind != AbilityKind::Mounted);
                CHECK(*a.name != 0);
            }
        }
        // A rideable final form has a mounted move, never autocast.
        if (IsRideableLine(sp))
        {
            CHECK(info.mounted.Exists());
            CHECK(info.mounted.kind == AbilityKind::Mounted);
        }
    }
    // The catalog's specifics.
    CHECK(MovesetOf(Sp(125)) == Moveset::Roc);
    CHECK(MovesetOf(Sp(137)) == Moveset::EarthElemental);
    CHECK(MovesetOf(Sp(150)) == Moveset::Shredder);
    CHECK(MovesetData(Moveset::Spider).stages[0][0].spell == 4167);
    CHECK(MovesetData(Moveset::Bear).stages[2][0].spell == 5211);
    CHECK(MovesetData(Moveset::Felhound).stages[1][0].kind == AbilityKind::Interrupt);
}

TEST(companion_picks_one_of_two_at_each_stage)
{
    CompanionSpecies const& wolf = Sp(6);
    CompanionState s;
    s.species = 6;
    CHECK(PendingPick(wolf, s) == 0);           // stage 1's pick is owed at the first summon
    CHECK(ActiveAbilities(wolf, s).empty());
    CHECK(!Pick(wolf, s, 1, 1));                // not reached
    CHECK(!Pick(wolf, s, 0, 3));                // no third option
    CHECK(Pick(wolf, s, 0, 2));
    CHECK(!Pick(wolf, s, 0, 1));                // chosen (a stable master changes it)
    CHECK(PendingPick(wolf, s) == -1);
    std::vector<Ability> a = ActiveAbilities(wolf, s);
    CHECK(a.size() == 1 && a[0].spell == 3604);  // Tendon Rip
    Evolve(s);
    CHECK(PendingPick(wolf, s) == 1);
    CHECK(Pick(wolf, s, 1, 1));
    Evolve(s);
    CHECK(Pick(wolf, s, 2, 1));
    a = ActiveAbilities(wolf, s);
    CHECK(a.size() == 3 && a[2].spell == 22911);  // Charge
    CHECK(AbilityInSlot(wolf, s, PetBarSlot::Ability3).spell == 22911);
    CHECK(!AbilityInSlot(wolf, s, PetBarSlot::Twin).Exists());
    CHECK(MountedMove(wolf, s).Exists());        // its final form is a mount
    s.stage = 1;
    CHECK(!MountedMove(wolf, s).Exists());

    // Twin-souled: both options of its stage.
    s.stage = 2;
    s.traits = TraitBit(Trait::TwinSouled);
    s.twinStage = 1;
    CHECK(AbilityInSlot(wolf, s, PetBarSlot::Twin).spell == 24604);     // Furious Howl, the one not picked
    CHECK(ActiveAbilities(wolf, s).size() == 4);
}

TEST(companion_ability_numbers_scale_with_its_basic_hit)
{
    CompanionStats st = Stats(Role::Melee, 1, 3, 40);
    Ability const rake = MovesetData(Moveset::Cat).stages[0][0];
    CHECK(AbilityDirect(rake, st) == uint32_t(std::lround(BasicHit(st) * 0.5f)));
    CHECK(AbilityOverTime(rake, st) == uint32_t(std::lround(BasicHit(st) * 0.9f)));
    // Heals are a share of the healed one's health.
    Ability const wave = MovesetData(Moveset::WaterElemental).stages[1][1];
    CHECK(wave.kind == AbilityKind::Heal);
    CHECK(AbilityDirect(wave, st, 2000) == 300);
    // Higher level, harder hit; Primal cuts cooldowns by a fifth.
    CHECK(AbilityDirect(rake, Stats(Role::Melee, 1, 3, 60)) > AbilityDirect(rake, st));
    CHECK(AbilityCooldownMs(rake, false) == 10000 && AbilityCooldownMs(rake, true) == 8000);
}

TEST(companion_autocast_waits_for_its_moment)
{
    AutocastContext c;
    Ability const growl = MovesetData(Moveset::Bear).stages[0][0];
    c.hasTarget = true;
    CHECK(!ShouldAutocast(growl, c));
    c.ownerHitByOther = true;
    CHECK(ShouldAutocast(growl, c));
    c.targetAttacksCompanion = true;            // never on what already fights it
    CHECK(!ShouldAutocast(growl, c));

    AutocastContext h;
    Ability const renew = MovesetData(Moveset::FaerieDragon).stages[0][0];
    CHECK(!ShouldAutocast(renew, h));
    h.ownerHealth = 0.45f;
    CHECK(ShouldAutocast(renew, h));
    h.hotFromIt = true;
    CHECK(!ShouldAutocast(renew, h));

    AutocastContext r;
    Ability const web = MovesetData(Moveset::Spider).stages[0][0];
    r.hasTarget = true;
    CHECK(!ShouldAutocast(web, r));
    r.targetApproachingOwner = true;            // a charging melee enemy
    CHECK(ShouldAutocast(web, r));

    AutocastContext b;
    Ability const ravage = MovesetData(Moveset::Cat).stages[2][0];
    b.hasTarget = true;
    CHECK(!ShouldAutocast(ravage, b));
    b.targetHealth = 0.4f;
    CHECK(ShouldAutocast(ravage, b));

    AutocastContext a;
    Ability const swipe = MovesetData(Moveset::Bear).stages[1][0];
    a.enemiesNear = 1;
    CHECK(!ShouldAutocast(swipe, a));
    a.enemiesNear = 3;
    CHECK(ShouldAutocast(swipe, a));
    a.wouldBreakCC = true;                      // never breaks the owner's own crowd control
    CHECK(!ShouldAutocast(swipe, a));

    AutocastContext i;
    Ability const lock = MovesetData(Moveset::Felhound).stages[1][0];
    i.hasTarget = true;
    CHECK(!ShouldAutocast(lock, i));
    i.targetCasting = true;
    CHECK(ShouldAutocast(lock, i));

    AutocastContext f;
    Ability const scream = MovesetData(Moveset::Banshee).stages[2][0];
    f.enemiesInMeleeWithUs = 2;
    CHECK(ShouldAutocast(scream, f));
    f.dungeonOtherPacksNear = true;
    CHECK(!ShouldAutocast(scream, f));

    // Mounted moves, and anything while ridden, are manual.
    AutocastContext m;
    m.hasTarget = true;
    CHECK(!ShouldAutocast(MovesetData(Moveset::Wolf).mounted, m));
    m.riding = true;
    CHECK(!ShouldAutocast(MovesetData(Moveset::Cat).stages[0][0], m));
}

TEST(companion_pet_bar_messages_round_trip)
{
    Moves m;
    m.item = 0x0102030405060708ull;
    m.species = 6;
    m.pending = 1;
    m.twinStage = 2;
    m.traits = TraitBit(Trait::Swift);
    m.autocast = 0x3FF;
    m.picks = {2, 0, 0};
    m.spells[0] = {24604, 3604};
    m.names[0] = {"Furious Howl", "Tendon Rip"};
    m.mountedSpell = 23110;
    m.mountedName = "Run Them Down";
    std::vector<uint8_t> bytes = EncodeMoves(m);
    CHECK(bytes[0] == kOpMoves && bytes[1] == 0x08);
    Moves back;
    CHECK(DecodeMoves(bytes, back));
    CHECK(back.item == m.item && back.species == 6 && back.pending == 1 && back.twinStage == 2);
    CHECK(back.traits == m.traits && back.autocast == 0x3FF && back.picks[0] == 2);
    CHECK(back.spells[0][1] == 3604 && back.names[0][0] == "Furious Howl" && back.names[2][1].empty());
    CHECK(back.mountedSpell == 23110 && back.mountedName == "Run Them Down");
    bytes.pop_back();
    CHECK(!DecodeMoves(bytes, back));

    CHECK(EncodeCooldown(4, 12000) == std::vector<uint8_t>({kOpCooldown, 4, 0xE0, 0x2E, 0, 0}));
    CompanionState s;
    s.generation = 2;
    s.parentA = 6;
    s.parentB = 111;
    s.breedCount = 1;
    CHECK(EncodeLineage(0xAABBCCDD, s).size() == 13);

    Request r;
    Request press;
    press.op = kOpBarPress;
    press.slot = PetBarSlot::Ability2;
    press.guid = 77;
    press.flags = PRESS_POINT;
    press.point = {1.5f, -2.f, 30.25f};
    std::vector<uint8_t> p = EncodeRequest(press);
    CHECK(p.size() == 23);
    CHECK(DecodeRequest(p, r) && r.slot == 4 && r.guid == 77 && r.flags == PRESS_POINT);
    CHECK(r.point.x == 1.5f && r.point.y == -2.f && r.point.z == 30.25f);
    p[1] = 10;
    CHECK(!DecodeRequest(p, r));                // no eleventh slot
    Request pick;
    pick.op = kOpPick;
    pick.guid = 9;
    pick.slot = 1;
    pick.option = 2;
    CHECK(DecodeRequest(EncodeRequest(pick), r) && r.guid == 9 && r.slot == 1 && r.option == 2);
    pick.option = 3;
    CHECK(!DecodeRequest(EncodeRequest(pick), r));
    Request ac;
    ac.op = kOpAutocast;
    ac.slot = 3;
    ac.accept = true;
    CHECK(EncodeRequest(ac) == std::vector<uint8_t>({kOpAutocast, 3, 1}));
    CHECK(DecodeRequest(EncodeRequest(ac), r) && r.slot == 3 && r.accept);
}
