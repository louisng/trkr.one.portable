#include "MidiOut.h"

#include <Arduino.h>
#include "BleMidi.h"

// USB MIDI needs the TinyUSB device stack (ARDUINO_USB_MODE=0). The debug
// build uses the chip's hardware USB serial instead, so it has no USB MIDI.
#define HAS_USB_MIDI (ARDUINO_USB_MODE == 0)

#if HAS_USB_MIDI
#include <USB.h>
#include <USBMIDI.h>
// Must be a global so its interface is registered before the USB stack starts
// (with ARDUINO_USB_CDC_ON_BOOT the core starts USB before setup()).
static USBMIDI usbMidi("Cardputer MIDI");
#endif

namespace midi {

static bool usbOn = false;
static uint32_t sentCount = 0;

#if HAS_USB_MIDI
static bool usbMounted() { return (bool)USB; }
#else
static bool usbMounted() { return false; }
#endif

void beginUsb(bool enabled) {
#if HAS_USB_MIDI
  usbMidi.begin();
  usbOn = enabled;
#else
  (void)enabled;
#endif
}

void beginBle(bool enabled) {
  blemidi::begin("Cardputer MIDI");
  blemidi::setEnabled(enabled);
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
  blePaused = blemidi::shutdown();
}

void resumeBle() {
  if (!blePaused) return;
  blePaused = false;
  blemidi::begin("Cardputer MIDI");
  blemidi::setEnabled(bleWasOn);
}

void setUsbEnabled(bool on) {
  if (!on && usbOn && usbMounted()) {
#if HAS_USB_MIDI
    for (uint8_t ch = 1; ch <= 16; ch++) usbMidi.controlChange(123, 0, ch);
#endif
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
#if HAS_USB_MIDI
  if (usbOn && usbMounted()) {
    uint8_t cin = status >> 4;
    midiEventPacket_t p = {cin, status, d1, d2};
    usbMidi.writePacket(&p);
  }
#endif
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

void pitchBend(uint8_t channel, uint16_t v) {
  if (v > 16383) v = 16383;
  send3(0xE0 | ((channel - 1) & 0x0F), v & 0x7F, (v >> 7) & 0x7F);
}

void programChange(uint8_t channel, uint8_t program) {
  const uint8_t status = 0xC0 | ((channel - 1) & 0x0F);
  sentCount++;
  blemidi::send2(status, program & 0x7F);
#if HAS_USB_MIDI
  if (usbOn && usbMounted()) {
    midiEventPacket_t p = {0x0C, status, (uint8_t)(program & 0x7F), 0};
    usbMidi.writePacket(&p);
  }
#endif
}

void realtime(uint8_t byte) {
  blemidi::send1(byte);
#if HAS_USB_MIDI
  if (usbOn && usbMounted()) {
    midiEventPacket_t p = {0x0F, byte, 0, 0};  // CIN 0xF: single byte
    usbMidi.writePacket(&p);
  }
#endif
}

void allNotesOff() {
  for (uint8_t ch = 1; ch <= 16; ch++) controlChange(ch, 123, 0);
  blemidi::flush();
}

uint32_t messagesSent() { return sentCount; }

}  // namespace midi
