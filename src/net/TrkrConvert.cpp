#include "TrkrConvert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

namespace trkr {

// ── helpers ──────────────────────────────────────────────────────────────────
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// The device's drum lanes (General MIDI notes): kick, snare, rim, clap, closed
// hat, open hat, low/mid/high tom, crash, ride, cowbell. Other GM drum notes
// are moved to the nearest of these.
int mapDrumNote(uint8_t gm) {
  switch (gm) {
    case 35: case 36: return 36;
    case 38: case 40: return 38;
    case 37: return 37;
    case 39: return 39;
    case 42: case 44: return 42;
    case 46: return 46;
    case 41: case 43: return 41;
    case 45: case 47: return 45;
    case 48: case 50: return 48;
    case 49: case 52: case 55: case 57: return 49;
    case 51: case 53: case 59: return 51;
    case 56: case 54: return 56;
    default: return -1;
  }
}

// Built-in synth presets: 0 Bass, 1 Pad, 2 Lead, 3 Pluck, 4 Keys, 5 Bell, 6 Organ, 7 Sub.
static uint8_t presetFor(const char* voice, TrackType type) {
  struct V { const char* id; uint8_t preset; };
  static const V TABLE[] = {
      {"fingerbass", 0}, {"synthbass", 0}, {"acidbass", 0}, {"sub808", 7}, {"ep", 4},     {"piano", 4},
      {"pad", 1},        {"organ", 6},     {"brass", 2},    {"pluck", 3},  {"arp", 3},    {"lead", 2},
      {"softlead", 2},   {"bell", 5},      {"whistle", 5},
  };
  for (const auto& v : TABLE)
    if (strcmp(voice, v.id) == 0) return v.preset;
  switch (type) {
    case TrackType::Bass: return 0;
    case TrackType::Chords: return 4;
    case TrackType::Lead: return 2;
    default: return 2;
  }
}

static bool sameNotes(const seq::Pattern& a, const seq::Pattern& b) {
  if (a.length != b.length || a.notes.size() != b.notes.size() || a.fx.size() != b.fx.size()) return false;
  for (const auto& x : a.fx) {  // the same strums on the same steps
    uint8_t c = 0, v = 0;
    if (!seq::fxAt(b, x.step, &c, &v) || c != x.cmd || v != x.val) return false;
  }
  for (size_t i = 0; i < a.notes.size(); i++) {
    const auto &x = a.notes[i], &y = b.notes[i];
    if (x.step != y.step || x.offset != y.offset || x.pitch != y.pitch || x.vel != y.vel || x.len != y.len)
      return false;
  }
  return true;
}

static bool lessNote(const seq::NoteEvent& a, const seq::NoteEvent& b) {
  return a.step != b.step ? a.step < b.step : a.pitch < b.pitch;
}

// ── website -> device ────────────────────────────────────────────────────────
bool Importer::begin(const Song& in, Meta& meta, const char** error) {
  auto fail = [&](const char* m) {
    if (error) *error = m;
    return false;
  };
  seq::songInitInto(out_);
  nT_ = in.trackCount > seq::TRACKS ? seq::TRACKS : in.trackCount;
  if (nT_ == 0) return fail("song has no tracks");
  scenes_ = 0;
  memset(slots_, 0, sizeof(slots_));
  arrange_.clear();

  out_.bpm = (uint16_t)clampi(in.bpm, seq::BPM_MIN, seq::BPM_MAX);
  meta = Meta();
  meta.swing = in.swing;
  meta.key = in.key;
  meta.scale = in.scale;
  meta.trackCount = nT_;
  strncpy(meta.title, in.name, sizeof(meta.title) - 1);

  for (uint8_t t = 0; t < nT_; t++) {
    const Track& s = in.tracks[t];
    seq::Track& d = out_.tracks[t];
    strncpy(d.name, s.name, sizeof(d.name) - 1);
    d.name[sizeof(d.name) - 1] = 0;
    const bool drum = s.type == TrackType::Drum;
    d.kind = drum ? seq::TrackKind::Drum : seq::TrackKind::Synth;
    d.channel = (uint8_t)clampi(s.ch, 1, 16);
    d.mute = s.mute;
    d.hold = false;
    d.pattern = 0;
    if (drum) seq::defaultInstrument(d.inst, t, true);
    else seq::applyPreset(d.inst, presetFor(s.voice, s.type));
    d.inst.volume = (uint8_t)clampi(s.vol * 127 / 100, 0, 127);
    memset(&d.drums, 0, sizeof(d.drums));
    seq::drumRoutingGmKit(d.drums);
    for (auto& p : d.patterns) {
      p.length = 16;
      p.notes.clear();
      p.fx.clear();
    }
    strncpy(meta.tracks[t].name, s.name, sizeof(meta.tracks[t].name) - 1);
    strncpy(meta.tracks[t].voice, s.voice, sizeof(meta.tracks[t].voice) - 1);
    meta.tracks[t].type = (uint8_t)s.type;
    meta.tracks[t].vol = s.vol;
    meta.tracks[t].strum = s.strum;
  }
  // Tracks the website song doesn't have stay silent.
  for (auto& sc : out_.scenes)
    for (uint8_t t = 0; t < seq::TRACKS; t++)
      if (t >= nT_) sc.pattern[t] = seq::SCENE_SILENT;
  return true;
}

bool Importer::addPattern(const Pattern& wp, const char** error) {
  auto fail = [&](const char* m) {
    if (error) *error = m;
    return false;
  };
  const uint16_t len = (uint16_t)clampi(wp.len, 1, seq::MAX_STEPS);
  uint8_t scene[seq::TRACKS];
  for (auto& v : scene) v = seq::SCENE_SILENT;
  bool any = false;
  for (uint8_t t = 0; t < nT_; t++) {
    seq::Pattern cand;
    cand.length = (uint8_t)len;
    for (const Event& e : wp.events) {
      if (e.track != t || e.step >= len) continue;
      for (uint8_t k = 0; k < e.count && k < MAX_NOTES; k++) {
        int pitch = e.notes[k];
        if (out_.tracks[t].kind == seq::TrackKind::Drum) {
          pitch = mapDrumNote((uint8_t)pitch);
          if (pitch < 0) continue;
        }
        seq::NoteEvent n{};
        n.step = e.step;
        n.offset = (int8_t)clampi(e.delay / 2, -(seq::TICKS_PER_STEP / 2), seq::TICKS_PER_STEP / 2 - 1);
        n.pitch = (uint8_t)pitch;
        n.vel = (uint8_t)clampi(e.vel, 1, 127);
        n.len = (uint16_t)std::max(1, e.len24 / 2);
        n.flags = 0;
        cand.notes.push_back(n);
      }
      if (seq::strumValid(e.strum) && e.count > 1) seq::setFx(cand, e.step, seq::FX_STRUM, e.strum);
    }
    if (cand.notes.empty()) continue;  // silent in this pattern
    std::sort(cand.notes.begin(), cand.notes.end(), lessNote);
    // two notes on the same step and pitch collapse into one
    cand.notes.erase(std::unique(cand.notes.begin(), cand.notes.end(),
                                 [](const seq::NoteEvent& a, const seq::NoteEvent& b) {
                                   return a.step == b.step && a.pitch == b.pitch;
                                 }),
                     cand.notes.end());
    any = true;
    int slot = -1;
    for (uint8_t i = 0; i < slots_[t]; i++)
      if (sameNotes(out_.tracks[t].patterns[i], cand)) {
        slot = i;
        break;
      }
    if (slot < 0) {
      if (slots_[t] >= seq::PATTERNS) return fail("a track needs more different patterns than the device holds");
      slot = slots_[t]++;
      cand.notes.shrink_to_fit();  // no spare capacity: a song holds thousands of notes
      out_.tracks[t].patterns[slot] = std::move(cand);
    }
    scene[t] = (uint8_t)slot;
  }
  if (!any) {
    // Nothing plays in this pattern (a rest): keep its length on track 1.
    int slot = -1;
    for (uint8_t i = 0; i < slots_[0]; i++)
      if (out_.tracks[0].patterns[i].notes.empty() && out_.tracks[0].patterns[i].length == len) slot = i;
    if (slot < 0 && slots_[0] < seq::PATTERNS) {
      slot = slots_[0]++;
      out_.tracks[0].patterns[slot].length = (uint8_t)len;
      out_.tracks[0].patterns[slot].notes.clear();
    }
    if (slot >= 0) scene[0] = (uint8_t)slot;
  }
  // The same combination of patterns as an earlier scene is the same scene.
  int sc = -1;
  for (int i = 0; i < scenes_; i++)
    if (memcmp(out_.scenes[i].pattern, scene, seq::TRACKS) == 0) {
      sc = i;
      break;
    }
  if (sc < 0) {
    if (scenes_ >= seq::SCENES) return fail("uses more than 99 different patterns");
    sc = scenes_++;
    memcpy(out_.scenes[sc].pattern, scene, seq::TRACKS);
  }
  if (arrange_.size() < seq::ARRANGE_MAX) arrange_.push_back((uint8_t)sc);
  return true;
}

void Importer::finish() {
  out_.arrange = arrange_;
  if (out_.arrange.empty()) out_.arrange.assign(1, 0);
  out_.songMode = true;
}

bool toDevice(const Song& in, seq::Song& out, Meta& meta, const char** error) {
  if (in.patterns.empty()) {
    if (error) *error = "song has no patterns";
    return false;
  }
  Importer imp(out);
  if (!imp.begin(in, meta, error)) return false;
  // The order: patterns in the order they are played (all of them if empty).
  std::vector<uint8_t> order;
  for (uint8_t p : in.order)
    if (p < in.patterns.size()) order.push_back(p);
  if (order.empty())
    for (size_t p = 0; p < in.patterns.size(); p++) order.push_back((uint8_t)p);
  for (uint8_t pi : order)
    if (!imp.addPattern(in.patterns[pi], error)) return false;
  imp.finish();
  return true;
}

// ── device -> website ────────────────────────────────────────────────────────
bool toWeb(const seq::Song& in, const Meta* meta, const char* name, Song& out, const char** error) {
  auto fail = [&](const char* m) {
    if (error) *error = m;
    return false;
  };
  out.clear();
  const char* title = meta && meta->title[0] ? meta->title : name && name[0] ? name : "Untitled";
  strncpy(out.name, title, sizeof(out.name) - 1);
  out.name[sizeof(out.name) - 1] = 0;
  out.bpm = in.bpm;
  out.swing = meta ? meta->swing : 0;
  out.key = meta ? meta->key : 0;
  out.scale = meta ? meta->scale : 0;

  // The scenes to export, in the order they first play.
  struct Spec {
    uint8_t pat[seq::TRACKS];
  };
  std::vector<Spec> specs;
  std::vector<uint8_t> order;
  if (in.songMode && !in.arrange.empty()) {
    int idx[seq::SCENES];
    for (int& v : idx) v = -1;
    for (uint8_t a : in.arrange) {
      if (a >= seq::SCENES) continue;
      if (idx[a] < 0) {
        idx[a] = (int)specs.size();
        Spec sp;
        memcpy(sp.pat, in.scenes[a].pattern, seq::TRACKS);
        specs.push_back(sp);
      }
      order.push_back((uint8_t)idx[a]);
    }
  }
  if (specs.empty()) {  // no song mode: the patterns each track is on now, looping
    Spec sp;
    for (uint8_t t = 0; t < seq::TRACKS; t++) sp.pat[t] = in.tracks[t].pattern;
    specs.push_back(sp);
    order.assign(1, 0);
  }
  if (specs.size() > MAX_PATTERNS) return fail("uses more than 64 different scenes");
  if (order.size() > MAX_ORDER) order.resize(MAX_ORDER);

  // Which tracks to export: those with music, and any the website song had.
  bool used[seq::TRACKS] = {false};
  for (uint8_t t = 0; t < seq::TRACKS; t++) {
    if (meta && t < meta->trackCount) used[t] = true;
    for (const Spec& sp : specs) {
      if (sp.pat[t] >= seq::PATTERNS) continue;
      for (const auto& n : in.tracks[t].patterns[sp.pat[t]].notes)
        if (n.pitch != seq::PITCH_OFF) used[t] = true;
    }
  }
  int last = 0;
  for (uint8_t t = 0; t < seq::TRACKS; t++)
    if (used[t]) last = t;
  out.trackCount = (uint8_t)(last + 1);

  for (uint8_t t = 0; t <= last; t++) {
    const seq::Track& s = in.tracks[t];
    Track& d = out.tracks[t];
    strncpy(d.name, s.name, sizeof(d.name) - 1);
    d.name[sizeof(d.name) - 1] = 0;
    d.ch = s.channel;
    d.mute = s.mute;
    if (meta && t < meta->trackCount) {
      // keep the full name unless it was renamed on the device
      if (strncmp(meta->tracks[t].name, s.name, 5) == 0 && meta->tracks[t].name[0]) {
        strncpy(d.name, meta->tracks[t].name, sizeof(d.name) - 1);
        d.name[sizeof(d.name) - 1] = 0;
      }
      strncpy(d.voice, meta->tracks[t].voice, sizeof(d.voice) - 1);
      d.type = (TrackType)meta->tracks[t].type;
      d.vol = meta->tracks[t].vol;
      d.strum = meta->tracks[t].strum;
      if (s.kind == seq::TrackKind::Drum && d.type != TrackType::Drum) d.type = TrackType::Drum;
    } else {
      d.type = s.kind == seq::TrackKind::Drum ? TrackType::Drum : TrackType::Synth;
      strcpy(d.voice, s.kind == seq::TrackKind::Drum ? "kit" : "ep");
      d.vol = (uint8_t)clampi(s.inst.volume * 100 / 127, 0, 100);
      d.strum = 0;
    }
  }

  uint32_t events = 0;
  for (size_t si = 0; si < specs.size(); si++) {
    const Spec& sp = specs[si];
    uint16_t len = 0;
    for (uint8_t t = 0; t <= last; t++)
      if (sp.pat[t] < seq::PATTERNS) len = std::max<uint16_t>(len, in.tracks[t].patterns[sp.pat[t]].length);
    if (len == 0) len = 16;
    Pattern wp;
    wp.len = len;
    snprintf(wp.name, sizeof(wp.name), "S%u", (unsigned)(si + 1));
    for (uint8_t t = 0; t <= last; t++) {
      if (sp.pat[t] >= seq::PATTERNS) continue;
      const seq::Pattern& p = in.tracks[t].patterns[sp.pat[t]];
      if (p.length == 0) continue;
      for (uint16_t base = 0; base < len; base += p.length) {  // looping tracks repeat to fill the pattern
        size_t i = 0;
        while (i < p.notes.size()) {
          const uint8_t step = p.notes[i].step;
          Event e{};
          e.track = t;
          bool first = true;
          for (; i < p.notes.size() && p.notes[i].step == step; i++) {
            const seq::NoteEvent& n = p.notes[i];
            if (n.pitch == seq::PITCH_OFF || e.count >= MAX_NOTES) continue;
            if (first) {
              e.vel = n.vel;
              e.len24 = (uint16_t)clampi(n.len * 2, 1, 24 * MAX_STEPS);
              e.delay = (int8_t)clampi(n.offset * 2, -12, 12);
              first = false;
            }
            e.notes[e.count++] = n.pitch;
          }
          if (e.count == 0 || base + step >= len) continue;
          e.step = (uint8_t)(base + step);
          uint8_t fc = seq::FX_NONE, fv = 0;
          if (e.count > 1 && seq::fxAt(p, step, &fc, &fv) && fc == seq::FX_STRUM && seq::strumValid(fv)) e.strum = fv;
          wp.events.push_back(e);
          if (++events > MAX_EVENTS) return fail("song is too big for the device to convert");
        }
      }
    }
    std::stable_sort(wp.events.begin(), wp.events.end(), [](const Event& a, const Event& b) {
      return a.step != b.step ? a.step < b.step : a.track < b.track;
    });
    out.patterns.push_back(std::move(wp));
  }
  out.order = order;
  return true;
}

// ── Meta as text ─────────────────────────────────────────────────────────────
//   M swing key scale trackCount
//   N title...
//   T voice type vol strum name...      (one per track)
void metaToText(const Meta& m, char* out, size_t n) {
  size_t k = (size_t)snprintf(out, n, "M %u %u %u %u\nN %s\n", m.swing, m.key, m.scale, m.trackCount, m.title);
  for (uint8_t t = 0; t < m.trackCount && k + 60 < n; t++)
    k += (size_t)snprintf(out + k, n - k, "T %s %u %u %u %s\n", m.tracks[t].voice[0] ? m.tracks[t].voice : "-",
                          m.tracks[t].type, m.tracks[t].vol, m.tracks[t].strum, m.tracks[t].name);
}

static void lineAfter(const char* p, char* out, size_t n) {
  size_t i = 0;
  while (*p && *p != '\n' && *p != '\r' && i + 1 < n) out[i++] = *p++;
  out[i] = 0;
}

bool metaFromText(const char* text, Meta& m) {
  m = Meta();
  unsigned sw, key, sc, cnt;
  if (sscanf(text, "M %u %u %u %u", &sw, &key, &sc, &cnt) != 4 || cnt > MAX_TRACKS) return false;
  m.swing = (uint8_t)sw;
  m.key = (uint8_t)key;
  m.scale = (uint8_t)sc;
  m.trackCount = (uint8_t)cnt;
  if (const char* q = strstr(text, "\nN ")) lineAfter(q + 3, m.title, sizeof(m.title));
  const char* p = text;
  for (uint8_t t = 0; t < cnt; t++) {
    p = strstr(p, "\nT ");
    if (!p) return false;
    p += 1;
    char voice[12];
    unsigned type, vol, strum;
    int used = 0;
    if (sscanf(p, "T %11s %u %u %u %n", voice, &type, &vol, &strum, &used) < 4) return false;
    strncpy(m.tracks[t].voice, strcmp(voice, "-") == 0 ? "" : voice, sizeof(m.tracks[t].voice) - 1);
    m.tracks[t].type = (uint8_t)(type > 4 ? 4 : type);
    m.tracks[t].vol = (uint8_t)(vol > 100 ? 100 : vol);
    m.tracks[t].strum = (uint8_t)(strum > 12 ? 12 : strum);
    if (used > 0) lineAfter(p + used, m.tracks[t].name, sizeof(m.tracks[t].name));
  }
  return true;
}

}  // namespace trkr
