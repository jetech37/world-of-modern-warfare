-- World of Modern Warfare: kits are edited at every trainer and innkeeper (WOMW design, section 3).
-- Their gossip gains "Edit your kits" (FusionMgr::AddKitGossip; a trainer without a gossip menu gets
-- a short one, FusionMgr::TrainerKitGossip), which opens the kit window to edit; kit edits and
-- switches are saved within reach of one (fusion/Kits.h, KitStationAllows) or through the kit
-- window's field editor. Nothing is added to the world for that: this file only takes away what
-- the Quartermasters left.
--
-- Gone:
--   - the Quartermasters: creature_template 92001-92199 (04_quartermasters.sql's generic one and
--     05_quartermasters_named.sql's named ones), their spawns (guids 9200000-9200199) and wares;
--   - the Prestige Quartermaster: creature_template 92500-92599 (11_prestige_professions.sql), its
--     spawns (guids 2592500-2592599). Its token shop is the Prestige tab's now.
-- Kept: the Field Quartermaster's Ledger (item 93001), so copies already in bags still open the field
-- editor; nobody sells it any more.
--
-- Safe to apply again: every statement deletes by range.

DELETE FROM `npc_vendor` WHERE `entry` BETWEEN 92001 AND 92199 OR `entry` BETWEEN 92500 AND 92599;
DELETE FROM `creature_addon` WHERE `guid` BETWEEN 9200000 AND 9200199 OR `guid` BETWEEN 2592500 AND 2592599;
DELETE FROM `creature` WHERE `id` BETWEEN 92001 AND 92199 OR `guid` BETWEEN 9200000 AND 9200199;
DELETE FROM `creature` WHERE `id` BETWEEN 92500 AND 92599 OR `guid` BETWEEN 2592500 AND 2592599;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 92001 AND 92199;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 92500 AND 92599;
