// Arp steps between trkr.one and the device: the `ax` line, the A effect, saving, and back.
//   arpstep_test
#include <Arduino.h>
#include <LittleFS.h>

#include <string>

#include "../../src/net/TrkrConvert.h"
#include "../../src/play/Arp.h"
#include "../../src/seq/Song.h"

LittleFSClass LittleFS;
namespace seq {
fs::FS* sdFs() { return nullptr; }
}  // namespace seq

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } } while (0)

int main() {
  const uint8_t code = arp::encode(arp::UPDOWN, 1, 2);   // up/down, 1/8, 2 octaves
  Stream st;
  st.in = std::string("TRKR 1\nname A\nbpm 100\nswing 0\nkey 0 0\ntrack 1 drums kit 0 80 DRUM\ntrack 3 chords pad 0 80 CHRD\n") +
          "pattern 16 A\nc 1 0 90 144 0 60 64 67\nax 1 0 " + std::to_string(code) + "\nc 1 8 90 24 0 62 65 69\nsx 1 8 2\n" +
          "ax 1 5 9\nax 1 8 7\norder 0\nend\n";
  trkr::Song a;
  const char* err = nullptr;
  CHECK(trkr::load(st, a, &err), "parses");
  CHECK(a.patterns[0].arps.size() == 1 && a.patterns[0].arps[0].code == code, "one arp kept (no cell at step 6, a bad byte at step 9)");

  seq::Song d;
  trkr::Meta meta;
  CHECK(trkr::toDevice(a, d, meta, &err), "converts");
  const seq::Pattern& p = d.tracks[1].patterns[0];
  uint8_t cmd = 0, val = 0;
  CHECK(seq::fxAt(p, 0, &cmd, &val) && cmd == seq::FX_ARP && val == code, "step 1 has the A effect with the same byte");
  CHECK(seq::fxAt(p, 8, &cmd, &val) && cmd == seq::FX_STRUM, "step 9 keeps its strum");
  CHECK(seq::songSaveCopy("a", d), "saves");
  seq::Song d2;
  CHECK(seq::songLoadCopy("a", d2) && seq::fxAt(d2.tracks[1].patterns[0], 0, &cmd, &val) && cmd == seq::FX_ARP && val == code, "and loads");

  trkr::Song c;
  CHECK(trkr::toWeb(d2, &meta, "x", c, &err), "back to the website");
  CHECK(c.patterns[0].arps.size() == 1 && c.patterns[0].arps[0].step == 0 && c.patterns[0].arps[0].code == code, "the arp came back");
  Print out;
  trkr::save(out, c);
  CHECK(out.out.find("ax 1 0 " + std::to_string(code) + "\n") != std::string::npos, "the text has its ax line");

  // ── deleting a step's notes takes its arp (and strum) with them ──
  {
    seq::Pattern q;
    q.length = 16;
    const uint8_t chord[] = {60, 64, 67};
    seq::setStep(q, 0, chord, 3, 100, 48);
    seq::setFx(q, 0, seq::FX_ARP, code);
    const uint8_t other[] = {62, 65, 69};
    seq::setStep(q, 0, other, 3, 100, 48);
    CHECK(seq::fxAt(q, 0, &cmd, &val) && cmd == seq::FX_ARP, "a new chord on the step keeps its arp");
    seq::clearStep(q, 0);
    CHECK(!seq::fxAt(q, 0, nullptr, nullptr), "clearing the step clears its arp");
    seq::setStep(q, 2, chord, 3, 100, 12);
    seq::setFx(q, 2, seq::FX_STRUM, 2);
    seq::removeNote(q, 2, 60); seq::removeNote(q, 2, 64);
    CHECK(seq::fxAt(q, 2, &cmd, nullptr) && cmd == seq::FX_STRUM, "while a note is left, the effect stays");
    seq::removeNote(q, 2, 67);
    CHECK(!seq::fxAt(q, 2, nullptr, nullptr), "removing the last note clears it");
    seq::toggleNote(q, 4, 36, 100, 12);
    seq::setFx(q, 4, seq::FX_RATCHET, 3);
    seq::toggleNote(q, 4, 36, 100, 12);
    CHECK(!seq::fxAt(q, 4, nullptr, nullptr), "toggling the last note off clears it");
    seq::setStep(q, 6, chord, 3, 100, 12);
    seq::setFx(q, 6, seq::FX_MOD, 64);
    seq::clearStep(q, 6);
    CHECK(seq::fxAt(q, 6, &cmd, nullptr) && cmd == seq::FX_MOD, "mod wheel works without a note: it stays");
    seq::setStep(q, 8, chord, 3, 100, 12);
    seq::setFx(q, 8, seq::FX_ARP, code);
    seq::setStepOff(q, 8);
    CHECK(!seq::fxAt(q, 8, nullptr, nullptr), "an OFF step has no arp");
  }

  printf("%s\n", failures ? "FAILED" : "arp steps: all ok");
  return failures != 0;
}
