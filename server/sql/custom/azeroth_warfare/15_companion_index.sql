-- World of Modern Warfare: the Companions window and its Bestiary
-- (server/src/fusion/include/fusion/CompanionIndex.h, game/Fusion/FusionCompanionIndex.cpp, the
-- client's crate::companion_ui). Apply to the world database after 13_companion_keeper.sql; safe to
-- apply again.
--
--  - The Companion Keeper leaves the world: its spawns and templates go. A game master's companion
--    tools (any species into the bags, any carried companion's level, stage, abilities, traits) are
--    in the Companions window now; players never buy companions.
--  - Stable masters need no change: speaking to any of them (gossip or the stable list) opens the
--    Companions window with the stable master's view (breeding, changing an ability).
--  - Nothing in the characters database: the server makes `character_fusion_companion_seen` (the
--    species each character has seen or caught) at startup, as it does its other fusion tables.

DELETE FROM `creature` WHERE `guid` BETWEEN 2592700 AND 2592799;
DELETE FROM `creature` WHERE `id` BETWEEN 92700 AND 92799;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 92700 AND 92799;
