# Guns against classic combat: the numbers, limited ammo, and what changed

Branch `classic-vs-guns`, 2026-10-26. The owner's brief: each class's abilities should give gunplay
a run for its money; limited ammunition should be the great balancer between modern and classic
combat; and the classic spell and melee systems should get more depth.

This document has four parts. Part 1 gives the model's numbers and what they show. Part 2 covers
the limited-ammo model, now built. Part 3 covers the classic-combat changes, also built. Part 4
lists what is left. The full tables the model prints are in the appendix.

## Summary

- **Guns win the first second of any fight by a wide margin.** At level 60 an AK-47 with Stopping
  Power kills a normal creature in 0.34 s and an equal player in 1.1 s, assuming a realistic 70% and
  45% of rounds hit. The level barely matters, because guns and health pools scale together.
- **Melee archetypes already compete.** Berserker, Crusader, Slayer, Assassin, Duelist and
  Earthbreaker kill a normal creature in 1.0-1.8 s and an equal player in 1.5-2.4 s once in reach.
  A two-handed swing at 60 hits for 360 on the CoD scale. They never use ammunition, and their
  risk is closing the distance.
- **Casters were 10-30 times behind.** Before this pass a level 60 Wizard needed 10 s for a normal
  creature and 5.6 s for an equal player, and healer-archetype basic spells needed 15-28 s. The
  catalog's %EH numbers assumed a magazine of 100-150 %EH (open question 4). A real rifle magazine
  is about 600 %EH against a player (30 rounds at 19.8%) and about 1,000% of a normal creature.
- **Ammunition was effectively unlimited.** Talking to any trainer or innkeeper refilled every gun
  for free. Ten quiet seconds trickled the reserve back to half at 12 rounds a second, and every
  respawn gave a full load. Ammo could not balance anything.
- **What changed:**
  - **Limited ammunition.** A load comes back free at a trainer or innkeeper (the trip is the cost;
    an earlier pass sold rounds by the copper, dropped 2026-10-26 after the owner's playtest).
    Gunners' and players' bodies can be scavenged, and a dungeon boss's armory restocks everyone
    who fought it. Out of combat the reserve trickles back to one magazine at half a round a second.
  - **More classic combat.** Ranged class spells now deal twice their damage. Spells can be held to
    a **full charge**, and melee builds **3-hit combos** with flank and back bonuses. A raised shield
    blocks frontal bullets and blows, with a **perfect-block parry** if raised in time. Class wards
    soak bullets 1.5 times better.
- **After the changes**, a level 60 Wizard kills an equal player in 4.2 s (was 5.6), Shadow in
  3.2 s (was 9.1), Elementalist in 2.9 s (was 9.4), and healer archetypes in 7 s (was 15).
  Guns still burst hardest. But a dungeon's trash now needs 2.3 rifle loads per player with no
  vendor in reach, so classes carry long fights, as the brief asked.

- **The class identity pass (part 5, 2026-10-28):**
  - A duel model shows that 10 of 27 archetypes could not beat an equal gunner without a gun:
    the hunters, the healers, the Afflicter, the Starcaller and the Earthbreaker.
  - Every caster's best questing play was the gun.
  - After the pass, every archetype wins the 25 yd duel with its own kit, and every one clears a
    normal creature in 5.5 s or less (was up to 11).
  - The rifle still wins at 40 yd against walking melee kits, and kills creatures 3-15 times
    faster while its ammunition lasts.
  - Every number of the pass is in `fusion/ClassTuning.h`.

## 1. The model

The model is in `server/src/fusion/include/fusion/Balance.h` and `src/Balance.cpp`, and its tests
are in `tests/ClassicVsGunsTests.cpp`. It uses the game's own rules and data:

- **Guns:**
  - The exported MW2 profiles in `contrib/fusion/weapons.json`.
  - Falloff stretched by `FalloffRangeScale` (`CodDamageAtRange`).
  - The level multiplier (1x at level 1, 3x at 60; guns ignore gear).
  - Stopping Power (1.4x) as the typical perk, and extended magazines (1.5x magazine) for rifles.
  - Fusion.Damage.PvE 1.0 / PvP 0.4, and a mid-gear armor cut of 15% against players.
  - A shotgun's reload under a second counts per shell.
- **Targets:**
  - An equal player's pool (`Scale::PlayerMaxHealth`: 100 at 1, 300 at 60).
  - A normal open-world creature with the danger table on it (`Danger::CreatureMaxHealth`: 500
    at 60, x2).
- **Melee:**
  - `Melee::ProfileFor` swing times and bases, a level-typical weapon (weapon factor 1), and
    M = L x G with G = 1 (fresh gear).
- **Class abilities:** the catalog's %EH numbers and cooldowns per archetype, with what each
  level has unlocked (tiers 10-60).
- **Hands:**
  - Busy for the hands' charge time (instant 0.3 s, quick 0.45, medium 0.6, heavy 0.8) or a
    channel's length.
  - Basic spells cost no mana and have a 2 s cooldown.
  - Mana and rage are token costs in the catalog, so they never bind. Cooldowns and charge times do.
- **Accuracy:** 70% of rounds hit a creature and 45% hit a player, with 10% of hits on the head.
  Free-aimed spells land 85% (a near miss still splashes) and blows 90%.
- **Rotation choice:** each move is picked greedily by its total damage; damage over time refreshes
  only when it is about to run out. The "after" columns add combos, smart full charges (casters
  hold a charge only when it delays nothing better) and the ranged-spell power.

What it leaves out:

- Movement and closing distance (melee time-to-kill assumes the target is already in reach).
- Crowds (splash, Whirlwind, Chain Lightning hit one target here).
- Crowd control, healing, and what the defensive answers save.
- Spell power from gear (casters will gain with gear; guns will not).

All of it is single-target damage. The answers to guns are listed per archetype in the tables.
To regenerate the tables, run `FUSION_BALANCE_REPORT=<file> fusion_tests`.

### Level 60 headline (deepened = after this pass)

The rifle is an AK-47 with Stopping Power, 25 yd. Times are seconds to kill one target from full.

| | Normal creature | Equal player |
|---|---|---|
| AK-47 | 0.34 | 1.11 |
| LMG (M240) | 0.27 | 0.73 |
| Shotgun (M1014, 7 yd) | 0.30 | 0.90 |
| Sniper (M21, 60 yd) | 0.40 | 1.20 |

| Archetype | Creature before | Creature after | Player before | Player after |
|---|---|---|---|---|
| Warrior Berserker | 1.6 | 1.6 | 1.6 | 1.6 |
| Warrior Vanguard | 1.5 | 1.5 | 3.3 | 2.7 |
| Warrior Warlord | 1.6 | 1.6 | 2.4 | 2.4 |
| Paladin Crusader | 1.1 | 1.1 | 2.3 | 1.1 |
| Paladin Protector | 1.5 | 1.5 | 3.1 | 2.3 |
| Paladin Lightwarden | 1.5 | 1.5 | 2.3 | 1.5 |
| Hunter Scout | 1.4 | 1.4 | 2.8 | 2.8 |
| Hunter Trapper | 1.7 | 1.7 | 3.1 | 2.4 |
| Hunter Slayer | 1.0 | 1.0 | 2.4 | 1.7 |
| Rogue Assassin | 1.1 | 1.1 | 1.5 | 1.5 |
| Rogue Trickster | 2.3 | 2.1 | 3.5 | 3.0 |
| Rogue Duelist | 1.8 | 1.8 | 2.4 | 1.8 |
| Priest Shadow | 14.8 | 6.7 | 9.1 | 3.2 |
| Priest Cleric | 26.6 | 11.2 | 14.6 | 7.2 |
| Priest Oracle | 26.6 | 11.2 | 14.6 | 7.2 |
| Shaman Elementalist | 17.4 | 6.9 | 9.4 | 2.9 |
| Shaman Earthbreaker | 1.7 | 1.7 | 1.7 | 1.7 |
| Shaman Totemist | 26.4 | 10.9 | 14.5 | 6.9 |
| Mage Wizard | 10.0 | 5.2 | 5.6 | 4.2 |
| Mage Spellsword | 2.7 | 1.5 | 2.7 | 1.5 |
| Mage Artificer | 18.6 | 7.2 | 10.6 | 5.2 |
| Warlock Afflicter | 18.9 | 10.0 | 10.9 | 8.0 |
| Warlock Felsworn | 15.1 | 6.4 | 7.1 | 4.4 |
| Warlock Necromancer | 17.1 | 8.2 | 9.1 | 4.2 |
| Druid Shapeshifter | 3.6 | 3.1 | 5.6 | 4.6 |
| Druid Grovekeeper | 28.6 | 11.2 | 16.6 | 7.2 |
| Druid Starcaller | 18.1 | 10.2 | 11.5 | 8.7 |

Levels 10 and 30 look the same, apart from which tier options a level has. The appendix has every
table. The rifle's time-to-kill is 0.27 / 0.40 / 0.34 s on creatures and 0.87 / 0.80 / 1.11 s on
players at 10 / 30 / 60. The classes' numbers barely move with level, because %EH scales by itself.

### Sustain: what runs out

| Resource | Gun (AK-47, level 60) | Classic |
|---|---|---|
| Per "magazine" | 30 rounds: 7 normal creatures or 2.2 players, 2.6 s reload | none: cooldowns (2-60 s) and the hands' charge |
| Per load | 150 rounds: 35 normal creatures, 6.6 open-world elites, 3.6 dungeon elites, 0.18 of a dungeon boss | unlimited (basic spells cost no mana; catalog costs are token; rage comes from fighting) |
| Back in the field | 1 magazine, at half a round a second after 10 s out of combat (limited model) | always |
| Restock | a kit station (trainer or innkeeper), free; scavenging (Scavenger doubles a body's), Scavenger's magazine a kill, Field Quartermaster's field rearm every 10 min, Packmule, a dungeon boss's armory | none needed |

### Long engagements (level 60, no restock)

| Engagement | Rifle, endless ammo | Kills one load covers | Rifle until dry, then the class | Class alone | Rounds needed |
|---|---|---|---|---|---|
| 30 normal creatures, solo | 10 s | 35 | 10 s | 63 s | 129 |
| 40 dungeon elites, a fifth of each per player | 27 s | 17.5 | 101 s | 156 s | 343 (2.3 loads) |

**What this shows.** In solo open-world play, one load covers about 35 normal kills, so limited
ammo means a trip to an innkeeper or trainer roughly every 2-3 quests, or mixing in the class. In a
dungeon a player runs dry halfway through the trash with no vendor near. From there the classes
carry the run, and each boss's armory refills the guns for the next pull. That is the intended
balance: guns for bursts, classes for staying power.

## 2. Limited ammunition (built)

All of it lives in `fusion/Ammo.h` (`AmmoRules`) and is set from `Fusion.Ammo.*` in `mangosd.conf`.
Setting `Fusion.Ammo.Limited = 0` restores the old model (half the reserve back in the field, a full
load at respawn, no scavenging).

| Rule | Limited (default) | Old model | Why |
|---|---|---|---|
| Talking to a trainer or innkeeper | refills every magazine, reserve and piece of equipment, free | same | the trip back to town is the cost, not the purse (the per-round price was dropped after the 2026-10-26 playtest) |
| Rearm (gossip, or the Rearm ability resting in an inn or city) | the same free refill, plus the special rounds for the quartermaster fee when it can be paid | same | specials stay a small one-stop cost |
| Rearm ability anywhere else | only with Field Quartermaster (One Man Army): a free full refill, once every 10 minutes (Pro: 5) | same | the perk's answer to running dry far from town |
| Field trickle | after 10 s out of combat, back to `FieldFloorMags` (1) magazines at `FieldRoundsPerSecond` (0.5) | to half the max reserve at 10% of it a second | never stranded with an empty gun, never a free refill |
| Respawn | magazines full, reserve topped up to `RespawnReserveShare` (50%) | full load | dying should not be the cheapest restock |
| A gunner NPC killed | the gun in hand takes `GunnerScavengeMags` (0.5) magazines; an elite `EliteScavengeScale` (3x) times that; Scavenger `ScavengerBodyScale` (2x) times that | nothing | fighting gunners pays for itself in part; a reason to loot the Defias |
| An enemy player killed | `PlayerScavengeMags` (1) magazine (2 with Scavenger) | nothing | PvP feeds itself |
| A boss killed in an instance | everyone who fought it: every gun topped up to `BossRestockShare` (100%) | nothing | dungeons have no vendor; trash is the classes' to carry, bosses are where guns shine |
| Scavenger perk (Gravedigger's Bandolier) | a magazine for every gun on every XP kill, and twice a body's rounds | same | with free town refills, the perk that keeps a gun kit going between trips |
| Field Quartermaster perk (One Man Army) | kits switch in combat, and the Rearm ability works in the field every 10 minutes (Pro 5) | same | a full load far from town, on a clock |
| Packmule companion | a magazine every 30 s (20 s Quartermaster) in a fight, 10 s out of one (unchanged) | same | see next steps |
| GMs (`Fusion.Admin.MinSecurity`) | restock free | free | testing |

**Code:**

- `GunAmmo::FieldTarget`, `RespawnResupply`, `TopUp`, `MissingRounds`, `Restock`, `ScavengeMags`.
- `BodyScavengeMags` (Scavenger's share), `Perks.h` `FieldRearmReady` / `FieldRearmCooldownMs`.
- In the game, `FusionMgr::RefillAmmo`, `RefillEquipment`, `RespawnAmmo` and `ScavengeRounds`, plus
  the station gossip (a free refill), the Rearm ability (resting, or Field Quartermaster's field
  rearm), the respawn and the kill hooks (gunner, player, instance boss).

## 3. Deeper classic combat (built)

Each of these is a pure rule in the fusion library with its own tests, plus a small hook in the game.

1. **Ranged spells hit twice as hard** (`Classes::RangedSpellScale`, `Fusion.Classes.RangedSpellPower`
   = 2.0).
   - What it covers: any class spell that reaches past 8 yd (bolts, shocks, damage over time,
     ground areas), against anyone. Melee-range strikes and areas centred on the caster keep their
     damage, because the blade already trades with a gun up close.
   - Where: `FusionClasses::AdjustDamage`.
   - Why: this is the one change that fixes the caster gap at its source, the %EH table being tuned
     against a magazine a quarter of the real one. A per-spell retune of the catalog is the
     long-term fix, and this knob is the stopgap.
2. **Full charge** (`Spellcasting::kFullChargeFactor` 2.75, `kFullChargePower` 2.25, `CAST_FULL`).
   - How it plays: hold the button for 2.75 times the charge and the release hits 2.25x. A dual
     cast of two full charges is capped at 2.8x. Let go once charged and it is a tap, as before.
     Each hand charges and strikes apart: a release's power rides its own bolt, zone and direct
     hits (`FusionHandBolt::power`, `FusionClassState::releasePower`), never the other hand's.
   - Client: sets the flag and the hand's glow keeps growing and pulses harder at full
     (`spellcast::full_charge`, `hands.rs`).
   - Server: holds the hand to the time (`FullChargeServed`) and carries the power on the spell's
     hits like a dual cast (`fullWindows` beside `dualWindows`). It is never wider than a tap.
   - Why: it gives the casting rhythm a decision (tap to keep pressure, hold when the cooldown
     leaves the time) and spends the dead time between 2 s cooldowns.
3. **Melee combos and positioning** (`Melee::ComboHit`, `PositionalMultiplier`, used in
   `FusionMgr::Strike`).
   - Combos: a blow landing within 450 ms after the weapon recovers continues the chain, giving
     1.0x, then 1.1x, then a 1.5x finisher (1.2x on average). A whiff or a pause breaks it.
   - Position: blows from the flank (more than 60 degrees off the target's facing) deal 1.1x, from
     behind (more than 120) 1.25x.
   - Why: rhythm over mashing, and a reason to circle a gunner rather than trade face to face.
4. **The raised shield** (`Melee::Guard`, `GuardBlocks`, `BlockMultiplier`, in
   `FusionClasses::AdjustTaken`).
   - Who: any class with a shield in the off hand, in the Classic layer, holding the right button
     (the client already sends that as `STATE_AIMING` and draws the block).
   - What it stops: half of bullet damage and 75% of blow damage from within 60 degrees of the
     facing.
   - The parry: for 300 ms after the shield comes up it stops everything from in front. The parry
     is armed only by a raise 1.2 s after the last one, so flicking the shield up and down is no
     wall.
   - Bulwark is unchanged and still the tank's full frontal stop.
   - Why: warriors, paladins and shamans with a shield get a skill answer to guns without a kit
     slot.
5. **Wards soak bullets better** (`Classes::WardAbsorb`, `kWardBulletEfficiency` 1.5).
   - What it covers: every class absorb (Mana Shield, Holy Shield, Rallying Cry, the bear's
     momentum, Grand Array, Premonition). Each point stops 1.5 points of a bullet, while spells
     and blows go one for one.
   - Why: "wards that absorb bullets" become a real counter to a magazine.

### Each class's answer to guns (now)

| Class | Burst answer | Sustain answer | Defensive answer |
|---|---|---|---|
| Warrior | Berserker swings and combos: kills a player in 1.6 s once in reach; Charge and Intercept close the distance | no ammo | Frenzy (25% fewer bullets), Bulwark (frontal stop), raised shield and parry, Unbreakable dome |
| Paladin | Crusader 1.1 s; Judgement and Exorcism now double from range | no ammo | Holy Shield (bullet ward, 1.5x), Bastion dome, raised shield |
| Hunter | Slayer 1.7 s (Bane); Scout and Trapper are the gunner's support by design | traps | Disengage, Hex Ward, Camouflage |
| Rogue | Assassin's opening 1.5 s; Duelist 1.8 s with Momentum and combos; flanking and backstab bonuses | no ammo | Stealth, Vanish, Slip, Relentless (15% fewer bullets) |
| Priest | Shadow 3.2 s (was 9.1) | DoTs and full charges | Power Word: Shield (bullet ward), Barrier wall, Shadowform |
| Shaman | Elementalist 2.9 s (was 9.4); Earthbreaker 1.7 s | no ammo | Stoneskin, raised shield |
| Mage | Wizard 4.2 s, Spellsword 1.5 s | no ammo, Spellweave | Mana Shield (bullet ward), Warding Rune wall, Ice Block, Blink |
| Warlock | Felsworn and Necromancer 4.2-4.4 s | Drain Life heals | Fel Hide, minions draw fire |
| Druid | Shapeshifter 4.6 s; Starcaller 8.7 s | forms | Bear's absorb (bullet ward), Tree of Life |

## 4. Next steps (not built)

1. **Retune the catalog's %EH against the real magazine** and retire the 2.0 ranged-spell knob.
   Healer and support basic spells (Smite, Wrath, the Totemist's Lightning Bolt) are still 7 s
   against a player. Starcaller (8.7 s) and Afflicter (8.0 s) are the weakest damage archetypes.
   Their damage over time and channels would be the place to start.
2. **The shield block is inferred from the right button.** A spell readied into the left hand over
   a shield also sends `STATE_AIMING`, so charging it counts as raising the shield. A dedicated
   client bit (the state flags are full; a new message or a byte in op 41) would close that.
3. **Feedback:**
   - Show the combo step and the finisher in the HUD and the swing's sound.
   - Show "Parried" on a perfect block.
   - Give the full-charge glow its own colour; the server sends nothing back for these today.
4. **Reflect projectiles:** a Spellward or Mage option that turns a frontal bullet back at the
   shooter for a short window. It needs the shot pipeline to fire a round from the victim.
5. **Vendors as quartermasters:** ammo and general-goods vendors (who have no gossip today) could
   sell rounds through the vendor window as an item. The current Rearm lives on kit stations only.
6. **Packmule out of combat** (a magazine every 10 s) undercuts the field trickle. Consider 30 s.
7. **Ammo as an item** (bought, traded, looted from gunner loot tables) would make scavenging
   visible. The current model keeps rounds as a per-gun counter for simplicity.
8. **Measure in play:** the 70% / 45% accuracy assumptions and the 35-kills-a-load figure want a
   playtest with the packet log, and the model's assumptions are knobs (`Balance::Assumptions`)
   to refit.

## 5. The class identity pass (branch `class-identity`, 2026-10-28)

The owner's brief: every class and spec should have its own effective way to fight that makes a
gun one option, not the only one. This part audits each archetype as the code plays it, measures
it against an equal gunner, and lists what changed.

### 5.1 How it was measured

- **The duel model.** `Balance::Duel` in `fusion/Balance.h`. It is deterministic, steps every
  50 ms, and its tests are in `tests/ClassIdentityTests.cpp`.
  - The gunner holds the level's rifle (an AK-47 with Stopping Power at 60). It hits 45% of its
    rounds on a strafing player.
  - It fires from 0.3 s, or from 0.6 s against a class that opens from stealth. It stops firing
    while it is stunned, feared, sheeped or disarmed. That is the game's rule: `Fusion::CanFire`
    and `FusionClasses::BlocksFire`.
  - The class uses its own deepened rotation (part 1), closes its kit's gap and spends its
    control on cooldown. Casts take time, and diminishing returns halve each control within 18 s
    of the last one. It soaks with its ward and heals as its kit does.
  - Both sides have an equal player's pool. Downed states are left out.
  - The duel runs at 25 yd and at 40 yd. At 40 yd, melee kits start out of Charge's reach.
- **Each archetype's duel kit is the audit.** `FillIdentity` in `Balance.cpp` records each kit's:
  - gap-closer and its reach;
  - control and how long it holds a player;
  - ward;
  - heals a second, and the share of damage it drains back;
  - bullet cut, with its usual uptime.

  The numbers come from the effects in `FusionClassAbilities.cpp` and the catalog.
- **The tuning is data in one place.** Every number below is in `fusion/ClassTuning.h` and
  `src/ClassTuning.cpp`. The game reads them in `FusionClasses::IdentityDamage`, `AdjustTaken`,
  `CastOption`, `OnTrapFired`, `Apply`, `HealTakenMultiplier` and `FusionMgr::BuildCompanion`.
  The model reads the same rows by option id, so the simulator and the game cannot drift apart.
- **Regenerate the tables** with `FUSION_DUEL_REPORT=<file> FUSION_BALANCE_REPORT=<file> fusion_tests`.

### 5.2 The audit: each archetype as it played before the pass

"Just shoot" marks the archetypes whose best play, in a fight or in questing, was the gun. Times
are level 60, seconds to kill an equal player once in reach (part 1's deepened model). "Duel"
is the 25 yd duel against the gunner, before the pass.

| Archetype | Rotation, range | Mobility | Control on a gunner | Defensives | Kill time | Duel | Just shoot? |
|---|---|---|---|---|---|---|---|
| Warrior Berserker | swings, Bloodthirst, Whirlwind; melee | Charge 30 yd, Intercept | Disarm 3 s, Intercept stun 2 s | Frenzy, Death Wish, Bloodthirst heals | 1.6 | wins, 97% HP | no |
| Warrior Vanguard | swings, Shield Charge | Charge | Disarm, Chain Hook | Bulwark, raised shield, Shield Wall | 2.7 | wins, 93% | no |
| Warrior Warlord | swings, shouts | Charge | Disarm, Intimidating Shout | Demoralizing Shout, Rallying Cry, banners | 2.4 | wins, 100% | no |
| Paladin Crusader | Crusader Strike, Judgement 20 yd, Exorcism 30 yd | walks in | Hammer of Justice 3 s, Judgement stun, Repentance | Avenging Crusade | 1.1 | wins, 95% | no |
| Paladin Protector | Consecration, Exorcism | walks in | Hammer of Justice | Holy Shield, Bastion | 2.3 | wins, 56% | no |
| Paladin Lightwarden | Holy Shock, Exorcism | walks in | Hammer of Justice | heals | 1.5 | wins, 57% | no |
| Hunter Scout | swings only | Mark Prey's speed | none | Camouflage, Disengage | 2.8 | loses (the gunner on 0%) | **yes**: a gunner's support by design |
| Hunter Trapper | swings, Bear Trap | none | Freezing Trap (breaks on damage) | Disengage | 2.4 | loses (the gunner on 39%) | **yes** |
| Hunter Slayer | swings, Throwing Axes | none | Scatter (breaks) | Hex Ward (not bullets) | 1.7 | loses (the gunner on 20%) | **yes** |
| Rogue Assassin | Ambush, Garrote from stealth | Stealth, Sprint | Cheap Shot 2.5-3.5 s | Vanish | 1.5 | wins, 97% | no |
| Rogue Trickster | poisons | Stealth, Smoke Bomb | smoke breaks sight | Slip | 3.0 | wins, 98% | no |
| Rogue Duelist | Sinister Strike, Lunge | Sprint, Lunge 30 yd | none | Relentless | 1.8 | wins, 9% | close |
| Priest Shadow | Shadow Word: Pain, Mind Blast, Devouring Plague; 40 yd | none | none | Power Word: Shield, Shadowform | 3.2 | wins, 33% | no |
| Priest Cleric | Smite; 30 yd | none | none | Power Word: Shield, heals | 7.2 | loses (the gunner on 67%) | **yes** |
| Priest Oracle | Smite | none | Psychic Scream (8 yd) | Barrier wall | 7.2 | loses (the gunner on 34%) | **yes** |
| Shaman Elementalist | Lightning Bolt, Chain Lightning, Frost Shock; 40 yd | Ghost Wolf | Earthquake | none | 2.9 | wins, 69% | no |
| Shaman Earthbreaker | swings, Stormstrike, Earth Shock, Ground Smash | Ghost Wolf, no closer | Ground Smash in reach | Stoneskin | 1.7 | loses (the gunner on 78%) | **yes** |
| Shaman Totemist | Lightning Bolt | Ghost Wolf | none | Windwall, heals | 6.9 | loses (the gunner on 34%) | **yes** |
| Mage Wizard | Fireball, Frostbolt, Arcane Missiles, Blast Wave | Blink | Polymorph | Ice Block | 4.2 | wins, 22% | no (burst) |
| Mage Spellsword | blade, Blade Blink 20 yd | Blink, Blade Blink | Polymorph | Mana Shield | 1.5 | wins, 60% | no |
| Mage Artificer | Fireball | Blink Pads | Polymorph | Warding Rune wall | 5.2 | wins, 63% | no |
| Warlock Afflicter | Corruption, Drain Life, Shadow Bolt | none | Fear | drains | 8.0 | loses (the gunner on 19%) | **yes** |
| Warlock Felsworn | Shadow Bolt, Hellfire, Death Coil | Fel Leap | Fear, Death Coil | Fel Hide, Demonic Pact | 4.4 | wins, 58% | no |
| Warlock Necromancer | Corpse Explosion, Shadow Bolt | none | Fear | Bone Armor, minions | 4.2 | wins, 83% | no |
| Druid Shapeshifter | Rake, Maul, forms | Prowl, Travel Form | Pounce, Bash | Barkskin, the bear's absorb | 4.6 | wins, 100% | no |
| Druid Grovekeeper | Wrath | none | Entangling Roots (no gun stop) | heals | 7.2 | loses (the gunner on 69%) | **yes** |
| Druid Starcaller | Moonfire, Wrath, Hurricane | none | none | Lunar Grace | 8.7 | loses (the gunner on 33%) | **yes** |

**What the audit found:**

- **In a duel, hard control is what beats a gun.** Stuns, fears, disarms and sheep stop the
  trigger outright. Every kit with a stun or a disarm in reach already beat the gunner from 25 yd.
  So did every kit that opens from stealth.
- **Ten archetypes had no way to win without a gun.**
  - The hunters: no gap-closer and no control that holds.
  - The healers: Smite and Wrath took 7 s to kill.
  - The Afflicter and Starcaller: damage over time too slow to race a magazine.
  - The Earthbreaker: walked into the rifle with no closer.
- **In PvE, every caster's best play was the gun.**
  - A rifle kills a normal level 60 creature in 0.34 s.
  - Casters needed 5-11 s (healers 11 s, Afflicter 10, Starcaller 10).
  - Melee archetypes need 1-2 s, which is fine next to the walk between pulls.

### 5.3 What changed, class by class

These are the identity rules and tuning rows. Everything is in `ClassTuning.h` and tested.

**Whole-class rules:**

- **Creature spell power** (`kCreatureSpellPower` 1.75): a class spell from range hits a creature
  1.75 times harder, on top of the ranged-spell power. Questing as a caster takes about 3 s a
  creature instead of 5-11.
- **The basic spell by the kit's lead archetype** (`LeadArchetype`, `BasicSpellScale`).
  - Healer-led kits: Smite, Lightning Bolt and Exorcism hit 1.7 times harder, Wrath 1.9 (it
    starts weaker).
  - Shadow's Smite 1.2, Shadow Bolt 1.2, Starcaller's Wrath 1.7, Elementalist's bolt 1.15.
  - The lead is the archetype with the most picks. A tie goes to the archetype with the higher
    tier pick.
- **Homing is a kit feature, never the default** (the owner's earlier direction). Arcane
  Missiles (Wizard) and Moonfire (Starcaller) home while they are in the kit. `FusionClasses::Apply`
  grants it through `GrantHoming`, which was built but never called before.

**Each class:**

- **Warrior: the gap-closer and cleave beat guns up close.**
  - New, **Charging through fire**: Charge, Intercept and Shield Charge take 50% of bullet damage
    for 2.5 s from the cast.
  - Play: Charge, Disarm (3 s with no gun), then Bloodthirst or Whirlwind.
  - Duel: wins at 25 yd and at 40 yd (40 yd: Berserker 45% HP left, was 34%).
- **Paladin: the front-line hybrid.**
  - Judgement 1.25x and Holy Shock 1.5x, so the Crusader and Lightwarden punch from 20-30 yd
    while they walk in under Hammer of Justice.
  - Healer-led Exorcism 1.7x with Atonement.
  - Duel: the Lightwarden now wins from 40 yd (60% HP left, was a loss).
- **Hunter: companion and trap control.**
  - New, **the hunter's bond**: a hunter's battle companion takes the hunter pet's place with
    1.3x health and 1.5x damage, the only class lifted over the companion cap. It bites 30%
    harder on the hunter's Mark Prey.
  - New, **the snap**: any hunter trap that goes off on a player stuns them 1.5 s, so a freeze is
    not wasted on the first hit.
  - Throwing Axes 2x (the Slayer's reach while it closes) and Bear Trap 2x.
  - Duel: Scout, Trapper and Slayer now win at 25 yd (24%, 4% and 12% HP left). At 40 yd the
    rifle still wins against Trapper and Slayer: hunters want prepared ground.
- **Rogue: stealth openers delete a gunner.**
  - New, **Ambush's sundering**: an Ambush that lands on a player disarms them for 2 s.
  - Play: Ambush, then Cheap Shot, and the gun never fires in the opening.
  - The Duelist's Lunge carries Charging through fire, and now wins from 40 yd (21% HP left, was
    a loss).
- **Priest: sustain wins attrition.**
  - New, **Atonement**: a healer-led kit's basic spell heals the caster for 60% of what it dealt
    (as a share of the victim's pool). It does nothing while downed.
  - Shadow: Shadow Word: Pain 1.4x, Mind Blast 1.25x, Devouring Plague 1.4x.
  - Duel: the Cleric and Oracle go from losses to wins (39% and 58% HP left).
- **Shaman: the front-line hybrid.**
  - New, **Ghost Wolf is hard to hit**: 35% fewer bullets in the form, which every shaman has.
    It is the Earthbreaker's way in, and firing breaks it.
  - Earth Shock 1.3x, Frost Shock 1.2x, and the healer-led Lightning Bolt with Atonement.
  - Duel: the Earthbreaker and Totemist now win at 25 yd.
- **Mage: burst and zone control.**
  - New, **Shatter**: a Wizard-led kit's spells hit a chilled, rooted or stunned target 1.5x
    harder. Frostbolt, then Fireball; Frost Nova, then Arcane Missiles.
  - Arcane Missiles home. Blast Wave 1.3x for the close range.
  - The Wizard already won its duel on Polymorph. Its kill time is cast-bound (4.2 s), so the
    gain shows in PvE and in group burst.
- **Warlock: DoT and drain pressure.**
  - Corruption and Drain Life 1.6x, Death Coil 1.3x.
  - New, **drain pressure**: each different Afflicter affliction on a target cuts the heals it
    takes by 15%, up to 45%. It applies to anyone's heals, Atonement included, and is the
    healer-killer.
  - Duel: the Afflicter goes from a loss to a win (47% HP left). Fel Leap carries Charging
    through fire.
- **Druid.**
  - Moonfire 1.8x, and it homes. Hurricane 1.4x. Healer-led Wrath 1.9x with Atonement.
  - Duel: the Grovekeeper and Starcaller go from losses to wins (39% and 19% HP left).

### 5.4 The numbers after the pass

**Level 60, an equal player once in reach (s):**

| Archetype | Before | After |
|---|---|---|
| Priest Cleric / Oracle | 7.2 | 3.2 |
| Shaman Totemist | 6.9 | 2.9 |
| Warlock Afflicter | 8.0 | 4.9 |
| Warlock Felsworn | 4.4 | 2.8 |
| Druid Grovekeeper | 7.2 | 3.2 |
| Druid Starcaller | 8.7 | 2.1 |
| The rifle | 1.1 | 1.1 |

The other archetypes keep their part-1 times (1.1-4.6 s).

**Level 60, a normal creature (s):** the rifle takes 0.34 s.

| Archetype | Before | After |
|---|---|---|
| Healers | 11 | 3 |
| Afflicter | 10 | 4.5 |
| Starcaller | 10 | 2.1 |
| Elementalist | 6.9 | 2.9 |
| Wizard | 5.2 | 4.2 |
| Artificer | 7.2 | 5.2 |
| Necromancer | 8.2 | 4.2 |
| Melee archetypes | 1-3 | 1-3 |

Every archetype now clears a normal creature in 5.5 s or less at levels 30 and 60 (a test).

**The duel at level 60**, an AK-47 gunner, the class without a gun: see the duel table in the
appendix.

- At 25 yd, all 27 archetypes win after the pass. Before it, 10 lost.
- At 40 yd the rifle still wins against walking melee kits with no long closer: the Protector,
  Trapper, Slayer, Earthbreaker and Spellsword. That is a test too
  (`identity_pass_guns_stay_a_strong_option`).
- A rifle still kills creatures 3-15 times faster than any class while its ammunition lasts.

Guns stay a strong option and are no longer the only one.

### 5.5 Integration check

| System | How the pass fits |
|---|---|
| Kits | The lead archetype and homing come from the kit's picks. `Apply` grants and revokes homing on every kit change. A blend of archetypes gets the lead's basic-spell scale only. |
| Glyphs (Inscription) and gems (Jewelcrafting) | They act on stats and on `AdjustDealt` multipliers. The pass multiplies after them (`IdentityDamage` runs after the dual-cast, ranged-power and splash factors), so nothing double-counts and the tuning rows stay relative. |
| Companions | The bond is applied in `BuildCompanion`, so it follows the companion's level, stage and traits and is recomputed at every summon. A companion duel (pet against pet) builds both without the bond (`BuildCompanion(..., bond = false)`), so a hunter has no edge there. Only hunters get it. |
| Skyriding and mounted combat | Gap-closers cannot be cast mounted. Ghost Wolf is a form, not a mount. Atonement and Shatter act on spells only, so a rider's gun is unaffected. |
| PvP | Ambush's disarm and the trap's snap act on players only. Atonement uses the victim's pool share, so CoD and WoW pools count alike. Drain pressure acts on anyone healed. |
| Downed state | Atonement never heals a downed caster. Drain pressure cuts heals, not the stand-up (which sets health). A downed player cannot cast the basic spell anyway. |
| Triggered spells | Holy Shock's smite, Hurricane's pulses and Arcane Missiles' missiles are other spell ids. `DamageOptionOf` maps the spells an option sets off (`EffectTriggerSpell`) to it, so their rows apply. |

### 5.6 What to try in game

1. **Warrior.** Charge an AK-47 bot from 25 yd and Disarm on arrival. The bullets during the
   charge should hurt half as much (watch the combat log for 2.5 s after the cast).
2. **Rogue Assassin.** Ambush a player from stealth. They should be disarmed for 2 s (no shots),
   then Cheap Shot.
3. **Hunter.** Lay a Freezing Trap and lure a gunner over it. They should be stunned 1.5 s
   first, then frozen. A hunter's companion should have more health than the same companion on
   a mage, and hit harder on the Mark Prey.
4. **Priest Cleric.** Smite a gunner. Each Smite should heal you about 60% of the share it took.
5. **Mage Wizard.** Frostbolt, then Fireball: the Fireball hits half again harder. With Arcane
   Missiles in the kit, the missiles curve to a strafing target.
6. **Shaman.** Close on a rifle in Ghost Wolf: about a third fewer bullets. Firing breaks it.
7. **Warlock Afflicter.** Put Corruption and Curse of Exhaustion on a healer. Their heals should
   be 30% weaker.
8. **Questing as a caster.** A normal creature should take about 3 s with no gun.
9. **Tooltips.** Each tuned option's talent tooltip shows its identity line under the
   description. Examples: "Charging through fire...", "Hits 1.6x as hard...", "Its bolts home...".

### 5.7 Open questions for the owner

1. **Hunter pets.** The catalog removed beasts. The pass makes the battle companion the hunter's
   pet (the bond). Is that the intended "hunter pet", or should hunters get real tamed pets back?
2. **Warriors, Assassins and Shapeshifters win the 25 yd duel with 95-100% health.** Disarm,
   Cheap Shot and Pounce stop the gun outright. Should hard control against a gun be shortened
   on players, for example Disarm 3 s to 2 s? The model has the knob (`Control::seconds`).
3. **Diminishing returns.** The trap's snap and Ambush's disarm use a timed aura that bypasses
   vanilla's diminishing returns. Fine for single uses. Each trap a player steps on snaps, but
   the traps it chains do not. A Killing Field ring can still snap more than once, so say if
   chained control feels oppressive.
4. **The 40 yd fight.** Walking melee kits lose to a rifle at 40 yd by design. Should the
   Protector and the Earthbreaker get a long closer? Candidates: a 30 yd Holy Charge, and a
   spirit-wolf pounce out of Ghost Wolf.
5. **The model leaves out:** casters' range limits at 40 yd, strafing, line of sight, groups, and
   gear spell power (casters gain from gear, guns do not). Playtests with the packet log should
   refit the knobs in `Balance::Assumptions` and the rows in `ClassTuning.cpp`.
6. **Vanilla spell tooltips** still show the stock numbers. The identity line says "Hits Nx as
   hard as its spell says". Should the client scale the numbers inside the spell text instead?

## Appendix: the model's tables

Generated by `FUSION_BALANCE_REPORT=<file> fusion_tests` with the defaults above. "Hit %" is one
round (all its pellets) as a share of the pool. "Burst" is while the trigger is down; "Sustained"
includes reloads. The class columns are % of the pool a second, seconds to kill, the share dealt in
the first 5 s, the same after part 3 ("Deepened"), and the kill time after part 5 ("Identity
pass"). The duel tables are `FUSION_DUEL_REPORT=<file> fusion_tests` (part 5).

#### PvE: a normal creature of the level

**Level 10** (pool 224)

| Gun (Stopping Power) | Hit % | Rounds/kill | TTK s | Burst %/s | Sustained %/s | Kills/mag | Kills/load | Trigger s/load |
|---|---|---|---|---|---|---|---|---|
| AR (famas) | 34.0 | 4.3 | 0.27 | 356 | 155 | 7.0 | 35.0 | 20.4 |
| AR +ext mags (famas) | 34.0 | 4.3 | 0.27 | 356 | 191 | 10.5 | 38.5 | 18.8 |
| SMG (ump45) | 34.0 | 4.3 | 0.36 | 265 | 150 | 7.5 | 37.3 | 23.1 |
| LMG (sa80) | 34.0 | 4.3 | 0.32 | 297 | 164 | 23.3 | 70.0 | 37.0 |
| Sniper (barrett) | 84.0 | 2.9 | 0.60 | 196 | 90 | 3.5 | 17.5 | 29.0 |
| Shotgun (spas12) | 260.9 | 1.0 | 0.00 | 213 | 125 | 8.0 | 40.0 | 53.5 |
| Pistol (usp) | 32.9 | 5.7 | 0.75 | 153 | 81 | 2.1 | 10.5 | 15.4 |
| Machine pistol (pp2000) | 25.5 | 5.7 | 0.32 | 282 | 109 | 3.5 | 17.5 | 14.3 |

| Class rotation | Sustained %/s | TTK s | First 5 s % | Deepened %/s | Deepened TTK s | Identity pass TTK s | Answer to guns |
|---|---|---|---|---|---|---|---|
| Warrior Berserker | 62.4 | 1.3 | 324 | 74.4 | 1.3 | 1.3 | Frenzy: 25% fewer bullets, Intercept, Death Wish |
| Warrior Vanguard | 53.0 | 1.8 | 283 | 63.4 | 1.2 | 1.2 | Bulwark: frontal bullets stopped; Unbreakable dome |
| Warrior Warlord | 56.5 | 1.6 | 312 | 67.7 | 1.6 | 1.6 | Banners (Ward: 15% fewer bullets), shouts |
| Paladin Crusader | 59.4 | 2.0 | 326 | 70.8 | 2.0 | 2.0 | Avenging Crusade: 20% fewer bullets |
| Paladin Protector | 54.5 | 2.0 | 271 | 65.5 | 1.2 | 1.2 | Holy Shield absorbs bullets; Bastion dome |
| Paladin Lightwarden | 54.5 | 2.0 | 271 | 65.5 | 1.2 | 1.2 | Heals 20-45% HP; Rekindle |
| Hunter Scout | 52.7 | 1.4 | 294 | 63.0 | 1.4 | 1.4 | Spotting, Camouflage: the gunner's support |
| Hunter Trapper | 52.7 | 1.4 | 294 | 63.0 | 1.4 | 1.4 | Traps and nets: roots, freezes |
| Hunter Slayer | 68.4 | 1.4 | 382 | 81.9 | 0.7 | 0.7 | Bane +30%; Hex Ward; Purge strips shields |
| Rogue Assassin | 44.2 | 1.2 | 394 | 52.7 | 0.8 | 0.8 | Stealth, Vanish: wins the first 4 s |
| Rogue Trickster | 43.1 | 2.5 | 214 | 51.2 | 2.1 | 2.1 | Slip: untargetable 0.5 s an ability; smoke |
| Rogue Duelist | 63.1 | 1.5 | 319 | 75.4 | 1.5 | 1.5 | Momentum +25%; Relentless: 15% fewer bullets |
| Priest Shadow | 4.5 | 22.9 | 26 | 9.7 | 11.2 | 5.2 | Shadowform: 30% less of everything |
| Priest Cleric | 3.8 | 26.6 | 22 | 10.0 | 11.2 | 3.2 | Power Word: Shield, heals |
| Priest Oracle | 3.8 | 26.6 | 22 | 10.0 | 11.2 | 3.2 | Barrier wall stops bullets; Premonition |
| Shaman Elementalist | 3.8 | 26.4 | 22 | 10.0 | 10.9 | 4.9 | Range and slows; Earthquake knock-downs |
| Shaman Earthbreaker | 61.8 | 1.6 | 342 | 74.0 | 0.8 | 0.8 | Stoneskin: 15% fewer bullets |
| Shaman Totemist | 3.8 | 26.4 | 22 | 10.0 | 10.9 | 2.9 | Totems: ground, heals |
| Mage Wizard | 5.1 | 20.6 | 29 | 13.3 | 7.2 | 5.2 | Spellweave +30%; Ice Block, Blink |
| Mage Spellsword | 51.6 | 1.8 | 247 | 65.4 | 1.8 | 1.2 | Mana Shield 25% HP; Arcane Avatar |
| Mage Artificer | 5.1 | 20.6 | 29 | 13.3 | 7.2 | 5.2 | Warding Rune: a bullet wall |
| Warlock Afflicter | 5.7 | 17.1 | 24 | 12.2 | 7.7 | 3.4 | Drains heal; Curse of Doom |
| Warlock Felsworn | 5.1 | 20.8 | 30 | 13.3 | 7.6 | 3.6 | Fel Hide: 20% fewer bullets |
| Warlock Necromancer | 5.1 | 20.8 | 30 | 13.3 | 7.6 | 3.6 | Minions draw fire |
| Druid Shapeshifter | 26.0 | 3.8 | 136 | 31.0 | 3.3 | 3.3 | Forms: Bear's absorb, Cat's stealth |
| Druid Grovekeeper | 3.6 | 28.6 | 21 | 9.3 | 11.2 | 3.2 | Heals, Tree of Life: 20% less damage |
| Druid Starcaller | 5.0 | 20.3 | 31 | 13.1 | 7.8 | 3.8 | Range, Hurricane widens spread |

**Level 30** (pool 347)

| Gun (Stopping Power) | Hit % | Rounds/kill | TTK s | Burst %/s | Sustained %/s | Kills/mag | Kills/load | Trigger s/load |
|---|---|---|---|---|---|---|---|---|
| AR (fal) | 45.7 | 4.3 | 0.40 | 320 | 139 | 4.7 | 23.3 | 20.4 |
| AR +ext mags (fal) | 45.7 | 4.3 | 0.40 | 320 | 171 | 7.0 | 25.7 | 18.8 |
| SMG (p90) | 24.9 | 7.1 | 0.46 | 268 | 139 | 7.0 | 35.0 | 28.3 |
| LMG (mg4) | 33.2 | 5.7 | 0.33 | 349 | 177 | 17.5 | 52.5 | 33.0 |
| Sniper (barrett) | 82.2 | 2.9 | 0.60 | 192 | 89 | 3.5 | 17.5 | 29.0 |
| Shotgun (aa12) | 159.5 | 1.0 | 0.00 | 744 | 223 | 8.0 | 40.0 | 17.2 |
| Pistol (coltanaconda) | 40.7 | 4.3 | 1.33 | 86 | 38 | 1.4 | 7.0 | 20.0 |
| Machine pistol (glock) | 24.9 | 7.1 | 0.38 | 320 | 105 | 2.5 | 12.6 | 12.9 |

| Class rotation | Sustained %/s | TTK s | First 5 s % | Deepened %/s | Deepened TTK s | Identity pass TTK s | Answer to guns |
|---|---|---|---|---|---|---|---|
| Warrior Berserker | 59.8 | 1.6 | 325 | 71.0 | 1.6 | 1.6 | Frenzy: 25% fewer bullets, Intercept, Death Wish |
| Warrior Vanguard | 51.9 | 1.8 | 277 | 62.1 | 1.2 | 1.2 | Bulwark: frontal bullets stopped; Unbreakable dome |
| Warrior Warlord | 55.3 | 1.6 | 306 | 66.2 | 1.6 | 1.6 | Banners (Ward: 15% fewer bullets), shouts |
| Paladin Crusader | 58.2 | 2.3 | 331 | 69.8 | 1.1 | 1.1 | Avenging Crusade: 20% fewer bullets |
| Paladin Protector | 53.3 | 2.3 | 274 | 64.2 | 1.5 | 1.5 | Holy Shield absorbs bullets; Bastion dome |
| Paladin Lightwarden | 52.7 | 1.5 | 275 | 63.9 | 1.5 | 0.8 | Heals 20-45% HP; Rekindle |
| Hunter Scout | 51.5 | 1.4 | 288 | 61.7 | 1.4 | 1.4 | Spotting, Camouflage: the gunner's support |
| Hunter Trapper | 51.5 | 1.4 | 288 | 61.7 | 1.4 | 1.4 | Traps and nets: roots, freezes |
| Hunter Slayer | 64.8 | 1.7 | 333 | 77.4 | 1.0 | 1.0 | Bane +30%; Hex Ward; Purge strips shields |
| Rogue Assassin | 43.6 | 1.2 | 359 | 51.6 | 1.1 | 1.1 | Stealth, Vanish: wins the first 4 s |
| Rogue Trickster | 42.2 | 2.5 | 209 | 50.1 | 2.1 | 2.1 | Slip: untargetable 0.5 s an ability; smoke |
| Rogue Duelist | 60.5 | 1.8 | 320 | 72.1 | 1.8 | 1.8 | Momentum +25%; Relentless: 15% fewer bullets |
| Priest Shadow | 5.7 | 17.4 | 28 | 12.1 | 8.1 | 2.7 | Shadowform: 30% less of everything |
| Priest Cleric | 3.8 | 26.6 | 22 | 9.8 | 11.2 | 3.2 | Power Word: Shield, heals |
| Priest Oracle | 3.8 | 26.6 | 22 | 9.8 | 11.2 | 3.2 | Barrier wall stops bullets; Premonition |
| Shaman Elementalist | 5.2 | 18.5 | 33 | 12.3 | 8.9 | 2.9 | Range and slows; Earthquake knock-downs |
| Shaman Earthbreaker | 58.0 | 1.4 | 302 | 69.4 | 1.4 | 1.4 | Stoneskin: 15% fewer bullets |
| Shaman Totemist | 3.8 | 26.4 | 22 | 9.8 | 10.9 | 2.9 | Totems: ground, heals |
| Mage Wizard | 10.3 | 10.1 | 40 | 19.9 | 5.6 | 4.2 | Spellweave +30%; Ice Block, Blink |
| Mage Spellsword | 50.0 | 2.7 | 215 | 62.3 | 1.5 | 1.5 | Mana Shield 25% HP; Arcane Avatar |
| Mage Artificer | 5.0 | 20.6 | 29 | 13.0 | 7.3 | 5.2 | Warding Rune: a bullet wall |
| Warlock Afflicter | 5.0 | 19.9 | 19 | 10.5 | 10.0 | 4.9 | Drains heal; Curse of Doom |
| Warlock Felsworn | 5.8 | 17.1 | 31 | 13.6 | 8.2 | 4.2 | Fel Hide: 20% fewer bullets |
| Warlock Necromancer | 5.3 | 19.1 | 29 | 13.2 | 8.2 | 4.2 | Minions draw fire |
| Druid Shapeshifter | 25.2 | 3.6 | 127 | 29.3 | 3.1 | 3.1 | Forms: Bear's absorb, Cat's stealth |
| Druid Grovekeeper | 3.5 | 28.6 | 20 | 9.1 | 11.2 | 3.2 | Heals, Tree of Life: 20% less damage |
| Druid Starcaller | 5.9 | 16.1 | 37 | 13.6 | 7.8 | 2.4 | Range, Hurricane widens spread |

**Level 60** (pool 500)

| Gun (Stopping Power) | Hit % | Rounds/kill | TTK s | Burst %/s | Sustained %/s | Kills/mag | Kills/load | Trigger s/load |
|---|---|---|---|---|---|---|---|---|
| AR (ak47) | 34.9 | 4.3 | 0.34 | 285 | 142 | 7.0 | 35.0 | 23.3 |
| AR +ext mags (ak47) | 34.9 | 4.3 | 0.34 | 285 | 170 | 10.5 | 38.5 | 21.9 |
| SMG (uzi) | 26.2 | 5.7 | 0.27 | 337 | 149 | 5.6 | 28.0 | 17.5 |
| LMG (m240) | 43.7 | 4.3 | 0.27 | 458 | 224 | 23.3 | 70.0 | 34.0 |
| Sniper (m21) | 61.7 | 2.9 | 0.40 | 216 | 86 | 3.5 | 17.5 | 22.0 |
| Shotgun (m1014) | 125.8 | 2.0 | 0.30 | 293 | 110 | 2.0 | 18.0 | 26.8 |
| Pistol (deserteagle) | 43.1 | 4.3 | 0.96 | 126 | 61 | 1.6 | 8.2 | 15.6 |
| Machine pistol (tmp) | 34.9 | 4.3 | 0.22 | 449 | 130 | 3.5 | 17.5 | 12.1 |

| Class rotation | Sustained %/s | TTK s | First 5 s % | Deepened %/s | Deepened TTK s | Identity pass TTK s | Answer to guns |
|---|---|---|---|---|---|---|---|
| Warrior Berserker | 62.9 | 1.6 | 341 | 74.6 | 1.6 | 1.6 | Frenzy: 25% fewer bullets, Intercept, Death Wish |
| Warrior Vanguard | 53.7 | 1.5 | 264 | 64.4 | 1.5 | 1.5 | Bulwark: frontal bullets stopped; Unbreakable dome |
| Warrior Warlord | 58.1 | 1.6 | 321 | 69.6 | 1.6 | 1.6 | Banners (Ward: 15% fewer bullets), shouts |
| Paladin Crusader | 61.1 | 1.1 | 348 | 73.4 | 1.1 | 1.1 | Avenging Crusade: 20% fewer bullets |
| Paladin Protector | 56.0 | 1.5 | 288 | 67.5 | 1.5 | 1.5 | Holy Shield absorbs bullets; Bastion dome |
| Paladin Lightwarden | 55.4 | 1.5 | 289 | 67.2 | 1.5 | 0.8 | Heals 20-45% HP; Rekindle |
| Hunter Scout | 54.2 | 1.4 | 302 | 64.8 | 1.4 | 1.4 | Spotting, Camouflage: the gunner's support |
| Hunter Trapper | 54.2 | 1.7 | 272 | 64.8 | 1.7 | 1.7 | Traps and nets: roots, freezes |
| Hunter Slayer | 68.1 | 1.0 | 350 | 81.4 | 1.0 | 1.0 | Bane +30%; Hex Ward; Purge strips shields |
| Rogue Assassin | 45.9 | 1.1 | 378 | 54.2 | 1.1 | 1.1 | Stealth, Vanish: wins the first 4 s |
| Rogue Trickster | 44.3 | 2.3 | 220 | 52.6 | 2.1 | 2.1 | Slip: untargetable 0.5 s an ability; smoke |
| Rogue Duelist | 63.6 | 1.8 | 336 | 75.8 | 1.8 | 1.8 | Momentum +25%; Relentless: 15% fewer bullets |
| Priest Shadow | 6.4 | 14.8 | 34 | 13.6 | 6.7 | 3.3 | Shadowform: 30% less of everything |
| Priest Cleric | 4.0 | 26.6 | 23 | 10.3 | 11.2 | 3.2 | Power Word: Shield, heals |
| Priest Oracle | 4.0 | 26.6 | 23 | 10.3 | 11.2 | 3.2 | Barrier wall stops bullets; Premonition |
| Shaman Elementalist | 5.4 | 17.4 | 34 | 12.9 | 6.9 | 2.9 | Range and slows; Earthquake knock-downs |
| Shaman Earthbreaker | 60.5 | 1.7 | 325 | 72.5 | 1.7 | 1.7 | Stoneskin: 15% fewer bullets |
| Shaman Totemist | 4.0 | 26.4 | 23 | 10.3 | 10.9 | 2.9 | Totems: ground, heals |
| Mage Wizard | 11.0 | 10.0 | 52 | 21.3 | 5.2 | 4.2 | Spellweave +30%; Ice Block, Blink |
| Mage Spellsword | 52.5 | 2.7 | 226 | 65.5 | 1.5 | 1.5 | Mana Shield 25% HP; Arcane Avatar |
| Mage Artificer | 5.3 | 18.6 | 30 | 13.7 | 7.2 | 5.2 | Warding Rune: a bullet wall |
| Warlock Afflicter | 5.3 | 18.9 | 20 | 11.0 | 10.0 | 4.5 | Drains heal; Curse of Doom |
| Warlock Felsworn | 6.4 | 15.1 | 41 | 14.8 | 6.4 | 2.8 | Fel Hide: 20% fewer bullets |
| Warlock Necromancer | 5.6 | 17.1 | 31 | 13.9 | 8.2 | 4.2 | Minions draw fire |
| Druid Shapeshifter | 26.5 | 3.6 | 133 | 30.8 | 3.1 | 3.1 | Forms: Bear's absorb, Cat's stealth |
| Druid Grovekeeper | 3.7 | 28.6 | 21 | 9.6 | 11.2 | 3.2 | Heals, Tree of Life: 20% less damage |
| Druid Starcaller | 6.1 | 18.1 | 24 | 13.6 | 10.2 | 2.1 | Range, Hurricane widens spread |

#### PvP: an equal-level player

**Level 10** (pool 131)

| Gun (Stopping Power) | Hit % | Rounds/kill | TTK s | Burst %/s | Sustained %/s | Kills/mag | Kills/load | Trigger s/load |
|---|---|---|---|---|---|---|---|---|
| AR (famas) | 19.8 | 13.3 | 0.87 | 134 | 58 | 2.2 | 11.2 | 20.4 |
| AR +ext mags (famas) | 19.8 | 13.3 | 0.87 | 134 | 72 | 3.4 | 12.4 | 18.8 |
| SMG (ump45) | 19.8 | 13.3 | 1.16 | 99 | 56 | 2.4 | 12.0 | 23.1 |
| LMG (sa80) | 19.8 | 13.3 | 1.04 | 111 | 61 | 7.5 | 22.5 | 37.0 |
| Sniper (barrett) | 49.0 | 6.7 | 1.80 | 73 | 34 | 1.5 | 7.5 | 29.0 |
| Shotgun (spas12) | 152.1 | 2.0 | 0.86 | 80 | 47 | 4.0 | 20.0 | 53.5 |
| Pistol (usp) | 19.2 | 13.3 | 3.55 | 57 | 30 | 0.9 | 4.5 | 15.4 |
| Machine pistol (pp2000) | 14.9 | 15.6 | 0.95 | 106 | 41 | 1.3 | 6.4 | 14.3 |

| Class rotation | Sustained %/s | TTK s | First 5 s % | Deepened %/s | Deepened TTK s | Identity pass TTK s | Answer to guns |
|---|---|---|---|---|---|---|---|
| Warrior Berserker | 38.2 | 2.3 | 200 | 45.2 | 2.3 | 2.3 | Frenzy: 25% fewer bullets, Intercept, Death Wish |
| Warrior Vanguard | 30.9 | 3.0 | 165 | 37.0 | 2.4 | 2.4 | Bulwark: frontal bullets stopped; Unbreakable dome |
| Warrior Warlord | 32.9 | 2.4 | 182 | 39.4 | 2.4 | 2.4 | Banners (Ward: 15% fewer bullets), shouts |
| Paladin Crusader | 36.6 | 2.0 | 203 | 43.5 | 2.0 | 2.0 | Avenging Crusade: 20% fewer bullets |
| Paladin Protector | 32.1 | 2.8 | 162 | 38.7 | 2.0 | 2.0 | Holy Shield absorbs bullets; Bastion dome |
| Paladin Lightwarden | 32.1 | 2.8 | 162 | 38.7 | 2.0 | 2.0 | Heals 20-45% HP; Rekindle |
| Hunter Scout | 30.7 | 2.8 | 171 | 36.7 | 2.8 | 2.8 | Spotting, Camouflage: the gunner's support |
| Hunter Trapper | 30.7 | 2.8 | 171 | 36.7 | 2.8 | 2.8 | Traps and nets: roots, freezes |
| Hunter Slayer | 39.9 | 2.1 | 223 | 47.8 | 1.4 | 1.4 | Bane +30%; Hex Ward; Purge strips shields |
| Rogue Assassin | 28.0 | 1.2 | 259 | 32.9 | 1.2 | 1.2 | Stealth, Vanish: wins the first 4 s |
| Rogue Trickster | 27.8 | 3.5 | 138 | 32.5 | 3.0 | 3.0 | Slip: untargetable 0.5 s an ability; smoke |
| Rogue Duelist | 37.7 | 2.7 | 190 | 44.8 | 2.1 | 2.1 | Momentum +25%; Relentless: 15% fewer bullets |
| Priest Shadow | 7.7 | 12.9 | 45 | 16.6 | 4.8 | 5.2 | Shadowform: 30% less of everything |
| Priest Cleric | 6.6 | 14.6 | 38 | 17.1 | 7.2 | 3.2 | Power Word: Shield, heals |
| Priest Oracle | 6.6 | 14.6 | 38 | 17.1 | 7.2 | 3.2 | Barrier wall stops bullets; Premonition |
| Shaman Elementalist | 6.6 | 14.5 | 38 | 17.1 | 6.9 | 4.9 | Range and slows; Earthquake knock-downs |
| Shaman Earthbreaker | 42.1 | 2.4 | 232 | 50.4 | 1.6 | 1.6 | Stoneskin: 15% fewer bullets |
| Shaman Totemist | 6.6 | 14.5 | 38 | 17.1 | 6.9 | 2.9 | Totems: ground, heals |
| Mage Wizard | 8.8 | 10.6 | 50 | 22.8 | 5.2 | 5.2 | Spellweave +30%; Ice Block, Blink |
| Mage Spellsword | 43.9 | 1.8 | 210 | 58.7 | 1.8 | 1.8 | Mana Shield 25% HP; Arcane Avatar |
| Mage Artificer | 8.8 | 10.6 | 50 | 22.8 | 5.2 | 5.2 | Warding Rune: a bullet wall |
| Warlock Afflicter | 9.8 | 11.1 | 40 | 21.0 | 5.4 | 3.4 | Drains heal; Curse of Doom |
| Warlock Felsworn | 8.8 | 10.8 | 51 | 22.8 | 5.6 | 3.6 | Fel Hide: 20% fewer bullets |
| Warlock Necromancer | 8.8 | 10.8 | 51 | 22.8 | 5.6 | 3.6 | Minions draw fire |
| Druid Shapeshifter | 16.2 | 5.8 | 86 | 19.1 | 5.3 | 5.3 | Forms: Bear's absorb, Cat's stealth |
| Druid Grovekeeper | 6.1 | 16.6 | 36 | 16.0 | 7.2 | 3.2 | Heals, Tree of Life: 20% less damage |
| Druid Starcaller | 8.6 | 10.9 | 54 | 22.4 | 4.6 | 3.8 | Range, Hurricane widens spread |

**Level 30** (pool 198)

| Gun (Stopping Power) | Hit % | Rounds/kill | TTK s | Burst %/s | Sustained %/s | Kills/mag | Kills/load | Trigger s/load |
|---|---|---|---|---|---|---|---|---|
| AR (fal) | 27.2 | 8.9 | 0.80 | 123 | 53 | 2.2 | 11.2 | 20.4 |
| AR +ext mags (fal) | 27.2 | 8.9 | 0.80 | 123 | 66 | 3.4 | 12.4 | 18.8 |
| SMG (p90) | 14.9 | 15.6 | 0.98 | 103 | 53 | 3.2 | 16.1 | 28.3 |
| LMG (mg4) | 19.8 | 13.3 | 0.87 | 134 | 68 | 7.5 | 22.5 | 33.0 |
| Sniper (barrett) | 49.0 | 6.7 | 1.80 | 73 | 34 | 1.5 | 7.5 | 29.0 |
| Shotgun (aa12) | 95.0 | 3.0 | 0.30 | 285 | 86 | 2.7 | 13.3 | 17.2 |
| Pistol (coltanaconda) | 24.3 | 11.1 | 6.17 | 33 | 15 | 0.5 | 2.7 | 20.0 |
| Machine pistol (glock) | 14.9 | 15.6 | 0.82 | 123 | 40 | 1.2 | 5.8 | 12.9 |

| Class rotation | Sustained %/s | TTK s | First 5 s % | Deepened %/s | Deepened TTK s | Identity pass TTK s | Answer to guns |
|---|---|---|---|---|---|---|---|
| Warrior Berserker | 38.2 | 1.6 | 212 | 44.9 | 1.6 | 1.6 | Frenzy: 25% fewer bullets, Intercept, Death Wish |
| Warrior Vanguard | 30.9 | 3.0 | 165 | 37.0 | 2.4 | 2.4 | Bulwark: frontal bullets stopped; Unbreakable dome |
| Warrior Warlord | 32.9 | 2.4 | 182 | 39.4 | 2.4 | 2.4 | Banners (Ward: 15% fewer bullets), shouts |
| Paladin Crusader | 38.0 | 2.3 | 223 | 46.7 | 1.1 | 1.1 | Avenging Crusade: 20% fewer bullets |
| Paladin Protector | 32.8 | 3.1 | 177 | 39.9 | 2.3 | 2.3 | Holy Shield absorbs bullets; Bastion dome |
| Paladin Lightwarden | 32.6 | 2.3 | 179 | 40.5 | 1.5 | 1.5 | Heals 20-45% HP; Rekindle |
| Hunter Scout | 30.7 | 2.8 | 171 | 36.7 | 2.8 | 2.8 | Spotting, Camouflage: the gunner's support |
| Hunter Trapper | 30.7 | 2.8 | 171 | 36.7 | 2.8 | 2.8 | Traps and nets: roots, freezes |
| Hunter Slayer | 39.6 | 2.4 | 204 | 47.1 | 1.7 | 1.7 | Bane +30%; Hex Ward; Purge strips shields |
| Rogue Assassin | 29.7 | 1.5 | 256 | 34.4 | 1.5 | 1.5 | Stealth, Vanish: wins the first 4 s |
| Rogue Trickster | 27.8 | 3.5 | 138 | 32.5 | 3.0 | 3.0 | Slip: untargetable 0.5 s an ability; smoke |
| Rogue Duelist | 37.7 | 2.4 | 204 | 44.6 | 1.8 | 1.8 | Momentum +25%; Relentless: 15% fewer bullets |
| Priest Shadow | 9.9 | 9.4 | 49 | 21.3 | 4.1 | 2.7 | Shadowform: 30% less of everything |
| Priest Cleric | 6.6 | 14.6 | 38 | 17.1 | 7.2 | 3.2 | Power Word: Shield, heals |
| Priest Oracle | 6.6 | 14.6 | 38 | 17.1 | 7.2 | 3.2 | Barrier wall stops bullets; Premonition |
| Shaman Elementalist | 9.1 | 9.4 | 57 | 21.6 | 2.9 | 2.9 | Range and slows; Earthquake knock-downs |
| Shaman Earthbreaker | 41.8 | 2.2 | 226 | 50.2 | 1.4 | 1.4 | Stoneskin: 15% fewer bullets |
| Shaman Totemist | 6.6 | 14.5 | 38 | 17.1 | 6.9 | 2.9 | Totems: ground, heals |
| Mage Wizard | 18.1 | 6.1 | 71 | 34.9 | 4.2 | 4.2 | Spellweave +30%; Ice Block, Blink |
| Mage Spellsword | 44.2 | 2.7 | 199 | 58.0 | 1.5 | 1.5 | Mana Shield 25% HP; Arcane Avatar |
| Mage Artificer | 8.8 | 10.6 | 50 | 22.8 | 5.2 | 5.2 | Warding Rune: a bullet wall |
| Warlock Afflicter | 8.8 | 10.9 | 33 | 18.4 | 8.0 | 4.9 | Drains heal; Curse of Doom |
| Warlock Felsworn | 10.2 | 9.1 | 55 | 23.8 | 4.2 | 4.2 | Fel Hide: 20% fewer bullets |
| Warlock Necromancer | 9.4 | 9.1 | 51 | 23.2 | 4.2 | 4.2 | Minions draw fire |
| Druid Shapeshifter | 17.2 | 5.6 | 89 | 19.6 | 4.6 | 4.6 | Forms: Bear's absorb, Cat's stealth |
| Druid Grovekeeper | 6.1 | 16.6 | 36 | 16.0 | 7.2 | 3.2 | Heals, Tree of Life: 20% less damage |
| Druid Starcaller | 10.4 | 8.9 | 64 | 23.9 | 4.6 | 2.1 | Range, Hurricane widens spread |

**Level 60** (pool 300)

| Gun (Stopping Power) | Hit % | Rounds/kill | TTK s | Burst %/s | Sustained %/s | Kills/mag | Kills/load | Trigger s/load |
|---|---|---|---|---|---|---|---|---|
| AR (ak47) | 19.8 | 13.3 | 1.11 | 104 | 52 | 2.2 | 11.2 | 23.3 |
| AR +ext mags (ak47) | 19.8 | 13.3 | 1.11 | 104 | 62 | 3.4 | 12.4 | 21.9 |
| SMG (uzi) | 14.9 | 15.6 | 0.82 | 123 | 54 | 2.1 | 10.3 | 17.5 |
| LMG (m240) | 24.8 | 11.1 | 0.73 | 167 | 81 | 9.0 | 27.0 | 34.0 |
| Sniper (m21) | 35.0 | 6.7 | 1.20 | 79 | 31 | 1.5 | 7.5 | 22.0 |
| Shotgun (m1014) | 71.3 | 4.0 | 0.90 | 107 | 40 | 1.0 | 9.0 | 26.8 |
| Pistol (deserteagle) | 24.4 | 11.1 | 4.44 | 46 | 22 | 0.6 | 3.1 | 15.6 |
| Machine pistol (tmp) | 19.8 | 13.3 | 0.71 | 163 | 47 | 1.1 | 5.6 | 12.1 |

| Class rotation | Sustained %/s | TTK s | First 5 s % | Deepened %/s | Deepened TTK s | Identity pass TTK s | Answer to guns |
|---|---|---|---|---|---|---|---|
| Warrior Berserker | 38.2 | 1.6 | 212 | 44.9 | 1.6 | 1.6 | Frenzy: 25% fewer bullets, Intercept, Death Wish |
| Warrior Vanguard | 30.7 | 3.3 | 154 | 36.7 | 2.7 | 2.7 | Bulwark: frontal bullets stopped; Unbreakable dome |
| Warrior Warlord | 32.9 | 2.4 | 182 | 39.4 | 2.4 | 2.4 | Banners (Ward: 15% fewer bullets), shouts |
| Paladin Crusader | 38.0 | 2.3 | 223 | 46.7 | 1.1 | 1.1 | Avenging Crusade: 20% fewer bullets |
| Paladin Protector | 32.8 | 3.1 | 177 | 39.9 | 2.3 | 2.3 | Holy Shield absorbs bullets; Bastion dome |
| Paladin Lightwarden | 32.6 | 2.3 | 179 | 40.5 | 1.5 | 1.5 | Heals 20-45% HP; Rekindle |
| Hunter Scout | 30.7 | 2.8 | 171 | 36.7 | 2.8 | 2.8 | Spotting, Camouflage: the gunner's support |
| Hunter Trapper | 31.4 | 3.1 | 163 | 37.4 | 2.4 | 2.4 | Traps and nets: roots, freezes |
| Hunter Slayer | 39.6 | 2.4 | 204 | 47.1 | 1.7 | 1.7 | Bane +30%; Hex Ward; Purge strips shields |
| Rogue Assassin | 29.7 | 1.5 | 256 | 34.4 | 1.5 | 1.5 | Stealth, Vanish: wins the first 4 s |
| Rogue Trickster | 27.8 | 3.5 | 138 | 32.5 | 3.0 | 3.0 | Slip: untargetable 0.5 s an ability; smoke |
| Rogue Duelist | 37.7 | 2.4 | 204 | 44.6 | 1.8 | 1.8 | Momentum +25%; Relentless: 15% fewer bullets |
| Priest Shadow | 10.6 | 9.1 | 57 | 22.6 | 3.2 | 3.3 | Shadowform: 30% less of everything |
| Priest Cleric | 6.6 | 14.6 | 38 | 17.1 | 7.2 | 3.2 | Power Word: Shield, heals |
| Priest Oracle | 6.6 | 14.6 | 38 | 17.1 | 7.2 | 3.2 | Barrier wall stops bullets; Premonition |
| Shaman Elementalist | 9.1 | 9.4 | 57 | 21.6 | 2.9 | 2.9 | Range and slows; Earthquake knock-downs |
| Shaman Earthbreaker | 41.9 | 1.7 | 239 | 50.3 | 1.7 | 1.7 | Stoneskin: 15% fewer bullets |
| Shaman Totemist | 6.6 | 14.5 | 38 | 17.1 | 6.9 | 2.9 | Totems: ground, heals |
| Mage Wizard | 18.3 | 5.6 | 87 | 35.5 | 4.2 | 4.2 | Spellweave +30%; Ice Block, Blink |
| Mage Spellsword | 44.2 | 2.7 | 199 | 58.0 | 1.5 | 1.5 | Mana Shield 25% HP; Arcane Avatar |
| Mage Artificer | 8.8 | 10.6 | 50 | 22.8 | 5.2 | 5.2 | Warding Rune: a bullet wall |
| Warlock Afflicter | 8.8 | 10.9 | 33 | 18.4 | 8.0 | 4.9 | Drains heal; Curse of Doom |
| Warlock Felsworn | 10.7 | 7.1 | 68 | 24.6 | 4.4 | 2.8 | Fel Hide: 20% fewer bullets |
| Warlock Necromancer | 9.4 | 9.1 | 51 | 23.2 | 4.2 | 4.2 | Minions draw fire |
| Druid Shapeshifter | 17.2 | 5.6 | 89 | 19.6 | 4.6 | 4.6 | Forms: Bear's absorb, Cat's stealth |
| Druid Grovekeeper | 6.1 | 16.6 | 36 | 16.0 | 7.2 | 3.2 | Heals, Tree of Life: 20% less damage |
| Druid Starcaller | 10.2 | 11.5 | 40 | 22.7 | 8.7 | 2.1 | Range, Hurricane widens spread |

#### Long engagements without restocking (level 60, AK-47 with Stopping Power, median archetype)

| Engagement | Rifle alone, endless ammo (s) | Kills one load covers | Rifle then class (s) | Class alone (s) | Rounds needed |
|---|---|---|---|---|---|
| 30 normal creatures, solo | 10.3 | 35.0 | 10.3 | 63.0 | 129 |
| 40 dungeon elites, a fifth each | 27.4 | 17.5 | 101.4 | 156.0 | 343 |

#### One rifle load at level 60 against harder targets

| Target | Pool | Rounds/kill | Kills/load |
|---|---|---|---|
| Normal (open world) | 500 | 4.3 | 35.00 |
| Elite (open world) | 2625 | 22.9 | 6.56 |
| Elite (dungeon trash) | 4922 | 41.4 | 3.62 |
| Dungeon boss | 100000 | 818.6 | 0.18 |

#### Restocking a load (limited model)

A trainer or innkeeper refills every load free; the trip is the cost.

| Level | Rifle load (rounds) | Field floor (rounds) | A gunner's body (rounds) | With Scavenger |
|---|---|---|---|---|
| 10 | 150 | 30 | 15 | 30 |
| 30 | 100 | 20 | 10 | 20 |
| 60 | 150 | 30 | 15 | 30 |

#### Duels against an equal gunner (level 60, AR (ak47); the gunner kills a player who never fights back in 1.11 s)

| Archetype | Class TTK before (s) | Class TTK after (s) | 25 yd before | 25 yd after | 40 yd before | 40 yd after |
|---|---|---|---|---|---|---|
| Warrior Berserker | 1.6 | 1.6 | **wins** 2.0 s, 97% left | **wins** 2.0 s, 100% left | **wins** 3.5 s, 34% left | **wins** 3.5 s, 45% left |
| Warrior Vanguard | 2.7 | 2.7 | **wins** 3.1 s, 93% left | **wins** 3.1 s, 95% left | **wins** 4.6 s, 50% left | **wins** 4.6 s, 58% left |
| Warrior Warlord | 2.4 | 2.4 | **wins** 2.8 s, 100% left | **wins** 2.8 s, 100% left | **wins** 4.3 s, 46% left | **wins** 4.3 s, 57% left |
| Paladin Crusader | 1.1 | 1.1 | **wins** 4.1 s, 95% left | **wins** 3.8 s, 95% left | **wins** 6.3 s, 11% left | **wins** 6.0 s, 11% left |
| Paladin Protector | 2.3 | 2.3 | **wins** 5.1 s, 56% left | **wins** 5.1 s, 56% left | loses 6.4 s, gunner at 66% | loses 6.4 s, gunner at 66% |
| Paladin Lightwarden | 1.5 | 1.5 | **wins** 4.3 s, 57% left | **wins** 4.0 s, 97% left | loses 5.8 s, gunner at 40% | **wins** 6.2 s, 60% left |
| Hunter Scout | 2.8 | 2.8 | loses 2.6 s, gunner at 0% | **wins** 2.1 s, 24% left | loses 2.6 s, gunner at 0% | **wins** 2.1 s, 24% left |
| Hunter Trapper | 2.4 | 2.4 | loses 4.3 s, gunner at 39% | **wins** 4.8 s, 4% left | loses 4.3 s, gunner at 83% | loses 4.9 s, gunner at 62% |
| Hunter Slayer | 1.7 | 1.7 | loses 4.3 s, gunner at 20% | **wins** 4.1 s, 12% left | loses 4.3 s, gunner at 88% | loses 4.3 s, gunner at 76% |
| Rogue Assassin | 1.5 | 1.5 | **wins** 1.5 s, 97% left | **wins** 1.5 s, 97% left | **wins** 1.5 s, 97% left | **wins** 1.5 s, 97% left |
| Rogue Trickster | 3.0 | 3.0 | **wins** 3.0 s, 98% left | **wins** 3.0 s, 98% left | **wins** 3.0 s, 98% left | **wins** 3.0 s, 98% left |
| Rogue Duelist | 1.8 | 1.8 | **wins** 2.2 s, 9% left | **wins** 2.2 s, 55% left | loses 2.4 s, gunner at 57% | **wins** 3.1 s, 21% left |
| Priest Shadow | 3.2 | 3.3 | **wins** 3.3 s, 33% left | **wins** 3.3 s, 41% left | **wins** 3.3 s, 33% left | **wins** 3.3 s, 41% left |
| Priest Cleric | 7.2 | 3.2 | loses 2.9 s, gunner at 67% | **wins** 3.2 s, 39% left | loses 2.9 s, gunner at 67% | **wins** 3.2 s, 39% left |
| Priest Oracle | 7.2 | 3.2 | loses 3.7 s, gunner at 34% | **wins** 3.2 s, 58% left | loses 3.7 s, gunner at 34% | **wins** 3.2 s, 58% left |
| Shaman Elementalist | 2.9 | 2.9 | **wins** 2.9 s, 69% left | **wins** 2.9 s, 69% left | **wins** 2.9 s, 69% left | **wins** 2.9 s, 69% left |
| Shaman Earthbreaker | 1.7 | 1.7 | loses 2.5 s, gunner at 78% | **wins** 3.4 s, 34% left | loses 2.5 s, gunner at 78% | loses 3.8 s, gunner at 71% |
| Shaman Totemist | 6.9 | 2.9 | loses 3.1 s, gunner at 34% | **wins** 2.9 s, 64% left | loses 3.1 s, gunner at 34% | **wins** 2.9 s, 64% left |
| Mage Wizard | 4.2 | 4.2 | **wins** 4.2 s, 22% left | **wins** 4.2 s, 22% left | **wins** 4.2 s, 22% left | **wins** 4.2 s, 22% left |
| Mage Spellsword | 1.5 | 1.5 | **wins** 1.9 s, 60% left | **wins** 1.9 s, 99% left | loses 2.9 s, gunner at 54% | loses 3.0 s, gunner at 54% |
| Mage Artificer | 5.2 | 5.2 | **wins** 5.2 s, 63% left | **wins** 5.2 s, 63% left | **wins** 5.2 s, 63% left | **wins** 5.2 s, 63% left |
| Warlock Afflicter | 8.0 | 4.9 | loses 5.7 s, gunner at 19% | **wins** 4.9 s, 47% left | loses 5.7 s, gunner at 19% | **wins** 4.9 s, 47% left |
| Warlock Felsworn | 4.4 | 2.8 | **wins** 4.4 s, 58% left | **wins** 2.8 s, 54% left | **wins** 4.4 s, 58% left | **wins** 2.8 s, 54% left |
| Warlock Necromancer | 4.2 | 4.2 | **wins** 4.2 s, 83% left | **wins** 4.2 s, 83% left | **wins** 4.2 s, 83% left | **wins** 4.2 s, 83% left |
| Druid Shapeshifter | 4.6 | 4.6 | **wins** 4.6 s, 100% left | **wins** 4.6 s, 100% left | **wins** 4.6 s, 100% left | **wins** 4.6 s, 100% left |
| Druid Grovekeeper | 7.2 | 3.2 | loses 2.5 s, gunner at 69% | **wins** 3.2 s, 39% left | loses 2.5 s, gunner at 69% | **wins** 3.2 s, 39% left |
| Druid Starcaller | 8.7 | 2.1 | loses 2.5 s, gunner at 33% | **wins** 2.1 s, 19% left | loses 2.5 s, gunner at 33% | **wins** 2.1 s, 19% left |

#### Duels against an equal gunner (level 10, AR (famas); the gunner kills a player who never fights back in 0.87 s)

| Archetype | Class TTK before (s) | Class TTK after (s) | 25 yd before | 25 yd after | 40 yd before | 40 yd after |
|---|---|---|---|---|---|---|
| Warrior Berserker | 2.3 | 2.3 | **wins** 2.7 s, 97% left | **wins** 2.7 s, 100% left | **wins** 4.2 s, 26% left | **wins** 4.2 s, 38% left |
| Warrior Vanguard | 2.4 | 2.4 | **wins** 2.8 s, 96% left | **wins** 2.8 s, 98% left | **wins** 4.3 s, 44% left | **wins** 4.3 s, 53% left |
| Warrior Warlord | 2.4 | 2.4 | **wins** 2.8 s, 96% left | **wins** 2.8 s, 100% left | **wins** 4.3 s, 22% left | **wins** 4.3 s, 35% left |
| Paladin Crusader | 2.0 | 2.0 | loses 5.3 s, gunner at 11% | loses 5.3 s, gunner at 11% | loses 5.3 s, gunner at 83% | loses 5.3 s, gunner at 83% |
| Paladin Protector | 2.0 | 2.0 | **wins** 5.6 s, 23% left | **wins** 5.6 s, 23% left | loses 6.1 s, gunner at 83% | loses 6.1 s, gunner at 83% |
| Paladin Lightwarden | 2.0 | 2.0 | loses 5.4 s, gunner at 16% | **wins** 4.8 s, 72% left | loses 5.5 s, gunner at 83% | loses 5.8 s, gunner at 72% |
| Hunter Scout | 2.8 | 2.8 | loses 2.4 s, gunner at 0% | **wins** 2.1 s, 14% left | loses 2.4 s, gunner at 0% | **wins** 2.1 s, 14% left |
| Hunter Trapper | 2.8 | 2.8 | loses 4.1 s, gunner at 52% | loses 4.7 s, gunner at 17% | loses 4.1 s, gunner at 97% | loses 4.6 s, gunner at 94% |
| Hunter Slayer | 1.4 | 1.4 | loses 2.1 s, gunner at 99% | loses 2.1 s, gunner at 98% | loses 2.1 s, gunner at 99% | loses 2.1 s, gunner at 98% |
| Rogue Assassin | 1.2 | 1.2 | **wins** 1.2 s, 62% left | **wins** 1.2 s, 97% left | **wins** 1.2 s, 62% left | **wins** 1.2 s, 97% left |
| Rogue Trickster | 3.0 | 3.0 | loses 2.6 s, gunner at 12% | loses 2.6 s, gunner at 12% | loses 2.6 s, gunner at 12% | loses 2.6 s, gunner at 12% |
| Rogue Duelist | 2.1 | 2.1 | loses 2.2 s, gunner at 100% | loses 2.2 s, gunner at 100% | loses 2.2 s, gunner at 100% | loses 2.2 s, gunner at 100% |
| Priest Shadow | 4.8 | 5.2 | loses 3.5 s, gunner at 30% | loses 3.6 s, gunner at 13% | loses 3.5 s, gunner at 30% | loses 3.6 s, gunner at 13% |
| Priest Cleric | 7.2 | 3.2 | loses 2.6 s, gunner at 67% | loses 3.1 s, gunner at 44% | loses 2.6 s, gunner at 67% | loses 3.1 s, gunner at 44% |
| Priest Oracle | 7.2 | 3.2 | loses 3.3 s, gunner at 34% | **wins** 3.2 s, 40% left | loses 3.3 s, gunner at 34% | **wins** 3.2 s, 40% left |
| Shaman Elementalist | 6.9 | 4.9 | loses 2.0 s, gunner at 67% | loses 2.0 s, gunner at 62% | loses 2.0 s, gunner at 67% | loses 2.0 s, gunner at 62% |
| Shaman Earthbreaker | 1.6 | 1.6 | loses 2.3 s, gunner at 100% | loses 3.1 s, gunner at 67% | loses 2.3 s, gunner at 100% | loses 3.4 s, gunner at 100% |
| Shaman Totemist | 6.9 | 2.9 | loses 2.8 s, gunner at 67% | **wins** 2.9 s, 55% left | loses 2.8 s, gunner at 67% | **wins** 2.9 s, 55% left |
| Mage Wizard | 5.2 | 5.2 | loses 5.0 s, gunner at 9% | loses 5.0 s, gunner at 9% | loses 5.0 s, gunner at 9% | loses 5.0 s, gunner at 9% |
| Mage Spellsword | 1.8 | 1.8 | **wins** 3.4 s, 13% left | **wins** 3.4 s, 13% left | loses 2.0 s, gunner at 56% | loses 2.0 s, gunner at 56% |
| Mage Artificer | 5.2 | 5.2 | **wins** 5.2 s, 51% left | **wins** 5.2 s, 51% left | **wins** 5.2 s, 51% left | **wins** 5.2 s, 51% left |
| Warlock Afflicter | 5.4 | 3.4 | **wins** 5.4 s, 14% left | **wins** 3.4 s, 46% left | **wins** 5.4 s, 14% left | **wins** 3.4 s, 46% left |
| Warlock Felsworn | 5.6 | 3.6 | **wins** 5.6 s, 15% left | **wins** 3.6 s, 48% left | **wins** 5.6 s, 15% left | **wins** 3.6 s, 48% left |
| Warlock Necromancer | 5.6 | 3.6 | **wins** 5.6 s, 4% left | **wins** 3.6 s, 39% left | **wins** 5.6 s, 4% left | **wins** 3.6 s, 39% left |
| Druid Shapeshifter | 5.3 | 5.3 | **wins** 5.3 s, 14% left | **wins** 5.3 s, 14% left | **wins** 5.3 s, 14% left | **wins** 5.3 s, 14% left |
| Druid Grovekeeper | 7.2 | 3.2 | loses 2.2 s, gunner at 69% | loses 2.9 s, gunner at 41% | loses 2.2 s, gunner at 69% | loses 2.9 s, gunner at 41% |
| Druid Starcaller | 4.6 | 3.8 | loses 2.3 s, gunner at 46% | loses 2.3 s, gunner at 6% | loses 2.3 s, gunner at 46% | loses 2.3 s, gunner at 6% |

#### Duels against an equal gunner (level 30, AR (fal); the gunner kills a player who never fights back in 0.80 s)

| Archetype | Class TTK before (s) | Class TTK after (s) | 25 yd before | 25 yd after | 40 yd before | 40 yd after |
|---|---|---|---|---|---|---|
| Warrior Berserker | 1.6 | 1.6 | **wins** 2.0 s, 97% left | **wins** 2.0 s, 100% left | **wins** 3.5 s, 32% left | **wins** 3.5 s, 43% left |
| Warrior Vanguard | 2.4 | 2.4 | **wins** 2.8 s, 98% left | **wins** 2.8 s, 99% left | **wins** 4.3 s, 49% left | **wins** 4.3 s, 57% left |
| Warrior Warlord | 2.4 | 2.4 | **wins** 2.8 s, 100% left | **wins** 2.8 s, 100% left | **wins** 4.3 s, 44% left | **wins** 4.3 s, 55% left |
| Paladin Crusader | 1.1 | 1.1 | **wins** 4.1 s, 95% left | **wins** 3.8 s, 95% left | loses 6.2 s, gunner at 4% | **wins** 6.0 s, 6% left |
| Paladin Protector | 2.3 | 2.3 | **wins** 5.1 s, 53% left | **wins** 5.1 s, 53% left | loses 6.4 s, gunner at 66% | loses 6.4 s, gunner at 66% |
| Paladin Lightwarden | 1.5 | 1.5 | **wins** 4.3 s, 56% left | **wins** 4.0 s, 96% left | loses 5.7 s, gunner at 40% | **wins** 6.2 s, 57% left |
| Hunter Scout | 2.8 | 2.8 | loses 2.5 s, gunner at 0% | **wins** 2.1 s, 22% left | loses 2.5 s, gunner at 0% | **wins** 2.1 s, 22% left |
| Hunter Trapper | 2.8 | 2.8 | loses 4.3 s, gunner at 52% | loses 4.9 s, gunner at 17% | loses 4.3 s, gunner at 97% | loses 4.8 s, gunner at 94% |
| Hunter Slayer | 1.7 | 1.7 | loses 2.2 s, gunner at 89% | loses 2.2 s, gunner at 79% | loses 2.2 s, gunner at 89% | loses 2.2 s, gunner at 79% |
| Rogue Assassin | 1.5 | 1.5 | **wins** 1.5 s, 97% left | **wins** 1.5 s, 97% left | **wins** 1.5 s, 97% left | **wins** 1.5 s, 97% left |
| Rogue Trickster | 3.0 | 3.0 | **wins** 3.0 s, 98% left | **wins** 3.0 s, 98% left | **wins** 3.0 s, 98% left | **wins** 3.0 s, 98% left |
| Rogue Duelist | 1.8 | 1.8 | **wins** 2.2 s, 7% left | **wins** 2.2 s, 53% left | loses 2.3 s, gunner at 57% | **wins** 3.1 s, 18% left |
| Priest Shadow | 4.1 | 2.7 | loses 3.9 s, gunner at 10% | **wins** 2.7 s, 57% left | loses 3.9 s, gunner at 10% | **wins** 2.7 s, 57% left |
| Priest Cleric | 7.2 | 3.2 | loses 2.9 s, gunner at 67% | **wins** 3.2 s, 36% left | loses 2.9 s, gunner at 67% | **wins** 3.2 s, 36% left |
| Priest Oracle | 7.2 | 3.2 | loses 3.6 s, gunner at 34% | **wins** 3.2 s, 54% left | loses 3.6 s, gunner at 34% | **wins** 3.2 s, 54% left |
| Shaman Elementalist | 2.9 | 2.9 | loses 2.1 s, gunner at 29% | loses 2.1 s, gunner at 21% | loses 2.1 s, gunner at 29% | loses 2.1 s, gunner at 21% |
| Shaman Earthbreaker | 1.4 | 1.4 | loses 2.5 s, gunner at 78% | **wins** 3.1 s, 4% left | loses 2.5 s, gunner at 78% | loses 3.6 s, gunner at 71% |
| Shaman Totemist | 6.9 | 2.9 | loses 3.0 s, gunner at 34% | **wins** 2.9 s, 62% left | loses 3.0 s, gunner at 34% | **wins** 2.9 s, 62% left |
| Mage Wizard | 4.2 | 4.2 | **wins** 4.2 s, 20% left | **wins** 4.2 s, 20% left | **wins** 4.2 s, 20% left | **wins** 4.2 s, 20% left |
| Mage Spellsword | 1.5 | 1.5 | **wins** 1.9 s, 20% left | **wins** 1.9 s, 60% left | loses 2.1 s, gunner at 55% | loses 2.1 s, gunner at 55% |
| Mage Artificer | 5.2 | 5.2 | **wins** 5.2 s, 60% left | **wins** 5.2 s, 60% left | **wins** 5.2 s, 60% left | **wins** 5.2 s, 60% left |
| Warlock Afflicter | 8.0 | 4.9 | loses 5.6 s, gunner at 20% | **wins** 4.9 s, 44% left | loses 5.6 s, gunner at 20% | **wins** 4.9 s, 44% left |
| Warlock Felsworn | 4.2 | 4.2 | **wins** 4.2 s, 53% left | **wins** 4.2 s, 55% left | **wins** 4.2 s, 53% left | **wins** 4.2 s, 55% left |
| Warlock Necromancer | 4.2 | 4.2 | **wins** 4.2 s, 82% left | **wins** 4.2 s, 82% left | **wins** 4.2 s, 82% left | **wins** 4.2 s, 82% left |
| Druid Shapeshifter | 4.6 | 4.6 | **wins** 4.6 s, 100% left | **wins** 4.6 s, 100% left | **wins** 4.6 s, 100% left | **wins** 4.6 s, 100% left |
| Druid Grovekeeper | 7.2 | 3.2 | loses 2.4 s, gunner at 69% | **wins** 3.2 s, 35% left | loses 2.4 s, gunner at 69% | **wins** 3.2 s, 35% left |
| Druid Starcaller | 4.6 | 2.1 | loses 2.5 s, gunner at 35% | **wins** 2.1 s, 16% left | loses 2.5 s, gunner at 35% | **wins** 2.1 s, 16% left |
