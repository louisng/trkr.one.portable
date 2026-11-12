#include <M5Cardputer.h>
#include <new>
#include <string.h>

#include "../app/Settings.h"
#include "../audio/Monitor.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../midi/MidiOut.h"
#include "../net/Cloud.h"
#include "../play/GenMode.h"
#include "../music/Theory.h"
#include "../play/LivePlayer.h"
#include "Screen.h"
#include "Ui.h"

namespace ui {

// The screen buffer needs the display object, which only exists once the M5 library has
// started. A plain global would be built during static start-up, in an order the linker
// picks, and could capture a null display (it crashed on pushSprite). So it is built in
// begin(), in place.
alignas(M5Canvas) static unsigned char canvasMem[sizeof(M5Canvas)];
static M5Canvas& canvas = *reinterpret_cast<M5Canvas*>(canvasMem);
static Screen* current = nullptr;
static bool dirty = true;
static uint32_t lastDraw = 0;
static uint32_t lastSave = 0;

static char toastMsg[40] = "";
static uint32_t toastUntil = 0;

// Hold ` (menu key): overlay with screen jumps + transport toggles.
// Tap ` alone: back to the Live screen.
static bool menuHeld = false;
static bool menuUsed = false;
static uint32_t menuSince = 0;
static constexpr uint32_t MENU_SHOW_DELAY_MS = 180;
static uint64_t swallowed = 0;  // keys pressed inside the menu: ignore their release

static midi::LinkState lastBle, lastUsb;
static bool adv = false;

// Menu shortcut keys, looked up by key cap so they follow the layout table.
static int8_t idxDigit[6];
static int8_t idxB = -1, idxU = -1, idxM = -1;

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

static void handleMenuKey(uint8_t idx) {
  if (idx == idxDigit[0]) switchTo(liveScreen());
  else if (idx == idxDigit[1]) switchTo(setupScreen());
  else if (idx == idxDigit[2]) switchTo(keyTestScreen());
  else if (idx == idxDigit[3]) switchTo(songsScreen());
  else if (idx == idxDigit[4]) switchTo(cloudScreen());
  else if (idx == idxDigit[5]) switchTo(genScreen());
  else if (idx == idxB) toggleBle();
  else if (idx == idxU) toggleUsb();
  else if (idx == idxM) toggleSpeaker();
}

static void handleEvent(const keys::Event& e) {
  dirty = true;
  current->onRawKey(e.index, e.pressed);
  const layout::KeyDef& def = layout::keyAt(e.index);
  uint64_t bit = 1ULL << e.index;

  if (def.role == layout::Role::Action && def.value == layout::ACT_MENU) {
    if (e.pressed) {
      menuHeld = true;
      menuUsed = false;
      menuSince = millis();
    } else {
      if (menuHeld && !menuUsed) switchTo(liveScreen());
      menuHeld = false;
    }
    return;
  }
  if (menuHeld && e.pressed) {
    menuUsed = true;
    swallowed |= bit;
    handleMenuKey(e.index);
    return;
  }
  if (!e.pressed && (swallowed & bit)) {
    swallowed &= ~bit;
    return;
  }
  if (current->consumeKey(e.index, e.pressed)) return;
  if (current->musicKeys() && player::onKey(e.index, e.pressed)) return;
  if (e.pressed && def.role == layout::Role::Action) current->onAction(def.value);
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

  int x = W - 3 - 24;
  drawPill(x, "SPK", settings.speaker ? midi::LinkState::Connected : midi::LinkState::Off);
  x -= 27;
  drawPill(x, "USB", midi::usbState());
  x -= 27;
  drawPill(x, "BLE", midi::bleState());

  char info[16];
  snprintf(info, sizeof(info), "%s O%d", adv ? "ADV" : "CP", settings.octave);
  canvas.setTextColor(col::DIM);
  canvas.setTextDatum(middle_right);
  canvas.drawString(info, x - 5, 7);
}

static void drawMenu() {
  const int x = 14, y = 22, w = W - 28, h = 100;
  canvas.fillRoundRect(x, y, w, h, 6, col::BAR);
  canvas.drawRoundRect(x, y, w, h, 6, col::ACCENT);
  canvas.setFont(&fonts::Font0);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(col::ACCENT);
  canvas.drawString("MENU  (hold `, press a key)", x + 8, y + 7);

  canvas.setTextColor(col::TEXT);
  canvas.drawString("1 Live   2 Setup   3 Key test", x + 8, y + 24);
  canvas.drawString("4 Songs  5 Cloud (trkr.one)", x + 8, y + 36);
  canvas.setTextColor(col::ACCENT);
  canvas.drawString("6 Generate  (endless songs)", x + 8, y + 52);

  char buf[32];
  canvas.setTextColor(col::TEXT);
  snprintf(buf, sizeof(buf), "b  Bluetooth MIDI: %s", settings.bleMidi ? "ON" : "off");
  canvas.drawString(buf, x + 8, y + 74);
  snprintf(buf, sizeof(buf), "u  USB MIDI: %s   m  Speaker: %s", settings.usbMidi ? "ON" : "off",
           settings.speaker ? "ON" : "off");
  canvas.drawString(buf, x + 8, y + 86);
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
  canvas.fillScreen(col::BG);
  current->draw(canvas);
  drawStatusBar();
  menuDrawn = menuVisible();
  if (menuDrawn) drawMenu();
  if (millis() < toastUntil) drawToast();
  canvas.pushSprite(0, 0);
}

// ── RAM for the cloud ────────────────────────────────────────────────────────
// WiFi plus the secure connection need ~100 KB. While a cloud job runs the
// screen buffer drops to 8-bit colour (32 KB instead of 64 KB), the speaker
// driver is released and generated playback stops; all of it comes back after.
static bool lowColor = false;
static bool jobMode = false;

static void setLowColor(bool on) {
  if (on == lowColor) return;
  canvas.deleteSprite();
  canvas.setColorDepth(on ? 8 : 16);
  if (!canvas.createSprite(W, H)) {
    // No room for full colour: stay in 8-bit rather than have no screen at all.
    canvas.setColorDepth(8);
    canvas.createSprite(W, H);
    on = true;
  }
  lowColor = on;
  dirty = true;
}

static void cloudMemoryTick() {
  bool busy = cloud::busy();
  if (busy && !jobMode) {
    jobMode = true;
    if (genmode::running()) genmode::stop();
    monitor::pause();
    setLowColor(true);
    cloud::memoryReady();
  } else if (jobMode && cloud::finishing()) {
    setLowColor(false);
    monitor::resume();
    jobMode = false;
    cloud::restored();
  }
}

void begin() {
  new (canvasMem) M5Canvas(&M5Cardputer.Display);
  adv = M5.getBoard() == m5::board_t::board_M5CardputerADV;
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  idxDigit[0] = findCap("1");
  idxDigit[1] = findCap("2");
  idxDigit[2] = findCap("3");
  idxDigit[3] = findCap("4");
  idxDigit[4] = findCap("5");
  idxDigit[5] = findCap("6");
  idxB = findCap("b");
  idxU = findCap("u");
  idxM = findCap("m");
  lastBle = midi::bleState();
  lastUsb = midi::usbState();
  current = liveScreen();
  current->onEnter();
  dirty = true;
}

void tick() {
  keys::Event ev[16];
  uint8_t n = keys::poll(ev, 16);
  for (uint8_t i = 0; i < n; i++) handleEvent(ev[i]);

  uint32_t now = millis();
  midi::LinkState b = midi::bleState(), u = midi::usbState();
  if (b != lastBle || u != lastUsb) {
    if (b == midi::LinkState::Connected && lastBle != b) toast("Bluetooth MIDI connected");
    if (u == midi::LinkState::Connected && lastUsb != u) toast("USB MIDI connected");
    lastBle = b;
    lastUsb = u;
    dirty = true;
  }
  cloudMemoryTick();
  current->tick();
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
