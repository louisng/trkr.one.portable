#pragma once
#include <M5Cardputer.h>
#include <stdint.h>

#include "../play/LivePlayer.h"

namespace ui {

static constexpr int W = 240;
static constexpr int H = 135;
static constexpr int STATUS_H = 14;

// RGB565 palette
namespace col {
static constexpr uint16_t BG = 0x0861;        // near-black blue
static constexpr uint16_t BAR = 0x18E5;       // status bar
static constexpr uint16_t TEXT = 0xE73C;
static constexpr uint16_t DIM = 0x7BEF;
static constexpr uint16_t FAINT = 0x31A6;
static constexpr uint16_t ACCENT = 0xFEA0;    // amber / highlight
static constexpr uint16_t OK = 0x3666;        // green
static constexpr uint16_t DRUM = 0xC2E3;      // orange
static constexpr uint16_t CHORD = 0x7A99;     // purple
static constexpr uint16_t WHITE_KEY = 0xDEFB;
static constexpr uint16_t BLACK_KEY = 0x2124;
static constexpr uint16_t ACTION = 0x39E8;
static constexpr uint16_t HELD = 0xFEC0;      // bright yellow
static constexpr uint16_t SELECT = 0x2B5B;    // selection row
}  // namespace col

class Screen {
 public:
  virtual ~Screen() = default;
  virtual const char* title() const = 0;
  // Whether piano / chord / drum keys play notes while this screen is open
  // (and enter arms recording).
  virtual bool musicKeys() const { return true; }
  // Whether space plays / stops on this screen.
  virtual bool spaceIsPlay() const { return true; }
  virtual void onEnter() {}
  virtual void onLeave() {}
  // Action keys (arrows, enter, del, space) -- presses only.
  virtual void onAction(uint8_t action) {}
  // A musical key was played (not while live recording). Used for step entry.
  virtual void onPlayed(uint8_t key, const player::Played& p) {}
  // Every raw key event, before anything else handles it.
  virtual void onRawKey(uint8_t index, bool pressed) {}
  virtual void draw(M5Canvas& c) = 0;
  // True while the screen needs periodic redraws (blinking cursor etc.).
  virtual bool animating() const { return false; }
};

Screen* liveScreen();
Screen* stepScreen();
Screen* trackerScreen();
Screen* songsScreen();
Screen* scenesScreen();
Screen* arrangeScreen();
Screen* synthScreen();
Screen* drumMidiScreen();
Screen* connectScreen();
void songsStartSaveAs();  // call before switching to songsScreen()
Screen* setupScreen();
Screen* keyTestScreen();

// Shared helpers (App.cpp)
void toast(const char* msg);
void goTo(Screen* s);
// Start / stop reconnecting to the remembered Bluetooth device per settings.
void applyBtAuto();  // switch screens from inside a screen / panel
void markDirty();
bool isAdv();

}  // namespace ui
