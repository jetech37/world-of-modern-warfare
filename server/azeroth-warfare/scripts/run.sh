#!/usr/bin/env bash
# Azeroth Warfare: write the config from the environment and start realmd or mangosd.
set -euo pipefail
ETC=/opt/aw/etc
HOST=${AW_DB_HOST:-db}
PASS=${AW_DB_PASSWORD:-mangos}
db() { echo "\"$HOST;3306;mangos;$PASS;$1\""; }

case "$1" in
  realmd)
    sed -e "s|^LoginDatabaseInfo = .*|LoginDatabaseInfo = $(db realmd)|" \
        "$ETC/realmd.conf.dist" > "$ETC/realmd.conf"
    exec /opt/aw/bin/realmd -c "$ETC/realmd.conf"
    ;;
  mangosd)
    mkdir -p /opt/aw/data/fusion
    [ -f /opt/aw/data/fusion/weapons.json ] || cp /opt/aw/seed/weapons.json /opt/aw/data/fusion/weapons.json
    # The catalog ships with the build: refreshed every start (a custom one goes in Fusion.CatalogFile).
    # A build without one (it is made from an MW2 install) plays from weapons.json.
    if [ -f /opt/aw/seed/catalog.json ]; then
        cp /opt/aw/seed/catalog.json /opt/aw/data/fusion/catalog.json
    else
        echo "No fusion catalog in this build: playing from weapons.json (see private-assets.md)."
    fi
    for d in 5875/dbc maps; do
        [ -d "/opt/aw/data/$d" ] || { echo "Missing /opt/aw/data/$d: run scripts/extract-data.sh against your 1.12.1 client first."; exit 1; }
    done
    sed -e "s|^LoginDatabase.Info = .*|LoginDatabase.Info = $(db realmd)|" \
        -e "s|^WorldDatabase.Info = .*|WorldDatabase.Info = $(db mangos)|" \
        -e "s|^CharacterDatabase.Info = .*|CharacterDatabase.Info = $(db characters)|" \
        -e "s|^LogsDatabase.Info = .*|LogsDatabase.Info = $(db logs)|" \
        -e "s|^DataDir = .*|DataDir = \"/opt/aw/data\"|" \
        "$ETC/mangosd.conf.dist" > "$ETC/mangosd.conf"
    exec /opt/aw/bin/mangosd -c "$ETC/mangosd.conf"
    ;;
  *) echo "usage: run.sh realmd|mangosd"; exit 2 ;;
esac
