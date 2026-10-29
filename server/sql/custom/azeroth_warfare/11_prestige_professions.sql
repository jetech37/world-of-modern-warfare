-- World of Modern Warfare: prestige and the professions' field gear (server/src/fusion/include/fusion/
-- Prestige.h, FieldGear.h; game/Fusion/FusionPrestige.cpp; docs in server/src/fusion/docs/PRESTIGE.md).
-- Apply to the world database after the vmangos world dump; safe to apply again (each block clears its
-- own range first). The characters tables (prestige ranks, tokens, field recipes learned) are made by
-- the server at startup.
--
-- Reserved ranges (W8 of the overnight ledger):
--   item_template.entry          93200-93299 (items), fusion_trade_recipe.id 93300-93399
--   creature_template.entry      92500-92599 (no longer used: the Prestige Quartermasters are gone)
--   creature.guid                2592500-2592599 (likewise)
--
-- Rows marked VERIFY need a look in game: display ids picked from similar vanilla items.

-- ── Field recipes ───────────────────────────────────────────────────────────────────────────────
-- Vanilla's recipes are spells in the client's Spell.dbc, which we can't add to; like Construction's
-- parts, the fusion teaches and crafts these itself. A profession trainer of the skill line shows a
-- "Field recipes" option (FusionMgr::AddProfessionGossip): learn a recipe there (its skill and `cost`
-- in copper), then make it at any trainer of that profession. Colours and skill-ups as vanilla's.
CREATE TABLE IF NOT EXISTS `fusion_trade_recipe` (
  `id` mediumint(8) unsigned NOT NULL,
  `name` varchar(100) NOT NULL DEFAULT '',
  `skill_line` smallint(5) unsigned NOT NULL COMMENT '202 Engineering, 165 Leatherworking, 197 Tailoring, 171 Alchemy',
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
  `cost` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'the trainer''s fee to learn it, copper',
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 COMMENT='World of Modern Warfare: field recipes the profession trainers teach';

DELETE FROM `fusion_trade_recipe` WHERE `id` BETWEEN 93300 AND 93399;
INSERT INTO `fusion_trade_recipe` (`id`, `name`, `skill_line`, `required`, `yellow`, `green`, `gray`, `product`, `product_count`,
  `reagent1`, `count1`, `reagent2`, `count2`, `reagent3`, `count3`, `reagent4`, `count4`, `cost`) VALUES
-- Engineering: a spare of the kit's own equipment (one more charge this life, two at most).
-- Coarse Blasting Powder 4364, Handful of Copper Bolts 4359, Bronze Bar 2841, Heavy Blasting Powder 4377,
-- Iron Bar 3575, Silk Cloth 4306, Bronze Tube 4371, Solid Blasting Powder 10505, Mithril Casing 10561,
-- Mageweave Cloth 4338, Flash Powder 5140, Iron Strut 4387, Wool Cloth 2592.
(93300, 'Spare Throwing Knives', 202, 50, 75, 100, 125, 93204, 2, 2841, 1, 4359, 1, 0, 0, 0, 0, 500),
(93301, 'Spare Thorium Grenade', 202, 100, 125, 150, 175, 93200, 2, 4364, 2, 4359, 1, 0, 0, 0, 0, 1000),
(93302, 'Spare Smoke Canister', 202, 100, 125, 150, 175, 93207, 2, 4364, 1, 2592, 2, 0, 0, 0, 0, 1000),
(93303, 'Spare Arcane Flash Bomb', 202, 125, 150, 175, 200, 93205, 2, 4364, 1, 5140, 2, 0, 0, 0, 0, 1500),
(93304, 'Spare Sticky Bomb', 202, 150, 175, 200, 225, 93201, 2, 4377, 2, 3575, 1, 4306, 1, 0, 0, 2500),
(93305, 'Spare Concussion Charge', 202, 150, 175, 200, 225, 93206, 2, 4377, 1, 4387, 1, 0, 0, 0, 0, 2500),
(93306, 'Spare Claymore', 202, 175, 195, 215, 235, 93202, 1, 3575, 2, 4377, 2, 4371, 1, 0, 0, 4000),
(93307, 'Spare Seaforium Charge', 202, 200, 220, 240, 260, 93203, 1, 10505, 2, 10561, 1, 4338, 1, 0, 0, 6000),
-- Leatherworking: bandoliers, ammo pouches worn in a bag slot, a quarter of the reserve more a step.
-- Medium Leather 2319, Coarse Thread 2320, Heavy Leather 4234, Fine Thread 2321, Thick Leather 4304,
-- Heavy Silken Thread 8343, Iron Buckle 7071.
(93310, 'Light Bandolier', 165, 100, 125, 150, 175, 93220, 1, 2319, 8, 2320, 2, 0, 0, 0, 0, 1500),
(93311, 'Bandolier', 165, 175, 195, 215, 235, 93221, 1, 4234, 10, 2321, 2, 0, 0, 0, 0, 4000),
(93312, 'Heavy Bandolier', 165, 250, 265, 280, 295, 93222, 1, 4304, 12, 8343, 2, 7071, 1, 0, 0, 10000),
-- Tailoring: ghillie mantles (cloaks): creatures notice you closer, and enemy radar misses you.
-- Wool Cloth 2592, Green Dye 2605, Silk Cloth 4306, Mageweave Cloth 4338, Silken Thread 4291.
(93320, 'Woven Ghillie Mantle', 197, 100, 125, 150, 175, 93240, 1, 2592, 6, 2320, 2, 2605, 1, 0, 0, 1500),
(93321, 'Ghillie Mantle', 197, 175, 195, 215, 235, 93241, 1, 4306, 8, 2321, 2, 2605, 2, 0, 0, 4000),
(93322, 'Master Ghillie Mantle', 197, 250, 265, 280, 295, 93242, 1, 4338, 10, 4291, 2, 2605, 2, 8343, 1, 10000),
-- Alchemy: combat stims. Mageroyal 785, Swiftthistle 2452, Empty Vial 3371, Bruiseweed 2453,
-- Briarthorn 2450, Leaded Vial 3372, Goldthorn 3821.
(93330, 'Adrenaline Stim', 171, 100, 125, 150, 175, 93262, 2, 2452, 1, 785, 1, 3371, 1, 0, 0, 1000),
(93331, 'Second Wind Stim', 171, 125, 150, 175, 200, 93261, 2, 2453, 1, 2450, 1, 3372, 1, 0, 0, 1500),
(93332, 'Focus Stim', 171, 150, 175, 200, 225, 93260, 2, 2452, 1, 3821, 1, 3372, 1, 0, 0, 2500);

-- ── Items ───────────────────────────────────────────────────────────────────────────────────────
DELETE FROM `item_template` WHERE `entry` BETWEEN 93200 AND 93299;
-- Engineering's spares (Consumables, no spell: the fusion uses them, FusionMgr::UseFieldItem).
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(93200, 0, 0, 0, 'Spare Thorium Grenade', 'Use: one more frag grenade for your kit, this life (a kit carrying frags; two spares at most).', 25482, 1, 0, 1, 400, 100, 0, 25, 0, 0, 20, 0, 1),
(93201, 0, 0, 0, 'Spare Sticky Bomb', 'Use: one more semtex for your kit, this life (a kit carrying semtex; two spares at most).', 7626, 1, 0, 1, 600, 150, 0, 35, 0, 0, 20, 0, 1),     -- VERIFY display (Big Bronze Bomb's)
(93202, 0, 0, 0, 'Spare Claymore', 'Use: one more claymore for your kit, this life (a kit carrying claymores; two spares at most).', 7367, 1, 0, 1, 800, 200, 0, 40, 0, 0, 20, 0, 1),   -- Goblin Land Mine's look
(93203, 0, 0, 0, 'Spare Seaforium Charge', 'Use: one more C4 for your kit, this life (a kit carrying C4; two spares at most).', 6393, 1, 0, 1, 1200, 300, 0, 45, 0, 0, 20, 0, 1),    -- Large Seaforium Charge's look
(93204, 0, 0, 0, 'Spare Throwing Knives', 'Use: one more throwing knife for your kit, this life (a kit carrying knives; two spares at most).', 6378, 1, 0, 1, 200, 50, 0, 15, 0, 0, 20, 0, 1),   -- VERIFY display
(93205, 0, 0, 0, 'Spare Arcane Flash Bomb', 'Use: one more flashbang for your kit, this life (a kit carrying flashbangs; two spares at most).', 6378, 1, 0, 1, 400, 100, 0, 30, 0, 0, 20, 0, 1),   -- Flash Bomb's look
(93206, 0, 0, 0, 'Spare Concussion Charge', 'Use: one more stun grenade for your kit, this life (a kit carrying stuns; two spares at most).', 7361, 1, 0, 1, 600, 150, 0, 35, 0, 0, 20, 0, 1),   -- VERIFY display
(93207, 0, 0, 0, 'Spare Smoke Canister', 'Use: one more smoke grenade for your kit, this life (a kit carrying smoke; two spares at most).', 6378, 1, 0, 1, 400, 100, 0, 25, 0, 0, 20, 0, 1);   -- VERIFY display

-- Leatherworking's bandoliers: ammo pouches (Quiver, Ammo Pouch; worn in a bag slot). Holding one
-- carries more spare ammunition for every gun (FieldGear::ReserveBonus), never a bigger magazine.
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`, `container_slots`, `bag_family`) VALUES
(93220, 0, 11, 3, 'Light Bandolier', 'Worn in a bag slot: carry a quarter more spare ammunition for every gun.', 1281, 1, 0, 1, 2000, 500, 18, 25, 15, 1, 1, 0, 8, 6, 2),
(93221, 0, 11, 3, 'Bandolier', 'Worn in a bag slot: carry half again the spare ammunition for every gun.', 1281, 2, 0, 1, 6000, 1500, 18, 40, 30, 1, 1, 0, 8, 8, 2),
(93222, 0, 11, 3, 'Heavy Bandolier', 'Worn in a bag slot: carry three quarters more spare ammunition for every gun.', 1281, 3, 0, 1, 15000, 3750, 18, 55, 45, 1, 1, 0, 8, 10, 2),
(93223, 0, 11, 3, 'Prestige Bandolier', 'Worn in a bag slot: carry twice the spare ammunition for every gun.', 1281, 4, 0, 1, 0, 0, 18, 60, 1, 1, 1, 1, 8, 12, 2);

-- Tailoring's ghillie mantles (cloaks): creatures notice the wearer closer in (the multiplier is
-- FieldGear::GhillieMultiplier), and enemy players' radar (a UAV) misses them unless fighting.
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`, `armor`) VALUES
(93240, 0, 4, 1, 'Woven Ghillie Mantle', 'Creatures notice you 15% closer in; enemy radar misses you unless you fight.', 27974, 1, 0, 1, 2000, 500, 16, 25, 15, 0, 1, 2, 7, 12),   -- Wolf Rider's Cloak's look, VERIFY
(93241, 0, 4, 1, 'Ghillie Mantle', 'Creatures notice you 25% closer in; enemy radar misses you unless you fight.', 27974, 2, 0, 1, 6000, 1500, 16, 40, 30, 0, 1, 2, 7, 22),
(93242, 0, 4, 1, 'Master Ghillie Mantle', 'Creatures notice you 35% closer in; enemy radar misses you unless you fight.', 27974, 3, 0, 1, 15000, 3750, 16, 55, 45, 0, 1, 2, 7, 32),
(93243, 0, 4, 1, 'Prestige Ghillie Mantle', 'Creatures notice you 40% closer in; enemy radar misses you unless you fight.', 27974, 4, 0, 1, 0, 0, 16, 60, 1, 0, 1, 1, 7, 35);

-- Alchemy's stims (Consumables, used by the fusion: FieldGear::ApplyStims).
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(93260, 0, 0, 0, 'Focus Stim', 'Use: for 60 sec, steadier aim: 40% less scope sway, less recoil and flinch.', 17902, 1, 0, 1, 600, 150, 0, 35, 25, 0, 20, 0, 0),   -- Elixir of Greater Agility's look
(93261, 0, 0, 0, 'Second Wind Stim', 'Use: for 12 sec, health comes back a second after a hit, and two and a half times as fast.', 24152, 1, 0, 1, 400, 100, 0, 30, 20, 0, 20, 0, 0),   -- Major Healing Potion's look
(93262, 0, 0, 0, 'Adrenaline Stim', 'Use: for 20 sec, sprint without tiring.', 15742, 1, 0, 1, 300, 75, 0, 25, 15, 0, 20, 0, 0);   -- Swiftness Potion's look

-- ── The token shop ──────────────────────────────────────────────────────────────────────────────
-- Prestige tokens are spent in the Prestige tab of the achievements window (Prestige::Shop, the
-- prestige channel's Buy op). The Prestige Quartermasters this file once spawned (creature 92500,
-- guids 2592501+) are gone: 14_kits_at_trainers.sql deletes them.
