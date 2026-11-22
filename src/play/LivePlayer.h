#pragma once
// Live playing: turns musical key presses into MIDI notes (and speaker preview).
// Every held key remembers exactly which notes it started, so releasing it
// always stops the right notes even if octave / key / modifiers changed.
#include <stdint.h>

#include "../input/KeyLayout.h"

namespace player {

// What a musical key press/release did, so screens can write it into the
// song (step entry) and the engine can record it.
struct Played {
  bool valid = false;
  bool pressed = false;
  layout::Role role = layout::Role::None;
  uint8_t track = 0;  // song track it was played on
  uint8_t pad = 0;    // drum pad (drums only)
  uint8_t count = 0;
  uint8_t notes[5];
};

void begin();
// Handles musical roles (piano / chord / drum) and the octave + inversion keys.
// Returns true if the key was consumed. Fills `out` for note keys.
// Call with the engine lock held.
bool onKey(uint8_t keyIndex, bool pressed, Played* out = nullptr);
void releaseAll();  // stop everything that's held (e.g. when switching screens)

// Optional play-along filter for the piano keys: given the pitch a key would
// play, return the pitch to play instead, or -1 to stay silent. nullptr = none.
typedef int (*NoteFilter)(int note);
void setNoteFilter(NoteFilter f);

uint8_t inversion();
// The live arpeggiator: while it is on, piano / chord keys feed it instead of sounding (and enter nothing).
void setArp(bool on);
bool arpEnabled();
// One engine tick (48 per beat). The engine calls it while playing; with the transport stopped it keeps
// its own clock running while arpActive().
void arpClock();
bool arpActive();
// The notes the arp is playing (in the order pressed) and, with HOLD, letting go of them all (del).
uint8_t arpPool(uint8_t* out);
// Sound some notes on a track for a moment (hearing an edit). A new preview cuts the last one.
// previewTick() (from the app loop) ends them. Call both with the engine lock held.
void preview(uint8_t track, const uint8_t* notes, uint8_t n, uint16_t ms = 350);
void previewTick();
void arpClear();
// Chord bank (top rows in TOP_BANK): the root picked, and the slash-bass mode
// (0 none, 1 /3rd, 2 /5th, 3 /7th). `bankReset` starts from the song's key.
uint8_t bankRoot();
uint8_t bankBass();
void bankReset();
// Text describing the most recent thing played, e.g. "Cmaj7", "E4", "Snare".
const char* lastPlayed();
// Pitch of a piano key in the current octave/key/mode, or -1 if inactive.
int pianoNoteFor(uint8_t semitoneValue);
// True if the given key index is currently sounding something.
bool keySounding(uint8_t keyIndex);
// Name / MIDI note for drum pad 0-11, and the pad for a note (-1 if none).
const char* drumName(uint8_t pad);
const char* drumShortName(uint8_t pad);
uint8_t drumNote(uint8_t pad);
int drumPadForNote(uint8_t note);

}  // namespace player
