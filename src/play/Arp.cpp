#include "Arp.h"

#include <string.h>

namespace arp {

static const char* const MODES[MODE_COUNT] = {"Up", "Down", "Up/Down", "Up+Down", "Order", "Random", "Chord"};
static const char* const RATES[RATE_COUNT] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const uint8_t TICKS[RATE_COUNT] = {48, 24, 16, 12, 8, 6};

const char* modeName(uint8_t m) { return m < MODE_COUNT ? MODES[m] : "?"; }
const char* rateName(uint8_t r) { return r < RATE_COUNT ? RATES[r] : "?"; }
uint8_t rateTicks(uint8_t r) { return TICKS[r < RATE_COUNT ? r : 3]; }

bool decode(uint8_t code, uint8_t* mode, uint8_t* rate, uint8_t* octaves) {
  const uint8_t m = code & 7, r = (code >> 3) & 7;
  if (m >= STEP_MODES || r >= RATE_COUNT) return false;
  if (mode) *mode = m;
  if (rate) *rate = r;
  if (octaves) *octaves = (uint8_t)((code >> 6) + 1);
  return true;
}
uint8_t encode(uint8_t mode, uint8_t rate, uint8_t octaves) {
  if (octaves < 1) octaves = 1;
  if (octaves > 4) octaves = 4;
  return (uint8_t)((mode & 7) | ((rate & 7) << 3) | ((octaves - 1) << 6));
}

uint8_t randomIndex(uint32_t k, uint8_t n) {
  if (!n) return 0;
  return (uint8_t)((((k + 1) * 1103515245u + 12345u) >> 16) % n);
}

uint8_t sequence(const uint8_t* notes, uint8_t n, uint8_t mode, uint8_t octaves, uint8_t* out) {
  uint8_t base[16];
  uint8_t nb = 0;
  for (uint8_t i = 0; i < n && nb < 16; i++) {   // without repeats, in press order
    bool dup = false;
    for (uint8_t j = 0; j < nb; j++) dup |= base[j] == notes[i];
    if (!dup) base[nb++] = notes[i];
  }
  if (!nb) return 0;
  if (mode != ORDER)   // everything else works from low to high
    for (uint8_t i = 1; i < nb; i++)
      for (uint8_t j = i; j > 0 && base[j - 1] > base[j]; j--) { uint8_t t = base[j]; base[j] = base[j - 1]; base[j - 1] = t; }
  uint8_t up[MAX_SEQ];
  uint8_t nu = 0;
  for (uint8_t o = 0; o < octaves; o++)
    for (uint8_t i = 0; i < nb && nu < MAX_SEQ / 2; i++)
      if (base[i] + 12 * o <= 127) up[nu++] = (uint8_t)(base[i] + 12 * o);
  if (mode != ORDER)   // octaves interleave when notes are spread out; keep it low to high
    for (uint8_t i = 1; i < nu; i++)
      for (uint8_t j = i; j > 0 && up[j - 1] > up[j]; j--) { uint8_t t = up[j]; up[j] = up[j - 1]; up[j - 1] = t; }
  uint8_t c = 0;
  switch (mode) {
    case DOWN:
      for (int i = nu - 1; i >= 0; i--) out[c++] = up[i];
      break;
    case UPDOWN:   // the ends once
      for (uint8_t i = 0; i < nu; i++) out[c++] = up[i];
      if (nu >= 3) for (int i = nu - 2; i >= 1; i--) out[c++] = up[i];
      break;
    case INCLUSIVE:   // the ends twice
      for (uint8_t i = 0; i < nu; i++) out[c++] = up[i];
      for (int i = nu - 1; i >= 0; i--) out[c++] = up[i];
      break;
    default:
      for (uint8_t i = 0; i < nu; i++) out[c++] = up[i];
      break;
  }
  return c;
}

void Live::press(uint8_t note) {
  bool isDown = false;
  for (uint8_t i = 0; i < nDown; i++) isDown |= down[i] == note;
  if (!isDown && nDown < 16) down[nDown++] = note;
  if (hold) {
    // every key joins the held notes, in the order pressed (del lets go of them all)
    bool have = false;
    for (uint8_t i = 0; i < nLatched; i++) have |= latched[i] == note;
    if (!have && nLatched < 16) latched[nLatched++] = note;
    if (nLatched == 1) { pos = 0; count = 0; }
  } else if (nDown == 1) {
    pos = 0;
    count = 0;
  }
}

uint8_t Live::pool(uint8_t* out) const {
  const uint8_t* src = hold ? latched : down;
  const uint8_t n = hold ? nLatched : nDown;
  memcpy(out, src, n);
  return n;
}

void Live::release(uint8_t note) {
  for (uint8_t i = 0; i < nDown; i++)
    if (down[i] == note) {
      memmove(down + i, down + i + 1, nDown - i - 1);
      nDown--;
      return;
    }
}

uint8_t Live::step(uint8_t* out) {
  const uint8_t* src = hold ? latched : down;
  const uint8_t n = hold ? nLatched : nDown;
  if (!n) return 0;
  count++;
  if (mode == CHORD) {   // the whole chord, an octave higher each time round
    const uint8_t o = (uint8_t)(pos % octaves);
    pos++;
    uint8_t c = 0;
    for (uint8_t i = 0; i < n; i++) if (src[i] + 12 * o <= 127) out[c++] = (uint8_t)(src[i] + 12 * o);
    return c;
  }
  uint8_t seq[MAX_SEQ];
  const uint8_t len = sequence(src, n, mode == RANDOM ? UP : mode, octaves, seq);
  if (!len) return 0;
  if (mode == RANDOM) { out[0] = seq[randomIndex(count - 1, len)]; return 1; }
  out[0] = seq[pos % len];
  pos = (uint16_t)((pos + 1) % len);
  return 1;
}

uint8_t Live::swingTicks() const {
  if (count % 2) return 0;   // the 2nd of each pair is late
  return (uint8_t)((uint32_t)(swing - 50) * rateTicks(rate) / 50);
}
uint8_t Live::gateTicks() const {
  const uint32_t g = (uint32_t)rateTicks(rate) * gate / 100;
  return (uint8_t)(g < 1 ? 1 : g);
}

}  // namespace arp
