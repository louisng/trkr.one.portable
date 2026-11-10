#pragma once
// Song generator: genre DNA + seed → a full song. A line-for-line port of
// trkr's public/lib/generator.js: the same seed makes the same song here and
// on trkr.one (checked by test/host against the JavaScript output).
#include <stdint.h>

#include <vector>

#include "../song/Song.h"
#include "GenreData.h"

namespace gen {

struct ChordSpan {
  int16_t start;     // step the chord begins (already moved a 16th early if "pushed")
  char sym[14];
  uint16_t pcs;      // chord tones, as a 12-bit mask of pitch classes
  uint16_t scale;    // notes that fit over this chord, same mask
};
struct SectionSpan {
  char name[10];
  uint16_t bar, bars;
  float energy;
};
// What the UI and the play-along keyboard need to know about a song.
struct Meta {
  std::vector<ChordSpan> chords;
  std::vector<SectionSpan> sections;
  uint8_t genre = 0;
  uint8_t key = 0, scale = 0;
  uint32_t seed = 0;
};

// Receives a song as it is made: first the tempo/key/tracks and chord map,
// then each finished 4-bar pattern in play order. Repeated patterns are sent
// again (a stream keeps nothing). Return false from either to stop early.
class Sink {
 public:
  virtual ~Sink() {}
  virtual bool begin(const song::Song& head, const Meta& meta) = 0;
  virtual bool pattern(song::Pattern&& p) = 0;
};

// Makes one song and streams it into `sink`. False if the sink stopped it.
bool generateStream(const Genre& g, uint32_t seed, Sink& sink);

// The whole song at once, with identical patterns stored once (website
// parity tests, tools). False if it would be too big for the device.
bool generate(const Genre& g, uint32_t seed, song::Song& out, Meta& meta);

const ChordSpan* chordAt(const Meta& m, uint32_t step);
const SectionSpan* sectionAt(const Meta& m, uint32_t bar);

}  // namespace gen
