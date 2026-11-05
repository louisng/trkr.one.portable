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

}  // namespace ui
