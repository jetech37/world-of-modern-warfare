/*
 * World of Modern Warfare: one entry per class spell (no ranks) and the class resources that are
 * left. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/ClassEffects.h"
#include "fusion/Classes.h"

#include <algorithm>
#include <map>
#include <set>

using namespace Fusion;
using namespace Fusion::Classes;

namespace
{
    // Fireball's vanilla chain (Spell.dbc spellLevel), the mage's basic spell, with each rank's
    // damage (its first effect's low end, base points + 1) as the old trainer sold it.
    struct OldRank
    {
        uint32_t spell;
        uint8_t level;
        int32_t damage;
    };
    std::vector<OldRank> const kFireball = {
        {133, 1, 14}, {143, 6, 31}, {145, 12, 53}, {3140, 18, 84}, {8400, 24, 139}, {8401, 30, 199},
        {8402, 36, 255}, {10148, 42, 318}, {10149, 48, 392}, {10150, 54, 475}, {10151, 60, 561}, {25306, 60, 596},
    };

    std::vector<Rank> ChainOf(std::vector<OldRank> const& old)
    {
        std::vector<Rank> c;
        for (OldRank const& r : old)
            c.push_back({r.spell, r.level});
        return c;
    }

    // What the old trainer would have had a character of `level` cast: the highest rank whose
    // level it reached (the first below every rank).
    OldRank const& TrainedAt(std::vector<OldRank> const& old, uint8_t level)
    {
        OldRank const* best = &old.front();
        for (OldRank const& r : old)
            if (r.level <= level)
                best = &r;
        return *best;
    }

    int32_t DamageOf(std::vector<OldRank> const& old, uint32_t spell)
    {
        for (OldRank const& r : old)
            if (r.spell == spell)
                return r.damage;
        return -1;
    }
}

TEST(class_single_entry_scales_like_the_old_ranks)
{
    std::vector<Rank> const chain = ChainOf(kFireball);
    // At each level the one entry is the rank the trainer would have sold, so its numbers are
    // the old rank's exactly; between rank levels it holds (vanilla's own per-level growth on
    // top, which the server applies from the caster's level).
    for (uint8_t level : {1, 5, 6, 10, 12, 20, 25, 30, 41, 42, 50, 59, 60})
    {
        uint32_t const entry = RankForLevel(chain, level);
        CHECK(entry == TrainedAt(kFireball, level).spell);
        CHECK(DamageOf(kFireball, entry) == TrainedAt(kFireball, level).damage);
    }
    // It only grows.
    int32_t last = 0;
    for (uint8_t level = 1; level <= 60; ++level)
    {
        int32_t const d = DamageOf(kFireball, RankForLevel(chain, level));
        CHECK(d >= last);
        last = d;
    }
    // At 60 the top rank (the catalog's spell), below the first rank's level the first.
    CHECK(RankForLevel(chain, 60) == 25306);
    CHECK(RankForLevel(chain, 0) == 133);
    CHECK(RankForLevel({}, 30) == 0);
}

TEST(class_ranks_chain_into_one_book_entry)
{
    std::vector<Rank> const chain = ChainOf(kFireball);
    std::vector<std::pair<uint32_t, uint32_t>> const next = Successors(chain);
    // Every rank but the last is superseded by the next: the book shows one entry.
    CHECK(next.size() == chain.size() - 1);
    for (size_t i = 0; i < next.size(); ++i)
        CHECK(next[i].first == chain[i].spell && next[i].second == chain[i + 1].spell);
    CHECK(Successors({{5019, 1}}).empty());       // a single rank: nothing to collapse
    CHECK(InChain(chain, 8400) && !InChain(chain, 6603) && !InChain(chain, 0));
}

TEST(class_a_stale_rank_is_cast_as_the_single_entry)
{
    std::vector<Rank> const chain = ChainOf(kFireball);
    std::set<uint32_t> known = {133, 143, 145};     // a level 12 mage
    auto knows = [&](uint32_t s) { return known.count(s) != 0; };
    // An old bar, a macro or a readied hand naming any rank casts the highest known.
    CHECK(SingleEntry(chain, 133, knows) == 145);
    CHECK(SingleEntry(chain, 143, knows) == 145);
    CHECK(SingleEntry(chain, 145, knows) == 145);
    // A rank above what is known still lands on what is known.
    CHECK(SingleEntry(chain, 25306, knows) == 145);
    // Not of the chain, or nothing known: no entry.
    CHECK(SingleEntry(chain, 6603, knows) == 0);
    CHECK(SingleEntry(chain, 133, [](uint32_t) { return false; }) == 0);
}

TEST(class_bars_move_onto_the_single_entry_at_login)
{
    // A character saved before the ranks were one entry: three Fireball ranks and a Frostbolt on
    // its bars, an Attack and an item-free spell of no chain, now level 30.
    std::vector<Rank> const fireball = ChainOf(kFireball);
    std::vector<Rank> const frostbolt = {{116, 4}, {205, 8}, {837, 14}, {7322, 20}, {8406, 26}, {8407, 32}};
    std::set<uint32_t> known;
    for (Rank const& r : fireball)
        if (r.level <= 30)
            known.insert(r.spell);
    for (Rank const& r : frostbolt)
        if (r.level <= 30)
            known.insert(r.spell);
    auto knows = [&](uint32_t s) { return known.count(s) != 0; };
    std::vector<std::pair<uint8_t, uint32_t>> const buttons = {
        {12, 133}, {13, 3140}, {14, 8401}, {24, 205}, {25, 6603}, {60, 99999}};
    std::vector<std::pair<uint8_t, uint32_t>> const moves = CollapseBar(buttons, {fireball, frostbolt}, 30, knows);
    std::map<uint8_t, uint32_t> m(moves.begin(), moves.end());
    CHECK(m.size() == 3);
    CHECK(m[12] == 8401 && m[13] == 8401);          // old ranks onto the entry
    CHECK(m.count(14) == 0);                        // already the entry
    CHECK(m[24] == 8406);                           // Frostbolt's entry at 30
    CHECK(m.count(25) == 0 && m.count(60) == 0);    // no chain: left alone
    // A level-up later: the next rank learned moves the entry on.
    known.insert(8407);
    std::vector<std::pair<uint8_t, uint32_t>> const after = CollapseBar({{24, 7322}}, {frostbolt}, 32, knows);
    CHECK(after.size() == 1 && after[0].second == 8407);
    // A rank the level allows but not yet learned: the button waits on what is known.
    CHECK(CollapseBar({{24, 7322}}, {frostbolt}, 40, [](uint32_t s) { return s == 7322; }).empty());
}

TEST(class_no_two_options_of_a_class_share_a_chain)
{
    // The catalog names each ranked chain by its top rank (the server walks down from it), and
    // the bar and the trainer take the rank for the level from it: no two options of a class
    // name one chain (one entry each).
    for (uint8_t cls : {kWarrior, kPaladin, kHunter, kRogue, kPriest, kShaman, kMage, kWarlock, kDruid})
    {
        std::set<uint32_t> named;
        for (Option const& o : Catalog())
            if (o.cls == cls)
                for (uint32_t s : o.spells)
                    if (s && LearnsSpells(o))
                        CHECK(named.insert(s).second || o.kind == Kind::Form);
    }
}

TEST(class_resources_left_are_three_shown_as_buffs)
{
    // Holy Power, Static and Life Force are left, by their old wire numbers, and the Duelist's
    // Momentum (2026-10-21) took a number never used before.
    std::vector<Resource> const all = AllResources();
    CHECK(all.size() == 4);
    CHECK(uint8_t(Resource::Momentum) == 16);
    CHECK(uint8_t(Resource::HolyPower) == 2 && uint8_t(Resource::Static) == 9 && uint8_t(Resource::LifeForce) == 14);
    for (Resource r : all)
        CHECK(std::string(ResourceName(r)).size() > 0);
    // Only their archetypes have one.
    for (uint8_t cls : {kWarrior, kPaladin, kHunter, kRogue, kPriest, kShaman, kMage, kWarlock, kDruid})
        for (uint8_t a = 0; a < kArchetypes; ++a)
        {
            std::vector<Resource> const r = ArchetypeResources(cls, a);
            bool const keeps = (cls == kPaladin && a == 0) || (cls == kShaman && a == 0) || (cls == kWarlock && a == 2)
                || (cls == kRogue && a == 2);
            CHECK(r.size() == (keeps ? 1u : 0u));
        }
    // A pure Berserker, Starcaller or Shadow kit has no resource any more; a Duelist has Momentum.
    CHECK(KitResources(kWarrior, Picks{111, 121, 131, 141, 151, 161}).empty());
    CHECK(KitResources(kRogue, Picks{413, 423, 433, 443, 453, 463}) == std::vector<Resource>{Resource::Momentum});
    CHECK(ResourceMax(Resource::Momentum, kRogue, Picks{413, 0, 0, 0, 0, 0}) == kMomentumMax);
    CHECK(ResourceMax(Resource::Momentum, kRogue, Picks{413, 0, 0, 0, 453, 0}) == kMomentumRelentless);
    CHECK(KitResources(kDruid, Picks{1113, 1123, 1133, 1143, 1153, 1163}).empty());
    CHECK(KitResources(kPriest, Picks{511, 521, 531, 541, 551, 561}).empty());
    // A Crusader-Necromancer can't be (one class), but a blend keeps each pick's own.
    Picks blend{211, 0, 0, 0, 0, 0};
    CHECK(KitResources(kPaladin, blend) == std::vector<Resource>{Resource::HolyPower});
    CHECK(ResourceMax(Resource::HolyPower, kPaladin, blend) == 3);
    blend[2] = 231;     // Zealotry
    CHECK(ResourceMax(Resource::HolyPower, kPaladin, blend) == 5);
    // The meters still travel on op 2 (the client draws them as buffs).
    std::vector<Meter> const ms = {{Resource::HolyPower, 2, 3}, {Resource::LifeForce, 40, 100}};
    std::vector<Meter> back;
    CHECK(DecodeMeters(EncodeMeters(ms), back) && back.size() == 2);
    CHECK(back[0].resource == Resource::HolyPower && back[0].value == 2 && back[1].max == 100);
}

TEST(class_abilities_of_removed_resources_need_nothing_but_a_cooldown)
{
    // Each spent a resource that is gone: they cast on their cooldown alone now.
    for (uint16_t id : {OptionId(kHunter, 2, 2),       // Throwing Axes (Axes)
             OptionId(kPriest, 5, 0),                   // Mind Control (Shadow Orbs)
             OptionId(kWarlock, 6, 1)})                 // Demon Form (Fel Fury)
    {
        CHECK(CastCost(id).first == Resource::None && CastCost(id).second == 0);
        Spec const* s = SpecOf(id);
        CHECK(s && s->cooldownMs > 0 && Available(id));
        Refit const* r = RefitOf(FindOption(id)->spells[0]);
        CHECK(r && r->cooldownMs > 0);
    }
    // The Necromancer still pays Life Force.
    CHECK(CastCost(OptionId(kWarlock, 1, 2)) == std::make_pair(Resource::LifeForce, uint16_t(20)));
    CHECK(CastCost(OptionId(kWarlock, 6, 2)) == std::make_pair(Resource::LifeForce, uint16_t(80)));
    // Every cost left is of a resource that is left.
    std::vector<Resource> const all = AllResources();
    for (Spec const& s : Specs())
        if (s.resource != Resource::None)
            CHECK(std::find(all.begin(), all.end(), s.resource) != all.end());
}

TEST(class_removed_resources_payoffs_come_by_chance_or_switch)
{
    // Zenith: the Lightwarden heals' old share of 100 Solar Power, as a chance.
    CHECK(ZenithChance(OptionId(kPaladin, 1, 2)) == 10);
    CHECK(ZenithChance(OptionId(kPaladin, 2, 2)) == 20);
    CHECK(ZenithChance(OptionId(kPaladin, 3, 2)) == 15);
    CHECK(ZenithChance(OptionId(kPaladin, 1, 0)) == 0);
    // Eclipse: the Starcaller's spells, and a blow less often.
    CHECK_NEAR(EclipseChance(OptionId(kDruid, 2, 2), false), 0.2f, 1e-6f);
    CHECK_NEAR(EclipseChance(0, true), 0.05f, 1e-6f);
    CHECK_NEAR(EclipseChance(0, false), 0.f, 1e-6f);
    // Spellweave: a switch of school pays, a repeat and the first spell do not.
    uint32_t last = 0;
    CHECK_NEAR(WeaveBonus(last, 4), 1.f, 1e-6f);
    CHECK(last == 4);
    CHECK_NEAR(WeaveBonus(last, 16), kWeaveBonus, 1e-6f);
    CHECK_NEAR(WeaveBonus(last, 16), 1.f, 1e-6f);
    CHECK_NEAR(WeaveBonus(last, 64), kWeaveBonus, 1e-6f);
    // The flat numbers that replace what a count used to scale.
    CHECK(kFrenzyChance > 0.f && kFrenzyChance < 1.f && kFrenzyCrowd == 3);
    CHECK_NEAR(kPrayerBonus, 1.2f, 1e-6f);
    CHECK(kStoneskinBulletCut < 1.f && kDemonicPactCut < 1.f);
    CHECK(kGroundSmashPct > 5.f && kBladeBlinkPct > 10.f);
    CHECK(kVampiricMs > 15000);
}
