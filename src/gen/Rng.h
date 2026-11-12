#pragma once
// Seeded random numbers, bit-for-bit the same as trkr's public/lib/rng.js
// (mulberry32 + FNV-1a forks), so a seed makes the same song on the device
// and on the website.
#include <math.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace gen {

inline uint32_t hashString(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h;
}

// JavaScript's Math.round: halves round up (towards +infinity).
inline double jsRound(double x) { return floor(x + 0.5); }

struct Rng {
  uint32_t a;
  explicit Rng(uint32_t seed = 1) : a(seed ? seed : 0x9e3779b9u) {}

  double next() {
    a += 0x6d2b79f5u;
    uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (double)(t ^ (t >> 14)) / 4294967296.0;
  }
  double flt(double lo, double hi) { return lo + next() * (hi - lo); }
  int integer(int lo, int hi) { return lo + (int)floor(next() * (hi - lo + 1)); }  // inclusive
  bool chance(double p) { return next() < p; }
  size_t pickIndex(size_t n) { return (size_t)floor(next() * n); }
  template <class T>
  const T& pick(const std::vector<T>& v) { return v[pickIndex(v.size())]; }
  // Index of the chosen item, given weights.
  size_t weighted(const std::vector<double>& w) {
    double total = 0;
    for (double x : w) total += x;
    double r = next() * total;
    for (size_t i = 0; i < w.size(); i++) {
      r -= w[i];
      if (r < 0) return i;
    }
    return w.size() - 1;
  }
  double gauss(double sd) {
    double u = 1 - next();
    double v = next();
    return sqrt(-2 * log(u)) * cos(2 * M_PI * v) * sd;
  }
  Rng fork(const std::string& label) const { return Rng(a ^ hashString(label)); }
};

}  // namespace gen
