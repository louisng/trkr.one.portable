// Arp: the live arpeggiator's settings. fn+a turns it on or off from anywhere; while it is on, the
// piano and chord keys feed it and nothing is entered into the song.
//
//   ; .    pick a setting        , /    change it        keys   play through the arp
#include <stdio.h>

#include "../app/Settings.h"
#include "../input/KeyLayout.h"
#include "../play/Arp.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

enum ArpParam : uint8_t { A_ON, A_MODE, A_RATE, A_OCT, A_GATE, A_SWING, A_HOLD, A_COUNT };
static const char* const LABELS[A_COUNT] = {"Arp", "Mode", "Rate", "Octaves", "Gate", "Swing", "Hold"};

class ArpScreen : public Screen {
 public:
  const char* title() const override { return "ARP"; }

  void onAction(uint8_t a) override {
    switch (a) {
      case layout::ACT_UP: sel = (sel + A_COUNT - 1) % A_COUNT; break;
      case layout::ACT_DOWN: sel = (sel + 1) % A_COUNT; break;
      case layout::ACT_LEFT: change(-1); break;
      case layout::ACT_RIGHT: change(+1); break;
      default: break;
    }
  }

  void draw(M5Canvas& c) override {
    c.setFont(&fonts::Font0);
    const int rowH = 11, top = 18, listW = 140;
    char buf[24];
    for (int i = 0; i < A_COUNT; i++) {
      const int y = top + i * rowH;
      if (i == sel) c.fillRect(0, y - 1, listW, rowH, col::SELECT);
      c.setTextDatum(top_left);
      c.setTextColor(i == sel ? col::TEXT : col::DIM);
      c.drawString(LABELS[i], 4, y);
      value((ArpParam)i, buf, sizeof(buf));
      c.setTextDatum(top_right);
      c.setTextColor(i == sel ? col::HELD : col::TEXT);
      c.drawString(buf, listW - 3, y);
    }
    c.setTextDatum(top_left);
    c.setTextColor(player::arpEnabled() ? col::OK : col::FAINT);
    c.drawString(player::arpEnabled() ? "ON: keys play the arp" : "off (fn+a)", listW + 6, top);
    c.setTextColor(col::DIM);
    c.drawString("hold notes to", listW + 6, top + 14);
    c.drawString("hear it; tempo", listW + 6, top + 24);
    c.drawString("is the song's", listW + 6, top + 34);
    c.setTextColor(col::HELD);
    c.drawString(player::lastPlayed(), listW + 6, top + 50);
    c.setTextColor(col::FAINT);
    c.drawString("; . pick  , / change", 4, top + A_COUNT * rowH + 4);
    c.drawString("Arp steps: Tracker FX A", 4, top + A_COUNT * rowH + 14);
  }

  bool animating() const override { return player::arpEnabled(); }

 private:
  int sel = 0;

  void value(ArpParam p, char* out, size_t n) {
    switch (p) {
      case A_ON: snprintf(out, n, "%s", player::arpEnabled() ? "ON" : "off"); break;
      case A_MODE: snprintf(out, n, "%s", arp::modeName(settings.arpMode)); break;
      case A_RATE: snprintf(out, n, "%s", arp::rateName(settings.arpRate)); break;
      case A_OCT: snprintf(out, n, "%d", settings.arpOctaves); break;
      case A_GATE: snprintf(out, n, "%d%%", settings.arpGate); break;
      case A_SWING: snprintf(out, n, "%d%%", settings.arpSwing); break;
      case A_HOLD: snprintf(out, n, "%s", settings.arpHold ? "ON (latch)" : "off"); break;
      default: out[0] = 0;
    }
  }

  static uint8_t wrap(int v, int count) { return (uint8_t)(((v % count) + count) % count); }
  static uint8_t clampU(int v, int lo, int hi) { return (uint8_t)(v < lo ? lo : v > hi ? hi : v); }

  void change(int dir) {
    seq::Lock l;
    switch (sel) {
      case A_ON: player::setArp(!player::arpEnabled()); break;
      case A_MODE: settings.arpMode = wrap(settings.arpMode + dir, arp::MODE_COUNT); break;
      case A_RATE: settings.arpRate = wrap(settings.arpRate + dir, arp::RATE_COUNT); break;
      case A_OCT: settings.arpOctaves = clampU(settings.arpOctaves + dir, 1, 4); break;
      case A_GATE: settings.arpGate = clampU(settings.arpGate + dir * 5, 5, 100); break;
      case A_SWING: settings.arpSwing = clampU(settings.arpSwing + dir, 50, 75); break;
      case A_HOLD: settings.arpHold = !settings.arpHold; break;
      default: break;
    }
  }
};

static ArpScreen instance;
Screen* arpScreen() { return &instance; }

}  // namespace ui
