#include "Sequencer.h"

#include <Arduino.h>

#include "../audio/Monitor.h"
#include "../midi/MidiOut.h"

namespace seq {

static song::Song songData;
static bool isLoaded = false;
static bool running = false;
static uint32_t absStep = 0;       // next step to schedule
static uint32_t playingStep = 0;   // last step scheduled
static uint16_t slot = 0;          // order index of absStep
static uint16_t inSlot = 0;        // step within that pattern
static uint32_t nextAt = 0;        // micros() of the next step
static uint32_t stepUs = 150000;

// Pending note-ons and note-offs, in no particular order.
struct Pending {
  uint32_t at;
  uint8_t ch, note, vel;
  uint8_t kind;  // 0 = off, 1 = on, 2 = drum hit (speaker one-shot)
  bool active;
};
static constexpr uint8_t QUEUE = 128;
static Pending queue[QUEUE];

// GM drum note → speaker drum pad (matches the Cardputer's 12 pads).
static int8_t padFor(uint8_t note) {
  switch (note) {
    case 35: case 36: return 0;
    case 38: case 40: return 1;
    case 37: return 2;
    case 39: return 3;
    case 42: case 44: case 54: case 70: return 4;
    case 46: return 5;
    case 41: case 43: return 6;
    case 45: case 47: return 7;
    case 48: case 50: return 8;
    case 49: case 52: case 55: case 57: return 9;
    case 51: case 53: case 59: return 10;
    case 56: return 11;
    default: return 2;
  }
}

static void push(uint32_t at, uint8_t ch, uint8_t note, uint8_t vel, uint8_t kind) {
  for (auto& p : queue)
    if (!p.active) {
      p = {at, ch, note, vel, kind, true};
      return;
    }
  // Queue full: play it now rather than drop a note-off.
  if (kind == 0) midi::noteOff(ch, note);
}

static void fire(const Pending& p) {
  if (p.kind == 0) {
    midi::noteOff(p.ch, p.note);
    if (p.ch != 10) monitor::noteOff(p.note);
  } else {
    midi::noteOn(p.ch, p.note, p.vel);
    if (p.kind == 2) monitor::drum(padFor(p.note));
    else monitor::noteOn(p.note);
  }
}

void begin() {
  for (auto& p : queue) p.active = false;
}

song::Song& current() { return songData; }
bool loaded() { return isLoaded; }
void setLoaded(bool on) {
  if (!on) stop();
  isLoaded = on;
}
bool playing() { return running; }
uint32_t position() { return playingStep; }
uint16_t orderIndex() { return slot; }
uint16_t bar() { return playingStep / 16 + 1; }

static void computeTempo() { stepUs = 60000000UL / (songData.bpm ? songData.bpm : 100) / 4; }
void changed() { computeTempo(); }

static void seek(uint32_t step) {
  slot = 0;
  inSlot = 0;
  absStep = 0;
  while (slot < songData.order.size()) {
    uint16_t len = songData.patterns[songData.order[slot]].len;
    if (step < absStep + len) {
      inSlot = step - absStep;
      absStep = step;
      return;
    }
    absStep += len;
    slot++;
  }
  slot = 0;
  absStep = 0;
}

void play(uint32_t fromStep) {
  if (!isLoaded) return;
  stop();
  computeTempo();
  seek(fromStep);
  nextAt = micros() + 2000;
  running = true;
}

void stop() {
  if (!running) return;
  running = false;
  for (auto& p : queue)
    if (p.active) {
      if (p.kind == 0) fire(p);
      p.active = false;
    }
  midi::allNotesOff();
  monitor::allOff();
}

static void scheduleStep(uint32_t t0) {
  const song::Pattern& pat = songData.patterns[songData.order[slot]];
  bool odd = inSlot & 1;
  uint32_t swingUs = odd ? (uint32_t)songData.swing * stepUs / 100 : 0;
  for (const auto& e : pat.events) {
    if (e.step < inSlot) continue;
    if (e.step > inSlot) break;
    const song::Track& t = songData.tracks[e.track];
    if (t.mute) continue;
    int32_t off = (int32_t)e.delay * (int32_t)stepUs / 24 + (int32_t)swingUs;
    uint32_t on = t0 + (off > 0 ? off : 0);
    uint32_t dur = (uint32_t)e.len24 * stepUs / 24;
    if (dur < 5000) dur = 5000;
    bool drum = t.type == song::TrackType::Drum;
    for (uint8_t k = 0; k < e.count; k++) {
      push(on, t.ch, e.notes[k], e.vel, drum ? 2 : 1);
      push(on + dur - 1000, t.ch, e.notes[k], 0, 0);
    }
  }
  playingStep = absStep;
  // advance
  absStep++;
  inSlot++;
  if (inSlot >= pat.len) {
    inSlot = 0;
    slot++;
    if (slot >= songData.order.size()) {  // loop the song
      slot = 0;
      absStep = 0;
    }
  }
}

void update() {
  if (running) {
    uint32_t now = micros();
    // Schedule a little ahead so a slow screen redraw doesn't delay the beat.
    while ((int32_t)(now + 4000 - nextAt) >= 0) {
      scheduleStep(nextAt);
      nextAt += stepUs;
    }
  }
  uint32_t now = micros();
  // Offs first, so a re-struck note isn't cut by its own previous off.
  for (uint8_t pass = 0; pass < 2; pass++)
    for (auto& p : queue)
      if (p.active && (p.kind == 0) == (pass == 0) && (int32_t)(now - p.at) >= 0) {
        p.active = false;
        fire(p);
      }
}

}  // namespace seq
