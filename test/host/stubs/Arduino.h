// Minimal Arduino shim so the firmware's song code can be tested on a PC.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + o); }
  bool endsWith(const char* e) const {
    size_t n = strlen(e);
    return s.size() >= n && s.compare(s.size() - n, n, e) == 0;
  }
  String substring(int from, int to = -1) const {
    if (to < 0) to = (int)s.size();
    return String(s.substr((size_t)from, (size_t)(to - from)));
  }
  int lastIndexOf(char c) const {
    size_t p = s.rfind(c);
    return p == std::string::npos ? -1 : (int)p;
  }
  int compareTo(const String& o) const { return s.compare(o.s); }
  const char* c_str() const { return s.c_str(); }
  unsigned length() const { return (unsigned)s.size(); }
  bool operator==(const String& o) const { return s == o.s; }
};

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
