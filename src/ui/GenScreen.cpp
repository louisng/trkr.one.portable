// Generate: endless songs made on the device, in the genre you pick, played by
// the sequencer (and over MIDI / the built-in synth like any other song).
//
//   , /      genre                 enter   a new song now
//   ; .      play-along keys       space   play / pause
//   del      switch generated mode off
//   fn + 1-8 mute a part (stays muted in the next songs)    fn + s   save this song
// The piano / chord / drum keys play as usual; the piano keys follow the
// chord of the song (chord tones only, snapped to the chord, or the scale).
#include <stdio.h>
#include <string.h>

#include "../app/Settings.h"
#include "../input/KeyLayout.h"
#include "../music/Theory.h"
#include "../play/GenMode.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

class GenScreen : public Screen {
 public:
  const char* title() const override { return "GENERATE"; }
  bool musicKeys() const override { return true; }
  bool spaceIsPlay() const override { return false; }
  bool animating() const override { return true; }

  // enter and space mean something different here than on the other screens.
  bool consumeKey(uint8_t index, bool pressed) override {
    const layout::KeyDef& d = layout::keyAt(index);
    if (d.role != layout::Role::Action) return false;
    if (d.value == layout::ACT_ENTER) {
      if (pressed) genmode::next();
      return true;
    }
    if (d.value == layout::ACT_PLAY) {
      if (pressed) toggle();
      return true;
    }
    return false;
  }

  void onAction(uint8_t a) override {
    switch (a) {
      case layout::ACT_LEFT: genmode::setGenre(genmode::genre() - 1); break;
      case layout::ACT_RIGHT: genmode::setGenre(genmode::genre() + 1); break;
      case layout::ACT_UP: genmode::setAlong((gen::Along)(((int)genmode::along() + 3) % 4)); break;
      case layout::ACT_DOWN: genmode::setAlong((gen::Along)(((int)genmode::along() + 1) % 4)); break;
      case layout::ACT_DELETE:
        if (genmode::running()) {
          genmode::stop();
          toast("Generated mode off");
        }
        break;
      default: break;
    }
  }

  void draw(M5Canvas& c) override {
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    char line[64];

    // Genre
    c.setTextColor(col::ACCENT);
    if (genmode::running() && genmode::playingGenre() != genmode::genre())
      snprintf(line, sizeof(line), "%s  >  %s", genmode::genreName(genmode::playingGenre()), genmode::genreName(genmode::genre()));
    else
      snprintf(line, sizeof(line), "< %s >", genmode::genreName(genmode::genre()));
    c.drawString(line, 6, 19);

    // State
    c.setTextColor(col::TEXT);
    const genmode::State st = genmode::state();
    if (genmode::lowMemory()) {
      c.setTextColor(col::DRUM);
      snprintf(line, sizeof(line), "low memory: %d KB free (blk %d), need ~75", genmode::freeHeapKb(), genmode::largestBlockKb());
    } else if (st == genmode::State::Idle) {
      c.setTextColor(col::DIM);
      snprintf(line, sizeof(line), "space = start   (%d KB free)", genmode::freeHeapKb());
    } else if (st == genmode::State::Generating) {
      snprintf(line, sizeof(line), "building the first song...");
    } else if (genmode::rebuilding()) {
      snprintf(line, sizeof(line), "building the next song...");
    } else {
      const gen::Meta& m = genmode::meta();
      snprintf(line, sizeof(line), "%u bpm   %s %s%s", seq::song.bpm, theory::noteName(m.key), theory::scaleName(m.scale),
               seq::playing() ? "" : "   (paused)");
    }
    c.drawString(line, 6, 31);

    // Where we are
    if (st == genmode::State::Playing) {
      const int32_t step = seq::songStep();
      if (step >= 0) {
        const gen::Meta& m = genmode::meta();
        const gen::SectionSpan* sec = gen::sectionAt(m, (uint32_t)(step / 16));
        const gen::ChordSpan* ch = gen::chordAt(m, (uint32_t)step);
        c.setTextColor(col::DIM);
        snprintf(line, sizeof(line), "bar %d   %s", (int)(step / 16) + 1, sec ? sec->name : "");
        c.drawString(line, 6, 42);
        if (ch) {
          c.setTextColor(col::HELD);
          c.drawString(ch->sym, 150, 42);
        }
      }
    }

    // Play-along rule
    c.setTextColor(col::DIM);
    snprintf(line, sizeof(line), "piano keys: %s", genmode::alongName(genmode::along()));
    c.drawString(line, 6, 53);
    if (genmode::state() == genmode::State::Playing && genmode::prefetchBlocked()) {
      c.setTextColor(col::DRUM);
      c.drawString("low RAM: short gap between songs", 6, 64);
    }

    // Parts
    if (st != genmode::State::Idle) {
      for (uint8_t t = 0; t < seq::TRACKS; t++) {
        const seq::Track& tr = seq::song.tracks[t];
        const int x = 4 + t * 29, y = 67;
        const bool silent = tr.mute;
        c.fillRoundRect(x, y, 27, 24, 3, silent ? col::FAINT : col::BAR);
        c.drawRoundRect(x, y, 27, 24, 3, silent ? col::FAINT : col::ACCENT);
        c.setTextColor(silent ? col::DIM : col::TEXT);
        char nm[6];
        snprintf(nm, sizeof(nm), "%.4s", tr.name);
        c.setTextDatum(top_center);
        c.drawString(nm, x + 13, y + 3);
        snprintf(nm, sizeof(nm), silent ? "mute" : "ch%u", tr.channel);
        c.setTextColor(silent ? col::DRUM : col::DIM);
        c.drawString(nm, x + 13, y + 13);
        c.setTextDatum(top_left);
      }
    }

    if (st != genmode::State::Idle) {
      c.setTextColor(col::FAINT);
      snprintf(line, sizeof(line), "%d KB free (blk %d)", genmode::freeHeapKb(), genmode::largestBlockKb());
      c.setTextDatum(top_right);
      c.drawString(line, W - 4, 53);
      c.setTextDatum(top_left);
    }
    c.setTextColor(col::FAINT);
    c.drawString(", / genre  ; . keys  enter new  space play", 4, H - 20);
    c.drawString("del off  fn+1-8 mute  fn+s save", 4, H - 10);
  }

 private:
  static void toggle() {
    if (!genmode::running()) {
      toast("Replaces the open song (unsaved changes are lost)");
      genmode::start();
    }
    else if (seq::playing()) seq::stop();
    else seq::play();
  }
};

static GenScreen instance;
Screen* genScreen() { return &instance; }

}  // namespace ui
