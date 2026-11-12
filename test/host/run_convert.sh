#!/bin/sh
# Checks the website <-> device song converter on songs trkr.one really makes
# (every genre, a handful of seeds), using the firmware's own song file code.
#   test/host/run_convert.sh /path/to/trkr [seed ...]
set -e
TRKR="${1:?path to the trkr repo}"; shift
SEEDS="${*:-1 2 3 42 12345 987654321}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
mkdir -p "$WORK/js"
node --input-type=module -e "
import { generateSong } from '$TRKR/public/lib/generator.js';
import { songToText } from '$TRKR/public/lib/songtext.js';
import { GENRES } from '$TRKR/public/lib/genres.js';
import { writeFileSync } from 'node:fs';
for (const id of Object.keys(GENRES)) for (const seed of process.argv.slice(1)) {
  writeFileSync('$WORK/js/' + id + '_' + seed + '.trk', songToText(generateSong({ genre: { id }, seed: +seed })));
}" $SEEDS 2>/dev/null
g++ -std=c++17 -O1 -Wall -I"$HERE/stubs" -o "$WORK/convert_test" "$HERE/convert_test.cpp" \
  "$HERE/../../src/net/TrkrConvert.cpp" "$HERE/../../src/net/TrkrSong.cpp" "$HERE/../../src/seq/Song.cpp"
"$WORK/convert_test" "$WORK"/js/*.trk
