# Construction, woodcutting and plots: integration

Building turns into a profession with materials, lumber gets gathered from new nodes, and players
can buy protected plots. The rules are in the fusion library and are tested on their own:

| Part | Where |
|---|---|
| Skill tiers, piece recipes, skill-ups, refunds, lumber nodes, crafting checks, the build menu's packets | `include/fusion/Construction.h`, `src/Construction.cpp`, `tests/ConstructionTests.cpp` |
| Plot districts, buying, upkeep, permits, the protection checks, raids, the plot packet | `include/fusion/Plots.h`, `src/Plots.cpp`, `tests/PlotTests.cpp` |
| Items, lumber nodes and their spawns, loot, NPCs, vendor, part recipes (`fusion_recipe`) | `server/sql/custom/azeroth_warfare/03_construction.sql` (world DB) |
| Three new `Build::Refusal` codes (`SkillTooLow` 9, `NotLearned` 10, `Protected` 11) | `include/fusion/Build.h` |
| Salvaged materials, paying fresh-or-salvaged, the fresh-materials skill rule | `include/fusion/Construction.h`, `src/Construction.cpp`, `tests/SalvageTests.cpp` |
| Door and portal locks, code hashing, wrong-code limits, their packets | `include/fusion/Locks.h`, `src/Locks.cpp`, `include/fusion/Sha256.h`, `tests/LockTests.cpp` |
| Portals, their destinations, the shared hearthstone cooldown, their packets | `include/fusion/Portals.h`, `src/Portals.cpp`, `tests/PortalTests.cpp` |
| Salvaged items (93600-93649) | `server/sql/custom/azeroth_warfare/24_construction_rules.sql` (world DB) |
| The game side of locks and portals | `server/src/game/Fusion/FusionLocks.cpp`; the client's `client/src/build/locks.rs` |
| Decay and repair: the daily wear, the plot exemption, the Repair Hammer's strikes and their cost | `include/fusion/Maintenance.h`, `src/Maintenance.cpp`, `tests/MaintenanceTests.cpp` |
| Utility's tools: each tool's trade and item, what it works, swings by skill, the strike counter | `include/fusion/Gathering.h`, `src/Gathering.cpp`, `tests/GatheringTests.cpp`; the abilities and ops in `ActionSystems.h` |
| The game side of tools, decay and repair | `server/src/game/Fusion/FusionTools.cpp`; the client's `client/src/tools.rs` |
| The Repair Hammer (91011), its vendor row and its smith's recipe (91009) | `server/sql/custom/azeroth_warfare/25_utility_tools.sql` (world DB) |

This document covers what `server/src/game/Fusion/FusionMgr.cpp` and the scripts have to do. It is
now done: the glue is in `server/src/game/Fusion/FusionConstruction.cpp` (FusionMgr.cpp only calls
into it at its decision points), the NPC and lumber node scripts are in
`server/src/scripts/custom/custom_fusion.cpp`, `.fusion construction` is the GM command, and the
client reads ops 16-18 in `client/src/build/`.

## Decisions

- **Construction is a secondary profession, and it lives in our own table.** The 1.12 client
  reads its skills from `SkillLine.dbc`, and we can't add a row to that file. So the skill goes in
  the characters table `character_fusion_skill` under the key `9001`
  (`Construction::kSkillConstruction`, which can't be confused with a real SkillLine id). The fusion
  UI shows it. It doesn't take a primary profession slot because it never touches vanilla's
  profession count.
- **Tiers:** Apprentice (cap 75, level 5, 10s), Journeyman (150, needs skill 50 and level 10, 50s),
  Expert (225, needs skill 125 and level 20, 3g), Artisan (300, needs skill 200 and level 35, 10g).
  They are learned one at a time from the Construction Trainer's window (`Construction::CanTrain`).
  Vanilla's `npc_trainer` teaches spells, and Construction isn't a spell.
- **Woodcutting uses the Construction skill.** It isn't a separate profession, so there is only one
  skill to train. A builder can level by cutting lumber or by building. Cutting needs Construction
  learned, a **Woodcutting Axe** (item 91010, sold by the supplier) in the bags, and the node's skill.
- **Skill-ups:** the vanilla formula. Orange always gives a point, gray never does, and between
  yellow and gray the chance is `(gray - skill) / (gray - yellow)` (`SkillUpChance`). Pieces use
  `Spaced(required)`, which means yellow at +25, green at +50 and gray at +75. Only placements
  paid with **fresh** materials teach (`PlacementSkillUp(..., paid, ...)` with `paid` = not free
  and no salvage used), so a GM, a free realm or a builder recycling the same materials can't
  farm skill.
- **Recipes:** wood pieces start at skill 1, stone and smith parts come in Journeyman/Expert, the
  wooden door is 125. WoW's props fill in the gaps so there is an orange or yellow recipe at every
  skill up to 300 (a test checks this). The full list is in `MakeRecipes()`. Building is
  **materially expensive**, because nothing else limits it (there is no cap on pieces): a wood wall
  is 16 Rough Lumber and 4 Copper Nails (five to eight trees), a wood floor 12 and 3, a door 14
  Sturdy Lumber, 2 hinges and 8 nails, a stone wall 20 Solid Stone and 5 Iron Brackets, sandbags 12
  Linen Cloth and 8 Coarse Stone. That is about twice what they first cost; WoW's props and the
  whole structures went up by half (`Pricier`, each count rounded up) so a structure costs about
  what its pieces would; the portal stayed as it was. The sentry gun is no longer built: it is the
  Sentry Gun killstreak (5 kills).
- **Taking a piece down** refunds 50% of each material, scaled by the piece's remaining health and
  rounded down (`Refund`), and the same share of its reinforcement. Building and taking down can
  never make a profit. **Releasing a plot, or packing a lapsed one,** gives back exactly the same,
  piece by piece (`Plots::Pack` calls `Refund` with each piece's health and reinforcement); it used
  to give 100%.
- **Salvage doesn't level Construction.** Everything that comes back out of a built piece (taken
  down, packed off a plot, or broken by someone: `Refund`, `Plots::Pack`, `Salvage`) comes back
  as its **salvaged item** ("Salvaged Rough Lumber", `AsSalvaged`; world DB 93600-93649, made by
  `24_construction_rules.sql` from each fresh item's row, `SalvageTable()` pairs them). Salvaged
  goods build pieces and pay for reinforcement exactly like fresh ones (`CanAffordBuild`,
  `PlanPayment`), but a piece paid with any salvage teaches nothing. When a piece could still
  teach (`CouldTeach`: not gray, under the tier's cap) fresh goods are spent first; otherwise
  salvage goes first, so a builder's salvage pile is used up on the pieces that can't teach
  anyway. Salvaged items are separate entries rather than a hidden flag or a per-character
  credit: they keep their nature when traded, mailed or banked (an alt can't launder them), the
  tooltip says what they are, and no table needs to track them. They are of no use to the other
  trades (the smiths' and engineers' recipes count only fresh goods) and sell to vendors for
  what the fresh goods sell for. Destroy-salvage is salvaged too: a friend breaking your pieces
  for you is no way round the rule.

## The open world (design doc, section 12)

- **Where a piece may go.** The builder must see it: line of sight from the eye to just short of
  its middle, through terrain, buildings and other players' pieces (`Refusal::OutOfSight`, 15;
  their own pieces never block it, so a blueprint's far wall goes up behind its near one). Never
  within 6 yards of a creature with an NPC service (`Build::CrowdsNpc`, `Refusal::CrowdsNpc`, 16).
  Taking down wants the builder alive and out of combat.
- **Collapse** after a piece goes looks 120 yards around it but lets only pieces within 50 yards
  fall (`Build::CollapseNear`); the remover is told how many came down with it.

- **Nothing protects a piece out in the world** (a bought plot still does, `Plots::CanDamage`).
  Guns break a wood wall (600) in under an M4 magazine and a stone wall (1,800) in two; FMJ does
  1.5 times. **Spells** hit pieces too (`FusionMgr::SpellHitsPieces`, called from
  `Spell::_handle_immediate_phase`): a player's (or their pet's) direct damage hits the pieces
  within its area, or within 2 yards of a single target and the piece between caster and target;
  a damaging area on the ground (Blizzard, Rain of Fire, Consecration) all its ticks at once. A
  spell as strong as its caster's level is expected to cast does 450 (`Build::SpellPieceDamage`),
  so a wood wall falls to two and a stone wall to four.
- **Salvage:** whoever destroys a piece gets 40% of each material (and of its reinforcement),
  rounded down, at least one of its main material (`Construction::Salvage`), straight to the bags
  (the stash when full). A group shares it item by item among members within 100 yards
  (`SplitSalvage`). A creature's kill gives nothing.
- **No cap on pieces.** `kMaxPiecesPerPlayer` is gone: what a piece costs and its upkeep (decay
  and repair, below) are the limit. The status op (14) sends max 0, which the client shows as
  "Built N". `Refusal::TooMany` stays on the wire, unused. Portals keep their own limit (two a
  player, one a plot) and a player keeps one bedroll.
- **Decay** (`Maintenance.h`, below): every open-world piece wears down 2% of its full health a
  day and falls at 0; a paid plot's pieces don't decay.
- **Door locks** (`Locks.h`, `FusionLocks.cpp`). Every door (all styles) and portal has a lock its
  owner picks: **Unlocked** (anyone), **Owner only** (the builder and their other characters),
  **Owner + party** (the owner's party or raid), **Owner + guild**, or **Code** (4 to 8 digits).
  In build mode, F on a door or portal of yours opens a small classic dialog (radio choices and a
  hidden-text code box); the builder, the owner of the plot it stands on, or a GM may set it.
  - **The default is Owner + party**, for new doors and for every door from before locks (the
    column's default migrates them): friends you group with get in without fuss, strangers don't,
    and an open-world door is no longer a hole anyone walks through. Unlocked is one click away.
  - **On a plot,** its owner, everyone they permit (guest or builder) and GMs open every door on it
    whatever its lock: the plot is their household (`Plots::CanUseDoor` Allowed). Anyone else
    meets the door's lock, so a plot owner who wants a shop door open to visitors sets it
    Unlocked. (Before, visitors could never open a plot door.)
  - **Codes:** someone at a code lock gets a keypad (the server's op 19); a right code opens the
    door (or the portal's list) and is remembered for the session until the code changes. Codes
    are kept as a salted (16 random bytes) SHA-256, run 2048 more times with the salt
    (`Locks::HashCode`, `Sha256.h` written out in the rules library so it builds anywhere), in
    `fusion_build_piece.lock_salt`/`lock_hash`; the code itself is never stored, logged or sent
    to any client (the wire carries only the mode, in flag bits 4-6).
  - **Wrong codes:** three free misses, then a wait of 30 s doubling to 15 minutes, counted per
    player and per lock (`Locks::Limiter`), so neither one player nor a crowd of alts can walk
    through 10,000 codes. A right code clears both counts. Someone hitting the longest wait is
    logged.
  - Refusals: `Locked` 17 (the client names whom the lock lets in), `WrongCode` 18,
    `TooManyTries` 19.
- **Portals** (`Portals.h`, `FusionLocks.cpp`). Prop 128 (`Build::kPortalProp`), drawn with the
  Stormwind mage portal model: an Artisan's piece (Construction 275) costing 10 Ironwood Plank,
  20 Dense Stone, 2 Fortified Plate, 2 Arcane Crystal and 4 Large Brilliant Shard; stone, 3,000
  health, not reinforceable. Two per player (by character) and one per plot (`PortalLimit` 21).
  F on it opens its list for that player: their own faction's three capitals (never the enemy's;
  the server checks again on use) and "My homestead" when they hold a plot. A capital landing is
  where the mages' own teleport lands (`spell_target_position` of Teleport: Stormwind 3561 and so
  on; the city's centre otherwise). Who may use it is its lock's say, exactly as a door's.
- **The shared hearthstone cooldown.** Using a portal, or the homestead courier's "Take me back",
  starts the hearthstone's cooldown (spell 8690 from item 6948, an hour) and waits on it
  (`CoolingDown` 20; the courier's line is greyed with the time left). The server keeps it in the
  player's own spell cooldowns (saved with the character, like the hearthstone's) and sends the
  client `SMSG_SPELL_COOLDOWN` so the hearthstone shows it. GMs aren't held to it.
- **Reinforcement** (G in build mode on a piece of yours; world channel op 50): three tiers, each
  multiplying the piece's health and cutting bullets and blows; blasts and spells are not cut, so
  explosives answer a fortified wall. Walls, floors, doors, stairs, roofs, beams and sandbags only.

  | Tier | Skill | Health | Bullets cut | Costs |
  |---|---|---|---|---|
  | 1 Braced | 150 | x2 | 25% | 4 Iron Bracket, 6 Iron Bar |
  | 2 Plated | 225 | x3.5 | 45% | 3 Reinforced Plate, 8 Mithril Bar |
  | 3 Fortified | 275 | x6 | 65% | 2 Fortified Plate, 6 Thorium Bar, 8 Dense Stone |

  The tier is saved (`fusion_build_piece.reinforcement`) and rides in a piece's wire flags (bits
  2-3); the tiers go to the client once (world channel op 51).
- **GMs build free:** `free = m_buildFree || IsAdmin(player)`. They skip the skill and materials
  checks and get no skill-ups. That matches what `BuildPlace` does today.
- **New spells are out.** Smith and engineer parts are crafted by the fusion code from the world
  table `fusion_recipe`, which the SQL creates. Its rows have a skill line (164 Blacksmithing,
  202 Engineering, or 9001), colour thresholds, reagents and the product.
- **Lumber nodes are not locked.** Vanilla's veins and herbs carry a `Lock.dbc` row of the mining or
  herbalism lock type, and the client ships `Lock.dbc` and `LockType.dbc`, so a "woodcutting" lock
  type can't be expressed. The nodes are type 3 chests with `data0 = 0` and
  `script_name = 'go_fusion_lumber'`, and the script gates them.
- **Plots go on the Hyjal summit, map 1.** Here is how the candidates compared:
  - **Hyjal summit, around (5476, -3729, 1593).** Chosen. 1.12 ships it with full terrain and it
    was never opened, so it's outside play bounds and only reachable by teleport. The world dump
    has no gameobject spawns within 500 yards. It is on the Kalimdor continent, so it's not
    instanceable and `CanBuildOn` already allows it. Grids, height maps and LoS work as everywhere
    else. vmangos has no code that special-cases Hyjal (checked with grep). It even has a
    `game_tele` "HyjalBlizzardConstructionCo".
  - **Hyjal at (4600, -3800).** Rejected. It's the Winterspring/Felwood edge, and the dump has
    mining veins there at z of about 950, so players can reach it.
  - **GM Island, map 1 (16226, 16257).** Rejected. It's small (room for a dozen 40-yard plots at
    most) and already has GM furniture on it.
  - **Azshara Crater, map 37.** Rejected. It's a battleground map, so `Instanceable()` would refuse
    building, and each instance would hold a separate copy of every plot.
- **The plot layout** is three districts north of the gate, with a 10-yard road between them. The
  values are in `Plots::Default()`, and the origins can be moved without changing anything else:

  | Size | Plots | Cell | Ids | Origin (x, y) | Price | Upkeep a week | Deed |
  |---|---|---|---|---|---|---|---|
  | Small | 8 x 4 | 24 yd | 1-32 | 5490, -3850 | 5g | 20s | 91030 |
  | Standard | 6 x 4 | 40 yd | 101-124 | 5490, -3740 | 25g | 1g | 91031 |
  | Large | 4 x 2 | 64 yd | 201-208 | 5490, -3570 | 100g | 3g | 91032 |

- **Ownership:** one plot per **account** (`kMaxPlotsPerAccount = 1`). Every character on the
  account has Owner access. Owners can grant up to 20 character permits at two levels. A Guest can
  use doors (whatever their locks). A Builder can also build, and can take down or damage what they built (only that:
  `CanDamage` refuses a builder the owner's pieces). A piece taken down by someone other than its
  builder refunds its builder, into their stash.
- **Upkeep:** a purchase includes the first week. Upkeep can be prepaid up to 12 weeks ahead, and
  weeks are counted from the end of the paid time, so arrears get paid first. After the paid time
  runs out, the plot is **Overdue** for 2 weeks. During that time it stays protected and the owner
  can remove pieces and use doors, but nothing new can go up. After that it is **Lapsed**:
  `Registry::Lapse` puts it back on sale, and every piece on it is packed (what taking it down
  would give: half, by its health, salvaged) into each piece builder's reclaim stash. Abandoning
  a plot works the same way, with no refund.
- **Protection on plots:** only the owner, permitted characters and GMs can build, remove or
  damage (`CanBuild`, `CanRemove`, `CanDamage`); they also open every door there (`CanUseDoor`),
  and anyone else opens a door only if its lock lets them. Creatures, raiders, sentries
  and blasts with no player behind them never damage a plot piece. In the districts outside a plot
  (the roads), nobody but a GM builds. A piece also has to fit entirely inside one plot, at least
  1 yard in from its edge (`Fits`). Each check returns `OpenWorld` outside the districts, and then
  the existing rules apply unchanged.
- **Raids:** no raid is ever called in a district or within 80 yards of one (`RaidsAllowedAt`).

## Decay and repair (`Maintenance.h`, `FusionTools.cpp`)

**Decay.** Out in the world a piece loses `kDecayPerDay` = 2% of its full health (its
reinforcement's included) a day: a whole piece left alone falls in 50 days, a half-broken one in
25. On a bought plot nothing decays for as long as its upkeep is paid (`paidUntil`); an overdue
plot's pieces decay at the open world's pace (and a lapsed plot is packed anyway). Upkeep paid late
covers the overdue weeks first (Plots.h), and decay already taken in them is not given back.

- **Lazily, from a timestamp.** `fusion_build_piece.decayed_at` is the time (unix seconds) a
  piece's stored health was true at. Whatever looks at a piece works out what time has taken since
  (`Maintenance::Decay`: `DecayLoss` over the seconds after `max(decayedAt, keptUntil)`), over any
  gap: an hour, a month offline, the server's own downtime. Nothing is applied twice: every write
  of `health` (a hit, a repair, a reinforcement) writes `decayed_at` with it, and the hourly sweep
  changes only the memory, so after a restart decay since the last write is simply worked out
  again from the stored pair.
- **The sweep.** Once an hour a map (`m_decayNext`), the first player's update there brings its
  pieces up to now (`DecayPieces`): each piece's health, its `kept` flag (bit 7 of the wire's
  flags), the health sent to whoever sees the changed ones (op 12, 400 at a time; a piece whose
  kept flag changed is sent whole, op 10). A piece with nothing left collapses as a broken piece
  does (`RemovePieces(..., Destroyed)`: what it held up falls with it; nobody salvages it). Maps
  nobody is on wait: their pieces are worked out when someone comes.
- **Safe on live data.** The column is added with `ADD COLUMN IF NOT EXISTS ... DEFAULT 0` at
  startup, and every piece still at 0 (from before decay) gets the startup's time, so none loses
  anything for the time before decay existed. No row is ever deleted by the migration; only a
  piece decay has emptied comes down, through the same path a broken one takes. A clock set back
  takes nothing.

**Repair.** The **Repair Hammer** (item 91011, `25_utility_tools.sql`): sold by the construction
supplier (91002, 5s) and made by smiths (Blacksmithing 50, 4 Copper Bar and 2 Rough Lumber at an
anvil, in the Construction trainer's window). It is a Utility tool (below): readied in the hands,
aimed at a damaged piece within 5.5 yards, fire strikes it; held, it keeps striking every 0.9 s.

- Each strike mends `kRepairTick` = 10% of the piece's full health (less when less is missing).
- It costs `kRepairShare` = 60% of what went into the piece (its recipe and its reinforcement's
  goods), in proportion to the health mended: a full repair from nothing is 60% of the piece, a
  week's decay (14%) about 8.4%. Fresh or salvaged goods pay alike (salvage first: a repair never
  teaches). Whole items are taken; what a strike pays over its share is credited to the next strike
  on the same piece (`Maintenance::Credit`, one piece at a time; another piece starts it over, so
  the rounding never makes a repair cheaper). 60% is over the 50% a take-down gives back, so
  mending and taking down never makes anything, and under the 100% of building anew.
- Not within `kRepairAfterDamage` = 30 s of a hit: no hammering a wall back up under fire.
- Who: anyone who may take the piece down (its builder, a plot's household, a GM) and the
  builder's party. GMs and a free realm mend free.
- **No Construction skill-up from repairs.** The skill comes from building with fresh materials and
  cutting lumber. A repair skill-up would be farmable (break your own wall a little, mend it, again),
  and salvage repairs would launder the fresh-materials rule; repairs are upkeep, not practice.
- The piece's health and decay show under the crosshair while the hammer is held (and in the
  build-mode hint panel): "Wood Wall 87% · decaying: about 43 days left", or "kept by the plot's
  upkeep".

## Utility's tools (`Gathering.h`, `FusionTools.cpp`)

Utility (the weapons put away, `ActionSystems.h`) holds the trades' tools as abilities of its own
bar (and no other): Mining Pick (10), Skinning Knife (11), Herbalism (12, bare hands), Fishing
Pole (13), Woodcutting Axe (14), Blacksmith Hammer (15) and the Repair Hammer (16). Each asks
vanilla's own requirement: the Mining Pick (2901) and Skinning Knife (7005) the gathering spells'
totems, any fishing pole for Fishing, the Blacksmith Hammer (5956) for Blacksmithing, the
Woodcutting Axe (91010) and Construction for lumber, the Repair Hammer (91011); a tool in the bags
or equipped will do, and the trade must be known.

- **Readying** (channel 10 op 2): the client tells the tool its slot key readied; the server puts
  the tool's item in the hands' melee profile (world op 20: its model, its kind of weapon, the
  tool's own swing time), so the client draws it and swings it with the tool's stroke.
- **Striking** (op 3: tool, target guid or piece id): the server checks the tool and trade, the
  target (a gameobject whose lock asks Mining or Herbalism, a lumber node, an anvil or forge, a
  skinnable corpse; within 5.5 yards), the pace (three quarters of the tool's swing), and counts
  strikes on one target (`Gathering::Strike`; another target, another tool or 4 s idle starts
  over). A vein or a tree takes 4 swings while it is orange to the gatherer, 3 to 75 points over
  its requirement, then 2; a herb or a corpse 3, 2, 1 (`StrikesFor`).
- **The last strike is WoW's own gathering**, cast for the player as a triggered spell: the
  open-lock spell the player knows for the lock's type (`Spell::EffectOpenLock`: its skill check,
  the loot window, depletion and the one skill-up a node), the skinning spell (its level check and
  orange failure chance), the lumber node's own use (`UseLumberNode`). Fishing: a strike casts
  the fishing spell (the bobber lands ahead in water or fails as vanilla's), the next uses the
  bobber (vanilla's catch or "got away"). The smith's hammer at an anvil answers Opened and the
  client opens Blacksmithing's window (the recipes keep their own anvil checks).
- **Answers** (op 4: tool, outcome, done, needed): the client shows "Mining Pick 2/4" or
  "Repaired: 80%" under the crosshair and plays the strike's sound; refusals ("Requires Mining
  150", "Requires a Mining Pick") come as the server's notifications.
- Bare hands stay as they were: in Utility with no tool, fire punches (WoW's unarmed swing).

Channel 10's ops (`ActionSystems.h`): 1 the system (client), 2 the tool (client), 3 a strike
(client), 4 its outcome (server). Ops 2-4 were free (only op 1 was used, by the client and the
server; nothing in benilla reads channel 10).

### The decay column

`fusion_build_piece` gains one column at startup (`LoadPieces`), and pieces from before take the
startup's time:

```sql
ALTER TABLE `fusion_build_piece` ADD COLUMN IF NOT EXISTS
  `decayed_at` bigint(20) NOT NULL DEFAULT '0' COMMENT 'unix time health is true at' AFTER `lock_hash`;
UPDATE `fusion_build_piece` SET `decayed_at` = <now> WHERE `decayed_at` = 0;
```

### The lock columns

`fusion_build_piece` gains three columns at startup (`LoadPieces`, `ADD COLUMN IF NOT EXISTS`, so
it is safe on live data and on every restart):

```sql
ALTER TABLE `fusion_build_piece`
  ADD COLUMN IF NOT EXISTS `lock_mode` tinyint(3) unsigned NOT NULL DEFAULT '2' COMMENT 'Locks::Mode' AFTER `reinforcement`,
  ADD COLUMN IF NOT EXISTS `lock_salt` varchar(32) NOT NULL DEFAULT '' AFTER `lock_mode`,
  ADD COLUMN IF NOT EXISTS `lock_hash` varchar(64) NOT NULL DEFAULT '' AFTER `lock_salt`;
```

A code lock whose salt or hash is missing loads as the default (Owner + party).

### The build channel's new ops

| Op | Way | Body |
|---|---|---|
| 4 | client | set a lock: piece u32, mode u8, code length u8, code digits (only for Code; empty keeps the code a code lock has) |
| 5 | client | try a code: piece u32, code length u8, code digits |
| 6 | client | F on a portal: piece u32 |
| 7 | client | through a portal: piece u32, destination u8 (1-6 the capitals, 100 the homestead) |
| 19 | server | a lock wants a code: piece u32, portal u8 |
| 20 | server | a portal's list: piece u32, hearthstone cooldown left u32 s, count u8, each: id u8, name length u8, name |

Ops 4-7 and 19-20 were free on channel 1 (the build ops 1-3 and 10-18, the kits' 30-32 and the
ledger's; nothing on the client or benilla decodes them otherwise).

## Characters DB tables

Create these at startup, next to `fusion_build_piece` in `FusionMgr::LoadPieces()`, using
`CREATE TABLE IF NOT EXISTS` the same way that table is created:

```sql
CREATE TABLE IF NOT EXISTS `character_fusion_skill` (
  `guid` int(10) unsigned NOT NULL COMMENT 'character guid',
  `skill` smallint(5) unsigned NOT NULL COMMENT '9001 Construction',
  `tier` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT 'Construction::Tier',
  `value` smallint(5) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `skill`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `character_fusion_plot` (
  `id` int(10) unsigned NOT NULL COMMENT 'Plots.h plot id',
  `account` int(10) unsigned NOT NULL,
  `owner` int(10) unsigned NOT NULL COMMENT 'the buying character',
  `bought_at` bigint(20) NOT NULL,
  `paid_until` bigint(20) NOT NULL,
  PRIMARY KEY (`id`), UNIQUE KEY `account` (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `character_fusion_plot_permit` (
  `plot` int(10) unsigned NOT NULL,
  `guid` int(10) unsigned NOT NULL,
  `level` tinyint(3) unsigned NOT NULL COMMENT '1 guest, 2 builder',
  PRIMARY KEY (`plot`, `guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `character_fusion_reclaim` (
  `guid` int(10) unsigned NOT NULL,
  `item` int(10) unsigned NOT NULL,
  `count` int(10) unsigned NOT NULL,
  PRIMARY KEY (`guid`, `item`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

The `UNIQUE KEY account` enforces `kMaxPlotsPerAccount = 1` in the database. Remove it if the limit
goes up. `fusion_build_piece` doesn't change, because a piece's plot comes from its position
(`Plots::PlotOf`).

## FusionMgr: step by step

### Loading
1. **Plots:** in `Load()`, load `character_fusion_plot` and the permits into a
   `Fusion::Plots::Registry m_plots`, guarded by `m_buildLock` or a lock of its own. The grid comes
   from `Fusion::Plots::Default()`.
2. **Part recipes:** load `fusion_recipe` from the world DB into
   `std::vector<Fusion::Construction::CraftRecipe>`, mapping `reagentN/countN` to `reagents` and
   skipping zero reagents.
3. **The player's skill:** on login (next to `LoadClasses`), read `character_fusion_skill` for
   9001 into `FusionPlayerData` (`Construction::Tier constructionTier; uint16 constructionSkill;`).
   Save it whenever it changes, using
   `REPLACE INTO character_fusion_skill (guid, skill, tier, value) VALUES (?, 9001, ?, ?)`.
4. **Bag counts:** build a `Construction::Inventory` for the item ids a recipe needs with
   `player->GetItemCount(item)`. Only build it for the items in the recipe, not the whole bag.

### Placing (`BuildPlace`)
Replace the single `Cost` (`Def(type).cost`) with the recipe:

1. After the reach/overlap/footing checks and before `Solve`, check the plot:
   `Plots::CanBuild(grid, m_plots, Actor::Player(guid, accountId, IsAdmin(player)), p, time(nullptr))`.
   `Denied` refuses with `Refusal::Protected`. `OpenWorld` and `Allowed` continue.
2. Replace the `HasItemCount` block with `Construction::CheckPlace(type, tier, skill, bags, free)`.
   Map the results like this: `NotLearned` to `Refusal::NotLearned`, `SkillTooLow` to
   `Refusal::SkillTooLow`, `NoMaterials` to `Refusal::NoMaterials`, and `Unknown` to
   `Refusal::Unknown`.
3. If the piece isn't free, call `DestroyItemCount(m.item, m.count, true)` for each entry in
   `RecipeFor(type)->materials`. Do this only after every check has passed, at the point where the
   old code took the cost.
4. Once the piece is stored, apply the skill-up:
   `uint16 next = Construction::PlacementSkillUp(type, skill, tier, !free, rand_norm_f())`.
   If it went up, save it, send `EncodeSkill(tier, next)` on `kChannelBuild`, and show the usual
   "Your skill in Construction has increased to N." system message.

### Taking down (`BuildRemove`)
- Replace the owner check with `Plots::CanRemove`. `Allowed` passes. `Denied` sends
  `Refusal::NotYours`. `OpenWorld` keeps today's rule (`found.owner == me || IsAdmin`).
- Replace the refund with `Construction::Refund(found.type, found.health / Def(found.type).health)`.
  Add each entry to the remover's bags with `StoreNewItemInBestSlots`, unless the remover is
  building free.

### Doors (`HandleChannel`, `ClientOp::Toggle`)
- `FusionMgr::SwingDoor` (FusionLocks.cpp): before flipping `q.open`, `LockVerdict` builds a
  `Locks::Visitor` (the owner or an alt of theirs, the plot's household from
  `Plots::CanUseDoor`, the owner's party or guild, a code given) and asks `Locks::MayOpen`.
  `Locked` refuses with `Refusal::Locked`; `NeedsCode` sends op 19 (the keypad).

### Damage (`DamagePiece`)
`DamagePiece(map, id, damage, blast)` doesn't know who did the damage. Add a
`Fusion::Plots::Actor const& by` parameter and check
`Plots::CanDamage(grid, m_plots, by, piece, now)` before reducing health. On `Denied`, return
without changing anything (the bullet still stops at the wall). The callers:

| Line (today) | Caller | Actor |
|---|---|---|
| ~1827 | a creature gunner's cover piece | `Actor::Creature()` |
| ~2623 | `BlastPieces`: add the same parameter and pass it through | from its callers |
| ~2101 | `NpcExplode` -> `BlastPieces` | `Actor::Creature()` |
| ~3069 | `Explode(owner, ...)` -> `BlastPieces` | `Actor::Player(owner guid, owner account, IsAdmin(owner))`, or `Actor::World()` when there is no owner |
| ~2989 | `UpdateCreatureSiege` | `Actor::Creature()`. Also stop sieging: if `CanDamage` would refuse, clear `m_fusionSiegePiece` and keep chasing, so a brawler doesn't stand hitting a wall it can't break. |
| ~3426 | a player's shot | `Actor::Player(shooter...)` |

### Raids (`UpdateRaid`)
- Add `&& Plots::RaidsAllowedAt(grid, map->GetId(), player->GetPositionX(), player->GetPositionY())`
  to `hasBase`, so a raid never starts in the homesteads. A raid that has already started against
  a base elsewhere ends normally.
- Also skip pieces whose `PlotOf != 0` when `UpdateBuild` counts `baseCount`. This only matters if
  the buffer is ever reduced.

### Lumber nodes (`go_fusion_lumber`)
Register a gameobject script (`scripts/custom/`, next to `custom_creatures.cpp`) with
`pGOHello = &GOHello_fusion_lumber`:

```cpp
bool GOHello_fusion_lumber(Player* p, GameObject* go)
{
    using namespace Fusion::Construction;
    auto& d = p->GetFusion();
    bool const gm = sFusionMgr.IsAdmin(p);
    switch (CanGather(go->GetEntry(), d.constructionTier, d.constructionSkill,
                      p->HasItemCount(kWoodcuttingAxe, 1), gm))
    {
        case GatherCheck::Ok: break;
        case GatherCheck::NoAxe: p->GetSession()->SendNotification("Requires a Woodcutting Axe"); return true;
        case GatherCheck::NotLearned: p->GetSession()->SendNotification("Requires Construction"); return true;
        case GatherCheck::SkillTooLow: p->GetSession()->SendNotification("Requires Construction %u", NodeFor(go->GetEntry())->skill.required); return true;
        default: return true;
    }
    // Vanilla gives the point when the vein is opened: do the same, then let the chest open (false).
    if (NodeDef const* n = NodeFor(go->GetEntry()); n && !gm)
        sFusionMgr.SetConstructionSkill(p, GatherSkillUp(*n, d.constructionSkill, d.constructionTier, rand_norm_f()));
    return false;
}
```

As built, the hook opens the loot itself (`Player::SendLoot`) and returns `true`: vmangos opens a
chest's loot only through the opening spell, and that spell refuses a chest with no lock
(`SPELL_FAILED_ALREADY_OPEN`), so an unlocked chest's plain use would show nothing. The skill-up
is given once a node spawn, as vanilla's veins do (`GameObject::AddToSkillupList`). The loot comes from
`gameobject_loot_template` 191001-191004, and the node despawns (`data3 = 1`) and respawns on its
`spawntimesecs`. If you want a gathering cast bar, cast vanilla's generic "Opening" spell first and
grant the skill-up from the spell's effect. That is optional.

### NPCs: the Construction window (`Homestead.h`)
The trainer (91001), the Plot Steward (91003, the plot coordinator) and the courier (91004) show
no gossip menu. Their gossip hello remembers the NPC and sends the client's Construction window
(world channel op 54, `Homestead::EncodeWindow`) with `Open::Npc`; the client opens
`Panel::Construction` with NPC access. The window lists what that NPC does as priced lines; a
press comes back as op 53 (`EncodeAct`), is checked against the NPC still being within 12 yards
(`Homestead::AllowedAt`), done, and answered with the window again, its status line saying what
happened. Nothing goes to chat. From the HUD the client sends op 52 and gets the window without
any NPC's lines: the player's Construction, their plot (where, size, upkeep, pieces on it,
permits) and the build catalogue.

- **Trainer:** train the next tier (`CanTrain`), and make the parts of `fusion_recipe` the
  player's skills reach (`CanCraft`; an anvil within 10 yards when `needs_anvil = 1`), each line in
  its recipe colour.
- **Plot Steward (the plot coordinator):** with no plot, claim one of each district of its site
  (`Homestead::ClaimInDistrict`: the first free plot, the one-plot limit checked first) or the
  plot last walked on (`Homestead::ClaimPlot`); the size's deed comes with it. With one: **take
  me to my homestead** (`Homestead::Arrival`: the middle of the plot's edge nearest the gate, in
  the build margin, on the ground there), pay upkeep (a week, four, as far as allowed), permits
  for the target and revoking each, and release (asked first; packed as a lapse is). Collect
  packed belongings when any wait.
- **Courier:** back where the player came from, the site's other stops, the stop nearest the
  player's own plot, and every other site's gate. "Take me back" shares the hearthstone's
  cooldown (`CourierAsk::backCooldownMs`): greyed with the time left while it runs, and it starts
  it when used, whether it goes back to where the player came from or to their inn.

**One plot to a player** (an account; alts are one player): every way to a plot checks the
limit before anything else, and at startup any account holding more than one (rows from before
the limit) keeps its earliest and the rest are packed (`Homestead::Excess`).

The claimed plots near a player (op 55, `Homestead::PlotsNear`) go to the client while it is
within 220 yards of a district; it marks them in the world: its own plot with banners at the
corners, posts along the edges and a faint line on the ground, others' with corner posts.

### The upkeep sweep
At startup and then once an hour:

1. Call `std::vector<uint32> ids = m_plots.Lapse(time(nullptr))`.
2. For each id:
   1. Collect the pieces on map 1 with `PlotOf(grid, p) == id`.
   2. Call `Plots::Pack(pieces)` and `INSERT ... ON DUPLICATE KEY UPDATE count = count + VALUES(count)`
      into `character_fusion_reclaim`.
   3. Remove the pieces through `RemovePieces(map, id, RemoveCause::TakenDown)` if the map is
      loaded. Otherwise delete them from `fusion_build_piece` and `m_pieces` directly, as the
      bedroll code does for another map.
   4. Delete the plot and permit rows.
3. Optionally send a mail with no items: "Your homestead lapsed; your belongings are with the Plot
   Steward." Vanilla mail carries one item, which is why packed items go into a stash rather than
   attachments.

When a player logs in with less than 2 days of upkeep left, or overdue, tell them with a system
message.

## The client: what the build menu needs

Everything goes on `kChannelBuild` (SMSG_FUSION_CHANNEL). It's little-endian, and the byte layouts
are in the headers:

| Op | Encoder | When to send | Body |
|---|---|---|---|
| 16 Recipes | `Construction::EncodeRecipes(Construction::Recipes())` | once on login (it's the same for everyone, so encode it once and cache it) | count u16, then for each recipe: type u8, required/yellow/green/gray u16, materials u8, then for each material: item u32, count u16 |
| 17 Skill | `Construction::EncodeSkill(tier, skill)` | on login, on training and on each skill-up | tier u8, skill u16, cap u16 |
| 18 Plot | `Plots::EncodePlot(grid, m_plots, id, viewer, now)` | when `PlotAt` under the player changes (check in `UpdateBuild`), and after buying, paying or a permit change | id u32 (0 = none), size u8, standing u8, access u8, x0 y0 x1 y1 f32, paid until u32, permits u8 |
| 13 Refused | existing | | new reasons: 9 skill too low, 10 not learned, 11 protected |

With these, the client's build menu (`client/src/build/menu.rs`) can grey out pieces above the
player's skill and colour each one with `ColorOf` (the same thresholds, so orange, yellow, green
or gray). It can list each piece's materials, using the item names it already gets from the item
cache, and show "Construction 123/150". It can also draw the plot's border, from x0 y0 x1 y1, and
show its standing. `Status` (op 14) keeps its `free` byte, and `free` also means the skill and
material checks are skipped.

## Verify in game

- **Tools:** each tool readied from the Utility bar shows its model in the hands (the pick, the
  knife, the pole, the hammers; Herbalism is bare hands), at a sensible size and grip; the strokes
  (overhead for the pick, axe and hammers, a cut for the knife, a reach for herbs, an overhead cast
  for the pole) look right; a copper vein takes 4 swings at Mining 1 and fewer later; the loot
  window and skill-ups come as from a right-click; fishing casts and reels; the smith's hammer at
  an anvil opens Blacksmithing; bare hands still punch; the strike sounds play (the sound kits named in
  `client/src/tools.rs`).
- **Decay:** `.fusion construction decay <days>` ages the pieces within 60 yards by that many days
  and works the map's decay out at once: check the percentages drop (2% a day), that a paid plot's
  pieces read "kept by the plot's upkeep" and don't move, and that `decay 60` brings an open-world
  piece down with what it held. After a restart the pieces carry on from their stored health.
- **Repair:** the hammer mends a tenth a strike and takes the right goods (salvage first), refuses
  within 30 s of a hit and on someone else's piece, works on a party member's piece.

- **Salvage:** take a piece down and check the bags hold "Salvaged ..." items (their tooltips), that
  they build a piece, that a piece built from them gives no skill-up while one from fresh goods
  can, and that a released plot's stash at the steward is about half, salvaged.
- **Locks:** F in build mode on your door opens the Door Lock dialog; each mode with a second
  character (in and out of your party and guild); a code lock's keypad, a wrong code, the wait
  after three misses; a plot guest opening a locked door on the plot.
- **Portals:** the model and its box (`examples/prop_bounds.rs` printed it), the list for an
  Alliance and a Horde character, the landings, the homestead, the hearthstone showing the
  cooldown after a portal or the courier's "Take me back", and the limits (two, one a plot).

- **Plot sites** (W9, sql 12; `Plots::Default()` is the fallback when the tables are missing):
  - `.tele homesteads` (9101, the re-laid Hyjal gate at 5385, -3710), `.tele homesteadshyjalcrater`
    (9102, the crater floor stop by the large plots), `.tele homesteadscrater` (9103, Azshara Crater,
    map 37) and `.tele homesteadsplateau` (9104, the Thalassian plateau). Walk each district: the
    ground, `GetHeight`, and that the map-37 PvP flag doesn't flag players.
  - An admin moves a district by its row in `fusion_plot_district` (never its `first_id`). An
    unsound table (overlaps, a district outside its site, a stop on a plot) is refused at startup
    and the built-in layout used: the log says why.
  - Each site has a floor: below it inside the site's bounds, a player is put back at the nearest
    courier stop. A ghost released in a site rises at the nearest stop's spirit healer.
- **Lumber node spawns:** gameobject guids 4191001-4191096, 8 per zone. They're placed 3 yards
  from a vanilla herb spawn and use that herb's height, so a few may float or sink. Check that
  each one sits by trees and nudge it if needed.
- **NPC spawns:**
  - Stormwind, 2591001-2591003, by Therum Deepforge at (-8424, 617, 95.5)
  - Orgrimmar, 2591011-2591013, by Saru Steelfury at (2059, -4836, 24.4)
  - Hyjal, 2591021-2591024 (gate), 9260001-9260003 (spirit healer, crater floor courier)
  - Azshara Crater 9260011-9260017, the Thalassian plateau 9260021-9260025, couriers in
    Ironforge, Undercity, Rut'theran, Thunder Bluff and Light's Hope 9260031-9260035
- **Display ids** (they were chosen from look-alike vanilla rows):
  - Lumber nodes: 1248 (LumberPile) and 1108 (Cut Woodpile).
  - Items: Rough Lumber 21102, Sturdy Lumber 13495, Hardwood Plank 7290, Ironwood Plank 929,
    Bronze Hinge 7840, Fortified Plate 26611, Sentry Targeting Unit 16367.
  - NPCs: 7179, 3482, 1298, 5032.
- **Reserved guids:** the creature guids 2591001+ and gameobject guids 4191001+ are above the dump's
  own rows but below where `AUTO_INCREMENT` would reach soon. If GM-spawned rows ever approach
  them, move the ranges.

## Furniture

Construction's decor layer, after retail WoW's housing (decor placed piece by piece, aligned to
the surface it goes on, small things parented onto tables, a grid and 15-degree turns) and the fan
projects that brought it to older clients. 55 pieces, each one of WoW 1.12's own doodads; many of
them do something. The rules are `include/fusion/Furniture.h`, `src/Furniture.cpp`,
`tests/FurnitureTests.cpp`; the game side is `server/src/game/Fusion/FusionFurniture.cpp`; the
client's are `client/src/build/pieces.rs` (`FURNITURE`, `place_furniture`) and
`client/src/build/furniture.rs` (the page and stash dialogs).

| Part | Where |
|---|---|
| The catalogue (ids 200-254: name, use, mount, station, a table's top, bounds, stash room, book, price, drop weight) | `Furniture.cpp` `All()`; the client's `pieces.rs` `FURNITURE` (models); bounds from `client/examples/prop_bounds.rs` (`furniture ...` lines) |
| Recipes (a chair at 1, a canopy bed at 240, a potbelly stove at 260) | `Construction.cpp` `MakeRecipes()`, after the structures |
| Placement: floor, table top (`OnSurface`), wall (`OnWall`), the closer same-piece spacing | `Furniture::Footed`, `Build::Overlaps`; `FusionMgr::BuildPlace` |
| Uses: F (build channel op 8), the stash (op 9), pages (op 21), the stash view (op 22) | `Furniture.h`; `FusionMgr::HandleChannel` |
| The stand-ins: a mailbox gameobject (194200), the training dummy (94250), a trophy likeness (94251) | `server/sql/custom/azeroth_warfare/33_furniture.sql`; `FusionFurniture.cpp` `UpdateFurniture`; the dummy's script `npc_fusion_training_dummy` in `scripts/custom/custom_fusion.cpp` |
| Stations as spell focuses | `Spell::CheckCast` (focus objects), `NearAnvil` (fusion parts) |
| The stage's boost | `MinstrelMgr::Finish` |
| The stash and trophy tables (characters DB, made at start) | `fusion_plot_stash` (plot, slot, item, count), `fusion_furniture_trophy` (piece, display, scale) |

### Decisions

- **A furniture piece is a piece.** Its own type on the wire and in `fusion_build_piece` (ids from
  `Build::kFurnitureBase` = 200, after the structures; `IsStructure` stops there), so it is
  saved, streamed, decayed, repaired, refunded, salvaged and packed as every piece is, and placed
  from a kit (made at the trade window, or bought, or found) with Build Structure. It is a prop to
  the rules (`IsProp`): no reinforcement, no snapping.
- **No cap.** As the rest of building, cost and upkeep are the limit; nothing new is drawn per
  piece but its one model. (Retail caps exterior decor at a budget for performance; we have no
  budget and haven't needed one. If a plot ever needs one, `Furniture::StashSlots`'s pattern, a
  sum over the plot's pieces, is the place.)
- **Finer placement.** Positions on a quarter-yard grid and turns in 15-degree steps (R turns
  furniture a step at a time); Sprint held places free (no grid, no steps). A wall piece aimed at
  a wall hangs flat on it, its back against the wall and facing out (`place_furniture`); the
  server holds it up only if it touches a built wall, beam, structure or prop (`OnWall`: furniture
  is no wall). Small pieces (candles, mugs, books, flowers, globes, kegs) may also stand on a
  table's, desk's or bench's top (`Mount::Surface`, a `top`); chairs and beds may not. Two of the
  same piece may stand 0.1 yd apart (others keep half a yard). Furniture holds up furniture but
  never the building (`Build::Solve`).
- **Uses (F, out of build mode; plain decor and tables do nothing):** a seat sits you facing out
  of it; a bed lies you down. A bed on a plot where you have Guest access or better (your
  homestead, or one you hold a permit on) makes you rest within 10 yards, as an inn does (rested
  XP builds; `REST_TYPE_IN_CITY`, cleared when you leave). A chest (16 slots), footlocker (8) or
  wardrobe (24) opens the plot's one shared stash, its room the sum of the plot's stash pieces (at
  most 240): plain items only (not soulbound, enchanted, of a random property, quest items, filled
  bags or conjured), kept as entry and count, all-or-nothing deposits, takes limited to what the
  bags hold. Out in the world a chest is only a chest. A packed plot's stash goes to its stash
  pieces' builder at the Plot Steward. Books show a lore book or a guide (`Books()`): A Builder's
  Primer, Hearth and Home, The Fall of the Second War, On the Use of Firearms (plus two kept for
  later pieces). Stations count as WoW's spell focus within 8 yards: anvil 1, loom 2, forge 3,
  cooking fire and potbelly stove 4, alchemy table 663 (the Alchemy Lab), and the anvil for the
  fusion's own parts. The mailbox is a real mailbox (a gameobject the server stands there):
  right-click it. The training dummy is a creature that never fights back or dies; each player's
  hits are metered and reported (damage, time, hits, damage per second) 5 s after they stop. A
  trophy plinth's owner, with a companion out, mounts its likeness on it (persisted). Songs
  played on (within 2.5 yd of) a bard's stage land 25% stronger.
- **Sold and found.** The Plot Steward sells kits of the mailbox (5g), training dummy (2g), the
  two faction banners (50s), the red rug (30s) and the painting (40s) (`priceCopper`;
  Homestead action 12). Cutting lumber finds a kit 4% of the time, weighted over the rustic
  pieces (`dropWeight`: candle, mug, flowers, book stack, log bench, footlocker, bearskin rug,
  boar head). A bought or found kit is a kit like a made one.
- **The stand-ins are the server's own objects.** The mailbox and dummy pieces draw nothing on
  the client (`model: None`) but their collision; the dummy's piece is only its base, so shots
  reach the creature. Stand-ins are summoned near players (120 yards, checked every 3 s) and
  removed when their piece is gone.
