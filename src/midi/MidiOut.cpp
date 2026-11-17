#include "MidiOut.h"

#include <Arduino.h>
#include <esp_system.h>
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

// ── Bluetooth: really loaded / unloaded ─────────────────────────────────────
// (The stack holds ~56 KB even while "off" if it stays loaded, so off unloads it.)
static volatile bool bleLoaded = false;  // the stack is in memory
static volatile bool bleWanted = false;  // Bluetooth MIDI is switched on
static volatile bool bleBusy_ = false;   // loading / unloading right now
static volatile bool bleFailed = false;
static char bleErr[48] = "";
static bool blePaused = false;           // a cloud job has it unloaded
static constexpr uint32_t BLE_NEEDS_BYTES = 72000;  // it took ~56 KB at start-up; keep some in hand

static bool loadBleNow() {
  if (bleLoaded) {
    blemidi::setEnabled(true);
    return true;
  }
  const uint32_t freeNow = esp_get_free_heap_size();
  if (freeNow < BLE_NEEDS_BYTES) {  // a failed allocation here would crash the device
    snprintf(bleErr, sizeof(bleErr), "No RAM for Bluetooth: %uK free, needs %uK", (unsigned)(freeNow / 1024),
             (unsigned)(BLE_NEEDS_BYTES / 1024));
    return false;
  }
  blemidi::begin("Cardputer MIDI");
  blemidi::setEnabled(true);
  bleLoaded = true;
  return true;
}

static void unloadBleNow() {
  if (!bleLoaded) return;
  if (blemidi::connected()) {
    for (uint8_t ch = 0; ch < 16; ch++) blemidi::send(0xB0 | ch, 123, 0);
    blemidi::flush();
  }
  blemidi::setEnabled(false);  // stops advertising, drops connections
  if (blemidi::shutdown()) bleLoaded = false;  // (false: the connect worker couldn't be stopped safely: stays loaded, off)
}

static void bleSwitchTask(void* arg) {
  if ((intptr_t)arg) {
    if (!loadBleNow()) {
      bleWanted = false;
      bleFailed = true;
    }
  } else {
    unloadBleNow();
  }
  bleBusy_ = false;
  vTaskDelete(nullptr);
}

void beginBle(bool enabled) {  // at start-up: with Bluetooth off the stack isn't loaded at all
  bleWanted = enabled;
  if (enabled && !loadBleNow()) {
    bleWanted = false;
    bleFailed = true;
  }
}

void update() { blemidi::flush(); }

bool bleBusy() { return bleBusy_; }

const char* bleTakeError() {
  if (!bleFailed) return nullptr;
  bleFailed = false;
  return bleErr;
}

bool setBleEnabled(bool on) {
  if (bleBusy_) return false;
  bleWanted = on;
  if (blePaused) return true;  // applied when the cloud job gives it back
  if (on == (bool)bleLoaded) {
    if (on) blemidi::setEnabled(true);
    return true;
  }
  bleBusy_ = true;
  if (xTaskCreatePinnedToCore(bleSwitchTask, "bleswitch", 8192, (void*)(intptr_t)(on ? 1 : 0), 1, nullptr, 0) != pdPASS) {
    bleBusy_ = false;
    bleWanted = false;
    snprintf(bleErr, sizeof(bleErr), "No RAM for Bluetooth task");
    bleFailed = true;
    return false;
  }
  return true;
}

// For a cloud job (runs on the job's own task).
void pauseBle() {
  if (blePaused) return;
  for (int i = 0; i < 1500 && bleBusy_; i++) vTaskDelay(pdMS_TO_TICKS(10));  // a switch in progress
  blePaused = true;
  unloadBleNow();
}

void resumeBle() {
  if (!blePaused) return;
  blePaused = false;
  if (bleWanted && !loadBleNow()) {
    bleWanted = false;
    bleFailed = true;
  }
}

void setUsbEnabled(bool on) {
  if (!on && usbOn && usbMounted()) {
#if HAS_USB_MIDI
    for (uint8_t ch = 1; ch <= 16; ch++) usbMidi.controlChange(123, 0, ch);
#endif
  }
  usbOn = on;
}

bool bleEnabled() { return bleWanted; }
bool usbEnabled() { return usbOn; }

LinkState bleState() {
  if (bleBusy_) return LinkState::Waiting;
  if (!bleLoaded || !blemidi::enabled()) return LinkState::Off;
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
