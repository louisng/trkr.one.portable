// Renders a generated song through the speaker synth to a WAV file, so the
// sound can be judged on a PC before flashing.
//   render_wav <genre id> <seed> <out.wav> [seconds]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/audio/SynthCore.h"
#include "../../src/gen/GenreData.h"
#include "../../src/gen/Generator.h"

struct Cmd { double t; int kind; uint8_t note, vel, timbre, owner; };  // kind 0 off, 1 on, 2 drum

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: render_wav <genre> <seed> <out.wav> [seconds]\n"); return 2; }
  const gen::Genre* g = nullptr;
  for (int i = 0; i < gen::GENRE_COUNT; i++) if (!strcmp(gen::GENRE_LIST[i]->id, argv[1])) g = gen::GENRE_LIST[i];
  if (!g) { fprintf(stderr, "unknown genre\n"); return 2; }
  uint32_t seed = (uint32_t)strtoul(argv[2], nullptr, 10);
  double limit = argc > 4 ? atof(argv[4]) : 1e9;
  song::Song s; gen::Meta meta;
  gen::generate(*g, seed, s, meta);

  const double stepSec = 60.0 / s.bpm / 4;
  std::vector<Cmd> cmds;
  uint32_t step0 = 0;
  for (uint8_t pi : s.order) {
    const song::Pattern& p = s.patterns[pi];
    for (const auto& e : p.events) {
      const song::Track& t = s.tracks[e.track];
      bool drum = t.type == song::TrackType::Drum;
      uint8_t tb = synth::timbreForVoice(t.voice, (int)t.type);
      double at = (step0 + e.step) * stepSec + e.delay / 24.0 * stepSec;
      double strum = t.strum / 24.0 * stepSec, dur = std::max(0.005, e.len24 / 24.0 * stepSec);
      for (int k = 0; k < e.count; k++) {
        double on = at + k * strum;
        if (drum) cmds.push_back({on, 2, e.notes[k], e.vel, 0, (uint8_t)e.track});
        else {
          cmds.push_back({on, 1, e.notes[k], e.vel, tb, (uint8_t)e.track});
          cmds.push_back({on + dur, 0, e.notes[k], 0, tb, (uint8_t)e.track});
        }
      }
    }
    step0 += p.len;
  }
  std::stable_sort(cmds.begin(), cmds.end(), [](const Cmd& a, const Cmd& b) { return a.t < b.t; });

  const float SR = 22050.f;
  synth::Core core(SR);
  double total = std::min(limit, step0 * stepSec + 2.5);
  size_t n = (size_t)(total * SR);
  std::vector<int16_t> pcm(n);
  size_t ci = 0;
  const int BLOCK = 128;
  for (size_t pos = 0; pos < n; pos += BLOCK) {
    double blockEnd = (pos + BLOCK) / (double)SR;
    while (ci < cmds.size() && cmds[ci].t < blockEnd) {
      const Cmd& c = cmds[ci++];
      if (c.kind == 1) core.noteOn(c.note, c.vel, c.timbre, c.owner);
      else if (c.kind == 0) core.noteOff(c.note, c.owner);
      else core.drum(c.note, c.vel);
    }
    core.render(&pcm[pos], (int)std::min<size_t>(BLOCK, n - pos));
  }
  // stats
  double sum2 = 0, dc = 0; size_t hot = 0; int peak = 0; bool bad = false;
  for (int16_t x : pcm) { sum2 += (double)x * x; dc += x; peak = std::max(peak, abs((int)x)); if (abs((int)x) > 29000) hot++; }
  printf("%-12s %.0f s  bpm %u  peak %.2f  rms %.3f  dc %.4f  near-clip %.2f%%\n", g->id, total, s.bpm, peak / 32768.0,
         sqrt(sum2 / n) / 32768.0, dc / n / 32768.0, 100.0 * hot / n);
  if (hot * 100.0 / n > 3.0 || fabs(dc / n / 32768.0) > 0.02) bad = true;

  FILE* f = fopen(argv[3], "wb");
  uint32_t dataBytes = (uint32_t)(n * 2), rate = (uint32_t)SR, byteRate = rate * 2;
  uint16_t ch = 1, bits = 16, align = 2, fmt = 1; uint32_t fmtLen = 16, riff = 36 + dataBytes;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
  fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
  fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
  fwrite(pcm.data(), 2, n, f);
  fclose(f);
  return bad ? 1 : 0;
}
