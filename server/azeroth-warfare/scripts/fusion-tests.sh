#!/usr/bin/env bash
# Azeroth Warfare: build and run the fusion rules' unit tests (src/fusion) in a small container, in
# seconds instead of the full image build. Run from azeroth-warfare/:  ./scripts/fusion-tests.sh
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$here/.." && pwd)
docker build -q -t azeroth-warfare-fusion-tests -f "$here/Dockerfile.fusion-tests" "$here" >/dev/null
# A failed build must not fall through to the previous build's test binary.
docker run --rm -v "$repo":/repo:ro -v aw-fusion-build:/build azeroth-warfare-fusion-tests bash -euo pipefail -c '
  cmake -S /repo/src/fusion -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  rm -f /build/fusion_tests
  cmake --build /build -- -j 6 2>&1 | tail -20
  /build/fusion_tests
'
