# Wanderers: a third kind of character

Players are the heroes; NPCs are the scenery that stands still, sells and gives quests. **Wanderers**
sit between them: named people of the playable races who *live* in the open world. They walk the
roads between towns, fight the local monsters with the same guns and rules players use, loot, rest at
inns, take bounties from the boards, greet you when you pass, shout for help when they are losing,
and remember you the next time you meet. They make the world feel inhabited between real players,
and they give a solo player someone to meet on the road.

Phase 1 is built (section 10), and Phase 2, their lives: trades, days, natures, talk and trade
(section 13); the rest is the plan.

---

## 1. Names

"Wanderers" is the working name and the one the code uses. Other names worth a thought, all things a
townsperson might call them:

| Name | The feel |
|---|---|
| **Wanderers** | Plain and honest: people who walk the roads. Our pick for now. |
| **Wayfarers** | Warmer, a little old-fashioned; fits the inns and roads. |
| **Drifters** | Rougher: sellswords who go where the coin is. Good for the gun setting. |
| **Roadfolk** | Homely; makes them sound like a people of their own. |
| **Freeblades** | Mercenary flavour; good for the later "hire one" phase. |
| **Riftwalkers** | Ties into the Rift story (IDEAS.md): those who came through, or chase it. |

Their title under the name says what kind of fighter each one is: *Wandering Rifleman*, *Wandering
Scout*, *Wandering Sharpshooter*, *Wandering Brawler*, *Wandering Gunslinger*.

---

## 2. Players, wanderers and NPCs

| | Player | **Wanderer** | NPC |
|---|---|---|---|
| Who | A real person | A named character the server plays | A role (guard, vendor, mob) |
| Looks | Their race, class and gear | A dressed person of a playable race, with a gun | Whatever the template says |
| Moves | Anywhere | Roads between towns, hunting grounds | Spawn point, patrol path |
| Fights | MW2 guns, class kit | The same guns on the same rules (aim cone, falloff, cover) | Gunner camps or WoW melee |
| Persists | Everything | Name, level, purse, kills, what it was doing, who it knows | Nothing |
| Talks to you | Chat | Greets you, calls for help, remarks; knows you by name | Gossip menus |
| Cost | A full session | A creature only while someone is near; a few numbers otherwise | A creature always |

The rule of thumb: **a wanderer should feel like another player from twenty yards, and cost like a
handful of NPCs.**

---

## 3. What they do

**Built in Phase 1**

- **Travel the roads.** Each region (the zones of one side on one continent, e.g. Elwynn + Westfall
  for the Alliance, Durotar + the Barrens for the Horde) has a road network made at startup from its
  towns, farms, inns, flight masters and bounty boards. A wanderer picks a destination (nearer places
  and its home zone likelier, not where it just was), walks there along the network at a walk, rests,
  and goes on.
- **Fight what it meets.** Monsters that attack it, and hostile monsters it chooses to shoot, with an
  MW2 gun on our rules: the NPC gunners' aim cone (wide at first, settling as it holds aim, wider with
  range and a moving target), the gun's real damage with falloff, scaled as a player of its level
  would deal it (never by gear), cover pieces taking rounds. It closes to its gun's standing-off
  range (a shotgunner close, a sharpshooter far) and fires in bursts.
- **Never steals.** It never starts on a monster a player has tagged, an elite, one more than two
  levels above it, or a player. Whoever attacks it, it fights back.
- **Loots.** After a kill it walks to the corpse, kneels, and takes the coin for its purse (the
  corpse is not lootable by players anyway: nobody tagged it).
- **Rests.** Sits after a hard fight until healed, and at every inn or town it reaches, longer at an
  inn, with a remark if someone is near ("Innkeeper! Something strong.").
- **Greets players.** A friendly player within 12 yards gets a wave and a line: a stranger
  "Safe roads to you, Jeton", someone it has met "Back on the road, Jeton?", a friend "Jeton, my
  friend! Share the road a while?". Once every ten minutes a player, and how well it knows you grows
  each time.
- **Calls for help.** Below 40% health it yells for help; other wanderers of its side within 30
  yards turn on its attacker, and nearby friendly NPCs are called the WoW way.
- **Takes bounties.** Now and then on arriving somewhere it walks to a bounty board of its region,
  takes one of today's kill bounties of its level (never an elite's), walks to where those creatures
  live, hunts up to six of them, and walks back to turn it in for coin and experience ("Bounty: Defias
  Trapper: done. Pay up."). It gives up a bounty after twenty minutes.
- **Grows.** It earns experience from kills and bounties and levels up, to two past its home zone's
  top; its gun grows with it (a pistol at 5, a sniper at 40 for a sharpshooter).
- **Dies and comes back.** Killed, its corpse lies thirty seconds, and three minutes later it is back
  on its feet at the nearest town. Its killer is remembered (badly).

**Later phases** (section 11): gathering with the Utility tools, small camps with construction
pieces, trading at vendors, duel challenges, asking a player for help with a bounty, joining a
player's group as a temporary ally, companions at their side, spells, PvP between wanderers of
opposite sides, and hiring one.

---

## 4. Sides and PvP

- A wanderer is of its race's faction, as a player of that race is: Alliance wanderers are friends to
  Alliance players and guards, enemies to the Horde, and every monster that hates players hates them.
- Enemy players can attack them as they can a guard, and they fight back with their guns (a player
  is hit as by an NPC gunner of its level). They never start a fight with a player.
- Killing one gives no experience or loot but its gun, which falls like any NPC gunner's (weapon
  pickups); it remembers who killed it.
- Wanderers of opposite sides do not fight each other yet (Phase 3: skirmishes at contested places,
  which world PvP can grow from).

---

## 5. What they remember

Kept in the characters database, saved every minute and survive restarts:

- **`aw_wanderer`**: who it is (id, name, race, gender, archetype, side, home zone, look, seed),
  how far it has come (level, experience, purse, kills, deaths, bounties) and what it is doing (one
  line of text: its trip along the roads, its goal, its bounty and how far through it is, the places
  it visited last, its respawn wait if it was dead).
- **`aw_wanderer_bond`**: how well it knows each player (points, greetings, when they last met). A
  greeting +1, being killed by them -5; tiers at 3 (met before) and 10 (a friend) change what it
  says. A deleted character's bonds go with it at the next startup.

The roster is fixed per zone (`Wanderers.PerZone`): the same people every day, so players get to know
them. Lowering the number lets the extras rest in the database; raising it adds new people.

---

## 6. How many, and what they cost

- **Out of sight they are paper.** A wanderer is a creature only while a player is within 200 yards
  (it leaves at 300, never mid-fight). Otherwise its trip advances a step a second on the world
  thread, 25 of them a tick: a few multiplications each. Unseen, it still rests, takes bounties,
  hunts (a kill every 45 seconds at the hunting ground) and turns them in, so the world has moved on
  when you arrive.
- **In the world, a small budget.** At most `Wanderers.MaxSpawned` (24) at once, and
  `Wanderers.PerPlayer` (8) for each player online, nearest to a player first. Each thinks every 0.4 s
  (staggered by id), looks about it (one grid search within 45 yards) every 1.2 s, moves with short
  pathfinding steps, and tells its pose to players within 100 yards once a second.
- **One roster, at most 99** (their creature entries 93700-93798 are reserved).
- Compared with a population of 1,200 player-session bots, Phase 1 runs about a dozen people per
  region, and none of them cost anything when nobody is there.

---

## 7. How they look

- **A person of a playable race.** Its model is a *dressed NPC model* of its race and gender, taken
  at startup from the game's own data (the display models of ordinary humanoid NPCs friendly to its
  side, with their gear baked in: robes, leathers, tabards), rulers, guards and ghosts left out. So
  a human wanderer looks like a Stormwind traveller, an orc like an Orgrimmar one, each its own.
- **An MW2 gun in its hands**, the same model the NPC gunners carry (world op 33), and each round its
  tracer, muzzle flash and report (op 32).
- **A player's body.** The server tells nearby clients a pose for it on the channel other players'
  bodies use (action systems op 7): the gun carried low on the road, up and aimed (pitch toward its
  target, sights down when standing still) in a fight, put away when sitting. The client's
  third-person layer poses it exactly as it poses another player.
- **Its own name** (each wanderer has its own creature template) and the title *<Wandering
  Rifleman>* under it, which already reads differently from an NPC's role title. A name colour of
  their own (player blue rather than NPC green) needs a small client change: Phase 2.

The client needed no change for any of this.

---

## 8. What they say

All lines are ours (fusion/Wanderers.h), short, in /say (a yell for help), in the universal language
so both sides hear them, and only when a player is near to hear. `Wanderers.Chat = 0` silences them.
Kinds: greetings by bond tier, calls for help, bounty taken, bounty done, resting at an inn or town.
Later: a duel challenge, a request for help with a bounty, thanks for help, a grudge.

---

## 9. Real player sessions, or creatures?

vmangos has **PlayerBots** (`server/src/game/PlayerBots/`): real `Player` objects with a fake session,
used by its party and battleground bots.

| | Player sessions (PlayerBots) | **Creatures (our pick)** |
|---|---|---|
| Looks like a player | Exactly: race, face, every gear slot | Very close: dressed race models; no per-item gear yet |
| Body poses and guns | Yes, like any player | Yes: the server sends their pose as for a player |
| Grouping, trading, duels | WoW's own | Our own systems (Phase 2-4) |
| Cost per bot | A full Player update, a character, inventory, quest log, saves; always online | A creature only near players; paper otherwise |
| Persistence | Full character saves (heavy) | Two small tables |
| Combat | WoW's auto-attack and spells unless rewritten | Our gun rules from day one |
| Risk | Shows on /who, fills character lists, needs accounts, quest and loot side effects | Small, contained; the client already draws everything |
| Scale | Hundreds is expensive; at 1,200 it needs squads and hacks (quarter health, halved respawns) | Thousands on paper, a few dozen in the world |

**Recommendation: creatures now, Player sessions later only where a real Player is required.** A
wanderer's life (roads, fights, bounties, chat) needs nothing a creature cannot do, costs a fraction,
and fits our server-authoritative gun rules directly. The things that truly need a Player (joining a
party with a party frame, battleground fill, a hired mercenary that loots and levels as a player
does) can later *promote* a wanderer: the same identity, name and look, spawned as a vmangos
PlayerBot for the duration, then back to a wanderer. The rules in fusion/Wanderers.h do not care
which body they drive.

**What we keep**: a living shooter population that quests and grinds, persistent identities keyed
by slot, combat taking priority over errands, never taking a human's tagged target, staggered
checkpoints and bounded logins. **What we avoid**: plain WoW auto-attack (ours shoot on our gun
rules), client-driven pieces (ours are entirely server-side and the client only draws; no MW2 game
runs beside it to animate and fire), and a population sized for spectacle that needs health and
respawn hacks.

---

## 10. Phase 1: built

- **Rules** (fusion/Wanderers.h, 27 tests in WandererTests.cpp): identity from a seed (race by side,
  archetype, our own names per race and gender, the gun by archetype and level), levels and
  experience, the road network (authored roads, a spanning tree, near neighbours), routes,
  destination choice, legs split into steps, unseen travel, the state machine, target choice,
  population cap with spawn hysteresis, the round-robin budget, lines by bond, and the saved state's
  text.
- **The game** (game/Fusion/FusionWanderers.cpp): regions from `aw_wanderer_zone`, places from
  `aw_wanderer_node` (anchored on real NPCs, checked to stand in their zone), the bounty boards and
  the region's own innkeepers and flight masters; the roster made or loaded; templates made per
  wanderer; summoning near players and sending out; the walk, the fight (FusionMgr::WandererRound),
  looting, resting, greeting, calls for help, bounty errands; saving.
- **Zones**: Elwynn Forest and Westfall (Alliance), Durotar and the Barrens (Horde).
- **Config** (mangosd.conf): `Wanderers.Enable` (0), `PerZone` (3), `Zones` ("12 40 14 17"),
  `MaxSpawned` (24), `PerPlayer` (8), `SpawnRadius`/`DespawnRadius` (200/300), `UnseenPerTick` (25),
  `Chat` (1), `Bounties` (1), `SaveSeconds` (60).
- **GM commands**: `.wanderer list`, `.wanderer info [id]` (or the one selected), `.wanderer spawn
  [id]` (brings one to you), `.wanderer despawn <id|all>` (out of the world for five minutes).
- **Data**: sql/custom/azeroth_warfare/28_wanderers.sql (world DB, idempotent); the characters
  tables are made at startup.

To see in game: wanderers walking the roads (paths around rivers and hills depend on the server's
move maps), the dressed looks, the gun in hand and its tracers, the body poses (low carry, aim,
sitting), greetings, a bounty hunt from Sentinel Hill or the Crossroads, and the places' anchors.

---

## 11. Roadmap

**Phase 2: they feel like players** (built: section 13, their lives)
- Built: trades with real work and markets, schedules, natures, friends and rivals, news, talk,
  stalls, hires, nine more zones, moving away.
- Left: a nameplate colour and a small icon of their own (a client change); road waypoints along
  the real roads (kind 3 nodes); repairing and buying better guns; spells for caster archetypes.

**Phase 3: they interact**
- Duel challenges to players they know (a /say challenge, the duel window, a bond either way).
- "Can you help me?": a wanderer stuck on a bounty asks a nearby player; helping shares its reward
  and its bond.
- A temporary ally: it joins your fight, then follows you for a while (a creature follower first, a
  party member when promoted to a PlayerBot).
- Companions at their side (a stage 1 or 2 companion of its archetype).
- Opposite sides skirmish at contested places; world PvP grows from it.

**Phase 4: they build and settle**
- Small camps on their way at night: a bedroll, a fire, a construction piece or two (and gone in the
  morning); later, wanderers moving into player towns (IDEAS.md, living player towns).

**Phase 5: hire and fill**
- Hireable mercenaries (IDEAS.md bots): a wanderer you know promoted to a PlayerBot in your party
  for an hour, paid from your purse.
- Battleground and frontline fill when few are online, always shown as bots.

---

## 12. Open questions

1. The final name (section 1).
2. How many per zone feels alive but not crowded (3 to start).
3. Should enemy players be able to kill them freely, or only with PvP on?
4. Should killing a friendly-side wanderer be possible at all (it cannot today: they are friendly)?
5. Should a wanderer's level follow the players near it (so it is useful to anyone), or stay its
   own?

---

## 13. Phase 2: their lives (built)

The owner's brief: wanderers "like players that are absolutely committed to RP". Phase 2 gives each
one a trade, a day, a nature, friends and rivals, news to tell, and a pack of real goods. Rules in
`fusion/WandererLives.h` (26 tests in `tests/WandererLifeTests.cpp`); the world side in
`game/Fusion/FusionWandererLives.cpp`, wired into FusionWanderers.cpp.

### Trades

| Trade | Works at | What its work does | Sells at |
|---|---|---|---|
| **Fisherman** | fishing waters | a catch every minute from the water's own `fishing_loot_template` (area, then zone); rare finds: Shiny Bauble, Nightcrawler Bait, Aquadynamic Fish Attractor, a better pole, the Lucky Fishing Hat | its market, every two sessions or when the pack is full |
| **Miner** | ore veins | the veins' own `gameobject_loot_template`; rare gems (Malachite, Tigerseye, Shadowgem, Moss Agate, Lesser Moonstone, Jade) | its market |
| **Herbalist** | herb meadows | the herbs' own loot; rare Liferoot, Fadeleaf, Goldthorn, Khadgar's Whisker | its market |
| **Trapper** | hunting grounds | the game's own `skinning_loot_template`; rare hides; starts fights more readily | its market |
| **Peddler** | markets | buys a few of what the market's vendors sell for ever (food, drink, trade goods) at their price | another market, to players at a mark-up |
| **Courier** | any settlement | takes a parcel for a settlement 600+ yards off, paid on delivery | - |
| **Sellsword** | bounty boards | the Phase 1 bounty hunt, most of its days; sells bandages and ammunition | - |
| **Minstrel** | inns | plays the room in the afternoon and evening for tips (more with players listening) | - |
| **Pilgrim** | landmarks | walks to the sights (the Cathedral, Grommash Hold, the lighthouse) | - |

Every trade's workplaces are found by the server in the world's own data, so no coordinates are
guessed: **mines and herb meadows** are the densest clusters of chests with a mining or herbalism
lock; **hunting grounds** the densest clusters of skinnable beasts of the region's levels; **fishing
waters** stand by the fishing trainers and suppliers (they stand at the water), plus authored
waters (Lake Everstill by the Lakeshire bridge); **markets** are clusters of three or more friendly
vendors (Stormwind's Trade District, the Valley of Strength). `Wanderers.WorkSpotsPerZone` (3) of
each kind a zone. Landmarks are authored (`28_wanderers.sql`, kind 9). Prices are item_template's:
a market's vendors pay `SellPrice`; a player pays its stall price (see below).

### A day in the life

Bren, *Fisherman of Lakeshire*, human, brave 41, chatty 78, greedy 30, curious 70:

- **00:00-06:00** asleep at the Lakeshire inn (stand state sleep; talk to him and he grumbles).
  Bed and board cost a few coppers.
- **07:00** breakfast at the inn (sitting, eating).
- **08:00** walks to Lake Everstill by the bridge and fishes for 8-20 minutes (a "using" stance, his
  rifle put away), a catch a minute from the lake's own loot table: smallfish, mud snappers, now and
  then something rare ("Caught an Aquadynamic Fish Attractor near Lakeshire today!").
- **10:30** a second session; being curious, on the way he takes a **detour** to the Lakeshire Town
  Hall to look around.
- **14:00** two sessions done, the pack heavy: the long road to **Stormwind**'s Trade District.
  On the way he meets Dalia, a herbalist he is friends with; they stop, he tells his news, she
  answers ("Ha! Same old Bren."), and their friendship grows.
- **16:30** at the market: the vendors buy all but a few of each catch at their price (his purse up
  by what they paid); the rest, and every rare find, stay in his pack: **his stall**. A player who
  talks to him and picks *Let me see your wares* sees exactly that pack.
- **19:00** an evening at the inn with company (he is sociable), then bed in Stormwind (home is too
  far to walk this late). Tomorrow he walks home.
- **Every 5-8 days** a day off: a friend, a sight, the inn.

Attacked by a murloc at the lake he fights back but never starts fights (he is not brave enough);
below 32% health he **runs** for the nearest settlement shouting "Jeton, help me!" to the nearest
player; if that player kills the murloc he thanks them by name, remembers it (a *help*, +3 bond)
and talks about it for two days.

### Their natures

Six traits from 0 to 100, from the seed, leaned by the trade (a sellsword braver, a minstrel more
sociable, a pilgrim more curious), saved:

- **brave / cowardly**: the brave never run (75+); a coward runs at 60% health (`FleeBelowPct`), once
  a fight, then fights cornered. Peaceable trades only fight back (`StartsFights`).
- **chatty / reserved**: a chatty one greets with its news, the weather and its day rather than a
  plain hello; a reserved one says little.
- **greedy / generous**: its stall prices (0.45x to 1.25x a vendor's buying price) and how much it
  keeps back from the market for its stall.
- **curious**: detours to landmarks on its way to work or market (up to one trip in three; never
  two in a row), more sightseeing on days off.
- **lawful / roguish**: a lawful one and a roguish one grate when they meet; a roguish brave one
  starts fights.
- **sociable**: stops to chat with other wanderers, keeps the inn later at night (a night owl),
  visits friends.

**Needs** (mood, fatigue, hunger) move every ten minutes: work and the road tire and starve, sleep
and inns restore, a meal ends hunger; hungry and tired is grumpy (and it says so). Sales, chats and
rare finds lift the mood; fights lost, fleeing and dying sink it.

**Friends and rivals**: each new wanderer starts with one to three ties among its region's others
(mostly friends, a fifth rivals) and they know it back. Every meeting (in sight, or two of them at
the same place on paper) moves the tie: alike natures warm, a lawful and a roguish one cool, a
friendship or a rivalry deepens rather than flips. Their best friend's whereabouts make a *visit*
plan; their friend and rival are named in their talk.

**Memories of players** (`aw_wanderer_bond`): greetings (Phase 1), plus *helped* (killed what was on
it), *harmed* (killed it) and *traded* (bought from it, hired it). Friends pay less.

### Talk

All our own lines, filled from templates with its name, the player's, its home, work and market, the
place it is at and heading for, the time of day, the zone's weather (rain, snow, storm), its friend
and rival, its mood and its latest news. The tests run every template with full and with empty facts
and fail on any unfilled `{token}`. News kinds: a rare find, a good market day, a kill, a flight,
a death, a bounty done, a killstreak craft overhead, a player building nearby, a friend met, a
friend moved away, a player who helped.

Lines are capped: `Wanderers.LineGapSeconds` (40) between one wanderer's remarks (greetings and
answers excepted), `Wanderers.LinesPerMinute` (20) for a whole continent.

### Talking to one

Wanderers have the gossip and vendor flags. The menu (stock gossip window):

- *Tell me about yourself.* Its backstory (made at creation: where it is from, how it came to the
  trade) and its rounds.
- *Where are you headed?* Its plan and where.
- *What's the news?* Its latest news, or a remark.
- *Heard any rumours?* A bounty posted at the nearest board, or a rare creature of the region and
  the place nearest where it lives.
- *Let me see your wares.* The stock vendor window, listing its pack (counts as they are). Buying
  takes the goods from its pack and puts the coin in its purse. Price: about a vendor's buying price
  (or 4x the selling price) times its greed, a rare find 1.6x, friends 8-20% off, and never at or
  under what a vendor pays (no buying from it to sell on at a profit).
- *Walk with me a while.* (its price, by level and greed; a sellsword cheaper, friends half) It
  follows you for ten minutes and fights whatever attacks you; cowards and those who dislike you
  refuse. *That's far enough* sends it on its way.

### The world it notices

A killstreak's craft called in (gunship, bomber, rocket...) or a construction piece placed: every
wanderer within 200 yards takes it as news; those in sight say something (the brave point and
cheer, cowards cry out). The game calls `WandererMgr::NoteWorldEvent` (any thread; taken up on
the world thread).

### Generated, kept, and passed on

At first start every wanderer is made from its id's seed: race of its side, gender, a dressed look,
a name, an archetype and gun (Phase 1), and now its trade (only trades whose workplace its region
has), traits, home (an inn of its home zone), workplace (one of the three nearest of its kind),
market (the bigger and nearer likelier), backstory and span. All of it is kept in `aw_wanderer.life`
(one line of text, `EncodeLife`; added to Phase 1 tables at startup) with its pack, what it is
doing, its parcel, news and ties. A Phase 1 roster takes up trades at the first Phase 2 start.

**Moving away**: after about `Wanderers.LifeDays` (60, a third either way) a wanderer moves away at a
startup: its friends hear of it (news), its bonds go, and a new person (new seed, race, name, look,
trade, nature) takes its id. The roster stays the same size; the world slowly changes.

### Cost

Nothing new on the budget model: the same creatures, caps and paper turns as Phase 1. A plan is made
only on arriving somewhere (minutes apart); an activity on paper is a timer; needs tick every ten
minutes for the roster; meetings on paper are checked every 30 seconds (99 x 99 at most); the
startup scan of the world's spawns runs once. Chatter is capped as above.

### Zones

Alliance: Elwynn Forest, Westfall, **Redridge Mountains, Duskwood, Stormwind City** (one region on
the Eastern Kingdoms). Horde: Durotar, the Barrens, **Orgrimmar, Mulgore, Thunder Bluff**
(Kalimdor), and a new region, **Tirisfal Glades, Silverpine Forest and the Undercity**. 39 people
at 3 a zone (`Wanderers.PerZone`).

### GM

`.wanderer info [id]` now shows the trade (and generation, days left), nature (with the flee point
and whether it starts fights), needs and purse, haunts, what it is doing now and for how long, the
day's schedule, its pack with stall prices, its ties, its news, its backstory and what it remembers
of you. `.wanderer goto <id>` takes you to it. The startup log names each region's workplaces and
markets.

### To see in game

- The workplaces the server found (the startup log; `.wanderer goto` a fisherman or a miner): are
  they at the water, in the mine, in the meadows?
- The poses: fishing (using), mining (work), herbs and traps (kneeling), sleeping at an inn,
  sitting to eat, a minstrel's dance; the rifle put away at work.
- The gossip menu and the stall window (counts, prices, buying, the refresh after a purchase).
- A hire: following, fighting your attackers, the end of it.
- Chats between two wanderers meeting on the road, and the lines' frequency in a busy town.
- The anchors of the new rows in `28_wanderers.sql` (the log names any missing).
- Wanderer looks for the new races' zones (Tirisfal: forsaken).

### Next (Phase 2 left over, and Phase 3)

- A nameplate colour and icon of their own (a client change).
- Wanderers buying better guns and repairing with their purses.
- Delivering a player's letter (a courier hire), carrying goods for a player.
- Weather that sends them indoors; seasonal fishing.
- A minstrel playing a real Bard chart (fusion/Bard.h) players can hear.
