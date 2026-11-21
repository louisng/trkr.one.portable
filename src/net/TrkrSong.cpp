#include "TrkrSong.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace trkr {

static const char* const TYPE_NAMES[] = {"drum", "bass", "chords", "lead", "synth"};

const char* typeName(TrackType t) { return TYPE_NAMES[(uint8_t)t % 5]; }

static TrackType parseType(const char* s) {
  for (uint8_t i = 0; i < 5; i++)
    if (strcmp(s, TYPE_NAMES[i]) == 0) return (TrackType)i;
  return TrackType::Synth;
}

void Song::clear() {
  strcpy(name, "Untitled");
  bpm = 100;
  swing = 0;
  key = 0;
  scale = 0;
  trackCount = 0;
  for (auto& t : tracks) t = Track();
  patterns.clear();
  order.clear();
}

uint32_t Song::eventCount() const {
  uint32_t n = 0;
  for (const auto& p : patterns) n += p.events.size();
  return n;
}

uint32_t Song::totalSteps() const {
  uint32_t n = 0;
  for (uint8_t pi : order)
    if (pi < patterns.size()) n += patterns[pi].len;
  return n;
}

// ── parsing helpers ──────────────────────────────────────────────────────────
static const char* skipWord(const char* p) {
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  return p;
}

static void copyTrim(char* dst, size_t n, const char* src) {
  while (*src == ' ') src++;
  size_t i = 0;
  while (src[i] && src[i] != '\r' && src[i] != '\n' && i < n - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

static bool cmdIs(const char* line, const char* cmd) {
  size_t n = strlen(cmd);
  return strncmp(line, cmd, n) == 0 && (line[n] == ' ' || line[n] == 0);
}

bool Parser::line(const char* text) {
  if (ended_) return true;
  while (*text == ' ' || *text == '\t') text++;
  if (!*text || *text == '\r' || *text == '\n') return true;
  if (!header_) {
    if (strncmp(text, "TRKR 1", 6) != 0) {
      err_ = "not a TRKR 1 file";
      return false;
    }
    header_ = true;
    return true;
  }
  const char* args = skipWord(text);
  char* end;
  // Note: Arduino's constrain() is a macro that evaluates its argument more
  // than once, so every strtol() result goes into a variable first.
  long v1, v2;
  if (cmdIs(text, "name")) {
    copyTrim(s_.name, sizeof(s_.name), args);
  } else if (cmdIs(text, "bpm")) {
    v1 = strtol(args, nullptr, 10);
    s_.bpm = (uint16_t)constrain(v1, 20, 300);
  } else if (cmdIs(text, "swing")) {
    v1 = strtol(args, nullptr, 10);
    s_.swing = (uint8_t)constrain(v1, 0, 50);
  } else if (cmdIs(text, "key")) {
    v1 = strtol(args, &end, 10);
    v2 = strtol(end, nullptr, 10);
    s_.key = (uint8_t)constrain(v1, 0, 11);
    s_.scale = (uint8_t)constrain(v2, 0, 6);
  } else if (cmdIs(text, "track")) {
    if (s_.trackCount >= MAX_TRACKS) return true;  // extra tracks are dropped
    Track& t = s_.tracks[s_.trackCount++];
    v1 = strtol(args, &end, 10);
    t.ch = (uint8_t)constrain(v1, 1, 16);
    char word[16];
    const char* p = end;
    while (*p == ' ') p++;
    size_t i = 0;
    while (*p && *p != ' ' && i < sizeof(word) - 1) word[i++] = *p++;
    word[i] = 0;
    t.type = parseType(word);
    while (*p == ' ') p++;
    i = 0;
    while (*p && *p != ' ' && i < sizeof(t.voice) - 1) t.voice[i++] = *p++;
    t.voice[i] = 0;
    while (*p && *p != ' ') p++;  // rest of an over-long voice name
    t.mute = strtol(p, &end, 10) == 1;
    v1 = strtol(end, &end, 10);
    t.vol = (uint8_t)constrain(v1, 0, 100);
    copyTrim(t.name, sizeof(t.name), end);
  } else if (cmdIs(text, "strum")) {
    v1 = strtol(args, &end, 10);
    v2 = strtol(end, nullptr, 10);
    if (v1 >= 0 && v1 < s_.trackCount) s_.tracks[v1].strum = (uint8_t)constrain(v2, 0, 12);
  } else if (cmdIs(text, "ti")) {
    // `ti <track> <sound> <wave> <octave> <detune> <attack> <decay> <sustain> <release> <cutoff> <resonance> <envMod>`
    long v[12];
    const char* p = args;
    int n = 0;
    while (n < 12) {
      char* after;
      v[n] = strtol(p, &after, 10);
      if (after == p) break;
      p = after;
      n++;
    }
    if (n < 12 || v[0] < 0 || v[0] >= s_.trackCount) return true;
    Track& t = s_.tracks[v[0]];
    auto u8 = [&](long x, long hi) { return (uint8_t)constrain(x, 0L, hi); };
    t.inst.sound = v[1] ? 1 : 0;
    t.inst.wave = u8(v[2], 5);
    t.inst.octave = (int8_t)constrain(v[3], -12L, 12L);
    t.inst.detune = u8(v[4], 127);  t.inst.attack = u8(v[5], 127);  t.inst.decay = u8(v[6], 127);
    t.inst.sustain = u8(v[7], 127); t.inst.release = u8(v[8], 127); t.inst.cutoff = u8(v[9], 127);
    t.inst.resonance = u8(v[10], 127); t.inst.envMod = u8(v[11], 127);
    t.hasInst = true;
  } else if (cmdIs(text, "pattern")) {
    if (s_.patterns.size() >= MAX_PATTERNS) {
      err_ = "too many patterns";
      return false;
    }
    Pattern p;
    v1 = strtol(args, &end, 10);
    p.len = (uint16_t)constrain(v1, 1, (long)MAX_STEPS);
    copyTrim(p.name, sizeof(p.name), end);
    s_.patterns.push_back(std::move(p));
  } else if (cmdIs(text, "c")) {
    if (s_.patterns.empty()) return true;
    Pattern& p = s_.patterns.back();
    Event e{};
    long track = strtol(args, &end, 10);
    long step = strtol(end, &end, 10);
    long vel = strtol(end, &end, 10);
    long len = strtol(end, &end, 10);
    long delay = strtol(end, &end, 10);
    if (track < 0 || track >= s_.trackCount || step < 0 || step >= p.len) return true;
    e.track = (uint8_t)track;
    e.step = (uint8_t)step;
    e.vel = (uint8_t)constrain(vel, 1, 127);
    e.len24 = (uint16_t)constrain(len, 1, 24 * MAX_STEPS);
    e.delay = (int8_t)constrain(delay, -12, 12);
    while (e.count < MAX_NOTES) {
      char* after;
      long n = strtol(end, &after, 10);
      if (after == end) break;
      end = after;
      if (n >= 0 && n <= 127) e.notes[e.count++] = (uint8_t)n;
    }
    if (!e.count) return true;
    if (s_.eventCount() >= MAX_EVENTS) {
      err_ = "song too big for this device";
      return false;
    }
    p.events.push_back(e);
  } else if (cmdIs(text, "lk")) {
    // `lk <track> <pattern>`: this pattern's track is the same part as that earlier pattern's
    if (s_.patterns.empty()) return true;
    long track = strtol(args, &end, 10);
    long from = strtol(end, &end, 10);
    if (track >= 0 && track < s_.trackCount && from >= 0 && from < (long)s_.patterns.size() - 1)
      s_.patterns.back().links.push_back({(uint8_t)track, (uint16_t)from});
  } else if (cmdIs(text, "sx")) {
    // `sx <track> <step> <code>`: the step of the cell just above is strummed. Older readers skip it.
    if (s_.patterns.empty()) return true;
    long track = strtol(args, &end, 10);
    long step = strtol(end, &end, 10);
    long code = strtol(end, &end, 10);
    if (code < 1 || code > 6) return true;
    for (auto it = s_.patterns.back().events.rbegin(); it != s_.patterns.back().events.rend(); ++it)
      if (it->track == track && it->step == step) {
        if (it->count > 1) it->strum = (uint8_t)code;
        break;
      }
  } else if (cmdIs(text, "order")) {
    const char* p = args;
    while (*p && s_.order.size() < MAX_ORDER) {
      char* after;
      long v = strtol(p, &after, 10);
      if (after == p) break;
      p = after;
      if (v >= 0 && v < (long)s_.patterns.size()) s_.order.push_back((uint8_t)v);
    }
  } else if (cmdIs(text, "end")) {
    ended_ = true;
  }
  // unknown lines are ignored so the format can grow
  return true;
}

bool Parser::finish() {
  if (!header_) {
    err_ = "empty file";
    return false;
  }
  if (!s_.trackCount) {
    err_ = "song has no tracks";
    return false;
  }
  if (s_.patterns.empty()) {
    Pattern p;
    s_.patterns.push_back(p);
  }
  if (s_.order.empty()) s_.order.push_back(0);
  for (auto& p : s_.patterns)
    std::stable_sort(p.events.begin(), p.events.end(),
                     [](const Event& a, const Event& b) { return a.step < b.step; });
  return true;
}

bool load(Stream& in, Song& out, const char** error) {
  Parser parser(out);
  static char buf[1100];  // the order line is the longest: 256 entries
  size_t n = 0;
  bool ok = true;
  while (ok) {
    int c = in.read();
    if (c < 0 || c == '\n') {
      buf[n] = 0;
      ok = parser.line(buf);
      n = 0;
      if (c < 0) break;
      continue;
    }
    if (n < sizeof(buf) - 1) buf[n++] = (char)c;
  }
  if (ok) ok = parser.finish();
  if (!ok && error) *error = parser.error();
  return ok;
}

void save(Print& out, const Song& s) {
  out.print("TRKR 1\nname ");
  out.println(s.name);
  out.printf("bpm %u\nswing %u\nkey %u %u\n", s.bpm, s.swing, s.key, s.scale);
  for (uint8_t i = 0; i < s.trackCount; i++) {
    const Track& t = s.tracks[i];
    out.printf("track %u %s %s %u %u %s\n", t.ch, typeName(t.type), t.voice[0] ? t.voice : "-",
               t.mute ? 1 : 0, t.vol, t.name[0] ? t.name : "-");
  }
  for (uint8_t i = 0; i < s.trackCount; i++)
    if (s.tracks[i].strum) out.printf("strum %u %u\n", i, s.tracks[i].strum);
  for (uint8_t i = 0; i < s.trackCount; i++) {
    const Track& t = s.tracks[i];
    if (!t.hasInst) continue;
    out.printf("ti %u %u %u %d %u %u %u %u %u %u %u %u\n", i, t.inst.sound, t.inst.wave, (int)t.inst.octave, t.inst.detune,
               t.inst.attack, t.inst.decay, t.inst.sustain, t.inst.release, t.inst.cutoff, t.inst.resonance, t.inst.envMod);
  }
  for (const auto& p : s.patterns) {
    out.printf("pattern %u %s\n", p.len, p.name[0] ? p.name : "-");
    for (const auto& e : p.events) {
      out.printf("c %u %u %u %u %d", e.track, e.step, e.vel, e.len24, e.delay);
      for (uint8_t k = 0; k < e.count; k++) out.printf(" %u", e.notes[k]);
      out.print('\n');
      if (e.strum && e.count > 1) out.printf("sx %u %u %u\n", e.track, e.step, e.strum);
    }
    for (const auto& l : p.links) out.printf("lk %u %u\n", l.first, (unsigned)l.second);
  }
  out.print("order");
  for (uint8_t pi : s.order) out.printf(" %u", pi);
  out.print("\nend\n");
}

}  // namespace trkr
