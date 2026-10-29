-- Azeroth Warfare: every class trains bows, guns and crossbows (design doc, "Guns and the class
-- game"), so every character's ranged slot can hold the item that powers their guns.
-- Paladins, shamans and druids lack them in 1.12; this teaches them at character creation.
-- Apply to the world database after the vmangos world dump.

-- Bows 264, Guns 266, Crossbows 5011.
DELETE FROM `playercreateinfo_spell` WHERE `class` IN (2, 7, 11) AND `spell` IN (264, 266, 5011);
INSERT INTO `playercreateinfo_spell` (`race`, `class`, `spell`, `build_min`, `build_max`) VALUES
-- Paladin: Human, Dwarf
(1, 2, 264, 0, 5875), (1, 2, 266, 0, 5875), (1, 2, 5011, 0, 5875),
(3, 2, 264, 0, 5875), (3, 2, 266, 0, 5875), (3, 2, 5011, 0, 5875),
-- Shaman: Orc, Tauren, Troll
(2, 7, 264, 0, 5875), (2, 7, 266, 0, 5875), (2, 7, 5011, 0, 5875),
(6, 7, 264, 0, 5875), (6, 7, 266, 0, 5875), (6, 7, 5011, 0, 5875),
(8, 7, 264, 0, 5875), (8, 7, 266, 0, 5875), (8, 7, 5011, 0, 5875),
-- Druid: Night Elf, Tauren
(4, 11, 264, 0, 5875), (4, 11, 266, 0, 5875), (4, 11, 5011, 0, 5875),
(6, 11, 264, 0, 5875), (6, 11, 266, 0, 5875), (6, 11, 5011, 0, 5875);

-- Existing characters of those classes learn them too.
INSERT IGNORE INTO `characters`.`character_spell` (`guid`, `spell`, `active`, `disabled`)
SELECT c.`guid`, s.`spell`, 1, 0 FROM `characters`.`characters` c
JOIN (SELECT 264 AS `spell` UNION SELECT 266 UNION SELECT 5011) s
WHERE c.`class` IN (2, 7, 11);
