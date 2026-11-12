#pragma once
// Speaker "monitor": plays what the Cardputer sends over MIDI (and what you
// play) through its own speaker, using the software synth in SynthCore.
// Calls just queue a command; a high-priority audio task renders and streams
// the sound, so nothing here blocks the MIDI timing.
#include <stdint.h>

#include "SynthCore.h"

namespace monitor {

using Timbre = synth::Timbre;

// Who is playing a note, so releasing one part never cuts another.
// Song tracks use their track number (0-7).
static constexpr uint8_t OWNER_LIVE_PIANO = 8;
static constexpr uint8_t OWNER_LIVE_CHORD = 9;

void begin();
void setEnabled(bool on);
// Release the speaker driver (frees RAM) while WiFi/TLS runs; resume() restarts it.
void pause();
void resume();
void setVolume(uint8_t v);
// timbre: sound for a song track's voice name (e.g. "ep", "fingerbass")
uint8_t timbreFor(const char* voice, uint8_t trackType);
void noteOn(uint8_t note, uint8_t vel, uint8_t timbre, uint8_t owner);
void noteOff(uint8_t note, uint8_t owner);
void drum(uint8_t gmNote, uint8_t vel);   // General MIDI drum number
void allOff();
void beep();                 // short test sound (kick + a note)
const char* status();        // e.g. "synth ok 5231/0": blocks played / refused

}  // namespace monitor
