#!/bin/sh
# Generated mode's path on a PC: generator -> importer, many seeds.
#   test/host/run_stream.sh [seed ...]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SEEDS="${*:-1 2 3 4 5 6 7 8 9 10 42 99 12345 54321 987654321 555555555}"
WORK="$(mktemp -d)"
g++ -std=c++17 -O1 -Wall -I"$HERE/stubs" -o "$WORK/stream_test" "$HERE/stream_test.cpp" \
  "$HERE/../../src/gen/Generator.cpp" "$HERE/../../src/gen/GenTheory.cpp" "$HERE/../../src/gen/GenreData.cpp" \
  "$HERE/../../src/net/TrkrSong.cpp" "$HERE/../../src/net/TrkrConvert.cpp" "$HERE/../../src/seq/Song.cpp"
"$WORK/stream_test" $SEEDS
