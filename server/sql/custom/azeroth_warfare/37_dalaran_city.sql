-- World of Modern Warfare: Dalaran's people, and the city fitted to the ground it stands on.
-- World DB; safe to apply again. Goes with 34_dalaran.sql (the portals) and the patch-9 build of
-- client/src/wotlk/dalaran.rs.
--
-- 1. The old ground's spawns. The city stands level with the land: the ground is laid as a
--    plateau under its rim and eased back over a 60-yard ring (client/src/wotlk/dalaran.rs). The
--    Dalaran summoners, workers, elemental slaves, bears and the odd lake murloc of the old crater
--    and its slopes would stand inside the city or in mid-air (or under ground) on the reshaped
--    ring. Every map 0 creature and gameobject inside the city's outline plus the ring
--    (`dalaran_probe outline` in the client crate writes the polygon) is saved to
--    `aw_orig_dalaran_*` and removed. Spawns beyond the ring keep their places.
-- 2. Getting in. The ground meets the rim ledge all round, so you walk up onto the rim from any
--    side. The rim and the streets are not joined: Wrath's city is walled all round and none of
--    its streets reaches its outline (checked with `dalaran_probe walk`, a flood fill over the
--    floors from the portal arrival), and there is no gate to open. So a Kirin Tor gatekeeper on
--    the east rim (the side the Alterac road comes up) sends you in to the portal arrival, and one
--    in the south court sends you back out to the rim; flyers land in the open squares, and the
--    archmages in Stormwind and Orgrimmar (34_dalaran.sql) still send you in.
--    The gossip scripts teleport the player: command 6 moves the script's source, and a gossip
--    script's source is the creature, so data_flags 1 (swap source and target) is set; without it
--    the archmage teleported itself away.
-- 3. The people: an innkeeper, two bankers, four merchants, a mage trainer and six profession
--    trainers round the market square, Kirin Tor guardians on both squares. Each is a copy of a
--    stock 1.12 NPC's template (all patches) with its wares or lessons, renamed for Dalaran and
--    friendly to both factions (faction 35). Every spot is checked against the city's floors by
--    the client test `the_city_spawns_stand_on_its_floors`.
--
-- Ids: creature entries 94600-94622, spawn guids 2594600-2594629, gossip menus 60220-60221,
-- gossip scripts 942010-942011.

-- 1. The old crater's spawns.
SET @city = ST_GeomFromText('POLYGON((580 369,574 395,560 420,549 443,533 464,519 484,508 505,505 531,507 565,492 588,464 596,439 605,430 639,414 669,391 691,366 713,337 729,305 733,274 733,242 727,213 713,186 695,164 672,145 645,124 628,100 617,79 601,58 585,40 565,25 544,16 518,21 487,19 462,9 440,4 417,1 393,3 369,13 346,25 325,26 303,30 281,34 258,42 235,41 206,29 163,28 124,35 84,57 60,85 42,116 31,151 31,185 39,216 43,244 34,274 24,304 20,335 23,364 33,393 41,420 55,448 68,473 85,494 106,527 116,546 141,553 174,572 197,579 227,575 259,583 286,591 313,587 342,580 369))');
CREATE TABLE IF NOT EXISTS `aw_orig_dalaran_creature` LIKE `creature`;
CREATE TABLE IF NOT EXISTS `aw_orig_dalaran_gameobject` LIKE `gameobject`;
INSERT IGNORE INTO `aw_orig_dalaran_creature` SELECT * FROM `creature`
  WHERE `map` = 0 AND `guid` NOT BETWEEN 2594200 AND 2594799 AND ST_Contains(@city, POINT(`position_x`, `position_y`));
DELETE FROM `creature`
  WHERE `map` = 0 AND `guid` NOT BETWEEN 2594200 AND 2594799 AND ST_Contains(@city, POINT(`position_x`, `position_y`));
INSERT IGNORE INTO `aw_orig_dalaran_gameobject` SELECT * FROM `gameobject`
  WHERE `map` = 0 AND ST_Contains(@city, POINT(`position_x`, `position_y`));
DELETE FROM `gameobject` WHERE `map` = 0 AND ST_Contains(@city, POINT(`position_x`, `position_y`));

-- 3. The people: copies of stock templates.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 94600 AND 94622;
DELETE FROM `npc_vendor` WHERE `entry` BETWEEN 94600 AND 94622;
DELETE FROM `npc_trainer` WHERE `entry` BETWEEN 94600 AND 94622;
DROP TEMPORARY TABLE IF EXISTS `aw_tmp_ct`;

-- Amisi Azuregaze, Innkeeper (from 6740)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 6740;
UPDATE `aw_tmp_ct` SET `entry` = 94600, `name` = 'Amisi Azuregaze', `subname` = 'Innkeeper', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94600, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 6740;
INSERT INTO `npc_trainer` SELECT 94600, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 6740;
-- Larana Drome, Banker (from 2455)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 2455;
UPDATE `aw_tmp_ct` SET `entry` = 94601, `name` = 'Larana Drome', `subname` = 'Banker', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94601, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 2455;
INSERT INTO `npc_trainer` SELECT 94601, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 2455;
-- Hobart Grapplehammer, Banker (from 2457)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 2457;
UPDATE `aw_tmp_ct` SET `entry` = 94602, `name` = 'Hobart Grapplehammer', `subname` = 'Banker', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94602, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 2457;
INSERT INTO `npc_trainer` SELECT 94602, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 2457;
-- Endora Moorehead, Trade Supplies (from 1286)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 1286;
UPDATE `aw_tmp_ct` SET `entry` = 94603, `name` = 'Endora Moorehead', `subname` = 'Trade Supplies', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94603, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 1286;
INSERT INTO `npc_trainer` SELECT 94603, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 1286;
-- Aerith Primrose, Food & Drink (from 3546)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 3546;
UPDATE `aw_tmp_ct` SET `entry` = 94604, `name` = 'Aerith Primrose', `subname` = 'Food & Drink', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94604, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 3546;
INSERT INTO `npc_trainer` SELECT 94604, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 3546;
-- Kizi Copperclip, Arcane Goods (from 1257)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 1257;
UPDATE `aw_tmp_ct` SET `entry` = 94605, `name` = 'Kizi Copperclip', `subname` = 'Arcane Goods', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94605, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 1257;
INSERT INTO `npc_trainer` SELECT 94605, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 1257;
-- Vanessa Sellers, Enchanting Supplies (from 5158)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 5158;
UPDATE `aw_tmp_ct` SET `entry` = 94606, `name` = 'Vanessa Sellers', `subname` = 'Enchanting Supplies', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94606, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 5158;
INSERT INTO `npc_trainer` SELECT 94606, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 5158;
-- Archmage Celindra, Mage Trainer (from 5497)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 5497;
UPDATE `aw_tmp_ct` SET `entry` = 94607, `name` = 'Archmage Celindra', `subname` = 'Mage Trainer', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94607, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 5497;
INSERT INTO `npc_trainer` SELECT 94607, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 5497;
-- Alexander Nowell, First Aid Trainer (from 5150)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 5150;
UPDATE `aw_tmp_ct` SET `entry` = 94608, `name` = 'Alexander Nowell', `subname` = 'First Aid Trainer', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94608, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 5150;
INSERT INTO `npc_trainer` SELECT 94608, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 5150;
-- Awilo Lon'gomba, Cooking Trainer (from 5159)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 5159;
UPDATE `aw_tmp_ct` SET `entry` = 94609, `name` = 'Awilo Lon''gomba', `subname` = 'Cooking Trainer', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94609, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 5159;
INSERT INTO `npc_trainer` SELECT 94609, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 5159;
-- Marcia Chase, Fishing Trainer (from 5161)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 5161;
UPDATE `aw_tmp_ct` SET `entry` = 94610, `name` = 'Marcia Chase', `subname` = 'Fishing Trainer', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94610, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 5161;
INSERT INTO `npc_trainer` SELECT 94610, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 5161;
-- Enchanter Nalthanis, Enchanting Trainer (from 5157)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 5157;
UPDATE `aw_tmp_ct` SET `entry` = 94611, `name` = 'Enchanter Nalthanis', `subname` = 'Enchanting Trainer', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94611, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 5157;
INSERT INTO `npc_trainer` SELECT 94611, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 5157;
-- Charles Worth, Tailoring Trainer (from 2627)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 2627;
UPDATE `aw_tmp_ct` SET `entry` = 94612, `name` = 'Charles Worth', `subname` = 'Tailoring Trainer', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94612, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 2627;
INSERT INTO `npc_trainer` SELECT 94612, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 2627;
-- Linzy Blackbolt, Alchemy Trainer (from 4900)
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 4900;
UPDATE `aw_tmp_ct` SET `entry` = 94613, `name` = 'Linzy Blackbolt', `subname` = 'Alchemy Trainer', `faction` = 35;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94613, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 4900;
INSERT INTO `npc_trainer` SELECT 94613, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 4900;
-- Kirin Tor Guardian: the Stormwind guard's stats in a Dalaran protector's look, no Stormwind directions.
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 68;
UPDATE `aw_tmp_ct` SET `entry` = 94620, `name` = 'Kirin Tor Guardian', `subname` = 'Dalaran', `faction` = 35, `display_id1` = 3602, `display_id2` = 0, `display_id3` = 0, `display_id4` = 0, `display_probability1` = 100, `display_probability2` = 0, `display_probability3` = 0, `display_probability4` = 0, `display_total_probability` = 100, `npc_flags` = 0, `gossip_menu_id` = 0, `script_name` = '';
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94620, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 68;
INSERT INTO `npc_trainer` SELECT 94620, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 68;
-- The gatekeepers: copies of 34_dalaran.sql's Archmage of the Kirin Tor (94630), with their own gossip.
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 94630;
UPDATE `aw_tmp_ct` SET `entry` = 94621, `name` = 'Kirin Tor Gatekeeper', `subname` = 'To the Rim', `faction` = 35, `display_id1` = 16590, `display_id2` = 0, `display_id3` = 0, `display_id4` = 0, `display_probability1` = 100, `display_probability2` = 0, `display_probability3` = 0, `display_probability4` = 0, `display_total_probability` = 100, `npc_flags` = 1, `script_name` = '', `level_min` = 60, `level_max` = 60, `gossip_menu_id` = 60221;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94621, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 94630;
INSERT INTO `npc_trainer` SELECT 94621, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 94630;
CREATE TEMPORARY TABLE `aw_tmp_ct` SELECT * FROM `creature_template` WHERE `entry` = 94630;
UPDATE `aw_tmp_ct` SET `entry` = 94622, `name` = 'Kirin Tor Gatekeeper', `subname` = 'To the City', `faction` = 35, `display_id1` = 16590, `display_id2` = 0, `display_id3` = 0, `display_id4` = 0, `display_probability1` = 100, `display_probability2` = 0, `display_probability3` = 0, `display_probability4` = 0, `display_total_probability` = 100, `npc_flags` = 1, `script_name` = '', `level_min` = 60, `level_max` = 60, `gossip_menu_id` = 60220;
INSERT INTO `creature_template` SELECT * FROM `aw_tmp_ct`;
DROP TEMPORARY TABLE `aw_tmp_ct`;
INSERT INTO `npc_vendor` SELECT 94622, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id` FROM `npc_vendor` WHERE `entry` = 94630;
INSERT INTO `npc_trainer` SELECT 94622, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`, `build_min`, `build_max` FROM `npc_trainer` WHERE `entry` = 94630;

-- 2. The gatekeepers' gossip: up onto the streets (the portal arrival), down to the road.
DELETE FROM `gossip_menu` WHERE `entry` IN (60220, 60221);
INSERT INTO `gossip_menu` (`entry`, `text_id`, `script_id`, `condition_id`) VALUES
(60220, 68, 0, 0),
(60221, 68, 0, 0);
DELETE FROM `gossip_menu_option` WHERE `menu_id` IN (60220, 60221);
INSERT INTO `gossip_menu_option` (`menu_id`, `id`, `option_icon`, `option_text`, `option_broadcast_text`, `option_id`,
  `npc_option_npcflag`, `action_menu_id`, `action_poi_id`, `action_script_id`, `box_coded`, `box_money`, `box_text`,
  `box_broadcast_text`, `condition_id`) VALUES
(60220, 0, 2, 'Take me up into Dalaran.', 0, 1, 1, 0, 0, 942010, 0, 0, NULL, 0, 0),
(60221, 0, 2, 'Take me down to the rim.', 0, 1, 1, 0, 0, 942011, 0, 0, NULL, 0, 0);
DELETE FROM `gossip_scripts` WHERE `id` IN (942010, 942011);
INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `datalong3`, `datalong4`,
  `target_param1`, `target_param2`, `target_type`, `data_flags`, `dataint`, `dataint2`, `dataint3`, `dataint4`,
  `x`, `y`, `z`, `o`, `condition_id`, `comments`) VALUES
(942010, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 297.12, 293.69, 71.00, 1.663, 0, 'Dalaran gate: up to the streets'),
(942011, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 522.00, 364.00, 40.31, 3.14, 0, 'Dalaran gate: down to the rim');

-- The spawns (map 0). Columns as 34_dalaran.sql's.
DELETE FROM `creature` WHERE `guid` BETWEEN 2594600 AND 2594629;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
(2594600, 94600, 0, 245.70, 541.20, 71.34, 0.62, 300, 300, 0, 0),
(2594601, 94601, 0, 261.70, 509.20, 71.34, 1.5, 300, 300, 0, 0),
(2594602, 94602, 0, 253.70, 501.20, 71.34, 1.36, 300, 300, 0, 0),
(2594603, 94603, 0, 237.70, 573.20, 71.34, 5.7, 300, 300, 0, 0),
(2594604, 94604, 0, 253.70, 605.20, 71.34, 4.93, 300, 300, 0, 0),
(2594605, 94605, 0, 277.70, 605.20, 71.34, 4.46, 300, 300, 0, 0),
(2594606, 94606, 0, 229.70, 525.20, 71.34, 0.7, 300, 300, 0, 0),
(2594607, 94607, 0, 293.70, 581.20, 71.34, 3.88, 300, 300, 0, 0),
(2594608, 94608, 0, 293.70, 557.20, 71.34, 3.22, 300, 300, 0, 0),
(2594609, 94609, 0, 293.70, 533.20, 71.34, 2.49, 300, 300, 0, 0),
(2594610, 94610, 0, 285.70, 621.20, 71.34, 4.41, 300, 300, 0, 0),
(2594611, 94611, 0, 229.70, 557.20, 71.34, 6.22, 300, 300, 0, 0),
(2594612, 94612, 0, 261.70, 621.20, 71.34, 4.76, 300, 300, 0, 0),
(2594613, 94613, 0, 269.70, 573.20, 71.34, 4.46, 300, 300, 0, 0),
(2594614, 94620, 0, 237.70, 509.20, 71.34, 4.17, 300, 300, 0, 0),
(2594615, 94620, 0, 253.70, 565.20, 71.34, 2.41, 300, 300, 0, 0),
(2594616, 94620, 0, 277.70, 477.20, 71.34, 4.87, 300, 300, 0, 0),
(2594617, 94620, 0, 285.70, 141.20, 68.84, 4.06, 300, 300, 0, 0),
(2594618, 94620, 0, 309.70, 141.20, 68.84, 5.36, 300, 300, 0, 0),
(2594619, 94620, 0, 309.70, 181.20, 68.85, 1.11, 300, 300, 0, 0),
(2594620, 94621, 0, 301.70, 157.20, 68.82, 3.19, 300, 300, 0, 0),
(2594621, 94622, 0, 516.00, 360.00, 41.06, 3.14, 300, 300, 0, 0);
