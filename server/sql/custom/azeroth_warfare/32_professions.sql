-- World of Modern Warfare: Construction and Minstrelsy as stock-like professions (the Professions
-- hub, B). World DB. Apply after 30_minstrelsy.sql. Safe to apply again: plain UPDATEs to fixed
-- values.
--
-- Construction's pieces are now made at its trade window (the stock TradeSkillFrame the client
-- mirrors) into kits, kept per character in the characters table
-- `character_fusion_construction_kit` (made by the server at start, FusionConstruction.cpp), and
-- placed with the Build Structure ability on Utility's bar. An instrument readied in Utility
-- turns the bar into its notes, as a druid's form does; the item text says so.

UPDATE `item_template` SET `description` = 'An instrument of Minstrelsy (1). Ready it in Utility: the bar becomes its notes. Fire plays the song picked.'
  WHERE `entry` = 94101;
