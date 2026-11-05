#include "LivePlayer.h"

#include <stdio.h>
#include <string.h>

#include "../app/Settings.h"
#include "../audio/Monitor.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../midi/MidiOut.h"
#include "../music/Theory.h"

namespace player {

using layout::Role;

struct Held {
  uint8_t channel;  // 0 = nothing held
  uint8_t count;
  uint8_t notes[theory::MAX_CHORD_NOTES];
};

static Held held[layout::KEY_COUNT];
// Reference counts so overlapping keys (e.g. a chord + a piano key sharing a
// note) don't cut each other off: note-off goes out only when the last one lets go.
static uint8_t refs[16][128];
static uint8_t inv = 0;
static char last[16] = "";

// General MIDI drum notes for the 12 pads.
static const uint8_t DRUM_NOTES[12] = {36, 38, 37, 39, 42, 46, 41, 45, 48, 49, 51, 56};
static const char* const DRUM_NAMES[12] = {"Kick",  "Snare",  "Rim",   "Clap",
                                           "C.Hat", "O.Hat",  "Lo Tom", "Mid Tom",
                                           "Hi Tom", "Crash", "Ride",  "Cowbell"};

// Key indices of the modifier keys (row 2/3 of the layout).
static constexpr uint8_t IDX_SHIFT = 2 * 14 + 1;
static constexpr uint8_t IDX_CTRL = 3 * 14 + 0;
static constexpr uint8_t IDX_OPT = 3 * 14 + 1;
static constexpr uint8_t IDX_ALT = 3 * 14 + 2;

void begin() {
  memset(held, 0, sizeof(held));
  memset(refs, 0, sizeof(refs));
}

const char* drumName(uint8_t pad) { return pad < 12 ? DRUM_NAMES[pad] : ""; }
uint8_t inversion() { return inv; }
const char* lastPlayed() { return last; }
bool keySounding(uint8_t k) { return k < layout::KEY_COUNT && held[k].channel != 0; }

static void startNote(uint8_t ch, uint8_t note, bool drum, uint8_t pad) {
  uint8_t& r = refs[(ch - 1) & 15][note & 127];
  if (r == 0) {
    midi::noteOn(ch, note, settings.velocity);
    if (drum) monitor::drum(pad);
    else monitor::noteOn(note);
  }
  if (r < 255) r++;
}

static void stopNote(uint8_t ch, uint8_t note, bool drum) {
  uint8_t& r = refs[(ch - 1) & 15][note & 127];
  if (r == 0) return;
  if (--r == 0) {
    midi::noteOff(ch, note);
    if (!drum) monitor::noteOff(note);
  }
}

static const int8_t WHITE_TO_DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};

int pianoNoteFor(uint8_t semitone) {
  int base = 12 * (settings.octave + 1);
  int n;
  if (settings.pianoScaleMode) {
    int deg = WHITE_TO_DEGREE[semitone % 12];
    if (deg < 0) return -1;  // black keys are unused in scale mode
    n = base + settings.root + theory::scaleOffset(settings.scale, deg);
  } else {
    n = base + semitone;
  }
  return (n >= 0 && n <= 127) ? n : -1;
}

static void press(uint8_t k, const layout::KeyDef& def) {
  Held& h = held[k];
  if (h.channel) return;  // already sounding

  switch (def.role) {
    case Role::Piano: {
      int n = pianoNoteFor(def.value);
      if (n < 0) return;
      h = {settings.pianoCh, 1, {(uint8_t)n}};
      startNote(h.channel, (uint8_t)n, false, 0);
      theory::formatNote((uint8_t)n, last);
      break;
    }
    case Role::Chord: {
      theory::ChordMods mods;
      mods.seventh = keys::isDown(IDX_SHIFT);
      mods.ninth = keys::isDown(IDX_CTRL);
      mods.swap = keys::isDown(IDX_OPT);
      mods.sus4 = keys::isDown(IDX_ALT);
      mods.inversion = inv;
      int bass = 12 * settings.octave + settings.root;  // one octave below the piano
      if (bass < 0) bass += 12;
      theory::Chord c = theory::buildChord(settings.scale, settings.root, def.value,
                                           (uint8_t)bass, mods);
      if (!c.count) return;
      h.channel = settings.chordCh;
      h.count = c.count;
      memcpy(h.notes, c.notes, c.count);
      for (uint8_t i = 0; i < c.count; i++) startNote(h.channel, c.notes[i], false, 0);
      snprintf(last, sizeof(last), "%s", c.name);
      break;
    }
    case Role::Drum: {
      uint8_t pad = def.value % 12;
      h = {settings.drumCh, 1, {DRUM_NOTES[pad]}};
      startNote(h.channel, DRUM_NOTES[pad], true, pad);
      snprintf(last, sizeof(last), "%s", DRUM_NAMES[pad]);
      break;
    }
    default:
      break;
  }
}

static void release(uint8_t k, const layout::KeyDef& def) {
  Held& h = held[k];
  if (!h.channel) return;
  bool drum = def.role == Role::Drum;
  for (uint8_t i = 0; i < h.count; i++) stopNote(h.channel, h.notes[i], drum);
  h.channel = 0;
  h.count = 0;
}

bool onKey(uint8_t k, bool pressed) {
  const layout::KeyDef& def = layout::keyAt(k);
  switch (def.role) {
    case Role::Piano:
    case Role::Chord:
    case Role::Drum:
      if (pressed) press(k, def);
      else release(k, def);
      return true;
    case Role::Action:
      if (!pressed) return false;
      if (def.value == layout::ACT_OCT_DOWN) {
        if (settings.octave > 1) settings.octave--;
        return true;
      }
      if (def.value == layout::ACT_OCT_UP) {
        if (settings.octave < 8) settings.octave++;
        return true;
      }
      if (def.value == layout::ACT_INVERSION) {
        inv = (inv + 1) % 3;
        return true;
      }
      return false;
    default:
      return false;
  }
}

void releaseAll() {
  for (uint8_t k = 0; k < layout::KEY_COUNT; k++) release(k, layout::keyAt(k));
}

}  // namespace player
