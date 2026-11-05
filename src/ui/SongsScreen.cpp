// Songs: save, load and delete named songs.
//
// List mode:   ; .  move     enter  load / pick the row's action
//              s    save the current song over the selected one
//              del  delete the selected song
// Anything destructive asks for a second press.
// Naming mode: type a name (shift = capitals), del = backspace,
//              space = space, enter = save, tap ` = cancel.
#include <stdio.h>
#include <string.h>

#include "../app/Settings.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

using namespace seq;

static constexpr uint8_t IDX_SHIFT = 2 * 14 + 1;
static constexpr int ROW_H = 11, LIST_TOP = 18;
static constexpr int VISIBLE = (H - LIST_TOP - 12) / ROW_H;
static constexpr int FIXED_ROWS = 2;  // "new empty song", "save as new..."

enum Pending : uint8_t { P_NONE, P_LOAD, P_DELETE, P_OVERWRITE, P_NEW, P_SAVE_EXISTING };

class SongsScreen : public Screen {
 public:
  const char* title() const override { return "SONGS"; }
  bool musicKeys() const override { return false; }
  bool spaceIsPlay() const override { return false; }  // the keyboard types names here

  void onEnter() override {
    refresh();
    pending = P_NONE;
    if (!startNaming) naming = false;
    startNaming = false;
  }

  // From elsewhere (quick save with no name yet): open straight into naming.
  void openForSaveAs() {
    startNaming = true;
    beginSaveAs();
  }

  bool animating() const override { return naming || pending != P_NONE; }

  void beginSaveAs() {
    naming = true;
    strncpy(name, currentSongName(), sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
  }

  void onRawKey(uint8_t idx, bool pressed) override {
    if (!pressed) return;
    const char* cap = layout::keyCapLabel(idx / layout::COLS, idx % layout::COLS);
    if (strlen(cap) != 1) return;
    char ch = cap[0];
    if (naming) {
      bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
      if (!ok) return;
      if (ch >= 'a' && ch <= 'z' && keys::isDown(IDX_SHIFT)) ch = (char)(ch - 'a' + 'A');
      size_t n = strlen(name);
      if (n < SONG_NAME_MAX) {
        name[n] = ch;
        name[n + 1] = 0;
      }
    } else if (ch == 's' && sel >= FIXED_ROWS) {
      confirm(P_OVERWRITE, "Press s again to save over");
    }
  }

  void onAction(uint8_t a) override {
    if (naming) {
      namingAction(a);
      return;
    }
    const int total = FIXED_ROWS + (int)names.size();
    switch (a) {
      case layout::ACT_UP: sel = (sel + total - 1) % total; pending = P_NONE; break;
      case layout::ACT_DOWN: sel = (sel + 1) % total; pending = P_NONE; break;
      case layout::ACT_ENTER:
        if (sel == 0) confirm(P_NEW, "Press enter again: new empty song");
        else if (sel == 1) beginSaveAs();
        else confirm(P_LOAD, "Press enter again to load");
        break;
      case layout::ACT_DELETE:
        if (sel >= FIXED_ROWS) confirm(P_DELETE, "Press del again to delete");
        break;
      default: break;
    }
  }

  void draw(M5Canvas& c) override {
    c.setFont(&fonts::Font0);
    if (sel < top) top = sel;
    if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;
    const int total = FIXED_ROWS + (int)names.size();
    for (int i = 0; i < VISIBLE && top + i < total; i++) {
      const int r = top + i, y = LIST_TOP + i * ROW_H;
      if (r == sel) c.fillRect(0, y - 1, W, ROW_H, col::SELECT);
      c.setTextDatum(top_left);
      if (r == 0) {
        c.setTextColor(col::DIM);
        c.drawString("+ New empty song", 6, y);
      } else if (r == 1) {
        c.setTextColor(col::DIM);
        c.drawString("+ Save as new name...", 6, y);
      } else {
        const String& n = names[r - FIXED_ROWS];
        bool isCur = n == currentSongName();
        c.setTextColor(isCur ? col::HELD : col::TEXT);
        c.drawString(n.c_str(), 6, y);
        if (isCur) {
          c.setTextDatum(top_right);
          c.setTextColor(col::DIM);
          c.drawString("open", W - 6, y);
        }
      }
    }
    if (names.empty()) {
      c.setTextDatum(top_left);
      c.setTextColor(col::FAINT);
      c.drawString("No saved songs yet", 6, LIST_TOP + 2 * ROW_H + 4);
    }

    c.setTextDatum(top_left);
    if (pending != P_NONE && millis() < pendingUntil) {
      c.setTextColor(col::ACCENT);
      c.drawString(pendingMsg, 4, H - 10);
    } else {
      pending = P_NONE;
      c.setTextColor(col::DIM);
      c.drawString("enter: load  s: save over  del: delete", 4, H - 10);
    }
    if (naming) drawNaming(c);
  }

 private:
  std::vector<String> names;
  int sel = 1, top = 0;
  bool naming = false, startNaming = false;
  char name[SONG_NAME_MAX + 1] = "";
  Pending pending = P_NONE;
  int pendingSel = -1;
  uint32_t pendingUntil = 0;
  char pendingMsg[40] = "";

  void refresh() {
    names = songList();
    const int total = FIXED_ROWS + (int)names.size();
    if (sel >= total) sel = total - 1;
    // Start on the open song if there is one.
    for (size_t i = 0; i < names.size(); i++)
      if (names[i] == currentSongName()) sel = FIXED_ROWS + (int)i;
  }

  const char* selName() { return sel >= FIXED_ROWS ? names[sel - FIXED_ROWS].c_str() : ""; }

  // First press arms the action, a second press of the same key within 3 s does it.
  void confirm(Pending what, const char* msg) {
    if (pending == what && pendingSel == sel && millis() < pendingUntil) {
      pending = P_NONE;
      run(what);
      return;
    }
    pending = what;
    pendingSel = sel;
    pendingUntil = millis() + 3000;
    snprintf(pendingMsg, sizeof(pendingMsg), "%s", msg);
  }

  void remember(const char* n) {
    strncpy(settings.lastSong, n, sizeof(settings.lastSong) - 1);
    settings.lastSong[sizeof(settings.lastSong) - 1] = 0;
    settingsSave();
  }

  void stopEverything() {
    seq::stop();
    player::releaseAll();
  }

  void run(Pending what) {
    char msg[40];
    switch (what) {
      case P_NEW:
        stopEverything();
        songNew();
        remember("");
        toast("New empty song");
        break;
      case P_LOAD: {
        String n = selName();
        stopEverything();
        if (songLoadNamed(n.c_str())) {
          remember(n.c_str());
          snprintf(msg, sizeof(msg), "Loaded %s", n.c_str());
        } else {
          snprintf(msg, sizeof(msg), "Couldn't load %s", n.c_str());
        }
        toast(msg);
        break;
      }
      case P_OVERWRITE: {
        String n = selName();
        bool ok = songSaveAs(n.c_str());
        if (ok) remember(n.c_str());
        snprintf(msg, sizeof(msg), ok ? "Saved as %s" : "Save failed", n.c_str());
        toast(msg);
        break;
      }
      case P_DELETE: {
        String n = selName();
        snprintf(msg, sizeof(msg), songDelete(n.c_str()) ? "Deleted %s" : "Couldn't delete %s",
                 n.c_str());
        if (strcmp(settings.lastSong, n.c_str()) == 0) remember("");
        toast(msg);
        break;
      }
      case P_SAVE_EXISTING: saveNamed(); return;
      default: break;
    }
    refresh();
  }

  // ── Naming ─────────────────────────────────────────────────────────────────
  void namingAction(uint8_t a) {
    size_t n = strlen(name);
    switch (a) {
      case layout::ACT_DELETE:
        if (n) name[n - 1] = 0;
        pending = P_NONE;
        break;
      case layout::ACT_PLAY:  // space bar
        if (n && n < SONG_NAME_MAX && name[n - 1] != ' ') {
          name[n] = ' ';
          name[n + 1] = 0;
        }
        break;
      case layout::ACT_ENTER: {
        // Trailing spaces aren't allowed in names.
        while (n && name[n - 1] == ' ') name[--n] = 0;
        if (!validSongName(name)) {
          toast("Type a name first");
          return;
        }
        bool exists = false;
        for (auto& s : names) exists |= s == name;
        if (exists && strcmp(name, currentSongName()) != 0) {
          confirm(P_SAVE_EXISTING, "Name taken: enter again to replace");
          return;
        }
        saveNamed();
        break;
      }
      default: break;
    }
  }

  void saveNamed() {
    char msg[40];
    bool ok = songSaveAs(name);
    if (ok) remember(name);
    snprintf(msg, sizeof(msg), ok ? "Saved as %s" : "Save failed", name);
    toast(msg);
    naming = false;
    refresh();
  }

  void drawNaming(M5Canvas& c) {
    const int x = 16, y = 34, w = W - 32, h = 64;
    c.fillRoundRect(x, y, w, h, 5, col::BAR);
    c.drawRoundRect(x, y, w, h, 5, col::ACCENT);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::ACCENT);
    c.drawString("SAVE AS", x + 8, y + 6);
    c.fillRect(x + 8, y + 20, w - 16, 16, col::BG);
    c.setFont(&fonts::FreeSansBold9pt7b);
    c.setTextColor(col::HELD);
    c.setTextDatum(middle_left);
    c.drawString(name, x + 12, y + 28);
    int cx = x + 12 + c.textWidth(name) + 1;
    if ((millis() / 400) % 2 == 0) c.fillRect(cx, y + 22, 2, 12, col::HELD);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    c.drawString("type a name  enter: save", x + 8, y + 40);
    c.drawString("del: erase   tap `: cancel", x + 8, y + 50);
  }
};

static SongsScreen instance;
Screen* songsScreen() { return &instance; }
void songsStartSaveAs() { instance.openForSaveAs(); }

}  // namespace ui
