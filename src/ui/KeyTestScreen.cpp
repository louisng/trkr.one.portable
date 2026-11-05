// Key test: shows raw key state so you can find out how many keys your unit
// can read at once, and which combinations "ghost" (light up keys you aren't
// pressing). Notes are muted while this screen is open.
#include <stdio.h>

#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "Screen.h"

namespace ui {

void drawKeyMap(M5Canvas& c, int top, bool showCaps);

class KeyTestScreen : public Screen {
 public:
  const char* title() const override { return "KEY TEST"; }
  bool musicKeys() const override { return false; }
  bool spaceIsPlay() const override { return false; }
  void onEnter() override {
    maxHeld = 0;
    events = 0;
  }
  void onRawKey(uint8_t, bool pressed) override {
    events++;
    if (pressed) {
      uint8_t n = keys::downCount();
      if (n > maxHeld) maxHeld = n;
    }
  }
  void draw(M5Canvas& c) override {
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::TEXT);
    char buf[48];
    snprintf(buf, sizeof(buf), "Held now: %d   Most at once: %d", keys::downCount(), maxHeld);
    c.drawString(buf, 8, 20);
    c.setTextColor(col::DIM);
    c.drawString("Hold several keys. Extra lit keys = ghosting.", 8, 33);
    c.drawString("Tap ` to go back.", 8, 45);
    drawKeyMap(c, 62, true);
  }

 private:
  uint8_t maxHeld = 0;
  uint32_t events = 0;
};

static KeyTestScreen instance;
Screen* keyTestScreen() { return &instance; }

}  // namespace ui
