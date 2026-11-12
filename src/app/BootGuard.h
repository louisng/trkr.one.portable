#pragma once
// Crash diagnostics for startup.
//
// Each startup step is recorded in RTC memory (which survives a crash reset).
// If the previous start crashed, the next start shows which step it was on
// and skips that step (where possible) so the device still comes up.
// A full power-off clears this and every step is tried again.
#include <stdint.h>

namespace bootguard {

enum Stage : uint8_t {
  ST_NONE = 0,
  ST_SETTINGS,
  ST_SPEAKER,
  ST_MIDI_USB,
  ST_MIDI_BLE,
  ST_KEYBOARD,
  ST_UI,
  ST_RUNNING,
  ST_COUNT
};

void begin();               // call right after M5Cardputer.begin()
void stage(Stage s);        // record + show "starting <step>"
bool skipped(Stage s);      // true if this step crashed last time and is skipped
const char* stageName(Stage s);
uint32_t heapAt(Stage s);   // free heap (bytes) when that step began / when startup finished
bool crashedLastBoot();
Stage crashedStage();
const char* resetReasonText();
void showReportIfCrashed();  // on-screen report; waits for a key (or 15 s)

}  // namespace bootguard
