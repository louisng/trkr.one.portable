// How much memory building a song needs, per genre: the generator's working
// memory plus the song it builds (what the device must hold at the same time).
#include <Arduino.h>
#include <LittleFS.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <new>

#include "../../src/gen/GenreData.h"
#include "../../src/gen/Generator.h"
#include "../../src/net/TrkrConvert.h"
#include "../../src/seq/Song.h"

LittleFSClass LittleFS;
namespace seq { fs::FS* sdFs() { return nullptr; } }

static size_t cur = 0, peak = 0;
void* operator new(size_t n) {
  size_t* p = (size_t*)malloc(n + 16);
  p[0] = n; cur += n; peak = std::max(peak, cur);
  return (char*)p + 16;
}
void operator delete(void* q) noexcept { if (!q) return; size_t* p = (size_t*)((char*)q - 16); cur -= p[0]; free(p); }
void operator delete(void* q, size_t) noexcept { operator delete(q); }

struct Sink2 : gen::Sink {
  explicit Sink2(seq::Song& o) : imp(o) {}
  trkr::Importer imp; trkr::Meta tm; gen::Meta meta; bool ok = true; const char* err = nullptr;
  bool begin(const trkr::Song& h, const gen::Meta& m) override { meta = m; ok = imp.begin(h, tm, &err); return ok; }
  bool pattern(trkr::Pattern&& p) override { ok = imp.addPattern(p, &err); return ok; }
};

int main(int argc, char** argv) {
  struct Row { const char* id; size_t worstPeak = 0, sumPeak = 0, worstSong = 0; int n = 0; };
  std::vector<Row> rows;
  for (int gi = 0; gi < gen::GENRE_COUNT; gi++) {
    Row r; r.id = gen::GENRE_LIST[gi]->id;
    for (int a = 1; a < argc; a++) {
      seq::Song* s = new seq::Song();
      size_t base = cur; peak = cur;
      Sink2 sink(*s);
      gen::generateStream(*gen::GENRE_LIST[gi], (uint32_t)strtoul(argv[a], nullptr, 10), sink);
      sink.imp.finish();
      size_t pk = peak - base;
      size_t notes = 0; for (auto& t : s->tracks) for (auto& p : t.patterns) notes += p.notes.size() * sizeof(seq::NoteEvent);
      r.worstPeak = std::max(r.worstPeak, pk); r.sumPeak += pk; r.worstSong = std::max(r.worstSong, notes); r.n++;
      delete s;
    }
    rows.push_back(r);
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.worstPeak > b.worstPeak; });
  printf("%-16s %10s %10s %10s\n", "genre", "worst KB", "average KB", "song KB");
  for (auto& r : rows) printf("%-16s %10zu %10zu %10zu\n", r.id, r.worstPeak / 1024, r.sumPeak / r.n / 1024, r.worstSong / 1024);
}
