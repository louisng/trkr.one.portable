#pragma once
// BLE MIDI peripheral (Apple / MMA "MIDI over Bluetooth LE" spec) on NimBLE.
// Messages are queued and sent as one notification per update() so a chord
// goes out in a single packet.
#include <stdint.h>

namespace blemidi {

void begin(const char* deviceName);  // initialises the stack, does not advertise
void setEnabled(bool on);            // start/stop advertising, drop connections
bool enabled();
bool connected();
void send(uint8_t status, uint8_t d1, uint8_t d2);
void send2(uint8_t status, uint8_t d1);  // 2-byte messages (program change)
void send1(uint8_t status);              // realtime (clock, start, stop)
void flush();

}  // namespace blemidi
