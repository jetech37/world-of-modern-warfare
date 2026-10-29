-- World of Modern Warfare: what each companion form looks like (Companions::StageInfo::display,
-- the look table in server/src/fusion/src/Companions.cpp). World DB; apply after 02 and 08 (02
-- rebuilds fusion_companion_species, 08 adds the new lines); safe to apply again: the columns are
-- added only if missing and every UPDATE sets the same values.
--
--  - stageN_display: the creature display the stage wears, chosen from the same family's 1.12
--    displays so each form of a line is its own model or skin (whelp -> drake -> dragon, cub ->
--    sabercat -> riding sabercat, raptor colours, bear -> dire bear...). 0 = the stage creature's
--    own display. A mount line's last form is a mount model (riding wolf, riding sabercat, riding
--    raptor, riding kodo, ...), which the companion walks as and is ridden as.
--  - stageN_scale: the companion's scale on top of the display's own size (CreatureDisplayInfo),
--    as vanilla scales any creature, so a stage is as big as the catalog's size rule says
--    (design/COMPANION_CATALOG.md section 0); mount forms 1.0, the size they are ridden at. Each
--    form is clearly bigger than the one before.
--  - ride_display: the lines that ride on the wolf's spells show their last form's display.
--  - stage1_display is the young of the last form: its skin, or its colour (catalog section 1f).
--    It is the only hook an imported young model needs (with kLooks in Companions.cpp).
--
-- Live companions keep their item, stage and level: they wear the new look of their stage at
-- their next summon. CompanionTests holds these rows to Companions::StarterSpecies(); the client's
-- companion_ui tests check every display id against the install's CreatureDisplayInfo.dbc.

ALTER TABLE `fusion_companion_species`
  ADD COLUMN IF NOT EXISTS `stage1_display` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'display worn at stage 1; 0 = the creature''s own',
  ADD COLUMN IF NOT EXISTS `stage2_display` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'display worn at stage 2; 0 = the creature''s own',
  ADD COLUMN IF NOT EXISTS `stage3_display` mediumint(8) unsigned NOT NULL DEFAULT '0' COMMENT 'display worn at stage 3; 0 = the creature''s own';

UPDATE `fusion_companion_species` SET `stage1_display` = 6288, `stage1_scale` = 0.9, `stage2_display` = 9586, `stage2_scale` = 0.25, `stage3_display` = 8574, `stage3_scale` = 0.2 WHERE `id` = 1;
UPDATE `fusion_companion_species` SET `stage1_display` = 6290, `stage1_scale` = 1, `stage2_display` = 6376, `stage2_scale` = 0.3, `stage3_display` = 2718, `stage3_scale` = 0.2 WHERE `id` = 2;
UPDATE `fusion_companion_species` SET `stage1_display` = 6291, `stage1_scale` = 1, `stage2_display` = 5145, `stage2_scale` = 0.3, `stage3_display` = 7863, `stage3_scale` = 1.35 WHERE `id` = 3;
UPDATE `fusion_companion_species` SET `stage1_display` = 6293, `stage1_scale` = 1, `stage2_display` = 13971, `stage2_scale` = 0.3, `stage3_display` = 1687, `stage3_scale` = 0.2 WHERE `id` = 4;
UPDATE `fusion_companion_species` SET `stage1_display` = 6294, `stage1_scale` = 1, `stage2_display` = 9345, `stage2_scale` = 0.75, `stage3_display` = 2158, `stage3_scale` = 1 WHERE `id` = 5;
UPDATE `fusion_companion_species` SET `stage1_display` = 9572, `stage1_scale` = 0.95, `stage2_display` = 802, `stage2_scale` = 0.85, `stage3_display` = 247, `stage3_scale` = 1 WHERE `id` = 6;
UPDATE `fusion_companion_species` SET `stage1_display` = 11454, `stage1_scale` = 0.6, `stage2_display` = 9954, `stage2_scale` = 0.75, `stage3_display` = 6448, `stage3_scale` = 1 WHERE `id` = 7;
UPDATE `fusion_companion_species` SET `stage1_display` = 16942, `stage1_scale` = 1.15, `stage2_display` = 9953, `stage2_scale` = 0.8, `stage3_display` = 6080, `stage3_scale` = 1 WHERE `id` = 8;
UPDATE `fusion_companion_species` SET `stage1_display` = 11448, `stage1_scale` = 0.6, `stage2_display` = 599, `stage2_scale` = 1, `stage3_display` = 11450, `stage3_scale` = 1.5 WHERE `id` = 9;
UPDATE `fusion_companion_species` SET `stage1_display` = 5510, `stage1_scale` = 0.65, `stage2_display` = 820, `stage2_scale` = 1, `stage3_display` = 2281, `stage3_scale` = 1.9 WHERE `id` = 10;
UPDATE `fusion_companion_species` SET `stage1_display` = 9934, `stage1_scale` = 1.8, `stage2_display` = 955, `stage2_scale` = 0.8, `stage3_display` = 6808, `stage3_scale` = 0.85 WHERE `id` = 11;
UPDATE `fusion_companion_species` SET `stage1_display` = 960, `stage1_scale` = 0.5, `stage2_display` = 1959, `stage2_scale` = 1.2, `stage3_display` = 6473, `stage3_scale` = 1 WHERE `id` = 12;
UPDATE `fusion_companion_species` SET `stage1_display` = 1034, `stage1_scale` = 0.55, `stage2_display` = 2548, `stage2_scale` = 0.8, `stage3_display` = 807, `stage3_scale` = 1 WHERE `id` = 13;
UPDATE `fusion_companion_species` SET `stage1_display` = 841, `stage1_scale` = 0.55, `stage2_display` = 838, `stage2_scale` = 0.85, `stage3_display` = 844, `stage3_scale` = 1.65 WHERE `id` = 14;
UPDATE `fusion_companion_species` SET `stage1_display` = 15534, `stage1_scale` = 0.45, `stage2_display` = 4566, `stage2_scale` = 1, `stage3_display` = 10833, `stage3_scale` = 0.8 WHERE `id` = 15;
UPDATE `fusion_companion_species` SET `stage1_display` = 4185, `stage1_scale` = 1, `stage2_display` = 1955, `stage2_scale` = 0.8, `stage3_display` = 7894, `stage3_scale` = 1.15 WHERE `id` = 16;
UPDATE `fusion_companion_species` SET `stage1_display` = 1284, `stage1_scale` = 0.35, `stage2_display` = 1220, `stage2_scale` = 1.2, `stage3_display` = 1221, `stage3_scale` = 2.05 WHERE `id` = 17;
UPDATE `fusion_companion_species` SET `stage1_display` = 10987, `stage1_scale` = 0.4, `stage2_display` = 3247, `stage2_scale` = 1, `stage3_display` = 2414, `stage3_scale` = 1.7 WHERE `id` = 18;
UPDATE `fusion_companion_species` SET `stage1_display` = 8409, `stage1_scale` = 1.6, `stage2_display` = 1070, `stage2_scale` = 0.8, `stage3_display` = 1405, `stage3_scale` = 2.25 WHERE `id` = 19;
UPDATE `fusion_companion_species` SET `stage1_display` = 2069, `stage1_scale` = 1.4, `stage2_display` = 5561, `stage2_scale` = 1, `stage3_display` = 525, `stage3_scale` = 2 WHERE `id` = 20;
UPDATE `fusion_companion_species` SET `stage1_display` = 4766, `stage1_scale` = 0.18, `stage2_display` = 171, `stage2_scale` = 1, `stage3_display` = 2533, `stage3_scale` = 1.25 WHERE `id` = 21;
UPDATE `fusion_companion_species` SET `stage1_display` = 5327, `stage1_scale` = 0.65, `stage2_display` = 8714, `stage2_scale` = 1.25, `stage3_display` = 8715, `stage3_scale` = 1.4 WHERE `id` = 22;
UPDATE `fusion_companion_species` SET `stage1_display` = 15369, `stage1_scale` = 0.85, `stage2_display` = 527, `stage2_scale` = 1.25, `stage3_display` = 1079, `stage3_scale` = 1.05 WHERE `id` = 23;
UPDATE `fusion_companion_species` SET `stage1_display` = 10906, `stage1_scale` = 0.2, `stage2_display` = 21132, `stage2_scale` = 1, `stage3_display` = 169, `stage3_scale` = 1.9 WHERE `id` = 24;
UPDATE `fusion_companion_species` SET `stage1_display` = 8573, `stage1_scale` = 0.25, `stage2_display` = 7763, `stage2_scale` = 1.25, `stage3_display` = 4754, `stage3_scale` = 0.4 WHERE `id` = 25;
UPDATE `fusion_companion_species` SET `stage1_display` = 748, `stage1_scale` = 0.55, `stage2_display` = 16942, `stage2_scale` = 1.45, `stage3_display` = 749, `stage3_scale` = 0.95 WHERE `id` = 26;
UPDATE `fusion_companion_species` SET `stage1_display` = 328, `stage1_scale` = 1, `stage2_display` = 4626, `stage2_scale` = 1.8, `stage3_display` = 16191, `stage3_scale` = 1.05 WHERE `id` = 27;
UPDATE `fusion_companion_species` SET `stage1_display` = 14379, `stage1_scale` = 0.27, `stage2_display` = 8910, `stage2_scale` = 1.15, `stage3_display` = 6891, `stage3_scale` = 0.75 WHERE `id` = 28;
UPDATE `fusion_companion_species` SET `stage1_display` = 1454, `stage1_scale` = 0.2, `stage2_display` = 1230, `stage2_scale` = 0.45, `stage3_display` = 11641, `stage3_scale` = 1 WHERE `id` = 29;
UPDATE `fusion_companion_species` SET `stage1_display` = 229, `stage1_scale` = 0.4, `stage2_display` = 908, `stage2_scale` = 0.75, `stage3_display` = 2404, `stage3_scale` = 1 WHERE `id` = 30;
UPDATE `fusion_companion_species` SET `stage1_display` = 10666, `stage1_scale` = 0.35, `stage2_display` = 10661, `stage2_scale` = 0.6, `stage3_display` = 6569, `stage3_scale` = 1 WHERE `id` = 31;
UPDATE `fusion_companion_species` SET `stage1_display` = 5050, `stage1_scale` = 0.4, `stage2_display` = 5228, `stage2_scale` = 0.75, `stage3_display` = 10670, `stage3_scale` = 1 WHERE `id` = 32;
UPDATE `fusion_companion_species` SET `stage1_display` = 10000, `stage1_scale` = 0.4, `stage2_display` = 10003, `stage2_scale` = 0.8, `stage3_display` = 2736, `stage3_scale` = 1 WHERE `id` = 33;
UPDATE `fusion_companion_species` SET `stage1_display` = 709, `stage1_scale` = 0.7, `stage2_display` = 366, `stage2_scale` = 1.25, `stage3_display` = 336, `stage3_scale` = 1, `ride_display` = 336 WHERE `id` = 101;
UPDATE `fusion_companion_species` SET `stage1_display` = 2536, `stage1_scale` = 2.2, `stage2_display` = 14853, `stage2_scale` = 0.75, `stage3_display` = 2537, `stage3_scale` = 1, `ride_display` = 2537 WHERE `id` = 102;
UPDATE `fusion_companion_species` SET `stage1_display` = 4457, `stage1_scale` = 0.27, `stage2_display` = 963, `stage2_scale` = 0.7, `stage3_display` = 7510, `stage3_scale` = 1, `ride_display` = 7510 WHERE `id` = 103;
UPDATE `fusion_companion_species` SET `stage1_display` = 8843, `stage1_scale` = 0.65, `stage2_display` = 11908, `stage2_scale` = 0.7, `stage3_display` = 762, `stage3_scale` = 1, `ride_display` = 762 WHERE `id` = 104;
UPDATE `fusion_companion_species` SET `stage1_display` = 8840, `stage1_scale` = 0.45, `stage2_display` = 865, `stage2_scale` = 0.9, `stage3_display` = 8838, `stage3_scale` = 1, `ride_display` = 8838 WHERE `id` = 105;
UPDATE `fusion_companion_species` SET `stage1_display` = 15151, `stage1_scale` = 0.7, `stage2_display` = 1043, `stage2_scale` = 0.9, `stage3_display` = 6443, `stage3_scale` = 1, `ride_display` = 6443 WHERE `id` = 106;
UPDATE `fusion_companion_species` SET `stage1_display` = 1059, `stage1_scale` = 0.5, `stage2_display` = 1043, `stage2_scale` = 1, `stage3_display` = 6442, `stage3_scale` = 1, `ride_display` = 6442 WHERE `id` = 107;
UPDATE `fusion_companion_species` SET `stage1_display` = 1978, `stage1_scale` = 0.4, `stage2_display` = 1977, `stage2_scale` = 0.7, `stage3_display` = 9714, `stage3_scale` = 1, `ride_display` = 9714 WHERE `id` = 108;
UPDATE `fusion_companion_species` SET `stage1_display` = 15204, `stage1_scale` = 1.15, `stage2_display` = 741, `stage2_scale` = 0.75, `stage3_display` = 2326, `stage3_scale` = 1, `ride_display` = 2326 WHERE `id` = 109;
UPDATE `fusion_companion_species` SET `stage1_display` = 855, `stage1_scale` = 1, `stage2_display` = 11419, `stage2_scale` = 0.75, `stage3_display` = 14336, `stage3_scale` = 1, `ride_display` = 14336 WHERE `id` = 110;
UPDATE `fusion_companion_species` SET `stage1_display` = 9572, `stage1_scale` = 1.15, `stage2_display` = 3202, `stage2_scale` = 0.65, `stage3_display` = 2328, `stage3_scale` = 1, `ride_display` = 2328 WHERE `id` = 111;
UPDATE `fusion_companion_species` SET `stage1_display` = 675, `stage1_scale` = 0.45, `stage2_display` = 1747, `stage2_scale` = 0.9, `stage3_display` = 6472, `stage3_scale` = 1, `ride_display` = 6472 WHERE `id` = 112;
UPDATE `fusion_companion_species` SET `stage1_display` = 674, `stage1_scale` = 0.5, `stage2_display` = 949, `stage2_scale` = 0.9, `stage3_display` = 4806, `stage3_scale` = 1, `ride_display` = 4806 WHERE `id` = 113;
UPDATE `fusion_companion_species` SET `stage1_display` = 2571, `stage1_scale` = 0.3, `stage2_display` = 788, `stage2_scale` = 0.7, `stage3_display` = 6469, `stage3_scale` = 1, `ride_display` = 6469 WHERE `id` = 114;
UPDATE `fusion_companion_species` SET `stage1_display` = 5905, `stage1_scale` = 1.5, `stage2_display` = 2732, `stage2_scale` = 0.9, `stage3_display` = 2491, `stage3_scale` = 1, `ride_display` = 2491 WHERE `id` = 115;
UPDATE `fusion_companion_species` SET `stage1_display` = 13097, `stage1_scale` = 0.5, `stage2_display` = 2765, `stage2_scale` = 1.1, `stage3_display` = 15385, `stage3_scale` = 1, `ride_display` = 15385 WHERE `id` = 116;
UPDATE `fusion_companion_species` SET `stage1_display` = 831, `stage1_scale` = 0.38, `stage2_display` = 2548, `stage2_scale` = 0.8, `stage3_display` = 807, `stage3_scale` = 1, `ride_display` = 807 WHERE `id` = 117;
UPDATE `fusion_companion_species` SET `stage1_display` = 14660, `stage1_scale` = 1.5, `stage2_display` = 1244, `stage2_scale` = 1.15, `stage3_display` = 5027, `stage3_scale` = 1, `ride_display` = 5027 WHERE `id` = 118;
UPDATE `fusion_companion_species` SET `stage1_display` = 6368, `stage1_scale` = 0.75, `stage2_display` = 7837, `stage2_scale` = 1, `stage3_display` = 5127, `stage3_scale` = 1, `ride_display` = 5127 WHERE `id` = 119;
UPDATE `fusion_companion_species` SET `stage1_display` = 837, `stage1_scale` = 0.45, `stage2_display` = 3188, `stage2_scale` = 0.9, `stage3_display` = 5294, `stage3_scale` = 1.55 WHERE `id` = 120;
UPDATE `fusion_companion_species` SET `stage1_display` = 6807, `stage1_scale` = 0.45, `stage2_display` = 704, `stage2_scale` = 0.8, `stage3_display` = 3035, `stage3_scale` = 1, `ride_display` = 3035 WHERE `id` = 121;
UPDATE `fusion_companion_species` SET `stage1_display` = 641, `stage1_scale` = 0.5, `stage2_display` = 981, `stage2_scale` = 1, `stage3_display` = 699, `stage3_scale` = 1.15 WHERE `id` = 122;
UPDATE `fusion_companion_species` SET `stage1_display` = 3006, `stage1_scale` = 0.35, `stage2_display` = 2705, `stage2_scale` = 1, `stage3_display` = 7829, `stage3_scale` = 0.7 WHERE `id` = 123;
UPDATE `fusion_companion_species` SET `stage1_display` = 1192, `stage1_scale` = 0.21, `stage2_display` = 2305, `stage2_scale` = 1.05, `stage3_display` = 10826, `stage3_scale` = 0.9 WHERE `id` = 124;
UPDATE `fusion_companion_species` SET `stage1_display` = 490, `stage1_scale` = 0.26, `stage2_display` = 2305, `stage2_scale` = 1.15, `stage3_display` = 10827, `stage3_scale` = 0.95 WHERE `id` = 125;
UPDATE `fusion_companion_species` SET `stage1_display` = 2726, `stage1_scale` = 0.55, `stage2_display` = 2711, `stage2_scale` = 1.15, `stage3_display` = 10902, `stage3_scale` = 1.5 WHERE `id` = 126;
UPDATE `fusion_companion_species` SET `stage1_display` = 15534, `stage1_scale` = 0.45, `stage2_display` = 4877, `stage2_scale` = 1, `stage3_display` = 10828, `stage3_scale` = 1.15 WHERE `id` = 127;
UPDATE `fusion_companion_species` SET `stage1_display` = 9535, `stage1_scale` = 0.9, `stage2_display` = 1955, `stage2_scale` = 1, `stage3_display` = 11032, `stage3_scale` = 0.8 WHERE `id` = 128;
UPDATE `fusion_companion_species` SET `stage1_display` = 1281, `stage1_scale` = 0.4, `stage2_display` = 178, `stage2_scale` = 1, `stage3_display` = 6477, `stage3_scale` = 1, `ride_display` = 6477 WHERE `id` = 129;
UPDATE `fusion_companion_species` SET `stage1_display` = 10916, `stage1_scale` = 0.15, `stage2_display` = 1308, `stage2_scale` = 0.3, `stage3_display` = 12241, `stage3_scale` = 1, `ride_display` = 12241 WHERE `id` = 130;
UPDATE `fusion_companion_species` SET `stage1_display` = 1074, `stage1_scale` = 0.22, `stage2_display` = 4486, `stage2_scale` = 0.9, `stage3_display` = 7345, `stage3_scale` = 1, `ride_display` = 7345 WHERE `id` = 131;
UPDATE `fusion_companion_species` SET `stage1_display` = 1073, `stage1_scale` = 0.85, `stage2_display` = 8797, `stage2_scale` = 1, `stage3_display` = 12339, `stage3_scale` = 1, `ride_display` = 12339 WHERE `id` = 132;
UPDATE `fusion_companion_species` SET `stage1_display` = 2425, `stage1_scale` = 0.2, `stage2_display` = 994, `stage2_scale` = 1.15, `stage3_display` = 6431, `stage3_scale` = 1.1 WHERE `id` = 133;
UPDATE `fusion_companion_species` SET `stage1_display` = 2728, `stage1_scale` = 0.2, `stage2_display` = 10898, `stage2_scale` = 1, `stage3_display` = 2423, `stage3_scale` = 0.8 WHERE `id` = 134;
UPDATE `fusion_companion_species` SET `stage1_display` = 5712, `stage1_scale` = 0.58, `stage2_display` = 2567, `stage2_scale` = 1.15, `stage3_display` = 2024, `stage3_scale` = 1.6 WHERE `id` = 135;
UPDATE `fusion_companion_species` SET `stage1_display` = 5491, `stage1_scale` = 0.9, `stage2_display` = 9449, `stage2_scale` = 0.9, `stage3_display` = 5497, `stage3_scale` = 1.5 WHERE `id` = 136;
UPDATE `fusion_companion_species` SET `stage1_display` = 2690, `stage1_scale` = 0.34, `stage2_display` = 10804, `stage2_scale` = 1, `stage3_display` = 10801, `stage3_scale` = 1.4 WHERE `id` = 137;
UPDATE `fusion_companion_species` SET `stage1_display` = 7763, `stage1_scale` = 1.55, `stage2_display` = 360, `stage2_scale` = 1.25, `stage3_display` = 11139, `stage3_scale` = 1.05 WHERE `id` = 138;
UPDATE `fusion_companion_species` SET `stage1_display` = 14350, `stage1_scale` = 0.65, `stage2_display` = 1145, `stage2_scale` = 1.25, `stage3_display` = 15686, `stage3_scale` = 0.25 WHERE `id` = 139;
UPDATE `fusion_companion_species` SET `stage1_display` = 387, `stage1_scale` = 0.6, `stage2_display` = 8713, `stage2_scale` = 0.95, `stage3_display` = 9585, `stage3_scale` = 1, `ride_display` = 9585 WHERE `id` = 140;
UPDATE `fusion_companion_species` SET `stage1_display` = 10811, `stage1_scale` = 0.75, `stage2_display` = 10812, `stage2_scale` = 1.4, `stage3_display` = 12190, `stage3_scale` = 1.35 WHERE `id` = 141;
UPDATE `fusion_companion_species` SET `stage1_display` = 850, `stage1_scale` = 0.5, `stage2_display` = 12168, `stage2_scale` = 0.85, `stage3_display` = 6173, `stage3_scale` = 1, `ride_display` = 6173 WHERE `id` = 142;
UPDATE `fusion_companion_species` SET `stage1_display` = 433, `stage1_scale` = 0.5, `stage2_display` = 9017, `stage2_scale` = 0.9, `stage3_display` = 4426, `stage3_scale` = 0.85 WHERE `id` = 143;
UPDATE `fusion_companion_species` SET `stage1_display` = 7892, `stage1_scale` = 0.6, `stage2_display` = 9020, `stage2_scale` = 1.1, `stage3_display` = 9022, `stage3_scale` = 1.3 WHERE `id` = 144;
UPDATE `fusion_companion_species` SET `stage1_display` = 1019, `stage1_scale` = 0.55, `stage2_display` = 200, `stage2_scale` = 1.25, `stage3_display` = 11489, `stage3_scale` = 1.2 WHERE `id` = 145;
UPDATE `fusion_companion_species` SET `stage1_display` = 3670, `stage1_scale` = 0.8, `stage2_display` = 570, `stage2_scale` = 0.85, `stage3_display` = 1693, `stage3_scale` = 0.6 WHERE `id` = 146;
UPDATE `fusion_companion_species` SET `stage1_display` = 985, `stage1_scale` = 0.55, `stage2_display` = 10702, `stage2_scale` = 1, `stage3_display` = 8783, `stage3_scale` = 1.15 WHERE `id` = 147;
UPDATE `fusion_companion_species` SET `stage1_display` = 9793, `stage1_scale` = 0.22, `stage2_display` = 3004, `stage2_scale` = 0.8, `stage3_display` = 9758, `stage3_scale` = 1, `ride_display` = 9758 WHERE `id` = 148;
UPDATE `fusion_companion_species` SET `stage1_display` = 1150, `stage1_scale` = 0.9, `stage2_display` = 367, `stage2_scale` = 1, `stage3_display` = 548, `stage3_scale` = 1.1 WHERE `id` = 149;
UPDATE `fusion_companion_species` SET `stage1_display` = 1159, `stage1_scale` = 0.7, `stage2_display` = 1303, `stage2_scale` = 1.05, `stage3_display` = 1269, `stage3_scale` = 1, `ride_display` = 1269 WHERE `id` = 150;
UPDATE `fusion_companion_species` SET `stage1_display` = 14379, `stage1_scale` = 0.27, `stage2_display` = 6890, `stage2_scale` = 0.8, `stage3_display` = 6891, `stage3_scale` = 1, `ride_display` = 6891 WHERE `id` = 151;
UPDATE `fusion_companion_species` SET `stage1_display` = 10791, `stage1_scale` = 0.55, `stage2_display` = 384, `stage2_scale` = 1, `stage3_display` = 609, `stage3_scale` = 1.35 WHERE `id` = 152;
UPDATE `fusion_companion_species` SET `stage1_display` = 10913, `stage1_scale` = 0.75, `stage2_display` = 2299, `stage2_scale` = 1.1, `stage3_display` = 511, `stage3_scale` = 1.25 WHERE `id` = 153;
UPDATE `fusion_companion_species` SET `stage1_display` = 990, `stage1_scale` = 0.5, `stage2_display` = 160, `stage2_scale` = 1, `stage3_display` = 6533, `stage3_scale` = 1 WHERE `id` = 154;
UPDATE `fusion_companion_species` SET `stage1_display` = 11571, `stage1_scale` = 0.28, `stage2_display` = 3193, `stage2_scale` = 1, `stage3_display` = 11561, `stage3_scale` = 0.85 WHERE `id` = 155;
UPDATE `fusion_companion_species` SET `stage1_display` = 9442, `stage1_scale` = 0.5, `stage2_display` = 9414, `stage2_scale` = 1, `stage3_display` = 9433, `stage3_scale` = 1.15 WHERE `id` = 156;
UPDATE `fusion_companion_species` SET `stage1_display` = 1967, `stage1_scale` = 0.55, `stage2_display` = 1253, `stage2_scale` = 0.8, `stage3_display` = 6108, `stage3_scale` = 1 WHERE `id` = 157;
UPDATE `fusion_companion_species` SET `stage1_display` = 6827, `stage1_scale` = 0.45, `stage2_display` = 1999, `stage2_scale` = 1, `stage3_display` = 6828, `stage3_scale` = 1.15 WHERE `id` = 158;
UPDATE `fusion_companion_species` SET `stage1_display` = 10873, `stage1_scale` = 0.34, `stage2_display` = 1255, `stage2_scale` = 1.15, `stage3_display` = 10874, `stage3_scale` = 1.35 WHERE `id` = 159;
UPDATE `fusion_companion_species` SET `stage1_display` = 5606, `stage1_scale` = 0.55, `stage2_display` = 4579, `stage2_scale` = 1, `stage3_display` = 7803, `stage3_scale` = 1.1 WHERE `id` = 160;
UPDATE `fusion_companion_species` SET `stage1_display` = 4921, `stage1_scale` = 0.45, `stage2_display` = 6747, `stage2_scale` = 1, `stage3_display` = 9134, `stage3_scale` = 1.1 WHERE `id` = 161;
UPDATE `fusion_companion_species` SET `stage1_display` = 2598, `stage1_scale` = 0.27, `stage2_display` = 2426, `stage2_scale` = 1, `stage3_display` = 1817, `stage3_scale` = 1.15 WHERE `id` = 162;
UPDATE `fusion_companion_species` SET `stage1_display` = 12234, `stage1_scale` = 0.35, `stage2_display` = 5927, `stage2_scale` = 1, `stage3_display` = 12237, `stage3_scale` = 1.5 WHERE `id` = 163;
UPDATE `fusion_companion_species` SET `stage1_display` = 657, `stage1_scale` = 0.6, `stage2_display` = 1979, `stage2_scale` = 0.9, `stage3_display` = 1098, `stage3_scale` = 1.2 WHERE `id` = 164;
