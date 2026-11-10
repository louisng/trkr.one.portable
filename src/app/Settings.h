#pragma once
// User settings, persisted to NVS flash.
#include <stdint.h>

struct Settings {
  bool bleMidi = true;
  bool usbMidi = true;
  bool speaker = true;     // play a simple preview tone on the built-in speaker
  uint8_t volume = 96;     // speaker volume 0-255
  uint8_t pianoCh = 1;     // MIDI channels 1-16
  uint8_t chordCh = 2;
  uint8_t drumCh = 10;
  uint8_t velocity = 100;
  uint8_t root = 0;        // key root pitch class, 0 = C
  uint8_t scale = 0;       // theory::SCALE index
  bool pianoScaleMode = false;  // false: chromatic piano; true: white keys = scale degrees
  int8_t octave = 4;       // piano octave (C4 = 60); chords play one octave lower
  // Generated mode
  uint8_t genGenre = 0;    // selected genre (index into gen::GENRE_LIST)
  uint8_t genAlong = 1;    // play-along mode: 0 free, 1 chord tones, 2 snap to chord, 3 scale
  uint16_t genMuted = 0;   // bit n = generated track n is muted (kept between songs)
};

extern Settings settings;

void settingsLoad();
void settingsSave();  // only writes when something changed
