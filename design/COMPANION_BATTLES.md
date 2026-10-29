# World of Modern Warfare: companion duels

Status: **card duels implemented** (branch `spire-duels`), replacing phase 1's Pokémon-style team
battle. Rules: `server/src/fusion/include/fusion/CompanionBattles.h` (tests
`tests/CompanionBattleTests.cpp`, with the balance sim). Game: `server/src/game/Fusion/
FusionCompanionBattles.cpp`. World data: `server/sql/custom/azeroth_warfare/33_companion_battles.sql`.
Client: `client/src/companion_battle.rs`. Wire: fusion channel 13, version 2.

Battle companions keep fighting at their owner's side in the open world exactly as before
(`server/src/fusion/docs/COMPANIONS.md`), and the real-time companion duel (Companions.h's
`CompanionDuel`, the Companions window's Duel Target) is unchanged. This is the separate,
turn-based game: a **card duel in the style of Slay the Spire**. Each companion fights with a deck
that comes from what it is (its family, its evolution stage and picks, its traits, its parents)
and what it has learned. It is played against **companion tamers** (NPCs in towns by zone level,
and some Wanderers), at the **Companion Battle Trainers** (practice), and against **other players**.

## 1. How a duel plays

- **Sides.** Each side brings up to three companions (the one out first, then the highest level
  carried). One fights at a time. A companion brought to nothing is **beaten**: it stays at 1
  health, out of this duel, and the next steps in with a fresh shuffle and hand (if it was beaten
  on its own turn, by thorns or its own price, the turn passes). A side with nobody standing loses.
  A duel never wounds or faints anyone in the world.
- **A turn.** At the start of your turn your **Block falls away** (unless you have Plating),
  poison and bleeding tick, your powers fire, and you get **3 energy**. You hold **5 cards** (drawn
  at the end of your last turn, so you see your next hand while the other side plays). Play cards
  for their energy in any order, then **End Turn**: the rest of the hand is discarded, Exposed and
  Weakened run down by one, and you draw your next 5. An empty draw pile reshuffles the discard
  pile (seeded).
- **Cards.** **Attacks** deal damage (Block soaks it first), **skills** block, buff, curse or
  draw, **powers** stay in play for the rest of the duel (they leave the deck when played).
  **Exhaust** removes a card for the rest of the duel.
- **Intents.** An NPC or wild opponent shows, during your turn, what it will do on its own:
  "Attack 12 x2", "Block 8", "Buff", "Debuff", "Power". The numbers are worked out by playing its
  plan on a copy of the duel, so a power or combo early in its turn shows in the hits after it,
  and your Weakened or Exposed on it show at once. The rules' AI plans that turn from the hand it
  holds and then **plays exactly that plan** (a card it can no longer pay for, because you Drained
  it, is skipped).
- **Statuses** (WoW names): Exposed (+50% attack damage taken), Weakened (-25% attack damage
  dealt), Poison (ticks, then 1 less; block does not help), Bleed (ticks, then halves), Strength
  (+1 per hit), Agility (+1 Block per block card), Thorns (attackers take it per hit), Evasion
  (the next hit misses), Regeneration (heals at turn end, then 1 less), Drained (less energy next
  turn); the powers Draconic Might (Strength each turn), Fortify (Block each turn), Plague (Poison
  the enemy each turn), Insight (draw more), Plating (Block stays), Frenzy (later attacks hit
  harder), Undying (Block per card exhausted), Attunement (more energy), Bloodlust (Strength each
  turn for 2 health).
- **Levels.** Health = 60 + 0.6 a level + 10 a stage, +2 per rarity step, tanks +5%, ranged -5%,
  Giant +6 (`DuelHealth`). A companion 4 or more levels above the one it faces starts with 1
  Strength per 4 levels (at most 3), so a higher tamer hits harder; PvP levels both sides down
  first.
- **Traits as relics.** Ferocious and Alpha start with 1 Strength, Primal with 1 Strength and 1
  Agility, Ironhide with 1 Agility, Runt with 1 Evasion; Swift draws a sixth card at the start;
  Loyal has 1 more energy on its first turn; Venomous poisons with its first attack each turn;
  Bloodthirsty heals 1 when an attack gets through; Berserk gains 2 Strength when it first drops
  under half; Thick-skulled can't be Weakened, Spellward can't be Exposed; Phoenix-blooded rises
  once at a quarter health instead of being beaten. Fire/Frost/Shadow/Storm-touched and Gunner
  each add a card. Twin-souled keeps both abilities of its stage as cards.
- **Ends.** Nobody standing (the other side wins); a forfeit; the turn limit (50 turns in all: the
  larger share of health wins, within 1% a draw); three timed-out turns in a row (45 seconds a
  turn); the game calls it off (an owner left the world, died, was pulled into combat, the NPC went
  away).
- **The server decides everything.** A play names a hand slot and the card the client sees there;
  the rules refuse a card not in the hand, one there is no energy for, a stale slot, or any play
  out of turn. The client never sends damage, energy or a card it does not hold.

### PvP: alternating turns

Two players **alternate whole turns** (the seed picks who starts). The player who starts draws a
card fewer on the first turn (4), the usual answer to going first in a card game. We chose this
over simultaneous turns because the game is about reacting to the board (block against what was
just put on you, burst when they are Exposed); with simultaneous turns blocking would be a guess,
and the intent, the heart of the Spire's design, would have nothing to show. A player's intent shows
as "Unknown": only NPCs telegraph. The 45-second clock keeps it moving.

## 2. Decks

### Where cards come from

| Source | What | Copies in a deck |
|---|---|---|
| Basics | The companion's **basic attack** (Bite, Claw, Firebolt... by its moveset; 6 damage) and **Guard** (5 Block) | 5 each |
| Family | Its archetype's **two starters** | 2 |
| Levels | Its archetype's six library cards, learned at levels 4, 10, 18, 26, 34 and 44 | 2 |
| Evolution | Each **ability it picked** at a stage is a card (both for a Twin-souled stage), made from the catalog's kind and numbers | 1 |
| Traits | Fire-touched Scorch, Frost-touched Frost Nova, Shadow-touched Shadow Bite, Storm-touched Static Charge, Gunner Gunshot | 1 |
| Breeding | A bred companion knows the first starter of each parent's archetype (when not its own) | 1 |
| Trainers | A Companion Battle Trainer teaches **lessons** for money ("Teach my companion a card"; the companion out learns it) | 2 |
| Drops | A tamer's **first win of the day** also teaches the lead companion a reward card it lacks | 2 |

### The rules

- A deck holds **12 to 30 cards**, at most **2 copies** of a card (5 of the basics; 1 of an
  ability, trait or bred card), and only cards the companion knows. The server checks every saved
  deck (`CheckDeck`).
- **Starter deck**, which is also the migration (every existing companion has one at once, with no
  data to convert): 5 basic attacks, 4 Guards, the first starter twice and the second once, one of
  each ability it picked and each trait card. A saved deck that stops holding (an ability changed
  at a stable master) falls back to the starter deck, and the deck window says so.
- NPCs play a **built deck**: the starter plus one of each library card their level knows, a basic
  and a Guard trimmed for every two learned.
- **Editing:** `.fusion petbattle deck`, or Deck at the end of a duel, opens the Companion Deck
  window for the companion out: every card it knows, where it came from, how many it may have, +
  and -, Save. Saved per item in `characters.character_fusion_companion_deck` (the deck and the
  learned cards; made at startup, orphans cleaned, forgotten with the item).

## 3. The families

Our families fold into thirteen archetypes (`ArchetypeOf`), each with eight cards: two starters
and a library learned by level. Blizzard's ten battle families stay as the window's label.

| Archetype | Families | Plays |
|---|---|---|
| Feline | Cat, Raptor | **Bleed and combos**: Rake bleeds, Pounce, Shred and Ferocious Bite hit harder after another attack, Rip cashes the bleed in, Savage Roar strengthens every later attack |
| Ursine | Bear, Gorilla, Boar, Kodo | **Block and thorns**: Maul hits and blocks, Bristle and Spiked Hide grow Thorns, Barkskin fortifies every turn, Hibernate |
| Avian | Owl, Bat, Carrion Bird, Harpy, Tallstrider, Gryphon, Wind Rider, Hippogryph, Dragonhawk | **Evasion and draw**: Wing Buffet draws (twice with Evasion), Take Wing and Updraft dodge, Keen Eyes draws every turn, Flurry of Talons |
| Draconic | Dragon, Faerie Dragon, Hydra, Wind Serpent | **Powers**: Draconic Might (Strength now and every turn), Dragonscale, Ancient Fury; Flame Breath and Dragonfire to spend it |
| Undead | Skeleton, Ghoul, Banshee, Crypt Fiend, Skeletal Horse | **Exhaust and recursion**: free Grave Claws that exhaust, Unending gives Block per exhaust, Raise Dead brings the last exhausted card back, Corpse Explosion grows with the exhaust pile |
| Venom | Spider, Scorpid, Basilisk, Poison Elemental, Bog Beast, Ooze | **Poison**: Poison Fang, Toxic Spit, Catalyst doubles it, Noxious Cloud poisons every turn, Paralytic Venom drains energy |
| Pack | Wolf, Worgen, Hyena, Horse, Ram | **Strength and many hits**: Pack Tactics and Tear hit several times, Howl and Bloodlust stack Strength |
| Elemental | Fire, Water, Earth and Air Elementals, Infernal | **Energy and big spells**: Mana Surge, Attunement, Lava Burst (28), Chain Lightning, Stoneskin |
| Fel | Imp, Felhound, Darkhound, Felguard | **Curses and drain**: Shadow Bolt and Curse of Weakness expose and weaken, Doom, Drain Life, Life Tap |
| Humanoid | Murloc, Gnoll, Kobold, Trogg, Ogre, Centaur, Quilboar, Furbolg, Troll, Naga, Moonkin | **Tactics and draw**: Quick Shot, Tactics (draw 3), Shield Bash, Ambush, War Stomp |
| Shell | Crocolisk, Turtle, Crab, Threshadon, Makrura | **Block that hits back**: Shell Up then Shell Slam (damage plus your Block), Hardened Shell keeps Block, Death Roll, Healing Tides |
| Mechanical | Mechanical, Mechanostrider | **Overclock and plating**: Overclock (energy for health), Reinforce, Shield Generator, Rocket Barrage, Death Ray |
| Critter | Critter | **Cheap and quick**: free Nibbles and Scurry, Squeak, Frenzied Nibbles, Underdog hits harder from behind |

### Every card

The table below is the rules' `CardTable()` (each archetype's first two are its starters, then its
library in level order).

**Basics**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 1 | Bite | Attack | 1 | deal 6 |
| 2 | Claw | Attack | 1 | deal 6 |
| 3 | Firebolt | Attack | 1 | deal 6 |
| 4 | Lightning Breath | Attack | 1 | deal 6 |
| 5 | Acid Spit | Attack | 1 | deal 6 |
| 6 | Frostbolt | Attack | 1 | deal 6 |
| 7 | Lightning Bolt | Attack | 1 | deal 6 |
| 8 | Snap Kick | Attack | 1 | deal 6 |
| 9 | Strike | Attack | 1 | deal 6 |
| 10 | Guard | Skill | 1 | block 5 |

**Lessons (trainers)**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 20 | Bandage | Skill | 1 | heal 6, exhaust |
| 21 | Quick Jab | Attack | 0 | deal 3 |
| 22 | Battle Shout | Skill | 1 | you 1 Strength, block 3, exhaust |
| 23 | Sunder | Attack | 2 | deal 8, 2 Exposed |
| 24 | Shield Wall | Skill | 2 | block 13 |
| 25 | Feint | Skill | 0 | 1 Weakened, draw 1 |
| 26 | Cleave | Attack | 1 | deal 8 |
| 27 | Cleanse | Skill | 1 | remove your debuffs, block 4 |

**Drops (tamers)**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 30 | Mortal Strike | Attack | 2 | deal 14 |
| 31 | Adrenaline Rush | Skill | 0 | +2 energy, draw 1, exhaust |
| 32 | Blade Flurry | Power | 2 | you 3 Frenzy |
| 33 | Inner Fire | Power | 1 | you 1 Strength, you 2 Fortify |
| 34 | Thorns | Skill | 1 | you 2 Thorns, exhaust |
| 35 | Healing Potion | Skill | 0 | heal 10, exhaust |

**Traits**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 40 | Scorch | Attack | 1 | deal 8 |
| 41 | Frost Nova | Skill | 1 | 2 Weakened, block 4 |
| 42 | Shadow Bite | Attack | 1 | deal 6, heal 2 |
| 43 | Static Charge | Attack | 1 | deal 3 x3 |
| 44 | Gunshot | Attack | 1 | deal 8 |

**Feline**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 100 | Rake | Attack | 1 | deal 3, 3 Bleed |
| 101 | Pounce | Attack | 0 | deal 3, combo +4 |
| 102 | Shred | Attack | 1 | deal 6, combo +4 |
| 103 | Prowl | Skill | 1 | you 1 Evasion, draw 1 |
| 104 | Rip | Attack | 2 | deal 8, + the target's Bleed |
| 105 | Savage Roar | Power | 1 | you 2 Frenzy |
| 106 | Ferocious Bite | Attack | 1 | deal 4 x2, combo +2 |
| 107 | Tiger's Fury | Skill | 0 | 5 Bleed, draw 1, exhaust |

**Ursine**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 120 | Maul | Attack | 2 | deal 10, block 4 |
| 121 | Bristle | Skill | 1 | block 5, you 1 Thorns |
| 122 | Demoralizing Roar | Skill | 1 | 2 Weakened, block 3 |
| 123 | Swipe | Attack | 1 | deal 8 |
| 124 | Spiked Hide | Power | 1 | you 2 Thorns |
| 125 | Barkskin | Power | 2 | you 4 Fortify |
| 126 | Hibernate | Skill | 2 | block 14, heal 5, exhaust |
| 127 | Frenzied Regeneration | Skill | 1 | you 5 Regen, exhaust |

**Avian**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 140 | Wing Buffet | Attack | 1 | deal 8, draw 1, draw 1 more with Evasion |
| 141 | Take Wing | Skill | 1 | you 1 Evasion, block 6, draw 1 |
| 142 | Screech | Skill | 0 | 1 Weakened, draw 1 |
| 143 | Dive | Attack | 1 | deal 10 |
| 144 | Keen Eyes | Power | 1 | you 1 Insight |
| 145 | Flurry of Talons | Attack | 2 | deal 5 x4 |
| 146 | Updraft | Skill | 0 | you 2 Evasion, exhaust |
| 147 | Gust | Attack | 0 | deal 5, draw 1 |

**Draconic**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 160 | Flame Breath | Attack | 2 | deal 12, 2 Exposed |
| 161 | Draconic Might | Power | 1 | you 1 Ritual, you 1 Strength |
| 162 | Dragonscale | Power | 1 | you 3 Agility, block 4 |
| 163 | Tail Swipe | Attack | 1 | deal 8, 1 Weakened |
| 164 | Wing Storm | Attack | 2 | deal 6 x2, block 6 |
| 165 | Ancient Fury | Power | 1 | you 2 Strength |
| 166 | Dragonfire | Attack | 2 | deal 15 |
| 167 | Hoard's Ward | Skill | 1 | block 10 |

**Undead**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 180 | Grave Claw | Attack | 0 | deal 7, exhaust |
| 181 | Unending | Power | 1 | you 4 Undying |
| 182 | Raise Dead | Skill | 0 | return the last exhausted card, draw 1, exhaust |
| 183 | Bone Shield | Skill | 1 | block 11, exhaust |
| 184 | Death Coil | Attack | 1 | deal 8, heal 4, exhaust |
| 185 | Corpse Explosion | Attack | 1 | deal 3, +2 per exhausted card, exhaust |
| 186 | Plague Strike | Attack | 1 | deal 5, 2 Poison |
| 187 | Unholy Frenzy | Skill | 1 | you 1 Strength, draw 1, exhaust |

**Venom**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 200 | Poison Fang | Attack | 1 | deal 4, 2 Poison |
| 201 | Toxic Spit | Skill | 1 | 3 Poison |
| 202 | Web | Skill | 1 | 2 Weakened, block 4 |
| 203 | Crippling Sting | Attack | 1 | deal 5, 1 Exposed, 2 Poison |
| 204 | Catalyst | Skill | 2 | double the Poison, exhaust |
| 205 | Noxious Cloud | Power | 3 | you 1 Plague |
| 206 | Paralytic Venom | Skill | 1 | 1 Drained, draw 1, exhaust |
| 207 | Carapace | Skill | 1 | block 8 |

**Pack**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 220 | Pack Tactics | Attack | 1 | deal 4 x2 |
| 221 | Howl | Power | 1 | you 2 Strength |
| 222 | Savage Bite | Attack | 2 | deal 12 |
| 223 | Hamstring | Attack | 1 | deal 5, 2 Weakened |
| 224 | Tear | Attack | 1 | deal 2 x3 |
| 225 | Lunge | Attack | 0 | deal 4, draw 1 |
| 226 | Bloodlust | Power | 3 | you 1 Bloodlust |
| 227 | Rally | Skill | 1 | block 6, draw 1 |

**Elemental**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 240 | Flame Lash | Attack | 1 | deal 9 |
| 241 | Mana Surge | Skill | 0 | +2 energy, exhaust |
| 242 | Frost Shock | Attack | 1 | deal 6, 2 Weakened |
| 243 | Chain Lightning | Attack | 2 | deal 6 x3 |
| 244 | Stoneskin | Skill | 2 | block 18 |
| 245 | Lava Burst | Attack | 3 | deal 28 |
| 246 | Attunement | Power | 2 | you 1 Attunement |
| 247 | Earth Shock | Attack | 1 | deal 5, block 5 |

**Fel**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 260 | Shadow Bolt | Attack | 1 | deal 7, 1 Exposed |
| 261 | Curse of Weakness | Skill | 1 | 2 Weakened, 1 Exposed |
| 262 | Drain Life | Attack | 1 | deal 7, heal 3 |
| 263 | Corruption | Skill | 1 | 4 Poison |
| 264 | Demon Skin | Power | 1 | you 3 Fortify |
| 265 | Doom | Skill | 2 | 3 Exposed, 3 Weakened, exhaust |
| 266 | Felfire | Attack | 2 | deal 12, 1 Exposed |
| 267 | Life Tap | Skill | 0 | lose 3 health, +1 energy, draw 1 |

**Humanoid**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 280 | Quick Shot | Attack | 0 | deal 4 |
| 281 | Tactics | Skill | 1 | draw 3 |
| 282 | Shield Bash | Attack | 1 | deal 6, block 5 |
| 283 | Ambush | Attack | 2 | deal 10, 2 Exposed |
| 284 | War Stomp | Skill | 1 | 2 Weakened, 1 Drained, exhaust |
| 285 | Battle Stance | Power | 1 | you 1 Strength, you 1 Agility |
| 286 | Second Wind | Skill | 1 | heal 8, exhaust |
| 287 | Throw Spear | Attack | 1 | deal 9 |

**Shell**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 300 | Shell Up | Skill | 1 | block 7 |
| 301 | Shell Slam | Attack | 1 | + your Block, deal 4 |
| 302 | Death Roll | Attack | 2 | deal 7 x2, 2 Weakened |
| 303 | Crushing Claw | Attack | 1 | deal 7, 1 Exposed |
| 304 | Hardened Shell | Power | 1 | you 1 Plating |
| 305 | Healing Tides | Skill | 1 | you 5 Regen, exhaust |
| 306 | Snap | Attack | 0 | deal 4, block 4 |
| 307 | Undertow | Attack | 2 | deal 8, block 8 |

**Mechanical**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 320 | Overclock | Skill | 0 | +1 energy, lose 1 health |
| 321 | Reinforce | Skill | 1 | block 8, you 1 Agility |
| 322 | Rocket Barrage | Attack | 2 | deal 5 x3 |
| 323 | Saw Blade | Attack | 1 | deal 6, 3 Bleed |
| 324 | Shield Generator | Power | 2 | you 5 Fortify |
| 325 | Steam Engine | Power | 1 | you 1 Insight |
| 326 | Death Ray | Attack | 3 | deal 28, lose 3 health |
| 327 | Bolt Gun | Attack | 1 | deal 9 |

**Critter**

| id | Card | Type | Cost | Does |
|---|---|---|---|---|
| 340 | Scurry | Skill | 0 | block 3, draw 1 |
| 341 | Nibble | Attack | 0 | deal 3 |
| 342 | Squeak | Skill | 0 | 1 Weakened, 1 Exposed |
| 343 | Burrow | Skill | 1 | you 1 Evasion, block 5 |
| 344 | Frenzied Nibbles | Attack | 1 | deal 3 x3 |
| 345 | Lucky Charm | Power | 1 | you 1 Insight |
| 346 | Underdog | Attack | 1 | deal 6, +7 when behind on health |
| 347 | Hide | Skill | 1 | block 8, draw 1 |

**Ability cards** (id 0x4000 + moveset x 8 + stage x 2 + option) are made from the catalog
(`CardFromAbility`): Damage 8 or more (a damage-over-time ability: half the hit and 3 Bleed), Burst
2 energy for 13-18, AoE two hits of 4-6, Heal 6-12 and exhaust, Root 5 and 2 Weakened, Interrupt
free 4 and 1 Weakened, Stun 1 Drained, 4 Block and exhaust, Fear 2 Weakened and 1 Exposed, Debuff 4
and 2 Exposed, Buff 2 Strength and exhaust, Defensive 10 Block, Taunt 6 Block and 1 Thorns, Dispel
5 and a cleanse, Gap closer 6 and draw 1. Each keeps its ability's name and spell icon.

## 4. Rewards, stakes and the old rules

- **Experience** (`XpEarned`): only for a duel that pays (`Companions::DuelPaysXp` on the duel's
  end, through `DuelEndOf`: won by beating the other side or ahead at the turn limit; never a
  forfeit or a duel called off). Each of the winner's companions that fought and still stands
  gains, per enemy beaten, `KillXp(level, enemy level) x 3` against a tamer, `x 2` against a
  player or a wanderer, `x 1` at a practice. The owner's level caps it as always.
- **PvP** pays at most `kDuelWinsPerDay` (5) wins a day per winner's account (`PayDuelWin`); both
  sides are levelled down to the lower side's top level (`PvpLevelCap`).
- **Tamer daily** (first win of the day against each tamer, `character_fusion_tamer`): half a
  level of experience for each companion that fought, money, a Reviving Draught, a catalyst at the
  grand masters, and a **card drop** for the lead companion.
- **Never faint, end at 1 health**: as in the real-time companion duel, a beaten companion stays at
  1 health and the duel never touches its health in the world. Fainted companions (from the open
  world) can't be brought.
- **Challenge holds**: a challenge waiting for its answer holds nobody (no root, no companion put
  away); only the duel itself does. The same player can't re-ask within ten seconds; a challenge
  lapses after a minute.
- **The hunter's bond does not apply**: duel health and damage come from `DuelHealth` and the
  cards, which have no class in them (and the real-time duel's check in `BuildCompanion` stays).

## 5. Balance

`duel_no_family_wins_more_than_about_sixty_percent_against_the_field` plays the AI against itself:
every archetype against every other, every species of each in turn, both seats, 12 seeds a
pairing, at three points (built decks at level 40 and the last stage, starter decks at level 12
and the first stage, built decks at level 60). It fails if any archetype wins more than 62% or
less than 36% of its duels against the field. The last run (share of duels won, draws half):

| Field | Feline | Ursine | Avian | Draconic | Undead | Venom | Pack | Elemental | Fel | Humanoid | Shell | Mechanical | Critter |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| grown decks, level 40 | 43% | 53% | 45% | 58% | 53% | 41% | 56% | 49% | 47% | 50% | 51% | 52% | 53% |
| starter decks, level 12 | 54% | 53% | 39% | 44% | 49% | 53% | 50% | 56% | 53% | 42% | 57% | 44% | 57% |
| grown decks, level 60 | 56% | 50% | 54% | 52% | 53% | 41% | 51% | 50% | 48% | 48% | 41% | 48% | 58% |

What moved the numbers while tuning: poison was far too strong in long duels (Toxic Spit 6 to 3,
Noxious Cloud 2 to 1 at 3 energy, Catalyst 2 energy); permanent Thorns from a starter snowballed
(Bristle is the second starter now, Spiked Hide 3 to 2); Draconic and Avian starters were weak
(Flame Breath first, Draconic Might also gives 1 Strength now; Wing Buffet always draws). An AI duel lasts
about 10 turns in all at level 12 and 13 at level 40 (five to seven each); a few blocking
match-ups run to the 50-turn limit.

## 6. The tamers

Levels follow the zone, not Blizzard's pet level (our companions level with their owner).
Each stands a few yards from the anchor NPC (by spawn guid, VERIFY in game).

| # | Tamer | Zone (town) | Side | Level | Team |
|---|---|---|---|---|---|
| 1 | Julia Stevens | Elwynn Forest (Goldshire) | A | 5 | Tabby Cat, Young Web Spider |
| 2 | Zunta | Durotar (Razor Hill) | H | 5 | Venomtail Scorpling, Coyote Pup |
| 3 | Old MacDonald | Westfall (Sentinel Hill) | A | 13 | Ancona Chicken, Goretusk Piglet, Mechanical Chicken |
| 4 | Dagra the Fierce | The Barrens (the Crossroads) | H | 14 | Plainstrider Chick, Savannah Cub, Bristleback Piglet |
| 5 | Lindsay | Redridge Mountains (Lakeshire) | A | 19 | Kobold Vermin, Black Bear Cub, Strigid Owlet |
| 6 | Analynn | Ashenvale (Astranaar) | A | 22 | Timberling Sprout, Gnarlpine Cub, Sprite Darter Hatchling |
| 7 | Shademaster Kiryn | Stonetalon Mountains (Sun Rock Retreat) | H | 22 | Windfury Fledgling, Cracked Golemling, Mechanical Squirrel |
| 8 | Eric Davidson | Duskwood (Darkshire) | A | 25 | Darkhound Whelp, Duskbat Pup, Rattlecage Bones |
| 9 | Kela Grimtotem | Thousand Needles (Freewind Post) | any | 30 | Roc Chick, Aged Kodo Calf, Gritjaw Hatchling |
| 10 | Merda Stronghoof | Desolace (Shadowprey Village) | any | 34 | Stonelash Scorpling, Kolkar Drudge, Kodo Calf |
| 11 | Steven Lisbane | Stranglethorn Vale (Nesingwary's Expedition) | any | 36 | Panther Cub, Stranglethorn Kitten, Infant Gorilla |
| 12 | Grazzle the Great | Dustwallow Marsh (Brackenwall Village) | any | 38 | Bloodfen Hatchling, Mystlash Hydraling, Drywallow Hatchling |
| 13 | Everessa | Swamp of Sorrows (Stonard) | any | 38 | Creeping Sludgeling, Young Crocolisk, Tree Frog |
| 14 | Bill Buckler | Stranglethorn Vale (Booty Bay) | any | 42 | Baby Shark, Senegal Parrot, Makrura Clacker |
| 15 | Traitor Gluk | Feralas (Camp Mojache) | any | 44 | Groddoc Infant, Troll Whelp, Strigid Owlet |
| 16 | David Kosse | The Hinterlands (Aerie Peak) | any | 46 | Raptor Hatchling, Owlet, Cloud Serpent Hatchling |
| 17 | Kortas Darkhammer | Searing Gorge (Thorium Point) | any | 48 | Pebble, Ember Spark, Rockjaw Trogg |
| 18 | Zoltan | Felwood (Talonbranch Glade) | any | 52 | Imp Familiar, Toxic Mote, Felstalker Pup |
| 19 | Durin Darkhammer | Burning Steppes (Morgan's Vigil) | any | 54 | Black Whelpling, Fel Cinder, Searing Hatchling |
| 20 | Elena Flutterfly | Moonglade (Nighthaven) | any | 55 | Sprite Darter Hatchling, Young Moonkin, Water Mote |
| 21 | Deiza Plaguehorn | Eastern Plaguelands (Light's Hope Chapel) | any | 57 | Ghoul Thrall, Wailing Shade, Crypt Scuttler |
| 22 | Grand Master Lydia Accoste | Deadwind Pass (by Karazhan) | any | 60 | Rattlecage Bones, Wailing Shade, Skeletal Foal |
| 23 | Grand Master Trixxy | Winterspring (Everlook) | any | 60 | Frostsaber Cub, Zephyr, Ironfur Cub |

Early tamers bring two pets (as Blizzard's first tamers do). Stages: 1 for levels under 20, 2 under
40, 3 above. Companion Battle Trainers stand by the innkeepers of Stormwind, Ironforge, Darnassus,
Orgrimmar, Thunder Bluff and the Undercity.

The intended path, as Blizzard's: a trainer in your capital, then your faction's first tamer
(Julia or Zunta), then a tamer every few levels as you level through their zones, ending at the
grand masters at 60. The trainer's "Where should I battle next?" names the next tamers above your
team's level.

## 7. Wire (channel 13, version 2, `CompanionBattles::Encode*`)

Version 2 on both sides; a version-1 message (the old battle) is refused by both.

Server to client:
- **op 1 state**: battle u32, phase, turn u16, end, winner (0 you, 1 them, -1), your turn, seconds,
  energy, max energy, your hand (each card: id, type, cost, playable, damage and block now, spell,
  icon, name, text), then each side (yours first): owner, active, hand/draw/discard/exhaust
  counts, block, statuses (kind, amount), intent (known, damage, hits, block, flags 1 buff 2
  debuff 4 power), the team (species, family, battle family, archetype, stage, level, health, max,
  beaten, name); then the new log lines and the rewards text.
- **op 2** a challenge (who), **op 3** a line for the window, **op 4 deck**: item, name, the deck
  limits, the library (each card, its copies allowed, its source, how many in the deck), a note.

Client to server: **op 10** challenge (guid), **op 11** answer, **op 12 action** (kind: 1 play, 3
end turn, 4 forfeit; the hand slot; the card id seen there), **op 13** the state again, **op 14**
ask for a deck (item, 0 the companion out), **op 15** save a deck (item, count, card ids). Every
client op is on the shared request throttle (`Throttle::Costly`).

## 8. The window

The **Companion Duel** window (classic dialog frame): the opponent's companion on top (family
portrait, health bar, Block, status icons with counts and tooltips, its team's pips, its intent
icon and text), the log, your companion with the **energy orb** and the draw, discard and exhaust
piles, then your hand as **parchment cards** (the mail's stationery) with the cost gem, the key,
the spell icon, the name, the type, the text and what it deals or blocks now; tooltips on all of
it. **1-0** play the cards, **E** ends the turn; End Turn and Forfeit buttons. Cards slide up in
turn when a hand is dealt, lift under the pointer and fly off when played. The **Companion Deck**
window lists what the companion knows, two to a row, with -, the count and +, and Save.

## 9. Later

- Wild duels (critters and young beasts in the field) with a better drop roll on a win.
- Card upgrades (a "+" version at the last stage), rarer drop cards per tamer, a deck button in the
  Companions window, a PvP queue by level.
- The two companions' 3D models in the window; attack and block animations on the portraits.
