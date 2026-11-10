// The play-along rule: what each mode lets through.
#include <cstdio>
#include "../../src/gen/Along.h"
using gen::Along;
static int fails = 0;
static void expect(int got, int want, const char* what) { if (got != want) { fails++; printf("  FAIL %s: got %d want %d\n", what, got, want); } }
int main() {
  const uint16_t Cmaj = (1 << 0) | (1 << 4) | (1 << 7);                       // C E G
  const uint16_t Cscale = Cmaj | (1 << 2) | (1 << 5) | (1 << 9) | (1 << 11);  // C major scale
  // Free: everything sounds
  expect(gen::alongNote(Along::Free, Cmaj, Cscale, 61), 61, "free C#");
  // Chord: only C E G (any octave); everything else is silent
  expect(gen::alongNote(Along::Chord, Cmaj, Cscale, 60), 60, "chord C");
  expect(gen::alongNote(Along::Chord, Cmaj, Cscale, 76), 76, "chord E5");
  expect(gen::alongNote(Along::Chord, Cmaj, Cscale, 62), -1, "chord D silent");
  expect(gen::alongNote(Along::Chord, Cmaj, Cscale, 61), -1, "chord C# silent");
  // Scale: the white keys of C major sound, the black keys do not
  expect(gen::alongNote(Along::Scale, Cmaj, Cscale, 62), 62, "scale D");
  expect(gen::alongNote(Along::Scale, Cmaj, Cscale, 69), 69, "scale A");
  expect(gen::alongNote(Along::Scale, Cmaj, Cscale, 61), -1, "scale C# silent");
  // Snap: a note outside the chord moves to the nearest chord note, lower one winning a tie
  expect(gen::alongNote(Along::Snap, Cmaj, Cscale, 60), 60, "snap C stays");
  expect(gen::alongNote(Along::Snap, Cmaj, Cscale, 61), 60, "snap C# -> C");
  expect(gen::alongNote(Along::Snap, Cmaj, Cscale, 62), 60, "snap D -> C (tie, lower)");
  expect(gen::alongNote(Along::Snap, Cmaj, Cscale, 63), 64, "snap D# -> E");
  expect(gen::alongNote(Along::Snap, Cmaj, Cscale, 66), 67, "snap F# -> G");
  expect(gen::alongNote(Along::Snap, Cmaj, Cscale, 71), 72, "snap B -> C");
  expect(gen::alongNote(Along::Snap, Cmaj, Cscale, 0), 0, "snap lowest note");
  expect(gen::alongNote(Along::Snap, Cmaj, Cscale, 127), 127, "snap highest note (G)");
  // Snap never lands outside the chord
  for (int n = 12; n < 120; n++) {
    int r = gen::alongNote(Along::Snap, Cmaj, Cscale, n);
    if (r < 0 || !((Cmaj >> (r % 12)) & 1)) { fails++; printf("  FAIL snap %d -> %d not a chord tone\n", n, r); }
  }
  printf(fails ? "FAILED (%d)\n" : "play-along rules: all good\n", fails);
  return fails ? 1 : 0;
}
