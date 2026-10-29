-- World of Modern Warfare: companion battles, turn-based battles of companion teams after Blizzard's
-- Pet Battles (design/COMPANION_BATTLES.md; rules server/src/fusion/include/fusion/CompanionBattles.h,
-- game server/src/game/Fusion/FusionCompanionBattles.cpp). World DB. Safe to apply again: every
-- block clears its own rows first.
--
-- Reserved ranges:
--   creature_template.entry      94200-94249 (94201-94229 the tamers, 94200 + tamer id;
--                                94230-94235 the Companion Battle Trainers)
--   creature.guid                2594200-2594299
--
-- VERIFY in game: each spawn stands a few yards from its anchor (by the anchor's spawn guid in the
-- 1.12 world dump, named beside it); a slope or a wall may want the offset changed. A tamer whose
-- anchor is not spawned is simply not placed. Looks copy a vanilla NPC's display (named beside).

-- -- The tamers' teams ---------------------------------------------------------------------------
-- Mirrored row for row by CompanionBattles::Tamers() (CompanionBattleTests holds the two
-- together); the server reads this table. A companion's stage follows the level (under 20 the
-- first form, under 40 the second, else the last); its picks are fixed per tamer by the rules.
DROP TABLE IF EXISTS `fusion_companion_tamer`;
CREATE TABLE `fusion_companion_tamer` (
  `id` smallint(5) unsigned NOT NULL,
  `entry` mediumint(8) unsigned NOT NULL,
  `name` varchar(64) NOT NULL,
  `zone` int(10) unsigned NOT NULL COMMENT 'AreaTable id',
  `side` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT '0 any, 1 Alliance, 2 Horde',
  `level` tinyint(3) unsigned NOT NULL,
  `grand` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `place` varchar(64) NOT NULL DEFAULT '',
  `species1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `species2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `species3` smallint(5) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW companion battles: the tamers and their teams';

INSERT INTO `fusion_companion_tamer` (`id`, `entry`, `name`, `zone`, `side`, `level`, `grand`, `place`, `species1`, `species2`, `species3`) VALUES
(1, 94201, 'Julia Stevens', 12, 1, 5, 0, 'Goldshire', 26, 11, 0),
(2, 94202, 'Zunta', 14, 2, 5, 0, 'Razor Hill', 115, 111, 0),
(3, 94203, 'Old MacDonald', 40, 1, 13, 0, 'Sentinel Hill', 183, 121, 182),
(4, 94204, 'Dagra the Fierce', 17, 2, 14, 0, 'the Crossroads', 17, 108, 157),
(5, 94205, 'Lindsay', 44, 1, 19, 0, 'Lakeshire', 153, 104, 127),
(6, 94206, 'Analynn', 331, 1, 22, 0, 'Astranaar', 135, 158, 5),
(7, 94207, 'Shademaster Kiryn', 406, 2, 22, 0, 'Sun Rock Retreat', 159, 137, 28),
(8, 94208, 'Eric Davidson', 10, 1, 25, 0, 'Darkshire', 144, 16, 145),
(9, 94209, 'Kela Grimtotem', 400, 0, 30, 0, 'Freewind Post', 125, 130, 131),
(10, 94210, 'Merda Stronghoof', 405, 0, 34, 0, 'Shadowprey Village', 116, 156, 29),
(11, 94211, 'Steven Lisbane', 33, 0, 36, 0, 'Nesingwary''s Expedition', 9, 106, 14),
(12, 94212, 'Grazzle the Great', 15, 0, 38, 0, 'Brackenwall Village', 114, 134, 117),
(13, 94213, 'Everessa', 8, 0, 38, 0, 'Stonard', 139, 13, 185),
(14, 94214, 'Bill Buckler', 33, 0, 42, 0, 'Booty Bay', 206, 174, 162),
(15, 94215, 'Traitor Gluk', 357, 0, 44, 0, 'Camp Mojache', 120, 160, 127),
(16, 94216, 'David Kosse', 47, 0, 46, 0, 'Aerie Peak', 12, 15, 123),
(17, 94217, 'Kortas Darkhammer', 51, 0, 48, 0, 'Thorium Point', 21, 19, 154),
(18, 94218, 'Zoltan', 361, 0, 52, 0, 'Talonbranch Glade', 141, 136, 142),
(19, 94219, 'Durin Darkhammer', 46, 0, 54, 0, 'Morgan''s Vigil', 1, 24, 140),
(20, 94220, 'Elena Flutterfly', 493, 0, 55, 0, 'Nighthaven', 5, 163, 20),
(21, 94221, 'Deiza Plaguehorn', 139, 0, 57, 0, 'Light''s Hope Chapel', 146, 147, 148),
(22, 94222, 'Grand Master Lydia Accoste', 41, 0, 60, 1, 'Karazhan', 145, 147, 32),
(23, 94223, 'Grand Master Trixxy', 618, 0, 60, 1, 'Everlook', 8, 22, 105);

-- -- The NPCs ------------------------------------------------------------------------------------
-- Gossip NPCs (script npc_fusion_companion_tamer) friendly to everyone (faction 35).
DELETE FROM `creature_template` WHERE `entry` BETWEEN 94200 AND 94249;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94201, 0, 'Julia Stevens', 'Companion Tamer', 5, 5, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 235 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 235's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94202, 0, 'Zunta', 'Companion Tamer', 5, 5, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 3188 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 3188's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94203, 0, 'Old MacDonald', 'Companion Tamer', 13, 13, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 233 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 233's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94204, 0, 'Dagra the Fierce', 'Companion Tamer', 14, 14, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 3429 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 3429's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94205, 0, 'Lindsay', 'Companion Tamer', 19, 19, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 238 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 238's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94206, 0, 'Analynn', 'Companion Tamer', 22, 22, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6738 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6738's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94207, 0, 'Shademaster Kiryn', 'Companion Tamer', 22, 22, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 7731 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 7731's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94208, 0, 'Eric Davidson', 'Companion Tamer', 25, 25, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 311 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 311's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94209, 0, 'Kela Grimtotem', 'Companion Tamer', 30, 30, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 11116 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 11116's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94210, 0, 'Merda Stronghoof', 'Companion Tamer', 34, 34, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6746 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6746's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94211, 0, 'Steven Lisbane', 'Companion Tamer', 36, 36, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 240 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 240's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94212, 0, 'Grazzle the Great', 'Companion Tamer', 38, 38, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 3391 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 3391's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94213, 0, 'Everessa', 'Companion Tamer', 38, 38, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6735 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6735's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94214, 0, 'Bill Buckler', 'Companion Tamer', 42, 42, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 2496 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 2496's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94215, 0, 'Traitor Gluk', 'Companion Tamer', 44, 44, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 117 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 117's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94216, 0, 'David Kosse', 'Companion Tamer', 46, 46, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 7744 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 7744's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94217, 0, 'Kortas Darkhammer', 'Companion Tamer', 48, 48, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 5111 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 5111's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94218, 0, 'Zoltan', 'Companion Tamer', 52, 52, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 12578 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 12578's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94219, 0, 'Durin Darkhammer', 'Companion Tamer', 54, 54, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 2299 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 2299's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94220, 0, 'Elena Flutterfly', 'Companion Tamer', 55, 55, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6735 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6735's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94221, 0, 'Deiza Plaguehorn', 'Companion Tamer', 57, 57, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 12617 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 12617's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94222, 0, 'Grand Master Lydia Accoste', 'Grand Master Tamer', 60, 60, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6741 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6741's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94223, 0, 'Grand Master Trixxy', 'Grand Master Tamer', 60, 60, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 11118 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 11118's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94230, 0, 'Audrey Burnhep', 'Companion Battle Trainer', 30, 30, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6740 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6740's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94231, 0, 'Grady Bannson', 'Companion Battle Trainer', 30, 30, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 5111 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 5111's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94232, 0, 'Lhara Moonbrook', 'Companion Battle Trainer', 30, 30, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6735 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6735's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94233, 0, 'Varzok', 'Companion Battle Trainer', 30, 30, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6929 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6929's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94234, 0, 'Naleen Skyfeather', 'Companion Battle Trainer', 30, 30, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6746 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6746's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94235, 0, 'Will Larsons', 'Companion Battle Trainer', 30, 30, 35, 1, t.`display_id1`, 1, 100, 100, 7, 1, 1, 'npc_fusion_companion_tamer'
FROM `creature_template` t WHERE t.`entry` = 6741 ORDER BY t.`patch` LIMIT 1;    -- VERIFY look (creature 6741's)

-- -- The spawns ----------------------------------------------------------------------------------
DELETE FROM `creature` WHERE `guid` BETWEEN 2594200 AND 2594299;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594201, 94201, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 80346;    -- VERIFY: by Innkeeper Farley, Goldshire
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594202, 94202, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 7671;    -- VERIFY: by Innkeeper Grosk, Razor Hill
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594203, 94203, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 66978;    -- VERIFY: by Innkeeper Heather, Sentinel Hill
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594204, 94204, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 14139;    -- VERIFY: by Innkeeper Boorand Plainswind, the Crossroads
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594205, 94205, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 10076;    -- VERIFY: by Innkeeper Brianna, Lakeshire
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594206, 94206, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 32451;    -- VERIFY: by Innkeeper Kimlya, Astranaar
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594207, 94207, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 29233;    -- VERIFY: by Innkeeper Jayka, Sun Rock Retreat
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594208, 94208, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 4208;    -- VERIFY: by Innkeeper Trelayne, Darkshire
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594209, 94209, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 21575;    -- VERIFY: by Innkeeper Abeqwa, Freewind Post
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594210, 94210, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 28340;    -- VERIFY: by Innkeeper Sikewa, Shadowprey Village
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594211, 94211, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 532;    -- VERIFY: by Hemet Nesingwary
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594212, 94212, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 8478;    -- VERIFY: by Shardi, Brackenwall Village
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594213, 94213, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 31947;    -- VERIFY: by Innkeeper Karakul, Stonard
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594214, 94214, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 537;    -- VERIFY: by Innkeeper Skindle, Booty Bay
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594215, 94215, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 50060;    -- VERIFY: by Innkeeper Greul, Camp Mojache
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594216, 94216, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 92923;    -- VERIFY: by Innkeeper Thulfram, Aerie Peak
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594217, 94217, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 5597;    -- VERIFY: by Lanie Reed, Thorium Point
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594218, 94218, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 39069;    -- VERIFY: by Mishellena, Talonbranch Glade
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594219, 94219, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 4571;    -- VERIFY: by Borgus Stoutarm, Morgan's Vigil
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594220, 94220, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 42337;    -- VERIFY: by Bunthen Plainswind, Nighthaven
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594221, 94221, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 53177;    -- VERIFY: by Khaelyn Steelwing, Light's Hope Chapel
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594222, 94222, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 17649;    -- VERIFY: by the spirit healer of Deadwind Pass
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594223, 94223, c.`map`, c.`position_x` + 4, c.`position_y` + 3, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 42181;    -- VERIFY: by Innkeeper Vizzie, Everlook
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594240, 94230, c.`map`, c.`position_x` + -3, c.`position_y` + -2, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 79841;    -- VERIFY: by Innkeeper Allison, Stormwind
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594241, 94231, c.`map`, c.`position_x` + -3, c.`position_y` + -2, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 1745;    -- VERIFY: by Innkeeper Firebrew, Ironforge
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594242, 94232, c.`map`, c.`position_x` + -3, c.`position_y` + -2, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 46341;    -- VERIFY: by Innkeeper Saelienne, Darnassus
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594243, 94233, c.`map`, c.`position_x` + -3, c.`position_y` + -2, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 4661;    -- VERIFY: by Innkeeper Gryshka, Orgrimmar
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594244, 94234, c.`map`, c.`position_x` + -3, c.`position_y` + -2, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 24772;    -- VERIFY: by Innkeeper Pala, Thunder Bluff
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
SELECT 2594245, 94235, c.`map`, c.`position_x` + -3, c.`position_y` + -2, c.`position_z`, c.`orientation`, 300, 300, 0, 0 FROM `creature` c WHERE c.`guid` = 38407;    -- VERIFY: by Innkeeper Norman, the Undercity
