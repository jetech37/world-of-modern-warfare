#!/usr/bin/env bash
# Azeroth Warfare: create and load the four databases (once). Runs inside the image.
# Env: AW_DB_HOST, AW_DB_ROOT_PASSWORD, AW_DB_PASSWORD, AW_PUBLIC_ADDRESS, AW_REALM_NAME,
#      AW_WORLD_DUMP (optional: a local world dump .sql/.zip/.7z instead of downloading).
set -euo pipefail
SQL=${AW_SQL_DIR:-/opt/aw/sql}
HOST=${AW_DB_HOST:-db}
ROOT=(mariadb -h "$HOST" -uroot "-p${AW_DB_ROOT_PASSWORD}")

"${ROOT[@]}" <<SQL
CREATE DATABASE IF NOT EXISTS realmd DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS mangos DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS characters DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS logs DEFAULT CHARSET utf8;
CREATE USER IF NOT EXISTS 'mangos'@'%' IDENTIFIED BY '${AW_DB_PASSWORD}';
GRANT ALL PRIVILEGES ON realmd.* TO 'mangos'@'%';
GRANT ALL PRIVILEGES ON mangos.* TO 'mangos'@'%';
GRANT ALL PRIVILEGES ON characters.* TO 'mangos'@'%';
GRANT ALL PRIVILEGES ON logs.* TO 'mangos'@'%';
FLUSH PRIVILEGES;
SQL

loaded() { [ "$("${ROOT[@]}" -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$1'")" != "0" ]; }

loaded realmd     || "${ROOT[@]}" realmd     < "$SQL/logon.sql"
loaded characters || "${ROOT[@]}" characters < "$SQL/characters.sql"
loaded logs       || "${ROOT[@]}" logs       < "$SQL/logs.sql"

if ! loaded mangos; then
    work=$(mktemp -d)
    dump=${AW_WORLD_DUMP:-}
    if [ -z "$dump" ]; then
        echo "Downloading the vmangos world database snapshot (release db_latest)..."
        url=$(curl -fsSL https://api.github.com/repos/vmangos/core/releases/tags/db_latest \
              | grep -o '"browser_download_url": *"[^"]*"' | head -1 | cut -d'"' -f4 || true)
        [ -n "$url" ] || {
            echo "Could not download the world snapshot. Get it from"
            echo "  https://github.com/vmangos/core/releases/tag/db_latest"
            echo "into azeroth-warfare/ and run: AW_WORLD_DUMP=/work/<file> docker compose --profile setup run --rm setup"
            exit 1
        }
        dump="$work/$(basename "$url")"
        curl -fL "$url" -o "$dump"
    fi
    case "$dump" in
        *.7z)  7z x -o"$work" "$dump" >/dev/null ;;
        *.zip) unzip -q -o "$dump" -d "$work" ;;
        *.sql) cp "$dump" "$work/" ;;
    esac
    # Older snapshots name it world*.sql, newer ones mysql-dump/mangos.sql.
    world=$(find "$work" -name '*.sql' | grep -iE 'world|mangos' | head -1 || true)
    [ -n "$world" ] || world=$(find "$work" -name '*.sql' | head -1)
    echo "Loading $(basename "$world") into mangos..."
    "${ROOT[@]}" mangos < "$world"
    rm -rf "$work"
fi

# Migrations are idempotent (each records itself in `migrations`).
for db in logon:realmd characters:characters logs:logs world:mangos; do
    kind=${db%%:*}; name=${db##*:}
    for f in $(ls "$SQL"/migrations/*_"$kind".sql 2>/dev/null | sort); do
        "${ROOT[@]}" "$name" < "$f"
    done
done

# Azeroth Warfare's own world changes.
for f in $(ls "$SQL"/custom/azeroth_warfare/*.sql | sort); do
    echo "Applying $(basename "$f")"
    "${ROOT[@]}" mangos < "$f"
done

"${ROOT[@]}" realmd <<SQL
DELETE FROM realmlist WHERE id = 1;
INSERT INTO realmlist (id, name, address, localAddress, port, icon, realmflags, timezone, allowedSecurityLevel, population, gamebuild_min, gamebuild_max)
VALUES (1, '${AW_REALM_NAME:-Azeroth Warfare}', '${AW_PUBLIC_ADDRESS:-127.0.0.1}', '${AW_PUBLIC_ADDRESS:-127.0.0.1}', 8085, 1, 0, 1, 0, 0, 5875, 5875);
SQL
echo "Databases ready."
