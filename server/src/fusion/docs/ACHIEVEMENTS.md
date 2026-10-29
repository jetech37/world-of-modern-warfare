# Achievements and unlocks: integration notes

The rules are in `include/fusion/Achievements.h` and `src/Achievements.cpp`, with tests in
`tests/AchievementTests.cpp`. The library counts nothing on its own. The game server feeds it events,
stores what comes back, and checks Create-a-Class against it.

## The model

- **Counters** (`AccountStats`): one `uint32` for each `(Stat, key)` pair on each **account**. Most
  counters are running totals. `Level`, `BestPlayerStreak` and `BestMultiKill` are maxima, as
  `IsMaximum` reports. Use `Record(key, v)` and it applies the correct rule for that stat.
- **The book** (`DefaultAchievementBook(catalog)`): about 600 achievements. Each has one or more
  requirements (`StatKey` plus threshold, all of which must be met) and the unlocks it grants.
  Achievement ids are stable (`AchievementId(category, item, sub)`), because the server stores them.
- **Unlocks are derived, never stored.** `UnlocksFor(catalog, book, earned, isGm)` is the starter kit
  (`usp_mp` alone) plus the rewards of every earned achievement. A GM gets `UnlockSet::Everything()`.
  If the book is retuned, every account's unlocks follow the new book at its next login.
- `Evaluate(book, stats, earned)` returns the achievements that are newly earned. Ids already in
  `earned` are never returned again, so each achievement is announced once.

## 1. Counting: what to call and where

Build the counters at login from the database with `stats.Load(key, value)`, and keep them in
`FusionPlayerData`. WoW allows one character per account online at a time, so a single in-memory copy
per account is safe.

| Event | Where in the server | Call |
|---|---|---|
| A kill credited to a player | `FusionMgr::OnUnitKilled` (from `Unit::Kill`). Credit the killer. For an elite, also credit every damager from `DamageCredit::Take`. | Fill a `Fusion::KillEvent` and call `RecordKill(stats, e)` |
| Death | `FusionMgr::OnPlayerDeath` | `Add(Deaths)`, and also `Add(PlayerDeaths)` if a player killed them. Reset the player-kill run (below). |
| Level up | `Player::GiveLevel` | `Record({Level}, newLevel)` |
| Zone explored | `Player::CheckAreaExploreAndOutdoor`, where the explore XP is given | `Add(ZonesExplored)` |
| Quest completed | `Player::RewardQuest` | `Add(QuestsCompleted)` |
| Sprinting | `FusionMgr::UpdatePlayer`, while `sprinting` | `Add(DistanceSprinted, yards)`. Add whole yards and carry the fraction yourself. |
| Corpse looted | `Player::SendLoot` on a creature corpse, the first time per corpse | `Add(CorpsesLooted)` |
| Reload finished | Where the server completes a reload (`STATE_RELOADING`) | `Add(Reloads)` |
| Class equipped | `FusionMgr::ApplyEquip` | `Add(ClassSwaps)` |
| Revive | Where a downed player is revived | The reviver: `Add(RevivesGiven)`. The revived player: `Add(TimesRevived)`. |
| Streak reward earned | `FusionMgr::AwardStreak`, for each reward that `Streak::AddKill` returns | `Add({StreaksEarned, 0})` and `Add({StreaksEarned, id})` |
| Streak called in | `FusionMgr::CallIn` | `Add({StreaksCalled, 0})` and `Add({StreaksCalled, id})` |
| Players killed in one life | Keep a per-life count of player kills, separate from `Streak`, because `Streak` also counts creatures in tenths. Reset it on death. | `Raise({BestPlayerStreak}, run)` |
| Flash, stun or smoke catches an enemy | `FusionMgr::ApplyTactical`, for each enemy affected | `Add({TacticalVictims, EquipmentKey(kind)})` |
| Building piece placed | The build channel's place handler | `Add(BuildingsBuilt)` |
| Companion evolved | Wherever companions evolve | `Add(CompanionsEvolved)` |
| Bounty turned in | `BountyMgr::TurnIn` (FusionBounties.cpp), at a bounty board | `Add(BountiesCompleted)` |

How to fill `KillEvent`:

- **Victim:** set `victimIsPlayer`. For a creature, set `creatureType` and `rank` from
  `creature_template`. Mark dungeon bosses as `CreatureRank::DungeonBoss`. Set `gray` for gray-level
  victims and critters, exactly as `KillKind::GrayCreature` is set for streaks. A gray kill counts
  nothing.
- **Weapon:** `weaponId` and `weaponClass` come from the catalog profile of the weapon that dealt the
  killing blow. `attachments` are the attachments of the equipped class for that gun. `perks` are the
  class's `perk1`–`perk3`, without zeros.
- **Kill source:** set `equipment = KindOf(name)` when a lethal killed. Set `streakId` when a
  killstreak reward killed. Set `explosive` for an underbarrel grenade or a launcher splash.
- **Shot details:** set `headshot` from the hit location (`EVENT_HEADSHOT`). Set `melee` for the knife,
  a lunge or a shield bash.
- **Position:** `distance` is killer to victim in yards. `stance` is the killer's stance.
- **Revenge and multi-kills:** `revenge` means the victim was the last player who killed the killer.
  `burst` is the number of kills by this killer within about 2 seconds, including this one.

After counting, run evaluation:

```cpp
std::vector<Fusion::StatKey> changed = stats.TakeDirty();         // also: the rows to save
std::vector<uint32_t> fresh = Fusion::Evaluate(book, stats, earned);
for (uint32_t id : fresh) { earned.insert(id); /* INSERT the row, announce it */ }
if (!fresh.empty()) unlocks = Fusion::UnlocksFor(catalog, book, earned, isGm);
// The window, if the client has it open (or always; it is small):
send(kChannelAchievements, AchievementWire::EncodeEarned(fresh));
send(kChannelAchievements, AchievementWire::EncodeProgress(AchievementWire::Touched(book, stats, earned, changed)));
```

Evaluation is cheap (one pass over about 600 achievements). Running it after each kill is fine.
If it needs to be cheaper, batch it once per `UpdatePlayer` tick while anything is dirty.

## 2. Database (characters DB, a new migration)

```sql
-- Azeroth Warfare: per-account achievement counters (Fusion::AccountStats).
CREATE TABLE IF NOT EXISTS `account_fusion_stats` (
  `account` int(11) unsigned NOT NULL COMMENT 'account.id',
  `stat` smallint(5) unsigned NOT NULL COMMENT 'Fusion::Stat',
  `key` int(11) unsigned NOT NULL DEFAULT '0' COMMENT 'gun id, ClassKey, TypeKey, GunAttachmentKey...; 0 any',
  `value` int(11) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account`, `stat`, `key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- Azeroth Warfare: achievements each account has earned (unlocks are derived from these).
CREATE TABLE IF NOT EXISTS `account_fusion_achievement` (
  `account` int(11) unsigned NOT NULL COMMENT 'account.id',
  `achievement` int(11) unsigned NOT NULL COMMENT 'Fusion::Achievement::id',
  `earned_at` bigint(20) unsigned NOT NULL DEFAULT '0' COMMENT 'unix time',
  `character` int(11) unsigned NOT NULL DEFAULT '0' COMMENT 'the character that earned it',
  PRIMARY KEY (`account`, `achievement`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

- **Saving counters:** write the keys from `TakeDirty()` with
  `INSERT ... ON DUPLICATE KEY UPDATE value = VALUES(value)`. Do this with the character save, or
  sooner.
- **Saving achievements:** insert each earned achievement as soon as it is earned. If an achievement
  row is ever lost, the counters still meet its requirements, so the next `Evaluate` earns it back.

## 3. Create-a-Class: unlocks instead of level

Keep an `UnlockSet unlocks` in `FusionPlayerData` and rebuild it with `UnlocksFor` at login and when
achievements are earned. Then make three changes in `FusionMgr`:

- At the three `Fusion::ValidateLoadout(m_catalog, l, player->GetLevel(), IsAdmin(player), ProPerks(player))`
  calls, use the new overload: `Fusion::ValidateLoadout(m_catalog, l, d.unlocks, ProPerks(player))`.
  It applies every existing rule first, judged as a GM so only the shape of the class is checked.
  Then it returns `Locked` for any gun, attachment, camo, perk, deathstreak, lethal, tactical or
  `streak1`–`streak3` the account has not unlocked.
- At the two `Fusion::CanUseWeapon(level, isGm, w)` calls (the held weapon and the shot check), use
  `d.unlocks.CanUseWeapon(w)`. An underbarrel weapon needs its gun and its attachment unlocked. A
  killstreak weapon is always allowed, because the reward itself is gated.
- Change `streak.SetLoadout(Fusion::DefaultStreakLadder(), ...)` to
  `Fusion::UnlockedStreaks(Fusion::DefaultStreakLadder(), d.unlocks)`. A new account then has no
  rewards until it earns them, which only player kills do.

The per-WoW-class starter guns (`starter: true` in the catalog) no longer apply. A new character has
the USP .45 as its secondary and nothing else. The default class sent at first login should be
`secondary = usp_mp` and everything else none. Stored classes that use something now locked are
rejected with `Locked` when equipped. The client should offer to clear those slots.

Pro perks are not part of this system. They stay as they are (`UseRequirement`, or `.fusion pro`).

## 4. The achievements window: payloads

All messages go over the existing `CMSG/SMSG_FUSION_CHANNEL` on channel
`Fusion::Packets::kChannelAchievements = 3`. Each body is
`version u8 (=1) · op u8 · ...`, little-endian.

| Op | Direction | Body after the version and op |
|---|---|---|
| 1 Request | client → server | Nothing. Sent when the window opens. The server replies with every Page. |
| 2 Page | server → client | `page u16 · pages u16 · count u16 · count × Row` |
| 3 Earned | server → client | `count u16 · count × id u32` (show a toast) |
| 4 Progress | server → client | `count u16 · count × (id u32 · n u8 · n × current u32)` |

The layout of `Row`:

```
id u32 · category u8 · flags u8 (1 earned, 2 hidden) · name (len u8 + bytes) · description (len u16 + bytes)
· nreq u8 · nreq × (stat u16 · key u32 · current u32 · needed u32)
· nrew u8 · nrew × (kind u8 · id u32 · camo u8 · attachment (len u8 + bytes))
```

- **Paging:** `AchievementWire::EncodePages(Views(book, stats, earned))` splits the list into bodies
  of at most `kMaxChannelBody` (8192) bytes. The full list is about 600 rows, which comes to several
  pages.
- **Hidden achievements:** a hidden achievement with no progress is sent as `"???"` with no
  requirements and no rewards.
- **Display names:** the client already has the catalog, so it can name rewards itself
  (`UnlockKind` 1 Gun, 2 Attachment, 3 Camo, 4 Perk, 5 Deathstreak, 6 Lethal, 7 Tactical,
  8 Killstreak, 9 Title).
- **Tabs:** `category` gives the window's tab: 1 Weapons, 2 Attachments, 3 Camos, 4 Perks,
  5 Deathstreaks, 6 Lethals, 7 Tacticals, 8 Killstreaks, 9 Feats.
- **Progress order:** the `current` values in a Progress message follow the requirement order of the
  Row that was sent earlier.
- **Shared definition:** the encoders and decoders, and `View`, `Page` and `ProgressEntry`, are
  defined in the library. The Rust side in `benilla` / `client` mirrors this table, so test it against
  the bytes `tests/AchievementTests.cpp` checks for `EncodeEarned`.

## 5. Things to know

- **Players count as humanoids** in every typed counter. For example, "Kill 25 humanoids with pistols"
  accepts Defias or Horde players.
- **Killstreak unlocks need player kills** (`PlayerKills`, and `BestPlayerStreak` for the bigger
  rewards). A PvE-only account never unlocks killstreaks. This follows the design (killstreaks are PvP).
- **Never renumber stored values.** `Stat` values, `AttachmentCode` values (MW2's 14 attachments are
  fixed at 1–14) and achievement ids are all stored in the database. Add new stats at the end of the
  enum.
- **New catalog guns** that the book has no hand-written path for still get an achievement, unlocked
  by kills with their class. The coverage test fails if anything in the catalog is unreachable.
- **GM commands** worth adding: `.fusion ach list|grant <id>|reset` and `.fusion stat <name> <key> <value>`.
  `StatName` gives the names to use.
