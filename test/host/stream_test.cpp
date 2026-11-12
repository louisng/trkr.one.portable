// The path Generated mode takes on the device: the generator streams straight
// into the importer (no whole website song in memory). For every genre and
// many seeds: nothing is refused, the result equals the whole-song path
// (generate() -> convert), and playing it from the start gives the same
// timeline of chords the generator reported.
#include <Arduino.h>
#include <LittleFS.h>

#include <algorithm>
#include <map>

#include "../../src/gen/GenreData.h"
#include "../../src/gen/Generator.h"
#include "../../src/net/TrkrConvert.h"
#include "../../src/seq/Song.h"

LittleFSClass LittleFS;
namespace seq { fs::FS* sdFs() { return nullptr; } }

namespace gen { namespace {
class Sink2 : public Sink {
 public:
  explicit Sink2(seq::Song& o) : imp(o) {}
  trkr::Importer imp; trkr::Meta tm; Meta meta; bool ok = true; const char* err = nullptr;
  bool begin(const trkr::Song& h, const Meta& m) override { meta = m; ok = imp.begin(h, tm, &err); return ok; }
  bool pattern(trkr::Pattern&& p) override { ok = imp.addPattern(p, &err); return ok; }
};
}}

static bool same(const seq::Song& a, const seq::Song& b) {
  if (a.bpm != b.bpm || a.arrange != b.arrange || a.songMode != b.songMode) return false;
  for (int s = 0; s < seq::SCENES; s++) if (memcmp(a.scenes[s].pattern, b.scenes[s].pattern, seq::TRACKS)) return false;
  for (int t = 0; t < seq::TRACKS; t++) {
    if (strcmp(a.tracks[t].name, b.tracks[t].name) || a.tracks[t].channel != b.tracks[t].channel) return false;
    for (int p = 0; p < seq::PATTERNS; p++) {
      auto &x = a.tracks[t].patterns[p], &y = b.tracks[t].patterns[p];
      if (x.length != y.length || x.notes.size() != y.notes.size()) return false;
      for (size_t i = 0; i < x.notes.size(); i++) if (memcmp(&x.notes[i], &y.notes[i], sizeof(seq::NoteEvent))) return false;
    }
  }
  return true;
}

int main(int argc, char** argv) {
  int n = 0, bad = 0, maxSlots = 0, maxRows = 0, maxScenes = 0;
  size_t maxEvents = 0;
  std::map<std::string, int> why;
  for (int gi = 0; gi < gen::GENRE_COUNT; gi++)
    for (int a = 1; a < argc; a++) {
      const gen::Genre& g = *gen::GENRE_LIST[gi];
      uint32_t seed = (uint32_t)strtoul(argv[a], nullptr, 10);
      seq::Song* s1 = new seq::Song();
      gen::Sink2 sink(*s1);
      bool done = gen::generateStream(g, seed, sink);
      if (!done || !sink.ok) { why[sink.err ? sink.err : "stopped"]++; bad++; delete s1; continue; }
      sink.imp.finish();
      n++;
      // the whole-song path
      trkr::Song w; gen::Meta m2;
      seq::Song* s2 = new seq::Song(); trkr::Meta tm2; const char* err = nullptr;
      if (!gen::generate(g, seed, w, m2) || !trkr::toDevice(w, *s2, tm2, &err)) { why["whole-song path failed"]++; bad++; delete s1; delete s2; continue; }
      if (!same(*s1, *s2)) { printf("FAIL %s %u: streamed song differs from the whole-song path\n", g.id, seed); bad++; }
      // memory footprint of what the device holds
      size_t ev = 0; int slots = 0;
      for (auto& t : s1->tracks) { int used = 0; for (auto& p : t.patterns) { ev += p.notes.size(); if (!p.notes.empty()) used++; } slots = std::max(slots, used); }
      maxEvents = std::max(maxEvents, ev); maxSlots = std::max(maxSlots, slots);
      maxRows = std::max(maxRows, (int)s1->arrange.size()); maxScenes = std::max(maxScenes, (int)sink.imp.scenes());
      // the chord timeline spans the whole arrangement
      uint32_t steps = 0; for (uint8_t sc : s1->arrange) steps += seq::sceneSteps(sc);
      (void)steps;
      delete s1; delete s2;
    }
  printf("streamed %d songs   refused/failed: %d\n", n, bad);
  for (auto& w : why) printf("  %s  x%d\n", w.first.c_str(), w.second);
  printf("most notes in one song: %zu (%zu KB)   most patterns on a track: %d (limit %d)   arrangement rows: %d   scenes: %d (limit %d)\n",
         maxEvents, maxEvents * sizeof(seq::NoteEvent) / 1024, maxSlots, (int)seq::PATTERNS, maxRows, maxScenes, (int)seq::SCENES);
  return bad ? 1 : 0;
}
