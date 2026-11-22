#!/bin/sh
# Transpose on the device (pattern, step, song).
#   test/host/run_transpose.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
g++ -std=c++17 -O1 -Wall -I"$HERE/stubs" -o "$WORK/transpose_test" "$HERE/transpose_test.cpp" "$HERE/../../src/seq/Song.cpp"
"$WORK/transpose_test"
