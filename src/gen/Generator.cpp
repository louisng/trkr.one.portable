#include "Generator.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "GenTheory.h"
#include "Rng.h"

// NOTE for anyone editing this file: it mirrors public/lib/generator.js
// statement by statement, including the ORDER of every random-number call.
// C++ does not fix the order function arguments are evaluated in, so any
// call that draws random numbers is put on its own line.

namespace gen {

static const int STEPS = 16;
static const int PATTERN_BARS = 4;

namespace {

enum Track : int { T_KICK, T_SNARE, T_HATS, T_PERC, T_BASS, T_KEYS, T_LEAD, T_EXTRA, T_COUNT };
enum Voice : int { V_KICK, V_SNARE, V_HATS, V_PERC, V_INST };

// Compact on purpose: a big song has ~2,500 of these, and the device has little RAM.
struct Ev {
  int16_t step;
  uint8_t n;
  uint8_t vel;
  int8_t delay;
  uint8_t notes[4];
  double len;
};
struct Sec {
  std::string name;
  int bars;
  double energy;
  int bar;
};
// One chord of the song, kept small: a song has ~90 of them and they stay in
// memory for the whole generation. toChord() rebuilds the full Chord when needed.
struct CI {
  int16_t start = 0, len = 0, secBar = 0;
  uint8_t secIdx = 0;               // which section (index into `sections`)
  bool push = false;
  uint8_t root = 0, bass = 0, quality = 0, nTones = 0;
  int8_t tones[6] = {0, 0, 0, 0, 0, 0};
  char symbol[12] = "";
  uint8_t nVoicing = 0, voicing[4] = {0, 0, 0, 0};
  uint8_t nExtra = 0, extra[4] = {0, 0, 0, 0};
};
Chord toChord(const CI& c) {
  Chord ch;
  ch.root = c.root;
  ch.bass = c.bass;
  ch.quality = (Quality)c.quality;
  ch.tones.assign(c.tones, c.tones + c.nTones);
  ch.symbol = c.symbol;
  return ch;
}
void setChord(CI& c, const Chord& ch) {
  c.root = (uint8_t)ch.root;
  c.bass = (uint8_t)ch.bass;
  c.quality = (uint8_t)ch.quality;
  c.nTones = (uint8_t)std::min<size_t>(ch.tones.size(), 6);
  for (int i = 0; i < c.nTones; i++) c.tones[i] = (int8_t)ch.tones[i];
  snprintf(c.symbol, sizeof(c.symbol), "%s", ch.symbol.c_str());
}
std::vector<int> voicingVec(const CI& c) { return std::vector<int>(c.voicing, c.voicing + c.nVoicing); }
std::vector<int> extraVec(const CI& c) { return std::vector<int>(c.extra, c.extra + c.nExtra); }
struct Motif {
  std::vector<int> onsets, lens, intervals;
  int bars = 1;
  int startTone = 0;
};
// A finished cell of a pattern (after notes that landed on the same step are merged).
struct FC {
  float len = 0;
  uint16_t key = 0;          // step * 16 + track
  uint8_t n = 0;
  uint8_t vel = 0;
  int8_t delay = 0;
  uint8_t notes[4] = {0, 0, 0, 0};
  bool operator==(const FC& o) const {
    return key == o.key && n == o.n && memcmp(notes, o.notes, sizeof(notes)) == 0 && vel == o.vel &&
           delay == o.delay && len == o.len;
  }
};

int level(double e) { return e < 0.45 ? 0 : e < 0.8 ? 1 : 2; }
const char* const LEVEL_NAME[3] = {"low", "mid", "high"};
double dyn(double e) {
  static const double D[3] = {0.78, 0.9, 1.0};
  return D[level(e)];
}
int velOf(char d) { return (int)jsRound(14 + (d - '1') * (113.0 / 8)); }
int sign(int x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t p = 0;
  for (;;) {
    size_t q = s.find(sep, p);
    if (q == std::string::npos) { out.push_back(s.substr(p)); break; }
    out.push_back(s.substr(p, q - p));
    p = q + 1;
  }
  return out;
}
bool inList(const StrList& l, const std::string& s) {
  for (int i = 0; i < l.n; i++) if (s == l.v[i]) return true;
  return false;
}
const StrList* findBank(const Genre& g, const std::string& name) {
  for (int i = 0; i < g.nBanks; i++) if (name == g.banks[i].section) return &g.banks[i].progs;
  return nullptr;
}
bool resolveChord(const std::string& tok, int keyRoot, int keyScale, Chord& out) {
  if (romanToChord(tok, keyRoot, keyScale, out)) return true;
  return parseChord(tok, out);
}
uint16_t maskOf(const std::vector<int>& pcs) {
  uint16_t m = 0;
  for (int pc : pcs) m |= 1u << mod12(pc);
  return m;
}

}  // namespace

bool generateStream(const Genre& g, uint32_t seed, Sink& sink) {
  song::Song head;   // tempo, key and tracks; patterns go to the sink
  Meta meta;
  bool aborted = false;
  Rng root(seed);
  auto R = [&](const std::string& label) { return root.fork(label); };
  const std::string gid = g.id;

  // ── tempo & feel ───────────────────────────────────────────────────────────
  int bpmLo = g.bpmLo;
  int bpmHi = std::max<int>(bpmLo, g.bpmHi);
  int bpm;
  {
    Rng r = R("bpm");
    bpm = r.integer(bpmLo, bpmHi);
  }
  const double swing = g.swing;

  // ── key, scale & section progressions ──────────────────────────────────────
  std::vector<std::string> sectionNames;
  for (int i = 0; i < g.nArr; i++) {
    std::string n = g.arr[i].name;
    if (std::find(sectionNames.begin(), sectionNames.end(), n) == sectionNames.end()) sectionNames.push_back(n);
  }
  std::map<std::string, std::vector<std::string>> progFor;
  int keyRoot, keyScale;
  {
    Rng kr = R("key");
    keyRoot = kr.integer(0, 11);
    const StrList* verse = findBank(g, "verse");
    const StrList* chorus = findBank(g, "chorus");
    auto bankOf = [&](const std::string& n) -> const StrList* {
      if (const StrList* b = findBank(g, n)) return b;
      if (n == "intro" || n == "break" || n == "solo") return verse;
      if (n == "outro") return chorus ? chorus : verse;
      return chorus ? chorus : verse;
    };
    for (const auto& n : sectionNames) {
      const StrList* bank = bankOf(n);
      Rng r = R("prog:" + n);
      size_t idx = r.pickIndex(bank->n);
      progFor[n] = split(bank->v[idx], ' ');
    }
    if (!findBank(g, "intro")) progFor["intro"] = progFor["verse"];
    if (!findBank(g, "outro")) progFor["outro"] = progFor.count("chorus") ? progFor["chorus"] : progFor["verse"];
    if (!findBank(g, "solo") && progFor.count("solo")) progFor["solo"] = progFor["verse"];
    const std::string& v0 = progFor["verse"][0];
    bool tonicMinor = v0.rfind("i", 0) == 0 && v0.rfind("ii", 0) != 0 && v0.rfind("iii", 0) != 0 && v0.rfind("iv", 0) != 0;
    static const int MINOR[] = {1, 2, 3, 6}, MAJOR[] = {0, 4, 5};
    std::vector<Weighted> scales;
    for (int i = 0; i < g.nScales; i++) {
      int s = (int)g.scales[i].v;
      bool ok = false;
      if (tonicMinor) { for (int m : MINOR) ok |= (m == s); }
      else { for (int m : MAJOR) ok |= (m == s); }
      if (ok) scales.push_back(g.scales[i]);
    }
    if (scales.empty()) scales.assign(g.scales, g.scales + g.nScales);
    std::vector<double> w;
    for (auto& s : scales) w.push_back(s.w);
    keyScale = (int)scales[kr.weighted(w)].v;
  }

  // ── form ───────────────────────────────────────────────────────────────────
  std::vector<Sec> sections;
  int bar = 0;
  for (int i = 0; i < g.nArr; i++) {
    sections.push_back({g.arr[i].name, g.arr[i].bars, g.arr[i].energy, bar});
    bar += g.arr[i].bars;
  }
  const int totalBars = bar;
  const int totalSteps = totalBars * STEPS;
  auto sectionIdxAtBar = [&](int b) {
    int idx = 0;
    for (int i = 0; i < (int)sections.size(); i++) if (sections[i].bar <= b) idx = i;
    return idx;
  };

  // ── chords over time ───────────────────────────────────────────────────────
  std::vector<CI> chords;
  std::map<std::string, double> hrFor;
  for (const Sec& sec : sections) {
    const std::vector<std::string>* prog = progFor.count(sec.name) ? &progFor[sec.name] : &progFor["verse"];
    if (!hrFor.count(sec.name)) {
      Rng r = R("hr:" + sec.name);
      std::vector<double> w;
      for (int i = 0; i < g.nHr; i++) w.push_back(g.hr[i].w);
      hrFor[sec.name] = g.hr[r.weighted(w)].v;
    }
    struct P { Chord chord; bool hasBars; double bars; };
    std::vector<P> parsed;
    for (const auto& tok : *prog) {
      size_t colon = tok.find(':');
      std::string name = colon == std::string::npos ? tok : tok.substr(0, colon);
      P p{};
      p.hasBars = colon != std::string::npos;
      p.bars = p.hasBars ? atof(tok.c_str() + colon + 1) : 0;
      if (resolveChord(name, keyRoot, keyScale, p.chord)) parsed.push_back(p);
    }
    if (parsed.empty()) continue;
    bool fixed = false;
    for (auto& p : parsed) if (p.hasBars && p.bars != 0) fixed = true;
    int pos = sec.bar * STEPS;
    int end = (sec.bar + sec.bars) * STEPS;
    int i = 0;
    while (pos < end) {
      const P& p = parsed[i % parsed.size()];
      double bars = fixed ? (p.hasBars ? p.bars : 1) : hrFor[sec.name];
      int len = std::min(end - pos, (int)jsRound(bars * STEPS));
      CI c;
      c.start = (int16_t)pos;
      c.len = (int16_t)len;
      setChord(c, p.chord);
      c.secIdx = (uint8_t)(&sec - sections.data());
      c.secBar = (int16_t)sec.bar;
      chords.push_back(c);
      pos += len;
      i++;
    }
  }
  for (size_t i = 1; i < chords.size(); i++) {
    CI& c = chords[i];
    int local = c.start - c.secBar * STEPS;
    Rng r = R("push:" + sections[c.secIdx].name + ":" + std::to_string(local));
    c.push = c.start % STEPS == 0 && r.chance(g.push) && strcmp(c.symbol, chords[i - 1].symbol) != 0;
  }
  auto chordIdxAt = [&](int step) {
    int lo = 0, hi = (int)chords.size() - 1;
    while (lo < hi) {
      int mid = (lo + hi + 1) >> 1;
      if (chords[mid].start <= step) lo = mid; else hi = mid - 1;
    }
    return lo;
  };
  auto chordAtStep = [&](int step) -> Chord { return toChord(chords[chordIdxAt(std::max(0, step))]); };

  {
    std::vector<int> prevV, prevX;
    bool havePrevV = false, havePrevX = false;
    std::string vSec;
    bool haveVSec = false;
    for (CI& c : chords) {
      if (!haveVSec || sections[c.secIdx].name != vSec) {
        vSec = sections[c.secIdx].name;
        haveVSec = true;
        if (c.start % (STEPS * 4) == 0) { havePrevV = havePrevX = false; }
      }
      Chord full = toChord(c);
      prevV = voiceChord(full, havePrevV ? &prevV : nullptr, g.vLo, g.vHi, g.voiceStyle, g.vMax);
      c.nVoicing = (uint8_t)std::min<size_t>(prevV.size(), 4);
      for (int i = 0; i < c.nVoicing; i++) c.voicing[i] = (uint8_t)prevV[i];
      havePrevV = true;
      if (g.hasExtra) {
        prevX = voiceChord(full, havePrevX ? &prevX : nullptr, g.exLo, g.exHi, "close", 4);
        c.nExtra = (uint8_t)std::min<size_t>(prevX.size(), 4);
        for (int i = 0; i < c.nExtra; i++) c.extra[i] = (uint8_t)prevX[i];
        havePrevX = true;
      }
    }
  }

  // ── the song and its tracks ────────────────────────────────────────────────
  head.clear();
  snprintf(head.name, sizeof(head.name), "%s #%lu", g.name, (unsigned long)seed);
  head.bpm = (uint16_t)bpm;
  head.swing = 0;
  head.key = (uint8_t)keyRoot;
  head.scale = (uint8_t)keyScale;

  auto setTrack = [&](const char* name, song::TrackType type, const char* voice, int ch, int vol) {
    song::Track& t = head.tracks[head.trackCount++];
    snprintf(t.name, sizeof(t.name), "%s", name);
    snprintf(t.voice, sizeof(t.voice), "%s", voice);
    t.type = type;
    t.ch = (uint8_t)ch;
    t.vol = (uint8_t)vol;
    t.mute = false;
    t.strum = 0;
    return &t;
  };
  setTrack("Kick", song::TrackType::Drum, g.sDrums, 10, 80);
  setTrack("Snare", song::TrackType::Drum, g.sDrums, 10, 80);
  setTrack("Hats", song::TrackType::Drum, g.sDrums, 10, 80);
  setTrack("Perc", song::TrackType::Drum, g.sDrums, 10, 80);
  setTrack("Bass", song::TrackType::Bass, g.sBass, 3, 80);
  song::Track* keys = setTrack("Keys", song::TrackType::Chords, g.sChords, 2, 80);
  keys->strum = g.strum;
  setTrack("Lead", song::TrackType::Lead, g.sLead, 1, 80);
  if (g.hasExtra) setTrack(g.extraKind == 1 ? "Arp" : "Pad", song::TrackType::Synth, g.extraVoice, 4, 60);
  const int nTracks = head.trackCount;

  // ── output collection ──────────────────────────────────────────────────────
  std::vector<Ev> events[T_COUNT];
  Rng humanRng = R("human");
  auto ctx = [&](const std::string& label) { humanRng = R("h:" + label); };
  auto timing = [&](int voice, int step, double extraHuman = 1) -> int {
    double d = 0;
    if (voice == V_KICK) d = g.feelKick;
    else if (voice == V_SNARE) d = g.feelSnare;
    else if (voice == V_HATS) d = g.feelHats;
    bool swings = (voice != V_INST && (g.swingMask & (1 << voice))) || (voice == V_INST && swing >= 0.12);
    if (swing > 0 && step % 2 == 1 && swings) d += swing * 24;
    d += humanRng.gauss(g.humanize * extraHuman);
    if (step == 0 && d < 0) d = 0;
    return (int)std::max(-12.0, std::min(12.0, jsRound(d)));
  };
  auto humanVel = [&](double v, double jitterScale = 1) -> int {
    double n = humanRng.gauss(g.velJitter * 0.5 * jitterScale);
    return (int)std::max(1.0, std::min(127.0, jsRound(v + n)));
  };
  auto add = [&](int track, int step, const std::vector<int>& notes, int vel, double len, int delay = 0) {
    if (step < 0 || step >= totalSteps) return;
    Ev e{};
    e.step = (int16_t)step;
    e.n = (uint8_t)std::min<size_t>(notes.size(), 4);
    for (int k = 0; k < e.n; k++) e.notes[k] = (uint8_t)notes[k];
    e.vel = (uint8_t)vel;
    e.len = len;
    e.delay = (int8_t)delay;
    events[track].push_back(e);
  };
  auto addOne = [&](int track, int step, int note, int vel, double len, int delay = 0) {
    add(track, step, std::vector<int>{note}, vel, len, delay);
  };

  // ── drums ──────────────────────────────────────────────────────────────────
  auto pickSet = [&](const DrumSet& s, char which) -> const char* {
    if (which == 'B' && s.B) return s.B;
    return s.A;
  };
  bool snareCarriesBackbeat = false;
  for (const char* p = g.snare.A; *p; p++) if (*p >= '1' && *p <= '9') snareCarriesBackbeat = true;

  auto drumsFor = [&](size_t si, int b0, int b1) {
    const Sec& sec = sections[si];
    int lv = level(sec.energy);
    double eScale = dyn(sec.energy);
    const Sec* next = si + 1 < sections.size() ? &sections[si + 1] : nullptr;
    for (int b = b0; b < b1; b++) {
      int absBar = sec.bar + b;
      int base = absBar * STEPS;
      int inPhrase = b % 4;
      bool lastBar = b == sec.bars - 1;
      bool songEnd = absBar == totalBars - 1;
      Rng br = R("drums:" + sec.name + ":" + std::to_string(b % 8) + ":" + LEVEL_NAME[lv]);
      ctx("drums:" + sec.name + ":" + std::to_string(b % 4) + ":" + LEVEL_NAME[lv]);
      enum { F_NONE, F_ROLL, F_TOMS, F_FLAM, F_STOP } fillKind = F_NONE;
      if (g.fills && lastBar && next && (next->energy >= sec.energy - 0.05 || sec.bars >= 8)) {
        std::vector<double> w{3, std::string(g.sDrums) == "kit909" ? 0.0 : 2.0, 2, sec.energy > 0.7 ? 1.0 : 0.0};
        fillKind = (decltype(fillKind))(1 + br.weighted(w));
      }
      int fillFrom = fillKind != F_NONE ? (sec.energy > 0.8 && fillKind != F_FLAM ? 8 : 12) : 99;

      if (songEnd) {
        addOne(T_KICK, base, 36, 120, 4);
        addOne(T_PERC, base, 49, 115, 16);
        continue;
      }
      bool isBreak = sec.name == "break";
      // kick
      if (!isBreak && !(sec.name == "intro" && sec.energy < 0.4 && b < sec.bars / 2.0)) {
        const char* p = lv == 0 ? (g.kick.low ? g.kick.low : g.kick.A) : pickSet(g.kick, inPhrase == 3 ? 'B' : 'A');
        for (int i = 0; p[i]; i++) {
          char ch = p[i];
          if (i >= fillFrom && fillKind != F_ROLL) continue;
          if (ch == 'r') {
            if (br.chance(0.5)) {
              int v = humanVel(96 * eScale);
              int d = timing(V_KICK, i);
              addOne(T_KICK, base + i, 36, v, 1, d);
            }
            continue;
          }
          if (ch != '.') {
            int v = humanVel(velOf(ch) * eScale);
            int d = timing(V_KICK, i);
            addOne(T_KICK, base + i, 36, v, 1, d);
          }
        }
      }
      // snare (+ ghost notes)
      if (!isBreak && !(sec.name == "intro" && lv == 0 && b < 2)) {
        const char* p = pickSet(g.snare, (inPhrase == 3 || (lv == 2 && inPhrase == 1)) ? 'B' : 'A');
        for (int i = 0; p[i]; i++) {
          char ch = p[i];
          if (i >= fillFrom) continue;
          if (ch == 'r') {
            if (br.chance(0.5)) {
              int v = humanVel(90 * eScale);
              int d = timing(V_SNARE, i);
              addOne(T_SNARE, base + i, 38, v, 1, d);
            }
            continue;
          }
          if (ch == '.') {
            if (lv == 2 && g.humanize >= 1 && (i == 7 || i == 9 || i == 15) && br.chance(0.2)) {
              int v = humanVel(26);
              int d = timing(V_SNARE, i);
              addOne(T_SNARE, base + i, 38, v, 1, d);
            }
            continue;
          }
          int v = humanVel(velOf(ch) * ((ch - '0') <= 3 ? 1 : eScale));
          int d = timing(V_SNARE, i);
          addOne(T_SNARE, base + i, 38, v, 1, d);
        }
      }
      // fills
      if (fillKind == F_ROLL) {
        for (int i = fillFrom; i < 16; i++) {
          double ramp = 50 + ((double)(i - fillFrom) / (16 - fillFrom)) * 70;
          if (fillFrom == 8 && i < 12 && i % 2 == 1) continue;
          int v = humanVel(ramp);
          int d = timing(V_SNARE, i, 0.5);
          addOne(T_SNARE, base + i, 38, v, 1, d);
        }
      } else if (fillKind == F_TOMS) {
        static const int toms[8] = {48, 48, 45, 45, 41, 41, 41, 36};
        for (int i = fillFrom; i < 16; i++) {
          int idx = std::min(7, (int)floor((double)(i - fillFrom) * 8 / (16 - fillFrom)));
          int v = humanVel(100);
          int d = timing(V_SNARE, i, 0.5);
          addOne(T_PERC, base + i, toms[idx], v, 1, d);
        }
      } else if (fillKind == F_FLAM) {
        {
          int v = humanVel(95);
          int d = timing(V_SNARE, 14);
          addOne(T_SNARE, base + 14, 38, v, 1, d);
        }
        {
          int v = humanVel(115);
          int d = timing(V_SNARE, 15);
          addOne(T_SNARE, base + 15, 38, v, 1, d);
        }
      }
      // hats
      {
        const char* closed = lv == 0 ? (g.hats.low ? g.hats.low : g.hats.A) : g.hats.A;
        for (int i = 0; i < 16; i++) {
          if (fillKind == F_STOP && i >= 14) continue;
          char ch = closed[i];
          int note = g.hatsNote;
          if (g.hats.open && lv != 0 && g.hats.open[i] != '.' && (inPhrase % 2 == 1 || g.hatsNote == 46)) {
            ch = g.hats.open[i];
            note = 46;
          } else if (ch == '.' && lv == 2 && g.hats.sixteenths) {
            ch = g.hats.sixteenths[i];
            note = 42;
          }
          if (ch == 'r') {
            if (br.chance(0.5)) {
              int v = humanVel(60);
              int d = timing(V_HATS, i);
              addOne(T_HATS, base + i, note, v, 1, d);
            }
            continue;
          }
          if (ch == 0 || ch == '.') continue;
          int v = humanVel(velOf(ch) * eScale, 1.4);
          int d = timing(V_HATS, i);
          addOne(T_HATS, base + i, note, v, note == 46 ? 2 : 1, d);
        }
      }
      // percussion
      bool percOn = g.percAlways ? true : snareCarriesBackbeat ? lv == 2 : lv != 0;
      if (g.percA && percOn && !isBreak) {
        for (int i = 0; g.percA[i]; i++) {
          char ch = g.percA[i];
          if (ch == '.' || i >= fillFrom) continue;
          int v = humanVel(velOf(ch) * eScale);
          int d = timing(V_PERC, i);
          addOne(T_PERC, base + i, g.percNote, v, 1, d);
        }
      }
      // crash on a section's downbeat
      if (b == 0 && sec.bar > 0 && sec.energy >= 0.7) {
        int v = humanVel(105);
        addOne(T_PERC, base, 49, v, 8, 0);
      }
    }
  };

  // ── bass ───────────────────────────────────────────────────────────────────
  int bassPrevRoot = 36, bassPrevNote = 36;   // carried from bar to bar within a section
  auto bassFor = [&](size_t si, int b0, int b1) {
    int& prevRoot = bassPrevRoot;
    int& prevNote = bassPrevNote;
    {
      const Sec& sec = sections[si];
      int lv = level(sec.energy);
      const StrList& pats = g.bass[lv];
      std::string pat;
      {
        Rng r = R("bass:" + sec.name + ":" + LEVEL_NAME[lv]);
        pat = pats.v[r.pickIndex(pats.n)];
      }
      if (sec.name == "break" || (sec.name == "intro" && sec.energy < 0.4 && gid != "house")) {
        if (sec.name == "break") return;
        pat = "R---------------";
      }
      if (b0 == 0) {
        prevRoot = 36;
        prevNote = 36;
      }
      for (int b = b0; b < b1; b++) {
        int absBar = sec.bar + b;
        int base = absBar * STEPS;
        ctx("bass:" + sec.name + ":" + std::to_string(b % 4));
        if (absBar == totalBars - 1) {
          const Chord& c = chordAtStep(base);
          addOne(T_BASS, base, nearestPc(c.bass, prevRoot, 28, 45), 110, 16, 0);
          continue;
        }
        for (int i = 0; i < 16; i++) {
          char tok = pat[i];
          if (tok == '-' || tok == '.' || tok == 0) continue;
          int len = 1;
          while (i + len < 16 && pat[i + len] == '-') len++;
          int step = base + i;
          int ci = chordIdxAt(step);
          const Chord c = toChord(chords[ci]);
          int rootN = nearestPc(c.bass, prevRoot, 28, 45);
          int note;
          bool skip = false;
          switch (tok) {
            case 'R': note = rootN; break;
            case 'O': note = rootN + 12; break;
            case '5': {
              bool has6 = contains(c.tones, 6), has7 = contains(c.tones, 7), has8 = contains(c.tones, 8);
              note = rootN + (has6 && !has7 ? 6 : has8 ? 8 : 7);
              break;
            }
            case '3': {
              int iv = 4;
              for (int t : c.tones) if (t == 3 || t == 4 || t == 5) { iv = t; break; }
              note = rootN + iv;
              break;
            }
            case '7': {
              int iv = 12;
              for (int t : c.tones) if (t == 10 || t == 11 || t == 9) { iv = t; break; }
              note = rootN + iv;
              break;
            }
            case 'S': case 's': {
              std::vector<int> pcs = chordScale(c, keyRoot, keyScale);
              int x = prevNote + (tok == 'S' ? 1 : -1);
              while (!contains(pcs, mod12(x))) x += tok == 'S' ? 1 : -1;
              note = x;
              break;
            }
            case 'A': {
              int nci = chordIdxAt(base + 16);
              const Chord nextC = toChord(chords[nci]);
              bool changes = nci != ci;
              if (!changes) { note = rootN - 5 >= 26 ? rootN - 5 : rootN + 7; break; }
              int target = nearestPc(nextC.bass, rootN, 28, 45);
              Rng ar = R("appr:" + sec.name + ":" + std::to_string(b));
              std::vector<int> opts{target - 1, target + 1, target + (target > rootN ? -2 : 2)};
              note = opts[ar.weighted({3, 2, 1})];
              break;
            }
            default: skip = true; note = 0;
          }
          if (skip) continue;
          while (note < 26) note += 12;
          while (note > 55) note -= 12;
          if (tok == 'R') prevRoot = note;
          prevNote = note;
          int accent = i == 0 ? 112 : tok == 'O' ? 104 : tok == 'A' ? 92 : 98;
          double art = tok == 'O' ? std::min(len, 1) * 0.8 : len - (gid == "lofi" ? 0 : 0.15);
          int v = humanVel(accent * dyn(sec.energy));
          int d = timing(V_INST, i, 0.6);
          addOne(T_BASS, step, note, v, std::max(0.5, art), d);
        }
      }
    }
  };

  // ── keys (comping) ─────────────────────────────────────────────────────────
  auto keysFor = [&](size_t si, int b0, int b1) {
    {
      const Sec& sec = sections[si];
      int lv = level(sec.energy);
      std::string pat;
      {
        Rng r = R("comp:" + sec.name + ":" + LEVEL_NAME[lv]);
        const StrList& list = g.comp[lv];
        pat = list.v[r.pickIndex(list.n)];
      }
      for (int b = b0; b < b1; b++) {
        int absBar = sec.bar + b;
        int base = absBar * STEPS;
        ctx("keys:" + sec.name + ":" + std::to_string(b % 4));
        if (absBar == totalBars - 1) {
          add(T_KEYS, base, voicingVec(chords[chordIdxAt(base)]), 100, 16, 0);
          continue;
        }
        struct Hit { int i; int len; bool acc; };
        std::vector<Hit> hits;
        for (int i = 0; i < 16; i++) {
          char ch = pat[i];
          if (ch != 'X' && ch != 'x') continue;
          int len = 1;
          while (i + len < 16 && pat[i + len] == '-') len++;
          hits.push_back({i, len, ch == 'X'});
        }
        std::vector<int> changesInBar;
        for (const CI& c : chords) if (c.start > base && c.start < base + 16) changesInBar.push_back(c.start - base);
        for (int ch : changesInBar) {
          Hit* cover = nullptr;
          for (auto& h : hits) if (h.i < ch && h.i + h.len > ch) { cover = &h; break; }
          bool exists = false;
          for (auto& h : hits) if (h.i == ch) exists = true;
          if (cover && !exists) {
            int cLen = cover->i + cover->len - ch;
            int cI = cover->i;
            hits.push_back({ch, cLen, false});
            // push_back may move the vector, so look the cover up again
            for (auto& h : hits) if (h.i == cI) { h.len = ch - cI; break; }
          }
        }
        for (const Hit& h : hits) {
          int step = base + h.i;
          const CI& c = chords[chordIdxAt(step)];
          double vel = (h.acc ? 102 : 84) * dyn(sec.energy);
          double len = g.stab ? 1 : std::max(0.5, h.len - 0.2);
          int v = humanVel(vel);
          int d = timing(V_INST, h.i, 0.7);
          add(T_KEYS, step, voicingVec(c), v, len, d);
        }
      }
    }
  };

  // Pushed chord changes: the attack on the change moves a 16th earlier. A chord
  // that starts a section reaches back into the previous section's last bar,
  // which is why that last pattern is kept back until the next section is done.
  auto pushesFor = [&](size_t si, int b0, int b1) {
    const Sec& psec = sections[si];
    for (const CI& c : chords) {
    if (c.start < (psec.bar + b0) * STEPS || c.start >= (psec.bar + b1) * STEPS) continue;
    if (!c.push) continue;
    for (int track : {(int)T_KEYS, (int)T_BASS}) {
      auto& evs = events[track];
      bool found = false;
      for (auto& e : evs) if (e.step == c.start) { found = true; break; }
      if (!found) continue;
      evs.erase(std::remove_if(evs.begin(), evs.end(), [&](const Ev& e) { return e.step == c.start - 1; }), evs.end());
      for (auto& e : evs)
        if (e.step < c.start - 1 && e.step + e.len > c.start - 1) e.len = std::max(0.5, (double)(c.start - 1 - e.step));
      for (auto& e : evs) {
        if (e.step == c.start) {
          e.step = c.start - 1;
          e.len += 1;
          e.delay = (int8_t)std::min(0, (int)e.delay);
          break;
        }
      }
    }
    }
  };

  // ── extra layer (pads / arpeggios) ─────────────────────────────────────────
  Rng arpRng(1);
  auto extraFor = [&](size_t si, int b0, int b1) {
    if (!g.hasExtra) return;
    {
      const Sec& sec = sections[si];
      if (!inList(g.extraSections, sec.name)) return;
      if (g.extraKind == 0) {
        for (const CI& c : chords) {
          if (c.start < (sec.bar + b0) * STEPS || c.start >= (sec.bar + b1) * STEPS) continue;
          ctx("pad:" + sec.name + ":" + std::to_string(c.start - sec.bar * STEPS));
          int v = humanVel((64 + 20 * dyn(sec.energy)) * g.exGain);
          add(T_EXTRA, c.start, extraVec(c), v, std::max(1.0, c.len - 0.25), 0);
        }
      } else {
        // the arpeggio's humanizing is one stream per section, kept between chunks
        Rng savedHuman = humanRng;
        if (b0 == 0) arpRng = R("h:arp:" + sec.name);
        humanRng = arpRng;
        int rate = g.exEvery >= 0 ? g.exEvery : (sec.energy >= 0.8 ? 1 : 2);
        int shape;
        {
          Rng r = R("arpshape:" + sec.name);
          shape = (int)r.pickIndex(3);  // 0 up, 1 updown, 2 skip
        }
        for (int s = (sec.bar + b0) * STEPS; s < (sec.bar + b1) * STEPS; s += rate) {
          const CI& c = chords[chordIdxAt(s)];
          std::vector<int> v = extraVec(c);
          v.push_back(v[0] + 12);
          int k = (s - c.start) / rate;
          int n = (int)v.size();
          int idx;
          if (shape == 0) idx = k % n;
          else if (shape == 2) { static const int SK[8] = {0, 2, 1, 3, 2, 4, 3, 1}; idx = SK[k % 8] % n; }
          else { int cyc = 2 * n - 2; int m = k % cyc; idx = m < n ? m : cyc - m; }
          int hv = humanVel(((s % 4) == 0 ? 92 : 70) * dyn(sec.energy) * g.exGain);
          addOne(T_EXTRA, s, v[idx], hv, rate * 0.7, 0);
        }
        arpRng = humanRng;
        humanRng = savedHuman;
      }
    }
  };

  // ── melody ─────────────────────────────────────────────────────────────────
    const double mStrong = g.mStrong;
    const int lo0 = g.mLo, hi0 = g.mHi;
    std::map<std::string, std::pair<Motif, Motif>> motifs;
    bool havePrevPitch = false;
    int prevPitch = 0;

    auto leadAllowed = [&](const Sec& sec) {
      if (g.mSections.n) return inList(g.mSections, sec.name);
      return sec.name != "intro" && sec.name != "break";
    };

    auto makeMotif = [&](Rng& r, int bars, double density, double sync, double legato) {
      int total = bars * STEPS;
      int want = (int)std::max(2.0, jsRound(density * bars * r.flt(0.8, 1.2)));
      std::vector<double> w;
      for (int s = 0; s < total; s++) {
        int p = s % 16;
        double wt = p == 0 ? 6 : p == 8 ? 4 : p % 4 == 0 ? 3 : p % 2 == 0 ? 1.5 + sync * 1.5 : 0.3 + sync * 1.6;
        if (s >= total - 3) wt *= 0.25;
        w.push_back(wt);
      }
      std::set<int> chosen;
      if (r.chance(sync * 0.6)) {
        static const std::vector<int> firsts{2, 3, 6};
        chosen.insert(r.pick(firsts));
      } else {
        chosen.insert(0);
      }
      int guard = 0;
      while ((int)chosen.size() < want) {
        if (!(guard++ < 400)) break;
        chosen.insert((int)r.weighted(w));
      }
      Motif m;
      m.onsets.assign(chosen.begin(), chosen.end());
      m.bars = bars;
      for (size_t i = 0; i < m.onsets.size(); i++) {
        int gap = (i + 1 < m.onsets.size() ? m.onsets[i + 1] : total) - m.onsets[i];
        int l;
        if (r.chance(legato)) {
          l = gap;
        } else {
          static const std::vector<int> ls{1, 2, 2, 3};
          l = std::min(gap, r.pick(ls));
        }
        m.lens.push_back(std::max(1, std::min(8, l)));
      }
      m.intervals.push_back(0);
      int last = 0;
      for (size_t i = 1; i < m.onsets.size(); i++) {
        int iv;
        if (abs(last) >= 3) {
          static const std::vector<int> fills{1, 1, 2};
          iv = -sign(last) * r.pick(fills);
        } else if (r.chance(g.mLeap)) {
          static const std::vector<int> leaps{-4, -3, 3, 4, 5, -5};
          iv = r.pick(leaps);
        } else {
          static const int steps[5] = {0, 1, -1, 2, -2};
          iv = steps[r.weighted({1.2, 4, 4, 2, 2})];
        }
        m.intervals.push_back(iv);
        last = iv;
      }
      static const std::vector<int> tones{0, 1, 2, 1};
      m.startTone = r.pick(tones);
      return m;
    };

    auto mutate = [&](const Motif& m, Rng& r) {
      Motif c = m;
      bool coin = r.chance(0.5);
      if (coin && c.intervals.size() > 2) {
        int i = r.integer(1, (int)c.intervals.size() - 1);
        static const std::vector<int> pm{-1, 1};
        c.intervals[i] += r.pick(pm);
      } else if (c.onsets.size() > 2) {
        int i = r.integer(1, (int)c.onsets.size() - 1);
        static const std::vector<int> pm{-1, 1};
        int ns = c.onsets[i] + r.pick(pm);
        int nextOnset = i + 1 < (int)c.onsets.size() ? c.onsets[i + 1] : m.bars * STEPS;
        if (ns > c.onsets[i - 1] && ns < nextOnset) c.onsets[i] = ns;
      }
      return c;
    };

    auto walk = [&](int midi, int steps, const std::vector<int>& pcs) {
      int x = midi;
      int dir = sign(steps);
      for (int k = 0; k < abs(steps); k++) {
        x += dir;
        int guard = 0;
        while (!contains(pcs, mod12(x)) && guard++ < 12) x += dir;
      }
      return x;
    };
    auto nearestChordTone = [&](int midi, const Chord& chord, int dirPref = 0) {
      std::vector<int> pcs;
      for (int pc : chordPcs(chord))
        if (pc != mod12(chord.root + 14) || chord.tones.size() < 5) pcs.push_back(pc);
      int best = midi;
      double bd = INFINITY;
      for (int d = -6; d <= 6; d++) {
        if (!contains(pcs, mod12(midi + d))) continue;
        double cost = abs(d) + (dirPref && sign(d) != dirPref && d != 0 ? 0.5 : 0);
        if (cost < bd) { bd = cost; best = midi + d; }
      }
      return best;
    };

    struct Note { int step; int len; int pitch; int o; };
    auto realize = [&](const Motif& motif, int startStep, int lo, int hi, double strong, int ending /*0 none,1 open,2 close*/,
                       int velBase, const std::string& tag) {
      ctx("lead:" + tag);
      std::vector<Note> notes;
      bool havePitch = false;
      int pitch = 0;
      for (size_t i = 0; i < motif.onsets.size(); i++) {
        int o = motif.onsets[i];
        int step = startStep + o;
        if (step >= totalSteps) continue;
        const Chord& chord = chordAtStep(step);
        std::vector<int> pcs = chordScale(chord, keyRoot, keyScale);
        if (!havePitch) {
          int toneIv = motif.startTone < (int)chord.tones.size() ? chord.tones[motif.startTone] : 0;
          int tonePc = mod12(chord.root + toneIv);
          int near = havePrevPitch ? prevPitch : (int)jsRound((lo + hi) / 2.0);
          pitch = nearestPc(tonePc, near, lo, hi);
          havePitch = true;
        } else {
          pitch = walk(pitch, motif.intervals[i], pcs);
        }
        int len = motif.lens[i];
        bool isStrong = o % 8 == 0 || (o % 4 == 0 && len >= 2) || len >= 4;
        bool took = false;
        if (isStrong) {
          Rng sr = R("strong:" + tag + ":" + std::to_string(i));
          took = sr.chance(strong);
        }
        if (isStrong && took) {
          pitch = nearestChordTone(pitch, chord, sign(motif.intervals[i]));
        } else if (len >= 2 && contains(avoidPcs(chord, pcs), mod12(pitch))) {
          pitch = nearestChordTone(pitch, chord);
        }
        while (pitch > hi) pitch -= 12;
        while (pitch < lo) pitch += 12;
        notes.push_back({step, len, pitch, o});
      }
      if (ending && !notes.empty()) {
        Note& last = notes.back();
        const Chord& chord = chordAtStep(last.step);
        if (ending == 2) {
          int second = chord.tones.size() > 1 ? chord.tones[1] : 4;
          int pref0 = chord.root, pref1 = mod12(chord.root + second);
          int targetPc = (pref0 == keyRoot || pref1 == keyRoot) ? keyRoot : pref0;
          last.pitch = nearestPc(targetPc, last.pitch, lo, hi);
        } else {
          Rng orr = R("open:" + tag);
          size_t which = orr.pickIndex(2);  // picks index 1 or 2
          int idx = (int)which == 0 ? 1 : 2;
          int iv = idx < (int)chord.tones.size() ? chord.tones[idx] : 7;
          int pc = mod12(chord.root + iv);
          last.pitch = nearestPc(pc, last.pitch, lo, hi);
        }
        last.len = std::max(last.len, std::min(6, motif.bars * STEPS - last.o - 1));
      }
      for (const Note& n : notes) {
        int p = n.o % 16;
        int accent = p == 0 ? 10 : p % 4 == 0 ? 4 : p % 2 ? -4 : 0;
        double height = ((double)(n.pitch - lo) / std::max(1, hi - lo)) * 10;
        int v = humanVel(velBase + accent + height);
        int d = timing(V_INST, p, 0.7);
        addOne(T_LEAD, n.step, n.pitch, v, std::max(0.5, n.len - 0.1), d);
        prevPitch = n.pitch;
        havePrevPitch = true;
      }
    };

  auto melodyFor = [&](size_t si, int b0, int b1) {
    {
      const Sec& sec = sections[si];
      if (!leadAllowed(sec)) return;
      bool isSolo = sec.name == "solo";
      bool isChorus = sec.name == "chorus";
      double density = g.mDensity * (isChorus ? 1.15 : sec.name == "verse" ? 0.85 : isSolo ? 1.8 : 1);
      int shift = isChorus ? 3 : sec.name == "bridge" ? 1 : isSolo ? 2 : 0;
      int lo = lo0 + shift, hi = hi0 + shift + (isSolo ? 4 : 0);
      int mb = g.mBars;
      std::string key = sec.name == "outro" ? "chorus" : sec.name;
      if (!motifs.count(key)) {
        Rng r = R("motif:" + key);
        Motif A = makeMotif(r, mb, density, g.mSync, g.mLegato);
        Motif B = makeMotif(r, mb, density * 0.9, g.mSync, g.mLegato);
        motifs[key] = {A, B};
      }
      const Motif A = motifs[key].first;
      const Motif B = motifs[key].second;
      int phrases = std::max(1, sec.bars / 4);
      if (b0 == 0) havePrevPitch = false;
      int lastBars = sec.name == "outro" ? 4 : sec.bars;
      for (int p = b0 / 4; p < std::min(phrases, (b1 + 3) / 4); p++) {
        if (p * 4 >= lastBars) break;
        int units = 4 / mb;
        for (int u = 0; u < units; u++) {
          int start = (sec.bar + p * 4 + u * mb) * STEPS;
          if (sec.name == "outro" && u >= units / 2.0) break;
          Rng r = R("unit:" + sec.name + ":" + std::to_string(p % 2) + ":" + std::to_string(u));
          Motif m = mb == 1 ? (u == 2 ? B : A) : A;
          if (p % 2 == 1 && u == units - 1 && mb == 2) m = B;
          if (isSolo) {
            Rng sr = R("solo:" + std::to_string(sec.bar) + ":" + std::to_string(p) + ":" + std::to_string(u));
            m = makeMotif(sr, mb, density, std::min(1.0, g.mSync + 0.2), 0.3);
          } else if (!r.chance(g.mRepeat)) {
            m = mutate(m, r);
          }
          int ending = u == units - 1 ? (p % 2 == 0 ? 1 : 2) : 0;
          realize(m, start, lo, hi, isSolo ? 0.55 : mStrong, ending, isChorus ? 98 : 88,
                  sec.name + ":" + std::to_string(p) + ":" + std::to_string(u));
        }
      }
    }
  };

  // ── build the song one section at a time ───────────────────────────────────
  // Notes are made a section at a time and each finished 4-bar pattern is
  // merged, compared with the ones before it and stored right away, so the
  // device never holds more than about two sections of loose notes.
  const int patCount = (totalBars + PATTERN_BARS - 1) / PATTERN_BARS;
  const int patLen = PATTERN_BARS * STEPS;
  int nextPat = 0;
  auto flushTo = [&](int upTo) {
    upTo = std::min(upTo, patCount);
    for (; nextPat < upTo && !aborted; nextPat++) {
      const int lo = nextPat * patLen, hi = lo + patLen;
      std::vector<FC> cells;
      for (int ti = 0; ti < nTracks; ti++) {
        std::vector<Ev>& evs = events[ti];
        // take this pattern's notes out of the waiting list, keeping their order
        std::vector<Ev> sel;
        size_t w = 0;
        for (size_t r = 0; r < evs.size(); r++) {
          if (evs[r].step >= lo && evs[r].step < hi) sel.push_back(evs[r]);
          else evs[w++] = evs[r];
        }
        evs.resize(w);
        // Notes that land on the same step of the same track are merged, in the
        // order they were made (a stable sort keeps that order).
        std::stable_sort(sel.begin(), sel.end(), [](const Ev& x, const Ev& y) { return x.step < y.step; });
        for (size_t k = 0; k < sel.size();) {
          const Ev& first = sel[k];
          FC cell;
          cell.key = (uint16_t)((first.step - lo) * 16 + ti);
          cell.n = first.n;
          memcpy(cell.notes, first.notes, 4);
          cell.vel = (uint8_t)std::max(1, std::min(127, (int)first.vel));
          cell.len = (float)(jsRound(first.len * 100) / 100);
          cell.delay = first.delay;
          size_t j = k + 1;
          for (; j < sel.size() && sel[j].step == first.step; j++) {
            const Ev& e = sel[j];
            std::vector<int> notes(cell.notes, cell.notes + cell.n);
            for (int q = 0; q < e.n; q++) if (!contains(notes, e.notes[q])) notes.push_back(e.notes[q]);
            if (notes.size() > 4) notes.resize(4);
            cell.n = (uint8_t)notes.size();
            for (int q = 0; q < cell.n; q++) cell.notes[q] = (uint8_t)notes[q];
            cell.vel = (uint8_t)std::max((int)cell.vel, (int)e.vel);
            cell.len = (float)std::max((double)cell.len, e.len);
            // the delay of the first note stays
          }
          cells.push_back(cell);
          k = j;
        }
      }
      std::sort(cells.begin(), cells.end(), [](const FC& x, const FC& y) { return x.key < y.key; });
      std::vector<song::Event> evOut;
      evOut.reserve(cells.size());
      for (const FC& c : cells) {
        song::Event e{};
        e.step = (uint8_t)(c.key / 16);
        e.track = (uint8_t)(c.key % 16);
        e.vel = (uint8_t)std::max(1, std::min(127, (int)c.vel));
        e.count = c.n;
        for (int k = 0; k < c.n; k++) e.notes[k] = c.notes[k];
        double len = c.len == 0 ? 1 : std::max(0.25, std::min(128.0, (double)c.len));
        e.len24 = (uint16_t)std::max(1.0, jsRound(len * 24));
        e.delay = (int8_t)std::max(-12.0, std::min(12.0, jsRound(c.delay)));
        evOut.push_back(e);
      }
      song::Pattern pat;
      pat.len = patLen;
      const Sec& psec = sections[sectionIdxAtBar(nextPat * PATTERN_BARS)];
      snprintf(pat.name, sizeof(pat.name), "%s", psec.name.c_str());
      pat.events = std::move(evOut);
      if (!sink.pattern(std::move(pat))) { aborted = true; return; }
    }
  };

  // ── what the UI and the play-along keyboard need ───────────────────────────
  meta.key = (uint8_t)keyRoot;
  meta.scale = (uint8_t)keyScale;
  meta.seed = seed;
  for (const CI& c : chords) {
    ChordSpan cs{};
    cs.start = (int16_t)(c.start - (c.push ? 1 : 0));
    Chord full = toChord(c);
    snprintf(cs.sym, sizeof(cs.sym), "%s", c.symbol);
    cs.pcs = maskOf(chordPcs(full));
    cs.scale = maskOf(chordScale(full, keyRoot, keyScale));
    meta.chords.push_back(cs);
  }
  for (const Sec& sec : sections) {
    SectionSpan ss{};
    snprintf(ss.name, sizeof(ss.name), "%s", sec.name.c_str());
    ss.bar = (uint16_t)sec.bar;
    ss.bars = (uint16_t)sec.bars;
    ss.energy = (float)sec.energy;
    meta.sections.push_back(ss);
  }
  if (!sink.begin(head, meta)) return false;

  for (size_t si = 0; si < sections.size(); si++) {
    for (int b0 = 0; b0 < sections[si].bars; b0 += PATTERN_BARS) {
      int b1 = std::min(b0 + PATTERN_BARS, sections[si].bars);
      drumsFor(si, b0, b1);
      bassFor(si, b0, b1);
      keysFor(si, b0, b1);
      pushesFor(si, b0, b1);
      extraFor(si, b0, b1);
      melodyFor(si, b0, b1);
      // Everything before the last pattern made so far is final: a pushed
      // chord only ever reaches back into the bar before it.
      flushTo((sections[si].bar + b1) / PATTERN_BARS - 1);
      if (aborted) return false;
      for (auto& evs : events) evs.shrink_to_fit();   // hand back the spare room
    }
  }
  flushTo(patCount);
  if (aborted) return false;
  return true;
}

namespace {
// Collects a whole song, storing identical patterns once.
struct Collect : Sink {
  song::Song& out;
  Meta& meta;
  Collect(song::Song& o, Meta& m) : out(o), meta(m) {}
  static bool same(const std::vector<song::Event>& x, const std::vector<song::Event>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++) {
      const song::Event &a = x[i], &b = y[i];
      if (a.step != b.step || a.track != b.track || a.vel != b.vel || a.count != b.count || a.len24 != b.len24 ||
          a.delay != b.delay || memcmp(a.notes, b.notes, sizeof(a.notes)) != 0)
        return false;
    }
    return true;
  }
  bool begin(const song::Song& head, const Meta& m) override {
    out = head;
    meta = m;
    return true;
  }
  bool pattern(song::Pattern&& p) override {
    int found = -1;
    for (size_t u = 0; u < out.patterns.size(); u++)
      if (same(out.patterns[u].events, p.events)) { found = (int)u; break; }
    if (found < 0) {
      out.patterns.push_back(std::move(p));
      found = (int)out.patterns.size() - 1;
    }
    out.order.push_back((uint8_t)found);
    return true;
  }
};
}  // namespace

bool generate(const Genre& g, uint32_t seed, song::Song& out, Meta& meta) {
  Collect c(out, meta);
  if (!generateStream(g, seed, c)) return false;
  return out.eventCount() <= song::MAX_EVENTS;
}

const ChordSpan* chordAt(const Meta& m, uint32_t step) {
  if (m.chords.empty()) return nullptr;
  const ChordSpan* cur = &m.chords[0];
  for (const auto& c : m.chords) {
    if (c.start <= (int32_t)step) cur = &c;
    else break;
  }
  return cur;
}

const SectionSpan* sectionAt(const Meta& m, uint32_t bar) {
  const SectionSpan* cur = nullptr;
  for (const auto& s : m.sections) if (s.bar <= bar) cur = &s;
  return cur;
}

}  // namespace gen
