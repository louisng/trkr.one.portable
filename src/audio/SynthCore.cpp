#include "SynthCore.h"

#include <math.h>
#include <string.h>

namespace synth {

namespace {

const float TWO_PI = 6.28318530718f;

// ── wave tables ──────────────────────────────────────────────────────────────
float sineTab[1025];
bool tabReady = false;
void initTables() {
  if (tabReady) return;
  for (int i = 0; i <= 1024; i++) sineTab[i] = sinf(TWO_PI * i / 1024.f);
  tabReady = true;
}
inline float frac(float x) {  // x - floor(x) without the slow floorf
  float f = x - (float)(int)x;
  return f < 0 ? f + 1.f : f;
}
inline float sinT(float ph) {  // ph in cycles; any value
  ph = frac(ph);
  float x = ph * 1024.f;
  int i = (int)x;
  float f = x - i;
  return sineTab[i] + (sineTab[i + 1] - sineTab[i]) * f;
}

enum Wave : uint8_t { W_SINE, W_TRI, W_SAW, W_SQR, W_PULSE, W_ORGAN };
inline float osc(uint8_t w, float ph) {  // ph in [0,1)
  switch (w) {
    case W_SINE: return sinT(ph);
    case W_TRI: return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
    case W_SAW: return 2 * ph - 1;
    case W_SQR: return ph < 0.5f ? 1.f : -1.f;
    case W_PULSE: return ph < 0.25f ? 1.5f : -0.5f;   // 25% pulse, centred on zero
    case W_ORGAN: return 0.6f * sinT(ph) + 0.3f * sinT(ph * 2) + 0.2f * sinT(ph * 3) + 0.1f * sinT(ph * 4);
  }
  return 0;
}

// ── melodic patches ──────────────────────────────────────────────────────────
struct Patch {
  uint8_t w1, w2;
  float detune;      // second oscillator frequency ratio
  float mix2;        // 0..1 share of the second oscillator
  float sub;         // sub-octave sine level
  float fmRatio, fmIdx, fmDecay;   // phase modulation: modulator ratio, depth (radians), decay (s)
  float tine, tineDecay;           // extra bright partial (EP "tine")
  float a, d, s, r;                // envelope: attack/decay/release seconds, sustain level
  float cut, cutEnv, cutDecay;     // low-pass: base Hz, extra Hz from its envelope, that envelope's decay (s)
  float vib;                       // vibrato depth, semitones
  float pitchDrop, pitchDecay;     // start sharp and glide down (808 sub)
  float gain;
};

//                  w1       w2       det    mix2  sub   fmR  fmI  fmD   tine tD    a      d     s     r     cut   cutEnv cD    vib  pDrop pD    gain
const Patch PATCHES[TB_COUNT] = {
  /* EP */        {W_SINE,  W_SINE,  1.0f, 0.0f, 0.0f, 1.f, 1.7f, 0.45f, 0.22f, 0.07f, 0.002f, 1.5f, 0.25f, 0.40f, 0,    0,    0.1f, 0.0f, 0.f, 0.f, 0.85f},
  /* PIANO */     {W_SINE,  W_SINE,  1.0f, 0.0f, 0.0f, 1.f, 2.3f, 0.30f, 0.30f, 0.05f, 0.002f, 0.9f, 0.06f, 0.30f, 0,    0,    0.1f, 0.0f, 0.f, 0.f, 0.85f},
  /* PAD */       {W_SAW,   W_SAW,   1.007f,0.5f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.40f,  0.6f, 0.75f, 1.30f, 1400, 600,  0.8f, 0.0f, 0.f, 0.f, 0.42f},
  /* ORGAN */     {W_ORGAN, W_SINE,  1.0f, 0.0f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.006f, 0.1f, 1.00f, 0.07f, 0,    0,    0.1f, 0.0f, 0.f, 0.f, 0.60f},
  /* BRASS */     {W_SAW,   W_SAW,   1.004f,0.4f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.05f,  0.3f, 0.80f, 0.14f, 700,  2600, 0.35f,0.0f, 0.f, 0.f, 0.45f},
  /* PLUCK */     {W_TRI,   W_SAW,   1.003f,0.3f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.002f, 0.30f,0.03f, 0.22f, 600,  3200, 0.12f,0.0f, 0.f, 0.f, 0.60f},
  /* BELL */      {W_SINE,  W_SINE,  1.0f, 0.0f, 0.0f, 3.5f,2.6f, 0.9f,  0.f,   0.1f,  0.001f, 1.4f, 0.00f, 0.60f, 0,    0,    0.1f, 0.0f, 0.f, 0.f, 0.70f},
  /* LEAD */      {W_SAW,   W_PULSE, 1.006f,0.4f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.012f, 0.2f, 0.70f, 0.30f, 2400, 900,  0.5f, 0.18f,0.f, 0.f, 0.42f},
  /* SOFTLEAD */  {W_SQR,   W_TRI,   1.004f,0.5f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.030f, 0.2f, 0.70f, 0.35f, 1700, 500,  0.5f, 0.15f,0.f, 0.f, 0.40f},
  /* WHISTLE */   {W_SINE,  W_SINE,  2.0f, 0.06f,0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.060f, 0.1f, 0.90f, 0.30f, 0,    0,    0.1f, 0.30f,0.f, 0.f, 0.70f},
  /* FINGERBASS */{W_SAW,   W_SAW,   1.003f,0.3f, 0.5f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.003f, 0.50f,0.50f, 0.08f, 380,  1500, 0.16f,0.0f, 0.f, 0.f, 0.75f},
  /* SYNTHBASS */ {W_SQR,   W_SAW,   1.005f,0.4f, 0.4f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.002f, 0.30f,0.70f, 0.06f, 650,  1800, 0.20f,0.0f, 0.f, 0.f, 0.60f},
  /* SUBBASS */   {W_SINE,  W_SINE,  1.0f, 0.0f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.002f, 1.20f,0.45f, 0.20f, 0,    0,    0.1f, 0.0f, 0.8f,0.05f,0.95f},
  /* ACIDBASS */  {W_SAW,   W_SAW,   1.0f, 0.0f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.002f, 0.25f,0.55f, 0.05f, 350,  2800, 0.18f,0.0f, 0.f, 0.f, 0.60f},
  /* ARP */       {W_SAW,   W_PULSE, 1.005f,0.4f, 0.0f, 1.f, 0.f,  0.1f,  0.f,   0.1f,  0.001f, 0.18f,0.08f, 0.10f, 800,  3000, 0.12f,0.0f, 0.f, 0.f, 0.50f},
};

// ── drums ────────────────────────────────────────────────────────────────────
struct DrumDef {
  float noiseAmt, noiseDecay, hpHz;          // noise part: level, decay (s), high-pass corner
  float toneAmt, f0, f1, sweep, toneDecay;   // sine part: level, start/end Hz, glide time, decay
  float metalAmt, metalHz, metalDecay;       // inharmonic squares (cymbals, cowbell)
  float click;                                // 3 ms noise click on top
  int bursts;                                 // noise re-triggers 11 ms apart (claps)
  float gain;
};
enum DrumId : uint8_t { D_KICK, D_SNARE, D_RIM, D_CLAP, D_HAT, D_OPENHAT, D_TOM_LO, D_TOM_MID, D_TOM_HI, D_CRASH, D_RIDE, D_COWBELL, D_SHAKER, D_TAMB, D_CLAVE, D_COUNT };
const DrumDef DRUMS[D_COUNT] = {
  /* KICK */    {0.0f, 0.01f, 0,    1.0f, 160, 46, 0.040f, 0.30f, 0.0f, 0, 0,    0.35f, 1, 1.05f},
  /* SNARE */   {0.9f, 0.14f, 1400, 0.6f, 200, 175,0.020f, 0.09f, 0.0f, 0, 0,    0.00f, 1, 0.75f},
  /* RIM */     {0.0f, 0.01f, 0,    0.9f, 1750,1700,0.001f,0.020f,0.0f, 0, 0,    0.30f, 1, 0.70f},
  /* CLAP */    {1.0f, 0.10f, 900,  0.0f, 0,   0,   0.001f,0.01f, 0.0f, 0, 0,    0.00f, 3, 0.70f},
  /* HAT */     {1.0f, 0.040f,5200, 0.0f, 0,   0,   0.001f,0.01f, 0.5f, 6200, 0.03f, 0.00f, 1, 0.45f},
  /* OPENHAT */ {1.0f, 0.30f, 5000, 0.0f, 0,   0,   0.001f,0.01f, 0.5f, 6200, 0.25f, 0.00f, 1, 0.45f},
  /* TOM LO */  {0.0f, 0.01f, 0,    1.0f, 150, 95,  0.060f,0.38f, 0.0f, 0, 0,    0.15f, 1, 0.95f},
  /* TOM MID */ {0.0f, 0.01f, 0,    1.0f, 190, 125, 0.055f,0.32f, 0.0f, 0, 0,    0.15f, 1, 0.95f},
  /* TOM HI */  {0.0f, 0.01f, 0,    1.0f, 250, 165, 0.050f,0.28f, 0.0f, 0, 0,    0.15f, 1, 0.95f},
  /* CRASH */   {0.8f, 1.10f, 3200, 0.0f, 0,   0,   0.001f,0.01f, 0.6f, 4200, 0.9f,  0.00f, 1, 0.50f},
  /* RIDE */    {0.35f,0.35f, 4500, 0.0f, 0,   0,   0.001f,0.01f, 0.8f, 3300, 0.40f, 0.00f, 1, 0.42f},
  /* COWBELL */ {0.0f, 0.01f, 0,    0.0f, 0,   0,   0.001f,0.01f, 1.0f, 540,  0.14f, 0.00f, 1, 0.55f},
  /* SHAKER */  {1.0f, 0.065f,6000, 0.0f, 0,   0,   0.001f,0.01f, 0.0f, 0, 0,    0.00f, 1, 0.40f},
  /* TAMB */    {1.0f, 0.15f, 5500, 0.0f, 0,   0,   0.001f,0.01f, 0.35f, 7000, 0.12f, 0.00f, 1, 0.42f},
  /* CLAVE */   {0.0f, 0.01f, 0,    1.0f, 2500,2450,0.001f,0.025f,0.0f, 0, 0,    0.10f, 1, 0.65f},
};

uint8_t drumFor(uint8_t gm) {
  switch (gm) {
    case 35: case 36: return D_KICK;
    case 38: case 40: return D_SNARE;
    case 37: return D_RIM;
    case 39: return D_CLAP;
    case 42: case 44: return D_HAT;
    case 46: return D_OPENHAT;
    case 41: case 43: return D_TOM_LO;
    case 45: case 47: return D_TOM_MID;
    case 48: case 50: return D_TOM_HI;
    case 49: case 52: case 55: case 57: return D_CRASH;
    case 51: case 53: case 59: return D_RIDE;
    case 56: return D_COWBELL;
    case 54: return D_TAMB;
    case 69: case 70: case 82: return D_SHAKER;
    case 75: case 76: case 77: return D_CLAVE;
    default: return D_RIM;
  }
}

enum Stage : uint8_t { ST_ATT, ST_DEC, ST_SUS, ST_REL };
inline float decayCoef(float seconds, float sr) {  // reaches ~1% after `seconds`
  return seconds <= 0 ? 0.f : expf(-4.6f / (seconds * sr));
}

}  // namespace

uint8_t timbreForVoice(const char* v, int type) {
  struct Map { const char* name; uint8_t tb; };
  static const Map M[] = {
      {"ep", TB_EP}, {"piano", TB_PIANO}, {"pad", TB_PAD}, {"organ", TB_ORGAN}, {"brass", TB_BRASS},
      {"pluck", TB_PLUCK}, {"bell", TB_BELL}, {"lead", TB_LEAD}, {"softlead", TB_SOFTLEAD},
      {"whistle", TB_WHISTLE}, {"fingerbass", TB_FINGERBASS}, {"synthbass", TB_SYNTHBASS},
      {"sub808", TB_SUBBASS}, {"acidbass", TB_ACIDBASS}, {"arp", TB_ARP},
  };
  if (v) for (const Map& m : M) if (strcmp(v, m.name) == 0) return m.tb;
  switch (type) {
    case 1: return TB_FINGERBASS;
    case 2: return TB_EP;
    case 3: return TB_LEAD;
    case 4: return TB_PAD;
  }
  return TB_EP;
}

Core::Core(float sampleRate) : sr_(sampleRate) {
  initTables();
  for (int n = 0; n < 128; n++) freq_[n] = 440.f * powf(2.f, (n - 69) / 12.f);
  for (int i = 0; i < TB_COUNT; i++) {
    const Patch& P = PATCHES[i];
    pc_[i] = {1.f / (P.a * sr_ + 1e-9f), decayCoef(P.d, sr_), decayCoef(P.r, sr_), decayCoef(P.fmDecay, sr_),
              decayCoef(P.tineDecay, sr_), decayCoef(P.cutDecay, sr_), decayCoef(P.pitchDecay, sr_)};
  }
  for (int i = 0; i < D_COUNT; i++) {
    const DrumDef& D = DRUMS[i];
    float rc = D.hpHz > 0 ? 1.f / (TWO_PI * D.hpHz) : 0, dt = 1.f / sr_;
    dc_[i] = {decayCoef(D.noiseDecay, sr_), decayCoef(0.012f, sr_), decayCoef(D.toneDecay, sr_),
              decayCoef(D.metalDecay, sr_), expf(-1.f / (D.sweep * sr_)),
              D.hpHz > 0 ? rc / (rc + dt) : 0};
  }
}

float Core::noise() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return (float)(int32_t)rng_ * (1.0f / 2147483648.0f);
}

int Core::activeVoices() const { int n = 0; for (auto& v : voices_) n += v.active; return n; }
int Core::activeDrums() const { int n = 0; for (auto& d : drums_) n += d.active; return n; }

void Core::noteOn(uint8_t note, uint8_t vel, uint8_t timbre, uint8_t owner) {
  if (note > 127 || timbre >= TB_COUNT) return;
  // Re-strike: a voice already playing this pitch for this owner restarts.
  Voice* slot = nullptr;
  for (auto& v : voices_) if (v.active && v.held && v.note == note && v.owner == owner) { slot = &v; break; }
  if (!slot) for (auto& v : voices_) if (!v.active) { slot = &v; break; }
  if (!slot) {  // steal: quietest releasing voice, else the oldest
    float best = 1e9f;
    for (auto& v : voices_) if (!v.held && v.env < best) { best = v.env; slot = &v; }
    if (!slot) { uint32_t oldest = 0xffffffffu; for (auto& v : voices_) if (v.born < oldest) { oldest = v.born; slot = &v; } }
  }
  const Patch& P = PATCHES[timbre];
  Voice& v = *slot;
  float keep = (slot->active && slot->note == note) ? slot->lp : 0;  // avoid a click on re-strike
  v = Voice();
  v.active = v.held = true;
  v.note = note;
  v.owner = owner;
  v.timbre = timbre;
  v.stage = ST_ATT;
  v.env = P.a <= 0.0015f ? 1.f : 0.f;
  v.stage = P.a <= 0.0015f ? ST_DEC : ST_ATT;
  float vf = vel / 127.f;
  v.gain = (0.25f + 0.75f * vf) * vf * 1.2f + 0.05f;
  if (v.gain > 1.2f) v.gain = 1.2f;
  v.inc = freq_[note] / sr_;
  v.fm = 1.f;
  v.tine = 1.f;
  v.fcEnv = 1.f;
  v.pitchMul = 1.f + P.pitchDrop;
  v.lp = keep;
  v.born = ++clock_;
  v.ph1 = v.ph2 = v.ph3 = 0;
  v.lfo = (note % 7) * 0.13f;  // spread vibrato phases a little
}

void Core::noteOff(uint8_t note, uint8_t owner) {
  for (auto& v : voices_)
    if (v.active && v.held && v.note == note && v.owner == owner) {
      v.held = false;
      v.stage = ST_REL;
    }
}

void Core::drum(uint8_t gm, uint8_t vel) {
  uint8_t def = drumFor(gm);
  Drum* slot = nullptr;
  for (auto& d : drums_) if (!d.active) { slot = &d; break; }
  if (!slot) {
    uint32_t oldest = 0xffffffffu;
    for (auto& d : drums_) if (d.born < oldest) { oldest = d.born; slot = &d; }
  }
  // a new closed hat chokes an open one
  if (def == D_HAT) for (auto& d : drums_) if (d.active && d.def == D_OPENHAT) d.active = false;
  const DrumDef& D = DRUMS[def];
  Drum& d = *slot;
  d = Drum();
  d.active = true;
  d.def = def;
  float vf = vel / 127.f;
  d.gain = D.gain * (0.35f + 0.65f * vf) * (0.3f + 0.7f * vf) * 1.4f;
  d.envN = 1.f;
  d.envT = 1.f;
  d.envM = 1.f;
  d.bursts = D.bursts;
  d.nextBurst = 0.011f * sr_;
  d.born = ++clock_;
}

void Core::allOff() {
  for (auto& v : voices_) v.active = false;
  for (auto& d : drums_) d.active = false;
}

float Core::renderVoice(Voice& v) {
  const Patch& P = PATCHES[v.timbre];
  const PatchCoef& K = pc_[v.timbre];
  // envelope
  switch (v.stage) {
    case ST_ATT:
      v.env += K.att;
      if (v.env >= 1.f) { v.env = 1.f; v.stage = ST_DEC; }
      break;
    case ST_DEC: {
      v.env = P.s + (v.env - P.s) * K.dec;
      if (v.env - P.s < 0.0002f) { v.env = P.s; v.stage = ST_SUS; }
      break;
    }
    case ST_SUS: break;
    case ST_REL:
      v.env *= K.rel;
      if (v.env < 0.0006f) { v.active = false; return 0; }
      break;
  }
  // pitch: vibrato and glide
  float inc = v.inc;
  if (P.vib > 0) {
    v.lfo += 5.3f / sr_;
    if (v.lfo >= 1.f) v.lfo -= 1.f;
    float fade = v.stage == ST_ATT ? 0.f : 1.f;
    inc *= 1.f + P.vib * 0.0578f * sinT(v.lfo) * fade;
  }
  if (P.pitchDrop > 0) {
    inc *= v.pitchMul;
    v.pitchMul = 1.f + (v.pitchMul - 1.f) * K.pitch;
  }
  v.ph1 += inc; if (v.ph1 >= 1.f) v.ph1 -= 1.f;
  float s;
  if (P.fmIdx > 0) {
    float modPh = v.ph1 * P.fmRatio;
    float m = sinT(modPh) * P.fmIdx * v.fm * (1.f / TWO_PI);
    s = sinT(v.ph1 + m);
    v.fm *= K.fm;
    if (P.tine > 0) {
      s += P.tine * sinT(v.ph1 * 7.f) * v.tine;
      v.tine *= K.tine;
    }
  } else {
    s = osc(P.w1, v.ph1);
    if (P.mix2 > 0) {
      v.ph2 += inc * P.detune; if (v.ph2 >= 1.f) v.ph2 -= 1.f;
      s = s * (1.f - P.mix2) + osc(P.w2, v.ph2) * P.mix2;
    }
    if (P.sub > 0) {
      v.ph3 += inc * 0.5f; if (v.ph3 >= 1.f) v.ph3 -= 1.f;
      s += P.sub * sinT(v.ph3);
    }
  }
  if (P.cut > 0) {
    float fc = P.cut + P.cutEnv * v.fcEnv;
    v.fcEnv *= K.cut;
    float x = TWO_PI * fc / sr_;
    float a = x / (1.f + x);
    v.lp += a * (s - v.lp);
    s = v.lp;
  }
  return s * v.env * v.gain * P.gain;
}

float Core::renderDrum(Drum& d) {
  const DrumDef& D = DRUMS[d.def];
  const DrumCoef& K = dc_[d.def];
  d.n += 1.f;
  float t = d.n / sr_;
  float out = 0;
  if (D.noiseAmt > 0) {
    // claps: the noise is hit a few times in quick succession
    if (d.bursts > 1 && d.n >= d.nextBurst) { d.envN = 1.f; d.bursts--; d.nextBurst += 0.011f * sr_; }
    float x = noise();
    if (D.hpHz > 0) {
      d.hpY = K.hpA * (d.hpY + x - d.hpX);
      d.hpX = x;
      x = d.hpY;
    }
    out += x * d.envN * D.noiseAmt;
    d.envN *= (d.bursts > 1) ? K.burstNoise : K.noise;
  }
  if (D.toneAmt > 0) {
    float f = D.f1 + (D.f0 - D.f1) * d.sweepEnv;
    d.sweepEnv *= K.sweep;
    d.ph += f / sr_; if (d.ph >= 1.f) d.ph -= 1.f;
    out += sinT(d.ph) * d.envT * D.toneAmt;
    d.envT *= K.tone;
  }
  if (D.metalAmt > 0) {
    // a few square waves at inharmonic ratios
    static const float R[4] = {1.f, 1.4471f, 1.6170f, 1.9265f};
    float m = 0;
    for (int i = 0; i < 4; i++) {
      float p = frac(d.mph * R[i]);
      m += p < 0.5f ? 0.25f : -0.25f;
    }
    d.mph += D.metalHz / sr_; if (d.mph >= 1.f) d.mph -= 1.f;
    out += m * d.envM * D.metalAmt;
    d.envM *= K.metal;
  }
  if (D.click > 0 && t < 0.003f) out += noise() * D.click * (1.f - t / 0.003f);
  float level = d.envN + d.envT + d.envM;
  if (t > 0.02f && level < 0.0008f) d.active = false;
  if (t > 3.0f) d.active = false;
  return out * d.gain * D.gain;
}

void Core::render(int16_t* out, int n) {
  for (int i = 0; i < n; i++) {
    float sum = 0;
    for (auto& v : voices_) if (v.active) sum += renderVoice(v);
    for (auto& d : drums_) if (d.active) sum += renderDrum(d);
    sum *= master_;
    // soft clip: transparent when quiet, rounds the peaks instead of crackling
    float x = sum * 0.9f;
    if (x > 3.f) x = 3.f; else if (x < -3.f) x = -3.f;
    float y = x * (27.f + x * x) / (27.f + 9.f * x * x);
    // DC blocker (about 15 Hz): keeps a steady offset off the speaker
    float z = y - dcX_ + 0.998f * dcY_;
    dcX_ = y;
    dcY_ = z;
    out[i] = (int16_t)(z * 32000.f);
  }
}

}  // namespace synth
