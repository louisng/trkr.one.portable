// Cardputer MIDI Controller / Sequencer
// Runs on both M5Stack Cardputer and Cardputer ADV (board detected at boot).
#include <M5Cardputer.h>
#include <esp_attr.h>
#include <esp_system.h>

#include "app/Settings.h"
#include "audio/Monitor.h"
#include "input/KeyInput.h"
#include "midi/MidiOut.h"
#include "play/GenMode.h"
#include "net/Cloud.h"
#include "play/Sequencer.h"
#include "song/SongStore.h"
#include "play/LivePlayer.h"
#include "ui/Ui.h"

// ── boot diagnostics ─────────────────────────────────────────────────────────
// The chip's crash messages don't reach the USB port, so each boot step is
// written to memory that survives a reset. If the last boot ended in a crash,
// watchdog or brownout, the next boot shows how far it got and why, on screen.
static constexpr uint32_t RTC_MAGIC = 0xB007D1A6;
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcStage;
RTC_NOINIT_ATTR static uint32_t rtcBoots;
static inline void stage(uint32_t n) { rtcStage = n; }

static const char* const STAGE_NAMES[] = {
    "(none)",
    "M5 init: display/speaker/keys",
    "after M5 init",
    "settings load",
    "speaker synth",
    "MIDI: USB + Bluetooth",
    "keys + player",
    "storage + WiFi",
    "sequencer + generator",
    "screens setup",
    "setup finished",
    "loop: sequencer",
    "loop: screens + keys",
    "loop: MIDI",
};

static const char* resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "external reset";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT (power dip)";
    case ESP_RST_USB: return "usb";
    default: return "other";
  }
}

static void showBootProblem(esp_reset_reason_t why, uint32_t died, uint32_t boots) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::Font0);
  d.setTextSize(1);
  d.setTextColor(TFT_YELLOW);
  d.setCursor(4, 4);
  d.println("BOOT PROBLEM (last start failed)");
  d.setTextColor(TFT_WHITE);
  d.setCursor(4, 20);
  d.printf("why:   %s\n", resetName(why));
  d.setCursor(4, 32);
  d.printf("stage: %u  %s\n", (unsigned)died, died < sizeof(STAGE_NAMES) / sizeof(STAGE_NAMES[0]) ? STAGE_NAMES[died] : "?");
  d.setCursor(4, 44);
  d.printf("boots: %u   free heap: %u\n", (unsigned)boots, (unsigned)esp_get_free_heap_size());
  d.setCursor(4, 56);
  d.printf("board: %s\n", M5.getBoard() == m5::board_t::board_M5CardputerADV ? "Cardputer ADV" : "Cardputer");
  d.setTextColor(TFT_CYAN);
  d.setCursor(4, 80);
  d.println("Photo this screen and send it.");
  d.setCursor(4, 96);
  d.println("Any key = try starting normally.");
  Serial.printf("BOOT PROBLEM why=%s stage=%u boots=%u\n", resetName(why), (unsigned)died, (unsigned)boots);
}

void setup() {
  esp_reset_reason_t why = esp_reset_reason();
  bool valid = rtcMagic == RTC_MAGIC;
  uint32_t diedAt = valid ? rtcStage : 0;
  if (!valid || why == ESP_RST_POWERON) {
    rtcBoots = 0;
    diedAt = 0;
  }
  rtcMagic = RTC_MAGIC;
  rtcBoots++;
  bool crashed = valid && (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT ||
                           why == ESP_RST_WDT || why == ESP_RST_BROWNOUT);

  stage(1);
  auto cfg = M5.config();
  if (crashed && diedAt <= 2) {  // it died inside the M5 init last time: try again without the speaker and mic
    cfg.internal_spk = false;
    cfg.internal_mic = false;
  }
  M5Cardputer.begin(cfg, true);
  stage(2);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(120);

  if (crashed) {
    showBootProblem(why, diedAt, rtcBoots);
    for (;;) {  // wait for a key so the screen can be read
      M5Cardputer.update();
      if (M5Cardputer.Keyboard.isPressed()) break;
      delay(50);
    }
    M5Cardputer.Display.fillScreen(TFT_BLACK);
  }

  stage(3);
  settingsLoad();

  stage(4);
  monitor::begin();
  monitor::setVolume(settings.volume);
  monitor::setEnabled(settings.speaker);

  stage(5);
  midi::begin(settings.bleMidi, settings.usbMidi);
  stage(6);
  keys::begin();
  player::begin();
  stage(7);
#ifndef CMC_MINIMAL
  store::begin();
  cloud::begin();
#endif
  stage(8);
  seq::begin();
#ifndef CMC_MINIMAL
  genmode::begin();
#endif
  stage(9);
  ui::begin();
  stage(10);
}

void loop() {
  stage(11);
  M5Cardputer.update();  // scans the keyboard
  genmode::tick();       // builds / starts generated songs
  seq::update();         // song playback (before and after the redraw)
  stage(12);
  ui::tick();            // key events -> notes / screens, redraw when needed
  seq::update();
  stage(13);
  midi::update();        // sends this pass's BLE MIDI packet
}
