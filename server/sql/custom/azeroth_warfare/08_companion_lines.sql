-- World of Modern Warfare: the companion catalog's new lines (design/COMPANION_CATALOG.md, package W5).
-- Apply to the world database after 02_companions.sql (and again whenever 02 is applied, as 02
-- drops and rebuilds fusion_companion_species). Safe to run again.
--
--  - ride_display: a line with no vanilla mount of its own rides on the family-neutral wolf spells
--    (580 / 23250, for the speed and vanilla's mounted rules) and the server shows the stage-3
--    creature's display (UNIT_FIELD_MOUNTDISPLAYID). 0 = the mount spell's own display.
--  - The starter set (species 1-33) is rescaled by the catalog's size rule (section 1b).
--  - Species 101-164 and their items 91101-91164; catalysts 90105-90107 (undead, mechanicals,
--    humanoids). Mirrors Companions::StarterSpecies() row for row (CompanionTests reads this file).

ALTER TABLE `fusion_companion_species`
  ADD COLUMN IF NOT EXISTS `ride_display` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'display shown while ridden; 0 = the mount spell''s own';

-- ── Rescale of the starter set (catalog section 1b) ────────────────────────────────────────────
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.9, `stage2_scale` = 1.25, `stage3_scale` = 0.4 WHERE `id` = 1;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 1.15, `stage3_scale` = 0.4 WHERE `id` = 2;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 0.85, `stage3_scale` = 1.35 WHERE `id` = 3;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 1, `stage3_scale` = 1.25 WHERE `id` = 4;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 0.85 WHERE `id` = 5;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.95, `stage2_scale` = 0.85, `stage3_scale` = 1 WHERE `id` = 6;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.6, `stage2_scale` = 1.2, `stage3_scale` = 1 WHERE `id` = 7;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.85, `stage2_scale` = 0.8, `stage3_scale` = 1 WHERE `id` = 8;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.5, `stage2_scale` = 1, `stage3_scale` = 1.5 WHERE `id` = 9;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.8, `stage2_scale` = 1, `stage3_scale` = 1.7 WHERE `id` = 10;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.6, `stage2_scale` = 0.8, `stage3_scale` = 0.85 WHERE `id` = 11;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.65, `stage2_scale` = 1.2, `stage3_scale` = 1 WHERE `id` = 12;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 0.8 WHERE `id` = 13;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.75, `stage2_scale` = 0.85, `stage3_scale` = 1.65 WHERE `id` = 14;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 0.8 WHERE `id` = 15;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 1.15 WHERE `id` = 16;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.6, `stage2_scale` = 1.2, `stage3_scale` = 2.05 WHERE `id` = 17;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.6, `stage2_scale` = 1, `stage3_scale` = 1.7 WHERE `id` = 18;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 0.8, `stage3_scale` = 2.25 WHERE `id` = 19;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.5, `stage2_scale` = 1, `stage3_scale` = 2 WHERE `id` = 20;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.35, `stage2_scale` = 1, `stage3_scale` = 1.25 WHERE `id` = 21;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.5, `stage2_scale` = 1.25, `stage3_scale` = 1.4 WHERE `id` = 22;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.85, `stage2_scale` = 1.25, `stage3_scale` = 1.05 WHERE `id` = 23;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.3, `stage2_scale` = 1, `stage3_scale` = 1.65 WHERE `id` = 24;
UPDATE `fusion_companion_species` SET `stage1_scale` = 1, `stage2_scale` = 1.25, `stage3_scale` = 0.4 WHERE `id` = 25;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.2, `stage2_scale` = 0.35, `stage3_scale` = 1 WHERE `id` = 29;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.4, `stage2_scale` = 1 WHERE `id` = 30;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.4, `stage2_scale` = 1 WHERE `id` = 31;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.4, `stage2_scale` = 1 WHERE `id` = 32;
UPDATE `fusion_companion_species` SET `stage1_scale` = 0.4, `stage2_scale` = 1 WHERE `id` = 33;

-- ── Species 101-164 ────────────────────────────────────────────────────────────────────────────
DELETE FROM `fusion_companion_species` WHERE `id` BETWEEN 100 AND 255;
INSERT INTO `fusion_companion_species` (`id`, `name`, `item_entry`, `family`, `role`,
  `stage1_creature`, `stage1_scale`, `stage2_creature`, `stage2_scale`, `stage3_creature`, `stage3_scale`,
  `evo1_level`, `evo1_item`, `evo1_item_count`, `evo1_areas`, `evo1_condition`, `evo1_condition_type`, `evo1_condition_count`,
  `evo2_level`, `evo2_item`, `evo2_item_count`, `evo2_areas`, `evo2_condition`, `evo2_condition_type`, `evo2_condition_count`,
  `drop_entries`, `drop_type`, `drop_family`, `ride_stage`, `ride_spell`, `ride_swift_spell`, `ride_display`) VALUES
(101, 'Webwood Spiderling', 91101, 6, 0, 1986, 0.7, 442, 1.25, 769, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '141,148,10,331', 0, 0, 0, '1986,442,769', 1, 3, 3, 580, 23250, 336),
(102, 'Darkmist Hatchling', 91102, 6, 0, 4377, 0.55, 4378, 0.8, 4380, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '15', 0, 0, 0, '4377,4378,4380,4376,4379,4411,4412,4413,4414,4415', 0, 0, 3, 580, 23250, 2537),
(103, 'Cinderweb Spiderling', 91103, 6, 0, 5856, 0.4, 5857, 0.85, 5858, 1, 45, 0, 0, '', 0, 0, 0, 55, 90102, 1, '51', 0, 0, 0, '5856,5857,5858', 0, 0, 3, 580, 23250, 7510),
(104, 'Black Bear Cub', 91104, 5, 2, 1128, 0.65, 1186, 0.9, 1189, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '1,38,267', 0, 0, 0, '1128,1186,1189', 1, 4, 3, 580, 23250, 762),
(105, 'Ironfur Cub', 91105, 5, 2, 1196, 0.6, 5268, 0.9, 5272, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '357,618', 0, 0, 0, '1196,5268,5272,5274,7443,7444', 0, 0, 3, 580, 23250, 8838),
(106, 'Stranglethorn Kitten', 91106, 4, 0, 681, 0.45, 682, 0.9, 1085, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '33', 0, 0, 0, '681,682,1085', 0, 0, 3, 580, 23250, 698),
(107, 'Mountain Lion Cub', 91107, 4, 0, 2384, 0.5, 2406, 0.85, 2407, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '267,36,45,400', 0, 0, 0, '2384,2406,2407,2385,2961,3035,4124', 0, 0, 3, 580, 23250, 2300),
(108, 'Savannah Cub', 91108, 4, 0, 3425, 0.4, 3241, 0.7, 5828, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '17', 0, 0, 0, '3425,3241,5828,3243,3415,3416,5766', 0, 0, 3, 580, 23250, 4424),
(109, 'Bloodsnout Pup', 91109, 3, 0, 1765, 0.55, 1923, 1, 9697, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '130,44,46', 0, 0, 0, '1765,1923,9697,1766,3861,3862,5058,7055,9416,9690,9694,9696,10221', 0, 0, 3, 580, 23250, 9370),
(110, 'Silvermane Pup', 91110, 3, 0, 2923, 0.5, 2924, 1, 2926, 1, 45, 0, 0, '', 0, 0, 0, 55, 90102, 1, '47', 0, 0, 0, '2923,2924,2926,2925', 0, 0, 3, 580, 23250, 11418),
(111, 'Timber Wolf Pup', 91111, 3, 0, 834, 0.5, 833, 1, 2729, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '40,3', 0, 0, 0, '834,833,2729,2727,2728,2730', 0, 0, 3, 580, 23250, 1164),
(112, 'Sunscale Hatchling', 91112, 7, 0, 3254, 0.45, 3255, 0.9, 3256, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '17', 0, 0, 0, '3254,3255,3256', 0, 0, 3, 580, 23250, 4442),
(113, 'Deviate Hatchling', 91113, 7, 0, 3634, 0.45, 3633, 0.9, 3637, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '718,17', 0, 0, 0, '3634,3633,3637,3636', 0, 0, 3, 580, 23250, 755),
(114, 'Bloodfen Hatchling', 91114, 7, 0, 4351, 0.4, 4355, 0.8, 4357, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '15', 0, 0, 0, '4351,4355,4357,4352,4356', 0, 0, 3, 580, 23250, 2573),
(115, 'Venomtail Scorpling', 91115, 13, 0, 3125, 0.45, 3127, 0.9, 4139, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '14,17,3', 0, 0, 0, '3125,3127,4139', 1, 20, 3, 580, 23250, 2491),
(116, 'Stonelash Scorpling', 91116, 13, 0, 7078, 1, 11735, 0.8, 11737, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '440,1377', 0, 0, 0, '7078,11735,11737,11736', 0, 0, 3, 580, 23250, 15385),
(117, 'Drywallow Hatchling', 91117, 8, 2, 4341, 0.4, 4343, 0.8, 4345, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '15,33', 0, 0, 0, '4341,4343,4345', 1, 6, 3, 580, 23250, 807),
(118, 'Snapjaw Hatchling', 91118, 30, 2, 3461, 1, 2408, 1.2, 2505, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '17,267,440', 0, 0, 0, '3461,2408,2505', 1, 21, 3, 580, 23250, 5027),
(119, 'Surf Glider Hatchling', 91119, 30, 2, 4142, 0.55, 5431, 1, 5432, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '400,440', 0, 0, 0, '4142,5431,5432,4143,4144', 0, 0, 3, 580, 23250, 5127),
(120, 'Groddoc Infant', 91120, 9, 2, 5260, 0.5, 5262, 0.9, 6513, 1.55, 45, 0, 0, '', 0, 0, 0, 55, 90102, 1, '357,490', 0, 0, 0, '5260,5262,6513', 1, 9, 0, 0, 0, 0),
(121, 'Goretusk Piglet', 91121, 28, 2, 454, 0.45, 157, 0.8, 547, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '40,44,491', 0, 0, 0, '454,157,547', 1, 5, 3, 580, 23250, 3035),
(122, 'Pygmy Crawler', 91122, 29, 2, 3106, 0.6, 2235, 1, 1088, 1.15, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '14,40,11,148,267,33', 0, 0, 0, '3106,2235,1088', 1, 8, 0, 0, 0, 0),
(123, 'Cloud Serpent Hatchling', 91123, 31, 1, 3247, 0.65, 4117, 1, 4119, 0.95, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '400,406,357', 0, 0, 0, '3247,4117,4119', 1, 27, 0, 0, 0, 0),
(124, 'Swoop Fledgling', 91124, 32, 0, 2969, 0.65, 2970, 1.25, 2971, 1.35, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '215,17,3', 0, 0, 0, '2969,2970,2971', 1, 7, 0, 0, 0, 0),
(125, 'Roc Chick', 91125, 32, 1, 5428, 0.5, 5429, 1, 5430, 0.9, 45, 0, 0, '', 0, 0, 0, 55, 90102, 1, '440,51', 0, 0, 0, '5428,5429,5430', 0, 0, 0, 0, 0, 0),
(126, 'Hecklefang Pup', 91126, 27, 0, 4127, 0.55, 4129, 1.15, 4128, 1.7, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '17,400,405,440', 0, 0, 0, '4127,4129,4128', 1, 25, 0, 0, 0, 0),
(127, 'Strigid Owlet', 91127, 10, 0, 1995, 0.5, 1996, 1, 1997, 1.15, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '141,361,618', 0, 0, 0, '1995,1996,1997', 0, 0, 0, 0, 0, 0),
(128, 'Kraul Batling', 91128, 11, 0, 1554, 1, 4538, 1, 4539, 1.3, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '85,491,1337', 0, 0, 0, '1554,4538,4539,1512,1513,1553', 0, 0, 0, 0, 0, 0),
(129, 'Foreststrider Fledgling', 91129, 12, 0, 2321, 0.5, 2322, 1, 2323, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '148', 0, 0, 0, '2321,2322,2323', 0, 0, 3, 580, 23250, 1283),
(130, 'Aged Kodo Calf', 91130, 22, 0, 3234, 0.45, 4700, 0.85, 4702, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '17,405', 0, 0, 0, '3234,4700,4702,4701', 0, 0, 3, 580, 23250, 2767),
(131, 'Gritjaw Hatchling', 91131, 33, 0, 4044, 0.6, 4728, 0.9, 4729, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '3,33,406,4,440', 0, 0, 0, '4044,4728,4729,688,689,690,1550,1551,4041,4042,4147,4863,5419,5990,11785', 0, 0, 3, 580, 23250, 7345),
(132, 'Glasshide Hatchling', 91132, 33, 0, 4147, 0.6, 5419, 1, 5421, 1, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '400,440', 0, 0, 0, '4147,5419,5421,4150,4151,5420', 0, 0, 3, 580, 23250, 12339),
(133, 'Thresher Fry', 91133, 34, 2, 1224, 0.55, 2187, 1.15, 4390, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '148,11,15', 0, 0, 0, '1224,2187,4390,2185,2188,4388,4389,4827,14357', 0, 0, 0, 0, 0, 0),
(134, 'Mystlash Hydraling', 91134, 35, 1, 3721, 0.4, 3721, 1, 4374, 0.8, 20, 0, 0, '', 0, 0, 0, 40, 90102, 1, '331,16', 0, 0, 0, '3721,4374', 0, 0, 0, 0, 0, 0),
(135, 'Timberling Sprout', 91135, 36, 0, 2022, 0.65, 1955, 1.15, 764, 1.6, 20, 0, 0, '', 0, 0, 0, 40, 90103, 1, '141,11,8', 0, 0, 0, '2022,1955,764,765,1040,1956,2025,2027,2029,4382', 0, 0, 0, 0, 0, 0),
(136, 'Toxic Mote', 91136, 56, 1, 5897, 0.8, 6220, 1.15, 8519, 1.5, 45, 0, 0, '', 0, 0, 0, 55, 90103, 1, '17,721,28,139', 0, 0, 0, '5897,6220,8519,7132', 0, 0, 0, 0, 0, 0),
(137, 'Cracked Golemling', 91137, 16, 2, 2156, 0.55, 2723, 1, 5854, 1.4, 20, 0, 0, '', 0, 0, 0, 40, 90103, 1, '36,3,46', 0, 0, 0, '2156,2723,5854,2749,2751,5853,8279,8906,8908', 0, 0, 0, 0, 0, 0),
(138, 'Ectoplasm Blob', 91138, 20, 0, 3638, 0.75, 1030, 1.25, 1033, 1.05, 20, 0, 0, '', 0, 0, 0, 40, 0, 0, '11,718', 0, 0, 0, '3638,1030,1033,1032,3640', 0, 0, 0, 0, 0, 0),
(139, 'Creeping Sludgeling', 91139, 20, 0, 12222, 1, 2655, 1.25, 2656, 1.2, 45, 0, 0, '', 0, 0, 0, 55, 0, 0, '2100,357,490', 0, 0, 0, '12222,2655,2656', 0, 0, 0, 0, 0, 0),
(140, 'Black Drake Hatchling', 91140, 1, 1, 4323, 0.45, 7048, 0.8, 7045, 1, 45, 0, 0, '', 0, 0, 0, 55, 90101, 1, '51,46,1583', 0, 0, 0, '4323,7048,7045,2725,4324', 0, 0, 3, 580, 23250, 9585),
(141, 'Imp Familiar', 91141, 37, 1, 416, 1, 9776, 1, 9778, 1.45, 45, 0, 0, '', 0, 0, 0, 55, 90104, 1, '46,361', 0, 0, 0, '416,9776,9778,9777,13276', 0, 0, 0, 0, 0, 0),
(142, 'Felstalker Pup', 91142, 38, 0, 3102, 1, 6071, 1.2, 4685, 1, 20, 0, 0, '', 0, 0, 0, 40, 90104, 1, '14,4,2557', 0, 0, 0, '3102,6071,4685,6010', 0, 0, 3, 580, 23250, 6173),
(143, 'Doomguard Spawn', 91143, 39, 2, 3772, 0.5, 6011, 0.9, 12396, 0.85, 20, 0, 0, '', 0, 0, 0, 40, 90104, 1, '331,361,4,16', 0, 0, 0, '3772,6011,12396,8717', 0, 0, 0, 0, 0, 0),
(144, 'Darkhound Whelp', 91144, 57, 0, 1547, 0.55, 1548, 1.1, 8598, 1.3, 20, 0, 0, '', 0, 0, 0, 40, 90104, 1, '85,130,139', 0, 0, 0, '1547,1548,8598,1549,8596,8597', 0, 0, 0, 0, 0, 0),
(145, 'Rattlecage Bones', 91145, 40, 2, 1890, 0.45, 1520, 1.25, 1788, 1.2, 20, 0, 0, '', 0, 0, 0, 40, 90105, 1, '85,10,28,139', 0, 0, 0, '1890,1520,1788,48,531,7343,10478,10816,11258', 0, 0, 0, 0, 0, 0),
(146, 'Ghoul Thrall', 91146, 41, 2, 1866, 0.45, 1791, 0.85, 8543, 0.6, 20, 0, 0, '', 0, 0, 0, 40, 90105, 1, '130,28,139', 0, 0, 0, '1866,1791,8543,511,846,1793,1794,7347,7348,8530,8531,8545,10405,10406,10407,10495,10801,16244,16447', 0, 0, 0, 0, 0, 0),
(147, 'Wailing Shade', 91147, 42, 1, 1534, 0.55, 6116, 1, 8541, 1.3, 20, 0, 0, '', 0, 0, 0, 40, 90105, 1, '85,16,28,139', 0, 0, 0, '1534,6116,8541,2178,3877,10463,10464,11466,12377', 0, 0, 0, 0, 0, 0),
(148, 'Crypt Scuttler', 91148, 58, 0, 8556, 0.4, 8555, 0.8, 8558, 1, 45, 0, 0, '', 0, 0, 0, 55, 90105, 1, '139,2017', 0, 0, 0, '8556,8555,8558,8557,10412,10413,15978', 0, 0, 3, 580, 23250, 9758),
(149, 'Servo', 91149, 21, 2, 2922, 0.9, 36, 1, 115, 1.25, 20, 0, 0, '', 0, 0, 0, 40, 90106, 1, '40,721', 0, 0, 0, '2922,36,115,114,480,2676', 0, 0, 0, 0, 0, 0),
(150, 'Compact Reaper', 91150, 21, 2, 2676, 1, 11684, 1.05, 4260, 1, 20, 0, 0, '', 0, 0, 0, 40, 90106, 1, '406,331,33', 0, 0, 0, '2676,11684,4260,115,573,642', 0, 0, 3, 580, 23250, 1269),
(151, 'Mechano-Tankling', 91151, 21, 1, 6225, 0.35, 6226, 0.8, 6227, 1, 20, 0, 0, '', 0, 0, 0, 40, 90106, 1, '721', 0, 0, 0, '6225,6226,6227', 0, 0, 3, 580, 23250, 6891),
(152, 'Riverpaw Runt', 91152, 43, 0, 97, 0.55, 124, 1, 445, 1.35, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '40,44,10,267,357', 0, 0, 0, '97,124,445,98,117,123,125,423,424,426,430,446,452,478,500,580,712,1007,1008,1009,1010,1011,1012,1013,1014,1426,5249,5254', 0, 0, 0, 0, 0, 0),
(153, 'Kobold Vermin', 91153, 44, 0, 6, 0.75, 475, 1.25, 2574, 1.25, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '12,40,45,400', 0, 0, 0, '6,475,2574,40,80,257,476,1172,1174,1176,1177,1236,2572,2573', 0, 0, 0, 0, 0, 0),
(154, 'Rockjaw Trogg', 91154, 45, 2, 707, 0.75, 1163, 1, 4855, 0.85, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '1,38,3,1337', 0, 0, 0, '707,1163,4855,724,1115,1116,1117,1118,1161,1164,1165,1718,2892,2893,2894,4850,4851,4852,4853,4856,6733,7175,7320,7321,10987,11318', 0, 0, 0, 0, 0, 0),
(155, 'Ogre Runt', 91155, 46, 2, 1178, 0.5, 2566, 1, 5241, 0.85, 45, 0, 0, '', 0, 0, 0, 55, 90107, 1, '10,45,36,357,46', 0, 0, 0, '1178,2566,5241,212,889,891,892,1180,1251,1487,2562,2564,2567,2569,2570,2571,5229,5232,5234,5236,5240', 0, 0, 0, 0, 0, 0),
(156, 'Kolkar Drudge', 91156, 47, 1, 3119, 0.55, 4093, 1, 5601, 1.15, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '17,400,405', 0, 0, 0, '3119,4093,5601,2967,2968,3120,3272,3273,3274,3275,3397,4095,4632,4634,4635,4638,4639,4640,4642,4643,4644,4646,4648,4651,4652,7404,10617,11559', 0, 0, 0, 0, 0, 0),
(157, 'Bristleback Piglet', 91157, 48, 1, 2952, 0.55, 3266, 0.8, 4436, 1, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '215,17,491', 0, 0, 0, '2952,3266,4436,2953,3111,3112,3113,3114,3232,3258,3260,3261,3263,3265,3267,3268,3269,3271,3456,3457,3458,3459,4435,4437,4438,4440,4442,4520,4522,4523,4525,4530,4531,4532,6035,6132,7873,7874', 0, 0, 0, 0, 0, 0),
(158, 'Gnarlpine Cub', 91158, 49, 0, 2006, 0.6, 3921, 1, 7440, 0.8, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '141,148,331,361,618', 0, 0, 0, '2006,3921,7440,2007,2008,2009,2011,2012,2014,2152,2167,2168,2169,2170,2171,2324,3743,3745,3746,3922,3923,3924,3925,3926,7153,7154,7155,7156,7235,7438,7439,7442,10916', 0, 0, 0, 0, 0, 0),
(159, 'Windfury Fledgling', 91159, 50, 1, 2962, 0.55, 2964, 1.15, 2965, 1.5, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '215,17,406,405,357', 0, 0, 0, '2962,2964,2965,2963,3276,3277,3278,3279,3280,4022,4023,12579', 0, 0, 0, 0, 0, 0),
(160, 'Troll Whelp', 91160, 51, 1, 706, 0.55, 597, 1, 9268, 1.1, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '1,33,47,1583', 0, 0, 0, '706,597,9268,587,588,667,669,672,699,702,781,783,946,1120,1121,1122,1123,1124,1397,2552,2553,2554,2555,2556,2557,2558,2639,2640,2641,2642,2643,2644,2645,2646,2647,2648,2649,2650,2651,2652,2653,2654,4465,4466,4467,9239,9240,9241,9265,9266,9267,9269', 0, 0, 0, 0, 0, 0),
(161, 'Stormscale Wave Rider', 91161, 52, 2, 2179, 0.55, 3711, 1, 4366, 1.1, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '148,331,16', 0, 0, 0, '2179,3711,4366,2180,2181,2182,2183,3712,3713,3715,4364,4368,4370,4371,4711,4712,4713,4714,4715,4716,4718,4719,6190,6194,6195,7885', 0, 0, 0, 0, 0, 0),
(162, 'Makrura Clacker', 91162, 53, 2, 3103, 0.7, 4401, 1, 6350, 1, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '14,15,16', 0, 0, 0, '3103,4401,6350,3104,3105,3141,4402,4403,4404,6370,6372', 0, 0, 0, 0, 0, 0),
(163, 'Young Moonkin', 91163, 54, 1, 10159, 0.5, 10158, 1, 7452, 1.5, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '148,47,618', 0, 0, 0, '10159,10158,7452,2927,2928,2929,7450,7451,10157,10160', 0, 0, 0, 0, 0, 0),
(164, 'Moonrage Whelp', 91164, 55, 0, 1769, 0.6, 1782, 0.9, 920, 1.2, 20, 0, 0, '', 0, 0, 0, 40, 90107, 1, '130,10,209', 0, 0, 0, '1769,1782,920,205,206,533,898,1770,1779,1924', 0, 0, 0, 0, 0, 0);

-- ── Items ──────────────────────────────────────────────────────────────────────────────────────
-- As 02's: class 15, quality 3, Bind on Equip, one per stack, no spell (the fusion hook catches
-- the use). Catalysts as 02's: stackable, tradeable, uncommon.
DELETE FROM `item_template` WHERE `entry` BETWEEN 91100 AND 91999;
DELETE FROM `item_template` WHERE `entry` BETWEEN 90105 AND 90107;
INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`,
  `quality`, `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `stackable`, `max_count`, `item_level`, `bonding`) VALUES
(91101, 0, 15, 0, 'Webwood Egg Sac', 'Battle companion: Webwood Spiderling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 22717, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91102, 0, 15, 0, 'Darkmist Egg Sac', 'Battle companion: Darkmist Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 22717, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91103, 0, 15, 0, 'Cinderweb Egg Sac', 'Battle companion: Cinderweb Spiderling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 22717, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91104, 0, 15, 0, 'Black Bear Cub Crate', 'Battle companion: Black Bear Cub. Grows into a rideable mount. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91105, 0, 15, 0, 'Ironfur Cub Crate', 'Battle companion: Ironfur Cub. Grows into a rideable mount. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91106, 0, 15, 0, 'Stranglethorn Kitten Carrier', 'Battle companion: Stranglethorn Kitten. Grows into a rideable mount. Its level, traits and experience travel with this item.', 20629, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91107, 0, 15, 0, 'Mountain Lion Cub Carrier', 'Battle companion: Mountain Lion Cub. Grows into a rideable mount. Its level, traits and experience travel with this item.', 20629, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91108, 0, 15, 0, 'Savannah Cub Carrier', 'Battle companion: Savannah Cub. Grows into a rideable mount. Its level, traits and experience travel with this item.', 20629, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91109, 0, 15, 0, 'Bloodsnout Pup Carrier', 'Battle companion: Bloodsnout Pup. Grows into a rideable mount. Its level, traits and experience travel with this item.', 20629, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91110, 0, 15, 0, 'Silvermane Pup Carrier', 'Battle companion: Silvermane Pup. Grows into a rideable mount. Its level, traits and experience travel with this item.', 20629, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91111, 0, 15, 0, 'Timber Wolf Pup Carrier', 'Battle companion: Timber Wolf Pup. Grows into a rideable mount. Its level, traits and experience travel with this item.', 20629, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91112, 0, 15, 0, 'Sunscale Raptor Egg', 'Battle companion: Sunscale Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91113, 0, 15, 0, 'Deviate Raptor Egg', 'Battle companion: Deviate Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91114, 0, 15, 0, 'Bloodfen Raptor Egg', 'Battle companion: Bloodfen Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91115, 0, 15, 0, 'Venomtail Egg Cluster', 'Battle companion: Venomtail Scorpling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91116, 0, 15, 0, 'Stonelash Egg Cluster', 'Battle companion: Stonelash Scorpling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91117, 0, 15, 0, 'Drywallow Egg', 'Battle companion: Drywallow Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91118, 0, 15, 0, 'Snapjaw Egg', 'Battle companion: Snapjaw Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91119, 0, 15, 0, 'Surf Glider Egg', 'Battle companion: Surf Glider Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91120, 0, 15, 0, 'Groddoc Infant Crate', 'Battle companion: Groddoc Infant. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91121, 0, 15, 0, 'Goretusk Piglet Crate', 'Battle companion: Goretusk Piglet. Grows into a rideable mount. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91122, 0, 15, 0, 'Pygmy Crawler Shell', 'Battle companion: Pygmy Crawler. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91123, 0, 15, 0, 'Cloud Serpent Egg', 'Battle companion: Cloud Serpent Hatchling. Its level, traits and experience travel with this item.', 18046, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91124, 0, 15, 0, 'Swoop Egg', 'Battle companion: Swoop Fledgling. Its level, traits and experience travel with this item.', 18046, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91125, 0, 15, 0, 'Roc Egg', 'Battle companion: Roc Chick. Its level, traits and experience travel with this item.', 18046, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91126, 0, 15, 0, 'Hecklefang Pup Carrier', 'Battle companion: Hecklefang Pup. Its level, traits and experience travel with this item.', 20629, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91127, 0, 15, 0, 'Strigid Owlet Cage', 'Battle companion: Strigid Owlet. Its level, traits and experience travel with this item.', 19091, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91128, 0, 15, 0, 'Kraul Bat Cage', 'Battle companion: Kraul Batling. Its level, traits and experience travel with this item.', 17292, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91129, 0, 15, 0, 'Foreststrider Egg', 'Battle companion: Foreststrider Fledgling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18046, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91130, 0, 15, 0, 'Aged Kodo Calf Tether', 'Battle companion: Aged Kodo Calf. Grows into a rideable mount. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91131, 0, 15, 0, 'Gritjaw Basilisk Egg', 'Battle companion: Gritjaw Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91132, 0, 15, 0, 'Glasshide Basilisk Egg', 'Battle companion: Glasshide Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91133, 0, 15, 0, 'Thresher Roe', 'Battle companion: Thresher Fry. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91134, 0, 15, 0, 'Mystlash Hydra Egg', 'Battle companion: Mystlash Hydraling. Its level, traits and experience travel with this item.', 18050, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91135, 0, 15, 0, 'Timberling Seedpod', 'Battle companion: Timberling Sprout. Its level, traits and experience travel with this item.', 23754, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91136, 0, 15, 0, 'Jar of Toxic Ooze', 'Battle companion: Toxic Mote. Its level, traits and experience travel with this item.', 13689, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91137, 0, 15, 0, 'Cracked Golem Core', 'Battle companion: Cracked Golemling. Its level, traits and experience travel with this item.', 23754, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91138, 0, 15, 0, 'Jar of Ectoplasm', 'Battle companion: Ectoplasm Blob. Its level, traits and experience travel with this item.', 33202, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91139, 0, 15, 0, 'Jar of Sludge', 'Battle companion: Creeping Sludgeling. Its level, traits and experience travel with this item.', 33202, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91140, 0, 15, 0, 'Black Drake Egg', 'Battle companion: Black Drake Hatchling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 20655, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91141, 0, 15, 0, 'Bound Imp Brand', 'Battle companion: Imp Familiar. Its level, traits and experience travel with this item.', 21724, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91142, 0, 15, 0, 'Felstalker Chain', 'Battle companion: Felstalker Pup. Grows into a rideable mount. Its level, traits and experience travel with this item.', 21724, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91143, 0, 15, 0, 'Doomguard Binding Rune', 'Battle companion: Doomguard Spawn. Its level, traits and experience travel with this item.', 21724, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91144, 0, 15, 0, 'Darkhound Chain', 'Battle companion: Darkhound Whelp. Its level, traits and experience travel with this item.', 17292, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91145, 0, 15, 0, 'Rattling Bone Phylactery', 'Battle companion: Rattlecage Bones. Its level, traits and experience travel with this item.', 17786, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91146, 0, 15, 0, 'Ghoul Thrall Phylactery', 'Battle companion: Ghoul Thrall. Its level, traits and experience travel with this item.', 17786, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91147, 0, 15, 0, 'Wailing Shade Phylactery', 'Battle companion: Wailing Shade. Its level, traits and experience travel with this item.', 17786, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91148, 0, 15, 0, 'Crypt Scuttler Phylactery', 'Battle companion: Crypt Scuttler. Grows into a rideable mount. Its level, traits and experience travel with this item.', 17786, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91149, 0, 15, 0, 'Servo Repair Kit', 'Battle companion: Servo. Its level, traits and experience travel with this item.', 16536, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91150, 0, 15, 0, 'Compact Reaper Kit', 'Battle companion: Compact Reaper. Grows into a rideable mount. Its level, traits and experience travel with this item.', 16536, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91151, 0, 15, 0, 'Mechano-Tank Kit', 'Battle companion: Mechano-Tankling. Grows into a rideable mount. Its level, traits and experience travel with this item.', 16536, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91152, 0, 15, 0, 'Riverpaw Runt Contract', 'Battle companion: Riverpaw Runt. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91153, 0, 15, 0, 'Kobold Vermin Contract', 'Battle companion: Kobold Vermin. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91154, 0, 15, 0, 'Rockjaw Trogg Contract', 'Battle companion: Rockjaw Trogg. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91155, 0, 15, 0, 'Ogre Runt Contract', 'Battle companion: Ogre Runt. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91156, 0, 15, 0, 'Kolkar Drudge Contract', 'Battle companion: Kolkar Drudge. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91157, 0, 15, 0, 'Bristleback Piglet Contract', 'Battle companion: Bristleback Piglet. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91158, 0, 15, 0, 'Gnarlpine Cub Contract', 'Battle companion: Gnarlpine Cub. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91159, 0, 15, 0, 'Windfury Fledgling Contract', 'Battle companion: Windfury Fledgling. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91160, 0, 15, 0, 'Troll Whelp Contract', 'Battle companion: Troll Whelp. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91161, 0, 15, 0, 'Stormscale Wave Rider Contract', 'Battle companion: Stormscale Wave Rider. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91162, 0, 15, 0, 'Makrura Clacker Contract', 'Battle companion: Makrura Clacker. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91163, 0, 15, 0, 'Young Moonkin Contract', 'Battle companion: Young Moonkin. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2),
(91164, 0, 15, 0, 'Moonrage Whelp Contract', 'Battle companion: Moonrage Whelp. Its level, traits and experience travel with this item.', 17284, 3, 1, 12000, 3000, 0, 1, 0, 20, 2);

INSERT INTO `item_template` (`entry`, `patch`, `class`, `subclass`, `name`, `description`, `display_id`,
  `quality`, `buy_count`, `buy_price`, `sell_price`, `inventory_type`, `stackable`, `max_count`, `item_level`, `bonding`) VALUES
(90105, 0, 15, 0, 'Necrotic Residue', 'Lets an undead companion grow into its next form.', 7350, 2, 1, 30000, 7500, 0, 20, 0, 40, 0),
(90106, 0, 15, 0, 'Arcane Gearwork', 'Lets a mechanical companion grow into its next form.', 7400, 2, 1, 30000, 7500, 0, 20, 0, 40, 0),
(90107, 0, 15, 0, 'Tribal Fetish', 'Lets a humanoid companion grow into its next form.', 6618, 2, 1, 30000, 7500, 0, 20, 0, 40, 0);
