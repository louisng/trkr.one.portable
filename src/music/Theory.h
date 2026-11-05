#pragma once
// Scales, note names and chord construction for the chord buttons.
#include <stdint.h>

namespace theory {

static constexpr uint8_t SCALE_COUNT = 7;
static constexpr uint8_t CHORD_BUTTONS = 13;
static constexpr uint8_t MAX_CHORD_NOTES = 5;

const char* scaleName(uint8_t scale);
// Semitone offset of `degree` (any integer, wraps into higher/lower octaves).
int scaleOffset(uint8_t scale, int degree);

const char* noteName(uint8_t pitchClass);  // "C", "C#", ...
// Writes e.g. "C#4" into buf (>= 5 bytes).
void formatNote(uint8_t midiNote, char* buf);

struct ChordMods {
  bool seventh = false;  // shift
  bool ninth = false;    // ctrl
  bool swap = false;     // opt: major <-> minor
  bool sus4 = false;     // alt
  uint8_t inversion = 0; // 0-2
};

struct Chord {
  uint8_t count = 0;
  uint8_t notes[MAX_CHORD_NOTES];
  char name[12];
};

// Short label for chord button i in the given scale (e.g. "I", "ii", "bVII").
const char* chordButtonLabel(uint8_t scale, uint8_t button);

// Build the chord for a chord button. `bassNote` is the MIDI note of the key
// root in the chord octave (e.g. 48 for C3 in C).
Chord buildChord(uint8_t scale, uint8_t root, uint8_t button, uint8_t bassNote,
                 const ChordMods& mods);

}  // namespace theory
