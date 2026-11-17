// Scenes: parts shared between scenes on the device come to the website as `lk` lines, and the
// website's links go to the device as one stored part.
//   scenes_test
#include <Arduino.h>
#include <LittleFS.h>

#include <string>

#include "../../src/net/TrkrConvert.h"
#include "../../src/seq/Song.h"

LittleFSClass LittleFS;
namespace seq {
fs::FS* sdFs() { return nullptr; }
}  // namespace seq

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } } while (0)

int main() {
  // ── device -> website: scenes 1 and 3 play the same drum part ──
  seq::Song d;
  seq::songInitInto(d);
  d.songMode = true;
  seq::setStep(d.tracks[0].patterns[0], 0, (const uint8_t[]){36}, 1, 100, 6);
  seq::setStep(d.tracks[0].patterns[1], 4, (const uint8_t[]){38}, 1, 100, 6);
  seq::setStep(d.tracks[1].patterns[0], 0, (const uint8_t[]){40}, 1, 100, 6);
  seq::setStep(d.tracks[1].patterns[1], 8, (const uint8_t[]){43}, 1, 100, 6);
  // scene 0: drum 0, bass 0   scene 1: drum 1, bass 0   scene 2: drum 0, bass 1
  for (auto& sc : d.scenes) for (auto& p : sc.pattern) p = seq::SCENE_SILENT;
  d.scenes[0].pattern[0] = 0; d.scenes[0].pattern[1] = 0;
  d.scenes[1].pattern[0] = 1; d.scenes[1].pattern[1] = 0;
  d.scenes[2].pattern[0] = 0; d.scenes[2].pattern[1] = 1;
  d.arrange = {0, 1, 2, 0};
  trkr::Song w;
  const char* err = nullptr;
  CHECK(trkr::toWeb(d, nullptr, "Scenes", w, &err), "converts to the website");
  CHECK(w.patterns.size() == 3, "three scenes");
  auto has = [&](size_t p, int track, int from) {
    for (const auto& l : w.patterns[p].links) if (l.first == track && l.second == from) return true;
    return false;
  };
  CHECK(w.patterns[0].links.empty(), "scene 1 shares with nobody before it");
  CHECK(has(1, 1, 0), "scene 2 plays scene 1's bass part");
  CHECK(!has(1, 0, 0), "but not its drums");
  CHECK(has(2, 0, 0), "scene 3 plays scene 1's drums");
  CHECK(!has(2, 1, 0), "and its own bass");
  Print out;
  trkr::save(out, w);
  CHECK(out.out.find("lk 1 0\n") != std::string::npos && out.out.find("lk 0 0\n") != std::string::npos, "the text has the lk lines");
  trkr::Song back;
  Stream st;
  st.in = out.out;
  CHECK(trkr::load(st, back, &err), "reads them back");
  CHECK(back.patterns[2].links.size() == 1 && back.patterns[2].links[0].first == 0 && back.patterns[2].links[0].second == 0, "the links survive");

  // ── website -> device: two scenes with the same part store it once ──
  seq::Song d2;
  trkr::Meta meta;
  CHECK(trkr::toDevice(w, d2, meta, &err), "converts to the device");
  CHECK(d2.scenes[0].pattern[0] == d2.scenes[2].pattern[0], "scenes 1 and 3 play the same drum slot");
  CHECK(d2.scenes[0].pattern[1] == d2.scenes[1].pattern[1], "scenes 1 and 2 play the same bass slot");
  CHECK(d2.scenes[0].pattern[0] != d2.scenes[1].pattern[0], "and the other drum part is another slot");

  // ── nonsense lk lines are ignored ──
  trkr::Song bad;
  Stream s2;
  s2.in = "TRKR 1\nname B\nbpm 100\nswing 0\nkey 0 0\ntrack 1 lead lead 0 80 A\npattern 16 A\nlk 0 0\nlk 9 0\nlk 0 7\norder 0\nend\n";
  CHECK(trkr::load(s2, bad, &err) && bad.patterns[0].links.empty(), "a link to itself, to a missing track or pattern does nothing");

  printf("%s\n", failures ? "FAILED" : "scenes: all ok");
  return failures != 0;
}
