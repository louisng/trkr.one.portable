#include "LivePlayer.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "../app/Settings.h"
#include "../audio/Synth.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../midi/MidiOut.h"
#include "../music/Theory.h"
#include "../seq/Engine.h"
#include "Arp.h"

static constexpr uint8_t MAX_NOTES_PER_STEP_PV = 8;

namespace player {

using layout::Role;

struct Held {
  uint8_t channel;  // MIDI channel it went out on; 0 = nothing held
  uint8_t track;
  uint8_t count;
  uint8_t notes[theory::MAX_CHORD_NOTES];  // as stored in the song (drums: lane id)
  bool drum;
  uint8_t drumOut;
  bool arp;        // the notes went to the arpeggiator, not straight out  // drums: the MIDI note actually sent (see the track's drum routing)
};

static Held held[layout::KEY_COUNT];
// Reference counts so overlapping keys (e.g. a chord + a piano key sharing a
// note) don't cut each other off: note-off goes out only when the last one lets go.
static uint8_t refs[16][128];
static uint8_t inv = 0;
static uint8_t bankRootPc = 0;
static uint8_t bankBassMode = 0;
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

static const char* const DRUM_SHORT[12] = {"KK", "SN", "RM", "CP", "CH", "OH",
                                           "LT", "MT", "HT", "CR", "RD", "CB"};

const char* drumName(uint8_t pad) { return pad < 12 ? DRUM_NAMES[pad] : ""; }
const char* drumShortName(uint8_t pad) { return pad < 12 ? DRUM_SHORT[pad] : ""; }
uint8_t drumNote(uint8_t pad) { return DRUM_NOTES[pad % 12]; }
int drumPadForNote(uint8_t note) {
  for (uint8_t i = 0; i < 12; i++)
    if (DRUM_NOTES[i] == note) return i;
  return -1;
}
uint8_t inversion() { return inv; }
uint8_t bankRoot() { return bankRootPc; }
uint8_t bankBass() { return bankBassMode; }
void bankReset() { bankRootPc = settings.root % 12; bankBassMode = 0; }
const char* lastPlayed() { return last; }
bool keySounding(uint8_t k) { return k < layout::KEY_COUNT && held[k].channel != 0; }

static void startNote(uint8_t track, uint8_t ch, uint8_t note, bool drum, uint8_t pad) {
  uint8_t& r = refs[(ch - 1) & 15][note & 127];
  if (r == 0) {
    midi::noteOn(ch, note, settings.velocity);
    if (drum) synth::drum(track, pad, settings.velocity);
    else synth::noteOn(track, note, settings.velocity);
  }
  if (r < 255) r++;
}

static void stopNote(uint8_t track, uint8_t ch, uint8_t note, bool drum) {
  uint8_t& r = refs[(ch - 1) & 15][note & 127];
  if (r == 0) return;
  if (--r == 0) {
    midi::noteOff(ch, note);
    if (!drum) synth::noteOff(track, note);
  }
}

static const int8_t WHITE_TO_DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};

int pianoNoteFor(uint8_t semitone) {
  int base = 12 * (settings.octave + 1);
  int n;
  if (settings.pianoScaleMode) {
    int deg = WHITE_TO_DEGREE[semitone % 12];
    if (deg < 0) return -1;  // black keys are unused in scale mode
    deg += 7 * (semitone / 12);
    n = base + settings.root + theory::scaleOffset(settings.scale, deg);
  } else {
    n = base + semitone;
  }
  return (n >= 0 && n <= 127) ? n : -1;
}

static NoteFilter noteFilter = nullptr;
void setNoteFilter(NoteFilter f) { noteFilter = f; }

// ── the live arpeggiator ──
static arp::Live liveArp;
static bool arpOn = false;
static uint32_t arpTick = 0, arpNext = 0;
struct ArpNote { bool used, started; uint8_t ch, track, note; uint32_t on, off; };
static ArpNote arpNotes[16];

static void arpSilence() {
  for (auto& a : arpNotes) {
    if (a.used && a.started) stopNote(a.track, a.ch, a.note, false);
    a.used = false;
  }
}
void setArp(bool on) {
  arpOn = on;
  liveArp.stop();
  arpSilence();
}
bool arpEnabled() { return arpOn; }

// ── preview: hear an edit ──
static uint8_t pvNotes[MAX_NOTES_PER_STEP_PV];
static uint8_t pvCount = 0, pvTrack = 0, pvCh = 0;
static uint32_t pvEnd = 0;
static void previewStop() {
  for (uint8_t i = 0; i < pvCount; i++) stopNote(pvTrack, pvCh, pvNotes[i], false);
  pvCount = 0;
}
void preview(uint8_t track, const uint8_t* notes, uint8_t n, uint16_t ms) {
  previewStop();
  if (track >= seq::TRACKS || seq::song.tracks[track].kind == seq::TrackKind::Drum) return;
  pvTrack = track;
  pvCh = seq::song.tracks[track].channel;
  for (uint8_t i = 0; i < n && pvCount < MAX_NOTES_PER_STEP_PV; i++) {
    pvNotes[pvCount++] = notes[i];
    startNote(track, pvCh, notes[i], false, 0);
  }
  pvEnd = millis() + ms;
}
void previewTick() {
  if (pvCount && (int32_t)(millis() - pvEnd) >= 0) previewStop();
}
uint8_t arpPool(uint8_t* out) { return liveArp.pool(out); }
void arpClear() { liveArp.clear(); }
bool arpActive() {
  if (!arpOn) return false;
  if (liveArp.running()) return true;
  for (const auto& a : arpNotes) if (a.used) return true;
  return false;
}
void arpClock() {
  if (!arpOn) return;
  arpTick++;
  for (auto& a : arpNotes) {   // notes due to start (swing makes some late) or to stop (gate)
    if (!a.used) continue;
    if (!a.started && (int32_t)(arpTick - a.on) >= 0) { startNote(a.track, a.ch, a.note, false, 0); a.started = true; }
    if (a.started && (int32_t)(arpTick - a.off) >= 0) { stopNote(a.track, a.ch, a.note, false); a.used = false; }
  }
  liveArp.mode = settings.arpMode; liveArp.rate = settings.arpRate; liveArp.octaves = settings.arpOctaves;
  liveArp.gate = settings.arpGate; liveArp.swing = settings.arpSwing;
  if (liveArp.hold != settings.arpHold) liveArp.setHold(settings.arpHold);
  if (!liveArp.running()) { arpNext = arpTick; return; }   // the first note plays as soon as a key goes down
  if ((int32_t)(arpTick - arpNext) < 0) return;
  uint8_t out[16];
  const uint8_t n = liveArp.step(out);
  const uint8_t late = liveArp.swingTicks(), len = liveArp.gateTicks();
  const uint8_t tr = seq::liveSynthTrack();
  const uint8_t ch = seq::song.tracks[tr].channel;
  for (uint8_t i = 0; i < n; i++)
    for (auto& a : arpNotes)
      if (!a.used) { a = {true, false, ch, tr, out[i], arpTick + late, arpTick + late + len}; break; }
  if (n) {
    if (n == 1) theory::formatNote(out[0], last);
    else snprintf(last, sizeof(last), "arp %u notes", n);
  }
  arpNext = arpTick + arp::rateTicks(liveArp.rate);
  // start the ones due right now
  for (auto& a : arpNotes)
    if (a.used && !a.started && (int32_t)(arpTick - a.on) >= 0) { startNote(a.track, a.ch, a.note, false, 0); a.started = true; }
}

static void press(uint8_t k, const layout::KeyDef& def) {
  Held& h = held[k];
  if (h.channel) return;  // already sounding
  const uint8_t synthTrack = seq::liveSynthTrack();
  const uint8_t drumTrack = seq::liveDrumTrack();

  switch (def.role) {
    case Role::Piano: {
      int n = pianoNoteFor(def.value);
      if (n >= 0 && noteFilter) n = noteFilter(n);
      if (n < 0 || n > 127) return;
      h = {seq::song.tracks[synthTrack].channel, synthTrack, 1, {(uint8_t)n}};
      if (arpOn) { h.arp = true; liveArp.press((uint8_t)n); }   // the arp plays it
      else startNote(h.track, h.channel, (uint8_t)n, false, 0);
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
      h.channel = seq::song.tracks[synthTrack].channel;
      h.track = synthTrack;
      h.count = c.count;
      memcpy(h.notes, c.notes, c.count);
      h.arp = arpOn;
      for (uint8_t i = 0; i < c.count; i++) {
        if (arpOn) liveArp.press(c.notes[i]);
        else startNote(h.track, h.channel, c.notes[i], false, 0);
      }
      snprintf(last, sizeof(last), "%s", c.name);
      break;
    }
    case Role::BankQual: {
      theory::BankMods mods;
      mods.seventh = keys::isDown(IDX_SHIFT);
      mods.ninth = keys::isDown(IDX_CTRL);
      mods.swap = keys::isDown(IDX_OPT);
      mods.sus4 = keys::isDown(IDX_ALT);
      mods.bass = bankBassMode;
      mods.flats = theory::keyUsesFlats(settings.root, settings.scale);
      int base = 12 * settings.octave + bankRootPc;  // chords sit an octave below the piano
      if (base < 0) base += 12;
      theory::Chord c = theory::buildBankChord(bankRootPc, def.value, (uint8_t)base, mods);
      if (!c.count) return;
      h.channel = seq::song.tracks[synthTrack].channel;
      h.track = synthTrack;
      h.count = c.count;
      memcpy(h.notes, c.notes, c.count);
      h.arp = arpOn;
      for (uint8_t i = 0; i < c.count; i++) {
        if (arpOn) liveArp.press(c.notes[i]);
        else startNote(h.track, h.channel, c.notes[i], false, 0);
      }
      snprintf(last, sizeof(last), "%s", c.name);
      break;
    }
    case Role::Drum: {
      uint8_t pad = def.value % 12;
      uint8_t ch, note;
      seq::midiOut(seq::song.tracks[drumTrack], DRUM_NOTES[pad], &ch, &note);
      h = {ch, drumTrack, 1, {DRUM_NOTES[pad]}, true, note};
      startNote(h.track, h.channel, note, true, pad);
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
  (void)def;
  if (h.arp) {
    for (uint8_t i = 0; i < h.count; i++) liveArp.release(h.notes[i]);
    h.arp = false;
    h.channel = 0;
    h.count = 0;
    return;
  }
  if (h.drum) stopNote(h.track, h.channel, h.drumOut, true);
  else
    for (uint8_t i = 0; i < h.count; i++) stopNote(h.track, h.channel, h.notes[i], false);
  h.drum = false;
  h.channel = 0;
  h.count = 0;
}

static void fillPlayed(Played* out, uint8_t k, const layout::KeyDef& def, bool pressed) {
  if (!out) return;
  const Held& h = held[k];
  out->valid = h.channel != 0 && !h.arp;   // what the arp plays is never entered or recorded
  out->pressed = pressed;
  out->role = def.role;
  out->track = h.track;
  out->pad = def.role == Role::Drum ? def.value % 12 : 0;
  out->count = h.count;
  memcpy(out->notes, h.notes, h.count);
}

bool onKey(uint8_t k, bool pressed, Played* out) {
  const layout::KeyDef& def = layout::keyAt(k);
  switch (def.role) {
    case Role::BankRoot:
      // Picking a root is silent: it is the root of the next chord types you press.
      if (pressed) {
        bankRootPc = def.value % 12;
        snprintf(last, sizeof(last), "root %s", theory::keyUsesFlats(settings.root, settings.scale)
                                                    ? theory::noteNameFlat(bankRootPc) : theory::noteName(bankRootPc));
      }
      return true;
    case Role::Piano:
    case Role::Chord:
    case Role::BankQual:
    case Role::Drum:
      if (pressed) {
        press(k, def);
        fillPlayed(out, k, def, true);
      } else {
        fillPlayed(out, k, def, false);  // before release clears it
        release(k, def);
      }
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
        if (layout::topMode() == layout::TOP_BANK) {
          bankBassMode = (bankBassMode + 1) % 4;
          static const char* const B[4] = {"no bass note", "bass: 3rd", "bass: 5th", "bass: 7th"};
          snprintf(last, sizeof(last), "%s", B[bankBassMode]);
          return true;
        }
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
  liveArp.stop();
  arpSilence();
  previewStop();
}

}  // namespace player
