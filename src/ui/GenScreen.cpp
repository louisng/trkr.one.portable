// Generate: endless songs from the genre engine, played over MIDI.
//
//   space      play / stop            enter     skip to a new song now
//   ;  .       previous / next genre  ,  /      tempo - / +
//   f          play-along mode: FREE > CHORD > SNAP > SCALE
//   l          loop this song (don't move on)
//   '          parts page: pick a part, change its MIDI channel, mute it
//   fn + 1-8   mute / unmute a part (kick snare hats perc bass keys lead pad)
//   fn + 9     unmute everything
//
// Everything else works as on the Live screen: the keyboard plays piano,
// chords and drum pads (in the song's key), with the play-along filter on
// the piano keys.
#include <stdio.h>
#include <string.h>

#include "../app/Settings.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../music/Theory.h"
#include "../play/GenMode.h"
#include "../play/LivePlayer.h"
#include "../play/Sequencer.h"
#include "Screen.h"

namespace ui {

static constexpr uint8_t IDX_FN = 2 * 14 + 0;
static constexpr uint8_t IDX_F = 2 * 14 + 5;      // play-along mode
static constexpr uint8_t IDX_L = 2 * 14 + 10;     // loop this song
static constexpr uint8_t IDX_QUOTE = 2 * 14 + 12; // parts page
static constexpr uint8_t IDX_NUM1 = 1;            // key "1"; digits 1..9 are indices 1..9

class GenScreen : public Screen {
 public:
  const char* title() const override { return "GENERATE"; }

  void onEnter() override {
    savedRoot = settings.root;
    savedScale = settings.scale;
    savedPianoMode = settings.pianoScaleMode;
    player::setNoteFilter(genmode::filterNote);
    followKey();
    lastSig = 0;
    toast("space play  f along  fn+1-8 mute");
  }
  void onLeave() override {
    player::setNoteFilter(nullptr);
    settings.root = savedRoot;
    settings.scale = savedScale;
    settings.pianoScaleMode = savedPianoMode;
  }

  // fn + digit = mute / unmute (the digit row is the drum pads otherwise).
  bool consumeKey(uint8_t idx, bool pressed) override {
    uint64_t bit = 1ULL << idx;
    if (!pressed) {
      if (taken & bit) { taken &= ~bit; return true; }
      return false;
    }
    if (!keys::isDown(IDX_FN)) return false;
    if (idx < IDX_NUM1 || idx > IDX_NUM1 + 8) return false;
    taken |= bit;
    if (idx == IDX_NUM1 + 8) {
      genmode::unmuteAll();
      toast("All parts on");
    } else {
      uint8_t t = idx - IDX_NUM1;
      if (t < genmode::trackCount() || !genmode::running()) {
        genmode::toggleMute(t);
        char m[40];
        snprintf(m, sizeof(m), "%s %s", t < genmode::trackCount() ? genmode::trackName(t) : "Part",
                 genmode::trackMuted(t) ? "muted" : "on");
        toast(m);
      }
    }
    markDirty();
    return true;
  }

  void onRawKey(uint8_t idx, bool pressed) override {
    if (!pressed || keys::isDown(IDX_FN)) return;
    if (idx == IDX_F) {
      player::releaseAll();
      genmode::cycleAlong();
      char m[32];
      snprintf(m, sizeof(m), "Play along: %s", genmode::alongName(genmode::along()));
      toast(m);
    } else if (idx == IDX_L) {
      genmode::setLocked(!genmode::locked());
      toast(genmode::locked() ? "Looping this song" : "Moving on after this song");
    } else if (idx == IDX_QUOTE) {
      partsPage = !partsPage;
    }
    markDirty();
  }

  void onAction(uint8_t a) override {
    if (partsPage && partsAction(a)) {
      markDirty();
      return;
    }
    switch (a) {
      case layout::ACT_PLAY:
        genmode::toggle();
        break;
      case layout::ACT_ENTER:
        genmode::next();
        break;
      case layout::ACT_UP:
        genmode::setGenre(genmode::genre() - 1);
        break;
      case layout::ACT_DOWN:
        genmode::setGenre(genmode::genre() + 1);
        break;
      case layout::ACT_LEFT:
        genmode::nudgeTempo(-2);
        break;
      case layout::ACT_RIGHT:
        genmode::nudgeTempo(+2);
        break;
      default:
        break;
    }
    markDirty();
  }

  // Redraw only when something shown has changed (a redraw takes a few ms,
  // which we'd rather not spend on every pass while MIDI is playing).
  void tick() override {
    uint32_t sig = (uint32_t)genmode::state() * 7;
    if (genmode::running()) {
      sig = sig * 31 + seq::bar();
      const char* c = genmode::chordName();
      for (; *c; c++) sig = sig * 31 + (uint8_t)*c;
    }
    sig = sig * 31 + genmode::genre();
    sig = sig * 31 + partsPage * 17 + partSel;
    for (uint8_t t = 0; t < genmode::TRACKS; t++) sig = sig * 31 + genmode::channelOf(t) + 16 * genmode::trackMuted(t);
    sig = sig * 31 + genmode::currentBpm();
    if (sig != lastSig) {
      lastSig = sig;
      markDirty();
    }
  }

  void draw(M5Canvas& c) override {
    followKey();
    if (partsPage) {
      drawParts(c);
      return;
    }
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);

    // Genre (and what comes next)
    char line[48];
    int sel = genmode::genre();
    if (genmode::running() && genmode::playingGenre() != sel)
      snprintf(line, sizeof(line), "%s > %s", genmode::genreName(genmode::playingGenre()), genmode::genreName(sel));
    else
      snprintf(line, sizeof(line), "< %s >", genmode::genreName(sel));
    c.setTextColor(col::ACCENT);
    c.drawString(line, 4, 17);
    c.setTextDatum(top_right);
    switch (genmode::state()) {
      case genmode::State::Idle: c.setTextColor(col::DIM); c.drawString("STOPPED", W - 4, 17); break;
      case genmode::State::Generating: c.setTextColor(col::ACCENT); c.drawString("BUILDING...", W - 4, 17); break;
      case genmode::State::Playing: c.setTextColor(col::OK); c.drawString(genmode::locked() ? "LOOP" : "PLAYING", W - 4, 17); break;
    }

    // Chord, section, bar
    c.setTextDatum(middle_left);
    if (genmode::state() == genmode::State::Playing) {
      c.setFont(&fonts::FreeSansBold12pt7b);
      c.setTextColor(col::HELD);
      c.drawString(genmode::chordName(), 6, 38);
      c.setFont(&fonts::Font0);
      c.setTextDatum(top_right);
      c.setTextColor(col::TEXT);
      char sec[24];
      snprintf(sec, sizeof(sec), "%s", genmode::sectionName());
      for (char* p = sec; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
      c.drawString(sec, W - 4, 29);
      snprintf(sec, sizeof(sec), "bar %u", seq::bar());
      c.setTextColor(col::DIM);
      c.drawString(sec, W - 4, 39);
    } else {
      c.setFont(&fonts::Font0);
      c.setTextColor(col::DIM);
      if (genmode::lowMemory()) {
        c.setTextColor(col::DRUM);
        c.drawString("not enough free memory", 6, 38);
      } else {
        c.drawString(genmode::running() ? "building..." : "space = play   enter = new song", 6, 38);
      }
    }

    // Info line
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::DIM);
    if (genmode::state() != genmode::State::Playing) {
      snprintf(line, sizeof(line), "free memory %d KB", genmode::freeHeapKb());
      c.drawString(line, 4, 49);
    }
    if (genmode::state() == genmode::State::Playing) {
      const gen::Meta& m = genmode::meta();
      snprintf(line, sizeof(line), "%u bpm  %s %s", genmode::currentBpm(), theory::noteName(m.key), theory::scaleName(m.scale));
      c.drawString(line, 4, 49);
    }
    c.setTextDatum(top_right);
    genmode::AlongMode am = genmode::along();
    c.setTextColor(am == genmode::AlongMode::Free ? col::DIM : col::ACCENT);
    snprintf(line, sizeof(line), "along:%s", genmode::alongName(am));
    c.drawString(line, W - 4, 49);

    drawChips(c, 59);
    drawKeyMap(c, 71, false, genmode::state() == genmode::State::Playing);
  }

 private:
  uint8_t savedRoot = 0, savedScale = 0;
  bool savedPianoMode = false;
  uint32_t lastSig = 0;
  uint64_t taken = 0;
  bool partsPage = false;
  uint8_t partSel = 0;

  // Parts page keys: ; . pick a part, , / its MIDI channel, enter mute, del reset.
  bool partsAction(uint8_t a) {
    switch (a) {
      case layout::ACT_UP: partSel = (partSel + genmode::TRACKS - 1) % genmode::TRACKS; return true;
      case layout::ACT_DOWN: partSel = (partSel + 1) % genmode::TRACKS; return true;
      case layout::ACT_LEFT:
        genmode::setChannel(partSel, genmode::channelOf(partSel) == 1 ? 16 : genmode::channelOf(partSel) - 1);
        return true;
      case layout::ACT_RIGHT:
        genmode::setChannel(partSel, genmode::channelOf(partSel) == 16 ? 1 : genmode::channelOf(partSel) + 1);
        return true;
      case layout::ACT_ENTER: genmode::toggleMute(partSel); return true;
      case layout::ACT_DELETE:
        genmode::resetChannel(partSel);
        toast("Channel reset");
        return true;
      default: return false;  // space etc. still work
    }
  }

  void drawParts(M5Canvas& c) {
    static const char* const NAMES[genmode::TRACKS] = {"Kick", "Snare", "Hats", "Perc", "Bass", "Keys", "Lead", "Pad/Arp"};
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::ACCENT);
    c.drawString("PARTS", 4, 17);
    c.setTextColor(col::DIM);
    c.drawString("' = back", 50, 17);
    for (uint8_t i = 0; i < genmode::TRACKS; i++) {
      int y = 29 + i * 11;
      if (i == partSel) c.fillRect(0, y - 1, W, 11, col::SELECT);
      bool muted = genmode::trackMuted(i);
      c.setTextColor(muted ? col::DIM : col::TEXT);
      char row[24];
      snprintf(row, sizeof(row), "%u  %s", i + 1, i < genmode::trackCount() ? genmode::trackName(i) : NAMES[i]);
      c.drawString(row, 6, y);
      uint8_t ch = genmode::channelOf(i);
      bool changed = ch != genmode::defaultChannel(i);
      c.setTextColor(changed ? col::ACCENT : col::TEXT);
      snprintf(row, sizeof(row), "ch %2u%s", ch, changed ? "*" : " ");
      c.drawString(row, 110, y);
      c.setTextDatum(top_right);
      c.setTextColor(muted ? col::DRUM : col::OK);
      c.drawString(muted ? "MUTED" : "on", W - 6, y);
      c.setTextDatum(top_left);
    }
    c.setTextColor(col::FAINT);
    c.drawString("; . pick  , / channel  ent mute  del reset", 4, H - 9);
  }

  // Chord buttons and scale mode follow the song's key while this screen is up.
  void followKey() {
    if (genmode::state() != genmode::State::Playing) return;
    const gen::Meta& m = genmode::meta();
    settings.root = m.key;
    settings.scale = m.scale;
  }

  void drawChips(M5Canvas& c, int y) {
    static const uint16_t TYPE_COL[5] = {col::DRUM, col::OK, col::CHORD, col::HELD, col::ACCENT};
    const int w = 29;
    c.setTextDatum(middle_center);
    for (uint8_t t = 0; t < genmode::TRACKS; t++) {
      int x = 3 + t * w;
      bool have = t < genmode::trackCount();
      bool muted = genmode::trackMuted(t);
      uint16_t bg = !have ? col::FAINT : muted ? col::BAR : TYPE_COL[(uint8_t)seq::current().tracks[t].type % 5];
      uint16_t fg = muted || !have ? col::DIM : TFT_BLACK;
      c.fillRoundRect(x, y, w - 2, 10, 2, bg);
      char lbl[8];
      if (have) {
        snprintf(lbl, sizeof(lbl), "%u", t + 1);
        const char* n = genmode::trackName(t);
        size_t len = strlen(lbl);
        for (int i = 0; i < 3 && n[i]; i++) lbl[len++] = (n[i] >= 'a' && n[i] <= 'z') ? n[i] - 32 : n[i];
        lbl[len] = 0;
      } else {
        snprintf(lbl, sizeof(lbl), "%u", t + 1);
      }
      c.setTextColor(fg);
      c.drawString(lbl, x + (w - 2) / 2, y + 5);
      if (muted && have) c.drawFastHLine(x + 2, y + 5, w - 6, col::DIM);  // struck through
    }
  }
};

static GenScreen instance;
Screen* genScreen() { return &instance; }

}  // namespace ui
