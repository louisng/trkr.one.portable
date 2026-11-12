#include "BleMidi.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

namespace blemidi {

static const char* MIDI_SERVICE_UUID = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char* MIDI_CHAR_UUID = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

static NimBLEServer* server = nullptr;
static NimBLECharacteristic* midiChar = nullptr;
static bool isEnabled = false;
static volatile bool subscribed = false;  // host has enabled notifications
static volatile uint16_t peerMtu = 23;

// Outgoing packet: [header][ts][status][d1][d2][ts][status][d1][d2]...
static uint8_t pkt[128];
static size_t pktLen = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    // Ask for a short connection interval (7.5-15 ms) for low latency.
    s->updateConnParams(info.getConnHandle(), 6, 12, 0, 200);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    subscribed = false;
    peerMtu = 23;
    // NimBLE restarts advertising automatically (advertiseOnDisconnect).
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { peerMtu = mtu; }
};

class CharCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
    subscribed = (subValue & 0x0001) != 0;
  }
  // Incoming MIDI (onWrite) is ignored for now; MIDI clock-in comes later.
};

static ServerCallbacks serverCallbacks;
static CharCallbacks charCallbacks;

void begin(const char* deviceName) {
  if (server) return;
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(128);
  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);

  NimBLEService* svc = server->createService(MIDI_SERVICE_UUID);
  midiChar = svc->createCharacteristic(
      MIDI_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  midiChar->setCallbacks(&charCallbacks);
  uint8_t empty[1] = {0};
  midiChar->setValue(empty, 0);
  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(MIDI_SERVICE_UUID);
  NimBLEAdvertisementData scanData;  // name lives in the scan response (adv packet is full)
  scanData.setName(deviceName);
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
}

void shutdown() {
  if (!server) return;
  isEnabled = false;  // the sending side checks these first
  subscribed = false;
  pktLen = 0;
  server = nullptr;
  midiChar = nullptr;
  NimBLEDevice::deinit(true);
}

void setEnabled(bool on) {
  if (!server) return;
  isEnabled = on;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (on) {
    server->advertiseOnDisconnect(true);
    if (!adv->isAdvertising() && server->getConnectedCount() == 0) adv->start();
  } else {
    server->advertiseOnDisconnect(false);
    adv->stop();
    for (uint16_t handle : server->getPeerDevices()) server->disconnect(handle);
    subscribed = false;
    pktLen = 0;
  }
}

bool enabled() { return isEnabled; }
bool connected() { return isEnabled && subscribed && server && server->getConnectedCount() > 0; }

static void writeTimestampedHeader() {
  uint16_t ts = (uint16_t)(millis() & 0x1FFF);  // 13-bit ms timestamp
  pkt[0] = 0x80 | ((ts >> 7) & 0x3F);
  pktLen = 1;
}

void flush() {
  if (pktLen > 1 && connected()) midiChar->notify(pkt, pktLen);
  pktLen = 0;
}

void send(uint8_t status, uint8_t d1, uint8_t d2) {
  if (!connected()) return;
  size_t maxPayload = peerMtu > 3 ? peerMtu - 3 : 20;
  if (maxPayload > sizeof(pkt)) maxPayload = sizeof(pkt);
  if (pktLen + 4 > maxPayload) flush();
  if (pktLen == 0) writeTimestampedHeader();
  uint16_t ts = (uint16_t)(millis() & 0x1FFF);
  pkt[pktLen++] = 0x80 | (ts & 0x7F);
  pkt[pktLen++] = status;
  pkt[pktLen++] = d1 & 0x7F;
  pkt[pktLen++] = d2 & 0x7F;
}

}  // namespace blemidi
