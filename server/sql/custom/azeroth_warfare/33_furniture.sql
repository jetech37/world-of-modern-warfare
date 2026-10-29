-- World of Modern Warfare: furniture, Construction's decor layer (server/src/fusion/include/fusion/
-- Furniture.h; docs in server/src/fusion/docs/CONSTRUCTION.md, "Furniture"). World DB. Apply after
-- 32_professions.sql. Safe to apply again: each block clears its own ids first.
--
-- The furniture pieces themselves are pieces like any other (the characters table
-- `fusion_build_piece`, their kits `character_fusion_construction_kit`); the server makes its own
-- characters tables at start (`fusion_plot_stash`, `fusion_furniture_trophy`). These rows are the
-- WoW objects the server stands in some pieces:
--
-- Displays: 3019 the wooden dummy (Creature\Object\WoodenDummy), 1907 the human postbox
-- (GameObjectDisplayInfo), both read from the 1.12 install's DBCs.
--
-- Reserved ranges:
--   creature_template.entry      94250-94259 (94250 the training dummy, 94251 a trophy's likeness;
--                                94200-94249 are the companion tamers', 33_companion_battles.sql)
--   gameobject_template.entry    194200-194209 (194200 a homestead mailbox)

DELETE FROM `creature_template` WHERE `entry` BETWEEN 94250 AND 94259;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`,
  `health_multiplier`, `civilian`, `movement_type`, `script_name`) VALUES
-- Attackable by anyone (faction 7, the critters' and the dummies' own), never fights back, never dies
-- (npc_fusion_training_dummy in scripts/custom/custom_fusion.cpp). Level 60 and stout, so every gun
-- and spell lands its usual damage.
(94250, 0, 'Training Dummy', 'Homestead', 60, 60, 7, 0, 3019, 1, 100, 100, 10, 1, 500, 0, 0, 'npc_fusion_training_dummy'),
-- A companion's likeness on a trophy plinth: the server gives it the companion's model and makes it
-- unselectable; it is never fought.
(94251, 0, 'Trophy', '', 1, 1, 35, 0, 3019, 1, 100, 100, 1, 1, 1, 1, 0, '');

DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 194200 AND 194209;
INSERT INTO `gameobject_template` (`entry`, `patch`, `type`, `displayId`, `name`, `faction`, `flags`, `size`,
  `data0`, `data1`, `data2`, `data3`, `data4`, `data5`, `script_name`) VALUES
-- Type 19: a mailbox. The client opens its mail window on a right-click, as at any mailbox.
(194200, 0, 19, 1907, 'Mailbox', 0, 0, 1, 0, 0, 0, 0, 0, 0, '');
