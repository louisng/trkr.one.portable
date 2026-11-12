#pragma once
// A song in memory: the same model as trkr.one (tracks, patterns, order),
// stored sparsely (only the cells that hold notes) so generated songs fit in
// RAM. Read from / written to the "TRKR 1" line format that trkr.one serves
// to devices (see trkr/public/lib/songtext.js).
#include <Arduino.h>
#include <stdint.h>

#include <vector>

namespace trkr {

static constexpr uint8_t MAX_TRACKS = 8;
static constexpr uint8_t MAX_NOTES = 4;
static constexpr uint16_t MAX_STEPS = 128;
static constexpr uint16_t MAX_PATTERNS = 64;
static constexpr uint16_t MAX_ORDER = 256;
static constexpr uint32_t MAX_EVENTS = 3000;  // ~40 KB while converting; bigger songs are refused

enum class TrackType : uint8_t { Drum, Bass, Chords, Lead, Synth };

struct Track {
  char name[16] = "Track";
  char voice[12] = "ep";
  TrackType type = TrackType::Synth;
  uint8_t ch = 1;      // MIDI channel 1-16
  uint8_t vol = 80;    // 0-100
  bool mute = false;
  uint8_t strum = 0;   // spread chord notes over this many 1/24 steps
};

// One note (or chord) in a pattern.
struct Event {
  uint8_t step;
  uint8_t track;
  uint8_t vel;
  uint8_t count;            // notes used in `notes`
  uint8_t notes[MAX_NOTES];
  uint16_t len24;           // length in 1/24 steps
  int8_t delay;             // micro-timing in 1/24 steps (-12..12)
};

struct Pattern {
  uint16_t len = 16;
  char name[12] = "";
  std::vector<Event> events;  // sorted by step
};

struct Song {
  char name[48] = "Untitled";
  uint16_t bpm = 100;
  uint8_t swing = 0;   // percent of a step
  uint8_t key = 0;
  uint8_t scale = 0;
  uint8_t trackCount = 0;
  Track tracks[MAX_TRACKS];
  std::vector<Pattern> patterns;
  std::vector<uint8_t> order;

  void clear();
  uint32_t eventCount() const;
  uint32_t totalSteps() const;
};

// Streaming parser: feed it lines; finish() validates.
class Parser {
 public:
  explicit Parser(Song& s) : s_(s) { s_.clear(); }
  bool line(const char* text);  // false = fatal error (see error())
  bool finish();
  const char* error() const { return err_; }

 private:
  Song& s_;
  bool header_ = false;
  bool ended_ = false;
  const char* err_ = nullptr;
};

bool load(Stream& in, Song& out, const char** error = nullptr);
void save(Print& out, const Song& s);

const char* typeName(TrackType t);

}  // namespace trkr
