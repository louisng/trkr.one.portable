#include "KeyInput.h"

#include <M5Cardputer.h>

#include "KeyLayout.h"

namespace keys {

static uint64_t prevMask = 0;

void begin() { prevMask = 0; }

static uint64_t readMask() {
  uint64_t mask = 0;
  for (const auto& p : M5Cardputer.Keyboard.keyList()) {
    if (p.x < 0 || p.y < 0 || p.x >= layout::COLS || p.y >= layout::ROWS) continue;
    mask |= 1ULL << (p.y * layout::COLS + p.x);
  }
  return mask;
}

uint8_t poll(Event* out, uint8_t maxEvents) {
  uint64_t now = readMask();
  uint64_t changed = now ^ prevMask;
  uint8_t n = 0;
  // Releases first so a quick re-press on a reused note stays balanced.
  for (uint8_t pass = 0; pass < 2; pass++) {
    bool wantPressed = (pass == 1);
    for (uint8_t i = 0; i < layout::KEY_COUNT && n < maxEvents; i++) {
      uint64_t bit = 1ULL << i;
      if (!(changed & bit)) continue;
      bool isPressed = (now & bit) != 0;
      if (isPressed != wantPressed) continue;
      out[n++] = {i, isPressed};
      prevMask ^= bit;  // only commit what we reported (overflow is re-reported next poll)
    }
  }
  return n;
}

bool isDown(uint8_t index) { return (prevMask >> index) & 1ULL; }
uint64_t downMask() { return prevMask; }
uint8_t downCount() { return (uint8_t)__builtin_popcountll(prevMask); }

}  // namespace keys
