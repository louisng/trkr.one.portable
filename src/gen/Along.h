#pragma once
// The play-along rule for the piano keys, kept free of hardware so it can be
// tested on a PC. Masks are 12-bit pitch-class sets (bit 0 = C).
#include <stdint.h>

namespace gen {

enum class Along : uint8_t { Free, Chord, Snap, Scale };

// Returns the pitch to sound for `note`, or -1 to stay silent.
//   Free   any note
//   Chord  only the chord's notes
//   Snap   a note outside the chord moves to the nearest chord note (lower wins a tie)
//   Scale  only notes of the scale that fits the chord
inline int alongNote(Along mode, uint16_t chordMask, uint16_t scaleMask, int note) {
  if (mode == Along::Free) return note;
  uint16_t mask = mode == Along::Scale ? scaleMask : chordMask;
  if ((mask >> (note % 12)) & 1) return note;
  if (mode != Along::Snap) return -1;
  for (int d = 1; d <= 6; d++) {
    int lo = note - d, hi = note + d;
    if (lo >= 0 && ((mask >> (lo % 12)) & 1)) return lo;
    if (hi <= 127 && ((mask >> (hi % 12)) & 1)) return hi;
  }
  return note;
}

}  // namespace gen
