-- World of Modern Warfare: the creature displays the patch layer adds (patch-9.MPQ, built from a
-- WotLK 3.3.5a install by `cargo run --release --example wotlk_import`; client/src/wotlk). World
-- DB; safe to apply again (REPLACE).
--
-- The display ids are the client's `wotlk::import::LOOKS` table (a client test holds this file to
-- it). The server knows a display only when its own CreatureDisplayInfo.dbc has the row: the
-- package copies the patch layer's tables into data\5875\dbc (package/build.ps1). Without them
-- the companions fall back to their creature's own look (FusionCompanions.cpp logs the miss).
--
-- creature_display_info_addon: bounding radius and combat reach in yards at scale 1, as vanilla's
-- rows for creatures of the same size; walk and run speed 1 (the client animates by moveSpeed).

REPLACE INTO `creature_display_info_addon` (`display_id`, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`) VALUES
(40001, 5875, 0.3, 1.0, 1, 1, 2, 0), -- raptor hatchling, orange
(40002, 5875, 0.3, 1.0, 1, 1, 2, 0), -- raptor hatchling, mottled blue-green
(40003, 5875, 0.3, 1.0, 1, 1, 2, 0), -- raptor hatchling, mottled dark green
(40004, 5875, 0.3, 1.0, 1, 1, 2, 0), -- raptor hatchling, red
(40005, 5875, 0.3, 1.0, 1, 1, 2, 0), -- baby crocolisk, green
(40006, 5875, 0.3, 1.0, 1, 1, 2, 0), -- baby crocolisk, brown
(40007, 5875, 1.5, 3.0, 1, 1, 2, 0), -- proto-drake, black
(40008, 5875, 1.5, 3.0, 1, 1, 2, 0), -- proto-drake, red
(40009, 5875, 1.5, 3.0, 1, 1, 2, 0), -- proto-drake, green
(40010, 5875, 1.5, 3.0, 1, 1, 2, 0), -- proto-drake, blue
(40011, 5875, 1.0, 2.0, 1, 1, 2, 0), -- armored bear, brown
(40012, 5875, 1.0, 2.0, 1, 1, 2, 0), -- armored bear, black
(40013, 5875, 1.0, 2.0, 1, 1, 2, 0), -- armored bear, white
(40014, 5875, 0.75, 1.5, 1, 1, 2, 0), -- spectral tiger
(40015, 5875, 1.0, 2.0, 1, 1, 2, 0), -- phoenix
(40016, 5875, 1.0, 1.5, 1, 1, 2, 0), -- riding turtle
(40017, 5875, 0.3, 1.0, 1, 1, 2, 0), -- hawkstrider hatchling
(40018, 5875, 0.3, 1.0, 1, 1, 2, 0); -- phoenix hatchling

-- The companion forms that wear them (on top of 29_companion_looks.sql and 31_vanilla_companions.sql;
-- apply after both). Each row sets one stage's display and scale; a ridden last form sets the
-- ride display too. Mirrored by kWotlkLooks in server/src/fusion/src/Companions.cpp; the client's
-- wotlk::import tests check every form against the patch layer's tables and the size rules.
-- Left out: the black whelpling (a proto-drake would need a scale under the catalog's 0.15) and the
-- ridden bears 104/105 (a ridden form stays at scale 1, where the armored bear is no bigger than
-- their second form). VERIFY in game: every form below.
UPDATE `fusion_companion_species` SET `stage3_display` = 40008, `stage3_scale` = 0.16 WHERE `id` = 2;   -- Crimson Whelpling: red proto-drake
UPDATE `fusion_companion_species` SET `stage3_display` = 40009, `stage3_scale` = 0.16 WHERE `id` = 3;   -- Emerald Whelpling: green proto-drake
UPDATE `fusion_companion_species` SET `stage3_display` = 40010, `stage3_scale` = 0.16 WHERE `id` = 4;   -- Azure Whelpling: blue proto-drake
UPDATE `fusion_companion_species` SET `stage3_display` = 40011, `stage3_scale` = 1.48 WHERE `id` = 10;    -- Bear Cub: armored bear
UPDATE `fusion_companion_species` SET `stage1_display` = 40001, `stage1_scale` = 0.8 WHERE `id` = 12;  -- Raptor Hatchling
UPDATE `fusion_companion_species` SET `stage1_display` = 40005, `stage1_scale` = 1 WHERE `id` = 13;    -- Young Crocolisk: baby crocolisk
UPDATE `fusion_companion_species` SET `stage1_display` = 40017, `stage1_scale` = 1 WHERE `id` = 17;    -- Plainstrider Chick: hawkstrider hatchling
UPDATE `fusion_companion_species` SET `stage1_display` = 40018, `stage1_scale` = 0.3, `stage3_display` = 40015, `stage3_scale` = 0.2 WHERE `id` = 19; -- Ember Spark: phoenix hatchling, phoenix
UPDATE `fusion_companion_species` SET `stage1_display` = 40002, `stage1_scale` = 0.8 WHERE `id` = 112; -- Sunscale Hatchling
UPDATE `fusion_companion_species` SET `stage1_display` = 40003, `stage1_scale` = 0.8 WHERE `id` = 113; -- Deviate Hatchling
UPDATE `fusion_companion_species` SET `stage1_display` = 40004, `stage1_scale` = 0.8 WHERE `id` = 114; -- Bloodfen Hatchling
UPDATE `fusion_companion_species` SET `stage1_display` = 40006, `stage1_scale` = 1 WHERE `id` = 117;   -- Drywallow Hatchling: baby crocolisk
UPDATE `fusion_companion_species` SET `stage1_display` = 40017, `stage1_scale` = 1 WHERE `id` = 129;   -- Foreststrider Fledgling: hawkstrider hatchling
UPDATE `fusion_companion_species` SET `stage3_display` = 40013, `stage3_scale` = 1 WHERE `id` = 208;   -- Poley: white armored bear
UPDATE `fusion_companion_species` SET `stage3_display` = 40014, `stage3_scale` = 0.73 WHERE `id` = 213; -- White Tiger Cub: spectral tiger
