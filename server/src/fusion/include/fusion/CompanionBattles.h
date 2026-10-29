/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_COMPANION_BATTLES_H
#define FUSION_COMPANION_BATTLES_H

#include "fusion/CompanionMoves.h"
#include "fusion/CompanionTraits.h"
#include "fusion/Companions.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

// Companion duels (design/COMPANION_BATTLES.md): a turn-based card duel after Slay the Spire.
// Each companion brings a deck built from what it is (its family's cards, its stage picks, its
// traits, its parents', and what it learned); each turn its side gets energy, draws a hand, plays
// cards (attacks, block, skills, powers) and ends the turn. Block falls away at the start of its
// owner's turn. An NPC or wild opponent shows its next turn in advance (its intent) and plays
// exactly that. A side brings up to three companions, one fighting at a time; a companion brought
// to nothing is beaten, left at 1 health (a duel never faints or wounds), and the next steps in.
//
// Everything here is plain rules: the cards, the decks and their rules, a duel's turns, the
// statuses, the AI, the experience, the tamers' data and the wire. Randomness (the shuffles) comes
// from the duel's own seed, so a duel replays the same from the same plays. The game
// (game/Fusion/FusionCompanionBattles.cpp) holds the players, feeds in their plays and the clock,
// and pays what the duel says. Every play is checked here: a client can't play a card it does not
// hold, or one it has no energy for.
namespace Fusion::CompanionBattles
{
    // ── Families ────────────────────────────────────────────────────────────────────────────
    // Blizzard's ten battle families, kept as the window's family label (BattleFamilyOf).
    enum class BattleFamily : uint8_t
    {
        Humanoid = 0, Dragonkin = 1, Flying = 2, Undead = 3, Critter = 4,
        Magic = 5, Elemental = 6, Beast = 7, Aquatic = 8, Mechanical = 9,
        Count = 10,
    };
    char const* FamilyName(BattleFamily f);
    BattleFamily BattleFamilyOf(Companions::Family f);

    // What a companion's deck plays like: each of our families folds into one (ArchetypeOf).
    enum class Archetype : uint8_t
    {
        Feline = 0,     // cats, raptors: bleed and combos
        Ursine = 1,     // bears, gorillas, boars, kodos: block and thorns
        Avian = 2,      // owls, bats, birds, gryphons: evasion and draw
        Draconic = 3,   // dragons, hydras, serpents: powers that grow
        Undead = 4,     // skeletons, ghouls, banshees: exhaust and recursion
        Venom = 5,      // spiders, scorpids, oozes: poison
        Pack = 6,       // wolves, worgen, hyenas, horses: strength and many hits
        Elemental = 7,  // the elementals, infernals: energy and big spells
        Fel = 8,        // imps, felhounds, felguards: curses and drain
        Humanoid = 9,   // murlocs, gnolls, trolls...: tactics and draw
        Shell = 10,     // crocolisks, turtles, crabs: block that hits back
        Mechanical = 11,// golems, shredders, mechanostriders: overclock and plating
        Critter = 12,   // critters: cheap and quick
        Count = 13,
    };
    char const* ArchetypeName(Archetype a);
    Archetype ArchetypeOf(Companions::Family f);

    // ── Statuses ────────────────────────────────────────────────────────────────────────────
    enum class Status : uint8_t
    {
        Exposed = 0,    // takes 50% more attack damage; turns, down by 1 at the end of its turn
        Weakened = 1,   // deals 25% less attack damage; turns
        Poison = 2,     // loses that much health at the start of its turn (block does not help), then 1 less
        Bleed = 3,      // loses that much health at the start of its turn, then half as much
        Strength = 4,   // +1 damage per hit per point
        Agility = 5,    // +1 block per block card per point
        Thorns = 6,     // an attacker takes this much per hit
        Evasion = 7,    // the next hits on it miss, one per point
        Regen = 8,      // heals that much at the end of its turn, then 1 less
        Ritual = 9,     // power: Strength at the start of each turn
        Fortify = 10,   // power: block at the start of each turn
        Plague = 11,    // power: Poison on the enemy at the start of each turn
        Insight = 12,   // power: draws this many more each turn
        Plating = 13,   // power: its block no longer falls away
        Frenzy = 14,    // power: each attack after the first in a turn deals this much more
        Undying = 15,   // power: block whenever one of its cards is exhausted
        Drained = 16,   // has this much less energy next turn
        Attunement = 17,// power: this much more energy each turn
        Bloodlust = 18, // power: Strength at the start of each turn, for 2 health
        Count = 19,
    };
    char const* StatusName(Status s);
    bool IsDebuff(Status s);

    // ── Cards ───────────────────────────────────────────────────────────────────────────────
    enum class CardType : uint8_t
    {
        Attack = 0,
        Skill = 1,
        Power = 2,      // stays in play: removed from the deck for the rest of the duel when played
    };
    enum class Special : uint8_t
    {
        None = 0,
        Combo = 1,          // +`extra` damage per hit when another attack was played this turn
        BleedBurst = 2,     // +the target's Bleed to the damage
        BlockStrike = 3,    // +the user's block to the damage
        PoisonDouble = 4,   // doubles the target's Poison
        Recall = 5,         // the card exhausted last comes back to the hand
        PerExhausted = 6,   // +`extra` damage per card in the user's exhaust pile
        EvasionDraw = 7,    // draws 1 more when the user has Evasion
        Underdog = 8,       // +`extra` damage when the user has less health than the target
        Cleanse = 9,        // removes the user's debuffs
    };

    // Card ids: 1-999 the fixed table (CardTable); kAbilityCardBase + the moveset, stage and
    // option for a card made from a catalog ability (AbilityCardId).
    constexpr uint16_t kAbilityCardBase = 0x4000;
    struct Card
    {
        uint16_t id = 0;
        std::string name;
        CardType type = CardType::Attack;
        uint8_t cost = 1;
        std::string icon;               // a stock icon's name (Interface\Icons\<icon>)
        uint32_t spell = 0;             // a catalog ability's spell, for its own icon (0 none)
        Archetype archetype = Archetype::Count;  // Count: any companion (basics, lessons, traits)
        uint16_t damage = 0;
        uint8_t hits = 0;
        uint16_t block = 0;
        uint8_t draw = 0;
        uint8_t energy = 0;             // energy gained
        uint8_t heal = 0;
        uint8_t selfHurt = 0;           // health the user pays
        std::array<int8_t, uint8_t(Status::Count)> enemy{};    // statuses put on the target
        std::array<int8_t, uint8_t(Status::Count)> self{};     // statuses put on the user
        Special special = Special::None;
        uint8_t extra = 0;
        bool exhaust = false;           // removed for the duel when played
        bool basic = false;             // a basic attack or Guard (more copies allowed)

        bool Exists() const { return id != 0; }
    };
    // The card of an id (fixed or ability), an empty Card (id 0) for none.
    Card const& CardById(uint16_t id);
    // The plain description of a card ("Deal 6 damage. Apply 2 Bleed.").
    std::string CardText(Card const& c);
    // The fixed cards, by id order.
    std::vector<Card> const& CardTable();
    uint16_t AbilityCardId(Companions::Moveset m, uint8_t stage, uint8_t option);
    // A catalog ability as a card (design section 3.3).
    Card CardFromAbility(Companions::Ability const& a, uint16_t id);
    // The basic attack card of a moveset (Bite, Claw, Firebolt...) and the shared Guard.
    uint16_t BasicAttackCard(Companions::MovesetInfo const& m);
    constexpr uint16_t kGuardCard = 10;

    // ── Decks ───────────────────────────────────────────────────────────────────────────────
    constexpr uint8_t kMinDeck = 12;
    constexpr uint8_t kMaxDeck = 30;
    constexpr uint8_t kMaxCopies = 2;
    constexpr uint8_t kMaxBasicCopies = 5;
    // The levels an archetype's library cards are learned at, in its card order.
    constexpr std::array<uint8_t, 6> kLibraryLevels{4, 10, 18, 26, 34, 44};

    struct LibraryCard
    {
        uint16_t card = 0;
        uint8_t copies = 1;             // at most this many in a deck
        uint8_t source = 0;             // CardSource
    };
    enum class CardSource : uint8_t
    {
        Basic = 0, Family = 1, Level = 2, Ability = 3, Trait = 4, Bred = 5, Learned = 6,
    };
    // An archetype's own cards: its two starters, then its library in kLibraryLevels order.
    std::vector<uint16_t> ArchetypeCards(Archetype a);
    // The cards a companion knows: the basics, its archetype's starters, its library up to its
    // level, a card for each ability it picked, its traits' cards, a starter of each parent's
    // archetype when bred, and what it learned (`learned`: drops and trainers' lessons).
    std::vector<LibraryCard> Library(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s,
        std::vector<uint16_t> const& learned, std::vector<Companions::CompanionSpecies> const& all);
    // The deck a companion has before its owner edits one: 5 basic attacks, 4 Guards, its two
    // starters (one twice) and one of each ability card it picked.
    std::vector<uint16_t> StarterDeck(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s);

    enum class DeckProblem : uint8_t
    {
        None = 0, TooSmall = 1, TooLarge = 2, Unknown = 3, TooManyCopies = 4,
    };
    char const* DeckProblemText(DeckProblem p);
    DeckProblem CheckDeck(std::vector<LibraryCard> const& library, std::vector<uint16_t> const& deck);
    // An NPC's deck: the starter deck and one of each library card its level has learned.
    std::vector<uint16_t> BuiltDeck(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s);
    // The deck a companion duels with: its saved one when it is still valid, else its starter.
    std::vector<uint16_t> DuelDeck(std::vector<LibraryCard> const& library, std::vector<uint16_t> const& saved,
        Companions::CompanionSpecies const& sp, Companions::CompanionState const& s);
    // Saved lists ("12,12,10,...").
    std::string JoinCards(std::vector<uint16_t> const& cards);
    std::vector<uint16_t> SplitCards(std::string const& text);

    // Lessons a Companion Battle Trainer sells, and what a tamer's daily reward may teach.
    std::vector<uint16_t> const& TrainerCards();
    uint32_t TrainerPrice(uint16_t card, uint8_t ownerLevel);
    std::vector<uint16_t> const& DropCards();
    // A tamer's card drop for a companion (by the roll): a drop card it does not know yet, else 0.
    uint16_t RollDropCard(std::vector<LibraryCard> const& library, uint32_t roll);

    // ── A companion in a duel ───────────────────────────────────────────────────────────────
    struct BattlePet
    {
        uint64_t item = 0;                      // its companion item (0: an NPC's)
        uint16_t species = 0;
        std::string name;
        Companions::Family family = Companions::Family::Critter;
        BattleFamily battleFamily = BattleFamily::Critter;
        Archetype archetype = Archetype::Critter;
        uint8_t stage = 0;
        uint8_t level = 1;
        uint8_t rarity = 0;
        uint32_t traits = 0;
        uint16_t maxHealth = 1;
        uint16_t health = 1;
        std::vector<uint16_t> deck;
        bool fought = false;                    // was in at some point
        bool beaten = false;                    // brought to 1 health: out of the duel
        bool phoenixUsed = false;

        bool Standing() const { return !beaten; }
        float HealthShare() const { return maxHealth ? float(health) / float(maxHealth) : 0.f; }
    };
    // A duel's health: 60 + 0.6 a level + 10 a stage, rarity +2 a step, by role (tanks +5%,
    // ranged -5%), Giant +6. No class bonus: a hunter's bond (ClassTuning) never comes in.
    uint16_t DuelHealth(Companions::Role role, uint8_t stage, uint8_t level, uint8_t rarity, uint32_t traits);
    // A companion as it duels, with its deck; `level` overrides its own (PvP's levelling down, a
    // tamer's set level), 0 its own.
    BattlePet MakePet(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s, uint64_t item,
        std::vector<uint16_t> deck, uint8_t level = 0);
    // A companion with its starter deck (NPCs, tests).
    BattlePet MakePet(Companions::CompanionSpecies const& sp, Companions::CompanionState const& s, uint64_t item,
        uint8_t level = 0);

    // ── The duel ────────────────────────────────────────────────────────────────────────────
    constexpr uint8_t kTeamSize = 3;
    constexpr uint8_t kEnergy = 3;
    constexpr uint8_t kHandSize = 5;
    constexpr uint8_t kMaxHand = 10;
    constexpr uint16_t kMaxTurns = 50;          // both sides' turns together
    constexpr uint32_t kTurnMs = 45000;         // a side not ending its turn in time ends it
    constexpr uint8_t kMaxTimeouts = 3;         // timeouts in a row forfeit
    // A side whose companion is this many levels above the other's starts with 1 Strength per
    // step (up to 3): a tamer of a higher level hits harder.
    constexpr uint8_t kLevelsPerStrength = 4;

    enum class ActionKind : uint8_t
    {
        None = 0,
        Play = 1,       // index: the hand slot; card: the card the client sees there
        EndTurn = 3,
        Forfeit = 4,
    };
    struct Action
    {
        ActionKind kind = ActionKind::None;
        uint8_t index = 0;
        uint16_t card = 0;      // 0: whatever is in the slot
    };

    enum class Phase : uint8_t
    {
        Playing = 0,
        Over = 2,
    };
    enum class End : uint8_t
    {
        None = 0,
        Defeated = 1,   // a side has no companion standing
        Forfeit = 2,
        Turns = 3,      // the turn limit: the larger share of health wins
        Abandoned = 4,  // the game ended it: nobody wins
    };
    // As the real-time companion duel's end, for Companions::DuelPaysXp.
    Companions::DuelEnd DuelEndOf(End e);

    enum class Refusal : uint8_t
    {
        None = 0,
        NotYourTurn = 1,
        NoSuchCard = 2,
        NoEnergy = 3,
        Over = 4,
        Stale = 5,      // the slot holds another card now
    };
    char const* RefusalText(Refusal r);

    // An NPC's next turn as the window shows it.
    struct Intent
    {
        uint16_t damage = 0;    // per hit, with the statuses now
        uint8_t hits = 0;
        uint16_t block = 0;
        bool buff = false;
        bool debuff = false;
        bool power = false;
    };

    // One side's piles and state for its fighting companion.
    struct SideState
    {
        std::vector<uint16_t> draw, hand, discard, exhaust;
        uint8_t energy = 0;
        uint16_t block = 0;
        std::array<int16_t, uint8_t(Status::Count)> status{};
        uint8_t attacksThisTurn = 0;
        uint16_t lastExhausted = 0;
        std::vector<uint16_t> plan;     // an AI side: the cards it will play next turn, in order
        bool firstTurn = true;
        uint8_t timeouts = 0;

        int16_t Get(Status s) const { return status[uint8_t(s)]; }
    };

    class Battle
    {
    public:
        Battle() = default;
        // `ai[s]`: the side is played by the rules' AI and shows its intent. A side of players
        // both: side `first` (by the seed when -1) goes first and draws one card fewer then.
        Battle(std::vector<BattlePet> a, std::vector<BattlePet> b, uint32_t seed, std::array<bool, 2> ai = {false, true},
            int first = 0);

        Phase GetPhase() const { return m_phase; }
        End GetEnd() const { return m_end; }
        int Winner() const { return m_winner; }
        uint16_t Turn() const { return m_turn; }
        int ToPlay() const { return m_toPlay; }
        bool IsAi(int side) const { return m_ai[side & 1]; }

        std::vector<BattlePet> const& Team(int side) const { return m_team[side & 1]; }
        uint8_t ActiveIndex(int side) const { return m_active[side & 1]; }
        BattlePet const& Active(int side) const { return m_team[side & 1][m_active[side & 1]]; }
        SideState const& State(int side) const { return m_side[side & 1]; }

        bool NeedsChoice(int side) const { return m_phase == Phase::Playing && m_toPlay == (side & 1); }
        Refusal Check(int side, Action a) const;
        Refusal Choose(int side, Action a);
        // The turn's clock ran out: the side to play ends its turn; kMaxTimeouts in a row forfeit.
        void Timeout();
        void Abandon();
        // An AI side plays its whole planned turn (the game calls it after a short pause).
        void PlayAiTurn();

        // What a card would do now (its damage per hit and block, with the statuses).
        uint16_t CardDamage(int side, Card const& c) const;
        uint16_t CardBlock(int side, Card const& c) const;
        Intent IntentOf(int side) const;

        std::vector<std::string> const& Log() const { return m_log; }
        uint8_t Defeated(int side) const;      // how many of `side`'s enemies are beaten

        // A copy without the log, for the AI's look ahead.
        Battle Fork() const;
        friend std::vector<uint16_t> PlanTurn(Battle const& b, int side);

    private:
        void Begin();
        void StartTurn(int side);
        void EndTurnOf(int side, bool fresh = false);
        void DrawCards(int side, uint8_t n);
        void Play(int side, uint8_t slot);
        void Hit(int side, uint16_t damage);
        void Hurt(int side, uint16_t amount, bool attack);
        void GainBlock(int side, uint16_t amount);
        void Exhausted(int side, uint16_t card);
        void Plan(int side);
        void CheckBeaten();
        void Ready(int side);
        void Shuffle(std::vector<uint16_t>& v);
        void Finish(End end, int winner);
        uint32_t Roll(uint32_t n);
        void Say(std::string line) { m_log.push_back(std::move(line)); }
        BattlePet& Me(int side) { return m_team[side & 1][m_active[side & 1]]; }

        std::array<std::vector<BattlePet>, 2> m_team;
        std::array<uint8_t, 2> m_active{};
        std::array<SideState, 2> m_side;
        std::array<bool, 2> m_ai{false, true};
        Phase m_phase = Phase::Playing;
        End m_end = End::None;
        int m_winner = -1;
        int m_toPlay = 0;
        uint16_t m_turn = 1;
        uint32_t m_rng = 1;
        bool m_quiet = false;
        bool m_starting = false;
        bool m_passTurn = false;
        std::vector<std::string> m_log;
    };

    // The AI's next turn for the hand `side` holds: the cards it plays, in order. Greedy: it tries
    // each card it can pay for on a copy of the duel and keeps the one that leaves it best off
    // (damage dealt, block against what is coming, statuses, powers, energy kept), until none helps.
    std::vector<uint16_t> PlanTurn(Battle const& b, int side);

    // ── Experience ──────────────────────────────────────────────────────────────────────────
    constexpr uint32_t kTamerXpFactor = 3;
    constexpr uint32_t kPvpXpFactor = 2;
    // Per team slot of `side`: what it earned, for each enemy beaten (KillXp x factor) — only a
    // winner's companions that fought and still stand, and only for a duel that pays
    // (Companions::DuelPaysXp: never a forfeit or an abandoned duel).
    std::vector<uint32_t> XpEarned(Battle const& b, int side, uint32_t factor);
    uint32_t DailyBonusXp(uint8_t level);
    uint32_t DailyMoney(uint8_t tamerLevel, uint8_t teamSize);
    uint8_t PvpLevelCap(std::vector<uint8_t> const& a, std::vector<uint8_t> const& b);

    // ── Tamers ──────────────────────────────────────────────────────────────────────────────
    struct TamerPet
    {
        uint16_t species = 0;
        uint8_t stage = 0;
        uint8_t level = 1;
        std::array<uint8_t, Companions::kMaxStages> picks{1, 1, 1};
    };
    struct Tamer
    {
        uint16_t id = 0;
        uint32_t entry = 0;
        std::string name;
        uint32_t zone = 0;
        uint8_t side = 0;
        uint8_t level = 1;
        bool grand = false;
        std::string place;
        std::vector<TamerPet> team;
    };
    constexpr uint32_t kTamerEntryFirst = 94201;
    constexpr uint32_t kTamerEntryLast = 94229;
    constexpr uint32_t kTrainerEntryFirst = 94230;
    constexpr uint32_t kTrainerEntryLast = 94235;
    constexpr uint8_t kTamerRewardDraughts = 1;
    std::vector<Tamer> const& Tamers();
    Tamer const* FindTamer(std::vector<Tamer> const& all, uint16_t id);
    Tamer const* TamerByEntry(std::vector<Tamer> const& all, uint32_t entry);
    std::vector<Tamer const*> NextTamers(std::vector<Tamer> const& all, uint8_t side, uint8_t level, size_t count);
    std::vector<TamerPet> PracticeTeam(uint8_t level, uint32_t seed);
    bool IsWandererTamer(uint32_t seed);
    std::vector<TamerPet> WandererTeam(uint32_t seed, uint8_t level);
    uint8_t StageForLevel(uint8_t level);

    // ── Wire (fusion channel 13, version 2: the card duel) ──────────────────────────────────
    constexpr uint8_t kChannel = 13;
    constexpr uint8_t kVersion = 2;
    // Server to client.
    constexpr uint8_t kOpState = 1;
    constexpr uint8_t kOpChallenged = 2;
    constexpr uint8_t kOpTell = 3;
    constexpr uint8_t kOpDeck = 4;
    // Client to server.
    constexpr uint8_t kOpChallenge = 10;        // target player guid u64
    constexpr uint8_t kOpAnswer = 11;           // accept u8
    constexpr uint8_t kOpAction = 12;           // kind u8, index u8, card u16
    constexpr uint8_t kOpAsk = 13;
    constexpr uint8_t kOpAskDeck = 14;          // item u64 (0: the companion out)
    constexpr uint8_t kOpSaveDeck = 15;         // item u64, count u8, cards u16 each

    struct View
    {
        uint32_t battle = 0;
        int side = 0;
        std::array<std::string, 2> owners;      // the reader's first
        uint32_t secondsLeft = 0;
        std::vector<std::string> newLines;
        std::string rewards;
    };
    // [version, op 1, battle u32, phase u8, turn u16, end u8, winner i8 (0 you, 1 them, -1),
    //  your turn u8, seconds u8, energy u8, max energy u8,
    //  hand u8 [card], then each side (yours first): owner str8, active u8, hand u8, draw u8,
    //  discard u8, exhaust u8, block u16, statuses u8 [kind u8, amount i16], intent (known u8,
    //  damage u16, hits u8, block u16, flags u8: 1 buff 2 debuff 4 power), count u8, each pet:
    //  species u16, family u8, battle family u8, archetype u8, stage u8, level u8, health u16,
    //  max u16, beaten u8, name str8;
    //  then lines u8 [str16], rewards str16]
    // A card: id u16, type u8, cost u8, playable u8, damage u16, block u16, spell u32,
    //  icon str8, name str8, text str16.
    std::vector<uint8_t> EncodeState(Battle const& b, View const& v);
    std::vector<uint8_t> EncodeChallenged(uint64_t challenger, std::string const& name);
    std::vector<uint8_t> EncodeTell(std::string const& text);

    struct DeckView
    {
        uint64_t item = 0;
        std::string name;
        std::vector<LibraryCard> library;
        std::vector<uint16_t> deck;
        std::string note;
    };
    // [version, op 4, item u64, name str8, min u8, max u8, library u8 [card, copies u8, source u8,
    //  in deck u8], note str16]
    std::vector<uint8_t> EncodeDeck(DeckView const& d);

    struct Request
    {
        uint8_t op = 0;
        uint64_t target = 0;        // a player (challenge) or an item (deck)
        bool accept = false;
        Action action;
        std::vector<uint16_t> cards;
    };
    std::optional<Request> DecodeRequest(std::vector<uint8_t> const& body);
    std::vector<uint8_t> EncodeChallenge(uint64_t target);
    std::vector<uint8_t> EncodeAnswer(bool accept);
    std::vector<uint8_t> EncodeAction(Action a);
    std::vector<uint8_t> EncodeAskDeck(uint64_t item);
    std::vector<uint8_t> EncodeSaveDeck(uint64_t item, std::vector<uint16_t> const& cards);
}

#endif
