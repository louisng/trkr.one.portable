#!/bin/sh
# Scenes: shared parts <-> `lk` lines, and the converter's one stored part.
#   test/host/run_scenes.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
g++ -std=c++17 -O1 -Wall -I"$HERE/stubs" -o "$WORK/scenes_test" "$HERE/scenes_test.cpp" \
  "$HERE/../../src/net/TrkrConvert.cpp" "$HERE/../../src/net/TrkrSong.cpp" "$HERE/../../src/seq/Song.cpp"
"$WORK/scenes_test"
