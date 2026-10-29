#!/usr/bin/env bash
# Azeroth Warfare: extract dbc, maps, vmaps and mmaps from your own WoW 1.12.1 client (build 5875)
# into ./data, using the extractors built into the image. Run on the host from azeroth-warfare/:
#   ./scripts/extract-data.sh /path/to/WoW-1.12.1
# Movement maps take a long while (an hour or more); pass --no-mmaps to skip them for a first test.
set -euo pipefail
CLIENT=${1:?usage: extract-data.sh /path/to/WoW-1.12.1 [--no-mmaps]}
MMAPS=1; [ "${2:-}" = "--no-mmaps" ] && MMAPS=0
OUT=$(pwd)/data
mkdir -p "$OUT"
[ -f "$CLIENT/WoW.exe" ] || echo "warning: no WoW.exe in $CLIENT; is this the 1.12.1 client folder?"
docker run --rm -v "$CLIENT":/client:ro -v "$OUT":/out -w /work azeroth-warfare bash -euc "
  cp -r /client/Data /work/Data
  /opt/aw/bin/Extractors/MapExtractor
  /opt/aw/bin/Extractors/VMapExtractor
  mkdir -p vmaps && /opt/aw/bin/Extractors/VMapAssembler Buildings vmaps
  mkdir -p /out/5875 && mv dbc /out/5875/  # mangosd reads DataDir/<build>/dbc
  mv maps vmaps /out/
  if [ $MMAPS = 1 ]; then
    mkdir -p mmaps && ln -s /out/maps maps && ln -s /out/vmaps vmaps
    cp /opt/aw/share/mmap-config.json config.json
    /opt/aw/bin/Extractors/MoveMapGenerator && mv mmaps /out/
  fi
"
echo "Client data is in $OUT."
