-- World of Modern Warfare: the Repair Hammer, Utility's tool for mending built pieces
-- (server/src/fusion/include/fusion/Maintenance.h, Gathering.h; docs in
-- server/src/fusion/docs/CONSTRUCTION.md). World DB. Apply after 03_construction.sql. Safe to
-- apply again: only its own rows are cleared and put back.
--
-- Built pieces decay (2% of their health a day out in the world) and are mended with this hammer
-- held in Utility's hands: aim at a damaged piece and hold fire. It is sold by the construction
-- supplier (91002, by the smiths of Stormwind and Orgrimmar and at the homesteads' gate) and made
-- by smiths (Blacksmithing 50, at an anvil) in the Construction trainer's window.
--
-- Reserved (inside 03_construction.sql's ranges):
--   item_template.entry          91011
--   fusion_recipe.id             91009

-- A one-handed hammer, a builder's: the Blacksmith Hammer's look (its display read from its own
-- row, so nothing here names a model).
DELETE FROM `item_template` WHERE `entry` = 91011;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`,
  `material`, `delay`, `dmg_min1`, `dmg_max1`, `dmg_type1`, `max_durability`, `sheath`)
SELECT 91011, 0, 2, 4, 'Repair Hammer', 'Mends built pieces: ready it in Utility, aim at a damaged piece and hold fire. Each strike takes a share of the piece''s own materials.',
  t.`display_id`, 1, 0, 1, 500, 100, 13, 10, 1, 1, 0, 1, 2400, 3, 6, 0, 30, 3
FROM `item_template` t WHERE t.`entry` = 5956 LIMIT 1;

-- Sold by the construction supplier, after its other wares.
DELETE FROM `npc_vendor` WHERE `entry` = 91002 AND `item` = 91011;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`) VALUES
(91002, 6, 91011, 0, 0);

-- Made by smiths: copper bars and a little lumber for the haft, at an anvil.
DELETE FROM `fusion_recipe` WHERE `id` = 91009;
INSERT INTO `fusion_recipe` (`id`, `name`, `skill_line`, `required`, `yellow`, `green`, `gray`, `product`, `product_count`,
  `reagent1`, `count1`, `reagent2`, `count2`, `reagent3`, `count3`, `reagent4`, `count4`, `cast_ms`, `needs_anvil`) VALUES
(91009, 'Repair Hammer', 164, 50, 75, 90, 110, 91011, 1, 2840, 4, 91001, 2, 0, 0, 0, 0, 3000, 1);
