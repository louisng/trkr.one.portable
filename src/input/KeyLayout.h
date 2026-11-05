#pragma once
// Physical key layout -> musical / UI role.
//
// Both Cardputer models report keys on the same 4 x 14 grid (the M5Cardputer
// library remaps the ADV's TCA8418 events onto the original matrix layout),
// so this one table drives both boards.
//
//   Row 0  `  1  2  3  4  5  6  7  8  9  0  -  =  del   drum pads 1-12
//   Row 1  tab q  w  e  r  t  y  u  i  o  p  [  ]  \    chord buttons
//   Row 2  fn sh a  s  d  f  g  h  j  k  l  ;  '  ent   piano black keys (+ a/k octave, f/l length, ' options)
//   Row 3  ct op al z  x  c  v  b  n  m  ,  .  /  spc   piano white keys
//
// The printed arrow keys (; , . /) are dedicated cursor keys: no fn needed.
#include <stdint.h>

namespace layout {

static constexpr uint8_t ROWS = 4;
static constexpr uint8_t COLS = 14;
static constexpr uint8_t KEY_COUNT = ROWS * COLS;

enum class Role : uint8_t {
  None,
  Drum,    // value = pad index 0-11
  Chord,   // value = chord button index 0-12
  Piano,   // value = semitones above the bottom rows' C (0-11 bottom rows, 12-33 upper piano)
  Action,  // value = Action
};

enum Action : uint8_t {
  ACT_NONE,
  ACT_MENU,       // ` : hold for screen menu, tap for back
  ACT_DELETE,     // del
  ACT_INVERSION,  // tab: cycle chord inversion
  ACT_OCT_DOWN,   // a
  ACT_OCT_UP,     // k
  ACT_UP,         // ;
  ACT_DOWN,       // .
  ACT_LEFT,       // ,
  ACT_RIGHT,      // /
  ACT_ENTER,      // enter: record (phase 2) / confirm
  ACT_PLAY,       // space: play / stop (phase 2)
  ACT_FN,
  ACT_SHIFT,      // chord modifier: add 7th
  ACT_CTRL,       // chord modifier: add 9th
  ACT_OPT,        // chord modifier: swap major/minor
  ACT_ALT,        // chord modifier: sus4
  ACT_SHORTER,    // f: shorten the note just entered / under the cursor
  ACT_LONGER,     // l: lengthen it
  ACT_OPTIONS,    // ': pattern / track options panel
  ACT_FREE,       // unassigned
};

struct KeyDef {
  Role role;
  uint8_t value;
  const char* label;  // short label for the on-screen key map (<= 3 chars)
};

// Index = row * COLS + col
const KeyDef& keyAt(uint8_t row, uint8_t col);
inline const KeyDef& keyAt(uint8_t index) { return keyAt(index / COLS, index % COLS); }

// Upper-piano mode: top two rows become a second piano octave instead of
// drum pads and chord buttons.
void setUpperPiano(bool on);
bool upperPiano();

// Printable character for a key (for the key-test screen).
const char* keyCapLabel(uint8_t row, uint8_t col);

}  // namespace layout
