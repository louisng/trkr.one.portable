#include "BootGuard.h"

#include <M5Cardputer.h>
#include <esp_attr.h>
#include <esp_system.h>

namespace bootguard {

static constexpr uint32_t MAGIC = 0xC0DE5EED;

// RTC memory that is not cleared on a crash/software reset.
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcStage;
RTC_NOINIT_ATTR static uint32_t rtcSkipMask;
RTC_NOINIT_ATTR static uint32_t rtcCrashCount;

static bool crashed = false;
static Stage crashStage = ST_NONE;
static esp_reset_reason_t reason = ESP_RST_UNKNOWN;
static int logY = 4;

static const char* const NAMES[ST_COUNT] = {
    "-", "settings", "speaker", "USB MIDI", "Bluetooth MIDI", "keyboard", "screen", "running",
};

const char* stageName(Stage s) { return s < ST_COUNT ? NAMES[s] : "?"; }

static bool isCrashReset(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
         r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
}

const char* resetReasonText() {
  switch (reason) {
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (low power)";
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "software reset";
    default: return "other";
  }
}

static void drawHeader() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  logY = 4;
  d.setCursor(4, logY);
  d.print("Cardputer MIDI - starting");
  logY += 12;
}

void begin() {
  reason = esp_reset_reason();
  if (rtcMagic != MAGIC || reason == ESP_RST_POWERON) {
    // Fresh power-up: RTC memory holds garbage. Try every step again.
    rtcMagic = MAGIC;
    rtcStage = ST_NONE;
    rtcSkipMask = 0;
    rtcCrashCount = 0;
  }
  Stage last = (Stage)(rtcStage < ST_COUNT ? rtcStage : ST_NONE);
  if (isCrashReset(reason) && last != ST_NONE) {
    crashed = true;
    crashStage = last;
    rtcCrashCount++;
    // Skip the step that crashed, if it's one we can live without.
    if (last == ST_SPEAKER || last == ST_MIDI_USB || last == ST_MIDI_BLE)
      rtcSkipMask |= 1u << last;
  }
  rtcStage = ST_NONE;

  M5Cardputer.Display.setRotation(1);
  drawHeader();
}

void stage(Stage s) {
  rtcStage = s;
  if (s == ST_RUNNING) return;
  auto& d = M5Cardputer.Display;
  d.setTextColor(skipped(s) ? TFT_ORANGE : TFT_DARKGREY, TFT_BLACK);
  d.setCursor(4, logY);
  d.printf("%s %s", skipped(s) ? "skip " : "start", stageName(s));
  logY += 10;
}

bool skipped(Stage s) { return (rtcSkipMask >> s) & 1u; }
bool crashedLastBoot() { return crashed; }
Stage crashedStage() { return crashStage; }

void showReportIfCrashed() {
  if (!crashed) return;
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_ORANGE, TFT_BLACK);
  d.setCursor(4, 4);
  d.print("The last start crashed.");
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(4, 22);
  d.printf("Step:   %s", stageName(crashStage));
  d.setCursor(4, 34);
  d.printf("Reason: %s", resetReasonText());
  d.setCursor(4, 46);
  d.printf("Crashes since power-on: %lu", (unsigned long)rtcCrashCount);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  int y = 64;
  if (crashStage == ST_SPEAKER || crashStage == ST_MIDI_USB || crashStage == ST_MIDI_BLE) {
    d.setCursor(4, y);
    d.printf("Skipping %s for now.", stageName(crashStage));
    y += 12;
  }
  d.setCursor(4, y);
  d.print("Power off/on to retry everything.");
  d.setCursor(4, 112);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.print("Take a photo of this, then press a key.");

  uint32_t until = millis() + 15000;
  // Wait for all keys up, then any key down.
  bool sawRelease = false;
  while ((int32_t)(millis() - until) < 0) {
    M5Cardputer.update();
    bool any = M5Cardputer.Keyboard.keyList().size() > 0;
    if (!any) sawRelease = true;
    else if (sawRelease) break;
    delay(10);
  }
  drawHeader();
}

}  // namespace bootguard
