-- World of Modern Warfare: Wrath's Dalaran in the Alterac Mountains (map 0), where classic
-- Dalaran's violet dome stood: client/src/wotlk/dalaran.rs removes the dome, places the WotLK city
-- over the crater and reshapes the terrain under it, in patched Azeroth tiles in patch-9.MPQ.
-- World DB; safe to apply again. (Map 800, the city's own map in the first version, is gone:
-- ids 800 and up are left free.)
--
-- Reached by gossip portals: an Archmage of the Kirin Tor in Stormwind's Trade District and in
-- Orgrimmar's Valley of Strength sends you to the city; one in the city sends you back to either.
--
-- The arrival point is the floor the 3.3.5 `Teleport: Dalaran` spell lands on, moved through the
-- city's Alterac placement (dalaran::arrival; a client test checks the floor is there).
--
-- VERIFY in game: the two capital spots (spawned at the GM teleport points: nobody floating, nobody
-- in a wall), the archmage's look (display 16590, the old teleporters'), and the city spot.
-- Collision: the server's maps and vmaps must be extracted again from the packaged client once
-- (aw.ps1 does it when patch-9.MPQ changes; design/WOTLK_ASSETS.md).
--
-- Ids: creature entries 94630-94639 (94630 the capitals' archmage, 94631 the city's), spawn guids
-- 2594630-2594639, gossip menus 60200-60201, gossip scripts 942001-942003. (The first version used
-- entries 94200-94201 and guids 2594201-2594203, the companion tamers' (33_companion_battles.sql):
-- it overwrote Julia Stevens and removed three tamers' spawns. The cleanup below takes away what
-- it left, and only that.)

DELETE FROM `map_template` WHERE `entry` = 800;

-- The first version's leftovers: its spawns in the tamers' guids (guid 2594201 is Julia Stevens's,
-- entry 94201; 2594202-2594203 are tamers 94202-94203's), and its templates, where 33 did not
-- write them back. A tamer's own row is never matched: entry 94200 is no tamer, and 94201 is only
-- Julia's at her own guid.
DELETE FROM `creature` WHERE `guid` BETWEEN 2594201 AND 2594203
  AND (`id` = 94200 OR (`id` = 94201 AND `guid` <> 2594201));
DELETE FROM `creature_template` WHERE `entry` = 94200 AND `name` = 'Archmage of the Kirin Tor';
DELETE FROM `creature_template` WHERE `entry` = 94201 AND `name` = 'Archmage of the Kirin Tor';

DELETE FROM `creature_template` WHERE `entry` BETWEEN 94630 AND 94639;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `gossip_menu_id`, `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`,
  `unit_class`, `civilian`, `script_name`) VALUES
(94630, 0, 'Archmage of the Kirin Tor', 'Portal to Dalaran', 60, 60, 35, 1, 60200, 16590, 1, 100, 100, 7, 8, 1, ''),
(94631, 0, 'Archmage of the Kirin Tor', 'Portals Home', 60, 60, 35, 1, 60201, 16590, 1, 100, 100, 7, 8, 1, '');

DELETE FROM `gossip_menu` WHERE `entry` IN (60200, 60201);
INSERT INTO `gossip_menu` (`entry`, `text_id`, `script_id`, `condition_id`) VALUES
(60200, 68, 0, 0),
(60201, 68, 0, 0);

DELETE FROM `gossip_menu_option` WHERE `menu_id` IN (60200, 60201);
INSERT INTO `gossip_menu_option` (`menu_id`, `id`, `option_icon`, `option_text`, `option_broadcast_text`, `option_id`,
  `npc_option_npcflag`, `action_menu_id`, `action_poi_id`, `action_script_id`, `box_coded`, `box_money`, `box_text`,
  `box_broadcast_text`, `condition_id`) VALUES
(60200, 0, 2, 'Send me to Dalaran.', 0, 1, 1, 0, 0, 942001, 0, 0, NULL, 0, 0),
(60201, 0, 2, 'Send me to Stormwind.', 0, 1, 1, 0, 0, 942002, 0, 0, NULL, 0, 0),
(60201, 1, 2, 'Send me to Orgrimmar.', 0, 1, 1, 0, 0, 942003, 0, 0, NULL, 0, 0);

-- Command 6: teleport to map `datalong` at x, y, z, o. It moves the script's source, and a gossip
-- script's source is the creature: data_flags 1 (swap source and target) makes it the player. (Without
-- it the archmage teleported itself to Dalaran and vanished from Stormwind.)
DELETE FROM `gossip_scripts` WHERE `id` IN (942001, 942002, 942003);
INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `datalong3`, `datalong4`,
  `target_param1`, `target_param2`, `target_type`, `data_flags`, `dataint`, `dataint2`, `dataint3`, `dataint4`,
  `x`, `y`, `z`, `o`, `condition_id`, `comments`) VALUES
(942001, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 297.12, 293.69, 71.00, 1.663, 0, 'Portal to Dalaran'),
(942002, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, -8833.38, 628.62, 94.01, 1.07, 0, 'Dalaran: portal to Stormwind'),
(942003, 0, 0, 6, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1629.36, -4373.39, 31.26, 3.55, 0, 'Dalaran: portal to Orgrimmar');

DELETE FROM `creature` WHERE `guid` BETWEEN 2594630 AND 2594639;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
(2594630, 94630, 0, -8831.40, 631.50, 94.01, 4.20, 300, 300, 0, 0),   -- Stormwind, Trade District
(2594631, 94630, 1, 1631.50, -4370.40, 31.26, 4.70, 300, 300, 0, 0),  -- Orgrimmar, Valley of Strength
(2594632, 94631, 0, 300.11, 293.81, 71.00, 3.22, 300, 300, 0, 0);     -- Dalaran, beside the arrival point
