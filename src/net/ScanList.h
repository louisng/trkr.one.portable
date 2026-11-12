#pragma once
// The list of WiFi networks found by a scan: one entry per name (the strongest
// signal wins), hidden networks left out, strongest first. Plain C++ so it can
// be tested on a PC.
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

namespace cloud {

struct Net {
  char ssid[33];
  int8_t rssi;   // dBm, e.g. -45 (strong) .. -90 (weak)
  bool open;     // no password needed
  uint8_t auth;  // WiFi security type code from the radio (0 open, 3 WPA2, 6 WPA3, 7 WPA2/3 ...)
};

static constexpr size_t MAX_NETS = 24;

inline void addNet(std::vector<Net>& v, const char* ssid, int rssi, bool open, uint8_t auth = 0) {
  if (!ssid || !ssid[0]) return;  // hidden network
  int8_t r = (int8_t)std::max(-127, std::min(0, rssi));
  for (auto& n : v) {
    if (strncmp(n.ssid, ssid, sizeof(n.ssid) - 1) == 0) {  // same name from another access point
      if (r > n.rssi) { n.rssi = r; n.open = open; n.auth = auth; }
      return;
    }
  }
  if (v.size() >= MAX_NETS) return;
  Net n{};
  strncpy(n.ssid, ssid, sizeof(n.ssid) - 1);
  n.rssi = r;
  n.open = open;
  n.auth = auth;
  v.push_back(n);
}

inline void sortNets(std::vector<Net>& v) {
  std::stable_sort(v.begin(), v.end(), [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
}

inline const char* authName(uint8_t a) {
  switch (a) {
    case 0: return "open";
    case 1: return "WEP";
    case 2: return "WPA";
    case 3: return "WPA2";
    case 4: return "WPA/WPA2";
    case 5: return "WPA2-Ent";
    case 6: return "WPA3";
    case 7: return "WPA2/WPA3";
    default: return "other";
  }
}

// 0-4 bars for a signal strength
inline int signalBars(int rssi) { return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0; }

}  // namespace cloud
