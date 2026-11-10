#include <cstdio>
#include "../../src/net/ScanList.h"
using namespace cloud;
static int fails = 0;
static void expect(bool ok, const char* what) { if (!ok) { fails++; printf("  FAIL: %s\n", what); } }
int main() {
  std::vector<Net> v;
  addNet(v, "Home", -70, false);
  addNet(v, "Cafe", -50, true);
  addNet(v, "", -40, false);            // hidden: left out
  addNet(v, "Home", -55, false);        // same network, stronger access point
  addNet(v, "Phone", -80, false);
  addNet(v, "Home", -90, false);        // weaker duplicate: ignored
  expect(v.size() == 3, "three distinct visible networks");
  sortNets(v);
  expect(!strcmp(v[0].ssid, "Cafe") && !strcmp(v[1].ssid, "Home") && !strcmp(v[2].ssid, "Phone"), "strongest first");
  expect(v[1].rssi == -55, "duplicate keeps the strongest signal");
  expect(v[0].open && !v[1].open, "open flag kept");
  expect(signalBars(-40) == 4 && signalBars(-60) == 3 && signalBars(-70) == 2 && signalBars(-80) == 1 && signalBars(-95) == 0, "bars");
  // a 32-character name fits, an over-long one is cut, and the list is capped
  std::vector<Net> w;
  addNet(w, "12345678901234567890123456789012", -60, false);
  expect(strlen(w[0].ssid) == 32, "32-character name kept whole");
  for (int i = 0; i < 40; i++) { char n[16]; snprintf(n, sizeof n, "net%d", i); addNet(w, n, -60 - i, false); }
  expect(w.size() == MAX_NETS, "list is capped");
  printf(fails ? "FAILED (%d)\n" : "wifi scan list: all good\n", fails);
  return fails ? 1 : 0;
}
