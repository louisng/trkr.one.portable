#pragma once
// A small software synth for the Cardputer's speaker: melodic voices with
// envelopes, FM / detuned oscillators and filters, plus synthesized drums.
// Pure C++ with no hardware calls, so it is tested on a PC (test/host) and
// driven by an audio task on the device (Monitor.cpp).
#include <stdint.h>

namespace synth {

enum Timbre : uint8_t {
  TB_EP, TB_PIANO, TB_PAD, TB_ORGAN, TB_BRASS, TB_PLUCK, TB_BELL, TB_LEAD, TB_SOFTLEAD, TB_WHISTLE,
  TB_FINGERBASS, TB_SYNTHBASS, TB_SUBBASS, TB_ACIDBASS, TB_ARP,
  TB_COUNT
};

// Sound for a song track: its voice name ("ep", "fingerbass", ...) from
// trkr.one, falling back on the track type (0 drum, 1 bass, 2 chords, 3 lead, 4 synth).
uint8_t timbreForVoice(const char* voice, int trackType);

class Core {
 public:
  static constexpr int MAX_VOICES = 14;
  static constexpr int MAX_DRUMS = 8;

  explicit Core(float sampleRate = 22050.f);

  // owner tells apart who is playing a note (a song track, the live keys), so
  // releasing one doesn't cut another that plays the same pitch.
  void noteOn(uint8_t note, uint8_t vel, uint8_t timbre, uint8_t owner);
  void noteOff(uint8_t note, uint8_t owner);
  void drum(uint8_t gmNote, uint8_t vel);  // General MIDI drum number
  void allOff();
  void setVolume(float v) { master_ = v; }  // 0..1+

  // Mono, signed 16-bit.
  void render(int16_t* out, int n);
  int activeVoices() const;
  int activeDrums() const;

 private:
  struct Voice {
    bool active = false, held = false;
    uint8_t note = 0, owner = 0, timbre = 0;
    uint8_t stage = 0;
    float env = 0, gain = 0, ph1 = 0, ph2 = 0, ph3 = 0, inc = 0, lp = 0, fm = 0, tine = 0, fcEnv = 0, pitchMul = 1, lfo = 0;
    uint32_t born = 0;
  };
  struct Drum {
    bool active = false;
    uint8_t def = 0;
    float gain = 0, n = 0, envN = 0, envT = 0, envM = 0, sweepEnv = 1, ph = 0, mph = 0, hpY = 0, hpX = 0, lp = 0;
    int bursts = 0;
    float nextBurst = 0;
    uint32_t born = 0;
  };

  float sr_;
  float master_ = 0.62f;
  float dcX_ = 0, dcY_ = 0;
  uint32_t clock_ = 0;
  uint32_t rng_ = 0x1234567u;
  Voice voices_[MAX_VOICES];
  Drum drums_[MAX_DRUMS];
  float freq_[128];
  // per-sample multipliers, worked out once (exp() is slow on the device)
  struct PatchCoef { float att, dec, rel, fm, tine, cut, pitch; } pc_[TB_COUNT];
  struct DrumCoef { float noise, burstNoise, tone, metal, sweep, hpA; } dc_[16];

  float noise();
  float renderVoice(Voice& v);
  float renderDrum(Drum& d);
};

}  // namespace synth
