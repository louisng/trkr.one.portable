// Checks the website <-> device song converter on real songs from trkr.one.
//   convert_test song1.trk song2.trk ...
// For each song: parse, convert to the device format, save and reload it with
// the firmware's own song file code, convert back, and check the music is the
// same (apart from the rounding the device's finer/coarser timing causes).
#include <Arduino.h>
#include <LittleFS.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

#include "../../src/net/TrkrConvert.h"
#include "../../src/seq/Song.h"

LittleFSClass LittleFS;
namespace seq {
fs::FS* sdFs() { return nullptr; }
}  // namespace seq

using Note = std::tuple<int, int, int, int, int, int>;  // step, track, pitch, vel, len24, delay

static int failures = 0;
static void bad(const std::string& f, const std::string& m) {
  printf("  FAIL %s: %s\n", f.c_str(), m.c_str());
  failures++;
}

// What a website pattern's notes become after a trip through the device.
static std::vector<Note> expected(const trkr::Song& a, const trkr::Pattern& p, uint8_t trackCount) {
  std::vector<Note> v;
  for (const auto& e : p.events) {
    if (e.track >= trackCount) continue;
    bool drum = a.tracks[e.track].type == trkr::TrackType::Drum;
    int len = std::max(1, e.len24 / 2) * 2;
    int delay = std::max(-6, std::min(5, e.delay / 2)) * 2;  // the device\'s offset reaches -6..+5 ticks
    // the device keeps one length / velocity per step, taken from the first note
    for (uint8_t k = 0; k < e.count; k++) {
      int pitch = e.notes[k];
      if (drum) {
        pitch = trkr::mapDrumNote((uint8_t)pitch);
        if (pitch < 0) continue;
      }
      v.emplace_back(e.step, e.track, pitch, std::max(1, std::min(127, (int)e.vel)), len, delay);
    }
  }
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end(), [](const Note& x, const Note& y) {
            return std::get<0>(x) == std::get<0>(y) && std::get<1>(x) == std::get<1>(y) &&
                   std::get<2>(x) == std::get<2>(y);
          }),
          v.end());
  return v;
}

static std::vector<Note> actual(const trkr::Pattern& p) {
  std::vector<Note> v;
  for (const auto& e : p.events)
    for (uint8_t k = 0; k < e.count; k++) v.emplace_back(e.step, e.track, e.notes[k], e.vel, e.len24, e.delay);
  std::sort(v.begin(), v.end());
  return v;
}

static bool sameDeviceSong(const seq::Song& a, const seq::Song& b) {
  if (a.bpm != b.bpm || a.songMode != b.songMode || a.arrange != b.arrange) return false;
  for (int s = 0; s < seq::SCENES; s++)
    if (memcmp(a.scenes[s].pattern, b.scenes[s].pattern, seq::TRACKS) != 0) return false;
  for (int t = 0; t < seq::TRACKS; t++) {
    const auto &x = a.tracks[t], &y = b.tracks[t];
    if (strcmp(x.name, y.name) || x.kind != y.kind || x.channel != y.channel || x.mute != y.mute ||
        x.hold != y.hold || memcmp(&x.inst, &y.inst, sizeof(x.inst)))
      return false;
    for (int p = 0; p < seq::PATTERNS; p++) {
      const auto &m = x.patterns[p], &n = y.patterns[p];
      if (m.length != n.length || m.notes.size() != n.notes.size()) return false;
      for (size_t i = 0; i < m.notes.size(); i++)
        if (memcmp(&m.notes[i], &n.notes[i], sizeof(seq::NoteEvent)) != 0) return false;
    }
  }
  return true;
}

// How many different bits of music the busiest track plays across the play order.
static int patternsNeeded(const trkr::Song& a) {
  std::vector<uint8_t> order = a.order;
  if (order.empty()) for (size_t p = 0; p < a.patterns.size(); p++) order.push_back((uint8_t)p);
  int worst = 0;
  for (int t = 0; t < a.trackCount && t < seq::TRACKS; t++) {
    std::vector<std::vector<Note>> seen;
    for (uint8_t o : order) {
      if (o >= a.patterns.size()) continue;
      std::vector<Note> v;
      for (auto& n : expected(a, a.patterns[o], a.trackCount)) if (std::get<1>(n) == t) v.push_back(n);
      if (v.empty()) continue;
      v.emplace_back(-1, a.patterns[o].len, 0, 0, 0, 0);  // length is part of the pattern
      if (std::find(seen.begin(), seen.end(), v) == seen.end()) seen.push_back(v);
    }
    worst = std::max(worst, (int)seen.size());
  }
  return worst;
}

int main(int argc, char** argv) {
  std::map<int, int> need;
  int tested = 0, refused = 0, maxScenes = 0, maxSlots = 0;
  std::map<std::string, int> reasons;
  for (int i = 1; i < argc; i++) {
    std::string name = argv[i];
    std::ifstream in(name);
    std::stringstream ss;
    ss << in.rdbuf();
    Stream st;
    st.in = ss.str();
    trkr::Song a;
    const char* err = nullptr;
    if (!trkr::load(st, a, &err)) {
      bad(name, std::string("website song didn't parse: ") + (err ? err : "?"));
      continue;
    }
    need[patternsNeeded(a)]++;
    seq::Song b;
    trkr::Meta meta;
    if (!trkr::toDevice(a, b, meta, &err)) {
      refused++;
      reasons[err ? err : "?"]++;
      continue;
    }
    tested++;

    // 1. the device's own file code can save and reload it unchanged
    if (!seq::songSaveCopy("t", b)) { bad(name, "device couldn't save the converted song"); continue; }
    {
      const std::string& file = LittleFS.store.files["/songs/t.cms"];
      int most = 0;
      for (auto& t : b.tracks) {
        int top = 0;
        for (int p = 0; p < seq::PATTERNS; p++) if (!t.patterns[p].notes.empty()) top = p + 1;
        most = std::max(most, top);
      }
      int want = most > seq::LEGACY_PATTERNS ? 7 : 6;
      if (file.size() < 5 || (int)(uint8_t)file[4] != want) bad(name, "saved with the wrong file version");
    }
    seq::Song b2;
    if (!seq::songLoadCopy("t", b2) || !sameDeviceSong(b, b2)) { bad(name, "device save/load changed the song"); continue; }

    // 2. back to the website format
    trkr::Meta meta2;
    char mt[1024];
    trkr::metaToText(meta, mt, sizeof(mt));
    if (!trkr::metaFromText(mt, meta2)) { bad(name, "meta text didn't parse back"); continue; }
    trkr::Song c;
    if (!trkr::toWeb(b2, &meta2, "ignored", c, &err)) { bad(name, std::string("toWeb: ") + (err ? err : "?")); continue; }

    // 3. same song: header, tracks and, in play order, every pattern
    if (strcmp(c.name, a.name)) bad(name, std::string("title changed: ") + c.name);
    if (c.bpm != std::max<int>(40, std::min<int>(240, a.bpm))) bad(name, "bpm changed");
    if (c.swing != a.swing || c.key != a.key || c.scale != a.scale) bad(name, "swing/key/scale lost");
    uint8_t nT = std::min<uint8_t>(a.trackCount, seq::TRACKS);
    if (c.trackCount != nT) { bad(name, "track count changed"); continue; }
    for (int t = 0; t < nT; t++) {
      const auto &x = a.tracks[t], &y = c.tracks[t];
      if (strcmp(x.name, y.name) || strcmp(x.voice, y.voice) || x.type != y.type || x.ch != y.ch || x.mute != y.mute ||
          x.vol != y.vol || x.strum != y.strum)
        bad(name, "track " + std::to_string(t) + " settings changed (" + x.name + " / " + y.name + ")");
    }
    std::vector<uint8_t> order = a.order;
    order.erase(std::remove_if(order.begin(), order.end(), [&](uint8_t p) { return p >= a.patterns.size(); }), order.end());
    if (order.empty()) for (size_t p = 0; p < a.patterns.size(); p++) order.push_back((uint8_t)p);
    if (order.size() > seq::ARRANGE_MAX) order.resize(seq::ARRANGE_MAX);
    if (c.order.size() != order.size()) { bad(name, "order length changed"); continue; }
    for (size_t o = 0; o < order.size(); o++) {
      const trkr::Pattern& pa = a.patterns[order[o]];
      const trkr::Pattern& pc = c.patterns[c.order[o]];
      if (pa.len != pc.len) { bad(name, "pattern " + std::to_string(o) + " length " + std::to_string(pa.len) + " -> " + std::to_string(pc.len)); break; }
      auto ex = expected(a, pa, nT), ac = actual(pc);
      if (ex != ac) {
        bad(name, "music differs in play-order slot " + std::to_string(o));
        std::vector<Note> onlyE, onlyA;
        std::set_difference(ex.begin(), ex.end(), ac.begin(), ac.end(), std::back_inserter(onlyE));
        std::set_difference(ac.begin(), ac.end(), ex.begin(), ex.end(), std::back_inserter(onlyA));
        for (size_t k = 0; k < onlyE.size() && k < 3; k++) {
          auto& n = onlyE[k];
          printf("    expected only: step %d track %d pitch %d vel %d len24 %d delay %d\n", std::get<0>(n), std::get<1>(n), std::get<2>(n), std::get<3>(n), std::get<4>(n), std::get<5>(n));
        }
        for (size_t k = 0; k < onlyA.size() && k < 3; k++) {
          auto& n = onlyA[k];
          printf("    actual only:   step %d track %d pitch %d vel %d len24 %d delay %d\n", std::get<0>(n), std::get<1>(n), std::get<2>(n), std::get<3>(n), std::get<4>(n), std::get<5>(n));
        }
        break;
      }
    }

    // 4. the website text for it is stable (write, parse, write again)
    Print p1;
    trkr::save(p1, c);
    Stream s2;
    s2.in = p1.out;
    trkr::Song d;
    if (!trkr::load(s2, d, &err)) { bad(name, "website text for the result didn't parse"); continue; }
    Print p2;
    trkr::save(p2, d);
    if (p1.out != p2.out) bad(name, "website text isn't stable");

    int scenes = 0;
    for (auto& sc : b.scenes) (void)sc;
    scenes = (int)b.arrange.size();
    maxScenes = std::max(maxScenes, scenes);
    for (auto& t : b.tracks) {
      int used = 0;
      for (auto& p : t.patterns) if (!p.notes.empty()) used++;
      maxSlots = std::max(maxSlots, used);
    }
  }
  printf("songs converted: %d   refused (too big for the device): %d   failures: %d\n", tested, refused, failures);
  for (auto& r : reasons) printf("  refused: %s  x%d\n", r.first.c_str(), r.second);
  printf("patterns the busiest track needs (songs):");
  for (auto& kv : need) printf("  %d:%d", kv.first, kv.second);
  printf("\nlongest arrangement: %d scenes   most patterns used by one track: %d (limit %d)\n", maxScenes, maxSlots, (int)seq::PATTERNS);
  return failures ? 1 : 0;
}
