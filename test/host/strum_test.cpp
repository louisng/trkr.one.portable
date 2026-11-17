// Strum on the device: the `sx` line of the trkr format, the step effect it becomes, saving it,
// the way back to the website format, and the timing rules shared with trkr.one (strum.js).
//   strum_test
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

static const char* TEXT_PREFIX =
    "TRKR 1\nname Strum\nbpm 100\nswing 0\nkey 0 0\n"
    "track 1 drums kit 0 80 Kick\ntrack 2 chords pad 0 80 Keys\npattern 16 A\n";

static bool parse(const std::string& body, trkr::Song& s) {
  Stream st;
  st.in = std::string(TEXT_PREFIX) + body + "order 0\nend\n";
  const char* err = nullptr;
  return trkr::load(st, s, &err);
}

int main() {
  // ── timing rules, the same numbers as test/strum.test.mjs on the website ──
  const int want[7][2] = {{0, 0}, {2, 0}, {4, 0}, {8, 0}, {2, 1}, {4, 1}, {8, 1}};  // gap in 1/24ths, up?
  for (uint8_t v = 1; v <= 6; v++) {
    CHECK(seq::strumValid(v), "codes 1-6 are valid");
    CHECK(seq::strumGapTicks(v) * 2 == want[v][0], "gap: fast 2, medium 4, slow 8 (in 1/24ths of a step)");
    CHECK(seq::strumUp(v) == (bool)want[v][1], "1-3 down, 4-6 up");
  }
  CHECK(!seq::strumValid(0) && !seq::strumValid(7), "0 and 7 are not strums");
  // four notes lowest first: a down strum plays them 0 1 2 3, an up strum 3 2 1 0
  for (uint8_t i = 0; i < 4; i++) {
    CHECK(seq::strumIndex(2, i, 4) == i, "down: lowest first");
    CHECK(seq::strumIndex(5, i, 4) == 3 - i, "up: highest first");
  }

  // ── a strummed website song becomes strum effects on the device ──
  trkr::Song a;
  CHECK(parse("c 1 0 100 96 0 60 64 67\nsx 1 0 5\nc 1 4 90 24 0 62 65 69\nc 0 2 100 24 0 36\nsx 0 2 3\n", a), "song with sx parses");
  CHECK(a.patterns[0].events.size() == 3, "three cells");
  CHECK(a.patterns[0].events[0].strum == 5, "the first chord is strummed (up, medium)");
  // (events are sorted by step: chord, kick, chord)
  CHECK(a.patterns[0].events[1].strum == 0 && a.patterns[0].events[1].count == 1, "one note can't be strummed");
  CHECK(a.patterns[0].events[2].strum == 0, "the second chord is not strummed");

  seq::Song d;
  trkr::Meta meta;
  const char* err = nullptr;
  CHECK(trkr::toDevice(a, d, meta, &err), "converts to the device");
  const seq::Pattern& kp = d.tracks[1].patterns[0];
  uint8_t cmd = 0, val = 0;
  CHECK(seq::fxAt(kp, 0, &cmd, &val) && cmd == seq::FX_STRUM && val == 5, "step 1 has the strum effect (5)");
  CHECK(!seq::fxAt(kp, 4, nullptr, nullptr), "step 5 has none");
  CHECK(kp.notes.size() == 6, "all the notes are still there");

  // the device can save it and load it again
  CHECK(seq::songSaveCopy("s", d), "saves");
  seq::Song d2;
  CHECK(seq::songLoadCopy("s", d2), "loads");
  CHECK(seq::fxAt(d2.tracks[1].patterns[0], 0, &cmd, &val) && cmd == seq::FX_STRUM && val == 5, "the strum survives saving and loading");

  // and goes back to the website with its strum
  trkr::Song c;
  CHECK(trkr::toWeb(d2, &meta, "x", c, &err), "converts back");
  bool found = false;
  for (const auto& e : c.patterns[0].events)
    if (e.track == 1 && e.step == 0) { found = true; CHECK(e.strum == 5, "web: the strum came back"); }
    else if (e.track == 1 && e.step == 4) CHECK(e.strum == 0, "web: the other chord has none");
  CHECK(found, "web: the strummed chord is there");
  Print out;
  trkr::save(out, c);
  CHECK(out.out.find("sx 1 0 5\n") != std::string::npos, "the text has an sx line for it");
  CHECK(out.out.find("sx 1 4") == std::string::npos, "and only for it");

  // ── the same music with and without a strum are different patterns ──
  trkr::Song plain, strummed;
  CHECK(parse("c 1 0 100 96 0 60 64 67\n", plain) && parse("c 1 0 100 96 0 60 64 67\nsx 1 0 2\n", strummed), "parse both");
  seq::Song dp, ds;
  trkr::Meta m1, m2;
  trkr::Importer imp(dp);
  CHECK(imp.begin(plain, m1, &err) && imp.addPattern(plain.patterns[0], &err) && imp.addPattern(strummed.patterns[0], &err), "two patterns, one strummed");
  imp.finish();
  CHECK(imp.scenes() == 2, "they stay two scenes: a strum is a difference");

  // ── old files: no sx, or nonsense, or a one-note cell ──
  trkr::Song o;
  CHECK(parse("c 1 0 100 96 0 60 64 67\n", o) && o.patterns[0].events[0].strum == 0, "no sx: not strummed");
  CHECK(parse("c 1 0 100 96 0 60 64 67\nsx 1 0 9\nsx 1 0 0\nsx 1 5 2\nsx 7 0 2\n", o) && o.patterns[0].events[0].strum == 0, "bad sx lines are ignored");
  CHECK(parse("c 1 0 100 96 0 60\nsx 1 0 3\n", o) && o.patterns[0].events[0].strum == 0, "a single note is not strummed");

  printf("%s\n", failures ? "FAILED" : "strum: all ok");
  return failures != 0;
}
