#pragma once
// Genre "DNA" tables (generated from trkr's public/lib/genres.js by
// scripts/export-cpp-genres.mjs). Read-only, lives in flash.
#include <stdint.h>

namespace gen {

struct StrList { const char* const* v; uint8_t n; };
struct Weighted { double v; double w; };
struct Bank { const char* section; StrList progs; };  // each prog: "tok tok tok tok"
struct ArrEntry { const char* name; uint8_t bars; double energy; };
struct DrumSet { const char* A; const char* B; const char* low; const char* open; const char* sixteenths; };

struct Genre {
  const char* id;
  const char* name;
  uint16_t bpmLo, bpmHi;
  double swing;
  uint8_t swingMask;  // which parts swing: 1 kick, 2 snare, 4 hats, 8 perc
  const Weighted* scales; uint8_t nScales;
  const Bank* banks; uint8_t nBanks;
  const Weighted* hr; uint8_t nHr;
  double push;
  const char* voiceStyle; uint8_t vLo, vHi, vMax;
  uint8_t strum; bool stab;
  StrList comp[3];  // low, mid, high
  StrList bass[3];
  DrumSet kick, snare, hats;
  uint8_t percNote; bool percAlways; const char* percA;
  uint8_t hatsNote;
  double feelSnare, feelKick, feelHats, humanize, velJitter;
  double mDensity; uint8_t mLo, mHi; double mSync, mLeap; uint8_t mBars; double mRepeat, mStrong, mLegato; StrList mSections;
  bool hasExtra; uint8_t extraKind;  // 0 pad, 1 arp
  const char* extraVoice; StrList extraSections; uint8_t exLo, exHi; int8_t exEvery; double exGain;
  const ArrEntry* arr; uint8_t nArr;
  const char* sDrums; const char* sBass; const char* sChords; const char* sLead;
  bool fills;
};

extern const Genre* const GENRE_LIST[];
extern const int GENRE_COUNT;

}  // namespace gen
