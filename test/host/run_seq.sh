#!/bin/sh
# Plays generated songs through the real sequencer on a virtual clock, as a
# finished song and as a stream of patterns, and compares the MIDI events.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$(mktemp -d)/seq_test"
g++ -std=c++17 -O1 -Wall -I"$HERE" -o "$OUT" "$HERE/seq_test.cpp" \
  "$HERE/../../src/play/Sequencer.cpp" "$HERE/../../src/gen/Generator.cpp" "$HERE/../../src/gen/GenTheory.cpp" \
  "$HERE/../../src/gen/GenreData.cpp" "$HERE/../../src/song/Song.cpp"
"$OUT"
