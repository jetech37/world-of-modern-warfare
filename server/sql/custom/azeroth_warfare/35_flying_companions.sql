-- World of Modern Warfare: the flying mounts as companion lines (design/COMPANION_CATALOG.md section
-- 1g, server/src/fusion/docs/COMPANIONS.md "Flying mounts"). World DB; apply after 02, 08, 29, 31
-- and 33. Safe to run again: the species and items are deleted and written afresh, the display,
-- look and vendor rows are REPLACEd or set to the same values.
--
-- Nine lines, species 215-223, items 91215-91223: a young form, a grown one, and a last form that is
-- the Wrath riding model (displays 40019-40037, the WotLK patch layer: client/src/wotlk/import.rs
-- LOOKS, design/WOTLK_ASSETS.md). The last form is ridden on the wolf's spells (580 / 23250, level
-- 40 and riding skill as every mount line) and skyrides (fusion/Skyriding.h): momentum flight,
-- never free, taking off with its mounted move, Launch.
-- Mirrors Companions::StarterSpecies() (kFlyingLooks); CompanionTests reads this file, and the
-- client's wotlk::import tests hold the display rows to LOOKS and the forms to the size rules.

-- ── The patch layer's displays the lines wear ────────────────────────────────────────────────
-- bounding radius and combat reach in yards at scale 1 (as 33_wotlk_looks.sql), walk and run 1.
REPLACE INTO `creature_display_info_addon` (`display_id`, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`) VALUES
(40019, 5875, 0.3, 1.0, 1, 1, 2, 0), -- gryphon hatchling
(40020, 5875, 1.0, 2.0, 1, 1, 2, 0), -- riding gryphon, armored
(40021, 5875, 0.3, 1.0, 1, 1, 2, 0), -- wind rider cub
(40022, 5875, 1.0, 2.0, 1, 1, 2, 0), -- riding wind rider, tawny
(40023, 5875, 1.0, 2.0, 1, 1, 2, 0), -- riding hippogryph, blue
(40024, 5875, 0.3, 1.0, 1, 1, 2, 0), -- dragonhawk hatchling, red
(40025, 5875, 0.75, 1.5, 1, 1, 2, 0), -- dragonhawk, golden
(40026, 5875, 1.0, 2.0, 1, 1, 2, 0), -- riding dragonhawk, red
(40027, 5875, 0.3, 1.0, 1, 1, 2, 0), -- netherwhelp
(40028, 5875, 1.0, 2.0, 1, 1, 2, 0), -- nether drake, purple
(40029, 5875, 1.5, 3.0, 1, 1, 2, 0), -- riding nether drake, purple
(40030, 5875, 0.3, 1.0, 1, 1, 2, 0), -- proto-drake whelp, red
(40031, 5875, 1.5, 3.0, 1, 1, 2, 0), -- proto-drake, violet
(40032, 5875, 1.5, 3.0, 1, 1, 2, 0), -- riding proto-drake, red
(40033, 5875, 0.3, 1.0, 1, 1, 2, 0), -- frost wyrm whelp
(40034, 5875, 1.5, 3.0, 1, 1, 2, 0), -- frost wyrm
(40035, 5875, 1.5, 3.0, 1, 1, 2, 0), -- riding frost wyrm, white
(40036, 5875, 1.5, 3.0, 1, 1, 2, 0), -- riding phoenix
(40037, 5875, 1.5, 3.0, 1, 1, 2, 0); -- riding drake, bronze

-- ── Species 215-223 ──────────────────────────────────────────────────────────────────────────
-- Families 59 Gryphon, 60 Wind Rider, 61 Hippogryph, 62 Dragonhawk; the drakes are dragons (1), the
-- phoenix a fire elemental (14). Drops: the wild creatures of each kind 1.12 has (gryphons of the
-- Hinterlands, wyverns of Stonetalon and Thousand Needles, the hippogryphs of Feralas and Azshara,
-- pterrordax for the proto-drake, Un'Goro's and Searing Gorge's fire for the phoenix), and where
-- 1.12 has none the nearest fit (Azshara's blood elves for the dragonhawk, the Dark Portal's
-- guards for the netherwhelp, the Plaguelands' Scourge for the frostbrood, Blackwing Lair's bronze
-- for the bronze whelpling). The three first ones are also sold (vendors below).
DELETE FROM `fusion_companion_species` WHERE `id` BETWEEN 215 AND 223;
INSERT INTO `fusion_companion_species` (`id`, `name`, `item_entry`, `family`, `role`,
  `stage1_creature`, `stage1_scale`, `stage2_creature`, `stage2_scale`, `stage3_creature`, `stage3_scale`,
  `evo1_level`, `evo1_item`, `evo1_item_count`, `evo1_areas`, `evo1_condition`, `evo1_condition_type`, `evo1_condition_count`,
  `evo2_level`, `evo2_item`, `evo2_item_count`, `evo2_areas`, `evo2_condition`, `evo2_condition_type`, `evo2_condition_count`,
  `drop_entries`, `drop_type`, `drop_family`, `ride_stage`, `ride_spell`, `ride_swift_spell`, `ride_display`) VALUES
(215, 'Gryphon Hatchling', 91215, 59, 0, 2347, 1, 2658, 0.6, 2658, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '47', 0, 0, 0, '2347,2658', 0, 0, 3, 580, 23250, 40020),
(216, 'Wind Rider Cub', 91216, 60, 0, 4012, 1, 4107, 0.65, 4107, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '400,406', 0, 0, 0, '4012,4107', 0, 0, 3, 580, 23250, 40022),
(217, 'Frayfeather Hatchling', 91217, 61, 0, 5300, 1, 5300, 0.55, 6375, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '357,16', 0, 0, 0, '5300,6375', 0, 0, 3, 580, 23250, 40023),
(218, 'Dragonhawk Hatchling', 91218, 62, 1, 6375, 0.9, 6375, 0.4, 6375, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '16', 0, 0, 0, '6198,6199,8581', 0, 0, 3, 580, 23250, 40026),
(219, 'Netherwhelp', 91219, 1, 1, 7044, 1, 7045, 1, 7046, 1, 20, 0, 0, '', 0, 0, 0, 40, 90101, 1, '4', 0, 0, 0, '6011,8716', 0, 0, 3, 580, 23250, 40029),
(220, 'Proto-Drake Whelp', 91220, 1, 1, 9166, 0.35, 9167, 0.3, 9167, 1, 20, 0, 0, '', 0, 0, 0, 40, 90101, 1, '490', 0, 0, 0, '9166,9167', 0, 0, 3, 580, 23250, 40032),
(221, 'Frostbrood Whelp', 91221, 1, 1, 8600, 0.5, 8600, 0.5, 8600, 1, 20, 0, 0, '', 0, 0, 0, 40, 90105, 1, '139', 1, 0, 0, '8525,8527,8529,10411', 0, 0, 3, 580, 23250, 40035),
(222, 'Phoenix Hatchling', 91222, 14, 1, 6521, 0.4, 6520, 0.5, 6521, 1, 20, 0, 0, '', 0, 0, 0, 40, 90103, 1, '537,490', 0, 0, 0, '6520,6521,5850', 0, 0, 3, 580, 23250, 40036),
(223, 'Bronze Whelpling', 91223, 1, 1, 7546, 1, 14025, 0.5, 14263, 1, 20, 0, 0, '', 0, 0, 0, 40, 90101, 1, '440', 0, 0, 0, '14025,14263', 0, 0, 3, 580, 23250, 40037);

-- The look table (Companions.cpp's kFlyingLooks): every stage's display and scale on top of the
-- display's own; the last form is the riding model at 1 and the display it is ridden with.
UPDATE `fusion_companion_species` SET `stage1_display` = 40019, `stage1_scale` = 1, `stage2_display` = 1149, `stage2_scale` = 0.6, `stage3_display` = 40020, `stage3_scale` = 1, `ride_display` = 40020 WHERE `id` = 215;
UPDATE `fusion_companion_species` SET `stage1_display` = 40021, `stage1_scale` = 1, `stage2_display` = 295, `stage2_scale` = 0.65, `stage3_display` = 40022, `stage3_scale` = 1, `ride_display` = 40022 WHERE `id` = 216;
UPDATE `fusion_companion_species` SET `stage1_display` = 16943, `stage1_scale` = 1, `stage2_display` = 10890, `stage2_scale` = 0.55, `stage3_display` = 40023, `stage3_scale` = 1, `ride_display` = 40023 WHERE `id` = 217;
UPDATE `fusion_companion_species` SET `stage1_display` = 40024, `stage1_scale` = 0.9, `stage2_display` = 40025, `stage2_scale` = 0.4, `stage3_display` = 40026, `stage3_scale` = 1, `ride_display` = 40026 WHERE `id` = 218;
UPDATE `fusion_companion_species` SET `stage1_display` = 40027, `stage1_scale` = 1, `stage2_display` = 40028, `stage2_scale` = 1, `stage3_display` = 40029, `stage3_scale` = 1, `ride_display` = 40029 WHERE `id` = 219;
UPDATE `fusion_companion_species` SET `stage1_display` = 40030, `stage1_scale` = 0.35, `stage2_display` = 40031, `stage2_scale` = 0.3, `stage3_display` = 40032, `stage3_scale` = 1, `ride_display` = 40032 WHERE `id` = 220;
UPDATE `fusion_companion_species` SET `stage1_display` = 40033, `stage1_scale` = 0.5, `stage2_display` = 40034, `stage2_scale` = 0.5, `stage3_display` = 40035, `stage3_scale` = 1, `ride_display` = 40035 WHERE `id` = 221;
UPDATE `fusion_companion_species` SET `stage1_display` = 40018, `stage1_scale` = 0.4, `stage2_display` = 40015, `stage2_scale` = 0.5, `stage3_display` = 40036, `stage3_scale` = 1, `ride_display` = 40036 WHERE `id` = 222;
UPDATE `fusion_companion_species` SET `stage1_display` = 6292, `stage1_scale` = 1, `stage2_display` = 13972, `stage2_scale` = 0.5, `stage3_display` = 40037, `stage3_scale` = 1, `ride_display` = 40037 WHERE `id` = 223;

-- ── Items 91215-91223 ────────────────────────────────────────────────────────────────────────
-- As the other companion items (class 15, Bind on Equip so always tradeable, one per stack, no
-- spell: the fusion hook summons it). The three sold ones cost 10 gold.
DELETE FROM `item_template` WHERE `entry` BETWEEN 91215 AND 91223;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`,
  `quality`, `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `stackable`, `max_count`, `item_level`, `bonding`) VALUES
(91215, 0, 15, 0, 'Gryphon Hatchling', 'Battle companion: Gryphon Hatchling. Grows into a rideable gryphon. Its level and experience travel with this item.', 18050, 1, 1, 100000, 25000, 0, 1, 0, 20, 2),
(91216, 0, 15, 0, 'Wind Rider Cub', 'Battle companion: Wind Rider Cub. Grows into a rideable wind rider. Its level and experience travel with this item.', 18053, 1, 1, 100000, 25000, 0, 1, 0, 20, 2),
(91217, 0, 15, 0, 'Frayfeather Hatchling', 'Battle companion: Frayfeather Hatchling. Grows into a rideable hippogryph. Its level and experience travel with this item.', 18047, 1, 1, 100000, 25000, 0, 1, 0, 20, 2),
(91218, 0, 15, 0, 'Dragonhawk Hatchling', 'Battle companion: Dragonhawk Hatchling. Grows into a rideable dragonhawk. Its level and experience travel with this item.', 18056, 1, 1, 0, 25000, 0, 1, 0, 20, 2),
(91219, 0, 15, 0, 'Netherwhelp', 'Battle companion: Netherwhelp. Grows into a rideable nether drake. Its level and experience travel with this item.', 16300, 1, 1, 0, 25000, 0, 1, 0, 20, 2),
(91220, 0, 15, 0, 'Proto-Drake Whelp', 'Battle companion: Proto-Drake Whelp. Grows into a rideable proto-drake. Its level and experience travel with this item.', 18046, 1, 1, 0, 25000, 0, 1, 0, 20, 2),
(91221, 0, 15, 0, 'Frostbrood Whelp', 'Battle companion: Frostbrood Whelp. Grows into a rideable frost wyrm. Its level and experience travel with this item.', 17598, 1, 1, 0, 25000, 0, 1, 0, 20, 2),
(91222, 0, 15, 0, 'Phoenix Hatchling', 'Battle companion: Phoenix Hatchling. Grows into a rideable phoenix. Its level and experience travel with this item.', 19530, 1, 1, 0, 25000, 0, 1, 0, 20, 2),
(91223, 0, 15, 0, 'Bronze Whelpling', 'Battle companion: Bronze Whelpling. Grows into a rideable bronze drake. Its level and experience travel with this item.', 18051, 1, 1, 0, 25000, 0, 1, 0, 20, 2);

-- ── Vendors ──────────────────────────────────────────────────────────────────────────────────
-- The pet vendors nearest each kind's home: Yarlyn Amberstill (Dun Morogh, the gryphon riders'
-- Ironforge), Halpa (Thunder Bluff, the wind riders'), Shylenai (Darnassus, the hippogryphs').
REPLACE INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`) VALUES
(1263, 0, 91215, 0, 0),
(8401, 0, 91216, 0, 0),
(8665, 0, 91217, 0, 0);
