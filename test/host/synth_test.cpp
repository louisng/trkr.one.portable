// Measures the speaker synth: pitch accuracy, levels, release, voice stealing,
// drum decay and mix headroom. Run on a PC, no hardware.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/audio/SynthCore.h"

static const float SR = 22050.f;
static int fails = 0;
static void check(bool ok, const std::string& what) { if (!ok) { fails++; printf("  FAIL: %s\n", what.c_str()); } }

static std::vector<float> renderSeconds(synth::Core& c, float sec) {
  std::vector<int16_t> buf((size_t)(sec * SR));
  c.render(buf.data(), (int)buf.size());
  std::vector<float> f(buf.size());
  for (size_t i = 0; i < buf.size(); i++) f[i] = buf[i] / 32768.f;
  return f;
}
static float peakOf(const std::vector<float>& v, size_t a = 0, size_t b = 0) {
  if (!b) b = v.size();
  float p = 0; for (size_t i = a; i < b; i++) p = fmaxf(p, fabsf(v[i])); return p;
}
static float rmsOf(const std::vector<float>& v, size_t a, size_t b) {
  double s = 0; for (size_t i = a; i < b; i++) s += v[i] * v[i]; return (float)sqrt(s / (double)(b - a));
}
// Fundamental by autocorrelation, in Hz.
static float pitchOf(const std::vector<float>& v, size_t start, size_t len, float fmin, float fmax) {
  size_t lagMin = (size_t)(SR / fmax), lagMax = (size_t)(SR / fmin);
  double best = -1; size_t bestLag = 0;
  for (size_t lag = lagMin; lag <= lagMax; lag++) {
    double num = 0, e1 = 0, e2 = 0;
    for (size_t i = 0; i < len; i++) { double a = v[start + i], b = v[start + i + lag]; num += a * b; e1 += a * a; e2 += b * b; }
    double r = num / sqrt(e1 * e2 + 1e-12);
    if (r > best * 1.001) { best = r; bestLag = lag; }   // first strong peak wins (avoids octave-down picks)
  }
  return SR / bestLag;
}

int main() {
  const char* NAMES[synth::TB_COUNT] = {"ep", "piano", "pad", "organ", "brass", "pluck", "bell", "lead", "softlead", "whistle",
                                        "fingerbass", "synthbass", "sub808", "acidbass", "arp"};
  // ── every timbre: pitch, level, release, cleanliness ──────────────────────
  for (int tb = 0; tb < synth::TB_COUNT; tb++) {
    bool bass = tb == synth::TB_FINGERBASS || tb == synth::TB_SYNTHBASS || tb == synth::TB_SUBBASS || tb == synth::TB_ACIDBASS;
    int notes[3] = {bass ? 36 : 57, bass ? 41 : 64, bass ? 48 : 72};
    for (int note : notes) {
      synth::Core c(SR);
      c.noteOn(note, 100, tb, 1);
      auto v = renderSeconds(c, 1.0f);
      float expect = 440.f * powf(2.f, (note - 69) / 12.f);
      float pk = peakOf(v);
      char what[96];
      snprintf(what, sizeof what, "%s n%d level %.2f", NAMES[tb], note, pk);
      check(pk > 0.06f && pk < 0.99f, what);
      bool nan = false; double dc = 0;
      for (float x : v) { if (!std::isfinite(x)) nan = true; dc += x; }
      dc /= v.size();
      snprintf(what, sizeof what, "%s n%d finite/DC (%.4f)", NAMES[tb], note, dc);
      check(!nan && fabsf((float)dc) < 0.02f, what);
      if (tb != synth::TB_BELL) {   // bell is deliberately inharmonic
        size_t from = tb == synth::TB_SUBBASS ? (size_t)(0.45f * SR) : (size_t)(0.12f * SR);
        float f = pitchOf(v, from, 4096, expect * 0.7f, expect * 1.45f);
        snprintf(what, sizeof what, "%s n%d pitch %.1f Hz, wanted %.1f", NAMES[tb], note, f, expect);
        check(fabsf(f - expect) / expect < 0.03f, what);
      }
    }
    // release: hold 0.3 s, let go, silent soon after the release time
    synth::Core c(SR);
    c.noteOn(60, 100, tb, 1);
    auto held = renderSeconds(c, 0.3f);
    c.noteOff(60, 1);
    auto tail = renderSeconds(c, 2.5f);
    float endRms = rmsOf(tail, tail.size() - 2205, tail.size());
    char what[96];
    snprintf(what, sizeof what, "%s silent after release (rms %.5f)", NAMES[tb], endRms);
    check(endRms < 0.001f && c.activeVoices() == 0, what);
  }

  // ── voice stealing and owners ─────────────────────────────────────────────
  {
    synth::Core c(SR);
    for (int i = 0; i < 60; i++) c.noteOn(40 + i % 40, 100, synth::TB_PAD, 2);
    check(c.activeVoices() <= synth::Core::MAX_VOICES, "voices never exceed the limit");
    auto v = renderSeconds(c, 0.5f);
    check(peakOf(v) < 1.0f && std::isfinite(peakOf(v)), "60 simultaneous notes stay clean");
    c.allOff();
    check(c.activeVoices() == 0, "allOff");

    synth::Core d(SR);
    d.noteOn(60, 100, synth::TB_ORGAN, 1);   // two parts on the same pitch
    d.noteOn(60, 100, synth::TB_ORGAN, 2);
    renderSeconds(d, 0.1f);
    d.noteOff(60, 1);
    auto after = renderSeconds(d, 0.5f);
    check(rmsOf(after, after.size() - 2000, after.size()) > 0.02f, "releasing one owner leaves the other playing");
  }

  // ── drums ─────────────────────────────────────────────────────────────────
  {
    const int gm[] = {36, 38, 37, 39, 42, 46, 41, 45, 48, 49, 51, 56, 54, 70, 75};
    const char* dn[] = {"kick", "snare", "rim", "clap", "closed hat", "open hat", "lo tom", "mid tom", "hi tom", "crash", "ride", "cowbell", "tambourine", "shaker", "clave"};
    for (int i = 0; i < 15; i++) {
      synth::Core c(SR);
      c.drum(gm[i], 110);
      auto v = renderSeconds(c, 3.2f);
      char what[96];
      float pk = peakOf(v, 0, (size_t)(0.3f * SR));
      snprintf(what, sizeof what, "%s hits (peak %.2f)", dn[i], pk);
      check(pk > 0.05f && pk < 0.99f, what);
      snprintf(what, sizeof what, "%s decays away", dn[i]);
      check(rmsOf(v, v.size() - 4410, v.size()) < 0.001f && c.activeDrums() == 0, what);
      printf("  %-11s peak %.2f\n", dn[i], pk);
    }
    synth::Core c(SR);
    c.drum(46, 100); renderSeconds(c, 0.05f); c.drum(42, 100);
    check(c.activeDrums() == 1, "a closed hat chokes an open one");
  }

  // ── headroom: a busy moment at full velocity ──────────────────────────────
  {
    synth::Core c(SR);
    int chord[4] = {60, 64, 67, 71};
    for (int n : chord) c.noteOn(n, 127, synth::TB_EP, 5);
    for (int n : chord) c.noteOn(n + 12, 127, synth::TB_PAD, 7);
    c.noteOn(36, 127, synth::TB_FINGERBASS, 4);
    c.noteOn(84, 127, synth::TB_LEAD, 6);
    c.drum(36, 127); c.drum(38, 127); c.drum(42, 127); c.drum(49, 127);
    auto v = renderSeconds(c, 0.6f);
    size_t hot = 0; for (float x : v) if (fabsf(x) > 0.9f) hot++;
    float frac = (float)hot / v.size();
    printf("  busiest moment: peak %.2f, %.2f%% of samples above 0.9\n", peakOf(v), frac * 100);
    check(peakOf(v) <= 1.0f && frac < 0.02f, "full-velocity mix keeps its headroom");
  }
  printf(fails ? "FAILED (%d)\n" : "synth: all good\n", fails);
  return fails ? 1 : 0;
}
