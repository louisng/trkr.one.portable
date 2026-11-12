// Arrange: the order scenes play in when song mode is on (like 1,1,4,3,2).
//
//   type a scene number, enter   set this row and move to the next
//                                (on the empty last row: adds a row)
//   f / l       scene number down / up
//   del         erase typed digits, else remove the row
//   ; .         move           '   song mode on / off
//   space       play / stop: in song mode, plays from the selected row
#include <stdio.h>
#include <string.h>

#include "../input/KeyLayout.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

using namespace seq;

static constexpr int ROW_H = 10, TOP = 30;
static constexpr int VISIBLE = (H - TOP - 10) / ROW_H;

class ArrangeScreen : public Screen {
 public:
  const char* title() const override { return "ARRANGE"; }
  bool musicKeys() const override { return false; }  // number keys type scene numbers

  void onEnter() override { typedLen = 0; }

  void onRawKey(uint8_t idx, bool pressed) override {
    if (!pressed) return;
    const char* cap = layout::keyCapLabel(idx / layout::COLS, idx % layout::COLS);
    if (strlen(cap) != 1 || cap[0] < '0' || cap[0] > '9') return;
    if (typedLen >= 2) typedLen = 0;  // a third digit starts a new number
    typed[typedLen++] = cap[0];
    typed[typedLen] = 0;
  }

  void onAction(uint8_t a) override {
    auto& arr = song.arrange;
    const int rows = (int)arr.size();  // plus one empty row at the end for adding
    switch (a) {
      case layout::ACT_UP: commit(); row = (row + rows) % (rows + 1); break;
      case layout::ACT_DOWN: commit(); row = (row + 1) % (rows + 1); break;
      case layout::ACT_ENTER:
        if (commit() || row < rows) row = row < (int)arr.size() ? row + 1 : row;
        break;
      case layout::ACT_SHORTER:
      case layout::ACT_LONGER:
        if (row < rows) {
          int v = arr[row] + (a == layout::ACT_LONGER ? 1 : -1);
          arr[row] = (uint8_t)((v + SCENES) % SCENES);
        }
        break;
      case layout::ACT_DELETE:
        if (typedLen) {
          typed[--typedLen] = 0;
        } else if (row < rows && rows > 1) {
          arr.erase(arr.begin() + row);
          if (row >= (int)arr.size()) row = (int)arr.size() - 1;
        }
        break;
      case layout::ACT_OPTIONS:
        song.songMode = !song.songMode;
        toast(song.songMode ? "Song mode: plays the arrangement" : "Pattern mode: tracks loop");
        break;
      default: break;
    }
    setSongStart((uint8_t)(row < (int)arr.size() ? row : 0));
  }

  void draw(M5Canvas& c) override {
    auto& arr = song.arrange;
    const int rows = (int)arr.size() + 1;
    if (row >= rows) row = rows - 1;
    if (row < top) top = row;
    if (row >= top + VISIBLE) top = row - VISIBLE + 1;
    const int playing = songPosition();

    // Mode banner.
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.fillRoundRect(4, 16, 74, 11, 3, song.songMode ? col::OK : col::FAINT);
    c.setTextColor(song.songMode ? TFT_BLACK : col::DIM);
    c.drawString(song.songMode ? "SONG MODE" : "PATTERNS", 8, 18);
    c.setTextColor(col::DIM);
    uint32_t steps = 0;
    for (uint8_t sc : arr) steps += sceneSteps(sc);
    char buf[48];
    snprintf(buf, sizeof(buf), "%d rows  %lu bars", (int)arr.size(), (unsigned long)(steps / 16));
    c.drawString(buf, 86, 18);

    for (int i = 0; i < VISIBLE && top + i < rows; i++) {
      const int r = top + i, y = TOP + i * ROW_H;
      const bool cur = r == row, isEnd = r == (int)arr.size();
      if (cur) c.fillRect(0, y - 1, W, ROW_H, col::SELECT);
      if (r == playing) c.fillTriangle(2, y, 2, y + 6, 6, y + 3, col::OK);
      c.setTextDatum(top_left);
      c.setTextColor(col::FAINT);
      snprintf(buf, sizeof(buf), "%02d", r + 1);
      c.drawString(buf, 10, y);
      if (isEnd) {
        c.setTextColor(cur ? col::TEXT : col::FAINT);
        c.drawString(cur && typedLen ? "" : "+ type a scene number", 34, y);
      } else {
        const uint8_t sc = arr[r];
        snprintf(buf, sizeof(buf), "scene %02d", sc + 1);
        c.setTextColor(r == playing ? col::OK : col::TEXT);
        c.drawString(buf, 34, y);
        // The scene's patterns at a glance.
        char pats[TRACKS * 2 + 1];
        for (uint8_t t = 0; t < TRACKS; t++) {
          uint8_t pt = song.scenes[sc].pattern[t];
          pats[t * 2] = patternChar(pt);
          pats[t * 2 + 1] = ' ';
        }
        pats[TRACKS * 2] = 0;
        c.setTextColor(col::DIM);
        c.drawString(pats, 98, y);
        snprintf(buf, sizeof(buf), "%d", sceneSteps(sc));
        c.setTextDatum(top_right);
        c.drawString(buf, W - 4, y);
      }
      if (cur && typedLen) {
        c.setTextDatum(top_left);
        c.fillRect(32, y - 1, 56, ROW_H, col::ACCENT);
        c.setTextColor(TFT_BLACK);
        snprintf(buf, sizeof(buf), "scene %s_", typed);
        c.drawString(buf, 34, y);
      }
    }
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    c.drawString("type no. + enter  del: remove  ': mode", 2, H - 9);
  }

 private:
  int row = 0, top = 0;
  char typed[3] = "";
  uint8_t typedLen = 0;

  // Put the typed number into the current row (adds a row on the end row).
  // Returns true if something was written.
  bool commit() {
    if (!typedLen) return false;
    int n = atoi(typed);
    typedLen = 0;
    typed[0] = 0;
    if (n < 1 || n > SCENES) {
      toast("Scenes are 1-99");
      return false;
    }
    auto& arr = song.arrange;
    if (row >= (int)arr.size()) {
      if (arr.size() >= ARRANGE_MAX) {
        toast("Arrangement is full");
        return false;
      }
      arr.push_back((uint8_t)(n - 1));
    } else {
      arr[row] = (uint8_t)(n - 1);
    }
    return true;
  }
};

static ArrangeScreen instance;
Screen* arrangeScreen() { return &instance; }

}  // namespace ui
