-- World of Modern Warfare: the Construction window (fusion/Homestead.h). World DB; safe to apply
-- again (it only updates rows 03 and 12 own, and clears its own two spawns first).
--
-- The Construction Trainer (91001), the Plot Steward (91003, the plot coordinator) and the
-- Homestead Courier (91004) no longer show gossip menus: their gossip hello opens the client's
-- Construction window with that NPC's buttons (claim, keep, release and go to a plot; travel;
-- training and parts), and every answer shows in the window, not in chat. Nothing here is needed
-- for that (the scripts already are theirs); this keeps them plain gossip NPCs with no stock menu
-- of their own that could get in the way, and puts a steward by the courier at the large plots'
-- stops, so a player there can claim one and be taken to it without the walk back to the gate.
--
-- The build catalogue's new structures (Build.h Structures(), ids 140-155: cottage, shack,
-- watchtower, lookout tower, barn, stable, smithy, forge, market stall, palisade, palisade gate,
-- footbridge, stone bridge, village well, guard post, woodshed) and their recipes live in the
-- fusion rules (Construction.cpp) and are sent to the client with the other pieces' recipes;
-- they use the existing materials (items 91001-91099 and vanilla's), so no item rows change.
--
-- Uses creature.guid 2591025-2591026 of 03's reserved range 2591001-2591099. VERIFY both spawns'
-- heights in game (nobody floating, nobody in the ground).

-- The homestead NPCs: gossip only, no stock gossip menu.
UPDATE `creature_template` SET `npc_flags` = 1, `gossip_menu_id` = 0 WHERE `entry` IN (91001, 91003, 91004);

-- A Plot Steward by each large-plot stop: the Hyjal crater floor (by the courier, 9260002) and
-- Azshara Crater's south canyon (by the courier, 9260016). Both stand off the plots' grids.
DELETE FROM `creature` WHERE `guid` IN (2591025, 2591026);
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
(2591025, 91003, 1, 5502.0, -2794.0, 1463.0, 2.4, 300, 300, 0, 0),
(2591026, 91003, 37, -522.0, -210.0, 320.5, 3.0, 300, 300, 0, 0);
