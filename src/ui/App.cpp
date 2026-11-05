#include <M5Cardputer.h>
#include <string.h>

#include "../app/Settings.h"
#include "../audio/Monitor.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../midi/MidiOut.h"
#include "../music/Theory.h"
#include "../play/LivePlayer.h"
#include "../seq/Engine.h"
#include "Screen.h"
#include "Ui.h"

namespace ui {

static M5Canvas canvas;  // parentless; pushed to the display explicitly
static Screen* current = nullptr;
static bool dirty = true;
static uint32_t lastDraw = 0;
static uint32_t lastSave = 0;

static char toastMsg[40] = "";
static uint32_t toastUntil = 0;

// Hold ` (menu key): overlay with screen jumps + transport toggles.
// Tap ` alone: cycle Step -> Tracker -> Live.
// Hold fn + key: quick toggles (speaker, upper piano, Bluetooth, USB).
static bool menuHeld = false;
static bool menuUsed = false;
static uint32_t menuSince = 0;
static constexpr uint32_t MENU_SHOW_DELAY_MS = 180;
static uint64_t swallowed = 0;  // keys pressed inside the menu: ignore their release

static bool fnHeld = false;
static Screen* lastMain = nullptr;  // Step / Tracker / Live: where a tap of ` returns to
static int lastPlayStep = -2;

static midi::LinkState lastBle, lastUsb;
static bool adv = false;

// Shortcut keys, looked up by key cap so they follow the layout table.
static int8_t idxDigit[5];
static int8_t idxB = -1, idxU = -1, idxM = -1, idxP = -1, idxS = -1, idxT = -1;

static int8_t findCap(const char* cap) {
  for (uint8_t r = 0; r < layout::ROWS; r++)
    for (uint8_t c = 0; c < layout::COLS; c++)
      if (strcmp(layout::keyCapLabel(r, c), cap) == 0) return r * layout::COLS + c;
  return -1;
}

void markDirty() { dirty = true; }
bool isAdv() { return adv; }

void toast(const char* msg) {
  strncpy(toastMsg, msg, sizeof(toastMsg) - 1);
  toastMsg[sizeof(toastMsg) - 1] = 0;
  toastUntil = millis() + 1500;
  dirty = true;
}

static void switchTo(Screen* s) {
  if (s == current) return;
  if (s == liveScreen() || s == stepScreen() || s == trackerScreen()) lastMain = s;
  if (current) current->onLeave();
  if (!s->musicKeys()) player::releaseAll();
  current = s;
  current->onEnter();
  settingsSave();
  dirty = true;
}

static void toggleBle() {
  settings.bleMidi = !settings.bleMidi;
  midi::setBleEnabled(settings.bleMidi);
  toast(settings.bleMidi ? "Bluetooth MIDI on" : "Bluetooth MIDI off");
}
static void toggleUsb() {
  settings.usbMidi = !settings.usbMidi;
  midi::setUsbEnabled(settings.usbMidi);
  toast(settings.usbMidi ? "USB MIDI on" : "USB MIDI off");
}
static void toggleSpeaker() {
  settings.speaker = !settings.speaker;
  monitor::setEnabled(settings.speaker);
  toast(settings.speaker ? "Speaker on" : "Speaker off");
}

static void toggleUpperPiano() {
  player::releaseAll();  // the keys are about to change meaning
  settings.upperPiano = !settings.upperPiano;
  layout::setUpperPiano(settings.upperPiano);
  toast(settings.upperPiano ? "Top rows: piano (upper octave)" : "Top rows: drums + chords");
}

static void saveSong() {
  bool ok = seq::songSave();
  toast(ok ? "Song saved" : "Save failed");
}

static void handleMenuKey(uint8_t idx) {
  if (idx == idxDigit[0]) switchTo(liveScreen());
  else if (idx == idxDigit[1]) switchTo(stepScreen());
  else if (idx == idxDigit[2]) switchTo(trackerScreen());
  else if (idx == idxDigit[3]) switchTo(setupScreen());
  else if (idx == idxDigit[4]) switchTo(keyTestScreen());
  else if (idx == idxB) toggleBle();
  else if (idx == idxU) toggleUsb();
  else if (idx == idxM) toggleSpeaker();
  else if (idx == idxP) toggleUpperPiano();
  else if (idx == idxS) saveSong();
}

static void changeTempo(int d) {
  int b = seq::song.bpm + d;
  seq::song.bpm = (uint16_t)(b < seq::BPM_MIN ? seq::BPM_MIN : b > seq::BPM_MAX ? seq::BPM_MAX : b);
  char msg[20];
  snprintf(msg, sizeof(msg), "Tempo %d", seq::song.bpm);
  toast(msg);
}

static void handleFnKey(uint8_t idx) {
  const layout::KeyDef& def = layout::keyAt(idx);
  if (def.role == layout::Role::Action) {
    switch (def.value) {
      case layout::ACT_LEFT: changeTempo(-1); return;
      case layout::ACT_RIGHT: changeTempo(+1); return;
      case layout::ACT_UP: changeTempo(+10); return;
      case layout::ACT_DOWN: changeTempo(-10); return;
      default: break;
    }
  }
  if (idx == idxT) {
    uint8_t n = seq::tapTempo();
    char msg[24];
    if (n < 2) snprintf(msg, sizeof(msg), "Tap tempo: keep tapping");
    else snprintf(msg, sizeof(msg), "Tempo %d (tap)", seq::song.bpm);
    toast(msg);
    return;
  }
  if (idx == idxM) toggleSpeaker();
  else if (idx == idxP) toggleUpperPiano();
  else if (idx == idxB) toggleBle();
  else if (idx == idxU) toggleUsb();
  else if (idx == idxS) saveSong();
}

// space / enter work on every screen that plays music.
static bool handleTransport(uint8_t action) {
  if (action == layout::ACT_PLAY) {
    if (seq::playing()) seq::stop();
    else seq::play();
    return true;
  }
  if (action == layout::ACT_ENTER) {
    seq::setRecordArmed(!seq::recordArmed());
    toast(seq::recordArmed() ? "Record armed" : "Record off");
    return true;
  }
  return false;
}

static void handleEvent(const keys::Event& e) {
  dirty = true;
  current->onRawKey(e.index, e.pressed);
  const layout::KeyDef& def = layout::keyAt(e.index);
  const uint64_t bit = 1ULL << e.index;
  const bool isAction = def.role == layout::Role::Action;

  if (isAction && def.value == layout::ACT_MENU) {
    if (e.pressed) {
      menuHeld = true;
      menuUsed = false;
      menuSince = millis();
    } else {
      if (menuHeld && !menuUsed)
        switchTo(current == stepScreen()      ? trackerScreen()
                 : current == trackerScreen() ? liveScreen()
                 : current == liveScreen()    ? stepScreen()
                                              : (lastMain ? lastMain : stepScreen()));
      menuHeld = false;
    }
    return;
  }
  if (isAction && def.value == layout::ACT_FN) {
    fnHeld = e.pressed;
    return;
  }
  if ((menuHeld || fnHeld) && e.pressed) {
    menuUsed = true;
    swallowed |= bit;
    if (menuHeld) handleMenuKey(e.index);
    else handleFnKey(e.index);
    return;
  }
  if (!e.pressed && (swallowed & bit)) {
    swallowed &= ~bit;
    return;
  }

  if (current->musicKeys()) {
    if (e.pressed && isAction && handleTransport(def.value)) return;
    player::Played played;
    if (player::onKey(e.index, e.pressed, &played)) {
      if (played.valid) {
        if (seq::recording()) {
          if (played.pressed)
            seq::recordPress(e.index, played.track, played.notes, played.count, settings.velocity);
          else
            seq::recordRelease(e.index);
        } else {
          current->onPlayed(e.index, played);
        }
      }
      return;
    }
  }
  if (e.pressed && isAction) current->onAction(def.value);
}

// ── Drawing ──────────────────────────────────────────────────────────────────
static void drawPill(int x, const char* label, midi::LinkState st) {
  uint16_t bg = st == midi::LinkState::Connected ? col::OK
                : st == midi::LinkState::Waiting  ? col::ACCENT
                                                  : col::FAINT;
  uint16_t fg = st == midi::LinkState::Off ? col::DIM : TFT_BLACK;
  canvas.fillRoundRect(x, 2, 24, 10, 3, bg);
  canvas.setTextColor(fg);
  canvas.setTextDatum(middle_center);
  canvas.drawString(label, x + 12, 7);
}

static void drawStatusBar() {
  canvas.fillRect(0, 0, W, STATUS_H, col::BAR);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(col::TEXT);
  canvas.setTextDatum(middle_left);
  canvas.drawString(current->title(), 3, 7);

  // Transport: green triangle = playing, red dot = record armed (filled while recording).
  int tx = 3 + canvas.textWidth(current->title()) + 6;
  if (seq::playing()) canvas.fillTriangle(tx, 3, tx, 11, tx + 7, 7, col::OK);
  else canvas.fillRect(tx, 4, 7, 7, col::FAINT);
  tx += 11;
  if (seq::recordArmed()) {
    if (seq::recording()) canvas.fillCircle(tx + 3, 7, 4, 0xF800);
    else canvas.drawCircle(tx + 3, 7, 4, 0xF800);
  }

  int x = W - 3 - 24;
  drawPill(x, "SPK", settings.speaker ? midi::LinkState::Connected : midi::LinkState::Off);
  x -= 27;
  drawPill(x, "USB", midi::usbState());
  x -= 27;
  drawPill(x, "BLE", midi::bleState());

  char info[16];
  snprintf(info, sizeof(info), "%d O%d", seq::song.bpm, settings.octave);
  canvas.setTextColor(col::DIM);
  canvas.setTextDatum(middle_right);
  canvas.drawString(info, x - 5, 7);
}

static void drawMenu() {
  const int x = 10, y = 18, w = W - 20, h = 114;
  canvas.fillRoundRect(x, y, w, h, 6, col::BAR);
  canvas.drawRoundRect(x, y, w, h, 6, col::ACCENT);
  canvas.setFont(&fonts::Font0);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(col::ACCENT);
  canvas.drawString("MENU  (hold `, press a key)", x + 8, y + 6);

  canvas.setTextColor(col::TEXT);
  canvas.drawString("1 Live   2 Step   3 Tracker", x + 8, y + 20);
  canvas.drawString("4 Setup  5 Key test", x + 8, y + 31);
  canvas.drawString("s Save song", x + 8, y + 42);

  char buf[40];
  snprintf(buf, sizeof(buf), "b Bluetooth MIDI: %s", settings.bleMidi ? "ON" : "off");
  canvas.drawString(buf, x + 8, y + 56);
  snprintf(buf, sizeof(buf), "u USB MIDI: %s", settings.usbMidi ? "ON" : "off");
  canvas.drawString(buf, x + 8, y + 67);
  snprintf(buf, sizeof(buf), "m Speaker: %s", settings.speaker ? "ON" : "off");
  canvas.drawString(buf, x + 8, y + 78);
  snprintf(buf, sizeof(buf), "p Top rows: %s", settings.upperPiano ? "piano" : "drums+chords");
  canvas.drawString(buf, x + 8, y + 89);
  canvas.setTextColor(col::DIM);
  canvas.drawString("fn+m/p/b/u/s, fn+arrows/t = tempo", x + 8, y + 101);
}

static void drawToast() {
  canvas.setFont(&fonts::Font0);
  int tw = canvas.textWidth(toastMsg) + 16;
  int x = (W - tw) / 2, y = H - 22;
  canvas.fillRoundRect(x, y, tw, 16, 4, col::ACCENT);
  canvas.setTextColor(TFT_BLACK);
  canvas.setTextDatum(middle_center);
  canvas.drawString(toastMsg, W / 2, y + 8);
}

static bool menuDrawn = false;
static bool menuVisible() { return menuHeld && millis() - menuSince >= MENU_SHOW_DELAY_MS; }

static void render() {
  {
    seq::Lock l;  // the screens read the song while drawing
    canvas.fillScreen(col::BG);
    current->draw(canvas);
    drawStatusBar();
  }
  menuDrawn = menuVisible();
  if (menuDrawn) drawMenu();
  if (millis() < toastUntil) drawToast();
  canvas.pushSprite(&M5Cardputer.Display, 0, 0);  // slow SPI transfer: outside the lock
}

void begin() {
  adv = M5.getBoard() == m5::board_t::board_M5CardputerADV;
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  for (uint8_t i = 0; i < 5; i++) {
    char cap[2] = {(char)('1' + i), 0};
    idxDigit[i] = findCap(cap);
  }
  idxB = findCap("b");
  idxU = findCap("u");
  idxM = findCap("m");
  idxP = findCap("p");
  idxS = findCap("s");
  idxT = findCap("t");
  layout::setUpperPiano(settings.upperPiano);
  lastBle = midi::bleState();
  lastUsb = midi::usbState();
  current = stepScreen();
  lastMain = current;
  current->onEnter();
  dirty = true;
}

void tick() {
  keys::Event ev[16];
  uint8_t n = keys::poll(ev, 16);
  if (n) {
    seq::Lock l;
    for (uint8_t i = 0; i < n; i++) handleEvent(ev[i]);
  }

  uint32_t now = millis();
  midi::LinkState b = midi::bleState(), u = midi::usbState();
  if (b != lastBle || u != lastUsb) {
    if (b == midi::LinkState::Connected && lastBle != b) toast("Bluetooth MIDI connected");
    if (u == midi::LinkState::Connected && lastUsb != u) toast("USB MIDI connected");
    lastBle = b;
    lastUsb = u;
    dirty = true;
  }
  // Follow the playhead.
  int ps = seq::trackStep(seq::selectedTrack());
  if (ps != lastPlayStep) {
    lastPlayStep = ps;
    dirty = true;
  }
  // Menu overlay appears after a short delay; toast disappears on its own.
  if (menuVisible() != menuDrawn) dirty = true;
  if (toastUntil && now >= toastUntil) {
    toastUntil = 0;
    dirty = true;
  }

  // Redraw only when something changed, capped at ~30 fps, so drawing never
  // delays note handling more than it has to.
  if (dirty && now - lastDraw >= 33) {
    dirty = false;
    lastDraw = now;
    render();
  }

  if (now - lastSave >= 5000) {
    lastSave = now;
    settingsSave();
  }
}

}  // namespace ui
