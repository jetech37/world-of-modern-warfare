-- World of Modern Warfare: the class options' stabilisation pass (design/CLASS_CATALOG.md
-- section 15). World DB; safe to apply again (it only replaces its own spell_chain rows and sets
-- the same levels).
--
-- Everything else of the pass lives in code: the refits of the class spells (Fusion::Classes::
-- Refits, applied at startup), the basic damaging spells (Classes::BasicSpellOf) and the options'
-- custom parts (game/Fusion/FusionClassAbilities.cpp).
--
-- Crusader Strike (the Crusader's tier 1 option) is a vanilla-era paladin spell left in the data
-- unfinished: its five ranks (2537, 8823, 8824, 10336, 10337) have no chain and spellLevel 99, so
-- every paladin got rank 5 at level 10. Chain them and give each rank its own level (its
-- baseLevel: 10, 22, 34, 46, 58), so the server teaches the rank the level allows, as with every
-- other ranked option.

REPLACE INTO `spell_chain` (`spell_id`, `prev_spell`, `first_spell`, `rank`, `req_spell`, `build_min`, `build_max`) VALUES
(2537, 0, 2537, 1, 0, 0, 5875),
(8823, 2537, 2537, 2, 0, 0, 5875),
(8824, 8823, 2537, 3, 0, 0, 5875),
(10336, 8824, 2537, 4, 0, 0, 5875),
(10337, 10336, 2537, 5, 0, 0, 5875);

UPDATE `spell_template` SET `spellLevel` = `baseLevel`
WHERE `entry` IN (2537, 8823, 8824, 10336, 10337) AND `spellLevel` = 99;
