// Connect: find a Bluetooth MIDI device that waits to be connected to, and
// connect to it. The Cardputer stays connectable by phones / computers at the
// same time, and MIDI goes to everything connected.
//
//   ; .      pick a row          enter (or , /)  do it
//   Rows: Scan, Disconnect / Reconnect, Reconnect at power-on, then devices found.
#include <stdio.h>
#include <string.h>

#include "../app/Settings.h"
#include "../input/KeyLayout.h"
#include "../midi/BleMidi.h"
#include "Screen.h"

namespace ui {

using blemidi::Found;
using blemidi::RemoteState;

static constexpr int FIXED = 3;  // Scan, Disconnect/Reconnect, Auto
static constexpr int MAX_FOUND = 16;
static constexpr int ROW_H = 10, TOP = 40;
static constexpr int VISIBLE = (H - TOP - 10) / ROW_H;

class ConnectScreen : public Screen {
 public:
  const char* title() const override { return "CONNECT"; }
  bool musicKeys() const override { return false; }  // enter selects here
  bool animating() const override { return true; }   // scan progress, connection state

  void onEnter() override {
    if (!settings.bleMidi) toast("Bluetooth MIDI is off: turn it on with fn+b");
  }

  void onAction(uint8_t a) override {
    const int total = FIXED + count;
    switch (a) {
      case layout::ACT_UP: sel = (sel + total - 1) % total; return;
      case layout::ACT_DOWN: sel = (sel + 1) % total; return;
      case layout::ACT_ENTER:
      case layout::ACT_LEFT:
      case layout::ACT_RIGHT: activate(); return;
      default: return;
    }
  }

  void draw(M5Canvas& c) override {
    count = blemidi::scanResults(devices, MAX_FOUND);
    const int total = FIXED + count;
    if (sel >= total) sel = total - 1;

    // Connection status.
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    char buf[48];
    const RemoteState st = blemidi::remoteState();
    uint16_t color = col::DIM;
    if (blemidi::remoteConnected()) {
      snprintf(buf, sizeof(buf), "Connected to %s", blemidi::remoteName());
      color = col::OK;
    } else if (st == RemoteState::Connecting) {
      snprintf(buf, sizeof(buf), "Connecting%s", dots());
      color = col::ACCENT;
    } else if (st == RemoteState::Failed) {
      snprintf(buf, sizeof(buf), "%s", blemidi::lastError());
      color = 0xF800;
    } else {
      snprintf(buf, sizeof(buf), "Not connected to a device");
    }
    c.setTextColor(color);
    c.drawString(buf, 4, 17);
    c.setTextColor(col::FAINT);
    c.drawString("Phones can still connect to it too.", 4, 28);

    if (sel < top) top = sel;
    if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;
    for (int i = 0; i < VISIBLE && top + i < total; i++) {
      const int r = top + i, y = TOP + i * ROW_H;
      const bool cur = r == sel;
      if (cur) c.fillRect(0, y - 1, W, ROW_H, col::SELECT);
      c.setTextDatum(top_left);
      c.setTextColor(cur ? col::TEXT : col::DIM);
      buf[0] = 0;
      const char* right = "";
      if (r == 0) {
        snprintf(buf, sizeof(buf), blemidi::scanning() ? "Scanning%s" : "Scan for devices",
                 blemidi::scanning() ? dots() : "");
      } else if (r == 1) {
        if (blemidi::remoteConnected()) snprintf(buf, sizeof(buf), "Disconnect");
        else if (settings.btAddr[0]) snprintf(buf, sizeof(buf), "Reconnect: %s", settings.btName);
        else snprintf(buf, sizeof(buf), "(no device remembered yet)");
      } else if (r == 2) {
        snprintf(buf, sizeof(buf), "Reconnect at power-on");
        right = settings.btAuto ? "on" : "off";
      } else {
        const Found& f = devices[r - FIXED];
        snprintf(buf, sizeof(buf), "%s", f.name);
        c.setTextColor(cur ? col::TEXT : (f.midi ? col::TEXT : col::DIM));
        static char rb[16];
        snprintf(rb, sizeof(rb), "%s%d", f.midi ? "MIDI  " : "", f.rssi);
        right = rb;
      }
      c.drawString(buf, 6, y);
      c.setTextDatum(top_right);
      c.setTextColor(cur ? col::HELD : col::DIM);
      c.drawString(right, W - 4, y);
    }
    if (count == 0 && !blemidi::scanning()) {
      c.setTextDatum(top_left);
      c.setTextColor(col::FAINT);
      c.drawString("Make it discoverable, then Scan.", 6, TOP + FIXED * ROW_H + 2);
    }
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    c.drawString("enter: do it   MIDI = has BLE MIDI", 2, H - 9);
  }

 private:
  Found devices[MAX_FOUND];
  uint8_t count = 0;
  int sel = 0, top = 0;

  static const char* dots() {
    static const char* const D[4] = {"", ".", "..", "..."};
    return D[(millis() / 300) % 4];
  }

  void activate() {
    if (!settings.bleMidi) {
      toast("Turn Bluetooth MIDI on first (fn+b)");
      return;
    }
    if (sel == 0) {
      blemidi::startScan(6);
    } else if (sel == 1) {
      if (blemidi::remoteConnected()) {
        blemidi::disconnectRemote();  // also stops auto-reconnect for this session
        toast("Disconnected");
      } else if (settings.btAddr[0]) {
        Found f{};
        strncpy(f.name, settings.btName, sizeof(f.name) - 1);
        strncpy(f.addr, settings.btAddr, sizeof(f.addr) - 1);
        f.addrType = settings.btAddrType;
        blemidi::connectTo(f);
      }
    } else if (sel == 2) {
      settings.btAuto = !settings.btAuto;
      applyBtAuto();
    } else {
      blemidi::connectTo(devices[sel - FIXED]);
    }
  }
};

static ConnectScreen instance;
Screen* connectScreen() { return &instance; }

}  // namespace ui
