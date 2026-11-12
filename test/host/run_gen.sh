#!/bin/sh
# Checks that the firmware's generator makes exactly the same songs as
# trkr.one's, for every genre and a handful of seeds.
#   test/host/run_gen.sh /path/to/trkr [seed ...]
set -e
TRKR="${1:?path to the trkr repo}"; shift
SEEDS="${*:-1 2 3 42 12345 987654321}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
mkdir -p "$WORK/c" "$WORK/js"
g++ -std=c++17 -O1 -Wall -I"$HERE/stubs" -o "$WORK/gen_test" "$HERE/gen_test.cpp" \
  "$HERE/../../src/gen/Generator.cpp" "$HERE/../../src/gen/GenTheory.cpp" \
  "$HERE/../../src/gen/GenreData.cpp" "$HERE/../../src/net/TrkrSong.cpp"
"$WORK/gen_test" "$WORK/c" $SEEDS
node --input-type=module -e "
import { generateSong } from '$TRKR/public/lib/generator.js';
import { songToText } from '$TRKR/public/lib/songtext.js';
import { GENRES } from '$TRKR/public/lib/genres.js';
import { chordScale, parseChord, mod12 } from '$TRKR/public/lib/theory.js';
import { writeFileSync } from 'node:fs';
for (const id of Object.keys(GENRES)) for (const seed of process.argv.slice(1)) {
  const song = generateSong({ genre: { id }, seed: +seed });
  writeFileSync('$WORK/js/' + id + '_' + seed + '.trk', songToText(song));
  const mask = pcs => pcs.reduce((m, p) => m | (1 << mod12(p)), 0);
  let meta = '';
  for (const c of song.meta.chords) {
    const ch = parseChord(c.sym);
    meta += c.s + ' ' + c.sym + ' ' + mask(c.pcs) + ' ' + mask(chordScale(ch, song.key, song.scale)) + '\\n';
  }
  for (const s of song.meta.sections) meta += s.name + ' ' + s.bar + ' ' + s.bars + ' ' + s.energy.toFixed(2) + '\\n';
  writeFileSync('$WORK/js/' + id + '_' + seed + '.meta', meta);
}" $SEEDS 2>/dev/null
same=0; diff=0
for f in "$WORK"/js/*.trk "$WORK"/js/*.meta; do
  n=$(basename "$f")
  if cmp -s "$f" "$WORK/c/$n"; then same=$((same+1)); else diff=$((diff+1)); echo "DIFFERS: $n"; fi
done
echo "identical: $same  different: $diff"
[ "$diff" -eq 0 ]
