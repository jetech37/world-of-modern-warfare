-- World of Modern Warfare: homestead sites and brief flight (W9). World DB.
--
-- 1. The homestead sites and their districts as data (Plots.h; the server falls back to its
--    built-in copy, Plots::Default(), when these tables are missing, empty or unsound). Plot ids
--    must never be renumbered: a bought plot is kept by its id.
-- 2. The Hyjal summit re-laid off the hillsides (design/WORLD_BUILDING.md 4.1): the gate hub moves
--    to (5385, -3710), and a second courier stop stands on the crater floor.
-- 3. Azshara Crater (map 37) and the Thalassian plateau (map 0) with their stewards, couriers,
--    trainers, suppliers and spirit healers; couriers in Ironforge, Undercity, Darnassus (Rut'theran),
--    Thunder Bluff and Light's Hope Chapel. Each site's own deeds.
-- 4. Graveyards: Azshara Crater's two "Plains of Snow" safe locs linked; Hyjal linked to Everlook as a
--    fallback (the fusion sends a ghost in a site to its nearest courier stop's spirit healer first).
-- 5. The launch points for brief flight (Riding.h).
--
-- Heights were read from the server's own height maps (dist data/maps); VERIFY each spawn in game
-- (nobody in a wall, nobody floating). Creature guids 9260001-9260199 are W9's.

-- ── 1. Sites and districts ──────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS `fusion_plot_site` (
  `id` tinyint(3) unsigned NOT NULL,
  `name` varchar(64) NOT NULL,
  `place` varchar(64) NOT NULL COMMENT 'after a plot''s name: "in Azshara Crater"',
  `map` smallint(5) unsigned NOT NULL,
  `gate_x` float NOT NULL, `gate_y` float NOT NULL, `gate_z` float NOT NULL, `gate_o` float NOT NULL,
  `min_x` float NOT NULL, `min_y` float NOT NULL, `max_x` float NOT NULL, `max_y` float NOT NULL,
  `floor_z` float NOT NULL COMMENT 'below it inside the bounds: put back at the nearest stop',
  `ceiling_z` float NOT NULL COMMENT 'no flight above it inside the bounds',
  PRIMARY KEY (`id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='World of Modern Warfare: homestead sites (Plots.h)';

CREATE TABLE IF NOT EXISTS `fusion_plot_site_stop` (
  `site` tinyint(3) unsigned NOT NULL,
  `idx` tinyint(3) unsigned NOT NULL,
  `name` varchar(64) NOT NULL,
  `x` float NOT NULL, `y` float NOT NULL, `z` float NOT NULL, `o` float NOT NULL,
  PRIMARY KEY (`site`, `idx`)) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='World of Modern Warfare: courier stops besides a site''s gate';

CREATE TABLE IF NOT EXISTS `fusion_plot_district` (
  `first_id` int(10) unsigned NOT NULL,
  `site` tinyint(3) unsigned NOT NULL,
  `size` tinyint(3) unsigned NOT NULL COMMENT '1 small, 2 standard, 3 large',
  `map` smallint(5) unsigned NOT NULL,
  `origin_x` float NOT NULL, `origin_y` float NOT NULL COMMENT 'the lowest x and y corner',
  `cell` float NOT NULL,
  `cols` smallint(5) unsigned NOT NULL COMMENT 'along x', `rows` smallint(5) unsigned NOT NULL COMMENT 'along y',
  `price` int(10) unsigned NOT NULL, `upkeep` int(10) unsigned NOT NULL COMMENT 'copper a week',
  `deed_item` int(10) unsigned NOT NULL,
  PRIMARY KEY (`first_id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='World of Modern Warfare: plot grids (Plots.h)';

DELETE FROM `fusion_plot_site`;
INSERT INTO `fusion_plot_site` VALUES
(1, 'the Hyjal summit', 'on the Hyjal summit', 1, 5385, -3710, 1594.2, 0, 5300, -3850, 5800, -2600, 1420, 1700),
(2, 'Azshara Crater', 'in Azshara Crater', 37, 128, 135, 236, 1.6, -800, -1100, 700, 1450, 100, 600),
(3, 'the Thalassian plateau', 'on the Thalassian plateau', 0, 3738, -4300, 129, 0, 3720, -5400, 4800, -3950, 90, 260);

DELETE FROM `fusion_plot_site_stop`;
INSERT INTO `fusion_plot_site_stop` VALUES
(1, 1, 'the crater floor', 5500, -2790, 1462.7, 3.1),
(2, 1, 'the south canyon', -530, -200, 321.8, 3.6);

DELETE FROM `fusion_plot_district`;
INSERT INTO `fusion_plot_district` VALUES
-- The Hyjal summit, same ids: east terrace, gate pocket, crater floor (now 2 by 4).
(1,   1, 1, 1, 5520, -3450, 24, 8, 4, 50000, 2000, 91030),
(101, 1, 2, 1, 5400, -3790, 40, 6, 4, 250000, 10000, 91031),
(201, 1, 3, 1, 5360, -2870, 64, 2, 4, 1000000, 30000, 91032),
-- Azshara Crater: central floor, north arm, south canyon.
(301, 2, 1, 37, 150, 0, 24, 8, 4, 50000, 2000, 93400),
(401, 2, 2, 37, 75, 600, 40, 6, 4, 250000, 10000, 93401),
(501, 2, 3, 37, -675, -325, 64, 2, 4, 1000000, 30000, 93402),
-- The Thalassian plateau, z 129: the small plots north of the town, the large ones south.
(601, 3, 1, 0, 3750, -4295, 24, 16, 4, 50000, 2000, 93403),
(701, 3, 2, 0, 3750, -4625, 40, 12, 8, 250000, 10000, 93404),
(801, 3, 3, 0, 3750, -4891, 64, 4, 4, 1000000, 30000, 93405);

-- Each site's deeds (Construction.h kDeedCrater*, kDeedPlateau*), as 03's.
DELETE FROM `item_template` WHERE `entry` BETWEEN 93400 AND 93409;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(93400, 0, 15, 0, 'Deed to a Small Homestead', 'A plot of 24 yards in Azshara Crater. Show it to a Plot Steward.', 15274, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7),
(93401, 0, 15, 0, 'Deed to a Homestead', 'A plot of 40 yards in Azshara Crater. Show it to a Plot Steward.', 15274, 2, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7),
(93402, 0, 15, 0, 'Deed to a Large Homestead', 'A plot of 64 yards in Azshara Crater. Show it to a Plot Steward.', 15274, 3, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7),
(93403, 0, 15, 0, 'Deed to a Small Homestead', 'A plot of 24 yards on the Thalassian plateau. Show it to a Plot Steward.', 15274, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7),
(93404, 0, 15, 0, 'Deed to a Homestead', 'A plot of 40 yards on the Thalassian plateau. Show it to a Plot Steward.', 15274, 2, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7),
(93405, 0, 15, 0, 'Deed to a Large Homestead', 'A plot of 64 yards on the Thalassian plateau. Show it to a Plot Steward.', 15274, 3, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7);

-- ── 2. Hyjal: the gate hub moves off the standard plots (03's spawns, kept in step there) ────
UPDATE `creature` SET `position_x` = 5388.0, `position_y` = -3716.0, `position_z` = 1595.5, `orientation` = 2.6 WHERE `guid` = 2591021;   -- steward
UPDATE `creature` SET `position_x` = 5381.0, `position_y` = -3716.0, `position_z` = 1595.8, `orientation` = 0.6 WHERE `guid` = 2591022;   -- courier
UPDATE `creature` SET `position_x` = 5389.0, `position_y` = -3704.0, `position_z` = 1593.5, `orientation` = 3.6 WHERE `guid` = 2591023;   -- trainer
UPDATE `creature` SET `position_x` = 5382.0, `position_y` = -3703.0, `position_z` = 1593.5, `orientation` = 4.2 WHERE `guid` = 2591024;   -- supplier
-- id-override: 03_construction.sql game_tele.id (the homesteads' point moved here)
DELETE FROM `game_tele` WHERE `id` BETWEEN 9101 AND 9104 OR `name` IN ('Homesteads', 'HomesteadsHyjalCrater', 'HomesteadsCrater', 'HomesteadsPlateau');
INSERT INTO `game_tele` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES
(9101, 5385.00, -3710.00, 1594.20, 0.00, 1, 'Homesteads'),
(9102, 5500.00, -2790.00, 1462.70, 3.10, 1, 'HomesteadsHyjalCrater'),
(9103, 128.00, 135.00, 236.00, 1.60, 37, 'HomesteadsCrater'),
(9104, 3738.00, -4300.00, 129.00, 0.00, 0, 'HomesteadsPlateau');

-- ── 3. People at the new stops and couriers in the other capitals ────────────────────────────
-- 91001 trainer, 91002 supplier, 91003 steward, 91004 courier (03_construction.sql), 6491 Spirit
-- Healer. City couriers stand beside each flight master: VERIFY none is in a wall.
DELETE FROM `creature` WHERE `guid` BETWEEN 9260001 AND 9260199;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
-- The Hyjal summit: a spirit healer at the gate; a courier and a spirit healer on the crater floor.
(9260001, 6491, 1, 5378.0, -3708.0, 1594.0, 0.0, 300, 300, 0, 0),
(9260002, 91004, 1, 5504.0, -2784.0, 1461.3, 3.6, 300, 300, 0, 0),
(9260003, 6491, 1, 5496.0, -2796.0, 1463.5, 2.0, 300, 300, 0, 0),
-- Azshara Crater: the hub (game_tele AzsharaCrater) and the south canyon.
(9260011, 91003, 37, 124.0, 140.0, 237.0, 5.5, 300, 300, 0, 0),
(9260012, 91004, 37, 132.0, 140.0, 236.5, 4.0, 300, 300, 0, 0),
(9260013, 91001, 37, 124.0, 128.0, 233.6, 0.8, 300, 300, 0, 0),
(9260014, 91002, 37, 132.0, 128.0, 233.3, 2.4, 300, 300, 0, 0),
(9260015, 6491, 37, 121.0, 134.0, 234.3, 0.0, 300, 300, 0, 0),
(9260016, 91004, 37, -526.0, -206.0, 320.5, 3.6, 300, 300, 0, 0),
(9260017, 6491, 37, -534.0, -194.0, 323.8, 3.6, 300, 300, 0, 0),
-- The Thalassian plateau: the gate, in the road between the small and standard plots.
(9260021, 91003, 0, 3746.0, -4296.0, 129.0, 3.1, 300, 300, 0, 0),
(9260022, 91004, 0, 3734.0, -4296.0, 129.0, 0.0, 300, 300, 0, 0),
(9260023, 91001, 0, 3746.0, -4304.0, 129.0, 3.1, 300, 300, 0, 0),
(9260024, 91002, 0, 3734.0, -4304.0, 129.0, 0.0, 300, 300, 0, 0),
(9260025, 6491, 0, 3728.0, -4300.0, 129.0, 0.0, 300, 300, 0, 0),
-- Couriers in the capitals 03 doesn't cover (WORLD_BUILDING.md 4: Ironforge, Undercity,
-- Darnassus, Thunder Bluff) and at Light's Hope Chapel for the Plaguelands.
(9260031, 91004, 0, -4823.5, -1155.0, 502.3, 4.3, 300, 300, 0, 0),    -- Ironforge, by Gryth Thurden
(9260032, 91004, 0, 1569.5, 268.5, -43.0, 0.8, 300, 300, 0, 0),       -- Undercity, by Michael Garrett
(9260033, 91004, 1, 8643.0, 843.0, 23.4, 0.1, 300, 300, 0, 0),        -- Rut'theran Village, by Vesprystus
(9260034, 91004, 1, -1194.0, 23.5, 177.0, 1.7, 300, 300, 0, 0),       -- Thunder Bluff, by Tal
(9260035, 91004, 0, 2272.5, -5343.0, 87.0, 1.8, 300, 300, 0, 0);      -- Light's Hope Chapel, by Khaelyn Steelwing

-- ── 4. Graveyards ───────────────────────────────────────────────────────────────────────────
-- Zone 268 (Azshara Crater): 49 "Plains of Snow - Horde Start" for the Horde, 269 "... Alliance
-- Start" for the Alliance. Zone 616 (Hyjal): Everlook (511), far below, as a fallback only.
DELETE FROM `game_graveyard_zone` WHERE (`id` IN (49, 269) AND `ghost_zone` = 268) OR (`id` = 511 AND `ghost_zone` = 616);
INSERT INTO `game_graveyard_zone` (`id`, `ghost_zone`, `faction`, `patch_min`, `patch_max`) VALUES
(49, 268, 67, 0, 10),
(269, 268, 469, 0, 10),
(511, 616, 0, 0, 10);

-- ── 5. Launch points (Riding.h; the server falls back to Riding::DefaultLaunchPoints) ────────
CREATE TABLE IF NOT EXISTS `fusion_launch_point` (
  `id` smallint(5) unsigned NOT NULL,
  `name` varchar(64) NOT NULL,
  `map` smallint(5) unsigned NOT NULL,
  `x` float NOT NULL, `y` float NOT NULL, `z` float NOT NULL,
  `radius` float NOT NULL COMMENT 'a mounted jump this near takes off',
  `burst_seconds` float NOT NULL COMMENT 'at most 20',
  `ceiling_z` float NOT NULL COMMENT 'the flight ends above it',
  `range` float NOT NULL COMMENT 'the flight ends this far from the point',
  PRIMARY KEY (`id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='World of Modern Warfare: launch points for brief flight';

DELETE FROM `fusion_launch_point`;
INSERT INTO `fusion_launch_point` VALUES
(1, 'the Stormwind gryphon tower', 0, -8835.8, 490.1, 109.7, 10, 18, 190, 300),
(2, 'the Dun Morogh airfield', 0, -4790, -1660, 504, 30, 20, 640, 450),
(3, 'the Orgrimmar wind rider tower', 1, 1676.2, -4313.5, 61.7, 10, 18, 150, 300),
(4, 'the Thunder Bluff wind rider rise', 1, -1196.8, 26.1, 177.0, 10, 18, 260, 300),
(5, 'the ruins of Lordaeron', 0, 1630, 238, 61.7, 15, 15, 140, 250),
(6, 'Rut''theran Village', 1, 8640.6, 841.1, 23.4, 10, 15, 120, 300),
(7, 'the Hyjal homesteads'' gate', 1, 5385, -3710, 1594.2, 15, 15, 1700, 600),
(8, 'the Hyjal ledge over Felwood', 1, 5234, -1220, 1366.8, 15, 20, 1420, 500),
(9, 'the Azshara Crater hub', 37, 128, 135, 236, 15, 20, 600, 900),
(10, 'the Thalassian plateau''s gate', 0, 3738, -4300, 129, 15, 20, 260, 700),
(11, 'Light''s Hope Chapel', 0, 2269.9, -5345.4, 87, 10, 15, 200, 300);
