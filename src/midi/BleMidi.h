#pragma once
// BLE MIDI (Apple / MMA "MIDI over Bluetooth LE" spec) on the core's BLE stack.
//
// Peripheral: advertises as "Cardputer MIDI" for phones / computers to connect to.
// Central: can also scan for and connect out to a BLE MIDI device that only
// waits to be connected to. MIDI goes to both when both are connected.
// Messages are queued and sent as one packet per flush() so a chord goes out together.
#include <stdint.h>

namespace blemidi {

void begin(const char* deviceName);  // initialises the stack, does not advertise
void setEnabled(bool on);            // start/stop advertising, drop connections
bool enabled();
bool connected();                    // anything to send to (either role)
void send(uint8_t status, uint8_t d1, uint8_t d2);
void send2(uint8_t status, uint8_t d1);  // 2-byte messages (program change)
void send1(uint8_t status);              // realtime (clock, start, stop)
void flush();

// ── Connecting out to a device ───────────────────────────────────────────────
struct Found {
  char name[24];
  char addr[18];     // "aa:bb:cc:dd:ee:ff"
  uint8_t addrType;
  int8_t rssi;
  bool midi;         // advertises the BLE MIDI service
};
enum class RemoteState : uint8_t { Idle, Connecting, Connected, Failed };

void startScan(uint8_t seconds);              // runs in the background
bool scanning();
uint8_t scanResults(Found* out, uint8_t max);  // MIDI devices first, then strongest
void connectTo(const Found& f);               // runs in the background
void disconnectRemote();                      // also stops auto-reconnect
// Keep reconnecting to this device whenever it isn't connected (nullptr = stop).
void setAutoReconnect(const Found* f);
RemoteState remoteState();
bool remoteConnected();
const char* remoteName();
Found connectedDevice();  // valid while remoteConnected()
bool droppedByUser();     // the last disconnect was disconnectRemote()
const char* lastError();

}  // namespace blemidi
