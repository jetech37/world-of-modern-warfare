/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/Packets.h"
#include "fusion/Rounds.h"

#include <fstream>
#include <sstream>

using namespace Fusion;
using namespace Fusion::Rounds;

TEST(every_round_trades_something_for_its_effect)
{
    for (uint8_t i = 1; i < uint8_t(Round::Count); ++i)
    {
        RoundDef const& d = Def(Round(i));
        CHECK(d.round == Round(i));
        // Never flat damage: no round hits harder in general, and each gives something up.
        CHECK(d.damageMultiplier <= 1.f);
        CHECK(d.damageMultiplier < 1.f || d.fireTimeMultiplier > 1.f);
        CHECK((d.magazines > 0) != (d.seconds > 0));
        CHECK(std::string(d.name).size() > 0 && std::string(d.tag).size() > 0);
    }
    CHECK(Def(Round::Frost).fireTimeMultiplier > 1.f && Def(Round::Frost).damageMultiplier == 1.f);
    CHECK(Def(Round(200)).round == Round::None);
}

TEST(round_items_map_both_ways)
{
    CHECK(ItemRound(93050).round == Round::Incendiary && ItemRound(93050).grade == 1);
    CHECK(ItemRound(93051).round == Round::Incendiary && ItemRound(93051).grade == 2);
    CHECK(ItemRound(93052).round == Round::Frost);
    CHECK(ItemRound(93059).round == Round::Holy && ItemRound(93059).grade == 2);
    CHECK(ItemRound(93049).round == Round::None && ItemRound(93060).round == Round::None);
    for (uint32_t e = kFirstItem; e <= kLastItem; ++e)
        CHECK(ItemFor(ItemRound(e).round, ItemRound(e).grade) == e);
    CHECK(ItemFor(Round::None, 1) == 0 && ItemFor(Round::Holy, 3) == 0);
    CHECK(ItemName(Round::ArmorPiercing, 2) == "Greater Armor-Piercing Rounds");
    // Inside the package's item range.
    CHECK(kFirstItem >= 93050 && kLastItem <= 93199);
}

TEST(a_load_lasts_its_magazines_in_its_own_gun)
{
    Loaded l = Load(Round::Incendiary, 1, 2, 30, 1000);
    CHECK(l.Active(1000) && !l.timed && l.roundsLeft == 90);   // three magazines
    CHECK(Load(Round::Incendiary, 2, 2, 30, 1000).roundsLeft == 180);   // greater: twice
    // Another gun fires plain rounds and uses none of it.
    CHECK(l.Fire(30, 1000) == Round::None && l.roundsLeft == 90);
    for (int i = 0; i < 89; ++i)
        CHECK(l.Fire(2, 1000) == Round::Incendiary);
    CHECK(l.Left(1000) == 1);
    CHECK(l.Fire(2, 1000) == Round::Incendiary);
    CHECK(!l.Active(1000) && l.Fire(2, 1000) == Round::None && l.round == Round::None);
    CHECK(!Load(Round::Frost, 1, 0, 30, 0).Active(0));      // no gun in hand
}

TEST(an_arcane_load_runs_on_time)
{
    Loaded l = Load(Round::Arcane, 1, 2, 30, 1000);
    CHECK(l.timed && l.Active(1000) && l.Left(1000) == 45000);
    CHECK(l.Fire(2, 30000) == Round::Arcane && l.Left(30000) == 16000);
    CHECK(Load(Round::Arcane, 2, 2, 30, 0).Left(0) == 90000);
    CHECK(l.Fire(2, 46000) == Round::None && !l.Active(46000));
}

TEST(each_round_does_its_own_thing_on_a_hit)
{
    TargetFacts plain;
    HitEffect e = OnHit(Round::Incendiary, plain);
    CHECK(e.burnShare > 0.f && e.burnMs == 3000 && e.damageMultiplier < 1.f);
    TargetFacts fire;
    fire.fireElemental = true;
    CHECK(OnHit(Round::Incendiary, fire).burnShare == 0.f);

    CHECK(OnHit(Round::Frost, plain).slow);
    TargetFacts boss;
    boss.slowImmune = true;
    CHECK(!OnHit(Round::Frost, boss).slow);

    CHECK(OnHit(Round::ArmorPiercing, plain).armorBypass == 0.5f);

    CHECK(OnHit(Round::Arcane, plain).manaBurnShare == 0.f);    // nothing to burn
    TargetFacts caster;
    caster.maxMana = 3000;
    CHECK(OnHit(Round::Arcane, caster).manaBurnShare > 0.f);

    TargetFacts undead;
    undead.type = CreatureType::Undead;
    TargetFacts demon;
    demon.type = CreatureType::Demon;
    CHECK(OnHit(Round::Holy, undead).damageMultiplier > 1.f);
    CHECK(OnHit(Round::Holy, demon).damageMultiplier > 1.f);
    CHECK(OnHit(Round::Holy, plain).damageMultiplier < 1.f);
    CHECK(OnHit(Round::None, plain).damageMultiplier == 1.f);
}

TEST(armor_piercing_gives_back_part_of_the_armor_cut)
{
    // A 30% cut, half ignored: 15% kept, so 0.85 / 0.70 of the damage after the full cut.
    CHECK_NEAR(ArmorBypassScale(0.30f, 0.5f), 0.85f / 0.70f, 1e-5);
    CHECK(ArmorBypassScale(0.f, 0.5f) == 1.f);
    CHECK(ArmorBypassScale(0.3f, 0.f) == 1.f);
    CHECK_NEAR(ArmorBypassScale(0.3f, 1.f), 1.f / 0.7f, 1e-5);
}

TEST(arcane_rounds_burn_mana_by_the_health_taken)
{
    CHECK(ManaBurned(0.10f, 3000, 0.6f) == 180);
    CHECK(ManaBurned(2.f, 1000, 0.6f) == 600);      // never more than the pool's share
    CHECK(ManaBurned(0.5f, 0, 0.6f) == 0);
}

TEST(a_burn_ticks_out_and_refreshes_without_stacking)
{
    std::vector<Burn> burns;
    AddBurn(burns, 7, 2, 30.f, 3000, 0);
    CHECK(burns.size() == 1);
    CHECK(TickBurns(burns, 999).empty());
    std::vector<BurnTick> t = TickBurns(burns, 1000);
    CHECK(t.size() == 1 && t[0].target == 7 && t[0].weapon == 2);
    CHECK_NEAR(t[0].damage, 10.f, 1e-4);
    // A new hit: the larger of what is left (20) and the new burn (12), over three fresh ticks.
    AddBurn(burns, 7, 2, 12.f, 3000, 1500);
    CHECK(burns.size() == 1 && burns[0].ticks == 3);
    float total = 0.f;
    for (BurnTick const& b : TickBurns(burns, 10000))
        total += b.damage;
    CHECK_NEAR(total, 20.f, 1e-3);
    CHECK(burns.empty());
    // Another target burns on its own.
    AddBurn(burns, 7, 2, 5.f, 3000, 0);
    AddBurn(burns, 8, 2, 5.f, 3000, 0);
    CHECK(burns.size() == 2);
    AddBurn(burns, 9, 2, 0.f, 3000, 0);             // nothing to burn
    CHECK(burns.size() == 2);
}

TEST(round_recipes_are_enchanting_from_vanilla_materials)
{
    std::vector<Recipe> const& all = Recipes();
    CHECK(all.size() == 10);
    uint16_t last = 0;
    for (Recipe const& r : all)
    {
        CHECK(r.craft.id >= 62600 && r.craft.id <= 62799);
        CHECK(r.craft.skillLine == kSkillEnchanting);
        CHECK(r.craft.skill.required >= last);      // by skill
        last = r.craft.skill.required;
        CHECK(ItemRound(r.craft.product).round != Round::None);
        CHECK(!r.craft.reagents.empty() && r.trainCopper > 0);
        CHECK(FindRecipe(r.craft.id) == &r);
    }
    // Every round item is made by some recipe.
    for (uint32_t e = kFirstItem; e <= kLastItem; ++e)
    {
        bool made = false;
        for (Recipe const& r : all)
            made = made || r.craft.product == e;
        CHECK(made);
    }
    CHECK(!FindRecipe(1));
}

TEST(learning_a_round_recipe)
{
    Recipe const& r = *FindRecipe(62601);       // frost, 110
    CHECK(CanLearn(r, false, true, 120, 100000, false) == LearnCheck::Ok);
    CHECK(CanLearn(r, true, true, 120, 100000, false) == LearnCheck::Known);
    CHECK(CanLearn(r, false, false, 0, 100000, false) == LearnCheck::NoEnchanting);
    CHECK(CanLearn(r, false, true, 100, 100000, false) == LearnCheck::SkillTooLow);
    CHECK(CanLearn(r, false, true, 120, 10, false) == LearnCheck::NotEnoughMoney);
    CHECK(CanLearn(r, false, false, 0, 0, true) == LearnCheck::Ok);
    CHECK(IsEnchantingTrainer(2, "Expert Enchanter"));
    CHECK(IsEnchantingTrainer(2, "Enchanting Trainer"));
    CHECK(!IsEnchantingTrainer(0, "Enchanting Supplies"));      // a vendor
    CHECK(!IsEnchantingTrainer(2, "Blacksmithing Trainer"));
}

TEST(the_loaded_message_encodes_and_decodes)
{
    Loaded l = Load(Round::Frost, 1, 0x0102, 30, 0);
    std::vector<uint8_t> const b = EncodeLoaded(l, 0);
    // Frozen: the client (crate::rounds) decodes these exact bytes.
    CHECK(Packets::ToHex(b) == "40020201005a000000");
    Loaded back;
    CHECK(DecodeLoaded(b, back) && back.round == Round::Frost && back.gun == 0x0102 && back.roundsLeft == 90);
    Loaded timed = Load(Round::Arcane, 1, 9, 30, 100);
    CHECK(DecodeLoaded(EncodeLoaded(timed, 1100), back) && back.timed && back.untilMs == 44000);
    // An ended load says none.
    CHECK(Packets::ToHex(EncodeLoaded(Loaded{}, 0)) == "400000000000000000");
    CHECK(!DecodeLoaded({kOpLoaded, 9, 0, 0, 0, 0, 0, 0, 0}, back));
}

TEST(the_round_items_are_in_the_world_sql)
{
    std::ifstream in(FUSION_VECTORS_DIR "/../../../sql/custom/azeroth_warfare/10_enchanted_rounds.sql");
    std::stringstream s;
    s << in.rdbuf();
    std::string const sql = s.str();
    CHECK(!sql.empty());
    for (uint32_t e = kFirstItem; e <= kLastItem; ++e)
    {
        RoundItem const it = ItemRound(e);
        std::string const row = "(" + std::to_string(e) + ", 0, 0, 6, '" + ItemName(it.round, it.grade) + "'";
        CHECK(sql.find(row) != std::string::npos);
        if (sql.find(row) == std::string::npos)
            std::printf("    missing row: %s\n", row.c_str());
    }
}
