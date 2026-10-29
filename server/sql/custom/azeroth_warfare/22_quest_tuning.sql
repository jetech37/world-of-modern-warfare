-- World of Modern Warfare: quest tuning. Guns kill fast, so open-world quests ask for more and the
-- world refills faster. World DB; safe to apply again (see "Idempotency" below).
--
--   1. Kill objectives need 3x the kills (6 -> 18), at most 63.
--   2. Ordinary open-world creatures respawn 3x faster, never below 60 s.
--   3. Quest items that creatures drop drop 3x as often, at most 100%. Required counts stay.
--
-- What is left alone, and why:
--   - A "special" creature: rank > 0 (elite, rare elite, world boss, rare), or one with fewer than
--     four open-world spawns (unique or near-unique; event-spawned and kill-credit creatures have
--     none), or one that also spawns in an instance (a dungeon/raid creature).
--   - Rule 1: counts of 3 or less, gameobject objectives (negative ReqCreatureOrGOId), spell-cast
--     objectives (ReqSpellCast), dungeon/raid quests (Type 62/81), timed quests (LimitTime), and
--     targets that are special. The cap: the quest log keeps each objective's counter in 6 bits of
--     PLAYER_QUEST_LOG_x_2 (Player::SetQuestSlotCounter masks 0x3F) and
--     Player::SendQuestUpdateAddCreatureOrGo asserts count < 64, so 63 is the most a kill count
--     can be without crashing the world server.
--   - Rule 2: only maps 0 and 1 (Eastern Kingdoms, Kalimdor); every creature a spawn can be
--     (id..id5) must be ordinary: rank 0, at least four open-world spawns, no npc_flags (vendors,
--     quest givers, trainers, flight masters keep their timers), not a guard (flags_extra 0x400),
--     not an invisible trigger (0x80), not a civilian; game-event spawns (game_event_creature:
--     Scourge Invasion, Midsummer and the like) are left alone. The floor is 60 s: a timer of
--     60 s or less is kept, and nothing is brought below 60 s (120 s -> 60 s, not 40 s). Below a
--     minute a camp refills while the party is still fighting and looting in it, and patrols chain
--     into fights that are not over yet; a minute also keeps a cleared spot clear long enough to
--     loot, skin and drink. Gathering nodes (gameobjects) are not touched.
--   - Rule 3: only creature_loot_template rows with a negative ChanceOrQuestChance (vmangos's
--     quest-only drop: LootStoreItem::needs_quest = chance < 0, shown only to players on the
--     quest) whose item is some quest's ReqItemId. Left alone: an item any quest asks for just one
--     of, base chances of 50% or more, items of dungeon/raid quests, grouped (groupid > 0) and
--     reference rows, and loot tables any special creature uses (loot_id): an elite, rare or boss,
--     an instance creature, or droppers with fewer than four open-world spawns in total.
--   - Our own content (entries 90000 and up) is not touched.
--
-- Idempotency: the first run saves the original values in aw_orig_quest_counts,
-- aw_orig_spawntimes and aw_orig_quest_drops (a row is saved once and never overwritten), and
-- every run sets the live value from the saved original, so applying this twice gives the same
-- result as applying it once. Which rows qualify is worked out from the originals and from tables
-- this file does not change, so it is the same on every run. Never drop the aw_orig_* tables by
-- hand: without them the next ship would triple the already tripled values. To undo, use
-- rollback/22_quest_tuning_rollback.sql (not applied automatically: the ship and setup scripts
-- apply only *.sql directly in this folder).
--
-- Content patch: the server runs WowPatch = 10 (mangosd.conf), so spawns and loot rows are
-- counted when patch_min <= 10 <= patch_max.

-- ── Helpers (dropped at the end) ────────────────────────────────────────────────────────────────
-- Spawns per creature entry: open world (maps 0, 1) and instances, game-event spawns excluded.
DROP TABLE IF EXISTS `aw_qt_spawns`;
CREATE TABLE `aw_qt_spawns` (
  `id` MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY,
  `ow` INT UNSIGNED NOT NULL,
  `inst` INT UNSIGNED NOT NULL
);
INSERT INTO `aw_qt_spawns` (`id`, `ow`, `inst`)
SELECT s.`cid`, SUM(s.`map` IN (0, 1)), SUM(s.`map` NOT IN (0, 1))
FROM (
  SELECT c.`guid`, c.`id`  AS `cid`, c.`map` FROM `creature` c
    WHERE c.`patch_min` <= 10 AND c.`patch_max` >= 10 AND NOT EXISTS (SELECT 1 FROM `game_event_creature` g WHERE g.`guid` = c.`guid`)
  UNION
  SELECT c.`guid`, c.`id2`, c.`map` FROM `creature` c
    WHERE c.`patch_min` <= 10 AND c.`patch_max` >= 10 AND NOT EXISTS (SELECT 1 FROM `game_event_creature` g WHERE g.`guid` = c.`guid`)
  UNION
  SELECT c.`guid`, c.`id3`, c.`map` FROM `creature` c
    WHERE c.`patch_min` <= 10 AND c.`patch_max` >= 10 AND NOT EXISTS (SELECT 1 FROM `game_event_creature` g WHERE g.`guid` = c.`guid`)
  UNION
  SELECT c.`guid`, c.`id4`, c.`map` FROM `creature` c
    WHERE c.`patch_min` <= 10 AND c.`patch_max` >= 10 AND NOT EXISTS (SELECT 1 FROM `game_event_creature` g WHERE g.`guid` = c.`guid`)
  UNION
  SELECT c.`guid`, c.`id5`, c.`map` FROM `creature` c
    WHERE c.`patch_min` <= 10 AND c.`patch_max` >= 10 AND NOT EXISTS (SELECT 1 FROM `game_event_creature` g WHERE g.`guid` = c.`guid`)
) s
WHERE s.`cid` <> 0
GROUP BY s.`cid`;

-- Per creature entry, over all its patch versions.
DROP TABLE IF EXISTS `aw_qt_tmpl`;
CREATE TABLE `aw_qt_tmpl` (
  `entry` MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY,
  `max_rank` TINYINT UNSIGNED NOT NULL,
  `townsfolk` TINYINT UNSIGNED NOT NULL   -- npc_flags, guard, invisible trigger or civilian
);
INSERT INTO `aw_qt_tmpl` (`entry`, `max_rank`, `townsfolk`)
SELECT `entry`, MAX(`rank`),
       MAX(`npc_flags` <> 0 OR (`flags_extra` & 0x480) <> 0 OR `civilian` <> 0)
FROM `creature_template`
GROUP BY `entry`;

-- Ordinary creatures: kill targets (rule 1) and droppers (rule 3).
DROP TABLE IF EXISTS `aw_qt_ordinary`;
CREATE TABLE `aw_qt_ordinary` (`entry` MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY);
INSERT INTO `aw_qt_ordinary` (`entry`)
SELECT t.`entry` FROM `aw_qt_tmpl` t JOIN `aw_qt_spawns` s ON s.`id` = t.`entry`
WHERE t.`max_rank` = 0 AND s.`ow` >= 4 AND s.`inst` = 0 AND t.`entry` < 90000;

-- Creatures whose open-world spawns respawn faster (rule 2).
DROP TABLE IF EXISTS `aw_qt_respawn`;
CREATE TABLE `aw_qt_respawn` (`entry` MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY);
INSERT INTO `aw_qt_respawn` (`entry`)
SELECT t.`entry` FROM `aw_qt_tmpl` t JOIN `aw_qt_spawns` s ON s.`id` = t.`entry`
WHERE t.`max_rank` = 0 AND t.`townsfolk` = 0 AND s.`ow` >= 4 AND t.`entry` < 90000;

-- ── 1. Kill objectives x3 (cap 63) ──────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS `aw_orig_quest_counts` (
  `entry` MEDIUMINT UNSIGNED NOT NULL,
  `patch` TINYINT UNSIGNED NOT NULL,
  `ReqCreatureOrGOCount1` SMALLINT UNSIGNED NOT NULL,
  `ReqCreatureOrGOCount2` SMALLINT UNSIGNED NOT NULL,
  `ReqCreatureOrGOCount3` SMALLINT UNSIGNED NOT NULL,
  `ReqCreatureOrGOCount4` SMALLINT UNSIGNED NOT NULL,
  PRIMARY KEY (`entry`, `patch`)
) COMMENT = 'WOMW 22_quest_tuning: original kill counts';
INSERT IGNORE INTO `aw_orig_quest_counts`
SELECT `entry`, `patch`, `ReqCreatureOrGOCount1`, `ReqCreatureOrGOCount2`, `ReqCreatureOrGOCount3`, `ReqCreatureOrGOCount4`
FROM `quest_template`
WHERE `ReqCreatureOrGOCount1` > 3 OR `ReqCreatureOrGOCount2` > 3 OR `ReqCreatureOrGOCount3` > 3 OR `ReqCreatureOrGOCount4` > 3;

UPDATE `quest_template` q
JOIN `aw_orig_quest_counts` o ON o.`entry` = q.`entry` AND o.`patch` = q.`patch`
JOIN `aw_qt_ordinary` k ON k.`entry` = q.`ReqCreatureOrGOId1`
SET q.`ReqCreatureOrGOCount1` = LEAST(63, o.`ReqCreatureOrGOCount1` * 3)
WHERE o.`ReqCreatureOrGOCount1` > 3 AND q.`ReqSpellCast1` = 0
  AND q.`Type` NOT IN (62, 81) AND q.`LimitTime` = 0 AND q.`entry` < 90000;

UPDATE `quest_template` q
JOIN `aw_orig_quest_counts` o ON o.`entry` = q.`entry` AND o.`patch` = q.`patch`
JOIN `aw_qt_ordinary` k ON k.`entry` = q.`ReqCreatureOrGOId2`
SET q.`ReqCreatureOrGOCount2` = LEAST(63, o.`ReqCreatureOrGOCount2` * 3)
WHERE o.`ReqCreatureOrGOCount2` > 3 AND q.`ReqSpellCast2` = 0
  AND q.`Type` NOT IN (62, 81) AND q.`LimitTime` = 0 AND q.`entry` < 90000;

UPDATE `quest_template` q
JOIN `aw_orig_quest_counts` o ON o.`entry` = q.`entry` AND o.`patch` = q.`patch`
JOIN `aw_qt_ordinary` k ON k.`entry` = q.`ReqCreatureOrGOId3`
SET q.`ReqCreatureOrGOCount3` = LEAST(63, o.`ReqCreatureOrGOCount3` * 3)
WHERE o.`ReqCreatureOrGOCount3` > 3 AND q.`ReqSpellCast3` = 0
  AND q.`Type` NOT IN (62, 81) AND q.`LimitTime` = 0 AND q.`entry` < 90000;

UPDATE `quest_template` q
JOIN `aw_orig_quest_counts` o ON o.`entry` = q.`entry` AND o.`patch` = q.`patch`
JOIN `aw_qt_ordinary` k ON k.`entry` = q.`ReqCreatureOrGOId4`
SET q.`ReqCreatureOrGOCount4` = LEAST(63, o.`ReqCreatureOrGOCount4` * 3)
WHERE o.`ReqCreatureOrGOCount4` > 3 AND q.`ReqSpellCast4` = 0
  AND q.`Type` NOT IN (62, 81) AND q.`LimitTime` = 0 AND q.`entry` < 90000;

-- ── 2. Open-world respawns x3 faster (floor 60 s) ───────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS `aw_orig_spawntimes` (
  `guid` INT UNSIGNED NOT NULL PRIMARY KEY,
  `id` MEDIUMINT UNSIGNED NOT NULL,   -- applied only while the spawn is still this creature
  `spawntimesecsmin` INT UNSIGNED NOT NULL,
  `spawntimesecsmax` INT UNSIGNED NOT NULL
) COMMENT = 'WOMW 22_quest_tuning: original respawn times';
INSERT IGNORE INTO `aw_orig_spawntimes`
SELECT `guid`, `id`, `spawntimesecsmin`, `spawntimesecsmax`
FROM `creature`
WHERE `map` IN (0, 1) AND `spawntimesecsmax` > 60 AND `id` < 90000;

UPDATE `creature` c
JOIN `aw_orig_spawntimes` o ON o.`guid` = c.`guid` AND o.`id` = c.`id`
SET c.`spawntimesecsmin` = IF(o.`spawntimesecsmin` <= 60, o.`spawntimesecsmin`, GREATEST(60, ROUND(o.`spawntimesecsmin` / 3))),
    c.`spawntimesecsmax` = IF(o.`spawntimesecsmax` <= 60, o.`spawntimesecsmax`, GREATEST(60, ROUND(o.`spawntimesecsmax` / 3)))
WHERE c.`map` IN (0, 1)
  AND c.`patch_min` <= 10 AND c.`patch_max` >= 10
  AND NOT EXISTS (SELECT 1 FROM `game_event_creature` g WHERE g.`guid` = c.`guid`)
  AND c.`id` IN (SELECT `entry` FROM `aw_qt_respawn`)
  AND (c.`id2` = 0 OR c.`id2` IN (SELECT `entry` FROM `aw_qt_respawn`))
  AND (c.`id3` = 0 OR c.`id3` IN (SELECT `entry` FROM `aw_qt_respawn`))
  AND (c.`id4` = 0 OR c.`id4` IN (SELECT `entry` FROM `aw_qt_respawn`))
  AND (c.`id5` = 0 OR c.`id5` IN (SELECT `entry` FROM `aw_qt_respawn`));

-- ── 3. Quest-item drop chances x3 (cap 100%) ────────────────────────────────────────────────────
-- Items worth tripling: every quest that asks for them asks for 2 or more, none is a dungeon/raid quest.
DROP TABLE IF EXISTS `aw_qt_items`;
CREATE TABLE `aw_qt_items` (`item` MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY);
INSERT INTO `aw_qt_items` (`item`)
SELECT r.`item` FROM (
  SELECT `ReqItemId1` AS `item`, `ReqItemCount1` AS `cnt`, `Type` FROM `quest_template` WHERE `ReqItemId1` <> 0 AND `ReqItemCount1` > 0
  UNION ALL
  SELECT `ReqItemId2`, `ReqItemCount2`, `Type` FROM `quest_template` WHERE `ReqItemId2` <> 0 AND `ReqItemCount2` > 0
  UNION ALL
  SELECT `ReqItemId3`, `ReqItemCount3`, `Type` FROM `quest_template` WHERE `ReqItemId3` <> 0 AND `ReqItemCount3` > 0
  UNION ALL
  SELECT `ReqItemId4`, `ReqItemCount4`, `Type` FROM `quest_template` WHERE `ReqItemId4` <> 0 AND `ReqItemCount4` > 0
) r
WHERE r.`item` < 90000
GROUP BY r.`item`
HAVING MIN(r.`cnt`) >= 2 AND SUM(r.`Type` IN (62, 81)) = 0;

-- Loot tables only ordinary open-world creatures use.
DROP TABLE IF EXISTS `aw_qt_loot`;
CREATE TABLE `aw_qt_loot` (`loot_id` MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY);
INSERT INTO `aw_qt_loot` (`loot_id`)
SELECT d.`loot_id`
FROM (SELECT DISTINCT `loot_id`, `entry` FROM `creature_template` WHERE `loot_id` <> 0) d
JOIN `aw_qt_tmpl` t ON t.`entry` = d.`entry`
LEFT JOIN `aw_qt_spawns` s ON s.`id` = d.`entry`
WHERE d.`loot_id` < 90000
GROUP BY d.`loot_id`
HAVING MAX(t.`max_rank`) = 0 AND SUM(COALESCE(s.`inst`, 0)) = 0 AND SUM(COALESCE(s.`ow`, 0)) >= 4
   AND MAX(d.`entry`) < 90000;

CREATE TABLE IF NOT EXISTS `aw_orig_quest_drops` (
  `entry` MEDIUMINT UNSIGNED NOT NULL,
  `item` MEDIUMINT UNSIGNED NOT NULL,
  `groupid` TINYINT UNSIGNED NOT NULL,
  `patch_min` TINYINT UNSIGNED NOT NULL,
  `patch_max` TINYINT UNSIGNED NOT NULL,
  `ChanceOrQuestChance` FLOAT NOT NULL,
  PRIMARY KEY (`entry`, `item`, `groupid`, `patch_min`, `patch_max`)
) COMMENT = 'WOMW 22_quest_tuning: original quest-drop chances';
INSERT IGNORE INTO `aw_orig_quest_drops`
SELECT `entry`, `item`, `groupid`, `patch_min`, `patch_max`, `ChanceOrQuestChance`
FROM `creature_loot_template`
WHERE `ChanceOrQuestChance` < 0;

UPDATE `creature_loot_template` l
JOIN `aw_orig_quest_drops` o
  ON o.`entry` = l.`entry` AND o.`item` = l.`item` AND o.`groupid` = l.`groupid`
 AND o.`patch_min` = l.`patch_min` AND o.`patch_max` = l.`patch_max`
JOIN `aw_qt_items` i ON i.`item` = l.`item`
JOIN `aw_qt_loot` t ON t.`loot_id` = l.`entry`
SET l.`ChanceOrQuestChance` = -LEAST(100, -o.`ChanceOrQuestChance` * 3)
WHERE o.`ChanceOrQuestChance` < 0 AND o.`ChanceOrQuestChance` > -50
  AND l.`groupid` = 0 AND l.`mincountOrRef` > 0
  AND l.`patch_min` <= 10 AND l.`patch_max` >= 10;

-- ── Clean up ────────────────────────────────────────────────────────────────────────────────────
DROP TABLE IF EXISTS `aw_qt_spawns`, `aw_qt_tmpl`, `aw_qt_ordinary`, `aw_qt_respawn`, `aw_qt_items`, `aw_qt_loot`;
