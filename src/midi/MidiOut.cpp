#include "MidiOut.h"

#include <Arduino.h>
#include <USB.h>
#include <USBMIDI.h>

#include "BleMidi.h"

// Must be a global so its interface is registered before the USB stack starts
// (with ARDUINO_USB_CDC_ON_BOOT the core starts USB before setup()).
static USBMIDI usbMidi("Cardputer MIDI");

namespace midi {

static bool usbOn = false;
static uint32_t sentCount = 0;

static bool usbMounted() { return (bool)USB; }

void begin(bool bleEnabled, bool usbEnabled) {
  usbMidi.begin();
  usbOn = usbEnabled;
  blemidi::begin("Cardputer MIDI");
  blemidi::setEnabled(bleEnabled);
}

void update() { blemidi::flush(); }

void setBleEnabled(bool on) {
  if (!on && blemidi::connected()) {
    for (uint8_t ch = 0; ch < 16; ch++) blemidi::send(0xB0 | ch, 123, 0);
    blemidi::flush();
  }
  blemidi::setEnabled(on);
}

static bool blePaused = false, bleWasOn = false;

void pauseBle() {
  if (blePaused) return;
  bleWasOn = blemidi::enabled();
  if (blemidi::connected()) {
    for (uint8_t ch = 0; ch < 16; ch++) blemidi::send(0xB0 | ch, 123, 0);
    blemidi::flush();
  }
  blemidi::shutdown();
  blePaused = true;
}

void resumeBle() {
  if (!blePaused) return;
  blePaused = false;
  blemidi::begin("Cardputer MIDI");
  blemidi::setEnabled(bleWasOn);
}

void setUsbEnabled(bool on) {
  if (!on && usbOn && usbMounted()) {
    for (uint8_t ch = 1; ch <= 16; ch++) usbMidi.controlChange(123, 0, ch);
  }
  usbOn = on;
}

bool bleEnabled() { return blemidi::enabled(); }
bool usbEnabled() { return usbOn; }

LinkState bleState() {
  if (!blemidi::enabled()) return LinkState::Off;
  return blemidi::connected() ? LinkState::Connected : LinkState::Waiting;
}

LinkState usbState() {
  if (!usbOn) return LinkState::Off;
  return usbMounted() ? LinkState::Connected : LinkState::Waiting;
}

static void send3(uint8_t status, uint8_t d1, uint8_t d2) {
  sentCount++;
  blemidi::send(status, d1, d2);  // no-op unless enabled + connected
  if (usbOn && usbMounted()) {
    uint8_t cin = status >> 4;
    midiEventPacket_t p = {cin, status, d1, d2};
    usbMidi.writePacket(&p);
  }
}

void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  send3(0x90 | ((channel - 1) & 0x0F), note & 0x7F, velocity & 0x7F);
}

void noteOff(uint8_t channel, uint8_t note) {
  send3(0x80 | ((channel - 1) & 0x0F), note & 0x7F, 0);
}

void controlChange(uint8_t channel, uint8_t cc, uint8_t value) {
  send3(0xB0 | ((channel - 1) & 0x0F), cc & 0x7F, value & 0x7F);
}

void allNotesOff() {
  for (uint8_t ch = 1; ch <= 16; ch++) controlChange(ch, 123, 0);
  blemidi::flush();
}

uint32_t messagesSent() { return sentCount; }

}  // namespace midi
