#include "BleMidi.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>

// Uses the BLE library that ships with the ESP32 Arduino core (NimBLE-based in
// core 3.3+). An earlier build linked the separate NimBLE-Arduino library on
// top of the core's own NimBLE stack, which is a likely cause of crashes at boot.

namespace blemidi {

static const char* MIDI_SERVICE_UUID = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char* MIDI_CHAR_UUID = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

static BLEServer* server = nullptr;
static BLECharacteristic* midiChar = nullptr;
static bool isEnabled = false;
static volatile uint16_t peerMtu = 23;

// Outgoing packet: [header][ts][status][d1][d2][ts][status][d1][d2]...
static uint8_t pkt[128];
static size_t pktLen = 0;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {}
  void onDisconnect(BLEServer*) override { peerMtu = 23; }
#if defined(CONFIG_NIMBLE_ENABLED)
  void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t mtu) override { peerMtu = mtu; }
#endif
};

static ServerCallbacks serverCallbacks;

void begin(const char* deviceName) {
  if (server) return;
  BLEDevice::init(deviceName);
  BLEDevice::setMTU(128);
  server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  server->advertiseOnDisconnect(false);  // only advertise while enabled

  BLEService* svc = server->createService(MIDI_SERVICE_UUID);
  midiChar = svc->createCharacteristic(MIDI_CHAR_UUID, BLECharacteristic::PROPERTY_READ |
                                                           BLECharacteristic::PROPERTY_WRITE_NR |
                                                           BLECharacteristic::PROPERTY_NOTIFY);
  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setFlags(0x06);  // general discoverable, BR/EDR not supported
  advData.setCompleteServices(BLEUUID(MIDI_SERVICE_UUID));
  BLEAdvertisementData scanData;  // the name goes in the scan response (adv packet is full)
  scanData.setName(deviceName);
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
}

void setEnabled(bool on) {
  if (!server) return;
  isEnabled = on;
  server->advertiseOnDisconnect(on);
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  if (on) {
    if (server->getConnectedCount() == 0) adv->start();
  } else {
    adv->stop();
    for (auto& peer : server->getPeerDevices(false)) server->disconnect(peer.first);
    pktLen = 0;
  }
}

bool enabled() { return isEnabled; }
bool connected() { return isEnabled && server && server->getConnectedCount() > 0; }

void flush() {
  if (pktLen > 1 && connected()) {
    midiChar->setValue(pkt, pktLen);
    midiChar->notify();
  }
  pktLen = 0;
}

void send(uint8_t status, uint8_t d1, uint8_t d2) {
  if (!connected()) return;
  size_t maxPayload = peerMtu > 3 ? peerMtu - 3 : 20;
  if (maxPayload > sizeof(pkt)) maxPayload = sizeof(pkt);
  if (pktLen + 4 > maxPayload) flush();
  uint16_t ts = (uint16_t)(millis() & 0x1FFF);  // 13-bit ms timestamp
  if (pktLen == 0) pkt[pktLen++] = 0x80 | ((ts >> 7) & 0x3F);
  pkt[pktLen++] = 0x80 | (ts & 0x7F);
  pkt[pktLen++] = status;
  pkt[pktLen++] = d1 & 0x7F;
  pkt[pktLen++] = d2 & 0x7F;
}

}  // namespace blemidi
