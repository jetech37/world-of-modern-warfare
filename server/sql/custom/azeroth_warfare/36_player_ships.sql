-- World of Modern Warfare: player ships (design/PLAYER_SHIPS.md, fusion/Ships.h,
-- game/Fusion/FusionShips.cpp). World DB; safe to apply again (it deletes its own rows first).
--
-- Reserved here:
--   gameobject_template.entry    191800-191809 (the ships; 191800 Sloop, 191801 Airship)
--   item_template.entry          93800-93809   (the deeds; 93800 Sloop Deed, 93801 Airship Charter)
--   creature_template.entry      91810-91819   (91810 the Shipwright)
--   creature.guid                2591810-2591819
--
-- The characters table `aw_ship` is made by the server at startup (FusionShips.cpp).
-- Rows marked VERIFY need a look in game.

-- ── The ships: type 11 (TRANSPORT) with no TransportAnimation path ──────────────────────────────
-- The server drives them (FusionShips.cpp PlayerShip) and streams their pose on fusion channel 14.
-- The look is the stock boats': the Ratchet to Booty Bay ship (20808, Transport_Ship) and the
-- Orgrimmar to Undercity zeppelin (164871, Transport_Zeppelin). Flags 40: no despawn, transport.
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 191800 AND 191809;
INSERT INTO `gameobject_template` (`entry`, `patch`, `type`, `displayId`, `name`, `faction`, `flags`, `size`,
  `data0`, `data1`, `data2`, `data3`, `script_name`)
SELECT 191800, 0, 11, t.`displayId`, 'Sloop', 0, 40, t.`size`, 0, 0, 0, 0, ''
FROM `gameobject_template` t WHERE t.`entry` = 20808 ORDER BY t.`patch` DESC LIMIT 1;
INSERT INTO `gameobject_template` (`entry`, `patch`, `type`, `displayId`, `name`, `faction`, `flags`, `size`,
  `data0`, `data1`, `data2`, `data3`, `script_name`)
SELECT 191801, 0, 11, t.`displayId`, 'Airship', 0, 40, t.`size`, 0, 0, 0, 0, ''
FROM `gameobject_template` t WHERE t.`entry` = 164871 ORDER BY t.`patch` DESC LIMIT 1;
-- Fallbacks, if the stock rows were missing: Transport_Ship 3015, Transport_Zeppelin 3031.
INSERT IGNORE INTO `gameobject_template` (`entry`, `patch`, `type`, `displayId`, `name`, `faction`, `flags`, `size`,
  `data0`, `data1`, `data2`, `data3`, `script_name`) VALUES
(191800, 0, 11, 3015, 'Sloop', 0, 40, 1, 0, 0, 0, 0, ''),
(191801, 0, 11, 3031, 'Airship', 0, 40, 1, 0, 0, 0, 0, '');

-- ── The deeds: sold by the Shipwright, tradeable (no bonding), one at a time ────────────────────
DELETE FROM `item_template` WHERE `entry` BETWEEN 93800 AND 93809;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(93800, 0, 15, 0, 'Sloop Deed', 'Grants the right to commission a Sloop at a Shipwright. Bring it with 400 gold, 20 Mithril Bars, 10 Thorium Bars, 40 Bolts of Mageweave and 20 Thick Leather.', 6270, 3, 0, 1, 1000000, 250000, 0, 50, 40, 1, 1, 0, 0),  -- VERIFY display (a rolled scroll)
(93801, 0, 15, 0, 'Airship Charter', 'Grants the right to commission an Airship at a Shipwright. Bring it with 1,200 gold, 20 Thorium Bars, 6 Arcanite Bars, 4 Gyrochronatoms, 2 Enchanted Thorium Bars, 40 Bolts of Runecloth and 20 Rugged Leather.', 6270, 4, 0, 1, 3000000, 750000, 0, 60, 55, 1, 1, 0, 0);  -- VERIFY display

-- ── The Shipwright ──────────────────────────────────────────────────────────────────────────────
-- Gossip (commission, launch, upkeep, repair, salvage, summon) and the deeds as a vendor.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 91810 AND 91819;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`,
  `civilian`, `script_name`) VALUES
(91810, 0, 'Captain Gearwhistle', 'Shipwright', 55, 55, 35, 5, 7179, 1, 100, 100, 7, 1, 1, 'npc_fusion_shipwright');  -- VERIFY display (a goblin)

DELETE FROM `npc_vendor` WHERE `entry` BETWEEN 91810 AND 91819;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`) VALUES
(91810, 0, 93800, 0, 0),    -- Sloop Deed
(91810, 1, 93801, 0, 0);    -- Airship Charter

-- One at each neutral port: Ratchet's dock (Kalimdor) and Booty Bay's (Eastern Kingdoms). A
-- sailing ship launches on the deep water nearest the Shipwright, an airship over them.
-- VERIFY both positions in game (on the planks, by deep water).
DELETE FROM `creature` WHERE `guid` BETWEEN 2591810 AND 2591819;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
(2591810, 91810, 1, -996.0, -3826.0, 5.6, 5.4, 300, 300, 0, 0),
(2591811, 91810, 0, -14284.0, 553.0, 8.9, 4.0, 300, 300, 0, 0);
