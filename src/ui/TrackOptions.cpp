#include "TrackOptions.h"

#include <stdio.h>

#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

using namespace seq;

static constexpr uint8_t IDX_OPT = 3 * 14 + 1;

uint16_t nextNoteLength(uint16_t len, bool longer, uint16_t maxLen) {
  const uint16_t T = TICKS_PER_STEP;
  uint16_t r;
  if (longer) r = len < T / 4 ? T / 4 : len < T / 2 ? T / 2 : len < T ? T : (len / T + 1) * T;
  else r = len > T ? ((len - 1) / T) * T : len > T / 2 ? T / 2 : T / 4;
  return r > maxLen ? maxLen : r;
}

void formatNoteLength(uint16_t len, char* out, size_t n) {
  const uint16_t T = TICKS_PER_STEP;
  if (len == 0) len = 1;
  if (len < T && T % len == 0) snprintf(out, n, "1/%d step", T / len);
  else if (len % T == 0) snprintf(out, n, "%d step%s", len / T, len == T ? "" : "s");
  else snprintf(out, n, "%.2g steps", (float)len / T);
}

void formatNoteLengthShort(uint16_t len, char* out, size_t n) {
  const uint16_t T = TICKS_PER_STEP;
  if (len % T == 0) snprintf(out, n, "%d", len / T);
  else if (len == T / 4) snprintf(out, n, ".25");
  else if (len == T / 2) snprintf(out, n, ".5");
  else if (len < T) snprintf(out, n, ".%02d", len * 100 / T);
  else snprintf(out, n, "%.1f", (float)len / T);
}

// ── Effects ──────────────────────────────────────────────────────────────────
char fxLetter(uint8_t cmd) {
  static const char L[FX_COUNT] = {'-', 'P', 'R', 'T', 'H', 'M', 'B', 'G', 'S'};
  return cmd < FX_COUNT ? L[cmd] : '?';
}

uint8_t fxDefault(uint8_t cmd) {
  switch (cmd) {
    case FX_CHANCE: return 50;
    case FX_RATCHET: return 2;
    case FX_COND: return 0x12;  // 1st of every 2 loops
    case FX_HUMANIZE: return 16;
    case FX_MOD: return 64;
    case FX_BEND: return 0x40;
    case FX_STRUM: return 2;  // down, medium
    default: return 0;
  }
}

// Trig conditions x:y (play on the x-th of every y loops), in order.
static uint8_t condIndex(uint8_t val) {
  uint8_t i = 0;
  for (uint8_t y = 2; y <= 8; y++)
    for (uint8_t x = 1; x <= y; x++, i++)
      if (val == ((x << 4) | y)) return i;
  return 0;
}
static uint8_t condAt(int idx) {
  const int count = 35;  // sum of 2..8
  idx = ((idx % count) + count) % count;
  int i = 0;
  for (uint8_t y = 2; y <= 8; y++)
    for (uint8_t x = 1; x <= y; x++, i++)
      if (i == idx) return (uint8_t)((x << 4) | y);
  return 0x12;
}

uint8_t fxStepValue(uint8_t cmd, uint8_t val, int dir, bool big) {
  if (cmd == FX_COND) return condAt(condIndex(val) + dir * (big ? 5 : 1));
  int lo = 0, hi = 127;
  if (cmd == FX_CHANCE) hi = 100;
  if (cmd == FX_RATCHET) { lo = 2; hi = 6; big = false; }
  if (cmd == FX_STRUM) { lo = 1; hi = 6; big = false; }
  int v = (int)val + dir * (big ? 16 : 1);
  return (uint8_t)(v < lo ? lo : v > hi ? hi : v);
}

void fxDescribe(uint8_t cmd, uint8_t val, char* out, size_t n) {
  switch (cmd) {
    case FX_CHANCE: snprintf(out, n, "P: plays %d%% of the time", val); break;
    case FX_RATCHET: snprintf(out, n, "R: %d hits in the step", val); break;
    case FX_COND: snprintf(out, n, "T: plays loop %d of every %d", val >> 4, val & 0x0F); break;
    case FX_HUMANIZE: snprintf(out, n, "H: velocity +/- %d", val); break;
    case FX_MOD: snprintf(out, n, "M: mod wheel = %d", val); break;
    case FX_BEND: snprintf(out, n, "B: pitch bend %+d", (int)val - 64); break;
    case FX_PROGRAM: snprintf(out, n, "G: program change %d", val); break;
    case FX_STRUM: {
      static const char* const SPEED[3] = {"fast", "medium", "slow"};
      if (strumValid(val)) snprintf(out, n, "S: strum %s %s", strumUp(val) ? "up" : "down", SPEED[(val - 1) % 3]);
      else snprintf(out, n, "S: strum (1-6)");
      break;
    }
    default: snprintf(out, n, "No effect"); break;
  }
}

namespace options {

enum Opt : uint8_t {
  OPT_PATTERN,
  OPT_LENGTH,
  OPT_DOUBLE,
  OPT_HOLD,
  OPT_CHANNEL,
  OPT_TYPE,
  OPT_MUTE,
  OPT_BPM,
  OPT_DRUM_MIDI,   // drum tracks: kit on one channel / a channel per drum
  OPT_DRUM_PAGE,   // drum tracks: open the per-drum page
  OPT_COUNT
};
static const char* const LABELS[OPT_COUNT] = {"Pattern",   "Length",       "Double it",
                                              "Notes end", "MIDI channel", "Type",
                                              "Mute",      "Tempo",        "Drum MIDI",
                                              "Each drum..."};

static bool isOpenFlag = false;
static uint8_t sel = 0;  // index into the visible item list

// Items shown for this track (drum tracks swap "Notes end" for drum MIDI routing).
static uint8_t items(const Track& tr, Opt* out) {
  static const Opt SYNTH[] = {OPT_PATTERN, OPT_LENGTH, OPT_DOUBLE,  OPT_HOLD,
                              OPT_CHANNEL, OPT_TYPE,   OPT_MUTE,    OPT_BPM};
  static const Opt DRUM[] = {OPT_PATTERN,   OPT_LENGTH,    OPT_DOUBLE, OPT_DRUM_MIDI,
                             OPT_DRUM_PAGE, OPT_CHANNEL,   OPT_TYPE,   OPT_MUTE, OPT_BPM};
  const bool drum = tr.kind == TrackKind::Drum;
  const Opt* src = drum ? DRUM : SYNTH;
  const uint8_t n = drum ? sizeof(DRUM) / sizeof(DRUM[0]) : sizeof(SYNTH) / sizeof(SYNTH[0]);
  for (uint8_t i = 0; i < n; i++) out[i] = src[i];
  return n;
}

void open() {
  isOpenFlag = true;
  sel = 0;
}
void close() { isOpenFlag = false; }
bool isOpen() { return isOpenFlag; }

bool onAction(uint8_t a) {
  const uint8_t t = selectedTrack();
  Track& tr = song.tracks[t];
  Opt list[OPT_COUNT];
  const uint8_t n = items(tr, list);
  if (sel >= n) sel = 0;
  int d = 0;
  switch (a) {
    case layout::ACT_UP: sel = (sel + n - 1) % n; return false;
    case layout::ACT_DOWN: sel = (sel + 1) % n; return false;
    case layout::ACT_LEFT: d = -1; break;
    case layout::ACT_RIGHT: d = +1; break;
    case layout::ACT_SHORTER: d = -16; break;  // f / l: a bar at a time
    case layout::ACT_LONGER: d = +16; break;
    case layout::ACT_OPTIONS:
    case layout::ACT_DELETE: close(); return false;
    default: return false;
  }
  const int dir = d > 0 ? 1 : -1;
  switch (list[sel]) {
    case OPT_PATTERN:
      tr.pattern = (uint8_t)((tr.pattern + PATTERNS + dir) % PATTERNS);
      return true;
    case OPT_LENGTH: {
      const int cur = tr.cur().length;
      int len = cur + d;
      if (keys::isDown(IDX_OPT) && (a == layout::ACT_LEFT || a == layout::ACT_RIGHT)) {
        // opt + , / snaps to the previous / next whole bar: 17 -> 32, 40 -> 32.
        len = dir > 0 ? (cur / 16 + 1) * 16 : ((cur - 1) / 16) * 16;
        if (len < 16) len = cur < 16 ? cur : 16;  // nothing below one bar to snap to
      }
      setPatternLength(tr.cur(), (uint8_t)(len < 1 ? 1 : len > MAX_STEPS ? MAX_STEPS : len));
      return true;
    }
    case OPT_DOUBLE:
      toast(doublePattern(tr.cur()) ? "Pattern doubled" : "Too long to double (max 128)");
      return true;
    case OPT_HOLD:
      tr.hold = !tr.hold;
      releaseTrack(t);
      break;
    case OPT_CHANNEL:
      player::releaseAll();
      releaseTrack(t);
      tr.channel = (uint8_t)((tr.channel - 1 + dir + 16) % 16 + 1);
      break;
    case OPT_TYPE:
      player::releaseAll();
      releaseTrack(t);
      tr.kind = tr.kind == TrackKind::Drum ? TrackKind::Synth : TrackKind::Drum;
      break;
    case OPT_MUTE:
      tr.mute = !tr.mute;
      if (tr.mute) releaseTrack(t);
      break;
    case OPT_DRUM_MIDI:
      player::releaseAll();
      releaseTrack(t);
      tr.drums.mode = tr.drums.mode == DRUM_KIT ? DRUM_LANES_MODE : DRUM_KIT;
      toast(tr.drums.mode == DRUM_KIT ? "Drums: one channel, a note per drum"
                                      : "Drums: each drum on its own channel");
      break;
    case OPT_DRUM_PAGE:
      close();
      goTo(drumMidiScreen());
      break;
    case OPT_BPM: {
      int b = song.bpm + d;
      song.bpm = (uint16_t)(b < BPM_MIN ? BPM_MIN : b > BPM_MAX ? BPM_MAX : b);
      break;
    }
    default: break;
  }
  return false;
}

void draw(M5Canvas& c) {
  const Track& tr = song.tracks[selectedTrack()];
  Opt list[OPT_COUNT];
  const uint8_t n = items(tr, list);
  if (sel >= n) sel = 0;
  const int x = 20, y = 15, w = 200, rowH = 10, h = 18 + n * rowH + 12;
  c.fillRoundRect(x, y, w, h, 5, col::BAR);
  c.drawRoundRect(x, y, w, h, 5, col::ACCENT);
  c.setFont(&fonts::Font0);
  c.setTextDatum(top_left);
  c.setTextColor(col::ACCENT);
  char buf[24];
  snprintf(buf, sizeof(buf), "TRACK %d %s", selectedTrack() + 1, tr.name);
  c.drawString(buf, x + 8, y + 6);
  for (uint8_t i = 0; i < n; i++) {
    const Opt o = list[i];
    int ry = y + 18 + i * rowH;
    if (i == sel) c.fillRect(x + 3, ry - 1, w - 6, rowH, col::SELECT);
    c.setTextDatum(top_left);
    c.setTextColor(i == sel ? col::TEXT : col::DIM);
    c.drawString(LABELS[o], x + 8, ry);
    switch (o) {
      case OPT_PATTERN: snprintf(buf, sizeof(buf), "%d / %d", tr.pattern + 1, PATTERNS); break;
      case OPT_LENGTH: {
        int len = tr.patterns[tr.pattern].length;
        if (len % 16 == 0) snprintf(buf, sizeof(buf), "%d (%d bar%s)", len, len / 16, len == 16 ? "" : "s");
        else snprintf(buf, sizeof(buf), "%d steps", len);
        break;
      }
      case OPT_DOUBLE: snprintf(buf, sizeof(buf), "press , or /"); break;
      case OPT_HOLD: snprintf(buf, sizeof(buf), "%s", tr.hold ? "next note (hold)" : "after length"); break;
      case OPT_CHANNEL:
        if (tr.kind == TrackKind::Drum && tr.drums.mode == DRUM_LANES_MODE) snprintf(buf, sizeof(buf), "per drum");
        else snprintf(buf, sizeof(buf), "%d", tr.channel);
        break;
      case OPT_TYPE: snprintf(buf, sizeof(buf), "%s", tr.kind == TrackKind::Drum ? "drums" : "synth"); break;
      case OPT_MUTE: snprintf(buf, sizeof(buf), "%s", tr.mute ? "MUTED" : "off"); break;
      case OPT_BPM: snprintf(buf, sizeof(buf), "%d bpm", song.bpm); break;
      case OPT_DRUM_MIDI:
        if (tr.drums.mode == DRUM_KIT) snprintf(buf, sizeof(buf), "kit on ch %d", tr.channel);
        else snprintf(buf, sizeof(buf), "ch per drum");
        break;
      case OPT_DRUM_PAGE: snprintf(buf, sizeof(buf), "open >"); break;
      default: buf[0] = 0;
    }
    c.setTextDatum(top_right);
    c.setTextColor(i == sel ? col::HELD : col::TEXT);
    c.drawString(buf, x + w - 8, ry);
  }
  c.setTextDatum(top_left);
  c.setTextColor(col::DIM);
  c.drawString(", / change  f/l: bar  ' close", x + 8, y + h - 11);
}

}  // namespace options
}  // namespace ui
