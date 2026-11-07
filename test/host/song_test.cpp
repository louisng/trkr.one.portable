#include <fstream>
#include <iostream>
#include <sstream>
#include "../../src/song/Song.h"
int main(int argc, char** argv) {
  std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf();
  Stream in; in.in = ss.str();
  song::Song s; const char* err = nullptr;
  if (!song::load(in, s, &err)) { std::cerr << "load failed: " << (err ? err : "?") << "\n"; return 1; }
  Print out; song::save(out, s);
  std::cout << "tracks " << (int)s.trackCount << " patterns " << s.patterns.size() << " events " << s.eventCount()
            << " steps " << s.totalSteps() << " bpm " << s.bpm << "\n";
  if (out.out != in.in) { std::ofstream("roundtrip.trk") << out.out; std::cerr << "round trip differs (see roundtrip.trk)\n"; return 2; }
  std::cout << "round trip identical\n";
  return 0;
}
