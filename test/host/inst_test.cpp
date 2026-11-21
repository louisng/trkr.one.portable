// A track's sound settings (`ti` line) between trkr.one and the device.
//   inst_test
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

static bool parse(const std::string& text, trkr::Song& s) {
  Stream st;
  st.in = text;
  const char* err = nullptr;
  return trkr::load(st, s, &err);
}

static const char* HEAD = "TRKR 1\nname Sounds\nbpm 100\nswing 0\nkey 0 0\ntrack 10 drums kit 0 86 DRUM\ntrack 2 bass synthbass 0 50 BASS\n";
static const char* TAIL = "pattern 16 A\nc 1 0 100 24 0 36\norder 0\nend\n";

int main() {
  // a song with sound settings: square wave, octave -1, a slow filter
  trkr::Song a;
  CHECK(parse(std::string(HEAD) + "ti 1 1 1 -1 12 3 40 90 25 77 33 66\n" + TAIL, a), "parses");
  CHECK(a.tracks[1].hasInst && !a.tracks[0].hasInst, "only the track with a ti line has one");
  const auto& i = a.tracks[1].inst;
  CHECK(i.sound == 1 && i.wave == 1 && i.octave == -1 && i.detune == 12 && i.attack == 3 && i.decay == 40 && i.sustain == 90 &&
            i.release == 25 && i.cutoff == 77 && i.resonance == 33 && i.envMod == 66, "all eleven values");

  seq::Song d;
  trkr::Meta meta;
  const char* err = nullptr;
  CHECK(trkr::toDevice(a, d, meta, &err), "converts");
  const seq::Instrument& di = d.tracks[1].inst;
  CHECK(di.wave == seq::WAVE_SQUARE && di.octave == -1 && di.detune == 12 && di.attack == 3 && di.decay == 40 && di.sustain == 90 &&
            di.release == 25 && di.cutoff == 77 && di.resonance == 33 && di.envMod == 66, "the device has the settings");
  CHECK(di.volume == 50 * 127 / 100, "volume is still the track line's");
  // the drum track (no ti line) is the device's own kit sound, the bass (with ti) is not the Bass preset
  seq::Instrument bass;
  seq::applyPreset(bass, 0);
  CHECK(memcmp(&bass, &di, sizeof(bass)) != 0, "not the preset");

  // a song without ti lines: the sound comes from the voice name, as it always did
  trkr::Song o;
  CHECK(parse(std::string(HEAD) + TAIL, o) && !o.tracks[1].hasInst, "no ti: no settings");
  seq::Song od;
  trkr::Meta m2;
  CHECK(trkr::toDevice(o, od, m2, &err), "converts");
  seq::Instrument want;
  seq::applyPreset(want, 0);   // synthbass -> the Bass preset
  CHECK(od.tracks[1].inst.wave == want.wave && od.tracks[1].inst.cutoff == want.cutoff && od.tracks[1].inst.attack == want.attack, "voice name -> preset, unchanged");

  // a trip back: the device's sounds come with the song, and survive saving
  CHECK(seq::songSaveCopy("i", d), "saves");
  seq::Song d2;
  CHECK(seq::songLoadCopy("i", d2), "loads");
  trkr::Song c;
  CHECK(trkr::toWeb(d2, &meta, "x", c, &err), "converts back");
  CHECK(c.tracks[1].hasInst && c.tracks[1].inst.wave == 1 && c.tracks[1].inst.octave == -1 && c.tracks[1].inst.cutoff == 77, "web: the sound came back");
  CHECK(c.tracks[0].hasInst, "web: the drum track's sound too (the device always sends them)");
  Print out;
  trkr::save(out, c);
  CHECK(out.out.find("ti 1 1 1 -1 12 3 40 90 25 77 33 66\n") != std::string::npos, "the text has its ti line");

  // bad lines are ignored
  trkr::Song b;
  CHECK(parse(std::string(HEAD) + "ti 1 1 1\nti 9 1 1 0 0 0 0 0 0 0 0 0\nti 1 1 9 99 999 0 0 0 0 0 0 0\n" + TAIL, b), "parses");
  CHECK(!b.tracks[0].hasInst, "a short or out-of-range ti line does nothing");
  CHECK(b.tracks[1].hasInst && b.tracks[1].inst.wave == 5 && b.tracks[1].inst.octave == 12 && b.tracks[1].inst.detune == 127, "numbers are clamped");

  printf("%s\n", failures ? "FAILED" : "inst: all ok");
  return failures != 0;
}
