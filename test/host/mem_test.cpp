// Peak memory the generator itself needs (not counting the song it makes).
#include <Arduino.h>
#include <cstdlib>
#include <new>
#include <algorithm>

#include "../../src/gen/GenreData.h"
#include "../../src/gen/Generator.h"

static size_t cur = 0, peak = 0;
void* operator new(size_t n) {
  size_t* p = (size_t*)malloc(n + 16);
  p[0] = n; cur += n; peak = std::max(peak, cur);
  return (char*)p + 16;
}
void operator delete(void* q) noexcept { if (!q) return; size_t* p = (size_t*)((char*)q - 16); cur -= p[0]; free(p); }
void operator delete(void* q, size_t) noexcept { operator delete(q); }

struct Null : gen::Sink {
  size_t held = 0, maxHeld = 0;
  bool begin(const trkr::Song&, const gen::Meta&) override { return true; }
  bool pattern(trkr::Pattern&& p) override { (void)p; return true; }   // dropped: only the generator's own memory counts
};

int main(int argc, char** argv) {
  size_t worst = 0; const char* worstG = "";
  for (int gi = 0; gi < gen::GENRE_COUNT; gi++)
    for (int a = 1; a < argc; a++) {
      const gen::Genre& g = *gen::GENRE_LIST[gi];
      Null sink;
      size_t base = cur; peak = cur;
      gen::generateStream(g, (uint32_t)strtoul(argv[a], nullptr, 10), sink);
      if (peak - base > worst) { worst = peak - base; worstG = g.id; }
    }
  printf("generator working memory, worst case over all genres/seeds: %zu bytes (%zu KB)  [%s]\n", worst, worst / 1024, worstG);
}
