#pragma once
// Turns the M5Cardputer key list into press / release events per physical key.
#include <stdint.h>

namespace keys {

struct Event {
  uint8_t index;  // row * 14 + col
  bool pressed;
};

void begin();
// Call after M5Cardputer.update(). Returns events since the last poll.
uint8_t poll(Event* out, uint8_t maxEvents);
bool isDown(uint8_t index);
uint64_t downMask();
uint8_t downCount();

}  // namespace keys
