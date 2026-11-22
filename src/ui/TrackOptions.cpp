#include "TrackOptions.h"

#include <stdio.h>

#include "../app/Settings.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../play/Arp.h"
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
  static const char L[FX_COUNT] = {'-', 'P', 'R', 'T', 'H', 'M', 'B', 'G', 'S', 'A'};
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
    case FX_ARP: return arp::encode(arp::UP, 3, 1);  // up, 1/16, 1 octave
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
  if (cmd == FX_ARP) {
    // , / step the mode; with shift, the rate (and past the last rate, the octaves)
    uint8_t m = 0, r = 3, o = 1;
    arp::decode(val, &m, &r, &o);
    int idx = m + arp::STEP_MODES * (r + arp::RATE_COUNT * (o - 1));
    const int count = arp::STEP_MODES * arp::RATE_COUNT * 4;
    idx = ((idx + dir * (big ? arp::STEP_MODES : 1)) % count + count) % count;
    m = (uint8_t)(idx % arp::STEP_MODES);
    r = (uint8_t)((idx / arp::STEP_MODES) % arp::RATE_COUNT);
    o = (uint8_t)(idx / (arp::STEP_MODES * arp::RATE_COUNT) + 1);
    return arp::encode(m, r, o);
  }
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
    case FX_ARP: {
      uint8_t m, r, o;
      if (arp::decode(val, &m, &r, &o)) snprintf(out, n, "A: arp %s %s %d oct", arp::modeName(m), arp::rateName(r), o);
      else snprintf(out, n, "A: arp");
      break;
    }
    case FX_STRUM: {
      static const char* const SPEED[3] = {"fast", "medium", "slow"};
      if (strumValid(val)) snprintf(out, n, "S: strum %s %s", strumUp(val) ? "up" : "down", SPEED[(val - 1) % 3]);
      else snprintf(out, n, "S: strum (1-6)");
      break;
    }
    default: snprintf(out, n, "No effect"); break;
  }
}

void placeArp(Track& tr, Pattern& p, uint8_t step, const uint8_t* notes, uint8_t n, uint8_t code) {
  if (tr.kind == TrackKind::Drum) { toast("Arps go on a synth track"); return; }
  if (step >= p.length) { toast("Past the end of this pattern"); return; }
  uint8_t mode = 0, rate = 3, oct = 1;
  if (!arp::decode(code, &mode, &rate, &oct)) return;
  uint8_t use[MAX_NOTES_PER_STEP];
  uint8_t cnt = 0;
  if (n) {
    for (uint8_t i = 0; i < n && cnt < MAX_NOTES_PER_STEP; i++) use[cnt++] = notes[i];
  } else {   // no notes held in the arp: arpeggiate what the step already has
    NoteEvent ev[MAX_NOTES_PER_STEP];
    const uint8_t have = notesAt(p, step, ev);
    for (uint8_t i = 0; i < have && i < MAX_NOTES_PER_STEP; i++)
      if (ev[i].pitch != PITCH_OFF) use[cnt++] = ev[i].pitch;
  }
  if (!cnt) { toast("Hold notes in the arp, or put a chord on this step"); return; }
  // one round of the arpeggio, as long as the pattern allows
  uint8_t seqOut[arp::MAX_SEQ];
  const uint8_t len = arp::sequence(use, cnt, mode == arp::RANDOM ? (uint8_t)arp::UP : mode, oct, seqOut);
  uint32_t ticks = (uint32_t)len * arp::rateTicks(rate);
  const uint32_t room = (uint32_t)(p.length - step) * TICKS_PER_STEP;
  if (ticks < TICKS_PER_STEP) ticks = TICKS_PER_STEP;
  if (ticks > room) ticks = room;
  if (n) setStep(p, step, use, cnt, settings.velocity, (uint16_t)ticks);
  else setStepLength(p, step, (uint16_t)ticks);
  setFx(p, step, FX_ARP, code);
  char msg[44];
  snprintf(msg, sizeof(msg), "Arp %s %s on step %d (%d steps)", arp::modeName(mode), arp::rateName(rate), step + 1,
           (int)((ticks + TICKS_PER_STEP - 1) / TICKS_PER_STEP));
  if (n > MAX_NOTES_PER_STEP) snprintf(msg, sizeof(msg), "Arp on step %d: its first 4 notes (a step holds 4)", step + 1);
  toast(msg);
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
  OPT_TRANSPOSE,   // synth tracks: move this pattern's notes
  OPT_TRANSPOSE_ALL,  // every synth track, every pattern
  OPT_COUNT
};
static const char* const LABELS[OPT_COUNT] = {"Pattern",   "Length",       "Double it",
                                              "Notes end", "MIDI channel", "Type",
                                              "Mute",      "Tempo",        "Drum MIDI",
                                              "Each drum...", "Transpose", "Transpose song"};

static bool isOpenFlag = false;
static uint8_t sel = 0;  // index into the visible item list

// Items shown for this track (drum tracks swap "Notes end" for drum MIDI routing).
static uint8_t items(const Track& tr, Opt* out) {
  static const Opt SYNTH[] = {OPT_PATTERN, OPT_LENGTH, OPT_DOUBLE, OPT_TRANSPOSE, OPT_TRANSPOSE_ALL,
                              OPT_HOLD,    OPT_CHANNEL, OPT_TYPE,  OPT_MUTE,      OPT_BPM};
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
    case OPT_TRANSPOSE:
    case OPT_TRANSPOSE_ALL: {
      // , / = a semitone, f / l = an octave
      const int semis = (d == 16 || d == -16) ? dir * 12 : dir;
      player::releaseAll();
      releaseTrack(t);
      char msg[40];
      if (list[sel] == OPT_TRANSPOSE) {
        if (transposePattern(tr.cur(), semis)) snprintf(msg, sizeof(msg), "Pattern %+d semitone%s", semis, semis == 1 || semis == -1 ? "" : "s");
        else snprintf(msg, sizeof(msg), tr.cur().notes.empty() ? "No notes to move" : "Out of range");
      } else {
        for (uint8_t k = 0; k < TRACKS; k++) releaseTrack(k);
        const int n = transposeSong(song, semis);
        if (n > 0) snprintf(msg, sizeof(msg), "Song %+d: %d notes moved", semis, n);
        else snprintf(msg, sizeof(msg), n < 0 ? "Out of range" : "No notes to move");
      }
      toast(msg);
      return true;
    }
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
  const uint8_t vis = n < 9 ? n : 9;   // rows that fit on the screen; the list scrolls past that
  const uint8_t first = sel < vis ? 0 : (uint8_t)(sel - vis + 1);
  const int x = 20, y = 15, w = 200, rowH = 10, h = 18 + vis * rowH + 12;
  c.fillRoundRect(x, y, w, h, 5, col::BAR);
  c.drawRoundRect(x, y, w, h, 5, col::ACCENT);
  c.setFont(&fonts::Font0);
  c.setTextDatum(top_left);
  c.setTextColor(col::ACCENT);
  char buf[24];
  snprintf(buf, sizeof(buf), "TRACK %d %s", selectedTrack() + 1, tr.name);
  c.drawString(buf, x + 8, y + 6);
  for (uint8_t i = first; i < first + vis; i++) {
    const Opt o = list[i];
    int ry = y + 18 + (i - first) * rowH;
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
      case OPT_TRANSPOSE: snprintf(buf, sizeof(buf), ", / 1  f/l octave"); break;
      case OPT_TRANSPOSE_ALL: snprintf(buf, sizeof(buf), "all synth tracks"); break;
      default: buf[0] = 0;
    }
    c.setTextDatum(top_right);
    c.setTextColor(i == sel ? col::HELD : col::TEXT);
    c.drawString(buf, x + w - 8, ry);
  }
  c.setTextDatum(top_left);
  c.setTextColor(col::DIM);
  c.drawString(", / change  f/l: bar  ' close", x + 8, y + h - 11);
  if (n > vis) {   // more above / below
    c.setTextDatum(top_right);
    c.drawString(first > 0 ? (first + vis < n ? "^v" : "^") : "v", x + w - 8, y + h - 11);
  }
}

}  // namespace options
}  // namespace ui
