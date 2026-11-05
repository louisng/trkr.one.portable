#pragma once
// The track / pattern options panel (opened with ' on the Step and Tracker
// screens). Works on the selected track.
#include <M5Cardputer.h>
#include <stddef.h>
#include <stdint.h>

namespace ui {
namespace options {

void open();
void close();
bool isOpen();
// Handles an action key while open. Returns true if the pattern changed
// (screens reset their entry state).
bool onAction(uint8_t action);
void draw(M5Canvas& c);

}  // namespace options

// Note lengths that f / l step through: 1/4, 1/2 step, then whole steps.
uint16_t nextNoteLength(uint16_t lenTicks, bool longer, uint16_t maxTicks);
// "1/4 step", "2 steps", ...
void formatNoteLength(uint16_t lenTicks, char* out, size_t n);
// Compact form for the tracker column: ".25" ".5" "1" "16" "1.5"
void formatNoteLengthShort(uint16_t lenTicks, char* out, size_t n);

// Tracker effects: one-letter name, default value, value stepping, and a
// plain-language description for the toast shown while editing.
char fxLetter(uint8_t cmd);
uint8_t fxDefault(uint8_t cmd);
uint8_t fxStepValue(uint8_t cmd, uint8_t val, int dir, bool big);
void fxDescribe(uint8_t cmd, uint8_t val, char* out, size_t n);

}  // namespace ui
