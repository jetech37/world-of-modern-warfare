-- World of Modern Warfare: the token shop's goods sell to no vendor (security review 2026-10, #23;
-- design/SECURITY_REVIEW_2026-10.md). World DB. Apply after 02_companions.sql and
-- 11_prestige_professions.sql (it changes rows they write). Safe to apply again: it only sets
-- prices to 0. (Salvaged materials, 93600-93649, are written with no price by
-- 24_construction_rules.sql.)
--
-- The token shop's goods (Prestige::Shop) are bought with prestige tokens: at a vendor they were
-- coin. Kept in step with Prestige::Shop() by a server test (fusion_tests,
-- security_handed_out_goods_sell_for_nothing).
UPDATE `item_template` SET `sell_price` = 0 WHERE `entry` IN (
  93260,   -- Focus Stim
  93261,   -- Second Wind Stim
  93262,   -- Adrenaline Stim
  93200,   -- Spare Thorium Grenade (field gear)
  93205,   -- Spare Arcane Flash Bomb (field gear)
  93243,   -- Prestige Ghillie Mantle
  93223,   -- Prestige Bandolier
  90006    -- Wolf Pup Carrier
);
