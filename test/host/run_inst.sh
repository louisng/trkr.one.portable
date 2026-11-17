#!/bin/sh
# Track sound settings (`ti` line): format, conversion, saving.
#   test/host/run_inst.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
g++ -std=c++17 -O1 -Wall -I"$HERE/stubs" -o "$WORK/inst_test" "$HERE/inst_test.cpp" \
  "$HERE/../../src/net/TrkrConvert.cpp" "$HERE/../../src/net/TrkrSong.cpp" "$HERE/../../src/seq/Song.cpp"
"$WORK/inst_test"
