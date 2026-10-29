#!/usr/bin/env bash
# Nightly game-database backup (run by aw-backup.timer). Players' accounts and characters
# (realmd, characters) every night, kept 30 days; the world (mangos, with our custom tables
# and tuning) too, kept 7 days. Consistent snapshots: --single-transaction, no server stop.
set -euo pipefail
DIR=/var/backups/aw
STAMP=$(date +%Y-%m-%d_%H%M)
mkdir -p "$DIR"
chmod 700 "$DIR"
for db in realmd characters mangos; do
    mariadb-dump --single-transaction --routines --triggers "$db" | gzip -9 > "$DIR/$db-$STAMP.sql.gz.part"
    mv "$DIR/$db-$STAMP.sql.gz.part" "$DIR/$db-$STAMP.sql.gz"
done
find "$DIR" -name 'realmd-*.sql.gz' -mtime +30 -delete
find "$DIR" -name 'characters-*.sql.gz' -mtime +30 -delete
find "$DIR" -name 'mangos-*.sql.gz' -mtime +7 -delete
find "$DIR" -name '*.part' -mtime +1 -delete
echo "backup $STAMP ok"
