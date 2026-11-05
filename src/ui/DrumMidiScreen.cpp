// Drum MIDI: what each drum sends over MIDI on the drum track.
//
// Two modes:
//   kit                 every drum on the track's channel, a note per drum
//                       (drum machines, DAW drum racks; GM notes by default)
//   a channel per drum  each drum on its own channel (e.g. one M8 instrument
//                       per drum), all on the same note by default
//
//   ; .    pick a row (hit a drum pad to jump to it)
//   , /    change the value / channel   (on the quick-setup rows: apply)
//   f / l  change the note (hold shift: an octave)
#include <stdio.h>

#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../music/Theory.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

using namespace seq;

static constexpr uint8_t IDX_SHIFT = 2 * 14 + 1;
enum Row : uint8_t { R_MODE, R_CHANNEL, R_GM, R_SPLIT, R_NOTE_ALL, R_FIXED };
static constexpr int ROWS = R_FIXED + DRUM_LANES;
static constexpr int ROW_H = 9, TOP = 27;
static constexpr int VISIBLE = (H - TOP - 10) / ROW_H;

static uint8_t stepCh(int ch, int d) { return (uint8_t)(((ch - 1 + d) % 16 + 16) % 16 + 1); }

class DrumMidiScreen : public Screen {
 public:
  const char* title() const override { return "DRUM MIDI"; }

  void onEnter() override {
    // Work on the selected track if it's a drum track, else the first one.
    if (song.tracks[selectedTrack()].kind != TrackKind::Drum) {
      uint8_t d = liveDrumTrack();
      if (song.tracks[d].kind == TrackKind::Drum) selectTrack(d);
    }
  }

  void onPlayed(uint8_t, const player::Played& p) override {
    if (p.pressed && p.role == layout::Role::Drum) sel = R_FIXED + p.pad;  // jump to the drum you hit
  }

  void onAction(uint8_t a) override {
    Track& tr = song.tracks[selectedTrack()];
    if (tr.kind != TrackKind::Drum) return;
    DrumRouting& r = tr.drums;
    const bool lanes = r.mode == DRUM_LANES_MODE;
    const bool shift = keys::isDown(IDX_SHIFT);
    int dir = 0;
    switch (a) {
      case layout::ACT_UP: sel = (sel + ROWS - 1) % ROWS; return;
      case layout::ACT_DOWN: sel = (sel + 1) % ROWS; return;
      case layout::ACT_LEFT: dir = -1; break;
      case layout::ACT_RIGHT: dir = 1; break;
      case layout::ACT_SHORTER: dir = -2; break;  // f / l: notes
      case layout::ACT_LONGER: dir = 2; break;
      default: return;
    }
    const bool noteKey = dir == 2 || dir == -2;
    const int d = dir > 0 ? 1 : -1;
    player::releaseAll();
    releaseTrack(selectedTrack());

    if (sel >= R_FIXED) {
      const uint8_t lane = sel - R_FIXED;
      if (noteKey) {
        uint8_t& n = lanes ? r.laneNote[lane] : r.kitNote[lane];
        n = (uint8_t)constrain((int)n + d * (shift ? 12 : 1), 0, 127);
      } else if (lanes) {
        r.laneCh[lane] = stepCh(r.laneCh[lane], d);
      } else {
        toast("Kit mode: all drums use the track channel");
      }
      return;
    }
    switch (sel) {
      case R_MODE:
        r.mode = lanes ? DRUM_KIT : DRUM_LANES_MODE;
        break;
      case R_CHANNEL:
        if (lanes) {  // first drum's channel; the rest follow in order
          uint8_t first = stepCh(r.laneCh[0], d);
          for (uint8_t i = 0; i < DRUM_LANES; i++) r.laneCh[i] = stepCh(first, i);
        } else {
          tr.channel = stepCh(tr.channel, d);
        }
        break;
      case R_GM:
        drumRoutingGmKit(r);
        tr.channel = 10;
        toast("Standard GM kit on channel 10");
        break;
      case R_SPLIT:
        drumRoutingPerLane(r, 1, 60);
        toast("Kick ch 1, snare ch 2 ... cowbell ch 12, all on C4");
        break;
      case R_NOTE_ALL: {
        if (!lanes) {
          toast("Only in channel-per-drum mode");
          break;
        }
        uint8_t n = (uint8_t)constrain((int)r.laneNote[0] + d * (shift ? 12 : 1), 0, 127);
        for (auto& x : r.laneNote) x = n;
        break;
      }
      default: break;
    }
  }

  void draw(M5Canvas& c) override {
    const uint8_t t = selectedTrack();
    const Track& tr = song.tracks[t];
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    if (tr.kind != TrackKind::Drum) {
      c.setTextColor(col::DIM);
      c.drawString("No drum track selected.", 6, 30);
      c.drawString("Set a track's Type to drums in", 6, 44);
      c.drawString("the ' options, then come back.", 6, 54);
      return;
    }
    const DrumRouting& r = tr.drums;
    const bool lanes = r.mode == DRUM_LANES_MODE;
    char buf[40];
    snprintf(buf, sizeof(buf), "%d %s", t + 1, tr.name);
    c.setTextColor(col::HELD);
    c.drawString(buf, 4, 16);
    c.setTextDatum(top_right);
    c.setTextColor(lanes ? col::OK : col::ACCENT);
    c.drawString(lanes ? "a channel per drum" : "kit on one channel", W - 4, 16);

    if (sel < top) top = sel;
    if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;
    for (int i = 0; i < VISIBLE && top + i < ROWS; i++) {
      const int row = top + i, y = TOP + i * ROW_H;
      const bool cur = row == sel;
      if (cur) c.fillRect(0, y - 1, W, ROW_H, col::SELECT);
      c.setTextDatum(top_left);
      c.setTextColor(cur ? col::TEXT : col::DIM);
      if (row < R_FIXED) {
        const char* label = "";
        buf[0] = 0;
        switch (row) {
          case R_MODE:
            label = "Mode";
            snprintf(buf, sizeof(buf), "%s", lanes ? "channel per drum" : "kit (one channel)");
            break;
          case R_CHANNEL:
            label = lanes ? "First drum's channel" : "Kit channel";
            snprintf(buf, sizeof(buf), "%d", lanes ? r.laneCh[0] : tr.channel);
            break;
          case R_GM: label = "Quick: GM kit, ch 10"; snprintf(buf, sizeof(buf), "press , or /"); break;
          case R_SPLIT: label = "Quick: ch 1-12, note C4"; snprintf(buf, sizeof(buf), "press , or /"); break;
          case R_NOTE_ALL: {
            label = "Same note for all";
            char nn[6];
            theory::formatNote(r.laneNote[0], nn);
            snprintf(buf, sizeof(buf), lanes ? "%s" : "-", nn);
            break;
          }
        }
        c.drawString(label, 4, y);
        c.setTextDatum(top_right);
        c.setTextColor(cur ? col::HELD : col::TEXT);
        c.drawString(buf, W - 4, y);
      } else {
        const uint8_t lane = row - R_FIXED;
        c.drawString(player::drumName(lane), 4, y);
        // Channel column, then note column.
        const uint8_t ch = lanes ? r.laneCh[lane] : tr.channel;
        const uint8_t note = lanes ? r.laneNote[lane] : r.kitNote[lane];
        snprintf(buf, sizeof(buf), "ch %2d", ch);
        c.setTextColor(lanes ? (cur ? col::HELD : col::TEXT) : col::FAINT);
        c.drawString(buf, 120, y);
        char nn[6];
        theory::formatNote(note, nn);
        snprintf(buf, sizeof(buf), "%-4s %3d", nn, note);
        c.setTextColor(cur ? col::HELD : col::TEXT);
        c.drawString(buf, 168, y);
      }
    }
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    c.drawString(sel >= R_FIXED ? ", / channel  f/l note  pads: jump" : ", / change  hit a pad to jump", 2,
                 H - 9);
  }

 private:
  int sel = 0, top = 0;
};

static DrumMidiScreen instance;
Screen* drumMidiScreen() { return &instance; }

}  // namespace ui
