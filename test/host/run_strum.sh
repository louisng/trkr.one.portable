#!/bin/sh
# The device side of strum: trkr format, conversion, saving, timing rules.
#   test/host/run_strum.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
g++ -std=c++17 -O1 -Wall -I"$HERE/stubs" -o "$WORK/strum_test" "$HERE/strum_test.cpp" \
  "$HERE/../../src/net/TrkrConvert.cpp" "$HERE/../../src/net/TrkrSong.cpp" "$HERE/../../src/seq/Song.cpp"
"$WORK/strum_test"
