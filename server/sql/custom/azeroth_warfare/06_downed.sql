-- World of Modern Warfare: the downed state's revival kits (server/src/fusion/include/fusion/Downed.h,
-- server/src/game/Fusion/FusionDowned.cpp). Apply to the world database after 03_construction.sql
-- (it adds to its `fusion_recipe` table and its supplier); safe to apply again (each block clears
-- its own rows first).
--
-- Reserved ranges:
--   item_template.entry          93020-93049
--   fusion_recipe.id             93020-93049
--   npc_vendor                   entry 91002 (the construction supplier), items 93020-93049
--
-- A downed player is brought back up only by a resurrection spell or one of these kits, used on a
-- downed ally (a short use) or on yourself while down (a long one). The kits and their manuals
-- carry no spell: the client sends their use on the fusion's downed channel (Downed::kOpUse) and
-- the server does the rest, as with battle companions' items. Rows marked VERIFY need a look in
-- game (display ids picked from similar vanilla items).

-- ── Items ───────────────────────────────────────────────────────────────────────────────────────
DELETE FROM `item_template` WHERE `entry` BETWEEN 93020 AND 93049;
-- The kits: consumables, never sold (made only). Downed::Kit holds their timings and health.
-- A vendor pays a little less for a kit than for its reagents (2 Heavy Silk Bandages and a
-- Swiftthistle; a Bronze Tube, 2 Handfuls of Copper Bolts and a Whirring Bronze Gizmo): at 25s and
-- 40s each, crafting them to sell was a money press.
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(93020, 0, 0, 0, 'Field Revival Kit', 'Brings a downed ally back up (3 sec), or yourself while down (7 sec). Hold still while it works.', 25146, 2, 0, 1, 0, 200, 0, 35, 0, 0, 5, 0, 7),       -- VERIFY display (Runecloth Bandage's)
(93021, 0, 0, 0, 'Gnomish Revival Coil', 'A jolt that brings a downed ally back up (2.5 sec), or yourself while down (6 sec). Hold still while it charges.', 31203, 2, 0, 1, 0, 350, 0, 40, 0, 0, 3, 0, 1); -- VERIFY display (Goblin Jumper Cables XL's)

-- The manuals: use one to make a kit from its reagents (the skill and reagents of its recipe
-- below), anywhere. Sold by the construction supplier.
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`, `required_skill`, `required_skill_rank`) VALUES
(93022, 0, 9, 7, 'Field Medic''s Handbook', 'Use: make a Field Revival Kit from 2 Heavy Silk Bandages and a Swiftthistle. Requires First Aid (150).', 8117, 1, 0, 1, 20000, 5000, 0, 30, 0, 1, 1, 1, 0, 129, 150),   -- VERIFY display (Manual: Heavy Silk Bandage's)
(93023, 0, 9, 3, 'Schematic: Gnomish Revival Coil', 'Use: make a Gnomish Revival Coil from a Bronze Tube, 2 Handfuls of Copper Bolts and a Whirring Bronze Gizmo. Requires Engineering (175).', 15274, 1, 0, 1, 30000, 7500, 0, 35, 0, 1, 1, 1, 0, 202, 175);

-- ── Recipes the fusion crafts (03_construction.sql's table) ─────────────────────────────────────
-- First Aid 129: Heavy Silk Bandage 6451, Swiftthistle 2452. Engineering 202: Bronze Tube 4371,
-- Handful of Copper Bolts 4359, Whirring Bronze Gizmo 4375. Also on the construction trainer's menu,
-- which lists every fusion recipe of a skill the player has.
DELETE FROM `fusion_recipe` WHERE `id` BETWEEN 93020 AND 93049;
INSERT INTO `fusion_recipe` (`id`, `name`, `skill_line`, `required`, `yellow`, `green`, `gray`, `product`, `product_count`,
  `reagent1`, `count1`, `reagent2`, `count2`, `reagent3`, `count3`, `reagent4`, `count4`, `cast_ms`, `needs_anvil`) VALUES
(93030, 'Field Revival Kit', 129, 150, 165, 180, 195, 93020, 1, 6451, 2, 2452, 1, 0, 0, 0, 0, 3000, 0),
(93031, 'Gnomish Revival Coil', 202, 175, 190, 205, 220, 93021, 1, 4371, 1, 4359, 2, 4375, 1, 0, 0, 5000, 0);

-- ── The manuals for sale ────────────────────────────────────────────────────────────────────────
DELETE FROM `npc_vendor` WHERE `entry` = 91002 AND `item` BETWEEN 93020 AND 93049;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`) VALUES
(91002, 20, 93022, 0, 0),   -- Field Medic's Handbook
(91002, 21, 93023, 0, 0);   -- Schematic: Gnomish Revival Coil
