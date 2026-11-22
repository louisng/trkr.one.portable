// The arpeggiator: the same answers as trkr.one's arp.js (test/arp.test.mjs).
//   arp_test
#include <stdio.h>
#include <string>
#include "../../src/play/Arp.h"
using namespace arp;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } } while (0)
static std::string seq(const uint8_t* notes, uint8_t n, uint8_t mode, uint8_t oct) {
  uint8_t out[MAX_SEQ];
  uint8_t c = sequence(notes, n, mode, oct, out);
  std::string s;
  for (uint8_t i = 0; i < c; i++) s += (i ? " " : "") + std::to_string(out[i]);
  return s;
}

int main() {
  const uint8_t ceg[] = {64, 60, 67};
  CHECK(seq(ceg, 3, UP, 1) == "60 64 67", "up");
  CHECK(seq(ceg, 3, DOWN, 1) == "67 64 60", "down");
  CHECK(seq(ceg, 3, UPDOWN, 1) == "60 64 67 64", "up/down: ends once");
  CHECK(seq(ceg, 3, INCLUSIVE, 1) == "60 64 67 67 64 60", "up+down: ends twice");
  CHECK(seq(ceg, 3, ORDER, 1) == "64 60 67", "as played");
  const uint8_t ce[] = {60, 64}, ec[] = {64, 60}, hi[] = {120};
  CHECK(seq(ce, 2, UP, 2) == "60 64 72 76", "2 octaves");
  CHECK(seq(ce, 2, DOWN, 3) == "88 84 76 72 64 60", "3 octaves down");
  CHECK(seq(ec, 2, ORDER, 2) == "64 60 76 72", "order over octaves");
  CHECK(seq(hi, 1, UP, 4) == "120", "nothing above 127");
  CHECK(seq(ce, 2, UPDOWN, 1) == "60 64", "two notes up/down");

  // the A effect's byte = arp.js encodeArp
  CHECK(encode(DOWN, 1, 2) == (1 | (1 << 3) | (1 << 6)), "encode");
  uint8_t m, r, o;
  CHECK(decode(encode(DOWN, 1, 2), &m, &r, &o) && m == DOWN && r == 1 && o == 2, "decode");
  CHECK(decode(0, &m, &r, &o) && m == UP && r == 0 && o == 1, "0 is up 1/4 1 oct");
  CHECK(!decode(7, &m, &r, &o) && !decode(6 << 3, &m, &r, &o), "invalid bytes");
  CHECK(rateTicks(3) == 12 && rateTicks(5) == 6 && rateTicks(2) == 16 && rateTicks(0) == 48, "ticks per rate");
  // the same random choices as the website (arp.js arpRandomIndex, k = 0..7, 100, 12345; n = 4 and 7)
  const uint32_t ks[10] = {0, 1, 2, 3, 4, 5, 6, 7, 100, 12345};
  const char* web = "2:3 0:6 3:3 1:4 3:0 2:4 0:0 2:1 1:4 2:1";
  std::string got;
  for (int i = 0; i < 10; i++) got += (i ? " " : "") + std::to_string(randomIndex(ks[i], 4)) + ":" + std::to_string(randomIndex(ks[i], 7));
  CHECK(got == web, "random matches trkr.one");
  if (got != web) printf("    got %s\n", got.c_str());

  // live: hold, release, latch, a fresh chord replaces it
  Live a;
  uint8_t out[16];
  a.press(60); a.press(64);
  CHECK(a.step(out) == 1 && out[0] == 60 && a.step(out) == 1 && out[0] == 64 && a.step(out) == 1 && out[0] == 60, "walks the notes");
  a.release(60); a.release(64);
  CHECK(!a.running() && a.step(out) == 0, "stops when you let go");
  a.setHold(true);
  a.press(60); a.press(64); a.release(60); a.release(64);
  CHECK(a.running(), "HOLD keeps going");
  a.press(62); a.press(65); a.release(62); a.release(65);
  a.step(out);
  CHECK(out[0] == 62, "a fresh chord replaces the held one");
  a.setHold(false);
  CHECK(!a.running(), "HOLD off lets go");
  Live c;
  c.mode = CHORD; c.octaves = 2;
  c.press(60); c.press(64);
  CHECK(c.step(out) == 2 && out[0] == 60 && c.step(out) == 2 && out[0] == 72, "chord, an octave up each time");
  Live s;
  s.swing = 75; s.gate = 50; s.rate = 3;
  s.press(60);
  s.step(out);
  CHECK(s.swingTicks() == 0 && s.gateTicks() == 6, "1st of a pair on time, half a step long");
  s.step(out);
  CHECK(s.swingTicks() == 6, "2nd pushed half a step by 75% swing");

  printf("%s\n", failures ? "FAILED" : "arp: all ok");
  return failures != 0;
}
