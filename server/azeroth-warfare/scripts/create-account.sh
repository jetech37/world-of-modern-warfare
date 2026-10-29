#!/usr/bin/env bash
# Azeroth Warfare: make (or reset) a login account; works before the world server has map data.
#   docker compose run --rm realmd /opt/aw/scripts/create-account.sh NAME PASSWORD [gmlevel 0-6]
# The verifier is made by the server's own SRP6 code (aw-account).
set -euo pipefail
[ $# -ge 2 ] || { echo "usage: create-account.sh NAME PASSWORD [gmlevel 0-6]"; exit 2; }
/opt/aw/bin/aw-account "$@" | mariadb -h "${AW_DB_HOST:-db}" -umangos "-p${AW_DB_PASSWORD:-mangos}" realmd
echo "Account $1 ready."
