// Cloud: WiFi, signing in to trkr.one (email + password, or a link code),
// and song sync.
//   ; .  move     enter  choose
// WiFi: choose a network from a scan list (or type a hidden one's name).
// WiFi setup asks for the network name and password with the keyboard
// (shift for capitals and symbols, del to erase, enter to confirm).
#include <stdio.h>
#include <string.h>

#include <esp_heap_caps.h>

#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../net/Cloud.h"
#include "Screen.h"

extern uint32_t g_bootHeap[11];

namespace ui {

static constexpr uint8_t IDX_SHIFT = 2 * 14 + 1;

// Unshifted → shifted characters for the symbol keys.
static char shifted(char c) {
  static const char* from = "`1234567890-=[]\\;',./";
  static const char* to = "~!@#$%^&*()_+{}|:\"<>?";
  const char* p = strchr(from, c);
  if (p) return to[p - from];
  if (c >= 'a' && c <= 'z') return c - 32;
  return c;
}

class CloudScreen : public Screen {
 public:
  const char* title() const override { return "CLOUD"; }
  bool musicKeys() const override { return false; }

  void onEnter() override { mode = Mode::Menu; }
  void onLeave() override { cloud::endScan(); }

  // Polls the scan so the list appears by itself.
  void tick() override {
    if (mode != Mode::Scan) return;
    bool now = cloud::scanning();
    if (now || scanWas) markDirty();
    scanWas = now;
  }

  void onRawKey(uint8_t idx, bool pressed) override {
    if (!pressed) { eatEnter = false; return; }
    if (mode == Mode::Heap) { mode = Mode::Menu; return; }  // any key goes back
    if (mode == Mode::Menu) {
      const char* k = layout::keyCapLabel(idx / layout::COLS, idx % layout::COLS);
      if (strcmp(k, "h") == 0) { mode = Mode::Heap; markDirty(); }
      return;
    }
    if (mode == Mode::Scan) return;
    const char* cap = layout::keyCapLabel(idx / layout::COLS, idx % layout::COLS);
    if (strcmp(cap, "del") == 0) {
      size_t n = strlen(field);
      if (n) field[n - 1] = 0;
    } else if (strcmp(cap, "ent") == 0) {
      confirmField();
      eatEnter = true;  // this same key press must not also act on the menu
    } else if (strcmp(cap, "spc") == 0) {
      append(' ');
    } else if (strlen(cap) == 1 && cap[0] != '`') {
      append(keys::isDown(IDX_SHIFT) ? shifted(cap[0]) : cap[0]);
    }
    markDirty();
  }

  void onAction(uint8_t a) override {
    if (a == layout::ACT_ENTER && eatEnter) {  // already used to confirm a typed field
      eatEnter = false;
      return;
    }
    if (mode == Mode::Scan) {
      scanAction(a);
      markDirty();
      return;
    }
    if (mode != Mode::Menu) return;  // typed characters are handled in onRawKey
    if (a == layout::ACT_UP && sel > 0) sel--;
    if (a == layout::ACT_DOWN && sel < ITEMS - 1) sel++;
    if (a == layout::ACT_ENTER) choose();
    markDirty();
  }

  void draw(M5Canvas& c) override {
    if (cloud::busy()) markDirty();  // keep the status fresh
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    if (mode == Mode::Scan) {
      drawScan(c);
      return;
    }
    if (mode == Mode::Heap) {
      drawHeap(c);
      return;
    }
    if (mode != Mode::Menu) {
      c.setTextColor(col::ACCENT);
      c.drawString(mode == Mode::Ssid ? "WiFi network name:" : mode == Mode::Pass ? "WiFi password:"
                   : mode == Mode::Email ? "trkr.one email:" : "trkr.one password:", 8, 22);
      c.fillRect(6, 36, W - 12, 16, col::BAR);
      c.setTextColor(col::TEXT);
      char shown[72];
      size_t n = strlen(field);
      if (mode == Mode::LoginPass) {
        for (size_t i = 0; i < n; i++) shown[i] = i + 1 == n ? field[i] : '*';
        shown[n] = 0;
      } else strcpy(shown, field);
      const char* vis = strlen(shown) > 36 ? shown + strlen(shown) - 36 : shown;
      c.drawString(vis, 10, 40);
      c.drawString("_", 10 + c.textWidth(vis), 40);
      c.setTextColor(col::DIM);
      if (mode == Mode::Pass) {
        char nm[48];
        snprintf(nm, sizeof(nm), "for \"%.28s\"  (%d chars)", newSsid, (int)strlen(field));
        c.drawString(nm, 8, 82);
      }
      c.drawString("shift = capitals/symbols  del = erase", 8, 60);
      c.drawString("enter = OK    hold ` = cancel", 8, 72);
      return;
    }

    // Big code while linking
    if (cloud::state() == cloud::State::Linking && cloud::linkCode()[0]) {
      c.setTextColor(col::TEXT);
      c.drawString("On your phone or computer open", 8, 20);
      c.setTextColor(col::ACCENT);
      c.drawString(cloud::linkUrl(), 8, 32);
      c.setFont(&fonts::FreeSansBold18pt7b);
      c.setTextColor(col::HELD);
      c.setTextDatum(top_center);
      c.drawString(cloud::linkCode(), W / 2, 50);
      c.setFont(&fonts::Font0);
      c.setTextDatum(top_left);
      c.setTextColor(col::DIM);
      c.drawString("and enter this code.  enter = cancel", 8, H - 14);
      return;
    }

    char buf[48];
    const char* labels[ITEMS];
    char l0[40], l1[40];
    if (cloud::hasWifi()) snprintf(l0, sizeof(l0), "WiFi: %.20s (%d chars)", cloud::ssid(), cloud::passLen());
    else snprintf(l0, sizeof(l0), "WiFi: set up...");
    snprintf(l1, sizeof(l1), "%s", cloud::linked() ? cloud::email() : "Sign in with email");
    labels[0] = l0;
    labels[1] = l1;
    // Signed out: sign in with email, or link with a code from trkr.one/link.
    labels[2] = cloud::linked() ? "Link a different account (code)" : "...or link with a code";
    labels[3] = "Sync songs now";
    labels[4] = cloud::linked() ? "Sign out of this device" : "(not signed in)";
    for (int i = 0; i < ITEMS; i++) {
      int y = 20 + i * 14;
      if (i == sel) c.fillRect(0, y - 2, W, 13, col::SELECT);
      bool off = !cloud::linked() && (i == 3 || i == 4);
      c.setTextColor(off ? col::FAINT : i == 1 && cloud::linked() ? col::OK : col::TEXT);
      c.drawString(labels[i], 8, y);
    }
    c.setTextColor(cloud::state() == cloud::State::Failed ? col::DRUM
                   : cloud::busy() ? col::ACCENT : col::OK);
    c.drawString(cloud::message(), 8, 92);
    c.setTextColor(col::DIM);
    if (cloud::detail()[0]) {
      c.drawString(cloud::detail(), 8, 104);
    } else if (cloud::lastSyncMs()) {
      snprintf(buf, sizeof(buf), "last sync %lus ago", (unsigned long)((millis() - cloud::lastSyncMs()) / 1000));
      c.drawString(buf, 8, 104);
    }
    c.drawString(cloud::busy() ? "enter = cancel" : ";. move  enter choose  h memory", 8, H - 14);
  }

 private:
  enum class Mode : uint8_t { Menu, Ssid, Pass, Email, LoginPass, Scan, Heap };
  static constexpr int ITEMS = 5;
  Mode mode = Mode::Menu;
  int sel = 0;
  char field[65] = "";
  char newSsid[33] = "";
  char newEmail[64] = "";
  int scanSel = 0;
  bool scanWas = false;
  bool eatEnter = false;

  // ── WiFi network list ──
  // Rows: every network found, then "Other network..." for one that is hidden.
  int scanRows() const { return cloud::scanCount() + 1; }

  void scanAction(uint8_t a) {
    switch (a) {
      case layout::ACT_UP: if (scanSel > 0) scanSel--; break;
      case layout::ACT_DOWN: if (scanSel + 1 < scanRows()) scanSel++; break;
      case layout::ACT_PLAY:  // space: look again
        cloud::startScan();
        scanSel = 0;
        scanWas = true;
        break;
      case layout::ACT_DELETE:  // back
        cloud::endScan();
        mode = Mode::Menu;
        break;
      case layout::ACT_ENTER: {
        if (cloud::scanning()) break;
        if (scanSel >= cloud::scanCount()) {  // type a name
          cloud::endScan();
          mode = Mode::Ssid;
          strncpy(field, cloud::ssid(), sizeof(field) - 1);
          break;
        }
        const cloud::Net& n = cloud::scanNet(scanSel);
        strncpy(newSsid, n.ssid, sizeof(newSsid) - 1);
        newSsid[sizeof(newSsid) - 1] = 0;
        cloud::endScan();
        if (n.open) {
          cloud::setWifi(newSsid, "");
          mode = Mode::Menu;
          toast("WiFi saved (open network)");
        } else {
          field[0] = 0;
          mode = Mode::Pass;  // now the password
        }
        break;
      }
      default: break;
    }
  }

  // Free memory (KB) after each start-up step, and the cost of each step.
  void drawHeap(M5Canvas& c) {
    static const char* names[11] = {"", "start", "M5 + display", "rotate", "settings", "speaker synth",
                                    "midi (BLE+USB)", "keys", "files + cloud", "sequencer + gen", "screen buffer"};
    c.setTextColor(col::ACCENT);
    c.drawString("free KB at each start step", 6, 16);
    if (cloud::bleFreedLastKb()) {
      char bf[24];
      snprintf(bf, sizeof(bf), "BLE off:+%dk", cloud::bleFreedLastKb());
      c.setTextDatum(top_right);
      c.setTextColor(col::OK);
      c.drawString(bf, W - 4, 16);
      c.setTextDatum(top_left);
      c.setTextColor(col::TEXT);
    }
    c.setTextColor(col::TEXT);
    char line[48];
    for (int i = 1; i <= 10; i++) {
      long d = i > 1 ? (long)g_bootHeap[i] - (long)g_bootHeap[i - 1] : 0;
      snprintf(line, sizeof(line), "%-15s %4u", names[i], (unsigned)(g_bootHeap[i] / 1024));
      c.setTextColor(col::TEXT);
      c.drawString(line, 6, 26 + (i - 1) * 9);
      if (i > 1) {
        snprintf(line, sizeof(line), "%+ld", d / 1024);
        c.setTextColor(d < -8192 ? col::DRUM : col::DIM);
        c.drawString(line, 130, 26 + (i - 1) * 9);
      }
    }
    snprintf(line, sizeof(line), "now %u  block %u  min %u",
             (unsigned)(esp_get_free_heap_size() / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024),
             (unsigned)(esp_get_minimum_free_heap_size() / 1024));
    c.setTextColor(col::OK);
    c.drawString(line, 6, 26 + 10 * 9);
  }

  void drawScan(M5Canvas& c) {
    c.setTextColor(col::ACCENT);
    c.drawString("WiFi networks", 6, 17);
    bool busy = cloud::scanning();
    c.setTextDatum(top_right);
    c.setTextColor(col::DIM);
    c.drawString(busy ? "scanning..." : cloud::scanFailed() ? "scan failed" : "", W - 6, 17);
    c.setTextDatum(top_left);
    if (!busy && cloud::scanCount() == 0 && !cloud::scanFailed()) {
      c.setTextColor(col::DIM);
      c.drawString("none found", 6, 32);
    }
    const int rowH = 11, top = 30, rows = 7;
    int total = scanRows();
    if (scanSel >= total) scanSel = total - 1;
    int first = scanSel - rows + 1;
    if (first < 0) first = 0;
    for (int i = 0; i < rows && first + i < total; i++) {
      int idx = first + i;
      int y = top + i * rowH;
      bool isSel = idx == scanSel;
      if (isSel) c.fillRect(0, y - 1, W, rowH, col::SELECT);
      if (idx >= cloud::scanCount()) {
        c.setTextColor(isSel ? col::TEXT : col::DIM);
        c.drawString("Other network... (type its name)", 6, y);
        continue;
      }
      const cloud::Net& n = cloud::scanNet(idx);
      bool saved = cloud::hasWifi() && strcmp(cloud::ssid(), n.ssid) == 0;
      c.setTextColor(isSel ? col::TEXT : col::DIM);
      char label[30];
      snprintf(label, sizeof(label), "%s%.26s", saved ? "> " : "", n.ssid);
      c.drawString(label, 6, y);
      int bars = cloud::signalBars(n.rssi);
      for (int b = 0; b < 4; b++) {  // signal bars
        int h = 2 + b * 2;
        c.fillRect(W - 28 + b * 5, y + 8 - h, 3, h, b < bars ? col::OK : col::FAINT);
      }
      if (!n.open) {  // secured: a small padlock mark
        c.setTextDatum(top_right);
        c.setTextColor(col::ACCENT);
        c.drawString("*", W - 34, y);
        c.setTextDatum(top_left);
      }
    }
    c.setTextColor(col::FAINT);
    if (scanSel < cloud::scanCount()) {
      char info[40];
      snprintf(info, sizeof(info), "%s  %d dBm", cloud::authName(cloud::scanNet(scanSel).auth), cloud::scanNet(scanSel).rssi);
      c.setTextDatum(top_right);
      c.drawString(info, W - 4, H - 9);
      c.setTextDatum(top_left);
      c.drawString("enter ok spc rescan", 4, H - 9);
    } else {
      c.drawString("; . pick  enter ok  spc rescan  del back", 4, H - 9);
    }
  }

  void append(char ch) {
    size_t n = strlen(field);
    size_t max = mode == Mode::Ssid ? 32 : mode == Mode::Email ? 63 : 64;
    if (n < max) {
      field[n] = ch;
      field[n + 1] = 0;
    }
  }

  void confirmField() {
    if (mode == Mode::Ssid) {
      if (!field[0]) return;
      strcpy(newSsid, field);
      field[0] = 0;
      mode = Mode::Pass;
    } else if (mode == Mode::Email) {
      if (!strchr(field, '@')) { toast("That doesn't look like an email"); return; }
      strncpy(newEmail, field, sizeof(newEmail) - 1);
      field[0] = 0;
      mode = Mode::LoginPass;
    } else if (mode == Mode::LoginPass) {
      cloud::startLogin(newEmail, field);
      memset(field, 0, sizeof(field));  // don't keep the password around
      mode = Mode::Menu;
    } else {
      cloud::setWifi(newSsid, field);
      field[0] = 0;
      mode = Mode::Menu;
      toast("WiFi saved");
    }
  }

  void choose() {
    if (cloud::busy()) {
      cloud::cancel();
      return;
    }
    switch (sel) {
      case 0:  // choose a WiFi network from a scan
        cloud::startScan();
        scanSel = 0;
        scanWas = true;
        mode = Mode::Scan;
        break;
      case 1:  // sign in with email + password
        if (cloud::linked()) { toast("Signed in - sign out below to change"); break; }
        if (!cloud::hasWifi()) { toast("Set up WiFi first"); break; }
        mode = Mode::Email;
        strncpy(field, newEmail, sizeof(field) - 1);
        break;
      case 2:  // link with a code from trkr.one/link
        if (!cloud::hasWifi()) { toast("Set up WiFi first"); break; }
        cloud::startLink();
        break;
      case 3:
        if (!cloud::linked()) { toast("Sign in first"); break; }
        cloud::startSync();
        break;
      case 4:
        if (cloud::linked()) { cloud::unlink(); toast("Signed out"); }
        break;
    }
  }
};

static CloudScreen instance;
Screen* cloudScreen() { return &instance; }

}  // namespace ui
