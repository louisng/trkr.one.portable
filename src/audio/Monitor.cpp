#include "Monitor.h"

#include <M5Cardputer.h>
#include <math.h>

namespace monitor {

static constexpr uint8_t CHANNELS = 8;

struct Slot {
  uint8_t note;      // 0xFF = free
  uint32_t until;    // millis() when a one-shot (drum) frees itself; 0 = held
  uint32_t started;  // for voice stealing
};

static Slot slots[CHANNELS];
static bool on = true;

// One period of a soft square-ish wave for notes, and a block of noise for
// drums (played back as a looping "period", it sounds like filtered noise).
static uint8_t toneWave[16];
static uint8_t noiseWave[256];

void begin() {
  for (auto& s : slots) s = {0xFF, 0, 0};
  for (uint8_t i = 0; i < 16; i++) toneWave[i] = (i < 8) ? 168 : 88;
  uint32_t lfsr = 0xACE1u;
  for (uint16_t i = 0; i < sizeof(noiseWave); i++) {
    lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
    noiseWave[i] = (uint8_t)(64 + (lfsr & 0x7F));
  }
  M5Cardputer.Speaker.setAllChannelVolume(110);
}

void setEnabled(bool en) {
  if (!en) allOff();
  on = en;
}

void setVolume(uint8_t v) { M5Cardputer.Speaker.setVolume(v); }

static float noteFreq(uint8_t note) { return 440.0f * powf(2.0f, ((int)note - 69) / 12.0f); }

static uint8_t allocSlot() {
  uint32_t now = millis();
  uint8_t oldest = 0;
  for (uint8_t i = 0; i < CHANNELS; i++) {
    Slot& s = slots[i];
    if (s.note != 0xFF && s.until && (int32_t)(now - s.until) >= 0) s.note = 0xFF;  // expired drum
    if (s.note == 0xFF) return i;
    if ((int32_t)(s.started - slots[oldest].started) < 0) oldest = i;
  }
  M5Cardputer.Speaker.stop(oldest);
  return oldest;  // steal the oldest voice
}

void noteOn(uint8_t note) {
  if (!on) return;
  uint8_t ch = allocSlot();
  slots[ch] = {note, 0, millis()};
  M5Cardputer.Speaker.tone(noteFreq(note), UINT32_MAX, ch, true, toneWave, sizeof(toneWave));
}

void noteOff(uint8_t note) {
  for (uint8_t i = 0; i < CHANNELS; i++) {
    if (slots[i].note == note && slots[i].until == 0) {
      M5Cardputer.Speaker.stop(i);
      slots[i].note = 0xFF;
      return;
    }
  }
}

// pad -> (frequency, duration ms, noisy?)
struct DrumVoice {
  float freq;
  uint16_t ms;
  bool noise;
};
static const DrumVoice DRUMS[12] = {
    {55, 120, false},  {180, 90, true},  {900, 30, false}, {120, 70, true},
    {160, 25, true},   {160, 140, true}, {90, 110, false}, {130, 100, false},
    {180, 90, false},  {110, 400, true}, {220, 250, true}, {800, 80, false},
};

void drum(uint8_t pad) {
  if (!on || pad >= 12) return;
  const DrumVoice& d = DRUMS[pad];
  uint8_t ch = allocSlot();
  uint32_t now = millis();
  slots[ch] = {(uint8_t)(0x80 | pad), now + d.ms, now};
  if (d.noise)
    M5Cardputer.Speaker.tone(d.freq, d.ms, ch, true, noiseWave, sizeof(noiseWave));
  else
    M5Cardputer.Speaker.tone(d.freq, d.ms, ch, true, toneWave, sizeof(toneWave));
}

void click(bool accent) {
  if (!on) return;
  uint8_t ch = allocSlot();
  uint32_t now = millis();
  slots[ch] = {0xC0, now + 25, now};
  M5Cardputer.Speaker.tone(accent ? 2000 : 1300, 25, ch, true, toneWave, sizeof(toneWave));
}

void allOff() {
  for (uint8_t i = 0; i < CHANNELS; i++) {
    M5Cardputer.Speaker.stop(i);
    slots[i].note = 0xFF;
  }
}

}  // namespace monitor
