-- World of Modern Warfare: open-world construction and a more dangerous world (design doc,
-- sections 5, 10 and 12; server/src/fusion/include/fusion/Construction.h, Build.h, Danger.h,
-- NpcGuns.h). Apply to the world database after 03_construction.sql; safe to apply again.
--
-- Reserved (unused so far): creature_template.entry 92200-92299.
--
-- Most of this package is code: the danger table (creature health and damage), the gunner camps
-- (by faction template), piece health, salvage and reinforcement live in the rules library, and
-- the characters table `fusion_build_piece` gains its `reinforcement` column at startup
-- (FusionMgr::LoadPieces). What the world database holds:

-- ── The sentry gun is a killstreak now ──────────────────────────────────────────────────────────
-- Construction no longer builds one, so its three parts are crafted no more. Any parts already
-- made (and those of sentries taken down at startup, put in their builders' stashes) stay as
-- trade goods to sell.
-- id-override: 03_construction.sql fusion_recipe.id (the sentry parts are crafted no more)
DELETE FROM `fusion_recipe` WHERE `id` IN (91005, 91007, 91008);
UPDATE `item_template` SET `description` = 'Salvage from an old sentry gun. Sentries come from a killstreak now.'
WHERE `entry` IN (91025, 91026, 91027);
