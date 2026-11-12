#include <M5Cardputer.h>
#include <string.h>

#include "../app/Settings.h"
#include "../audio/Synth.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../midi/BleMidi.h"
#include "../midi/MidiOut.h"
#include "../net/Cloud.h"
#include "../play/GenMode.h"
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

// Held arrow / f / l keys repeat after a short pause.
static int8_t repeatKey = -1;
static uint8_t repeatAction = 0;
static uint32_t repeatAt = 0;
static constexpr uint32_t REPEAT_DELAY_MS = 350, REPEAT_RATE_MS = 60;

static bool repeatable(uint8_t a) {
  return a == layout::ACT_UP || a == layout::ACT_DOWN || a == layout::ACT_LEFT ||
         a == layout::ACT_RIGHT || a == layout::ACT_SHORTER || a == layout::ACT_LONGER;
}
static Screen* lastMain = nullptr;  // Step / Tracker / Live: where a tap of ` returns to
static int lastPlayStep = -2;

static midi::LinkState lastBle, lastUsb;
static bool adv = false;

// Shortcut keys, looked up by key cap so they follow the layout table.
static int8_t idxDigit[10];
static int8_t idxB = -1, idxU = -1, idxM = -1, idxP = -1, idxS = -1, idxT = -1, idxX = -1, idxC = -1, idxW = -1, idxG = -1;

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

void goTo(Screen* s) { switchTo(s); }

void applyBtAuto() {
  if (settings.bleMidi && settings.btAuto && settings.btAddr[0]) {
    blemidi::Found f{};
    strncpy(f.name, settings.btName, sizeof(f.name) - 1);
    strncpy(f.addr, settings.btAddr, sizeof(f.addr) - 1);
    f.addrType = settings.btAddrType;
    blemidi::setAutoReconnect(&f);
  } else {
    blemidi::setAutoReconnect(nullptr);
  }
}

static void toggleBle() {
  settings.bleMidi = !settings.bleMidi;
  midi::setBleEnabled(settings.bleMidi);
  applyBtAuto();
  toast(settings.bleMidi ? "Bluetooth MIDI on" : "Bluetooth MIDI off");
  bleSettingChanged();
}

// Notices connections made / lost / failed on the Connect screen (or by
// auto-reconnect), remembers the device, and says what happened.
static void watchRemote() {
  static blemidi::RemoteState last = blemidi::RemoteState::Idle;
  static bool wasConnected = false;
  const blemidi::RemoteState st = blemidi::remoteState();
  const bool conn = blemidi::remoteConnected();
  char msg[48];
  if (conn && !wasConnected) {
    blemidi::Found d = blemidi::connectedDevice();
    if (strcmp(settings.btAddr, d.addr) != 0 || strcmp(settings.btName, d.name) != 0) {
      strncpy(settings.btAddr, d.addr, sizeof(settings.btAddr) - 1);
      strncpy(settings.btName, d.name, sizeof(settings.btName) - 1);
      settings.btAddrType = d.addrType;
      settingsSave();
      applyBtAuto();
    }
    snprintf(msg, sizeof(msg), "Connected to %s", d.name);
    toast(msg);
  } else if (!conn && wasConnected && st != blemidi::RemoteState::Connecting &&
             !blemidi::droppedByUser()) {
    snprintf(msg, sizeof(msg), "Lost %s", blemidi::remoteName());
    toast(msg);
  } else if (st == blemidi::RemoteState::Failed && last != blemidi::RemoteState::Failed) {
    toast(blemidi::lastError());
  }
  if (conn != wasConnected || st != last) markDirty();
  wasConnected = conn;
  last = st;
}
static void toggleUsb() {
  settings.usbMidi = !settings.usbMidi;
  midi::setUsbEnabled(settings.usbMidi);
  toast(settings.usbMidi ? "USB MIDI on" : "USB MIDI off");
}
static void toggleSpeaker() {
  settings.speaker = !settings.speaker;
  synth::setEnabled(settings.speaker);
  toast(settings.speaker ? "Speaker on" : "Speaker off");
}

static void toggleUpperPiano() {
  player::releaseAll();  // the keys are about to change meaning
  settings.upperPiano = !settings.upperPiano;
  layout::setUpperPiano(settings.upperPiano);
  toast(settings.upperPiano ? "Top rows: piano (upper octave)" : "Top rows: drums + chords");
}

// Quick save: to the open song's name, or ask for a name if it has none.
static void saveSong() {
  if (!seq::currentSongName()[0]) {
    songsStartSaveAs();
    switchTo(songsScreen());
    return;
  }
  bool ok = seq::songSave();
  if (ok) {
    strncpy(settings.lastSong, seq::currentSongName(), sizeof(settings.lastSong) - 1);
    settings.lastSong[sizeof(settings.lastSong) - 1] = 0;
    settings.lastSongOnSd = seq::currentSongStorage() == seq::Storage::Sd;
    settingsSave();  // remember it right away, not on the next periodic save
  }
  char msg[40];
  snprintf(msg, sizeof(msg), ok ? "Saved %s" : "Save failed", seq::currentSongName());
  toast(msg);
}

static void handleMenuKey(uint8_t idx) {
  if (idx == idxDigit[0]) switchTo(liveScreen());
  else if (idx == idxDigit[1]) switchTo(stepScreen());
  else if (idx == idxDigit[2]) switchTo(trackerScreen());
  else if (idx == idxDigit[3]) switchTo(scenesScreen());
  else if (idx == idxDigit[4]) switchTo(arrangeScreen());
  else if (idx == idxDigit[5]) switchTo(songsScreen());
  else if (idx == idxDigit[6]) switchTo(setupScreen());
  else if (idx == idxDigit[7]) switchTo(keyTestScreen());
  else if (idx == idxDigit[8]) switchTo(synthScreen());
  else if (idx == idxDigit[9]) switchTo(drumMidiScreen());
  else if (idx == idxC) switchTo(connectScreen());
  else if (idx == idxW) switchTo(cloudScreen());
  else if (idx == idxG) switchTo(genScreen());
  else if (idx == idxB) toggleBle();
  else if (idx == idxU) toggleUsb();
  else if (idx == idxM) toggleSpeaker();
  else if (idx == idxP) toggleUpperPiano();
  else if (idx == idxS) saveSong();
}

// Mute / unmute a track (what the sequencer plays on it).
// fn + x: the selected track. fn + 1-8: track 1-8 (1 is the drum track by default).
static void toggleTrackMute(uint8_t t) {
  if (t >= seq::TRACKS) return;
  seq::Track& tr = seq::song.tracks[t];
  tr.mute = !tr.mute;
  if (tr.mute) seq::releaseTrack(t);  // cut anything it's still sounding
  char msg[32];
  snprintf(msg, sizeof(msg), "Track %d %s %s", t + 1, tr.name, tr.mute ? "muted" : "unmuted");
  toast(msg);
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
  for (uint8_t t = 0; t < seq::TRACKS; t++) {
    if (idx == idxDigit[t]) {  // number row keys 1-8
      toggleTrackMute(t);
      return;
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
  else if (idx == idxX) toggleTrackMute(seq::selectedTrack());
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
  if (!e.pressed && e.index == repeatKey) repeatKey = -1;

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

  if (current->consumeKey(e.index, e.pressed)) return;

  if (e.pressed && isAction && def.value == layout::ACT_PLAY && current->spaceIsPlay()) {
    handleTransport(def.value);
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
  if (e.pressed && isAction) {
    current->onAction(def.value);
    if (repeatable(def.value)) {
      repeatKey = (int8_t)e.index;
      repeatAction = def.value;
      repeatAt = millis() + REPEAT_DELAY_MS;
    }
  }
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
  if (seq::song.songMode) {  // song mode: "S" + the arrangement row playing
    char sp[8];
    int pos = seq::songPosition();
    if (pos >= 0) snprintf(sp, sizeof(sp), "S%02d", pos + 1);
    else snprintf(sp, sizeof(sp), "S");
    canvas.setTextColor(col::OK);
    canvas.setTextDatum(middle_left);
    canvas.drawString(sp, tx + 11, 7);
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

  // Screens, 3 per row.
  static const char* const SCREENS[10] = {"1 Live",  "2 Step",  "3 Tracker", "4 Scenes", "5 Arrange",
                                          "6 Songs", "7 Setup", "8 Keys",    "9 Synth",  "0 Drum MIDI"};
  canvas.setTextColor(col::TEXT);
  for (int i = 0; i < 9; i++) canvas.drawString(SCREENS[i], x + 8 + (i % 3) * 68, y + 19 + (i / 3) * 10);
  canvas.drawString(SCREENS[9], x + 8, y + 49);  // fourth row
  canvas.drawString("w Cloud", x + 8 + 68, y + 49);
  canvas.drawString("c Connect BT", x + 8 + 2 * 68, y + 49);

  char buf[40];
  snprintf(buf, sizeof(buf), "s Save %.9s", seq::currentSongName()[0] ? seq::currentSongName() : "as...");
  canvas.drawString(buf, x + 8, y + 61);
  canvas.drawString("g Generate", x + 110, y + 61);
  snprintf(buf, sizeof(buf), "b BLE: %s", settings.bleMidi ? "ON" : "off");
  canvas.drawString(buf, x + 8, y + 72);
  snprintf(buf, sizeof(buf), "u USB: %s", settings.usbMidi ? "ON" : "off");
  canvas.drawString(buf, x + 110, y + 72);
  snprintf(buf, sizeof(buf), "m Sound: %s", settings.speaker ? "ON" : "off");
  canvas.drawString(buf, x + 8, y + 82);
  snprintf(buf, sizeof(buf), "p Top: %s", settings.upperPiano ? "piano" : "drum+chd");
  canvas.drawString(buf, x + 110, y + 82);
  canvas.setTextColor(col::DIM);
  canvas.drawString("fn+ m p b u s: same  fn+1-8: mute", x + 8, y + 94);
  canvas.drawString("fn+x mute sel  fn+arrows/t tempo", x + 8, y + 103);
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

// ── RAM for the cloud ────────────────────────────────────────────────────────
// WiFi plus the secure connection need ~100 KB. While a cloud job runs the
// speaker driver is released and the screen buffer drops to 8-bit colour
// (32 KB instead of 64 KB); the job itself unloads Bluetooth. All of it comes
// back afterwards, Bluetooth last.
static bool lowColor = false;
static bool jobMode = false;

static uint32_t colorRetryAt = 0;  // after a failed return to full colour, wait before trying again

static void setLowColor(bool on) {
  if (on == lowColor) return;
  canvas.deleteSprite();
  canvas.setColorDepth(on ? 8 : 16);
  if (!canvas.createSprite(W, H)) {
    // No room for full colour: stay in 8-bit rather than have no screen at all.
    canvas.setColorDepth(8);
    canvas.createSprite(W, H);
    on = true;
    colorRetryAt = millis() + 3000;
  }
  lowColor = on;
  dirty = true;
}

// Generated mode builds the next song while one plays: that needs the RAM the
// 16-bit screen buffer holds, so the screen runs in 8-bit colour meanwhile.
// Generated mode plays through the built-in speaker OR through MIDI, not both,
// and what the other way needs is released to make room (decided when it starts):
//   a MIDI device is connected -> the speaker driver is released (~18 KB);
//                                 Bluetooth stays only if that device is on Bluetooth
//   nothing connected          -> the speaker plays; Bluetooth is unloaded (~56 KB)
// Unloading Bluetooth can take a moment (it waits for the connect-out worker), so it
// happens on a small task, never in the UI loop.
static bool genLatched = false, genSynthOff = false;
static volatile int genBle = 0;  // 0 loaded, 1 unloading, 2 unloaded
static uint32_t genStoppedAt = 0;
static char genNote[40] = "";
static void bleUnloadTask(void*) {
  midi::pauseBle();
  genBle = 2;
  vTaskDelete(nullptr);
}
const char* genOutputNote() { return genNote; }

// Bluetooth MIDI was switched on while Generated mode runs with Bluetooth unloaded
// (to make room): there is no stack to switch on. Restart Generated mode, which decides
// again with Bluetooth wanted: Bluetooth stays loaded and the speaker is released instead.
// (Reloading Bluetooth in the middle of a song build could run out of memory.)
void bleSettingChanged() {
  if (!genmode::running() || !settings.bleMidi || genBle == 0 || jobMode) return;
  for (int i = 0; i < 200 && genBle == 1; i++) vTaskDelay(pdMS_TO_TICKS(10));  // an unload still in progress
  if (genBle != 2) return;
  genmode::stop();
  if (genSynthOff) {
    synth::resume();
    genSynthOff = false;
  }
  midi::resumeBle();
  midi::setBleEnabled(settings.bleMidi);  // it comes back in the state it was unloaded in: apply the setting
  applyBtAuto();
  genBle = 0;
  genLatched = false;  // the next tick decides again, with Bluetooth wanted
  genNote[0] = 0;
  genmode::start();
  toast("Bluetooth on: restarting Generated mode");
}

static void genColorTick() {
  if (jobMode) return;  // a cloud job decides the colour depth, the speaker and Bluetooth
  const bool want = genmode::running();
  if (want != lowColor && (int32_t)(millis() - colorRetryAt) >= 0) setLowColor(want);

  if (want && !genLatched) {  // Generated mode just started: decide what to switch off
    genLatched = true;
    genStoppedAt = 0;
    // Bluetooth stays if it is switched on (even if nothing has connected yet) or connected:
    // that is how the songs get to a better-sounding device.
    const bool bleKeep = settings.bleMidi || midi::bleState() == midi::LinkState::Connected;
    const bool usbConn = midi::usbState() == midi::LinkState::Connected;
    const bool midiOut = bleKeep || usbConn;
    genSynthOff = midiOut || !settings.speaker;
    if (genSynthOff) synth::pause();
    if (!bleKeep && genBle == 0) {
      genBle = 1;
      if (xTaskCreatePinnedToCore(bleUnloadTask, "bleoff", 8192, nullptr, 1, nullptr, 0) != pdPASS) genBle = 0;
    }
    if (bleKeep) snprintf(genNote, sizeof(genNote), "MIDI out: speaker off");
    else if (usbConn) snprintf(genNote, sizeof(genNote), "USB MIDI: speaker, Bluetooth off");
    else if (settings.speaker) snprintf(genNote, sizeof(genNote), "speaker: Bluetooth off (fn+b = MIDI)");
    else snprintf(genNote, sizeof(genNote), "no sound: speaker and MIDI off");
  } else if (!want && genLatched) {  // switched off: give things back
    if (genSynthOff) synth::resume();
    genSynthOff = false;
    genLatched = false;
    genNote[0] = 0;
  }
  // Bluetooth comes back once the screen is back to full colour (the screen needs one big
  // block of memory, so Bluetooth goes last), or after 8 s regardless.
  if (!want && genBle == 2) {
    if (!genStoppedAt) genStoppedAt = millis();
    if (!lowColor || millis() - genStoppedAt > 8000) {
      midi::resumeBle();
      midi::setBleEnabled(settings.bleMidi);  // (as above: the setting may have changed meanwhile)
      applyBtAuto();
      genBle = 0;
      genStoppedAt = 0;
    }
  }
}

static void cloudMemoryTick() {
  if (cloud::needsMemory() && !jobMode) {
    jobMode = true;
    if (genmode::running()) genmode::stop();  // its song building needs the same memory
    synth::pause();
    setLowColor(true);
    cloud::memoryReady();  // the job's worker is created only now
  } else if (jobMode && cloud::finishing()) {
    setLowColor(false);
    synth::resume();
    jobMode = false;
    settingsSave();  // a new login / WiFi should survive a power-off right away
    cloud::restored();
  }
}

void begin() {
  adv = M5.getBoard() == m5::board_t::board_M5CardputerADV;
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  for (uint8_t i = 0; i < 9; i++) {
    char cap[2] = {(char)('1' + i), 0};
    idxDigit[i] = findCap(cap);
  }
  idxDigit[9] = findCap("0");
  idxB = findCap("b");
  idxU = findCap("u");
  idxM = findCap("m");
  idxP = findCap("p");
  idxS = findCap("s");
  idxT = findCap("t");
  idxX = findCap("x");
  idxC = findCap("c");
  idxW = findCap("w");
  idxG = findCap("g");
  applyBtAuto();  // reconnect the remembered Bluetooth device
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
  cloudMemoryTick();
  genColorTick();
  genmode::tick();
  watchRemote();
  midi::LinkState b = midi::bleState(), u = midi::usbState();
  if (b != lastBle || u != lastUsb) {
    if (b == midi::LinkState::Connected && lastBle != b) toast("Bluetooth MIDI connected");
    if (u == midi::LinkState::Connected && lastUsb != u) toast("USB MIDI connected");
    lastBle = b;
    lastUsb = u;
    dirty = true;
  }
  if (repeatKey >= 0 && (int32_t)(now - repeatAt) >= 0) {
    if (keys::isDown((uint8_t)repeatKey) && !menuHeld && !fnHeld) {
      seq::Lock l;
      current->onAction(repeatAction);
      dirty = true;
      repeatAt = now + REPEAT_RATE_MS;
    } else {
      repeatKey = -1;
    }
  }
  static uint32_t lastAnim = 0;
  if (current->animating() && now - lastAnim > 200) {
    lastAnim = now;
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
