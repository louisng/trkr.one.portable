// Scenes: which pattern each track plays, per scene (1-99).
//
//   ; .      pick a scene (hold opt: 10 at a time)
//   , /      pick a track
//   1-8      set the pattern     0 / - / del   silent in this scene
//   f / l    pattern down / up (through "--")
//   enter    apply this scene now (tracks switch to its patterns)
//   c        capture: copy the patterns tracks are on right now into this scene
//   space    play / stop
#include <stdio.h>
#include <string.h>

#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

using namespace seq;

static constexpr int ROW_H = 10, TOP = 28, LABEL_W = 30;
static constexpr int VISIBLE = (H - TOP - 10) / ROW_H;
static constexpr uint8_t IDX_OPT = 3 * 14 + 1;

class ScenesScreen : public Screen {
 public:
  const char* title() const override { return "SCENES"; }
  bool musicKeys() const override { return false; }  // number keys type patterns

  void onRawKey(uint8_t idx, bool pressed) override {
    if (!pressed) return;
    const char* cap = layout::keyCapLabel(idx / layout::COLS, idx % layout::COLS);
    if (strlen(cap) != 1) return;
    uint8_t& pt = song.scenes[scene].pattern[track];
    char ch = cap[0];
    if (ch >= '1' && ch <= '8') pt = (uint8_t)(ch - '1');
    else if (ch == '0' || ch == '-') pt = SCENE_SILENT;
    else if (ch == 'c') {
      for (uint8_t t = 0; t < TRACKS; t++) song.scenes[scene].pattern[t] = song.tracks[t].pattern;
      toast("Captured the current patterns");
    }
  }

  void onAction(uint8_t a) override {
    const int jump = keys::isDown(IDX_OPT) ? 10 : 1;
    uint8_t& pt = song.scenes[scene].pattern[track];
    switch (a) {
      case layout::ACT_UP: scene = (uint8_t)((scene + SCENES - jump % SCENES) % SCENES); break;
      case layout::ACT_DOWN: scene = (uint8_t)((scene + jump) % SCENES); break;
      case layout::ACT_LEFT: track = (uint8_t)((track + TRACKS - 1) % TRACKS); break;
      case layout::ACT_RIGHT: track = (uint8_t)((track + 1) % TRACKS); break;
      case layout::ACT_DELETE: pt = SCENE_SILENT; break;
      case layout::ACT_SHORTER:
      case layout::ACT_LONGER: {
        // Cycle --, 1 .. 9, A ...
        int v = pt >= PATTERNS ? PATTERNS : pt;  // "--" sits after the last pattern
        v = (v + (a == layout::ACT_LONGER ? 1 : PATTERNS)) % (PATTERNS + 1);
        pt = v == PATTERNS ? SCENE_SILENT : (uint8_t)v;
        break;
      }
      case layout::ACT_ENTER: {
        applyScene(scene);
        char msg[32];
        snprintf(msg, sizeof(msg), "Tracks now on scene %d", scene + 1);
        toast(msg);
        break;
      }
      default: break;
    }
  }

  void draw(M5Canvas& c) override {
    if (scene < top) top = scene;
    if (scene >= top + VISIBLE) top = scene - VISIBLE + 1;
    const int cw = (W - LABEL_W) / TRACKS;
    const int playing = songPosition() >= 0 ? song.arrange[songPosition()] : -1;

    // Track names.
    c.setFont(&fonts::TomThumb);
    c.setTextDatum(middle_center);
    for (uint8_t t = 0; t < TRACKS; t++) {
      bool sel = t == track;
      if (sel) c.fillRect(LABEL_W + t * cw, 16, cw - 1, 10, col::SELECT);
      c.setTextColor(sel ? col::HELD : col::DIM);
      c.drawString(song.tracks[t].name, LABEL_W + t * cw + cw / 2, 22);
    }

    c.setFont(&fonts::Font0);
    for (int i = 0; i < VISIBLE; i++) {
      const int sc = top + i;
      if (sc >= SCENES) break;
      const int y = TOP + i * ROW_H;
      const bool used = inArrangement((uint8_t)sc);
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d", sc + 1);
      c.setTextDatum(top_left);
      c.setTextColor(sc == playing ? col::OK : used ? col::TEXT : col::FAINT);
      if (sc == playing) c.fillTriangle(1, y + 1, 1, y + 7, 5, y + 4, col::OK);
      c.drawString(buf, 8, y + 1);
      for (uint8_t t = 0; t < TRACKS; t++) {
        const int x = LABEL_W + t * cw;
        const bool cur = sc == scene && t == track;
        uint16_t bg = cur ? TFT_WHITE : (sc == playing ? 0x2A69 : (i % 2 ? 0x10A3 : 0x18E4));
        c.fillRect(x, y, cw - 1, ROW_H - 1, bg);
        uint8_t pt = song.scenes[sc].pattern[t];
        if (pt < PATTERNS) snprintf(buf, sizeof(buf), "%c", patternChar(pt));
        else snprintf(buf, sizeof(buf), "--");
        c.setTextDatum(top_center);
        c.setTextColor(cur ? TFT_BLACK : pt < PATTERNS ? col::TEXT : col::FAINT);
        c.drawString(buf, x + cw / 2, y + 1);
      }
    }
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    char foot[48];
    snprintf(foot, sizeof(foot), "1-8/0 set  enter apply  c capture %dst", sceneSteps(scene));
    c.drawString(foot, 2, H - 9);
  }

 private:
  uint8_t scene = 0, track = 0;
  int top = 0;

  bool inArrangement(uint8_t sc) {
    for (uint8_t a : song.arrange)
      if (a == sc) return true;
    return false;
  }
};

static ScenesScreen instance;
Screen* scenesScreen() { return &instance; }

}  // namespace ui
