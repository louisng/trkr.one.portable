// Generates songs on the PC with the firmware's generator and writes them in
// the TRKR 1 text format, to be compared with trkr's JavaScript generator.
//   gen_test <outdir> <seed>...      (one file per genre and seed)
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "../../src/gen/GenreData.h"
#include "../../src/gen/Generator.h"

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: gen_test <outdir> <seed>...\n"); return 2; }
  std::string dir = argv[1];
  int bad = 0;
  for (int gi = 0; gi < gen::GENRE_COUNT; gi++) {
    const gen::Genre& g = *gen::GENRE_LIST[gi];
    for (int a = 2; a < argc; a++) {
      uint32_t seed = (uint32_t)strtoul(argv[a], nullptr, 10);
      song::Song s;
      gen::Meta meta;
      if (!gen::generate(g, seed, s, meta)) { fprintf(stderr, "%s #%u: too big\n", g.id, seed); bad++; continue; }
      Print out;
      song::save(out, s);
      std::ofstream(dir + "/" + g.id + "_" + argv[a] + ".trk") << out.out;
      std::string mt;
      char ln[96];
      for (auto& c : meta.chords) { snprintf(ln, sizeof ln, "%d %s %u %u\n", c.start, c.sym, c.pcs, c.scale); mt += ln; }
      for (auto& sc : meta.sections) { snprintf(ln, sizeof ln, "%s %u %u %.2f\n", sc.name, sc.bar, sc.bars, sc.energy); mt += ln; }
      std::ofstream(dir + "/" + g.id + "_" + argv[a] + ".meta") << mt;
    }
  }
  return bad ? 1 : 0;
}
