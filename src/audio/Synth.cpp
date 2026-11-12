#include "Synth.h"

#include <M5Cardputer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

#include "../seq/Song.h"

namespace synth {

using seq::Instrument;

static constexpr uint32_t SR = 32000;      // sample rate
static constexpr int BLOCK = 256;          // samples per block (8 ms)
static constexpr int NBUF = 3;             // 2 queued in the speaker + 1 being rendered
static constexpr int VOICES = 10;
static constexpr int SPK_CH = 0;           // M5 speaker mixer channel
static constexpr uint8_t PAD_CLICK = 0xFE; // metronome voice

enum CmdType : uint8_t { C_ON, C_OFF, C_DRUM, C_CLICK, C_ALLOFF };
struct Cmd {
  uint8_t type, track, note, vel;
};

static QueueHandle_t queue = nullptr;
static volatile bool enabled = true;
static volatile bool paused = false;    // asked to release the speaker
static volatile bool released = false;  // the audio task has released it
static uint8_t volume = 96;
static volatile uint8_t cpuLoad = 0;
static int16_t bufs[NBUF][BLOCK];

enum Stage : uint8_t { ST_OFF, ST_ATTACK, ST_DECAY, ST_SUSTAIN, ST_RELEASE };

struct Voice {
  Stage stage;
  bool drum;
  uint8_t track, note, pad, wave;
  uint32_t age;
  float gain;
  // oscillators
  float ph1, ph2, inc1, inc2, pw;
  // amplitude envelope
  float env, atkInc, decCoef, susLvl, relCoef;
  // state-variable filter
  float lp, bp, cutBase, envMod, damp, f;
  uint8_t fCount;
  // drums
  uint32_t t;
  float amp, ampCoef, amp2, amp2Coef, pitch, pitchEnd, pitchCoef, hpState, bright;
};

static Voice voices[VOICES];
static uint32_t ageCounter = 0;
static uint32_t noiseState = 0x12345678;

// ── Helpers ──────────────────────────────────────────────────────────────────
static inline float noise() {
  noiseState ^= noiseState << 13;
  noiseState ^= noiseState >> 17;
  noiseState ^= noiseState << 5;
  return (float)(int32_t)noiseState * (1.0f / 2147483648.0f);
}

static inline float msToSamples(float ms) { return ms * (float)SR / 1000.0f; }
// 0-127 knob -> milliseconds, with more resolution at the short end.
static inline float knobMs(uint8_t v, float maxMs) {
  float x = v / 127.0f;
  return 1.0f + x * x * maxMs;
}
// Per-sample multiplier that decays to ~1% over `samples`.
static inline float expCoef(float samples) { return expf(-4.6f / (samples < 1.0f ? 1.0f : samples)); }

// sin(2*pi*t) for t in [0,1), parabolic approximation (plenty for a speaker).
static inline float fastSin(float t) {
  float x = t < 0.5f ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);  // triangle -1..1
  return x * (1.5f - 0.5f * x * x);                             // soften into a sine-ish curve
}

static inline float polyblep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

static inline float osc(uint8_t w, float t, float dt, float pw) {
  switch (w) {
    case seq::WAVE_SAW: return 2.0f * t - 1.0f - polyblep(t, dt);
    case seq::WAVE_SQUARE:
    case seq::WAVE_PULSE: {
      float s = t < pw ? 1.0f : -1.0f;
      s += polyblep(t, dt);
      float t2 = t - pw;
      if (t2 < 0.0f) t2 += 1.0f;
      return s - polyblep(t2, dt);
    }
    case seq::WAVE_TRIANGLE: return 4.0f * fabsf(t - 0.5f) - 1.0f;
    case seq::WAVE_SINE: return fastSin(t);
    default: return noise();
  }
}

static inline float midiFreq(float note) { return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f); }

static Voice* allocVoice() {
  Voice* best = nullptr;
  for (auto& v : voices)
    if (v.stage == ST_OFF) return &v;
  for (auto& v : voices)  // steal the oldest releasing voice first
    if (v.stage == ST_RELEASE && (!best || v.age < best->age)) best = &v;
  if (best) return best;
  for (auto& v : voices)
    if (!best || v.age < best->age) best = &v;
  return best;
}

// ── Starting / stopping voices (audio task only) ─────────────────────────────
static void startNote(uint8_t track, uint8_t note, uint8_t vel) {
  if (track >= seq::TRACKS) return;
  const Instrument& in = seq::song.tracks[track].inst;
  if (!in.sound) return;
  Voice& v = *allocVoice();
  memset(&v, 0, sizeof(v));
  v.track = track;
  v.note = note;
  v.age = ++ageCounter;
  const float vol = in.volume / 127.0f;
  v.gain = (vel / 127.0f) * vol * vol;
  v.wave = in.wave < seq::WAVE_COUNT ? in.wave : seq::WAVE_SAW;
  v.pw = v.wave == seq::WAVE_PULSE ? 0.25f : 0.5f;
  const float fr = midiFreq((float)note + in.octave * 12.0f);
  v.inc1 = fr / SR;
  v.inc2 = in.detune ? v.inc1 * powf(2.0f, (in.detune / 127.0f * 0.5f) / 12.0f) : 0.0f;  // up to 50 cents
  v.ph2 = 0.37f;
  v.atkInc = 1.0f / msToSamples(knobMs(in.attack, 3000));
  v.decCoef = expCoef(msToSamples(knobMs(in.decay, 4000)));
  v.susLvl = in.sustain / 127.0f;
  v.relCoef = expCoef(msToSamples(knobMs(in.release, 4000)));
  v.cutBase = 30.0f * powf(2.0f, in.cutoff / 127.0f * 9.4f);  // ~30 Hz .. 20 kHz
  v.envMod = in.envMod / 127.0f * 6.0f;                       // up to 6 octaves
  v.damp = 2.0f * (1.0f - in.resonance / 127.0f * 0.95f);
  if (v.damp < 0.1f) v.damp = 0.1f;
  v.fCount = 0;
  v.stage = ST_ATTACK;
}

static void stopNote(uint8_t track, uint8_t note) {
  for (auto& v : voices)
    if (!v.drum && v.track == track && v.note == note && v.stage != ST_OFF && v.stage != ST_RELEASE)
      v.stage = ST_RELEASE;
}

// Drum kit: pad 0-11 = kick, snare, rim, clap, closed hat, open hat,
// low / mid / high tom, crash, ride, cowbell.
static void startDrum(uint8_t track, uint8_t pad, uint8_t vel) {
  const Instrument* in = nullptr;
  if (pad != PAD_CLICK) {
    if (track >= seq::TRACKS) return;
    in = &seq::song.tracks[track].inst;
    if (!in->sound) return;
  }
  Voice& v = *allocVoice();
  memset(&v, 0, sizeof(v));
  v.drum = true;
  v.track = track;
  v.pad = pad;
  v.age = ++ageCounter;
  v.stage = ST_SUSTAIN;
  v.amp = 1.0f;
  v.amp2 = 1.0f;

  if (pad == PAD_CLICK) {
    v.gain = 0.35f;
    v.pitch = v.pitchEnd = vel ? 2000.0f : 1300.0f;  // vel != 0 marks the accent
    v.pitchCoef = 0.0f;
    v.ampCoef = expCoef(msToSamples(25));
    return;
  }
  const float vol = in->volume / 127.0f;
  v.gain = (vel / 127.0f) * vol * vol;
  const float ds = powf(2.0f, (in->decay - 64) / 32.0f);  // decay x0.25 .. x4
  const float ps = powf(2.0f, in->octave / 12.0f);        // pitch in semitones
  v.bright = in->cutoff / 127.0f;
  auto setAmp = [&](float ms) { v.ampCoef = expCoef(msToSamples(ms * ds)); };
  auto setPitch = [&](float from, float to, float ms) {
    v.pitch = from * ps;
    v.pitchEnd = to * ps;
    v.pitchCoef = expCoef(msToSamples(ms));
  };
  switch (pad) {
    case 0: setPitch(170, 45, 45); setAmp(380); break;                     // kick
    case 1: setPitch(220, 175, 25); setAmp(120); v.amp2Coef = expCoef(msToSamples(200 * ds)); break;  // snare
    case 2: setPitch(1700, 1700, 1); setAmp(28); break;                    // rim
    case 3: setAmp(260); break;                                            // clap
    case 4: setAmp(45); break;                                             // closed hat
    case 5: setAmp(380); break;                                            // open hat
    case 6: setPitch(130, 85, 80); setAmp(320); break;                     // low tom
    case 7: setPitch(170, 115, 80); setAmp(290); break;                    // mid tom
    case 8: setPitch(230, 160, 80); setAmp(260); break;                    // high tom
    case 9: setAmp(1200); break;                                           // crash
    case 10: setPitch(330, 330, 1); setAmp(800); break;                    // ride
    default: setPitch(560, 560, 1); setAmp(240); break;                    // cowbell
  }
}

// ── Rendering ────────────────────────────────────────────────────────────────
static inline float renderSynth(Voice& v) {
  switch (v.stage) {
    case ST_ATTACK:
      v.env += v.atkInc;
      if (v.env >= 1.0f) {
        v.env = 1.0f;
        v.stage = ST_DECAY;
      }
      break;
    case ST_DECAY:
      v.env = v.susLvl + (v.env - v.susLvl) * v.decCoef;
      if (v.env - v.susLvl < 0.0005f) {
        v.env = v.susLvl;
        v.stage = v.susLvl > 0.0005f ? ST_SUSTAIN : ST_OFF;
      }
      break;
    case ST_RELEASE:
      v.env *= v.relCoef;
      if (v.env < 0.0005f) v.stage = ST_OFF;
      break;
    default: break;
  }
  if (v.stage == ST_OFF) return 0.0f;

  // Filter cutoff follows the envelope; recomputed every 16 samples.
  if ((v.fCount++ & 15) == 0) {
    float fc = v.cutBase * exp2f(v.envMod * v.env);
    if (fc > SR * 0.45f) fc = SR * 0.45f;
    v.f = 2.0f * sinf(3.14159265f * fc / (2.0f * SR));  // 2x oversampled SVF
    // The Chamberlin filter blows up once f passes sqrt(damp^2 + 4) - damp;
    // stay safely under that (it only limits the very top of the range).
    const float fMax = 0.9f * (sqrtf(v.damp * v.damp + 4.0f) - v.damp);
    if (v.f > fMax) v.f = fMax;
    if (!isfinite(v.lp) || !isfinite(v.bp)) v.lp = v.bp = 0.0f;  // never stay broken
  }

  float s = osc(v.wave, v.ph1, v.inc1, v.pw);
  v.ph1 += v.inc1;
  if (v.ph1 >= 1.0f) v.ph1 -= 1.0f;
  if (v.inc2 > 0.0f) {
    s = 0.6f * (s + osc(v.wave, v.ph2, v.inc2, v.pw));
    v.ph2 += v.inc2;
    if (v.ph2 >= 1.0f) v.ph2 -= 1.0f;
  }
  for (int i = 0; i < 2; i++) {
    v.lp += v.f * v.bp;
    float hp = s - v.lp - v.damp * v.bp;
    v.bp += v.f * hp;
  }
  return v.lp * v.env * v.gain;
}

// One-pole high-pass on noise; `a` closer to 1 = brighter.
static inline float hpNoise(Voice& v, float a) {
  float n = noise();
  v.hpState += (1.0f - a) * (n - v.hpState);
  return n - v.hpState;
}

static inline float renderDrum(Voice& v) {
  v.amp *= v.ampCoef;
  v.pitch = v.pitchEnd + (v.pitch - v.pitchEnd) * v.pitchCoef;
  const float tSec = (float)v.t / SR;
  float out = 0.0f;
  auto tone = [&](float freq) {
    v.ph1 += freq / SR;
    if (v.ph1 >= 1.0f) v.ph1 -= 1.0f;
    return fastSin(v.ph1);
  };
  switch (v.pad) {
    case 0:  // kick: falling sine + a little click
      out = tone(v.pitch) * v.amp;
      if (v.t < 80) out += noise() * 0.4f * (1.0f - v.t / 80.0f) * (0.3f + v.bright);
      break;
    case 1:  // snare: body + noise
      v.amp2 *= v.amp2Coef;
      out = tone(v.pitch) * v.amp * 0.6f + hpNoise(v, 0.3f + 0.5f * v.bright) * v.amp2 * 0.8f;
      break;
    case 2:  // rim
      out = (v.ph1 < 0.5f ? 1.0f : -1.0f) * v.amp * 0.6f;
      tone(v.pitch);
      break;
    case 3: {  // clap: three quick bursts then a tail
      float burst = tSec < 0.03f ? ((fmodf(tSec, 0.01f) < 0.004f) ? 1.0f : 0.15f) : 1.0f;
      out = hpNoise(v, 0.5f + 0.4f * v.bright) * v.amp * burst;
      break;
    }
    case 4:
    case 5:  // hats: bright noise
      out = hpNoise(v, 0.85f + 0.14f * v.bright) * v.amp * 0.7f;
      break;
    case 6:
    case 7:
    case 8:  // toms
      out = tone(v.pitch) * v.amp;
      break;
    case 9:  // crash
      out = hpNoise(v, 0.7f + 0.29f * v.bright) * v.amp * 0.6f;
      break;
    case 10: {  // ride: inharmonic squares + a little noise
      v.ph1 += v.pitch / SR;
      v.ph2 += v.pitch * 1.48f / SR;
      if (v.ph1 >= 1.0f) v.ph1 -= 1.0f;
      if (v.ph2 >= 1.0f) v.ph2 -= 1.0f;
      float m = ((v.ph1 < 0.5f) ? 1.0f : -1.0f) * ((v.ph2 < 0.5f) ? 1.0f : -1.0f);
      out = (m * 0.25f + hpNoise(v, 0.9f) * 0.35f) * v.amp;
      break;
    }
    case PAD_CLICK:
      out = tone(v.pitch) * v.amp;
      break;
    default: {  // cowbell: two squares, softened
      v.ph1 += v.pitch / SR;
      v.ph2 += v.pitch * 1.51f / SR;
      if (v.ph1 >= 1.0f) v.ph1 -= 1.0f;
      if (v.ph2 >= 1.0f) v.ph2 -= 1.0f;
      float sq = ((v.ph1 < 0.5f) ? 1.0f : -1.0f) + ((v.ph2 < 0.5f) ? 1.0f : -1.0f);
      v.lp += 0.35f * (sq - v.lp);
      out = v.lp * 0.4f * v.amp;
      break;
    }
  }
  v.t++;
  if (v.amp < 0.0005f) v.stage = ST_OFF;
  return out * v.gain;
}

static void render(int16_t* out) {
  for (int i = 0; i < BLOCK; i++) {
    float mix = 0.0f;
    for (auto& v : voices) {
      if (v.stage == ST_OFF) continue;
      mix += v.drum ? renderDrum(v) : renderSynth(v);
    }
    mix *= 0.45f;
    // Soft clip so stacked voices saturate instead of wrapping.
    if (mix > 1.0f) mix = 1.0f;
    if (mix < -1.0f) mix = -1.0f;
    mix = mix * (1.5f - 0.5f * mix * mix);
    out[i] = (int16_t)(mix * 30000.0f);
  }
}

static void handle(const Cmd& c) {
  switch (c.type) {
    case C_ON: startNote(c.track, c.note, c.vel); break;
    case C_OFF: stopNote(c.track, c.note); break;
    case C_DRUM: startDrum(c.track, c.note, c.vel); break;
    case C_CLICK: startDrum(0, PAD_CLICK, c.vel); break;
    case C_ALLOFF:
      for (auto& v : voices) v.stage = ST_OFF;
      break;
  }
}

static void audioTask(void*) {
  uint8_t bi = 0;
  Cmd c;
  for (;;) {
    if (paused) {
      if (!released) {
        while (xQueueReceive(queue, &c, 0) == pdTRUE) {}
        for (auto& v : voices) v.stage = ST_OFF;
        M5Cardputer.Speaker.end();
        released = true;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (released) {  // resumed: start the speaker again, from silence
      if (M5Cardputer.Speaker.begin()) {
        M5Cardputer.Speaker.setChannelVolume(SPK_CH, 255);
        M5Cardputer.Speaker.setVolume(volume);
        released = false;
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
    }
    if (!enabled) {
      while (xQueueReceive(queue, &c, 0) == pdTRUE) {}
      for (auto& v : voices) v.stage = ST_OFF;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    // The speaker holds two blocks (one playing, one waiting): top it up.
    while (M5Cardputer.Speaker.isPlaying(SPK_CH) >= 2) vTaskDelay(1);
    const uint32_t t0 = micros();
    while (xQueueReceive(queue, &c, 0) == pdTRUE) handle(c);
    render(bufs[bi]);
    M5Cardputer.Speaker.playRaw(bufs[bi], BLOCK, SR, false, 1, SPK_CH, false);
    bi = (bi + 1) % NBUF;
    const uint32_t us = micros() - t0;
    cpuLoad = (uint8_t)(us * 100 / (BLOCK * 1000000UL / SR) > 100 ? 100
                                                                   : us * 100 / (BLOCK * 1000000UL / SR));
  }
}

// ── Public API ───────────────────────────────────────────────────────────────
void begin() {
  memset(voices, 0, sizeof(voices));
  queue = xQueueCreate(64, sizeof(Cmd));
  M5Cardputer.Speaker.setChannelVolume(SPK_CH, 255);
  // Core 0, next to Bluetooth; the sequencer clock and UI run on core 1.
  xTaskCreatePinnedToCore(audioTask, "synth", 4096, nullptr, 4, nullptr, 0);
}

static void send(uint8_t type, uint8_t track, uint8_t note, uint8_t vel) {
  if (!queue || !enabled) return;
  Cmd c{type, track, note, vel};
  xQueueSend(queue, &c, 0);  // never block the caller; drop if the queue is full
}

void setEnabled(bool on) {
  enabled = on;
  if (!on) M5Cardputer.Speaker.stop(SPK_CH);
}
void setVolume(uint8_t v) {
  volume = v;
  M5Cardputer.Speaker.setVolume(v);
}
void pause() {
  if (!queue || paused) return;
  paused = true;
  for (int i = 0; i < 100 && !released; i++) vTaskDelay(pdMS_TO_TICKS(10));  // wait for the task to let go
}
void resume() {
  if (!paused) return;
  paused = false;
  for (int i = 0; i < 100 && released; i++) vTaskDelay(pdMS_TO_TICKS(10));  // and to restart it
}
void noteOn(uint8_t track, uint8_t note, uint8_t vel) { send(C_ON, track, note, vel); }
void noteOff(uint8_t track, uint8_t note) {
  if (queue) {  // note-offs go through even while disabling, so nothing hangs
    Cmd c{C_OFF, track, note, 0};
    xQueueSend(queue, &c, 0);
  }
}
void drum(uint8_t track, uint8_t pad, uint8_t vel) { send(C_DRUM, track, pad, vel); }
void click(bool accent) { send(C_CLICK, 0, 0, accent ? 1 : 0); }
void allOff() {
  if (queue) {
    Cmd c{C_ALLOFF, 0, 0, 0};
    xQueueSend(queue, &c, 0);
  }
}
uint8_t load() { return cpuLoad; }

}  // namespace synth
