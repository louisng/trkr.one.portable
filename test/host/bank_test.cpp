// The chord bank plays a chord chart as written. Each row: a chord from a real chart,
// how it is pressed (root, type, modifiers) and the notes it must make.
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../src/music/Theory.h"
using namespace theory;

enum { M, m, S7, M7, m7, s4, s2, a9, m7b, o, six, m6, aug };
static const char* PC[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int fails = 0;
static void check(const char* want, uint8_t root, uint8_t q, BankMods mods, std::vector<int> pcs /*lowest first*/) {
  Chord c = buildBankChord(root, q, 48 + root, mods);
  bool ok = !strcmp(c.name, want) && c.count == pcs.size();
  for (size_t i = 0; ok && i < pcs.size(); i++) ok = (c.notes[i] % 12) == pcs[i];
  if (!ok) {
    fails++;
    printf("FAIL want %-9s got %-9s notes:", want, c.name);
    for (int i = 0; i < c.count; i++) printf(" %d(%s)", c.notes[i], PC[c.notes[i] % 12]);
    printf("\n");
  }
}
int main() {
  BankMods none, nine, sw, sus, sev, b3, b5, b7;
  nine.ninth = true; sw.swap = true; sus.sus4 = true; sev.seventh = true; b3.bass = 1; b5.bass = 2; b7.bass = 3;
  // the chart in the screenshot (key of D, then F)
  check("Bm7", 11, m7, none, {11, 2, 6, 9});
  check("Gadd9", 7, a9, none, {7, 11, 2, 9});
  check("Dadd9", 2, a9, none, {2, 6, 9, 4});
  check("A/C#", 9, M, b3, {1, 9, 1, 4});
  check("A/G", 9, M, b7, {7, 9, 1, 4});
  check("Gmaj7", 7, M7, none, {7, 11, 2, 6});
  check("F#7", 6, S7, none, {6, 10, 1, 4});
  check("Em7", 4, m7, none, {4, 7, 11, 2});
  check("F#m7", 6, m7, none, {6, 9, 1, 4});
  check("Asus4", 9, s4, none, {9, 2, 4});
  check("B7", 11, S7, none, {11, 3, 6, 9});
  check("Dm/F", 2, m, b3, {5, 2, 5, 9});
  BankMods fl; fl.flats = true;
  check("Bb", 10, M, fl, {10, 2, 5});
  check("A#", 10, M, none, {10, 2, 5});
  check("C", 0, M, none, {0, 4, 7});
  check("C6/E", 0, six, b3, {4, 0, 4, 7, 9});
  check("Bbsus2", 10, s2, fl, {10, 0, 5});
  check("Csus4", 0, s4, none, {0, 5, 7});
  check("Bm7b5", 11, m7b, none, {11, 2, 5, 9});
  check("E/G#", 4, M, b3, {8, 4, 8, 11});
  check("Cm", 0, m, none, {0, 3, 7});
  check("Fadd9", 5, a9, none, {5, 9, 0, 7});
  // modifiers
  check("Cmaj9", 0, M7, nine, {0, 4, 7, 11, 2});
  check("Cm9", 0, m7, nine, {0, 3, 7, 10, 2});
  check("C9", 0, S7, nine, {0, 4, 7, 10, 2});
  check("Cmadd9", 0, a9, sw, {0, 3, 7, 2});
  check("Cm6", 0, six, sw, {0, 3, 7, 9});
  check("Cm7", 0, S7, sw, {0, 3, 7, 10});
  check("C7sus4", 0, S7, sus, {0, 5, 7, 10});
  check("C7", 0, M, sev, {0, 4, 7, 10});
  check("Cm7", 0, m, sev, {0, 3, 7, 10});
  check("Cmaj7", 0, M7, sev, {0, 4, 7, 11});   // already has its 7th: unchanged
  check("Caug", 0, aug, none, {0, 4, 8});
  check("Cdim", 0, o, none, {0, 3, 6});
  check("Cm/G", 0, m, b5, {7, 0, 3, 7});
  // five notes at most: a 9th chord with a bass note drops its 5th
  check("C9/E", 0, S7, (BankMods){false, true, false, false, 1}, {4, 0, 4, 10, 2});
  // every type and root makes a sane chord
  for (uint8_t r = 0; r < 12; r++) for (uint8_t q = 0; q < BANK_QUALITIES; q++) for (uint8_t b = 0; b < 4; b++) {
    BankMods mm; mm.bass = b; mm.ninth = (r + q) % 2; mm.seventh = q % 3 == 0;
    Chord c = buildBankChord(r, q, 36 + r, mm);
    if (c.count < 3 || c.count > MAX_CHORD_NOTES || !c.name[0]) { fails++; printf("FAIL sane r=%d q=%d b=%d n=%d\n", r, q, b, c.count); }
    for (int i = 0; i < c.count; i++) if (c.notes[i] > 127) { fails++; printf("FAIL range\n"); }
  }
  if (!keyUsesFlats(5, 0) || keyUsesFlats(2, 0) || !keyUsesFlats(2, 1) || keyUsesFlats(9, 1)) { fails++; printf("FAIL keyUsesFlats\n"); }  // F major, D major, D minor (=F), A minor
  printf("%s\n", fails ? "FAILED" : "all bank chords ok");
  return fails != 0;
}
