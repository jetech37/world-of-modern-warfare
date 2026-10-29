-- Azeroth Warfare: battle companions (src/fusion/include/fusion/Companions.h, docs/COMPANIONS.md).
-- Apply to the world database after the vmangos world dump. Safe to run again: every row in the
-- reserved ranges is deleted and written afresh.
--
-- NOT ON ITS OWN: this file drops and rebuilds `fusion_companion_species` and
-- `fusion_companion_item` and deletes item_template 90001-90199, which wipes what the later
-- companion files add (08's species 101-164, their ride_display column, the starter rescale and
-- catalysts 90105-90107; 20's item texts). Whenever 02 is applied, apply 08_companion_lines.sql,
-- 13_companion_keeper.sql, 15_companion_index.sql and 20_companion_fixes.sql after it, in that
-- order (or simply every file in this folder in order). The server then loads 97 species.
--
-- Reserved item entries: 90001-90099 companion items (one per species, entry = 90000 + species
-- id; from 90060 the mounts' companion items, fusion_companion_item), 90101-90199 evolution
-- catalysts. Companion items are Bind on Equip (bonding 2) and do not
-- stack, so every one is its own item instance: the companion's level, xp and stage live in the
-- characters database (character_fusion_companion, keyed by item guid) and travel with the item
-- when it is traded, mailed or sold. They carry no spell: the server's fusion hook catches the
-- use (and the client's Companions window sends a summon request), see docs/COMPANIONS.md.
-- Drops are rolled by the fusion rules (Companions::RollDrop) at loot time, not loot tables.

-- ── Species ─────────────────────────────────────────────────────────────────────────────────────
-- Dropped and made again: every row is written below, and the ride columns came later.
DROP TABLE IF EXISTS `fusion_companion_species`;
CREATE TABLE `fusion_companion_species` (
  `id` smallint(5) unsigned NOT NULL,
  `name` varchar(64) NOT NULL DEFAULT '',
  `item_entry` mediumint(8) unsigned NOT NULL COMMENT 'item_template entry that summons it',
  `family` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT 'Companions::Family',
  `role` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT '0 melee, 1 ranged, 2 tank',
  `stage1_creature` mediumint(8) unsigned NOT NULL COMMENT 'creature_template entry (model) at stage 1',
  `stage1_scale` float NOT NULL DEFAULT '1',
  `stage2_creature` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT '0 = no such stage',
  `stage2_scale` float NOT NULL DEFAULT '1',
  `stage3_creature` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `stage3_scale` float NOT NULL DEFAULT '1',
  `evo1_level` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT 'stage 1 -> 2; 0 = no evolution',
  `evo1_item` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'catalyst consumed, 0 = none',
  `evo1_item_count` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `evo1_areas` varchar(128) NOT NULL DEFAULT '' COMMENT 'zone or area ids, comma separated; empty = anywhere',
  `evo1_condition` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT '0 none, 1 night, 2 day, 3 kills of a creature type',
  `evo1_condition_type` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT 'creature type for condition 3',
  `evo1_condition_count` smallint(5) unsigned NOT NULL DEFAULT '0',
  `evo2_level` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT 'stage 2 -> 3',
  `evo2_item` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `evo2_item_count` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `evo2_areas` varchar(128) NOT NULL DEFAULT '',
  `evo2_condition` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `evo2_condition_type` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `evo2_condition_count` smallint(5) unsigned NOT NULL DEFAULT '0',
  `drop_entries` varchar(255) NOT NULL DEFAULT '' COMMENT 'creature entries that drop it, comma separated',
  `drop_type` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT 'else any creature of this type (0 = entries only)',
  `drop_family` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT '... with this pet_family (0 = any)',
  `ride_stage` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT 'the stage (1-3) that is a mount; 0 = none',
  `ride_spell` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'vanilla 60% mount spell cast to ride it',
  `ride_swift_spell` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'vanilla 100% mount spell',
  PRIMARY KEY (`id`),
  UNIQUE KEY `item_entry` (`item_entry`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 COMMENT='Azeroth Warfare battle companion species';

-- Mirrors Companions::StarterSpecies() row for row (CompanionTests reads this file to hold them together).
DELETE FROM `fusion_companion_species` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `fusion_companion_species` (`id`, `name`, `item_entry`, `family`, `role`,
  `stage1_creature`, `stage1_scale`, `stage2_creature`, `stage2_scale`, `stage3_creature`, `stage3_scale`,
  `evo1_level`, `evo1_item`, `evo1_item_count`, `evo1_areas`, `evo1_condition`, `evo1_condition_type`, `evo1_condition_count`,
  `evo2_level`, `evo2_item`, `evo2_item_count`, `evo2_areas`, `evo2_condition`, `evo2_condition_type`, `evo2_condition_count`,
  `drop_entries`, `drop_type`, `drop_family`, `ride_stage`, `ride_spell`, `ride_swift_spell`) VALUES
(1, 'Black Whelpling', 90001, 1, 1, 7543, 1, 441, 1, 7044, 0.6, 20, 0, 0, '25,46,51,254,511', 0, 0, 0, 45, 90101, 1, '25,254,1583,511', 0, 0, 0, '441,4323,4324,7047,7048,10161,11262', 0, 0, 0, 0, 0),
(2, 'Crimson Whelpling', 90002, 1, 1, 7544, 1, 1069, 1, 13976, 0.6, 20, 0, 0, '11,1037,1038', 0, 0, 0, 45, 90101, 1, '1037', 0, 0, 0, '1042,1043,1044,1045,1046,1069', 0, 0, 0, 0, 0),
(3, 'Emerald Whelpling', 90003, 1, 1, 7545, 1, 741, 1, 742, 0.7, 20, 0, 0, '856,438,1111,356', 0, 0, 0, 45, 90101, 1, '856,438,1111,356', 1, 0, 0, '740,741,742,5277,5280,5283,8319,8776', 0, 0, 0, 0, 0),
(4, 'Azure Whelpling', 90004, 1, 1, 7547, 1, 10659, 1, 10660, 1, 25, 0, 0, '618,16', 0, 0, 0, 50, 90101, 1, '2245', 0, 0, 0, '193,7227,7435,10659,10660,12200', 0, 0, 0, 0, 0),
(5, 'Sprite Darter Hatchling', 90005, 2, 1, 9662, 1, 5278, 0.75, 5278, 1, 35, 0, 0, '357', 1, 0, 0, 50, 0, 0, '', 0, 0, 0, '5278,7997', 0, 0, 0, 0, 0),
(6, 'Wolf Pup', 90006, 3, 0, 10259, 1, 565, 1, 358, 1, 10, 0, 0, '', 0, 0, 0, 30, 90102, 1, '10', 1, 0, 0, '', 1, 1, 3, 580, 23250),
(7, 'Nightsaber Cub', 90007, 4, 0, 2031, 1, 2042, 1, 7690, 1, 10, 0, 0, '', 0, 0, 0, 30, 90102, 1, '493', 1, 0, 0, '2031,2042,2033', 1, 2, 3, 10793, 23219),
(8, 'Frostsaber Cub', 90008, 4, 0, 7430, 1, 7431, 1, 6074, 1, 25, 0, 0, '618', 0, 0, 0, 50, 90102, 1, '618', 1, 0, 0, '7430,7431,7433', 1, 2, 3, 8394, 23221),
(9, 'Panther Cub', 90009, 4, 0, 683, 0.6, 736, 1, 684, 1, 20, 0, 0, '33', 0, 0, 0, 40, 90102, 1, '33', 3, 7, 30, '683,736,684', 1, 2, 0, 0, 0),
(10, 'Bear Cub', 90010, 5, 2, 6789, 1, 3809, 1, 3810, 1, 15, 0, 0, '', 0, 0, 0, 35, 90102, 1, '331', 0, 0, 0, '6789,822,1128,1186,1196,3809,3810', 1, 4, 0, 0, 0),
(11, 'Young Web Spider', 90011, 6, 0, 1504, 1, 217, 1, 2001, 1, 12, 0, 0, '', 0, 0, 0, 30, 90102, 1, '', 3, 1, 40, '30,43,217,930,1504,1505,2001,4263,4413', 1, 3, 0, 0, 0),
(12, 'Raptor Hatchling', 90012, 7, 0, 3122, 0.6, 3123, 1, 7708, 1, 12, 0, 0, '17,14', 0, 0, 0, 35, 90102, 1, '33', 0, 0, 0, '107,685,855,856,3122,3123', 1, 11, 3, 10799, 23241),
(13, 'Young Crocolisk', 90013, 8, 2, 1417, 0.4, 1151, 0.8, 1151, 1, 30, 0, 0, '15,11,8', 0, 0, 0, 45, 0, 0, '', 0, 0, 0, '1084,1151,1417', 1, 6, 0, 0, 0),
(14, 'Infant Gorilla', 90014, 9, 2, 1108, 0.55, 1557, 1, 1558, 1, 30, 0, 0, '33', 0, 0, 0, 45, 90102, 1, '490', 0, 0, 0, '1108,1511,1557,1558,6514', 1, 9, 0, 0, 0),
(15, 'Owlet', 90015, 10, 0, 7555, 1, 7456, 1, 7456, 0.8, 40, 0, 0, '618,493', 1, 0, 0, 55, 0, 0, '', 0, 0, 0, '7456', 1, 26, 0, 0, 0),
(16, 'Duskbat Pup', 90016, 11, 0, 1512, 1, 8600, 0.8, 8600, 1.15, 40, 0, 0, '28,139', 1, 0, 0, 55, 0, 0, '', 0, 0, 0, '1512,1513,8600', 1, 24, 0, 0, 0),
(17, 'Plainstrider Chick', 90017, 12, 0, 2955, 0.6, 2956, 1, 2957, 1, 5, 0, 0, '', 0, 0, 0, 10, 0, 0, '215', 0, 0, 0, '2321,2955,2956,2957', 1, 12, 0, 0, 0),
(18, 'Scorpid Hatchling', 90018, 13, 2, 3124, 0.6, 4140, 1, 5422, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '440,1377', 0, 0, 0, '3124,4140,4697,5422,5423', 1, 20, 0, 0, 0),
(19, 'Ember Spark', 90019, 14, 1, 17003, 0.5, 6296, 1, 575, 1, 20, 0, 0, '51,46,537', 0, 0, 0, 40, 90103, 1, '537', 0, 0, 0, '575,5850,5852,5855,6296,6520,7031,7032,11321,12076', 0, 0, 0, 0, 0),
(20, 'Water Mote', 90020, 15, 1, 6748, 0.6, 691, 1, 510, 1, 20, 0, 0, '16,1227,15', 0, 0, 0, 45, 90103, 1, '1227', 0, 0, 0, '510,691,3917,5461,6748,10756,10757', 0, 0, 0, 0, 0),
(21, 'Pebble', 90021, 16, 2, 2735, 0.45, 92, 1, 2736, 1, 20, 0, 0, '3', 0, 0, 0, 45, 90103, 1, '3,51', 0, 0, 0, '92,329,2593,2735,2736,2791', 0, 0, 0, 0, 0),
(22, 'Zephyr', 90022, 17, 1, 5898, 0.6, 11576, 1, 11744, 1, 25, 0, 0, '400,1377', 0, 0, 0, 50, 90103, 1, '1377', 0, 0, 0, '832,5898,11576,11577,11744', 0, 0, 0, 0, 0),
(23, 'Murloc Hatchling', 90023, 18, 0, 15186, 1, 735, 1, 127, 1, 10, 0, 0, '40,148,11,38', 0, 0, 0, 25, 0, 0, '', 3, 7, 30, '46,126,127,171,285,456,458,513,515,548,578,735', 0, 0, 0, 0, 0),
(24, 'Fel Cinder', 90024, 19, 2, 4676, 0.5, 6073, 1, 89, 0.8, 30, 90104, 1, '4,361', 0, 0, 0, 50, 90104, 1, '4', 0, 0, 0, '89,4676,6073', 3, 0, 0, 0, 0),
(25, 'Oozeling', 90025, 20, 0, 15429, 1, 8257, 1, 12387, 0.7, 20, 0, 0, '', 0, 0, 0, 45, 0, 0, '28,139', 0, 0, 0, '364,1030,1806,3928,6218,8257,10697,12387', 0, 0, 0, 0, 0),
(26, 'Snow Tiger Kitten', 90026, 0, 0, 7381, 1, 7381, 1.45, 7381, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '2110,4075', 0, 0, 0, 0, 0),
(27, 'Snowshoe Rabbit', 90027, 0, 0, 7560, 1, 7560, 1.8, 7560, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '721,5951', 8, 0, 0, 0, 0),
(28, 'Spider Tank Bot', 90028, 21, 1, 2671, 1, 2671, 1.05, 2671, 0.75, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '', 9, 0, 0, 0, 0),
(29, 'Kodo Calf', 90029, 22, 2, 2972, 0.7, 3236, 1, 11689, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '17', 0, 0, 0, '2972,2973,2974,3234,3236,3237,4700,4701,4702', 0, 0, 3, 18990, 23249),
(30, 'Foal', 90030, 23, 0, 385, 0.4, 385, 0.75, 284, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '12', 2, 0, 0, '385', 0, 0, 3, 458, 23229),
(31, 'Mechanostrider Runt', 90031, 24, 0, 7749, 0.35, 7749, 0.6, 7749, 1, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '1,721', 0, 0, 0, '', 9, 0, 3, 10969, 23223),
(32, 'Skeletal Foal', 90032, 25, 0, 11153, 0.4, 11153, 0.75, 11153, 1, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '85', 1, 0, 0, '', 6, 0, 3, 17462, 17465),
(33, 'Ram Kid', 90033, 26, 2, 2098, 0.4, 2098, 0.8, 4710, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '1', 0, 0, 0, '2098', 0, 0, 3, 6777, 23239);

-- The mounts' companion items: a species at its rideable stage and level 40 (vanilla's riding
-- level), sold where vanilla's mounts are (see docs/COMPANIONS.md, "Mounts"). Mirrors
-- Companions::StarterItems() row for row.
DROP TABLE IF EXISTS `fusion_companion_item`;
CREATE TABLE `fusion_companion_item` (
  `item_entry` mediumint(8) unsigned NOT NULL COMMENT 'item_template entry',
  `species` smallint(5) unsigned NOT NULL,
  `stage` tinyint(3) unsigned NOT NULL DEFAULT '0' COMMENT '0-based stage the item starts at',
  `level` tinyint(3) unsigned NOT NULL DEFAULT '1' COMMENT 'level the item starts at',
  PRIMARY KEY (`item_entry`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 COMMENT='Azeroth Warfare companion items that start further along';
INSERT INTO `fusion_companion_item` (`item_entry`, `species`, `stage`, `level`) VALUES
(90060, 6, 2, 40),
(90061, 7, 2, 40),
(90062, 8, 2, 40),
(90063, 12, 2, 40),
(90064, 29, 2, 40),
(90065, 30, 2, 40),
(90066, 31, 2, 40),
(90067, 32, 2, 40),
(90068, 33, 2, 40);

-- ── Items ───────────────────────────────────────────────────────────────────────────────────────
-- Class 15 / subclass 0 (miscellaneous, like the vanilla pet carriers), quality 3 (rare: a drop
-- worth shouting about), bonding 2 (Bind on Equip; never binds, as it is never equipped, so it
-- always stays tradeable), stackable 1 (each item is one companion), no spell. Icons reuse vanilla
-- pet-item display ids: 20655 whelpling, 20629 cat carrier, 17284 crate, 17292 parrot cage,
-- 19091 owl, 18046/18047/18050 eggs, 22717 Smolderweb Carrier, 16536 Mechanical Squirrel Box,
-- 33202 Disgusting Oozeling, and the elemental essences 23287/13689/23754/23284, 21724 Burning Essence.
DELETE FROM `item_template` WHERE `entry` BETWEEN 90001 AND 90199;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`,
  `quality`, `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `stackable`, `max_count`, `item_level`, `bonding`) VALUES
(90001, 0, 15, 0, 'Dark Whelpling Egg', 'Battle companion: Black Whelpling. Its level and experience travel with this item.', 20655, 3, 1, 20000, 5000, 0, 1, 0, 20, 2),
(90002, 0, 15, 0, 'Crimson Whelpling Egg', 'Battle companion: Crimson Whelpling. Its level and experience travel with this item.', 20655, 3, 1, 20000, 5000, 0, 1, 0, 20, 2),
(90003, 0, 15, 0, 'Emerald Whelpling Egg', 'Battle companion: Emerald Whelpling. Its level and experience travel with this item.', 20655, 3, 1, 20000, 5000, 0, 1, 0, 20, 2),
(90004, 0, 15, 0, 'Azure Whelpling Egg', 'Battle companion: Azure Whelpling. Its level and experience travel with this item.', 20655, 3, 1, 20000, 5000, 0, 1, 0, 20, 2),
(90005, 0, 15, 0, 'Shimmering Sprite Darter Egg', 'Battle companion: Sprite Darter Hatchling. Its level and experience travel with this item.', 18047, 3, 1, 16000, 4000, 0, 1, 0, 20, 2),
(90006, 0, 15, 0, 'Wolf Pup Carrier', 'Battle companion: Wolf Pup. Grows into a rideable wolf. Its level and experience travel with this item.', 20629, 3, 1, 10000, 2500, 0, 1, 0, 20, 2),
(90007, 0, 15, 0, 'Nightsaber Cub Carrier', 'Battle companion: Nightsaber Cub. Grows into a rideable nightsaber. Its level and experience travel with this item.', 20629, 3, 1, 10000, 2500, 0, 1, 0, 20, 2),
(90008, 0, 15, 0, 'Frostsaber Cub Carrier', 'Battle companion: Frostsaber Cub. Grows into a rideable frostsaber. Its level and experience travel with this item.', 20629, 3, 1, 16000, 4000, 0, 1, 0, 20, 2),
(90009, 0, 15, 0, 'Panther Cub Carrier', 'Battle companion: Panther Cub. Its level and experience travel with this item.', 20629, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90010, 0, 15, 0, 'Bear Cub Crate', 'Battle companion: Bear Cub. Its level and experience travel with this item.', 17284, 3, 1, 10000, 2500, 0, 1, 0, 20, 2),
(90011, 0, 15, 0, 'Spider Egg Sac', 'Battle companion: Young Web Spider. Its level and experience travel with this item.', 22717, 3, 1, 10000, 2500, 0, 1, 0, 20, 2),
(90012, 0, 15, 0, 'Raptor Egg Nest', 'Battle companion: Raptor Hatchling. Grows into a rideable raptor. Its level and experience travel with this item.', 18050, 3, 1, 10000, 2500, 0, 1, 0, 20, 2),
(90013, 0, 15, 0, 'Crocolisk Egg', 'Battle companion: Young Crocolisk. Its level and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90014, 0, 15, 0, 'Gorilla Infant Crate', 'Battle companion: Infant Gorilla. Its level and experience travel with this item.', 17284, 3, 1, 14000, 3500, 0, 1, 0, 20, 2),
(90015, 0, 15, 0, 'Owlet Cage', 'Battle companion: Owlet. Its level and experience travel with this item.', 19091, 3, 1, 14000, 3500, 0, 1, 0, 20, 2),
(90016, 0, 15, 0, 'Duskbat Cage', 'Battle companion: Duskbat Pup. Its level and experience travel with this item.', 17292, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90017, 0, 15, 0, 'Plainstrider Egg', 'Battle companion: Plainstrider Chick. Its level and experience travel with this item.', 18046, 3, 1, 6000, 1500, 0, 1, 0, 20, 2),
(90018, 0, 15, 0, 'Scorpid Egg Cluster', 'Battle companion: Scorpid Hatchling. Its level and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90019, 0, 15, 0, 'Jar of Living Embers', 'Battle companion: Ember Spark. Its level and experience travel with this item.', 23287, 3, 1, 16000, 4000, 0, 1, 0, 20, 2),
(90020, 0, 15, 0, 'Bottled Tide', 'Battle companion: Water Mote. Its level and experience travel with this item.', 13689, 3, 1, 16000, 4000, 0, 1, 0, 20, 2),
(90021, 0, 15, 0, 'Humming Pebble', 'Battle companion: Pebble. Its level and experience travel with this item.', 23754, 3, 1, 16000, 4000, 0, 1, 0, 20, 2),
(90022, 0, 15, 0, 'Captured Zephyr', 'Battle companion: Zephyr. Its level and experience travel with this item.', 23284, 3, 1, 16000, 4000, 0, 1, 0, 20, 2),
(90023, 0, 15, 0, 'Speckled Murloc Egg', 'Battle companion: Murloc Hatchling. Its level and experience travel with this item.', 18050, 3, 1, 10000, 2500, 0, 1, 0, 20, 2),
(90024, 0, 15, 0, 'Smoldering Fel Stone', 'Battle companion: Fel Cinder. Its level and experience travel with this item.', 21724, 3, 1, 20000, 5000, 0, 1, 0, 20, 2),
(90025, 0, 15, 0, 'Jar of Ooze', 'Battle companion: Oozeling. Its level and experience travel with this item.', 33202, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90026, 0, 15, 0, 'Cat Carrier (Snow Tiger)', 'Battle companion: Snow Tiger Kitten. Its level and experience travel with this item.', 20629, 3, 1, 4000, 1000, 0, 1, 0, 20, 2),
(90027, 0, 15, 0, 'Rabbit Crate (Wild)', 'Battle companion: Snowshoe Rabbit. Its level and experience travel with this item.', 17284, 3, 1, 2000, 500, 0, 1, 0, 20, 2),
(90028, 0, 15, 0, 'Salvaged Spider Tank Bot', 'Battle companion: Spider Tank Bot. Its level and experience travel with this item.', 16536, 3, 1, 8000, 2000, 0, 1, 0, 20, 2),
(90029, 0, 15, 0, 'Kodo Calf Tether', 'Battle companion: Kodo Calf. Grows into a rideable kodo. Its level and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90030, 0, 15, 0, 'Foal Halter', 'Battle companion: Foal. Grows into a rideable horse. Its level and experience travel with this item.', 13108, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90031, 0, 15, 0, 'Mechanostrider Runt Kit', 'Battle companion: Mechanostrider Runt. Grows into a rideable mechanostrider. Its level and experience travel with this item.', 16536, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90032, 0, 15, 0, 'Skeletal Foal Bones', 'Battle companion: Skeletal Foal. Grows into a rideable skeletal horse. Its level and experience travel with this item.', 17786, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90033, 0, 15, 0, 'Ram Kid Tether', 'Battle companion: Ram Kid. Grows into a rideable ram. Its level and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(90060, 0, 15, 0, 'Horn of the War Wolf', 'Battle companion mount: a level 40 war wolf. Walks and fights at your side; ride it with the mount button on its pet bar.', 16208, 3, 1, 800000, 200000, 0, 1, 0, 40, 2),
(90061, 0, 15, 0, 'Reins of the Companion Nightsaber', 'Battle companion mount: a level 40 striped nightsaber. Walks and fights at your side; ride it with the mount button on its pet bar.', 17606, 3, 1, 800000, 200000, 0, 1, 0, 40, 2),
(90062, 0, 15, 0, 'Reins of the Companion Frostsaber', 'Battle companion mount: a level 40 striped frostsaber. Walks and fights at your side; ride it with the mount button on its pet bar.', 17608, 3, 1, 800000, 200000, 0, 1, 0, 40, 2),
(90063, 0, 15, 0, 'Whistle of the Companion Raptor', 'Battle companion mount: a level 40 violet raptor. Walks and fights at your side; ride it with the mount button on its pet bar.', 17494, 3, 1, 800000, 200000, 0, 1, 0, 40, 2),
(90064, 0, 15, 0, 'Companion Brown Kodo', 'Battle companion mount: a level 40 brown kodo. Walks and fights at your side; ride it with the mount button on its pet bar.', 29447, 3, 1, 800000, 200000, 0, 1, 0, 40, 2),
(90065, 0, 15, 0, 'Companion Horse Bridle', 'Battle companion mount: a level 40 brown horse. Walks and fights at your side; ride it with the mount button on its pet bar.', 13108, 3, 1, 800000, 200000, 0, 1, 0, 40, 2),
(90066, 0, 15, 0, 'Companion Blue Mechanostrider', 'Battle companion mount: a level 40 blue mechanostrider. Walks and fights at your side; ride it with the mount button on its pet bar.', 17785, 3, 1, 800000, 200000, 0, 1, 0, 40, 2),
(90067, 0, 15, 0, 'Companion Red Skeletal Horse', 'Battle companion mount: a level 40 red skeletal horse. Walks and fights at your side; ride it with the mount button on its pet bar.', 17786, 3, 1, 800000, 200000, 0, 1, 0, 40, 2),
(90068, 0, 15, 0, 'Companion Gray Ram', 'Battle companion mount: a level 40 gray ram. Walks and fights at your side; ride it with the mount button on its pet bar.', 17343, 3, 1, 800000, 200000, 0, 1, 0, 40, 2);

-- Evolution catalysts: stackable, tradeable (no binding), uncommon. Each drops from its creature
-- type at level 25 and up (Companions::CatalystFor / CatalystDropChance).
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`,
  `quality`, `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `stackable`, `max_count`, `item_level`, `bonding`) VALUES
(90101, 0, 15, 0, 'Draconic Essence', 'Lets a dragonkin companion grow into its next form.', 7050, 2, 1, 40000, 10000, 0, 20, 0, 45, 0),
(90102, 0, 15, 0, 'Primal Beast Essence', 'Lets a beast companion grow into its next form.', 23285, 2, 1, 20000, 5000, 0, 20, 0, 35, 0),
(90103, 0, 15, 0, 'Elemental Core', 'Lets an elemental companion grow into its next form.', 8560, 2, 1, 30000, 7500, 0, 20, 0, 40, 0),
(90104, 0, 15, 0, 'Fel Ember', 'Lets a demonic companion grow into its next form.', 21724, 2, 1, 30000, 7500, 0, 20, 0, 40, 0);
