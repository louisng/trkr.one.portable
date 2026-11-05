// Cardputer MIDI Controller / Sequencer
// Runs on both M5Stack Cardputer and Cardputer ADV (board detected at boot).
#include <M5Cardputer.h>

#include "app/BootGuard.h"
#include "app/Settings.h"
#include "audio/Monitor.h"
#include "input/KeyInput.h"
#include "midi/MidiOut.h"
#include "play/LivePlayer.h"
#include "ui/Ui.h"

using namespace bootguard;

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setBrightness(120);

  // Each step is recorded so that if one crashes, the next start reports it
  // on screen and skips it where possible.
  bootguard::begin();
  showReportIfCrashed();

  stage(ST_SETTINGS);
  settingsLoad();

  stage(ST_SPEAKER);
  if (skipped(ST_SPEAKER)) settings.speaker = false;
  monitor::begin();
  monitor::setVolume(settings.volume);
  monitor::setEnabled(settings.speaker);

  stage(ST_MIDI_USB);
  midi::beginUsb(settings.usbMidi && !skipped(ST_MIDI_USB));

  stage(ST_MIDI_BLE);
  if (!skipped(ST_MIDI_BLE)) midi::beginBle(settings.bleMidi);

  stage(ST_KEYBOARD);
  keys::begin();
  player::begin();

  stage(ST_UI);
  ui::begin();

  stage(ST_RUNNING);
}

void loop() {
  M5Cardputer.update();  // scans the keyboard
  ui::tick();            // key events -> notes / screens, redraw when needed
  midi::update();        // sends this pass's BLE MIDI packet
}
