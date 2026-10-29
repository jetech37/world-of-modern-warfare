DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261004073000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261004073000');
-- Add your query below.

-- Azeroth Warfare: the five Create-a-Class classes of each character (MW2 design spec 1.4).
CREATE TABLE IF NOT EXISTS `character_fusion_class` (
  `guid` int(11) unsigned NOT NULL COMMENT 'Character Global Unique Identifier',
  `slot` tinyint(3) unsigned NOT NULL COMMENT '0-4',
  `name` varchar(24) NOT NULL DEFAULT '',
  `primary` smallint(5) unsigned NOT NULL DEFAULT '0' COMMENT 'catalog weapon id, 0 none',
  `primary_att1` varchar(16) NOT NULL DEFAULT '' COMMENT 'MW2 attachment id',
  `primary_att2` varchar(16) NOT NULL DEFAULT '',
  `primary_camo` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `secondary` smallint(5) unsigned NOT NULL DEFAULT '0',
  `secondary_att` varchar(16) NOT NULL DEFAULT '',
  `lethal` smallint(5) unsigned NOT NULL DEFAULT '0',
  `tactical` smallint(5) unsigned NOT NULL DEFAULT '0',
  `perk1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `perk2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `perk3` smallint(5) unsigned NOT NULL DEFAULT '0',
  `deathstreak` smallint(5) unsigned NOT NULL DEFAULT '0',
  `streak1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `streak2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `streak3` smallint(5) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- The class each character has equipped (255: none, the per-WoW-class gun).
CREATE TABLE IF NOT EXISTS `character_fusion` (
  `guid` int(11) unsigned NOT NULL COMMENT 'Character Global Unique Identifier',
  `equipped_slot` tinyint(3) unsigned NOT NULL DEFAULT '255',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
