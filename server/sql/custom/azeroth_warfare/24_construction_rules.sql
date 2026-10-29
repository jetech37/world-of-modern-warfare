-- World of Modern Warfare: salvaged building materials (server/src/fusion/include/fusion/Construction.h,
-- SalvageTable; docs in server/src/fusion/docs/CONSTRUCTION.md). World DB. Apply after
-- 03_construction.sql (its lumber and parts are copied here). Safe to apply again: the range is
-- cleared first and nothing else is touched.
--
-- What comes back out of a built piece (taken down, packed off a released or lapsed plot, or
-- broken by someone) comes back as "Salvaged <material>": its own item that builds pieces and
-- pays for reinforcement as the fresh one does, but never teaches Construction. Skill-ups want
-- fresh materials, so taking a piece down and putting it up again can't level the skill. The
-- server gives these and pays with them (Construction::AsSalvaged, PlanPayment); each row here is
-- its fresh item's row (look, stack, quality) under a new entry and name. Salvaged goods are of no
-- use to the other trades, and sell to no vendor: they are building currency only, so no way a
-- piece pays out (a free build, a plot owner's guns) is a way to make money (security review
-- 2026-10, #14 and #23).
--
-- The entries are fixed for good (an item in someone's bags keeps its entry); the pairs must match
-- Construction::SalvageTable() (a server test checks the table).
--
-- Reserved ranges (kept free):
--   item_template.entry          93600-93649

DELETE FROM `item_template` WHERE `entry` BETWEEN 93600 AND 93649;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`,
  `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `item_level`, `required_level`, `max_count`, `stackable`,
  `bonding`, `material`)
SELECT m.`salvaged`, t.`patch`, t.`class`, t.`subclass`, CONCAT('Salvaged ', t.`name`),
  'Taken back out of a built piece. Builds pieces and pays for reinforcement like the fresh goods, but teaches no Construction.',
  t.`display_id`, t.`quality`, 0, 1, 0, 0, 0, t.`item_level`, 0, 0, t.`stackable`, 0, t.`material`
FROM `item_template` t
JOIN (
  -- Lumber and the construction parts (03_construction.sql).
  SELECT 91001 AS `fresh`, 93600 AS `salvaged` UNION ALL   -- Rough Lumber
  SELECT 91002, 93601 UNION ALL                            -- Sturdy Lumber
  SELECT 91003, 93602 UNION ALL                            -- Hardwood Plank
  SELECT 91004, 93603 UNION ALL                            -- Ironwood Plank
  SELECT 91020, 93604 UNION ALL                            -- Copper Nails
  SELECT 91021, 93605 UNION ALL                            -- Bronze Hinge
  SELECT 91022, 93606 UNION ALL                            -- Iron Bracket
  SELECT 91023, 93607 UNION ALL                            -- Reinforced Plate
  SELECT 91024, 93608 UNION ALL                            -- Fortified Plate
  -- Vanilla's stone, bars, cloth and leather.
  SELECT 2835, 93610 UNION ALL                             -- Rough Stone
  SELECT 2836, 93611 UNION ALL                             -- Coarse Stone
  SELECT 2838, 93612 UNION ALL                             -- Heavy Stone
  SELECT 7912, 93613 UNION ALL                             -- Solid Stone
  SELECT 12365, 93614 UNION ALL                            -- Dense Stone
  SELECT 2840, 93615 UNION ALL                             -- Copper Bar
  SELECT 2841, 93616 UNION ALL                             -- Bronze Bar
  SELECT 3575, 93617 UNION ALL                             -- Iron Bar
  SELECT 3860, 93618 UNION ALL                             -- Mithril Bar
  SELECT 12359, 93619 UNION ALL                            -- Thorium Bar
  SELECT 2589, 93620 UNION ALL                             -- Linen Cloth
  SELECT 2592, 93621 UNION ALL                             -- Wool Cloth
  SELECT 4305, 93622 UNION ALL                             -- Bolt of Silk Cloth
  SELECT 4339, 93623 UNION ALL                             -- Bolt of Mageweave
  SELECT 2318, 93624 UNION ALL                             -- Light Leather
  -- The portal's arcane goods (Portals.h).
  SELECT 12363, 93625 UNION ALL                            -- Arcane Crystal
  SELECT 14344, 93626                                      -- Large Brilliant Shard
) m ON m.`fresh` = t.`entry`;

-- The fresh goods say what they are good for (03_construction.sql writes them first; this sets
-- the whole line, so applying again changes nothing).
UPDATE `item_template` SET `description` = 'Cut from softwood. Builders use it; building with fresh lumber can teach Construction.' WHERE `entry` = 91001;
UPDATE `item_template` SET `description` = 'Cut from sturdy timber. Builders use it; building with fresh lumber can teach Construction.' WHERE `entry` = 91002;
UPDATE `item_template` SET `description` = 'Cut from hardwood. Builders use it; building with fresh lumber can teach Construction.' WHERE `entry` = 91003;
UPDATE `item_template` SET `description` = 'Cut from ironwood. Builders use it; building with fresh lumber can teach Construction.' WHERE `entry` = 91004;
