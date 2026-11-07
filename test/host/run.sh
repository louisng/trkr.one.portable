#!/bin/sh
# Host test for the song parser: builds it with a tiny Arduino shim and
# round-trips a TRKR file (export one from the trkr tracker: FILE → EXPORT .TRK).
#   test/host/run.sh path/to/song.trk
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -I. -o song_test song_test.cpp ../../src/song/Song.cpp
./song_test "$1"
