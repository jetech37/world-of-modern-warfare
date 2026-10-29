-- World of Modern Warfare: Enchanting's specialized rounds (src/fusion/include/fusion/Rounds.h).
-- Apply to the world database after the vmangos world dump. Safe to run again: the package's item
-- range is cleared and written afresh.
--
-- Reserved (package W7): item_template.entry 93050-93199 (93050-93059 the rounds); the recipes'
-- ids 62600-62609 are the rules' own (Rounds::Recipes), not spells: vanilla's recipes are spells
-- in the client's Spell.dbc, which we can't add to, so enchanting trainers teach and make these
-- through a gossip menu (FusionRounds.cpp), and the learned recipes are kept in the characters
-- table `character_fusion_round_recipe`, which the server makes at startup.
--
-- The items carry no spell: the server's fusion hook catches the use (WorldSession's use-item
-- handler) and loads the rounds into the gun in hand. Class 0 / subclass 6 (consumable, item
-- enhancement, like weapon oils), stacks of 20, lesser common and greater uncommon. RoundTests
-- reads this file to hold the rows to the rules' names. Display ids marked VERIFY are picks from
-- vanilla's ammunition and want a look in game.

DELETE FROM `item_template` WHERE `entry` BETWEEN 93050 AND 93199;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`,
  `quality`, `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `stackable`, `max_count`, `item_level`,
  `required_level`, `bonding`) VALUES
(93050, 0, 0, 6, 'Lesser Incendiary Rounds', 'Use: load the gun in hand for 3 magazines. Hits burn for 30% more over 3 sec; each round hits 8% softer.', 5998, 1, 1, 0, 250, 0, 20, 0, 15, 10, 0),          -- VERIFY display (Exploding Shot's)
(93051, 0, 0, 6, 'Greater Incendiary Rounds', 'Use: load the gun in hand for 6 magazines. Hits burn for 30% more over 3 sec; each round hits 8% softer.', 5998, 2, 1, 0, 1000, 0, 20, 0, 45, 40, 0),       -- VERIFY display
(93052, 0, 0, 6, 'Lesser Frost Rounds', 'Use: load the gun in hand for 3 magazines. Hits slow the target by 30% for 1.5 sec; the gun fires 12% slower.', 31813, 1, 1, 0, 300, 0, 20, 0, 22, 15, 0),        -- Ice Threaded Bullet's look
(93053, 0, 0, 6, 'Greater Frost Rounds', 'Use: load the gun in hand for 6 magazines. Hits slow the target by 30% for 1.5 sec; the gun fires 12% slower.', 31813, 2, 1, 0, 1100, 0, 20, 0, 47, 42, 0),
(93054, 0, 0, 6, 'Lesser Armor-Piercing Rounds', 'Use: load the gun in hand for 3 magazines. Ignores half of the target''s bullet armor; 10% less damage.', 19422, 1, 1, 0, 400, 0, 20, 0, 30, 25, 0),          -- Mithril Gyro-Shot's look
(93055, 0, 0, 6, 'Greater Armor-Piercing Rounds', 'Use: load the gun in hand for 6 magazines. Ignores half of the target''s bullet armor; 10% less damage.', 19422, 2, 1, 0, 1200, 0, 20, 0, 50, 45, 0),
(93056, 0, 0, 6, 'Lesser Arcane Rounds', 'Use: load the gun in hand for 45 sec. Hits burn mana as they take health; 8% less damage.', 26613, 1, 1, 0, 500, 0, 20, 0, 36, 30, 0),            -- VERIFY display (Thorium Shells')
(93057, 0, 0, 6, 'Greater Arcane Rounds', 'Use: load the gun in hand for 90 sec. Hits burn mana as they take health; 8% less damage.', 26613, 2, 1, 0, 1300, 0, 20, 0, 53, 48, 0),         -- VERIFY display
(93058, 0, 0, 6, 'Lesser Holy Rounds', 'Use: load the gun in hand for 3 magazines. 15% more damage to undead and demons, 10% less to anything else.', 2418, 1, 1, 0, 600, 0, 20, 0, 42, 35, 0),      -- VERIFY display (Rockshard Pellets')
(93059, 0, 0, 6, 'Greater Holy Rounds', 'Use: load the gun in hand for 6 magazines. 15% more damage to undead and demons, 10% less to anything else.', 2418, 2, 1, 0, 1500, 0, 20, 0, 57, 52, 0);   -- VERIFY display
