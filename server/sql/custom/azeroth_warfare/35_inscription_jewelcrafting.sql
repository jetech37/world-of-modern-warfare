-- World of Modern Warfare: Inscription and Jewelcrafting, the layer's two primary professions
-- (server/src/fusion/include/fusion/Crafts.h, game/Fusion/FusionCrafts.cpp; design/GAME_DESIGN.md
-- section 12). World DB. Apply after 30_minstrelsy.sql. Safe to apply again: every block clears its
-- own rows first. The characters tables (skills, recipes, the kits' glyphs and gems) are made by
-- the server at start.
--
-- Reserved ranges:
--   item_template.entry          94300-94449 Inscription (94300-94305 pigments, 94310-94315 inks,
--                                94320-94321 parchment, 94330-94379 glyphs: Crafts::Item, Glyphs())
--                                94450-94599 Jewelcrafting (94450-94479 cut gems: Crafts::Gems();
--                                94480-94484 wire, settings and the Jeweler's Kit; 94490-94519 jewelry)
--   creature_template.entry      94300 the Inscription Trainer, 94450 the Jewelcrafting Trainer
--                                (Crafts::kTrainerInscription, kTrainerJewelcrafting)
--   creature.guid                2594300-2594349 (Inscription), 2594450-2594499 (Jewelcrafting)
--
-- Rows marked VERIFY need a look in game: every item copies the display of a vanilla item named
-- beside it (1.12 has no pigments, inks, glyphs or cut gems of its own; cut gems wear their raw
-- gem's look, jewelry the first uncommon ring or necklace's), the trainers copy a vanilla trainer's
-- look, and every spawn stands two yards off the Minstrel Trainer's by the same innkeeper.

DELETE FROM `item_template` WHERE `entry` BETWEEN 94300 AND 94599;

-- ── Inscription: pigments, inks, parchment ─────────────────────────────────────────────────────
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94300, 0, 7, 0, 'Alabaster Pigment', 'Milled from Peacebloom, Silverleaf and Earthroot.',
  t.`display_id`, 1, 0, 1, 0, 5, 0, 5, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 10940 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Strange Dust's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94301, 0, 7, 0, 'Dusky Pigment', 'Milled from Mageroyal, Briarthorn, Swiftthistle, Bruiseweed and Stranglekelp.',
  t.`display_id`, 1, 0, 1, 0, 15, 0, 15, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 10940 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Strange Dust's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94302, 0, 7, 0, 'Golden Pigment', 'Milled from Wild Steelbloom, Grave Moss, Kingsblood and Liferoot.',
  t.`display_id`, 1, 0, 1, 0, 25, 0, 25, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 10940 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Strange Dust's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94303, 0, 7, 0, 'Emerald Pigment', 'Milled from Fadeleaf, Goldthorn, Khadgar''s Whisker and Wintersbite.',
  t.`display_id`, 1, 0, 1, 0, 35, 0, 35, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 10940 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Strange Dust's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94304, 0, 7, 0, 'Violet Pigment', 'Milled from Firebloom, Purple Lotus, Arthas'' Tears, Sungrass, Blindweed, Ghost Mushroom and Gromsblood.',
  t.`display_id`, 1, 0, 1, 0, 45, 0, 45, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 10940 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Strange Dust's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94305, 0, 7, 0, 'Silvery Pigment', 'Milled from Golden Sansam, Dreamfoil, Mountain Silversage, Plaguebloom, Icecap and Black Lotus.',
  t.`display_id`, 1, 0, 1, 0, 55, 0, 55, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 10940 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Strange Dust's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94310, 0, 7, 0, 'Moonglow Ink', 'An ink of Inscription, for scrolls and glyphs.',
  t.`display_id`, 1, 0, 1, 0, 10, 0, 5, 0, 20, 0, 4 FROM `item_template` t WHERE t.`entry` = 3371 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Empty Vial's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94311, 0, 7, 0, 'Midnight Ink', 'An ink of Inscription, for scrolls and glyphs.',
  t.`display_id`, 1, 0, 1, 0, 35, 0, 15, 0, 20, 0, 4 FROM `item_template` t WHERE t.`entry` = 3371 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Empty Vial's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94312, 0, 7, 0, 'Lion''s Ink', 'An ink of Inscription, for scrolls and glyphs.',
  t.`display_id`, 1, 0, 1, 0, 60, 0, 25, 0, 20, 0, 4 FROM `item_template` t WHERE t.`entry` = 3371 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Empty Vial's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94313, 0, 7, 0, 'Jadefire Ink', 'An ink of Inscription, for scrolls and glyphs.',
  t.`display_id`, 1, 0, 1, 0, 85, 0, 35, 0, 20, 0, 4 FROM `item_template` t WHERE t.`entry` = 3371 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Empty Vial's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94314, 0, 7, 0, 'Celestial Ink', 'An ink of Inscription, for scrolls and glyphs.',
  t.`display_id`, 1, 0, 1, 0, 110, 0, 45, 0, 20, 0, 4 FROM `item_template` t WHERE t.`entry` = 3371 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Empty Vial's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94315, 0, 7, 0, 'Shimmering Ink', 'An ink of Inscription, for scrolls and glyphs.',
  t.`display_id`, 1, 0, 1, 0, 135, 0, 55, 0, 20, 0, 4 FROM `item_template` t WHERE t.`entry` = 3371 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Empty Vial's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94320, 0, 7, 0, 'Light Parchment', 'Sold by Inscription Trainers.',
  t.`display_id`, 1, 0, 1, 15, 4, 0, 5, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 1180 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (a scroll's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94321, 0, 7, 0, 'Resilient Parchment', 'Sold by Inscription Trainers.',
  t.`display_id`, 1, 0, 1, 120, 30, 0, 30, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10307 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (a scroll's)

-- ── Inscription: glyphs (Crafts::Glyphs; inscribed into a kit at a kit station) ─────────────────
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94330, 0, 15, 0, 'Glyph of the Quick Draw', 'Major Glyph: Weapon swaps 12% quicker; aiming down sights 4% slower. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 240, 0, 16, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94331, 0, 15, 0, 'Glyph of the Steady Hand', 'Major Glyph: Hip fire 8% tighter; out of a sprint 5% slower. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 270, 0, 18, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94332, 0, 15, 0, 'Glyph of Iron Skin', 'Major Glyph: 15% less flinch from rounds that hit you; weapon swaps 5% slower. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 315, 0, 21, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94333, 0, 15, 0, 'Glyph of Second Wind', 'Major Glyph: Regeneration starts sooner (15% of its delay) but runs 5% slower. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 390, 0, 26, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94334, 0, 15, 0, 'Glyph of the Sapper', 'Major Glyph: Your flashes, stuns and smoke 10% stronger. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 435, 0, 29, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94335, 0, 15, 0, 'Glyph of the Quartermaster', 'Major Glyph: The field rearm is ready a quarter sooner. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 495, 0, 33, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94336, 0, 15, 0, 'Glyph of Last Rites', 'Major Glyph: Down in Last Stand, you bleed out 5 seconds later. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 555, 0, 37, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94337, 0, 15, 0, 'Glyph of the Hunter''s Eye', 'Major Glyph: You see stealth 3 yards farther. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 615, 0, 41, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94338, 0, 15, 0, 'Glyph of the Shadow', 'Major Glyph: Creatures notice you 7% later. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 660, 0, 44, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94339, 0, 15, 0, 'Glyph of the Heavy Gunner', 'Major Glyph: An LMG''s or sniper''s weight slows you 10% less. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 780, 0, 52, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94340, 0, 15, 0, 'Glyph of Quickened Words', 'Major Glyph: Your spells cast 6% quicker; reloads 5% slower. Casters only. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 705, 0, 47, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94341, 0, 15, 0, 'Glyph of the Bladesworn', 'Major Glyph: Your weapon''s melee hits 6% harder; aiming down sights 4% slower. Warriors, paladins and rogues only. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 705, 0, 47, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94342, 0, 15, 0, 'Glyph of the Tracker', 'Major Glyph: You see stealth 5 yards farther and creatures notice you 5% later. Hunters only. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 705, 0, 47, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94350, 0, 15, 0, 'Glyph of the Long Road', 'Minor Glyph: Out of combat you run 3% faster. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 1, 0, 1, 0, 150, 0, 10, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94351, 0, 15, 0, 'Glyph of the Feather', 'Minor Glyph: Falls hurt 20% less. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 1, 0, 1, 0, 180, 0, 12, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94352, 0, 15, 0, 'Glyph of the Gravedigger', 'Minor Glyph: 10% more gold looted from creatures. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 1, 0, 1, 0, 345, 0, 23, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94353, 0, 15, 0, 'Glyph of the Field Editor', 'Minor Glyph: The field editor is ready 10 minutes sooner. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 1, 0, 1, 0, 465, 0, 31, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94354, 0, 15, 0, 'Glyph of the Veteran', 'Minor Glyph: 3% more experience from kills. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 1, 0, 1, 0, 585, 0, 39, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94355, 0, 15, 0, 'Glyph of the Sprinter', 'Minor Glyph: Out of a sprint 4% quicker. Inscribe it into the kit you carry at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 1, 0, 1, 0, 735, 0, 49, 0, 20, 0, 7 FROM `item_template` t WHERE t.`entry` = 10309 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Scroll of Agility IV's)

-- ── Jewelcrafting: cut gems (Crafts::Gems; set into a kit's guns at a kit station) ──────────────
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94450, 0, 7, 0, 'Bold Tigerseye', 'Red gem: Aim down sights 3% quicker. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 50, 0, 10, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 818 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94451, 0, 7, 0, 'Solid Malachite', 'Blue gem: 3% less recoil. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 90, 0, 13, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 774 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94452, 0, 7, 0, 'Shifting Shadowgem', 'Red and blue gem: Hip fire 2% tighter, 2% less flinch. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 130, 0, 16, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 1210 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94453, 0, 7, 0, 'Jagged Moss Agate', 'Yellow and blue gem: Out of a sprint 3% quicker, 2% less sway. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 170, 0, 19, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 1206 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94454, 0, 7, 0, 'Lustrous Moonstone', 'Blue gem: 4% less scope sway (a scoped gun only). Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 210, 0, 22, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 1705 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94455, 0, 7, 0, 'Quick Jade', 'Yellow gem: Reloads 3% quicker. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 250, 0, 25, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 1529 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94456, 0, 7, 0, 'Rigid Citrine', 'Yellow gem: Weapon swaps 4% quicker. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 290, 0, 28, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 3864 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94457, 0, 7, 0, 'Inscribed Citrine', 'Red and yellow gem: Aim 2% quicker, reload 2% quicker. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 330, 0, 31, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 3864 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94458, 0, 7, 0, 'Sparkling Aquamarine', 'Blue gem: 5% less recoil. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 370, 0, 34, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 7909 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94459, 0, 7, 0, 'Bright Star Ruby', 'Red gem: Aim down sights 5% quicker. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 410, 0, 37, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 7910 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94460, 0, 7, 0, 'Smooth Large Opal', 'Yellow gem: Reloads 5% quicker. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 450, 0, 40, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 12799 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94461, 0, 7, 0, 'Radiant Huge Emerald', 'Yellow and blue gem: Out of a sprint 4% quicker, 3% less bloom. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 490, 0, 43, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 12364 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94462, 0, 7, 0, 'Stormy Blue Sapphire', 'Blue gem: 8% less flinch. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 530, 0, 46, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 12361 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94463, 0, 7, 0, 'Runed Star Ruby', 'Red gem: Hip fire 5% tighter. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 570, 0, 49, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 7910 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94464, 0, 7, 0, 'Prismatic Arcane Crystal', 'Every colour gem: 4% less bloom. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 2, 0, 1, 0, 610, 0, 52, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 12363 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94465, 0, 7, 0, 'Relentless Azerothian Diamond', 'Meta gem: Regeneration starts sooner (8% of its delay). Wakes with gems of two colours in the gun. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 3, 0, 1, 0, 650, 0, 55, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 12800 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94466, 0, 7, 0, 'Swift Azerothian Diamond', 'Meta gem: 5% of weak-spot hits on you turned to body hits. Wakes with gems of two colours in the gun. Set it into a kit''s gun at a trainer or an innkeeper (Glyphs & Gems, the Professions hub).',
  t.`display_id`, 3, 0, 1, 0, 690, 0, 58, 0, 20, 0, 0 FROM `item_template` t WHERE t.`entry` = 12800 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (its raw gem's)

-- ── Jewelcrafting: wire, settings and the kit ───────────────────────────────────────────────────
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94480, 0, 7, 0, 'Delicate Copper Wire', 'Jewelcrafting''s first material.',
  t.`display_id`, 1, 0, 1, 0, 8, 0, 5, 0, 20, 0, 1 FROM `item_template` t WHERE t.`entry` = 2840 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Copper Bar's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94481, 0, 7, 0, 'Bronze Setting', 'A setting for jewelry.',
  t.`display_id`, 1, 0, 1, 0, 30, 0, 15, 0, 20, 0, 1 FROM `item_template` t WHERE t.`entry` = 2841 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Bronze Bar's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94482, 0, 7, 0, 'Mithril Filigree', 'A setting for jewelry.',
  t.`display_id`, 1, 0, 1, 0, 120, 0, 35, 0, 20, 0, 1 FROM `item_template` t WHERE t.`entry` = 3860 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Mithril Bar's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94483, 0, 7, 0, 'Thorium Setting', 'A setting for jewelry and the finest cuts.',
  t.`display_id`, 1, 0, 1, 0, 250, 0, 50, 0, 20, 0, 1 FROM `item_template` t WHERE t.`entry` = 12359 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Thorium Bar's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`)
SELECT 94484, 0, 15, 0, 'Jeweler''s Kit', 'Cutting gems and making jewelry ask it in the bags.',
  t.`display_id`, 1, 0, 1, 300, 75, 0, 5, 0, 1, 0, 1 FROM `item_template` t WHERE t.`entry` = 5956 ORDER BY t.`patch` LIMIT 1;   -- VERIFY look (Blacksmith Hammer's)

-- ── Jewelcrafting: jewelry (vanilla's stats: 3 Agility, 4 Strength, 5 Intellect, 6 Spirit, 7 Stamina) ──
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94490, 0, 4, 0, 'Braided Copper Ring', 'Made by a jeweler.', t.`display_id`, 1, 0, 1, 0, 300, 11, 10, 5, 1, 2, 1, 7, 1, 6, 1
FROM `item_template` t WHERE t.`inventory_type` = 11 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon ring's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`)
SELECT 94491, 0, 4, 0, 'Woven Copper Ring', 'Made by a jeweler.', t.`display_id`, 1, 0, 1, 0, 600, 11, 15, 10, 1, 2, 1, 4, 2
FROM `item_template` t WHERE t.`inventory_type` = 11 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon ring's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94492, 0, 4, 0, 'Tigerseye Band', 'Made by a jeweler.', t.`display_id`, 2, 0, 1, 0, 900, 11, 20, 15, 1, 2, 1, 3, 3, 7, 1
FROM `item_template` t WHERE t.`inventory_type` = 11 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon ring's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94493, 0, 4, 0, 'Malachite Pendant', 'Made by a jeweler.', t.`display_id`, 2, 0, 1, 0, 1080, 2, 23, 18, 1, 2, 1, 7, 3, 5, 2
FROM `item_template` t WHERE t.`inventory_type` = 2 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon necklace's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94494, 0, 4, 0, 'Bronze Band of Force', 'Made by a jeweler.', t.`display_id`, 2, 0, 1, 0, 1440, 11, 29, 24, 1, 2, 1, 4, 4, 7, 3
FROM `item_template` t WHERE t.`inventory_type` = 11 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon ring's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94495, 0, 4, 0, 'Jade Pendant of Guarding', 'Made by a jeweler.', t.`display_id`, 2, 0, 1, 0, 1800, 2, 35, 30, 1, 2, 1, 7, 6, 4, 3
FROM `item_template` t WHERE t.`inventory_type` = 2 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon necklace's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94496, 0, 4, 0, 'Citrine Ring of Swiftness', 'Made by a jeweler.', t.`display_id`, 2, 0, 1, 0, 2220, 11, 42, 37, 1, 2, 1, 3, 7, 7, 4
FROM `item_template` t WHERE t.`inventory_type` = 11 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon ring's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94497, 0, 4, 0, 'Aquamarine Signet', 'Made by a jeweler.', t.`display_id`, 2, 0, 1, 0, 2640, 11, 49, 44, 1, 2, 1, 5, 8, 6, 6
FROM `item_template` t WHERE t.`inventory_type` = 11 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon ring's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94498, 0, 4, 0, 'Star Ruby Choker', 'Made by a jeweler.', t.`display_id`, 3, 0, 1, 0, 3000, 2, 55, 50, 1, 2, 1, 4, 9, 7, 8
FROM `item_template` t WHERE t.`inventory_type` = 2 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon necklace's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94499, 0, 4, 0, 'Sapphire Band of the Sage', 'Made by a jeweler.', t.`display_id`, 3, 0, 1, 0, 3300, 11, 60, 55, 1, 2, 1, 5, 11, 7, 8
FROM `item_template` t WHERE t.`inventory_type` = 11 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon ring's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94500, 0, 4, 0, 'Opal Pendant of the Hunt', 'Made by a jeweler.', t.`display_id`, 3, 0, 1, 0, 3480, 2, 63, 58, 1, 2, 1, 3, 12, 7, 9
FROM `item_template` t WHERE t.`inventory_type` = 2 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon necklace's)
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `stackable`, `bonding`, `material`, `stat_type1`, `stat_value1`, `stat_type2`, `stat_value2`)
SELECT 94501, 0, 4, 0, 'Emerald Signet of Endurance', 'Made by a jeweler.', t.`display_id`, 3, 0, 1, 0, 3600, 11, 65, 60, 1, 2, 1, 7, 15, 4, 8
FROM `item_template` t WHERE t.`inventory_type` = 11 AND t.`quality` = 2 AND t.`entry` < 20000 ORDER BY t.`entry`, t.`patch` LIMIT 1;   -- VERIFY look (the first uncommon ring's)

-- ── The trainers ────────────────────────────────────────────────────────────────────────────────
-- Not spell trainers (the trades are no SkillLine.dbc lines): gossip NPCs whose script opens the
-- trade's window (train the tiers, learn the recipes, buy the goods, unlearn). Faction 35:
-- friendly to everyone.
DELETE FROM `creature_template` WHERE `entry` IN (94300, 94450);
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94300, 0, 'Scribe Halvorn Inkwell', 'Inscription Trainer', 40, 40, 35, 1, COALESCE((SELECT t.`display_id1` FROM `creature_template` t
  WHERE t.`subname` = 'Enchanting Trainer' ORDER BY t.`entry`, t.`patch` LIMIT 1), 4026), 1, 100, 100, 7, 1, 1, 'npc_fusion_craft_trainer';   -- VERIFY look (an enchanting trainer's)
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
  `display_id1`, `display_scale1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `civilian`, `script_name`)
SELECT 94450, 0, 'Gemcutter Tessa Brightfacet', 'Jewelcrafting Trainer', 40, 40, 35, 1, COALESCE((SELECT t.`display_id1` FROM `creature_template` t
  WHERE t.`subname` = 'Blacksmithing Trainer' ORDER BY t.`entry`, t.`patch` LIMIT 1), 4026), 1, 100, 100, 7, 1, 1, 'npc_fusion_craft_trainer';   -- VERIFY look (a blacksmithing trainer's)

-- By the innkeeper of each capital and of four busy inns, beside the Minstrel Trainer (two yards
-- either way along x). VERIFY: every position.
DELETE FROM `creature` WHERE `guid` BETWEEN 2594300 AND 2594349;
DELETE FROM `creature` WHERE `guid` BETWEEN 2594450 AND 2594499;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
  `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
(2594301, 94300, 0, -8867.56, 672.73, 97.99, 5.20, 300, 300, 0, 0),    -- Stormwind, the Gilded Rose
(2594302, 94300, 0, -4840.65, -857.40, 502.00, 4.87, 300, 300, 0, 0),    -- Ironforge, the Stonefire Tavern
(2594303, 94300, 1, 10131.49, 2226.00, 1328.81, 2.22, 300, 300, 0, 0),    -- Darnassus
(2594304, 94300, 1, 1636.73, -4437.51, 15.52, 2.76, 300, 300, 0, 0),    -- Orgrimmar, Valley of Strength
(2594305, 94300, 1, -1297.22, 36.80, 129.29, 0.56, 300, 300, 0, 0),    -- Thunder Bluff
(2594306, 94300, 0, 1637.99, 225.24, -43.02, 2.84, 300, 300, 0, 0),    -- Undercity
(2594307, 94300, 0, -9460.46, 18.18, 57.05, 3.04, 300, 300, 0, 0),    -- Goldshire, the Lion's Pride Inn
(2594308, 94300, 1, -405.71, -2643.31, 96.31, 3.44, 300, 300, 0, 0),    -- the Crossroads
(2594309, 94300, 0, -14457.19, 496.69, 15.21, 3.98, 300, 300, 0, 0),    -- Booty Bay, the Salty Sailor Tavern
(2594310, 94300, 1, -7155.10, -3840.87, 8.85, 1.95, 300, 300, 0, 0),    -- Gadgetzan
(2594451, 94450, 0, -8871.56, 672.73, 97.99, 5.20, 300, 300, 0, 0),    -- Stormwind, the Gilded Rose
(2594452, 94450, 0, -4844.65, -857.40, 502.00, 4.87, 300, 300, 0, 0),    -- Ironforge, the Stonefire Tavern
(2594453, 94450, 1, 10127.49, 2226.00, 1328.81, 2.22, 300, 300, 0, 0),    -- Darnassus
(2594454, 94450, 1, 1632.73, -4437.51, 15.52, 2.76, 300, 300, 0, 0),    -- Orgrimmar, Valley of Strength
(2594455, 94450, 1, -1301.22, 36.80, 129.29, 0.56, 300, 300, 0, 0),    -- Thunder Bluff
(2594456, 94450, 0, 1633.99, 225.24, -43.02, 2.84, 300, 300, 0, 0),    -- Undercity
(2594457, 94450, 0, -9464.46, 18.18, 57.05, 3.04, 300, 300, 0, 0),    -- Goldshire, the Lion's Pride Inn
(2594458, 94450, 1, -409.71, -2643.31, 96.31, 3.44, 300, 300, 0, 0),    -- the Crossroads
(2594459, 94450, 0, -14461.19, 496.69, 15.21, 3.98, 300, 300, 0, 0),    -- Booty Bay, the Salty Sailor Tavern
(2594460, 94450, 1, -7159.10, -3840.87, 8.85, 1.95, 300, 300, 0, 0);    -- Gadgetzan
