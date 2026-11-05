// Cardputer MIDI Controller / Sequencer
// Runs on both M5Stack Cardputer and Cardputer ADV (board detected at boot).
#include <M5Cardputer.h>

#include "app/BootGuard.h"
#include "app/Settings.h"
#include "audio/Monitor.h"
#include "input/KeyInput.h"
#include "midi/MidiOut.h"
#include "play/LivePlayer.h"
#include "seq/Engine.h"
#include "ui/Ui.h"

using namespace bootguard;

#ifdef BOOT_DEBUG
#include <esp_system.h>
#define DBG(...) Serial.printf(__VA_ARGS__)
#else
#define DBG(...)
#endif

void setup() {
#ifdef BOOT_DEBUG
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 4000) delay(10);  // give the console time to attach
  delay(300);
  DBG("\n\n[boot] Cardputer MIDI debug build\n");
  DBG("[boot] reset reason: %d\n", (int)esp_reset_reason());
  DBG("[boot] free heap: %u\n", (unsigned)ESP.getFreeHeap());
  DBG("[boot] M5Cardputer.begin...\n");
#endif
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  DBG("[boot] M5Cardputer.begin done, board=%d (ADV=%d, Cardputer=%d)\n", (int)M5.getBoard(),
      (int)m5::board_t::board_M5CardputerADV, (int)m5::board_t::board_M5Cardputer);
  DBG("[boot] display %dx%d\n", (int)M5Cardputer.Display.width(), (int)M5Cardputer.Display.height());
  M5Cardputer.Display.setBrightness(120);

  // Each step is recorded so that if one crashes, the next start reports it
  // on screen and skips it where possible.
  bootguard::begin();
  showReportIfCrashed();

  stage(ST_SETTINGS);
  settingsLoad();
  seq::songInit();
  // The song you had open last time (or the one carried over from phase 2).
  if (!seq::songLoadNamed(settings.lastSong,
                          settings.lastSongOnSd ? seq::Storage::Sd : seq::Storage::Flash))
    seq::songLoadNamed("untitled");
  seq::engineBegin();

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
  DBG("[boot] setup finished, free heap: %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  M5Cardputer.update();  // scans the keyboard
  ui::tick();            // key events -> notes / screens, redraw when needed
  {
    seq::Lock l;
    midi::update();  // sends this pass's BLE MIDI packet
  }
#ifdef BOOT_DEBUG
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 2000) {
    lastBeat = millis();
    DBG("[alive] %lus, keys held: %u, heap: %u\n", (unsigned long)(millis() / 1000),
        (unsigned)M5Cardputer.Keyboard.keyList().size(), (unsigned)ESP.getFreeHeap());
  }
#endif
}
