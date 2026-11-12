#include "BleMidi.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include <algorithm>
#include <vector>

// Uses the BLE library that ships with the ESP32 Arduino core (NimBLE-based in
// core 3.3+). An earlier build linked the separate NimBLE-Arduino library on
// top of the core's own NimBLE stack, which is a likely cause of crashes at boot.
//
// Two roles at once:
//   peripheral  the Cardputer advertises as "Cardputer MIDI" and phones / computers
//               connect to it (they subscribe to notifications)
//   central     the Cardputer scans for, and connects out to, a BLE MIDI device
//               that only waits to be connected to (we write to it)
// Every MIDI packet goes to both when both are connected.

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

// ── Central (outgoing connection) state ──────────────────────────────────────
static BLEClient* client = nullptr;
static BLERemoteCharacteristic* remoteChar = nullptr;
static volatile bool remoteReady = false;   // connected and the MIDI characteristic found
static SemaphoreHandle_t remoteMtx = nullptr;  // guards remoteChar between writers and reconnects
static volatile RemoteState rstate = RemoteState::Idle;
static char remoteNameBuf[24] = "";
static Found connectedDev{};  // the device we're connected to (for remembering it)
static char errorBuf[40] = "";

static SemaphoreHandle_t foundMtx = nullptr;
static std::vector<Found> found;
static volatile bool isScanning = false;

static TaskHandle_t worker = nullptr;
static volatile bool workerStop = false, workerAlive = false;
static Found target;                 // device the worker should connect to
static volatile bool connectRequested = false;
static Found autoTarget;             // reconnected to automatically while not connected
static volatile bool autoOn = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {}
  void onDisconnect(BLEServer*) override { peerMtu = 23; }
#if defined(CONFIG_NIMBLE_ENABLED)
  void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t mtu) override { peerMtu = mtu; }
#endif
};

class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient*) override {}
  void onDisconnect(BLEClient*) override {
    remoteReady = false;  // the characteristic stays allocated until the next connect
    if (rstate == RemoteState::Connected) rstate = RemoteState::Idle;
  }
};

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    const bool midi = d.haveServiceUUID() && d.isAdvertisingService(BLEUUID(MIDI_SERVICE_UUID));
    if (!midi && !d.haveName()) return;  // nameless non-MIDI devices are just noise
    Found f{};
    strncpy(f.name, d.haveName() ? d.getName().c_str() : "(no name)", sizeof(f.name) - 1);
    strncpy(f.addr, d.getAddress().toString().c_str(), sizeof(f.addr) - 1);
    f.addrType = d.getAddressType();
    f.rssi = (int8_t)d.getRSSI();
    f.midi = midi;
    xSemaphoreTake(foundMtx, portMAX_DELAY);
    bool dup = false;
    for (auto& e : found) dup |= strcmp(e.addr, f.addr) == 0;
    if (!dup && found.size() < 24) found.push_back(f);
    xSemaphoreGive(foundMtx);
  }
};

static ServerCallbacks serverCallbacks;
static ClientCallbacks clientCallbacks;
static ScanCallbacks scanCallbacks;

static void setError(const char* msg) {
  strncpy(errorBuf, msg, sizeof(errorBuf) - 1);
  errorBuf[sizeof(errorBuf) - 1] = 0;
}

static volatile bool userDropped = false;  // last disconnect was asked for here

// Runs on the worker task: connecting blocks for up to a few seconds.
// `quiet` = background reconnect: failures don't show up as errors.
static void doConnect(const Found& t, bool quiet = false) {
  const RemoteState failState = quiet ? RemoteState::Idle : RemoteState::Failed;
  if (!quiet) rstate = RemoteState::Connecting;
  if (isScanning) BLEDevice::getScan()->stop();
  isScanning = false;

  xSemaphoreTake(remoteMtx, portMAX_DELAY);
  remoteReady = false;
  remoteChar = nullptr;
  xSemaphoreGive(remoteMtx);

  if (!client) {
    client = BLEDevice::createClient();
    client->setClientCallbacks(&clientCallbacks);
  }
  if (client->isConnected()) client->disconnect();

  if (!client->connect(BLEAddress(String(t.addr), t.addrType), t.addrType, 6000)) {
    if (!quiet) setError("Couldn't connect. Is it discoverable?");
    rstate = failState;
    return;
  }
  BLERemoteService* svc = client->getService(MIDI_SERVICE_UUID);
  BLERemoteCharacteristic* ch = svc ? svc->getCharacteristic(MIDI_CHAR_UUID) : nullptr;
  if (!ch || !(ch->canWriteNoResponse() || ch->canWrite())) {
    client->disconnect();
    if (!quiet) setError("That device has no BLE MIDI input");
    rstate = failState;
    return;
  }
  // Subscribe too: some devices only accept MIDI from centrals that listen.
  if (ch->canNotify()) ch->registerForNotify([](BLERemoteCharacteristic*, uint8_t*, size_t, bool) {});

  xSemaphoreTake(remoteMtx, portMAX_DELAY);
  remoteChar = ch;
  strncpy(remoteNameBuf, t.name, sizeof(remoteNameBuf) - 1);
  connectedDev = t;
  userDropped = false;
  remoteReady = true;
  xSemaphoreGive(remoteMtx);
  errorBuf[0] = 0;
  rstate = RemoteState::Connected;
}

static void workerTask(void*) {
  for (;;) {
    // Wake for a connect request, or every 10 s to retry the remembered device.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
    if (workerStop) break;
    if (!isEnabled) continue;
    if (connectRequested) {
      connectRequested = false;
      doConnect(target);
    } else if (autoOn && !remoteReady && !isScanning && rstate != RemoteState::Connecting &&
               autoTarget.addr[0]) {
      doConnect(autoTarget, true);
    }
    if (workerStop) break;
  }
  worker = nullptr;
  workerAlive = false;
  vTaskDelete(nullptr);
}

void begin(const char* deviceName) {
  if (server) return;
  if (!remoteMtx) remoteMtx = xSemaphoreCreateMutex();
  if (!foundMtx) foundMtx = xSemaphoreCreateMutex();
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

  workerStop = false;
  workerAlive = true;
  xTaskCreatePinnedToCore(workerTask, "blecentral", 6144, nullptr, 1, &worker, 0);
}

bool shutdown() {
  if (!server) return true;
  const bool wasEnabled = isEnabled;
  isEnabled = false;  // the sending side checks this first
  if (isScanning) BLEDevice::getScan()->stop();
  isScanning = false;
  connectRequested = false;
  workerStop = true;
  if (worker) xTaskNotifyGive(worker);
  // A connect attempt in progress can take up to ~6 s: wait for the worker to finish.
  for (int i = 0; i < 800 && workerAlive; i++) vTaskDelay(pdMS_TO_TICKS(10));
  if (workerAlive) {  // couldn't stop it safely: leave Bluetooth as it was
    workerStop = false;
    isEnabled = wasEnabled;
    return false;
  }
  if (remoteMtx) xSemaphoreTake(remoteMtx, portMAX_DELAY);
  remoteReady = false;
  remoteChar = nullptr;
  if (remoteMtx) xSemaphoreGive(remoteMtx);
  if (client && client->isConnected()) client->disconnect();
  BLEDevice::getAdvertising()->stop();
  for (auto& peer : server->getPeerDevices(false)) server->disconnect(peer.first);
  vTaskDelay(pdMS_TO_TICKS(150));  // let the disconnects complete
  rstate = RemoteState::Idle;
  pktLen = 0;
  client = nullptr;
  midiChar = nullptr;
  server = nullptr;
  BLEDevice::deinit(false);  // false: free the memory but allow init() again
  return true;
}

void setEnabled(bool on) {
  if (!server) return;
  isEnabled = on;
  server->advertiseOnDisconnect(on);
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  if (on) {
    if (server->getConnectedCount() == 0) adv->start();
    if (worker) xTaskNotifyGive(worker);  // reconnect the remembered device soon
  } else {
    adv->stop();
    for (auto& peer : server->getPeerDevices(false)) server->disconnect(peer.first);
    disconnectRemote();
    pktLen = 0;
  }
}

bool enabled() { return isEnabled; }
static bool serverConnected() { return server && server->getConnectedCount() > 0; }
bool connected() { return isEnabled && (serverConnected() || remoteReady); }

void flush() {
  if (pktLen > 1 && isEnabled) {
    if (serverConnected()) {
      midiChar->setValue(pkt, pktLen);
      midiChar->notify();
    }
    if (remoteReady && xSemaphoreTake(remoteMtx, 0) == pdTRUE) {  // never wait on a reconnect
      if (remoteReady && remoteChar) remoteChar->writeValue(pkt, pktLen, false);
      xSemaphoreGive(remoteMtx);
    }
  }
  pktLen = 0;
}

static void sendBytes(const uint8_t* msg, uint8_t n) {
  if (!connected()) return;
  size_t maxPayload = peerMtu > 3 ? peerMtu - 3 : 20;
  if (remoteReady) maxPayload = 20;  // safe for any device we connected out to
  if (maxPayload > sizeof(pkt)) maxPayload = sizeof(pkt);
  if (pktLen + 1 + n > maxPayload) flush();
  uint16_t ts = (uint16_t)(millis() & 0x1FFF);
  if (pktLen == 0) pkt[pktLen++] = 0x80 | ((ts >> 7) & 0x3F);
  pkt[pktLen++] = 0x80 | (ts & 0x7F);
  for (uint8_t i = 0; i < n; i++) pkt[pktLen++] = i == 0 ? msg[0] : (msg[i] & 0x7F);
}

void send1(uint8_t status) { sendBytes(&status, 1); }

void send2(uint8_t status, uint8_t d1) {
  uint8_t m[2] = {status, d1};
  sendBytes(m, 2);
}

void send(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t m[3] = {status, d1, d2};
  sendBytes(m, 3);
}

// ── Central API ──────────────────────────────────────────────────────────────
static void scanDone(BLEScanResults) { isScanning = false; }

void startScan(uint8_t seconds) {
  if (!server || !isEnabled || isScanning || rstate == RemoteState::Connecting) return;
  xSemaphoreTake(foundMtx, portMAX_DELAY);
  found.clear();
  xSemaphoreGive(foundMtx);
  BLEScan* s = BLEDevice::getScan();
  s->setAdvertisedDeviceCallbacks(&scanCallbacks, false);
  s->setActiveScan(true);  // ask for scan responses: that's where most names are
  s->setInterval(100);
  s->setWindow(90);
  s->clearResults();
  isScanning = true;
  if (!s->start(seconds, scanDone, false)) isScanning = false;
}

bool scanning() { return isScanning; }

uint8_t scanResults(Found* out, uint8_t max) {
  if (!foundMtx) return 0;
  xSemaphoreTake(foundMtx, portMAX_DELAY);
  // MIDI devices first, then by signal strength.
  std::vector<Found> sorted = found;
  xSemaphoreGive(foundMtx);
  std::sort(sorted.begin(), sorted.end(), [](const Found& a, const Found& b) {
    return a.midi != b.midi ? a.midi : a.rssi > b.rssi;
  });
  uint8_t n = 0;
  for (auto& f : sorted)
    if (n < max) out[n++] = f;
  return n;
}

void connectTo(const Found& f) {
  if (!worker || !isEnabled) return;
  target = f;
  connectRequested = true;
  rstate = RemoteState::Connecting;
  xTaskNotifyGive(worker);
}

void disconnectRemote() {
  autoOn = false;
  userDropped = true;
  if (remoteMtx) {
    xSemaphoreTake(remoteMtx, portMAX_DELAY);
    remoteReady = false;
    xSemaphoreGive(remoteMtx);
  }
  if (client && client->isConnected()) client->disconnect();
  rstate = RemoteState::Idle;
}

void setAutoReconnect(const Found* f) {
  if (f && f->addr[0]) {
    autoTarget = *f;
    autoOn = true;
    if (worker) xTaskNotifyGive(worker);
  } else {
    autoOn = false;
  }
}

RemoteState remoteState() { return rstate; }
bool remoteConnected() { return remoteReady; }
const char* remoteName() { return remoteNameBuf; }
Found connectedDevice() { return connectedDev; }
bool droppedByUser() { return userDropped; }
const char* lastError() { return errorBuf; }

}  // namespace blemidi
