// Tracker: every track side by side, one row per step, with each note's
// value laid bare. It's another view of the same song as the Step screen, so
// edits show up in both immediately.
//
// Per track:  NOTE [NOTE...]  VL  LEN  OF  FX
//   NOTE  one column per chord note (as many as the track's biggest chord)
//   VL    velocity, hex 01-7F
//   LEN   note length in steps (.25 .5 1 2 ...)
//   OF    timing offset in ticks (12 ticks per step)
//   FX    effect letter + value (hex): P chance, R ratchet, T trig condition,
//         H humanize, M mod wheel, B pitch bend, G program change
//
// Keys: ; . rows (hold opt: 10 at a time)   , / columns (scrolls across tracks)
//   piano / chord / drum keys: write notes at the cursor, move down a row
//   f / l: value down / up (note column: transpose a semitone); shift = bigger
//   ctrl + arrows: change what is under the cursor (a note, a number, an arp or strum), or on a blank
//   row put in the last note or chord played (see ctrlArrow)
//   fn+e: put the arp (its held notes, mode, rate, octaves) on this row
//   del: clear the value (on an empty row: OFF)   ': track options
#include <stdio.h>

#include "../app/Settings.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../music/Theory.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"
#include "TrackOptions.h"

namespace ui {

using namespace seq;

static constexpr int ROW_H = 9;
static constexpr int ROWS_TOP = 34;
static constexpr int VISIBLE_ROWS = (H - ROWS_TOP) / ROW_H;  // 11
static constexpr int ROWNUM_W = 14;
static constexpr int NOTE_W = 20, VEL_W = 14, LEN_W = 20, OFS_W = 14, FXC_W = 8, FXV_W = 14,
                     GAP_W = 4;
static constexpr int EXTRA_FIELDS = 5;  // VL LEN OF FX FV
static constexpr uint8_t IDX_SHIFT = 2 * 14 + 1;
static constexpr uint8_t IDX_OPT = 3 * 14 + 1;
static constexpr uint8_t IDX_CTRL = 3 * 14 + 0;
static constexpr int JUMP_ROWS = 10;

enum FieldKind : uint8_t { F_NOTE, F_VEL, F_LEN, F_OFS, F_FXC, F_FXV };

static uint8_t noteCols(const Pattern& p) {
  uint8_t maxN = 1;
  for (uint8_t s = 0; s < p.length; s++) {
    uint8_t n = notesAt(p, s);
    if (n > maxN) maxN = n;
  }
  return maxN > MAX_NOTES_PER_STEP ? MAX_NOTES_PER_STEP : maxN;
}

static int trackWidth(uint8_t nc) {
  return nc * NOTE_W + VEL_W + LEN_W + OFS_W + FXC_W + FXV_W + GAP_W;
}

// Notes on a step sorted by pitch, OFF excluded. Returns count; sets *off.
static uint8_t stepNotes(const Pattern& p, uint8_t step, NoteEvent* out, bool* off) {
  NoteEvent ev[MAX_NOTES_PER_STEP];
  uint8_t n = notesAt(p, step, ev), k = 0;
  *off = false;
  for (uint8_t i = 0; i < n && i < MAX_NOTES_PER_STEP; i++) {
    if (ev[i].pitch == PITCH_OFF) *off = true;
    else out[k++] = ev[i];
  }
  return k;
}

class TrackerScreen : public Screen {
 public:
  const char* title() const override { return "TRACKER"; }

  void onEnter() override {
    options::close();
    track = selectedTrack();
    entryHeld = 0;
  }

  void onAction(uint8_t a) override {
    if (options::isOpen()) {
      options::onAction(a);
      return;
    }
    Track& tr = song.tracks[track];
    Pattern& p = tr.cur();
    const uint8_t nc = noteCols(p);
    const bool shift = keys::isDown(IDX_SHIFT);
    // ctrl + arrows: change what is under the cursor (see ctrlArrow)
    if (keys::isDown(IDX_CTRL) && row < p.length &&
        (a == layout::ACT_LEFT || a == layout::ACT_RIGHT || a == layout::ACT_UP || a == layout::ACT_DOWN)) {
      ctrlArrow(tr, p, nc, a);
      return;
    }
    switch (a) {
      case layout::ACT_UP: moveRow(keys::isDown(IDX_OPT) ? -JUMP_ROWS : -1, p); break;
      case layout::ACT_DOWN: moveRow(keys::isDown(IDX_OPT) ? JUMP_ROWS : 1, p); break;
      case layout::ACT_LEFT: moveField(-1); break;
      case layout::ACT_RIGHT: moveField(+1); break;
      case layout::ACT_OPTIONS: options::open(); break;
      case layout::ACT_SHORTER:
      case layout::ACT_LONGER:
        if (row < p.length) adjust(tr, p, nc, a == layout::ACT_LONGER ? 1 : -1, shift);
        break;
      case layout::ACT_DELETE:
        if (row < p.length) del(tr, p, nc);
        break;
      default: break;
    }
  }

  bool placeArp(const uint8_t* notes, uint8_t n, uint8_t code) override {
    if (options::isOpen()) return true;
    Track& tr = song.tracks[track];
    ui::placeArp(tr, tr.cur(), (uint8_t)row, notes, n, code);
    return true;
  }

  void onPlayed(uint8_t, const player::Played& pl) override {
    if (options::isOpen()) return;
    Track& tr = song.tracks[track];
    Pattern& p = tr.cur();
    if (pl.track != track) return;  // e.g. piano keys while on a drum track
    if (!pl.pressed) {
      if (entryHeld) entryHeld--;
      return;
    }
    if (entryHeld == 0) lastCount = 0;   // a new note or chord: remember it for ctrl + arrows on a blank row
    for (uint8_t i = 0; i < pl.count && lastCount < MAX_NOTES_PER_STEP; i++) lastNotes[lastCount++] = pl.notes[i];
    if (row >= p.length) {
      toast("Past the end of this pattern");
      return;
    }
    const uint8_t nc = noteCols(p);
    NoteEvent ev[MAX_NOTES_PER_STEP];
    bool off;

    if (entryHeld > 0 && entryRow >= 0) {
      // More keys while one is held: stack them on the same row.
      uint8_t n = stepNotes(p, (uint8_t)entryRow, ev, &off);
      for (uint8_t i = 0; i < pl.count; i++)
        addNote(p, NoteEvent{(uint8_t)entryRow, n ? ev[0].offset : (int8_t)0, pl.notes[i],
                             n ? ev[0].vel : settings.velocity, n ? ev[0].len : entryLen, 0});
    } else {
      const uint8_t step = (uint8_t)row;
      uint8_t n = stepNotes(p, step, ev, &off);
      if (off) clearStep(p, step);
      const int k = field < nc ? field : 0;
      if (pl.count > 1 || n == 0) {
        // Chord button, or an empty row: the row becomes exactly what you played.
        setStep(p, step, pl.notes, pl.count, n ? ev[0].vel : settings.velocity,
                n ? ev[0].len : entryLen);
      } else if (k < n) {
        if (!repitchNote(p, step, ev[k].pitch, pl.notes[0]))
          toast("That note is already on this row");
      } else if (n < MAX_NOTES_PER_STEP) {
        addNote(p, NoteEvent{step, ev[0].offset, pl.notes[0], ev[0].vel, ev[0].len, 0});
      } else {
        repitchNote(p, step, ev[n - 1].pitch, pl.notes[0]);
      }
      entryRow = step;
      moveRow(+1, p);
      cursorMovedAt = millis();
    }
    entryHeld++;
  }

  void draw(M5Canvas& c) override {
    if (track != selectedTrack()) {  // changed elsewhere (Live / Step screen)
      track = selectedTrack();
      field = 0;
    }
    const Pattern& cp = song.tracks[track].patterns[song.tracks[track].pattern];
    if (field >= fieldCount(track)) field = fieldCount(track) - 1;

    // Which rows to show: follow the playhead while playing (unless you just
    // moved the cursor), otherwise keep the cursor in view.
    int focus = row;
    const int play = trackStep(track);
    if (play >= 0 && (millis() - cursorMovedAt > 2500 || recording())) focus = play;
    if (focus < top) top = focus;
    if (focus >= top + VISIBLE_ROWS) top = focus - VISIBLE_ROWS + 1;
    if (top < 0) top = 0;
    (void)cp;

    // Which tracks fit: keep the cursor track fully visible.
    if (track < firstTrack) firstTrack = track;
    while (true) {
      int x = ROWNUM_W;
      for (uint8_t t = firstTrack; t <= track; t++) x += trackWidth(noteCols(song.tracks[t].cur()));
      if (x <= W || firstTrack == track) break;
      firstTrack++;
    }

    drawRowNumbers(c);
    int x = ROWNUM_W;
    for (uint8_t t = firstTrack; t < TRACKS && x < W; t++) {
      x = drawTrack(c, t, x);
    }
    if (options::isOpen()) options::draw(c);
  }

 private:
  uint8_t track = 1;
  uint8_t lastNotes[MAX_NOTES_PER_STEP] = {60};   // the last note or chord played in (ctrl + arrows on a blank row)
  uint8_t lastCount = 0;
  int row = 0;
  int field = 0;  // within the track: note columns, then VL, LEN, OF
  int top = 0;
  uint8_t firstTrack = 0;
  int entryRow = -1;
  uint8_t entryHeld = 0;
  uint16_t entryLen = TICKS_PER_STEP;
  uint32_t cursorMovedAt = 0;

  int fieldCount(uint8_t t) { return noteCols(song.tracks[t].cur()) + EXTRA_FIELDS; }

  FieldKind kindOf(int f, uint8_t nc) {
    if (f < nc) return F_NOTE;
    static const FieldKind K[EXTRA_FIELDS] = {F_VEL, F_LEN, F_OFS, F_FXC, F_FXV};
    int i = f - nc;
    return i < EXTRA_FIELDS ? K[i] : F_FXV;
  }

  void moveRow(int d, const Pattern& p) {
    row = ((row + d) % p.length + p.length) % p.length;
    entryRow = -1;
    entryHeld = 0;
    cursorMovedAt = millis();
  }

  void moveField(int d) {
    field += d;
    if (field < 0) {
      track = (track + TRACKS - 1) % TRACKS;
      field = fieldCount(track) - 1;
    } else if (field >= fieldCount(track)) {
      track = (track + 1) % TRACKS;
      field = 0;
    }
    const uint8_t len = song.tracks[track].cur().length;
    if (row >= len) row = len - 1;
    selectTrack(track);  // live keys follow the cursor's track
    entryRow = -1;
    entryHeld = 0;
    cursorMovedAt = millis();
  }

  // ctrl + arrows, by what the cursor is on:
  //   note           , /  a semitone          ; .  an octave        (drums: , / the previous / next drum)
  //   blank row      any arrow puts in the last note or chord you played (middle C, or a kick, at first)
  //   velocity       , /  1                   ; .  10
  //   length         , /  the next length     ; .  a bar
  //   offset         , /  1 tick              ; .  3 ticks
  //   effect value   , /  1 (strum: the next one; arp: the mode)   ; .  10 (strum: up / down; arp: the rate)
  //   effect letter  nothing (f / l choose the effect)
  void ctrlArrow(Track& tr, Pattern& p, uint8_t nc, uint8_t a) {
    const bool lr = a == layout::ACT_LEFT || a == layout::ACT_RIGHT;
    const int dir = (a == layout::ACT_RIGHT || a == layout::ACT_UP) ? 1 : -1;
    const uint8_t step = (uint8_t)row;
    const bool drum = tr.kind == TrackKind::Drum;
    NoteEvent ev[MAX_NOTES_PER_STEP];
    bool off;
    const uint8_t n = stepNotes(p, step, ev, &off);
    switch (kindOf(field, nc)) {
      case F_NOTE: {
        if (!n || off) {   // a blank row: the last note or chord played in
          uint8_t notes[MAX_NOTES_PER_STEP];
          uint8_t cnt = lastCount;
          for (uint8_t i = 0; i < cnt; i++) notes[i] = lastNotes[i];
          if (!cnt) notes[cnt++] = drum ? 36 : 60;
          if (off) clearStep(p, step);
          setStep(p, step, notes, cnt, settings.velocity, entryLen);
          player::previewStep(track, notes, cnt, entryLen, settings.velocity);
          return;
        }
        if ((int)field >= n) return;   // the empty column of a chord
        if (drum) {   // drums: the previous / next drum
          if (!lr) return;
          int lane = drumLaneOf(ev[field].pitch);
          for (int tries = 0; tries < DRUM_LANES; tries++) {
            lane = ((lane < 0 ? 0 : lane) + dir + DRUM_LANES) % DRUM_LANES;
            if (repitchNote(p, step, ev[field].pitch, DRUM_LANE_ID[lane])) break;
          }
          return;
        }
        const int np = ev[field].pitch + (lr ? dir : dir * 12);
        if (np < 0 || np > 127 || !repitchNote(p, step, ev[field].pitch, (uint8_t)np)) { toast("Out of range"); return; }
        const uint8_t h = (uint8_t)np;
        player::previewStep(track, &h, 1, ev[field].len, ev[field].vel);   // hear it as it will play
        return;
      }
      case F_VEL:
        if (n && !off) setStepVelocity(p, step, (uint8_t)constrain(ev[0].vel + dir * (lr ? 1 : 10), 1, 127));
        return;
      case F_LEN: {
        if (!n || off) return;
        const uint16_t maxLen = (uint16_t)p.length * TICKS_PER_STEP;
        const uint16_t len = lr ? nextNoteLength(ev[0].len, dir > 0, maxLen)
                                : (uint16_t)constrain((int)ev[0].len + dir * 16 * TICKS_PER_STEP, TICKS_PER_STEP / 4, maxLen);
        setStepLength(p, step, len);
        entryLen = len;
        return;
      }
      case F_OFS:
        if (n && !off) setStepOffset(p, step, (int8_t)(ev[0].offset + dir * (lr ? 1 : 3)));
        return;
      case F_FXC:
        return;   // f / l choose the effect
      case F_FXV: {
        uint8_t cmd = FX_NONE, val = 0;
        if (!fxAt(p, step, &cmd, &val)) return;
        switch (cmd) {
          case FX_STRUM:
            if (lr) val = fxStepValue(cmd, val, dir, false);   // 1..6
            else val = (uint8_t)(val > 3 ? val - 3 : val + 3);  // down <-> up, same speed
            break;
          case FX_ARP: val = fxStepValue(cmd, val, dir, !lr); break;   // , / mode   ; . rate
          case FX_COND: val = fxStepValue(cmd, val, dir, !lr); break;
          case FX_RATCHET: val = fxStepValue(cmd, val, dir, false); break;
          default: {
            const int hi = cmd == FX_CHANCE ? 100 : 127;
            const int v = (int)val + dir * (lr ? 1 : 10);
            val = (uint8_t)(v < 0 ? 0 : v > hi ? hi : v);
            break;
          }
        }
        setFx(p, step, cmd, val);
        char msg[40];
        fxDescribe(cmd, val, msg, sizeof(msg));
        toast(msg);
        return;
      }
    }
  }

  void adjust(Track& tr, Pattern& p, uint8_t nc, int dir, bool big) {
    const uint8_t step = (uint8_t)row;
    NoteEvent ev[MAX_NOTES_PER_STEP];
    bool off;
    uint8_t n = stepNotes(p, step, ev, &off);
    switch (kindOf(field, nc)) {
      case F_NOTE: {
        if ((int)field >= n) return;
        int np = ev[field].pitch + dir * (big ? 12 : 1);
        if (np >= 0 && np <= 127 && repitchNote(p, step, ev[field].pitch, (uint8_t)np)) {
          const uint8_t h = (uint8_t)np;
          player::previewStep(track, &h, 1, ev[field].len, ev[field].vel);   // hear it as it will play
        }
        break;
      }
      case F_VEL:
        if (n) setStepVelocity(p, step, (uint8_t)constrain(ev[0].vel + dir * (big ? 16 : 1), 1, 127));
        break;
      case F_LEN: {
        uint16_t maxLen = (uint16_t)p.length * TICKS_PER_STEP;
        uint16_t cur = n ? ev[0].len : entryLen;
        uint16_t len = big ? (uint16_t)constrain((int)cur + dir * 16 * TICKS_PER_STEP,
                                                 TICKS_PER_STEP / 4, maxLen)
                           : nextNoteLength(cur, dir > 0, maxLen);
        if (n) setStepLength(p, step, len);
        entryLen = len;
        break;
      }
      case F_OFS:
        if (n) setStepOffset(p, step, (int8_t)(ev[0].offset + dir));
        break;
      case F_FXC:
      case F_FXV: {
        uint8_t cmd = FX_NONE, val = 0;
        fxAt(p, step, &cmd, &val);
        if (kindOf(field, nc) == F_FXC) {
          cmd = (uint8_t)((cmd + FX_COUNT + dir) % FX_COUNT);  // cycles through "-" too
          val = fxDefault(cmd);
        } else {
          if (cmd == FX_NONE) return;
          val = fxStepValue(cmd, val, dir, big);
        }
        setFx(p, step, cmd, val);
        char msg[40];
        fxDescribe(cmd, val, msg, sizeof(msg));
        toast(msg);
        break;
      }
    }
    (void)tr;
  }

  void del(Track& tr, Pattern& p, uint8_t nc) {
    const uint8_t step = (uint8_t)row;
    NoteEvent ev[MAX_NOTES_PER_STEP];
    bool off;
    uint8_t n = stepNotes(p, step, ev, &off);
    switch (kindOf(field, nc)) {
      case F_NOTE:
        if (off) clearStep(p, step);
        else if (n == 0) {
          if (tr.kind == TrackKind::Synth) setStepOff(p, step);
        } else if ((int)field < n) removeNote(p, step, ev[field].pitch);
        break;
      case F_VEL: setStepVelocity(p, step, settings.velocity); break;
      case F_LEN: setStepLength(p, step, TICKS_PER_STEP); break;
      case F_OFS: setStepOffset(p, step, 0); break;
      case F_FXC:
      case F_FXV: setFx(p, step, FX_NONE, 0); break;
    }
  }

  // ── Drawing ────────────────────────────────────────────────────────────────
  void drawRowNumbers(M5Canvas& c) {
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    for (int i = 0; i < VISIBLE_ROWS; i++) {
      int r = top + i, y = ROWS_TOP + i * ROW_H;
      if (r % 16 == 0) c.setTextColor(col::ACCENT);
      else if (r % 4 == 0) c.setTextColor(col::TEXT);
      else c.setTextColor(col::FAINT);
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X", r);
      c.drawString(buf, 1, y + 1);
    }
  }

  // Draws one track's columns starting at x; returns the next x.
  int drawTrack(M5Canvas& c, uint8_t t, int x0) {
    const Track& tr = song.tracks[t];
    const Pattern& p = tr.patterns[tr.pattern];
    const uint8_t nc = noteCols(p);
    const int w = trackWidth(nc);
    const bool cur = t == track;
    const int play = trackStep(t);

    // Header: name + pattern, then column labels.
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    if (cur) c.fillRect(x0, 15, w - GAP_W, 10, col::SELECT);
    c.setTextColor(cur ? col::HELD : (tr.mute ? col::FAINT : col::TEXT));
    char buf[16];
    snprintf(buf, sizeof(buf), "%d %s%s%s", t + 1, tr.name, tr.hold ? "~" : "", tr.mute ? " M" : "");
    c.drawString(buf, x0 + 1, 16);
    c.setFont(&fonts::TomThumb);
    c.setTextColor(col::DIM);
    int lx = x0;
    for (uint8_t k = 0; k < nc; k++, lx += NOTE_W) c.drawString(k == 0 ? "NOTE" : "+", lx + 1, 27);
    c.drawString("VL", lx + 1, 27);
    c.drawString("LEN", lx + VEL_W + 1, 27);
    c.drawString("OF", lx + VEL_W + LEN_W + 1, 27);
    c.drawString("FX", lx + VEL_W + LEN_W + OFS_W + 1, 27);

    c.setFont(&fonts::Font0);
    for (int i = 0; i < VISIBLE_ROWS; i++) {
      const int r = top + i, y = ROWS_TOP + i * ROW_H;
      if (r >= p.length) {
        c.fillRect(x0, y, w - GAP_W, ROW_H - 1, col::BG);
        continue;
      }
      uint16_t bg = (r == play) ? 0x2A69 : (r % 4 == 0 ? 0x18E4 : 0x10A3);
      c.fillRect(x0, y, w - GAP_W, ROW_H - 1, bg);

      NoteEvent ev[MAX_NOTES_PER_STEP];
      bool off;
      uint8_t n = stepNotes(p, (uint8_t)r, ev, &off);
      int x = x0;
      for (uint8_t k = 0; k < nc; k++, x += NOTE_W) {
        char s[6];
        uint16_t fg = col::FAINT;
        if (k == 0 && off && n == 0) {
          snprintf(s, sizeof(s), "OFF");
          fg = col::DIM;
        } else if (k < n) {
          if (tr.kind == TrackKind::Drum && player::drumPadForNote(ev[k].pitch) >= 0)
            snprintf(s, sizeof(s), "%s", player::drumShortName(player::drumPadForNote(ev[k].pitch)));
          else theory::formatNote(ev[k].pitch, s);
          fg = col::HELD;
        } else {
          snprintf(s, sizeof(s), "---");
        }
        cell(c, x, y, NOTE_W, s, fg, cur && r == row && field == k);
      }
      char s[6];
      if (n) snprintf(s, sizeof(s), "%02X", ev[0].vel);
      else snprintf(s, sizeof(s), "..");
      cell(c, x, y, VEL_W, s, n ? col::TEXT : col::FAINT, cur && r == row && field == nc);
      x += VEL_W;
      if (n) formatNoteLengthShort(ev[0].len, s, sizeof(s));
      else snprintf(s, sizeof(s), "..");
      cell(c, x, y, LEN_W, s, n ? (tr.hold ? col::DIM : col::TEXT) : col::FAINT,
           cur && r == row && field == nc + 1);
      x += LEN_W;
      if (n && ev[0].offset) snprintf(s, sizeof(s), "%+d", ev[0].offset);
      else snprintf(s, sizeof(s), n ? " 0" : "..");
      cell(c, x, y, OFS_W, s, n && ev[0].offset ? col::ACCENT : col::FAINT,
           cur && r == row && field == nc + 2);
      x += OFS_W;
      uint8_t cmd = FX_NONE, val = 0;
      bool hasFx = fxAt(p, (uint8_t)r, &cmd, &val);
      s[0] = hasFx ? fxLetter(cmd) : '.';
      s[1] = 0;
      cell(c, x, y, FXC_W, s, hasFx ? col::CHORD : col::FAINT, cur && r == row && field == nc + 3);
      x += FXC_W;
      if (hasFx) snprintf(s, sizeof(s), "%02X", val);
      else snprintf(s, sizeof(s), "..");
      cell(c, x, y, FXV_W, s, hasFx ? col::TEXT : col::FAINT, cur && r == row && field == nc + 4);
    }
    return x0 + w;
  }

  void cell(M5Canvas& c, int x, int y, int w, const char* s, uint16_t fg, bool selected) {
    if (selected) {
      c.fillRect(x, y, w - 1, ROW_H - 1, TFT_WHITE);
      fg = TFT_BLACK;
    }
    c.setTextColor(fg);
    c.setTextDatum(top_left);
    c.drawString(s, x + 1, y + 1);
  }
};

static TrackerScreen instance;
Screen* trackerScreen() { return &instance; }

}  // namespace ui
