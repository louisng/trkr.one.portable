#include "Theory.h"

#include <stdio.h>
#include <string.h>

namespace theory {

struct ScaleDef {
  const char* name;
  uint8_t steps[7];
};

static const ScaleDef SCALES[SCALE_COUNT] = {
    {"Major", {0, 2, 4, 5, 7, 9, 11}},
    {"Minor", {0, 2, 3, 5, 7, 8, 10}},
    {"Dorian", {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian", {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian", {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolyd", {0, 2, 4, 5, 7, 9, 10}},
    {"HarmMin", {0, 2, 3, 5, 7, 8, 11}},
};

static const char* const NOTE_NAMES[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                           "F#", "G",  "G#", "A",  "A#", "B"};

const char* scaleName(uint8_t scale) { return SCALES[scale % SCALE_COUNT].name; }

int scaleOffset(uint8_t scale, int degree) {
  const ScaleDef& s = SCALES[scale % SCALE_COUNT];
  int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
  int d = degree - oct * 7;
  return s.steps[d] + oct * 12;
}

const char* noteName(uint8_t pc) { return NOTE_NAMES[pc % 12]; }

void formatNote(uint8_t midiNote, char* buf) {
  int octave = (int)(midiNote / 12) - 1;
  snprintf(buf, 5, "%s%d", NOTE_NAMES[midiNote % 12], octave);
}

// ── Chord buttons ────────────────────────────────────────────────────────────
// Buttons 0-6: the diatonic chords of the current scale (I .. vii).
// Buttons 7-12: common borrowed / secondary chords, chosen per scale family.
struct ExtraChord {
  uint8_t rootSemis;  // semitones above key root
  uint8_t third;      // 3 = minor, 4 = major
  uint8_t fifth;      // 6 = dim, 7 = perfect
  const char* label;
};

static const ExtraChord EXTRAS_MAJOR[6] = {
    {3, 4, 7, "bIII"}, {5, 3, 7, "iv"}, {8, 4, 7, "bVI"},
    {10, 4, 7, "bVII"}, {2, 4, 7, "II"}, {4, 4, 7, "III"},
};
static const ExtraChord EXTRAS_MINOR[6] = {
    {5, 4, 7, "IV"}, {7, 4, 7, "V"}, {1, 4, 7, "bII"},
    {0, 4, 7, "I"},  {2, 3, 7, "ii"}, {11, 3, 6, "viio"},
};

static bool isMinorFamily(uint8_t scale) { return scaleOffset(scale, 2) == 3; }

static const char* const ROMAN_UP[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
static const char* const ROMAN_LO[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};

const char* chordButtonLabel(uint8_t scale, uint8_t button) {
  static char buf[8];
  if (button < 7) {
    int third = scaleOffset(scale, button + 2) - scaleOffset(scale, button);
    int fifth = scaleOffset(scale, button + 4) - scaleOffset(scale, button);
    const char* base = (third == 4) ? ROMAN_UP[button] : ROMAN_LO[button];
    // Mark diminished (o) and augmented (+) triads.
    const char* suffix = (fifth == 6) ? "o" : (fifth == 8) ? "+" : "";
    snprintf(buf, sizeof(buf), "%s%s", base, suffix);
    return buf;
  }
  if (button >= CHORD_BUTTONS) return "";
  const ExtraChord* ex = isMinorFamily(scale) ? EXTRAS_MINOR : EXTRAS_MAJOR;
  return ex[button - 7].label;
}

static void chordName(uint8_t rootPc, int third, int fifth, int seventh, bool ninth,
                      char* out, size_t n) {
  const char* q = "";
  if (third == 5) q = "sus4";
  else if (third == 4 && fifth == 8) q = "aug";
  else if (third == 4) q = "";
  else if (third == 3 && fifth == 6) q = "dim";
  else q = "m";

  char sev[8] = "";
  if (seventh >= 0) {
    if (third == 5) snprintf(sev, sizeof(sev), "7");
    else if (third == 4) snprintf(sev, sizeof(sev), seventh == 11 ? "maj7" : "7");
    else if (fifth == 6) { q = ""; snprintf(sev, sizeof(sev), seventh == 9 ? "dim7" : "m7b5"); }
    else snprintf(sev, sizeof(sev), seventh == 11 ? "M7" : "7");
  }
  // sus4 reads better after the 7: C7sus4
  if (third == 5 && seventh >= 0)
    snprintf(out, n, "%s7sus4%s", noteName(rootPc), ninth ? "+9" : "");
  else
    snprintf(out, n, "%s%s%s%s", noteName(rootPc), q, sev, ninth ? "+9" : "");
}

Chord buildChord(uint8_t scale, uint8_t root, uint8_t button, uint8_t bassNote,
                 const ChordMods& mods) {
  Chord c;
  if (button >= CHORD_BUTTONS) return c;

  int rootOff, third, fifth, seventh, ninth;
  if (button < 7) {
    rootOff = scaleOffset(scale, button);
    third = scaleOffset(scale, button + 2) - rootOff;
    fifth = scaleOffset(scale, button + 4) - rootOff;
    seventh = scaleOffset(scale, button + 6) - rootOff;
    ninth = scaleOffset(scale, button + 8) - rootOff;
  } else {
    const ExtraChord& ex = (isMinorFamily(scale) ? EXTRAS_MINOR : EXTRAS_MAJOR)[button - 7];
    rootOff = ex.rootSemis;
    third = ex.third;
    fifth = ex.fifth;
    seventh = (fifth == 6) ? 9 : 10;  // dim7, or dominant / minor 7th
    ninth = 14;
  }

  if (mods.swap) {
    if (third == 4) {
      third = 3;
      if (fifth == 8) fifth = 7;
      if (seventh == 11) seventh = 10;
    } else if (third == 3) {
      third = 4;
      if (fifth == 6) fifth = 7;
      if (seventh == 9) seventh = 10;
    }
  }
  if (mods.sus4) {
    third = 5;
    if (fifth == 6) fifth = 7;
  }

  int intervals[MAX_CHORD_NOTES];
  uint8_t n = 0;
  intervals[n++] = 0;
  intervals[n++] = third;
  intervals[n++] = fifth;
  if (mods.seventh) intervals[n++] = seventh;
  if (mods.ninth) intervals[n++] = ninth;

  int notes[MAX_CHORD_NOTES];
  for (uint8_t i = 0; i < n; i++) notes[i] = (int)bassNote + rootOff + intervals[i];

  // Inversions: lift the lowest note(s) an octave.
  for (uint8_t inv = 0; inv < mods.inversion && inv < n - 1; inv++) {
    uint8_t lo = 0;
    for (uint8_t i = 1; i < n; i++)
      if (notes[i] < notes[lo]) lo = i;
    notes[lo] += 12;
  }

  for (uint8_t i = 0; i < n; i++) {
    int v = notes[i];
    while (v > 127) v -= 12;
    while (v < 0) v += 12;
    c.notes[c.count++] = (uint8_t)v;
  }
  chordName((uint8_t)((root + rootOff) % 12), third, fifth, mods.seventh ? seventh : -1,
            mods.ninth, c.name, sizeof(c.name));
  return c;
}

}  // namespace theory
