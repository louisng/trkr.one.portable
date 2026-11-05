// Setup: MIDI transports, channels, key/scale, speaker.
// ; and . move the selection, , and / change the value, enter toggles.
#include <stdio.h>

#include "../app/Settings.h"
#include "../audio/Monitor.h"
#include "../input/KeyLayout.h"
#include "../midi/MidiOut.h"
#include "../music/Theory.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

enum Item : uint8_t {
  IT_BLE,
  IT_USB,
  IT_SPEAKER,
  IT_VOLUME,
  IT_VELOCITY,
  IT_BPM,
  IT_ROOT,
  IT_SCALE,
  IT_PIANO_MODE,
  IT_METRONOME,
  IT_QUANTIZE,
  IT_CLOCK,
  IT_COUNT
};

static const char* const LABELS[IT_COUNT] = {
    "Bluetooth MIDI", "USB MIDI", "Speaker",    "Speaker volume", "Velocity", "Tempo",
    "Key",            "Scale",    "Piano keys", "Metronome",      "Rec quantize",
    "MIDI clock out",
};
static constexpr int VISIBLE = 10;

static const char* linkText(midi::LinkState s) {
  switch (s) {
    case midi::LinkState::Connected: return "ON - connected";
    case midi::LinkState::Waiting: return "ON - waiting";
    default: return "off";
  }
}

static const char* const METRO[3] = {"off", "when recording", "always"};

class SetupScreen : public Screen {
 public:
  const char* title() const override { return "SETUP"; }

  void onAction(uint8_t a) override {
    switch (a) {
      case layout::ACT_UP: sel = (sel + IT_COUNT - 1) % IT_COUNT; break;
      case layout::ACT_DOWN: sel = (sel + 1) % IT_COUNT; break;
      case layout::ACT_LEFT: change(-1); break;
      case layout::ACT_RIGHT:
      case layout::ACT_ENTER: change(+1); break;
      default: break;
    }
  }

  void draw(M5Canvas& c) override {
    const int rowH = 10, top = STATUS_H + 2;
    if (sel < first) first = sel;
    if (sel >= first + VISIBLE) first = sel - VISIBLE + 1;
    c.setFont(&fonts::Font0);
    for (uint8_t i = first; i < IT_COUNT && i < first + VISIBLE; i++) {
      int y = top + (i - first) * rowH;
      if (i == sel) c.fillRect(0, y, W, rowH, col::SELECT);
      c.setTextDatum(top_left);
      c.setTextColor(i == sel ? col::TEXT : col::DIM);
      c.drawString(LABELS[i], 6, y + 1);
      char v[24];
      value((Item)i, v, sizeof(v));
      c.setTextDatum(top_right);
      c.setTextColor(i == sel ? col::HELD : col::TEXT);
      c.drawString(v, W - 6, y + 1);
    }
    c.setTextDatum(top_left);
    c.setTextColor(col::FAINT);
    c.drawString(isAdv() ? "Cardputer ADV" : "Cardputer", 6, H - 9);
    c.setTextDatum(top_right);
    char pos[12];
    snprintf(pos, sizeof(pos), "%d/%d", sel + 1, IT_COUNT);
    c.drawString(pos, W - 6, H - 9);
  }

 private:
  uint8_t sel = 0, first = 0;

  void value(Item it, char* out, size_t n) {
    switch (it) {
      case IT_BLE: snprintf(out, n, "%s", linkText(midi::bleState())); break;
      case IT_USB: snprintf(out, n, "%s", linkText(midi::usbState())); break;
      case IT_SPEAKER: snprintf(out, n, "%s", settings.speaker ? "ON" : "off"); break;
      case IT_VOLUME: snprintf(out, n, "%d", settings.volume); break;
      case IT_VELOCITY: snprintf(out, n, "%d", settings.velocity); break;
      case IT_BPM: snprintf(out, n, "%d bpm", seq::song.bpm); break;
      case IT_METRONOME: snprintf(out, n, "%s", METRO[settings.metronome % 3]); break;
      case IT_QUANTIZE: snprintf(out, n, "%s", settings.quantize ? "on (snap to steps)" : "off"); break;
      case IT_CLOCK: snprintf(out, n, "%s", settings.clockOut ? "ON (start/stop too)" : "off"); break;
      case IT_ROOT: snprintf(out, n, "%s", theory::noteName(settings.root)); break;
      case IT_SCALE: snprintf(out, n, "%s", theory::scaleName(settings.scale)); break;
      case IT_PIANO_MODE:
        snprintf(out, n, "%s", settings.pianoScaleMode ? "scale (white keys)" : "chromatic");
        break;
      default: out[0] = 0;
    }
  }

  void change(int d) {
    switch ((Item)sel) {
      case IT_BLE:
        settings.bleMidi = !settings.bleMidi;
        midi::setBleEnabled(settings.bleMidi);
        break;
      case IT_USB:
        settings.usbMidi = !settings.usbMidi;
        midi::setUsbEnabled(settings.usbMidi);
        break;
      case IT_SPEAKER:
        settings.speaker = !settings.speaker;
        monitor::setEnabled(settings.speaker);
        break;
      case IT_VOLUME: {
        int v = settings.volume + d * 16;
        settings.volume = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        monitor::setVolume(settings.volume);
        break;
      }
      case IT_VELOCITY: {
        int v = settings.velocity + d * 5;
        settings.velocity = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
        break;
      }
      case IT_BPM: {
        int b = seq::song.bpm + d;
        seq::song.bpm = (uint16_t)(b < seq::BPM_MIN ? seq::BPM_MIN : b > seq::BPM_MAX ? seq::BPM_MAX : b);
        break;
      }
      case IT_METRONOME: settings.metronome = (uint8_t)((settings.metronome + 3 + d) % 3); break;
      case IT_QUANTIZE: settings.quantize = !settings.quantize; break;
      case IT_CLOCK: settings.clockOut = !settings.clockOut; break;
      case IT_ROOT: settings.root = (uint8_t)((settings.root + 12 + d) % 12); break;
      case IT_SCALE:
        settings.scale = (uint8_t)((settings.scale + theory::SCALE_COUNT + d) % theory::SCALE_COUNT);
        break;
      case IT_PIANO_MODE: settings.pianoScaleMode = !settings.pianoScaleMode; break;
      default: break;
    }
  }
};

static SetupScreen instance;
Screen* setupScreen() { return &instance; }

}  // namespace ui
