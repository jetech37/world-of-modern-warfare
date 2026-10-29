-- World of Modern Warfare: the wanderers (design/WANDERERS.md; server/src/game/Fusion/FusionWanderers.cpp,
-- the rules in server/src/fusion/include/fusion/Wanderers.h). World DB; safe to apply again: the
-- three tables are this file's own and are made afresh.
--
-- Reserved ranges (kept free):
--   creature_template.entry      93700-93799: made by the server at startup (one per wanderer,
--                                93700 + its id), never in creature_template
--
-- The wanderers themselves (their names, levels, purses, what they are doing, how well they know
-- each player) live in the characters database, in `aw_wanderer` and `aw_wanderer_bond`, which
-- the server makes at startup. mangosd.conf: Wanderers.* (off by default).

-- ── The zones ───────────────────────────────────────────────────────────────────────────────────
-- Where wanderers live: `zone` the AreaTable id, `map` its continent, `side` 1 Alliance or 2 Horde
-- (a region is the zones of one map and one side: its wanderers walk all of it), and the levels
-- they are made at (they may gain two past the top). Wanderers.Zones picks which rows are used.
DROP TABLE IF EXISTS `aw_wanderer_zone`;
CREATE TABLE `aw_wanderer_zone` (
  `zone` int(10) unsigned NOT NULL,
  `map` int(10) unsigned NOT NULL,
  `side` tinyint(3) unsigned NOT NULL,
  `min_level` tinyint(3) unsigned NOT NULL,
  `max_level` tinyint(3) unsigned NOT NULL,
  `name` varchar(64) NOT NULL DEFAULT '' COMMENT 'for reading this table; the server names zones from AreaTable',
  PRIMARY KEY (`zone`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW wanderers: the zones they live in';

INSERT INTO `aw_wanderer_zone` (`zone`, `map`, `side`, `min_level`, `max_level`, `name`) VALUES
(12, 0, 1,  4, 10, 'Elwynn Forest'),
(40, 0, 1, 10, 18, 'Westfall'),
(14, 1, 2,  4, 10, 'Durotar'),
(17, 1, 2, 10, 22, 'The Barrens'),
-- Phase 2: the wider world.
(44,   0, 1, 15, 25, 'Redridge Mountains'),
(10,   0, 1, 18, 30, 'Duskwood'),
(1519, 0, 1, 10, 30, 'Stormwind City'),
(1637, 1, 2, 10, 30, 'Orgrimmar'),
(215,  1, 2,  4, 10, 'Mulgore'),
(1638, 1, 2, 10, 30, 'Thunder Bluff'),
(85,   0, 2,  4, 10, 'Tirisfal Glades'),
(130,  0, 2, 10, 20, 'Silverpine Forest'),
(1497, 0, 2, 10, 30, 'Undercity');

-- ── The places ──────────────────────────────────────────────────────────────────────────────────
-- The towns, farms and camps wanderers walk between. `kind`: 0 a town (a short rest), 1 an inn (a
-- long one), 3 a waypoint on a road (never a destination). A place stands beside an `anchor`
-- creature (its first spawn), `dx`/`dy` yards from it; with anchor 0, at `x`/`y`/`z`. Phase 2's
-- kinds: 4 fishing water, 5 a mine, 6 herb meadows, 7 hunting grounds, 8 a market, 9 a landmark (a
-- sight a curious wanderer or a pilgrim goes to see). Most workplaces and markets are found by the
-- server in the world's own data (ore veins and herbs by their locks, skinnable game, fishing
-- trainers and suppliers, clusters of vendors: Wanderers.WorkSpotsPerZone); the rows here add the
-- ones it cannot find (water with no fishing folk, the landmarks). A place
-- whose anchor is not spawned, or that does not stand in its zone, is left out and logged, so a
-- wrong entry never puts a town somewhere strange. Ids 1-9999 (10000+ are the bounty boards,
-- 20000+ the innkeepers and flight masters the server finds by itself in the same zones).
-- VERIFY in game (.wanderer list, .go creature): each anchor is the NPC named beside it.
DROP TABLE IF EXISTS `aw_wanderer_node`;
CREATE TABLE `aw_wanderer_node` (
  `id` int(10) unsigned NOT NULL,
  `zone` int(10) unsigned NOT NULL,
  `kind` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `name` varchar(64) NOT NULL,
  `anchor` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `dx` float NOT NULL DEFAULT '0',
  `dy` float NOT NULL DEFAULT '0',
  `x` float NOT NULL DEFAULT '0',
  `y` float NOT NULL DEFAULT '0',
  `z` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW wanderers: the places on their roads';

INSERT INTO `aw_wanderer_node` (`id`, `zone`, `kind`, `name`, `anchor`, `dx`, `dy`) VALUES
-- Elwynn Forest
(1,  12, 1, 'Goldshire, the Lion''s Pride Inn',  295, 0, 0),    -- Innkeeper Farley
(2,  12, 0, 'Goldshire',                         240, 3, 3),    -- Marshal Dughan
(3,  12, 0, 'Northshire Abbey',                  197, 3, 0),    -- Marshal McBride
(4,  12, 0, 'Northshire, the vineyards road',    823, 4, 0),    -- Deputy Willem
(5,  12, 0, 'Tower of Azora',                    313, 4, 0),    -- Theocritus
(6,  12, 0, 'Eastvale Logging Camp',           10616, 4, 0),    -- Supervisor Raelen
-- Westfall
(20, 40, 1, 'Sentinel Hill, the inn',           8931, 0, 0),    -- Innkeeper Heather
(21, 40, 0, 'Sentinel Hill',                     234, 3, 3),    -- Gryan Stoutmantle
(22, 40, 0, 'Saldean''s Farm',                   233, 4, 0),    -- Farmer Saldean
(23, 40, 0, 'Furlbrow''s Pumpkin Farm',          237, 4, 0),    -- Farmer Furlbrow
-- Durotar
(40, 14, 1, 'Razor Hill, the inn',              6928, 0, 0),    -- Innkeeper Grosk
(41, 14, 0, 'Razor Hill',                       3139, 3, 3),    -- Gar'Thok
(42, 14, 0, 'Valley of Trials',                 3143, 4, 0),    -- Gornek
(43, 14, 0, 'Sen''jin Village',                 3188, 4, 0),    -- Master Gadrin
-- The Barrens
(60, 17, 1, 'The Crossroads, the inn',          3934, 0, 0),    -- Innkeeper Boorand Plainswind
(61, 17, 0, 'The Crossroads',                   3429, 3, 3),    -- Thork
(62, 17, 1, 'Ratchet, the inn',                 6791, 0, 0),    -- Innkeeper Wiley
(63, 17, 0, 'Ratchet',                          3391, 4, 0),    -- Gazlowe
(64, 17, 1, 'Camp Taurajo, the inn',            7714, 0, 0),    -- Innkeeper Byula
(65, 17, 0, 'Far Watch Post',                   3337, 4, 0),    -- Kargal Battlescar
-- Phase 2. Westfall
(24, 40, 9, 'The Westfall Lighthouse',           392, 6, 0),    -- Captain Grayson
-- Redridge Mountains (Lakeshire's inn and flight master are found by the server)
(80, 44, 4, 'Lake Everstill, by the bridge',     341, 6, 4),    -- Foreman Oslow
(81, 44, 9, 'Lakeshire Town Hall',               344, 4, 0),    -- Magistrate Solomon
-- Duskwood
(90, 10, 9, 'Darkshire',                         264, 4, 0),    -- Commander Althea Ebonlocke
(91, 10, 9, 'Raven Hill',                        311, 4, 0),    -- Sven Yorgen
-- Stormwind City
(100, 1519, 9, 'The Cathedral of Light',        1284, 4, 0),    -- Archbishop Benedictus
(101, 1519, 9, 'Stormwind Keep',                1748, 6, 0),    -- Highlord Bolvar Fordragon
(102, 1519, 4, 'The Stormwind canals',          5493, 4, 0),    -- Arnold Leland
-- Orgrimmar
(120, 1637, 9, 'Grommash Hold',                 4949, 6, 0),    -- Thrall
(121, 1637, 4, 'The Valley of Honor pool',      3332, 4, 0),    -- Lumak
-- Thunder Bluff
(140, 1638, 9, 'Elder Rise',                    3057, 6, 0),    -- Cairne Bloodhoof
-- Tirisfal Glades
(160, 85, 0, 'Brill',                           1499, 4, 0),    -- Magistrate Sevren
(161, 85, 0, 'Deathknell',                      1568, 4, 0),    -- Undertaker Mordo
-- Undercity
(170, 1497, 9, 'The Royal Quarter',             2425, 6, 0),    -- Varimathras
-- Silverpine Forest
(180, 130, 0, 'The Sepulcher',                  1952, 4, 0);    -- High Executor Hadrec

-- ── Roads ───────────────────────────────────────────────────────────────────────────────────────
-- Roads of their own between two places (node ids), besides those the server makes: a spanning
-- tree of the shortest legs (so every place is reachable) and a road between any two places
-- within 350 yards. Add one where the shortest leg is not the road (a river, a ridge between);
-- road waypoints (kind 3) bend a leg along a real road.
DROP TABLE IF EXISTS `aw_wanderer_road`;
CREATE TABLE `aw_wanderer_road` (
  `a` int(10) unsigned NOT NULL,
  `b` int(10) unsigned NOT NULL,
  PRIMARY KEY (`a`, `b`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW wanderers: roads between places';

INSERT INTO `aw_wanderer_road` (`a`, `b`) VALUES
(2, 3),         -- Goldshire up the road to Northshire
(41, 43),       -- Razor Hill down the coast road to Sen'jin
(41, 42),       -- Razor Hill to the Valley of Trials
(61, 65),       -- the Crossroads to Far Watch Post (and on to Durotar)
(61, 63),       -- the Crossroads to Ratchet
(61, 64);       -- the Crossroads south to Camp Taurajo
