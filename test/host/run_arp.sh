#!/bin/sh
# The arpeggiator (same answers as trkr.one) and arp steps through the format and the converter.
#   test/host/run_arp.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
g++ -std=c++17 -O1 -Wall -o "$WORK/arp_test" "$HERE/arp_test.cpp" "$HERE/../../src/play/Arp.cpp"
"$WORK/arp_test"
g++ -std=c++17 -O1 -Wall -I"$HERE/stubs" -o "$WORK/arpstep_test" "$HERE/arpstep_test.cpp" "$HERE/../../src/play/Arp.cpp" \
  "$HERE/../../src/net/TrkrConvert.cpp" "$HERE/../../src/net/TrkrSong.cpp" "$HERE/../../src/seq/Song.cpp"
"$WORK/arpstep_test"
