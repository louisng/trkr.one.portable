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
const char* noteNameFlat(uint8_t pitchClass);  // "C", "Db", ...
// True if a key (root + scale) is normally written with flats (F, Bb, Eb ... and their relative minors).
bool keyUsesFlats(uint8_t root, uint8_t scale);
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
  char name[16];
};

// Short label for chord button i in the given scale (e.g. "I", "ii", "bVII").
const char* chordButtonLabel(uint8_t scale, uint8_t button);

// Build the chord for a chord button. `bassNote` is the MIDI note of the key
// root in the chord octave (e.g. 48 for C3 in C).
Chord buildChord(uint8_t scale, uint8_t root, uint8_t button, uint8_t bassNote,
                 const ChordMods& mods);

// ── Chord bank ───────────────────────────────────────────────────────────────
// Any root x any of 13 chord types, so a chord chart can be played as written
// (Bm7, Gadd9, F#7, Bbsus2 ...), in any key. Modifiers combine with the type:
//   7TH adds a 7th, 9TH adds a 9th, M/m swaps major <-> minor, SUS swaps the 3rd for a 4th.
// `bass` adds a bass note an octave below: 1 = the 3rd, 2 = the 5th, 3 = the 7th
// (A/C#, A/G, Dm/F ...).
static constexpr uint8_t BANK_QUALITIES = 13;
struct BankMods {
  bool seventh = false;
  bool ninth = false;
  bool swap = false;
  bool sus4 = false;
  uint8_t bass = 0;  // 0 none, 1 third, 2 fifth, 3 seventh
  bool flats = false;  // spell black keys as flats (Bb, not A#), for flat keys
};
const char* bankQualityLabel(uint8_t quality);  // short key label: "M", "m", "7", "M7" ...
// `baseNote` is the MIDI note of the root in the chord octave.
Chord buildBankChord(uint8_t rootPc, uint8_t quality, uint8_t baseNote, const BankMods& mods);

}  // namespace theory
