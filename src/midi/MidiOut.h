#pragma once
// MIDI output fan-out: every message goes to each enabled transport
// (BLE MIDI and/or USB MIDI). Transports can be switched on/off at runtime.
#include <stdint.h>

namespace midi {

enum class LinkState : uint8_t {
  Off,        // transport disabled by the user
  Waiting,    // enabled, nothing connected (advertising / not mounted)
  Connected,  // enabled and a host is connected
};

void beginUsb(bool enabled);
void beginBle(bool enabled);
void update();  // call every loop: flushes BLE packets, tracks connection changes

void setBleEnabled(bool on);
void setUsbEnabled(bool on);
bool bleEnabled();
bool usbEnabled();
LinkState bleState();
LinkState usbState();

// Channels are 1-16.
void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void noteOff(uint8_t channel, uint8_t note);
void controlChange(uint8_t channel, uint8_t cc, uint8_t value);
void pitchBend(uint8_t channel, uint16_t value14);  // 0-16383, 8192 = centre
void programChange(uint8_t channel, uint8_t program);
void allNotesOff();  // CC123 on every channel, on every enabled transport

// Counters for the status display.
uint32_t messagesSent();

}  // namespace midi
