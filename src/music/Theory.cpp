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

const char* noteNameFlat(uint8_t pc) {
  static const char* const N[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
  return N[pc % 12];
}
bool keyUsesFlats(uint8_t root, uint8_t scale) {
  const uint8_t major = isMinorFamily(scale) ? (root + 3) % 12 : root % 12;
  return major == 5 || major == 10 || major == 3 || major == 8 || major == 1 || major == 6;
}

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

// ── Chord bank ───────────────────────────────────────────────────────────────
struct BankQuality {
  const char* label;
  uint8_t n;
  int8_t iv[4];
};
static const BankQuality BANK[BANK_QUALITIES] = {
    {"M", 3, {0, 4, 7}},        {"m", 3, {0, 3, 7}},        {"7", 4, {0, 4, 7, 10}},
    {"M7", 4, {0, 4, 7, 11}},   {"m7", 4, {0, 3, 7, 10}},   {"s4", 3, {0, 5, 7}},
    {"s2", 3, {0, 2, 7}},       {"a9", 4, {0, 4, 7, 14}},   {"m7b", 4, {0, 3, 6, 10}},
    {"o", 3, {0, 3, 6}},        {"6", 4, {0, 4, 7, 9}},     {"m6", 4, {0, 3, 7, 9}},
    {"+", 3, {0, 4, 8}},
};

const char* bankQualityLabel(uint8_t q) { return q < BANK_QUALITIES ? BANK[q].label : ""; }

static bool hasIv(const int* iv, uint8_t n, int v) {
  for (uint8_t i = 0; i < n; i++)
    if (iv[i] == v) return true;
  return false;
}

// Reads the name straight off the final set of intervals, so it stays right
// whatever combination of type and modifiers made them.
static void bankName(uint8_t rootPc, const int* iv, uint8_t n, int bassIv, bool flats, char* out, size_t cap) {
  auto nm = [&](uint8_t pc) { return flats ? noteNameFlat(pc % 12) : noteName(pc % 12); };
  const int third = hasIv(iv, n, 3) ? 3 : hasIv(iv, n, 4) ? 4 : hasIv(iv, n, 5) ? 5 : hasIv(iv, n, 2) ? 2 : 0;
  const int fifth = hasIv(iv, n, 6) ? 6 : hasIv(iv, n, 8) ? 8 : 7;
  const bool dim = third == 3 && fifth == 6;
  const int seventh = hasIv(iv, n, 11) ? 11 : hasIv(iv, n, 10) ? 10 : (dim && hasIv(iv, n, 9)) ? 9 : 0;
  const bool sixth = !seventh && hasIv(iv, n, 9);
  const bool ninth = hasIv(iv, n, 14);

  char q[24] = "";
  if (dim) snprintf(q, sizeof(q), seventh == 9 ? (ninth ? "dim9" : "dim7") : seventh == 10 ? (ninth ? "m9b5" : "m7b5") : "dim");
  else if (third == 3) {
    if (seventh == 11) snprintf(q, sizeof(q), ninth ? "mMaj9" : "mMaj7");
    else if (seventh == 10) snprintf(q, sizeof(q), ninth ? "m9" : "m7");
    else if (sixth) snprintf(q, sizeof(q), ninth ? "m6add9" : "m6");
    else snprintf(q, sizeof(q), ninth ? "madd9" : "m");
  } else {
    const char* aug = fifth == 8 ? "aug" : "";
    if (seventh == 11) snprintf(q, sizeof(q), "%s%s", aug, ninth ? "maj9" : "maj7");
    else if (seventh == 10) snprintf(q, sizeof(q), "%s%s", aug, ninth ? "9" : "7");
    else if (sixth) snprintf(q, sizeof(q), ninth ? "6add9" : "6");
    else snprintf(q, sizeof(q), "%s%s", aug, ninth ? "add9" : "");
  }
  const char* s = third == 5 ? "sus4" : third == 2 ? "sus2" : "";
  char tail[8] = "";
  if (bassIv >= 0) snprintf(tail, sizeof(tail), "/%s", nm((rootPc + bassIv) % 12));
  snprintf(out, cap, "%s%s%s%s", nm(rootPc), q, s, tail);
}

Chord buildBankChord(uint8_t rootPc, uint8_t quality, uint8_t baseNote, const BankMods& mods) {
  Chord c;
  if (quality >= BANK_QUALITIES) return c;
  const BankQuality& bq = BANK[quality];
  int iv[MAX_CHORD_NOTES + 1];
  uint8_t n = 0;
  for (uint8_t i = 0; i < bq.n; i++) iv[n++] = bq.iv[i];

  auto idx = [&](int v) { for (uint8_t i = 0; i < n; i++) if (iv[i] == v) return (int)i; return -1; };
  if (mods.swap) {
    int t = idx(4) >= 0 ? idx(4) : idx(3);
    if (t >= 0) {
      const bool wasMajor = iv[t] == 4;
      iv[t] = wasMajor ? 3 : 4;
      int f = idx(8) >= 0 ? idx(8) : idx(6);
      if (f >= 0 && (idx(8) >= 0 || !wasMajor)) iv[f] = 7;  // aug -> 5, dim -> 5 when it turns major
    }
  }
  if (mods.sus4) {
    int t = idx(4) >= 0 ? idx(4) : idx(3) >= 0 ? idx(3) : idx(2);
    if (t >= 0) iv[t] = 5;
  }
  if (mods.seventh && idx(10) < 0 && idx(11) < 0 && !(idx(6) >= 0 && idx(9) >= 0)) iv[n++] = 10;
  if (mods.ninth && idx(14) < 0) iv[n++] = 14;

  // The bass note: the chord's 3rd, 5th or 7th, an octave below the chord.
  int bassIv = -1;
  if (mods.bass == 1) { int t = idx(3) >= 0 ? 3 : idx(4) >= 0 ? 4 : idx(5) >= 0 ? 5 : idx(2) >= 0 ? 2 : -1; bassIv = t; }
  else if (mods.bass == 2) bassIv = idx(6) >= 0 ? 6 : idx(8) >= 0 ? 8 : 7;
  else if (mods.bass == 3) bassIv = idx(11) >= 0 ? 11 : idx(10) >= 0 ? 10 : 10;

  char name[16];
  bankName(rootPc, iv, n, bassIv, mods.flats, name, sizeof(name));

  // At most 5 notes: with a bass note and a 9th the 5th is the one to leave out.
  int notes[MAX_CHORD_NOTES + 2];
  uint8_t cnt = 0;
  if (bassIv >= 0) notes[cnt++] = (int)baseNote + bassIv - 12;
  for (uint8_t i = 0; i < n; i++) {
    if (bassIv >= 0 && n + 1 > MAX_CHORD_NOTES && iv[i] == 7 && n > 3) continue;
    if (cnt < MAX_CHORD_NOTES) notes[cnt++] = (int)baseNote + iv[i];
  }
  for (uint8_t i = 0; i < cnt; i++) {
    int v = notes[i];
    while (v > 127) v -= 12;
    while (v < 0) v += 12;
    c.notes[c.count++] = (uint8_t)v;
  }
  snprintf(c.name, sizeof(c.name), "%s", name);
  return c;
}

}  // namespace theory
