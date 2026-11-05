#pragma once
// User settings, persisted to NVS flash.
#include <stdint.h>

struct Settings {
  bool bleMidi = true;
  bool usbMidi = true;
  bool speaker = true;     // play a simple preview tone on the built-in speaker
  uint8_t volume = 96;     // speaker volume 0-255
  uint8_t velocity = 100;
  uint8_t root = 0;        // key root pitch class, 0 = C
  uint8_t scale = 0;       // theory::SCALE index
  bool pianoScaleMode = false;  // false: chromatic piano; true: white keys = scale degrees
  int8_t octave = 4;       // piano octave (C4 = 60); chords play one octave lower
  uint8_t metronome = 1;   // 0 off, 1 while recording, 2 always
  bool quantize = true;    // snap live-recorded notes to steps
  bool upperPiano = false; // top two rows = second piano octave (fn+p)
  char lastSong[17] = "";  // song loaded at power-on
  bool clockOut = true;    // send MIDI clock + start/stop while playing
  bool lastSongOnSd = false;  // lastSong lives on the SD card
  // New fields go at the end: older saved settings still load (the new
  // fields keep their defaults).
};

extern Settings settings;

void settingsLoad();
void settingsSave();  // only writes when something changed
