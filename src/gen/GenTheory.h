#pragma once
// Chord symbols, Roman numerals, chord scales and voice leading for the
// generator: a port of trkr's public/lib/theory.js (same results).
#include <stdint.h>

#include <string>
#include <vector>

namespace gen {

enum Quality : uint8_t { Q_MAJ, Q_MIN, Q_DOM, Q_HDIM, Q_DIM, Q_AUG, Q_SUS };

struct Chord {
  int root = 0;
  int bass = 0;               // pitch class of the lowest note
  std::vector<int> tones;     // intervals above the root (9ths etc. are > 12)
  Quality quality = Q_MAJ;
  std::string symbol;
};

inline int mod12(int n) { return ((n % 12) + 12) % 12; }
const char* noteName(int pc, bool flats = false);

// Semitone offset of a scale degree (scale index = the firmware's scale list).
int scaleOffset(int scale, int degree);
bool isMinorFamily(int scale);
std::vector<int> scalePcs(int root, int scale);

bool parseChord(const std::string& symbol, Chord& out);
// Roman numeral ("ii9", "bVIImaj7", "Vsus4") in a key → chord. False if unreadable.
bool romanToChord(const std::string& roman, int keyRoot, int keyScale, Chord& out);

std::vector<int> chordPcs(const Chord& c);                      // unique pitch classes
std::vector<int> chordScale(const Chord& c, int keyRoot, int keyScale);
std::vector<int> avoidPcs(const Chord& c, const std::vector<int>& scale);

// `prev` may be null. style: "close" | "drop2" | "rootless" | "spread".
std::vector<int> voiceChord(const Chord& c, const std::vector<int>* prev, int lo, int hi,
                            const std::string& style, int maxNotes);
int nearestPc(int pc, int near, int lo, int hi);

bool contains(const std::vector<int>& v, int x);

}  // namespace gen
