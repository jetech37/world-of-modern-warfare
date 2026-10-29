-- Azeroth Warfare: Construction, woodcutting and plots (server/src/fusion/include/fusion/Construction.h,
-- Plots.h; docs in server/src/fusion/docs/CONSTRUCTION.md). Apply to the world database after the
-- vmangos world dump; safe to apply again (each block clears its own id range first).
--
-- Reserved ranges:
--   item_template.entry          91001-91099
--   creature_template.entry      91001-91099
--   gameobject_template.entry    191001-191099 (gameobject_loot_template.entry the same)
--   gameobject.guid              4191001-4191999
--   creature.guid                2591001-2591099
--   fusion_recipe.id             91001-91099
--   game_tele.id                 9101
--
-- Rows marked VERIFY need a look in game: display ids picked from similar vanilla items and
-- objects, and every spawn position.

-- ── Parts the fusion crafts ─────────────────────────────────────────────────────────────────────
-- Vanilla's recipes are spells in the client's Spell.dbc, which we can't add to; the fusion crafts
-- these itself (a crafting window in the fusion UI), checking `skill_line` against the player's
-- own skill (Blacksmithing 164 and Engineering 202 are vanilla's; 9001 is Construction, kept in
-- the characters table `character_fusion_skill`). Colours as vanilla's: orange from `required`,
-- yellow, green, gray (Construction.h Thresholds).
CREATE TABLE IF NOT EXISTS `fusion_recipe` (
  `id` mediumint(8) unsigned NOT NULL,
  `name` varchar(100) NOT NULL DEFAULT '',
  `skill_line` smallint(5) unsigned NOT NULL COMMENT '164 Blacksmithing, 202 Engineering, 9001 Construction',
  `required` smallint(5) unsigned NOT NULL DEFAULT '1',
  `yellow` smallint(5) unsigned NOT NULL DEFAULT '25',
  `green` smallint(5) unsigned NOT NULL DEFAULT '50',
  `gray` smallint(5) unsigned NOT NULL DEFAULT '75',
  `product` mediumint(8) unsigned NOT NULL,
  `product_count` tinyint(3) unsigned NOT NULL DEFAULT '1',
  `reagent1` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `count1` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `reagent2` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `count2` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `reagent3` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `count3` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `reagent4` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `count4` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `cast_ms` mediumint(8) unsigned NOT NULL DEFAULT '3000' COMMENT 'how long making one takes',
  `needs_anvil` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT '1: an anvil (spell focus 1) within 10 yards',
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 COMMENT='Azeroth Warfare: recipes the fusion crafts';

DELETE FROM `fusion_recipe` WHERE `id` BETWEEN 91001 AND 91099;
INSERT INTO `fusion_recipe` (`id`, `name`, `skill_line`, `required`, `yellow`, `green`, `gray`, `product`, `product_count`,
  `reagent1`, `count1`, `reagent2`, `count2`, `reagent3`, `count3`, `reagent4`, `count4`, `cast_ms`, `needs_anvil`) VALUES
-- Blacksmithing: Copper Bar 2840, Bronze Bar 2841, Iron Bar 3575, Mithril Bar 3860, Thorium Bar 12359.
(91001, 'Copper Nails', 164, 1, 30, 45, 60, 91020, 5, 2840, 1, 0, 0, 0, 0, 0, 0, 2000, 1),
(91002, 'Bronze Hinge', 164, 90, 115, 130, 145, 91021, 2, 2841, 1, 0, 0, 0, 0, 0, 0, 3000, 1),
(91003, 'Iron Bracket', 164, 150, 170, 185, 200, 91022, 2, 3575, 1, 0, 0, 0, 0, 0, 0, 3000, 1),
(91004, 'Reinforced Plate', 164, 200, 220, 235, 250, 91023, 1, 3860, 2, 91022, 1, 0, 0, 0, 0, 3000, 1),
(91005, 'Sentry Tripod', 164, 225, 245, 260, 275, 91027, 1, 3860, 4, 91022, 2, 0, 0, 0, 0, 5000, 1),
(91006, 'Fortified Plate', 164, 260, 275, 290, 305, 91024, 1, 12359, 2, 91023, 1, 0, 0, 0, 0, 5000, 1),
-- Engineering: Bronze Tube 4371, Handful of Copper Bolts 4359, Gyrochronatom 4389, Bronze Framework 4382,
-- Mithril Tube 10559.
(91007, 'Sentry Barrel Assembly', 202, 175, 195, 210, 225, 91025, 1, 4371, 2, 3575, 2, 4359, 2, 0, 0, 5000, 0),
(91008, 'Sentry Targeting Unit', 202, 200, 220, 235, 250, 91026, 1, 4389, 1, 4382, 1, 10559, 1, 0, 0, 5000, 0);

-- ── Items ───────────────────────────────────────────────────────────────────────────────────────
DELETE FROM `item_template` WHERE `entry` BETWEEN 91001 AND 91099;
-- Lumber (Trade Goods), from the lumber nodes below. Material 2: wood.
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(91001, 0, 7, 0, 'Rough Lumber', 'Cut from softwood. Builders use it.', 21102, 1, 0, 1, 20, 5, 0, 5, 0, 0, 20, 0, 2),        -- VERIFY display (Simple Wood's)
(91002, 0, 7, 0, 'Sturdy Lumber', 'Cut from sturdy timber. Builders use it.', 13495, 1, 0, 1, 100, 25, 0, 20, 0, 0, 20, 0, 2), -- VERIFY display (Whitestone Oak Lumber's)
(91003, 0, 7, 0, 'Hardwood Plank', 'Cut from hardwood. Builders use it.', 7290, 1, 0, 1, 400, 100, 0, 35, 0, 0, 20, 0, 2),   -- VERIFY display (Star Wood's)
(91004, 0, 7, 0, 'Ironwood Plank', 'Cut from ironwood. Builders use it.', 929, 2, 0, 1, 1000, 250, 0, 50, 0, 0, 20, 0, 2);   -- VERIFY display (Splintered Log's)

-- The woodcutting axe: a one-handed axe a woodcutter keeps in the bags, like a Mining Pick (Lumberjack
-- Axe 768's model).
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`,
  `material`, `delay`, `dmg_min1`, `dmg_max1`, `dmg_type1`, `max_durability`, `sheath`) VALUES
(91010, 0, 2, 0, 'Woodcutting Axe', 'Woodcutters need an axe for felling lumber trees.', 5012, 1, 0, 1, 100, 20, 13, 5, 1, 1, 0,
  1, 2600, 3, 6, 0, 25, 3);

-- Parts (Trade Goods, Parts). Copper Nails are also sold by the construction supplier, dearer than a smith's.
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(91020, 0, 7, 1, 'Copper Nails', 'Used in construction.', 10700, 1, 0, 5, 125, 5, 0, 5, 0, 0, 100, 0, 1),           -- Handful of Copper Bolts' look
(91021, 0, 7, 1, 'Bronze Hinge', 'Used in construction.', 7840, 1, 0, 1, 200, 50, 0, 25, 0, 0, 20, 0, 1),          -- VERIFY display (Whirring Bronze Gizmo's)
(91022, 0, 7, 1, 'Iron Bracket', 'Used in construction.', 7377, 1, 0, 1, 400, 100, 0, 35, 0, 0, 20, 0, 1),         -- Iron Strut's look
(91023, 0, 7, 1, 'Reinforced Plate', 'Used in construction.', 20620, 1, 0, 1, 1600, 400, 0, 45, 0, 0, 20, 0, 1),   -- Mithril Casing's look
(91024, 0, 7, 1, 'Fortified Plate', 'Used in construction.', 26611, 2, 0, 1, 4000, 1000, 0, 55, 0, 0, 20, 0, 1),   -- VERIFY display (Thorium Widget's)
(91025, 0, 7, 1, 'Sentry Barrel Assembly', 'Part of a sentry gun.', 19481, 2, 0, 1, 6000, 1500, 0, 45, 0, 0, 5, 0, 1),  -- Thorium Tube's look
(91026, 0, 7, 1, 'Sentry Targeting Unit', 'Part of a sentry gun.', 16367, 2, 0, 1, 6000, 1500, 0, 45, 0, 0, 5, 0, 1),   -- VERIFY display (Fused Wiring's)
(91027, 0, 7, 1, 'Sentry Tripod', 'Part of a sentry gun.', 20624, 2, 0, 1, 4000, 1000, 0, 45, 0, 0, 5, 0, 1);           -- Bronze Framework's look

-- Plot deeds: the steward's receipt for a bought plot (Plots.h). Soulbound, unique, worth nothing to
-- a vendor; the plot itself lives in the characters table `character_fusion_plot`.
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(91030, 0, 15, 0, 'Deed to a Small Homestead', 'A plot of 24 yards on the Hyjal summit. Show it to a Plot Steward.', 15274, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7),
(91031, 0, 15, 0, 'Deed to a Homestead', 'A plot of 40 yards on the Hyjal summit. Show it to a Plot Steward.', 15274, 2, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7),
(91032, 0, 15, 0, 'Deed to a Large Homestead', 'A plot of 64 yards on the Hyjal summit. Show it to a Plot Steward.', 15274, 3, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 7);

-- ── Lumber nodes ────────────────────────────────────────────────────────────────────────────────
-- Type 3 (chest) like vanilla's herbs and veins: data0 lock, data1 loot, data3 consumable (it
-- despawns when looted and comes back on its spawn timer). Vanilla's veins are locked by a Lock.dbc
-- row of the mining lock type, which carries the Mining skill and the pick; the client ships
-- Lock.dbc and LockType.dbc, so a new "Woodcutting" lock can't be added. The nodes are left
-- unlocked (data0 0) and the fusion gates them instead: script `go_fusion_lumber` refuses the use
-- unless Construction::CanGather passes (Construction learned, a Woodcutting Axe in the bags, the
-- skill), then grants the skill-up (docs/CONSTRUCTION.md).
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 191001 AND 191099;
INSERT INTO `gameobject_template` (`entry`, `patch`, `type`, `displayId`, `name`, `faction`, `flags`, `size`,
  `data0`, `data1`, `data2`, `data3`, `data4`, `data5`, `script_name`) VALUES
(191001, 0, 3, 1248, 'Softwood Tree', 0, 0, 1, 0, 191001, 0, 1, 0, 0, 'go_fusion_lumber'),      -- VERIFY display (a lumber pile)
(191002, 0, 3, 1248, 'Sturdy Timber', 0, 0, 1.2, 0, 191002, 0, 1, 0, 0, 'go_fusion_lumber'),
(191003, 0, 3, 1108, 'Hardwood Tree', 0, 0, 1, 0, 191003, 0, 1, 0, 0, 'go_fusion_lumber'),      -- VERIFY display (a cut woodpile)
(191004, 0, 3, 1108, 'Ironwood Tree', 0, 0, 1.2, 0, 191004, 0, 1, 0, 0, 'go_fusion_lumber');

-- What a cut gives: the node's own lumber always (Construction.h's yields), now and then some of
-- the tier below.
DELETE FROM `gameobject_loot_template` WHERE `entry` BETWEEN 191001 AND 191099;
INSERT INTO `gameobject_loot_template` (`entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount`) VALUES
(191001, 91001, 100, 0, 2, 4),
(191002, 91002, 100, 0, 2, 4),
(191002, 91001, 20, 0, 1, 2),
(191003, 91003, 100, 0, 2, 3),
(191003, 91002, 15, 0, 1, 2),
(191004, 91004, 100, 0, 1, 3),
(191004, 91003, 15, 0, 1, 2);

-- Spawns: eight a zone, beside herb spawns of the same zone (3 yards along x) so each stands on
-- ground a player reaches. Softwood (1-75) in Elwynn, Teldrassil, Tirisfal, Durotar; sturdy
-- timber (75-150) in Duskwood, Ashenvale, Silverpine; hardwood (150-225) in Feralas and
-- Stranglethorn; ironwood (225-300) in Azshara, Winterspring and Un'Goro. VERIFY every one in
-- game (.go xyz): the height is the herb's, and the spot may want nudging to sit by a real tree.
DELETE FROM `gameobject` WHERE `guid` BETWEEN 4191001 AND 4191999;
INSERT INTO `gameobject` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `rotation0`, `rotation1`, `rotation2`, `rotation3`, `spawntimesecsmin`, `spawntimesecsmax`, `animprogress`, `state`) VALUES
-- Elwynn Forest
(4191001, 191001, 0, -9522.54, -597.31, 61.85, 0.0, 0, 0, 0.0, 1.0, 300, 450, 100, 1),  -- by herb spawn 26967
(4191002, 191001, 0, -9816.25, -1392.68, 53.76, 2.4, 0, 0, 0.932039, 0.362358, 300, 450, 100, 1),  -- by herb spawn 1838
(4191003, 191001, 0, -9102.53, -1322.45, 109.18, 4.7999, 0, 0, 0.6755, -0.73736, 300, 450, 100, 1),  -- by herb spawn 68551
(4191004, 191001, 0, -9124.98, -15.85, 85.70, 0.9167, 0, 0, 0.442469, 0.896784, 300, 450, 100, 1),  -- by herb spawn 748
(4191005, 191001, 0, -9893.43, -96.58, 29.15, 3.3167, 0, 0, 0.99617, -0.087442, 300, 450, 100, 1),  -- by herb spawn 46547
(4191006, 191001, 0, -9112.96, -828.88, 70.30, 5.7166, 0, 0, 0.279519, -0.96014, 300, 450, 100, 1),  -- by herb spawn 26987
(4191007, 191001, 0, -9850.78, -952.62, 37.35, 1.8334, 0, 0, 0.793598, 0.608442, 300, 450, 100, 1),  -- by herb spawn 43843
(4191008, 191001, 0, -9538.16, -232.51, 60.56, 4.2333, 0, 0, 0.854684, -0.519148, 300, 450, 100, 1),  -- by herb spawn 30851
-- Teldrassil
(4191009, 191001, 1, 9748.44, 1840.75, 1302.00, 0.3501, 0, 0, 0.174157, 0.984718, 300, 450, 100, 1),  -- by herb spawn 11006
(4191010, 191001, 1, 10097.60, 312.47, 1326.69, 2.7501, 0, 0, 0.980903, 0.194499, 300, 450, 100, 1),  -- by herb spawn 49644
(4191011, 191001, 1, 9317.82, 943.73, 1302.86, 5.15, 0, 0, 0.53676, -0.843735, 300, 450, 100, 1),  -- by herb spawn 11621
(4191012, 191001, 1, 10488.30, 1441.48, 1329.91, 1.2668, 0, 0, 0.591889, 0.80602, 300, 450, 100, 1),  -- by herb spawn 49634
(4191013, 191001, 1, 9977.96, 939.73, 1325.99, 3.6668, 0, 0, 0.965717, -0.259596, 300, 450, 100, 1),  -- by herb spawn 49648
(4191014, 191001, 1, 9345.01, 1491.86, 1273.54, 6.0667, 0, 0, 0.108031, -0.994147, 300, 450, 100, 1),  -- by herb spawn 43908
(4191015, 191001, 1, 9628.06, 552.73, 1320.63, 2.1835, 0, 0, 0.887435, 0.460933, 300, 450, 100, 1),  -- by herb spawn 11948
(4191016, 191001, 1, 10021.40, 1455.01, 1281.14, 4.5835, 0, 0, 0.751177, -0.660101, 300, 450, 100, 1),  -- by herb spawn 58513
-- Tirisfal Glades
(4191017, 191001, 0, 2351.53, -211.14, 39.21, 0.7002, 0, 0, 0.342992, 0.939338, 300, 450, 100, 1),  -- by herb spawn 1715
(4191018, 191001, 0, 2447.71, 1443.39, 36.09, 3.1002, 0, 0, 0.999786, 0.020695, 300, 450, 100, 1),  -- by herb spawn 27746
(4191019, 191001, 0, 1704.92, 705.04, 63.81, 5.5002, 0, 0, 0.381569, -0.924341, 300, 450, 100, 1),  -- by herb spawn 500
(4191020, 191001, 0, 2788.70, 501.15, 26.61, 1.6169, 0, 0, 0.723218, 0.69062, 300, 450, 100, 1),  -- by herb spawn 1623
(4191021, 191001, 0, 1764.84, -737.91, 59.94, 4.0169, 0, 0, 0.905749, -0.423815, 300, 450, 100, 1),  -- by herb spawn 1766
(4191022, 191001, 0, 2755.14, -776.59, 136.01, 0.1337, 0, 0, 0.0668, 0.997766, 300, 450, 100, 1),  -- by herb spawn 45254
(4191023, 191001, 0, 2288.03, 847.53, 37.17, 2.5336, 0, 0, 0.954148, 0.299336, 300, 450, 100, 1),  -- by herb spawn 45055
(4191024, 191001, 0, 2120.83, 295.34, 60.94, 4.9336, 0, 0, 0.624735, -0.780837, 300, 450, 100, 1),  -- by herb spawn 1334
-- Durotar
(4191025, 191001, 1, 148.65, -4619.62, 29.96, 1.0504, 0, 0, 0.501386, 0.865224, 300, 450, 100, 1),  -- by herb spawn 12480
(4191026, 191001, 1, 1153.29, -4129.90, 20.57, 3.4503, 0, 0, 0.988111, -0.153741, 300, 450, 100, 1),  -- by herb spawn 12533
(4191027, 191001, 1, -942.42, -4816.15, 13.90, 5.8503, 0, 0, 0.214757, -0.976668, 300, 450, 100, 1),  -- by herb spawn 12479
(4191028, 191001, 1, 824.03, -4890.18, 35.38, 1.9671, 0, 0, 0.83247, 0.554071, 300, 450, 100, 1),  -- by herb spawn 10397
(4191029, 191001, 1, 547.14, -4190.09, 16.03, 4.367, 0, 0, 0.818096, -0.575081, 300, 450, 100, 1),  -- by herb spawn 12476
(4191030, 191001, 1, -407.61, -4662.34, 42.36, 0.4838, 0, 0, 0.239548, 0.970885, 300, 450, 100, 1),  -- by herb spawn 46565
(4191031, 191001, 1, 15.63, -4112.04, 68.99, 2.8838, 0, 0, 0.991704, 0.12854, 300, 450, 100, 1),  -- by herb spawn 58539
(4191032, 191001, 1, 1156.60, -4530.12, 19.54, 5.2837, 0, 0, 0.4792, -0.877706, 300, 450, 100, 1),  -- by herb spawn 43851
-- Duskwood
(4191033, 191002, 0, -10595.50, -952.97, 53.28, 1.4005, 0, 0, 0.644409, 0.764681, 450, 600, 100, 1),  -- by herb spawn 43353
(4191034, 191002, 0, -10262.80, 266.94, 39.25, 3.8005, 0, 0, 0.946219, -0.323526, 450, 600, 100, 1),  -- by herb spawn 38555
(4191035, 191002, 0, -10945.90, -185.26, 37.76, 6.2004, 0, 0, 0.041381, -0.999143, 450, 600, 100, 1),  -- by herb spawn 42482
(4191036, 191002, 0, -10915.70, -1349.68, 54.65, 2.3172, 0, 0, 0.916243, 0.400623, 450, 600, 100, 1),  -- by herb spawn 33491
(4191037, 191002, 0, -10284.70, -1316.91, 49.29, 4.7172, 0, 0, 0.705404, -0.708806, 450, 600, 100, 1),  -- by herb spawn 38546
(4191038, 191002, 0, -10259.60, -208.18, 71.92, 0.8339, 0, 0, 0.404974, 0.914328, 450, 600, 100, 1),  -- by herb spawn 16986
(4191039, 191002, 0, -10950.20, 255.44, 28.46, 3.2339, 0, 0, 0.998935, -0.046137, 450, 600, 100, 1),  -- by herb spawn 40334
(4191040, 191002, 0, -10248.40, -700.82, 46.99, 5.6338, 0, 0, 0.319018, -0.947749, 450, 600, 100, 1),  -- by herb spawn 24183
-- Ashenvale
(4191041, 191002, 1, 2511.07, -1679.79, 143.55, 1.7506, 0, 0, 0.767736, 0.640767, 450, 600, 100, 1),  -- by herb spawn 43512
(4191042, 191002, 1, 3466.65, -57.88, 4.58, 4.1506, 0, 0, 0.875414, -0.483373, 450, 600, 100, 1),  -- by herb spawn 58822
(4191043, 191002, 1, 2010.82, -85.13, 114.97, 0.2674, 0, 0, 0.133302, 0.991075, 450, 600, 100, 1),  -- by herb spawn 47927
(4191044, 191002, 1, 3054.65, -2787.59, 199.60, 2.6673, 0, 0, 0.972012, 0.23493, 450, 600, 100, 1),  -- by herb spawn 24292
(4191045, 191002, 1, 1808.31, -2657.99, 117.38, 5.0673, 0, 0, 0.57118, -0.820825, 450, 600, 100, 1),  -- by herb spawn 68465
(4191046, 191002, 1, 3559.59, -1425.08, 192.13, 1.184, 0, 0, 0.558022, 0.829826, 450, 600, 100, 1),  -- by herb spawn 56229
(4191047, 191002, 1, 2855.96, -785.46, 156.36, 3.584, 0, 0, 0.975634, -0.219404, 450, 600, 100, 1),  -- by herb spawn 43506
(4191048, 191002, 1, 2075.23, -909.10, 106.51, 5.984, 0, 0, 0.149035, -0.988832, 450, 600, 100, 1),  -- by herb spawn 38706
-- Silverpine Forest
(4191049, 191002, 0, 798.12, 1585.94, 33.83, 2.1007, 0, 0, 0.867597, 0.497267, 450, 600, 100, 1),  -- by herb spawn 35526
(4191050, 191002, 0, -265.34, 815.99, 76.85, 4.5007, 0, 0, 0.777853, -0.628446, 450, 600, 100, 1),  -- by herb spawn 58448
(4191051, 191002, 0, 1339.83, 743.91, 34.28, 0.6175, 0, 0, 0.303868, 0.952714, 450, 600, 100, 1),  -- by herb spawn 2132
(4191052, 191002, 0, -250.46, 1531.90, 79.47, 3.0174, 0, 0, 0.998073, 0.062056, 450, 600, 100, 1),  -- by herb spawn 68446
(4191053, 191002, 0, 360.36, 713.06, 45.08, 5.4174, 0, 0, 0.419498, -0.907756, 450, 600, 100, 1),  -- by herb spawn 1597
(4191054, 191002, 0, 1210.02, 2047.57, 4.11, 1.5342, 0, 0, 0.69405, 0.719926, 450, 600, 100, 1),  -- by herb spawn 58558
(4191055, 191002, 0, 278.48, 1292.72, 79.59, 3.9341, 0, 0, 0.922513, -0.385965, 450, 600, 100, 1),  -- by herb spawn 1666
(4191056, 191002, 0, 1207.60, 1263.11, 42.56, 0.0509, 0, 0, 0.025447, 0.999676, 450, 600, 100, 1),  -- by herb spawn 42962
-- Feralas
(4191057, 191003, 1, -4654.48, 1604.65, 115.78, 2.4509, 0, 0, 0.940958, 0.338523, 600, 900, 100, 1),  -- by herb spawn 16067
(4191058, 191003, 1, -4182.15, 514.30, 60.82, 4.8508, 0, 0, 0.656518, -0.754311, 600, 900, 100, 1),  -- by herb spawn 49993
(4191059, 191003, 1, -5147.22, 644.62, 47.91, 0.9676, 0, 0, 0.465146, 0.885234, 600, 900, 100, 1),  -- by herb spawn 68903
(4191060, 191003, 1, -3798.20, 2052.18, 74.90, 3.3676, 0, 0, 0.993622, -0.112763, 600, 900, 100, 1),  -- by herb spawn 8832
(4191061, 191003, 1, -3820.00, 1222.52, 150.91, 5.7675, 0, 0, 0.254995, -0.966942, 600, 900, 100, 1),  -- by herb spawn 29445
(4191062, 191003, 1, -5083.44, 2110.42, 30.41, 1.8843, 0, 0, 0.808824, 0.58805, 600, 900, 100, 1),  -- by herb spawn 58811
(4191063, 191003, 1, -5172.75, 1287.60, 49.83, 4.2843, 0, 0, 0.84117, -0.540771, 600, 900, 100, 1),  -- by herb spawn 68904
(4191064, 191003, 1, -4486.16, 2211.10, 15.08, 0.401, 0, 0, 0.199159, 0.979967, 600, 900, 100, 1),  -- by herb spawn 42352
-- Stranglethorn Vale
(4191065, 191003, 0, -12517.10, -720.78, 41.08, 2.801, 0, 0, 0.985535, 0.169474, 600, 900, 100, 1),  -- by herb spawn 11677
(4191066, 191003, 0, -13894.60, 679.25, 10.03, 5.201, 0, 0, 0.515073, -0.857146, 600, 900, 100, 1),  -- by herb spawn 34041
(4191067, 191003, 0, -11586.20, 898.21, 39.78, 1.3177, 0, 0, 0.612208, 0.790697, 600, 900, 100, 1),  -- by herb spawn 58597
(4191068, 191003, 0, -12835.90, 366.57, 23.62, 3.7177, 0, 0, 0.958799, -0.284087, 600, 900, 100, 1),  -- by herb spawn 7965
(4191069, 191003, 0, -13624.40, -333.38, 9.63, 6.1176, 0, 0, 0.082698, -0.996575, 600, 900, 100, 1),  -- by herb spawn 68838
(4191070, 191003, 0, -11513.30, -453.81, 41.22, 2.2344, 0, 0, 0.898877, 0.438201, 600, 900, 100, 1),  -- by herb spawn 58295
(4191071, 191003, 0, -11994.50, 193.18, 12.67, 4.6344, 0, 0, 0.734136, -0.679003, 600, 900, 100, 1),  -- by herb spawn 43450
(4191072, 191003, 0, -12971.70, -292.99, 12.71, 0.7512, 0, 0, 0.366831, 0.930288, 600, 900, 100, 1),  -- by herb spawn 42270
-- Azshara
(4191073, 191004, 1, 3487.41, -5216.15, 85.98, 3.1511, 0, 0, 0.999989, -0.004754, 900, 1200, 100, 1),  -- by herb spawn 68830
(4191074, 191004, 1, 4705.16, -7436.10, 74.80, 5.5511, 0, 0, 0.357923, -0.933751, 900, 1200, 100, 1),  -- by herb spawn 6317
(4191075, 191004, 1, 2549.52, -6899.08, 147.12, 1.6679, 0, 0, 0.740591, 0.671956, 900, 1200, 100, 1),  -- by herb spawn 66721
(4191076, 191004, 1, 4776.87, -5952.30, 94.85, 4.0678, 0, 0, 0.89467, -0.446727, 900, 1200, 100, 1),  -- by herb spawn 68871
(4191077, 191004, 1, 2692.89, -4126.37, 128.58, 0.1846, 0, 0, 0.092169, 0.995743, 900, 1200, 100, 1),  -- by herb spawn 42205
(4191078, 191004, 1, 2590.47, -5770.75, 101.55, 2.5845, 0, 0, 0.961456, 0.274958, 900, 1200, 100, 1),  -- by herb spawn 68879
(4191079, 191004, 1, 3622.93, -4289.52, 101.91, 4.9845, 0, 0, 0.604663, -0.796481, 900, 1200, 100, 1),  -- by herb spawn 13692
(4191080, 191004, 1, 2618.51, -4944.38, 126.45, 1.1013, 0, 0, 0.523241, 0.852185, 900, 1200, 100, 1),  -- by herb spawn 18315
-- Winterspring
(4191081, 191004, 1, 6581.92, -4341.99, 691.95, 3.5012, 0, 0, 0.983879, -0.178836, 900, 1200, 100, 1),  -- by herb spawn 29054
(4191082, 191004, 1, 6317.28, -2348.73, 587.45, 5.9012, 0, 0, 0.189834, -0.981816, 900, 1200, 100, 1),  -- by herb spawn 66799
(4191083, 191004, 1, 8152.54, -3712.11, 742.03, 2.018, 0, 0, 0.8463, 0.532707, 900, 1200, 100, 1),  -- by herb spawn 49216
(4191084, 191004, 1, 4966.60, -4681.59, 896.60, 4.4179, 0, 0, 0.803197, -0.595713, 900, 1200, 100, 1),  -- by herb spawn 66795
(4191085, 191004, 1, 7566.06, -5089.49, 758.55, 0.5347, 0, 0, 0.264177, 0.964474, 900, 1200, 100, 1),  -- by herb spawn 49116
(4191086, 191004, 1, 6943.44, -3354.29, 750.91, 2.9347, 0, 0, 0.994654, 0.103262, 900, 1200, 100, 1),  -- by herb spawn 68784
(4191087, 191004, 1, 6116.66, -5209.13, 885.37, 5.3346, 0, 0, 0.456709, -0.889616, 900, 1200, 100, 1),  -- by herb spawn 49105
(4191088, 191004, 1, 5756.22, -4379.39, 795.88, 1.4514, 0, 0, 0.663659, 0.748035, 900, 1200, 100, 1),  -- by herb spawn 29043
-- Un Goro Crater
(4191089, 191004, 1, -7144.22, -1173.04, -243.57, 3.8514, 0, 0, 0.93768, -0.3475, 900, 1200, 100, 1),  -- by herb spawn 68775
(4191090, 191004, 1, -6982.03, -2384.21, -221.71, 6.2513, 0, 0, 0.015942, -0.999873, 900, 1200, 100, 1),  -- by herb spawn 17750
(4191091, 191004, 1, -6161.92, -1572.70, -208.78, 2.3681, 0, 0, 0.926141, 0.377177, 900, 1200, 100, 1),  -- by herb spawn 19616
(4191092, 191004, 1, -7941.49, -1863.99, -250.35, 4.7681, 0, 0, 0.687138, -0.726527, 900, 1200, 100, 1),  -- by herb spawn 68774
(4191093, 191004, 1, -7905.18, -535.41, -231.77, 0.8848, 0, 0, 0.42811, 0.903727, 900, 1200, 100, 1),  -- by herb spawn 66775
(4191094, 191004, 1, -6920.29, -214.66, -187.16, 3.2848, 0, 0, 0.997438, -0.071543, 900, 1200, 100, 1),  -- by herb spawn 68778
(4191095, 191004, 1, -6451.92, -921.17, -275.23, 5.6848, 0, 0, 0.294749, -0.955575, 900, 1200, 100, 1),  -- by herb spawn 18395
(4191096, 191004, 1, -7812.27, -1183.70, -261.47, 1.8015, 0, 0, 0.783793, 0.621022, 900, 1200, 100, 1);  -- by herb spawn 16090

-- ── People ──────────────────────────────────────────────────────────────────────────────────────
-- Vanilla's trainers teach spells (npc_trainer), and Construction is no spell, so the trainer is a
-- gossip NPC whose script teaches the tiers (Construction::CanTrain). The supplier is an ordinary
-- vendor. The steward sells, keeps and lets go of plots by gossip (Plots::Buy, PayUpkeep, Grant);
-- the courier takes a player to the homesteads and back. Faction 35: friendly to everyone, as the
-- homesteads take both sides. VERIFY the display ids (picked from vanilla's trade suppliers).
DELETE FROM `creature_template` WHERE `entry` BETWEEN 91001 AND 91099;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`,
  `civilian`, `script_name`) VALUES
(91001, 0, 'Foreman Rivetbolt', 'Construction Trainer', 45, 45, 35, 1, 7179, 1, 100, 100, 7, 1, 1, 'npc_fusion_construction_trainer'),
(91002, 0, 'Hettie Sawdust', 'Construction Supplies', 35, 35, 35, 4, 3482, 1, 100, 100, 7, 1, 1, ''),
(91003, 0, 'Warden Oakhallow', 'Plot Steward', 50, 50, 35, 1, 1298, 1, 100, 100, 7, 1, 1, 'npc_fusion_plot_steward'),
(91004, 0, 'Bramble Fernsby', 'Homestead Courier', 40, 40, 35, 1, 5032, 1, 100, 100, 7, 1, 1, 'npc_fusion_homestead_courier');

-- The supplier: the axe, nails, and vanilla's stones and cloth a builder runs short of.
DELETE FROM `npc_vendor` WHERE `entry` BETWEEN 91001 AND 91099;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`) VALUES
(91002, 0, 91010, 0, 0),    -- Woodcutting Axe
(91002, 1, 91020, 0, 0),    -- Copper Nails
(91002, 2, 2901, 0, 0),     -- Mining Pick
(91002, 3, 5956, 0, 0),     -- Blacksmith Hammer
(91002, 4, 2589, 20, 900),  -- Linen Cloth, a few at a time
(91002, 5, 2835, 20, 900);  -- Rough Stone, a few at a time

-- Spawns: the trainer, supplier and a courier by the blacksmiths of Stormwind (Dwarven District, by
-- Therum Deepforge) and Orgrimmar (Valley of Honor, by Saru Steelfury), and the steward with the
-- trainer, supplier and a courier home at the homesteads' gate on the Hyjal summit (game_tele
-- "HyjalBlizzardConstructionCo"). VERIFY: heights, and that nobody stands in a wall.
DELETE FROM `creature` WHERE `guid` BETWEEN 2591001 AND 2591099;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
-- Stormwind
(2591001, 91001, 0, -8428.90, 612.60, 95.50, 0.65, 300, 300, 0, 0),
(2591002, 91002, 0, -8426.40, 609.90, 95.50, 1.20, 300, 300, 0, 0),
(2591003, 91004, 0, -8431.60, 615.70, 95.50, 0.20, 300, 300, 0, 0),
-- Orgrimmar
(2591011, 91001, 1, 2054.20, -4830.40, 24.44, 4.40, 300, 300, 0, 0),
(2591012, 91002, 1, 2051.90, -4833.30, 24.44, 4.90, 300, 300, 0, 0),
(2591013, 91004, 1, 2056.60, -4827.40, 24.44, 4.10, 300, 300, 0, 0),
-- The homesteads (Hyjal summit, map 1): the gate hub, moved off the standard plots by sql 12
(2591021, 91003, 1, 5388.00, -3716.00, 1595.50, 2.60, 300, 300, 0, 0),
(2591022, 91004, 1, 5381.00, -3716.00, 1595.80, 0.60, 300, 300, 0, 0),
(2591023, 91001, 1, 5389.00, -3704.00, 1593.50, 3.60, 300, 300, 0, 0),
(2591024, 91002, 1, 5382.00, -3703.00, 1593.50, 4.20, 300, 300, 0, 0);

-- A game master's way in: .tele homesteads
DELETE FROM `game_tele` WHERE `id` = 9101 OR `name` = 'Homesteads';
INSERT INTO `game_tele` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES
(9101, 5385.00, -3710.00, 1594.20, 0.00, 1, 'Homesteads');
