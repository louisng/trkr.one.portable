#pragma once
// Speaker "monitor": a basic preview of what you play, using M5Unified's
// 8-channel tone mixer. Placeholder until the real synth engine (phase 4).
#include <stdint.h>

namespace monitor {

void begin();
void setEnabled(bool on);
void setVolume(uint8_t v);
void noteOn(uint8_t note);
void noteOff(uint8_t note);
void drum(uint8_t pad);
void click(bool accent);  // metronome
void allOff();

}  // namespace monitor
