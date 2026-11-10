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
static bool looping = true;
static bool ending = false;        // last step scheduled; waiting for it to sound out
static bool finishedFlag = false;
static PatternSource* source = nullptr;   // stream mode: patterns come from here
static song::Pattern streamPat;           // the pattern being played in stream mode
static bool patNeeded = false;            // stream: the current pattern is used up
static uint32_t streamBase = 0;           // absolute step at which the current song began

// Pending note-ons and note-offs, in no particular order.
struct Pending {
  uint32_t at;
  uint8_t ch, note, vel;
  uint8_t kind;    // 0 = off, 1 = on, 2 = drum hit (speaker one-shot)
  uint8_t timbre;  // speaker sound; 0xFF for drums
  uint8_t owner;   // track number
  bool active;
};
static constexpr uint8_t QUEUE = 192;
static Pending queue[QUEUE];

static void push(uint32_t at, uint8_t ch, uint8_t note, uint8_t vel, uint8_t kind, uint8_t timbre, uint8_t owner) {
  for (auto& p : queue)
    if (!p.active) {
      p = {at, ch, note, vel, kind, timbre, owner, true};
      return;
    }
  // Queue full: play it now rather than drop a note-off.
  if (kind == 0) midi::noteOff(ch, note);
}

static void fire(const Pending& p) {
  if (p.kind == 0) {
    midi::noteOff(p.ch, p.note);
    if (p.timbre != 0xFF) monitor::noteOff(p.note, p.owner);
  } else {
    midi::noteOn(p.ch, p.note, p.vel);
    if (p.kind == 2) monitor::drum(p.note, p.vel);
    else monitor::noteOn(p.note, p.vel, p.timbre, p.owner);
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
uint32_t songPosition() { return source ? playingStep - streamBase : playingStep; }
uint16_t orderIndex() { return slot; }
uint16_t bar() { return songPosition() / 16 + 1; }

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

void playStream(PatternSource* src) {
  stop();
  source = src;
  isLoaded = true;
  patNeeded = true;
  absStep = 0;
  playingStep = 0;
  streamBase = 0;
  inSlot = 0;
  ending = false;
  finishedFlag = false;
  nextAt = micros() + 2000;
  running = true;
}

void adopt(song::Song&& s) {
  stop();
  songData = std::move(s);
  isLoaded = true;
  finishedFlag = false;
  computeTempo();
}
void setLoop(bool loop) { looping = loop; }
bool finished() { return finishedFlag; }

void silenceTrack(uint8_t t) {
  if (t >= songData.trackCount) return;
  if (songData.tracks[t].type == song::TrackType::Drum) return;  // drum hits are one-shots
  for (auto& p : queue)
    if (p.active && p.owner == t) {
      if (p.kind == 0) fire(p);   // let go of notes that are sounding
      p.active = false;
    }
}

void play(uint32_t fromStep) {
  if (!isLoaded) return;
  stop();
  ending = false;
  finishedFlag = false;
  computeTempo();
  seek(fromStep);
  nextAt = micros() + 2000;
  running = true;
}

void stop() {
  source = nullptr;
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
  const song::Pattern& pat = source ? streamPat : songData.patterns[songData.order[slot]];
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
    uint8_t timbre = drum ? 0xFF : monitor::timbreFor(t.voice, (uint8_t)t.type);
    uint32_t strumUs = (uint32_t)t.strum * stepUs / 24;  // chord notes arrive one after another
    for (uint8_t k = 0; k < e.count; k++) {
      uint32_t at = on + k * strumUs;
      push(at, t.ch, e.notes[k], e.vel, drum ? 2 : 1, timbre, e.track);
      push(at + dur - 1000, t.ch, e.notes[k], 0, 0, timbre, e.track);
    }
  }
  playingStep = absStep;
  // advance
  absStep++;
  inSlot++;
  if (source) {
    if (inSlot >= pat.len) {
      inSlot = 0;
      patNeeded = true;  // update() fetches the next pattern before the next step
    }
    return;
  }
  if (inSlot >= pat.len) {
    inSlot = 0;
    slot++;
    if (slot >= songData.order.size()) {
      if (looping) {  // loop the song
        slot = 0;
        absStep = 0;
      } else {
        ending = true;  // let the last notes sound out, then report finished()
      }
    }
  }
}

void update() {
  if (running) {
    uint32_t now = micros();
    // Schedule a little ahead so a slow screen redraw doesn't delay the beat.
    while (!ending && (int32_t)(now + 4000 - nextAt) >= 0) {
      if (source && patNeeded) {
        if (source->ready()) {
          bool newSong = false;
          source->take(streamPat, newSong);
          if (newSong) {
            streamBase = absStep;
            computeTempo();
          }
          patNeeded = false;
          inSlot = 0;
        } else if (source->finished()) {
          ending = true;
          break;
        } else {
          nextAt = now;  // the next pattern isn't built yet: wait, don't run ahead
          break;
        }
      }
      scheduleStep(nextAt);
      nextAt += stepUs;
    }
    if (ending && (int32_t)(now - nextAt) >= 0) {
      running = false;
      ending = false;
      finishedFlag = true;
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
