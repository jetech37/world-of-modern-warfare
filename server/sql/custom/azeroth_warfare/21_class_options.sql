-- World of Modern Warfare: the class options' polish pass (design/CLASS_CATALOG.md section 15).
-- World DB; safe to apply again (it deletes and writes back only its own rows).
--
-- Reserved:
--   item_template.entry          93450-93459 (93450 the Arcane Stim)
--
-- Everything else of the pass lives in code: the refits (Fusion::Classes::Refits), the rules of
-- the custom effects (fusion/ClassEffects.h) and the options' custom parts
-- (game/Fusion/FusionClassAbilities.cpp).
--
-- The Artificer's Conjure Supplies (Conjure Mana Ruby 10054, its own item replaced) conjures Arcane
-- Stims for the squad, four at most carried. The stim carries no spell: the fusion uses it
-- (FusionClasses::UseClassItem: 25% of one's health back and 20% faster for 4 s). Conjured (flag
-- 2), tradeable, stacks of four.

DELETE FROM `item_template` WHERE `entry` BETWEEN 93450 AND 93459;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`) VALUES
(93450, 0, 0, 0, 'Arcane Stim', 'Use: heals 25% of your health and you run 20% faster for 4 sec. Conjured by a mage.', 24152, 1, 2, 1, 0, 0, 0, 40, 0, 4, 4, 0, 0);   -- Major Healing Potion's look
