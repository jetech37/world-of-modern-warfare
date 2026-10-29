# Battle companions: integration guide

The rules live in `include/fusion/Companions.h` / `src/Companions.cpp` (namespace
`Fusion::Companions`, tested by `tests/CompanionTests.cpp`). The world data lives in
`server/sql/custom/azeroth_warfare/02_companions.sql`. This document is for whoever wires them
into the game server (`server/src/game`) and the clients (`benilla/`, `client/`).

**Integrated.** The game side is `src/game/Fusion/FusionCompanions.cpp` (declared in
`FusionMgr.h`), reached through one-line hooks: `FusionMgr::Load` (`LoadCompanions`),
`UpdatePlayer`, `OnDamage`, `OnUnitKilled`, `HandleChannel` (channel 4);
`WorldSession::HandleUseItemOpcode` (a companion item's use, before the spell-slot check);
`Creature::GenerateLootForBody` (drops) and `Unit::Kill` (a corpse whose only loot is a fusion
drop is lootable); `Unit::DealDamage` (the companion-duel clamp); `WorldObject::GetReactionTo` /
`IsValidAttackTarget` (duelling companions are enemies); `Item::DeleteAllFromDB` (a destroyed
item forgets its companion); `.fusion pet` / `.fusion petduel` in `Chat.cpp`. The client side is
`client/src/companion.rs` (the bar) and benilla's bag right-click
(`ui_items::companion_summon`, the channel's summon request). Notes on what was decided where the
guide below left a choice:

- The owner level-up, the "new area" bar and the item-left-the-bags checks run in the companion's
  half-second look (`UpdateCompanion`), not in `Player::GiveLevel` / `UpdateZone`: the same
  effect with no hook in `Player.cpp`. A banked bar is also paid at every summon.
- Participation credit is a `DamageCredit` of victim -> owners (`OnDamage`), plus a companion in
  the fight when it ended; xp is credited in `OnUnitKilled`.
- The companion runs `PetAI` (replacing whatever the display creature's template picks) in
  defensive mode, and is told to attack what its owner hits and what hits its owner. Ranged roles
  close to melee for now (their numbers already price them as fragile).
- Stats: the rules' health and armor are set through the pet's base stat modifiers, so the stat
  system keeps them through recomputes (`BuildCompanion`).
- A companion beaten to 0 health **faints** (see "Fainting and reviving" below): it goes back in its
  item and stays there, on its own, until a stable master or a Reviving Draught revives it or its
  hour is over. A teleport to another map (which unsummons guardians) re-summons it on arrival.
- Duel experience is paid at most 5 times a day per pair of players, and only for a fight won
  (beaten, or ahead when time runs out; `DuelPaysXp`), never a forfeit or a flight. A duel is no
  heal: each companion goes back no healthier than it went in. Losing a companion duel never
  faints it: the fight ends at 1 health, as a player duel does. A challenge only waiting for its
  answer holds nobody's companion (`DuelHolds`); the same two players challenge each other once
  a minute at most. An owner who leaves the duel's map flees.
- Each companion's wounds are kept per item and saved (`CompanionRest.h`, `Recovery`,
  `character_fusion_companion_rest`), its pet bar cooldowns per item in memory (`RestBook`):
  swapping companions or relogging heals nothing and resets nothing (wounds heal in a minute of
  rest). One put away dead (its item, Dismiss, dying with its owner, a logout) faints all the same.
- A companion item's rarity rolls by the corpse it is stored from (`FusionLootSource`, set around
  the loot stores, the area-loot window's other corpses, group rolls and the master looter).
- Mounts follow vanilla's mount rules: no dungeons, raids or indoors (`RideBlock::NotHere`).
- **What a buyer sees.** A companion item's species, stage and level ride in its suffix factor
  (`ITEM_FIELD_PROPERTY_SEED`, `ItemSeed` / `FromItemSeed`), which companion items never use for a
  random suffix. The field reaches the client in the item's update fields and in the auction,
  trade and mail lists, so the tooltip reads "Level 43 Wolf Pup, stage 3 of 3" everywhere.
  `FusionMgr::StampCompanionItem` writes it on `Item::Create`, `Item::LoadFromDB` and every state
  change, and stamps the auction house's items after `LoadCompanions` (they load first). It is
  not saved: the companion table is the record. The client: benilla's `set_item_note` hook, filled
  by `companion::item_note`.

## Companion duels (`CompanionBattles.h`, `FusionCompanionBattles.cpp`, channel 13)

Turn-based **card duels after Slay the Spire**, separate from open-world fighting (they replaced the
Pokemon-style battle of phase 1): design, card lists and balance in `design/COMPANION_BATTLES.md`.
Rules and tests: `CompanionBattles.h`, `tests/CompanionBattleTests.cpp` (with the AI-vs-AI balance
sim). Each companion plays its own **deck**: built from its family's archetype, its stage picks
(each picked ability is a card), its traits, its parents' and what it learned (trainers' lessons,
tamers' daily drops). Decks are saved per item in `characters.character_fusion_companion_deck`
(made at startup, cleaned of orphans, forgotten with the item in `ForgetCompanion`); a companion
with no saved deck, or one that no longer holds, duels with its **starter deck**, so every
existing companion can duel at once. World data: `33_companion_battles.sql` (tamers 94201-94223 and
Companion Battle Trainers 94230-94235, script `npc_fusion_companion_tamer`; table
`fusion_companion_tamer`; the trainers also sell card lessons). Wanderers whose seed makes them
tamers (`IsWandererTamer`) offer a duel in their gossip. PvP and decks:
`.fusion petbattle [accept|decline|forfeit|deck]` or channel ops 10/11 and 14/15. Players are
rooted and pacified for the duel and their companion is put away; a duel never wounds or faints a
companion (a beaten one stays at 1 health in the duel). The hunter's bond (`ClassTuning`) never
reaches a duel: duel health comes from `DuelHealth`, which has no class in it. Daily tamer rewards:
`characters.character_fusion_tamer`. Wire version 2 on both sides. Client:
`client/src/companion_battle.rs` (the duel window and the deck window).

## The vanilla pets (`31_vanilla_companions.sql`, `Companions::VanillaPets`)

Every vanilla 1.12 pet item is retired and replaced by a companion item whose species' first form is
that pet's line (catalog section 1d): 11 go to existing species (Dark Whelpling to Black Whelpling, Silver
Tabby to Snow Tiger Kitten...), 50 to new species 165-214 (items 91165-91214). The world SQL rewrites every
source (npc_vendor, creature/gameobject/item/reference/mail loot, quest rewards, spell_template's
created items), so the old entries can no longer be had. `FusionMgr::SwapRetiredPetItems` runs from
`World::SetInitialWorldSettings` before the auction house loads its items and rewrites the old entries
in `item_instance`, `character_inventory`, `mail_items` and `auction` (`VanillaPetSwapSql`); the
copies become fresh stage-1 companions (no companion row yet). Running it again changes nothing.
The recipes' tooltips in the stock client still name the old item (its DBC), but the craft makes the
companion item. Tests: `CompanionVanillaPetTests.cpp`, and the SQL mirrors in `CompanionTests`.

## Flying mounts (`35_flying_companions.sql`, species 215-223)

The Wrath flying mounts as companion lines (catalog sections 1g and 2e): Gryphon Hatchling, Wind
Rider Cub, Frayfeather Hatchling (hippogryph), Dragonhawk Hatchling, Netherwhelp, Proto-Drake Whelp,
Frostbrood Whelp (frost wyrm), Phoenix Hatchling, Bronze Whelpling (bronze drake). New families 59
Gryphon, 60 Wind Rider, 61 Hippogryph, 62 Dragonhawk (`Family`, `kLastFamily`); the drakes are
Dragon, the phoenix Fire Elemental, with their own movesets (`Moveset::Drake`, `Moveset::Phoenix`,
by species id in `MovesetOf`).

- **Models.** Each line's last form is a 3.3.5 riding model and most first forms 3.3.5 pets, from the
  WotLK patch layer (displays 40019-40037, `client/src/wotlk/import.rs` `LOOKS`,
  design/WOTLK_ASSETS.md). The SQL's `creature_display_info_addon` rows and the looks
  (`kFlyingLooks` in `Companions.cpp`) name them; a server without the layer's
  `CreatureDisplayInfo.dbc` falls back to the stage creature's own look, as for 33.
- **Riding.** The last form rides on the wolf's spells (580 / 23250) with its own display
  (`ride_display`), level 40 and riding skill as every mount line, and it **skyrides**.
- **Mounting is a cast** (`Companions::MountCastMs`, `MountSpot`, `MountCastHolds`, op 7
  `[7, phase, ms]`; `FusionMgr::RideCompanion`, `UpdateMountCast`, `FinishRide`). Its owner stands
  still under the stock cast bar ("Ride <form>", `companion_bar::mount_cast_edges`) for 1.2 to 3.5 s
  while the companion answers in its own voice (its pet-order bark) and runs to a spot 1.5 yd in
  front of them, faster when far off so it arrives as the bar ends; then the mount spell's aura goes
  on at once (`AddAura`: no second cast, none of the spell's own summoning sound). A step, a fight,
  the companion gone or the ride key again breaks it ("Interrupted"). The buff and its tooltip read
  "Ride <form>" (benilla `LayerMountLook`, and `GameTooltip:SetPlayerBuff` titled by the aura's own
  name), never the wolf spell's.
- **Skyriding** (`Skyriding.h`, `Riding.h` `Phase::Skyride`, `FusionRiding.cpp`, client
  `riding.rs`): Dragonflight-style momentum flight, never free. Ground mounts keep the old glide
  and launch-point burst.
  - *Getting up.* A mounted fall from `kGlideMinHeight` (8 yd) or more turns into a skyride at the
    mount's run speed; Launch takes off from the ground; a launch point's updraft is a free
    Ascent (its 30 s cooldown kept).
  - *The physics* (`Skyriding::Advance`, mirrored to the float in `riding.rs` `advance`, one shared
    test vector on both sides). The mount always flies forward along the rider's pitch (clamped
    -80..+70 degrees): speed changes by `-kGravity·sin(pitch) - kDrag·speed` (16, 0.06), and it
    always sinks `kSink` (1.5 yd/s), more under `kStallSpeed` (10). So a dive gains speed (capped at
    `kMaxSpeed` 50), a pull-up spends it on height, level flight slows and sinks: energy
    `g·z + v²/2` only falls. Range comes from height: a -8 degree glide covers about 40 yd from
    10 yd up, 490 from 100, 2100 from 400.
  - *Vigor.* `kMaxCharges` 3; one back every `kChargeAirSeconds` 20 in flight, every
    `kChargeGroundSeconds` 5 otherwise; `kMoveGapSeconds` 1 between moves.
  - *Launch* (the mounted move, pet bar slot 7 / Ctrl+7, Slow Fall's icon 130, slot
    cooldown 1 s; or Ctrl+Space on the ground or in the air, or Space in the air): a charge for `kAscentRate` 28 yd/s straight up for
    `kAscentSeconds` 1.2 (about 34 yd), leaving at `kAscentExitSpeed` 20 at least.
    *Surge* (Shift in the air, world op 72): a charge for `kSurgeSpeed` 16 more speed.
  - *Landing* (under 2.5 yd over the ground, after a take-off's 2 s grace) ends it; water ends it;
    knocked off, out of time (`kMaxFlightSeconds` 300) or against the law, it ends in the feather
    fall glide down (`Phase::Descent`).
  - *Authority.* The server grants GM flight (the client's levitating swim, as the burst) with the
    swim speed at `kMaxSpeed`·1.05, so the stock movement anticheat bounds the speed as for any
    swimmer; the client flies the physics through benilla's `LayerFlight` seam (forward always on,
    no breach). `Skyriding::Envelope` holds every position report to the energy law: a budget
    starting at the take-off's energy, raised only by a spent charge (`Gain`), bled each 0.4 s
    sample by most of the sink and half the drag at the measured speed; a report whose height and
    speed are worth more than the budget plus `kSlackHeight` 8 yd / `kSlackSpeed` 6 yd/s twice
    running ends the flight (`Flight::broke`, logged to the anticheat log). Holding altitude,
    climbing without a charge, or cruising faster than the height pays for are all ended.
  - *Wire.* World op 70 phase 4 is the skyride; op 71 `[flags, charges, max, progress, speed]` on
    every change (`SKY_FLYING`, `SKY_ASCENT`, `SKY_SURGE`, `SKY_RIDER`); op 72 `[move]` asks.
  - *Client feel.* The mount pitches with the flight (the swim pose's pitch law), the camera is the
    rider's look, the view widens with speed (to 1.25x at 50 yd/s), and a gauge over the action bar
    shows the three vigor pips (the refilling one by its share) and the speed.
  - `.fusion riding` reports the phase, the energy budget (as a height), the measured speed and the
    vigor.
- **Sources.** Drops from the fitting 1.12 creatures (entries only, no family fallback); the first
  three are also sold for 10 gold by Yarlyn Amberstill (Dun Morogh), Halpa (Thunder Bluff) and
  Shylenai (Darnassus).
- **Tests.** `CompanionTests` (the SQL against the catalog, the mount families), the move tests (a
  moveset per species, the mounted move), `SkyridingTests.cpp` (the physics, range from height,
  vigor, the envelope passing a lawful flight and ending free flight and speed hacks, the phase,
  the wire), the client's `riding` tests (the same vector, the ops, the gauge),
  `CompanionFlyingTests.cpp` (the Ascent, the riding forms,
  the display range), the client's `wotlk::import` tests (the display rows match `LOOKS`, the size
  rules against the built layer, every line ridden as a riding model at scale 1).

## Looks: a display per stage (`29_companion_looks.sql`, `CompanionTraits.h`)

Every form of a line wears its own display (`StageInfo::display`, columns `stage1_display` to
`stage3_display`, added by `29_companion_looks.sql`, which also sets every stage's scale; table and
choices: `design/COMPANION_CATALOG.md` section 1c). `LoadCompanions` reads the columns when they are
there (a display the client lacks is logged and dropped: the creature's own then).

- **Display.** `CompanionDisplay` = `LookDisplay` (the stage's own, or a hybrid's other line's at
  that stage), else the template's display picked by the item as before. Summon, evolution
  (a re-summon) and the ride all go through it, so a live companion wears its stage's new look at
  its next summon; nothing per item changes.
- **Size.** `CompanionScale` = `LookScale`: the stage's scale and the traits' (Giant, Runt) on top of
  the display's own size (`Unit::GetScaleForDisplayId`, what vanilla gives any creature), then
  `UpdateModelData` for its reach. A mount line's last form at 1.0 is the size it is ridden at.
- **Riding.** The ridden display is the one it walks with: the wolf, cat, raptor and kodo lines end
  on riding models, so the summoned companion and the mount are one model; the lines on the wolf's
  spells carry their last form's display in `ride_display`. The fork's seat offsets
  (`entities/mount.rs`, `COMPANION_SEATS`) stay only for the finals on seatless models.
- **The window.** Op 34 sends each stage's display; the Bestiary's form buttons show each. The 3D
  pane is the stable pane borrowed (`LayerCreaturePreview`): while the window holds it, it renders
  live (half rate with `boothHalfRate`), so the model plays its Stand loop, and its camera is fitted
  to the model's box (`portrait::framing::fit_frame`: the sphere the model turns in fills the
  frame), so a whelp and a riding kodo both show whole at every facing the drag turns them to.
- **First form = the young of the last** (catalog section 1f). Stage 1 wears the last form's skin (or
  its colour on a young model), named for it (Bombay Kitten is now Shadowmaw Kitten); the lines 1.12
  cannot match yet are listed in the client test's `FIRST_FORM_TONE_EXCEPTIONS`. Imported (WotLK)
  young models only need a new `stage1_display` / `stage1_scale` (SQL 29/31 and `kLooks`).
- **Tests.** `CompanionLookTests.cpp` (every stage named and distinct, mount finals, scale on top of
  the display's, hybrids, op 34), `CompanionTests` (the SQL against the table), the client's
  `every_stage_look_is_a_real_display_of_its_family` (every id against the install's
  `CreatureDisplayInfo.dbc` and model files, stage 1 in stage 3's colour, skipped without one) and benilla's fit and live-pane tests.

## Fainting and reviving (`CompanionRest.h`, `FusionCompanionRevive.cpp`)

The monster-tamer rule. A companion beaten to 0 health **faints**: only that companion, its owner's
others can still be summoned. It stays fainted (no summoning, so no riding either) until one of:

1. **A stable master** (any `UNIT_NPC_FLAG_STABLEMASTER` NPC). Spoken to while a companion in the
   bags is fainted or hurt, the stable master offers a gossip first: "Revive and heal my
   companions (fee)", "Buy a Reviving Draught (15s)", "Show me my companions" (the window). The
   window at a stable master has the same two buttons. The fee is level x
   `Companions.StableReviveCostPerLevel` copper for each fainted one (10: a level 60 one costs 6s);
   healing the hurt (and the one out) is free. All or nothing: not enough money revives none.
2. **A Reviving Draught** (item 93470, `kReviveDraughtItem`; `24_companion_revive.sql`): 15s at
   every general goods vendor (the vendors titled "General ..." that sell water, Simple Wood or a
   Small Brown Pouch) and at stable masters. One revives one companion at half health
   (`kDraughtReviveHealth`). Three ways to use it: right-click the draught (the companion that
   fainted last; benilla's `companion_draught_use` sends `kOpUseDraught` with item 0), right-click
   the fainted companion's item twice within 10 s (the first says what is wrong and how many
   draughts you have), or the window's "Use Reviving Draught" on that companion.
3. **The long wait**: `Companions.FaintRecoverMinutes` (60), Loyal a third (`FaintRecoverSeconds`).
   Then it comes round whole. The wait runs on unix time, through logouts and restarts.

- **Saved.** `characters.character_fusion_companion_rest` (made at startup, rows swept with their
  items): `item_guid`, `health` (thousandths, when put away), `away_at`, `fainted_at` (unix
  seconds, 0 none). A row exists only for a companion hurt or fainted. The faint is written when
  the companion is killed (`OnCompanionKill`), wounds when it goes back in its item (Dismiss, a
  swap, a taxi or teleport, `CompanionLogout` from `WorldSession::LogoutPlayer` before the pet
  goes). Keyed by item, so a trade carries the faint with it.
- **Anti-exploit.** Dismissing, re-using the item, dying with it or logging out never skips a faint
  (`PutAway` leaves a fainted record alone; `FaintCompanion` once per faint). Swapping or relogging
  never heals (health comes from the saved record). A fainted companion can't breed. The one out at
  logout comes back at login as hurt as it was; a fainted one stays in its item.
- **Messages.** Fainting and coming round are chat lines (and a line in the UI error frame); a
  fainted one's item use says how long is left and how to revive it.
- **Window and tooltip.** `kOpCarried` adds per companion faint seconds u32, health % u8 and its item
  seed u32, then draughts u8, the stable fee u32 and how many need care u8 (`CarriedCare`). The
  client greys a fainted one's portrait, shows "Fainted: comes round on its own in 52 min", a
  "Use Reviving Draught (n)" button and, at a stable master, "Revive and heal my companions" and
  "Buy Reviving Draught". The item tooltip (`companion::item_note`, keyed by seed from the carried
  list) adds "Fainted: comes round in 52 min" and how to revive it.
- **Ops** (channel 4, the window's): 26 `kOpStableRevive` `[26]`, 27 `kOpUseDraught`
  `[27, item u64]` (0: the one that fainted last), 28 `kOpBuyDraught` `[28]`. Stable ones need a
  stable master near (`Index::Action::StableCare`), an admin anywhere and free.

## The pet slot, movesets, traits, breeding (package W5)

Data: `design/COMPANION_CATALOG.md`. Rules: `CompanionMoves.h` (movesets, picks, autocast, pet
bar slots), `CompanionTraits.h` (traits, rarity, combinations, breeding). Game:
`src/game/Fusion/FusionCompanionBar.cpp`. World SQL: `08_companion_lines.sql` after 02.

- **Pet slot.** The companion is a `Pet(SUMMON_PET)` with `Pet::IsFusionCompanion()` set, the
  player's `GetPet()` (UNIT_FIELD_SUMMON, SMSG_PET_SPELLS). `Pet::SavePetToDB` returns early for
  it, so it never reaches `character_pet`. A hunter's or warlock's old pet (loaded at login) is
  unsummoned `PET_SAVE_NOT_IN_SLOT` every 2 s and before a summon. The game's temporary
  unsummons (taxi, other mounts, map change) leave the companion suspended; it comes back after.
- **Pet bar** (`PetBarSlot`): 1 Command, 2 Assist/Aggressive, 3 Hold, 4-6 the stage picks, 7 the
  mounted move (channel only, the pet is away while ridden), 8 a Twin-souled stage's other
  option, 9 Follow, 10 Stay. `WorldSession::HandlePetAction`, `HandlePetSetAction` and
  `HandlePetSpellAutocastOpcode` hand companion presses to `FusionMgr::CompanionPetAction` /
  `CompanionAutocast`. The client shows no stock pet bar: its **Companion action system** (C,
  client `companion_bar.rs`) puts these slots on the main bar (`ActionSystems::CompanionSlot1`..10,
  ability ids 24-33, saved only on buttons 216-227, `kCompanionBar`), a press or slot key pressing
  the slot and a right click toggling autocast (`kOpAutocast`). The client presses with the
  Companion bar or Ctrl+1..0 (`kOpBarPress`: slot, the unit under the
  crosshair, the ground point). Abilities are the vanilla spells cast triggered from the
  companion with base points from its basic hit (`AbilityDirect` / `AbilityOverTime`); the
  catalog's durations are clamped on the aura half a second later (stuns on players at most 3 s);
  cooldowns are tracked per slot (Primal -20%) and sent as SMSG_SPELL_COOLDOWN and `kOpCooldown`.
  Autocast is `ShouldAutocast` per ability kind, every half second.
- **Picks.** `PendingPick` owes stage 1 at the first summon and each reached stage after; the
  Companions window shows the two options and sends `kOpPick` (free, anywhere). At a stable
  master the window changes a choice already made (`Index::kOpRepick`, 50 silver a stage,
  `Keeper::CanPick`).
- **Traits** roll the first time an item is stamped (`RollFreshCompanion`: rarity by the rank of
  the creature its owner is looting). Columns added at startup to `character_fusion_companion`:
  traits, rarity, twin_stage, picks (a byte a stage), breed_count, breed_ready, generation,
  parent_a, parent_b, hybrid. Effects in play: stats and scale (`StatsWithTraits`, `ModsOf`),
  Venomous / elements / Shadow-touched / Bloodthirsty on its hits, Berserk, Phoenix-blooded
  (Ashborn), Loyal, Tracker, Scavenger and Golden Hoard, Forager and Pathfinder, Packmule and
  Quartermaster, Gunner (a gun item and shots at 25 yd), Tireless only in the rules so far.
  Not yet in play: Thick-skulled, Spellward, Alpha, the looks' tints, mount traits' riding
  effects, most combinations' extra effects (Viper's slow, Stormcaller, Colossus' health and
  Ashborn are).
- **Item seed**: level 6 bits, stage 2, species 8, three trait ids of 5 bits, bred bit 31. The
  lineage line comes over `kOpLineage` for the items in the bags, keyed by seed.
- **Breeding.** At a stable master (UNIT_NPC_FLAG_STABLEMASTER) the Companions window offers
  each fully grown companion's partners (`Index::kOpBreed`; the server checks the breed group);
  5 reagents from each, 10 silver x average level, 72 h rest, 3 per companion. `.fusion pet
  breed [free]` does it anywhere for an admin.
- **Riding.** A rideable form's pet bar holds its mount spell in slot 7 (`PetBarSlot::Mounted`);
  pressing it (or Ctrl+7) rides it (`RideCompanion`). The client names the ride "Ride <form>" with
  the family's mount icon on the Companion bar, and dresses the player's mount aura the same while
  `BAR_RIDING` holds (benilla's `LayerMountLook` seam: any helpful `SPELL_AURA_MOUNTED` aura), so no
  line shows the Timber Wolf spell it borrows. No server spell or DBC row is involved. The ridden companion shows as itself:
  `Index::MountDisplay` (the display it walked with, `CompanionDisplay` / `LookDisplay`, else its
  stage's own display, else `ride_display`, else its stage creature's; the mount lines' last forms
  are riding models, see "Looks" below), set when the mount spell's aura lands (`Aura::HandleAuraMounted` asks
  `CompanionMountDisplay`; the spell keeps its 3 s cast even triggered) and held by the
  half-second look. The fork seats riders on seatless models at a per-display height
  (`entities/mount.rs`, `COMPANION_SEATS`).
- **The ride's look** (`LookAtRide`): the mount spell is cast triggered, which keeps its 3 s cast
  but puts it in no spell slot, so only the clock says it may still land; the look waits out the
  cast time plus 1.5 s before saying "You can't ride your companion here". A companion ride
  spell's aura that no ride accounts for (`StrayRideAura`: not the ride in progress, not cast
  from an item, not a known spell) is taken off, and a logout in the saddle takes the rider off
  first: such an aura would show the spell's own mount (a Timber Wolf for every line on the
  wolf's spells).

## The Companions window (`CompanionIndex.h`, `FusionCompanionIndex.cpp`, client `companion_ui`)

The Companion Keeper NPC is gone (`13_companion_keeper.sql` spawns nothing, `15_companion_index.sql`
takes old spawns out). The window opens from the HUD (the limited view) or at any stable master
(gossip hello or the stable list open it instead of the stock stable or gossip; op 33).

- **Bestiary**: every species by family. `character_fusion_companion_seen` (made at
  startup) keeps per character what was seen (targeted: `WorldSession::HandleSetSelectionOpcode`
  -> `OnCompanionTarget`, by the stage creatures' entries, or a companion's own species) or
  caught (carried, dropped, bred, given). `Index::MakeEntry` sends an unseen species as its id
  and family only; a game master (`IsAdmin`) gets every entry in full.
- **Your companions**: the bags' companions (op 31), summon or dismiss, evolve, an owed ability
  choice, a companion duel against the target; at a stable master breeding and changing an
  ability (`Index::Allowed`).
- **Game masters** put any species in the bags at any stage and level (op 22,
  `Keeper::CanBuy` with `gm`) and change a carried one's level, stage, abilities, traits and
  Twin-souled stage (op 23, the keeper's `CanChange` / `CanPick` / `CanToggleTrait` /
  `CanSetTwinStage`). Players never buy or change a companion (`Keeper::kPlayersBuy`).
- **Looks** (op 34 `kOpLooks`, `Index::MakeLooks`): for each species the reader may see, each
  stage's display (its own from the look table, else its creature's: the window's 3D model, the
  stable window's pane, which the window borrows live and fitted to the model), each stage's two
  ability spells (their icons), the basic and mounted moves' spells, the display it is ridden
  with (`MountDisplay`) and where it is found (`FoundText`, from its drop sources). Sent after the
  index pages and again for an entry newly seen; a client that does not know the op skips it.
- **Lines**: the companion's messages (levels, evolutions, refusals, duels) go to the client's
  UI error frame (op 32, `FusionCompanionTell`), not the chat.
- **Item names**: the client names a companion's item after its current form (benilla's
  `set_item_name` hook, from the item seed and the index's form names) in bag tooltips and
  links, trade, mail and the auction house.

## The design in one page

- **No capturing.** A companion comes from a **companion item** (entries 90001-90099, one per
  species: entry = 90000 + species id). Items are quality 3, Bind on Equip (`bonding` 2, never
  equipped so never bound), `stackable` 1. They drop rarely from creatures, critters included
  (`DropChance`: critter 0.5%, normal 0.2%, rare 2%, elite 1%, rare elite 5%, boss 3%).
- **The item instance is the companion.** Level, xp, stage and the evolution kill count are stored
  per **item guid** (`character_fusion_companion`). Trading, mailing or auctioning the item moves
  the companion with all its progress. Keeping many costs bag and bank space, on purpose.
- **It fights.** A summoned companion is a guardian that follows its owner and fights what the
  owner fights. One companion at a time. It can be out together with a hunter or warlock pet (it
  is a guardian, not the controlled pet) and with a vanity mini-pet.
- **It follows at an offset, never in its owner.** Every follow (summon, Follow command,
  double-tap Command, after a fight, ride or catch-up) is the follow movement at
  `Companions::FollowDistance(radius)` (2.5 + 1.5 x bounding radius, at least `PET_FOLLOW_DIST`)
  and `kFollowAngle` (0.8 pi: behind and to the left), through `Pet::GetFollowDistance/Angle`.
  When the walk to that spot can't be traced on the navmesh (the package runs with
  `mmap.enabled = 0`), vanilla's follow fell back to the owner's own position, which stood
  pets in the player; a companion keeps its follow spot (`Companions::FollowFallbackOf`).
- **Never stronger than a hunter pet.** `HunterPetBaseline(level)` is the yardstick (3340 health,
  49.5 dps, 3000 armor at 60). A role takes a share of it (melee 55% health / 50% dps / 55% armor;
  ranged 40 / 55 / 35 at 20 yards; tank 70 / 35 / 80) times a stage multiplier (baby 0.85, adult
  1.0, elder 1.12; a two-stage species 0.85 / 1.05; a single-stage one 0.92; every
  species now has three stages, design/COMPANION_CATALOG.md 1e). The highest product
  is 0.896 of the baseline. Tests check every species, stage and level.
- **Levels 1 to 60, capped by the owner.** `EffectiveLevel = min(level, ownerLevel)`. It fights,
  and its stats are built, at its effective level. At the owner's level it banks experience up to a
  full bar and drops the rest; when the owner dings, `GainXp(state, 0, newLevel)` pays the banked
  bar at once. A companion bought by a lower character keeps its level on the item and fights at
  the owner's level.
- **Faster than a player.** A level costs 60% of a player's 1.12 xp (`XpToNext`), and the
  companion gets the full `KillXp` of every kill it took part in (1.12's creature xp: 5L+45 at
  the same level, +5%/level above to +4, falling off to the gray level, elites x2).
- **Evolution.** Most species have a baby form that grows into larger forms of the same creature
  (whelpling -> whelp -> drake, pup -> wolf -> dire wolf, cub -> cat, spark -> elemental, hatchling
  -> murloc). An `EvolutionRule` wants a level, and may want a catalyst item (consumed), a zone or
  sub-area, night or day, or N kills of a creature type made while the companion was out. The
  companion must be summoned, out of combat and not duelling. Catalysts (90101-90104: Draconic
  Essence, Primal Beast Essence, Elemental Core, Fel Ember) drop from dragonkin, beasts, elementals
  and demons of level 25+ (`CatalystDropChance`).
- **Companion duels.** Real time. Levels normalized to the lower effective level. Beaten at 1
  health (no death), fled, forfeited or an owner joining in loses; 3 minute limit, then the higher
  health share wins. No xp loss; the winner gets 5% of a level unless the loser was gray to it.

## 1. Database

### World (`02_companions.sql`, apply after the world dump; idempotent)

- `fusion_companion_species`: one row per species (stages' creature entries and scales, two
  evolution rules, drop sources). `CompanionTests` parses this file and checks it row for row
  against `StarterSpecies()`, so edit both together (or load the table and stop using the C++
  copy; the tests then only guard the starter set).
- `item_template` rows 90001-90028 (companions) and 90101-90104 (catalysts).

### Characters (create at startup in code, as `FusionMgr::LoadPieces` does for `fusion_build_piece`)

```sql
CREATE TABLE IF NOT EXISTS `character_fusion_companion` (
  `item_guid` int(10) unsigned NOT NULL COMMENT 'item_instance.guid',
  `species` smallint(5) unsigned NOT NULL,
  `stage` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `level` tinyint(3) unsigned NOT NULL DEFAULT '1',
  `xp` int(10) unsigned NOT NULL DEFAULT '0',
  `evolve_kills` smallint(5) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`item_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

- A companion item with no row is a fresh companion: species from `FindByItem(item entry)`,
  stage 0, level 1, xp 0. Write the row on the first change (`REPLACE INTO`), asynchronously.
- vmangos keeps an item's guid through trade, mail and the auction house (the same `Item` object
  moves), so the state follows the item with no extra work.
- When an item is deleted (`Item::DeleteFromDB`, `Objects/Item.cpp:551`) delete its row; also at
  startup sweep orphans:
  `DELETE c FROM character_fusion_companion c LEFT JOIN item_instance i ON i.guid = c.item_guid WHERE i.guid IS NULL`.
- Map the C++ struct straight across: `CompanionState{species, stage, level, xp, evolveKills}`.

## 2. Loading

In `FusionMgr::Load()` read `fusion_companion_species` into a `std::vector<CompanionSpecies>`:
the `*_areas` and `drop_entries` columns are comma-separated id lists, `evoN_level` 0 means no
rule, `stageN_creature` 0 ends the stages (so `stageCount` = number of non-zero stage creatures).
Keep the vector for `FindByItem`, `RollDrop` and `CanEvolve`.

## 3. Summoning (item use)

The items have **no spell**. Stock vmangos rejects such a use in
`WorldSession::HandleUseItemOpcode` (`Handlers/SpellHandler.cpp:36`, the
`proto->Spells[packet.spellSlot].SpellId == 0` check), and the stock 1.12 client does not send
`CMSG_USE_ITEM` for an item without an on-use spell. Two ways in, do both:

1. **The companion bar / benilla's bag right-click** sends `CMSG_FUSION_CHANNEL` channel
   `kChannelCompanion` (4), op `kOpSummon` with the item guid. Route channel 4 in
   `FusionMgr::HandleChannel` (`Fusion/FusionMgr.cpp:2228`) through `DecodeRequest`.
2. In `HandleUseItemOpcode`, right after `proto` is found and **before** the spell-slot check, add
   `if (Fusion::Companions::IsCompanionItem(proto->ItemId)) { sFusionMgr.UseCompanionItem(pUser, pItem); return; }`
   (keep the trade and `CanUseItem` checks in `UseCompanionItem`). If a stock client must work too,
   give the items a harmless dummy on-use spell in `spellid_1` (trigger 0); the hook still
   returns before any cast.

`UseCompanionItem`: the item must be in the player's bags (not bank, not in a trade window); if
this companion is out, dismiss it (toggle); if another is out, dismiss that first. Then summon a
guardian the way `Spell::EffectSummonGuardian` does (`Spells/SpellEffects.cpp:2775`):

```cpp
CompanionSpecies const& sp = ...; CompanionState st = ...;          // from the item guid
uint8 level = Fusion::Companions::EffectiveLevel(st, player->GetLevel());
CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(sp.stages[st.stage].creature);
Pet* c = new Pet(GUARDIAN_PET);
c->Create(map->GenerateLocalLowGuid(HIGHGUID_PET), CreatureCreatePos(player, player->GetOrientation()), cInfo, sObjectMgr.GeneratePetNumber());
c->SetOwnerGuid(player->GetObjectGuid()); c->SetCreatorGuid(player->GetObjectGuid());
c->SetFactionTemplateId(player->GetFactionTemplateId());
c->InitStatsForLevel(level, player);
// then overwrite with the rules' numbers:
CompanionStats s = Fusion::Companions::Stats(sp, st, player->GetLevel());
//   SetCreateHealth / SetMaxHealth / SetHealth(s.health), SetArmor(s.armor),
//   SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE/MAXDAMAGE, s.damageMin/Max), SetAttackTime(BASE_ATTACK, s.attackMs),
//   UpdateDamagePhysical(BASE_ATTACK)
c->SetObjectScale(sp.stages[st.stage].scale);
c->SetFollowAngle(...);           // offset from a hunter/warlock pet, as EffectSummonGuardian does
c->AIM_Initialize(); map->Add((Creature*)c); player->AddGuardian(c);
```

Notes:
- The display creatures are ordinary world creatures (some elite, some with `ai_name` /
  `script_name`). Make sure the guardian runs **PetAI** (follow, assist the owner, defend), not the
  template's EventAI/script: set it explicitly if `AIM_Initialize` picks the template's.
- **Ranged role** (`s.reach` = 20): PetAI closes to melee. Either let it (the numbers already
  price ranged as fragile), or have the fusion tick keep it at `reach` and deal its auto attack as
  a ranged hit every `attackMs` (a visual spell such as a fire bolt for whelps). Integrator's call;
  melee-only is acceptable for a first pass.
- Remember `{player guid -> companion guid, item guid}` in `FusionMgr`. Dismiss when: the owner
  logs out, dies, or uses the item again; the item leaves the bags (check each fusion tick that
  `player->GetItemByGuid(itemGuid)` is still there); the companion dies (no xp loss; it faints,
  see "Fainting and reviving").
- On `Player::GiveLevel` (`Objects/Player.cpp:3068`): `GainXp(state, 0, newLevel)` (cashes the
  banked bar), rebuild stats at the new effective level, persist, send the bar.

## 4. Experience on kills

- **Participation**: in `FusionMgr::OnDamage` (called from `Unit::DealDamage`,
  `Objects/Unit.cpp:662`) when the attacker is a companion guardian, add the victim's guid to that
  companion's "touched" set (forget entries after ~5 min, like `DamageCredit`). Also count the kill
  if the companion was out and in combat with the victim when it died.
- **Credit**: in `FusionMgr::OnUnitKilled` (called from `Unit::Kill`, `Objects/Unit.cpp:994`),
  when a creature dies, for each companion that took part: `KillXp(EffectiveLevel(st, ownerLevel),
  victim->GetLevel(), rank >= elite)` then `GainXp(st, xp, ownerLevel)`, and
  `RecordKill(species, st, victim type)` for KillType evolution conditions. Persist, send the bar.
  No xp for players, critters (always gray) or gray creatures (`KillXp` returns 0). The companion's
  xp does not reduce the owner's.

## 5. Drops

- In `Creature::GenerateLootForBody` (`Objects/Creature.cpp:1655`), after `FillLoot`, build a
  `DropVictim{entry, type, pet_family, rank}` (use `CreatureRank::DungeonBoss` for instance
  bosses) and `RollDrop(species, victim, rand_norm(), rand_norm())`; if a species comes back, add
  its item to `loot` (`loot.AddItem(LootStoreItem(item, 100.f, 0, 0, 1, 1))`).
- Catalysts: `if (rand_norm() < CatalystDropChance(type, rank, level)) loot.AddItem(... CatalystFor(type) ...)`.
- Critters have no `loot_id`; make sure a corpse with only the companion item still becomes
  lootable (the lootable dynamic flag is set when loot is not empty).
- Announce a companion drop to the looter (and, for rare elites and bosses, maybe the zone).

## 6. Evolution

On `kOpEvolve` (item guid): find the companion (it must be the one summoned), build an
`EvolveContext{ownerLevel, summoned, inCombat (owner or companion), inDuel, zoneId =
GetZoneId(), areaId = GetAreaId(), itemsHeld = GetItemCount(rule->item), hour = local hour of
sWorld.GetGameTime()}`, `CanEvolve(species, state, ctx)`. On `None`: `DestroyItemCount(rule->item,
rule->itemCount, true)`, `Evolve(state)`, persist, re-summon at the new stage's creature and scale
(or `UpdateEntry`), send `kOpEvolved` (play an effect) and the bar. Otherwise send the bar, whose
`block` tells the client why. Send the bar again on `Player::UpdateZone` (`Objects/Player.cpp:6598`)
so the evolve button lights up when the owner walks into the right place.

## 7. Companion duels

`FusionMgr` keeps a `CompanionDuel` per pair (and an index from each owner to it).

1. `kOpDuelChallenge(target)`: both players have a companion out, neither is in combat or another
   duel, distance <= `kChallengeRange`; `Challenge(a, b, now)`; send the target `kOpDuel` with
   phase Challenged (the client shows Accept / Decline).
2. `kOpDuelAnswer(accept)`: `Accept` or `Decline`. On accept: heal both companions to full, rebuild
   both at `DuelLevels(effA, effB)`, remember the arena center (midpoint of the two companions).
3. Every fusion tick: `Update(now, healthShareA, healthShareB)`; out of the arena
   (`InArena(center, pos)` false) or dismissed -> `Fled(owner)`.
4. Hostility: only during `Fighting`, the two companions must be hostile to each other and to
   nothing else. Add a fusion check next to the player-duel cases in `WorldObject::GetReactionTo`
   (`Objects/Object.cpp:3608`, the `m_duel->opponent` case ~3651) and
   `WorldObject::IsValidAttackTarget` (`Objects/Object.cpp:3745`, ~3799): two companions in the
   same fighting companion duel are `REP_HOSTILE` / valid targets. Order both to attack each other
   when the fight starts.
5. No deaths: in `Unit::DealDamage` (`Objects/Unit.cpp:662`), like the player-duel clamp, if the
   victim is a duelling companion and the hit would kill, leave it at 1 health and call
   `Beaten(victim owner)`.
6. Owners stay out: in `OnDamage`, an owner damaging the other owner or either companion ->
   `Interfered(owner)`.
7. Finished: stop both companions' combat, restore their own-level stats (health stays as it is),
   send each owner `kOpDuel` with `winner` from that reader's side (0 = you), and
   `GainXp(winnerState, DuelWinXp(winnerLevel, loserLevel), owner level)` for the winner.
   Suggested anti-farm on top: duel xp at most 5 times a day per pair of players.

## 8. Packets (channel 4, little-endian; encoders/decoders and tests in the library)

`SMSG_FUSION_CHANNEL` / `CMSG_FUSION_CHANNEL` with channel byte `kChannelCompanion` = 4; the body's
first byte is the op.

| Op | Dir | Body | Meaning |
|---|---|---|---|
| 1 `kOpBar` | S->C | 27 bytes: item guid u64, species u16, stage u8, level u8, effective level u8, xp u32, xp to next u32, flags u8 (`BAR_*`), evolve block u8 (`EvolveBlock`), next evolve level u8, catalysts needed u8, health % u8 | The companion bar. `MakeBar` fills it. |
| 2 `kOpEvolved` | S->C | 12 bytes: item guid u64, species u16, stage u8 | Play the evolution effect. |
| 3 `kOpDuel` | S->C | 12 bytes: phase u8, end u8, winner i8 (0 = you, 1 = them, -1 none), opponent guid u64 | Duel prompt / countdown / result. |
| 10 `kOpSummon` | C->S | item guid u64 | Summon, or dismiss if that one is out. |
| 11 `kOpDismiss` | C->S | - | Dismiss. |
| 12 `kOpEvolve` | C->S | item guid u64 | Try to evolve. |
| 13 `kOpDuelChallenge` | C->S | target player guid u64 | Challenge. |
| 14 `kOpDuelAnswer` | C->S | accept u8 | Accept (1) / decline (0). |
| 15 `kOpDuelForfeit` | C->S | - | Give up. |
| 16 `kOpRide` | C->S | - | Mount the companion out (a rideable stage), or get off it. |

Bar flags 0x20 `BAR_RIDEABLE` (its stage is a mount), 0x40 `BAR_CAN_RIDE` (`CanRide` passes, or
it is being ridden), 0x80 `BAR_RIDING` are filled by the server (`RideFlags`). While ridden the bar
keeps its item guid with `BAR_RIDING` and without `BAR_SUMMONED`.

## 8b. Mounts (every mount is a companion's last form)

- **Data.** A stage can be `rideable`, with two vanilla mount spells (`mountSpell` 60%,
  `swiftMountSpell` 100%). SQL: `ride_stage` (1-based), `ride_spell`, `ride_swift_spell` on
  `fusion_companion_species`. The rideable stage's creature is the riding creature those spells
  show (their `SPELL_AURA_MOUNTED` misc value, read from the world dump's `spell_template`), so
  the companion walking beside you looks exactly like what you ride. Deviation from "store the
  mount display id": the spell id is stored instead, because the spell brings the display, the
  speed aura and vanilla's own rules (no casting or firing mounted, knocked off by daze etc.).
- **Families.** Wolf Pup -> Dire Wolf (565) -> war wolf (358, Timber Wolf 580 / Swift Brown Wolf
  23250); Nightsaber Cub -> Nightsaber -> 7690 (Striped Nightsaber 10793 / Swift Mistsaber 23219);
  Frostsaber Cub -> Frostsaber -> 6074 (8394 / Swift Frostsaber 23221); Raptor Hatchling ->
  Scythemaw -> 7708 (Violet Raptor 10799 / Swift Blue Raptor 23241); new: Kodo Calf (2972 -> 3236
  -> 11689, Brown Kodo 18990 / Great Brown Kodo 23249), Foal (385 -> 284, Brown Horse 458 / Swift
  Brown Steed 23229), Mechanostrider Runt (7749 small -> 7749, 10969 / 23223), Skeletal Foal
  (11153 small -> 11153, Red Skeletal Horse 17462 / Green Skeletal Warhorse 17465), Ram Kid (2098
  -> 4710, Gray Ram 6777 / Swift Gray Ram 23239). Vanilla has no tallstrider mount, so the
  Plainstrider Chick stays unridden; Panther Cub too (the only black-cat mount is a 100% one).
- **Riding.** `kOpRide` (the bar's Ride button): `CanRide` wants the companion out, no combat or
  companion duel, level 40 and 75 in any vanilla riding skill (60%; level 60 and 150 for the
  swift spell), and at least 35% health (`kRideMinHealth`): a worn companion carries nobody
  until it heals; a fainted one can't be summoned, so it carries nobody until it is revived. Any riding skill counts (deviation: vanilla
  ties a mount to its race's skill; here the companion is the mount). Admins ride at 150.
  Riding unsummons the guardian, remembers its health share and casts the mount spell
  (triggered). Getting off, by the button, a right-click on the aura, or being knocked off,
  puts it back on foot at its owner's side within half a second, at the health it had, attacking
  what its owner fights.
- **Mount items.** `fusion_companion_item` (and `StarterItems()`): items 90060-90068 start their
  species at the rideable stage, level 40 (`FreshState`), Bind on Equip, 80 gold. Vanilla's mount
  vendors are untouched in this pass. To sell these instead: in `npc_vendor`, for each mount vendor
  (e.g. 3362 Ogunaro Wolfrunner, 4730 Lelanai, 4731 Zachariah Post, 7952 Zjolnir, 3685 Harb
  Clawhoof, 384 Katie Hunter, 7955 Milli Featherwhistle, 1261 Veron Amberstill), replace the
  vanilla reins' `item` with the matching 9006x entry (or add it); the race-locked faction
  requirements of those vendors stay as they are.

The client's species names, families and stage creatures come from the same species table (ship
the rows with the client, or add an op that sends them). The client needs: a companion bar
(portrait, level / effective level, xp bar with a "capped" look, stage pips, an Evolve button that
shows `block` as its tooltip, health), the item tooltip line "Level N <species>, stage S" (ask the
server for the state of an item guid, or include it in the bar for items in the bags), and the duel
prompt and result banner.

## 9. Starter data notes

All creature entries were checked against the vmangos world dump (`creature_template`,
`patch` <= 10). Entries worth a look in game, because the model or color may not be what the
name promises:

- 13976 Tortured Drake (red whelp's adult; patch 4, red coloring assumed).
- 742 Green Wyrmkin (emerald line's last stage; a dragonspawn-like model, not a drake).
- 10660 Cobalt Broodling (azure line's last stage; larger whelp rather than a drake).
- 17003 Cinder Elemental (patch 9), 15186 Murky (patch 6), 15429 Disgusting Oozeling and 12387
  Large Vile Slime (patch 7), 2736 Greater Rock Elemental (patch 1): later-patch entries, present
  on a 1.12 realm.
- Scales below 1 shrink adult models used as babies (e.g. 683 Young Panther at 0.6) and keep the
  big final forms (Black Drake 0.6, Infernal 0.8) companion-sized; tune by eye.

Area ids are `area_template` entries: e.g. 25 Blackrock Mountain, 46 Burning Steppes, 51 Searing
Gorge, 511 Wyrmbog, 1037 Grim Batol, 856 Twilight Grove, 2245 Mazthoril, 537 Fire Plume Ridge,
1227 Bay of Storms, 3 Badlands, 1377 Silithus, 4 Blasted Lands, 493 Moonglade, 618 Winterspring.
A rule's areas match either the player's zone id or area id.
