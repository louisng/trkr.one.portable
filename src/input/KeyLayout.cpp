#include "KeyLayout.h"

namespace layout {

using R = Role;

// clang-format off
static const KeyDef LAYOUT[ROWS][COLS] = {
  // Row 0: ` 1 2 3 4 5 6 7 8 9 0 - = del
  { {R::Action, ACT_MENU, "MNU"},
    {R::Drum, 0, "KK"}, {R::Drum, 1, "SN"}, {R::Drum, 2, "RM"}, {R::Drum, 3, "CP"},
    {R::Drum, 4, "CH"}, {R::Drum, 5, "OH"}, {R::Drum, 6, "LT"}, {R::Drum, 7, "MT"},
    {R::Drum, 8, "HT"}, {R::Drum, 9, "CR"}, {R::Drum, 10, "RD"}, {R::Drum, 11, "CB"},
    {R::Action, ACT_DELETE, "DEL"} },
  // Row 1: tab q w e r t y u i o p [ ] backslash
  { {R::Action, ACT_INVERSION, "INV"},
    {R::Chord, 0, nullptr}, {R::Chord, 1, nullptr}, {R::Chord, 2, nullptr},
    {R::Chord, 3, nullptr}, {R::Chord, 4, nullptr}, {R::Chord, 5, nullptr},
    {R::Chord, 6, nullptr}, {R::Chord, 7, nullptr}, {R::Chord, 8, nullptr},
    {R::Chord, 9, nullptr}, {R::Chord, 10, nullptr}, {R::Chord, 11, nullptr},
    {R::Chord, 12, nullptr} },
  // Row 2: fn shift a s d f g h j k l ; ' enter
  { {R::Action, ACT_FN, "FN"}, {R::Action, ACT_SHIFT, "7TH"},
    {R::Action, ACT_OCT_DOWN, "OC-"},
    {R::Piano, 1, nullptr}, {R::Piano, 3, nullptr},
    {R::Action, ACT_FREE, ""},
    {R::Piano, 6, nullptr}, {R::Piano, 8, nullptr}, {R::Piano, 10, nullptr},
    {R::Action, ACT_OCT_UP, "OC+"},
    {R::Action, ACT_FREE, ""},
    {R::Action, ACT_UP, "^"},
    {R::Action, ACT_FREE, ""},
    {R::Action, ACT_ENTER, "REC"} },
  // Row 3: ctrl opt alt z x c v b n m , . / space
  { {R::Action, ACT_CTRL, "9TH"}, {R::Action, ACT_OPT, "M/m"}, {R::Action, ACT_ALT, "SUS"},
    {R::Piano, 0, nullptr}, {R::Piano, 2, nullptr}, {R::Piano, 4, nullptr},
    {R::Piano, 5, nullptr}, {R::Piano, 7, nullptr}, {R::Piano, 9, nullptr},
    {R::Piano, 11, nullptr},
    {R::Action, ACT_LEFT, "<"}, {R::Action, ACT_DOWN, "v"}, {R::Action, ACT_RIGHT, ">"},
    {R::Action, ACT_PLAY, "PLY"} },
};

static const char* const CAPS[ROWS][COLS] = {
  { "`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "del" },
  { "tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\\" },
  { "fn", "sh", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "ent" },
  { "ctl", "opt", "alt", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "spc" },
};
// clang-format on

static const KeyDef NONE_KEY = {R::None, 0, ""};

const KeyDef& keyAt(uint8_t row, uint8_t col) {
  if (row >= ROWS || col >= COLS) return NONE_KEY;
  return LAYOUT[row][col];
}

const char* keyCapLabel(uint8_t row, uint8_t col) {
  if (row >= ROWS || col >= COLS) return "";
  return CAPS[row][col];
}

}  // namespace layout
