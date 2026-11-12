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
// Unload / reload the Bluetooth stack to free RAM for WiFi. resume() restores
// whatever on/off state Bluetooth MIDI had.
void pauseBle();
void resumeBle();
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
// System realtime: 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop.
void realtime(uint8_t byte);
static constexpr uint8_t RT_CLOCK = 0xF8, RT_START = 0xFA, RT_STOP = 0xFC;
void allNotesOff();  // CC123 on every channel, on every enabled transport

// Counters for the status display.
uint32_t messagesSent();

}  // namespace midi
