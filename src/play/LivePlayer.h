#pragma once
// Live playing: turns musical key presses into MIDI notes (and speaker preview).
// Every held key remembers exactly which notes it started, so releasing it
// always stops the right notes even if octave / key / modifiers changed.
#include <stdint.h>

namespace player {

void begin();
// Handles musical roles (piano / chord / drum) and the octave + inversion keys.
// Returns true if the key was consumed.
bool onKey(uint8_t keyIndex, bool pressed);
void releaseAll();  // stop everything that's held (e.g. when switching screens)

// Optional play-along filter for the piano keys: given the pitch a key would
// play, return the pitch to play instead, or -1 to stay silent.
typedef int (*NoteFilter)(int note);
void setNoteFilter(NoteFilter f);

uint8_t inversion();
// Text describing the most recent thing played, e.g. "Cmaj7", "E4", "Snare".
const char* lastPlayed();
// Pitch of a piano key in the current octave/key/mode, or -1 if inactive.
int pianoNoteFor(uint8_t semitoneValue);
// True if the given key index is currently sounding something.
bool keySounding(uint8_t keyIndex);
// Name for drum pad 0-11.
const char* drumName(uint8_t pad);

}  // namespace player
