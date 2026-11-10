#!/bin/sh
# Tests the speaker synth on a PC, and renders every genre to a WAV file
# (in $OUT, default a temp folder) so the sound can be judged without flashing.
#   test/host/run_synth.sh [outdir]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$(mktemp -d)}"
SRC="$HERE/../../src"
g++ -std=c++17 -O2 -Wall -o "$OUT/synth_test" "$HERE/synth_test.cpp" "$SRC/audio/SynthCore.cpp"
"$OUT/synth_test"
g++ -std=c++17 -O2 -Wall -I"$HERE" -o "$OUT/render_wav" "$HERE/render_wav.cpp" "$SRC/audio/SynthCore.cpp" \
  "$SRC/gen/Generator.cpp" "$SRC/gen/GenTheory.cpp" "$SRC/gen/GenreData.cpp" "$SRC/song/Song.cpp"
for id in city_pop lofi boom_bap synthwave house neo_soul funk reggae trap bossa_nova dnb disco ambient synth_pop electronica emo pop; do
  "$OUT/render_wav" $id 5 "$OUT/$id.wav" 75
done
echo "WAV files in $OUT"
