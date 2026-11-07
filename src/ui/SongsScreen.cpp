// Songs: pick a song saved on the device (made or synced from trkr.one),
// play it over MIDI / the speaker, and jam on top with the keyboard.
//   ; .  select      enter  load + play      space  play / stop
//   , /  tempo -/+   del    delete (press twice)
#include <stdio.h>
#include <string.h>

#include <vector>

#include "../input/KeyLayout.h"
#include "../net/Cloud.h"
#include "../play/Sequencer.h"
#include "../song/SongStore.h"
#include "Screen.h"

namespace ui {

class SongsScreen : public Screen {
 public:
  const char* title() const override { return "SONGS"; }

  void onEnter() override { refresh(); }
  void onLeave() override { saveIfEdited(); }

  void onAction(uint8_t a) override {
    switch (a) {
      case layout::ACT_UP:
        if (sel > 0) sel--;
        break;
      case layout::ACT_DOWN:
        if (sel + 1 < (int)songs.size()) sel++;
        break;
      case layout::ACT_ENTER:
        open(true);
        break;
      case layout::ACT_PLAY:
        if (!seq::loaded()) open(true);
        else if (seq::playing()) { seq::stop(); saveIfEdited(); }
        else seq::play(0);
        break;
      case layout::ACT_LEFT:
      case layout::ACT_RIGHT:
        if (seq::loaded()) {
          auto& s = seq::current();
          int bpm = s.bpm + (a == layout::ACT_RIGHT ? 2 : -2);
          s.bpm = constrain(bpm, 40, 240);
          seq::changed();
          edited = true;
        }
        break;
      case layout::ACT_DELETE:
        if (songs.empty()) break;
        if (millis() - delArmed < 2000) {
          if (loadedFile == songs[sel].file) { seq::setLoaded(false); loadedFile = ""; }
          store::remove(songs[sel].file);
          toast("Deleted from this device");
          delArmed = 0;
          refresh();
        } else {
          delArmed = millis();
          toast(songs[sel].cloudId[0] ? "del again: remove (stays online)" : "del again to delete");
        }
        break;
      default:
        break;
    }
    markDirty();
  }

  void draw(M5Canvas& c) override {
    // Keep the play position moving on screen.
    if (seq::playing()) markDirty();
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    if (cloud::busy()) {
      c.setTextColor(col::ACCENT);
      c.drawString(cloud::message(), 6, 18);
    }
    if (songs.empty()) {
      c.setTextColor(col::TEXT);
      c.drawString("No songs on this device yet.", 6, 34);
      c.setTextColor(col::DIM);
      c.drawString("Make one at trkr.one, then", 6, 50);
      c.drawString("hold ` > 5 Cloud > Sync now.", 6, 62);
      return;
    }
    const int rowH = 12, top = 18, rows = 6;
    int first = sel - rows / 2;
    if (first > (int)songs.size() - rows) first = songs.size() - rows;
    if (first < 0) first = 0;
    for (int i = 0; i < rows && first + i < (int)songs.size(); i++) {
      const auto& e = songs[first + i];
      int y = top + i * rowH;
      bool isSel = first + i == sel;
      if (isSel) c.fillRect(0, y - 1, W, rowH, col::SELECT);
      bool isLoaded = loadedFile == e.file;
      c.setTextColor(isLoaded ? col::HELD : col::TEXT);
      char line[44];
      snprintf(line, sizeof(line), "%s %-28.28s", isLoaded ? (seq::playing() ? ">" : "=") : " ", e.name);
      c.drawString(line, 4, y);
      c.setTextColor(col::DIM);
      c.setTextDatum(top_right);
      c.drawString(e.dirty ? "edited" : e.cloudId[0] ? "synced" : "local", W - 4, y);
      c.setTextDatum(top_left);
    }
    // Transport line
    c.drawFastHLine(0, H - 26, W, col::FAINT);
    if (seq::loaded()) {
      const auto& s = seq::current();
      char buf[48];
      c.setTextColor(seq::playing() ? col::OK : col::DIM);
      snprintf(buf, sizeof(buf), "%s %u BPM  bar %u  part %u/%u", seq::playing() ? "PLAY" : "STOP", s.bpm, seq::bar(),
               seq::orderIndex() + 1, (unsigned)s.order.size());
      c.drawString(buf, 6, H - 22);
    }
    c.setTextColor(col::DIM);
    c.drawString("ent play  spc stop  ,/ tempo  keys jam", 6, H - 11);
  }

 private:
  std::vector<store::Entry> songs;
  int sel = 0;
  String loadedFile;
  bool edited = false;
  uint32_t delArmed = 0;

  void refresh() {
    songs = store::list();
    if (sel >= (int)songs.size()) sel = songs.empty() ? 0 : songs.size() - 1;
  }

  void open(bool play) {
    if (songs.empty()) return;
    saveIfEdited();
    const char* err = nullptr;
    seq::stop();
    if (!store::load(songs[sel].file, seq::current(), &err)) {
      seq::setLoaded(false);
      loadedFile = "";
      char m[40];
      snprintf(m, sizeof(m), "Can't open: %s", err ? err : "?");
      toast(m);
      return;
    }
    seq::setLoaded(true);
    loadedFile = songs[sel].file;
    edited = false;
    if (play) seq::play(0);
  }

  void saveIfEdited() {
    if (!edited || !seq::loaded()) return;
    for (auto& e : songs)
      if (loadedFile == e.file) {
        store::save(e, seq::current(), false);
        break;
      }
    edited = false;
    refresh();
  }
};

static SongsScreen instance;
Screen* songsScreen() { return &instance; }

}  // namespace ui
