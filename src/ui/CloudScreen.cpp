// Cloud: WiFi, signing in to trkr.one (email + password, or a link code),
// and song sync.
//   ; .  move     enter  choose
// WiFi setup asks for the network name and password with the keyboard
// (shift for capitals and symbols, del to erase, enter to confirm).
#include <stdio.h>
#include <string.h>

#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../net/Cloud.h"
#include "Screen.h"

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

  void onRawKey(uint8_t idx, bool pressed) override {
    if (mode == Mode::Menu || !pressed) return;
    const char* cap = layout::keyCapLabel(idx / layout::COLS, idx % layout::COLS);
    if (strcmp(cap, "del") == 0) {
      size_t n = strlen(field);
      if (n) field[n - 1] = 0;
    } else if (strcmp(cap, "ent") == 0) {
      confirmField();
    } else if (strcmp(cap, "spc") == 0) {
      append(' ');
    } else if (strlen(cap) == 1 && cap[0] != '`') {
      append(keys::isDown(IDX_SHIFT) ? shifted(cap[0]) : cap[0]);
    }
    markDirty();
  }

  void onAction(uint8_t a) override {
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
    if (mode != Mode::Menu) {
      c.setTextColor(col::ACCENT);
      c.drawString(mode == Mode::Ssid ? "WiFi network name:" : mode == Mode::Pass ? "WiFi password:"
                   : mode == Mode::Email ? "trkr.one email:" : "trkr.one password:", 8, 22);
      c.fillRect(6, 36, W - 12, 16, col::BAR);
      c.setTextColor(col::TEXT);
      char shown[72];
      size_t n = strlen(field);
      if (mode == Mode::Pass || mode == Mode::LoginPass) {
        for (size_t i = 0; i < n; i++) shown[i] = i + 1 == n ? field[i] : '*';
        shown[n] = 0;
      } else strcpy(shown, field);
      const char* vis = strlen(shown) > 36 ? shown + strlen(shown) - 36 : shown;
      c.drawString(vis, 10, 40);
      c.drawString("_", 10 + c.textWidth(vis), 40);
      c.setTextColor(col::DIM);
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
    snprintf(l0, sizeof(l0), "WiFi: %s", cloud::hasWifi() ? cloud::ssid() : "set up...");
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
    if (cloud::lastSyncMs()) {
      snprintf(buf, sizeof(buf), "last sync %lus ago", (unsigned long)((millis() - cloud::lastSyncMs()) / 1000));
      c.drawString(buf, 8, 104);
    }
    c.drawString(cloud::busy() ? "enter = cancel" : ";. move  enter choose", 8, H - 14);
  }

 private:
  enum class Mode : uint8_t { Menu, Ssid, Pass, Email, LoginPass };
  static constexpr int ITEMS = 5;
  Mode mode = Mode::Menu;
  int sel = 0;
  char field[65] = "";
  char newSsid[33] = "";
  char newEmail[64] = "";

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
      case 0:
        mode = Mode::Ssid;
        strncpy(field, cloud::ssid(), sizeof(field) - 1);
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
