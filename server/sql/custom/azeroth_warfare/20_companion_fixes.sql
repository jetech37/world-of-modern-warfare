-- World of Modern Warfare: battle companion clean-up (src/fusion/include/fusion/Companions.h,
-- game/Fusion/FusionCompanions.cpp). Apply to the world database after 02_companions.sql and
-- 08_companion_lines.sql; safe to run again (every statement sets a value outright).
--
-- The characters database needs nothing here: the server makes its own companion tables at
-- startup, now including `character_fusion_companion_out` (the companion a character had out when
-- it logged out, summoned again at login).

-- The mount companions' items told players to ride them "from the companion bar", which is gone:
-- a rideable companion is ridden with the mount button on its pet bar (slot 7). 02_companions.sql
-- now writes these texts itself; this brings a world that applied the old file up to date.
-- (Clients cache item texts: a client that saw the old text shows it until its cache is cleared.)
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 war wolf. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90060;
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 striped nightsaber. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90061;
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 striped frostsaber. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90062;
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 violet raptor. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90063;
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 brown kodo. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90064;
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 brown horse. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90065;
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 blue mechanostrider. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90066;
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 red skeletal horse. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90067;
UPDATE `item_template` SET `description` = 'Battle companion mount: a level 40 gray ram. Walks and fights at your side; ride it with the mount button on its pet bar.' WHERE `entry` = 90068;

-- The starter lines that grow into mounts (wolf, nightsaber, frostsaber, raptor) did not say so on
-- their items, as every other rideable line's does.
UPDATE `item_template` SET `description` = 'Battle companion: Wolf Pup. Grows into a rideable wolf. Its level and experience travel with this item.' WHERE `entry` = 90006;
UPDATE `item_template` SET `description` = 'Battle companion: Nightsaber Cub. Grows into a rideable nightsaber. Its level and experience travel with this item.' WHERE `entry` = 90007;
UPDATE `item_template` SET `description` = 'Battle companion: Frostsaber Cub. Grows into a rideable frostsaber. Its level and experience travel with this item.' WHERE `entry` = 90008;
UPDATE `item_template` SET `description` = 'Battle companion: Raptor Hatchling. Grows into a rideable raptor. Its level and experience travel with this item.' WHERE `entry` = 90012;
