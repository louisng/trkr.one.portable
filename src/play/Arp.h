#pragma once
// The arpeggiator, shared by arp steps (the A effect) and live playing (fn+a). Same modes, rates and
// maths as trkr.one's arp.js, so a song sounds the same on both.
#include <stdint.h>

namespace arp {

enum Mode : uint8_t { UP, DOWN, UPDOWN, INCLUSIVE, ORDER, RANDOM, CHORD, MODE_COUNT };
static constexpr uint8_t STEP_MODES = 6;   // arp steps use UP..RANDOM (CHORD is live only)
static constexpr uint8_t RATE_COUNT = 6;   // 1/4 1/8 1/8T 1/16 1/16T 1/32
static constexpr uint8_t MAX_SEQ = 64;

const char* modeName(uint8_t mode);
const char* rateName(uint8_t rate);
// Engine ticks (12 per step, 48 per beat) between notes at a rate.
uint8_t rateTicks(uint8_t rate);

// An arp step's settings in one byte (the A effect's value, and trkr.one's): bits 0-2 mode,
// bits 3-5 rate, bits 6-7 octaves - 1. False if the byte isn't a valid setting.
bool decode(uint8_t code, uint8_t* mode, uint8_t* rate, uint8_t* octaves);
uint8_t encode(uint8_t mode, uint8_t rate, uint8_t octaves);

// One cycle of single notes for these notes (in the order they were pressed). Returns the count.
uint8_t sequence(const uint8_t* notes, uint8_t n, uint8_t mode, uint8_t octaves, uint8_t* out);
// Which note the k-th hit of a RANDOM arp plays (the same on trkr.one).
uint8_t randomIndex(uint32_t k, uint8_t n);

// Live arp: keys go down and up, step() gives what to play on each beat of the arp's clock.
// With HOLD, every key pressed joins the held notes (in the order pressed) until clear().
struct Live {
  uint8_t mode = UP, rate = 3, octaves = 1, gate = 50, swing = 50;
  bool hold = false;
  uint8_t down[16];
  uint8_t nDown = 0;
  uint8_t latched[16];
  uint8_t nLatched = 0;
  uint16_t pos = 0;
  uint32_t count = 0;

  void press(uint8_t note);
  void release(uint8_t note);
  void stop() { nDown = nLatched = 0; pos = 0; count = 0; }
  // HOLD: let go of every held note (del while the arp is on)
  void clear() { nLatched = 0; if (!nDown) { pos = 0; count = 0; } }
  // the notes it plays now, in the order they were pressed; returns how many
  uint8_t pool(uint8_t* out) const;
  void setHold(bool on) {
    hold = on;
    nLatched = 0;
    if (on) for (uint8_t i = 0; i < nDown; i++) latched[nLatched++] = down[i];   // what is held now is held
  }
  bool running() const { return hold ? nLatched > 0 : nDown > 0; }
  // the notes to play now (several for CHORD); returns how many
  uint8_t step(uint8_t* out);
  // after step(): how many ticks late this one is (swing), and how long it sounds
  uint8_t swingTicks() const;
  uint8_t gateTicks() const;
};

}  // namespace arp
