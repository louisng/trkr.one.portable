// Setup: MIDI transports, channels, key/scale, speaker.
// ; and . move the selection, , and / change the value, enter toggles.
#include <stdio.h>

#include "../app/Settings.h"
#include "../audio/Monitor.h"
#include "../input/KeyLayout.h"
#include "../midi/MidiOut.h"
#include "../music/Theory.h"
#include "../play/LivePlayer.h"
#include "Screen.h"

namespace ui {

enum Item : uint8_t {
  IT_BLE,
  IT_USB,
  IT_SPEAKER,
  IT_VOLUME,
  IT_VELOCITY,
  IT_PIANO_CH,
  IT_CHORD_CH,
  IT_DRUM_CH,
  IT_ROOT,
  IT_SCALE,
  IT_PIANO_MODE,
  IT_COUNT
};

static const char* const LABELS[IT_COUNT] = {
    "Bluetooth MIDI", "USB MIDI",      "Speaker",    "Speaker volume",
    "Velocity",       "Piano channel", "Chord channel", "Drum channel",
    "Key",            "Scale",         "Piano keys",
};

static const char* linkText(midi::LinkState s) {
  switch (s) {
    case midi::LinkState::Connected: return "ON - connected";
    case midi::LinkState::Waiting: return "ON - waiting";
    default: return "off";
  }
}

static uint8_t stepCh(uint8_t ch, int d) { return (uint8_t)((ch - 1 + d + 16) % 16 + 1); }

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
    c.setFont(&fonts::Font0);
    for (uint8_t i = 0; i < IT_COUNT; i++) {
      int y = top + i * rowH;
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
  }

 private:
  uint8_t sel = 0;

  void value(Item it, char* out, size_t n) {
    switch (it) {
      case IT_BLE: snprintf(out, n, "%s", linkText(midi::bleState())); break;
      case IT_USB: snprintf(out, n, "%s", linkText(midi::usbState())); break;
      case IT_SPEAKER: snprintf(out, n, "%s", settings.speaker ? "ON" : "off"); break;
      case IT_VOLUME: snprintf(out, n, "%d", settings.volume); break;
      case IT_VELOCITY: snprintf(out, n, "%d", settings.velocity); break;
      case IT_PIANO_CH: snprintf(out, n, "%d", settings.pianoCh); break;
      case IT_CHORD_CH: snprintf(out, n, "%d", settings.chordCh); break;
      case IT_DRUM_CH: snprintf(out, n, "%d", settings.drumCh); break;
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
      // Changing a channel or the piano mode while notes are held would
      // strand them, so release everything first.
      case IT_PIANO_CH: player::releaseAll(); settings.pianoCh = stepCh(settings.pianoCh, d); break;
      case IT_CHORD_CH: player::releaseAll(); settings.chordCh = stepCh(settings.chordCh, d); break;
      case IT_DRUM_CH: player::releaseAll(); settings.drumCh = stepCh(settings.drumCh, d); break;
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
