-- World of Modern Warfare: the lore killstreaks' craft (server/src/fusion/include/fusion/SkyStreaks.h,
-- game/Fusion/FusionSkyStreaks.cpp).
--
-- The server summons these for a caller and puts on the model for their side (SkyStreaks.h LookOf):
-- a goblin rocket car for the Goblin Rocket, a dwarf on a gryphon or an orc on a wind rider for the
-- Sky Rider Gunship, a gnomish flying machine for the Gyrocopter Gunship and the Sky Hornets, a big
-- flying machine or a big wind rider for the Siege Bomber, a cannon for the Dwarven Sentry Turret.
-- They are flown and fired by the server: passive, no melee, no XP (static_flags1 NO_XP |
-- NO_MELEE | IGNORE_COMBAT, and the flyers LARGE_AOI: seen from 200 yards), never spawned in the
-- world.
--
-- The rocket is untouchable (IMMUNE_TO_PC | IMMUNE_TO_NPC | UNINTERACTIBLE). Every other craft can
-- be shot down by the caller's enemies: selectable, not immune to players, its health its own
-- (NO_AUTOMATIC_REGEN; the server sets it, SkyStreaks.h CraftMaxHealth). The flyers stay out of
-- the creatures' reach (IMMUNE_TO_NPC); the turret, on the ground, takes their blows too.
--
-- Safe to apply again, in any order.
--
-- Reserved ranges (kept free):
--   creature_template.entry      93500-93599

DELETE FROM `creature_template` WHERE `entry` BETWEEN 93500 AND 93599;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `speed_walk`, `speed_run`,
  `type`, `unit_class`, `ai_name`, `inhabit_type`, `static_flags1`, `script_name`) VALUES
(93500, 0, 'Goblin Rocket', 'Killstreak', 60, 60, 35, 0, 10318, 1, 100, 100, 4, 14, 9, 1, 'NullAI', 4, 2182087266, ''),
(93501, 0, 'Sky Rider Gunship', 'Killstreak', 60, 60, 35, 0, 13471, 1, 100, 100, 4, 14, 7, 1, 'NullAI', 4, 2182087746, ''),
(93502, 0, 'Gyrocopter Gunship', 'Killstreak', 60, 60, 35, 0, 8011, 1, 100, 100, 4, 14, 9, 1, 'NullAI', 4, 2182087746, ''),
(93503, 0, 'Siege Bomber', 'Killstreak', 60, 60, 35, 0, 8011, 1, 100, 100, 4, 14, 9, 1, 'NullAI', 4, 2182087746, ''),
(93504, 0, 'Dwarven Sentry Turret', 'Killstreak', 60, 60, 35, 0, 3127, 1, 100, 100, 1, 1, 9, 1, 'NullAI', 1, 34604034, ''),
(93505, 0, 'Gnomish Sky Hornet', 'Killstreak', 60, 60, 35, 0, 8011, 1, 100, 100, 4, 14, 9, 1, 'NullAI', 4, 2182087746, '');
