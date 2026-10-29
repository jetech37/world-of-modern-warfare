-- World of Modern Warfare: undo 22_quest_tuning.sql. World DB. NOT applied automatically: the ship
-- and setup scripts (vps/ship-server.sh, vps/setup-server.sh, scripts/setup-db.sh, package/aw.ps1)
-- apply only the *.sql files directly in custom/azeroth_warfare/, never this subfolder.
--
-- First delete (or rename away from *.sql) custom/azeroth_warfare/22_quest_tuning.sql, or the next
-- ship tunes everything again. Then run by hand: mariadb mangos < 22_quest_tuning_rollback.sql
-- and restart the world server (it loads quests, spawns and loot at startup).
-- Puts every saved original back, then drops the saved originals. Safe to run twice: a missing
-- backup table is created empty first, so the restore does nothing.

CREATE TABLE IF NOT EXISTS `aw_orig_quest_counts` (
  `entry` MEDIUMINT UNSIGNED NOT NULL,
  `patch` TINYINT UNSIGNED NOT NULL,
  `ReqCreatureOrGOCount1` SMALLINT UNSIGNED NOT NULL,
  `ReqCreatureOrGOCount2` SMALLINT UNSIGNED NOT NULL,
  `ReqCreatureOrGOCount3` SMALLINT UNSIGNED NOT NULL,
  `ReqCreatureOrGOCount4` SMALLINT UNSIGNED NOT NULL,
  PRIMARY KEY (`entry`, `patch`)
);
UPDATE `quest_template` q
JOIN `aw_orig_quest_counts` o ON o.`entry` = q.`entry` AND o.`patch` = q.`patch`
SET q.`ReqCreatureOrGOCount1` = o.`ReqCreatureOrGOCount1`,
    q.`ReqCreatureOrGOCount2` = o.`ReqCreatureOrGOCount2`,
    q.`ReqCreatureOrGOCount3` = o.`ReqCreatureOrGOCount3`,
    q.`ReqCreatureOrGOCount4` = o.`ReqCreatureOrGOCount4`;
DROP TABLE `aw_orig_quest_counts`;

CREATE TABLE IF NOT EXISTS `aw_orig_spawntimes` (
  `guid` INT UNSIGNED NOT NULL PRIMARY KEY,
  `id` MEDIUMINT UNSIGNED NOT NULL,
  `spawntimesecsmin` INT UNSIGNED NOT NULL,
  `spawntimesecsmax` INT UNSIGNED NOT NULL
);
UPDATE `creature` c
JOIN `aw_orig_spawntimes` o ON o.`guid` = c.`guid` AND o.`id` = c.`id`
SET c.`spawntimesecsmin` = o.`spawntimesecsmin`,
    c.`spawntimesecsmax` = o.`spawntimesecsmax`;
DROP TABLE `aw_orig_spawntimes`;

CREATE TABLE IF NOT EXISTS `aw_orig_quest_drops` (
  `entry` MEDIUMINT UNSIGNED NOT NULL,
  `item` MEDIUMINT UNSIGNED NOT NULL,
  `groupid` TINYINT UNSIGNED NOT NULL,
  `patch_min` TINYINT UNSIGNED NOT NULL,
  `patch_max` TINYINT UNSIGNED NOT NULL,
  `ChanceOrQuestChance` FLOAT NOT NULL,
  PRIMARY KEY (`entry`, `item`, `groupid`, `patch_min`, `patch_max`)
);
UPDATE `creature_loot_template` l
JOIN `aw_orig_quest_drops` o
  ON o.`entry` = l.`entry` AND o.`item` = l.`item` AND o.`groupid` = l.`groupid`
 AND o.`patch_min` = l.`patch_min` AND o.`patch_max` = l.`patch_max`
SET l.`ChanceOrQuestChance` = o.`ChanceOrQuestChance`;
DROP TABLE `aw_orig_quest_drops`;
