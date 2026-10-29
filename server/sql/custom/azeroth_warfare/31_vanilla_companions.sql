-- World of Modern Warfare: every vanilla 1.12 non-combat pet is a battle companion (design/
-- COMPANION_CATALOG.md section 1d). World DB; apply after 02, 08 and 29 (02 rebuilds
-- fusion_companion_species, 08 clears species 100-255 and items 91100-91999). Safe to run again:
-- the species and items are deleted and written afresh, the look rows set the same values, and the
-- source UPDATEs only ever turn a retired pet item into its companion item (a second run finds none).
--
--  - Species 165-214, items 91165-91214: the pets with no companion line yet. Each one's stage 1 is
--    the vanilla pet's creature; its look (the UPDATEs below) is the young of the line's last form
--    (catalog section 1f: the Bombay's line is the Shadowmaw Kitten's), or the pet's own display where
--    the last form was coloured to match it; where a bigger form makes sense the line
--    has a second stage (owl, spider, turtles, dragons, murlocs, shark, bear, boar, fire, saber).
--  - The pets that already were a line's first form (Dark/Crimson/Emerald Whelpling, Sprite
--    Darter, Worg Pup, Murky, Disgusting Oozeling, Silver Tabby, Snowshoe Rabbit, Mechanical
--    Squirrel, Hawk Owl) are replaced by that species' own item.
--  - Every source of a vanilla pet item (vendors, creature and object loot, containers, quest
--    rewards, the spells that make one) now gives its companion item: the old items are retired.
--    Copies already in characters' bags, banks, mail and the auction house are swapped by the
--    server at startup (Companions::VanillaPets, FusionMgr::LoadCompanions).
-- Mirrors Companions::StarterSpecies() and Companions::VanillaPets() (CompanionTests reads this file).

-- ── Species 165-214 ──────────────────────────────────────────────────────────────────────────
DELETE FROM `fusion_companion_species` WHERE `id` BETWEEN 165 AND 214;
INSERT INTO `fusion_companion_species` (`id`, `name`, `item_entry`, `family`, `role`,
  `stage1_creature`, `stage1_scale`, `stage2_creature`, `stage2_scale`, `stage3_creature`, `stage3_scale`,
  `evo1_level`, `evo1_item`, `evo1_item_count`, `evo1_areas`, `evo1_condition`, `evo1_condition_type`, `evo1_condition_count`,
  `evo2_level`, `evo2_item`, `evo2_item_count`, `evo2_areas`, `evo2_condition`, `evo2_condition_type`, `evo2_condition_count`,
  `drop_entries`, `drop_type`, `drop_family`, `ride_stage`, `ride_spell`, `ride_swift_spell`, `ride_display`) VALUES
(165, 'Shadowmaw Kitten', 91165, 0, 0, 7385, 1, 7385, 1.45, 7385, 1.15, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7385', 0, 0, 0, 0, 0, 0),
(166, 'Ridge Stalker Kitten', 91166, 0, 0, 7384, 1, 7384, 1.45, 7384, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7384', 0, 0, 0, 0, 0, 0),
(167, 'Jungle Tiger Kitten', 91167, 0, 0, 7382, 1, 7382, 1.45, 7382, 1.15, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7382', 0, 0, 0, 0, 0, 0),
(168, 'Winterspring Kitten', 91168, 0, 0, 7386, 1, 7386, 1.45, 7386, 0.85, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7386', 0, 0, 0, 0, 0, 0),
(169, 'Ice Tiger Kitten', 91169, 0, 0, 7380, 1, 7380, 1.45, 7380, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7380', 0, 0, 0, 0, 0, 0),
(170, 'Nightstalker Kitten', 91170, 0, 0, 7383, 1, 7383, 1.45, 7383, 1.15, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7383', 0, 0, 0, 0, 0, 0),
(171, 'Corrupted Kitten', 91171, 0, 0, 9936, 1, 9936, 1.45, 9936, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '9936', 0, 0, 0, 0, 0, 0),
(172, 'Green Wing Macaw', 91172, 0, 0, 7387, 1, 7387, 0.8, 7387, 0.6, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7387', 0, 0, 0, 0, 0, 0),
(173, 'Hyacinth Macaw', 91173, 0, 0, 7391, 1, 7391, 0.8, 7391, 0.7, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7391', 0, 0, 0, 0, 0, 0),
(174, 'Senegal Parrot', 91174, 0, 0, 7389, 1, 7389, 0.8, 7389, 0.6, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7389', 0, 0, 0, 0, 0, 0),
(175, 'Cockatiel', 91175, 0, 0, 7390, 1, 7390, 0.8, 7390, 0.6, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7390', 0, 0, 0, 0, 0, 0),
(176, 'Snowy Owlet', 91176, 10, 0, 7553, 1, 7456, 0.6, 7456, 0.8, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7553', 0, 0, 0, 0, 0, 0),
(177, 'Olive Serpent Hatchling', 91177, 0, 0, 7565, 1, 7565, 1.85, 7565, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7565', 0, 0, 0, 0, 0, 0),
(178, 'Brown Snake', 91178, 0, 0, 7562, 1, 7562, 1.85, 7562, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7562', 0, 0, 0, 0, 0, 0),
(179, 'Crimson Snake', 91179, 0, 0, 7567, 1, 7567, 1.85, 7567, 1.4, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7567', 0, 0, 0, 0, 0, 0),
(180, 'Cockroach', 91180, 0, 0, 7395, 0.6, 7395, 1.4, 7395, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7395', 0, 0, 0, 0, 0, 0),
(181, 'Prairie Dog', 91181, 0, 0, 14421, 1, 14421, 1.1, 14421, 0.9, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14421', 0, 0, 0, 0, 0, 0),
(182, 'Mechanostrider Hatchling', 91182, 21, 0, 8376, 1, 8376, 0.65, 8376, 1, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '8376', 0, 0, 0, 0, 0, 0),
(183, 'Ivory Tallstrider Chick', 91183, 0, 0, 7394, 1, 7394, 2.3, 7394, 1.25, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7394', 0, 0, 0, 0, 0, 0),
(184, 'Gray Tallstrider Chick', 91184, 0, 0, 7392, 1, 7392, 2.3, 7392, 1.25, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7392', 0, 0, 0, 0, 0, 0),
(185, 'Tree Frog', 91185, 0, 0, 7549, 0.7, 7549, 1.4, 7549, 2.5, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7549', 0, 0, 0, 0, 0, 0),
(186, 'Wood Frog', 91186, 0, 0, 7550, 0.7, 7550, 1.4, 7550, 2.5, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '7550', 0, 0, 0, 0, 0, 0),
(187, 'Pet Bombling', 91187, 21, 0, 9656, 1, 9656, 1, 9656, 0.75, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '9656', 0, 0, 0, 0, 0, 0),
(188, 'Lil'' Smoky', 91188, 21, 0, 9657, 0.8, 9657, 0.6, 9657, 1.2, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '9657', 0, 0, 0, 0, 0, 0),
(189, 'Lifelike Toad', 91189, 21, 0, 12419, 0.7, 12419, 1.4, 12419, 2.5, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '12419', 0, 0, 0, 0, 0, 0),
(190, 'Tranquil Mechanical Yeti', 91190, 21, 0, 15699, 1, 15699, 0.5, 15699, 1, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '15699', 0, 0, 0, 0, 0, 0),
(191, 'Smolderweb Hatchling', 91191, 6, 0, 10598, 0.9, 5857, 0.5, 5857, 1, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '10598', 0, 0, 0, 0, 0, 0),
(192, 'Zergling', 91192, 0, 0, 11327, 1, 11327, 1.2, 11327, 0.65, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '11327', 0, 0, 0, 0, 0, 0),
(193, 'Panda Cub', 91193, 0, 0, 11325, 1, 11325, 1.6, 11325, 1.1, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '11325', 0, 0, 0, 0, 0, 0),
(194, 'Mini Diablo', 91194, 0, 0, 11326, 1, 11326, 1, 11326, 0.8, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '11326', 0, 0, 0, 0, 0, 0),
(195, 'Albino Snapjaw', 91195, 30, 2, 14633, 1, 2408, 0.8, 2408, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14633', 0, 0, 0, 0, 0, 0),
(196, 'Loggerhead Snapjaw', 91196, 30, 2, 14629, 1, 2408, 0.8, 2408, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14629', 0, 0, 0, 0, 0, 0),
(197, 'Hawksbill Snapjaw', 91197, 30, 2, 14632, 1, 2408, 0.8, 2408, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14632', 0, 0, 0, 0, 0, 0),
(198, 'Leatherback Snapjaw', 91198, 30, 2, 14630, 1, 2408, 0.8, 2408, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14630', 0, 0, 0, 0, 0, 0),
(199, 'Olive Snapjaw', 91199, 30, 2, 14631, 1, 2408, 0.6, 2408, 0.8, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14631', 0, 0, 0, 0, 0, 0),
(200, 'Tiny Red Dragon', 91200, 1, 1, 14756, 0.75, 1069, 0.3, 13976, 0.2, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14756', 0, 0, 0, 0, 0, 0),
(201, 'Tiny Green Dragon', 91201, 1, 1, 14755, 0.75, 741, 0.3, 742, 0.2, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14755', 0, 0, 0, 0, 0, 0),
(202, 'Jubling', 91202, 0, 0, 14878, 0.65, 14878, 1.65, 14878, 1.15, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '14878', 0, 0, 0, 0, 0, 0),
(203, 'Murki', 91203, 18, 0, 15361, 1, 735, 1.25, 735, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '15361', 0, 0, 0, 0, 0, 0),
(204, 'Gurky', 91204, 18, 0, 16069, 1, 735, 1.25, 735, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '16069', 0, 0, 0, 0, 0, 0),
(205, 'Terky', 91205, 18, 0, 16445, 1, 735, 1.25, 735, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '16445', 0, 0, 0, 0, 0, 0),
(206, 'Baby Shark', 91206, 34, 2, 15661, 1, 2187, 0.7, 2187, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '15661', 0, 0, 0, 0, 0, 0),
(207, 'Peddlefeet', 91207, 0, 0, 16085, 1, 16085, 1, 16085, 1.65, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '16085', 0, 0, 0, 0, 0, 0),
(208, 'Poley', 91208, 5, 2, 16456, 1, 1196, 0.9, 1196, 1.3, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '16456', 0, 0, 0, 0, 0, 0),
(209, 'Speedy', 91209, 30, 2, 16547, 1, 2408, 1, 2408, 1.55, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '16547', 0, 0, 0, 0, 0, 0),
(210, 'Mr. Wiggles', 91210, 28, 2, 16548, 1, 157, 1.2, 157, 1, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '16548', 0, 0, 0, 0, 0, 0),
(211, 'Whiskers the Rat', 91211, 0, 0, 16549, 0.75, 16549, 1.6, 16549, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '16549', 0, 0, 0, 0, 0, 0),
(212, 'Spirit of Summer', 91212, 14, 1, 16701, 0.3, 6296, 0.8, 6296, 2.25, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '16701', 0, 0, 0, 0, 0, 0),
(213, 'White Tiger Cub', 91213, 4, 0, 17254, 1, 7431, 0.8, 7431, 1, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '17254', 0, 0, 0, 0, 0, 0),
(214, 'Hippogryph Hatchling', 91214, 0, 0, 17255, 1, 17255, 0.55, 17255, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '', 0, 0, 0, '17255', 0, 0, 0, 0, 0, 0);

-- The look table (Companions.cpp's kLooks): stage 1 wears the pet's own display.
UPDATE `fusion_companion_species` SET `stage1_display` = 6943, `stage1_scale` = 0.75, `stage2_display` = 16942, `stage2_scale` = 1.45, `stage3_display` = 2437, `stage3_scale` = 1.15 WHERE `id` = 165;
UPDATE `fusion_companion_species` SET `stage1_display` = 2278, `stage1_scale` = 0.9, `stage2_display` = 16942, `stage2_scale` = 1.45, `stage3_display` = 1056, `stage3_scale` = 0.95 WHERE `id` = 166;
UPDATE `fusion_companion_species` SET `stage1_display` = 918, `stage1_scale` = 0.45, `stage2_display` = 16942, `stage2_scale` = 1.45, `stage3_display` = 1008, `stage3_scale` = 1.15 WHERE `id` = 167;
UPDATE `fusion_companion_species` SET `stage1_display` = 6248, `stage1_scale` = 0.75, `stage2_display` = 16942, `stage2_scale` = 1.45, `stage3_display` = 11446, `stage3_scale` = 0.85 WHERE `id` = 168;
UPDATE `fusion_companion_species` SET `stage1_display` = 9951, `stage1_scale` = 0.37, `stage2_display` = 16942, `stage2_scale` = 1.45, `stage3_display` = 9949, `stage3_scale` = 0.95 WHERE `id` = 169;
UPDATE `fusion_companion_species` SET `stage1_display` = 321, `stage1_scale` = 0.4, `stage2_display` = 16942, `stage2_scale` = 1.45, `stage3_display` = 11449, `stage3_scale` = 1.15 WHERE `id` = 170;
UPDATE `fusion_companion_species` SET `stage1_display` = 9993, `stage1_scale` = 0.75, `stage2_display` = 16942, `stage2_scale` = 1.45, `stage3_display` = 9230, `stage3_scale` = 0.95 WHERE `id` = 171;
UPDATE `fusion_companion_species` SET `stage1_display` = 5866, `stage1_scale` = 1, `stage2_display` = 8818, `stage2_scale` = 0.8, `stage3_display` = 5865, `stage3_scale` = 0.6 WHERE `id` = 172;
UPDATE `fusion_companion_species` SET `stage1_display` = 6192, `stage1_scale` = 1, `stage2_display` = 8819, `stage2_scale` = 0.8, `stage3_display` = 8816, `stage3_scale` = 1.4 WHERE `id` = 173;
UPDATE `fusion_companion_species` SET `stage1_display` = 6190, `stage1_scale` = 1, `stage2_display` = 8816, `stage2_scale` = 0.8, `stage3_display` = 8819, `stage3_scale` = 1.4 WHERE `id` = 174;
UPDATE `fusion_companion_species` SET `stage1_display` = 6191, `stage1_scale` = 1, `stage2_display` = 8817, `stage2_scale` = 0.8, `stage3_display` = 8818, `stage3_scale` = 1.4 WHERE `id` = 175;
UPDATE `fusion_companion_species` SET `stage1_display` = 15534, `stage1_scale` = 0.45, `stage2_display` = 4877, `stage2_scale` = 0.6, `stage3_display` = 10833, `stage3_scale` = 0.8 WHERE `id` = 176;
UPDATE `fusion_companion_species` SET `stage1_display` = 2995, `stage1_scale` = 0.32, `stage2_display` = 2955, `stage2_scale` = 1.85, `stage3_display` = 14557, `stage3_scale` = 1.05 WHERE `id` = 177;
UPDATE `fusion_companion_species` SET `stage1_display` = 2957, `stage1_scale` = 1, `stage2_display` = 2958, `stage2_scale` = 1.85, `stage3_display` = 15182, `stage3_scale` = 1.25 WHERE `id` = 178;
UPDATE `fusion_companion_species` SET `stage1_display` = 6303, `stage1_scale` = 1, `stage2_display` = 2956, `stage2_scale` = 1.85, `stage3_display` = 4312, `stage3_scale` = 1.4 WHERE `id` = 179;
UPDATE `fusion_companion_species` SET `stage1_display` = 2177, `stage1_scale` = 0.6, `stage2_display` = 7470, `stage2_scale` = 1.4, `stage3_display` = 157, `stage3_scale` = 0.95 WHERE `id` = 180;
UPDATE `fusion_companion_species` SET `stage1_display` = 1072, `stage1_scale` = 1, `stage2_display` = 2838, `stage2_scale` = 1.1, `stage3_display` = 4468, `stage3_scale` = 0.9 WHERE `id` = 181;
UPDATE `fusion_companion_species` SET `stage1_display` = 6569, `stage1_scale` = 0.35, `stage2_display` = 6978, `stage2_scale` = 0.65, `stage3_display` = 10666, `stage3_scale` = 1 WHERE `id` = 182;
UPDATE `fusion_companion_species` SET `stage1_display` = 1284, `stage1_scale` = 0.35, `stage2_display` = 2741, `stage2_scale` = 0.55, `stage3_display` = 1221, `stage3_scale` = 1.25 WHERE `id` = 183;
UPDATE `fusion_companion_species` SET `stage1_display` = 2741, `stage1_scale` = 0.35, `stage2_display` = 1219, `stage2_scale` = 0.85, `stage3_display` = 1220, `stage3_scale` = 1.25 WHERE `id` = 184;
UPDATE `fusion_companion_species` SET `stage1_display` = 14932, `stage1_scale` = 0.47, `stage2_display` = 6297, `stage2_scale` = 1.4, `stage3_display` = 1924, `stage3_scale` = 2.5 WHERE `id` = 185;
UPDATE `fusion_companion_species` SET `stage1_display` = 901, `stage1_scale` = 0.7, `stage2_display` = 6296, `stage2_scale` = 1.4, `stage3_display` = 5379, `stage3_scale` = 1.65 WHERE `id` = 186;
UPDATE `fusion_companion_species` SET `stage1_display` = 8909, `stage1_scale` = 1, `stage2_display` = 6977, `stage2_scale` = 1, `stage3_display` = 6890, `stage3_scale` = 0.75 WHERE `id` = 187;
UPDATE `fusion_companion_species` SET `stage1_display` = 8910, `stage1_scale` = 0.8, `stage2_display` = 14379, `stage2_scale` = 0.6, `stage3_display` = 1303, `stage3_scale` = 1.2 WHERE `id` = 188;
UPDATE `fusion_companion_species` SET `stage1_display` = 901, `stage1_scale` = 0.7, `stage2_display` = 6297, `stage2_scale` = 1.4, `stage3_display` = 5379, `stage3_scale` = 1.65 WHERE `id` = 189;
UPDATE `fusion_companion_species` SET `stage1_display` = 10269, `stage1_scale` = 1, `stage2_display` = 3207, `stage2_scale` = 0.5, `stage3_display` = 927, `stage3_scale` = 1.2 WHERE `id` = 190;
UPDATE `fusion_companion_species` SET `stage1_display` = 4458, `stage1_scale` = 0.21, `stage2_display` = 963, `stage2_scale` = 0.5, `stage3_display` = 7510, `stage3_scale` = 1 WHERE `id` = 191;
UPDATE `fusion_companion_species` SET `stage1_display` = 10993, `stage1_scale` = 1, `stage2_display` = 9469, `stage2_scale` = 1.2, `stage3_display` = 11091, `stage3_scale` = 0.65 WHERE `id` = 192;
UPDATE `fusion_companion_species` SET `stage1_display` = 10990, `stage1_scale` = 1, `stage2_display` = 16189, `stage2_scale` = 1.6, `stage3_display` = 913, `stage3_scale` = 1.1 WHERE `id` = 193;
UPDATE `fusion_companion_species` SET `stage1_display` = 10992, `stage1_scale` = 1, `stage2_display` = 5049, `stage2_scale` = 1, `stage3_display` = 4426, `stage3_scale` = 0.8 WHERE `id` = 194;
UPDATE `fusion_companion_species` SET `stage1_display` = 14661, `stage1_scale` = 1, `stage2_display` = 1244, `stage2_scale` = 0.8, `stage3_display` = 10947, `stage3_scale` = 0.95 WHERE `id` = 195;
UPDATE `fusion_companion_species` SET `stage1_display` = 14657, `stage1_scale` = 1, `stage2_display` = 1244, `stage2_scale` = 0.8, `stage3_display` = 7114, `stage3_scale` = 1.1 WHERE `id` = 196;
UPDATE `fusion_companion_species` SET `stage1_display` = 14660, `stage1_scale` = 1, `stage2_display` = 1244, `stage2_scale` = 0.8, `stage3_display` = 5027, `stage3_scale` = 0.95 WHERE `id` = 197;
UPDATE `fusion_companion_species` SET `stage1_display` = 14658, `stage1_scale` = 1, `stage2_display` = 1244, `stage2_scale` = 0.8, `stage3_display` = 7839, `stage3_scale` = 0.95 WHERE `id` = 198;
UPDATE `fusion_companion_species` SET `stage1_display` = 14659, `stage1_scale` = 1, `stage2_display` = 7837, `stage2_scale` = 0.6, `stage3_display` = 2902, `stage3_scale` = 0.95 WHERE `id` = 199;
UPDATE `fusion_companion_species` SET `stage1_display` = 14779, `stage1_scale` = 0.75, `stage2_display` = 6376, `stage2_scale` = 0.3, `stage3_display` = 2718, `stage3_scale` = 0.2 WHERE `id` = 200;
UPDATE `fusion_companion_species` SET `stage1_display` = 14778, `stage1_scale` = 0.75, `stage2_display` = 5145, `stage2_scale` = 0.3, `stage3_display` = 2717, `stage3_scale` = 0.2 WHERE `id` = 201;
UPDATE `fusion_companion_species` SET `stage1_display` = 14938, `stage1_scale` = 0.65, `stage2_display` = 6295, `stage2_scale` = 1.65, `stage3_display` = 14934, `stage3_scale` = 1.15 WHERE `id` = 202;
UPDATE `fusion_companion_species` SET `stage1_display` = 15395, `stage1_scale` = 1, `stage2_display` = 527, `stage2_scale` = 1.25, `stage3_display` = 5025, `stage3_scale` = 0.9 WHERE `id` = 203;
UPDATE `fusion_companion_species` SET `stage1_display` = 15393, `stage1_scale` = 1, `stage2_display` = 527, `stage2_scale` = 1.25, `stage3_display` = 1079, `stage3_scale` = 1.05 WHERE `id` = 204;
UPDATE `fusion_companion_species` SET `stage1_display` = 15398, `stage1_scale` = 1, `stage2_display` = 527, `stage2_scale` = 1.25, `stage3_display` = 11293, `stage3_scale` = 0.9 WHERE `id` = 205;
UPDATE `fusion_companion_species` SET `stage1_display` = 15595, `stage1_scale` = 1, `stage2_display` = 12193, `stage2_scale` = 0.7, `stage3_display` = 2851, `stage3_scale` = 1.05 WHERE `id` = 206;
UPDATE `fusion_companion_species` SET `stage1_display` = 15992, `stage1_scale` = 1, `stage2_display` = 15989, `stage2_scale` = 1, `stage3_display` = 15990, `stage3_scale` = 1.65 WHERE `id` = 207;
UPDATE `fusion_companion_species` SET `stage1_display` = 16189, `stage1_scale` = 1, `stage2_display` = 8840, `stage2_scale` = 0.9, `stage3_display` = 913, `stage3_scale` = 1.3 WHERE `id` = 208;
UPDATE `fusion_companion_species` SET `stage1_display` = 16259, `stage1_scale` = 1, `stage2_display` = 16360, `stage2_scale` = 1, `stage3_display` = 17158, `stage3_scale` = 1.55 WHERE `id` = 209;
UPDATE `fusion_companion_species` SET `stage1_display` = 16257, `stage1_scale` = 1, `stage2_display` = 16357, `stage2_scale` = 1.2, `stage3_display` = 499, `stage3_scale` = 0.9 WHERE `id` = 210;
UPDATE `fusion_companion_species` SET `stage1_display` = 4959, `stage1_scale` = 0.95, `stage2_display` = 1141, `stage2_scale` = 1.6, `stage3_display` = 16143, `stage3_scale` = 1.05 WHERE `id` = 211;
UPDATE `fusion_companion_species` SET `stage1_display` = 6537, `stage1_scale` = 1, `stage2_display` = 1070, `stage2_scale` = 0.8, `stage3_display` = 1405, `stage3_scale` = 2.25 WHERE `id` = 212;
UPDATE `fusion_companion_species` SET `stage1_display` = 16942, `stage1_scale` = 1, `stage2_display` = 9953, `stage2_scale` = 0.8, `stage3_display` = 6080, `stage3_scale` = 1 WHERE `id` = 213;
UPDATE `fusion_companion_species` SET `stage1_display` = 16943, `stage1_scale` = 1, `stage2_display` = 479, `stage2_scale` = 0.55, `stage3_display` = 10890, `stage3_scale` = 1.05 WHERE `id` = 214;

-- ── Items 91165-91214 ────────────────────────────────────────────────────────────────────────
-- As the other companion items (class 15, Bind on Equip so always tradeable, one per stack, no
-- spell: the fusion hook summons it), with the vanilla pet item's name, icon and vendor price.
DELETE FROM `item_template` WHERE `entry` BETWEEN 91165 AND 91214;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`,
  `quality`, `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `stackable`, `max_count`, `item_level`, `bonding`) VALUES
(91165, 0, 15, 0, 'Cat Carrier (Shadowmaw)', 'Battle companion: Shadowmaw Kitten. Its level and experience travel with this item.', 20629, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91166, 0, 15, 0, 'Cat Carrier (Ridge Stalker)', 'Battle companion: Ridge Stalker Kitten. Its level and experience travel with this item.', 20629, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91167, 0, 15, 0, 'Cat Carrier (Jungle Tiger)', 'Battle companion: Jungle Tiger Kitten. Its level and experience travel with this item.', 20629, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91168, 0, 15, 0, 'Cat Carrier (Winterspring)', 'Battle companion: Winterspring Kitten. Its level and experience travel with this item.', 20629, 1, 1, 6000, 1500, 0, 1, 0, 20, 2),
(91169, 0, 15, 0, 'Cat Carrier (Ice Tiger)', 'Battle companion: Ice Tiger Kitten. Its level and experience travel with this item.', 20629, 1, 1, 6000, 1500, 0, 1, 0, 20, 2),
(91170, 0, 15, 0, 'Cat Carrier (Nightstalker)', 'Battle companion: Nightstalker Kitten. Its level and experience travel with this item.', 20629, 1, 1, 6000, 1500, 0, 1, 0, 20, 2),
(91171, 0, 15, 0, 'Cat Carrier (Corrupted Kitten)', 'Battle companion: Corrupted Kitten. Its level and experience travel with this item.', 20629, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91172, 0, 15, 0, 'Parrot Cage (Green Wing Macaw)', 'Battle companion: Green Wing Macaw. Its level and experience travel with this item.', 17292, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91173, 0, 15, 0, 'Parrot Cage (Hyacinth Macaw)', 'Battle companion: Hyacinth Macaw. Its level and experience travel with this item.', 17292, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91174, 0, 15, 0, 'Parrot Cage (Senegal)', 'Battle companion: Senegal Parrot. Its level and experience travel with this item.', 17292, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91175, 0, 15, 0, 'Parrot Cage (Cockatiel)', 'Battle companion: Cockatiel. Its level and experience travel with this item.', 17292, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91176, 0, 15, 0, 'Snowy Owl Egg', 'Battle companion: Snowy Owlet. Grows into a bigger form. Its level and experience travel with this item.', 19091, 1, 1, 5000, 1250, 0, 1, 0, 20, 2),
(91177, 0, 15, 0, 'Olive Serpent Egg', 'Battle companion: Olive Serpent Hatchling. Its level and experience travel with this item.', 19089, 1, 1, 5000, 1250, 0, 1, 0, 20, 2),
(91178, 0, 15, 0, 'Brown Snake', 'Battle companion: Brown Snake. Its level and experience travel with this item.', 19089, 1, 1, 5000, 1250, 0, 1, 0, 20, 2),
(91179, 0, 15, 0, 'Crimson Snake', 'Battle companion: Crimson Snake. Its level and experience travel with this item.', 19089, 1, 1, 5000, 1250, 0, 1, 0, 20, 2),
(91180, 0, 15, 0, 'Cockroach', 'Battle companion: Cockroach. Its level and experience travel with this item.', 19092, 1, 1, 5000, 1250, 0, 1, 0, 20, 2),
(91181, 0, 15, 0, 'Prairie Dog Whistle', 'Battle companion: Prairie Dog. Its level and experience travel with this item.', 15798, 1, 1, 5000, 1250, 0, 1, 0, 20, 2),
(91182, 0, 15, 0, 'Mechanostrider Hatchling', 'Battle companion: Mechanostrider Hatchling. Its level and experience travel with this item.', 19115, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91183, 0, 15, 0, 'Ivory Tallstrider Egg', 'Battle companion: Ivory Tallstrider Chick. Its level and experience travel with this item.', 17284, 1, 1, 10000, 2500, 0, 1, 0, 20, 2),
(91184, 0, 15, 0, 'Gray Tallstrider Egg', 'Battle companion: Gray Tallstrider Chick. Its level and experience travel with this item.', 18047, 1, 1, 10, 2, 0, 1, 0, 20, 2),
(91185, 0, 15, 0, 'Tree Frog Box', 'Battle companion: Tree Frog. Its level and experience travel with this item.', 17284, 1, 1, 10000, 2500, 0, 1, 0, 20, 2),
(91186, 0, 15, 0, 'Wood Frog Box', 'Battle companion: Wood Frog. Its level and experience travel with this item.', 17284, 1, 1, 10000, 2500, 0, 1, 0, 20, 2),
(91187, 0, 15, 0, 'Pet Bombling', 'Battle companion: Pet Bombling. Its level and experience travel with this item.', 21833, 1, 1, 10000, 2500, 0, 1, 0, 20, 2),
(91188, 0, 15, 0, 'Lil'' Smoky', 'Battle companion: Lil'' Smoky. Its level and experience travel with this item.', 9730, 1, 1, 10000, 2500, 0, 1, 0, 20, 2),
(91189, 0, 15, 0, 'Lifelike Mechanical Toad', 'Battle companion: Lifelike Toad. Its level and experience travel with this item.', 26612, 1, 1, 10000, 2500, 0, 1, 0, 20, 2),
(91190, 0, 15, 0, 'Tranquil Mechanical Yeti', 'Battle companion: Tranquil Mechanical Yeti. Its level and experience travel with this item.', 34050, 1, 1, 5000, 1250, 0, 1, 0, 20, 2),
(91191, 0, 15, 0, 'Smolderweb Carrier', 'Battle companion: Smolderweb Hatchling. Grows into a bigger form. Its level and experience travel with this item.', 22717, 1, 1, 6000, 1500, 0, 1, 0, 20, 2),
(91192, 0, 15, 0, 'Zergling Leash', 'Battle companion: Zergling. Its level and experience travel with this item.', 24252, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91193, 0, 15, 0, 'Panda Collar', 'Battle companion: Panda Cub. Its level and experience travel with this item.', 24251, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91194, 0, 15, 0, 'Diablo Stone', 'Battle companion: Mini Diablo. Its level and experience travel with this item.', 34364, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91195, 0, 15, 0, 'Turtle Egg (Albino)', 'Battle companion: Albino Snapjaw. Grows into a bigger form. Its level and experience travel with this item.', 18051, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91196, 0, 15, 0, 'Turtle Egg (Loggerhead)', 'Battle companion: Loggerhead Snapjaw. Grows into a bigger form. Its level and experience travel with this item.', 18048, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91197, 0, 15, 0, 'Turtle Egg (Hawksbill)', 'Battle companion: Hawksbill Snapjaw. Grows into a bigger form. Its level and experience travel with this item.', 18051, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91198, 0, 15, 0, 'Turtle Egg (Leatherback)', 'Battle companion: Leatherback Snapjaw. Grows into a bigger form. Its level and experience travel with this item.', 18051, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91199, 0, 15, 0, 'Turtle Egg (Olive)', 'Battle companion: Olive Snapjaw. Grows into a bigger form. Its level and experience travel with this item.', 18048, 1, 1, 4000, 1000, 0, 1, 0, 20, 2),
(91200, 0, 15, 0, 'Red Dragon Orb', 'Battle companion: Tiny Red Dragon. Grows into a bigger form. Its level and experience travel with this item.', 27454, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91201, 0, 15, 0, 'Green Dragon Orb', 'Battle companion: Tiny Green Dragon. Grows into a bigger form. Its level and experience travel with this item.', 6564, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91202, 0, 15, 0, 'A Jubling''s Tiny Home', 'Battle companion: Jubling. Its level and experience travel with this item.', 18173, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91203, 0, 15, 0, 'Orange Murloc Egg', 'Battle companion: Murki. Grows into a bigger form. Its level and experience travel with this item.', 18050, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91204, 0, 15, 0, 'Pink Murloc Egg', 'Battle companion: Gurky. Grows into a bigger form. Its level and experience travel with this item.', 18050, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91205, 0, 15, 0, 'White Murloc Egg', 'Battle companion: Terky. Grows into a bigger form. Its level and experience travel with this item.', 18050, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91206, 0, 15, 0, 'Baby Shark', 'Battle companion: Baby Shark. Grows into a bigger form. Its level and experience travel with this item.', 33467, 1, 1, 6000, 1500, 0, 1, 0, 20, 2),
(91207, 0, 15, 0, 'Truesilver Shafted Arrow', 'Battle companion: Peddlefeet. Its level and experience travel with this item.', 34535, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91208, 0, 15, 0, 'Polar Bear Collar', 'Battle companion: Poley. Grows into a bigger form. Its level and experience travel with this item.', 35224, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91209, 0, 15, 0, 'Turtle Box', 'Battle companion: Speedy. Grows into a bigger form. Its level and experience travel with this item.', 35406, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91210, 0, 15, 0, 'Piglet''s Collar', 'Battle companion: Mr. Wiggles. Grows into a bigger form. Its level and experience travel with this item.', 35405, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91211, 0, 15, 0, 'Rat Cage', 'Battle companion: Whiskers the Rat. Its level and experience travel with this item.', 35407, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91212, 0, 15, 0, 'Captured Flame', 'Battle companion: Spirit of Summer. Grows into a bigger form. Its level and experience travel with this item.', 15788, 1, 1, 0, 0, 0, 1, 0, 20, 2),
(91213, 0, 15, 0, 'White Tiger Cub', 'Battle companion: White Tiger Cub. Grows into a bigger form. Its level and experience travel with this item.', 36470, 1, 1, 5000, 1250, 0, 1, 0, 20, 2),
(91214, 0, 15, 0, 'Hippogryph Hatchling', 'Battle companion: Hippogryph Hatchling. Its level and experience travel with this item.', 18048, 1, 1, 5000, 1250, 0, 1, 0, 20, 2);

-- ── Retiring the vanilla pet items: every source gives the companion item instead ─────────────
-- (old item -> companion item). Rows are matched by the old entry, so a second run changes nothing.
-- 4401 Mechanical Squirrel Box -> 90028 (species 28)
UPDATE IGNORE `npc_vendor` SET `item` = 90028 WHERE `item` = 4401;
UPDATE IGNORE `creature_loot_template` SET `item` = 90028 WHERE `item` = 4401;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90028 WHERE `item` = 4401;
UPDATE IGNORE `item_loot_template` SET `item` = 90028 WHERE `item` = 4401;
UPDATE IGNORE `reference_loot_template` SET `item` = 90028 WHERE `item` = 4401;
UPDATE IGNORE `mail_loot_template` SET `item` = 90028 WHERE `item` = 4401;
UPDATE `quest_template` SET `RewItemId1` = 90028 WHERE `RewItemId1` = 4401;
UPDATE `quest_template` SET `RewItemId2` = 90028 WHERE `RewItemId2` = 4401;
UPDATE `quest_template` SET `RewItemId3` = 90028 WHERE `RewItemId3` = 4401;
UPDATE `quest_template` SET `RewItemId4` = 90028 WHERE `RewItemId4` = 4401;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90028 WHERE `RewChoiceItemId1` = 4401;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90028 WHERE `RewChoiceItemId2` = 4401;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90028 WHERE `RewChoiceItemId3` = 4401;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90028 WHERE `RewChoiceItemId4` = 4401;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90028 WHERE `RewChoiceItemId5` = 4401;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90028 WHERE `RewChoiceItemId6` = 4401;
UPDATE `quest_template` SET `SrcItemId` = 90028 WHERE `SrcItemId` = 4401;
UPDATE `spell_template` SET `effectItemType1` = 90028 WHERE `effectItemType1` = 4401;
UPDATE `spell_template` SET `effectItemType2` = 90028 WHERE `effectItemType2` = 4401;
UPDATE `spell_template` SET `effectItemType3` = 90028 WHERE `effectItemType3` = 4401;
-- 8485 Cat Carrier (Bombay) -> 91165 (species 165)
UPDATE IGNORE `npc_vendor` SET `item` = 91165 WHERE `item` = 8485;
UPDATE IGNORE `creature_loot_template` SET `item` = 91165 WHERE `item` = 8485;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91165 WHERE `item` = 8485;
UPDATE IGNORE `item_loot_template` SET `item` = 91165 WHERE `item` = 8485;
UPDATE IGNORE `reference_loot_template` SET `item` = 91165 WHERE `item` = 8485;
UPDATE IGNORE `mail_loot_template` SET `item` = 91165 WHERE `item` = 8485;
UPDATE `quest_template` SET `RewItemId1` = 91165 WHERE `RewItemId1` = 8485;
UPDATE `quest_template` SET `RewItemId2` = 91165 WHERE `RewItemId2` = 8485;
UPDATE `quest_template` SET `RewItemId3` = 91165 WHERE `RewItemId3` = 8485;
UPDATE `quest_template` SET `RewItemId4` = 91165 WHERE `RewItemId4` = 8485;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91165 WHERE `RewChoiceItemId1` = 8485;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91165 WHERE `RewChoiceItemId2` = 8485;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91165 WHERE `RewChoiceItemId3` = 8485;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91165 WHERE `RewChoiceItemId4` = 8485;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91165 WHERE `RewChoiceItemId5` = 8485;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91165 WHERE `RewChoiceItemId6` = 8485;
UPDATE `quest_template` SET `SrcItemId` = 91165 WHERE `SrcItemId` = 8485;
UPDATE `spell_template` SET `effectItemType1` = 91165 WHERE `effectItemType1` = 8485;
UPDATE `spell_template` SET `effectItemType2` = 91165 WHERE `effectItemType2` = 8485;
UPDATE `spell_template` SET `effectItemType3` = 91165 WHERE `effectItemType3` = 8485;
-- 8486 Cat Carrier (Cornish Rex) -> 91166 (species 166)
UPDATE IGNORE `npc_vendor` SET `item` = 91166 WHERE `item` = 8486;
UPDATE IGNORE `creature_loot_template` SET `item` = 91166 WHERE `item` = 8486;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91166 WHERE `item` = 8486;
UPDATE IGNORE `item_loot_template` SET `item` = 91166 WHERE `item` = 8486;
UPDATE IGNORE `reference_loot_template` SET `item` = 91166 WHERE `item` = 8486;
UPDATE IGNORE `mail_loot_template` SET `item` = 91166 WHERE `item` = 8486;
UPDATE `quest_template` SET `RewItemId1` = 91166 WHERE `RewItemId1` = 8486;
UPDATE `quest_template` SET `RewItemId2` = 91166 WHERE `RewItemId2` = 8486;
UPDATE `quest_template` SET `RewItemId3` = 91166 WHERE `RewItemId3` = 8486;
UPDATE `quest_template` SET `RewItemId4` = 91166 WHERE `RewItemId4` = 8486;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91166 WHERE `RewChoiceItemId1` = 8486;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91166 WHERE `RewChoiceItemId2` = 8486;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91166 WHERE `RewChoiceItemId3` = 8486;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91166 WHERE `RewChoiceItemId4` = 8486;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91166 WHERE `RewChoiceItemId5` = 8486;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91166 WHERE `RewChoiceItemId6` = 8486;
UPDATE `quest_template` SET `SrcItemId` = 91166 WHERE `SrcItemId` = 8486;
UPDATE `spell_template` SET `effectItemType1` = 91166 WHERE `effectItemType1` = 8486;
UPDATE `spell_template` SET `effectItemType2` = 91166 WHERE `effectItemType2` = 8486;
UPDATE `spell_template` SET `effectItemType3` = 91166 WHERE `effectItemType3` = 8486;
-- 8487 Cat Carrier (Orange Tabby) -> 91167 (species 167)
UPDATE IGNORE `npc_vendor` SET `item` = 91167 WHERE `item` = 8487;
UPDATE IGNORE `creature_loot_template` SET `item` = 91167 WHERE `item` = 8487;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91167 WHERE `item` = 8487;
UPDATE IGNORE `item_loot_template` SET `item` = 91167 WHERE `item` = 8487;
UPDATE IGNORE `reference_loot_template` SET `item` = 91167 WHERE `item` = 8487;
UPDATE IGNORE `mail_loot_template` SET `item` = 91167 WHERE `item` = 8487;
UPDATE `quest_template` SET `RewItemId1` = 91167 WHERE `RewItemId1` = 8487;
UPDATE `quest_template` SET `RewItemId2` = 91167 WHERE `RewItemId2` = 8487;
UPDATE `quest_template` SET `RewItemId3` = 91167 WHERE `RewItemId3` = 8487;
UPDATE `quest_template` SET `RewItemId4` = 91167 WHERE `RewItemId4` = 8487;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91167 WHERE `RewChoiceItemId1` = 8487;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91167 WHERE `RewChoiceItemId2` = 8487;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91167 WHERE `RewChoiceItemId3` = 8487;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91167 WHERE `RewChoiceItemId4` = 8487;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91167 WHERE `RewChoiceItemId5` = 8487;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91167 WHERE `RewChoiceItemId6` = 8487;
UPDATE `quest_template` SET `SrcItemId` = 91167 WHERE `SrcItemId` = 8487;
UPDATE `spell_template` SET `effectItemType1` = 91167 WHERE `effectItemType1` = 8487;
UPDATE `spell_template` SET `effectItemType2` = 91167 WHERE `effectItemType2` = 8487;
UPDATE `spell_template` SET `effectItemType3` = 91167 WHERE `effectItemType3` = 8487;
-- 8488 Cat Carrier (Silver Tabby) -> 90026 (species 26)
UPDATE IGNORE `npc_vendor` SET `item` = 90026 WHERE `item` = 8488;
UPDATE IGNORE `creature_loot_template` SET `item` = 90026 WHERE `item` = 8488;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90026 WHERE `item` = 8488;
UPDATE IGNORE `item_loot_template` SET `item` = 90026 WHERE `item` = 8488;
UPDATE IGNORE `reference_loot_template` SET `item` = 90026 WHERE `item` = 8488;
UPDATE IGNORE `mail_loot_template` SET `item` = 90026 WHERE `item` = 8488;
UPDATE `quest_template` SET `RewItemId1` = 90026 WHERE `RewItemId1` = 8488;
UPDATE `quest_template` SET `RewItemId2` = 90026 WHERE `RewItemId2` = 8488;
UPDATE `quest_template` SET `RewItemId3` = 90026 WHERE `RewItemId3` = 8488;
UPDATE `quest_template` SET `RewItemId4` = 90026 WHERE `RewItemId4` = 8488;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90026 WHERE `RewChoiceItemId1` = 8488;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90026 WHERE `RewChoiceItemId2` = 8488;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90026 WHERE `RewChoiceItemId3` = 8488;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90026 WHERE `RewChoiceItemId4` = 8488;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90026 WHERE `RewChoiceItemId5` = 8488;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90026 WHERE `RewChoiceItemId6` = 8488;
UPDATE `quest_template` SET `SrcItemId` = 90026 WHERE `SrcItemId` = 8488;
UPDATE `spell_template` SET `effectItemType1` = 90026 WHERE `effectItemType1` = 8488;
UPDATE `spell_template` SET `effectItemType2` = 90026 WHERE `effectItemType2` = 8488;
UPDATE `spell_template` SET `effectItemType3` = 90026 WHERE `effectItemType3` = 8488;
-- 8489 Cat Carrier (White Kitten) -> 91168 (species 168)
UPDATE IGNORE `npc_vendor` SET `item` = 91168 WHERE `item` = 8489;
UPDATE IGNORE `creature_loot_template` SET `item` = 91168 WHERE `item` = 8489;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91168 WHERE `item` = 8489;
UPDATE IGNORE `item_loot_template` SET `item` = 91168 WHERE `item` = 8489;
UPDATE IGNORE `reference_loot_template` SET `item` = 91168 WHERE `item` = 8489;
UPDATE IGNORE `mail_loot_template` SET `item` = 91168 WHERE `item` = 8489;
UPDATE `quest_template` SET `RewItemId1` = 91168 WHERE `RewItemId1` = 8489;
UPDATE `quest_template` SET `RewItemId2` = 91168 WHERE `RewItemId2` = 8489;
UPDATE `quest_template` SET `RewItemId3` = 91168 WHERE `RewItemId3` = 8489;
UPDATE `quest_template` SET `RewItemId4` = 91168 WHERE `RewItemId4` = 8489;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91168 WHERE `RewChoiceItemId1` = 8489;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91168 WHERE `RewChoiceItemId2` = 8489;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91168 WHERE `RewChoiceItemId3` = 8489;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91168 WHERE `RewChoiceItemId4` = 8489;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91168 WHERE `RewChoiceItemId5` = 8489;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91168 WHERE `RewChoiceItemId6` = 8489;
UPDATE `quest_template` SET `SrcItemId` = 91168 WHERE `SrcItemId` = 8489;
UPDATE `spell_template` SET `effectItemType1` = 91168 WHERE `effectItemType1` = 8489;
UPDATE `spell_template` SET `effectItemType2` = 91168 WHERE `effectItemType2` = 8489;
UPDATE `spell_template` SET `effectItemType3` = 91168 WHERE `effectItemType3` = 8489;
-- 8490 Cat Carrier (Siamese) -> 91169 (species 169)
UPDATE IGNORE `npc_vendor` SET `item` = 91169 WHERE `item` = 8490;
UPDATE IGNORE `creature_loot_template` SET `item` = 91169 WHERE `item` = 8490;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91169 WHERE `item` = 8490;
UPDATE IGNORE `item_loot_template` SET `item` = 91169 WHERE `item` = 8490;
UPDATE IGNORE `reference_loot_template` SET `item` = 91169 WHERE `item` = 8490;
UPDATE IGNORE `mail_loot_template` SET `item` = 91169 WHERE `item` = 8490;
UPDATE `quest_template` SET `RewItemId1` = 91169 WHERE `RewItemId1` = 8490;
UPDATE `quest_template` SET `RewItemId2` = 91169 WHERE `RewItemId2` = 8490;
UPDATE `quest_template` SET `RewItemId3` = 91169 WHERE `RewItemId3` = 8490;
UPDATE `quest_template` SET `RewItemId4` = 91169 WHERE `RewItemId4` = 8490;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91169 WHERE `RewChoiceItemId1` = 8490;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91169 WHERE `RewChoiceItemId2` = 8490;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91169 WHERE `RewChoiceItemId3` = 8490;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91169 WHERE `RewChoiceItemId4` = 8490;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91169 WHERE `RewChoiceItemId5` = 8490;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91169 WHERE `RewChoiceItemId6` = 8490;
UPDATE `quest_template` SET `SrcItemId` = 91169 WHERE `SrcItemId` = 8490;
UPDATE `spell_template` SET `effectItemType1` = 91169 WHERE `effectItemType1` = 8490;
UPDATE `spell_template` SET `effectItemType2` = 91169 WHERE `effectItemType2` = 8490;
UPDATE `spell_template` SET `effectItemType3` = 91169 WHERE `effectItemType3` = 8490;
-- 8491 Cat Carrier (Black Tabby) -> 91170 (species 170)
UPDATE IGNORE `npc_vendor` SET `item` = 91170 WHERE `item` = 8491;
UPDATE IGNORE `creature_loot_template` SET `item` = 91170 WHERE `item` = 8491;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91170 WHERE `item` = 8491;
UPDATE IGNORE `item_loot_template` SET `item` = 91170 WHERE `item` = 8491;
UPDATE IGNORE `reference_loot_template` SET `item` = 91170 WHERE `item` = 8491;
UPDATE IGNORE `mail_loot_template` SET `item` = 91170 WHERE `item` = 8491;
UPDATE `quest_template` SET `RewItemId1` = 91170 WHERE `RewItemId1` = 8491;
UPDATE `quest_template` SET `RewItemId2` = 91170 WHERE `RewItemId2` = 8491;
UPDATE `quest_template` SET `RewItemId3` = 91170 WHERE `RewItemId3` = 8491;
UPDATE `quest_template` SET `RewItemId4` = 91170 WHERE `RewItemId4` = 8491;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91170 WHERE `RewChoiceItemId1` = 8491;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91170 WHERE `RewChoiceItemId2` = 8491;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91170 WHERE `RewChoiceItemId3` = 8491;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91170 WHERE `RewChoiceItemId4` = 8491;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91170 WHERE `RewChoiceItemId5` = 8491;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91170 WHERE `RewChoiceItemId6` = 8491;
UPDATE `quest_template` SET `SrcItemId` = 91170 WHERE `SrcItemId` = 8491;
UPDATE `spell_template` SET `effectItemType1` = 91170 WHERE `effectItemType1` = 8491;
UPDATE `spell_template` SET `effectItemType2` = 91170 WHERE `effectItemType2` = 8491;
UPDATE `spell_template` SET `effectItemType3` = 91170 WHERE `effectItemType3` = 8491;
-- 8492 Parrot Cage (Green Wing Macaw) -> 91172 (species 172)
UPDATE IGNORE `npc_vendor` SET `item` = 91172 WHERE `item` = 8492;
UPDATE IGNORE `creature_loot_template` SET `item` = 91172 WHERE `item` = 8492;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91172 WHERE `item` = 8492;
UPDATE IGNORE `item_loot_template` SET `item` = 91172 WHERE `item` = 8492;
UPDATE IGNORE `reference_loot_template` SET `item` = 91172 WHERE `item` = 8492;
UPDATE IGNORE `mail_loot_template` SET `item` = 91172 WHERE `item` = 8492;
UPDATE `quest_template` SET `RewItemId1` = 91172 WHERE `RewItemId1` = 8492;
UPDATE `quest_template` SET `RewItemId2` = 91172 WHERE `RewItemId2` = 8492;
UPDATE `quest_template` SET `RewItemId3` = 91172 WHERE `RewItemId3` = 8492;
UPDATE `quest_template` SET `RewItemId4` = 91172 WHERE `RewItemId4` = 8492;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91172 WHERE `RewChoiceItemId1` = 8492;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91172 WHERE `RewChoiceItemId2` = 8492;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91172 WHERE `RewChoiceItemId3` = 8492;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91172 WHERE `RewChoiceItemId4` = 8492;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91172 WHERE `RewChoiceItemId5` = 8492;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91172 WHERE `RewChoiceItemId6` = 8492;
UPDATE `quest_template` SET `SrcItemId` = 91172 WHERE `SrcItemId` = 8492;
UPDATE `spell_template` SET `effectItemType1` = 91172 WHERE `effectItemType1` = 8492;
UPDATE `spell_template` SET `effectItemType2` = 91172 WHERE `effectItemType2` = 8492;
UPDATE `spell_template` SET `effectItemType3` = 91172 WHERE `effectItemType3` = 8492;
-- 8494 Parrot Cage (Hyacinth Macaw) -> 91173 (species 173)
UPDATE IGNORE `npc_vendor` SET `item` = 91173 WHERE `item` = 8494;
UPDATE IGNORE `creature_loot_template` SET `item` = 91173 WHERE `item` = 8494;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91173 WHERE `item` = 8494;
UPDATE IGNORE `item_loot_template` SET `item` = 91173 WHERE `item` = 8494;
UPDATE IGNORE `reference_loot_template` SET `item` = 91173 WHERE `item` = 8494;
UPDATE IGNORE `mail_loot_template` SET `item` = 91173 WHERE `item` = 8494;
UPDATE `quest_template` SET `RewItemId1` = 91173 WHERE `RewItemId1` = 8494;
UPDATE `quest_template` SET `RewItemId2` = 91173 WHERE `RewItemId2` = 8494;
UPDATE `quest_template` SET `RewItemId3` = 91173 WHERE `RewItemId3` = 8494;
UPDATE `quest_template` SET `RewItemId4` = 91173 WHERE `RewItemId4` = 8494;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91173 WHERE `RewChoiceItemId1` = 8494;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91173 WHERE `RewChoiceItemId2` = 8494;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91173 WHERE `RewChoiceItemId3` = 8494;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91173 WHERE `RewChoiceItemId4` = 8494;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91173 WHERE `RewChoiceItemId5` = 8494;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91173 WHERE `RewChoiceItemId6` = 8494;
UPDATE `quest_template` SET `SrcItemId` = 91173 WHERE `SrcItemId` = 8494;
UPDATE `spell_template` SET `effectItemType1` = 91173 WHERE `effectItemType1` = 8494;
UPDATE `spell_template` SET `effectItemType2` = 91173 WHERE `effectItemType2` = 8494;
UPDATE `spell_template` SET `effectItemType3` = 91173 WHERE `effectItemType3` = 8494;
-- 8495 Parrot Cage (Senegal) -> 91174 (species 174)
UPDATE IGNORE `npc_vendor` SET `item` = 91174 WHERE `item` = 8495;
UPDATE IGNORE `creature_loot_template` SET `item` = 91174 WHERE `item` = 8495;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91174 WHERE `item` = 8495;
UPDATE IGNORE `item_loot_template` SET `item` = 91174 WHERE `item` = 8495;
UPDATE IGNORE `reference_loot_template` SET `item` = 91174 WHERE `item` = 8495;
UPDATE IGNORE `mail_loot_template` SET `item` = 91174 WHERE `item` = 8495;
UPDATE `quest_template` SET `RewItemId1` = 91174 WHERE `RewItemId1` = 8495;
UPDATE `quest_template` SET `RewItemId2` = 91174 WHERE `RewItemId2` = 8495;
UPDATE `quest_template` SET `RewItemId3` = 91174 WHERE `RewItemId3` = 8495;
UPDATE `quest_template` SET `RewItemId4` = 91174 WHERE `RewItemId4` = 8495;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91174 WHERE `RewChoiceItemId1` = 8495;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91174 WHERE `RewChoiceItemId2` = 8495;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91174 WHERE `RewChoiceItemId3` = 8495;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91174 WHERE `RewChoiceItemId4` = 8495;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91174 WHERE `RewChoiceItemId5` = 8495;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91174 WHERE `RewChoiceItemId6` = 8495;
UPDATE `quest_template` SET `SrcItemId` = 91174 WHERE `SrcItemId` = 8495;
UPDATE `spell_template` SET `effectItemType1` = 91174 WHERE `effectItemType1` = 8495;
UPDATE `spell_template` SET `effectItemType2` = 91174 WHERE `effectItemType2` = 8495;
UPDATE `spell_template` SET `effectItemType3` = 91174 WHERE `effectItemType3` = 8495;
-- 8496 Parrot Cage (Cockatiel) -> 91175 (species 175)
UPDATE IGNORE `npc_vendor` SET `item` = 91175 WHERE `item` = 8496;
UPDATE IGNORE `creature_loot_template` SET `item` = 91175 WHERE `item` = 8496;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91175 WHERE `item` = 8496;
UPDATE IGNORE `item_loot_template` SET `item` = 91175 WHERE `item` = 8496;
UPDATE IGNORE `reference_loot_template` SET `item` = 91175 WHERE `item` = 8496;
UPDATE IGNORE `mail_loot_template` SET `item` = 91175 WHERE `item` = 8496;
UPDATE `quest_template` SET `RewItemId1` = 91175 WHERE `RewItemId1` = 8496;
UPDATE `quest_template` SET `RewItemId2` = 91175 WHERE `RewItemId2` = 8496;
UPDATE `quest_template` SET `RewItemId3` = 91175 WHERE `RewItemId3` = 8496;
UPDATE `quest_template` SET `RewItemId4` = 91175 WHERE `RewItemId4` = 8496;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91175 WHERE `RewChoiceItemId1` = 8496;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91175 WHERE `RewChoiceItemId2` = 8496;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91175 WHERE `RewChoiceItemId3` = 8496;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91175 WHERE `RewChoiceItemId4` = 8496;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91175 WHERE `RewChoiceItemId5` = 8496;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91175 WHERE `RewChoiceItemId6` = 8496;
UPDATE `quest_template` SET `SrcItemId` = 91175 WHERE `SrcItemId` = 8496;
UPDATE `spell_template` SET `effectItemType1` = 91175 WHERE `effectItemType1` = 8496;
UPDATE `spell_template` SET `effectItemType2` = 91175 WHERE `effectItemType2` = 8496;
UPDATE `spell_template` SET `effectItemType3` = 91175 WHERE `effectItemType3` = 8496;
-- 8497 Rabbit Crate (Snowshoe) -> 90027 (species 27)
UPDATE IGNORE `npc_vendor` SET `item` = 90027 WHERE `item` = 8497;
UPDATE IGNORE `creature_loot_template` SET `item` = 90027 WHERE `item` = 8497;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90027 WHERE `item` = 8497;
UPDATE IGNORE `item_loot_template` SET `item` = 90027 WHERE `item` = 8497;
UPDATE IGNORE `reference_loot_template` SET `item` = 90027 WHERE `item` = 8497;
UPDATE IGNORE `mail_loot_template` SET `item` = 90027 WHERE `item` = 8497;
UPDATE `quest_template` SET `RewItemId1` = 90027 WHERE `RewItemId1` = 8497;
UPDATE `quest_template` SET `RewItemId2` = 90027 WHERE `RewItemId2` = 8497;
UPDATE `quest_template` SET `RewItemId3` = 90027 WHERE `RewItemId3` = 8497;
UPDATE `quest_template` SET `RewItemId4` = 90027 WHERE `RewItemId4` = 8497;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90027 WHERE `RewChoiceItemId1` = 8497;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90027 WHERE `RewChoiceItemId2` = 8497;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90027 WHERE `RewChoiceItemId3` = 8497;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90027 WHERE `RewChoiceItemId4` = 8497;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90027 WHERE `RewChoiceItemId5` = 8497;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90027 WHERE `RewChoiceItemId6` = 8497;
UPDATE `quest_template` SET `SrcItemId` = 90027 WHERE `SrcItemId` = 8497;
UPDATE `spell_template` SET `effectItemType1` = 90027 WHERE `effectItemType1` = 8497;
UPDATE `spell_template` SET `effectItemType2` = 90027 WHERE `effectItemType2` = 8497;
UPDATE `spell_template` SET `effectItemType3` = 90027 WHERE `effectItemType3` = 8497;
-- 8498 Tiny Emerald Whelpling -> 90003 (species 3)
UPDATE IGNORE `npc_vendor` SET `item` = 90003 WHERE `item` = 8498;
UPDATE IGNORE `creature_loot_template` SET `item` = 90003 WHERE `item` = 8498;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90003 WHERE `item` = 8498;
UPDATE IGNORE `item_loot_template` SET `item` = 90003 WHERE `item` = 8498;
UPDATE IGNORE `reference_loot_template` SET `item` = 90003 WHERE `item` = 8498;
UPDATE IGNORE `mail_loot_template` SET `item` = 90003 WHERE `item` = 8498;
UPDATE `quest_template` SET `RewItemId1` = 90003 WHERE `RewItemId1` = 8498;
UPDATE `quest_template` SET `RewItemId2` = 90003 WHERE `RewItemId2` = 8498;
UPDATE `quest_template` SET `RewItemId3` = 90003 WHERE `RewItemId3` = 8498;
UPDATE `quest_template` SET `RewItemId4` = 90003 WHERE `RewItemId4` = 8498;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90003 WHERE `RewChoiceItemId1` = 8498;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90003 WHERE `RewChoiceItemId2` = 8498;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90003 WHERE `RewChoiceItemId3` = 8498;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90003 WHERE `RewChoiceItemId4` = 8498;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90003 WHERE `RewChoiceItemId5` = 8498;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90003 WHERE `RewChoiceItemId6` = 8498;
UPDATE `quest_template` SET `SrcItemId` = 90003 WHERE `SrcItemId` = 8498;
UPDATE `spell_template` SET `effectItemType1` = 90003 WHERE `effectItemType1` = 8498;
UPDATE `spell_template` SET `effectItemType2` = 90003 WHERE `effectItemType2` = 8498;
UPDATE `spell_template` SET `effectItemType3` = 90003 WHERE `effectItemType3` = 8498;
-- 8499 Tiny Crimson Whelpling -> 90002 (species 2)
UPDATE IGNORE `npc_vendor` SET `item` = 90002 WHERE `item` = 8499;
UPDATE IGNORE `creature_loot_template` SET `item` = 90002 WHERE `item` = 8499;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90002 WHERE `item` = 8499;
UPDATE IGNORE `item_loot_template` SET `item` = 90002 WHERE `item` = 8499;
UPDATE IGNORE `reference_loot_template` SET `item` = 90002 WHERE `item` = 8499;
UPDATE IGNORE `mail_loot_template` SET `item` = 90002 WHERE `item` = 8499;
UPDATE `quest_template` SET `RewItemId1` = 90002 WHERE `RewItemId1` = 8499;
UPDATE `quest_template` SET `RewItemId2` = 90002 WHERE `RewItemId2` = 8499;
UPDATE `quest_template` SET `RewItemId3` = 90002 WHERE `RewItemId3` = 8499;
UPDATE `quest_template` SET `RewItemId4` = 90002 WHERE `RewItemId4` = 8499;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90002 WHERE `RewChoiceItemId1` = 8499;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90002 WHERE `RewChoiceItemId2` = 8499;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90002 WHERE `RewChoiceItemId3` = 8499;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90002 WHERE `RewChoiceItemId4` = 8499;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90002 WHERE `RewChoiceItemId5` = 8499;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90002 WHERE `RewChoiceItemId6` = 8499;
UPDATE `quest_template` SET `SrcItemId` = 90002 WHERE `SrcItemId` = 8499;
UPDATE `spell_template` SET `effectItemType1` = 90002 WHERE `effectItemType1` = 8499;
UPDATE `spell_template` SET `effectItemType2` = 90002 WHERE `effectItemType2` = 8499;
UPDATE `spell_template` SET `effectItemType3` = 90002 WHERE `effectItemType3` = 8499;
-- 8500 Great Horned Owl -> 91176 (species 176)
UPDATE IGNORE `npc_vendor` SET `item` = 91176 WHERE `item` = 8500;
UPDATE IGNORE `creature_loot_template` SET `item` = 91176 WHERE `item` = 8500;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91176 WHERE `item` = 8500;
UPDATE IGNORE `item_loot_template` SET `item` = 91176 WHERE `item` = 8500;
UPDATE IGNORE `reference_loot_template` SET `item` = 91176 WHERE `item` = 8500;
UPDATE IGNORE `mail_loot_template` SET `item` = 91176 WHERE `item` = 8500;
UPDATE `quest_template` SET `RewItemId1` = 91176 WHERE `RewItemId1` = 8500;
UPDATE `quest_template` SET `RewItemId2` = 91176 WHERE `RewItemId2` = 8500;
UPDATE `quest_template` SET `RewItemId3` = 91176 WHERE `RewItemId3` = 8500;
UPDATE `quest_template` SET `RewItemId4` = 91176 WHERE `RewItemId4` = 8500;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91176 WHERE `RewChoiceItemId1` = 8500;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91176 WHERE `RewChoiceItemId2` = 8500;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91176 WHERE `RewChoiceItemId3` = 8500;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91176 WHERE `RewChoiceItemId4` = 8500;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91176 WHERE `RewChoiceItemId5` = 8500;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91176 WHERE `RewChoiceItemId6` = 8500;
UPDATE `quest_template` SET `SrcItemId` = 91176 WHERE `SrcItemId` = 8500;
UPDATE `spell_template` SET `effectItemType1` = 91176 WHERE `effectItemType1` = 8500;
UPDATE `spell_template` SET `effectItemType2` = 91176 WHERE `effectItemType2` = 8500;
UPDATE `spell_template` SET `effectItemType3` = 91176 WHERE `effectItemType3` = 8500;
-- 8501 Hawk Owl -> 90015 (species 15)
UPDATE IGNORE `npc_vendor` SET `item` = 90015 WHERE `item` = 8501;
UPDATE IGNORE `creature_loot_template` SET `item` = 90015 WHERE `item` = 8501;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90015 WHERE `item` = 8501;
UPDATE IGNORE `item_loot_template` SET `item` = 90015 WHERE `item` = 8501;
UPDATE IGNORE `reference_loot_template` SET `item` = 90015 WHERE `item` = 8501;
UPDATE IGNORE `mail_loot_template` SET `item` = 90015 WHERE `item` = 8501;
UPDATE `quest_template` SET `RewItemId1` = 90015 WHERE `RewItemId1` = 8501;
UPDATE `quest_template` SET `RewItemId2` = 90015 WHERE `RewItemId2` = 8501;
UPDATE `quest_template` SET `RewItemId3` = 90015 WHERE `RewItemId3` = 8501;
UPDATE `quest_template` SET `RewItemId4` = 90015 WHERE `RewItemId4` = 8501;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90015 WHERE `RewChoiceItemId1` = 8501;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90015 WHERE `RewChoiceItemId2` = 8501;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90015 WHERE `RewChoiceItemId3` = 8501;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90015 WHERE `RewChoiceItemId4` = 8501;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90015 WHERE `RewChoiceItemId5` = 8501;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90015 WHERE `RewChoiceItemId6` = 8501;
UPDATE `quest_template` SET `SrcItemId` = 90015 WHERE `SrcItemId` = 8501;
UPDATE `spell_template` SET `effectItemType1` = 90015 WHERE `effectItemType1` = 8501;
UPDATE `spell_template` SET `effectItemType2` = 90015 WHERE `effectItemType2` = 8501;
UPDATE `spell_template` SET `effectItemType3` = 90015 WHERE `effectItemType3` = 8501;
-- 10360 Black Kingsnake -> 91177 (species 177)
UPDATE IGNORE `npc_vendor` SET `item` = 91177 WHERE `item` = 10360;
UPDATE IGNORE `creature_loot_template` SET `item` = 91177 WHERE `item` = 10360;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91177 WHERE `item` = 10360;
UPDATE IGNORE `item_loot_template` SET `item` = 91177 WHERE `item` = 10360;
UPDATE IGNORE `reference_loot_template` SET `item` = 91177 WHERE `item` = 10360;
UPDATE IGNORE `mail_loot_template` SET `item` = 91177 WHERE `item` = 10360;
UPDATE `quest_template` SET `RewItemId1` = 91177 WHERE `RewItemId1` = 10360;
UPDATE `quest_template` SET `RewItemId2` = 91177 WHERE `RewItemId2` = 10360;
UPDATE `quest_template` SET `RewItemId3` = 91177 WHERE `RewItemId3` = 10360;
UPDATE `quest_template` SET `RewItemId4` = 91177 WHERE `RewItemId4` = 10360;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91177 WHERE `RewChoiceItemId1` = 10360;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91177 WHERE `RewChoiceItemId2` = 10360;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91177 WHERE `RewChoiceItemId3` = 10360;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91177 WHERE `RewChoiceItemId4` = 10360;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91177 WHERE `RewChoiceItemId5` = 10360;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91177 WHERE `RewChoiceItemId6` = 10360;
UPDATE `quest_template` SET `SrcItemId` = 91177 WHERE `SrcItemId` = 10360;
UPDATE `spell_template` SET `effectItemType1` = 91177 WHERE `effectItemType1` = 10360;
UPDATE `spell_template` SET `effectItemType2` = 91177 WHERE `effectItemType2` = 10360;
UPDATE `spell_template` SET `effectItemType3` = 91177 WHERE `effectItemType3` = 10360;
-- 10361 Brown Snake -> 91178 (species 178)
UPDATE IGNORE `npc_vendor` SET `item` = 91178 WHERE `item` = 10361;
UPDATE IGNORE `creature_loot_template` SET `item` = 91178 WHERE `item` = 10361;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91178 WHERE `item` = 10361;
UPDATE IGNORE `item_loot_template` SET `item` = 91178 WHERE `item` = 10361;
UPDATE IGNORE `reference_loot_template` SET `item` = 91178 WHERE `item` = 10361;
UPDATE IGNORE `mail_loot_template` SET `item` = 91178 WHERE `item` = 10361;
UPDATE `quest_template` SET `RewItemId1` = 91178 WHERE `RewItemId1` = 10361;
UPDATE `quest_template` SET `RewItemId2` = 91178 WHERE `RewItemId2` = 10361;
UPDATE `quest_template` SET `RewItemId3` = 91178 WHERE `RewItemId3` = 10361;
UPDATE `quest_template` SET `RewItemId4` = 91178 WHERE `RewItemId4` = 10361;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91178 WHERE `RewChoiceItemId1` = 10361;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91178 WHERE `RewChoiceItemId2` = 10361;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91178 WHERE `RewChoiceItemId3` = 10361;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91178 WHERE `RewChoiceItemId4` = 10361;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91178 WHERE `RewChoiceItemId5` = 10361;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91178 WHERE `RewChoiceItemId6` = 10361;
UPDATE `quest_template` SET `SrcItemId` = 91178 WHERE `SrcItemId` = 10361;
UPDATE `spell_template` SET `effectItemType1` = 91178 WHERE `effectItemType1` = 10361;
UPDATE `spell_template` SET `effectItemType2` = 91178 WHERE `effectItemType2` = 10361;
UPDATE `spell_template` SET `effectItemType3` = 91178 WHERE `effectItemType3` = 10361;
-- 10392 Crimson Snake -> 91179 (species 179)
UPDATE IGNORE `npc_vendor` SET `item` = 91179 WHERE `item` = 10392;
UPDATE IGNORE `creature_loot_template` SET `item` = 91179 WHERE `item` = 10392;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91179 WHERE `item` = 10392;
UPDATE IGNORE `item_loot_template` SET `item` = 91179 WHERE `item` = 10392;
UPDATE IGNORE `reference_loot_template` SET `item` = 91179 WHERE `item` = 10392;
UPDATE IGNORE `mail_loot_template` SET `item` = 91179 WHERE `item` = 10392;
UPDATE `quest_template` SET `RewItemId1` = 91179 WHERE `RewItemId1` = 10392;
UPDATE `quest_template` SET `RewItemId2` = 91179 WHERE `RewItemId2` = 10392;
UPDATE `quest_template` SET `RewItemId3` = 91179 WHERE `RewItemId3` = 10392;
UPDATE `quest_template` SET `RewItemId4` = 91179 WHERE `RewItemId4` = 10392;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91179 WHERE `RewChoiceItemId1` = 10392;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91179 WHERE `RewChoiceItemId2` = 10392;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91179 WHERE `RewChoiceItemId3` = 10392;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91179 WHERE `RewChoiceItemId4` = 10392;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91179 WHERE `RewChoiceItemId5` = 10392;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91179 WHERE `RewChoiceItemId6` = 10392;
UPDATE `quest_template` SET `SrcItemId` = 91179 WHERE `SrcItemId` = 10392;
UPDATE `spell_template` SET `effectItemType1` = 91179 WHERE `effectItemType1` = 10392;
UPDATE `spell_template` SET `effectItemType2` = 91179 WHERE `effectItemType2` = 10392;
UPDATE `spell_template` SET `effectItemType3` = 91179 WHERE `effectItemType3` = 10392;
-- 10393 Cockroach -> 91180 (species 180)
UPDATE IGNORE `npc_vendor` SET `item` = 91180 WHERE `item` = 10393;
UPDATE IGNORE `creature_loot_template` SET `item` = 91180 WHERE `item` = 10393;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91180 WHERE `item` = 10393;
UPDATE IGNORE `item_loot_template` SET `item` = 91180 WHERE `item` = 10393;
UPDATE IGNORE `reference_loot_template` SET `item` = 91180 WHERE `item` = 10393;
UPDATE IGNORE `mail_loot_template` SET `item` = 91180 WHERE `item` = 10393;
UPDATE `quest_template` SET `RewItemId1` = 91180 WHERE `RewItemId1` = 10393;
UPDATE `quest_template` SET `RewItemId2` = 91180 WHERE `RewItemId2` = 10393;
UPDATE `quest_template` SET `RewItemId3` = 91180 WHERE `RewItemId3` = 10393;
UPDATE `quest_template` SET `RewItemId4` = 91180 WHERE `RewItemId4` = 10393;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91180 WHERE `RewChoiceItemId1` = 10393;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91180 WHERE `RewChoiceItemId2` = 10393;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91180 WHERE `RewChoiceItemId3` = 10393;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91180 WHERE `RewChoiceItemId4` = 10393;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91180 WHERE `RewChoiceItemId5` = 10393;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91180 WHERE `RewChoiceItemId6` = 10393;
UPDATE `quest_template` SET `SrcItemId` = 91180 WHERE `SrcItemId` = 10393;
UPDATE `spell_template` SET `effectItemType1` = 91180 WHERE `effectItemType1` = 10393;
UPDATE `spell_template` SET `effectItemType2` = 91180 WHERE `effectItemType2` = 10393;
UPDATE `spell_template` SET `effectItemType3` = 91180 WHERE `effectItemType3` = 10393;
-- 10394 Prairie Dog Whistle -> 91181 (species 181)
UPDATE IGNORE `npc_vendor` SET `item` = 91181 WHERE `item` = 10394;
UPDATE IGNORE `creature_loot_template` SET `item` = 91181 WHERE `item` = 10394;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91181 WHERE `item` = 10394;
UPDATE IGNORE `item_loot_template` SET `item` = 91181 WHERE `item` = 10394;
UPDATE IGNORE `reference_loot_template` SET `item` = 91181 WHERE `item` = 10394;
UPDATE IGNORE `mail_loot_template` SET `item` = 91181 WHERE `item` = 10394;
UPDATE `quest_template` SET `RewItemId1` = 91181 WHERE `RewItemId1` = 10394;
UPDATE `quest_template` SET `RewItemId2` = 91181 WHERE `RewItemId2` = 10394;
UPDATE `quest_template` SET `RewItemId3` = 91181 WHERE `RewItemId3` = 10394;
UPDATE `quest_template` SET `RewItemId4` = 91181 WHERE `RewItemId4` = 10394;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91181 WHERE `RewChoiceItemId1` = 10394;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91181 WHERE `RewChoiceItemId2` = 10394;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91181 WHERE `RewChoiceItemId3` = 10394;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91181 WHERE `RewChoiceItemId4` = 10394;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91181 WHERE `RewChoiceItemId5` = 10394;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91181 WHERE `RewChoiceItemId6` = 10394;
UPDATE `quest_template` SET `SrcItemId` = 91181 WHERE `SrcItemId` = 10394;
UPDATE `spell_template` SET `effectItemType1` = 91181 WHERE `effectItemType1` = 10394;
UPDATE `spell_template` SET `effectItemType2` = 91181 WHERE `effectItemType2` = 10394;
UPDATE `spell_template` SET `effectItemType3` = 91181 WHERE `effectItemType3` = 10394;
-- 10398 Mechanical Chicken -> 91182 (species 182)
UPDATE IGNORE `npc_vendor` SET `item` = 91182 WHERE `item` = 10398;
UPDATE IGNORE `creature_loot_template` SET `item` = 91182 WHERE `item` = 10398;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91182 WHERE `item` = 10398;
UPDATE IGNORE `item_loot_template` SET `item` = 91182 WHERE `item` = 10398;
UPDATE IGNORE `reference_loot_template` SET `item` = 91182 WHERE `item` = 10398;
UPDATE IGNORE `mail_loot_template` SET `item` = 91182 WHERE `item` = 10398;
UPDATE `quest_template` SET `RewItemId1` = 91182 WHERE `RewItemId1` = 10398;
UPDATE `quest_template` SET `RewItemId2` = 91182 WHERE `RewItemId2` = 10398;
UPDATE `quest_template` SET `RewItemId3` = 91182 WHERE `RewItemId3` = 10398;
UPDATE `quest_template` SET `RewItemId4` = 91182 WHERE `RewItemId4` = 10398;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91182 WHERE `RewChoiceItemId1` = 10398;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91182 WHERE `RewChoiceItemId2` = 10398;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91182 WHERE `RewChoiceItemId3` = 10398;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91182 WHERE `RewChoiceItemId4` = 10398;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91182 WHERE `RewChoiceItemId5` = 10398;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91182 WHERE `RewChoiceItemId6` = 10398;
UPDATE `quest_template` SET `SrcItemId` = 91182 WHERE `SrcItemId` = 10398;
UPDATE `spell_template` SET `effectItemType1` = 91182 WHERE `effectItemType1` = 10398;
UPDATE `spell_template` SET `effectItemType2` = 91182 WHERE `effectItemType2` = 10398;
UPDATE `spell_template` SET `effectItemType3` = 91182 WHERE `effectItemType3` = 10398;
-- 10822 Dark Whelpling -> 90001 (species 1)
UPDATE IGNORE `npc_vendor` SET `item` = 90001 WHERE `item` = 10822;
UPDATE IGNORE `creature_loot_template` SET `item` = 90001 WHERE `item` = 10822;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90001 WHERE `item` = 10822;
UPDATE IGNORE `item_loot_template` SET `item` = 90001 WHERE `item` = 10822;
UPDATE IGNORE `reference_loot_template` SET `item` = 90001 WHERE `item` = 10822;
UPDATE IGNORE `mail_loot_template` SET `item` = 90001 WHERE `item` = 10822;
UPDATE `quest_template` SET `RewItemId1` = 90001 WHERE `RewItemId1` = 10822;
UPDATE `quest_template` SET `RewItemId2` = 90001 WHERE `RewItemId2` = 10822;
UPDATE `quest_template` SET `RewItemId3` = 90001 WHERE `RewItemId3` = 10822;
UPDATE `quest_template` SET `RewItemId4` = 90001 WHERE `RewItemId4` = 10822;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90001 WHERE `RewChoiceItemId1` = 10822;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90001 WHERE `RewChoiceItemId2` = 10822;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90001 WHERE `RewChoiceItemId3` = 10822;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90001 WHERE `RewChoiceItemId4` = 10822;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90001 WHERE `RewChoiceItemId5` = 10822;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90001 WHERE `RewChoiceItemId6` = 10822;
UPDATE `quest_template` SET `SrcItemId` = 90001 WHERE `SrcItemId` = 10822;
UPDATE `spell_template` SET `effectItemType1` = 90001 WHERE `effectItemType1` = 10822;
UPDATE `spell_template` SET `effectItemType2` = 90001 WHERE `effectItemType2` = 10822;
UPDATE `spell_template` SET `effectItemType3` = 90001 WHERE `effectItemType3` = 10822;
-- 11023 Ancona Chicken -> 91183 (species 183)
UPDATE IGNORE `npc_vendor` SET `item` = 91183 WHERE `item` = 11023;
UPDATE IGNORE `creature_loot_template` SET `item` = 91183 WHERE `item` = 11023;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91183 WHERE `item` = 11023;
UPDATE IGNORE `item_loot_template` SET `item` = 91183 WHERE `item` = 11023;
UPDATE IGNORE `reference_loot_template` SET `item` = 91183 WHERE `item` = 11023;
UPDATE IGNORE `mail_loot_template` SET `item` = 91183 WHERE `item` = 11023;
UPDATE `quest_template` SET `RewItemId1` = 91183 WHERE `RewItemId1` = 11023;
UPDATE `quest_template` SET `RewItemId2` = 91183 WHERE `RewItemId2` = 11023;
UPDATE `quest_template` SET `RewItemId3` = 91183 WHERE `RewItemId3` = 11023;
UPDATE `quest_template` SET `RewItemId4` = 91183 WHERE `RewItemId4` = 11023;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91183 WHERE `RewChoiceItemId1` = 11023;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91183 WHERE `RewChoiceItemId2` = 11023;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91183 WHERE `RewChoiceItemId3` = 11023;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91183 WHERE `RewChoiceItemId4` = 11023;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91183 WHERE `RewChoiceItemId5` = 11023;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91183 WHERE `RewChoiceItemId6` = 11023;
UPDATE `quest_template` SET `SrcItemId` = 91183 WHERE `SrcItemId` = 11023;
UPDATE `spell_template` SET `effectItemType1` = 91183 WHERE `effectItemType1` = 11023;
UPDATE `spell_template` SET `effectItemType2` = 91183 WHERE `effectItemType2` = 11023;
UPDATE `spell_template` SET `effectItemType3` = 91183 WHERE `effectItemType3` = 11023;
-- 11026 Tree Frog Box -> 91185 (species 185)
UPDATE IGNORE `npc_vendor` SET `item` = 91185 WHERE `item` = 11026;
UPDATE IGNORE `creature_loot_template` SET `item` = 91185 WHERE `item` = 11026;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91185 WHERE `item` = 11026;
UPDATE IGNORE `item_loot_template` SET `item` = 91185 WHERE `item` = 11026;
UPDATE IGNORE `reference_loot_template` SET `item` = 91185 WHERE `item` = 11026;
UPDATE IGNORE `mail_loot_template` SET `item` = 91185 WHERE `item` = 11026;
UPDATE `quest_template` SET `RewItemId1` = 91185 WHERE `RewItemId1` = 11026;
UPDATE `quest_template` SET `RewItemId2` = 91185 WHERE `RewItemId2` = 11026;
UPDATE `quest_template` SET `RewItemId3` = 91185 WHERE `RewItemId3` = 11026;
UPDATE `quest_template` SET `RewItemId4` = 91185 WHERE `RewItemId4` = 11026;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91185 WHERE `RewChoiceItemId1` = 11026;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91185 WHERE `RewChoiceItemId2` = 11026;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91185 WHERE `RewChoiceItemId3` = 11026;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91185 WHERE `RewChoiceItemId4` = 11026;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91185 WHERE `RewChoiceItemId5` = 11026;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91185 WHERE `RewChoiceItemId6` = 11026;
UPDATE `quest_template` SET `SrcItemId` = 91185 WHERE `SrcItemId` = 11026;
UPDATE `spell_template` SET `effectItemType1` = 91185 WHERE `effectItemType1` = 11026;
UPDATE `spell_template` SET `effectItemType2` = 91185 WHERE `effectItemType2` = 11026;
UPDATE `spell_template` SET `effectItemType3` = 91185 WHERE `effectItemType3` = 11026;
-- 11027 Wood Frog Box -> 91186 (species 186)
UPDATE IGNORE `npc_vendor` SET `item` = 91186 WHERE `item` = 11027;
UPDATE IGNORE `creature_loot_template` SET `item` = 91186 WHERE `item` = 11027;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91186 WHERE `item` = 11027;
UPDATE IGNORE `item_loot_template` SET `item` = 91186 WHERE `item` = 11027;
UPDATE IGNORE `reference_loot_template` SET `item` = 91186 WHERE `item` = 11027;
UPDATE IGNORE `mail_loot_template` SET `item` = 91186 WHERE `item` = 11027;
UPDATE `quest_template` SET `RewItemId1` = 91186 WHERE `RewItemId1` = 11027;
UPDATE `quest_template` SET `RewItemId2` = 91186 WHERE `RewItemId2` = 11027;
UPDATE `quest_template` SET `RewItemId3` = 91186 WHERE `RewItemId3` = 11027;
UPDATE `quest_template` SET `RewItemId4` = 91186 WHERE `RewItemId4` = 11027;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91186 WHERE `RewChoiceItemId1` = 11027;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91186 WHERE `RewChoiceItemId2` = 11027;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91186 WHERE `RewChoiceItemId3` = 11027;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91186 WHERE `RewChoiceItemId4` = 11027;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91186 WHERE `RewChoiceItemId5` = 11027;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91186 WHERE `RewChoiceItemId6` = 11027;
UPDATE `quest_template` SET `SrcItemId` = 91186 WHERE `SrcItemId` = 11027;
UPDATE `spell_template` SET `effectItemType1` = 91186 WHERE `effectItemType1` = 11027;
UPDATE `spell_template` SET `effectItemType2` = 91186 WHERE `effectItemType2` = 11027;
UPDATE `spell_template` SET `effectItemType3` = 91186 WHERE `effectItemType3` = 11027;
-- 11110 Chicken Egg -> 91184 (species 184)
UPDATE IGNORE `npc_vendor` SET `item` = 91184 WHERE `item` = 11110;
UPDATE IGNORE `creature_loot_template` SET `item` = 91184 WHERE `item` = 11110;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91184 WHERE `item` = 11110;
UPDATE IGNORE `item_loot_template` SET `item` = 91184 WHERE `item` = 11110;
UPDATE IGNORE `reference_loot_template` SET `item` = 91184 WHERE `item` = 11110;
UPDATE IGNORE `mail_loot_template` SET `item` = 91184 WHERE `item` = 11110;
UPDATE `quest_template` SET `RewItemId1` = 91184 WHERE `RewItemId1` = 11110;
UPDATE `quest_template` SET `RewItemId2` = 91184 WHERE `RewItemId2` = 11110;
UPDATE `quest_template` SET `RewItemId3` = 91184 WHERE `RewItemId3` = 11110;
UPDATE `quest_template` SET `RewItemId4` = 91184 WHERE `RewItemId4` = 11110;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91184 WHERE `RewChoiceItemId1` = 11110;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91184 WHERE `RewChoiceItemId2` = 11110;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91184 WHERE `RewChoiceItemId3` = 11110;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91184 WHERE `RewChoiceItemId4` = 11110;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91184 WHERE `RewChoiceItemId5` = 11110;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91184 WHERE `RewChoiceItemId6` = 11110;
UPDATE `quest_template` SET `SrcItemId` = 91184 WHERE `SrcItemId` = 11110;
UPDATE `spell_template` SET `effectItemType1` = 91184 WHERE `effectItemType1` = 11110;
UPDATE `spell_template` SET `effectItemType2` = 91184 WHERE `effectItemType2` = 11110;
UPDATE `spell_template` SET `effectItemType3` = 91184 WHERE `effectItemType3` = 11110;
-- 11474 Sprite Darter Egg -> 90005 (species 5)
UPDATE IGNORE `npc_vendor` SET `item` = 90005 WHERE `item` = 11474;
UPDATE IGNORE `creature_loot_template` SET `item` = 90005 WHERE `item` = 11474;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90005 WHERE `item` = 11474;
UPDATE IGNORE `item_loot_template` SET `item` = 90005 WHERE `item` = 11474;
UPDATE IGNORE `reference_loot_template` SET `item` = 90005 WHERE `item` = 11474;
UPDATE IGNORE `mail_loot_template` SET `item` = 90005 WHERE `item` = 11474;
UPDATE `quest_template` SET `RewItemId1` = 90005 WHERE `RewItemId1` = 11474;
UPDATE `quest_template` SET `RewItemId2` = 90005 WHERE `RewItemId2` = 11474;
UPDATE `quest_template` SET `RewItemId3` = 90005 WHERE `RewItemId3` = 11474;
UPDATE `quest_template` SET `RewItemId4` = 90005 WHERE `RewItemId4` = 11474;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90005 WHERE `RewChoiceItemId1` = 11474;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90005 WHERE `RewChoiceItemId2` = 11474;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90005 WHERE `RewChoiceItemId3` = 11474;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90005 WHERE `RewChoiceItemId4` = 11474;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90005 WHERE `RewChoiceItemId5` = 11474;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90005 WHERE `RewChoiceItemId6` = 11474;
UPDATE `quest_template` SET `SrcItemId` = 90005 WHERE `SrcItemId` = 11474;
UPDATE `spell_template` SET `effectItemType1` = 90005 WHERE `effectItemType1` = 11474;
UPDATE `spell_template` SET `effectItemType2` = 90005 WHERE `effectItemType2` = 11474;
UPDATE `spell_template` SET `effectItemType3` = 90005 WHERE `effectItemType3` = 11474;
-- 11825 Pet Bombling -> 91187 (species 187)
UPDATE IGNORE `npc_vendor` SET `item` = 91187 WHERE `item` = 11825;
UPDATE IGNORE `creature_loot_template` SET `item` = 91187 WHERE `item` = 11825;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91187 WHERE `item` = 11825;
UPDATE IGNORE `item_loot_template` SET `item` = 91187 WHERE `item` = 11825;
UPDATE IGNORE `reference_loot_template` SET `item` = 91187 WHERE `item` = 11825;
UPDATE IGNORE `mail_loot_template` SET `item` = 91187 WHERE `item` = 11825;
UPDATE `quest_template` SET `RewItemId1` = 91187 WHERE `RewItemId1` = 11825;
UPDATE `quest_template` SET `RewItemId2` = 91187 WHERE `RewItemId2` = 11825;
UPDATE `quest_template` SET `RewItemId3` = 91187 WHERE `RewItemId3` = 11825;
UPDATE `quest_template` SET `RewItemId4` = 91187 WHERE `RewItemId4` = 11825;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91187 WHERE `RewChoiceItemId1` = 11825;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91187 WHERE `RewChoiceItemId2` = 11825;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91187 WHERE `RewChoiceItemId3` = 11825;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91187 WHERE `RewChoiceItemId4` = 11825;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91187 WHERE `RewChoiceItemId5` = 11825;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91187 WHERE `RewChoiceItemId6` = 11825;
UPDATE `quest_template` SET `SrcItemId` = 91187 WHERE `SrcItemId` = 11825;
UPDATE `spell_template` SET `effectItemType1` = 91187 WHERE `effectItemType1` = 11825;
UPDATE `spell_template` SET `effectItemType2` = 91187 WHERE `effectItemType2` = 11825;
UPDATE `spell_template` SET `effectItemType3` = 91187 WHERE `effectItemType3` = 11825;
-- 11826 Lil' Smoky -> 91188 (species 188)
UPDATE IGNORE `npc_vendor` SET `item` = 91188 WHERE `item` = 11826;
UPDATE IGNORE `creature_loot_template` SET `item` = 91188 WHERE `item` = 11826;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91188 WHERE `item` = 11826;
UPDATE IGNORE `item_loot_template` SET `item` = 91188 WHERE `item` = 11826;
UPDATE IGNORE `reference_loot_template` SET `item` = 91188 WHERE `item` = 11826;
UPDATE IGNORE `mail_loot_template` SET `item` = 91188 WHERE `item` = 11826;
UPDATE `quest_template` SET `RewItemId1` = 91188 WHERE `RewItemId1` = 11826;
UPDATE `quest_template` SET `RewItemId2` = 91188 WHERE `RewItemId2` = 11826;
UPDATE `quest_template` SET `RewItemId3` = 91188 WHERE `RewItemId3` = 11826;
UPDATE `quest_template` SET `RewItemId4` = 91188 WHERE `RewItemId4` = 11826;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91188 WHERE `RewChoiceItemId1` = 11826;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91188 WHERE `RewChoiceItemId2` = 11826;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91188 WHERE `RewChoiceItemId3` = 11826;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91188 WHERE `RewChoiceItemId4` = 11826;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91188 WHERE `RewChoiceItemId5` = 11826;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91188 WHERE `RewChoiceItemId6` = 11826;
UPDATE `quest_template` SET `SrcItemId` = 91188 WHERE `SrcItemId` = 11826;
UPDATE `spell_template` SET `effectItemType1` = 91188 WHERE `effectItemType1` = 11826;
UPDATE `spell_template` SET `effectItemType2` = 91188 WHERE `effectItemType2` = 11826;
UPDATE `spell_template` SET `effectItemType3` = 91188 WHERE `effectItemType3` = 11826;
-- 11903 Cat Carrier (Corrupted Kitten) -> 91171 (species 171)
UPDATE IGNORE `npc_vendor` SET `item` = 91171 WHERE `item` = 11903;
UPDATE IGNORE `creature_loot_template` SET `item` = 91171 WHERE `item` = 11903;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91171 WHERE `item` = 11903;
UPDATE IGNORE `item_loot_template` SET `item` = 91171 WHERE `item` = 11903;
UPDATE IGNORE `reference_loot_template` SET `item` = 91171 WHERE `item` = 11903;
UPDATE IGNORE `mail_loot_template` SET `item` = 91171 WHERE `item` = 11903;
UPDATE `quest_template` SET `RewItemId1` = 91171 WHERE `RewItemId1` = 11903;
UPDATE `quest_template` SET `RewItemId2` = 91171 WHERE `RewItemId2` = 11903;
UPDATE `quest_template` SET `RewItemId3` = 91171 WHERE `RewItemId3` = 11903;
UPDATE `quest_template` SET `RewItemId4` = 91171 WHERE `RewItemId4` = 11903;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91171 WHERE `RewChoiceItemId1` = 11903;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91171 WHERE `RewChoiceItemId2` = 11903;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91171 WHERE `RewChoiceItemId3` = 11903;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91171 WHERE `RewChoiceItemId4` = 11903;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91171 WHERE `RewChoiceItemId5` = 11903;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91171 WHERE `RewChoiceItemId6` = 11903;
UPDATE `quest_template` SET `SrcItemId` = 91171 WHERE `SrcItemId` = 11903;
UPDATE `spell_template` SET `effectItemType1` = 91171 WHERE `effectItemType1` = 11903;
UPDATE `spell_template` SET `effectItemType2` = 91171 WHERE `effectItemType2` = 11903;
UPDATE `spell_template` SET `effectItemType3` = 91171 WHERE `effectItemType3` = 11903;
-- 12264 Worg Carrier -> 90006 (species 6)
UPDATE IGNORE `npc_vendor` SET `item` = 90006 WHERE `item` = 12264;
UPDATE IGNORE `creature_loot_template` SET `item` = 90006 WHERE `item` = 12264;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90006 WHERE `item` = 12264;
UPDATE IGNORE `item_loot_template` SET `item` = 90006 WHERE `item` = 12264;
UPDATE IGNORE `reference_loot_template` SET `item` = 90006 WHERE `item` = 12264;
UPDATE IGNORE `mail_loot_template` SET `item` = 90006 WHERE `item` = 12264;
UPDATE `quest_template` SET `RewItemId1` = 90006 WHERE `RewItemId1` = 12264;
UPDATE `quest_template` SET `RewItemId2` = 90006 WHERE `RewItemId2` = 12264;
UPDATE `quest_template` SET `RewItemId3` = 90006 WHERE `RewItemId3` = 12264;
UPDATE `quest_template` SET `RewItemId4` = 90006 WHERE `RewItemId4` = 12264;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90006 WHERE `RewChoiceItemId1` = 12264;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90006 WHERE `RewChoiceItemId2` = 12264;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90006 WHERE `RewChoiceItemId3` = 12264;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90006 WHERE `RewChoiceItemId4` = 12264;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90006 WHERE `RewChoiceItemId5` = 12264;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90006 WHERE `RewChoiceItemId6` = 12264;
UPDATE `quest_template` SET `SrcItemId` = 90006 WHERE `SrcItemId` = 12264;
UPDATE `spell_template` SET `effectItemType1` = 90006 WHERE `effectItemType1` = 12264;
UPDATE `spell_template` SET `effectItemType2` = 90006 WHERE `effectItemType2` = 12264;
UPDATE `spell_template` SET `effectItemType3` = 90006 WHERE `effectItemType3` = 12264;
-- 12529 Smolderweb Carrier -> 91191 (species 191)
UPDATE IGNORE `npc_vendor` SET `item` = 91191 WHERE `item` = 12529;
UPDATE IGNORE `creature_loot_template` SET `item` = 91191 WHERE `item` = 12529;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91191 WHERE `item` = 12529;
UPDATE IGNORE `item_loot_template` SET `item` = 91191 WHERE `item` = 12529;
UPDATE IGNORE `reference_loot_template` SET `item` = 91191 WHERE `item` = 12529;
UPDATE IGNORE `mail_loot_template` SET `item` = 91191 WHERE `item` = 12529;
UPDATE `quest_template` SET `RewItemId1` = 91191 WHERE `RewItemId1` = 12529;
UPDATE `quest_template` SET `RewItemId2` = 91191 WHERE `RewItemId2` = 12529;
UPDATE `quest_template` SET `RewItemId3` = 91191 WHERE `RewItemId3` = 12529;
UPDATE `quest_template` SET `RewItemId4` = 91191 WHERE `RewItemId4` = 12529;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91191 WHERE `RewChoiceItemId1` = 12529;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91191 WHERE `RewChoiceItemId2` = 12529;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91191 WHERE `RewChoiceItemId3` = 12529;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91191 WHERE `RewChoiceItemId4` = 12529;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91191 WHERE `RewChoiceItemId5` = 12529;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91191 WHERE `RewChoiceItemId6` = 12529;
UPDATE `quest_template` SET `SrcItemId` = 91191 WHERE `SrcItemId` = 12529;
UPDATE `spell_template` SET `effectItemType1` = 91191 WHERE `effectItemType1` = 12529;
UPDATE `spell_template` SET `effectItemType2` = 91191 WHERE `effectItemType2` = 12529;
UPDATE `spell_template` SET `effectItemType3` = 91191 WHERE `effectItemType3` = 12529;
-- 13582 Zergling Leash -> 91192 (species 192)
UPDATE IGNORE `npc_vendor` SET `item` = 91192 WHERE `item` = 13582;
UPDATE IGNORE `creature_loot_template` SET `item` = 91192 WHERE `item` = 13582;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91192 WHERE `item` = 13582;
UPDATE IGNORE `item_loot_template` SET `item` = 91192 WHERE `item` = 13582;
UPDATE IGNORE `reference_loot_template` SET `item` = 91192 WHERE `item` = 13582;
UPDATE IGNORE `mail_loot_template` SET `item` = 91192 WHERE `item` = 13582;
UPDATE `quest_template` SET `RewItemId1` = 91192 WHERE `RewItemId1` = 13582;
UPDATE `quest_template` SET `RewItemId2` = 91192 WHERE `RewItemId2` = 13582;
UPDATE `quest_template` SET `RewItemId3` = 91192 WHERE `RewItemId3` = 13582;
UPDATE `quest_template` SET `RewItemId4` = 91192 WHERE `RewItemId4` = 13582;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91192 WHERE `RewChoiceItemId1` = 13582;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91192 WHERE `RewChoiceItemId2` = 13582;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91192 WHERE `RewChoiceItemId3` = 13582;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91192 WHERE `RewChoiceItemId4` = 13582;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91192 WHERE `RewChoiceItemId5` = 13582;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91192 WHERE `RewChoiceItemId6` = 13582;
UPDATE `quest_template` SET `SrcItemId` = 91192 WHERE `SrcItemId` = 13582;
UPDATE `spell_template` SET `effectItemType1` = 91192 WHERE `effectItemType1` = 13582;
UPDATE `spell_template` SET `effectItemType2` = 91192 WHERE `effectItemType2` = 13582;
UPDATE `spell_template` SET `effectItemType3` = 91192 WHERE `effectItemType3` = 13582;
-- 13583 Panda Collar -> 91193 (species 193)
UPDATE IGNORE `npc_vendor` SET `item` = 91193 WHERE `item` = 13583;
UPDATE IGNORE `creature_loot_template` SET `item` = 91193 WHERE `item` = 13583;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91193 WHERE `item` = 13583;
UPDATE IGNORE `item_loot_template` SET `item` = 91193 WHERE `item` = 13583;
UPDATE IGNORE `reference_loot_template` SET `item` = 91193 WHERE `item` = 13583;
UPDATE IGNORE `mail_loot_template` SET `item` = 91193 WHERE `item` = 13583;
UPDATE `quest_template` SET `RewItemId1` = 91193 WHERE `RewItemId1` = 13583;
UPDATE `quest_template` SET `RewItemId2` = 91193 WHERE `RewItemId2` = 13583;
UPDATE `quest_template` SET `RewItemId3` = 91193 WHERE `RewItemId3` = 13583;
UPDATE `quest_template` SET `RewItemId4` = 91193 WHERE `RewItemId4` = 13583;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91193 WHERE `RewChoiceItemId1` = 13583;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91193 WHERE `RewChoiceItemId2` = 13583;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91193 WHERE `RewChoiceItemId3` = 13583;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91193 WHERE `RewChoiceItemId4` = 13583;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91193 WHERE `RewChoiceItemId5` = 13583;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91193 WHERE `RewChoiceItemId6` = 13583;
UPDATE `quest_template` SET `SrcItemId` = 91193 WHERE `SrcItemId` = 13583;
UPDATE `spell_template` SET `effectItemType1` = 91193 WHERE `effectItemType1` = 13583;
UPDATE `spell_template` SET `effectItemType2` = 91193 WHERE `effectItemType2` = 13583;
UPDATE `spell_template` SET `effectItemType3` = 91193 WHERE `effectItemType3` = 13583;
-- 13584 Diablo Stone -> 91194 (species 194)
UPDATE IGNORE `npc_vendor` SET `item` = 91194 WHERE `item` = 13584;
UPDATE IGNORE `creature_loot_template` SET `item` = 91194 WHERE `item` = 13584;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91194 WHERE `item` = 13584;
UPDATE IGNORE `item_loot_template` SET `item` = 91194 WHERE `item` = 13584;
UPDATE IGNORE `reference_loot_template` SET `item` = 91194 WHERE `item` = 13584;
UPDATE IGNORE `mail_loot_template` SET `item` = 91194 WHERE `item` = 13584;
UPDATE `quest_template` SET `RewItemId1` = 91194 WHERE `RewItemId1` = 13584;
UPDATE `quest_template` SET `RewItemId2` = 91194 WHERE `RewItemId2` = 13584;
UPDATE `quest_template` SET `RewItemId3` = 91194 WHERE `RewItemId3` = 13584;
UPDATE `quest_template` SET `RewItemId4` = 91194 WHERE `RewItemId4` = 13584;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91194 WHERE `RewChoiceItemId1` = 13584;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91194 WHERE `RewChoiceItemId2` = 13584;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91194 WHERE `RewChoiceItemId3` = 13584;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91194 WHERE `RewChoiceItemId4` = 13584;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91194 WHERE `RewChoiceItemId5` = 13584;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91194 WHERE `RewChoiceItemId6` = 13584;
UPDATE `quest_template` SET `SrcItemId` = 91194 WHERE `SrcItemId` = 13584;
UPDATE `spell_template` SET `effectItemType1` = 91194 WHERE `effectItemType1` = 13584;
UPDATE `spell_template` SET `effectItemType2` = 91194 WHERE `effectItemType2` = 13584;
UPDATE `spell_template` SET `effectItemType3` = 91194 WHERE `effectItemType3` = 13584;
-- 15996 Lifelike Mechanical Toad -> 91189 (species 189)
UPDATE IGNORE `npc_vendor` SET `item` = 91189 WHERE `item` = 15996;
UPDATE IGNORE `creature_loot_template` SET `item` = 91189 WHERE `item` = 15996;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91189 WHERE `item` = 15996;
UPDATE IGNORE `item_loot_template` SET `item` = 91189 WHERE `item` = 15996;
UPDATE IGNORE `reference_loot_template` SET `item` = 91189 WHERE `item` = 15996;
UPDATE IGNORE `mail_loot_template` SET `item` = 91189 WHERE `item` = 15996;
UPDATE `quest_template` SET `RewItemId1` = 91189 WHERE `RewItemId1` = 15996;
UPDATE `quest_template` SET `RewItemId2` = 91189 WHERE `RewItemId2` = 15996;
UPDATE `quest_template` SET `RewItemId3` = 91189 WHERE `RewItemId3` = 15996;
UPDATE `quest_template` SET `RewItemId4` = 91189 WHERE `RewItemId4` = 15996;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91189 WHERE `RewChoiceItemId1` = 15996;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91189 WHERE `RewChoiceItemId2` = 15996;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91189 WHERE `RewChoiceItemId3` = 15996;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91189 WHERE `RewChoiceItemId4` = 15996;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91189 WHERE `RewChoiceItemId5` = 15996;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91189 WHERE `RewChoiceItemId6` = 15996;
UPDATE `quest_template` SET `SrcItemId` = 91189 WHERE `SrcItemId` = 15996;
UPDATE `spell_template` SET `effectItemType1` = 91189 WHERE `effectItemType1` = 15996;
UPDATE `spell_template` SET `effectItemType2` = 91189 WHERE `effectItemType2` = 15996;
UPDATE `spell_template` SET `effectItemType3` = 91189 WHERE `effectItemType3` = 15996;
-- 18963 Turtle Egg (Albino) -> 91195 (species 195)
UPDATE IGNORE `npc_vendor` SET `item` = 91195 WHERE `item` = 18963;
UPDATE IGNORE `creature_loot_template` SET `item` = 91195 WHERE `item` = 18963;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91195 WHERE `item` = 18963;
UPDATE IGNORE `item_loot_template` SET `item` = 91195 WHERE `item` = 18963;
UPDATE IGNORE `reference_loot_template` SET `item` = 91195 WHERE `item` = 18963;
UPDATE IGNORE `mail_loot_template` SET `item` = 91195 WHERE `item` = 18963;
UPDATE `quest_template` SET `RewItemId1` = 91195 WHERE `RewItemId1` = 18963;
UPDATE `quest_template` SET `RewItemId2` = 91195 WHERE `RewItemId2` = 18963;
UPDATE `quest_template` SET `RewItemId3` = 91195 WHERE `RewItemId3` = 18963;
UPDATE `quest_template` SET `RewItemId4` = 91195 WHERE `RewItemId4` = 18963;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91195 WHERE `RewChoiceItemId1` = 18963;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91195 WHERE `RewChoiceItemId2` = 18963;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91195 WHERE `RewChoiceItemId3` = 18963;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91195 WHERE `RewChoiceItemId4` = 18963;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91195 WHERE `RewChoiceItemId5` = 18963;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91195 WHERE `RewChoiceItemId6` = 18963;
UPDATE `quest_template` SET `SrcItemId` = 91195 WHERE `SrcItemId` = 18963;
UPDATE `spell_template` SET `effectItemType1` = 91195 WHERE `effectItemType1` = 18963;
UPDATE `spell_template` SET `effectItemType2` = 91195 WHERE `effectItemType2` = 18963;
UPDATE `spell_template` SET `effectItemType3` = 91195 WHERE `effectItemType3` = 18963;
-- 18964 Turtle Egg (Loggerhead) -> 91196 (species 196)
UPDATE IGNORE `npc_vendor` SET `item` = 91196 WHERE `item` = 18964;
UPDATE IGNORE `creature_loot_template` SET `item` = 91196 WHERE `item` = 18964;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91196 WHERE `item` = 18964;
UPDATE IGNORE `item_loot_template` SET `item` = 91196 WHERE `item` = 18964;
UPDATE IGNORE `reference_loot_template` SET `item` = 91196 WHERE `item` = 18964;
UPDATE IGNORE `mail_loot_template` SET `item` = 91196 WHERE `item` = 18964;
UPDATE `quest_template` SET `RewItemId1` = 91196 WHERE `RewItemId1` = 18964;
UPDATE `quest_template` SET `RewItemId2` = 91196 WHERE `RewItemId2` = 18964;
UPDATE `quest_template` SET `RewItemId3` = 91196 WHERE `RewItemId3` = 18964;
UPDATE `quest_template` SET `RewItemId4` = 91196 WHERE `RewItemId4` = 18964;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91196 WHERE `RewChoiceItemId1` = 18964;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91196 WHERE `RewChoiceItemId2` = 18964;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91196 WHERE `RewChoiceItemId3` = 18964;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91196 WHERE `RewChoiceItemId4` = 18964;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91196 WHERE `RewChoiceItemId5` = 18964;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91196 WHERE `RewChoiceItemId6` = 18964;
UPDATE `quest_template` SET `SrcItemId` = 91196 WHERE `SrcItemId` = 18964;
UPDATE `spell_template` SET `effectItemType1` = 91196 WHERE `effectItemType1` = 18964;
UPDATE `spell_template` SET `effectItemType2` = 91196 WHERE `effectItemType2` = 18964;
UPDATE `spell_template` SET `effectItemType3` = 91196 WHERE `effectItemType3` = 18964;
-- 18965 Turtle Egg (Hawksbill) -> 91197 (species 197)
UPDATE IGNORE `npc_vendor` SET `item` = 91197 WHERE `item` = 18965;
UPDATE IGNORE `creature_loot_template` SET `item` = 91197 WHERE `item` = 18965;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91197 WHERE `item` = 18965;
UPDATE IGNORE `item_loot_template` SET `item` = 91197 WHERE `item` = 18965;
UPDATE IGNORE `reference_loot_template` SET `item` = 91197 WHERE `item` = 18965;
UPDATE IGNORE `mail_loot_template` SET `item` = 91197 WHERE `item` = 18965;
UPDATE `quest_template` SET `RewItemId1` = 91197 WHERE `RewItemId1` = 18965;
UPDATE `quest_template` SET `RewItemId2` = 91197 WHERE `RewItemId2` = 18965;
UPDATE `quest_template` SET `RewItemId3` = 91197 WHERE `RewItemId3` = 18965;
UPDATE `quest_template` SET `RewItemId4` = 91197 WHERE `RewItemId4` = 18965;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91197 WHERE `RewChoiceItemId1` = 18965;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91197 WHERE `RewChoiceItemId2` = 18965;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91197 WHERE `RewChoiceItemId3` = 18965;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91197 WHERE `RewChoiceItemId4` = 18965;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91197 WHERE `RewChoiceItemId5` = 18965;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91197 WHERE `RewChoiceItemId6` = 18965;
UPDATE `quest_template` SET `SrcItemId` = 91197 WHERE `SrcItemId` = 18965;
UPDATE `spell_template` SET `effectItemType1` = 91197 WHERE `effectItemType1` = 18965;
UPDATE `spell_template` SET `effectItemType2` = 91197 WHERE `effectItemType2` = 18965;
UPDATE `spell_template` SET `effectItemType3` = 91197 WHERE `effectItemType3` = 18965;
-- 18966 Turtle Egg (Leatherback) -> 91198 (species 198)
UPDATE IGNORE `npc_vendor` SET `item` = 91198 WHERE `item` = 18966;
UPDATE IGNORE `creature_loot_template` SET `item` = 91198 WHERE `item` = 18966;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91198 WHERE `item` = 18966;
UPDATE IGNORE `item_loot_template` SET `item` = 91198 WHERE `item` = 18966;
UPDATE IGNORE `reference_loot_template` SET `item` = 91198 WHERE `item` = 18966;
UPDATE IGNORE `mail_loot_template` SET `item` = 91198 WHERE `item` = 18966;
UPDATE `quest_template` SET `RewItemId1` = 91198 WHERE `RewItemId1` = 18966;
UPDATE `quest_template` SET `RewItemId2` = 91198 WHERE `RewItemId2` = 18966;
UPDATE `quest_template` SET `RewItemId3` = 91198 WHERE `RewItemId3` = 18966;
UPDATE `quest_template` SET `RewItemId4` = 91198 WHERE `RewItemId4` = 18966;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91198 WHERE `RewChoiceItemId1` = 18966;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91198 WHERE `RewChoiceItemId2` = 18966;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91198 WHERE `RewChoiceItemId3` = 18966;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91198 WHERE `RewChoiceItemId4` = 18966;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91198 WHERE `RewChoiceItemId5` = 18966;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91198 WHERE `RewChoiceItemId6` = 18966;
UPDATE `quest_template` SET `SrcItemId` = 91198 WHERE `SrcItemId` = 18966;
UPDATE `spell_template` SET `effectItemType1` = 91198 WHERE `effectItemType1` = 18966;
UPDATE `spell_template` SET `effectItemType2` = 91198 WHERE `effectItemType2` = 18966;
UPDATE `spell_template` SET `effectItemType3` = 91198 WHERE `effectItemType3` = 18966;
-- 18967 Turtle Egg (Olive) -> 91199 (species 199)
UPDATE IGNORE `npc_vendor` SET `item` = 91199 WHERE `item` = 18967;
UPDATE IGNORE `creature_loot_template` SET `item` = 91199 WHERE `item` = 18967;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91199 WHERE `item` = 18967;
UPDATE IGNORE `item_loot_template` SET `item` = 91199 WHERE `item` = 18967;
UPDATE IGNORE `reference_loot_template` SET `item` = 91199 WHERE `item` = 18967;
UPDATE IGNORE `mail_loot_template` SET `item` = 91199 WHERE `item` = 18967;
UPDATE `quest_template` SET `RewItemId1` = 91199 WHERE `RewItemId1` = 18967;
UPDATE `quest_template` SET `RewItemId2` = 91199 WHERE `RewItemId2` = 18967;
UPDATE `quest_template` SET `RewItemId3` = 91199 WHERE `RewItemId3` = 18967;
UPDATE `quest_template` SET `RewItemId4` = 91199 WHERE `RewItemId4` = 18967;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91199 WHERE `RewChoiceItemId1` = 18967;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91199 WHERE `RewChoiceItemId2` = 18967;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91199 WHERE `RewChoiceItemId3` = 18967;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91199 WHERE `RewChoiceItemId4` = 18967;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91199 WHERE `RewChoiceItemId5` = 18967;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91199 WHERE `RewChoiceItemId6` = 18967;
UPDATE `quest_template` SET `SrcItemId` = 91199 WHERE `SrcItemId` = 18967;
UPDATE `spell_template` SET `effectItemType1` = 91199 WHERE `effectItemType1` = 18967;
UPDATE `spell_template` SET `effectItemType2` = 91199 WHERE `effectItemType2` = 18967;
UPDATE `spell_template` SET `effectItemType3` = 91199 WHERE `effectItemType3` = 18967;
-- 19054 Red Dragon Orb -> 91200 (species 200)
UPDATE IGNORE `npc_vendor` SET `item` = 91200 WHERE `item` = 19054;
UPDATE IGNORE `creature_loot_template` SET `item` = 91200 WHERE `item` = 19054;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91200 WHERE `item` = 19054;
UPDATE IGNORE `item_loot_template` SET `item` = 91200 WHERE `item` = 19054;
UPDATE IGNORE `reference_loot_template` SET `item` = 91200 WHERE `item` = 19054;
UPDATE IGNORE `mail_loot_template` SET `item` = 91200 WHERE `item` = 19054;
UPDATE `quest_template` SET `RewItemId1` = 91200 WHERE `RewItemId1` = 19054;
UPDATE `quest_template` SET `RewItemId2` = 91200 WHERE `RewItemId2` = 19054;
UPDATE `quest_template` SET `RewItemId3` = 91200 WHERE `RewItemId3` = 19054;
UPDATE `quest_template` SET `RewItemId4` = 91200 WHERE `RewItemId4` = 19054;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91200 WHERE `RewChoiceItemId1` = 19054;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91200 WHERE `RewChoiceItemId2` = 19054;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91200 WHERE `RewChoiceItemId3` = 19054;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91200 WHERE `RewChoiceItemId4` = 19054;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91200 WHERE `RewChoiceItemId5` = 19054;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91200 WHERE `RewChoiceItemId6` = 19054;
UPDATE `quest_template` SET `SrcItemId` = 91200 WHERE `SrcItemId` = 19054;
UPDATE `spell_template` SET `effectItemType1` = 91200 WHERE `effectItemType1` = 19054;
UPDATE `spell_template` SET `effectItemType2` = 91200 WHERE `effectItemType2` = 19054;
UPDATE `spell_template` SET `effectItemType3` = 91200 WHERE `effectItemType3` = 19054;
-- 19055 Green Dragon Orb -> 91201 (species 201)
UPDATE IGNORE `npc_vendor` SET `item` = 91201 WHERE `item` = 19055;
UPDATE IGNORE `creature_loot_template` SET `item` = 91201 WHERE `item` = 19055;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91201 WHERE `item` = 19055;
UPDATE IGNORE `item_loot_template` SET `item` = 91201 WHERE `item` = 19055;
UPDATE IGNORE `reference_loot_template` SET `item` = 91201 WHERE `item` = 19055;
UPDATE IGNORE `mail_loot_template` SET `item` = 91201 WHERE `item` = 19055;
UPDATE `quest_template` SET `RewItemId1` = 91201 WHERE `RewItemId1` = 19055;
UPDATE `quest_template` SET `RewItemId2` = 91201 WHERE `RewItemId2` = 19055;
UPDATE `quest_template` SET `RewItemId3` = 91201 WHERE `RewItemId3` = 19055;
UPDATE `quest_template` SET `RewItemId4` = 91201 WHERE `RewItemId4` = 19055;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91201 WHERE `RewChoiceItemId1` = 19055;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91201 WHERE `RewChoiceItemId2` = 19055;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91201 WHERE `RewChoiceItemId3` = 19055;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91201 WHERE `RewChoiceItemId4` = 19055;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91201 WHERE `RewChoiceItemId5` = 19055;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91201 WHERE `RewChoiceItemId6` = 19055;
UPDATE `quest_template` SET `SrcItemId` = 91201 WHERE `SrcItemId` = 19055;
UPDATE `spell_template` SET `effectItemType1` = 91201 WHERE `effectItemType1` = 19055;
UPDATE `spell_template` SET `effectItemType2` = 91201 WHERE `effectItemType2` = 19055;
UPDATE `spell_template` SET `effectItemType3` = 91201 WHERE `effectItemType3` = 19055;
-- 19450 A Jubling's Tiny Home -> 91202 (species 202)
UPDATE IGNORE `npc_vendor` SET `item` = 91202 WHERE `item` = 19450;
UPDATE IGNORE `creature_loot_template` SET `item` = 91202 WHERE `item` = 19450;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91202 WHERE `item` = 19450;
UPDATE IGNORE `item_loot_template` SET `item` = 91202 WHERE `item` = 19450;
UPDATE IGNORE `reference_loot_template` SET `item` = 91202 WHERE `item` = 19450;
UPDATE IGNORE `mail_loot_template` SET `item` = 91202 WHERE `item` = 19450;
UPDATE `quest_template` SET `RewItemId1` = 91202 WHERE `RewItemId1` = 19450;
UPDATE `quest_template` SET `RewItemId2` = 91202 WHERE `RewItemId2` = 19450;
UPDATE `quest_template` SET `RewItemId3` = 91202 WHERE `RewItemId3` = 19450;
UPDATE `quest_template` SET `RewItemId4` = 91202 WHERE `RewItemId4` = 19450;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91202 WHERE `RewChoiceItemId1` = 19450;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91202 WHERE `RewChoiceItemId2` = 19450;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91202 WHERE `RewChoiceItemId3` = 19450;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91202 WHERE `RewChoiceItemId4` = 19450;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91202 WHERE `RewChoiceItemId5` = 19450;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91202 WHERE `RewChoiceItemId6` = 19450;
UPDATE `quest_template` SET `SrcItemId` = 91202 WHERE `SrcItemId` = 19450;
UPDATE `spell_template` SET `effectItemType1` = 91202 WHERE `effectItemType1` = 19450;
UPDATE `spell_template` SET `effectItemType2` = 91202 WHERE `effectItemType2` = 19450;
UPDATE `spell_template` SET `effectItemType3` = 91202 WHERE `effectItemType3` = 19450;
-- 20371 Blue Murloc Egg -> 90023 (species 23)
UPDATE IGNORE `npc_vendor` SET `item` = 90023 WHERE `item` = 20371;
UPDATE IGNORE `creature_loot_template` SET `item` = 90023 WHERE `item` = 20371;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90023 WHERE `item` = 20371;
UPDATE IGNORE `item_loot_template` SET `item` = 90023 WHERE `item` = 20371;
UPDATE IGNORE `reference_loot_template` SET `item` = 90023 WHERE `item` = 20371;
UPDATE IGNORE `mail_loot_template` SET `item` = 90023 WHERE `item` = 20371;
UPDATE `quest_template` SET `RewItemId1` = 90023 WHERE `RewItemId1` = 20371;
UPDATE `quest_template` SET `RewItemId2` = 90023 WHERE `RewItemId2` = 20371;
UPDATE `quest_template` SET `RewItemId3` = 90023 WHERE `RewItemId3` = 20371;
UPDATE `quest_template` SET `RewItemId4` = 90023 WHERE `RewItemId4` = 20371;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90023 WHERE `RewChoiceItemId1` = 20371;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90023 WHERE `RewChoiceItemId2` = 20371;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90023 WHERE `RewChoiceItemId3` = 20371;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90023 WHERE `RewChoiceItemId4` = 20371;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90023 WHERE `RewChoiceItemId5` = 20371;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90023 WHERE `RewChoiceItemId6` = 20371;
UPDATE `quest_template` SET `SrcItemId` = 90023 WHERE `SrcItemId` = 20371;
UPDATE `spell_template` SET `effectItemType1` = 90023 WHERE `effectItemType1` = 20371;
UPDATE `spell_template` SET `effectItemType2` = 90023 WHERE `effectItemType2` = 20371;
UPDATE `spell_template` SET `effectItemType3` = 90023 WHERE `effectItemType3` = 20371;
-- 20651 Orange Murloc Egg -> 91203 (species 203)
UPDATE IGNORE `npc_vendor` SET `item` = 91203 WHERE `item` = 20651;
UPDATE IGNORE `creature_loot_template` SET `item` = 91203 WHERE `item` = 20651;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91203 WHERE `item` = 20651;
UPDATE IGNORE `item_loot_template` SET `item` = 91203 WHERE `item` = 20651;
UPDATE IGNORE `reference_loot_template` SET `item` = 91203 WHERE `item` = 20651;
UPDATE IGNORE `mail_loot_template` SET `item` = 91203 WHERE `item` = 20651;
UPDATE `quest_template` SET `RewItemId1` = 91203 WHERE `RewItemId1` = 20651;
UPDATE `quest_template` SET `RewItemId2` = 91203 WHERE `RewItemId2` = 20651;
UPDATE `quest_template` SET `RewItemId3` = 91203 WHERE `RewItemId3` = 20651;
UPDATE `quest_template` SET `RewItemId4` = 91203 WHERE `RewItemId4` = 20651;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91203 WHERE `RewChoiceItemId1` = 20651;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91203 WHERE `RewChoiceItemId2` = 20651;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91203 WHERE `RewChoiceItemId3` = 20651;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91203 WHERE `RewChoiceItemId4` = 20651;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91203 WHERE `RewChoiceItemId5` = 20651;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91203 WHERE `RewChoiceItemId6` = 20651;
UPDATE `quest_template` SET `SrcItemId` = 91203 WHERE `SrcItemId` = 20651;
UPDATE `spell_template` SET `effectItemType1` = 91203 WHERE `effectItemType1` = 20651;
UPDATE `spell_template` SET `effectItemType2` = 91203 WHERE `effectItemType2` = 20651;
UPDATE `spell_template` SET `effectItemType3` = 91203 WHERE `effectItemType3` = 20651;
-- 20769 Disgusting Oozeling -> 90025 (species 25)
UPDATE IGNORE `npc_vendor` SET `item` = 90025 WHERE `item` = 20769;
UPDATE IGNORE `creature_loot_template` SET `item` = 90025 WHERE `item` = 20769;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 90025 WHERE `item` = 20769;
UPDATE IGNORE `item_loot_template` SET `item` = 90025 WHERE `item` = 20769;
UPDATE IGNORE `reference_loot_template` SET `item` = 90025 WHERE `item` = 20769;
UPDATE IGNORE `mail_loot_template` SET `item` = 90025 WHERE `item` = 20769;
UPDATE `quest_template` SET `RewItemId1` = 90025 WHERE `RewItemId1` = 20769;
UPDATE `quest_template` SET `RewItemId2` = 90025 WHERE `RewItemId2` = 20769;
UPDATE `quest_template` SET `RewItemId3` = 90025 WHERE `RewItemId3` = 20769;
UPDATE `quest_template` SET `RewItemId4` = 90025 WHERE `RewItemId4` = 20769;
UPDATE `quest_template` SET `RewChoiceItemId1` = 90025 WHERE `RewChoiceItemId1` = 20769;
UPDATE `quest_template` SET `RewChoiceItemId2` = 90025 WHERE `RewChoiceItemId2` = 20769;
UPDATE `quest_template` SET `RewChoiceItemId3` = 90025 WHERE `RewChoiceItemId3` = 20769;
UPDATE `quest_template` SET `RewChoiceItemId4` = 90025 WHERE `RewChoiceItemId4` = 20769;
UPDATE `quest_template` SET `RewChoiceItemId5` = 90025 WHERE `RewChoiceItemId5` = 20769;
UPDATE `quest_template` SET `RewChoiceItemId6` = 90025 WHERE `RewChoiceItemId6` = 20769;
UPDATE `quest_template` SET `SrcItemId` = 90025 WHERE `SrcItemId` = 20769;
UPDATE `spell_template` SET `effectItemType1` = 90025 WHERE `effectItemType1` = 20769;
UPDATE `spell_template` SET `effectItemType2` = 90025 WHERE `effectItemType2` = 20769;
UPDATE `spell_template` SET `effectItemType3` = 90025 WHERE `effectItemType3` = 20769;
-- 21168 Baby Shark -> 91206 (species 206)
UPDATE IGNORE `npc_vendor` SET `item` = 91206 WHERE `item` = 21168;
UPDATE IGNORE `creature_loot_template` SET `item` = 91206 WHERE `item` = 21168;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91206 WHERE `item` = 21168;
UPDATE IGNORE `item_loot_template` SET `item` = 91206 WHERE `item` = 21168;
UPDATE IGNORE `reference_loot_template` SET `item` = 91206 WHERE `item` = 21168;
UPDATE IGNORE `mail_loot_template` SET `item` = 91206 WHERE `item` = 21168;
UPDATE `quest_template` SET `RewItemId1` = 91206 WHERE `RewItemId1` = 21168;
UPDATE `quest_template` SET `RewItemId2` = 91206 WHERE `RewItemId2` = 21168;
UPDATE `quest_template` SET `RewItemId3` = 91206 WHERE `RewItemId3` = 21168;
UPDATE `quest_template` SET `RewItemId4` = 91206 WHERE `RewItemId4` = 21168;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91206 WHERE `RewChoiceItemId1` = 21168;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91206 WHERE `RewChoiceItemId2` = 21168;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91206 WHERE `RewChoiceItemId3` = 21168;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91206 WHERE `RewChoiceItemId4` = 21168;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91206 WHERE `RewChoiceItemId5` = 21168;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91206 WHERE `RewChoiceItemId6` = 21168;
UPDATE `quest_template` SET `SrcItemId` = 91206 WHERE `SrcItemId` = 21168;
UPDATE `spell_template` SET `effectItemType1` = 91206 WHERE `effectItemType1` = 21168;
UPDATE `spell_template` SET `effectItemType2` = 91206 WHERE `effectItemType2` = 21168;
UPDATE `spell_template` SET `effectItemType3` = 91206 WHERE `effectItemType3` = 21168;
-- 21277 Tranquil Mechanical Yeti -> 91190 (species 190)
UPDATE IGNORE `npc_vendor` SET `item` = 91190 WHERE `item` = 21277;
UPDATE IGNORE `creature_loot_template` SET `item` = 91190 WHERE `item` = 21277;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91190 WHERE `item` = 21277;
UPDATE IGNORE `item_loot_template` SET `item` = 91190 WHERE `item` = 21277;
UPDATE IGNORE `reference_loot_template` SET `item` = 91190 WHERE `item` = 21277;
UPDATE IGNORE `mail_loot_template` SET `item` = 91190 WHERE `item` = 21277;
UPDATE `quest_template` SET `RewItemId1` = 91190 WHERE `RewItemId1` = 21277;
UPDATE `quest_template` SET `RewItemId2` = 91190 WHERE `RewItemId2` = 21277;
UPDATE `quest_template` SET `RewItemId3` = 91190 WHERE `RewItemId3` = 21277;
UPDATE `quest_template` SET `RewItemId4` = 91190 WHERE `RewItemId4` = 21277;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91190 WHERE `RewChoiceItemId1` = 21277;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91190 WHERE `RewChoiceItemId2` = 21277;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91190 WHERE `RewChoiceItemId3` = 21277;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91190 WHERE `RewChoiceItemId4` = 21277;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91190 WHERE `RewChoiceItemId5` = 21277;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91190 WHERE `RewChoiceItemId6` = 21277;
UPDATE `quest_template` SET `SrcItemId` = 91190 WHERE `SrcItemId` = 21277;
UPDATE `spell_template` SET `effectItemType1` = 91190 WHERE `effectItemType1` = 21277;
UPDATE `spell_template` SET `effectItemType2` = 91190 WHERE `effectItemType2` = 21277;
UPDATE `spell_template` SET `effectItemType3` = 91190 WHERE `effectItemType3` = 21277;
-- 22114 Pink Murloc Egg -> 91204 (species 204)
UPDATE IGNORE `npc_vendor` SET `item` = 91204 WHERE `item` = 22114;
UPDATE IGNORE `creature_loot_template` SET `item` = 91204 WHERE `item` = 22114;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91204 WHERE `item` = 22114;
UPDATE IGNORE `item_loot_template` SET `item` = 91204 WHERE `item` = 22114;
UPDATE IGNORE `reference_loot_template` SET `item` = 91204 WHERE `item` = 22114;
UPDATE IGNORE `mail_loot_template` SET `item` = 91204 WHERE `item` = 22114;
UPDATE `quest_template` SET `RewItemId1` = 91204 WHERE `RewItemId1` = 22114;
UPDATE `quest_template` SET `RewItemId2` = 91204 WHERE `RewItemId2` = 22114;
UPDATE `quest_template` SET `RewItemId3` = 91204 WHERE `RewItemId3` = 22114;
UPDATE `quest_template` SET `RewItemId4` = 91204 WHERE `RewItemId4` = 22114;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91204 WHERE `RewChoiceItemId1` = 22114;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91204 WHERE `RewChoiceItemId2` = 22114;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91204 WHERE `RewChoiceItemId3` = 22114;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91204 WHERE `RewChoiceItemId4` = 22114;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91204 WHERE `RewChoiceItemId5` = 22114;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91204 WHERE `RewChoiceItemId6` = 22114;
UPDATE `quest_template` SET `SrcItemId` = 91204 WHERE `SrcItemId` = 22114;
UPDATE `spell_template` SET `effectItemType1` = 91204 WHERE `effectItemType1` = 22114;
UPDATE `spell_template` SET `effectItemType2` = 91204 WHERE `effectItemType2` = 22114;
UPDATE `spell_template` SET `effectItemType3` = 91204 WHERE `effectItemType3` = 22114;
-- 22235 Truesilver Shafted Arrow -> 91207 (species 207)
UPDATE IGNORE `npc_vendor` SET `item` = 91207 WHERE `item` = 22235;
UPDATE IGNORE `creature_loot_template` SET `item` = 91207 WHERE `item` = 22235;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91207 WHERE `item` = 22235;
UPDATE IGNORE `item_loot_template` SET `item` = 91207 WHERE `item` = 22235;
UPDATE IGNORE `reference_loot_template` SET `item` = 91207 WHERE `item` = 22235;
UPDATE IGNORE `mail_loot_template` SET `item` = 91207 WHERE `item` = 22235;
UPDATE `quest_template` SET `RewItemId1` = 91207 WHERE `RewItemId1` = 22235;
UPDATE `quest_template` SET `RewItemId2` = 91207 WHERE `RewItemId2` = 22235;
UPDATE `quest_template` SET `RewItemId3` = 91207 WHERE `RewItemId3` = 22235;
UPDATE `quest_template` SET `RewItemId4` = 91207 WHERE `RewItemId4` = 22235;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91207 WHERE `RewChoiceItemId1` = 22235;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91207 WHERE `RewChoiceItemId2` = 22235;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91207 WHERE `RewChoiceItemId3` = 22235;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91207 WHERE `RewChoiceItemId4` = 22235;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91207 WHERE `RewChoiceItemId5` = 22235;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91207 WHERE `RewChoiceItemId6` = 22235;
UPDATE `quest_template` SET `SrcItemId` = 91207 WHERE `SrcItemId` = 22235;
UPDATE `spell_template` SET `effectItemType1` = 91207 WHERE `effectItemType1` = 22235;
UPDATE `spell_template` SET `effectItemType2` = 91207 WHERE `effectItemType2` = 22235;
UPDATE `spell_template` SET `effectItemType3` = 91207 WHERE `effectItemType3` = 22235;
-- 22780 White Murloc Egg -> 91205 (species 205)
UPDATE IGNORE `npc_vendor` SET `item` = 91205 WHERE `item` = 22780;
UPDATE IGNORE `creature_loot_template` SET `item` = 91205 WHERE `item` = 22780;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91205 WHERE `item` = 22780;
UPDATE IGNORE `item_loot_template` SET `item` = 91205 WHERE `item` = 22780;
UPDATE IGNORE `reference_loot_template` SET `item` = 91205 WHERE `item` = 22780;
UPDATE IGNORE `mail_loot_template` SET `item` = 91205 WHERE `item` = 22780;
UPDATE `quest_template` SET `RewItemId1` = 91205 WHERE `RewItemId1` = 22780;
UPDATE `quest_template` SET `RewItemId2` = 91205 WHERE `RewItemId2` = 22780;
UPDATE `quest_template` SET `RewItemId3` = 91205 WHERE `RewItemId3` = 22780;
UPDATE `quest_template` SET `RewItemId4` = 91205 WHERE `RewItemId4` = 22780;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91205 WHERE `RewChoiceItemId1` = 22780;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91205 WHERE `RewChoiceItemId2` = 22780;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91205 WHERE `RewChoiceItemId3` = 22780;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91205 WHERE `RewChoiceItemId4` = 22780;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91205 WHERE `RewChoiceItemId5` = 22780;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91205 WHERE `RewChoiceItemId6` = 22780;
UPDATE `quest_template` SET `SrcItemId` = 91205 WHERE `SrcItemId` = 22780;
UPDATE `spell_template` SET `effectItemType1` = 91205 WHERE `effectItemType1` = 22780;
UPDATE `spell_template` SET `effectItemType2` = 91205 WHERE `effectItemType2` = 22780;
UPDATE `spell_template` SET `effectItemType3` = 91205 WHERE `effectItemType3` = 22780;
-- 22781 Polar Bear Collar -> 91208 (species 208)
UPDATE IGNORE `npc_vendor` SET `item` = 91208 WHERE `item` = 22781;
UPDATE IGNORE `creature_loot_template` SET `item` = 91208 WHERE `item` = 22781;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91208 WHERE `item` = 22781;
UPDATE IGNORE `item_loot_template` SET `item` = 91208 WHERE `item` = 22781;
UPDATE IGNORE `reference_loot_template` SET `item` = 91208 WHERE `item` = 22781;
UPDATE IGNORE `mail_loot_template` SET `item` = 91208 WHERE `item` = 22781;
UPDATE `quest_template` SET `RewItemId1` = 91208 WHERE `RewItemId1` = 22781;
UPDATE `quest_template` SET `RewItemId2` = 91208 WHERE `RewItemId2` = 22781;
UPDATE `quest_template` SET `RewItemId3` = 91208 WHERE `RewItemId3` = 22781;
UPDATE `quest_template` SET `RewItemId4` = 91208 WHERE `RewItemId4` = 22781;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91208 WHERE `RewChoiceItemId1` = 22781;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91208 WHERE `RewChoiceItemId2` = 22781;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91208 WHERE `RewChoiceItemId3` = 22781;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91208 WHERE `RewChoiceItemId4` = 22781;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91208 WHERE `RewChoiceItemId5` = 22781;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91208 WHERE `RewChoiceItemId6` = 22781;
UPDATE `quest_template` SET `SrcItemId` = 91208 WHERE `SrcItemId` = 22781;
UPDATE `spell_template` SET `effectItemType1` = 91208 WHERE `effectItemType1` = 22781;
UPDATE `spell_template` SET `effectItemType2` = 91208 WHERE `effectItemType2` = 22781;
UPDATE `spell_template` SET `effectItemType3` = 91208 WHERE `effectItemType3` = 22781;
-- 23002 Turtle Box -> 91209 (species 209)
UPDATE IGNORE `npc_vendor` SET `item` = 91209 WHERE `item` = 23002;
UPDATE IGNORE `creature_loot_template` SET `item` = 91209 WHERE `item` = 23002;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91209 WHERE `item` = 23002;
UPDATE IGNORE `item_loot_template` SET `item` = 91209 WHERE `item` = 23002;
UPDATE IGNORE `reference_loot_template` SET `item` = 91209 WHERE `item` = 23002;
UPDATE IGNORE `mail_loot_template` SET `item` = 91209 WHERE `item` = 23002;
UPDATE `quest_template` SET `RewItemId1` = 91209 WHERE `RewItemId1` = 23002;
UPDATE `quest_template` SET `RewItemId2` = 91209 WHERE `RewItemId2` = 23002;
UPDATE `quest_template` SET `RewItemId3` = 91209 WHERE `RewItemId3` = 23002;
UPDATE `quest_template` SET `RewItemId4` = 91209 WHERE `RewItemId4` = 23002;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91209 WHERE `RewChoiceItemId1` = 23002;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91209 WHERE `RewChoiceItemId2` = 23002;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91209 WHERE `RewChoiceItemId3` = 23002;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91209 WHERE `RewChoiceItemId4` = 23002;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91209 WHERE `RewChoiceItemId5` = 23002;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91209 WHERE `RewChoiceItemId6` = 23002;
UPDATE `quest_template` SET `SrcItemId` = 91209 WHERE `SrcItemId` = 23002;
UPDATE `spell_template` SET `effectItemType1` = 91209 WHERE `effectItemType1` = 23002;
UPDATE `spell_template` SET `effectItemType2` = 91209 WHERE `effectItemType2` = 23002;
UPDATE `spell_template` SET `effectItemType3` = 91209 WHERE `effectItemType3` = 23002;
-- 23007 Piglet's Collar -> 91210 (species 210)
UPDATE IGNORE `npc_vendor` SET `item` = 91210 WHERE `item` = 23007;
UPDATE IGNORE `creature_loot_template` SET `item` = 91210 WHERE `item` = 23007;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91210 WHERE `item` = 23007;
UPDATE IGNORE `item_loot_template` SET `item` = 91210 WHERE `item` = 23007;
UPDATE IGNORE `reference_loot_template` SET `item` = 91210 WHERE `item` = 23007;
UPDATE IGNORE `mail_loot_template` SET `item` = 91210 WHERE `item` = 23007;
UPDATE `quest_template` SET `RewItemId1` = 91210 WHERE `RewItemId1` = 23007;
UPDATE `quest_template` SET `RewItemId2` = 91210 WHERE `RewItemId2` = 23007;
UPDATE `quest_template` SET `RewItemId3` = 91210 WHERE `RewItemId3` = 23007;
UPDATE `quest_template` SET `RewItemId4` = 91210 WHERE `RewItemId4` = 23007;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91210 WHERE `RewChoiceItemId1` = 23007;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91210 WHERE `RewChoiceItemId2` = 23007;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91210 WHERE `RewChoiceItemId3` = 23007;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91210 WHERE `RewChoiceItemId4` = 23007;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91210 WHERE `RewChoiceItemId5` = 23007;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91210 WHERE `RewChoiceItemId6` = 23007;
UPDATE `quest_template` SET `SrcItemId` = 91210 WHERE `SrcItemId` = 23007;
UPDATE `spell_template` SET `effectItemType1` = 91210 WHERE `effectItemType1` = 23007;
UPDATE `spell_template` SET `effectItemType2` = 91210 WHERE `effectItemType2` = 23007;
UPDATE `spell_template` SET `effectItemType3` = 91210 WHERE `effectItemType3` = 23007;
-- 23015 Rat Cage -> 91211 (species 211)
UPDATE IGNORE `npc_vendor` SET `item` = 91211 WHERE `item` = 23015;
UPDATE IGNORE `creature_loot_template` SET `item` = 91211 WHERE `item` = 23015;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91211 WHERE `item` = 23015;
UPDATE IGNORE `item_loot_template` SET `item` = 91211 WHERE `item` = 23015;
UPDATE IGNORE `reference_loot_template` SET `item` = 91211 WHERE `item` = 23015;
UPDATE IGNORE `mail_loot_template` SET `item` = 91211 WHERE `item` = 23015;
UPDATE `quest_template` SET `RewItemId1` = 91211 WHERE `RewItemId1` = 23015;
UPDATE `quest_template` SET `RewItemId2` = 91211 WHERE `RewItemId2` = 23015;
UPDATE `quest_template` SET `RewItemId3` = 91211 WHERE `RewItemId3` = 23015;
UPDATE `quest_template` SET `RewItemId4` = 91211 WHERE `RewItemId4` = 23015;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91211 WHERE `RewChoiceItemId1` = 23015;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91211 WHERE `RewChoiceItemId2` = 23015;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91211 WHERE `RewChoiceItemId3` = 23015;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91211 WHERE `RewChoiceItemId4` = 23015;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91211 WHERE `RewChoiceItemId5` = 23015;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91211 WHERE `RewChoiceItemId6` = 23015;
UPDATE `quest_template` SET `SrcItemId` = 91211 WHERE `SrcItemId` = 23015;
UPDATE `spell_template` SET `effectItemType1` = 91211 WHERE `effectItemType1` = 23015;
UPDATE `spell_template` SET `effectItemType2` = 91211 WHERE `effectItemType2` = 23015;
UPDATE `spell_template` SET `effectItemType3` = 91211 WHERE `effectItemType3` = 23015;
-- 23083 Captured Flame -> 91212 (species 212)
UPDATE IGNORE `npc_vendor` SET `item` = 91212 WHERE `item` = 23083;
UPDATE IGNORE `creature_loot_template` SET `item` = 91212 WHERE `item` = 23083;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91212 WHERE `item` = 23083;
UPDATE IGNORE `item_loot_template` SET `item` = 91212 WHERE `item` = 23083;
UPDATE IGNORE `reference_loot_template` SET `item` = 91212 WHERE `item` = 23083;
UPDATE IGNORE `mail_loot_template` SET `item` = 91212 WHERE `item` = 23083;
UPDATE `quest_template` SET `RewItemId1` = 91212 WHERE `RewItemId1` = 23083;
UPDATE `quest_template` SET `RewItemId2` = 91212 WHERE `RewItemId2` = 23083;
UPDATE `quest_template` SET `RewItemId3` = 91212 WHERE `RewItemId3` = 23083;
UPDATE `quest_template` SET `RewItemId4` = 91212 WHERE `RewItemId4` = 23083;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91212 WHERE `RewChoiceItemId1` = 23083;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91212 WHERE `RewChoiceItemId2` = 23083;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91212 WHERE `RewChoiceItemId3` = 23083;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91212 WHERE `RewChoiceItemId4` = 23083;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91212 WHERE `RewChoiceItemId5` = 23083;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91212 WHERE `RewChoiceItemId6` = 23083;
UPDATE `quest_template` SET `SrcItemId` = 91212 WHERE `SrcItemId` = 23083;
UPDATE `spell_template` SET `effectItemType1` = 91212 WHERE `effectItemType1` = 23083;
UPDATE `spell_template` SET `effectItemType2` = 91212 WHERE `effectItemType2` = 23083;
UPDATE `spell_template` SET `effectItemType3` = 91212 WHERE `effectItemType3` = 23083;
-- 23712 White Tiger Cub -> 91213 (species 213)
UPDATE IGNORE `npc_vendor` SET `item` = 91213 WHERE `item` = 23712;
UPDATE IGNORE `creature_loot_template` SET `item` = 91213 WHERE `item` = 23712;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91213 WHERE `item` = 23712;
UPDATE IGNORE `item_loot_template` SET `item` = 91213 WHERE `item` = 23712;
UPDATE IGNORE `reference_loot_template` SET `item` = 91213 WHERE `item` = 23712;
UPDATE IGNORE `mail_loot_template` SET `item` = 91213 WHERE `item` = 23712;
UPDATE `quest_template` SET `RewItemId1` = 91213 WHERE `RewItemId1` = 23712;
UPDATE `quest_template` SET `RewItemId2` = 91213 WHERE `RewItemId2` = 23712;
UPDATE `quest_template` SET `RewItemId3` = 91213 WHERE `RewItemId3` = 23712;
UPDATE `quest_template` SET `RewItemId4` = 91213 WHERE `RewItemId4` = 23712;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91213 WHERE `RewChoiceItemId1` = 23712;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91213 WHERE `RewChoiceItemId2` = 23712;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91213 WHERE `RewChoiceItemId3` = 23712;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91213 WHERE `RewChoiceItemId4` = 23712;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91213 WHERE `RewChoiceItemId5` = 23712;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91213 WHERE `RewChoiceItemId6` = 23712;
UPDATE `quest_template` SET `SrcItemId` = 91213 WHERE `SrcItemId` = 23712;
UPDATE `spell_template` SET `effectItemType1` = 91213 WHERE `effectItemType1` = 23712;
UPDATE `spell_template` SET `effectItemType2` = 91213 WHERE `effectItemType2` = 23712;
UPDATE `spell_template` SET `effectItemType3` = 91213 WHERE `effectItemType3` = 23712;
-- 23713 Hippogryph Hatchling -> 91214 (species 214)
UPDATE IGNORE `npc_vendor` SET `item` = 91214 WHERE `item` = 23713;
UPDATE IGNORE `creature_loot_template` SET `item` = 91214 WHERE `item` = 23713;
UPDATE IGNORE `gameobject_loot_template` SET `item` = 91214 WHERE `item` = 23713;
UPDATE IGNORE `item_loot_template` SET `item` = 91214 WHERE `item` = 23713;
UPDATE IGNORE `reference_loot_template` SET `item` = 91214 WHERE `item` = 23713;
UPDATE IGNORE `mail_loot_template` SET `item` = 91214 WHERE `item` = 23713;
UPDATE `quest_template` SET `RewItemId1` = 91214 WHERE `RewItemId1` = 23713;
UPDATE `quest_template` SET `RewItemId2` = 91214 WHERE `RewItemId2` = 23713;
UPDATE `quest_template` SET `RewItemId3` = 91214 WHERE `RewItemId3` = 23713;
UPDATE `quest_template` SET `RewItemId4` = 91214 WHERE `RewItemId4` = 23713;
UPDATE `quest_template` SET `RewChoiceItemId1` = 91214 WHERE `RewChoiceItemId1` = 23713;
UPDATE `quest_template` SET `RewChoiceItemId2` = 91214 WHERE `RewChoiceItemId2` = 23713;
UPDATE `quest_template` SET `RewChoiceItemId3` = 91214 WHERE `RewChoiceItemId3` = 23713;
UPDATE `quest_template` SET `RewChoiceItemId4` = 91214 WHERE `RewChoiceItemId4` = 23713;
UPDATE `quest_template` SET `RewChoiceItemId5` = 91214 WHERE `RewChoiceItemId5` = 23713;
UPDATE `quest_template` SET `RewChoiceItemId6` = 91214 WHERE `RewChoiceItemId6` = 23713;
UPDATE `quest_template` SET `SrcItemId` = 91214 WHERE `SrcItemId` = 23713;
UPDATE `spell_template` SET `effectItemType1` = 91214 WHERE `effectItemType1` = 23713;
UPDATE `spell_template` SET `effectItemType2` = 91214 WHERE `effectItemType2` = 23713;
UPDATE `spell_template` SET `effectItemType3` = 91214 WHERE `effectItemType3` = 23713;
-- A source that already had the companion item keeps one row (the IGNOREs above): drop the rest.
DELETE FROM `npc_vendor` WHERE `item` IN (4401, 8485, 8486, 8487, 8488, 8489, 8490, 8491, 8492, 8494, 8495, 8496, 8497, 8498, 8499, 8500, 8501, 10360, 10361, 10392, 10393, 10394, 10398, 10822, 11023, 11026, 11027, 11110, 11474, 11825, 11826, 11903, 12264, 12529, 13582, 13583, 13584, 15996, 18963, 18964, 18965, 18966, 18967, 19054, 19055, 19450, 20371, 20651, 20769, 21168, 21277, 22114, 22235, 22780, 22781, 23002, 23007, 23015, 23083, 23712, 23713);
DELETE FROM `creature_loot_template` WHERE `item` IN (4401, 8485, 8486, 8487, 8488, 8489, 8490, 8491, 8492, 8494, 8495, 8496, 8497, 8498, 8499, 8500, 8501, 10360, 10361, 10392, 10393, 10394, 10398, 10822, 11023, 11026, 11027, 11110, 11474, 11825, 11826, 11903, 12264, 12529, 13582, 13583, 13584, 15996, 18963, 18964, 18965, 18966, 18967, 19054, 19055, 19450, 20371, 20651, 20769, 21168, 21277, 22114, 22235, 22780, 22781, 23002, 23007, 23015, 23083, 23712, 23713);
DELETE FROM `gameobject_loot_template` WHERE `item` IN (4401, 8485, 8486, 8487, 8488, 8489, 8490, 8491, 8492, 8494, 8495, 8496, 8497, 8498, 8499, 8500, 8501, 10360, 10361, 10392, 10393, 10394, 10398, 10822, 11023, 11026, 11027, 11110, 11474, 11825, 11826, 11903, 12264, 12529, 13582, 13583, 13584, 15996, 18963, 18964, 18965, 18966, 18967, 19054, 19055, 19450, 20371, 20651, 20769, 21168, 21277, 22114, 22235, 22780, 22781, 23002, 23007, 23015, 23083, 23712, 23713);
DELETE FROM `item_loot_template` WHERE `item` IN (4401, 8485, 8486, 8487, 8488, 8489, 8490, 8491, 8492, 8494, 8495, 8496, 8497, 8498, 8499, 8500, 8501, 10360, 10361, 10392, 10393, 10394, 10398, 10822, 11023, 11026, 11027, 11110, 11474, 11825, 11826, 11903, 12264, 12529, 13582, 13583, 13584, 15996, 18963, 18964, 18965, 18966, 18967, 19054, 19055, 19450, 20371, 20651, 20769, 21168, 21277, 22114, 22235, 22780, 22781, 23002, 23007, 23015, 23083, 23712, 23713);
DELETE FROM `reference_loot_template` WHERE `item` IN (4401, 8485, 8486, 8487, 8488, 8489, 8490, 8491, 8492, 8494, 8495, 8496, 8497, 8498, 8499, 8500, 8501, 10360, 10361, 10392, 10393, 10394, 10398, 10822, 11023, 11026, 11027, 11110, 11474, 11825, 11826, 11903, 12264, 12529, 13582, 13583, 13584, 15996, 18963, 18964, 18965, 18966, 18967, 19054, 19055, 19450, 20371, 20651, 20769, 21168, 21277, 22114, 22235, 22780, 22781, 23002, 23007, 23015, 23083, 23712, 23713);
DELETE FROM `mail_loot_template` WHERE `item` IN (4401, 8485, 8486, 8487, 8488, 8489, 8490, 8491, 8492, 8494, 8495, 8496, 8497, 8498, 8499, 8500, 8501, 10360, 10361, 10392, 10393, 10394, 10398, 10822, 11023, 11026, 11027, 11110, 11474, 11825, 11826, 11903, 12264, 12529, 13582, 13583, 13584, 15996, 18963, 18964, 18965, 18966, 18967, 19054, 19055, 19450, 20371, 20651, 20769, 21168, 21277, 22114, 22235, 22780, 22781, 23002, 23007, 23015, 23083, 23712, 23713);
-- The retired items say so, should a game master ever create one.
UPDATE `item_template` SET `description` = 'Retired: the server swaps this for its battle companion.' WHERE `entry` IN (4401, 8485, 8486, 8487, 8488, 8489, 8490, 8491, 8492, 8494, 8495, 8496, 8497, 8498, 8499, 8500, 8501, 10360, 10361, 10392, 10393, 10394, 10398, 10822, 11023, 11026, 11027, 11110, 11474, 11825, 11826, 11903, 12264, 12529, 13582, 13583, 13584, 15996, 18963, 18964, 18965, 18966, 18967, 19054, 19055, 19450, 20371, 20651, 20769, 21168, 21277, 22114, 22235, 22780, 22781, 23002, 23007, 23015, 23083, 23712, 23713);
