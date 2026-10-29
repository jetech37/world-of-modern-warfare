-- World of Modern Warfare: Minstrelsy, music as a secondary profession (server/src/fusion/include/
-- fusion/Minstrelsy.h, game/Fusion/FusionMinstrelsy.cpp; design/GAME_DESIGN.md section 12). World
-- DB. Apply after 03_construction.sql and 11_prestige_professions.sql (their recipe tables). Safe to
-- apply again: every block clears its own rows first.
--
-- Reserved ranges:
--   item_template.entry          94100-94119 (94101-94106 the instruments, Minstrelsy::Instruments;
--                                94110 the Thunder Drums' score, Minstrelsy::kThunderDrumsScore)
--   creature_template.entry      94100-94119 (94100 the Minstrel Trainer, Minstrelsy::kTrainerEntry)
--   creature.guid                2594100-2594199
--   fusion_recipe.id             94110-94119, fusion_trade_recipe.id 94120-94129: kept clear (an
--                                earlier draft put instruments there; the rows are deleted below)
--
-- Rows marked VERIFY need a look in game: the instruments' looks (vanilla has no drum or lute item,
-- so each copies a horn's or a flute's display from its own row, naming no model here), the
-- trainer's display and every spawn position.

-- ── The instruments ─────────────────────────────────────────────────────────────────────────────
-- Utility tools: carried in the bags, readied from Utility's bar (shown once Minstrelsy reaches the
-- instrument's skill). Minstrels make every one of them (Minstrelsy::Recipes, learned from a
-- Minstrel Trainer, of other trades' goods); the Hand Drum is also sold by the trainers and every
-- innkeeper, so a new minstrel can start. The Thunder Drums are learned only from their score, a
-- rare drop of the Smolderthorn and Vilebranch trolls' drummers and casters.
DELETE FROM `item_template` WHERE `entry` BETWEEN 94100 AND 94119;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94101, 0, 15, 0, 'Hand Drum', 'An instrument of Minstrelsy (1). Ready it in Utility and fire to play; press its key again for the next song.',
  t.`display_id`, 1, 0, 1, 100, 25, 0, 5, 0, 1, 0, 8 FROM `item_template` t WHERE t.`entry` = 15852 LIMIT 1;      -- VERIFY look (Kodo Horn's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94102, 0, 15, 0, 'Lute', 'An instrument of Minstrelsy (50): three strings, eighth notes and chords.',
  t.`display_id`, 1, 0, 1, 0, 375, 0, 15, 0, 1, 0, 2 FROM `item_template` t WHERE t.`entry` = 17117 LIMIT 1;    -- VERIFY look (a flute's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94103, 0, 15, 0, 'Flute', 'An instrument of Minstrelsy (100): four keys and long held notes.',
  t.`display_id`, 2, 0, 1, 0, 1200, 0, 25, 0, 1, 2, 2 FROM `item_template` t WHERE t.`entry` = 11445 LIMIT 1;      -- VERIFY look (Flute of the Ancients')
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94104, 0, 15, 0, 'Harp', 'An instrument of Minstrelsy (150): five strings, runs and chords of three.',
  t.`display_id`, 2, 0, 1, 0, 2500, 0, 35, 0, 1, 2, 2 FROM `item_template` t WHERE t.`entry` = 13536 LIMIT 1;      -- VERIFY look (a horn's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94105, 0, 15, 0, 'Bagpipes', 'An instrument of Minstrelsy (225): all six keys, sixteenth runs over long drones.',
  t.`display_id`, 3, 0, 1, 0, 5000, 0, 45, 0, 1, 2, 8 FROM `item_template` t WHERE t.`entry` = 5432 LIMIT 1;       -- VERIFY look (Hickory Pipe's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94106, 0, 15, 0, 'Thunder Drums', 'An instrument of Minstrelsy (275): the hardest to play, the strongest played well.',
  t.`display_id`, 3, 0, 1, 0, 10000, 0, 55, 0, 1, 2, 8 FROM `item_template` t WHERE t.`entry` = 14557 LIMIT 1;    -- VERIFY look (a horn's)

-- ── The Thunder Drums' score ─────────────────────────────────────────────────────────────────────
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94110, 0, 15, 0, 'Score: Thunder Drums', 'A Minstrel Trainer teaches the Thunder Drums (Minstrelsy 275) to whoever brings this.',
  t.`display_id`, 3, 0, 1, 0, 2500, 0, 55, 0, 1, 2, 7 FROM `item_template` t WHERE t.`entry` = 2589 LIMIT 1;      -- VERIFY look (Linen Cloth's)

-- Instruments are no other trade's recipes (an earlier draft had them at Construction and
-- Leatherworking): those rows go.
DELETE FROM `fusion_recipe` WHERE `id` BETWEEN 94110 AND 94119;
DELETE FROM `fusion_trade_recipe` WHERE `id` BETWEEN 94120 AND 94129;

-- The score found: rarely, on the trolls' drummers and casters (the instrument itself never drops).
DELETE FROM `creature_loot_template` WHERE `item` IN (94106, 94110);
INSERT INTO `creature_loot_template` (`entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount`) VALUES
(9239, 94110, 0.5, 0, 1, 1),    -- Smolderthorn Mystic
(9265, 94110, 0.5, 0, 1, 1),    -- Smolderthorn Shadow Hunter
(9266, 94110, 0.5, 0, 1, 1),    -- Smolderthorn Witch Doctor
(2645, 94110, 0.2, 0, 1, 1),    -- Vilebranch Shadow Hunter
(2640, 94110, 0.2, 0, 1, 1);    -- Vilebranch Witch Doctor

-- ── Vendors ─────────────────────────────────────────────────────────────────────────────────────
-- Every innkeeper sells the Hand Drum: those with the innkeepers' shared list (vendor template 100)
-- through it, the rest through their own.
DELETE FROM `npc_vendor_template` WHERE `entry` = 100 AND `item` = 94101;
INSERT INTO `npc_vendor_template` (`entry`, `slot`, `item`, `maxcount`, `incrtime`) VALUES (100, 90, 94101, 0, 0);
DELETE FROM `npc_vendor` WHERE `item` = 94101;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`)
SELECT `entry`, 90, 94101, 0, 0 FROM `creature_template`
WHERE `subname` = 'Innkeeper' AND `vendor_id` = 0 AND (`npc_flags` & 4) <> 0 GROUP BY `entry`;

-- ── The Minstrel Trainers ───────────────────────────────────────────────────────────────────────
-- Not a spell trainer (Minstrelsy is no spell): a gossip NPC whose script opens the Minstrelsy
-- window (train the tiers, learn the instrument recipes, buy a starter Hand Drum). Faction 35: friendly to everyone.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 94100 AND 94119;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`,
  `civilian`, `script_name`) VALUES
(94100, 0, 'Lorien Songweaver', 'Minstrel Trainer', 40, 40, 35, 1, 4026, 1, 100, 100, 7, 1, 1, 'npc_fusion_minstrel_trainer');   -- VERIFY display (the Singer's)

-- By the innkeeper of each capital and of four busy inns, two yards to the innkeeper's right.
-- VERIFY: every position (a counter or a wall may be in the way).
DELETE FROM `creature` WHERE `guid` BETWEEN 2594100 AND 2594199;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
(2594101, 94100, 0, -8869.56, 672.73, 97.99, 5.20, 300, 300, 0, 0),     -- Stormwind, the Gilded Rose (Innkeeper Allison)
(2594102, 94100, 0, -4842.65, -857.40, 502.00, 4.87, 300, 300, 0, 0),   -- Ironforge, the Stonefire Tavern (Innkeeper Firebrew)
(2594103, 94100, 1, 10129.49, 2226.00, 1328.81, 2.22, 300, 300, 0, 0),  -- Darnassus (Innkeeper Saelienne)
(2594104, 94100, 1, 1634.73, -4437.51, 15.52, 2.76, 300, 300, 0, 0),    -- Orgrimmar, Valley of Strength (Innkeeper Gryshka)
(2594105, 94100, 1, -1299.22, 36.80, 129.29, 0.56, 300, 300, 0, 0),     -- Thunder Bluff (Innkeeper Pala)
(2594106, 94100, 0, 1635.99, 225.24, -43.02, 2.84, 300, 300, 0, 0),     -- Undercity (Innkeeper Norman)
(2594107, 94100, 0, -9462.46, 18.18, 57.05, 3.04, 300, 300, 0, 0),      -- Goldshire, the Lion's Pride Inn (Innkeeper Farley)
(2594108, 94100, 1, -407.71, -2643.31, 96.31, 3.44, 300, 300, 0, 0),    -- the Crossroads (Innkeeper Boorand Plainswind)
(2594109, 94100, 0, -14459.19, 496.69, 15.21, 3.98, 300, 300, 0, 0),    -- Booty Bay, the Salty Sailor Tavern (Innkeeper Skindle)
(2594110, 94100, 1, -7157.10, -3840.87, 8.85, 1.95, 300, 300, 0, 0);    -- Gadgetzan (Innkeeper Fizzgrimble)
