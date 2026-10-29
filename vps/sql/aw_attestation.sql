-- Players' legal-access attestations, in the login database (realmd). Safe to run again.
--
-- The gate (vps/gate.py) adds a row when an account signs up (the box ticked in the game's
-- Create Account dialog, or Y typed in the launcher) and once for an older account from the
-- launcher (POST /api/attest). One row per account and statement version: a new version of the
-- statement asks everyone again. `aw-admin attestations` lists them.
--
-- The statement, version 1:
--   I own and have legal access to World of Warcraft and Call of Duty: Modern Warfare 2 (2009).
CREATE TABLE IF NOT EXISTS aw_attestation (
    id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    account_id INT UNSIGNED NOT NULL,
    version SMALLINT UNSIGNED NOT NULL,
    attested_at DATETIME NOT NULL COMMENT 'UTC',
    ip VARCHAR(45) NOT NULL DEFAULT '',
    source VARCHAR(16) NOT NULL DEFAULT '' COMMENT 'signup or launcher',
    UNIQUE KEY account_version (account_id, version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='WOMW legal-access attestations';
