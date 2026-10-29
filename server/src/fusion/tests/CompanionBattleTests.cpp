/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"
#include "fusion/CompanionBattles.h"
#include "fusion/Throttle.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

using namespace Fusion;
using namespace Fusion::CompanionBattles;
using Companions::AbilityKind;

namespace
{
    // A plain companion for the rules: `health`, a deck of the given cards.
    BattlePet Pet(std::string name, std::vector<uint16_t> deck, uint16_t health = 60, uint8_t level = 20)
    {
        BattlePet p;
        p.name = std::move(name);
        p.maxHealth = p.health = health;
        p.level = level;
        p.deck = std::move(deck);
        return p;
    }

    std::vector<uint16_t> Many(uint16_t card, size_t n) { return std::vector<uint16_t>(n, card); }

    // Plays the first copy of `card` in `side`'s hand.
    Refusal PlayCard(Battle& b, int side, uint16_t card)
    {
        SideState const& s = b.State(side);
        for (uint8_t i = 0; i < s.hand.size(); ++i)
            if (s.hand[i] == card)
                return b.Choose(side, {ActionKind::Play, i, card});
        return Refusal::NoSuchCard;
    }

    bool InHand(Battle const& b, int side, uint16_t card)
    {
        auto const& h = b.State(side).hand;
        return std::find(h.begin(), h.end(), card) != h.end();
    }

    Companions::CompanionSpecies const* SpeciesOf(Archetype a)
    {
        for (Companions::CompanionSpecies const& sp : Companions::StarterSpecies())
            if (ArchetypeOf(sp.family) == a)
                return &sp;
        return nullptr;
    }

    BattlePet Grown(Companions::CompanionSpecies const& sp, uint8_t level, uint8_t stage, uint8_t pick)
    {
        Companions::CompanionState s;
        s.species = sp.id;
        s.level = level;
        s.stage = std::min<uint8_t>(stage, uint8_t(sp.stageCount - 1));
        s.picks = {pick, pick, pick};
        return MakePet(sp, s, 0, BuiltDeck(sp, s), level);
    }
}

// ── Families and cards ──────────────────────────────────────────────────────────────────────

TEST(duel_every_companion_family_folds_into_an_archetype)
{
    std::set<Archetype> seen;
    for (uint8_t f = 0; f <= uint8_t(Companions::kLastFamily); ++f)
    {
        Archetype const a = ArchetypeOf(Companions::Family(f));
        CHECK(uint8_t(a) < uint8_t(Archetype::Count));
        CHECK(uint8_t(BattleFamilyOf(Companions::Family(f))) < uint8_t(BattleFamily::Count));
        seen.insert(a);
    }
    CHECK(seen.size() == size_t(Archetype::Count));
    // The owner's identities.
    CHECK(ArchetypeOf(Companions::Family::Cat) == Archetype::Feline);
    CHECK(ArchetypeOf(Companions::Family::Bear) == Archetype::Ursine);
    CHECK(ArchetypeOf(Companions::Family::Owl) == Archetype::Avian);
    CHECK(ArchetypeOf(Companions::Family::Dragon) == Archetype::Draconic);
    CHECK(ArchetypeOf(Companions::Family::Skeleton) == Archetype::Undead);
    CHECK(std::string(ArchetypeName(Archetype::Shell)) == "Shell");
}

TEST(duel_the_card_table_is_whole)
{
    std::vector<Card> const& t = CardTable();
    uint16_t last = 0;
    for (Card const& c : t)
    {
        CHECK(c.id > last);
        last = c.id;
        CHECK(&CardById(c.id) != nullptr && CardById(c.id).id == c.id);
        CHECK(!c.name.empty() && !c.icon.empty());
        CHECK(!CardText(c).empty());
        CHECK(c.cost <= 3);
        CHECK((c.type == CardType::Attack) == (c.hits > 0));
        CHECK(c.id < kAbilityCardBase);
    }
    CHECK(!CardById(999).Exists());
    CHECK(!CardById(0).Exists());
    CHECK(CardText(CardById(kGuardCard)) == "Gain 5 Block.");
    CHECK(CardText(CardById(100)) == "Deal 3 damage. Apply 3 Bleed.");
    CHECK(CardText(CardById(145)) == "Deal 5 damage 4 times.");
    // Every archetype has its eight cards, two of them starters.
    for (uint8_t a = 0; a < uint8_t(Archetype::Count); ++a)
    {
        std::vector<uint16_t> const cards = ArchetypeCards(Archetype(a));
        CHECK(cards.size() == 2 + kLibraryLevels.size());
        uint8_t powers = 0;
        for (uint16_t id : cards)
        {
            CHECK(CardById(id).Exists());
            CHECK(CardById(id).archetype == Archetype(a));
            powers = uint8_t(powers + (CardById(id).type == CardType::Power ? 1 : 0));
        }
        CHECK(powers >= 1);
    }
}

TEST(duel_each_family_plays_its_own_way)
{
    auto count = [](Archetype a, auto pred) {
        int n = 0;
        for (uint16_t id : ArchetypeCards(a))
            n += pred(CardById(id)) ? 1 : 0;
        return n;
    };
    auto applies = [](Status s) { return [s](Card const& c) { return c.enemy[uint8_t(s)] > 0 || c.self[uint8_t(s)] > 0; }; };
    // Cats bleed and combo.
    CHECK(count(Archetype::Feline, applies(Status::Bleed)) >= 2);
    CHECK(count(Archetype::Feline, [](Card const& c) { return c.special == Special::Combo; }) >= 2);
    // Bears block and thorns.
    CHECK(count(Archetype::Ursine, applies(Status::Thorns)) >= 2);
    CHECK(count(Archetype::Ursine, [](Card const& c) { return c.block > 0 || c.self[uint8_t(Status::Fortify)]; }) >= 4);
    // Birds evasion and draw.
    CHECK(count(Archetype::Avian, applies(Status::Evasion)) >= 2);
    CHECK(count(Archetype::Avian, [](Card const& c) { return c.draw > 0 || c.special == Special::EvasionDraw || c.self[uint8_t(Status::Insight)]; }) >= 4);
    // Drakes powers.
    CHECK(count(Archetype::Draconic, [](Card const& c) { return c.type == CardType::Power; }) >= 3);
    // Undead exhaust and recursion.
    CHECK(count(Archetype::Undead, [](Card const& c) { return c.exhaust; }) >= 5);
    CHECK(count(Archetype::Undead, [](Card const& c) { return c.special == Special::Recall; }) >= 1);
    CHECK(count(Archetype::Venom, applies(Status::Poison)) >= 3);
    CHECK(count(Archetype::Shell, [](Card const& c) { return c.special == Special::BlockStrike; }) >= 1);
}

TEST(duel_catalog_abilities_become_cards)
{
    size_t made = 0;
    for (uint8_t m = 1; m < uint8_t(Companions::Moveset::Count); ++m)
    {
        Companions::MovesetInfo const& info = Companions::MovesetData(Companions::Moveset(m));
        CHECK(CardById(BasicAttackCard(info)).basic);
        for (uint8_t st = 0; st < Companions::kMaxStages; ++st)
            for (uint8_t opt = 0; opt < 2; ++opt)
            {
                Companions::Ability const& a = info.stages[st][opt];
                uint16_t const id = AbilityCardId(Companions::Moveset(m), st, opt);
                Card const& c = CardById(id);
                if (!a.Exists() || a.kind == AbilityKind::Mounted)
                {
                    CHECK(!c.Exists());
                    continue;
                }
                CHECK(c.Exists() && c.id == id);
                CHECK(c.name == a.name && c.spell == a.spell);
                CHECK(c.cost <= 2 && !CardText(c).empty());
                CHECK(c.type != CardType::Attack || c.damage > 0);
                ++made;
            }
    }
    CHECK(made > 200);
    CHECK(!CardById(uint16_t(kAbilityCardBase + 7)).Exists());     // moveset 0
}

// ── Decks ───────────────────────────────────────────────────────────────────────────────────

TEST(duel_every_companion_gets_a_valid_starter_deck)
{
    std::vector<Companions::CompanionSpecies> const& all = Companions::StarterSpecies();
    for (Companions::CompanionSpecies const& sp : all)
        for (uint8_t stage = 0; stage < sp.stageCount; ++stage)
        {
            Companions::CompanionState s;
            s.species = sp.id;
            s.stage = stage;
            s.level = uint8_t(10 + stage * 20);
            s.picks = {2, 1, 2};
            std::vector<uint16_t> const deck = StarterDeck(sp, s);
            std::vector<LibraryCard> const lib = Library(sp, s, {}, all);
            CHECK(CheckDeck(lib, deck) == DeckProblem::None);
            CHECK(deck.size() >= kMinDeck && deck.size() <= 12u + 3u + 5u);
            CHECK(CheckDeck(lib, BuiltDeck(sp, s)) == DeckProblem::None);
            // A saved deck that is no longer valid falls back to the starter.
            CHECK(DuelDeck(lib, {1, 2, 3}, sp, s) == deck);
        }
}

TEST(duel_a_library_grows_with_level_picks_traits_breeding_and_lessons)
{
    std::vector<Companions::CompanionSpecies> const& all = Companions::StarterSpecies();
    Companions::CompanionSpecies const* cat = SpeciesOf(Archetype::Feline);
    Companions::CompanionSpecies const* bear = SpeciesOf(Archetype::Ursine);
    CHECK(cat && bear);
    if (!cat || !bear)
        return;
    auto has = [](std::vector<LibraryCard> const& lib, uint16_t c) {
        return std::any_of(lib.begin(), lib.end(), [c](LibraryCard const& l) { return l.card == c; });
    };
    Companions::CompanionState s;
    s.species = cat->id;
    s.level = 1;
    std::vector<LibraryCard> lib = Library(*cat, s, {}, all);
    CHECK(lib.size() == 4);     // the basic attack, Guard, the two starters
    CHECK(has(lib, 100) && has(lib, 101) && !has(lib, 102));
    s.level = 60;
    lib = Library(*cat, s, {}, all);
    CHECK(lib.size() == 4 + kLibraryLevels.size());
    s.level = 10;
    CHECK(Library(*cat, s, {}, all).size() == 6);
    // A pick, the traits, the parents, a lesson.
    s.picks = {1, 0, 0};
    lib = Library(*cat, s, {}, all);
    CHECK(has(lib, AbilityCardId(Companions::MovesetOf(*cat), 0, 0)));
    s.traits = Companions::TraitBit(Companions::Trait::FireTouched);
    CHECK(has(Library(*cat, s, {}, all), 40));
    s.generation = 1;
    s.parentA = bear->id;
    CHECK(has(Library(*cat, s, {}, all), 120));
    lib = Library(*cat, s, {24, 24}, all);
    CHECK(has(lib, 24));
    for (LibraryCard const& l : lib)
        if (l.card == 24)
            CHECK(l.copies == kMaxCopies && l.source == uint8_t(CardSource::Learned));
    // Lessons and drops.
    for (uint16_t c : TrainerCards())
        CHECK(CardById(c).Exists() && TrainerPrice(c, 30) > 0);
    CHECK(TrainerPrice(24, 60) > TrainerPrice(24, 10));
    uint16_t const drop = RollDropCard(lib, 3);
    CHECK(drop >= 30 && drop <= 35 && !has(lib, drop));
    std::vector<LibraryCard> full = lib;
    for (uint16_t c : DropCards())
        full.push_back({c, 2, uint8_t(CardSource::Learned)});
    CHECK(RollDropCard(full, 3) == 0);
}

TEST(duel_deck_rules_hold)
{
    std::vector<LibraryCard> const lib = {{1, 5, 0}, {10, 5, 0}, {100, 2, 1}, {101, 2, 1}, {0x4009, 1, 3}};
    std::vector<uint16_t> ok = {1, 1, 1, 1, 1, 10, 10, 10, 10, 10, 100, 100};
    CHECK(CheckDeck(lib, ok) == DeckProblem::None);
    std::vector<uint16_t> small(ok.begin(), ok.end() - 1);
    CHECK(CheckDeck(lib, small) == DeckProblem::TooSmall);
    std::vector<uint16_t> big(31, 1);
    CHECK(CheckDeck(lib, big) == DeckProblem::TooLarge);
    std::vector<uint16_t> copies = ok;
    copies.push_back(100);
    CHECK(CheckDeck(lib, copies) == DeckProblem::TooManyCopies);
    std::vector<uint16_t> unknown = ok;
    unknown.push_back(220);
    CHECK(CheckDeck(lib, unknown) == DeckProblem::Unknown);
    std::vector<uint16_t> six = ok;
    six.push_back(1);
    CHECK(CheckDeck(lib, six) == DeckProblem::TooManyCopies);
    CHECK(std::string(DeckProblemText(DeckProblem::TooSmall)).find("12") != std::string::npos);
    // Saved as text.
    CHECK(SplitCards(JoinCards(ok)) == ok);
    CHECK(SplitCards("") .empty());
    CHECK((SplitCards("1,,x,10, 7") == std::vector<uint16_t>{1, 10, 7}));
    CHECK(SplitCards("99999") == std::vector<uint16_t>{0xFFFF});
}

// ── A duel's turns ──────────────────────────────────────────────────────────────────────────

TEST(duel_a_turn_has_energy_a_hand_and_checked_plays)
{
    Battle b({Pet("Mine", Many(9, 12))}, {Pet("Theirs", Many(10, 12))}, 7, {false, false}, 0);
    CHECK(b.GetPhase() == Phase::Playing && b.ToPlay() == 0 && b.Turn() == 1);
    CHECK(b.State(0).energy == kEnergy);
    // Two players: the first side starts a card short.
    CHECK(b.State(0).hand.size() == kHandSize - 1u && b.State(1).hand.size() == kHandSize);
    CHECK(b.State(0).draw.size() == 12u - 4u);
    // The other side can't play; a slot out of range, a stale card, no energy: refused.
    CHECK(b.Choose(1, {ActionKind::Play, 0}) == Refusal::NotYourTurn);
    CHECK(b.Choose(0, {ActionKind::Play, 9}) == Refusal::NoSuchCard);
    CHECK(b.Choose(0, {ActionKind::Play, 0, 10}) == Refusal::Stale);
    CHECK(b.Choose(0, {ActionKind::Play, 0, 9}) == Refusal::None);
    CHECK(b.Active(1).health == 60 - 6);
    CHECK(b.State(0).energy == 2 && b.State(0).hand.size() == 3 && b.State(0).discard.size() == 1);
    CHECK(PlayCard(b, 0, 9) == Refusal::None);
    CHECK(PlayCard(b, 0, 9) == Refusal::None);
    CHECK(b.State(0).energy == 0);
    CHECK(PlayCard(b, 0, 9) == Refusal::NoEnergy);
    CHECK(b.Active(1).health == 60 - 18);
    // The turn ends: the hand is discarded, a new one drawn, the other side plays.
    CHECK(b.Choose(0, {ActionKind::EndTurn}) == Refusal::None);
    CHECK(b.ToPlay() == 1 && b.Turn() == 2);
    CHECK(b.State(0).hand.size() == kHandSize && b.State(0).discard.size() == 4);
    CHECK(b.State(1).energy == kEnergy);
    CHECK(b.Choose(0, {ActionKind::EndTurn}) == Refusal::NotYourTurn);
}

TEST(duel_block_falls_away_at_the_start_of_its_owners_turn)
{
    Battle b({Pet("Mine", Many(10, 12))}, {Pet("Theirs", Many(9, 12))}, 3, {false, false}, 0);
    PlayCard(b, 0, 10);
    PlayCard(b, 0, 10);
    CHECK(b.State(0).block == 10);
    b.Choose(0, {ActionKind::EndTurn});
    // Their strike is blocked, then the block goes when the turn comes back.
    PlayCard(b, 1, 9);
    CHECK(b.Active(0).health == 60 && b.State(0).block == 4);
    b.Choose(1, {ActionKind::EndTurn});
    CHECK(b.State(0).block == 0);
    // Plating keeps it.
    std::vector<uint16_t> deck = Many(10, 11);
    deck.push_back(304);
    Battle p({Pet("Shell", deck, 60)}, {Pet("Theirs", Many(9, 12))}, 1, {false, false}, 0);
    for (int turn = 0; turn < 8 && !InHand(p, 0, 304); ++turn)
    {
        p.Choose(0, {ActionKind::EndTurn});
        p.Choose(1, {ActionKind::EndTurn});
    }
    CHECK(PlayCard(p, 0, 304) == Refusal::None);
    PlayCard(p, 0, 10);
    p.Choose(0, {ActionKind::EndTurn});
    p.Choose(1, {ActionKind::EndTurn});
    CHECK(p.State(0).block == 5 + 0);
}

TEST(duel_statuses_change_the_numbers)
{
    Battle b({Pet("Mine", Many(9, 12))}, {Pet("Theirs", Many(10, 12))}, 3, {false, false}, 0);
    Card const& strike = CardById(9);
    CHECK(b.CardDamage(0, strike) == 6);
    // Exposed +50%, Weakened -25%, Strength per hit (set by hand through cards below).
    Battle e({Pet("Mine", {261, 261, 261, 261, 261, 261, 9, 9, 9, 9, 9, 9})}, {Pet("Theirs", Many(10, 12))}, 11,
        {false, false}, 0);
    for (int turn = 0; turn < 6 && !(InHand(e, 0, 261) && InHand(e, 0, 9)); ++turn)
    {
        e.Choose(0, {ActionKind::EndTurn});
        e.Choose(1, {ActionKind::EndTurn});
    }
    CHECK(PlayCard(e, 0, 261) == Refusal::None);       // Weakened 2, Exposed 1 on them
    CHECK(e.State(1).Get(Status::Exposed) == 1 && e.State(1).Get(Status::Weakened) == 2);
    CHECK(e.CardDamage(0, strike) == 9);
    CHECK(e.CardDamage(1, strike) == 4);
    // Exposed and Weakened run down at the end of the bearer's turn.
    e.Choose(0, {ActionKind::EndTurn});
    e.Choose(1, {ActionKind::EndTurn});
    CHECK(e.State(1).Get(Status::Exposed) == 0 && e.State(1).Get(Status::Weakened) == 1);
    // Poison: at the start of its turn, then one less; block doesn't help.
    int const spit = CardById(201).enemy[uint8_t(Status::Poison)];
    Battle p({Pet("Spider", Many(201, 12))}, {Pet("Theirs", Many(10, 12))}, 5, {false, false}, 0);
    PlayCard(p, 0, 201);
    CHECK(p.State(1).Get(Status::Poison) == spit);
    p.Choose(0, {ActionKind::EndTurn});
    CHECK(p.Active(1).health == 60 - spit && p.State(1).Get(Status::Poison) == spit - 1);
    // Bleed: at the start of its turn, then half.
    int const rake = CardById(100).enemy[uint8_t(Status::Bleed)];
    Battle r({Pet("Cat", Many(100, 12))}, {Pet("Theirs", Many(10, 12))}, 5, {false, false}, 0);
    PlayCard(r, 0, 100);
    CHECK(r.Active(1).health == 57 && r.State(1).Get(Status::Bleed) == rake);
    r.Choose(0, {ActionKind::EndTurn});
    CHECK(r.Active(1).health == 57 - rake && r.State(1).Get(Status::Bleed) == rake / 2);
    // Thorns hurt the attacker; Evasion makes a hit miss.
    Battle t({Pet("Bear", Many(124, 12))}, {Pet("Cat", Many(9, 12))}, 5, {false, false}, 0);
    PlayCard(t, 0, 124);
    PlayCard(t, 0, 124);
    t.Choose(0, {ActionKind::EndTurn});
    PlayCard(t, 1, 9);
    CHECK(t.Active(1).health == 60 - 2 * CardById(124).self[uint8_t(Status::Thorns)] && t.Active(0).health == 60 - 6);
    Battle v({Pet("Owl", Many(146, 12))}, {Pet("Cat", Many(9, 12))}, 5, {false, false}, 0);
    PlayCard(v, 0, 146);
    v.Choose(0, {ActionKind::EndTurn});
    PlayCard(v, 1, 9);
    PlayCard(v, 1, 9);
    CHECK(v.Active(0).health == 60 && v.State(0).Get(Status::Evasion) == 0);
    PlayCard(v, 1, 9);
    CHECK(v.Active(0).health == 54);
}

TEST(duel_powers_combos_exhaust_and_recursion)
{
    // A power stays in play: it leaves the deck, its effect each turn.
    Battle d({Pet("Drake", Many(161, 12))}, {Pet("Theirs", Many(10, 12))}, 5, {false, false}, 0);
    PlayCard(d, 0, 161);
    CHECK(d.State(0).discard.empty() && d.State(0).Get(Status::Ritual) == 1);
    d.Choose(0, {ActionKind::EndTurn});
    d.Choose(1, {ActionKind::EndTurn});
    CHECK(d.State(0).Get(Status::Strength) == 2);
    // Combo: more damage after another attack this turn.
    Battle c({Pet("Cat", Many(101, 12))}, {Pet("Theirs", Many(10, 12), 99)}, 5, {false, false}, 0);
    PlayCard(c, 0, 101);
    CHECK(c.Active(1).health == 99 - 3);
    PlayCard(c, 0, 101);
    CHECK(c.Active(1).health == 99 - 3 - 7);
    // Exhaust takes a card out for the duel; Raise Dead brings the last one back.
    std::vector<uint16_t> deck = Many(180, 6);
    for (int i = 0; i < 6; ++i)
        deck.push_back(182);
    Battle u({Pet("Ghoul", deck)}, {Pet("Theirs", Many(10, 12), 99)}, 9, {false, false}, 0);
    for (int turn = 0; turn < 6 && !(InHand(u, 0, 180) && InHand(u, 0, 182)); ++turn)
    {
        u.Choose(0, {ActionKind::EndTurn});
        u.Choose(1, {ActionKind::EndTurn});
    }
    CHECK(PlayCard(u, 0, 180) == Refusal::None);
    size_t const ex = u.State(0).exhaust.size();
    CHECK(ex >= 1 && u.State(0).exhaust.back() == 180);
    size_t const hand = u.State(0).hand.size();
    CHECK(PlayCard(u, 0, 182) == Refusal::None);
    CHECK(u.State(0).exhaust.back() == 182);
    CHECK(InHand(u, 0, 180) && u.State(0).hand.size() >= hand);
}

TEST(duel_a_beaten_companion_stays_at_one_and_the_next_steps_in)
{
    BattlePet a = Pet("Big", Many(30, 12), 60);
    BattlePet b1 = Pet("First", Many(10, 12), 10);
    BattlePet b2 = Pet("Second", Many(10, 12), 10);
    Battle b({a}, {b1, b2}, 4, {false, false}, 0);
    CHECK(PlayCard(b, 0, 30) == Refusal::None);         // 14 into 10
    CHECK(b.Team(1)[0].beaten && b.Team(1)[0].health == 1);
    CHECK(b.ActiveIndex(1) == 1 && b.Active(1).fought);
    CHECK(b.State(1).hand.size() == kHandSize);
    CHECK(b.GetPhase() == Phase::Playing);
    // The turn goes on against the new one.
    CHECK(b.ToPlay() == 0);
    b.Choose(0, {ActionKind::EndTurn});
    b.Choose(1, {ActionKind::EndTurn});
    CHECK(PlayCard(b, 0, 30) == Refusal::None);
    CHECK(b.GetPhase() == Phase::Over && b.GetEnd() == End::Defeated && b.Winner() == 0);
    CHECK(b.Team(1)[1].health == 1);
    CHECK(b.Defeated(0) == 2);
    // Experience: the winner, standing and fought, for each enemy beaten.
    std::vector<uint32_t> const xp = XpEarned(b, 0, kTamerXpFactor);
    CHECK(xp.size() == 1 && xp[0] == 2 * Companions::KillXp(20, 20, false) * kTamerXpFactor);
    CHECK(XpEarned(b, 1, kTamerXpFactor)[0] == 0);
}

TEST(duel_a_forfeit_or_an_abandoned_duel_pays_nothing)
{
    Battle b({Pet("A", Many(9, 12))}, {Pet("B", Many(9, 12))}, 4, {false, false}, 0);
    CHECK(b.Choose(1, {ActionKind::Forfeit}) == Refusal::None);   // even on the other's turn
    CHECK(b.GetEnd() == End::Forfeit && b.Winner() == 0);
    CHECK(XpEarned(b, 0, 3)[0] == 0);
    CHECK(!Companions::DuelPaysXp(DuelEndOf(End::Forfeit)));
    CHECK(!Companions::DuelPaysXp(DuelEndOf(End::Abandoned)));
    CHECK(Companions::DuelPaysXp(DuelEndOf(End::Defeated)) && Companions::DuelPaysXp(DuelEndOf(End::Turns)));
    CHECK(b.Choose(0, {ActionKind::EndTurn}) == Refusal::Over);
    Battle c({Pet("A", Many(9, 12))}, {Pet("B", Many(9, 12))}, 4);
    c.Abandon();
    CHECK(c.GetEnd() == End::Abandoned && c.Winner() == -1);
}

TEST(duel_timeouts_end_the_turn_then_forfeit)
{
    Battle b({Pet("A", Many(9, 12))}, {Pet("B", Many(9, 12))}, 4, {false, false}, 0);
    b.Timeout();
    CHECK(b.ToPlay() == 1);
    b.Choose(1, {ActionKind::EndTurn});
    b.Timeout();
    b.Choose(1, {ActionKind::EndTurn});
    CHECK(b.GetPhase() == Phase::Playing);
    b.Timeout();
    CHECK(b.GetEnd() == End::Forfeit && b.Winner() == 1);
    // A play in between starts the count over.
    Battle c({Pet("A", Many(9, 12))}, {Pet("B", Many(9, 12))}, 4, {false, false}, 0);
    c.Timeout();
    c.Choose(1, {ActionKind::EndTurn});
    c.Timeout();
    c.Choose(1, {ActionKind::EndTurn});
    PlayCard(c, 0, 9);
    c.Timeout();
    CHECK(c.GetPhase() == Phase::Playing);
}

TEST(duel_the_turn_limit_goes_to_the_healthier_side)
{
    Battle b({Pet("A", Many(10, 12))}, {Pet("B", Many(10, 12))}, 4, {false, false}, 0);
    PlayCard(b, 0, 10);
    for (int i = 0; i < kMaxTurns + 2 && b.GetPhase() == Phase::Playing; ++i)
        b.Choose(b.ToPlay(), {ActionKind::EndTurn});
    CHECK(b.GetEnd() == End::Turns && b.Winner() == -1);
    Battle c({Pet("A", Many(9, 12), 200)}, {Pet("B", Many(10, 12), 200)}, 4, {false, false}, 0);
    PlayCard(c, 0, 9);
    for (int i = 0; i < kMaxTurns + 2 && c.GetPhase() == Phase::Playing; ++i)
        c.Choose(c.ToPlay(), {ActionKind::EndTurn});
    CHECK(c.GetEnd() == End::Turns && c.Winner() == 0);
}

TEST(duel_traits_and_levels_give_their_gifts)
{
    BattlePet a = Pet("Ferocious", Many(9, 12));
    a.traits = Companions::TraitBit(Companions::Trait::Ferocious) | Companions::TraitBit(Companions::Trait::Swift);
    BattlePet b = Pet("Spellward", Many(261, 12));
    b.traits = Companions::TraitBit(Companions::Trait::Spellward) | Companions::TraitBit(Companions::Trait::ThickSkulled);
    Battle d({a}, {b}, 4, {false, true}, 0);
    CHECK(d.State(0).Get(Status::Strength) == 1);
    CHECK(d.State(0).hand.size() == kHandSize + 1u);
    // Higher level: a point of Strength per kLevelsPerStrength levels, at most 3.
    Battle l({Pet("Old", Many(9, 12), 60, 30)}, {Pet("Young", Many(9, 12), 60, 10)}, 4);
    CHECK(l.State(0).Get(Status::Strength) == 3 && l.State(1).Get(Status::Strength) == 0);
    Battle m({Pet("Old", Many(9, 12), 60, 25)}, {Pet("Young", Many(9, 12), 60, 20)}, 4);
    CHECK(m.State(0).Get(Status::Strength) == 1);
    // Phoenix-blooded rises once.
    BattlePet ph = Pet("Phoenix", Many(10, 12), 10);
    ph.traits = Companions::TraitBit(Companions::Trait::PhoenixBlooded);
    Battle p({Pet("Big", Many(30, 12))}, {ph}, 4, {false, false}, 0);
    PlayCard(p, 0, 30);
    CHECK(!p.Active(1).beaten && p.Active(1).health == 2);
    // The duel's health: no class in it at all (a hunter's bond never applies here).
    CHECK(DuelHealth(Companions::Role::Melee, 0, 1, 0, 0) == 61);
    CHECK(DuelHealth(Companions::Role::Tank, 2, 60, 4, 0) > DuelHealth(Companions::Role::Ranged, 2, 60, 4, 0));
    CHECK(DuelHealth(Companions::Role::Melee, 1, 30, 0, Companions::TraitBit(Companions::Trait::Giant))
        == DuelHealth(Companions::Role::Melee, 1, 30, 0, 0) + 6);
}

// ── The AI ──────────────────────────────────────────────────────────────────────────────────

TEST(duel_the_npc_shows_its_intent_and_plays_it)
{
    Companions::CompanionSpecies const* wolf = SpeciesOf(Archetype::Pack);
    Companions::CompanionSpecies const* cat = SpeciesOf(Archetype::Feline);
    CHECK(wolf && cat);
    if (!wolf || !cat)
        return;
    Battle b({Grown(*cat, 30, 1, 1)}, {Grown(*wolf, 30, 1, 2)}, 77);
    CHECK(b.IsAi(1) && !b.IsAi(0));
    for (int turn = 0; turn < 12 && b.GetPhase() == Phase::Playing; ++turn)
    {
        // The player ends the turn at once; the NPC's intent says what then happens.
        b.Choose(0, {ActionKind::EndTurn});
        if (b.GetPhase() != Phase::Playing)
            break;
        Intent const in = b.IntentOf(1);
        std::vector<uint16_t> const plan = b.State(1).plan;
        uint16_t const before = uint16_t(b.Active(0).health + b.State(0).block);
        uint16_t const blockBefore = b.State(1).block;
        b.PlayAiTurn();
        CHECK(b.ToPlay() == 0 || b.GetPhase() == Phase::Over);
        uint16_t const after = uint16_t(b.Active(0).health + b.State(0).block);
        uint32_t const dealt = uint32_t(in.damage) * std::max<uint8_t>(in.hits, 1);
        // What it showed is what landed (no evasion, thorns or block on this side).
        if (b.GetPhase() == Phase::Playing && b.ActiveIndex(0) == 0)
            CHECK(uint32_t(before - after) == dealt);
        (void)plan;
        (void)blockBefore;
    }
}

TEST(duel_ai_duels_always_finish_and_replay_the_same)
{
    std::vector<Archetype> const all = {Archetype::Feline, Archetype::Undead, Archetype::Elemental, Archetype::Shell};
    for (Archetype x : all)
        for (Archetype y : all)
        {
            Companions::CompanionSpecies const* a = SpeciesOf(x);
            Companions::CompanionSpecies const* c = SpeciesOf(y);
            if (!a || !c)
                continue;
            auto run = [&](uint32_t seed) {
                Battle b({Grown(*a, 40, 2, 1)}, {Grown(*c, 40, 2, 2), Grown(*c, 30, 1, 1)}, seed, {true, true}, 0);
                for (int i = 0; i < 400 && b.GetPhase() == Phase::Playing; ++i)
                    b.PlayAiTurn();
                return b;
            };
            Battle const one = run(12345), two = run(12345);
            CHECK(one.GetPhase() == Phase::Over);
            CHECK(one.Log() == two.Log());
            CHECK(one.Winner() == two.Winner());
        }
}

// ── Balance: AI against AI across the families ─────────────────────────────────────────────

TEST(duel_no_family_wins_more_than_about_sixty_percent_against_the_field)
{
    struct Row
    {
        uint32_t wins = 0, games = 0;
    };
    auto field = [](uint8_t level, uint8_t stage, uint32_t seeds, char const* label) {
        std::map<Archetype, Row> rows;
        // Every species of each family takes its turn in the field.
        std::map<Archetype, std::vector<Companions::CompanionSpecies const*>> line;
        for (Companions::CompanionSpecies const& sp : Companions::StarterSpecies())
            line[ArchetypeOf(sp.family)].push_back(&sp);
        CHECK(line.size() == size_t(Archetype::Count));
        for (auto const& [ax, as] : line)
            for (auto const& [bx, bs] : line)
            {
                if (ax == bx)
                    continue;
                for (uint32_t seed = 1; seed <= seeds; ++seed)
                {
                    uint8_t const pick = uint8_t(1 + seed % 2);
                    Companions::CompanionSpecies const* asp = as[(seed * 7 + uint32_t(bx)) % as.size()];
                    Companions::CompanionSpecies const* bsp = bs[(seed * 11 + uint32_t(ax)) % bs.size()];
                    Battle b({Grown(*asp, level, stage, pick)}, {Grown(*bsp, level, stage, pick)}, seed * 7919u,
                        {true, true}, int(seed % 2));
                    for (int i = 0; i < 200 && b.GetPhase() == Phase::Playing; ++i)
                        b.PlayAiTurn();
                    rows[ax].games += 2;
                    rows[bx].games += 2;
                    if (b.Winner() == 0)
                        rows[ax].wins += 2;
                    else if (b.Winner() == 1)
                        rows[bx].wins += 2;
                    else
                        ++rows[ax].wins, ++rows[bx].wins;
                }
            }
        std::string line2 = std::string("  balance (") + label + ", level " + std::to_string(level) + " stage "
            + std::to_string(stage) + "):";
        for (auto const& [a, r] : rows)
        {
            float const share = r.games ? float(r.wins) / float(r.games) : 0.f;
            char buf[48];
            std::snprintf(buf, sizeof buf, " %s %.0f%%", ArchetypeName(a), share * 100.f);
            line2 += buf;
        }
        std::printf("%s\n", line2.c_str());
        for (auto const& [a, r] : rows)
        {
            float const share = r.games ? float(r.wins) / float(r.games) : 0.f;
            CHECK(share <= 0.62f);
            CHECK(share >= 0.36f);
        }
    };
    field(40, 2, 12, "grown decks");
    field(12, 0, 12, "starter decks");
    field(60, 2, 12, "grown decks");
}
TEST(battle_tamers_stand_by_zone_level_with_real_teams)
{
    std::vector<Tamer> const& all = Tamers();
    CHECK(all.size() >= 20);
    std::set<uint32_t> entries;
    uint16_t id = 0;
    for (Tamer const& t : all)
    {
        CHECK(t.id == ++id);
        CHECK(t.entry == kTamerEntryFirst + t.id - 1 && t.entry <= kTamerEntryLast);
        CHECK(entries.insert(t.entry).second);
        CHECK(!t.name.empty() && !t.place.empty() && t.zone != 0);
        CHECK(t.side <= 2);
        CHECK(t.team.size() >= 2 && t.team.size() <= kTeamSize);
        CHECK(t.grand == (t.level == 60));
        for (TamerPet const& p : t.team)
        {
            Companions::CompanionSpecies const* sp = Companions::FindSpecies(Companions::StarterSpecies(), p.species);
            CHECK(sp != nullptr);
            CHECK(p.level == t.level);
            CHECK(p.stage == StageForLevel(t.level));
            if (sp)
                CHECK(p.stage < sp->stageCount);
        }
        CHECK(TamerByEntry(all, t.entry) == &t);
        CHECK(FindTamer(all, t.id) == &t);
    }
    // Each side starts at level 5 (Julia Stevens, Zunta) and every side's path ends at 60.
    CHECK(all[0].name == "Julia Stevens" && all[0].level == 5 && all[0].side == 1);
    CHECK(all[1].name == "Zunta" && all[1].level == 5 && all[1].side == 2);
    std::vector<Tamer const*> const horde = NextTamers(all, 2, 1, 3);
    CHECK(horde.size() == 3 && horde[0]->name == "Zunta");
    for (Tamer const* t : horde)
        CHECK(t->side != 1);
    std::vector<Tamer const*> const top = NextTamers(all, 1, 60, 5);
    CHECK(!top.empty());
    for (Tamer const* t : top)
        CHECK(t->level >= 58);
}

TEST(battle_tamers_match_the_world_table)
{
    std::ifstream in(std::string(FUSION_VECTORS_DIR "/../../../sql/custom/azeroth_warfare/") + "33_companion_battles.sql");
    CHECK(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    std::string const sql = ss.str();
    std::regex const row(R"(\((\d+), (\d+), '((?:[^']|'')*)', (\d+), (\d+), (\d+), (\d+), '((?:[^']|'')*)', (\d+), (\d+), (\d+)\))");
    std::vector<Tamer> const& all = Tamers();
    size_t n = 0;
    for (auto it = std::sregex_iterator(sql.begin(), sql.end(), row); it != std::sregex_iterator(); ++it, ++n)
    {
        std::smatch const& m = *it;
        Tamer const* t = FindTamer(all, uint16_t(std::stoul(m[1])));
        CHECK(t != nullptr);
        if (!t)
            continue;
        auto unq = [](std::string s) { return std::regex_replace(s, std::regex("''"), "'"); };
        CHECK(t->entry == std::stoul(m[2]));
        CHECK(t->name == unq(m[3]));
        CHECK(t->zone == std::stoul(m[4]));
        CHECK(t->side == std::stoul(m[5]));
        CHECK(t->level == std::stoul(m[6]));
        CHECK(t->grand == (std::stoul(m[7]) != 0));
        CHECK(t->place == unq(m[8]));
        for (size_t k = 0; k < 3; ++k)
        {
            uint16_t const s = uint16_t(std::stoul(m[9 + k]));
            CHECK(k < t->team.size() ? t->team[k].species == s : s == 0);
        }
        // Its creature template and its spawn are there.
        CHECK(sql.find("SELECT " + std::to_string(t->entry) + ", 0, '") != std::string::npos);
        CHECK(sql.find("SELECT " + std::to_string(2594200 + t->id) + ", " + std::to_string(t->entry)) != std::string::npos);
    }
    CHECK(n == all.size());
    // Safe to apply again.
    CHECK(sql.find("DROP TABLE IF EXISTS `fusion_companion_tamer`") != std::string::npos);
    CHECK(sql.find("DELETE FROM `creature_template` WHERE `entry` BETWEEN 94200 AND 94249") != std::string::npos);
    CHECK(sql.find("DELETE FROM `creature` WHERE `guid` BETWEEN 2594200 AND 2594299") != std::string::npos);
    for (uint32_t e = kTrainerEntryFirst; e <= kTrainerEntryLast; ++e)
        CHECK(sql.find("SELECT " + std::to_string(e) + ", 0, '") != std::string::npos);
}

TEST(battle_some_wanderers_are_tamers_with_teams_of_their_own)
{
    uint32_t tamers = 0;
    for (uint32_t seed = 1; seed <= 2000; ++seed)
        if (IsWandererTamer(seed * 2654435761u))
            ++tamers;
    CHECK(tamers > 300 && tamers < 500);
    for (uint32_t seed : {1u, 77u, 123456u, 0xDEADBEEFu})
    {
        std::vector<TamerPet> const a = WandererTeam(seed, 30);
        std::vector<TamerPet> const b = WandererTeam(seed, 30);
        CHECK(a.size() == 3);
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].species == b[i].species);
            CHECK(Companions::FindSpecies(Companions::StarterSpecies(), a[i].species) != nullptr);
            CHECK(a[i].level == 30 && a[i].stage == 1);
            for (size_t j = 0; j < i; ++j)
                CHECK(a[i].species != a[j].species);
        }
        // The same species as it grows.
        std::vector<TamerPet> const older = WandererTeam(seed, 50);
        for (size_t i = 0; i < older.size(); ++i)
            CHECK(older[i].species == a[i].species && older[i].stage == 2);
    }
    CHECK(WandererTeam(5, 5).size() == 1);
    CHECK(WandererTeam(5, 12).size() == 2);
    CHECK(PracticeTeam(10, 3).size() == 3);
}

// ── Wire ────────────────────────────────────────────────────────────────────────────────────


TEST(duel_requests_round_trip)
{
    auto r = DecodeRequest(EncodeChallenge(0x1122334455667788ull));
    CHECK(r && r->op == kOpChallenge && r->target == 0x1122334455667788ull);
    r = DecodeRequest(EncodeAnswer(true));
    CHECK(r && r->op == kOpAnswer && r->accept);
    r = DecodeRequest(EncodeAction({ActionKind::Play, 2, 0x4123}));
    CHECK(r && r->op == kOpAction && r->action.kind == ActionKind::Play && r->action.index == 2 && r->action.card == 0x4123);
    r = DecodeRequest(EncodeAction({ActionKind::EndTurn, 0}));
    CHECK(r && r->action.kind == ActionKind::EndTurn);
    r = DecodeRequest({kVersion, kOpAsk});
    CHECK(r && r->op == kOpAsk);
    r = DecodeRequest(EncodeAskDeck(42));
    CHECK(r && r->op == kOpAskDeck && r->target == 42);
    std::vector<uint16_t> const cards = {1, 10, 100, 0x4009};
    r = DecodeRequest(EncodeSaveDeck(7, cards));
    CHECK(r && r->op == kOpSaveDeck && r->target == 7 && r->cards == cards);
    CHECK(!DecodeRequest({}));
    CHECK(!DecodeRequest({1, kOpAsk}));                 // the old battle's version
    CHECK(!DecodeRequest({kVersion, kOpAction, 2, 0, 0, 0}));   // no more swaps
    CHECK(!DecodeRequest({kVersion, kOpAction, 1, 0}));
    CHECK(!DecodeRequest({kVersion, kOpChallenge, 1, 2}));
    std::vector<uint8_t> bad = EncodeSaveDeck(7, cards);
    bad.pop_back();
    CHECK(!DecodeRequest(bad));
    CHECK(!DecodeRequest({kVersion, 99}));
    CHECK(Throttle::Costly(kChannel, EncodeAction({ActionKind::Play, 0})));
    CHECK(Throttle::Costly(kChannel, EncodeSaveDeck(1, cards)));
}

TEST(duel_state_reads_from_the_readers_side)
{
    BattlePet a = Pet("Mine", Many(9, 12));
    a.species = 6;
    BattlePet b2 = Pet("Theirs", Many(10, 12));
    b2.species = 26;
    Battle b({a}, {b2}, 5);
    View v;
    v.battle = 77;
    v.side = 1;
    v.owners = {"Bob", "Alice"};
    v.secondsLeft = 25;
    v.newLines = {"one", "two"};
    std::vector<uint8_t> const s = EncodeState(b, v);
    CHECK(s.size() > 40);
    CHECK(s[0] == kVersion && s[1] == kOpState);
    CHECK(s[2] == 77 && s[3] == 0 && s[4] == 0 && s[5] == 0);
    CHECK(s[6] == uint8_t(Phase::Playing));
    CHECK(s[7] == 1 && s[8] == 0);              // turn 1
    CHECK(s[9] == 0 && s[10] == 0xFF);          // no end, no winner
    CHECK(s[11] == 0 && s[12] == 25);           // not the reader's turn (side 0 plays first)
    CHECK(s[13] == 0 && s[14] == kEnergy);      // energy, max
    CHECK(s[15] == kHandSize);                  // the reader's hand: five Guards
    CHECK(s[16] == kGuardCard && s[17] == 0);
    CHECK(s[18] == uint8_t(CardType::Skill) && s[19] == 1 && s[20] == 0);   // not playable now
    // The end: the lines and an empty reward.
    CHECK(s[s.size() - 2] == 0 && s[s.size() - 1] == 0);
    std::vector<uint8_t> const t = EncodeTell("Hi");
    CHECK(t.size() == 6 && t[1] == kOpTell && t[2] == 2 && t[4] == 'H');
    std::vector<uint8_t> const c = EncodeChallenged(5, "Al");
    CHECK(c.size() == 13 && c[1] == kOpChallenged && c[2] == 5 && c[10] == 2);
    DeckView d;
    d.item = 9;
    d.name = "Tabby";
    d.library = {{1, 5, 0}, {10, 5, 0}};
    d.deck = {1, 1, 10};
    std::vector<uint8_t> const dk = EncodeDeck(d);
    CHECK(dk[1] == kOpDeck && dk[2] == 9 && dk[10] == 5 && dk[16] == kMinDeck && dk[17] == kMaxDeck && dk[18] == 2);
}
