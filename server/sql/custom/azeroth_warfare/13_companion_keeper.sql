-- World of Modern Warfare: the Companion Keeper, retired.
--
-- The keeper was a gossip NPC (creatures 92700-92799) that sold any companion and changed the ones a
-- player carried. Players no longer buy companions (they drop and are bred), and a game master's
-- tools moved into the Companions window (server/src/fusion/include/fusion/CompanionIndex.h,
-- game/Fusion/FusionCompanionIndex.cpp), so a fresh world spawns no keeper. This file only clears
-- its reserved ranges; safe to apply again, in any order. 15_companion_index.sql does the same for a
-- world that had the keeper.
--
-- Reserved ranges (kept free):
--   creature_template.entry      92700-92799
--   creature.guid                2592700-2592799

DELETE FROM `creature` WHERE `guid` BETWEEN 2592700 AND 2592799;
DELETE FROM `creature` WHERE `id` BETWEEN 92700 AND 92799;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 92700 AND 92799;
