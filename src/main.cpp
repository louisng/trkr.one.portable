// Cardputer MIDI Controller / Sequencer
// Runs on both M5Stack Cardputer and Cardputer ADV (board detected at boot).
#include <M5Cardputer.h>

#include "app/Settings.h"
#include "audio/Monitor.h"
#include "input/KeyInput.h"
#include "midi/MidiOut.h"
#include "play/LivePlayer.h"
#include "ui/Ui.h"

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(120);

  settingsLoad();

  monitor::begin();
  monitor::setVolume(settings.volume);
  monitor::setEnabled(settings.speaker);

  midi::begin(settings.bleMidi, settings.usbMidi);
  keys::begin();
  player::begin();
  ui::begin();
}

void loop() {
  M5Cardputer.update();  // scans the keyboard
  ui::tick();            // key events -> notes / screens, redraw when needed
  midi::update();        // sends this pass's BLE MIDI packet
}
