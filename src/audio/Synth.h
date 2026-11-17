#pragma once
// Built-in polyphonic synth + drum kit, streamed to the speaker (and the ADV's
// headphone jack) from its own task. Replaces the old beep-tone preview.
//
// Note on/off calls are thread-safe and non-blocking: they're queued and the
// audio task picks them up at the start of its next block (~8 ms).
// Each track's sound comes from seq::song.tracks[t].inst.
#include <stdint.h>

namespace synth {

void begin();
// The "speaker" setting. Off releases the speaker driver (frees RAM); on starts it
// again, if there is room (false = not enough memory: it stays off).
bool setEnabled(bool on);
void setVolume(uint8_t v); // 0-255
// Release the speaker driver (frees RAM) while WiFi/TLS runs, then restart it.
void pause();
void resume();

void noteOn(uint8_t track, uint8_t note, uint8_t vel);
void noteOff(uint8_t track, uint8_t note);
void drum(uint8_t track, uint8_t pad, uint8_t vel);
void click(bool accent);   // metronome
void allOff();

// Rough CPU use of the audio task, 0-100 (for the Synth screen).
uint8_t load();

}  // namespace synth
