#!/bin/sh
# Proves a song that fits in 8 patterns per track is saved byte-for-byte as the
# original firmware saved it. Needs the original song code from git:
#   test/host/run_compat.sh <git ref of the original, e.g. origin/main~N or a commit>
set -e
REF="${1:?git ref of the firmware to compare against}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$HERE/../.."
WORK="$(mktemp -d)"
mkdir -p "$WORK/old/seq"
git -C "$REPO" show "$REF:src/seq/Song.h" > "$WORK/old/seq/Song.h"
git -C "$REPO" show "$REF:src/seq/Song.cpp" > "$WORK/old/seq/Song.cpp"
g++ -std=c++17 -I"$HERE/stubs" -I"$WORK/old" -o "$WORK/old_w" "$HERE/compat_write.cpp" "$WORK/old/seq/Song.cpp"
g++ -std=c++17 -I"$HERE/stubs" -I"$REPO/src" -o "$WORK/new_w" "$HERE/compat_write.cpp" "$REPO/src/seq/Song.cpp"
"$WORK/old_w" "$WORK/old.cms"; "$WORK/new_w" "$WORK/new.cms"
echo "original: $(wc -c < "$WORK/old.cms") bytes, version byte $(od -An -tu1 -j4 -N1 "$WORK/old.cms" | tr -d ' ')"
echo "new:      $(wc -c < "$WORK/new.cms") bytes, version byte $(od -An -tu1 -j4 -N1 "$WORK/new.cms" | tr -d ' ')"
cmp "$WORK/old.cms" "$WORK/new.cms" && echo "IDENTICAL: older firmware can still read songs that fit in 8 patterns"
# and the new code reads the original's file and saves it back unchanged
"$WORK/new_w" "$WORK/again.cms" "$WORK/old.cms"
cmp "$WORK/old.cms" "$WORK/again.cms" && echo "IDENTICAL: the new code reads an old song and saves it back unchanged"
