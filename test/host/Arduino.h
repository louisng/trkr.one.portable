// Minimal Arduino shim so src/song/Song.cpp can be tested on a PC.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
class Print {
 public:
  std::string out;
  void print(const char* s) { out += s; }
  void print(char c) { out += c; }
  void println(const char* s) { out += s; out += '\n'; }
  int printf(const char* fmt, ...) {
    char buf[1024];
    va_list a; va_start(a, fmt); int n = vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    out += buf; return n;
  }
};
class Stream : public Print {
 public:
  std::string in; size_t pos = 0;
  int read() { return pos < in.size() ? (unsigned char)in[pos++] : -1; }
};

// virtual clock for the sequencer test
extern uint32_t g_micros;
inline uint32_t micros() { return g_micros; }
