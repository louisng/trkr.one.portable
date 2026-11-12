#pragma once
// BLE MIDI peripheral (Apple / MMA "MIDI over Bluetooth LE" spec) on NimBLE.
// Messages are queued and sent as one notification per update() so a chord
// goes out in a single packet.
#include <stdint.h>

namespace blemidi {

void begin(const char* deviceName);  // initialises the stack, does not advertise
void shutdown();                     // unload the whole stack (frees RAM); begin() brings it back
void setEnabled(bool on);            // start/stop advertising, drop connections
bool enabled();
bool connected();
void send(uint8_t status, uint8_t d1, uint8_t d2);
void flush();

}  // namespace blemidi
