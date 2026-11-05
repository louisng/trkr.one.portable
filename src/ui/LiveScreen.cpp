// Live play: shows what you're playing and a live map of all 56 keys.
#include <stdio.h>

#include "../app/Settings.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../music/Theory.h"
#include "../play/LivePlayer.h"
#include "Screen.h"

namespace ui {

using layout::Role;

// Draw the 4 x 14 key map with each key's current function. Shared with the
// key-test screen (which shows key caps instead of functions).
void drawKeyMap(M5Canvas& c, int top, bool showCaps) {
  const int cw = 17, ch = 15, x0 = (W - cw * layout::COLS) / 2;
  c.setFont(&fonts::TomThumb);
  c.setTextDatum(middle_center);
  for (uint8_t r = 0; r < layout::ROWS; r++) {
    for (uint8_t k = 0; k < layout::COLS; k++) {
      uint8_t idx = r * layout::COLS + k;
      const layout::KeyDef& def = layout::keyAt(r, k);
      int x = x0 + k * cw, y = top + r * (ch + 1);

      uint16_t bg = col::ACTION, fg = col::TEXT;
      char label[6] = "";
      switch (def.role) {
        case Role::Drum:
          bg = col::DRUM;
          snprintf(label, sizeof(label), "%s", def.label);
          break;
        case Role::Chord:
          bg = col::CHORD;
          snprintf(label, sizeof(label), "%s",
                   theory::chordButtonLabel(settings.scale, def.value));
          break;
        case Role::Piano: {
          bool black = (def.value == 1 || def.value == 3 || def.value == 6 ||
                        def.value == 8 || def.value == 10);
          bg = black ? col::BLACK_KEY : col::WHITE_KEY;
          fg = black ? col::TEXT : TFT_BLACK;
          int n = player::pianoNoteFor(def.value);
          if (n >= 0) snprintf(label, sizeof(label), "%s", theory::noteName(n % 12));
          else bg = col::FAINT;
          break;
        }
        case Role::Action:
          if (def.value == layout::ACT_FREE) bg = col::FAINT;
          fg = col::DIM;
          snprintf(label, sizeof(label), "%s", def.label);
          break;
        default:
          bg = col::FAINT;
          break;
      }
      bool down = showCaps ? keys::isDown(idx) : (keys::isDown(idx) || player::keySounding(idx));
      if (down) {
        bg = col::HELD;
        fg = TFT_BLACK;
      }
      c.fillRoundRect(x + 1, y, cw - 2, ch, 2, bg);
      c.setTextColor(fg);
      c.drawString(showCaps ? layout::keyCapLabel(r, k) : label, x + cw / 2, y + ch / 2 + 1);
    }
  }
}

class LiveScreen : public Screen {
 public:
  const char* title() const override { return "LIVE"; }

  void onAction(uint8_t a) override {
    switch (a) {
      case layout::ACT_UP:
        settings.velocity = settings.velocity >= 117 ? 127 : settings.velocity + 10;
        break;
      case layout::ACT_DOWN:
        settings.velocity = settings.velocity <= 11 ? 1 : settings.velocity - 10;
        break;
      case layout::ACT_LEFT:
        settings.root = (settings.root + 11) % 12;
        break;
      case layout::ACT_RIGHT:
        settings.root = (settings.root + 1) % 12;
        break;
      case layout::ACT_ENTER:
      case layout::ACT_PLAY:
        toast("Sequencer arrives in phase 2");
        break;
      default:
        break;
    }
  }

  void draw(M5Canvas& c) override {
    // Big readout of the last thing played.
    c.setFont(&fonts::FreeSansBold12pt7b);
    c.setTextDatum(middle_left);
    c.setTextColor(col::HELD);
    const char* lp = player::lastPlayed();
    c.drawString(lp[0] ? lp : "play!", 8, 32);

    c.setFont(&fonts::Font0);
    c.setTextDatum(top_right);
    c.setTextColor(col::TEXT);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s %s", theory::noteName(settings.root),
             theory::scaleName(settings.scale));
    c.drawString(buf, W - 6, 20);
    c.setTextColor(col::DIM);
    snprintf(buf, sizeof(buf), "vel %d  inv %d", settings.velocity, player::inversion());
    c.drawString(buf, W - 6, 32);

    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    snprintf(buf, sizeof(buf), "piano %s  ch P%d C%d D%d",
             settings.pianoScaleMode ? "scale" : "chrom", settings.pianoCh, settings.chordCh,
             settings.drumCh);
    c.drawString(buf, 8, 48);

    drawKeyMap(c, 62, false);
  }
};

static LiveScreen instance;
Screen* liveScreen() { return &instance; }

}  // namespace ui
