#include "TrackOptions.h"

#include <stdio.h>

#include "../input/KeyLayout.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

using namespace seq;

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
  OPT_COUNT
};
static const char* const LABELS[OPT_COUNT] = {"Pattern",   "Length",       "Double it",
                                              "Notes end", "MIDI channel", "Type",
                                              "Mute",      "Tempo"};

static bool isOpenFlag = false;
static uint8_t sel = 0;

void open() {
  isOpenFlag = true;
  sel = 0;
}
void close() { isOpenFlag = false; }
bool isOpen() { return isOpenFlag; }

bool onAction(uint8_t a) {
  const uint8_t t = selectedTrack();
  Track& tr = song.tracks[t];
  int d = 0;
  switch (a) {
    case layout::ACT_UP: sel = (sel + OPT_COUNT - 1) % OPT_COUNT; return false;
    case layout::ACT_DOWN: sel = (sel + 1) % OPT_COUNT; return false;
    case layout::ACT_LEFT: d = -1; break;
    case layout::ACT_RIGHT: d = +1; break;
    case layout::ACT_SHORTER: d = -16; break;  // f / l: a bar at a time
    case layout::ACT_LONGER: d = +16; break;
    case layout::ACT_OPTIONS:
    case layout::ACT_DELETE: close(); return false;
    default: return false;
  }
  const int dir = d > 0 ? 1 : -1;
  switch (sel) {
    case OPT_PATTERN:
      tr.pattern = (uint8_t)((tr.pattern + PATTERNS + dir) % PATTERNS);
      return true;
    case OPT_LENGTH: {
      int len = tr.cur().length + d;
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
  const int x = 20, y = 16, w = 200, rowH = 11, h = 18 + OPT_COUNT * rowH + 12;
  c.fillRoundRect(x, y, w, h, 5, col::BAR);
  c.drawRoundRect(x, y, w, h, 5, col::ACCENT);
  c.setFont(&fonts::Font0);
  c.setTextDatum(top_left);
  c.setTextColor(col::ACCENT);
  char buf[24];
  snprintf(buf, sizeof(buf), "TRACK %d %s", selectedTrack() + 1, tr.name);
  c.drawString(buf, x + 8, y + 6);
  for (uint8_t i = 0; i < OPT_COUNT; i++) {
    int ry = y + 18 + i * rowH;
    if (i == sel) c.fillRect(x + 3, ry - 2, w - 6, rowH, col::SELECT);
    c.setTextDatum(top_left);
    c.setTextColor(i == sel ? col::TEXT : col::DIM);
    c.drawString(LABELS[i], x + 8, ry);
    switch (i) {
      case OPT_PATTERN: snprintf(buf, sizeof(buf), "%d / %d", tr.pattern + 1, PATTERNS); break;
      case OPT_LENGTH: {
        int len = tr.patterns[tr.pattern].length;
        if (len % 16 == 0) snprintf(buf, sizeof(buf), "%d (%d bar%s)", len, len / 16, len == 16 ? "" : "s");
        else snprintf(buf, sizeof(buf), "%d steps", len);
        break;
      }
      case OPT_DOUBLE: snprintf(buf, sizeof(buf), "press , or /"); break;
      case OPT_HOLD: snprintf(buf, sizeof(buf), "%s", tr.hold ? "next note (hold)" : "after length"); break;
      case OPT_CHANNEL: snprintf(buf, sizeof(buf), "%d", tr.channel); break;
      case OPT_TYPE: snprintf(buf, sizeof(buf), "%s", tr.kind == TrackKind::Drum ? "drums" : "synth"); break;
      case OPT_MUTE: snprintf(buf, sizeof(buf), "%s", tr.mute ? "MUTED" : "off"); break;
      case OPT_BPM: snprintf(buf, sizeof(buf), "%d bpm", song.bpm); break;
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
