-- World of Modern Warfare: reviving fainted battle companions (src/fusion/include/fusion/CompanionRest.h,
-- game/Fusion/FusionCompanionRevive.cpp). Apply to the world database after 02_companions.sql; safe
-- to apply again (each block clears its own rows first).
--
-- Reserved ranges:
--   item_template.entry          93470-93479 (93470 the Reviving Draught, Companions::kReviveDraughtItem)
--   npc_vendor                   item 93470, at the general goods vendors
--
-- A companion beaten to 0 health faints and stays fainted until a stable master revives it (a small
-- fee by level, Companions.StableReviveCostPerLevel), a Reviving Draught does, or an hour passes
-- (Companions.FaintRecoverMinutes). The characters database needs nothing here: the server makes its
-- `character_fusion_companion_rest` table at startup.

-- ── The Reviving Draught ────────────────────────────────────────────────────────────────────────
-- No spell, as the companion items: the client sends its use on the companion channel
-- (Index::kOpUseDraught, item 0) and the server revives the companion that fainted last, at half
-- health. Stackable, tradeable. Stable masters sell it too (their gossip and the Companions window,
-- at its buy price below).
DELETE FROM `item_template` WHERE `entry` BETWEEN 93470 AND 93479;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(93470, 0, 0, 0, 'Reviving Draught', 'Use: revives your battle companion that fainted last, at half health. Or use a fainted companion''s item twice to give it one.', 8453, 1, 0, 1, 1500, 375, 0, 10, 0, 0, 10, 0, 0);   -- VERIFY display (Free Action Potion's look)

-- ── Sold by the general goods vendors ───────────────────────────────────────────────────────────
-- Every vendor titled "General ..." (General Supplies, General Goods, ...) that sells water, Simple
-- Wood or a Small Brown Pouch: the general goods vendors of the towns and villages.
DELETE FROM `npc_vendor` WHERE `item` BETWEEN 93470 AND 93479;
INSERT IGNORE INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`)
SELECT DISTINCT v.`entry`, 100, 93470, 0, 0
FROM `npc_vendor` v
JOIN `creature_template` c ON c.`entry` = v.`entry`
WHERE v.`item` IN (159, 4470, 4496) AND c.`subname` LIKE '%General%';
