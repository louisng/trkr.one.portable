// Step screen: the main (non-live) editor.
//
// Synth tracks: 16 steps per page. Press piano keys or a chord button to write
// it at the cursor; the cursor then moves on. Holding a key while pressing
// more stacks them on the same step. f / l shorten / lengthen what you just
// entered (or the step under the cursor).
// Drum tracks: 12-lane grid. Drum pads toggle hits at the cursor.
//
// , / move the cursor, ; . pick the track, del clears the step,
// ' opens the pattern / track options.
#include <stdio.h>

#include "../app/Settings.h"
#include "../input/KeyLayout.h"
#include "../music/Theory.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

using namespace seq;

static constexpr int PAGE = 16;
static constexpr int GRID_TOP = 40;

enum Opt : uint8_t { OPT_PATTERN, OPT_LENGTH, OPT_CHANNEL, OPT_TYPE, OPT_MUTE, OPT_BPM, OPT_COUNT };
static const char* const OPT_LABELS[OPT_COUNT] = {"Pattern", "Length", "MIDI channel",
                                                  "Type", "Mute", "Tempo"};

class StepScreen : public Screen {
 public:
  const char* title() const override { return "STEP"; }

  void onEnter() override {
    panel = false;
    entryHeld = 0;
  }

  void onAction(uint8_t a) override {
    Track& tr = song.tracks[selectedTrack()];
    Pattern& p = tr.cur();
    if (panel) {
      panelAction(a, tr);
      return;
    }
    switch (a) {
      case layout::ACT_LEFT: moveCursor(-1, p); break;
      case layout::ACT_RIGHT: moveCursor(+1, p); break;
      case layout::ACT_UP: selectTrack((selectedTrack() + TRACKS - 1) % TRACKS); resetEntry(); break;
      case layout::ACT_DOWN: selectTrack((selectedTrack() + 1) % TRACKS); resetEntry(); break;
      case layout::ACT_DELETE: clearStep(p, cursor % p.length); break;
      case layout::ACT_SHORTER:
      case layout::ACT_LONGER: {
        if (tr.kind != TrackKind::Synth) break;
        int target = lastEntered >= 0 ? lastEntered : cursor % p.length;
        int d = (a == layout::ACT_LONGER ? 1 : -1) * TICKS_PER_STEP;
        adjustStepLength(p, (uint8_t)target, d);
        NoteEvent ev[MAX_NOTES_PER_STEP];
        if (notesAt(p, (uint8_t)target, ev)) entryLen = ev[0].len;  // next entries match
        break;
      }
      case layout::ACT_OPTIONS:
        panel = true;
        panelSel = 0;
        break;
      default: break;
    }
  }

  void onPlayed(uint8_t, const player::Played& pl) override {
    if (panel) return;
    const uint8_t t = selectedTrack();
    Track& tr = song.tracks[t];
    Pattern& p = tr.cur();
    if (pl.track != t) return;  // played on another track (e.g. piano keys on a drum track)
    const uint8_t step = (uint8_t)(cursor % p.length);

    if (tr.kind == TrackKind::Drum) {
      if (pl.pressed && pl.role == layout::Role::Drum)
        toggleNote(p, step, pl.notes[0], settings.velocity, TICKS_PER_STEP);
      return;
    }

    if (!pl.pressed) {
      if (entryHeld) entryHeld--;
      return;
    }
    if (entryHeld > 0 && entryStep >= 0) {
      // Another key while one is still held: stack onto the same step.
      for (uint8_t i = 0; i < pl.count; i++)
        addNote(p, NoteEvent{(uint8_t)entryStep, 0, pl.notes[i], settings.velocity, entryLen, 0});
    } else {
      setStep(p, step, pl.notes, pl.count, settings.velocity, entryLen);
      entryStep = step;
      lastEntered = step;
      cursor = (step + 1) % p.length;
    }
    entryHeld++;
  }

  void draw(M5Canvas& c) override {
    const uint8_t t = selectedTrack();
    Track& tr = song.tracks[t];
    Pattern& p = tr.cur();
    if (cursor >= p.length) cursor = p.length - 1;

    drawHeader(c, tr, p);
    drawTrackStrip(c);
    if (tr.kind == TrackKind::Drum) drawDrumGrid(c, t, p);
    else drawSynthGrid(c, t, p);
    if (panel) drawPanel(c, tr);
  }

 private:
  int cursor = 0;
  int lastEntered = -1;  // step just written by step entry (target of f / l)
  int entryStep = -1;
  uint8_t entryHeld = 0;
  uint16_t entryLen = TICKS_PER_STEP;
  bool panel = false;
  uint8_t panelSel = 0;

  void resetEntry() {
    lastEntered = -1;
    entryStep = -1;
    entryHeld = 0;
  }

  void moveCursor(int d, const Pattern& p) {
    cursor = (cursor + d + p.length) % p.length;
    resetEntry();
  }

  // ── Options panel ──────────────────────────────────────────────────────────
  void panelAction(uint8_t a, Track& tr) {
    int d = 0;
    switch (a) {
      case layout::ACT_UP: panelSel = (panelSel + OPT_COUNT - 1) % OPT_COUNT; return;
      case layout::ACT_DOWN: panelSel = (panelSel + 1) % OPT_COUNT; return;
      case layout::ACT_LEFT: d = -1; break;
      case layout::ACT_RIGHT: d = +1; break;
      case layout::ACT_OPTIONS:
      case layout::ACT_DELETE: panel = false; return;
      default: return;
    }
    switch (panelSel) {
      case OPT_PATTERN:
        tr.pattern = (uint8_t)((tr.pattern + PATTERNS + d) % PATTERNS);
        resetEntry();
        break;
      case OPT_LENGTH: {
        int len = tr.cur().length + d;
        setPatternLength(tr.cur(), (uint8_t)(len < 1 ? 1 : len > MAX_STEPS ? MAX_STEPS : len));
        break;
      }
      case OPT_CHANNEL:
        player::releaseAll();
        tr.channel = (uint8_t)((tr.channel - 1 + d + 16) % 16 + 1);
        break;
      case OPT_TYPE:
        player::releaseAll();
        tr.kind = tr.kind == TrackKind::Drum ? TrackKind::Synth : TrackKind::Drum;
        break;
      case OPT_MUTE: tr.mute = !tr.mute; break;
      case OPT_BPM: {
        int b = song.bpm + d;
        song.bpm = (uint16_t)(b < BPM_MIN ? BPM_MIN : b > BPM_MAX ? BPM_MAX : b);
        break;
      }
      default: break;
    }
  }

  void drawPanel(M5Canvas& c, const Track& tr) {
    const int x = 40, y = 28, w = 160, rowH = 12, h = 20 + OPT_COUNT * rowH + 12;
    c.fillRoundRect(x, y, w, h, 5, col::BAR);
    c.drawRoundRect(x, y, w, h, 5, col::ACCENT);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::ACCENT);
    char buf[24];
    snprintf(buf, sizeof(buf), "TRACK %d %s", selectedTrack() + 1, tr.name);
    c.drawString(buf, x + 8, y + 6);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
      int ry = y + 20 + i * rowH;
      if (i == panelSel) c.fillRect(x + 3, ry - 2, w - 6, rowH, col::SELECT);
      c.setTextDatum(top_left);
      c.setTextColor(i == panelSel ? col::TEXT : col::DIM);
      c.drawString(OPT_LABELS[i], x + 8, ry);
      switch (i) {
        case OPT_PATTERN: snprintf(buf, sizeof(buf), "%d / %d", tr.pattern + 1, PATTERNS); break;
        case OPT_LENGTH: snprintf(buf, sizeof(buf), "%d steps", tr.patterns[tr.pattern].length); break;
        case OPT_CHANNEL: snprintf(buf, sizeof(buf), "%d", tr.channel); break;
        case OPT_TYPE: snprintf(buf, sizeof(buf), "%s", tr.kind == TrackKind::Drum ? "drums" : "synth"); break;
        case OPT_MUTE: snprintf(buf, sizeof(buf), "%s", tr.mute ? "MUTED" : "off"); break;
        case OPT_BPM: snprintf(buf, sizeof(buf), "%d bpm", song.bpm); break;
      }
      c.setTextDatum(top_right);
      c.setTextColor(i == panelSel ? col::HELD : col::TEXT);
      c.drawString(buf, x + w - 8, ry);
    }
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    c.drawString("; . pick  , / change  ' close", x + 8, y + h - 11);
  }

  // ── Header + track strip ───────────────────────────────────────────────────
  void drawHeader(M5Canvas& c, const Track& tr, const Pattern& p) {
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::TEXT);
    char buf[48];
    snprintf(buf, sizeof(buf), "%d %s  P%d  %dst  ch%d%s", selectedTrack() + 1, tr.name,
             tr.pattern + 1, p.length, tr.channel, tr.mute ? "  MUTE" : "");
    c.drawString(buf, 4, 17);
    const int pages = (p.length + PAGE - 1) / PAGE;
    snprintf(buf, sizeof(buf), "pg %d/%d", cursor / PAGE + 1, pages);
    c.setTextDatum(top_right);
    c.setTextColor(col::DIM);
    c.drawString(buf, W - 4, 17);
  }

  void drawTrackStrip(M5Canvas& c) {
    const int y = 27, w = W / TRACKS;
    c.setFont(&fonts::TomThumb);
    c.setTextDatum(middle_center);
    for (uint8_t t = 0; t < TRACKS; t++) {
      const Track& tr = song.tracks[t];
      bool selT = t == selectedTrack();
      uint16_t bg = selT ? col::ACCENT : (tr.kind == TrackKind::Drum ? 0x4208 : col::FAINT);
      c.fillRoundRect(t * w + 1, y, w - 2, 10, 2, bg);
      c.setTextColor(selT ? TFT_BLACK : (tr.mute ? col::FAINT : col::TEXT));
      char buf[10];
      snprintf(buf, sizeof(buf), "%d %s", t + 1, tr.name);
      c.drawString(buf, t * w + w / 2, y + 6);
      if (tr.mute) c.drawFastHLine(t * w + 4, y + 5, w - 8, col::DIM);
    }
  }

  // ── Synth grid ─────────────────────────────────────────────────────────────
  void drawSynthGrid(M5Canvas& c, uint8_t t, const Pattern& p) {
    const int cw = W / PAGE, top = GRID_TOP, h = 70;
    const int page0 = (cursor / PAGE) * PAGE;
    const int play = trackStep(t);

    for (int i = 0; i < PAGE; i++) {
      const int s = page0 + i, x = i * cw;
      const bool inPat = s < p.length;
      uint16_t bg = !inPat ? col::BG : ((i / 4) % 2 ? 0x10A3 : 0x18E4);
      if (inPat && s == play) bg = 0x2A69;
      c.fillRect(x, top, cw - 1, h, bg);
      if (!inPat) continue;

      NoteEvent ev[MAX_NOTES_PER_STEP];
      uint8_t n = notesAt(p, (uint8_t)s, ev);
      if (n) {
        // Highest note's name + octave, and "+n" for chords.
        uint8_t hi = 0;
        for (uint8_t k = 1; k < n && k < MAX_NOTES_PER_STEP; k++)
          if (ev[k].pitch > ev[hi].pitch) hi = k;
        c.setFont(&fonts::Font0);
        c.setTextDatum(top_center);
        c.setTextColor(col::HELD);
        c.drawString(theory::noteName(ev[hi].pitch % 12), x + cw / 2, top + 4);
        char oc[4];
        snprintf(oc, sizeof(oc), "%d", ev[hi].pitch / 12 - 1);
        c.setTextColor(col::TEXT);
        c.drawString(oc, x + cw / 2, top + 14);
        if (n > 1) {
          snprintf(oc, sizeof(oc), "+%d", n - 1);
          c.setTextColor(col::CHORD);
          c.drawString(oc, x + cw / 2, top + 24);
        }
        // Velocity bar.
        int vh = ev[hi].vel * 18 / 127;
        c.fillRect(x + 3, top + h - 3 - vh, cw - 7, vh, col::OK);
        // Length bar: spans the steps the note rings for.
        int lenPx = (int)ev[hi].len * cw / TICKS_PER_STEP - 2;
        if (x + lenPx > W) lenPx = W - x;
        c.fillRect(x + 1, top + 38, lenPx > 2 ? lenPx : 2, 3, col::ACCENT);
        if (ev[hi].offset) c.fillCircle(x + cw / 2 + (ev[hi].offset > 0 ? 4 : -4), top + 46, 1, col::DIM);
      }
      if (s == cursor) c.drawRect(x, top, cw - 1, h, TFT_WHITE);
    }

    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    c.drawString("notes: enter  del: clear  f/l: length", 4, top + h + 4);
    c.drawString("; . track  , / move  ' options", 4, top + h + 14);
  }

  // ── Drum grid ──────────────────────────────────────────────────────────────
  void drawDrumGrid(M5Canvas& c, uint8_t t, const Pattern& p) {
    const int labelW = 14, cw = (W - labelW) / PAGE, laneH = 7, top = GRID_TOP;
    const int page0 = (cursor / PAGE) * PAGE;
    const int play = trackStep(t);
    c.setFont(&fonts::TomThumb);
    c.setTextDatum(middle_left);

    for (uint8_t lane = 0; lane < 12; lane++) {
      int y = top + lane * laneH;
      c.setTextColor(col::DIM);
      c.drawString(player::drumShortName(lane), 1, y + laneH / 2);
      uint8_t pitch = player::drumNote(lane);
      for (int i = 0; i < PAGE; i++) {
        const int s = page0 + i, x = labelW + i * cw;
        if (s >= p.length) continue;
        uint16_t bg = (i / 4) % 2 ? 0x10A3 : 0x18E4;
        if (s == play) bg = 0x2A69;
        c.fillRect(x, y, cw - 1, laneH - 1, bg);
        if (hasNote(p, (uint8_t)s, pitch)) c.fillRect(x + 2, y + 1, cw - 5, laneH - 3, col::DRUM);
      }
    }
    if (cursor - page0 < PAGE) {
      int x = labelW + (cursor - page0) * cw;
      c.drawRect(x - 1, top - 1, cw + 1, 12 * laneH + 1, TFT_WHITE);
    }
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    c.drawString(layout::upperPiano() ? "fn+p: drum pads are piano now"
                                      : "drum pads: toggle  del: clear",
                 4, top + 12 * laneH + 3);
  }
};

static StepScreen instance;
Screen* stepScreen() { return &instance; }

}  // namespace ui
